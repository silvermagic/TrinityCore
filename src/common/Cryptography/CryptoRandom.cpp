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
 * @file CryptoRandom.cpp
 * @brief 密码学安全随机数生成器实现
 *
 * 本文件提供了密码学安全的随机数生成功能，用于生成不可预测的随机字节序列。
 * 这些随机数适用于密码学操作，如密钥生成、会话令牌、加密盐值等安全敏感场景。
 *
 * 实现基于 OpenSSL 库的 RAND_bytes 函数，该函数使用操作系统提供的
 * 密码学安全伪随机数生成器(CSPRNG)：
 * - Linux/Unix: 读取 /dev/urandom 或使用 getrandom() 系统调用
 * - Windows: 使用 CryptGenRandom 或 BCryptGenRandom API
 *
 * 安全特性：
 * - 输出的随机字节在密码学意义上不可预测
 * - 不依赖于可被预测的种子或状态
 * - 满足密码学应用的安全要求
 */

#include "CryptoRandom.h"
#include "Errors.h"
#include <openssl/rand.h>

/**
 * @brief 生成密码学安全的随机字节序列
 *
 * @职责：
 * 使用 OpenSSL 的密码学安全随机数生成器(CSPRNG)填充指定的缓冲区，
 * 生成适用于密码学操作的高质量随机字节序列。
 *
 * @参数：
 * @param buf - 目标缓冲区指针，用于存储生成的随机字节
 *              调用者需确保缓冲区至少有 len 字节的空间
 * @param len - 需要生成的随机字节数量
 *              可以是任意正整数，但大请求可能消耗更多熵池资源
 *
 * @返回值：
 * 无（void 函数）。生成的随机字节直接写入 buf 指向的缓冲区。
 *
 * @异常：
 * 如果随机数生成失败（例如系统熵池耗尽），将触发断言失败并终止程序。
 * 这是故意的设计：在密码学上下文中，无法生成随机数是致命错误，
 * 比返回可预测的伪随机值更安全（"快速失败"原则）。
 *
 * @主要流程：
 * 1. 调用 OpenSSL 的 RAND_bytes 函数请求 len 个随机字节
 *    - RAND_bytes 内部使用操作系统提供的 CSPRNG
 *    - 在 Linux 上通常使用 getrandom() 或 /dev/urandom
 *    - 在 Windows 上使用 CryptoAPI 的随机数生成器
 *
 * 2. 检查返回值确认操作成功
 *    - 返回 1 表示成功生成随机字节
 *    - 返回 0 或 -1 表示失败（通常是熵不足）
 *
 * 3. 如果失败则触发断言错误
 *    - 错误信息明确指出是 OpenSSL 熵池问题
 *    - 这通常只会在极端情况下发生（如系统启动早期）
 *
 * @安全注意事项：
 * - 此函数生成的随机数适用于密码学操作
 * - 永远不要用普通的 rand() 或 srand() 替代此函数用于安全目的
 * - 在性能敏感场景，可以考虑批量生成随机字节并缓存
 *
 * @使用示例：
 * @code
 * uint8 sessionKey[32];
 * GetRandomBytes(sessionKey, 32);  // 生成 256 位会话密钥
 *
 * uint8 salt[16];
 * GetRandomBytes(salt, 16);        // 生成 128 位盐值
 * @endcode
 *
 * @see RAND_bytes(3) - OpenSSL 文档
 * @see GetRandomBytes(Container& c) - 模板版本，用于 STL 容器
 */
void Trinity::Crypto::GetRandomBytes(uint8* buf, size_t len)
{
    // 调用 OpenSSL 的密码学安全随机数生成器
    // RAND_bytes 返回 1 表示成功，0 或 -1 表示失败
    int result = RAND_bytes(buf, len);

    // 断言检查：确保随机数生成成功
    // 失败通常意味着系统熵池耗尽，这是严重的安全问题
    // 在密码学上下文中，无法获取真随机数比崩溃更危险
    ASSERT(result == 1, "Not enough randomness in OpenSSL's entropy pool. What in the world are you running on?");
}
