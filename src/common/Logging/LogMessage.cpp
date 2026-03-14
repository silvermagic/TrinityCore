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
 * @file LogMessage.cpp
 * @brief 日志消息结构体实现
 *
 * 本文件实现了LogMessage结构体，提供日志消息的构造和时间戳格式化功能。
 */

#include "LogMessage.h"
#include "StringFormat.h"
#include "Util.h"

/**
 * @brief LogMessage构造函数（基本版本）
 *
 * 创建一个包含基本信息的日志消息对象
 *
 * @param _level 日志级别
 * @param _type  日志类型/过滤器名称
 * @param _text  日志消息内容
 *
 * 初始化：
 *   - 记录日志级别、类型和内容
 *   - 捕获当前系统时间作为消息时间戳
 */
LogMessage::LogMessage(LogLevel _level, std::string_view _type, std::string _text)
    : level(_level), type(_type), text(std::move(_text)), mtime(time(nullptr))
{
}

/**
 * @brief LogMessage构造函数（带附加参数）
 *
 * 创建一个包含附加参数的日志消息对象，用于动态文件名等场景
 *
 * @param _level  日志级别
 * @param _type   日志类型/过滤器名称
 * @param _text   日志消息内容
 * @param _param1 附加参数，用于替换动态文件名中的%s占位符
 *
 * 使用场景：
 *   - 玩家日志：param1可以是"玩家名"或"GUID_玩家名"
 *   - GM命令日志：param1可以是账号ID
 *   - 其他需要动态文件名的日志
 */
LogMessage::LogMessage(LogLevel _level, std::string_view _type, std::string _text, std::string _param1)
    : level(_level), type(_type), text(std::move(_text)), param1(std::move(_param1)), mtime(time(nullptr))
{
}

/**
 * @brief 将时间戳转换为格式化字符串（静态方法）
 *
 * 将Unix时间戳转换为可读的时间字符串格式
 *
 * @param time Unix时间戳
 * @return 格式化的时间字符串（YYYY-MM-DD_HH:MM:SS）
 *
 * 时间格式说明：
 *   - YYYY: 4位年份
 *   - MM:   2位月份（01-12）
 *   - DD:   2位日期（01-31）
 *   - HH:   2位小时（00-23）
 *   - MM:   2位分钟（00-59）
 *   - SS:   2位秒钟（00-59）
 *
 * 示例输出：
 *   2024-03-15_14:30:25
 *
 * 线程安全：
 *   使用localtime_r替代localtime，确保线程安全
 */
std::string LogMessage::getTimeStr(time_t time)
{
    tm aTm;
    // 使用线程安全版本的localtime
    localtime_r(&time, &aTm);
    // 格式化时间：年-月-日_时:分:秒
    return Trinity::StringFormat("{:04}-{:02}-{:02}_{:02}:{:02}:{:02}",
        aTm.tm_year + 1900, aTm.tm_mon + 1, aTm.tm_mday,
        aTm.tm_hour, aTm.tm_min, aTm.tm_sec);
}

/**
 * @brief 获取消息创建时间的格式化字符串
 *
 * 调用静态getTimeStr方法，使用消息对象的mtime成员
 *
 * @return 格式化的时间字符串
 */
std::string LogMessage::getTimeStr() const
{
    return getTimeStr(mtime);
}
