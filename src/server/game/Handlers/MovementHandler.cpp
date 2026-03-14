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

#include "Battleground.h"
#include "Common.h"
#include "Corpse.h"
#include "GameTime.h"
#include "GameClient.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "MovementGenerator.h"
#include "MovementPacketSender.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Transport.h"
#include "Vehicle.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/circular_buffer.hpp>

/**
 * @brief 处理世界传送确认操作码
 *
 * @职责 处理客户端发送的世界传送确认消息(MSG_MOVE_WORLDPORT_ACK)
 *       当玩家跨地图传送时,客户端会发送此确认包
 *
 * @param recvData 接收到的网络包数据(未使用)
 *
 * @返回值 无
 *
 * @主要流程 直接调用 HandleMoveWorldportAck() 进行实际处理
 */
void WorldSession::HandleMoveWorldportAckOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: got MSG_MOVE_WORLDPORT_ACK.");
    HandleMoveWorldportAck();
}

/**
 * @brief 处理世界传送确认
 *
 * @职责 完成玩家跨地图传送的实际处理逻辑
 *       验证传送目标、创建新地图、更新玩家位置、处理战场/副本逻辑
 *
 * @参数 无(使用玩家内部的传送目标信息)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 检查玩家是否正在被远距离传送
 *   2. 验证目标坐标的有效性
 *   3. 获取目标地图信息并创建新地图实例
 *   4. 将玩家从旧地图移除并添加到新地图
 *   5. 处理战场/副本相关的逻辑
 *   6. 处理飞行状态和复活逻辑
 *   7. 更新区域信息和PvP状态
 *   8. 重新召唤宠物并处理延迟操作
 */
void WorldSession::HandleMoveWorldportAck()
{
    Player* player = GetPlayer();
    // 忽略意外的远距离传送请求
    if (!player->IsBeingTeleportedFar())
        return;

    // 重置远距离传送信号量
    player->SetSemaphoreTeleportFar(false);

    // 获取传送目标位置
    WorldLocation const& loc = player->GetTeleportDest();

    // 坐标有效性检查,可能出现的错误
    if (!MapManager::IsValidMapCoord(loc))
    {
        LogoutPlayer(false);
        return;
    }

    // 获取目标地图条目(不是当前地图),这将修复homebind并重置问候
    MapEntry const* mEntry = sMapStore.LookupEntry(loc.GetMapId());
    InstanceTemplate const* mInstance = sObjectMgr->GetInstanceTemplate(loc.GetMapId());

    // 重置副本有效性,除非是在副本内进入另一个副本
    if (player->m_InstanceValid == false && !mInstance)
        player->m_InstanceValid = true;

    // 获取旧地图和创建新地图
    Map* oldMap = player->GetMap();
    Map* newMap = sMapMgr->CreateMap(loc.GetMapId(), player);

    // 如果玩家仍在世界中,需要先从旧地图移除
    if (player->IsInWorld())
    {
        TC_LOG_ERROR("network", "{} {} is still in world when teleported from map {} ({}) to new map {} ({})", player->GetGUID().ToString(), player->GetName(), oldMap->GetMapName(), oldMap->GetId(), newMap ? newMap->GetMapName() : "Unknown", loc.GetMapId());
        oldMap->RemovePlayerFromMap(player, false);
    }

    // 将玩家重新定位到传送目标位置
    // CannotEnter检查在TeleporTo中完成,但条件可能在传送过程中改变
    // 例如地图可能已满
    if (!newMap || newMap->CannotEnter(player))
    {
        TC_LOG_ERROR("network", "Map {} ({}) could not be created for player {} ({}), porting player to homebind", loc.GetMapId(), newMap ? newMap->GetMapName() : "Unknown", player->GetGUID().ToString(), player->GetName());
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
        return;
    }

    // 计算Z轴位置(考虑悬浮偏移)并重新定位玩家
    float z = loc.GetPositionZ() + player->GetHoverOffset();
    player->Relocate(loc.GetPositionX(), loc.GetPositionY(), z, loc.GetOrientation());
    player->SetFallInformation(0, player->GetPositionZ());

    // 重置地图引用并设置新地图
    player->ResetMap();
    player->SetMap(newMap);

    // 在添加到地图前发送初始数据包
    player->SendInitialPacketsBeforeAddToMap();
    if (!player->GetMap()->AddPlayerToMap(player))
    {
        TC_LOG_ERROR("network", "WORLD: failed to teleport player {} {} to map {} ({}) because of unknown reason!",
            player->GetName(), player->GetGUID().ToString(), loc.GetMapId(), newMap ? newMap->GetMapName() : "Unknown");
        player->ResetMap();
        player->SetMap(oldMap);
        player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation());
        return;
    }

    // 战场状态准备(以防加入战场),在重新登录/传送时玩家未被邀请
    // 只有在被邀请的情况下才添加到战场队伍和对象中(否则是通过命令进入的)
    if (player->InBattleground())
    {
        // 如果设置过时则清理
        if (!mEntry->IsBattlegroundOrArena())
        {
            // 我们不在战场中
            player->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE);
            // 重置目标战场队伍
            player->SetBGTeam(0);
        }
        // 加入战场的情况
        else if (Battleground* bg = player->GetBattleground())
        {
            if (player->IsInvitedForBattlegroundInstance(player->GetBattlegroundId()))
                bg->AddPlayer(player);
        }
    }

    // 添加到地图后发送初始数据包
    player->SendInitialPacketsAfterAddToMap();

    // 飞行快速传送的情况
    if (player->IsInFlight())
    {
        if (!player->InBattleground())
        {
            // 简短准备以继续飞行
            MovementGenerator* movementGenerator = player->GetMotionMaster()->GetCurrentMovementGenerator();
            movementGenerator->Initialize(player);
            return;
        }

        // 战场状态准备,停止飞行
        player->FinishTaxiFlight();
    }

    // 如果设置了传送时复活标志且玩家已死亡,则复活玩家
    if (!player->IsAlive() && player->GetTeleportOptions() & TELE_REVIVE_AT_TELEPORT)
        player->ResurrectPlayer(0.5f);

    // 在进入存在尸体的副本时复活角色(添加到地图后)
    if (mEntry->IsDungeon() && !player->IsAlive())
    {
        if (player->GetCorpseLocation().GetMapId() == mEntry->ID)
        {
            player->ResurrectPlayer(0.5f);
            player->SpawnCorpseBones();
        }
    }

    // 判断是否允许坐骑
    bool allowMount = !mEntry->IsDungeon() || mEntry->IsBattlegroundOrArena();
    if (mInstance)
    {
        // 检查此副本是否有重置时间,如果有则发送给玩家
        Difficulty diff = player->GetDifficulty(mEntry->IsRaid());
        if (MapDifficulty const* mapDiff = GetMapDifficultyData(mEntry->ID, diff))
        {
            if (mapDiff->resetTime)
            {
                if (time_t timeReset = sInstanceSaveMgr->GetResetTimeFor(mEntry->ID, diff))
                {
                    uint32 timeleft = uint32(timeReset - GameTime::GetGameTime());
                    player->SendInstanceResetWarning(mEntry->ID, diff, timeleft, true);
                }
            }
        }

        // 检查副本是否有效
        if (!player->CheckInstanceValidity(false))
            player->m_InstanceValid = false;

        // 副本坐骑处理由InstanceTemplate控制
        allowMount = mInstance->AllowMount;
    }

    // 坐骑允许检查,如果不允许则移除坐骑光环
    if (!allowMount)
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);

    // 立即更新区域,否则离开频道会导致多线程地图崩溃
    uint32 newzone, newarea;
    player->GetZoneAndAreaId(newzone, newarea);
    player->UpdateZone(newzone, newarea);

    // 无荣誉目标(在敌对区域)
    if (player->pvpInfo.IsHostile)
        player->CastSpell(player, 2479, true);

    // 在友好区域
    else if (player->IsPvP() && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
        player->UpdatePvP(false, false);

    // 重新召唤宠物
    player->ResummonPetTemporaryUnSummonedIfAny();

    // 在成功传送后处理所有延迟操作
    player->ProcessDelayedOperations();
}

