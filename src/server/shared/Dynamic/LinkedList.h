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
 * @file LinkedList.h
 * @brief 双向链表基础数据结构模块
 *
 * 本模块提供了双向链表的基础实现，包括链表元素类（LinkedListElement）和链表头类（LinkedListHead）。
 * 这是一个底层数据结构，被 Reference 和 RefManager 等引用管理系统广泛使用。
 *
 * 主要特点:
 * - 双向链表实现，支持前后双向遍历
 * - 提供 STL 风格的迭代器支持
 * - 采用哨兵节点（sentinel）设计，简化边界处理
 * - 禁止拷贝，确保数据结构安全性
 *
 * 适用场景:
 * - 需要频繁插入/删除操作的容器
 * - 引用管理系统中的对象关联
 * - 需要顺序访问的数据集合
 */

#ifndef _LINKEDLIST
#define _LINKEDLIST

#include "Define.h"
#include <iterator>

//============================================
class LinkedListHead;

/**
 * @class LinkedListElement
 * @brief 双向链表元素基类
 *
 * 表示双向链表中的一个节点，包含指向前驱和后继节点的指针。
 * 该类设计为基类，其他类可以继承此类来获得链表节点的能力。
 *
 * 设计要点:
 * - 使用哨兵节点模式：链表头尾的 iNext/iPrev 指向哨兵节点
 * - 析构时自动从链表中移除（调用 delink）
 * - 禁止拷贝构造和赋值，避免指针混乱
 *
 * @性能说明:
 * - 插入/删除操作: O(1)
 * - 查找操作: O(n)
 */
class LinkedListElement
{
    private:
        friend class LinkedListHead;  ///< 允许链表头类访问私有成员

        LinkedListElement* iNext;  ///< 指向下一个节点的指针
        LinkedListElement* iPrev;  ///< 指向前一个节点的指针

    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化一个未链接到任何链表的独立节点，前后指针都为 nullptr。
         * 节点创建后需要通过 insertBefore/insertAfter 或链表头的插入方法加入链表。
         */
        LinkedListElement() : iNext(nullptr), iPrev(nullptr) { }

        /**
         * @brief 检查是否存在后继节点
         * @return 如果存在有效的后继节点返回 true，否则返回 false
         *
         * 哨兵节点的判断：如果 iNext->iNext 为 nullptr，说明 iNext 是链表尾部的哨兵节点，
         * 此时 hasNext() 返回 false，表示没有有效的后继元素。
         */
        bool hasNext() const  { return (iNext && iNext->iNext != nullptr); }

        /**
         * @brief 检查是否存在前驱节点
         * @return 如果存在有效的前驱节点返回 true，否则返回 false
         *
         * 哨兵节点的判断：如果 iPrev->iPrev 为 nullptr，说明 iPrev 是链表头部的哨兵节点，
         * 此时 hasPrev() 返回 false，表示没有有效的前驱元素。
         */
        bool hasPrev() const  { return (iPrev && iPrev->iPrev != nullptr); }

        /**
         * @brief 检查节点是否在链表中
         * @return 如果节点已链接到链表中返回 true，否则返回 false
         *
         * 节点只有同时具有前驱和后继指针时才被视为在链表中。
         * 独立节点或已从链表移除的节点返回 false。
         */
        bool isInList() const { return (iNext != nullptr && iPrev != nullptr); }

        /**
         * @brief 获取后继节点（带有效性检查）
         * @return 如果存在有效后继节点则返回其指针，否则返回 nullptr
         *
         * 安全的访问方法，会先检查后继节点是否为有效元素（非哨兵节点）。
         * 推荐在不确定链表结构时使用。
         */
        LinkedListElement      * next()       { return hasNext() ? iNext : nullptr; }
        LinkedListElement const* next() const { return hasNext() ? iNext : nullptr; }

        /**
         * @brief 获取前驱节点（带有效性检查）
         * @return 如果存在有效前驱节点则返回其指针，否则返回 nullptr
         *
         * 安全的访问方法，会先检查前驱节点是否为有效元素（非哨兵节点）。
         * 推荐在不确定链表结构时使用。
         */
        LinkedListElement      * prev()       { return hasPrev() ? iPrev : nullptr; }
        LinkedListElement const* prev() const { return hasPrev() ? iPrev : nullptr; }

