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
 * @file SRP6.cpp
 * @brief SRP6安全远程密码协议实现文件
 *
 * 模块职责：
 *   实现SRP6协议的服务器端逻辑，包括：
 *   - 用户注册数据生成（盐值和验证器）
 *   - 认证挑战构建（服务器临时公钥B）
 *   - 客户端响应验证（验证证明消息M1）
 *   - 会话密钥派生（从临时密钥S派生会话密钥K）
 *
 * SRP6协议简介：
 *   SRP6（Secure Remote Password protocol version 6）是一种安全的远程密码认证协议。
 *   它允许客户端向服务器证明自己知道密码，而不需要传输密码或其等价信息。
 *   这是一种零知识证明协议，提供双向认证和前向保密。
 *
 * 协议安全性：
 *   - 离散对数问题：攻击者无法从公钥推导私钥
 *   - 零知识证明：服务器验证客户端知道密码，但不获取密码信息
 *   - 前向保密：每次会话使用不同的临时密钥，即使长期密钥泄露，历史会话仍然安全
 *   - 抗重放攻击：每次认证使用随机挑战值
 */

#include "SRP6.h"
#include "CryptoRandom.h"
#include "Util.h"
#include <algorithm>
#include <functional>

using SHA1 = Trinity::Crypto::SHA1;
using SRP6 = Trinity::Crypto::SRP6;

/**
 * SRP6 协议常量定义
 * g: 生成器（generator），值为7
 * N: 大素数（modulus），256位，用于模运算
 */
/*static*/ std::array<uint8, 1> const SRP6::g = { 7 };
/*static*/ std::array<uint8, 32> const SRP6::N = HexStrToByteArray<32>("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", true);
/*static*/ BigNumber const SRP6::_g(SRP6::g);
/*static*/ BigNumber const SRP6::_N(N);

/**
 * @brief 生成用户注册数据（盐值和验证器）
 *
 * 职责：
 *   为新用户注册生成必要的SRP6验证数据，包括随机盐值和对应的验证器。
 *   这些数据将存储在服务器端用于后续的认证过程。
 *
 * 参数：
 *   @param username - 用户名，用于计算验证器
 *   @param password - 用户密码，用于计算验证器
 *
 * 返回值：
 *   @return std::pair<Salt, Verifier> - 包含盐值和验证器的键值对
 *            Salt: 32字节的随机盐值
 *            Verifier: 32字节的验证器
 *
 * 主要流程：
 *   1. 生成32字节的随机盐值
 *   2. 使用用户名、密码和盐值计算验证器
 *   3. 返回盐值和验证器对
 */
/*static*/ std::pair<SRP6::Salt, SRP6::Verifier> SRP6::MakeRegistrationData(std::string const& username, std::string const& password)
{
    std::pair<SRP6::Salt, SRP6::Verifier> res;
    Crypto::GetRandomBytes(res.first); // random salt - 生成随机盐值
    res.second = CalculateVerifier(username, password, res.first);
    return res;
}

/**
 * @brief 计算SRP6验证器
 *
 * 职责：
 *   根据用户名、密码和盐值计算SRP6协议的验证器(v)。
 *   验证器存储在服务器端，用于后续的认证挑战响应验证。
 *
 * 参数：
 *   @param username - 用户名
 *   @param password - 用户密码
 *   @param salt - 盐值（32字节）
 *
 * 返回值：
 *   @return Verifier - 32字节的验证器
 *
 * 主要流程：
 *   1. 计算内部哈希：H(username || ':' || password)
 *   2. 计算外部哈希：H(salt || 内部哈希)
 *   3. 计算验证器：v = g ^ 外部哈希 mod N
 *   4. 返回32字节的验证器字节数组
 *
 * 数学公式：
 *   x = H(s || H(u || ':' || p))
 *   v = g^x mod N
 */
/*static*/ SRP6::Verifier SRP6::CalculateVerifier(std::string const& username, std::string const& password, SRP6::Salt const& salt)
{
    // v = g ^ H(s || H(u || ':' || p)) mod N
    return _g.ModExp(
        SHA1::GetDigestOf(
            salt,
            SHA1::GetDigestOf(username, ":", password)
        )
    ,_N).ToByteArray<32>();
}

