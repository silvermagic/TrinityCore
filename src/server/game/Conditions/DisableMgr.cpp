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
 * @file DisableMgr.cpp
 * @brief 禁用管理器实现文件 - 实现游戏内容禁用的核心逻辑
 *
 * 本文件实现了禁用管理器的所有功能，包括：
 * - 从数据库加载禁用设置
 * - 各种游戏内容的禁用检查
 * - 参数解析和验证
 *
 * 主要数据流：
 * 1. 服务器启动时调用LoadDisables()从disables表加载数据
 * 2. 根据禁用类型存储到相应的数据结构
 * 3. 游戏运行时通过IsDisabledFor等接口检查禁用状态
 *
 * 禁用类型说明：
 * - 法术禁用：可针对不同目标类型（玩家、生物、宠物等）
 * - 地图禁用：可针对不同难度模式
 * - VMAP/MMAP禁用：影响地形和寻路系统
 */

#include "DisableMgr.h"
#include "AchievementMgr.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "OutdoorPvP.h"
#include "Player.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "VMapManager2.h"
#include "World.h"

namespace DisableMgr
{

namespace
{
    /**
     * @brief 禁用数据结构体 - 存储单个禁用条目的信息
     */
    struct DisableData
    {
        uint16 flags;                   // 禁用标志位，根据禁用类型含义不同
        std::set<uint32> params[2];     // 参数集合：params0和params1，用于存储地图ID、区域ID等
    };

    // 按条目ID索引的禁用数据映射
    typedef std::map<uint32, DisableData> DisableTypeMap;
    // 按禁用类型索引的全局禁用映射
    typedef std::map<DisableType, DisableTypeMap> DisableMap;

    // 全局禁用数据存储
    DisableMap m_DisableMap;

    // 最大禁用类型数量
    uint8 MAX_DISABLE_TYPES = 9;
}

/**
 * @brief 从数据库加载所有禁用设置
 *
 * 主要流程：
 * 1. 清理现有的禁用数据
 * 2. 从disables表读取所有数据
 * 3. 根据禁用类型验证并存储每个条目
 * 4. 解析参数（如地图列表、区域列表）
 *
 * 禁用类型处理：
 * - 法术：验证法术存在性，解析地图和区域参数
 * - 任务：延迟验证（在CheckQuestDisables中进行）
 * - 地图：验证地图存在性，处理难度标志
 * - 战场：验证战场存在性
 * - VMAP/MMAP：验证地图存在性，记录禁用信息
 */
void LoadDisables()
{
    uint32 oldMSTime = getMSTime();

    // 重载情况：先清理所有现有数据
    for (DisableMap::iterator itr = m_DisableMap.begin(); itr != m_DisableMap.end(); ++itr)
        itr->second.clear();

    m_DisableMap.clear();

    // 从数据库查询所有禁用设置
    QueryResult result = WorldDatabase.Query("SELECT sourceType, entry, flags, params_0, params_1 FROM disables");

    uint32 total_count = 0;

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 disables. DB table `disables` is empty!");
        return;
    }

