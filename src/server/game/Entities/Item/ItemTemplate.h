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
 * @file ItemTemplate.h
 * @brief 物品模板定义模块
 *
 * 本文件定义了游戏中所有物品的基础数据结构和相关枚举类型。
 *
 * 主要职责：
 *   - 定义物品的各种属性类型枚举（属性类型、触发类型、绑定类型等）
 *   - 定义物品的字段标志位和自定义标志
 *   - 定义背包家族、插槽颜色、装备槽位等物品分类
 *   - 提供物品模板结构体（ItemTemplate），存储物品的静态数据
 *   - 提供物品本地化和套装名称的数据结构
 *
 * 模块关系：
 *   - 被 Item.h/cpp 引用，提供物品模板数据访问
 *   - 被 ObjectMgr 加载和管理物品模板数据
 *   - 被数据库系统读取 item_template 表数据
 *
 * 设计模式：
 *   - 使用结构体存储静态数据，避免虚函数开销
 *   - 使用枚举定义常量，提高代码可读性
 *   - 使用位标志实现高效的标志位操作
 */

#ifndef _ITEMPROTOTYPE_H
#define _ITEMPROTOTYPE_H

#include "Common.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include <vector>

class ObjectMgr;

/**
 * @brief 物品属性修改类型枚举
 *
 * 定义物品可以提供的各种属性修改类型。
 * 用于 ItemTemplate.ItemStat[i].ItemStatType 字段。
 */
enum ItemModType
{
    ITEM_MOD_MANA                     = 0,  ///< 法力值
    ITEM_MOD_HEALTH                   = 1,  ///< 生命值
    ITEM_MOD_AGILITY                  = 3,  ///< 敏捷
    ITEM_MOD_STRENGTH                 = 4,  ///< 力量
    ITEM_MOD_INTELLECT                = 5,  ///< 智力
    ITEM_MOD_SPIRIT                   = 6,  ///< 精神
    ITEM_MOD_STAMINA                  = 7,  ///< 耐力
    ITEM_MOD_DEFENSE_SKILL_RATING     = 12, ///< 防御技能等级
    ITEM_MOD_DODGE_RATING             = 13, ///< 躲闪等级
    ITEM_MOD_PARRY_RATING             = 14, ///< 招架等级
    ITEM_MOD_BLOCK_RATING             = 15, ///< 格挡等级
    ITEM_MOD_HIT_MELEE_RATING         = 16, ///< 近战命中等级
    ITEM_MOD_HIT_RANGED_RATING        = 17, ///< 远程命中等级
    ITEM_MOD_HIT_SPELL_RATING         = 18, ///< 法术命中等级
    ITEM_MOD_CRIT_MELEE_RATING        = 19, ///< 近战暴击等级
    ITEM_MOD_CRIT_RANGED_RATING       = 20, ///< 远程暴击等级
    ITEM_MOD_CRIT_SPELL_RATING        = 21, ///< 法术暴击等级
    ITEM_MOD_HIT_TAKEN_MELEE_RATING   = 22, ///< 近战被命中等级
    ITEM_MOD_HIT_TAKEN_RANGED_RATING  = 23, ///< 远程被命中等级
    ITEM_MOD_HIT_TAKEN_SPELL_RATING   = 24, ///< 法术被命中等级
    ITEM_MOD_CRIT_TAKEN_MELEE_RATING  = 25, ///< 近战被暴击等级
    ITEM_MOD_CRIT_TAKEN_RANGED_RATING = 26, ///< 远程被暴击等级
    ITEM_MOD_CRIT_TAKEN_SPELL_RATING  = 27, ///< 法术被暴击等级
    ITEM_MOD_HASTE_MELEE_RATING       = 28, ///< 近战急速等级
    ITEM_MOD_HASTE_RANGED_RATING      = 29, ///< 远程急速等级
    ITEM_MOD_HASTE_SPELL_RATING       = 30, ///< 法术急速等级
    ITEM_MOD_HIT_RATING               = 31, ///< 通用命中等级
    ITEM_MOD_CRIT_RATING              = 32, ///< 通用暴击等级
    ITEM_MOD_HIT_TAKEN_RATING         = 33, ///< 通用被命中等级
    ITEM_MOD_CRIT_TAKEN_RATING        = 34, ///< 通用被暴击等级
    ITEM_MOD_RESILIENCE_RATING        = 35, ///< 韧性等级
    ITEM_MOD_HASTE_RATING             = 36, ///< 通用急速等级
    ITEM_MOD_EXPERTISE_RATING         = 37, ///< 精准等级
    ITEM_MOD_ATTACK_POWER             = 38, ///< 攻击强度
    ITEM_MOD_RANGED_ATTACK_POWER      = 39, ///< 远程攻击强度
    //ITEM_MOD_FERAL_ATTACK_POWER       = 40, not in 3.3  ///< 野性攻击强度（3.3版本已废弃）
    ITEM_MOD_SPELL_HEALING_DONE       = 41, ///< 法术治疗效果（已废弃）
    ITEM_MOD_SPELL_DAMAGE_DONE        = 42, ///< 法术伤害效果（已废弃）
    ITEM_MOD_MANA_REGENERATION        = 43, ///< 法力回复
    ITEM_MOD_ARMOR_PENETRATION_RATING = 44, ///< 护甲穿透等级
    ITEM_MOD_SPELL_POWER              = 45, ///< 法术强度
    ITEM_MOD_HEALTH_REGEN             = 46, ///< 生命回复
    ITEM_MOD_SPELL_PENETRATION        = 47, ///< 法术穿透
    ITEM_MOD_BLOCK_VALUE              = 48  ///< 格挡值
};

#define MAX_ITEM_MOD                    49  ///< 物品属性类型最大值

/**
 * @brief 物品法术触发类型枚举
 *
 * 定义物品上附带的法术的触发方式。
 * 用于 ItemTemplate.Spells[i].SpellTrigger 字段。
 */
enum ItemSpelltriggerType
{
    ITEM_SPELLTRIGGER_ON_USE          = 0,  ///< 使用时触发（装备后有30秒冷却）
    ITEM_SPELLTRIGGER_ON_EQUIP        = 1,  ///< 装备时触发
    ITEM_SPELLTRIGGER_CHANCE_ON_HIT   = 2,  ///< 命中时有几率触发
    ITEM_SPELLTRIGGER_SOULSTONE       = 4,  ///< 灵魂石类型
    /*
     * ItemSpelltriggerType 5 might have changed on 2.4.3/3.0.3: Such auras
     * will be applied on item pickup and removed on item loss - maybe on the
     * other hand the item is destroyed if the aura is removed ("removed on
     * death" of spell 57348 makes me think so)
     */
    ITEM_SPELLTRIGGER_ON_NO_DELAY_USE = 5,  ///< 使用时触发（无装备冷却）
    ITEM_SPELLTRIGGER_LEARN_SPELL_ID  = 6   ///< 学习法术ID（用于配方类物品）
};

#define MAX_ITEM_SPELLTRIGGER           7   ///< 法术触发类型最大值

/**
 * @brief 物品绑定类型枚举
 *
 * 定义物品的绑定方式。
 * 用于 ItemTemplate.Bonding 字段。
 */
