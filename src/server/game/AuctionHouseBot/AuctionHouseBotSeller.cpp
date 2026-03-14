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
 * @file AuctionHouseBotSeller.cpp
 * @brief 拍卖行机器人销售者功能实现
 *
 * 本文件实现了拍卖行机器人的销售功能,负责自动向拍卖行投放物品。
 * 主要功能包括:
 * - 物品池的初始化和过滤
 * - 根据配置自动上架物品
 * - 价格计算和定价策略
 * - 拍卖行库存管理和补货
 */

#include "AuctionHouseBotSeller.h"
#include "AuctionHouseMgr.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Random.h"
#include <sstream>

/**
 * @brief 拍卖行机器人销售者构造函数
 *
 * 初始化所有拍卖行类型的配置对象。
 * 为联盟、部落和中立拍卖行分别创建 SellerConfiguration 实例。
 */
AuctionBotSeller::AuctionBotSeller()
{
    // 定义主要数据类的阵营
    for (uint8 i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        _houseConfig[i].Initialize(AuctionHouseType(i));
}

/**
 * @brief 拍卖行机器人销售者析构函数
 */
AuctionBotSeller::~AuctionBotSeller()
{
}

/**
 * @brief 初始化拍卖行机器人销售者
 *
 * 该方法执行以下初始化步骤:
 * 1. 加载强制包含和排除的物品列表
 * 2. 加载NPC商人出售的物品列表(用于过滤)
 * 3. 加载战利品表中的物品列表(用于过滤)
 * 4. 根据配置项过滤并构建物品池
 * 5. 加载销售者配置
 *
 * @return true 如果初始化成功且有可用物品
 * @return false 如果没有可用物品,将禁用拍卖行机器人
 */
bool AuctionBotSeller::Initialize()
{
    // NPC商人出售的物品集合
    std::unordered_set<uint32> npcItems;
    // 战利品物品集合
    std::unordered_set<uint32> lootItems;
    // 强制包含的物品集合
    std::unordered_set<uint32> includeItems;
    // 强制排除的物品集合
    std::unordered_set<uint32> excludeItems;

    TC_LOG_DEBUG("ahbot", "AHBot seller filters:");

    // 解析强制包含物品列表(逗号分隔的物品ID字符串)
    {
        std::stringstream includeStream(sAuctionBotConfig->GetAHBotIncludes());
        std::string temp;
        while (std::getline(includeStream, temp, ','))
            includeItems.insert(atoi(temp.c_str()));
    }

    // 解析强制排除物品列表(逗号分隔的物品ID字符串)
    {
        std::stringstream excludeStream(sAuctionBotConfig->GetAHBotExcludes());
        std::string temp;
        while (std::getline(excludeStream, temp, ','))
            excludeItems.insert(atoi(temp.c_str()));
    }

    TC_LOG_DEBUG("ahbot", "Forced Inclusion {} items", (uint32)includeItems.size());
    TC_LOG_DEBUG("ahbot", "Forced Exclusion {} items", (uint32)excludeItems.size());

    // 加载NPC商人物品用于过滤器
    TC_LOG_DEBUG("ahbot", "Loading npc vendor items for filter..");
    CreatureTemplateContainer const& creatures = sObjectMgr->GetCreatureTemplates();
    for (auto const& creatureTemplatePair : creatures)
        if (VendorItemData const* data = sObjectMgr->GetNpcVendorItemList(creatureTemplatePair.first))
            for (VendorItem const& vendorItem : data->m_items)
                npcItems.insert(vendorItem.item);

    TC_LOG_DEBUG("ahbot", "Npc vendor filter has {} items", (uint32)npcItems.size());

    // 加载所有战利品表中的物品用于过滤器
    // 从多个战利品表中查询所有非引用物品
    TC_LOG_DEBUG("ahbot", "Loading loot items for filter..");
    QueryResult result = WorldDatabase.PQuery(
        "SELECT `item` FROM `creature_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `disenchant_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `fishing_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `gameobject_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `item_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `milling_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `pickpocketing_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `prospecting_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `reference_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `skinning_loot_template` WHERE `Reference` = 0 UNION "
        "SELECT `item` FROM `spell_loot_template` WHERE `Reference` = 0");

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 entry = fields[0].GetUInt32();
            if (!entry)
                continue;

            lootItems.insert(entry);
        } while (result->NextRow());
    }

    TC_LOG_DEBUG("ahbot", "Loot filter has {} items", (uint32)lootItems.size());
    TC_LOG_DEBUG("ahbot", "Sorting and cleaning items for AHBot seller...");

    uint32 itemsAdded = 0;

    // 遍历所有物品模板,构建拍卖行物品池
    for (uint32 itemId = 0; itemId < sItemStore.GetNumRows(); ++itemId)
    {
        ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(itemId);
        if (!prototype)
            continue;

        // 跳过品质过高的物品(代码无法正确处理)
        if (prototype->Quality >= MAX_AUCTION_QUALITY)
            continue;

        // 强制排除过滤器检查
        if (excludeItems.count(itemId))
            continue;

        // 强制包含过滤器检查 - 直接添加到物品池,跳过其他过滤
        if (includeItems.count(itemId))
        {
            _itemPool[prototype->Quality][prototype->Class].push_back(itemId);
            ++itemsAdded;
            continue;
        }

        // 绑定类型过滤器检查
        switch (prototype->Bonding)
        {
            case NO_BIND:
                if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIND_NO))
                    continue;
                break;
            case BIND_WHEN_PICKED_UP:
                if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIND_PICKUP))
                    continue;
                break;
            case BIND_WHEN_EQUIPED:
                if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIND_EQUIP))
                    continue;
                break;
            case BIND_WHEN_USE:
                if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIND_USE))
                    continue;
                break;
            case BIND_QUEST_ITEM:
                if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIND_QUEST))
                    continue;
                break;
            default:
                continue;
        }

        // 根据物品类别检查是否允许零价格物品
        bool allowZero = false;
        switch (prototype->Class)
        {
            case ITEM_CLASS_CONSUMABLE:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONSUMABLE_ALLOW_ZERO); break;
            case ITEM_CLASS_CONTAINER:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONTAINER_ALLOW_ZERO); break;
            case ITEM_CLASS_WEAPON:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_WEAPON_ALLOW_ZERO); break;
            case ITEM_CLASS_GEM:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GEM_ALLOW_ZERO); break;
            case ITEM_CLASS_ARMOR:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_ARMOR_ALLOW_ZERO); break;
            case ITEM_CLASS_REAGENT:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_REAGENT_ALLOW_ZERO); break;
            case ITEM_CLASS_PROJECTILE:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_PROJECTILE_ALLOW_ZERO); break;
            case ITEM_CLASS_TRADE_GOODS:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_ALLOW_ZERO); break;
            case ITEM_CLASS_RECIPE:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RECIPE_ALLOW_ZERO); break;
            case ITEM_CLASS_QUIVER:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_QUIVER_ALLOW_ZERO); break;
            case ITEM_CLASS_QUEST:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_QUEST_ALLOW_ZERO); break;
            case ITEM_CLASS_KEY:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_KEY_ALLOW_ZERO); break;
            case ITEM_CLASS_MISC:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_ALLOW_ZERO); break;
            case ITEM_CLASS_GLYPH:
                allowZero = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_ALLOW_ZERO); break;
            default:
                allowZero = false;
        }

        // 过滤掉没有买价/卖价的物品,除非配置中明确允许
        if (!allowZero)
        {
            if (sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYPRICE_SELLER))
            {
                if (prototype->SellPrice == 0)
                    continue;
            }
            else
            {
                if (prototype->BuyPrice == 0)
                    continue;
            }
        }

        // NPC商人物品过滤器
        if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEMS_VENDOR))
        {
            if (npcItems.count(itemId))
                continue;
        }

        // 战利品物品过滤器
        if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEMS_LOOT))
        {
            if (lootItems.count(itemId))
                continue;
        }

        // 非商人/非战利品物品过滤器
        // 如果启用,只添加来自商人或战利品的物品
        if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEMS_MISC))
        {
            bool const isVendorItem = npcItems.count(itemId) > 0;
            bool const isLootItem = lootItems.count(itemId) > 0;

            if (!isLootItem && !isVendorItem)
                continue;
        }

        // 物品类别/子类别特定过滤器
        // 根据不同的物品类别应用不同的过滤规则
        switch (prototype->Class)
        {
            case ITEM_CLASS_ARMOR:
            case ITEM_CLASS_WEAPON:
            {
                // 护甲和武器:检查物品等级、需求等级和技能等级范围
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MIN_ITEM_LEVEL))
                    if (prototype->ItemLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MAX_ITEM_LEVEL))
                    if (prototype->ItemLevel > value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MIN_REQ_LEVEL))
                    if (prototype->RequiredLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MAX_REQ_LEVEL))
                    if (prototype->RequiredLevel > value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MIN_SKILL_RANK))
                    if (prototype->RequiredSkillRank < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MAX_SKILL_RANK))
                    if (prototype->RequiredSkillRank > value)
                        continue;
                break;
            }
            case ITEM_CLASS_RECIPE:
            case ITEM_CLASS_CONSUMABLE:
            case ITEM_CLASS_PROJECTILE:
            {
                // 配方、消耗品和弹药:检查需求等级和技能等级范围
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MIN_REQ_LEVEL))
                    if (prototype->RequiredLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MAX_REQ_LEVEL))
                    if (prototype->RequiredLevel > value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MIN_SKILL_RANK))
                    if (prototype->RequiredSkillRank < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_ITEM_MAX_SKILL_RANK))
                    if (prototype->RequiredSkillRank > value)
                        continue;
                break;
            }
            case ITEM_CLASS_MISC:
                // 坐骑的特殊过滤
                if (prototype->SubClass == ITEM_SUBCLASS_JUNK_MOUNT)
                {
                    if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_REQ_LEVEL))
                        if (prototype->RequiredLevel < value)
                            continue;
                    if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_REQ_LEVEL))
                        if (prototype->RequiredLevel > value)
                            continue;
                    if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_SKILL_RANK))
                        if (prototype->RequiredSkillRank < value)
                            continue;
                    if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_SKILL_RANK))
                        if (prototype->RequiredSkillRank > value)
                            continue;
                }

                // 可战利品物品(如锁箱)的特殊处理
                if (prototype->HasFlag(ITEM_FLAG_HAS_LOOT))
                {
                    // 跳过任何未锁定的可战利品物品(主要是任务特定或奖励情况)
                    if (!prototype->LockID)
                        continue;

                    if (!sAuctionBotConfig->GetConfig(CONFIG_AHBOT_LOCKBOX_ENABLED))
                        continue;
                }

                break;
            case ITEM_CLASS_GLYPH:
            {
                // 雕文:检查需求等级和物品等级范围
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_MIN_REQ_LEVEL))
                    if (prototype->RequiredLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_MAX_REQ_LEVEL))
                    if (prototype->RequiredLevel > value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_MIN_ITEM_LEVEL))
                    if (prototype->RequiredLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_MAX_ITEM_LEVEL))
                    if (prototype->RequiredLevel > value)
                        continue;
                break;
            }
            case ITEM_CLASS_TRADE_GOODS:
            {
                // 交易商品:检查物品等级范围
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_MIN_ITEM_LEVEL))
                    if (prototype->ItemLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_MAX_ITEM_LEVEL))
                    if (prototype->ItemLevel > value)
                        continue;
                break;
            }
            case ITEM_CLASS_CONTAINER:
            case ITEM_CLASS_QUIVER:
            {
                // 容器和箭袋:检查物品等级范围
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONTAINER_MIN_ITEM_LEVEL))
                    if (prototype->ItemLevel < value)
                        continue;
                if (uint32 value = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONTAINER_MAX_ITEM_LEVEL))
                    if (prototype->ItemLevel > value)
                        continue;
                break;
            }
        }

        // 通过所有过滤器,将物品添加到对应品质和类别的物品池中
        _itemPool[prototype->Quality][prototype->Class].push_back(itemId);
        ++itemsAdded;
    }

    // 如果没有添加任何物品,禁用拍卖行机器人
    if (!itemsAdded)
    {
        TC_LOG_ERROR("ahbot", "AuctionHouseBot seller not have items, disabled.");
        sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO, 0);
        sAuctionBotConfig->SetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO, 0);
        sAuctionBotConfig->SetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO, 0);
        return false;
    }

    TC_LOG_DEBUG("ahbot", "AuctionHouseBot seller will use {} items to fill auction house (according your config choices)", itemsAdded);

    LoadConfig();

    // 输出各品质和类别的物品数量统计
    TC_LOG_DEBUG("ahbot", "Items loaded \tGray\tWhite\tGreen\tBlue\tPurple\tOrange\tYellow");
    for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        TC_LOG_DEBUG("ahbot", "\t\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
        (uint32)_itemPool[0][i].size(), (uint32)_itemPool[1][i].size(), (uint32)_itemPool[2][i].size(),
        (uint32)_itemPool[3][i].size(), (uint32)_itemPool[4][i].size(), (uint32)_itemPool[5][i].size(),
        (uint32)_itemPool[6][i].size());

    TC_LOG_DEBUG("ahbot", "AHBot seller configuration data loaded and initialized");
    return true;
}

