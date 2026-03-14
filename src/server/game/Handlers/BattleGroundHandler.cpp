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
 * @file BattleGroundHandler.cpp
 * @brief 战场和竞技场网络消息处理器模块
 *
 * 本文件实现了 WorldSession 类中处理战场和竞技场相关网络消息的方法。
 * 主要功能包括：
 *
 * - 战场队列管理：加入队列、离开队列、队列状态查询
 * - 战场邀请处理：接受/拒绝战场邀请、传送玩家到战场
 * - 竞技场队列管理：支持2v2、3v3、5v5竞技场，积分赛和练习赛
 * - 战场信息查询：战场列表、玩家位置、记分板数据
 * - 挂机举报：举报战场中挂机玩家
 *
 * 消息处理流程：
 * 1. 客户端发送请求消息（CMSG_*）
 * 2. 服务端验证玩家状态和权限
 * 3. 执行相应的战场操作（加入队列、传送等）
 * 4. 向客户端发送响应消息（SMSG_*）
 *
 * 关键数据结构：
 * - Battleground：战场实例，管理战场逻辑
 * - BattlegroundQueue：战场队列，管理排队玩家
 * - GroupQueueInfo：队伍队列信息，记录排队队伍的详细信息
 * - ArenaTeam：竞技场队伍，管理竞技场积分
 *
 * 线程安全：
 * 所有方法都在 WorldSession 的网络线程中执行，通过 BattlegroundMgr
 * 进行线程间的同步操作。
 *
 * 性能考虑：
 * - 队列更新采用调度机制，避免频繁更新
 * - 使用对象缓存减少数据库查询
 * - 玩家位置查询仅返回必要信息（旗手位置）
 *
 * @see Battleground
 * @see BattlegroundMgr
 * @see BattlegroundQueue
 * @see ArenaTeam
 */

#include "WorldSession.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Chat.h"
#include "Common.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DisableMgr.h"
#include "GameTime.h"
#include "Group.h"
#include "Language.h"
#include "Log.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理战场管理员NPC交互请求
 *
 * 职责：
 *   处理玩家与战场管理员NPC的交互，验证玩家等级是否满足战场要求，
 *   并发送战场列表给玩家。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含战场管理员NPC的GUID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 从网络包中读取战场管理员NPC的GUID
 *   2. 验证玩家是否可以与该NPC交互（检查NPC标志）
 *   3. 暂停NPC的移动并设置其初始位置
 *   4. 获取该NPC对应的战场类型ID
 *   5. 检查玩家等级是否满足该战场的要求
 *   6. 如果满足要求，发送战场列表给玩家
 */
void WorldSession::HandleBattlemasterHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_BATTLEMASTER_HELLO Message from {}", guid.ToString());

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BATTLEMASTER);
    if (!unit)
        return;

    // 如果NPC有交互暂停定时器，则暂停NPC的移动
    if (uint32 pause = unit->GetMovementTemplate().GetInteractionPauseTimer())
        unit->PauseMovement(pause);
    // 设置NPC的初始位置（防止NPC移动后位置不同步）
    unit->SetHomePosition(unit->GetPosition());

    // 根据NPC的entry获取对应的战场类型ID
    BattlegroundTypeId bgTypeId = sBattlegroundMgr->GetBattleMasterBG(unit->GetEntry());

    // 检查玩家等级是否满足该战场的要求
    if (!_player->GetBGAccessByLevel(bgTypeId))
    {
                                                            // temp, must be gossip message...
        SendNotification(LANG_YOUR_BG_LEVEL_REQ_ERROR);
        return;
    }

    SendBattleGroundList(guid, bgTypeId);
}

/**
 * @brief 发送战场列表给玩家
 *
 * 职责：
 *   构建并发送战场列表网络包给玩家，显示可用的战场实例。
 *
 * 参数：
 *   guid     - 战场管理员NPC的GUID（可以为空）
 *   bgTypeId - 战场类型ID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 构建战场列表数据包
 *   2. 发送数据包给玩家
 */
void WorldSession::SendBattleGroundList(ObjectGuid guid, BattlegroundTypeId bgTypeId)
{
    WorldPacket data;
    sBattlegroundMgr->BuildBattlegroundListPacket(&data, guid, _player, bgTypeId, 0);
    SendPacket(&data);
}

/**
 * @brief 处理玩家加入战场队列请求
 *
 * 职责：
 *   处理玩家通过战场管理员NPC加入战场队列的请求，支持单人或组队加入，
 *   进行多项检查（逃兵debuff、等级限制、队列状态等），并将符合条件的
 *   玩家加入战场队列。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - guid: 战场管理员GUID
 *              - bgTypeId_: 战场类型ID（DBC ID）
 *              - instanceId: 实例ID（0表示选择第一个可用的）
 *              - joinAsGroup: 是否以队伍形式加入
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取并验证网络包数据
 *   2. 检查战场类型是否有效和是否被禁用
 *   3. 获取战场实例或模板
 *   4. 获取玩家所在等级段信息
 *   5. 如果是单人加入：
 *      - 检查是否在使用随机副本查找器
 *      - 检查RBAC权限
 *      - 检查逃兵debuff
 *      - 检查是否已在随机战场队列
 *      - 检查是否已有队列槽位
 *      - 检查冻结debuff
 *      - 将玩家加入队列并发送状态包
 *   6. 如果是组队加入：
 *      - 验证队伍和队长权限
 *      - 检查队伍是否满足加入条件
 *      - 将队伍成员全部加入队列
 *   7. 调度队列更新
 */
void WorldSession::HandleBattlemasterJoinOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 bgTypeId_;
    uint32 instanceId;
    uint8 joinAsGroup;
    bool isPremade = false;
    Group* grp = nullptr;

    recvData >> guid;                                      // 战场管理员GUID
    recvData >> bgTypeId_;                                 // 战场类型ID（DBC ID）
    recvData >> instanceId;                                // 实例ID，0表示选择第一个可用的
    recvData >> joinAsGroup;                               // 是否以队伍形式加入

    // 验证战场类型ID是否有效
    if (!sBattlemasterListStore.LookupEntry(bgTypeId_))
    {
        TC_LOG_ERROR("network", "Battleground: invalid bgtype ({}) received. possible cheater? player {}", bgTypeId_, _player->GetGUID().ToString());
        return;
    }

    // 检查该战场类型是否被禁用
    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId_, nullptr))
    {
        ChatHandler(this).PSendSysMessage(LANG_BG_DISABLED);
        return;
    }

    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgTypeId_);

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_BATTLEMASTER_JOIN Message from {}", guid.ToString());

    // 构建战场队列类型ID（战场类型，非竞技场）
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, 0);
    // 构建随机战场队列类型ID
    BattlegroundQueueTypeId bgQueueTypeIdRandom = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RB, 0);

    // 如果玩家已在战场中，忽略请求
    if (_player->InBattleground())
        return;

    // 获取战场实例或战场模板
    Battleground* bg = nullptr;
    if (instanceId)
        bg = sBattlegroundMgr->GetBattlegroundThroughClientInstance(instanceId, bgTypeId);

    if (!bg)
        bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return;

    // 获取玩家等级对应的战场等级段
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->GetLevel());
    if (!bracketEntry)
        return;

    GroupJoinBattlegroundResult err;

    // ========== 单人加入战场队列的检查流程 ==========
    if (!joinAsGroup)
    {
        // 检查玩家是否正在使用副本查找器或团队查找器
        if (GetPlayer()->isUsingLfg())
        {
            // 玩家正在使用副本查找器或团队查找器，不能同时排队战场
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_LFG_CANT_USE_BATTLEGROUND);
            GetPlayer()->SendDirectMessage(&data);
            return;
        }

        // 检查RBAC权限，判断玩家是否可以加入战场
        if (!_player->CanJoinToBattleground(bg))
        {
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_BATTLEGROUND_JOIN_TIMED_OUT);
            GetPlayer()->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否有逃兵debuff
        if (_player->IsDeserter())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否已在随机战场队列中
        if (_player->GetBattlegroundQueueIndex(bgQueueTypeIdRandom) < PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            // 玩家已在随机战场队列中
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_IN_RANDOM_BG);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否已在普通战场队列，且尝试加入随机战场
        if (_player->InBattlegroundQueue(true) && bgTypeId == BATTLEGROUND_RB)
        {
            // 玩家已在普通战场队列，不能加入随机战场
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_IN_NON_RANDOM_BG);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否已在此战场队列中
        if (_player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            // 玩家已在此队列中
            return;

        // 检查玩家是否有空闲的队列槽位
        if (!_player->HasFreeBattlegroundQueueId())
        {
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_BATTLEGROUND_TOO_MANY_QUEUES);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否有冻结debuff（防止作弊）
        if (_player->HasAura(9454))
            return;

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

        // 将玩家加入战场队列
        GroupQueueInfo* ginfo = bgQueue.AddGroup(_player, nullptr, bgTypeId, bracketEntry, 0, false, isPremade, 0, 0);
        // 获取平均等待时间
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        // 已验证队列槽位有效，获取队列槽位索引
        uint32 queueSlot = _player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPacket data;
        // 发送战场状态包（排队中）
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, ginfo->ArenaType, 0);
        SendPacket(&data);
        TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for bg queue type {} bg type {}: GUID {}, NAME {}",
                       bgQueueTypeId, bgTypeId, _player->GetGUID().ToString(), _player->GetName());
    }
    else
    {
        // ========== 组队加入战场队列的流程 ==========
        grp = _player->GetGroup();
        // 未找到队伍，返回错误
        if (!grp)
            return;
        // 只有队长才能将队伍加入战场队列
        if (grp->GetLeaderGUID() != _player->GetGUID())
            return;
        // 检查队伍是否满足加入战场的条件
        err = grp->CanJoinBattlegroundQueue(bg, bgQueueTypeId, 0, bg->GetMaxPlayersPerTeam(), false, 0);
        // 判断是否为预组队伍（人数达到最小队伍要求）
        isPremade = (grp->GetMembersCount() >= bg->GetMinPlayersPerTeam());

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo* ginfo = nullptr;
        uint32 avgTime = 0;

        // 如果队伍检查通过
        if (err > 0)
        {
            TC_LOG_DEBUG("bg.battleground", "Battleground: the following players are joining as group:");
            // 将队伍加入战场队列
            ginfo = bgQueue.AddGroup(_player, grp, bgTypeId, bracketEntry, 0, false, isPremade, 0, 0);
            // 获取平均等待时间
            avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        }

        // 遍历队伍成员，为每个成员发送加入结果
        for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;   // 此情况不应发生

            WorldPacket data;

            // 如果队伍检查失败，发送错误消息给该成员
            if (err <= 0)
            {
                sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, err);
                member->SendDirectMessage(&data);
                continue;
            }

            // 将成员加入队列
            uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

            // 发送战场状态包（排队中）
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, ginfo->ArenaType, 0);
            member->SendDirectMessage(&data);
            // 发送加入结果包
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, err);
            member->SendDirectMessage(&data);
            TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for bg queue type {} bg type {}: GUID {}, NAME {}",
                bgQueueTypeId, bgTypeId, member->GetGUID().ToString(), member->GetName());
        }
        TC_LOG_DEBUG("bg.battleground", "Battleground: group end");
    }
    // 调度队列更新（检查是否可以开始新的战场）
    sBattlegroundMgr->ScheduleQueueUpdate(0, 0, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
}

