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
 * @file CombatHandler.cpp
 * @brief 战斗处理模块
 *
 * 本模块负责处理游戏中所有与战斗相关的基础网络消息，包括：
 * - 攻击挥动（自动攻击启动）
 * - 停止攻击
 * - 武器收纳状态设置
 *
 * 主要职责：
 * 1. 处理玩家发起和停止攻击的请求
 * 2. 验证攻击目标的合法性
 * 3. 检查载具座位是否允许攻击
 * 4. 管理武器收纳状态（出鞘/入鞘）
 * 5. 发送停止攻击消息给客户端
 *
 * 注意事项：
 * - 此模块处理的是基础攻击（自动攻击），不包含技能攻击
 * - 载具上的攻击需要检查座位权限
 * - 武器收纳状态影响角色外观显示
 *
 * 相关模块：
 * - SpellHandler: 处理技能和法术攻击
 * - AI系统: 处理NPC的战斗行为
 */

#include "WorldSession.h"
#include "CombatPackets.h"
#include "Common.h"
#include "CreatureAI.h"
#include "DBCStructure.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Vehicle.h"
#include "WorldPacket.h"

/**
 * @brief 处理玩家发起攻击挥动的网络包
 *
 * 职责：
 *   处理客户端发送的 CMSG_ATTACK_SWING 消息，验证攻击目标的合法性，
 *   并启动玩家对目标的自动攻击状态。
 *
 * 参数：
 *   @param packet - 攻击挥动数据包，包含攻击目标的 GUID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 从数据包中获取攻击目标
 *   2. 验证目标是否存在，不存在则通知客户端停止攻击
 *   3. 验证目标是否为合法攻击目标，不合法则通知客户端停止攻击
 *   4. 检查玩家是否在载具中，如果是则验证座位是否允许攻击
 *   5. 所有的验证通过后，启动对目标的自动攻击
 */
void WorldSession::HandleAttackSwingOpcode(WorldPackets::Combat::AttackSwing& packet)
{
    // 从数据包中获取攻击目标单位
    Unit* enemy = ObjectAccessor::GetUnit(*_player, packet.Victim);

    if (!enemy)
    {
        // 目标不存在，通知客户端停止攻击状态
        SendAttackStop(nullptr);
        return;
    }

    if (!_player->IsValidAttackTarget(enemy))
    {
        // 目标不是合法的攻击目标，通知客户端停止攻击状态
        SendAttackStop(enemy);
        return;
    }

    //! 客户端在发送 CMSG_ATTACK_SWING 数据包之前会进行以下检查，
    //! 所以我们在这里也放置相同的检查。注意这段代码可能在其他地方也能复用。
    //! 检查玩家是否在载具中
    if (Vehicle* vehicle = _player->GetVehicle())
    {
        // 获取玩家在载具中的座位信息
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(_player);
        ASSERT(seat);
        // 检查座位是否允许攻击
        if (!(seat->Flags & VEHICLE_SEAT_FLAG_CAN_ATTACK))
        {
            // 座位不允许攻击，通知客户端停止攻击
            SendAttackStop(enemy);
            return;
        }
    }

    // 所有的验证通过，启动对目标的自动攻击
    // 第二个参数 true 表示这是自动攻击（自动挥动武器）
    _player->Attack(enemy, true);
}

/**
 * @brief 处理玩家停止攻击的网络包
 *
 * 职责：
 *   处理客户端发送的 CMSG_ATTACK_STOP 消息，停止玩家当前的自动攻击状态。
 *
 * 参数：
 *   @param packet - 停止攻击数据包（未使用）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 获取玩家对象
 *   2. 调用 AttackStop() 停止所有攻击行为
 */
void WorldSession::HandleAttackStopOpcode(WorldPackets::Combat::AttackStop& /*packet*/)
{
    // 停止玩家的所有攻击行为，包括自动攻击和武器挥动
    GetPlayer()->AttackStop();
}

/**
 * @brief 处理玩家设置武器收纳状态的网络包
 *
 * 职责：
 *   处理客户端发送的 CMSG_SET_SHEATHED 消息，设置玩家的武器收纳状态
 *   （如：武器入鞘、武器出鞘等）。
 *
 * 参数：
 *   @param packet - 设置收纳状态数据包，包含新的收纳状态值
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 验证收纳状态值是否在有效范围内
 *   2. 如果无效则记录错误日志并返回
 *   3. 设置玩家的武器收纳状态
 *
 * 收纳状态说明：
 *   - SHEATH_STATE_UNARMED (0): 未装备武器状态
 *   - SHEATH_STATE_MELEE (1): 近战武器出鞘状态
 *   - SHEATH_STATE_RANGED (2): 远程武器出鞘状态
 */
void WorldSession::HandleSetSheathedOpcode(WorldPackets::Combat::SetSheathed& packet)
{
    // 验证收纳状态值是否有效
    if (packet.CurrentSheathState >= MAX_SHEATH_STATE)
    {
        // 记录无效的收纳状态值
        TC_LOG_ERROR("network", "Unknown sheath state {} ??", packet.CurrentSheathState);
        return;
    }

    // 设置玩家的武器收纳状态
    _player->SetSheath(SheathState(packet.CurrentSheathState));
}

/**
 * @brief 发送停止攻击消息给客户端
 *
 * 职责：
 *   构造并发送 SMSG_ATTACK_STOP 消息给客户端，通知客户端停止攻击动画和状态。
 *
 * 参数：
 *   @param enemy - 攻击目标（可为 nullptr，表示目标已不存在）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 构造 SAttackStop 数据包，包含玩家和敌人信息
 *   2. 发送数据包给客户端
 *
 * 使用场景：
 *   - 目标不存在时
 *   - 目标不是合法攻击目标时
 *   - 玩家所在载具座位不允许攻击时
 *   - 其他需要强制停止攻击的场景
 */
void WorldSession::SendAttackStop(Unit const* enemy)
{
    // 构造并发送停止攻击的数据包
    // 包含攻击者（当前玩家）和目标（敌人）的信息
    SendPacket(WorldPackets::Combat::SAttackStop(GetPlayer(), enemy).Write());
}
