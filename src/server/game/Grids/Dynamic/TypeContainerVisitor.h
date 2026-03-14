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

#ifndef TRINITY_TYPECONTAINERVISITOR_H
#define TRINITY_TYPECONTAINERVISITOR_H

/**
 * @file TypeContainerVisitor.h
 * @brief 类型容器访问器实现 - 访问者模式
 *
 * 本文件实现了访问者模式(Visitor Pattern),用于遍历和操作 TypeContainer 系统中的异构容器。
 * 主要功能包括:
 * - 提供类型安全的容器遍历机制
 * - 支持编译期多态,避免运行时类型检查开销
 * - 通过模板递归展开实现对复合类型容器的访问
 *
 * 设计模式:
 * - 访问者模式: 将数据结构与操作分离,使得可以在不修改数据结构的前提下定义新操作
 * - 模板元编程: 利用模板特化和递归在编译期生成类型安全的访问代码
 *
 * 性能特点:
 * - 所有类型检查在编译期完成,运行时无额外开销
 * - 编译期递归展开,避免虚函数调用开销
 * - 内联友好,编译器可充分优化
 */

#include "Define.h"
#include "Dynamic/TypeContainer.h"

// 前向声明
template<class T, class Y> class TypeContainerVisitor;

/**
 * @brief 访问者辅助函数 - 通用入口点
 *
 * 这是访问者模式的主要入口函数,将访问者和容器连接起来。
 * 当编译器无法匹配更特化的版本时,会调用此版本。
 *
 * @tparam VISITOR 访问者类型,必须提供 Visit() 方法
 * @tparam TYPE_CONTAINER 容器类型
 *
 * @param v 访问者对象的引用,用于执行访问操作
 * @param c 容器对象的引用,存储待访问的数据
 *
 * @note 这是一个转发函数,实际的访问逻辑由访问者的 Visit() 方法实现
 * @note 通过模板特化机制,编译器会自动选择最匹配的重载版本
 */
template<class VISITOR, class TYPE_CONTAINER>
void VisitorHelper(VISITOR &v, TYPE_CONTAINER &c)
{
    v.Visit(c);
}

/**
 * @brief 递归终止条件 - 空类型列表特化
 *
 * 当递归遍历到 TypeNull 时停止,这是类型列表遍历的基准情况(Base Case)。
 * TypeNull 表示类型列表的末尾,类似于链表的 nullptr。
 *
 * @tparam VISITOR 访问者类型(虽然此处不使用,但必须保留以匹配模板签名)
 *
 * @param v 访问者对象(未使用)
 * @param c 空类型容器(未使用)
 *
 * @note 这是一个空函数,作为递归展开的终止条件
 * @note 编译器会优化掉这个空函数调用,无运行时开销
 */
template<class VISITOR>
void VisitorHelper(VISITOR &/*v*/, ContainerMapList<TypeNull> &/*c*/) { }

/**
 * @brief 单元素容器访问 - 最简情况的特化
 *
 * 当容器只包含单一类型 T 时,直接访问该元素。
 * 这是递归展开的叶子节点,直接调用访问者的 Visit() 方法。
 *
 * @tparam VISITOR 访问者类型
 * @tparam T 容器存储的具体类型
 *
 * @param v 访问者对象,其 Visit() 方法将被调用
 * @param c 单元素容器,包含一个 _element 成员
 *
 * @note _element 是 ContainerMapList<T> 中存储的实际数据容器
 * @note 调用时机: 当递归展开到达类型列表的最后一个具体类型时
 * @note 性能说明: 编译器会内联此函数调用,避免函数调用开销
 */
template<class VISITOR, class T>
void VisitorHelper(VISITOR &v, ContainerMapList<T> &c)
{
    v.Visit(c._element);
}

/**
 * @brief 递归遍历复合类型列表 - TypeList 的展开访问
 *
 * 当容器包含多个类型(TypeList)时,递归访问头部类型和尾部类型列表。
 * 这实现了编译期的类型列表遍历,将 TypeList<H, T> 分解为:
 * - H: 头部类型(当前要访问的类型)
 * - T: 尾部类型列表(剩余待访问的类型)
 *
 * @tparam VISITOR 访问者类型
 * @tparam H 类型列表的头部类型(Head)
 * @tparam T 类型列表的尾部类型列表(Tail),通常是另一个 TypeList 或 TypeNull
 *
 * @param v 访问者对象
 * @param c 复合容器,包含 _elements(头部容器)和 _TailElements(尾部容器)
 *
 * 递归过程:
 * 1. 首先访问 _elements(ContainerMapList<H>),处理头部类型 H
 * 2. 然后访问 _TailElements(容器类型 T),递归处理剩余类型
 * 3. 递归终止于 TypeNull 或单元素容器
 *
 * @note 编译期递归: 所有递归在编译期展开,无运行时递归开销
 * @note 编译器会生成一系列顺序的函数调用,等效于手动展开的代码
 */
