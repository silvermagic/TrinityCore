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
 * @file Base32.cpp
 * @brief Base32 编码实现
 *
 * Base32 是一种用 32 个可打印字符来表示二进制数据的编码方式。
 * 每 5 个比特位编码为一个字符，因此编码后数据量会增加约 37%。
 *
 * Base32 字符集：
 * - A-Z (值 0-25)
 * - 2-7 (值 26-31)
 *
 * 与 Base64 相比，Base32 虽然编码效率较低，但更适合不区分大小写的环境，
 * 且避免了容易混淆的字符（如 0/O, 1/I/l 等）。
 */

#include "Base32.h"
#include "BaseEncoding.h"
#include "Errors.h"

/**
 * @struct B32Impl
 * @brief Base32 编码实现策略结构体
 *
 * 该结构体定义了 Base32 编码的核心参数和转换函数，
 * 作为模板参数传递给 GenericBaseEncoding 实现具体的编码/解码逻辑。
 */
struct B32Impl
{
    /**
     * @brief 每个编码字符表示的比特位数
     *
     * Base32 编码中，每个字符表示 5 个比特位（2^5 = 32 种可能）。
     */
    static constexpr std::size_t BITS_PER_CHAR = 5;

    /**
     * @brief 填充字符
     *
     * 当输入数据长度不是 5 的倍数时，使用此字符进行填充。
     */
    static constexpr char PADDING = '=';

    /**
     * @brief 将 5 位值编码为 Base32 字符
     *
     * @param v 5 位无符号整数值（范围：0-31）
     * @return 编码后的 Base32 字符
     *
     * @note 编码规则：
     *       - 值 0-25 -> 字符 'A'-'Z'
     *       - 值 26-31 -> 字符 '2'-'7'
     *
     * @par 主要流程：
     * 1. 断言检查值必须在有效范围内（< 32）
     * 2. 如果值 < 26，映射到字母 'A'-'Z'
     * 3. 否则映射到数字 '2'-'7'
     */
    static constexpr char Encode(uint8 v)
    {
        ASSERT(v < 0x20);
        if (v < 26) return 'A'+v;
        else        return '2' + (v-26);
    }

    /**
     * @brief 解码错误标记值
     *
     * 当解码遇到无效字符时，返回此值表示错误。
     */
    static constexpr uint8 DECODE_ERROR = 0xff;

    /**
     * @brief 将 Base32 字符解码为 5 位值
     *
     * @param v 要解码的字符
     * @return 解码后的 5 位值（0-31），或 DECODE_ERROR 表示无效字符
     *
     * @note 解码规则（按 RFC 4648，包含容错处理）：
     *       - 'A'-'Z' 或 'a'-'z' -> 值 0-25（不区分大小写）
     *       - '2'-'7' -> 值 26-31
     *       - 容错映射：'0'->'O', '1'->'I'/'L', '8'->'B'
     *
     * @par 主要流程：
     * 1. 对容易混淆的字符进行容错转换：
     *    - '0' (零) -> 'O' (字母O)
     *    - '1' (一) -> 'l' (小写L)
     *    - '8' (八) -> 'B' (字母B)
     * 2. 大写字母 'A'-'Z' -> 值 0-25
     * 3. 小写字母 'a'-'z' -> 值 0-25
     * 4. 数字 '2'-'7' -> 值 26-31
     * 5. 其他字符返回 DECODE_ERROR
     */
    static constexpr uint8 Decode(uint8 v)
    {
        if (v == '0') return Decode('O');
        if (v == '1') return Decode('l');
        if (v == '8') return Decode('B');
        if (('A' <= v) && (v <= 'Z')) return (v-'A');
        if (('a' <= v) && (v <= 'z')) return (v-'a');
        if (('2' <= v) && (v <= '7')) return (v-'2')+26;
        return DECODE_ERROR;
    }
};

/**
 * @brief 将二进制数据编码为 Base32 字符串
 *
 * @param data 要编码的二进制数据向量
 * @return Base32 编码后的字符串
 *
 * @par 职责：
 * 将输入的二进制数据按照 Base32 编码规则转换为可打印的 ASCII 字符串。
 *
 * @par 主要流程：
 * 1. 调用通用 Base 编码模板类 GenericBaseEncoding
 * 2. 使用 B32Impl 策略进行实际的编码转换
 * 3. 返回编码后的字符串
 */
/*static*/ std::string Trinity::Encoding::Base32::Encode(std::vector<uint8> const& data)
{
    return Trinity::Impl::GenericBaseEncoding<B32Impl>::Encode(data);
}

/**
 * @brief 将 Base32 字符串解码为二进制数据
 *
 * @param data Base32 编码的字符串
 * @return 解码成功返回包含二进制数据的 Optional，解码失败返回空 Optional
 *
 * @par 职责：
 * 将 Base32 编码的字符串还原为原始的二进制数据。
 * 如果输入字符串包含无效字符或格式错误，返回空 Optional。
 *
 * @par 主要流程：
 * 1. 调用通用 Base 解码模板类 GenericBaseEncoding
 * 2. 使用 B32Impl 策略进行实际的解码转换
 * 3. 验证输入数据的有效性
 * 4. 成功返回解码后的二进制数据，失败返回空 Optional
 */
/*static*/ Optional<std::vector<uint8>> Trinity::Encoding::Base32::Decode(std::string const& data)
{
    return Trinity::Impl::GenericBaseEncoding<B32Impl>::Decode(data);
}
