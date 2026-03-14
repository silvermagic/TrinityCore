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
 * @file InstanceSaveMgr.cpp
 * @brief 副本存档管理器实现文件
 *
 * 实现了副本存档的创建、加载、保存、重置等核心功能。
 * 包括实例绑定管理、重置时间调度、数据库持久化等操作。
 */

#include "InstanceSaveMgr.h"
#include "Common.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridStates.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MapInstanced.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "World.h"

/// 重置延迟时间数组(秒): 分别对应1小时、15分钟、5分钟、1分钟前的警告
uint16 InstanceSaveManager::ResetTimeDelay[] = {3600, 900, 300, 60};

InstanceSaveManager::~InstanceSaveManager()
{
}

/**
 * @brief 获取单例实例
 * @return InstanceSaveManager单例指针
 */
InstanceSaveManager* InstanceSaveManager::instance()
{
    static InstanceSaveManager instance;
    return &instance;
}

/**
 * @brief 卸载所有实例存档
 *
 * 在服务器关闭时调用,解绑所有玩家和团队,清理所有实例存档数据
 */
void InstanceSaveManager::Unload()
{
    lock_instLists = true;  // 锁定实例列表,防止卸载过程中修改

    // 遍历所有实例存档
    for (InstanceSaveHashMap::iterator itr = m_instanceSaveById.begin(); itr != m_instanceSaveById.end(); ++itr)
    {
        InstanceSave* save = itr->second;

        // 解绑所有玩家
        for (InstanceSave::PlayerListType::iterator itr2 = save->m_playerList.begin(), next = itr2; itr2 != save->m_playerList.end(); itr2 = next)
        {
            ++next;  // 先保存下一个迭代器,因为UnbindInstance可能修改列表
            (*itr2)->UnbindInstance(save->GetMapId(), save->GetDifficulty(), true);
        }

        // 解绑所有团队
        for (InstanceSave::GroupListType::iterator itr2 = save->m_groupList.begin(), next = itr2; itr2 != save->m_groupList.end(); itr2 = next)
        {
            ++next;  // 先保存下一个迭代器
            (*itr2)->UnbindInstance(save->GetMapId(), save->GetDifficulty(), true);
        }

        delete save;
    }
}

/**
 * @brief 添加实例存档到管理器
 * @param mapId 地图ID
 * @param instanceId 实例ID
 * @param difficulty 难度等级
 * @param resetTime 重置时间戳(0表示需要初始化)
 * @param canReset 是否可以重置
 * @param load 是否从数据库加载
 * @return 实例存档指针
 *
 * 调用时机:
 * - InstanceMap::Add 创建新实例地图时
 * - Player::_LoadBoundInstances 加载玩家绑定的实例时
 * - Group::LoadGroup 加载团队绑定的实例时
 */
InstanceSave* InstanceSaveManager::AddInstanceSave(uint32 mapId, uint32 instanceId, Difficulty difficulty, time_t resetTime, bool canReset, bool load)
{
    // 如果实例存档已存在,直接返回
    if (InstanceSave* old_save = GetInstanceSave(instanceId))
        return old_save;

    // 验证地图ID是否有效
    MapEntry const* entry = sMapStore.LookupEntry(mapId);
    if (!entry)
    {
        TC_LOG_ERROR("misc", "InstanceSaveManager::AddInstanceSave: wrong mapid = {}, instanceid = {}!", mapId, instanceId);
        return nullptr;
    }

    // 验证实例ID是否有效
    if (instanceId == 0)
    {
        TC_LOG_ERROR("misc", "InstanceSaveManager::AddInstanceSave: mapid = {}, wrong instanceid = {}!", mapId, instanceId);
        return nullptr;
    }

    // 验证难度等级是否有效
    if (difficulty >= (entry->IsRaid() ? MAX_RAID_DIFFICULTY : MAX_DUNGEON_DIFFICULTY))
    {
        TC_LOG_ERROR("misc", "InstanceSaveManager::AddInstanceSave: mapid = {}, instanceid = {}, wrong dificalty {}!", mapId, instanceId, static_cast<uint32>(difficulty));
        return nullptr;
    }

    // 如果没有提供重置时间,则需要初始化
    if (!resetTime)
    {
        // 团队副本和英雄副本使用全局重置时间
        if (entry->InstanceType == MAP_RAID || difficulty > DUNGEON_DIFFICULTY_NORMAL)
            resetTime = GetResetTimeFor(mapId, difficulty);
        else
        {
            // 普通副本: 如果没有生物被击杀,实例将在2小时后重置
            resetTime = GameTime::GetGameTime() + 2 * HOUR;
            // 调度重置事件(通常会在InstanceMap::Add中很快被移除,防止错误)
            ScheduleReset(true, resetTime, InstResetEvent(0, mapId, difficulty, instanceId));
        }
    }

    TC_LOG_DEBUG("maps", "InstanceSaveManager::AddInstanceSave: mapid = {}, instanceid = {}", mapId, instanceId);

    // 创建新的实例存档
    InstanceSave* save = new InstanceSave(mapId, instanceId, difficulty, resetTime, canReset);
    if (!load)
        save->SaveToDB();  // 新创建的实例需要保存到数据库

    // 添加到管理器的哈希映射中
    m_instanceSaveById[instanceId] = save;
    return save;
}

