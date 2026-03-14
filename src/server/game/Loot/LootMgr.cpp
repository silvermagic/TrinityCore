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
 * @file LootMgr.cpp
 * @brief 战利品管理系统核心模块实现
 *
 * 该文件实现了魔兽世界服务器的战利品生成系统，是游戏奖励机制的核心组件。
 *
 * 战利品生成流程：
 * 1. 服务器启动时从数据库加载所有战利品模板
 * 2. 验证模板数据的完整性和有效性
 * 3. 当需要生成战利品时（如生物死亡、打开宝箱）：
 *    a. 查找对应的战利品模板
 *    b. 遍历模板中的每个条目
 *    c. 根据概率判定每个条目是否掉落
 *    d. 对于分组条目，从组中随机选择一个
 *    e. 对于引用条目，递归处理引用的模板
 *    f. 将所有成功判定的物品添加到战利品列表
 *
 * 掉落概率计算：
 * - 基础概率：数据库中配置的 Chance 字段
 * - 品质修正：根据物品品质应用服务器配置的掉落率修正
 * - 引用修正：引用模板使用专用的掉落率修正
 * - 100%概率：直接掉落，无需掷骰
 *
 * 战利品分组机制：
 * - 同组的物品只会掉落其中一个
 * - 支持显式概率条目和等概率条目
 * - 先判定显式概率条目，如果全部失败再从等概率条目中随机选择
 * - 可以实现"必定掉落但随机选择"或"概率掉落"等不同策略
 *
 * 引用模板机制：
 * - 允许一个战利品表引用另一个战利品表
 * - 支持引用倍数（maxcount），可以多次处理引用的模板
 * - 实现配置复用，减少数据库存储和配置工作
 *
 * 条件系统：
 * - 战利品条目可以附加条件（如阵营、职业、技能等）
 * - 条件在发送战利品给客户端时检查，而非生成时
 * - 允许同一战利品表服务不同条件的玩家
 *
 * 性能优化：
 * - 加载阶段完成所有数据验证，避免运行时检查
 * - 使用 hash map 存储模板，实现O(1)查找
 * - 引用检查在加载阶段完成，避免运行时错误
 * - 未使用的战利品ID会在加载时报告警告
 */

#include "LootMgr.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Group.h"
#include "Log.h"
#include "Loot.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Util.h"
#include "World.h"

/**
 * @brief 物品品质到掉落率配置的映射表
 *
 * 将物品品质枚举值映射到对应的服务器配置率（Rates枚举）
 * 用于根据物品品质调整掉落概率
 */
static Rates const qualityToRate[MAX_ITEM_QUALITY] =
{
    RATE_DROP_ITEM_POOR,                                    // ITEM_QUALITY_POOR - 灰色（垃圾）品质
    RATE_DROP_ITEM_NORMAL,                                  // ITEM_QUALITY_NORMAL - 白色（普通）品质
    RATE_DROP_ITEM_UNCOMMON,                                // ITEM_QUALITY_UNCOMMON - 绿色（优秀）品质
    RATE_DROP_ITEM_RARE,                                    // ITEM_QUALITY_RARE - 蓝色（稀有）品质
    RATE_DROP_ITEM_EPIC,                                    // ITEM_QUALITY_EPIC - 紫色（史诗）品质
    RATE_DROP_ITEM_LEGENDARY,                               // ITEM_QUALITY_LEGENDARY - 橙色（传说）品质
    RATE_DROP_ITEM_ARTIFACT,                                // ITEM_QUALITY_ARTIFACT - 金色（神器）品质
};

/**
 * @brief 全局战利品模板存储实例
 *
 * 这些是游戏中各种类型的战利品表的全局存储对象
 * 每个存储对象管理一种特定类型的战利品模板
 */
LootStore LootTemplates_Creature("creature_loot_template",           "creature entry",                  true);       // 生物掉落表 - 击杀生物获得的战利品
LootStore LootTemplates_Disenchant("disenchant_loot_template",       "item disenchant id",              true);       // 附魔分解表 - 分解装备获得的材料
LootStore LootTemplates_Fishing("fishing_loot_template",             "area id",                         true);       // 钓鱼掉落表 - 钓鱼获得的物品
LootStore LootTemplates_Gameobject("gameobject_loot_template",       "gameobject entry",                true);       // 游戏对象掉落表 - 开箱子等获得的物品
LootStore LootTemplates_Item("item_loot_template",                   "item entry",                      true);       // 物品掉落表 - 打开物品容器获得的物品
LootStore LootTemplates_Mail("mail_loot_template",                   "mail template id",                false);      // 邮件掉落表 - 邮件附件中的物品
LootStore LootTemplates_Milling("milling_loot_template",             "item entry (herb)",               true);       // 研磨掉落表 - 研磨草药获得的颜料
LootStore LootTemplates_Pickpocketing("pickpocketing_loot_template", "creature pickpocket lootid",      true);       // 偷窃掉落表 - 潜行偷窃生物获得的物品
LootStore LootTemplates_Prospecting("prospecting_loot_template",     "item entry (ore)",                true);       // 选矿掉落表 - 选矿矿石获得的宝石
LootStore LootTemplates_Reference("reference_loot_template",         "reference id",                    false);      // 引用掉落表 - 可被其他掉落表引用的模板
LootStore LootTemplates_Skinning("skinning_loot_template",           "creature skinning id",            true);       // 剥皮掉落表 - 剥皮生物获得的皮革
LootStore LootTemplates_Spell("spell_loot_template",                 "spell id (random item creating)", false);      // 法术掉落表 - 施放法术获得的随机物品

/**
 * @brief 战利品组无效项选择器
 *
 * 用于在战利品掷骰之前，从组的可能条目中筛选出无效的战利品项
 * 主要检查：
 * 1. 战利品模式是否匹配
 * 2. 是否已达到该物品的最大重复数量
 */
struct LootGroupInvalidSelector
{
    /**
     * @brief 构造函数
     * @param loot 当前战利品容器引用
     * @param lootMode 当前战利品模式
     */
    explicit LootGroupInvalidSelector(Loot const& loot, uint16 lootMode) : _loot(loot), _lootMode(lootMode) { }

    /**
     * @brief 判断战利品项是否无效（应被移除）
     * @param item 待检查的战利品项
     * @return true 如果该项应被移除，false 如果该项有效
     */
    bool operator()(LootStoreItem* item) const
    {
        // 检查战利品模式是否匹配
        if (!(item->lootmode & _lootMode))
            return true;

        // 检查是否已达到该物品的最大重复数量
        uint8 foundDuplicates = 0;
        for (std::vector<LootItem>::const_iterator itr = _loot.items.begin(); itr != _loot.items.end(); ++itr)
            if (itr->itemid == item->itemid)
                if (++foundDuplicates == _loot.maxDuplicates)
                    return true;

        return false;
    }

private:
    Loot const& _loot;      // 战利品容器引用
    uint16 _lootMode;       // 战利品模式标志
};

