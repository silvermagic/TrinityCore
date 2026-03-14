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
 * @file enuminfo_ArenaTeam.cpp
 * @brief 竞技场队伍类型枚举工具实现文件
 *
 * 本文件为 ArenaTeamTypes 枚举类型提供枚举工具函数的实现，包括:
 * - 枚举值到字符串的转换
 * - 枚举计数
 * - 索引与枚举值的双向转换
 *
 * 这些工具函数用于日志记录、配置解析、调试输出等场景。
 */

#include "ArenaTeam.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/******************************************************************\
|* data for enum 'ArenaTeamTypes' in 'ArenaTeam.h' auto-generated *|
\******************************************************************/

/**
 * @brief 将竞技场队伍类型枚举值转换为文本表示
 *
 * 将 ArenaTeamTypes 枚举值转换为包含名称、显示文本和描述的结构体。
 * 主要用于日志输出、调试信息和配置文件解析。
 *
 * @param value 要转换的竞技场队伍类型枚举值
 * @return EnumText 包含枚举名称、显示文本和描述的结构体
 * @throw std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 性能说明: 使用 switch-case 结构，时间复杂度 O(1)
 * @see ArenaTeamTypes
 */
template <>
TC_API_EXPORT EnumText EnumUtils<ArenaTeamTypes>::ToString(ArenaTeamTypes value)
{
    switch (value)
    {
        case ARENA_TEAM_2v2: return { "ARENA_TEAM_2v2", "ARENA_TEAM_2v2", "" };
        case ARENA_TEAM_3v3: return { "ARENA_TEAM_3v3", "ARENA_TEAM_3v3", "" };
        case ARENA_TEAM_5v5: return { "ARENA_TEAM_5v5", "ARENA_TEAM_5v5", "" };
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取竞技场队伍类型的枚举值总数
 *
 * 返回 ArenaTeamTypes 枚举类型中定义的有效枚举值数量。
 * 当前支持 3 种竞技场类型: 2v2, 3v3, 5v5。
 *
 * @return size_t 枚举值总数，固定返回 3
 *
 * @note 性能说明: 直接返回常量，时间复杂度 O(1)
 */
template <>
TC_API_EXPORT size_t EnumUtils<ArenaTeamTypes>::Count() { return 3; }

/**
 * @brief 根据索引获取对应的竞技场队伍类型枚举值
 *
 * 将顺序索引（0-2）转换为对应的 ArenaTeamTypes 枚举值。
 * 索引顺序: 0=2v2, 1=3v3, 2=5v5
 *
 * @param index 索引值，有效范围 [0, 2]
 * @return ArenaTeamTypes 对应的竞技场队伍类型枚举值
 * @throw std::out_of_range 当索引超出有效范围时抛出异常
 *
 * @note 性能说明: 使用 switch-case 结构，时间复杂度 O(1)
 * @see ToIndex
 */
template <>
TC_API_EXPORT ArenaTeamTypes EnumUtils<ArenaTeamTypes>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return ARENA_TEAM_2v2;
        case 1: return ARENA_TEAM_3v3;
        case 2: return ARENA_TEAM_5v5;
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将竞技场队伍类型枚举值转换为索引
 *
 * 将 ArenaTeamTypes 枚举值转换为顺序索引（0-2）。
 * 索引顺序: 2v2=0, 3v3=1, 5v5=2
 *
 * @param value 竞技场队伍类型枚举值
 * @return size_t 对应的索引值，范围 [0, 2]
 * @throw std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 性能说明: 使用 switch-case 结构，时间复杂度 O(1)
 * @see FromIndex
 */
template <>
TC_API_EXPORT size_t EnumUtils<ArenaTeamTypes>::ToIndex(ArenaTeamTypes value)
{
    switch (value)
    {
        case ARENA_TEAM_2v2: return 0;
        case ARENA_TEAM_3v3: return 1;
        case ARENA_TEAM_5v5: return 2;
        default: throw std::out_of_range("value");
    }
}
}
