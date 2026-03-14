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
 * @file AuctionHouseBot.h
 * @brief 拍卖行机器人核心模块头文件
 *
 * 本文件定义了TrinityCore拍卖行机器人系统的核心类和枚举类型。
 * 拍卖行机器人(AHBot)是一个自动化系统,用于:
 * - 自动上架物品到拍卖行,保持拍卖行有足够的物品
 * - 自动购买和竞拍玩家上架的物品,活跃拍卖行经济
 * - 根据配置自动调整价格和数量,模拟真实的经济环境
 *
 * 主要组件:
 * - AuctionBotConfig: 全局配置管理器
 * - AuctionBotAgent: 机器人代理基类
 * - AuctionHouseBot: 主管理器,协调买卖行为
 * - AuctionBotSeller: 销售代理(定义在单独文件)
 * - AuctionBotBuyer: 购买代理(定义在单独文件)
 *
 * 支持三种拍卖行类型:联盟、部落和中立拍卖行。
 */

#ifndef AUCTION_HOUSE_BOT_H
#define AUCTION_HOUSE_BOT_H

#include "Define.h"
#include "SharedDefines.h"
#include <string>
#include <unordered_map>
#include <vector>

class AuctionBotSeller;
class AuctionBotBuyer;

/**
 * @enum AuctionQuality
 * @brief 拍卖物品品质等级枚举
 *
 * 继承自ItemQualities枚举,但跳过了传家宝(ITEM_QUALITY_HEIRLOOM)及其后的品质。
 * 用于拍卖行机器人对不同品质物品进行分类管理和定价策略。
 *
 * 品质从低到高依次为:
 * - GRAY(灰色): 垃圾物品,价值最低
 * - WHITE(白色): 普通物品
 * - GREEN(绿色): 优秀物品
 * - BLUE(蓝色): 精良物品
 * - PURPLE(紫色): 史诗物品
 * - ORANGE(橙色): 传说物品
 * - YELLOW(黄色): 神器物品
 */
enum AuctionQuality
{
    AUCTION_QUALITY_GRAY    = ITEM_QUALITY_POOR,        // 灰色品质(垃圾)
    AUCTION_QUALITY_WHITE   = ITEM_QUALITY_NORMAL,      // 白色品质(普通)
    AUCTION_QUALITY_GREEN   = ITEM_QUALITY_UNCOMMON,    // 绿色品质(优秀)
    AUCTION_QUALITY_BLUE    = ITEM_QUALITY_RARE,        // 蓝色品质(精良)
    AUCTION_QUALITY_PURPLE  = ITEM_QUALITY_EPIC,        // 紫色品质(史诗)
    AUCTION_QUALITY_ORANGE  = ITEM_QUALITY_LEGENDARY,   // 橙色品质(传说)
    AUCTION_QUALITY_YELLOW  = ITEM_QUALITY_ARTIFACT,    // 黄色品质(神器)
};

#define MAX_AUCTION_QUALITY 7  // 拍卖品质类型总数

/**
 * @enum AuctionHouseType
 * @brief 拍卖行类型枚举
 *
 * 定义游戏中的三种拍卖行类型,用于区分不同阵营的拍卖行。
 * 不同的拍卖行可以配置不同的物品数量比例和价格比例。
 */
enum AuctionHouseType
{
    AUCTION_HOUSE_NEUTRAL   = 0,  // 中立拍卖行(地精拍卖行,所有阵营可用)
    AUCTION_HOUSE_ALLIANCE  = 1,  // 联盟拍卖行(仅联盟玩家可用)
    AUCTION_HOUSE_HORDE     = 2   // 部落拍卖行(仅部落玩家可用)
};

#define MAX_AUCTION_HOUSE_TYPE 3  // 拍卖行类型总数

/**
 * @enum AuctionBotConfigUInt32Values
 * @brief 拍卖行机器人配置项索引枚举(整数型配置)
 *
 * 定义所有整数类型的配置项索引,用于访问配置数组。
 * 这些配置项控制拍卖行机器人的各种行为参数,包括:
 * - 拍卖持续时间范围
 * - 物品数量比例
 * - 价格比例
 * - 物品等级和技能等级限制
 * - 各品质物品的数量配置
 * - 各物品类型的优先级和价格比例
 * - 买家相关配置(重新检查间隔、基础价格、几率倍数等)
 */
