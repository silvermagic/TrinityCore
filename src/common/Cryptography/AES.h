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
 * @file AES.h
 * @brief AES 加密模块头文件 - 实现 AES-128-GCM 加密算法
 *
 * 模块职责:
 *   提供 AES-128-GCM (Advanced Encryption Standard with Galois/Counter Mode) 加密和解密功能
 *   用于保护网络通信数据的安全性和完整性
 *
 * 主要功能:
 *   - AES-128 位密钥加密/解密
 *   - GCM 模式提供认证加密 (Authenticated Encryption)
 *   - 自动生成和验证认证标签 (Authentication Tag)
 *   - 支持初始化向量 (IV) 确保加密随机性
 *
 * AES 加密原理:
 *   AES (高级加密标准) 是一种对称分组加密算法:
 *   - 使用 128 位固定分组大小
 *   - 支持 128/192/256 位密钥长度 (本实现使用 128 位)
 *   - 通过多轮替换和置换操作实现加密
 *   - 每轮包含: 字节替换、行移位、列混淆、轮密钥加
 *
 * GCM 模式特点:
 *   - 结合 CTR (计数器) 模式和 GHASH 认证
 *   - 同时提供加密和数据完整性验证
 *   - 生成的认证标签可检测任何篡改
 *   - 适用于高速网络通信加密
 *
 * 使用场景:
 *   - 游戏客户端与服务器之间的敏感数据传输
 *   - 认证信息加密
 *   - 防止数据包篡改和重放攻击
 *
 * @see AES.cpp 实现文件
 */

#ifndef Trinity_AES_h__
#define Trinity_AES_h__

#include "Define.h"
#include <array>
#include <openssl/evp.h>

namespace Trinity::Crypto
{
    /**
     * @class AES
     * @brief AES-128-GCM 加密器 - 提供认证加密功能
     *
     * 该类封装了 OpenSSL 的 EVP 加密接口,实现 AES-128-GCM 加密算法。
     * GCM 模式提供加密和认证双重保障,确保数据的机密性和完整性。
     *
     * 使用流程:
     *   1. 构造对象时指定加密或解密模式
     *   2. 调用 Init() 设置加密密钥
     *   3. 调用 Process() 执行加密/解密操作
     *
     * 线程安全性:
     *   - 非线程安全,每个线程应使用独立的实例
     *   - 不可复制或移动
     *
     * 示例用法:
     * @code
     *   // 加密
     *   AES encryptor(true);
     *   encryptor.Init(key);
     *   encryptor.Process(iv, plaintext, length, tag);
     *
     *   // 解密
     *   AES decryptor(false);
     *   decryptor.Init(key);
     *   if (decryptor.Process(iv, ciphertext, length, tag)) {
     *       // 解密成功,tag 验证通过
     *   }
     * @endcode
     */
    class TC_COMMON_API AES
    {
    public:
        /** @brief 初始化向量 (IV) 大小: 12 字节 (96 位) */
        static constexpr size_t IV_SIZE_BYTES = 12;

        /** @brief 密钥大小: 16 字节 (128 位) - AES-128 标准 */
        static constexpr size_t KEY_SIZE_BYTES = 16;

        /** @brief 认证标签大小: 12 字节 (96 位) */
        static constexpr size_t TAG_SIZE_BYTES = 12;

        /** @brief 初始化向量类型 - 12 字节数组 */
        using IV = std::array<uint8, IV_SIZE_BYTES>;

        /** @brief 密钥类型 - 16 字节数组 */
        using Key = std::array<uint8, KEY_SIZE_BYTES>;

        /** @brief 认证标签类型 - 12 字节数组 */
        using Tag = uint8[TAG_SIZE_BYTES];

        /**
         * @brief 构造函数 - 初始化 AES 加密上下文
         *
         * 创建并配置 OpenSSL EVP 加密上下文为 AES-128-GCM 模式。
         * 必须在构造后调用 Init() 设置密钥才能使用。
         *
         * @param encrypting true=加密模式, false=解密模式
         *
         * @note 构造失败会触发断言,程序终止
         */
        AES(bool encrypting);

        /**
         * @brief 析构函数 - 释放加密上下文资源
         *
         * 清理 OpenSSL EVP 上下文,防止内存泄漏。
         */
        ~AES();

        /**
         * @brief 初始化加密密钥
         *
         * 为加密上下文设置 128 位密钥。必须在 Process() 之前调用。
         *
         * @param key 16 字节的加密密钥
         *
         * @note 密钥设置失败会触发断言,程序终止
         * @warning 密钥必须保密,不可在不安全的地方存储或传输
         */
        void Init(Key const& key);

        /**
         * @brief 执行加密或解密操作
         *
         * 使用 AES-128-GCM 算法对数据进行加密或解密,同时处理认证标签。
         * 数据在原缓冲区中进行原地处理 (in-place)。
         *
         * 加密模式:
         *   - 输入明文数据,输出密文
         *   - 生成认证标签用于后续验证
         *
         * 解密模式:
         *   - 输入密文数据,输出明文
         *   - 验证提供的认证标签,检测数据是否被篡改
         *   - 标签验证失败返回 false
         *
         * @param iv 初始化向量 (12 字节),确保相同明文加密得到不同密文
         * @param data 数据缓冲区 (输入/输出),加密时明文->密文,解密时相反
         * @param length 数据长度 (字节数)
         * @param tag 认证标签 (12 字节):
         *            - 加密时: 输出参数,存储生成的标签
         *            - 解密时: 输入参数,提供待验证的标签
         *
         * @return true 操作成功 (解密时标签验证通过)
         * @return false 操作失败 (OpenSSL 错误或解密时标签验证失败)
         *
         * @note IV 应该是随机生成的,不可重复使用相同的 IV 和密钥组合
         * @warning 数据长度不能超过 INT_MAX (约 2GB)
         */
        bool Process(IV const& iv, uint8* data, size_t length, Tag& tag);

    private:
        /** @brief OpenSSL EVP 加密上下文指针 - 存储加密状态和算法配置 */
        EVP_CIPHER_CTX* _ctx;

        /** @brief 加密模式标志 - true=加密, false=解密 */
        bool _encrypting;
    };
}

#endif // Trinity_AES_h__