/**
 * @brief 获取实例存档
 * @param InstanceId 实例ID
 * @return 实例存档指针,不存在则返回nullptr
 */
InstanceSave* InstanceSaveManager::GetInstanceSave(uint32 InstanceId)
{
    InstanceSaveHashMap::iterator itr = m_instanceSaveById.find(InstanceId);
    return itr != m_instanceSaveById.end() ? itr->second : nullptr;
}

/**
 * @brief 从数据库删除实例记录
 * @param instanceid 实例ID
 *
 * 删除instance、character_instance、group_instance表中的相关记录
 * 注意: 重生时间应该在地图卸载时删除,而不是在这里
 */
void InstanceSaveManager::DeleteInstanceFromDB(uint32 instanceid)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 删除instance表记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_INSTANCE_BY_INSTANCE);
    stmt->setUInt32(0, instanceid);
    trans->Append(stmt);

    // 删除character_instance表记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_INSTANCE_BY_INSTANCE);
    stmt->setUInt32(0, instanceid);
    trans->Append(stmt);

    // 删除group_instance表记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_INSTANCE_BY_INSTANCE);
    stmt->setUInt32(0, instanceid);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 移除实例存档
 * @param InstanceId 实例ID
 *
 * 从管理器中移除实例存档,并保存普通副本的重置时间到数据库
 */
void InstanceSaveManager::RemoveInstanceSave(uint32 InstanceId)
{
    InstanceSaveHashMap::iterator itr = m_instanceSaveById.find(InstanceId);
    if (itr != m_instanceSaveById.end())
    {
        // 仅保存普通副本的重置时间(团队/英雄副本不保存单独的重置时间)
        if (time_t resettime = itr->second->GetResetTimeForDB())
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_INSTANCE_RESETTIME);

            stmt->setUInt64(0, uint64(resettime));
            stmt->setUInt32(1, InstanceId);

            CharacterDatabase.Execute(stmt);
        }

        // 标记为待删除,从映射中移除
        itr->second->SetToDelete(true);
        m_instanceSaveById.erase(itr);
    }
}

/**
 * @brief 卸载实例存档
 * @param InstanceId 实例ID
 *
 * 如果实例存档为空(没有绑定玩家和团队)则卸载
 */
void InstanceSaveManager::UnloadInstanceSave(uint32 InstanceId)
{
    if (InstanceSave* save = GetInstanceSave(InstanceId))
    {
        save->UnloadIfEmpty();
        if (save->m_toDelete)
            delete save;
    }
}

/**
 * @brief InstanceSave构造函数
 * @param MapId 地图ID
 * @param InstanceId 实例ID
 * @param difficulty 难度等级
 * @param resetTime 重置时间戳
 * @param canReset 是否可以重置
 */
InstanceSave::InstanceSave(uint16 MapId, uint32 InstanceId, Difficulty difficulty, time_t resetTime, bool canReset)
: m_resetTime(resetTime), m_instanceid(InstanceId), m_mapid(MapId),
  m_difficulty(difficulty), m_canReset(canReset), m_toDelete(false) { }

/**
 * @brief InstanceSave析构函数
 *
 * 确保在删除存档前,玩家和团队已经被解绑
 */
InstanceSave::~InstanceSave()
{
    // 玩家和团队必须在删除存档前解绑
    ASSERT(m_playerList.empty() && m_groupList.empty());
}

