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
 * @file AuctionHouseBot.cpp
 * @brief 拍卖行机器人核心实现文件
 *
 * 本文件实现了拍卖行机器人系统的核心类:
 * - AuctionBotConfig: 配置管理器的具体实现
 * - AuctionHouseBot: 主管理器的具体实现
 *
 * 主要功能:
 * - 从配置文件加载和管理所有拍卖行机器人配置
 * - 初始化和管理买家、卖家代理
 * - 提供定期更新机制,协调买卖行为
 * - 提供GM命令接口,支持运行时配置调整
 *
 * 配置系统:
 * - 支持整数、布尔、浮点三种类型的配置项
 * - 提供范围验证和默认值设置
 * - 支持热重载配置
 *
 * 调用流程:
 * 1. 服务器启动时调用AuctionHouseBot::Initialize()
 * 2. Initialize()调用AuctionBotConfig::Initialize()加载配置
 * 3. 根据配置创建买家和卖家代理
 * 4. 世界更新循环定期调用AuctionHouseBot::Update()
 * 5. Update()轮流调用各个代理的Update()方法
 */

#include "AuctionHouseBot.h"
#include "AccountMgr.h"
#include "AuctionHouseBotBuyer.h"
#include "AuctionHouseBotSeller.h"
#include "AuctionHouseMgr.h"
#include "Config.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "World.h"

/**
 * @brief 获取AuctionBotConfig单例实例
 * @return AuctionBotConfig指针
 *
 * 使用静态局部变量实现线程安全的单例模式(C++11保证)。
 */
AuctionBotConfig* AuctionBotConfig::instance()
{
    static AuctionBotConfig instance;
    return &instance;
}

/**
 * @brief 初始化拍卖行机器人配置
 * @return 如果至少启用了一个功能返回true,否则返回false
 *
 * 执行以下初始化步骤:
 * 1. 从配置文件加载所有配置项
 * 2. 检查是否至少启用了买家或卖家功能
 * 3. 检查阵营交互设置,给出警告
 * 4. 加载拍卖行机器人账号的角色列表
 *
 * 配置验证:
 * - 如果买家和卖家都未启用,返回false
 * - 如果所有拍卖行的物品数量比例都为0且买家都未启用,返回false
 * - 如果启用了阵营交互但配置了阵营特定的设置,给出警告
 */
bool AuctionBotConfig::Initialize()
{
    // 从配置文件加载所有配置项
    GetConfigFromFile();

    // 检查是否至少启用了买家或卖家功能
    if (!GetConfig(CONFIG_AHBOT_BUYER_ENABLED) && !GetConfig(CONFIG_AHBOT_SELLER_ENABLED))
    {
        TC_LOG_INFO("ahbot", "AHBOT is Disabled.");
        return false;
    }

    // 检查是否所有功能都被禁用
    if (GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO) == 0 && GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO) == 0 && GetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO) == 0 &&
        !GetConfig(CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED) && !GetConfig(CONFIG_AHBOT_BUYER_HORDE_ENABLED) && !GetConfig(CONFIG_AHBOT_BUYER_NEUTRAL_ENABLED))
    {
        TC_LOG_INFO("ahbot", "All feature of AuctionHouseBot are disabled!");
        return false;
    }

    // 输出卖家和买家的启用状态
    if (GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO) == 0 && GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO) == 0 && GetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO) == 0)
        TC_LOG_INFO("ahbot", "AuctionHouseBot SELLER is disabled!");

    if (!GetConfig(CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED) && !GetConfig(CONFIG_AHBOT_BUYER_HORDE_ENABLED) && !GetConfig(CONFIG_AHBOT_BUYER_NEUTRAL_ENABLED))
        TC_LOG_INFO("ahbot", "AuctionHouseBot BUYER is disabled!");

    // 检查阵营交互设置,给出警告
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        TC_LOG_INFO("ahbot", "AllowTwoSide.Interaction.Auction is enabled, AuctionHouseBot faction-specific settings might not work as expected!");
        if (GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO) != 0 || GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO) != 0
            || GetConfig(CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED) || GetConfig(CONFIG_AHBOT_BUYER_HORDE_ENABLED))
            TC_LOG_WARN("ahbot", "AllowTwoSide.Interaction.Auction is enabled, AuctionHouseBot should be enabled only for Neutral faction!");
    }

    // 加载每周期处理的物品数量配置
    _itemsPerCycleBoost = GetConfig(CONFIG_AHBOT_ITEMS_PER_CYCLE_BOOST);
    _itemsPerCycleNormal = GetConfig(CONFIG_AHBOT_ITEMS_PER_CYCLE_NORMAL);

    // 加载拍卖行机器人账号的角色列表
    if (uint32 ahBotAccId = GetConfig(CONFIG_AHBOT_ACCOUNT_ID))
    {
        // 检查账号是否有角色
        if (AccountMgr::GetCharactersCount(GetConfig(CONFIG_AHBOT_ACCOUNT_ID)))
        {
            // 查询账号关联的所有角色GUID
            uint32 count = 0;
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);
            stmt->setUInt32(0, ahBotAccId);
            if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            {
                do
                {
                    Field* fields = result->Fetch();
                    _AHBotCharacters.push_back(fields[0].GetUInt32());
                    ++count;
                } while (result->NextRow());
            }

            TC_LOG_DEBUG("ahbot", "AuctionHouseBot found {} characters", count);
        }
        else
            TC_LOG_WARN("ahbot", "AuctionHouseBot Account ID {} has no associated characters.", ahBotAccId);
    }

    return true;
}

