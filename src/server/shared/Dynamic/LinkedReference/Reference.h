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
 * @file Reference.h
 * @brief 双向引用关系管理模板类
 *
 * 本模块提供了 Reference 模板类，用于管理两个对象之间的双向引用关系。
 * 这是一个核心的基础设施，广泛应用于 TrinityCore 的各种引用管理场景，
 * 如威胁列表、光环效果、目标选择等。
 *
 * 设计特点:
 * - 双向链接：维护从源对象到目标对象的引用关系
 * - 自动管理：通过虚函数通知源对象和目标对象链接状态变化
 * - 链表集成：继承自 LinkedListElement，支持链表管理
 * - 安全检查：提供有效性检查，防止悬空引用
 *
 * 典型应用场景:
 * - 威胁列表管理（Unit <-> ThreatManager）
 * - 光环效果跟踪（Unit <-> Aura）
 * - 目标引用（Unit <-> Unit）
 * - 组队成员管理（Group <-> Player）
 *
 * @tparam TO 目标对象类型（被引用的对象）
 * @tparam FROM 源对象类型（持有引用的对象）
 */

#ifndef _REFERENCE_H
#define _REFERENCE_H

#include "Dynamic/LinkedList.h"
#include "Errors.h" // for ASSERT

//=====================================================

/**
 * @class Reference
 * @brief 双向引用关系模板类
 *
 * 该类表示从源对象到目标对象的引用关系，继承自 LinkedListElement，
 * 可以被组织在链表中进行管理。通过虚函数机制，在引用建立或销毁时
 * 通知相关的源对象和目标对象。
 *
 * 生命周期管理:
 * 1. link() 建立引用：源对象 -> 目标对象
 * 2. unlink() 主动断开：源对象不再需要引用
 * 3. invalidate() 被动失效：目标对象被销毁
 *
 * @tparam TO 目标对象类型（被引用的对象，例如被攻击的目标）
 * @tparam FROM 源对象类型（持有引用的对象，例如攻击者）
 *
 * @example
 * // 定义从 Player 到 Unit 的引用
 * class PlayerToUnitReference : public Reference<Unit, Player> {
 * protected:
 *     void targetObjectBuildLink() override {
 *         // 通知目标 Unit 有新的引用
 *         getTarget()->AddReference(this);
 *     }
 *     void targetObjectDestroyLink() override {
 *         // 通知目标 Unit 引用被移除
 *         getTarget()->RemoveReference(this);
 *     }
 *     void sourceObjectDestroyLink() override {
 *         // 通知源 Player 目标已销毁
 *         GetSource()->OnTargetDestroyed();
 *     }
 * };
 */
