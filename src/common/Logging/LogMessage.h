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
 * @file LogMessage.h
 * @brief 日志消息数据结构定义
 *
 * 本文件定义了LogMessage结构体，用于封装单条日志消息的所有信息。
 * LogMessage是日志系统的核心数据结构，承载日志内容从产生到输出的全过程。
 *
 * 主要职责：
 *   - 封装日志消息内容、级别、类型等元数据
 *   - 提供时间戳格式化功能
 *   - 支持附加参数用于动态文件名等场景
 *   - 计算消息大小用于文件大小限制
 */

#ifndef LogMessage_h__
#define LogMessage_h__

#include "Define.h"
#include "LogCommon.h"
#include <string>
#include <ctime>

/**
 * @brief 日志消息结构体
 *
 * LogMessage是日志系统中传递的基本数据单元，封装了单条日志的所有信息。
 * 日志消息在产生后会被传递给Logger，再由Logger分发给各个Appender输出。
 *
 * 数据流：
 *   1. 日志宏(TC_LOG_*)创建LogMessage实例
 *   2. Log::write()异步投递到IO线程
 *   3. Logger::write()分发给各Appender
 *   4. Appender::_write()实际输出到介质
 *
 * 生命周期：
 *   - 创建：在日志宏中通过std::make_unique创建
 *   - 传递：通过unique_ptr在异步队列中传递
 *   - 销毁：输出完成后自动销毁
 *
 * 线程安全：
 *   - 消息创建后不可修改（level、type、text为const）
 *   - prefix可由Appender修改
 *   - 通过unique_ptr确保唯一所有权
 */
struct TC_COMMON_API LogMessage
{
    /**
     * @brief 构造函数（基本版本）
     *
     * @param _level 日志级别
     * @param _type  日志类型/过滤器名称
     * @param _text  日志消息内容
     */
    LogMessage(LogLevel _level, std::string_view _type, std::string _text);

    /**
     * @brief 构造函数（带附加参数）
     *
     * @param _level  日志级别
     * @param _type   日志类型/过滤器名称
     * @param _text   日志消息内容
     * @param _param1 附加参数，用于动态文件名等场景
     */
    LogMessage(LogLevel _level, std::string_view _type, std::string _text, std::string _param1);

    /// 禁用拷贝构造（确保唯一所有权）
    LogMessage(LogMessage const& /*other*/) = delete;
    /// 禁用拷贝赋值
    LogMessage& operator=(LogMessage const& /*other*/) = delete;

    /**
     * @brief 将时间戳转换为格式化字符串
     *
     * @param time 要格式化的时间戳
     * @return 格式化的时间字符串（YYYY-MM-DD_HH:MM:SS）
     *
     * 静态工具函数，用于时间戳格式化
     */
    static std::string getTimeStr(time_t time);

    /**
     * @brief 获取消息创建时间的格式化字符串
     * @return 格式化的时间字符串
     */
    std::string getTimeStr() const;

    /// 日志级别（const，创建后不可修改）
    LogLevel const level;
    /// 日志类型/过滤器名称，如"entities.player"、"network"等
    std::string const type;
    /// 日志消息内容文本
    std::string const text;
    /// 消息前缀，由Appender根据flags生成（时间戳、级别、类型等）
    std::string prefix;
    /// 附加参数，用于动态文件名（如Player_%s.log中的%s）
    std::string param1;
    /// 消息创建时间戳
    time_t mtime;

    /**
     * @brief 计算日志消息内容的字节大小
     * @return 前缀+文本的总字节数
     *
     * 用于文件大小限制检查，避免日志文件过大
     */
    uint32 Size() const
    {
        return static_cast<uint32>(prefix.size() + text.size());
    }
};

#endif // LogMessage_h__
