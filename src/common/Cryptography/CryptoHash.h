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
 * @file CryptoHash.h
 * @brief 加密哈希算法通用模板实现模块
 *
 * 本文件提供了基于 OpenSSL EVP 接口的通用哈希算法模板实现。
 * 通过模板参数化，支持 MD5、SHA-1、SHA-256 等多种哈希算法。
 *
 * 主要功能：
 * - 提供通用哈希计算接口 GenericHash
 * - 支持流式数据更新（UpdateData）
 * - 支持一次性摘要计算（GetDigestOf）
 * - 支持 RAII 资源管理，自动释放 OpenSSL 上下文
 *
 * 使用场景：
 * - 密码哈希存储
 * - 数据完整性校验
 * - 数字签名
 * - 认证协议（如 SRP6）
 *
 * 性能考虑：
 * - OpenSSL EVP 接口经过高度优化，适合生产环境
 * - 避免频繁创建/销毁哈希对象，尽量复用
 * - 大数据处理时可分块调用 UpdateData
 */

#ifndef TRINITY_CRYPTOHASH_H
#define TRINITY_CRYPTOHASH_H

#include "CryptoConstants.h"
#include "Define.h"
#include "Errors.h"
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <openssl/evp.h>

class BigNumber;

namespace Trinity::Impl
{
    /**
     * @brief 通用哈希实现辅助结构体
     *
     * 提供 OpenSSL EVP 接口的封装，用于管理哈希上下文的生命周期。
     * EVP（Envelope）是 OpenSSL 提供的高级密码学接口，支持多种算法的统一调用方式。
     */
    struct GenericHashImpl
    {
        /** @brief 哈希算法创建函数指针类型，返回 OpenSSL EVP_MD 结构体指针 */
        typedef EVP_MD const* (*HashCreator)();

        /**
         * @brief 创建新的哈希上下文
         * @return 新分配的 EVP_MD_CTX 指针
         *
         * 调用 OpenSSL 函数创建并初始化一个新的消息摘要上下文。
         * 此函数不会抛出异常，适合在构造函数中使用。
         */
        static EVP_MD_CTX* MakeCTX() noexcept { return EVP_MD_CTX_new(); }

        /**
         * @brief 销毁哈希上下文
         * @param ctx 要销毁的 EVP_MD_CTX 指针
         *
         * 释放 OpenSSL 哈希上下文占用的所有资源。
         * 调用后 ctx 指针不再有效。
         */
        static void DestroyCTX(EVP_MD_CTX* ctx) { EVP_MD_CTX_free(ctx); }
    };

    /**
     * @brief 通用哈希算法模板类
     *
     * 基于 OpenSSL EVP 接口实现的通用哈希计算器。通过模板参数支持不同的哈希算法。
     * 提供流式哈希计算接口，支持多次更新数据后一次性获取摘要。
     *
     * @tparam HashCreator 哈希算法创建函数指针，如 EVP_sha1、EVP_sha256 等
     * @tparam DigestLength 摘要长度（字节数）
     *
     * 使用示例：
     * @code
     * SHA1 hash;
     * hash.UpdateData("hello");
     * hash.UpdateData("world");
     * hash.Finalize();
     * auto digest = hash.GetDigest();
     * @endcode
     *
     * 或者使用静态方法一次性计算：
     * @code
     * auto digest = SHA1::GetDigestOf("hello", "world");
     * @endcode
     *
     * 线程安全：非线程安全，每个线程应使用独立的实例
     */
    template <GenericHashImpl::HashCreator HashCreator, size_t DigestLength>
    class GenericHash
    {
        public:
            /** @brief 摘要长度常量，用于模板元编程和数组声明 */
            static constexpr size_t DIGEST_LENGTH = DigestLength;

            /** @brief 摘要类型，固定大小的字节数组 */
            using Digest = std::array<uint8, DIGEST_LENGTH>;

            /**
             * @brief 计算单个数据块的哈希摘要（静态便捷方法）
             *
             * 一次性完成初始化、更新、最终化操作，返回哈希摘要。
             * 适合简单场景，避免手动管理哈希对象生命周期。
             *
             * @param data 指向数据缓冲区的指针
             * @param len 数据长度（字节数）
             * @return 哈希摘要值
             *
             * 调用时机：当需要快速计算一段连续内存的哈希值时使用
             *
             * 性能注意：每次调用都会创建和销毁内部上下文，
             *           对于大量数据分块处理，建议使用实例方法
             */
            static Digest GetDigestOf(uint8 const* data, size_t len)
            {
                GenericHash hash;
                hash.UpdateData(data, len);
                hash.Finalize();
                return hash.GetDigest();
            }

