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
 * @file BigNumber.h
 * @brief 大数运算模块 - 提供任意精度整数运算支持
 *
 * 模块职责：
 *   封装 OpenSSL BIGNUM 库，提供易用的大数运算接口
 *   支持密码学应用中的高精度整数运算
 *
 * 主要功能：
 *   - 基本算术运算：加减乘除、取模、位移
 *   - 密码学运算：模指数运算、随机数生成
 *   - 数据转换：二进制、十六进制、十进制格式互转
 *   - 比较操作：大小比较、零值判断、符号判断
 *
 * 大数运算原理：
 *   大数（BigNumber）用于表示超出标准整数类型范围（如 32 位或 64 位）的整数。
 *   在密码学中，RSA 等算法需要处理数千位的超大整数。
 *
 *   存储方式：
 *   - 使用动态数组存储数字的各个"位"（通常是 32 位或 64 位）
 *   - 内部采用小端序表示：低有效位存储在数组前端
 *   - 符号单独存储，支持负数运算
 *
 *   运算复杂度示例：
 *   - 加减法：O(n)，n 为位数
 *   - 乘法：O(n^2) 或更优算法（Karatsuba 等）
 *   - 模幂运算：O(n^3) 或更优算法
 *
 * 应用场景：
 *   - RSA 加密解密：c = m^e mod n
 *   - Diffie-Hellman 密钥交换：g^a mod p
 *   - 数字签名验证
 *   - SRP6 认证协议（游戏登录验证）
 *
 * 设计说明：
 *   - 封装 OpenSSL BIGNUM 结构（bignum_st），提供 RAII 管理
 *   - 支持多种构造方式：整数、字符串、二进制数组
 *   - 提供运算符重载，使大数运算代码更直观
 *   - 线程安全：每个 BigNumber 对象独立管理自己的内存
 */

#ifndef _AUTH_BIGNUMBER_H
#define _AUTH_BIGNUMBER_H

#include "Define.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

struct bignum_st;

/**
 * @class BigNumber
 * @brief 任意精度大整数类
 *
 * BigNumber 类封装了 OpenSSL 的 BIGNUM 结构，提供面向对象的大数运算接口。
 * 该类支持动态精度的整数运算，数值大小仅受可用内存限制。
 *
 * 核心特性：
 *   - 自动内存管理：构造时分配，析构时释放
 *   - 值语义：支持拷贝构造和赋值，深拷贝内部数据
 *   - 类型安全：通过运算符重载提供类型检查
 *   - 多格式转换：支持二进制、十六进制、十进制格式
 *
 * 使用示例：
 * @code
 *   BigNumber a("1234567890ABCDEF", 16);  // 十六进制构造
 *   BigNumber b(12345);                    // 整数构造
 *   BigNumber c = a + b;                   // 加法运算
 *   BigNumber d = a.ModExp(b, BigNumber(65537));  // 模幂运算（RSA）
 *   std::string hex = c.AsHexStr();        // 转十六进制字符串
 * @endcode
 *
 * 性能考虑：
 *   - 拷贝操作需要复制整个大数，成本较高
 *   - 乘除法需要创建临时上下文，建议避免频繁调用
 *   - 对于连续运算，优先使用赋值运算符（如 +=）而非二元运算符（如 +）
 *
 * 线程安全性：
 *   - 单个 BigNumber 对象不能在多线程间共享
 *   - 不同 BigNumber 对象可以在不同线程中独立使用
 *
 * @see OpenSSL BIGNUM 文档: https://www.openssl.org/docs/man1.1.1/man3/BN_new.html
 */
