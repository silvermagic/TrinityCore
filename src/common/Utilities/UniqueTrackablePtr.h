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
 * @file UniqueTrackablePtr.h
 * @brief 可追踪唯一指针模块 - 提供具有唯一所有权和弱引用能力的智能指针
 *
 * 模块职责：
 *   - 提供unique_trackable_ptr智能指针，强制唯一所有权
 *   - 提供unique_weak_ptr弱引用智能指针，用于观察对象生命周期
 *   - 提供unique_strong_ref_ptr强引用智能指针，临时持有对象引用
 *   - 结合std::unique_ptr的唯一性和std::shared_ptr的可追踪性
 *
 * 设计特点：
 *   - unique_trackable_ptr: 类似std::unique_ptr，但支持weak_ptr观察
 *   - unique_weak_ptr: 类似std::weak_ptr，用于观察对象是否存活
 *   - unique_strong_ref_ptr: 临时强引用，防止多线程环境下对象被释放
 *   - 内部使用std::shared_ptr实现，因此有控制块的开销
 *
 * 使用场景：
 *   - 需要唯一所有权，但又需要观察对象生命周期的情况
 *   - 多线程环境下需要临时持有对象引用
 *   - 替代原始指针，提供更安全的生命周期管理
 *
 * 主要组件：
 *   - unique_trackable_ptr: 唯一所有权智能指针
 *   - unique_weak_ptr: 弱引用智能指针
 *   - unique_strong_ref_ptr: 临时强引用智能指针
 */

#ifndef TRINITYCORE_UNIQUE_TRACKABLE_PTR_H
#define TRINITYCORE_UNIQUE_TRACKABLE_PTR_H

#include <memory>

namespace Trinity
{
template <typename T>
class unique_trackable_ptr;

template <typename T>
class unique_weak_ptr;

template <typename T>
class unique_strong_ref_ptr;

/**
 * @class unique_trackable_ptr
 * @brief 可追踪唯一指针 - 强制唯一所有权并支持弱引用
 *
 * 职责：
 *   - 提供类似std::shared_ptr的功能，但强制唯一所有权
 *   - 允许创建weak_ptr观察对象生命周期
 *   - 结合std::unique_ptr的唯一性和std::shared_ptr的可追踪性
 *
 * 实现细节：
 *   - 内部使用std::shared_ptr实现
 *   - 有控制块分配的开销（与std::shared_ptr相同）
 *   - 禁止拷贝，仅支持移动
 *
 * @tparam T 持有对象的类型
 *
 * 使用示例：
 *   unique_trackable_ptr<MyClass> ptr = make_unique_trackable<MyClass>(args...);
 *   unique_weak_ptr<MyClass> weak = ptr;
 *   if (auto strong = weak.lock()) {
 *       // 安全访问对象
 *   }
 */
template <typename T>
class unique_trackable_ptr
{
public:
    using element_type = T;     ///< 元素类型
    using pointer = T*;         ///< 原始指针类型

    /**
     * @brief 默认构造函数
     *
     * 构造一个空的unique_trackable_ptr
     */
    unique_trackable_ptr() : _ptr() { }

    /**
     * @brief 原始指针构造函数
     * @param ptr 要管理的原始指针
     */
    explicit unique_trackable_ptr(pointer ptr)
        : _ptr(ptr) { }

    /**
     * @brief 带删除器的构造函数
     * @tparam Deleter 删除器类型
     * @param ptr 要管理的原始指针
     * @param deleter 自定义删除器
     */
    template <typename Deleter, std::enable_if_t<std::conjunction_v<std::is_move_constructible<Deleter>, std::is_invocable<Deleter&, T*&>>, int> = 0>
    explicit unique_trackable_ptr(pointer ptr, Deleter deleter)
        : _ptr(ptr, std::move(deleter)) { }

    /// 禁用拷贝构造函数
    unique_trackable_ptr(unique_trackable_ptr const&) = delete;

