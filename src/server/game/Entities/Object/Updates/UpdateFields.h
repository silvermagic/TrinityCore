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
 * @file UpdateFields.h
 * @brief 游戏对象更新字段定义
 *
 * 本文件定义了所有游戏对象的更新字段枚举,用于服务器与客户端之间的状态同步。
 * 这些字段采用偏移量方式组织,支持继承式的字段布局:
 * - Object: 所有对象的基类字段
 * - Item: 继承 Object,物品对象字段
 * - Container: 继承 Item,容器对象字段
 * - Unit: 继承 Object,单位对象字段(Player/Creature)
 * - GameObject: 继承 Object,游戏对象字段
 * - DynamicObject: 继承 Object,动态对象字段
 * - Corpse: 继承 Object,尸体对象字段
 *
 * 字段属性说明:
 * - Size: 字段占用的 32 位字数量
 * - Type: 数据类型(LONG/INT/FLOAT/BYTES/TWO_SHORT)
 * - Flags: 可见性标志(PUBLIC/PRIVATE/OWNER/DYNAMIC 等)
 *
 * @note 此文件为自动生成,对应客户端版本 3.3.5 (12340)
 */

#ifndef _UPDATEFIELDS_AUTO_H
#define _UPDATEFIELDS_AUTO_H

// Auto generated for version 3, 3, 5, 12340

/**
 * @brief 基础对象字段枚举
 *
 * 所有游戏对象的基类字段,定义了对象的基本属性。
 * 所有其他对象类型(Item/Unit/GameObject等)都从这些字段开始继承。
 */
enum EObjectFields
{
    OBJECT_FIELD_GUID                         = 0x0000, ///< 对象的全局唯一标识符 (Size: 2, Type: LONG, Flags: PUBLIC)
    OBJECT_FIELD_TYPE                         = 0x0002, ///< 对象类型掩码,标识对象的具体类型 (Size: 1, Type: INT, Flags: PUBLIC)
    OBJECT_FIELD_ENTRY                        = 0x0003, ///< 模板 ID,对应数据库中的模板条目 (Size: 1, Type: INT, Flags: PUBLIC)
    OBJECT_FIELD_SCALE_X                      = 0x0004, ///< 对象的缩放比例,1.0 为正常大小 (Size: 1, Type: FLOAT, Flags: PUBLIC)
    OBJECT_FIELD_PADDING                      = 0x0005, ///< 对齐填充字段,未使用 (Size: 1, Type: INT, Flags: NONE)
    OBJECT_END                                = 0x0006  ///< Object 字段结束标记,子类从此偏移开始
};

/**
 * @brief 物品对象字段枚举
 *
 * 继承自 Object 字段,定义了所有物品(装备/消耗品等)的公共属性。
 * 包含物品的所有者、附魔、耐久度、堆叠数量等信息。
 */
enum EItemFields
{
    ITEM_FIELD_OWNER                          = OBJECT_END + 0x0000, ///< 物品所有者的 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    ITEM_FIELD_CONTAINED                      = OBJECT_END + 0x0002, ///< 包含此物品的容器 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    ITEM_FIELD_CREATOR                        = OBJECT_END + 0x0004, ///< 创建此物品的玩家 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    ITEM_FIELD_GIFTCREATOR                    = OBJECT_END + 0x0006, ///< 赠送此物品的玩家 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    ITEM_FIELD_STACK_COUNT                    = OBJECT_END + 0x0008, ///< 堆叠数量 (Size: 1, Type: INT, Flags: OWNER, ITEM_OWNER)
    ITEM_FIELD_DURATION                       = OBJECT_END + 0x0009, ///< 物品持续时间(秒),0 表示永久 (Size: 1, Type: INT, Flags: OWNER, ITEM_OWNER)
    ITEM_FIELD_SPELL_CHARGES                  = OBJECT_END + 0x000A, ///< 物品法术充能数量数组[5] (Size: 5, Type: INT, Flags: OWNER, ITEM_OWNER)
    ITEM_FIELD_FLAGS                          = OBJECT_END + 0x000F, ///< 物品标志位 (Size: 1, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_1_1                = OBJECT_END + 0x0010, ///< 附魔槽 1: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_1_3                = OBJECT_END + 0x0012, ///< 附魔槽 1: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_2_1                = OBJECT_END + 0x0013, ///< 附魔槽 2: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_2_3                = OBJECT_END + 0x0015, ///< 附魔槽 2: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_3_1                = OBJECT_END + 0x0016, ///< 附魔槽 3: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_3_3                = OBJECT_END + 0x0018, ///< 附魔槽 3: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_4_1                = OBJECT_END + 0x0019, ///< 附魔槽 4: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_4_3                = OBJECT_END + 0x001B, ///< 附魔槽 4: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_5_1                = OBJECT_END + 0x001C, ///< 附魔槽 5: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_5_3                = OBJECT_END + 0x001E, ///< 附魔槽 5: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_6_1                = OBJECT_END + 0x001F, ///< 附魔槽 6: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_6_3                = OBJECT_END + 0x0021, ///< 附魔槽 6: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_7_1                = OBJECT_END + 0x0022, ///< 附魔槽 7: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_7_3                = OBJECT_END + 0x0024, ///< 附魔槽 7: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_8_1                = OBJECT_END + 0x0025, ///< 附魔槽 8: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_8_3                = OBJECT_END + 0x0027, ///< 附魔槽 8: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_9_1                = OBJECT_END + 0x0028, ///< 附魔槽 9: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_9_3                = OBJECT_END + 0x002A, ///< 附魔槽 9: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_10_1               = OBJECT_END + 0x002B, ///< 附魔槽 10: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_10_3               = OBJECT_END + 0x002D, ///< 附魔槽 10: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_11_1               = OBJECT_END + 0x002E, ///< 附魔槽 11: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_11_3               = OBJECT_END + 0x0030, ///< 附魔槽 11: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_12_1               = OBJECT_END + 0x0031, ///< 附魔槽 12: 附魔 ID (Size: 2, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_ENCHANTMENT_12_3               = OBJECT_END + 0x0033, ///< 附魔槽 12: 持续时间和 charges (Size: 1, Type: TWO_SHORT, Flags: PUBLIC)
    ITEM_FIELD_PROPERTY_SEED                  = OBJECT_END + 0x0034, ///< 属性随机种子,用于生成随机属性 (Size: 1, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_RANDOM_PROPERTIES_ID           = OBJECT_END + 0x0035, ///< 随机属性 ID (Size: 1, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_DURABILITY                     = OBJECT_END + 0x0036, ///< 当前耐久度 (Size: 1, Type: INT, Flags: OWNER, ITEM_OWNER)
    ITEM_FIELD_MAXDURABILITY                  = OBJECT_END + 0x0037, ///< 最大耐久度 (Size: 1, Type: INT, Flags: OWNER, ITEM_OWNER)
    ITEM_FIELD_CREATE_PLAYED_TIME             = OBJECT_END + 0x0038, ///< 创建者的游戏时间 (Size: 1, Type: INT, Flags: PUBLIC)
    ITEM_FIELD_PAD                            = OBJECT_END + 0x0039, ///< 对齐填充字段 (Size: 1, Type: INT, Flags: NONE)
    ITEM_END                                  = OBJECT_END + 0x003A  ///< Item 字段结束标记
};

