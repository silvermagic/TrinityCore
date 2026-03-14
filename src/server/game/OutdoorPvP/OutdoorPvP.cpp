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
 * @file OutdoorPvP.cpp
 * @brief 户外PvP系统核心实现文件
 *
 * 本文件实现了户外PvP系统的核心功能，包括：
 * - 争夺点(OPvPCapturePoint)的占领机制
 * - 玩家进入/离开区域的检测
 * - 阵营控制状态的转换逻辑
 * - 世界状态同步和UI更新
 * - 击杀奖励分发
 *
 * 核心算法：
 * 1. 占领进度计算：基于区域内双方玩家数量差异动态调整占领速度
 * 2. 状态机转换：管理争夺点的7种状态及其转换条件
 * 3. 玩家检测：使用Grid系统高效检测区域内的玩家
 *
 * 性能优化：
 * - 使用定时器控制更新频率（OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL）
 * - 只在状态改变时发送世界状态更新
 * - 使用空间分割（Grid）进行玩家范围查询
 */

#include "OutdoorPvP.h"
#include "CellImpl.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "WorldPacket.h"

/**
 * @class DefenseMessageBuilder
 * @brief 防御消息构建器
 *
 * 用于构建防御消息数据包，支持多语言本地化
 * 当玩家在户外PvP区域进行防御作战时，使用此类构建广播消息
 */
class DefenseMessageBuilder
{
    public:
        /**
         * @brief 构造函数
         * @param zoneId 区域ID
         * @param id 广播文本ID
         */
        DefenseMessageBuilder(uint32 zoneId, uint32 id)
            : _zoneId(zoneId), _id(id) { }

        /**
         * @brief 构建防御消息数据包
         * @param data 输出的数据包
         * @param locale 语言区域设置
         *
         * 数据包格式：
         * - uint32 zoneId: 区域ID
         * - uint32 textLen: 文本长度
         * - string text: 本地化文本
         */
        void operator()(WorldPacket& data, LocaleConstant locale) const
        {
            std::string text = sOutdoorPvPMgr->GetDefenseMessage(_zoneId, _id, locale);

            data.Initialize(SMSG_DEFENSE_MESSAGE, 4 + 4 + text.length());
            data.append<uint32>(_zoneId);
            data.append<uint32>(text.length());
            data << text;
        }

    private:
        uint32 _zoneId; ///< 区域ID
        uint32 _id;     ///< 广播文本ID（对应broadcast_text表）
};

/**
 * @brief OPvPCapturePoint构造函数
 * @param pvp 所属的OutdoorPvP实例
 *
 * 初始化所有成员变量为默认值
 */
OPvPCapturePoint::OPvPCapturePoint(OutdoorPvP* pvp):
    m_capturePointSpawnId(), m_capturePoint(nullptr), m_maxValue(0.0f), m_minValue(0.0f), m_maxSpeed(0),
    m_value(0), m_team(TEAM_NEUTRAL), m_OldState(OBJECTIVESTATE_NEUTRAL),
    m_State(OBJECTIVESTATE_NEUTRAL), m_neutralValuePct(0), m_PvP(pvp)
{ }

/**
 * @brief 处理玩家进入争夺区域
 * @param player 进入的玩家指针
 * @return 是否成功添加到活跃玩家列表（如果玩家已在列表中则返回false）
 *
 * 执行操作：
 * 1. 发送世界状态更新给玩家，显示占领进度条UI
 *    - worldState1: 进度条显示/隐藏（1=显示）
 *    - worldState2: 当前进度百分比（0-100）
 *    - worldState3: 中性区域百分比位置
 * 2. 将玩家GUID添加到对应阵营的活跃玩家列表
 */
bool OPvPCapturePoint::HandlePlayerEnter(Player* player)
{
    if (m_capturePoint)
    {
        // 发送进度条显示状态
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldState1, 1);
        // 计算并发送当前占领进度百分比
        // 进度值范围: [-m_maxValue, m_maxValue]，映射到 [0, 100]
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldstate2, (uint32)ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f));
        // 发送中性区域标记位置
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
    }
    return m_activePlayers[player->GetTeamId()].insert(player->GetGUID()).second;
}

