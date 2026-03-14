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
 * @file TOTP.cpp
 * @brief TOTP（基于时间的一次性密码）算法实现
 *
 * 模块职责：
 *   本文件实现了 RFC 6238 规范定义的 TOTP（Time-based One-Time Password）算法，
 *   提供令牌生成和验证功能。TOTP 通过将当前时间作为计数器，基于 HMAC-SHA1 算法
 *   生成动态的一次性密码，广泛应用于双因素认证（2FA）场景。
 *
 * 主要功能：
 *   1. GenerateToken(): 根据密钥和时间戳生成 TOTP 令牌
 *   2. ValidateToken(): 验证用户提供的 TOTP 令牌是否有效
 *
 * TOTP 算法核心流程：
 *   1. 时间归一化：将时间戳除以时间间隔，得到时间计数器
 *   2. 挑战构造：将时间计数器编码为大端序字节数组
 *   3. HMAC 计算：使用密钥对挑战数据进行 HMAC-SHA1 运算
 *   4. 动态截断：从 HMAC 结果中提取 4 字节作为令牌基础
 *   5. 数字转换：将截断值转换为 6 位十进制数字
 *
 * 技术细节：
 *   - 使用 OpenSSL HMAC 库实现 HMAC-SHA1 算法
 *   - 时间间隔采用 RFC 6238 推荐的 30 秒
 *   - 动态截断算法遵循 RFC 4226 规范
 *   - 验证时允许 ±30 秒的时间容差
 *
 * 依赖项：
 *   - OpenSSL EVP 和 HMAC 库
 *   - C++ 标准库（ctime、vector）
 *
 * 参考文献：
 *   - RFC 6238: TOTP: Time-Based One-Time Password Algorithm
 *   - RFC 4226: HOTP: An HMAC-Based One-Time Password Algorithm
 */

#include "TOTP.h"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/hmac.h>

/**
 * @brief 静态常量定义
 */
constexpr std::size_t Trinity::Crypto::TOTP::RECOMMENDED_SECRET_LENGTH;

/**
 * @brief TOTP 时间间隔（秒）
 *
 * 定义 TOTP 令牌的有效时间窗口。RFC 6238 推荐使用 30 秒作为时间间隔，
 * 这在安全性和用户体验之间取得了良好平衡：
 * - 时间窗口足够长，用户有充足时间输入令牌
 * - 时间窗口足够短，令牌变化频繁，安全性高
 */
static constexpr uint32 TOTP_INTERVAL = 30;

/**
 * @brief HMAC-SHA1 运算结果大小（字节）
 *
 * SHA1 哈希算法的输出固定为 20 字节（160 位）。
 * HMAC-SHA1 的输出长度与底层哈希算法相同。
 */
static constexpr uint32 HMAC_RESULT_SIZE = 20;

/**
 * @brief 生成 TOTP（基于时间的一次性密码）令牌
 *
 * 职责：
 *   根据 RFC 6238 规范，使用 HMAC-SHA1 算法基于当前时间戳生成一次性密码。
 *   该算法广泛应用于双因素认证（2FA）系统，如 Google Authenticator、Authy 等。
 *
 * 算法原理：
 *   TOTP = Truncate(HMAC-SHA1(K, T)) mod 10^d
 *   其中：
 *   - K: 共享密钥（Secret）
 *   - T: 时间计数器 = floor(timestamp / 时间间隔)
 *   - d: 令牌位数（本实现为 6 位）
 *   - Truncate: 动态截断函数
 *
 * 参数：
 *   @param secret 共享密钥，用于 HMAC 计算
 *                 必须与客户端共享同一密钥，建议长度为 RECOMMENDED_SECRET_LENGTH（20 字节）
 *   @param timestamp Unix 时间戳（秒），指定生成令牌的时间点
 *                    通常传入 time(nullptr) 获取当前时间
 *
 * 返回值：
 *   @return uint32 返回 6 位数字的 TOTP 令牌（范围：000000-999999）
 *
 * 实现细节：
 *   步骤 1：时间戳归一化
 *     - 将时间戳除以时间间隔（30秒），得到时间计数器
 *     - 确保同一 30 秒窗口内的所有时间戳生成相同令牌
 *
 *   步骤 2：构造挑战数据
 *     - 将时间计数器编码为大端序的 8 字节数组
 *     - 大端序确保跨平台兼容性
 *
 *   步骤 3：计算 HMAC-SHA1
 *     - 使用密钥对挑战数据进行 HMAC-SHA1 运算
 *     - 生成 20 字节的哈希摘要
 *
 *   步骤 4：动态截断（Dynamic Truncation）
 *     - 从哈希结果的最后一个字节提取低 4 位作为偏移量（0-15）
 *     - 从偏移量位置开始，提取连续 4 个字节
 *     - 将 4 个字节组合成一个 32 位整数
 *
 *   步骤 5：生成数字令牌
 *     - 提取 32 位整数的低 31 位（确保非负）
 *     - 对 1000000 取模，得到 6 位十进制数字
 *
 * 示例：
 *   @code
 *   TOTP::Secret key = {...}; // 20 字节密钥
 *   uint32 token = TOTP::GenerateToken(key, time(nullptr));
 *   // token 可能是：123456
 *   @endcode
 *
 * 注意事项：
 *   - 相同密钥和相同时间窗口将生成相同令牌
 *   - 每 30 秒生成一个新令牌
 *   - 密钥必须保密，不得泄露
 *
 * @see ValidateToken() 验证令牌的有效性
 * @see RFC 6238 https://tools.ietf.org/html/rfc6238
 */
