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
 * @file SmartEnum.h
 * @brief 智能枚举工具模块，提供枚举类型的反射、迭代和字符串转换功能。
 *
 * 本模块提供了一套完整的枚举类型工具系统，支持：
 * - 枚举值的字符串表示（常量名、标题、描述）
 * - 枚举值的索引转换
 * - 枚举值的有效性验证
 * - 枚举类型的迭代遍历
 *
 * 通过特化 EnumUtilsImpl::EnumUtils 模板，可以为任意枚举类型提供上述功能。
 */

#ifndef TRINITY_SMARTENUM_H
#define TRINITY_SMARTENUM_H

#include "IteratorPair.h"
#include <iterator>

/**
 * @brief 枚举文本描述结构体，存储枚举值的字符串表示信息。
 *
 * 该结构体封装了枚举值的三种字符串表示形式：
 * - Constant: 枚举常量的原始名称（如 "ENUM_VALUE"）
 * - Title: 人类可读的简短标题
 * - Description: 人类可读的详细描述
 */
struct EnumText
{
    /**
     * @brief 构造函数，初始化枚举文本描述。
     * @param c 枚举常量名称字符串
     * @param t 枚举值标题字符串
     * @param d 枚举值描述字符串
     */
    EnumText(char const* c, char const* t, char const* d) : Constant(c), Title(t), Description(d) { }

    /** @brief 枚举常量名称，如 "ENUM_VALUE" */
    char const* const Constant;
    /** @brief 人类可读的枚举值标题，简短的显示名称 */
    char const* const Title;
    /** @brief 人类可读的枚举值描述，详细的说明文本 */
    char const* const Description;
};

/**
 * @brief 枚举工具实现的内部命名空间
 */
namespace Trinity::Impl::EnumUtilsImpl
{
    /**
     * @brief 枚举工具模板基类，需要为具体枚举类型进行特化。
     *
     * 每个需要使用 EnumUtils 的枚举类型都必须特化此模板，
     * 实现以下四个静态方法：
     * - Count(): 返回枚举值的总数
     * - ToString(): 将枚举值转换为文本描述
     * - FromIndex(): 从索引转换为枚举值
     * - ToIndex(): 将枚举值转换为索引
     *
     * @tparam Enum 枚举类型
     */
    template <typename Enum>
    struct EnumUtils
    {
        /**
         * @brief 获取枚举类型的值总数。
         * @return 枚举值的数量
         */
        static size_t Count();

        /**
         * @brief 将枚举值转换为文本描述。
         * @param value 枚举值
         * @return 包含常量名、标题和描述的 EnumText 对象
         */
        static EnumText ToString(Enum value);

        /**
         * @brief 从索引获取对应的枚举值。
         * @param index 索引值（范围：[0, Count())）
         * @return 对应的枚举值
         */
        static Enum FromIndex(size_t index);

        /**
         * @brief 将枚举值转换为索引。
         * @param index 枚举值
         * @return 对应的索引值
         */
        static size_t ToIndex(Enum index);
    };
}

/**
 * @brief 枚举工具类，提供枚举类型的通用操作接口。
 *
 * 这是一个静态工具类，为枚举类型提供以下功能：
 * - 值数量查询
 * - 字符串转换
 * - 索引映射
 * - 有效性验证
 * - 迭代器支持
 *
 * 使用前需要为目标枚举类型特化 Trinity::Impl::EnumUtilsImpl::EnumUtils 模板。
 *
 * @example
 * @code
 * // 定义枚举
 * enum class MyEnum { Value1, Value2 };
 *
 * // 特化模板
 * template<>
 * struct Trinity::Impl::EnumUtilsImpl::EnumUtils<MyEnum>
 * {
 *     static size_t Count() { return 2; }
 *     static EnumText ToString(MyEnum value) { ... }
 *     static MyEnum FromIndex(size_t index) { return static_cast<MyEnum>(index); }
 *     static size_t ToIndex(MyEnum value) { return static_cast<size_t>(value); }
 * };
 *
 * // 使用 EnumUtils
 * for (MyEnum e : EnumUtils::Iterate<MyEnum>()) {
 *     EnumText text = EnumUtils::ToString(e);
 *     printf("%s: %s\n", text.Constant, text.Description);
 * }
 * @endcode
 */
