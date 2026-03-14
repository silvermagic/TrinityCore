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

// ============================================================================
// EnumFlag.h - 枚举标志位工具库
// ============================================================================
// 模块职责：
//   为枚举类型提供位操作支持，使枚举可以安全地作为标志位使用。
//   支持 &、|、~ 等位运算操作符。
//
// 使用场景：
//   - 游戏对象标志（可见性、交互性等）
//   - 技能标志（是否可打断、是否需要目标等）
//   - 单位标志（是否可攻击、是否飞行等）
//   - 状态标志（中毒、减速、沉默等）
//
// 使用方法：
//   1. 定义枚举类型，值设为 2 的幂次方
//   2. 使用 DEFINE_ENUM_FLAG 宏声明该枚举为标志类型
//   3. 使用 EnumFlag<EnumType> 包装类或直接使用位运算符
//
// 示例：
//   enum class UnitFlags : uint32 {
//       None = 0,
//       Attackable = 1 << 0,
//       Friendly = 1 << 1,
//       Invisible = 1 << 2
//   };
//   DEFINE_ENUM_FLAG(UnitFlags);
//
//   EnumFlag<UnitFlags> flags = UnitFlags::Attackable | UnitFlags::Friendly;
//   if (flags.HasFlag(UnitFlags::Attackable)) { ... }
//
// 性能考虑：
//   - 编译时类型安全
//   - 零运行时开销（内联展开）
// ============================================================================

#ifndef EnumFlag_h__
#define EnumFlag_h__

#include <type_traits>

/**
 * @brief 枚举标志类型检测函数（默认版本）
 *
 * @tparam T 枚举类型
 * @param 枚举值（用于类型推导）
 * @return 始终返回 false
 *
 * 默认情况下，所有枚举类型都不是标志类型。
 * 只有使用 DEFINE_ENUM_FLAG 宏声明的枚举才返回 true。
 */
template<typename T>
constexpr bool IsEnumFlag(T) { return false; }

/**
 * @brief 定义枚举为标志类型的宏
 *
 * @param enumType 枚举类型名称
 *
 * 使用此宏将枚举类型标记为标志类型，使其支持位运算操作。
 *
 * 示例：
 *   enum class MyFlags : uint32 { ... };
 *   DEFINE_ENUM_FLAG(MyFlags);
 */
#define DEFINE_ENUM_FLAG(enumType) constexpr bool IsEnumFlag(enumType) { return true; }

namespace EnumTraits
{
    /**
     * @brief 枚举标志类型特征
     *
     * @tparam T 要检测的类型
     *
     * 当 T 是枚举类型且 IsEnumFlag(T{}) 返回 true 时，IsFlag<T>::value 为 true。
     * 用于在编译时判断一个类型是否可以用作标志位。
     */
    template<typename T>
    using IsFlag = std::conjunction<std::is_enum<T>, std::integral_constant<bool, IsEnumFlag(T{})>>;
}

/**
 * @brief 枚举类型的按位与操作符
 *
 * @tparam T 枚举类型（必须标记为标志类型）
 * @param left 左操作数
 * @param right 右操作数
 * @return 位与运算结果
 *
 * 对两个枚举值执行按位与操作。
 * 只有被标记为标志类型的枚举才能使用此操作符。
 *
 * 调用时机：
 *   - 检查是否包含特定标志
 *   - flags & UnitFlags::Attackable
 */
template<typename T, std::enable_if_t<EnumTraits::IsFlag<T>::value, std::nullptr_t> = nullptr>
inline constexpr T operator&(T left, T right)
{
    return static_cast<T>(static_cast<std::underlying_type_t<T>>(left) & static_cast<std::underlying_type_t<T>>(right));
}

/**
 * @brief 枚举类型的按位与赋值操作符
 *
 * @tparam T 枚举类型（必须标记为标志类型）
 * @param left 左操作数（会被修改）
 * @param right 右操作数
 * @return 修改后的左操作数引用
 *
 * 对左操作数执行按位与操作并赋值。
 * 通常用于清除某些标志位。
 *
 * 调用时机：
 *   - 清除特定标志
 *   - flags &= ~UnitFlags::Invisible;
 */
