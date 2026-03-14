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
 * @file StringConvert.h
 * @brief 字符串与数值类型转换工具模块
 *
 * 本模块提供了类型安全的字符串转换功能，包括：
 * - 字符串转换为各种数值类型（整数、浮点数、布尔值）
 * - 数值类型转换为字符串
 * - 支持多种进制（十进制、十六进制、二进制）
 * - 错误处理和可选返回值
 *
 * 主要特点：
 * - 使用 C++17 的 std::from_chars 和 std::to_chars 实现高性能转换
 * - 类型安全，编译期检查类型有效性
 * - 支持可选返回值，转换失败时返回 std::nullopt
 * - 针对不同编译器提供兼容性实现
 *
 * 使用示例：
 * @code
 *   // 字符串转数值
 *   Optional<int32> value = Trinity::StringTo<int32>("12345");
 *   if (value)
 *       int32 num = *value;
 *
 *   // 数值转字符串
 *   std::string str = Trinity::ToString(12345);
 *
 *   // 十六进制转换
 *   Optional<int32> hexValue = Trinity::StringTo<int32>("0xFF", 16);
 * @endcode
 */

#ifndef TRINITY_STRINGCONVERT_H
#define TRINITY_STRINGCONVERT_H

#include "Define.h"
#include "Errors.h"
#include "Optional.h"
#include "Types.h"
#include "Util.h"
#include <charconv>
#include <string>
#include <string_view>
#include <type_traits>

namespace Trinity::Impl::StringConvertImpl
{
    /**
     * @brief 类型转换模板结构体（基础模板）
     *
     * @tparam T 要转换的目标类型
     * @tparam void SFINAE 类型参数，用于特化
     *
     * 该模板是基础模板，对于不支持的类型会触发编译期错误。
     * 通过模板特化为不同类型提供具体的转换实现。
     */
    template <typename T, typename = void> struct For
    {
        // 对于不支持的类型，编译时会触发此静态断言
        static_assert(Trinity::dependant_false_v<T>, "Unsupported type used for ToString or StringTo");
        /*
        static Optional<T> FromString(std::string_view str, ...);
        static std::string ToString(T&& val, ...);
        */
    };

    /**
     * @brief 整数类型转换特化模板
     *
     * @tparam T 整数类型（不包括 bool）
     *
     * 该特化为所有整数类型（int8, int16, int32, int64, uint8, uint16, uint32, uint64）
     * 提供字符串转换功能。
     */
    template <typename T>
    struct For<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
    {
        /**
         * @brief 从字符串解析整数值
         *
         * @param str 要解析的字符串视图
         * @param base 数制基数（默认 10），支持 0（自动检测）、2、10、16
         * @return Optional<T> 解析成功返回整数值，失败返回 std::nullopt
         *
         * 主要流程：
         * 1. 如果 base 为 0，自动检测进制（0x 开头为 16 进制，0b 开头为 2 进制，其他为 10 进制）
         * 2. 使用 std::from_chars 进行解析
         * 3. 检查是否整个字符串都被成功解析
         * 4. 返回解析结果
         *
         * 性能注意事项：
         * - 使用 std::from_chars 比传统的 atoi/stoi 更快
         * - 不进行内存分配
         *
         * 示例：
         * @code
         *   StringTo<int32>("123");        // 返回 123
         *   StringTo<int32>("0xFF", 16);   // 返回 255
         *   StringTo<int32>("0b1010", 0);  // 返回 10（自动检测二进制）
         *   StringTo<int32>("abc");        // 返回 std::nullopt
         * @endcode
         */
        static Optional<T> FromString(std::string_view str, int base = 10)
        {
            // 自动检测进制
            if (base == 0)
            {
                if (StringEqualI(str.substr(0, 2), "0x"))
                {
                    base = 16;  // 十六进制
                    str.remove_prefix(2);
                }
                else if (StringEqualI(str.substr(0, 2), "0b"))
                {
                    base = 2;   // 二进制
                    str.remove_prefix(2);
                }
                else
                    base = 10;  // 默认十进制

                if (str.empty())
                    return std::nullopt;
            }

            // 使用 std::from_chars 进行高效解析
            char const* const start = str.data();
            char const* const end = (start + str.length());

            T val;
            std::from_chars_result const res = std::from_chars(start, end, val, base);

            // 确保整个字符串都被成功解析
            if ((res.ptr == end) && (res.ec == std::errc()))
                return val;
            else
                return std::nullopt;
        }

