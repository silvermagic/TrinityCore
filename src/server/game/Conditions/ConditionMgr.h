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
 * @file ConditionMgr.h
 * @brief 条件管理器模块 - 负责游戏中各种条件判断系统的核心实现
 *
 * 本模块提供了灵活的条件判断框架，用于控制游戏中的各种逻辑：
 * - 战利品掉落条件判断
 * - 法术施放条件判断
 * - 任务可用性条件判断
 * - NPC对话菜单条件判断
 * - 物品购买条件判断
 * - 智能AI事件触发条件判断
 *
 * 条件系统支持多种条件类型（如等级、职业、种族、声望、任务完成状态等），
 * 可以组合使用并通过ElseGroup实现复杂的逻辑判断。
 */

#ifndef TRINITY_CONDITIONMGR_H
#define TRINITY_CONDITIONMGR_H

#include "Define.h"
#include "Hash.h"
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Creature;
class Player;
class Unit;
class WorldObject;
class LootTemplate;
struct Condition;

/**
 * @brief 条件类型枚举 - 定义所有可用的条件判断类型
 *
 * 每种条件类型支持不同的参数组合（value1, value2, value3），
 * 用于实现各种游戏逻辑判断。
 */
enum ConditionTypes
{                                                              // value1                 value2         value3
    CONDITION_NONE                     = 0,                    // 0                      0              0                  总是为真
    CONDITION_AURA                     = 1,                    // spell_id               effindex       use target?        如果玩家（或目标，如果value3设置）拥有spell_id的光环效果effindex则为真
    CONDITION_ITEM                     = 2,                    // item_id                count          bank               如果拥有#count个item_id物品则为真（如果设置'bank'则同时搜索银行）
    CONDITION_ITEM_EQUIPPED            = 3,                    // item_id                0              0                  如果装备了item_id则为真
    CONDITION_ZONEID                   = 4,                    // zone_id                0              0                  如果在zone_id区域则为真
    CONDITION_REPUTATION_RANK          = 5,                    // faction_id             rankMask       0                  如果在faction_id阵营拥有rankMask指定的声望等级则为真
    CONDITION_TEAM                     = 6,                    // player_team            0,             0                  469 - 联盟, 67 - 部落
    CONDITION_SKILL                    = 7,                    // skill_id               skill_value    0                  如果skill_id技能达到skill_value值则为真
    CONDITION_QUESTREWARDED            = 8,                    // quest_id               0              0                  如果quest_id任务已奖励则为真
    CONDITION_QUESTTAKEN               = 9,                    // quest_id               0,             0                  如果任务进行中则为真
    CONDITION_DRUNKENSTATE             = 10,                   // DrunkenState           0,             0                  如果玩家醉酒程度足够则为真
    CONDITION_WORLD_STATE              = 11,                   // index                  value          0                  如果世界状态index的值为value则为真
    CONDITION_ACTIVE_EVENT             = 12,                   // event_id               0              0                  如果event_id事件激活则为真
    CONDITION_INSTANCE_INFO            = 13,                   // entry                  data           type               如果由type（InstanceInfo枚举）定义的副本信息等于data则为真
    CONDITION_QUEST_NONE               = 14,                   // quest_id               0              0                  如果没有保存quest_id任务则为真
    CONDITION_CLASS                    = 15,                   // class                  0              0                  如果玩家职业等于class则为真
    CONDITION_RACE                     = 16,                   // race                   0              0                  如果玩家种族等于race则为真
    CONDITION_ACHIEVEMENT              = 17,                   // achievement_id         0              0                  如果achievement_id成就完成则为真
    CONDITION_TITLE                    = 18,                   // title id               0              0                  如果玩家拥有title称号则为真
    CONDITION_SPAWNMASK                = 19,                   // spawnMask              0              0                  如果在spawnMask生成掩码中则为真
    CONDITION_GENDER                   = 20,                   // gender                 0              0                  如果玩家性别等于gender则为真
    CONDITION_UNIT_STATE               = 21,                   // unitState              0              0                  如果单位拥有unitState状态则为真
    CONDITION_MAPID                    = 22,                   // map_id                 0              0                  如果在map_id地图中则为真
    CONDITION_AREAID                   = 23,                   // area_id                0              0                  如果在area_id区域中则为真
    CONDITION_CREATURE_TYPE            = 24,                   // cinfo.type             0              0                  如果creature_template.type = value1则为真
    CONDITION_SPELL                    = 25,                   // spell_id               0              0                  如果玩家已学习spell_id法术则为真
    CONDITION_PHASEMASK                = 26,                   // phasemask              0              0                  如果对象在phasemask相位掩码中则为真
    CONDITION_LEVEL                    = 27,                   // level                  ComparisonType 0                  如果单位等级等于param1（param2可修改比较方式）则为真
    CONDITION_QUEST_COMPLETE           = 28,                   // quest_id               0              0                  如果玩家完成quest_id所有目标但未领奖则为真
    CONDITION_NEAR_CREATURE            = 29,                   // creature entry         distance       dead (0/1)         如果distance范围内有entry生物则为真（dead指定是否死亡）
    CONDITION_NEAR_GAMEOBJECT          = 30,                   // gameobject entry       distance       0                  如果distance范围内有entry游戏对象则为真
    CONDITION_OBJECT_ENTRY_GUID        = 31,                   // TypeID                 entry          guid               如果对象类型为TypeID且entry为0或匹配对象entry或匹配guid则为真
    CONDITION_TYPE_MASK                = 32,                   // TypeMask               0              0                  如果对象类型掩码匹配TypeMask则为真
    CONDITION_RELATION_TO              = 33,                   // ConditionTarget        RelationType   0                  如果对象与ConditionTarget指定的对象有RelationType关系则为真
    CONDITION_REACTION_TO              = 34,                   // ConditionTarget        rankMask       0                  如果对象对ConditionTarget的反应匹配rankMask则为真
    CONDITION_DISTANCE_TO              = 35,                   // ConditionTarget        distance       ComparisonType     如果对象与ConditionTarget在distance距离内则为真
    CONDITION_ALIVE                    = 36,                   // 0                      0              0                  如果单位存活则为真
    CONDITION_HP_VAL                   = 37,                   // hpVal                  ComparisonType 0                  如果单位生命值匹配hpVal则为真
    CONDITION_HP_PCT                   = 38,                   // hpPct                  ComparisonType 0                  如果单位生命值百分比匹配hpPct则为真
    CONDITION_REALM_ACHIEVEMENT        = 39,                   // achievement_id         0              0                  如果服务器成就achievement_id完成则为真
    CONDITION_IN_WATER                 = 40,                   // 0                      0              0                  如果单位在水中则为真
    CONDITION_TERRAIN_SWAP             = 41,                   //                                                          仅用于master分支
    CONDITION_STAND_STATE              = 42,                   // stateType              state          0                  如果单位站姿状态匹配则为真（0,x: 精确状态x; 1,0: 任意站立; 1,1: 任意坐下）
    CONDITION_DAILY_QUEST_DONE         = 43,                   // quest id               0              0                  如果今日已完成daily quest则为真
    CONDITION_CHARMED                  = 44,                   // 0                      0              0                  如果单位被魅惑则为真
    CONDITION_PET_TYPE                 = 45,                   // mask                   0              0                  如果玩家拥有指定类型宠物则为真
    CONDITION_TAXI                     = 46,                   // 0                      0              0                  如果玩家在飞行途中则为真
    CONDITION_QUESTSTATE               = 47,                   // quest_id               state_mask     0                  如果玩家任务状态匹配state_mask则为真（1=未接, 2=完成, 8=进行中, 32=失败, 64=已奖励）
    CONDITION_QUEST_OBJECTIVE_PROGRESS = 48,                   // quest_id               objectiveIndex objectiveCount     如果玩家任务目标进度达到objectiveCount则为真
    CONDITION_DIFFICULTY_ID            = 49,                   // Difficulty             0              0                  如果地图难度ID匹配则为真
    CONDITION_GAMEMASTER               = 50,                   // canBeGM                0              0                  如果玩家是GM（或可以是GM）则为真
    CONDITION_OBJECT_ENTRY_GUID_MASTER = 51,                   // TypeID                 entry          guid               使用master分支TypeID检查对象类型、entry或guid匹配
    CONDITION_TYPE_MASK_MASTER         = 52,                   // TypeMask               0              0                  使用master分支TypeMask检查对象类型掩码
    CONDITION_BATTLE_PET_COUNT         = 53,                   // SpecieId               count          ComparisonType     如果玩家拥有count个战斗宠物物种则为真
    CONDITION_SCENARIO_STEP            = 54,                   // ScenarioStepId         0              0                  如果玩家在场景战役中当前步骤等于ScenarioStepID则为真
    CONDITION_SCENE_IN_PROGRESS        = 55,                   // SceneScriptPackageId   0              0                  如果玩家正在播放ScriptPackageId场景则为真
    CONDITION_PLAYER_CONDITION         = 56,                   // PlayerConditionId      0              0                  如果玩家满足PlayerCondition条件则为真
    CONDITION_MAX
};