/**
 * @brief 容器对象字段枚举
 *
 * 继承自 Item 字段,定义了背包/银行袋等容器的属性。
 * 包含容器槽数量和各槽位中的物品 GUID 列表。
 */
enum EContainerFields
{
    CONTAINER_FIELD_NUM_SLOTS                 = ITEM_END + 0x0000, ///< 容器可容纳的物品槽位数量 (Size: 1, Type: INT, Flags: PUBLIC)
    CONTAINER_ALIGN_PAD                       = ITEM_END + 0x0001, ///< 对齐填充字段 (Size: 1, Type: BYTES, Flags: NONE)
    CONTAINER_FIELD_SLOT_1                    = ITEM_END + 0x0002, ///< 容器槽位数组,存储每个槽位中物品的 GUID (Size: 72, Type: LONG, Flags: PUBLIC)
    CONTAINER_END                             = ITEM_END + 0x004A  ///< Container 字段结束标记
};

/**
 * @brief 单位对象字段枚举
 *
 * 继承自 Object 字段,定义了所有单位(Player/Creature/Pet)的公共属性。
 * 包含生命值、法力值、属性、抗性、战斗属性等核心数据。
 *
 * 注意:此枚举同时包含 Player 字段定义,从 UNIT_END 开始延伸。
 */
enum EUnitFields
{
    // ==================== 控制关系字段 ====================
    UNIT_FIELD_CHARM                          = OBJECT_END + 0x0000, ///< 被魅惑目标的 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_SUMMON                         = OBJECT_END + 0x0002, ///< 召唤单位 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_CRITTER                        = OBJECT_END + 0x0004, ///< 小宠物 GUID (Size: 2, Type: LONG, Flags: PRIVATE)
    UNIT_FIELD_CHARMEDBY                      = OBJECT_END + 0x0006, ///< 魅惑者的 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_SUMMONEDBY                     = OBJECT_END + 0x0008, ///< 召唤者的 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_CREATEDBY                      = OBJECT_END + 0x000A, ///< 创建者的 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_TARGET                         = OBJECT_END + 0x000C, ///< 当前目标 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    UNIT_FIELD_CHANNEL_OBJECT                 = OBJECT_END + 0x000E, ///< 引导法术的目标对象 GUID (Size: 2, Type: LONG, Flags: PUBLIC)