template<class VISITOR, class H, class T>
void VisitorHelper(VISITOR &v, ContainerMapList<TypeList<H, T> > &c)
{
    // 先访问头部类型容器
    VisitorHelper(v, c._elements);
    // 再递归访问尾部类型列表容器
    VisitorHelper(v, c._TailElements);
}

/**
 * @brief TypeMapContainer 容器访问 - 顶层容器适配器
 *
 * 这是访问 TypeMapContainer 的入口点,将外层容器剥离,转发到内部元素容器的访问。
 * TypeMapContainer 是一个包装器,内部存储实际的类型列表容器。
 *
 * @tparam VISITOR 访问者类型
 * @tparam OBJECT_TYPES 对象类型列表,定义了容器可以存储的类型集合
 *
 * @param v 访问者对象
 * @param c TypeMapContainer 容器对象
 *
 * @note GetElements() 返回内部的 ContainerMapList,然后递归调用相应的 VisitorHelper
 * @note 这是访问 TypeMapContainer 的主要入口,通常由用户代码间接调用
 */
template<class VISITOR, class OBJECT_TYPES>
void VisitorHelper(VISITOR &v, TypeMapContainer<OBJECT_TYPES> &c)
{
    // 剥离外层容器,访问内部元素
    VisitorHelper(v, c.GetElements());
}

/**
 * @brief 无序映射容器的递归终止条件 - 空类型列表特化
 *
 * 与 ContainerMapList<TypeNull> 类似,这是 TypeUnorderedMap 类型列表遍历的基准情况。
 * 当递归到达 TypeNull 时停止遍历。
 *
 * @tparam VISITOR 访问者类型(未使用)
 * @tparam KEY_TYPE 键类型,用于无序映射的键
 *
 * @param v 访问者对象(未使用)
 * @param c 空类型的无序映射容器(未使用)
 *
 * @note 这是一个空函数,作为递归展开的终止条件
 * @note 编译器会完全优化掉这个调用
 */
template<class VISITOR, class KEY_TYPE>
void VisitorHelper(VISITOR& /*v*/, ContainerUnorderedMap<TypeNull, KEY_TYPE>& /*c*/) { }

/**
 * @brief 单元素无序映射容器访问 - 最简情况的特化
 *
 * 当无序映射容器只包含单一类型 T 时,直接访问该元素。
 * 这是无序映射容器递归展开的叶子节点。
 *
 * @tparam VISITOR 访问者类型
 * @tparam KEY_TYPE 键类型,用于无序映射的键
 * @tparam T 容器存储的具体值类型
 *
 * @param v 访问者对象,其 Visit() 方法将被调用
 * @param c 单元素无序映射容器,包含一个 _element 成员
 *
 * @note _element 是 ContainerUnorderedMap<T, KEY_TYPE> 中存储的实际数据容器
 *       通常是一个 std::unordered_map<KEY_TYPE, T*> 类型
 * @note 与 ContainerMapList 类似,但使用键值对存储方式
 */
template<class VISITOR, class KEY_TYPE, class T>
void VisitorHelper(VISITOR& v, ContainerUnorderedMap<T, KEY_TYPE>& c)
{
    v.Visit(c._element);
}

/**
 * @brief 递归遍历复合类型无序映射容器 - TypeList 的展开访问
 *
 * 当无序映射容器包含多个类型(TypeList)时,递归访问头部类型和尾部类型列表。
 * 这与 ContainerMapList 的递归逻辑相同,但适用于无序映射容器。
 *
 * @tparam VISITOR 访问者类型
 * @tparam KEY_TYPE 键类型,用于无序映射的键
 * @tparam H 类型列表的头部类型(Head)
 * @tparam T 类型列表的尾部类型列表(Tail)
 *
 * @param v 访问者对象
 * @param c 复合无序映射容器,包含 _elements(头部容器)和 _TailElements(尾部容器)
 *
 * 递归过程:
 * 1. 访问 _elements(ContainerUnorderedMap<H, KEY_TYPE>),处理头部类型 H
 * 2. 访问 _TailElements(容器类型 T),递归处理剩余类型
 * 3. 递归终止于 TypeNull 或单元素容器
 *
 * @note 编译期递归展开,无运行时递归开销
 * @note 使用键值对存储,适合快速查找场景
 */
template<class VISITOR, class KEY_TYPE, class H, class T>
void VisitorHelper(VISITOR& v, ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE>& c)
{
    // 先访问头部类型容器
    VisitorHelper(v, c._elements);
    // 再递归访问尾部类型列表容器
    VisitorHelper(v, c._TailElements);
}

