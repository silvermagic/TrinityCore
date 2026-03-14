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
 * @file BigNumber.cpp
 * @brief BigNumber 类的实现文件 - 大数运算核心逻辑
 *
 * 本文件实现了 BigNumber 类的所有成员函数，封装 OpenSSL BIGNUM 库调用。
 *
 * 实现要点：
 *   - 所有运算都委托给 OpenSSL 的 BN_* 系列函数
 *   - 资源管理遵循 RAII 原则
 *   - 需要临时变量的运算使用 BN_CTX 上下文
 *   - 字节序转换使用 OpenSSL 提供的专用函数
 *
 * 性能考虑：
 *   - BN_CTX 创建/销毁有开销，连续运算可复用上下文
 *   - 拷贝操作较重，优先使用移动语义或引用传递
 *   - 模幂运算是性能瓶颈，已使用 OpenSSL 优化实现
 *
 * 错误处理：
 *   - OpenSSL 错误通常通过返回值表示
 *   - 关键操作使用断言检查前置条件
 *   - 内存分配失败会导致程序终止（OpenSSL 行为）
 *
 * @see BigNumber.h 头文件中的类定义和详细文档
 * @see OpenSSL BN API: https://www.openssl.org/docs/man1.1.1/man3/BN_new.html
 */

#include "Cryptography/BigNumber.h"
#include "Errors.h"
#include <openssl/bn.h>
#include <cstring>
#include <algorithm>
#include <memory>

/**
 * @brief 默认构造函数
 *
 * 职责：
 *   创建一个初始化为0的大数对象
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_new() 函数分配并初始化一个新的 BIGNUM 结构
 */
BigNumber::BigNumber()
    : _bn(BN_new())
{ }

/**
 * @brief 拷贝构造函数
 *
 * 职责：
 *   从现有 BigNumber 对象创建一个新的副本
 *
 * 参数：
 *   bn: 源 BigNumber 对象的常量引用
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_dup() 函数复制源对象的 BIGNUM 结构
 */
BigNumber::BigNumber(BigNumber const& bn)
    : _bn(BN_dup(bn.BN()))
{ }

/**
 * @brief 析构函数
 *
 * 职责：
 *   释放大数对象占用的资源
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_free() 函数释放 BIGNUM 结构及其关联的内存
 */
BigNumber::~BigNumber()
{
    BN_free(_bn);
}

/**
 * @brief 设置有符号32位整数值
 *
 * 职责：
 *   将大数设置为有符号32位整数值
 *
 * 参数：
 *   val: 有符号32位整数值，可以为负数
 *
 * 主要流程：
 *   1. 取绝对值并调用无符号版本设置数值
 *   2. 如果原值为负数，设置 BIGNUM 的负数标志
 */
void BigNumber::SetDword(int32 val)
{
    SetDword(uint32(abs(val)));
    if (val < 0)
        BN_set_negative(_bn, 1);
}

/**
 * @brief 设置无符号32位整数值
 *
 * 职责：
 *   将大数设置为无符号32位整数值
 *
 * 参数：
 *   val: 无符号32位整数值
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_set_word() 函数设置值
 */
void BigNumber::SetDword(uint32 val)
{
    BN_set_word(_bn, val);
}

/**
 * @brief 设置64位整数值
 *
 * 职责：
 *   将大数设置为64位无符号整数值
 *
 * 参数：
 *   val: 64位无符号整数值
 *
 * 主要流程：
 *   1. 设置高32位：将值右移32位后设置
 *   2. 左移32位腾出低32位空间
 *   3. 添加低32位：使用掩码提取低32位并相加
 *
 * 注意：
 *   由于 OpenSSL 的 BN_set_word 只支持设置单个字（通常为32位），
 *   需要分两步处理64位值
 */
void BigNumber::SetQword(uint64 val)
{
    BN_set_word(_bn, (uint32)(val >> 32));
    BN_lshift(_bn, _bn, 32);
    BN_add_word(_bn, (uint32)(val & 0xFFFFFFFF));
}

/**
 * @brief 从二进制数据设置大数值
 *
 * 职责：
 *   将字节数组转换为大数值
 *
 * 参数：
 *   bytes: 源字节数组指针
 *   len: 字节数组长度
 *   littleEndian: 是否为小端序（true=小端序，false=大端序）
 *
 * 主要流程：
 *   根据字节序选择不同的 OpenSSL 转换函数：
 *   - 小端序：使用 BN_lebin2bn（最低有效字节在前）
 *   - 大端序：使用 BN_bin2bn（最高有效字节在前）
 *
 * 应用场景：
 *   常用于从网络数据包或文件读取大数
 */