/**
 * @brief 处理玩家离开争夺区域
 * @param player 离开的玩家指针
 *
 * 执行操作：
 * 1. 发送世界状态更新，隐藏占领进度条UI
 * 2. 从活跃玩家列表中移除玩家
 */
void OPvPCapturePoint::HandlePlayerLeave(Player* player)
{
    if (m_capturePoint)
        player->SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldState1, 0);  // 隐藏进度条
    m_activePlayers[player->GetTeamId()].erase(player->GetGUID());
}

/**
 * @brief 发送进度条阶段改变通知
 *
 * 当占领进度值改变时调用，向所有在场玩家发送进度更新
 *
 * 注意：这里发送了三个世界状态更新以解决客户端UI偶尔消失或重置的问题
 */
void OPvPCapturePoint::SendChangePhase()
{
    if (!m_capturePoint)
        return;

    // 发送进度条显示状态（解决偶尔消失的问题）
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldState1, 1);
    // 发送当前占领进度百分比给所有在场玩家
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldstate2, (uint32)ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f));
    // 发送中性区域标记位置（解决偶尔重置的问题）
    SendUpdateWorldState(m_capturePoint->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
}

/**
 * @brief 添加游戏对象到管理列表
 * @param type 对象类型标识符（由脚本定义）
 * @param guid 对象的spawnId（低GUID）
 * @param entry 对象模板ID（可选，为0时自动从数据库查询）
 *
 * 将游戏对象添加到争夺点的管理列表，建立双向映射：
 * - m_Objects: type -> spawnId
 * - m_ObjectTypes: spawnId -> type
 */
void OPvPCapturePoint::AddGO(uint32 type, ObjectGuid::LowType guid, uint32 entry)
{
    if (!entry)
    {
        // 如果未提供entry，从数据库查询游戏对象数据
        GameObjectData const* data = sObjectMgr->GetGameObjectData(guid);
        if (!data)
            return;
        entry = data->id;
    }

    m_Objects[type] = guid;
    m_ObjectTypes[m_Objects[type]] = type;
}

/**
 * @brief 添加生物到管理列表
 * @param type 生物类型标识符（由脚本定义）
 * @param guid 生物的spawnId（低GUID）
 * @param entry 生物模板ID（可选，为0时自动从数据库查询）
 *
 * 将生物添加到争夺点的管理列表，建立双向映射：
 * - m_Creatures: type -> spawnId
 * - m_CreatureTypes: spawnId -> type
 */
void OPvPCapturePoint::AddCre(uint32 type, ObjectGuid::LowType guid, uint32 entry)
{
    if (!entry)
    {
        // 如果未提供entry，从数据库查询生物数据
        CreatureData const* data = sObjectMgr->GetCreatureData(guid);
        if (!data)
            return;
        entry = data->id;
    }

    m_Creatures[type] = guid;
    m_CreatureTypes[m_Creatures[type]] = type;
}

/**
 * @brief 在世界中创建并添加游戏对象
 * @param type 对象类型标识符
 * @param entry 对象模板ID
 * @param map 地图ID
 * @param pos 位置坐标
 * @param rot 旋转四元数
 * @return 是否成功创建
 *
 * 向对象管理器注册新的游戏对象数据，并添加到争夺点管理列表
 */
bool OPvPCapturePoint::AddObject(uint32 type, uint32 entry, uint32 map, Position const& pos, QuaternionData const& rot)
{
    if (ObjectGuid::LowType guid = sObjectMgr->AddGameObjectData(entry, map, pos, rot, 0))
    {
        AddGO(type, guid, entry);
        return true;
    }

    return false;
}

/**
 * @brief 在世界中创建并添加生物
 * @param type 生物类型标识符
 * @param entry 生物模板ID
 * @param map 地图ID
 * @param pos 位置坐标
 * @param teamId 阵营ID（默认中立）
 * @param spawntimedelay 生成延迟时间（毫秒）
 * @return 是否成功创建
 *
 * 向对象管理器注册新的生物数据，并添加到争夺点管理列表
 */
