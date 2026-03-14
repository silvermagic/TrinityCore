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
 * @file GroupReference.h
 * @brief 队伍引用类头文件
 *
 * 本文件定义了 GroupReference 类，用于管理玩家与队伍之间的双向引用关系。
 * 这是 TrinityCore 引用计数系统的一部分，确保玩家和队伍对象之间的关联能够正确建立和销毁。
 *
 * 主要功能：
 * - 维护玩家与队伍的关联关系
 * - 管理玩家在队伍中的子组信息
 * - 提供引用链接的自动管理机制
 */

#ifndef _GROUPREFERENCE_H
#define _GROUPREFERENCE_H

#include "LinkedReference/Reference.h"

class Group;
class Player;

/**
 * @brief 队伍引用类 - 管理玩家与队伍之间的引用关系
 *
 * 继承自 Reference<Group, Player>，用于建立从队伍到玩家的单向引用关系。
 * 每个 GroupReference 对象代表一个玩家在特定队伍中的成员身份。
 *
 * 工作原理：
 * - 源对象（Source）：Group 类型，表示队伍
 * - 目标对象（Target）：Player 类型，表示玩家
 * - 引用方向：Group -> Player
 * - 配合 GroupReference 被添加到玩家的引用管理器中，实现双向关联
 *
 * 使用场景：
 * - 当玩家加入队伍时，创建 GroupReference 并链接
 * - 当玩家离开队伍时，解除链接
 * - 用于遍历队伍成员
 *
 * 性能注意事项：
 * - 引用操作的时间复杂度为 O(1)
 * - 解链操作会在对象销毁时自动调用
 * - 子组信息存储在引用对象中，避免重复查找
 */
class TC_GAME_API GroupReference : public Reference<Group, Player>
{
    protected:
        /**
         * @brief 子组编号
         *
         * 存储玩家在团队中的子组编号（0-7）。
         * 对于普通小队，此值通常为 0。
         * 在 40 人团队中，分为 8 个子组，每个子组最多 5 人。
         */
        uint8 iSubGroup;

        /**
         * @brief 构建目标对象链接
         *
         * 当调用 link() 方法时自动调用此函数。
         * 将此引用注册到队伍对象中，建立队伍到玩家的引用。
         *
         * @note 此方法由基类的 link() 方法自动调用，不应直接调用
         * @see Group::LinkMember()
         */
        void targetObjectBuildLink() override;

        /**
         * @brief 销毁目标对象链接
         *
         * 当调用 unlink() 方法时自动调用此函数。
         * 用于在解除引用时执行清理操作。
         *
         * @note 当前实现中未使用，代码被注释
         * @see Group::DelinkMember()
         */
        void targetObjectDestroyLink() override;

        /**
         * @brief 销毁源对象链接
         *
         * 当调用 invalidate() 方法时自动调用此函数。
         * 用于在引用失效时执行清理操作。
         *
         * @note 当前实现中未使用，代码被注释
         */
        void sourceObjectDestroyLink() override;

    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化引用对象，子组编号默认为 0。
         */
        GroupReference() : Reference<Group, Player>(), iSubGroup(0) { }

        /**
         * @brief 析构函数
         *
         * 自动解除引用链接，确保资源正确释放。
         */
        ~GroupReference() { unlink(); }

        /**
         * @brief 获取下一个引用（可修改版本）
         * @return 指向下一个 GroupReference 的指针，如果没有下一个则返回 nullptr
         *
         * 用于遍历队伍成员链表。
         * 配合 Group::GetFirstMember() 使用可以遍历所有队伍成员。
         */
        GroupReference* next() { return (GroupReference*)Reference<Group, Player>::next(); }

        /**
         * @brief 获取下一个引用（只读版本）
         * @return 指向下一个 GroupReference 的常量指针，如果没有下一个则返回 nullptr
         *
         * 用于只读方式遍历队伍成员链表。
         */
        GroupReference const* next() const { return (GroupReference const*)Reference<Group, Player>::next(); }

        /**
         * @brief 获取子组编号
         * @return 子组编号（0-7）
         *
         * 返回玩家在团队中所属的子组编号。
         */
        uint8 getSubGroup() const { return iSubGroup; }

        /**
         * @brief 设置子组编号
         * @param pSubGroup 新的子组编号（0-7）
         *
         * 设置玩家在团队中的子组编号。
         * 通常在玩家加入队伍或更换子组时调用。
         */
        void setSubGroup(uint8 pSubGroup) { iSubGroup = pSubGroup; }
};
#endif
