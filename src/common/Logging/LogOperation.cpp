/**
 * @file LogOperation.cpp
 * @brief 日志操作封装类实现文件
 *
 * 本文件实现了LogOperation类的方法，提供异步日志写入操作的封装。
 * LogOperation作为日志系统的异步执行单元，负责在IO线程中执行实际的日志写入。
 *
 * 实现要点：
 * - 使用std::forward完美转发unique_ptr，确保资源所有权正确转移
 * - 析构函数默认实现即可，unique_ptr会自动释放LogMessage
 * - call()方法通过Logger::write()完成实际的日志写入
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

#include "LogOperation.h"
#include "Logger.h"
#include "LogMessage.h"

/**
 * @brief LogOperation构造函数实现
 *
 * 初始化日志操作对象，转移LogMessage的所有权。
 *
 * @param _logger 目标日志记录器指针
 * @param _msg 日志消息对象的右值引用
 *
 * 实现说明：
 * - 使用std::forward完美转发unique_ptr，保持右值语义
 * - logger指针直接保存，不管理生命周期
 * - msg通过移动语义转移所有权，确保资源高效传递
 */
LogOperation::LogOperation(Logger const* _logger, std::unique_ptr<LogMessage>&& _msg) : logger(_logger), msg(std::forward<std::unique_ptr<LogMessage>>(_msg))
{
}

/**
 * @brief LogOperation析构函数实现
 *
 * 析构函数默认实现，依赖unique_ptr自动释放LogMessage资源。
 *
 * 资源释放顺序：
 * 1. 析构函数体执行（空实现）
 * 2. 成员变量析构（逆序）
 * 3. msg析构，自动删除LogMessage对象
 * 4. logger指针成员析构（仅释放指针本身，不删除Logger对象）
 */
LogOperation::~LogOperation()
{
}

/**
 * @brief 执行日志写入操作
 *
 * 调用Logger::write()方法将日志消息写入所有关联的输出器。
 *
 * @return int 返回0表示成功（保留用于未来错误码扩展）
 *
 * 执行流程：
 * 1. 获取msg指针（通过msg.get()）
 * 2. 调用logger->write()传入LogMessage指针
 * 3. Logger遍历其所有Appender执行写入
 *
 * 注意事项：
 * - 此方法在IO线程中异步执行
 * - 通过ASIO strand保证线程安全
 * - LogMessage指针在call()执行期间保持有效
 * - call()执行完成后，LogOperation对象可能立即被销毁
 */
int LogOperation::call()
{
    logger->write(msg.get());
    return 0;
}
