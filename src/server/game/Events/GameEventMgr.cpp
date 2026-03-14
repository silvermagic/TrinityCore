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
 * @file GameEventMgr.cpp
 * @brief 游戏事件管理器实现文件
 *
 * 本文件实现了GameEventMgr类，负责管理游戏中的所有周期性事件和节日活动。
 * 主要功能包括：
 * - 从数据库加载事件数据
 * - 根据时间或条件自动启动和停止事件
 * - 生成和移除事件相关的生物、游戏对象
 * - 更新事件相关的任务、商人和装备
 * - 处理世界事件的条件检测和进度追踪
 * - 发送世界状态更新给玩家
 */

#include "GameEventMgr.h"
#include "BattlegroundMgr.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "PoolMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldStatePackets.h"

/**
 * @brief 获取GameEventMgr单例实例
 * @return GameEventMgr单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
GameEventMgr* GameEventMgr::instance()
{
    static GameEventMgr instance;
    return &instance;
}

/**
 * @brief 检查单个游戏事件是否应该激活
 * @param entry 事件ID
 * @return 如果事件应该激活返回true
 *
 * 根据事件状态和当前时间判断事件是否应该处于活动状态：
 * - GAMEEVENT_NORMAL: 根据时间范围和周期判断
 * - GAMEEVENT_WORLD_CONDITIONS/NEXTPHASE: 总是返回true
 * - GAMEEVENT_WORLD_FINISHED/INTERNAL: 总是返回false
 * - GAMEEVENT_WORLD_INACTIVE: 检查前置事件是否完成
 */
bool GameEventMgr::CheckOneGameEvent(uint16 entry) const
{
    switch (mGameEvent[entry].state)
    {
        default:
        case GAMEEVENT_NORMAL:
        {
            time_t currenttime = GameTime::GetGameTime();
            // 检查事件是否在时间范围内，并且当前周期未结束
            // 条件1: 当前时间在事件的总时间范围内
            // 条件2: 当前时间在事件周期的活动期内
            return mGameEvent[entry].start < currenttime
                && currenttime < mGameEvent[entry].end
                && (currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE) < mGameEvent[entry].length * MINUTE;
        }
        // 如果状态是CONDITIONS或NEXTPHASE，则事件应该处于活动状态
        case GAMEEVENT_WORLD_CONDITIONS:
        case GAMEEVENT_WORLD_NEXTPHASE:
            return true;
        // 已完成的世界事件和内部事件不活动
        case GAMEEVENT_WORLD_FINISHED:
        case GAMEEVENT_INTERNAL:
            return false;
        // 如果是非活动的世界事件，检查前置事件是否完成
        case GAMEEVENT_WORLD_INACTIVE:
        {
            time_t currenttime = GameTime::GetGameTime();
            // 遍历所有前置事件
            for (std::set<uint16>::const_iterator itr = mGameEvent[entry].prerequisite_events.begin(); itr != mGameEvent[entry].prerequisite_events.end(); ++itr)
            {
                // 如果前置事件不在NEXTPHASE或FINISHED状态，或者还没到开始时间，则无法启动本事件
                if ((mGameEvent[*itr].state != GAMEEVENT_WORLD_NEXTPHASE && mGameEvent[*itr].state != GAMEEVENT_WORLD_FINISHED) ||   // 如果前置事件不在下一阶段或已完成状态，则无法启动本事件
                    mGameEvent[*itr].nextstart > currenttime)               // 如果下一阶段状态持续时间不够长，无法启动本事件
                    return false;
            }
            // 所有前置事件条件都满足
            // 但如果没有前置事件，这只能通过GM命令激活
            return !(mGameEvent[entry].prerequisite_events.empty());
        }
    }
}

/**
 * @brief 计算下次检查事件的时间
 * @param entry 事件ID
 * @return 距离下次检查的秒数
 *
 * 根据事件状态和时间计算多久后需要再次检查该事件：
 * - NEXTPHASE/FINISHED状态：返回到下一阶段开始的时间
 * - CONDITIONS状态：返回检查条件的时间间隔
 * - 已过期事件：返回最大延迟
 * - 未开始事件：返回到开始的时间
 * - 活动中事件：返回到结束或下一周期开始的时间
 */
uint32 GameEventMgr::NextCheck(uint16 entry) const
{
    time_t currenttime = GameTime::GetGameTime();

    // 对于NEXTPHASE状态的世界事件，返回启动下一事件的延迟，以便正确检查后续事件
    if ((mGameEvent[entry].state == GAMEEVENT_WORLD_NEXTPHASE || mGameEvent[entry].state == GAMEEVENT_WORLD_FINISHED) && mGameEvent[entry].nextstart >= currenttime)
        return uint32(mGameEvent[entry].nextstart - currenttime);

    // 对于CONDITIONS状态的世界事件，返回等待周期的长度，这样如果条件满足，会再次调用此检查设置为NEXTPHASE事件
    if (mGameEvent[entry].state == GAMEEVENT_WORLD_CONDITIONS)
    {
        if (mGameEvent[entry].length)
            return mGameEvent[entry].length * 60;
        else
            return max_ge_check_delay;
    }

    // 过期事件：返回最大延迟
    if (currenttime > mGameEvent[entry].end)
        return max_ge_check_delay;

    // 从未启动的事件，返回到启动前的延迟
    if (mGameEvent[entry].start > currenttime)
        return uint32(mGameEvent[entry].start - currenttime);

    uint32 delay;
    // 如果在事件周期内，返回到结束的延迟
    if ((((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * 60)) < (mGameEvent[entry].length * 60)))
        // 返回到事件结束前的延迟
        delay = (mGameEvent[entry].length * MINUTE) - ((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE));
    else                                                    // 不在活动窗口内，返回到下次启动前的延迟
        delay = (mGameEvent[entry].occurence * MINUTE) - ((currenttime - mGameEvent[entry].start) % (mGameEvent[entry].occurence * MINUTE));
    // 如果结束时间在下次检查之前
    if (mGameEvent[entry].end  < time_t(currenttime + delay))
        return uint32(mGameEvent[entry].end - currenttime);
    else
        return delay;
}

/**
 * @brief 启动内部事件
 * @param event_id 事件ID
 *
 * 启动GAMEEVENT_INTERNAL类型的事件，这类事件不会通过Update自动处理
 */
void GameEventMgr::StartInternalEvent(uint16 event_id)
{
    if (event_id < 1 || event_id >= mGameEvent.size())
        return;

    if (!mGameEvent[event_id].isValid())
        return;

    // 如果事件已经活动，直接返回
    if (m_ActiveEvents.find(event_id) != m_ActiveEvents.end())
        return;

    StartEvent(event_id);
}

/**
 * @brief 启动指定事件
 * @param event_id 事件ID
 * @param overwrite 是否覆盖时间检查，默认false
 * @return 如果世界事件的条件立即满足返回true
 *
 * 手动启动事件，会生成事件相关的生物、游戏对象，更新任务等。
 * 当overwrite=true时，会调整事件时间到当前时间并强制启动。
 * 对于世界事件，会检查条件是否满足并更新状态。
 */
bool GameEventMgr::StartEvent(uint16 event_id, bool overwrite)
{
    GameEventData &data = mGameEvent[event_id];
    // 处理普通事件和内部事件
    if (data.state == GAMEEVENT_NORMAL || data.state == GAMEEVENT_INTERNAL)
    {
        // 添加到活动事件列表
        AddActiveEvent(event_id);
        // 应用事件（生成生物、游戏对象等）
        ApplyNewEvent(event_id);
        // 如果是覆盖模式，调整事件时间
        if (overwrite)
        {
            mGameEvent[event_id].start = GameTime::GetGameTime();
            if (data.end <= data.start)
                data.end = data.start + data.length;
        }

        // 事件启动时，设置世界状态为当前时间
        sWorld->setWorldState(event_id, GameTime::GetGameTime());
        return false;
    }
    else
    {
        // 处理世界事件
        if (data.state == GAMEEVENT_WORLD_INACTIVE)
            // 设置为条件检查阶段
            data.state = GAMEEVENT_WORLD_CONDITIONS;

        // 添加到活动事件列表
        AddActiveEvent(event_id);
        // 应用事件（生成生物、游戏对象等）
        ApplyNewEvent(event_id);

        // 检查是否可以进入下一状态
        bool conditions_met = CheckOneGameEventConditions(event_id);
        // 保存到数据库
        SaveWorldEventStateToDB(event_id);
        // 如果通过命令满足条件，强制游戏事件更新以设置更新定时器
        // 此更新用于启动依赖于此事件的其他事件
        // 或安排下次更新以启动下一事件
        if (overwrite && conditions_met)
            sWorld->ForceGameEventUpdate();

        return conditions_met;
    }
}

/**
 * @brief 停止指定事件
 * @param event_id 事件ID
 * @param overwrite 是否覆盖时间检查，默认false
 *
 * 手动停止事件，会移除事件相关的生物、游戏对象，恢复原始状态。
 * 当overwrite=true时，会更新事件时间防止自动重启。
 */