class TC_COMMON_API BigNumber
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个初始化为 0 的大数对象。
         * 内部调用 OpenSSL 的 BN_new() 分配并初始化 BIGNUM 结构。
         */
        BigNumber();

        /**
         * @brief 拷贝构造函数
         *
         * 从现有 BigNumber 对象创建深拷贝。
         * 内部调用 OpenSSL 的 BN_dup() 复制所有数据。
         *
         * @param bn 源 BigNumber 对象
         */
        BigNumber(BigNumber const& bn);

        /**
         * @brief 从 uint32 构造大数
         *
         * 委托构造函数，先创建默认对象，再设置值。
         *
         * @param v 无符号 32 位整数值
         */
        BigNumber(uint32 v) : BigNumber() { SetDword(v); }

        /**
         * @brief 从 int32 构造大数
         *
         * 支持负数输入。委托构造函数。
         *
         * @param v 有符号 32 位整数值（可以为负）
         */
        BigNumber(int32 v) : BigNumber() { SetDword(v); }

        /**
         * @brief 从十六进制字符串构造大数
         *
         * 字符串可以包含 "0x" 前缀，也可以不包含。
         * 示例: "123ABC" 或 "0x123ABC"
         *
         * @param v 十六进制格式字符串
         */
        BigNumber(std::string const& v) : BigNumber() { SetHexStr(v); }

        /**
         * @brief 从字节数组构造大数
         *
         * 从固定大小的字节数组（如 std::array）构造大数。
         * 常用于从网络数据包或加密密钥数据构造大数。
         *
         * @tparam Size 数组大小（自动推导）
         * @param v 源字节数组
         * @param littleEndian 字节序：true=小端序，false=大端序
         *                     小端序：最低有效字节在前（网络传输常用）
         *                     大端序：最高有效字节在前（人类可读格式）
         */
        template <size_t Size>
        BigNumber(std::array<uint8, Size> const& v, bool littleEndian = true) : BigNumber() { SetBinary(v.data(), Size, littleEndian); }

        /**
         * @brief 析构函数
         *
         * 释放 OpenSSL BIGNUM 结构及其关联的动态内存。
         * 调用 OpenSSL 的 BN_free() 函数。
         */
        ~BigNumber();

        /**
         * @brief 设置有符号 32 位整数值
         *
         * 将大数设置为指定的有符号整数值。支持负数。
         * 内部取绝对值设置数值，然后设置负数标志。
         *
         * @param val 有符号 32 位整数值
         */
        void SetDword(int32);

        /**
         * @brief 设置无符号 32 位整数值
         *
         * 将大数设置为指定的无符号整数值。
         * 内部调用 OpenSSL 的 BN_set_word() 函数。
         *
         * @param val 无符号 32 位整数值
         */
        void SetDword(uint32);

        /**
         * @brief 设置 64 位整数值
         *
         * 将大数设置为指定的 64 位整数值。
         * 由于 OpenSSL 的 BN_set_word() 只支持单字（通常 32 位），
         * 需要分两步设置：先设置高 32 位，再添加低 32 位。
         *
         * @param val 64 位无符号整数值
         */
        void SetQword(uint64);

        /**
         * @brief 从二进制数据设置大数值
         *
         * 将字节数组转换为大数值。支持大端序和小端序格式。
         *
         * @param bytes 源字节数组指针
         * @param len 字节数组长度
         * @param littleEndian 字节序标志：
         *                     true = 小端序（最低有效字节在前，Intel x86 架构）
         *                     false = 大端序（最高有效字节在前，网络字节序）
         *
         * @note 小端序调用 BN_lebin2bn()，大端序调用 BN_bin2bn()
         */
        void SetBinary(uint8 const* bytes, int32 len, bool littleEndian = true);

        /**
         * @brief 从容器设置二进制数据（模板版本）
         *
         * 通用接口，支持任何具有 data() 和 size() 的容器。
         * 使用 SFINAE 排除指针类型，确保传入的是容器。
         *
         * @tparam Container 容器类型（如 std::vector, std::array 等）
         * @param c 源容器
         * @param littleEndian 字节序标志
         */
        template <typename Container>
        auto SetBinary(Container const& c, bool littleEndian = true) -> std::enable_if_t<!std::is_pointer_v<std::decay_t<Container>>> { SetBinary(std::data(c), std::size(c), littleEndian); }

        /**
         * @brief 从十六进制字符串设置大数值
         *
         * 解析十六进制字符串并设置大数值。
         * 字符串可以包含可选的 "0x" 前缀。
         *
         * @param str 十六进制字符串（如 "123ABC" 或 "0x123ABC"）
         * @return true 解析成功
         * @return false 解析失败（字符串格式错误）
         */
        bool SetHexStr(char const* str);

        /**
         * @brief 从十六进制字符串设置大数值（std::string 重载）
         *
         * 便捷接口，接受 std::string 参数。
         *
         * @param str 十六进制字符串
         * @return true 解析成功
         * @return false 解析失败
         */
        bool SetHexStr(std::string const& str) { return SetHexStr(str.c_str()); }

        /**
         * @brief 生成指定位数的随机大数
         *
         * 生成加密强度的随机大数，用于密码学应用。
         *
         * @param numbits 随机数的位数（bit 数）
         *                例如：numbits=256 生成 256 位随机数（32 字节）
         *
         * @note 使用 OpenSSL 的加密随机数生成器，适合用于密钥生成
         */
        void SetRand(int32 numbits);

        /**
         * @brief 赋值运算符
         *
         * 执行深拷贝，将源大数的值复制到当前对象。
         * 包含自赋值检查以避免不必要的操作。
         *
         * @param bn 源 BigNumber 对象
         * @return 当前对象的引用（支持链式赋值）
         */
        BigNumber& operator=(BigNumber const& bn);

        /**
         * @brief 加法赋值运算符
         *
         * 将当前大数加上另一个大数，结果存储在当前对象中。
         * 内部调用 OpenSSL 的 BN_add() 函数。
         *
         * @param bn 要加上的 BigNumber 对象
         * @return 当前对象的引用
         */
        BigNumber& operator+=(BigNumber const& bn);

        /**
         * @brief 加法运算符
         *
         * 返回两个大数之和的新对象。
         * 实现方式：创建临时副本，调用 += 运算符。
         *
         * @param bn 要加上的 BigNumber 对象
         * @return 包含和的新 BigNumber 对象
         *
         * @note 性能提示：对于连续加法，优先使用 += 避免临时对象
         */
        BigNumber operator+(BigNumber const& bn) const
        {
            BigNumber t(*this);
            return t += bn;
        }

        /**
         * @brief 减法赋值运算符
         *
         * 从当前大数减去另一个大数，结果存储在当前对象中。
         * 支持负数结果。
         *
         * @param bn 要减去的 BigNumber 对象
         * @return 当前对象的引用
         */
        BigNumber& operator-=(BigNumber const& bn);

        /**
         * @brief 减法运算符
         *
         * 返回两个大数之差的新对象。
         *
         * @param bn 要减去的 BigNumber 对象
         * @return 包含差的新 BigNumber 对象
         */
        BigNumber operator-(BigNumber const& bn) const
        {
            BigNumber t(*this);
            return t -= bn;
        }

        /**
         * @brief 乘法赋值运算符
         *
         * 将当前大数乘以另一个大数，结果存储在当前对象中。
         * 需要创建 BN_CTX 上下文来存储临时变量。
         * 时间复杂度：O(n^2)，n 为位数。
         *
         * @param bn 要乘以的 BigNumber 对象
         * @return 当前对象的引用
         */
        BigNumber& operator*=(BigNumber const& bn);

        /**
         * @brief 乘法运算符
         *
         * 返回两个大数之积的新对象。
         *
         * @param bn 要乘以的 BigNumber 对象
         * @return 包含积的新 BigNumber 对象
         */
        BigNumber operator*(BigNumber const& bn) const
        {
            BigNumber t(*this);
            return t *= bn;
        }

        /**
         * @brief 除法赋值运算符
         *
         * 将当前大数除以另一个大数，商存储在当前对象中。
         * 余数被丢弃。除数不能为零（会导致未定义行为）。
         *
         * @param bn 除数 BigNumber 对象
         * @return 当前对象的引用
         *
         * @warning 除数为零时会触发 OpenSSL 错误
         */
        BigNumber& operator/=(BigNumber const& bn);

        /**
         * @brief 除法运算符
         *
         * 返回两个大数之商的新对象。
         *
         * @param bn 除数 BigNumber 对象
         * @return 包含商的新 BigNumber 对象
         */
        BigNumber operator/(BigNumber const& bn) const
        {
            BigNumber t(*this);
            return t /= bn;
        }

        /**
         * @brief 取模赋值运算符
         *
         * 计算当前大数对另一个大数取模的结果，存储在当前对象中。
         * 这是密码学中的核心运算之一。
         *
         * @param bn 模数 BigNumber 对象
         * @return 当前对象的引用
         *
         * @note 结果的符号与被除数相同（C++ 标准行为）
         */
        BigNumber& operator%=(BigNumber const& bn);

        /**
         * @brief 取模运算符
         *
         * 返回取模运算结果的新对象。
         *
         * @param bn 模数 BigNumber 对象
         * @return 包含余数的新 BigNumber 对象
         */
        BigNumber operator%(BigNumber const& bn) const
        {
            BigNumber t(*this);
            return t %= bn;
        }

        /**
         * @brief 左移赋值运算符
         *
         * 将大数左移指定位数，相当于乘以 2^n。
         * 这是非常高效的操作，只需调整内部数组和索引。
         *
         * @param n 左移的位数
         * @return 当前对象的引用
         *
         * @example BigNumber a(5); a <<= 3; // a = 40 (5 * 2^3)
         */
        BigNumber& operator<<=(int n);

        /**
         * @brief 左移运算符
         *
         * 返回左移结果的新对象。
         *
         * @param n 左移的位数
         * @return 包含左移结果的新 BigNumber 对象
         */
        BigNumber operator<<(int n) const
        {
            BigNumber t(*this);
            return t <<= n;
        }

        /**
         * @brief 比较两个大数的大小
         *
         * 执行数值比较，考虑符号和绝对值。
         * 内部调用 OpenSSL 的 BN_cmp() 函数。
         *
         * @param bn 要比较的 BigNumber 对象
         * @return <0 当前对象小于 bn
         * @return 0 当前对象等于 bn
         * @return >0 当前对象大于 bn
         */
        int32 CompareTo(BigNumber const& bn) const;

        /**
         * @brief 相等比较运算符
         *
         * 判断两个大数是否相等。
         *
         * @param bn 要比较的 BigNumber 对象
         * @return true 两数相等
         * @return false 两数不等
         */
        bool operator==(BigNumber const& bn) const { return (CompareTo(bn) == 0); }

        /**
         * @brief 三路比较运算符（C++20）
         *
         * 支持所有比较运算符：<, <=, >, >=, ==, !=
         * 实现 std::strong_ordering，用于排序和比较。
         *
         * @param other 要比较的 BigNumber 对象
         * @return std::strong_ordering::less 当前对象小于 other
         * @return std::strong_ordering::equal 当前对象等于 other
         * @return std::strong_ordering::greater 当前对象大于 other
         */
        std::strong_ordering operator<=>(BigNumber const& other) const
        {
            int32 cmp = CompareTo(other);
            if (cmp < 0)
                return std::strong_ordering::less;
            if (cmp > 0)
                return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }

        /**
         * @brief 判断大数是否为零
         *
         * 快速判断大数值是否等于 0。
         *
         * @return true 大数为 0
         * @return false 大数不为 0
         */
        bool IsZero() const;

        /**
         * @brief 判断大数是否为负数
         *
         * 检查大数的符号标志。
         *
         * @return true 大数为负数
         * @return false 大数为正数或零
         */
        bool IsNegative() const;

        /**
         * @brief 模指数运算（模幂运算）
         *
         * 计算 (this ^ bn1) mod bn2，即：底数^指数 mod 模数
         * 这是密码学中最重要的运算之一。
         *
         * 数学公式: result = base^exponent mod modulus
         *
         * 算法优化：
         *   使用平方-乘算法（Square-and-Multiply），复杂度 O(log(exponent))
         *   避免计算庞大的中间结果，每次乘法后立即取模
         *
         * 应用场景：
         *   - RSA 加密：c = m^e mod n
         *   - RSA 解密：m = c^d mod n
         *   - Diffie-Hellman 密钥交换：B = g^b mod p
         *   - DSA 数字签名验证
         *
         * @param bn1 指数（exponent）
         * @param bn2 模数（modulus）
         * @return 包含模幂运算结果的新 BigNumber 对象
         *
         * @note 模数必须为正数，指数可以为负（计算模逆元）
         * @warning 模数为 1 时，结果总是 0
         */
        BigNumber ModExp(BigNumber const& bn1, BigNumber const& bn2) const;

        /**
         * @brief 指数运算
         *
         * 计算当前大数的另一个大数次幂：this ^ bn
         * 这是普通指数运算，不涉及模运算。
         *
         * 注意：对于大指数，结果会非常巨大，可能消耗大量内存。
         * 在密码学中，通常使用 ModExp() 而非此函数。
         *
         * @param bn 指数 BigNumber 对象
         * @return 包含指数运算结果的新 BigNumber 对象
         *
         * @warning 结果增长极快，谨慎使用
         */
        BigNumber Exp(BigNumber const&) const;

        /**
         * @brief 获取大数的字节数
         *
         * 计算表示该大数所需的最少字节数。
         * 对于正数：字节数 = ceil(位数 / 8)
         * 对于零：返回 0
         *
         * 用于序列化时确定缓冲区大小。
         *
         * @return 表示该大数所需的字节数
         */
        int32 GetNumBytes() const;

        /**
         * @brief 获取内部 BIGNUM 指针（可变版本）
         *
         * 提供直接访问 OpenSSL BIGNUM 结构的接口。
         * 用于需要直接调用 OpenSSL API 的高级场景。
         *
         * @return 指向内部 bignum_st 结构的指针
         *
         * @warning 直接操作内部结构可能破坏对象不变性，谨慎使用
         */
        struct bignum_st* BN() { return _bn; }

        /**
         * @brief 获取内部 BIGNUM 指针（常量版本）
         *
         * 提供只读访问 OpenSSL BIGNUM 结构的接口。
         *
         * @return 指向内部 bignum_st 结构的常量指针
         */
        struct bignum_st const* BN() const { return _bn; }

        /**
         * @brief 将大数转换为 32 位无符号整数
         *
         * 提取大数的低 32 位值。
         * 如果大数超过 32 位范围，高位被截断。
         * 如果大数为负数，返回其二进制补码表示的低 32 位。
         *
         * @return 大数的低 32 位值（uint32）
         *
         * @note 调用前应确保大数在 uint32 范围内，否则数据丢失
         */
        uint32 AsDword() const;

        /**
         * @brief 将大数导出为字节数组
         *
         * 将大数值序列化为字节数组。支持大端序和小端序。
         * 如果缓冲区大于实际所需字节数，前导部分用零填充。
         *
         * @param buf 目标缓冲区指针
         * @param bufsize 缓冲区大小（字节数）
         * @param littleEndian 字节序：true=小端序，false=大端序
         *
         * @note 如果 bufsize < GetNumBytes()，触发断言错误
         * @note 负数会被转换为其二进制补码表示
         */
        void GetBytes(uint8* buf, size_t bufsize, bool littleEndian = true) const;

        /**
         * @brief 将大数转换为字节向量
         *
         * 创建包含大数字节表示的 vector。
         * 可以指定最小大小，不足部分用前导零填充。
         *
         * @param minSize 最小输出字节数（默认 0，表示使用实际所需大小）
         * @param littleEndian 字节序：true=小端序，false=大端序
         * @return 包含大数字节数据的 std::vector<uint8>
         *
         * @example
         *   BigNumber a(12345);
         *   std::vector<uint8> bytes = a.ToByteVector(8);  // 输出至少 8 字节
         */
        std::vector<uint8> ToByteVector(int32 minSize = 0, bool littleEndian = true) const;

        /**
         * @brief 将大数转换为固定大小的字节数组
         *
         * 模板函数，创建固定大小的 std::array。
         * 常用于已知大小的场景，如加密密钥（256位 = 32字节）。
         *
         * @tparam Size 输出数组大小
         * @param littleEndian 字节序：true=小端序，false=大端序
         * @return 包含大数字节数据的 std::array<uint8, Size>
         *
         * @example
         *   BigNumber key("123456...", 16);
         *   std::array<uint8, 32> bytes = key.ToByteArray<32>();
         */
        template <std::size_t Size>
        std::array<uint8, Size> ToByteArray(bool littleEndian = true) const
        {
            std::array<uint8, Size> buf;
            GetBytes(buf.data(), Size, littleEndian);
            return buf;
        }

        /**
         * @brief 将大数转换为十六进制字符串
         *
         * 生成大数的十六进制表示，不含 "0x" 前缀。
         * 字母使用大写形式（A-F）。
         * 负数会以 "-" 开头。
         *
         * @return 十六进制格式字符串
         *
         * @example BigNumber(255).AsHexStr() 返回 "FF"
         */
        std::string AsHexStr() const;

        /**
         * @brief 将大数转换为十进制字符串
         *
         * 生成大数的十进制表示，人类可读。
         * 负数会以 "-" 开头。
         *
         * @return 十进制格式字符串
         *
         * @example BigNumber(12345).AsDecStr() 返回 "12345"
         */
        std::string AsDecStr() const;

    private:
        /**
         * @brief OpenSSL BIGNUM 结构指针
         *
         * 内部数据存储，由 OpenSSL 库管理。
         * BIGNUM 结构包含：
         *   - d: 指向数字位数数组的指针
         *   - top: 数组中有效元素个数
         *   - dmax: 数组容量
         *   - neg: 符号标志（0=正，1=负）
         *   - flags: 标志位
         *
         * 内存管理：
         *   - 构造时通过 BN_new() 分配
         *   - 析构时通过 BN_free() 释放
         *   - 拷贝时通过 BN_copy() 或 BN_dup() 复制
         */
        struct bignum_st* _bn;

};
#endif