    // ==================== 法术和基础属性 ====================
    UNIT_CHANNEL_SPELL                        = OBJECT_END + 0x0010, ///< 正在引导的法术 ID (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_BYTES_0                        = OBJECT_END + 0x0011, ///< 字节数据: 种族/职业/性别/能量类型 (Size: 1, Type: BYTES, Flags: PUBLIC)
    UNIT_FIELD_HEALTH                         = OBJECT_END + 0x0012, ///< 当前生命值 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER1                         = OBJECT_END + 0x0013, ///< 能量值 1: 法力 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER2                         = OBJECT_END + 0x0014, ///< 能量值 2: 怒气 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER3                         = OBJECT_END + 0x0015, ///< 能量值 3: 专注 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER4                         = OBJECT_END + 0x0016, ///< 能量值 4: 能量 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER5                         = OBJECT_END + 0x0017, ///< 能量值 5: 幸福度 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER6                         = OBJECT_END + 0x0018, ///< 能量值 6: 符文 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_POWER7                         = OBJECT_END + 0x0019, ///< 能量值 7: 符文 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXHEALTH                      = OBJECT_END + 0x001A, ///< 最大生命值 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER1                      = OBJECT_END + 0x001B, ///< 最大能量值 1: 法力 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER2                      = OBJECT_END + 0x001C, ///< 最大能量值 2: 怒气 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER3                      = OBJECT_END + 0x001D, ///< 最大能量值 3: 专注 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER4                      = OBJECT_END + 0x001E, ///< 最大能量值 4: 能量 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER5                      = OBJECT_END + 0x001F, ///< 最大能量值 5: 幸福度 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER6                      = OBJECT_END + 0x0020, ///< 最大能量值 6: 符文 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MAXPOWER7                      = OBJECT_END + 0x0021, ///< 最大能量值 7: 符文 (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== 能量回复 ====================
    UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER      = OBJECT_END + 0x0022, ///< 能量回复修正数组[7] (Size: 7, Type: FLOAT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER = OBJECT_END + 0x0029, ///< 受打断的能量回复修正数组[7] (Size: 7, Type: FLOAT, Flags: PRIVATE, OWNER)

    // ==================== 等级和阵营 ====================
    UNIT_FIELD_LEVEL                          = OBJECT_END + 0x0030, ///< 等级 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_FACTIONTEMPLATE                = OBJECT_END + 0x0031, ///< 阵营模板 ID (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== 虚拟物品和标志 ====================
    UNIT_VIRTUAL_ITEM_SLOT_ID                 = OBJECT_END + 0x0032, ///< 虚拟物品槽位数组[3],用于显示装备 (Size: 3, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_FLAGS                          = OBJECT_END + 0x0035, ///< 单位标志位 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_FLAGS_2                        = OBJECT_END + 0x0036, ///< 单位标志位 2 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_AURASTATE                      = OBJECT_END + 0x0037, ///< 光环状态标志 (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== 攻击和碰撞 ====================
    UNIT_FIELD_BASEATTACKTIME                 = OBJECT_END + 0x0038, ///< 基础攻击速度数组[2]: 主手/副手 (Size: 2, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_RANGEDATTACKTIME               = OBJECT_END + 0x003A, ///< 远程攻击速度 (Size: 1, Type: INT, Flags: PRIVATE)
    UNIT_FIELD_BOUNDINGRADIUS                 = OBJECT_END + 0x003B, ///< 碰撞半径 (Size: 1, Type: FLOAT, Flags: PUBLIC)
    UNIT_FIELD_COMBATREACH                    = OBJECT_END + 0x003C, ///< 战斗触及距离 (Size: 1, Type: FLOAT, Flags: PUBLIC)

    // ==================== 显示和坐骑 ====================
    UNIT_FIELD_DISPLAYID                      = OBJECT_END + 0x003D, ///< 当前显示模型 ID (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_NATIVEDISPLAYID                = OBJECT_END + 0x003E, ///< 原始显示模型 ID (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_MOUNTDISPLAYID                 = OBJECT_END + 0x003F, ///< 坐骑显示模型 ID (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== 伤害范围 ====================
    UNIT_FIELD_MINDAMAGE                      = OBJECT_END + 0x0040, ///< 主手最小伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER, PARTY_LEADER)
    UNIT_FIELD_MAXDAMAGE                      = OBJECT_END + 0x0041, ///< 主手最大伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER, PARTY_LEADER)
    UNIT_FIELD_MINOFFHANDDAMAGE               = OBJECT_END + 0x0042, ///< 副手最小伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER, PARTY_LEADER)
    UNIT_FIELD_MAXOFFHANDDAMAGE               = OBJECT_END + 0x0043, ///< 副手最大伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER, PARTY_LEADER)

    // ==================== 字节数据和宠物信息 ====================
    UNIT_FIELD_BYTES_1                        = OBJECT_END + 0x0044, ///< 字节数据: 站立状态/变形形态/可见性/宠物类型 (Size: 1, Type: BYTES, Flags: PUBLIC)
    UNIT_FIELD_PETNUMBER                      = OBJECT_END + 0x0045, ///< 宠物编号 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_PET_NAME_TIMESTAMP             = OBJECT_END + 0x0046, ///< 宠物命名时间戳 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_PETEXPERIENCE                  = OBJECT_END + 0x0047, ///< 宠物经验值 (Size: 1, Type: INT, Flags: OWNER)
    UNIT_FIELD_PETNEXTLEVELEXP                = OBJECT_END + 0x0048, ///< 宠物升级所需经验值 (Size: 1, Type: INT, Flags: OWNER)