/*static*/ uint32 Trinity::Crypto::TOTP::GenerateToken(Secret const& secret, time_t timestamp)
{
    // ========================================================================
    // 步骤 1：时间戳归一化（Time Normalization）
    // ========================================================================
    // 将时间戳转换为时间计数器，每 30 秒一个计数周期
    // 例如：时间戳 100 -> 计数器 3，时间戳 115 -> 计数器 3（同一窗口）
    // 这确保了同一 30 秒窗口内的所有请求生成相同的令牌
    timestamp /= TOTP_INTERVAL;

    // ========================================================================
    // 步骤 2：构造挑战数据（Challenge Construction）
    // ========================================================================
    // 将时间计数器编码为大端序（Big-Endian）8 字节数组
    // 大端序保证了跨平台的一致性（不同 CPU 架构可能使用不同字节序）
    unsigned char challenge[8];
    for (int i = 8; i--; timestamp >>= 8)
        challenge[i] = timestamp;
    // 循环说明：
    // - i 从 7 到 0 递减
    // - 每次取 timestamp 的低 8 位存入 challenge[i]
    // - timestamp >>= 8 右移 8 位，准备取下一字节
    // 结果：challenge[0] 存储最高位字节，challenge[7] 存储最低位字节

    // ========================================================================
    // 步骤 3：计算 HMAC-SHA1（HMAC Calculation）
    // ========================================================================
    // 使用 OpenSSL HMAC 函数计算消息认证码
    // HMAC-SHA1 = H((K ^ opad) || H((K ^ ipad) || message))
    // 其中：
    // - K: 密钥（secret）
    // - message: 挑战数据（challenge）
    // - H: SHA1 哈希函数
    // - opad: 0x5C 重复填充
    // - ipad: 0x36 重复填充
    unsigned char digest[HMAC_RESULT_SIZE];  // 存储 HMAC 结果（20 字节）
    uint32 digestSize = HMAC_RESULT_SIZE;     // 输出缓冲区大小
    HMAC(EVP_sha1(),                          // 使用 SHA1 哈希算法
         secret.data(),                       // 密钥数据指针
         secret.size(),                       // 密钥长度
         challenge,                           // 挑战数据（时间计数器）
         8,                                   // 挑战数据长度（8 字节）
         digest,                              // 输出缓冲区
         &digestSize);                        // 输出长度指针

    // ========================================================================
    // 步骤 4：动态截断（Dynamic Truncation）
    // ========================================================================
    // 动态截断是 TOTP/HOTP 算法的核心步骤，用于从 20 字节的 HMAC 结果中
    // 提取一个较小的数值，同时保持良好的随机性分布

    // 4.1 提取动态偏移量
    // 使用 HMAC 结果的最后一个字节（索引 19）的低 4 位作为偏移量
    // offset 范围：0x0 到 0xF（0 到 15）
    uint32 offset = digest[19] & 0xF;

    // 4.2 提取 4 字节并组合成 32 位整数
    // 从 digest[offset] 开始，提取连续 4 个字节
    // 使用大端序（Big-Endian）方式组合：
    //   字节 0 左移 24 位 -> 高字节
    //   字节 1 左移 16 位
    //   字节 2 左移 8 位
    //   字节 3 不移位 -> 低字节
    uint32 truncated = (digest[offset] << 24) | (digest[offset + 1] << 16) | (digest[offset + 2] << 8) | (digest[offset + 3]);

    // ========================================================================
    // 步骤 5：生成数字令牌（Token Generation）
    // ========================================================================
    // 5.1 提取 31 位，确保结果为正数
    // 0x7FFFFFFF 的二进制形式：0111 1111 1111 1111 1111 1111 1111 1111
    // 这样可以清除最高位（符号位），避免生成负数
    truncated &= 0x7FFFFFFF;

    // 5.2 对 1000000 取模，得到 6 位十进制数字
    // 范围：000000 到 999999
    return (truncated % 1000000);
}