/**
 * @brief 将实例保存到数据库
 *
 * 在AddInstanceSave中调用
 */
void InstanceSave::SaveToDB()
{
    // 保存实例数据
    std::string data;
    uint32 completedEncounters = 0;

    // 如果地图已加载,从实例脚本获取保存数据
    Map* map = sMapMgr->FindMap(GetMapId(), m_instanceid);
    if (map)
    {
        ASSERT(map->IsDungeon());
        if (InstanceScript* instanceScript = ((InstanceMap*)map)->GetInstanceScript())
        {
            data = instanceScript->GetSaveData();
            completedEncounters = instanceScript->GetCompletedEncounterMask();
        }
    }

    // 插入instance表记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_INSTANCE_SAVE);
    stmt->setUInt32(0, m_instanceid);
    stmt->setUInt16(1, GetMapId());
    stmt->setUInt64(2, uint64(GetResetTimeForDB()));
    stmt->setUInt8(3, uint8(GetDifficulty()));
    stmt->setUInt32(4, completedEncounters);
    stmt->setString(5, data);
    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 获取用于数据库存储的重置时间
 * @return 重置时间戳,团队/英雄副本返回0
 *
 * 仅保存普通副本的重置时间
 */
time_t InstanceSave::GetResetTimeForDB()
{
    // 仅保存普通副本的重置时间
    MapEntry const* entry = sMapStore.LookupEntry(GetMapId());
    if (!entry || entry->InstanceType == MAP_RAID || GetDifficulty() == DUNGEON_DIFFICULTY_HEROIC)
        return 0;
    else
        return GetResetTime();
}

/**
 * @brief 获取副本模板
 * @return 副本模板指针
 */
InstanceTemplate const* InstanceSave::GetTemplate()
{
    return sObjectMgr->GetInstanceTemplate(m_mapid);
}

/**
 * @brief 获取地图条目
 * @return 地图条目指针
 */
MapEntry const* InstanceSave::GetMapEntry()
{
    return sMapStore.LookupEntry(m_mapid);
}

/**
 * @brief 从数据库删除实例记录
 */
void InstanceSave::DeleteFromDB()
{
    InstanceSaveManager::DeleteInstanceFromDB(GetInstanceId());
}

/**
 * @brief 如果玩家列表和团队列表为空则卸载实例
 * @return 实例存档是否仍然有效
 */
bool InstanceSave::UnloadIfEmpty()
{
    // 检查玩家列表和团队列表是否为空
    if (m_playerList.empty() && m_groupList.empty())
    {
        // 如果地图中还有玩家,不要移除存档
        if (Map* map = sMapMgr->FindMap(GetMapId(), GetInstanceId()))
            if (map->HavePlayers())
                return true;

        // 从管理器中移除存档(如果没有锁定)
        if (!sInstanceSaveMgr->lock_instLists)
            sInstanceSaveMgr->RemoveInstanceSave(GetInstanceId());

        return false;  // 实例存档已无效
    }
    else
        return true;   // 实例存档仍然有效
}

/**
 * @brief 加载所有实例数据
 *
 * 在服务器启动时调用,执行以下操作:
 * 1. 删除过期的实例
 * 2. 清理无效的绑定关系
 * 3. 初始化实例ID存储
 * 4. 加载重置时间
 */
void InstanceSaveManager::LoadInstances()
{
    uint32 oldMSTime = getMSTime();

    // 删除过期的实例(实例相关的重生点会在后续清理查询中删除)
    CharacterDatabase.DirectExecute("DELETE i FROM instance i LEFT JOIN instance_reset ir ON mapid = map AND i.difficulty = ir.difficulty "
                                    "WHERE (i.resettime > 0 AND i.resettime < UNIX_TIMESTAMP()) OR (ir.resettime IS NOT NULL AND ir.resettime < UNIX_TIMESTAMP())");

    // 删除无效的character_instance和group_instance引用(玩家/团队已不存在)
    CharacterDatabase.DirectExecute("DELETE ci.* FROM character_instance AS ci LEFT JOIN characters AS c ON ci.guid = c.guid WHERE c.guid IS NULL");
    CharacterDatabase.DirectExecute("DELETE gi.* FROM group_instance     AS gi LEFT JOIN `groups`   AS g ON gi.guid = g.guid WHERE g.guid IS NULL");

    // 删除无效的实例引用(没有玩家或团队绑定)
    CharacterDatabase.DirectExecute("DELETE i.* FROM instance AS i LEFT JOIN character_instance AS ci ON i.id = ci.instance LEFT JOIN group_instance AS gi ON i.id = gi.instance WHERE ci.guid IS NULL AND gi.guid IS NULL");

    // 删除对实例的无效引用
    CharacterDatabase.DirectExecute("DELETE FROM respawn WHERE instanceId > 0 AND instanceId NOT IN (SELECT id FROM instance)");
    CharacterDatabase.DirectExecute("DELETE tmp.* FROM character_instance AS tmp LEFT JOIN instance ON tmp.instance = instance.id WHERE tmp.instance > 0 AND instance.id IS NULL");
    CharacterDatabase.DirectExecute("DELETE tmp.* FROM group_instance     AS tmp LEFT JOIN instance ON tmp.instance = instance.id WHERE tmp.instance > 0 AND instance.id IS NULL");

    // 清理对实例的无效引用
    CharacterDatabase.DirectExecute("UPDATE corpse SET instanceId = 0 WHERE instanceId > 0 AND instanceId NOT IN (SELECT id FROM instance)");
    CharacterDatabase.DirectExecute("UPDATE characters AS tmp LEFT JOIN instance ON tmp.instance_id = instance.id SET tmp.instance_id = 0 WHERE tmp.instance_id > 0 AND instance.id IS NULL");

    // 初始化实例ID存储(需要在清理垃圾数据之后)
    sMapMgr->InitInstanceIds();

    // 加载重置时间并清理过期实例
    sInstanceSaveMgr->LoadResetTimes();

    TC_LOG_INFO("server.loading", ">> Loaded instances in {} ms", GetMSTimeDiffToNow(oldMSTime));

}

/**
 * @brief 加载重置时间
 *
 * 从数据库加载实例和全局重置时间,并调度重置事件
 */
void InstanceSaveManager::LoadResetTimes()
{
    time_t now = GameTime::GetGameTime();
    time_t today = (now / DAY) * DAY;

    // 注意: 对后续需要查询的表使用DirectPExecute

    // 获取普通实例的当前重置时间(可能需要更新)
    // 这些仅在内存中保存,供后续加载的InstanceSaves使用
    // 团队/英雄实例的resettime在数据库中为0,所以跳过
    typedef std::pair<uint32 /*PAIR32(map, difficulty)*/, time_t> ResetTimeMapDiffType;
    typedef std::map<uint32, ResetTimeMapDiffType> InstResetTimeMapDiffType;
    InstResetTimeMapDiffType instResetTime;

    // 按地图/难度对索引实例ID,用于快速发送重置警告
    typedef std::multimap<uint32 /*PAIR32(map, difficulty)*/, uint32 /*instanceid*/ > ResetTimeMapDiffInstances;
    typedef std::pair<ResetTimeMapDiffInstances::const_iterator, ResetTimeMapDiffInstances::const_iterator> ResetTimeMapDiffInstancesBounds;
    ResetTimeMapDiffInstances mapDiffResetInstances;

    // 查询所有实例
    if (QueryResult result = CharacterDatabase.Query("SELECT id, map, difficulty, resettime FROM instance ORDER BY id ASC"))
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 instanceId = fields[0].GetUInt32();

            // 标记实例ID为已使用
            sMapMgr->RegisterInstanceId(instanceId);

            // 如果有重置时间(普通实例)
            if (time_t resettime = time_t(fields[3].GetUInt64()))
            {
                uint32 mapid = fields[1].GetUInt16();
                uint32 difficulty = fields[2].GetUInt8();

                instResetTime[instanceId] = ResetTimeMapDiffType(MAKE_PAIR32(mapid, difficulty), resettime);
                mapDiffResetInstances.insert(ResetTimeMapDiffInstances::value_type(MAKE_PAIR32(mapid, difficulty), instanceId));
            }
        }
        while (result->NextRow());

        // 调度重置时间
        for (InstResetTimeMapDiffType::iterator itr = instResetTime.begin(); itr != instResetTime.end(); ++itr)
            if (itr->second.second > now)
                ScheduleReset(true, itr->second.second, InstResetEvent(0, PAIR32_LOPART(itr->second.first), Difficulty(PAIR32_HIPART(itr->second.first)), itr->first));
    }

    // 加载团队/英雄实例的全局重生时间
    uint32 resetHour = sWorld->getIntConfig(CONFIG_INSTANCE_RESET_TIME_HOUR);
    if (QueryResult result = CharacterDatabase.Query("SELECT mapid, difficulty, resettime FROM instance_reset"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 mapid = fields[0].GetUInt16();
            Difficulty difficulty = Difficulty(fields[1].GetUInt8());
            uint64 oldresettime = fields[2].GetUInt64();

            // 验证地图难度是否有效
            MapDifficulty const* mapDiff = GetMapDifficultyData(mapid, difficulty);
            if (!mapDiff)
            {
                TC_LOG_ERROR("misc", "InstanceSaveManager::LoadResetTimes: invalid mapid({})/difficulty({}) pair in instance_reset!", mapid, static_cast<uint32>(difficulty));

                // 删除无效记录
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GLOBAL_INSTANCE_RESETTIME);
                stmt->setUInt16(0, uint16(mapid));
                stmt->setUInt8(1, uint8(difficulty));
                CharacterDatabase.DirectExecute(stmt);
                continue;
            }

            // 如果配置中的小时数发生变化,更新重置时间
            uint64 newresettime = GetLocalHourTimestamp(oldresettime, resetHour, false);
            if (oldresettime != newresettime)
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GLOBAL_INSTANCE_RESETTIME);
                stmt->setUInt64(0, uint64(newresettime));
                stmt->setUInt16(1, uint16(mapid));
                stmt->setUInt8(2, uint8(difficulty));
                CharacterDatabase.DirectExecute(stmt);
            }

            InitializeResetTimeFor(mapid, difficulty, newresettime);
        } while (result->NextRow());
    }

    // 为过期实例和从未重置过的实例计算新的全局重置时间
    // 将全局重置时间添加到优先队列
    for (MapDifficultyMap::const_iterator itr = sMapDifficultyMap.begin(); itr != sMapDifficultyMap.end(); ++itr)
    {
        uint32 map_diff_pair = itr->first;
        uint32 mapid = PAIR32_LOPART(map_diff_pair);
        Difficulty difficulty = Difficulty(PAIR32_HIPART(map_diff_pair));
        MapDifficulty const* mapDiff = &itr->second;
        if (!mapDiff->resetTime)
            continue;

        // 重置延迟必须至少为一天
        uint32 period = uint32(((mapDiff->resetTime * sWorld->getRate(RATE_INSTANCE_RESET_TIME)) / float(DAY)) * float(DAY));
        if (period < DAY)
            period = DAY;

        time_t t = GetResetTimeFor(mapid, difficulty);
        if (!t)
        {
            // 初始化重置时间
            t = GetLocalHourTimestamp(today + period, resetHour);

            // 插入新的全局重置时间记录
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GLOBAL_INSTANCE_RESETTIME);
            stmt->setUInt16(0, uint16(mapid));
            stmt->setUInt8(1, uint8(difficulty));
            stmt->setUInt64(2, uint64(t));
            CharacterDatabase.DirectExecute(stmt);
        }

        if (t < now)
        {
            // 假设过期实例已被清理
            // 计算下一次重置时间
            time_t day = (t / DAY) * DAY;
            t = GetLocalHourTimestamp(day + ((today - day) / period + 1) * period, resetHour);

            // 更新全局重置时间
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GLOBAL_INSTANCE_RESETTIME);
            stmt->setUInt64(0, uint64(t));
            stmt->setUInt16(1, uint16(mapid));
            stmt->setUInt8(2, uint8(difficulty));
            CharacterDatabase.DirectExecute(stmt);
        }

        InitializeResetTimeFor(mapid, difficulty, t);

        // 调度全局重置/警告
        uint8 type;
        for (type = 1; type < 4; ++type)
            if (t - ResetTimeDelay[type-1] > now)
                break;

        // 调度全局重置事件
        ScheduleReset(true, t - ResetTimeDelay[type-1], InstResetEvent(type, mapid, difficulty, 0));

        // 为该地图/难度的所有实例调度重置事件
        ResetTimeMapDiffInstancesBounds range = mapDiffResetInstances.equal_range(map_diff_pair);
        for (; range.first != range.second; ++range.first)
            ScheduleReset(true, t - ResetTimeDelay[type-1], InstResetEvent(type, mapid, difficulty, range.first->second));
    }
}