    Field* fields;
    do
    {
        fields = result->Fetch();
        DisableType type = DisableType(fields[0].GetUInt32());

        // 验证禁用类型是否有效
        if (type >= MAX_DISABLE_TYPES)
        {
            TC_LOG_ERROR("sql.sql", "Invalid type {} specified in `disables` table, skipped.", type);
            continue;
        }

        uint32 entry = fields[1].GetUInt32();
        uint16 flags = fields[2].GetUInt16();
        std::string params_0 = fields[3].GetString();
        std::string params_1 = fields[4].GetString();

        DisableData data;
        data.flags = flags;

        // 根据禁用类型进行不同的验证和处理
        switch (type)
        {
            case DISABLE_TYPE_SPELL:
                // 验证法术是否存在（除非是已废弃法术）
                if (!(sSpellMgr->GetSpellInfo(entry) || flags & SPELL_DISABLE_DEPRECATED_SPELL))
                {
                    TC_LOG_ERROR("sql.sql", "Spell entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }

                // 验证禁用标志是否有效
                if (!flags || flags > MAX_SPELL_DISABLE_TYPE)
                {
                    TC_LOG_ERROR("sql.sql", "Disable flags for spell {} are invalid, skipped.", entry);
                    continue;
                }

                // 解析地图参数（如果设置了SPELL_DISABLE_MAP标志）
                if (flags & SPELL_DISABLE_MAP)
                {
                    // 将逗号分隔的地图ID字符串解析为集合
                    for (std::string_view mapStr : Trinity::Tokenize(params_0, ',', true))
                    {
                        if (Optional<uint32> mapId = Trinity::StringTo<uint32>(mapStr))
                            data.params[0].insert(*mapId);
                        else
                            TC_LOG_ERROR("sql.sql", "Disable map '{}' for spell {} is invalid, skipped.", std::string(mapStr), entry);
                    }
                }

                if (flags & SPELL_DISABLE_AREA)
                {
                    for (std::string_view areaStr : Trinity::Tokenize(params_1, ',', true))
                    {
                        if (Optional<uint32> areaId = Trinity::StringTo<uint32>(areaStr))
                            data.params[1].insert(*areaId);
                        else
                            TC_LOG_ERROR("sql.sql", "Disable area '{}' for spell {} is invalid, skipped.", std::string(areaStr), entry);
                    }
                }

                break;
            // checked later
            case DISABLE_TYPE_QUEST:
                break;
            case DISABLE_TYPE_MAP:
            case DISABLE_TYPE_LFG_MAP:
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (!mapEntry)
                {
                    TC_LOG_ERROR("sql.sql", "Map entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                bool isFlagInvalid = false;
                switch (mapEntry->InstanceType)
                {
                    case MAP_COMMON:
                        if (flags)
                            isFlagInvalid = true;
                        break;
                    case MAP_INSTANCE:
                    case MAP_RAID:
                        if (flags & DUNGEON_STATUSFLAG_HEROIC && !GetMapDifficultyData(entry, DUNGEON_DIFFICULTY_HEROIC))
                            flags -= DUNGEON_STATUSFLAG_HEROIC;
                        if (flags & RAID_STATUSFLAG_10MAN_HEROIC && !GetMapDifficultyData(entry, RAID_DIFFICULTY_10MAN_HEROIC))
                            flags -= RAID_STATUSFLAG_10MAN_HEROIC;
                        if (flags & RAID_STATUSFLAG_25MAN_HEROIC && !GetMapDifficultyData(entry, RAID_DIFFICULTY_25MAN_HEROIC))
                            flags -= RAID_STATUSFLAG_25MAN_HEROIC;
                        if (!flags)
                            isFlagInvalid = true;
                        break;
                    case MAP_BATTLEGROUND:
                    case MAP_ARENA:
                        TC_LOG_ERROR("sql.sql", "Battleground map {} specified to be disabled in map case, skipped.", entry);
                        continue;
                }
                if (isFlagInvalid)
                {
                    TC_LOG_ERROR("sql.sql", "Disable flags for map {} are invalid, skipped.", entry);
                    continue;
                }
                break;
            }
            case DISABLE_TYPE_BATTLEGROUND:
                if (!sBattlemasterListStore.LookupEntry(entry))
                {
                    TC_LOG_ERROR("sql.sql", "Battleground entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                if (flags)
                    TC_LOG_ERROR("sql.sql", "Disable flags specified for battleground {}, useless data.", entry);
                break;
            case DISABLE_TYPE_OUTDOORPVP:
                if (entry > MAX_OUTDOORPVP_TYPES)
                {
                    TC_LOG_ERROR("sql.sql", "OutdoorPvPTypes value {} from `disables` is invalid, skipped.", entry);
                    continue;
                }
                if (flags)
                    TC_LOG_ERROR("sql.sql", "Disable flags specified for outdoor PvP {}, useless data.", entry);
                break;
            case DISABLE_TYPE_ACHIEVEMENT_CRITERIA:
                if (!sAchievementMgr->GetAchievementCriteria(entry))
                {
                    TC_LOG_ERROR("sql.sql", "Achievement Criteria entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                if (flags)
                    TC_LOG_ERROR("sql.sql", "Disable flags specified for Achievement Criteria {}, useless data.", entry);
                break;
            case DISABLE_TYPE_VMAP:
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (!mapEntry)
                {
                    TC_LOG_ERROR("sql.sql", "Map entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                switch (mapEntry->InstanceType)
                {
                    case MAP_COMMON:
                        if (flags & VMAP::VMAP_DISABLE_AREAFLAG)
                            TC_LOG_INFO("misc", "Areaflag disabled for world map {}.", entry);
                        if (flags & VMAP::VMAP_DISABLE_LIQUIDSTATUS)
                            TC_LOG_INFO("misc", "Liquid status disabled for world map {}.", entry);
                        break;
                    case MAP_INSTANCE:
                    case MAP_RAID:
                        if (flags & VMAP::VMAP_DISABLE_HEIGHT)
                            TC_LOG_INFO("misc", "Height disabled for instance map {}.", entry);
                        if (flags & VMAP::VMAP_DISABLE_LOS)
                            TC_LOG_INFO("misc", "LoS disabled for instance map {}.", entry);
                        break;
                    case MAP_BATTLEGROUND:
                        if (flags & VMAP::VMAP_DISABLE_HEIGHT)
                            TC_LOG_INFO("misc", "Height disabled for battleground map {}.", entry);
                        if (flags & VMAP::VMAP_DISABLE_LOS)
                            TC_LOG_INFO("misc", "LoS disabled for battleground map {}.", entry);
                        break;
                    case MAP_ARENA:
                        if (flags & VMAP::VMAP_DISABLE_HEIGHT)
                            TC_LOG_INFO("misc", "Height disabled for arena map {}.", entry);
                        if (flags & VMAP::VMAP_DISABLE_LOS)
                            TC_LOG_INFO("misc", "LoS disabled for arena map {}.", entry);
                        break;
                    default:
                        break;
                }
                break;
            }
            case DISABLE_TYPE_MMAP:
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (!mapEntry)
                {
                    TC_LOG_ERROR("sql.sql", "Map entry {} from `disables` doesn't exist in dbc, skipped.", entry);
                    continue;
                }
                switch (mapEntry->InstanceType)
                {
                    case MAP_COMMON:
                        TC_LOG_INFO("misc", "Pathfinding disabled for world map {}.", entry);
                        break;
                    case MAP_INSTANCE:
                    case MAP_RAID:
                        TC_LOG_INFO("misc", "Pathfinding disabled for instance map {}.", entry);
                        break;
                    case MAP_BATTLEGROUND:
                        TC_LOG_INFO("misc", "Pathfinding disabled for battleground map {}.", entry);
                        break;
                    case MAP_ARENA:
                        TC_LOG_INFO("misc", "Pathfinding disabled for arena map {}.", entry);
                        break;
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }

        m_DisableMap[type].insert(DisableTypeMap::value_type(entry, data));
        ++total_count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} disables in {} ms", total_count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 检查任务禁用的有效性
 *
 * 调用时机：任务加载完成后
 *
 * 主要功能：
 * - 验证所有任务禁用条目的任务是否存在
 * - 移除无效的任务禁用条目
 * - 检查无用的标志数据
 *
 * 注意：其他禁用类型在LoadDisables中已验证，
 *       但任务需要在任务模板加载后才能验证。
 */
void CheckQuestDisables()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = m_DisableMap[DISABLE_TYPE_QUEST].size();
    if (!count)
    {
        TC_LOG_INFO("server.loading", ">> Checked 0 quest disables.");
        return;
    }

    // 仅检查任务，其他类型在启动时已完成验证
    for (DisableTypeMap::iterator itr = m_DisableMap[DISABLE_TYPE_QUEST].begin(); itr != m_DisableMap[DISABLE_TYPE_QUEST].end();)
    {
        const uint32 entry = itr->first;
        // 检查任务模板是否存在
        if (!sObjectMgr->GetQuestTemplate(entry))
        {
            TC_LOG_ERROR("sql.sql", "Quest entry {} from `disables` doesn't exist, skipped.", entry);
            m_DisableMap[DISABLE_TYPE_QUEST].erase(itr++);
            continue;
        }
        // 任务禁用不需要标志，警告无用数据
        if (itr->second.flags)
            TC_LOG_ERROR("sql.sql", "Disable flags specified for quest {}, useless data.", entry);
        ++itr;
    }

    TC_LOG_INFO("server.loading", ">> Checked {} quest disables in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 检查指定条目是否被禁用
 * @param type 禁用类型
 * @param entry 条目ID
 * @param ref 参考对象（用于上下文判断）
 * @param flags 额外标志
 * @return 是否被禁用
 *
 * 这是最常用的禁用检查接口，被多个游戏系统调用：
 * - 法术系统检查法术是否禁用
 * - 地图系统检查地图是否禁用
 * - 任务系统检查任务是否禁用
 * - 等等...
 *
 * 性能注意：此函数调用频率很高，使用哈希表快速查找
 */
bool IsDisabledFor(DisableType type, uint32 entry, WorldObject const* ref, uint8 flags /*= 0*/)
{
    ASSERT(type < MAX_DISABLE_TYPES);

    // 如果该类型没有禁用数据，直接返回false
    if (m_DisableMap[type].empty())
        return false;

    // 查找指定条目
    DisableTypeMap::iterator itr = m_DisableMap[type].find(entry);
    if (itr == m_DisableMap[type].end())    // 未找到，未被禁用
        return false;

    // 根据禁用类型进行详细检查
    switch (type)
    {
        case DISABLE_TYPE_SPELL:
        {
            uint16 spellFlags = itr->second.flags;
            if (ref)
            {
                // 检查对象类型与禁用标志是否匹配
                if ((ref->GetTypeId() == TYPEID_PLAYER && (spellFlags & SPELL_DISABLE_PLAYER)) ||
                    (ref->GetTypeId() == TYPEID_UNIT && ((spellFlags & SPELL_DISABLE_CREATURE) || (ref->ToCreature()->IsPet() && (spellFlags & SPELL_DISABLE_PET)))) ||
                    (ref->GetTypeId() == TYPEID_GAMEOBJECT && (spellFlags & SPELL_DISABLE_GAMEOBJECT)))
                {
                    if (spellFlags & (SPELL_DISABLE_ARENAS | SPELL_DISABLE_BATTLEGROUNDS))
                    {
                        if (Map const* map = ref->FindMap())
                        {
                            if (spellFlags & SPELL_DISABLE_ARENAS && map->IsBattleArena())
                                return true;                                    // Current map is Arena and this spell is disabled here

                            if (spellFlags & SPELL_DISABLE_BATTLEGROUNDS && map->IsBattleground())
                                return true;                                    // Current map is a Battleground and this spell is disabled here
                        }
                    }

                    if (spellFlags & SPELL_DISABLE_MAP)
                    {
                        std::set<uint32> const& mapIds = itr->second.params[0];
                        if (mapIds.find(ref->GetMapId()) != mapIds.end())
                            return true;                                        // Spell is disabled on current map

                        if (!(spellFlags & SPELL_DISABLE_AREA))
                            return false;                                       // Spell is disabled on another map, but not this one, return false

                        // Spell is disabled in an area, but not explicitly our current mapId. Continue processing.
                    }

                    if (spellFlags & SPELL_DISABLE_AREA)
                    {
                        std::set<uint32> const& areaIds = itr->second.params[1];
                        if (areaIds.find(ref->GetAreaId()) != areaIds.end())
                            return true;                                        // Spell is disabled in this area
                        return false;                                           // Spell is disabled in another area, but not this one, return false
                    }
                    else
                        return true;                                            // Spell disabled for all maps
                }

                return false;
            }
            else if (spellFlags & SPELL_DISABLE_DEPRECATED_SPELL)    // 不是从施法调用的
                return true;
            else if (flags & SPELL_DISABLE_LOS)
                return (spellFlags & SPELL_DISABLE_LOS) != 0;

            break;
        }
        case DISABLE_TYPE_MAP:
        case DISABLE_TYPE_LFG_MAP:
            // 检查地图禁用（需要考虑难度）
            if (Player const* player = ref->ToPlayer())
            {
                MapEntry const* mapEntry = sMapStore.LookupEntry(entry);
                if (mapEntry->IsDungeon())
                {
                    uint8 disabledModes = itr->second.flags;
                    Difficulty targetDifficulty = player->GetDifficulty(mapEntry->IsRaid());
                    GetDownscaledMapDifficultyData(entry, targetDifficulty);

                    // 根据难度检查是否禁用
                    switch (targetDifficulty)
                    {
                        case DUNGEON_DIFFICULTY_NORMAL:
                            return (disabledModes & DUNGEON_STATUSFLAG_NORMAL) != 0;
                        case DUNGEON_DIFFICULTY_HEROIC:
                            return (disabledModes & DUNGEON_STATUSFLAG_HEROIC) != 0;
                        case RAID_DIFFICULTY_10MAN_HEROIC:
                            return (disabledModes & RAID_STATUSFLAG_10MAN_HEROIC) != 0;
                        case RAID_DIFFICULTY_25MAN_HEROIC:
                            return (disabledModes & RAID_STATUSFLAG_25MAN_HEROIC) != 0;
                    }
                }
                else if (mapEntry->InstanceType == MAP_COMMON)
                    return true;
            }
            return false;
        case DISABLE_TYPE_QUEST:
            // 任务禁用：直接返回true
            return true;
        case DISABLE_TYPE_BATTLEGROUND:
        case DISABLE_TYPE_OUTDOORPVP:
        case DISABLE_TYPE_ACHIEVEMENT_CRITERIA:
        case DISABLE_TYPE_MMAP:
            // 这些类型直接返回true（已找到禁用条目）
            return true;
        case DISABLE_TYPE_VMAP:
            // VMAP禁用：检查标志匹配
           return (flags & itr->second.flags) != 0;
    }

    return false;
}

/**
 * @brief 检查VMAP是否被禁用
 * @param entry 地图ID
 * @param flags VMAP禁用标志
 * @return VMAP是否被禁用
 *
 * 这是IsDisabledFor的便捷封装，专门用于VMAP检查。
 */
bool IsVMAPDisabledFor(uint32 entry, uint8 flags)
{
    return IsDisabledFor(DISABLE_TYPE_VMAP, entry, nullptr, flags);
}

/**
 * @brief 检查指定地图是否启用了寻路
 * @param mapId 地图ID
 * @return 是否启用寻路
 *
 * 需要同时满足两个条件：
 * 1. 全局配置启用了MMAP
 * 2. 该地图没有被禁用MMAP
 */
bool IsPathfindingEnabled(uint32 mapId)
{
    return sWorld->getBoolConfig(CONFIG_ENABLE_MMAPS)
        && !IsDisabledFor(DISABLE_TYPE_MMAP, mapId, nullptr, MMAP_DISABLE_PATHFINDING);
}

} // Namespace
