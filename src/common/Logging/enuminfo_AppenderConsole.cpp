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
 * @file enuminfo_AppenderConsole.cpp
 * @brief 控制台日志输出器颜色枚举信息模块
 *
 * 本文件提供控制台输出器使用的颜色类型枚举（ColorTypes）的元数据信息。
 * 实现了枚举值与字符串之间的转换功能，支持配置文件解析和日志系统初始化。
 *
 * 主要功能：
 * - 定义 ColorTypes 枚举的所有可能取值及其文本表示
 * - 提供枚举值到字符串的转换（ToString）
 * - 提供索引到枚举值的转换（FromIndex）
 * - 提供枚举值到索引的转换（ToIndex）
 *
 * @note 本文件由工具自动生成，不建议手动修改
 */

#include "AppenderConsole.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/********************************************************************\
|* data for enum 'ColorTypes' in 'AppenderConsole.h' auto-generated *|
\********************************************************************/

/**
 * @brief 将 ColorTypes 枚举值转换为文本表示
 *
 * 为每个颜色类型提供三部分信息：
 * - 枚举名称（用于配置文件）
 * - 显示名称（用于用户界面）
 * - 描述文本（用于帮助信息）
 *
 * @param value 颜色类型枚举值
 * @return EnumText 包含枚举名称、显示名称和描述的结构体
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT EnumText EnumUtils<ColorTypes>::ToString(ColorTypes value)
{
    switch (value)
    {
        case BLACK: return { "BLACK", "BLACK", "" };        // 黑色 - 默认前景色
        case RED: return { "RED", "RED", "" };              // 红色 - 用于错误信息
        case GREEN: return { "GREEN", "GREEN", "" };        // 绿色 - 用于成功信息
        case BROWN: return { "BROWN", "BROWN", "" };        // 棕色 - 用于警告信息
        case BLUE: return { "BLUE", "BLUE", "" };           // 蓝色 - 用于调试信息
        case MAGENTA: return { "MAGENTA", "MAGENTA", "" };  // 洋红色 - 用于特殊信息
        case CYAN: return { "CYAN", "CYAN", "" };           // 青色 - 用于提示信息
        case GREY: return { "GREY", "GREY", "" };           // 灰色 - 用于次要信息
        case YELLOW: return { "YELLOW", "YELLOW", "" };     // 黄色 - 用于注意信息
        case LRED: return { "LRED", "LRED", "" };           // 亮红色 - 用于重要错误
        case LGREEN: return { "LGREEN", "LGREEN", "" };     // 亮绿色 - 用于重要成功
        case LBLUE: return { "LBLUE", "LBLUE", "" };        // 亮蓝色 - 用于重要调试
        case LMAGENTA: return { "LMAGENTA", "LMAGENTA", "" }; // 亮洋红色 - 用于重要特殊信息
        case LCYAN: return { "LCYAN", "LCYAN", "" };        // 亮青色 - 用于重要提示
        case WHITE: return { "WHITE", "WHITE", "" };        // 白色 - 用于重要信息
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 ColorTypes 枚举类型的枚举值总数
 *
 * @return size_t 枚举值总数（15个颜色类型）
 */
template <>
TC_API_EXPORT size_t EnumUtils<ColorTypes>::Count() { return 15; }

/**
 * @brief 将索引转换为对应的 ColorTypes 枚举值
 *
 * 用于序列化和反序列化操作，支持按索引顺序访问所有枚举值。
 *
 * @param index 枚举索引（0-14）
 * @return ColorTypes 对应的颜色类型枚举值
 * @throw std::out_of_range 当索引超出有效范围时抛出异常
 */
template <>
TC_API_EXPORT ColorTypes EnumUtils<ColorTypes>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return BLACK;       // 索引0 -> 黑色
        case 1: return RED;         // 索引1 -> 红色
        case 2: return GREEN;       // 索引2 -> 绿色
        case 3: return BROWN;       // 索引3 -> 棕色
        case 4: return BLUE;        // 索引4 -> 蓝色
        case 5: return MAGENTA;     // 索引5 -> 洋红色
        case 6: return CYAN;        // 索引6 -> 青色
        case 7: return GREY;        // 索引7 -> 灰色
        case 8: return YELLOW;      // 索引8 -> 黄色
        case 9: return LRED;        // 索引9 -> 亮红色
        case 10: return LGREEN;     // 索引10 -> 亮绿色
        case 11: return LBLUE;      // 索引11 -> 亮蓝色
        case 12: return LMAGENTA;   // 索引12 -> 亮洋红色
        case 13: return LCYAN;      // 索引13 -> 亮青色
        case 14: return WHITE;      // 索引14 -> 白色
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将 ColorTypes 枚举值转换为对应的索引
 *
 * 用于序列化和反序列化操作，支持将枚举值转换为索引存储。
 *
 * @param value 颜色类型枚举值
 * @return size_t 对应的枚举索引（0-14）
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT size_t EnumUtils<ColorTypes>::ToIndex(ColorTypes value)
{
    switch (value)
    {
        case BLACK: return 0;       // 黑色 -> 索引0
        case RED: return 1;         // 红色 -> 索引1
        case GREEN: return 2;       // 绿色 -> 索引2
        case BROWN: return 3;       // 棕色 -> 索引3
        case BLUE: return 4;        // 蓝色 -> 索引4
        case MAGENTA: return 5;     // 洋红色 -> 索引5
        case CYAN: return 6;        // 青色 -> 索引6
        case GREY: return 7;        // 灰色 -> 索引7
        case YELLOW: return 8;      // 黄色 -> 索引8
        case LRED: return 9;        // 亮红色 -> 索引9
        case LGREEN: return 10;     // 亮绿色 -> 索引10
        case LBLUE: return 11;      // 亮蓝色 -> 索引11
        case LMAGENTA: return 12;   // 亮洋红色 -> 索引12
        case LCYAN: return 13;      // 亮青色 -> 索引13
        case WHITE: return 14;      // 白色 -> 索引14
        default: throw std::out_of_range("value");
    }
}
}