enum AuctionBotConfigUInt32Values
{
    CONFIG_AHBOT_MAXTIME,                                   // 拍卖最长时间(小时)
    CONFIG_AHBOT_MINTIME,                                   // 拍卖最短时间(小时)
    CONFIG_AHBOT_ITEMS_PER_CYCLE_BOOST,                     // 每周期处理的物品数量(加速模式)
    CONFIG_AHBOT_ITEMS_PER_CYCLE_NORMAL,                    // 每周期处理的物品数量(正常模式)
    CONFIG_AHBOT_ALLIANCE_ITEM_AMOUNT_RATIO,                // 联盟拍卖行物品数量比例
    CONFIG_AHBOT_HORDE_ITEM_AMOUNT_RATIO,                   // 部落拍卖行物品数量比例
    CONFIG_AHBOT_NEUTRAL_ITEM_AMOUNT_RATIO,                 // 中立拍卖行物品数量比例
    CONFIG_AHBOT_ITEM_MIN_ITEM_LEVEL,                       // 物品最小物品等级
    CONFIG_AHBOT_ITEM_MAX_ITEM_LEVEL,                       // 物品最大物品等级
    CONFIG_AHBOT_ITEM_MIN_REQ_LEVEL,                        // 物品最小需求等级
    CONFIG_AHBOT_ITEM_MAX_REQ_LEVEL,                        // 物品最大需求等级
    CONFIG_AHBOT_ITEM_MIN_SKILL_RANK,                       // 物品最小技能等级要求
    CONFIG_AHBOT_ITEM_MAX_SKILL_RANK,                       // 物品最大技能等级要求
    CONFIG_AHBOT_ITEM_GRAY_AMOUNT,                          // 灰色物品数量
    CONFIG_AHBOT_ITEM_WHITE_AMOUNT,                         // 白色物品数量
    CONFIG_AHBOT_ITEM_GREEN_AMOUNT,                         // 绿色物品数量
    CONFIG_AHBOT_ITEM_BLUE_AMOUNT,                          // 蓝色物品数量
    CONFIG_AHBOT_ITEM_PURPLE_AMOUNT,                        // 紫色物品数量
    CONFIG_AHBOT_ITEM_ORANGE_AMOUNT,                        // 橙色物品数量
    CONFIG_AHBOT_ITEM_YELLOW_AMOUNT,                        // 黄色物品数量
    CONFIG_AHBOT_CLASS_CONSUMABLE_PRIORITY,                 // 消耗品类优先级
    CONFIG_AHBOT_CLASS_CONTAINER_PRIORITY,                  // 容器类优先级
    CONFIG_AHBOT_CLASS_WEAPON_PRIORITY,                     // 武器类优先级
    CONFIG_AHBOT_CLASS_GEM_PRIORITY,                        // 宝石类优先级
    CONFIG_AHBOT_CLASS_ARMOR_PRIORITY,                      // 护甲类优先级
    CONFIG_AHBOT_CLASS_REAGENT_PRIORITY,                    // 材料类优先级
    CONFIG_AHBOT_CLASS_PROJECTILE_PRIORITY,                 // 弹药类优先级
    CONFIG_AHBOT_CLASS_TRADEGOOD_PRIORITY,                  // 商品类优先级
    CONFIG_AHBOT_CLASS_GENERIC_PRIORITY,                    // 通用物品优先级
    CONFIG_AHBOT_CLASS_RECIPE_PRIORITY,                     // 配方类优先级
    CONFIG_AHBOT_CLASS_QUIVER_PRIORITY,                     // 箭袋类优先级
    CONFIG_AHBOT_CLASS_QUEST_PRIORITY,                      // 任务物品优先级
    CONFIG_AHBOT_CLASS_KEY_PRIORITY,                        // 钥匙类优先级
    CONFIG_AHBOT_CLASS_MISC_PRIORITY,                       // 杂项类优先级
    CONFIG_AHBOT_CLASS_GLYPH_PRIORITY,                      // 雕文类优先级
    CONFIG_AHBOT_ALLIANCE_PRICE_RATIO,                      // 联盟拍卖行价格比例
    CONFIG_AHBOT_HORDE_PRICE_RATIO,                         // 部落拍卖行价格比例
    CONFIG_AHBOT_NEUTRAL_PRICE_RATIO,                       // 中立拍卖行价格比例
    CONFIG_AHBOT_ITEM_GRAY_PRICE_RATIO,                     // 灰色物品价格比例
    CONFIG_AHBOT_ITEM_WHITE_PRICE_RATIO,                    // 白色物品价格比例
    CONFIG_AHBOT_ITEM_GREEN_PRICE_RATIO,                    // 绿色物品价格比例
    CONFIG_AHBOT_ITEM_BLUE_PRICE_RATIO,                     // 蓝色物品价格比例
    CONFIG_AHBOT_ITEM_PURPLE_PRICE_RATIO,                   // 紫色物品价格比例
    CONFIG_AHBOT_ITEM_ORANGE_PRICE_RATIO,                   // 橙色物品价格比例
    CONFIG_AHBOT_ITEM_YELLOW_PRICE_RATIO,                   // 黄色物品价格比例
    CONFIG_AHBOT_CLASS_CONSUMABLE_PRICE_RATIO,              // 消耗品类价格比例
    CONFIG_AHBOT_CLASS_CONTAINER_PRICE_RATIO,               // 容器类价格比例
    CONFIG_AHBOT_CLASS_WEAPON_PRICE_RATIO,                  // 武器类价格比例
    CONFIG_AHBOT_CLASS_GEM_PRICE_RATIO,                     // 宝石类价格比例
    CONFIG_AHBOT_CLASS_ARMOR_PRICE_RATIO,                   // 护甲类价格比例
    CONFIG_AHBOT_CLASS_REAGENT_PRICE_RATIO,                 // 材料类价格比例
    CONFIG_AHBOT_CLASS_PROJECTILE_PRICE_RATIO,              // 弹药类价格比例
    CONFIG_AHBOT_CLASS_TRADEGOOD_PRICE_RATIO,               // 商品类价格比例
    CONFIG_AHBOT_CLASS_GENERIC_PRICE_RATIO,                 // 通用物品价格比例
    CONFIG_AHBOT_CLASS_RECIPE_PRICE_RATIO,                  // 配方类价格比例
    CONFIG_AHBOT_CLASS_MONEY_PRICE_RATIO,                   // 金钱类价格比例
    CONFIG_AHBOT_CLASS_QUIVER_PRICE_RATIO,                  // 箭袋类价格比例
    CONFIG_AHBOT_CLASS_QUEST_PRICE_RATIO,                   // 任务物品价格比例
    CONFIG_AHBOT_CLASS_KEY_PRICE_RATIO,                     // 钥匙类价格比例
    CONFIG_AHBOT_CLASS_PERMANENT_PRICE_RATIO,               // 永久物品价格比例
    CONFIG_AHBOT_CLASS_MISC_PRICE_RATIO,                    // 杂项类价格比例
    CONFIG_AHBOT_CLASS_GLYPH_PRICE_RATIO,                   // 雕文类价格比例
    CONFIG_AHBOT_BUYER_RECHECK_INTERVAL,                    // 买家重新检查间隔(分钟)
    CONFIG_AHBOT_BUYER_BASEPRICE_GRAY,                      // 买家灰色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_WHITE,                     // 买家白色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_GREEN,                     // 买家绿色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_BLUE,                      // 买家蓝色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_PURPLE,                    // 买家紫色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_ORANGE,                    // 买家橙色物品基础价格
    CONFIG_AHBOT_BUYER_BASEPRICE_YELLOW,                    // 买家黄色物品基础价格
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GRAY,               // 买家灰色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_WHITE,              // 买家白色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GREEN,              // 买家绿色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_BLUE,               // 买家蓝色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_PURPLE,             // 买家紫色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_ORANGE,             // 买家橙色物品几率倍数
    CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_YELLOW,             // 买家黄色物品几率倍数
    CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_REQ_LEVEL,            // 坐骑最小需求等级
    CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_REQ_LEVEL,            // 坐骑最大需求等级
    CONFIG_AHBOT_CLASS_MISC_MOUNT_MIN_SKILL_RANK,           // 坐骑最小骑术等级
    CONFIG_AHBOT_CLASS_MISC_MOUNT_MAX_SKILL_RANK,           // 坐骑最大骑术等级
    CONFIG_AHBOT_CLASS_GLYPH_MIN_REQ_LEVEL,                 // 雕文最小需求等级
    CONFIG_AHBOT_CLASS_GLYPH_MAX_REQ_LEVEL,                 // 雕文最大需求等级
    CONFIG_AHBOT_CLASS_GLYPH_MIN_ITEM_LEVEL,                // 雕文最小物品等级
    CONFIG_AHBOT_CLASS_GLYPH_MAX_ITEM_LEVEL,                // 雕文最大物品等级
    CONFIG_AHBOT_CLASS_TRADEGOOD_MIN_ITEM_LEVEL,            // 商品类最小物品等级
    CONFIG_AHBOT_CLASS_TRADEGOOD_MAX_ITEM_LEVEL,            // 商品类最大物品等级
    CONFIG_AHBOT_CLASS_CONTAINER_MIN_ITEM_LEVEL,            // 容器类最小物品等级
    CONFIG_AHBOT_CLASS_CONTAINER_MAX_ITEM_LEVEL,            // 容器类最大物品等级
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONSUMABLE,         // 消耗品随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_CONTAINER,          // 容器随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_WEAPON,             // 武器随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GEM,                // 宝石随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_ARMOR,              // 护甲随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_REAGENT,            // 材料随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_PROJECTILE,         // 弹药随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_TRADEGOOD,          // 商品随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GENERIC,            // 通用物品随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_RECIPE,             // 配方随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUIVER,             // 箭袋随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_QUEST,              // 任务物品随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_KEY,                // 钥匙随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_MISC,               // 杂项随机堆叠比例
    CONFIG_AHBOT_CLASS_RANDOMSTACKRATIO_GLYPH,              // 雕文随机堆叠比例
    CONFIG_AHBOT_ACCOUNT_ID,                                // 拍卖行机器人账号ID
    CONFIG_UINT32_AHBOT_UINT32_COUNT                        // 配置项总数(用于数组大小)
};

