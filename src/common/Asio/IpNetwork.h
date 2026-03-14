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

#ifndef IpNetwork_h__
#define IpNetwork_h__

#include "Define.h"
#include "IpAddress.h"
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>

/**
 * @file IpNetwork.h
 * @brief IP 网络工具函数
 *
 * 本文件提供 IP 网络相关的工具函数，用于网络地址范围检查和计算。
 * 主要功能包括：
 * - 检查 IP 地址是否属于指定网段
 * - 计算默认子网掩码
 * - 支持 IPv4 和 IPv6
 *
 * 这些函数常用于：
 * - 访问控制列表（ACL）实现
 * - IP 白名单/黑名单检查
 * - 网络地址分类
 *
 * @note 网络地址和掩码的计算遵循标准网络协议规范
 */

/**
 * @brief TrinityCore 命名空间
 */
namespace Trinity
{
    /**
     * @brief 网络工具命名空间
     */
    namespace Net
    {
        /**
         * @brief 检查 IPv4 地址是否属于指定网段
         *
         * @param networkAddress 网络地址（网段起始地址）
         * @param mask 子网掩码
         * @param clientAddress 待检查的客户端 IP 地址
         * @return true 如果客户端地址属于该网段，否则返回 false
         *
         * 通过子网掩码判断一个 IPv4 地址是否属于指定的网络地址范围。
         * 这是网络访问控制的基础功能，常用于：
         * - IP 白名单检查
         * - 网络访问权限控制
         * - 私有地址识别
         *
         * 调用时机：
         * - 客户端连接时检查是否允许访问
         * - 实现 IP 地址过滤功能
         * - 判断客户端是否来自内网
         *
         * 典型用法：
         * @code
         * auto networkAddr = make_address_v4("192.168.1.0");
         * auto mask = make_address_v4("255.255.255.0");
         * auto clientAddr = make_address_v4("192.168.1.100");
         *
         * if (IsInNetwork(networkAddr, mask, clientAddr)) {
         *     // 客户端在 192.168.1.0/24 网段内
         *     // 允许访问
         * }
         * @endcode
         *
         * 算法说明：
         * 1. 使用网络地址和掩码创建 network_v4 对象
         * 2. 获取该网络的所有主机地址范围
         * 3. 检查客户端地址是否在范围内
         *
         * 性能注意事项：
         * - 该函数创建临时网络对象，有一定开销
         * - 如果需要频繁检查同一网段，考虑缓存 network_v4 对象
         * - 时间复杂度：O(1) 查找，但包含对象构造开销
         *
         * @note 网络地址应该是子网的网络地址（非主机地址）
         * @warning 传入无效的网络地址或掩码可能导致意外结果
         *
         * @see GetDefaultNetmaskV4, IsInNetwork(IPv6版本)
         */
        inline bool IsInNetwork(boost::asio::ip::address_v4 const& networkAddress, boost::asio::ip::address_v4 const& mask, boost::asio::ip::address_v4 const& clientAddress)
        {
            // 创建网络对象，包含网络地址和掩码
            boost::asio::ip::network_v4 network = boost::asio::ip::make_network_v4(networkAddress, mask);

            // 获取网络中所有主机地址的范围
            boost::asio::ip::address_v4_range hosts = network.hosts();

            // 检查客户端地址是否在主机地址范围内
            return hosts.find(clientAddress) != hosts.end();
        }