/**
 * @brief 实现新ConditionSourceType的文档说明
 *
 * 步骤1: 查找最低的可用ID。在枚举中查找CONDITION_SOURCE_TYPE_UNUSED_XX，
 *        然后定义新的源类型。
 *
 * 步骤2: 确定并映射新条件类型的参数。
 *
 * 步骤3: 在ConditionMgr::isSourceTypeValid中添加case块，
 *        验证参数的有效性。
 *
 * 步骤4: 如果条件可以分组（在步骤2中确定），
 *        在ConditionMgr::CanHaveSourceGroupSet中添加规则。
 *
 * 步骤5: 在ConditionMgr::GetMaxAvailableConditionTargets中定义最大可用条件目标数量。
 *
 * 以下步骤仅适用于可分组条件：
 *
 * 步骤6: 确定如何存储条件。需要在ConditionMgr类中添加新的存储容器，
 *        以及类似如下的函数：
 *        ConditionList GetConditionsForXXXYourNewSourceTypeXXX(parameters...)
 *
 *        上述函数应放在实际检查条件的高层（实用）代码中。
 *
 * 步骤7: 在ConditionMgr::LoadConditions中实现源类型的加载。
 *
 * 步骤8: 在ConditionMgr::Clean中实现源类型的内存清理。
*/

