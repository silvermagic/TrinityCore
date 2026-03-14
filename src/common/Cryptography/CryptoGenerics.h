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
 * @file CryptoGenerics.h
 * @brief 通用加密工具函数模块
 *
 * 本文件提供了与加密算法相关的通用辅助工具函数，
 * 主要用于认证加密（Authenticated Encryption, AE）操作。
 *
 * 主要功能：
 * - 生成随机初始化向量（IV）
 * - 数据封装和拆封操作
 * - 认证加密（AEAD）的高层封装
 *
 * 认证加密说明：
 * 认证加密是一种同时提供保密性、完整性和真实性的加密方式。
 * 典型的 AEAD 算法包括 AES-GCM、ChaCha20-Poly1305 等。
 *
 * 数据格式：
 * 加密后的数据格式为：[密文 || IV || Tag]
 * - 密文：加密后的原始数据
 * - IV：初始化向量（用于解密）
 * - Tag：认证标签（用于验证完整性）
 *
 * 使用场景：
 * - 游戏数据包加密
 * - 敏感配置数据存储
 * - 会话令牌加密
 */

#ifndef TRINITY_CRYPTO_GENERICS_HPP
#define TRINITY_CRYPTO_GENERICS_HPP

#include "BigNumber.h"
#include "CryptoRandom.h"
#include "Define.h"
#include "Errors.h"
#include <iterator>
#include <vector>

namespace Trinity::Impl
{
    /**
     * @brief 加密通用工具实现结构体
     *
     * 提供加密操作中常用的辅助函数，包括：
     * - 随机 IV 生成
     * - 数据追加和拆分
     *
     * 这些函数主要用于支持认证加密（AEAD）的高层封装。
     */
    struct CryptoGenericsImpl
    {
        /**
         * @brief 生成随机初始化向量（IV）
         *
         * 为指定的加密算法生成一个随机的初始化向量。
         * IV 用于确保相同的明文在每次加密后产生不同的密文。
         *
         * @tparam Cipher 加密算法类型（需要有 IV 类型定义）
         * @return 随机生成的初始化向量
         *
         * 安全性说明：
         * - IV 不需要保密，但必须是唯一的
         * - 对于大多数模式（如 CBC、GCM），IV 应该是不可预测的随机值
         * - GCM 模式下 IV 的唯一性至关重要，重复使用相同的 IV 会严重破坏安全性
         *
         * 调用时机：每次加密操作开始前生成新的 IV
         */
        template <typename Cipher>
        static typename Cipher::IV GenerateRandomIV()
        {
            typename Cipher::IV iv;
            // 使用加密安全的随机数生成器填充 IV
            Trinity::Crypto::GetRandomBytes(iv);
            return iv;
        }

        /**
         * @brief 将容器数据追加到字节向量末尾
         *
         * 用于将 IV 或认证标签追加到加密数据后，
         * 便于存储或传输。
         *
         * @tparam Container 容器类型（需要有迭代器支持）
         * @param data 目标字节向量
         * @param tail 要追加的数据容器
         *
         * 数据格式：[原有数据 || tail数据]
         *
         * 使用场景：
         * - 加密后将 IV 和 Tag 追加到密文
         * - 构建加密数据包
         */
        template <typename Container>
        static void AppendToBack(std::vector<uint8>& data, Container const& tail)
        {
            data.insert(data.end(), std::begin(tail), std::end(tail));
        }

        /**
         * @brief 从字节向量末尾拆分数据到容器
         *
         * 从数据末尾提取指定大小的数据（如 IV 或 Tag），
         * 并从原向量中移除。用于解密操作前的数据解析。
         *
         * @tparam Container 目标容器类型
         * @param data 源字节向量（会被修改，移除末尾数据）
         * @param tail 目标容器（用于存储提取的数据）
         *
         * @throws 如果数据长度不足，触发 ASSERT 断言
         *
         * 数据格式：从 [数据 || tail] 变为 [数据]，tail 被提取
         *
         * 使用场景：
         * - 解密前从密文中提取 IV 和 Tag
         * - 解析加密数据包
         *
         * 注意：数据从末尾按逆序提取，保持与追加顺序相反
         */
        template <typename Container>
        static void SplitFromBack(std::vector<uint8>& data, Container& tail)
        {
            // 确保数据足够长
            ASSERT(data.size() >= std::size(tail));
            // 从后向前提取数据
            for (size_t i = 1, N = std::size(tail); i <= N; ++i)
            {
                tail[N - i] = data.back();
                data.pop_back();
            }
        }
    };
}