void GameEventMgr::StopEvent(uint16 event_id, bool overwrite)
{
    GameEventData &data = mGameEvent[event_id];
    bool serverwide_evt = data.state != GAMEEVENT_NORMAL && data.state != GAMEEVENT_INTERNAL;

    // 从活动事件列表移除
    RemoveActiveEvent(event_id);
    // 取消应用事件（移除生物、游戏对象等）
    UnApplyEvent(event_id);

    // 事件停止时，清理世界状态
    sWorld->setWorldState(event_id, 0);

    // 如果是覆盖模式且不是世界事件
    if (overwrite && !serverwide_evt)
    {
        // 设置开始时间为过去，防止自动重启
        data.start = GameTime::GetGameTime() - data.length * MINUTE;
        if (data.end <= data.start)
            data.end = data.start + data.length;
    }
    else if (serverwide_evt)
    {
        // 如果是已完成的世界事件，只有GM命令可以停止
        if (overwrite || data.state != GAMEEVENT_WORLD_FINISHED)
        {
            // 重置条件
            data.nextstart = 0;
            data.state = GAMEEVENT_WORLD_INACTIVE;
            GameEventConditionMap::iterator itr;
            for (itr = data.conditions.begin(); itr != data.conditions.end(); ++itr)
                itr->second.done = 0;

            // 从数据库删除条件保存数据
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ALL_GAME_EVENT_CONDITION_SAVE);
            stmt->setUInt8(0, uint8(event_id));
            trans->Append(stmt);

            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GAME_EVENT_SAVE);
            stmt->setUInt8(0, uint8(event_id));
            trans->Append(stmt);

            CharacterDatabase.CommitTransaction(trans);
        }
    }
}

/**
 * @brief 从数据库加载所有事件数据
 *
 * 加载事件定义、生物、游戏对象、任务、商人、模型装备等数据。
 * 调用时机：世界服务器启动时。
 * 加载顺序：
 * 1. 事件基础定义（game_event表）
 * 2. 事件前置条件（game_event_prerequisite表）
 * 3. 事件完成条件（game_event_condition表）
 * 4. 任务到事件条件映射（game_event_quest_condition表）
 * 5. 事件条件保存数据（game_event_condition_save表）
 * 6. 事件保存数据（game_event_save表）
 * 7. 事件关联的生物、游戏对象、池、任务等
 */
