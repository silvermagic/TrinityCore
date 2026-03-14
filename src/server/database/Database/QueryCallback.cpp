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
 * @file QueryCallback.cpp
 * @brief 异步查询回调处理实现
 *
 * 本文件实现了QueryCallback类，提供异步查询回调的核心功能。
 * 使用placement new和union技术实现类型安全的变体类型，
 * 同时支持普通查询和预处理查询的回调处理。
 *
 * 关键技术：
 * - union存储不同类型的future，节省内存
 * - 使用placement new在union中构造对象
 * - 手动管理union成员的生命周期
 * - 支持链式回调和链式查询
 */

#include "QueryCallback.h"
#include "Errors.h"

/**
 * @brief 在指定内存位置构造对象
 * @tparam T 对象类型
 * @tparam Args 构造函数参数类型
 * @param t 目标内存位置
 * @param args 构造函数参数
 *
 * 使用placement new在已有内存上构造对象
 */
template<typename T, typename... Args>
inline void Construct(T& t, Args&&... args)
{
    new (&t) T(std::forward<Args>(args)...);
}

/**
 * @brief 在指定内存位置析构对象
 * @tparam T 对象类型
 * @param t 要析构的对象
 *
 * 显式调用析构函数，用于union成员的手动生命周期管理
 */
template<typename T>
inline void Destroy(T& t)
{
    t.~T();
}

/**
 * @brief 构造当前活跃的union成员
 * @tparam T QueryCallback或QueryCallbackData类型
 * @param obj 对象指针
 *
 * 根据_isPrepared标志决定构造哪个union成员
 */
template<typename T>
inline void ConstructActiveMember(T* obj)
{
    if (!obj->_isPrepared)
        Construct(obj->_string);
    else
        Construct(obj->_prepared);
}

/**
 * @brief 析构当前活跃的union成员
 * @tparam T QueryCallback或QueryCallbackData类型
 * @param obj 对象指针
 *
 * 根据_isPrepared标志决定析构哪个union成员
 */
template<typename T>
inline void DestroyActiveMember(T* obj)
{
    if (!obj->_isPrepared)
        Destroy(obj->_string);
    else
        Destroy(obj->_prepared);
}

/**
 * @brief 移动union成员数据
 * @tparam T QueryCallback或QueryCallbackData类型
 * @param to 目标对象指针
 * @param from 源对象右值引用
 *
 * 移动union中当前活跃的成员，要求两个对象的类型相同
 */
template<typename T>
inline void MoveFrom(T* to, T&& from)
{
    // 确保类型一致
    ASSERT(to->_isPrepared == from._isPrepared);

    // 根据类型移动对应的成员
    if (!to->_isPrepared)
        to->_string = std::move(from._string);
    else
        to->_prepared = std::move(from._prepared);
}

/**
 * @struct QueryCallback::QueryCallbackData
 * @brief 回调数据结构
 *
 * 存储单个回调函数及其类型信息。
 * 使用union存储两种类型的回调函数，类似QueryCallback本身。
 */
struct QueryCallback::QueryCallbackData
{
public:
    friend class QueryCallback;

    /**
     * @brief 构造函数 - 创建普通查询回调
     * @param callback 回调函数
     */
    QueryCallbackData(std::function<void(QueryCallback&, QueryResult)>&& callback) : _string(std::move(callback)), _isPrepared(false) { }

    /**
     * @brief 构造函数 - 创建预处理查询回调
     * @param callback 回调函数
     */
    QueryCallbackData(std::function<void(QueryCallback&, PreparedQueryResult)>&& callback) : _prepared(std::move(callback)), _isPrepared(true) { }

    /**
     * @brief 移动构造函数
     * @param right 源对象
     */
    QueryCallbackData(QueryCallbackData&& right)
    {
        _isPrepared = right._isPrepared;
        ConstructActiveMember(this);
        MoveFrom(this, std::move(right));
    }