/**
 * @brief 处理传送确认
 *
 * @职责 处理客户端发送的近距离传送确认消息(MSG_MOVE_TELEPORT_ACK)
 *       完成同地图内的传送确认并更新玩家位置
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 被移动单位的GUID
 *        - sequenceIndex: 序列索引
 *        - time: 时间戳
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取并验证移动单位GUID
 *   2. 读取序列索引和时间戳
 *   3. 检查玩家是否正在被近距离传送
 *   4. 更新玩家位置和下落信息
 *   5. 更新区域信息,如果区域改变则处理PvP状态
 *   6. 重新召唤宠物并处理延迟操作
 */
void WorldSession::HandleMoveTeleportAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "MSG_MOVE_TELEPORT_ACK");
    ObjectGuid guid;

    // 读取被移动单位的GUID(打包格式)
    recvData >> guid.ReadAsPacked();

    // 验证是否是正确的移动单位
    if (!IsRightUnitBeingMoved(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    // 读取序列索引和时间
    uint32 sequenceIndex, time;
    recvData >> sequenceIndex >> time;

    // 获取游戏客户端和正在移动的单位
    GameClient* client = GetGameClient();
    Unit* mover = client->GetActivelyMovedUnit();
    Player* plMover = mover->ToPlayer();

    // 如果不是玩家移动或者玩家没有被近距离传送,则返回
    if (!plMover || !plMover->IsBeingTeleportedNear())
        return;

    // 重置近距离传送信号量
    plMover->SetSemaphoreTeleportNear(false);

    // 记录旧区域ID
    uint32 old_zone = plMover->GetZoneId();

    // 获取传送目标位置
    WorldLocation const& dest = plMover->GetTeleportDest();

    // 更新玩家位置
    plMover->UpdatePosition(dest, true);
    plMover->SetFallInformation(0, GetPlayer()->GetPositionZ());

    // 更新区域信息
    uint32 newzone, newarea;
    plMover->GetZoneAndAreaId(newzone, newarea);
    plMover->UpdateZone(newzone, newarea);

    // 如果进入了新区域
    if (old_zone != newzone)
    {
        // 无荣誉目标(在敌对区域)
        if (plMover->pvpInfo.IsHostile)
            plMover->CastSpell(plMover, 2479, true);

        // 在友好区域
        else if (plMover->IsPvP() && !plMover->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
            plMover->UpdatePvP(false, false);
    }

    // 重新召唤宠物
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();

    // 在成功传送后处理所有延迟操作
    GetPlayer()->ProcessDelayedOperations();
}

/**
 * @brief 处理移动操作码
 *
 * @职责 处理各种移动相关的操作码,包括行走、奔跑、游泳、飞行等
 *       验证移动数据、更新单位位置、处理载具和运输工具逻辑
 *
 * @param recvData 接收到的网络包数据
 *        - opcode: 移动操作码类型
 *        - guid: 移动单位GUID
 *        - movementInfo: 移动信息结构
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取并验证移动单位GUID
 *   2. 如果玩家正在传送中则忽略此移动包
 *   3. 提取移动信息数据包
 *   4. 验证位置坐标的有效性
 *   5. 检查移动样条是否已完成
 *   6. 处理运输工具(船只/飞艇)相关逻辑
 *   7. 处理下落伤害和降落伞效果
 *   8. 广播移动数据包给周围玩家
 *   9. 更新单位位置和载具状态
 *   10. 检查是否掉落到地图下方(虚空伤害)
 */
void WorldSession::HandleMovementOpcodes(WorldPacket& recvData)
{
    uint16 opcode = recvData.GetOpcode();

    // 读取移动单位GUID
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // 验证是否是正确的移动单位
    if (!IsRightUnitBeingMoved(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    // 获取游戏客户端和正在移动的单位
    GameClient* client = GetGameClient();
    Unit* mover = client->GetActivelyMovedUnit();
    Player* plrMover = mover->ToPlayer();

    // 忽略,等待 WorldSession::HandleMoveWorldportAckOpcode 和 WorldSession::HandleMoveTeleportAck 处理
    if (plrMover && plrMover->IsBeingTeleported())
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    /* 提取数据包 */

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.rfinish();                         // 防止警告刷屏

    // 验证位置是否有效
    if (!movementInfo.pos.IsPositionValid())
        return;

    // 如果移动样条未完成则返回
    if (!mover->movespline->Finalized())
        return;

    /* 处理特殊情况 */

    // 处理运输工具相关逻辑
    if (movementInfo.HasMovementFlag(MOVEMENTFLAG_ONTRANSPORT))
    {
        // 我们被传送了,跳过传送前广播的数据包
        if (movementInfo.pos.GetExactDist2d(mover) > SIZE_OF_GRIDS)
            return;

        // 运输工具大小限制
        // (也会在离开飞艇时收到,原因不明,t_*为大洲坐标的绝对值,可以安全跳过)
        if (fabs(movementInfo.transport.pos.GetPositionX()) > 75.0f || fabs(movementInfo.transport.pos.GetPositionY()) > 75.0f || fabs(movementInfo.transport.pos.GetPositionZ()) > 75.0f)
            return;

        // 验证运输工具坐标的有效性
        if (!Trinity::IsValidMapCoord(movementInfo.pos.GetPositionX() + movementInfo.transport.pos.GetPositionX(), movementInfo.pos.GetPositionY() + movementInfo.transport.pos.GetPositionY(),
            movementInfo.pos.GetPositionZ() + movementInfo.transport.pos.GetPositionZ(), movementInfo.pos.GetOrientation() + movementInfo.transport.pos.GetOrientation()))
            return;

        // 如果我们登上了运输工具,将我们添加到其中
        if (plrMover)
        {
            if (!plrMover->GetTransport())
            {
                // 玩家不在运输工具上,尝试添加到运输工具
                if (Transport* transport = plrMover->GetMap()->GetTransport(movementInfo.transport.guid))
                    transport->AddPassenger(plrMover);
            }
            else if (plrMover->GetTransport()->GetGUID() != movementInfo.transport.guid)
            {
                // 玩家切换到了另一个运输工具
                plrMover->GetTransport()->RemovePassenger(plrMover);
                if (Transport* transport = plrMover->GetMap()->GetTransport(movementInfo.transport.guid))
                    transport->AddPassenger(plrMover);
                else
                    movementInfo.transport.Reset();
            }
        }

        // 如果单位不在运输工具上也不在载具上,验证游戏对象
        if (!mover->GetTransport() && !mover->GetVehicle())
        {
            GameObject* go = mover->GetMap()->GetGameObject(movementInfo.transport.guid);
            if (!go || go->GetGoType() != GAMEOBJECT_TYPE_TRANSPORT)
                movementInfo.RemoveMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
        }
    }
    else if (plrMover && plrMover->GetTransport())                // 如果我们在运输工具上,离开它
    {
        plrMover->GetTransport()->RemovePassenger(plrMover);
        movementInfo.transport.Reset();
    }

    // 下落伤害生成(忽略飞行情况,飞行也可能在传送到另一张地图时的延迟触发)
    if (opcode == MSG_MOVE_FALL_LAND && plrMover && !plrMover->IsInFlight())
        plrMover->HandleFall(movementInfo);

    // 下落或落入水中时中断降落伞效果
    if (opcode == MSG_MOVE_FALL_LAND || opcode == MSG_MOVE_START_SWIM)
        mover->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_LANDING); // 降落伞

    /* 处理位置变更 */

    // 创建广播数据包
    WorldPacket data(opcode, recvData.size());
    int64 movementTime = (int64) movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        TC_LOG_WARN("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = GameTime::GetGameTimeMS();
    }
    else
    {
        movementInfo.time = (uint32)movementTime;
    }

    // 设置移动信息并广播给周围玩家
    movementInfo.guid = mover->GetGUID();
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, _player);

    // 保存移动信息
    mover->m_movementInfo = movementInfo;

    // 某些载具允许乘客自己转向
    if (Vehicle* vehicle = mover->GetVehicle())
    {
        if (VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(mover))
        {
            if (seat->Flags & VEHICLE_SEAT_FLAG_ALLOW_TURNING)
            {
                if (movementInfo.pos.GetOrientation() != mover->GetOrientation())
                {
                    mover->SetOrientation(movementInfo.pos.GetOrientation());
                    mover->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TURNING);
                }
            }
        }
        return;
    }

    // 更新单位位置
    mover->UpdatePosition(movementInfo.pos);

    // 如果是玩家移动单位
    if (plrMover)                                            // 没有被魅惑,或者玩家被魅惑
    {
        // 如果玩家处于坐下状态且正在移动或转向,则站起来
        if (plrMover->IsSitState() && (movementInfo.flags & (MOVEMENTFLAG_MASK_MOVING | MOVEMENTFLAG_MASK_TURNING)))
            plrMover->SetStandState(UNIT_STAND_STATE_STAND);

        // 更新下落信息(如果需要)
        plrMover->UpdateFallInformationIfNeed(movementInfo, opcode);

        // 检查玩家是否掉落到地图最小高度以下(掉入虚空)
        if (movementInfo.pos.GetPositionZ() < plrMover->GetMap()->GetMinHeight(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY()))
        {
            // 如果不在战场,或者战场不处理地图下方的玩家
            if (!(plrMover->GetBattleground() && plrMover->GetBattleground()->HandlePlayerUnderMap(plrMover)))
            {
                // 注意:这实际上在下落过程中会被调用很多次
                // 即使玩家已经被传送走了
                /// @todo 在玩家被定身后丢弃移动数据包
                if (plrMover->IsAlive())
                {
                    TC_LOG_DEBUG("entities.player.falldamage", "FALLDAMAGE Below map. Map min height: {} , Player debug info:\n{}", plrMover->GetMap()->GetMinHeight(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY()), plrMover->GetDebugInfo());
                    plrMover->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_IS_OUT_OF_BOUNDS);
                    plrMover->EnvironmentalDamage(DAMAGE_FALL_TO_VOID, GetPlayer()->GetMaxHealth());
                    // 如果是GM等,玩家可能还活着
                    // 将死亡状态改为CORPSE以防止死亡计时器
                    // 在下一个玩家更新中启动
                    if (plrMover->IsAlive())
                        plrMover->KillPlayer();
                }
            }
        }
        else
            plrMover->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_IS_OUT_OF_BOUNDS);
    }
}

