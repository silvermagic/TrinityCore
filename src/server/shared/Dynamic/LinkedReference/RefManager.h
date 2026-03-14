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
 * @file RefManager.h
 * @brief 引用管理器模板类
 *
 * 本模块提供了 RefManager 模板类，用于管理一组同类型的引用对象。
 * RefManager 继承自 LinkedListHead，以链表形式存储 Reference 对象，
 * 并提供迭代器支持，方便遍历和管理所有引用。
 *
 * 主要职责:
 * - 作为引用对象的容器，提供统一的存储和管理
 * - 提供迭代器接口，支持范围 for 循环遍历
 * - 在析构时自动清理所有引用，防止悬空指针
 *
 * 典型应用场景:
 * - Unit 的威胁列表管理（ThreatManager 继承自 RefManager）
 * - Unit 的光环列表管理
 * - 对象的引用者追踪
 *
 * @tparam TO 目标对象类型（被引用的对象）
 * @tparam FROM 源对象类型（持有引用的对象）
 */

#ifndef _REFMANAGER_H
#define _REFMANAGER_H

#include "Dynamic/LinkedList.h"
#include "Dynamic/LinkedReference/Reference.h"

/**
 * @class RefManager
 * @brief 引用管理器模板类
 *
 * 管理一组从源对象到目标对象的引用。作为 LinkedListHead 的派生类，
 * 以双向链表形式存储 Reference 对象，并提供类型安全的访问接口。
 *
 * 设计要点:
 * - 继承 LinkedListHead，复用链表管理能力
 * - 提供类型安全的迭代器和访问方法
 * - 析构时自动使所有引用失效，确保安全
 *
 * 与 Reference 的协作:
 * - Reference 对象存储在 RefManager 管理的链表中
 * - RefManager 提供遍历所有引用的能力
 * - 析构时调用所有引用的 invalidate() 方法
 *
 * @tparam TO 目标对象类型（被引用的对象）
 * @tparam FROM 源对象类型（持有引用的对象）
 *
 * @example
 * // 定义威胁引用管理器
 * class ThreatManager : public RefManager<Unit, Unit> {
 *     // 管理所有威胁引用
 *     void UpdateThreat() {
 *         for (auto& ref : *this) {
 *             Unit* target = ref.getTarget();
 *             // 处理威胁逻辑...
 *         }
 *     }
 * };
 */
template <class TO, class FROM>
class RefManager : public LinkedListHead
{
public:
    typedef LinkedListHead::Iterator<Reference<TO, FROM>> iterator;              ///< 可变迭代器类型
    typedef LinkedListHead::Iterator<Reference<TO, FROM> const> const_iterator;  ///< 常量迭代器类型

    /**
     * @brief 默认构造函数
     *
     * 创建一个空的引用管理器。
     * 初始化链表头，此时链表中没有引用对象。
     */
    RefManager() { }

    /**
     * @brief 获取第一个引用对象
     * @return 链表中第一个引用的指针，如果链表为空则返回 nullptr
     *
     * 返回类型安全转换为 Reference<TO, FROM>* 的第一个元素。
     * 常用于手动遍历链表或获取首个引用。
     */
    Reference<TO, FROM>* getFirst() { return static_cast<Reference<TO, FROM>*>(LinkedListHead::getFirst()); }

    /**
     * @brief 获取第一个引用对象（const 版本）
     * @return 链表中第一个引用的常量指针，如果链表为空则返回 nullptr
     *
     * const 重载版本，用于只读访问。
     */
    Reference<TO, FROM> const* getFirst() const { return static_cast<Reference<TO, FROM> const*>(LinkedListHead::getFirst()); }

    /**
     * @brief 获取起始迭代器
     * @return 指向第一个引用的迭代器
     *
     * 用于范围 for 循环的开始位置。
     *
     * @example
     * for (auto& ref : refManager) {
     *     // 处理每个引用
     * }
     */
    iterator begin() { return iterator(getFirst()); }

    /**
     * @brief 获取结束迭代器
     * @return 空迭代器，表示遍历结束
     *
     * 返回指向 nullptr 的迭代器作为结束标志。
     */
    iterator end() { return iterator(nullptr); }

    /**
     * @brief 获取起始迭代器（const 版本）
     * @return 指向第一个引用的常量迭代器
     *
     * const 重载版本，用于只读遍历。
     */
    const_iterator begin() const { return const_iterator(getFirst()); }

    /**
     * @brief 获取结束迭代器（const 版本）
     * @return 空常量迭代器，表示遍历结束
     *
     * const 重载版本。
     */
    const_iterator end() const { return const_iterator(nullptr); }

    /**
     * @brief 虚析构函数
     *
     * 析构时调用 clearReferences() 清理所有引用。
     * 这确保了即使引用对象的目标被销毁，引用也会正确失效，
     * 不会留下悬空指针。
     */
    virtual ~RefManager()
    {
        clearReferences();
    }

    /**
     * @brief 清除所有引用
     *
     * 遍历链表中的所有引用，依次调用 invalidate() 使其失效。
     * 这会导致每个引用通知其源对象目标已销毁。
     *
     * 操作流程:
     * 1. 获取第一个引用
     * 2. 调用其 invalidate() 方法
     * 3. invalidate() 会自动从链表中移除该引用
     * 4. 继续处理新的第一个引用，直到链表为空
     *
     * @调用时机:
     * - RefManager 析构时
     * - 目标对象销毁前需要清理所有引用时
     *
     * @性能说明 O(n)，n 为引用数量
     *
     * @warning invalidate() 会修改链表结构，因此必须在循环中
     *          每次重新获取 getFirst()，而不是使用 next() 遍历
     */
    void clearReferences()
    {
        // 每次从链表头部获取引用，因为 invalidate 会将引用从链表移除
        while (Reference<TO, FROM>* ref = getFirst())
            ref->invalidate();
    }
};

#endif