/**
 * @brief 加载整数配置项(无范围限制)
 * @param index 配置项索引
 * @param fieldname 配置文件中的字段名
 * @param defvalue 默认值
 *
 * 从配置文件读取整数值,如果为负数则使用默认值并记录错误日志。
 */
void AuctionBotConfig::SetConfig(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue)
{
    SetConfig(index, sConfigMgr->GetIntDefault(fieldname, defvalue));

    // 检查是否为负数,如果为负数则使用默认值
    if (int32(GetConfig(index)) < 0)
    {
        TC_LOG_ERROR("ahbot", "AHBot: {} ({}) can't be negative. Using {} instead.", fieldname, int32(GetConfig(index)), defvalue);
        SetConfig(index, defvalue);
    }
}

/**
 * @brief 加载整数配置项(最大值限制)
 * @param index 配置项索引
 * @param fieldname 配置文件中的字段名
 * @param defvalue 默认值
 * @param maxvalue 最大值
 *
 * 从配置文件读取整数值,如果超过最大值则使用最大值并记录错误日志。
 */
void AuctionBotConfig::SetConfigMax(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue, uint32 maxvalue)
{
    SetConfig(index, sConfigMgr->GetIntDefault(fieldname, defvalue));

    // 检查是否超过最大值
    if (GetConfig(index) > maxvalue)
    {
        TC_LOG_ERROR("ahbot", "AHBot: {} ({}) must be in range 0...{}. Using {} instead.", fieldname, GetConfig(index), maxvalue, maxvalue);
        SetConfig(index, maxvalue);
    }
}

/**
 * @brief 加载整数配置项(最小和最大值限制)
 * @param index 配置项索引
 * @param fieldname 配置文件中的字段名
 * @param defvalue 默认值
 * @param minvalue 最小值
 * @param maxvalue 最大值
 *
 * 从配置文件读取整数值,如果超出范围则使用边界值并记录错误日志。
 */
void AuctionBotConfig::SetConfigMinMax(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue, uint32 minvalue, uint32 maxvalue)
{
    SetConfig(index, sConfigMgr->GetIntDefault(fieldname, defvalue));

    // 检查是否超过最大值
    if (GetConfig(index) > maxvalue)
    {
        TC_LOG_ERROR("ahbot", "AHBot: {} ({}) must be in range {}...{}. Using {} instead.", fieldname, GetConfig(index), minvalue, maxvalue, maxvalue);
        SetConfig(index, maxvalue);
    }

    // 检查是否小于最小值
    if (GetConfig(index) < minvalue)
    {
        TC_LOG_ERROR("ahbot", "AHBot: {} ({}) must be in range {}...{}. Using {} instead.", fieldname, GetConfig(index), minvalue, maxvalue, minvalue);
        SetConfig(index, minvalue);
    }
}

/**
 * @brief 加载布尔配置项
 * @param index 配置项索引
 * @param fieldname 配置文件中的字段名
 * @param defvalue 默认值
 */
void AuctionBotConfig::SetConfig(AuctionBotConfigBoolValues index, char const* fieldname, bool defvalue)
{
    SetConfig(index, sConfigMgr->GetBoolDefault(fieldname, defvalue));
}

/**
 * @brief 加载浮点配置项
 * @param index 配置项索引
 * @param fieldname 配置文件中的字段名
 * @param defvalue 默认值
 */
void AuctionBotConfig::SetConfig(AuctionBotConfigFloatValues index, char const* fieldname, float defvalue)
{
    SetConfig(index, sConfigMgr->GetFloatDefault(fieldname, defvalue));
}

/**
 * @brief 从配置文件加载所有拍卖行机器人配置项
 *
 * 此函数从worldserver.conf读取所有拍卖行机器人相关的配置项。
 * 配置项包括:
 * - 账号ID和角色管理
 * - 各拍卖行的物品数量比例和价格比例
 * - 物品品质相关的数量和价格配置
 * - 物品类型优先级和价格配置
 * - 买家相关配置(启用状态、基础价格、几率倍数等)
 * - 物品过滤规则(来源、绑定类型等)
 * - 拍卖持续时间范围
 *
 * 性能注意: 此函数在初始化和重载配置时调用,不是高频操作
 */