template<typename T, std::enable_if_t<EnumTraits::IsFlag<T>::value, std::nullptr_t> = nullptr>
inline constexpr T& operator&=(T& left, T right)
{
    return left = left & right;
}

/**
 * @brief 枚举类型的按位或操作符
 *
 * @tparam T 枚举类型（必须标记为标志类型）
 * @param left 左操作数
 * @param right 右操作数
 * @return 位或运算结果
 *
 * 对两个枚举值执行按位或操作。
 * 用于组合多个标志。
 *
 * 调用时机：
 *   - 组合多个标志
 *   - flags = UnitFlags::Attackable | UnitFlags::Friendly;
 */
template<typename T, std::enable_if_t<EnumTraits::IsFlag<T>::value, std::nullptr_t> = nullptr>
inline constexpr T operator|(T left, T right)
{
    return static_cast<T>(static_cast<std::underlying_type_t<T>>(left) | static_cast<std::underlying_type_t<T>>(right));
}

/**
 * @brief 枚举类型的按位或赋值操作符
 *
 * @tparam T 枚举类型（必须标记为标志类型）
 * @param left 左操作数（会被修改）
 * @param right 右操作数
 * @return 修改后的左操作数引用
 *
 * 对左操作数执行按位或操作并赋值。
 * 用于添加新的标志位。
 *
 * 调用时机：
 *   - 添加新标志
 *   - flags |= UnitFlags::Invisible;
 */
template<typename T, std::enable_if_t<EnumTraits::IsFlag<T>::value, std::nullptr_t> = nullptr>
inline constexpr T& operator|=(T& left, T right)
{
    return left = left | right;
}

/**
 * @brief 枚举类型的按位取反操作符
 *
 * @tparam T 枚举类型（必须标记为标志类型）
 * @param value 要取反的枚举值
 * @return 位取反运算结果
 *
 * 对枚举值执行按位取反操作。
 * 通常用于清除特定标志（配合 & 操作符）。
 *
 * 调用时机：
 *   - 清除特定标志
 *   - flags &= ~UnitFlags::Invisible;
 */
template<typename T, std::enable_if_t<EnumTraits::IsFlag<T>::value, std::nullptr_t> = nullptr>
inline constexpr T operator~(T value)
{
    return static_cast<T>(~static_cast<std::underlying_type_t<T>>(value));
}

// ============================================================================
// EnumFlag - 枚举标志包装类
// ============================================================================
// 类职责：
//   提供类型安全的枚举标志操作，包括标志检查、添加、删除等功能。
//   相比直接使用位运算符，提供更清晰和安全的接口。
//
// 使用场景：
//   - 需要频繁检查标志状态的场景
//   - 需要动态添加/删除标志的场景
//   - 需要将枚举转换为底层类型的场景
//
// 性能考虑：
//   - 所有方法都是 constexpr，编译时求值
//   - 零运行时开销
//
// 示例：
//   EnumFlag<UnitFlags> flags = UnitFlags::Attackable | UnitFlags::Friendly;
//   if (flags.HasFlag(UnitFlags::Attackable)) { /* 可攻击 */ }
//   flags.RemoveFlag(UnitFlags::Attackable);
// ============================================================================
template<typename T>
class EnumFlag
{
    // 编译时检查：T 必须是标记为标志类型的枚举
    static_assert(EnumTraits::IsFlag<T>::value, "EnumFlag must be used only with enums that are marked as flags by DEFINE_ENUM_FLAG macro");

public:
    /**
     * @brief 隐式构造函数
     *
     * @param value 枚举值
     *
     * 允许从枚举类型隐式构造 EnumFlag 对象。
     * 这使得可以直接使用枚举值赋值：EnumFlag<UnitFlags> flags = UnitFlags::Attackable;
     */
    /*implicit*/ constexpr EnumFlag(T value) : _value(value)
    {
    }

