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
 * @file BattlegroundAB.cpp
 * @brief 阿拉希盆地（Arathi Basin）战场实现文件
 *
 * 本文件实现了阿拉希盆地战场的核心逻辑，包括：
 * - 资源点占领和状态切换
 * - 资源分数累积计算
 * - 胜负判定
 * - 玩家交互（点击旗帜）
 * - 成就判定
 *
 * 阿拉希盆地是一个15v15的资源争夺战场，双方争夺5个资源点，
 * 占领资源点的队伍会定期获得资源分数，先达到1600分的队伍获胜。
 */

#include "BattlegroundAB.h"
#include "BattlegroundMgr.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Random.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

/**
 * @brief 构建玩家目标数据块
 * @param data 世界数据包
 *
 * 将玩家的目标数据（突袭基地次数、防守基地次数）打包到数据包中
 * 用于发送给客户端显示战场结算界面
 */
void BattlegroundABScore::BuildObjectivesBlock(WorldPacket& data)
{
    data << uint32(2);                    // 目标数量（2个目标）
    data << uint32(BasesAssaulted);       // 突袭基地次数
    data << uint32(BasesDefended);        // 防守基地次数
}

/**
 * @brief 构造函数
 *
 * 初始化战场成员变量：
 * - 设置对象和生物容器大小
 * - 初始化节点状态和计时器
 * - 初始化队伍分数和荣誉声望计数器
 *
 * 调用时机：战场创建时
 */
BattlegroundAB::BattlegroundAB()
{
    m_IsInformedNearVictory = false;      // 尚未发送即将胜利警告
    m_BuffChange = true;                  // 允许buff变化
    BgObjects.resize(BG_AB_OBJECT_MAX);   // 调整游戏对象容器大小
    BgCreatures.resize(BG_AB_ALL_NODES_COUNT + 5); // 调整生物容器大小（7个节点 + 5个光环触发器）

    // 初始化所有资源点的状态和计时器
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
    {
        m_Nodes[i] = 0;                   // 节点状态：中立
        m_prevNodes[i] = 0;               // 前一状态：中立
        m_NodeTimers[i] = 0;              // 节点计时器：0
        m_BannerTimers[i].timer = 0;      // 旗帜计时器：0
        m_BannerTimers[i].type = 0;       // 旗帜类型：中立
        m_BannerTimers[i].teamIndex = 0;  // 队伍索引：无
    }

    // 初始化队伍相关数据
    for (uint8 i = 0; i < PVP_TEAMS_COUNT; ++i)
    {
        m_lastTick[i] = 0;                        // 上次资源更新时间：0
        m_HonorScoreTics[i] = 0;                  // 荣誉分数累计器：0
        m_ReputationScoreTics[i] = 0;             // 声望分数累计器：0
        m_TeamScores500Disadvantage[i] = false;   // 分数劣势标志：false
    }

    m_HonorTics = 0;                       // 荣誉点数间隔（将在Reset中设置）
    m_ReputationTics = 0;                  // 声望点数间隔（将在Reset中设置）
}

/**
 * @brief 析构函数
 *
 * 清理战场资源
 */
BattlegroundAB::~BattlegroundAB() { }

/**
 * @brief 战场更新实现
 * @param diff 时间差（毫秒）
 *
 * 每帧调用，处理：
 * 1. 旗帜生成延迟计时
 * 2. 资源点占领计时（从争夺状态变为占领状态需要60秒）
 * 3. 资源分数累积（根据占领的资源点数量）
 * 4. 荣誉和声望奖励发放
 * 5. 即将胜利警告
 * 6. 胜负判定
 *
 * 调用时机：每帧更新时由Battleground::Update调用
 * 性能注意：此函数每帧调用，需要优化处理
 */
