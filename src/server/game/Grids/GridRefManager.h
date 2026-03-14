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
 * @file GridRefManager.h
 * @brief 网格引用管理器定义
 *
 * 本文件定义了GridRefManager类，用于管理网格中特定类型的所有游戏对象引用。
 * 这是网格系统中对象存储和遍历的核心容器。
 *
 * 主要功能:
 * - 管理网格中特定类型对象的所有引用
 * - 提供对象遍历的迭代器接口
 * - 维护对象计数和链表结构
 *
 * 使用场景:
 * - 每个单元格(Cell)包含多个GridRefManager实例，分别管理不同类型的对象
 * - 通过GridRefManager遍历网格中的玩家、生物、游戏对象等
 *
 * 类型定义:
 * - PlayerMapType: 管理玩家对象
 * - CreatureMapType: 管理生物对象
 * - GameObjectMapType: 管理游戏对象
 * - CorpseMapType: 管理尸体对象
 * - DynamicObjectMapType: 管理动态对象
 */

#ifndef _GRIDREFMANAGER
#define _GRIDREFMANAGER

#include "RefManager.h"

// 前置声明
template<class OBJECT>
class GridReference;

/**
 * @class GridRefManager
 * @brief 网格引用管理器，管理网格中特定类型的所有游戏对象
 *
 * @tparam OBJECT 管理的游戏对象类型(如Player, Creature, GameObject等)
 *
 * GridRefManager是网格系统中存储和访问游戏对象的核心容器。每个单元格(Cell)
 * 包含多个不同类型的GridRefManager实例，每个实例管理一种类型的游戏对象。
 *
 * 继承关系:
 * - 继承自RefManager基类，提供基本的引用管理功能
 * - 使用双向链表存储所有GridReference
 *
 * 内部结构:
 * - 维护一个GridReference链表
 * - 每个GridReference指向一个具体的游戏对象
 * - 提供迭代器支持STL风格的遍历
 *
 * 性能特点:
 * - 插入/删除操作: O(1)
 * - 遍历操作: O(n)
 * - 内存开销: 每个对象一个引用节点
 *
 * 使用示例:
 * @code
 * // 遍历网格中的所有玩家
 * for (PlayerMapType::iterator iter = playerMap.begin(); iter != playerMap.end(); ++iter)
 * {
 *     Player* player = iter->GetSource();
 *     // 处理玩家...
 * }
 * @endcode
 */
template<class OBJECT>
class GridRefManager : public RefManager<GridRefManager<OBJECT>, OBJECT>
{
    public:
        /**
         * @brief 迭代器类型定义
         *
         * 提供STL风格的迭代器，用于遍历网格中的所有同类型对象。
         */
        typedef LinkedListHead::Iterator< GridReference<OBJECT> > iterator;

        /**
         * @brief 获取链表中的第一个引用
         *
         * @return GridReference<OBJECT>* 指向第一个引用的指针，如果链表为空则返回nullptr
         *
         * 用于开始遍历网格中的对象。
         *
         * 调用时机: 在需要遍历网格中所有对象时使用
         * 性能注意: O(1)操作
         */
        GridReference<OBJECT>* getFirst() { return (GridReference<OBJECT>*)RefManager<GridRefManager<OBJECT>, OBJECT>::getFirst(); }

        /**
         * @brief 获取链表中的最后一个引用
         *
         * @return GridReference<OBJECT>* 指向最后一个引用的指针，如果链表为空则返回nullptr
         *
         * 用于反向遍历或获取链表尾部。
         */
        GridReference<OBJECT>* getLast() { return (GridReference<OBJECT>*)RefManager<GridRefManager<OBJECT>, OBJECT>::getLast(); }

        /**
         * @brief 获取开始迭代器
         *
         * @return iterator 指向链表第一个元素的迭代器
         *
         * 用于STL风格的遍历开始。
         */
        iterator begin() { return iterator(getFirst()); }

        /**
         * @brief 获取结束迭代器
         *
         * @return iterator 表示链表结束的迭代器(nullptr)
         *
         * 用于STL风格的遍历结束判断。
         */
        iterator end() { return iterator(nullptr); }
};
#endif