    /**
     * @brief 按位与赋值操作符
     *
     * @param right 右操作数
     * @return 修改后的对象引用
     *
     * 清除未在 right 中设置的标志位。
     */
    constexpr EnumFlag& operator&=(EnumFlag right)
    {
        _value &= right._value;
        return *this;
    }

    /**
     * @brief 按位与操作符
     *
     * @param left 左操作数
     * @param right 右操作数
     * @return 位与运算结果
     */
    constexpr friend EnumFlag operator&(EnumFlag left, EnumFlag right)
    {
        return left &= right;
    }

    /**
     * @brief 按位或赋值操作符
     *
     * @param right 右操作数
     * @return 修改后的对象引用
     *
     * 添加 right 中设置的标志位。
     */
    constexpr EnumFlag& operator|=(EnumFlag right)
    {
        _value |= right._value;
        return *this;
    }

    /**
     * @brief 按位或操作符
     *
     * @param left 左操作数
     * @param right 右操作数
     * @return 位或运算结果
     */
    constexpr friend EnumFlag operator|(EnumFlag left, EnumFlag right)
    {
        return left |= right;
    }

    /**
     * @brief 按位取反操作符
     *
     * @return 位取反运算结果
     *
     * 翻转所有标志位。
     */
    constexpr EnumFlag operator~() const
    {
        return static_cast<T>(~static_cast<std::underlying_type_t<T>>(_value));
    }

    /**
     * @brief 移除指定标志
     *
     * @param flag 要移除的标志
     *
     * 清除指定的标志位，不影响其他标志。
     *
     * 调用时机：
     *   - 移除单个状态效果
     *   - 清除特定属性
     */
    constexpr void RemoveFlag(EnumFlag flag)
    {
        _value &= ~flag._value;
    }

    /**
     * @brief 检查是否包含指定标志
     *
     * @param flag 要检查的标志
     * @return 如果包含该标志返回 true，否则返回 false
     *
     * 检查是否设置了指定的标志位（可能还包含其他标志）。
     *
     * 调用时机：
     *   - 检查单位状态
     *   - 检查对象属性
     *
     * 示例：
     *   if (flags.HasFlag(UnitFlags::Invisible)) { // 单位隐身 }
     */
    constexpr bool HasFlag(T flag) const
    {
        using i = std::underlying_type_t<T>;
        return static_cast<i>(_value & flag) != static_cast<i>(0);
    }

    /**
     * @brief 检查是否包含所有指定标志
     *
     * @param flags 要检查的标志组合
     * @return 如果包含所有指定标志返回 true，否则返回 false
     *
     * 检查是否设置了所有指定的标志位。
     *
     * 调用时机：
     *   - 检查多个状态同时存在
     *   - 验证前提条件
     *
     * 示例：
     *   if (flags.HasAllFlags(UnitFlags::Attackable | UnitFlags::Friendly)) {
     *       // 既是可攻击又是友方
     *   }
     */
    constexpr bool HasAllFlags(T flags) const
    {
        return (_value & flags) == flags;
    }

    /**
     * @brief 隐式转换为枚举类型
     *
     * @return 枚举值
     *
     * 允许 EnumFlag 对象隐式转换为原始枚举类型。
     * 方便与期望枚举类型的 API 兼容。
     */
    constexpr operator T() const
    {
        return _value;
    }

    /**
     * @brief 转换为底层类型
     *
     * @return 枚举的底层整数值
     *
     * 将枚举值转换为其底层整数类型（通常是 uint32 或 uint8）。
     *
     * 调用时机：
     *   - 需要序列化或网络传输时
     *   - 需要整数计算时
     *   - 与 C API 交互时
     */
    constexpr std::underlying_type_t<T> AsUnderlyingType() const
    {
        return static_cast<std::underlying_type_t<T>>(_value);
    }

private:
    T _value;  // 存储的枚举值
};

#endif // EnumFlag_h__
