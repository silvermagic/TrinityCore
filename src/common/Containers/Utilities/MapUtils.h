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
 * @file MapUtils.h
 * @brief Map 容器工具函数库
 *
 * 本模块提供了一系列用于操作 Map 容器的实用工具函数，简化常见的 Map 操作。
 * 主要功能包括：
 * - 安全地从 Map 中获取值的指针（自动处理值类型和指针类型）
 * - 从 Multimap 中删除特定键值对
 *
 * 这些工具函数封装了常见的 Map 操作模式，提高代码可读性和安全性。
 * 所有函数都使用模板实现，支持不同类型的 Map 容器。
 */

#ifndef TRINITYCORE_MAP_UTILS_H
#define TRINITYCORE_MAP_UTILS_H

#include <type_traits>

namespace Trinity::Containers
{
/**
 * @brief 从 Map 中获取映射值的指针
 *
 * 根据键查找 Map 中的值，并返回指向该值的指针。
 * 该函数会智能地处理 Map 存储值类型和存储指针类型的情况：
 * - 如果 Map 存储的是指针（mapped_type 是指针类型），则直接返回该指针
 * - 如果 Map 存储的是值，则返回该值的地址
 *
 * 这种设计避免了在使用前需要判断 Map 存储类型的问题，统一了接口。
 *
 * @tparam M Map 容器类型（支持 std::map, std::unordered_map 等）
 * @param map 要查找的 Map 容器引用
 * @param key 要查找的键
 * @return auto 指向映射值的指针，如果键不存在则返回 nullptr
 *
 * @note 调用时机：
 *       - 需要安全地访问 Map 中的值而不确定键是否存在时
 *       - 需要获取值的指针进行后续操作时
 *       - 处理不确定存储值类型还是指针类型的模板代码时
 *
 * @note 性能说明：
 *       - 时间复杂度：O(log n) 对于有序 Map，O(1) 平均对于无序 Map
 *       - 无内存分配，仅进行查找操作
 *
 * @example
 * std::map<int, std::string> valueMap;
 * std::map<int, std::string*> ptrMap;
 *
 * auto ptr1 = MapGetValuePtr(valueMap, 1);  // 返回 std::string*
 * auto ptr2 = MapGetValuePtr(ptrMap, 1);    // 返回 std::string*
 */
template<class M>
auto MapGetValuePtr(M& map, typename M::key_type const& key)
{
    // 在 Map 中查找指定的键
    auto itr = map.find(key);

    // 使用编译时 if 判断 mapped_type 是否为指针类型
    if constexpr (std::is_pointer_v<typename M::mapped_type>)
        // 如果存储的是指针，直接返回该指针（或 nullptr）
        return itr != map.end() ? itr->second : nullptr;
    else
        // 如果存储的是值，返回值的地址（或 nullptr）
        return itr != map.end() ? &itr->second : nullptr;
}

/**
 * @brief 从 Multimap 中删除指定的键值对
 *
 * 在 Multimap 中查找所有具有指定键的条目，并删除其中值等于指定值的条目。
 * 这是对 Multimap 操作的常见模式封装，因为 Multimap 的 equal_range 返回一个范围，
 * 需要遍历该范围来找到并删除特定的键值对。
 *
 * 该函数支持各种 Multimap 类型，包括 std::multimap 和 std::unordered_multimap。
 *
 * @tparam K 键类型
 * @tparam V 值类型
 * @tparam M Multimap 容器模板
 * @tparam Rest 其他模板参数（如比较器、分配器等）
 * @param multimap 要操作的 Multimap 容器引用
 * @param key 要删除条目的键
 * @param value 要删除条目的值（只有键和值都匹配的条目才会被删除）
 *
 * @note 调用时机：
 *       - 需要从 Multimap 中精确删除某个键值对时
 *       - Multimap 中同一键可能对应多个值，只想删除其中一个时
 *
 * @note 性能说明：
 *       - 时间复杂度：O(m) 其中 m 是具有相同键的条目数量
 *       - 需要遍历所有具有相同键的条目进行比较
 *       - erase 操作可能导致迭代器失效，函数内部已正确处理
 *
 * @example
 * std::multimap<int, std::string> mmap;
 * mmap.insert({1, "a"});
 * mmap.insert({1, "b"});
 * mmap.insert({1, "c"});
 *
 * MultimapErasePair(mmap, 1, "b");  // 删除键为 1 且值为 "b" 的条目
 */
template<class K, class V, template<class, class, class...> class M, class... Rest>
void MultimapErasePair(M<K, V, Rest...>& multimap, K const& key, V const& value)
{
    // 获取所有具有指定键的条目范围
    auto range = multimap.equal_range(key);

    // 遍历该范围内的所有条目
    for (auto itr = range.first; itr != range.second;)
    {
        // 检查当前条目的值是否等于目标值
        if (itr->second == value)
            // 找到匹配的条目，删除它
            // erase 返回下一个有效迭代器，赋值给 itr 继续循环
            itr = multimap.erase(itr);
        else
            // 不匹配，继续检查下一个条目
            ++itr;
    }
}

} // namespace Trinity::Containers

#endif // TRINITYCORE_MAP_UTILS_H