/**
 * @brief 处理战场玩家位置查询请求
 *
 * 职责：
 *   处理玩家请求战场中其他玩家位置信息的请求，主要用于夺旗战场
 *   （如战歌峡谷）中显示旗手的位置。
 *
 * 参数：
 *   recvData - 接收的网络包数据（未使用）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 获取玩家当前所在的战场
 *   2. 获取联盟和部落的旗手信息
 *   3. 统计旗手数量
 *   4. 构建并发送玩家位置数据包，包含：
 *      - 玩家位置数量
 *      - 旗手的GUID和坐标信息
 */
void WorldSession::HandleBattlegroundPlayerPositionsOpcode(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd MSG_BATTLEGROUND_PLAYER_POSITIONS Message");

    Battleground* bg = _player->GetBattleground();
    if (!bg)                                                 // 如果玩家不在战场中，无法接收此消息
        return;

    uint32 flagCarrierCount = 0;
    Player* allianceFlagCarrier = nullptr;
    Player* hordeFlagCarrier = nullptr;

    // 查找联盟旗手
    if (ObjectGuid guid = bg->GetFlagPickerGUID(TEAM_ALLIANCE))
    {
        allianceFlagCarrier = ObjectAccessor::FindPlayer(guid);
        if (allianceFlagCarrier)
            ++flagCarrierCount;
    }

    // 查找部落旗手
    if (ObjectGuid guid = bg->GetFlagPickerGUID(TEAM_HORDE))
    {
        hordeFlagCarrier = ObjectAccessor::FindPlayer(guid);
        if (hordeFlagCarrier)
            ++flagCarrierCount;
    }

    WorldPacket data(MSG_BATTLEGROUND_PLAYER_POSITIONS, 4 + 4 + 16 * flagCarrierCount);
    // 用于发送多个玩家位置（曾在奥特兰克山谷中使用）
    data << 0;  // CGBattlefieldInfo__m_numPlayerPositions（预留字段）
    /*
    for (CGBattlefieldInfo__m_numPlayerPositions)
        data << guid << posx << posy;
    */
    // 发送旗手数量和位置信息
    data << flagCarrierCount;
    if (allianceFlagCarrier)
    {
        data << uint64(allianceFlagCarrier->GetGUID());
        data << float(allianceFlagCarrier->GetPositionX());
        data << float(allianceFlagCarrier->GetPositionY());
    }

    if (hordeFlagCarrier)
    {
        data << uint64(hordeFlagCarrier->GetGUID());
        data << float(hordeFlagCarrier->GetPositionX());
        data << float(hordeFlagCarrier->GetPositionY());
    }

    SendPacket(&data);
}

/**
 * @brief 处理PVP日志数据请求
 *
 * 职责：
 *   处理玩家请求查看战场记分板数据的请求，发送战场的详细统计信息。
 *
 * 参数：
 *   recvData - 接收的网络包数据（未使用）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 获取玩家当前所在的战场
 *   2. 如果是竞技场比赛，直接返回（竞技场只在结束时发送）
 *   3. 构建PVP日志数据包（包含伤害、治疗、击杀等统计信息）
 *   4. 发送数据包给玩家
 */
void WorldSession::HandlePVPLogDataOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd MSG_PVP_LOG_DATA Message");

    Battleground* bg = _player->GetBattleground();
    if (!bg)
        return;

    // Prevent players from sending BuildPvpLogDataPacket in an arena except for when sent in BattleGround::EndBattleGround.
    if (bg->isArena())
        return;

    WorldPacket data;
    bg->BuildPvPLogDataPacket(data);
    SendPacket(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent MSG_PVP_LOG_DATA Message");
}

