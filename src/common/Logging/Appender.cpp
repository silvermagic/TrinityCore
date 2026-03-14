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
 * @file Appender.cpp
 * @brief 日志追加器基类实现
 *
 * 本文件实现了Appender基类，提供日志消息写入的基础框架。
 * 主要包含日志级别过滤、消息前缀格式化等核心功能。
 */

#include "Appender.h"
#include "LogMessage.h"
#include "StringFormat.h"

#include <sstream>

/**
 * @brief Appender构造函数
 *
 * 初始化追加器的基本属性，包括ID、名称、日志级别和标志位
 *
 * @param _id    追加器唯一标识符
 * @param _name  追加器名称
 * @param _level 日志级别，默认为LOG_LEVEL_DISABLED
 * @param _flags 追加器标志位，默认为APPENDER_FLAGS_NONE
 */
Appender::Appender(uint8 _id, std::string const& _name, LogLevel _level /* = LOG_LEVEL_DISABLED */, AppenderFlags _flags /* = APPENDER_FLAGS_NONE */):
id(_id), name(_name), level(_level), flags(_flags) { }

/**
 * @brief Appender析构函数
 *
 * 虚析构函数，确保派生类资源正确释放
 */
Appender::~Appender() { }

/**
 * @brief 获取追加器ID
 * @return 追加器的唯一标识符
 */
uint8 Appender::getId() const
{
    return id;
}

/**
 * @brief 获取追加器名称
 * @return 追加器名称的常量引用
 */
std::string const& Appender::getName() const
{
    return name;
}

/**
 * @brief 获取追加器的日志级别
 * @return 当前设置的日志级别
 */
LogLevel Appender::getLogLevel() const
{
    return level;
}

/**
 * @brief 获取追加器标志
 * @return 追加器标志位，用于控制消息前缀格式
 */
AppenderFlags Appender::getFlags() const
{
    return flags;
}

/**
 * @brief 设置日志级别
 * @param _level 新的日志级别
 *
 * 允许运行时动态调整追加器的日志级别
 */
void Appender::setLogLevel(LogLevel _level)
{
    level = _level;
}

/**
 * @brief 写入日志消息
 *
 * 核心写入函数，负责日志级别过滤和消息格式化
 *
 * @param message 要写入的日志消息指针
 *
 * 主要流程：
 *   1. 级别过滤：如果追加器级别为DISABLED或级别高于消息级别，则忽略该消息
 *   2. 生成前缀：根据flags添加时间戳、日志级别、过滤器类型
 *   3. 调用派生类的_write方法执行实际写入
 *
 * 前缀格式示例：
 *   - 时间戳："2024-03-15_14:30:25 "
 *   - 日志级别："ERROR "
 *   - 过滤器类型："[entities.player] "
 */
void Appender::write(LogMessage* message)
{
    // 级别过滤：level为0表示禁用，level > message->level表示级别不满足
    if (!level || level > message->level)
        return;

    std::ostringstream ss;

    // 如果设置了时间戳前缀标志，添加时间戳
    if (flags & APPENDER_FLAGS_PREFIX_TIMESTAMP)
        ss << message->getTimeStr() << ' ';

    // 如果设置了日志级别前缀标志，添加级别字符串（左对齐，宽度5）
    if (flags & APPENDER_FLAGS_PREFIX_LOGLEVEL)
        ss << Trinity::StringFormat("{:<5} ", Appender::getLogLevelString(message->level));

    // 如果设置了过滤器类型前缀标志，添加过滤器类型
    if (flags & APPENDER_FLAGS_PREFIX_LOGFILTERTYPE)
        ss << '[' << message->type << "] ";

    // 将生成的前缀存储到消息对象中
    message->prefix = ss.str();
    // 调用派生类实现的_write方法执行实际写入
    _write(message);
}

/**
 * @brief 将日志级别转换为字符串
 *
 * 工具函数，将日志级别枚举转换为可读的字符串表示
 *
 * @param level 日志级别枚举值
 * @return 日志级别的字符串表示
 *
 * 返回值：
 *   - LOG_LEVEL_FATAL  -> "FATAL"
 *   - LOG_LEVEL_ERROR  -> "ERROR"
 *   - LOG_LEVEL_WARN   -> "WARN"
 *   - LOG_LEVEL_INFO   -> "INFO"
 *   - LOG_LEVEL_DEBUG  -> "DEBUG"
 *   - LOG_LEVEL_TRACE  -> "TRACE"
 *   - 其他             -> "DISABLED"
 */
char const* Appender::getLogLevelString(LogLevel level)
{
    switch (level)
    {
        case LOG_LEVEL_FATAL:
            return "FATAL";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_WARN:
            return "WARN";
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        case LOG_LEVEL_TRACE:
            return "TRACE";
        default:
            return "DISABLED";
    }
}