        /**
         * @brief 获取后继节点（无检查）
         * @return 直接返回 iNext 指针，可能是哨兵节点或 nullptr
         *
         * 高性能版本，跳过有效性检查，直接返回原始指针。
         * 调用者必须确保链表结构的正确性。
         * @warning 不当使用可能导致访问哨兵节点或空指针
         */
        LinkedListElement      * nocheck_next()       { return iNext; }
        LinkedListElement const* nocheck_next() const { return iNext; }

        /**
         * @brief 获取前驱节点（无检查）
         * @return 直接返回 iPrev 指针，可能是哨兵节点或 nullptr
         *
         * 高性能版本，跳过有效性检查，直接返回原始指针。
         * 调用者必须确保链表结构的正确性。
         * @warning 不当使用可能导致访问哨兵节点或空指针
         */
        LinkedListElement      * nocheck_prev()       { return iPrev; }
        LinkedListElement const* nocheck_prev() const { return iPrev; }

        /**
         * @brief 将节点从链表中移除
         *
         * 解除此节点与前后节点的链接关系，将其从链表中独立出来。
         * 如果节点本就不在链表中，则不做任何操作。
         *
         * 操作步骤:
         * 1. 检查节点是否在链表中
         * 2. 让前驱节点指向后继节点
         * 3. 让后继节点指向前驱节点
         * 4. 将本节点的前后指针置空
         *
         * @性能说明 时间复杂度 O(1)
         */
        void delink()
        {
            if (!isInList())
                return;

            // 将前后节点互相连接，跳过本节点
            iNext->iPrev = iPrev;
            iPrev->iNext = iNext;
            // 清空本节点的链接指针
            iNext = nullptr;
            iPrev = nullptr;
        }

        /**
         * @brief 在本节点之前插入新节点
         * @param pElem 要插入的新节点指针
         *
         * 将 pElem 插入到本节点的前面，更新相关的链接指针。
         * 假设本节点已在链表中或为链表头的哨兵节点。
         *
         * @性能说明 时间复杂度 O(1)
         */
        void insertBefore(LinkedListElement* pElem)
        {
            // 设置新节点的链接关系
            pElem->iNext = this;
            pElem->iPrev = iPrev;
            // 更新前后节点的指向
            iPrev->iNext = pElem;
            iPrev = pElem;
        }

        /**
         * @brief 在本节点之后插入新节点
         * @param pElem 要插入的新节点指针
         *
         * 将 pElem 插入到本节点的后面，更新相关的链接指针。
         * 假设本节点已在链表中或为链表尾的哨兵节点。
         *
         * @性能说明 时间复杂度 O(1)
         */
        void insertAfter(LinkedListElement* pElem)
        {
            // 设置新节点的链接关系
            pElem->iPrev = this;
            pElem->iNext = iNext;
            // 更新前后节点的指向
            iNext->iPrev = pElem;
            iNext = pElem;
        }

    private:
        LinkedListElement(LinkedListElement const&) = delete;             ///< 禁止拷贝构造
        LinkedListElement& operator=(LinkedListElement const&) = delete;  ///< 禁止赋值操作

    protected:
        /**
         * @brief 保护析构函数
         *
         * 析构时自动从链表中移除节点，防止悬空指针。
         * 设计为 protected，确保只能通过派生类析构。
         */
        ~LinkedListElement()
        {
            delink();
        }
};

//============================================

/**
 * @class LinkedListHead
 * @brief 双向链表头管理类
 *
 * 管理整个双向链表，包含头尾哨兵节点和链表大小信息。
 * 提供链表的插入、查询和迭代器支持等核心操作。
 *
 * 设计要点:
 * - 使用两个哨兵节点（iFirst 和 iLast）简化边界处理
 * - iFirst 作为头部哨兵，其 iNext 指向第一个真实元素
 * - iLast 作为尾部哨兵，其 iPrev 指向最后一个真实元素
 * - 提供 STL 风格的迭代器，支持范围 for 循环
 *
 * 哨兵节点优势:
 * - 插入/删除操作无需特殊处理空链表或边界情况
 * - 所有元素都有前后节点，简化逻辑
 * - 迭代器结束判断更加简单（指向哨兵即结束）
 *
 * @性能说明:
 * - 插入/删除: O(1)
 * - 获取大小: O(1)（如果维护了 iSize）或 O(n)（需要遍历计数）
 */
