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
 * @file UpdateFieldFlags.cpp
 * @brief 更新字段标志定义模块
 *
 * 本文件定义了各类游戏对象的更新字段访问权限标志数组。这些标志控制着
 * 游戏对象属性在网络同步过程中的可见性范围，决定哪些字段对哪些客户端可见。
 *
 * 核心概念：
 * - 更新字段(Update Field)：游戏对象的属性数据，需要同步给客户端
 * - 可见性标志：控制字段对特定观察者的可见性（公开/私有/所有者专属等）
 * - 权限分层：PUBLIC(所有人) > PRIVATE(仅自己) > OWNER(所有者) > ITEM_OWNER(物品所有者)
 *
 * 本文件包含以下对象的字段标志定义：
 * - Item/Container：物品和容器对象
 * - Unit/Player：单位和玩家对象
 * - GameObject：游戏对象（箱子、门等）
 * - DynamicObject：动态对象（法术效果区域）
 * - Corpse：尸体对象
 *
 * 使用场景：
 * 当服务器向客户端发送对象创建/更新数据包时，会根据观察者的身份
 * 和字段的权限标志决定是否包含该字段数据，实现数据权限控制。
 *
 * @see UpdateFieldFlags.h 标志常量定义
 * @see UpdateData 更新数据构建器
 * @see Object::BuildCreateUpdateBlock 构建更新数据块
 */

#include "UpdateFieldFlags.h"

/**
 * @brief 物品和容器更新字段权限标志数组
 *
 * 定义了物品(Item)和容器(Container)类型对象的所有更新字段的访问权限。
 * 数组索引对应字段枚举值(UpdateFields.h 中的 ITEM_FIELD_* 和 CONTAINER_FIELD_* 常量)。
 *
 * 权限标志说明：
 * - UF_FLAG_PUBLIC：对所有观察者公开（如物品外观、附魔效果）
 * - UF_FLAG_PRIVATE：仅对自己可见
 * - UF_FLAG_OWNER：仅对象所有者可见
 * - UF_FLAG_ITEM_OWNER：仅物品持有者可见（如耐久度、充能数）
 * - UF_FLAG_NONE：不发送给客户端（填充字段）
 *
 * 数组大小：CONTAINER_END，涵盖从 OBJECT_FIELD_GUID 到 CONTAINER_FIELD_SLOT_1+71
 * 所有物品和容器共享此标志数组。
 *
 * @note 耐久度和充能数等敏感数据使用 UF_FLAG_ITEM_OWNER 保护，
 *       防止其他玩家查看物品的详细私有属性
 */