    // ==================== 动态标志和施法 ====================
    UNIT_DYNAMIC_FLAGS                        = OBJECT_END + 0x0049, ///< 动态标志位(拾取/标签等) (Size: 1, Type: INT, Flags: DYNAMIC)
    UNIT_MOD_CAST_SPEED                       = OBJECT_END + 0x004A, ///< 施法速度修正(1.0=正常) (Size: 1, Type: FLOAT, Flags: PUBLIC)
    UNIT_CREATED_BY_SPELL                     = OBJECT_END + 0x004B, ///< 创建此单位的法术 ID (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== NPC 属性 ====================
    UNIT_NPC_FLAGS                            = OBJECT_END + 0x004C, ///< NPC 功能标志(商人/任务等) (Size: 1, Type: INT, Flags: DYNAMIC)
    UNIT_NPC_EMOTESTATE                       = OBJECT_END + 0x004D, ///< NPC 表情状态 (Size: 1, Type: INT, Flags: PUBLIC)

    // ==================== 基础属性(力量/敏捷等) ====================
    UNIT_FIELD_STAT0                          = OBJECT_END + 0x004E, ///< 基础属性 0: 力量 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_STAT1                          = OBJECT_END + 0x004F, ///< 基础属性 1: 敏捷 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_STAT2                          = OBJECT_END + 0x0050, ///< 基础属性 2: 耐力 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_STAT3                          = OBJECT_END + 0x0051, ///< 基础属性 3: 智力 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_STAT4                          = OBJECT_END + 0x0052, ///< 基础属性 4: 精神 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)

    // ==================== 属性修正(正面) ====================
    UNIT_FIELD_POSSTAT0                       = OBJECT_END + 0x0053, ///< 力量正面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POSSTAT1                       = OBJECT_END + 0x0054, ///< 敏捷正面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POSSTAT2                       = OBJECT_END + 0x0055, ///< 耐力正面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POSSTAT3                       = OBJECT_END + 0x0056, ///< 智力正面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POSSTAT4                       = OBJECT_END + 0x0057, ///< 精神正面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)

    // ==================== 属性修正(负面) ====================
    UNIT_FIELD_NEGSTAT0                       = OBJECT_END + 0x0058, ///< 力量负面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_NEGSTAT1                       = OBJECT_END + 0x0059, ///< 敏捷负面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_NEGSTAT2                       = OBJECT_END + 0x005A, ///< 耐力负面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_NEGSTAT3                       = OBJECT_END + 0x005B, ///< 智力负面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_NEGSTAT4                       = OBJECT_END + 0x005C, ///< 精神负面修正 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)

    // ==================== 抗性 ====================
    UNIT_FIELD_RESISTANCES                    = OBJECT_END + 0x005D, ///< 抗性数组[7]: 物理/神圣/火焰/自然/冰霜/暗影/奥术 (Size: 7, Type: INT, Flags: PRIVATE, OWNER, PARTY_LEADER)
    UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE     = OBJECT_END + 0x0064, ///< 正面抗性修正数组[7] (Size: 7, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE     = OBJECT_END + 0x006B, ///< 负面抗性修正数组[7] (Size: 7, Type: INT, Flags: PRIVATE, OWNER)

    // ==================== 基础属性 ====================
    UNIT_FIELD_BASE_MANA                      = OBJECT_END + 0x0072, ///< 基础法力值 (Size: 1, Type: INT, Flags: PUBLIC)
    UNIT_FIELD_BASE_HEALTH                    = OBJECT_END + 0x0073, ///< 基础生命值 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_BYTES_2                        = OBJECT_END + 0x0074, ///< 字节数据: 姿态/剪切标志/宠物标志/变形标志 (Size: 1, Type: BYTES, Flags: PUBLIC)

    // ==================== 攻击强度 ====================
    UNIT_FIELD_ATTACK_POWER                   = OBJECT_END + 0x0075, ///< 攻击强度 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_ATTACK_POWER_MODS              = OBJECT_END + 0x0076, ///< 攻击强度修正(基础+临时) (Size: 1, Type: TWO_SHORT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_ATTACK_POWER_MULTIPLIER        = OBJECT_END + 0x0077, ///< 攻击强度乘数 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER)

    // ==================== 远程攻击强度 ====================
    UNIT_FIELD_RANGED_ATTACK_POWER            = OBJECT_END + 0x0078, ///< 远程攻击强度 (Size: 1, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_RANGED_ATTACK_POWER_MODS       = OBJECT_END + 0x0079, ///< 远程攻击强度修正 (Size: 1, Type: TWO_SHORT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER = OBJECT_END + 0x007A, ///< 远程攻击强度乘数 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER)

    // ==================== 远程伤害范围 ====================
    UNIT_FIELD_MINRANGEDDAMAGE                = OBJECT_END + 0x007B, ///< 远程最小伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_MAXRANGEDDAMAGE                = OBJECT_END + 0x007C, ///< 远程最大伤害 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER)

    // ==================== 法术消耗修正 ====================
    UNIT_FIELD_POWER_COST_MODIFIER            = OBJECT_END + 0x007D, ///< 能量消耗修正数组[7] (Size: 7, Type: INT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_POWER_COST_MULTIPLIER          = OBJECT_END + 0x0084, ///< 能量消耗乘数数组[7] (Size: 7, Type: FLOAT, Flags: PRIVATE, OWNER)

