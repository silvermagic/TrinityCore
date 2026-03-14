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
 * @file Appender.h
 * @brief 日志追加器基类定义
 *
 * 本文件定义了Appender抽象基类，是日志系统输出目标的核心接口。
 * Appender负责将日志消息输出到具体的介质（如控制台、文件、数据库等）。
 *
 * 主要职责：
 *   - 定义日志输出的统一接口
 *   - 管理日志级别过滤
 *   - 提供日志格式化前缀功能（时间戳、日志级别、过滤器类型）
 *   - 支持多种输出目标的扩展
 *
 * 设计模式：
 *   采用工厂模式创建具体的Appender实例，通过模板方法模式实现_write虚函数。
 *
 * 继承体系：
 *   Appender (基类)
 *     ├── AppenderConsole (控制台输出)
 *     ├── AppenderFile (文件输出)
 *     └── 其他自定义输出器...
 */

#ifndef APPENDER_H
#define APPENDER_H

#include "Define.h"
#include "LogCommon.h"
#include <stdexcept>
#include <string>
#include <vector>

struct LogMessage;

/**
 * @brief 日志追加器抽象基类
 *
 * Appender是日志系统中的输出目标抽象，负责将日志消息写入到具体的存储介质。
 * 每个Appender都有独立的日志级别设置，可以过滤不需要的消息。
 *
 * 主要特性：
 *   - 支持多种输出目标（控制台、文件等）
 *   - 可配置日志级别过滤
 *   - 支持消息前缀格式化（时间戳、级别、类型）
 *   - 支持运行时动态调整日志级别
 *
 * 线程安全：
 *   基类本身不保证线程安全，由Log系统通过strand机制保证线程安全
 */
class TC_COMMON_API Appender
{
    public:
        /**
         * @brief 构造函数
         *
         * @param _id   追加器的唯一标识符
         * @param name  追加器名称，用于配置和调试
         * @param level 追加器的日志级别，默认为LOG_LEVEL_DISABLED（禁用）
         * @param flags 追加器标志位，控制前缀格式化选项
         */
        Appender(uint8 _id, std::string const& name, LogLevel level = LOG_LEVEL_DISABLED, AppenderFlags flags = APPENDER_FLAGS_NONE);

        /**
         * @brief 虚析构函数
         *
         * 确保派生类正确释放资源
         */
        virtual ~Appender();

        /**
         * @brief 获取追加器ID
         * @return 追加器的唯一标识符
         */
        uint8 getId() const;

        /**
         * @brief 获取追加器名称
         * @return 追加器名称的常量引用
         */
        std::string const& getName() const;

        /**
         * @brief 获取追加器类型
         * @return 追加器类型枚举值（如APPENDER_CONSOLE、APPENDER_FILE等）
         *
         * 纯虚函数，必须由派生类实现
         */
        virtual AppenderType getType() const = 0;

        /**
         * @brief 获取追加器的日志级别
         * @return 当前设置的日志级别
         */
        LogLevel getLogLevel() const;

        /**
         * @brief 获取追加器标志
         * @return 追加器标志位
         */
        AppenderFlags getFlags() const;

        /**
         * @brief 设置日志级别
         * @param _level 新的日志级别
         *
         * 运行时动态调整追加器的日志级别
         */
        void setLogLevel(LogLevel);

        /**
         * @brief 写入日志消息
         * @param message 要写入的日志消息指针
         *
         * 主要流程：
         *   1. 检查消息级别是否满足追加器的级别要求
         *   2. 根据flags生成消息前缀（时间戳、级别、类型）
         *   3. 调用_write虚函数执行实际写入操作
         */
        void write(LogMessage* message);

        /**
         * @brief 将日志级别转换为字符串
         * @param level 日志级别枚举值
         * @return 日志级别的字符串表示（如"FATAL"、"ERROR"等）
         *
         * 静态工具函数，用于日志格式化
         */
        static char const* getLogLevelString(LogLevel level);

        /**
         * @brief 设置领域ID
         * @param realmId 领域服务器ID
         *
         * 虚函数，默认空实现。派生类可重写以支持多领域日志标识
         */
        virtual void setRealmId(uint32 /*realmId*/) { }

    private:
        /**
         * @brief 实际写入日志消息的虚函数
         * @param message 日志消息常量指针
         *
         * 纯虚函数，由派生类实现具体的写入逻辑（写入控制台、文件等）
         */
        virtual void _write(LogMessage const* /*message*/) = 0;

        /// 追加器的唯一标识符
        uint8 id;
        /// 追加器名称
        std::string name;
        /// 追加器的日志级别
        LogLevel level;
        /// 追加器标志位
        AppenderFlags flags;
};

/**
 * @brief 无效追加器参数异常类
 *
 * 当追加器配置参数无效时抛出此异常
 * 继承自std::length_error以保持与标准库异常体系的一致性
 */
class TC_COMMON_API InvalidAppenderArgsException : public std::length_error
{
public:
    /**
     * @brief 构造函数
     * @param message 异常描述信息
     */
    explicit InvalidAppenderArgsException(std::string const& message) : std::length_error(message) { }
};

#endif
