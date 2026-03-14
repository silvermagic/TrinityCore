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

#include "Errors.h"
#include "StringFormat.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstdarg>

/**
    @file Errors.cpp

    @brief 此文件包含用于报告应用程序严重错误的函数定义

    【重要说明】
    非常重要:绝不能使用 (std::)abort 来代替 *((volatile int*)NULL) = 0;

    在 Windows 平台上,调用 abort() 不会触发未处理异常过滤器 - 这是 WheatyExceptionReport
    用于记录崩溃的机制。此处的 exit(1) 调用是为了让静态分析工具知道,调用本文件中定义的函数
    会终止应用程序。

    【关键点】
    1. Windows 平台:使用 RaiseException 触发异常,让崩溃报告工具捕获
    2. 其他平台:通过写入空指针触发段错误,便于 gdb 调试
    3. 所有错误处理函数都会输出详细错误信息并导致程序终止(Warning除外)
 */

// ============================================================================
// 平台相关的崩溃处理宏定义
// ============================================================================

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
#include <Windows.h>

/**
 * @brief Windows 平台的崩溃处理宏
 *
 * 【职责】
 * 在 Windows 平台上触发异常,让崩溃报告工具(如 WheatyExceptionReport)捕获并记录。
 *
 * 【实现细节】
 * - 使用 RaiseException 触发 EXCEPTION_ASSERTION_FAILURE 异常
 * - 传递两个参数:错误消息和返回地址
 * - 错误消息会被复制到堆上(strdup),确保异常处理时可以访问
 * - 返回地址用于定位崩溃发生的位置
 */
#define Crash(message) \
    ULONG_PTR execeptionArgs[] = { reinterpret_cast<ULONG_PTR>(strdup(message)), reinterpret_cast<ULONG_PTR>(_ReturnAddress()) }; \
    RaiseException(EXCEPTION_ASSERTION_FAILURE, 0, 2, execeptionArgs);
#else

/**
 * @brief 非 Windows 平台的崩溃处理宏
 *
 * 【职责】
 * 在 Linux/Unix 等平台上触发段错误,便于 gdb 调试器捕获和定位问题。
 *
 * 【实现细节】
 * - 将错误消息保存到全局变量 TrinityAssertionFailedMessage,便于 gdb 中查看
 * - 通过向空指针写入数据触发段错误: *((volatile int*)nullptr) = 0
 * - volatile 关键字防止编译器优化掉这个操作
 * - 后续的 exit(1) 用于静态分析工具,实际不会执行到
 *
 * 【调试提示】
 * 在 gdb 中可以通过 print TrinityAssertionFailedMessage 查看最后的断言失败消息
 */
extern "C" { TC_COMMON_API char const* TrinityAssertionFailedMessage = nullptr; }
#define Crash(message) \
    TrinityAssertionFailedMessage = strdup(message); \
    *((volatile int*)nullptr) = 0; \
    exit(1);
#endif

namespace
{
    /**
     * @brief 格式化断言消息
     *
     * 【职责】
     * 根据格式化字符串和可变参数列表,生成格式化后的断言消息字符串。
     * 这是一个内部辅助函数,用于处理断言、错误等消息的格式化。
     *
     * 【参数】
     * @param format - 格式化字符串,包含占位符(如 %s, %d 等)
     * @param args - 可变参数列表,包含要填充到格式化字符串中的实际值
     *
     * 【返回值】
     * std::string - 格式化后的完整消息字符串
     *
     * 【主要流程】
     * 1. 复制 va_list 用于两次遍历(第一次计算长度,第二次实际格式化)
     * 2. 使用 vsnprintf 计算格式化后字符串的长度
     * 3. 调整目标字符串大小以容纳格式化内容
     * 4. 使用 vsnprintf 实际执行格式化操作
     * 5. 返回格式化后的字符串
     */
    std::string FormatAssertionMessage(char const* format, va_list args)
    {
        std::string formatted;
        va_list len;

        va_copy(len, args);
        int32 length = vsnprintf(nullptr, 0, format, len);
        va_end(len);

        formatted.resize(length);
        vsnprintf(&formatted[0], length + 1, format, args);

        return formatted;
    }
}

