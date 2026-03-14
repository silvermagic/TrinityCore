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
 * @file ARC4.cpp
 * @brief ARC4 流加密算法实现
 *
 * 模块职责:
 *   实现 ARC4 类的构造、析构、初始化和加密/解密功能
 *
 * 主要功能:
 *   1. 管理 OpenSSL EVP 上下文的生命周期
 *   2. 处理不同 OpenSSL 版本的兼容性问题
 *   3. 提供密钥初始化接口
 *   4. 实现数据加密/解密操作
 *
 * 技术细节:
 *   - 使用 OpenSSL EVP 高级加密接口
 *   - 支持 OpenSSL 1.x 和 3.x 版本
 *   - OpenSSL 3.x 使用提供者（provider）架构
 *   - 所有错误通过断言机制处理
 */

#include "ARC4.h"
#include "Errors.h"

/**
 * @brief ARC4 构造函数 - 初始化 ARC4 流密码上下文
 *
 * 职责:
 *   创建并初始化 OpenSSL EVP 加密上下文,为 ARC4 加密操作做准备
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 创建新的 EVP_CIPHER_CTX 上下文对象
 *   2. 根据 OpenSSL 版本获取 RC4 密码算法:
 *      - OpenSSL 3.0+: 使用 EVP_CIPHER_fetch 动态获取
 *      - OpenSSL 1.x: 使用 EVP_rc4() 获取内置密码
 *   3. 初始化加密上下文
 *   4. 设置加密算法为 RC4,准备进行加密操作
 *
 * 技术说明:
 *   EVP_CIPHER_CTX_new() 分配并初始化一个新的加密上下文对象
 *   EVP_EncryptInit_ex() 初始化加密操作，此时还未设置密钥
 *   使用 ASSERT 确保所有操作成功完成
 */
Trinity::Crypto::ARC4::ARC4() : _ctx(EVP_CIPHER_CTX_new())
{
    // 根据 OpenSSL 版本选择不同的密码对象获取方式
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // OpenSSL 3.0+ 版本: 使用新的提供者架构
    // EVP_CIPHER_fetch 从默认提供者中动态获取 RC4 密码实现
    // 参数: (OSSL_LIB_CTX*, 算法名称, 属性查询字符串)
    // 返回: EVP_CIPHER 对象指针，需要在使用后释放
    _cipher = EVP_CIPHER_fetch(nullptr, "RC4", nullptr);
#else
    // OpenSSL 1.x 版本: 使用内置的密码对象
    // EVP_rc4() 返回指向内置 RC4 密码对象的指针，无需手动释放
    EVP_CIPHER const* _cipher = EVP_rc4();
#endif

    // 初始化加密上下文，清零所有内部状态
    EVP_CIPHER_CTX_init(_ctx);

    // 初始化加密操作，设置使用的密码算法（RC4）
    // 参数: (上下文, 密码算法, 引擎, 密钥, 初始化向量)
    // 此时密钥和 IV 都为 nullptr，将在 Init() 方法中设置
    int result = EVP_EncryptInit_ex(_ctx, _cipher, nullptr, nullptr, nullptr);

    // 断言确保初始化成功（返回值为 1 表示成功）
    ASSERT(result == 1);
}

/**
 * @brief ARC4 析构函数 - 清理加密上下文资源
 *
 * 职责:
 *   释放 ARC4 加密上下文和相关资源,防止内存泄漏
 *
 * 参数:
 *   无
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 释放 EVP_CIPHER_CTX 上下文对象
 *   2. 如果是 OpenSSL 3.0+,额外释放动态获取的密码对象
 *
 * 技术说明:
 *   EVP_CIPHER_CTX_free() 清理并释放加密上下文
 *   在 OpenSSL 3.0+ 中，密码对象是通过 EVP_CIPHER_fetch() 动态获取的，
 *   因此需要使用 EVP_CIPHER_free() 显式释放
 */
Trinity::Crypto::ARC4::~ARC4()
{
    // 释放加密上下文对象
    // EVP_CIPHER_CTX_free 内部会清理上下文状态并释放内存
    EVP_CIPHER_CTX_free(_ctx);

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // OpenSSL 3.0+ 版本需要显式释放动态获取的密码对象
    // 这是 OpenSSL 3.x 的新要求，因为密码对象可能有引用计数
    EVP_CIPHER_free(_cipher);
#endif
}

