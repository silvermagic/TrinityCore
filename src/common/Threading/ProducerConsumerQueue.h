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
 * @file ProducerConsumerQueue.h
 * @brief 生产者-消费者队列模板类
 *
 * 本模块实现了一个线程安全的生产者-消费者队列，用于多线程环境下的任务调度。
 * 主要应用于：
 * - 网络消息处理：接收线程将消息推入队列，处理线程从队列取出并处理
 * - 数据库操作：主线程提交数据库任务，数据库线程执行并返回结果
 * - 异步任务处理：将耗时任务放入队列，由工作线程异步处理
 *
 * 特性：
 * - 线程安全：所有操作都经过互斥锁保护
 * - 阻塞等待：消费者可阻塞等待新任务到达
 * - 优雅关闭：支持安全关闭队列并清理资源
 * - 泛型设计：支持任意数据类型
 */

#ifndef TRINITY_PRODUCER_CONSUMER_QUEUE_H
#define TRINITY_PRODUCER_CONSUMER_QUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>
#include <atomic>
#include <type_traits>

/**
 * @class ProducerConsumerQueue
 * @brief 线程安全的生产者-消费者队列模板类
 *
 * 该类实现了一个经典的生产者-消费者模式队列，支持多个生产者线程同时向队列推送数据，
 * 多个消费者线程同时从队列取数据。使用互斥锁和条件变量实现线程同步。
 *
 * 典型使用场景：
 * @code
 * ProducerConsumerQueue<DatabaseTask*> taskQueue;
 *
 * // 生产者线程
 * taskQueue.Push(new DatabaseTask(...));
 *
 * // 消费者线程
 * DatabaseTask* task;
 * taskQueue.WaitAndPop(task);
 * if (task) {
 *     task->Execute();
 *     delete task;
 * }
 * @endcode
 *
 * @tparam T 队列元素类型，可以是值类型或指针类型
 */
template <typename T>
class ProducerConsumerQueue
{
private:
    /**
     * @brief 队列互斥锁
     *
     * 保护对内部队列的并发访问。使用 mutable 修饰以支持 const 成员函数中的锁定操作。
     * 性能注意：每次队列操作都需要获取此锁，高并发场景下可能成为瓶颈。
     */
    mutable std::mutex _queueLock;

    /**
     * @brief 内部数据队列
     *
     * 使用 std::queue 作为底层容器，采用先进先出(FIFO)策略。
     */
    std::queue<T> _queue;

    /**
     * @brief 条件变量
     *
     * 用于消费者线程阻塞等待新数据到达。当生产者 Push 数据时通知等待的消费者。
     * 关闭队列时通过 notify_all() 唤醒所有等待线程。
     */
    std::condition_variable _condition;

    /**
     * @brief 关闭标志
     *
     * 原子布尔值，标识队列是否已关闭。关闭后：
     * - Pop 和 WaitAndPop 将返回失败
     * - WaitAndPop 会立即返回而非继续等待
     */
    std::atomic<bool> _shutdown;

public:

    /**
     * @brief 构造函数
     *
     * 初始化一个空的生产者-消费者队列，关闭标志初始化为 false。
     */
    ProducerConsumerQueue() : _shutdown(false) { }

    /**
     * @brief 向队列推送元素（左值引用版本）
     *
     * 将元素复制到队列末尾，并通知一个等待的消费者线程。
     * 调用时机：生产者线程有新数据需要处理时调用。
     *
     * @param value 要推入队列的元素（const 左值引用）
     *
     * 性能注意事项：
     * - 会复制元素，对于大型对象建议使用移动语义版本或智能指针
     * - 仅唤醒一个等待线程，避免惊群效应
     */
    void Push(T const& value)
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        _queue.push(value);

