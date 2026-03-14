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
 * @file Base32.h
 *
 * @brief Base32编码/解码工具类
 *
 * 本模块实现了RFC 4648标准的Base32编码和解码功能。
 *
 * @section base32_principle Base32编码原理
 *
 * Base32编码将二进制数据转换为可打印的ASCII字符,具有以下特点:
 *
 * - 字符集: A-Z(0-25), 2-7(26-31) 共32个字符
 * - 编码方式: 每5位二进制数据映射为一个Base32字符
 * - 填充字符: '=' 用于末尾对齐
 * - 数据膨胀: 编码后数据量约为原始数据的8/5倍(约160%)
 *
 * @section base32_example 编码示例
 *
 * 原始数据(二进制): 01001000 01100101 01101100 01101100 01101111
 * 按5位分组:        01001 00011 00101 01101 10011 01100 11011 11???
 * 编码字符:         J     D     U     2     T     M     4     =
 *
 * @section base32_usage 使用场景
 *
 * Base32编码常用于:
 * - 需要不区分大小写的场景
 * - 人类可读的编码(如验证码、序列号)
 * - 对URL友好的编码(避免使用特殊字符)
 */

#ifndef TRINITY_BASE32_H
#define TRINITY_BASE32_H

#include "Define.h"
#include "Optional.h"
#include <string>
#include <vector>

namespace Trinity
{
namespace Encoding
{
/**
 * @brief Base32编码/解码工具类
 *
 * 提供静态方法用于Base32编码和解码操作,符合RFC 4648标准。
 *
 * @note 本类不可实例化,所有方法均为静态方法
 *
 * @see https://tools.ietf.org/html/rfc4648 RFC 4648标准文档
 */
struct TC_COMMON_API Base32
{
    /**
     * @brief 将二进制数据编码为Base32字符串
     *
     * 将输入的字节数组编码为Base32格式的字符串,自动处理填充字符。
     *
     * @param data 要编码的二进制数据(字节数组)
     * @return Base32编码字符串,包含必要的填充字符('=')
     *
     * @note 编码规则:
     *       - 每5位二进制数据映射为一个Base32字符
     *       - 字符集: A-Z(表示0-25), 2-7(表示26-31)
     *       - 末尾使用'='填充到8字节的倍数
     *
     * @par 示例:
     * @code
     * std::vector<uint8> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
     * std::string encoded = Base32::Encode(data);
     * // 结果: "JBSWY3DPEE======"
     * @endcode
     */
    static std::string Encode(std::vector<uint8> const& data);

    /**
     * @brief 将Base32字符串解码为二进制数据
     *
     * 将Base32编码的字符串解码为原始字节数组,自动验证和移除填充字符。
     *
     * @param data Base32编码字符串(可包含填充字符'=')
     * @return 解码成功返回包含字节数组的Optional,失败返回空Optional
     *
     * @note 解码失败的可能原因:
     *       - 包含无效字符(非A-Z, 2-7, '=')
     *       - 填充字符位置或数量错误
     *       - 数据损坏导致尾部存在非零位
     *
     * @par 示例:
     * @code
     * std::string encoded = "JBSWY3DPEE======";
     * Optional<std::vector<uint8>> decoded = Base32::Decode(encoded);
     * if (decoded)
     * {
     *     // 解码成功,使用decoded.value()
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
