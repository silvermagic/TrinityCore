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
 * @file IteratorPair.h
 * @brief 迭代器对工具类模块
 *
 * 本模块提供了 IteratorPair 工具类，用于简化对 multimap::equal_range 返回结果的范围遍历操作。
 * 通过封装迭代器对，支持现代 C++ 的范围 for 循环语法，使代码更加简洁易读。
 * 主要用于遍历 multimap 中具有相同键的所有元素。
 */

#ifndef IteratorPair_h__
#define IteratorPair_h__

#include "Define.h"
#include <utility>

namespace Trinity
{
    /**
     * @class IteratorPair
     * @brief 迭代器对封装类
     *
     * 该类封装了一对迭代器（begin 和 end），使其能够用于范围 for 循环。
     * 主要应用场景是处理 multimap::equal_range() 返回的迭代器对，
     * 允许以更简洁的方式遍历 multimap 中具有相同键的所有元素。
     *
     * @tparam iterator 迭代器类型
     * @tparam end_iterator 结束迭代器类型，默认与 iterator 相同
     *
     * @example
     * // 传统写法:
     * auto range = myMultimap.equal_range(key);
     * for (auto it = range.first; it != range.second; ++it) { ... }
     *
     * // 使用 IteratorPair 的简洁写法:
     * for (auto& elem : MapEqualRange(myMultimap, key)) { ... }
     */
    template<class iterator, class end_iterator = iterator>
    class IteratorPair
    {
    public:
        /**
         * @brief 默认构造函数
         *
         * 构造一个空的迭代器对，两个迭代器都使用默认值初始化。
         * 主要用于延迟赋值的场景。
         */
        constexpr IteratorPair() : _iterators() { }

        /**
         * @brief 通过两个迭代器构造
         * @param first 起始迭代器
         * @param second 结束迭代器
         *
         * 直接传入 begin 和 end 迭代器进行构造。
         */
        constexpr IteratorPair(iterator first, end_iterator second) : _iterators(first, second) { }

        /**
         * @brief 通过 std::pair 构造
         * @param iterators 包含两个迭代器的 pair 对象
         *
         * 接受 std::pair 类型参数，常用于接收 equal_range() 的返回值。
         */
        constexpr IteratorPair(std::pair<iterator, end_iterator> iterators) : _iterators(iterators) { }

        /**
         * @brief 获取起始迭代器
         * @return 返回范围的起始迭代器
         *
         * 用于范围 for 循环的开始位置。
         */
        constexpr iterator begin() const { return _iterators.first; }

        /**
         * @brief 获取结束迭代器
         * @return 返回范围的结束迭代器
         *
         * 用于范围 for 循环的结束位置。
         */
        constexpr end_iterator end() const { return _iterators.second; }

    private:
        std::pair<iterator, end_iterator> _iterators;  ///< 存储的迭代器对，first 为起始，second 为结束
    };

    /**
     * @namespace Containers
     * @brief 容器辅助工具命名空间
     *
     * 提供创建 IteratorPair 的便捷函数，简化常用操作。
     */
    namespace Containers
    {
        /**
         * @brief 创建 IteratorPair 对象（通过两个迭代器）
         * @tparam iterator 迭代器类型
         * @tparam end_iterator 结束迭代器类型
         * @param first 起始迭代器
         * @param second 结束迭代器
         * @return 构造好的 IteratorPair 对象
         *
         * 工厂函数，简化 IteratorPair 的创建过程。
         */
        template<typename iterator, class end_iterator = iterator>
        constexpr IteratorPair<iterator, end_iterator> MakeIteratorPair(iterator first, end_iterator second)
        {
            return { first, second };
        }

        /**
         * @brief 创建 IteratorPair 对象（通过 std::pair）
         * @tparam iterator 迭代器类型
         * @tparam end_iterator 结束迭代器类型
         * @param iterators 包含两个迭代器的 pair 对象
         * @return 构造好的 IteratorPair 对象
         *
         * 重载版本，直接接受 pair 参数。
         */
        template<typename iterator, class end_iterator = iterator>
        constexpr IteratorPair<iterator, end_iterator> MakeIteratorPair(std::pair<iterator, end_iterator> iterators)
        {
            return iterators;
        }

        /**
         * @brief 获取 multimap 中指定键的范围迭代器对
         * @tparam M map 类型（通常是 multimap）
         * @param map 目标 map 对象
         * @param key 要查找的键
         * @return 包含所有匹配元素的 IteratorPair
         *
         * 这是最常用的便捷函数，专门用于处理 multimap 的 equal_range 操作。
         * 封装了 map.equal_range(key) 调用，返回可直接用于范围 for 循环的 IteratorPair。
         *
         * @性能说明:
         * - 时间复杂度: O(log n)，与 multimap::equal_range 相同
         * - 空间复杂度: O(1)，仅存储两个迭代器
         *
         * @example
         * std::multimap<int, std::string> myMap;
         * // 遍历所有键为 42 的元素
         * for (auto& pair : MapEqualRange(myMap, 42)) {
         *     std::cout << pair.second << std::endl;
         * }
         */
        template<class M>
        auto MapEqualRange(M& map, typename M::key_type const& key)
        {
            return MakeIteratorPair(map.equal_range(key));
        }
    }
    //! namespace Containers
}
//! namespace Trinity

#endif // IteratorPair_h__