/**
 * @brief 加载拍卖行机器人销售者配置
 *
 * 为每个激活的拍卖行类型加载销售者配置值。
 */
void AuctionBotSeller::LoadConfig()
{
    for (uint8 i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        if (sAuctionBotConfig->GetConfigItemAmountRatio(AuctionHouseType(i)))
            LoadSellerValues(_houseConfig[i]);
}

/**
 * @brief 加载物品数量配置
 *
 * 根据配置计算各拍卖行应该保持的物品数量。
 * 基于配置的比例值计算各品质和类别的物品数量。
 *
 * @param config 销售者配置对象的引用
 */
void AuctionBotSeller::LoadItemsQuantity(SellerConfiguration& config)
{
    uint32 ratio = sAuctionBotConfig->GetConfigItemAmountRatio(config.GetHouseType());

    // 计算各品质的物品数量(基于比例)
    for (uint32 i = 0; i < MAX_AUCTION_QUALITY; ++i)
    {
        uint32 amount = sAuctionBotConfig->GetConfig(AuctionBotConfigUInt32Values(CONFIG_AHBOT_ITEM_GRAY_AMOUNT + i));
        config.SetItemsAmountPerQuality(AuctionQuality(i), std::lroundf(amount * ratio / 100.f));
    }

    // 设置各类别的随机堆叠比例
    // 这些比例决定了物品是单个上架还是堆叠上架
    config.SetRandomStackRatioPerClass(ITEM_CLASS_CONSUMABLE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONSUMABLE));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_CONTAINER, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONTAINER));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_WEAPON, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_WEAPON));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_GEM, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GEM));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_ARMOR, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_ARMOR));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_REAGENT, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_REAGENT));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_PROJECTILE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_PROJECTILE));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_TRADE_GOODS, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_TRADEGOOD));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_GENERIC, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GENERIC));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_RECIPE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_RECIPE));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_QUIVER, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUIVER));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_QUEST, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUEST));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_KEY, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_KEY));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_MISC, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_MISC));
    config.SetRandomStackRatioPerClass(ITEM_CLASS_GLYPH, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GLYPH));

    // 设置最优值以获取最接近期望的物品数量
    // Lambda函数:获取指定物品类别的优先级
    auto getPriorityForClass = [](uint32 itemClass) -> uint32
    {
        AuctionBotConfigUInt32Values index;
        switch (itemClass)
        {
            case ITEM_CLASS_CONSUMABLE:
                index = CONFIG_AHBOT_CLASS_CONSUMABLE_PRIORITY; break;
            case ITEM_CLASS_CONTAINER:
                index = CONFIG_AHBOT_CLASS_CONTAINER_PRIORITY; break;
            case ITEM_CLASS_WEAPON:
                index = CONFIG_AHBOT_CLASS_WEAPON_PRIORITY; break;
            case ITEM_CLASS_GEM:
                index = CONFIG_AHBOT_CLASS_GEM_PRIORITY; break;
            case ITEM_CLASS_ARMOR:
                index = CONFIG_AHBOT_CLASS_ARMOR_PRIORITY; break;
            case ITEM_CLASS_REAGENT:
                index = CONFIG_AHBOT_CLASS_REAGENT_PRIORITY; break;
            case ITEM_CLASS_PROJECTILE:
                index = CONFIG_AHBOT_CLASS_PROJECTILE_PRIORITY; break;
            case ITEM_CLASS_TRADE_GOODS:
                index = CONFIG_AHBOT_CLASS_TRADEGOOD_PRIORITY; break;
            case ITEM_CLASS_GENERIC:
                index = CONFIG_AHBOT_CLASS_GENERIC_PRIORITY; break;
            case ITEM_CLASS_RECIPE:
                index = CONFIG_AHBOT_CLASS_RECIPE_PRIORITY; break;
            case ITEM_CLASS_QUIVER:
                index = CONFIG_AHBOT_CLASS_QUIVER_PRIORITY; break;
            case ITEM_CLASS_QUEST:
                index = CONFIG_AHBOT_CLASS_QUEST_PRIORITY; break;
            case ITEM_CLASS_KEY:
                index = CONFIG_AHBOT_CLASS_KEY_PRIORITY; break;
            case ITEM_CLASS_MISC:
                index = CONFIG_AHBOT_CLASS_MISC_PRIORITY; break;
            case ITEM_CLASS_GLYPH:
                index = CONFIG_AHBOT_CLASS_GLYPH_PRIORITY; break;
            default:
                return 0;
        }

        return sAuctionBotConfig->GetConfig(index);
    };

    // 计算每个品质的总优先级
    std::vector<uint32> totalPrioPerQuality(MAX_AUCTION_QUALITY);
    for (uint32 j = 0; j < MAX_AUCTION_QUALITY; ++j)
    {
        for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        {
            // 跳过空的物品池
            if (_itemPool[j][i].empty())
                continue;

            totalPrioPerQuality[j] += getPriorityForClass(i);
        }
    }

    // 根据优先级权重分配各品质中各类别的物品数量
    for (uint32 j = 0; j < MAX_AUCTION_QUALITY; ++j)
    {
        uint32 qualityAmount = config.GetItemsAmountPerQuality(AuctionQuality(j));
        if (!totalPrioPerQuality[j])
            continue;

        for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        {
            uint32 classPrio = getPriorityForClass(i);
            if (_itemPool[j][i].empty())
                classPrio = 0;

            // 按权重计算该类别的物品数量
            uint32 weightedAmount = std::lroundf(classPrio / float(totalPrioPerQuality[j]) * qualityAmount);
            config.SetItemsAmountPerClass(AuctionQuality(j), ItemClass(i), weightedAmount);
        }
    }

    // 断言检查:如果选定的物品池为空,GetItemAmount必须返回0
    for (uint32 j = 0; j < MAX_AUCTION_QUALITY; ++j)
    {
        for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        {
            if (_itemPool[j][i].empty())
                ASSERT(config.GetItemsAmountPerClass(AuctionQuality(j), ItemClass(i)) == 0);
        }
    }
}