class LinkedListHead
{
    private:
        LinkedListElement iFirst;  ///< 头部哨兵节点，iNext 指向第一个真实元素
        LinkedListElement iLast;   ///< 尾部哨兵节点，iPrev 指向最后一个真实元素
        uint32 iSize;              ///< 链表元素计数（可选维护，为 0 表示未维护）

    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个空链表，初始化头尾哨兵节点使其互相指向。
         * 初始状态下：
         * - iFirst.iNext 指向 iLast
         * - iLast.iPrev 指向 iFirst
         * - 没有真实元素
         */
        LinkedListHead(): iSize(0)
        {
            // 创建空链表：头哨兵的 next 指向尾哨兵，尾哨兵的 prev 指向头哨兵
            iFirst.iNext = &iLast;
            iLast.iPrev = &iFirst;
        }

        /**
         * @brief 检查链表是否为空
         * @return 如果链表中没有真实元素返回 true，否则返回 false
         *
         * 通过检查头哨兵的 iNext 是否在链表中来判断。
         * 空链表时，头哨兵的 iNext 指向尾哨兵，而尾哨兵不在链表中。
         */
        bool isEmpty() const { return(!iFirst.iNext->isInList()); }

        /**
         * @brief 获取链表第一个元素
         * @return 第一个元素的指针，空链表返回 nullptr
         *
         * 返回头哨兵指向的第一个真实元素。
         * 常用于从头开始遍历链表。
         */
        LinkedListElement      * getFirst()       { return (isEmpty() ? nullptr : iFirst.iNext); }
        LinkedListElement const* getFirst() const { return (isEmpty() ? nullptr : iFirst.iNext); }

        /**
         * @brief 获取链表最后一个元素
         * @return 最后一个元素的指针，空链表返回 nullptr
         *
         * 返回尾哨兵指向的最后一个真实元素。
         * 常用于从尾开始反向遍历链表。
         */
        LinkedListElement      * getLast()       { return(isEmpty() ? nullptr : iLast.iPrev); }
        LinkedListElement const* getLast() const { return(isEmpty() ? nullptr : iLast.iPrev); }

        /**
         * @brief 在链表头部插入元素
         * @param pElem 要插入的元素指针
         *
         * 将元素插入到链表的最前面，成为新的第一个元素。
         * 内部调用头哨兵的 insertAfter 方法。
         *
         * @性能说明 时间复杂度 O(1)
         */
        void insertFirst(LinkedListElement* pElem)
        {
            iFirst.insertAfter(pElem);
        }

        /**
         * @brief 在链表尾部插入元素
         * @param pElem 要插入的元素指针
         *
         * 将元素插入到链表的最后面，成为新的最后一个元素。
         * 内部调用尾哨兵的 insertBefore 方法。
         *
         * @性能说明 时间复杂度 O(1)
         */
        void insertLast(LinkedListElement* pElem)
        {
            iLast.insertBefore(pElem);
        }

        /**
         * @brief 获取链表元素数量
         * @return 链表中的元素个数
         *
         * 如果 iSize 被维护（非零），直接返回 iSize。
         * 否则需要遍历链表计数，时间复杂度 O(n)。
         *
         * @性能说明:
         * - 如果维护了 iSize: O(1)
         * - 如果未维护 iSize: O(n)
         */
        uint32 getSize() const
        {
            if (!iSize)
            {
                // 未维护计数，需要遍历计算
                uint32 result = 0;
                LinkedListElement const* e = getFirst();
                while (e)
                {
                    ++result;
                    e = e->next();
                }
                return result;
            }
            else
                return iSize;
        }

        /**
         * @brief 增加链表大小计数
         *
         * 在插入元素时调用，维护链表大小计数器。
         * 仅在派生类中手动调用维护时有效。
         */
        void incSize() { ++iSize; }

        /**
         * @brief 减少链表大小计数
         *
         * 在移除元素时调用，维护链表大小计数器。
         * 仅在派生类中手动调用维护时有效。
         */
        void decSize() { --iSize; }

