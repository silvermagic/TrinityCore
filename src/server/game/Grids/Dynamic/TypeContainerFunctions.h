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

#ifndef TYPECONTAINER_FUNCTIONS_H
#define TYPECONTAINER_FUNCTIONS_H

/**
 * @file TypeContainerFunctions.h
 * @brief 类型容器辅助函数集合
 *
 * 本文件提供了一系列模板辅助函数，用于操作 TypeContainer。
 * 这些函数封装了对类型容器的插入、查找、删除和计数等操作，
 * 使 TypeContainer 更加易用和安全。
 *
 * 主要功能：
 * - 向 ContainerUnorderedMap 插入、查找、删除对象
 * - 向 ContainerMapList 插入、计数对象
 * - 通过模板元编程在编译期确定类型位置
 *
 * 设计思想：
 * 使用递归模板遍历 TypeList，在编译期匹配目标类型，
 * 实现类型安全的容器操作。
 */

#include "Define.h"
#include "Dynamic/TypeList.h"
#include <map>
#include <unordered_map>

/**
 * @namespace Trinity
 * @brief TrinityCore 核心命名空间
 *
 * 包含类型容器系统的核心工具函数和辅助类。
 */
namespace Trinity
{
    // ========== ContainerUnorderedMap 辅助函数 ==========

