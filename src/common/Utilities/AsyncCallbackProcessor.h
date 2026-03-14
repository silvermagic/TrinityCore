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
 * @file AsyncCallbackProcessor.h
 * @brief 异步回调处理器模块
 *
 * 本模块提供了一个通用的异步回调处理器模板类，用于管理和执行异步操作完成后的回调函数。
 *
 * @section 模块职责
 * - 管理异步回调对象的集合
 * - 检测异步操作是否完成
 * - 在适当时机执行已就绪的回调
 * - 自动清理已完成或无效的回调
 *
 * @section 主要功能
 * - AddCallback(): 添加新的异步回调对象到管理队列
 * - ProcessReadyCallbacks(): 处理所有已就绪的回调，执行并移除完成的回调
 *
 * @section 异步回调处理原理
 * 异步回调处理器的核心工作流程：
 * 1. 回调添加：当发起异步操作时（如数据库查询），将回调对象添加到处理器中
 * 2. 就绪检测：定期调用 ProcessReadyCallbacks() 检查所有回调的状态
 * 3. 回调执行：对每个回调调用 InvokeIfReady()，如果返回 true 表示回调已执行完成
 * 4. 自动清理：已完成的回调会被自动从队列中移除，未完成的回调保留待下次处理
 *
 * @section 设计模式
 * - 使用模板实现对任意回调类型的支持
 * - 使用移动语义优化性能，避免不必要的拷贝
 * - 采用交换-擦除模式安全地移除元素
 *
 * @section 使用示例
 * 回调类型 T 需要实现 InvokeIfReady() 方法：
 * @code
 * class MyCallback
 * {
 * public:
 *     bool InvokeIfReady()
 *     {
 *         if (IsOperationComplete())
 *         {
 *             ExecuteCallback();
 *             return true; // 表示回调已执行完成，应从队列中移除
 *         }
 *         return false; // 操作未完成，保留在队列中
 *     }
 * };
 * @endcode
 */

#ifndef AsyncCallbackProcessor_h__
#define AsyncCallbackProcessor_h__

#include "Define.h"
#include <algorithm>
#include <vector>

//template <class T>
//concept AsyncCallback = requires(T t) { { t.InvokeIfReady() } -> std::convertible_to<bool> };

/**
 * @class AsyncCallbackProcessor
 * @brief 异步回调处理器模板类
 *
 * 该模板类提供了一个通用的异步回调管理框架，支持任意类型的回调对象。
 * 回调类型 T 必须实现 InvokeIfReady() 方法，该方法返回 bool 值：
 * - true: 表示回调已成功执行或已完成，应从队列中移除
 * - false: 表示异步操作尚未完成，回调仍需保留在队列中
 *
 * @tparam T 回调类型，必须具有 InvokeIfReady() 方法
 *
 * @note 该类不可拷贝，以防止回调队列的意外复制
 * @note 使用 C++20 concept (已注释) 可在编译期约束模板参数
 */
template<typename T> // requires AsyncCallback<T>
class AsyncCallbackProcessor
{
public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个空的回调处理器实例
     */
    AsyncCallbackProcessor() = default;

    /**
     * @brief 析构函数
     *
     * 销毁回调处理器，自动清理所有未完成的回调
     */
    ~AsyncCallbackProcessor() = default;

    /**
     * @brief 添加异步回调到处理队列
     *
     * 将一个异步回调对象添加到管理队列中。该回调将在后续的 ProcessReadyCallbacks()
     * 调用中被检查和执行。
     *
     * 使用移动语义来避免不必要的拷贝操作，提高性能。
     *
     * @param query 要添加的回调对象（使用右值引用，支持移动语义）
     * @return T& 返回队列中新添加的回调对象的引用，可用于后续操作
     *
     * @example
     * @code
     * AsyncCallbackProcessor<DatabaseCallback> processor;
     * DatabaseCallback& callback = processor.AddCallback(CreateDatabaseQuery(...));
     * // 可以保存 callback 引用以进行后续检查或取消操作
     * @endcode
     */
    T& AddCallback(T&& query)
    {
        // 使用 emplace_back 直接在容器中构造对象，避免额外的移动操作
        _callbacks.emplace_back(std::move(query));
        // 返回新添加的回调对象的引用
        return _callbacks.back();
    }

