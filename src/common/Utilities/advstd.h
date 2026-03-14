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
 * @file advstd.h
 * @brief 高级标准库工具模块
 *
 * 本文件提供了对 C++ 标准库最新特性的兼容性实现，包含当前编译器版本尚不支持的标准库功能。
 * 这些实现遵循 C++ 标准规范，允许项目在较旧的编译器上使用现代 C++ 特性。
 *
 * 主要功能包括：
 * - 比较操作辅助函数（is_eq, is_neq）
 * - 位转换工具（bit_cast）
 *
 * @note 所有实现都位于 advstd 命名空间中，以避免与标准库冲突
 * @note 当编译器支持相应特性时，会自动使用标准库实现
 */

#ifndef TRINITY_ADVSTD_H
#define TRINITY_ADVSTD_H

#include <version>

#ifdef __cpp_lib_bit_cast
#include <bit>
#else
#include <cstring> // for memcpy
#endif
#include <compare>

/**
 * @namespace advstd
 * @brief 高级标准库工具命名空间
 *
 * 此命名空间包含了当前 C++ 标准库版本中缺失的、或编译器实现不完整的标准库功能的实现。
 * 这些实现遵循 C++ 标准规范，提供了向前兼容性。
 *
 * @note 当编译器标准库支持相应特性时，会自动使用标准库版本
 * @warning 此命名空间中的功能仅用于填补标准库的空白，不应在新标准支持后继续使用
 *
 * @details 设计原则：
 * - 优先使用标准库实现（通过特性宏检测）
 * - 提供与标准库一致的接口和行为
 * - 保证 noexcept 和 constexpr 属性
 * - 保持最小化实现，避免不必要的依赖
 */
namespace advstd
{
/**
 * @brief 检查偏序关系是否为相等
 *
 * 判断给定的偏序关系是否表示相等状态。这是 C++20 比较操作的辅助函数，
 * 用于简化三路比较结果的判断。
 *
 * @param cmp 要检查的偏序关系值
 * @return true 如果比较结果为相等
 * @return false 如果比较结果不为相等
 *
 * @note 此函数标记为 [[nodiscard]]，确保返回值不被忽略
 * @note constexpr 函数，可在编译期求值
 * @note noexcept 函数，不会抛出异常
 *
 * @details 实现说明：
 * - libc++ 标准库缺少此函数的实现
 * - 等价于判断 cmp == 0
 * - 适用于所有可偏序类型的比较结果
 *
 * @code
 * std::partial_ordering result = a <=> b;
 * if (advstd::is_eq(result)) {
 *     // a 等于 b
 * }
 * @endcode
 *
 * @see is_neq()
 */
[[nodiscard]] constexpr bool is_eq(std::partial_ordering cmp) noexcept { return cmp == 0; }

/**
 * @brief 检查偏序关系是否为不相等
 *
 * 判断给定的偏序关系是否表示不相等状态。这是 C++20 比较操作的辅助函数，
 * 用于简化三路比较结果的判断。
 *
 * @param cmp 要检查的偏序关系值
 * @return true 如果比较结果为不相等
 * @return false 如果比较结果为相等
 *
 * @note 此函数标记为 [[nodiscard]]，确保返回值不被忽略
 * @note constexpr 函数，可在编译期求值
 * @note noexcept 函数，不会抛出异常
 *
 * @details 实现说明：
 * - libc++ 标准库缺少此函数的实现
 * - 等价于判断 cmp != 0
 * - 适用于所有可偏序类型的比较结果
 *
 * @code
 * std::partial_ordering result = a <=> b;
 * if (advstd::is_neq(result)) {
 *     // a 不等于 b
 * }
 * @endcode
 *
 * @see is_eq()
 */
[[nodiscard]] constexpr bool is_neq(std::partial_ordering cmp) noexcept { return cmp != 0; }

#ifdef __cpp_lib_bit_cast
/**
 * @brief 使用标准库的 bit_cast 实现
 *
 * 当编译器支持 __cpp_lib_bit_cast 特性宏时，使用标准库提供的 bit_cast 实现。
 * 这确保了最佳的性能和编译器优化支持。
 */
using std::bit_cast;
#else
/**
 * @brief 位转换模板函数（兼容性实现）
 *
 * 将一种类型的对象按位转换为另一种类型，保持二进制表示不变。
 * 这是 C++20 std::bit_cast 的兼容性实现，用于较旧的编译器（如 libstdc++ v10）。
 *
 * @tparam To 目标类型，必须满足特定约束条件
 * @tparam From 源类型，必须满足特定约束条件
 * @param from 要转换的源对象
 * @return To 转换后的目标类型对象
 *
 * @note 标记为 [[nodiscard]]，确保返回值不被忽略
 * @note constexpr 函数，可能在编译期求值（需要编译器支持常量求值的 memcpy）
 * @note noexcept 函数，不会抛出异常
 *
 * @details 类型约束条件：
 * - To 和 From 的大小必须相同（sizeof(To) == sizeof(From)）
 * - To 必须是可平凡复制的类型（is_trivially_copyable）
 * - From 必须是可平凡复制的类型（is_trivially_copyable）
 *
 * 实现原理：
 * - 使用 std::memcpy 进行低级别的内存拷贝
 * - 保证不触发未定义行为
 * - 编译器可以优化为无操作（通过寄存器重命名）
 *
 * 典型用途：
 * - 浮点数与整数的位表示转换
 * - 网络字节序转换
 * - 序列化和反序列化
 * - 类型擦除和恢复
 *
 * @code
 * // 将 float 转换为 uint32_t 查看其二进制表示
 * float f = 3.14f;
 * uint32_t bits = advstd::bit_cast<uint32_t>(f);
 *
 * // 将 uint32_t 转换回 float
 * float restored = advstd::bit_cast<float>(bits);
 * assert(f == restored); // 保证相等
 * @endcode
 *
 * @warning 不可用于涉及填充字节、陷阱表示或对齐要求的类型
 * @warning 不能用于非平凡类型，如含有指针或虚函数的类
 *
 * @see std::memcpy
 */
template <typename To, typename From,
    std::enable_if_t<std::conjunction_v<
    std::bool_constant<sizeof(To) == sizeof(From)>,
    std::is_trivially_copyable<To>,
    std::is_trivially_copyable<From>>, int> = 0>
    [[nodiscard]] constexpr To bit_cast(From const& from) noexcept
{
    To to;
    std::memcpy(&to, &from, sizeof(To));
    return to;
}
#endif

/**
 * @name C++ 标准版本兼容性说明
 *
 * 本文件根据编译器和标准库的能力自动选择实现：
 *
 * **bit_cast 功能：**
 * - C++20 标准：std::bit_cast（需要 __cpp_lib_bit_cast 特性宏）
 * - 兼容实现：使用 std::memcpy 的自定义实现
 * - 支持编译器：libstdc++ v10+ 和所有支持 C++17 的编译器
 *
 * **比较操作辅助函数：**
 * - C++20 标准：std::is_eq, std::is_neq（部分标准库实现缺失）
 * - 兼容实现：直接比较运算符
 * - 已知问题：libc++ 缺少这些函数的实现
 *
 * @note 迁移指南：当项目升级到完全支持 C++20 的编译器和标准库后，
 *       可以直接使用 std 命名空间中的对应函数，逐步移除对 advstd 的依赖
 */

} // namespace advstd

#endif // TRINITY_ADVSTD_H
