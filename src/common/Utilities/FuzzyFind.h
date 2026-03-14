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
 * @file FuzzyFind.h
 * @brief 模糊查找工具模块
 *
 * 本文件提供了通用的模糊查找功能，用于在容器中查找包含指定关键词的元素，
 * 并根据匹配程度进行排序。该功能广泛应用于：
 * - 命令补全和搜索
 * - NPC/物品名称搜索
 * - GM 命令的模糊匹配
 *
 * 主要特点：
 * - 支持任意容器类型（需支持迭代器）
 * - 支持自定义匹配谓词
 * - 支持匹配奖励机制（提高某些结果的权重）
 * - 按匹配度降序排列结果
 */

#ifndef TRINITY_FUZZYFIND_H
#define TRINITY_FUZZYFIND_H

#include <map>
#include <string>
#include <type_traits>

/**
 * @namespace Trinity::Containers
 * @brief TrinityCore 容器工具命名空间
 *
 * 提供各种容器操作的工具函数，包括查找、过滤、排序等功能。
 */
namespace Trinity
{
    namespace Containers
    {
        /**
         * @brief 在容器中进行模糊查找，返回按匹配度排序的结果
         *
         * 该函数遍历容器中的每个元素，检查是否包含搜索关键词列表中的关键词，
         * 并根据匹配的关键词数量对结果进行排序。可以指定自定义的匹配谓词和奖励函数。
         *
         * @tparam Container 要搜索的容器类型（必须支持迭代器访问）
         * @tparam NeedleContainer 搜索关键词容器类型
         * @tparam ContainsOperator 匹配谓词函数类型，默认为字符串包含判断
         * @tparam T 额外的模板参数（用于 SFINAE 特化）
         *
         * @param container 要搜索的容器（被搜索的元素集合）
         * @param needles 搜索关键词容器（包含多个搜索关键词）
         * @param contains 匹配谓词函数，用于判断元素是否包含某个关键词
         *                  签名：bool(ElementType const&, std::string const&)
         *                  默认为 StringContainsStringI（不区分大小写的字符串包含判断）
         * @param bonus 奖励函数指针，用于给特定元素增加额外的匹配权重
         *               签名：int(ElementType)
         *               返回值会被加到匹配计数上， nullptr 表示不使用奖励
         *
         * @return std::multimap<size_t, MappedType, std::greater<size_t>>
         *         返回一个多重映射，键为匹配度分数，值为元素的引用或副本。
         *         结果按匹配度降序排列（最匹配的在前）。
         *         MappedType 根据容器迭代器返回类型自动推导：
         *         - 如果是引用，则使用 std::reference_wrapper
         *         - 如果是值，则直接存储值
         *
         * @note 性能注意事项：
         *       - 时间复杂度为 O(N*M)，其中 N 是容器大小，M 是关键词数量
         *       - 对于大型容器和大量关键词，性能可能受影响
         *       - 结果存储在 multimap 中，内存开销与匹配结果数量成正比
         *
         * @example 使用示例：
         * @code
         * std::vector<std::string> commands = {"lookup", "lookup_item", "lookup_player"};
         * std::vector<std::string> keywords = {"look", "item"};
         * auto results = FuzzyFindIn(commands, keywords);
         * // results 会按匹配度排序："lookup_item" 匹配2个关键词，排在最前
         * @endcode
         */
        template <typename Container, typename NeedleContainer, typename ContainsOperator = bool(std::string const&, std::string const&), typename T = void>
        auto FuzzyFindIn(Container const& container, NeedleContainer const& needles, ContainsOperator const& contains = StringContainsStringI, int(*bonus)(decltype((*std::begin(std::declval<Container>())))) = nullptr)
        {
            // 推导迭代器解引用的结果类型，用于确定如何存储元素
            using IteratorResult = decltype((*std::begin(container)));

            // 确定映射的值类型：如果是引用则包装为 reference_wrapper，否则直接使用值类型
            // 这样可以避免不必要的拷贝，同时保证类型安全
            using MappedType = std::conditional_t<std::is_reference_v<IteratorResult>, std::reference_wrapper<std::remove_reference_t<IteratorResult>>, IteratorResult>;

            // 结果容器：使用 multimap 存储匹配结果，键为匹配度分数，值为元素
            // 使用 std::greater<size_t> 使结果按匹配度降序排列
            std::multimap<size_t, MappedType, std::greater<size_t>> results;

            // 遍历容器中的每个元素
            for (auto outerIt = std::begin(container), outerEnd = std::end(container); outerIt != outerEnd; ++outerIt)
            {
                size_t count = 0;  // 当前元素匹配的关键词计数

                // 遍历所有搜索关键词，统计当前元素匹配了多少个关键词
                for (auto innerIt = std::begin(needles), innerEnd = std::end(needles); innerIt != innerEnd; ++innerIt)
                    if (contains(*outerIt, *innerIt))  // 使用匹配谓词判断是否包含该关键词
                        ++count;

                // 如果没有任何匹配，跳过该元素（不加入结果集）
                if (!count)
                    continue;

                // 如果提供了奖励函数，将奖励值加到匹配计数上
                // 这允许某些元素获得额外的权重（例如精确匹配、前缀匹配等）
                if (bonus)
                    count += bonus(*outerIt);

                // 将结果插入 multimap，键为匹配度分数，值为元素
                // multimap 会自动按照键（匹配度）降序排列
                results.emplace(count, *outerIt);
            }

            return results;  // 返回排序后的匹配结果集
        }
    }
}

#endif
