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
// 模块：MapReference - 地图引用管理
// ============================================================================
// 职责：
//   管理 Player 到 Map 的引用关系，用于追踪哪些玩家在哪个地图上
//   实现双向引用链表，支持快速遍历地图上的所有玩家
//
// 设计模式：
//   - 引用计数模式：自动管理引用生命周期
//   - 双向链表：高效的玩家遍历
//
// 使用场景：
//   - 当玩家进入地图时，创建 MapReference 并链接到 Map 的引用管理器
//   - 当玩家离开地图时，自动销毁引用
//   - 地图更新时遍历所有玩家进行广播
// ============================================================================

#ifndef _MAPREFERENCE_H
#define _MAPREFERENCE_H

#include "Reference.h"

class Map;
class Player;

// ============================================================================
// MapReference - 地图引用类
// ============================================================================
// 职责：表示一个玩家对地图的引用关系
// 继承：Reference<Map, Player> - 源是 Map，目标是 Player
// 生命周期：
//   - 创建：玩家进入地图时
//   - 销毁：玩家离开地图或地图销毁时
// ============================================================================
class MapReference : public Reference<Map, Player>
{
    protected:
        // ====================================================================
        // 构建目标对象链接
        // ====================================================================
        // @brief 当引用建立时调用，将此引用插入到 Map 的引用管理器中
        // @param 无
        // @return 无
        // 调用时机：Reference::link() 方法内部调用
        // 性能：O(1) 操作，直接插入链表头部
        // ====================================================================
        void targetObjectBuildLink() override;

        // ====================================================================
        // 销毁目标对象链接
        // ====================================================================
        // @brief 当引用被移除时调用，从 Map 的引用管理器中移除此引用
        // @param 无
        // @return 无
        // 调用时机：Reference::unlink() 方法内部调用
        // 注意：需要检查引用是否有效，防止重复移除
        // ====================================================================
        void targetObjectDestroyLink() override;

        // ====================================================================
        // 销毁源对象链接
        // ====================================================================
        // @brief 当 Player 对象销毁时调用，清理引用计数
        // @param 无
        // @return 无
        // 调用时机：Player 对象析构时
        // ====================================================================
        void sourceObjectDestroyLink() override;

    public:
        // ====================================================================
        // 构造函数
        // ====================================================================
        // @brief 初始化一个空的地图引用
        // ====================================================================
        MapReference() : Reference<Map, Player>() { }

        // ====================================================================
        // 析构函数
        // ====================================================================
        // @brief 析构时自动解除链接，确保引用关系正确清理
        // ====================================================================
        ~MapReference() { unlink(); }

        // ====================================================================
        // 获取下一个引用
        // ====================================================================
        // @brief 获取链表中的下一个 MapReference 节点
        // @return 下一个 MapReference 指针，如果是末尾返回 nullptr
        // 用途：遍历地图上的所有玩家
        // ====================================================================
        MapReference* next() { return (MapReference*)Reference<Map, Player>::next(); }
        MapReference const* next() const { return (MapReference const*)Reference<Map, Player>::next(); }

        // ====================================================================
        // 获取前一个引用（无检查版本）
        // ====================================================================
        // @brief 获取链表中的前一个 MapReference 节点（不进行空指针检查）
        // @return 前一个 MapReference 指针
        // 警告：不检查空指针，调用者需确保链表状态正确
        // 用途：反向遍历链表
        // ====================================================================
        MapReference* nockeck_prev() { return (MapReference*)Reference<Map, Player>::nocheck_prev(); }
        MapReference const* nocheck_prev() const { return (MapReference const*)Reference<Map, Player>::nocheck_prev(); }
};
#endif
