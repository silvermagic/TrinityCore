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
 * @file AdhocStatement.cpp
 * @brief 即席SQL语句任务实现
 *
 * 本文件实现了 BasicStatementTask 类，提供即席SQL语句的执行功能。
 * 即席查询是指在运行时动态构建的SQL语句，与预处理语句相对。
 *
 * 核心功能：
 * - 封装原始SQL字符串为可执行任务
 * - 支持同步执行（无结果返回）和异步执行（通过Future返回结果）
 * - 作为数据库工作队列中的任务单元被调度执行
 */

#include "AdhocStatement.h"
#include "Errors.h"
#include "MySQLConnection.h"
#include "QueryResult.h"
#include <cstdlib>
#include <cstring>

/**
 * @brief 构造函数 - 初始化即席SQL任务
 * @param sql   原始SQL语句字符串
 * @param async 是否为异步模式（需要返回结果集）
 *
 * 初始化流程：
 * 1. 使用 strdup() 复制SQL字符串，确保字符串生命周期与任务对象一致
 * 2. 根据异步模式标志决定是否创建 Promise 对象
 * 3. 异步模式下创建 QueryResultPromise，用于跨线程传递结果
 */
BasicStatementTask::BasicStatementTask(char const* sql, bool async) :
m_result(nullptr)
{
    // 复制SQL字符串，避免外部字符串被释放后导致悬空指针
    m_sql = strdup(sql);
    // 如果是异步操作，则需要保存结果供调用者获取
    m_has_result = async;
    if (async)
        m_result = new QueryResultPromise();
}

/**
 * @brief 析构函数 - 释放任务资源
 *
 * 资源释放：
 * 1. 释放由 strdup() 分配的SQL字符串内存（使用 free()）
 * 2. 如果是异步模式，释放 Promise 对象
 *
 * @note Promise 对象必须在设置值之后才能释放，否则等待的线程将无法获取结果
 */
BasicStatementTask::~BasicStatementTask()
{
    // 释放SQL字符串内存
    free((void*)m_sql);
    // 异步模式下释放结果承诺对象
    if (m_has_result && m_result != nullptr)
        delete m_result;
}

/**
 * @brief 执行SQL语句
 * @return 执行结果
 *         - 异步模式：true=成功且有结果，false=失败或无数据
 *         - 同步模式：true=执行成功，false=执行失败
 *
 * 执行流程（异步模式）：
 * 1. 调用 MySQLConnection::Query() 执行查询
 * 2. 检查结果集是否有效：
 *    - 结果集为空
 *    - 结果集行数为0
 *    - 无法获取第一行数据
 *    以上情况均视为失败，设置空结果并返回 false
 * 3. 成功时，将结果封装为 QueryResult 并通过 Promise 传递
 *
 * 执行流程（同步模式）：
 * - 直接调用 MySQLConnection::Execute() 执行SQL
 * - 不关心返回结果，适用于 INSERT/UPDATE/DELETE 等操作
 *
 * @note 此方法由 DatabaseWorker 工作线程调用
 * @note m_conn 成员由 SQLOperation 基类提供，在执行前由工作线程设置
 */
bool BasicStatementTask::Execute()
{
    if (m_has_result)
    {
        // 异步模式：执行查询并返回结果
        ResultSet* result = m_conn->Query(m_sql);
        // 检查结果有效性：结果集存在、有数据、能读取第一行
        if (!result || !result->GetRowCount() || !result->NextRow())
        {
            // 查询失败或无数据，清理资源并设置空结果
            delete result;
            m_result->set_value(QueryResult(nullptr));
            return false;
        }

        // 查询成功，通过 Promise 传递结果给等待的线程
        m_result->set_value(QueryResult(result));
        return true;
    }

    // 同步模式：直接执行，不返回结果
    return m_conn->Execute(m_sql);
}
