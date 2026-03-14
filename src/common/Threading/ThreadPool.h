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
 * @file ThreadPool.h
 * @brief 线程池封装类
 *
 * 本模块基于 Boost.Asio 的线程池实现，提供了一个简单易用的线程池接口。
 * 主要应用于：
 * - 异步任务处理：将耗时任务提交到线程池异步执行
 * - 并发操作：利用多核 CPU 并行处理多个独立任务
 * - 资源管理：限制并发线程数量，避免线程创建销毁开销
 *
 * 特性：
 * - 基于 Boost.Asio 高性能 IO 模型
 * - 自动根据硬件并发度创建合适数量的工作线程
 * - 支持任意可调用对象（函数、lambda、函数对象）
 */

#ifndef TRINITY_THREAD_POOL_H
#define TRINITY_THREAD_POOL_H

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <thread>

namespace Trinity
{
/**
 * @class ThreadPool
 * @brief 基于 Boost.Asio 的线程池封装类
 *
 * 该类封装了 Boost.Asio 的 thread_pool，提供简洁的任务提交接口。
 * 线程池在构造时创建指定数量的工作线程，这些线程持续从任务队列中取出任务执行。
 *
 * 典型使用场景：
 * @code
 * // 创建拥有 4 个工作线程的线程池
 * Trinity::ThreadPool pool(4);
 *
 * // 提交异步任务
 * pool.PostWork([]() {
 *     // 执行耗时操作
 *     ProcessData();
 * });
 *
 * // 主线程继续其他工作...
 *
 * // 等待所有任务完成
 * pool.Join();
 * @endcode
 *
 * 性能注意事项：
 * - 线程数量建议设置为 CPU 核心数或稍多，过多线程会增加上下文切换开销
 * - 任务应尽量独立，避免频繁的线程间同步
 * - 对于 IO 密集型任务，可以适当增加线程数
 */
class ThreadPool
{
public:
    /**
     * @brief 构造函数
     *
     * 创建一个拥有指定数量工作线程的线程池。
     * 线程池一旦创建，工作线程立即开始等待任务。
     *
     * @param numThreads 工作线程数量，默认为 std::thread::hardware_concurrency()
     *                   该值通常等于 CPU 逻辑核心数，适合 CPU 密集型任务
     *
     * 性能注意事项：
     * - 对于 CPU 密集型任务，使用默认值（核心数）可获得最佳性能
     * - 对于 IO 密集型任务，可考虑增加线程数（如 2 倍核心数）
     * - 线程数过少：无法充分利用 CPU；线程数过多：增加调度开销
     */
    explicit ThreadPool(std::size_t numThreads = std::thread::hardware_concurrency()) : _impl(numThreads) { }

    /**
     * @brief 向线程池提交工作任务
     *
     * 将可调用对象（函数、lambda、函数对象等）提交到线程池的任务队列，
     * 由工作线程异步执行。提交后立即返回，不等待任务完成。
     *
     * @tparam T 可调用对象类型（自动推导）
     * @param work 要执行的工作对象（可调用对象）
     * @return 返回 boost::asio::post 的结果类型
     *
     * 调用时机：
     * - 需要异步执行耗时操作时
     * - 需要将任务分发到后台线程时
     * - 需要并行处理多个独立任务时
     *
     * 使用示例：
     * @code
     * // 提交 lambda 表达式
     * pool.PostWork([]() { DoSomething(); });
     *
     * // 提交函数对象
     * pool.PostWork(std::bind(&MyClass::Method, this));
     *
     * // 提交带捕获的 lambda
     * int data = 42;
     * pool.PostWork([data]() { ProcessData(data); });
     * @endcode
     *
     * 性能注意事项：
     * - 任务提交是非阻塞的，立即返回
     * - 如果任务队列已满（极端情况），可能会短暂阻塞
     * - 避免在任务中执行阻塞操作，会浪费工作线程
     */
    template<typename T>
    decltype(auto) PostWork(T&& work)
    {
        return boost::asio::post(_impl, std::forward<T>(work));
    }

    /**
     * @brief 等待所有任务完成并关闭线程池
     *
     * 阻塞当前线程，直到线程池中所有已提交的任务都执行完毕。
     * 调用后线程池将不再接受新任务。
     *
     * 调用时机：
     * - 程序关闭前，确保所有异步任务完成
     * - 需要同步等待一批任务全部完成时
     *
     * @warning 此函数会阻塞调用线程直到所有任务完成，请确保不会导致死锁：
     *          不要在已提交的任务中调用 Join()，否则会死锁
     *
     * 使用示例：
     * @code
     * ThreadPool pool;
     *
     * // 提交多个任务
     * for (int i = 0; i < 10; ++i)
     *     pool.PostWork([i]() { ProcessTask(i); });
     *
     * // 等待所有任务完成
     * pool.Join();
     * @endcode
     */
    void Join()
    {
        _impl.join();
    }

private:
    /**
     * @brief Boost.Asio 线程池实现
     *
     * boost::asio::thread_pool 是 Boost 库提供的线程池实现，内部管理：
     * - 固定数量的工作线程
     * - 任务队列（io_context）
     * - 任务调度机制
     *
     * 工作原理：
     * 1. 构造时创建 numThreads 个工作线程
     * 2. 工作线程从 io_context 中获取任务执行
     * 3. 通过 post() 将任务添加到 io_context
     * 4. join() 等待所有任务完成并停止工作线程
     */
    boost::asio::thread_pool _impl;
};
}

#endif // TRINITY_THREAD_POOL_H
