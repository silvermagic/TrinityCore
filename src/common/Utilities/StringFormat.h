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
 * @file StringFormat.h
 * @brief 字符串格式化工具模块
 *
 * 本文件提供了一套类型安全的字符串格式化工具函数，基于 {fmt} 库实现。
 * 所有格式化函数都内置异常处理机制，确保在格式化错误时不会导致程序崩溃，
 * 而是返回包含错误信息的字符串。
 *
 * @section FormatRules 格式化规则
 *
 * 使用 {fmt} 库的格式化语法，主要特点：
 * - 使用 {} 作为占位符，支持位置参数和命名参数
 * - 支持格式说明符：{:d} (整数), {:f} (浮点), {:s} (字符串) 等
 * - 支持宽度和精度控制：{:.2f} (保留2位小数), {:10} (宽度10)
 * - 类型安全：编译时检查参数类型匹配
 *
 * 示例：
 * @code
 * StringFormat("Hello, {}!", "World");                    // Hello, World!
 * StringFormat("{0} {1} {0}", "a", "b");                  // a b a
 * StringFormat("Value: {:.2f}", 3.14159);                 // Value: 3.14
 * StringFormat("Int: {:d}, Hex: {:x}", 255, 255);         // Int: 255, Hex: ff
 * @endcode
 *
 * @section Performance 性能注意事项
 *
 * 1. 异常处理开销：所有格式化函数都包含 try-catch 块，在正常情况下性能影响很小，
 *    但在格式化错误时会额外构造错误信息字符串。
 *
 * 2. 编译时检查：使用 FormatString 类型可以在编译时检查格式字符串有效性，
 *    减少运行时错误。
 *
 * 3. 内存分配：格式化函数返回 std::string，会产生内存分配。
 *    对于性能敏感场景，考虑使用 StringFormatTo 直接输出到现有缓冲区。
 *
 * 4. 参数转发：函数使用完美转发传递参数，避免不必要的拷贝。
 *
 * 5. 推荐做法：
 *    - 对于简单格式化，直接使用 StringFormat
 *    - 对于批量格式化或性能敏感场景，使用 StringFormatTo
 *    - 避免在紧密循环中进行字符串格式化
 */

#ifndef TRINITYCORE_STRING_FORMAT_H
#define TRINITYCORE_STRING_FORMAT_H

#include "fmt/core.h"

namespace Trinity
{
    /**
     * @brief 格式化字符串类型
     *
     * 类型安全的格式化字符串类型，在编译时检查格式字符串与参数类型是否匹配。
     * 这是 fmt::format_string 的别名，用于 StringFormat 函数的参数类型。
     *
     * @tparam Args 格式化参数类型列表
     *
     * 示例：
     * @code
     * FormatString<int, std::string> fmt = "Value: {}, Name: {}";
     * StringFormat(fmt, 42, "test");
     * @endcode
     */
    template<typename... Args>
    using FormatString = fmt::format_string<Args...>;

    /**
     * @brief 格式化字符串视图类型
     *
     * 非拥有的字符串视图类型，用于 StringVFormat 函数。
     * 可以接受 const char*、std::string、std::string_view 等多种字符串类型。
     */
    using FormatStringView = fmt::string_view;

    /**
     * @brief 格式化参数包类型
     *
     * 封装的格式化参数列表，用于 StringVFormat 等函数。
     * 可以通过 MakeFormatArgs 函数创建。
     */
    using FormatArgs = fmt::format_args;

    /**
     * @brief 构造格式化参数包
     *
     * 将参数打包为 FormatArgs 类型，用于后续格式化操作。
     * 通常与 StringVFormat 配合使用，支持延迟格式化。
     *
     * @tparam Args 参数类型列表
     * @param args 要打包的参数
     * @return 格式化参数包
     *
     * 示例：
     * @code
     * auto args = MakeFormatArgs(42, "test", 3.14);
     * std::string result = StringVFormat("Int: {}, Str: {}, Float: {}", args);
     * @endcode
     */
    template<typename... Args>
    constexpr auto MakeFormatArgs(Args&&... args) { return fmt::make_format_args(args...); }

    /**
     * @brief TrinityCore 默认字符串格式化函数
     *
     * 使用 {fmt} 库进行类型安全的字符串格式化，内置异常处理机制。
     * 当格式化失败时，不会抛出异常，而是返回包含错误信息的字符串。
     *
     * @tparam Args 可变参数类型列表
     * @param fmt 格式化字符串，使用 {} 作为占位符
     * @param args 格式化参数
     * @return 格式化后的字符串，或错误信息字符串
     *
     * @note 所有异常都会被捕获，不会传播到调用者
     * @note 使用完美转发传递参数，避免不必要的拷贝
     *
     * 示例：
     * @code
     * std::string s1 = StringFormat("Hello, {}!", "World");
     * std::string s2 = StringFormat("Player {} has {} gold", playerName, gold);
     * std::string s3 = StringFormat("Position: ({:.2f}, {:.2f}, {:.2f})", x, y, z);
     * @endcode
     */
    template<typename... Args>
    inline std::string StringFormat(FormatString<Args...> fmt, Args&&... args)
    {
        try
        {
            return fmt::format(fmt, std::forward<Args>(args)...);
        }
        catch (std::exception const& formatError)
        {
            return fmt::format("An error occurred formatting string \"{}\" : {}", fmt, formatError.what());
        }
    }