/**
 * @brief 处理强制速度变更确认
 *
 * @职责 处理客户端发送的速度变更确认包,验证客户端返回的速度数据是否正确
 *       防止作弊并实际应用速度变更
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - movementCounter: 移动计数器
 *        - movementInfo: 移动信息
 *        - speedReceived: 接收到的速度值
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取并验证移动单位GUID
 *   2. 根据操作码确定移动类型(行走/奔跑/游泳等)
 *   3. 读取移动计数器、移动信息和速度值
 *   4. 验证客户端返回的数据是否与服务器发送的一致
 *   5. 检查移动计数器、速度值、移动类型是否匹配
 *   6. 如果数据不匹配,记录作弊日志并可能踢出玩家
 *   7. 验证通过后,应用速度变更并广播给观察者
 */
void WorldSession::HandleForceSpeedChangeAck(WorldPacket &recvData)
{
    /* 提取数据包 */
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();

    // ACK处理程序应该调用 GameClient::IsAllowedToMove 而不是 WorldSession::IsRightUnitBeingMoved
    // 因为ACK可能来自该客户端控制的单位,但不是"活动移动者"单位。
    // 示例:对自己施加速度增益,然后在增益结束前登上载具。当增益过期时,
    // 会向客户端发送关于玩家的强制消息,客户端需要响应ACK。
    // 但此时载具将是活动移动者单位。
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        TC_LOG_DEBUG("entities.unit", "Ignoring ACK. Bad or outdated movement data by Player {}", _player->GetName());
        return;
    }

    // 获取移动单位
    Unit* mover = ObjectAccessor::GetUnit(*_player, guid);

    // 根据操作码确定移动类型
    UnitMoveType move_type;
    switch (recvData.GetOpcode())
    {
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:          move_type = MOVE_WALK;          break;
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:           move_type = MOVE_RUN;           break;
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:      move_type = MOVE_RUN_BACK;      break;
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:          move_type = MOVE_SWIM;          break;
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:     move_type = MOVE_SWIM_BACK;     break;
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:           move_type = MOVE_TURN_RATE;     break;
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:        move_type = MOVE_FLIGHT;        break;
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:   move_type = MOVE_FLIGHT_BACK;   break;
        case CMSG_FORCE_PITCH_RATE_CHANGE_ACK:          move_type = MOVE_PITCH_RATE;    break;
        default:
            TC_LOG_ERROR("network", "WorldSession::HandleForceSpeedChangeAck: Unknown move type opcode: {}", GetOpcodeNameForLogging(static_cast<OpcodeClient>(recvData.GetOpcode())));
            return;
    }

    // 读取移动计数器、移动信息和速度值
    uint32 movementCounter;
    float  speedReceived;
    MovementInfo movementInfo;
    movementInfo.guid = guid;

    recvData >> movementCounter;
    ReadMovementInfo(recvData, &movementInfo);
    recvData >> speedReceived;

    ASSERT(mover);

    // 验证客户端确实是用发送给它的变更来回复
    if (!mover->HasPendingMovementChange() || mover->PeakFirstPendingMovementChange().movementCounter > movementCounter)
    {
        TC_LOG_DEBUG("entities.unit", "Ignoring ACK. Bad or outdated movement data by Player {}", _player->GetName());
        return;
    }

    // 获取待处理的移动变更
    PlayerMovementPendingChange pendingChange = mover->PopPendingMovementChange();
    float speedSent = pendingChange.newValue;
    MovementChangeType changeType = pendingChange.movementChangeType;
    UnitMoveType moveTypeSent;
    switch (changeType)
    {
        case MovementChangeType::SPEED_CHANGE_WALK:                 moveTypeSent = MOVE_WALK; break;
        case MovementChangeType::SPEED_CHANGE_RUN:                  moveTypeSent = MOVE_RUN; break;
        case MovementChangeType::SPEED_CHANGE_RUN_BACK:             moveTypeSent = MOVE_RUN_BACK; break;
        case MovementChangeType::SPEED_CHANGE_SWIM:                 moveTypeSent = MOVE_SWIM; break;
        case MovementChangeType::SPEED_CHANGE_SWIM_BACK:            moveTypeSent = MOVE_SWIM_BACK; break;
        case MovementChangeType::RATE_CHANGE_TURN:                  moveTypeSent = MOVE_TURN_RATE; break;
        case MovementChangeType::SPEED_CHANGE_FLIGHT_SPEED:         moveTypeSent = MOVE_FLIGHT; break;
        case MovementChangeType::SPEED_CHANGE_FLIGHT_BACK_SPEED:    moveTypeSent = MOVE_FLIGHT_BACK; break;
        case MovementChangeType::RATE_CHANGE_PITCH:                 moveTypeSent = MOVE_PITCH_RATE; break;
        default:
            TC_LOG_INFO("cheat", "WorldSession::HandleForceSpeedChangeAck: Player {} from account id {} kicked for incorrect data returned in an ack. movementChangeType: {}",
                _player->GetName(), _player->GetSession()->GetAccountId(), static_cast<uint32>(AsUnderlyingType(changeType)));
            if (sWorld->getIntConfig(CONFIG_PENDING_MOVE_CHANGES_TIMEOUT) != 0)
                _player->GetSession()->KickPlayer("incorrect movementChangeType returned in an ack");
            return;
    }

    // 验证移动计数器是否匹配
    if (pendingChange.movementCounter != movementCounter)
    {
        TC_LOG_INFO("cheat", "WorldSession::HandleForceSpeedChangeAck: Player {} from account id {} kicked for incorrect data returned in an ack. pendingChange.movementCounter: {}, movementCounter: {}",
            _player->GetName(), _player->GetSession()->GetAccountId(), pendingChange.movementCounter, movementCounter);
        if (sWorld->getIntConfig(CONFIG_PENDING_MOVE_CHANGES_TIMEOUT) != 0)
            _player->GetSession()->KickPlayer("incorrect movementCounter returned in an ack");
        return;
    }

    // 验证速度值是否匹配(允许0.01的误差)
    if (std::fabs(speedSent - speedReceived) > 0.01f)
    {
        TC_LOG_INFO("cheat", "WorldSession::HandleForceSpeedChangeAck: Player {} from account id {} kicked for incorrect data returned in an ack. speedSent - speedReceived: {}",
            _player->GetName(), _player->GetSession()->GetAccountId(), std::fabs(speedSent - speedReceived));
        if (sWorld->getIntConfig(CONFIG_PENDING_MOVE_CHANGES_TIMEOUT) != 0)
            _player->GetSession()->KickPlayer("incorrect speed returned in an ack");
        return;
    }

    // 验证移动类型是否匹配
    if (moveTypeSent != move_type)
    {
        TC_LOG_INFO("cheat", "WorldSession::HandleForceSpeedChangeAck: Player {} from account id {} kicked for incorrect data returned in an ack. moveTypeSent: {}, move_type: {}",
            _player->GetName(), _player->GetSession()->GetAccountId(), static_cast<uint32>(AsUnderlyingType(moveTypeSent)), static_cast<uint32>(AsUnderlyingType(move_type)));
        if (sWorld->getIntConfig(CONFIG_PENDING_MOVE_CHANGES_TIMEOUT) != 0)
            _player->GetSession()->KickPlayer("incorrect moveType returned in an ack");
        return;
    }

    /* 客户端数据已验证。现在进行实际变更 */
    int64 movementTime = (int64)movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        TC_LOG_WARN("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = GameTime::GetGameTimeMS();
    }
    else
    {
        movementInfo.time = (uint32)movementTime;
    }

    // 更新移动信息和位置
    mover->m_movementInfo = movementInfo;
    mover->UpdatePosition(movementInfo.pos);

    // 计算并应用新的速度比率
    float newSpeedRate = speedSent / (mover->IsControlledByPlayer() ? playerBaseMoveSpeed[move_type] : baseMoveSpeed[move_type]);
    mover->SetSpeedRateReal(move_type, newSpeedRate);
    MovementPacketSender::SendSpeedChangeToObservers(mover, move_type, speedSent);
}