        /**
         * @brief 获取 IPv4 地址的默认子网掩码
         *
         * @param networkAddress IPv4 网络地址
         * @return 对应的默认子网掩码
         *
         * 根据 IPv4 地址分类（A/B/C/D/E 类）返回默认子网掩码。
         * 这是传统的 IP 地址分类方法，虽然现代网络使用 CIDR，
         * 但此函数仍可用于兼容旧系统或快速分类。
         *
         * IP 地址分类及默认掩码：
         * - A 类地址 (0.0.0.0 - 127.255.255.255): 255.0.0.0
         * - B 类地址 (128.0.0.0 - 191.255.255.255): 255.255.0.0
         * - C 类地址 (192.0.0.0 - 223.255.255.255): 255.255.255.0
         * - D 类地址 (组播，224.0.0.0 - 239.255.255.255): 255.255.255.255
         * - E 类地址 (保留，240.0.0.0 - 255.255.255.255): 255.255.255.255
         *
         * 调用时机：
         * - 需要快速判断 IP 地址类型时
         * - 配置网络参数时
         * - 实现简化的网络分类逻辑
         *
         * 典型用法：
         * @code
         * auto addr = make_address_v4("192.168.1.1");
         * auto mask = GetDefaultNetmaskV4(addr);
         * // mask 为 255.255.255.0（C 类地址默认掩码）
         *
         * auto addr2 = make_address_v4("10.0.0.1");
         * auto mask2 = GetDefaultNetmaskV4(addr2);
         * // mask2 为 255.0.0.0（A 类地址默认掩码）
         * @endcode
         *
         * 算法说明：
         * 通过检查 IP 地址的高位比特判断地址类别：
         * 1. 最高位为 0 -> A 类（掩码 255.0.0.0，即 0xFF000000）
         * 2. 最高两位为 10 -> B 类（掩码 255.255.0.0，即 0xFFFF0000）
         * 3. 最高三位为 110 -> C 类（掩码 255.255.255.0，即 0xFFFFFF00）
         * 4. 其他 -> D/E 类（掩码 255.255.255.255，即 0xFFFFFFFF）
         *
         * 性能注意事项：
         * - 仅使用位运算，非常快速
         * - 时间复杂度：O(1)
         * - 适合频繁调用
         *
         * @note 现代网络通常使用 CIDR（无类别域间路由），
         *       默认掩码可能不适用于所有场景
         * @warning D 类和 E 类地址返回的掩码 255.255.255.255 可能无实际意义
         *
         * @see IsInNetwork, address_to_uint
         */
        inline boost::asio::ip::address_v4 GetDefaultNetmaskV4(boost::asio::ip::address_v4 const& networkAddress)
        {
            // 获取 IP 地址的整数表示
            uint32 addrValue = address_to_uint(networkAddress);

            // 检查最高位，判断地址类别
            if ((addrValue & 0x80000000) == 0)
            {
                // 最高位为 0，A 类地址
                // 默认掩码：255.0.0.0
                return boost::asio::ip::address_v4(0xFF000000);
            }

            // 检查最高两位
            if ((addrValue & 0xC0000000) == 0x80000000)
            {
                // 最高两位为 10，B 类地址
                // 默认掩码：255.255.0.0
                return boost::asio::ip::address_v4(0xFFFF0000);
            }

            // 检查最高三位
            if ((addrValue & 0xE0000000) == 0xC0000000)
            {
                // 最高三位为 110，C 类地址
                // 默认掩码：255.255.255.0
                return boost::asio::ip::address_v4(0xFFFFFF00);
            }

            // D 类或 E 类地址
            // 默认掩码：255.255.255.255
            return boost::asio::ip::address_v4(0xFFFFFFFF);
        }

        /**
         * @brief 检查 IPv6 地址是否属于指定网段
         *
         * @param networkAddress IPv6 网络地址
         * @param prefixLength 前缀长度（类似 IPv4 的掩码，范围 0-128）
         * @param clientAddress 待检查的客户端 IPv6 地址
         * @return true 如果客户端地址属于该网段，否则返回 false
         *
         * 通过前缀长度判断一个 IPv6 地址是否属于指定的网络地址范围。
         * IPv6 使用前缀长度（CIDR 表示法）而非传统的子网掩码。
         *
         * 调用时机：
         * - IPv6 客户端连接时检查访问权限
         * - 实现 IPv6 地址过滤
         * - 判断 IPv6 客户端是否来自特定网段
         *
         * 典型用法：
         * @code
         * // 检查是否属于本地链路地址 (fe80::/10)
         * auto networkAddr = make_address_v6("fe80::");
         * auto clientAddr = make_address_v6("fe80::1");
         *
         * if (IsInNetwork(networkAddr, 10, clientAddr)) {
         *     // 客户端是本地链路地址
         * }
         *
         * // 检查是否属于特定子网 (2001:db8::/32)
         * auto subnet = make_address_v6("2001:db8::");
         * auto testAddr = make_address_v6("2001:db8:85a3::8a2e:370:7334");
         *
         * if (IsInNetwork(subnet, 32, testAddr)) {
         *     // 地址属于 2001:db8::/32 网段
         * }
         * @endcode
         *
         * 前缀长度说明：
         * - 0: 匹配所有 IPv6 地址（相当于 ::/0）
         * - 128: 精确匹配单个地址
         * - 常见前缀：
         *   - ::1/128: 本地回环地址
         *   - fe80::/10: 本地链路地址
         *   - fc00::/7: 唯一本地地址（类似 IPv4 私有地址）
         *   - 2000::/3: 全球单播地址
         *
         * 算法说明：
         * 1. 使用网络地址和前缀长度创建 network_v6 对象
         * 2. 获取该网络的所有主机地址范围
         * 3. 检查客户端地址是否在范围内
         *
         * 性能注意事项：
         * - 该函数创建临时网络对象，有一定开销
         * - 如果需要频繁检查同一网段，考虑缓存 network_v6 对象
         * - IPv6 地址比较比 IPv4 稍慢（128 位 vs 32 位）
         *
         * @note 前缀长度必须在 0-128 范围内
         * @warning 传入无效的前缀长度可能导致异常
         *
         * @see IsInNetwork(IPv4版本)
         */
        inline bool IsInNetwork(boost::asio::ip::address_v6 const& networkAddress, uint16 prefixLength, boost::asio::ip::address_v6 const& clientAddress)
        {
            // 创建 IPv6 网络对象，包含网络地址和前缀长度
            boost::asio::ip::network_v6 network = boost::asio::ip::make_network_v6(networkAddress, prefixLength);

            // 获取网络中所有主机地址的范围
            boost::asio::ip::address_v6_range hosts = network.hosts();

            // 检查客户端地址是否在主机地址范围内
            return hosts.find(clientAddress) != hosts.end();
        }
    }
}

#endif // IpNetwork_h__