class EnumUtils
{
    public:
        /**
         * @brief 获取枚举类型的值总数。
         * @tparam Enum 枚举类型
         * @return 枚举值的数量
         */
        template <typename Enum>
        static size_t Count() { return Trinity::Impl::EnumUtilsImpl::EnumUtils<Enum>::Count(); }

        /**
         * @brief 将枚举值转换为文本描述对象。
         * @tparam Enum 枚举类型
         * @param value 枚举值
         * @return 包含常量名、标题和描述的 EnumText 对象
         */
        template <typename Enum>
        static EnumText ToString(Enum value) { return Trinity::Impl::EnumUtilsImpl::EnumUtils<Enum>::ToString(value); }

        /**
         * @brief 从索引获取对应的枚举值。
         * @tparam Enum 枚举类型
         * @param index 索引值（范围：[0, Count())）
         * @return 对应的枚举值
         */
        template <typename Enum>
        static Enum FromIndex(size_t index) { return Trinity::Impl::EnumUtilsImpl::EnumUtils<Enum>::FromIndex(index); }

        /**
         * @brief 将枚举值转换为索引。
         * @tparam Enum 枚举类型
         * @param value 枚举值
         * @return 对应的索引值
         */
        template <typename Enum>
        static uint32 ToIndex(Enum value) { return Trinity::Impl::EnumUtilsImpl::EnumUtils<Enum>::ToIndex(value);}

        /**
         * @brief 验证枚举值是否有效。
         *
         * 通过尝试将枚举值转换为索引来验证其有效性。
         * 如果转换抛出异常，则认为该值无效。
         *
         * @tparam Enum 枚举类型
         * @param value 待验证的枚举值
         * @return 如果枚举值有效返回 true，否则返回 false
         */
        template<typename Enum>
        static bool IsValid(Enum value)
        {
            try
            {
                Trinity::Impl::EnumUtilsImpl::EnumUtils<Enum>::ToIndex(value);
                return true;
            } catch (...)
            {
                return false;
            }
        }

        /**
         * @brief 验证枚举底层类型值是否有效。
         *
         * 将底层类型值转换为枚举类型后进行验证。
         *
         * @tparam Enum 枚举类型
         * @param value 枚举底层类型的值
         * @return 如果转换后的枚举值有效返回 true，否则返回 false
         */
        template<typename Enum>
        static bool IsValid(std::underlying_type_t<Enum> value) { return IsValid(static_cast<Enum>(value)); }

        /**
         * @brief 枚举迭代器类，支持随机访问遍历枚举值。
         *
         * 该迭代器实现了随机访问迭代器的完整接口，
         * 可以用于范围循环遍历枚举类型的所有值。
         *
         * @tparam Enum 枚举类型
         */
        template <typename Enum>
        class Iterator
        {
            public:
                /** @brief 迭代器类别：随机访问迭代器 */
                using iterator_category = std::random_access_iterator_tag;
                /** @brief 迭代器值类型：枚举类型 */
                using value_type = Enum;
                /** @brief 指针类型 */
                using pointer = Enum*;
                /** @brief 引用类型 */
                using reference = Enum&;
                /** @brief 差值类型 */
                using difference_type = std::ptrdiff_t;

                /**
                 * @brief 默认构造函数，创建指向末尾的迭代器。
                 */
                Iterator() : _index(EnumUtils::Count<Enum>()) {}

                /**
                 * @brief 构造指定索引位置的迭代器。
                 * @param index 枚举值的索引位置
                 */
                explicit Iterator(size_t index) : _index(index) { }

                /**
                 * @brief 相等比较运算符。
                 * @param other 另一个迭代器
                 * @return 如果两个迭代器位置相同返回 true
                 */
                bool operator==(const Iterator& other) const = default;

                /**
                 * @brief 三向比较运算符。
                 * @param other 另一个迭代器
                 * @return 三向比较结果
                 */
                std::strong_ordering operator<=>(const Iterator& other) const = default;

                /**
                 * @brief 计算两个迭代器之间的距离。
                 * @param other 另一个迭代器
                 * @return 两个迭代器之间的距离
                 */
                difference_type operator-(Iterator const& other) const { return _index - other._index; }