/**
 * @brief 处理设置活动移动者操作码
 *
 * @职责 设置客户端当前正在控制的移动单位
 *       当玩家进入/离开载具时需要更新活动移动者
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 要设置为活动移动者的单位GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取目标单位GUID
 *   2. 检查客户端是否被允许移动该单位
 *   3. 如果允许,将该单位设置为活动移动者
 */
void WorldSession::HandleSetActiveMoverOpcode(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_SET_ACTIVE_MOVER");

    ObjectGuid guid;
    recvData >> guid;

    GameClient* client = GetGameClient();

    // 步骤1: 查看该客户端被允许移动的单位列表。检查客户端是否被允许移动
    // 数据包中提到的单位。如果不允许,则静默忽略、记录此事件或踢出客户端。
    if (!client->IsAllowedToMove(guid))
    {
        // @todo 根据配置决定记录日志、踢出或什么都不做
        TC_LOG_DEBUG("entities.unit", "set active mover FAILED for client of player {}. GUID {}.", _player->GetName(), guid.ToString());
        return;
    }

    // 步骤2: 设置活动移动者
    TC_LOG_DEBUG("entities.unit", "set active mover OK for client of player {}. GUID {}.", _player->GetName(), guid.ToString());
    Unit* newActivelyMovedUnit = ObjectAccessor::GetUnit(*_player, guid);
    client->SetActivelyMovedUnit(newActivelyMovedUnit);
}