void BattlegroundAB::PostUpdateImpl(uint32 diff)
{
    // 只在战斗进行中处理
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        // 统计双方占领的资源点数量
        int team_points[PVP_TEAMS_COUNT] = { 0, 0 };

        // 遍历所有资源点
        for (int node = 0; node < BG_AB_DYNAMIC_NODES_COUNT; ++node)
        {
            // 处理旗帜生成延迟（3秒延迟后生成新旗帜替代被删除的旗帜）
            if (m_BannerTimers[node].timer)
            {
                if (m_BannerTimers[node].timer > diff)
                    m_BannerTimers[node].timer -= diff;
                else
                {
                    // 计时结束，生成旗帜
                    m_BannerTimers[node].timer = 0;
                    _CreateBanner(node, m_BannerTimers[node].type, m_BannerTimers[node].teamIndex, false);
                }
            }

            // 处理资源点占领计时（60秒后从争夺状态变为占领状态）
            if (m_NodeTimers[node])
            {
                if (m_NodeTimers[node] > diff)
                    m_NodeTimers[node] -= diff;
                else
                {
                    // 计时结束，资源点被占领
                    m_NodeTimers[node] = 0;

                    // 计算占领队伍索引
                    uint8 teamIndex = m_Nodes[node]-1;

                    // 更新节点状态：从争夺状态变为占领状态
                    m_prevNodes[node] = m_Nodes[node];
                    m_Nodes[node] += 2;  // 状态值+2（争夺->占领）

                    // 删除争夺状态的旗帜
                    _DelBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex);
                    // 生成占领状态的旗帜
                    _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex, true);
                    // 发送节点更新给客户端
                    _SendNodeUpdate(node);
                    // 处理节点被占领事件（生成灵魂医者等）
                    _NodeOccupied(node, (teamIndex == TEAM_ALLIANCE) ? ALLIANCE : HORDE);

                    // 发送系统消息和播放音效
                    if (teamIndex == TEAM_ALLIANCE)
                    {
                        SendBroadcastText(ABNodes[node].TextAllianceTaken, CHAT_MSG_BG_SYSTEM_ALLIANCE);
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_ALLIANCE);
                    }
                    else
                    {
                        SendBroadcastText(ABNodes[node].TextHordeTaken, CHAT_MSG_BG_SYSTEM_HORDE);
                        PlaySoundToAll(BG_AB_SOUND_NODE_CAPTURED_HORDE);
                    }
                }
            }

            // 统计每个队伍占领的资源点数量
            for (int team = 0; team < PVP_TEAMS_COUNT; ++team)
                if (m_Nodes[node] == team + BG_AB_NODE_TYPE_OCCUPIED)
                    ++team_points[team];
        }

        // 累积资源分数
        for (int team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            int points = team_points[team];
            if (!points)
                continue;

            // 累积时间
            m_lastTick[team] += diff;

            // 检查是否到达资源获取时间间隔
            if (m_lastTick[team] > BG_AB_TickIntervals[points])
            {
                // 减去时间间隔
                m_lastTick[team] -= BG_AB_TickIntervals[points];

                // 增加资源分数
                m_TeamScores[team] += BG_AB_TickPoints[points];
                m_HonorScoreTics[team] += BG_AB_TickPoints[points];
                m_ReputationScoreTics[team] += BG_AB_TickPoints[points];

                // 发放声望奖励
                if (m_ReputationScoreTics[team] >= m_ReputationTics)
                {
                    (team == TEAM_ALLIANCE) ? RewardReputationToTeam(509, 10, ALLIANCE) : RewardReputationToTeam(510, 10, HORDE);
                    m_ReputationScoreTics[team] -= m_ReputationTics;
                }

                // 发放荣誉奖励
                if (m_HonorScoreTics[team] >= m_HonorTics)
                {
                    RewardHonorToTeam(GetBonusHonorFromKill(1), (team == TEAM_ALLIANCE) ? ALLIANCE : HORDE);
                    m_HonorScoreTics[team] -= m_HonorTics;
                }

                // 检查是否需要发送即将胜利警告（分数超过1400分）
                if (!m_IsInformedNearVictory && m_TeamScores[team] > BG_AB_WARNING_NEAR_VICTORY_SCORE)
                {
                    if (team == TEAM_ALLIANCE)
                    {
                        SendBroadcastText(BG_AB_TEXT_ALLIANCE_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                        PlaySoundToAll(BG_AB_SOUND_NEAR_VICTORY_ALLIANCE);
                    }
                    else
                    {
                        SendBroadcastText(BG_AB_TEXT_HORDE_NEAR_VICTORY, CHAT_MSG_BG_SYSTEM_NEUTRAL);
                        PlaySoundToAll(BG_AB_SOUND_NEAR_VICTORY_HORDE);
                    }
                    m_IsInformedNearVictory = true;
                }

                // 限制最大分数为1600
                if (m_TeamScores[team] > BG_AB_MAX_TEAM_SCORE)
                    m_TeamScores[team] = BG_AB_MAX_TEAM_SCORE;

                // 更新客户端世界状态
                if (team == TEAM_ALLIANCE)
                    UpdateWorldState(BG_AB_OP_RESOURCES_ALLY, m_TeamScores[team]);
                else
                    UpdateWorldState(BG_AB_OP_RESOURCES_HORDE, m_TeamScores[team]);

                // 更新成就标志：检查是否有一方分数领先500分以上
                uint8 otherTeam = (team + 1) % PVP_TEAMS_COUNT;
                if (m_TeamScores[team] > m_TeamScores[otherTeam] + 500)
                    m_TeamScores500Disadvantage[otherTeam] = true;
            }
        }

        // 检查胜负条件：先达到1600分的队伍获胜
        if (m_TeamScores[TEAM_ALLIANCE] >= BG_AB_MAX_TEAM_SCORE)
            EndBattleground(ALLIANCE);
        else if (m_TeamScores[TEAM_HORDE] >= BG_AB_MAX_TEAM_SCORE)
            EndBattleground(HORDE);
    }
}

/**
 * @brief 开始事件：关闭大门
 *
 * 在战场开始前的准备阶段调用，执行以下操作：
 * 1. 隐藏所有旗帜、光环和增益buff（设为1天后重生）
 * 2. 关闭并生成双方起始大门
 * 3. 生成双方起始基地的灵魂医者
 *
 * 调用时机：战场初始化时，玩家进入准备阶段
 */
void BattlegroundAB::StartingEventCloseDoors()
{
    // 隐藏所有资源点的旗帜和光环（每个资源点8个对象）
    for (int obj = BG_AB_OBJECT_BANNER_NEUTRAL; obj < BG_AB_DYNAMIC_NODES_COUNT * 8; ++obj)
        SpawnBGObject(obj, RESPAWN_ONE_DAY);

    // 隐藏所有增益buff（每个资源点3个buff）
    for (int i = 0; i < BG_AB_DYNAMIC_NODES_COUNT * 3; ++i)
        SpawnBGObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + i, RESPAWN_ONE_DAY);

    // 关闭并生成双方大门
    DoorClose(BG_AB_OBJECT_GATE_A);               // 关闭联盟大门
    DoorClose(BG_AB_OBJECT_GATE_H);               // 关闭部落大门
    SpawnBGObject(BG_AB_OBJECT_GATE_A, RESPAWN_IMMEDIATELY);  // 立即生成联盟大门
    SpawnBGObject(BG_AB_OBJECT_GATE_H, RESPAWN_IMMEDIATELY);  // 立即生成部落大门

    // 生成双方起始基地的灵魂医者
    _NodeOccupied(BG_AB_SPIRIT_ALIANCE, ALLIANCE);  // 联盟基地灵魂医者
    _NodeOccupied(BG_AB_SPIRIT_HORDE, HORDE);       // 部落基地灵魂医者
}

