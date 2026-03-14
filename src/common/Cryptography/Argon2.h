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
 * @file Argon2.h
 * @brief Argon2密码哈希算法封装模块
 *
 * @details 本模块提供了Argon2密码哈希算法的封装实现,用于安全地存储和验证用户密码。
 *
 * Argon2算法原理:
 * Argon2是2015年密码哈希竞赛的获胜者,设计用于抵抗GPU和ASIC攻击。它是一种内存密集型
 * 密码哈希函数,通过大量内存使用来增加攻击成本。Argon2有三个变体:
 * - Argon2d: 数据依赖型内存访问,抗GPU攻击强,但有侧信道攻击风险
 * - Argon2i: 数据独立型内存访问,抗侧信道攻击强,但抗GPU攻击稍弱
 * - Argon2id: 混合模式,前半部分使用数据独立访问,后半部分使用数据依赖访问
 *
 * 本实现使用Argon2id变体,结合了两种模式的优势。
 *
 * 算法参数说明:
 * - 时间成本(Iterations): 哈希计算的迭代次数,增加计算时间
 * - 内存成本(Memory Cost): 哈希计算使用的内存量,增加内存消耗
 * - 并行度(Parallelism): 并行执行的线程数
 * - 盐值(Salt): 随机数据,防止彩虹表攻击
 * - 输出长度(Hash Length): 生成的哈希值字节长度
 *
 * 安全特性:
 * 1. 抗GPU/ASIC攻击: 大量内存需求使得并行攻击成本极高
 * 2. 抗侧信道攻击: 使用Argon2id变体,平衡安全性和性能
 * 3. 可配置参数: 可根据安全需求调整时间和内存成本
 * 4. 标准化实现: 使用参考实现库,确保算法正确性
 */

#ifndef TRINITY_ARGON2_H
#define TRINITY_ARGON2_H

#include "BigNumber.h"
#include "Define.h"
#include "Optional.h"
#include <string>

namespace Trinity::Crypto
{
    /**
     * @struct Argon2
     * @brief Argon2密码哈希算法的封装类
     *
     * @details 提供密码哈希生成和验证功能,使用Argon2id算法变体。
     * 所有方法均为静态方法,无需实例化即可使用。
     *
     * 使用示例:
     * @code
     * // 生成密码哈希
     * BigNumber salt;
     * salt.SetRand(16);  // 生成16字节随机盐值
     * Optional<std::string> hash = Argon2::Hash("password123", salt);
     * if (hash)
     *     std::cout << "Hash: " << *hash << std::endl;
     *
     * // 验证密码
     * bool valid = Argon2::Verify("password123", *hash);
     * @endcode
     */
    struct TC_COMMON_API Argon2
    {
        static constexpr uint32 HASH_LEN = 16;                    ///< 哈希输出长度,16字节(128位),足够用于密码存储
        static constexpr uint32 ENCODED_HASH_LEN = 100;           ///< 编码后的哈希字符串最大长度(字符数),包含参数、盐值和哈希值
        static constexpr uint32 DEFAULT_ITERATIONS = 10;          ///< 默认迭代次数(时间成本),值越大计算越慢但越安全
        static constexpr uint32 DEFAULT_MEMORY_COST = (1u << 17); ///< 默认内存成本,2^17 KiB = 128 MiB,平衡安全性和性能
        static constexpr uint32 PARALLELISM = 1;                  ///< 并行度(线程数),当前实现不支持多线程哈希

        /**
         * @brief 使用Argon2id算法生成密码哈希
         *
         * @details 该函数使用Argon2id算法对密码进行哈希处理,生成包含算法参数、盐值和哈希值的
         * 编码字符串。编码格式符合PHC(Password Hashing Competition)标准,格式为:
         * $argon2id$v=19$m=<memory>,t=<time>,p=<parallelism>$<salt>$<hash>
         *
         * @param password 待哈希的明文密码
         * @param salt 盐值,用于增加哈希的唯一性,防止彩虹表攻击。每个密码应使用唯一的随机盐值
         * @param nIterations 迭代次数(时间成本),默认为DEFAULT_ITERATIONS。增加此值会线性增加计算时间
         * @param kibMemoryCost 内存成本(单位:KiB),默认为DEFAULT_MEMORY_COST。增加此值会增加内存消耗,使GPU攻击更困难
         *
         * @return Optional<std::string> 成功返回包含编码哈希的字符串,失败返回空Optional
         *
         * @note 哈希生成的字符串可以直接存储到数据库中,用于后续的密码验证
         * @note 内存成本以KiB为单位,例如65536表示64 MiB
         *
         * @see Verify() 用于验证密码
         */
        static Optional<std::string> Hash(std::string const& password, BigNumber const& salt, uint32 nIterations = DEFAULT_ITERATIONS, uint32 kibMemoryCost = DEFAULT_MEMORY_COST);

        /**
         * @brief 验证密码是否匹配给定的哈希值
         *
         * @details 该函数使用Argon2id算法验证密码是否与之前生成的哈希值匹配。
         * 哈希字符串包含算法参数,因此验证时会自动使用正确的参数。
         *
         * 验证过程:
         * 1. 解析哈希字符串,提取算法参数、盐值和目标哈希值
         * 2. 使用相同参数对输入密码进行哈希
         * 3. 比较生成的哈希值与目标哈希值
         *
         * @param password 待验证的明文密码
         * @param hash 之前生成的编码哈希字符串(由Hash()函数返回)
         *
         * @return bool 密码匹配返回true,不匹配或哈希格式错误返回false
         *
         * @note 此函数是恒定时间比较,可防止计时攻击
         * @note 即使哈希格式错误,函数也会安全地返回false,不会抛出异常
         *
         * @see Hash() 用于生成密码哈希
         */
        static bool Verify(std::string const& password, std::string const& hash);
    };
}

#endif