void AuctionBotConfig::GetConfigFromFile()
{
    SetConfig(CONFIG_AHBOT_ACCOUNT_ID, "AuctionHouseBot.Account", 0);

    SetConfigMax(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO, "AuctionHouseBot.Alliance.Items.Amount.Ratio", 100, 10000);
    SetConfigMax(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO, "AuctionHouseBot.Horde.Items.Amount.Ratio", 100, 10000);
    SetConfigMax(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO, "AuctionHouseBot.Neutral.Items.Amount.Ratio", 100, 10000);

    SetAHBotIncludes(sConfigMgr->GetStringDefault("AuctionHouseBot.forceIncludeItems", ""));
    SetAHBotExcludes(sConfigMgr->GetStringDefault("AuctionHouseBot.forceExcludeItems", ""));

    SetConfig(CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED, "AuctionHouseBot.Buyer.Alliance.Enabled", false);
    SetConfig(CONFIG_AHBOT_BUYER_HORDE_ENABLED, "AuctionHouseBot.Buyer.Horde.Enabled", false);
    SetConfig(CONFIG_AHBOT_BUYER_NEUTRAL_ENABLED, "AuctionHouseBot.Buyer.Neutral.Enabled", false);

    SetConfig(CONFIG_AHBOT_BUYER_CHANCE_FACTOR, "AuctionHouseBot.Buyer.ChanceFactor", 2.0f);

    SetConfig(CONFIG_AHBOT_ITEMS_VENDOR, "AuctionHouseBot.Items.Vendor", false);
    SetConfig(CONFIG_AHBOT_ITEMS_LOOT, "AuctionHouseBot.Items.Loot", true);
    SetConfig(CONFIG_AHBOT_ITEMS_MISC, "AuctionHouseBot.Items.Misc", false);

    SetConfig(CONFIG_AHBOT_BIND_NO, "AuctionHouseBot.Bind.No", true);
    SetConfig(CONFIG_AHBOT_BIND_PICKUP, "AuctionHouseBot.Bind.Pickup", false);
    SetConfig(CONFIG_AHBOT_BIND_EQUIP, "AuctionHouseBot.Bind.Equip", true);
    SetConfig(CONFIG_AHBOT_BIND_USE, "AuctionHouseBot.Bind.Use", true);
    SetConfig(CONFIG_AHBOT_BIND_QUEST, "AuctionHouseBot.Bind.Quest", false);
    SetConfig(CONFIG_AHBOT_LOCKBOX_ENABLED, "AuctionHouseBot.LockBox.Enabled", false);

    SetConfig(CONFIG_AHBOT_BUYPRICE_SELLER, "AuctionHouseBot.BuyPrice.Seller", false);

    SetConfig(CONFIG_AHBOT_ITEMS_PER_CYCLE_BOOST, "AuctionHouseBot.ItemsPerCycle.Boost", 1000);
    SetConfig(CONFIG_AHBOT_ITEMS_PER_CYCLE_NORMAL, "AuctionHouseBot.ItemsPerCycle.Normal", 20);

    SetConfig(CONFIG_AHBOT_ITEM_MIN_ITEM_LEVEL, "AuctionHouseBot.Items.ItemLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_ITEM_MAX_ITEM_LEVEL, "AuctionHouseBot.Items.ItemLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_ITEM_MIN_REQ_LEVEL, "AuctionHouseBot.Items.ReqLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_ITEM_MAX_REQ_LEVEL, "AuctionHouseBot.Items.ReqLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_ITEM_MIN_SKILL_RANK, "AuctionHouseBot.Items.ReqSkill.Min", 0);
    SetConfig(CONFIG_AHBOT_ITEM_MAX_SKILL_RANK, "AuctionHouseBot.Items.ReqSkill.Max", 0);

    SetConfig(CONFIG_AHBOT_ITEM_GRAY_AMOUNT, "AuctionHouseBot.Items.Amount.Gray", 0);
    SetConfig(CONFIG_AHBOT_ITEM_WHITE_AMOUNT, "AuctionHouseBot.Items.Amount.White", 2000);
    SetConfig(CONFIG_AHBOT_ITEM_GREEN_AMOUNT, "AuctionHouseBot.Items.Amount.Green", 2500);
    SetConfig(CONFIG_AHBOT_ITEM_BLUE_AMOUNT, "AuctionHouseBot.Items.Amount.Blue", 1500);
    SetConfig(CONFIG_AHBOT_ITEM_PURPLE_AMOUNT, "AuctionHouseBot.Items.Amount.Purple", 500);
    SetConfig(CONFIG_AHBOT_ITEM_ORANGE_AMOUNT, "AuctionHouseBot.Items.Amount.Orange", 0);
    SetConfig(CONFIG_AHBOT_ITEM_YELLOW_AMOUNT, "AuctionHouseBot.Items.Amount.Yellow", 0);

    SetConfigMax(CONFIG_AHBOT_CLASS_CONSUMABLE_PRIORITY, "AuctionHouseBot.Class.Consumable", 6, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_CONTAINER_PRIORITY, "AuctionHouseBot.Class.Container", 4, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_WEAPON_PRIORITY, "AuctionHouseBot.Class.Weapon", 8, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_GEM_PRIORITY, "AuctionHouseBot.Class.Gem", 3, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_ARMOR_PRIORITY, "AuctionHouseBot.Class.Armor", 8, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_REAGENT_PRIORITY, "AuctionHouseBot.Class.Reagent", 1, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_PROJECTILE_PRIORITY, "AuctionHouseBot.Class.Projectile", 2, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_TRADEGOOD_PRIORITY, "AuctionHouseBot.Class.TradeGood", 10, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_GENERIC_PRIORITY, "AuctionHouseBot.Class.Generic", 1, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_RECIPE_PRIORITY, "AuctionHouseBot.Class.Recipe", 6, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_QUIVER_PRIORITY, "AuctionHouseBot.Class.Quiver", 1, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_QUEST_PRIORITY, "AuctionHouseBot.Class.Quest", 1, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_KEY_PRIORITY, "AuctionHouseBot.Class.Key", 1, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_MISC_PRIORITY, "AuctionHouseBot.Class.Misc", 5, 10);
    SetConfigMax(CONFIG_AHBOT_CLASS_GLYPH_PRIORITY, "AuctionHouseBot.Class.Glyph", 3, 10);

    SetConfig(CONFIG_AHBOT_ALLIANCE_PRICE_RATIO, "AuctionHouseBot.Alliance.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_HORDE_PRICE_RATIO, "AuctionHouseBot.Horde.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_NEUTRAL_PRICE_RATIO, "AuctionHouseBot.Neutral.Price.Ratio", 100);

    SetConfig(CONFIG_AHBOT_ITEM_GRAY_PRICE_RATIO, "AuctionHouseBot.Items.Gray.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_WHITE_PRICE_RATIO, "AuctionHouseBot.Items.White.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_GREEN_PRICE_RATIO, "AuctionHouseBot.Items.Green.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_BLUE_PRICE_RATIO, "AuctionHouseBot.Items.Blue.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_PURPLE_PRICE_RATIO, "AuctionHouseBot.Items.Purple.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_ORANGE_PRICE_RATIO, "AuctionHouseBot.Items.Orange.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_ITEM_YELLOW_PRICE_RATIO, "AuctionHouseBot.Items.Yellow.Price.Ratio", 100);

    SetConfig(CONFIG_AHBOT_CLASS_CONSUMABLE_PRICE_RATIO, "AuctionHouseBot.Class.Consumable.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_CONTAINER_PRICE_RATIO, "AuctionHouseBot.Class.Container.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_WEAPON_PRICE_RATIO, "AuctionHouseBot.Class.Weapon.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_GEM_PRICE_RATIO, "AuctionHouseBot.Class.Gem.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_ARMOR_PRICE_RATIO, "AuctionHouseBot.Class.Armor.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_REAGENT_PRICE_RATIO, "AuctionHouseBot.Class.Reagent.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_PROJECTILE_PRICE_RATIO, "AuctionHouseBot.Class.Projectile.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_PRICE_RATIO, "AuctionHouseBot.Class.TradeGood.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_GENERIC_PRICE_RATIO, "AuctionHouseBot.Class.Generic.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RECIPE_PRICE_RATIO, "AuctionHouseBot.Class.Recipe.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_QUIVER_PRICE_RATIO, "AuctionHouseBot.Class.Quiver.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_QUEST_PRICE_RATIO, "AuctionHouseBot.Class.Quest.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_KEY_PRICE_RATIO, "AuctionHouseBot.Class.Key.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_MISC_PRICE_RATIO, "AuctionHouseBot.Class.Misc.Price.Ratio", 100);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_PRICE_RATIO, "AuctionHouseBot.Class.Glyph.Price.Ratio", 100);

    SetConfig(CONFIG_AHBOT_CLASS_CONSUMABLE_ALLOW_ZERO, "AuctionHouseBot.Class.Consumable.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_CONTAINER_ALLOW_ZERO, "AuctionHouseBot.Class.Container.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_WEAPON_ALLOW_ZERO, "AuctionHouseBot.Class.Weapon.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_GEM_ALLOW_ZERO, "AuctionHouseBot.Class.Gem.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_ARMOR_ALLOW_ZERO, "AuctionHouseBot.Class.Armor.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_REAGENT_ALLOW_ZERO, "AuctionHouseBot.Class.Reagent.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_PROJECTILE_ALLOW_ZERO, "AuctionHouseBot.Class.Projectile.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_ALLOW_ZERO, "AuctionHouseBot.Class.TradeGood.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_RECIPE_ALLOW_ZERO, "AuctionHouseBot.Class.Recipe.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_QUIVER_ALLOW_ZERO, "AuctionHouseBot.Class.Quiver.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_QUEST_ALLOW_ZERO, "AuctionHouseBot.Class.Quest.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_KEY_ALLOW_ZERO, "AuctionHouseBot.Class.Key.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_MISC_ALLOW_ZERO, "AuctionHouseBot.Class.Misc.Allow.Zero", false);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_ALLOW_ZERO, "AuctionHouseBot.Class.Glyph.Allow.Zero", false);

    SetConfig(CONFIG_AHBOT_MINTIME, "AuctionHouseBot.MinTime", 1);
    SetConfig(CONFIG_AHBOT_MAXTIME, "AuctionHouseBot.MaxTime", 72);

    SetConfigMinMax(CONFIG_AHBOT_BUYER_RECHECK_INTERVAL, "AuctionHouseBot.Buyer.Recheck.Interval", 20, 1, DAY / MINUTE);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_GRAY, "AuctionHouseBot.Buyer.Baseprice.Gray", 3504);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_WHITE, "AuctionHouseBot.Buyer.Baseprice.White", 5429);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_GREEN, "AuctionHouseBot.Buyer.Baseprice.Green", 21752);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_BLUE, "AuctionHouseBot.Buyer.Baseprice.Blue", 36463);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_PURPLE, "AuctionHouseBot.Buyer.Baseprice.Purple", 87124);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_ORANGE, "AuctionHouseBot.Buyer.Baseprice.Orange", 214347);
    SetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_YELLOW, "AuctionHouseBot.Buyer.Baseprice.Yellow", 407406);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GRAY, "AuctionHouseBot.Buyer.ChanceMultiplier.Gray", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_WHITE, "AuctionHouseBot.Buyer.ChanceMultiplier.White", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GREEN, "AuctionHouseBot.Buyer.ChanceMultiplier.Green", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_BLUE, "AuctionHouseBot.Buyer.ChanceMultiplier.Blue", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_PURPLE, "AuctionHouseBot.Buyer.ChanceMultiplier.Purple", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_ORANGE, "AuctionHouseBot.Buyer.ChanceMultiplier.Orange", 100);
    SetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_YELLOW, "AuctionHouseBot.Buyer.ChanceMultiplier.Yellow", 100);

    SetConfig(CONFIG_AHBOT_SELLER_ENABLED, "AuctionHouseBot.Seller.Enabled", false);
    SetConfig(CONFIG_AHBOT_BUYER_ENABLED, "AuctionHouseBot.Buyer.Enabled", false);

    SetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_REQ_LEVEL, "AuctionHouseBot.Class.Misc.Mount.ReqLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_REQ_LEVEL, "AuctionHouseBot.Class.Misc.Mount.ReqLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_SKILL_RANK, "AuctionHouseBot.Class.Misc.Mount.ReqSkill.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_SKILL_RANK, "AuctionHouseBot.Class.Misc.Mount.ReqSkill.Max", 0);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_MIN_REQ_LEVEL, "AuctionHouseBot.Class.Glyph.ReqLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_MAX_REQ_LEVEL, "AuctionHouseBot.Class.Glyph.ReqLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_MIN_ITEM_LEVEL, "AuctionHouseBot.Class.Glyph.ItemLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_GLYPH_MAX_ITEM_LEVEL, "AuctionHouseBot.Class.Glyph.ItemLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_MIN_ITEM_LEVEL, "AuctionHouseBot.Class.TradeGood.ItemLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_TRADEGOOD_MAX_ITEM_LEVEL, "AuctionHouseBot.Class.TradeGood.ItemLevel.Max", 0);
    SetConfig(CONFIG_AHBOT_CLASS_CONTAINER_MIN_ITEM_LEVEL, "AuctionHouseBot.Class.Container.ItemLevel.Min", 0);
    SetConfig(CONFIG_AHBOT_CLASS_CONTAINER_MAX_ITEM_LEVEL, "AuctionHouseBot.Class.Container.ItemLevel.Max", 0);

    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONSUMABLE, "AuctionHouseBot.Class.RandomStackRatio.Consumable", 20);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONTAINER, "AuctionHouseBot.Class.RandomStackRatio.Container", 0);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_WEAPON, "AuctionHouseBot.Class.RandomStackRatio.Weapon", 0);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GEM, "AuctionHouseBot.Class.RandomStackRatio.Gem", 20);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_ARMOR, "AuctionHouseBot.Class.RandomStackRatio.Armor", 0);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_REAGENT, "AuctionHouseBot.Class.RandomStackRatio.Reagent", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_PROJECTILE, "AuctionHouseBot.Class.RandomStackRatio.Projectile", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_TRADEGOOD, "AuctionHouseBot.Class.RandomStackRatio.TradeGood", 50);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GENERIC, "AuctionHouseBot.Class.RandomStackRatio.Generic", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_RECIPE, "AuctionHouseBot.Class.RandomStackRatio.Recipe", 0);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUIVER, "AuctionHouseBot.Class.RandomStackRatio.Quiver", 0);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUEST, "AuctionHouseBot.Class.RandomStackRatio.Quest", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_KEY, "AuctionHouseBot.Class.RandomStackRatio.Key", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_MISC, "AuctionHouseBot.Class.RandomStackRatio.Misc", 100);
    SetConfig(CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GLYPH, "AuctionHouseBot.Class.RandomStackRatio.Glyph", 0);

    SetConfig(CONFIG_AHBOT_BIDPRICE_MIN, "AuctionHouseBot.BidPrice.Min", 0.6f);
    SetConfig(CONFIG_AHBOT_BIDPRICE_MAX, "AuctionHouseBot.BidPrice.Max", 0.9f);
}

