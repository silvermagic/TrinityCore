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
 * @file HMAC.h
 * @brief HMAC（基于哈希的消息认证码）实现模块
 *
 * 本文件提供了 HMAC（Hash-based Message Authentication Code）的实现。
 * HMAC 是一种使用密钥和哈希函数进行消息认证的机制，广泛用于：
 * - 消息完整性验证
 * - 身份认证
 * - 密钥派生
 * - 防篡改保护
 *
 * HMAC 工作原理：
 * HMAC(K, m) = H((K ^ opad) || H((K ^ ipad) || m))
 * 其中：
 * - K 是密钥
 * - m 是消息
 * - H 是哈希函数（如 SHA-1、SHA-256）
 * - opad 是外部填充（0x5c 重复）
 * - ipad 是内部填充（0x36 重复）
 *
 * 主要功能：
 * - 提供 GenericHMAC 模板类，支持多种哈希算法
 * - 支持 HMAC-SHA1 和 HMAC-SHA256
 * - 支持流式数据更新
 *
 * 使用场景：
 * - 游戏协议中的消息认证
 * - SRP6 认证协议
 * - TOTP（时间基一次性密码）
 * - 密钥派生函数
 *
 * 安全性：
 * - HMAC 的安全性依赖于底层的哈希函数
 * - 即使哈希函数存在某些弱点，HMAC 仍能保持较好的安全性
 */

#ifndef TRINITY_HMAC_H
#define TRINITY_HMAC_H

#include "CryptoConstants.h"
#include "CryptoHash.h"
#include "Define.h"
#include "Errors.h"
#include <array>
#include <string>
#include <string_view>

class BigNumber;

namespace Trinity::Impl
{
    /**
     * @brief 通用 HMAC 实现模板类
     *
     * 基于 OpenSSL EVP 接口实现的通用 HMAC 计算器。
     * HMAC 是一种使用密钥的消息认证机制，可以验证消息的完整性和真实性。
     *
     * @tparam HashCreator 哈希算法创建函数指针，如 EVP_sha1、EVP_sha256 等
     * @tparam DigestLength HMAC 摘要长度（字节数），与底层哈希算法一致
     *
     * 实现说明：
     * - 使用 OpenSSL EVP_PKEY 接口实现 HMAC
     * - 支持流式数据更新
     * - 支持 RAII 资源管理
     * - 支持拷贝和移动语义
     *
     * 使用示例：
     * @code
     * HMAC_SHA1 hmac(secretKey);
     * hmac.UpdateData("message");
     * hmac.Finalize();
     * auto mac = hmac.GetDigest();
     * @endcode
     *
     * 线程安全：非线程安全，每个线程应使用独立的实例
     */
    template <GenericHashImpl::HashCreator HashCreator, size_t DigestLength>
    class GenericHMAC
    {
        public:
            /** @brief HMAC 摘要长度常量，与底层哈希算法的摘要长度一致 */
            static constexpr size_t DIGEST_LENGTH = DigestLength;

            /** @brief 摘要类型，固定大小的字节数组 */
            using Digest = std::array<uint8, DIGEST_LENGTH>;

            /**
             * @brief 计算 HMAC 摘要（静态便捷方法，原始指针版本）
             *
             * 使用给定的密钥和数据计算 HMAC 摘要。
             * 一次性完成初始化、更新、最终化操作。
             *
             * @tparam Container 密钥容器类型
             * @param seed HMAC 密钥
             * @param data 指向数据缓冲区的指针
             * @param len 数据长度（字节数）
             * @return HMAC 摘要值
             *
             * 调用时机：当需要快速计算一段数据的 HMAC 时使用
             *
             * 性能注意：每次调用都会创建和销毁内部上下文
             */
            template <typename Container>
            static Digest GetDigestOf(Container const& seed, uint8 const* data, size_t len)
            {
                GenericHMAC hash(seed);
                hash.UpdateData(data, len);
                hash.Finalize();
                return hash.GetDigest();
            }

            /**
             * @brief 计算 HMAC 摘要（静态便捷方法，多数据块版本）
             *
             * 支持可变参数模板，可以一次性传入多个数据块进行 HMAC 计算。
             *
             * @tparam Container 密钥容器类型
             * @tparam Ts 可变参数类型，通过 SFINAE 排除整型参数
             * @param seed HMAC 密钥
             * @param pack 多个数据块（支持字符串、容器、数组等）
             * @return HMAC 摘要值
             *
             * 使用示例：
             * @code
             * auto mac = HMAC_SHA1::GetDigestOf(secretKey, "prefix", dataBuffer, "suffix");
             * @endcode
             */
            template <typename Container, typename... Ts>
            static auto GetDigestOf(Container const& seed, Ts&&... pack) -> std::enable_if_t<!(std::is_integral_v<std::decay_t<Ts>> || ...), Digest>
            {
                GenericHMAC hash(seed);
                // 使用折叠表达式依次更新每个数据块
                (hash.UpdateData(std::forward<Ts>(pack)), ...);
                hash.Finalize();
                return hash.GetDigest();
            }

