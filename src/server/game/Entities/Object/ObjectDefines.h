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
 * @file ObjectDefines.h
 * @brief 游戏对象核心定义头文件
 *
 * 本文件定义了游戏中所有对象共用的基础常量、枚举类型和工具函数，包括：
 * - 距离常量（接触距离、交互距离、攻击距离等）
 * - 可见性距离类型和默认值
 * - 临时召唤类型枚举
 * - 相位掩码定义
 * - 通知标志位
 * - 位对操作工具函数
 *
 * 这些定义是游戏对象系统的基础，被 Player、Creature、GameObject 等所有实体类型使用。
 */

#ifndef TRINITY_OBJECTDEFINES_H
#define TRINITY_OBJECTDEFINES_H

#include "Define.h"

// ============================================================================
// 距离常量定义
// ============================================================================

/** @brief 接触距离，用于判断两个对象是否处于直接接触状态 */
#define CONTACT_DISTANCE                    0.5f

/** @brief 交互距离，玩家与 NPC 或物体进行交互的最大距离 */
#define INTERACTION_DISTANCE                5.0f

/** @brief 攻击距离，近战攻击的基础判定距离 */
#define ATTACK_DISTANCE                     5.0f

/** @brief 检视距离，玩家检视其他玩家装备信息的最大距离 */
#define INSPECT_DISTANCE                    28.0f

/** @brief 交易距离，玩家之间进行交易的最大距离 */
#define TRADE_DISTANCE                      11.11f

/** @brief 最大可见距离，等于网格大小，对象可见性的理论上限 */
#define MAX_VISIBILITY_DISTANCE             SIZE_OF_GRIDS           // max distance for visible objects

/** @brief 单位视野范围，用于部分视野相关的计算 */
#define SIGHT_RANGE_UNIT                    50.0f

/** @brief 超大可见距离，用于特殊的大型对象或场景 */
#define VISIBILITY_DISTANCE_GIGANTIC        400.0f

/** @brief 大型可见距离，用于较大对象或特殊场景 */
#define VISIBILITY_DISTANCE_LARGE           200.0f

/** @brief 普通可见距离，大多数情况下的默认可见范围 */
#define VISIBILITY_DISTANCE_NORMAL          100.0f

/** @brief 小型可见距离，用于较小对象或受限视野 */
#define VISIBILITY_DISTANCE_SMALL           50.0f

/** @brief 极小可见距离，用于非常小的对象 */
#define VISIBILITY_DISTANCE_TINY            25.0f

/** @brief 默认可见距离，大陆地图上的标准可见范围（100 码） */
#define DEFAULT_VISIBILITY_DISTANCE         VISIBILITY_DISTANCE_NORMAL            // default visible distance, 100 yards on continents

/** @brief 副本默认可见距离，副本内的标准可见范围（170 码） */
#define DEFAULT_VISIBILITY_INSTANCE         170.0f                  // default visible distance in instances, 170 yards

/** @brief 战场/竞技场默认可见距离，PVP 场景的标准可见范围（约 533 码） */
#define DEFAULT_VISIBILITY_BGARENAS         533.0f                  // default visible distance in BG/Arenas, roughly 533 yards

// ============================================================================
// 碰撞体积和攻击范围常量
// ============================================================================

/** @brief 玩家默认边界半径，也用于非 Unit 世界对象的碰撞体积计算 */
#define DEFAULT_PLAYER_BOUNDING_RADIUS      0.388999998569489f     // player size, also currently used (correctly?) for any non Unit world objects

/** @brief 玩家默认战斗触及距离，用于判定攻击是否命中 */
#define DEFAULT_PLAYER_COMBAT_REACH         1.5f

/** @brief 最小近战触及距离 */
#define MIN_MELEE_REACH                     2.0f

/** @brief 名义近战范围 */
#define NOMINAL_MELEE_RANGE                 5.0f

/** @brief 实际近战范围，计算公式：名义范围 - 最小触及距离 * 2（用于玩家中心到中心的判定） */
#define MELEE_RANGE                         (NOMINAL_MELEE_RANGE - MIN_MELEE_REACH * 2) //center to center for players

/** @brief 额外单元格搜索半径，用于在相邻单元格中查找具有巨大战斗触及距离的生物 */
#define EXTRA_CELL_SEARCH_RADIUS            40.0f // We need in some cases increase search radius. Allow to find creatures with huge combat reach in a different nearby cell.

// ============================================================================
// 可见性距离类型枚举
// ============================================================================

/**
 * @enum VisibilityDistanceType
 * @brief 可见性距离类型枚举
 *
 * 定义不同级别的可见性距离，用于根据对象类型或场景动态调整可见范围。
 * 例如，大型 BOSS 可能使用 Gigantic 级别，而小型物体使用 Tiny 级别。
 */
