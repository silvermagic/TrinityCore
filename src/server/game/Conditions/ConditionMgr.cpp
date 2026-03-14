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
 * @file ConditionMgr.cpp
 * @brief 条件管理器实现文件 - 实现条件系统的核心逻辑
 *
 * 本文件实现了条件管理器的所有功能，包括：
 * - 从数据库加载条件定义
 * - 条件验证和错误检查
 * - 各种条件类型的判断逻辑
 * - 条件分组和引用处理
 *
 * 主要数据流：
 * 1. 服务器启动时调用LoadConditions()从conditions表加载数据
 * 2. 将条件按源类型和条目ID存储到相应的容器中
 * 3. 游戏运行时通过IsObjectMeetToConditions等接口进行条件判断
 *
 * 性能考虑：
 * - 条件判断是高频操作，使用哈希表快速查找
 * - 条件结果不缓存，每次都实时判断（因为状态可能变化）
 */

#include "ConditionMgr.h"
#include "AchievementMgr.h"
#include "DatabaseEnv.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Pet.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "World.h"

// 条件源类型名称数组 - 用于日志输出和调试
char const* const ConditionMgr::StaticSourceTypeData[CONDITION_SOURCE_TYPE_MAX] =
{
    "None",                    // 无
    "Creature Loot",           // 生物战利品
    "Disenchant Loot",         // 分解战利品
    "Fishing Loot",            // 钓鱼战利品
    "GameObject Loot",         // 游戏对象战利品
    "Item Loot",               // 物品战利品
    "Mail Loot",               // 邮件战利品
    "Milling Loot",            // 研磨战利品
    "Pickpocketing Loot",      // 搜索战利品
    "Prospecting Loot",        // 探矿战利品
    "Reference Loot",          // 引用战利品
    "Skinning Loot",           // 剥皮战利品
    "Spell Loot",              // 法术战利品
    "Spell Impl. Target",      // 法术隐式目标
    "Gossip Menu",             // 对话菜单
    "Gossip Menu Option",      // 对话菜单选项
    "Creature Vehicle",        // 生物载具
    "Spell Expl. Target",      // 法术显式目标
    "Spell Click Event",       // 法术点击事件
    "Quest Accept",            // 任务接受
    "Quest Show Mark",         // 任务显示标记
    "Vehicle Spell",           // 载具法术
    "SmartScript",             // 智能脚本
    "Npc Vendor",              // NPC商人
    "Spell Proc",              // 法术触发
    "Terrain Swap",            // 地形交换
    "Phase"                    // 相位
};

// 条件类型信息数组 - 定义每种条件类型的参数需求
// 数组元素格式：{名称, 是否使用值1, 是否使用值2, 是否使用值3}
ConditionMgr::ConditionTypeInfo const ConditionMgr::StaticConditionTypeData[CONDITION_MAX] =
{
    { "None",                     false, false, false }, // 无条件
    { "Aura",                      true, true,  true  }, // 光环条件：需要法术ID、效果索引、是否使用目标
    { "Item Stored",               true, true,  true  }, // 物品存储：需要物品ID、数量、是否包含银行
    { "Item Equipped",             true, false, false }, // 物品装备：需要物品ID
    { "Zone",                      true, false, false }, // 区域：需要区域ID
    { "Reputation",                true, true,  false }, // 声望：需要阵营ID、声望等级掩码
    { "Team",                      true, false, false }, // 阵营：需要阵营ID
    { "Skill",                     true, true,  false }, // 技能：需要技能ID、技能值
    { "Quest Rewarded",            true, false, false }, // 任务已奖励：需要任务ID
    { "Quest Taken",               true, false, false }, // 任务已接取：需要任务ID
    { "Drunken",                   true, false, false }, // 醉酒状态：需要醉酒等级
    { "WorldState",                true, true,  false }, // 世界状态：需要索引、值
    { "Active Event",              true, false, false }, // 激活事件：需要事件ID
    { "Instance Info",             true, true,  true  }, // 副本信息：需要条目、数据、类型
    { "Quest None",                true, false, false }, // 无任务：需要任务ID
    { "Class",                     true, false, false }, // 职业：需要职业掩码
    { "Race",                      true, false, false }, // 种族：需要种族掩码
    { "Achievement",               true, false, false }, // 成就：需要成就ID
    { "Title",                     true, false, false }, // 称号：需要称号ID
    { "SpawnMask",                 true, false, false }, // 生成掩码：需要掩码值
    { "Gender",                    true, false, false }, // 性别：需要性别值
    { "Unit State",                true, false, false }, // 单位状态：需要状态值
    { "Map",                       true, false, false }, // 地图：需要地图ID
    { "Area",                      true, false, false }, // 区域：需要区域ID
    { "CreatureType",              true, false, false }, // 生物类型：需要类型值
    { "Spell Known",               true, false, false }, // 已知法术：需要法术ID
    { "PhaseMask",                 true, false, false }, // 相位掩码：需要掩码值
    { "Level",                     true, true,  false }, // 等级：需要等级、比较类型
    { "Quest Completed",           true, false, false }, // 任务完成：需要任务ID
    { "Near Creature",             true, true,  true  }, // 附近生物：需要生物条目、距离、是否死亡
    { "Near GameObject",           true, true,  false }, // 附近游戏对象：需要对象条目、距离
    { "Object Entry or Guid",      true, true,  true  }, // 对象条目或GUID：需要类型ID、条目、GUID
    { "Object TypeMask",           true, false, false }, // 对象类型掩码：需要掩码值
    { "Relation",                  true, true,  false }, // 关系：需要目标索引、关系类型
    { "Reaction",                  true, true,  false }, // 反应：需要目标索引、等级掩码
    { "Distance",                  true, true,  true  }, // 距离：需要目标索引、距离、比较类型
    { "Alive",                    false, false, false }, // 存活：无需参数
    { "Health Value",              true, true,  false }, // 生命值：需要值、比较类型
    { "Health Pct",                true, true,  false }, // 生命值百分比：需要百分比、比较类型
    { "Realm Achievement",         true, false, false }, // 服务器成就：需要成就ID
    { "In Water",                 false, false, false }, // 在水中：无需参数
    { "Terrain Swap",             false, false, false }, // 地形交换：仅master分支
    { "Sit/stand state",           true, true,  false }, // 坐/站状态：需要状态类型、状态值
    { "Daily Quest Completed",     true, false, false }, // 日常任务完成：需要任务ID
    { "Charmed",                  false, false, false }, // 魅惑状态：无需参数
    { "Pet type",                  true, false, false }, // 宠物类型：需要类型掩码
    { "On Taxi",                  false, false, false }, // 在飞行中：无需参数
    { "Quest state mask",          true, true,  false }, // 任务状态掩码：需要任务ID、状态掩码
    { "Quest objective progress",  true, true,   true }, // 任务目标进度：需要任务ID、目标索引、进度值
    { "Map difficulty",            true, false, false }, // 地图难度：需要难度ID
    { "Is Gamemaster",             true, false, false }, // 是否GM：是否可以是GM
    { "Object Entry or Guid",      true, true,  true  }, // 对象条目或GUID（master分支）
    { "Object TypeMask",           true, false, false }  // 对象类型掩码（master分支）
};

/**
 * @brief 检查对象是否满足条件
 * @param sourceInfo 条件源信息，包含目标对象数组
 * @return 条件是否满足
 *
 * 这是条件判断的核心函数，根据ConditionType执行不同的判断逻辑。
 *
 * 特殊说明：
 * - 可以有CONDITION_SOURCE_TYPE_NONE && !mReferenceId的情况（如从特殊事件调用，如eventAI）
 * - 如果目标对象不存在，返回false
 */