bool OPvPCapturePoint::AddCreature(uint32 type, uint32 entry, uint32 map, Position const& pos, TeamId /*teamId = TEAM_NEUTRAL*/, uint32 spawntimedelay /*= 0*/)
{
    if (ObjectGuid::LowType guid = sObjectMgr->AddCreatureData(entry, map, pos, spawntimedelay))
    {
        AddCre(type, guid, entry);
        return true;
    }

    return false;
}

/**
 * @brief 设置争夺点的核心数据
 * @param entry 争夺点游戏对象模板ID
 * @param map 地图ID
 * @param pos 位置坐标
 * @param rot 旋转四元数
 * @return 是否设置成功
 *
 * 从游戏对象模板读取争夺点参数并初始化：
 * - m_maxValue: 完全占领所需进度（capturePoint.maxTime）
 * - m_maxSpeed: 最大占领速度 = m_maxValue / minTime
 * - m_neutralValuePct: 中性区域百分比（进度条中心区域大小）
 * - m_minValue: 中性区域边界值 = m_maxValue * neutralPercent%
 *
 * 要求：游戏对象类型必须为 GAMEOBJECT_TYPE_CAPTURE_POINT
 */
bool OPvPCapturePoint::SetCapturePointData(uint32 entry, uint32 map, Position const& pos, QuaternionData const& rot)
{
    TC_LOG_DEBUG("outdoorpvp", "Creating capture point {}", entry);

    // 检查游戏对象模板是否存在且类型正确
    GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(entry);
    if (!goinfo || goinfo->type != GAMEOBJECT_TYPE_CAPTURE_POINT)
    {
        TC_LOG_ERROR("outdoorpvp", "OutdoorPvP: GO {} is not capture point!", entry);
        return false;
    }

    // 向对象管理器注册争夺点游戏对象
    m_capturePointSpawnId = sObjectMgr->AddGameObjectData(entry, map, pos, rot, 0);

    if (m_capturePointSpawnId == 0)
        return false;

    // 从游戏对象模板读取占领参数
    m_maxValue = (float)goinfo->capturePoint.maxTime;  // 最大进度值
    // 计算最大占领速度（如果未设置minTime则默认60秒）
    m_maxSpeed = m_maxValue / (goinfo->capturePoint.minTime ? goinfo->capturePoint.minTime : 60);
    m_neutralValuePct = goinfo->capturePoint.neutralPercent;  // 中性区域百分比
    // 计算中性区域边界值
    m_minValue = CalculatePct(m_maxValue, m_neutralValuePct);

    return true;
}

/**
 * @brief 删除生物
 * @param type 生物类型标识符
 * @return 是否成功删除
 *
 * 从数据库和世界中删除指定类型的生物
 */
bool OPvPCapturePoint::DelCreature(uint32 type)
{
    uint32 spawnId = m_Creatures[type];
    if (!spawnId)
    {
        TC_LOG_DEBUG("outdoorpvp", "opvp creature type {} was already deleted", type);
        return false;
    }
    TC_LOG_DEBUG("outdoorpvp", "deleting opvp creature type {}", type);
    // 清理映射关系
    m_CreatureTypes[m_Creatures[type]] = 0;
    m_Creatures[type] = 0;

    return Creature::DeleteFromDB(spawnId);
}

/**
 * @brief 删除游戏对象
 * @param type 对象类型标识符
 * @return 是否成功删除
 *
 * 从数据库和世界中删除指定类型的游戏对象
 */
bool OPvPCapturePoint::DelObject(uint32 type)
{
    uint32 spawnId = m_Objects[type];
    if (!spawnId)
        return false;

    // 清理映射关系
    m_ObjectTypes[m_Objects[type]] = 0;
    m_Objects[type] = 0;

    return GameObject::DeleteFromDB(spawnId);
}