enum ItemBondingType
{
    NO_BIND                                     = 0, ///< 不绑定
    BIND_WHEN_PICKED_UP                         = 1, ///< 拾取时绑定
    BIND_WHEN_EQUIPED                           = 2, ///< 装备时绑定
    BIND_WHEN_USE                               = 3, ///< 使用时绑定
    BIND_QUEST_ITEM                             = 4, ///< 任务物品绑定
    BIND_QUEST_ITEM1                            = 5  ///< 任务物品绑定1（游戏中未使用）
};

#define MAX_BIND_TYPE                             6   ///< 绑定类型最大值

/* /// @todo: Requiring actual cases in which using (an) item isn't allowed while shapeshifted. Else, this flag would need an implementation.
    ITEM_FLAG_USE_WHEN_SHAPESHIFTED    = 0x00800000, // Item can be used in shapeshift forms */

/**
 * @brief 物品字段标志枚举
 *
 * 定义物品在 ITEM_FIELD_FLAGS 字段中存储的标志位。
 * 这些标志表示物品实例的运行时状态。
 */
enum ItemFieldFlags : uint32
{
    ITEM_FIELD_FLAG_SOULBOUND     = 0x00000001, ///< 物品已灵魂绑定，无法交易
    ITEM_FIELD_FLAG_UNK1          = 0x00000002, ///< 未知标志1
    ITEM_FIELD_FLAG_UNLOCKED      = 0x00000004, ///< 物品曾被锁定但现在已解锁
    ITEM_FIELD_FLAG_WRAPPED       = 0x00000008, ///< 物品已包装，包含另一件物品
    ITEM_FIELD_FLAG_UNK2          = 0x00000010, ///< 未知标志2
    ITEM_FIELD_FLAG_UNK3          = 0x00000020, ///< 未知标志3
    ITEM_FIELD_FLAG_UNK4          = 0x00000040, ///< 未知标志4
    ITEM_FIELD_FLAG_UNK5          = 0x00000080, ///< 未知标志5
    ITEM_FIELD_FLAG_BOP_TRADEABLE = 0x00000100, ///< 允许交易灵魂绑定物品（限时交易）
    ITEM_FIELD_FLAG_READABLE      = 0x00000200, ///< 右键点击可打开文本页面
    ITEM_FIELD_FLAG_UNK6          = 0x00000400, ///< 未知标志6
    ITEM_FIELD_FLAG_UNK7          = 0x00000800, ///< 未知标志7
    ITEM_FIELD_FLAG_REFUNDABLE    = 0x00001000, ///< 物品可退还给商贩获取原价（扩展成本）
    ITEM_FIELD_FLAG_UNK8          = 0x00002000, ///< 未知标志8
    ITEM_FIELD_FLAG_UNK9          = 0x00004000, ///< 未知标志9
    ITEM_FIELD_FLAG_UNK10         = 0x00008000, ///< 未知标志10
    ITEM_FIELD_FLAG_UNK11         = 0x00010000, ///< 未知标志11
    ITEM_FIELD_FLAG_UNK12         = 0x00020000, ///< 未知标志12
    ITEM_FIELD_FLAG_UNK13         = 0x00040000, ///< 未知标志13
    ITEM_FIELD_FLAG_UNK14         = 0x00080000, ///< 未知标志14
    ITEM_FIELD_FLAG_UNK15         = 0x00100000, ///< 未知标志15
    ITEM_FIELD_FLAG_UNK16         = 0x00200000, ///< 未知标志16
    ITEM_FIELD_FLAG_UNK17         = 0x00400000, ///< 未知标志17
    ITEM_FIELD_FLAG_UNK18         = 0x00800000, ///< 未知标志18
    ITEM_FIELD_FLAG_UNK19         = 0x01000000, ///< 未知标志19
    ITEM_FIELD_FLAG_UNK20         = 0x02000000, ///< 未知标志20
    ITEM_FIELD_FLAG_UNK21         = 0x04000000, ///< 未知标志21
    ITEM_FIELD_FLAG_UNK22         = 0x08000000, ///< 未知标志22
    ITEM_FIELD_FLAG_UNK23         = 0x10000000, ///< 未知标志23
    ITEM_FIELD_FLAG_UNK24         = 0x20000000, ///< 未知标志24
    ITEM_FIELD_FLAG_UNK25         = 0x40000000, ///< 未知标志25
    ITEM_FIELD_FLAG_UNK26         = 0x80000000, ///< 未知标志26

    ITEM_FLAG_MAIL_TEXT_MASK = ITEM_FIELD_FLAG_READABLE | ITEM_FIELD_FLAG_UNK13 | ITEM_FIELD_FLAG_UNK14 ///< 邮件文本掩码
};

/**
 * @brief 物品模板标志枚举
 *
 * 定义物品模板中的标志位，用于控制物品的各种特性。
 * 这些标志来自数据库，是物品的静态属性。
 */
enum ItemFlags : uint32
{
    ITEM_FLAG_NO_PICKUP                         = 0x00000001, ///< 无法拾取
    ITEM_FLAG_CONJURED                          = 0x00000002, ///< 魔法制造的物品
    ITEM_FLAG_HAS_LOOT                          = 0x00000004, ///< 物品可右键打开获取战利品
    ITEM_FLAG_HEROIC_TOOLTIP                    = 0x00000008, ///< 显示绿色"英雄"文字
    ITEM_FLAG_DEPRECATED                        = 0x00000010, ///< 已废弃，无法装备或使用
    ITEM_FLAG_NO_USER_DESTROY                   = 0x00000020, ///< 用户无法销毁（可作为施法材料）
    ITEM_FLAG_PLAYERCAST                        = 0x00000040, ///< 物品法术可由玩家施放
    ITEM_FLAG_NO_EQUIP_COOLDOWN                 = 0x00000080, ///< 装备时无默认30秒冷却
    ITEM_FLAG_MULTI_LOOT_QUEST                  = 0x00000100, ///< 多人拾取任务物品
    ITEM_FLAG_IS_WRAPPER                        = 0x00000200, ///< 物品可包装其他物品
    ITEM_FLAG_USES_RESOURCES                    = 0x00000400, ///< 使用资源
    ITEM_FLAG_MULTI_DROP                        = 0x00000800, ///< 拾取不会从战利品列表中移除
    ITEM_FLAG_ITEM_PURCHASE_RECORD              = 0x00001000, ///< 可退还给商贩获取原价
    ITEM_FLAG_PETITION                          = 0x00002000, ///< 公会或竞技场申请书
    ITEM_FLAG_HAS_TEXT                          = 0x00004000, ///< 可阅读物品（部分可阅读物品有此标志）
    ITEM_FLAG_NO_DISENCHANT                     = 0x00008000, ///< 无法分解
    ITEM_FLAG_REAL_DURATION                     = 0x00010000, ///< 真实持续时间（离线也计时）
    ITEM_FLAG_NO_CREATOR                        = 0x00020000, ///< 无创建者
    ITEM_FLAG_IS_PROSPECTABLE                   = 0x00040000, ///< 可勘探（矿石类）
    ITEM_FLAG_UNIQUE_EQUIPPABLE                 = 0x00080000, ///< 只能装备一件
    ITEM_FLAG_IGNORE_FOR_AURAS                  = 0x00100000, ///< 光环效果忽略此物品
    ITEM_FLAG_IGNORE_DEFAULT_ARENA_RESTRICTIONS = 0x00200000, ///< 竞技场中可使用
    ITEM_FLAG_NO_DURABILITY_LOSS                = 0x00400000, ///< 无耐久度损耗（部分投掷武器）
    ITEM_FLAG_USE_WHEN_SHAPESHIFTED             = 0x00800000, ///< 变身形态下可使用
    ITEM_FLAG_HAS_QUEST_GLOW                    = 0x01000000, ///< 有任务发光效果
    ITEM_FLAG_HIDE_UNUSABLE_RECIPE              = 0x02000000, ///< 专业配方：只有满足要求且未学习时才能看到
    ITEM_FLAG_NOT_USEABLE_IN_ARENA              = 0x04000000, ///< 竞技场中无法使用
    ITEM_FLAG_IS_BOUND_TO_ACCOUNT               = 0x08000000, ///< 账号绑定，只能发给自己的角色
    ITEM_FLAG_NO_REAGENT_COST                   = 0x10000000, ///< 施法忽略材料消耗
    ITEM_FLAG_IS_MILLABLE                       = 0x20000000, ///< 可研磨（草药类）
    ITEM_FLAG_REPORT_TO_GUILD_CHAT              = 0x40000000, ///< 报告到公会聊天
    ITEM_FLAG_NO_PROGRESSIVE_LOOT               = 0x80000000  ///< 无渐进式战利品
};