/**
 * @brief 初始化 ARC4 密钥
 *
 * 职责:
 *   使用提供的种子数据(密钥)初始化 ARC4 流密码状态
 *
 * 参数:
 *   @param seed 指向种子/密钥数据的指针
 *   @param len  种子/密钥数据的长度(字节)
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 设置加密上下文的密钥长度
 *   2. 使用种子数据初始化加密操作
 *
 * 技术说明:
 *   ARC4 是对称流密码,加密和解密使用相同的操作
 *   密钥长度可变，RC4 支持的密钥长度为 1-256 字节
 *   EVP_CIPHER_CTX_set_key_length 设置密钥长度
 *   EVP_EncryptInit_ex 设置密钥，IV 参数为 nullptr 因为 RC4 不使用 IV
 *
 * 注意事项:
 *   - 相同的密钥不能重复用于加密不同的数据（会降低安全性）
 *   - 每次加密应使用新的密钥或密钥+IV组合
 */
void Trinity::Crypto::ARC4::Init(uint8 const* seed, size_t len)
{
    // 设置加密上下文的密钥长度
    // 对于 RC4，密钥长度可以是 1-256 字节的任意长度
    // 此函数调整 EVP 上下文以适应提供的密钥长度
    int result1 = EVP_CIPHER_CTX_set_key_length(_ctx, len);

    // 断言确保设置成功
    ASSERT(result1 == 1);

    // 使用提供的种子数据设置密钥
    // EVP_EncryptInit_ex 参数说明:
    //   _ctx: 加密上下文
    //   nullptr: 不改变密码算法（已在构造函数中设置为 RC4）
    //   nullptr: 不指定引擎（使用默认引擎）
    //   seed: 密钥数据
    //   nullptr: 初始化向量（RC4 不使用 IV）
    int result2 = EVP_EncryptInit_ex(_ctx, nullptr, nullptr, seed, nullptr);

    // 断言确保密钥设置成功
    ASSERT(result2 == 1);
}

/**
 * @brief 使用 ARC4 加密/解密数据
 *
 * 职责:
 *   对数据执行 ARC4 流密码加密或解密操作
 *   由于 ARC4 是对称流密码,加密和解密使用相同的操作
 *
 * 参数:
 *   @param data 指向要加密/解密的数据缓冲区(原地处理)
 *   @param len  数据长度(字节)
 *
 * 返回值:
 *   无
 *
 * 主要流程:
 *   1. 调用 EVP_EncryptUpdate 执行加密/解密操作(原地处理)
 *   2. 调用 EVP_EncryptFinal_ex 完成加密/解密操作
 *
 * 技术说明:
 *   ARC4 流密码加密原理:
 *     1. 生成伪随机密钥流
 *     2. 明文/密文与密钥流进行异或运算
 *     3. 由于异或运算可逆: A XOR B XOR B = A
 *        因此加密和解密使用相同操作
 *
 *   EVP_EncryptUpdate 执行实际的加密/解密操作:
 *     - 输入和输出可以是同一缓冲区（原地加密）
 *     - 对于 RC4 流密码，不需要填充
 *     - outlen 返回实际处理的字节数
 *
 *   EVP_EncryptFinal_ex 完成加密操作:
 *     - 对于流密码（如 RC4），此步骤主要清理内部状态
 *     - 不需要对数据进行填充
 *
 * 注意事项:
 *   - 数据在原缓冲区中修改，调用前请确保已备份（如需要）
 *   - 加密和解密操作完全相同
 *   - 每次调用都会推进密钥流状态，不可重放
 */
void Trinity::Crypto::ARC4::UpdateData(uint8* data, size_t len)
{
    int outlen = 0;

    // 执行加密/解密操作（原地处理）
    // 参数说明:
    //   _ctx: 加密上下文
    //   data: 输出缓冲区（原地加密）
    //   &outlen: 输出实际处理的字节数
    //   data: 输入数据缓冲区（与输出缓冲区相同，实现原地加密）
    //   len: 输入数据长度
    //
    // 对于 RC4 流密码:
    //   - 逐字节生成密钥流
    //   - 密钥流与输入数据异或
    //   - 输出长度等于输入长度（无填充）
    int result1 = EVP_EncryptUpdate(_ctx, data, &outlen, data, len);

    // 断言确保加密操作成功
    ASSERT(result1 == 1);

    // 完成加密操作
    // 对于 RC4 流密码，此调用主要用于：
    //   - 清理内部状态
    //   - 处理可能的剩余数据（虽然 RC4 不需要）
    //   - 完成密码操作的完整生命周期
    //
    // 参数说明:
    //   _ctx: 加密上下文
    //   data: 输出缓冲区（此处不会有额外输出）
    //   &outlen: 输出字节数（对于 RC4 应为 0）
    int result2 = EVP_EncryptFinal_ex(_ctx, data, &outlen);

    // 断言确保完成操作成功
    ASSERT(result2 == 1);
}
