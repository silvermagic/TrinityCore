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
 * @file AchievementMgr.cpp
 * @brief 成就系统管理模块实现
 *
 * 本文件实现了成就系统的核心功能，包括：
 * - 成就进度跟踪与更新
 * - 成就完成检测与通知
 * - 成就奖励发放
 * - 计时成就管理
 * - 数据持久化
 * - 全局成就数据加载与管理
 *
 * 成就系统是魔兽世界中的重要游戏机制，用于记录玩家的各种成就
 * 包括击杀Boss、完成任务、收集物品、探索地图、PVP成就等
 */

#include "AchievementMgr.h"
#include "ArenaTeamMgr.h"
#include "Battleground.h"
#include "CellImpl.h"
#include "ChatTextBuilder.h"
#include "DatabaseEnv.h"
#include "DBCEnums.h"
#include "DisableMgr.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WowTime.h"

/**
 * @brief 验证成就条件数据是否有效
 *
 * 检查条件数据的类型是否正确，以及相关值是否在有效范围内
 * 这在加载achievement_criteria_data表时被调用
 *
 * @param criteria 成就条件定义条目
 * @return 数据有效返回true，否则返回false
 */
bool AchievementCriteriaData::IsValid(AchievementCriteriaEntry const* criteria)
{
    // 检查数据类型是否在有效范围内
    if (dataType >= MAX_ACHIEVEMENT_CRITERIA_DATA_TYPE)
    {
        TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` for criteria (Entry: {}) contains a wrong data type ({}), ignored.", criteria->ID, dataType);
        return false;
    }

    // 验证条件类型是否支持附加数据
    // 只有特定类型的条件才支持数据库配置的附加条件
    switch (criteria->Type)
    {
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:           // 击杀生物
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:      // 击杀生物类型
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:                  // 赢得战场
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON:        // 副本死亡
        case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:      // 坠落不死
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:          // 完成任务（仅限硬编码列表）
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:              // 施放法术
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:         // 赢得竞技场
        case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:                // 表情动作
        case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:        // 特殊PVP击杀
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:                // 赢得决斗
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:               // 拾取类型
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:             // 施放法术2
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:         // 成为法术目标
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:        // 成为法术目标2
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:         // 装备史诗物品
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:       // 需求掷骰
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:      // 贪婪掷骰
        case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:    // 战场目标占领
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:          // 荣誉击杀
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:    // 完成日常任务（仅儿童周成就）
        case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:                // 使用物品（仅儿童周成就）
        case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:       // 获得击杀
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL:             // 达到等级
        case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:                // 登录时
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM:          // 拾取史诗物品
        case ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM:       // 获得史诗物品
            break;
        default:
            // 其他类型的条件只支持脚本类型数据
            if (dataType != ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` contains data for a non-supported criteria type (Entry: {} Type: {}), ignored.", criteria->ID, criteria->Type);
                return false;
            }
            break;
    }

    // 根据数据类型进行具体验证
    switch (dataType)
    {
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE:            // 无附加条件
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT: // 副本脚本（无需验证）
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_NTH_BIRTHDAY:    // 周年纪念（无需验证）
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_CREATURE:      // 目标生物验证
            if (!creature.id || !sObjectMgr->GetCreatureTemplate(creature.id))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_CREATURE ({}) contains a non-existing creature id in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, creature.id);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE: // 目标职业/种族验证
            // 验证职业ID是否有效
            if (classRace.class_id && ((1 << (classRace.class_id-1)) & CLASSMASK_ALL_PLAYABLE) == 0)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE ({}) contains a non-existing class in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, classRace.class_id);
                return false;
            }
            // 验证种族ID是否有效
            if (classRace.race_id && ((1 << (classRace.race_id-1)) & RACEMASK_ALL_PLAYABLE) == 0)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE ({}) contains a non-existing race in value2 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, classRace.race_id);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_LESS_HEALTH: // 目标玩家血量百分比验证
            // 血量百分比必须在1-100之间
            if (health.percent < 1 || health.percent > 100)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_PLAYER_LESS_HEALTH ({}) contains a wrong percent value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, health.percent);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_DEAD:
            if (player_dead.own_team_flag > 1)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_DEAD ({}) contains a wrong boolean value1 ({}).",
                    criteria->ID, criteria->Type, dataType, player_dead.own_team_flag);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA:
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA:
        {
            SpellInfo const* spellEntry = sSpellMgr->GetSpellInfo(aura.spell_id);
            if (!spellEntry)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type {} ({}) contains a wrong spell id in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, (dataType == ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA?"ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA":"ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA"), dataType, aura.spell_id);
                return false;
            }
            if (aura.effect_idx >= 3)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type {} ({}) contains a wrong spell effect index in value2 ({}), ignored.",
                    criteria->ID, criteria->Type, (dataType == ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA?"ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA":"ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA"), dataType, aura.effect_idx);
                return false;
            }
            if (!spellEntry->GetEffect(SpellEffIndex(aura.effect_idx)).ApplyAuraName)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type {} ({}) contains a non-aura spell effect (ID: {} Effect: {}), ignored.",
                    criteria->ID, criteria->Type, (dataType == ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA?"ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA":"ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA"), dataType, aura.spell_id, aura.effect_idx);
                return false;
            }
            return true;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AREA:
            if (!sAreaTableStore.LookupEntry(area.id))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AREA ({}) contains a wrong area id in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, area.id);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_VALUE:
            if (value.compType >= COMP_TYPE_MAX)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_VALUE ({}) contains a wrong ComparisionType in value2 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, value.compType);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_LEVEL:
            if (level.minlevel > STRONG_MAX_LEVEL)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_LEVEL ({}) contains a wrong minlevel in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, level.minlevel);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_GENDER:
            if (gender.gender > GENDER_NONE)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_GENDER ({}) contains a wrong gender value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, gender.gender);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT:
            if (!ScriptId)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT ({}) does not have a ScriptName set, ignored.",
                    criteria->ID, criteria->Type, dataType);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_DIFFICULTY:
            if (difficulty.difficulty >= MAX_DIFFICULTY)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_DIFFICULTY ({}) contains a wrong difficulty value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, difficulty.difficulty);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_PLAYER_COUNT:
            if (map_players.maxcount <= 0)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_PLAYER_COUNT ({}) contains a wrong max players count in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, map_players.maxcount);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_TEAM:
            if (team.team != ALLIANCE && team.team != HORDE)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_T_TEAM ({}) contains an unknown team value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, team.team);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_DRUNK:
            if (drunk.state >= MAX_DRUNKEN)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_DRUNK ({}) contains an unknown drunken state value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, drunk.state);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY:
            if (!sHolidaysStore.LookupEntry(holiday.id))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY ({}) contains an unknown holiday entry in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, holiday.id);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_BG_LOSS_TEAM_SCORE:
            return true;                                    // not check correctness node indexes
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM:
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_ITEM_QUALITY:
            if (equipped_item.item_quality >= MAX_ITEM_QUALITY)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type {} ({}) contains an unknown quality state value in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, (dataType == ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM ? "ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM" : "ACHIEVEMENT_CRITERIA_DATA_TYPE_S_ITEM_QUALITY"), dataType, equipped_item.item_quality);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_ID:
            if (!sMapStore.LookupEntry(map_id.mapId))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_ID ({}) contains an unknown map id in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, map_id.mapId);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE:
            if (!classRace.class_id && !classRace.race_id)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE ({}) should not have 0 in either value field. Ignored.",
                    criteria->ID, criteria->Type, dataType);
                return false;
            }
            if (classRace.class_id && ((1 << (classRace.class_id-1)) & CLASSMASK_ALL_PLAYABLE) == 0)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE ({}) contains a non-existing class entry in value1 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, classRace.class_id);
                return false;
            }
            if (classRace.race_id && ((1 << (classRace.race_id-1)) & RACEMASK_ALL_PLAYABLE) == 0)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE ({}) contains a non-existing race entry in value2 ({}), ignored.",
                    criteria->ID, criteria->Type, dataType, classRace.race_id);
                return false;
            }
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_KNOWN_TITLE:
            if (!sCharTitlesStore.LookupEntry(known_title.title_id))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) for data type ACHIEVEMENT_CRITERIA_DATA_TYPE_S_KNOWN_TITLE ({}) contains an unknown title_id in value1 ({}), ignore.",
                    criteria->ID, criteria->Type, dataType, known_title.title_id);
                return false;
            }
            return true;
        default:
            TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` (Entry: {} Type: {}) contains data of a non-supported data type ({}), ignored.", criteria->ID, criteria->Type, dataType);
            return false;
    }
}

/**
 * @brief 检查成就条件数据是否满足要求
 *
 * 这是成就系统的核心条件检查函数
 * 根据不同的数据类型，检查玩家、目标、环境等是否满足特定条件
 *
 * @param criteria_id 条件ID
 * @param source 源玩家（触发条件的玩家）
 * @param target 目标对象（如被击杀的生物、被治疗的玩家等）
 * @param miscvalue1 杂项值1（根据条件类型有不同含义）
 * @param miscvalue2 杂项值2（根据条件类型有不同含义）
 * @return 条件满足返回true，否则返回false
 */
bool AchievementCriteriaData::Meets(uint32 criteria_id, Player const* source, WorldObject const* target, uint32 miscvalue1 /*= 0*/, uint32 miscvalue2 /* = 0*/) const
{
    // 根据数据类型进行条件判断
    switch (dataType)
    {
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE:
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_CREATURE:
            if (!target || target->GetTypeId() != TYPEID_UNIT)
                return false;
            return target->GetEntry() == creature.id;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE:
            if (!target || target->GetTypeId() != TYPEID_PLAYER)
                return false;
            if (classRace.class_id && classRace.class_id != target->ToPlayer()->GetClass())
                return false;
            if (classRace.race_id && classRace.race_id != target->ToPlayer()->GetRace())
                return false;
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE:
            if (source->GetTypeId() != TYPEID_PLAYER)
                return false;
            if (classRace.class_id && classRace.class_id != source->ToPlayer()->GetClass())
                return false;
            if (classRace.race_id && classRace.race_id != source->ToPlayer()->GetRace())
                return false;
            return true;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_LESS_HEALTH:
            if (!target || target->GetTypeId() != TYPEID_PLAYER)
                return false;
            return !target->ToPlayer()->HealthAbovePct(health.percent);
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_DEAD:
            if (!target || target->GetTypeId() != TYPEID_PLAYER)
                return false;
            if (Player const* player = target->ToPlayer())
                if (!player->IsAlive() && player->GetDeathTimer() != 0)
                    // flag set == must be same team, not set == different team
                    return (player->GetTeam() == source->GetTeam()) == (player_dead.own_team_flag != 0);
            return false;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA:
            return source->HasAuraEffect(aura.spell_id, aura.effect_idx);
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AREA:
        {
            uint32 zone_id, area_id;
            source->GetZoneAndAreaId(zone_id, area_id);
            return area.id == zone_id || area.id == area_id;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA:
        {
            if (!target)
                return false;
            Unit const* unitTarget = target->ToUnit();
            if (!unitTarget)
                return false;
            return unitTarget->HasAuraEffect(aura.spell_id, aura.effect_idx);
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_VALUE:
            return CompareValues(ComparisionType(value.compType), miscvalue1, value.value);
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_LEVEL:
        {
            if (!target)
                return false;
            Unit const* unitTarget = target->ToUnit();
            if (!unitTarget)
                return false;
            return unitTarget->GetLevel() >= level.minlevel;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_GENDER:
        {
            if (!target)
                return false;
            Unit const* unitTarget = target->ToUnit();
            if (!unitTarget)
                return false;
            return unitTarget->GetGender() == Gender(gender.gender);
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT:
        {
            Unit const* unitTarget = nullptr;
            if (target)
                unitTarget = target->ToUnit();
            return sScriptMgr->OnCriteriaCheck(ScriptId, const_cast<Player*>(source), const_cast<Unit*>(unitTarget));
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_DIFFICULTY:
            if (source->GetMap()->IsRaid())
                if (source->GetMap()->Is25ManRaid() != ((difficulty.difficulty & RAID_DIFFICULTY_MASK_25MAN) != 0))
                    return false;
            return source->GetMap()->GetSpawnMode() >= difficulty.difficulty;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_PLAYER_COUNT:
            return source->GetMap()->GetPlayersCountExceptGMs() <= map_players.maxcount;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_T_TEAM:
            if (!target || target->GetTypeId() != TYPEID_PLAYER)
                return false;
            return target->ToPlayer()->GetTeam() == team.team;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_DRUNK:
            return Player::GetDrunkenstateByValue(source->GetDrunkValue()) >= DrunkenState(drunk.state);
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY:
            return IsHolidayActive(HolidayIds(holiday.id));
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_BG_LOSS_TEAM_SCORE:
        {
            Battleground* bg = source->GetBattleground();
            if (!bg)
                return false;

            uint32 score = bg->GetTeamScore(source->GetTeamId() == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE);
            return score >= bg_loss_team_score.min_score && score <= bg_loss_team_score.max_score;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT:
        {
            if (!source->IsInWorld())
                return false;
            Map* map = source->GetMap();
            if (!map->IsDungeon())
            {
                TC_LOG_ERROR("achievement", "Achievement system call ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT ({}) for achievement criteria {} in a non-dungeon/non-raid map {}",
                    dataType, criteria_id, map->GetId());
                return false;
            }
            InstanceScript* instance = map->ToInstanceMap()->GetInstanceScript();
            if (!instance)
            {
                TC_LOG_ERROR("achievement", "Achievement system call ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT ({}) for achievement criteria {} in map {}, but the map does not have an instance script.",
                    dataType, criteria_id, map->GetId());
                return false;
            }

            Unit const* unitTarget = nullptr;
            if (target)
                unitTarget = target->ToUnit();
            return instance->CheckAchievementCriteriaMeet(criteria_id, source, unitTarget, miscvalue1);
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM:
        {
            AchievementCriteriaEntry const* entry = ASSERT_NOTNULL(sAchievementMgr->GetAchievementCriteria(criteria_id));

            uint32 itemId = (entry->Type == ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM ? miscvalue2 : miscvalue1);
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
            if (!itemTemplate)
                return false;
            return itemTemplate->ItemLevel >= equipped_item.item_level && itemTemplate->Quality >= equipped_item.item_quality;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_ID:
            return source->GetMapId() == map_id.mapId;
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_NTH_BIRTHDAY:
        {
            time_t birthday_start = time_t(sWorld->getIntConfig(CONFIG_BIRTHDAY_TIME));
            tm birthday_tm;
            localtime_r(&birthday_start, &birthday_tm);

            // exactly N birthday
            birthday_tm.tm_year += birthday_login.nth_birthday;

            time_t birthday = mktime(&birthday_tm);
            time_t now = GameTime::GetGameTime();
            return now <= birthday + DAY && now >= birthday;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_KNOWN_TITLE:
        {
            if (CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(known_title.title_id))
                return source->HasTitle(titleInfo->MaskID);
            return false;
        }
        case ACHIEVEMENT_CRITERIA_DATA_TYPE_S_ITEM_QUALITY:
        {
            ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(miscvalue1);
            if (!pProto)
                return false;
            return pProto->Quality == item.item_quality;
        }
        default:
            break;
    }
    return false;
}

/**
 * @brief 检查条件数据集中的所有条件是否都满足
 *
 * 遍历数据集中的所有条件数据，只要有一个不满足就返回false
 *
 * @param source 源玩家
 * @param target 目标对象
 * @param miscvalue1 杂项值1
 * @param miscvalue2 杂项值2
 * @return 所有条件都满足返回true，否则返回false
 */
bool AchievementCriteriaDataSet::Meets(Player const* source, WorldObject const* target, uint32 miscvalue1 /*= 0*/, uint32 miscvalue2 /* = 0*/) const
{
    // 遍历所有条件数据，必须全部满足
    for (AchievementCriteriaData const& criteriadata : storage)
        if (!criteriadata.Meets(criteria_id, source, target, miscvalue1, miscvalue2))
            return false;

    return true;
}

/**
 * @brief AchievementMgr构造函数
 * @param player 关联的玩家对象
 */
AchievementMgr::AchievementMgr(Player* player)
{
    m_player = player;
}

/**
 * @brief AchievementMgr析构函数
 */
AchievementMgr::~AchievementMgr() { }

/**
 * @brief 重置所有成就数据
 *
 * 清空所有已完成成就和进度，通知客户端，并重新检查所有条件
 * 这是一个完整的成就重置操作
 */
void AchievementMgr::Reset()
{
    // 向客户端发送删除已完成成就的通知
    for (std::pair<uint32 const, CompletedAchievementData> const& completedAchievement : m_completedAchievements)
    {
        WorldPacket data(SMSG_ACHIEVEMENT_DELETED, 4);
        data << uint32(completedAchievement.first);
        m_player->SendDirectMessage(&data);
    }

    // 向客户端发送删除条件进度的通知
    for (std::pair<uint32 const, CriteriaProgress> const& criteriaprogress : m_criteriaProgress)
    {
        WorldPacket data(SMSG_CRITERIA_DELETED, 4);
        data << uint32(criteriaprogress.first);
        m_player->SendDirectMessage(&data);
    }

    // 清空内存数据
    m_completedAchievements.clear();
    m_criteriaProgress.clear();
    // 从数据库删除成就数据
    DeleteFromDB(m_player->GetGUID());

    // 重新检查所有成就条件，填充可能已完成的数据
    CheckAllAchievementCriteria();
}

/**
 * @brief 重置特定条件的成就进度
 * @param condition 条件类型
 * @param value 条件值
 * @param evenIfCriteriaComplete 是否重置已完成的条件
 *
 * 当特定事件发生时（如更改阵营），需要重置相关的成就进度
 */
void AchievementMgr::ResetAchievementCriteria(AchievementCriteriaCondition condition, uint32 value, bool evenIfCriteriaComplete)
{
    TC_LOG_DEBUG("achievement", "AchievementMgr::ResetAchievementCriteria({}, {}, {})", condition, value, evenIfCriteriaComplete);

    // GM模式或没有成就权限的玩家不处理
    if (m_player->IsGameMaster() || m_player->GetSession()->HasPermission(rbac::RBAC_PERM_CANNOT_EARN_ACHIEVEMENTS))
        return;

    // 获取符合条件的成就条件列表
    AchievementCriteriaEntryList const* achievementCriteriaList = sAchievementMgr->GetAchievementCriteriaByCondition(condition, value);
    if (!achievementCriteriaList)
        return;

    // 遍历并重置符合条件的进度
    for (AchievementCriteriaEntry const* achievementCriteria : *achievementCriteriaList)
    {
        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(achievementCriteria->AchievementID);
        if (!achievement)
            continue;

        // 如果条件已完成但未强制重置，或成就已完成，则跳过
        if ((IsCompletedCriteria(achievementCriteria, achievement) && !evenIfCriteriaComplete) || HasAchieved(achievement->ID))
            continue;

        // 移除条件进度
        RemoveCriteriaProgress(achievementCriteria);
    }
}

/**
 * @brief 从数据库删除玩家的成就数据
 * @param guid 玩家的GUID
 *
 * 静态函数，用于删除角色时清理成就数据
 */
void AchievementMgr::DeleteFromDB(ObjectGuid guid)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 删除已完成成就记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACHIEVEMENT);
    stmt->setUInt32(0, guid.GetCounter());
    trans->Append(stmt);

    // 删除条件进度记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACHIEVEMENT_PROGRESS);
    stmt->setUInt32(0, guid.GetCounter());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 保存成就数据到数据库
 * @param trans 数据库事务
 *
 * 保存所有已修改的成就数据到数据库
 * 包括已完成的成就和条件进度
 */
void AchievementMgr::SaveToDB(CharacterDatabaseTransaction trans)
{
    // 保存已完成的成就
    if (!m_completedAchievements.empty())
    {
        for (std::pair<uint32 const, CompletedAchievementData>& completedAchievement : m_completedAchievements)
        {
            // 只保存已修改的数据
            if (!completedAchievement.second.changed)
                continue;

            // 先删除旧记录
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACHIEVEMENT_BY_ACHIEVEMENT);
            stmt->setUInt16(0, completedAchievement.first);
            stmt->setUInt32(1, GetPlayer()->GetGUID().GetCounter());
            trans->Append(stmt);

            // 插入新记录
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_ACHIEVEMENT);
            stmt->setUInt32(0, GetPlayer()->GetGUID().GetCounter());
            stmt->setUInt16(1, completedAchievement.first);
            stmt->setUInt32(2, uint32(completedAchievement.second.date));
            trans->Append(stmt);

            // 标记为已保存
            completedAchievement.second.changed = false;
        }
    }

    // 保存条件进度
    if (!m_criteriaProgress.empty())
    {
        for (std::pair<uint32 const, CriteriaProgress>& criteriaProgres : m_criteriaProgress)
        {
            // 只保存已修改的数据
            if (!criteriaProgres.second.changed)
                continue;

            // 先删除旧记录
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACHIEVEMENT_PROGRESS_BY_CRITERIA);
            stmt->setUInt32(0, GetPlayer()->GetGUID().GetCounter());
            stmt->setUInt16(1, criteriaProgres.first);
            trans->Append(stmt);

            // 只有进度不为0时才插入
            if (criteriaProgres.second.counter)
            {
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_ACHIEVEMENT_PROGRESS);
                stmt->setUInt32(0, GetPlayer()->GetGUID().GetCounter());
                stmt->setUInt16(1, criteriaProgres.first);
                stmt->setUInt32(2, criteriaProgres.second.counter);
                stmt->setUInt32(3, uint32(criteriaProgres.second.date));
                trans->Append(stmt);
            }

            // 标记为已保存
            criteriaProgres.second.changed = false;
        }
    }
}

/**
 * @brief 从数据库加载成就数据
 * @param achievementResult 已完成成就的查询结果
 * @param criteriaResult 条件进度的查询结果
 *
 * 在玩家登录时调用，加载玩家的成就数据
 * 同时处理称号奖励的回溯授予
 */
void AchievementMgr::LoadFromDB(PreparedQueryResult achievementResult, PreparedQueryResult criteriaResult)
{
    // 加载已完成的成就
    if (achievementResult)
    {
        do
        {
            Field* fields = achievementResult->Fetch();
            uint32 achievementid = fields[0].GetUInt16();

            // 验证成就是否存在（服务器启动时会清理无效数据）
            AchievementEntry const* achievement = sAchievementMgr->GetAchievement(achievementid);
            if (!achievement)
                continue;

            // 存储已完成成就数据
            CompletedAchievementData& ca = m_completedAchievements[achievementid];
            ca.date = time_t(fields[1].GetUInt32());
            ca.changed = false;

            // 回溯授予称号奖励
            // 某些成就在成就系统之前就存在，玩家可能已完成但未获得称号
            if (AchievementReward const* reward = sAchievementMgr->GetAchievementReward(achievement))
                if (uint32 titleId = reward->TitleId[Player::TeamForRace(GetPlayer()->GetRace()) == ALLIANCE ? 0 : 1])
                    if (CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(titleId))
                        GetPlayer()->SetTitle(titleEntry);

        } while (achievementResult->NextRow());
    }

    // 加载条件进度
    if (criteriaResult)
    {
        do
        {
            Field* fields  = criteriaResult->Fetch();
            uint32 id      = fields[0].GetUInt16();
            uint32 counter = fields[1].GetUInt32();
            time_t date    = time_t(fields[2].GetUInt32());

            // 验证条件是否存在
            AchievementCriteriaEntry const* criteria = sAchievementMgr->GetAchievementCriteria(id);
            if (!criteria)
            {
                // 删除不存在的条件数据
                TC_LOG_ERROR("achievement", "Non-existing achievement criteria {} data has been removed from the table `character_achievement_progress`.", id);

                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_INVALID_ACHIEV_PROGRESS_CRITERIA);

                stmt->setUInt16(0, uint16(id));

                CharacterDatabase.Execute(stmt);

                continue;
            }

            // 如果是计时成就且已超时，则不加载
            if (criteria->StartTimer && time_t(date + criteria->StartTimer) < GameTime::GetGameTime())
                continue;

            // 存储条件进度
            CriteriaProgress& progress = m_criteriaProgress[id];
            progress.counter = counter;
            progress.date    = date;
            progress.changed = false;
        } while (criteriaResult->NextRow());
    }
}

/**
 * @brief 发送成就完成通知
 * @param achievement 成就定义条目
 *
 * 向周围玩家、公会成员和全服广播成就完成消息
 * 服务器首杀成就会有特殊处理
 */
void AchievementMgr::SendAchievementEarned(AchievementEntry const* achievement) const
{
    if (GetPlayer()->GetSession()->PlayerLoading())
        return;

    // Don't send for achievements with ACHIEVEMENT_FLAG_HIDDEN
    if (achievement->Flags & ACHIEVEMENT_FLAG_HIDDEN)
        return;

    TC_LOG_DEBUG("achievement", "AchievementMgr::SendAchievementEarned({})", achievement->ID);

    if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
    {
        Trinity::BroadcastTextBuilder _builder(GetPlayer(), CHAT_MSG_GUILD_ACHIEVEMENT, BROADCAST_TEXT_ACHIEVEMENT_EARNED, GetPlayer()->GetNativeGender(), GetPlayer(), achievement->ID);
        Trinity::LocalizedPacketDo<Trinity::BroadcastTextBuilder> _localizer(_builder);
        guild->BroadcastWorker(_localizer, GetPlayer());
    }

    if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_KILL | ACHIEVEMENT_FLAG_REALM_FIRST_REACH))
    {
        uint32 team = GetPlayer()->GetTeam();

        // broadcast realm first reached
        WorldPacket data(SMSG_SERVER_FIRST_ACHIEVEMENT, GetPlayer()->GetName().size() + 1 + 8 + 4 + 4);
        data << GetPlayer()->GetName();
        data << uint64(GetPlayer()->GetGUID());
        data << uint32(achievement->ID);

        std::size_t linkTypePos = data.wpos();
        data << uint32(1);                                  // display name as clickable link in chat
        sWorld->SendGlobalMessage(&data, nullptr, team);

        data.put<uint32>(linkTypePos, 0);                   // display name as plain string in chat
        sWorld->SendGlobalMessage(&data, nullptr, team == ALLIANCE ? HORDE : ALLIANCE);
    }
    // if player is in world he can tell his friends about new achievement
    else if (GetPlayer()->IsInWorld())
    {
        Trinity::BroadcastTextBuilder _builder(GetPlayer(), CHAT_MSG_ACHIEVEMENT, BROADCAST_TEXT_ACHIEVEMENT_EARNED, GetPlayer()->GetNativeGender(), GetPlayer(), achievement->ID);
        Trinity::LocalizedPacketDo<Trinity::BroadcastTextBuilder> _localizer(_builder);
        Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::BroadcastTextBuilder>> _worker(GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY), _localizer);
        Cell::VisitWorldObjects(GetPlayer(), _worker, sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY));
    }

    auto achievementEarnedBuilder = [&](Player const* receiver)
    {
        WowTime now = *GameTime::GetUtcWowTime();
        now += receiver->GetSession()->GetTimezoneOffset();

        WorldPacket data(SMSG_ACHIEVEMENT_EARNED, 8 + 4 + 8);
        data << GetPlayer()->GetPackGUID();
        data << uint32(achievement->ID);
        data << now;
        data << uint32(0);
        receiver->SendDirectMessage(&data);
    };

    float dist = sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY);
    Trinity::PlayerDistWorker notifier(GetPlayer(), dist, achievementEarnedBuilder);
    Cell::VisitWorldObjects(GetPlayer(), notifier, dist);
}

void AchievementMgr::SendCriteriaUpdate(AchievementCriteriaEntry const* entry, CriteriaProgress const* progress, uint32 timeElapsed, bool timedCompleted) const
{
    WowTime date;
    date.SetUtcTimeFromUnixTime(progress->date);
    date += GetPlayer()->GetSession()->GetTimezoneOffset();

    WorldPacket data(SMSG_CRITERIA_UPDATE, 8 + 4 + 8);
    data << uint32(entry->ID);

    // 计数器使用压缩格式发送（类似压缩GUID）
    data.appendPackGUID(progress->counter);

    data << GetPlayer()->GetPackGUID();
    if (!entry->StartTimer)
        data << uint32(0);
    else
        data << uint32(timedCompleted ? 1 : 0); // 标志位，1表示在客户端保持计数器为0
    data << date;
    data << uint32(timeElapsed);    // 已经过的时间（秒）
    data << uint32(0);              // 未知字段
    GetPlayer()->SendDirectMessage(&data);
}

/**
 * @brief 检查所有成就条件
 *
 * 在玩家登录时调用，检查所有可能已完成的成就
 * 用于处理成就系统禁用期间可能错过的成就
 */
void AchievementMgr::CheckAllAchievementCriteria()
{
    // 抑制发送数据包，静默检查所有条件类型
    for (uint32 i = 0; i < ACHIEVEMENT_CRITERIA_TYPE_TOTAL; ++i)
        UpdateAchievementCriteria(AchievementCriteriaTypes(i));
}

/// 竞技场槽位对应的成就ID数组
static const uint32 achievIdByArenaSlot[MAX_ARENA_SLOT] = { 1057, 1107, 1108 };

/**
 * @brief 更新成就条件进度
 * @param type 条件类型
 * @param miscValue1 杂项值1（根据条件类型有不同含义）
 * @param miscValue2 杂项值2（根据条件类型有不同含义）
 * @param ref 相关的世界对象引用
 *
 * 这是成就系统的核心函数，在玩家执行相关动作时被调用
 * 例如：击杀生物、完成任务、获得物品等
 *
 * @note 此函数会被频繁调用，性能敏感
 */
void AchievementMgr::UpdateAchievementCriteria(AchievementCriteriaTypes type, uint32 miscValue1 /*= 0*/, uint32 miscValue2 /*= 0*/, WorldObject* ref /*= nullptr*/)
{
    // 验证条件类型是否有效
    if (type >= ACHIEVEMENT_CRITERIA_TYPE_TOTAL)
    {
        TC_LOG_DEBUG("achievement", "UpdateAchievementCriteria: Wrong criteria type {}", type);
        return;
    }

    // GM模式或没有成就权限的玩家不处理
    if (m_player->IsGameMaster() || m_player->GetSession()->HasPermission(rbac::RBAC_PERM_CANNOT_EARN_ACHIEVEMENTS))
    {
        TC_LOG_DEBUG("achievement", "UpdateAchievementCriteria: [Player {} {}] {}, {} ({}), {}, {}"
            , m_player->GetName(), m_player->IsGameMaster() ? "GM mode on" : "disallowed by RBAC", m_player->GetGUID().ToString(), AchievementGlobalMgr::GetCriteriaTypeString(type), type, miscValue1, miscValue2);
        return;
    }

    TC_LOG_DEBUG("achievement", "UpdateAchievementCriteria: {}, {} ({}), {}, {}"
        , m_player->GetGUID().ToString(), AchievementGlobalMgr::GetCriteriaTypeString(type), type, miscValue1, miscValue2);

    // 获取符合条件的成就条件列表
    AchievementCriteriaEntryList const& achievementCriteriaList = sAchievementMgr->GetAchievementCriteriaByType(type, miscValue1);
    for (AchievementCriteriaEntry const* achievementCriteria : achievementCriteriaList)
    {
        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(achievementCriteria->AchievementID);
        if (!CanUpdateCriteria(achievementCriteria, achievement, miscValue1, miscValue2, ref))
            continue;

        switch (type)
        {
            // special cases, db data is checked later
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
                break;
            default:
                if (AchievementCriteriaDataSet const* data = sAchievementMgr->GetCriteriaDataSet(achievementCriteria))
                    if (!data->Meets(GetPlayer(), ref, miscValue1, miscValue2))
                        continue;
                break;
        }

        switch (type)
        {
            // std. case: increment at 1
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND:
            case ACHIEVEMENT_CRITERIA_TYPE_DEATH_AT_MAP:
            case ACHIEVEMENT_CRITERIA_TYPE_DEATH:
            case ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON:
            case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE:
            case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER:
            case ACHIEVEMENT_CRITERIA_TYPE_DEATHS_FROM:
            case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
            case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
            case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
            case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA:
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA: // This also behaves like ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA
            case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:
            case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS:
            case ACHIEVEMENT_CRITERIA_TYPE_HK_RACE:
            case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:
            case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:
            case ACHIEVEMENT_CRITERIA_TYPE_NUMBER_OF_TALENT_RESETS:
            case ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT:
            case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
            case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:
            case ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT:
            case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:
            case ACHIEVEMENT_CRITERIA_TYPE_LOSE_DUEL:
            case ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION:
            case ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS:            /* FIXME: for online player only currently */
            case ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED:
            case ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED:
            case ACHIEVEMENT_CRITERIA_TYPE_FLIGHT_PATHS_TAKEN:
            case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
            case ACHIEVEMENT_CRITERIA_TYPE_ACCEPTED_SUMMONINGS:
                SetCriteriaProgress(achievementCriteria, 1, PROGRESS_ACCUMULATE);
                break;
            // std case: increment at miscvalue1
            case ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE:
            case ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE:
            case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS:
            case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TALENTS:
            case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD:
            case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TRAVELLING:
            case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER:
            case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL:
            case ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY:
            case ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS: /* FIXME: for online player only currently */
            case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED:
            case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_HEALING_RECEIVED:
            case ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS:
                SetCriteriaProgress(achievementCriteria, miscValue1, PROGRESS_ACCUMULATE);
                break;
            // std case: increment at miscvalue2
            case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
            case ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:
            case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:
                SetCriteriaProgress(achievementCriteria, miscValue2, PROGRESS_ACCUMULATE);
                break;
            // std case: high value at miscvalue1
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD: /* FIXME: for online player only currently */
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_DEALT:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_RECEIVED:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEAL_CAST:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALING_RECEIVED:
                SetCriteriaProgress(achievementCriteria, miscValue1, PROGRESS_HIGHEST);
                break;
            // std. case: set at 1
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
            case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL:
            case ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA:
            case ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP:
            case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT:
                SetCriteriaProgress(achievementCriteria, 1, PROGRESS_SET);
                break;

            // specialized cases
            case ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetLevel());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL:
                if (uint32 skillvalue = GetPlayer()->GetBaseSkillValue(achievementCriteria->Asset.SkillID))
                    SetCriteriaProgress(achievementCriteria, skillvalue);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL:
                if (uint32 maxSkillvalue = GetPlayer()->GetPureMaxSkillValue(achievementCriteria->Asset.SkillID))
                    SetCriteriaProgress(achievementCriteria, maxSkillvalue);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetRewardedQuestCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST_DAILY:
            {
                time_t nextDailyResetTime = sWorld->GetNextDailyQuestsResetTime();
                CriteriaProgress const* progress = GetCriteriaProgress(achievementCriteria);

                if (!miscValue1) // Login case.
                {
                    // reset if player missed one day.
                    if (progress && progress->date < (nextDailyResetTime - 2 * DAY))
                        SetCriteriaProgress(achievementCriteria, 0, PROGRESS_SET);
                    continue;
                }

                ProgressType progressType;
                if (!progress)
                    // 1st time. Start count.
                    progressType = PROGRESS_SET;
                else if (progress->date < (nextDailyResetTime - 2 * DAY))
                    // last progress is older than 2 days. Player missed 1 day => Restart count.
                    progressType = PROGRESS_SET;
                else if (progress->date < (nextDailyResetTime - DAY))
                    // last progress is between 1 and 2 days. => 1st time of the day.
                    progressType = PROGRESS_ACCUMULATE;
                else
                    // last progress is within the day before the reset => Already counted today.
                    continue;

                SetCriteriaProgress(achievementCriteria, 1, progressType);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE:
            {
                uint32 counter = 0;

                RewardedQuestSet const& rewQuests = GetPlayer()->getRewardedQuests();
                for (uint32 rewQuest : rewQuests)
                {
                    Quest const* quest = sObjectMgr->GetQuestTemplate(rewQuest);
                    if (quest && quest->GetZoneOrSort() >= 0 && uint32(quest->GetZoneOrSort()) == achievementCriteria->Asset.ZoneID)
                        ++counter;
                }
                SetCriteriaProgress(achievementCriteria, counter);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:
                // miscvalue1 is the ingame fallheight*100 as stored in dbc
                SetCriteriaProgress(achievementCriteria, miscValue1);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:
                // additional requirements
                if (achievementCriteria->AdditionalRequirements[0].Type == ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE)
                {
                    // those requirements couldn't be found in the dbc
                    AchievementCriteriaDataSet const* data = sAchievementMgr->GetCriteriaDataSet(achievementCriteria);
                    if (!data || !data->Meets(GetPlayer(), ref, miscValue1))
                    {
                        // reset the progress as we have a win without the requirement.
                        SetCriteriaProgress(achievementCriteria, 0);
                        continue;
                    }
                }

                SetCriteriaProgress(achievementCriteria, 1, PROGRESS_ACCUMULATE);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetBankBagSlotCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION:
            {
                int32 reputation = GetPlayer()->GetReputationMgr().GetReputation(achievementCriteria->Asset.FactionID);
                if (reputation > 0)
                    SetCriteriaProgress(achievementCriteria, reputation);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetReputationMgr().GetExaltedFactionCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(miscValue1);
                if (!proto)
                    continue;

                // check item level via achievement_criteria_data
                AchievementCriteriaDataSet const* data = sAchievementMgr->GetCriteriaDataSet(achievementCriteria);
                if (!data || !data->Meets(GetPlayer(), nullptr, proto->ItemLevel))
                    continue;

                SetCriteriaProgress(achievementCriteria, 1, PROGRESS_ACCUMULATE);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS:
            {
                uint32 spellCount = 0;
                for (std::pair<uint32 const, PlayerSpell> const& spellIter : GetPlayer()->GetSpellMap())
                {
                    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellIter.first);
                    for (SkillLineAbilityMap::const_iterator skillIter = bounds.first; skillIter != bounds.second; ++skillIter)
                    {
                        if (skillIter->second->SkillLine == achievementCriteria->Asset.SkillID)
                        {
                            // do not add couter twice if by any chance skill is listed twice in dbc (eg. skill 777 and spell 22717)
                            ++spellCount;
                            break;
                        }
                    }
                }
                SetCriteriaProgress(achievementCriteria, spellCount);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetReputationMgr().GetReveredFactionCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetReputationMgr().GetHonoredFactionCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetReputationMgr().GetVisibleFactionCount());
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE:
            {
                uint32 spellCount = 0;
                for (std::pair<uint32 const, PlayerSpell> const& spellIter : GetPlayer()->GetSpellMap())
                {
                    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellIter.first);
                    for (SkillLineAbilityMap::const_iterator skillIter = bounds.first; skillIter != bounds.second; ++skillIter)
                    {
                        if (skillIter->second->SkillLine == achievementCriteria->Asset.SkillID)
                        {
                            // do not add couter twice if by any chance skill is listed twice in dbc (eg. skill 777 and spell 22717)
                            ++spellCount;
                            break;
                        }
                    }
                }
                SetCriteriaProgress(achievementCriteria, spellCount);
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS));
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_GOLD_VALUE_OWNED:
                SetCriteriaProgress(achievementCriteria, GetPlayer()->GetMoney(), PROGRESS_HIGHEST);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS:
                if (!miscValue1)
                {
                    uint32 points = 0;
                    for (std::pair<uint32 const, CompletedAchievementData> const& completedAchievement : m_completedAchievements)
                        if (AchievementEntry const* completedAchievements = sAchievementMgr->GetAchievement(completedAchievement.first))
                            points += completedAchievements->Points;
                    SetCriteriaProgress(achievementCriteria, points, PROGRESS_SET);
                }
                else
                    SetCriteriaProgress(achievementCriteria, miscValue1, PROGRESS_ACCUMULATE);
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING:
            {
                uint32 reqTeamType = achievementCriteria->Asset.TeamType;

                if (miscValue1)
                {
                    if (miscValue2 != reqTeamType)
                        continue;

                    SetCriteriaProgress(achievementCriteria, miscValue1, PROGRESS_HIGHEST);
                }
                else // login case
                {
                    for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                    {
                        uint32 teamId = GetPlayer()->GetArenaTeamId(arena_slot);
                        if (!teamId)
                            continue;

                        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(teamId);
                        if (!team || team->GetType() != reqTeamType)
                            continue;

                        SetCriteriaProgress(achievementCriteria, team->GetStats().Rating, PROGRESS_HIGHEST);
                        break;
                    }
                }
                break;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING:
            {
                uint32 reqTeamType = achievementCriteria->Asset.TeamType;

                if (miscValue1)
                {
                    if (miscValue2 != reqTeamType)
                        continue;

                    SetCriteriaProgress(achievementCriteria, miscValue1, PROGRESS_HIGHEST);
                }
                else // login case
                {
                    for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                    {
                        uint32 teamId = GetPlayer()->GetArenaTeamId(arena_slot);
                        if (!teamId)
                            continue;

                        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(teamId);
                        if (!team || team->GetType() != reqTeamType)
                            continue;

                        if (ArenaTeamMember const* member = team->GetMember(GetPlayer()->GetGUID()))
                        {
                            SetCriteriaProgress(achievementCriteria, member->PersonalRating, PROGRESS_HIGHEST);
                            break;
                        }
                    }
                }

                break;
            }
            // std case: does not exist in DBC, not triggered in code as result
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALTH:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_SPELLPOWER:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_ARMOR:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_POWER:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_STAT:
            case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_RATING:
                break;
            // FIXME: not triggered in code as result, need to implement
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_RAID:
            case ACHIEVEMENT_CRITERIA_TYPE_PLAY_ARENA:
            case ACHIEVEMENT_CRITERIA_TYPE_OWN_RANK:
                break;                                   // Not implemented yet :(
        }

        if (IsCompletedCriteria(achievementCriteria, achievement))
            CompletedCriteriaFor(achievement);

        // check again the completeness for SUMM and REQ COUNT achievements,
        // as they don't depend on the completed criteria but on the sum of the progress of each individual criteria
        if (achievement->Flags & ACHIEVEMENT_FLAG_SUMM)
            if (IsCompletedAchievement(achievement))
                CompletedAchievement(achievement);

        if (AchievementEntryList const* achRefList = sAchievementMgr->GetAchievementByReferencedId(achievement->ID))
            for (AchievementEntry const* achievement : *achRefList)
                if (IsCompletedAchievement(achievement))
                    CompletedAchievement(achievement);
    }
}

bool AchievementMgr::IsCompletedCriteria(AchievementCriteriaEntry const* achievementCriteria, AchievementEntry const* achievement)
{
    if (!achievement)
        return false;

    // counter can never complete
    if (achievement->Flags & ACHIEVEMENT_FLAG_COUNTER)
        return false;

    if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
    {
        // someone on this realm has already completed that achievement
        if (sAchievementMgr->IsRealmCompleted(achievement))
            return false;
    }

    CriteriaProgress const* progress = GetCriteriaProgress(achievementCriteria);
    if (!progress)
        return false;

    switch (achievementCriteria->Type)
    {
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST_DAILY:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE:
        case ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE:
        case ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:
        case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
        case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA:
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP:
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
        case ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS:
        case ACHIEVEMENT_CRITERIA_TYPE_HK_RACE:
        case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT:
        case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS:
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS:
        case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:
            return progress->counter >= achievementCriteria->Quantity;
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL:
        case ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA:
            return progress->counter >= 1;
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL:
            return progress->counter >= (achievementCriteria->Quantity * 75);
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS:
            return progress->counter >= 9000;
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA:
            return achievementCriteria->Quantity && progress->counter >= achievementCriteria->Quantity;
        case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:
            return true;
        // handle all statistic-only criteria here
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND:
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_AT_MAP:
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH:
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON:
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE:
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER:
        case ACHIEVEMENT_CRITERIA_TYPE_DEATHS_FROM:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING:
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TALENTS:
        case ACHIEVEMENT_CRITERIA_TYPE_NUMBER_OF_TALENT_RESETS:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL:
        case ACHIEVEMENT_CRITERIA_TYPE_LOSE_DUEL:
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_GOLD_VALUE_OWNED:
        case ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALTH:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_SPELLPOWER:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_ARMOR:
        case ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED:
        case ACHIEVEMENT_CRITERIA_TYPE_FLIGHT_PATHS_TAKEN:
        case ACHIEVEMENT_CRITERIA_TYPE_ACCEPTED_SUMMONINGS:
        default:
            break;
    }
    return false;
}

void AchievementMgr::CompletedCriteriaFor(AchievementEntry const* achievement)
{
    // counter can never complete
    if (achievement->Flags & ACHIEVEMENT_FLAG_COUNTER)
        return;

    // already completed and stored
    if (HasAchieved(achievement->ID))
        return;

    if (IsCompletedAchievement(achievement))
        CompletedAchievement(achievement);
}

bool AchievementMgr::IsCompletedAchievement(AchievementEntry const* entry)
{
    // counter can never complete
    if (entry->Flags & ACHIEVEMENT_FLAG_COUNTER)
        return false;

    // for achievement with referenced achievement criterias get from referenced and counter from self
    uint32 achievementForTestId = entry->SharesCriteria ? entry->SharesCriteria : entry->ID;
    uint32 achievementForTestCount = entry->MinimumCriteria;

    AchievementCriteriaEntryList const* cList = sAchievementMgr->GetAchievementCriteriaByAchievement(achievementForTestId);
    if (!cList)
        return false;
    uint32 count = 0;

    // For SUMM achievements, we have to count the progress of each criteria of the achievement.
    // Oddly, the target count is NOT contained in the achievement, but in each individual criteria
    if (entry->Flags & ACHIEVEMENT_FLAG_SUMM)
    {
        for (AchievementCriteriaEntry const* criteria : *cList)
        {
            CriteriaProgress const* progress = GetCriteriaProgress(criteria);
            if (!progress)
                continue;

            count += progress->counter;

            // for counters, field4 contains the main count requirement
            if (count >= criteria->Quantity)
                return true;
        }
        return false;
    }

    // Default case - need complete all or
    bool completed_all = true;
    for (AchievementCriteriaEntry const* criteria : *cList)
    {
        bool completed = IsCompletedCriteria(criteria, entry);

        // found an uncompleted criteria, but DONT return false yet - there might be a completed criteria with ACHIEVEMENT_CRITERIA_COMPLETE_FLAG_ALL
        if (completed)
            ++count;
        else
            completed_all = false;

        // completed as have req. count of completed criterias
        if (achievementForTestCount > 0 && achievementForTestCount <= count)
           return true;
    }

    // all criterias completed requirement
    if (completed_all && achievementForTestCount == 0)
        return true;

    return false;
}

CriteriaProgress* AchievementMgr::GetCriteriaProgress(AchievementCriteriaEntry const* entry)
{
    CriteriaProgressMap::iterator iter = m_criteriaProgress.find(entry->ID);

    if (iter == m_criteriaProgress.end())
        return nullptr;

    return &(iter->second);
}

void AchievementMgr::SetCriteriaProgress(AchievementCriteriaEntry const* entry, uint32 changeValue, ProgressType ptype)
{
    // Don't allow to cheat - doing timed achievements without timer active
    TimedAchievementMap::iterator timedIter = m_timedAchievements.find(entry->ID);
    if (entry->StartTimer && timedIter == m_timedAchievements.end())
        return;

    TC_LOG_DEBUG("achievement", "AchievementMgr::SetCriteriaProgress({}, {}) for {}", entry->ID, changeValue, m_player->GetGUID().ToString());

    CriteriaProgress* progress = GetCriteriaProgress(entry);
    if (!progress)
    {
        // not create record for 0 counter but allow it for timed achievements
        // we will need to send 0 progress to client to start the timer
        if (changeValue == 0 && !entry->StartTimer)
            return;

        progress = &m_criteriaProgress[entry->ID];
        progress->counter = changeValue;
    }
    else
    {
        uint32 newValue = 0;
        switch (ptype)
        {
            case PROGRESS_SET:
                newValue = changeValue;
                break;
            case PROGRESS_ACCUMULATE:
            {
                // avoid overflow
                uint32 max_value = std::numeric_limits<uint32>::max();
                newValue = max_value - progress->counter > changeValue ? progress->counter + changeValue : max_value;
                break;
            }
            case PROGRESS_HIGHEST:
                newValue = progress->counter < changeValue ? changeValue : progress->counter;
                break;
        }

        // not update (not mark as changed) if counter will have same value
        if (progress->counter == newValue && !entry->StartTimer)
            return;

        progress->counter = newValue;
    }

    progress->changed = true;
    progress->date = GameTime::GetGameTime(); // set the date to the latest update.

    uint32 timeElapsed = 0;
    bool timedCompleted = false;

    if (entry->StartTimer)
    {
        // has to exist, otherwise we wouldn't be here
        timedCompleted = IsCompletedCriteria(entry, sAchievementMgr->GetAchievement(entry->AchievementID));
        // Client expects this in packet
        timeElapsed = entry->StartTimer - (timedIter->second/IN_MILLISECONDS);

        // Remove the timer, we wont need it anymore
        if (timedCompleted)
            m_timedAchievements.erase(timedIter);
    }

    SendCriteriaUpdate(entry, progress, timeElapsed, timedCompleted);
}

void AchievementMgr::RemoveCriteriaProgress(AchievementCriteriaEntry const* entry)
{
    if (!entry)
        return;

    CriteriaProgressMap::iterator criteriaProgress = m_criteriaProgress.find(entry->ID);
    if (criteriaProgress == m_criteriaProgress.end())
        return;

    WorldPacket data(SMSG_CRITERIA_DELETED, 4);
    data << uint32(entry->ID);
    m_player->SendDirectMessage(&data);

    m_criteriaProgress.erase(criteriaProgress);
}

void AchievementMgr::UpdateTimedAchievements(uint32 timeDiff)
{
    if (!m_timedAchievements.empty())
    {
        for (TimedAchievementMap::iterator itr = m_timedAchievements.begin(); itr != m_timedAchievements.end();)
        {
            // Time is up, remove timer and reset progress
            if (itr->second <= timeDiff)
            {
                AchievementCriteriaEntry const* entry = sAchievementMgr->GetAchievementCriteria(itr->first);
                RemoveCriteriaProgress(entry);
                m_timedAchievements.erase(itr++);
            }
            else
            {
                itr->second -= timeDiff;
                ++itr;
            }
        }
    }
}

void AchievementMgr::StartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry, uint32 timeLost /*= 0*/)
{
    for (AchievementCriteriaEntry const* criteria : sAchievementMgr->GetTimedAchievementCriteriaByType(type))
    {
        if (criteria->StartAsset != entry)
            continue;

        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(criteria->AchievementID);
        if (m_timedAchievements.find(criteria->ID) == m_timedAchievements.end() && !IsCompletedCriteria(criteria, achievement))
        {
            // Start the timer
            if (criteria->StartTimer * IN_MILLISECONDS > timeLost)
            {
                m_timedAchievements[criteria->ID] = criteria->StartTimer * IN_MILLISECONDS - timeLost;

                // and at client too
                SetCriteriaProgress(criteria, 0, PROGRESS_SET);
            }
        }
    }
}

void AchievementMgr::RemoveTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry)
{
    for (AchievementCriteriaEntry const* criteria : sAchievementMgr->GetTimedAchievementCriteriaByType(type))
    {
        if (criteria->StartAsset != entry)
            continue;

        TimedAchievementMap::iterator timedIter = m_timedAchievements.find(criteria->ID);
        // We don't have timer for this achievement
        if (timedIter == m_timedAchievements.end())
            continue;

        // remove progress
        RemoveCriteriaProgress(criteria);

        // Remove the timer
        m_timedAchievements.erase(timedIter);
    }
}

/**
 * @brief 完成指定成就
 * @param achievement 成就定义条目
 *
 * 处理成就完成逻辑：
 * - 发送通知消息
 * - 记录完成时间
 * - 发放奖励（称号、物品等）
 * - 触发关联成就检查
 */
void AchievementMgr::CompletedAchievement(AchievementEntry const* achievement)
{
    // GM模式或没有成就权限的玩家不能完成成就
    if (m_player->IsGameMaster() || m_player->GetSession()->HasPermission(rbac::RBAC_PERM_CANNOT_EARN_ACHIEVEMENTS))
        return;

    // 计数器类型成就或已完成的成就不再处理
    if (achievement->Flags & ACHIEVEMENT_FLAG_COUNTER || HasAchieved(achievement->ID))
        return;

    TC_LOG_INFO("achievement", "AchievementMgr::CompletedAchievement({}). Player: {} {}",
        achievement->ID, m_player->GetName(), m_player->GetGUID().ToString());

    // 发送成就完成通知
    SendAchievementEarned(achievement);

    // 记录完成数据
    CompletedAchievementData& ca = m_completedAchievements[achievement->ID];
    ca.date = GameTime::GetGameTime();
    ca.changed = true;

    // 服务器首杀成就需要全局标记
    if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
        sAchievementMgr->SetRealmCompleted(achievement);

    // 触发"完成成就"类型的条件更新
    UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT, achievement->ID);
    // 触发"获得成就点数"类型的条件更新
    UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS, achievement->Points);

    // 处理奖励
    AchievementReward const* reward = sAchievementMgr->GetAchievementReward(achievement);

    // 无奖励则直接返回
    if (!reward)
        return;

    // 授予称号
    // 目前只有一个成就(1793)涉及性别特定称号
    // 由于没有找到通用属性，这里通过ID显式检查
    // 未来可能将条件字段移至条件系统
    if (uint32 titleId = reward->TitleId[achievement->ID == 1793 ? GetPlayer()->GetNativeGender() : (GetPlayer()->GetTeam() == ALLIANCE ? 0 : 1)])
        if (CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(titleId))
            GetPlayer()->SetTitle(titleEntry);

    // 发送奖励邮件
    if (reward->SenderCreatureId)
    {
        MailDraft draft(reward->MailTemplateId);

        // 如果没有使用邮件模板，则使用自定义主题和正文
        if (!reward->MailTemplateId)
        {
            std::string subject = reward->Subject;
            std::string text = reward->Body;

            // 处理本地化文本
            LocaleConstant localeConstant = GetPlayer()->GetSession()->GetSessionDbLocaleIndex();
            if (localeConstant != LOCALE_enUS)
            {
                if (AchievementRewardLocale const* loc = sAchievementMgr->GetAchievementRewardLocale(achievement))
                {
                    ObjectMgr::GetLocaleString(loc->Subject, localeConstant, subject);
                    ObjectMgr::GetLocaleString(loc->Text,    localeConstant, text);
                }
            }

            draft = MailDraft(subject, text);
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 创建奖励物品
        Item* item = reward->ItemId ? Item::CreateItem(reward->ItemId, 1, GetPlayer()) : nullptr;
        if (item)
        {
            // 发送前先保存物品，防止邮件加载时丢失
            // 如果发送失败，物品将被删除
            item->SaveToDB(trans);

            draft.AddItem(item);
        }

        // 发送邮件
        draft.SendMailTo(trans, GetPlayer(), MailSender(MAIL_CREATURE, reward->SenderCreatureId));
        CharacterDatabase.CommitTransaction(trans);
    }
}

void AchievementMgr::SendAllAchievementData() const
{
    WorldPacket data(SMSG_ALL_ACHIEVEMENT_DATA, m_completedAchievements.size() * 8 + 4 + m_criteriaProgress.size() * 38 + 4);
    BuildAllDataPacket(GetPlayer(), &data);
    GetPlayer()->SendDirectMessage(&data);
}

void AchievementMgr::SendRespondInspectAchievements(Player* player) const
{
    WorldPacket data(SMSG_RESPOND_INSPECT_ACHIEVEMENTS, 9 + m_completedAchievements.size() * 8 + 4 + m_criteriaProgress.size() * 38 + 4);
    data << GetPlayer()->GetPackGUID();
    BuildAllDataPacket(player, &data);
    player->SendDirectMessage(&data);
}

/**
 * used by SMSG_RESPOND_INSPECT_ACHIEVEMENT and SMSG_ALL_ACHIEVEMENT_DATA
 */
void AchievementMgr::BuildAllDataPacket(Player const* receiver, WorldPacket* data) const
{
    for (std::pair<uint32 const, CompletedAchievementData> const& completedAchievement : m_completedAchievements)
    {
        // Skip hidden achievements
        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(completedAchievement.first);
        if (!achievement || achievement->Flags & ACHIEVEMENT_FLAG_HIDDEN)
            continue;

        WowTime date;
        date.SetUtcTimeFromUnixTime(completedAchievement.second.date);
        date += receiver->GetSession()->GetTimezoneOffset();

        *data << uint32(completedAchievement.first);
        *data << date;
    }
    *data << int32(-1);

    for (std::pair<uint32 const, CriteriaProgress> const& criteriaProgress : m_criteriaProgress)
    {
        WowTime date;
        date.SetUtcTimeFromUnixTime(criteriaProgress.second.date);
        date += receiver->GetSession()->GetTimezoneOffset();

        *data << uint32(criteriaProgress.first);
        data->appendPackGUID(criteriaProgress.second.counter);
        *data << GetPlayer()->GetPackGUID();
        *data << uint32(0);
        *data << date;
        *data << uint32(0);
        *data << uint32(0);
    }

    *data << int32(-1);
}

bool AchievementMgr::HasAchieved(uint32 achievementId) const
{
    return m_completedAchievements.find(achievementId) != m_completedAchievements.end();
}

bool AchievementMgr::CanUpdateCriteria(AchievementCriteriaEntry const* criteria, AchievementEntry const* achievement, uint32 miscValue1, uint32 miscValue2, WorldObject const* ref)
{
    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_ACHIEVEMENT_CRITERIA, criteria->ID, nullptr))
    {
        TC_LOG_TRACE("achievement", "CanUpdateCriteria: (Id: {} Type {}) Disabled",
            criteria->ID, AchievementGlobalMgr::GetCriteriaTypeString(criteria->Type));
        return false;
    }

    if (achievement->InstanceID != -1 && GetPlayer()->GetMapId() != uint32(achievement->InstanceID))
    {
        TC_LOG_TRACE("achievement", "CanUpdateCriteria: (Id: {} Type {} Achievement {}) Wrong map",
            criteria->ID, AchievementGlobalMgr::GetCriteriaTypeString(criteria->Type), achievement->ID);
        return false;
    }

    if ((achievement->Faction == ACHIEVEMENT_FACTION_HORDE    && GetPlayer()->GetTeam() != HORDE) ||
        (achievement->Faction == ACHIEVEMENT_FACTION_ALLIANCE && GetPlayer()->GetTeam() != ALLIANCE))
    {
        TC_LOG_TRACE("achievement", "CanUpdateCriteria: (Id: {} Type {} Achievement {}) Wrong faction",
            criteria->ID, AchievementGlobalMgr::GetCriteriaTypeString(criteria->Type), achievement->ID);
        return false;
    }

    if (!RequirementsSatisfied(criteria, achievement, miscValue1, miscValue2, ref))
    {
        TC_LOG_TRACE("achievement", "CanUpdateCriteria: (Id: {} Type {}) Requirements have not been satisfied",
            criteria->ID, AchievementGlobalMgr::GetCriteriaTypeString(criteria->Type));
        return false;
    }

    if (!ConditionsSatisfied(criteria))
    {
        TC_LOG_TRACE("achievement", "CanUpdateCriteria: (Id: {} Type {}) Conditions have not been satisfied",
            criteria->ID, AchievementGlobalMgr::GetCriteriaTypeString(criteria->Type));
        return false;
    }

    // Don't update realm first achievements if the player's account isn't allowed to do so
    if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
        if (GetPlayer()->GetSession()->HasPermission(rbac::RBAC_PERM_CANNOT_EARN_REALM_FIRST_ACHIEVEMENTS))
            return false;

    // don't update already completed criteria
    if (IsCompletedCriteria(criteria, achievement))
        return false;

    return true;
}

bool AchievementMgr::ConditionsSatisfied(AchievementCriteriaEntry const* criteria) const
{
    for (auto AdditionalRequirement : criteria->AdditionalRequirements)
    {
        if (!AdditionalRequirement.Type)
            continue;

        switch (AdditionalRequirement.Type)
        {
            case ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP:
                if (GetPlayer()->GetMapId() != AdditionalRequirement.Asset)
                    return false;
                break;
            case ACHIEVEMENT_CRITERIA_CONDITION_NOT_IN_GROUP:
                if (GetPlayer()->GetGroup())
                    return false;
                break;
            default:
                break;
        }
    }

    return true;
}

bool AchievementMgr::RequirementsSatisfied(AchievementCriteriaEntry const* achievementCriteria, AchievementEntry const* achievement, uint32 miscValue1, uint32 miscValue2, WorldObject const* ref) const
{
    switch (AchievementCriteriaTypes(achievementCriteria->Type))
    {
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:
        case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:
        case ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP:
        case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TALENTS:
        case ACHIEVEMENT_CRITERIA_TYPE_NUMBER_OF_TALENT_RESETS:
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TRAVELLING:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY:
        case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:
        case ACHIEVEMENT_CRITERIA_TYPE_LOSE_DUEL:
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID:
        case ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_DEALT:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_RECEIVED:
        case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEAL_CAST:
        case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_HEALING_RECEIVED:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALING_RECEIVED:
        case ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED:
        case ACHIEVEMENT_CRITERIA_TYPE_FLIGHT_PATHS_TAKEN:
        case ACHIEVEMENT_CRITERIA_TYPE_ACCEPTED_SUMMONINGS:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS:
            if (!miscValue1)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST_DAILY:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING:
        case ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS:
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL:
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_GOLD_VALUE_OWNED:
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS:
            break;

        // specialized cases
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT:
            if ((miscValue1 && achievementCriteria->Asset.AchievementID != miscValue1) || (!miscValue1 && m_completedAchievements.find(achievementCriteria->Asset.AchievementID) == m_completedAchievements.end()))
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND:
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_AT_MAP:
            if (!miscValue1 || achievementCriteria->Asset.MapID != GetPlayer()->GetMapId())
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE:
            if (!miscValue1 || achievementCriteria->Asset.CreatureID != miscValue1)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL:
            if (miscValue1 && miscValue1 != achievementCriteria->Asset.SkillID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE:
            if (miscValue1 && miscValue1 != achievementCriteria->Asset.ZoneID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH:
            if (!miscValue1)
                return false;
            for (uint8 j = 0; j < MAX_ARENA_SLOT; ++j)
            {
                if (achievIdByArenaSlot[j] == achievement->ID)
                {
                    Battleground* bg = GetPlayer()->GetBattleground();
                    if (!bg || !bg->isArena() || ArenaTeam::GetSlotByType(bg->GetArenaType()) != j)
                        return false;
                    break;
                }
            }
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON:
        {
            if (!miscValue1)
                return false;

            Map const* map = GetPlayer()->IsInWorld() ? GetPlayer()->GetMap() : sMapMgr->FindMap(GetPlayer()->GetMapId(), GetPlayer()->GetInstanceId());
            if (!map || !map->IsDungeon())
                return false;

            //FIXME: work only for instances where max == min for players
            if (map->ToInstanceMap()->GetMaxPlayers() != achievementCriteria->Asset.GroupSize)
                return false;
            break;
        }
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER:
            if (!miscValue1)
                return false;
            // if team check required: must kill by opposition faction
            if (achievement->ID == 318 && miscValue2 == GetPlayer()->GetTeam())
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_DEATHS_FROM:
            if (!miscValue1 || miscValue2 != achievementCriteria->Asset.DamageType)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
            // if miscvalues != 0, it contains the questID.
            if (miscValue1)
            {
                if (miscValue1 != achievementCriteria->Asset.QuestID)
                    return false;
            }
            else
            {
                // login case.
                if (!GetPlayer()->GetQuestRewardStatus(achievementCriteria->Asset.QuestID))
                    return false;
            }
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
            if (!miscValue1 || miscValue1 != achievementCriteria->Asset.SpellID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL:
            if (miscValue1 && miscValue1 != achievementCriteria->Asset.SpellID)
                return false;
            if (!GetPlayer()->HasSpell(achievementCriteria->Asset.SpellID))
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:
            // miscvalue1=loot_type (note: 0 = LOOT_CORPSE and then it ignored)
            // miscvalue2=count of item loot
            if (!miscValue1 || !miscValue2)
                return false;
            if (miscValue1 != achievementCriteria->Asset.LootType)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM:
            if (miscValue1 && achievementCriteria->Asset.ItemID != miscValue1)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM:
            if (!miscValue1)
                return false;
            if (miscValue1 != achievementCriteria->Asset.ItemID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA:
        {
            WorldMapOverlayEntry const* worldOverlayEntry = sWorldMapOverlayStore.LookupEntry(achievementCriteria->Asset.WorldMapOverlayID);
            if (!worldOverlayEntry)
                return false;

            bool matchFound = false;
            for (uint32 j : worldOverlayEntry->AreaID)
            {
                AreaTableEntry const* area = sAreaTableStore.LookupEntry(j);
                if (!area)
                    break;

                uint32 playerIndexOffset = uint32(area->AreaBit) / 32;
                if (playerIndexOffset >= PLAYER_EXPLORED_ZONES_SIZE)
                    continue;

                uint32 mask = 1 << (uint32(area->AreaBit) % 32);
                if (GetPlayer()->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + playerIndexOffset) & mask)
                {
                    matchFound = true;
                    break;
                }
            }

            if (!matchFound)
                return false;
            break;
        }
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION:
            if (miscValue1 && miscValue1 != achievementCriteria->Asset.FactionID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
            // miscvalue1 = itemSlot
            // miscvalue2 = itemid
            if (!miscValue2)
                return false;
            if (miscValue1 != achievementCriteria->Asset.ItemSlot)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
            // miscvalue1 = itemid
            // miscvalue2 = diced value
            if (!miscValue1)
                return false;
            if (miscValue2 != achievementCriteria->Asset.RollValue)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:
            // miscvalue1 = emote
            if (!miscValue1)
                return false;
            if (miscValue1 != achievementCriteria->Asset.EmoteID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE:
        case ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE:
            if (!miscValue1)
                return false;

            if (achievementCriteria->AdditionalRequirements[0].Type == ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP)
            {
                if (GetPlayer()->GetMapId() != achievementCriteria->AdditionalRequirements[0].Asset)
                    return false;

                // map specific case (BG in fact) expected player targeted damage/heal
                if (!ref || ref->GetTypeId() != TYPEID_PLAYER)
                    return false;
            }
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT:
        case ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT:
            if (!miscValue1)
                return false;
            if (miscValue1 != achievementCriteria->Asset.GameObjectID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE:
            if (miscValue1 && miscValue1 != achievementCriteria->Asset.SkillID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM:
        {
            if (!miscValue1)
                return false;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(miscValue1);
            if (!proto || proto->Quality < ITEM_QUALITY_EPIC)
                return false;
            break;
        }
        case ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS:
            if (!miscValue1 || miscValue1 != achievementCriteria->Asset.ClassID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_HK_RACE:
            if (!miscValue1 || miscValue1 != achievementCriteria->Asset.RaceID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
            if (!miscValue1 || miscValue1 != achievementCriteria->Asset.ObjectiveId)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA:
            if (!miscValue1 || miscValue1 != achievementCriteria->Asset.AreaID)
                return false;
            break;
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA:
            if (miscValue1 != achievementCriteria->Asset.MapID)
                return false;
            break;
        default:
            break;
    }

    return true;
}

char const* AchievementGlobalMgr::GetCriteriaTypeString(uint32 type)
{
    return GetCriteriaTypeString(AchievementCriteriaTypes(type));
}

char const* AchievementGlobalMgr::GetCriteriaTypeString(AchievementCriteriaTypes type)
{
    switch (type)
    {
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
            return "KILL_CREATURE";
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
            return "TYPE_WIN_BG";
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL:
            return "REACH_LEVEL";
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL:
            return "REACH_SKILL_LEVEL";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT:
            return "COMPLETE_ACHIEVEMENT";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT:
            return "COMPLETE_QUEST_COUNT";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST_DAILY:
            return "COMPLETE_DAILY_QUEST_DAILY";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE:
            return "COMPLETE_QUESTS_IN_ZONE";
        case ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE:
            return "DAMAGE_DONE";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:
            return "COMPLETE_DAILY_QUEST";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND:
            return "COMPLETE_BATTLEGROUND";
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_AT_MAP:
            return "DEATH_AT_MAP";
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH:
            return "DEATH";
        case ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON:
            return "DEATH_IN_DUNGEON";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_RAID:
            return "COMPLETE_RAID";
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE:
            return "KILLED_BY_CREATURE";
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER:
            return "KILLED_BY_PLAYER";
        case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:
            return "FALL_WITHOUT_DYING";
        case ACHIEVEMENT_CRITERIA_TYPE_DEATHS_FROM:
            return "DEATHS_FROM";
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
            return "COMPLETE_QUEST";
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
            return "BE_SPELL_TARGET";
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
            return "CAST_SPELL";
        case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
            return "BG_OBJECTIVE_CAPTURE";
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA:
            return "HONORABLE_KILL_AT_AREA";
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA:
            return "WIN_ARENA";
        case ACHIEVEMENT_CRITERIA_TYPE_PLAY_ARENA:
            return "PLAY_ARENA";
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL:
            return "LEARN_SPELL";
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:
            return "HONORABLE_KILL";
        case ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM:
            return "OWN_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA:
            return "WIN_RATED_ARENA";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING:
            return "HIGHEST_TEAM_RATING";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING:
            return "HIGHEST_PERSONAL_RATING";
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL:
            return "LEARN_SKILL_LEVEL";
        case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:
            return "USE_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM:
            return "LOOT_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA:
            return "EXPLORE_AREA";
        case ACHIEVEMENT_CRITERIA_TYPE_OWN_RANK:
            return "OWN_RANK";
        case ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT:
            return "BUY_BANK_SLOT";
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION:
            return "GAIN_REPUTATION";
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION:
            return "GAIN_EXALTED_REPUTATION";
        case ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP:
            return "VISIT_BARBER_SHOP";
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
            return "EQUIP_EPIC_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
            return "ROLL_NEED_ON_LOOT";
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
            return "GREED_ON_LOOT";
        case ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS:
            return "HK_CLASS";
        case ACHIEVEMENT_CRITERIA_TYPE_HK_RACE:
            return "HK_RACE";
        case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:
            return "DO_EMOTE";
        case ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE:
            return "HEALING_DONE";
        case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:
            return "GET_KILLING_BLOWS";
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM:
            return "EQUIP_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS:
            return "MONEY_FROM_VENDORS";
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TALENTS:
            return "GOLD_SPENT_FOR_TALENTS";
        case ACHIEVEMENT_CRITERIA_TYPE_NUMBER_OF_TALENT_RESETS:
            return "NUMBER_OF_TALENT_RESETS";
        case ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD:
            return "MONEY_FROM_QUEST_REWARD";
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TRAVELLING:
            return "GOLD_SPENT_FOR_TRAVELLING";
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER:
            return "GOLD_SPENT_AT_BARBER";
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL:
            return "GOLD_SPENT_FOR_MAIL";
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY:
            return "LOOT_MONEY";
        case ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT:
            return "USE_GAMEOBJECT";
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
            return "BE_SPELL_TARGET2";
        case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:
            return "SPECIAL_PVP_KILL";
        case ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT:
            return "FISH_IN_GAMEOBJECT";
        case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:
            return "ON_LOGIN";
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS:
            return "LEARN_SKILLLINE_SPELLS";
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:
            return "WIN_DUEL";
        case ACHIEVEMENT_CRITERIA_TYPE_LOSE_DUEL:
            return "LOSE_DUEL";
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:
            return "KILL_CREATURE_TYPE";
        case ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS:
            return "GOLD_EARNED_BY_AUCTIONS";
        case ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION:
            return "CREATE_AUCTION";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID:
            return "HIGHEST_AUCTION_BID";
        case ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS:
            return "WON_AUCTIONS";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD:
            return "HIGHEST_AUCTION_SOLD";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_GOLD_VALUE_OWNED:
            return "HIGHEST_GOLD_VALUE_OWNED";
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION:
            return "GAIN_REVERED_REPUTATION";
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION:
            return "GAIN_HONORED_REPUTATION";
        case ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS:
            return "KNOWN_FACTIONS";
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM:
            return "LOOT_EPIC_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM:
            return "RECEIVE_EPIC_ITEM";
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED:
            return "ROLL_NEED";
        case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED:
            return "ROLL_GREED";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALTH:
            return "HIGHEST_HEALTH";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_POWER:
            return "HIGHEST_POWER";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_STAT:
            return "HIGHEST_STAT";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_SPELLPOWER:
            return "HIGHEST_SPELLPOWER";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_ARMOR:
            return "HIGHEST_ARMOR";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_RATING:
            return "HIGHEST_RATING";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_DEALT:
            return "HIT_DEALT";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_RECEIVED:
            return "HIT_RECEIVED";
        case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED:
            return "TOTAL_DAMAGE_RECEIVED";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEAL_CAST:
            return "HIGHEST_HEAL_CAST";
        case ACHIEVEMENT_CRITERIA_TYPE_TOTAL_HEALING_RECEIVED:
            return "TOTAL_HEALING_RECEIVED";
        case ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALING_RECEIVED:
            return "HIGHEST_HEALING_RECEIVED";
        case ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED:
            return "QUEST_ABANDONED";
        case ACHIEVEMENT_CRITERIA_TYPE_FLIGHT_PATHS_TAKEN:
            return "FLIGHT_PATHS_TAKEN";
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:
            return "LOOT_TYPE";
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
            return "CAST_SPELL2";
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE:
            return "LEARN_SKILL_LINE";
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL:
            return "EARN_HONORABLE_KILL";
        case ACHIEVEMENT_CRITERIA_TYPE_ACCEPTED_SUMMONINGS:
            return "ACCEPTED_SUMMONINGS";
        case ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS:
            return "EARN_ACHIEVEMENT_POINTS";
        case ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS:
            return "USE_LFD_TO_GROUP_WITH_PLAYERS";
    }
    return "MISSING_TYPE";
}

AchievementCriteriaEntryList const AchievementGlobalMgr::EmptyCriteriaList;

AchievementGlobalMgr* AchievementGlobalMgr::instance()
{
    static AchievementGlobalMgr instance;
    return &instance;
}

inline bool IsAchievementCriteriaTypeStoredByMiscValue(AchievementCriteriaTypes type)
{
    switch (type)
    {
        case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
        case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
        case ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND:
        case ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE:
        case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
        case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
        case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL:
        case ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA:
        case ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION:
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS:
        case ACHIEVEMENT_CRITERIA_TYPE_HK_RACE:
        case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:
        case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM:
        case ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT:
        case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
        case ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS:
        case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:
        case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
        case ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE:
            return true;
        default:
            break;
    }
    return false;
}

AchievementCriteriaEntryList const& AchievementGlobalMgr::GetAchievementCriteriaByType(AchievementCriteriaTypes type, uint32 miscValue) const
{
    if (miscValue && IsAchievementCriteriaTypeStoredByMiscValue(type))
    {
        auto itr = m_AchievementCriteriasByMiscValue[type].find(miscValue);
        if (itr != m_AchievementCriteriasByMiscValue[type].end())
            return itr->second;

        return EmptyCriteriaList;
    }

    return m_AchievementCriteriasByType[type];
}

bool AchievementGlobalMgr::IsRealmCompleted(AchievementEntry const* achievement) const
{
    auto itr = _allCompletedAchievements.find(achievement->ID);
    if (itr == _allCompletedAchievements.end())
        return false;

    if (itr->second == SystemTimePoint ::min())
        return false;

    if (itr->second == SystemTimePoint::max())
        return true;

    // Allow completing the realm first kill for entire minute after first person did it
    // it may allow more than one group to achieve it (highly unlikely)
    // but apparently this is how blizz handles it as well
    if (achievement->Flags & ACHIEVEMENT_FLAG_REALM_FIRST_KILL)
        return (GameTime::GetSystemTime() - itr->second) > Minutes(1);

    return true;
}

/**
 * @brief 设置服务器首杀成就已完成
 * @param achievement 成就定义条目
 *
 * 标记服务器首杀成就为已完成状态
 * 防止其他玩家获得同一首杀成就
 */
void AchievementGlobalMgr::SetRealmCompleted(AchievementEntry const* achievement)
{
    if (IsRealmCompleted(achievement))
        return;

    _allCompletedAchievements[achievement->ID] = GameTime::GetSystemTime();
}

//==========================================================
/**
 * @brief 加载成就条件列表
 *
 * 从DBC加载成就条件定义，并建立各种索引用于快速查找：
 * - 按类型索引
 * - 按成就ID索引
 * - 按杂项值索引（优化特定类型条件查找）
 * - 按计时类型索引
 *
 * 在服务器启动时调用
 */
void AchievementGlobalMgr::LoadAchievementCriteriaList()
{
    uint32 oldMSTime = getMSTime();

    if (sAchievementCriteriaStore.GetNumRows() == 0)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 achievement criteria.");
        return;
    }

    uint32 loaded = 0;
    for (uint32 entryId = 0; entryId < sAchievementCriteriaStore.GetNumRows(); ++entryId)
    {
        AchievementCriteriaEntry const* criteria = sAchievementMgr->GetAchievementCriteria(entryId);
        if (!criteria)
            continue;

        // 验证关联的成就是否存在
        if (!GetAchievement(criteria->AchievementID))
        {
            TC_LOG_DEBUG("server.loading", "Achievement {} referenced by criteria {} doesn't exist, criteria not loaded.", criteria->AchievementID, criteria->ID);
            continue;
        }

        ASSERT(criteria->Type < ACHIEVEMENT_CRITERIA_TYPE_TOTAL, "ACHIEVEMENT_CRITERIA_TYPE_TOTAL must be greater than or equal to %u but is currently equal to %u",
            criteria->Type + 1, ACHIEVEMENT_CRITERIA_TYPE_TOTAL);

        // 按类型索引
        m_AchievementCriteriasByType[criteria->Type].push_back(criteria);
        // 按成就ID索引
        m_AchievementCriteriaListByAchievement[criteria->AchievementID].push_back(criteria);

        // 按杂项值索引（用于优化查找性能）
        if (IsAchievementCriteriaTypeStoredByMiscValue(AchievementCriteriaTypes(criteria->Type)))
        {
            if (criteria->Type != ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA)
                m_AchievementCriteriasByMiscValue[criteria->Type][criteria->Asset.ID].push_back(criteria);
            else
            {
                // 探索区域类型需要特殊处理：一个条件可能对应多个区域
                WorldMapOverlayEntry const* worldOverlayEntry = sWorldMapOverlayStore.LookupEntry(criteria->Asset.WorldMapOverlayID);
                if (!worldOverlayEntry)
                    break;

                for (uint8 j = 0; j < MAX_WORLD_MAP_OVERLAY_AREA_IDX; ++j)
                {
                    if (worldOverlayEntry->AreaID[j])
                    {
                        // 避免重复添加同一区域
                        bool valid = true;
                        for (uint8 i = 0; i < j; ++i)
                            if (worldOverlayEntry->AreaID[j] == worldOverlayEntry->AreaID[i])
                                valid = false;
                        if (valid)
                            m_AchievementCriteriasByMiscValue[criteria->Type][worldOverlayEntry->AreaID[j]].push_back(criteria);
                    }
                }
            }
        }

        // 按附加条件索引
        for (uint32 i = 0; i < MAX_CRITERIA_REQUIREMENTS; ++i)
        {
            if (criteria->AdditionalRequirements[i].Type != ACHIEVEMENT_CRITERIA_CONDITION_NONE)
            {
                ASSERT(criteria->AdditionalRequirements[i].Type < ACHIEVEMENT_CRITERIA_CONDITION_MAX,
                    "ACHIEVEMENT_CRITERIA_CONDITION_MAX must be greater than or equal to %u but is currently equal to %u",
                    criteria->AdditionalRequirements[i].Type + 1, ACHIEVEMENT_CRITERIA_CONDITION_MAX);

                // 避免重复添加相同条件
                if (i == 0
                    || criteria->AdditionalRequirements[i].Type != criteria->AdditionalRequirements[i - 1].Type
                    || criteria->AdditionalRequirements[i].Asset != criteria->AdditionalRequirements[i - 1].Asset)
                    m_AchievementCriteriasByCondition[criteria->AdditionalRequirements[i].Type][criteria->AdditionalRequirements[i].Asset].push_back(criteria);
            }
        }

        // 按计时类型索引
        if (criteria->StartTimer)
        {
            ASSERT(criteria->StartEvent < ACHIEVEMENT_TIMED_TYPE_MAX, "ACHIEVEMENT_TIMED_TYPE_MAX must be greater than or equal to %u but is currently equal to %u",
                criteria->StartEvent + 1, ACHIEVEMENT_TIMED_TYPE_MAX);
            m_AchievementCriteriasByTimedType[criteria->StartEvent].push_back(criteria);
        }

        ++loaded;
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} achievement criteria in {} ms.", loaded, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载成就引用列表
 *
 * 建立成就之间的引用关系索引
 * 用于处理"完成另一个成就作为条件"的成就
 */
void AchievementGlobalMgr::LoadAchievementReferenceList()
{
    uint32 oldMSTime = getMSTime();

    if (sAchievementStore.GetNumRows() == 0)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 achievement references.");
        return;
    }

    uint32 count = 0;

    for (uint32 entryId = 0; entryId < sAchievementStore.GetNumRows(); ++entryId)
    {
        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(entryId);
        if (!achievement || !achievement->SharesCriteria)
            continue;

        m_AchievementListByReferencedId[achievement->SharesCriteria].push_back(achievement);
        ++count;
    }

    // Once Bitten, Twice Shy (10 player) - Icecrown Citadel
    if (AchievementEntry const* achievement = sAchievementMgr->GetAchievement(4539))
        const_cast<AchievementEntry*>(achievement)->InstanceID = 631;    // Correct map requirement (currently has Ulduar)

    TC_LOG_INFO("server.loading", ">> Loaded {} achievement references in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
}

void AchievementGlobalMgr::LoadAchievementCriteriaData()
{
    uint32 oldMSTime = getMSTime();

    m_criteriaDataMap.clear();                              // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT criteria_id, type, value1, value2, ScriptName FROM achievement_criteria_data");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 additional achievement criteria data. DB table `achievement_criteria_data` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 criteria_id = fields[0].GetUInt32();

        AchievementCriteriaEntry const* criteria = sAchievementMgr->GetAchievementCriteria(criteria_id);

        if (!criteria)
        {
            TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` contains data for non-existing criteria (Entry: {}). Ignored.", criteria_id);
            continue;
        }

        uint32 dataType = fields[1].GetUInt8();
        std::string scriptName = fields[4].GetString();
        uint32 scriptId = 0;
        if (scriptName.length()) // not empty
        {
            if (dataType != ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT)
                TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` contains a ScriptName for non-scripted data type (Entry: {}, type {}), useless data.", criteria_id, dataType);
            else
                scriptId = sObjectMgr->GetScriptId(scriptName);
        }

        AchievementCriteriaData data(dataType, fields[2].GetUInt32(), fields[3].GetUInt32(), scriptId);

        if (!data.IsValid(criteria))
            continue;

        // this will allocate empty data set storage
        AchievementCriteriaDataSet& dataSet = m_criteriaDataMap[criteria_id];
        dataSet.SetCriteriaId(criteria_id);

        // add real data only for not NONE data types
        if (data.dataType != ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE)
            dataSet.Add(data);

        // counting data by and data types
        ++count;
    }
    while (result->NextRow());

    // post loading checks
    for (uint32 entryId = 0; entryId < sAchievementCriteriaStore.GetNumRows(); ++entryId)
    {
        AchievementCriteriaEntry const* criteria = sAchievementMgr->GetAchievementCriteria(entryId);
        if (!criteria)
            continue;

        switch (criteria->Type)
        {
            case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE:
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_BG:
            case ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING:
            case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET:
            case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL:
            case ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE:
            case ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL:
            case ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT:
            case ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT:
            case ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS:
            case ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2:
            case ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL:
            case ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN:
            case ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE:
            case ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2:
                // achievement requires db data
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST:
            {
                AchievementEntry const* achievement = sAchievementMgr->GetAchievement(criteria->AchievementID);
                if (!achievement)
                    continue;

                // There are many achievements with these criteria, use hardcoded check at this moment to pick a simple case
                if (achievement->ID == 1282)
                    break;

                continue;
            }
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA: // need skip generic cases
                if (criteria->AdditionalRequirements[0].Type != ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE)
                    continue;
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE:        // need skip generic cases
                if (criteria->Quantity == 0)
                    continue;
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL:        // skip statistics
                if (criteria->Quantity == 0)
                    continue;
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE:       // need skip generic cases
                if (criteria->Quantity != 1)
                    continue;
                break;
            case ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST:
            case ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM:        // only Children's Week achievements
            {
                AchievementEntry const* achievement = sAchievementMgr->GetAchievement(criteria->AchievementID);
                if (!achievement)
                    continue;
                if (achievement->Category != CATEGORY_CHILDRENS_WEEK && achievement->ID != 1785)
                    continue;
                break;
            }
            default:                                        // type not use DB data, ignore
                continue;
        }

        if (!GetCriteriaDataSet(criteria) && !DisableMgr::IsDisabledFor(DISABLE_TYPE_ACHIEVEMENT_CRITERIA, entryId, nullptr))
            TC_LOG_ERROR("sql.sql", "Table `achievement_criteria_data` does not contain expected data for criteria (Entry: {} Type: {}) for achievement {}.", criteria->ID, criteria->Type, criteria->AchievementID);
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} additional achievement criteria data in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载已完成的成就
 *
 * 从数据库加载服务器上已完成的成就，用于服务器首杀成就的检测
 * 同时初始化所有服务器首杀成就的条目，使多线程访问更安全
 */
void AchievementGlobalMgr::LoadCompletedAchievements()
{
    uint32 oldMSTime = getMSTime();

    // 预先填充所有服务器首杀成就ID，使多线程访问更安全
    // 虽然不能完全防止竞争，但可以防止因std::unordered_map添加键导致的崩溃
    // 唯一可能的竞争发生在与键关联的值上
    for (uint32 i = 0; i < sAchievementStore.GetNumRows(); ++i)
        if (AchievementEntry const* achievement = sAchievementStore.LookupEntry(i))
            if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
                _allCompletedAchievements[achievement->ID] = SystemTimePoint::min();

    QueryResult result = CharacterDatabase.Query("SELECT achievement FROM character_achievement GROUP BY achievement");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 realm first completed achievements. DB table `character_achievement` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint16 achievementId = fields[0].GetUInt16();
        AchievementEntry const* achievement = sAchievementMgr->GetAchievement(achievementId);
        if (!achievement)
        {
            // Remove non-existing achievements from all characters
            TC_LOG_ERROR("achievement", "Non-existing achievement {} data has been removed from the table `character_achievement`.", achievementId);

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_INVALID_ACHIEVMENT);
            stmt->setUInt16(0, uint16(achievementId));
            CharacterDatabase.Execute(stmt);

            continue;
        }
        else if (achievement->Flags & (ACHIEVEMENT_FLAG_REALM_FIRST_REACH | ACHIEVEMENT_FLAG_REALM_FIRST_KILL))
            _allCompletedAchievements[achievementId] = SystemTimePoint::max();
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} realm first completed achievements in {} ms.", (unsigned long)_allCompletedAchievements.size(), GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载成就奖励配置
 *
 * 从数据库加载成就奖励（物品、称号、邮件等）
 * 包括数据验证和错误报告
 */
void AchievementGlobalMgr::LoadRewards()
{
    uint32 oldMSTime = getMSTime();

    m_achievementRewards.clear();                           // 重载时需要清空

    //                                               0   1       2       3       4       5        6     7
    QueryResult result = WorldDatabase.Query("SELECT ID, TitleA, TitleH, ItemID, Sender, Subject, Body, MailTemplateID FROM achievement_reward");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 achievement rewards. DB table `achievement_reward` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetUInt32();
        AchievementEntry const* achievement = GetAchievement(id);
        if (!achievement)
        {
            TC_LOG_ERROR("sql.sql", "Table `achievement_reward` contains a wrong achievement ID ({}), ignored.", id);
            continue;
        }

        AchievementReward reward;
        reward.TitleId[0]       = fields[1].GetUInt32();   // 联盟称号
        reward.TitleId[1]       = fields[2].GetUInt32();   // 部落称号
        reward.ItemId           = fields[3].GetUInt32();   // 奖励物品
        reward.SenderCreatureId = fields[4].GetUInt32();   // 邮件发送者
        reward.Subject          = fields[5].GetString();   // 邮件主题
        reward.Body             = fields[6].GetString();   // 邮件正文
        reward.MailTemplateId   = fields[7].GetUInt32();   // 邮件模板ID

        // 必须至少有称号或邮件奖励
        if (!reward.TitleId[0] && !reward.TitleId[1] && !reward.SenderCreatureId)
        {
            TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) does not contain title or item reward data. Ignored.", id);
            continue;
        }

        // 两个阵营都可完成的成就，如果只有一个阵营有称号奖励则报错
        if (achievement->Faction == ACHIEVEMENT_FACTION_ANY && (!reward.TitleId[0] ^ !reward.TitleId[1]))
            TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) contains the title (A: {} H: {}) for only one team.", id, reward.TitleId[0], reward.TitleId[1]);

        // 验证联盟称号
        if (reward.TitleId[0])
        {
            CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(reward.TitleId[0]);
            if (!titleEntry)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (Entry: {}) contains an invalid title id ({}) in `title_A`, set to 0", id, reward.TitleId[0]);
                reward.TitleId[0] = 0;
            }
        }

        // 验证部落称号
        if (reward.TitleId[1])
        {
            CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(reward.TitleId[1]);
            if (!titleEntry)
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (Entry: {}) contains an invalid title id ({}) in `title_H`, set to 0", id, reward.TitleId[1]);
                reward.TitleId[1] = 0;
            }
        }

        // 检查邮件数据
        if (reward.SenderCreatureId)
        {
            // 验证发送者生物是否存在
            if (!sObjectMgr->GetCreatureTemplate(reward.SenderCreatureId))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) contains an invalid creature ID {} as sender, mail reward skipped.", id, reward.SenderCreatureId);
                reward.SenderCreatureId = 0;
            }
        }
        else
        {
            // 没有发送者但有邮件相关数据，报错
            if (reward.ItemId)
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) does not have sender data, but contains an item reward. Item will not be rewarded.", id);

            if (!reward.Subject.empty())
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) does not have sender data, but contains a mail subject.", id);

            if (!reward.Body.empty())
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) does not have sender data, but contains mail text.", id);

            if (reward.MailTemplateId)
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) does not have sender data, but has a MailTemplate.", id);
        }

        if (reward.MailTemplateId)
        {
            if (!sMailTemplateStore.LookupEntry(reward.MailTemplateId))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) is using an invalid MailTemplate ({}).", id, reward.MailTemplateId);
                reward.MailTemplateId = 0;
            }
            else if (!reward.Subject.empty() || !reward.Body.empty())
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) is using MailTemplate ({}) and mail subject/text.", id, reward.MailTemplateId);
        }

        // 验证奖励物品
        if (reward.ItemId)
        {
            if (!sObjectMgr->GetItemTemplate(reward.ItemId))
            {
                TC_LOG_ERROR("sql.sql", "Table `achievement_reward` (ID: {}) contains an invalid item id {}, reward mail will not contain the rewarded item.", id, reward.ItemId);
                reward.ItemId = 0;
            }
        }

        m_achievementRewards[id] = reward;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} achievement rewards in {} ms.", uint32(m_achievementRewards.size()), GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载成就奖励本地化文本
 *
 * 从数据库加载多语言的奖励邮件文本
 * 支持不同语言版本的邮件主题和正文
 */
void AchievementGlobalMgr::LoadRewardLocales()
{
    uint32 oldMSTime = getMSTime();

    m_achievementRewardLocales.clear();                       // 重载时需要清空

    //                                               0   1       2        3
    QueryResult result = WorldDatabase.Query("SELECT ID, Locale, Subject, Body FROM achievement_reward_locale");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 achievement reward locale strings.  DB table `achievement_reward_locale` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        // 验证关联的奖励是否存在
        if (m_achievementRewards.find(id) == m_achievementRewards.end())
        {
            TC_LOG_ERROR("sql.sql", "Table `achievement_reward_locale` (ID: {}) contains locale strings for a non-existing achievement reward.", id);
            continue;
        }

        // 存储本地化文本
        AchievementRewardLocale& data = m_achievementRewardLocales[id];
        ObjectMgr::AddLocaleString(fields[2].GetString(), locale, data.Subject);
        ObjectMgr::AddLocaleString(fields[3].GetString(), locale, data.Text);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} achievement reward locale strings in {} ms.", uint32(m_achievementRewardLocales.size()), GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 根据ID获取成就定义
 * @param achievementId 成就ID
 * @return 成就定义指针，不存在返回nullptr
 */
AchievementEntry const* AchievementGlobalMgr::GetAchievement(uint32 achievementId) const
{
    return sAchievementStore.LookupEntry(achievementId);
}

/**
 * @brief 根据ID获取条件定义
 * @param criteriaId 条件ID
 * @return 条件定义指针，不存在返回nullptr
 */
AchievementCriteriaEntry const* AchievementGlobalMgr::GetAchievementCriteria(uint32 criteriaId) const
{
    return sAchievementCriteriaStore.LookupEntry(criteriaId);
}