/**
 * @brief 物品模板标志2枚举
 *
 * 定义物品模板中的第二组标志位，用于扩展物品特性。
 */
enum ItemFlags2 : uint32
{
    ITEM_FLAG2_FACTION_HORDE                            = 0x00000001, ///< 部落专属
    ITEM_FLAG2_FACTION_ALLIANCE                         = 0x00000002, ///< 联盟专属
    ITEM_FLAG2_DONT_IGNORE_BUY_PRICE                    = 0x00000004, ///< 使用扩展成本时仍需金币
    ITEM_FLAG2_CLASSIFY_AS_CASTER                       = 0x00000008, ///< 分类为法系
    ITEM_FLAG2_CLASSIFY_AS_PHYSICAL                     = 0x00000010, ///< 分类为物理系
    ITEM_FLAG2_EVERYONE_CAN_ROLL_NEED                   = 0x00000020, ///< 所有人都可以贪婪需求
    ITEM_FLAG2_NO_TRADE_BIND_ON_ACQUIRE                 = 0x00000040, ///< 获取绑定不可交易
    ITEM_FLAG2_CAN_TRADE_BIND_ON_ACQUIRE                = 0x00000080, ///< 获取绑定可交易
    ITEM_FLAG2_CAN_ONLY_ROLL_GREED                      = 0x00000100, ///< 只能贪婪
    ITEM_FLAG2_CASTER_WEAPON                            = 0x00000200, ///< 法系武器
    ITEM_FLAG2_DELETE_ON_LOGIN                          = 0x00000400, ///< 登录时删除
    ITEM_FLAG2_INTERNAL_ITEM                            = 0x00000800, ///< 内部物品
    ITEM_FLAG2_NO_VENDOR_VALUE                          = 0x00001000, ///< 无商贩价值
    ITEM_FLAG2_SHOW_BEFORE_DISCOVERED                   = 0x00002000, ///< 发现前显示
    ITEM_FLAG2_OVERRIDE_GOLD_COST                       = 0x00004000, ///< 覆盖金币成本
    ITEM_FLAG2_IGNORE_DEFAULT_RATED_BG_RESTRICTIONS     = 0x00008000, ///< 忽略默认评级战场限制
    ITEM_FLAG2_NOT_USABLE_IN_RATED_BG                   = 0x00010000, ///< 评级战场中不可用
    ITEM_FLAG2_BNET_ACCOUNT_TRADE_OK                    = 0x00020000, ///< 战网账号交易许可
    ITEM_FLAG2_CONFIRM_BEFORE_USE                       = 0x00040000, ///< 使用前确认
    ITEM_FLAG2_REEVALUATE_BONDING_ON_TRANSFORM          = 0x00080000, ///< 变形时重新评估绑定
    ITEM_FLAG2_NO_TRANSFORM_ON_CHARGE_DEPLETION         = 0x00100000, ///< 次数耗尽不变形
    ITEM_FLAG2_NO_ALTER_ITEM_VISUAL                     = 0x00200000, ///< 无物品视觉变化
    ITEM_FLAG2_NO_SOURCE_FOR_ITEM_VISUAL                = 0x00400000, ///< 无物品视觉来源
    ITEM_FLAG2_IGNORE_QUALITY_FOR_ITEM_VISUAL_SOURCE    = 0x00800000, ///< 忽略品质的物品视觉来源
    ITEM_FLAG2_NO_DURABILITY                            = 0x01000000, ///< 无耐久度
    ITEM_FLAG2_ROLE_TANK                                = 0x02000000, ///< 坦克角色
    ITEM_FLAG2_ROLE_HEALER                              = 0x04000000, ///< 治疗角色
    ITEM_FLAG2_ROLE_DAMAGE                              = 0x08000000, ///< 伤害角色
    ITEM_FLAG2_CAN_DROP_IN_CHALLENGE_MODE               = 0x10000000, ///< 挑战模式可掉落
    ITEM_FLAG2_NEVER_STACK_IN_LOOT_UI                   = 0x20000000, ///< 战利品界面永不堆叠
    ITEM_FLAG2_DISENCHANT_TO_LOOT_TABLE                 = 0x40000000, ///< 分解到战利品表
    ITEM_FLAG2_USED_IN_A_TRADESKILL                     = 0x80000000  ///< 用于专业技能
};

/**
 * @brief 物品自定义标志枚举
 *
 * 定义TrinityCore特有的物品标志位，用于服务器端逻辑控制。
 */
enum ItemFlagsCustom
{
    ITEM_FLAGS_CU_DURATION_REAL_TIME    = 0x0001,   ///< 持续时间实时计时（离线也计时）
    ITEM_FLAGS_CU_IGNORE_QUEST_STATUS   = 0x0002,   ///< 掉落时不检查任务状态
    ITEM_FLAGS_CU_FOLLOW_LOOT_RULES     = 0x0004    ///< 遵循队伍/队长/贪婪优先的拾取规则
};

/**
 * @brief 背包家族掩码枚举
 *
 * 定义背包可容纳的物品类型掩码。
 * 用于 ItemTemplate.BagFamily 字段。
 */
enum BAG_FAMILY_MASK
{
    BAG_FAMILY_MASK_NONE                      = 0x00000000, ///< 无限制
    BAG_FAMILY_MASK_ARROWS                    = 0x00000001, ///< 箭矢袋
    BAG_FAMILY_MASK_BULLETS                   = 0x00000002, ///< 子弹袋
    BAG_FAMILY_MASK_SOUL_SHARDS               = 0x00000004, ///< 灵魂碎片袋
    BAG_FAMILY_MASK_LEATHERWORKING_SUPP       = 0x00000008, ///< 制皮材料袋
    BAG_FAMILY_MASK_INSCRIPTION_SUPP          = 0x00000010, ///< 铭文材料袋
    BAG_FAMILY_MASK_HERBS                     = 0x00000020, ///< 草药袋
    BAG_FAMILY_MASK_ENCHANTING_SUPP           = 0x00000040, ///< 附魔材料袋
    BAG_FAMILY_MASK_ENGINEERING_SUPP          = 0x00000080, ///< 工程材料袋
    BAG_FAMILY_MASK_KEYS                      = 0x00000100, ///< 钥匙袋
    BAG_FAMILY_MASK_GEMS                      = 0x00000200, ///< 宝石袋
    BAG_FAMILY_MASK_MINING_SUPP               = 0x00000400, ///< 采矿材料袋
    BAG_FAMILY_MASK_SOULBOUND_EQUIPMENT       = 0x00000800, ///< 灵魂绑定装备袋
    BAG_FAMILY_MASK_VANITY_PETS               = 0x00001000, ///< 宠物袋
    BAG_FAMILY_MASK_CURRENCY_TOKENS           = 0x00002000, ///< 货币代币袋
    BAG_FAMILY_MASK_QUEST_ITEMS               = 0x00004000  ///< 任务物品袋
};