/**
 * @brief 加载销售者配置值
 *
 * 加载销售者的完整配置,包括物品数量和价格比例。
 *
 * @param config 销售者配置对象的引用
 */
void AuctionBotSeller::LoadSellerValues(SellerConfiguration& config)
{
    LoadItemsQuantity(config);
    uint32 ratio = sAuctionBotConfig->GetConfigPriceRatio(config.GetHouseType());

    // 设置各品质的价格比例
    for (uint32 i = 0; i < MAX_AUCTION_QUALITY; ++i)
    {
        uint32 amount = sAuctionBotConfig->GetConfig(AuctionBotConfigUInt32Values(CONFIG_AHBOT_ITEM_GRAY_PRICE_RATIO + i));
        config.SetPriceRatioPerQuality(AuctionQuality(i), std::lroundf(amount * ratio / 100.f));
    }

    // 设置各类别的价格比例
    config.SetPriceRatioPerClass(ITEM_CLASS_CONSUMABLE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONSUMABLE_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_CONTAINER, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_CONTAINER_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_WEAPON, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_WEAPON_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_GEM, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GEM_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_ARMOR, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_ARMOR_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_REAGENT, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_REAGENT_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_PROJECTILE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_PROJECTILE_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_TRADE_GOODS, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_GENERIC, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GENERIC_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_RECIPE, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_RECIPE_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_MONEY, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MONEY_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_QUIVER, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_QUIVER_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_QUEST, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_QUEST_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_KEY, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_KEY_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_PERMANENT, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_PERMANENT_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_MISC, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_MISC_PRICE_RATIO));
    config.SetPriceRatioPerClass(ITEM_CLASS_GLYPH, sAuctionBotConfig->GetConfig(CONFIG_AHBOT_CLASS_GLYPH_PRICE_RATIO));

    // 加载最小和最大拍卖时间
    config.SetMinTime(sAuctionBotConfig->GetConfig(CONFIG_AHBOT_MINTIME));
    config.SetMaxTime(sAuctionBotConfig->GetConfig(CONFIG_AHBOT_MAXTIME));
}

