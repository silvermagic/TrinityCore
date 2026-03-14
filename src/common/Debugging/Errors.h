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
 * @file Errors.h
 * @brief 错误处理和断言宏定义模块
 *
 * 本文件提供了TrinityCore项目的核心错误处理机制，包括：
 * - 断言宏（ASSERT, WPAssert等）用于运行时条件检查
 * - 错误报告函数用于生成详细的错误信息
 * - 致命错误处理机制
 * - 性能分析模式下的断言优化
 *
 * 这些宏和函数在调试和错误诊断中起着关键作用，
 * 能够在程序出现异常时提供详细的调用堆栈和上下文信息。
 */

#ifndef TRINITYCORE_ERRORS_H
#define TRINITYCORE_ERRORS_H

#include "Define.h"
#include <string>

/**
 * @namespace Trinity
 * @brief TrinityCore核心命名空间，包含错误处理相关的函数
 */
namespace Trinity
{
    /**
     * @brief 断言失败处理函数（带调试信息）
     *
     * 当断言条件失败时调用，输出详细的错误信息并终止程序
     *
     * @param file 发生断言失败的源文件名
     * @param line 发生断言失败的行号
     * @param function 发生断言失败的函数名
     * @param debugInfo 调试信息字符串（可包含变量状态等）
     * @param message 断言失败的消息说明
     */
    [[noreturn]] TC_COMMON_API void Assert(char const* file, int line, char const* function, std::string debugInfo, char const* message);

    /**
     * @brief 断言失败处理函数（带调试信息和格式化消息）
     *
     * 当断言条件失败时调用，输出详细的错误信息并终止程序
     *
     * @param file 发生断言失败的源文件名
     * @param line 发生断言失败的行号
     * @param function 发生断言失败的函数名
     * @param debugInfo 调试信息字符串
     * @param message 断言失败的基础消息
     * @param format 格式化字符串（printf风格）
     * @param ... 格式化参数
     */
    [[noreturn]] TC_COMMON_API void Assert(char const* file, int line, char const* function, std::string debugInfo, char const* message, char const* format, ...) ATTR_PRINTF(6, 7);

    /**
     * @brief 致命错误处理函数
     *
     * 处理不可恢复的致命错误，输出错误信息并终止程序
     *
     * @param file 发生错误的源文件名
     * @param line 发生错误的行号
     * @param function 发生错误的函数名
     * @param message 错误消息格式化字符串
     * @param ... 格式化参数
     */
    [[noreturn]] TC_COMMON_API void Fatal(char const* file, int line, char const* function, char const* message, ...) ATTR_PRINTF(4, 5);

    /**
     * @brief 一般错误处理函数
     *
     * 处理一般性错误，输出错误信息并终止程序
     *
     * @param file 发生错误的源文件名
     * @param line 发生错误的行号
     * @param function 发生错误的函数名
     * @param message 错误消息
     */
    [[noreturn]] TC_COMMON_API void Error(char const* file, int line, char const* function, char const* message);

    /**
     * @brief 程序中止函数（无消息）
     *
     * 立即中止程序执行
     *
     * @param file 发生中止的源文件名
     * @param line 发生中止的行号
     * @param function 发生中止的函数名
     */
    [[noreturn]] TC_COMMON_API void Abort(char const* file, int line, char const* function);

    /**
     * @brief 程序中止函数（带消息）
     *
     * 输出中止消息并立即中止程序执行
     *
     * @param file 发生中止的源文件名
     * @param line 发生中止的行号
     * @param function 发生中止的函数名
     * @param message 中止消息格式化字符串
     * @param ... 格式化参数
     */
    [[noreturn]] TC_COMMON_API void Abort(char const* file, int line, char const* function, char const* message, ...);

    /**
     * @brief 警告处理函数
     *
     * 输出警告信息，但不中止程序执行
     *
     * @param file 发生警告的源文件名
     * @param line 发生警告的行号
     * @param function 发生警告的函数名
     * @param message 警告消息
     */
    TC_COMMON_API void Warning(char const* file, int line, char const* function, char const* message);

    /**
     * @brief 中止信号处理器
     *
     * 处理系统中止信号（如SIGABRT），生成崩溃报告
     *
     * @param sigval 信号值
     */
    [[noreturn]] TC_COMMON_API void AbortHandler(int sigval);

} // namespace Trinity

/**
 * @brief 获取调试信息字符串
 *
 * 生成包含当前程序状态的调试信息，用于错误报告
 *
 * @return std::string 调试信息字符串
 */
TC_COMMON_API std::string GetDebugInfo();

/**
 * @brief 断言宏开始标记（Microsoft编译器专用）
 *
 * 在Microsoft编译器下禁用警告4127（条件表达式为常量）
 * 这是因为断言宏使用do-while(0)结构会触发此警告
 */
#if TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
#define ASSERT_BEGIN __pragma(warning(push)) __pragma(warning(disable: 4127))
#define ASSERT_END __pragma(warning(pop))
#else
#define ASSERT_BEGIN
#define ASSERT_END
#endif

