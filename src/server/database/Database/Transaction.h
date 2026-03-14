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
 * @file Transaction.h
 * @brief 数据库事务处理模块
 *
 * 本模块提供了数据库事务的支持，主要功能包括：
 * - 事务的构建和管理
 * - 批量SQL语句的原子执行
 * - 死锁检测和自动重试
 * - 异步事务执行
 *
 * 事务特点：
 * - 支持混合普通SQL和预处理语句
 * - 自动处理死锁，最多重试60秒
 * - 提供同步和异步执行方式
 * - 支持事务结果回调
 *
 * 使用场景：
 * - 需要保证多个SQL操作原子性的场景
 * - 玩家数据保存、物品交易等关键操作
 * - 批量数据更新和插入
 */

#ifndef _TRANSACTION_H
#define _TRANSACTION_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "SQLOperation.h"
#include "StringFormat.h"
#include <functional>
#include <mutex>
#include <vector>

/**
 * @class TransactionBase
 * @brief 数据库事务基类，用于构建和管理事务中的SQL操作
 *
 * 该类提供了事务的基本功能，允许将多个SQL操作组合成一个原子事务。
 * 支持普通SQL语句和预处理语句的混合使用。
 *
 * 事务生命周期：
 * 1. 创建Transaction对象
 * 2. 调用Append()或PAppend()添加SQL语句
 * 3. 提交到数据库执行
 * 4. 执行成功或失败
 *
 * 线程安全：
 * - 非线程安全，应在单线程中构建和提交
 * - 执行时由数据库工作线程处理
 *
 * 使用示例：
 * @code
 * Transaction transaction;
 * transaction->Append("UPDATE accounts SET money = money - 100 WHERE id = 1");
 * transaction->Append("UPDATE accounts SET money = money + 100 WHERE id = 2");
 * CharacterDatabase.CommitTransaction(transaction);
 * @endcode
 */
class TC_DATABASE_API TransactionBase
{
    friend class TransactionTask;
    friend class MySQLConnection;

    template <typename T>
    friend class DatabaseWorkerPool;

    public:
        /**
         * @brief 构造函数
         *
         * 初始化事务对象，设置清理标志为false
         */
        TransactionBase() : _cleanedUp(false) { }

        /**
         * @brief 虚析构函数
         *
         * 析构时调用Cleanup()清理资源
         */
        virtual ~TransactionBase() { Cleanup(); }

        /**
         * @brief 追加普通SQL语句
         *
         * @brief 简要说明：将一个SQL语句添加到事务中
         * @param sql SQL语句字符串
         *
         * 调用时机：构建事务时调用
         * 性能注意事项：会复制SQL字符串
         *
         * 注意：SQL语句会在内部被复制，调用者需要管理原始字符串的生命周期
         */
        void Append(char const* sql);

        /**
         * @brief 追加格式化SQL语句
         *
         * @brief 简要说明：使用格式化字符串生成并添加SQL语句
         * @tparam Args 格式化参数类型
         * @param sql 格式化SQL字符串
         * @param args 格式化参数
         *
         * 调用时机：需要动态构建SQL语句时调用
         * 性能注意事项：包含字符串格式化操作
         *
         * 使用示例：
         * @code
         * transaction->PAppend("UPDATE players SET level = {} WHERE guid = {}", level, guid);
         * @endcode
         */
        template<typename... Args>
        void PAppend(Trinity::FormatString<Args...> sql, Args&&... args)
        {
            this->Append(Trinity::StringFormat(sql, std::forward<Args>(args)...).c_str());
        }

        /**
         * @brief 获取事务中SQL语句的数量
         *
         * @brief 简要说明：返回事务中包含的SQL语句数量
         * @return std::size_t SQL语句数量
         *
         * 调用时机：检查事务大小或调试时调用
         * 性能注意事项：O(1)时间复杂度
         */
        std::size_t GetSize() const { return m_queries.size(); }

    protected:
        /**
         * @brief 追加预处理语句
         *
         * @brief 简要说明：将预处理语句添加到事务中
         * @param statement 预处理语句基类指针
         *
         * 调用时机：由派生类的Append模板方法调用
         * 性能注意事项：简单地将指针存入向量
         */
        void AppendPreparedStatement(PreparedStatementBase* statement);

        /**
         * @brief 清理资源
         *
         * 释放所有存储的SQL语句和预处理语句。
         * 由析构函数或执行失败时调用。
         */
        void Cleanup();

        /**
         * @brief SQL操作元素集合
         *
         * 存储事务中的所有SQL操作，每个元素可以是普通SQL或预处理语句
         */
        std::vector<SQLElementData> m_queries;

    private:
        /**
         * @brief 清理标志
         *
         * 防止重复清理资源，当已经清理时为true
         */
        bool _cleanedUp;
};

