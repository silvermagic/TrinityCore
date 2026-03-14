/**
 * @file LogOperation.h
 * @brief 日志操作封装类头文件
 *
 * 本文件定义了LogOperation类，用于封装异步日志写入操作。
 * LogOperation是日志系统异步写入机制的核心组件，负责将日志消息
 * 和日志记录器封装成一个可执行的操作对象。
 *
 * 异步日志工作流程：
 * 1. 日志调用发生时，创建LogMessage对象
 * 2. 将LogMessage和Logger封装成LogOperation
 * 3. 通过ASIO的strand机制投递到IO线程
 * 4. IO线程执行LogOperation::call()方法写入日志
 * 5. 写入完成后LogOperation对象被销毁
 *
 * 这种设计确保了：
 * - 线程安全：通过strand保证日志写入的顺序性
 * - 性能优化：调用线程不必等待日志写入完成
 * - 资源管理：使用智能指针自动管理LogMessage生命周期
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

#ifndef LOGOPERATION_H
#define LOGOPERATION_H

#include "Define.h"
#include <memory>

// 前向声明
class Logger;        ///< 日志记录器类
struct LogMessage;   ///< 日志消息结构体

/**
 * @class LogOperation
 * @brief 日志操作封装类
 *
 * LogOperation类封装了一个日志写入操作，包括目标日志记录器和要写入的日志消息。
 * 该类主要用于异步日志系统，允许将日志写入操作投递到独立的IO线程执行。
 *
 * 设计模式：
 * - Command模式：将日志写入操作封装为对象
 * - 资源管理：使用unique_ptr管理LogMessage的生命周期
 *
 * 线程安全：
 * - LogOperation对象在创建后不可修改
 * - LogMessage的所有权转移到LogOperation中
 * - 通过ASIO strand保证异步执行的线程安全
 *
 * 使用场景：
 * @code
 * // 在Log::write()方法中创建LogOperation
 * std::shared_ptr<LogOperation> logOperation =
 *     std::make_shared<LogOperation>(logger, std::move(msg));
 *
 * // 投递到IO线程异步执行
 * Trinity::Asio::post(*_ioContext,
 *     Trinity::Asio::bind_executor(*_strand, [logOperation]() {
 *         logOperation->call();
 *     }));
 * @endcode
 */
class TC_COMMON_API LogOperation
{
    public:
        /**
         * @brief 构造函数
         *
         * 创建一个日志操作对象，封装日志记录器和日志消息。
         *
         * @param _logger 目标日志记录器指针（不可为空）
         * @param _msg 要写入的日志消息（通过右值引用转移所有权）
         *
         * 说明：
         * - 使用右值引用确保LogMessage的所有权转移
         * - logger指针由调用者保证有效
         * - 构造后LogOperation对象处于可执行状态
         */
        LogOperation(Logger const* _logger, std::unique_ptr<LogMessage>&& _msg);

        /**
         * @brief 析构函数
         *
         * 销毁日志操作对象，自动释放LogMessage资源。
         *
         * 说明：
         * - unique_ptr成员msg会在析构时自动删除LogMessage
         * - 不需要手动管理资源释放
         */
        ~LogOperation();

        /**
         * @brief 执行日志写入操作
         *
         * 调用日志记录器的write方法，将日志消息写入到所有关联的输出器。
         *
         * @return int 返回0表示执行成功（保留用于未来扩展）
         *
         * 主要流程：
         * 1. 调用logger->write(msg.get())
         * 2. Logger遍历所有关联的Appender
         * 3. 每个Appender将日志消息写入到对应的目标
         *
         * 线程安全：
         * - 此方法通过ASIO strand保证同一时间只有一个线程执行
         * - 避免多个线程同时写入同一输出器导致的竞态条件
         */
        int call();

    protected:
        Logger const* logger;              ///< 目标日志记录器指针（不拥有所有权）
        std::unique_ptr<LogMessage> msg;   ///< 日志消息对象（拥有所有权，自动管理生命周期）
};

#endif