void BigNumber::SetBinary(uint8 const* bytes, int32 len, bool littleEndian)
{
    if (littleEndian)
        BN_lebin2bn(bytes, len, _bn);
    else
        BN_bin2bn(bytes, len, _bn);
}

/**
 * @brief 从十六进制字符串设置大数值
 *
 * 职责：
 *   将十六进制字符串解析为大数值
 *
 * 参数：
 *   str: 十六进制字符串（可以包含前导"0x"或不包含）
 *
 * 返回值：
 *   true: 解析成功
 *   false: 解析失败（字符串格式错误）
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_hex2bn() 函数解析十六进制字符串
 */
bool BigNumber::SetHexStr(char const* str)
{
    int n = BN_hex2bn(&_bn, str);
    return (n > 0);
}

/**
 * @brief 生成随机大数
 *
 * 职责：
 *   生成指定位数的随机大数
 *
 * 参数：
 *   numbits: 随机数的位数（bit数）
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_rand() 函数生成随机数
 *   参数说明：
 *   - numbits: 位数
 *   - 0: 不强制设置最高位为1（可能生成较小的数）
 *   - 1: 使用加密强度随机数生成器
 *
 * 应用场景：
 *   用于生成密码学随机数，如加密密钥、随机素数等
 */
void BigNumber::SetRand(int32 numbits)
{
    BN_rand(_bn, numbits, 0, 1);
}

/**
 * @brief 赋值运算符
 *
 * 职责：
 *   将另一个 BigNumber 对象的值赋给当前对象
 *
 * 参数：
 *   bn: 源 BigNumber 对象的常量引用
 *
 * 返回值：
 *   当前对象的引用（支持链式赋值）
 *
 * 主要流程：
 *   1. 自赋值检查：如果源对象和目标对象相同，直接返回
 *   2. 调用 OpenSSL 的 BN_copy() 复制内部 BIGNUM 结构
 */
BigNumber& BigNumber::operator=(BigNumber const& bn)
{
    if (this == &bn)
        return *this;

    BN_copy(_bn, bn._bn);
    return *this;
}

/**
 * @brief 加法赋值运算符
 *
 * 职责：
 *   将当前大数与另一个大数相加，结果存储在当前对象中
 *
 * 参数：
 *   bn: 要加上的 BigNumber 对象
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_add() 执行大数加法
 */
BigNumber& BigNumber::operator+=(BigNumber const& bn)
{
    BN_add(_bn, _bn, bn._bn);
    return *this;
}

/**
 * @brief 减法赋值运算符
 *
 * 职责：
 *   从当前大数减去另一个大数，结果存储在当前对象中
 *
 * 参数：
 *   bn: 要减去的 BigNumber 对象
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_sub() 执行大数减法
 */
BigNumber& BigNumber::operator-=(BigNumber const& bn)
{
    BN_sub(_bn, _bn, bn._bn);
    return *this;
}

/**
 * @brief 乘法赋值运算符
 *
 * 职责：
 *   将当前大数与另一个大数相乘，结果存储在当前对象中
 *
 * 参数：
 *   bn: 要乘以的 BigNumber 对象
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   1. 创建 BN_CTX 上下文（存储临时变量）
 *   2. 调用 OpenSSL 的 BN_mul() 执行大数乘法
 *   3. 释放上下文
 *
 * 注意：
 *   乘法运算需要临时上下文来存储中间结果
 */
BigNumber& BigNumber::operator*=(BigNumber const& bn)
{
    BN_CTX *bnctx;

    bnctx = BN_CTX_new();
    BN_mul(_bn, _bn, bn._bn, bnctx);
    BN_CTX_free(bnctx);

    return *this;
}

/**
 * @brief 除法赋值运算符
 *
 * 职责：
 *   将当前大数除以另一个大数，商存储在当前对象中
 *
 * 参数：
 *   bn: 除数 BigNumber 对象
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   1. 创建 BN_CTX 上下文
 *   2. 调用 OpenSSL 的 BN_div() 执行大数除法
 *      - 第二个参数为 nullptr 表示不保存余数
 *   3. 释放上下文
 *
 * 注意：
 *   只保留商，余数被丢弃
 */
BigNumber& BigNumber::operator/=(BigNumber const& bn)
{
    BN_CTX *bnctx;

    bnctx = BN_CTX_new();
    BN_div(_bn, nullptr, _bn, bn._bn, bnctx);
    BN_CTX_free(bnctx);

    return *this;
}