/**
 * @brief 处理战场列表请求
 *
 * 职责：
 *   处理玩家请求战场列表的操作，支持从战场管理员NPC或UI界面发起请求。
 *   发送可用战场实例列表给玩家。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - bgTypeId: 战场类型ID（来自DBC）
 *              - fromWhere: 请求来源（0-战场管理员NPC，1-UI界面）
 *              - canGainXP: 是否获得经验（锁定经验的玩家有独立的战场队列）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 从网络包中读取战场类型ID、请求来源和经验获取标志
 *   2. 验证战场类型ID是否有效
 *   3. 构建战场列表数据包
 *   4. 发送数据包给玩家
 */
void WorldSession::HandleBattlefieldListOpcode(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_BATTLEFIELD_LIST Message");

    uint32 bgTypeId;
    recvData >> bgTypeId;                                  // id from DBC

    uint8 fromWhere;
    recvData >> fromWhere;                                 // 0 - battlemaster (lua: ShowBattlefieldList), 1 - UI (lua: RequestBattlegroundInstanceInfo)

    uint8 canGainXP;
    recvData >> canGainXP;                                 // players with locked xp have their own bg queue on retail

    BattlemasterListEntry const* bl = sBattlemasterListStore.LookupEntry(bgTypeId);
    if (!bl)
    {
        TC_LOG_DEBUG("bg.battleground", "BattlegroundHandler: invalid bgtype ({}) with player (Name: {}, {}) received.", bgTypeId, _player->GetName(), _player->GetGUID().ToString());
        return;
    }

    WorldPacket data;
    sBattlegroundMgr->BuildBattlegroundListPacket(&data, ObjectGuid::Empty, _player, BattlegroundTypeId(bgTypeId), fromWhere);
    SendPacket(&data);
}

/**
 * @brief 处理战场传送响应（接受或拒绝战场邀请）
 *
 * 职责：
 *   处理玩家对战场邀请的响应，支持两种操作：
 *   1. 接受邀请并传送到战场（action = 1）
 *   2. 拒绝邀请并离开队列（action = 0）
 *   同时处理逃兵debuff、等级变化、竞技场积分等特殊情况。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - type: 竞技场类型（如果是竞技场）
 *              - unk2: 未知标志
 *              - bgTypeId_: 战场类型ID（来自DBC）
 *              - unk: 未知常量
 *              - action: 操作类型（1=进入战场，0=离开队列）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   【接受战场邀请（action = 1）】
 *   1. 验证战场类型和玩家队列状态
 *   2. 获取玩家的队列信息和战场实例
 *   3. 检查逃兵debuff和等级限制
 *   4. 检查冻结debuff
 *   5. 复活玩家（如果已死亡）
 *   6. 停止飞行路线
 *   7. 设置战场入口点
 *   8. 从队列中移除玩家
 *   9. 设置目标战场实例ID和队伍
 *   10. 传送玩家到战场
 *
 *   【拒绝战场邀请（action = 0）】
 *   1. 验证是否可以离开队列（竞技场特殊规则）
 *   2. 如果是积分竞技场，记录失败并扣分
 *   3. 从队列中移除玩家
 *   4. 更新队列状态
 *   5. 如果配置启用，记录逃兵行为到数据库
 */