/**
 * @brief 插槽颜色枚举
 *
 * 定义装备插槽的颜色类型。
 * 用于 ItemTemplate.Socket[i].Color 字段。
 */
enum SocketColor
{
    SOCKET_COLOR_META                           = 1,  ///< 变形插槽（红色）
    SOCKET_COLOR_RED                            = 2,  ///< 红色插槽
    SOCKET_COLOR_YELLOW                         = 4,  ///< 黄色插槽
    SOCKET_COLOR_BLUE                           = 8   ///< 蓝色插槽
};

#define SOCKET_COLOR_ALL (SOCKET_COLOR_META | SOCKET_COLOR_RED | SOCKET_COLOR_YELLOW | SOCKET_COLOR_BLUE) ///< 所有插槽颜色掩码

/**
 * @brief 装备槽位类型枚举
 *
 * 定义物品可装备的槽位类型。
 * 用于 ItemTemplate.InventoryType 字段。
 */
enum InventoryType : uint8
{
    INVTYPE_NON_EQUIP                           = 0,  ///< 不可装备
    INVTYPE_HEAD                                = 1,  ///< 头部
    INVTYPE_NECK                                = 2,  ///< 颈部
    INVTYPE_SHOULDERS                           = 3,  ///< 肩部
    INVTYPE_BODY                                = 4,  ///< 衬衣
    INVTYPE_CHEST                               = 5,  ///< 胸部
    INVTYPE_WAIST                               = 6,  ///< 腰部
    INVTYPE_LEGS                                = 7,  ///< 腿部
    INVTYPE_FEET                                = 8,  ///< 脚部
    INVTYPE_WRISTS                              = 9,  ///< 手腕
    INVTYPE_HANDS                               = 10, ///< 手部
    INVTYPE_FINGER                              = 11, ///< 手指
    INVTYPE_TRINKET                             = 12, ///< 饰品
    INVTYPE_WEAPON                              = 13, ///< 单手武器
    INVTYPE_SHIELD                              = 14, ///< 盾牌
    INVTYPE_RANGED                              = 15, ///< 远程武器
    INVTYPE_CLOAK                               = 16, ///< 披风
    INVTYPE_2HWEAPON                            = 17, ///< 双手武器
    INVTYPE_BAG                                 = 18, ///< 背包
    INVTYPE_TABARD                              = 19, ///< 战袍
    INVTYPE_ROBE                                = 20, ///< 长袍
    INVTYPE_WEAPONMAINHAND                      = 21, ///< 主手武器
    INVTYPE_WEAPONOFFHAND                       = 22, ///< 副手武器
    INVTYPE_HOLDABLE                            = 23, ///< 副手物品
    INVTYPE_AMMO                                = 24, ///< 弹药
    INVTYPE_THROWN                              = 25, ///< 投掷武器
    INVTYPE_RANGEDRIGHT                         = 26, ///< 右手远程武器
    INVTYPE_QUIVER                              = 27, ///< 箭袋
    INVTYPE_RELIC                               = 28  ///< 圣物
};

#define MAX_INVTYPE                               29  ///< 装备槽位类型最大值

/**
 * @brief 物品类别枚举
 *
 * 定义物品的主要分类。
 * 用于 ItemTemplate.Class 字段。
 */
enum ItemClass : uint8
{
    ITEM_CLASS_CONSUMABLE                       = 0,  ///< 消耗品
    ITEM_CLASS_CONTAINER                        = 1,  ///< 容器（背包）
    ITEM_CLASS_WEAPON                           = 2,  ///< 武器
    ITEM_CLASS_GEM                              = 3,  ///< 宝石
    ITEM_CLASS_ARMOR                            = 4,  ///< 护甲
    ITEM_CLASS_REAGENT                          = 5,  ///< 材料
    ITEM_CLASS_PROJECTILE                       = 6,  ///< 弹药
    ITEM_CLASS_TRADE_GOODS                      = 7,  ///< 交易物品
    ITEM_CLASS_GENERIC                          = 8,  ///< 通用物品
    ITEM_CLASS_RECIPE                           = 9,  ///< 配方
    ITEM_CLASS_MONEY                            = 10, ///< 金钱（废弃）
    ITEM_CLASS_QUIVER                           = 11, ///< 箭袋
    ITEM_CLASS_QUEST                            = 12, ///< 任务物品
    ITEM_CLASS_KEY                              = 13, ///< 钥匙
    ITEM_CLASS_PERMANENT                        = 14, ///< 永久物品（废弃）
    ITEM_CLASS_MISC                             = 15, ///< 杂项
    ITEM_CLASS_GLYPH                            = 16  ///< 雕文
};

#define MAX_ITEM_CLASS                            17  ///< 物品类别最大值

/**
 * @brief 消耗品子类别枚举
 */
enum ItemSubclassConsumable
{
    ITEM_SUBCLASS_CONSUMABLE                    = 0, ///< 消耗品
    ITEM_SUBCLASS_POTION                        = 1, ///< 药水
    ITEM_SUBCLASS_ELIXIR                        = 2, ///< 药剂
    ITEM_SUBCLASS_FLASK                         = 3, ///< 合剂
    ITEM_SUBCLASS_SCROLL                        = 4, ///< 卷轴
    ITEM_SUBCLASS_FOOD                          = 5, ///< 食物
    ITEM_SUBCLASS_ITEM_ENHANCEMENT              = 6, ///< 物品增强
    ITEM_SUBCLASS_BANDAGE                       = 7, ///< 绷带
    ITEM_SUBCLASS_CONSUMABLE_OTHER              = 8  ///< 其他消耗品
};

#define MAX_ITEM_SUBCLASS_CONSUMABLE              9   ///< 消耗品子类别最大值

/**
 * @brief 容器子类别枚举
 */
enum ItemSubclassContainer
{
    ITEM_SUBCLASS_CONTAINER                     = 0, ///< 普通背包
    ITEM_SUBCLASS_SOUL_CONTAINER                = 1, ///< 灵魂碎片袋
    ITEM_SUBCLASS_HERB_CONTAINER                = 2, ///< 草药袋
    ITEM_SUBCLASS_ENCHANTING_CONTAINER          = 3, ///< 附魔材料袋
    ITEM_SUBCLASS_ENGINEERING_CONTAINER         = 4, ///< 工程材料袋
    ITEM_SUBCLASS_GEM_CONTAINER                 = 5, ///< 宝石袋
    ITEM_SUBCLASS_MINING_CONTAINER              = 6, ///< 矿石袋
    ITEM_SUBCLASS_LEATHERWORKING_CONTAINER      = 7, ///< 制皮材料袋
    ITEM_SUBCLASS_INSCRIPTION_CONTAINER         = 8  ///< 铭文材料袋
};

#define MAX_ITEM_SUBCLASS_CONTAINER               9   ///< 容器子类别最大值

/**
 * @brief 武器子类别枚举
 */