/**
 * @brief 获取拍卖行类型名称
 * @param houseType 拍卖行类型
 * @return 类型名称字符串
 *
 * 返回易读的拍卖行类型名称,用于日志输出和GM命令显示。
 */
char const* AuctionBotConfig::GetHouseTypeName(AuctionHouseType houseType)
{
    static char const* names[MAX_AUCTION_HOUSE_TYPE] = { "Neutral", "Alliance", "Horde" };
    return names[houseType];
}

/**
 * @brief 随机选择一个拍卖行机器人角色
 * @return 角色GUID,如果列表为空返回0
 *
 * 从拍卖行机器人角色列表中随机选择一个角色。
 * 用于随机选择拍卖上架者或竞拍者。
 */
uint32 AuctionBotConfig::GetRandChar() const
{
    if (_AHBotCharacters.empty())
        return 0;

    return Trinity::Containers::SelectRandomContainerElement(_AHBotCharacters);
}

/**
 * @brief 随机选择一个拍卖行机器人角色(排除指定角色)
 * @param exclude 要排除的角色GUID
 * @return 角色GUID,如果列表为空或只有被排除的角色返回0
 *
 * 从拍卖行机器人角色列表中随机选择一个角色,但排除指定的角色。
 * 主要用于: 当一个角色上架物品时,用另一个角色来竞拍,避免自我竞拍。
 */
