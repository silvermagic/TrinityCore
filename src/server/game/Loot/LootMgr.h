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
 * @file LootMgr.h
 * @brief 战利品管理系统核心模块
 *
 * 该模块实现了魔兽世界服务器的战利品生成系统，负责从各种来源生成战利品内容。
 * 战利品系统是游戏经济和玩家奖励的核心机制之一。
 *
 * 主要功能：
 * 1. 管理多种类型的战利品模板（生物掉落、钓鱼、宝箱、分解等）
 * 2. 从数据库加载战利品模板配置
 * 3. 根据概率规则生成战利品内容
 * 4. 支持引用模板实现配置复用
 * 5. 支持战利品分组和组内随机选择
 * 6. 支持条件系统控制战利品可见性
 *
 * 战利品类型：
 * - creature_loot_template: 生物击杀掉落
 * - fishing_loot_template: 钓鱼获取物品
 * - gameobject_loot_template: 游戏对象（宝箱、矿石等）掉落
 * - item_loot_template: 可打开物品容器内容
 * - mail_loot_template: 邮件附件物品
 * - milling_loot_template: 研磨草药获得颜料
 * - pickpocketing_loot_template: 潜行者偷窃获得物品
 * - prospecting_loot_template: 选矿获得宝石
 * - disenchant_loot_template: 分解装备获得材料
 * - skinning_loot_template: 剥皮获得皮革
 * - spell_loot_template: 法术生成物品
 * - reference_loot_template: 引用模板（可被其他模板引用）
 *
 * 核心概念：
 * 1. LootStore: 战利品存储类，管理特定类型的所有战利品模板
 * 2. LootTemplate: 战利品模板类，定义一个战利品表的所有条目
 * 3. LootStoreItem: 战利品条目，包含物品ID、掉落几率、数量等信息
 * 4. LootGroup: 战利品组，实现"从中选择一个"的掉落机制
 *
 * 掉落机制：
 * 1. 普通掉落：每个条目独立判定是否掉落
 * 2. 分组掉落：从组中随机选择一个条目
 * 3. 引用掉落：引用其他战利品模板的内容
 * 4. 任务掉落：只有接了相关任务的玩家才能看到
 *
 * 性能优化：
 * - 所有验证在加载阶段完成，生成战利品时无需再验证
 * - 使用 hash map 实现模板快速查找
 * - 引用模板实现配置复用，减少数据库存储
 */

#ifndef TRINITY_LOOTMGR_H
#define TRINITY_LOOTMGR_H

#include "Define.h"
#include "ConditionMgr.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <list>
#include <vector>

class LootStore;
class LootTemplate;
class Player;
struct Loot;
struct LootItem;

/**
 * @brief 战利品存储项结构体
 *
 * 表示战利品模板中的一个条目，包含物品ID、掉落几率、数量范围等信息
 */
struct TC_GAME_API LootStoreItem
{
    uint32 itemid;                                         // 物品ID
    uint32 reference;                                      // 引用的模板ID（用于引用其他战利品模板）
    float chance;                                          // 掉落几率（用于任务物品和非任务物品，也用于引用模板的使用几率）
    uint16 lootmode;                                       // 战利品模式标志，用于控制不同情况下的掉落
    bool needs_quest;                                      // 是否需要任务（为true时，只有接了相关任务的玩家才能获得该物品）
    uint8 groupid;                                         // 组ID，同组的物品会进行组内随机
    uint8 mincount;                                        // 最小掉落数量
    uint8 maxcount;                                        // 最大掉落数量（对于引用模板，这是乘数）
    ConditionContainer conditions;                         // 额外的战利品条件容器

