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
 * @file QueryCallback.h
 * @brief 异步查询回调处理模块
 *
 * 本模块实现了异步数据库查询的回调机制，主要功能包括：
 * - 封装异步查询的future对象
 * - 支持回调函数链式调用
 * - 支持普通查询和预处理查询两种类型
 * - 支持链式查询（一个查询完成后触发下一个）
 *
 * 设计模式：
 * - 使用std::future进行异步结果传递
 * - 支持链式调用模式（Fluent Interface）
 * - 使用union节省内存（只能持有一种future）
 *
 * 使用场景：
 * - 异步数据库操作完成后需要执行回调
 * - 需要按顺序执行多个相关查询
 * - 在主线程处理异步查询结果
 *
 * 线程安全：
 * - future从工作线程传递到主线程
 * - 回调在主线程执行
 */

#ifndef _QUERY_CALLBACK_H
#define _QUERY_CALLBACK_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <functional>
#include <future>
#include <list>
#include <queue>
#include <utility>

/**
 * @class QueryCallback
 * @brief 异步查询回调处理类
 *
 * 封装异步查询的future和回调函数，支持链式调用和回调链。
 * 当异步查询完成时，执行注册的回调函数处理结果。
 *
 * 使用示例：
 * @code
 * // 单次回调
 * database.AsyncQuery(sql).WithCallback([](QueryResult result) {
 *     // 处理结果
 * });
 *
 * // 链式回调
 * database.AsyncQuery(sql1)
 *     .WithChainingCallback([](QueryCallback& cb, QueryResult result) {
 *         // 处理第一个查询结果
 *         // 可以设置下一个查询
 *         cb.SetNextQuery(database.AsyncQuery(sql2));
 *     });
 * @endcode
 */
class TC_DATABASE_API QueryCallback
{
public:
    /**
     * @brief 构造函数 - 从普通查询future创建
     * @param result 查询结果的future对象
     */
    explicit QueryCallback(QueryResultFuture&& result);

    /**
     * @brief 构造函数 - 从预处理查询future创建
     * @param result 预处理查询结果的future对象
     */
    explicit QueryCallback(PreparedQueryResultFuture&& result);

    /**
     * @brief 移动构造函数
     * @param right 要移动的对象
     */
    QueryCallback(QueryCallback&& right);

    /**
     * @brief 移动赋值运算符
     * @param right 要移动的对象
     * @return 当前对象引用
     */
    QueryCallback& operator=(QueryCallback&& right);

    /**
     * @brief 析构函数
     */
    ~QueryCallback();

    /**
     * @brief 设置普通查询回调函数
     * @param callback 回调函数，接收QueryResult参数
     * @return 当前对象的右值引用，支持链式调用
     *
     * 回调函数在查询完成时被调用，接收查询结果
     */
    QueryCallback&& WithCallback(std::function<void(QueryResult)>&& callback);

    /**
     * @brief 设置预处理查询回调函数
     * @param callback 回调函数，接收PreparedQueryResult参数
     * @return 当前对象的右值引用，支持链式调用
     */
    QueryCallback&& WithPreparedCallback(std::function<void(PreparedQueryResult)>&& callback);

    /**
     * @brief 设置链式回调函数（普通查询）
     * @param callback 回调函数，接收QueryCallback引用和QueryResult
     * @return 当前对象的右值引用，支持链式调用
     *
     * 链式回调可以通过SetNextQuery设置下一个查询，实现查询链
     */
    QueryCallback&& WithChainingCallback(std::function<void(QueryCallback&, QueryResult)>&& callback);

    /**
     * @brief 设置链式回调函数（预处理查询）
     * @param callback 回调函数，接收QueryCallback引用和PreparedQueryResult
     * @return 当前对象的右值引用，支持链式调用
     */
    QueryCallback&& WithChainingPreparedCallback(std::function<void(QueryCallback&, PreparedQueryResult)>&& callback);

    /**
     * @brief 设置下一个查询
     * @param next 下一个查询的回调对象
     *
     * 将下一个查询的future移动到当前对象，用于链式查询
     */
    void SetNextQuery(QueryCallback&& next);

    /**
     * @brief 如果查询就绪则调用回调
     * @return 查询完成且回调链结束返回true，否则返回false
     *
     * 检查future是否就绪，如果就绪则执行回调。
     * 应该在主循环中定期调用，直到返回true。
     */
    bool InvokeIfReady();

private:
    // 禁止拷贝
    QueryCallback(QueryCallback const& right) = delete;
    QueryCallback& operator=(QueryCallback const& right) = delete;

    // 友元函数用于union成员的构造、析构和移动
    template<typename T> friend void ConstructActiveMember(T* obj);
    template<typename T> friend void DestroyActiveMember(T* obj);
    template<typename T> friend void MoveFrom(T* to, T&& from);

    // 使用union存储两种类型的future，节省内存
    // 同一时间只能持有其中一种
    union
    {
        QueryResultFuture _string;              // 普通查询结果future
        PreparedQueryResultFuture _prepared;    // 预处理查询结果future
    };
    bool _isPrepared;                           // 标识当前持有哪种类型的future

    struct QueryCallbackData;                   // 回调数据结构（在cpp中定义）
    std::queue<QueryCallbackData, std::list<QueryCallbackData>> _callbacks;  // 回调队列，支持链式回调
};

#endif // _QUERY_CALLBACK_H