enum ItemSubclassWeapon
{
    ITEM_SUBCLASS_WEAPON_AXE                    = 0,  ///< 单手斧
    ITEM_SUBCLASS_WEAPON_AXE2                   = 1,  ///< 双手斧
    ITEM_SUBCLASS_WEAPON_BOW                    = 2,  ///< 弓
    ITEM_SUBCLASS_WEAPON_GUN                    = 3,  ///< 枪
    ITEM_SUBCLASS_WEAPON_MACE                   = 4,  ///< 单手锤
    ITEM_SUBCLASS_WEAPON_MACE2                  = 5,  ///< 双手锤
    ITEM_SUBCLASS_WEAPON_POLEARM                = 6,  ///< 长柄武器
    ITEM_SUBCLASS_WEAPON_SWORD                  = 7,  ///< 单手剑
    ITEM_SUBCLASS_WEAPON_SWORD2                 = 8,  ///< 双手剑
    ITEM_SUBCLASS_WEAPON_obsolete               = 9,  ///< 已废弃
    ITEM_SUBCLASS_WEAPON_STAFF                  = 10, ///< 法杖
    ITEM_SUBCLASS_WEAPON_EXOTIC                 = 11, ///< 异种武器
    ITEM_SUBCLASS_WEAPON_EXOTIC2                = 12, ///< 异种武器2
    ITEM_SUBCLASS_WEAPON_FIST                   = 13, ///< 拳套
    ITEM_SUBCLASS_WEAPON_MISC                   = 14, ///< 杂项武器
    ITEM_SUBCLASS_WEAPON_DAGGER                 = 15, ///< 匕首
    ITEM_SUBCLASS_WEAPON_THROWN                 = 16, ///< 投掷武器
    ITEM_SUBCLASS_WEAPON_SPEAR                  = 17, ///< 长矛
    ITEM_SUBCLASS_WEAPON_CROSSBOW               = 18, ///< 弩
    ITEM_SUBCLASS_WEAPON_WAND                   = 19, ///< 魔杖
    ITEM_SUBCLASS_WEAPON_FISHING_POLE           = 20  ///< 鱼竿
};

#define ITEM_SUBCLASS_MASK_WEAPON_RANGED (\
    (1 << ITEM_SUBCLASS_WEAPON_BOW) | (1 << ITEM_SUBCLASS_WEAPON_GUN) |\
    (1 << ITEM_SUBCLASS_WEAPON_CROSSBOW) | (1 << ITEM_SUBCLASS_WEAPON_THROWN)) ///< 远程武器掩码

#define MAX_ITEM_SUBCLASS_WEAPON                  21  ///< 武器子类别最大值

/**
 * @brief 宝石子类别枚举
 */
enum ItemSubclassGem
{
    ITEM_SUBCLASS_GEM_RED                       = 0, ///< 红色宝石
    ITEM_SUBCLASS_GEM_BLUE                      = 1, ///< 蓝色宝石
    ITEM_SUBCLASS_GEM_YELLOW                    = 2, ///< 黄色宝石
    ITEM_SUBCLASS_GEM_PURPLE                    = 3, ///< 紫色宝石
    ITEM_SUBCLASS_GEM_GREEN                     = 4, ///< 绿色宝石
    ITEM_SUBCLASS_GEM_ORANGE                    = 5, ///< 橙色宝石
    ITEM_SUBCLASS_GEM_META                      = 6, ///< 变形宝石
    ITEM_SUBCLASS_GEM_SIMPLE                    = 7, ///< 简单宝石
    ITEM_SUBCLASS_GEM_PRISMATIC                 = 8  ///< 棱彩宝石
};

#define MAX_ITEM_SUBCLASS_GEM                     9   ///< 宝石子类别最大值

/**
 * @brief 护甲子类别枚举
 */
enum ItemSubclassArmor
{
    ITEM_SUBCLASS_ARMOR_MISC                    = 0,  ///< 杂项护甲
    ITEM_SUBCLASS_ARMOR_CLOTH                   = 1,  ///< 布甲
    ITEM_SUBCLASS_ARMOR_LEATHER                 = 2,  ///< 皮甲
    ITEM_SUBCLASS_ARMOR_MAIL                    = 3,  ///< 锁甲
    ITEM_SUBCLASS_ARMOR_PLATE                   = 4,  ///< 板甲
    ITEM_SUBCLASS_ARMOR_BUCKLER                 = 5,  ///< 小盾（已废弃）
    ITEM_SUBCLASS_ARMOR_SHIELD                  = 6,  ///< 盾牌
    ITEM_SUBCLASS_ARMOR_LIBRAM                  = 7,  ///< 圣契
    ITEM_SUBCLASS_ARMOR_IDOL                    = 8,  ///< 神像
    ITEM_SUBCLASS_ARMOR_TOTEM                   = 9,  ///< 图腾
    ITEM_SUBCLASS_ARMOR_SIGIL                   = 10  ///< 符印
};

#define MAX_ITEM_SUBCLASS_ARMOR                   11  ///< 护甲子类别最大值

/**
 * @brief 材料子类别枚举
 */
enum ItemSubclassReagent
{
    ITEM_SUBCLASS_REAGENT                       = 0  ///< 材料
};

#define MAX_ITEM_SUBCLASS_REAGENT                 1   ///< 材料子类别最大值

/**
 * @brief 弹药子类别枚举
 */
enum ItemSubclassProjectile
{
    ITEM_SUBCLASS_WAND                          = 0,  ///< 魔杖（废弃）
    ITEM_SUBCLASS_BOLT                          = 1,  ///< 弩箭（废弃）
    ITEM_SUBCLASS_ARROW                         = 2,  ///< 箭矢
    ITEM_SUBCLASS_BULLET                        = 3,  ///< 子弹
    ITEM_SUBCLASS_THROWN                        = 4   ///< 投掷（废弃）
};

#define MAX_ITEM_SUBCLASS_PROJECTILE              5   ///< 弹药子类别最大值

/**
 * @brief 交易物品子类别枚举
 */
enum ItemSubclassTradeGoods
{
    ITEM_SUBCLASS_TRADE_GOODS                   = 0,  ///< 交易物品
    ITEM_SUBCLASS_PARTS                         = 1,  ///< 零件
    ITEM_SUBCLASS_EXPLOSIVES                    = 2,  ///< 爆炸物
    ITEM_SUBCLASS_DEVICES                       = 3,  ///< 装置
    ITEM_SUBCLASS_JEWELCRAFTING                 = 4,  ///< 珠宝加工
    ITEM_SUBCLASS_CLOTH                         = 5,  ///< 布料
    ITEM_SUBCLASS_LEATHER                       = 6,  ///< 皮革
    ITEM_SUBCLASS_METAL_STONE                   = 7,  ///< 金属与石头
    ITEM_SUBCLASS_MEAT                          = 8,  ///< 肉类
    ITEM_SUBCLASS_HERB                          = 9,  ///< 草药
    ITEM_SUBCLASS_ELEMENTAL                     = 10, ///< 元素材料
    ITEM_SUBCLASS_TRADE_GOODS_OTHER             = 11, ///< 其他交易物品
    ITEM_SUBCLASS_ENCHANTING                    = 12, ///< 附魔材料
    ITEM_SUBCLASS_MATERIAL                      = 13, ///< 原材料
    ITEM_SUBCLASS_ARMOR_ENCHANTMENT             = 14, ///< 护甲附魔
    ITEM_SUBCLASS_WEAPON_ENCHANTMENT            = 15  ///< 武器附魔
};

