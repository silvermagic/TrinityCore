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

#ifndef TRINITY_DEFINE_H
#define TRINITY_DEFINE_H

/**
 * @file Define.h
 * @brief 基础宏定义和类型定义模块
 *
 * 本文件定义了 TrinityCore 项目中广泛使用的基础宏、类型别名和平台相关配置。
 * 主要功能包括：
 * - 编译器特定的宏配置（GCC/MSVC）
 * - 字节序检测和定义
 * - 平台路径长度限制
 * - 调试模式配置
 * - 动态链接库导出/导入宏
 * - 格式化字符串宏
 * - 统一的整数类型别名
 *
 * 这些定义为整个项目提供了平台无关的基础设施支持。
 */

#include "CompilerDefs.h"

/**
 * @brief GCC 编译器特定配置
 *
 * 为 GCC 编译器设置必要的标准宏和线程同步支持。
 * 这些宏确保跨平台的整数格式化和常量定义兼容性。
 */
#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#  if !defined(__STDC_FORMAT_MACROS)
#    define __STDC_FORMAT_MACROS
#  endif
#  if !defined(__STDC_CONSTANT_MACROS)
#    define __STDC_CONSTANT_MACROS
#  endif
  /**
   * @brief 启用 libstdc++ 的纳秒级休眠支持
   *
   * 允许使用 std::this_thread::sleep_for 和 sleep_until 的高精度版本。
   */
#  if !defined(_GLIBCXX_USE_NANOSLEEP)
#    define _GLIBCXX_USE_NANOSLEEP
#  endif
  /**
   * @brief Helgrind 线程检查工具支持
   *
   * 当定义了 HELGRIND 宏时，启用 Valgrind Helgrind 工具的线程同步注解。
   * Helgrind 是一个检测多线程程序中同步错误的工具。
   * 这些注解帮助 Helgrind 理解 C++ 标准库的同步原语。
   */
#  if defined(HELGRIND)
#    include <valgrind/helgrind.h>
#    undef _GLIBCXX_SYNCHRONIZATION_HAPPENS_BEFORE
#    undef _GLIBCXX_SYNCHRONIZATION_HAPPENS_AFTER
    /** @brief 标记同步事件"发生在...之前"的关系 */
#    define _GLIBCXX_SYNCHRONIZATION_HAPPENS_BEFORE(A) ANNOTATE_HAPPENS_BEFORE(A)
    /** @brief 标记同步事件"发生在...之后"的关系 */
#    define _GLIBCXX_SYNCHRONIZATION_HAPPENS_AFTER(A)  ANNOTATE_HAPPENS_AFTER(A)
#  endif
#endif

#include <cstddef>
#include <cinttypes>
#include <climits>

/**
 * @name 字节序常量定义
 * @{
 */
/** @brief 小端字节序标识 */
#define TRINITY_LITTLEENDIAN 0
/** @brief 大端字节序标识 */
#define TRINITY_BIGENDIAN    1
/** @} */

/**
 * @brief 平台字节序检测
 *
 * 自动检测当前平台的字节序。如果未明确定义 TRINITY_ENDIAN，
 * 则根据 Boost 库的 BOOST_BIG_ENDIAN 宏进行判断。
 * 默认为小端字节序（x86/x64 平台）。
 */
#if !defined(TRINITY_ENDIAN)
#  if defined (BOOST_BIG_ENDIAN)
#    define TRINITY_ENDIAN TRINITY_BIG_ENDIAN
#  else
#    define TRINITY_ENDIAN TRINITY_LITTLEENDIAN
#  endif
#endif

/**
 * @brief 路径最大长度定义
 *
 * 不同平台对文件路径长度有不同的限制：
 * - Windows: 使用 MAX_PATH (260 字符)
 * - Unix/Linux: 使用 PATH_MAX (通常为 4096 字符)
 */
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
#  define TRINITY_PATH_MAX 260
#else // TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS
#  define TRINITY_PATH_MAX PATH_MAX
#endif // TRINITY_PLATFORM

/**
 * @name 调试模式配置
 * @{
 */
/**
 * @brief 内联函数配置
 *
 * 在非调试模式下，使用 inline 关键字允许编译器优化。
 * 在调试模式下，禁用内联以便于单步调试和设置断点。
 */
#if !defined(COREDEBUG)
#  define TRINITY_INLINE inline
#else //COREDEBUG
  /** @brief 调试模式标志 */
#  if !defined(TRINITY_DEBUG)
#    define TRINITY_DEBUG
#  endif //TRINITY_DEBUG
#  define TRINITY_INLINE
#endif //!COREDEBUG
/** @} */

/**
 * @brief printf 格式化属性宏
 *
 * GCC 特定的属性，用于检查 printf 风格函数的格式化字符串参数。
 * 参数 F: 格式化字符串参数的位置（从 1 开始计数）
 * 参数 V: 可变参数列表的起始位置
 *
 * 示例：ATTR_PRINTF(1, 2) 表示第 1 个参数是格式化字符串，第 2 个参数开始是可变参数
 *
 * 在非 GCC 编译器上定义为空，保持代码兼容性。
 */
#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#  define ATTR_PRINTF(F, V) __attribute__ ((__format__ (__printf__, F, V)))
#else //TRINITY_COMPILER != TRINITY_COMPILER_GNU
#  define ATTR_PRINTF(F, V)
#endif //TRINITY_COMPILER == TRINITY_COMPILER_GNU

/**
 * @name 动态链接库导出/导入宏
 *
 * 这些宏用于控制符号的可见性和链接行为，支持动态链接库的构建。
 * 在 Windows 上使用 __declspec(dllexport/dllimport)，
 * 在 GCC 上使用 visibility 属性。
 * @{
 */