/**
 * @enum AuctionBotConfigBoolValues
 * @brief 拍卖行机器人配置项索引枚举(布尔型配置)
 *
 * 定义所有布尔类型的配置项索引,用于控制拍卖行机器人的功能开关。
 * 包括买家启用状态、物品来源过滤、绑定类型过滤等开关选项。
 */
enum AuctionBotConfigBoolValues
{
    CONFIG_AHBOT_BUYER_ALLIANCE_ENABLED,               // 启用联盟拍卖行买家
    CONFIG_AHBOT_BUYER_HORDE_ENABLED,                  // 启用部落拍卖行买家
    CONFIG_AHBOT_BUYER_NEUTRAL_ENABLED,                // 启用中立拍卖行买家
    CONFIG_AHBOT_ITEMS_VENDOR,                         // 允许上架商人物品
    CONFIG_AHBOT_ITEMS_LOOT,                           // 允许上架掉落物品
    CONFIG_AHBOT_ITEMS_MISC,                           // 允许上架杂项物品
    CONFIG_AHBOT_BIND_NO,                              // 允许非绑定物品
    CONFIG_AHBOT_BIND_PICKUP,                          // 允许拾取绑定物品
    CONFIG_AHBOT_BIND_EQUIP,                           // 允许装备绑定物品
    CONFIG_AHBOT_BIND_USE,                             // 允许使用绑定物品
    CONFIG_AHBOT_BIND_QUEST,                           // 允许任务绑定物品
    CONFIG_AHBOT_BUYPRICE_SELLER,                      // 卖家使用购买价格而非出售价格
    CONFIG_AHBOT_SELLER_ENABLED,                       // 启用卖家功能
    CONFIG_AHBOT_BUYER_ENABLED,                        // 启用买家功能
    CONFIG_AHBOT_LOCKBOX_ENABLED,                      // 启用锁箱物品
    CONFIG_AHBOT_CLASS_CONSUMABLE_ALLOW_ZERO,          // 允许消耗品数量为零
    CONFIG_AHBOT_CLASS_CONTAINER_ALLOW_ZERO,           // 允许容器数量为零
    CONFIG_AHBOT_CLASS_WEAPON_ALLOW_ZERO,              // 允许武器数量为零
    CONFIG_AHBOT_CLASS_GEM_ALLOW_ZERO,                 // 允许宝石数量为零
    CONFIG_AHBOT_CLASS_ARMOR_ALLOW_ZERO,               // 允许护甲数量为零
    CONFIG_AHBOT_CLASS_REAGENT_ALLOW_ZERO,             // 允许材料数量为零
    CONFIG_AHBOT_CLASS_PROJECTILE_ALLOW_ZERO,          // 允许弹药数量为零
    CONFIG_AHBOT_CLASS_TRADEGOOD_ALLOW_ZERO,           // 允许商品数量为零
    CONFIG_AHBOT_CLASS_RECIPE_ALLOW_ZERO,              // 允许配方数量为零
    CONFIG_AHBOT_CLASS_QUIVER_ALLOW_ZERO,              // 允许箭袋数量为零
    CONFIG_AHBOT_CLASS_QUEST_ALLOW_ZERO,               // 允许任务物品数量为零
    CONFIG_AHBOT_CLASS_KEY_ALLOW_ZERO,                 // 允许钥匙数量为零
    CONFIG_AHBOT_CLASS_MISC_ALLOW_ZERO,                // 允许杂项数量为零
    CONFIG_AHBOT_CLASS_GLYPH_ALLOW_ZERO,               // 允许雕文数量为零
    CONFIG_UINT32_AHBOT_BOOL_COUNT                     // 布尔配置项总数
};