            /**
             * @brief 计算多个数据块的哈希摘要（静态便捷方法）
             *
             * 支持可变参数模板，可以一次性传入多个数据块进行哈希计算。
             * 所有数据块会被顺序合并计算，产生单一摘要。
             *
             * @tparam Ts 可变参数类型，通过 SFINAE 排除整型参数
             * @param pack 多个数据块（支持字符串、容器、数组等）
             * @return 哈希摘要值
             *
             * 使用示例：
             * @code
             * auto digest = SHA1::GetDigestOf("prefix", dataBuffer, "suffix");
             * @endcode
             *
             * SFINAE 说明：使用 enable_if_t 排除整型参数，
             *              防止与上面的指针+长度版本产生歧义
             */
            template <typename... Ts>
            static auto GetDigestOf(Ts&&... pack) -> std::enable_if_t<!(std::is_integral_v<std::decay_t<Ts>> || ...), Digest>
            {
                GenericHash hash;
                // 使用折叠表达式依次更新每个数据块
                (hash.UpdateData(std::forward<Ts>(pack)), ...);
                hash.Finalize();
                return hash.GetDigest();
            }

            /**
             * @brief 默认构造函数
             *
             * 创建并初始化哈希计算上下文。
             * 构造函数内部调用 OpenSSL EVP_DigestInit_ex 初始化哈希状态。
             *
             * @throws 如果 OpenSSL 初始化失败，触发 ASSERT 断言
             *
             * 调用时机：需要开始新的哈希计算时创建实例
             */
            GenericHash() : _ctx(GenericHashImpl::MakeCTX())
            {
                // 初始化哈希上下文，使用模板参数指定的算法
                int result = EVP_DigestInit_ex(_ctx, HashCreator(), nullptr);
                ASSERT(result == 1);  // 确保初始化成功
            }

            /**
             * @brief 拷贝构造函数
             *
             * 创建新的哈希对象并复制另一个对象的状态。
             * 允许在哈希计算过程中创建快照。
             *
             * @param right 源哈希对象
             *
             * 使用场景：当需要在哈希计算过程中分支到多个不同的结果时
             */
            GenericHash(GenericHash const& right) : _ctx(GenericHashImpl::MakeCTX())
            {
                *this = right;
            }

            /**
             * @brief 移动构造函数
             *
             * 通过移动语义转移哈希对象的所有权。
             * 移动后源对象处于有效但未定义的状态。
             *
             * @param right 源哈希对象（右值引用）
             *
             * 性能优势：避免深拷贝，提高性能
             */
            GenericHash(GenericHash&& right) noexcept
            {
                *this = std::move(right);
            }

            /**
             * @brief 析构函数
             *
             * 释放 OpenSSL 哈希上下文资源。
             * 遵循 RAII 原则，确保资源正确释放。
             */
            ~GenericHash()
            {
                if (!_ctx)
                    return;
                GenericHashImpl::DestroyCTX(_ctx);
                _ctx = nullptr;
            }

            /**
             * @brief 拷贝赋值运算符
             *
             * 复制另一个哈希对象的完整状态（包括已更新的数据和摘要）。
             *
             * @param right 源哈希对象
             * @return 当前对象的引用
             *
             * @throws 如果 OpenSSL 拷贝失败，触发 ASSERT 断言
             */
            GenericHash& operator=(GenericHash const& right)
            {
                if (this == &right)
                    return *this;

                // 深拷贝 OpenSSL 上下文，包括当前的哈希状态
                int result = EVP_MD_CTX_copy_ex(_ctx, right._ctx);
                ASSERT(result == 1);
                _digest = right._digest;
                return *this;
            }

            /**
             * @brief 移动赋值运算符
             *
             * 通过移动语义转移哈希对象的所有权。
             * 移动后的源对象会被重置为新创建的空上下文。
             *
             * @param right 源哈希对象（右值引用）
             * @return 当前对象的引用
             */
            GenericHash& operator=(GenericHash&& right) noexcept
            {
                if (this == &right)
                    return *this;

                // 交换资源：当前对象获得 right 的上下文，
                // right 获得一个新的空上下文（确保其仍处于有效状态）
                _ctx = std::exchange(right._ctx, GenericHashImpl::MakeCTX());
                _digest = std::exchange(right._digest, Digest{});
                return *this;
            }

            /**
             * @brief 更新哈希数据（原始字节数组）
             *
             * 将数据添加到哈希计算中。可以多次调用以处理大数据或流式数据。
             *
             * @param data 指向数据缓冲区的指针
             * @param len 数据长度（字节数）
             *
             * @throws 如果 OpenSSL 更新失败，触发 ASSERT 断言
             *
             * 调用时机：在构造对象后、Finalize() 前调用
             * 可多次调用，数据会被顺序追加计算
             *
             * 性能注意：OpenSSL 内部有缓冲优化，
             *           大数据可分块调用而不会显著影响性能
             */
            void UpdateData(uint8 const* data, size_t len)
            {
                int result = EVP_DigestUpdate(_ctx, data, len);
                ASSERT(result == 1);
            }