        /**
         * @brief 将整数转换为字符串
         *
         * @param val 要转换的整数值
         * @return std::string 转换后的字符串表示
         *
         * 主要流程：
         * 1. 根据整数大小分配适当的缓冲区
         * 2. 使用 std::to_chars 进行转换
         * 3. 调整字符串大小并返回
         *
         * 性能注意事项：
         * - 使用 std::to_chars 比传统的 std::to_string 更快
         * - 预分配固定大小缓冲区避免重新分配
         * - 缓冲区大小：32 位整数 11 字节，64 位整数 20 字节
         */
        static std::string ToString(T val)
        {
            // 根据整数类型大小确定缓冲区大小
            using buffer_size = std::integral_constant<size_t, sizeof(T) < 8 ? 11 : 20>;

            // 分配缓冲区：2^64 是 20 位十进制字符，-(2^63) 包含符号也是 20 位
            std::string buf(buffer_size::value,'\0');
            char* const start = buf.data();
            char* const end = (start + buf.length());

            // 使用 std::to_chars 进行转换
            std::to_chars_result const res = std::to_chars(start, end, val);
            ASSERT(res.ec == std::errc());

            // 调整字符串大小为实际写入的长度
            buf.resize(res.ptr - start);
            return buf;
        }
    };

#ifdef TRINITY_NEED_CHARCONV_WORKAROUND
    /**
     * @brief Clang-7 编译器 bug 的变通方案
     *
     * 在 Clang-7 版本中，std::from_chars 对于 64 位类型会导致链接错误。
     * 这个变通方案使用传统的 std::stoull 和 std::stoll 函数。
     *
     * 如果 Clang 最低要求提升到 >= clang-8，可以移除此代码块
     * 及其在 cmake/compiler/clang/settings.cmake 中的相关检查。
     */

    /**
     * @brief uint64 类型转换特化（Clang-7 变通方案）
     *
     * 使用 std::stoull 替代 std::from_chars 来避免 Clang-7 的链接错误
     */
    template <>
    struct For<uint64, void>
    {
        /**
         * @brief 从字符串解析 64 位无符号整数
         *
         * @param str 要解析的字符串视图
         * @param base 数制基数（默认 10）
         * @return Optional<uint64> 解析成功返回整数值，失败返回 std::nullopt
         *
         * @note 使用 std::stoull 实现，会进行内存分配创建临时 string 对象
         */
        static Optional<uint64> FromString(std::string_view str, int base = 10)
        {
            if (str.empty())
                return std::nullopt;
            try
            {
                size_t n;
                uint64 val = std::stoull(std::string(str), &n, base);
                if (n != str.length())
                    return std::nullopt;
                return val;
            }
            catch (...) { return std::nullopt; }
        }

        /**
         * @brief 将 64 位无符号整数转换为字符串
         *
         * @param val 要转换的整数值
         * @return std::string 转换后的字符串表示
         */
        static std::string ToString(uint64 val)
        {
            return std::to_string(val);
        }
    };

    /**
     * @brief int64 类型转换特化（Clang-7 变通方案）
     *
     * 使用 std::stoll 替代 std::from_chars 来避免 Clang-7 的链接错误
     */
    template <>
    struct For<int64, void>
    {
        /**
         * @brief 从字符串解析 64 位有符号整数
         *
         * @param str 要解析的字符串视图
         * @param base 数制基数（默认 10）
         * @return Optional<int64> 解析成功返回整数值，失败返回 std::nullopt
         *
         * @note 使用 std::stoll 实现，会进行内存分配创建临时 string 对象
         */
        static Optional<int64> FromString(std::string_view str, int base = 10)
        {
            try {
                if (str.empty())
                    return std::nullopt;
                size_t n;
                int64 val = std::stoll(std::string(str), &n, base);
                if (n != str.length())
                    return std::nullopt;
                return val;
            }
            catch (...) { return std::nullopt; }
        }