/**
 * @brief 处理非活动移动者操作码
 *
 * @职责 清除客户端的活动移动者状态
 *       当玩家离开载具或停止控制某个单位时调用
 *
 * @param recvData 接收到的网络包数据
 *        - old_mover_guid: 之前的移动者GUID
 *        - movementInfo: 移动信息(当前被忽略)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取旧移动者GUID
 *   2. 验证当前活动移动者是否匹配
 *   3. 清除活动移动者设置
 */
void WorldSession::HandleMoveNotActiveMover(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_MOVE_NOT_ACTIVE_MOVER");

    ObjectGuid old_mover_guid;
    recvData >> old_mover_guid.ReadAsPacked();
    recvData.rfinish();                   // 防止警告刷屏
    // 此类数据包中的移动信息目前被忽略。目前尚不清楚是否应该使用。

    GameClient* client = GetGameClient();

    // 验证当前活动移动者是否匹配
    if (client->GetActivelyMovedUnit() == nullptr || client->GetActivelyMovedUnit()->GetGUID() != old_mover_guid)
    {
        TC_LOG_DEBUG("entities.unit", "unset active mover FAILED for client of player {}. GUID {}.", _player->GetName(), old_mover_guid.ToString());
        return;
    }

    TC_LOG_DEBUG("entities.unit", "unset active mover OK for client of player {}. GUID {}.", _player->GetName(), old_mover_guid.ToString());
    client->SetActivelyMovedUnit(nullptr);
}

