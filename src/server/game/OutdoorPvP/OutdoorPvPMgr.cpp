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
 * @file OutdoorPvPMgr.cpp
 * @brief 户外PvP管理器实现文件
 *
 * 本文件实现了OutdoorPvPMgr类的所有功能，包括：
 * - 户外PvP实例的初始化和管理
 * - 玩家事件的路由分发
 * - 定期更新所有户外PvP区域
 *
 * 初始化流程：
 * 1. 从数据库outdoorpvp_template表加载模板
 * 2. 通过脚本系统创建户外PvP实例
 * 3. 调用SetupOutdoorPvP()初始化每个实例
 * 4. 将成功初始化的实例添加到管理列表
 *
 * 更新流程：
 * - 使用定时器控制更新频率（OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL）
 * - 每隔指定时间更新所有户外PvP实例
 */

#include "OutdoorPvPMgr.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

/**
 * @brief OutdoorPvPMgr构造函数
 *
 * 初始化更新计时器为0
 */
OutdoorPvPMgr::OutdoorPvPMgr()
{
    m_UpdateTimer = 0;
}

/**
 * @brief 清理所有户外PvP实例
 *
 * 删除所有户外PvP实例并清空所有容器
 */
void OutdoorPvPMgr::Die()
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
        delete *itr;

    m_OutdoorPvPSet.clear();

    m_OutdoorPvPDatas.fill(0);

    m_OutdoorPvPMap.clear();
}

/**
 * @brief 获取单例实例
 * @return OutdoorPvPMgr单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
OutdoorPvPMgr* OutdoorPvPMgr::instance()
{
    static OutdoorPvPMgr instance;
    return &instance;
}

/**
 * @brief 初始化所有户外PvP事件
 *
 * 从数据库加载户外PvP模板，创建并初始化所有户外PvP实例
 *
 * 加载流程：
 * 1. 从outdoorpvp_template表读取模板数据（TypeId, ScriptName）
 * 2. 跳过被禁用的户外PvP类型
 * 3. 验证TypeId有效性
 * 4. 通过脚本系统创建户外PvP实例
 * 5. 调用SetupOutdoorPvP()初始化每个实例
 * 6. 将成功初始化的实例添加到管理列表
 *
 * 错误处理：
 * - 无效TypeId：记录错误日志并跳过
 * - 脚本创建失败：记录错误日志并跳过
 * - SetupOutdoorPvP失败：删除实例并跳过
 */
void OutdoorPvPMgr::InitOutdoorPvP()
{
    uint32 oldMSTime = getMSTime();

    // 从数据库查询户外PvP模板
    //                                                 0       1
    QueryResult result = WorldDatabase.Query("SELECT TypeId, ScriptName FROM outdoorpvp_template");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 outdoor PvP definitions. DB table `outdoorpvp_template` is empty.");
        return;
    }

    uint32 count = 0;
    uint32 typeId = 0;

    // 加载所有模板数据
    do
    {
        Field* fields = result->Fetch();

        typeId = fields[0].GetUInt8();

        // 检查是否被禁用
        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_OUTDOORPVP, typeId, nullptr))
            continue;

        // 验证TypeId有效性
        if (typeId >= MAX_OUTDOORPVP_TYPES)
        {
            TC_LOG_ERROR("sql.sql", "Invalid OutdoorPvPTypes value {} in outdoorpvp_template; skipped.", typeId);
            continue;
        }

        OutdoorPvPTypes realTypeId = OutdoorPvPTypes(typeId);
        m_OutdoorPvPDatas[realTypeId] = sObjectMgr->GetScriptId(fields[1].GetString());

        ++count;
    }
    while (result->NextRow());

    // 创建并初始化所有户外PvP实例
    OutdoorPvP* pvp;
    for (uint8 i = 1; i < MAX_OUTDOORPVP_TYPES; ++i)
    {
        if (!m_OutdoorPvPDatas[i])
        {
            TC_LOG_ERROR("sql.sql", "Could not initialize OutdoorPvP object for type ID {}; no entry in database.", uint32(i));
            continue;
        }

        // 通过脚本系统创建户外PvP实例
        pvp = sScriptMgr->CreateOutdoorPvP(m_OutdoorPvPDatas[i]);
        if (!pvp)
        {
            TC_LOG_ERROR("outdoorpvp", "Could not initialize OutdoorPvP object for type ID {}; got NULL pointer from script.", uint32(i));
            continue;
        }

        // 初始化户外PvP实例
        if (!pvp->SetupOutdoorPvP())
        {
            TC_LOG_ERROR("outdoorpvp", "Could not initialize OutdoorPvP object for type ID {}; SetupOutdoorPvP failed.", uint32(i));
            delete pvp;
            continue;
        }

        m_OutdoorPvPSet.push_back(pvp);
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} outdoor PvP definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 添加区域到户外PvP映射
 * @param zoneid 区域ID
 * @param handle 户外PvP实例指针
 *
 * 在SetupOutdoorPvP中被调用，建立区域ID到户外PvP实例的映射关系
 */
void OutdoorPvPMgr::AddZone(uint32 zoneid, OutdoorPvP* handle)
{
    m_OutdoorPvPMap[zoneid] = handle;
}

/**
 * @brief 处理玩家进入户外PvP区域
 * @param player 进入的玩家指针
 * @param zoneid 区域ID
 *
 * 查找区域对应的户外PvP实例并通知玩家进入事件
 */
