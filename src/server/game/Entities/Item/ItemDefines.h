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
 * @file ItemDefines.h
 * @brief 物品系统核心定义头文件
 *
 * 本文件定义了物品系统相关的枚举类型，包括：
 * - 背包操作结果码 (InventoryResult)
 * - 商店购买结果码 (BuyResult)
 * - 商店出售结果码 (SellResult)
 * - 物品附魔槽位定义 (EnchantmentSlot)
 *
 * 这些定义被物品管理、交易系统、背包系统等模块广泛使用。
 */

#ifndef ItemDefines_h__
#define ItemDefines_h__

#include "Define.h"

/**
 * @enum InventoryResult
 * @brief 背包/装备操作结果枚举
 *
 * 定义了所有背包和装备操作可能返回的结果码。
 * 这些结果码由服务器发送给客户端，用于显示相应的错误提示信息。
 *
 * @note 客户端根据这些错误码显示本地化的错误消息
 * @note 枚举值对应客户端的错误消息ID
 */
enum InventoryResult : uint8
{
    EQUIP_ERR_OK                                 = 0,  ///< 操作成功，无错误
    EQUIP_ERR_CANT_EQUIP_LEVEL_I                 = 1,  ///< 等级不足，无法装备
    EQUIP_ERR_CANT_EQUIP_SKILL                   = 2,  ///< 技能不足，无法装备
    EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT             = 3,  ///< 物品无法放入该槽位
    EQUIP_ERR_BAG_FULL                           = 4,  ///< 背包已满
    EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG        = 5,  ///< 非空背包无法放入其他背包
    EQUIP_ERR_CANT_TRADE_EQUIP_BAGS              = 6,  ///< 无法交易已装备的背包
    EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE              = 7,  ///< 该槽位只能放置弹药
    EQUIP_ERR_NO_REQUIRED_PROFICIENCY            = 8,  ///< 缺少所需熟练度
    EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE        = 9,  ///< 没有可用的装备槽位
    EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM        = 10, ///< 永远无法使用该物品
    EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2       = 11, ///< 永远无法使用该物品(重复)
    EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2       = 12, ///< 没有可用的装备槽位(重复)
    EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED          = 13, ///< 无法装备双手武器(已有物品)
    EQUIP_ERR_CANT_DUAL_WIELD                    = 14, ///< 无法双持
    EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG            = 15, ///< 物品无法放入该背包
    EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2           = 16, ///< 物品无法放入该背包(重复)
    EQUIP_ERR_CANT_CARRY_MORE_OF_THIS            = 17, ///< 无法携带更多此类物品
    EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3       = 18, ///< 没有可用的装备槽位(重复)
    EQUIP_ERR_ITEM_CANT_STACK                    = 19, ///< 物品无法堆叠
    EQUIP_ERR_ITEM_CANT_BE_EQUIPPED              = 20, ///< 物品无法被装备
    EQUIP_ERR_ITEMS_CANT_BE_SWAPPED              = 21, ///< 物品无法交换
    EQUIP_ERR_SLOT_IS_EMPTY                      = 22, ///< 槽位为空
    EQUIP_ERR_ITEM_NOT_FOUND                     = 23, ///< 物品未找到
    EQUIP_ERR_CANT_DROP_SOULBOUND                = 24, ///< 无法丢弃灵魂绑定物品
    EQUIP_ERR_OUT_OF_RANGE                       = 25, ///< 超出范围
    EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT     = 26, ///< 尝试拆分数量超过物品数量
    EQUIP_ERR_COULDNT_SPLIT_ITEMS                = 27, ///< 无法拆分物品
    EQUIP_ERR_MISSING_REAGENT                    = 28, ///< 缺少材料
    EQUIP_ERR_NOT_ENOUGH_MONEY                   = 29, ///< 金币不足
    EQUIP_ERR_NOT_A_BAG                          = 30, ///< 不是背包
    EQUIP_ERR_CAN_ONLY_DO_WITH_EMPTY_BAGS        = 31, ///< 只能对空背包执行此操作
    EQUIP_ERR_DONT_OWN_THAT_ITEM                 = 32, ///< 不拥有该物品
    EQUIP_ERR_CAN_EQUIP_ONLY1_QUIVER             = 33, ///< 只能装备一个箭袋
    EQUIP_ERR_MUST_PURCHASE_THAT_BAG_SLOT        = 34, ///< 必须购买该背包槽位
    EQUIP_ERR_TOO_FAR_AWAY_FROM_BANK             = 35, ///< 距离银行太远
    EQUIP_ERR_ITEM_LOCKED                        = 36, ///< 物品已锁定
    EQUIP_ERR_YOU_ARE_STUNNED                    = 37, ///< 处于昏迷状态
    EQUIP_ERR_YOU_ARE_DEAD                       = 38, ///< 已死亡
    EQUIP_ERR_CANT_DO_RIGHT_NOW                  = 39, ///< 当前无法执行
    EQUIP_ERR_INT_BAG_ERROR                      = 40, ///< 内部背包错误
    EQUIP_ERR_CAN_EQUIP_ONLY1_BOLT               = 41, ///< 只能装备一个弹药袋
    EQUIP_ERR_CAN_EQUIP_ONLY1_AMMOPOUCH          = 42, ///< 只能装备一个箭袋
    EQUIP_ERR_STACKABLE_CANT_BE_WRAPPED          = 43, ///< 可堆叠物品无法被包装
    EQUIP_ERR_EQUIPPED_CANT_BE_WRAPPED           = 44, ///< 已装备物品无法被包装
    EQUIP_ERR_WRAPPED_CANT_BE_WRAPPED            = 45, ///< 已包装物品无法再次包装
    EQUIP_ERR_BOUND_CANT_BE_WRAPPED              = 46, ///< 绑定物品无法被包装
    EQUIP_ERR_UNIQUE_CANT_BE_WRAPPED             = 47, ///< 唯一物品无法被包装
    EQUIP_ERR_BAGS_CANT_BE_WRAPPED               = 48, ///< 背包无法被包装
    EQUIP_ERR_ALREADY_LOOTED                     = 49, ///< 已被拾取
    EQUIP_ERR_INVENTORY_FULL                     = 50, ///< 背包已满
    EQUIP_ERR_BANK_FULL                          = 51, ///< 银行已满
    EQUIP_ERR_ITEM_IS_CURRENTLY_SOLD_OUT         = 52, ///< 物品已售罄
    EQUIP_ERR_BAG_FULL3                          = 53, ///< 背包已满(重复)
    EQUIP_ERR_ITEM_NOT_FOUND2                    = 54, ///< 物品未找到(重复)
    EQUIP_ERR_ITEM_CANT_STACK2                   = 55, ///< 物品无法堆叠(重复)
    EQUIP_ERR_BAG_FULL4                          = 56, ///< 背包已满(重复)
    EQUIP_ERR_ITEM_SOLD_OUT                      = 57, ///< 物品已售罄(重复)
    EQUIP_ERR_OBJECT_IS_BUSY                     = 58, ///< 对象繁忙
    EQUIP_ERR_NONE                               = 59, ///< 无错误(保留)
    EQUIP_ERR_NOT_IN_COMBAT                      = 60, ///< 不在战斗中
    EQUIP_ERR_NOT_WHILE_DISARMED                 = 61, ///< 缴械状态下无法执行
    EQUIP_ERR_BAG_FULL6                          = 62, ///< 背包已满(重复)
    EQUIP_ERR_CANT_EQUIP_RANK                    = 63, ///< 军衔不足，无法装备
    EQUIP_ERR_CANT_EQUIP_REPUTATION              = 64, ///< 声望不足，无法装备
    EQUIP_ERR_TOO_MANY_SPECIAL_BAGS              = 65, ///< 特殊背包数量过多
    EQUIP_ERR_LOOT_CANT_LOOT_THAT_NOW            = 66, ///< 当前无法拾取该物品
    EQUIP_ERR_ITEM_UNIQUE_EQUIPABLE              = 67, ///< 唯一装备物品限制
    EQUIP_ERR_VENDOR_MISSING_TURNINS             = 68, ///< 商店缺少所需物品
    EQUIP_ERR_NOT_ENOUGH_HONOR_POINTS            = 69, ///< 荣誉点数不足
    EQUIP_ERR_NOT_ENOUGH_ARENA_POINTS            = 70, ///< 竞技场点数不足
    EQUIP_ERR_ITEM_MAX_COUNT_SOCKETED            = 71, ///< 镶嵌物品达到最大数量
    EQUIP_ERR_MAIL_BOUND_ITEM                    = 72, ///< 邮件绑定物品
    EQUIP_ERR_NO_SPLIT_WHILE_PROSPECTING         = 73, ///< 探矿时无法拆分
    EQUIP_ERR_ITEM_MAX_COUNT_EQUIPPED_SOCKETED   = 75, ///< 装备镶嵌达到最大数量
    EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE_SOCKETED    = 76, ///< 镶嵌唯一装备限制
    EQUIP_ERR_TOO_MUCH_GOLD                      = 77, ///< 金币过多
    EQUIP_ERR_NOT_DURING_ARENA_MATCH             = 78, ///< 竞技场比赛中无法执行
    EQUIP_ERR_CANNOT_TRADE_THAT                  = 79, ///< 无法交易该物品
    EQUIP_ERR_PERSONAL_ARENA_RATING_TOO_LOW      = 80, ///< 个人竞技场等级过低
    EQUIP_ERR_EVENT_AUTOEQUIP_BIND_CONFIRM       = 81, ///< 自动装备绑定确认事件
    EQUIP_ERR_ARTEFACTS_ONLY_FOR_OWN_CHARACTERS  = 82, ///< 神器只能由角色自己使用
    // no output                                 = 83, ///< 无输出(保留)
    EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_COUNT_EXCEEDED     = 84, ///< 物品类别数量限制超出
    EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_SOCKETED_EXCEEDED  = 85, ///< 镶嵌物品类别限制超出
    EQUIP_ERR_SCALING_STAT_ITEM_LEVEL_EXCEEDED           = 86, ///< 装备等级超出限制
    EQUIP_ERR_PURCHASE_LEVEL_TOO_LOW                     = 87, ///< 购买等级过低
    EQUIP_ERR_CANT_EQUIP_NEED_TALENT                     = 88, ///< 需要天赋才能装备
    EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_EQUIPPED_EXCEEDED  = 89  ///< 装备物品类别限制超出
};