/**
 * @brief 验证 TOTP 令牌的有效性
 *
 * 职责：
 *   验证用户提供的令牌是否有效。考虑到客户端与服务器可能存在时钟偏差，
 *   本方法允许前后一个时间窗口（共 90 秒）内的令牌通过验证。
 *   这是一种常见的容错机制，平衡了安全性和用户体验。
 *
 * 时间窗口容错机制：
 *   由于客户端和服务器的时钟可能不完全同步，TOTP 验证通常采用多窗口验证策略：
 *   - 当前窗口：now（当前时间）
 *   - 前一窗口：now - 30秒（允许客户端时钟慢 30 秒）
 *   - 后一窗口：now + 30秒（允许客户端时钟快 30 秒）
 *
 *   时间线示例（假设当前服务器时间为 T）：
 *   [T-30s]--------[T]--------[T+30s]
 *      ^            ^            ^
 *   前一窗口     当前窗口     后一窗口
 *
 *   只要令牌匹配任意一个窗口，即视为有效。
 *   总容忍范围：±30 秒（共 90 秒的时间窗口）
 *
 * 参数：
 *   @param secret 共享密钥，用于 HMAC 计算
 *                 必须与生成令牌时使用的密钥一致
 *                 通常从数据库或配置中读取用户的密钥
 *   @param token 用户提供的 6 位数字令牌
 *                范围：000000-999999
 *                通常由用户从验证器应用中读取并输入
 *
 * 返回值：
 *   @return bool 令牌有效返回 true，否则返回 false
 *
 * 验证流程：
 *   1. 获取当前系统时间（服务器时间）
 *   2. 生成前一窗口的令牌并比较（now - 30s）
 *   3. 生成当前窗口的令牌并比较（now）
 *   4. 生成后一窗口的令牌并比较（now + 30s）
 *   5. 任意一个匹配即返回 true，否则返回 false
 *
 * 示例：
 *   @code
 *   TOTP::Secret key = user->GetTOTPSecret();
 *   uint32 userToken = GetUserInput(); // 用户输入的令牌
 *
 *   if (TOTP::ValidateToken(key, userToken))
 *   {
 *       // 验证成功，允许登录
 *       user->MarkTOTPUsed(userToken); // 防止重放攻击
 *   }
 *   else
 *   {
 *       // 验证失败，拒绝访问
 *       LogFailedAttempt();
 *   }
 *   @endcode
 *
 * 安全注意事项：
 *   - 验证成功后应立即使令牌失效，防止重放攻击（Replay Attack）
 *   - 可记录失败的验证尝试，防止暴力破解
 *   - 时钟偏差过大（>30秒）将导致验证失败，需同步时间
 *   - 密钥必须妥善保管，不得泄露
 *
 * 性能考虑：
 *   - 本方法最多调用 3 次 GenerateToken，开销较小
 *   - 如需更高性能，可考虑优化 HMAC 计算（如缓存）
 *
 * @see GenerateToken() 生成令牌
 * @see RFC 6238 Section 5.2: Validation of TOTP Values
 */
/*static*/ bool Trinity::Crypto::TOTP::ValidateToken(Secret const& secret, uint32 token)
{
    // 获取当前系统时间（Unix 时间戳，秒）
    time_t now = time(nullptr);

    // ========================================================================
    // 多窗口验证策略
    // ========================================================================
    // 按顺序检查三个时间窗口的令牌，只要匹配任意一个即返回 true
    // 这种设计允许 ±30 秒的时钟偏差，提高了用户体验

    return (
        // --------------------------------------------------------------------
        // 检查前一窗口（now - 30s）
        // --------------------------------------------------------------------
        // 场景：客户端时钟比服务器慢（落后）
        // 例如：客户端时间为 12:00:00，服务器时间为 12:00:25
        // 客户端生成的令牌对应 11:59:30-12:00:00 窗口
        // 服务器检查前一窗口（11:59:30-12:00:00）即可匹配
        (token == GenerateToken(secret, now - TOTP_INTERVAL)) ||

        // --------------------------------------------------------------------
        // 检查当前窗口（now）
        // --------------------------------------------------------------------
        // 场景：客户端时钟与服务器同步（理想情况）
        // 这是最常见的情况，客户端和服务器在同一时间窗口内
        (token == GenerateToken(secret, now)) ||

        // --------------------------------------------------------------------
        // 检查后一窗口（now + 30s）
        // --------------------------------------------------------------------
        // 场景：客户端时钟比服务器快（超前）
        // 例如：客户端时间为 12:00:35，服务器时间为 12:00:05
        // 客户端生成的令牌对应 12:00:30-12:01:00 窗口
        // 服务器检查后一窗口（12:00:30-12:01:00）即可匹配
        (token == GenerateToken(secret, now + TOTP_INTERVAL))
    );

    // 注意：使用逻辑或运算符 || 的短路特性，一旦匹配成功立即返回，不再计算后续窗口
    // 这可以提高性能，特别是在客户端时钟准确的情况下（当前窗口最先匹配）
}