/**
 * @brief 取模赋值运算符
 *
 * 职责：
 *   计算当前大数对另一个大数取模的结果，存储在当前对象中
 *
 * 参数：
 *   bn: 模数 BigNumber 对象
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   1. 创建 BN_CTX 上下文
 *   2. 调用 OpenSSL 的 BN_mod() 执行取模运算
 *   3. 释放上下文
 *
 * 应用场景：
 *   密码学运算中常用的模运算，如 RSA 加密、Diffie-Hellman 密钥交换等
 */
BigNumber& BigNumber::operator%=(BigNumber const& bn)
{
    BN_CTX *bnctx;

    bnctx = BN_CTX_new();
    BN_mod(_bn, _bn, bn._bn, bnctx);
    BN_CTX_free(bnctx);

    return *this;
}

/**
 * @brief 左移赋值运算符
 *
 * 职责：
 *   将大数左移指定位数（相当于乘以 2^n）
 *
 * 参数：
 *   n: 左移的位数
 *
 * 返回值：
 *   当前对象的引用
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_lshift() 执行左移操作
 */
BigNumber& BigNumber::operator<<=(int n)
{
    BN_lshift(_bn, _bn, n);
    return *this;
}

/**
 * @brief 比较两个大数的大小
 *
 * 职责：
 *   将当前大数与另一个大数进行比较
 *
 * 参数：
 *   bn: 要比较的 BigNumber 对象
 *
 * 返回值：
 *   < 0: 当前对象小于 bn
 *   = 0: 当前对象等于 bn
 *   > 0: 当前对象大于 bn
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_cmp() 执行比较
 */
int32 BigNumber::CompareTo(BigNumber const& bn) const
{
    return BN_cmp(_bn, bn._bn);
}

/**
 * @brief 计算指数运算
 *
 * 职责：
 *   计算当前大数的另一个大数次幂（this ^ bn）
 *
 * 参数：
 *   bn: 指数 BigNumber 对象
 *
 * 返回值：
 *   包含指数运算结果的新 BigNumber 对象
 *
 * 主要流程：
 *   1. 创建结果对象和 BN_CTX 上下文
 *   2. 调用 OpenSSL 的 BN_exp() 计算指数
 *   3. 释放上下文并返回结果
 *
 * 注意：
 *   这是一个普通指数运算，不是模指数运算
 */
BigNumber BigNumber::Exp(BigNumber const& bn) const
{
    BigNumber ret;
    BN_CTX *bnctx;

    bnctx = BN_CTX_new();
    BN_exp(ret._bn, _bn, bn._bn, bnctx);
    BN_CTX_free(bnctx);

    return ret;
}

/**
 * @brief 计算模指数运算（模幂运算）
 *
 * 职责：
 *   计算 (this ^ bn1) mod bn2，即模指数运算
 *
 * 参数：
 *   bn1: 指数 BigNumber 对象
 *   bn2: 模数 BigNumber 对象
 *
 * 返回值：
 *   包含模指数运算结果的新 BigNumber 对象
 *
 * 主要流程：
 *   1. 创建结果对象和 BN_CTX 上下文
 *   2. 调用 OpenSSL 的 BN_mod_exp() 计算模指数
 *   3. 释放上下文并返回结果
 *
 * 应用场景：
 *   RSA 加密/解密的核心运算：c = m^e mod n
 *   Diffie-Hellman 密钥交换：g^a mod p
 *   这是密码学中最重要的大数运算之一
 */
BigNumber BigNumber::ModExp(BigNumber const& bn1, BigNumber const& bn2) const
{
    BigNumber ret;
    BN_CTX *bnctx;

    bnctx = BN_CTX_new();
    BN_mod_exp(ret._bn, _bn, bn1._bn, bn2._bn, bnctx);
    BN_CTX_free(bnctx);

    return ret;
}

/**
 * @brief 获取大数的字节数
 *
 * 职责：
 *   计算表示该大数所需的最少字节数
 *
 * 返回值：
 *   表示该大数所需的字节数
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_num_bytes() 获取字节数
 *
 * 应用场景：
 *   在序列化大数或分配缓冲区时确定所需大小
 */
int32 BigNumber::GetNumBytes() const
{
    return BN_num_bytes(_bn);
}

/**
 * @brief 将大数转换为32位无符号整数
 *
 * 职责：
 *   将大数转换为 uint32 类型
 *
 * 返回值：
 *   大数的低32位值
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_get_word() 获取值并转换为 uint32
 *
 * 注意：
 *   如果大数值超过 2^32-1，只会返回低32位，高位被截断
 */