uint32 AuctionBotConfig::GetRandCharExclude(uint32 exclude) const
{
    if (_AHBotCharacters.empty())
        return 0;

    // 创建过滤后的角色列表
    std::vector<uint32> filteredCharacters;
    filteredCharacters.reserve(_AHBotCharacters.size() - 1);

    for (uint32 charId : _AHBotCharacters)
        if (charId != exclude)
            filteredCharacters.push_back(charId);

    if (filteredCharacters.empty())
        return 0;

    return Trinity::Containers::SelectRandomContainerElement(filteredCharacters);
}

/**
 * @brief 检查指定角色是否为拍卖行机器人角色
 * @param characterID 角色GUID
 * @return 如果是机器人角色返回true
 *
 * 用于区分机器人上架的物品和玩家上架的物品。
 * 注意: characterID为0时也返回true,用于处理特殊情况。
 */
bool AuctionBotConfig::IsBotChar(uint32 characterID) const
{
    return !characterID || std::find(_AHBotCharacters.begin(), _AHBotCharacters.end(), characterID) != _AHBotCharacters.end();
}

/**
 * @brief 获取指定拍卖行类型的物品数量比例
 * @param houseType 拍卖行类型
 * @return 物品数量比例(百分比)
 *
 * 根据拍卖行类型返回对应的物品数量比例配置。
 * 此比例用于控制拍卖行中机器人上架物品的数量。
 */