/**
 * @brief TypeUnorderedMapContainer 容器访问 - 顶层无序映射容器适配器
 *
 * 这是访问 TypeUnorderedMapContainer 的入口点,将外层容器剥离,转发到内部元素容器的访问。
 * TypeUnorderedMapContainer 是一个包装器,内部存储实际的类型列表无序映射容器。
 *
 * @tparam VISITOR 访问者类型
 * @tparam OBJECT_TYPES 对象类型列表,定义了容器可以存储的类型集合
 * @tparam KEY_TYPE 键类型,用于无序映射的键(通常是 ObjectGuid 或 uint32)
 *
 * @param v 访问者对象
 * @param c TypeUnorderedMapContainer 容器对象
 *
 * @note GetElements() 返回内部的 ContainerUnorderedMap,然后递归调用相应的 VisitorHelper
 * @note 这是访问 TypeUnorderedMapContainer 的主要入口
 * @note 无序映射容器支持 O(1) 平均时间复杂度的查找操作
 */
template<class VISITOR, class OBJECT_TYPES, class KEY_TYPE>
void VisitorHelper(VISITOR& v, TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>& c)
{
    // 剥离外层容器,访问内部元素
    VisitorHelper(v, c.GetElements());
}

/**
 * @class TypeContainerVisitor
 * @brief 类型容器访问器 - 访问者模式的实现
 *
 * 该类实现了访问者模式,用于遍历和操作 TypeContainer 系列容器中的异构数据。
 * 它持有一个访问者对象的引用,并将访问请求转发到具体的容器类型。
 *
 * 设计模式:
 * - 访问者模式(Visitor Pattern): 将数据结构与操作分离
 * - 模板方法模式: 通过模板参数化访问者和容器类型
 *
 * 工作原理:
 * 1. 用户创建一个访问者对象,实现 Visit() 方法重载来处理不同类型的对象
 * 2. 使用 TypeContainerVisitor 包装该访问者
 * 3. 调用 Visit() 方法遍历容器,自动调用访问者的相应 Visit() 方法
 *
 * 典型用法:
 * @code
 * // 定义访问者
 * class MyVisitor {
 * public:
 *     void Visit(std::map<uint32, Player*>& m) { /* 处理 Player *\/ }
 *     void Visit(std::map<uint32, Creature*>& m) { /* 处理 Creature *\/ }
 * };
 *
 * // 使用访问者
 * MyVisitor visitor;
 * TypeContainerVisitor<MyVisitor, TypeMapContainer<TypeList<Player, Creature>>> containerVisitor(visitor);
 * containerVisitor.Visit(myContainer);
 * @endcode
 *
 * @tparam VISITOR 访问者类型,必须提供针对容器中各种类型的 Visit() 方法重载
 * @tparam TYPE_CONTAINER 容器类型,可以是 TypeMapContainer 或 TypeUnorderedMapContainer
 *
 * @note 类型安全: 所有类型检查在编译期完成,运行时无类型转换开销
 * @note 零开销抽象: 通过模板内联,编译后代码与手写循环一样高效
 * @note 线程安全: 本身不维护状态,可安全地在多线程环境中使用(前提是访问者和容器也是线程安全的)
 */
template<class VISITOR, class TYPE_CONTAINER>
class TypeContainerVisitor
{
    public:
        /**
         * @brief 构造函数 - 初始化访问者引用
         *
         * @param v 访问者对象的引用,生命周期必须长于本对象
         *
         * @warning 访问者对象必须在本对象销毁前保持有效,否则会产生悬空引用
         */
        TypeContainerVisitor(VISITOR &v) : i_visitor(v) { }

        /**
         * @brief 访问容器(非 const 版本)
         *
         * 启动对容器的遍历,将访问请求转发到 VisitorHelper 函数。
         * VisitorHelper 会递归展开容器的类型列表,调用访问者相应的 Visit() 方法。
         *
         * @param c 容器对象的引用,将被遍历访问
         *
         * @note 调用时机: 当需要修改容器中的对象时使用此版本
         * @note 性能说明: 函数会被内联,实际调用开销由编译器优化消除
         */
        void Visit(TYPE_CONTAINER& c)
        {
            VisitorHelper(i_visitor, c);
        }

        /**
         * @brief 访问容器(const 版本)
         *
         * 启动对容器的只读遍历,将访问请求转发到 VisitorHelper 函数。
         * 提供了对 const 容器的访问支持,允许在只读上下文中使用访问者。
         *
         * @param c 容器对象的 const 引用,将被遍历访问(只读)
         *
         * @note 调用时机: 当只需要读取容器中的对象,不修改时使用此版本
         * @note 访问者的 Visit() 方法也需要提供 const 版本重载
         * @note 性能说明: 函数会被内联,实际调用开销由编译器优化消除
         */
        void Visit(TYPE_CONTAINER const& c) const
        {
            VisitorHelper(i_visitor, c);
        }

    private:
        VISITOR &i_visitor; ///< 访问者对象的引用,用于执行实际的访问操作
};
#endif
