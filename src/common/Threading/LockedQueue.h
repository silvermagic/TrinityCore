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
 * @file LockedQueue.h
 * @brief 基于互斥锁的线程安全队列实现
 *
 * @details 本文件提供了一个通用的线程安全队列模板类 LockedQueue。
 * 该队列使用互斥锁（std::mutex）来保护内部数据结构，确保多线程环境下的安全访问。
 *
 * ## 模块职责
 * - 提供线程安全的队列数据结构
 * - 支持多生产者多消费者（MPMC）场景
 * - 提供队列的添加、获取、取消等基本操作
 *
 * ## 主要功能
 * 1. 线程安全的入队和出队操作
 * 2. 支持批量重新添加元素到队列前端
 * 3. 支持条件检查的出队操作
 * 4. 队列取消机制
 * 5. 队列元素的查看（peek）功能
 *
 * ## 并发队列原理
 * 该队列采用互斥锁（mutex）机制实现线程安全：
 * - 所有对队列的访问操作（读/写）都需要先获取互斥锁
 * - 使用 RAII 风格的 std::lock_guard 自动管理锁的生命周期
 * - 支持手动加锁/解锁，允许更灵活的访问控制
 *
 * ## 性能特点
 * - 优点：实现简单，功能完整，适用于通用场景
 * - 缺点：锁竞争可能导致性能瓶颈，不适合超高并发场景
 * - 适用场景：中等并发量，需要完整队列功能的场景
 *
 * ## 使用示例
 * @code
 * LockedQueue<int> queue;
 * queue.add(42);           // 线程安全地添加元素
 * int value;
 * if (queue.next(value))   // 线程安全地获取元素
 *     // 使用 value
 * @endcode
 */

#ifndef LOCKEDQUEUE_H
#define LOCKEDQUEUE_H

#include <deque>
#include <mutex>

/**
 * @class LockedQueue
 * @brief 基于互斥锁的线程安全队列模板类
 *
 * @details 该类提供了一个通用的线程安全队列实现，使用互斥锁保护内部数据结构。
 * 支持多生产者多消费者（MPMC）场景，适用于需要线程安全队列的各种情况。
 *
 * ## 模板参数
 * @tparam T 存储在队列中的元素类型
 * @tparam StorageType 底层容器类型，默认为 std::deque<T>
 *                     可以是任何支持 push_back、pop_front、front、empty 等操作的容器
 *
 * ## 线程安全保证
 * - 所有公共方法都是线程安全的
 * - 内部使用 std::mutex 保护所有状态变更
 * - peek() 方法支持手动锁管理，需要调用者谨慎使用
 *
 * ## 设计说明
 * - 使用 RAII 管理锁的生命周期（std::lock_guard）
 * - 提供取消机制，可以优雅地停止队列处理
 * - 支持条件检查的出队操作，允许更灵活的处理逻辑
 */
template <class T, typename StorageType = std::deque<T> >
class LockedQueue
{
    //! 互斥锁，用于保护队列的并发访问
    //! 所有对队列的操作都需要先获取此锁
    std::mutex _lock;

    //! 队列的底层存储容器
    //! 默认使用 std::deque，支持快速的前端和后端操作
    StorageType _queue;

    //! 取消标志，volatile 确保多线程可见性
    //! 设置为 true 后，队列进入取消状态
    volatile bool _canceled;

public:

    /**
     * @brief 构造一个空的 LockedQueue 对象
     *
     * @details 初始化队列为空，取消标志为 false
     * 该构造函数是线程安全的（构造期间不需要加锁）
     */
    LockedQueue()
        : _canceled(false)
    {
    }

    /**
     * @brief 虚析构函数
     *
     * @details 析构队列，不清理队列中的元素
     * 注意：调用者需要确保在析构前队列中不再有元素，
     * 或者在析构前显式清空队列
     */
    virtual ~LockedQueue()
    {
    }

    /**
     * @brief 向队列尾部添加一个元素
     *
     * @details 线程安全地将元素添加到队列末尾。
     * 使用手动加锁/解锁，而非 std::lock_guard。
     *
     * @param item 要添加的元素（const 引用）
     *
     * @note 该方法是线程安全的，可以被多个线程并发调用
     * @note 使用拷贝语义，item 会被复制到队列中
     */
    void add(const T& item)
    {
        lock();  // 手动加锁，保护队列访问

        _queue.push_back(item);  // 将元素添加到队列尾部

        unlock();  // 手动解锁，允许其他线程访问
    }