/**
 * @brief 开始事件：打开大门
 *
 * 战场正式开始时调用，执行以下操作：
 * 1. 生成5个资源点的中立旗帜
 * 2. 随机生成各资源点的增益buff（每个资源点1个，随机3种类型）
 * 3. 打开双方大门
 * 4. 启动定时成就计时
 *
 * 调用时机：战场准备时间结束，战斗正式开始
 */
void BattlegroundAB::StartingEventOpenDoors()
{
    // 生成5个资源点的中立旗帜
    for (int banner = BG_AB_OBJECT_BANNER_NEUTRAL, i = 0; i < 5; banner += 8, ++i)
        SpawnBGObject(banner, RESPAWN_IMMEDIATELY);

    // 为每个资源点随机生成一个增益buff（速度、回复、狂暴三种之一）
    for (int i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
    {
        uint8 buff = urand(0, 2);  // 随机选择buff类型
        SpawnBGObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + buff + i * 3, RESPAWN_IMMEDIATELY);
    }

    // 打开双方大门
    DoorOpen(BG_AB_OBJECT_GATE_A);  // 打开联盟大门
    DoorOpen(BG_AB_OBJECT_GATE_H);  // 打开部落大门

    // 启动定时成就：Let's Get This Done（6分钟内获胜）
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, AB_EVENT_START_BATTLE);
}

/**
 * @brief 添加玩家到战场
 * @param player 玩家指针
 *
 * 当玩家进入战场时调用：
 * 1. 调用父类AddPlayer方法添加玩家
 * 2. 如果玩家是新进入战场（不是重新登录），创建玩家分数记录
 *
 * 调用时机：玩家进入战场时
 */
void BattlegroundAB::AddPlayer(Player* player)
{
    // 检查玩家是否已经在战场中（处理断线重连情况）
    bool const isInBattleground = IsPlayerInBattleground(player->GetGUID());

    // 调用父类方法添加玩家
    Battleground::AddPlayer(player);

    // 如果是新进入的玩家，创建分数记录
    if (!isInBattleground)
        PlayerScores[player->GetGUID().GetCounter()] = new BattlegroundABScore(player->GetGUID());
}

/**
 * @brief 移除玩家
 * @param player 玩家指针
 * @param guid 玩家GUID
 * @param team 队伍ID
 *
 * 当玩家离开战场时调用（目前未实现特殊逻辑）
 *
 * 调用时机：玩家离开战场时
 */
void BattlegroundAB::RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/, uint32 /*team*/)
{
}

/**
 * @brief 处理区域触发器
 * @param player 触发玩家
 * @param trigger 触发器ID
 *
 * 处理玩家进入特定区域触发器的事件：
 * - 3948: 联盟离开战场传送门
 * - 3949: 部落离开战场传送门
 * - 其他: 资源点相关触发器（目前未实现特殊处理）
 *
 * 调用时机：玩家进入区域触发器范围时
 */
void BattlegroundAB::HandleAreaTrigger(Player* player, uint32 trigger)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 3948:  // 阿拉希盆地联盟出口
            if (player->GetTeam() != ALLIANCE)
                player->GetSession()->SendAreaTriggerMessage("Only The Alliance can use that portal");
            else
                player->LeaveBattleground();
            break;
        case 3949:  // 阿拉希盆地部落出口
            if (player->GetTeam() != HORDE)
                player->GetSession()->SendAreaTriggerMessage("Only The Horde can use that portal");
            else
                player->LeaveBattleground();
            break;
        case 3866:  // 马厩
        case 3869:  // 金矿
        case 3867:  // 农场
        case 3868:  // 伐木场
        case 3870:  // 铁匠铺
        case 4020:  // 未知1
        case 4021:  // 未知2
        case 4674:  // 未知3
            // 目前未实现特殊处理
            //break;
        default:
            // 其他触发器交给父类处理
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

/**
 * @brief 创建旗帜
 * @param node 资源点索引（0-4）
 * @param type 旗帜类型（0=中立，1=争夺，3=占领）
 * @param teamIndex 队伍索引（0=联盟，1=部落）
 * @param delay 是否延迟生成
 *
 * 在指定资源点生成对应状态的旗帜和光环：
 * - 如果delay为true，将旗帜生成加入延迟队列（2秒后生成）
 * - 如果delay为false，立即生成旗帜
 * - 生成旗帜的同时生成对应的光环效果
 *
 * 调用时机：资源点状态改变时
 */
void BattlegroundAB::_CreateBanner(uint8 node, uint8 type, uint8 teamIndex, bool delay)
{
    // 如果需要延迟生成，加入延迟队列
    if (delay)
    {
        m_BannerTimers[node].timer = 2000;      // 2秒延迟
        m_BannerTimers[node].type = type;
        m_BannerTimers[node].teamIndex = teamIndex;
        return;
    }

    // 计算旗帜对象索引：节点索引*8 + 类型 + 队伍索引
    uint8 obj = node*8 + type + teamIndex;

    // 立即生成旗帜
    SpawnBGObject(obj, RESPAWN_IMMEDIATELY);

    // 如果是中立状态，不生成光环
    if (!type)
        return;

    // 计算光环对象索引
    // 占领状态：节点索引*8 + (5 + 队伍索引)
    // 争夺状态：节点索引*8 + 7
    obj = node * 8 + ((type == BG_AB_NODE_TYPE_OCCUPIED) ? (5 + teamIndex) : 7);
    SpawnBGObject(obj, RESPAWN_IMMEDIATELY);
}

/**
 * @brief 删除旗帜
 * @param node 资源点索引（0-4）
 * @param type 旗帜类型（0=中立，1=争夺，3=占领）
 * @param teamIndex 队伍索引（0=联盟，1=部落）
 *
 * 移除指定资源点的旗帜和光环：
 * - 将旗帜和光环设置为1天后重生（相当于隐藏）
 *
 * 调用时机：资源点状态改变，需要移除旧旗帜时
 */