/**
 * @brief 设置拍卖行物品统计数据
 *
 * 统计当前拍卖行中的物品数量,计算需要补充的物品数量。
 * 只统计属于拍卖行机器人的物品。
 *
 * @param config 销售者配置对象的引用
 * @return uint32 需要补充的物品总数
 */
uint32 AuctionBotSeller::SetStat(SellerConfiguration& config)
{
    AllItemsArray itemsSaved(MAX_AUCTION_QUALITY, std::vector<uint32>(MAX_ITEM_CLASS));

    // 遍历拍卖行中的所有拍卖项
    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config.GetHouseType());
    for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = auctionHouse->GetAuctionsBegin(); itr != auctionHouse->GetAuctionsEnd(); ++itr)
    {
        AuctionEntry* auctionEntry = itr->second;
        Item* item = sAuctionMgr->GetAItem(auctionEntry->itemGUIDLow);
        if (item)
        {
            ItemTemplate const* prototype = item->GetTemplate();
            if (prototype)
                // 只添加拍卖行机器人的物品
                if (!auctionEntry->owner || sAuctionBotConfig->IsBotChar(auctionEntry->owner))
                    ++itemsSaved[prototype->Quality][prototype->Class];
        }
    }

    // 计算各品质和类别需要补充的物品数量
    uint32 count = 0;
    for (uint32 j = 0; j < MAX_AUCTION_QUALITY; ++j)
    {
        for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        {
            config.SetMissedItemsPerClass((AuctionQuality)j, (ItemClass)i, itemsSaved[j][i]);
            count += config.GetMissedItemsPerClass((AuctionQuality)j, (ItemClass)i);
        }
    }

    // 输出调试信息
    TC_LOG_DEBUG("ahbot", "AHBot: Missed Item       \tGray\tWhite\tGreen\tBlue\tPurple\tOrange\tYellow");
    for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
    {
        TC_LOG_DEBUG("ahbot", "AHBot: \t\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
            config.GetMissedItemsPerClass(AUCTION_QUALITY_GRAY, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_WHITE, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_GREEN, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_BLUE, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_PURPLE, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_ORANGE, (ItemClass)i),
            config.GetMissedItemsPerClass(AUCTION_QUALITY_YELLOW, (ItemClass)i));
    }
    config.LastMissedItem = count;

    return count;
}