/**
 * @brief 战利品组类
 *
 * 表示一组战利品定义的集合，用于实现"从中选择一个"的掉落机制
 * 例如：一个组包含多件装备，但玩家只能获得其中一件
 * 注意：组内不允许包含引用条目（refs are not allowed）
 */
class LootTemplate::LootGroup
{
    public:
        LootGroup() { }
        ~LootGroup();

        /**
         * @brief 向组中添加条目（加载阶段使用）
         * @param item 战利品存储项指针
         */
        void AddEntry(LootStoreItem* item);

        /**
         * @brief 检查组中是否包含任务物品
         * @return true 如果组中至少包含1个任务掉落条目
         */
        bool HasQuestDrop() const;

        /**
         * @brief 检查组中是否有玩家当前任务需要的掉落
         * @param player 玩家对象指针
         * @return true 如果组中有玩家当前任务需要的物品
         */
        bool HasQuestDropForPlayer(Player const* player) const;

        /**
         * @brief 处理战利品组
         *
         * 从组中掷骰选择一个物品（如果有的话），并将其添加到战利品容器中
         * @param loot 战利品容器
         * @param lootMode 战利品模式标志
         */
        void Process(Loot& loot, uint16 lootMode) const;

        /**
         * @brief 获取组的原始总概率（不含等概率项）
         * @return 组内所有显式概率条目的概率总和
         */
        float RawTotalChance() const;

        /**
         * @brief 获取组的总概率
         * @return 组的整体掉落概率（考虑等概率项）
         */
        float TotalChance() const;

        /**
         * @brief 验证组配置的有效性
         * @param lootstore 战利品存储对象
         * @param id 条目ID
         * @param group_id 组ID
         */
        void Verify(LootStore const& lootstore, uint32 id, uint8 group_id) const;

        /**
         * @brief 检查组中的引用关系
         * @param store 战利品模板映射
         * @param ref_set 引用ID集合（用于追踪已使用的引用）
         */
        void CheckLootRefs(LootTemplateMap const& store, LootIdSet* ref_set) const;

        /** 获取显式概率条目列表 */
        LootStoreItemList* GetExplicitlyChancedItemList() { return &ExplicitlyChanced; }

        /** 获取等概率条目列表 */
        LootStoreItemList* GetEqualChancedItemList() { return &EqualChanced; }

        /**
         * @brief 复制条件到组内所有条目
         * @param conditions 条件容器
         */
        void CopyConditions(ConditionContainer conditions);

    private:
        LootStoreItemList ExplicitlyChanced;        // 显式概率条目列表 - 在数据库中定义了具体掉落概率
        LootStoreItemList EqualChanced;             // 等概率条目列表 - 概率为0，每个条目概率相等

        /**
         * @brief 从组中掷骰选择一个物品
         * @param loot 战利品容器
         * @param lootMode 战利品模式标志
         * @return 选中的战利品项指针，如果全部失败则返回NULL
         */
        LootStoreItem const* Roll(Loot& loot, uint16 lootMode) const;

        // 此类禁止复制 - 因为存储了指针
        LootGroup(LootGroup const&) = delete;
        LootGroup& operator=(LootGroup const&) = delete;
};

/**
 * @brief 清除所有数据并释放内存
 *
 * 遍历所有战利品模板并删除，然后清空模板映射
 * 用于服务器关闭或重新加载配置时清理资源
 */
void LootStore::Clear()
{
    for (LootTemplateMap::const_iterator itr = m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
        delete itr->second;
    m_LootTemplates.clear();
}

/**
 * @brief 验证战利品存储的有效性
 *
 * 对存储中的每个战利品模板调用其Verify()方法进行有效性检查
 * 实际的验证逻辑在 LootTemplate::Verify() 中实现
 * 用于确保加载的数据配置正确
 */
void LootStore::Verify() const
{
    for (LootTemplateMap::const_iterator i = m_LootTemplates.begin(); i != m_LootTemplates.end(); ++i)
        i->second->Verify(*this, i->first);
}

/**
 * @brief 从数据库加载战利品模板表
 *
 * 从对应的 *_loot_template 数据库表中加载战利品模板数据
 * 所有加载模板的有效性检查都在此处完成，确保生成战利品时无需再次检查
 *
 * @return 成功加载的记录数量
 *
 * 主要流程：
 * 1. 清空现有存储（支持重新加载）
 * 2. 从数据库查询所有战利品条目
 * 3. 遍历每条记录，创建 LootStoreItem 对象
 * 4. 验证每个条目的有效性
 * 5. 将有效条目添加到对应的战利品模板中
 * 6. 验证整个存储的有效性
 */
uint32 LootStore::LoadLootTable()
{
    LootTemplateMap::const_iterator tab;

    // Clearing store (for reloading case)
    Clear();

    //                                                  0     1            2               3         4         5             6
    QueryResult result = WorldDatabase.PQuery("SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount FROM {}", GetName());

    if (!result)
        return 0;

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry               = fields[0].GetUInt32();
        uint32 item                = fields[1].GetUInt32();
        uint32 reference           = fields[2].GetUInt32();
        float  chance              = fields[3].GetFloat();
        bool   needsquest          = fields[4].GetBool();
        uint16 lootmode            = fields[5].GetUInt16();
        uint8  groupid             = fields[6].GetUInt8();
        uint8  mincount            = fields[7].GetUInt8();
        uint8  maxcount            = fields[8].GetUInt8();

        if (groupid >= 1 << 7)                                     // it stored in 7 bit field
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: GroupId ({}) must be less {} - skipped", GetName(), entry, item, groupid, 1 << 7);
            return 0;
        }

        LootStoreItem* storeitem = new LootStoreItem(item, reference, chance, needsquest, lootmode, groupid, mincount, maxcount);

        if (!storeitem->IsValid(*this, entry))            // Validity checks
        {
            delete storeitem;
            continue;
        }

        // Looking for the template of the entry
                                                         // often entries are put together
        if (m_LootTemplates.empty() || tab->first != entry)
        {
            // Searching the template (in case template Id changed)
            tab = m_LootTemplates.find(entry);
            if (tab == m_LootTemplates.end())
            {
                std::pair< LootTemplateMap::iterator, bool > pr = m_LootTemplates.insert(LootTemplateMap::value_type(entry, new LootTemplate()));
                tab = pr.first;
            }
        }
        // else is empty - template Id and iter are the same
        // finally iter refers to already existed or just created <entry, LootTemplate>

        // Adds current row to the template
        tab->second->AddEntry(storeitem);
        ++count;
    }
    while (result->NextRow());

    Verify();                                           // Checks validity of the loot store

    return count;
}