void BattlegroundAB::_DelBanner(uint8 node, uint8 type, uint8 teamIndex)
{
    // 计算旗帜对象索引
    uint8 obj = node*8 + type + teamIndex;
    SpawnBGObject(obj, RESPAWN_ONE_DAY);  // 设为1天后重生（隐藏）

    // 如果是中立状态，不处理光环
    if (!type)
        return;

    // 计算并移除光环对象
    obj = node * 8 + ((type == BG_AB_NODE_TYPE_OCCUPIED) ? (5 + teamIndex) : 7);
    SpawnBGObject(obj, RESPAWN_ONE_DAY);  // 设为1天后重生（隐藏）
}

/**
 * @brief 填充初始世界状态
 * @param packet 世界状态数据包
 *
 * 初始化客户端的世界状态，包括：
 * 1. 各资源点的图标显示（中立状态显示图标）
 * 2. 各资源点的占领状态（联盟、部落、争夺中）
 * 3. 双方占领的基地数量
 * 4. 双方的资源分数
 * 5. 最大分数和警告阈值
 *
 * 调用时机：玩家进入战场时，发送初始化数据
 */
void BattlegroundAB::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 状态偏移数组（用于计算世界状态ID）
    const uint8 plusArray[] = {0, 2, 3, 0, 1};

    // 发送资源点图标状态（只在资源点为中立时显示）
    for (uint8 node = 0; node < BG_AB_DYNAMIC_NODES_COUNT; ++node)
        packet.Worldstates.emplace_back(BG_AB_OP_NODEICONS[node], (m_Nodes[node] == 0) ? 1 : 0);

    // 发送各资源点的占领状态
    for (uint8 node = 0; node < BG_AB_DYNAMIC_NODES_COUNT; ++node)
        for (uint8 itr = 1; itr < BG_AB_DYNAMIC_NODES_COUNT; ++itr)
            packet.Worldstates.emplace_back(BG_AB_OP_NODESTATES[node] + plusArray[itr], (m_Nodes[node] == itr) ? 1 : 0);

    // 统计双方占领的基地数量
    int32 ally = 0, horde = 0;
    for (uint8 node = 0; node < BG_AB_DYNAMIC_NODES_COUNT; ++node)
        if (m_Nodes[node] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
            ++ally;
        else if (m_Nodes[node] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
            ++horde;

    // 发送双方占领基地数量
    packet.Worldstates.emplace_back(BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    packet.Worldstates.emplace_back(BG_AB_OP_OCCUPIED_BASES_HORDE, horde);

    // 发送队伍分数
    packet.Worldstates.emplace_back(BG_AB_OP_RESOURCES_MAX, BG_AB_MAX_TEAM_SCORE);           // 最大分数：1600
    packet.Worldstates.emplace_back(BG_AB_OP_RESOURCES_WARNING, BG_AB_WARNING_NEAR_VICTORY_SCORE);  // 警告阈值：1400
    packet.Worldstates.emplace_back(BG_AB_OP_RESOURCES_ALLY, m_TeamScores[TEAM_ALLIANCE]);   // 联盟分数
    packet.Worldstates.emplace_back(BG_AB_OP_RESOURCES_HORDE, m_TeamScores[TEAM_HORDE]);     // 部落分数

    // 其他未知状态
    packet.Worldstates.emplace_back(1861, 2);
}

/**
 * @brief 发送节点更新
 * @param node 资源点索引（0-4）
 *
 * 向所有玩家发送资源点状态更新的世界状态消息：
 * 1. 清除旧状态的显示
 * 2. 显示新状态
 * 3. 更新双方占领的基地数量
 *
 * 调用时机：资源点状态改变时
 */
void BattlegroundAB::_SendNodeUpdate(uint8 node)
{
    // 状态偏移数组
    const uint8 plusArray[] = {0, 2, 3, 0, 1};

    // 清除旧状态
    if (m_prevNodes[node])
        UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_prevNodes[node]], 0);
    else
        UpdateWorldState(BG_AB_OP_NODEICONS[node], 0);

    // 显示新状态
    UpdateWorldState(BG_AB_OP_NODESTATES[node] + plusArray[m_Nodes[node]], 1);

    // 统计并更新双方占领的基地数量
    uint8 ally = 0, horde = 0;
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        if (m_Nodes[i] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
            ++ally;
        else if (m_Nodes[i] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
            ++horde;

    // 更新世界状态
    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_ALLY, ally);
    UpdateWorldState(BG_AB_OP_OCCUPIED_BASES_HORDE, horde);
}

/**
 * @brief 节点被占领
 * @param node 资源点索引（0-6，包括5个资源点和2个起始基地）
 * @param team 占领队伍
 *
 * 当资源点被占领时调用，执行以下操作：
 * 1. 生成灵魂医者（允许占领队伍的玩家在此复活）
 * 2. 如果是动态资源点（非起始基地）：
 *    a. 检查任务完成条件（占领4个或5个资源点）
 *    b. 生成荣誉奖励光环（占领队伍的玩家在资源点附近获得额外荣誉）
 *
 * 调用时机：资源点从争夺状态变为占领状态时
 */
void BattlegroundAB::_NodeOccupied(uint8 node, Team team)
{
    // 生成灵魂医者
    if (!AddSpiritGuide(node, BG_AB_SpiritGuidePos[node], GetTeamIndexByTeamId(team)))
        TC_LOG_ERROR("bg.battleground", "Failed to spawn spirit guide! point: {}, team: {}, ", node, team);

    // 只处理动态资源点，不处理起始基地
    if (node >= BG_AB_DYNAMIC_NODES_COUNT)
        return;

    // 统计队伍占领的资源点数量
    uint8 capturedNodes = 0;
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        if (m_Nodes[i] == uint8(GetTeamIndexByTeamId(team)) + BG_AB_NODE_TYPE_OCCUPIED && !m_NodeTimers[i])
            ++capturedNodes;

    // 检查任务完成条件并施放奖励法术
    if (capturedNodes >= 5)
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_5_BASES, team);  // 占领5个资源点任务奖励
    if (capturedNodes >= 4)
        CastSpellOnTeam(SPELL_AB_QUEST_REWARD_4_BASES, team);  // 占领4个资源点任务奖励

    // 获取或生成荣誉奖励光环触发器生物
    // 索引7-11是荣誉光环触发器（0-6是灵魂医者）
    Creature* trigger = !BgCreatures[node + 7] ? GetBGCreature(node + 7) : nullptr;
    if (!trigger)
        trigger = AddCreature(WORLD_TRIGGER, node+7, BG_AB_NodePositions[node], GetTeamIndexByTeamId(team));

    // 生成荣誉奖励光环（占领队伍的玩家在资源点附近获得额外荣誉）
    if (trigger)
    {
        // 设置触发器阵营，确保光环只影响占领队伍的玩家
        trigger->SetFaction(team == ALLIANCE ? FACTION_ALLIANCE_GENERIC : FACTION_HORDE_GENERIC);
        // 施放荣誉防御者光环（25码范围内获得50%额外荣誉）
        trigger->CastSpell(trigger, SPELL_HONORABLE_DEFENDER_25Y, false);
    }
}