    /**
     * @brief 将多个元素批量添加到队列前端
     *
     * @details 线程安全地将一个元素范围插入到队列的前端。
     * 这通常用于将之前取出的元素重新放回队列，优先处理。
     *
     * @tparam Iterator 迭代器类型，自动推导
     * @param begin 范围起始迭代器
     * @param end 范围结束迭代器
     *
     * @note 使用 std::lock_guard 自动管理锁的生命周期
     * @note 元素会按照迭代器顺序插入到队列前端
     *
     * ## 使用场景
     * 当某些元素需要重新处理时，可以将其重新添加到队列前端，
     * 确保它们优先于其他元素被处理。
     */
    template<class Iterator>
    void readd(Iterator begin, Iterator end)
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁，自动解锁
        _queue.insert(_queue.begin(), begin, end);  // 将元素范围插入到队列前端
    }

    /**
     * @brief 从队列前端获取下一个元素
     *
     * @details 线程安全地从队列前端取出一个元素。
     * 如果队列为空，返回 false，不修改 result。
     *
     * @param[out] result 输出参数，用于存储取出的元素
     * @return true 成功获取元素
     * @return false 队列为空，未获取元素
     *
     * @note 该方法是线程安全的，可以被多个消费者线程并发调用
     * @note 使用 std::lock_guard 自动管理锁的生命周期
     * @note 元素会从队列中移除
     *
     * ## 典型用法
     * @code
     * T item;
     * while (queue.next(item)) {
     *     // 处理 item
     * }
     * @endcode
     */
    bool next(T& result)
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁

        // 检查队列是否为空
        if (_queue.empty())
            return false;  // 队列为空，返回 false

        result = _queue.front();  // 获取队首元素
        _queue.pop_front();       // 移除队首元素

        return true;  // 成功获取元素
    }

    /**
     * @brief 从队列前端获取下一个元素，并执行条件检查
     *
     * @details 线程安全地从队列前端取出一个元素，但在移除前会调用检查器进行验证。
     * 如果检查器返回 false，元素不会被移除，方法返回 false。
     * 这允许在出队时进行额外的验证逻辑。
     *
     * @tparam Checker 检查器类型，必须有 Process(T&) 方法
     * @param[out] result 输出参数，用于存储取出的元素
     * @param[in,out] check 检查器对象，用于验证元素是否可以被处理
     * @return true 成功获取元素并通过检查
     * @return false 队列为空，或检查器返回 false
     *
     * @note 该方法是线程安全的
     * @note 如果检查失败，元素会保留在队列前端，供下次尝试
     *
     * ## 使用场景
     * - 需要根据当前状态决定是否处理某个元素
     * - 实现优先级调度或条件调度
     * - 延迟处理某些暂时无法处理的任务
     *
     * ## 示例
     * @code
     * struct MyChecker {
     *     bool Process(MyTask& task) {
     *         return task.CanExecute();  // 只有可执行的任务才返回 true
     *     }
     * };
     * MyChecker checker;
     * T item;
     * if (queue.next(item, checker)) {
     *     // 处理 item
     * }
     * @endcode
     */
    template<class Checker>
    bool next(T& result, Checker& check)
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁

        // 检查队列是否为空
        if (_queue.empty())
            return false;

        result = _queue.front();  // 获取队首元素（不移除）

        // 调用检查器验证元素是否可以被处理
        if (!check.Process(result))
            return false;  // 检查失败，元素保留在队列中

        _queue.pop_front();  // 检查通过，移除队首元素
        return true;  // 成功获取元素
    }

    /**
     * @brief 查看队列前端元素（不移除）
     *
     * @details 获取队列前端元素的引用，但不移除它。
     * 调用前必须确保队列不为空，否则行为未定义！
     *
     * @warning 此方法会加锁，调用者需要根据 autoUnlock 参数决定是否自动解锁。
     *          如果 autoUnlock == false，调用者必须手动调用 unlock()！
     * @warning 在持有锁期间，其他线程会被阻塞，请尽快释放锁！
     *
     * @param autoUnlock 是否自动解锁，默认为 false
     *                   - true: 获取元素后自动解锁，适合简单查看
     *                   - false: 保持锁定状态，适合需要进一步操作的场景
     * @return T& 队列前端元素的引用
     *
     * @note 该方法是线程安全的，但需要调用者谨慎管理锁的生命周期
     * @note 调用前应该先调用 empty() 检查队列是否为空
     *
     * ## 使用场景
     * - 需要查看队列头部元素但不立即移除
     * - 需要根据元素内容决定是否出队
     * - 需要对队列进行复杂的原子操作
     *
     * ## 危险用法（autoUnlock=false）
     * @code
     * T& item = queue.peek();  // 锁被持有
     * // ... 处理 item
     * queue.unlock();  // 必须手动解锁！
     * @endcode
     *
     * ## 安全用法（autoUnlock=true）
     * @code
     * T& item = queue.peek(true);  // 自动解锁
     * // 使用 item（注意：此时其他线程可能修改队列）
     * @endcode
     */
    T& peek(bool autoUnlock = false)
    {
        lock();  // 手动加锁

        T& result = _queue.front();  // 获取队首元素的引用

        // 根据 autoUnlock 参数决定是否自动解锁
        if (autoUnlock)
            unlock();

        return result;  // 返回元素引用（可能仍持有锁）
    }

    /**
     * @brief 取消队列操作
     *
     * @details 设置队列的取消标志，表示队列不再接受新操作。
     * 这是一种优雅停止队列处理机制的信号。
     *
     * @note 该方法是线程安全的
     * @note 取消后，队列仍然可以正常操作，只是状态变为已取消
     * @note 调用者需要定期检查 cancelled() 来响应取消请求
     *
     * ## 使用场景
     * - 服务器关闭时停止任务队列
     * - 需要优雅停止后台线程处理
     * - 实现可取消的后台任务系统
     */
    void cancel()
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁

        _canceled = true;  // 设置取消标志
    }

    /**
     * @brief 检查队列是否已被取消
     *
     * @details 查询队列的取消状态，用于判断是否应该停止处理队列中的任务。
     *
     * @return true 队列已被取消
     * @return false 队列正常运作
     *
     * @note 该方法是线程安全的
     * @note 返回的是调用时的快照状态，状态可能随后改变
     *
     * ## 典型用法
     * @code
     * while (!queue.cancelled()) {
     *     T item;
     *     if (queue.next(item)) {
     *         // 处理 item
     *     }
     * }
     * @endcode
     */
    bool cancelled()
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁
        return _canceled;  // 返回取消标志
    }

    /**
     * @brief 手动锁定队列
     *
     * @details 获取队列的互斥锁，用于需要手动控制锁生命周期的场景。
     *
     * @warning 调用此方法后，必须确保调用 unlock() 释放锁！
     * @warning 在持有锁期间，其他线程访问队列会被阻塞
     * @warning 推荐优先使用 RAII 风格的 std::lock_guard
     *
     * @note 此方法会阻塞，直到获取锁为止
     * @note 如果当前线程已持有锁，行为取决于 std::mutex 的实现（可能死锁）
     *
     * ## 使用场景
     * - 需要对队列进行一系列原子操作
     * - 需要配合 peek() 使用
     * - 需要更细粒度的锁控制
     *
     * ## 典型用法
     * @code
     * queue.lock();
     * // 临界区：对队列进行多个操作
     * if (!queue.empty()) {
     *     T& item = queue.peek(false);  // 不再加锁，因为已经持有锁
     *     // 处理 item
     * }
     * queue.unlock();
     * @endcode
     */
    void lock()
    {
        this->_lock.lock();  // 获取互斥锁，可能阻塞
    }

    /**
     * @brief 手动解锁队列
     *
     * @details 释放队列的互斥锁，允许其他线程访问队列。
     *
     * @warning 只能由持有锁的线程调用！
     * @warning 每次调用 lock() 后必须恰好调用一次 unlock()
     *
     * @note 如果当前线程未持有锁，行为未定义（可能崩溃或抛出异常）
     *
     * @see lock()
     */
    void unlock()
    {
        this->_lock.unlock();  // 释放互斥锁
    }

    /**
     * @brief 移除队列前端元素
     *
     * @details 线程安全地移除队列前端的元素，但不返回它。
     * 适用于只需要删除而不需要处理的场景。
     *
     * @note 该方法是线程安全的
     * @note 如果队列为空，行为取决于底层容器（std::deque 会未定义）
     * @note 建议在调用前检查队列是否为空
     *
     * ## 使用场景
     * - 丢弃不需要的任务或消息
     * - 清理过期数据
     */
    void pop_front()
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁
        _queue.pop_front();  // 移除队首元素
    }

    /**
     * @brief 检查队列是否为空
     *
     * @details 线程安全地检查队列是否不包含任何元素。
     *
     * @return true 队列为空
     * @return false 队列包含至少一个元素
     *
     * @note 该方法是线程安全的
     * @note 返回的是调用时的快照状态，其他线程可能在此之后修改队列
     *
     * @warning 在多线程环境下，empty() 返回 true 并不保证后续操作成功，
     *          因为其他线程可能在检查和后续操作之间修改队列。
     *          例如：
     *          @code
     *          if (!queue.empty()) {  // 检查时非空
     *              // 此时其他线程可能已经取走了元素
     *              queue.pop_front();  // 可能失败！
     *          }
     *          @endcode
     *
     * ## 推荐用法
     * 使用 next() 方法代替 empty() + pop_front() 组合：
     * @code
     * T item;
     * if (queue.next(item)) {  // 原子操作
     *     // 处理 item
     * }
     * @endcode
     */
    bool empty()
    {
        std::lock_guard<std::mutex> lock(_lock);  // RAII 风格加锁
        return _queue.empty();  // 返回队列是否为空
    }
};
#endif