uint32 ItemUpdateFieldFlags[CONTAINER_END] =
{
    // ========== 对象基础字段 (Object 基类) ==========
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID：对象GUID，所有客户端可见
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID+1：GUID的高32位
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_TYPE：对象类型标识（TypeMask）
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_ENTRY：对象模板ID（entry）
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_SCALE_X：对象缩放比例
    UF_FLAG_NONE,                                           // OBJECT_FIELD_PADDING：填充字段，不发送

    // ========== 物品字段 (Item 类) ==========
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_OWNER：物品所有者GUID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_OWNER+1：所有者GUID高位
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_CONTAINED：容器GUID（所在背包）
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_CONTAINED+1：容器GUID高位
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_CREATOR：创建者GUID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_CREATOR+1：创建者GUID高位
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_GIFTCREATOR：赠送者GUID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_GIFTCREATOR+1：赠送者GUID高位
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_STACK_COUNT：堆叠数量（仅所有者可见）
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_DURATION：持续时间（仅所有者可见）
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_SPELL_CHARGES：法术充能[0]（仅所有者可见）
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_SPELL_CHARGES+1：法术充能[1]
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_SPELL_CHARGES+2：法术充能[2]
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_SPELL_CHARGES+3：法术充能[3]
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_SPELL_CHARGES+4：法术充能[4]
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_FLAGS：物品标志（如已装备、已附魔等）

    // ========== 附魔字段组（12组附魔槽位，每组3个字段） ==========
    // 附魔数据结构：[附魔ID, 持续时间, 充能数]
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_1_1：附魔槽1 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_1_1+1：附魔槽1 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_1_3：附魔槽1 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_2_1：附魔槽2 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_2_1+1：附魔槽2 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_2_3：附魔槽2 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_3_1：附魔槽3 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_3_1+1：附魔槽3 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_3_3：附魔槽3 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_4_1：附魔槽4 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_4_1+1：附魔槽4 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_4_3：附魔槽4 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_5_1：附魔槽5 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_5_1+1：附魔槽5 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_5_3：附魔槽5 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_6_1：附魔槽6 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_6_1+1：附魔槽6 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_6_3：附魔槽6 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_7_1：附魔槽7 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_7_1+1：附魔槽7 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_7_3：附魔槽7 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_8_1：附魔槽8 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_8_1+1：附魔槽8 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_8_3：附魔槽8 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_9_1：附魔槽9 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_9_1+1：附魔槽9 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_9_3：附魔槽9 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_10_1：附魔槽10 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_10_1+1：附魔槽10 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_10_3：附魔槽10 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_11_1：附魔槽11 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_11_1+1：附魔槽11 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_11_3：附魔槽11 - 充能数
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_12_1：附魔槽12 - 附魔ID
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_12_1+1：附魔槽12 - 持续时间
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_ENCHANTMENT_12_3：附魔槽12 - 充能数

    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_PROPERTY_SEED：属性种子（用于随机属性）
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_RANDOM_PROPERTIES_ID：随机属性ID
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_DURABILITY：当前耐久度（仅所有者可见）
    UF_FLAG_OWNER | UF_FLAG_ITEM_OWNER,                     // ITEM_FIELD_MAXDURABILITY：最大耐久度（仅所有者可见）
    UF_FLAG_PUBLIC,                                         // ITEM_FIELD_CREATE_PLAYED_TIME：创建时的游戏时间
    UF_FLAG_NONE,                                           // ITEM_FIELD_PAD：填充字段，不发送

    // ========== 容器字段 (Container 类) ==========
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_NUM_SLOTS：容器槽位数量（背包格数）
    UF_FLAG_NONE,                                           // CONTAINER_ALIGN_PAD：对齐填充字段，不发送

    // 容器槽位数组：每个槽位存储物品GUID（72个槽位 = 最大背包容量）
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1：槽位0 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+1：槽位0 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+2：槽位1 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+3：槽位1 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+4：槽位2 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+5：槽位2 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+6：槽位3 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+7：槽位3 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+8：槽位4 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+9：槽位4 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+10：槽位5 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+11：槽位5 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+12：槽位6 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+13：槽位6 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+14：槽位7 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+15：槽位7 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+16：槽位8 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+17：槽位8 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+18：槽位9 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+19：槽位9 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+20：槽位10 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+21：槽位10 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+22：槽位11 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+23：槽位11 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+24：槽位12 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+25：槽位12 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+26：槽位13 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+27：槽位13 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+28：槽位14 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+29：槽位14 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+30：槽位15 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+31：槽位15 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+32：槽位16 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+33：槽位16 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+34：槽位17 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+35：槽位17 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+36：槽位18 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+37：槽位18 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+38：槽位19 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+39：槽位19 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+40：槽位20 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+41：槽位20 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+42：槽位21 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+43：槽位21 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+44：槽位22 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+45：槽位22 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+46：槽位23 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+47：槽位23 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+48：槽位24 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+49：槽位24 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+50：槽位25 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+51：槽位25 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+52：槽位26 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+53：槽位26 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+54：槽位27 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+55：槽位27 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+56：槽位28 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+57：槽位28 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+58：槽位29 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+59：槽位29 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+60：槽位30 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+61：槽位30 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+62：槽位31 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+63：槽位31 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+64：槽位32 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+65：槽位32 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+66：槽位33 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+67：槽位33 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+68：槽位34 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+69：槽位34 - 物品GUID高位
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+70：槽位35 - 物品GUID
    UF_FLAG_PUBLIC,                                         // CONTAINER_FIELD_SLOT_1+71：槽位35 - 物品GUID高位
};

/**
 * @brief 单位（Unit）和玩家（Player）更新字段权限标志数组
 *
 * 定义了单位(Unit)和玩家(Player)类型对象的所有更新字段的访问权限。
 * 数组索引对应字段枚举值(UpdateFields.h 中的 UNIT_FIELD_* 和 PLAYER_FIELD_* 常量)。
 *
 * 权限标志说明：
 * - UF_FLAG_PUBLIC：对所有观察者公开（如生命值、等级、外观）
 * - UF_FLAG_PRIVATE：仅对自己可见（如小宠物GUID）
 * - UF_FLAG_OWNER：仅对象所有者可见（如召唤者的私有数据）
 * - UF_FLAG_DYNAMIC：动态标志，根据上下文决定可见性
 * - UF_FLAG_GROUP_ONLY：仅队伍成员可见（如队友的详细状态）
 * - UF_FLAG_NONE：不发送给客户端
 *
 * 数组大小：PLAYER_END，涵盖从 OBJECT_FIELD_GUID 到所有 Player 特有字段。
 * 所有单位类型（玩家、NPC、宠物等）共享此标志数组。
 *
 * @note 单位字段包含大量游戏核心数据：属性、装备、光环、任务进度等，
 *       权限控制非常严格以防止作弊和信息泄露
 */