namespace Trinity
{

/**
 * @brief 断言失败处理函数(简单消息版本)
 *
 * 【职责】
 * 处理断言失败情况,输出详细的错误信息并导致程序崩溃。
 * 当断言条件不满足时调用此函数,用于调试和错误诊断。
 *
 * 【参数】
 * @param file - 断言失败所在的源文件名
 * @param line - 断言失败所在的行号
 * @param function - 断言失败所在的函数名
 * @param debugInfo - 调试信息字符串,包含额外的上下文信息
 * @param message - 断言失败的错误消息
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 格式化断言失败消息,包含文件名、行号、函数名和错误消息
 * 2. 追加调试信息到消息末尾
 * 3. 将完整消息输出到标准错误流(stderr)
 * 4. 刷新错误流确保消息被输出
 * 5. 触发程序崩溃(Crash宏),生成崩溃转储供后续分析
 */
void Assert(char const* file, int line, char const* function, std::string debugInfo, char const* message)
{
    std::string formattedMessage = StringFormat("\n{}:{} in {} ASSERTION FAILED:\n  {}\n", file, line, function, message) + debugInfo + '\n';
    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);
    Crash(formattedMessage.c_str());
}

/**
 * @brief 断言失败处理函数(格式化消息版本)
 *
 * 【职责】
 * 处理断言失败情况,支持格式化的错误消息输出并导致程序崩溃。
 * 这是 Assert 的重载版本,允许使用 printf 风格的格式化参数。
 *
 * 【参数】
 * @param file - 断言失败所在的源文件名
 * @param line - 断言失败所在的行号
 * @param function - 断言失败所在的函数名
 * @param debugInfo - 调试信息字符串,包含额外的上下文信息
 * @param message - 断言失败的基础错误消息
 * @param format - 格式化字符串,用于格式化额外的错误详情
 * @param ... - 可变参数列表,对应 format 中的占位符
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 解析可变参数列表
 * 2. 格式化基础断言失败消息(文件名、行号、函数名、基础消息)
 * 3. 使用 FormatAssertionMessage 格式化额外详情
 * 4. 组合所有信息:基础消息 + 格式化详情 + 调试信息
 * 5. 输出到标准错误流并刷新
 * 6. 触发程序崩溃(Crash宏)
 */
void Assert(char const* file, int line, char const* function, std::string debugInfo, char const* message, char const* format, ...)
{
    va_list args;
    va_start(args, format);

    std::string formattedMessage = StringFormat("\n{}:{} in {} ASSERTION FAILED:\n  {}\n", file, line, function, message) + FormatAssertionMessage(format, args) + '\n' + debugInfo + '\n';
    va_end(args);

    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);

    Crash(formattedMessage.c_str());
}

/**
 * @brief 致命错误处理函数
 *
 * 【职责】
 * 处理致命错误情况,输出错误信息、延迟一段时间后导致程序崩溃。
 * 用于不可恢复的严重错误,比 Assert 更严重,会额外增加10秒延迟。
 *
 * 【参数】
 * @param file - 发生致命错误所在的源文件名
 * @param line - 发生致命错误所在的行号
 * @param function - 发生致命错误所在的函数名
 * @param message - 格式化错误消息字符串
 * @param ... - 可变参数列表,用于消息格式化
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 解析可变参数列表
 * 2. 格式化致命错误消息,包含文件名、行号、函数名
 * 3. 使用 FormatAssertionMessage 格式化具体的错误详情
 * 4. 输出完整消息到标准错误流并刷新
 * 5. 线程休眠10秒,给用户或日志系统时间记录错误
 * 6. 触发程序崩溃(Crash宏)
 */
void Fatal(char const* file, int line, char const* function, char const* message, ...)
{
    va_list args;
    va_start(args, message);

    std::string formattedMessage = StringFormat("\n{}:{} in {} FATAL ERROR:\n", file, line, function) + FormatAssertionMessage(message, args) + '\n';
    va_end(args);

    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);

    std::this_thread::sleep_for(std::chrono::seconds(10));
    Crash(formattedMessage.c_str());
}

/**
 * @brief 错误处理函数
 *
 * 【职责】
 * 处理一般错误情况,输出错误信息并导致程序崩溃。
 * 用于处理严重的错误情况,需要立即终止程序。
 *
 * 【参数】
 * @param file - 发生错误所在的源文件名
 * @param line - 发生错误所在的行号
 * @param function - 发生错误所在的函数名
 * @param message - 错误消息字符串
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 格式化错误消息,包含文件名、行号、函数名和错误详情
 * 2. 输出完整消息到标准错误流
 * 3. 刷新错误流确保消息被输出
 * 4. 触发程序崩溃(Crash宏)
 */
void Error(char const* file, int line, char const* function, char const* message)
{
    std::string formattedMessage = StringFormat("\n{}:{} in {} ERROR:\n  {}\n", file, line, function, message);
    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);
    Crash(formattedMessage.c_str());
}