#define MAX_ITEM_SUBCLASS_TRADE_GOODS             16  ///< 交易物品子类别最大值

/**
 * @brief 通用物品子类别枚举
 */
enum ItemSubclassGeneric
{
    ITEM_SUBCLASS_GENERIC                       = 0  ///< 通用物品
};

#define MAX_ITEM_SUBCLASS_GENERIC                 1   ///< 通用物品子类别最大值

/**
 * @brief 配方子类别枚举
 */
enum ItemSubclassRecipe
{
    ITEM_SUBCLASS_BOOK                          = 0,  ///< 书籍
    ITEM_SUBCLASS_LEATHERWORKING_PATTERN        = 1,  ///< 制皮图纸
    ITEM_SUBCLASS_TAILORING_PATTERN             = 2,  ///< 裁缝图纸
    ITEM_SUBCLASS_ENGINEERING_SCHEMATIC         = 3,  ///< 工程图纸
    ITEM_SUBCLASS_BLACKSMITHING                 = 4,  ///< 锻造图纸
    ITEM_SUBCLASS_COOKING_RECIPE                = 5,  ///< 烹饪食谱
    ITEM_SUBCLASS_ALCHEMY_RECIPE                = 6,  ///< 炼金配方
    ITEM_SUBCLASS_FIRST_AID_MANUAL              = 7,  ///< 急救手册
    ITEM_SUBCLASS_ENCHANTING_FORMULA            = 8,  ///< 附魔公式
    ITEM_SUBCLASS_FISHING_MANUAL                = 9,  ///< 钓鱼手册
    ITEM_SUBCLASS_JEWELCRAFTING_RECIPE          = 10  ///< 珠宝配方
};

#define MAX_ITEM_SUBCLASS_RECIPE                  11  ///< 配方子类别最大值

/**
 * @brief 金钱子类别枚举
 */
enum ItemSubclassMoney
{
    ITEM_SUBCLASS_MONEY                         = 0  ///< 金钱
};

#define MAX_ITEM_SUBCLASS_MONEY                   1   ///< 金钱子类别最大值

/**
 * @brief 箭袋子类别枚举
 */
enum ItemSubclassQuiver
{
    ITEM_SUBCLASS_QUIVER0                       = 0,  ///< 箭袋0（废弃）
    ITEM_SUBCLASS_QUIVER1                       = 1,  ///< 箭袋1（废弃）
    ITEM_SUBCLASS_QUIVER                        = 2,  ///< 箭袋
    ITEM_SUBCLASS_AMMO_POUCH                    = 3   ///< 弹药袋
};

#define MAX_ITEM_SUBCLASS_QUIVER                  4   ///< 箭袋子类别最大值

/**
 * @brief 任务物品子类别枚举
 */
enum ItemSubclassQuest
{
    ITEM_SUBCLASS_QUEST                         = 0  ///< 任务物品
};

#define MAX_ITEM_SUBCLASS_QUEST                   1   ///< 任务物品子类别最大值

/**
 * @brief 钥匙子类别枚举
 */
enum ItemSubclassKey
{
    ITEM_SUBCLASS_KEY                           = 0, ///< 钥匙
    ITEM_SUBCLASS_LOCKPICK                      = 1  ///< 开锁器
};

#define MAX_ITEM_SUBCLASS_KEY                     2   ///< 钥匙子类别最大值

/**
 * @brief 永久物品子类别枚举
 */
enum ItemSubclassPermanent
{
    ITEM_SUBCLASS_PERMANENT                     = 0  ///< 永久物品
};

#define MAX_ITEM_SUBCLASS_PERMANENT               1   ///< 永久物品子类别最大值

/**
 * @brief 杂项子类别枚举
 */
enum ItemSubclassJunk
{
    ITEM_SUBCLASS_JUNK                          = 0, ///< 垃圾
    ITEM_SUBCLASS_JUNK_REAGENT                  = 1, ///< 材料垃圾
    ITEM_SUBCLASS_JUNK_PET                      = 2, ///< 宠物垃圾
    ITEM_SUBCLASS_JUNK_HOLIDAY                  = 3, ///< 节日垃圾
    ITEM_SUBCLASS_JUNK_OTHER                    = 4, ///< 其他垃圾
    ITEM_SUBCLASS_JUNK_MOUNT                    = 5  ///< 坐骑垃圾
};

#define MAX_ITEM_SUBCLASS_JUNK                    6   ///< 杂项子类别最大值

/**
 * @brief 雕文子类别枚举
 */
enum ItemSubclassGlyph
{
    ITEM_SUBCLASS_GLYPH_WARRIOR                 = 1,  ///< 战士雕文
    ITEM_SUBCLASS_GLYPH_PALADIN                 = 2,  ///< 圣骑士雕文
    ITEM_SUBCLASS_GLYPH_HUNTER                  = 3,  ///< 猎人雕文
    ITEM_SUBCLASS_GLYPH_ROGUE                   = 4,  ///< 盗贼雕文
    ITEM_SUBCLASS_GLYPH_PRIEST                  = 5,  ///< 牧师雕文
    ITEM_SUBCLASS_GLYPH_DEATH_KNIGHT            = 6,  ///< 死亡骑士雕文
    ITEM_SUBCLASS_GLYPH_SHAMAN                  = 7,  ///< 萨满祭司雕文
    ITEM_SUBCLASS_GLYPH_MAGE                    = 8,  ///< 法师雕文
    ITEM_SUBCLASS_GLYPH_WARLOCK                 = 9,  ///< 术士雕文
    ITEM_SUBCLASS_GLYPH_DRUID                   = 11  ///< 德鲁伊雕文
};

#define MAX_ITEM_SUBCLASS_GLYPH                   12  ///< 雕文子类别最大值

/**
 * @brief 物品子类别最大值数组
 *
 * 用于快速获取各物品类别的子类别数量。
 * 索引为 ItemClass 枚举值。
 */
const uint32 MaxItemSubclassValues[MAX_ITEM_CLASS] =
{
    MAX_ITEM_SUBCLASS_CONSUMABLE,    ///< 消耗品子类别最大值
    MAX_ITEM_SUBCLASS_CONTAINER,     ///< 容器子类别最大值
    MAX_ITEM_SUBCLASS_WEAPON,        ///< 武器子类别最大值
    MAX_ITEM_SUBCLASS_GEM,           ///< 宝石子类别最大值
    MAX_ITEM_SUBCLASS_ARMOR,         ///< 护甲子类别最大值
    MAX_ITEM_SUBCLASS_REAGENT,       ///< 材料子类别最大值
    MAX_ITEM_SUBCLASS_PROJECTILE,    ///< 弹药子类别最大值
    MAX_ITEM_SUBCLASS_TRADE_GOODS,   ///< 交易物品子类别最大值
    MAX_ITEM_SUBCLASS_GENERIC,       ///< 通用物品子类别最大值
    MAX_ITEM_SUBCLASS_RECIPE,        ///< 配方子类别最大值
    MAX_ITEM_SUBCLASS_MONEY,         ///< 金钱子类别最大值
    MAX_ITEM_SUBCLASS_QUIVER,        ///< 箭袋子类别最大值
    MAX_ITEM_SUBCLASS_QUEST,         ///< 任务物品子类别最大值
    MAX_ITEM_SUBCLASS_KEY,           ///< 钥匙子类别最大值
    MAX_ITEM_SUBCLASS_PERMANENT,     ///< 永久物品子类别最大值
    MAX_ITEM_SUBCLASS_JUNK,          ///< 杂项子类别最大值
    MAX_ITEM_SUBCLASS_GLYPH          ///< 雕文子类别最大值
};

