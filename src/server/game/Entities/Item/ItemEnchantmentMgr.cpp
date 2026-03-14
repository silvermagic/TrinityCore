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
 * @file ItemEnchantmentMgr.cpp
 * @brief 物品附魔管理器实现模块
 *
 * 本文件实现了物品随机附魔和随机属性的生成逻辑。
 *
 * 主要功能：
 *   - 加载和管理随机附魔模板数据
 *   - 根据权重随机选择附魔效果
 *   - 生成物品的随机属性ID
 *   - 计算随机后缀的属性因子值
 *
 * 核心数据结构：
 *   - EnchStoreItem: 存储单个附魔条目（附魔ID和概率）
 *   - EnchStoreList: 附魔条目列表
 *   - EnchantmentStore: 按条目ID索引的附魔存储表
 *
 * 随机附魔流程：
 *   1. 物品模板定义 RandomProperty 或 RandomSuffix 字段
 *   2. 根据该字段查询附魔模板表
 *   3. 按概率随机选择一个附魔效果
 *   4. 应用选中的附魔到物品实例
 */

#include "ItemEnchantmentMgr.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Util.h"
#include "DBCStores.h"
#include "Random.h"
#include "Timer.h"

#include <list>
#include <vector>
#include <stdlib.h>

/**
 * @brief 附魔存储条目结构体
 *
 * 存储单个附魔效果的定义，包括附魔ID和出现概率。
 */
struct EnchStoreItem
{
    uint32  ench;    ///< 附魔效果ID
    float   chance;  ///< 出现概率（百分比，如5.0表示5%）

    /**
     * @brief 默认构造函数
     */
    EnchStoreItem()
        : ench(0), chance(0) { }

    /**
     * @brief 参数构造函数
     * @param _ench 附魔效果ID
     * @param _chance 出现概率
     */
    EnchStoreItem(uint32 _ench, float _chance)
        : ench(_ench), chance(_chance) { }
};

typedef std::vector<EnchStoreItem> EnchStoreList;        ///< 附魔条目列表类型
typedef std::unordered_map<uint32, EnchStoreList> EnchantmentStore; ///< 附魔存储表类型（按条目ID索引）

static EnchantmentStore RandomItemEnch;  ///< 全局附魔存储表，在服务器启动时加载

/**
 * @brief 加载随机附魔模板表
 *
 * 从数据库 world.item_enchantment_template 表加载随机附魔定义数据。
 * 每个条目包含一个附魔效果ID和其出现概率。
 *
 * 数据库表结构：
 *   - entry: 随机属性条目ID（对应 item_template 的 RandomProperty 或 RandomSuffix）
 *   - ench: 附魔效果ID（SpellItemEnchantment.dbc）
 *   - chance: 出现概率（百分比，有效范围 0.000001 ~ 100.0）
 *
 * 性能注意事项：
 *   - 数据在服务器启动时一次性加载到内存
 *   - 使用 unordered_map 提供O(1)的查询效率
 *   - 重载时会清空旧数据重新加载
 */
void LoadRandomEnchantmentsTable()
{
    uint32 oldMSTime = getMSTime();

    RandomItemEnch.clear();                                 // for reload case

    //                                                 0      1      2
    QueryResult result = WorldDatabase.Query("SELECT entry, ench, chance FROM item_enchantment_template");

    if (result)
    {
        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 entry = fields[0].GetUInt32();   // 随机属性条目ID
            uint32 ench = fields[1].GetUInt32();    // 附魔效果ID
            float chance = fields[2].GetFloat();    // 出现概率

            // 只加载有效的概率值（排除无效数据）
            if (chance > 0.000001f && chance <= 100.0f)
                RandomItemEnch[entry].push_back(EnchStoreItem(ench, chance));

            ++count;
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} Item Enchantment definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 Item Enchantment definitions. DB table `item_enchantment_template` is empty.");
}

/**
 * @brief 根据条目ID获取随机附魔效果
 *
 * 从预加载的附魔表中根据权重随机选择一个附魔效果。
 * 使用加权随机算法，确保每个附魔按照其概率被选中。
 *
 * @param entry 随机属性条目ID（来自 item_enchantment_template 表）
 * @return 随机选择的附魔效果ID，如果条目无效则返回0
 *
 * 算法说明：
 *   1. 第一轮：生成0-100的随机数，累加概率直到超过随机数
 *   2. 如果第一轮未选中（概率之和<100%），进入第二轮
 *   3. 第二轮：在概率之和范围内重新随机选择
 *
 * 这种设计允许：
 *   - 概率之和<100%时，有一定几率不选择任何附魔
 *   - 概率之和=100%时，必定选择一个附魔
 *   - 概率之和>100%时，按比例选择
 *
 * @note 性能考虑：O(n)复杂度，n为该条目的附魔数量
 */