        // 通知一个等待的消费者线程，避免不必要的线程唤醒
        _condition.notify_one();
    }

    /**
     * @brief 向队列推送元素（右值引用版本）
     *
     * 将元素移动到队列末尾，避免不必要的拷贝操作。
     * 调用时机：当元素是临时对象或明确需要移动语义时使用。
     *
     * @param value 要推入队列的元素（右值引用）
     *
     * 性能注意事项：
     * - 使用移动语义，适用于大型对象或 unique_ptr 等不可复制的类型
     * - 比左值引用版本更高效
     */
    void Push(T&& value)
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        _queue.push(std::move(value));

        _condition.notify_one();
    }

    /**
     * @brief 检查队列是否为空
     *
     * 调用时机：需要在非阻塞方式下快速检查队列状态时调用。
     *
     * @return 队列为空返回 true，否则返回 false
     *
     * @warning 在多线程环境下，返回值可能立即过时。仅用于诊断或启发式判断，
     *          不应用于同步逻辑。
     */
    bool Empty() const
    {
        std::lock_guard<std::mutex> lock(_queueLock);

        return _queue.empty();
    }

    /**
     * @brief 获取队列中元素数量
     *
     * 调用时机：需要监控队列积压情况或进行统计时调用。
     *
     * @return 队列中的元素数量
     *
     * @warning 与 Empty() 类似，在多线程环境下返回值可能立即过时。
     */
    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(_queueLock);

        return _queue.size();
    }

    /**
     * @brief 非阻塞弹出元素
     *
     * 尝试从队列前端弹出一个元素。如果队列为空或已关闭，则立即返回失败。
     * 调用时机：消费者线程需要尝试处理任务但不愿阻塞等待时使用。
     *
     * @param[out] value 输出参数，用于接收弹出的元素
     * @return 成功弹出返回 true，队列为空或已关闭返回 false
     *
     * 性能注意事项：
     * - 非阻塞操作，适用于轮询模式
     * - 如果队列为空，可立即返回进行其他工作
     */
    bool Pop(T& value)
    {
        std::lock_guard<std::mutex> lock(_queueLock);

        // 检查队列是否为空或已关闭
        if (_queue.empty() || _shutdown)
            return false;

        // 使用移动语义获取前端元素，避免不必要的拷贝
        value = std::move(_queue.front());

        _queue.pop();

        return true;
    }

    /**
     * @brief 阻塞等待并弹出元素
     *
     * 如果队列为空，则阻塞当前线程直到有新元素到达或队列关闭。
     * 这是消费者线程的主要工作方法。
     * 调用时机：消费者线程需要等待任务到达并处理时使用。
     *
     * @param[out] value 输出参数，用于接收弹出的元素
     *
     * 性能注意事项：
     * - 当队列为空时，消费者线程进入休眠状态，不占用 CPU
     * - 被唤醒后需要重新检查队列状态（防止虚假唤醒）
     * - 如果队列已关闭，函数立即返回
     *
     * @note 使用 while 循环而非 wait(lock, predicate) 的原因是某些编译器实现存在 bug：
     *       https://connect.microsoft.com/VisualStudio/feedback/details/1098841
     */
    void WaitAndPop(T& value)
    {
        std::unique_lock<std::mutex> lock(_queueLock);

        // 使用 while 循环而非 wait(lock, predicate)，因为某些 MSVC 版本存在 bug
        // 循环检查：队列为空且队列未关闭时，继续等待
        // we could be using .wait(lock, predicate) overload here but it is broken
        // https://connect.microsoft.com/VisualStudio/feedback/details/1098841
        while (_queue.empty() && !_shutdown)
            _condition.wait(lock);

        // 被唤醒后再次检查，可能是由于关闭通知而唤醒
        if (_queue.empty() || _shutdown)
            return;

        value = _queue.front();

        _queue.pop();
    }

    /**
     * @brief 取消队列并清理所有元素
     *
     * 关闭队列，清理所有待处理元素，并唤醒所有等待的消费者线程。
     * 调用时机：应用程序关闭或需要终止队列处理时调用。
     *
     * 功能：
     * 1. 清空队列中所有待处理元素
     * 2. 如果元素类型是指针，自动释放内存
     * 3. 设置关闭标志，阻止后续操作
     * 4. 唤醒所有等待中的消费者线程
     *
     * @warning 调用此函数后，队列将无法再使用。仅应在程序终止或确定不再需要队列时调用。
     * @warning 如果元素类型是指针，函数会自动 delete；确保所有元素都是通过 new 分配的。
     */
    void Cancel()
    {
        std::unique_lock<std::mutex> lock(_queueLock);

        // 遍历并清理队列中的所有元素
        while (!_queue.empty())
        {
            T& value = _queue.front();

            // 如果元素类型是指针，自动释放内存
            // 使用 if constexpr 在编译期判断，避免非指针类型的开销
            if constexpr (std::is_pointer_v<T>)
                delete value;

            _queue.pop();
        }

        // 设置关闭标志，阻止后续的 Push 和 Pop 操作
        _shutdown = true;

        // 唤醒所有等待中的消费者线程，让它们检测到关闭状态并退出
        _condition.notify_all();
    }
};

#endif // TRINITY_PRODUCER_CONSUMER_QUEUE_H
