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
 * @file Argon2.cpp
 * @brief Argon2密码哈希算法的实现文件
 *
 * @details 本文件实现了Argon2id密码哈希算法的Hash和Verify功能,
 * 通过调用libargon2库函数提供安全的密码哈希服务。
 *
 * 实现细节:
 * - 使用Argon2id变体,平衡抗侧信道攻击和抗GPU攻击能力
 * - 哈希输出采用PHC标准编码格式,便于存储和传输
 * - 错误处理通过Optional返回值实现,避免异常开销
 */

#include "Argon2.h"
#include <argon2/argon2.h>

/**
 * @brief 使用Argon2id算法生成密码哈希
 *
 * @details 该实现调用libargon2库的argon2id_hash_encoded函数,
 * 生成符合PHC标准的编码哈希字符串。
 *
 * 实现流程:
 * 1. 将BigNumber类型的盐值转换为字节数组
 * 2. 调用argon2id_hash_encoded进行哈希计算
 * 3. 检查返回状态,成功则返回编码字符串,失败返回空Optional
 *
 * @param password 待哈希的明文密码
 * @param salt 盐值,通过BigNumber传入,支持任意长度
 * @param nIterations 迭代次数(时间成本)
 * @param kibMemoryCost 内存成本(单位:KiB)
 *
 * @return Optional<std::string> 成功返回编码哈希字符串,失败返回空Optional
 */
/*static*/ Optional<std::string> Trinity::Crypto::Argon2::Hash(std::string const& password, BigNumber const& salt, uint32 nIterations, uint32 kibMemoryCost)
{
    // 存储编码后的哈希字符串缓冲区
    char buf[ENCODED_HASH_LEN];

    // 将BigNumber类型的盐值转换为字节数组
    // 这一步确保盐值可以传递给argon2库函数
    std::vector<uint8> saltBytes = salt.ToByteVector();

    // 调用libargon2库的argon2id_hash_encoded函数
    // 参数说明:
    // - t_cost: 时间成本(迭代次数),增加此值会线性增加计算时间
    // - m_cost: 内存成本(单位:KiB),增加此值会增加内存消耗和GPU攻击难度
    // - parallelism: 并行度(线程数),当前固定为1
    // - pwd: 密码数据和长度
    // - salt: 盐值数据和长度
    // - hashlen: 输出哈希长度(字节)
    // - encoded: 输出缓冲区和长度
    int status = argon2id_hash_encoded(
        nIterations,        // 时间成本(迭代次数)
        kibMemoryCost,      // 内存成本(KiB)
        PARALLELISM,        // 并行度
        password.c_str(), password.length(),  // 密码数据和长度
        saltBytes.data(), saltBytes.size(),   // 盐值数据和长度
        HASH_LEN,           // 输出哈希长度(16字节)
        buf,                // 输出缓冲区
        ENCODED_HASH_LEN    // 缓冲区大小
    );

    // 检查哈希计算是否成功
    // ARGON2_OK表示成功,其他值表示各种错误(如内存不足、参数错误等)
    if (status == ARGON2_OK)
        return std::string(buf);

    // 失败时返回空Optional,调用者可检查Optional是否为空来判断成功与否
    return {};
}

/**
 * @brief 验证密码是否匹配给定的哈希值
 *
 * @details 该实现调用libargon2库的argon2id_verify函数,
 * 自动解析编码哈希字符串中的参数并验证密码。
 *
 * 实现流程:
 * 1. 调用argon2id_verify进行验证
 * 2. 库函数会自动解析哈希字符串,提取参数、盐值和目标哈希
 * 3. 使用相同参数对密码进行哈希并比较
 * 4. 返回验证结果
 *
 * @param password 待验证的明文密码
 * @param hash 编码哈希字符串
 *
 * @return bool 密码匹配返回true,否则返回false
 */
/*static*/ bool Trinity::Crypto::Argon2::Verify(std::string const& password, std::string const& hash)
{
    // 调用libargon2库的argon2id_verify函数
    // 该函数会:
    // 1. 解析hash字符串,提取算法参数、盐值和目标哈希值
    // 2. 使用相同的参数和盐值对password进行哈希
    // 3. 使用恒定时间比较算法比对生成的哈希与目标哈希
    // 4. 返回比较结果
    //
    // 参数说明:
    // - hash: 编码的哈希字符串(包含参数、盐值和哈希值)
    // - pwd: 待验证的密码数据和长度
    int status = argon2id_verify(hash.c_str(), password.c_str(), password.length());

    // 返回验证结果
    // ARGON2_OK表示密码匹配,其他值表示不匹配或哈希格式错误
    return (status == ARGON2_OK);
}