                /**
                 * @brief 随机访问操作符，访问指定偏移量处的枚举值。
                 * @param d 偏移量
                 * @return 偏移量处的枚举值
                 */
                value_type operator[](difference_type d) const { return FromIndex<Enum>(_index + d); }

                /**
                 * @brief 解引用操作符，获取当前位置的枚举值。
                 * @return 当前位置的枚举值
                 */
                value_type operator*() const { return operator[](0); }

                /**
                 * @brief 复合赋值加法运算符，向前移动迭代器。
                 * @param d 移动的距离
                 * @return 移动后的迭代器引用
                 */
                Iterator& operator+=(difference_type d) { _index += d; return *this; }

                /**
                 * @brief 前置递增运算符，移动到下一个枚举值。
                 * @return 递增后的迭代器引用
                 */
                Iterator& operator++() { return operator+=(1); }

                /**
                 * @brief 后置递增运算符，移动到下一个枚举值。
                 * @return 递增前的迭代器副本
                 */
                Iterator operator++(int) { Iterator i = *this; operator++(); return i; }

                /**
                 * @brief 加法运算符，返回移动指定距离后的迭代器。
                 * @param d 移动的距离
                 * @return 移动后的迭代器副本
                 */
                Iterator operator+(difference_type d) const { Iterator i = *this; i += d; return i; }

                /**
                 * @brief 复合赋值减法运算符，向后移动迭代器。
                 * @param d 移动的距离
                 * @return 移动后的迭代器引用
                 */
                Iterator& operator-=(difference_type d) { _index -= d; return *this; }

                /**
                 * @brief 前置递减运算符，移动到上一个枚举值。
                 * @return 递减后的迭代器引用
                 */
                Iterator& operator--() { return operator-=(1); }

                /**
                 * @brief 后置递减运算符，移动到上一个枚举值。
                 * @return 递减前的迭代器副本
                 */
                Iterator operator--(int) { Iterator i = *this; operator--(); return i; }

                /**
                 * @brief 减法运算符，返回向后移动指定距离后的迭代器。
                 * @param d 移动的距离
                 * @return 移动后的迭代器副本
                 */
                Iterator operator-(difference_type d) const { Iterator i = *this; i -= d; return i; }

            private:
                /** @brief 当前枚举值的索引位置 */
                difference_type _index;
        };

        /**
         * @brief 获取枚举类型的起始迭代器。
         * @tparam Enum 枚举类型
         * @return 指向第一个枚举值的迭代器
         */
        template <typename Enum>
        static Iterator<Enum> Begin() { return Iterator<Enum>(0); }

        /**
         * @brief 获取枚举类型的末尾迭代器。
         * @tparam Enum 枚举类型
         * @return 指向末尾的迭代器（不指向任何有效枚举值）
         */
        template <typename Enum>
        static Iterator<Enum> End() { return Iterator<Enum>(); }

        /**
         * @brief 获取枚举类型的迭代器范围对。
         *
         * 返回的 IteratorPair 可用于范围循环遍历所有枚举值。
         *
         * @tparam Enum 枚举类型
         * @return 包含 Begin() 和 End() 的迭代器对
         *
         * @example
         * @code
         * for (MyEnum e : EnumUtils::Iterate<MyEnum>()) {
         *     // 处理每个枚举值
         * }
         * @endcode
         */
        template <typename Enum>
        static Trinity::IteratorPair<Iterator<Enum>> Iterate() { return { Begin<Enum>(), End<Enum>() }; }

        /**
         * @brief 获取枚举值的常量名称字符串。
         * @tparam Enum 枚举类型
         * @param value 枚举值
         * @return 枚举常量名称字符串（如 "ENUM_VALUE"）
         */
        template <typename Enum>
        static char const* ToConstant(Enum value) { return ToString(value).Constant; }

        /**
         * @brief 获取枚举值的标题字符串。
         * @tparam Enum 枚举类型
         * @param value 枚举值
         * @return 枚举值的人类可读标题
         */
        template <typename Enum>
        static char const* ToTitle(Enum value) { return ToString(value).Title; }

        /**
         * @brief 获取枚举值的描述字符串。
         * @tparam Enum 枚举类型
         * @param value 枚举值
         * @return 枚举值的人类可读描述
         */
        template <typename Enum>
        static char const* ToDescription(Enum value) { return ToString(value).Description; }
};

#endif