/**
 * @enum AuctionBotConfigFloatValues
 * @brief 拍卖行机器人配置项索引枚举(浮点型配置)
 *
 * 定义所有浮点类型的配置项索引,用于控制价格计算和几率因子等参数。
 */
enum AuctionBotConfigFloatValues
{
    CONFIG_AHBOT_BUYER_CHANCE_FACTOR,                  // 买家几率因子
    CONFIG_AHBOT_BIDPRICE_MIN,                         // 竞拍价格最小比例
    CONFIG_AHBOT_BIDPRICE_MAX,                         // 竞拍价格最大比例
    CONFIG_AHBOT_FLOAT_COUNT                           // 浮点配置项总数
};

/**
 * @class AuctionBotConfig
 * @brief 拍卖行机器人配置管理器
 *
 * 单例类,负责管理拍卖行机器人的所有配置数据。
 * 从配置文件(worldserver.conf)加载并缓存配置项,提供快速访问接口。
 *
 * 主要功能:
 * - 加载和存储所有拍卖行机器人配置项
 * - 提供类型安全的配置访问接口
 * - 管理拍卖行机器人角色列表
 * - 支持配置热重载
 *
 * 配置分类:
 * - 整数配置: 物品数量、价格比例、时间限制等
 * - 布尔配置: 功能开关、过滤选项等
 * - 浮点配置: 价格系数、几率因子等
 *
 * 调用时机:
 * - 服务器启动时调用Initialize()初始化
 * - GM命令重新加载配置时调用Reload()
 * - 其他拍卖行机器人组件通过sAuctionBotConfig访问配置
 */