/**
 * @brief 条件源类型枚举 - 定义条件判断的来源和应用场景
 *
 * 每种源类型对应不同的游戏系统，条件会在相应系统中被检查。
 */
enum ConditionSourceType
{
    CONDITION_SOURCE_TYPE_NONE                           = 0,   // 无源类型
    CONDITION_SOURCE_TYPE_CREATURE_LOOT_TEMPLATE         = 1,   // 生物战利品模板
    CONDITION_SOURCE_TYPE_DISENCHANT_LOOT_TEMPLATE       = 2,   // 分解战利品模板
    CONDITION_SOURCE_TYPE_FISHING_LOOT_TEMPLATE          = 3,   // 钓鱼战利品模板
    CONDITION_SOURCE_TYPE_GAMEOBJECT_LOOT_TEMPLATE       = 4,   // 游戏对象战利品模板
    CONDITION_SOURCE_TYPE_ITEM_LOOT_TEMPLATE             = 5,   // 物品战利品模板
    CONDITION_SOURCE_TYPE_MAIL_LOOT_TEMPLATE             = 6,   // 邮件战利品模板
    CONDITION_SOURCE_TYPE_MILLING_LOOT_TEMPLATE          = 7,   // 研磨战利品模板
    CONDITION_SOURCE_TYPE_PICKPOCKETING_LOOT_TEMPLATE    = 8,   // 搜索战利品模板
    CONDITION_SOURCE_TYPE_PROSPECTING_LOOT_TEMPLATE      = 9,   // 探矿战利品模板
    CONDITION_SOURCE_TYPE_REFERENCE_LOOT_TEMPLATE        = 10,  // 引用战利品模板
    CONDITION_SOURCE_TYPE_SKINNING_LOOT_TEMPLATE         = 11,  // 剥皮战利品模板
    CONDITION_SOURCE_TYPE_SPELL_LOOT_TEMPLATE            = 12,  // 法术战利品模板
    CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET          = 13,  // 法术隐式目标
    CONDITION_SOURCE_TYPE_GOSSIP_MENU                    = 14,  // 对话菜单
    CONDITION_SOURCE_TYPE_GOSSIP_MENU_OPTION             = 15,  // 对话菜单选项
    CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE      = 16,  // 生物载具模板
    CONDITION_SOURCE_TYPE_SPELL                          = 17,  // 法术
    CONDITION_SOURCE_TYPE_SPELL_CLICK_EVENT              = 18,  // 法术点击事件
    CONDITION_SOURCE_TYPE_QUEST_AVAILABLE                = 19,  // 任务可用性
    // Condition source type 20 unused - 未使用
    CONDITION_SOURCE_TYPE_VEHICLE_SPELL                  = 21,  // 载具法术
    CONDITION_SOURCE_TYPE_SMART_EVENT                    = 22,  // 智能事件（SAI）
    CONDITION_SOURCE_TYPE_NPC_VENDOR                     = 23,  // NPC商人
    CONDITION_SOURCE_TYPE_SPELL_PROC                     = 24,  // 法术触发
    CONDITION_SOURCE_TYPE_TERRAIN_SWAP                   = 25,  // 仅master分支 - 地形交换
    CONDITION_SOURCE_TYPE_PHASE                          = 26,  // 仅master分支 - 相位
    CONDITION_SOURCE_TYPE_GRAVEYARD                      = 27,  // 仅master分支 - 墓地
    CONDITION_SOURCE_TYPE_AREATRIGGER                    = 28,  // 仅master分支 - 区域触发器（动态生成，非AreaTrigger.dbc/db2中的）
    CONDITION_SOURCE_TYPE_CONVERSATION_LINE              = 29,  // 仅master分支 - 对话行
    CONDITION_SOURCE_TYPE_AREATRIGGER_CLIENT_TRIGGERED   = 30,  // 客户端触发区域触发器
    CONDITION_SOURCE_TYPE_TRAINER_SPELL                  = 31,  // 仅master分支 - 训练师法术
    CONDITION_SOURCE_TYPE_OBJECT_ID_VISIBILITY           = 32,  // 仅master分支 - 对象ID可见性
    CONDITION_SOURCE_TYPE_SPAWN_GROUP                    = 33,  // 仅master分支 - 生成组
    CONDITION_SOURCE_TYPE_MAX                            = 34   // 最大值
};