    /**
     * @brief 移动构造函数
     * @param other 要移动的unique_trackable_ptr
     */
    unique_trackable_ptr(unique_trackable_ptr&& other) noexcept
        : _ptr(std::move(other._ptr)) { }

    /**
     * @brief 移动构造函数（支持派生类转换）
     * @tparam T2 派生类类型
     * @param other 要移动的unique_trackable_ptr
     */
    template <typename T2, std::enable_if_t<std::is_convertible_v<T2*, T*>, int> = 0>
    unique_trackable_ptr(unique_trackable_ptr<T2>&& other) noexcept
        : _ptr(std::move(other)._ptr) { }

    /// 禁用拷贝赋值运算符
    unique_trackable_ptr& operator=(unique_trackable_ptr const&) = delete;

    /**
     * @brief 移动赋值运算符
     * @param other 要移动的unique_trackable_ptr
     * @return 返回当前对象引用
     */
    unique_trackable_ptr& operator=(unique_trackable_ptr&& other) noexcept
    {
        _ptr = std::move(other._ptr);
        return *this;
    }

    /**
     * @brief 移动赋值运算符（支持派生类转换）
     * @tparam T2 派生类类型
     * @param other 要移动的unique_trackable_ptr
     * @return 返回当前对象引用
     */
    template <typename T2, std::enable_if_t<std::is_convertible_v<T2*, T*>, int> = 0>
    unique_trackable_ptr& operator=(unique_trackable_ptr<T2>&& other) noexcept
    {
        _ptr = std::move(other)._ptr;
        return *this;
    }

    /// 析构函数
    ~unique_trackable_ptr() = default;

    /**
     * @brief 空指针赋值运算符
     * @return 返回当前对象引用
     *
     * 重置指针为空
     */
    unique_trackable_ptr& operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    /**
     * @brief 交换两个unique_trackable_ptr
     * @param other 要交换的unique_trackable_ptr
     */
    void swap(unique_trackable_ptr& other) noexcept
    {
        using std::swap;
        swap(_ptr, other._ptr);
    }

    /**
     * @brief 解引用运算符
     * @return 返回对象引用
     */
    element_type& operator*() const
    {
        return *_ptr;
    }

    /**
     * @brief 成员访问运算符
     * @return 返回原始指针
     */
    pointer operator->() const
    {
        return _ptr.operator->();
    }

    /**
     * @brief 获取原始指针
     * @return 返回原始指针
     */
    pointer get() const
    {
        return _ptr.get();
    }

    /**
     * @brief 布尔转换运算符
     * @return 如果指针非空返回true，否则返回false
     */
    explicit operator bool() const
    {
        return static_cast<bool>(_ptr);
    }

    /**
     * @brief 重置指针为空
     */
    void reset()
    {
        _ptr.reset();
    }

    /**
     * @brief 重置指针为新的原始指针
     * @param ptr 新的原始指针
     */
    void reset(pointer ptr)
    {
        _ptr.reset(ptr);
    }

    /**
     * @brief 重置指针为新的原始指针并设置删除器
     * @tparam Deleter 删除器类型
     * @param ptr 新的原始指针
     * @param deleter 自定义删除器
     */
    template <class Deleter, std::enable_if_t<std::conjunction_v<std::is_move_constructible<Deleter>, std::is_invocable<Deleter&, T*&>>, int> = 0>
    void reset(pointer ptr, Deleter deleter)
    {
        _ptr.reset(ptr, std::move(deleter));
    }

private:
    template <typename T0>
    friend class unique_trackable_ptr;

    template <typename T0>
    friend class unique_weak_ptr;

    template <typename T0, typename... Args>
    friend std::enable_if_t<!std::is_array_v<T0>, unique_trackable_ptr<T0>> make_unique_trackable(Args&&... args);

    template <typename T0>
    friend std::enable_if_t<std::is_unbounded_array_v<T0>, unique_trackable_ptr<T0>> make_unique_trackable(std::size_t N);

