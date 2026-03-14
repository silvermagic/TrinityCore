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
 * @file enuminfo_UnitDefines.cpp
 * @brief UnitDefines 枚举类型的反射工具实现
 *
 * 本文件为 UnitDefines.h 中定义的枚举类型提供反射功能实现:
 * - UnitFlags: 单位标志位枚举
 * - NPCFlags: NPC 功能标志位枚举
 *
 * 通过 EnumUtils 模板特化实现枚举值的字符串转换、
 * 索引映射等功能,用于日志输出、调试信息、配置解析等场景。
 *
 * @note 本文件由代码生成器自动生成,请勿手动修改
 * @see UnitDefines.h 枚举定义头文件
 * @see SmartEnum.h EnumUtils 模板基类
 */

#include "UnitDefines.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/***************************************************************\
|* data for enum 'UnitFlags' in 'UnitDefines.h' auto-generated *|
\***************************************************************/

/**
 * @brief UnitFlags 枚举值转字符串描述
 *
 * 将 UnitFlags 枚举值转换为包含枚举名称、简短描述和详细说明的文本结构。
 *
 * @tparam UnitFlags 枚举类型模板参数(特化版本)
 * @param value 要转换的 UnitFlags 枚举值
 * @return EnumText 包含三部分文本的结构体:
 *         - 枚举常量名称(如 "UNIT_FLAG_STUNNED")
 *         - 可读性标题(通常与常量名相同)
 *         - 详细功能描述(英文)
 * @throws std::out_of_range 当传入无效枚举值时抛出
 *
 * @note 该函数用于日志输出和调试信息,性能敏感场景请避免频繁调用
 * @note 描述文本为英文,由代码生成器从枚举定义中提取
 */