    /**
     * @brief 构造函数
     *
     * displayid 在 IsValid() 中填充，必须在构造后调用 IsValid()
     *
     * @param _itemid 物品ID
     * @param _reference 引用的模板ID
     * @param _chance 掉落几率
     * @param _needs_quest 是否需要任务
     * @param _lootmode 战利品模式
     * @param _groupid 组ID
     * @param _mincount 最小数量
     * @param _maxcount 最大数量
     */
    LootStoreItem(uint32 _itemid, uint32 _reference, float _chance, bool _needs_quest, uint16 _lootmode, uint8 _groupid, int32 _mincount, uint8 _maxcount)
        : itemid(_itemid), reference(_reference), chance(_chance), lootmode(_lootmode),
        needs_quest(_needs_quest), groupid(_groupid), mincount(_mincount), maxcount(_maxcount)
         { }

    /**
     * @brief 随机判定是否掉落
     *
     * 在战利品生成时检查该条目是否命中（根据几率判定）
     *
     * @param rate 是否应用掉落倍率
     * @return true 如果命中
     * @return false 如果未命中
     */
    bool Roll(bool rate) const;

    /**
     * @brief 验证数据有效性
     *
     * 检查各项数值的正确性
     *
     * @param store 战利品存储对象
     * @param entry 条目ID
     * @return true 数据有效
     * @return false 数据无效
     */
    bool IsValid(LootStore const& store, uint32 entry) const;
};

// 战利品存储项列表类型
typedef std::list<LootStoreItem*> LootStoreItemList;
// 战利品模板映射类型（ID -> 模板指针）
typedef std::unordered_map<uint32, LootTemplate*> LootTemplateMap;
// 战利品ID集合类型
typedef std::set<uint32> LootIdSet;

/**
 * @brief 战利品存储类
 *
 * 管理特定类型的所有战利品模板（如生物掉落、钓鱼、游戏对象等）
 */
class TC_GAME_API LootStore
{
    public:
        /**
         * @brief 构造函数
         *
         * @param name 存储名称
         * @param entryName 条目名称
         * @param ratesAllowed 是否允许应用掉落倍率
         */
        explicit LootStore(char const* name, char const* entryName, bool ratesAllowed)
            : m_name(name), m_entryName(entryName), m_ratesAllowed(ratesAllowed) { }

        /**
         * @brief 析构函数
         */
        virtual ~LootStore() { Clear(); }

        /**
         * @brief 验证所有模板的有效性
         */
        void Verify() const;

        /**
         * @brief 加载战利品表并收集所有战利品ID
         *
         * @param ids_set 用于存储收集到的战利品ID集合
         * @return uint32 加载的模板数量
         */
        uint32 LoadAndCollectLootIds(LootIdSet& ids_set);

        /**
         * @brief 检查战利品引用的有效性
         *
         * 检查引用是否存在，并从 ref_set 中移除已验证的引用
         *
         * @param ref_set 引用ID集合（可选）
         */
        void CheckLootRefs(LootIdSet* ref_set = nullptr) const;

        /**
         * @brief 报告未使用的战利品ID
         *
         * @param ids_set 被检查的ID集合
         */
        void ReportUnusedIds(LootIdSet const& ids_set) const;

        /**
         * @brief 报告不存在的战利品ID
         *
         * @param lootId 战利品ID
         * @param ownerType 拥有者类型名称
         * @param ownerId 拥有者ID
         */
        void ReportNonExistingId(uint32 lootId, char const* ownerType, uint32 ownerId) const;

        /**
         * @brief 检查是否存在指定战利品ID的模板
         *
         * @param loot_id 战利品ID
         * @return true 存在该战利品
         * @return false 不存在
         */
        bool HaveLootFor(uint32 loot_id) const { return m_LootTemplates.find(loot_id) != m_LootTemplates.end(); }

        /**
         * @brief 检查是否包含任务掉落物品
         *
         * @param loot_id 战利品ID
         * @return true 包含任务掉落
         * @return false 不包含
         */
        bool HaveQuestLootFor(uint32 loot_id) const;