/**
 * @brief Windows平台断言失败异常代码
 *
 * 定义用于标识断言失败的自定义异常代码
 */
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
#define EXCEPTION_ASSERTION_FAILURE 0xC0000420L
#endif

/**
 * @defgroup AssertionMacros 断言宏组
 * @brief 用于运行时条件检查和错误处理的宏定义
 * @{
 */

/**
 * @brief 带调试信息的断言宏
 *
 * 检查条件，如果为false则输出详细的调试信息并终止程序
 *
 * @param cond 断言条件
 * @param ... 可选的格式化消息参数
 */
#define WPAssert(cond, ...) ASSERT_BEGIN do { if (!(cond)) Trinity::Assert(__FILE__, __LINE__, __FUNCTION__, GetDebugInfo(), #cond, ##__VA_ARGS__); } while(0) ASSERT_END

/**
 * @brief 不带调试信息的断言宏
 *
 * 检查条件，如果为false则终止程序，但不输出调试信息
 *
 * @param cond 断言条件
 * @param ... 可选的格式化消息参数
 */
#define WPAssert_NODEBUGINFO(cond, ...) ASSERT_BEGIN do { if (!(cond)) Trinity::Assert(__FILE__, __LINE__, __FUNCTION__, "", #cond, ##__VA_ARGS__); } while(0) ASSERT_END

/**
 * @brief 致命错误检查宏
 *
 * 检查条件，如果为false则触发致命错误处理
 *
 * @param cond 检查条件
 * @param ... 可选的错误消息参数
 */
#define WPFatal(cond, ...) ASSERT_BEGIN do { if (!(cond)) Trinity::Fatal(__FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); } while(0) ASSERT_END

/**
 * @brief 错误检查宏
 *
 * 检查条件，如果为false则触发错误处理
 *
 * @param cond 检查条件
 * @param msg 错误消息
 */
#define WPError(cond, msg) ASSERT_BEGIN do { if (!(cond)) Trinity::Error(__FILE__, __LINE__, __FUNCTION__, (msg)); } while(0) ASSERT_END

/**
 * @brief 警告检查宏
 *
 * 检查条件，如果为false则输出警告信息，但不中止程序
 *
 * @param cond 检查条件
 * @param msg 警告消息
 */
#define WPWarning(cond, msg) ASSERT_BEGIN do { if (!(cond)) Trinity::Warning(__FILE__, __LINE__, __FUNCTION__, (msg)); } while(0) ASSERT_END

/**
 * @brief 程序中止宏（无消息）
 *
 * 立即中止程序执行
 */
#define WPAbort() ASSERT_BEGIN do { Trinity::Abort(__FILE__, __LINE__, __FUNCTION__); } while(0) ASSERT_END

/**
 * @brief 程序中止宏（带消息）
 *
 * 输出中止消息并立即中止程序执行
 *
 * @param msg 中止消息
 * @param ... 可选的格式化参数
 */
#define WPAbort_MSG(msg, ...) ASSERT_BEGIN do { Trinity::Abort(__FILE__, __LINE__, __FUNCTION__, (msg), ##__VA_ARGS__); } while(0) ASSERT_END

/** @} */ // AssertionMacros

/**
 * @brief 条件编译的断言宏定义
 *
 * 在性能分析模式下，断言被禁用以减少性能开销
 * 在正常模式下，使用完整的断言检查
 */
#ifdef PERFORMANCE_PROFILING
#define ASSERT(cond, ...) ((void)0)
#define ASSERT_NODEBUGINFO(cond, ...) ((void)0)
#else
#define ASSERT WPAssert
#define ASSERT_NODEBUGINFO WPAssert_NODEBUGINFO
#endif

/**
 * @brief 带副作用的断言宏
 *
 * 即使在性能分析模式下也会执行的断言宏
 * 用于断言条件中包含需要执行的代码的情况
 */
#define ASSERT_WITH_SIDE_EFFECTS WPAssert

/**
 * @brief 程序中止宏（简化名称）
 */
#define ABORT WPAbort

/**
 * @brief 程序中止宏（简化名称，带消息）
 */
#define ABORT_MSG WPAbort_MSG

/**
 * @brief 非空指针检查实现函数
 *
 * 检查指针是否为空，如果为空则触发断言失败
 *
 * @tparam T 指针类型
 * @param pointer 要检查的指针
 * @param expr 指针表达式的字符串表示（用于错误报告）
 * @return T* 返回原始指针（确保不为空）
 */
template <typename T>
inline T* ASSERT_NOTNULL_IMPL(T* pointer, char const* expr)
{
    ASSERT(pointer, "%s", expr);
    return pointer;
}

/**
 * @brief 非空指针检查宏
 *
 * 检查指针是否为空，如果为空则触发断言失败
 * 这是一个便捷宏，会自动将指针表达式转换为字符串用于错误报告
 *
 * @param pointer 要检查的指针
 */
#define ASSERT_NOTNULL(pointer) ASSERT_NOTNULL_IMPL(pointer, #pointer)

#endif