/**
 * @brief 获取需要销售的物品列表
 *
 * 根据拍卖行当前库存和配置,确定哪些物品类别需要补充。
 * 该方法用于创建一个可随机选择的物品类别列表。
 *
 * @param config 销售者配置对象的引用
 * @param itemsToSellArray 输出参数,需要销售的物品类别列表
 * @param addedItem 已添加物品的统计数组
 * @return true 如果有需要补充的物品
 * @return false 如果拍卖行已满
 */
bool AuctionBotSeller::GetItemsToSell(SellerConfiguration& config, ItemsToSellArray& itemsToSellArray, AllItemsArray const& addedItem)
{
    itemsToSellArray.clear();
    bool found = false;

    for (uint32 j = 0; j < MAX_AUCTION_QUALITY; ++j)
    {
        for (uint32 i = 0; i < MAX_ITEM_CLASS; ++i)
        {
            // 如果选定类别的物品池为空,MissedItemsPerClass将返回0(在启动时检查)
            if (config.GetMissedItemsPerClass(AuctionQuality(j), ItemClass(i)) > addedItem[j][i])
            {
                ItemToSell miss_item;
                miss_item.Color = j;
                miss_item.Itemclass = i;
                itemsToSellArray.emplace_back(std::move(miss_item));
                found = true;
            }
        }
    }

    return found;
}

/**
 * @brief 设置物品价格
 *
 * 根据物品模板和配置计算拍卖行的买断价和竞价。
 * 价格计算考虑了:
 * - 物品的类别和品质价格比例
 * - 物品的买价/卖价
 * - 堆叠数量
 * - 配置中的价格比率
 *
 * @param itemProto 物品模板指针
 * @param config 销售者配置对象的引用
 * @param buyp 输出参数,买断价格
 * @param bidp 输出参数,竞拍价格
 * @param stackCount 堆叠数量
 */