bool Condition::Meets(ConditionSourceInfo& sourceInfo) const
{
    // 确保条件目标索引有效
    ASSERT(ConditionTarget < MAX_CONDITION_TARGETS);
    WorldObject* object = sourceInfo.mConditionTargets[ConditionTarget];

    // 对象不存在，返回false
    if (!object)
    {
        TC_LOG_DEBUG("condition", "Condition object not found for {}", ToString());
        return false;
    }

    bool condMeets = false;
    switch (ConditionType)
    {
        case CONDITION_NONE:
            condMeets = true;                                    // 空条件，总是满足
            break;
        case CONDITION_AURA:
        {
            // 检查单位是否拥有指定光环效果
            if (Unit* unit = object->ToUnit())
                condMeets = unit->HasAuraEffect(ConditionValue1, ConditionValue2);
            break;
        }
        case CONDITION_ITEM:
        {
            // 检查玩家是否拥有指定数量的物品
            if (Player* player = object->ToPlayer())
            {
                // 不允许0个物品（在表加载时已检查）
                ASSERT(ConditionValue2);
                bool checkBank = ConditionValue3 ? true : false;
                condMeets = player->HasItemCount(ConditionValue1, ConditionValue2, checkBank);
            }
            break;
        }
        case CONDITION_ITEM_EQUIPPED:
        {
            // 检查玩家是否装备了指定物品
            if (Player* player = object->ToPlayer())
                condMeets = player->HasItemOrGemWithIdEquipped(ConditionValue1, 1);
            break;
        }
        case CONDITION_ZONEID:
            // 检查是否在指定区域
            condMeets = object->GetZoneId() == ConditionValue1;
            break;
        case CONDITION_REPUTATION_RANK:
        {
            // 检查玩家是否达到指定声望等级
            if (Player* player = object->ToPlayer())
            {
                if (FactionEntry const* faction = sFactionStore.LookupEntry(ConditionValue1))
                    condMeets = (ConditionValue2 & (1 << player->GetReputationMgr().GetRank(faction))) != 0;
            }
            break;
        }
        case CONDITION_ACHIEVEMENT:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->HasAchieved(ConditionValue1);
            break;
        }
        case CONDITION_TEAM:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->GetTeam() == ConditionValue1;
            break;
        }
        case CONDITION_CLASS:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = (unit->GetClassMask() & ConditionValue1) != 0;
            break;
        }
        case CONDITION_RACE:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = (unit->GetRaceMask() & ConditionValue1) != 0;
            break;
        }
        case CONDITION_GENDER:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->GetNativeGender() == Gender(ConditionValue1);
            break;
        }
        case CONDITION_SKILL:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->HasSkill(ConditionValue1) && player->GetBaseSkillValue(ConditionValue1) >= ConditionValue2;
            break;
        }
        case CONDITION_QUESTREWARDED:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->GetQuestRewardStatus(ConditionValue1);
            break;
        }
        case CONDITION_QUESTTAKEN:
        {
            if (Player* player = object->ToPlayer())
            {
                QuestStatus status = player->GetQuestStatus(ConditionValue1);
                condMeets = (status == QUEST_STATUS_INCOMPLETE);
            }
            break;
        }
        case CONDITION_QUEST_COMPLETE:
        {
            if (Player* player = object->ToPlayer())
            {
                QuestStatus status = player->GetQuestStatus(ConditionValue1);
                condMeets = (status == QUEST_STATUS_COMPLETE && !player->GetQuestRewardStatus(ConditionValue1));
            }
            break;
        }
        case CONDITION_QUEST_NONE:
        {
            if (Player* player = object->ToPlayer())
            {
                QuestStatus status = player->GetQuestStatus(ConditionValue1);
                condMeets = (status == QUEST_STATUS_NONE);
            }
            break;
        }
        case CONDITION_ACTIVE_EVENT:
            condMeets = sGameEventMgr->IsActiveEvent(ConditionValue1);
            break;
        case CONDITION_INSTANCE_INFO:
        {
            Map* map = object->GetMap();
            if (map->IsDungeon())
            {
                if (InstanceScript const* instance = ((InstanceMap*)map)->GetInstanceScript())
                {
                    switch (ConditionValue3)
                    {
                        case INSTANCE_INFO_DATA:
                            condMeets = instance->GetData(ConditionValue1) == ConditionValue2;
                            break;
                        case INSTANCE_INFO_GUID_DATA:
                            condMeets = instance->GetGuidData(ConditionValue1) == ObjectGuid(uint64(ConditionValue2));
                            break;
                        case INSTANCE_INFO_BOSS_STATE:
                            condMeets = instance->GetBossState(ConditionValue1) == EncounterState(ConditionValue2);
                            break;
                        case INSTANCE_INFO_DATA64:
                            condMeets = instance->GetData64(ConditionValue1) == ConditionValue2;
                            break;
                    }
                }
            }
            break;
        }
        case CONDITION_MAPID:
            condMeets = object->GetMapId() == ConditionValue1;
            break;
        case CONDITION_AREAID:
            condMeets = object->GetAreaId() == ConditionValue1;
            break;
        case CONDITION_SPELL:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->HasSpell(ConditionValue1);
            break;
        }
        case CONDITION_LEVEL:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = CompareValues(static_cast<ComparisionType>(ConditionValue2), static_cast<uint32>(unit->GetLevel()), ConditionValue1);
            break;
        }
        case CONDITION_DRUNKENSTATE:
        {
            if (Player* player = object->ToPlayer())
                condMeets = (uint32)Player::GetDrunkenstateByValue(player->GetDrunkValue()) >= ConditionValue1;
            break;
        }
        case CONDITION_NEAR_CREATURE:
        {
            condMeets = object->FindNearestCreature(ConditionValue1, (float)ConditionValue2, bool(!ConditionValue3)) != nullptr;
            break;
        }
        case CONDITION_NEAR_GAMEOBJECT:
        {
            condMeets = object->FindNearestGameObject(ConditionValue1, (float)ConditionValue2) != nullptr;
            break;
        }
        case CONDITION_OBJECT_ENTRY_GUID:
        {
            if (uint32(object->GetTypeId()) == ConditionValue1)
            {
                condMeets = !ConditionValue2 || (object->GetEntry() == ConditionValue2);

                if (ConditionValue3)
                {
                    switch (object->GetTypeId())
                    {
                        case TYPEID_UNIT:
                            condMeets &= object->ToCreature()->GetSpawnId() == ConditionValue3;
                            break;
                        case TYPEID_GAMEOBJECT:
                            condMeets &= object->ToGameObject()->GetSpawnId() == ConditionValue3;
                            break;
                        default:
                            break;
                    }
                }
            }
            break;
        }
        case CONDITION_TYPE_MASK:
        {
            condMeets = object->isType(ConditionValue1);
            break;
        }
        case CONDITION_RELATION_TO:
        {
            if (WorldObject* toObject = sourceInfo.mConditionTargets[ConditionValue1])
            {
                Unit* toUnit = toObject->ToUnit();
                Unit* unit = object->ToUnit();
                if (toUnit && unit)
                {
                    switch (static_cast<RelationType>(ConditionValue2))
                    {
                        case RELATION_SELF:
                            condMeets = unit == toUnit;
                            break;
                        case RELATION_IN_PARTY:
                            condMeets = unit->IsInPartyWith(toUnit);
                            break;
                        case RELATION_IN_RAID_OR_PARTY:
                            condMeets = unit->IsInRaidWith(toUnit);
                            break;
                        case RELATION_OWNED_BY:
                            condMeets = unit->GetOwnerGUID() == toUnit->GetGUID();
                            break;
                        case RELATION_PASSENGER_OF:
                            condMeets = unit->IsOnVehicle(toUnit);
                            break;
                        case RELATION_CREATED_BY:
                            condMeets = unit->GetCreatorGUID() == toUnit->GetGUID();
                            break;
                        default:
                            break;
                    }
                }
            }
            break;
        }
        case CONDITION_REACTION_TO:
        {
            if (WorldObject* toObject = sourceInfo.mConditionTargets[ConditionValue1])
            {
                Unit* toUnit = toObject->ToUnit();
                Unit* unit = object->ToUnit();
                if (toUnit && unit)
                    condMeets = ((1 << unit->GetReactionTo(toUnit)) & ConditionValue2) != 0;
            }
            break;
        }
        case CONDITION_DISTANCE_TO:
        {
            if (WorldObject* toObject = sourceInfo.mConditionTargets[ConditionValue1])
                condMeets = CompareValues(static_cast<ComparisionType>(ConditionValue3), object->GetDistance(toObject), static_cast<float>(ConditionValue2));
            break;
        }
        case CONDITION_ALIVE:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = unit->IsAlive();
            break;
        }
        case CONDITION_HP_VAL:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = CompareValues(static_cast<ComparisionType>(ConditionValue2), unit->GetHealth(), static_cast<uint32>(ConditionValue1));
            break;
        }
        case CONDITION_HP_PCT:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = CompareValues(static_cast<ComparisionType>(ConditionValue2), unit->GetHealthPct(), static_cast<float>(ConditionValue1));
            break;
        }
        case CONDITION_WORLD_STATE:
        {
            condMeets = ConditionValue2 == sWorld->getWorldState(ConditionValue1);
            break;
        }
        case CONDITION_PHASEMASK:
        {
            condMeets = (object->GetPhaseMask() & ConditionValue1) != 0;
            break;
        }
        case CONDITION_TITLE:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->HasTitle(ConditionValue1);
            break;
        }
        case CONDITION_SPAWNMASK:
        {
            condMeets = ((1 << object->GetMap()->GetSpawnMode()) & ConditionValue1) != 0;
            break;
        }
        case CONDITION_UNIT_STATE:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = unit->HasUnitState(ConditionValue1);
            break;
        }
        case CONDITION_CREATURE_TYPE:
        {
            if (Creature* creature = object->ToCreature())
                condMeets = creature->GetCreatureTemplate()->type == ConditionValue1;
            break;
        }
        case CONDITION_REALM_ACHIEVEMENT:
        {
            AchievementEntry const* achievement = sAchievementMgr->GetAchievement(ConditionValue1);
            if (achievement && sAchievementMgr->IsRealmCompleted(achievement))
                condMeets = true;
            break;
        }
        case CONDITION_IN_WATER:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = unit->IsInWater();
            break;
        }
        case CONDITION_STAND_STATE:
        {
            if (Unit* unit = object->ToUnit())
            {
                if (ConditionValue1 == 0)
                    condMeets = (unit->GetStandState() == ConditionValue2);
                else if (ConditionValue2 == 0)
                    condMeets = unit->IsStandState();
                else if (ConditionValue2 == 1)
                    condMeets = unit->IsSitState();
            }
            break;
        }
        case CONDITION_DAILY_QUEST_DONE:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->IsDailyQuestDone(ConditionValue1);
            break;
        }
        case CONDITION_CHARMED:
        {
            if (Unit* unit = object->ToUnit())
                condMeets = unit->IsCharmed();
            break;
        }
        case CONDITION_PET_TYPE:
        {
            if (Player* player = object->ToPlayer())
                if (Pet* pet = player->GetPet())
                    condMeets = (((1 << pet->getPetType()) & ConditionValue1) != 0);
            break;
        }
        case CONDITION_TAXI:
        {
            if (Player* player = object->ToPlayer())
                condMeets = player->IsInFlight();
            break;
        }
        case CONDITION_QUESTSTATE:
        {
            if (Player* player = object->ToPlayer())
            {
                if (
                    ((ConditionValue2 & (1 << QUEST_STATUS_NONE)) && (player->GetQuestStatus(ConditionValue1) == QUEST_STATUS_NONE)) ||
                    ((ConditionValue2 & (1 << QUEST_STATUS_COMPLETE)) && (player->GetQuestStatus(ConditionValue1) == QUEST_STATUS_COMPLETE)) ||
                    ((ConditionValue2 & (1 << QUEST_STATUS_INCOMPLETE)) && (player->GetQuestStatus(ConditionValue1) == QUEST_STATUS_INCOMPLETE)) ||
                    ((ConditionValue2 & (1 << QUEST_STATUS_FAILED)) && (player->GetQuestStatus(ConditionValue1) == QUEST_STATUS_FAILED)) ||
                    ((ConditionValue2 & (1 << QUEST_STATUS_REWARDED)) && player->GetQuestRewardStatus(ConditionValue1))
                )
                    condMeets = true;
            }
            break;
        }
        case CONDITION_QUEST_OBJECTIVE_PROGRESS:
        {
            if (Player* player = object->ToPlayer())
            {
                const Quest* quest = ASSERT_NOTNULL(sObjectMgr->GetQuestTemplate(ConditionValue1));
                uint16 log_slot = player->FindQuestSlot(quest->GetQuestId());
                if (log_slot >= MAX_QUEST_LOG_SIZE)
                    break;
                if (player->GetQuestSlotCounter(log_slot, ConditionValue2) == ConditionValue3)
                    condMeets = true;
            }
            break;
        }
        case CONDITION_DIFFICULTY_ID:
        {
            condMeets = object->GetMap()->GetDifficulty() == ConditionValue1;
            break;
        }
        case CONDITION_GAMEMASTER:
        {
            if (Player* player = object->ToPlayer())
            {
                if (ConditionValue1 == 1)
                    condMeets = player->CanBeGameMaster();
                else
                    condMeets = player->IsGameMaster();
            }
            break;
        }
        default:
            condMeets = false;
            break;
    }

    if (NegativeCondition)
        condMeets = !condMeets;

    if (!condMeets)
        sourceInfo.mLastFailedCondition = this;

    return condMeets && sScriptMgr->OnConditionCheck(this, sourceInfo); // Returns true by default.;
}

