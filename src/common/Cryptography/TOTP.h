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
 * @file TOTP.h
 * @brief TOTP（基于时间的一次性密码）算法实现
 *
 * 模块职责：
 *   本模块实现了 RFC 6238 规范定义的 TOTP（Time-based One-Time Password）算法，
 *   用于提供双因素认证（2FA）功能。TOTP 是基于 HOTP（HMAC-based One-Time Password）
 *   的扩展，使用当前时间作为动态因子生成一次性密码。
 *
 * 主要功能：
 *   1. 生成基于时间的一次性密码（TOTP）
 *   2. 验证用户提供的一次性密码是否有效
 *   3. 支持时间窗口容错机制（允许客户端与服务器时钟存在一定偏差）
 *
 * TOTP 算法原理：
 *   TOTP = HOTP(K, T)
 *   其中：
 *   - K：共享密钥（Secret），客户端和服务器共享
 *   - T：时间计数器 = floor((当前时间 - 起始时间) / 时间间隔)
 *   - 时间间隔通常为 30 秒（RFC 6238 推荐）
 *
 *   TOTP 算法流程：
 *   1. 计算时间计数器：将当前 Unix 时间戳除以时间间隔（如 30 秒）
 *   2. 将时间计数器编码为大端序字节数组
 *   3. 使用 HMAC-SHA1 算法计算密钥和时间计数器的哈希值
 *   4. 对哈希结果进行动态截断（Dynamic Truncation）
 *   5. 将截断结果转换为指定位数的数字（通常为 6 位）
 *
 * 应用场景：
 *   - 用户登录双因素认证
 *   - 敏感操作的二次验证
 *   - 替代传统的短信验证码
 *
 * 安全特性：
 *   - 基于时间动态生成，每 30 秒变化一次
 *   - 密钥仅存储在服务器和客户端，不通过网络传输
 *   - 一次性使用，有效期内使用后即失效
 *
 * 相关标准：
 *   RFC 6238: TOTP: Time-Based One-Time Password Algorithm
 *   RFC 4226: HOTP: An HMAC-Based One-Time Password Algorithm
 */

#ifndef TRINITY_TOTP_H
#define TRINITY_TOTP_H

#include "Define.h"
#include <ctime>
#include <vector>

namespace Trinity::Crypto
{
    /**
     * @struct TOTP
     * @brief TOTP（基于时间的一次性密码）工具类
     *
     * 该结构体提供 TOTP 令牌的生成和验证功能，所有方法均为静态方法，
     * 无需实例化即可使用。遵循 RFC 6238 规范实现。
     *
     * 使用示例：
     * @code
     * // 生成密钥
     * TOTP::Secret key = GenerateRandomSecret();
     *
     * // 生成当前时间的 TOTP 令牌
     * uint32 token = TOTP::GenerateToken(key, time(nullptr));
     *
     * // 验证用户提供的令牌
     * bool valid = TOTP::ValidateToken(key, userToken);
     * @endcode
     */
    struct TC_COMMON_API TOTP
    {
        /**
         * @brief 推荐的密钥长度（20 字节）
         *
         * 建议使用 20 字节（160 位）的密钥长度，这是 HMAC-SHA1 算法的最佳密钥长度。
         * 该长度既能保证安全性，又不会因密钥过长而影响性能。
         *
         * 注意：
         * - 密钥长度应至少为 HMAC 输出长度（20 字节）才能保证安全性
         * - 更长的密钥不会显著增加安全性（HMAC-SHA1 输出固定为 20 字节）
         * - 过短的密钥会降低安全性
         */
        static constexpr size_t RECOMMENDED_SECRET_LENGTH = 20;

        /**
         * @brief 密钥类型定义
         *
         * 使用字节向量存储共享密钥，便于处理变长密钥。
         * 推荐长度为 RECOMMENDED_SECRET_LENGTH（20 字节）。
         */
        using Secret = std::vector<uint8>;

        /**
         * @brief 生成 TOTP 令牌
         *
         * 根据给定的密钥和时间戳生成一个 6 位数字的 TOTP 令牌。
         * 该令牌在 30 秒的时间窗口内有效。
         *
         * @param key 共享密钥，用于 HMAC 计算
         *            长度建议为 RECOMMENDED_SECRET_LENGTH（20 字节）
         * @param timestamp Unix 时间戳（秒），指定生成令牌的时间点
         *                  通常传入当前时间 time(nullptr)
         *
         * @return uint32 返回 6 位数字的 TOTP 令牌（范围：000000-999999）
         *
         * @note 相同的密钥和相同的时间窗口（30秒内）将生成相同的令牌
         * @note 该方法不验证密钥的有效性，调用者需确保密钥长度足够
         *
         * @see ValidateToken() 验证令牌的有效性
         */
        static uint32 GenerateToken(Secret const& key, time_t timestamp);

        /**
         * @brief 验证 TOTP 令牌的有效性
         *
         * 验证用户提供的令牌是否有效。考虑到客户端与服务器可能存在时钟偏差，
         * 本方法允许前后一个时间窗口（共 90 秒）内的令牌通过验证。
         *
         * 时间窗口容错机制：
         * - 当前窗口：now（当前时间）
         * - 前一窗口：now - 30秒（允许客户端时钟慢 30 秒）
         * - 后一窗口：now + 30秒（允许客户端时钟快 30 秒）
         * - 只要匹配任意一个窗口的令牌，即视为有效
         *
         * @param key 共享密钥，用于 HMAC 计算
         *            必须与生成令牌时使用的密钥一致
         * @param token 用户提供的 6 位数字令牌
         *              范围：000000-999999
         *
         * @return bool 令牌有效返回 true，否则返回 false
         *
         * @note 验证成功后应立即使该令牌失效，防止重放攻击
         * @note 时钟偏差超过 30 秒将导致验证失败
         *
         * @see GenerateToken() 生成令牌
         */
        static bool ValidateToken(Secret const& key, uint32 token);
    };
}

#endif