class TC_GAME_API AuctionBotConfig
{
private:
    // 私有构造函数(单例模式)
    AuctionBotConfig(): _itemsPerCycleBoost(1000), _itemsPerCycleNormal(20) {}
    ~AuctionBotConfig() {}
    AuctionBotConfig(AuctionBotConfig const&) = delete;              // 禁用拷贝构造
    AuctionBotConfig& operator=(AuctionBotConfig const&) = delete;   // 禁用赋值操作

public:
    /**
     * @brief 获取单例实例
     * @return AuctionBotConfig指针
     */
    static AuctionBotConfig* instance();

    /**
     * @brief 初始化配置管理器
     * @return 如果至少启用了一个功能(买家或卖家)返回true,否则返回false
     *
     * 从配置文件加载所有配置项,检查配置的有效性,
     * 并加载拍卖行机器人账号关联的角色列表。
     */
    bool Initialize();

    /**
     * @brief 获取强制包含物品列表
     * @return 强制包含的物品ID字符串(逗号分隔)
     */
    std::string const& GetAHBotIncludes() const { return _AHBotIncludes; }

    /**
     * @brief 获取强制排除物品列表
     * @return 强制排除的物品ID字符串(逗号分隔)
     */
    std::string const& GetAHBotExcludes() const { return _AHBotExcludes; }