uint32 UnitUpdateFieldFlags[PLAYER_END] =
{
    // ========== 对象基础字段 (Object 基类) ==========
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID：对象GUID，所有客户端可见
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID+1：GUID高32位
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_TYPE：对象类型标识
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_ENTRY：对象模板ID
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_SCALE_X：对象缩放比例
    UF_FLAG_NONE,                                           // OBJECT_FIELD_PADDING：填充字段，不发送

    // ========== 单位关系字段 (Unit 类) ==========
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHARM：被魅惑对象GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHARM+1：被魅惑对象GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_SUMMON：召唤物GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_SUMMON+1：召唤物GUID高位
    UF_FLAG_PRIVATE,                                        // UNIT_FIELD_CRITTER：小宠物GUID（仅自己可见）
    UF_FLAG_PRIVATE,                                        // UNIT_FIELD_CRITTER+1：小宠物GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHARMEDBY：魅惑者GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHARMEDBY+1：魅惑者GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_SUMMONEDBY：召唤者GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_SUMMONEDBY+1：召唤者GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CREATEDBY：创建者GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CREATEDBY+1：创建者GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_TARGET：当前目标GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_TARGET+1：目标GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHANNEL_OBJECT：引导法术目标对象GUID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_CHANNEL_OBJECT+1：引导目标GUID高位
    UF_FLAG_PUBLIC,                                         // UNIT_CHANNEL_SPELL：引导法术ID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BYTES_0：字节字段0（种族/职业/性别/能量类型）

    // ========== 单位基础属性字段 ==========
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_HEALTH：当前生命值
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER1：能量值[0]（法力）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER2：能量值[1]（怒气）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER3：能量值[2]（专注）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER4：能量值[3]（能量）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER5：能量值[4]（快乐度）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER6：能量值[5]（符文）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_POWER7：能量值[6]（荣誉/其他）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXHEALTH：最大生命值
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER1：最大能量[0]（最大法力）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER2：最大能量[1]（最大怒气）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER3：最大能量[2]（最大专注）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER4：最大能量[3]（最大能量）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER5：最大能量[4]（最大快乐度）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER6：最大能量[5]（最大符文）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MAXPOWER7：最大能量[6]

    // ========== 能量回复修正字段（私有数据，仅所有者可见） ==========
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER：能量回复修正[0]（仅自己+所有者可见）
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+1：能量回复修正[1]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+2：能量回复修正[2]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+3：能量回复修正[3]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+4：能量回复修正[4]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+5：能量回复修正[5]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER+6：能量回复修正[6]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER：打断回复修正[0]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+1：打断回复修正[1]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+2：打断回复修正[2]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+3：打断回复修正[3]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+4：打断回复修正[4]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+5：打断回复修正[5]
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER+6：打断回复修正[6]

    // ========== 单位基础信息字段 ==========
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_LEVEL：等级
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_FACTIONTEMPLATE：阵营模板ID
    UF_FLAG_PUBLIC,                                         // UNIT_VIRTUAL_ITEM_SLOT_ID：虚拟物品槽位ID[0]（NPC武器显示）
    UF_FLAG_PUBLIC,                                         // UNIT_VIRTUAL_ITEM_SLOT_ID+1：虚拟物品槽位ID[1]
    UF_FLAG_PUBLIC,                                         // UNIT_VIRTUAL_ITEM_SLOT_ID+2：虚拟物品槽位ID[2]
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_FLAGS：单位标志（如不可攻击、飞行等）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_FLAGS_2：单位标志2（扩展标志）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_AURASTATE：光环状态（显示特殊效果）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BASEATTACKTIME：基础攻击时间[0]（主手）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BASEATTACKTIME+1：基础攻击时间[1]（副手）
    UF_FLAG_PRIVATE,                                        // UNIT_FIELD_RANGEDATTACKTIME：远程攻击时间（仅自己可见）
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BOUNDINGRADIUS：碰撞半径
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_COMBATREACH：战斗触及距离
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_DISPLAYID：显示模型ID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_NATIVEDISPLAYID：原生模型ID
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_MOUNTDISPLAYID：坐骑模型ID
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_MINDAMAGE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_MAXDAMAGE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_MINOFFHANDDAMAGE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_MAXOFFHANDDAMAGE
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BYTES_1
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_PETNUMBER
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_PET_NAME_TIMESTAMP
    UF_FLAG_OWNER,                                          // UNIT_FIELD_PETEXPERIENCE
    UF_FLAG_OWNER,                                          // UNIT_FIELD_PETNEXTLEVELEXP
    UF_FLAG_DYNAMIC,                                        // UNIT_DYNAMIC_FLAGS
    UF_FLAG_PUBLIC,                                         // UNIT_MOD_CAST_SPEED
    UF_FLAG_PUBLIC,                                         // UNIT_CREATED_BY_SPELL
    UF_FLAG_DYNAMIC,                                        // UNIT_NPC_FLAGS
    UF_FLAG_PUBLIC,                                         // UNIT_NPC_EMOTESTATE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_STAT0
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_STAT1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_STAT2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_STAT3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_STAT4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POSSTAT0
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POSSTAT1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POSSTAT2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POSSTAT3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POSSTAT4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_NEGSTAT0
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_NEGSTAT1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_NEGSTAT2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_NEGSTAT3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_NEGSTAT4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+5
    UF_FLAG_PRIVATE | UF_FLAG_OWNER | UF_FLAG_SPECIAL_INFO, // UNIT_FIELD_RESISTANCES+6
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+5
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE+6
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+5
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE+6
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BASE_MANA
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_BASE_HEALTH
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_BYTES_2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_ATTACK_POWER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_ATTACK_POWER_MODS
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_ATTACK_POWER_MULTIPLIER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RANGED_ATTACK_POWER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RANGED_ATTACK_POWER_MODS
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_MINRANGEDDAMAGE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_MAXRANGEDDAMAGE
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+5
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MODIFIER+6
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+1
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+2
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+3
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+4
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+5
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_POWER_COST_MULTIPLIER+6
    UF_FLAG_PRIVATE | UF_FLAG_OWNER,                        // UNIT_FIELD_MAXHEALTHMODIFIER
    UF_FLAG_PUBLIC,                                         // UNIT_FIELD_HOVERHEIGHT
    UF_FLAG_NONE,                                           // UNIT_FIELD_PADDING
    UF_FLAG_PUBLIC,                                         // PLAYER_DUEL_ARBITER
    UF_FLAG_PUBLIC,                                         // PLAYER_DUEL_ARBITER+1
    UF_FLAG_PUBLIC,                                         // PLAYER_FLAGS
    UF_FLAG_PUBLIC,                                         // PLAYER_GUILDID
    UF_FLAG_PUBLIC,                                         // PLAYER_GUILDRANK
    UF_FLAG_PUBLIC,                                         // PLAYER_BYTES
    UF_FLAG_PUBLIC,                                         // PLAYER_BYTES_2
    UF_FLAG_PUBLIC,                                         // PLAYER_BYTES_3
    UF_FLAG_PUBLIC,                                         // PLAYER_DUEL_TEAM
    UF_FLAG_PUBLIC,                                         // PLAYER_GUILD_TIMESTAMP
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_1_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_1_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_1_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_1_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_1_4
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_2_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_2_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_2_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_2_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_2_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_3_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_3_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_3_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_3_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_3_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_4_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_4_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_4_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_4_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_4_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_5_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_5_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_5_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_5_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_5_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_6_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_6_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_6_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_6_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_6_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_7_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_7_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_7_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_7_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_7_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_8_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_8_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_8_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_8_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_8_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_9_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_9_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_9_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_9_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_9_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_10_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_10_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_10_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_10_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_10_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_11_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_11_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_11_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_11_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_11_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_12_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_12_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_12_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_12_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_12_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_13_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_13_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_13_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_13_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_13_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_14_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_14_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_14_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_14_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_14_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_15_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_15_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_15_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_15_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_15_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_16_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_16_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_16_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_16_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_16_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_17_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_17_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_17_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_17_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_17_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_18_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_18_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_18_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_18_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_18_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_19_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_19_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_19_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_19_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_19_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_20_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_20_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_20_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_20_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_20_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_21_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_21_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_21_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_21_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_21_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_22_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_22_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_22_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_22_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_22_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_23_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_23_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_23_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_23_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_23_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_24_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_24_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_24_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_24_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_24_5
    UF_FLAG_PARTY_MEMBER,                                   // PLAYER_QUEST_LOG_25_1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_25_2
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_25_3
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_25_3+1
    UF_FLAG_PRIVATE,                                        // PLAYER_QUEST_LOG_25_5
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_1_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_1_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_2_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_2_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_3_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_3_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_4_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_4_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_5_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_5_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_6_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_6_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_7_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_7_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_8_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_8_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_9_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_9_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_10_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_10_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_11_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_11_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_12_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_12_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_13_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_13_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_14_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_14_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_15_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_15_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_16_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_16_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_17_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_17_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_18_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_18_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_19_ENTRYID
    UF_FLAG_PUBLIC,                                         // PLAYER_VISIBLE_ITEM_19_ENCHANTMENT
    UF_FLAG_PUBLIC,                                         // PLAYER_CHOSEN_TITLE
    UF_FLAG_PUBLIC,                                         // PLAYER_FAKE_INEBRIATION
    UF_FLAG_NONE,                                           // PLAYER_FIELD_PAD_0
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+25
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+26
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+27
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+28
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+29
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+30
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+31
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+32
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+33
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+34
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+35
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+36
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+37
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+38
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+39
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+40
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+41
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+42
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+43
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+44
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_INV_SLOT_HEAD+45
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PACK_SLOT_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+32
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+33
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+34
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+35
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+36
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+37
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+38
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+39
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+40
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+41
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+42
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+43
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+44
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+45
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+46
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+47
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+48
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+49
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+50
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+51
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+52
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+53
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+54
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANK_SLOT_1+55
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BANKBAG_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_VENDORBUYBACK_SLOT_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+32
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+33
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+34
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+35
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+36
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+37
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+38
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+39
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+40
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+41
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+42
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+43
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+44
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+45
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+46
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+47
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+48
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+49
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+50
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+51
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+52
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+53
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+54
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+55
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+56
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+57
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+58
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+59
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+60
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+61
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+62
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KEYRING_SLOT_1+63
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+32
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+33
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+34
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+35
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+36
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+37
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+38
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+39
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+40
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+41
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+42
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+43
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+44
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+45
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+46
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+47
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+48
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+49
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+50
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+51
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+52
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+53
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+54
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+55
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+56
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+57
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+58
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+59
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+60
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+61
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+62
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_CURRENCYTOKEN_SLOT_1+63
    UF_FLAG_PRIVATE,                                        // PLAYER_FARSIGHT
    UF_FLAG_PRIVATE,                                        // PLAYER_FARSIGHT+1
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES+1
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES1
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES1+1
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES2
    UF_FLAG_PRIVATE,                                        // PLAYER__FIELD_KNOWN_TITLES2+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KNOWN_CURRENCIES
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KNOWN_CURRENCIES+1
    UF_FLAG_PRIVATE,                                        // PLAYER_XP
    UF_FLAG_PRIVATE,                                        // PLAYER_NEXT_LEVEL_XP
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+32
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+33
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+34
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+35
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+36
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+37
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+38
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+39
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+40
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+41
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+42
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+43
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+44
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+45
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+46
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+47
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+48
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+49
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+50
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+51
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+52
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+53
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+54
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+55
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+56
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+57
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+58
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+59
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+60
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+61
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+62
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+63
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+64
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+65
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+66
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+67
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+68
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+69
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+70
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+71
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+72
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+73
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+74
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+75
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+76
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+77
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+78
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+79
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+80
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+81
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+82
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+83
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+84
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+85
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+86
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+87
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+88
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+89
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+90
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+91
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+92
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+93
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+94
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+95
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+96
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+97
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+98
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+99
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+100
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+101
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+102
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+103
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+104
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+105
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+106
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+107
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+108
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+109
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+110
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+111
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+112
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+113
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+114
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+115
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+116
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+117
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+118
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+119
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+120
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+121
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+122
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+123
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+124
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+125
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+126
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+127
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+128
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+129
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+130
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+131
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+132
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+133
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+134
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+135
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+136
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+137
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+138
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+139
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+140
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+141
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+142
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+143
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+144
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+145
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+146
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+147
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+148
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+149
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+150
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+151
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+152
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+153
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+154
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+155
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+156
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+157
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+158
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+159
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+160
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+161
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+162
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+163
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+164
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+165
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+166
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+167
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+168
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+169
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+170
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+171
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+172
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+173
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+174
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+175
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+176
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+177
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+178
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+179
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+180
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+181
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+182
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+183
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+184
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+185
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+186
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+187
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+188
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+189
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+190
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+191
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+192
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+193
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+194
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+195
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+196
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+197
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+198
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+199
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+200
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+201
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+202
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+203
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+204
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+205
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+206
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+207
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+208
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+209
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+210
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+211
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+212
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+213
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+214
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+215
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+216
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+217
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+218
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+219
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+220
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+221
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+222
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+223
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+224
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+225
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+226
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+227
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+228
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+229
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+230
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+231
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+232
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+233
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+234
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+235
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+236
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+237
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+238
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+239
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+240
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+241
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+242
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+243
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+244
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+245
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+246
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+247
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+248
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+249
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+250
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+251
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+252
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+253
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+254
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+255
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+256
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+257
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+258
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+259
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+260
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+261
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+262
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+263
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+264
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+265
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+266
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+267
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+268
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+269
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+270
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+271
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+272
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+273
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+274
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+275
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+276
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+277
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+278
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+279
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+280
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+281
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+282
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+283
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+284
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+285
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+286
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+287
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+288
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+289
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+290
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+291
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+292
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+293
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+294
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+295
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+296
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+297
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+298
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+299
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+300
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+301
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+302
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+303
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+304
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+305
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+306
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+307
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+308
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+309
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+310
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+311
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+312
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+313
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+314
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+315
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+316
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+317
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+318
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+319
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+320
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+321
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+322
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+323
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+324
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+325
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+326
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+327
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+328
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+329
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+330
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+331
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+332
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+333
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+334
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+335
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+336
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+337
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+338
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+339
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+340
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+341
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+342
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+343
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+344
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+345
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+346
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+347
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+348
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+349
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+350
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+351
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+352
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+353
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+354
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+355
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+356
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+357
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+358
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+359
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+360
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+361
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+362
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+363
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+364
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+365
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+366
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+367
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+368
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+369
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+370
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+371
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+372
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+373
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+374
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+375
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+376
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+377
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+378
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+379
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+380
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+381
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+382
    UF_FLAG_PRIVATE,                                        // PLAYER_SKILL_INFO_1_1+383
    UF_FLAG_PRIVATE,                                        // PLAYER_CHARACTER_POINTS1
    UF_FLAG_PRIVATE,                                        // PLAYER_CHARACTER_POINTS2
    UF_FLAG_PRIVATE,                                        // PLAYER_TRACK_CREATURES
    UF_FLAG_PRIVATE,                                        // PLAYER_TRACK_RESOURCES
    UF_FLAG_PRIVATE,                                        // PLAYER_BLOCK_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_DODGE_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_PARRY_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPERTISE
    UF_FLAG_PRIVATE,                                        // PLAYER_OFFHAND_EXPERTISE
    UF_FLAG_PRIVATE,                                        // PLAYER_CRIT_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_RANGED_CRIT_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_OFFHAND_CRIT_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_SPELL_CRIT_PERCENTAGE1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_SHIELD_BLOCK
    UF_FLAG_PRIVATE,                                        // PLAYER_SHIELD_BLOCK_CRIT_PERCENTAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+25
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+26
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+27
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+28
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+29
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+30
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+31
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+32
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+33
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+34
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+35
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+36
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+37
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+38
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+39
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+40
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+41
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+42
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+43
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+44
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+45
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+46
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+47
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+48
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+49
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+50
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+51
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+52
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+53
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+54
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+55
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+56
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+57
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+58
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+59
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+60
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+61
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+62
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+63
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+64
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+65
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+66
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+67
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+68
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+69
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+70
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+71
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+72
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+73
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+74
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+75
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+76
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+77
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+78
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+79
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+80
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+81
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+82
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+83
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+84
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+85
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+86
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+87
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+88
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+89
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+90
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+91
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+92
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+93
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+94
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+95
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+96
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+97
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+98
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+99
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+100
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+101
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+102
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+103
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+104
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+105
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+106
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+107
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+108
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+109
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+110
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+111
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+112
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+113
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+114
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+115
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+116
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+117
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+118
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+119
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+120
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+121
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+122
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+123
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+124
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+125
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+126
    UF_FLAG_PRIVATE,                                        // PLAYER_EXPLORED_ZONES_1+127
    UF_FLAG_PRIVATE,                                        // PLAYER_REST_STATE_EXPERIENCE
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COINAGE
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_POS+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_NEG+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_DAMAGE_DONE_PCT+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_HEALING_DONE_POS
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_HEALING_PCT
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_HEALING_DONE_PCT
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_TARGET_RESISTANCE
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BYTES
    UF_FLAG_PRIVATE,                                        // PLAYER_AMMO_ID
    UF_FLAG_PRIVATE,                                        // PLAYER_SELF_RES_SPELL
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_PVP_MEDALS
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_PRICE_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BUYBACK_TIMESTAMP_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_KILLS
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_TODAY_CONTRIBUTION
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_YESTERDAY_CONTRIBUTION
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_LIFETIME_HONORBALE_KILLS
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_BYTES2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_WATCHED_FACTION_INDEX
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_COMBAT_RATING_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_TEAM_INFO_1_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_HONOR_CURRENCY
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_ARENA_CURRENCY
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_MAX_LEVEL
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+6
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+7
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+8
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+9
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+10
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+11
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+12
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+13
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+14
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+15
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+16
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+17
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+18
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+19
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+20
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+21
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+22
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+23
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_DAILY_QUESTS_1+24
    UF_FLAG_PRIVATE,                                        // PLAYER_RUNE_REGEN_1
    UF_FLAG_PRIVATE,                                        // PLAYER_RUNE_REGEN_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_RUNE_REGEN_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_RUNE_REGEN_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_NO_REAGENT_COST_1
    UF_FLAG_PRIVATE,                                        // PLAYER_NO_REAGENT_COST_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_NO_REAGENT_COST_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPH_SLOTS_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1+1
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1+2
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1+3
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1+4
    UF_FLAG_PRIVATE,                                        // PLAYER_FIELD_GLYPHS_1+5
    UF_FLAG_PRIVATE,                                        // PLAYER_GLYPHS_ENABLED
    UF_FLAG_PRIVATE,                                        // PLAYER_PET_SPELL_POWER
};

