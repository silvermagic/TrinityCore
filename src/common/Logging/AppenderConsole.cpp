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
 * @file AppenderConsole.cpp
 * @brief 控制台日志追加器实现文件
 *
 * 本文件实现了AppenderConsole类，负责将日志消息输出到控制台（标准输出或标准错误）。
 * 支持彩色输出功能，能够根据日志级别为不同类型的消息设置不同的颜色。
 *
 * 主要功能：
 *   - 日志输出到控制台（stdout/stderr）
 *   - 彩色输出支持
 *   - 跨平台兼容（Windows和Unix/Linux）
 *
 * 平台实现差异：
 *   Windows平台：
 *     - 使用Windows控制台API (SetConsoleTextAttribute)
 *     - 支持16种标准控制台颜色
 *
 *   Unix/Linux平台：
 *     - 使用ANSI转义序列
 *     - 支持8种标准色和8种高亮色
 *     - 兼容大多数现代终端
 *
 * 输出流选择：
 *   - FATAL、ERROR级别 -> stderr（标准错误流）
 *   - 其他级别 -> stdout（标准输出流）
 */

#include "AppenderConsole.h"
#include "LogMessage.h"
#include "SmartEnum.h"
#include "StringFormat.h"
#include "StringConvert.h"
#include "Util.h"
#include <sstream>

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
  #include <Windows.h>
#endif

/**
 * @brief 构造函数 - 初始化控制台追加器
 *
 * 职责：
 *   初始化控制台日志追加器实例，设置基本属性和颜色配置
 *
 * 参数：
 *   id     - 追加器唯一标识符
 *   name   - 追加器名称
 *   level  - 追加器的日志级别
 *   flags  - 追加器标志位
 *   args   - 配置参数数组，args[3]包含颜色配置字符串
 *
 * 主要流程：
 *   1. 调用基类构造函数初始化基本属性
 *   2. 初始化彩色输出标志为false
 *   3. 将所有日志级别的颜色初始化为默认值（无效颜色）
 *   4. 如果提供了颜色配置参数，调用InitColors进行解析
 */
AppenderConsole::AppenderConsole(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args)
    : Appender(id, name, level, flags), _colored(false)
{
    // 初始化所有日志级别的颜色为无效值
    for (uint8 i = 0; i < NUM_ENABLED_LOG_LEVELS; ++i)
        _colors[i] = ColorTypes(NUM_COLOR_TYPES);

    // 如果提供了颜色配置参数（第4个参数），则初始化颜色
    if (3 < args.size())
        InitColors(name, args[3]);
}

/**
 * @brief 初始化颜色配置
 *
 * 职责：
 *   解析颜色配置字符串，为每个日志级别设置对应的显示颜色
 *
 * 参数：
 *   name - 追加器名称（用于错误消息）
 *   str  - 颜色配置字符串，格式为空格分隔的颜色值列表，每个值对应一个日志级别
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 检查配置字符串是否为空，为空则禁用彩色输出
 *   2. 按空格分割配置字符串，获取各日志级别的颜色值
 *   3. 验证颜色值数量是否与日志级别数量匹配
 *   4. 逐个解析颜色值并验证其有效性
 *   5. 解析成功则启用彩色输出标志
 *
 * 异常：
 *   如果颜色配置格式错误或颜色值无效，抛出InvalidAppenderArgsException
 */
void AppenderConsole::InitColors(std::string const& name, std::string_view str)
{
    // 空字符串则禁用彩色输出
    if (str.empty())
    {
        _colored = false;
        return;
    }

    // 按空格分割颜色配置字符串
    std::vector<std::string_view> colorStrs = Trinity::Tokenize(str, ' ', false);

    // 验证颜色值数量是否正确
    if (colorStrs.size() != NUM_ENABLED_LOG_LEVELS)
    {
        throw InvalidAppenderArgsException(Trinity::StringFormat("Log::CreateAppenderFromConfig: Invalid color data '{}' for console appender {} (expected {} entries, got {})",
            str, name, NUM_ENABLED_LOG_LEVELS, colorStrs.size()));
    }

    // 解析每个日志级别的颜色值
    for (uint8 i = 0; i < NUM_ENABLED_LOG_LEVELS; ++i)
    {
        // 尝试将字符串转换为颜色枚举值
        if (Optional<uint8> color = Trinity::StringTo<uint8>(colorStrs[i]); color && EnumUtils::IsValid<ColorTypes>(*color))
            _colors[i] = static_cast<ColorTypes>(*color);
        else
        {
            // 颜色值无效，抛出异常
            throw InvalidAppenderArgsException(Trinity::StringFormat("Log::CreateAppenderFromConfig: Invalid color '{}' for log level {} on console appender {}",
                colorStrs[i], EnumUtils::ToTitle(static_cast<LogLevel>(i)), name));
        }
    }

    // 颜色初始化成功，启用彩色输出
    _colored = true;
}

