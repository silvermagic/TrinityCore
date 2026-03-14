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
 * @file QueryHolder.h
 * @brief SQL查询持有者模块
 *
 * 本模块提供了批量执行多个SQL查询的能力。主要功能包括：
 * - 批量存储和管理多个预处理语句
 * - 异步执行批量查询并收集结果
 * - 提供回调机制处理查询完成事件
 *
 * 使用场景：
 * - 需要同时执行多个相关查询并获取所有结果的场景
 * - 减少数据库连接的往返次数，提高性能
 * - 异步加载玩家数据、公会数据等需要多个查询的情况
 */

#ifndef _QUERYHOLDER_H
#define _QUERYHOLDER_H

#include "SQLOperation.h"
#include <vector>

/**
 * @class SQLQueryHolderBase
 * @brief SQL查询持有者基类，用于存储和管理多个预处理SQL查询及其结果
 *
 * 该类提供了批量管理多个SQL查询的能力，每个查询都有一个索引位置。
 * 查询执行后，结果会被存储在持有者中供后续访问。
 *
 * 生命周期：
 * 1. 创建SQLQueryHolder对象
 * 2. 调用SetSize()设置查询数量
 * 3. 调用SetPreparedQuery()为每个索引设置查询语句
 * 4. 通过SQLQueryHolderTask提交到数据库执行
 * 5. 执行完成后通过GetPreparedResult()获取结果
 * 6. 析构时自动清理所有PreparedStatement对象
 */
class TC_DATABASE_API SQLQueryHolderBase
{
    friend class SQLQueryHolderTask;
    private:
        /**
         * @brief 查询和结果对的集合
         *
         * 每个元素包含一个PreparedStatement指针和一个PreparedQueryResult结果
         * first: 预处理语句对象指针，执行完成后会被删除
         * second: 查询结果，可能为空（如果查询没有返回结果或执行失败）
         */
        std::vector<std::pair<PreparedStatementBase*, PreparedQueryResult>> m_queries;
    public:
        /**
         * @brief 默认构造函数
         */
        SQLQueryHolderBase() = default;

        /**
         * @brief 虚析构函数，清理所有存储的预处理语句
         */
        virtual ~SQLQueryHolderBase();

        /**
         * @brief 设置查询持有者的大小（查询数量）
         *
         * @brief 简要说明：调整内部向量大小以容纳指定数量的查询
         * @param size 查询数量
         *
         * 调用时机：在设置任何查询之前调用，用于预先分配空间
         * 性能注意事项：使用resize而不是push_back可以减少内存重分配次数
         */
        void SetSize(size_t size);

        /**
         * @brief 获取指定索引位置的查询结果
         *
         * @brief 简要说明：返回指定索引的预处理查询结果
         * @param index 查询索引位置（从0开始）
         * @return PreparedQueryResult 查询结果智能指针，可能为空
         *
         * 调用时机：在SQLQueryHolderTask执行完成后调用
         * 性能注意事项：O(1)时间复杂度
         */
        PreparedQueryResult GetPreparedResult(size_t index) const;

        /**
         * @brief 设置指定索引位置的查询结果
         *
         * @brief 简要说明：内部使用，用于存储查询执行结果
         * @param index 查询索引位置
         * @param result 预处理结果集指针
         *
         * 调用时机：仅由SQLQueryHolderTask::Execute()调用
         * 性能注意事项：如果结果集为空，会立即删除result指针
         */
        void SetPreparedResult(size_t index, PreparedResultSet* result);

    protected:
        /**
         * @brief 设置预处理查询的实现方法
         *
         * @brief 简要说明：将预处理语句存储到指定索引位置
         * @param index 查询索引位置
         * @param stmt 预处理语句基类指针
         * @return bool 成功返回true，索引越界返回false
         *
         * 调用时机：由派生类的SetPreparedQuery()调用
         * 性能注意事项：会进行索引边界检查
         */
        bool SetPreparedQueryImpl(size_t index, PreparedStatementBase* stmt);
};

/**
 * @class SQLQueryHolder
 * @brief 类型安全的SQL查询持有者模板类
 *
 * @tparam T 数据库连接类型，用于类型安全的预处理语句
 *
 * 该模板类继承自SQLQueryHolderBase，提供了类型安全的接口。
 * 通过模板参数确保预处理语句与正确的数据库连接类型匹配。
 *
 * 使用示例：
 * @code
 * SQLQueryHolder<CharacterDatabaseConnection> holder;
 * holder.SetSize(3);
 * holder.SetPreparedQuery(0, CharacterDatabase.GetPreparedStatement(CHAR_SEL_PLAYER_DATA));
 * holder.SetPreparedQuery(1, CharacterDatabase.GetPreparedStatement(CHAR_SEL_PLAYER_ITEMS));
 * holder.SetPreparedQuery(2, CharacterDatabase.GetPreparedStatement(CHAR_SEL_PLAYER_SPELLS));
 * @endcode
 */
template<typename T>
class SQLQueryHolder : public SQLQueryHolderBase
{
public:
    /**
     * @brief 设置预处理查询
     *
     * @brief 简要说明：将类型安全的预处理语句存储到指定索引位置
     * @param index 查询索引位置
     * @param stmt 类型安全的预处理语句指针
     * @return bool 成功返回true，索引越界返回false
     *
     * 调用时机：在设置好持有者大小后，执行前调用
     * 性能注意事项：简单的转发调用，开销极小
     */
    bool SetPreparedQuery(size_t index, PreparedStatement<T>* stmt)
    {
        return SetPreparedQueryImpl(index, stmt);
    }
};