    // ==================== 其他修正 ====================
    UNIT_FIELD_MAXHEALTHMODIFIER              = OBJECT_END + 0x008B, ///< 最大生命值修正乘数 (Size: 1, Type: FLOAT, Flags: PRIVATE, OWNER)
    UNIT_FIELD_HOVERHEIGHT                    = OBJECT_END + 0x008C, ///< 悬停高度 (Size: 1, Type: FLOAT, Flags: PUBLIC)
    UNIT_FIELD_PADDING                        = OBJECT_END + 0x008D, ///< 对齐填充字段 (Size: 1, Type: INT, Flags: NONE)
    UNIT_END                                  = OBJECT_END + 0x008E, ///< Unit 字段结束标记(不包含 Player)

    // ==================== Player 特有字段 ====================
    // 以下字段继承自 Unit,仅适用于玩家对象

    PLAYER_DUEL_ARBITER                       = UNIT_END + 0x0000, ///< 决斗仲裁者 GUID (Size: 2, Type: LONG, Flags: PUBLIC)
    PLAYER_FLAGS                              = UNIT_END + 0x0002, ///< 玩家标志位 (Size: 1, Type: INT, Flags: PUBLIC)
    PLAYER_GUILDID                            = UNIT_END + 0x0003, ///< 公会 ID (Size: 1, Type: INT, Flags: PUBLIC)
    PLAYER_GUILDRANK                          = UNIT_END + 0x0004, ///< 公会等级 (Size: 1, Type: INT, Flags: PUBLIC)
    PLAYER_BYTES                              = UNIT_END + 0x0005, ///< 字节数据: 发型/发色/面部/肤色 (Size: 1, Type: BYTES, Flags: PUBLIC)
    PLAYER_BYTES_2                            = UNIT_END + 0x0006, ///< 字节数据: 纹身风格/纹身颜色/面部特征/银行槽位数 (Size: 1, Type: BYTES, Flags: PUBLIC)
    PLAYER_BYTES_3                            = UNIT_END + 0x0007, ///< 字节数据: 性别/醉酒等级/荣誉等级 (Size: 1, Type: BYTES, Flags: PUBLIC)
    PLAYER_DUEL_TEAM                          = UNIT_END + 0x0008, ///< 决斗队伍 (Size: 1, Type: INT, Flags: PUBLIC)
    PLAYER_GUILD_TIMESTAMP                    = UNIT_END + 0x0009, ///< 加入公会的时间戳 (Size: 1, Type: INT, Flags: PUBLIC)
    PLAYER_QUEST_LOG_1_1                      = UNIT_END + 0x000A, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_1_2                      = UNIT_END + 0x000B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_1_3                      = UNIT_END + 0x000C, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_1_4                      = UNIT_END + 0x000E, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_2_1                      = UNIT_END + 0x000F, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_2_2                      = UNIT_END + 0x0010, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_2_3                      = UNIT_END + 0x0011, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_2_5                      = UNIT_END + 0x0013, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_3_1                      = UNIT_END + 0x0014, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_3_2                      = UNIT_END + 0x0015, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_3_3                      = UNIT_END + 0x0016, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_3_5                      = UNIT_END + 0x0018, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_4_1                      = UNIT_END + 0x0019, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_4_2                      = UNIT_END + 0x001A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_4_3                      = UNIT_END + 0x001B, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_4_5                      = UNIT_END + 0x001D, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_5_1                      = UNIT_END + 0x001E, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_5_2                      = UNIT_END + 0x001F, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_5_3                      = UNIT_END + 0x0020, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_5_5                      = UNIT_END + 0x0022, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_6_1                      = UNIT_END + 0x0023, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_6_2                      = UNIT_END + 0x0024, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_6_3                      = UNIT_END + 0x0025, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_6_5                      = UNIT_END + 0x0027, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_7_1                      = UNIT_END + 0x0028, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_7_2                      = UNIT_END + 0x0029, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_7_3                      = UNIT_END + 0x002A, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_7_5                      = UNIT_END + 0x002C, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_8_1                      = UNIT_END + 0x002D, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_8_2                      = UNIT_END + 0x002E, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_8_3                      = UNIT_END + 0x002F, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_8_5                      = UNIT_END + 0x0031, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_9_1                      = UNIT_END + 0x0032, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_9_2                      = UNIT_END + 0x0033, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_9_3                      = UNIT_END + 0x0034, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_9_5                      = UNIT_END + 0x0036, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_10_1                     = UNIT_END + 0x0037, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_10_2                     = UNIT_END + 0x0038, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_10_3                     = UNIT_END + 0x0039, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_10_5                     = UNIT_END + 0x003B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_11_1                     = UNIT_END + 0x003C, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_11_2                     = UNIT_END + 0x003D, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_11_3                     = UNIT_END + 0x003E, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_11_5                     = UNIT_END + 0x0040, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_12_1                     = UNIT_END + 0x0041, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_12_2                     = UNIT_END + 0x0042, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_12_3                     = UNIT_END + 0x0043, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_12_5                     = UNIT_END + 0x0045, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_13_1                     = UNIT_END + 0x0046, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_13_2                     = UNIT_END + 0x0047, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_13_3                     = UNIT_END + 0x0048, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_13_5                     = UNIT_END + 0x004A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_14_1                     = UNIT_END + 0x004B, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_14_2                     = UNIT_END + 0x004C, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_14_3                     = UNIT_END + 0x004D, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_14_5                     = UNIT_END + 0x004F, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_15_1                     = UNIT_END + 0x0050, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_15_2                     = UNIT_END + 0x0051, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_15_3                     = UNIT_END + 0x0052, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_15_5                     = UNIT_END + 0x0054, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_16_1                     = UNIT_END + 0x0055, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_16_2                     = UNIT_END + 0x0056, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_16_3                     = UNIT_END + 0x0057, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_16_5                     = UNIT_END + 0x0059, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_17_1                     = UNIT_END + 0x005A, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_17_2                     = UNIT_END + 0x005B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_17_3                     = UNIT_END + 0x005C, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_17_5                     = UNIT_END + 0x005E, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_18_1                     = UNIT_END + 0x005F, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_18_2                     = UNIT_END + 0x0060, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_18_3                     = UNIT_END + 0x0061, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_18_5                     = UNIT_END + 0x0063, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_19_1                     = UNIT_END + 0x0064, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_19_2                     = UNIT_END + 0x0065, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_19_3                     = UNIT_END + 0x0066, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_19_5                     = UNIT_END + 0x0068, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_20_1                     = UNIT_END + 0x0069, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_20_2                     = UNIT_END + 0x006A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_20_3                     = UNIT_END + 0x006B, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_20_5                     = UNIT_END + 0x006D, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_21_1                     = UNIT_END + 0x006E, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_21_2                     = UNIT_END + 0x006F, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_21_3                     = UNIT_END + 0x0070, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_21_5                     = UNIT_END + 0x0072, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_22_1                     = UNIT_END + 0x0073, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_22_2                     = UNIT_END + 0x0074, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_22_3                     = UNIT_END + 0x0075, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_22_5                     = UNIT_END + 0x0077, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_23_1                     = UNIT_END + 0x0078, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_23_2                     = UNIT_END + 0x0079, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_23_3                     = UNIT_END + 0x007A, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_23_5                     = UNIT_END + 0x007C, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_24_1                     = UNIT_END + 0x007D, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_24_2                     = UNIT_END + 0x007E, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_24_3                     = UNIT_END + 0x007F, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_24_5                     = UNIT_END + 0x0081, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_25_1                     = UNIT_END + 0x0082, // Size: 1, Type: INT, Flags: PARTY_MEMBER
    PLAYER_QUEST_LOG_25_2                     = UNIT_END + 0x0083, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_QUEST_LOG_25_3                     = UNIT_END + 0x0084, // Size: 2, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_QUEST_LOG_25_5                     = UNIT_END + 0x0086, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_VISIBLE_ITEM_1_ENTRYID             = UNIT_END + 0x0087, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_1_ENCHANTMENT         = UNIT_END + 0x0088, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_2_ENTRYID             = UNIT_END + 0x0089, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_2_ENCHANTMENT         = UNIT_END + 0x008A, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_3_ENTRYID             = UNIT_END + 0x008B, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_3_ENCHANTMENT         = UNIT_END + 0x008C, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_4_ENTRYID             = UNIT_END + 0x008D, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_4_ENCHANTMENT         = UNIT_END + 0x008E, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_5_ENTRYID             = UNIT_END + 0x008F, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_5_ENCHANTMENT         = UNIT_END + 0x0090, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_6_ENTRYID             = UNIT_END + 0x0091, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_6_ENCHANTMENT         = UNIT_END + 0x0092, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_7_ENTRYID             = UNIT_END + 0x0093, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_7_ENCHANTMENT         = UNIT_END + 0x0094, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_8_ENTRYID             = UNIT_END + 0x0095, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_8_ENCHANTMENT         = UNIT_END + 0x0096, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_9_ENTRYID             = UNIT_END + 0x0097, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_9_ENCHANTMENT         = UNIT_END + 0x0098, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_10_ENTRYID            = UNIT_END + 0x0099, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_10_ENCHANTMENT        = UNIT_END + 0x009A, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_11_ENTRYID            = UNIT_END + 0x009B, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_11_ENCHANTMENT        = UNIT_END + 0x009C, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_12_ENTRYID            = UNIT_END + 0x009D, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_12_ENCHANTMENT        = UNIT_END + 0x009E, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_13_ENTRYID            = UNIT_END + 0x009F, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_13_ENCHANTMENT        = UNIT_END + 0x00A0, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_14_ENTRYID            = UNIT_END + 0x00A1, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_14_ENCHANTMENT        = UNIT_END + 0x00A2, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_15_ENTRYID            = UNIT_END + 0x00A3, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_15_ENCHANTMENT        = UNIT_END + 0x00A4, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_16_ENTRYID            = UNIT_END + 0x00A5, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_16_ENCHANTMENT        = UNIT_END + 0x00A6, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_17_ENTRYID            = UNIT_END + 0x00A7, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_17_ENCHANTMENT        = UNIT_END + 0x00A8, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_18_ENTRYID            = UNIT_END + 0x00A9, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_18_ENCHANTMENT        = UNIT_END + 0x00AA, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_19_ENTRYID            = UNIT_END + 0x00AB, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_VISIBLE_ITEM_19_ENCHANTMENT        = UNIT_END + 0x00AC, // Size: 1, Type: TWO_SHORT, Flags: PUBLIC
    PLAYER_CHOSEN_TITLE                       = UNIT_END + 0x00AD, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_FAKE_INEBRIATION                   = UNIT_END + 0x00AE, // Size: 1, Type: INT, Flags: PUBLIC
    PLAYER_FIELD_PAD_0                        = UNIT_END + 0x00AF, // Size: 1, Type: INT, Flags: NONE
    PLAYER_FIELD_INV_SLOT_HEAD                = UNIT_END + 0x00B0, // Size: 46, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_PACK_SLOT_1                  = UNIT_END + 0x00DE, // Size: 32, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_BANK_SLOT_1                  = UNIT_END + 0x00FE, // Size: 56, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_BANKBAG_SLOT_1               = UNIT_END + 0x0136, // Size: 14, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_VENDORBUYBACK_SLOT_1         = UNIT_END + 0x0144, // Size: 24, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_KEYRING_SLOT_1               = UNIT_END + 0x015C, // Size: 64, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_CURRENCYTOKEN_SLOT_1         = UNIT_END + 0x019C, // Size: 64, Type: LONG, Flags: PRIVATE
    PLAYER_FARSIGHT                           = UNIT_END + 0x01DC, // Size: 2, Type: LONG, Flags: PRIVATE
    PLAYER__FIELD_KNOWN_TITLES                 = UNIT_END + 0x01DE, // Size: 2, Type: LONG, Flags: PRIVATE
    PLAYER__FIELD_KNOWN_TITLES1                = UNIT_END + 0x01E0, // Size: 2, Type: LONG, Flags: PRIVATE
    PLAYER__FIELD_KNOWN_TITLES2                = UNIT_END + 0x01E2, // Size: 2, Type: LONG, Flags: PRIVATE
    PLAYER_FIELD_KNOWN_CURRENCIES             = UNIT_END + 0x01E4, // Size: 2, Type: LONG, Flags: PRIVATE
    PLAYER_XP                                 = UNIT_END + 0x01E6, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_NEXT_LEVEL_XP                      = UNIT_END + 0x01E7, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_SKILL_INFO_1_1                     = UNIT_END + 0x01E8, // Size: 384, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_CHARACTER_POINTS1                  = UNIT_END + 0x0368, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_CHARACTER_POINTS2                  = UNIT_END + 0x0369, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_TRACK_CREATURES                    = UNIT_END + 0x036A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_TRACK_RESOURCES                    = UNIT_END + 0x036B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_BLOCK_PERCENTAGE                   = UNIT_END + 0x036C, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_DODGE_PERCENTAGE                   = UNIT_END + 0x036D, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_PARRY_PERCENTAGE                   = UNIT_END + 0x036E, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_EXPERTISE                          = UNIT_END + 0x036F, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_OFFHAND_EXPERTISE                  = UNIT_END + 0x0370, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_CRIT_PERCENTAGE                    = UNIT_END + 0x0371, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_RANGED_CRIT_PERCENTAGE             = UNIT_END + 0x0372, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_OFFHAND_CRIT_PERCENTAGE            = UNIT_END + 0x0373, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_SPELL_CRIT_PERCENTAGE1             = UNIT_END + 0x0374, // Size: 7, Type: FLOAT, Flags: PRIVATE
    PLAYER_SHIELD_BLOCK                       = UNIT_END + 0x037B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_SHIELD_BLOCK_CRIT_PERCENTAGE       = UNIT_END + 0x037C, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_EXPLORED_ZONES_1                   = UNIT_END + 0x037D, // Size: 128, Type: BYTES, Flags: PRIVATE
    PLAYER_REST_STATE_EXPERIENCE              = UNIT_END + 0x03FD, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_COINAGE                      = UNIT_END + 0x03FE, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_DAMAGE_DONE_POS          = UNIT_END + 0x03FF, // Size: 7, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_DAMAGE_DONE_NEG          = UNIT_END + 0x0406, // Size: 7, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_DAMAGE_DONE_PCT          = UNIT_END + 0x040D, // Size: 7, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_HEALING_DONE_POS         = UNIT_END + 0x0414, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_HEALING_PCT              = UNIT_END + 0x0415, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_FIELD_MOD_HEALING_DONE_PCT         = UNIT_END + 0x0416, // Size: 1, Type: FLOAT, Flags: PRIVATE
    PLAYER_FIELD_MOD_TARGET_RESISTANCE        = UNIT_END + 0x0417, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE = UNIT_END + 0x0418, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_BYTES                        = UNIT_END + 0x0419, // Size: 1, Type: BYTES, Flags: PRIVATE
    PLAYER_AMMO_ID                            = UNIT_END + 0x041A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_SELF_RES_SPELL                     = UNIT_END + 0x041B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_PVP_MEDALS                   = UNIT_END + 0x041C, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_BUYBACK_PRICE_1              = UNIT_END + 0x041D, // Size: 12, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_BUYBACK_TIMESTAMP_1          = UNIT_END + 0x0429, // Size: 12, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_KILLS                        = UNIT_END + 0x0435, // Size: 1, Type: TWO_SHORT, Flags: PRIVATE
    PLAYER_FIELD_TODAY_CONTRIBUTION           = UNIT_END + 0x0436, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_YESTERDAY_CONTRIBUTION       = UNIT_END + 0x0437, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_LIFETIME_HONORABLE_KILLS     = UNIT_END + 0x0438, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_BYTES2                       = UNIT_END + 0x0439, // Size: 1, Type: 6, Flags: PRIVATE
    PLAYER_FIELD_WATCHED_FACTION_INDEX        = UNIT_END + 0x043A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_COMBAT_RATING_1              = UNIT_END + 0x043B, // Size: 25, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_ARENA_TEAM_INFO_1_1          = UNIT_END + 0x0454, // Size: 21, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_HONOR_CURRENCY               = UNIT_END + 0x0469, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_ARENA_CURRENCY               = UNIT_END + 0x046A, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_MAX_LEVEL                    = UNIT_END + 0x046B, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_DAILY_QUESTS_1               = UNIT_END + 0x046C, // Size: 25, Type: INT, Flags: PRIVATE
    PLAYER_RUNE_REGEN_1                       = UNIT_END + 0x0485, // Size: 4, Type: FLOAT, Flags: PRIVATE
    PLAYER_NO_REAGENT_COST_1                  = UNIT_END + 0x0489, // Size: 3, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_GLYPH_SLOTS_1                = UNIT_END + 0x048C, // Size: 6, Type: INT, Flags: PRIVATE
    PLAYER_FIELD_GLYPHS_1                     = UNIT_END + 0x0492, // Size: 6, Type: INT, Flags: PRIVATE
    PLAYER_GLYPHS_ENABLED                     = UNIT_END + 0x0498, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_PET_SPELL_POWER                    = UNIT_END + 0x0499, // Size: 1, Type: INT, Flags: PRIVATE
    PLAYER_END                                = UNIT_END + 0x049A
};