/**
 * @brief 关系类型枚举 - 定义对象之间的关系类型
 */
enum RelationType
{
    RELATION_SELF = 0,             // 自身
    RELATION_IN_PARTY,             // 在同一队伍中
    RELATION_IN_RAID_OR_PARTY,     // 在同一团队或队伍中
    RELATION_OWNED_BY,             // 被...拥有
    RELATION_PASSENGER_OF,         // 是...的乘客
    RELATION_CREATED_BY,           // 由...创建
    RELATION_MAX
};

/**
 * @brief 副本信息类型枚举 - 定义副本数据查询的类型
 */
enum InstanceInfo
{
    INSTANCE_INFO_DATA = 0,        // 副本数据
    INSTANCE_INFO_GUID_DATA,       // GUID数据
    INSTANCE_INFO_BOSS_STATE,      // Boss状态
    INSTANCE_INFO_DATA64           // 64位数据
};

/**
 * @brief 最大条件目标数量常量
 */
enum MaxConditionTargets
{
    MAX_CONDITION_TARGETS = 3
};

/**
 * @brief 条件源信息结构体 - 包含条件判断所需的目标对象信息
 *
 * 用于传递条件判断时需要的所有目标对象，
 * 条件可以根据ConditionTarget设置选择不同的目标进行判断。
 */
struct TC_GAME_API ConditionSourceInfo
{
    WorldObject* mConditionTargets[MAX_CONDITION_TARGETS]; // 条件目标数组，最多支持3个目标
    Condition const* mLastFailedCondition;                 // 最后一个失败的条件指针，用于调试
    /**
     * @brief 构造函数
     * @param target0 第一个目标对象
     * @param target1 第二个目标对象（可选）
     * @param target2 第三个目标对象（可选）
     */
    ConditionSourceInfo(WorldObject* target0, WorldObject* target1 = nullptr, WorldObject* target2 = nullptr)
    {
        mConditionTargets[0] = target0;
        mConditionTargets[1] = target1;
        mConditionTargets[2] = target2;
        mLastFailedCondition = nullptr;
    }
};

/**
 * @brief 条件结构体 - 存储单个条件的完整信息
 *
 * 这是条件系统的核心数据结构，包含条件的来源、类型、参数等所有信息。
 * 条件从数据库的conditions表中加载，支持复杂的逻辑组合。
 */
