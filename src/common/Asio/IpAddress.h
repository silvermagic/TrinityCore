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

#ifndef IpAddress_h__
#define IpAddress_h__

#include "Define.h"
#include <boost/asio/ip/address.hpp>

/**
 * @file IpAddress.h
 * @brief IP 地址处理工具
 *
 * 本文件提供 IP 地址相关的工具函数和类型别名，简化 Boost.ASIO IP 地址的使用。
 * 主要功能包括：
 * - IP 地址创建和转换
 * - IPv4 地址与整数转换
 *
 * 这些工具函数封装了 Boost.ASIO 的 IP 地址操作，提供更简洁的接口。
 *
 * @note 该文件主要用于网络模块，处理客户端和服务器之间的 IP 地址
 */

/**
 * @brief TrinityCore 命名空间
 */
namespace Trinity
{
    /**
     * @brief 网络工具命名空间
     *
     * 提供网络相关的工具类和函数，包括：
     * - IP 地址处理
     * - 网络掩码计算
     * - IP 地址范围检查
     */
    namespace Net
    {
        /**
         * @brief 创建 IP 地址对象
         *
         * 引入 Boost.ASIO 的 make_address 函数，用于从字符串创建 IP 地址。
         * 支持自动识别 IPv4 和 IPv6 地址格式。
         *
         * 使用示例：
         * @code
         * // 创建 IPv4 地址
         * auto addr1 = make_address("192.168.1.1");
         *
         * // 创建 IPv6 地址
         * auto addr2 = make_address("::1");
         *
         * // 错误处理
         * boost::system::error_code ec;
         * auto addr3 = make_address("invalid", ec);
         * if (ec) {
         *     // 地址解析失败
         * }
         * @endcode
         *
         * @note 该函数是 Boost.ASIO 原生函数的类型别名
         * @see make_address_v4
         */
        using boost::asio::ip::make_address;

        /**
         * @brief 创建 IPv4 地址对象
         *
         * 引入 Boost.ASIO 的 make_address_v4 函数，专门用于创建 IPv4 地址。
         * 提供更严格的类型保证，确保创建的是 IPv4 地址。
         *
         * 使用示例：
         * @code
         * // 从字符串创建
         * auto addr1 = make_address_v4("192.168.1.1");
         *
         * // 从整数创建
         * auto addr2 = make_address_v4(0x7F000001);  // 127.0.0.1
         * @endcode
         *
         * @note 专门用于 IPv4 地址，如果输入是 IPv6 地址会抛出异常
         * @see make_address, address_to_uint
         */
        using boost::asio::ip::make_address_v4;

        /**
         * @brief 将 IPv4 地址转换为无符号整数
         *
         * @param address IPv4 地址对象
         * @return IPv4 地址的 32 位无符号整数表示（网络字节序）
         *
         * 将 boost::asio::ip::address_v4 对象转换为 uint32 类型。
         * 转换后的整数可用于：
         * - IP 地址范围比较
         * - 网络掩码计算
         * - 数据库存储
         * - 高效的 IP 地址匹配
         *
         * 调用时机：
         * - 需要进行 IP 地址数学运算时
         * - 判断 IP 是否属于某个网段时
         * - 存储 IP 地址到数据库时
         *
         * 典型用法：
         * @code
         * auto addr = make_address_v4("192.168.1.1");
         * uint32 ipValue = address_to_uint(addr);
         *
         * // 检查是否是私有地址（192.168.0.0/16）
         * if ((ipValue & 0xFFFF0000) == 0xC0A80000) {
         *     // 是 192.168.x.x 私有地址
         * }
         * @endcode
         *
         * 性能注意事项：
         * - 转换操作非常快，O(1) 时间复杂度
         * - 适合频繁调用
         *
         * @note 返回的是网络字节序（大端序）的整数表示
         * @warning 仅适用于 IPv4 地址，IPv6 地址请使用其他方法
         *
         * @see make_address_v4, GetDefaultNetmaskV4
         */
        inline uint32 address_to_uint(boost::asio::ip::address_v4 const& address) { return address.to_uint(); }
    }
}

#endif // IpAddress_h__