/**
 * @enum BuyResult
 * @brief 商店购买操作结果枚举
 *
 * 定义了从NPC商店购买物品时可能返回的结果码。
 * 由购物系统使用，用于向玩家反馈购买失败原因。
 *
 * @note 购买失败时，服务器会发送相应的错误消息给客户端
 */
enum BuyResult
{
    BUY_ERR_CANT_FIND_ITEM                      = 0,  ///< 未找到物品
    BUY_ERR_ITEM_ALREADY_SOLD                   = 1,  ///< 物品已售出
    BUY_ERR_NOT_ENOUGHT_MONEY                   = 2,  ///< 金币不足
    BUY_ERR_SELLER_DONT_LIKE_YOU                = 4,  ///< 商贩不喜欢你
    BUY_ERR_DISTANCE_TOO_FAR                    = 5,  ///< 距离过远
    BUY_ERR_ITEM_SOLD_OUT                       = 7,  ///< 物品已售罄
    BUY_ERR_CANT_CARRY_MORE                     = 8,  ///< 无法携带更多物品
    BUY_ERR_RANK_REQUIRE                        = 11, ///< 需要军衔
    BUY_ERR_REPUTATION_REQUIRE                  = 12  ///< 需要声望
};

/**
 * @enum SellResult
 * @brief 商店出售操作结果枚举
 *
 * 定义了向NPC商店出售物品时可能返回的结果码。
 * 由购物系统使用，用于向玩家反馈出售失败原因。
 */