            /**
             * @brief 更新哈希数据（字符串视图）
             * @param str 字符串视图
             *
             * 重载版本，接受 std::string_view 参数。
             * 将字符串的字节内容添加到哈希计算中。
             */
            void UpdateData(std::string_view str) { UpdateData(reinterpret_cast<uint8 const*>(str.data()), str.size()); }

            /**
             * @brief 更新哈希数据（std::string）
             * @param str 字符串引用
             *
             * 显式重载以避免使用容器模板版本。
             * std::string 有 data() 和 size() 方法，但此重载提供更好的类型安全。
             */
            void UpdateData(std::string const& str) { UpdateData(std::string_view(str)); } /* explicit overload to avoid using the container template */

            /**
             * @brief 更新哈希数据（C 风格字符串）
             * @param str 以 null 结尾的 C 字符串指针
             *
             * 显式重载以避免使用容器模板版本。
             * 注意：只计算到第一个 null 字符的内容。
             */
            void UpdateData(char const* str) { UpdateData(std::string_view(str)); } /* explicit overload to avoid using the container template */

            /**
             * @brief 更新哈希数据（通用容器）
             *
             * 模板版本，支持任何有 data() 和 size() 方法的容器。
             * 如 std::vector<uint8>、std::array 等。
             *
             * @tparam Container 容器类型
             * @param c 容器实例
             */
            template <typename Container>
            void UpdateData(Container const& c) { UpdateData(std::data(c), std::size(c)); }

            /**
             * @brief 完成哈希计算
             *
             * 完成哈希计算并存储结果。调用后不能再调用 UpdateData()。
             * 结果可通过 GetDigest() 获取。
             *
             * @throws 如果 OpenSSL 最终化失败或长度不匹配，触发 ASSERT 断言
             *
             * 调用时机：所有数据更新完成后调用一次
             * 调用后对象状态改变，不可再次更新数据
             */
            void Finalize()
            {
                uint32 length;
                // 完成哈希计算，将结果写入 _digest 缓冲区
                int result = EVP_DigestFinal_ex(_ctx, _digest.data(), &length);
                ASSERT(result == 1);
                // 验证返回的长度与预期一致（防止算法不匹配）
                ASSERT(length == DIGEST_LENGTH);
            }

            /**
             * @brief 获取哈希摘要结果
             * @return 摘要值的常量引用
             *
             * 返回 Finalize() 计算得到的哈希摘要。
             * 必须在 Finalize() 之后调用。
             *
             * 调用时机：Finalize() 调用后
             * 可多次调用，返回相同的摘要值
             */
            Digest const& GetDigest() const { return _digest; }

        private:
            /** @brief OpenSSL 哈希上下文指针，管理哈希计算状态 */
            EVP_MD_CTX* _ctx;

            /** @brief 存储最终哈希摘要的字节数组，由 Finalize() 填充 */
            Digest _digest = { };
    };
}

namespace Trinity::Crypto
{
    /**
     * @brief MD5 哈希算法类型别名
     *
     * 使用 GenericHash 模板实例化 MD5 算法。
     * 产生 16 字节（128 位）的哈希摘要。
     *
     * 安全警告：MD5 已被认为是不安全的哈希算法，
     *           存在碰撞攻击风险，不应用于安全敏感场景。
     *           仅建议用于非安全场景（如数据校验、缓存键等）。
     */
    using MD5 = Trinity::Impl::GenericHash<EVP_md5, Constants::MD5_DIGEST_LENGTH_BYTES>;

    /**
     * @brief SHA-1 哈希算法类型别名
     *
     * 使用 GenericHash 模板实例化 SHA-1 算法。
     * 产生 20 字节（160 位）的哈希摘要。
     *
     * 使用场景：
     * - 游戏协议中的数据校验（魔兽世界协议使用 SHA-1）
     * - 兼容旧系统的认证协议
     * - SRP6 安全远程密码协议
     *
     * 安全注意：SHA-1 已被认为不适合用于数字签名等高安全性场景，
     *           但在游戏协议兼容性需求下仍然使用。
     */
    using SHA1 = Trinity::Impl::GenericHash<EVP_sha1, Constants::SHA1_DIGEST_LENGTH_BYTES>;

    /**
     * @brief SHA-256 哈希算法类型别名
     *
     * 使用 GenericHash 模板实例化 SHA-256 算法。
     * 产生 32 字节（256 位）的哈希摘要。
     *
     * 使用场景：
     * - 安全敏感的密码存储
     * - 现代认证协议
     * - 数字签名和数据完整性校验
     *
     * 安全性：SHA-256 是目前推荐的哈希算法，具有较高的安全性。
     */
    using SHA256 = Trinity::Impl::GenericHash<EVP_sha256, Constants::SHA256_DIGEST_LENGTH_BYTES>;
}

#endif