            /**
             * @brief 构造函数（原始指针版本）
             *
             * 使用给定的密钥初始化 HMAC 计算上下文。
             *
             * @param seed 指向密钥数据的指针
             * @param len 密钥长度（字节数）
             *
             * @throws 如果 OpenSSL 初始化失败，触发 ASSERT 断言
             *
             * 实现细节：
             * - 创建 EVP_MD_CTX 上下文
             * - 使用 EVP_PKEY_new_mac_key 创建 HMAC 密钥对象
             * - 调用 EVP_DigestSignInit 初始化签名操作
             */
            GenericHMAC(uint8 const* seed, size_t len) : _ctx(GenericHashImpl::MakeCTX()), _key(EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, nullptr, seed, len))
            {
                // 初始化 HMAC 签名上下文
                int result = EVP_DigestSignInit(_ctx, nullptr, HashCreator(), nullptr, _key);
                ASSERT(result == 1);
            }

            /**
             * @brief 构造函数（容器版本）
             *
             * 从容器中提取密钥数据并初始化 HMAC。
             *
             * @tparam Container 密钥容器类型（需要有 data() 和 size() 方法）
             * @param container 密钥容器
             */
            template <typename Container>
            GenericHMAC(Container const& container) : GenericHMAC(std::data(container), std::size(container)) {}

            /**
             * @brief 拷贝构造函数
             *
             * 创建新的 HMAC 对象并复制另一个对象的状态。
             *
             * @param right 源 HMAC 对象
             *
             * 使用场景：当需要在 HMAC 计算过程中创建快照时
             */
            GenericHMAC(GenericHMAC const& right) : _ctx(GenericHashImpl::MakeCTX())
            {
                *this = right;
            }

            /**
             * @brief 移动构造函数
             *
             * 通过移动语义转移 HMAC 对象的所有权。
             *
             * @param right 源 HMAC 对象（右值引用）
             */
            GenericHMAC(GenericHMAC&& right) noexcept
            {
                *this = std::move(right);
            }

            /**
             * @brief 析构函数
             *
             * 释放 OpenSSL HMAC 上下文和密钥资源。
             * 遵循 RAII 原则，确保资源正确释放。
             */
            ~GenericHMAC()
            {
                GenericHashImpl::DestroyCTX(_ctx);
                _ctx = nullptr;
                EVP_PKEY_free(_key);
                _key = nullptr;
            }

            /**
             * @brief 拷贝赋值运算符
             *
             * 复制另一个 HMAC 对象的完整状态。
             *
             * @param right 源 HMAC 对象
             * @return 当前对象的引用
             *
             * 实现细节：
             * - 深拷贝 OpenSSL 上下文
             * - 增加密钥对象的引用计数（EVP_PKEY 使用引用计数管理）
             *
             * @throws 如果 OpenSSL 拷贝失败，触发 ASSERT 断言
             */
            GenericHMAC& operator=(GenericHMAC const& right)
            {
                if (this == &right)
                    return *this;

                // 深拷贝 OpenSSL 上下文
                int result = EVP_MD_CTX_copy_ex(_ctx, right._ctx);
                ASSERT(result == 1);

                // EVP_PKEY 使用内部引用计数，直接复制指针
                _key = right._key;
                // 增加引用计数：每个实例持有两个引用（构造函数和拷贝）
                // 析构函数会减少引用计数
                EVP_PKEY_up_ref(_key);
                _digest = right._digest;
                return *this;
            }

            /**
             * @brief 移动赋值运算符
             *
             * 通过移动语义转移 HMAC 对象的所有权。
             *
             * @param right 源 HMAC 对象（右值引用）
             * @return 当前对象的引用
             */
            GenericHMAC& operator=(GenericHMAC&& right) noexcept
            {
                if (this == &right)
                    return *this;

                // 交换资源
                _ctx = std::exchange(right._ctx, GenericHashImpl::MakeCTX());
                _key = std::exchange(right._key, EVP_PKEY_new());
                _digest = std::exchange(right._digest, Digest{});
                return *this;
            }

