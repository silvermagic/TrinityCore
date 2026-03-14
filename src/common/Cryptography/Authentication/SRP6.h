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
 * @file SRP6.h
 * @brief SRP6（Secure Remote Password）协议实现头文件
 *
 * 模块职责：
 *   实现SRP6安全远程密码协议，提供安全的密码认证机制。
 *   SRP6是一种零知识证明协议，允许客户端向服务器证明自己知道密码，
 *   而不需要在网络中传输密码或密码的等价信息。
 *
 * 核心特性：
 *   - 安全性：密码不以任何形式存储或传输
 *   - 双向认证：客户端和服务器互相验证身份
 *   - 前向保密：每次会话生成不同的临时密钥
 *   - 抗重放攻击：每次认证使用随机挑战值
 *
 * 协议流程：
 *   1. 注册阶段：客户端生成盐值(s)和验证器(v)，发送给服务器存储
 *   2. 认证阶段：
 *      a. 客户端发送用户名，服务器返回盐值(s)和临时公钥(B)
 *      b. 客户端计算临时密钥(S)和证明消息(M1)，发送临时公钥(A)和M1
 *      c. 服务器验证M1，计算自己的临时密钥(S)和证明消息(M2)，返回M2
 *      d. 客户端验证M2，双方获得会话密钥(K)
 *
 * 数学基础：
 *   SRP6基于离散对数问题的困难性，使用大素数N和生成器g构建有限域。
 *   所有计算都在模N下进行，确保单向性。
 */

#ifndef TRINITY_SRP6_H
#define TRINITY_SRP6_H

#include "AuthDefines.h"
#include "BigNumber.h"
#include "Define.h"
#include "Common.h"
#include "CryptoHash.h"
#include <array>
#include <optional>

namespace Trinity::Crypto
{
    /**
     * @class SRP6
     * @brief SRP6安全远程密码协议实现类
     *
     * 职责：
     *   实现服务器端的SRP6协议逻辑，包括：
     *   - 用户注册数据生成（盐值和验证器）
     *   - 认证挑战生成（服务器临时公钥B）
     *   - 客户端响应验证（验证证明消息M1）
     *   - 会话密钥派生（从临时密钥S派生K）
     *
     * 使用生命周期：
     *   注册阶段（静态方法）：
     *     - 调用MakeRegistrationData生成盐值和验证器
     *     - 将盐值和验证器存储到数据库
     *
     *   认证阶段（实例方法）：
     *     - 创建SRP6实例，传入用户名、盐值、验证器
     *     - 将实例的公钥B发送给客户端
     *     - 接收客户端的A和M1
     *     - 调用VerifyChallengeResponse验证并获取会话密钥
     *     - 如果验证成功，使用会话密钥初始化AuthCrypt
     *
     * 线程安全性：
     *   非线程安全。每个认证会话应创建独立的SRP6实例。
     *
     * 安全注意事项：
     *   - 每个SRP6实例只能使用一次（VerifyChallengeResponse只能调用一次）
     *   - 用户名和密码必须先用Utf8ToUpperOnlyLatin规范化
     *   - 临时私钥b必须保密，不能泄露
     */
    class TC_COMMON_API SRP6
    {
        public:
            /**
             * @brief 盐值长度（字节）
             *
             * 盐值长度为32字节（256位），提供足够的随机性防止彩虹表攻击。
             */
            static constexpr size_t SALT_LENGTH = 32;

            /**
             * @brief 盐值类型定义
             *
             * 随机生成的用户密码盐值，在用户注册时生成并存储。
             * 用于计算密码验证器v = g^H(s || H(username || ':' || password)) mod N
             */
            using Salt = std::array<uint8, SALT_LENGTH>;

            /**
             * @brief 验证器长度（字节）
             *
             * 验证器长度为32字节（256位），与模数N的长度一致。
             */
            static constexpr size_t VERIFIER_LENGTH = 32;

