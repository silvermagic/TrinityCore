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

#ifndef Resolver_h__
#define Resolver_h__

#include "IoContext.h"
#include "Optional.h"
#include <boost/asio/ip/tcp.hpp>
#include <string>

/**
 * @file Resolver.h
 * @brief DNS 解析器封装
 *
 * 本文件提供对 Boost.ASIO TCP 解析器的封装，简化 DNS 解析操作。
 * 主要功能包括：
 * - 将主机名解析为 IP 地址
 * - 将服务名解析为端口号
 * - 支持同步解析
 *
 * 该封装解决了 Boost.ASIO 解析器类型的前向声明问题，
 * 隐藏了复杂的模板参数，提供简洁的接口。
 *
 * @note 解析操作可能阻塞，建议在工作线程中调用
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
         * @brief DNS 解析器类
         *
         * 封装 Boost.ASIO 的 TCP 解析器，提供简化的 DNS 解析接口。
         *
         * 主要用途：
         * - 将主机名（如 "example.com"）解析为 IP 地址
         * - 将服务名（如 "http"）解析为端口号
         * - 在客户端连接建立前解析服务器地址
         *
         * 典型使用场景：
         * @code
         * IoContext ioContext;
         * Resolver resolver(ioContext);
         *
         * // 解析主机和端口
         * auto endpoint = resolver.Resolve(
         *     boost::asio::ip::tcp::v4(),
         *     "example.com",
         *     "80"
         * );
         *
         * if (endpoint) {
         *     // 解析成功，可以使用 endpoint 连接
         *     boost::asio::ip::tcp::socket socket(ioContext);
         *     socket.connect(*endpoint);
         * }
         * @endcode
         *
         * 性能注意事项：
         * - Resolve 方法是同步阻塞的
         * - DNS 解析可能耗时数秒
         * - 建议在工作线程或独立线程中调用
         * - 可以考虑缓存解析结果
         *
         * @note 该类仅返回第一个匹配的端点，适用于单地址场景
         * @warning 如果 DNS 解析失败或网络不可达，返回空的 Optional
         *
         * @see IoContext, Optional
         */
        class Resolver
        {
        public:
            /**
             * @brief 构造函数
             *
             * @param ioContext I/O 上下文引用
             *
             * 创建一个 DNS 解析器实例，关联到指定的 I/O 上下文。
             * 解析器需要 IoContext 来执行底层的 DNS 查询操作。
             *
             * 调用时机：
             * - 在需要解析主机名的模块初始化时
             * - 创建网络客户端连接前
             *
             * 典型用法：
             * @code
             * IoContext ioContext;
             * Resolver resolver(ioContext);
             * @endcode
             *
             * @note 解析器对象的生命周期应与 IoContext 同步
             */
            explicit Resolver(IoContext& ioContext) : _impl(ioContext) { }

            /**
             * @brief 同步解析主机名和服务名
             *
             * @param protocol TCP 协议版本（IPv4 或 IPv6）
             * @param host 主机名（如 "example.com" 或 IP 地址字符串）
             * @param service 服务名（如 "http"）或端口号字符串（如 "8080"）
             * @return 解析成功返回端点（IP 地址 + 端口），失败返回空 Optional
             *
             * 执行同步 DNS 解析，将主机名和服务名转换为 TCP 端点。
             * 仅返回第一个匹配的端点，适用于单地址连接场景。
             *
             * 调用时机：
             * - 客户端连接服务器前，需要解析服务器地址
             * - 配置文件中使用主机名而非 IP 地址
             * - 需要将服务名转换为端口号
             *
             * 典型用法：
             * @code
             * Resolver resolver(ioContext);
             *
             * // 使用 IPv4 解析
             * auto endpoint = resolver.Resolve(
             *     boost::asio::ip::tcp::v4(),
             *     "login.example.com",
             *     "8080"
             * );
             *
             * if (endpoint) {
             *     // 解析成功
             *     std::cout << "Resolved to: " << endpoint->address() << ":" << endpoint->port() << std::endl;
             * } else {
             *     // 解析失败
             *     std::cerr << "DNS resolution failed" << std::endl;
             * }
             *
             * // 使用 IPv6 解析
             * auto endpoint6 = resolver.Resolve(
             *     boost::asio::ip::tcp::v6(),
             *     "ipv6.example.com",
             *     "https"
             * );
             * @endcode
             *
             * 解析流程：
             * 1. 调用底层 resolver 进行 DNS 查询
             * 2. 获取所有匹配的结果列表
             * 3. 检查是否有有效结果
             * 4. 返回第一个结果的端点
             *
             * 常见错误情况：
             * - 主机名不存在：返回空 Optional
             * - 网络不可达：返回空 Optional
             * - DNS 服务器无响应：返回空 Optional
             * - 服务名未知：返回空 Optional
             *
             * 性能注意事项：
             * - 此方法是同步阻塞的，会阻塞当前线程
             * - DNS 解析时间取决于网络状况，可能需要数秒
             * - 建议在工作线程中调用，避免阻塞主线程
             * - 可以缓存解析结果以避免重复解析
             * - 如果解析失败，考虑实现重试机制
             *
             * 协议参数说明：
             * - boost::asio::ip::tcp::v4(): 仅解析 IPv4 地址
             * - boost::asio::ip::tcp::v6(): 仅解析 IPv6 地址
             *
             * 服务名说明：
             * - 可以是端口号字符串："80", "8080", "3443"
             * - 可以是服务名："http", "https", "ssh"（需要系统支持）
             *
             * @note 使用 all_matching 标志，返回所有匹配地址中的第一个
             * @note 如果传入 IP 地址字符串，会直接转换而不进行 DNS 查询
             *
             * @see IoContext, Optional
             */
            Optional<boost::asio::ip::tcp::endpoint> Resolve(boost::asio::ip::tcp const& protocol, std::string const& host, std::string const& service)
            {
                boost::system::error_code ec;

                // 设置解析标志为 all_matching，获取所有匹配的地址
                boost::asio::ip::resolver_base::flags flagsResolver = boost::asio::ip::resolver_base::all_matching;

                // 执行同步 DNS 解析
                // 返回结果列表（可能包含多个 IP 地址）
                boost::asio::ip::tcp::resolver::results_type results = _impl.resolve(protocol, host, service, flagsResolver, ec);

                // 检查是否解析成功且有结果
                if (results.begin() == results.end() || ec)
                {
                    // 解析失败或没有结果，返回空 Optional
                    return {};
                }

                // 返回第一个结果的端点
                return results.begin()->endpoint();
            }

        private:
            /**
             * @brief 内部 Boost.ASIO TCP 解析器实现
             *
             * 封装实际的 Boost.ASIO tcp::resolver 对象。
             * 所有解析操作都委托给该实现对象。
             *
             * @note 使用 Pimpl 模式，隐藏 Boost.ASIO 的实现细节
             */
            boost::asio::ip::tcp::resolver _impl;
        };
    }
}

#endif // Resolver_h__