void WorldSession::HandleBattleFieldPortOpcode(WorldPacket &recvData)
{
    uint8 type;                                             // arenatype if arena
    uint8 unk2;                                             // unk, can be 0x0 (may be if was invited?) and 0x1
    uint32 bgTypeId_;                                       // type id from dbc
    uint16 unk;                                             // 0x1F90 constant?
    uint8 action;                                           // enter battle 0x1, leave queue 0x0

    recvData >> type >> unk2 >> bgTypeId_ >> unk >> action;
    // 验证战场类型ID是否有效
    if (!sBattlemasterListStore.LookupEntry(bgTypeId_))
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}. Invalid BgType!",
            GetPlayerInfo(), type, unk2, bgTypeId_, action);
        return;
    }

    // 检查玩家是否在战场队列中
    if (!_player->InBattlegroundQueue())
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}. Player not in queue!",
            GetPlayerInfo(), type, unk2, bgTypeId_, action);
        return;
    }

    // 从战场队列中获取队伍队列信息
    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgTypeId_);
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, type);
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    // 必须使用临时变量，因为GroupQueueInfo指针可能在BattlegroundQueue::RemovePlayer()函数中被删除
    GroupQueueInfo ginfo;
    if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}. Player not in queue (No player Group Info)!",
            GetPlayerInfo(), type, unk2, bgTypeId_, action);
        return;
    }
    // 如果action == 1（接受邀请），则必须有实例ID
    if (!ginfo.IsInvitedToBGInstanceGUID && action == 1)
    {
        TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}. Player is not invited to any bg!",
            GetPlayerInfo(), type, unk2, bgTypeId_, action);
        return;
    }

    // 获取战场实例
    Battleground* bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
    if (!bg)
    {
        if (action)
        {
            TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}. Cant find BG with id {}!",
                GetPlayerInfo(), type, unk2, bgTypeId_, action, ginfo.IsInvitedToBGInstanceGUID);
            return;
        }

        // 如果是离开队列操作，获取战场模板即可
        bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (!bg)
        {
            TC_LOG_ERROR("network", "BattlegroundHandler: bg_template not found for type id {}.", bgTypeId);
            return;
        }
    }

    TC_LOG_DEBUG("bg.battleground", "CMSG_BATTLEFIELD_PORT {} ArenaType: {}, Unk: {}, BgType: {}, Action: {}.",
        GetPlayerInfo(), type, unk2, bgTypeId_, action);

    // 获取玩家等级对应的战场等级段
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->GetLevel());
    if (!bracketEntry)
        return;

    // 防作弊检查（非竞技场情况下）
    if (action == 1 && ginfo.ArenaType == 0)
    {
        // 如果玩家尝试进入战场（非竞技场）且有逃兵debuff，将其移出队列
        if (_player->IsDeserter())
        {
            // 发送战场命令结果，显示友好消息
            WorldPacket data2;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data2, ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS);
            _player->SendDirectMessage(&data2);
            action = 0;
            TC_LOG_DEBUG("bg.battleground", "Player {} {} has a deserter debuff, do not port him to battleground!", _player->GetName(), _player->GetGUID().ToString());
        }
        // 如果玩家等级超过战场最大等级，不允许进入（可能发生在排队期间升级）
        if (_player->GetLevel() > bg->GetMaxLevel())
        {
            TC_LOG_ERROR("network", "Player {} {} has level ({}) higher than maxlevel ({}) of battleground ({})! Do not port him to battleground!",
                _player->GetName(), _player->GetGUID().ToString(), _player->GetLevel(), bg->GetMaxLevel(), bg->GetTypeID());
            action = 0;
        }
    }
    uint32 queueSlot = _player->GetBattlegroundQueueIndex(bgQueueTypeId);
    WorldPacket data;
    if (action)
    {
        // ========== 接受战场邀请流程 ==========
        // 检查冻结debuff
        if (_player->HasAura(9454))
            return;

        // 验证玩家是否被邀请到此战场队列
        if (!_player->IsInvitedForBattlegroundQueueType(bgQueueTypeId))
            return;                                 // 可能是作弊？

        // 记录战场入口点（用于战场结束后传送回来）
        if (!_player->InBattleground())
            _player->SetBattlegroundEntryPoint();

        // 复活玩家（如果已死亡）
        if (!_player->IsAlive())
        {
            _player->ResurrectPlayer(1.0f);
            _player->SpawnCorpseBones();
        }
        // 停止飞行路线
        _player->FinishTaxiFlight();

        // 发送战场状态包（进行中）
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_IN_PROGRESS, 0, bg->GetStartTime(), bg->GetArenaType(), ginfo.Team);
        _player->SendDirectMessage(&data);

        // 从战场管理器中移除战场队列状态
        bgQueue.RemovePlayer(_player->GetGUID(), false);
        // 如果战场"跳跃"不应该添加逃兵debuff，这里仍然需要此操作
        // 同时这也是防止在SetBattlegroundId设置为新的战场后在旧战场卡住所必需的
        if (Battleground* currentBg = _player->GetBattleground())
            currentBg->RemovePlayerAtLeave(_player->GetGUID(), false, true);

        // 设置目标战场实例ID
        _player->SetBattlegroundId(bg->GetInstanceID(), bgTypeId);
        // 设置目标队伍
        _player->SetBGTeam(ginfo.Team);

        // 传送玩家到战场
        // bg->HandleBeforeTeleportToBattleground(_player);
        sBattlegroundMgr->SendToBattleground(_player, ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
        // 在HandleMoveWorldPortAck()中添加玩家
        // bg->AddPlayer(_player, team);
        TC_LOG_DEBUG("bg.battleground", "Battleground: player {} {} joined battle for bg {}, bgtype {}, queue type {}.", _player->GetName(), _player->GetGUID().ToString(), bg->GetInstanceID(), bg->GetTypeID(), bgQueueTypeId);
    }
    else // 离开队列
    {
        // ========== 拒绝战场邀请/离开队列流程 ==========
        // 如果是竞技场且状态已超过等待排队状态，不允许离开
        if (bg->isArena() && bg->GetStatus() > STATUS_WAIT_QUEUE)
            return;

        // 如果玩家在比赛开始前离开积分竞技场，视为已比赛但失败
        if (ginfo.IsRated && ginfo.IsInvitedToBGInstanceGUID)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ginfo.Team);
            if (at)
            {
                TC_LOG_DEBUG("bg.battleground", "UPDATING memberLost's personal arena rating for {} by opponents rating: {}, because he has left queue!", _player->GetGUID().ToString(), ginfo.OpponentsTeamRating);
                at->MemberLost(_player, ginfo.OpponentsMatchmakerRating);
                at->SaveToDB();
            }
        }
        // 必须这样调用，如果将此调用移到queue->removeplayer中，会导致bug
        _player->RemoveBattlegroundQueueId(bgQueueTypeId);
        // 发送战场状态包（无状态）
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_NONE, 0, 0, 0, 0);
        bgQueue.RemovePlayer(_player->GetGUID(), true);
        // 玩家离开队列，应该更新队列 - 不更新竞技场队列
        if (!ginfo.ArenaType)
            sBattlegroundMgr->ScheduleQueueUpdate(ginfo.ArenaMatchmakerRating, ginfo.ArenaType, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
        SendPacket(&data);
        TC_LOG_DEBUG("bg.battleground", "Battleground: player {} {} left queue for bgtype {}, queue type {}.", _player->GetName(), _player->GetGUID().ToString(), bg->GetTypeID(), bgQueueTypeId);

        // 如果配置启用，追踪玩家在受邀后拒绝加入战场的行为（逃兵行为）
        if (bg->isBattleground() && sWorld->getBoolConfig(CONFIG_BATTLEGROUND_TRACK_DESERTERS) &&
                (bg->GetStatus() == STATUS_IN_PROGRESS || bg->GetStatus() == STATUS_WAIT_JOIN))
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_DESERTER_TRACK);
            stmt->setUInt32(0, _player->GetGUID().GetCounter());
            stmt->setUInt8(1, BG_DESERTION_TYPE_LEAVE_QUEUE);
            CharacterDatabase.Execute(stmt);
        }
    }
}