enum class VisibilityDistanceType : uint8
{
    Normal = 0,     ///< 普通可见距离（100 码）
    Tiny = 1,       ///< 极小可见距离（25 码）
    Small = 2,      ///< 小型可见距离（50 码）
    Large = 3,      ///< 大型可见距离（200 码）
    Gigantic = 4,   ///< 超大可见距离（400 码）
    Infinite = 5,   ///< 无限可见距离（特殊用途）

    Max             ///< 枚举计数，用于数组边界检查
};

// ============================================================================
// 临时召唤类型枚举
// ============================================================================

/**
 * @enum TempSummonType
 * @brief 临时召唤类型枚举
 *
 * 定义临时召唤生物的不同消散行为。召唤类型决定了生物在何种条件下会自动消失。
 * 这些类型被 SummonCreature 等函数使用来控制召唤生物的生命周期。
 *
 * @note 召唤生物的消散时机取决于具体的召唤类型，某些类型有多个消散条件（OR 关系）
 */
enum TempSummonType
{
    /**
     * @brief 定时消散或死亡消散
     *
     * 满足以下任一条件时消散：
     * - 指定时间后自动消散
     * - 生物消失时（如被删除）
     */
    TEMPSUMMON_TIMED_OR_DEAD_DESPAWN       = 1,             // despawns after a specified time OR when the creature disappears

    /**
     * @brief 定时消散或尸体消散
     *
     * 满足以下任一条件时消散：
     * - 指定时间后自动消散
     * - 生物死亡时
     */
    TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN     = 2,             // despawns after a specified time OR when the creature dies

    /**
     * @brief 定时消散
     *
     * 在指定时间后自动消散，无论是否在战斗中
     */
    TEMPSUMMON_TIMED_DESPAWN               = 3,             // despawns after a specified time

    /**
     * @brief 脱战后定时消散
     *
     * 生物脱离战斗后开始计时，指定时间后消散
     */
    TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT = 4,             // despawns after a specified time after the creature is out of combat

    /**
     * @brief 尸体即时消散
     *
     * 生物死亡时立即消散，不留下尸体
     */
    TEMPSUMMON_CORPSE_DESPAWN              = 5,             // despawns instantly after death

    /**
     * @brief 尸体定时消散
     *
     * 生物死亡后经过指定时间消散，允许尸体存在一段时间
     */
    TEMPSUMMON_CORPSE_TIMED_DESPAWN        = 6,             // despawns after a specified time after death

    /**
     * @brief 死亡消散
     *
     * 当生物消失时消散（如被删除、传送等）
     */
    TEMPSUMMON_DEAD_DESPAWN                = 7,             // despawns when the creature disappears

    /**
     * @brief 手动消散
     *
     * 仅当调用 UnSummon() 时才消散，不会自动消失
     */
    TEMPSUMMON_MANUAL_DESPAWN              = 8              // despawns when UnSummon() is called
};

// ============================================================================
// 相位掩码枚举
// ============================================================================

/**
 * @enum PhaseMasks
 * @brief 相位掩码枚举
 *
 * 相位系统允许不同玩家看到不同的游戏世界状态。
 * 通过相位掩码可以控制对象的可见性，实现多人副本、任务相位等功能。
 *
 * @note 相位系统使用位掩码，一个对象可以同时处于多个相位
 */
enum PhaseMasks
{
    PHASEMASK_NORMAL   = 0x00000001,  ///< 正常相位，默认可见状态
    PHASEMASK_ANYWHERE = 0xFFFFFFFF   ///< 全相位，在所有相位中都可见
};

// ============================================================================
// 通知标志枚举
// ============================================================================

/**
 * @enum NotifyFlags
 * @brief 通知标志枚举
 *
 * 定义对象状态变化时的通知类型，用于触发相应的更新逻辑。
 * 通过标志位可以组合多种通知类型。
 */
enum NotifyFlags
{
    NOTIFY_NONE                     = 0x00,  ///< 无通知
    NOTIFY_AI_RELOCATION            = 0x01,  ///< AI 重定位通知，当对象位置改变时通知 AI 系统
    NOTIFY_VISIBILITY_CHANGED       = 0x02,  ///< 可见性变化通知，当对象可见性状态改变时触发
    NOTIFY_ALL                      = 0xFF   ///< 所有通知类型
};

// ============================================================================
// 游戏对象召唤类型枚举
// ============================================================================

/**
 * @enum GOSummonType
 * @brief 游戏对象召唤类型枚举
 *
 * 定义游戏对象（GameObject）被召唤后的消散行为。
 * 与 TempSummonType 类似，但专门用于游戏对象（如陷阱、图腾等）。
 */
enum GOSummonType
{
   /**
    * @brief 定时消散或召唤者死亡消散
    *
    * 满足以下任一条件时消散：
    * - 指定时间后自动消散
    * - 召唤者死亡时消散
    */
   GO_SUMMON_TIMED_OR_CORPSE_DESPAWN = 0,    // despawns after a specified time OR when the summoner dies

