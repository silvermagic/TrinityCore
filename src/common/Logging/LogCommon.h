/**
 * @file LogCommon.h
 * @brief 日志系统通用定义头文件
 *
 * 本文件定义了日志系统的核心枚举类型和常量，包括：
 * - LogLevel: 日志级别枚举，定义日志消息的严重程度
 * - AppenderType: 输出器类型枚举，定义日志输出目标
 * - AppenderFlags: 输出器标志枚举，定义输出器的行为特性
 *
 * 这些定义被整个日志系统共享使用，是日志系统的基础类型定义。
 *
 * @author TrinityCore Team
 * @copyright GNU General Public License version 2
 */

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

#ifndef LogCommon_h__
#define LogCommon_h__

#include "Define.h"

/**
 * @enum LogLevel
 * @brief 日志级别枚举
 *
 * 定义日志消息的严重程度级别，用于过滤和分类日志输出。
 * 日志级别按严重程度递增排序，数值越大表示越严重。
 *
 * 级别说明：
 * - DISABLED: 禁用日志输出
 * - TRACE: 最详细的跟踪信息，用于调试程序流程
 * - DEBUG: 调试信息，用于开发和问题诊断
 * - INFO: 一般信息，记录正常运行状态
 * - WARN: 警告信息，表示潜在问题但不影响运行
 * - ERROR: 错误信息，表示功能异常但系统可继续运行
 * - FATAL: 致命错误，表示严重问题可能导致系统崩溃
 *
 * 使用示例：
 * @code
 * if (ShouldLog("entities.player", LOG_LEVEL_DEBUG))
 *     LOG_DEBUG("entities.player", "Player {} logged in", playerName);
 * @endcode
 */
enum LogLevel : uint8
{
    LOG_LEVEL_DISABLED                           = 0,  ///< 禁用日志输出
    LOG_LEVEL_TRACE                              = 1,  ///< 跟踪级别 - 最详细的调试信息
    LOG_LEVEL_DEBUG                              = 2,  ///< 调试级别 - 开发调试信息
    LOG_LEVEL_INFO                               = 3,  ///< 信息级别 - 一般运行信息
    LOG_LEVEL_WARN                               = 4,  ///< 警告级别 - 潜在问题警告
    LOG_LEVEL_ERROR                              = 5,  ///< 错误级别 - 功能错误
    LOG_LEVEL_FATAL                              = 6,  ///< 致命级别 - 严重错误

    NUM_ENABLED_LOG_LEVELS = LOG_LEVEL_FATAL,    ///< 启用的日志级别数量（不包括DISABLED）
    LOG_LEVEL_INVALID = 0xFF                     ///< 无效日志级别标识
};

/**
 * @enum AppenderType
 * @brief 日志输出器类型枚举
 *
 * 定义日志输出目标的类型，支持控制台、文件、数据库等多种输出方式。
 * 每种输出器类型对应一个具体的Appender实现类。
 *
 * 输出器类型说明：
 * - NONE: 无效或未定义的输出器
 * - CONSOLE: 控制台输出器，将日志输出到标准输出/错误流
 * - FILE: 文件输出器，将日志写入文件系统
 * - DB: 数据库输出器，将日志存储到数据库表中
 *
 * 配置示例：
 * @code
 * # worldserver.conf
 * Appender.Console=1,2,0
 * Appender.Server=2,3,0,Server.log,w
 * @endcode
 */
enum AppenderType : uint8
{
    APPENDER_NONE,        ///< 无输出器（无效类型）
    APPENDER_CONSOLE,     ///< 控制台输出器
    APPENDER_FILE,        ///< 文件输出器
    APPENDER_DB,          ///< 数据库输出器

    APPENDER_INVALID = 0xFF  ///< 无效输出器类型标识
};

/**
 * @enum AppenderFlags
 * @brief 日志输出器标志枚举
 *
 * 定义日志输出器的行为特性标志，可以通过位运算组合多个标志。
 * 这些标志控制日志消息的前缀格式和文件处理行为。
 *
 * 标志说明：
 * - PREFIX_TIMESTAMP: 在日志消息前添加时间戳
 * - PREFIX_LOGLEVEL: 在日志消息前添加日志级别
 * - PREFIX_LOGFILTERTYPE: 在日志消息前添加日志过滤器类型
 * - USE_TIMESTAMP: 在日志文件名中添加时间戳
 * - MAKE_FILE_BACKUP: 创建日志文件备份
 *
 * 使用示例：
 * @code
 * // 组合多个标志：时间戳 + 日志级别 + 过滤器类型
 * AppenderFlags flags = AppenderFlags(
 *     APPENDER_FLAGS_PREFIX_TIMESTAMP |
 *     APPENDER_FLAGS_PREFIX_LOGLEVEL |
 *     APPENDER_FLAGS_PREFIX_LOGFILTERTYPE
 * );
 * @endcode
 */
enum AppenderFlags : uint8
{
    APPENDER_FLAGS_NONE                          = 0x00,  ///< 无标志（默认行为）
    APPENDER_FLAGS_PREFIX_TIMESTAMP              = 0x01,  ///< 添加时间戳前缀
    APPENDER_FLAGS_PREFIX_LOGLEVEL               = 0x02,  ///< 添加日志级别前缀
    APPENDER_FLAGS_PREFIX_LOGFILTERTYPE          = 0x04,  ///< 添加过滤器类型前缀
    APPENDER_FLAGS_USE_TIMESTAMP                 = 0x08,  ///< 文件名包含时间戳
    APPENDER_FLAGS_MAKE_FILE_BACKUP              = 0x10   ///< 创建文件备份
};

#endif // LogCommon_h__