    /**
     * @brief 处理所有已就绪的回调
     *
     * 遍历所有注册的回调，检查其是否就绪并执行相应的回调操作。
     * 已完成执行的回调会自动从队列中移除，未完成的回调保留待下次处理。
     *
     * @section 处理流程
     * 1. 检查回调队列是否为空，为空则直接返回
     * 2. 将当前回调队列移动到临时向量中（避免迭代时修改原容器）
     * 3. 对每个回调调用 InvokeIfReady()：
     *    - 如果返回 true，表示回调已执行完成，将其移除
     *    - 如果返回 false，表示操作未完成，保留回调
     * 4. 将未完成的回调移回原队列，等待下次处理
     *
     * @section 线程安全
     * 此方法本身不是线程安全的，调用者需确保在正确的线程上下文中调用。
     * 通常应在主线程或特定的回调处理线程中调用。
     *
     * @section 性能优化
     * - 使用移动语义转移队列，避免拷贝开销
     * - 使用 std::remove_if 配合 erase 高效移除元素
     * - 使用 make_move_iterator 优化元素移动操作
     */
    void ProcessReadyCallbacks()
    {
        // 如果回调队列为空，直接返回，避免不必要的操作
        if (_callbacks.empty())
            return;

        // 将原回调队列移动到临时向量中
        // 这样做的好处：
        // 1. 避免在遍历过程中修改原容器
        // 2. 允许新的回调在处理过程中被添加到原队列
        // 3. 使用移动语义，避免拷贝开销
        std::vector<T> updateCallbacks{ std::move(_callbacks) };

        // 使用 erase-remove 惯用法移除已完成的回调
        // std::remove_if 将返回 true 的元素移到容器末尾，并返回新的逻辑结束位置
        // erase 实际删除这些元素
        updateCallbacks.erase(std::remove_if(updateCallbacks.begin(), updateCallbacks.end(), [](T& callback)
        {
            // 对每个回调调用 InvokeIfReady()
            // 返回 true 表示回调已执行完成，应该被移除
            // 返回 false 表示操作未完成，回调保留在队列中
            return callback.InvokeIfReady();
        }), updateCallbacks.end());

        // 将未完成的回调移回原队列
        // 使用 make_move_iterator 将输入迭代器转换为移动迭代器
        // 确保回调对象被移动而非拷贝，提高性能
        // 这样做允许新的回调在下次 ProcessReadyCallbacks() 调用时被一起处理
        _callbacks.insert(_callbacks.end(), std::make_move_iterator(updateCallbacks.begin()), std::make_move_iterator(updateCallbacks.end()));
    }

private:
    // 禁用拷贝构造函数，防止回调队列被意外复制
    AsyncCallbackProcessor(AsyncCallbackProcessor const&) = delete;

    // 禁用拷贝赋值运算符，防止回调队列被意外复制
    AsyncCallbackProcessor& operator=(AsyncCallbackProcessor const&) = delete;

    /**
     * @brief 异步回调队列
     *
     * 存储所有已注册但尚未完成执行的异步回调对象。
     *
     * @details
     * - 回调对象在 AddCallback() 中被添加到此队列
     * - ProcessReadyCallbacks() 会遍历并处理此队列中的回调
     * - 已完成的回调会被移除，未完成的回调保留在此队列中
     *
     * @note 使用 std::vector 提供：
     * - 高效的随机访问
     * - 连续的内存布局（缓存友好）
     * - 动态扩容能力
     */
    std::vector<T> _callbacks;
};

#endif // AsyncCallbackProcessor_h__