    /**
     * @brief 格式化字符串到输出迭代器
     *
     * 将格式化结果直接写入指定的输出迭代器，避免额外的字符串拷贝。
     * 适用于需要将格式化结果写入现有缓冲区或容器的场景。
     *
     * @tparam OutputIt 输出迭代器类型
     * @tparam Args 可变参数类型列表
     * @param out 输出迭代器，指向写入位置
     * @param fmt 格式化字符串，使用 {} 作为占位符
     * @param args 格式化参数
     * @return 指向写入结束位置的迭代器
     *
     * @note 所有异常都会被捕获，错误信息会写入输出迭代器
     * @note 性能优于 StringFormat，适用于批量格式化场景
     *
     * 示例：
     * @code
     * std::string buffer;
     * auto it = StringFormatTo(std::back_inserter(buffer), "Value: {}", 42);
     * // buffer 现在包含 "Value: 42"
     *
     * char buffer[256];
     * auto end = StringFormatTo(buffer, "ID: {}, Name: {}", id, name);
     * *end = '\0';  // 添加 null 终止符
     * @endcode
     */
    template<typename OutputIt, typename... Args>
    inline OutputIt StringFormatTo(OutputIt out, FormatString<Args...> fmt, Args&&... args)
    {
        try
        {
            return fmt::format_to(out, fmt, std::forward<Args>(args)...);
        }
        catch (std::exception const& formatError)
        {
            return fmt::format_to(out, "An error occurred formatting string \"{}\" : {}", fmt, formatError.what());
        }
    }

    /**
     * @brief 使用预打包参数进行格式化
     *
     * 接受预先构造的格式化参数包，支持延迟格式化。
     * 适用于需要多次使用相同参数、或者参数在运行时动态构造的场景。
     *
     * @param fmt 格式化字符串视图
     * @param args 预打包的格式化参数
     * @return 格式化后的字符串，或错误信息字符串
     *
     * @note 所有异常都会被捕获，不会传播到调用者
     *
     * 示例：
     * @code
     * auto args = MakeFormatArgs(playerName, level, gold);
     * std::string msg1 = StringVFormat("Player: {}", args);      // 可以多次使用
     * std::string msg2 = StringVFormat("Status: {} Lv.{}", args);
     * @endcode
     */
    inline std::string StringVFormat(FormatStringView fmt, FormatArgs args)
    {
        try
        {
            return fmt::vformat(fmt, args);
        }
        catch (std::exception const& formatError)
        {
            return fmt::format("An error occurred formatting string \"{}\" : {}", fmt, formatError.what());
        }
    }

    /**
     * @brief 使用预打包参数格式化到输出迭代器
     *
     * 结合 StringVFormat 和 StringFormatTo 的特性，支持预打包参数和直接输出。
     * 适用于性能敏感且需要延迟格式化的场景。
     *
     * @tparam OutputIt 输出迭代器类型
     * @param out 输出迭代器，指向写入位置
     * @param fmt 格式化字符串视图
     * @param args 预打包的格式化参数
     * @return 指向写入结束位置的迭代器
     *
     * @note 所有异常都会被捕获，错误信息会写入输出迭代器
     *
     * 示例：
     * @code
     * auto args = MakeFormatArgs(id, name, value);
     * std::string buffer;
     * StringVFormatTo(std::back_inserter(buffer), "Item: {} ({}) = {}", args);
     * @endcode
     */
    template<typename OutputIt>
    inline OutputIt StringVFormatTo(OutputIt out, FormatStringView fmt, FormatArgs args)
    {
        try
        {
            return fmt::vformat_to(out, fmt, args);
        }
        catch (std::exception const& formatError)
        {
            return fmt::format_to(out, "An error occurred formatting string \"{}\" : {}", fmt, formatError.what());
        }
    }

    /**
     * @brief 检查 C 风格字符串指针是否为空或 null
     *
     * 判断给定的 char 指针是否为 nullptr。
     * 用于在格式化前检查字符串指针的有效性。
     *
     * @param fmt 要检查的 C 风格字符串指针
     * @return 如果指针为 nullptr 返回 true，否则返回 false
     *
     * @note 此重载仅检查指针是否为 null，不检查指向的字符串是否为空字符串
     *
     * 示例：
     * @code
     * const char* str = nullptr;
     * if (IsFormatEmptyOrNull(str)) {
     *     // 处理 null 指针情况
     * }
     * @endcode
     */
    inline bool IsFormatEmptyOrNull(char const* fmt)
    {
        return fmt == nullptr;
    }

    /**
     * @brief 检查 std::string 是否为空
     *
     * 判断给定的 std::string 对象是否为空字符串。
     *
     * @param fmt 要检查的字符串引用
     * @return 如果字符串为空返回 true，否则返回 false
     */
    inline bool IsFormatEmptyOrNull(std::string const& fmt)
    {
        return fmt.empty();
    }

    /**
     * @brief 检查 std::string_view 是否为空
     *
     * 判断给定的字符串视图是否为空。
     * 使用 constexpr 允许在编译时进行检查。
     *
     * @param fmt 要检查的字符串视图
     * @return 如果字符串视图为空返回 true，否则返回 false
     *
     * @note constexpr 函数可以在编译时求值
     */
    inline constexpr bool IsFormatEmptyOrNull(std::string_view fmt)
    {
        return fmt.empty();
    }

    /**
     * @brief 检查 fmt::string_view 是否为空
     *
     * 判断给定的 {fmt} 字符串视图是否为空。
     * 使用 constexpr 允许在编译时进行检查。
     *
     * @param fmt 要检查的 {fmt} 字符串视图
     * @return 如果字符串视图大小为 0 返回 true，否则返回 false
     *
     * @note constexpr 函数可以在编译时求值
     */
    inline constexpr bool IsFormatEmptyOrNull(fmt::string_view fmt)
    {
        return fmt.size() == 0;
    }
}

#endif