/**
 * @brief SHA1交错哈希算法生成会话密钥
 *
 * 职责：
 *   将临时密钥S通过SHA1交错算法转换为最终的会话密钥K。
 *   这是一种密钥派生方法，将32字节的临时密钥扩展为40字节的会话密钥。
 *
 * 参数：
 *   @param S - 临时密钥（EphemeralKey，32字节），由服务器和客户端协商生成
 *
 * 返回值：
 *   @return SessionKey - 会话密钥（40字节），用于后续通信加密
 *
 * 主要流程：
 *   1. 将S分解为两个缓冲区buf0和buf1
 *      - buf0: 偶数索引字节 (S[0], S[2], S[4], ...)
 *      - buf1: 奇数索引字节 (S[1], S[3], S[5], ...)
 *   2. 找到第一个非零字节的位置p
 *   3. 如果p是奇数，跳过一个额外字节确保对齐
 *   4. 分别对两个缓冲区从位置p开始进行SHA1哈希
 *   5. 将两个哈希结果交错组合：
 *      - K[0] = hash0[0], K[1] = hash1[0], K[2] = hash0[1], K[3] = hash1[1], ...
 *   6. 返回40字节的会话密钥K
 *
 * 注意：
 *   - 这种交错哈希方法增加了密钥派生的复杂性，提高安全性
 *   - 跳过前导零字节是为了消除可能的偏差
 */
/*static*/ SessionKey SRP6::SHA1Interleave(SRP6::EphemeralKey const& S)
{
    // split S into two buffers - 将S分解为两个缓冲区
    std::array<uint8, EPHEMERAL_KEY_LENGTH/2> buf0, buf1;
    for (size_t i = 0; i < EPHEMERAL_KEY_LENGTH/2; ++i)
    {
        buf0[i] = S[2 * i + 0];  // 偶数索引字节
        buf1[i] = S[2 * i + 1];  // 奇数索引字节
    }

    // find position of first nonzero byte - 查找第一个非零字节的位置
    size_t p = 0;
    while (p < EPHEMERAL_KEY_LENGTH && !S[p]) ++p;
    if (p & 1) ++p; // skip one extra byte if p is odd - 如果p是奇数，跳过额外一个字节
    p /= 2; // offset into buffers - 转换为缓冲区内的偏移量

    // hash each of the halves, starting at the first nonzero byte - 分别对两个半部分进行哈希
    SHA1::Digest const hash0 = SHA1::GetDigestOf(buf0.data() + p, EPHEMERAL_KEY_LENGTH/2 - p);
    SHA1::Digest const hash1 = SHA1::GetDigestOf(buf1.data() + p, EPHEMERAL_KEY_LENGTH/2 - p);

    // stick the two hashes back together - 将两个哈希交错组合
    SessionKey K;
    for (size_t i = 0; i < SHA1::DIGEST_LENGTH; ++i)
    {
        K[2 * i + 0] = hash0[i];  // 来自hash0的字节
        K[2 * i + 1] = hash1[i];  // 来自hash1的字节
    }
    return K;
}

/**
 * @brief SRP6服务器端会话构造函数
 *
 * 职责：
 *   初始化SRP6服务器端认证会话对象，生成服务器端的临时私钥和公钥。
 *   该对象将用于验证客户端的挑战响应。
 *
 * 参数：
 *   @param username - 用户名，用于计算身份标识
 *   @param salt - 用户注册时生成的盐值（32字节）
 *   @param verifier - 用户注册时生成的验证器（32字节）
 *
 * 主要流程：
 *   1. 计算用户名哈希作为身份标识 _I = H(username)
 *   2. 生成服务器端临时私钥 _b（32字节随机数）
 *   3. 存储验证器 _v
 *   4. 存储盐值 s
 *   5. 计算服务器端公钥 B = (k*v + g^b) mod N
 *      其中 k = 3（在头文件中定义的常数）
 *
 * 注意：
 *   - B是服务器的公开值，将发送给客户端
 *   - _b是服务器的私密值，必须保密
 */
