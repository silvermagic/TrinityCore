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
 * @file BaseEncoding.h
 *
 * @brief 通用Base编码模板基类
 *
 * 本模块提供了Base编码(如Base32、Base64)的通用实现框架。
 *
 * @section encoding_principle 编码原理
 *
 * Base编码是一种将二进制数据转换为可打印ASCII字符的编码方式,主要原理:
 * 1. 将二进制数据按位分组(每组N位,N由具体编码决定)
 * 2. 每个分组映射到一个字符表中的字符
 * 3. 输入数据不足时使用填充字符(如'=')补齐到边界
 *
 * 例如:
 * - Base32: 每5位一组,映射到32个字符(A-Z, 2-7)
 * - Base64: 每6位一组,映射到64个字符(A-Z, a-z, 0-9, +, /)
 *
 * @section usage 使用方式
 *
 * 具体的编码类(如Base32、Base64)需要特化此模板,提供:
 * - BITS_PER_CHAR: 每个编码字符表示的位数
 * - PADDING: 填充字符
 * - DECODE_ERROR: 解码错误标记
 * - Encode()/Decode(): 单字符编码/解码函数
 */

#ifndef TRINITY_BASE_ENCODING_HPP
#define TRINITY_BASE_ENCODING_HPP

#include "Define.h"
#include "Optional.h"
#include <numeric>
#include <string>
#include <vector>

namespace Trinity
{
namespace Impl
{
/**
 * @brief 通用Base编码模板类
 *
 * 提供Base编码/解码的通用实现,通过模板参数支持不同的Base编码方案。
 * 此类实现了位操作的核心算法,处理字节流到字符流的转换。
 *
 * @tparam Encoding 编码策略类型,需提供以下成员:
 *         - BITS_PER_CHAR: 每个编码字符表示的位数(必须小于8)
 *         - PADDING: 填充字符
 *         - DECODE_ERROR: 解码错误标记值
 *         - Encode(uint8): 单字符编码函数
 *         - Decode(char): 单字符解码函数
 */
template <typename Encoding>
struct GenericBaseEncoding
{
    /** @brief 每个编码字符表示的位数(从Encoding策略继承) */
    static constexpr std::size_t BITS_PER_CHAR = Encoding::BITS_PER_CHAR;

    /** @brief 填充边界值,字节数(8位)与编码字符位数的最小公倍数 */
    static constexpr std::size_t PAD_TO = std::lcm(8u, BITS_PER_CHAR);

    // 静态断言:确保编码参数有效(每个字符表示的位数必须小于8位)
    static_assert(BITS_PER_CHAR < 8, "Encoding parameters are invalid");

    /** @brief 解码错误标记值 */
    static constexpr uint8 DECODE_ERROR = Encoding::DECODE_ERROR;

    /** @brief 填充字符 */
    static constexpr char PADDING = Encoding::PADDING;

    /**
     * @brief 计算编码后的字符串长度
     *
     * 根据输入数据的字节数计算编码后字符串的长度(包含填充字符)。
     *
     * 计算过程:
     * 1. 将字节数转换为位数(size * 8)
     * 2. 对齐到填充边界(PAD_TO的倍数)
     * 3. 除以每个字符表示的位数,得到字符数
     *
     * @param size 输入数据的字节数
     * @return 编码后字符串的长度(字符数)
     */
    static constexpr std::size_t EncodedSize(std::size_t size)
    {
        size *= 8; // 转换为位数
        if (size % PAD_TO) // 如果不是边界对齐,则补齐到边界
            size += (PAD_TO - (size % PAD_TO));
        return (size / BITS_PER_CHAR); // 计算字符数
    }

    /**
     * @brief 计算解码后的数据长度
     *
     * 根据编码字符串的长度计算解码后的字节数(理论最大值)。
     *
     * 计算过程:
     * 1. 将字符数转换为位数(size * BITS_PER_CHAR)
     * 2. 对齐到填充边界(PAD_TO的倍数)
     * 3. 除以8,得到字节数
     *
     * @param size 编码字符串的长度(字符数)
     * @return 解码后数据的字节数
     */
    static constexpr std::size_t DecodedSize(std::size_t size)
    {
        size *= BITS_PER_CHAR; // 转换为位数
        if (size % PAD_TO) // 如果不是边界对齐,则补齐到边界
            size += (PAD_TO - (size % PAD_TO));
        return (size / 8); // 计算字节数
    }