/**
 * @brief 节点失去占领
 * @param node 资源点索引（0-4）
 *
 * 当资源点失去占领时调用，执行以下操作：
 * 1. 移除荣誉奖励光环触发器
 * 2. 重新定位在灵魂医者附近的死亡玩家
 * 3. 移除灵魂医者
 *
 * 调用时机：资源点从占领状态变为争夺状态时
 */
void BattlegroundAB::_NodeDeOccupied(uint8 node)
{
    // 只处理动态资源点，不处理起始基地
    if (node >= BG_AB_DYNAMIC_NODES_COUNT)
        return;

    // 移除荣誉奖励光环触发器（索引7-11是荣誉光环触发器）
    DelCreature(node+7);

    // 重新定位在灵魂医者附近的死亡玩家（防止玩家卡在资源点）
    RelocateDeadPlayers(BgCreatures[node]);

    // 移除灵魂医者
    DelCreature(node);

    // 注意：增益buff对象不会被移除
}

/**
 * @brief 玩家点击旗帜事件
 * @param source 点击玩家
 * @param target_obj 旗帜游戏对象（未使用）
 *
 * 当玩家点击旗帜时调用，处理资源点占领逻辑：
 * 1. 查找玩家点击的是哪个资源点
 * 2. 检查玩家是否可以操作该旗帜
 * 3. 根据当前状态处理状态转换：
 *    - 中立状态 -> 争夺状态
 *    - 争夺状态 -> 占领状态（防守）或切换争夺阵营
 *    - 占领状态 -> 争夺状态（敌方突袭）
 * 4. 更新玩家分数
 * 5. 发送系统消息和播放音效
 *
 * 调用时机：玩家右键点击旗帜游戏对象时
 */