            /**
             * @brief 验证器类型定义
             *
             * 用户密码验证器，在用户注册时生成并存储。
             * 验证器v = g^x mod N，其中x = H(s || H(username || ':' || password))
             * 验证器等价于密码，但无法反推出密码，是SRP协议的核心安全机制。
             */
            using Verifier = std::array<uint8, VERIFIER_LENGTH>;

            /**
             * @brief 临时密钥长度（字节）
             *
             * 临时密钥长度为32字节（256位），由双方临时私钥协商生成。
             */
            static constexpr size_t EPHEMERAL_KEY_LENGTH = 32;

            /**
             * @brief 临时密钥类型定义
             *
             * 用于存储临时公钥和临时协商密钥。
             * 客户端临时公钥A、服务器临时公钥B、协商出的临时密钥S都是该类型。
             */
            using EphemeralKey = std::array<uint8, EPHEMERAL_KEY_LENGTH>;

            /**
             * @brief 生成器常量g
             *
             * 值为7，是有限域Z_N^*的生成器。
             * 用于计算验证器v和临时公钥B。
             */
            static std::array<uint8, 1> const g;

            /**
             * @brief 模数常量N
             *
             * 256位大素数，定义了SRP6协议的有限域。
             * 所有运算都在模N下进行。
             */
            static std::array<uint8, 32> const N;

            /**
             * @brief 生成用户注册数据
             *
             * @param username - 用户名（必须先通过Utf8ToUpperOnlyLatin规范化）
             * @param password - 用户密码（必须先通过Utf8ToUpperOnlyLatin规范化）
             * @return std::pair<Salt, Verifier> - 盐值和验证器对
             *
             * 为新用户生成注册所需的盐值和验证器。
             * 盐值随机生成，验证器根据用户名、密码和盐值计算。
             *
             * 调用时机：
             *   用户注册或修改密码时调用。
             *
             * 性能注意事项：
             *   - 执行两次SHA1哈希计算
             *   - 执行一次大数模幂运算
             *   - 执行一次随机数生成
             *   - 总时间约几毫秒
             *
             * 返回值说明：
             *   - Salt: 32字节随机值，需要存储到数据库
             *   - Verifier: 32字节验证器，需要存储到数据库
             *   - 验证器相当于加密的密码，但无法解密回原密码
             *
             * 安全注意事项：
             *   - 用户名和密码必须先转为大写拉丁字符
             *   - 盐值必须使用加密安全的随机数生成器
             *   - 验证器必须安全存储，泄露等同于密码泄露
             */
            // username + password must be passed through Utf8ToUpperOnlyLatin FIRST!
            static std::pair<Salt, Verifier> MakeRegistrationData(std::string const& username, std::string const& password);

            /**
             * @brief 验证用户登录凭据
             *
             * @param username - 用户名（必须先通过Utf8ToUpperOnlyLatin规范化）
             * @param password - 用户密码（必须先通过Utf8ToUpperOnlyLatin规范化）
             * @param salt - 用户注册时生成的盐值
             * @param verifier - 用户注册时生成的验证器
             * @return bool - 如果凭据正确返回true，否则返回false
             *
             * 验证用户提供的用户名和密码是否匹配存储的验证器。
             * 通常用于管理员验证或测试，不用于正常的SRP6认证流程。
             *
             * 调用时机：
             *   - 管理员工具验证用户密码
             *   - 测试或调试目的
             *   - 非SRP6协议的密码验证场景
             *
             * 性能注意事项：
             *   - 执行两次SHA1哈希计算
             *   - 执行一次大数模幂运算
             *   - 总时间约几毫秒
             *
             * 安全注意事项：
             *   不要在正常的SRP6认证流程中使用此方法
             *   正常认证应该使用SRP6实例的VerifyChallengeResponse方法
             */
            // username + password must be passed through Utf8ToUpperOnlyLatin FIRST!
            static bool CheckLogin(std::string const& username, std::string const& password, Salt const& salt, Verifier const& verifier)
            {
                return (verifier == CalculateVerifier(username, password, salt));
            }