        /**
         * @brief 将 64 位有符号整数转换为字符串
         *
         * @param val 要转换的整数值
         * @return std::string 转换后的字符串表示
         */
        static std::string ToString(int64 val)
        {
            return std::to_string(val);
        }
    };
#endif

    /**
     * @brief 布尔类型转换特化
     *
     * 为 bool 类型提供字符串转换功能，支持多种布尔值表示形式
     */
    template <>
    struct For<bool, void>
    {
        /**
         * @brief 从字符串解析布尔值
         *
         * @param str 要解析的字符串视图
         * @param strict 是否使用严格模式（默认 0，非严格模式）
         *               - 严格模式：仅接受 "0" 和 "1"
         *               - 非严格模式：接受多种布尔值表示
         * @return Optional<bool> 解析成功返回布尔值，失败返回 std::nullopt
         *
         * 非严格模式支持的真值表示：
         * - "1", "y", "Y", "on", "ON", "yes", "YES", "true", "TRUE"
         *
         * 非严格模式支持的假值表示：
         * - "0", "n", "N", "off", "OFF", "no", "NO", "false", "FALSE"
         *
         * 示例：
         * @code
         *   StringTo<bool>("true");   // 返回 true
         *   StringTo<bool>("yes");    // 返回 true
         *   StringTo<bool>("1");      // 返回 true
         *   StringTo<bool>("0");      // 返回 false
         *   StringTo<bool>("maybe");  // 返回 std::nullopt
         *
         *   // 严格模式
         *   StringTo<bool>("true", 1);  // 返回 std::nullopt（仅接受 "0" 或 "1"）
         *   StringTo<bool>("1", 1);     // 返回 true
         * @endcode
         *
         * @note 参数类型为 int 以匹配其他整数类型的签名
         */
        static Optional<bool> FromString(std::string_view str, int strict = 0)
        {
            if (strict)
            {
                // 严格模式：仅接受 "0" 和 "1"
                if (str == "1")
                    return true;
                if (str == "0")
                    return false;
                return std::nullopt;
            }
            else
            {
                // 非严格模式：接受多种布尔值表示
                if ((str == "1") || StringEqualI(str, "y") || StringEqualI(str, "on") || StringEqualI(str, "yes") || StringEqualI(str, "true"))
                    return true;
                if ((str == "0") || StringEqualI(str, "n") || StringEqualI(str, "off") || StringEqualI(str, "no") || StringEqualI(str, "false"))
                    return false;
                return std::nullopt;
            }
        }

        /**
         * @brief 将布尔值转换为字符串
         *
         * @param val 要转换的布尔值
         * @return std::string 转换后的字符串表示（"1" 或 "0"）
         *
         * @note 总是输出 "1" 或 "0"，不输出 "true"/"false"
         */
        static std::string ToString(bool val)
        {
            return (val ? "1" : "0");
        }
    };

#if TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
    /**
     * @brief 浮点数类型转换特化（Microsoft Visual C++ 版本）
     *
     * @tparam T 浮点数类型（float, double）
     *
     * MSVC 编译器提供了完整的 std::from_chars 浮点数支持，
     * 因此可以直接使用标准库实现。
     */
    template <typename T>
    struct For<T, std::enable_if_t<std::is_floating_point_v<T>>>
    {
        /**
         * @brief 从字符串解析浮点数
         *
         * @param str 要解析的字符串视图
         * @param fmt 字符格式（默认自动检测）
         *            - std::chars_format::general: 通用格式（十进制或科学计数法）
         *            - std::chars_format::hex: 十六进制浮点格式
         * @return Optional<T> 解析成功返回浮点数值，失败返回 std::nullopt
         *
         * 主要流程：
         * 1. 检查字符串是否为空
         * 2. 如果格式未指定，自动检测（0x 开头为十六进制，否则为通用格式）
         * 3. 使用 std::from_chars 进行解析
         * 4. 返回解析结果
         *
         * 示例：
         * @code
         *   StringTo<double>("3.14159");           // 返回 3.14159
         *   StringTo<double>("1.23e10");           // 返回 1.23e10
         *   StringTo<double>("0x1.8p1");           // 返回 3.0（十六进制浮点）
         *   StringTo<double>("abc");               // 返回 std::nullopt
         * @endcode
         */
        static Optional<T> FromString(std::string_view str, std::chars_format fmt = std::chars_format())
        {
            if (str.empty())
                return std::nullopt;

            // 自动检测格式
            if (fmt == std::chars_format())
            {
                if (StringEqualI(str.substr(0, 2), "0x"))
                {
                    fmt = std::chars_format::hex;  // 十六进制浮点
                    str.remove_prefix(2);
                }
                else
                    fmt = std::chars_format::general;  // 通用格式

                if (str.empty())
                    return std::nullopt;
            }

            // 使用 std::from_chars 进行高效解析
            char const* const start = str.data();
            char const* const end = (start + str.length());

            T val;
            std::from_chars_result const res = std::from_chars(start, end, val, fmt);
            if ((res.ptr == end) && (res.ec == std::errc()))
                return val;
            else
                return std::nullopt;
        }

