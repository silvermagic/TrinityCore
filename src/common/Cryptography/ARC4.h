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
 * @file ARC4.h
 * @brief ARC4 流加密算法封装类
 *
 * 模块职责:
 *   提供 ARC4 (Alleged RC4) 流加密算法的 C++ 封装实现，基于 OpenSSL EVP 接口
 *
 * 主要功能:
 *   1. 封装 OpenSSL 的 RC4 流密码实现
 *   2. 提供简洁的初始化和加密/解密接口
 *   3. 支持任意长度的密钥种子
 *   4. 提供容器类型的便捷模板方法
 *
 * ARC4 流加密原理:
 *   ARC4 是一种对称流加密算法（也称为密钥流加密），其工作原理如下：
 *
 *   1. 密钥调度算法 (KSA - Key Scheduling Algorithm):
 *      - 使用用户提供的密钥初始化一个 256 字节的状态向量 S (0-255)
 *      - 通过置换操作打乱 S 向量的顺序
 *      - 密钥长度可变，通常为 40-2048 位
 *
 *   2. 伪随机生成算法 (PRGA - Pseudo-Random Generation Algorithm):
 *      - 从 S 向量中生成伪随机密钥流
 *      - 每次生成一个字节的密钥流
 *      - 密钥流与明文进行异或运算生成密文
 *
 *   3. 加密/解密特性:
 *      - 对称性：加密和解密使用相同的操作（异或运算可逆）
 *      - 流密码：逐字节处理，适合数据流加密
 *      - 相同密钥不能重用：每次加密应使用不同的密钥或初始化向量
 *
 *   4. 在 TrinityCore 中的应用:
 *      - 主要用于网络通信的加密保护
 *      - 客户端-服务器认证流程
 *      - 数据包加密
 *
 * 注意事项:
 *   - ARC4 现在已不再推荐用于新的安全系统（存在已知的弱点）
 *   - 在新的应用中应考虑使用更安全的算法（如 AES-GCM）
 *   - 本实现主要用于兼容 WoW 协议和旧系统
 */

#ifndef _AUTH_SARC4_H
#define _AUTH_SARC4_H

#include "Define.h"
#include <array>
#include <openssl/evp.h>

namespace Trinity::Crypto
{
    /**
     * @class ARC4
     * @brief ARC4 流加密算法封装类
     *
     * 该类封装了 OpenSSL 的 EVP (Envelope Encryption) 接口，提供 RC4 流密码功能。
     * RC4 是一种广泛使用的流加密算法，以其简单性和速度著称。
     *
     * 主要特性:
     *   - 对称加密：加密和解密使用相同的操作
     *   - 流密码：可以处理任意长度的数据流
     *   - 变长密钥：支持不同长度的密钥种子
     *
     * 使用流程:
     *   1. 创建 ARC4 对象
     *   2. 调用 Init() 方法设置密钥
     *   3. 调用 UpdateData() 方法加密/解密数据
     *
     * 兼容性:
     *   - 支持 OpenSSL 1.x 和 3.x 版本
     *   - OpenSSL 3.x 使用动态获取密码对象的方式
     *   - OpenSSL 1.x 使用内置密码对象
     *
     * 示例代码:
     * @code
     *   ARC4 cipher;
     *   std::vector<uint8> key = {0x01, 0x02, 0x03};
     *   cipher.Init(key);
     *   cipher.UpdateData(data); // 原地加密/解密
     * @endcode
     */
    class TC_COMMON_API ARC4
    {
        public:
            /**
             * @brief 构造函数 - 初始化 ARC4 加密上下文
             *
             * 创建并初始化 OpenSSL EVP 加密上下文对象。
             * 根据 OpenSSL 版本选择合适的密码算法获取方式。
             */
            ARC4();

            /**
             * @brief 析构函数 - 清理加密上下文资源
             *
             * 释放 OpenSSL EVP 上下文和密码对象，防止资源泄漏。
             */
            ~ARC4();

            /**
             * @brief 初始化 ARC4 密钥
             *
             * 使用提供的种子数据作为密钥，初始化 ARC4 流密码状态。
             * 必须在进行任何加密/解密操作之前调用此方法。
             *
             * @param seed 指向密钥/种子数据的指针
             * @param len  密钥数据的字节长度
             */
            void Init(uint8 const* seed, size_t len);

            /**
             * @brief 初始化 ARC4 密钥（容器版本）
             *
             * 模板方法，接受标准容器类型的密钥数据。
             * 自动从容器中提取数据和大小。
             *
             * @tparam Container 容器类型（如 std::vector, std::array 等）
             * @param c          包含密钥数据的容器
             */
            template <typename Container>
            void Init(Container const& c) { Init(std::data(c), std::size(c)); }

            /**
             * @brief 加密/解密数据
             *
             * 使用 ARC4 流密码对数据进行加密或解密（操作相同）。
             * 数据将在原缓冲区中进行修改（原地加密）。
             *
             * 由于 ARC4 是流密码且使用异或运算，加密和解密使用相同操作：
             *   - 加密：明文 XOR 密钥流 = 密文
             *   - 解密：密文 XOR 密钥流 = 明文
             *
             * @param data 指向待处理数据的缓冲区（原地修改）
             * @param len  数据的字节长度
             */
            void UpdateData(uint8* data, size_t len);

            /**
             * @brief 加密/解密数据（容器版本）
             *
             * 模板方法，接受标准容器类型的数据。
             * 数据将在原容器中进行修改（原地加密）。
             *
             * @tparam Container 容器类型（如 std::vector, std::array 等）
             * @param c          包含待处理数据的容器（原地修改）
             */
            template <typename Container>
            void UpdateData(Container& c) { UpdateData(std::data(c), std::size(c)); }

        private:
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            /**
             * @brief 密码算法对象（OpenSSL 3.0+）
             *
             * 在 OpenSSL 3.0 及更高版本中，密码算法对象需要动态获取。
             * 使用 EVP_CIPHER_fetch() 从 OpenSSL 提供者中获取 RC4 密码实现。
             */
            EVP_CIPHER* _cipher;
#endif

            /**
             * @brief 加密上下文对象
             *
             * OpenSSL EVP 加密上下文，存储加密操作所需的所有状态信息。
             * 包括加密算法、密钥、初始化向量等。
             *
             * EVP (Envelope Encryption) 是 OpenSSL 提供的高级加密接口，
             * 提供了统一的 API 来访问不同的加密算法。
             */
            EVP_CIPHER_CTX* _ctx;
    };
}

#endif