/**
 * @brief 处理坐骑特殊动画操作码
 *
 * @职责 广播坐骑特殊动画给周围玩家
 *
 * @param recvData 接收到的网络包数据(未使用)
 *
 * @返回值 无
 *
 * @主要流程 创建坐骑特殊动画数据包并广播给周围玩家
 */
void WorldSession::HandleMountSpecialAnimOpcode(WorldPacket& /*recvData*/)
{
    WorldPacket data(SMSG_MOUNTSPECIAL_ANIM, 8);
    data << uint64(GetPlayer()->GetGUID());

    GetPlayer()->SendMessageToSet(&data, false);
}

/**
 * @brief 处理击退确认
 *
 * @职责 处理客户端发送的击退效果确认包
 *       更新单位位置并广播击退数据给周围玩家
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 被击退单位的GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息(包含跳跃信息)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取并验证移动单位GUID
 *   2. 读取移动信息(包含跳跃/击退信息)
 *   3. 更新移动时间和位置
 *   4. 创建击退数据包并广播给周围玩家
 */
void WorldSession::HandleMoveKnockBackAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_MOVE_KNOCK_BACK_ACK");

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();

    // ACK处理程序应该调用 GameClient::IsAllowedToMove 而不是 WorldSession::IsRightUnitBeingMoved
    // 因为ACK可能来自该客户端控制的单位,但不是"活动移动者"单位。
    // 示例:对自己施加速度增益,然后在增益结束前登上载具。当增益过期时,
    // 会向客户端发送关于玩家的强制消息,客户端需要响应ACK。
    // 但此时载具将是活动移动者单位。
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    Unit* mover = ObjectAccessor::GetUnit(*_player, guid);
    ASSERT(mover);

    recvData.read_skip<uint32>();                          // 未知字段

    // 读取移动信息
    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    // 计算移动时间
    int64 movementTime = (int64)movementInfo.time + _timeSyncClockDelta;
    if (_timeSyncClockDelta == 0 || movementTime < 0 || movementTime > 0xFFFFFFFF)
    {
        TC_LOG_WARN("misc", "The computed movement time using clockDelta is erronous. Using fallback instead");
        movementInfo.time = GameTime::GetGameTimeMS();
    }
    else
    {
        movementInfo.time = (uint32)movementTime;
    }

    // 更新移动信息和位置
    mover->m_movementInfo = movementInfo;
    mover->UpdatePosition(movementInfo.pos);

    // 创建击退数据包
    WorldPacket data(MSG_MOVE_KNOCK_BACK, 66);
    WriteMovementInfo(&data, &movementInfo);

    // 击退特定信息
    data << movementInfo.jump.sinAngle;
    data << movementInfo.jump.cosAngle;
    data << movementInfo.jump.xyspeed;
    data << movementInfo.jump.zspeed;

    // 广播给周围玩家
    client->GetBasePlayer()->SendMessageToSet(&data, false);
}

