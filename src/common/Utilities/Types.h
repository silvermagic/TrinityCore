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
 * @file Types.h
 * @brief 类型工具模块 - 提供类型元编程工具
 *
 * 模块职责：
 *   - 提供类型查找工具，在类型列表中查找满足条件的类型
 *   - 提供依赖型false工具，用于static_assert的条件失败
 *
 * 主要组件：
 *   - find_type_if: 在类型列表中查找满足谓词的第一个类型
 *   - find_type_end: 查找结束标记类型
 *   - dependant_false: 依赖型false值，用于模板条件断言
 */

#ifndef Types_h__
#define Types_h__

#include <type_traits>

namespace Trinity
{
    /**
     * @struct find_type_end
     * @brief 类型查找结束标记 - 用于find_type_if的"迭代器"结束标记
     *
     * 职责：
     *   当find_type_if在类型列表中未找到匹配类型时，返回此类型作为结束标记
     */
    struct find_type_end;

    /**
     * @struct find_type_if
     * @brief 类型查找器 - 在类型列表中查找满足谓词的第一个类型
     *
     * 职责：
     *   编译时在类型列表Ts中查找第一个满足谓词Check的类型
     *   如果找到则返回该类型，否则返回find_type_end
     *
     * @tparam Check 类型谓词模板，必须包含static bool ::value成员
     * @tparam Ts 类型列表
     *
     * 注意：
     *   Check必须是包含static bool ::value的类型，_v别名无法工作
     */
    template<template<typename...> typename Check, typename... Ts>
    struct find_type_if;

    /**
     * @brief find_type_if的空列表特化版本
     * @tparam Check 类型谓词模板
     *
     * 当类型列表为空时，返回find_type_end
     */
    template<template<typename...> typename Check>
    struct find_type_if<Check>
    {
        using type = find_type_end;
    };

    /**
     * @brief find_type_if的递归特化版本
     * @tparam Check 类型谓词模板
     * @tparam T1 类型列表的第一个类型
     * @tparam Ts 类型列表的剩余类型
     *
     * 如果T1满足谓词Check，则返回T1；否则递归检查剩余类型Ts
     * 使用std::conditional_t实现条件类型选择
     */
    template<template<typename...> typename Check, typename T1, typename... Ts>
    struct find_type_if<Check, T1, Ts...> : std::conditional_t<Check<T1>::value, std::type_identity<T1>, find_type_if<Check, Ts...>>
    {
    };

    /**
     * @brief find_type_if的便捷别名模板
     * @tparam Check 类型谓词模板
     * @tparam Ts 类型列表
     * @return 返回找到的类型或find_type_end
     *
     * 使用示例：
     *   template<typename... Ts>
     *   struct Example
     *   {
     *       using TupleArg = Trinity::find_type_if_t<Trinity::is_tuple, Ts...>;
     *
     *       bool HasTuple()
     *       {
     *           return !std::is_same_v<TupleArg, Trinity::find_type_end>;
     *       }
     *   };
     *
     *   Example<int, std::string, std::tuple<int, int, int>, char> example;
     *   example.HasTuple() == true; // TupleArg is std::tuple<int, int, int>
     *
     *   Example<int, std::string, char> example2;
     *   example2.HasTuple() == false; // TupleArg is Trinity::find_type_end
     */
    template<template<typename...> typename Check, typename... Ts>
    using find_type_if_t = typename find_type_if<Check, Ts...>::type;

    /**
     * @struct dependant_false
     * @brief 依赖型false工具 - 用于模板中的条件static_assert
     *
     * 职责：
     *   提供一个依赖于模板参数T的false值
     *   用于在模板特化中触发static_assert失败
     *
     * 为什么需要：
     *   直接使用static_assert(false)会在模板实例化前就失败
     *   使用dependant_false<T>可以延迟到模板实例化时才失败
     *
     * @tparam T 依赖的类型参数
     *
     * 使用示例：
     *   template<typename T>
     *   void Process() {
     *       if constexpr (std::is_integral_v<T>) {
     *           // 处理整数类型
     *       } else {
     *           static_assert(dependant_false_v<T>, "Unsupported type");
     *       }
     *   }
     */
    template <typename T>
    struct dependant_false { static constexpr bool value = false; };

    /**
     * @brief dependant_false的便捷变量模板
     * @tparam T 依赖的类型参数
     * @return 返回false值
     */
    template <typename T>
    constexpr bool dependant_false_v = dependant_false<T>::value;
}

#endif // Types_h__
