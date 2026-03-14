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
 * @file QueryHolder.cpp
 * @brief SQL查询持有者实现文件
 *
 * 本文件实现了SQLQueryHolderBase和SQLQueryHolderTask的核心功能，包括：
 * - 查询的存储和管理
 * - 批量执行查询并收集结果
 * - 异步任务执行和结果通知
 */

#include "QueryHolder.h"
#include "Errors.h"
#include "Log.h"
#include "MySQLConnection.h"
#include "PreparedStatement.h"
#include "QueryResult.h"

/**
 * @brief 设置预处理查询的实现方法
 *
 * @param index 查询索引位置
 * @param stmt 预处理语句基类指针
 * @return bool 成功返回true，索引越界返回false
 *
 * 该方法由派生类的SetPreparedQuery模板方法调用，用于将预处理语句
 * 存储到指定索引位置。如果索引越界，会记录错误日志并返回false。
 *
 * 调用时机：在设置好持有者大小后，提交执行前调用
 * 性能注意事项：包含边界检查，O(1)时间复杂度
 */
bool SQLQueryHolderBase::SetPreparedQueryImpl(size_t index, PreparedStatementBase* stmt)
{
    // 检查索引是否越界
    if (m_queries.size() <= index)
    {
        TC_LOG_ERROR("sql.sql", "Query index ({}) out of range (size: {}) for prepared statement", uint32(index), (uint32)m_queries.size());
        return false;
    }

    // 存储预处理语句到指定位置
    m_queries[index].first = stmt;
    return true;
}

/**
 * @brief 获取指定索引位置的查询结果
 *
 * @param index 查询索引位置（从0开始）
 * @return PreparedQueryResult 查询结果智能指针，可能为空
 *
 * 该方法用于在查询执行完成后获取结果。如果索引越界，会触发断言失败。
 *
 * 调用时机：在SQLQueryHolderTask执行完成后调用
 * 性能注意事项：O(1)时间复杂度，直接访问vector元素
 */
PreparedQueryResult SQLQueryHolderBase::GetPreparedResult(size_t index) const
{
    // 检查索引是否越界，越界则触发断言失败
    // 注意：不应使用该函数查询预处理语句的索引
    ASSERT(index < m_queries.size(), "Query holder result index out of range, tried to access index " SZFMTD " but there are only " SZFMTD " results",
        index, m_queries.size());

    // 返回存储的查询结果
    return m_queries[index].second;
}

/**
 * @brief 设置指定索引位置的查询结果
 *
 * @param index 查询索引位置
 * @param result 预处理结果集指针
 *
 * 该方法由SQLQueryHolderTask::Execute()调用，用于存储查询执行结果。
 * 如果结果集为空（行数为0），会删除结果集对象以节省内存。
 *
 * 调用时机：仅由SQLQueryHolderTask::Execute()内部调用
 * 性能注意事项：如果结果为空，会立即释放内存
 */
void SQLQueryHolderBase::SetPreparedResult(size_t index, PreparedResultSet* result)
{
    // 如果结果集存在但没有数据行，删除它以节省内存
    if (result && !result->GetRowCount())
    {
        delete result;
        result = nullptr;
    }

    // 将结果存储到持有者中，使用shared_ptr管理生命周期
    if (index < m_queries.size())
        m_queries[index].second = PreparedQueryResult(result);
}

/**
 * @brief SQL查询持有者析构函数
 *
 * 析构函数会清理所有存储的预处理语句对象。
 * 注意：
 * - 只清理PreparedStatement对象，不清理结果集
 * - 结果集由shared_ptr管理，会自动释放
 * - 如果结果未被使用，资源会在此处释放
 */
SQLQueryHolderBase::~SQLQueryHolderBase()
{
    // 遍历所有查询，清理预处理语句对象
    for (std::pair<PreparedStatementBase*, PreparedQueryResult>& query : m_queries)
    {
        // 删除预处理语句对象
        // 注意：结果集由shared_ptr自动管理，不需要手动删除
        // 如果结果从未被使用（未调用GetPreparedResult），shared_ptr会自动释放
        delete query.first;
    }
}

/**
 * @brief 设置查询持有者的大小
 *
 * @param size 查询数量
 *
 * 该方法用于预先分配足够的空间来存储指定数量的查询。
 * 使用resize而不是push_back可以避免多次内存重分配，提高性能。
 *
 * 调用时机：在设置任何查询之前调用
 * 性能注意事项：一次性分配内存，优于多次push_back
 */
void SQLQueryHolderBase::SetSize(size_t size)
{
    // 调整向量大小以容纳指定数量的查询
    // 这样可以避免push_back带来的多次内存重分配
    m_queries.resize(size);
}

/**
 * @brief SQL查询持有者任务析构函数
 *
 * 默认析构函数，由编译器自动生成。
 * 成员变量的析构会自动处理资源释放：
 * - m_holder是shared_ptr，会自动减少引用计数
 * - m_result是promise，会自动设置异常状态（如果有）
 */
SQLQueryHolderTask::~SQLQueryHolderTask() = default;

/**
 * @brief 执行SQL查询持有者任务
 *
 * @return bool 总是返回true
 *
 * 该方法是SQLOperation的虚函数实现，在数据库工作线程中执行。
 * 它会遍历持有者中的所有查询，逐个执行并将结果存储回持有者。
 *
 * 执行流程：
 * 1. 遍历m_queries中的每个查询
 * 2. 如果预处理语句存在，执行查询
 * 3. 将结果存储到持有者中
 * 4. 设置promise完成信号
 *
 * 调用时机：由数据库工作线程调用
 * 性能注意事项：执行时间取决于查询数量和复杂度
 *
 * 注意：即使某些查询失败，也会继续执行后续查询
 */
bool SQLQueryHolderTask::Execute()
{
    // 遍历并执行所有查询，将结果存储到持有者中
    for (size_t i = 0; i < m_holder->m_queries.size(); ++i)
    {
        // 如果预处理语句存在，执行查询
        if (PreparedStatementBase* stmt = m_holder->m_queries[i].first)
        {
            // 执行查询并存储结果
            // m_conn是SQLOperation的成员，表示当前数据库连接
            m_holder->SetPreparedResult(i, m_conn->Query(stmt));
        }
    }

    // 设置promise完成信号，通知等待的线程
    m_result.set_value();
    return true;
}

/**
 * @brief 检查查询是否完成并执行回调
 *
 * @return bool 查询完成且回调已执行返回true，否则返回false
 *
 * 该方法用于非阻塞地检查查询是否已完成，如果完成则执行注册的回调函数。
 * 通常在游戏更新循环中定期调用。
 *
 * 执行流程：
 * 1. 检查future是否有效
 * 2. 使用wait_for(0)进行非阻塞等待
 * 3. 如果future已就绪，调用回调函数
 * 4. 返回执行状态
 *
 * 调用时机：在游戏更新循环中定期调用
 * 性能注意事项：wait_for(0)是非阻塞检查，性能开销极小
 */
bool SQLQueryHolderCallback::InvokeIfReady()
{
    // 检查future是否有效且已就绪
    if (m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        // future已就绪，执行回调函数
        // 传入持有者的常量引用，允许回调读取结果
        m_callback(*m_holder);
        return true;
    }

    // future未就绪或无效，返回false
    return false;
}