/**
 * @brief 获取条件的搜索者类型掩码
 * @return 搜索者类型掩码
 *
 * 此函数用于构建条件可以返回true的对象类型掩码，
 * 主要用于加速网格搜索（gridsearch）。
 *
 * 例如：
 * - CONDITION_AURA只适用于单位（生物和玩家）
 * - CONDITION_ITEM只适用于玩家
 * - CONDITION_NONE适用于所有对象
 */
uint32 Condition::GetSearcherTypeMaskForCondition() const
{
    // 构建条件可以返回true的对象类型掩码
    // 用于加速网格搜索
    if (NegativeCondition)
        return (GRID_MAP_TYPE_MASK_ALL);

    uint32 mask = 0;
    switch (ConditionType)
    {
        case CONDITION_NONE:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 无条件适用于所有对象
            break;
        case CONDITION_AURA:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;  // 光环仅适用于单位
            break;
        case CONDITION_ITEM:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 物品检查仅适用于玩家
            break;
        case CONDITION_ITEM_EQUIPPED:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 装备检查仅适用于玩家
            break;
        case CONDITION_ZONEID:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 区域检查适用于所有对象
            break;
        case CONDITION_REPUTATION_RANK:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 声望检查仅适用于玩家
            break;
        case CONDITION_ACHIEVEMENT:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 成就检查仅适用于玩家
            break;
        case CONDITION_TEAM:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 阵营检查仅适用于玩家
            break;
        case CONDITION_CLASS:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;  // 职业检查适用于单位
            break;
        case CONDITION_RACE:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;  // 种族检查适用于单位
            break;
        case CONDITION_SKILL:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 技能检查仅适用于玩家
            break;
        case CONDITION_QUESTREWARDED:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 任务奖励检查仅适用于玩家
            break;
        case CONDITION_QUESTTAKEN:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 任务接取检查仅适用于玩家
            break;
        case CONDITION_QUEST_COMPLETE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 任务完成检查仅适用于玩家
            break;
        case CONDITION_QUEST_NONE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 无任务检查仅适用于玩家
            break;
        case CONDITION_ACTIVE_EVENT:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 活动事件检查适用于所有对象
            break;
        case CONDITION_INSTANCE_INFO:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 副本信息检查适用于所有对象
            break;
        case CONDITION_MAPID:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 地图ID检查适用于所有对象
            break;
        case CONDITION_AREAID:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 区域ID检查适用于所有对象
            break;
        case CONDITION_SPELL:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 法术检查仅适用于玩家
            break;
        case CONDITION_LEVEL:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;  // 等级检查适用于单位
            break;
        case CONDITION_DRUNKENSTATE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;  // 醉酒状态检查仅适用于玩家
            break;
        case CONDITION_NEAR_CREATURE:
            mask |= GRID_MAP_TYPE_MASK_ALL;  // 附近生物检查适用于所有对象
            break;
        case CONDITION_NEAR_GAMEOBJECT:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_OBJECT_ENTRY_GUID:
            switch (ConditionValue1)
            {
                case TYPEID_UNIT:
                    mask |= GRID_MAP_TYPE_MASK_CREATURE;
                    break;
                case TYPEID_PLAYER:
                    mask |= GRID_MAP_TYPE_MASK_PLAYER;
                    break;
                case TYPEID_GAMEOBJECT:
                    mask |= GRID_MAP_TYPE_MASK_GAMEOBJECT;
                    break;
                case TYPEID_CORPSE:
                    mask |= GRID_MAP_TYPE_MASK_CORPSE;
                    break;
                default:
                    break;
            }
            break;
        case CONDITION_TYPE_MASK:
            if (ConditionValue1 & TYPEMASK_UNIT)
                mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            if (ConditionValue1 & TYPEMASK_PLAYER)
                mask |= GRID_MAP_TYPE_MASK_PLAYER;
            if (ConditionValue1 & TYPEMASK_GAMEOBJECT)
                mask |= GRID_MAP_TYPE_MASK_GAMEOBJECT;
            if (ConditionValue1 & TYPEMASK_CORPSE)
                mask |= GRID_MAP_TYPE_MASK_CORPSE;
            break;
        case CONDITION_RELATION_TO:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_REACTION_TO:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_DISTANCE_TO:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_ALIVE:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_HP_VAL:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_HP_PCT:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_WORLD_STATE:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_PHASEMASK:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_TITLE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_SPAWNMASK:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_GENDER:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_UNIT_STATE:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_CREATURE_TYPE:
            mask |= GRID_MAP_TYPE_MASK_CREATURE;
            break;
        case CONDITION_REALM_ACHIEVEMENT:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_IN_WATER:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_STAND_STATE:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_DAILY_QUEST_DONE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_CHARMED:
            mask |= GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_PET_TYPE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_TAXI:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_QUESTSTATE:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_QUEST_OBJECTIVE_PROGRESS:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        case CONDITION_DIFFICULTY_ID:
            mask |= GRID_MAP_TYPE_MASK_ALL;
            break;
        case CONDITION_GAMEMASTER:
            mask |= GRID_MAP_TYPE_MASK_PLAYER;
            break;
        default:
            ABORT_MSG("Condition::GetSearcherTypeMaskForCondition - missing condition handling!");
            break;
    }
    return mask;
}

uint32 Condition::GetMaxAvailableConditionTargets() const
{
    // returns number of targets which are available for given source type
    switch (SourceType)
    {
        case CONDITION_SOURCE_TYPE_SPELL:
        case CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET:
        case CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE:
        case CONDITION_SOURCE_TYPE_VEHICLE_SPELL:
        case CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT:
        case CONDITION_SOURCE_TYPE_GOSSIP_MENU:
        case CONDITION_SOURCE_TYPE_GOSSIP_MENU_OPTION:
        case CONDITION_SOURCE_TYPE_SMART_EVENT:
        case CONDITION_SOURCE_TYPE_NPC_VENDOR:
        case CONDITION_SOURCE_TYPE_SPELL_PROC:
            return 2;
        default:
            return 1;
    }
}

std::string Condition::ToString(bool ext /*= false*/) const
{
    std::ostringstream ss;
    ss << "[Condition ";
    ss << "SourceType: " << SourceType;
    if (SourceType < CONDITION_SOURCE_TYPE_MAX)
        ss << " (" << ConditionMgr::StaticSourceTypeData[SourceType] << ")";
    else
        ss << " (Unknown)";
    if (ConditionMgr::CanHaveSourceGroupSet(SourceType))
        ss << ", SourceGroup: " << SourceGroup;
    ss << ", SourceEntry: " << SourceEntry;
    if (ConditionMgr::CanHaveSourceIdSet(SourceType))
        ss << ", SourceId: " << SourceId;

    if (ext)
    {
        ss << ", ConditionType: " << ConditionType;
        if (ConditionType < CONDITION_MAX)
            ss << " (" << ConditionMgr::StaticConditionTypeData[ConditionType].Name << ")";
        else
            ss << " (Unknown)";
    }

    ss << "]";
    return ss.str();
}

ConditionMgr::ConditionMgr() { }

ConditionMgr::~ConditionMgr()
{
    Clean();
}

uint32 ConditionMgr::GetSearcherTypeMaskForConditionList(ConditionContainer const& conditions) const
{
    if (conditions.empty())
        return GRID_MAP_TYPE_MASK_ALL;
    //     groupId, typeMask
    std::map<uint32, uint32> elseGroupSearcherTypeMasks;
    for (ConditionContainer::const_iterator i = conditions.begin(); i != conditions.end(); ++i)
    {
        // no point of having not loaded conditions in list
        ASSERT((*i)->isLoaded() && "ConditionMgr::GetSearcherTypeMaskForConditionList - not yet loaded condition found in list");
        std::map<uint32, uint32>::const_iterator itr = elseGroupSearcherTypeMasks.find((*i)->ElseGroup);
        // group not filled yet, fill with widest mask possible
        if (itr == elseGroupSearcherTypeMasks.end())
            elseGroupSearcherTypeMasks[(*i)->ElseGroup] = GRID_MAP_TYPE_MASK_ALL;
        // no point of checking anymore, empty mask
        else if (!itr->second)
            continue;

        if ((*i)->ReferenceId) // handle reference
        {
            ConditionReferenceContainer::const_iterator ref = ConditionReferenceStore.find((*i)->ReferenceId);
            ASSERT(ref != ConditionReferenceStore.end() && "ConditionMgr::GetSearcherTypeMaskForConditionList - incorrect reference");
            elseGroupSearcherTypeMasks[(*i)->ElseGroup] &= GetSearcherTypeMaskForConditionList((*ref).second);
        }
        else // handle normal condition
        {
            // object will match conditions in one ElseGroupStore only when it matches all of them
            // so, let's find a smallest possible mask which satisfies all conditions
            elseGroupSearcherTypeMasks[(*i)->ElseGroup] &= (*i)->GetSearcherTypeMaskForCondition();
        }
    }
    // object will match condition when one of the checks in ElseGroupStore is matching
    // so, let's include all possible masks
    uint32 mask = 0;
    for (std::map<uint32, uint32>::const_iterator i = elseGroupSearcherTypeMasks.begin(); i != elseGroupSearcherTypeMasks.end(); ++i)
        mask |= i->second;

    return mask;
}

/**
 * @brief 检查对象是否满足条件列表（核心实现）
 * @param sourceInfo 条件源信息
 * @param conditions 条件列表
 * @return 是否满足条件
 *
 * ElseGroup逻辑说明：
 * - 同一ElseGroup内的条件为AND关系（所有条件都必须满足）
 * - 不同ElseGroup之间为OR关系（任意一个ElseGroup满足即可）
 * - 如果没有ElseGroup（都为0），则所有条件必须满足
 *
 * 示例：
 * 条件1: ElseGroup=0, 需要等级>=10
 * 条件2: ElseGroup=0, 需要职业=战士   (与条件1为AND关系)
 * 条件3: ElseGroup=1, 需要等级>=20
 * 条件4: ElseGroup=1, 需要职业=法师   (与条件3为AND关系)
 *
 * 结果：(等级>=10 AND 职业=战士) OR (等级>=20 AND 职业=法师)
 */