enum EGameObjectFields
{
    OBJECT_FIELD_CREATED_BY                   = OBJECT_END + 0x0000, // Size: 2, Type: LONG, Flags: PUBLIC
    GAMEOBJECT_DISPLAYID                      = OBJECT_END + 0x0002, // Size: 1, Type: INT, Flags: PUBLIC
    GAMEOBJECT_FLAGS                          = OBJECT_END + 0x0003, // Size: 1, Type: INT, Flags: PUBLIC
    GAMEOBJECT_PARENTROTATION                 = OBJECT_END + 0x0004, // Size: 4, Type: FLOAT, Flags: PUBLIC
    GAMEOBJECT_DYNAMIC                        = OBJECT_END + 0x0008, // Size: 1, Type: TWO_SHORT, Flags: DYNAMIC
    GAMEOBJECT_FACTION                        = OBJECT_END + 0x0009, // Size: 1, Type: INT, Flags: PUBLIC
    GAMEOBJECT_LEVEL                          = OBJECT_END + 0x000A, // Size: 1, Type: INT, Flags: PUBLIC
    GAMEOBJECT_BYTES_1                        = OBJECT_END + 0x000B, // Size: 1, Type: BYTES, Flags: PUBLIC
    GAMEOBJECT_END                            = OBJECT_END + 0x000C
};