void BattlegroundAB::EventPlayerClickedOnFlag(Player* source, GameObject* /*target_obj*/)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 查找玩家点击的是哪个资源点的旗帜
    uint8 node = BG_AB_NODE_STABLES;
    GameObject* obj = GetBgMap()->GetGameObject(BgObjects[node*8+7]);
    while ((node < BG_AB_DYNAMIC_NODES_COUNT) && ((!obj) || (!source->IsWithinDistInMap(obj, 10))))
    {
        ++node;
        obj = GetBgMap()->GetGameObject(BgObjects[node*8+BG_AB_OBJECT_AURA_CONTESTED]);
    }

    // 如果没有找到附近的旗帜，可能玩家在作弊
    if (node == BG_AB_DYNAMIC_NODES_COUNT)
    {
        return;
    }

    // 获取玩家队伍索引
    TeamId teamIndex = GetTeamIndexByTeamId(source->GetTeam());

    // 检查玩家是否可以操作该旗帜
    // 条件：资源点为中立，或者玩家队伍与当前占领队伍不同
    if (!(m_Nodes[node] == 0 || teamIndex == m_Nodes[node]%2))
        return;

    // 移除玩家的PVP战斗中断光环（如进食、喝水等）
    source->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    uint32 sound = 0;

    // 情况1：资源点为中立状态，变为争夺状态
    if (m_Nodes[node] == BG_AB_NODE_TYPE_NEUTRAL)
    {
        // 更新玩家分数（突袭基地）
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);

        // 更新节点状态：中立 -> 争夺
        m_prevNodes[node] = m_Nodes[node];
        m_Nodes[node] = teamIndex + 1;

        // 删除中立旗帜，生成争夺状态旗帜
        _DelBanner(node, BG_AB_NODE_TYPE_NEUTRAL, 0);
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
        _SendNodeUpdate(node);

        // 启动占领计时器（60秒）
        m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        // 发送系统消息
        if (teamIndex == TEAM_ALLIANCE)
            SendBroadcastText(ABNodes[node].TextAllianceClaims, CHAT_MSG_BG_SYSTEM_ALLIANCE, source);
        else
            SendBroadcastText(ABNodes[node].TextHordeClaims, CHAT_MSG_BG_SYSTEM_HORDE, source);

        sound = BG_AB_SOUND_NODE_CLAIMED;
    }
    // 情况2：资源点为争夺状态
    else if ((m_Nodes[node] == BG_AB_NODE_STATUS_ALLY_CONTESTED) || (m_Nodes[node] == BG_AB_NODE_STATUS_HORDE_CONTESTED))
    {
        // 情况2a：前一状态不是占领状态，切换争夺阵营
        if (m_prevNodes[node] < BG_AB_NODE_TYPE_OCCUPIED)
        {
            // 更新玩家分数（突袭基地）
            UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);

            // 更新节点状态：争夺 -> 敌方争夺
            m_prevNodes[node] = m_Nodes[node];
            m_Nodes[node] = uint8(teamIndex) + BG_AB_NODE_TYPE_CONTESTED;

            // 删除旧旗帜，生成新旗帜
            _DelBanner(node, BG_AB_NODE_TYPE_CONTESTED, !teamIndex);
            _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
            _SendNodeUpdate(node);

            // 重置占领计时器
            m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

            // 发送系统消息
            if (teamIndex == TEAM_ALLIANCE)
                SendBroadcastText(ABNodes[node].TextAllianceAssaulted, CHAT_MSG_BG_SYSTEM_ALLIANCE, source);
            else
                SendBroadcastText(ABNodes[node].TextHordeAssaulted, CHAT_MSG_BG_SYSTEM_HORDE, source);
        }
        // 情况2b：前一状态是占领状态，防守成功
        else
        {
            // 更新玩家分数（防守基地）
            UpdatePlayerScore(source, SCORE_BASES_DEFENDED, 1);

            // 更新节点状态：争夺 -> 占领（防守成功）
            m_prevNodes[node] = m_Nodes[node];
            m_Nodes[node] = uint8(teamIndex) + BG_AB_NODE_TYPE_OCCUPIED;

            // 删除争夺旗帜，生成占领旗帜
            _DelBanner(node, BG_AB_NODE_TYPE_CONTESTED, !teamIndex);
            _CreateBanner(node, BG_AB_NODE_TYPE_OCCUPIED, teamIndex, true);
            _SendNodeUpdate(node);

            // 清除占领计时器
            m_NodeTimers[node] = 0;

            // 处理节点被占领事件（生成灵魂医者等）
            _NodeOccupied(node, (teamIndex == TEAM_ALLIANCE) ? ALLIANCE : HORDE);

            // 发送系统消息
            if (teamIndex == TEAM_ALLIANCE)
                SendBroadcastText(ABNodes[node].TextAllianceDefended, CHAT_MSG_BG_SYSTEM_ALLIANCE, source);
            else
                SendBroadcastText(ABNodes[node].TextHordeDefended, CHAT_MSG_BG_SYSTEM_HORDE, source);
        }
        sound = (teamIndex == TEAM_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }
    // 情况3：资源点为占领状态，变为争夺状态（敌方突袭）
    else
    {
        // 更新玩家分数（突袭基地）
        UpdatePlayerScore(source, SCORE_BASES_ASSAULTED, 1);

        // 更新节点状态：占领 -> 争夺
        m_prevNodes[node] = m_Nodes[node];
        m_Nodes[node] = uint8(teamIndex) + BG_AB_NODE_TYPE_CONTESTED;

        // 删除占领旗帜，生成争夺旗帜
        _DelBanner(node, BG_AB_NODE_TYPE_OCCUPIED, !teamIndex);
        _CreateBanner(node, BG_AB_NODE_TYPE_CONTESTED, teamIndex, true);
        _SendNodeUpdate(node);

        // 处理节点失去占领事件（移除灵魂医者等）
        _NodeDeOccupied(node);

        // 启动占领计时器
        m_NodeTimers[node] = BG_AB_FLAG_CAPTURING_TIME;

        // 发送系统消息
        if (teamIndex == TEAM_ALLIANCE)
            SendBroadcastText(ABNodes[node].TextAllianceAssaulted, CHAT_MSG_BG_SYSTEM_ALLIANCE, source);
        else
            SendBroadcastText(ABNodes[node].TextHordeAssaulted, CHAT_MSG_BG_SYSTEM_HORDE, source);

        sound = (teamIndex == TEAM_ALLIANCE) ? BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE : BG_AB_SOUND_NODE_ASSAULTED_HORDE;
    }

    // 如果资源点变为占领状态，发送占领消息
    if (m_Nodes[node] >= BG_AB_NODE_TYPE_OCCUPIED)
    {
        if (teamIndex == TEAM_ALLIANCE)
            SendBroadcastText(ABNodes[node].TextAllianceTaken, CHAT_MSG_BG_SYSTEM_ALLIANCE);
        else
            SendBroadcastText(ABNodes[node].TextHordeTaken, CHAT_MSG_BG_SYSTEM_HORDE);
    }

    // 播放音效给所有玩家
    PlaySoundToAll(sound);
}

/**
 * @brief 获取提前结束时的获胜者
 * @return 获胜队伍ID
 *
 * 当战场因玩家不足而提前结束时，根据占领的资源点数量判定获胜者：
 * 1. 统计双方占领的资源点数量
 * 2. 占领更多资源点的队伍获胜
 * 3. 如果数量相等，使用父类方法判定（基于玩家数量）
 *
 * 调用时机：战场因玩家不足而提前结束时
 */
uint32 BattlegroundAB::GetPrematureWinner()
{
    // 统计双方占领的资源点数量
    uint8 ally = 0, horde = 0;
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        if (m_Nodes[i] == BG_AB_NODE_STATUS_ALLY_OCCUPIED)
            ++ally;
        else if (m_Nodes[i] == BG_AB_NODE_STATUS_HORDE_OCCUPIED)
            ++horde;

    // 根据占领的资源点数量判定获胜者
    if (ally > horde)
        return ALLIANCE;
    else if (horde > ally)
        return HORDE;

    // 如果数量相等，使用父类方法判定（基于玩家数量）
    return Battleground::GetPrematureWinner();
}

