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
 * @file GridReference.h
 * @brief 网格引用类定义
 *
 * 本文件定义了GridReference类，用于建立游戏对象与网格管理器之间的双向引用关系。
 * 这是实现游戏对象在网格系统中管理的基础设施。
 *
 * 主要功能:
 * - 维护对象与网格管理器之间的引用链接
 * - 在链接建立/销毁时自动更新管理器的计数
 * - 支持遍历同一网格中的所有对象
 *
 * 设计模式:
 * - 使用双向链表结构管理引用
 * - 继承自Reference基类，实现引用计数管理
 */

#ifndef _GRIDREFERENCE_H
#define _GRIDREFERENCE_H

#include "LinkedReference/Reference.h"

// 前置声明
template<class OBJECT>
class GridRefManager;

/**
 * @class GridReference
 * @brief 网格引用类，管理游戏对象与网格管理器之间的引用关系
 *
 * @tparam OBJECT 被引用的游戏对象类型(如Player, Creature, GameObject等)
 *
 * 每个游戏对象都有一个GridReference成员，用于将其链接到所在的网格管理器。
 * 网格管理器通过GridReference链表管理该网格中所有的同类型对象。
 *
 * 使用场景:
 * - 游戏对象进入网格时，建立引用链接
 * - 游戏对象离开网格时，断开引用链接
 * - 遍历网格中所有同类对象时使用next()方法
 */
template<class OBJECT>
class GridReference : public Reference<GridRefManager<OBJECT>, OBJECT>
{
    protected:
        /**
         * @brief 建立目标对象链接
         *
         * 当调用link()方法建立引用关系时被调用。
         * 将当前引用插入到网格管理器的链表头部，并增加管理器的对象计数。
         *
         * 调用时机: 在游戏对象被添加到网格时自动调用
         * 性能注意: O(1)操作，直接插入链表头部
         */
        void targetObjectBuildLink() override
        {
            // 将当前引用插入管理器链表头部
            this->getTarget()->insertFirst(this);
            // 增加管理器中的对象计数
            this->getTarget()->incSize();
        }

        /**
         * @brief 销毁目标对象链接
         *
         * 当调用unlink()方法断开引用关系时被调用。
         * 减少网格管理器的对象计数。
         *
         * 调用时机: 在游戏对象从网格移除时自动调用
         * 注意事项: 仅在引用有效时才减少计数
         */
        void targetObjectDestroyLink() override
        {
            // 仅在引用有效时才减少计数
            if (this->isValid()) this->getTarget()->decSize();
        }

        /**
         * @brief 销毁源对象链接
         *
         * 当调用invalidate()方法使引用失效时被调用。
         * 减少网格管理器的对象计数。
         *
         * 调用时机: 在游戏对象被销毁或移动到其他网格时调用
         */
        void sourceObjectDestroyLink() override
        {
            // 减少管理器中的对象计数
            this->getTarget()->decSize();
        }

    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个未链接的网格引用对象。
         */
        GridReference() : Reference<GridRefManager<OBJECT>, OBJECT>() { }

        /**
         * @brief 析构函数
         *
         * 自动断开引用链接，确保资源正确释放。
         */
        ~GridReference() { this->unlink(); }

        /**
         * @brief 获取链表中的下一个引用
         *
         * @return GridReference* 指向下一个引用的指针，如果是链表末尾则返回nullptr
         *
         * 用于遍历网格中所有同类型对象。
         *
         * 使用示例:
         * @code
         * for (GridReference<Player>* ref = manager.getFirst(); ref != nullptr; ref = ref->next())
         * {
         *     Player* player = ref->GetSource();
         *     // 处理player...
         * }
         * @endcode
         */
        GridReference* next() { return (GridReference*)Reference<GridRefManager<OBJECT>, OBJECT>::next(); }
};
#endif