        /**
         * @brief 检查是否包含玩家当前任务相关的掉落物品
         *
         * @param loot_id 战利品ID
         * @param player 玩家对象
         * @return true 包含玩家当前任务相关的掉落
         * @return false 不包含
         */
        bool HaveQuestLootForPlayer(uint32 loot_id, Player const* player) const;

        /**
         * @brief 获取指定战利品ID的模板
         *
         * @param loot_id 战利品ID
         * @return LootTemplate const* 战利品模板指针（常量版本）
         */
        LootTemplate const* GetLootFor(uint32 loot_id) const;

        /**
         * @brief 重置所有模板的条件
         */
        void ResetConditions();

        /**
         * @brief 获取指定战利品ID的模板（用于条件填充）
         *
         * @param loot_id 战利品ID
         * @return LootTemplate* 战利品模板指针
         */
        LootTemplate* GetLootForConditionFill(uint32 loot_id);

        /**
         * @brief 获取存储名称
         *
         * @return char const* 存储名称
         */
        char const* GetName() const { return m_name; }

        /**
         * @brief 获取条目名称
         *
         * @return char const* 条目名称
         */
        char const* GetEntryName() const { return m_entryName; }

        /**
         * @brief 检查是否允许应用掉落倍率
         *
         * @return true 允许
         * @return false 不允许
         */
        bool IsRatesAllowed() const { return m_ratesAllowed; }
    protected:
        /**
         * @brief 加载战利品表
         *
         * @return uint32 加载的模板数量
         */
        uint32 LoadLootTable();

        /**
         * @brief 清空所有模板
         */
        void Clear();
    private:
        LootTemplateMap m_LootTemplates;                    // 战利品模板映射表
        char const* m_name;                                 // 存储名称
        char const* m_entryName;                            // 条目名称
        bool m_ratesAllowed;                                // 是否允许应用掉落倍率
};

/**
 * @brief 战利品模板类
 *
 * 定义了一个战利品模板，包含多个战利品条目和分组
 * 用于生成具体的战利品内容
 */
class TC_GAME_API LootTemplate
{
    /**
     * @brief 战利品组内部类
     *
     * 一组战利品定义的集合（组内不允许包含引用）
     */
    class LootGroup;
    typedef std::vector<LootGroup*> LootGroups;            // 战利品组列表类型

    public:
        /**
         * @brief 默认构造函数
         */
        LootTemplate() { }

        /**
         * @brief 析构函数
         */
        ~LootTemplate();

        /**
         * @brief 添加条目到模板
         *
         * 在加载阶段向模板添加一个条目
         *
         * @param item 战利品存储项指针
         */
        void AddEntry(LootStoreItem* item);

        /**
         * @brief 处理战利品模板
         *
         * 对模板中的每个物品进行随机判定，并将成功掉落的物品添加到战利品对象中
         *
         * @param loot 战利品对象引用
         * @param rate 是否应用掉落倍率
         * @param lootMode 战利品模式
         * @param groupId 组ID（默认为0）
         */
        void Process(Loot& loot, bool rate, uint16 lootMode, uint8 groupId = 0) const;

        /**
         * @brief 复制条件到所有条目
         *
         * @param conditions 条件容器
         */
        void CopyConditions(ConditionContainer const& conditions);

        /**
         * @brief 复制条件到指定战利品物品
         *
         * @param li 战利品物品指针
         */
        void CopyConditions(LootItem* li) const;

        /**
         * @brief 检查是否包含任务掉落
         *
         * 检查模板是否包含至少一个任务掉落条目
         *
         * @param store 战利品模板映射
         * @param groupId 组ID（默认为0）
         * @return true 包含任务掉落
         * @return false 不包含
         */
        bool HasQuestDrop(LootTemplateMap const& store, uint8 groupId = 0) const;