/**
 * @brief 检查指定战利品ID是否有任务掉落
 * @param loot_id 战利品模板ID
 * @return true 如果该战利品表包含至少一个任务物品
 */
bool LootStore::HaveQuestLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator itr = m_LootTemplates.find(loot_id);
    if (itr == m_LootTemplates.end())
        return false;

    // 扫描战利品中的任务物品
    return itr->second->HasQuestDrop(m_LootTemplates);
}

/**
 * @brief 检查指定战利品ID是否有玩家当前任务需要的掉落
 * @param loot_id 战利品模板ID
 * @param player 玩家对象指针
 * @return true 如果该战利品表包含玩家当前任务需要的物品
 */
bool LootStore::HaveQuestLootForPlayer(uint32 loot_id, Player const* player) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);
    if (tab != m_LootTemplates.end())
        if (tab->second->HasQuestDropForPlayer(m_LootTemplates, player))
            return true;

    return false;
}

/**
 * @brief 重置所有战利品模板的条件
 *
 * 清空所有战利品模板中条目的条件列表
 * 用于重新加载配置时清除旧的条件数据
 */
void LootStore::ResetConditions()
{
    for (LootTemplateMap::iterator itr = m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
    {
        ConditionContainer empty;
        itr->second->CopyConditions(empty);
    }
}

/**
 * @brief 获取指定ID的战利品模板（const版本）
 * @param loot_id 战利品模板ID
 * @return 战利品模板指针，如果不存在则返回nullptr
 */
LootTemplate const* LootStore::GetLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return nullptr;

    return tab->second;
}

/**
 * @brief 获取指定ID的战利品模板（用于条件填充）
 * @param loot_id 战利品模板ID
 * @return 战利品模板指针（可修改），如果不存在则返回nullptr
 *
 * 此版本返回可修改的指针，用于在加载时填充条件数据
 */
LootTemplate* LootStore::GetLootForConditionFill(uint32 loot_id)
{
    LootTemplateMap::iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return nullptr;

    return tab->second;
}

/**
 * @brief 加载战利品表并收集所有战利品ID
 * @param lootIdSet 输出参数，用于存储收集到的战利品ID集合
 * @return 成功加载的记录数量
 *
 * 主要流程：
 * 1. 调用 LoadLootTable() 加载战利品数据
 * 2. 遍历所有模板，收集ID到集合中
 */
uint32 LootStore::LoadAndCollectLootIds(LootIdSet& lootIdSet)
{
    uint32 count = LoadLootTable();

    for (LootTemplateMap::const_iterator tab = m_LootTemplates.begin(); tab != m_LootTemplates.end(); ++tab)
        lootIdSet.insert(tab->first);

    return count;
}

/**
 * @brief 检查战利品模板中的引用关系
 * @param ref_set 引用ID集合指针，用于追踪已使用的引用ID
 *
 * 遍历所有战利品模板，检查其中的引用条目是否有效
 */
void LootStore::CheckLootRefs(LootIdSet* ref_set) const
{
    for (LootTemplateMap::const_iterator ltItr = m_LootTemplates.begin(); ltItr != m_LootTemplates.end(); ++ltItr)
        ltItr->second->CheckLootRefs(m_LootTemplates, ref_set);
}

/**
 * @brief 报告未使用的战利品ID
 * @param lootIdSet 未使用的战利品ID集合
 *
 * 输出错误日志，报告那些未被任何地方引用的战利品ID
 * 这些ID对应的战利品模板是无用的配置
 */
void LootStore::ReportUnusedIds(LootIdSet const& lootIdSet) const
{
    // all still listed ids isn't referenced
    for (LootIdSet::const_iterator itr = lootIdSet.begin(); itr != lootIdSet.end(); ++itr)
        TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} isn't {} and not referenced from loot, and thus useless.", GetName(), *itr, GetEntryName());
}

/**
 * @brief 报告不存在的战利品ID
 * @param lootId 不存在的战利品ID
 * @param ownerType 拥有者类型描述（如"Creature"、"Item"等）
 * @param ownerId 拥有者ID
 *
 * 输出错误日志，报告某个实体引用了不存在的战利品模板
 */
void LootStore::ReportNonExistingId(uint32 lootId, char const* ownerType, uint32 ownerId) const
{
    TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} does not exist but it is used by {} {}", GetName(), lootId, ownerType, ownerId);
}

//
// --------- LootStoreItem 战利品存储项实现 ---------
//

/**
 * @brief 掷骰判断该条目是否命中（战利品生成时调用）
 *
 * 根据条目的掉落概率进行随机判定，决定是否生成该物品
 * RATE_DROP_ITEMS 配置不再用于所有类型的条目
 *
 * @param rate 是否应用服务器掉落率修正
 * @return true 如果掷骰成功（应该生成该物品），false 如果失败
 *
 * 判定逻辑：
 * 1. 如果概率>=100%，必定成功
 * 2. 对于引用条目，应用 RATE_DROP_ITEM_REFERENCED 修正
 * 3. 对于物品条目，根据物品品质应用对应的掉落率修正
 */
bool LootStoreItem::Roll(bool rate) const
{
    if (chance >= 100.0f)
        return true;

    if (reference > 0)                                   // reference case
        return roll_chance_f(chance* (rate ? sWorld->getRate(RATE_DROP_ITEM_REFERENCED) : 1.0f));

    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemid);

    float qualityModifier = pProto && rate ? sWorld->getRate(qualityToRate[pProto->Quality]) : 1.0f;

    return roll_chance_f(chance*qualityModifier);
}

/**
 * @brief 检查战利品条目数值的正确性
 *
 * 在加载阶段验证每个战利品条目的配置是否有效
 * 确保战利品生成时无需再进行错误检查
 *
 * @param store 战利品存储对象
 * @param entry 条目ID（用于错误日志）
 * @return true 如果条目配置有效，false 如果无效
 *
 * 验证内容：
 * 1. mincount 必须大于0
 * 2. 物品条目必须存在于 item_template 表中
 * 3. 零概率只允许在分组条目中使用
 * 4. 概率不能过小（<0.000001）
 * 5. maxcount 不能小于 mincount
 * 6. 引用条目必须有非零概率
 */
