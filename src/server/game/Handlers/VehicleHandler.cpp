/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file VehicleHandler.cpp
 * @brief 载具系统网络消息处理模块
 *
 * 本模块实现了游戏中载具（Vehicle）相关的所有网络消息处理器，包括：
 * - 离开受控载具
 * - 切换载具座位
 * - 进入玩家载具
 * - 弹出乘客
 * - 请求退出载具
 *
 * 载具系统说明：
 * - 载具可以是坐骑、战车、飞行器等各种可乘坐的游戏对象
 * - 玩家可以在载具上切换不同的座位
 * - 每个座位有不同的功能（驾驶、攻击、乘客等）
 * - 载具可以通过法术点击或交互进入
 *
 * 载具座位类型：
 * - 驾驶员座位：控制载具移动
 * - 武器座位：可以攻击
 * - 乘客座位：仅乘坐
 *
 * 安全考虑：
 * - 验证座位切换权限
 * - 验证乘客弹出权限
 * - 确保载具和座位的有效性
 */

#include "WorldSession.h"
#include "DBCStructure.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Vehicle.h"
#include "WorldPacket.h"

/**
 * @brief 处理解散受控载具的消息
 *
 * 当玩家离开一个受控制的载具时调用此函数
 * 处理玩家的移动信息更新并退出载具
 *
 * @param recvData 接收到的网络数据包，包含载具GUID和移动信息
 *
 * 调用时机：
 * - 玩家主动离开载具
 * - 载具被销毁
 * - 客户端发送 CMSG_DISMISS_CONTROLLED_VEHICLE 消息
 *
 * 处理流程：
 * 1. 检查玩家当前是否在载具中
 * 2. 读取客户端发送的载具GUID和移动信息
 * 3. 更新玩家的移动状态
 * 4. 执行退出载具操作
 *
 * 注意事项：
 * - 移动信息包含玩家的位置、朝向等数据
 * - 需要防止警告日志，使用 rfinish() 清空数据包
 */
void WorldSession::HandleDismissControlledVehicle(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_DISMISS_CONTROLLED_VEHICLE");

    // 获取玩家当前所乘载具的GUID
    ObjectGuid vehicleGUID = _player->GetCharmedGUID();

    // 如果玩家不在载具中，说明出现了异常情况
    if (!vehicleGUID)
    {
        // 清空数据包防止警告日志刷屏
        recvData.rfinish();
        return;
    }

    // 读取载具GUID（压缩格式）
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // 读取移动信息
    MovementInfo mi;
    mi.guid = guid;
    ReadMovementInfo(recvData, &mi);

    // 更新玩家的移动信息
    _player->m_movementInfo = mi;

    // 执行退出载具操作
    _player->ExitVehicle();
}

/**
 * @brief 处理在受控载具上切换座位的消息
 *
 * 当玩家在载具上切换座位时调用此函数
 * 支持多种座位切换操作，包括前一个座位、后一个座位、指定座位等
 *
 * @param recvData 接收到的网络数据包，内容根据操作码不同而变化
 *
 * 支持的操作码：
 * - CMSG_REQUEST_VEHICLE_PREV_SEAT：请求切换到前一个座位
 * - CMSG_REQUEST_VEHICLE_NEXT_SEAT：请求切换到后一个座位
 * - CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE：切换到指定载具的指定座位
 * - CMSG_REQUEST_VEHICLE_SWITCH_SEAT：请求切换到指定座位
 *
 * 验证流程：
 * 1. 检查玩家是否在载具上
 * 2. 检查当前座位是否允许切换
 * 3. 根据操作码执行相应的座位切换
 *
 * 座位切换规则：
 * - 某些座位不允许切换（如固定的驾驶员座位）
 * - 目标座位必须为空
 * - 可以切换到同一载具的其他座位
 * - 可以切换到另一个载具的座位
 *
 * 调用时机：
 * - 玩家在载具上点击座位切换按钮
 * - 玩家点击载具上的其他座位
 * - 玩家点击另一个载具的座位
 *
 * 安全措施：
 * - 验证当前座位的切换权限
 * - 验证目标座位的有效性
 * - 记录非法操作日志
 */
