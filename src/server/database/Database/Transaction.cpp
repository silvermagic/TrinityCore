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
 * @file Transaction.cpp
 * @brief 数据库事务处理实现文件
 *
 * 本文件实现了TransactionBase和TransactionTask的核心功能，包括：
 * - 事务的构建和管理
 * - 事务的异步执行
 * - 死锁检测和自动重试
 * - 事务结果反馈
 */

#include "Log.h"
#include "Transaction.h"
#include "MySQLConnection.h"
#include "PreparedStatement.h"
#include "Timer.h"
#include <mysqld_error.h>
#include <sstream>
#include <thread>

// 定义静态成员变量：全局死锁互斥锁
std::mutex TransactionTask::_deadlockLock;

// 死锁最大重试时间（毫秒）
#define DEADLOCK_MAX_RETRY_TIME_MS 60000

/**
 * @brief 追加普通SQL语句到事务
 *
 * @param sql SQL语句字符串
 *
 * 该方法将普通SQL语句添加到事务中。SQL字符串会被复制存储，
 * 以确保即使原始字符串被销毁，事务中仍有有效副本。
 *
 * 调用时机：构建事务时调用
 * 性能注意事项：使用strdup()复制字符串，需要调用者管理原始字符串生命周期
 *
 * 注意：SQL语句在Cleanup()时会被释放
 */
void TransactionBase::Append(char const* sql)
{
    SQLElementData data;
    data.type = SQL_ELEMENT_RAW;                // 标记为普通SQL类型
    data.element.query = strdup(sql);           // 复制SQL字符串
    m_queries.push_back(data);
}

/**
 * @brief 追加预处理语句到事务
 *
 * @param stmt 预处理语句基类指针
 *
 * 该方法将预处理语句添加到事务中。预处理语句对象本身会被存储，
 * 并在事务执行后或清理时删除。
 *
 * 调用时机：由Transaction<T>::Append()模板方法调用
 * 性能注意事项：只存储指针，开销极小
 */
void TransactionBase::AppendPreparedStatement(PreparedStatementBase* stmt)
{
    SQLElementData data;
    data.type = SQL_ELEMENT_PREPARED;           // 标记为预处理语句类型
    data.element.stmt = stmt;                   // 存储预处理语句指针
    m_queries.push_back(data);
}

/**
 * @brief 清理事务资源
 *
 * 该方法释放所有存储的SQL语句和预处理语句。
 * 可以被显式调用或由析构函数调用，防止重复清理。
 *
 * 清理内容：
 * - 普通SQL语句：使用free()释放strdup()复制的字符串
 * - 预处理语句：使用delete释放对象
 */
void TransactionBase::Cleanup()
{
    // 如果已经清理过，直接返回，防止重复清理
    if (_cleanedUp)
        return;

    // 遍历所有SQL元素并释放资源
    for (SQLElementData const& data : m_queries)
    {
        switch (data.type)
        {
            case SQL_ELEMENT_PREPARED:
                // 删除预处理语句对象
                delete data.element.stmt;
            break;
            case SQL_ELEMENT_RAW:
                // 释放strdup()复制的字符串
                free((void*)(data.element.query));
            break;
        }
    }

    // 清空查询向量
    m_queries.clear();
    // 标记为已清理
    _cleanedUp = true;
}

/**
 * @brief 执行事务任务
 *
 * @return bool 成功返回true，失败返回false
 *
 * 该方法是SQLOperation的虚函数实现，在数据库工作线程中执行。
 * 它会尝试执行事务，如果发生死锁，会获取全局锁并重试。
 *
 * 执行流程：
 * 1. 调用TryExecute()尝试执行事务
 * 2. 如果成功（错误码为0），返回true
 * 3. 如果失败且错误码为ER_LOCK_DEADLOCK（死锁），进入重试逻辑
 * 4. 获取全局死锁锁，确保只有一个线程重试
 * 5. 循环重试直到成功或超过最大重试时间（60秒）
 * 6. 如果重试失败，记录错误日志并清理资源
 * 7. 其他错误直接清理资源
 *
 * 死锁处理机制：
 * - MySQL检测到死锁时会返回ER_LOCK_DEADLOCK错误
 * - 获取全局锁防止多个线程同时重试
 * - 每次重试都会记录警告日志
 * - 超过最大重试时间后放弃并记录错误日志
 *
 * 调用时机：由数据库工作线程调用
 * 性能注意事项：正常情况下执行一次，死锁时最多重试60秒
 */
bool TransactionTask::Execute()
{
    // 尝试执行事务
    int errorCode = TryExecute();
    if (!errorCode)
        return true;  // 执行成功

    // 检查是否是死锁错误
    if (errorCode == ER_LOCK_DEADLOCK)
    {
        // 获取当前线程ID用于日志记录
        std::string threadId = []()
        {
            // 使用lambda包装以修复分析工具的假阳性警告C26115
            std::ostringstream threadIdStream;
            threadIdStream << std::this_thread::get_id();
            return threadIdStream.str();
        }();

        // 获取全局死锁锁，确保只有一个异步线程重试事务
        // 这样可以防止多个线程相互死锁
        std::lock_guard<std::mutex> lock(_deadlockLock);

        // 重试循环，最多重试DEADLOCK_MAX_RETRY_TIME_MS毫秒
        for (uint32 loopDuration = 0, startMSTime = getMSTime(); loopDuration <= DEADLOCK_MAX_RETRY_TIME_MS; loopDuration = GetMSTimeDiffToNow(startMSTime))
        {
            // 尝试重新执行事务
            if (!TryExecute())
                return true;  // 重试成功

            // 记录重试警告
            TC_LOG_WARN("sql.sql", "Deadlocked SQL Transaction, retrying. Loop timer: {} ms, Thread Id: {}", loopDuration, threadId);
        }

        // 超过最大重试时间，记录致命错误
        TC_LOG_ERROR("sql.sql", "Fatal deadlocked SQL Transaction, it will not be retried anymore. Thread Id: {}", threadId);
    }

    // 事务执行失败，清理资源
    CleanupOnFailure();

    return false;
}