/**
 * @class Transaction
 * @brief 类型安全的数据库事务模板类
 *
 * @tparam T 数据库连接类型，用于类型安全的预处理语句
 *
 * 该模板类继承自TransactionBase，提供了类型安全的接口。
 * 通过模板参数确保预处理语句与正确的数据库连接类型匹配。
 *
 * 使用示例：
 * @code
 * Transaction<CharacterDatabaseConnection> transaction;
 * auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PLAYER_MONEY);
 * stmt->setUInt32(0, money);
 * stmt->setUInt64(1, guid);
 * transaction->Append(stmt);
 * CharacterDatabase.CommitTransaction(transaction);
 * @endcode
 */
template<typename T>
class Transaction : public TransactionBase
{
public:
    // 引入基类的Append方法
    using TransactionBase::Append;

    /**
     * @brief 追加预处理语句
     *
     * @brief 简要说明：将类型安全的预处理语句添加到事务中
     * @param statement 类型安全的预处理语句指针
     *
     * 调用时机：需要在事务中执行预处理语句时调用
     * 性能注意事项：简单的转发调用，开销极小
     */
    void Append(PreparedStatement<T>* statement)
    {
        this->AppendPreparedStatement(statement);
    }
};

/**
 * @class TransactionTask
 * @brief 数据库事务执行任务，用于异步执行事务
 *
 * 该类继承自SQLOperation，是一个可以在数据库工作线程中执行的事务任务。
 * 提供了死锁检测和自动重试机制，确保事务在并发冲突时能够成功执行。
 *
 * 执行流程：
 * 1. 调用Execute()开始执行
 * 2. 调用TryExecute()尝试执行事务
 * 3. 如果发生死锁，获取全局锁并重试
 * 4. 最多重试60秒（DEADLOCK_MAX_RETRY_TIME_MS）
 * 5. 失败时调用CleanupOnFailure()清理资源
 *
 * 死锁处理机制：
 * - 使用全局互斥锁_deadlockLock确保只有一个线程重试
 * - 避免多个线程同时重试导致持续死锁
 * - 重试间隔由MySQL自行决定
 *
 * 线程安全：任务在工作线程中执行，通过互斥锁保证死锁重试的线程安全
 */
class TC_DATABASE_API TransactionTask : public SQLOperation
{
    template <class T> friend class DatabaseWorkerPool;
    friend class DatabaseWorker;
    friend class TransactionCallback;

    public:
        /**
         * @brief 构造函数
         *
         * @brief 简要说明：使用事务对象初始化任务
         * @param trans 事务共享指针
         *
         * 调用时机：需要异步执行事务时创建任务
         */
        TransactionTask(std::shared_ptr<TransactionBase> trans) : m_trans(trans) { }

        /**
         * @brief 析构函数
         */
        ~TransactionTask() { }

    protected:
        /**
         * @brief 执行事务任务
         *
         * @brief 简要说明：尝试执行事务，处理死锁和重试逻辑
         * @return bool 成功返回true，失败返回false
         *
         * 调用时机：由数据库工作线程调用
         * 性能注意事项：如果发生死锁可能重试多次，最长60秒
         *
         * 执行流程：
         * 1. 调用TryExecute()尝试执行
         * 2. 如果返回错误码ER_LOCK_DEADLOCK，进入重试循环
         * 3. 获取全局死锁锁，防止多线程同时重试
         * 4. 循环重试直到成功或超时
         * 5. 失败时调用CleanupOnFailure()清理资源
         */
        bool Execute() override;

        /**
         * @brief 尝试执行事务
         *
         * @brief 简要说明：调用数据库连接执行事务
         * @return int MySQL错误码，0表示成功
         *
         * 调用时机：由Execute()调用
         * 性能注意事项：执行时间取决于事务大小和数据库负载
         */
        int TryExecute();

        /**
         * @brief 失败时清理资源
         *
         * 调用事务的Cleanup()方法释放SQL语句资源。
         * 在事务执行失败时调用。
         */
        void CleanupOnFailure();

        /**
         * @brief 事务对象共享指针
         *
         * 使用shared_ptr确保在任务执行期间事务对象不会被销毁
         */
        std::shared_ptr<TransactionBase> m_trans;

        /**
         * @brief 全局死锁互斥锁
         *
         * 用于确保只有一个线程在重试死锁事务，
         * 避免多个线程同时重试导致持续死锁
         */
        static std::mutex _deadlockLock;
};

/**
 * @class TransactionWithResultTask
 * @brief 带结果反馈的事务执行任务
 *
 * 该类继承自TransactionTask，额外提供了事务执行结果的反馈机制。
 * 通过promise/future模式，调用者可以知道事务是否成功执行。
 *
 * 使用场景：
 * - 需要知道事务执行结果的场景
 * - 需要在事务完成后执行特定逻辑的场景
 *
 * 使用示例：
 * @code
 * auto task = std::make_shared<TransactionWithResultTask>(trans);
 * TransactionFuture future = task->GetFuture();
 * Database.Execute(task);
 * // 稍后检查结果
 * if (future.get()) {
 *     // 事务成功
 * } else {
 *     // 事务失败
 * }
 * @endcode
 */