/**
 * @brief 游戏对象（GameObject）更新字段权限标志数组
 *
 * 定义了游戏对象(GameObject)类型对象的所有更新字段的访问权限。
 * 数组索引对应字段枚举值(UpdateFields.h 中的 GAMEOBJECT_* 常量)。
 *
 * 游戏对象类型：
 * - 门、箱子、矿石、草药等可交互对象
 * - 任务物品、陷阱、传送门等
 * - 建筑、旗帜等场景对象
 *
 * 权限特点：
 * - 大部分字段为 UF_FLAG_PUBLIC（所有客户端可见）
 * - GAMEOBJECT_DYNAMIC 使用动态标志，根据对象状态和观察者身份决定可见性
 * - 游戏对象的状态通常需要所有人看到（开关门、宝箱状态等）
 *
 * @note 游戏对象字段较少，主要用于显示模型、状态标志和旋转信息
 */
uint32 GameObjectUpdateFieldFlags[GAMEOBJECT_END] =
{
    // ========== 对象基础字段 (Object 基类) ==========
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID：对象GUID，所有客户端可见
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID+1：GUID高32位
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_TYPE：对象类型标识
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_ENTRY：对象模板ID
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_SCALE_X：对象缩放比例
    UF_FLAG_NONE,                                           // OBJECT_FIELD_PADDING：填充字段，不发送
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_CREATED_BY：创建者GUID（如陷阱创建者）
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_CREATED_BY+1：创建者GUID高位

    // ========== 游戏对象特有字段 ==========
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_DISPLAYID：显示模型ID
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_FLAGS：游戏对象标志（如激活状态、已使用等）
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_PARENTROTATION：父旋转四元数[0]
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_PARENTROTATION+1：父旋转四元数[1]
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_PARENTROTATION+2：父旋转四元数[2]
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_PARENTROTATION+3：父旋转四元数[3]
    UF_FLAG_DYNAMIC,                                        // GAMEOBJECT_DYNAMIC：动态标志（根据上下文决定可见性，如宝箱状态）
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_FACTION：阵营ID
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_LEVEL：对象等级（如锁的等级）
    UF_FLAG_PUBLIC,                                         // GAMEOBJECT_BYTES_1：字节字段（状态、类型等）
};

