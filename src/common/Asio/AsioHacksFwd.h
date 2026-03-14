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

#ifndef AsioHacksFwd_h__
#define AsioHacksFwd_h__

#include <boost/version.hpp>

/**
 * @file AsioHacksFwd.h
 * @brief ASIO 前向声明头文件
 *
 * 本文件提供 Boost.ASIO 和 TrinityCore Asio 封装类的前向声明，
 * 主要用于减少编译依赖，提高编译速度。
 *
 * 通过前向声明，其他头文件可以避免直接包含完整的 ASIO 头文件，
 * 从而减少头文件包含链，显著缩短编译时间。
 *
 * @note 该文件应被需要在头文件中引用 ASIO 类型但不需完整定义的模块包含
 */

/**
 * @brief Boost 库命名空间
 *
 * 包含 Boost 库的各种组件，特别是 Boost.ASIO 网络库相关类型。
 */
namespace boost
{
    /**
     * @brief Boost.POSIX 时间库命名空间
     *
     * 提供时间点和时间间隔的处理能力。
     */
    namespace posix_time
    {
        /**
         * @brief POSIX 时间点类型
         *
         * 表示一个绝对时间点，用于定时器的过期时间设置。
         */
        class ptime;
    }

    /**
     * @brief Boost.ASIO 异步 I/O 库命名空间
     *
     * 提供异步网络和 I/O 操作的核心功能。
     */
    namespace asio
    {
        /**
         * @brief 时间特性模板
         *
         * @tparam Time 时间类型
         *
         * 为特定时间类型提供适配接口，使 Boost.ASIO 能够使用该时间类型。
         * 特化版本定义了如何获取当前时间、时间相加等操作。
         */
        template <typename Time>
        struct time_traits;

        /**
         * @brief IP 网络协议命名空间
         *
         * 包含 IP 地址、端点、套接字等网络相关类型。
         */
        namespace ip
        {
            /**
             * @brief IP 地址类
             *
             * 表示 IPv4 或 IPv6 地址，提供地址解析、转换等功能。
             */
            class address;

            /**
             * @brief TCP 协议类
             *
             * 表示 TCP 协议，用于创建 TCP 套接字和端点。
             */
            class tcp;

            /**
             * @brief 基本端点模板类
             *
             * @tparam InternetProtocol 网络协议类型（如 TCP、UDP）
             *
             * 表示一个网络端点，包含协议、IP 地址和端口号。
             * 是网络通信的基本单元。
             */
            template <typename InternetProtocol>
            class basic_endpoint;

            /**
             * @brief TCP 端点类型别名
             *
             * 专门用于 TCP 协议的端点类型，包含 IP 地址和端口号。
             * 常用于套接字连接和绑定操作。
             */
            typedef basic_endpoint<tcp> tcp_endpoint;
        }
    }
}

/**
 * @brief TrinityCore 命名空间
 *
 * TrinityCore 服务器项目的核心命名空间。
 */
namespace Trinity
{
    /**
     * @brief TrinityCore ASIO 封装命名空间
     *
     * 提供对 Boost.ASIO 的封装类，简化使用并提供更好的前向声明支持。
     * 主要封装类包括：
     * - IoContext: I/O 上下文管理
     * - DeadlineTimer: 截止时间定时器
     * - Resolver: DNS 解析器
     * - Strand: 顺序执行保证器
     *
     * @note 这些封装类主要用于解决 Boost.ASIO 类型的前向声明问题，
     *       避免在头文件中暴露复杂的模板类型
     */
    namespace Asio
    {
        /**
         * @brief 截止时间定时器类
         *
         * 提供定时器功能，支持在指定时间点触发异步回调。
         * 常用于实现超时控制、周期性任务等场景。
         */
        class DeadlineTimer;

        /**
         * @brief I/O 上下文类
         *
         * 封装 Boost.ASIO 的 io_context，是异步 I/O 操作的核心。
         * 管理异步操作的执行队列，提供事件循环机制。
         */
        class IoContext;

        /**
         * @brief DNS 解析器类
         *
         * 提供主机名到 IP 地址的解析功能。
         * 支持同步解析，返回第一个匹配的端点。
         */
        class Resolver;

        /**
         * @brief Strand 类
         *
         * 提供非阻塞的顺序执行保证。
         * 确保通过同一 Strand 投递的异步处理器不会并发执行。
         * 用于实现线程安全的异步操作序列化。
         */
        class Strand;
    }
}

#endif // AsioHacksFwd_h__