void AuctionBotSeller::SetPricesOfItem(ItemTemplate const* itemProto, SellerConfiguration& config, uint32& buyp, uint32& bidp, uint32 stackCount)
{
    uint32 classRatio = config.GetPriceRatioPerClass(ItemClass(itemProto->Class));
    uint32 qualityRatio = config.GetPriceRatioPerQuality(AuctionQuality(itemProto->Quality));
    float priceRatio = (classRatio * qualityRatio) / 10000.0f;

    float buyPrice = itemProto->BuyPrice;
    float sellPrice = itemProto->SellPrice;

    // 如果没有买价,尝试从卖价计算
    if (buyPrice == 0)
    {
        if (sellPrice > 0)
            buyPrice = sellPrice * GetSellModifier(itemProto);
        else
        {
            // 如果既没有买价也没有卖价,基于物品等级和品质计算
            float divisor = ((itemProto->Class == ITEM_CLASS_WEAPON || itemProto->Class == ITEM_CLASS_ARMOR) ? 284.0f : 80.0f);
            float tempLevel = (itemProto->ItemLevel == 0 ? 1.0f : itemProto->ItemLevel);
            float tempQuality = (itemProto->Quality == 0 ? 1.0f : itemProto->Quality);

            buyPrice = tempLevel * tempQuality * static_cast<float>(GetBuyModifier(itemProto))* tempLevel / divisor;
        }
    }

    // 如果没有卖价,从买价计算
    if (sellPrice == 0)
        sellPrice = (buyPrice > 10 ? buyPrice / GetSellModifier(itemProto) : buyPrice);

    // 根据配置决定使用买价还是卖价作为基础
    if (sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYPRICE_SELLER))
        buyPrice = sellPrice;

    // 计算基础价格并应用价格比率
    float basePriceFloat = buyPrice * stackCount / (itemProto->Class == 6 ? 200.0f : static_cast<float>(itemProto->BuyCount));
    basePriceFloat *= priceRatio;

    // 添加随机浮动(±4%)
    float range = basePriceFloat * 0.04f;

    buyp = static_cast<uint32>(frand(basePriceFloat - range, basePriceFloat + range) + 0.5f);
    if (buyp == 0)
        buyp = 1;

    // 计算竞拍价格(买断价的一定比例)
    float bidPercentage = frand(sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIDPRICE_MIN), sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BIDPRICE_MAX));
    bidp = static_cast<uint32>(bidPercentage * buyp);
    if (bidp == 0)
        bidp = 1;
}

/**
 * @brief 获取物品的堆叠大小
 *
 * 根据配置决定物品是单个上架还是以随机堆叠数量上架。
 *
 * @param itemProto 物品模板指针
 * @param config 销售者配置对象的引用
 * @return uint32 堆叠大小(1或随机值)
 */
uint32 AuctionBotSeller::GetStackSizeForItem(ItemTemplate const* itemProto, SellerConfiguration& config) const
{
    // 根据配置的随机堆叠比例决定是否使用随机堆叠
    if (config.GetRandomStackRatioPerClass(ItemClass(itemProto->Class)) > urand(0, 99))
        return urand(1, itemProto->GetMaxStackSize());
    else
        return 1;
}

/**
 * @brief 获取卖价修正系数
 *
 * 用于从卖价推算买价,或从买价推算卖价。
 * 不同类别的物品有不同的修正系数。
 *
 * @param prototype 物品模板指针
 * @return uint32 卖价修正系数
 */
uint32 AuctionBotSeller::GetSellModifier(ItemTemplate const* prototype)
{
    switch (prototype->Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
        case ITEM_CLASS_REAGENT:
        case ITEM_CLASS_PROJECTILE:
            return 5;
        default:
            return 4;
    }
}

/**
 * @brief 获取买价修正系数
 *
 * 用于计算物品的基础价格。不同类别和子类别的物品有不同的修正系数。
 * 该系数与物品等级和品质一起使用,以计算相对准确的价格。
 *
 * @param prototype 物品模板指针
 * @return uint32 买价修正系数
 */
uint32 AuctionBotSeller::GetBuyModifier(ItemTemplate const* prototype)
{
    switch (prototype->Class)
    {
        case ITEM_CLASS_CONSUMABLE:
        {
            switch (prototype->SubClass)
            {
            case ITEM_SUBCLASS_CONSUMABLE:
                return 100;
            case ITEM_SUBCLASS_FLASK:
                return 400;
            case ITEM_SUBCLASS_SCROLL:
                return 15;
            case ITEM_SUBCLASS_ITEM_ENHANCEMENT:
                return 250;
            case ITEM_SUBCLASS_BANDAGE:
                return 125;
            default:
                return 300;
            }
        }
        case ITEM_CLASS_WEAPON:
        {
            switch (prototype->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_AXE:
                case ITEM_SUBCLASS_WEAPON_MACE:
                case ITEM_SUBCLASS_WEAPON_SWORD:
                case ITEM_SUBCLASS_WEAPON_FIST:
                case ITEM_SUBCLASS_WEAPON_DAGGER:
                    return 1200;
                case ITEM_SUBCLASS_WEAPON_AXE2:
                case ITEM_SUBCLASS_WEAPON_MACE2:
                case ITEM_SUBCLASS_WEAPON_POLEARM:
                case ITEM_SUBCLASS_WEAPON_SWORD2:
                case ITEM_SUBCLASS_WEAPON_STAFF:
                    return 1500;
                case ITEM_SUBCLASS_WEAPON_THROWN:
                    return 350;
                default:
                    return 1000;
            }
        }
        case ITEM_CLASS_ARMOR:
        {
            switch (prototype->SubClass)
            {
                case ITEM_SUBCLASS_ARMOR_MISC:
                case ITEM_SUBCLASS_ARMOR_CLOTH:
                    return 500;
                case ITEM_SUBCLASS_ARMOR_LEATHER:
                    return 600;
                case ITEM_SUBCLASS_ARMOR_MAIL:
                    return 700;
                case ITEM_SUBCLASS_ARMOR_PLATE:
                case ITEM_SUBCLASS_ARMOR_SHIELD:
                    return 800;
                default:
                    return 400;
            }
        }
        case ITEM_CLASS_REAGENT:
        case ITEM_CLASS_PROJECTILE:
            return 50;
        case ITEM_CLASS_TRADE_GOODS:
        {
            switch (prototype->SubClass)
            {
                case ITEM_SUBCLASS_TRADE_GOODS:
                case ITEM_SUBCLASS_PARTS:
                case ITEM_SUBCLASS_MEAT:
                    return 50;
                case ITEM_SUBCLASS_EXPLOSIVES:
                    return 250;
                case ITEM_SUBCLASS_DEVICES:
                    return 500;
                case ITEM_SUBCLASS_ELEMENTAL:
                case ITEM_SUBCLASS_TRADE_GOODS_OTHER:
                case ITEM_SUBCLASS_ENCHANTING:
                    return 300;
                default:
                    return 100;
            }
        }
        case ITEM_CLASS_QUEST: return 1000;
        case ITEM_CLASS_KEY: return 3000;
        default:
            return 500;
    }
}