/**
 * @brief 获取后续重置时间
 * @param mapid 地图ID
 * @param difficulty 难度等级
 * @param resetTime 当前重置时间
 * @return 下一次重置时间戳
 */
time_t InstanceSaveManager::GetSubsequentResetTime(uint32 mapid, Difficulty difficulty, time_t resetTime) const
{
    MapDifficulty const* mapDiff = GetMapDifficultyData(mapid, difficulty);
    if (!mapDiff || !mapDiff->resetTime)
    {
        TC_LOG_ERROR("misc", "InstanceSaveManager::GetSubsequentResetTime: not valid difficulty or no reset delay for map {}", mapid);
        return 0;
    }

    time_t resetHour = sWorld->getIntConfig(CONFIG_INSTANCE_RESET_TIME_HOUR);
    time_t period = uint32(((mapDiff->resetTime * sWorld->getRate(RATE_INSTANCE_RESET_TIME)) / float(DAY)) * float(DAY));
    if (period < DAY)
        period = DAY;

    return GetLocalHourTimestamp(((resetTime + MINUTE) / DAY * DAY) + period, resetHour);
}

/**
 * @brief 设置重置时间
 * @param mapid 地图ID
 * @param d 难度等级
 * @param t 重置时间戳
 *
 * 仅用于更新现有重置时间,不用于初始化
 */