/**
 * @brief 删除争夺点游戏对象
 * @return 是否成功删除
 *
 * 清理争夺点对象数据和实例
 */
bool OPvPCapturePoint::DelCapturePoint()
{
    // 从对象管理器删除数据
    sObjectMgr->DeleteGameObjectData(m_capturePointSpawnId);
    m_capturePointSpawnId = 0;

    // 如果游戏对象实例存在，删除它
    if (m_capturePoint)
    {
        m_capturePoint->SetRespawnTime(0);  // 不保存重生时间
        m_capturePoint->Delete();
    }

    return true;
}

/**
 * @brief 删除所有生成的对象和生物
 *
 * 清理争夺点创建的所有游戏对象、生物和争夺点本身
 */
void OPvPCapturePoint::DeleteSpawns()
{
    // 删除所有游戏对象
    for (std::map<uint32, ObjectGuid::LowType>::iterator i = m_Objects.begin(); i != m_Objects.end(); ++i)
        DelObject(i->first);
    // 删除所有生物
    for (std::map<uint32, ObjectGuid::LowType>::iterator i = m_Creatures.begin(); i != m_Creatures.end(); ++i)
        DelCreature(i->first);
    // 删除争夺点
    DelCapturePoint();
}

/**
 * @brief 删除所有生成的游戏对象和生物
 *
 * 清理户外PvP区域的所有资源：
 * 1. 清除游戏对象的脚本绑定
 * 2. 清除生物的脚本绑定
 * 3. 删除所有争夺点及其关联对象
 */
void OutdoorPvP::DeleteSpawns()
{
    // 移除所有游戏对象的脚本绑定
    for (auto itr = m_GoScriptStore.begin(); itr != m_GoScriptStore.end(); ++itr)
    {
        if (GameObject* go = itr->second)
            go->ClearZoneScript();
    }
    m_GoScriptStore.clear();

    // 移除所有生物的脚本绑定
    for (auto itr = m_CreatureScriptStore.begin(); itr != m_CreatureScriptStore.end(); ++itr)
    {
        if (Creature* creature = itr->second)
            creature->ClearZoneScript();
    }
    m_CreatureScriptStore.clear();

    // 删除所有争夺点及其关联对象
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
    {
        itr->second->DeleteSpawns();
        delete itr->second;
    }
    m_capturePoints.clear();
}

/**
 * @brief OutdoorPvP构造函数
 *
 * 初始化成员变量为默认值
 */
OutdoorPvP::OutdoorPvP() : m_TypeId(0), m_sendUpdate(true), m_map(nullptr) { }

/**
 * @brief OutdoorPvP析构函数
 *
 * 自动调用DeleteSpawns()清理所有资源
 */
OutdoorPvP::~OutdoorPvP()
{
    DeleteSpawns();
}

/**
 * @brief 处理玩家进入区域
 * @param player 进入的玩家
 * @param zone 区域ID（未使用）
 *
 * 将玩家添加到对应阵营的玩家列表
 */
void OutdoorPvP::HandlePlayerEnterZone(Player* player, uint32 /*zone*/)
{
    m_players[player->GetTeamId()].insert(player->GetGUID());
}

/**
 * @brief 处理玩家离开区域
 * @param player 离开的玩家
 * @param zone 区域ID（未使用）
 *
 * 执行操作：
 * 1. 从所有争夺点移除玩家
 * 2. 清理玩家的世界状态UI（如果不是登出）
 * 3. 从玩家列表移除
 */
void OutdoorPvP::HandlePlayerLeaveZone(Player* player, uint32 /*zone*/)
{
    // 通知所有争夺点该玩家离开
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->HandlePlayerLeave(player);
    // 移除玩家的世界状态信息（非登出情况下）
    // 因为无法保持所有人同步，所以只更新在区域内的玩家
    if (!player->GetSession()->PlayerLogout())
        SendRemoveWorldStates(player);
    m_players[player->GetTeamId()].erase(player->GetGUID());
    TC_LOG_DEBUG("outdoorpvp", "Player {} left an outdoorpvp zone", player->GetName());
}