    // 配置访问接口
    uint32 GetConfig(AuctionBotConfigUInt32Values index) const { return _configUint32Values[index]; }
    bool GetConfig(AuctionBotConfigBoolValues index) const { return _configBoolValues[index]; }
    float GetConfig(AuctionBotConfigFloatValues index) const { return _configFloatValues[index]; }

    // 配置设置接口(运行时修改)
    void SetConfig(AuctionBotConfigBoolValues index, bool value) { _configBoolValues[index] = value; }
    void SetConfig(AuctionBotConfigUInt32Values index, uint32 value) { _configUint32Values[index] = value; }
    void SetConfig(AuctionBotConfigFloatValues index, float value) { _configFloatValues[index] = value; }

    /**
     * @brief 获取指定拍卖行类型的物品数量比例
     * @param houseType 拍卖行类型
     * @return 物品数量比例(百分比)
     */
    uint32 GetConfigItemAmountRatio(AuctionHouseType houseType) const;

    /**
     * @brief 获取指定拍卖行类型的价格比例
     * @param houseType 拍卖行类型
     * @return 价格比例(百分比)
     */
    uint32 GetConfigPriceRatio(AuctionHouseType houseType) const;

    /**
     * @brief 检查指定拍卖行类型的买家是否启用
     * @param houseType 拍卖行类型
     * @return 如果启用返回true
     */
    bool GetConfigBuyerEnabled(AuctionHouseType houseType) const;

    /**
     * @brief 获取指定品质物品的目标数量
     * @param quality 物品品质
     * @return 目标数量
     */
    uint32 GetConfigItemQualityAmount(AuctionQuality quality) const;

    /**
     * @brief 获取加速模式下的每周期处理物品数
     * @return 物品数量
     */
    uint32 GetItemPerCycleBoost() const { return _itemsPerCycleBoost; }

    /**
     * @brief 获取正常模式下的每周期处理物品数
     * @return 物品数量
     */
    uint32 GetItemPerCycleNormal() const { return _itemsPerCycleNormal; }

    /**
     * @brief 随机选择一个拍卖行机器人角色
     * @return 角色GUID,如果列表为空返回0
     */
    uint32 GetRandChar() const;

    /**
     * @brief 随机选择一个拍卖行机器人角色(排除指定角色)
     * @param exclude 要排除的角色GUID
     * @return 角色GUID,如果列表为空或只有被排除的角色返回0
     */
    uint32 GetRandCharExclude(uint32 exclude) const;

    /**
     * @brief 检查指定角色是否为拍卖行机器人角色
     * @param characterID 角色GUID
     * @return 如果是机器人角色返回true
     */
    bool IsBotChar(uint32 characterID) const;

    /**
     * @brief 重新加载配置文件
     *
     * 从配置文件重新加载所有配置项,用于GM命令热重载。
     */
    void Reload() { GetConfigFromFile(); }