    /**
     * @brief 向 ContainerUnorderedMap 中插入对象
     *
     * 递归遍历 TypeList，找到匹配的类型后插入对象到对应的哈希表中。
     * 使用编译期类型检查确保类型安全。
     *
     * @tparam SPECIFIC_TYPE 要插入的对象类型
     * @tparam KEY_TYPE 键类型（通常是 ObjectGuid 或 uint32）
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器引用，包含多个类型的哈希表
     * @param handle 对象的键值（如 GUID）
     * @param obj 指向要插入的对象的指针
     *
     * @return true 插入成功
     * @return false 插入失败（类型不匹配或已存在不同对象）
     *
     * @note 时间复杂度：O(1) 平均情况，递归深度由 TypeList 长度决定（编译期）
     * @note 如果键已存在且对象指针相同，返回 false（幂等性保证）
     * @note 如果键已存在但对象指针不同，触发断言失败
     *
     * @example
     * @code
     * ContainerUnorderedMap<TypeList<Player, Creature>, ObjectGuid> container;
     * Player* player = new Player();
     * Insert(container, player->GetGUID(), player);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class KEY_TYPE, class H, class T>
    inline bool Insert(ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE>& elements, KEY_TYPE const& handle, SPECIFIC_TYPE* obj)
    {
        // 编译期检查：当前类型 H 是否匹配目标类型 SPECIFIC_TYPE
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 在对应的哈希表中查找键是否存在
            auto i = elements._elements._element.find(handle);
            if (i == elements._elements._element.end())
            {
                // 键不存在，执行插入操作
                elements._elements._element[handle] = obj;
                return true;
            }
            else
            {
                // 键已存在，验证是否为相同对象（防止重复插入不同对象）
                ASSERT(i->second == obj, "Object with certain key already in but objects are different!");
                return false;
            }
        }

        // 如果当前类型不匹配，检查是否到达 TypeList 末尾
        if constexpr (std::is_same_v<T, TypeNull>)
            return false; // 遍历完整个 TypeList 未找到匹配类型
        else
            // 递归处理 TypeList 的下一层
            return Insert(elements._TailElements, handle, obj);
    }

    /**
     * @brief 在 ContainerUnorderedMap 中查找对象
     *
     * 根据键值在指定类型的哈希表中查找对象。
     * 通过编译期类型推导定位目标哈希表。
     *
     * @tparam SPECIFIC_TYPE 要查找的对象类型
     * @tparam KEY_TYPE 键类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器常量引用
     * @param handle 要查找的键值
     * @param obj 未使用的参数（用于类型推导）
     *
     * @return SPECIFIC_TYPE* 找到的对象指针，未找到返回 nullptr
     *
     * @note 时间复杂度：O(1) 平均情况
     * @note obj 参数仅为编译器提供类型信息，实际值被忽略
     * @note 如果类型不在 TypeList 中，编译期会返回 nullptr
     *
     * @example
     * @code
     * Player* found = Find(container, playerGuid, (Player*)nullptr);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class KEY_TYPE, class H, class T>
    inline SPECIFIC_TYPE* Find(ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE> const& elements, KEY_TYPE const& handle, SPECIFIC_TYPE* obj)
    {
        // 编译期类型匹配检查
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 在对应类型的哈希表中查找
            auto i = elements._elements._element.find(handle);
            if (i == elements._elements._element.end())
                return nullptr; // 未找到，返回空指针
            else
                return i->second; // 返回找到的对象指针
        }

        // 到达 TypeList 末尾仍未找到匹配类型
        if constexpr (std::is_same_v<T, TypeNull>)
            return nullptr;
        else
            // 递归处理下一层
            return Find(elements._TailElements, handle, obj);
    }

    /**
     * @brief 从 ContainerUnorderedMap 中删除对象
     *
     * 根据键值从指定类型的哈希表中删除条目。
     * 不检查对象指针是否匹配，仅根据键删除。
     *
     * @tparam SPECIFIC_TYPE 要删除的对象类型
     * @tparam KEY_TYPE 键类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器引用
     * @param handle 要删除的键值
     * @param obj 未使用的参数（用于类型推导）
     *
     * @return true 删除成功（找到并删除了对应类型）
     * @return false 删除失败（类型不在 TypeList 中）
     *
     * @note 时间复杂度：O(1) 平均情况
     * @note 即使键不存在于哈希表中，只要类型匹配也会返回 true
     * @note 此函数不释放对象内存，仅从容器中移除映射关系
     *
     * @warning 不验证键对应的对象指针，如需验证请先使用 Find
     *
     * @example
     * @code
     * Remove(container, playerGuid, (Player*)nullptr);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class KEY_TYPE, class H, class T>
    inline bool Remove(ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE>& elements, KEY_TYPE const& handle, SPECIFIC_TYPE* obj)
    {
        // 编译期类型匹配检查
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 从对应类型的哈希表中删除键
            elements._elements._element.erase(handle);
            return true;
        }

        // 到达 TypeList 末尾仍未找到匹配类型
        if constexpr (std::is_same_v<T, TypeNull>)
            return false;
        else
            // 递归处理下一层
            return Remove(elements._TailElements, handle, obj);
    }

    /**
     * @brief 获取 ContainerUnorderedMap 中指定类型的元素数量
     *
     * 遍历 TypeList，找到匹配类型后返回其哈希表的元素数量。
     *
     * @tparam SPECIFIC_TYPE 要查询的对象类型
     * @tparam KEY_TYPE 键类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器常量引用
     * @param size 输出参数，存储元素数量
     * @param obj 未使用的参数（用于类型推导）
     *
     * @return true 成功获取大小（类型在 TypeList 中）
     * @return false 获取失败（类型不在 TypeList 中）
     *
     * @note 时间复杂度：O(1)，直接读取哈希表大小
     * @note 通过输出参数而非返回值传递大小，避免与 bool 返回值混淆
     *
     * @example
     * @code
     * size_t playerCount = 0;
     * Size(container, &playerCount, (Player*)nullptr);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class KEY_TYPE, class H, class T>
    inline bool Size(ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE> const& elements, std::size_t* size, SPECIFIC_TYPE* obj)
    {
        // 编译期类型匹配检查
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 获取对应类型哈希表的元素数量
            *size = elements._elements._element.size();
            return true;
        }

        // 到达 TypeList 末尾仍未找到匹配类型
        if constexpr (std::is_same_v<T, TypeNull>)
            return false;
        else
            // 递归处理下一层
            return Size(elements._TailElements, size, obj);
    }

    // ========== ContainerMapList 辅助函数 ==========

    /**
     * @brief 统计 ContainerMapList 中指定类型的元素数量
     *
     * ContainerMapList 用于网格（Grid）系统，存储游戏对象列表。
     * 此函数返回特定类型对象的列表大小。
     *
     * @tparam SPECIFIC_TYPE 要统计的对象类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器常量引用
     * @param fake 未使用的参数（用于类型推导）
     *
     * @return size_t 指定类型的元素数量，类型不存在返回 0
     *
     * @note 时间复杂度：O(1)，直接读取列表大小
     * @note ContainerMapList 使用 LinkedList 元素管理，不同于 ContainerUnorderedMap
     *
     * @example
     * @code
     * size_t creatureCount = Count(gridContainer, (Creature*)nullptr);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class H, class T>
    inline size_t Count(ContainerMapList<TypeList<H, T>> const& elements, SPECIFIC_TYPE* fake)
    {
        // 编译期类型匹配检查
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 返回对应类型列表的元素数量
            return elements._elements._element.getSize();
        }

        // 到达 TypeList 末尾仍未找到匹配类型
        if constexpr (std::is_same_v<T, TypeNull>)
            return 0;
        else
            // 递归处理下一层
            return Count(elements._TailElements, fake);
    }

    /**
     * @brief 将对象插入到 ContainerMapList 中
     *
     * 将对象添加到网格（Grid）系统的类型化列表中。
     * 通过调用对象的 AddToGrid 方法完成实际插入操作。
     *
     * @tparam SPECIFIC_TYPE 要插入的对象类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器引用
     * @param obj 指向要插入的对象的指针
     *
     * @return SPECIFIC_TYPE* 成功插入返回对象指针，失败返回 nullptr
     *
     * @note 时间复杂度：O(1) 平均情况
     * @note 对象必须实现 AddToGrid 方法（通常是 GridObject 子类）
     * @note 此函数将对象添加到网格引用列表，用于空间分区和对象可见性管理
     *
     * @warning 插入前应确保对象不在其他网格中，避免引用混乱
     *
     * @example
     * @code
     * Creature* creature = new Creature();
     * Insert(gridContainer, creature);
     * @endcode
     */
    template<class SPECIFIC_TYPE, class H, class T>
    inline SPECIFIC_TYPE* Insert(ContainerMapList<TypeList<H, T>>& elements, SPECIFIC_TYPE* obj)
    {
        // 编译期类型匹配检查
        if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
        {
            // 将对象添加到网格引用列表
            obj->AddToGrid(elements._elements._element);
            return obj;
        }

        // 到达 TypeList 末尾仍未找到匹配类型
        if constexpr (std::is_same_v<T, TypeNull>)
            return nullptr;
        else
            // 递归处理下一层
            return Insert(elements._TailElements, obj);
    }

