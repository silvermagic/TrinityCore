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
 * @file enuminfo_LogCommon.cpp
 * @brief 日志系统核心枚举信息模块
 *
 * 本文件提供日志系统的核心枚举类型元数据信息，包括：
 * - LogLevel（日志级别）：定义日志消息的严重程度级别
 * - AppenderType（输出器类型）：定义日志输出目标的类型
 *
 * 主要功能：
 * - 提供枚举值与字符串之间的双向转换功能
 * - 支持配置文件解析和日志系统初始化
 * - 实现枚举值的序列化和反序列化
 *
 * @note 本文件由工具自动生成，不建议手动修改
 */

#include "LogCommon.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/************************************************************\
|* data for enum 'LogLevel' in 'LogCommon.h' auto-generated *|
\************************************************************/

/**
 * @brief 将 LogLevel 枚举值转换为文本表示
 *
 * 为每个日志级别提供三部分信息：
 * - 枚举名称（用于配置文件）
 * - 显示名称（用于用户界面）
 * - 描述文本（用于帮助信息）
 *
 * 日志级别从低到高：
 * - TRACE: 最详细的跟踪信息，用于开发调试
 * - DEBUG: 调试信息，用于问题诊断
 * - INFO: 常规信息，用于正常运行状态
 * - WARN: 警告信息，表示潜在问题
 * - ERROR: 错误信息，表示功能故障
 * - FATAL: 致命错误，导致程序无法继续运行
 *
 * @param value 日志级别枚举值
 * @return EnumText 包含枚举名称、显示名称和描述的结构体
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT EnumText EnumUtils<LogLevel>::ToString(LogLevel value)
{
    switch (value)
    {
        case LOG_LEVEL_DISABLED: return { "LOG_LEVEL_DISABLED", "LOG_LEVEL_DISABLED", "" }; // 禁用日志输出
        case LOG_LEVEL_TRACE: return { "LOG_LEVEL_TRACE", "LOG_LEVEL_TRACE", "" };          // 跟踪级别 - 最详细的调试信息
        case LOG_LEVEL_DEBUG: return { "LOG_LEVEL_DEBUG", "LOG_LEVEL_DEBUG", "" };          // 调试级别 - 用于开发调试
        case LOG_LEVEL_INFO: return { "LOG_LEVEL_INFO", "LOG_LEVEL_INFO", "" };             // 信息级别 - 常规运行信息
        case LOG_LEVEL_WARN: return { "LOG_LEVEL_WARN", "LOG_LEVEL_WARN", "" };             // 警告级别 - 潜在问题提示
        case LOG_LEVEL_ERROR: return { "LOG_LEVEL_ERROR", "LOG_LEVEL_ERROR", "" };          // 错误级别 - 功能故障
        case LOG_LEVEL_FATAL: return { "LOG_LEVEL_FATAL", "LOG_LEVEL_FATAL", "" };          // 致命级别 - 导致程序终止的严重错误
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 LogLevel 枚举类型的枚举值总数
 *
 * @return size_t 枚举值总数（7个日志级别）
 */
template <>
TC_API_EXPORT size_t EnumUtils<LogLevel>::Count() { return 7; }

/**
 * @brief 将索引转换为对应的 LogLevel 枚举值
 *
 * 用于序列化和反序列化操作，支持按索引顺序访问所有日志级别。
 *
 * @param index 枚举索引（0-6）
 * @return LogLevel 对应的日志级别枚举值
 * @throw std::out_of_range 当索引超出有效范围时抛出异常
 */