template <class TO, class FROM> class Reference : public LinkedListElement
{
    private:
        TO* iRefTo;    ///< 指向目标对象的指针（被引用的对象）
        FROM* iRefFrom; ///< 指向源对象的指针（持有引用的对象）

    protected:
        /**
         * @brief 通知目标对象建立链接
         *
         * 当引用建立时调用，派生类应重写此函数以通知目标对象。
         * 典型操作：将此引用添加到目标对象的引用列表中。
         *
         * 调用时机: link() 成功建立引用后
         */
        virtual void targetObjectBuildLink() = 0;

        /**
         * @brief 通知目标对象销毁链接
         *
         * 当引用被主动断开时调用，派生类应重写此函数以通知目标对象。
         * 典型操作：将此引用从目标对象的引用列表中移除。
         *
         * 调用时机: unlink() 执行时
         */
        virtual void targetObjectDestroyLink() = 0;

        /**
         * @brief 通知源对象目标已销毁
         *
         * 当目标对象被销毁导致引用失效时调用，派生类应重写此函数以通知源对象。
         * 典型操作：清理源对象中与目标相关的状态。
         *
         * 调用时机: invalidate() 执行时
         *
         * @注意 此时 iRefFrom（源对象指针）必须保持有效
         */
        virtual void sourceObjectDestroyLink() = 0;

    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化一个无效的引用，目标和源指针都为 nullptr。
         * 需要后续调用 link() 建立实际的引用关系。
         */
        Reference() { iRefTo = nullptr; iRefFrom = nullptr; }

        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确析构。
         * 注意：析构时不会自动调用 unlink() 或 invalidate()，
         * 派生类需要在析构前正确处理引用关系。
         */
        virtual ~Reference() { }

        /**
         * @brief 建立新的引用关系
         * @param toObj 目标对象指针
         * @param fromObj 源对象指针
         *
         * 建立从源对象到目标对象的引用关系。如果已存在有效引用，
         * 会先断开旧引用再建立新引用。
         *
         * 操作流程:
         * 1. 验证源对象不为空
         * 2. 如果已有有效引用，先调用 unlink() 断开
         * 3. 设置目标和源指针
         * 4. 调用 targetObjectBuildLink() 通知目标对象
         *
         * @调用时机 需要建立新的引用关系时
         * @性能说明 O(1)
         *
         * @warning fromObj 必须不为 nullptr，否则触发断言失败
         */
        void link(TO* toObj, FROM* fromObj)
        {
            ASSERT(fromObj);                                // fromObj MUST not be NULL
            // 如果已有有效引用，先断开
            if (isValid())
                unlink();
            // 只有目标对象有效时才建立引用
            if (toObj != nullptr)
            {
                iRefTo = toObj;
                iRefFrom = fromObj;
                targetObjectBuildLink();
            }
        }

        /**
         * @brief 主动断开引用关系
         *
         * 当源对象不再需要此引用时调用，主动断开与目标对象的链接。
         * 会通知目标对象引用被移除，并清理相关状态。
         *
         * 操作流程:
         * 1. 调用 targetObjectDestroyLink() 通知目标对象
         * 2. 调用 delink() 从链表中移除
         * 3. 清空目标和源指针
         *
         * @调用时机 源对象主动放弃引用时
         * @性能说明 O(1)
         */
        void unlink()
        {
            targetObjectDestroyLink();
            delink();
            iRefTo = nullptr;
            iRefFrom = nullptr;
        }

        /**
         * @brief 使引用失效
         *
         * 当目标对象被销毁时调用，被动地使引用失效。
         * 会通知源对象目标已销毁，但保持源对象指针有效。
         *
         * 操作流程:
         * 1. 调用 sourceObjectDestroyLink() 通知源对象
         * 2. 调用 delink() 从链表中移除
         * 3. 清空目标指针（保留源指针）
         *
         * @调用时机 目标对象被销毁时
         * @性能说明 O(1)
         *
         * @warning iRefFrom（源对象指针）在调用期间必须保持有效
         */
        void invalidate()                                   // the iRefFrom MUST remain!!
        {
            sourceObjectDestroyLink();
            delink();
            iRefTo = nullptr;
        }

        /**
         * @brief 检查引用是否有效
         * @return 如果引用指向有效的目标对象返回 true，否则返回 false
         *
         * 只检查目标指针是否有效，不检查源指针。
         * 一个引用可能处于以下状态：
         * - 有效：目标指针非空，引用关系正常
         * - 失效：目标指针为空（目标已销毁），但源指针可能仍有效
         * - 空：目标和源指针都为空
         */
        bool isValid() const                                // Only check the iRefTo
        {
            return iRefTo != nullptr;
        }

        /**
         * @brief 获取下一个引用节点（带有效性检查）
         * @return 下一个引用节点指针，如果没有则返回 nullptr
         *
         * 重写 LinkedListElement::next()，返回类型转换为 Reference*。
         */
        Reference<TO, FROM>       * next()       { return((Reference<TO, FROM>       *) LinkedListElement::next()); }
        Reference<TO, FROM> const* next() const { return((Reference<TO, FROM> const*) LinkedListElement::next()); }

        /**
         * @brief 获取前一个引用节点（带有效性检查）
         * @return 前一个引用节点指针，如果没有则返回 nullptr
         *
         * 重写 LinkedListElement::prev()，返回类型转换为 Reference*。
         */
        Reference<TO, FROM>       * prev()       { return((Reference<TO, FROM>       *) LinkedListElement::prev()); }
        Reference<TO, FROM> const* prev() const { return((Reference<TO, FROM> const*) LinkedListElement::prev()); }

        /**
         * @brief 获取下一个引用节点（无检查）
         * @return 下一个引用节点指针（可能是哨兵节点）
         *
         * 高性能版本，跳过有效性检查。
         * @warning 调用者必须确保链表结构正确
         */
        Reference<TO, FROM>       * nocheck_next()       { return((Reference<TO, FROM>       *) LinkedListElement::nocheck_next()); }
        Reference<TO, FROM> const* nocheck_next() const { return((Reference<TO, FROM> const*) LinkedListElement::nocheck_next()); }

        /**
         * @brief 获取前一个引用节点（无检查）
         * @return 前一个引用节点指针（可能是哨兵节点）
         *
         * 高性能版本，跳过有效性检查。
         * @warning 调用者必须确保链表结构正确
         */
        Reference<TO, FROM>       * nocheck_prev()       { return((Reference<TO, FROM>       *) LinkedListElement::nocheck_prev()); }
        Reference<TO, FROM> const* nocheck_prev() const { return((Reference<TO, FROM> const*) LinkedListElement::nocheck_prev()); }

        /**
         * @brief 箭头操作符，访问目标对象
         * @return 目标对象指针
         *
         * 提供便捷的指针式访问，可以直接使用 ref->Method() 访问目标对象。
         * @warning 在无效引用上调用会导致未定义行为
         */
        TO* operator->() const { return iRefTo; }

        /**
         * @brief 获取目标对象
         * @return 目标对象指针，如果引用无效则返回 nullptr
         *
         * 获取被引用的目标对象指针。
         */
        TO* getTarget() const { return iRefTo; }

        /**
         * @brief 获取源对象
         * @return 源对象指针
         *
         * 获取持有此引用的源对象指针。
         * 即使在引用失效（invalidate）后，源对象指针仍可能有效。
         */
        FROM* GetSource() const { return iRefFrom; }

    private:
        Reference(Reference const&) = delete;             ///< 禁止拷贝构造
        Reference& operator=(Reference const&) = delete;  ///< 禁止赋值操作
};

//=====================================================
#endif
