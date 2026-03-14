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
 * @file GroupRefManager.h
 * @brief 队伍引用管理器头文件
 *
 * 本文件定义了 GroupRefManager 类，用于管理队伍中所有成员的引用关系。
 * 这是 TrinityCore 引用管理系统的重要组成部分，负责维护队伍到玩家的引用集合。
 *
 * 主要功能：
 * - 存储和管理队伍中所有成员的引用（GroupReference 对象）
 * - 提供遍历队伍成员的能力
 * - 在队伍销毁时自动清理所有成员引用
 *
 * 设计模式：
 * - 采用双向链表存储引用对象
 * - 提供类型安全的访问接口
 * - 配合 GroupReference 实现队伍与玩家的双向关联
 *
 * 与相关类的关系：
 * - Group 类持有 GroupRefManager 实例，用于管理所有成员
 * - GroupReference 对象存储在 GroupRefManager 的链表中
 * - 每个 GroupReference 代表一个队伍成员身份
 */

#ifndef _GROUPREFMANAGER
#define _GROUPREFMANAGER

#include "GroupReference.h"
#include "RefManager.h"

class Group;
class Player;

/**
 * @class GroupRefManager
 * @brief 队伍引用管理器 - 管理队伍中所有成员的引用
 *
 * 继承自 RefManager<Group, Player>，专门用于管理从队伍到玩家的引用集合。
 * 该类提供了类型安全的接口来访问 GroupReference 对象，而不是基类的 Reference 对象。
 *
 * 继承关系：
 * - LinkedListHead -> RefManager<Group, Player> -> GroupRefManager
 *
 * 工作原理：
 * - 源对象（TO）：Group 类型，表示队伍
 * - 目标对象（FROM）：Player 类型，表示玩家
 * - 引用方向：队伍持有多个到玩家的引用
 * - 存储：所有 GroupReference 以链表形式存储
 *
 * 使用场景：
 * - Group 类中持有此管理器实例
 * - 当玩家加入队伍时，创建 GroupReference 并添加到此管理器
 * - 当玩家离开队伍时，从管理器中移除对应的引用
 * - 遍历队伍成员时，通过 getFirst() 和 next() 遍历链表
 *
 * 典型操作流程：
 * 1. 玩家加入队伍：创建 GroupReference，调用 link() 建立关联
 * 2. 遍历成员：调用 getFirst() 获取首个引用，通过 next() 遍历
 * 3. 玩家离开队伍：调用 unlink() 解除关联，引用自动从链表移除
 * 4. 队伍销毁：析构函数自动调用 clearReferences() 清理所有引用
 *
 * 性能特点：
 * - 添加/删除引用：O(1) 时间复杂度
 * - 遍历成员：O(n) 时间复杂度，n 为成员数量
 * - 内存开销：每个引用对象占用少量内存（指针 + 子组信息）
 *
 * 线程安全：
 * - 非线程安全，调用者需要在队伍锁保护下操作
 * - 通常在 Group 类的方法中调用，Group 负责同步控制
 *
 * @example
 * // 遍历队伍成员示例
 * Group* group = ...;
 * GroupReference* ref = group->GetFirstMember();
 * while (ref) {
 *     Player* member = ref->GetTarget();
 *     if (member) {
 *         // 处理队伍成员
 *     }
 *     ref = ref->next();
 * }
 */
class GroupRefManager : public RefManager<Group, Player>
{
    public:
        /**
         * @brief 获取第一个成员引用（可修改版本）
         * @return 指向第一个 GroupReference 的指针，如果队伍为空则返回 nullptr
         *
         * 重写基类的 getFirst() 方法，返回类型安全的 GroupReference 指针。
         * 这是遍历队伍成员的起点。
         *
         * 使用方法：
         * 1. 调用此方法获取首个成员引用
         * 2. 通过 GroupReference::next() 继续遍历后续成员
         * 3. 当 next() 返回 nullptr 时，遍历结束
         *
         * @调用时机
         * - 需要遍历队伍所有成员时
         * - 需要访问第一个成员时
         * - 广播消息给队伍成员时
         *
         * @性能说明 O(1) 时间复杂度，直接访问链表头
         *
         * @note 返回的引用可能指向已离线的玩家对象，调用者需要检查
         *       玩家的在线状态（Player::IsInWorld()）
         *
         * @example
         * // 遍历所有队伍成员
         * if (GroupReference* ref = groupRefManager.getFirst()) {
         *     do {
         *         Player* member = ref->GetTarget();
         *         // 处理成员...
         *     } while ((ref = ref->next()));
         * }
         */
        GroupReference* getFirst() { return ((GroupReference*)RefManager<Group, Player>::getFirst()); }

        /**
         * @brief 获取第一个成员引用（只读版本）
         * @return 指向第一个 GroupReference 的常量指针，如果队伍为空则返回 nullptr
         *
         * const 重载版本，用于只读访问队伍成员列表。
         * 适用于不需要修改引用的场景，如统计成员数量、查询成员信息等。
         *
         * @调用时机
         * - 只读遍历队伍成员时
         * - 在 const 成员函数中访问队伍成员时
         * - 查询队伍状态时
         *
         * @性能说明 O(1) 时间复杂度
         *
         * @see getFirst() 可修改版本
         */
        GroupReference const* getFirst() const { return ((GroupReference const*)RefManager<Group, Player>::getFirst()); }
};
#endif