template <>
TC_API_EXPORT EnumText EnumUtils<UnitFlags>::ToString(UnitFlags value)
{
    switch (value)
    {
        // 服务器控制标志 - 当单位移动由服务器控制时设置
        // 与 UNIT_FLAG_STUNNED 配合使用,仅对客户端控制的单位有效
        case UNIT_FLAG_SERVER_CONTROLLED: return { "UNIT_FLAG_SERVER_CONTROLLED", "UNIT_FLAG_SERVER_CONTROLLED", "set only when unit movement is controlled by server - by SPLINE/MONSTER_MOVE packets, together with UNIT_FLAG_STUNNED; only set to units controlled by client; client function CGUnit_C::IsClientControlled returns false when set for owner" };

        // 不可攻击标志 - 生物开始施放带施法时间的生成法术时设置
        // 法术命中施法者后移除,原始名称为 UNIT_FLAG_SPAWNING
        case UNIT_FLAG_NON_ATTACKABLE: return { "UNIT_FLAG_NON_ATTACKABLE", "UNIT_FLAG_NON_ATTACKABLE", "not attackable, set when creature starts to cast spells with SPELL_EFFECT_SPAWN and cast time, removed when spell hits caster, original name is UNIT_FLAG_SPAWNING. Rename when it will be removed from all scripts" };

        // 移除客户端控制标志 - 遗留标志,用于在控制其他单位时禁用玩家移动
        // 现由 SMSG_CLIENT_CONTROL 在客户端替代此功能
        case UNIT_FLAG_REMOVE_CLIENT_CONTROL: return { "UNIT_FLAG_REMOVE_CLIENT_CONTROL", "UNIT_FLAG_REMOVE_CLIENT_CONTROL", "This is a legacy flag used to disable movement player's movement while controlling other units, SMSG_CLIENT_CONTROL replaces this functionality clientside now. CONFUSED and FLEEING flags have the same effect on client movement asDISABLE_MOVE_CONTROL in addition to preventing spell casts/autoattack (they all allow climbing steeper hills and emotes while moving)" };

        // 玩家控制标志 - 由玩家控制时使用 _IMMUNE_TO_PC 而非 _IMMUNE_TO_NPC
        case UNIT_FLAG_PLAYER_CONTROLLED: return { "UNIT_FLAG_PLAYER_CONTROLLED", "UNIT_FLAG_PLAYER_CONTROLLED", "controlled by player, use _IMMUNE_TO_PC instead of _IMMUNE_TO_NPC" };

        // 重命名标志
        case UNIT_FLAG_RENAME: return { "UNIT_FLAG_RENAME", "UNIT_FLAG_RENAME", "" };

        // 准备状态标志 - 战场准备阶段,不消耗带有 SPELL_ATTR5_NO_REAGENT_WHILE_PREP 属性的法术材料
        case UNIT_FLAG_PREPARATION: return { "UNIT_FLAG_PREPARATION", "UNIT_FLAG_PREPARATION", "don't take reagents for spells with SPELL_ATTR5_NO_REAGENT_WHILE_PREP" };

        // 未知标志 6
        case UNIT_FLAG_UNK_6: return { "UNIT_FLAG_UNK_6", "UNIT_FLAG_UNK_6", "" };

        // 不可攻击标志 1 - 与 PLAYER_CONTROLLED 组合为 NON_PVP_ATTACKABLE
        case UNIT_FLAG_NOT_ATTACKABLE_1: return { "UNIT_FLAG_NOT_ATTACKABLE_1", "UNIT_FLAG_NOT_ATTACKABLE_1", "?? (UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_NOT_ATTACKABLE_1) is NON_PVP_ATTACKABLE" };

        // 对玩家角色免疫 - 禁用与玩家角色(PC)的战斗/协助交互
        // 参见 Unit::IsValidAttackTarget, Unit::IsValidAssistTarget
        case UNIT_FLAG_IMMUNE_TO_PC: return { "UNIT_FLAG_IMMUNE_TO_PC", "UNIT_FLAG_IMMUNE_TO_PC", "disables combat/assistance with PlayerCharacters (PC) - see Unit::IsValidAttackTarget, Unit::IsValidAssistTarget" };

        // 对非玩家角色免疫 - 禁用与非玩家角色(NPC)的战斗/协助交互
        // 参见 Unit::IsValidAttackTarget, Unit::IsValidAssistTarget
        case UNIT_FLAG_IMMUNE_TO_NPC: return { "UNIT_FLAG_IMMUNE_TO_NPC", "UNIT_FLAG_IMMUNE_TO_NPC", "disables combat/assistance with NonPlayerCharacters (NPC) - see Unit::IsValidAttackTarget, Unit::IsValidAssistTarget" };

        // 拾取动画标志 - 显示拾取动作动画
        case UNIT_FLAG_LOOTING: return { "UNIT_FLAG_LOOTING", "UNIT_FLAG_LOOTING", "loot animation" };

        // 宠物战斗标志 - 玩家宠物:宠物是否正在追击攻击目标
        // 其他单位:该单位的任何随从是否在战斗中
        case UNIT_FLAG_PET_IN_COMBAT: return { "UNIT_FLAG_PET_IN_COMBAT", "UNIT_FLAG_PET_IN_COMBAT", "on player pets: whether the pet is chasing a target to attack || on other units: whether any of the unit's minions is in combat" };

        // PvP 启用标志 - 3.0.3 版本后改为使用 UNIT_BYTES_2_OFFSET_PVP_FLAG
        case UNIT_FLAG_PVP_ENABLING: return { "UNIT_FLAG_PVP_ENABLING", "UNIT_FLAG_PVP_ENABLING", "changed in 3.0.3, now UNIT_BYTES_2_OFFSET_PVP_FLAG from UNIT_FIELD_BYTES_2" };

        // 沉默标志 - 禁止施法,2.1.1 版本
        case UNIT_FLAG_SILENCED: return { "UNIT_FLAG_SILENCED", "UNIT_FLAG_SILENCED", "silenced, 2.1.1" };

        // 不能游泳标志 - 2.0.8 版本
        case UNIT_FLAG_CANNOT_SWIM: return { "UNIT_FLAG_CANNOT_SWIM", "UNIT_FLAG_CANNOT_SWIM", "2.0.8" };

        // 可以游泳标志 - 在水中显示游泳动画
        case UNIT_FLAG_CAN_SWIM: return { "UNIT_FLAG_CAN_SWIM", "UNIT_FLAG_CAN_SWIM", "shows swim animation in water" };

        // 不可攻击标志 2 - 移除攻击图标,对自己设置时无法协助自己但可施放 TARGET_SELF 法术
        // 由 SPELL_AURA_MOD_UNATTACKABLE 光环添加
        case UNIT_FLAG_NON_ATTACKABLE_2: return { "UNIT_FLAG_NON_ATTACKABLE_2", "UNIT_FLAG_NON_ATTACKABLE_2", "removes attackable icon, if on yourself, cannot assist self but can cast TARGET_SELF spells - added by SPELL_AURA_MOD_UNATTACKABLE" };

        // 平息标志 - 3.0.3 版本确认有效
        case UNIT_FLAG_PACIFIED: return { "UNIT_FLAG_PACIFIED", "UNIT_FLAG_PACIFIED", "3.0.3 ok" };

        // 眩晕标志 - 3.0.3 版本确认有效
        case UNIT_FLAG_STUNNED: return { "UNIT_FLAG_STUNNED", "UNIT_FLAG_STUNNED", "3.0.3 ok" };

        // 战斗中标志 - 单位处于战斗状态
        case UNIT_FLAG_IN_COMBAT: return { "UNIT_FLAG_IN_COMBAT", "UNIT_FLAG_IN_COMBAT", "" };

        // 出租车标志 - 在出租车(飞行路线)上时禁用客户端施放不允许的法术
        // 可能与 0x4 标志配合使用
        case UNIT_FLAG_ON_TAXI: return { "UNIT_FLAG_ON_TAXI", "UNIT_FLAG_ON_TAXI", "disable casting at client side spell not allowed by taxi flight (mounted?), probably used with 0x4 flag" };

        // 缴械标志 - 3.0.3 版本,禁用近战法术施放
        // 近战法术工具提示中添加"需要近战武器"
        case UNIT_FLAG_DISARMED: return { "UNIT_FLAG_DISARMED", "UNIT_FLAG_DISARMED", "3.0.3, disable melee spells casting..., \042Required melee weapon\042 added to melee spells tooltip." };

        // 混乱标志 - 单位处于混乱状态(随机移动)
        case UNIT_FLAG_CONFUSED: return { "UNIT_FLAG_CONFUSED", "UNIT_FLAG_CONFUSED", "" };

        // 逃跑标志 - 单位处于恐惧逃跑状态
        case UNIT_FLAG_FLEEING: return { "UNIT_FLAG_FLEEING", "UNIT_FLAG_FLEEING", "" };

        // 附身标志 - 由玩家直接控制(附身或载具)
        case UNIT_FLAG_POSSESSED: return { "UNIT_FLAG_POSSESSED", "UNIT_FLAG_POSSESSED", "under direct client control by a player (possess or vehicle)" };

        // 不可交互标志
        case UNIT_FLAG_UNINTERACTIBLE: return { "UNIT_FLAG_UNINTERACTIBLE", "UNIT_FLAG_UNINTERACTIBLE", "" };

        // 可剥皮标志
        case UNIT_FLAG_SKINNABLE: return { "UNIT_FLAG_SKINNABLE", "UNIT_FLAG_SKINNABLE", "" };

        // 骑乘标志 - 单位正在骑乘坐骑
        case UNIT_FLAG_MOUNT: return { "UNIT_FLAG_MOUNT", "UNIT_FLAG_MOUNT", "" };

        // 未知标志 28
        case UNIT_FLAG_UNK_28: return { "UNIT_FLAG_UNK_28", "UNIT_FLAG_UNK_28", "" };

        // 阻止聊天文本表情标志 - 防止自动播放从聊天文本解析的表情
        // 例如 /say 中的"lol",以 ? 或 ! 结尾的消息,或使用 /yell
        case UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT: return { "UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT", "UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT", "Prevent automatically playing emotes from parsing chat text, for example \042lol\042 in /say, ending message with ? or !, or using /yell" };

        // 收起武器标志
        case UNIT_FLAG_SHEATHE: return { "UNIT_FLAG_SHEATHE", "UNIT_FLAG_SHEATHE", "" };

        // 免疫标志 - 对伤害免疫
        case UNIT_FLAG_IMMUNE: return { "UNIT_FLAG_IMMUNE", "UNIT_FLAG_IMMUNE", "Immune to damage" };

        // 无效值,抛出异常
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 UnitFlags 枚举的元素总数
 *
 * @tparam UnitFlags 枚举类型模板参数(特化版本)
 * @return size_t 枚举元素总数,固定返回 32
 *
 * @note 该值用于迭代枚举值时的边界检查
 */
template <>
TC_API_EXPORT size_t EnumUtils<UnitFlags>::Count() { return 32; }

/**
 * @brief 根据索引获取 UnitFlags 枚举值
 *
 * 将顺序索引转换为对应的枚举常量值。
 * 索引范围: [0, Count()-1],超出范围将抛出异常。
 *
 * @tparam UnitFlags 枚举类型模板参数(特化版本)
 * @param index 枚举值的顺序索引(从 0 开始)
 * @return UnitFlags 对应的枚举常量值
 * @throws std::out_of_range 当索引超出有效范围时抛出
 *
 * @note 该函数用于数组式访问枚举值,支持枚举迭代
 * @see ToIndex() 反向映射函数
 */
template <>
TC_API_EXPORT UnitFlags EnumUtils<UnitFlags>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return UNIT_FLAG_SERVER_CONTROLLED;
        case 1: return UNIT_FLAG_NON_ATTACKABLE;
        case 2: return UNIT_FLAG_REMOVE_CLIENT_CONTROL;
        case 3: return UNIT_FLAG_PLAYER_CONTROLLED;
        case 4: return UNIT_FLAG_RENAME;
        case 5: return UNIT_FLAG_PREPARATION;
        case 6: return UNIT_FLAG_UNK_6;
        case 7: return UNIT_FLAG_NOT_ATTACKABLE_1;
        case 8: return UNIT_FLAG_IMMUNE_TO_PC;
        case 9: return UNIT_FLAG_IMMUNE_TO_NPC;
        case 10: return UNIT_FLAG_LOOTING;
        case 11: return UNIT_FLAG_PET_IN_COMBAT;
        case 12: return UNIT_FLAG_PVP_ENABLING;
        case 13: return UNIT_FLAG_SILENCED;
        case 14: return UNIT_FLAG_CANNOT_SWIM;
        case 15: return UNIT_FLAG_CAN_SWIM;
        case 16: return UNIT_FLAG_NON_ATTACKABLE_2;
        case 17: return UNIT_FLAG_PACIFIED;
        case 18: return UNIT_FLAG_STUNNED;
        case 19: return UNIT_FLAG_IN_COMBAT;
        case 20: return UNIT_FLAG_ON_TAXI;
        case 21: return UNIT_FLAG_DISARMED;
        case 22: return UNIT_FLAG_CONFUSED;
        case 23: return UNIT_FLAG_FLEEING;
        case 24: return UNIT_FLAG_POSSESSED;
        case 25: return UNIT_FLAG_UNINTERACTIBLE;
        case 26: return UNIT_FLAG_SKINNABLE;
        case 27: return UNIT_FLAG_MOUNT;
        case 28: return UNIT_FLAG_UNK_28;
        case 29: return UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT;
        case 30: return UNIT_FLAG_SHEATHE;
        case 31: return UNIT_FLAG_IMMUNE;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将 UnitFlags 枚举值转换为索引
 *
 * 获取枚举常量值对应的顺序索引。
 * 与 FromIndex() 构成双向映射关系。
 *
 * @tparam UnitFlags 枚举类型模板参数(特化版本)
 * @param value 要转换的枚举值
 * @return size_t 对应的顺序索引(从 0 开始)
 * @throws std::out_of_range 当传入无效枚举值时抛出
 *
 * @note 该函数用于将枚举值转换为数组索引,便于数组存储和查找
 * @see FromIndex() 反向映射函数
 */
template <>
TC_API_EXPORT size_t EnumUtils<UnitFlags>::ToIndex(UnitFlags value)
{
    switch (value)
    {
        case UNIT_FLAG_SERVER_CONTROLLED: return 0;
        case UNIT_FLAG_NON_ATTACKABLE: return 1;
        case UNIT_FLAG_REMOVE_CLIENT_CONTROL: return 2;
        case UNIT_FLAG_PLAYER_CONTROLLED: return 3;
        case UNIT_FLAG_RENAME: return 4;
        case UNIT_FLAG_PREPARATION: return 5;
        case UNIT_FLAG_UNK_6: return 6;
        case UNIT_FLAG_NOT_ATTACKABLE_1: return 7;
        case UNIT_FLAG_IMMUNE_TO_PC: return 8;
        case UNIT_FLAG_IMMUNE_TO_NPC: return 9;
        case UNIT_FLAG_LOOTING: return 10;
        case UNIT_FLAG_PET_IN_COMBAT: return 11;
        case UNIT_FLAG_PVP_ENABLING: return 12;
        case UNIT_FLAG_SILENCED: return 13;
        case UNIT_FLAG_CANNOT_SWIM: return 14;
        case UNIT_FLAG_CAN_SWIM: return 15;
        case UNIT_FLAG_NON_ATTACKABLE_2: return 16;
        case UNIT_FLAG_PACIFIED: return 17;
        case UNIT_FLAG_STUNNED: return 18;
        case UNIT_FLAG_IN_COMBAT: return 19;
        case UNIT_FLAG_ON_TAXI: return 20;
        case UNIT_FLAG_DISARMED: return 21;
        case UNIT_FLAG_CONFUSED: return 22;
        case UNIT_FLAG_FLEEING: return 23;
        case UNIT_FLAG_POSSESSED: return 24;
        case UNIT_FLAG_UNINTERACTIBLE: return 25;
        case UNIT_FLAG_SKINNABLE: return 26;
        case UNIT_FLAG_MOUNT: return 27;
        case UNIT_FLAG_UNK_28: return 28;
        case UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT: return 29;
        case UNIT_FLAG_SHEATHE: return 30;
        case UNIT_FLAG_IMMUNE: return 31;
        default: throw std::out_of_range("value");
    }
}

/**************************************************************\
|* data for enum 'NPCFlags' in 'UnitDefines.h' auto-generated *|
\**************************************************************/

/**
 * @brief NPCFlags 枚举值转字符串描述
 *
 * 将 NPCFlags 枚举值转换为包含枚举名称、简短描述和详细说明的文本结构。
 * NPC 标志定义了 NPC 的功能类型,如商人、训练师、任务发布者等。
 *
 * @tparam NPCFlags 枚举类型模板参数(特化版本)
 * @param value 要转换的 NPCFlags 枚举值
 * @return EnumText 包含三部分文本的结构体:
 *         - 枚举常量名称(如 "UNIT_NPC_FLAG_VENDOR")
 *         - 可读性标题(如 "is vendor (generic)")
 *         - 详细功能描述(英文)
 * @throws std::out_of_range 当传入无效枚举值时抛出
 *
 * @note 该函数用于日志输出和调试信息,性能敏感场景请避免频繁调用
 * @note 描述文本为英文,由代码生成器从枚举定义中提取
 */
template <>
TC_API_EXPORT EnumText EnumUtils<NPCFlags>::ToString(NPCFlags value)
{
    switch (value)
    {
        // 对话菜单标志 - NPC 拥有对话菜单交互功能
        case UNIT_NPC_FLAG_GOSSIP: return { "UNIT_NPC_FLAG_GOSSIP", "has gossip menu", "100%" };

        // 任务发布者标志 - NPC 可以发布任务
        case UNIT_NPC_FLAG_QUESTGIVER: return { "UNIT_NPC_FLAG_QUESTGIVER", "is quest giver", "guessed, probably ok" };

        // 未知标志 1
        case UNIT_NPC_FLAG_UNK1: return { "UNIT_NPC_FLAG_UNK1", "UNIT_NPC_FLAG_UNK1", "" };

        // 未知标志 2
        case UNIT_NPC_FLAG_UNK2: return { "UNIT_NPC_FLAG_UNK2", "UNIT_NPC_FLAG_UNK2", "" };

        // 训练师标志 - 通用训练师
        case UNIT_NPC_FLAG_TRAINER: return { "UNIT_NPC_FLAG_TRAINER", "is trainer", "100%" };

        // 职业训练师标志 - 提供职业技能训练
        case UNIT_NPC_FLAG_TRAINER_CLASS: return { "UNIT_NPC_FLAG_TRAINER_CLASS", "is class trainer", "100%" };

        // 专业训练师标志 - 提供专业技能训练
        case UNIT_NPC_FLAG_TRAINER_PROFESSION: return { "UNIT_NPC_FLAG_TRAINER_PROFESSION", "is profession trainer", "100%" };

        // 商人标志 - 通用商人
        case UNIT_NPC_FLAG_VENDOR: return { "UNIT_NPC_FLAG_VENDOR", "is vendor (generic)", "100%" };

        // 弹药商人标志 - 出售弹药,通常是杂货商
        case UNIT_NPC_FLAG_VENDOR_AMMO: return { "UNIT_NPC_FLAG_VENDOR_AMMO", "is vendor (ammo)", "100%, general goods vendor" };

        // 食物商人标志 - 出售食物
        case UNIT_NPC_FLAG_VENDOR_FOOD: return { "UNIT_NPC_FLAG_VENDOR_FOOD", "is vendor (food)", "100%" };

        // 毒药商人标志 - 出售毒药
        case UNIT_NPC_FLAG_VENDOR_POISON: return { "UNIT_NPC_FLAG_VENDOR_POISON", "is vendor (poison)", "guessed" };

        // 材料商人标志 - 出售施法材料
        case UNIT_NPC_FLAG_VENDOR_REAGENT: return { "UNIT_NPC_FLAG_VENDOR_REAGENT", "is vendor (reagents)", "100%" };

        // 修理商标志 - 提供装备修理服务
        case UNIT_NPC_FLAG_REPAIR: return { "UNIT_NPC_FLAG_REPAIR", "can repair", "100%" };

        // 飞行管理员标志 - 提供飞行点服务
        case UNIT_NPC_FLAG_FLIGHTMASTER: return { "UNIT_NPC_FLAG_FLIGHTMASTER", "is flight master", "100%" };

        // 灵魂医者标志 - 提供灵魂复活服务
        case UNIT_NPC_FLAG_SPIRITHEALER: return { "UNIT_NPC_FLAG_SPIRITHEALER", "is spirit healer", "guessed" };

        // 灵魂向导标志 - 指引灵魂方向
        case UNIT_NPC_FLAG_SPIRITGUIDE: return { "UNIT_NPC_FLAG_SPIRITGUIDE", "is spirit guide", "guessed" };

        // 旅店老板标志 - 提供绑定炉石位置服务
        case UNIT_NPC_FLAG_INNKEEPER: return { "UNIT_NPC_FLAG_INNKEEPER", "is innkeeper", "" };

        // 银行员标志 - 提供银行访问服务
        case UNIT_NPC_FLAG_BANKER: return { "UNIT_NPC_FLAG_BANKER", "is banker", "100%" };

        // 公会申请处理者标志 - 处理公会/竞技场战队申请
        // 0xC0000 = 公会申请, 0x40000 = 竞技场战队申请
        case UNIT_NPC_FLAG_PETITIONER: return { "UNIT_NPC_FLAG_PETITIONER", "handles guild/arena petitions", "100% 0xC0000 = guild petitions, 0x40000 = arena team petitions" };

        // 徽章设计师标志 - 提供公会徽章设计服务
        case UNIT_NPC_FLAG_TABARDDESIGNER: return { "UNIT_NPC_FLAG_TABARDDESIGNER", "is guild tabard designer", "100%" };

        // 战场军官标志 - 提供战场排队服务
        case UNIT_NPC_FLAG_BATTLEMASTER: return { "UNIT_NPC_FLAG_BATTLEMASTER", "is battlemaster", "100%" };

        // 拍卖师标志 - 提供拍卖行访问服务
        case UNIT_NPC_FLAG_AUCTIONEER: return { "UNIT_NPC_FLAG_AUCTIONEER", "is auctioneer", "100%" };

        // 兽栏管理员标志 - 管理宠物兽栏
        case UNIT_NPC_FLAG_STABLEMASTER: return { "UNIT_NPC_FLAG_STABLEMASTER", "is stable master", "100%" };

        // 公会银行员标志 - 提供公会银行访问服务
        // 触发客户端发送 997 操作码
        case UNIT_NPC_FLAG_GUILD_BANKER: return { "UNIT_NPC_FLAG_GUILD_BANKER", "is guild banker", "cause client to send 997 opcode" };

        // 法术点击标志 - 启用法术点击交互
        // 触发客户端发送 1015 操作码(法术点击)
        case UNIT_NPC_FLAG_SPELLCLICK: return { "UNIT_NPC_FLAG_SPELLCLICK", "has spell click enabled", "cause client to send 1015 opcode (spell click)" };

        // 玩家载具标志 - 玩家的坐骑如果具有载具数据应设置此标志
        case UNIT_NPC_FLAG_PLAYER_VEHICLE: return { "UNIT_NPC_FLAG_PLAYER_VEHICLE", "is player vehicle", "players with mounts that have vehicle data should have it set" };

        // 邮箱标志 - NPC 作为邮箱使用
        case UNIT_NPC_FLAG_MAILBOX: return { "UNIT_NPC_FLAG_MAILBOX", "is mailbox", "" };

        // 无效值,抛出异常
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 NPCFlags 枚举的元素总数
 *
 * @tparam NPCFlags 枚举类型模板参数(特化版本)
 * @return size_t 枚举元素总数,固定返回 27
 *
 * @note 该值用于迭代枚举值时的边界检查
 */
template <>
TC_API_EXPORT size_t EnumUtils<NPCFlags>::Count() { return 27; }

/**
 * @brief 根据索引获取 NPCFlags 枚举值
 *
 * 将顺序索引转换为对应的枚举常量值。
 * 索引范围: [0, Count()-1],超出范围将抛出异常。
 *
 * @tparam NPCFlags 枚举类型模板参数(特化版本)
 * @param index 枚举值的顺序索引(从 0 开始)
 * @return NPCFlags 对应的枚举常量值
 * @throws std::out_of_range 当索引超出有效范围时抛出
 *
 * @note 该函数用于数组式访问枚举值,支持枚举迭代
 * @see ToIndex() 反向映射函数
 */
template <>
TC_API_EXPORT NPCFlags EnumUtils<NPCFlags>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return UNIT_NPC_FLAG_GOSSIP;
        case 1: return UNIT_NPC_FLAG_QUESTGIVER;
        case 2: return UNIT_NPC_FLAG_UNK1;
        case 3: return UNIT_NPC_FLAG_UNK2;
        case 4: return UNIT_NPC_FLAG_TRAINER;
        case 5: return UNIT_NPC_FLAG_TRAINER_CLASS;
        case 6: return UNIT_NPC_FLAG_TRAINER_PROFESSION;
        case 7: return UNIT_NPC_FLAG_VENDOR;
        case 8: return UNIT_NPC_FLAG_VENDOR_AMMO;
        case 9: return UNIT_NPC_FLAG_VENDOR_FOOD;
        case 10: return UNIT_NPC_FLAG_VENDOR_POISON;
        case 11: return UNIT_NPC_FLAG_VENDOR_REAGENT;
        case 12: return UNIT_NPC_FLAG_REPAIR;
        case 13: return UNIT_NPC_FLAG_FLIGHTMASTER;
        case 14: return UNIT_NPC_FLAG_SPIRITHEALER;
        case 15: return UNIT_NPC_FLAG_SPIRITGUIDE;
        case 16: return UNIT_NPC_FLAG_INNKEEPER;
        case 17: return UNIT_NPC_FLAG_BANKER;
        case 18: return UNIT_NPC_FLAG_PETITIONER;
        case 19: return UNIT_NPC_FLAG_TABARDDESIGNER;
        case 20: return UNIT_NPC_FLAG_BATTLEMASTER;
        case 21: return UNIT_NPC_FLAG_AUCTIONEER;
        case 22: return UNIT_NPC_FLAG_STABLEMASTER;
        case 23: return UNIT_NPC_FLAG_GUILD_BANKER;
        case 24: return UNIT_NPC_FLAG_SPELLCLICK;
        case 25: return UNIT_NPC_FLAG_PLAYER_VEHICLE;
        case 26: return UNIT_NPC_FLAG_MAILBOX;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将 NPCFlags 枚举值转换为索引
 *
 * 获取枚举常量值对应的顺序索引。
 * 与 FromIndex() 构成双向映射关系。
 *
 * @tparam NPCFlags 枚举类型模板参数(特化版本)
 * @param value 要转换的枚举值
 * @return size_t 对应的顺序索引(从 0 开始)
 * @throws std::out_of_range 当传入无效枚举值时抛出
 *
 * @note 该函数用于将枚举值转换为数组索引,便于数组存储和查找
 * @see FromIndex() 反向映射函数
 */
template <>
TC_API_EXPORT size_t EnumUtils<NPCFlags>::ToIndex(NPCFlags value)
{
    switch (value)
    {
        case UNIT_NPC_FLAG_GOSSIP: return 0;
        case UNIT_NPC_FLAG_QUESTGIVER: return 1;
        case UNIT_NPC_FLAG_UNK1: return 2;
        case UNIT_NPC_FLAG_UNK2: return 3;
        case UNIT_NPC_FLAG_TRAINER: return 4;
        case UNIT_NPC_FLAG_TRAINER_CLASS: return 5;
        case UNIT_NPC_FLAG_TRAINER_PROFESSION: return 6;
        case UNIT_NPC_FLAG_VENDOR: return 7;
        case UNIT_NPC_FLAG_VENDOR_AMMO: return 8;
        case UNIT_NPC_FLAG_VENDOR_FOOD: return 9;
        case UNIT_NPC_FLAG_VENDOR_POISON: return 10;
        case UNIT_NPC_FLAG_VENDOR_REAGENT: return 11;
        case UNIT_NPC_FLAG_REPAIR: return 12;
        case UNIT_NPC_FLAG_FLIGHTMASTER: return 13;
        case UNIT_NPC_FLAG_SPIRITHEALER: return 14;
        case UNIT_NPC_FLAG_SPIRITGUIDE: return 15;
        case UNIT_NPC_FLAG_INNKEEPER: return 16;
        case UNIT_NPC_FLAG_BANKER: return 17;
        case UNIT_NPC_FLAG_PETITIONER: return 18;
        case UNIT_NPC_FLAG_TABARDDESIGNER: return 19;
        case UNIT_NPC_FLAG_BATTLEMASTER: return 20;
        case UNIT_NPC_FLAG_AUCTIONEER: return 21;
        case UNIT_NPC_FLAG_STABLEMASTER: return 22;
        case UNIT_NPC_FLAG_GUILD_BANKER: return 23;
        case UNIT_NPC_FLAG_SPELLCLICK: return 24;
        case UNIT_NPC_FLAG_PLAYER_VEHICLE: return 25;
        case UNIT_NPC_FLAG_MAILBOX: return 26;
        default: throw std::out_of_range("value");
    }
}

} // namespace Trinity::Impl::EnumUtilsImpl