#pragma pack(push, 1)

/**
 * @brief 物品伤害结构体
 *
 * 存储武器的伤害范围和伤害类型。
 */
struct _Damage
{
    float   DamageMin = 0.0f;    ///< 最小伤害值
    float   DamageMax = 0.0f;    ///< 最大伤害值
    uint32  DamageType = 0;      ///< 伤害类型（来自 Resistances.dbc）
};

/**
 * @brief 物品属性结构体
 *
 * 存储物品提供的单项属性加成。
 */
struct _ItemStat
{
    uint32  ItemStatType = 0;    ///< 属性类型（见 ItemModType 枚举）
    int32   ItemStatValue = 0;   ///< 属性值
};

/**
 * @brief 物品法术结构体
 *
 * 存储物品附带的法术信息。
 */
struct _Spell
{
    int32 SpellId = 0;               ///< 法术ID（来自 Spell.dbc）
    uint32 SpellTrigger = 0;         ///< 法术触发类型（见 ItemSpelltriggerType 枚举）
    int32  SpellCharges = 0;         ///< 法术使用次数
    float  SpellPPMRate = 0.0f;      ///< 每分钟触发次数（PPM）
    int32  SpellCooldown = -1;       ///< 法术冷却时间（毫秒）
    uint32 SpellCategory = 0;        ///< 法术类别（来自 SpellCategory.dbc）
    int32  SpellCategoryCooldown = -1; ///< 法术类别冷却时间（毫秒）
};

/**
 * @brief 物品插槽结构体
 *
 * 存储装备的宝石插槽信息。
 */
struct _Socket
{
    uint32 Color = 0;    ///< 插槽颜色（见 SocketColor 枚举）
    uint32 Content = 0;  ///< 插槽内容（镶嵌的宝石ID）
};

#pragma pack(pop)

#define MAX_ITEM_PROTO_DAMAGES 2    ///< 物品伤害类型最大数量（3.1.0版本更改）
#define MAX_ITEM_PROTO_SOCKETS 3    ///< 物品插槽最大数量
#define MAX_ITEM_PROTO_SPELLS  5    ///< 物品法术最大数量
#define MAX_ITEM_PROTO_STATS  10    ///< 物品属性最大数量

/**
 * @brief 物品模板结构体
 *
 * 存储物品的静态数据，从数据库 item_template 表加载。
 * 每种物品ID对应一个唯一的 ItemTemplate 实例。
 *
 * 职责：
 *   - 存储物品的基本属性（ID、名称、类别等）
 *   - 存储物品的属性加成和伤害数据
 *   - 存储物品的使用要求和限制
 *   - 提供查询数据包构建功能
 *
 * 性能注意事项：
 *   - 物品模板数据在服务器启动时加载，运行时只读
 *   - QueryData 缓存了预构建的查询数据包，避免重复构建
 */
struct TC_GAME_API ItemTemplate
{
    friend class ObjectMgr;

    // ==================== 基本属性 ====================
    uint32 ItemId;                     ///< 物品ID
    uint32 Class;                      ///< 物品类别（来自 ItemClass.dbc）
    uint32 SubClass;                   ///< 物品子类别（来自 ItemSubClass.dbc）
    int32  SoundOverrideSubclass;      ///< 声音覆盖子类别（< 0时有效，用于覆盖武器声音）
    std::string Name1;                 ///< 物品名称
    uint32 DisplayInfoID;              ///< 显示信息ID（来自 ItemDisplayInfo.dbc）
    uint32 Quality;                    ///< 物品品质
    uint32 Flags;                      ///< 物品标志（见 ItemFlags 枚举）
    uint32 Flags2;                     ///< 物品标志2（见 ItemFlags2 枚举）

    // ==================== 交易属性 ====================
    uint32 BuyCount;                   ///< 商店购买数量
    int32  BuyPrice;                   ///< 商店购买价格
    uint32 SellPrice;                  ///< 商店出售价格

    // ==================== 装备属性 ====================
    uint32 InventoryType;              ///< 装备槽位类型（见 InventoryType 枚举）
    uint32 AllowableClass;             ///< 允许的职业掩码
    uint32 AllowableRace;              ///< 允许的种族掩码
    uint32 ItemLevel;                  ///< 物品等级
    uint32 RequiredLevel;              ///< 需求等级
    uint32 RequiredSkill;              ///< 需求技能ID（来自 SkillLine.dbc）
    uint32 RequiredSkillRank;          ///< 需求技能等级
    uint32 RequiredSpell;              ///< 需求法术ID（来自 Spell.dbc）
    uint32 RequiredHonorRank;          ///< 需求荣誉等级
    uint32 RequiredCityRank;           ///< 需求城市等级
    uint32 RequiredReputationFaction;  ///< 需求声望阵营ID（来自 Faction.dbc）
    uint32 RequiredReputationRank;     ///< 需求声望等级

    // ==================== 数量和容器属性 ====================
    int32  MaxCount;                   ///< 最大拥有数量（<= 0表示无限制）
    int32  Stackable;                  ///< 堆叠数量（0表示不允许堆叠，-1表示放入玩家货币栏）
    uint32 ContainerSlots;             ///< 容器槽位数（背包类物品）

    // ==================== 属性加成 ====================
    uint32 StatsCount;                                         ///< 属性数量
    std::array<_ItemStat, MAX_ITEM_PROTO_STATS> ItemStat;      ///< 属性列表
    uint32 ScalingStatDistribution;                            ///< 缩放属性分配ID（来自 ScalingStatDistribution.dbc）
    uint32 ScalingStatValue;                                   ///< 缩放属性值掩码（用于选择 ScalingStatValues.dbc 列）

    // ==================== 伤害属性 ====================
    std::array<_Damage, MAX_ITEM_PROTO_DAMAGES> Damage;        ///< 伤害列表
    uint32 Armor;                      ///< 护甲值
    uint32 HolyRes;                    ///< 神圣抗性
    uint32 FireRes;                    ///< 火焰抗性
    uint32 NatureRes;                  ///< 自然抗性
    uint32 FrostRes;                   ///< 冰霜抗性
    uint32 ShadowRes;                  ///< 暗影抗性
    uint32 ArcaneRes;                  ///< 奥术抗性
    uint32 Delay;                      ///< 武器攻击速度（毫秒）
    uint32 AmmoType;                   ///< 弹药类型
    float  RangedModRange;             ///< 远程武器射程修正

    // ==================== 法术属性 ====================
    std::array<_Spell, MAX_ITEM_PROTO_SPELLS> Spells;          ///< 法术列表
    uint32 Bonding;                    ///< 绑定类型（见 ItemBondingType 枚举）
    std::string  Description;          ///< 物品描述

    // ==================== 文本和页面属性 ====================
    uint32 PageText;                   ///< 页面文本ID
    uint32 LanguageID;                 ///< 语言ID
    uint32 PageMaterial;               ///< 页面材质