void WorldSession::HandleChangeSeatsOnControlledVehicle(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE");

    // 获取玩家所在载具的基础单位
    Unit* vehicle_base = GetPlayer()->GetVehicleBase();
    if (!vehicle_base)
    {
        // 玩家不在载具上，清空数据包防止警告
        recvData.rfinish();
        return;
    }

    // 获取玩家当前座位的信息
    VehicleSeatEntry const* seat = GetPlayer()->GetVehicle()->GetSeatForPassenger(GetPlayer());

    // 检查当前座位是否允许切换
    if (!seat->CanSwitchFromSeat())
    {
        recvData.rfinish();
        TC_LOG_ERROR("network", "HandleChangeSeatsOnControlledVehicle, Opcode: {}, Player {} tried to switch seats but current seatflags {} don't permit that.",
            recvData.GetOpcode(), GetPlayer()->GetGUID().ToString(), seat->Flags);
        return;
    }

    // 根据不同的操作码执行不同的座位切换操作
    switch (recvData.GetOpcode())
    {
        // 请求切换到前一个座位
        case CMSG_REQUEST_VEHICLE_PREV_SEAT:
            GetPlayer()->ChangeSeat(-1, false);  // -1表示自动查找，false表示前一个
            break;

        // 请求切换到后一个座位
        case CMSG_REQUEST_VEHICLE_NEXT_SEAT:
            GetPlayer()->ChangeSeat(-1, true);   // -1表示自动查找，true表示后一个
            break;

        // 在受控载具上切换座位（复杂操作，包含移动信息和目标载具）
        case CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE:
        {
            // 读取当前载具的GUID
            ObjectGuid guid;        // current vehicle guid
            recvData >> guid.ReadAsPacked();

            // 读取移动信息并更新载具的移动状态
            MovementInfo movementInfo;
            movementInfo.guid = guid;
            ReadMovementInfo(recvData, &movementInfo);
            vehicle_base->m_movementInfo = movementInfo;

            // 读取目标附属物（另一个载具或座位）的GUID
            ObjectGuid accessory;        // accessory guid
            recvData >> accessory.ReadAsPacked();

            // 读取目标座位ID
            int8 seatId;
            recvData >> seatId;

            // 验证载具GUID是否匹配
            if (vehicle_base->GetGUID() != guid)
                return;

            // 如果没有指定附属物，则切换到当前载具的前一个或后一个座位
            if (!accessory)
                GetPlayer()->ChangeSeat(-1, seatId > 0); // seatId > 0 表示后一个，否则前一个
            // 如果指定了附属物，则切换到另一个载具的座位
            else if (Unit* vehUnit = ObjectAccessor::GetUnit(*GetPlayer(), accessory))
            {
                // 获取目标载具的载具套件
                if (Vehicle* vehicle = vehUnit->GetVehicleKit())
                    // 检查目标座位是否为空
                    if (vehicle->HasEmptySeat(seatId))
                        // 通过法术点击机制进入新载具
                        vehUnit->HandleSpellClick(GetPlayer(), seatId);
            }
            break;
        }

        // 请求切换到指定座位
        case CMSG_REQUEST_VEHICLE_SWITCH_SEAT:
        {
            // 读取目标载具的GUID
            ObjectGuid guid;        // current vehicle guid
            recvData >> guid.ReadAsPacked();

            // 读取目标座位ID
            int8 seatId;
            recvData >> seatId;

            // 如果目标载具是当前载具，直接切换座位
            if (vehicle_base->GetGUID() == guid)
                GetPlayer()->ChangeSeat(seatId);
            // 如果目标载具是另一个载具，需要检查座位是否可用
            else if (Unit* vehUnit = ObjectAccessor::GetUnit(*GetPlayer(), guid))
                if (Vehicle* vehicle = vehUnit->GetVehicleKit())
                    if (vehicle->HasEmptySeat(seatId))
                        // 通过法术点击机制进入新载具
                        vehUnit->HandleSpellClick(GetPlayer(), seatId);
            break;
        }
        default:
            break;
    }
}

/**
 * @brief 处理进入玩家载具的消息
 *
 * 当玩家尝试进入另一个玩家的载具（如双人坐骑）时调用此函数
 * 执行完整的验证流程后允许玩家进入
 *
 * @param data 接收到的网络数据包，包含目标玩家的GUID
 *
 * 验证流程：
 * 1. 检查目标玩家是否存在
 * 2. 检查目标玩家是否有载具套件
 * 3. 检查是否在同一团队/小队中
 * 4. 检查距离是否在交互范围内
 * 5. 检查当前地图是否允许（竞技场禁止）
 *
 * 调用时机：
 * - 玩家右键点击另一个玩家的载具
 * - 玩家尝试进入队友的坐骑
 * - 客户端发送 CMSG_ENTER_PLAYER_VEHICLE 消息
 *
 * 使用场景：
 * - 双人坐骑（如旅行者的苔原猛犸象）
 * - 多人载具（如某些大型坐骑）
 * - 玩家控制的战车
 *
 * 安全措施：
 * - 距离验证防止远程进入
 * - 团队验证限制陌生人进入
 * - 竞技场限制防止不公平竞技
 */