/**
 * @brief 处理玩家离开战场请求
 *
 * 职责：
 *   处理玩家主动离开战场的请求，执行离开战场的操作。
 *   在战斗状态下有特殊限制。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - unk1: 未知字节
 *              - unk2: 未知字节
 *              - BattlegroundTypeId: 战场类型ID
 *              - unk3: 未知字
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取网络包数据（实际未使用）
 *   2. 检查玩家是否在战斗状态
 *   3. 如果在战斗中，只有当战场状态为STATUS_WAIT_LEAVE时才允许离开
 *   4. 调用LeaveBattleground()执行离开战场的操作
 */
void WorldSession::HandleBattlefieldLeaveOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_LEAVE_BATTLEFIELD Message");

    recvData.read_skip<uint8>();                           // 未知字节1
    recvData.read_skip<uint8>();                           // 未知字节2
    recvData.read_skip<uint32>();                          // 战场类型ID
    recvData.read_skip<uint16>();                          // 未知字

    // 不允许玩家在战斗中离开战场，除非战场已结束
    if (_player->IsInCombat())
        if (Battleground* bg = _player->GetBattleground())
            if (bg->GetStatus() != STATUS_WAIT_LEAVE)
                return;

    _player->LeaveBattleground();
}

/**
 * @brief 处理战场状态查询请求
 *
 * 职责：
 *   处理玩家请求当前所有战场队列状态的请求，更新并返回玩家
 *   在所有战场队列中的状态信息。玩家最多可以同时在PLAYER_MAX_BATTLEGROUND_QUEUES
 *   个队列中等待。
 *
 * 参数：
 *   recvData - 接收的网络包数据（未使用）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 遍历玩家所有可能的队列槽位（最多PLAYER_MAX_BATTLEGROUND_QUEUES个）
 *   2. 对每个队列槽位：
 *      a. 获取队列类型ID
 *      b. 如果玩家已在战场中且类型匹配，发送STATUS_IN_PROGRESS状态
 *      c. 如果玩家已被邀请加入战场，发送STATUS_WAIT_JOIN状态和剩余时间
 *      d. 如果玩家正在排队等待，发送STATUS_WAIT_QUEUE状态和预估等待时间
 *   3. 所有状态包都包含战场类型、竞技场类型、队伍信息等详细信息
 */
void WorldSession::HandleBattlefieldStatusOpcode(WorldPacket & /*recvData*/)
{
    // empty opcode
    TC_LOG_DEBUG("network", "WORLD: Battleground status");

    WorldPacket data;
    // 必须在此处更新所有队列状态
    Battleground* bg = nullptr;
    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId bgQueueTypeId = _player->GetBattlegroundQueueTypeId(i);
        if (!bgQueueTypeId)
            continue;
        BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(bgQueueTypeId);
        uint8 arenaType = BattlegroundMgr::BGArenaType(bgQueueTypeId);

        // 检查玩家是否已在战场中
        if (bgTypeId == _player->GetBattlegroundTypeId())
        {
            bg = _player->GetBattleground();
            // 无法从玩家类检查任何变量，因为玩家类不知道玩家是在2v2 / 3v3还是5v5竞技场中
            // 所以必须使用bg指针获取该信息
            if (bg && bg->GetArenaType() == arenaType)
            {
                // 此行已检查，只是不确定GetStartTime在战场结束后是否会自动改变
                // 发送战场进行中状态
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, i, STATUS_IN_PROGRESS, bg->GetEndTime(), bg->GetStartTime(), arenaType, _player->GetBGTeam());
                SendPacket(&data);
                continue;
            }
        }

        // 发送队列更新给玩家 - 玩家可能已被邀请！
        // 获取队列状态的GroupQueueInfo
        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        GroupQueueInfo ginfo;
        if (!bgQueue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
            continue;

        // 如果玩家已被邀请加入战场实例
        if (ginfo.IsInvitedToBGInstanceGUID)
        {
            bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
            if (!bg)
                continue;
            uint32 remainingTime = getMSTimeDiff(GameTime::GetGameTimeMS(), ginfo.RemoveInviteTime);
            // 发送已受邀状态
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, i, STATUS_WAIT_JOIN, remainingTime, 0, arenaType, 0);
            SendPacket(&data);
        }
        else
        {
            // 玩家正在排队等待
            bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
            if (!bg)
                continue;

            // 获取玩家等级对应的战场等级段
            PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->GetLevel());
            if (!bracketEntry)
                continue;

            uint32 avgTime = bgQueue.GetAverageQueueWaitTime(&ginfo, bracketEntry->GetBracketId());
            // 发送排队等待状态
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, i, STATUS_WAIT_QUEUE, avgTime, getMSTimeDiff(ginfo.JoinTime, GameTime::GetGameTimeMS()), arenaType, 0);
            SendPacket(&data);
        }
    }
}