    template <typename T0>
    friend std::enable_if_t<std::is_unbounded_array_v<T0>, unique_trackable_ptr<T0>> make_unique_trackable(std::size_t N, std::remove_extent_t<T0> const& val);

    template <typename T0>
    friend std::enable_if_t<std::is_bounded_array_v<T0>, unique_trackable_ptr<T0>> make_unique_trackable();

    template <typename T0>
    friend std::enable_if_t<std::is_bounded_array_v<T0>, unique_trackable_ptr<T0>> make_unique_trackable(std::remove_extent_t<T0> const& val);

    std::shared_ptr<element_type> _ptr;     ///< 内部shared_ptr，实际管理对象
};

/**
 * @class unique_weak_ptr
 * @brief 弱引用智能指针 - 用于观察unique_trackable_ptr管理的对象
 *
 * 职责：
 *   - 观察unique_trackable_ptr管理的对象，不增加引用计数
 *   - 提供lock()方法尝试获取临时强引用
 *   - 类似std::weak_ptr相对于std::shared_ptr的关系
 *
 * 使用场景：
 *   - 需要观察对象生命周期但不拥有所有权
 *   - 避免循环引用
 *   - 检查对象是否仍然存活
 *
 * @tparam T 持有对象的类型
 */
template <typename T>
class unique_weak_ptr
{
public:
    using element_type = T;     ///< 元素类型
    using pointer = T*;         ///< 原始指针类型

    /**
     * @brief 默认构造函数
     */
    unique_weak_ptr() = default;

    /**
     * @brief 从unique_trackable_ptr构造
     * @param trackable 要观察的unique_trackable_ptr
     */
    unique_weak_ptr(unique_trackable_ptr<T> const& trackable)
        : _ptr(trackable._ptr) { }

    /**
     * @brief 拷贝构造函数
     * @param other 要拷贝的unique_weak_ptr
     */
    unique_weak_ptr(unique_weak_ptr const& other) = default;

    /**
     * @brief 拷贝构造函数（支持派生类转换）
     * @tparam T2 派生类类型
     * @param other 要拷贝的unique_weak_ptr
     */
    template <typename T2, std::enable_if_t<std::is_convertible_v<T2*, T*>, int> = 0>
    unique_weak_ptr(unique_weak_ptr<T2> const& other) noexcept
        : _ptr(other._ptr) { }

    /**
     * @brief 移动构造函数
     * @param other 要移动的unique_weak_ptr
     */
    unique_weak_ptr(unique_weak_ptr&& other) noexcept = default;

    /**
     * @brief 移动构造函数（支持派生类转换）
     * @tparam T2 派生类类型
     * @param other 要移动的unique_weak_ptr
     */
    template <typename T2, std::enable_if_t<std::is_convertible_v<T2*, T*>, int> = 0>
    unique_weak_ptr(unique_weak_ptr<T2>&& other) noexcept
        : _ptr(std::move(other)._ptr) { }

    /**
     * @brief 从unique_trackable_ptr赋值
     * @param trackable 要观察的unique_trackable_ptr
     * @return 返回当前对象引用
     */
    unique_weak_ptr& operator=(unique_trackable_ptr<T> const& trackable)
    {
        _ptr = trackable._ptr;
        return *this;
    }

    /**
     * @brief 拷贝赋值运算符
     * @param other 要拷贝的unique_weak_ptr
     * @return 返回当前对象引用
     */
    unique_weak_ptr& operator=(unique_weak_ptr const& other) = default;