uint32 AuctionBotConfig::GetConfigItemAmountRatio(AuctionHouseType houseType) const
{
    switch (houseType)
    {
        case AUCTION_HOUSE_ALLIANCE:
            return GetConfig(CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO);
        case AUCTION_HOUSE_HORDE:
            return GetConfig(CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO);
        default:
            return GetConfig(CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO);
    }
}

/**
 * @brief 获取指定拍卖行类型的价格比例
 * @param houseType 拍卖行类型
 * @return 价格比例(百分比)
 *
 * 根据拍卖行类型返回对应的价格比例配置。
 * 此比例用于调整机器人上架物品的价格。
 */
uint32 AuctionBotConfig::GetConfigPriceRatio(AuctionHouseType houseType) const
{
    switch (houseType)
    {
        case AUCTION_HOUSE_ALLIANCE:
            return GetConfig(CONFIG_AHBOT_ALLIANCE_PRICE_RATIO);
        case AUCTION_HOUSE_HORDE:
            return GetConfig(CONFIG_AHBOT_HORDE_PRICE_RATIO);
        default:
            return GetConfig(CONFIG_AHBOT_NEUTRAL_PRICE_RATIO);
    }
}

/**
 * @brief 检查指定拍卖行类型的买家是否启用
 * @param houseType 拍卖行类型
 * @return 如果启用返回true
 *
 * 根据拍卖行类型返回对应的买家启用状态。
 * 允许为不同阵营的拍卖行配置不同的买家行为。
 */
bool AuctionBotConfig::GetConfigBuyerEnabled(AuctionHouseType houseType) const
{
    switch (houseType)
    {
        case AUCTION_HOUSE_ALLIANCE:
            return GetConfig(CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED);
        case AUCTION_HOUSE_HORDE:
            return GetConfig(CONFIG_AHBOT_BUYER_HORDE_ENABLED);
        default:
            return GetConfig(CONFIG_AHBOT_BUYER_NEUTRAL_ENABLED);
    }
}

/**
 * @brief 获取指定品质物品的目标数量
 * @param quality 物品品质
 * @return 目标数量
 *
 * 根据物品品质返回对应的目标数量配置。
 * 卖家代理会根据此数值决定上架多少该品质的物品。
 */
