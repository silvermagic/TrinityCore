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
 * @file Regex.h
 * @brief 正则表达式封装模块
 *
 * 本文件提供了 TrinityCore 的正则表达式功能封装。
 * 由于标准库的 std::wregex 无法正确处理 DB2 数据文件中提供的正则表达式模式，
 * 因此使用 Boost.Regex 库作为底层实现。
 *
 * 主要功能包括：
 * - 正则表达式类型定义（regex 和 wregex）
 * - 正则匹配和搜索功能
 * - 统一的正则表达式接口，便于未来替换底层实现
 *
 * 使用方式：
 * @code
 * Trinity::regex pattern("[0-9]+");
 * std::string text = "12345";
 * if (Trinity::regex_match(text, pattern)) {
 *     // 匹配成功
 * }
 * @endcode
 */

#ifndef TrinityCore_Regex_h__
#define TrinityCore_Regex_h__

// std::wregex doesn't work with patterns provided in db2 files
// so we have to use boost
// 注意：标准库的 std::wregex 无法正确处理 DB2 数据文件中的正则表达式模式
// 因此必须使用 Boost.Regex 库作为替代实现
#include <boost/regex.hpp>

/// 定义正则表达式命名空间宏，指向 Boost.Regex
/// 这样设计便于未来切换底层实现（如切换到 std::regex 或其他库）
#define TC_REGEX_NAMESPACE boost

namespace Trinity
{
    /// 正则表达式类型（窄字符版本）
    /// 用于处理 std::string 类型的文本匹配
    using regex = TC_REGEX_NAMESPACE :: regex;

    /// 宽字符正则表达式类型
    /// 用于处理 std::wstring 类型的文本匹配
    /// 注意：标准库的 std::wregex 存在兼容性问题，此处使用 Boost 实现
    using wregex = TC_REGEX_NAMESPACE :: wregex;

    /// 导入正则匹配函数
    /// 用于检查整个字符串是否匹配指定的正则表达式模式
    using :: TC_REGEX_NAMESPACE :: regex_match;

    /// 导入正则搜索函数
    /// 用于在字符串中搜索匹配正则表达式模式的子串
    using :: TC_REGEX_NAMESPACE :: regex_search;
}

#endif // TrinityCore_Regex_h__