/**
 * @brief 设置控制台输出颜色
 *
 * 职责：
 *   根据平台类型设置控制台的前景色（文字颜色）
 *
 * 参数：
 *   stdout_stream - true表示使用标准输出，false表示使用标准错误输出
 *   color         - 要设置的颜色类型（ColorTypes枚举值）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   Windows平台：
 *     1. 使用预定义的Windows控制台颜色属性数组映射颜色
 *     2. 获取对应的控制台句柄（标准输出或标准错误）
 *     3. 调用SetConsoleTextAttribute设置颜色属性
 *
 *   Unix/Linux平台：
 *     1. 使用ANSI转义序列定义颜色码
 *     2. 通过fprintf输出ANSI转义序列设置终端颜色
 *     3. 亮色（BOLD）颜色使用额外的";1"参数
 */
void AppenderConsole::SetColor(bool stdout_stream, ColorTypes color)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows平台：控制台颜色属性映射表
    // 使用Windows API的前景色常量组合定义各种颜色
    static WORD WinColorFG[NUM_COLOR_TYPES] =
    {
        0,                                                  // BLACK - 黑色
        FOREGROUND_RED,                                     // RED - 红色
        FOREGROUND_GREEN,                                   // GREEN - 绿色
        FOREGROUND_RED | FOREGROUND_GREEN,                  // BROWN - 棕色
        FOREGROUND_BLUE,                                    // BLUE - 蓝色
        FOREGROUND_RED |                    FOREGROUND_BLUE, // MAGENTA - 洋红色
        FOREGROUND_GREEN | FOREGROUND_BLUE,                 // CYAN - 青色
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // WHITE - 白色
                                                            // YELLOW - 黄色（亮色）
        FOREGROUND_RED | FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
                                                            // RED_BOLD - 亮红色
        FOREGROUND_RED |                                      FOREGROUND_INTENSITY,
                                                            // GREEN_BOLD - 亮绿色
        FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,             // BLUE_BOLD - 亮蓝色
                                                            // MAGENTA_BOLD - 亮洋红色
        FOREGROUND_RED |                    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // CYAN_BOLD - 亮青色
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // WHITE_BOLD - 亮白色
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
    };

    // 获取控制台句柄并设置颜色属性
    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, WinColorFG[color]);
#else
    // Unix/Linux平台：ANSI转义序列属性定义
    enum ANSITextAttr
    {
        TA_NORMAL                                = 0,     // 正常属性
        TA_BOLD                                  = 1,     // 粗体/高亮
        TA_BLINK                                 = 5,     // 闪烁
        TA_REVERSE                               = 7      // 反色
    };

    // ANSI前景色代码
    enum ANSIFgTextAttr
    {
        FG_BLACK                                 = 30,    // 黑色
        FG_RED,                                           // 红色 (31)
        FG_GREEN,                                         // 绿色 (32)
        FG_BROWN,                                         // 棕色 (33)
        FG_BLUE,                                          // 蓝色 (34)
        FG_MAGENTA,                                       // 洋红色 (35)
        FG_CYAN,                                          // 青色 (36)
        FG_WHITE,                                         // 白色 (37)
        FG_YELLOW                                         // 黄色 (38)
    };

    // ANSI背景色代码
    enum ANSIBgTextAttr
    {
        BG_BLACK                                 = 40,    // 黑色背景
        BG_RED,                                           // 红色背景 (41)
        BG_GREEN,                                         // 绿色背景 (42)
        BG_BROWN,                                         // 棕色背景 (43)
        BG_BLUE,                                          // 蓝色背景 (44)
        BG_MAGENTA,                                       // 洋红色背景 (45)
        BG_CYAN,                                          // 青色背景 (46)
        BG_WHITE                                          // 白色背景 (47)
    };

    // Unix/Linux平台：ANSI颜色码映射表
    static uint8 UnixColorFG[NUM_COLOR_TYPES] =
    {
        FG_BLACK,                                          // BLACK
        FG_RED,                                            // RED
        FG_GREEN,                                          // GREEN
        FG_BROWN,                                          // BROWN
        FG_BLUE,                                           // BLUE
        FG_MAGENTA,                                        // MAGENTA
        FG_CYAN,                                           // CYAN
        FG_WHITE,                                          // WHITE
        FG_YELLOW,                                         // YELLOW
        FG_RED,                                            // LRED（亮红色，使用ANSI粗体属性）
        FG_GREEN,                                          // LGREEN（亮绿色）
        FG_BLUE,                                           // LBLUE（亮蓝色）
        FG_MAGENTA,                                        // LMAGENTA（亮洋红色）
        FG_CYAN,                                           // LCYAN（亮青色）
        FG_WHITE                                           // LWHITE（亮白色）
    };

    // 输出ANSI转义序列设置颜色
    // 格式：\x1b[颜色码;属性m
    // 对于亮色（YELLOW及以上），添加";1"启用粗体/高亮属性
    fprintf((stdout_stream? stdout : stderr), "\x1b[%d%sm", UnixColorFG[color], (color >= YELLOW && color < NUM_COLOR_TYPES ? ";1" : ""));
    #endif
}

