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

#ifndef Strand_h__
#define Strand_h__

#include "IoContext.h"
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>

/**
 * @file Strand.h
 * @brief Strand 顺序执行保证器封装
 *
 * 本文件提供对 Boost.ASIO Strand 的封装，用于实现异步操作的顺序执行。
 * Strand 是 Boost.ASIO 中实现并发安全的重要工具。
 *
 * Strand 的核心概念：
 * - 确保通过同一 Strand 投递的异步处理器不会并发执行
 * - 提供非阻塞的顺序执行保证
 * - 允许多个异步操作安全地共享资源
 *
 * 主要用途：
 * - 保护共享资源免受并发访问
 * - 确保回调函数按顺序执行
 * - 实现线程安全的异步模式
 *
 * @note Strand 不使用互斥锁，而是通过任务队列实现顺序性，性能更好
 */

/**
 * @brief TrinityCore 命名空间
 */
namespace Trinity
{
    /**
     * @brief ASIO 封装命名空间
     */
    namespace Asio
    {
        /**
         * @brief Strand 顺序执行保证器类
         *
         * 继承自 Boost.ASIO 的 io_context::strand，提供异步操作的顺序执行保证。
         *
         * Strand 的核心特性：
         * 1. 顺序性：通过同一 Strand 投递的异步操作会按顺序执行
         * 2. 非阻塞：Strand 本身不阻塞线程，只是保证顺序性
         * 3. 跨线程安全：可以从多个线程向同一 Strand 投递任务
         *
         * 典型使用场景：
         * - 多个异步操作访问共享数据时
         * - 需要保证操作顺序的场景
         * - 网络连接的读写操作序列化
         *
         * 典型用法：
         * @code
         * IoContext ioContext;
         * Strand strand(ioContext);
         *
         * // 从任意线程投递任务到 Strand
         * // 这些任务会按投递顺序执行，不会并发
         * strand.post([]() {
         *     // 任务 1 - 访问共享资源
         * });
         *
         * strand.post([]() {
         *     // 任务 2 - 保证在任务 1 完成后执行
         * });
         *
         * // 启动多线程事件循环
         * std::thread t1([&ioContext]() { ioContext.run(); });
         * std::thread t2([&ioContext]() { ioContext.run(); });
         *
         * t1.join();
         * t2.join();
         * @endcode
         *
         * 与互斥锁的比较：
         * - Strand：非阻塞，通过任务调度实现顺序性，性能更好
         * - 互斥锁：阻塞线程，可能导致死锁，性能较差
         *
         * Strand 的优势：
         * - 不会死锁
         * - 不会阻塞线程
         * - 可以在异步回调中使用
         * - 性能优于互斥锁
         *
         * 性能注意事项：
         * - Strand 对象本身很轻量
         * - 使用 Strand 的异步操作有轻微开销
         * - 但比使用互斥锁的性能更好
         * - 避免过度使用，仅在必要时使用
         *
         * 常见使用模式：
         * 1. 保护共享数据：
         *    @code
         *    class Connection : public std::enable_shared_from_this<Connection> {
         *        Strand _strand;
         *        std::vector<int> _sharedData;
         *    public:
         *        void AddData(int value) {
         *            post(_strand, [self = shared_from_this(), value]() {
         *                self->_sharedData.push_back(value);  // 线程安全
         *            });
         *        }
         *    };
         *    @endcode
         *
         * 2. 网络连接序列化：
         *    @code
         *    class AsyncClient {
         *        Strand _strand;
         *        tcp::socket _socket;
         *    public:
         *        void SendMessage(const std::string& msg) {
         *            // 所有写操作通过 Strand 保证顺序
         *            async_write(_socket, buffer(msg),
         *                bind_executor(_strand, [](error_code ec, size_t bytes) {
         *                    // 处理结果
         *                }));
         *        }
         *    };
         *    @endcode
         *
         * @warning Strand 只保证通过它投递的操作顺序性，
         *          不保护直接访问共享资源
         * @warning 必须通过 Strand 的 post/dispatch 或 bind_executor 投递任务
         *
         * @see IoContext, bind_executor
         */
        class Strand : public boost::asio::io_context::strand
        {
        public:
            /**
             * @brief 构造函数
             *
             * @param ioContext I/O 上下文引用
             *
             * 创建一个 Strand 对象，关联到指定的 I/O 上下文。
             * Strand 需要与 IoContext 关联，以便投递和执行任务。
             *
             * 调用时机：
             * - 在需要保护共享资源的类初始化时
             * - 创建网络连接对象时
             * - 需要顺序执行异步操作时
             *
             * 典型用法：
             * @code
             * IoContext ioContext;
             * Strand strand(ioContext);
             *
             * // 使用 Strand 投递任务
             * strand.post([]() {
             *     // 该任务保证顺序执行
             * });
             * @endcode
             *
             * @note Strand 对象的生命周期应与 IoContext 同步
             * @note Strand 通常作为类的成员变量，保护该类的共享资源
             */
            Strand(IoContext& ioContext) : boost::asio::io_context::strand(ioContext) { }
        };