/**
 * @brief 设置战场
 * @return 设置成功返回true，否则返回false
 *
 * 生成战场中的所有游戏对象：
 * 1. 为每个资源点生成8个对象（中立旗帜、联盟/部落争夺旗帜、联盟/部落占领旗帜、联盟/部落/争夺光环）
 * 2. 生成双方起始大门
 * 3. 为每个资源点生成3种增益buff（速度、回复、狂暴）
 *
 * 调用时机：战场创建时
 * 性能注意：此函数只在战场创建时调用一次，性能开销较小
 */
bool BattlegroundAB::SetupBattleground()
{
    // 为每个资源点生成旗帜和光环对象
    for (int i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
    {
        // 为每个资源点生成8个对象（中立旗帜、联盟/部落争夺旗帜、联盟/部落占领旗帜、联盟/部落/争夺光环）
        if (!AddObject(BG_AB_OBJECT_BANNER_NEUTRAL + 8*i, BG_AB_OBJECTID_NODE_BANNER_0 + i, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_BANNER_CONT_A + 8*i, BG_AB_OBJECTID_BANNER_CONT_A, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_BANNER_CONT_H + 8*i, BG_AB_OBJECTID_BANNER_CONT_H, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_BANNER_ALLY + 8*i, BG_AB_OBJECTID_BANNER_A, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_BANNER_HORDE + 8*i, BG_AB_OBJECTID_BANNER_H, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_AURA_ALLY + 8*i, BG_AB_OBJECTID_AURA_A, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_AURA_HORDE + 8*i, BG_AB_OBJECTID_AURA_H, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_AURA_CONTESTED + 8*i, BG_AB_OBJECTID_AURA_C, BG_AB_NodePositions[i], 0, 0, std::sin(BG_AB_NodePositions[i].GetOrientation()/2), std::cos(BG_AB_NodePositions[i].GetOrientation()/2), RESPAWN_ONE_DAY))
        {
            TC_LOG_ERROR("sql.sql", "BatteGroundAB: Failed to spawn some object Battleground not created!");
            return false;
        }
    }

    // 生成双方起始大门
    if (!AddObject(BG_AB_OBJECT_GATE_A, BG_AB_OBJECTID_GATE_A, BG_AB_DoorPositions[0][0], BG_AB_DoorPositions[0][1], BG_AB_DoorPositions[0][2], BG_AB_DoorPositions[0][3], BG_AB_DoorPositions[0][4], BG_AB_DoorPositions[0][5], BG_AB_DoorPositions[0][6], BG_AB_DoorPositions[0][7], RESPAWN_IMMEDIATELY)
        || !AddObject(BG_AB_OBJECT_GATE_H, BG_AB_OBJECTID_GATE_H, BG_AB_DoorPositions[1][0], BG_AB_DoorPositions[1][1], BG_AB_DoorPositions[1][2], BG_AB_DoorPositions[1][3], BG_AB_DoorPositions[1][4], BG_AB_DoorPositions[1][5], BG_AB_DoorPositions[1][6], BG_AB_DoorPositions[1][7], RESPAWN_IMMEDIATELY))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundAB: Failed to spawn door object Battleground not created!");
        return false;
    }

    // 为每个资源点生成3种增益buff
    for (int i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
    {
        if (!AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i, Buff_Entries[0], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, std::sin(BG_AB_BuffPositions[i][3]/2), std::cos(BG_AB_BuffPositions[i][3]/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i + 1, Buff_Entries[1], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, std::sin(BG_AB_BuffPositions[i][3]/2), std::cos(BG_AB_BuffPositions[i][3]/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AB_OBJECT_SPEEDBUFF_STABLES + 3 * i + 2, Buff_Entries[2], BG_AB_BuffPositions[i][0], BG_AB_BuffPositions[i][1], BG_AB_BuffPositions[i][2], BG_AB_BuffPositions[i][3], 0, 0, std::sin(BG_AB_BuffPositions[i][3]/2), std::cos(BG_AB_BuffPositions[i][3]/2), RESPAWN_ONE_DAY))
            TC_LOG_ERROR("sql.sql", "BatteGroundAB: Failed to spawn buff object!");
    }

    return true;
}

/**
 * @brief 重置战场
 *
 * 将战场重置到初始状态：
 * 1. 调用父类Reset方法
 * 2. 重置所有分数和计时器
 * 3. 根据是否为战场周末设置荣誉和声望点数间隔
 * 4. 重置所有节点状态
 * 5. 移除所有生物
 *
 * 调用时机：战场结束或重新开始时
 */
void BattlegroundAB::Reset()
{
    // 调用父类的Reset方法
    Battleground::Reset();

    // 重置队伍分数
    m_TeamScores[TEAM_ALLIANCE]          = 0;
    m_TeamScores[TEAM_HORDE]             = 0;
    m_lastTick[TEAM_ALLIANCE]            = 0;
    m_lastTick[TEAM_HORDE]               = 0;
    m_HonorScoreTics[TEAM_ALLIANCE]      = 0;
    m_HonorScoreTics[TEAM_HORDE]         = 0;
    m_ReputationScoreTics[TEAM_ALLIANCE] = 0;
    m_ReputationScoreTics[TEAM_HORDE]    = 0;
    m_IsInformedNearVictory              = false;

    // 根据是否为战场周末设置荣誉和声望点数间隔
    bool isBGWeekend = sBattlegroundMgr->IsBGWeekend(GetTypeID());
    m_HonorTics = (isBGWeekend) ? BG_AB_ABBGWeekendHonorTicks : BG_AB_NotABBGWeekendHonorTicks;
    m_ReputationTics = (isBGWeekend) ? BG_AB_ABBGWeekendReputationTicks : BG_AB_NotABBGWeekendReputationTicks;

    // 重置成就相关标志
    m_TeamScores500Disadvantage[TEAM_ALLIANCE] = false;
    m_TeamScores500Disadvantage[TEAM_HORDE]    = false;

    // 重置所有资源点状态
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
    {
        m_Nodes[i] = 0;
        m_prevNodes[i] = 0;
        m_NodeTimers[i] = 0;
        m_BannerTimers[i].timer = 0;
    }

    // 移除所有生物（灵魂医者和荣誉光环触发器）
    for (uint8 i = 0; i < BG_AB_ALL_NODES_COUNT + 5; ++i)
        if (BgCreatures[i])
            DelCreature(i);
}

/**
 * @brief 结束战场
 * @param winner 获胜队伍ID
 *
 * 战场结束时调用，发放荣誉奖励：
 * 1. 获胜队伍获得额外荣誉奖励
 * 2. 双方都获得完成战场的基础荣誉奖励
 * 3. 调用父类EndBattleground方法
 *
 * 调用时机：一方达到1600分或对方玩家全部离开时
 */
void BattlegroundAB::EndBattleground(uint32 winner)
{
    // 获胜队伍获得额外荣誉奖励
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);

    // 双方都获得完成战场的基础荣誉奖励
    RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);
    RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);

    // 调用父类方法完成战场结束流程
    Battleground::EndBattleground(winner);
}