void InstanceSaveManager::SetResetTimeFor(uint32 mapid, Difficulty d, time_t t)
{
    ResetTimeByMapDifficultyMap::iterator itr = m_resetTimeByMapDifficulty.find(MAKE_PAIR32(mapid, d));
    ASSERT(itr != m_resetTimeByMapDifficulty.end());
    itr->second = t;
}

/**
 * @brief 调度重置事件
 * @param add true:添加事件, false:移除事件
 * @param time 重置时间戳
 * @param event 重置事件对象
 */
void InstanceSaveManager::ScheduleReset(bool add, time_t time, InstResetEvent event)
{
    if (!add)
    {
        // 从队列中查找并移除事件
        ResetTimeQueue::iterator itr;
        std::pair<ResetTimeQueue::iterator, ResetTimeQueue::iterator> range;
        range = m_resetTimeQueue.equal_range(time);
        for (itr = range.first; itr != range.second; ++itr)
        {
            if (itr->second == event)
            {
                m_resetTimeQueue.erase(itr);
                return;
            }
        }

        // 如果重置时间发生变化(极少发生),搜索整个队列
        if (itr == range.second)
        {
            for (itr = m_resetTimeQueue.begin(); itr != m_resetTimeQueue.end(); ++itr)
            {
                if (itr->second == event)
                {
                    m_resetTimeQueue.erase(itr);
                    return;
                }
            }

            if (itr == m_resetTimeQueue.end())
                TC_LOG_ERROR("misc", "InstanceSaveManager::ScheduleReset: cannot cancel the reset, the event({}, {}, {}) was not found!", event.type, event.mapid, event.instanceId);
        }
    }
    else
        m_resetTimeQueue.insert(std::pair<time_t, InstResetEvent>(time, event));
}