/**
 * @class SQLQueryHolderTask
 * @brief SQL查询持有者任务，用于异步批量执行多个SQL查询
 *
 * 该类继承自SQLOperation，是一个可以在数据库工作线程中执行的任务。
 * 它会遍历持有者中的所有查询，逐个执行并存储结果。
 *
 * 工作流程：
 * 1. 创建SQLQueryHolderBase并设置查询
 * 2. 创建SQLQueryHolderTask包装持有者
 * 3. 将任务添加到数据库工作队列
 * 4. 通过GetFuture()获取future对象等待执行完成
 * 5. 执行完成后从future获取信号
 *
 * 线程安全：任务在工作线程中执行，通过promise/future机制实现同步
 */
class TC_DATABASE_API SQLQueryHolderTask : public SQLOperation
{
    private:
        /**
         * @brief 查询持有者智能指针
         *
         * 使用shared_ptr确保在任务执行期间持有者对象不会被销毁
         */
        std::shared_ptr<SQLQueryHolderBase> m_holder;

        /**
         * @brief 结果承诺对象
         *
         * 用于在工作线程中设置完成信号，主线程通过future等待
         */
        QueryResultHolderPromise m_result;

    public:
        /**
         * @brief 构造函数
         *
         * @brief 简要说明：使用查询持有者初始化任务
         * @param holder 查询持有者共享指针
         *
         * 调用时机：需要批量执行查询时创建任务
         */
        explicit SQLQueryHolderTask(std::shared_ptr<SQLQueryHolderBase> holder)
            : m_holder(std::move(holder)) { }

        /**
         * @brief 析构函数
         */
        ~SQLQueryHolderTask();

        /**
         * @brief 执行任务
         *
         * @brief 简要说明：遍历所有查询并执行，将结果存储回持有者
         * @return bool 总是返回true
         *
         * 调用时机：由数据库工作线程调用
         * 性能注意事项：会遍历所有查询，执行时间取决于查询数量和复杂度
         *
         * 执行流程：
         * 1. 遍历m_queries中的每个查询
         * 2. 执行预处理语句
         * 3. 将结果存储到持有者中
         * 4. 设置promise完成信号
         */
        bool Execute() override;

        /**
         * @brief 获取结果future
         *
         * @brief 简要说明：返回用于等待任务完成的future对象
         * @return QueryResultHolderFuture future对象
         *
         * 调用时机：任务添加到队列后立即调用，用于后续等待
         * 性能注意事项：返回future的拷贝，开销极小
         */
        QueryResultHolderFuture GetFuture() { return m_result.get_future(); }
};

/**
 * @class SQLQueryHolderCallback
 * @brief SQL查询持有者回调封装类
 *
 * 该类封装了查询持有者、future和回调函数，提供了一种便捷的方式来
 * 处理异步查询完成事件。通常用于更新循环中检查查询是否完成。
 *
 * 使用示例：
 * @code
 * auto holder = std::make_shared<SQLQueryHolder<CharacterDatabaseConnection>>();
 * // ... 设置查询 ...
 * SQLQueryHolderCallback callback = CharacterDatabase.QueryHolder(std::move(holder));
 * callback.AfterComplete([](SQLQueryHolderBase const& holder) {
 *     // 处理查询结果
 *     auto result = holder.GetPreparedResult(0);
 *     // ...
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
 * 4. 查询完成时自动触发回调
 */
class TC_DATABASE_API SQLQueryHolderCallback
{
public:
    /**
     * @brief 构造函数
     *
     * @brief 简要说明：使用持有者和future初始化回调对象
     * @param holder 查询持有者右值引用
     * @param future 结果future右值引用
     *
     * 调用时机：由数据库接口方法创建
     */
    SQLQueryHolderCallback(std::shared_ptr<SQLQueryHolderBase>&& holder, QueryResultHolderFuture&& future)
        : m_holder(std::move(holder)), m_future(std::move(future)) { }

    /**
     * @brief 移动构造函数
     */
    SQLQueryHolderCallback(SQLQueryHolderCallback&&) = default;

    /**
     * @brief 移动赋值运算符
     */
    SQLQueryHolderCallback& operator=(SQLQueryHolderCallback&&) = default;

    /**
     * @brief 设置完成回调函数
     *
     * @brief 简要说明：注册查询完成后执行的回调函数
     * @param callback 回调函数，接收查询持有者常量引用
     *
     * 调用时机：创建callback后立即调用，设置处理逻辑
     * 注意：只能在左值对象上调用（使用&限定符）
     */
    void AfterComplete(std::function<void(SQLQueryHolderBase const&)> callback) &
    {
        m_callback = std::move(callback);
    }

    /**
     * @brief 检查并调用回调
     *
     * @brief 简要说明：检查查询是否完成，如果完成则执行回调
     * @return bool 查询完成且回调已执行返回true，否则返回false
     *
     * 调用时机：在游戏更新循环中定期调用
     * 性能注意事项：使用wait_for(0)进行非阻塞检查，性能开销小
     *
     * 执行流程：
     * 1. 检查future是否有效
     * 2. 非阻塞等待future状态
     * 3. 如果已就绪，调用回调函数
     * 4. 返回执行状态
     */
    bool InvokeIfReady();

    /**
     * @brief 查询持有者智能指针
     */
    std::shared_ptr<SQLQueryHolderBase> m_holder;

    /**
     * @brief 结果future对象
     */
    QueryResultHolderFuture m_future;

    /**
     * @brief 完成回调函数
     *
     * 当查询完成时，InvokeIfReady()会调用此函数，传入持有者引用
     */
    std::function<void(SQLQueryHolderBase const&)> m_callback;
};

#endif