enum SellResult
{
    SELL_ERR_CANT_FIND_ITEM                      = 1,  ///< 未找到物品
    SELL_ERR_CANT_SELL_ITEM                      = 2,  ///< 商贩不想要该物品
    SELL_ERR_CANT_FIND_VENDOR                    = 3,  ///< 商贩不喜欢你
    SELL_ERR_YOU_DONT_OWN_THAT_ITEM              = 4,  ///< 你不拥有该物品
    SELL_ERR_UNK                                 = 5,  ///< 未知错误(无提示)
    SELL_ERR_ONLY_EMPTY_BAG                      = 6,  ///< 只能出售空背包
    SELL_ERR_CANT_SELL_TO_THIS_MERCHANT          = 7   ///< 无法向此商贩出售物品
};

/**
 * @enum EnchantmentSlot
 * @brief 物品附魔槽位枚举
 *
 * 定义了物品上各种附魔槽位的位置索引。
 * 物品可以拥有多个附魔槽位，用于存储不同类型的附魔效果。
 *
 * @note 客户端槽位编号 = 服务器槽位编号 + 1
 * @note 不同的槽位有不同持久性和显示特性
 *
 * 槽位分类：
 * - PERM_ENCHANTMENT_SLOT: 永久附魔，持久有效
 * - TEMP_ENCHANTMENT_SLOT: 临时附魔，如磨刀石、武器油等
 * - SOCK_ENCHANTMENT_SLOT: 宝石镶嵌槽位
 * - BONUS_ENCHANTMENT_SLOT: 奖励附魔槽位
 * - PRISMATIC_ENCHANTMENT_SLOT: 棱彩附魔槽位
 * - PROP_ENCHANTMENT_SLOT: 随机属性/后缀附魔槽位
 */