/**
 * @brief 处理玩家复活
 * @param player 复活的玩家（未使用）
 * @param zone 区域ID（未使用）
 *
 * 默认空实现，派生类可重写以实现特定逻辑
 */
void OutdoorPvP::HandlePlayerResurrects(Player* /*player*/, uint32 /*zone*/) { }

/**
 * @brief 更新所有争夺点
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return 是否有任何争夺点状态改变
 *
 * 遍历所有争夺点并调用其Update方法
 */
bool OutdoorPvP::Update(uint32 diff)
{
    bool objective_changed = false;
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
    {
        if (itr->second->Update(diff))
            objective_changed = true;
    }
    return objective_changed;
}

/**
 * @brief 更新争夺点状态（核心逻辑）
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return 状态是否发生改变
 *
 * 这是户外PvP系统的核心算法，负责：
 * 1. 检测并更新区域内的活跃玩家列表
 * 2. 根据双方人数计算占领进度变化
 * 3. 更新争夺点状态
 *
 * 算法流程：
 * === 第一步：更新活跃玩家列表 ===
 * - 检查现有活跃玩家是否仍在范围内
 * - 检测范围内的新玩家并添加
 *
 * === 第二步：计算占领进度变化 ===
 * - fact_diff = (联盟人数 - 部落人数) * diff / 更新间隔
 * - 正值表示联盟优势，负值表示部落优势
 * - 限制最大变化速度为 m_maxSpeed * diff
 *
 * === 第三步：更新状态机 ===
 * 根据进度值(m_value)判断状态：
 * - m_value < -m_minValue: 部落控制
 * - m_value > m_minValue: 联盟控制
 * - -m_minValue <= m_value <= m_minValue: 中性区域
 *
 * 状态转换规则：
 * - 中性 -> 阵营控制: 玩家占领到一方
 * - 阵营控制 -> 中性: 对方玩家将进度推回中性区域
 * - 阵营控制 -> 阵营控制: 对方玩家直接争夺
 *
 * 性能注意事项：
 * - 使用Grid系统进行玩家范围查询，复杂度O(n)，n为附近玩家数
 * - 更新频率由OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL控制（默认1秒）
 */