/**
 * @brief 强制全局重置
 * @param mapId 地图ID
 * @param difficulty 难度等级
 *
 * 立即强制重置指定地图和难度的所有实例
 */
void InstanceSaveManager::ForceGlobalReset(uint32 mapId, Difficulty difficulty)
{
    if (!GetDownscaledMapDifficultyData(mapId, difficulty))
        return;

    // 移除当前调度的重置时间
    ScheduleReset(false, 0, InstResetEvent(1, mapId, difficulty, 0));
    ScheduleReset(false, 0, InstResetEvent(4, mapId, difficulty, 0));

    // 强制实例全局重置
    _ResetOrWarnAll(mapId, difficulty, false, GameTime::GetGameTime());
}

/**
 * @brief 更新重置队列
 *
 * 每个世界更新周期调用,检查并执行到期的重置事件
 */
void InstanceSaveManager::Update()
{
    time_t now = GameTime::GetGameTime();
    time_t t;

    // 处理所有到期的重置事件
    while (!m_resetTimeQueue.empty())
    {
        t = m_resetTimeQueue.begin()->first;
        if (t >= now)
            break;  // 还没到重置时间

        InstResetEvent &event = m_resetTimeQueue.begin()->second;
        if (event.type == 0)
        {
            // 单个普通实例重置,最大生物重生时间 + X小时
            _ResetInstance(event.mapid, event.instanceId);
            m_resetTimeQueue.erase(m_resetTimeQueue.begin());
        }
        else
        {
            // 某个地图的全局重置/警告
            time_t resetTime = GetResetTimeFor(event.mapid, event.difficulty);
            _ResetOrWarnAll(event.mapid, event.difficulty, event.type != 4, resetTime);
            if (event.type != 4)
            {
                // 调度下一次警告/重置
                ++event.type;
                ScheduleReset(true, resetTime - ResetTimeDelay[event.type-1], event);
            }
            m_resetTimeQueue.erase(m_resetTimeQueue.begin());
        }
    }
}