            /**
             * @brief 更新 HMAC 数据（原始字节数组）
             *
             * 将数据添加到 HMAC 计算中。可以多次调用以处理大数据或流式数据。
             *
             * @param data 指向数据缓冲区的指针
             * @param len 数据长度（字节数）
             *
             * @throws 如果 OpenSSL 更新失败，触发 ASSERT 断言
             *
             * 调用时机：在构造对象后、Finalize() 前调用
             */
            void UpdateData(uint8 const* data, size_t len)
            {
                int result = EVP_DigestSignUpdate(_ctx, data, len);
                ASSERT(result == 1);
            }

            /**
             * @brief 更新 HMAC 数据（字符串视图）
             * @param str 字符串视图
             */
            void UpdateData(std::string_view str) { UpdateData(reinterpret_cast<uint8 const*>(str.data()), str.size()); }

            /**
             * @brief 更新 HMAC 数据（std::string）
             * @param str 字符串引用
             *
             * 显式重载以避免使用容器模板版本
             */
            void UpdateData(std::string const& str) { UpdateData(std::string_view(str)); } /* explicit overload to avoid using the container template */

            /**
             * @brief 更新 HMAC 数据（C 风格字符串）
             * @param str 以 null 结尾的 C 字符串指针
             */
            void UpdateData(char const* str) { UpdateData(std::string_view(str)); } /* explicit overload to avoid using the container template */

            /**
             * @brief 更新 HMAC 数据（通用容器）
             *
             * 模板版本，支持任何有 data() 和 size() 方法的容器。
             *
             * @tparam Container 容器类型
             * @param c 容器实例
             */
            template <typename Container>
            void UpdateData(Container const& c) { UpdateData(std::data(c), std::size(c)); }

            /**
             * @brief 完成 HMAC 计算
             *
             * 完成 HMAC 计算并存储结果。调用后不能再调用 UpdateData()。
             *
             * @throws 如果 OpenSSL 最终化失败或长度不匹配，触发 ASSERT 断言
             *
             * 调用时机：所有数据更新完成后调用一次
             */
            void Finalize()
            {
                size_t length = DIGEST_LENGTH;
                // 完成签名计算，结果写入 _digest
                int result = EVP_DigestSignFinal(_ctx, _digest.data(), &length);
                ASSERT(result == 1);
                ASSERT(length == DIGEST_LENGTH);
            }

            /**
             * @brief 获取 HMAC 摘要结果
             * @return 摘要值的常量引用
             *
             * 返回 Finalize() 计算得到的 HMAC 摘要。
             * 必须在 Finalize() 之后调用。
             */
            Digest const& GetDigest() const { return _digest; }

        private:
            /** @brief OpenSSL 摘要上下文指针，管理 HMAC 计算状态 */
            EVP_MD_CTX* _ctx;

            /** @brief OpenSSL 密钥对象指针，存储 HMAC 密钥 */
            EVP_PKEY* _key;

            /** @brief 存储 HMAC 摘要结果的字节数组，由 Finalize() 填充 */
            Digest _digest = { };
    };
}

namespace Trinity::Crypto
{
    /**
     * @brief HMAC-SHA1 类型别名
     *
     * 使用 GenericHMAC 模板实例化 HMAC-SHA1 算法。
     * 产生 20 字节（160 位）的 HMAC 摘要。
     *
     * 使用场景：
     * - 游戏协议中的消息认证（魔兽世界协议）
     * - SRP6 认证协议
     * - TOTP（时间基一次性密码）生成
     * - 密钥派生
     *
     * 使用示例：
     * @code
     * // 一次性计算
     * auto mac = HMAC_SHA1::GetDigestOf(secretKey, "message");
     *
     * // 流式计算
     * HMAC_SHA1 hmac(secretKey);
     * hmac.UpdateData("part1");
     * hmac.UpdateData("part2");
     * hmac.Finalize();
     * auto result = hmac.GetDigest();
     * @endcode
     */
    using HMAC_SHA1 = Trinity::Impl::GenericHMAC<EVP_sha1, Constants::SHA1_DIGEST_LENGTH_BYTES>;

    /**
     * @brief HMAC-SHA256 类型别名
     *
     * 使用 GenericHMAC 模板实例化 HMAC-SHA256 算法。
     * 产生 32 字节（256 位）的 HMAC 摘要。
     *
     * 使用场景：
     * - 高安全性要求的消息认证
     * - 现代认证协议
     * - 密钥派生函数（如 HKDF）
     * - JWT（JSON Web Token）签名
     *
     * 安全性：HMAC-SHA256 是目前推荐使用的算法，具有较高的安全性。
     */
    using HMAC_SHA256 = Trinity::Impl::GenericHMAC<EVP_sha256, Constants::SHA256_DIGEST_LENGTH_BYTES>;
}
#endif