uint32 GetItemEnchantMod(int32 entry)
{
    // 无效条目ID
    if (!entry)
        return 0;

    // 特殊值-1表示无随机属性
    if (entry == -1)
        return 0;

    // 查找附魔条目
    EnchantmentStore::const_iterator tab = RandomItemEnch.find(entry);
    if (tab == RandomItemEnch.end())
    {
        TC_LOG_ERROR("sql.sql", "Item RandomProperty / RandomSuffix id #{} used in `item_template` but it does not have records in `item_enchantment_template` table.", entry);
        return 0;
    }

    // 第一轮随机：使用0-100范围的随机数
    double dRoll = rand_chance();  // 生成0.0-100.0的随机数
    float fCount = 0;

    for (EnchStoreList::const_iterator ench_iter = tab->second.begin(); ench_iter != tab->second.end(); ++ench_iter)
    {
        fCount += ench_iter->chance;

        if (fCount > dRoll)
            return ench_iter->ench;
    }

    //we could get here only if sum of all enchantment chances is lower than 100%
    // 第二轮随机：当所有附魔概率之和小于100%时，在概率和范围内重新选择
    dRoll = (irand(0, (int)floor(fCount * 100) + 1)) / 100;
    fCount = 0;

    for (EnchStoreList::const_iterator ench_iter = tab->second.begin(); ench_iter != tab->second.end(); ++ench_iter)
    {
        fCount += ench_iter->chance;

        if (fCount > dRoll)
            return ench_iter->ench;
    }

    // 无法选择任何附魔
    return 0;
}

/**
 * @brief 生成物品的随机属性ID
 *
 * 根据物品模板的 RandomProperty 或 RandomSuffix 字段，
 * 随机选择一个具体的属性ID返回。
 *
 * @param item_id 物品ID
 * @return 随机属性ID：
 *         - 正数：RandomProperty ID（固定属性组合）
 *         - 负数：RandomSuffix ID（动态计算属性）
 *         - 0：无随机属性或生成失败
 *
 * 工作流程：
 *   1. 获取物品模板
 *   2. 检查物品是否支持随机属性（RandomProperty 或 RandomSuffix 非空）
 *   3. 验证两个字段不能同时非空
 *   4. 根据 RandomProperty 或 RandomSuffix 字段随机选择属性ID
 *
 * RandomProperty vs RandomSuffix：
 *   - RandomProperty：返回正ID，属性在 ItemRandomProperties.dbc 中定义
 *   - RandomSuffix：返回负ID，属性在 ItemRandomSuffix.dbc 中定义
 *
 * @note 一个物品只能有 RandomProperty 或 RandomSuffix 之一，不能同时存在
 */
int32 GenerateItemRandomPropertyId(uint32 item_id)
{
    // 获取物品模板
    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item_id);

    if (!itemProto)
        return 0;

    // item must have one from this field values not null if it can have random enchantments
    // 物品必须至少有一个随机属性字段非空
    if ((!itemProto->RandomProperty) && (!itemProto->RandomSuffix))
        return 0;

    // item can have not null only one from field values
    // 物品不能同时设置两个随机属性字段
    if ((itemProto->RandomProperty) && (itemProto->RandomSuffix))
    {
        TC_LOG_ERROR("sql.sql", "Item template {} have RandomProperty == {} and RandomSuffix == {}, but must have one from field =0", itemProto->ItemId, itemProto->RandomProperty, itemProto->RandomSuffix);
        return 0;
    }

    // RandomProperty case
    // RandomProperty 情况：使用 ItemRandomProperties.dbc
    if (itemProto->RandomProperty)
    {
        // 根据权重随机选择一个属性ID
        uint32 randomPropId = GetItemEnchantMod(itemProto->RandomProperty);

        // 查找属性定义
        ItemRandomPropertiesEntry const* random_id = sItemRandomPropertiesStore.LookupEntry(randomPropId);
        if (!random_id)
        {
            TC_LOG_ERROR("sql.sql", "Enchantment id #{} used but it doesn't have records in 'ItemRandomProperties.dbc'", randomPropId);
            return 0;
        }

        // 返回正ID表示 RandomProperty
        return random_id->ID;
    }
    // RandomSuffix case
    // RandomSuffix 情况：使用 ItemRandomSuffix.dbc
    else
    {
        // 根据权重随机选择一个后缀ID
        uint32 randomPropId = GetItemEnchantMod(itemProto->RandomSuffix);

        // 查找后缀定义
        ItemRandomSuffixEntry const* random_id = sItemRandomSuffixStore.LookupEntry(randomPropId);
        if (!random_id)
        {
            TC_LOG_ERROR("sql.sql", "Enchantment id #{} used but it doesn't have records in sItemRandomSuffixStore.", randomPropId);
            return 0;
        }

        // 返回负ID表示 RandomSuffix（使用负数区分两种类型）
        return -int32(random_id->ID);
    }
}