        /**
         * @class Iterator
         * @brief 双向链表迭代器模板类
         *
         * 提供 STL 风格的双向迭代器，支持正向和反向遍历。
         * 符合 C++ 标准库双向迭代器要求。
         *
         * @tparam _Ty 迭代器指向的元素类型
         */
        template<class _Ty>
            class Iterator
        {
            public:
                typedef std::bidirectional_iterator_tag     iterator_category;  ///< 迭代器类别：双向迭代器
                typedef _Ty                                 value_type;         ///< 元素值类型
                typedef ptrdiff_t                           difference_type;    ///< 指针差值类型
                typedef ptrdiff_t                           distance_type;      ///< 距离类型
                typedef _Ty*                                pointer;            ///< 指针类型
                typedef _Ty const*                          const_pointer;      ///< 常量指针类型
                typedef _Ty&                                reference;          ///< 引用类型
                typedef _Ty const &                         const_reference;    ///< 常量引用类型

                /**
                 * @brief 默认构造函数
                 *
                 * 构造一个空迭代器，指针为 nullptr。
                 * 通常用作结束迭代器或默认值。
                 */
                Iterator() : _Ptr(nullptr)
                {                                           // construct with null node pointer
                }

                /**
                 * @brief 通过节点指针构造迭代器
                 * @param _Pnode 指向链表节点的指针
                 *
                 * 显式构造，避免隐式转换。
                 */
                explicit Iterator(pointer _Pnode) : _Ptr(_Pnode)
                {                                           // construct with node pointer _Pnode
                }

                /**
                 * @brief 赋值操作符
                 * @param _Right 要赋值的常量指针
                 * @return 迭代器自身的引用
                 *
                 * 允许从常量指针赋值到迭代器。
                 */
                Iterator& operator=(const_pointer const& _Right)
                {
                    _Ptr = pointer(_Right);
                    return *this;
                }

                /**
                 * @brief 解引用操作符
                 * @return 当前节点的引用
                 *
                 * 获取迭代器指向的元素引用。
                 * @warning 在空迭代器上调用会导致未定义行为
                 */
                reference operator*()
                {                                           // return designated value
                    return *_Ptr;
                }

                /**
                 * @brief 成员访问操作符
                 * @return 当前节点的指针
                 *
                 * 用于访问元素的成员。
                 * @warning 在空迭代器上调用会导致未定义行为
                 */
                pointer operator->()
                {                                           // return pointer to class object
                    return _Ptr;
                }

                /**
                 * @brief 前置递增操作符
                 * @return 递增后的迭代器引用
                 *
                 * 移动到下一个节点。
                 * @性能说明 O(1)
                 */
                Iterator& operator++()
                {                                           // preincrement
                    _Ptr = _Ptr->next();
                    return (*this);
                }

                /**
                 * @brief 后置递增操作符
                 * @return 递增前的迭代器副本
                 *
                 * 返回当前迭代器的副本，然后移动到下一个节点。
                 * @性能说明 O(1)，但比前置递增多一次拷贝
                 */
                Iterator operator++(int)
                {                                           // postincrement
                    iterator _Tmp = *this;
                    ++*this;
                    return (_Tmp);
                }

                /**
                 * @brief 前置递减操作符
                 * @return 递减后的迭代器引用
                 *
                 * 移动到前一个节点。
                 * @性能说明 O(1)
                 */
                Iterator& operator--()
                {                                           // predecrement
                    _Ptr = _Ptr->prev();
                    return (*this);
                }

                /**
                 * @brief 后置递减操作符
                 * @return 递减前的迭代器副本
                 *
                 * 返回当前迭代器的副本，然后移动到前一个节点。
                 * @性能说明 O(1)，但比前置递减多一次拷贝
                 */
                Iterator operator--(int)
                {                                           // postdecrement
                    iterator _Tmp = *this;
                    --*this;
                    return (_Tmp);
                }

                /**
                 * @brief 相等比较操作符
                 * @param _Right 要比较的右值迭代器
                 * @return 如果两个迭代器指向相同节点返回 true
                 *
                 * 使用默认的 == 操作符实现。
                 */
                bool operator==(Iterator const& _Right) const = default;
                                                            // test for iterator equality

            protected:
                pointer _Ptr;                               ///< 指向当前节点的指针
        };

        typedef Iterator<LinkedListElement> iterator;  ///< 链表迭代器类型别名

    private:
        LinkedListHead(LinkedListHead const&) = delete;             ///< 禁止拷贝构造
        LinkedListHead& operator=(LinkedListHead const&) = delete;  ///< 禁止赋值操作

    protected:
        /**
         * @brief 保护析构函数
         *
         * 设计为 protected，确保只能通过派生类析构。
         * 基类不负责清理链表元素。
         */
        ~LinkedListHead() { }
};

//============================================
#endif
