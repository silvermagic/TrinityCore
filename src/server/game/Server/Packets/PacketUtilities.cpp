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
 * @file PacketUtilities.cpp
 * @brief 网络包工具类实现文件
 *
 * 本文件实现了 PacketUtilities.h 中定义的异常类和字符串验证器。
 * 这些组件为网络包处理提供安全保障,防止恶意或格式错误的数据包
 * 进入服务器核心逻辑。
 *
 * 主要功能:
 * 1. 异常类的构造和消息生成
 * 2. UTF-8编码验证
 * 3. 超链接格式验证
 * 4. 数组容量限制检查
 */

#include "PacketUtilities.h"
#include "Hyperlinks.h"
#include <utf8.h>

/**
 * @brief InvalidStringValueException 构造函数实现
 * @param value 无效的字符串值
 *
 * 调用基类 ByteBufferInvalidValueException 的构造函数,
 * 将值类型标识为 "string",并传递无效值用于错误报告。
 */
WorldPackets::InvalidStringValueException::InvalidStringValueException(std::string const& value) : ByteBufferInvalidValueException("string", value.c_str())
{
}

/**
 * @brief InvalidUtf8ValueException 构造函数实现
 * @param value 包含无效UTF-8编码的字符串
 *
 * 调用基类 InvalidStringValueException 的构造函数。
 * 该异常在 UTF-8 验证失败时抛出。
 */
WorldPackets::InvalidUtf8ValueException::InvalidUtf8ValueException(std::string const& value) : InvalidStringValueException(value)
{
}

/**
 * @brief InvalidHyperlinkException 构造函数实现
 * @param value 包含无效超链接的字符串
 *
 * 调用基类 InvalidStringValueException 的构造函数。
 * 该异常在超链接格式验证失败时抛出。
 */
WorldPackets::InvalidHyperlinkException::InvalidHyperlinkException(std::string const& value) : InvalidStringValueException(value)
{
}

/**
 * @brief IllegalHyperlinkException 构造函数实现
 * @param value 包含非法超链接的字符串
 *
 * 调用基类 InvalidStringValueException 的构造函数。
 * 该异常在不允许超链接的位置发现超链接时抛出。
 */
WorldPackets::IllegalHyperlinkException::IllegalHyperlinkException(std::string const& value) : InvalidStringValueException(value)
{
}

/**
 * @brief UTF-8编码验证实现
 * @param value 待验证的字符串
 * @return 如果UTF-8编码有效则返回 true
 * @throws InvalidUtf8ValueException 如果字符串包含无效的UTF-8编码
 *
 * 使用 utf8cpp 库验证字符串是否为有效的UTF-8编码。
 * 验证包括:
 * - 检查字节序列是否符合UTF-8编码规则
 * - 检查是否包含无效的代理对
 * - 检查是否包含过长的编码序列
 *
 * @note UTF-8验证对于防止字符编码攻击和确保字符串正确显示至关重要
 */
bool WorldPackets::Strings::Utf8::Validate(std::string const& value)
{
    // 使用 utf8cpp 库验证整个字符串的UTF-8编码有效性
    if (!utf8::is_valid(value.begin(), value.end()))
        throw InvalidUtf8ValueException(value);  // 编码无效时抛出异常
    return true;
}

/**
 * @brief 超链接验证实现
 * @param value 待验证的字符串
 * @return 如果所有超链接格式正确则返回 true
 * @throws InvalidHyperlinkException 如果字符串包含格式错误的超链接
 *
 * 调用 Trinity::Hyperlinks::CheckAllLinks 检查字符串中的所有超链接。
 * 允许存在超链接,但要求所有超链接符合游戏定义的格式。
 *
 * 超链接格式示例: |cff00ff00|Hitem:12345:0:0:0:0:0:0:0:80|h[物品名称]|h|r
 *
 * 验证内容:
 * - 颜色标记(|cffRRGGBB)是否正确
 * - 链接类型是否受支持(item, spell, quest等)
 * - 链接参数格式是否正确
 * - 链接是否正确关闭(|h, |r)
 *
 * @note 此验证器允许超链接存在,仅验证其格式正确性
 */
bool WorldPackets::Strings::Hyperlinks::Validate(std::string const& value)
{
    // 检查所有超链接是否格式正确
    if (!Trinity::Hyperlinks::CheckAllLinks(value))
        throw InvalidHyperlinkException(value);  // 超链接格式错误时抛出异常
    return true;
}

/**
 * @brief 禁止超链接验证实现
 * @param value 待验证的字符串
 * @return 如果字符串不包含超链接则返回 true
 * @throws IllegalHyperlinkException 如果字符串包含超链接标记
 *
 * 检查字符串中是否包含超链接标记字符 '|'。
 * 在某些上下文中(如玩家名称、纯文本输入),不允许任何超链接。
 *
 * 这是一种简单的启发式检查,仅查找 '|' 字符。
 * 如果发现该字符,说明可能存在超链接注入尝试。
 *
 * @note 此验证器完全禁止超链接,适用于不允许富文本的场景
 * @note '|' 是WoW中超链接语法的特殊字符,所有超链接都以它开头
 */
bool WorldPackets::Strings::NoHyperlinks::Validate(std::string const& value)
{
    // 在字符串中查找超链接标记字符 '|'
    if (value.find('|') != std::string::npos)
        throw IllegalHyperlinkException(value);  // 发现超链接标记时抛出异常
    return true;
}

/**
 * @brief PacketArrayMaxCapacityException 构造函数实现
 * @param requestedSize 客户端请求的数组大小
 * @param sizeLimit 允许的最大容量限制
 *
 * 构造详细的错误消息,说明客户端尝试读取的数组大小超过了安全限制。
 * 错误消息包含具体的请求数量和限制数量,便于调试和安全审计。
 *
 * 错误消息示例:
 * "Attempted to read more array elements from packet 1000 than allowed 100"
 *
 * @note 此异常是防止"循环计数器欺骗"攻击的关键防御机制
 * @note 恶意客户端可能在数据包中声明巨大的数组大小,试图:
 *       1. 导致服务器分配过多内存
 *       2. 导致服务器执行过多的循环迭代(DoS攻击)
 *       3. 触发缓冲区溢出漏洞
 */
WorldPackets::PacketArrayMaxCapacityException::PacketArrayMaxCapacityException(std::size_t requestedSize, std::size_t sizeLimit)
{
    // 构建详细的错误消息,包含请求的大小和允许的限制
    message().assign("Attempted to read more array elements from packet " + Trinity::ToString(requestedSize) + " than allowed " + Trinity::ToString(sizeLimit));
}
