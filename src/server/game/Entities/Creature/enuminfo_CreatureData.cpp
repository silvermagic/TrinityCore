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
 * @file enuminfo_CreatureData.cpp
 * @brief CreatureFlagsExtra 枚举工具类的模板特化实现
 *
 * 本文件提供了 CreatureFlagsExtra 枚举类型的枚举工具支持，包括：
 * - 枚举值到文本描述的转换
 * - 枚举值与索引之间的双向映射
 *
 * 这些功能用于调试输出、日志记录、控制台命令解析等场景。
 * 文件内容基于 CreatureData.h 中的 CreatureFlagsExtra 枚举自动生成。
 */

#include "CreatureData.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/*************************************************************************\
|* data for enum 'CreatureFlagsExtra' in 'CreatureData.h' auto-generated *|
\*************************************************************************/

/**
 * @brief CreatureFlagsExtra 枚举的字符串转换模板特化
 *
 * 将 CreatureFlagsExtra 枚举值转换为包含名称、标识符和描述的 EnumText 结构。
 * 该函数用于日志输出、调试信息和控制台命令处理。
 *
 * @tparam CreatureFlagsExtra 枚举类型特化参数
 * @param value 要转换的 CreatureFlagsExtra 枚举值
 * @return EnumText 包含三个字段的文本结构：
 *         - 第一个字段：枚举常量名称（用于代码识别）
 *         - 第二个字段：显示名称（与常量名相同）
 *         - 第三个字段：人类可读的功能描述
 * @throws std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 该函数通过 switch-case 实现 O(1) 时间复杂度的查找
 * @note 所有描述均为英文，遵循 TrinityCore 的国际化规范
 */