struct TC_GAME_API Condition
{
    ConditionSourceType     SourceType;        // 条件源类型（或引用ID）
    uint32                  SourceGroup;       // 条件源分组ID，用于同一来源的多个条件分组
    int32                   SourceEntry;       // 条件源条目ID，通常是数据库条目（法术ID、物品ID等）
    uint32                  SourceId;          // 条件源ID，目前仅用于CONDITION_SOURCE_TYPE_SMART_EVENT
    uint32                  ElseGroup;         // Else组ID，用于实现OR逻辑（相同ElseGroup的条件为OR关系）
    ConditionTypes          ConditionType;     // 条件类型（或引用）
    uint32                  ConditionValue1;   // 条件参数1，根据ConditionType不同含义不同
    uint32                  ConditionValue2;   // 条件参数2，根据ConditionType不同含义不同
    uint32                  ConditionValue3;   // 条件参数3，根据ConditionType不同含义不同
    uint32                  ErrorType;         // 错误类型，条件不满足时返回的错误代码
    uint32                  ErrorTextId;       // 错误文本ID，自定义错误消息
    uint32                  ReferenceId;       // 引用ID，引用另一个条件定义
    uint32                  ScriptId;          // 脚本ID，关联的脚本
    uint8                   ConditionTarget;   // 条件目标索引（0-2），指定使用哪个目标进行条件判断
    bool                    NegativeCondition; // 是否取反结果（true表示条件取反）

    /**
     * @brief 默认构造函数 - 初始化所有成员变量为默认值
     */
    Condition()
    {
        SourceType         = CONDITION_SOURCE_TYPE_NONE;
        SourceGroup        = 0;
        SourceEntry        = 0;
        SourceId           = 0;
        ElseGroup          = 0;
        ConditionType      = CONDITION_NONE;
        ConditionTarget    = 0;
        ConditionValue1    = 0;
        ConditionValue2    = 0;
        ConditionValue3    = 0;
        ReferenceId        = 0;
        ErrorType          = 0;
        ErrorTextId        = 0;
        ScriptId           = 0;
        NegativeCondition  = false;
    }

    /**
     * @brief 判断条件是否满足
     * @param sourceInfo 条件源信息，包含目标对象
     * @return 条件是否满足
     *
     * 这是条件判断的核心函数，会根据ConditionType调用相应的判断逻辑。
     */
    bool Meets(ConditionSourceInfo& sourceInfo) const;

    /**
     * @brief 获取条件的搜索者类型掩码
     * @return 搜索者类型掩码
     *
     * 用于确定哪些类型的对象可以满足此条件。
     */
    uint32 GetSearcherTypeMaskForCondition() const;

    /**
     * @brief 检查条件是否已加载
     * @return 如果条件类型大于CONDITION_NONE或有引用ID则返回true
     */
    bool isLoaded() const { return ConditionType > CONDITION_NONE || ReferenceId; }

    /**
     * @brief 获取最大可用条件目标数量
     * @return 最大条件目标数量
     */
    uint32 GetMaxAvailableConditionTargets() const;

    /**
     * @brief 转换为字符串表示（用于日志）
     * @param ext 是否输出扩展信息
     * @return 条件的字符串表示
     */
    std::string ToString(bool ext = false) const;
};

// 条件容器类型定义
typedef std::vector<Condition*> ConditionContainer;                                          // 条件列表
typedef std::unordered_map<uint32 /*SourceEntry*/, ConditionContainer> ConditionsByEntryMap; // 按条目ID索引的条件映射
typedef std::array<ConditionsByEntryMap, CONDITION_SOURCE_TYPE_MAX> ConditionEntriesByTypeArray; // 按源类型索引的条件数组
typedef std::unordered_map<uint32, ConditionsByEntryMap> ConditionEntriesByCreatureIdMap;    // 按生物ID索引的条件映射
typedef std::unordered_map<std::pair<int32, uint32 /*SAI source_type*/>, ConditionsByEntryMap> SmartEventConditionContainer; // 智能事件条件容器
typedef std::unordered_map<uint32, ConditionContainer> ConditionReferenceContainer;          // 条件引用容器（仅用于引用）

/**
 * @brief 条件管理器类 - 负责条件的加载、存储和判断
 *
 * 这是条件系统的核心管理类，提供以下功能：
 * - 从数据库加载条件定义
 * - 存储和管理所有条件
 * - 提供条件判断接口
 * - 支持条件引用和分组逻辑
 *
 * 使用单例模式，通过sConditionMgr宏访问。
 */
class TC_GAME_API ConditionMgr
{
    private:
        ConditionMgr();
        ~ConditionMgr();