/**
 * @brief 重置存档
 * @param itr 实例存档迭代器
 *
 * 解绑所有玩家和团队,根据条件删除或保留存档
 */
void InstanceSaveManager::_ResetSave(InstanceSaveHashMap::iterator &itr)
{
    // 解绑所有绑定到实例的玩家
    // 不允许UnbindInstance自动卸载InstanceSaves
    lock_instLists = true;

    bool shouldDelete = true;
    InstanceSave::PlayerListType &pList = itr->second->m_playerList;
    std::vector<Player*> temp;  // 应该被解绑的过期绑定列表
    for (Player* player : pList)
    {
        if (InstancePlayerBind* bind = player->GetBoundInstance(itr->second->GetMapId(), itr->second->GetDifficulty()))
        {
            ASSERT(bind->save == itr->second);
            if (bind->perm && bind->extendState)  // 永久绑定且未过期
            {
                // 实际的数据库提升已在调用者中完成
                bind->extendState = bind->extendState == EXTEND_STATE_EXTENDED ? EXTEND_STATE_NORMAL : EXTEND_STATE_EXPIRED;
                shouldDelete = false;
                continue;
            }
        }
        temp.push_back(player);
    }

    // 解绑过期绑定的玩家
    for (Player* player : temp)
    {
        player->UnbindInstance(itr->second->GetMapId(), itr->second->GetDifficulty(), true);
    }

    // 解绑所有团队
    InstanceSave::GroupListType &gList = itr->second->m_groupList;
    while (!gList.empty())
    {
        Group* group = *(gList.begin());
        group->UnbindInstance(itr->second->GetMapId(), itr->second->GetDifficulty(), true);
    }

    // 根据条件删除或保留存档
    if (shouldDelete)
    {
        delete itr->second;
        itr = m_instanceSaveById.erase(itr);
    }
    else
        ++itr;

    lock_instLists = false;
}

/**
 * @brief 重置单个实例
 * @param mapid 地图ID
 * @param instanceId 实例ID
 *
 * 重置指定的实例,删除相关数据
 */
void InstanceSaveManager::_ResetInstance(uint32 mapid, uint32 instanceId)
{
    TC_LOG_DEBUG("maps", "InstanceSaveMgr::_ResetInstance {}, {}", mapid, instanceId);
    Map const* map = sMapMgr->CreateBaseMap(mapid);
    if (!map->Instanceable())
        return;

    // 重置存档(如果存在)
    InstanceSaveHashMap::iterator itr = m_instanceSaveById.find(instanceId);
    if (itr != m_instanceSaveById.end())
        _ResetSave(itr);

    // 从数据库删除实例记录(即使存档未加载)
    DeleteInstanceFromDB(instanceId);

    // 获取实例地图
    Map* iMap = ((MapInstanced*)map)->FindInstanceMap(instanceId);

    // 重置实例地图
    if (iMap && iMap->IsDungeon())
        ((InstanceMap*)iMap)->Reset(INSTANCE_RESET_RESPAWN_DELAY);

    // 删除重生时间和尸体数据
    if (iMap)
    {
        iMap->DeleteRespawnTimes();
        iMap->DeleteCorpseData();
    }
    else
        Map::DeleteRespawnTimesInDB(mapid, instanceId);

    // 释放实例ID,允许重用
    sMapMgr->FreeInstanceId(instanceId);
}

/**
 * @brief 重置或警告所有实例
 * @param mapid 地图ID
 * @param difficulty 难度等级
 * @param warn 是否仅发送警告
 * @param resetTime 重置时间
 *
 * 执行指定地图的全局重置或发送重置警告
 */