    /**
     * @brief 从 ContainerMapList 中移除对象（已禁用）
     *
     * 此函数被注释掉，可能的原因：
     * - 网格移除操作由其他机制处理
     * - 避免直接操作网格引用导致的不一致
     * - 对象生命周期管理需要更复杂的逻辑
     *
     * 原实现逻辑：
     * - 匹配类型后，调用 GetGridRef().unlink() 断开对象与网格的连接
     * - 递归遍历 TypeList 直到找到匹配类型
     *
     * @tparam SPECIFIC_TYPE 要移除的对象类型
     * @tparam H TypeList 的头部类型
     * @tparam T TypeList 的尾部类型
     *
     * @param elements 容器引用
     * @param obj 指向要移除的对象的指针
     *
     * @return SPECIFIC_TYPE* 成功移除返回对象指针，失败返回 nullptr
     */
    //template<class SPECIFIC_TYPE, class H, class T>
    //SPECIFIC_TYPE* Remove(ContainerMapList<TypeList<H, T>>& elements, SPECIFIC_TYPE* obj)
    //{
    //    if constexpr (std::is_same_v<H, SPECIFIC_TYPE>)
    //    {
    //        // 断开对象与网格的引用关系
    //        obj->GetGridRef().unlink();
    //        return obj;
    //    }

    //    if constexpr (std::is_same_v<T, TypeNull>)
    //        return nullptr;
    //    else
    //        // 递归处理下一层
    //        return Remove(elements._TailElements, obj);
    //}
}
#endif
