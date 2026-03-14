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
 * @file Base64.h
 *
 * @brief Base64编码/解码工具类
 *
 * 本模块实现了RFC 4648标准的Base64编码和解码功能。
 *
 * @section base64_principle Base64编码原理
 *
 * Base64编码将二进制数据转换为可打印的ASCII字符,具有以下特点:
 *
 * - 字符集: A-Z(0-25), a-z(26-51), 0-9(52-61), +(62), /(63) 共64个字符
 * - 编码方式: 每6位二进制数据映射为一个Base64字符
 * - 填充字符: '=' 用于末尾对齐(最多2个)
 * - 数据膨胀: 编码后数据量约为原始数据的4/3倍(约133%)
 *
 * @section base64_example 编码示例
 *
 * 原始数据(二进制): 01001000 01100101 01101100 01101100 01101111
 * 按6位分组:        010010 000110 010101 101100 011011 000110 1111???
 * 编码字符:         S      G      V      s      b      G      8      =
 *
 * @section base64_usage 使用场景
 *
 * Base64编码常用于:
 * - 电子邮件附件传输
 * - 在JSON/XML中嵌入二进制数据
 * - HTTP认证头(Basic Auth)
 * - 数据URL(Data URI)
 * - 加密数据的文本表示
 *
 * @note Base64编码不是加密,仅是一种编码方式,不能保证数据安全性
 */

#ifndef TRINITY_BASE64_H
#define TRINITY_BASE64_H

#include "Define.h"
#include "Optional.h"
#include <string>
#include <vector>

namespace Trinity
{
namespace Encoding
{
/**
 * @brief Base64编码/解码工具类
 *
 * 提供静态方法用于Base64编码和解码操作,符合RFC 4648标准。
 *
 * @note 本类不可实例化,所有方法均为静态方法
 *
 * @see https://tools.ietf.org/html/rfc4648 RFC 4648标准文档
 */
struct TC_COMMON_API Base64
{
    /**
     * @brief 将二进制数据编码为Base64字符串
     *
     * 将输入的字节数组编码为Base64格式的字符串,自动处理填充字符。
     *
     * @param data 要编码的二进制数据(字节数组)
     * @return Base64编码字符串,包含必要的填充字符('=')
     *
     * @note 编码规则:
     *       - 每6位二进制数据映射为一个Base64字符
     *       - 字符集: A-Z, a-z, 0-9, +, /
     *       - 末尾使用'='填充到4字节的倍数(最多2个)
     *
     * @par 示例:
     * @code
     * std::vector<uint8> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
     * std::string encoded = Base64::Encode(data);
     * // 结果: "SGVsbG8="
     * @endcode
     */
    static std::string Encode(std::vector<uint8> const& data);

    /**
     * @brief 将Base64字符串解码为二进制数据
     *
     * 将Base64编码的字符串解码为原始字节数组,自动验证和移除填充字符。
     *
     * @param data Base64编码字符串(可包含填充字符'=')
     * @return 解码成功返回包含字节数组的Optional,失败返回空Optional
     *
     * @note 解码失败的可能原因:
     *       - 包含无效字符(非A-Z, a-z, 0-9, +, /, '=')
     *       - 填充字符位置或数量错误
     *       - 数据损坏导致尾部存在非零位
     *
     * @par 示例:
     * @code
     * std::string encoded = "SGVsbG8=";
     * Optional<std::vector<uint8>> decoded = Base64::Decode(encoded);
     * if (decoded)
     * {
     *     // 解码成功,使用decoded.value()
     *     std::string text(decoded->begin(), decoded->end());
     *     // text == "Hello"
     * }
     * else
     * {
     *     // 解码失败
     * }
     * @endcode
     */
    static Optional<std::vector<uint8>> Decode(std::string const& data);
};
}
}

#endif