void InstanceSaveManager::_ResetOrWarnAll(uint32 mapid, Difficulty difficulty, bool warn, time_t resetTime)
{
    // 指定地图的所有实例的全局重置
    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    if (!mapEntry->Instanceable())
        return;
    TC_LOG_DEBUG("misc", "InstanceSaveManager::ResetOrWarnAll: Processing map {} ({}) on difficulty {} (warn? {})", mapEntry->MapName[0], mapid, static_cast<uint32>(difficulty), warn);

    time_t now = GameTime::GetGameTime();

    if (!warn)
    {
        // 计算下一次重置时间
        time_t next_reset = GetSubsequentResetTime(mapid, difficulty, resetTime);
        if (!next_reset)
            return;

        // 从数据库删除/提升实例绑定,即使未加载
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 删除过期的玩家实例绑定
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_EXPIRED_CHAR_INSTANCE_BY_MAP_DIFF);
        stmt->setUInt16(0, uint16(mapid));
        stmt->setUInt8(1, uint8(difficulty));
        trans->Append(stmt);

        // 删除团队实例绑定
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_INSTANCE_BY_MAP_DIFF);
        stmt->setUInt16(0, uint16(mapid));
        stmt->setUInt8(1, uint8(difficulty));
        trans->Append(stmt);

        // 删除过期实例
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_EXPIRED_INSTANCE_BY_MAP_DIFF);
        stmt->setUInt16(0, uint16(mapid));
        stmt->setUInt8(1, uint8(difficulty));
        trans->Append(stmt);

        // 标记玩家实例绑定为过期
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_EXPIRE_CHAR_INSTANCE_BY_MAP_DIFF);
        stmt->setUInt16(0, uint16(mapid));
        stmt->setUInt8(1, uint8(difficulty));
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);

        // 提升加载的指定地图实例的绑定
        for (InstanceSaveHashMap::iterator itr = m_instanceSaveById.begin(); itr != m_instanceSaveById.end();)
        {
            if (itr->second->GetMapId() == mapid && itr->second->GetDifficulty() == difficulty)
                _ResetSave(itr);
            else
                ++itr;
        }

        // 设置新的重置时间并调度下次重置
        SetResetTimeFor(mapid, difficulty, next_reset);
        ScheduleReset(true, time_t(next_reset-3600), InstResetEvent(1, mapid, difficulty, 0));

        // 更新数据库中的重置时间
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GLOBAL_INSTANCE_RESETTIME);

        stmt->setUInt64(0, uint64(next_reset));
        stmt->setUInt16(1, uint16(mapid));
        stmt->setUInt8(2, uint8(difficulty));

        CharacterDatabase.Execute(stmt);
    }

    // 注意: 这不是很快,但意味着很少执行
    Map* baseMap = sMapMgr->CreateBaseMap(mapid);  // 不包含难度
    uint32 timeLeft;

    // 遍历所有实例地图
    for (auto& [_, map] : baseMap->ToMapInstanced()->GetInstancedMaps())
    {
        InstanceMap* instanceMap = map->ToInstanceMap();
        if (warn)
        {
            // 发送重置警告
            if (now >= resetTime)
                timeLeft = 0;
            else
                timeLeft = uint32(resetTime - now);

            instanceMap->SendResetWarnings(timeLeft);
        }
        else
        {
            // 执行重置
            instanceMap->Reset(INSTANCE_RESET_GLOBAL);
        }
    }

    /// @todo 即使地图未加载也要删除生物/游戏对象的重生时间
}

/**
 * @brief 获取绑定玩家总数
 * @return 绑定玩家总数
 */
uint32 InstanceSaveManager::GetNumBoundPlayersTotal() const
{
    uint32 ret = 0;
    for (InstanceSaveHashMap::const_iterator itr = m_instanceSaveById.begin(); itr != m_instanceSaveById.end(); ++itr)
        ret += itr->second->GetPlayerCount();

    return ret;
}

/**
 * @brief 获取绑定团队总数
 * @return 绑定团队总数
 */
uint32 InstanceSaveManager::GetNumBoundGroupsTotal() const
{
    uint32 ret = 0;
    for (InstanceSaveHashMap::const_iterator itr = m_instanceSaveById.begin(); itr != m_instanceSaveById.end(); ++itr)
        ret += itr->second->GetGroupCount();

    return ret;
}