    public:
        /**
         * @brief 获取单例实例
         * @return ConditionMgr单例指针
         */
        static ConditionMgr* instance();

        /**
         * @brief 从数据库加载所有条件
         * @param isReload 是否为重新加载
         *
         * 调用时机：服务器启动时或重载条件时
         * 性能注意：加载过程较慢，会读取整个conditions表
         */
        void LoadConditions(bool isReload = false);

        /**
         * @brief 验证条件类型是否有效
         * @param cond 条件指针
         * @return 条件是否有效
         */
        bool isConditionTypeValid(Condition* cond) const;

        /**
         * @brief 获取条件列表的搜索者类型掩码
         * @param conditions 条件列表
         * @return 搜索者类型掩码
         */
        uint32 GetSearcherTypeMaskForConditionList(ConditionContainer const& conditions) const;

        /**
         * @brief 检查对象是否满足条件列表
         * @param object 要检查的对象
         * @param conditions 条件列表
         * @return 是否满足所有条件
         */
        bool IsObjectMeetToConditions(WorldObject* object, ConditionContainer const& conditions) const;

        /**
         * @brief 检查对象是否满足条件列表（双对象版本）
         * @param object1 第一个对象
         * @param object2 第二个对象
         * @param conditions 条件列表
         * @return 是否满足所有条件
         */
        bool IsObjectMeetToConditions(WorldObject* object1, WorldObject* object2, ConditionContainer const& conditions) const;

        /**
         * @brief 检查对象是否满足条件列表（完整版本）
         * @param sourceInfo 条件源信息
         * @param conditions 条件列表
         * @return 是否满足所有条件
         */
        bool IsObjectMeetToConditions(ConditionSourceInfo& sourceInfo, ConditionContainer const& conditions) const;

        /**
         * @brief 检查源类型是否可以设置分组
         * @param sourceType 条件源类型
         * @return 是否支持分组
         */
        static bool CanHaveSourceGroupSet(ConditionSourceType sourceType);

        /**
         * @brief 检查源类型是否可以设置源ID
         * @param sourceType 条件源类型
         * @return 是否支持源ID
         */
        static bool CanHaveSourceIdSet(ConditionSourceType sourceType);

        /**
         * @brief 检查对象是否满足非分组条件
         * @param sourceType 条件源类型
         * @param entry 条目ID
         * @param sourceInfo 条件源信息
         * @return 是否满足条件
         */
        bool IsObjectMeetingNotGroupedConditions(ConditionSourceType sourceType, uint32 entry, ConditionSourceInfo& sourceInfo) const;

        /**
         * @brief 检查对象是否满足非分组条件（简化版本）
         * @param sourceType 条件源类型
         * @param entry 条目ID
         * @param target0 第一个目标
         * @param target1 第二个目标（可选）
         * @param target2 第三个目标（可选）
         * @return 是否满足条件
         */
        bool IsObjectMeetingNotGroupedConditions(ConditionSourceType sourceType, uint32 entry, WorldObject* target0, WorldObject* target1 = nullptr, WorldObject* target2 = nullptr) const;

        /**
         * @brief 检查指定条目是否有非分组条件
         * @param sourceType 条件源类型
         * @param entry 条目ID
         * @return 是否存在条件
         */
        bool HasConditionsForNotGroupedEntry(ConditionSourceType sourceType, uint32 entry) const;

        /**
         * @brief 检查法术点击条件
         * @param creatureId 生物ID
         * @param spellId 法术ID
         * @param clicker 点击者
         * @param target 目标对象
         * @return 是否满足条件
         */
        bool IsObjectMeetingSpellClickConditions(uint32 creatureId, uint32 spellId, WorldObject* clicker, WorldObject* target) const;

        /**
         * @brief 获取法术点击事件的条件列表
         * @param creatureId 生物ID
         * @param spellId 法术ID
         * @return 条件列表指针，如果不存在则返回nullptr
         */
        ConditionContainer const* GetConditionsForSpellClickEvent(uint32 creatureId, uint32 spellId) const;

        /**
         * @brief 检查载具法术条件
         * @param creatureId 生物ID
         * @param spellId 法术ID
         * @param player 玩家
         * @param vehicle 载具
         * @return 是否满足条件
         */
        bool IsObjectMeetingVehicleSpellConditions(uint32 creatureId, uint32 spellId, Player* player, Unit* vehicle) const;