/**
 * @brief 获取最近的墓地
 * @param player 玩家指针
 * @return 墓地位置信息
 *
 * 根据玩家位置和阵营占领的资源点，返回最近的墓地位置：
 * 1. 查找玩家阵营占领的所有资源点
 * 2. 计算玩家到各墓地的距离
 * 3. 返回最近的墓地位置
 * 4. 如果没有占领任何资源点，返回起始基地墓地
 *
 * 调用时机：玩家死亡释放灵魂时
 * 性能注意：此函数使用简单的距离计算，性能开销较小
 */
WorldSafeLocsEntry const* BattlegroundAB::GetClosestGraveyard(Player* player)
{
    TeamId teamIndex = GetTeamIndexByTeamId(player->GetTeam());

    // 查找玩家阵营占领的所有资源点
    std::vector<uint8> nodes;
    for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        if (m_Nodes[i] == teamIndex + 3)  // teamIndex + 3 = 占领状态
            nodes.push_back(i);

    WorldSafeLocsEntry const* good_entry = nullptr;

    // 如果占领了资源点，选择最近的墓地
    if (!nodes.empty())
    {
        float plr_x = player->GetPositionX();
        float plr_y = player->GetPositionY();

        float mindist = 999999.0f;
        for (uint8 i = 0; i < nodes.size(); ++i)
        {
            WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[nodes[i]]);
            if (!entry)
                continue;

            // 计算距离的平方（避免开方运算，提高性能）
            float dist = (entry->Loc.X - plr_x)*(entry->Loc.X - plr_x)+(entry->Loc.Y - plr_y)*(entry->Loc.Y - plr_y);
            if (mindist > dist)
            {
                mindist = dist;
                good_entry = entry;
            }
        }
        nodes.clear();
    }

    // 如果没有占领任何资源点，返回起始基地墓地
    if (!good_entry)
        good_entry = sWorldSafeLocsStore.LookupEntry(BG_AB_GraveyardIds[teamIndex+5]);

    return good_entry;
}

/**
 * @brief 更新玩家分数
 * @param player 玩家指针
 * @param type 分数类型
 * @param value 增加的值
 * @param doAddHonor 是否添加荣誉
 * @return 更新成功返回true，否则返回false
 *
 * 更新玩家分数并触发相关成就：
 * - SCORE_BASES_ASSAULTED: 突袭基地次数
 * - SCORE_BASES_DEFENDED: 防守基地次数
 *
 * 调用时机：玩家突袭或防守基地时
 */
bool BattlegroundAB::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    // 调用父类方法更新基础分数
    if (!Battleground::UpdatePlayerScore(player, type, value, doAddHonor))
        return false;

    // 根据分数类型触发相关成就
    switch (type)
    {
        case SCORE_BASES_ASSAULTED:
            // 突袭基地成就
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AB_OBJECTIVE_ASSAULT_BASE);
            break;
        case SCORE_BASES_DEFENDED:
            // 防守基地成就
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AB_OBJECTIVE_DEFEND_BASE);
            break;
        default:
            break;
    }
    return true;
}

/**
 * @brief 检查某队是否控制所有资源点
 * @param team 队伍ID
 * @return 如果控制所有资源点返回true，否则返回false
 *
 * 用于成就判定：检查指定队伍是否控制所有5个资源点
 *
 * 调用时机：成就系统检查时
 */
bool BattlegroundAB::IsAllNodesControlledByTeam(uint32 team) const
{
    uint32 count = 0;
    for (int i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        if ((team == ALLIANCE && m_Nodes[i] == BG_AB_NODE_STATUS_ALLY_OCCUPIED) ||
            (team == HORDE    && m_Nodes[i] == BG_AB_NODE_STATUS_HORDE_OCCUPIED))
            ++count;

    return count == BG_AB_DYNAMIC_NODES_COUNT;
}

/**
 * @brief 检查成就条件是否满足
 * @param criteriaId 成就条件ID
 * @param player 玩家指针
 * @param target 目标单位
 * @param miscvalue 杂项值
 * @return 满足条件返回true，否则返回false
 *
 * 检查特定成就条件是否满足：
 * - BG_CRITERIA_CHECK_RESILIENT_VICTORY: 逆境求胜（在分数落后500分以上时获胜）
 *
 * 调用时机：成就系统检查时
 */
bool BattlegroundAB::CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* player, Unit const* target, uint32 miscvalue)
{
    switch (criteriaId)
    {
        case BG_CRITERIA_CHECK_RESILIENT_VICTORY:
            // 检查玩家队伍是否曾落后500分以上
            return m_TeamScores500Disadvantage[GetTeamIndexByTeamId(player->GetTeam())];
    }

    // 其他条件交给父类处理
    return Battleground::CheckAchievementCriteriaMeet(criteriaId, player, target, miscvalue);
}
