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
// 模块：MapRefManager - 地图引用管理器
// ============================================================================
// 职责：
//   管理一个地图上所有玩家的引用链表
//   提供高效的玩家遍历和计数功能
//
// 设计模式：
//   - 引用管理器模式：集中管理所有引用
//   - 迭代器模式：提供 STL 风格的迭代器接口
//
// 使用场景：
//   - 每个地图对象持有一个 MapRefManager
//   - 地图需要广播消息时，遍历所有玩家
//   - 统计地图上的玩家数量
//
// 数据结构：
//   基于双向链表，支持 O(1) 插入和删除
// ============================================================================

#ifndef _MAPREFMANAGER
#define _MAPREFMANAGER

#include "RefManager.h"

class MapReference;

// ============================================================================
// MapRefManager - 地图引用管理器类
// ============================================================================
// 职责：管理 Map 上所有 Player 的引用
// 继承：RefManager<Map, Player> - 管理从 Map 到 Player 的引用
// 线程安全：非线程安全，需在外部加锁
// ============================================================================
class MapRefManager : public RefManager<Map, Player>
{
    public:
        // ====================================================================
        // 迭代器类型定义
        // ====================================================================
        // 提供 STL 风格的迭代器，支持范围 for 循环
        // ====================================================================
        typedef LinkedListHead::Iterator<MapReference> iterator;
        typedef LinkedListHead::Iterator<MapReference const> const_iterator;

        // ====================================================================
        // 获取第一个引用节点
        // ====================================================================
        // @brief 返回链表的第一个 MapReference 节点
        // @return 第一个 MapReference 指针，链表为空时返回 nullptr
        // 用途：手动遍历链表或获取链表起点
        // 性能：O(1) 操作
        // ====================================================================
        MapReference* getFirst() { return (MapReference*)RefManager<Map, Player>::getFirst(); }
        MapReference const* getFirst() const { return (MapReference const*)RefManager<Map, Player>::getFirst(); }

        // ====================================================================
        // 迭代器接口 - begin
        // ====================================================================
        // @brief 返回指向链表起始位置的迭代器
        // @return 迭代器，指向第一个元素
        // 用途：支持 STL 风格的范围循环
        // 示例：
        //   for (MapReference* ref : mapRefManager) { ... }
        // ====================================================================
        iterator begin() { return iterator(getFirst()); }
        iterator end() { return iterator(nullptr); }

        // ====================================================================
        // 迭代器接口 - begin (const 版本)
        // ====================================================================
        // @brief 返回指向链表起始位置的常量迭代器
        // @return 常量迭代器，指向第一个元素
        // 用途：在 const 上下文中遍历链表
        // ====================================================================
        const_iterator begin() const { return const_iterator(getFirst()); }
        const_iterator end() const { return const_iterator(nullptr); }
};
#endif