/**
 * @brief 动态对象（DynamicObject）更新字段权限标志数组
 *
 * 定义了动态对象(DynamicObject)类型对象的所有更新字段的访问权限。
 * 数组索引对应字段枚举值(UpdateFields.h 中的 DYNAMICOBJECT_* 常量)。
 *
 * 动态对象用途：
 * - 法术效果区域（AOE 区域，如暴风雪、奉献等）
 * - 范围性法术作用区域标记
 * - 持续性地面效果
 *
 * 权限特点：
 * - 所有字段均为 UF_FLAG_PUBLIC（所有客户端需要看到法术效果区域）
 * - 动态对象是临时对象，生命周期通常与法术持续时间一致
 *
 * @note 动态对象字段最少，仅包含基础信息和法术相关数据
 */
uint32 DynamicObjectUpdateFieldFlags[DYNAMICOBJECT_END] =
{
    // ========== 对象基础字段 (Object 基类) ==========
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID：对象GUID，所有客户端可见
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID+1：GUID高32位
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_TYPE：对象类型标识
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_ENTRY：对象模板ID
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_SCALE_X：对象缩放比例
    UF_FLAG_NONE,                                           // OBJECT_FIELD_PADDING：填充字段，不发送

    // ========== 动态对象特有字段 ==========
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_CASTER：施法者GUID
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_CASTER+1：施法者GUID高位
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_BYTES：字节字段（类型等）
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_SPELLID：法术ID
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_RADIUS：作用半径
    UF_FLAG_PUBLIC,                                         // DYNAMICOBJECT_CASTTIME：施法时间
};