        /**
         * @brief 检查是否包含玩家当前任务相关的掉落
         *
         * 检查模板是否包含至少一个与玩家当前任务相关的掉落条目
         *
         * @param store 战利品模板映射
         * @param player 玩家对象
         * @param groupId 组ID（默认为0）
         * @return true 包含玩家当前任务相关的掉落
         * @return false 不包含
         */
        bool HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId = 0) const;

        /**
         * @brief 验证模板的完整性
         *
         * @param store 战利品存储对象
         * @param Id 模板ID
         */
        void Verify(LootStore const& store, uint32 Id) const;

        /**
         * @brief 检查战利品引用
         *
         * @param store 战利品模板映射
         * @param ref_set 引用ID集合
         */
        void CheckLootRefs(LootTemplateMap const& store, LootIdSet* ref_set) const;

        /**
         * @brief 为物品添加条件
         *
         * @param cond 条件指针
         * @return true 添加成功
         * @return false 添加失败
         */
        bool addConditionItem(Condition* cond);

        /**
         * @brief 检查指定ID是否为引用
         *
         * @param id 条目ID
         * @return true 是引用
         * @return false 不是引用
         */
        bool isReference(uint32 id);

    private:
        LootStoreItemList Entries;                          // 非分组条目列表（仅包含未分组的条目）
        LootGroups        Groups;                           // 战利品组列表（分组条目存储于此，组有独立的优化处理逻辑）

        // 此类的对象绝不能被复制，因为我们在容器中存储指针
        LootTemplate(LootTemplate const&) = delete;
        LootTemplate& operator=(LootTemplate const&) = delete;
};

//=====================================================

// 全局战利品存储对象声明
TC_GAME_API extern LootStore LootTemplates_Creature;         // 生物掉落战利品存储
TC_GAME_API extern LootStore LootTemplates_Fishing;          // 钓鱼战利品存储
TC_GAME_API extern LootStore LootTemplates_Gameobject;       // 游戏对象（如宝箱）战利品存储
TC_GAME_API extern LootStore LootTemplates_Item;             // 物品容器（如礼包）战利品存储
TC_GAME_API extern LootStore LootTemplates_Mail;             // 邮件战利品存储
TC_GAME_API extern LootStore LootTemplates_Milling;          // 研磨（草药）战利品存储
TC_GAME_API extern LootStore LootTemplates_Pickpocketing;    // 偷窃战利品存储
TC_GAME_API extern LootStore LootTemplates_Reference;        // 引用战利品模板存储
TC_GAME_API extern LootStore LootTemplates_Skinning;         // 剥皮战利品存储
TC_GAME_API extern LootStore LootTemplates_Disenchant;       // 分解战利品存储
TC_GAME_API extern LootStore LootTemplates_Prospecting;      // 选矿（矿石）战利品存储
TC_GAME_API extern LootStore LootTemplates_Spell;            // 法术战利品存储

// 战利品模板加载函数声明
TC_GAME_API void LoadLootTemplates_Creature();               // 加载生物掉落模板
TC_GAME_API void LoadLootTemplates_Fishing();                // 加载钓鱼模板
TC_GAME_API void LoadLootTemplates_Gameobject();             // 加载游戏对象模板
TC_GAME_API void LoadLootTemplates_Item();                   // 加载物品容器模板
TC_GAME_API void LoadLootTemplates_Mail();                   // 加载邮件模板
TC_GAME_API void LoadLootTemplates_Milling();                // 加载研磨模板
TC_GAME_API void LoadLootTemplates_Pickpocketing();          // 加载偷窃模板
TC_GAME_API void LoadLootTemplates_Skinning();               // 加载剥皮模板
TC_GAME_API void LoadLootTemplates_Disenchant();             // 加载分解模板
TC_GAME_API void LoadLootTemplates_Prospecting();            // 加载选矿模板

TC_GAME_API void LoadLootTemplates_Spell();                  // 加载法术模板
TC_GAME_API void LoadLootTemplates_Reference();              // 加载引用模板

/**
 * @brief 加载所有战利品表
 *
 * 初始化并加载所有类型的战利品模板数据
 */
TC_GAME_API void LoadLootTables();

#endif