void GameEventMgr::LoadFromDB()
{
    {
        uint32 oldMSTime = getMSTime();
        //                                               0           1                           2                         3          4       5        6             7            8            9
        QueryResult result = WorldDatabase.Query("SELECT eventEntry, UNIX_TIMESTAMP(start_time), UNIX_TIMESTAMP(end_time), occurence, length, holiday, holidayStage, description, world_event, announce FROM game_event");
        if (!result)
        {
            mGameEvent.clear();
            TC_LOG_INFO("server.loading", ">> Loaded 0 game events. DB table `game_event` is empty.");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            uint8 event_id = fields[0].GetUInt8();
            // 事件ID为0是保留的，不能使用
            if (event_id == 0)
            {
                TC_LOG_ERROR("sql.sql", "`game_event`: game event entry 0 is reserved and can't be used.");
                continue;
            }

            GameEventData& pGameEvent = mGameEvent[event_id];
            uint64 starttime        = fields[1].GetUInt64();
            pGameEvent.start        = time_t(starttime);
            uint64 endtime          = fields[2].GetUInt64();
            pGameEvent.end          = time_t(endtime);
            pGameEvent.occurence    = fields[3].GetUInt64();
            pGameEvent.length       = fields[4].GetUInt64();
            pGameEvent.holiday_id   = HolidayIds(fields[5].GetUInt32());
            pGameEvent.holidayStage = fields[6].GetUInt8();
            pGameEvent.description  = fields[7].GetString();
            pGameEvent.state        = (GameEventState)(fields[8].GetUInt8());
            pGameEvent.announce     = fields[9].GetUInt8();
            pGameEvent.nextstart    = 0;

            ++count;

            if (pGameEvent.length == 0 && pGameEvent.state == GAMEEVENT_NORMAL)                            // length>0 is validity check
            {
                TC_LOG_ERROR("sql.sql", "`game_event`: game event id ({}) is not a world event and has length = 0, thus cannot be used.", event_id);
                continue;
            }

            if (pGameEvent.holiday_id != HOLIDAY_NONE)
            {
                if (!sHolidaysStore.LookupEntry(pGameEvent.holiday_id))
                {
                    TC_LOG_ERROR("sql.sql", "`game_event`: game event id ({}) contains nonexisting holiday id {}.", event_id, pGameEvent.holiday_id);
                    pGameEvent.holiday_id = HOLIDAY_NONE;
                    continue;
                }
                if (pGameEvent.holidayStage > MAX_HOLIDAY_DURATIONS)
                {
                    TC_LOG_ERROR("sql.sql", "`game_event` game event id ({}) has out of range holidayStage {}.", event_id, pGameEvent.holidayStage);
                    pGameEvent.holidayStage = 0;
                    continue;
                }

                SetHolidayEventTime(pGameEvent);
            }

        }
        while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));

    }

    TC_LOG_INFO("server.loading", "Loading Game Event Saves Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                       0       1        2
        QueryResult result = CharacterDatabase.Query("SELECT eventEntry, state, next_start FROM game_event_save");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 game event saves in game events. DB table `game_event_save` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint8 event_id = fields[0].GetUInt8();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_save`: game event entry ({}) is out of range compared to max event entry in `game_event`.", event_id);
                    continue;
                }

                if (mGameEvent[event_id].state != GAMEEVENT_NORMAL && mGameEvent[event_id].state != GAMEEVENT_INTERNAL)
                {
                    mGameEvent[event_id].state = (GameEventState)(fields[1].GetUInt8());
                    mGameEvent[event_id].nextstart    = time_t(fields[2].GetUInt32());
                }
                else
                {
                    TC_LOG_ERROR("sql.sql", "game_event_save includes event save for non-worldevent id {}.", event_id);
                    continue;
                }

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} game event saves in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));

        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Prerequisite Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                   0             1
        QueryResult result = WorldDatabase.Query("SELECT eventEntry, prerequisite_event FROM game_event_prerequisite");
        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 game event prerequisites in game events. DB table `game_event_prerequisite` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint16 event_id = fields[0].GetUInt8();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_prerequisite`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                if (mGameEvent[event_id].state != GAMEEVENT_NORMAL && mGameEvent[event_id].state != GAMEEVENT_INTERNAL)
                {
                    uint16 prerequisite_event = fields[1].GetUInt32();
                    if (prerequisite_event >= mGameEvent.size())
                    {
                        TC_LOG_ERROR("sql.sql", "`game_event_prerequisite`: game event prerequisite id ({}) is out of range compared to max event id in `game_event`.", prerequisite_event);
                        continue;
                    }
                    mGameEvent[event_id].prerequisite_events.insert(prerequisite_event);
                }
                else
                {
                    TC_LOG_ERROR("sql.sql", "game_event_prerequisiste includes event entry for non-worldevent id {}.", event_id);
                    continue;
                }

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} game event prerequisites in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));

        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Creature Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 0        1
        QueryResult result = WorldDatabase.Query("SELECT guid, eventEntry FROM game_event_creature");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 creatures in game events. DB table `game_event_creature` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid = fields[0].GetUInt32();
                int16 event_id = fields[1].GetInt8();

                int32 internal_event_id = mGameEvent.size() + event_id - 1;

                CreatureData const* data = sObjectMgr->GetCreatureData(guid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_creature` contains creature (GUID: {}) not found in `creature` table.", guid);
                    continue;
                }

                if (internal_event_id < 0 || internal_event_id >= int32(mGameEventCreatureGuids.size()))
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_creature`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                // Log error for pooled object, but still spawn it
                if (uint32 poolId = sPoolMgr->IsPartOfAPool(SPAWN_TYPE_CREATURE, guid))
                    TC_LOG_ERROR("sql.sql", "`game_event_creature`: game event id ({}) contains creature ({}) which is part of a pool ({}). This should be spawned in game_event_pool", event_id, guid, poolId);

                GuidList& crelist = mGameEventCreatureGuids[internal_event_id];
                crelist.push_back(guid);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} creatures in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));

        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event GO Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0         1
        QueryResult result = WorldDatabase.Query("SELECT guid, eventEntry FROM game_event_gameobject");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 gameobjects in game events. DB table `game_event_gameobject` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid = fields[0].GetUInt32();
                int16 event_id = fields[1].GetInt8();

                int32 internal_event_id = mGameEvent.size() + event_id - 1;

                GameObjectData const* data = sObjectMgr->GetGameObjectData(guid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_gameobject` contains gameobject (GUID: {}) not found in `gameobject` table.", guid);
                    continue;
                }

                if (internal_event_id < 0 || internal_event_id >= int32(mGameEventGameobjectGuids.size()))
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_gameobject`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                // Log error for pooled object, but still spawn it
                if (uint32 poolId = sPoolMgr->IsPartOfAPool(SPAWN_TYPE_GAMEOBJECT, guid))
                    TC_LOG_ERROR("sql.sql", "`game_event_gameobject`: game event id ({}) contains game object ({}) which is part of a pool ({}). This should be spawned in game_event_pool", event_id, guid, poolId);

                GuidList& golist = mGameEventGameobjectGuids[internal_event_id];
                golist.push_back(guid);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} gameobjects in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Model/Equipment Change Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                       0           1                       2                                 3                                     4
        QueryResult result = WorldDatabase.Query("SELECT creature.guid, creature.id, game_event_model_equip.eventEntry, game_event_model_equip.modelid, game_event_model_equip.equipment_id "
                                                 "FROM creature JOIN game_event_model_equip ON creature.guid = game_event_model_equip.guid");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 model/equipment changes in game events. DB table `game_event_model_equip` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid = fields[0].GetUInt32();
                uint32 entry    = fields[1].GetUInt32();
                uint16 event_id = fields[2].GetUInt8();

                if (event_id >= mGameEventModelEquip.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_model_equip`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                ModelEquipList& equiplist = mGameEventModelEquip[event_id];
                ModelEquip newModelEquipSet;
                newModelEquipSet.modelid = fields[3].GetUInt32();
                newModelEquipSet.equipment_id = fields[4].GetUInt8();
                newModelEquipSet.equipement_id_prev = 0;
                newModelEquipSet.modelid_prev = 0;

                if (newModelEquipSet.equipment_id > 0)
                {
                    int8 equipId = static_cast<int8>(newModelEquipSet.equipment_id);
                    if (!sObjectMgr->GetEquipmentInfo(entry, equipId))
                    {
                        TC_LOG_ERROR("sql.sql", "Table `game_event_model_equip` contains creature (Guid: {}, entry: {}) with equipment_id {} not found in table `creature_equip_template`. Setting entry to no equipment.",
                                         guid, entry, newModelEquipSet.equipment_id);
                        continue;
                    }
                }

                equiplist.push_back(std::pair<uint32, ModelEquip>(guid, newModelEquipSet));

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} model/equipment changes in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Quest Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                               0     1      2
        QueryResult result = WorldDatabase.Query("SELECT id, quest, eventEntry FROM game_event_creature_quest");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 quests additions in game events. DB table `game_event_creature_quest` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 id       = fields[0].GetUInt32();
                uint32 quest    = fields[1].GetUInt32();
                uint16 event_id = fields[2].GetUInt8();

                if (event_id >= mGameEventCreatureQuests.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_creature_quest`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                QuestRelList& questlist = mGameEventCreatureQuests[event_id];
                questlist.push_back(QuestRelation(id, quest));

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} quests additions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event GO Quest Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                               0     1      2
        QueryResult result = WorldDatabase.Query("SELECT id, quest, eventEntry FROM game_event_gameobject_quest");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 go quests additions in game events. DB table `game_event_gameobject_quest` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 id       = fields[0].GetUInt32();
                uint32 quest    = fields[1].GetUInt32();
                uint16 event_id = fields[2].GetUInt8();

                if (event_id >= mGameEventGameObjectQuests.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_gameobject_quest`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                QuestRelList& questlist = mGameEventGameObjectQuests[event_id];
                questlist.push_back(QuestRelation(id, quest));

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} quests additions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Quest Condition Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 0       1         2             3
        QueryResult result = WorldDatabase.Query("SELECT quest, eventEntry, condition_id, num FROM game_event_quest_condition");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 quest event conditions in game events. DB table `game_event_quest_condition` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 quest     = fields[0].GetUInt32();
                uint16 event_id  = fields[1].GetUInt8();
                uint32 condition = fields[2].GetUInt32();
                float num       = fields[3].GetFloat();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_quest_condition`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                mQuestToEventConditions[quest].event_id = event_id;
                mQuestToEventConditions[quest].condition = condition;
                mQuestToEventConditions[quest].num = num;

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} quest event conditions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Condition Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                  0          1            2             3                      4
        QueryResult result = WorldDatabase.Query("SELECT eventEntry, condition_id, req_num, max_world_state_field, done_world_state_field FROM game_event_condition");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 conditions in game events. DB table `game_event_condition` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint16 event_id  = fields[0].GetUInt8();
                uint32 condition = fields[1].GetUInt32();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_condition`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                mGameEvent[event_id].conditions[condition].reqNum = fields[2].GetFloat();
                mGameEvent[event_id].conditions[condition].done = 0;
                mGameEvent[event_id].conditions[condition].max_world_state = fields[3].GetUInt16();
                mGameEvent[event_id].conditions[condition].done_world_state = fields[4].GetUInt16();

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} conditions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Condition Save Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                      0           1         2
        QueryResult result = CharacterDatabase.Query("SELECT eventEntry, condition_id, done FROM game_event_condition_save");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 condition saves in game events. DB table `game_event_condition_save` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint16 event_id  = fields[0].GetUInt8();
                uint32 condition = fields[1].GetUInt32();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_condition_save`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                GameEventConditionMap::iterator itr = mGameEvent[event_id].conditions.find(condition);
                if (itr != mGameEvent[event_id].conditions.end())
                {
                    itr->second.done = fields[2].GetFloat();
                }
                else
                {
                    TC_LOG_ERROR("sql.sql", "game_event_condition_save contains not present condition event id {} condition id {}.", event_id, condition);
                    continue;
                }

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} condition saves in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event NPCflag Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0       1        2
        QueryResult result = WorldDatabase.Query("SELECT guid, eventEntry, npcflag FROM game_event_npcflag");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 npcflags in game events. DB table `game_event_npcflag` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid = fields[0].GetUInt32();
                uint16 event_id = fields[1].GetUInt8();
                uint32 npcflag  = fields[2].GetUInt32();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_npcflag`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                mGameEventNPCFlags[event_id].push_back(GuidNPCFlagPair(guid, npcflag));

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} npcflags in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Seasonal Quest Relations...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                  0          1
        QueryResult result = WorldDatabase.Query("SELECT questId, eventEntry FROM game_event_seasonal_questrelation");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 seasonal quests additions in game events. DB table `game_event_seasonal_questrelation` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 questId  = fields[0].GetUInt32();
                uint32 eventEntry = fields[1].GetUInt32(); /// @todo Change to uint8

                Quest* questTemplate = const_cast<Quest*>(sObjectMgr->GetQuestTemplate(questId));
                if (!questTemplate)
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_seasonal_questrelation`: quest id ({}) does not exist in `quest_template`.", questId);
                    continue;
                }

                if (eventEntry >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_seasonal_questrelation`: event id ({}) is out of range compared to max event in `game_event`.", eventEntry);
                    continue;
                }

                questTemplate->SetEventIdForQuest(static_cast<uint16>(eventEntry));
                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} quests additions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Vendor Additions Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                               0           1     2     3         4         5
        QueryResult result = WorldDatabase.Query("SELECT eventEntry, guid, item, maxcount, incrtime, ExtendedCost FROM game_event_npc_vendor ORDER BY guid, slot ASC");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 vendor additions in game events. DB table `game_event_npc_vendor` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint8 event_id  = fields[0].GetUInt8();

                if (event_id >= mGameEventVendors.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_npc_vendor`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                NPCVendorList& vendors = mGameEventVendors[event_id];
                NPCVendorEntry newEntry;
                ObjectGuid::LowType guid = fields[1].GetUInt32();
                newEntry.item = fields[2].GetUInt32();
                newEntry.maxcount = fields[3].GetUInt32();
                newEntry.incrtime = fields[4].GetUInt32();
                newEntry.ExtendedCost = fields[5].GetUInt32();
                // get the event npc flag for checking if the npc will be vendor during the event or not
                uint32 event_npc_flag = 0;
                NPCFlagList& flist = mGameEventNPCFlags[event_id];
                for (NPCFlagList::const_iterator itr = flist.begin(); itr != flist.end(); ++itr)
                {
                    if (itr->first == guid)
                    {
                        event_npc_flag = itr->second;
                        break;
                    }
                }
                // get creature entry
                newEntry.entry = 0;

                if (CreatureData const* data = sObjectMgr->GetCreatureData(guid))
                    newEntry.entry = data->id;

                // check validity with event's npcflag
                if (!sObjectMgr->IsVendorItemValid(newEntry.entry, newEntry.item, newEntry.maxcount, newEntry.incrtime, newEntry.ExtendedCost, nullptr, nullptr, event_npc_flag))
                    continue;

                vendors.push_back(newEntry);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} vendor additions in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Battleground Holiday Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                               0           1
        QueryResult result = WorldDatabase.Query("SELECT EventEntry, BattlegroundID FROM game_event_battleground_holiday");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 battleground holidays in game events. DB table `game_event_battleground_holiday` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint16 event_id = fields[0].GetUInt8();

                if (event_id >= mGameEvent.size())
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_battleground_holiday`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                mGameEventBattlegroundHolidays[event_id] = fields[1].GetUInt32();

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} battleground holidays in game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    TC_LOG_INFO("server.loading", "Loading Game Event Pool Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                               0                         1
        QueryResult result = WorldDatabase.Query("SELECT pool_template.entry, game_event_pool.eventEntry FROM pool_template"
                                                 " JOIN game_event_pool ON pool_template.entry = game_event_pool.pool_entry");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 pools for game events. DB table `game_event_pool` is empty.");
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 entry   = fields[0].GetUInt32();
                int16 event_id = fields[1].GetInt8();

                int32 internal_event_id = mGameEvent.size() + event_id - 1;

                if (internal_event_id < 0 || internal_event_id >= int32(mGameEventPoolIds.size()))
                {
                    TC_LOG_ERROR("sql.sql", "`game_event_pool`: game event id ({}) is out of range compared to max event id in `game_event`.", event_id);
                    continue;
                }

                if (!sPoolMgr->CheckPool(entry))
                {
                    TC_LOG_ERROR("sql.sql", "Pool Id ({}) has all creatures or gameobjects with explicit chance sum <> 100 and no equal chance defined. The pool system cannot pick one to spawn.", entry);
                    continue;
                }

                IdList& poollist = mGameEventPoolIds[internal_event_id];
                poollist.push_back(entry);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} pools for game events in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }
}

void GameEventMgr::LoadHolidayDates()
{
    uint32 oldMSTime = getMSTime();

    //                                               0   1        2           3
    QueryResult result = WorldDatabase.Query("SELECT id, date_id, date_value, holiday_duration FROM holiday_dates");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 holiday dates. DB table `holiday_dates` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 holidayId = fields[0].GetUInt32();
        HolidaysEntry* entry = const_cast<HolidaysEntry*>(sHolidaysStore.LookupEntry(holidayId));
        if (!entry)
        {
            TC_LOG_ERROR("sql.sql", "holiday_dates entry has invalid holiday id {}.", holidayId);
            continue;
        }

        uint8 dateId = fields[1].GetUInt8();
        if (dateId >= MAX_HOLIDAY_DATES)
        {
            TC_LOG_ERROR("sql.sql", "holiday_dates entry has out of range date_id {}.", dateId);
            continue;
        }

        entry->Date[dateId] = fields[2].GetUInt32();

        if (uint32 duration = fields[3].GetUInt32())
            entry->Duration[0] = duration;

        auto itr = std::lower_bound(modifiedHolidays.begin(), modifiedHolidays.end(), entry->ID);
        if (itr == modifiedHolidays.end() || *itr != entry->ID)
            modifiedHolidays.insert(itr, entry->ID);
        ++count;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} holiday dates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取生物的NPC标志
 * @param cr 生物对象指针
 * @return NPC标志位掩码
 *
 * 根据活动事件计算生物应该显示的NPC标志。
 * 遍历所有活动事件，累积该生物的事件相关NPC标志。
 */
uint32 GameEventMgr::GetNPCFlag(Creature* cr)
{
    uint32 mask = 0;
    ObjectGuid::LowType guid = cr->GetSpawnId();

    // 遍历所有活动事件
    for (ActiveEvents::iterator e_itr = m_ActiveEvents.begin(); e_itr != m_ActiveEvents.end(); ++e_itr)
    {
        // 检查该事件是否有此生物的NPC标志变更
        for (NPCFlagList::iterator itr = mGameEventNPCFlags[*e_itr].begin();
            itr != mGameEventNPCFlags[*e_itr].end();
            ++ itr)
            if (itr->first == guid)
                mask |= itr->second;
    }

    return mask;
}

/**
 * @brief 初始化游戏事件管理器
 *
 * 重置所有事件状态，清空活动事件列表。
 * 查询数据库获取最大事件ID，并调整容器大小。
 * 调用时机：世界服务器启动时
 */
void GameEventMgr::Initialize()
{
    QueryResult result = WorldDatabase.Query("SELECT MAX(eventEntry) FROM game_event");
    if (result)
    {
        Field* fields = result->Fetch();

        uint32 maxEventId = fields[0].GetUInt8();

        // ID从1开始，vector从0开始，因此需要增加
        maxEventId++;

        // 调整各容器大小，* 2 - 1是为了支持负数事件ID（用于移除生物/对象）
        mGameEvent.resize(maxEventId);
        mGameEventCreatureGuids.resize(maxEventId * 2 - 1);
        mGameEventGameobjectGuids.resize(maxEventId * 2 - 1);
        mGameEventCreatureQuests.resize(maxEventId);
        mGameEventGameObjectQuests.resize(maxEventId);
        mGameEventVendors.resize(maxEventId);
        mGameEventBattlegroundHolidays.resize(maxEventId, 0);
        mGameEventPoolIds.resize(maxEventId * 2 - 1);
        mGameEventNPCFlags.resize(maxEventId);
        mGameEventModelEquip.resize(maxEventId);
    }
}

/**
 * @brief 启动系统事件
 * @return 下次更新时间（秒）
 *
 * 初始化并启动所有应该活动的事件。
 * 清空活动事件列表，执行更新，设置系统初始化标志。
 */
uint32 GameEventMgr::StartSystem()
{
    m_ActiveEvents.clear();
    uint32 delay = Update();
    isSystemInit = true;
    return delay;
}

/**
 * @brief 启动竞技场赛季事件
 *
 * 根据数据库配置启动当前竞技场赛季对应的事件。
 * 从配置中读取当前赛季ID，查找对应的事件并启动。
 */
void GameEventMgr::StartArenaSeason()
{
    uint8 season = sWorld->getIntConfig(CONFIG_ARENA_SEASON_ID);
    QueryResult result = WorldDatabase.PQuery("SELECT eventEntry FROM game_event_arena_seasons WHERE season = '{}'", season);

    if (!result)
    {
        TC_LOG_ERROR("gameevent", "ArenaSeason ({}) must be an existing Arena Season.", season);
        return;
    }

    Field* fields = result->Fetch();
    uint16 eventId = fields[0].GetUInt8();

    if (eventId >= mGameEvent.size())
    {
        TC_LOG_ERROR("gameevent", "EventEntry {} for ArenaSeason ({}) does not exist.", eventId, season);
        return;
    }

    StartEvent(eventId, true);
    TC_LOG_INFO("gameevent", "Arena Season {} started...", season);

}

/**
 * @brief 更新所有游戏事件
 * @return 距离下次需要更新的时间（秒）
 *
 * 检查所有事件的状态，启动应该启动的事件，停止应该停止的事件。
 * 调用时机：世界服务器每次更新循环中。
 * 性能注意：会遍历所有事件，但不频繁调用。
 *
 * 处理流程：
 * 1. 遍历所有事件，检查每个事件是否应该活动
 * 2. 对于世界事件，检查是否需要更新状态
 * 3. 收集需要激活和停用的事件列表
 * 4. 先激活事件，再停用事件（避免客户端闪烁）
 * 5. 计算下次更新的延迟时间
 */
uint32 GameEventMgr::Update()
{
    time_t currenttime = GameTime::GetGameTime();
    uint32 nextEventDelay = max_ge_check_delay;             // 默认1天
    uint32 calcDelay;
    std::set<uint16> activate, deactivate;
    for (uint16 itr = 1; itr < mGameEvent.size(); ++itr)
    {
        // 必须先处理激活，再处理停用
        // 所以先排队
        if (CheckOneGameEvent(itr))
        {
            // 如果世界事件在NEXTPHASE状态，并且时间已过，则完成此事件
            if (mGameEvent[itr].state == GAMEEVENT_WORLD_NEXTPHASE && mGameEvent[itr].nextstart <= currenttime)
            {
                // 设置此事件为完成状态，清空nextstart时间
                mGameEvent[itr].state = GAMEEVENT_WORLD_FINISHED;
                mGameEvent[itr].nextstart = 0;
                // 保存此游戏事件的状态
                SaveWorldEventStateToDB(itr);
                // 加入停用队列
                if (IsActiveEvent(itr))
                    deactivate.insert(itr);
                // 跳到下一个事件，此事件不再需要事件更新定时器
                continue;
            }
            else if (mGameEvent[itr].state == GAMEEVENT_WORLD_CONDITIONS && CheckOneGameEventConditions(itr))
                // 状态已改变，保存到数据库，将在下次更新周期中更新
                SaveWorldEventStateToDB(itr);

            // 加入激活队列
            if (!IsActiveEvent(itr))
                activate.insert(itr);
        }
        else
        {
            // 如果事件非活动，定期清理其世界状态
            sWorld->setWorldState(itr, 0);
            if (IsActiveEvent(itr))
                deactivate.insert(itr);
            else
            {
                // 系统初始化时，生成负ID的生物/对象（移除列表）
                if (!isSystemInit)
                {
                    int16 event_nid = (-1) * (itr);
                    // 为此事件生成所有负ID的生物/对象
                    GameEventSpawn(event_nid);
                }
            }
        }
        // 计算此事件的下次检查延迟
        calcDelay = NextCheck(itr);
        if (calcDelay < nextEventDelay)
            nextEventDelay = calcDelay;
    }
    // 现在激活队列
    // 一个现在激活的事件可能包含一个将要停用的事件的生物生成
    // 按照激活-停用顺序，稍后停用第一个事件将保留生成（不会在客户端消失然后重新出现）
    for (std::set<uint16>::iterator itr = activate.begin(); itr != activate.end(); ++itr)
        // 启动事件
        // 如果启动的事件立即完成，返回true
        // 在这种情况下，在1秒后发起下次更新
        if (StartEvent(*itr))
            nextEventDelay = 0;
    // 停用队列
    for (std::set<uint16>::iterator itr = deactivate.begin(); itr != deactivate.end(); ++itr)
        StopEvent(*itr);
    TC_LOG_INFO("gameevent", "Next game event check in {} seconds.", nextEventDelay + 1);
    return (nextEventDelay + 1) * IN_MILLISECONDS;           // 加1秒确保下次调用时事件已启动/停止
}

/**
 * @brief 取消应用事件
 * @param event_id 事件ID
 *
 * 执行事件停止时的所有清理操作：
 * 1. 运行SmartAI脚本（GAME_EVENT_END）
 * 2. 移除事件关联的生物和游戏对象
 * 3. 恢复负ID事件关联的生物和游戏对象
 * 4. 恢复原始装备和模型
 * 5. 移除事件任务
 * 6. 更新世界状态
 * 7. 恢复NPC标志
 * 8. 移除商人物品
 * 9. 更新战场设置
 */
void GameEventMgr::UnApplyEvent(uint16 event_id)
{
    TC_LOG_INFO("gameevent", "GameEvent {} \"{}\" removed.", event_id, mGameEvent[event_id].description);
    //! 运行SmartAI脚本，触发SMART_EVENT_GAME_EVENT_END事件
    RunSmartAIScripts(event_id, false);
    // 移除正ID事件标记的对象
    GameEventUnspawn(event_id);
    // 生成负ID事件标记的对象
    int16 event_nid = (-1) * event_id;
    GameEventSpawn(event_nid);
    // 恢复装备或模型
    ChangeEquipOrModel(event_id, false);
    // 从非事件NPC移除仅限事件的任务
    UpdateEventQuests(event_id, false);
    // 更新世界状态
    UpdateWorldStates(event_id, false);
    // 更新此事件中的NPC标志
    UpdateEventNPCFlags(event_id);
    // 移除商人物品
    UpdateEventNPCVendor(event_id, false);
    // 更新战场假日设置
    UpdateBattlegroundSettings();
}

/**
 * @brief 应用新事件
 * @param event_id 事件ID
 *
 * 执行事件启动时的所有操作：
 * 1. 发送事件公告（如果配置允许）
 * 2. 生成事件关联的生物和游戏对象
 * 3. 移除负ID事件关联的生物和游戏对象
 * 4. 更改装备和模型
 * 5. 添加事件任务
 * 6. 更新世界状态
 * 7. 更新NPC标志
 * 8. 添加商人物品
 * 9. 更新战场设置
 * 10. 运行SmartAI脚本（GAME_EVENT_START）
 * 11. 重置季节性任务（如果是首次启动）
 */
void GameEventMgr::ApplyNewEvent(uint16 event_id)
{
    // 检查是否需要发送公告
    uint8 announce = mGameEvent[event_id].announce;
    if (announce == 1 || (announce == 2 && sWorld->getBoolConfig(CONFIG_EVENT_ANNOUNCE)))
        sWorld->SendWorldText(LANG_EVENTMESSAGE, mGameEvent[event_id].description.c_str());

    TC_LOG_INFO("gameevent", "GameEvent {} \"{}\" started.", event_id, mGameEvent[event_id].description);

    // 生成正ID事件标记的对象
    GameEventSpawn(event_id);
    // 移除负ID事件标记的对象
    int16 event_nid = (-1) * event_id;
    GameEventUnspawn(event_nid);
    // 更改装备或模型
    ChangeEquipOrModel(event_id, true);
    // 向非事件NPC添加仅限事件的任务
    UpdateEventQuests(event_id, true);
    // 更新世界状态
    UpdateWorldStates(event_id, true);
    // 更新此事件中的NPC标志
    UpdateEventNPCFlags(event_id);
    // 添加商人物品
    UpdateEventNPCVendor(event_id, true);
    // 更新战场假日设置
    UpdateBattlegroundSettings();

    //! 运行SmartAI脚本，触发SMART_EVENT_GAME_EVENT_START事件
    RunSmartAIScripts(event_id, true);

    // 如果事件的世界状态为0，表示事件尚未启动过。在这种情况下，重置季节性任务。
    // 当事件结束（过期或通过命令停止）时，世界状态将再次设置为0，准备进行下一次季节性任务重置。
    if (sWorld->getWorldState(event_id) == 0)
        sWorld->ResetEventSeasonalQuests(event_id);
}

/**
 * @brief 更新事件NPC标志
 * @param event_id 事件ID
 *
 * 更新事件关联NPC的交互标志。
 * 通知地图上的所有玩家更新NPC标志。
 */
void GameEventMgr::UpdateEventNPCFlags(uint16 event_id)
{
    std::unordered_map<uint32, std::unordered_set<ObjectGuid::LowType>> creaturesByMap;

    // go through the creatures whose npcflags are changed in the event
    for (NPCFlagList::iterator itr = mGameEventNPCFlags[event_id].begin(); itr != mGameEventNPCFlags[event_id].end(); ++itr)
        // get the creature data from the low guid to get the entry, to be able to find out the whole guid
        if (CreatureData const* data = sObjectMgr->GetCreatureData(itr->first))
            creaturesByMap[data->mapId].insert(itr->first);

    for (auto const& p : creaturesByMap)
    {
        sMapMgr->DoForAllMapsWithMapId(p.first, [this, &p](Map* map)
        {
            for (auto& spawnId : p.second)
            {
                auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
                for (auto itr = creatureBounds.first; itr != creatureBounds.second; ++itr)
                {
                    Creature* creature = itr->second;
                    uint32 npcflag = GetNPCFlag(creature);
                    if (CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate())
                        npcflag |= creatureTemplate->npcflag;

                    creature->ReplaceAllNpcFlags(NPCFlags(npcflag));
                    // reset gossip options, since the flag change might have added / removed some
                    //cr->ResetGossipOptions();
                }
            }
        });
    }
}

/**
 * @brief 更新战场设置
 *
 * 根据活动的节日事件更新战场假日设置。
 * 重置所有假日状态，然后根据当前活动事件重新设置。
 */
void GameEventMgr::UpdateBattlegroundSettings()
{
    sBattlegroundMgr->ResetHolidays();

    // 为每个活动事件设置战场假日
    for (uint16 activeEventId : m_ActiveEvents)
        sBattlegroundMgr->SetHolidayActive(mGameEventBattlegroundHolidays[activeEventId]);
}

/**
 * @brief 更新事件NPC商人
 * @param event_id 事件ID
 * @param activate true表示激活，false表示停用
 *
 * 添加或移除事件期间NPC出售的物品。
 */
void GameEventMgr::UpdateEventNPCVendor(uint16 event_id, bool activate)
{
    for (NPCVendorList::iterator itr = mGameEventVendors[event_id].begin(); itr != mGameEventVendors[event_id].end(); ++itr)
    {
        if (activate)
            // 添加商人物品
            sObjectMgr->AddVendorItem(itr->entry, itr->item, itr->maxcount, itr->incrtime, itr->ExtendedCost, false);
        else
            // 移除商人物品
            sObjectMgr->RemoveVendorItem(itr->entry, itr->item, false);
    }
}

/**
 * @brief 生成事件生物和游戏对象
 * @param event_id 事件ID（可以为负数，表示移除列表）
 *
 * 生成事件关联的所有生物和游戏对象。
 * 对于已加载的地图格子，立即生成实体；
 * 对于未加载的格子，添加到格子数据中等待加载时生成。
 *
 * 处理流程：
 * 1. 计算内部事件ID（支持负ID）
 * 2. 遍历所有关联的生物GUID
 * 3. 将生物添加到格子并生成（如果格子已加载）
 * 4. 遍历所有关联的游戏对象GUID
 * 5. 将游戏对象添加到格子并生成（如果格子已加载）
 */
void GameEventMgr::GameEventSpawn(int16 event_id)
{
    // 计算内部事件ID，支持负ID
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    // 边界检查
    if (internal_event_id < 0 || internal_event_id >= int32(mGameEventCreatureGuids.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventSpawn attempted access to out of range mGameEventCreatureGuids element {} (size: {}).",
            internal_event_id, mGameEventCreatureGuids.size());
        return;
    }

    // 遍历所有关联的生物GUID
    for (GuidList::iterator itr = mGameEventCreatureGuids[internal_event_id].begin(); itr != mGameEventCreatureGuids[internal_event_id].end(); ++itr)
    {
        // 添加到正确的格子
        if (CreatureData const* data = sObjectMgr->GetCreatureData(*itr))
        {
            sObjectMgr->AddCreatureToGrid(*itr, data);

            // 如果需要则生成（仅已加载的格子）
            Map* map = sMapMgr->CreateBaseMap(data->mapId);
            map->RemoveRespawnTime(SPAWN_TYPE_CREATURE, *itr);
            // 使用生成坐标生成
            if (!map->Instanceable() && map->IsGridLoaded(data->spawnPoint))
            {
                Creature* creature = new Creature();
                if (!creature->LoadFromDB(*itr, map, true, false))
                    delete creature;
            }
        }
    }

    // 边界检查
    if (internal_event_id >= int32(mGameEventGameobjectGuids.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventSpawn attempted access to out of range mGameEventGameobjectGuids element {} (size: {}).",
            internal_event_id, mGameEventGameobjectGuids.size());
        return;
    }

    // 遍历所有关联的游戏对象GUID
    for (GuidList::iterator itr = mGameEventGameobjectGuids[internal_event_id].begin(); itr != mGameEventGameobjectGuids[internal_event_id].end(); ++itr)
    {
        // 添加到正确的格子
        if (GameObjectData const* data = sObjectMgr->GetGameObjectData(*itr))
        {
            sObjectMgr->AddGameobjectToGrid(*itr, data);
            // 如果需要则生成（仅已加载的格子）
            // 此基础地图检查为非实例化且仅存在的
            Map* map = sMapMgr->CreateBaseMap(data->mapId);
            map->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, *itr);
            // 使用当前坐标取消生成，而不是生成坐标，因为生物可能已更改格子
            if (!map->Instanceable() && map->IsGridLoaded(data->spawnPoint))
            {
                GameObject* pGameobject = new GameObject;
                /// @todo 找出何时添加到地图
                if (!pGameobject->LoadFromDB(*itr, map, false))
                    delete pGameobject;
                else
                {
                    if (pGameobject->isSpawnedByDefault())
                        map->AddToMap(pGameobject);
                }
            }
        }
    }

    if (internal_event_id >= int32(mGameEventPoolIds.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventSpawn attempted access to out of range mGameEventPoolIds element {} (size: {}).",
            internal_event_id, mGameEventPoolIds.size());
        return;
    }

    for (IdList::iterator itr = mGameEventPoolIds[internal_event_id].begin(); itr != mGameEventPoolIds[internal_event_id].end(); ++itr)
        sPoolMgr->SpawnPool(*itr);
}

/**
 * @brief 移除事件生物和游戏对象
 * @param event_id 事件ID（可以为负数，表示移除列表）
 *
 * 移除事件关联的所有生物和游戏对象。
 * 如果生物/游戏对象被其他活动事件使用，则不会移除。
 *
 * 处理流程：
 * 1. 计算内部事件ID（支持负ID）
 * 2. 遍历所有关联的生物GUID
 * 3. 检查是否被其他事件使用，如果是则跳过
 * 4. 从格子移除并删除所有地图上的生物实例
 * 5. 遍历所有关联的游戏对象GUID
 * 6. 同样处理游戏对象的移除
 * 7. 处理池的取消生成
 */
void GameEventMgr::GameEventUnspawn(int16 event_id)
{
    // 计算内部事件ID，支持负ID
    int32 internal_event_id = mGameEvent.size() + event_id - 1;

    // 边界检查
    if (internal_event_id < 0 || internal_event_id >= int32(mGameEventCreatureGuids.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventUnspawn attempted access to out of range mGameEventCreatureGuids element {} (size: {}).",
            internal_event_id, mGameEventCreatureGuids.size());
        return;
    }

    // 遍历所有关联的生物GUID
    for (GuidList::iterator itr = mGameEventCreatureGuids[internal_event_id].begin(); itr != mGameEventCreatureGuids[internal_event_id].end(); ++itr)
    {
        // 检查是否被其他事件需要，如果是，不移除
        if (event_id > 0 && hasCreatureActiveEventExcept(*itr, event_id))
            continue;
        // 从格子移除生物
        if (CreatureData const* data = sObjectMgr->GetCreatureData(*itr))
        {
            sObjectMgr->RemoveCreatureFromGrid(*itr, data);

            // 从所有地图移除生物
            sMapMgr->DoForAllMapsWithMapId(data->mapId, [&itr](Map* map)
            {
                map->RemoveRespawnTime(SPAWN_TYPE_CREATURE, *itr);
                auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(*itr);
                for (auto itr2 = creatureBounds.first; itr2 != creatureBounds.second;)
                {
                    Creature* creature = itr2->second;
                    ++itr2;
                    creature->AddObjectToRemoveList();
                }
            });
        }
    }

    // 边界检查
    if (internal_event_id < 0 || internal_event_id >= int32(mGameEventGameobjectGuids.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventUnspawn attempted access to out of range mGameEventGameobjectGuids element {} (size: {}).",
            internal_event_id, mGameEventGameobjectGuids.size());
        return;
    }

    // 遍历所有关联的游戏对象GUID
    for (GuidList::iterator itr = mGameEventGameobjectGuids[internal_event_id].begin(); itr != mGameEventGameobjectGuids[internal_event_id].end(); ++itr)
    {
        // 检查是否被其他事件需要，如果是，不移除
        if (event_id >0 && hasGameObjectActiveEventExcept(*itr, event_id))
            continue;
        // 从格子移除游戏对象
        if (GameObjectData const* data = sObjectMgr->GetGameObjectData(*itr))
        {
            sObjectMgr->RemoveGameobjectFromGrid(*itr, data);

            // 从所有地图移除游戏对象
            sMapMgr->DoForAllMapsWithMapId(data->mapId, [&itr](Map* map)
            {
                map->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, *itr);
                auto gameobjectBounds = map->GetGameObjectBySpawnIdStore().equal_range(*itr);
                for (auto itr2 = gameobjectBounds.first; itr2 != gameobjectBounds.second;)
                {
                    GameObject* go = itr2->second;
                    ++itr2;
                    go->AddObjectToRemoveList();
                }
            });
        }
    }
    // 边界检查
    if (internal_event_id < 0 || internal_event_id >= int32(mGameEventPoolIds.size()))
    {
        TC_LOG_ERROR("gameevent", "GameEventMgr::GameEventUnspawn attempted access to out of range mGameEventPoolIds element {} (size: {}).", internal_event_id, mGameEventPoolIds.size());
        return;
    }

    // 取消生成池
    for (IdList::iterator itr = mGameEventPoolIds[internal_event_id].begin(); itr != mGameEventPoolIds[internal_event_id].end(); ++itr)
    {
        sPoolMgr->DespawnPool(*itr, true);
    }
}

/**
 * @brief 更改装备或模型
 * @param event_id 事件ID
 * @param activate true表示激活事件，false表示停止事件
 *
 * 切换事件关联生物的模型和装备。
 * 当activate=true时，应用事件的模型和装备；
 * 当activate=false时，恢复原始模型和装备。
 */
void GameEventMgr::ChangeEquipOrModel(int16 event_id, bool activate)
{
    for (ModelEquipList::iterator itr = mGameEventModelEquip[event_id].begin(); itr != mGameEventModelEquip[event_id].end(); ++itr)
    {
        // 从格子移除生物
        CreatureData const* data = sObjectMgr->GetCreatureData(itr->first);
        if (!data)
            continue;

        // 如果已生成则更新
        sMapMgr->DoForAllMapsWithMapId(data->mapId, [&itr, activate](Map* map)

        {
            auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(itr->first);
            for (auto itr2 = creatureBounds.first; itr2 != creatureBounds.second; ++itr2)
            {
                Creature* creature = itr2->second;
                if (activate)
                {
                    // 保存原始装备ID和模型ID
                    itr->second.equipement_id_prev = creature->GetCurrentEquipmentId();
                    itr->second.modelid_prev = creature->GetDisplayId();
                    creature->LoadEquipment(itr->second.equipment_id, true);
                    if (itr->second.modelid > 0 && itr->second.modelid_prev != itr->second.modelid &&
                        sObjectMgr->GetCreatureModelInfo(itr->second.modelid))
                    {
                        creature->SetDisplayId(itr->second.modelid);
                        creature->SetNativeDisplayId(itr->second.modelid);
                    }
                }
                else
                {
                    creature->LoadEquipment(itr->second.equipement_id_prev, true);
                    if (itr->second.modelid_prev > 0 && itr->second.modelid_prev != itr->second.modelid &&
                        sObjectMgr->GetCreatureModelInfo(itr->second.modelid_prev))
                    {
                        creature->SetDisplayId(itr->second.modelid_prev);
                        creature->SetNativeDisplayId(itr->second.modelid_prev);
                    }
                }
            }
        });
        // now last step: put in data
        CreatureData& data2 = sObjectMgr->NewOrExistCreatureData(itr->first);
        if (activate)
        {
            itr->second.modelid_prev = data2.displayid;
            itr->second.equipement_id_prev = data2.equipmentId;
            data2.displayid = itr->second.modelid;
            data2.equipmentId = itr->second.equipment_id;
        }
        else
        {
            data2.displayid = itr->second.modelid_prev;
            data2.equipmentId = itr->second.equipement_id_prev;
        }
    }
}

/**
 * @brief 检查是否有其他活动事件也包含该生物任务
 * @param quest_id 任务ID
 * @param event_id 要排除的事件ID
 * @return 如果有其他活动事件包含此任务返回true
 *
 * 用于在停止事件时判断任务是否可以安全移除
 */
bool GameEventMgr::hasCreatureQuestActiveEventExcept(uint32 quest_id, uint16 event_id)
{
    for (ActiveEvents::iterator e_itr = m_ActiveEvents.begin(); e_itr != m_ActiveEvents.end(); ++e_itr)
    {
        if ((*e_itr) != event_id)
            for (QuestRelList::iterator itr = mGameEventCreatureQuests[*e_itr].begin();
                itr != mGameEventCreatureQuests[*e_itr].end();
                ++ itr)
                if (itr->second == quest_id)
                    return true;
    }
    return false;
}

/**
 * @brief 检查是否有其他活动事件也包含该游戏对象任务
 * @param quest_id 任务ID
 * @param event_id 要排除的事件ID
 * @return 如果有其他活动事件包含此任务返回true
 *
 * 用于在停止事件时判断任务是否可以安全移除
 */
bool GameEventMgr::hasGameObjectQuestActiveEventExcept(uint32 quest_id, uint16 event_id)
{
    for (ActiveEvents::iterator e_itr = m_ActiveEvents.begin(); e_itr != m_ActiveEvents.end(); ++e_itr)
    {
        if ((*e_itr) != event_id)
            for (QuestRelList::iterator itr = mGameEventGameObjectQuests[*e_itr].begin();
                itr != mGameEventGameObjectQuests[*e_itr].end();
                ++ itr)
                if (itr->second == quest_id)
                    return true;
    }
    return false;
}

/**
 * @brief 检查是否有其他活动事件也包含该生物
 * @param creature_id 生物GUID低32位
 * @param event_id 要排除的事件ID
 * @return 如果有其他活动事件包含此生物返回true
 *
 * 用于在停止事件时判断生物是否可以安全移除
 */
bool GameEventMgr::hasCreatureActiveEventExcept(ObjectGuid::LowType creature_id, uint16 event_id)
{
    for (ActiveEvents::iterator e_itr = m_ActiveEvents.begin(); e_itr != m_ActiveEvents.end(); ++e_itr)
    {
        if ((*e_itr) != event_id)
        {
            int32 internal_event_id = mGameEvent.size() + (*e_itr) - 1;
            for (GuidList::iterator itr = mGameEventCreatureGuids[internal_event_id].begin();
                itr != mGameEventCreatureGuids[internal_event_id].end();
                ++ itr)
                if (*itr == creature_id)
                    return true;
        }
    }
    return false;
}

/**
 * @brief 检查是否有其他活动事件也包含该游戏对象
 * @param go_id 游戏对象GUID低32位
 * @param event_id 要排除的事件ID
 * @return 如果有其他活动事件包含此游戏对象返回true
 *
 * 用于在停止事件时判断游戏对象是否可以安全移除
 */
bool GameEventMgr::hasGameObjectActiveEventExcept(ObjectGuid::LowType go_id, uint16 event_id)
{
    for (ActiveEvents::iterator e_itr = m_ActiveEvents.begin(); e_itr != m_ActiveEvents.end(); ++e_itr)
    {
        if ((*e_itr) != event_id)
        {
            int32 internal_event_id = mGameEvent.size() + (*e_itr) - 1;
            for (GuidList::iterator itr = mGameEventGameobjectGuids[internal_event_id].begin();
                itr != mGameEventGameobjectGuids[internal_event_id].end();
                ++ itr)
                if (*itr == go_id)
                    return true;
        }
    }
    return false;
}

/**
 * @brief 更新事件任务
 * @param event_id 事件ID
 * @param activate true表示激活任务，false表示停用任务
 *
 * 激活或停用事件关联的任务。
 * 当activate=true时，添加任务关系；
 * 当activate=false时，移除任务关系（如果没有其他活动事件使用）。
 */
void GameEventMgr::UpdateEventQuests(uint16 event_id, bool activate)
{
    QuestRelList::iterator itr;
    // 处理生物任务
    for (itr = mGameEventCreatureQuests[event_id].begin(); itr != mGameEventCreatureQuests[event_id].end(); ++itr)
    {
        QuestRelations* CreatureQuestMap = sObjectMgr->GetCreatureQuestRelationMapHACK();
        if (activate)                                           // 添加(id, quest)对到multimap
            CreatureQuestMap->insert(QuestRelations::value_type(itr->first, itr->second));
        else
        {
            // 如果没有其他活动事件使用此任务，则移除
            if (!hasCreatureQuestActiveEventExcept(itr->second, event_id))
            {
                // 从multimap移除(id, quest)对
                QuestRelations::iterator qitr = CreatureQuestMap->find(itr->first);
                if (qitr == CreatureQuestMap->end())
                    continue;
                QuestRelations::iterator lastElement = CreatureQuestMap->upper_bound(itr->first);
                for (; qitr != lastElement; ++qitr)
                {
                    if (qitr->second == itr->second)
                    {
                        CreatureQuestMap->erase(qitr);          // iterator is now no more valid
                        break;                                  // but we can exit loop since the element is found
                    }
                }
            }
        }
    }
    // 处理游戏对象任务
    for (itr = mGameEventGameObjectQuests[event_id].begin(); itr != mGameEventGameObjectQuests[event_id].end(); ++itr)
    {
        QuestRelations* GameObjectQuestMap = sObjectMgr->GetGOQuestRelationMapHACK();
        if (activate)                                           // 添加(id, quest)对到multimap
            GameObjectQuestMap->insert(QuestRelations::value_type(itr->first, itr->second));
        else
        {
            // 如果没有其他活动事件使用此任务，则移除
            if (!hasGameObjectQuestActiveEventExcept(itr->second, event_id))
            {
                // 从multimap移除(id, quest)对
                QuestRelations::iterator qitr = GameObjectQuestMap->find(itr->first);
                if (qitr == GameObjectQuestMap->end())
                    continue;
                QuestRelations::iterator lastElement = GameObjectQuestMap->upper_bound(itr->first);
                for (; qitr != lastElement; ++qitr)
                {
                    if (qitr->second == itr->second)
                    {
                        GameObjectQuestMap->erase(qitr);        // 迭代器现在不再有效
                        break;                                  // 但我们可以退出循环，因为元素已找到
                    }
                }
            }
        }
    }
}

/**
 * @brief 更新世界状态
 * @param event_id 事件ID
 * @param Activate true表示激活，false表示停用
 *
 * 更新事件相关的世界状态变量。
 * 如果事件关联节日，更新对应战场假日的世界状态。
 */
void GameEventMgr::UpdateWorldStates(uint16 event_id, bool Activate)
{
    GameEventData const& event = mGameEvent[event_id];
    // 如果事件关联节日，更新战场假日世界状态
    if (event.holiday_id != HOLIDAY_NONE)
    {
        BattlegroundTypeId bgTypeId = BattlegroundMgr::WeekendHolidayIdToBGType(event.holiday_id);
        if (bgTypeId != BATTLEGROUND_TYPE_NONE)
        {
            BattlemasterListEntry const* bl = sBattlemasterListStore.LookupEntry(bgTypeId);
            if (bl && bl->HolidayWorldState)
            {
                // 发送世界状态更新包给所有玩家
                WorldPackets::WorldState::UpdateWorldState worldstate;
                worldstate.VariableID = bl->HolidayWorldState;
                worldstate.Value = Activate ? 1 : 0;
                sWorld->SendGlobalMessage(worldstate.Write());
            }
        }
    }
}

/**
 * @brief 默认构造函数
 */
GameEventMgr::GameEventMgr() : isSystemInit(false) { }

/**
 * @brief 处理任务完成
 * @param quest_id 任务ID
 *
 * 当玩家完成与世界事件相关的任务时调用，更新事件条件进度。
 * 调用时机：玩家完成任务时。
 *
 * 处理流程：
 * 1. 查找任务到事件条件的映射
 * 2. 更新事件条件进度
 * 3. 发送世界状态更新
 * 4. 保存进度到数据库
 */
void GameEventMgr::HandleQuestComplete(uint32 quest_id)
{
    // 查找任务到事件和条件的映射
    QuestIdToEventConditionMap::iterator itr = mQuestToEventConditions.find(quest_id);
    // 任务已注册
    if (itr != mQuestToEventConditions.end())
    {
        uint16 event_id = itr->second.event_id;
        uint32 condition = itr->second.condition;
        float num = itr->second.num;

        // 如果事件不活动，直接返回，不增加条件完成数
        if (!IsActiveEvent(event_id))
            return;
        // 不在正确的阶段，直接返回
        if (mGameEvent[event_id].state != GAMEEVENT_WORLD_CONDITIONS)
            return;
        GameEventConditionMap::iterator citr = mGameEvent[event_id].conditions.find(condition);
        // 条件已注册
        if (citr != mGameEvent[event_id].conditions.end())
        {
            // 增加完成计数，仅当小于要求数时
            if (citr->second.done < citr->second.reqNum)
            {
                citr->second.done += num;
                // 检查最大限制
                if (citr->second.done > citr->second.reqNum)
                    citr->second.done = citr->second.reqNum;
                // 保存更改到数据库
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GAME_EVENT_CONDITION_SAVE);
                stmt->setUInt8(0, uint8(event_id));
                stmt->setUInt32(1, condition);
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GAME_EVENT_CONDITION_SAVE);
                stmt->setUInt8(0, uint8(event_id));
                stmt->setUInt32(1, condition);
                stmt->setFloat(2, citr->second.done);
                trans->Append(stmt);
                CharacterDatabase.CommitTransaction(trans);
                // 检查所有条件是否满足，如果是，更新事件状态
                if (CheckOneGameEventConditions(event_id))
                {
                    // 已改变，保存游戏事件状态到数据库
                    SaveWorldEventStateToDB(event_id);
                    // 强制更新事件以设置定时器
                    sWorld->ForceGameEventUpdate();
                }
            }
        }
    }
}

/**
 * @brief 检查单个游戏事件的条件
 * @param event_id 事件ID
 * @return 如果所有条件都满足返回true
 *
 * 检查事件的所有条件是否满足。
 * 如果所有条件都满足，将事件状态设置为NEXTPHASE并设置下一阶段开始时间。
 */
bool GameEventMgr::CheckOneGameEventConditions(uint16 event_id)
{
    for (GameEventConditionMap::const_iterator itr = mGameEvent[event_id].conditions.begin(); itr != mGameEvent[event_id].conditions.end(); ++itr)
        if (itr->second.done < itr->second.reqNum)
            // 如果条件不匹配则返回false
            return false;
    // 设置阶段
    mGameEvent[event_id].state = GAMEEVENT_WORLD_NEXTPHASE;
    // 设置后续事件的开始时间
    if (!mGameEvent[event_id].nextstart)
    {
        time_t currenttime = GameTime::GetGameTime();
        mGameEvent[event_id].nextstart = currenttime + mGameEvent[event_id].length * 60;
    }
    return true;
}

/**
 * @brief 保存世界事件状态到数据库
 * @param event_id 事件ID
 *
 * 将世界事件的状态和进度保存到game_event表。
 */
void GameEventMgr::SaveWorldEventStateToDB(uint16 event_id)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GAME_EVENT_SAVE);
    stmt->setUInt8(0, uint8(event_id));
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GAME_EVENT_SAVE);
    stmt->setUInt8(0, uint8(event_id));
    stmt->setUInt8(1, mGameEvent[event_id].state);
    stmt->setUInt32(2, mGameEvent[event_id].nextstart ? uint32(mGameEvent[event_id].nextstart) : 0);
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 发送世界状态更新给玩家
 * @param player 玩家对象指针
 * @param event_id 事件ID
 *
 * 将事件相关的世界状态变量发送给指定玩家。
 * 发送条件完成进度和要求值。
 */
void GameEventMgr::SendWorldStateUpdate(Player* player, uint16 event_id)
{
    GameEventConditionMap::const_iterator itr;
    for (itr = mGameEvent[event_id].conditions.begin(); itr !=mGameEvent[event_id].conditions.end(); ++itr)
    {
        // 发送完成进度
        if (itr->second.done_world_state)
            player->SendUpdateWorldState(itr->second.done_world_state, (uint32)(itr->second.done));
        // 发送要求数值
        if (itr->second.max_world_state)
            player->SendUpdateWorldState(itr->second.max_world_state, (uint32)(itr->second.reqNum));
    }
}

/**
 * @class GameEventAIHookWorker
 * @brief 游戏事件AI钩子工作者
 *
 * 用于遍历地图上的所有生物和游戏对象，触发事件相关的AI脚本
 */
class GameEventAIHookWorker
{
public:
    GameEventAIHookWorker(uint16 eventId, bool activate) : _eventId(eventId), _activate(activate) { }

    /**
     * @brief 访问生物容器
     * @param creatureMap 生物映射
     */
    void Visit(std::unordered_map<ObjectGuid, Creature*>& creatureMap)
    {
        for (auto const& p : creatureMap)
            if (p.second->IsInWorld() && p.second->IsAIEnabled())
                p.second->AI()->OnGameEvent(_activate, _eventId);
    }

    /**
     * @brief 访问游戏对象容器
     * @param gameObjectMap 游戏对象映射
     */
    void Visit(std::unordered_map<ObjectGuid, GameObject*>& gameObjectMap)
    {
        for (auto const& p : gameObjectMap)
            if (p.second->IsInWorld())
                p.second->AI()->OnGameEvent(_activate, _eventId);
    }

    /**
     * @brief 模板访问函数（空实现）
     */
    template<class T>
    void Visit(std::unordered_map<ObjectGuid, T*>&) { }

private:
    uint16 _eventId;    // 事件ID
    bool _activate;     // 激活标志
};

/**
 * @brief 运行SmartAI脚本
 * @param event_id 事件ID
 * @param activate true表示事件开始，false表示事件结束
 *
 * 触发SMART_EVENT_GAME_EVENT_START或SMART_EVENT_GAME_EVENT_END事件。
 * 遍历所有地图上的所有生物和游戏对象，调用它们的AI处理事件。
 */
void GameEventMgr::RunSmartAIScripts(uint16 event_id, bool activate)
{
    //! 遍历每个支持的源类型（生物和游戏对象）
    //! 不完全确定这将如何影响非加载格子中的单位
    sMapMgr->DoForAllMaps([event_id, activate](Map* map)
    {
        GameEventAIHookWorker worker(event_id, activate);
        TypeContainerVisitor<GameEventAIHookWorker, MapStoredObjectTypesContainer> visitor(worker);
        visitor.Visit(map->GetObjectsStore());
    });
}

/**
 * @brief 设置节日事件时间
 * @param event 事件数据引用
 *
 * 根据节日DBC数据计算并设置事件的开始和结束时间。
 * 处理节日的多阶段和重复周期。
 */
void GameEventMgr::SetHolidayEventTime(GameEventData& event)
{
    if (!event.holidayStage) // 忽略节日
        return;

    HolidaysEntry const* holiday = sHolidaysStore.LookupEntry(event.holiday_id);
    if (!holiday->Date[0] || !holiday->Duration[0]) // Invalid definitions
    {
        TC_LOG_ERROR("sql.sql", "Missing date or duration for holiday {}.", event.holiday_id);
        return;
    }

    // 计算阶段索引和长度
    uint8 stageIndex = event.holidayStage - 1;
    event.length = holiday->Duration[stageIndex] * HOUR / MINUTE;

    // 计算阶段偏移时间
    time_t stageOffset = 0;
    for (uint8 i = 0; i < stageIndex; ++i)
        stageOffset += holiday->Duration[i] * HOUR;

    // 根据节日类型设置周期
    switch (holiday->CalendarFilterType)
    {
        case -1: // 每年
            event.occurence = YEAR / MINUTE; // 不太有用
            break;
        case 0: // 每周
            event.occurence = WEEK / MINUTE;
            break;
        case 1: // 仅定义日期（暗月马戏团）
            break;
        case 2: // 仅用于循环事件（战吼）
            break;
    }

    // 处理循环事件
    if (holiday->Looping)
    {
        event.occurence = 0;
        for (uint8 i = 0; i < MAX_HOLIDAY_DURATIONS && holiday->Duration[i]; ++i)
            event.occurence += holiday->Duration[i] * HOUR / MINUTE;
    }

    // 检查是否为单日期事件（年度内固定日期）
    bool singleDate = ((holiday->Date[0] >> 24) & 0x1F) == 31;

    time_t curTime = GameTime::GetGameTime();
    // 遍历所有日期，找到下一个有效的开始时间
    for (uint8 i = 0; i < MAX_HOLIDAY_DATES && holiday->Date[i]; ++i)
    {
        uint32 date = holiday->Date[i];

        tm timeInfo;
        if (singleDate)
        {
            localtime_r(&curTime, &timeInfo);
            timeInfo.tm_year -= 1; // 先尝试去年（事件跨年）
        }
        else
            timeInfo.tm_year = ((date >> 24) & 0x1F) + 100;

        // 解析日期时间（压缩格式）
        timeInfo.tm_mon = (date >> 20) & 0xF;
        timeInfo.tm_mday = ((date >> 14) & 0x3F) + 1;
        timeInfo.tm_hour = (date >> 6) & 0x1F;
        timeInfo.tm_min = date & 0x3F;
        timeInfo.tm_sec = 0;
        timeInfo.tm_wday = 0;
        timeInfo.tm_yday = 0;
        timeInfo.tm_isdst = -1;

        // 尝试获取下一个开始时间（跳过过去的日期）
        time_t startTime = mktime(&timeInfo);
        if (curTime < startTime + event.length * MINUTE)
        {
            event.start = startTime + stageOffset;
            break;
        }
        else if (singleDate)
        {
            // 单日期事件，尝试今年
            tm tmCopy;
            localtime_r(&curTime, &tmCopy);
            int year = tmCopy.tm_year; // 今年
            tmCopy = timeInfo;
            tmCopy.tm_year = year;

            event.start = mktime(&tmCopy) + stageOffset;
            break;
        }
        else
        {
            // 日期已过且不是单日期事件，尝试下一个DBC日期（由holiday_dates修改）
            // 如果找不到，我们不修改开始日期并使用game_event中的值
        }
    }
}

/**
 * @brief 检查节日是否活动
 * @param id 节日ID
 * @return 如果节日活动返回true
 *
 * 全局辅助函数，检查指定的节日是否有对应的活动事件
 */
bool IsHolidayActive(HolidayIds id)
{
    if (id == HOLIDAY_NONE)
        return false;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
    GameEventMgr::ActiveEvents const& ae = sGameEventMgr->GetActiveEventList();

    // 遍历所有活动事件，检查是否有匹配的节日ID
    for (GameEventMgr::ActiveEvents::const_iterator itr = ae.begin(); itr != ae.end(); ++itr)
        if (events[*itr].holiday_id == id)
            return true;

    return false;
}

/**
 * @brief 检查事件是否活动
 * @param eventId 事件ID
 * @return 如果事件活动返回true
 *
 * 全局辅助函数，检查指定的事件是否在活动事件列表中
 */
bool IsEventActive(uint16 eventId)
{
    GameEventMgr::ActiveEvents const& ae = sGameEventMgr->GetActiveEventList();
    return ae.find(eventId) != ae.end();
}