template <>
TC_API_EXPORT LogLevel EnumUtils<LogLevel>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return LOG_LEVEL_DISABLED;  // 索引0 -> 禁用日志
        case 1: return LOG_LEVEL_TRACE;     // 索引1 -> 跟踪级别
        case 2: return LOG_LEVEL_DEBUG;     // 索引2 -> 调试级别
        case 3: return LOG_LEVEL_INFO;      // 索引3 -> 信息级别
        case 4: return LOG_LEVEL_WARN;      // 索引4 -> 警告级别
        case 5: return LOG_LEVEL_ERROR;     // 索引5 -> 错误级别
        case 6: return LOG_LEVEL_FATAL;     // 索引6 -> 致命级别
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将 LogLevel 枚举值转换为对应的索引
 *
 * 用于序列化和反序列化操作，支持将日志级别转换为索引存储。
 *
 * @param value 日志级别枚举值
 * @return size_t 对应的枚举索引（0-6）
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT size_t EnumUtils<LogLevel>::ToIndex(LogLevel value)
{
    switch (value)
    {
        case LOG_LEVEL_DISABLED: return 0;  // 禁用日志 -> 索引0
        case LOG_LEVEL_TRACE: return 1;     // 跟踪级别 -> 索引1
        case LOG_LEVEL_DEBUG: return 2;     // 调试级别 -> 索引2
        case LOG_LEVEL_INFO: return 3;      // 信息级别 -> 索引3
        case LOG_LEVEL_WARN: return 4;      // 警告级别 -> 索引4
        case LOG_LEVEL_ERROR: return 5;     // 错误级别 -> 索引5
        case LOG_LEVEL_FATAL: return 6;     // 致命级别 -> 索引6
        default: throw std::out_of_range("value");
    }
}

/****************************************************************\
|* data for enum 'AppenderType' in 'LogCommon.h' auto-generated *|
\****************************************************************/

/**
 * @brief 将 AppenderType 枚举值转换为文本表示
 *
 * 为每个输出器类型提供三部分信息：
 * - 枚举名称（用于配置文件）
 * - 显示名称（用于用户界面）
 * - 描述文本（用于帮助信息）
 *
 * 输出器类型说明：
 * - NONE: 无输出器（占位符）
 * - CONSOLE: 控制台输出器，输出到标准输出/错误流
 * - FILE: 文件输出器，输出到磁盘文件
 * - DB: 数据库输出器，输出到数据库表
 *
 * @param value 输出器类型枚举值
 * @return EnumText 包含枚举名称、显示名称和描述的结构体
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT EnumText EnumUtils<AppenderType>::ToString(AppenderType value)
{
    switch (value)
    {
        case APPENDER_NONE: return { "APPENDER_NONE", "APPENDER_NONE", "" };       // 无输出器 - 占位符类型
        case APPENDER_CONSOLE: return { "APPENDER_CONSOLE", "APPENDER_CONSOLE", "" }; // 控制台输出器 - 输出到终端
        case APPENDER_FILE: return { "APPENDER_FILE", "APPENDER_FILE", "" };       // 文件输出器 - 输出到日志文件
        case APPENDER_DB: return { "APPENDER_DB", "APPENDER_DB", "" };             // 数据库输出器 - 输出到数据库
        default: throw std::out_of_range("value");
    }
}

/**
 * @brief 获取 AppenderType 枚举类型的枚举值总数
 *
 * @return size_t 枚举值总数（4个输出器类型）
 */
template <>
TC_API_EXPORT size_t EnumUtils<AppenderType>::Count() { return 4; }

/**
 * @brief 将索引转换为对应的 AppenderType 枚举值
 *
 * 用于序列化和反序列化操作，支持按索引顺序访问所有输出器类型。
 *
 * @param index 枚举索引（0-3）
 * @return AppenderType 对应的输出器类型枚举值
 * @throw std::out_of_range 当索引超出有效范围时抛出异常
 */
template <>
TC_API_EXPORT AppenderType EnumUtils<AppenderType>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return APPENDER_NONE;      // 索引0 -> 无输出器
        case 1: return APPENDER_CONSOLE;   // 索引1 -> 控制台输出器
        case 2: return APPENDER_FILE;      // 索引2 -> 文件输出器
        case 3: return APPENDER_DB;        // 索引3 -> 数据库输出器
        default: throw std::out_of_range("index");
    }
}

/**
 * @brief 将 AppenderType 枚举值转换为对应的索引
 *
 * 用于序列化和反序列化操作，支持将输出器类型转换为索引存储。
 *
 * @param value 输出器类型枚举值
 * @return size_t 对应的枚举索引（0-3）
 * @throw std::out_of_range 当传入无效枚举值时抛出异常
 */
template <>
TC_API_EXPORT size_t EnumUtils<AppenderType>::ToIndex(AppenderType value)
{
    switch (value)
    {
        case APPENDER_NONE: return 0;      // 无输出器 -> 索引0
        case APPENDER_CONSOLE: return 1;   // 控制台输出器 -> 索引1
        case APPENDER_FILE: return 2;      // 文件输出器 -> 索引2
        case APPENDER_DB: return 3;        // 数据库输出器 -> 索引3
        default: throw std::out_of_range("value");
    }
}
}