template <>
TC_API_EXPORT EnumText EnumUtils<CreatureFlagsExtra>::ToString(CreatureFlagsExtra value)
{
    switch (value)
    {
        // 实例绑定标志：击杀该生物会将实例与击杀者及其队伍绑定
        case CREATURE_FLAG_EXTRA_INSTANCE_BIND: return { "CREATURE_FLAG_EXTRA_INSTANCE_BIND", "CREATURE_FLAG_EXTRA_INSTANCE_BIND", "creature kill bind instance with killer and killer's group" };
        // 平民标志：不会主动攻击，忽略阵营/声望敌对关系
        case CREATURE_FLAG_EXTRA_CIVILIAN: return { "CREATURE_FLAG_EXTRA_CIVILIAN", "CREATURE_FLAG_EXTRA_CIVILIAN", "not aggro (ignore faction/reputation hostility)" };
        // 禁用招架：该生物无法招架攻击
        case CREATURE_FLAG_EXTRA_NO_PARRY: return { "CREATURE_FLAG_EXTRA_NO_PARRY", "CREATURE_FLAG_EXTRA_NO_PARRY", "creature can't parry" };
        // 禁用招架反击：招架后不会进行反击
        case CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN: return { "CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN", "CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN", "creature can't counter-attack at parry" };
        // 禁用格挡：该生物无法格挡攻击
        case CREATURE_FLAG_EXTRA_NO_BLOCK: return { "CREATURE_FLAG_EXTRA_NO_BLOCK", "CREATURE_FLAG_EXTRA_NO_BLOCK", "creature can't block" };
        // 禁用碾压攻击：该生物无法造成碾压伤害
        case CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS: return { "CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS", "CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS", "creature can't do crush attacks" };
        // 无经验值：击杀该生物不提供经验值
        case CREATURE_FLAG_EXTRA_NO_XP: return { "CREATURE_FLAG_EXTRA_NO_XP", "CREATURE_FLAG_EXTRA_NO_XP", "creature kill does not provide XP" };
        // 触发器生物：用于脚本触发或区域检测的隐形生物
        case CREATURE_FLAG_EXTRA_TRIGGER: return { "CREATURE_FLAG_EXTRA_TRIGGER", "CREATURE_FLAG_EXTRA_TRIGGER", "trigger creature" };
        // 免疫嘲讽：免疫嘲讽光环和"攻击我"效果
        case CREATURE_FLAG_EXTRA_NO_TAUNT: return { "CREATURE_FLAG_EXTRA_NO_TAUNT", "CREATURE_FLAG_EXTRA_NO_TAUNT", "creature is immune to taunt auras and 'attack me' effects" };
        // 禁用移动标志更新：不会更新移动标志位
        case CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE: return { "CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE", "CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE", "creature won't update movement flags" };
        // 幽灵可见性：仅对死亡玩家可见
        case CREATURE_FLAG_EXTRA_GHOST_VISIBILITY: return { "CREATURE_FLAG_EXTRA_GHOST_VISIBILITY", "CREATURE_FLAG_EXTRA_GHOST_VISIBILITY", "creature will only be visible to dead players" };
        // 使用副手攻击：启用副手攻击行为
        case CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK: return { "CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK", "CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK", "creature will use offhand attacks" };
        // 禁止出售：玩家无法向此商人出售物品
        case CREATURE_FLAG_EXTRA_NO_SELL_VENDOR: return { "CREATURE_FLAG_EXTRA_NO_SELL_VENDOR", "CREATURE_FLAG_EXTRA_NO_SELL_VENDOR", "players can't sell items to this vendor" };
        // 禁止进入战斗：该生物不允许进入战斗状态
        case CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT: return { "CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT", "CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT", "creature is not allowed to enter combat" };
        // 世界事件标志：用于世界事件生物的自定义标志
        case CREATURE_FLAG_EXTRA_WORLDEVENT: return { "CREATURE_FLAG_EXTRA_WORLDEVENT", "CREATURE_FLAG_EXTRA_WORLDEVENT", "custom flag for world event creatures (left room for merging)" };
        // 卫兵：该生物是卫兵类型
        case CREATURE_FLAG_EXTRA_GUARD: return { "CREATURE_FLAG_EXTRA_GUARD", "CREATURE_FLAG_EXTRA_GUARD", "Creature is guard" };
        // 忽略假死：忽略玩家的假死效果
        case CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH: return { "CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH", "CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH", "creature ignores feign death" };
        // 禁用暴击：该生物无法造成暴击
        case CREATURE_FLAG_EXTRA_NO_CRIT: return { "CREATURE_FLAG_EXTRA_NO_CRIT", "CREATURE_FLAG_EXTRA_NO_CRIT", "creature can't do critical strikes" };
        // 禁用技能提升：攻击该生物不会提升武器技能
        case CREATURE_FLAG_EXTRA_NO_SKILL_GAINS: return { "CREATURE_FLAG_EXTRA_NO_SKILL_GAINS", "CREATURE_FLAG_EXTRA_NO_SKILL_GAINS", "creature won't increase weapon skills" };
        // 遵循嘲讽递减规则：嘲讽效果受递减规则影响
        case CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS: return { "CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS", "CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS", "Taunt is subject to diminishing returns on this creature" };
        // 全递减规则：所有控制效果受递减规则影响（与玩家相同）
        case CREATURE_FLAG_EXTRA_ALL_DIMINISH: return { "CREATURE_FLAG_EXTRA_ALL_DIMINISH", "CREATURE_FLAG_EXTRA_ALL_DIMINISH", "creature is subject to all diminishing returns as players are" };
        // 无玩家伤害要求：击杀时无需玩家造成伤害即可获得击杀奖励
        case CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ: return { "CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ", "CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ", "creature does not need to take player damage for kill credit" };
        // 未使用的标志位 22-27：保留供未来扩展
        case CREATURE_FLAG_EXTRA_UNUSED_22: return { "CREATURE_FLAG_EXTRA_UNUSED_22", "CREATURE_FLAG_EXTRA_UNUSED_22", "" };
        case CREATURE_FLAG_EXTRA_UNUSED_23: return { "CREATURE_FLAG_EXTRA_UNUSED_23", "CREATURE_FLAG_EXTRA_UNUSED_23", "" };
        case CREATURE_FLAG_EXTRA_UNUSED_24: return { "CREATURE_FLAG_EXTRA_UNUSED_24", "CREATURE_FLAG_EXTRA_UNUSED_24", "" };
        case CREATURE_FLAG_EXTRA_UNUSED_25: return { "CREATURE_FLAG_EXTRA_UNUSED_25", "CREATURE_FLAG_EXTRA_UNUSED_25", "" };
        case CREATURE_FLAG_EXTRA_UNUSED_26: return { "CREATURE_FLAG_EXTRA_UNUSED_26", "CREATURE_FLAG_EXTRA_UNUSED_26", "" };
        case CREATURE_FLAG_EXTRA_UNUSED_27: return { "CREATURE_FLAG_EXTRA_UNUSED_27", "CREATURE_FLAG_EXTRA_UNUSED_27", "" };
        // 地下城 Boss：动态设置的标志，切勿在数据库中添加
        case CREATURE_FLAG_EXTRA_DUNGEON_BOSS: return { "CREATURE_FLAG_EXTRA_DUNGEON_BOSS", "CREATURE_FLAG_EXTRA_DUNGEON_BOSS", "creature is a dungeon boss (SET DYNAMICALLY, DO NOT ADD IN DB)" };
        // 忽略寻路：禁用寻路系统，直线移动
        case CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING: return { "CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING", "CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING", "creature ignore pathfinding" };
        // 免疫击退：免疫击退效果
        case CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK: return { "CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK", "CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK", "creature is immune to knockback effects" };
        // 未使用的标志位 31：保留供未来扩展
        case CREATURE_FLAG_EXTRA_UNUSED_31: return { "CREATURE_FLAG_EXTRA_UNUSED_31", "CREATURE_FLAG_EXTRA_UNUSED_31", "" };
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 CreatureFlagsExtra 枚举的元素总数
 *
 * 返回 CreatureFlagsExtra 枚举类型的有效标志位数量。
 * 由于 CreatureFlagsExtra 是位掩码枚举，此值代表可用标志位的总数。
 *
 * @tparam CreatureFlagsExtra 枚举类型特化参数
 * @return size_t 返回固定值 32，表示 32 位标志位
 *
 * @note 该值对应枚举的位宽，用于迭代所有可能的标志位
 */
template <>
TC_API_EXPORT size_t EnumUtils<CreatureFlagsExtra>::Count() { return 32; }

/**
 * @brief 索引转枚举值的模板特化
 *
 * 将线性索引（0 到 Count()-1）转换为对应的 CreatureFlagsExtra 枚举值。
 * 该函数支持枚举值的顺序迭代，常用于枚举遍历和批量处理场景。
 *
 * @tparam CreatureFlagsExtra 枚举类型特化参数
 * @param index 线性索引值，有效范围为 [0, Count()-1]
 * @return CreatureFlagsExtra 对应的枚举常量值
 * @throws std::out_of_range 当索引超出有效范围时抛出异常
 *
 * @note 索引与枚举值之间的关系：
 *       - 索引 0 对应 CREATURE_FLAG_EXTRA_INSTANCE_BIND (值 1 << 0)
 *       - 索引 1 对应 CREATURE_FLAG_EXTRA_CIVILIAN (值 1 << 1)
 *       - 以此类推，遵循位掩码枚举的二进制位顺序
 * @note 时间复杂度：O(1)，通过 switch-case 直接映射
 */
template <>
TC_API_EXPORT CreatureFlagsExtra EnumUtils<CreatureFlagsExtra>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return CREATURE_FLAG_EXTRA_INSTANCE_BIND;
        case 1: return CREATURE_FLAG_EXTRA_CIVILIAN;
        case 2: return CREATURE_FLAG_EXTRA_NO_PARRY;
        case 3: return CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN;
        case 4: return CREATURE_FLAG_EXTRA_NO_BLOCK;
        case 5: return CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS;
        case 6: return CREATURE_FLAG_EXTRA_NO_XP;
        case 7: return CREATURE_FLAG_EXTRA_TRIGGER;
        case 8: return CREATURE_FLAG_EXTRA_NO_TAUNT;
        case 9: return CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE;
        case 10: return CREATURE_FLAG_EXTRA_GHOST_VISIBILITY;
        case 11: return CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK;
        case 12: return CREATURE_FLAG_EXTRA_NO_SELL_VENDOR;
        case 13: return CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT;
        case 14: return CREATURE_FLAG_EXTRA_WORLDEVENT;
        case 15: return CREATURE_FLAG_EXTRA_GUARD;
        case 16: return CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH;
        case 17: return CREATURE_FLAG_EXTRA_NO_CRIT;
        case 18: return CREATURE_FLAG_EXTRA_NO_SKILL_GAINS;
        case 19: return CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS;
        case 20: return CREATURE_FLAG_EXTRA_ALL_DIMINISH;
        case 21: return CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ;
        case 22: return CREATURE_FLAG_EXTRA_UNUSED_22;
        case 23: return CREATURE_FLAG_EXTRA_UNUSED_23;
        case 24: return CREATURE_FLAG_EXTRA_UNUSED_24;
        case 25: return CREATURE_FLAG_EXTRA_UNUSED_25;
        case 26: return CREATURE_FLAG_EXTRA_UNUSED_26;
        case 27: return CREATURE_FLAG_EXTRA_UNUSED_27;
        case 28: return CREATURE_FLAG_EXTRA_DUNGEON_BOSS;
        case 29: return CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING;
        case 30: return CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK;
        case 31: return CREATURE_FLAG_EXTRA_UNUSED_31;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 枚举值转索引的模板特化
 *
 * 将 CreatureFlagsExtra 枚举值转换为对应的线性索引（0 到 Count()-1）。
 * 该函数是 FromIndex 的逆操作，用于确定枚举值在枚举列表中的位置。
 *
 * @tparam CreatureFlagsExtra 枚举类型特化参数
 * @param value 要转换的 CreatureFlagsExtra 枚举值
 * @return size_t 对应的线性索引值，范围为 [0, Count()-1]
 * @throws std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 应用场景：
 *       - 枚举值的数组索引映射
 *       - 序列化和反序列化操作
 *       - 枚举值的比较和排序
 * @note 时间复杂度：O(1)，通过 switch-case 直接映射
 * @note 与 FromIndex 互为逆函数：ToIndex(FromIndex(i)) == i
 */
template <>
TC_API_EXPORT size_t EnumUtils<CreatureFlagsExtra>::ToIndex(CreatureFlagsExtra value)
{
    switch (value)
    {
        case CREATURE_FLAG_EXTRA_INSTANCE_BIND: return 0;
        case CREATURE_FLAG_EXTRA_CIVILIAN: return 1;
        case CREATURE_FLAG_EXTRA_NO_PARRY: return 2;
        case CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN: return 3;
        case CREATURE_FLAG_EXTRA_NO_BLOCK: return 4;
        case CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS: return 5;
        case CREATURE_FLAG_EXTRA_NO_XP: return 6;
        case CREATURE_FLAG_EXTRA_TRIGGER: return 7;
        case CREATURE_FLAG_EXTRA_NO_TAUNT: return 8;
        case CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE: return 9;
        case CREATURE_FLAG_EXTRA_GHOST_VISIBILITY: return 10;
        case CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK: return 11;
        case CREATURE_FLAG_EXTRA_NO_SELL_VENDOR: return 12;
        case CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT: return 13;
        case CREATURE_FLAG_EXTRA_WORLDEVENT: return 14;
        case CREATURE_FLAG_EXTRA_GUARD: return 15;
        case CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH: return 16;
        case CREATURE_FLAG_EXTRA_NO_CRIT: return 17;
        case CREATURE_FLAG_EXTRA_NO_SKILL_GAINS: return 18;
        case CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS: return 19;
        case CREATURE_FLAG_EXTRA_ALL_DIMINISH: return 20;
        case CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ: return 21;
        case CREATURE_FLAG_EXTRA_UNUSED_22: return 22;
        case CREATURE_FLAG_EXTRA_UNUSED_23: return 23;
        case CREATURE_FLAG_EXTRA_UNUSED_24: return 24;
        case CREATURE_FLAG_EXTRA_UNUSED_25: return 25;
        case CREATURE_FLAG_EXTRA_UNUSED_26: return 26;
        case CREATURE_FLAG_EXTRA_UNUSED_27: return 27;
        case CREATURE_FLAG_EXTRA_DUNGEON_BOSS: return 28;
        case CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING: return 29;
        case CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK: return 30;
        case CREATURE_FLAG_EXTRA_UNUSED_31: return 31;
        default: throw std::out_of_range("value");
    }
}

} // namespace Trinity::Impl::EnumUtilsImpl