bool ConditionMgr::IsObjectMeetToConditionList(ConditionSourceInfo& sourceInfo, ConditionContainer const& conditions) const
{
    // ElseGroup存储：key=组ID, value=该组是否通过检查
    std::map<uint32, bool> elseGroupStore;

    // 遍历所有条件
    for (Condition const* condition : conditions)
    {
        TC_LOG_DEBUG("condition", "ConditionMgr::IsPlayerMeetToConditionList {} val1: {}", condition->ToString(), condition->ConditionValue1);
        if (condition->isLoaded())
        {
            // 查找ElseGroup在存储中的状态
            std::map<uint32, bool>::const_iterator itr = elseGroupStore.find(condition->ElseGroup);
            // 如果未找到，添加一个条目并设置为true（占位符）
            if (itr == elseGroupStore.end())
                elseGroupStore[condition->ElseGroup] = true;
            else if (!(*itr).second) // 如果该组已有条件不满足，跳过后续检查（该组已经失败）
                continue;

            // 处理条件引用
            if (condition->ReferenceId)
            {
                ConditionReferenceContainer::const_iterator ref = ConditionReferenceStore.find(condition->ReferenceId);
                if (ref != ConditionReferenceStore.end())
                {
                    // 递归检查引用的条件列表
                    if (!IsObjectMeetToConditionList(sourceInfo, ref->second))
                        elseGroupStore[condition->ElseGroup] = false;
                }
                else
                {
                    TC_LOG_DEBUG("condition", "ConditionMgr::IsPlayerMeetToConditionList {} Reference template -{} not found",
                        condition->ToString(), condition->ReferenceId); // 加载时已检查，不应发生
                }

            }
            else // 处理普通条件
            {
                if (!condition->Meets(sourceInfo))
                    elseGroupStore[condition->ElseGroup] = false;
            }
        }
    }

    // 检查是否有任何一个ElseGroup通过
    for (std::map<uint32, bool>::const_iterator i = elseGroupStore.begin(); i != elseGroupStore.end(); ++i)
        if (i->second)
            return true;

    return false;
}

/**
 * @brief 检查对象是否满足条件列表（单对象版本）
 * @param object 要检查的对象
 * @param conditions 条件列表
 * @return 是否满足所有条件
 */
bool ConditionMgr::IsObjectMeetToConditions(WorldObject* object, ConditionContainer const& conditions) const
{
    ConditionSourceInfo srcInfo = ConditionSourceInfo(object);
    return IsObjectMeetToConditions(srcInfo, conditions);
}

/**
 * @brief 检查对象是否满足条件列表（双对象版本）
 * @param object1 第一个对象
 * @param object2 第二个对象
 * @param conditions 条件列表
 * @return 是否满足所有条件
 */
bool ConditionMgr::IsObjectMeetToConditions(WorldObject* object1, WorldObject* object2, ConditionContainer const& conditions) const
{
    ConditionSourceInfo srcInfo = ConditionSourceInfo(object1, object2);
    return IsObjectMeetToConditions(srcInfo, conditions);
}

/**
 * @brief 检查对象是否满足条件列表（完整版本）
 * @param sourceInfo 条件源信息
 * @param conditions 条件列表
 * @return 是否满足所有条件
 *
 * 这是条件检查的主入口函数，处理NegativeCondition逻辑。
 */
bool ConditionMgr::IsObjectMeetToConditions(ConditionSourceInfo& sourceInfo, ConditionContainer const& conditions) const
{
    if (conditions.empty())
        return true;

    TC_LOG_DEBUG("condition", "ConditionMgr::IsObjectMeetToConditions");
    return IsObjectMeetToConditionList(sourceInfo, conditions);
}

bool ConditionMgr::CanHaveSourceGroupSet(ConditionSourceType sourceType)
{
    return (sourceType == CONDITION_SOURCE_TYPE_CREATURE_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_DISENCHANT_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_FISHING_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_GAMEOBJECT_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_ITEM_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_MAIL_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_MILLING_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_PICKPOCKETING_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_PROSPECTING_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_REFERENCE_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_SKINNING_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_SPELL_LOOT_TEMPLATE ||
            sourceType == CONDITION_SOURCE_TYPE_GOSSIP_MENU ||
            sourceType == CONDITION_SOURCE_TYPE_GOSSIP_MENU_OPTION ||
            sourceType == CONDITION_SOURCE_TYPE_VEHICLE_SPELL ||
            sourceType == CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET ||
            sourceType == CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT ||
            sourceType == CONDITION_SOURCE_TYPE_SMART_EVENT ||
            sourceType == CONDITION_SOURCE_TYPE_NPC_VENDOR);
}

bool ConditionMgr::CanHaveSourceIdSet(ConditionSourceType sourceType)
{
    return (sourceType == CONDITION_SOURCE_TYPE_SMART_EVENT);
}

bool ConditionMgr::IsObjectMeetingNotGroupedConditions(ConditionSourceType sourceType, uint32 entry, ConditionSourceInfo& sourceInfo) const
{
    if (sourceType > CONDITION_SOURCE_TYPE_NONE && sourceType < CONDITION_SOURCE_TYPE_MAX)
    {
        ConditionsByEntryMap::const_iterator i = ConditionStore[sourceType].find(entry);
        if (i != ConditionStore[sourceType].end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForNotGroupedEntry: found conditions for type {} and entry {}", uint32(sourceType), entry);
            return IsObjectMeetToConditions(sourceInfo, i->second);
        }
    }

    return true;
}

bool ConditionMgr::IsObjectMeetingNotGroupedConditions(ConditionSourceType sourceType, uint32 entry, WorldObject* target0, WorldObject* target1 /*= nullptr*/, WorldObject* target2 /*= nullptr*/) const
{
    ConditionSourceInfo conditionSource(target0, target1, target2);
    return IsObjectMeetingNotGroupedConditions(sourceType, entry, conditionSource);
}

bool ConditionMgr::HasConditionsForNotGroupedEntry(ConditionSourceType sourceType, uint32 entry) const
{
    if (sourceType > CONDITION_SOURCE_TYPE_NONE && sourceType < CONDITION_SOURCE_TYPE_MAX)
        if (ConditionStore[sourceType].find(entry) != ConditionStore[sourceType].end())
            return true;

    return false;
}

bool ConditionMgr::IsObjectMeetingSpellClickConditions(uint32 creatureId, uint32 spellId, WorldObject* clicker, WorldObject* target) const
{
    ConditionEntriesByCreatureIdMap::const_iterator itr = SpellClickEventConditionStore.find(creatureId);
    if (itr != SpellClickEventConditionStore.end())
    {
        ConditionsByEntryMap::const_iterator i = itr->second.find(spellId);
        if (i != itr->second.end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForSpellClickEvent: found conditions for SpellClickEvent entry {} spell {}", creatureId, spellId);
            ConditionSourceInfo sourceInfo(clicker, target);
            return IsObjectMeetToConditions(sourceInfo, i->second);
        }
    }
    return true;
}

ConditionContainer const* ConditionMgr::GetConditionsForSpellClickEvent(uint32 creatureId, uint32 spellId) const
{
    ConditionEntriesByCreatureIdMap::const_iterator itr = SpellClickEventConditionStore.find(creatureId);
    if (itr != SpellClickEventConditionStore.end())
    {
        ConditionsByEntryMap::const_iterator i = itr->second.find(spellId);
        if (i != itr->second.end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForSpellClickEvent: found conditions for SpellClickEvent entry {} spell {}", creatureId, spellId);
            return &i->second;
        }
    }
    return nullptr;
}

bool ConditionMgr::IsObjectMeetingVehicleSpellConditions(uint32 creatureId, uint32 spellId, Player* player, Unit* vehicle) const
{
    ConditionEntriesByCreatureIdMap::const_iterator itr = VehicleSpellConditionStore.find(creatureId);
    if (itr != VehicleSpellConditionStore.end())
    {
        ConditionsByEntryMap::const_iterator i = itr->second.find(spellId);
        if (i != itr->second.end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForVehicleSpell: found conditions for Vehicle entry {} spell {}", creatureId, spellId);
            ConditionSourceInfo sourceInfo(player, vehicle);
            return IsObjectMeetToConditions(sourceInfo, i->second);
        }
    }
    return true;
}

bool ConditionMgr::IsObjectMeetingSmartEventConditions(int32 entryOrGuid, uint32 eventId, uint32 sourceType, Unit* unit, WorldObject* baseObject) const
{
    SmartEventConditionContainer::const_iterator itr = SmartEventConditionStore.find(std::make_pair(entryOrGuid, sourceType));
    if (itr != SmartEventConditionStore.end())
    {
        ConditionsByEntryMap::const_iterator i = itr->second.find(eventId + 1);
        if (i != itr->second.end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForSmartEvent: found conditions for Smart Event entry or guid {} eventId {}", entryOrGuid, eventId);
            ConditionSourceInfo sourceInfo(unit, baseObject);
            return IsObjectMeetToConditions(sourceInfo, i->second);
        }
    }
    return true;
}

bool ConditionMgr::IsObjectMeetingVendorItemConditions(uint32 creatureId, uint32 itemId, Player* player, Creature* vendor) const
{
    ConditionEntriesByCreatureIdMap::const_iterator itr = NpcVendorConditionContainerStore.find(creatureId);
    if (itr != NpcVendorConditionContainerStore.end())
    {
        ConditionsByEntryMap::const_iterator i = (*itr).second.find(itemId);
        if (i != (*itr).second.end())
        {
            TC_LOG_DEBUG("condition", "GetConditionsForNpcVendorEvent: found conditions for creature entry {} item {}", creatureId, itemId);
            ConditionSourceInfo sourceInfo(player, vendor);
            return IsObjectMeetToConditions(sourceInfo, i->second);
        }
    }
    return true;
}

bool ConditionMgr::IsSpellUsedInSpellClickConditions(uint32 spellId) const
{
    return SpellsUsedInSpellClickConditions.find(spellId) != SpellsUsedInSpellClickConditions.end();
}

ConditionMgr* ConditionMgr::instance()
{
    static ConditionMgr instance;
    return &instance;
}

/**
 * @brief 从数据库加载所有条件定义
 * @param isReload 是否为重新加载（热重载）
 *
 * 调用时机：
 * - 服务器启动时
 * - 执行重载命令时（如.reload conditions）
 *
 * 主要流程：
 * 1. 清理现有的条件数据
 * 2. 如果是重载，重置相关系统（战利品、对话菜单等）
 * 3. 从conditions表读取所有数据
 * 4. 验证并存储每个条件
 * 5. 处理条件引用和分组
 *
 * 性能注意事项：
 * - 此函数耗时较长，会读取整个conditions表
 * - 重新加载会影响游戏性能，建议在低峰期执行
 */