enum EDynamicObjectFields
{
    DYNAMICOBJECT_CASTER                      = OBJECT_END + 0x0000, // Size: 2, Type: LONG, Flags: PUBLIC
    DYNAMICOBJECT_BYTES                       = OBJECT_END + 0x0002, // Size: 1, Type: BYTES, Flags: PUBLIC
    DYNAMICOBJECT_SPELLID                     = OBJECT_END + 0x0003, // Size: 1, Type: INT, Flags: PUBLIC
    DYNAMICOBJECT_RADIUS                      = OBJECT_END + 0x0004, // Size: 1, Type: FLOAT, Flags: PUBLIC
    DYNAMICOBJECT_CASTTIME                    = OBJECT_END + 0x0005, // Size: 1, Type: INT, Flags: PUBLIC
    DYNAMICOBJECT_END                         = OBJECT_END + 0x0006
};

enum ECorpseFields
{
    CORPSE_FIELD_OWNER                        = OBJECT_END + 0x0000, // Size: 2, Type: LONG, Flags: PUBLIC
    CORPSE_FIELD_PARTY                        = OBJECT_END + 0x0002, // Size: 2, Type: LONG, Flags: PUBLIC
    CORPSE_FIELD_DISPLAY_ID                   = OBJECT_END + 0x0004, // Size: 1, Type: INT, Flags: PUBLIC
    CORPSE_FIELD_ITEM                         = OBJECT_END + 0x0005, // Size: 19, Type: INT, Flags: PUBLIC
    CORPSE_FIELD_BYTES_1                      = OBJECT_END + 0x0018, // Size: 1, Type: BYTES, Flags: PUBLIC
    CORPSE_FIELD_BYTES_2                      = OBJECT_END + 0x0019, // Size: 1, Type: BYTES, Flags: PUBLIC
    CORPSE_FIELD_GUILD                        = OBJECT_END + 0x001A, // Size: 1, Type: INT, Flags: PUBLIC
    CORPSE_FIELD_FLAGS                        = OBJECT_END + 0x001B, // Size: 1, Type: INT, Flags: PUBLIC
    CORPSE_FIELD_DYNAMIC_FLAGS                = OBJECT_END + 0x001C, // Size: 1, Type: INT, Flags: DYNAMIC
    CORPSE_FIELD_PAD                          = OBJECT_END + 0x001D, // Size: 1, Type: INT, Flags: NONE
    CORPSE_END                                = OBJECT_END + 0x001E
};
#endif