/**
 * @brief 设置所有拍卖行的物品数量比例
 *
 * 同时设置联盟、部落和中立拍卖行的物品数量比例。
 *
 * @param al 联盟拍卖行物品数量比例
 * @param ho 部落拍卖行物品数量比例
 * @param ne 中立拍卖行物品数量比例
 */
void AuctionBotSeller::SetItemsRatio(uint32 al, uint32 ho, uint32 ne)
{
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO, std::max(al, 100000u));
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO, std::max(ho, 100000u));
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO, std::max(ne, 100000u));

    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        LoadItemsQuantity(_houseConfig[i]);
}

/**
 * @brief 设置指定拍卖行的物品数量比例
 *
 * @param house 拍卖行类型
 * @param val 物品数量比例值
 */
void AuctionBotSeller::SetItemsRatioForHouse(AuctionHouseType house, uint32 val)
{
    val = std::max(val, 10000u); // 应用与配置加载相同的上限

    switch (house)
    {
        case AUCTION_HOUSE_ALLIANCE: sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO, val); break;
        case AUCTION_HOUSE_HORDE:    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO, val); break;
        default:                     sAuctionBotConfig->SetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO, val); break;
    }

    LoadItemsQuantity(_houseConfig[house]);
}

/**
 * @brief 设置所有品质的物品数量
 *
 * 同时设置所有品质(灰色到橙色)的物品数量。
 *
 * @param amounts 包含所有品质物品数量的数组
 */
void AuctionBotSeller::SetItemsAmount(std::array<uint32, MAX_AUCTION_QUALITY> const& amounts)
{
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_GRAY_AMOUNT, amounts[AUCTION_QUALITY_GRAY]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_WHITE_AMOUNT, amounts[AUCTION_QUALITY_WHITE]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_GREEN_AMOUNT, amounts[AUCTION_QUALITY_GREEN]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_BLUE_AMOUNT, amounts[AUCTION_QUALITY_BLUE]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_PURPLE_AMOUNT, amounts[AUCTION_QUALITY_PURPLE]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_ORANGE_AMOUNT, amounts[AUCTION_QUALITY_ORANGE]);
    sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_YELLOW_AMOUNT, amounts[AUCTION_QUALITY_YELLOW]);

    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        LoadItemsQuantity(_houseConfig[i]);
}

/**
 * @brief 设置指定品质的物品数量
 *
 * @param quality 物品品质
 * @param val 物品数量值
 */
void AuctionBotSeller::SetItemsAmountForQuality(AuctionQuality quality, uint32 val)
{
    switch (quality)
    {
        case AUCTION_QUALITY_GRAY:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_GRAY_AMOUNT, val); break;
        case AUCTION_QUALITY_WHITE:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_WHITE_AMOUNT, val); break;
        case AUCTION_QUALITY_GREEN:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_GREEN_AMOUNT, val); break;
        case AUCTION_QUALITY_BLUE:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_BLUE_AMOUNT, val); break;
        case AUCTION_QUALITY_PURPLE:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_PURPLE_AMOUNT, val); break;
        case AUCTION_QUALITY_ORANGE:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_ORANGE_AMOUNT, val); break;
        default:
            sAuctionBotConfig->SetConfig(CONFIG_AHBOT_ITEM_YELLOW_AMOUNT, val); break;
    }

    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        LoadItemsQuantity(_houseConfig[i]);
}

/**
 * @brief 向拍卖行添加新拍卖
 *
 * 根据配置自动向指定拍卖行添加新的拍卖项。
 * 该方法会:
 * 1. 确定需要补充的物品数量
 * 2. 随机选择需要补充的物品类别
 * 3. 从物品池中随机选择具体物品
 * 4. 创建物品并设置价格
 * 5. 创建拍卖项并保存到数据库
 *
 * @param config 销售者配置对象的引用
 */