namespace Trinity::Crypto
{
    /**
     * @brief 使用随机 IV 进行认证加密
     *
     * 对数据进行认证加密（AEAD），自动生成随机 IV，
     * 并将 IV 和认证标签追加到密文后。
     *
     * @tparam Cipher 加密算法类型（需支持 AEAD 模式，如 AES-GCM）
     * @param data 要加密的数据（输入明文，输出格式：密文 || IV || Tag）
     * @param key 加密密钥
     *
     * 输出数据格式：
     * [密文 || IV || Tag]
     * - 密文：与明文长度相同
     * - IV：初始化向量（Cipher::IV 类型的长度）
     * - Tag：认证标签（Cipher::Tag 类型的长度）
     *
     * 处理流程：
     * 1. 生成随机 IV
     * 2. 使用密钥和 IV 加密数据
     * 3. 生成认证标签
     * 4. 将 IV 和 Tag 追加到密文后
     *
     * 使用场景：
     * - 加密敏感数据存储
     * - 加密网络传输数据
     * - 加密游戏配置信息
     *
     * 安全性保证：
     * - 保密性：没有密钥无法解密
     * - 完整性：任何篡改都会导致解密失败
     * - 真实性：只有拥有密钥的方能生成有效密文
     *
     * @throws 如果加密失败，触发 ASSERT 断言
     */
    template <typename Cipher>
    void AEEncryptWithRandomIV(std::vector<uint8>& data, typename Cipher::Key const& key)
    {
        using IV = typename Cipher::IV;
        using Tag = typename Cipher::Tag;

        // 生成随机 IV（初始化向量）
        IV iv = Trinity::Impl::CryptoGenericsImpl::GenerateRandomIV<Cipher>();
        Tag tag;

        // 创建加密器实例（参数 true 表示加密模式）
        Cipher cipher(true);
        cipher.Init(key);

        // 执行加密操作，生成认证标签
        bool success = cipher.Process(iv, data.data(), data.size(), tag);
        ASSERT(success);

        // 将 IV 和 Tag 追加到密文后，便于后续解密使用
        Trinity::Impl::CryptoGenericsImpl::AppendToBack(data, iv);
        Trinity::Impl::CryptoGenericsImpl::AppendToBack(data, tag);
    }

    /**
     * @brief 使用 BigNumber 密钥进行认证加密
     *
     * BigNumber 密钥版本的认证加密，将 BigNumber 转换为字节数组后调用上述函数。
     * 常用于从 BigNumber 派生加密密钥的场景。
     *
     * @tparam Cipher 加密算法类型
     * @param data 要加密的数据（输入明文，输出：密文 || IV || Tag）
     * @param key BigNumber 类型的密钥
     *
     * 使用场景：
     * - 使用 SRP6 会话密钥加密数据
     * - 使用 BigNumber 派生密钥加密
     */
    template <typename Cipher>
    void AEEncryptWithRandomIV(std::vector<uint8>& data, BigNumber const& key)
    {
        // 将 BigNumber 转换为指定长度的字节数组作为密钥
        AEEncryptWithRandomIV<Cipher>(data, key.ToByteArray<Cipher::KEY_SIZE_BYTES>());
    }

    /**
     * @brief 认证解密
     *
     * 从加密数据中提取 IV 和 Tag，执行解密和完整性验证。
     * 如果认证失败，数据将被保持无效状态。
     *
     * @tparam Cipher 加密算法类型
     * @param data 加密数据（输入：密文 || IV || Tag，输出：明文）
     * @param key 解密密钥
     * @return true 解密成功且认证通过
     * @return false 认证失败（数据可能被篡改或密钥错误）
     *
     * 处理流程：
     * 1. 从数据末尾提取 Tag
     * 2. 从数据末尾提取 IV
     * 3. 使用密钥和 IV 解密数据
     * 4. 验证认证标签
     *
     * 安全性说明：
     * - 认证失败时返回 false，不应信任解密后的数据
     * - 防止选择密文攻击（CCA）
     * - 检测任何篡改或损坏
     *
     * 使用场景：
     * - 解密存储的敏感数据
     * - 解密网络传输数据
     * - 解密游戏配置信息
     *
     * 性能注意：
     * - 解密操作包含验证步骤，失败时会提前返回
     * - 数据向量会被修改（移除 IV 和 Tag）
     */
    template <typename Cipher>
    bool AEDecrypt(std::vector<uint8>& data, typename Cipher::Key const& key)
    {
        using IV = typename Cipher::IV;
        using Tag = typename Cipher::Tag;

        // 从数据末尾提取认证标签和 IV
        IV iv;
        Tag tag;
        Trinity::Impl::CryptoGenericsImpl::SplitFromBack(data, tag);
        Trinity::Impl::CryptoGenericsImpl::SplitFromBack(data, iv);

        // 创建解密器实例（参数 false 表示解密模式）
        Cipher cipher(false);
        cipher.Init(key);

        // 执行解密和认证验证
        return cipher.Process(iv, data.data(), data.size(), tag);
    }

    /**
     * @brief 使用 BigNumber 密钥进行认证解密
     *
     * BigNumber 密钥版本的认证解密。
     *
     * @tparam Cipher 加密算法类型
     * @param data 加密数据（输入：密文 || IV || Tag，输出：明文）
     * @param key BigNumber 类型的密钥
     * @return true 解密成功
     * @return false 认证失败
     *
     * 使用场景：
     * - 使用 SRP6 会话密钥解密数据
     */
    template <typename Cipher>
    bool AEDecrypt(std::vector<uint8>& data, BigNumber const& key)
    {
        return AEDecrypt<Cipher>(data, key.ToByteArray<Cipher::KEY_SIZE_BYTES>());
    }
}

#endif