void ConditionMgr::LoadConditions(bool isReload)
{
    uint32 oldMSTime = getMSTime();

    // 清理现有的条件数据
    Clean();

    // 如果是重新加载，必须清除所有自定义处理的分组类型
    if (isReload)
    {
        TC_LOG_INFO("misc", "Reseting Loot Conditions...");
        LootTemplates_Creature.ResetConditions();
        LootTemplates_Fishing.ResetConditions();
        LootTemplates_Gameobject.ResetConditions();
        LootTemplates_Item.ResetConditions();
        LootTemplates_Mail.ResetConditions();
        LootTemplates_Milling.ResetConditions();
        LootTemplates_Pickpocketing.ResetConditions();
        LootTemplates_Reference.ResetConditions();
        LootTemplates_Skinning.ResetConditions();
        LootTemplates_Disenchant.ResetConditions();
        LootTemplates_Prospecting.ResetConditions();
        LootTemplates_Spell.ResetConditions();

        TC_LOG_INFO("misc", "Re-Loading `gossip_menu` Table for Conditions!");
        sObjectMgr->LoadGossipMenu();

        TC_LOG_INFO("misc", "Re-Loading `gossip_menu_option` Table for Conditions!");
        sObjectMgr->LoadGossipMenuItems();
        sSpellMgr->UnloadSpellInfoImplicitTargetConditionLists();
    }

    // 从数据库查询所有条件
    QueryResult result = WorldDatabase.Query("SELECT SourceTypeOrReferenceId, SourceGroup, SourceEntry, SourceId, ElseGroup, ConditionTypeOrReference, ConditionTarget, "
                                             " ConditionValue1, ConditionValue2, ConditionValue3, NegativeCondition, ErrorType, ErrorTextId, ScriptName FROM conditions");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 conditions. DB table `conditions` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        Condition* cond = new Condition();
        int32 iSourceTypeOrReferenceId  = fields[0].GetInt32();
        cond->SourceGroup               = fields[1].GetUInt32();
        cond->SourceEntry               = fields[2].GetInt32();
        cond->SourceId                  = fields[3].GetInt32();
        cond->ElseGroup                 = fields[4].GetUInt32();
        int32 iConditionTypeOrReference = fields[5].GetInt32();
        cond->ConditionTarget           = fields[6].GetUInt8();
        cond->ConditionValue1           = fields[7].GetUInt32();
        cond->ConditionValue2           = fields[8].GetUInt32();
        cond->ConditionValue3           = fields[9].GetUInt32();
        cond->NegativeCondition         = fields[10].GetBool();
        cond->ErrorType                 = fields[11].GetUInt32();
        cond->ErrorTextId               = fields[12].GetUInt32();
        cond->ScriptId                  = sObjectMgr->GetScriptId(fields[13].GetString());

        // 设置条件类型
        if (iConditionTypeOrReference >= 0)
            cond->ConditionType = ConditionTypes(iConditionTypeOrReference);

        // 设置源类型
        if (iSourceTypeOrReferenceId >= 0)
            cond->SourceType = ConditionSourceType(iSourceTypeOrReferenceId);

        // 处理条件引用（负值表示引用其他条件）
        if (iConditionTypeOrReference < 0)
        {
            // 自引用检查
            if (iConditionTypeOrReference == iSourceTypeOrReferenceId)
            {
                TC_LOG_ERROR("sql.sql", "Condition reference {} is referencing self, skipped", iSourceTypeOrReferenceId);
                delete cond;
                continue;
            }
            cond->ReferenceId = uint32(abs(iConditionTypeOrReference));

            char const* rowType = "reference template";
            if (iSourceTypeOrReferenceId >= 0)
                rowType = "reference";

            // 检查无用的数据并记录警告
            if (cond->ConditionTarget)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in ConditionTarget ({})!", rowType, iSourceTypeOrReferenceId, cond->ConditionTarget);
            if (cond->ConditionValue1)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in value1 ({})!", rowType, iSourceTypeOrReferenceId, cond->ConditionValue1);
            if (cond->ConditionValue2)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in value2 ({})!", rowType, iSourceTypeOrReferenceId, cond->ConditionValue2);
            if (cond->ConditionValue3)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in value3 ({})!", rowType, iSourceTypeOrReferenceId, cond->ConditionValue3);
            if (cond->NegativeCondition)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in NegativeCondition ({})!", rowType, iSourceTypeOrReferenceId, cond->NegativeCondition);
            if (cond->SourceGroup && iSourceTypeOrReferenceId < 0)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in SourceGroup ({})!", rowType, iSourceTypeOrReferenceId, cond->SourceGroup);
            if (cond->SourceEntry && iSourceTypeOrReferenceId < 0)
                TC_LOG_ERROR("sql.sql", "Condition {} {} has useless data in SourceEntry ({})!", rowType, iSourceTypeOrReferenceId, cond->SourceEntry);
        }
        else if (!isConditionTypeValid(cond)) // 没有引用，验证条件类型
        {
            delete cond;
            continue;
        }

        // 如果是引用模板，存储到引用容器中
        if (iSourceTypeOrReferenceId < 0)
        {
            ConditionReferenceStore[std::abs(iSourceTypeOrReferenceId)].push_back(cond);
            ++count;
            continue;
        }// 引用模板处理结束

        // 如果不是引用且源类型无效，跳过
        if (iConditionTypeOrReference >= 0 && !isSourceTypeValid(cond))
        {
            delete cond;
            continue;
        }

        // 分组只允许某些类型（战利品模板、对话菜单、对话选项）
        if (cond->SourceGroup && !CanHaveSourceGroupSet(cond->SourceType))
        {
            TC_LOG_ERROR("sql.sql", "{} has not allowed value of SourceGroup = {}!", cond->ToString(), cond->SourceGroup);
            delete cond;
            continue;
        }
        if (cond->SourceId && !CanHaveSourceIdSet(cond->SourceType))
        {
            TC_LOG_ERROR("sql.sql", "{} has not allowed value of SourceId = {}!", cond->ToString(), cond->SourceId);
            delete cond;
            continue;
        }

        // 错误类型只能用于法术条件源
        if (cond->ErrorType && cond->SourceType != CONDITION_SOURCE_TYPE_SPELL)
        {
            TC_LOG_ERROR("sql.sql", "{} can't have ErrorType ({}), set to 0!", cond->ToString(), cond->ErrorType);
            cond->ErrorType = 0;
        }

        // 错误文本ID需要错误类型
        if (cond->ErrorTextId && !cond->ErrorType)
        {
            TC_LOG_ERROR("sql.sql", "{} has any ErrorType, ErrorTextId ({}) is set, set to 0!", cond->ToString(), cond->ErrorTextId);
            cond->ErrorTextId = 0;
        }

        // 处理分组条件
        if (cond->SourceGroup)
        {
            bool valid = false;
            // 处理分组条件
            switch (cond->SourceType)
            {
                case CONDITION_SOURCE_TYPE_CREATURE_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Creature.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_DISENCHANT_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Disenchant.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_FISHING_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Fishing.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_GAMEOBJECT_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Gameobject.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_ITEM_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Item.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_MAIL_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Mail.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_MILLING_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Milling.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_PICKPOCKETING_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Pickpocketing.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_PROSPECTING_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Prospecting.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_REFERENCE_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Reference.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_SKINNING_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Skinning.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_SPELL_LOOT_TEMPLATE:
                    valid = addToLootTemplate(cond, LootTemplates_Spell.GetLootForConditionFill(cond->SourceGroup));
                    break;
                case CONDITION_SOURCE_TYPE_GOSSIP_MENU:
                    valid = addToGossipMenus(cond);
                    break;
                case CONDITION_SOURCE_TYPE_GOSSIP_MENU_OPTION:
                    valid = addToGossipMenuItems(cond);
                    break;
                case CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT:
                {
                    SpellClickEventConditionStore[cond->SourceGroup][cond->SourceEntry].push_back(cond);
                    if (cond->ConditionType == CONDITION_AURA)
                        SpellsUsedInSpellClickConditions.insert(cond->ConditionValue1);
                    valid = true;
                    ++count;
                    continue;   // do not add to m_AllocatedMemory to avoid double deleting
                }
                case CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET:
                    valid = addToSpellImplicitTargetConditions(cond);
                    break;
                case CONDITION_SOURCE_TYPE_VEHICLE_SPELL:
                {
                    VehicleSpellConditionStore[cond->SourceGroup][cond->SourceEntry].push_back(cond);
                    valid = true;
                    ++count;
                    continue;   // do not add to m_AllocatedMemory to avoid double deleting
                }
                case CONDITION_SOURCE_TYPE_SMART_EVENT:
                {
                    //! TODO: PAIR_32 ?
                    std::pair<int32, uint32> key = std::make_pair(cond->SourceEntry, cond->SourceId);
                    SmartEventConditionStore[key][cond->SourceGroup].push_back(cond);
                    valid = true;
                    ++count;
                    continue;
                }
                case CONDITION_SOURCE_TYPE_NPC_VENDOR:
                {
                    NpcVendorConditionContainerStore[cond->SourceGroup][cond->SourceEntry].push_back(cond);
                    valid = true;
                    ++count;
                    continue;
                }
                default:
                    break;
            }

            if (!valid)
            {
                TC_LOG_ERROR("sql.sql", "{} Not handled grouped condition.", cond->ToString());
                delete cond;
            }
            else
            {
                AllocatedMemoryStore.push_back(cond);
                ++count;
            }
            continue;
        }

        //handle not grouped conditions
        //add new Condition to storage based on Type/Entry
        if (cond->SourceType == CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT && cond->ConditionType == CONDITION_AURA)
            SpellsUsedInSpellClickConditions.insert(cond->ConditionValue1);
        ConditionStore[cond->SourceType][cond->SourceEntry].push_back(cond);
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} conditions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

bool ConditionMgr::addToLootTemplate(Condition* cond, LootTemplate* loot) const
{
    if (!loot)
    {
        TC_LOG_ERROR("sql.sql", "{} LootTemplate {} not found.", cond->ToString(), cond->SourceGroup);
        return false;
    }

    if (loot->addConditionItem(cond))
        return true;

    TC_LOG_ERROR("sql.sql", "{} Item {} not found in LootTemplate {}.", cond->ToString(), cond->SourceEntry, cond->SourceGroup);
    return false;
}

bool ConditionMgr::addToGossipMenus(Condition* cond) const
{
    GossipMenusMapBoundsNonConst pMenuBounds = sObjectMgr->GetGossipMenusMapBoundsNonConst(cond->SourceGroup);

    if (pMenuBounds.first != pMenuBounds.second)
    {
        for (GossipMenusContainer::iterator itr = pMenuBounds.first; itr != pMenuBounds.second; ++itr)
        {
            if ((*itr).second.MenuID == cond->SourceGroup && (*itr).second.TextID == uint32(cond->SourceEntry))
            {
                (*itr).second.Conditions.push_back(cond);
                return true;
            }
        }
    }

    TC_LOG_ERROR("sql.sql", "{} GossipMenu {} not found.", cond->ToString(), cond->SourceGroup);
    return false;
}