    /**
     * @brief 移动赋值运算符
     * @param right 源对象
     * @return 当前对象引用
     */
    QueryCallbackData& operator=(QueryCallbackData&& right)
    {
        if (this != &right)
        {
            // 如果类型不同，需要先销毁再重新构造
            if (_isPrepared != right._isPrepared)
            {
                DestroyActiveMember(this);
                _isPrepared = right._isPrepared;
                ConstructActiveMember(this);
            }
            MoveFrom(this, std::move(right));
        }
        return *this;
    }

    /**
     * @brief 析构函数 - 销毁当前活跃的union成员
     */
    ~QueryCallbackData() { DestroyActiveMember(this); }

private:
    // 禁止拷贝
    QueryCallbackData(QueryCallbackData const&) = delete;
    QueryCallbackData& operator=(QueryCallbackData const&) = delete;

    template<typename T> friend void ConstructActiveMember(T* obj);
    template<typename T> friend void DestroyActiveMember(T* obj);
    template<typename T> friend void MoveFrom(T* to, T&& from);

    // union存储两种类型的回调函数
    union
    {
        std::function<void(QueryCallback&, QueryResult)> _string;              // 普通查询回调
        std::function<void(QueryCallback&, PreparedQueryResult)> _prepared;    // 预处理查询回调
    };
    bool _isPrepared;   // 标识当前回调类型
};

// 不使用初始化列表来避免在clang编译器下不使用预编译头时出现段错误
// 这是一种兼容性处理，确保在各种编译器下都能正常工作

/**
 * @brief 构造函数 - 从普通查询future创建
 * @param result 查询结果future
 *
 * 初始化union成员_string
 */
QueryCallback::QueryCallback(std::future<QueryResult>&& result)
{
    _isPrepared = false;
    Construct(_string, std::move(result));
}

/**
 * @brief 构造函数 - 从预处理查询future创建
 * @param result 预处理查询结果future
 *
 * 初始化union成员_prepared
 */
QueryCallback::QueryCallback(std::future<PreparedQueryResult>&& result)
{
    _isPrepared = true;
    Construct(_prepared, std::move(result));
}

/**
 * @brief 移动构造函数
 * @param right 源对象
 *
 * 移动future和回调队列
 */
QueryCallback::QueryCallback(QueryCallback&& right)
{
    _isPrepared = right._isPrepared;
    ConstructActiveMember(this);
    MoveFrom(this, std::move(right));
    _callbacks = std::move(right._callbacks);
}

/**
 * @brief 移动赋值运算符
 * @param right 源对象
 * @return 当前对象引用
 *
 * 处理类型变化和资源转移
 */
QueryCallback& QueryCallback::operator=(QueryCallback&& right)
{
    if (this != &right)
    {
        // 如果类型不同，需要先销毁再重新构造
        if (_isPrepared != right._isPrepared)
        {
            DestroyActiveMember(this);
            _isPrepared = right._isPrepared;
            ConstructActiveMember(this);
        }
        MoveFrom(this, std::move(right));
        _callbacks = std::move(right._callbacks);
    }
    return *this;
}

/**
 * @brief 析构函数
 *
 * 销毁union中当前活跃的成员
 */
QueryCallback::~QueryCallback()
{
    DestroyActiveMember(this);
}

/**
 * @brief 设置普通查询回调（简单模式）
 * @param callback 回调函数
 * @return 当前对象的右值引用
 *
 * 将简单回调包装为链式回调，简化使用
 */
QueryCallback&& QueryCallback::WithCallback(std::function<void(QueryResult)>&& callback)
{
    // 包装为链式回调，忽略QueryCallback参数
    return WithChainingCallback([callback](QueryCallback& /*this*/, QueryResult result) { callback(std::move(result)); });
}

/**
 * @brief 设置预处理查询回调（简单模式）
 * @param callback 回调函数
 * @return 当前对象的右值引用
 */
QueryCallback&& QueryCallback::WithPreparedCallback(std::function<void(PreparedQueryResult)>&& callback)
{
    return WithChainingPreparedCallback([callback](QueryCallback& /*this*/, PreparedQueryResult result) { callback(std::move(result)); });
}

