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
 * @file CryptoRandom.h
 * @brief 加密安全的随机数生成模块
 *
 * 本文件提供了加密安全的随机数生成功能，用于生成不可预测的随机字节序列。
 * 主要用途：
 * - 生成加密密钥和会话密钥
 * - 生成随机盐值（salt）
 * - 生成初始化向量（IV）
 * - 生成挑战-响应机制的随机数
 *
 * 安全特性：
 * - 使用操作系统提供的加密安全随机数生成器（CSPRNG）
 * - 随机数具有足够的熵，不可预测
 * - 适用于密码学应用场景
 */

#ifndef TRINITY_CRYPTORANDOM_H
#define TRINITY_CRYPTORANDOM_H

#include "Define.h"
#include <array>

/**
 * @namespace Trinity::Crypto
 * @brief TrinityCore 加密功能命名空间
 *
 * 提供各种加密相关的工具函数和类，包括：
 * - 随机数生成
 * - 哈希计算
 * - 加密解密操作
 */
namespace Trinity::Crypto
{
    /**
     * @brief 生成指定长度的加密安全随机字节
     *
     * 使用加密安全的伪随机数生成器（CSPRNG）生成随机字节序列。
     * 底层实现：
     * - Linux/Unix: 使用 /dev/urandom 或 getrandom() 系统调用
     * - Windows: 使用 CryptGenRandom() 或 BCryptGenRandom()
     * - OpenSSL: 使用 RAND_bytes()
     *
     * @param buf 输出缓冲区，用于存储生成的随机字节
     * @param len 需要生成的随机字节数量
     *
     * @note 此函数是线程安全的
     * @note 生成的随机数适用于密码学应用，不可预测
     * @warning 如果随机数生成失败，可能会抛出异常或终止程序
     */
    void TC_COMMON_API GetRandomBytes(uint8* buf, size_t len);

    /**
     * @brief 为容器填充加密安全的随机字节（模板函数）
     *
     * 便利函数，直接为任意标准容器填充随机字节。
     * 支持的容器类型包括：
     * - std::array
     * - std::vector
     * - std::string
     * - C 风格数组
     * - 任何支持 std::data() 和 std::size() 的容器
     *
     * @tparam Container 容器类型，必须支持 std::data() 和 std::size()
     * @param c 容器引用，将被填充随机字节
     *
     * @example
     * std::vector<uint8> buffer(32);
     * GetRandomBytes(buffer); // 填充 32 个随机字节
     *
     * std::array<uint8, 16> key;
     * GetRandomBytes(key); // 生成 16 字节的密钥
     */
    template <typename Container>
    void GetRandomBytes(Container& c)
    {
        // 使用 std::data() 和 std::size() 获取容器的数据指针和大小
        // 这样可以支持多种容器类型
        GetRandomBytes(std::data(c), std::size(c));
    }

    /**
     * @brief 生成固定大小的随机字节数组（模板函数）
     *
     * 便利函数，生成并返回一个固定大小的随机字节数组。
     * 常用于生成固定长度的密钥、IV 或盐值。
     *
     * @tparam S 数组大小（字节数）
     * @return std::array<uint8, S> 包含随机字节的数组
     *
     * @example
     * auto key = GetRandomBytes<32>(); // 生成 32 字节（256 位）的随机密钥
     * auto iv = GetRandomBytes<16>();  // 生成 16 字节（128 位）的 IV
     *
     * @note 函数返回值优化（RVO）会避免不必要的拷贝
     */
    template <size_t S>
    std::array<uint8, S> GetRandomBytes()
    {
        // 创建固定大小的数组
        std::array<uint8, S> arr;
        // 填充随机字节
        GetRandomBytes(arr);
        // 返回数组（RVO 会优化掉拷贝）
        return arr;
    }
}

#endif
