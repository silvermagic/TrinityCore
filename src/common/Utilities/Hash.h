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
 * @file Hash.h
 * @brief 哈希工具函数模块
 *
 * 本文件提供了一系列哈希相关的工具函数和扩展，主要包括：
 * - 哈希值组合函数 (hash_combine)：用于将多个值组合成单一哈希值
 * - FNV-1a 哈希算法实现：用于字符串视图的快速哈希计算
 * - std::pair 的哈希特化：扩展标准库以支持 pair 类型的哈希
 *
 * 这些工具广泛应用于容器（如 unordered_map、unordered_set）的键哈希计算，
 * 以及需要快速哈希值的场景。
 */

#ifndef TrinityCore_Hash_h__
#define TrinityCore_Hash_h__

#include <functional>
#include <string_view>
#include <utility>

namespace Trinity
{
    /**
     * @brief 组合哈希值
     *
     * 将给定值的哈希组合到现有的哈希种子中。该函数使用基于
     * boost::hash_combine 的算法，通过位操作和魔数来分散哈希值，
     * 以减少哈希冲突。
     *
     * 算法细节：
     * - 使用 std::hash 计算输入值的哈希
     * - 添加魔数 0x9E3779B9（黄金比例常数的派生值）增加随机性
     * - 通过位移操作（左移6位、右移2位）进一步分散比特位
     * - 使用异或操作混合到种子中
     *
     * @tparam T 要哈希的值类型，必须支持 std::hash
     * @param[in,out] seed 哈希种子，函数会将新值组合到此种子中
     * @param[in] val 要组合到种子中的值
     *
     * @note 该函数会修改 seed 参数
     * @note 时间复杂度：O(1)（假设 std::hash 为常数时间）
     * @note 空间复杂度：O(1)
     *
     * @warning 该哈希组合方法不是加密安全的的，仅用于哈希表等非安全场景
     *
     * @par 使用示例：
     * @code
     * size_t hash = 0;
     * Trinity::hash_combine(hash, value1);
     * Trinity::hash_combine(hash, value2);
     * // hash 现在包含 value1 和 value2 的组合哈希
     * @endcode
     */
    template<typename T>
    inline void hash_combine(std::size_t& seed, T const& val)
    {
        // 使用魔数和位移操作来增强哈希的分散性
        // 0x9E3779B9 是黄金比例常数的派生值，有助于产生更好的哈希分布
        seed ^= std::hash<T>()(val) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
    }

    /**
     * @brief 使用 FNV-1a 算法计算字符串视图的哈希值
     *
     * 实现 Fowler-Noll-Vo 哈希函数的 FNV-1a 变体，专为字符串数据设计。
     * 该算法简单高效，具有良好的分散性和较低的碰撞率。
     *
     * FNV-1a 算法流程：
     * 1. 初始化哈希值为 FNV offset basis (0x811C9DC5)
     * 2. 对每个字节：先异或到哈希值，再乘以 FNV prime (0x01000193)
     * 3. 返回最终哈希值
     *
     * FNV-1a vs FNV-1:
     * - FNV-1a 先异或后乘法（本实现）
     * - FNV-1 先乘法后异或
     * - FNV-1a 通常产生更好的分散性
     *
     * @param[in] data 要计算哈希的字符串视图
     * @return 计算得到的 32 位无符号哈希值
     *
     * @note 时间复杂度：O(n)，其中 n 为字符串长度
     * @note 空间复杂度：O(1)
     * @note 本实现为 32 位版本，适用于短字符串和标识符
     *
     * @warning FNV-1a 不是加密安全的哈希函数，不应用于安全敏感场景
     *
     * @par 使用示例：
     * @code
     * uint32_t hash1 = Trinity::HashFnv1a("player_name");
     * uint32_t hash2 = Trinity::HashFnv1a(std::string_view(buffer, length));
     * @endcode
     *
     * @par 性能注意事项：
     * - 对于非常长的字符串，考虑使用 64 位 FNV 变体或更快的哈希算法
     * - 编译器通常能够优化此函数的简单循环
     */
    inline std::uint32_t HashFnv1a(std::string_view data)
    {
        // FNV offset basis: 初始哈希值
        // 0x811C9DC5 是 FNV-1a 32位版本的标准初始值
        std::uint32_t hash = 0x811C9DC5u;

        // 逐字节处理：先异或后乘法（FNV-1a 的特征）
        for (char c : data)
        {
            hash ^= c;              // 异或当前字节到哈希值
            hash *= 0x1000193u;     // 乘以 FNV prime: 0x01000193
        }

        return hash;
    }
}

/**
 * @brief std::pair 的哈希特化实现
 *
 * 扩展标准库以支持 std::pair 作为 unordered_set 和 unordered_map 的键。
 * 该特化通过组合两个元素的哈希值来生成 pair 的哈希值。
 *
 * 实现原理：
 * - 使用 Trinity::hash_combine 依次组合两个元素的哈希
 * - 保证相同的 pair 值产生相同的哈希
 * - 尽量减少不同 pair 产生相同哈希的概率
 *
 * @tparam K pair 的第一个元素类型（必须支持 std::hash）
 * @tparam V pair 的第二个元素类型（必须支持 std::hash）
 *
 * @par 要求：
 * - K 类型必须可用 std::hash 哈希
 * - V 类型必须可用 std::hash 哈希
 *
 * @par 使用示例：
 * @code
 * std::unordered_set<std::pair<int, std::string>> set;
 * set.insert({1, "test"});  // 现在可以正常工作
 *
 * std::unordered_map<std::pair<uint32_t, uint32_t>, PlayerData> playerMap;
 * playerMap[{zoneId, playerId}] = data;
 * @endcode
 *
 * @note 添加到 std 命名空间是合法的，因为这是对标准库类型的用户定义特化
 */
namespace std
{
    template<class K, class V>
    struct hash<std::pair<K, V>>
    {
    public:
        /**
         * @brief 计算 pair 的哈希值
         *
         * 通过组合两个元素的哈希值来生成单一哈希值。
         *
         * @param[in] p 要计算哈希的 pair 对象
         * @return pair 的哈希值
         *
         * @note 时间复杂度：取决于 K 和 V 的哈希函数复杂度，通常为 O(1)
         * @note 空间复杂度：O(1)
         */
        size_t operator()(std::pair<K, V> const& p) const
        {
            size_t hashVal = 0;
            // 组合第一个元素的哈希
            Trinity::hash_combine(hashVal, p.first);
            // 组合第二个元素的哈希
            Trinity::hash_combine(hashVal, p.second);
            return hashVal;
        }
    };
}

#endif // TrinityCore_Hash_h__