bool ConditionMgr::addToGossipMenuItems(Condition* cond) const
{
    GossipMenuItemsMapBoundsNonConst pMenuItemBounds = sObjectMgr->GetGossipMenuItemsMapBoundsNonConst(cond->SourceGroup);
    if (pMenuItemBounds.first != pMenuItemBounds.second)
    {
        for (GossipMenuItemsContainer::iterator itr = pMenuItemBounds.first; itr != pMenuItemBounds.second; ++itr)
        {
            if ((*itr).second.MenuID == cond->SourceGroup && (*itr).second.OptionID == uint32(cond->SourceEntry))
            {
                (*itr).second.Conditions.push_back(cond);
                return true;
            }
        }
    }

    TC_LOG_ERROR("sql.sql", "{} GossipMenuId {} Item {} not found.", cond->ToString(), cond->SourceGroup, cond->SourceEntry);
    return false;
}

bool ConditionMgr::addToSpellImplicitTargetConditions(Condition* cond) const
{
    uint32 conditionEffMask = cond->SourceGroup;
    SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->AssertSpellInfo(cond->SourceEntry));
    std::list<uint32> sharedMasks;
    for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
    {
        // additional checks by condition type
        if (conditionEffMask & (1 << spellEffectInfo.EffectIndex))
        {
            switch (cond->ConditionType)
            {
                case CONDITION_OBJECT_ENTRY_GUID:
                {
                    uint32 implicitTargetMask = GetTargetFlagMask(spellEffectInfo.TargetA.GetObjectType()) | GetTargetFlagMask(spellEffectInfo.TargetB.GetObjectType());
                    if ((implicitTargetMask & TARGET_FLAG_UNIT_MASK) && cond->ConditionValue1 != TYPEID_UNIT && cond->ConditionValue1 != TYPEID_PLAYER)
                    {
                        TC_LOG_ERROR("sql.sql", "{} in `condition` table - spell {} EFFECT_{} - "
                            "target requires ConditionValue1 to be either TYPEID_UNIT ({}) or TYPEID_PLAYER ({})", cond->ToString(), spellInfo->Id, uint32(spellEffectInfo.EffectIndex), uint32(TYPEID_UNIT), uint32(TYPEID_PLAYER));
                        return false;
                    }

                    if ((implicitTargetMask & TARGET_FLAG_GAMEOBJECT_MASK) && cond->ConditionValue1 != TYPEID_GAMEOBJECT)
                    {
                        TC_LOG_ERROR("sql.sql", "{} in `condition` table - spell {} EFFECT_{} - "
                            "target requires ConditionValue1 to be TYPEID_GAMEOBJECT ({})", cond->ToString(), spellInfo->Id, uint32(spellEffectInfo.EffectIndex), uint32(TYPEID_GAMEOBJECT));
                        return false;
                    }

                    if ((implicitTargetMask & TARGET_FLAG_CORPSE_MASK) && cond->ConditionValue1 != TYPEID_CORPSE)
                    {
                        TC_LOG_ERROR("sql.sql", "{} in `condition` table - spell {} EFFECT_{} - "
                            "target requires ConditionValue1 to be TYPEID_CORPSE ({})", cond->ToString(), spellInfo->Id, uint32(spellEffectInfo.EffectIndex), uint32(TYPEID_CORPSE));
                        return false;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // check if effect is already a part of some shared mask
        auto itr = std::find_if(sharedMasks.begin(), sharedMasks.end(), [&](uint32 mask) { return !!(mask & (1 << spellEffectInfo.EffectIndex)); });
        if (itr != sharedMasks.end())
            continue;

        // build new shared mask with found effect
        uint32 sharedMask = 1 << spellEffectInfo.EffectIndex;
        ConditionContainer* cmp = spellEffectInfo.ImplicitTargetConditions;
        for (size_t effIndex = spellEffectInfo.EffectIndex + 1; effIndex < spellInfo->GetEffects().size(); ++effIndex)
            if (spellInfo->GetEffect(SpellEffIndex(effIndex)).ImplicitTargetConditions == cmp)
                sharedMask |= 1 << effIndex;

        sharedMasks.push_back(sharedMask);
    }

    for (uint32 effectMask : sharedMasks)
    {
        // some effect indexes should have same data
        if (uint32 commonMask = effectMask & conditionEffMask)
        {
            size_t firstEffIndex = 0;
            for (; firstEffIndex < spellInfo->GetEffects().size(); ++firstEffIndex)
                if ((1 << firstEffIndex) & effectMask)
                    break;

            if (firstEffIndex >= spellInfo->GetEffects().size())
                return false;

            // get shared data
            ConditionContainer* sharedList = spellInfo->GetEffect(SpellEffIndex(firstEffIndex)).ImplicitTargetConditions;

            // there's already data entry for that sharedMask
            if (sharedList)
            {
                // we have overlapping masks in db
                if (conditionEffMask != effectMask)
                {
                    TC_LOG_ERROR("sql.sql", "{} in `condition` table, has incorrect SourceGroup {} (spell effectMask) set - "
                        "effect masks are overlapping (all SourceGroup values having given bit set must be equal) - ignoring.", cond->ToString(), cond->SourceGroup);
                    return false;
                }
            }
            // no data for shared mask, we can create new submask
            else
            {
                // add new list, create new shared mask
                sharedList = new ConditionContainer();
                bool assigned = false;
                for (size_t i = firstEffIndex; i < spellInfo->GetEffects().size(); ++i)
                {
                    if ((1 << i) & commonMask)
                    {
                        const_cast<SpellEffectInfo&>(spellInfo->GetEffect(SpellEffIndex(i))).ImplicitTargetConditions = sharedList;
                        assigned = true;
                    }
                }

                if (!assigned)
                {
                    delete sharedList;
                    return false;
                }
            }
            sharedList->push_back(cond);
            break;
        }
    }
    return true;
}

bool ConditionMgr::isSourceTypeValid(Condition* cond) const
{
    if (cond->SourceType == CONDITION_SOURCE_TYPE_NONE || cond->SourceType >= CONDITION_SOURCE_TYPE_MAX)
    {
        TC_LOG_ERROR("sql.sql", "{} Invalid ConditionSourceType in `condition` table, ignoring.", cond->ToString());
        return false;
    }

    switch (cond->SourceType)
    {
        case CONDITION_SOURCE_TYPE_CREATURE_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Creature.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `creature_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Creature.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_DISENCHANT_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Disenchant.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `disenchant_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Disenchant.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_FISHING_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Fishing.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `fishing_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Fishing.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_GAMEOBJECT_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Gameobject.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `gameobject_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Gameobject.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_ITEM_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Item.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `item_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Item.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_MAIL_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Mail.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `mail_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Mail.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_MILLING_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Milling.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `milling_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Milling.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_PICKPOCKETING_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Pickpocketing.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `pickpocketing_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Pickpocketing.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_PROSPECTING_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Prospecting.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `prospecting_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Prospecting.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_REFERENCE_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Reference.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `reference_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Reference.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_SKINNING_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Skinning.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `skinning_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Skinning.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_SPELL_LOOT_TEMPLATE:
        {
            if (!LootTemplates_Spell.HaveLootFor(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceGroup in `condition` table, does not exist in `spell_loot_template`, ignoring.", cond->ToString());
                return false;
            }

            LootTemplate* loot = LootTemplates_Spell.GetLootForConditionFill(cond->SourceGroup);
            ItemTemplate const* pItemProto = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!pItemProto && !loot->isReference(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceType, SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET:
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cond->SourceEntry);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table does not exist in `spell.dbc`, ignoring.", cond->ToString());
                return false;
            }

            if ((cond->SourceGroup > MAX_EFFECT_MASK) || !cond->SourceGroup)
            {
                TC_LOG_ERROR("sql.sql", "{} in `condition` table, has incorrect SourceGroup (spell effectMask) set, ignoring.", cond->ToString());
                return false;
            }

            uint32 origGroup = cond->SourceGroup;

            for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
            {
                if (!((1 << spellEffectInfo.EffectIndex) & cond->SourceGroup))
                    continue;

                if (spellEffectInfo.ChainTarget > 0)
                    continue;

                switch (spellEffectInfo.TargetA.GetSelectionCategory())
                {
                    case TARGET_SELECT_CATEGORY_NEARBY:
                    case TARGET_SELECT_CATEGORY_CONE:
                    case TARGET_SELECT_CATEGORY_AREA:
                    case TARGET_SELECT_CATEGORY_TRAJ:
                        continue;
                    default:
                        break;
                }

                switch (spellEffectInfo.TargetB.GetSelectionCategory())
                {
                    case TARGET_SELECT_CATEGORY_NEARBY:
                    case TARGET_SELECT_CATEGORY_CONE:
                    case TARGET_SELECT_CATEGORY_AREA:
                    case TARGET_SELECT_CATEGORY_TRAJ:
                        continue;
                    default:
                        break;
                }

                switch (spellEffectInfo.Effect)
                {
                    case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                    case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                    case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
                    case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
                    case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                    case SPELL_EFFECT_APPLY_AREA_AURA_PET:
                    case SPELL_EFFECT_APPLY_AREA_AURA_OWNER:
                        continue;
                    default:
                        break;
                }

                TC_LOG_ERROR("sql.sql", "SourceEntry {} SourceGroup {} in `condition` table - spell {} does not have implicit targets of types: _AREA_, _CONE_, _NEARBY_, __CHAIN__ or is not SPELL_EFFECT_PERSISTENT_AREA_AURA or SPELL_EFFECT_APPLY_AREA_AURA_* for effect {}, SourceGroup needs correction, ignoring.", cond->SourceEntry, origGroup, cond->SourceEntry, uint32(spellEffectInfo.EffectIndex));
                cond->SourceGroup &= ~(1 << spellEffectInfo.EffectIndex);
            }
            // all effects were removed, no need to add the condition at all
            if (!cond->SourceGroup)
                return false;
            break;
        }
        case CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE:
        {
            if (!sObjectMgr->GetCreatureTemplate(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table, does not exist in `creature_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_SPELL:
        case CONDITION_SOURCE_TYPE_SPELL_PROC:
        {
            SpellInfo const* spellProto = sSpellMgr->GetSpellInfo(cond->SourceEntry);
            if (!spellProto)
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table does not exist in `spell.dbc`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_QUEST_AVAILABLE:
            if (!sObjectMgr->GetQuestTemplate(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry specifies non-existing quest, skipped.", cond->ToString());
                return false;
            }
            break;
        case CONDITION_SOURCE_TYPE_VEHICLE_SPELL:
            if (!sObjectMgr->GetCreatureTemplate(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table, does not exist in `creature_template`, ignoring.", cond->ToString());
                return false;
            }

            if (!sSpellMgr->GetSpellInfo(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table does not exist in `spell.dbc`, ignoring.", cond->ToString());
                return false;
            }
            break;
        case CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT:
            if (!sObjectMgr->GetCreatureTemplate(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table, does not exist in `creature_template`, ignoring.", cond->ToString());
                return false;
            }

            if (!sSpellMgr->GetSpellInfo(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table does not exist in `spell.dbc`, ignoring.", cond->ToString());
                return false;
            }
            break;
        case CONDITION_SOURCE_TYPE_NPC_VENDOR:
        {
            if (!sObjectMgr->GetCreatureTemplate(cond->SourceGroup))
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table, does not exist in `creature_template`, ignoring.", cond->ToString());
                return false;
            }
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(cond->SourceEntry);
            if (!itemTemplate)
            {
                TC_LOG_ERROR("sql.sql", "{} SourceEntry in `condition` table, does not exist in `item_template`, ignoring.", cond->ToString());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_AREATRIGGER_CLIENT_TRIGGERED:
        {
            if (!sAreaTriggerStore.LookupEntry(cond->SourceEntry))
            {
                TC_LOG_ERROR("sql.sql", "%s SourceEntry in `condition` table, does not exists in AreaTrigger.dbc, ignoring.", cond->ToString().c_str());
                return false;
            }
            break;
        }
        case CONDITION_SOURCE_TYPE_TERRAIN_SWAP:
        {
            TC_LOG_ERROR("sql.sql", "CONDITION_SOURCE_TYPE_TERRAIN_SWAP: is only for master branch, skipped");
            return false;
        }
        case CONDITION_SOURCE_TYPE_PHASE:
        {
            TC_LOG_ERROR("sql.sql", "CONDITION_SOURCE_TYPE_PHASE: is only for master branch, skipped");
            return false;
        }
        case CONDITION_SOURCE_TYPE_GRAVEYARD:
        {
            TC_LOG_ERROR("sql.sql", "CONDITION_SOURCE_TYPE_GRAVEYARD: is only for master branch, skipped");
            return false;
        }
        case CONDITION_SOURCE_TYPE_AREATRIGGER:
        {
            TC_LOG_ERROR("sql.sql", "CONDITION_SOURCE_TYPE_AREATRIGGER: is only for master branch, skipped");
            return false;
        }
        case CONDITION_SOURCE_TYPE_CONVERSATION_LINE:
        {
            TC_LOG_ERROR("sql.sql", "CONDITION_SOURCE_TYPE_CONVERSATION_LINE: is only for master branch, skipped");
            return false;
        }
        case CONDITION_SOURCE_TYPE_GOSSIP_MENU:
        case CONDITION_SOURCE_TYPE_GOSSIP_MENU_OPTION:
        case CONDITION_SOURCE_TYPE_SMART_EVENT:
        case CONDITION_SOURCE_TYPE_NONE:
        default:
            break;
    }

    return true;
}

bool ConditionMgr::isConditionTypeValid(Condition* cond) const
{
    if (cond->ConditionType == CONDITION_NONE || cond->ConditionType >= CONDITION_MAX)
    {
        TC_LOG_ERROR("sql.sql", "{} Invalid ConditionType in `condition` table, ignoring.", cond->ToString(true));
        return false;
    }

    if (cond->ConditionTarget >= cond->GetMaxAvailableConditionTargets())
    {
        TC_LOG_ERROR("sql.sql", "{} in `condition` table, has incorrect ConditionTarget set, ignoring.", cond->ToString(true));
        return false;
    }

    switch (cond->ConditionType)
    {
        case CONDITION_AURA:
        {
            if (!sSpellMgr->GetSpellInfo(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing spell (Id: {}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }

            if (cond->ConditionValue2 > EFFECT_2)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing effect index ({}) (must be 0..2), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_ITEM:
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(cond->ConditionValue1);
            if (!proto)
            {
                TC_LOG_ERROR("sql.sql", "{} Item ({}) does not exist, skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }

            if (!cond->ConditionValue2)
            {
                TC_LOG_ERROR("sql.sql", "{} Zero item count in ConditionValue2, skipped.", cond->ToString(true));
                return false;
            }
            break;
        }
        case CONDITION_ITEM_EQUIPPED:
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(cond->ConditionValue1);
            if (!proto)
            {
                TC_LOG_ERROR("sql.sql", "{} Item ({}) does not exist, skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_ZONEID:
        {
            AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(cond->ConditionValue1);
            if (!areaEntry)
            {
                TC_LOG_ERROR("sql.sql", "{} Area ({}) does not exist, skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }

            if (areaEntry->ParentAreaID != 0)
            {
                TC_LOG_ERROR("sql.sql", "{} requires to be in area ({}) which is a subzone but zone expected, skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_REPUTATION_RANK:
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(cond->ConditionValue1);
            if (!factionEntry)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing faction ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_TEAM:
        {
            if (cond->ConditionValue1 != ALLIANCE && cond->ConditionValue1 != HORDE)
            {
                TC_LOG_ERROR("sql.sql", "{} specifies unknown team ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_SKILL:
        {
            SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(cond->ConditionValue1);
            if (!pSkill)
            {
                TC_LOG_ERROR("sql.sql", "{} specifies non-existing skill ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }

            if (cond->ConditionValue2 < 1 || cond->ConditionValue2 > sWorld->GetConfigMaxSkillValue())
            {
                TC_LOG_ERROR("sql.sql", "{} specifies skill ({}) with invalid value ({}), skipped.", cond->ToString(true), cond->ConditionValue1, cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_QUESTSTATE:
            if (cond->ConditionValue2 >= (1 << MAX_QUEST_STATUS))
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid state mask ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            [[fallthrough]];
        case CONDITION_QUESTREWARDED:
        case CONDITION_QUESTTAKEN:
        case CONDITION_QUEST_NONE:
        case CONDITION_QUEST_COMPLETE:
        case CONDITION_DAILY_QUEST_DONE:
        {
            if (!sObjectMgr->GetQuestTemplate(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} points to non-existing quest ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_ACTIVE_EVENT:
        {
            GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
            if (cond->ConditionValue1 >= events.size() || !events[cond->ConditionValue1].isValid())
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing event id ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_ACHIEVEMENT:
        {
            AchievementEntry const* achievement = sAchievementMgr->GetAchievement(cond->ConditionValue1);
            if (!achievement)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing achivement id ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_CLASS:
        {
            if (!(cond->ConditionValue1 & CLASSMASK_ALL_PLAYABLE))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing classmask ({}), skipped.", cond->ToString(true), cond->ConditionValue1 & ~CLASSMASK_ALL_PLAYABLE);
                return false;
            }
            break;
        }
        case CONDITION_RACE:
        {
            if (!(cond->ConditionValue1 & RACEMASK_ALL_PLAYABLE))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing racemask ({}), skipped.", cond->ToString(true), cond->ConditionValue1 & ~RACEMASK_ALL_PLAYABLE);
                return false;
            }
            break;
        }
        case CONDITION_GENDER:
        {
            if (!Player::IsValidGender(uint8(cond->ConditionValue1)))
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid gender ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_MAPID:
        {
            MapEntry const* me = sMapStore.LookupEntry(cond->ConditionValue1);
            if (!me)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing map ({}), skipped", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_SPELL:
        {
            if (!sSpellMgr->GetSpellInfo(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing spell (Id: {}), skipped", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_LEVEL:
        {
            if (cond->ConditionValue2 >= COMP_TYPE_MAX)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ComparisionType ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_DRUNKENSTATE:
        {
            if (cond->ConditionValue1 > DRUNKEN_SMASHED)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid state ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_NEAR_CREATURE:
        {
            if (!sObjectMgr->GetCreatureTemplate(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing creature template entry ({}), skipped", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_NEAR_GAMEOBJECT:
        {
            if (!sObjectMgr->GetGameObjectTemplate(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing gameobject template entry ({}), skipped.", cond->ToString(), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_OBJECT_ENTRY_GUID:
        {
            switch (cond->ConditionValue1)
            {
                case TYPEID_UNIT:
                    if (cond->ConditionValue2 && !sObjectMgr->GetCreatureTemplate(cond->ConditionValue2))
                    {
                        TC_LOG_ERROR("sql.sql", "{} has non existing creature template entry ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                        return false;
                    }
                    if (cond->ConditionValue3)
                    {
                        if (CreatureData const* creatureData = sObjectMgr->GetCreatureData(cond->ConditionValue3))
                        {
                            if (cond->ConditionValue2 && creatureData->id != cond->ConditionValue2)
                            {
                                TC_LOG_ERROR("sql.sql", "{} has guid {} set but does not match creature entry ({}), skipped.", cond->ToString(true), cond->ConditionValue3, cond->ConditionValue2);
                                return false;
                            }
                        }
                        else
                        {
                            TC_LOG_ERROR("sql.sql", "{} has non existing creature guid ({}), skipped.", cond->ToString(true), cond->ConditionValue3);
                            return false;
                        }
                    }
                    break;
                case TYPEID_GAMEOBJECT:
                    if (cond->ConditionValue2 && !sObjectMgr->GetGameObjectTemplate(cond->ConditionValue2))
                    {
                        TC_LOG_ERROR("sql.sql", "{} has non existing gameobject template entry ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                        return false;
                    }
                    if (cond->ConditionValue3)
                    {
                        if (GameObjectData const* goData = sObjectMgr->GetGameObjectData(cond->ConditionValue3))
                        {
                            if (cond->ConditionValue2 && goData->id != cond->ConditionValue2)
                            {
                                TC_LOG_ERROR("sql.sql", "{} has guid {} set but does not match gameobject entry ({}), skipped.", cond->ToString(true), cond->ConditionValue3, cond->ConditionValue2);
                                return false;
                            }
                        }
                        else
                        {
                            TC_LOG_ERROR("sql.sql", "{} has non existing gameobject guid ({}), skipped.", cond->ToString(true), cond->ConditionValue3);
                            return false;
                        }
                    }
                    break;
                case TYPEID_PLAYER:
                case TYPEID_CORPSE:
                    if (cond->ConditionValue2)
                        LogUselessConditionValue(cond, 2, cond->ConditionValue2);
                    if (cond->ConditionValue3)
                        LogUselessConditionValue(cond, 3, cond->ConditionValue3);
                    break;
                default:
                    TC_LOG_ERROR("sql.sql", "{} has wrong typeid set ({}), skipped", cond->ToString(true), cond->ConditionValue1);
                    return false;
            }
            break;
        }
        case CONDITION_TYPE_MASK:
        {
            if (!cond->ConditionValue1 || (cond->ConditionValue1 & ~(TYPEMASK_UNIT | TYPEMASK_PLAYER | TYPEMASK_GAMEOBJECT | TYPEMASK_CORPSE)))
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid typemask set ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_RELATION_TO:
        {
            if (cond->ConditionValue1 >= cond->GetMaxAvailableConditionTargets())
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ConditionValue1(ConditionTarget selection) ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue1 == cond->ConditionTarget)
            {
                TC_LOG_ERROR("sql.sql", "{} has ConditionValue1(ConditionTarget selection) set to self ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue2 >= RELATION_MAX)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ConditionValue2(RelationType) ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_REACTION_TO:
        {
            if (cond->ConditionValue1 >= cond->GetMaxAvailableConditionTargets())
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ConditionValue1(ConditionTarget selection) ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue1 == cond->ConditionTarget)
            {
                TC_LOG_ERROR("sql.sql", "{} has ConditionValue1(ConditionTarget selection) set to self ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (!cond->ConditionValue2)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ConditionValue2(rankMask) ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_DISTANCE_TO:
        {
            if (cond->ConditionValue1 >= cond->GetMaxAvailableConditionTargets())
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ConditionValue1(ConditionTarget selection) ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue1 == cond->ConditionTarget)
            {
                TC_LOG_ERROR("sql.sql", "{} has ConditionValue1(ConditionTarget selection) set to self ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue3 >= COMP_TYPE_MAX)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ComparisionType ({}), skipped.", cond->ToString(true), cond->ConditionValue3);
                return false;
            }
            break;
        }
        case CONDITION_HP_VAL:
        {
            if (cond->ConditionValue2 >= COMP_TYPE_MAX)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ComparisionType ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            if (cond->ConditionValue3)
                TC_LOG_ERROR("sql.sql", "{} has useless data in value3 ({})!", cond->ToString(true), cond->ConditionValue3);
            break;
        }
        case CONDITION_HP_PCT:
        {
            if (cond->ConditionValue1 > 100)
            {
                TC_LOG_ERROR("sql.sql", "{} has too big percent value ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            if (cond->ConditionValue2 >= COMP_TYPE_MAX)
            {
                TC_LOG_ERROR("sql.sql", "{} has invalid ComparisionType ({}), skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }
            if (cond->ConditionValue3)
                TC_LOG_ERROR("sql.sql", "{} has useless data in value3 ({})!", cond->ToString(), cond->ConditionValue3);
            break;
        }
        case CONDITION_WORLD_STATE:
        {
            if (!sWorld->getWorldState(cond->ConditionValue1))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing world state in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_TITLE:
        {
            CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(cond->ConditionValue1);
            if (!titleEntry)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing title in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_SPAWNMASK:
        {
            if (cond->ConditionValue1 > SPAWNMASK_RAID_ALL)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing SpawnMask in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_UNIT_STATE:
        {
            if (!(cond->ConditionValue1 & UNIT_STATE_ALL_STATE_SUPPORTED))
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing UnitState in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_CREATURE_TYPE:
        {
            if (!cond->ConditionValue1 || cond->ConditionValue1 > CREATURE_TYPE_GAS_CLOUD)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing CreatureType in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_INSTANCE_INFO:
        case CONDITION_AREAID:
        case CONDITION_PHASEMASK:
        case CONDITION_ALIVE:
            break;
        case CONDITION_REALM_ACHIEVEMENT:
        {
            AchievementEntry const* achievement = sAchievementMgr->GetAchievement(cond->ConditionValue1);
            if (!achievement)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing realm first achivement id ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        }
        case CONDITION_TERRAIN_SWAP:
            TC_LOG_ERROR("sql.sql", "{} is not valid for this branch, skipped.", cond->ToString(true));
            return false;
        case CONDITION_STAND_STATE:
        {
            bool valid = false;
            switch (cond->ConditionValue1)
            {
                case 0:
                    valid = cond->ConditionValue2 <= UNIT_STAND_STATE_SUBMERGED;
                    break;
                case 1:
                    valid = cond->ConditionValue2 <= 1;
                    break;
                default:
                    valid = false;
                    break;
            }
            if (!valid)
            {
                TC_LOG_ERROR("sql.sql", "{} has non-existing stand state ({},{}), skipped.", cond->ToString(true), cond->ConditionValue1, cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_PET_TYPE:
            if (cond->ConditionValue1 >= (1 << MAX_PET_TYPE))
            {
                TC_LOG_ERROR("sql.sql", "{} has non-existing pet type {}, skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        case CONDITION_QUEST_OBJECTIVE_PROGRESS:
        {
            const Quest* quest = sObjectMgr->GetQuestTemplate(cond->ConditionValue1);
            if (!quest)
            {
                TC_LOG_ERROR("sql.sql", "{} points to non-existing quest ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }

            if (cond->ConditionValue2 > 3)
            {
                TC_LOG_ERROR("sql.sql", "{} has out-of-range quest objective index specified ({}), it must be a number between 0 and 3. skipped.", cond->ToString(true), cond->ConditionValue2);
                return false;
            }

            if (quest->RequiredNpcOrGo[cond->ConditionValue2] == 0)
            {
                TC_LOG_ERROR("sql.sql", "{} has quest objective {} for quest {}, but the field RequiredNPCOrGo{} is 0, skipped.", cond->ToString(true), cond->ConditionValue2, cond->ConditionValue1, cond->ConditionValue2);
                return false;
            }

            if (cond->ConditionValue3 > quest->RequiredNpcOrGoCount[cond->ConditionValue2])
            {
                TC_LOG_ERROR("sql.sql", "{} has quest objective count {} in value3, but quest {} has a maximum objective count of {} in RequiredNPCOrGOCount{}, skipped.", cond->ToString(true), cond->ConditionValue3, cond->ConditionValue2, quest->RequiredNpcOrGoCount[cond->ConditionValue2], cond->ConditionValue2);
                return false;
            }
            break;
        }
        case CONDITION_DIFFICULTY_ID:
            if (cond->ConditionValue1 >= MAX_DIFFICULTY)
            {
                TC_LOG_ERROR("sql.sql", "{} has non existing difficulty in value1 ({}), skipped.", cond->ToString(true), cond->ConditionValue1);
                return false;
            }
            break;
        case CONDITION_IN_WATER:
        case CONDITION_CHARMED:
        case CONDITION_TAXI:
        case CONDITION_GAMEMASTER:
        default:
            break;
    }

    if (cond->ConditionValue1 && !StaticConditionTypeData[cond->ConditionType].HasConditionValue1)
        LogUselessConditionValue(cond, 1, cond->ConditionValue1);
    if (cond->ConditionValue2 && !StaticConditionTypeData[cond->ConditionType].HasConditionValue2)
        LogUselessConditionValue(cond, 2, cond->ConditionValue2);
    if (cond->ConditionValue3 && !StaticConditionTypeData[cond->ConditionType].HasConditionValue3)
        LogUselessConditionValue(cond, 3, cond->ConditionValue3);

    return true;
}

void ConditionMgr::LogUselessConditionValue(Condition* cond, uint8 index, uint32 value)
{
    TC_LOG_ERROR("sql.sql", "{} has useless data in ConditionValue{} ({})!", cond->ToString(true), index, value);
}

void ConditionMgr::Clean()
{
    for (ConditionReferenceContainer::iterator itr = ConditionReferenceStore.begin(); itr != ConditionReferenceStore.end(); ++itr)
        for (ConditionContainer::const_iterator it = itr->second.begin(); it != itr->second.end(); ++it)
            delete *it;

    ConditionReferenceStore.clear();

    for (uint32 i = 0; i < CONDITION_SOURCE_TYPE_MAX; ++i)
    {
        for (ConditionsByEntryMap::iterator it = ConditionStore[i].begin(); it != ConditionStore[i].end(); ++it)
            for (ConditionContainer::const_iterator itr = it->second.begin(); itr != it->second.end(); ++itr)
                delete *itr;

        ConditionStore[i].clear();
    }

    for (ConditionEntriesByCreatureIdMap::iterator itr = VehicleSpellConditionStore.begin(); itr != VehicleSpellConditionStore.end(); ++itr)
        for (ConditionsByEntryMap::iterator it = itr->second.begin(); it != itr->second.end(); ++it)
            for (ConditionContainer::const_iterator i = it->second.begin(); i != it->second.end(); ++i)
                delete *i;

    VehicleSpellConditionStore.clear();

    for (SmartEventConditionContainer::iterator itr = SmartEventConditionStore.begin(); itr != SmartEventConditionStore.end(); ++itr)
        for (ConditionsByEntryMap::iterator it = itr->second.begin(); it != itr->second.end(); ++it)
            for (ConditionContainer::const_iterator i = it->second.begin(); i != it->second.end(); ++i)
                delete *i;

    SmartEventConditionStore.clear();

    for (ConditionEntriesByCreatureIdMap::iterator itr = SpellClickEventConditionStore.begin(); itr != SpellClickEventConditionStore.end(); ++itr)
        for (ConditionsByEntryMap::iterator it = itr->second.begin(); it != itr->second.end(); ++it)
            for (ConditionContainer::const_iterator i = it->second.begin(); i != it->second.end(); ++i)
                delete *i;

    SpellClickEventConditionStore.clear();
    SpellsUsedInSpellClickConditions.clear();

    for (ConditionEntriesByCreatureIdMap::iterator itr = NpcVendorConditionContainerStore.begin(); itr != NpcVendorConditionContainerStore.end(); ++itr)
        for (ConditionsByEntryMap::iterator it = itr->second.begin(); it != itr->second.end(); ++it)
            for (ConditionContainer::const_iterator i = it->second.begin(); i != it->second.end(); ++i)
                delete *i;

    NpcVendorConditionContainerStore.clear();

    // this is a BIG hack, feel free to fix it if you can figure out the ConditionMgr ;)
    for (std::vector<Condition*>::const_iterator itr = AllocatedMemoryStore.begin(); itr != AllocatedMemoryStore.end(); ++itr)
        delete *itr;

    AllocatedMemoryStore.clear();
}