            /**
             * @brief 生成会话验证器
             *
             * @param A - 客户端临时公钥
             * @param clientM - 客户端证明消息
             * @param K - 会话密钥
             * @return SHA1::Digest - 会话验证器（20字节）
             *
             * 生成服务器发送给客户端的证明消息M2，用于双向认证。
             * 客户端验证M2后确信服务器拥有正确的验证器。
             *
             * 调用时机：
             *   VerifyChallengeResponse成功后，发送M2给客户端。
             *
             * 计算公式：
             *   M2 = H(A || M1 || K)
             *
             * 安全机制：
             *   包含A、M1和K，确保证明消息与会话绑定，防止重放攻击。
             */
            static SHA1::Digest GetSessionVerifier(EphemeralKey const& A, SHA1::Digest const& clientM, SessionKey const& K)
            {
                return SHA1::GetDigestOf(A, clientM, K);
            }

            /**
             * @brief SRP6服务器端会话构造函数
             *
             * @param username - 用户名
             * @param salt - 用户注册时生成的盐值（32字节）
             * @param verifier - 用户注册时生成的验证器（32字节）
             *
             * 初始化SRP6服务器端认证会话，生成服务器临时私钥和公钥。
             *
             * 调用时机：
             *   收到客户端的认证请求（CMSG_AUTH_SESSION）时调用。
             *
             * 主要操作：
             *   1. 计算用户名哈希作为身份标识_I
             *   2. 生成服务器临时私钥_b（32字节随机数）
             *   3. 存储验证器_v和盐值s
             *   4. 计算服务器临时公钥B = 3*v + g^b mod N
             *
             * 性能注意事项：
             *   - 执行一次SHA1哈希计算
             *   - 执行一次随机数生成（32字节）
             *   - 执行一次大数模幂运算
             *   - 总时间约几毫秒
             *
             * 安全注意事项：
             *   - 临时私钥_b必须保密，不能发送给客户端
             *   - 公钥B将在构造后发送给客户端
             *   - 每次认证都应创建新的SRP6实例
             */
            SRP6(std::string const& username, Salt const& salt, Verifier const& verifier);

            /**
             * @brief 验证客户端的挑战响应
             *
             * @param A - 客户端临时公钥（32字节）
             * @param clientM - 客户端证明消息M1（20字节SHA1哈希）
             * @return std::optional<SessionKey> - 验证成功返回会话密钥，失败返回std::nullopt
             *
             * 这是SRP6认证的核心验证步骤，验证客户端身份并生成会话密钥。
             *
             * 调用时机：
             *   收到客户端的认证响应（包含A和M1）时调用。
             *   只能调用一次，重复调用会触发断言失败。
             *
             * 主要流程：
             *   1. 安全检查：A mod N != 0
             *   2. 计算参数u = H(A || B)
             *   3. 计算临时密钥S = (A * v^u)^b mod N
             *   4. 派生会话密钥K = SHA1Interleave(S)
             *   5. 计算服务器证明消息M = H(H(N) xor H(g) || I || s || A || B || K)
             *   6. 比较M与clientM
             *   7. 如果匹配返回K，否则返回失败
             *
             * 性能注意事项：
             *   - 执行多次SHA1哈希计算（约6次）
             *   - 执行两次大数模幂运算
             *   - 总时间约几十毫秒
             *
             * 返回值说明：
             *   - 成功：返回40字节的会话密钥，用于初始化AuthCrypt
             *   - 失败：返回std::nullopt，表示认证失败或数据错误
             *
             * 安全机制：
             *   - 单次使用标志防止重放攻击
             *   - 证明消息包含所有会话参数防止篡改
             *   - 零知识证明确保密码安全
             */
            std::optional<SessionKey> VerifyChallengeResponse(EphemeralKey const& A, SHA1::Digest const& clientM);