/**
 * @brief 尸体对象（Corpse）更新字段权限标志数组
 *
 * 定义了尸体对象(Corpse)类型对象的所有更新字段的访问权限。
 * 数组索引对应字段枚举值(UpdateFields.h 中的 CORPSE_FIELD_* 常量)。
 *
 * 尸体对象用途：
 * - 玩家死亡后留下的尸体
 * - 显示死亡时的装备外观
 * - 标记玩家死亡位置，用于灵魂复活
 *
 * 权限特点：
 * - 大部分字段为 UF_FLAG_PUBLIC（所有客户端可见尸体外观）
 * - CORPSE_FIELD_DYNAMIC_FLAGS 使用动态标志，根据距离和状态决定可见性
 * - 尸体物品字段展示死亡时穿戴的装备外观（非实际物品，仅显示模型）
 *
 * @note 尸体对象是临时对象，在一定时间后自动消失
 */
uint32 CorpseUpdateFieldFlags[CORPSE_END] =
{
    // ========== 对象基础字段 (Object 基类) ==========
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID：对象GUID，所有客户端可见
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_GUID+1：GUID高32位
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_TYPE：对象类型标识
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_ENTRY：对象模板ID
    UF_FLAG_PUBLIC,                                         // OBJECT_FIELD_SCALE_X：对象缩放比例
    UF_FLAG_NONE,                                           // OBJECT_FIELD_PADDING：填充字段，不发送

    // ========== 尸体对象特有字段 ==========
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_OWNER：尸体所有者GUID
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_OWNER+1：所有者GUID高位
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_PARTY：队伍GUID（用于队伍成员尸体显示）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_PARTY+1：队伍GUID高位
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_DISPLAY_ID：显示模型ID（死亡时外观）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM：物品显示ID[0]（头部装备模型）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+1：物品显示ID[1]（颈部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+2：物品显示ID[2]（肩部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+3：物品显示ID[3]（背部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+4：物品显示ID[4]（胸部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+5：物品显示ID[5]（衬衣）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+6：物品显示ID[6]（战袍）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+7：物品显示ID[7]（护腕）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+8：物品显示ID[8]（手套）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+9：物品显示ID[9]（腰带）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+10：物品显示ID[10]（腿部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+11：物品显示ID[11]（脚部）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+12：物品显示ID[12]（手指1）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+13：物品显示ID[13]（手指2）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+14：物品显示ID[14]（饰品1）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+15：物品显示ID[15]（饰品2）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+16：物品显示ID[16]（主手武器）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+17：物品显示ID[17]（副手武器）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_ITEM+18：物品显示ID[18]（远程武器）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_BYTES_1：字节字段1（种族、性别等）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_BYTES_2：字节字段2（外观设置）
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_GUILD：公会ID
    UF_FLAG_PUBLIC,                                         // CORPSE_FIELD_FLAGS：尸体标志（如是否可剥皮）
    UF_FLAG_DYNAMIC,                                        // CORPSE_FIELD_DYNAMIC_FLAGS：动态标志（根据距离和关系决定可见性）
    UF_FLAG_NONE,                                           // CORPSE_FIELD_PAD：填充字段，不发送
};