bool OPvPCapturePoint::Update(uint32 diff)
{
    if (!m_capturePoint)
        return false;

    float radius = (float)m_capturePoint->GetGOInfo()->capturePoint.radius;

    // === 第一步：检查现有活跃玩家是否仍在范围内 ===
    for (uint32 team = 0; team < 2; ++team)
    {
        for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end();)
        {
            ObjectGuid playerGuid = *itr;
            ++itr;

            if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
                // 如果玩家超出范围或不再活跃，则移除
                if (!m_capturePoint->IsWithinDistInMap(player, radius) || !player->IsOutdoorPvPActive())
                    HandlePlayerLeave(player);
        }
    }

    // === 第二步：检测范围内的新玩家 ===
    std::list<Player*> players;
    Trinity::AnyPlayerInObjectRangeCheck checker(m_capturePoint, radius);
    Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(m_capturePoint, players, checker);
    Cell::VisitWorldObjects(m_capturePoint, searcher, radius);

    for (std::list<Player*>::iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* const player = *itr;
        if (player->IsOutdoorPvPActive())
        {
            // 如果玩家不在活跃列表中，则添加并触发进入事件
            if (m_activePlayers[player->GetTeamId()].insert(player->GetGUID()).second)
                HandlePlayerEnter(*itr);
        }
    }

    // === 第三步：计算占领进度变化 ===
    // 计算双方人数差值，乘以时间因子
    float fact_diff = ((float)m_activePlayers[0].size() - (float)m_activePlayers[1].size()) * diff / OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL;
    if (!fact_diff)
        return false;

    uint32 Challenger = 0;
    float maxDiff = m_maxSpeed * diff;  // 本帧允许的最大进度变化

    if (fact_diff < 0)
    {
        // 部落占多数
        // 如果已经是部落控制且进度已满，则不继续变化
        if (m_State == OBJECTIVESTATE_HORDE && m_value <= -m_maxValue)
            return false;

        // 限制最大变化速度
        if (fact_diff < -maxDiff)
            fact_diff = -maxDiff;

        Challenger = HORDE;
    }
    else
    {
        // 联盟占多数
        // 如果已经是联盟控制且进度已满，则不继续变化
        if (m_State == OBJECTIVESTATE_ALLIANCE && m_value >= m_maxValue)
            return false;

        // 限制最大变化速度
        if (fact_diff > maxDiff)
            fact_diff = maxDiff;

        Challenger = ALLIANCE;
    }

    float oldValue = m_value;
    TeamId oldTeam = m_team;

    m_OldState = m_State;

    // 更新进度值
    m_value += fact_diff;

    // === 第四步：根据进度值判断状态 ===
    if (m_value < -m_minValue) // 部落控制区域
    {
        if (m_value < -m_maxValue)
            m_value = -m_maxValue;  // 限制最大值
        m_State = OBJECTIVESTATE_HORDE;
        m_team = TEAM_HORDE;
    }
    else if (m_value > m_minValue) // 联盟控制区域
    {
        if (m_value > m_maxValue)
            m_value = m_maxValue;  // 限制最大值
        m_State = OBJECTIVESTATE_ALLIANCE;
        m_team = TEAM_ALLIANCE;
    }
    else if (oldValue * m_value <= 0) // 进入中性区域（经过中点）
    {
        // 根据挑战者阵营决定状态
        if (Challenger == ALLIANCE)
            m_State = OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE;     // 中立->联盟争夺中
        else if (Challenger == HORDE)
            m_State = OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE;        // 中立->部落争夺中
        m_team = TEAM_NEUTRAL;
    }
    else // 在中性区域内移动（未经过中点）
    {
        // 根据挑战者和旧状态决定状态
        if (Challenger == ALLIANCE && (m_OldState == OBJECTIVESTATE_HORDE || m_OldState == OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE))
            m_State = OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;       // 部落->联盟争夺中
        else if (Challenger == HORDE && (m_OldState == OBJECTIVESTATE_ALLIANCE || m_OldState == OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE))
            m_State = OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE;       // 联盟->部落争夺中
        m_team = TEAM_NEUTRAL;
    }

    // 如果进度值改变，发送UI更新
    if (m_value != oldValue)
        SendChangePhase();

    // 如果状态改变，触发事件
    if (m_OldState != m_State)
    {
        if (oldTeam != m_team)
            ChangeTeam(oldTeam);
        ChangeState();
        return true;
    }

    return false;
}

/**
 * @brief 向区域内所有玩家发送世界状态更新
 * @param field 世界状态字段ID
 * @param value 新值
 *
 * 如果启用了更新标志，遍历两个阵营的玩家列表发送更新
 */
void OutdoorPvP::SendUpdateWorldState(uint32 field, uint32 value)
{
    if (m_sendUpdate)
        for (int i = 0; i < 2; ++i)
            for (GuidSet::iterator itr = m_players[i].begin(); itr != m_players[i].end(); ++itr)
                if (Player* const player = ObjectAccessor::FindPlayer(*itr))
                    player->SendUpdateWorldState(field, value);
}

/**
 * @brief 向争夺点所有在场玩家发送世界状态更新
 * @param field 世界状态字段ID
 * @param value 新值
 *
 * 遍历两个阵营的活跃玩家列表发送更新
 */
void OPvPCapturePoint::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint32 team = 0; team < 2; ++team)
    {
        // 发送给区域内的所有玩家
        for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
            if (Player* const player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
    }
}

/**
 * @brief 发送目标完成通知（击杀奖励）
 * @param id 怪物凭证ID
 * @param guid 对象GUID
 *
 * 向控制阵营的在场玩家发送击杀奖励凭证
 * 只有在联盟或部落控制状态下才发送
 */