/**
 * @brief 处理玩家加入竞技场队列请求
 *
 * 职责：
 *   处理玩家通过战场管理员NPC加入竞技场队列的请求，支持2v2、3v3、5v5
 *   三种竞技场类型，可以单人或组队加入，支持练习赛和积分赛。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - guid: 竞技场管理员NPC的GUID
 *              - arenaslot: 竞技场槽位（0=2v2, 1=3v3, 2=5v5）
 *              - asGroup: 是否以队伍形式加入
 *              - isRated: 是否为积分赛
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取并验证网络包数据
 *   2. 如果是积分赛但未组队，直接返回（积分赛必须组队）
 *   3. 检查玩家是否已在战场中
 *   4. 验证NPC是否为战场管理员
 *   5. 根据槽位确定竞技场类型（2v2/3v3/5v5）
 *   6. 获取竞技场模板并检查是否被禁用
 *   7. 获取等级段信息
 *
 *   【单人加入】
 *   8. 检查是否在使用副本查找器
 *   9. 检查RBAC权限
 *   10. 检查是否已在队列中
 *   11. 检查是否有空闲队列槽位
 *   12. 加入队列
 *
 *   【组队加入】
 *   8. 验证队伍和队长权限
 *   9. 检查队伍是否满足加入条件
 *   10. 如果是积分赛，获取竞技场队伍信息和积分
 *   11. 将所有队员加入队列
 *   12. 发送队列状态给所有队员
 *
 *   13. 调度队列更新
 */