uint32 AuctionBotConfig::GetConfigItemQualityAmount(AuctionQuality quality) const
{
    switch (quality)
    {
        case AUCTION_QUALITY_GRAY:
            return GetConfig(CONFIG_AHBOT_ITEM_GRAY_AMOUNT);
        case AUCTION_QUALITY_WHITE:
            return GetConfig(CONFIG_AHBOT_ITEM_WHITE_AMOUNT);
        case AUCTION_QUALITY_GREEN:
            return GetConfig(CONFIG_AHBOT_ITEM_GREEN_AMOUNT);
        case AUCTION_QUALITY_BLUE:
            return GetConfig(CONFIG_AHBOT_ITEM_BLUE_AMOUNT);
        case AUCTION_QUALITY_PURPLE:
            return GetConfig(CONFIG_AHBOT_ITEM_PURPLE_AMOUNT);
        case AUCTION_QUALITY_ORANGE:
            return GetConfig(CONFIG_AHBOT_ITEM_ORANGE_AMOUNT);
        default:
            return GetConfig(CONFIG_AHBOT_ITEM_YELLOW_AMOUNT);
    }
}

/**
 * @brief AuctionHouseBot构造函数
 *
 * 初始化成员变量为nullptr和0,实际的对象创建在InitializeAgents()中进行。
 */
AuctionHouseBot::AuctionHouseBot(): _buyer(nullptr), _seller(nullptr), _operationSelector(0)
{
}

/**
 * @brief AuctionHouseBot析构函数
 *
 * 清理买家和卖家代理对象。
 */
AuctionHouseBot::~AuctionHouseBot()
{
    delete _buyer;
    delete _seller;
}

/**
 * @brief 初始化买家和卖家代理
 *
 * 根据配置创建相应的代理实例:
 * - 如果启用了卖家功能,创建卖家代理并初始化
 * - 如果启用了买家功能,创建买家代理并初始化
 * - 如果代理初始化失败,删除实例并置为nullptr
 *
 * 调用时机:
 * - AuctionHouseBot::Initialize()时调用
 * - ReloadAllConfig()重新加载配置时调用
 */
void AuctionHouseBot::InitializeAgents()
{
    // 创建卖家代理
    if (sAuctionBotConfig->GetConfig(CONFIG_AHBOT_SELLER_ENABLED))
    {
        delete _seller;

        _seller = new AuctionBotSeller();
        if (!_seller->Initialize())
        {
            delete _seller;
            _seller = nullptr;
        }
    }

    // 创建买家代理
    if (sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_ENABLED))
    {
        delete _buyer;

        _buyer = new AuctionBotBuyer();
        if (!_buyer->Initialize())
        {
            delete _buyer;
            _buyer = nullptr;
        }
    }
}

/**
 * @brief 初始化拍卖行机器人
 *
 * 初始化配置管理器,如果成功则创建代理实例。
 * 这是拍卖行机器人的主入口点,在服务器启动时调用。
 */
void AuctionHouseBot::Initialize()
{
    if (sAuctionBotConfig->Initialize())
        InitializeAgents();
}

/**
 * @brief 设置所有拍卖行的物品数量比例
 * @param al 联盟拍卖行比例
 * @param ho 部落拍卖行比例
 * @param ne 中立拍卖行比例
 *
 * GM命令接口,用于动态调整各拍卖行的物品数量比例。
 * 仅当卖家代理存在时有效。
 */
void AuctionHouseBot::SetItemsRatio(uint32 al, uint32 ho, uint32 ne)
{
    if (_seller)
        _seller->SetItemsRatio(al, ho, ne);
}

/**
 * @brief 设置指定拍卖行的物品数量比例
 * @param house 拍卖行类型
 * @param val 比例值
 *
 * GM命令接口,用于单独调整某个拍卖行的物品数量比例。
 */
void AuctionHouseBot::SetItemsRatioForHouse(AuctionHouseType house, uint32 val)
{
    if (_seller)
        _seller->SetItemsRatioForHouse(house, val);
}

/**
 * @brief 设置所有品质物品的目标数量
 * @param amounts 各品质数量的数组
 *
 * GM命令接口,用于批量调整各品质物品的目标数量。
 */
void AuctionHouseBot::SetItemsAmount(std::array<uint32, MAX_AUCTION_QUALITY> const& amounts)
{
    if (_seller)
        _seller->SetItemsAmount(amounts);
}

/**
 * @brief 设置指定品质物品的目标数量
 * @param quality 物品品质
 * @param val 目标数量
 *
 * GM命令接口,用于单独调整某个品质物品的目标数量。
 */
void AuctionHouseBot::SetItemsAmountForQuality(AuctionQuality quality, uint32 val)
{
    if (_seller)
        _seller->SetItemsAmountForQuality(quality, val);
}

/**
 * @brief 重新加载所有配置并重新初始化代理
 *
 * GM命令接口,用于热重载配置。
 * 先重新加载配置文件,然后重新创建代理实例。
 */
void AuctionHouseBot::ReloadAllConfig()
{
    sAuctionBotConfig->Reload();
    InitializeAgents();
}