    /**
     * @brief 将二进制数据编码为Base编码字符串
     *
     * 将输入的字节数组转换为Base编码字符串,处理位对齐并添加填充字符。
     *
     * @section encode_algorithm 编码算法
     *
     * 1. 按BITS_PER_CHAR位从输入数据中提取位组
     * 2. 将每个位组映射到对应的编码字符
     * 3. 处理跨字节边界的情况
     * 4. 末尾不足部分用填充字符补齐
     *
     * @param data 要编码的二进制数据(字节数组)
     * @return 编码后的字符串,包含填充字符
     */
    static std::string Encode(std::vector<uint8> const& data)
    {
        auto it = data.begin(), end = data.end();
        if (it == end)
            return ""; // 空输入返回空字符串

        std::string s;
        s.reserve(EncodedSize(data.size())); // 预分配结果字符串空间

        uint8 bitsLeft = 8; // 当前字节中剩余未处理的位数
        do
        {
            uint8 thisC = 0; // 当前要编码的位组
            if (bitsLeft >= BITS_PER_CHAR)
            {
                // 情况1: 当前字节中剩余的位数足够提取一个完整的编码字符
                bitsLeft -= BITS_PER_CHAR;
                thisC = ((*it >> bitsLeft) & ((1 << BITS_PER_CHAR)-1)); // 提取BITS_PER_CHAR位
                if (!bitsLeft)
                {
                    // 当前字节已处理完,移动到下一个字节
                    ++it;
                    bitsLeft = 8;
                }
            }
            else
            {
                // 情况2: 需要跨字节提取位组
                thisC = (*it & ((1 << bitsLeft) - 1)) << (BITS_PER_CHAR - bitsLeft); // 从当前字节提取剩余位
                bitsLeft += (8 - BITS_PER_CHAR); // 计算还需要从下一个字节提取的位数
                if ((++it) != end)
                    thisC |= (*it >> bitsLeft); // 从下一个字节补充所需的位
            }
            s.append(1, Encoding::Encode(thisC)); // 将位组编码为字符并追加到结果
        } while (it != end);

        // 添加填充字符:处理末尾未对齐的部分
        while (bitsLeft != 8)
        {
            if (bitsLeft > BITS_PER_CHAR)
                bitsLeft -= BITS_PER_CHAR;
            else
                bitsLeft += (8 - BITS_PER_CHAR);
            s.append(1, PADDING); // 添加填充字符
        }

        return s;
    }

    /**
     * @brief 将Base编码字符串解码为二进制数据
     *
     * 将输入的Base编码字符串解码为原始字节数组,处理填充字符并验证数据有效性。
     *
     * @section decode_algorithm 解码算法
     *
     * 1. 逐个读取编码字符并解码为位组
     * 2. 将位组按8位重组为字节
     * 3. 处理跨字符边界的情况
     * 4. 验证填充字符的正确性
     * 5. 检查尾部不应有非零位
     *
     * @param data 要解码的Base编码字符串
     * @return 解码成功返回字节数组,失败返回空Optional
     *
     * @note 解码失败的情况:
     *       - 包含无效字符
     *       - 填充字符位置错误
     *       - 尾部存在非零位(数据损坏)
     */
    static Optional<std::vector<uint8>> Decode(std::string const& data)
    {
        auto it = data.begin(), end = data.end();
        if (it == end)
            return std::vector<uint8>(); // 空输入返回空数组

        std::vector<uint8> v;
        v.reserve(DecodedSize(data.size())); // 预分配结果向量空间

        uint8 currentByte = 0; // 当前正在组装的字节
        uint8 bitsLeft = 8; // 当前字节还需要填充的位数
        while ((it != end) && (*it != PADDING))
        {
            uint8 cur = Encoding::Decode(*(it++)); // 解码当前字符
            if (cur == DECODE_ERROR)
                return {}; // 遇到无效字符,解码失败

            if (bitsLeft > BITS_PER_CHAR)
            {
                // 情况1: 当前字节还能容纳完整的解码位组
                bitsLeft -= BITS_PER_CHAR;
                currentByte |= (cur << bitsLeft); // 将解码位组左移后填入当前字节
            }
            else
            {
                // 情况2: 解码位组跨越两个字节边界
                bitsLeft = BITS_PER_CHAR - bitsLeft; // 计算有多少位要填入下一个字节
                currentByte |= (cur >> bitsLeft); // 高位填入当前字节
                v.push_back(currentByte); // 当前字节完成,添加到结果
                currentByte = (cur & ((1 << bitsLeft) - 1)); // 提取剩余的低位
                bitsLeft = 8 - bitsLeft; // 计算下一个字节还需要填充的位数
                currentByte <<= bitsLeft; // 将剩余位移到高位
            }
        }

        // 检查尾部非零位:如果currentByte不为0,说明有无效的尾部数据
        if (currentByte)
            return {}; // 解码错误,存在尾部非零位

        // 处理填充字符:验证填充的正确性
        while ((it != end) && (*it == PADDING) && (bitsLeft != 8))
        {
            if (bitsLeft > BITS_PER_CHAR)
                bitsLeft -= BITS_PER_CHAR;
            else
                bitsLeft += (8 - BITS_PER_CHAR);
            ++it;
        }

        // 所有填充字符应已处理完,且到达字符串末尾
        if (it == end)
            return v; // 解码成功

        // 如果还有未处理的字符,说明格式错误
        return {};
    }
};
}
}

#endif