    /**
     * @brief 移动赋值运算符（支持派生类转换）
     * @tparam T2 派生类类型
     * @param other 要移动的unique_weak_ptr
     * @return 返回当前对象引用
     */
    template <typename T2, std::enable_if_t<std::is_convertible_v<T2*, T*>, int> = 0>
    unique_weak_ptr& operator=(unique_weak_ptr<T2>&& other)
    {
        _ptr = std::move(other)._ptr;
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     * @param other 要移动的unique_weak_ptr
     * @return 返回当前对象引用
     */
    unique_weak_ptr& operator=(unique_weak_ptr&& other) noexcept = default;

    /// 析构函数
    ~unique_weak_ptr() = default;

    /**
     * @brief 交换两个unique_weak_ptr
     * @param other 要交换的unique_weak_ptr
     */
    void swap(unique_weak_ptr& other) noexcept
    {
        using std::swap;
        swap(_ptr, other._ptr);
    }

    /**
     * @brief 检查对象是否已过期（被销毁）
     * @return 如果对象已被销毁返回true，否则返回false
     */
    bool expired() const
    {
        return _ptr.expired();
    }

    /**
     * @brief 尝试获取临时强引用
     * @return 返回unique_strong_ref_ptr，如果对象已过期则为空
     *
     * 如果对象仍然存活，返回一个临时强引用，防止对象被其他线程释放
     */
    unique_strong_ref_ptr<element_type> lock() const
    {
        return unique_strong_ref_ptr<element_type>(_ptr.lock());
    }

private:
    template <typename T0>
    friend class unique_weak_ptr;

    template <typename T0>
    friend class unique_strong_ref_ptr;

    template <class To, class From>
    friend unique_weak_ptr<To> static_pointer_cast(unique_weak_ptr<From> const& other);

    template <class To, class From>
    friend unique_weak_ptr<To> const_pointer_cast(unique_weak_ptr<From> const& other);

    template <class To, class From>
    friend unique_weak_ptr<To> reinterpret_pointer_cast(unique_weak_ptr<From> const& other);

    template <class To, class From>
    friend unique_weak_ptr<To> dynamic_pointer_cast(unique_weak_ptr<From> const& other);

    std::weak_ptr<element_type> _ptr;     ///< 内部weak_ptr，观察对象
};

/**
 * @class unique_strong_ref_ptr
 * @brief 临时强引用智能指针 - 防止多线程环境下对象被意外释放
 *
 * 职责：
 *   - 持有对象的临时强引用，防止对象被释放
 *   - unique_weak_ptr::lock()的返回类型
 *   - 不可移动和不可拷贝，仅用于短生命周期的局部变量
 *
 * 使用场景：
 *   - 多线程环境下访问对象时，防止对象被其他线程释放
 *   - 作为unique_weak_ptr::lock()的返回值
 *
 * 设计说明：
 *   - 禁止拷贝和移动，确保生命周期受控
 *   - 仅用于局部变量，不应存储或传递
 *
 * @tparam T 持有对象的类型
 */
template <typename T>
class unique_strong_ref_ptr
{
public:
    using element_type = T;     ///< 元素类型
    using pointer = T*;         ///< 原始指针类型

    /// 禁用拷贝构造函数
    unique_strong_ref_ptr(unique_strong_ref_ptr const&) = delete;
    /// 禁用移动构造函数
    unique_strong_ref_ptr(unique_strong_ref_ptr&&) = delete;
    /// 禁用拷贝赋值运算符
    unique_strong_ref_ptr& operator=(unique_strong_ref_ptr const&) = delete;
    /// 禁用移动赋值运算符
    unique_strong_ref_ptr& operator=(unique_strong_ref_ptr&&) = delete;

    /// 析构函数
    ~unique_strong_ref_ptr() = default;

    /**
     * @brief 解引用运算符
     * @return 返回对象引用
     */
    element_type& operator*() const
    {
        return *_ptr;
    }

    /**
     * @brief 成员访问运算符
     * @return 返回原始指针
     */
    pointer operator->() const
    {
        return _ptr.operator->();
    }

    /**
     * @brief 获取原始指针
     * @return 返回原始指针
     */
    pointer get() const
    {
        return _ptr.get();
    }

    /**
     * @brief 布尔转换运算符
     * @return 如果指针非空返回true，否则返回false
     */
    explicit operator bool() const
    {
        return static_cast<bool>(_ptr);
    }