#ifdef TRINITY_API_USE_DYNAMIC_LINKING
#  if TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
    /** @brief Windows 平台导出符号 */
#    define TC_API_EXPORT __declspec(dllexport)
    /** @brief Windows 平台导入符号 */
#    define TC_API_IMPORT __declspec(dllimport)
#  elif TRINITY_COMPILER == TRINITY_COMPILER_GNU
    /** @brief GCC 平台导出符号（设置为默认可见性） */
#    define TC_API_EXPORT __attribute__((visibility("default")))
    /** @brief GCC 平台导入符号（不需要特殊处理） */
#    define TC_API_IMPORT
#  else
#    error compiler not supported!
#  endif
#else
  /** @brief 静态链接时导出宏为空 */
#  define TC_API_EXPORT
  /** @brief 静态链接时导入宏为空 */
#  define TC_API_IMPORT
#endif

/**
 * @brief Common 库的 API 导出/导入宏
 *
 * 构建 common 库时定义 TRINITY_API_EXPORT_COMMON，
 * 其他模块链接 common 库时不定义此宏。
 */
#ifdef TRINITY_API_EXPORT_COMMON
#  define TC_COMMON_API TC_API_EXPORT
#else
#  define TC_COMMON_API TC_API_IMPORT
#endif

/**
 * @brief Database 库的 API 导出/导入宏
 *
 * 构建 database 库时定义 TRINITY_API_EXPORT_DATABASE，
 * 其他模块链接 database 库时不定义此宏。
 */
#ifdef TRINITY_API_EXPORT_DATABASE
#  define TC_DATABASE_API TC_API_EXPORT
#else
#  define TC_DATABASE_API TC_API_IMPORT
#endif

/**
 * @brief Shared 库的 API 导出/导入宏
 *
 * 构建 shared 库时定义 TRINITY_API_EXPORT_SHARED，
 * 其他模块链接 shared 库时不定义此宏。
 */
#ifdef TRINITY_API_EXPORT_SHARED
#  define TC_SHARED_API TC_API_EXPORT
#else
#  define TC_SHARED_API TC_API_IMPORT
#endif

/**
 * @brief Game 库的 API 导出/导入宏
 *
 * 构建 game 库时定义 TRINITY_API_EXPORT_GAME，
 * 其他模块链接 game 库时不定义此宏。
 */
#ifdef TRINITY_API_EXPORT_GAME
#  define TC_GAME_API TC_API_EXPORT
#else
#  define TC_GAME_API TC_API_IMPORT
#endif
/** @} */

/**
 * @name 格式化字符串宏
 *
 * 提供跨平台的整数和字符串格式化支持。
 * 这些宏确保在 32 位和 64 位系统上都能正确格式化整数。
 * @{
 */

/**
 * @brief uint64 格式化字符串
 *
 * 用于 printf 风格函数中格式化 64 位无符号整数。
 * 示例：printf("Value: " UI64FMTD "\n", value);
 */
#define UI64FMTD "%" PRIu64

/**
 * @brief uint64 字面量宏
 *
 * 用于创建 64 位无符号整数字面量，确保跨平台兼容。
 * 示例：uint64 bigValue = UI64LIT(12345678901234);
 */
#define UI64LIT(N) UINT64_C(N)

/**
 * @brief int64 格式化字符串
 *
 * 用于 printf 风格函数中格式化 64 位有符号整数。
 * 示例：printf("Value: " SI64FMTD "\n", value);
 */
#define SI64FMTD "%" PRId64

/**
 * @brief int64 字面量宏
 *
 * 用于创建 64 位有符号整数字面量，确保跨平台兼容。
 * 示例：int64 bigValue = SI64LIT(-12345678901234);
 */
#define SI64LIT(N) INT64_C(N)

/**
 * @brief size_t 格式化字符串
 *
 * 用于 printf 风格函数中格式化 size_t 类型。
 * 示例：printf("Size: " SZFMTD "\n", bufferSize);
 */
#define SZFMTD "%" PRIuPTR

/**
 * @brief string_view 格式化字符串模板
 *
 * 用于 printf 风格函数中格式化 std::string_view 对象。
 * 使用 %.*s 格式，需要配合 STRING_VIEW_FMT_ARG 宏使用。
 * 示例：printf("String: " STRING_VIEW_FMT "\n", STRING_VIEW_FMT_ARG(strView));
 */
#define STRING_VIEW_FMT "%.*s"

/**
 * @brief string_view 格式化参数宏
 *
 * 将 std::string_view 转换为 printf 可接受的参数格式。
 * 自动提取长度和数据指针。
 *
 * @param str std::string_view 对象
 *
 * 示例：
 * @code
 * std::string_view sv = "Hello World";
 * printf("Text: " STRING_VIEW_FMT "\n", STRING_VIEW_FMT_ARG(sv));
 * @endcode
 */
#define STRING_VIEW_FMT_ARG(str) static_cast<int>((str).length()), (str).data()
/** @} */

/**
 * @name 统一整数类型别名
 *
 * 定义统一的整数类型别名，提高代码可读性和一致性。
 * 这些类型在整个 TrinityCore 项目中广泛使用。
 * @{
 */
typedef int64_t int64;   ///< 64位有符号整数
typedef int32_t int32;   ///< 32位有符号整数
typedef int16_t int16;   ///< 16位有符号整数
typedef int8_t int8;     ///< 8位有符号整数
typedef uint64_t uint64; ///< 64位无符号整数
typedef uint32_t uint32; ///< 32位无符号整数
typedef uint16_t uint16; ///< 16位无符号整数
typedef uint8_t uint8;   ///< 8位无符号整数
/** @} */

#endif //TRINITY_DEFINE_H
