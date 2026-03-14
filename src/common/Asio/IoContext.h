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

#ifndef IoContext_h__
#define IoContext_h__

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

/**
 * @file IoContext.h
 * @brief I/O 上下文封装
 *
 * 本文件提供对 Boost.ASIO io_context 的封装，它是异步 I/O 操作的核心组件。
 * IoContext 管理异步操作的执行队列，提供事件循环机制。
 *
 * 主要功能：
 * - 管理异步 I/O 对象（套接字、定时器等）
 * - 提供事件循环，执行异步回调
 * - 支持向事件队列投递任务
 *
 * 典型使用场景：
 * - 网络服务器的 I/O 管理核心
 * - 异步任务调度
 * - 定时器管理
 *
 * @note 每个网络服务通常只需要一个 IoContext 实例
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
         * @brief I/O 上下文类
         *
         * 封装 Boost.ASIO 的 io_context，是异步 I/O 框架的核心。
         * 所有异步 I/O 对象（套接字、定时器等）都需要关联到一个 IoContext。
         *
         * IoContext 维护一个任务队列，通过 run() 方法启动事件循环，
         * 不断从队列中取出任务并执行。当队列为空时，run() 方法返回。
         *
         * 典型使用模式：
         * @code
         * // 创建 I/O 上下文
         * IoContext ioContext;
         *
         * // 创建异步对象（如套接字、定时器）
         * DeadlineTimer timer(ioContext);
         * timer.expires_from_now(boost::posix_time::seconds(1));
         * timer.async_wait([](boost::system::error_code ec) {
         *     // 异步回调处理
         * });
         *
         * // 运行事件循环（阻塞，直到所有异步操作完成）
         * ioContext.run();
         * @endcode
         *
         * 多线程使用模式：
         * @code
         * IoContext ioContext(4);  // 并发提示：建议使用 4 个线程
         *
         * // 启动多个工作线程
         * std::vector<std::thread> threads;
         * for (int i = 0; i < 4; ++i) {
         *     threads.emplace_back([&ioContext]() {
         *         ioContext.run();
         *     });
         * }
         *
         * // 等待所有线程完成
         * for (auto& thread : threads) {
         *     thread.join();
         * }
         * @endcode
         *
         * 性能注意事项：
         * - concurrency_hint 参数可以优化内部锁竞争
         * - 避免在回调中执行阻塞操作
         * - 对于 CPU 密集型任务，使用 post 投递到线程池
         *
         * @warning 在调用 run() 之前，必须先创建异步操作，否则 run() 会立即返回
         * @warning IoContext 对象必须比所有关联的异步对象存活更久
         */
        class IoContext
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 创建一个默认的 I/O 上下文实例。
             * 默认情况下不设置并发提示，适用于单线程场景。
             *
             * 调用时机：
             * - 创建网络服务器的 I/O 核心时
             * - 需要异步操作的模块初始化时
             *
             * @note 适用于单线程事件循环
             */
            IoContext() : _impl() { }

            /**
             * @brief 带并发提示的构造函数
             *
             * @param concurrency_hint 并发提示，建议的并发线程数
             *
             * 创建一个带有并发提示的 I/O 上下文实例。
             * 并发提示用于优化 io_context 的内部实现，
             * 可以减少多线程环境下的锁竞争。
             *
             * 典型值：
             * - 1: 单线程模式（默认行为）
             * - N: 建议使用 N 个线程运行事件循环
             *
             * 调用时机：
             * - 多线程网络服务器初始化时
             * - 需要优化多线程性能时
             *
             * 性能注意事项：
             * - 设置合理的并发提示可以减少锁竞争
             * - 通常设置为 CPU 核心数或预期的工作线程数
             *
             * @note 并发提示不是强制限制，只是实现提示
             */
            explicit IoContext(int concurrency_hint) : _impl(concurrency_hint) { }

            /**
             * @brief 转换为 boost::asio::io_context 引用（非 const 版本）
             *
             * @return 内部 io_context 实现的引用
             *
             * 提供对内部 Boost.ASIO io_context 对象的访问。
             * 用于与需要 boost::asio::io_context& 参数的 Boost.ASIO API 兼容。
             *
             * 调用时机：
             * - 需要直接使用 Boost.ASIO API 时
             * - 创建 Boost.ASIO 原生对象时
             *
             * @code
             * IoContext ioContext;
             * boost::asio::ip::tcp::socket socket(ioContext);  // 隐式转换
             * @endcode
             *
             * @note 允许隐式转换以简化与 Boost.ASIO 的互操作
             */
            operator boost::asio::io_context&() { return _impl; }

            /**
             * @brief 转换为 boost::asio::io_context 引用（const 版本）
             *
             * @return 内部 io_context 实现的 const 引用
             *
             * 提供 const 访问，用于只读操作场景。
             *
             * @note 允许隐式转换以简化与 Boost.ASIO 的互操作
             */
            operator boost::asio::io_context const&() const { return _impl; }

            /**
             * @brief 运行事件循环
             *
             * @return 已完成的异步操作数量
             *
             * 启动事件循环，阻塞执行异步操作，直到：
             * - 所有异步操作完成
             * - 调用了 stop() 方法
             *
             * 调用时机：
             * - 服务器启动时，在主线程或工作线程中调用
             * - 需要处理异步操作时
             *
             * 典型用法：
             * @code
             * IoContext ioContext;
             * // ... 创建异步操作 ...
             * ioContext.run();  // 阻塞，直到所有操作完成
             * @endcode
             *
             * 性能注意事项：
             * - run() 方法是阻塞的，通常在工作线程中调用
             * - 可以在多个线程中同时调用 run()，实现多线程事件循环
             * - 返回后可以调用 restart() 并再次运行
             *
             * @warning 如果没有异步操作，run() 会立即返回 0
             * @see stop(), restart()
             */
            std::size_t run() { return _impl.run(); }

            /**
             * @brief 停止事件循环
             *
             * 请求事件循环停止。正在执行的异步回调会继续完成，
             * 但待处理的异步回调将被取消。
             *
             * 调用时机：
             * - 服务器关闭时
             * - 需要提前终止事件循环时
             *
             * 典型用法：
             * @code
             * // 工作线程
             * std::thread worker([&ioContext]() {
             *     ioContext.run();
             * });
             *
             * // 主线程
             * // ... 服务器运行 ...
             * ioContext.stop();  // 停止事件循环
             * worker.join();     // 等待工作线程结束
             * @endcode
             *
             * @note stop() 是非阻塞的，只是发出停止请求
             * @warning 调用 stop() 后，可以调用 restart() 重新启动
             * @see run(), restart()
             */
            void stop() { _impl.stop(); }

            /**
             * @brief 获取执行器
             *
             * @return io_context 的执行器对象
             *
             * 获取与该 IoContext 关联的执行器。
             * 执行器用于控制异步操作的执行方式，
             * 是 Boost.ASIO 执行器模型的核心组件。
             *
             * 调用时机：
             * - 创建需要执行器的对象时
             * - 使用 Strand 等执行器包装器时
             * - 投递异步任务时
             *
             * 典型用法：
             * @code
             * auto executor = ioContext.get_executor();
             * boost::asio::post(executor, []() {
             *     // 在 IoContext 的线程中执行
             * });
             * @endcode
             *
             * @note 执行器对象可以复制，用于跨线程投递任务
             * @see post()
             */
            boost::asio::io_context::executor_type get_executor() noexcept { return _impl.get_executor(); }

        private:
            /**
             * @brief 内部 Boost.ASIO io_context 实现
             *
             * 封装实际的 Boost.ASIO io_context 对象。
             * 所有操作都委托给该实现对象。
             *
             * @note 使用 Pimpl 模式，隐藏 Boost.ASIO 的实现细节
             */
            boost::asio::io_context _impl;
        };

        /**
         * @brief 向 IoContext 投递异步任务
         *
         * @tparam T 可调用对象类型
         * @param ioContext 目标 I/O 上下文
         * @param t 可调用对象（函数、lambda、函数对象等）
         * @return 返回 boost::asio::post 的结果
         *
         * 将任务投递到 IoContext 的任务队列中，异步执行。
         * 任务会在 IoContext 的事件循环中被执行。
         *
         * 调用时机：
         * - 需要在 IoContext 线程中执行任务时
         * - 跨线程安全地投递任务时
         * - 实现异步操作时
         *
         * 典型用法：
         * @code
         * IoContext ioContext;
         *
         * // 从任意线程投递任务
         * post(ioContext, []() {
         *     // 该代码会在 IoContext 的线程中执行
         *     // 可以安全地访问 IoContext 管理的资源
         * });
         *
         * // 启动事件循环
         * ioContext.run();
         * @endcode
         *
         * 性能注意事项：
         * - post 是线程安全的，可以从任意线程调用
         * - 任务会被复制到任务队列，注意性能开销
         * - 避免投递过重的任务，会阻塞事件循环
         *
         * @note 使用完美转发，保持参数的值类别
         * @see IoContext::get_executor()
         */
        template<typename T>
        inline decltype(auto) post(boost::asio::io_context& ioContext, T&& t)
        {
            return boost::asio::post(ioContext, std::forward<T>(t));
        }

        /**
         * @brief 从 I/O 对象获取关联的 IoContext
         *
         * @tparam T I/O 对象类型（如套接字、定时器等）
         * @param ioObject I/O 对象引用
         * @return 关联的 io_context 引用
         *
         * 从任意 Boost.ASIO I/O 对象（如套接字、定时器）中
         * 提取其关联的 io_context 对象。
         *
         * 调用时机：
         * - 需要访问套接字或定时器的 IoContext 时
         * - 在回调函数中需要投递新任务时
         *
         * 典型用法：
         * @code
         * void OnAccept(boost::asio::ip::tcp::socket socket) {
         *     // 获取套接字关联的 IoContext
         *     auto& ioContext = get_io_context(socket);
         *
         *     // 使用 IoContext 投递新任务
         *     post(ioContext, []() {
         *         // 处理逻辑
         *     });
         * }
         * @endcode
         *
         * @note 使用模板，适用于所有具有 get_executor() 方法的 I/O 对象
         * @warning 返回的 io_context 引用必须在 I/O 对象存活期间有效
         */
        template<typename T>
        inline decltype(auto) get_io_context(T&& ioObject)
        {
            return ioObject.get_executor().context();
        }
    }
}

#endif // IoContext_h__