/**
 * @brief 处理悬浮确认
 *
 * @职责 处理客户端发送的悬浮状态确认包
 *       用于确认悬浮效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *        - unk2: 未知字段2
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveHoverAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_MOVE_HOVER_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                           // 未知字段2
}

/**
 * @brief 处理水上行走确认
 *
 * @职责 处理客户端发送的水上行走状态确认包
 *       用于确认水上行走效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *        - unk2: 未知字段2
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveWaterWalkAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_MOVE_WATER_WALK_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                           // 未知字段2
}

/**
 * @brief 处理定身确认
 *
 * @职责 处理客户端发送的定身状态确认包
 *       用于确认定身效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveRootAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_FORCE_MOVE_ROOT_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);
}

/**
 * @brief 处理缓落确认
 *
 * @职责 处理客户端发送的缓落状态确认包
 *       用于确认缓落效果(如羽落术)的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *        - unk2: 未知字段2
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleFeatherFallAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_FEATHER_FALL_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                           // 未知字段2
}

/**
 * @brief 处理解除定身确认
 *
 * @职责 处理客户端发送的解除定身状态确认包
 *       用于确认解除定身效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveUnRootAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_FORCE_MOVE_UNROOT_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);
}

/**
 * @brief 处理设置可飞行确认
 *
 * @职责 处理客户端发送的可飞行状态确认包
 *       用于确认飞行能力的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *        - unk2: 未知字段2
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveSetCanFlyAckOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_SET_CAN_FLY_ACK");

    ObjectGuid guid;                                        // guid - 未使用
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                           // 未知字段2
}

/**
 * @brief 处理游泳与飞行转换确认
 *
 * @职责 处理客户端发送的游泳与飞行状态转换确认包
 *       用于确认游泳和飞行状态之间转换的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *        - unk2: 未知字段2
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveSetCanTransitionBetweenSwinAndFlyAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY_ACK");

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                           // 未知字段2
}

/**
 * @brief 处理禁用重力确认
 *
 * @职责 处理客户端发送的禁用重力状态确认包
 *       用于确认禁用重力效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveGravityDisableAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_GRAVITY_DISABLE_ACK");

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);
}

/**
 * @brief 处理启用重力确认
 *
 * @职责 处理客户端发送的启用重力状态确认包
 *       用于确认启用重力效果的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - unk: 未知字段
 *        - movementInfo: 移动信息
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息
 */
void WorldSession::HandleMoveGravityEnableAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_GRAVITY_ENABLE_ACK");

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 未知字段

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);
}

/**
 * @brief 处理设置碰撞高度确认
 *
 * @职责 处理客户端发送的碰撞高度设置确认包
 *       用于确认碰撞高度变更的客户端响应
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - movementCounter: 移动计数器
 *        - movementInfo: 移动信息
 *        - newValue: 新的碰撞高度值
 *
 * @返回值 无
 *
 * @主要流程 读取并验证数据,但当前未使用移动信息和新值
 */