        /**
         * @brief 从字符串解析浮点数（整数格式参数版本）
         *
         * @param str 要解析的字符串视图
         * @param base 数制基数（16 为十六进制，10 为十进制）
         * @return Optional<T> 解析成功返回浮点数值，失败返回 std::nullopt
         *
         * 该重载允许使用整数类型的接口风格进行浮点数转换，
         * 方便模板编程时统一处理所有数值类型。
         */
        // this allows generic converters for all numeric types (easier templating!)
        static Optional<T> FromString(std::string_view str, int base)
        {
            if (base == 16)
                return FromString(str, std::chars_format::hex);
            else if (base == 10)
                return FromString(str, std::chars_format::general);
            else
                return FromString(str, std::chars_format());
        }

        /**
         * @brief 将浮点数转换为字符串
         *
         * @param val 要转换的浮点数值
         * @return std::string 转换后的字符串表示
         *
         * @note 使用 std::to_string 进行转换
         */
        static std::string ToString(T val)
        {
            return std::to_string(val);
        }
    };
#else
    /**
     * @brief 浮点数类型转换特化（非 MSVC 版本）
     *
     * @tparam T 浮点数类型（float, double）
     *
     * 某些编译器（如 libc++）的 std::from_chars 尚不支持 double 类型参数，
     * 因此使用 std::stold 作为替代实现。
     *
     * @todo 一旦 libc++ 支持 double 参数的 from_chars，应替换此实现
     */
    template <typename T>
    struct For<T, std::enable_if_t<std::is_floating_point_v<T>>>
    {
        /**
         * @brief 从字符串解析浮点数
         *
         * @param str 要解析的字符串视图
         * @param base 数制基数（默认 0，自动检测）
         *             - 0: 自动检测（0x 开头为十六进制，否则为十进制）
         *             - 10: 十进制
         *             - 16: 十六进制
         * @return Optional<T> 解析成功返回浮点数值，失败返回 std::nullopt
         *
         * 主要流程：
         * 1. 检查字符串是否为空
         * 2. 如果是十进制模式但以 0x 开头，返回失败
         * 3. 构建临时字符串（十六进制时添加 "0x" 前缀）
         * 4. 使用 std::stold 进行解析
         * 5. 返回解析结果
         *
         * 性能注意事项：
         * - 需要创建临时 string 对象
         * - 使用 std::stold，性能低于 std::from_chars
         *
         * 示例：
         * @code
         *   StringTo<double>("3.14159");       // 返回 3.14159
         *   StringTo<double>("1.23e10");       // 返回 1.23e10
         *   StringTo<double>("FF", 16);        // 返回 255.0
         * @endcode
         */
        static Optional<T> FromString(std::string_view str, int base = 0)
        {
            try {
                if (str.empty())
                    return std::nullopt;

                // 十进制模式下不允许十六进制前缀
                if ((base == 10) && StringEqualI(str.substr(0, 2), "0x"))
                    return std::nullopt;

                // 构建临时字符串
                std::string tmp;
                if (base == 16)
                    tmp.append("0x");  // std::stold 需要十六进制前缀
                tmp.append(str);

                // 使用 std::stold 进行解析
                size_t n;
                T val = static_cast<T>(std::stold(tmp, &n));
                if (n != tmp.length())
                    return std::nullopt;
                return val;
            }
            catch (...) { return std::nullopt; }
        }