    /**
     * @brief 获取拍卖行类型名称
     * @param houseType 拍卖行类型
     * @return 类型名称字符串(如"Alliance"、"Horde"、"Neutral")
     */
    static char const* GetHouseTypeName(AuctionHouseType houseType);

private:
    // 成员变量
    std::string _AHBotIncludes;                            // 强制包含的物品ID列表
    std::string _AHBotExcludes;                            // 强制排除的物品ID列表
    std::vector<uint32> _AHBotCharacters;                  // 拍卖行机器人角色GUID列表
    uint32 _itemsPerCycleBoost;                            // 加速模式每周期处理数
    uint32 _itemsPerCycleNormal;                           // 正常模式每周期处理数

    uint32 _configUint32Values[CONFIG_UINT32_AHBOT_UINT32_COUNT];  // 整数配置数组
    bool _configBoolValues[CONFIG_UINT32_AHBOT_BOOL_COUNT];        // 布尔配置数组
    float _configFloatValues[CONFIG_AHBOT_FLOAT_COUNT];            // 浮点配置数组

    // 私有设置方法
    void SetAHBotIncludes(const std::string& AHBotIncludes) { _AHBotIncludes = AHBotIncludes; }
    void SetAHBotExcludes(const std::string& AHBotExcludes) { _AHBotExcludes = AHBotExcludes; }

    // 配置加载辅助方法
    void SetConfig(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue);
    void SetConfigMax(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue, uint32 maxvalue);
    void SetConfigMinMax(AuctionBotConfigUInt32Values index, char const* fieldname, uint32 defvalue, uint32 minvalue, uint32 maxvalue);
    void SetConfig(AuctionBotConfigBoolValues index, char const* fieldname, bool defvalue);
    void SetConfig(AuctionBotConfigFloatValues index, char const* fieldname, float defvalue);

    /**
     * @brief 从配置文件加载所有配置项
     *
     * 读取worldserver.conf中的拍卖行机器人配置,
     * 并进行有效性验证和默认值设置。
     */
    void GetConfigFromFile();
};

// 全局访问宏,简化单例访问
#define sAuctionBotConfig AuctionBotConfig::instance()

/**
 * @class AuctionBotAgent
 * @brief 拍卖行机器人代理抽象基类
 *
 * 定义拍卖行机器人代理的基本接口,所有具体代理(买家、卖家)都需要继承此类。
 * 采用策略模式,允许不同类型的代理共享统一的管理框架。
 *
 * 主要职责:
 * - 定义初始化接口
 * - 定义更新接口,供主管理器定期调用
 *
 * 子类:
 * - AuctionBotSeller: 卖家代理,负责上架物品
 * - AuctionBotBuyer: 买家代理,负责购买和竞拍物品
 */
class AuctionBotAgent
{
public:
    AuctionBotAgent() {}
    virtual ~AuctionBotAgent() {}

    /**
     * @brief 初始化代理
     * @return 如果初始化成功返回true
     */
    virtual bool Initialize() = 0;

    /**
     * @brief 更新代理状态(执行买卖操作)
     * @param houseType 要处理的拍卖行类型
     * @return 如果执行了操作返回true,否则返回false
     *
     * 此方法由主管理器定期调用,代理在此方法中执行具体的买卖逻辑。
     */
    virtual bool Update(AuctionHouseType houseType) = 0;
};

/**
 * @struct AuctionHouseBotStatusInfoPerType
 * @brief 单个拍卖行的状态信息
 *
 * 用于统计和报告拍卖行机器人物品的状态,包括总物品数量和各品质物品数量。
 * 主要用于GM命令查询拍卖行状态。
 */
struct AuctionHouseBotStatusInfoPerType
{
    uint32 ItemsCount;                                          // 机器人上架的物品总数
    std::unordered_map<AuctionQuality, uint32> QualityInfo;     // 各品质物品数量映射
};