        /**
         * @brief 检查智能事件条件
         * @param entryOrGuid 条目或GUID
         * @param eventId 事件ID
         * @param sourceType 源类型
         * @param unit 单位对象
         * @param baseObject 基础对象
         * @return 是否满足条件
         */
        bool IsObjectMeetingSmartEventConditions(int32 entryOrGuid, uint32 eventId, uint32 sourceType, Unit* unit, WorldObject* baseObject) const;

        /**
         * @brief 检查NPC商人物品条件
         * @param creatureId 生物ID
         * @param itemId 物品ID
         * @param player 玩家
         * @param vendor 商人NPC
         * @return 是否满足条件
         */
        bool IsObjectMeetingVendorItemConditions(uint32 creatureId, uint32 itemId, Player* player, Creature* vendor) const;

        /**
         * @brief 检查法术是否用于法术点击条件
         * @param spellId 法术ID
         * @return 是否用于法术点击条件
         */
        bool IsSpellUsedInSpellClickConditions(uint32 spellId) const;

        /**
         * @brief 条件类型信息结构体
         */
        struct ConditionTypeInfo
        {
            char const* Name;            // 条件类型名称
            bool HasConditionValue1;     // 是否使用条件值1
            bool HasConditionValue2;     // 是否使用条件值2
            bool HasConditionValue3;     // 是否使用条件值3
        };

        // 静态数据数组
        static char const* const StaticSourceTypeData[CONDITION_SOURCE_TYPE_MAX];    // 源类型名称数组
        static ConditionTypeInfo const StaticConditionTypeData[CONDITION_MAX];       // 条件类型信息数组

    private:
        /**
         * @brief 验证条件源类型是否有效
         * @param cond 条件指针
         * @return 源类型是否有效
         */
        bool isSourceTypeValid(Condition* cond) const;

        /**
         * @brief 将条件添加到战利品模板
         * @param cond 条件指针
         * @param loot 战利品模板
         * @return 是否添加成功
         */
        bool addToLootTemplate(Condition* cond, LootTemplate* loot) const;

        /**
         * @brief 将条件添加到对话菜单
         * @param cond 条件指针
         * @return 是否添加成功
         */
        bool addToGossipMenus(Condition* cond) const;

        /**
         * @brief 将条件添加到对话菜单选项
         * @param cond 条件指针
         * @return 是否添加成功
         */
        bool addToGossipMenuItems(Condition* cond) const;

        /**
         * @brief 将条件添加到法术隐式目标
         * @param cond 条件指针
         * @return 是否添加成功
         */
        bool addToSpellImplicitTargetConditions(Condition* cond) const;

        /**
         * @brief 检查对象是否满足条件列表（内部实现）
         * @param sourceInfo 条件源信息
         * @param conditions 条件列表
         * @return 是否满足所有条件
         *
         * 处理ElseGroup逻辑：
         * - 同一ElseGroup内的条件为AND关系
         * - 不同ElseGroup之间为OR关系
         */
        bool IsObjectMeetToConditionList(ConditionSourceInfo& sourceInfo, ConditionContainer const& conditions) const;

        /**
         * @brief 记录无用的条件值（用于调试）
         * @param cond 条件指针
         * @param index 值索引
         * @param value 值
         */
        static void LogUselessConditionValue(Condition* cond, uint8 index, uint32 value);

        /**
         * @brief 清理资源
         *
         * 释放所有分配的条件对象内存
         */
        void Clean();

        // 成员变量
        std::vector<Condition*> AllocatedMemoryStore; // 已分配内存存储，用于垃圾回收

        ConditionEntriesByTypeArray     ConditionStore;                   // 按源类型存储的条件
        ConditionReferenceContainer     ConditionReferenceStore;          // 条件引用存储
        ConditionEntriesByCreatureIdMap VehicleSpellConditionStore;       // 载具法术条件存储
        ConditionEntriesByCreatureIdMap SpellClickEventConditionStore;    // 法术点击事件条件存储
        ConditionEntriesByCreatureIdMap NpcVendorConditionContainerStore; // NPC商人条件存储
        SmartEventConditionContainer    SmartEventConditionStore;         // 智能事件条件存储

        std::unordered_set<uint32> SpellsUsedInSpellClickConditions;     // 用于法术点击条件的法术ID集合
};

#define sConditionMgr ConditionMgr::instance()

#endif