void OPvPCapturePoint::SendObjectiveComplete(uint32 id, ObjectGuid guid)
{
    uint32 team;
    switch (m_State)
    {
    case OBJECTIVESTATE_ALLIANCE:
        team = 0;  // 联盟
        break;
    case OBJECTIVESTATE_HORDE:
        team = 1;  // 部落
        break;
    default:
        return;  // 中性状态不发送
    }

    // 发送给控制阵营的所有在场玩家
    for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
        if (Player* const player = ObjectAccessor::FindPlayer(*itr))
            player->KilledMonsterCredit(id, guid);
}

/**
 * @brief 处理击杀事件
 * @param killer 击杀者
 * @param killed 被击杀单位
 *
 * 处理玩家或NPC的击杀事件，向小队成员分发奖励
 *
 * 逻辑：
 * - 如果击杀者在小队中，遍历所有小队成员
 * - 只奖励在奖励距离内的成员
 * - NPC击杀：所有成员都可获得奖励（即使不在争夺点内）
 * - 玩家击杀：只有在争夺点内且PvP活跃的玩家才能获得奖励
 */
void OutdoorPvP::HandleKill(Player* killer, Unit* killed)
{
    if (Group* group = killer->GetGroup())
    {
        // 击杀者在小队中，遍历所有小队成员
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* groupGuy = itr->GetSource();

            if (!groupGuy)
                continue;

            // skip if too far away
            if (!groupGuy->IsAtGroupRewardDistance(killed))
                continue;

            // creature kills must be notified, even if not inside objective / not outdoor pvp active
            // player kills only count if active and inside objective
            if ((groupGuy->IsOutdoorPvPActive() && IsInsideObjective(groupGuy)) || killed->GetTypeId() == TYPEID_UNIT)
                HandleKillImpl(groupGuy, killed);
        }
    }
    else
    {
        // creature kills must be notified, even if not inside objective / not outdoor pvp active
        if ((killer->IsOutdoorPvPActive() && IsInsideObjective(killer)) || killed->GetTypeId() == TYPEID_UNIT)
            HandleKillImpl(killer, killed);
    }
}

bool OutdoorPvP::IsInsideObjective(Player* player) const
{
    for (OPvPCapturePointMap::const_iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->IsInsideObjective(player))
            return true;

    return false;
}

bool OPvPCapturePoint::IsInsideObjective(Player* player) const
{
    GuidSet const& plSet = m_activePlayers[player->GetTeamId()];
    return plSet.find(player->GetGUID()) != plSet.end();
}

bool OutdoorPvP::HandleCustomSpell(Player* player, uint32 spellId, GameObject* go)
{
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->HandleCustomSpell(player, spellId, go))
            return true;

    return false;
}

bool OPvPCapturePoint::HandleCustomSpell(Player* player, uint32 /*spellId*/, GameObject* /*go*/)
{
    if (!player->IsOutdoorPvPActive())
        return false;
    return true;
}

bool OutdoorPvP::HandleOpenGo(Player* player, GameObject* go)
{
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->HandleOpenGo(player, go) >= 0)
            return true;

    return false;
}

bool OutdoorPvP::HandleGossipOption(Player* player, Creature* creature, uint32 id)
{
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->HandleGossipOption(player, creature, id))
            return true;

    return false;
}

bool OutdoorPvP::CanTalkTo(Player* player, Creature* c, GossipMenuItems const& gso)
{
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->CanTalkTo(player, c, gso))
            return true;

    return false;
}

bool OutdoorPvP::HandleDropFlag(Player* player, uint32 id)
{
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        if (itr->second->HandleDropFlag(player, id))
            return true;

    return false;
}

bool OPvPCapturePoint::HandleGossipOption(Player* /*player*/, Creature* /*guid*/, uint32 /*id*/)
{
    return false;
}

bool OPvPCapturePoint::CanTalkTo(Player* /*player*/, Creature* /*c*/, GossipMenuItems const& /*gso*/)
{
    return false;
}

bool OPvPCapturePoint::HandleDropFlag(Player* /*player*/, uint32 /*id*/)
{
    return false;
}