/**
 * @class AuctionHouseBot
 * @brief 拍卖行机器人主管理器
 *
 * 单例类,作为拍卖行机器人系统的顶层管理器。
 * 持有买家代理和卖家代理实例,协调它们的操作。
 *
 * 主要职责:
 * - 初始化和管理买家、卖家代理
 * - 定期更新拍卖行状态
 * - 提供GM命令接口(调整比例、重载配置、重建拍卖行等)
 * - 统计拍卖行状态信息
 *
 * 调用时机:
 * - 服务器启动时调用Initialize()
 * - 世界更新循环中定期调用Update()
 * - GM命令通过cs_ahbot.cpp调用各种管理方法
 *
 * 更新机制:
 * - 使用轮转方式依次处理各个拍卖行,避免一次性处理所有拍卖行导致性能问题
 * - _operationSelector用于记录当前处理位置,实现负载均衡
 */
class TC_GAME_API AuctionHouseBot
{
private:
    // 私有构造函数(单例模式)
    AuctionHouseBot();
    ~AuctionHouseBot();
    AuctionHouseBot(AuctionHouseBot const&) = delete;              // 禁用拷贝构造
    AuctionHouseBot& operator=(AuctionHouseBot const&) = delete;   // 禁用赋值操作

public:
    /**
     * @brief 获取单例实例
     * @return AuctionHouseBot指针
     */
    static AuctionHouseBot* instance();

    /**
     * @brief 更新拍卖行状态
     *
     * 定期调用,轮流处理各个拍卖行的卖家和买家操作。
     * 每次调用只处理一个拍卖行的一种操作(卖家或买家),避免性能峰值。
     *
     * 调用时机: 世界更新循环中定期调用
     * 性能注意: 每次调用最多处理一个拍卖行的一个操作,分散负载
     */
    void Update();

    /**
     * @brief 初始化拍卖行机器人
     *
     * 初始化配置管理器,创建买家和卖家代理实例。
     * 如果配置未启用任何功能,则不会创建相应代理。
     */
    void Initialize();

    // GM命令接口 - 主要用于cs_ahbot.cpp中的控制台/游戏内命令

    /**
     * @brief 设置所有拍卖行的物品数量比例
     * @param al 联盟拍卖行比例
     * @param ho 部落拍卖行比例
     * @param ne 中立拍卖行比例
     */
    void SetItemsRatio(uint32 al, uint32 ho, uint32 ne);

    /**
     * @brief 设置指定拍卖行的物品数量比例
     * @param house 拍卖行类型
     * @param val 比例值
     */
    void SetItemsRatioForHouse(AuctionHouseType house, uint32 val);

    /**
     * @brief 设置所有品质的物品目标数量
     * @param amounts 各品质数量的数组
     */
    void SetItemsAmount(std::array<uint32, MAX_AUCTION_QUALITY> const& amounts);

    /**
     * @brief 设置指定品质物品的目标数量
     * @param quality 物品品质
     * @param val 目标数量
     */
    void SetItemsAmountForQuality(AuctionQuality quality, uint32 val);

    /**
     * @brief 重新加载所有配置并重新初始化代理
     */
    void ReloadAllConfig();

    /**
     * @brief 重建拍卖行(清理机器人上架的物品)
     * @param all 如果为true清理所有物品,否则只清理无人竞拍的物品
     */
    void Rebuild(bool all);

    /**
     * @brief 准备拍卖行状态信息
     * @param statusInfo 输出参数,存储各拍卖行的状态统计
     */
    void PrepareStatusInfos(std::unordered_map<AuctionHouseType, AuctionHouseBotStatusInfoPerType>& statusInfo);

private:
    /**
     * @brief 初始化买家和卖家代理
     *
     * 根据配置创建相应的代理实例,如果初始化失败则删除实例。
     */
    void InitializeAgents();

    // 成员变量
    AuctionBotBuyer* _buyer;        // 买家代理指针
    AuctionBotSeller* _seller;      // 卖家代理指针

    uint32 _operationSelector;      // 操作选择器(0..2*MAX_AUCTION_HOUSE_TYPE-1)
                                    // 0-2: 卖家处理三种拍卖行
                                    // 3-5: 买家处理三种拍卖行
};

// 全局访问宏,简化单例访问
#define sAuctionBot AuctionHouseBot::instance()

#endif