    /**
     * @brief 转换为unique_weak_ptr
     * @return 返回对应的unique_weak_ptr
     */
    operator unique_weak_ptr<T>() const
    {
        unique_weak_ptr<T> weak;
        weak._ptr = _ptr;
        return weak;
    }

private:
    template <typename T0>
    friend class unique_weak_ptr;

    template <class To, class From>
    friend unique_strong_ref_ptr<To> static_pointer_cast(unique_strong_ref_ptr<From> const& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> static_pointer_cast(unique_strong_ref_ptr<From>&& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> const_pointer_cast(unique_strong_ref_ptr<From> const& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> const_pointer_cast(unique_strong_ref_ptr<From>&& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> reinterpret_pointer_cast(unique_strong_ref_ptr<From> const& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> reinterpret_pointer_cast(unique_strong_ref_ptr<From>&& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> dynamic_pointer_cast(unique_strong_ref_ptr<From> const& other);

    template <class To, class From>
    friend unique_strong_ref_ptr<To> dynamic_pointer_cast(unique_strong_ref_ptr<From>&& other);

    /**
     * @brief 私有构造函数
     * @param ptr shared_ptr对象
     *
     * 仅unique_weak_ptr::lock()可以调用
     */
    unique_strong_ref_ptr(std::shared_ptr<element_type> ptr) : _ptr(std::move(ptr)) { }

    std::shared_ptr<element_type> _ptr;     ///< 内部shared_ptr，持有临时强引用
};

// unique_trackable_ptr 函数

/**
 * @brief 相等比较运算符
 * @tparam T1 左侧unique_trackable_ptr的元素类型
 * @tparam T2 右侧unique_trackable_ptr的元素类型
 * @param left 左侧unique_trackable_ptr
 * @param right 右侧unique_trackable_ptr
 * @return 如果两个指针指向相同对象返回true，否则返回false
 */
template <typename T1, typename T2>
bool operator==(unique_trackable_ptr<T1> const& left, unique_trackable_ptr<T2> const& right)
{
    return left.get() == right.get();
}

/**
 * @brief 三向比较运算符
 * @tparam T1 左侧unique_trackable_ptr的元素类型
 * @tparam T2 右侧unique_trackable_ptr的元素类型
 * @param left 左侧unique_trackable_ptr
 * @param right 右侧unique_trackable_ptr
 * @return 返回三向比较结果
 */
template <typename T1, typename T2>
std::strong_ordering operator<=>(unique_trackable_ptr<T1> const& left, unique_trackable_ptr<T2> const& right)
{
    return left.get() <=> right.get();
}

/**
 * @brief 与nullptr比较运算符
 * @tparam T1 unique_trackable_ptr的元素类型
 * @param left unique_trackable_ptr
 * @return 如果指针为空返回true，否则返回false
 */
template <typename T1>
bool operator==(unique_trackable_ptr<T1> const& left, std::nullptr_t)
{
    return left.get() == nullptr;
}

/**
 * @brief 与nullptr三向比较运算符
 * @tparam T1 unique_trackable_ptr的元素类型
 * @param left unique_trackable_ptr
 * @return 返回三向比较结果
 */
template <typename T1>
std::strong_ordering operator<=>(unique_trackable_ptr<T1> const& left, std::nullptr_t)
{
    return left.get() <=> nullptr;
}

/**
 * @brief 创建unique_trackable_ptr（非数组类型）
 * @tparam T 对象类型
 * @tparam Args 构造函数参数类型
 * @param args 构造函数参数
 * @return 返回新创建的unique_trackable_ptr
 *
 * 职责：
 *   创建一个新对象并返回管理它的unique_trackable_ptr
 *   类似std::make_unique和std::make_shared
 */
template <typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, unique_trackable_ptr<T>> make_unique_trackable(Args&&... args)
{
    unique_trackable_ptr<T> ptr;
    ptr._ptr = std::make_shared<T>(std::forward<Args>(args)...);
    return ptr;
}

/**
 * @brief 创建unique_trackable_ptr（无界数组类型）
 * @tparam T 数组类型
 * @param N 数组大小
 * @return 返回新创建的unique_trackable_ptr
 */
template <typename T>
std::enable_if_t<std::is_unbounded_array_v<T>, unique_trackable_ptr<T>> make_unique_trackable(std::size_t N)
{
    unique_trackable_ptr<T> ptr;
    ptr._ptr = std::make_shared<T>(N);
    return ptr;
}

/**
 * @brief 创建unique_trackable_ptr（无界数组类型，带初始值）
 * @tparam T 数组类型
 * @param N 数组大小
 * @param val 初始值
 * @return 返回新创建的unique_trackable_ptr
 */
template <typename T>
std::enable_if_t<std::is_unbounded_array_v<T>, unique_trackable_ptr<T>> make_unique_trackable(std::size_t N, std::remove_extent_t<T> const& val)
{
    unique_trackable_ptr<T> ptr;
    ptr._ptr = std::make_shared<T>(N, val);
    return ptr;
}

/**
 * @brief 创建unique_trackable_ptr（有界数组类型）
 * @tparam T 数组类型
 * @return 返回新创建的unique_trackable_ptr
 */
template <typename T>
std::enable_if_t<std::is_bounded_array_v<T>, unique_trackable_ptr<T>> make_unique_trackable()
{
    unique_trackable_ptr<T> ptr;
    ptr._ptr = std::make_shared<T>();
    return ptr;
}

/**
 * @brief 创建unique_trackable_ptr（有界数组类型，带初始值）
 * @tparam T 数组类型
 * @param val 初始值
 * @return 返回新创建的unique_trackable_ptr
 */
template <typename T>
std::enable_if_t<std::is_bounded_array_v<T>, unique_trackable_ptr<T>> make_unique_trackable(std::remove_extent_t<T> const& val)
{
    unique_trackable_ptr<T> ptr;
    ptr._ptr = std::make_shared<T>(val);
    return ptr;
}

// unique_weak_ptr 函数

/**
 * @brief 静态类型转换
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_weak_ptr
 * @return 返回转换后的unique_weak_ptr
 */
template <class To, class From>
unique_weak_ptr<To> static_pointer_cast(unique_weak_ptr<From> const& other)
{
    unique_weak_ptr<To> to;
    to._ptr = std::static_pointer_cast<To>(other._ptr.lock());
    return to;
}

/**
 * @brief const类型转换
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_weak_ptr
 * @return 返回转换后的unique_weak_ptr
 */
template <class To, class From>
unique_weak_ptr<To> const_pointer_cast(unique_weak_ptr<From> const& other)
{
    unique_weak_ptr<To> to;
    to._ptr = std::const_pointer_cast<To>(other._ptr.lock());
    return to;
}

/**
 * @brief 重解释类型转换
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_weak_ptr
 * @return 返回转换后的unique_weak_ptr
 */
template <class To, class From>
unique_weak_ptr<To> reinterpret_pointer_cast(unique_weak_ptr<From> const& other)
{
    unique_weak_ptr<To> to;
    to._ptr = std::reinterpret_pointer_cast<To>(other._ptr.lock());
    return to;
}

/**
 * @brief 动态类型转换
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_weak_ptr
 * @return 返回转换后的unique_weak_ptr
 */
template <class To, class From>
unique_weak_ptr<To> dynamic_pointer_cast(unique_weak_ptr<From> const& other)
{
    unique_weak_ptr<To> to;
    to._ptr = std::dynamic_pointer_cast<To>(other._ptr.lock());
    return to;
}

// unique_strong_ref_ptr 函数

/**
 * @brief 相等比较运算符
 * @tparam T1 左侧unique_strong_ref_ptr的元素类型
 * @tparam T2 右侧unique_strong_ref_ptr的元素类型
 * @param left 左侧unique_strong_ref_ptr
 * @param right 右侧unique_strong_ref_ptr
 * @return 如果两个指针指向相同对象返回true，否则返回false
 */
template <typename T1, typename T2>
bool operator==(unique_strong_ref_ptr<T1> const& left, unique_strong_ref_ptr<T2> const& right)
{
    return left.get() == right.get();
}

/**
 * @brief 三向比较运算符
 * @tparam T1 左侧unique_strong_ref_ptr的元素类型
 * @tparam T2 右侧unique_strong_ref_ptr的元素类型
 * @param left 左侧unique_strong_ref_ptr
 * @param right 右侧unique_strong_ref_ptr
 * @return 返回三向比较结果
 */
template <typename T1, typename T2>
std::strong_ordering operator<=>(unique_strong_ref_ptr<T1> const& left, unique_strong_ref_ptr<T2> const& right)
{
    return left.get() <=> right.get();
}

/**
 * @brief 与nullptr比较运算符
 * @tparam T1 unique_strong_ref_ptr的元素类型
 * @param left unique_strong_ref_ptr
 * @return 如果指针为空返回true，否则返回false
 */
template <typename T1>
bool operator==(unique_strong_ref_ptr<T1> const& left, std::nullptr_t)
{
    return left.get() == nullptr;
}

/**
 * @brief 与nullptr三向比较运算符
 * @tparam T1 unique_strong_ref_ptr的元素类型
 * @param left unique_strong_ref_ptr
 * @return 返回三向比较结果
 */
template <typename T1>
std::strong_ordering operator<=>(unique_strong_ref_ptr<T1> const& left, std::nullptr_t)
{
    return left.get() <=> nullptr;
}

/**
 * @brief 静态类型转换（const版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> static_pointer_cast(unique_strong_ref_ptr<From> const& other)
{
    return unique_strong_ref_ptr<To>(std::static_pointer_cast<To>(other._ptr));
}

/**
 * @brief 静态类型转换（右值版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> static_pointer_cast(unique_strong_ref_ptr<From>&& other)
{
    return unique_strong_ref_ptr<To>(std::static_pointer_cast<To>(std::move(other._ptr)));
}

/**
 * @brief const类型转换（const版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> const_pointer_cast(unique_strong_ref_ptr<From> const& other)
{
    return unique_strong_ref_ptr<To>(std::const_pointer_cast<To>(other._ptr));
}

/**
 * @brief const类型转换（右值版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> const_pointer_cast(unique_strong_ref_ptr<From>&& other)
{
    return unique_strong_ref_ptr<To>(std::const_pointer_cast<To>(std::move(other._ptr)));
}

/**
 * @brief 重解释类型转换（const版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> reinterpret_pointer_cast(unique_strong_ref_ptr<From> const& other)
{
    return unique_strong_ref_ptr<To>(std::reinterpret_pointer_cast<To>(other._ptr));
}

/**
 * @brief 重解释类型转换（右值版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> reinterpret_pointer_cast(unique_strong_ref_ptr<From>&& other)
{
    return unique_strong_ref_ptr<To>(std::reinterpret_pointer_cast<To>(std::move(other._ptr)));
}

/**
 * @brief 动态类型转换（const版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> dynamic_pointer_cast(unique_strong_ref_ptr<From> const& other)
{
    return unique_strong_ref_ptr<To>(std::dynamic_pointer_cast<To>(other._ptr));
}

/**
 * @brief 动态类型转换（右值版本）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param other 源unique_strong_ref_ptr
 * @return 返回转换后的unique_strong_ref_ptr
 */
template <class To, class From>
unique_strong_ref_ptr<To> dynamic_pointer_cast(unique_strong_ref_ptr<From>&& other)
{
    return unique_strong_ref_ptr<To>(std::dynamic_pointer_cast<To>(std::move(other._ptr)));
}
}

#endif // TRINITYCORE_UNIQUE_TRACKABLE_PTR_H