        /**
         * @brief 绑定执行器工具函数
         *
         * 引入 Boost.ASIO 的 bind_executor 函数，用于将异步操作绑定到指定执行器。
         *
         * 主要用途：
         * - 将异步回调绑定到 Strand，保证顺序执行
         * - 将异步操作绑定到特定的执行上下文
         *
         * 典型用法：
         * @code
         * Strand strand(ioContext);
         * tcp::socket socket(ioContext);
         *
         * // 绑定异步读操作的回调到 Strand
         * socket.async_read_some(buffer(data, size),
         *     bind_executor(strand, [](error_code ec, size_t bytes_transferred) {
         *         // 该回调通过 Strand 执行，保证顺序性
         *     }));
         *
         * // 绑定异步写操作到 Strand
         * async_write(socket, buffer(data, size),
         *     bind_executor(strand, [](error_code ec, size_t bytes_transferred) {
         *         // 该回调也通过 Strand 执行，保证与读操作的顺序性
         *     }));
         * @endcode
         *
         * 工作原理：
         * bind_executor 返回一个包装的完成处理器，
         * 该处理器会通过指定的执行器（如 Strand）调用。
         * 这样可以确保回调在 Strand 的上下文中执行，
         * 从而保证顺序性。
         *
         * 调用时机：
         * - 异步操作的回调需要与其他操作顺序执行时
         * - 需要将回调转移到特定执行上下文时
         * - 保护共享资源免受并发访问时
         *
         * 使用场景示例：
         * 1. 网络连接的读写序列化：
         *    @code
         *    class Session {
         *        Strand _strand;
         *        tcp::socket _socket;
         *        void DoRead() {
         *            async_read(_socket, buffer(_buffer),
         *                bind_executor(_strand, [this](error_code ec, size_t bytes) {
         *                    if (!ec) {
         *                        ProcessData(_buffer, bytes);
         *                        DoRead();  // 继续读取
         *                    }
         *                }));
         *        }
         *        void DoWrite(const std::string& msg) {
         *            async_write(_socket, buffer(msg),
         *                bind_executor(_strand, [this](error_code ec, size_t bytes) {
         *                    if (!ec) {
         *                        // 写完成后的处理
         *                    }
         *                }));
         *        }
         *    };
         *    @endcode
         *
         * 2. 定时器回调序列化：
         *    @code
         *    Strand strand(ioContext);
         *    DeadlineTimer timer(ioContext);
         *
         *    timer.async_wait(bind_executor(strand, [](error_code ec) {
         *        // 定时器回调通过 Strand 执行
         *        // 保证与其他 Strand 操作的顺序性
         *    }));
         *    @endcode
         *
         * 性能注意事项：
         * - bind_executor 是轻量级操作，性能开销很小
         * - 使用它比使用互斥锁性能更好
         * - 避免在 Strand 回调中执行阻塞操作
         *
         * @note 该函数是 Boost.ASIO 原生函数的类型别名
         * @warning 绑定到 Strand 的回调会按顺序执行，可能增加延迟
         *
         * @see Strand, IoContext
         */
        using boost::asio::bind_executor;
    }
}

#endif // Strand_h__