void WorldSession::HandleMoveSetCollisionHgtAck(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_MOVE_SET_COLLISION_HGT_ACK");

    ObjectGuid guid;
    float  newValue;
    recvData >> guid.ReadAsPacked();

    GameClient* client = GetGameClient();
    if (!client->IsAllowedToMove(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    recvData.read_skip<uint32>();                           // 移动计数器

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData >> newValue;
}

/**
 * @brief 处理召唤响应操作码
 *
 * @职责 处理玩家对召唤请求的响应(接受或拒绝)
 *       当其他玩家使用召唤技能时,被召唤的玩家可以选择接受或拒绝
 *
 * @param recvData 接收到的网络包数据
 *        - summoner_guid: 召唤者的GUID
 *        - agree: 是否同意召唤(true=同意,false=拒绝)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 检查玩家是否存活且不在战斗中
 *   2. 读取召唤者GUID和同意标志
 *   3. 如果同意,执行召唤逻辑
 */
void WorldSession::HandleSummonResponseOpcode(WorldPacket& recvData)
{
    // 如果玩家已死亡或在战斗中,则忽略召唤请求
    if (!_player->IsAlive() || _player->IsInCombat())
        return;

    ObjectGuid summoner_guid;
    bool agree;
    recvData >> summoner_guid;
    recvData >> agree;

    // 如果同意,尝试执行召唤
    _player->SummonIfPossible(agree);
}

/**
 * @brief 处理移动时间跳过操作码
 *
 * @职责 处理客户端报告的时间跳过情况
 *       当客户端检测到时间不同步时会发送此包,服务器需要更新移动时间并广播
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 移动单位GUID
 *        - timeSkipped: 跳过的时间(毫秒)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取移动单位GUID和跳过的时间
 *   2. 验证移动单位是否正确
 *   3. 更新移动信息中的时间
 *   4. 广播时间跳过数据包给周围玩家
 */
void WorldSession::HandleMoveTimeSkippedOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_MOVE_TIME_SKIPPED");

    ObjectGuid guid;
    uint32 timeSkipped;
    recvData >> guid.ReadAsPacked();
    recvData >> timeSkipped;

    // 验证是否是正确的移动单位
    if (!IsRightUnitBeingMoved(guid))
    {
        recvData.rfinish();                     // 防止警告刷屏
        return;
    }

    // 获取游戏客户端和活动移动单位,更新移动时间
    GameClient* client = GetGameClient();
    Unit* mover = client->GetActivelyMovedUnit();
    mover->m_movementInfo.time += timeSkipped;

    // 创建并广播时间跳过数据包
    WorldPacket data(MSG_MOVE_TIME_SKIPPED, recvData.size());
    data << guid.WriteAsPacked();
    data << timeSkipped;
    GetPlayer()->SendMessageToSet(&data, false);
}

/**
 * @brief 处理时间同步响应
 *
 * @职责 处理客户端发送的时间同步响应包
 *       计算服务器和客户端之间的时钟差值,用于移动时间校正
 *
 * @param recvData 接收到的网络包数据
 *        - counter: 时间同步计数器
 *        - clientTimestamp: 客户端时间戳
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取计数器和客户端时间戳
 *   2. 验证是否存在对应的待处理时间同步请求
 *   3. 计算往返时间(RTT)和延迟
 *   4. 计算时钟差值 = 服务器时间 - 客户端时间
 *   5. 将结果添加到时钟差值队列
 *   6. 计算新的时钟差值(使用统计过滤)
 *
 * @时间同步原理
 *   clockDelta = serverTime - clientTime
 *   其中:
 *   - serverTime: 客户端处理SMSG_TIME_SYNC_REQUEST包时服务器时钟显示的时间
 *   - clientTime: 客户端处理SMSG_TIME_SYNC_REQUEST包时客户端时钟显示的时间
 *
 *   一旦计算出clockDelta,就可以在知道客户端时间的情况下计算服务器时间:
 *   serverTime = clockDelta + clientTime
 */
void WorldSession::HandleTimeSyncResponse(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_TIME_SYNC_RESP");

    uint32 counter, clientTimestamp;
    recvData >> counter >> clientTimestamp;

    // 检查是否存在对应的待处理时间同步请求
    if (_pendingTimeSyncRequests.count(counter) == 0)
        return;

    // 获取发送请求时的服务器时间
    uint32 serverTimeAtSent = _pendingTimeSyncRequests.at(counter);
    _pendingTimeSyncRequests.erase(counter);

    // 请求从服务器发送到客户端、客户端处理并回复、响应从客户端发送回服务器所花费的时间
    // 我们将做两个假设:
    // 1) 我们假设请求处理时间等于0
    // 2) 我们假设数据包从服务器到客户端的传输时间与从客户端到服务器的传输时间相同
    uint32 roundTripDuration = getMSTimeDiff(serverTimeAtSent, recvData.GetReceivedTime());
    uint32 lagDelay = roundTripDuration / 2;

    /*
    clockDelta = serverTime - clientTime
    其中
    serverTime: 客户端处理SMSG_TIME_SYNC_REQUEST包时服务器时钟显示的时间
    clientTime: 客户端处理SMSG_TIME_SYNC_REQUEST包时客户端时钟显示的时间

    一旦计算出clockDelta,当我们知道某个事件在客户端时钟上的时间时,
    就可以计算该事件在服务器时钟上的时间,使用以下关系:
    serverTime = clockDelta + clientTime
    */
    int64 clockDelta = (int64)serverTimeAtSent + (int64)lagDelay - (int64)clientTimestamp;
    _timeSyncClockDeltaQueue->push_back(std::pair<int64, uint32>(clockDelta, roundTripDuration));
    ComputeNewClockDelta();
}

/**
 * @brief 计算新的时钟差值
 *
 * @职责 使用统计学方法计算服务器和客户端之间的时钟差值
 *       使用延迟过滤和标准差来减少TCP重传数据包引起的偏差
 *
 * @参数 无(使用_timeSyncClockDeltaQueue中的数据)
 *
 * @返回值 无(更新_timeSyncClockDelta成员变量)
 *
 * @主要流程
 *   1. 计算延迟的中位数和标准差
 *   2. 过滤掉延迟过大的样本(延迟 > 中位数 + 标准差)
 *   3. 对过滤后的时钟差值样本求平均值
 *   4. 如果新平均值与当前值的差异超过25ms,则更新时钟差值
 *
 * @算法来源 https://web.archive.org/web/20180430214420/http://www.mine-control.com/zack/timesync/timesync.html
 * 该技术用于减少因TCP数据包丢弃和重传引起的偏差
 */
void WorldSession::ComputeNewClockDelta()
{
    // 实现了以下描述的技术: https://web.archive.org/web/20180430214420/http://www.mine-control.com/zack/timesync/timesync.html
    // 用于减少因TCP数据包丢弃和重传引起的偏差

    using namespace boost::accumulators;

    // 创建延迟累积器,计算平均值、中位数和方差
    accumulator_set<uint32, features<tag::mean, tag::median, tag::variance(lazy)> > latencyAccumulator;

    // 累积所有延迟样本
    for (auto [_, roundTripDuration] : *_timeSyncClockDeltaQueue)
        latencyAccumulator(roundTripDuration);

    // 计算延迟的中位数和标准差
    uint32 latencyMedian = static_cast<uint32>(std::round(median(latencyAccumulator)));
    uint32 latencyStandardDeviation = static_cast<uint32>(std::round(sqrt(variance(latencyAccumulator))));

    // 创建过滤后的时钟差值累积器
    accumulator_set<int64, features<tag::mean> > clockDeltasAfterFiltering;
    uint32 sampleSizeAfterFiltering = 0;

    // 过滤掉延迟过大的样本(延迟 >= 中位数 + 标准差)
    for (auto [clockDelta, roundTripDuration] : *_timeSyncClockDeltaQueue)
    {
        if (roundTripDuration < latencyStandardDeviation + latencyMedian) {
            clockDeltasAfterFiltering(clockDelta);
            sampleSizeAfterFiltering++;
        }
    }

    // 如果过滤后还有样本,计算平均时钟差值
    if (sampleSizeAfterFiltering != 0)
    {
        int64 meanClockDelta = static_cast<int64>(std::round(mean(clockDeltasAfterFiltering)));
        // 如果新值与当前值的差异超过25ms,则更新
        if (std::abs(meanClockDelta - _timeSyncClockDelta) > 25)
            _timeSyncClockDelta = meanClockDelta;
    }
    // 如果没有过滤后的样本且当前时钟差值为0,使用最后一个样本
    else if (_timeSyncClockDelta == 0)
        _timeSyncClockDelta = _timeSyncClockDeltaQueue->back().first;
}