/**
 * @brief 重置控制台颜色
 *
 * 职责：
 *   将控制台颜色恢复到默认状态（通常是白色或灰色文字）
 *
 * 参数：
 *   stdout_stream - true表示使用标准输出，false表示使用标准错误输出
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   Windows平台：
 *     1. 获取控制台句柄
 *     2. 设置颜色为红、绿、蓝三色组合（白色文字）
 *
 *   Unix/Linux平台：
 *     1. 输出ANSI转义序列"\x1b[0m"重置所有属性
 */
void AppenderConsole::ResetColor(bool stdout_stream)
{
    #if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows平台：恢复默认颜色（白色文字）
    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
    #else
    // Unix/Linux平台：输出ANSI重置序列
    fprintf((stdout_stream ? stdout : stderr), "\x1b[0m");
    #endif
}

/**
 * @brief 打印字符串到控制台
 *
 * 职责：
 *   将字符串输出到控制台（标准输出或标准错误），自动添加换行符
 *
 * 参数：
 *   str   - 要打印的字符串内容
 *   error - true表示输出到标准错误，false表示输出到标准输出
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   Windows平台：
 *     调用WriteWinConsole处理Unicode字符输出
 *
 *   Unix/Linux平台：
 *     使用utf8printf输出UTF-8编码的字符串
 */
void AppenderConsole::Print(std::string const& str, bool error)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows平台：使用专门的Windows控制台写入函数
    WriteWinConsole(str + "\n", error);
#else
    // Unix/Linux平台：使用UTF-8格式化输出
    utf8printf(error ? stderr : stdout, "%s\n", str.c_str());
#endif
}

/**
 * @brief 写入日志消息到控制台（核心输出函数）
 *
 * 职责：
 *   实现基类的纯虚函数，将日志消息输出到控制台
 *   根据日志级别选择输出流（stdout/stderr）和颜色
 *
 * 参数：
 *   message - 指向日志消息对象的指针，包含日志级别、前缀和文本内容
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 确定输出流：ERROR和FATAL级别输出到stderr，其他级别输出到stdout
 *   2. 如果启用彩色输出：
 *      a. 根据日志级别确定颜色索引
 *      b. 设置对应的前景色
 *      c. 打印日志消息（前缀+文本）
 *      d. 重置颜色
 *   3. 如果未启用彩色输出：
 *      直接打印日志消息
 *
 * 说明：
 *   颜色索引映射：
 *     index 0 -> FATAL（致命错误）
 *     index 1 -> ERROR（错误）
 *     index 2 -> WARN（警告）
 *     index 3 -> INFO（信息）
 *     index 4 -> DEBUG（调试）
 *     index 5 -> TRACE（跟踪）
 */
void AppenderConsole::_write(LogMessage const* message)
{
    // 确定输出流：ERROR和FATAL级别输出到stderr，其他输出到stdout
    bool stdout_stream = !(message->level == LOG_LEVEL_ERROR || message->level == LOG_LEVEL_FATAL);

    // 如果启用了彩色输出
    if (_colored)
    {
        uint8 index;

        // 根据日志级别确定颜色索引
        switch (message->level)
        {
            case LOG_LEVEL_TRACE:    // 跟踪级别
               index = 5;
               break;
            case LOG_LEVEL_DEBUG:    // 调试级别
               index = 4;
               break;
            case LOG_LEVEL_INFO:     // 信息级别
               index = 3;
               break;
            case LOG_LEVEL_WARN:     // 警告级别
               index = 2;
               break;
            case LOG_LEVEL_FATAL:    // 致命错误级别
               index = 0;
               break;
            case LOG_LEVEL_ERROR:    // 错误级别
                [[fallthrough]];     // 显式指定fallthrough
            default:                 // 默认情况（包括ERROR）
               index = 1;
               break;
        }

        // 设置颜色、打印消息、重置颜色
        SetColor(stdout_stream, _colors[index]);
        Print(message->prefix + message->text, !stdout_stream);
        ResetColor(stdout_stream);
    }
    else
    {
        // 未启用彩色输出，直接打印消息
        Print(message->prefix + message->text, !stdout_stream);
    }
}