   /**
    * @brief 定时消散
    *
    * 在指定时间后自动消散
    */
   GO_SUMMON_TIMED_DESPAWN = 1     // despawns after a specified time
};

// ============================================================================
// 位对操作工具函数
// ============================================================================

/**
 * @brief 将两个 32 位无符号整数组合成一个 64 位无符号整数
 *
 * 将低位和高位两部分组合成一个完整的 64 位值。
 * 常用于将两个独立的 ID 或索引组合成唯一的复合标识符。
 *
 * @param l 低 32 位部分
 * @param h 高 32 位部分
 * @return 组合后的 64 位无符号整数
 *
 * @note 此函数为内联函数，编译时展开，无函数调用开销
 *
 * @example
 * @code
 * uint32 lowId = 12345;
 * uint32 highId = 67890;
 * uint64 combined = MAKE_PAIR64(lowId, highId);
 * // combined = 67890 * 2^32 + 12345
 * @endcode
 */
inline uint64 MAKE_PAIR64(uint32 l, uint32 h)
{
    return uint64(l | (uint64(h) << 32));
}

/**
 * @brief 从 64 位无符号整数中提取高 32 位部分
 *
 * 将 64 位值右移 32 位后提取，获取原值的高位部分。
 * 常用于从复合标识符中分离出高位 ID 或索引。
 *
 * @param x 待提取的 64 位无符号整数
 * @return 高 32 位部分
 *
 * @note 此函数为内联函数，性能高效
 *
 * @see MAKE_PAIR64
 * @see PAIR64_LOPART
 */
inline uint32 PAIR64_HIPART(uint64 x)
{
    return (uint32)((x >> 32) & UI64LIT(0x00000000FFFFFFFF));
}

/**
 * @brief 从 64 位无符号整数中提取低 32 位部分
 *
 * 通过位掩码操作提取 64 位值的低位部分。
 * 常用于从复合标识符中分离出低位 ID 或索引。
 *
 * @param x 待提取的 64 位无符号整数
 * @return 低 32 位部分
 *
 * @note 此函数为内联函数，性能高效
 *
 * @see MAKE_PAIR64
 * @see PAIR64_HIPART
 */
inline uint32 PAIR64_LOPART(uint64 x)
{
    return (uint32)(x & UI64LIT(0x00000000FFFFFFFF));
}

/**
 * @brief 将两个 8 位无符号整数组合成一个 16 位无符号整数
 *
 * 将低位和高位两部分组合成一个完整的 16 位值。
 * 常用于网络数据包解析或紧凑数据存储。
 *
 * @param l 低 8 位部分
 * @param h 高 8 位部分
 * @return 组合后的 16 位无符号整数
 *
 * @note 此函数为内联函数，编译时展开，无函数调用开销
 */
inline uint16 MAKE_PAIR16(uint8 l, uint8 h)
{
    return uint16(l | (uint16(h) << 8));
}

/**
 * @brief 将两个 16 位无符号整数组合成一个 32 位无符号整数
 *
 * 将低位和高位两部分组合成一个完整的 32 位值。
 * 常用于将两个独立的 ID 或索引组合成复合标识符。
 *
 * @param l 低 16 位部分
 * @param h 高 16 位部分
 * @return 组合后的 32 位无符号整数
 *
 * @note 此函数为内联函数，编译时展开，无函数调用开销
 *
 * @see PAIR32_HIPART
 * @see PAIR32_LOPART
 */
inline uint32 MAKE_PAIR32(uint16 l, uint16 h)
{
    return uint32(l | (uint32(h) << 16));
}

/**
 * @brief 从 32 位无符号整数中提取高 16 位部分
 *
 * 将 32 位值右移 16 位后提取，获取原值的高位部分。
 * 常用于从复合标识符中分离出高位 ID 或索引。
 *
 * @param x 待提取的 32 位无符号整数
 * @return 高 16 位部分
 *
 * @note 此函数为内联函数，性能高效
 *
 * @see MAKE_PAIR32
 * @see PAIR32_LOPART
 */
inline uint16 PAIR32_HIPART(uint32 x)
{
    return (uint16)((x >> 16) & 0x0000FFFF);
}

/**
 * @brief 从 32 位无符号整数中提取低 16 位部分
 *
 * 通过位掩码操作提取 32 位值的低位部分。
 * 常用于从复合标识符中分离出低位 ID 或索引。
 *
 * @param x 待提取的 32 位无符号整数
 * @return 低 16 位部分
 *
 * @note 此函数为内联函数，性能高效
 *
 * @see MAKE_PAIR32
 * @see PAIR32_HIPART
 */
inline uint16 PAIR32_LOPART(uint32 x)
{
    return (uint16)(x & 0x0000FFFF);
}

#endif
