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
 * @file AES.cpp
 * @brief AES 加密模块实现文件 - 实现 AES-128-GCM 加密算法
 *
 * 模块职责:
 *   实现 AES-128-GCM 加密算法的核心逻辑,提供对称加密和解密功能
 *
 * 主要功能:
 *   - 初始化 OpenSSL EVP 加密上下文
 *   - 设置加密密钥和初始化向量
 *   - 执行加密/解密操作
 *   - 生成和验证 GCM 认证标签
 *
 * AES 加密原理:
 *   AES (Advanced Encryption Standard, 高级加密标准) 是对称分组加密算法:
 *   - 分组大小: 128 位 (16 字节)
 *   - 密钥长度: 本实现使用 128 位 (AES-128)
 *   - 加密轮数: 128 位密钥需要 10 轮变换
 *
 *   每轮变换包含 4 个步骤:
 *   1. SubBytes (字节替换): 使用 S-box 进行非线性替换
 *   2. ShiftRows (行移位): 对状态矩阵的行进行循环移位
 *   3. MixColumns (列混淆): 使用 Galois 域乘法混淆列数据
 *   4. AddRoundKey (轮密钥加): 将状态与轮密钥异或
 *
 * GCM (Galois/Counter Mode) 模式:
 *   - CTR 模式: 将分组密码转换为流密码,支持任意长度数据
 *   - GHASH: 在 Galois 域上进行认证计算,生成认证标签
 *   - 并行性: 加密和认证可并行处理,性能优异
 *   - 安全性: 提供机密性 (加密) 和完整性 (认证) 双重保障
 *
 * 加密流程:
 *   1. 构造加密器对象
 *   2. 初始化上下文 (EVP_CipherInit_ex)
 *   3. 设置密钥 (Init -> EVP_CipherInit_ex)
 *   4. 设置 IV (Process -> EVP_CipherInit_ex)
 *   5. 加密数据 (EVP_CipherUpdate + EVP_CipherFinal_ex)
 *   6. 生成认证标签 (EVP_CIPHER_CTX_ctrl)
 *
 * 解密流程:
 *   1. 构造解密器对象
 *   2. 初始化上下文
 *   3. 设置密钥
 *   4. 设置 IV 和预期标签
 *   5. 解密数据并验证标签
 *   6. 标签验证失败则返回 false
 *
 * @see AES.h 头文件声明
 */

#include "AES.h"
#include "Errors.h"
#include <limits>

/**
 * @brief AES 构造函数 - 初始化 AES-128-GCM 加密上下文
 *
 * 职责:
 *   创建并初始化 OpenSSL EVP 加密上下文,配置为 AES-128-GCM 模式
 *
 * 参数:
 *   encrypting - true 表示加密模式,false 表示解密模式
 *
 * 主要流程:
 *   1. 创建新的 EVP_CIPHER_CTX 上下文对象
 *   2. 初始化上下文结构
 *   3. 配置为 AES-128-GCM 加密算法
 *   4. 根据参数设置加密或解密模式
 */
Trinity::Crypto::AES::AES(bool encrypting) : _ctx(EVP_CIPHER_CTX_new()), _encrypting(encrypting)
{
    // 初始化加密上下文结构体
    EVP_CIPHER_CTX_init(_ctx);
    // 配置加密算法为 AES-128-GCM,设置加密/解密模式(1为加密,0为解密)
    int status = EVP_CipherInit_ex(_ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr, _encrypting ? 1 : 0);
    ASSERT(status);
}

/**
 * @brief AES 析构函数 - 清理加密上下文资源
 *
 * 职责:
 *   释放 OpenSSL EVP 加密上下文,防止内存泄漏
 *
 * 主要流程:
 *   调用 EVP_CIPHER_CTX_free 释放上下文对象及其关联的所有资源
 */
Trinity::Crypto::AES::~AES()
{
    EVP_CIPHER_CTX_free(_ctx);
}

/**
 * @brief Init - 设置 AES 加密密钥
 *
 * 职责:
 *   为已初始化的加密上下文设置加密密钥
 *
 * 参数:
 *   key - 128位(16字节)的加密密钥,使用 std::array<uint8, 16> 存储
 *
 * 返回值:
 *   无返回值,但通过断言确保密钥设置成功
 *
 * 主要流程:
 *   调用 EVP_CipherInit_ex 更新上下文的密钥参数,保持其他参数不变
 *   参数 -1 表示保持之前设置的加密/解密模式不变
 */
void Trinity::Crypto::AES::Init(Key const& key)
{
    // 设置加密密钥,其他参数(算法类型、加密模式)保持不变
    int status = EVP_CipherInit_ex(_ctx, nullptr, nullptr, key.data(), nullptr, -1);
    ASSERT(status);
}

/**
 * @brief Process - 执行 AES-GCM 加密或解密操作
 *
 * 职责:
 *   使用 AES-128-GCM 模式对数据进行加密或解密,并处理认证标签
 *
 * 参数:
 *   iv     - 初始化向量(IV),12字节随机数,确保相同密钥加密相同明文得到不同密文
 *   data   - 输入/输出数据缓冲区,加密时输入明文输出密文,解密时相反(原地处理)
 *   length - 数据长度(字节数)
 *   tag    - GCM 认证标签,16字节:
 *            - 加密模式: 输出参数,生成的认证标签
 *            - 解密模式: 输入参数,用于验证数据完整性
 *
 * 返回值:
 *   true  - 操作成功
 *   false - 操作失败(解密时标签验证失败或 OpenSSL 操作失败)
 *
 * 主要流程:
 *   1. 设置初始化向量(IV)
 *   2. 执行加密/解密操作(EVP_CipherUpdate)
 *   3. 解密模式: 设置预期的认证标签用于验证
 *   4. 完成加密/解密操作(EVP_CipherFinal_ex)
 *   5. 加密模式: 获取生成的认证标签
 *
 * 注意事项:
 *   - GCM 模式同时提供加密和认证功能
 *   - 解密时如果 tag 验证失败,函数返回 false
 *   - 数据长度不能超过 INT_MAX (约2GB)
 */
bool Trinity::Crypto::AES::Process(IV const& iv, uint8* data, size_t length, Tag& tag)
{
    // 确保数据长度不超过 int 类型最大值
    ASSERT(length <= static_cast<size_t>(std::numeric_limits<int>::max()));
    int len = static_cast<int>(length);

    // 设置初始化向量(IV),保持密钥和加密模式不变
    if (!EVP_CipherInit_ex(_ctx, nullptr, nullptr, nullptr, iv.data(), -1))
        return false;

    // 执行加密/解密操作,结果直接写回 data 缓冲区(原地处理)
    int outLen;
    if (!EVP_CipherUpdate(_ctx, data, &outLen, data, len))
        return false;

    // 计算剩余未处理的数据长度
    len -= outLen;

    // 解密模式: 设置预期的认证标签,用于后续验证数据完整性和真实性
    if (!_encrypting && !EVP_CIPHER_CTX_ctrl(_ctx, EVP_CTRL_GCM_SET_TAG, sizeof(tag), tag))
        return false;

    // 完成加密/解密操作,处理最后的数据块
    // 注意: GCM 模式下此步骤会验证认证标签(解密时)
    if (!EVP_CipherFinal_ex(_ctx, data + outLen, &outLen))
        return false;

    // 验证所有数据都已处理完成
    ASSERT(len == outLen);

    // 加密模式: 获取生成的 GCM 认证标签
    if (_encrypting && !EVP_CIPHER_CTX_ctrl(_ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag))
        return false;

    return true;
}