SRP6::SRP6(std::string const& username, Salt const& salt, Verifier const& verifier)
    : _I(SHA1::GetDigestOf(username)), _b(Crypto::GetRandomBytes<32>()), _v(verifier), s(salt), B(_B(_b, _v)) {}

/**
 * @brief 验证客户端的挑战响应
 *
 * 职责：
 *   验证客户端发送的挑战响应，如果验证成功则返回会话密钥。
 *   这是SRP6认证协议的核心验证步骤，确保客户端拥有正确的密码而不传输密码本身。
 *
 * 参数：
 *   @param A - 客户端临时公钥（32字节），由客户端生成并发送
 *   @param clientM - 客户端证明消息（20字节SHA1哈希），用于验证客户端身份
 *
 * 返回值：
 *   @return std::optional<SessionKey> - 如果验证成功，返回会话密钥（40字节）；
 *                                        如果验证失败，返回std::nullopt
 *
 * 主要流程：
 *   1. 安全检查：确保A mod N != 0，防止除零攻击
 *   2. 计算临时参数：u = H(A || B)
 *   3. 计算服务器端临时密钥：
 *      S = (A * v^u)^b mod N
 *      这确保服务器和客户端计算出相同的S值
 *   4. 派生会话密钥：K = SHA1Interleave(S)
 *   5. 计算NgHash = H(N) XOR H(g)，用于后续验证
 *   6. 计算服务器端证明消息：
 *      ourM = H(NgHash || I || s || A || B || K)
 *   7. 比较服务器端计算的证明消息与客户端发送的证明消息
 *   8. 如果匹配，返回会话密钥K；否则返回失败
 *
 * 安全机制：
 *   - 使用std::optional确保单次使用：每个SRP6对象只能验证一次
 *   - 验证A mod N != 0防止恶意输入
 *   - 证明消息M包含所有会话参数，防止中间人攻击
 *   - 会话密钥K仅在验证成功后才返回
 *
 * 数学原理：
 *   客户端计算: S = (B - k*v)^(a + u*x) mod N
 *   服务器计算: S = (A * v^u)^b mod N
 *   两边应该相等，因为：
 *   客户端: S = (g^b + k*v - k*v)^(a + u*x) = g^(b*(a + u*x))
 *   服务器: S = (g^a * g^x)^b = g^(b*(a + u*x))
 *   其中 x = H(s || H(u || ':' || p))
 */
std::optional<SessionKey> SRP6::VerifyChallengeResponse(EphemeralKey const& A, SHA1::Digest const& clientM)
{
    ASSERT(!_used, "A single SRP6 object must only ever be used to verify ONCE!");
    _used = true;  // 标记为已使用，防止重放攻击

    BigNumber const _A(A);
    // 安全检查：A mod N 不能为零，否则可能导致安全漏洞
    if ((_A % _N).IsZero())
        return std::nullopt;

    // 计算随机参数 u = H(A || B)
    BigNumber const u(SHA1::GetDigestOf(A, B));

    // 计算服务器端临时密钥 S = (A * v^u)^b mod N
    EphemeralKey const S = (_A * (_v.ModExp(u, _N))).ModExp(_b, N).ToByteArray<32>();

    // 使用SHA1交错算法从临时密钥派生会话密钥
    SessionKey K = SHA1Interleave(S);

    // NgHash = H(N) xor H(g) - 计算协议参数哈希的异或值
    SHA1::Digest const NHash = SHA1::GetDigestOf(N);
    SHA1::Digest const gHash = SHA1::GetDigestOf(g);
    SHA1::Digest NgHash;
    std::transform(NHash.begin(), NHash.end(), gHash.begin(), NgHash.begin(), std::bit_xor<>());

    // 计算服务器端证明消息 M = H(NgHash || I || s || A || B || K)
    SHA1::Digest const ourM = SHA1::GetDigestOf(NgHash, _I, s, A, B, K);

    // 验证客户端证明消息是否匹配
    if (ourM == clientM)
        return K;  // 验证成功，返回会话密钥
    else
        return std::nullopt;  // 验证失败
}
