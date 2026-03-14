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
 * @file Tuples.h
 * @brief 元组工具模块 - 提供元组类型检查和操作的工具函数
 *
 * 模块职责：
 *   - 提供元组类型检查功能（判断类型是否在元组中）
 *   - 提供元组类型判断功能（判断类型是否为元组）
 *   - 提供从元组构造对象的功能
 *
 * 主要组件：
 *   - has_type: 检查类型是否在元组的参数列表中
 *   - is_tuple: 判断类型是否为std::tuple
 *   - new_from_tuple: 从元组参数构造对象
 */

#ifndef Tuples_h__
#define Tuples_h__

#include <tuple>

namespace Trinity
{
    /**
     * @struct has_type
     * @brief 类型检查器 - 检查类型T是否在元组的参数列表中
     *
     * 职责：
     *   编译时检查指定类型T是否存在于元组Tuple的参数列表中
     *   使用std::disjunction实现类型匹配检查
     *
     * @tparam T 要检查的类型
     * @tparam Tuple 元组类型
     */
    template <typename T, typename Tuple>
    struct has_type;

    /**
     * @brief has_type的元组特化版本
     * @tparam T 要检查的类型
     * @tparam Us 元组参数包
     *
     * 继承自std::disjunction，如果T与任一Us类型相同，value为true
     */
    template <typename T, typename... Us>
    struct has_type<T, std::tuple<Us...>> : std::disjunction<std::is_same<T, Us>...>
    {
    };

    /**
     * @brief has_type的便捷变量模板
     * @tparam T 要检查的类型
     * @tparam Us 元组参数包
     * @return 如果类型在元组中返回true，否则返回false
     */
    template <typename T, typename... Us>
    constexpr bool has_type_v = has_type<T, Us...>::value;

    /**
     * @struct is_tuple
     * @brief 元组类型判断器 - 判断类型是否为std::tuple
     *
     * 职责：
     *   编译时判断类型T是否为std::tuple类型
     *   默认继承std::false_type（非元组类型）
     *
     * @tparam T 要判断的类型
     */
    template<typename>
    struct is_tuple : std::false_type
    {
    };

    /**
     * @brief is_tuple的元组特化版本
     * @tparam Ts 元组参数包
     *
     * 特化版本，匹配std::tuple<Ts...>类型，继承std::true_type
     */
    template<typename... Ts>
    struct is_tuple<std::tuple<Ts...>> : std::true_type
    {
    };

    /**
     * @brief is_tuple的便捷变量模板
     * @tparam Ts 要判断的类型（可能是元组类型）
     * @return 如果是元组类型返回true，否则返回false
     */
    template<typename... Ts>
    constexpr bool is_tuple_v = is_tuple<Ts...>::value;

    /**
     * @namespace Impl
     * @brief 内部实现细节命名空间
     */
    namespace Impl
    {
        /**
         * @brief 从元组构造对象的实现函数
         * @tparam T 要构造的对象类型
         * @tparam Tuple 元组类型
         * @tparam I 索引序列
         * @param args 包含构造函数参数的元组
         * @param 索引序列，用于展开元组参数
         * @return 返回新创建的对象指针
         *
         * 使用std::get和索引序列展开元组参数，调用构造函数创建对象
         */
        template <class T, class Tuple, size_t... I>
        T* new_from_tuple(Tuple&& args, std::index_sequence<I...>)
        {
            return new T(std::get<I>(std::forward<Tuple>(args))...);
        }
    }

    /**
     * @brief 从元组构造对象
     * @tparam T 要构造的对象类型
     * @tparam Tuple 元组类型
     * @param args 包含构造函数参数的元组
     * @return 返回新创建的对象指针
     *
     * 职责：
     *   从元组中提取参数并构造T类型的对象
     *   自动生成索引序列以展开元组参数
     *
     * 使用示例：
     *   auto args = std::make_tuple(42, 3.14, "hello");
     *   auto obj = new_from_tuple<MyClass>(args);
     *
     * 性能注意事项：
     *   使用完美转发，避免不必要的拷贝
     *   [[nodiscard]]属性确保返回值不被忽略
     */
    template<class T, class Tuple>
    [[nodiscard]] T* new_from_tuple(Tuple&& args)
    {
        return Impl::new_from_tuple<T>(std::forward<Tuple>(args), std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
    }
}

#endif // Tuples_h__