int32 OPvPCapturePoint::HandleOpenGo(Player* /*player*/, GameObject* go)
{
    std::map<ObjectGuid::LowType, uint32>::iterator itr = m_ObjectTypes.find(go->GetSpawnId());
    if (itr != m_ObjectTypes.end())
        return itr->second;

    return -1;
}

bool OutdoorPvP::HandleAreaTrigger(Player* /*player*/, uint32 /*trigger*/)
{
    return false;
}

void OutdoorPvP::BroadcastPacket(WorldPacket &data) const
{
    // This is faster than sWorld->SendZoneMessage
    for (uint32 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidSet::const_iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* const player = ObjectAccessor::FindPlayer(*itr))
                player->SendDirectMessage(&data);
}

void OutdoorPvP::RegisterZone(uint32 zoneId)
{
    sOutdoorPvPMgr->AddZone(zoneId, this);
}

bool OutdoorPvP::HasPlayer(Player const* player) const
{
    GuidSet const& plSet = m_players[player->GetTeamId()];
    return plSet.find(player->GetGUID()) != plSet.end();
}

void OutdoorPvP::TeamCastSpell(TeamId team, int32 spellId)
{
    if (spellId > 0)
    {
        for (GuidSet::iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* const player = ObjectAccessor::FindPlayer(*itr))
                player->CastSpell(player, (uint32)spellId, true);
    }
    else
    {
        for (GuidSet::iterator itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* const player = ObjectAccessor::FindPlayer(*itr))
                player->RemoveAura((uint32)-spellId); // by stack?
    }
}

void OutdoorPvP::TeamApplyBuff(TeamId team, uint32 spellId, uint32 spellId2)
{
    TeamCastSpell(team, spellId);
    TeamCastSpell(OTHER_TEAM(team), spellId2 ? -(int32)spellId2 : -(int32)spellId);
}

void OutdoorPvP::OnGameObjectCreate(GameObject* go)
{
    GoScriptPair sp(go->GetGUID().GetCounter(), go);
    m_GoScriptStore.insert(sp);
    if (go->GetGoType() != GAMEOBJECT_TYPE_CAPTURE_POINT)
        return;

    if (OPvPCapturePoint *cp = GetCapturePoint(go->GetSpawnId()))
        cp->m_capturePoint = go;
}

void OutdoorPvP::OnGameObjectRemove(GameObject* go)
{
    m_GoScriptStore.erase(go->GetGUID().GetCounter());

    if (go->GetGoType() != GAMEOBJECT_TYPE_CAPTURE_POINT)
        return;

    if (OPvPCapturePoint *cp = GetCapturePoint(go->GetSpawnId()))
        cp->m_capturePoint = nullptr;
}

void OutdoorPvP::OnCreatureCreate(Creature* creature)
{
    CreatureScriptPair sp(creature->GetGUID().GetCounter(), creature);
    m_CreatureScriptStore.insert(sp);
}

void OutdoorPvP::OnCreatureRemove(Creature* creature)
{
    m_CreatureScriptStore.erase(creature->GetGUID().GetCounter());
}

void OutdoorPvP::SendDefenseMessage(uint32 zoneId, uint32 id)
{
    DefenseMessageBuilder builder(zoneId, id);
    Trinity::LocalizedPacketDo<DefenseMessageBuilder> localizer(builder);
    BroadcastWorker(localizer, zoneId);
}

template<class Worker>
void OutdoorPvP::BroadcastWorker(Worker& _worker, uint32 zoneId)
{
    for (uint32 i = 0; i < PVP_TEAMS_COUNT; ++i)
        for (GuidSet::iterator itr = m_players[i].begin(); itr != m_players[i].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->GetZoneId() == zoneId)
                    _worker(player);
}

void OutdoorPvP::SetMapFromZone(uint32 zone)
{
    AreaTableEntry const* areaTable = sAreaTableStore.AssertEntry(zone);
    Map* map = sMapMgr->CreateBaseMap(areaTable->ContinentID);
    ASSERT(!map->Instanceable());
    m_map = map;
}