/**
 * @brief 生成随机后缀因子
 *
 * 计算随机后缀物品的属性因子值，该因子决定了随机后缀提供的属性加成数值。
 * 因子值取决于物品等级、品质和装备槽位类型。
 *
 * @param item_id 物品ID
 * @return 随机后缀因子值，如果物品不支持随机后缀则返回0
 *
 * 计算依据：
 *   1. 物品等级 -> 从 RandPropPoints.dbc 获取基础属性值范围
 *   2. 装备槽位类型 -> 决定使用哪个属性系数（Good/Superior/Epic数组中的索引）
 *   3. 物品品质 -> 决定使用 Good（绿色）、Superior（蓝色）还是 Epic（紫色）系数
 *
 * 槽位因子映射：
 *   - 0: 头部、胸部、腿部、双手武器（最高属性值）
 *   - 1: 肩部、腰部、脚部、手部、饰品
 *   - 2: 颈部、手腕、手指、盾牌、披风、副手物品
 *   - 3: 单手武器、主手武器、副手武器
 *   - 4: 远程武器、投掷武器（最高属性值）
 *
 * @note 传说和神器品质的物品不支持随机后缀
 */
uint32 GenerateEnchSuffixFactor(uint32 item_id)
{
    // 获取物品模板
    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(item_id);

    if (!itemProto)
        return 0;

    // 只支持 RandomSuffix 类型的随机属性
    if (!itemProto->RandomSuffix)
        return 0;

    // 根据物品等级获取属性值范围定义
    RandPropPointsEntry const* randomProperty = sRandPropPointsStore.LookupEntry(itemProto->ItemLevel);
    if (!randomProperty)
        return 0;

    uint32 suffixFactor;

    // 根据装备槽位类型确定使用哪个属性系数索引
    switch (itemProto->InventoryType)
    {
        // Items of that type don`t have points
        // 这些槽位类型的物品不支持随机属性
        case INVTYPE_NON_EQUIP:  // 不可装备
        case INVTYPE_BAG:        // 背包
        case INVTYPE_TABARD:     // 战袍
        case INVTYPE_AMMO:       // 弹药
        case INVTYPE_QUIVER:     // 箭袋
        case INVTYPE_RELIC:      // 圣物
            return 0;

            // Select point coefficient
            // 根据槽位类型选择属性系数索引
        case INVTYPE_HEAD:       // 头部
        case INVTYPE_BODY:       // 衬衣
        case INVTYPE_CHEST:      // 胸部
        case INVTYPE_LEGS:       // 腿部
        case INVTYPE_2HWEAPON:   // 双手武器
        case INVTYPE_ROBE:       // 长袍
            suffixFactor = 0;    // 使用第一个系数（最高值）
            break;

        case INVTYPE_SHOULDERS:  // 肩部
        case INVTYPE_WAIST:      // 腰部
        case INVTYPE_FEET:       // 脚部
        case INVTYPE_HANDS:      // 手部
        case INVTYPE_TRINKET:    // 饰品
            suffixFactor = 1;    // 使用第二个系数
            break;

        case INVTYPE_NECK:       // 颈部
        case INVTYPE_WRISTS:     // 手腕
        case INVTYPE_FINGER:     // 手指
        case INVTYPE_SHIELD:     // 盾牌
        case INVTYPE_CLOAK:      // 披风
        case INVTYPE_HOLDABLE:   // 副手物品
            suffixFactor = 2;    // 使用第三个系数
            break;

        case INVTYPE_WEAPON:         // 单手武器
        case INVTYPE_WEAPONMAINHAND: // 主手武器
        case INVTYPE_WEAPONOFFHAND:  // 副手武器
            suffixFactor = 3;        // 使用第四个系数
            break;

        case INVTYPE_RANGED:      // 远程武器
        case INVTYPE_THROWN:      // 投掷武器
        case INVTYPE_RANGEDRIGHT: // 右手远程武器
            suffixFactor = 4;     // 使用第五个系数
            break;

        default:
            return 0;
    }

    // Select rare/epic modifier
    // 根据物品品质选择属性值来源
    switch (itemProto->Quality)
    {
        case ITEM_QUALITY_UNCOMMON:  // 绿色（优秀）
            return randomProperty->Good[suffixFactor];      // 使用Good数组

        case ITEM_QUALITY_RARE:      // 蓝色（精良）
            return randomProperty->Superior[suffixFactor];  // 使用Superior数组

        case ITEM_QUALITY_EPIC:      // 紫色（史诗）
            return randomProperty->Epic[suffixFactor];      // 使用Epic数组

        case ITEM_QUALITY_LEGENDARY: // 橙色（传说）
        case ITEM_QUALITY_ARTIFACT:  // 神器
            return 0;                                       // not have random properties
                                                            // 传说和神器不支持随机属性

        default:
            break;
    }
    return 0;
}