/**
 * @brief 准备拍卖行状态信息
 * @param statusInfo 输出参数,存储各拍卖行的状态统计
 *
 * 遍历所有拍卖行,统计机器人上架的物品数量和各品质分布。
 * 用于GM命令查询拍卖行机器人状态。
 *
 * 性能注意: 此函数会遍历所有拍卖行,在物品数量较多时可能耗时,
 * 但由于是GM命令调用,频率较低。
 */
void AuctionHouseBot::PrepareStatusInfos(std::unordered_map<AuctionHouseType, AuctionHouseBotStatusInfoPerType>& statusInfo)
{
    // 遍历所有拍卖行类型
    for (AuctionHouseType ahType : EnumUtils::Iterate<AuctionHouseType>())
    {
        statusInfo[ahType].ItemsCount = 0;

        // 初始化各品质计数器
        for (AuctionQuality quality : EnumUtils::Iterate<AuctionQuality>())
            statusInfo[ahType].QualityInfo[quality] = 0;

        // 获取拍卖行对象
        AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(ahType);

        // 遍历拍卖行中的所有拍卖项
        for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = auctionHouse->GetAuctionsBegin(); itr != auctionHouse->GetAuctionsEnd(); ++itr)
        {
            AuctionEntry* auctionEntry = itr->second;

            // 获取拍卖物品
            if (Item* item = sAuctionMgr->GetAItem(auctionEntry->itemGUIDLow))
            {
                ItemTemplate const* prototype = item->GetTemplate();

                // 只统计机器人上架的物品(owner为0或属于机器人账号)
                if (!auctionEntry->owner || sAuctionBotConfig->IsBotChar(auctionEntry->owner))
                {
                    // 统计各品质物品数量
                    if (prototype->Quality < MAX_AUCTION_QUALITY)
                        ++statusInfo[ahType].QualityInfo[AuctionQuality(prototype->Quality)];

                    // 累计总数
                    ++statusInfo[ahType].ItemsCount;
                }
            }
        }
    }
}

/**
 * @brief 重建拍卖行(清理机器人上架的物品)
 * @param all 如果为true清理所有物品,否则只清理无人竞拍的物品
 *
 * 通过将拍卖项的过期时间设置为当前时间,强制其到期。
 * 主要用于GM命令重新生成拍卖行物品。
 *
 * 清理策略:
 * - all=true: 所有机器人上架的物品都立即过期
 * - all=false: 只有无人竞拍的机器人物品过期
 */
void AuctionHouseBot::Rebuild(bool all)
{
    // 遍历所有拍卖行
    for (uint32 i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
    {
        AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(AuctionHouseType(i));

        // 遍历拍卖项
        for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = auctionHouse->GetAuctionsBegin(); itr != auctionHouse->GetAuctionsEnd(); ++itr)
        {
            // 只处理机器人上架的拍卖项
            if (!itr->second->owner || sAuctionBotConfig->IsBotChar(itr->second->owner))
            {
                // 根据参数决定是否强制过期
                if (all || itr->second->bid == 0)
                    itr->second->expire_time = GameTime::GetGameTime();
            }
        }
    }
}

/**
 * @brief 获取AuctionHouseBot单例实例
 * @return AuctionHouseBot指针
 *
 * 使用静态局部变量实现线程安全的单例模式(C++11保证)。
 */
AuctionHouseBot* AuctionHouseBot::instance()
{
    static AuctionHouseBot instance;
    return &instance;
}

/**
 * @brief 更新拍卖行状态
 *
 * 定期调用,轮流处理各个拍卖行的卖家和买家操作。
 * 采用轮转机制,每次只处理一个拍卖行的一种操作,避免性能峰值。
 *
 * 轮转机制:
 * - _operationSelector范围: 0到2*MAX_AUCTION_HOUSE_TYPE-1
 * - 0-2: 卖家处理中立、联盟、部落拍卖行
 * - 3-5: 买家处理中立、联盟、部落拍卖行
 * - 每次调用后selector+1,超过范围则重置为0
 *
 * 调用时机: 世界更新循环中定期调用
 * 性能注意: 每次最多执行一个成功操作,分散负载
 */
void AuctionHouseBot::Update()
{
    // 如果买家和卖家都不存在,直接返回
    if (!_buyer && !_seller)
        return;

    // 尝试所有可能的更新情况,直到第一个成功
    for (uint32 count = 0; count < 2 * MAX_AUCTION_HOUSE_TYPE; ++count)
    {
        bool successStep = false;

        // 根据selector判断是卖家还是买家操作
        if (_operationSelector < MAX_AUCTION_HOUSE_TYPE)
        {
            // 卖家操作(0-2)
            if (_seller)
                successStep = _seller->Update(AuctionHouseType(_operationSelector));
        }
        else
        {
            // 买家操作(3-5)
            if (_buyer)
                successStep = _buyer->Update(AuctionHouseType(_operationSelector - MAX_AUCTION_HOUSE_TYPE));
        }

        // 移动到下一个操作
        ++_operationSelector;
        if (_operationSelector >= 2 * MAX_AUCTION_HOUSE_TYPE)
            _operationSelector = 0;

        // 每次调用只执行一个成功操作
        if (successStep)
            break;
    }
}