/**
 * @brief 警告处理函数
 *
 * 【职责】
 * 输出警告信息到标准错误流,但不会导致程序终止。
 * 用于非致命的问题提示,允许程序继续运行。
 *
 * 【参数】
 * @param file - 发生警告所在的源文件名
 * @param line - 发生警告所在的行号
 * @param function - 发生警告所在的函数名
 * @param message - 警告消息字符串
 *
 * 【返回值】
 * void - 无返回值
 *
 * 【主要流程】
 * 1. 格式化警告消息,包含文件名、行号、函数名和警告详情
 * 2. 输出到标准错误流(注意:这里使用 fprintf 而不是 StringFormat)
 * 3. 与其他错误处理函数不同,不触发程序崩溃,仅记录警告
 */
void Warning(char const* file, int line, char const* function, char const* message)
{
    fprintf(stderr, "\n%s:%i in %s WARNING:\n  %s\n",
                   file, line, function, message);
}

/**
 * @brief 程序中止处理函数(简单版本)
 *
 * 【职责】
 * 处理程序中止情况,输出中止位置信息并导致程序崩溃。
 * 用于需要立即中止程序但不带详细消息的情况。
 *
 * 【参数】
 * @param file - 发生中止所在的源文件名
 * @param line - 发生中止所在的行号
 * @param function - 发生中止所在的函数名
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 格式化中止消息,包含文件名、行号和函数名
 * 2. 输出到标准错误流并刷新
 * 3. 触发程序崩溃(Crash宏)
 */
void Abort(char const* file, int line, char const* function)
{
    std::string formattedMessage = StringFormat("\n{}:{} in {} ABORTED.\n", file, line, function);
    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);
    Crash(formattedMessage.c_str());
}

/**
 * @brief 程序中止处理函数(格式化消息版本)
 *
 * 【职责】
 * 处理程序中止情况,支持格式化的错误消息输出并导致程序崩溃。
 * 这是 Abort 的重载版本,允许使用 printf 风格的格式化参数。
 *
 * 【参数】
 * @param file - 发生中止所在的源文件名
 * @param line - 发生中止所在的行号
 * @param function - 发生中止所在的函数名
 * @param message - 格式化中止消息字符串
 * @param ... - 可变参数列表,用于消息格式化
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 解析可变参数列表
 * 2. 格式化基础中止消息(文件名、行号、函数名)
 * 3. 使用 FormatAssertionMessage 格式化具体的错误详情
 * 4. 组合所有信息并输出到标准错误流
 * 5. 刷新错误流确保消息被输出
 * 6. 触发程序崩溃(Crash宏)
 */
void Abort(char const* file, int line, char const* function, char const* message, ...)
{
    va_list args;
    va_start(args, message);

    std::string formattedMessage = StringFormat("\n{}:{} in {} ABORTED:\n", file, line, function) + FormatAssertionMessage(message, args) + '\n';
    va_end(args);

    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);

    Crash(formattedMessage.c_str());
}

/**
 * @brief 信号中止处理函数
 *
 * 【职责】
 * 处理系统信号(如 SIGABRT、SIGSEGV 等),捕获信号并导致程序崩溃。
 * 用作系统信号处理器,当程序收到特定信号时自动调用。
 *
 * 【参数】
 * @param sigval - 接收到的信号值(如 SIGABRT=6, SIGSEGV=11 等)
 *
 * 【返回值】
 * void - 无返回值,函数会导致程序终止
 *
 * 【主要流程】
 * 1. 格式化信号捕获消息,显示接收到的信号值
 * 2. 输出消息到标准错误流并刷新
 * 3. 触发程序崩溃(Crash宏)
 *
 * 【注意事项】
 * 由于是信号处理函数,无法传递额外参数,只能记录信号值。
 * 这是一个全局信号处理器,用于捕获意外信号并生成崩溃转储。
 */
void AbortHandler(int sigval)
{
    // nothing useful to log here, no way to pass args
    std::string formattedMessage = StringFormat("Caught signal {}\n", sigval);
    fprintf(stderr, "%s", formattedMessage.c_str());
    fflush(stderr);
    Crash(formattedMessage.c_str());
}

} // namespace Trinity

/**
 * @brief 获取调试信息
 *
 * 【职责】
 * 获取当前的调试信息字符串,用于在错误输出时提供额外的上下文。
 * 这是一个全局辅助函数,可以在断言或错误发生时调用以获取调试数据。
 *
 * 【参数】
 * 无
 *
 * 【返回值】
 * std::string - 调试信息字符串,当前默认返回空字符串
 *
 * 【主要流程】
 * 1. 返回调试信息字符串(当前实现返回空字符串)
 *
 * 【注意事项】
 * 当前实现返回空字符串,可以根据需要在特定项目中扩展此函数,
 * 以返回有用的调试信息,如堆栈跟踪、变量状态等。
 */
std::string GetDebugInfo()
{
    return "";
}