        /**
         * @brief 将浮点数转换为字符串
         *
         * @param val 要转换的浮点数值
         * @return std::string 转换后的字符串表示
         */
        static std::string ToString(T val)
        {
            return std::to_string(val);
        }
    };
#endif
}

namespace Trinity
{
    /**
     * @brief 将字符串转换为指定类型的值
     *
     * @tparam Result 目标类型（int32, uint32, float, double, bool 等）
     * @tparam Params 额外参数类型（如进制基数、格式等）
     * @param str 要转换的字符串视图
     * @param params 传递给底层转换函数的额外参数
     * @return Optional<Result> 转换成功返回目标值，失败返回 std::nullopt
     *
     * 该函数是字符串转数值的主要接口，支持多种数值类型和转换选项。
     *
     * 主要流程：
     * 1. 根据目标类型选择对应的特化模板
     * 2. 调用特化的 FromString 方法进行转换
     * 3. 返回转换结果
     *
     * 使用示例：
     * @code
     *   // 基本用法
     *   Optional<int32> num = StringTo<int32>("12345");
     *   if (num)
     *       std::cout << *num << std::endl;
     *
     *   // 十六进制转换
     *   Optional<int32> hex = StringTo<int32>("FF", 16);
     *
     *   // 浮点数转换
     *   Optional<double> pi = StringTo<double>("3.14159");
     *
     *   // 布尔值转换
     *   Optional<bool> flag = StringTo<bool>("true");
     *
     *   // 错误处理
     *   Optional<int32> invalid = StringTo<int32>("abc");
     *   if (!invalid)
     *       std::cout << "转换失败" << std::endl;
     * @endcode
     *
     * 性能注意事项：
     * - 使用 std::from_chars 实现，性能优于传统方法
     * - 不进行额外的内存分配（部分编译器除外）
     * - 编译期类型检查，无运行时开销
     */
    template <typename Result, typename... Params>
    Optional<Result> StringTo(std::string_view str, Params&&... params)
    {
        return Trinity::Impl::StringConvertImpl::For<Result>::FromString(str, std::forward<Params>(params)...);
    }

    /**
     * @brief 将数值类型转换为字符串
     *
     * @tparam Type 要转换的源类型（int32, uint32, float, double, bool 等）
     * @tparam Params 额外参数类型
     * @param val 要转换的数值
     * @param params 传递给底层转换函数的额外参数
     * @return std::string 转换后的字符串表示
     *
     * 该函数是数值转字符串的主要接口，支持多种数值类型。
     *
     * 主要流程：
     * 1. 根据源类型选择对应的特化模板
     * 2. 调用特化的 ToString 方法进行转换
     * 3. 返回转换结果
     *
     * 使用示例：
     * @code
     *   // 整数转换
     *   std::string str1 = ToString(12345);        // "12345"
     *   std::string str2 = ToString(-9876);        // "-9876"
     *
     *   // 浮点数转换
     *   std::string str3 = ToString(3.14159);      // "3.141590"
     *
     *   // 布尔值转换
     *   std::string str4 = ToString(true);         // "1"
     *   std::string str5 = ToString(false);        // "0"
     * @endcode
     *
     * 性能注意事项：
     * - 使用 std::to_chars 实现，性能优于 std::to_string
     * - 对于整数类型，预分配固定大小缓冲区避免重新分配
     * - 编译期类型检查，无运行时开销
     */
    template <typename Type, typename... Params>
    std::string ToString(Type&& val, Params&&... params)
    {
        return Trinity::Impl::StringConvertImpl::For<std::decay_t<Type>>::ToString(std::forward<Type>(val), std::forward<Params>(params)...);
    }
}

#endif