bool LootStoreItem::IsValid(LootStore const& store, uint32 entry) const
{
    if (mincount == 0)
    {
        TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: wrong MinCount ({}) - skipped", store.GetName(), entry, itemid, mincount);
        return false;
    }

    if (reference == 0)                                      // item (quest or non-quest) entry, maybe grouped
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
        if (!proto)
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: item entry not listed in `item_template` - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance == 0 && groupid == 0)                     // Zero chance is allowed for grouped entries only
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: equal-chanced grouped entry, but group not defined - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance != 0 && chance < 0.000001f)             // loot with low chance
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: low chance ({}) - skipped",
                store.GetName(), entry, itemid, chance);
            return false;
        }

        if (maxcount < mincount)                       // wrong max count
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: MaxCount ({}) less that MinCount ({}) - skipped", store.GetName(), entry, itemid, int32(maxcount), mincount);
            return false;
        }
    }
    else                                                    // if reference loot
    {
        if (needs_quest)
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: quest required will be ignored", store.GetName(), entry, itemid);
        else if (chance == 0)                              // no chance for the reference
        {
            TC_LOG_ERROR("sql.sql", "Table '{}' Entry {} Item {}: zero chance is specified for a reference, skipped", store.GetName(), entry, itemid);
            return false;
        }
    }
    return true;                                            // Referenced template existence is checked at whole store level
}

//
// --------- LootTemplate::LootGroup 战利品组实现 ---------
//

/**
 * @brief 析构函数 - 清理组内所有条目
 *
 * 删除所有显式概率条目和等概率条目，释放内存
 */
LootTemplate::LootGroup::~LootGroup()
{
    while (!ExplicitlyChanced.empty())
    {
        delete ExplicitlyChanced.back();
        ExplicitlyChanced.pop_back();
    }

    while (!EqualChanced.empty())
    {
        delete EqualChanced.back();
        EqualChanced.pop_back();
    }
}

/**
 * @brief 向组中添加条目（加载阶段使用）
 * @param item 战利品存储项指针
 *
 * 根据条目的概率值，将其添加到相应的列表中：
 * - 非零概率条目添加到 ExplicitlyChanced 列表
 * - 零概率条目添加到 EqualChanced 列表
 */
void LootTemplate::LootGroup::AddEntry(LootStoreItem* item)
{
    if (item->chance != 0)
        ExplicitlyChanced.push_back(item);
    else
        EqualChanced.push_back(item);
}

/**
 * @brief 从组中掷骰选择一个物品
 *
 * 首先检查显式概率条目，如果全部失败则从等概率条目中随机选择
 *
 * @param loot 战利品容器
 * @param lootMode 战利品模式标志
 * @return 选中的战利品项指针，如果全部失败则返回nullptr
 *
 * 选择逻辑：
 * 1. 过滤掉无效条目（模式不匹配或重复数量超限）
 * 2. 首先检查显式概率条目：
 *    - 生成随机数
 *    - 遍历条目，减去概率值
 *    - 当随机数小于0时命中该条目
 *    - 100%概率的条目直接返回
 * 3. 如果没有命中，从等概率条目中随机选择一个
 * 4. 如果没有可用条目，返回nullptr（空掉落）
 */
LootStoreItem const* LootTemplate::LootGroup::Roll(Loot& loot, uint16 lootMode) const
{
    LootStoreItemList possibleLoot = ExplicitlyChanced;
    possibleLoot.remove_if(LootGroupInvalidSelector(loot, lootMode));

    if (!possibleLoot.empty())                             // First explicitly chanced entries are checked
    {
        float roll = (float)rand_chance();

        for (LootStoreItemList::const_iterator itr = possibleLoot.begin(); itr != possibleLoot.end(); ++itr)   // check each explicitly chanced entry in the template and modify its chance based on quality.
        {
            LootStoreItem* item = *itr;
            if (item->chance >= 100.0f)
                return item;

            roll -= item->chance;
            if (roll < 0)
                return item;
        }
    }

    possibleLoot = EqualChanced;
    possibleLoot.remove_if(LootGroupInvalidSelector(loot, lootMode));
    if (!possibleLoot.empty())                              // If nothing selected yet - an item is taken from equal-chanced part
        return Trinity::Containers::SelectRandomContainerElement(possibleLoot);

    return nullptr;                                            // 组内空掉落
}

/**
 * @brief 检查组中是否包含任务物品
 * @return true 如果组中至少包含1个任务掉落条目
 */