        private:
            /**
             * @brief 单次使用标志
             *
             * 标记该SRP6实例是否已经被使用过。
             * 每个实例只能验证一次挑战响应，防止重放攻击。
             */
            bool _used = false; // a single instance can only be used to verify once

            /**
             * @brief 计算密码验证器
             *
             * @param username - 用户名
             * @param password - 用户密码
             * @param salt - 盐值
             * @return Verifier - 验证器（32字节）
             *
             * 计算公式：v = g^x mod N，其中x = H(s || H(username || ':' || password))
             *
             * 这是一个静态私有方法，被MakeRegistrationData和CheckLogin调用。
             */
            static Verifier CalculateVerifier(std::string const& username, std::string const& password, Salt const& salt);

            /**
             * @brief SHA1交错哈希算法
             *
             * @param S - 临时密钥（32字节）
             * @return SessionKey - 会话密钥（40字节）
             *
             * 将32字节的临时密钥S扩展为40字节的会话密钥K。
             * 通过分离偶数位和奇数位字节，分别哈希后交错组合实现。
             *
             * 这是一个静态私有方法，被VerifyChallengeResponse调用。
             */
            static SessionKey SHA1Interleave(EphemeralKey const& S);

            /* global algorithm parameters */
            /**
             * @brief 生成器BigNumber对象
             *
             * 值为7的BigNumber表示，用于模幂运算。
             * 这是全局算法参数，由常量g构造。
             */
            static BigNumber const _g; // a [g]enerator for the ring of integers mod N, algorithm parameter

            /**
             * @brief 模数BigNumber对象
             *
             * 256位大素数的BigNumber表示，所有运算都在模_N下进行。
             * 这是全局算法参数，由常量N构造。
             */
            static BigNumber const _N; // the modulus, an algorithm parameter; all operations are mod this

            /**
             * @brief 计算服务器临时公钥B
             *
             * @param b - 服务器临时私钥
             * @param v - 密码验证器
             * @return EphemeralKey - 服务器临时公钥B（32字节）
             *
             * 计算公式：B = (k*v + g^b) mod N，其中k = 3
             *
             * 这是一个静态私有辅助方法，在构造函数中调用。
             */
            static EphemeralKey _B(BigNumber const& b, BigNumber const& v) { return ((_g.ModExp(b,_N) + (v * 3)) % N).ToByteArray<EPHEMERAL_KEY_LENGTH>(); }

            /* per-instantiation parameters, set on construction */
            /**
             * @brief 用户名哈希
             *
             * 用户名的SHA1哈希值，用作身份标识。
             * _I = H(username)
             */
            SHA1::Digest const _I; // H(I) - the username, all uppercase

            /**
             * @brief 服务器临时私钥
             *
             * 服务器生成的32字节随机数，必须保密。
             * 用于计算服务器临时公钥B和临时密钥S。
             * 永远不会发送给客户端。
             */
            BigNumber const _b; // b - randomly chosen by the server, 19 bytes, never given out

            /**
             * @brief 密码验证器
             *
             * 从数据库加载的用户验证器，在用户注册时生成。
             * v = g^x mod N，其中x = H(s || H(username || ':' || password))
             */
            BigNumber const _v; // v - the user's password verifier, derived from s + H(USERNAME || ":" || PASSWORD)

        public:
            /**
             * @brief 用户密码盐值（公开）
             *
             * 用户注册时生成的随机盐值，从数据库加载。
             * 在认证挑战阶段发送给客户端。
             */
            Salt const s; // s - the user's password salt, random, used to calculate v on registration

            /**
             * @brief 服务器临时公钥（公开）
             *
             * 服务器生成的临时公钥，B = 3*v + g^b mod N。
             * 在认证挑战阶段发送给客户端。
             */
            EphemeralKey const B; // B = 3v + g^b
    };
}

#endif