uint32 BigNumber::AsDword() const
{
    return (uint32)BN_get_word(_bn);
}

/**
 * @brief 检查大数是否为零
 *
 * 职责：
 *   判断大数是否等于0
 *
 * 返回值：
 *   true: 大数为0
 *   false: 大数不为0
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_is_zero() 进行判断
 */
bool BigNumber::IsZero() const
{
    return BN_is_zero(_bn);
}

/**
 * @brief 检查大数是否为负数
 *
 * 职责：
 *   判断大数是否为负数
 *
 * 返回值：
 *   true: 大数为负数
 *   false: 大数为正数或零
 *
 * 主要流程：
 *   调用 OpenSSL 的 BN_is_negative() 进行判断
 */
bool BigNumber::IsNegative() const
{
    return BN_is_negative(_bn);
}

/**
 * @brief 将大数转换为字节数组
 *
 * 职责：
 *   将大数值导出为字节数组格式
 *
 * 参数：
 *   buf: 目标缓冲区指针
 *   bufsize: 缓冲区大小（字节数）
 *   littleEndian: 是否使用小端序输出
 *                 true: 小端序（最低有效字节在前）
 *                 false: 大端序（最高有效字节在前）
 *
 * 主要流程：
 *   1. 根据字节序选择 OpenSSL 函数：
 *      - 小端序：BN_bn2lebinpad()
 *      - 大端序：BN_bn2binpad()
 *   2. 如果缓冲区太小，触发断言错误
 *
 * 注意：
 *   这两个 OpenSSL 函数会自动填充前导零以满足 bufsize 要求
 *   如果 bufsize 小于实际所需字节数，函数会失败并触发断言
 */
void BigNumber::GetBytes(uint8* buf, size_t bufsize, bool littleEndian) const
{
    int res = littleEndian ? BN_bn2lebinpad(_bn, buf, bufsize) : BN_bn2binpad(_bn, buf, bufsize);
    ASSERT(res > 0, "Buffer of size %zu is too small to hold bignum with %d bytes.\n", bufsize, BN_num_bytes(_bn));
}

/**
 * @brief 将大数转换为字节向量
 *
 * 职责：
 *   将大数值导出为 std::vector<uint8> 格式
 *
 * 参数：
 *   minSize: 最小输出字节数（如果大数较小，会用前导零填充）
 *   littleEndian: 是否使用小端序输出
 *
 * 返回值：
 *   包含大数字节数据的 vector
 *
 * 主要流程：
 *   1. 计算输出长度：取大数实际字节数和 minSize 的较大值
 *   2. 分配 vector 空间
 *   3. 调用 GetBytes() 填充数据
 *
 * 应用场景：
 *   方便地将大数转换为可用于网络传输或存储的字节序列
 */
std::vector<uint8> BigNumber::ToByteVector(int32 minSize, bool littleEndian) const
{
    std::size_t length = std::max(GetNumBytes(), minSize);
    std::vector<uint8> v;
    v.resize(length);
    GetBytes(v.data(), length, littleEndian);
    return v;
}

/**
 * @brief 将大数转换为十六进制字符串
 *
 * 职责：
 *   将大数值转换为十六进制字符串表示
 *
 * 返回值：
 *   十六进制格式的字符串（不带"0x"前缀）
 *
 * 主要流程：
 *   1. 调用 OpenSSL 的 BN_bn2hex() 转换为十六进制字符串
 *   2. 将结果复制到 std::string
 *   3. 释放 OpenSSL 分配的内存
 *
 * 应用场景：
 *   用于日志输出、调试显示或数据序列化
 */
std::string BigNumber::AsHexStr() const
{
    char* ch = BN_bn2hex(_bn);
    std::string ret = ch;
    OPENSSL_free(ch);
    return ret;
}

/**
 * @brief 将大数转换为十进制字符串
 *
 * 职责：
 *   将大数值转换为十进制字符串表示
 *
 * 返回值：
 *   十进制格式的字符串
 *
 * 主要流程：
 *   1. 调用 OpenSSL 的 BN_bn2dec() 转换为十进制字符串
 *   2. 将结果复制到 std::string
 *   3. 释放 OpenSSL 分配的内存
 *
 * 应用场景：
 *   用于人类可读的大数输出
 */
std::string BigNumber::AsDecStr() const
{
    char* ch = BN_bn2dec(_bn);
    std::string ret = ch;
    OPENSSL_free(ch);
    return ret;
}
