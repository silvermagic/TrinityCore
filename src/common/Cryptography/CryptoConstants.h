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
 * @file CryptoConstants.h
 * @brief 加密常量定义模块
 *
 * 本文件定义了 TrinityCore 加密模块中使用的各种哈希算法摘要长度常量。
 * 这些常量用于哈希计算、HMAC 运算、数字签名等密码学操作中，
 * 确保缓冲区大小和摘要长度的正确性。
 *
 * 主要用途：
 * - 定义 MD5、SHA1、SHA256 等哈希算法的摘要字节长度
 * - 为模板类提供编译时常量
 * - 保证加密模块中摘要长度的一致性
 */

#ifndef TRINITY_CRYPTO_CONSTANTS_H
#define TRINITY_CRYPTO_CONSTANTS_H

#include "Define.h"

namespace Trinity::Crypto
{
    /**
     * @brief 加密算法常量结构体
     *
     * 包含各种密码学算法的常量定义，主要用于哈希算法的摘要长度。
     * 所有常量均为编译时常量，可用于模板参数和静态数组大小声明。
     */
    struct Constants
    {
        /** @brief MD5 摘要长度（字节），MD5 产生 128 位（16 字节）的哈希值 */
        static constexpr size_t MD5_DIGEST_LENGTH_BYTES = 16;

        /** @brief SHA-1 摘要长度（字节），SHA-1 产生 160 位（20 字节）的哈希值 */
        static constexpr size_t SHA1_DIGEST_LENGTH_BYTES = 20;

        /** @brief SHA-256 摘要长度（字节），SHA-256 产生 256 位（32 字节）的哈希值 */
        static constexpr size_t SHA256_DIGEST_LENGTH_BYTES = 32;
    };
}

#endif
