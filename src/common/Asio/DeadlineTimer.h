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

#ifndef DeadlineTimer_h__
#define DeadlineTimer_h__

#include <boost/asio/deadline_timer.hpp>

/**
 * @file DeadlineTimer.h
 * @brief 截止时间定时器封装
 *
 * 本文件提供对 Boost.ASIO deadline_timer 的封装，简化定时器的使用。
 * 定时器支持绝对时间点和相对时间间隔的设置，广泛用于：
 * - 网络连接超时控制
 * - 周期性任务调度
 * - 异步操作的限时等待
 *
 * @note 该封装类主要用于解决 Boost.ASIO deadline_timer 的前向声明问题，
 *       避免在头文件中暴露复杂的模板参数
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
         * @brief 截止时间定时器类
         *
         * 继承自 Boost.ASIO 的 basic_deadline_timer，提供基于 POSIX 时间的定时器功能。
         * 该定时器使用绝对时间点（boost::posix_time::ptime）进行定时。
         *
         * 主要功能：
         * - 在指定时间点触发异步回调
         * - 支持同步等待和异步等待
         * - 支持定时器的取消和重置
         *
         * 典型使用场景：
         * @code
         * IoContext ioContext;
         * DeadlineTimer timer(ioContext);
         *
         * // 设置 5 秒后过期
         * timer.expires_from_now(boost::posix_time::seconds(5));
         *
         * // 异步等待
         * timer.async_wait([](boost::system::error_code ec) {
         *     if (!ec) {
         *         // 定时器正常触发
         *     }
         * });
         *
         * ioContext.run();
         * @endcode
         *
         * @note 使用绝对时间（expires_at）或相对时间（expires_from_now）设置过期时间
         * @warning 定时器回调可能被取消（ec == boost::asio::error::operation_aborted）
         *
         * 性能注意事项：
         * - 定时器精度取决于操作系统的定时器分辨率
         * - 大量定时器会增加事件循环的开销
         * - 尽量避免在定时器回调中执行耗时操作
         */
        class DeadlineTimer : public boost::asio::basic_deadline_timer<boost::posix_time::ptime, boost::asio::time_traits<boost::posix_time::ptime>, boost::asio::io_context::executor_type>
        {
        public:
            /**
             * @brief 继承基类的所有构造函数
             *
             * 使用 using 声明引入基类的构造函数，支持以下构造方式：
             *
             * 1. 从 IoContext 构造：
             *    @code
             *    IoContext ioContext;
             *    DeadlineTimer timer(ioContext);
             *    @endcode
             *
             * 2. 从 IoContext 和绝对时间构造：
             *    @code
             *    DeadlineTimer timer(ioContext, boost::posix_time::ptime(...));
             *    @endcode
             *
             * 3. 从 IoContext 和相对时间构造：
             *    @code
             *    DeadlineTimer timer(ioContext, boost::posix_time::seconds(5));
             *    @endcode
             *
             * @param args 构造参数，传递给基类构造函数
             *
             * @note 构造后的定时器需要调用 expires_at 或 expires_from_now 设置过期时间
             */
            using basic_deadline_timer::basic_deadline_timer;
        };
    }
}

#endif // DeadlineTimer_h__
