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
 * @file Logger.cpp
 * @brief 日志记录器实现
 *
 * 本文件实现了Logger类，提供日志消息分发和Appender管理功能。
 */

#include "Logger.h"
#include "Appender.h"
#include "LogMessage.h"

/**
 * @brief Logger构造函数
 *
 * 初始化日志记录器的名称和日志级别
 *
 * @param _name  日志器名称，用于标识日志类型
 * @param _level 日志级别
 */
Logger::Logger(std::string const& _name, LogLevel _level): name(_name), level(_level) { }

/**
 * @brief 获取日志器名称
 * @return 日志器名称的常量引用
 */
std::string const& Logger::getName() const
{
    return name;
}

/**
 * @brief 获取日志级别
 * @return 当前设置的日志级别
 */
LogLevel Logger::getLogLevel() const
{
    return level;
}

/**
 * @brief 添加Appender到日志器
 *
 * 将指定的Appender注册到日志器，建立ID到指针的映射关系
 *
 * @param id       Appender的唯一标识符
 * @param appender Appender对象指针
 *
 * 说明：
 *   - 如果ID已存在，会覆盖原有的Appender指针
 *   - Logger不获取Appender的所有权，由Log单例负责生命周期管理
 */
void Logger::addAppender(uint8 id, Appender* appender)
{
    appenders[id] = appender;
}

/**
 * @brief 从日志器移除Appender
 *
 * 根据ID移除已注册的Appender
 *
 * @param id 要移除的Appender ID
 *
 * 说明：
 *   仅从Logger的映射表中移除引用，不销毁Appender对象
 */
void Logger::delAppender(uint8 id)
{
    appenders.erase(id);
}

/**
 * @brief 设置日志级别
 *
 * 动态修改日志器的日志级别
 *
 * @param _level 新的日志级别
 */
void Logger::setLogLevel(LogLevel _level)
{
    level = _level;
}

/**
 * @brief 写入日志消息
 *
 * 核心方法，负责将日志消息分发给所有关联的Appender
 *
 * @param message 日志消息指针
 *
 * 主要流程：
 *   1. 级别检查：
 *      - 如果日志器级别为DISABLED(0)，忽略所有消息
 *      - 如果日志器级别高于消息级别，忽略该消息
 *   2. 内容检查：
 *      - 如果消息文本为空，忽略该消息
 *   3. 分发消息：
 *      - 遍历所有已注册的Appender
 *      - 调用每个Appender的write方法
 *
 * 过滤逻辑说明：
 *   日志级别数值越小，级别越高：
 *   - FATAL = 0 (最高)
 *   - ERROR = 1
 *   - WARN  = 2
 *   - INFO  = 3
 *   - DEBUG = 4
 *   - TRACE = 5 (最低)
 *
 *   如果Logger级别为INFO(3)，则：
 *   - FATAL、ERROR、WARN、INFO消息会被处理
 *   - DEBUG、TRACE消息会被过滤
 */
void Logger::write(LogMessage* message) const
{
    // 级别过滤：
    // - level为0表示日志器禁用
    // - level > message->level表示消息级别不够高
    // - message->text.empty()表示空消息
    if (!level || level > message->level || message->text.empty())
    {
        // 调试输出（已注释）
        //fprintf(stderr, "Logger::write: Logger %s, Level %u. Msg %s Level %u WRONG LEVEL MASK OR EMPTY MSG\n",
        //        getName().c_str(), getLogLevel(), message.text.c_str(), message.level);
        return;
    }

    // 遍历所有已注册的Appender，分发日志消息
    for (std::pair<uint8 const, Appender*> const& appender : appenders)
        if (appender.second)
            appender.second->write(message);
}