enum EnchantmentSlot : uint16
{
    PERM_ENCHANTMENT_SLOT           = 0,  ///< 永久附魔槽位(如武器附魔)
    TEMP_ENCHANTMENT_SLOT           = 1,  ///< 临时附魔槽位(如磨刀石、武器油)
    SOCK_ENCHANTMENT_SLOT           = 2,  ///< 宝石镶嵌槽位1
    SOCK_ENCHANTMENT_SLOT_2         = 3,  ///< 宝石镶嵌槽位2
    SOCK_ENCHANTMENT_SLOT_3         = 4,  ///< 宝石镶嵌槽位3
    BONUS_ENCHANTMENT_SLOT          = 5,  ///< 奖励附魔槽位(如套装奖励)
    PRISMATIC_ENCHANTMENT_SLOT      = 6,  ///< 棱彩附魔槽位(特殊永久附魔)
    MAX_INSPECTED_ENCHANTMENT_SLOT  = 7,  ///< 检查时可见的最大附魔槽位数量

    PROP_ENCHANTMENT_SLOT_0         = 7,  ///< 随机后缀属性槽位0
    PROP_ENCHANTMENT_SLOT_1         = 8,  ///< 随机后缀属性槽位1
    PROP_ENCHANTMENT_SLOT_2         = 9,  ///< 随机后缀/随机属性槽位2
    PROP_ENCHANTMENT_SLOT_3         = 10, ///< 随机属性槽位3
    PROP_ENCHANTMENT_SLOT_4         = 11, ///< 随机属性槽位4
    MAX_ENCHANTMENT_SLOT            = 12  ///< 附魔槽位总数
};

/**
 * @def MAX_VISIBLE_ITEM_OFFSET
 * @brief 可见物品字段偏移量
 *
 * 每个可见物品占用2个字段：
 * 1. 物品模板ID (entry)
 * 2. 附魔外观ID (enchantment)
 *
 * 用于玩家外观数据更新时的字段计算。
 */
#define MAX_VISIBLE_ITEM_OFFSET       2

#endif // ItemDefines_h__