void OutdoorPvPMgr::HandlePlayerEnterZone(Player* player, uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
        return;

    // 检查玩家是否已经在该户外PvP中
    if (itr->second->HasPlayer(player))
        return;

    itr->second->HandlePlayerEnterZone(player, zoneid);
    TC_LOG_DEBUG("outdoorpvp", "Player {} entered outdoorpvp id {}", player->GetGUID().ToString(), itr->second->GetTypeId());
}

/**
 * @brief 处理玩家离开户外PvP区域
 * @param player 离开的玩家指针
 * @param zoneid 区域ID
 *
 * 查找区域对应的户外PvP实例并通知玩家离开事件
 *
 * 注意：传送时会触发两次（RemoveFromWorld和UpdateZone），需检查玩家是否在实例中
 */
void OutdoorPvPMgr::HandlePlayerLeaveZone(Player* player, uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
        return;

    // 传送：在RemoveFromWorld和UpdateZone中各触发一次，需检查玩家是否在实例中
    if (!itr->second->HasPlayer(player))
        return;

    itr->second->HandlePlayerLeaveZone(player, zoneid);
    TC_LOG_DEBUG("outdoorpvp", "Player {} left outdoorpvp id {}", player->GetGUID().ToString(), itr->second->GetTypeId());
}

/**
 * @brief 根据区域ID获取对应的户外PvP实例
 * @param zoneid 区域ID
 * @return 对应的OutdoorPvP指针，无则返回nullptr
 */
OutdoorPvP* OutdoorPvPMgr::GetOutdoorPvPToZoneId(uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
    {
        // 该区域没有对应的户外PvP实例
        return nullptr;
    }
    return itr->second;
}

/**
 * @brief 更新所有户外PvP实例
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 使用定时器控制更新频率，每隔OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL毫秒更新一次
 * 所有户外PvP实例共享同一个更新计时器
 */
void OutdoorPvPMgr::Update(uint32 diff)
{
    m_UpdateTimer += diff;
    if (m_UpdateTimer > OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL)
    {
        // 更新所有户外PvP实例
        for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
            (*itr)->Update(m_UpdateTimer);
        m_UpdateTimer = 0;
    }
}

/**
 * @brief 处理自定义法术
 * @param player 施法玩家指针
 * @param spellId 法术ID
 * @param go 目标游戏对象指针
 * @return 是否成功处理
 *
 * 遍历所有户外PvP实例，找到能处理该法术的实例
 */
bool OutdoorPvPMgr::HandleCustomSpell(Player* player, uint32 spellId, GameObject* go)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleCustomSpell(player, spellId, go))
            return true;
    }
    return false;
}

/**
 * @brief 获取区域的脚本实例
 * @param zoneId 区域ID
 * @return ZoneScript指针，无则返回nullptr
 */
ZoneScript* OutdoorPvPMgr::GetZoneScript(uint32 zoneId)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneId);
    if (itr != m_OutdoorPvPMap.end())
        return itr->second;
    else
        return nullptr;
}

/**
 * @brief 处理打开游戏对象
 * @param player 操作玩家指针
 * @param go 被打开的游戏对象指针
 * @return 是否成功处理
 *
 * 遍历所有户外PvP实例，找到能处理该游戏对象的实例
 */
bool OutdoorPvPMgr::HandleOpenGo(Player* player, GameObject* go)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleOpenGo(player, go))
            return true;
    }
    return false;
}

/**
 * @brief 处理NPC对话选项
 * @param player 玩家指针
 * @param creature NPC指针
 * @param gossipid 选项ID
 *
 * 遍历所有户外PvP实例，找到能处理该对话的实例
 */
void OutdoorPvPMgr::HandleGossipOption(Player* player, Creature* creature, uint32 gossipid)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleGossipOption(player, creature, gossipid))
            return;
    }
}

/**
 * @brief 检查玩家是否可以与NPC对话
 * @param player 玩家指针
 * @param creature NPC指针
 * @param gso 对话菜单项
 * @return 是否可以对话
 */
bool OutdoorPvPMgr::CanTalkTo(Player* player, Creature* creature, GossipMenuItems const& gso)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->CanTalkTo(player, creature, gso))
            return true;
    }
    return false;
}

/**
 * @brief 处理玩家丢弃旗帜
 * @param player 玩家指针
 * @param spellId 法术ID
 *
 * 遍历所有户外PvP实例，找到能处理该旗帜的实例
 */
void OutdoorPvPMgr::HandleDropFlag(Player* player, uint32 spellId)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleDropFlag(player, spellId))
            return;
    }
}

/**
 * @brief 处理玩家复活
 * @param player 复活的玩家指针
 * @param zoneid 区域ID
 *
 * 查找区域对应的户外PvP实例并通知玩家复活事件
 */
void OutdoorPvPMgr::HandlePlayerResurrects(Player* player, uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
        return;

    if (itr->second->HasPlayer(player))
        itr->second->HandlePlayerResurrects(player, zoneid);
}

/**
 * @brief 获取防御消息文本
 * @param zoneId 区域ID
 * @param id 广播文本ID（对应broadcast_text表）
 * @param locale 语言区域设置
 * @return 本地化的消息文本
 *
 * 从广播文本表获取指定语言的文本
 */
std::string OutdoorPvPMgr::GetDefenseMessage(uint32 zoneId, uint32 id, LocaleConstant locale) const
{
    if (BroadcastText const* bct = sObjectMgr->GetBroadcastText(id))
        return bct->GetText(locale);

    TC_LOG_ERROR("outdoorpvp", "Can not find DefenseMessage (Zone: {}, Id: {}). BroadcastText (Id: {}) does not exist.", zoneId, id, id);
    return "";
}