    // ==================== 其他属性 ====================
    uint32 StartQuest;                 ///< 起始任务ID（来自 QuestCache.wdb）
    uint32 LockID;                     ///< 锁ID
    int32  Material;                   ///< 材质ID（来自 Material.dbc，影响声音）
    uint32 Sheath;                     ///< 武器佩戴姿态
    int32  RandomProperty;             ///< 随机属性ID（来自 ItemRandomProperties.dbc）
    int32  RandomSuffix;               ///< 随机后缀ID（来自 ItemRandomSuffix.dbc）
    uint32 Block;                      ///< 格挡值
    uint32 ItemSet;                    ///< 套装ID（来自 ItemSet.dbc）
    uint32 MaxDurability;              ///< 最大耐久度
    uint32 Area;                       ///< 区域ID（来自 AreaTable.dbc）
    uint32 Map;                        ///< 地图ID（来自 Map.dbc）
    uint32 BagFamily;                  ///< 背包家族掩码（来自 ItemBagFamily.dbc）
    uint32 TotemCategory;              ///< 图腾类别ID（来自 TotemCategory.dbc）

    // ==================== 插槽和宝石属性 ====================
    std::array<_Socket, MAX_ITEM_PROTO_SOCKETS> Socket;        ///< 插槽列表
    uint32 socketBonus;                ///< 插槽奖励ID（来自 SpellItemEnchantment.dbc）
    uint32 GemProperties;              ///< 宝石属性ID（来自 GemProperties.dbc）
    uint32 RequiredDisenchantSkill;    ///< 分解所需技能等级
    float  ArmorDamageModifier;        ///< 护甲伤害修正

    // ==================== 持续时间和限制 ====================
    uint32 Duration;                   ///< 持续时间（秒，0表示永久）
    uint32 ItemLimitCategory;          ///< 物品限制类别ID（来自 ItemLimitCategory.dbc）
    uint32 HolidayId;                  ///< 节日ID（来自 Holidays.dbc）

    // ==================== 脚本和战利品属性 ====================
    uint32 ScriptId;                   ///< 脚本ID
    uint32 DisenchantID;               ///< 分解ID
    uint32 FoodType;                   ///< 食物类型
    uint32 MinMoneyLoot;               ///< 最小金币战利品
    uint32 MaxMoneyLoot;               ///< 最大金币战利品
    uint32 FlagsCu;                    ///< 自定义标志（见 ItemFlagsCustom 枚举）

    // ==================== 缓存的查询数据 ====================
    std::array<WorldPacket, TOTAL_LOCALES> QueryData;          ///< 预构建的查询数据包（按语言缓存）

    // ==================== 辅助方法 ====================

    /**
     * @brief 检查物品是否可以在战斗中更换装备状态
     * @return 是否可以更换
     */
    bool CanChangeEquipStateInCombat() const;

    /**
     * @brief 检查物品是否为货币代币
     * @return 是否为货币代币
     */
    bool IsCurrencyToken() const { return (BagFamily & BAG_FAMILY_MASK_CURRENCY_TOKENS) != 0; }

    /**
     * @brief 获取最大堆叠数量
     * @return 最大堆叠数量
     */
    uint32 GetMaxStackSize() const
    {
        return (Stackable == 2147483647 || Stackable <= 0) ? uint32(0x7FFFFFFF-1) : uint32(Stackable);
    }

    /**
     * @brief 获取武器DPS（每秒伤害）
     * @return DPS值
     */
    float getDPS() const;

    /**
     * @brief 获取野性攻击强度加成
     * @param extraDPS 额外DPS
     * @return 野性攻击强度加成
     */
    int32 getFeralBonus(int32 extraDPS = 0) const;

    /**
     * @brief 获取总攻击强度加成
     * @return 总攻击强度加成
     */
    int32 GetTotalAPBonus() const { return _totalAP; }

    /**
     * @brief 获取包含品质修正的物品等级
     * @return 修正后的物品等级
     */
    float GetItemLevelIncludingQuality() const;

    /**
     * @brief 获取物品所需的技能ID
     * @return 技能ID，如果不需要技能则返回0
     */
    uint32 GetSkill() const;

    /**
     * @brief 检查物品是否为药水
     * @return 是否为药水
     */
    bool IsPotion() const { return Class == ITEM_CLASS_CONSUMABLE && SubClass == ITEM_SUBCLASS_POTION; }

    /**
     * @brief 检查物品是否为武器羊皮纸
     * @return 是否为武器羊皮纸
     */
    bool IsWeaponVellum() const { return Class == ITEM_CLASS_TRADE_GOODS && SubClass == ITEM_SUBCLASS_WEAPON_ENCHANTMENT; }

    /**
     * @brief 检查物品是否为护甲羊皮纸
     * @return 是否为护甲羊皮纸
     */
    bool IsArmorVellum() const { return Class == ITEM_CLASS_TRADE_GOODS && SubClass == ITEM_SUBCLASS_ARMOR_ENCHANTMENT; }

    /**
     * @brief 检查物品是否为魔法制造的消耗品
     * @return 是否为魔法制造的消耗品
     */
    bool IsConjuredConsumable() const { return Class == ITEM_CLASS_CONSUMABLE && HasFlag(ITEM_FLAG_CONJURED); }

    /**
     * @brief 检查物品是否可以有签名
     * @return 是否可以有签名
     */
    bool HasSignature() const;

    /**
     * @brief 检查物品是否具有指定标志
     * @param flag 要检查的标志
     * @return 是否具有该标志
     */
    inline bool HasFlag(ItemFlags flag) const { return (Flags & flag) != 0; }

    /**
     * @brief 检查物品是否具有指定标志2
     * @param flag 要检查的标志
     * @return 是否具有该标志
     */
    inline bool HasFlag(ItemFlags2 flag) const { return (Flags2 & flag) != 0; }

    /**
     * @brief 检查物品是否具有指定自定义标志
     * @param customFlag 要检查的自定义标志
     * @return 是否具有该标志
     */
    inline bool HasFlag(ItemFlagsCustom customFlag) const { return (FlagsCu & customFlag) != 0; }

    /**
     * @brief 初始化所有语言的查询数据
     *
     * 在服务器启动时调用，为所有支持的语言预构建查询数据包。
     */
    void InitializeQueryData();

    /**
     * @brief 构建指定语言的查询数据包
     * @param loc 语言常量
     * @return 查询数据包
     */
    WorldPacket BuildQueryData(LocaleConstant loc) const;

private:
    // ==================== 缓存信息 ====================
    int32 _totalAP;                    ///< 缓存的总攻击强度加成

    // ==================== 加载辅助方法 ====================

    /**
     * @brief 加载并计算总攻击强度加成
     *
     * 在物品模板加载后调用，计算并缓存总攻击强度。
     */
    void _LoadTotalAP();
};

/**
 * @brief 物品本地化数据结构体
 *
 * 存储物品名称和描述的多语言翻译。
 */
struct ItemLocale
{
    std::vector<std::string> Name;        ///< 名称翻译（按语言索引）
    std::vector<std::string> Description; ///< 描述翻译（按语言索引）
};

/**
 * @brief 套装名称条目结构体
 *
 * 存储套装物品的名称和装备槽位类型。
 */
struct ItemSetNameEntry
{
    std::string name;        ///< 套装名称
    uint32 InventoryType;    ///< 装备槽位类型
};

/**
 * @brief 套装名称本地化数据结构体
 *
 * 存储套装名称的多语言翻译。
 */
struct ItemSetNameLocale
{
    std::vector<std::string> Name;  ///< 名称翻译（按语言索引）
};

#endif
