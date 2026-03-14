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
 * @file Base64.cpp
 * @brief Base64 编码实现
 *
 * Base64 是一种基于 64 个可打印字符来表示二进制数据的编码方法。
 * 常用于在需要处理文本数据的场合存储和传输二进制数据。
 *
 * 编码表：
 *   A-Z (0-25), a-z (26-51), 0-9 (52-61), + (62), / (63)
 *
 * 编码原理：
 *   将 3 个字节（24 位）的数据转换为 4 个 Base64 字符（每个字符 6 位）
 *   如果输入数据不足 3 字节，使用 '=' 进行填充
 */

#include "Base64.h"
#include "BaseEncoding.h"
#include "Errors.h"

/**
 * @struct B64Impl
 * @brief Base64 编解码核心实现结构体
 *
 * 提供Base64编码所需的字符映射表和编解码函数，
 * 作为模板参数传递给 GenericBaseEncoding 通用编解码模板类。
 */
struct B64Impl
{
    /**
     * @brief 每个Base64字符表示的位数
     *
     * Base64 使用 6 位来表示一个字符（2^6 = 64 种可能）
     */
    static constexpr std::size_t BITS_PER_CHAR = 6;

    /**
     * @brief 填充字符
     *
     * 当输入数据长度不是 3 的倍数时，使用 '=' 进行填充
     */
    static constexpr char PADDING = '=';

    /**
     * @brief 将 6 位数值编码为 Base64 字符
     *
     * @param v 6 位数值（范围：0-63）
     * @return 对应的 Base64 字符
     *
     * 编码规则：
     *   0-25  -> 'A'-'Z' (大写字母)
     *   26-51 -> 'a'-'z' (小写字母)
     *   52-61 -> '0'-'9' (数字)
     *   62    -> '+'     (加号)
     *   63    -> '/'     (斜杠)
     *
     * 主要流程：
     *   1. 断言检查输入值范围是否有效（0-63）
     *   2. 根据数值范围返回对应的字符
     */
    static constexpr char Encode(uint8 v)
    {
        ASSERT(v < 0x40);  // 确保值在 0-63 范围内
        if (v < 26)  return 'A' + v;           // 0-25 映射到 A-Z
        if (v < 52)  return 'a' + (v - 26);    // 26-51 映射到 a-z
        if (v < 62)  return '0' + (v - 52);    // 52-61 映射到 0-9
        if (v == 62) return '+';               // 62 映射到 +
        else         return '/';               // 63 映射到 /
    }

    /**
     * @brief 解码错误标志值
     *
     * 当输入字符不是有效的 Base64 字符时，Decode 函数返回此值
     */
    static constexpr uint8 DECODE_ERROR = 0xff;

    /**
     * @brief 将 Base64 字符解码为 6 位数值
     *
     * @param v 待解码的 ASCII 字符
     * @return 解码后的 6 位数值（0-63），无效字符返回 DECODE_ERROR (0xff)
     *
     * 解码规则：
     *   'A'-'Z' -> 0-25
     *   'a'-'z' -> 26-51
     *   '0'-'9' -> 52-61
     *   '+'     -> 62
     *   '/'     -> 63
     *   其他    -> DECODE_ERROR
     *
     * 主要流程：
     *   1. 检查字符是否在大写字母范围内，计算对应值
     *   2. 检查字符是否在小写字母范围内，计算对应值
     *   3. 检查字符是否在数字范围内，计算对应值
     *   4. 检查是否为 '+' 或 '/'，返回对应值
     *   5. 其他字符均视为无效，返回 DECODE_ERROR
     */
    static constexpr uint8 Decode(uint8 v)
    {
        if (('A' <= v) && (v <= 'Z')) return (v - 'A');         // A-Z -> 0-25
        if (('a' <= v) && (v <= 'z')) return (v - 'a') + 26;    // a-z -> 26-51
        if (('0' <= v) && (v <= '9')) return (v - '0') + 52;    // 0-9 -> 52-61
        if (v == '+') return 62;                                 // + -> 62
        if (v == '/') return 63;                                 // / -> 63
        return DECODE_ERROR;                                     // 无效字符
    }
};

/**
 * @brief 将二进制数据编码为 Base64 字符串
 *
 * @param data 待编码的二进制数据向量
 * @return Base64 编码后的字符串
 *
 * 职责：
 *   将任意二进制数据转换为 Base64 编码的字符串表示形式，
 *   便于在仅支持文本的协议中传输二进制数据。
 *
 * 主要流程：
 *   调用 GenericBaseEncoding 模板类的 Encode 方法，
 *   使用 B64Impl 提供的字符映射规则进行编码。
 */
/*static*/ std::string Trinity::Encoding::Base64::Encode(std::vector<uint8> const& data)
{
    return Trinity::Impl::GenericBaseEncoding<B64Impl>::Encode(data);
}

/**
 * @brief 将 Base64 字符串解码为二进制数据
 *
 * @param data Base64 编码的字符串
 * @return 解码成功返回包含二进制数据的 Optional，失败返回空 Optional
 *
 * 职责：
 *   将 Base64 编码的字符串还原为原始二进制数据，
 *   如果输入包含非法字符则解码失败。
 *
 * 主要流程：
 *   调用 GenericBaseEncoding 模板类的 Decode 方法，
 *   使用 B64Impl 提供的字符解码规则进行解码。
 *
 * 注意事项：
 *   - 输入字符串应仅包含有效的 Base64 字符和填充字符 '='
 *   - 返回 Optional 类型，调用者需要检查是否解码成功
 */
/*static*/ Optional<std::vector<uint8>> Trinity::Encoding::Base64::Decode(std::string const& data)
{
    return Trinity::Impl::GenericBaseEncoding<B64Impl>::Decode(data);
}