void WorldSession::HandleBattlemasterJoinArena(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_BATTLEMASTER_JOIN_ARENA");

    ObjectGuid guid;                                        // arena Battlemaster guid
    uint8 arenaslot;                                        // 2v2, 3v3 or 5v5
    uint8 asGroup;                                          // asGroup
    uint8 isRated;                                          // isRated
    Group* grp = nullptr;

    recvData >> guid >> arenaslot >> asGroup >> isRated;

    // 如果是积分赛但未组队，忽略请求
    if (isRated && !asGroup)
        return;

    // 如果玩家已在战场或战场队列中，忽略请求
    if (_player->InBattleground())
        return;

    // 验证NPC是否为战场管理员
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BATTLEMASTER);
    if (!unit)
        return;

    uint8 arenatype = 0;
    uint32 arenaRating = 0;
    uint32 matchmakerRating = 0;
    uint32 previousOpponents = 0;

    // 根据竞技场槽位确定竞技场类型
    switch (arenaslot)
    {
        case 0:
            arenatype = ARENA_TYPE_2v2;
            break;
        case 1:
            arenatype = ARENA_TYPE_3v3;
            break;
        case 2:
            arenatype = ARENA_TYPE_5v5;
            break;
        default:
            TC_LOG_ERROR("network", "Unknown arena slot {} at HandleBattlemasterJoinArena()", arenaslot);
            return;
    }

    // 检查竞技场模板是否存在
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
    if (!bg)
    {
        TC_LOG_ERROR("network", "Battleground: template bg (all arenas) not found");
        return;
    }

    // 检查竞技场是否被禁用
    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
    {
        ChatHandler(this).PSendSysMessage(LANG_ARENA_DISABLED);
        return;
    }

    BattlegroundTypeId bgTypeId = bg->GetTypeID();
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, arenatype);
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), _player->GetLevel());
    if (!bracketEntry)
        return;

    GroupJoinBattlegroundResult err = ERR_GROUP_JOIN_BATTLEGROUND_FAIL;

    // ========== 单人加入竞技场队列 ==========
    if (!asGroup)
    {
        // 检查玩家是否在使用副本查找器或团队查找器
        if (_player->isUsingLfg())
        {
            // 玩家正在使用副本查找器或团队查找器
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_LFG_CANT_USE_BATTLEGROUND);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否可以加入战场
        if (!_player->CanJoinToBattleground(bg))
        {
            WorldPacket data;
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, ERR_BATTLEGROUND_JOIN_FAILED);
            _player->SendDirectMessage(&data);
            return;
        }

        // 检查玩家是否已在此队列中
        if (_player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            // 玩家已在此队列中
            return;
        // 检查玩家是否有空闲队列槽位
        if (!_player->HasFreeBattlegroundQueueId())
            return;
    }
    else
    {
        // ========== 组队加入竞技场队列 ==========
        grp = _player->GetGroup();
        // 未找到队伍，返回错误
        if (!grp)
            return;
        // 只有队长才能将队伍加入队列
        if (grp->GetLeaderGUID() != _player->GetGUID())
            return;
        // 检查队伍是否满足加入竞技场队列的条件
        err = grp->CanJoinBattlegroundQueue(bg, bgQueueTypeId, arenatype, arenatype, isRated != 0, arenaslot);
    }

    uint32 ateamId = 0;

    // 如果是积分赛
    if (isRated)
    {
        // 获取玩家的竞技场队伍ID
        ateamId = _player->GetArenaTeamId(arenaslot);
        // 仅在此处检查真实的竞技场队伍是否存在（如果移到group->CanJoin..()中，则需要获取两次）
        ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
        if (!at)
        {
            // 玩家不在竞技场队伍中，发送错误消息
            _player->GetSession()->SendNotInArenaTeamPacket(arenatype);
            return;
        }
        // 获取用于排队的队伍积分
        arenaRating = at->GetRating();
        matchmakerRating = at->GetAverageMMR(grp);
        // 竞技场队伍ID必须与队伍中所有成员匹配

        if (arenaRating <= 0)
            arenaRating = 1;

        // 获取上次对手信息（用于避免连续匹配相同对手）
        previousOpponents = at->GetPreviousOpponents();
    }

    BattlegroundQueue &bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    if (asGroup)
    {
        uint32 avgTime = 0;

        // 如果队伍检查通过
        if (err > 0)
        {
            TC_LOG_DEBUG("bg.battleground", "Battleground: arena join as group start");
            if (isRated)
            {
                TC_LOG_DEBUG("bg.battleground", "Battleground: arena team id {}, leader {} queued with matchmaker rating {} for type {}", _player->GetArenaTeamId(arenaslot), _player->GetName(), matchmakerRating, arenatype);
                bg->SetRated(true);
            }
            else
                bg->SetRated(false);

            // 将队伍加入竞技场队列
            GroupQueueInfo* ginfo = bgQueue.AddGroup(_player, grp, bgTypeId, bracketEntry, arenatype, isRated != 0, false, arenaRating, matchmakerRating, ateamId, previousOpponents);
            avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        }

        // 遍历队伍成员，为每个成员发送加入结果
        for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            WorldPacket data;

            // 如果队伍检查失败，发送错误消息给该成员
            if (err <= 0)
            {
                sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, err);
                member->SendDirectMessage(&data);
                continue;
            }

            // 将成员加入队列
            uint32 queueSlot = member->AddBattlegroundQueueId(bgQueueTypeId);

            // 发送战场状态包（排队中）
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, 0);
            member->SendDirectMessage(&data);
            // 发送加入结果包
            sBattlegroundMgr->BuildGroupJoinedBattlegroundPacket(&data, err);
            member->SendDirectMessage(&data);
            TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for arena as group bg queue type {} bg type {}: {}, NAME {}", bgQueueTypeId, bgTypeId, member->GetGUID().ToString(), member->GetName());
        }
    }
    else
    {
        // ========== 单人加入竞技场队列 ==========
        // 将玩家加入竞技场队列
        GroupQueueInfo* ginfo = bgQueue.AddGroup(_player, nullptr, bgTypeId, bracketEntry, arenatype, isRated != 0, false, arenaRating, matchmakerRating, ateamId, previousOpponents);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo, bracketEntry->GetBracketId());
        uint32 queueSlot = _player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPacket data;
        // 发送战场状态包（排队中）
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, 0);
        SendPacket(&data);
        TC_LOG_DEBUG("bg.battleground", "Battleground: player joined queue for arena, skirmish, bg queue type {} bg type {}: {}, NAME {}", bgQueueTypeId, bgTypeId, _player->GetGUID().ToString(), _player->GetName());
    }
    // 调度队列更新
    sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, arenatype, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());
}

/**
 * @brief 处理举报战场中挂机玩家
 *
 * 职责：
 *   处理玩家举报战场中挂机（AFK）玩家的请求，将举报信息传递给
 *   被举报玩家进行处理。如果被举报玩家收到足够多的举报，可能会
 *   受到惩罚。
 *
 * 参数：
 *   recvData - 接收的网络包数据，包含：
 *              - playerGuid: 被举报玩家的GUID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 从网络包中读取被举报玩家的GUID
 *   2. 查找被举报玩家对象
 *   3. 如果找不到玩家，记录日志并返回
 *   4. 调用被举报玩家的ReportedAfkBy方法处理举报
 */
void WorldSession::HandleReportPvPAFK(WorldPacket& recvData)
{
    ObjectGuid playerGuid;
    recvData >> playerGuid;
    Player* reportedPlayer = ObjectAccessor::FindPlayer(playerGuid);

    // 如果找不到被举报的玩家
    if (!reportedPlayer)
    {
        TC_LOG_INFO("bg.reportpvpafk", "WorldSession::HandleReportPvPAFK: {} [IP: {}] reported {}", _player->GetName(), _player->GetSession()->GetRemoteAddress(), playerGuid.ToString());
        return;
    }

    TC_LOG_DEBUG("bg.battleground", "WorldSession::HandleReportPvPAFK: {} reported {}", _player->GetName(), reportedPlayer->GetName());

    // 调用被举报玩家的举报处理方法
    reportedPlayer->ReportedAfkBy(_player);
}