bool LootTemplate::LootGroup::HasQuestDrop() const
{
    for (LootStoreItemList::const_iterator i = ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if ((*i)->needs_quest)
            return true;

    for (LootStoreItemList::const_iterator i = EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if ((*i)->needs_quest)
            return true;

    return false;
}

/**
 * @brief 检查组中是否有玩家当前任务需要的掉落
 * @param player 玩家对象指针
 * @return true 如果组中有玩家当前任务需要的物品
 */
bool LootTemplate::LootGroup::HasQuestDropForPlayer(Player const* player) const
{
    for (LootStoreItemList::const_iterator i = ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (player->HasQuestForItem((*i)->itemid))
            return true;

    for (LootStoreItemList::const_iterator i = EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if (player->HasQuestForItem((*i)->itemid))
            return true;

    return false;
}

/**
 * @brief 复制条件到组内所有条目（实际清空条件）
 * @param conditions 条件容器（未使用）
 *
 * 遍历组内所有条目并清空其条件列表
 * 用于重新加载时清除旧的条件数据
 */
void LootTemplate::LootGroup::CopyConditions(ConditionContainer /*conditions*/)
{
    for (LootStoreItemList::iterator i = ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        (*i)->conditions.clear();

    for (LootStoreItemList::iterator i = EqualChanced.begin(); i != EqualChanced.end(); ++i)
        (*i)->conditions.clear();
}

/**
 * @brief 处理战利品组
 *
 * 从组中掷骰选择一个物品（如果有命中的），并将其添加到战利品容器中
 *
 * @param loot 战利品容器
 * @param lootMode 战利品模式标志
 */
void LootTemplate::LootGroup::Process(Loot& loot, uint16 lootMode) const
{
    if (LootStoreItem const* item = Roll(loot, lootMode))
        loot.AddItem(*item);
}

/**
 * @brief 计算组的原始总概率（不含等概率项）
 * @return 组内所有显式概率条目的概率总和（仅统计非任务物品）
 */
float LootTemplate::LootGroup::RawTotalChance() const
{
    float result = 0;

    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (!(*i)->needs_quest)
            result += (*i)->chance;

    return result;
}

/**
 * @brief 计算组的总概率
 * @return 组的整体掉落概率（如果存在等概率项且总概率<100%，返回100%）
 */
float LootTemplate::LootGroup::TotalChance() const
{
    float result = RawTotalChance();

    if (!EqualChanced.empty() && result < 100.0f)
        return 100.0f;

    return result;
}

/**
 * @brief 验证组配置的有效性
 * @param lootstore 战利品存储对象
 * @param id 条目ID
 * @param group_id 组ID
 *
 * 检查：
 * 1. 组的总概率是否超过100%
 * 2. 如果总概率>=100%，是否还包含等概率项（配置冲突）
 */
void LootTemplate::LootGroup::Verify(LootStore const& lootstore, uint32 id, uint8 group_id) const
{
    float chance = RawTotalChance();
    if (chance > 101.0f)                                    /// @todo replace with 100% when DBs will be ready
        TC_LOG_ERROR("sql.sql", "Table '{}' entry {} group {} has total chance > 100% ({})", lootstore.GetName(), id, group_id, chance);

    if (chance >= 100.0f && !EqualChanced.empty())
        TC_LOG_ERROR("sql.sql", "Table '{}' entry {} group {} has items with chance=0% but group total chance >= 100% ({})", lootstore.GetName(), id, group_id, chance);
}

/**
 * @brief 检查组中的引用关系
 * @param store 战利品模板映射（未使用）
 * @param ref_set 引用ID集合，用于标记已使用的引用ID
 *
 * 遍历组内所有条目，检查引用条目的有效性，并从ref_set中移除已使用的引用ID
 */
void LootTemplate::LootGroup::CheckLootRefs(LootTemplateMap const& /*store*/, LootIdSet* ref_set) const
{
    for (LootStoreItemList::const_iterator ieItr = ExplicitlyChanced.begin(); ieItr != ExplicitlyChanced.end(); ++ieItr)
    {
        LootStoreItem* item = *ieItr;
        if (item->reference > 0)
        {
            if (!LootTemplates_Reference.GetLootFor(item->reference))
                LootTemplates_Reference.ReportNonExistingId(item->reference, "Reference", item->itemid);
            else if (ref_set)
                ref_set->erase(item->reference);
        }
    }

    for (LootStoreItemList::const_iterator ieItr = EqualChanced.begin(); ieItr != EqualChanced.end(); ++ieItr)
    {
        LootStoreItem* item = *ieItr;
        if (item->reference > 0)
        {
            if (!LootTemplates_Reference.GetLootFor(item->reference))
                LootTemplates_Reference.ReportNonExistingId(item->reference, "Reference", item->itemid);
            else if (ref_set)
                ref_set->erase(item->reference);
        }
    }
}

//
// --------- LootTemplate 战利品模板实现 ---------
//

/**
 * @brief 析构函数 - 清理模板内所有条目和组
 *
 * 删除所有条目和战利品组，释放内存
 */
LootTemplate::~LootTemplate()
{
    for (LootStoreItemList::iterator i = Entries.begin(); i != Entries.end(); ++i)
        delete *i;

    for (size_t i = 0; i < Groups.size(); ++i)
        delete Groups[i];
}

/**
 * @brief 向模板中添加条目（加载阶段使用）
 * @param item 战利品存储项指针
 *
 * 根据条目的groupid属性决定存储位置：
 * - groupid > 0 且非引用：添加到对应的战利品组
 * - 其他情况：添加到普通条目列表（Entries）
 *
 * 如果组不存在，会自动创建新的战利品组
 */
void LootTemplate::AddEntry(LootStoreItem* item)
{
    if (item->groupid > 0 && item->reference == 0)            // Group
    {
        if (item->groupid >= Groups.size())
            Groups.resize(item->groupid, nullptr);               // Adds new group the the loot template if needed
        if (!Groups[item->groupid - 1])
            Groups[item->groupid - 1] = new LootGroup();

        Groups[item->groupid - 1]->AddEntry(item);            // Adds new entry to the group
    }
    else                                                      // Non-grouped entries and references are stored together
        Entries.push_back(item);
}

/**
 * @brief 复制条件到模板内所有条目（实际清空条件）
 * @param conditions 条件容器（未使用）
 *
 * 遍历所有条目和组，清空其条件列表
 * 用于重新加载时清除旧的条件数据
 */
void LootTemplate::CopyConditions(ConditionContainer const& conditions)
{
    for (LootStoreItemList::iterator i = Entries.begin(); i != Entries.end(); ++i)
        (*i)->conditions.clear();

    for (LootGroups::iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (LootGroup* group = *i)
            group->CopyConditions(conditions);
}

/**
 * @brief 将模板项的条件复制到战利品项
 * @param li 战利品项指针
 *
 * 遍历模板条目列表，找到匹配的物品ID，并将其条件列表复制到战利品项
 */
void LootTemplate::CopyConditions(LootItem* li) const
{
    // Copies the conditions list from a template item to a LootItem
    for (LootStoreItemList::const_iterator _iter = Entries.begin(); _iter != Entries.end(); ++_iter)
    {
        LootStoreItem* item = *_iter;
        if (item->itemid != li->itemid)
            continue;

        li->conditions = item->conditions;
        break;
    }
}

/**
 * @brief 处理战利品模板 - 核心掉落生成函数
 *
 * 遍历模板中的每个条目进行掷骰，并将掷骰成功的物品添加到战利品容器中
 *
 * @param loot 战利品容器
 * @param rate 是否应用服务器掉落率修正
 * @param lootMode 战利品模式标志
 * @param groupId 组ID（如果指定，只处理该组）
 *
 * 主要流程：
 * 1. 如果指定了groupId，只处理对应的组
 * 2. 处理非分组条目：
 *    - 检查战利品模式是否匹配
 *    - 进行掷骰判定
 *    - 如果是引用条目，递归处理引用的模板
 *    - 如果是普通条目，直接添加到战利品容器
 * 3. 处理所有战利品组（如果没有指定groupId）
 */
void LootTemplate::Process(Loot& loot, bool rate, uint16 lootMode, uint8 groupId) const
{
    if (groupId)                                            // Group reference uses own processing of the group
    {
        if (groupId > Groups.size())
            return;                                         // Error message already printed at loading stage

        if (!Groups[groupId - 1])
            return;

        Groups[groupId - 1]->Process(loot, lootMode);
        return;
    }

    // Rolling non-grouped items
    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        LootStoreItem* item = *i;
        if (!(item->lootmode & lootMode))                       // Do not add if mode mismatch
            continue;

        if (!item->Roll(rate))
            continue;                                           // Bad luck for the entry

        if (item->reference > 0)                            // References processing
        {
            LootTemplate const* Referenced = LootTemplates_Reference.GetLootFor(item->reference);
            if (!Referenced)
                continue;                                       // Error message already printed at loading stage

            uint32 maxcount = uint32(float(item->maxcount) * sWorld->getRate(RATE_DROP_ITEM_REFERENCED_AMOUNT));
            for (uint32 loop = 0; loop < maxcount; ++loop)      // Ref multiplicator
                Referenced->Process(loot, rate, lootMode, item->groupid);
        }
        else                                                    // Plain entries (not a reference, not grouped)
            loot.AddItem(*item);                                // Chance is already checked, just add
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (LootGroup* group = *i)
            group->Process(loot, lootMode);
}

/**
 * @brief 检查模板中是否包含任务物品
 * @param store 战利品模板映射
 * @param groupId 组ID（如果指定，只检查该组）
 * @return true 如果模板中至少包含1个任务掉落条目
 */
bool LootTemplate::HasQuestDrop(LootTemplateMap const& store, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message [should be] already printed at loading stage

        if (!Groups[groupId - 1])
            return false;

        return Groups[groupId-1]->HasQuestDrop();
    }

    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        LootStoreItem* item = *i;
        if (item->reference > 0)                        // References
        {
            LootTemplateMap::const_iterator Referenced = store.find(item->reference);
            if (Referenced == store.end())
                continue;                                   // Error message [should be] already printed at loading stage
            if (Referenced->second->HasQuestDrop(store, item->groupid))
                return true;
        }
        else if (item->needs_quest)
            return true;                                    // quest drop found
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (LootGroup* group = *i)
            if (group->HasQuestDrop())
                return true;

    return false;
}

/**
 * @brief 检查模板中是否有玩家当前任务需要的掉落
 * @param store 战利品模板映射
 * @param player 玩家对象指针
 * @param groupId 组ID（如果指定，只检查该组）
 * @return true 如果模板中有玩家当前任务需要的物品
 */
bool LootTemplate::HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message already printed at loading stage

        if (!Groups[groupId - 1])
            return false;

        return Groups[groupId - 1]->HasQuestDropForPlayer(player);
    }

    // Checking non-grouped entries
    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        LootStoreItem* item = *i;
        if (item->reference > 0)                        // References processing
        {
            LootTemplateMap::const_iterator Referenced = store.find(item->reference);
            if (Referenced == store.end())
                continue;                                   // Error message already printed at loading stage
            if (Referenced->second->HasQuestDropForPlayer(store, player, item->groupid))
                return true;
        }
        else if (player->HasQuestForItem(item->itemid))
            return true;                                    // active quest drop found
    }

    // Now checking groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (LootGroup* group = *i)
            if (group->HasQuestDropForPlayer(player))
                return true;

    return false;
}

/**
 * @brief 验证模板配置的完整性
 * @param lootstore 战利品存储对象
 * @param id 模板ID
 *
 * 检查所有战利品组的概率配置是否有效
 */
void LootTemplate::Verify(LootStore const& lootstore, uint32 id) const
{
    // Checking group chances
    for (uint32 i = 0; i < Groups.size(); ++i)
        if (Groups[i])
            Groups[i]->Verify(lootstore, id, i + 1);

    /// @todo 待实现：引用有效性检查
}

/**
 * @brief 检查模板中的引用关系
 * @param store 战利品模板映射
 * @param ref_set 引用ID集合，用于标记已使用的引用ID
 *
 * 遍历所有条目和组，检查引用条目的有效性，并从ref_set中移除已使用的引用ID
 */
void LootTemplate::CheckLootRefs(LootTemplateMap const& store, LootIdSet* ref_set) const
{
    for (LootStoreItemList::const_iterator ieItr = Entries.begin(); ieItr != Entries.end(); ++ieItr)
    {
        LootStoreItem* item = *ieItr;
        if (item->reference > 0)
        {
            if (!LootTemplates_Reference.GetLootFor(item->reference))
                LootTemplates_Reference.ReportNonExistingId(item->reference, "Reference", item->itemid);
            else if (ref_set)
                ref_set->erase(item->reference);
        }
    }

    for (LootGroups::const_iterator grItr = Groups.begin(); grItr != Groups.end(); ++grItr)
        if (LootGroup* group = *grItr)
            group->CheckLootRefs(store, ref_set);
}

/**
 * @brief 向模板中的物品添加条件
 * @param cond 条件对象指针
 * @return true 如果成功添加条件，false 如果未找到对应物品
 *
 * 遍历模板中的所有条目（包括分组条目），找到匹配SourceEntry的物品并添加条件
 */
bool LootTemplate::addConditionItem(Condition* cond)
{
    if (!cond || !cond->isLoaded())//should never happen, checked at loading
    {
        TC_LOG_ERROR("loot", "LootTemplate::addConditionItem: condition is null");
        return false;
    }

    if (!Entries.empty())
    {
        for (LootStoreItemList::iterator i = Entries.begin(); i != Entries.end(); ++i)
        {
            if ((*i)->itemid == uint32(cond->SourceEntry))
            {
                (*i)->conditions.push_back(cond);
                return true;
            }
        }
    }

    if (!Groups.empty())
    {
        for (LootGroups::iterator groupItr = Groups.begin(); groupItr != Groups.end(); ++groupItr)
        {
            LootGroup* group = *groupItr;
            if (!group)
                continue;

            LootStoreItemList* itemList = group->GetExplicitlyChancedItemList();
            if (!itemList->empty())
            {
                for (LootStoreItemList::iterator i = itemList->begin(); i != itemList->end(); ++i)
                {
                    if ((*i)->itemid == uint32(cond->SourceEntry))
                    {
                        (*i)->conditions.push_back(cond);
                        return true;
                    }
                }
            }

            itemList = group->GetEqualChancedItemList();
            if (!itemList->empty())
            {
                for (LootStoreItemList::iterator i = itemList->begin(); i != itemList->end(); ++i)
                {
                    if ((*i)->itemid == uint32(cond->SourceEntry))
                    {
                        (*i)->conditions.push_back(cond);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * @brief 检查指定ID的物品是否为引用条目
 * @param id 物品ID
 * @return true 如果该物品是引用条目，false 如果未找到或不是引用
 */
bool LootTemplate::isReference(uint32 id)
{
    for (LootStoreItemList::const_iterator ieItr = Entries.begin(); ieItr != Entries.end(); ++ieItr)
        if ((*ieItr)->itemid == id && (*ieItr)->reference > 0)
            return true;

    return false;//not found or not reference
}

/**
 * @brief 加载生物掉落模板
 *
 * 从 creature_loot_template 表加载生物掉落数据
 * 并验证每个生物的掉落ID是否有效
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有生物模板，检查其掉落ID是否存在
 * 3. 报告未使用的掉落ID（配置了但未被任何生物使用）
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Creature()
{
    TC_LOG_INFO("server.loading", "Loading creature loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Creature.LoadAndCollectLootIds(lootIdSet);

    // Remove real entries and check loot existence
    CreatureTemplateContainer const& ctc = sObjectMgr->GetCreatureTemplates();
    for (auto const& creatureTemplatePair : ctc)
    {
        if (uint32 lootid = creatureTemplatePair.second.lootid)
        {
            if (!lootIdSet.count(lootid))
                LootTemplates_Creature.ReportNonExistingId(lootid, "Creature", creatureTemplatePair.first);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // 1 means loot for player corpse
    lootIdSet.erase(PLAYER_CORPSE_LOOT_ENTRY);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Creature.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} creature loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature loot templates. DB table `creature_loot_template` is empty");
}

/**
 * @brief 加载附魔分解掉落模板
 *
 * 从 disenchant_loot_template 表加载分解装备获得的物品数据
 * 并验证每个物品的分解ID是否有效
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有物品模板，检查有分解ID的物品是否配置了分解掉落
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Disenchant()
{
    TC_LOG_INFO("server.loading", "Loading disenchanting loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Disenchant.LoadAndCollectLootIds(lootIdSet);

    ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
    for (auto const& itemTemplatePair : its)
    {
        if (uint32 lootid = itemTemplatePair.second.DisenchantID)
        {
            if (!lootIdSet.count(lootid))
                LootTemplates_Disenchant.ReportNonExistingId(lootid, "Item", itemTemplatePair.first);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Disenchant.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} disenchanting loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 disenchanting loot templates. DB table `disenchant_loot_template` is empty");
}

/**
 * @brief 加载钓鱼掉落模板
 *
 * 从 fishing_loot_template 表加载钓鱼获得的物品数据
 * 掉落ID对应区域ID
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有区域表，移除有效的区域掉落ID
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Fishing()
{
    TC_LOG_INFO("server.loading", "Loading fishing loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Fishing.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (AreaTableEntry const* areaTable : sAreaTableStore)
        if (lootIdSet.find(areaTable->ID) != lootIdSet.end())
            lootIdSet.erase(areaTable->ID);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Fishing.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} fishing loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 fishing loot templates. DB table `fishing_loot_template` is empty");
}

/**
 * @brief 加载游戏对象掉落模板
 *
 * 从 gameobject_loot_template 表加载游戏对象（如宝箱、矿石等）掉落数据
 * 并验证每个游戏对象的掉落ID是否有效
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有游戏对象模板，检查其掉落ID是否存在
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Gameobject()
{
    TC_LOG_INFO("server.loading", "Loading gameobject loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Gameobject.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    GameObjectTemplateContainer const& gotc = sObjectMgr->GetGameObjectTemplates();
    for (auto const& gameObjectTemplatePair : gotc)
    {
        if (uint32 lootid = gameObjectTemplatePair.second.GetLootId())
        {
            if (!lootIdSet.count(lootid))
                LootTemplates_Gameobject.ReportNonExistingId(lootid, "Gameobject", gameObjectTemplatePair.first);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Gameobject.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} gameobject loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject loot templates. DB table `gameobject_loot_template` is empty");
}

/**
 * @brief 加载物品容器掉落模板
 *
 * 从 item_loot_template 表加载可打开物品容器（如礼品包、宝箱）内的物品数据
 * 掉落ID对应物品ID本身
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有物品模板，检查有ITEM_FLAG_HAS_LOOT标志的物品是否配置了掉落
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Item()
{
    TC_LOG_INFO("server.loading", "Loading item loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Item.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
    for (auto const& itemTemplatePair : its)
        if (lootIdSet.count(itemTemplatePair.first) > 0 && itemTemplatePair.second.HasFlag(ITEM_FLAG_HAS_LOOT))
            lootIdSet.erase(itemTemplatePair.first);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Item.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} item loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 item loot templates. DB table `item_loot_template` is empty");
}

/**
 * @brief 加载研磨掉落模板
 *
 * 从 milling_loot_template 表加载研磨草药获得的颜料数据
 * 掉落ID对应草药物品ID
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有物品模板，检查有ITEM_FLAG_IS_MILLABLE标志的物品是否配置了研磨掉落
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Milling()
{
    TC_LOG_INFO("server.loading", "Loading milling loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Milling.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
    for (auto const& itemTemplatePair : its)
    {
        if (!itemTemplatePair.second.HasFlag(ITEM_FLAG_IS_MILLABLE))
            continue;

        if (lootIdSet.count(itemTemplatePair.first) > 0)
            lootIdSet.erase(itemTemplatePair.first);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Milling.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} milling loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 milling loot templates. DB table `milling_loot_template` is empty");
}

/**
 * @brief 加载偷窃掉落模板
 *
 * 从 pickpocketing_loot_template 表加载潜行者偷窃生物获得的物品数据
 * 并验证每个生物的偷窃掉落ID是否有效
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有生物模板，检查其偷窃掉落ID是否存在
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Pickpocketing()
{
    TC_LOG_INFO("server.loading", "Loading pickpocketing loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Pickpocketing.LoadAndCollectLootIds(lootIdSet);

    // Remove real entries and check loot existence
    CreatureTemplateContainer const& ctc = sObjectMgr->GetCreatureTemplates();
    for (auto const& creatureTemplatePair : ctc)
    {
        if (uint32 lootid = creatureTemplatePair.second.pickpocketLootId)
        {
            if (!lootIdSet.count(lootid))
                LootTemplates_Pickpocketing.ReportNonExistingId(lootid, "Creature", creatureTemplatePair.first);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Pickpocketing.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} pickpocketing loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 pickpocketing loot templates. DB table `pickpocketing_loot_template` is empty");
}

/**
 * @brief 加载选矿掉落模板
 *
 * 从 prospecting_loot_template 表加载选矿矿石获得的宝石数据
 * 掉落ID对应矿石物品ID
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有物品模板，检查有ITEM_FLAG_IS_PROSPECTABLE标志的物品是否配置了选矿掉落
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Prospecting()
{
    TC_LOG_INFO("server.loading", "Loading prospecting loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Prospecting.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
    for (auto const& itemTemplatePair : its)
    {
        if (!itemTemplatePair.second.HasFlag(ITEM_FLAG_IS_PROSPECTABLE))
            continue;

        if (lootIdSet.count(itemTemplatePair.first) > 0)
            lootIdSet.erase(itemTemplatePair.first);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Prospecting.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} prospecting loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 prospecting loot templates. DB table `prospecting_loot_template` is empty");
}

/**
 * @brief 加载邮件掉落模板
 *
 * 从 mail_loot_template 表加载邮件附件中的物品数据
 * 掉落ID对应邮件模板ID
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有邮件模板，移除有效的邮件掉落ID
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Mail()
{
    TC_LOG_INFO("server.loading", "Loading mail loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Mail.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sMailTemplateStore.GetNumRows(); ++i)
        if (sMailTemplateStore.LookupEntry(i))
            if (lootIdSet.find(i) != lootIdSet.end())
                lootIdSet.erase(i);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Mail.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} mail loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 mail loot templates. DB table `mail_loot_template` is empty");
}

/**
 * @brief 加载剥皮掉落模板
 *
 * 从 skinning_loot_template 表加载剥皮生物获得的皮革、鳞片等材料数据
 * 并验证每个生物的剥皮掉落ID是否有效
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有生物模板，检查其剥皮掉落ID是否存在
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Skinning()
{
    TC_LOG_INFO("server.loading", "Loading skinning loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Skinning.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    CreatureTemplateContainer const& ctc = sObjectMgr->GetCreatureTemplates();
    for (auto const& creatureTemplatePair : ctc)
    {
        if (uint32 lootid = creatureTemplatePair.second.SkinLootId)
        {
            if (!lootIdSet.count(lootid))
                LootTemplates_Skinning.ReportNonExistingId(lootid, "Creature", creatureTemplatePair.first);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Skinning.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} skinning loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 skinning loot templates. DB table `skinning_loot_template` is empty");
}

/**
 * @brief 加载法术掉落模板
 *
 * 从 spell_loot_template 表加载施放法术获得的随机物品数据
 * 用于专业技能的随机物品生成（如附魔、珠宝加工）
 * 掉落ID对应法术ID
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 遍历所有法术，检查IsLootCrafting类型的法术是否配置了掉落
 * 3. 报告未使用的掉落ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Spell()
{
    TC_LOG_INFO("server.loading", "Loading spell loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Spell.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (uint32 spell_id = 1; spell_id < sSpellMgr->GetSpellInfoStoreSize(); ++spell_id)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell_id);
        if (!spellInfo)
            continue;

        // possible cases
        if (!spellInfo->IsLootCrafting())
            continue;

        if (lootIdSet.find(spell_id) == lootIdSet.end())
        {
            // not report about not trainable spells (optionally supported by DB)
            // ignore 61756 (Northrend Inscription Research (FAST QA VERSION) for example
            if (!spellInfo->HasAttribute(SPELL_ATTR0_NOT_SHAPESHIFT) || spellInfo->HasAttribute(SPELL_ATTR0_TRADESPELL))
                LootTemplates_Spell.ReportNonExistingId(spell_id, "Spell", spellInfo->Id);
        }
        else
            lootIdSet.erase(spell_id);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Spell.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded {} spell loot templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell loot templates. DB table `spell_loot_template` is empty");
}

/**
 * @brief 加载引用掉落模板
 *
 * 从 reference_loot_template 表加载可被其他掉落表引用的通用模板
 * 引用模板实现了掉落配置的复用，避免重复定义相同的掉落内容
 *
 * 主要流程：
 * 1. 加载掉落模板并收集所有掉落ID
 * 2. 检查所有其他掉落表中的引用关系
 * 3. 报告未被任何掉落表使用的引用ID
 * 4. 输出加载统计信息
 */
void LoadLootTemplates_Reference()
{
    TC_LOG_INFO("server.loading", "Loading reference loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    LootTemplates_Reference.LoadAndCollectLootIds(lootIdSet);

    // check references and remove used
    LootTemplates_Creature.CheckLootRefs(&lootIdSet);
    LootTemplates_Fishing.CheckLootRefs(&lootIdSet);
    LootTemplates_Gameobject.CheckLootRefs(&lootIdSet);
    LootTemplates_Item.CheckLootRefs(&lootIdSet);
    LootTemplates_Milling.CheckLootRefs(&lootIdSet);
    LootTemplates_Pickpocketing.CheckLootRefs(&lootIdSet);
    LootTemplates_Skinning.CheckLootRefs(&lootIdSet);
    LootTemplates_Disenchant.CheckLootRefs(&lootIdSet);
    LootTemplates_Prospecting.CheckLootRefs(&lootIdSet);
    LootTemplates_Mail.CheckLootRefs(&lootIdSet);
    LootTemplates_Reference.CheckLootRefs(&lootIdSet);

    // output error for any still listed ids (not referenced from any loot table)
    LootTemplates_Reference.ReportUnusedIds(lootIdSet);

    TC_LOG_INFO("server.loading", ">> Loaded reference loot templates in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载所有战利品表 - 服务器启动时调用
 *
 * 按顺序加载所有类型的战利品模板，引用模板最后加载以确保其他模板可以正确检查引用关系
 *
 * 加载顺序：
 * 1. creature_loot_template - 生物掉落表
 * 2. fishing_loot_template - 钓鱼掉落表
 * 3. gameobject_loot_template - 游戏对象掉落表
 * 4. item_loot_template - 物品容器掉落表
 * 5. mail_loot_template - 邮件掉落表
 * 6. milling_loot_template - 研磨掉落表
 * 7. pickpocketing_loot_template - 偷窃掉落表
 * 8. skinning_loot_template - 剥皮掉落表
 * 9. disenchant_loot_template - 附魔分解掉落表
 * 10. prospecting_loot_template - 选矿掉落表
 * 11. spell_loot_template - 法术掉落表
 * 12. reference_loot_template - 引用掉落表（最后加载）
 */
void LoadLootTables()
{
    LoadLootTemplates_Creature();
    LoadLootTemplates_Fishing();
    LoadLootTemplates_Gameobject();
    LoadLootTemplates_Item();
    LoadLootTemplates_Mail();
    LoadLootTemplates_Milling();
    LoadLootTemplates_Pickpocketing();
    LoadLootTemplates_Skinning();
    LoadLootTemplates_Disenchant();
    LoadLootTemplates_Prospecting();
    LoadLootTemplates_Spell();

    LoadLootTemplates_Reference();
}