void AuctionBotSeller::AddNewAuctions(SellerConfiguration& config)
{
    uint32 count = 0;
    uint32 items = 0;

    // 如果缺少大量物品,使用加速值快速填充拍卖行
    if (config.LastMissedItem > sAuctionBotConfig->GetItemPerCycleBoost())
    {
        items = sAuctionBotConfig->GetItemPerCycleBoost();
        TC_LOG_DEBUG("ahbot", "AHBot: Boost value used to fill AH! (if this happens often adjust both ItemsPerCycle in worldserver.conf)");
    }
    else
        items = sAuctionBotConfig->GetItemPerCycleNormal();

    // 确定拍卖行ID
    uint32 houseid = 0;
    switch (config.GetHouseType())
    {
        case AUCTION_HOUSE_ALLIANCE:
            houseid = AUCTIONHOUSE_ALLIANCE;
            break;
        case AUCTION_HOUSE_HORDE:
            houseid = AUCTIONHOUSE_HORDE;
            break;
        default:
            houseid = AUCTIONHOUSE_NEUTRAL;
            break;
    }

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(houseid);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config.GetHouseType());

    ItemsToSellArray itemsToSell;
    AllItemsArray allItems(MAX_AUCTION_QUALITY, std::vector<uint32>(MAX_ITEM_CLASS));

    // 主循环
    // GetItemsToSell会给出应该添加哪些类别的物品(如果有至少1个物品缺失则返回true)
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    while (GetItemsToSell(config, itemsToSell, allItems) && items > 0)
    {
        --items;

        // 从缺失物品表中随机选择一个位置
        ItemToSell const& sellItem = Trinity::Containers::SelectRandomContainerElement(itemsToSell);

        // 从物品池表中为选定的类别和颜色设置随机的物品ID
        uint32 itemId = Trinity::Containers::SelectRandomContainerElement(_itemPool[sellItem.Color][sellItem.Itemclass]);
        ++allItems[sellItem.Color][sellItem.Itemclass]; // 辅助表,避免在此循环中重新扫描数据库(因为物品是随机顺序添加的)

        if (!itemId)
        {
            TC_LOG_DEBUG("ahbot", "AHBot: Item entry 0 auction creating attempt.");
            continue;
        }

        ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(itemId);
        if (!prototype)
        {
            TC_LOG_DEBUG("ahbot", "AHBot: Unknown item {} auction creating attempt.", itemId);
            continue;
        }

        uint32 stackCount = GetStackSizeForItem(prototype, config);

        Item* item = Item::CreateItem(itemId, stackCount);
        if (!item)
        {
            TC_LOG_ERROR("ahbot", "AHBot: Item::CreateItem() returned NULL for item {} (stack: {})", itemId, stackCount);
            return;
        }

        // 更新刚创建的物品,如果需要随机属性则添加
        // 例如:如果没有这个步骤,"耐力之刻痕短剑"只会生成为"刻痕短剑"
        if (int32 randomPropertyId = GenerateItemRandomPropertyId(itemId))
            item->SetItemRandomProperties(randomPropertyId);

        uint32 buyoutPrice;
        uint32 bidPrice = 0;

        // 在此处设置物品价格
        SetPricesOfItem(prototype, config, buyoutPrice, bidPrice, stackCount);

        // 随机确定拍卖时长
        uint32 etime = urand(1, 3);
        switch (etime)
        {
            case 1:
                etime = DAY / 2;
                break;
            case 3:
                etime = 2 *DAY;
                break;
            case 2:
            default:
                etime = DAY;
                break;
        }

        // 创建拍卖项
        AuctionEntry* auctionEntry = new AuctionEntry();
        auctionEntry->Id = sObjectMgr->GenerateAuctionID();
        auctionEntry->owner = sAuctionBotConfig->GetRandChar();
        auctionEntry->itemGUIDLow = item->GetGUID().GetCounter();
        auctionEntry->itemEntry = item->GetEntry();
        auctionEntry->startbid = bidPrice;
        auctionEntry->buyout = buyoutPrice;
        auctionEntry->houseId = houseid;
        auctionEntry->bidder = 0;
        auctionEntry->bid = 0;
        auctionEntry->deposit = sAuctionMgr->GetAuctionDeposit(ahEntry, etime, item, stackCount);
        auctionEntry->auctionHouseEntry = ahEntry;
        auctionEntry->expire_time = GameTime::GetGameTime() + urand(config.GetMinTime(), config.GetMaxTime()) * HOUR;
        auctionEntry->Flags = AUCTION_ENTRY_FLAG_NONE;

        // 保存物品和拍卖项到数据库
        item->SaveToDB(trans);
        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(auctionEntry);
        auctionEntry->SaveToDB(trans);

        auctionHouse->AddAuction(auctionEntry);

        ++count;
    }
    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_DEBUG("ahbot", "AHBot: Added {} items to auction", count);
}

/**
 * @brief 更新指定拍卖行的拍卖项
 *
 * 该方法定期被调用,用于检查和补充拍卖行中的物品。
 * 如果拍卖行启用了机器人,会统计当前库存并添加缺失的物品。
 *
 * @param houseType 拍卖行类型(联盟/部落/中立)
 * @return true 如果该拍卖行已启用机器人
 * @return false 如果该拍卖行未启用机器人
 */
bool AuctionBotSeller::Update(AuctionHouseType houseType)
{
    if (sAuctionBotConfig->GetConfigItemAmountRatio(houseType) > 0)
    {
        TC_LOG_DEBUG("ahbot", "AHBot: {} selling ...", AuctionBotConfig::GetHouseTypeName(houseType));
        if (SetStat(_houseConfig[houseType]))
            AddNewAuctions(_houseConfig[houseType]);
        return true;
    }
    else
        return false;
}
