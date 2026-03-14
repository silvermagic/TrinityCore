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
 * @file SessionKeyGenerator.h
 * @brief 会话密钥生成器模板类
 *
 * 本文件实现了一个基于哈希算法的会话密钥生成器，用于从输入数据派生出任意长度的密钥流。
 * 主要用途：
 * - 生成客户端和服务器之间的会话密钥
 * - 从密码或种子数据派生加密密钥
 * - 生成密钥流用于流式加密
 *
 * 工作原理：
 * 1. 将输入数据分成两部分，分别计算哈希值得到 o1 和 o2
 * 2. 计算 o1、o0、o2 的组合哈希值得到初始的 o0
 * 3. 当需要更多密钥数据时，使用相同的哈希链方式生成新的 o0
 * 4. 每次调用 Generate() 方法，从 o0 中读取字节作为密钥流
 *
 * 这种方法确保：
 * - 从有限的输入数据可以生成无限长度的密钥流
 * - 密钥流的每个部分都依赖于原始输入数据的所有部分
 * - 密钥流具有加密强度，难以预测
 */

#ifndef TRINITY_SESSIONKEYGENERATOR_HPP
#define TRINITY_SESSIONKEYGENERATOR_HPP

#include <cstring>
#include "CryptoHash.h"

/**
 * @class SessionKeyGenerator
 * @brief 会话密钥生成器模板类
 *
 * @tparam Hash 哈希算法类型，如 SHA1、SHA256 等
 *              必须提供 Digest 类型定义和 GetDigestOf() 静态方法
 *
 * 该类使用哈希链技术从输入数据生成密钥流，支持生成任意长度的密钥数据。
 *
 * @example
 * // 使用 SHA1 生成会话密钥
 * std::array<uint8, 16> seed = {...};
 * SessionKeyGenerator<Trinity::Crypto::SHA1> keygen(seed);
 *
 * // 生成 40 字节的会话密钥
 * std::vector<uint8> sessionKey(40);
 * keygen.Generate(sessionKey.data(), sessionKey.size());
 */
template <typename Hash>
class SessionKeyGenerator
{
    public:
        /**
         * @brief 构造函数，从输入数据初始化密钥生成器
         *
         * 输入数据被分成两部分：
         * - 前半部分用于生成 o1
         * - 后半部分用于生成 o2
         * - 然后组合 o1、o0、o2 生成初始的 o0
         *
         * 这种分割确保生成的密钥依赖于输入数据的所有部分，增加了安全性。
         *
         * @tparam C 输入容器类型（支持 std::data() 和 std::size()）
         * @param buf 输入数据缓冲区，作为密钥派生的种子数据
         *            通常包含客户端和服务器交换的随机数、密码哈希等
         *
         * @note 输入数据长度至少应为哈希摘要长度的 2 倍，以获得最佳安全性
         * @note o0 的初始迭代器指向开始位置，准备输出第一个字节
         *
         * @example
         * std::vector<uint8> seed = GetClientServerRandomData();
         * SessionKeyGenerator<SHA1> generator(seed);
         */
        template <typename C>
        SessionKeyGenerator(C const& buf) :
            o0it(o0.begin())
        {
            // 获取输入数据的指针和长度
            uint8 const* data = std::data(buf);
            size_t const len = std::size(buf);

            // 计算中点位置，将输入数据分成两部分
            // 例如：如果输入是 40 字节，halflen = 20
            size_t const halflen = (len / 2);

            // 使用输入数据的前半部分计算 o1
            // o1 = Hash(data[0..halflen-1])
            o1 = Hash::GetDigestOf(data, halflen);

            // 使用输入数据的后半部分计算 o2
            // o2 = Hash(data[halflen..len-1])
            o2 = Hash::GetDigestOf(data + halflen, len - halflen);

            // 使用 o1、o0、o2 的组合计算初始的 o0
            // o0 = Hash(o1 || o0 || o2)
            // 注意：此时 o0 为全零，这是初始化过程
            o0 = Hash::GetDigestOf(o1, o0, o2);
        }

        /**
         * @brief 生成指定长度的密钥字节
         *
         * 从内部状态生成密钥流字节。当 o0 的字节用尽时，自动重新计算 o0：
         * o0_new = Hash(o1 || o0_old || o2)
         *
         * 这形成了一个哈希链，使得：
         * - 每次生成的 o0 都依赖于前一次的 o0
         * - 输出具有连续性，但无法从后续输出推断之前的输出
         * - 可以生成无限长度的密钥流
         *
         * @param buf 输出缓冲区，用于存储生成的密钥字节
         * @param sz 需要生成的密钥字节数量
         *
         * @note 可以多次调用此方法，每次生成需要的字节数
         * @note 内部状态会持续更新，确保密钥流的连续性
         *
         * @example
         * // 生成 20 字节的会话密钥
         * uint8 sessionKey[20];
         * generator.Generate(sessionKey, 20);
         *
         * // 再生成 16 字节的 IV
         * uint8 iv[16];
         * generator.Generate(iv, 16);
         */
        void Generate(uint8* buf, uint32 sz)
        {
            // 逐字节生成密钥流
            for (uint32 i = 0; i < sz; ++i)
            {
                // 检查 o0 是否已经用尽（迭代器到达末尾）
                if (o0it == o0.end())
                {
                    // 重新计算 o0，形成哈希链
                    // 新的 o0 = Hash(o1 || 旧 o0 || o2)
                    // 这确保了密钥流的连续性和不可预测性
                    o0 = Hash::GetDigestOf(o1, o0, o2);

                    // 重置迭代器到新 o0 的开始位置
                    o0it = o0.begin();
                }

                // 从 o0 中读取一个字节到输出缓冲区
                // 使用后置递增，迭代器移动到下一个字节
                buf[i] = *(o0it++);
            }
        }

    private:
        /**
         * @brief 主哈希状态 o0
         *
         * 这是主要的输出状态，Generate() 方法从这里读取密钥字节。
         * 当字节用尽时，使用 o1、o0、o2 重新计算。
         */
        typename Hash::Digest o0 = { };

        /**
         * @brief 辅助哈希状态 o1
         *
         * 从输入数据的前半部分计算得出，在哈希链中作为常量。
         * 确保密钥流依赖于输入数据的前半部分。
         */
        typename Hash::Digest o1 = { };

        /**
         * @brief 辅助哈希状态 o2
         *
         * 从输入数据的后半部分计算得出，在哈希链中作为常量。
         * 确保密钥流依赖于输入数据的后半部分。
         */
        typename Hash::Digest o2 = { };

        /**
         * @brief o0 的当前读取位置迭代器
         *
         * 指向 o0 中下一个要输出的字节。
         * 当迭代器到达 o0.end() 时，需要重新计算 o0。
         */
        typename Hash::Digest::const_iterator o0it;
    };

#endif