/**
 * @brief 尝试执行事务
 *
 * @return int MySQL错误码，0表示成功
 *
 * 该方法调用MySQL连接的ExecuteTransaction()方法执行事务。
 * 事务中的所有SQL语句会在一个事务中执行，要么全部成功，要么全部失败。
 *
 * 调用时机：由Execute()调用
 * 性能注意事项：执行时间取决于事务中SQL语句的数量和复杂度
 */
int TransactionTask::TryExecute()
{
    // 调用MySQL连接执行事务
    // m_conn是SQLOperation的成员，表示当前数据库连接
    return m_conn->ExecuteTransaction(m_trans);
}

/**
 * @brief 失败时清理资源
 *
 * 该方法在事务执行失败时调用，清理事务中的SQL语句和预处理语句。
 * 防止内存泄漏。
 */
void TransactionTask::CleanupOnFailure()
{
    m_trans->Cleanup();
}

/**
 * @brief 执行带结果反馈的事务任务
 *
 * @return bool 成功返回true，失败返回false
 *
 * 该方法是TransactionTask::Execute()的扩展版本，增加了结果反馈功能。
 * 执行流程与TransactionTask::Execute()类似，但会设置promise的结果。
 *
 * 执行流程：
 * 1. 调用TryExecute()尝试执行事务
 * 2. 如果成功（错误码为0），设置promise为true并返回true
 * 3. 如果失败且错误码为ER_LOCK_DEADLOCK（死锁），进入重试逻辑
 * 4. 获取全局死锁锁，确保只有一个线程重试
 * 5. 循环重试直到成功或超过最大重试时间（60秒）
 * 6. 重试成功时设置promise为true并返回true
 * 7. 如果重试失败，记录错误日志
 * 8. 其他错误直接进入失败处理
 * 9. 失败时清理资源，设置promise为false，返回false
 *
 * 调用时机：由数据库工作线程调用
 * 性能注意事项：正常情况下执行一次，死锁时最多重试60秒
 *
 * 注意：通过GetFuture()返回的future可以获取事务执行结果（true表示成功）
 */
bool TransactionWithResultTask::Execute()
{
    // 尝试执行事务
    int errorCode = TryExecute();
    if (!errorCode)
    {
        // 执行成功，设置promise结果为true
        m_result.set_value(true);
        return true;
    }

    // 检查是否是死锁错误
    if (errorCode == ER_LOCK_DEADLOCK)
    {
        // 获取当前线程ID用于日志记录
        std::string threadId = []()
        {
            // 使用lambda包装以修复分析工具的假阳性警告C26115
            std::ostringstream threadIdStream;
            threadIdStream << std::this_thread::get_id();
            return threadIdStream.str();
        }();

        // 获取全局死锁锁，确保只有一个异步线程重试事务
        std::lock_guard<std::mutex> lock(_deadlockLock);

        // 重试循环，最多重试DEADLOCK_MAX_RETRY_TIME_MS毫秒
        for (uint32 loopDuration = 0, startMSTime = getMSTime(); loopDuration <= DEADLOCK_MAX_RETRY_TIME_MS; loopDuration = GetMSTimeDiffToNow(startMSTime))
        {
            // 尝试重新执行事务
            if (!TryExecute())
            {
                // 重试成功，设置promise结果为true
                m_result.set_value(true);
                return true;
            }

            // 记录重试警告
            TC_LOG_WARN("sql.sql", "Deadlocked SQL Transaction, retrying. Loop timer: {} ms, Thread Id: {}", loopDuration, threadId);
        }

        // 超过最大重试时间，记录致命错误
        TC_LOG_ERROR("sql.sql", "Fatal deadlocked SQL Transaction, it will not be retried anymore. Thread Id: {}", threadId);
    }

    // 事务执行失败，清理资源
    CleanupOnFailure();

    // 设置promise结果为false
    m_result.set_value(false);

    return false;
}

/**
 * @brief 检查事务是否完成并执行回调
 *
 * @return bool 事务完成且回调已执行返回true，否则返回false
 *
 * 该方法用于非阻塞地检查事务是否已完成，如果完成则执行注册的回调函数。
 * 通常在游戏更新循环中定期调用。
 *
 * 执行流程：
 * 1. 检查future是否有效
 * 2. 使用wait_for(0)进行非阻塞等待
 * 3. 如果future已就绪，获取结果并调用回调函数
 * 4. 返回执行状态
 *
 * 调用时机：在游戏更新循环中定期调用
 * 性能注意事项：wait_for(0)是非阻塞检查，性能开销极小
 *
 * 使用示例：
 * @code
 * TransactionCallback callback = Database.AsyncCommitTransaction(trans);
 * callback.AfterComplete([](bool success) {
 *     if (success) {
 *         TC_LOG_INFO("server", "Transaction succeeded");
 *     }
 * });
 *
 * // 在游戏循环中
 * if (callback.InvokeIfReady()) {
 *     // 回调已执行，callback可以被销毁
 * }
 * @endcode
 */
bool TransactionCallback::InvokeIfReady()
{
    // 检查future是否有效且已就绪
    if (m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        // future已就绪，获取结果并调用回调函数
        // m_future.get()会返回bool值（true表示事务成功）
        m_callback(m_future.get());
        return true;
    }

    // future未就绪或无效，返回false
    return false;
}