/**
 * @brief 设置链式回调（普通查询）
 * @param callback 回调函数
 * @return 当前对象的右值引用
 *
 * 回调可以通过SetNextQuery设置下一个查询，实现查询链
 */
QueryCallback&& QueryCallback::WithChainingCallback(std::function<void(QueryCallback&, QueryResult)>&& callback)
{
    // 确保类型匹配：不能在预处理查询上设置普通查询回调
    ASSERT(!_callbacks.empty() || !_isPrepared, "Attempted to set callback function for string query on a prepared async query");
    _callbacks.emplace(std::move(callback));
    return std::move(*this);
}

/**
 * @brief 设置链式回调（预处理查询）
 * @param callback 回调函数
 * @return 当前对象的右值引用
 */
QueryCallback&& QueryCallback::WithChainingPreparedCallback(std::function<void(QueryCallback&, PreparedQueryResult)>&& callback)
{
    // 确保类型匹配：不能在普通查询上设置预处理查询回调
    ASSERT(!_callbacks.empty() || _isPrepared, "Attempted to set callback function for prepared query on a string async query");
    _callbacks.emplace(std::move(callback));
    return std::move(*this);
}

/**
 * @brief 设置下一个查询
 * @param next 下一个查询回调对象
 *
 * 将下一个查询的future移动到当前对象，用于链式查询
 * 要求两个查询类型相同（普通查询或预处理查询）
 */
void QueryCallback::SetNextQuery(QueryCallback&& next)
{
    MoveFrom(this, std::move(next));
}

/**
 * @brief 如果查询就绪则调用回调
 * @return 完成所有回调返回true，否则返回false
 *
 * 执行流程：
 * 1. 检查future是否就绪
 * 2. 如果就绪，执行队列前端的回调
 * 3. 从回调中获取结果并传递给回调函数
 * 4. 检查是否还有后续回调
 * 5. 如果没有回调了且没有下一个查询，返回true
 *
 * 调用时机：应该在主循环中定期调用，直到返回true
 * 性能考虑：使用wait_for(0)进行非阻塞检查
 */
bool QueryCallback::InvokeIfReady()
{
    // 获取队列前端的回调
    QueryCallbackData& callback = _callbacks.front();

    // 检查状态并返回是否完成的lambda函数
    auto checkStateAndReturnCompletion = [this]()
    {
        _callbacks.pop();  // 移除已执行的回调

        // 检查是否还有下一个查询（future是否有效）
        bool hasNext = !_isPrepared ? _string.valid() : _prepared.valid();

        if (_callbacks.empty())
        {
            // 没有更多回调了
            // 如果还有下一个查询但没回调，说明逻辑错误
            ASSERT(!hasNext);
            return true;
        }

        // 中止链：有回调但没有下一个查询结果
        // 这种情况可能是回调没有设置下一个查询
        if (!hasNext)
            return true;

        // 确保下一个回调的类型与当前查询类型匹配
        ASSERT(_isPrepared == _callbacks.front()._isPrepared);
        return false;
    };

    if (!_isPrepared)
    {
        // 处理普通查询
        // 检查future是否有效且就绪
        if (_string.valid() && _string.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            // 移动future和回调，避免悬垂引用
            QueryResultFuture f(std::move(_string));
            std::function<void(QueryCallback&, QueryResult)> cb(std::move(callback._string));

            // 执行回调，传入结果
            cb(*this, f.get());

            return checkStateAndReturnCompletion();
        }
    }
    else
    {
        // 处理预处理查询
        if (_prepared.valid() && _prepared.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            // 移动future和回调
            PreparedQueryResultFuture f(std::move(_prepared));
            std::function<void(QueryCallback&, PreparedQueryResult)> cb(std::move(callback._prepared));

            // 执行回调
            cb(*this, f.get());

            return checkStateAndReturnCompletion();
        }
    }

    // future尚未就绪，返回false表示需要继续等待
    return false;
}