class TC_DATABASE_API TransactionWithResultTask : public TransactionTask
{
public:
    /**
     * @brief 构造函数
     *
     * @brief 简要说明：使用事务对象初始化任务
     * @param trans 事务共享指针
     *
     * 调用时机：需要带结果反馈的事务执行时创建
     */
    TransactionWithResultTask(std::shared_ptr<TransactionBase> trans) : TransactionTask(trans) { }

    /**
     * @brief 获取结果future
     *
     * @brief 简要说明：返回用于获取事务执行结果的future对象
     * @return TransactionFuture future对象，可通过get()获取bool结果
     *
     * 调用时机：任务添加到队列后立即调用
     * 性能注意事项：返回future的拷贝，开销极小
     */
    TransactionFuture GetFuture() { return m_result.get_future(); }

protected:
    /**
     * @brief 执行事务任务
     *
     * @brief 简要说明：执行事务并设置结果到promise
     * @return bool 成功返回true，失败返回false
     *
     * 调用时机：由数据库工作线程调用
     * 性能注意事项：与TransactionTask::Execute()相同，但额外设置promise结果
     *
     * 执行流程：
     * 1. 调用TryExecute()尝试执行
     * 2. 如果成功，设置promise为true
     * 3. 如果发生死锁，重试直到成功或超时
     * 4. 最终失败时设置promise为false并清理资源
     */
    bool Execute() override;

    /**
     * @brief 事务结果承诺对象
     *
     * 用于设置事务执行结果，主线程通过future获取
     */
    TransactionPromise m_result;
};

/**
 * @class TransactionCallback
 * @brief 事务回调封装类
 *
 * 该类封装了事务的future和回调函数，提供了一种便捷的方式来
 * 处理异步事务完成事件。通常用于更新循环中检查事务是否完成。
 *
 * 使用示例：
 * @code
 * TransactionCallback callback = CharacterDatabase.AsyncCommitTransaction(trans);
 * callback.AfterComplete([](bool success) {
 *     if (success) {
 *         TC_LOG_INFO("server", "Transaction succeeded");
 *     } else {
 *         TC_LOG_ERROR("server", "Transaction failed");
 *     }
 * });
 *
 * // 在更新循环中调用
 * if (callback.InvokeIfReady()) {
 *     // 回调已执行，清理callback
 * }
 * @endcode
 *
 * 生命周期：
 * 1. 通过数据库接口创建callback对象
 * 2. 设置AfterComplete回调
 * 3. 在游戏循环中定期调用InvokeIfReady()
 * 4. 事务完成时自动触发回调
 */
class TC_DATABASE_API TransactionCallback
{
public:
    /**
     * @brief 构造函数
     *
     * @brief 简要说明：使用future初始化回调对象
     * @param future 事务结果future右值引用
     *
     * 调用时机：由数据库接口方法创建
     */
    TransactionCallback(TransactionFuture&& future) : m_future(std::move(future)) { }

    /**
     * @brief 移动构造函数
     */
    TransactionCallback(TransactionCallback&&) = default;

    /**
     * @brief 移动赋值运算符
     */
    TransactionCallback& operator=(TransactionCallback&&) = default;

    /**
     * @brief 设置完成回调函数
     *
     * @brief 简要说明：注册事务完成后执行的回调函数
     * @param callback 回调函数，接收bool参数表示事务是否成功
     *
     * 调用时机：创建callback后立即调用，设置处理逻辑
     * 注意：只能在左值对象上调用（使用&限定符）
     */
    void AfterComplete(std::function<void(bool)> callback) &
    {
        m_callback = std::move(callback);
    }

    /**
     * @brief 检查并调用回调
     *
     * @brief 简要说明：检查事务是否完成，如果完成则执行回调
     * @return bool 事务完成且回调已执行返回true，否则返回false
     *
     * 调用时机：在游戏更新循环中定期调用
     * 性能注意事项：使用wait_for(0)进行非阻塞检查，性能开销小
     *
     * 执行流程：
     * 1. 检查future是否有效
     * 2. 非阻塞等待future状态
     * 3. 如果已就绪，获取结果并调用回调函数
     * 4. 返回执行状态
     */
    bool InvokeIfReady();

    /**
     * @brief 事务结果future对象
     */
    TransactionFuture m_future;

    /**
     * @brief 完成回调函数
     *
     * 当事务完成时，InvokeIfReady()会调用此函数，传入bool参数表示成功或失败
     */
    std::function<void(bool)> m_callback;
};

#endif
