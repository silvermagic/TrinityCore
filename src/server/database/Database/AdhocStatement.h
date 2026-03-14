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
 * @file AdhocStatement.h
 * @brief 即席SQL语句任务模块
 *
 * 本模块提供对原始SQL语句的封装，用于执行即席查询（Ad-hoc Query）。
 * 即席查询是指那些在运行时动态构建、非预定义的SQL查询语句。
 *
 * 主要用途：
 * - 执行一次性SQL操作（如服务器启动时的初始化查询）
 * - 执行动态构建的SQL语句
 * - 支持同步和异步两种执行模式
 *
 * 与预处理语句（PreparedStatement）的区别：
 * - 即席语句：每次执行都需要完整解析SQL，适合一次性查询
 * - 预处理语句：SQL只需解析一次，适合重复执行的查询，效率更高
 */

#ifndef _ADHOCSTATEMENT_H
#define _ADHOCSTATEMENT_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "SQLOperation.h"

/**
 * @class BasicStatementTask
 * @brief 即席SQL语句任务类
 *
 * 封装原始SQL字符串，作为数据库工作队列中的任务单元。
 * 继承自 SQLOperation，可被 DatabaseWorker 线程池调度执行。
 *
 * 支持两种模式：
 * 1. 异步模式（async=true）：执行后返回结果集，通过 Future/Promise 机制获取结果
 * 2. 同步模式（async=false）：仅执行SQL，不关心结果（如 INSERT/UPDATE/DELETE）
 *
 * 使用场景：
 * - 服务器启动时的一次性初始化查询
 * - 动态构建的SQL语句执行
 * - 不适合频繁执行的查询（应使用 PreparedStatement）
 */
class TC_DATABASE_API BasicStatementTask : public SQLOperation
{
    public:
        /**
         * @brief 构造函数
         * @param sql   要执行的原始SQL语句字符串
         * @param async 是否为异步模式（需要返回结果集）
         *              - true: 创建 Promise 对象，调用者可通过 GetFuture() 获取结果
         *              - false: 仅执行SQL，不返回结果
         *
         * @note SQL字符串会被复制（strdup），调用者无需保持原字符串生命周期
         * @note 异步模式下会创建 QueryResultPromise 对象，用于跨线程传递查询结果
         */
        BasicStatementTask(char const* sql, bool async = false);

        /**
         * @brief 析构函数
         *
         * 释放由 strdup() 分配的SQL字符串内存，
         * 如果存在结果对象（异步模式），也会一并释放。
         */
        ~BasicStatementTask();

        /**
         * @brief 执行SQL语句
         * @return 执行结果
         *         - 异步模式：true 表示查询成功且有结果，false 表示查询失败或无结果
         *         - 同步模式：true 表示执行成功，false 表示执行失败
         *
         * @brief 执行流程：
         * 1. 异步模式：
         *    - 调用 MySQLConnection::Query() 执行查询
         *    - 检查结果集是否有数据
         *    - 通过 Promise 设置结果值，供等待的线程获取
         * 2. 同步模式：
         *    - 直接调用 MySQLConnection::Execute() 执行
         *    - 不关心返回结果
         *
         * @note 此方法由 DatabaseWorker 线程调用，不在主线程执行
         */
        bool Execute() override;

        /**
         * @brief 获取查询结果的 Future 对象
         * @return QueryResultFuture 对象，调用者可通过此对象等待并获取查询结果
         *
         * @note 仅在异步模式下有效（构造时 async=true）
         * @note 调用者使用示例：
         *       QueryResultFuture future = task->GetFuture();
         *       QueryResult result = future.get(); // 阻塞等待结果
         */
        QueryResultFuture GetFuture() const { return m_result->get_future(); }

    private:
        char const* m_sql;          ///< 原始SQL查询字符串，由 strdup() 分配，析构时需 free()
        bool m_has_result;          ///< 是否需要返回结果集（异步模式为 true）
        QueryResultPromise* m_result; ///< 结果承诺对象，用于跨线程传递查询结果（仅异步模式）
};

#endif