void WorldSession::HandleEnterPlayerVehicle(WorldPacket &data)
{
    // 读取目标玩家的GUID
    ObjectGuid guid;
    data >> guid;

    // 查找目标玩家
    if (Player* player = ObjectAccessor::GetPlayer(*_player, guid))
    {
        // 检查目标玩家是否有载具套件（是否是载具）
        if (!player->GetVehicleKit())
            return;

        // 检查是否在同一团队或小队中（只有队友可以进入玩家的载具）
        if (!player->IsInRaidWith(_player))
            return;

        // 检查距离是否在交互范围内
        if (!player->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
            return;

        // 竞技场不允许玩家进入其他玩家的载具
        // 防止在竞技场中使用某些载具获得不公平优势
        if (!_player->FindMap() || _player->FindMap()->IsBattleArena())
            return;

        // 所有检查通过，进入载具
        _player->EnterVehicle(player);
    }
}

/**
 * @brief 处理弹出乘客的消息
 *
 * 当载具驾驶员或控制者弹出某个乘客时调用此函数
 * 将指定乘客从载具中移除
 *
 * @param data 接收到的网络数据包，包含要弹出的乘客GUID
 *
 * 验证流程：
 * 1. 检查玩家是否拥有载具套件
 * 2. 检查目标GUID是否是单位类型
 * 3. 检查目标单位是否存在
 * 4. 检查目标单位是否在同一载具上
 * 5. 检查座位是否可弹出
 *
 * 调用时机：
 * - 载具驾驶员点击弹出乘客按钮
 * - 载具控制者执行弹出操作
 * - 客户端发送 CMSG_EJECT_PASSENGER 消息
 *
 * 座位弹出权限：
 * - 不是所有座位都可以被弹出
 * - 某些特殊座位（如驾驶员）不能被弹出
 * - 座位的 IsEjectable() 标志决定是否可弹出
 *
 * 安全措施：
 * - 验证载具所有权
 * - 验证座位弹出权限
 * - 记录非法操作日志
 */
void WorldSession::HandleEjectPassenger(WorldPacket &data)
{
    // 获取玩家的载具套件
    Vehicle* vehicle = _player->GetVehicleKit();
    if (!vehicle)
    {
        // 玩家没有载具，清空数据包防止警告
        data.rfinish();
        TC_LOG_ERROR("network", "HandleEjectPassenger: {} is not in a vehicle!", GetPlayer()->GetGUID().ToString());
        return;
    }

    // 读取要弹出的乘客GUID
    ObjectGuid guid;
    data >> guid;

    // 检查GUID是否是单位类型（玩家或生物）
    if (guid.IsUnit())
    {
        // 查找目标单位
        Unit* unit = ObjectAccessor::GetUnit(*_player, guid);
        if (!unit) // 生物也可以从玩家坐骑上被弹出
        {
            TC_LOG_ERROR("network", "{} tried to eject {} from vehicle, but the latter was not found in world!", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        // 检查目标单位是否在同一载具上
        if (!unit->IsOnVehicle(vehicle->GetBase()))
        {
            TC_LOG_ERROR("network", "{} tried to eject {}, but they are not in the same vehicle", GetPlayer()->GetGUID().ToString(), guid.ToString());
            return;
        }

        // 获取目标单位所在的座位信息
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(unit);
        ASSERT(seat);  // 座位必须存在

        // 检查座位是否可以被弹出
        if (seat->IsEjectable())
            unit->ExitVehicle();  // 执行弹出操作
        else
            TC_LOG_ERROR("network", "Player {} attempted to eject {} from non-ejectable seat.", GetPlayer()->GetGUID().ToString(), guid.ToString());
    }
    else
        // GUID不是单位类型，记录错误
        TC_LOG_ERROR("network", "HandleEjectPassenger: {} tried to eject invalid {} ", GetPlayer()->GetGUID().ToString(), guid.ToString());
}

/**
 * @brief 处理请求退出载具的消息
 *
 * 当玩家主动请求离开当前载具时调用此函数
 * 根据座位的退出权限决定是否允许退出
 *
 * @param recvData 接收到的网络数据包（未使用）
 *
 * 验证流程：
 * 1. 检查玩家是否在载具中
 * 2. 获取当前座位信息
 * 3. 检查座位是否允许进出
 *
 * 调用时机：
 * - 玩家按下退出载具的快捷键
 * - 玩家点击界面上的离开载具按钮
 * - 客户端发送 CMSG_REQUEST_VEHICLE_EXIT 消息
 *
 * 座位退出权限：
 * - CanEnterOrExit() 标志决定是否允许主动退出
 * - 某些特殊座位可能不允许主动退出
 * - 驾驶员座位通常允许退出
 *
 * 安全措施：
 * - 验证座位权限防止非法退出
 * - 记录非法操作日志
 */
void WorldSession::HandleRequestVehicleExit(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_REQUEST_VEHICLE_EXIT");

    // 获取玩家当前所在的载具
    if (Vehicle* vehicle = GetPlayer()->GetVehicle())
    {
        // 获取玩家所在的座位信息
        if (VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(GetPlayer()))
        {
            // 检查座位是否允许进出
            if (seat->CanEnterOrExit())
                GetPlayer()->ExitVehicle();  // 执行退出操作
            else
                TC_LOG_ERROR("network", "Player {} tried to exit vehicle, but seatflags {} (ID: {}) don't permit that.",
                GetPlayer()->GetGUID().ToString(), seat->ID, seat->Flags);
        }
    }
}
