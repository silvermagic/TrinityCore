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
// 模块：MapReference 实现
// ============================================================================
// 职责：实现 Player 到 Map 引用关系的底层管理
// 核心功能：维护 Map 的引用链表和引用计数
// ============================================================================

#include "MapReference.h"
#include "Map.h"

// ============================================================================
// 构建目标对象链接
// ============================================================================
// @brief 在引用建立时，将此 MapReference 插入到 Map 的引用管理器链表头部
// @param 无
// @return 无
// 调用时机：由 Reference::link() 方法内部调用
// 操作步骤：
//   1. 将此引用节点插入到 Map 的 m_mapRefManager 链表头部
//   2. 增加引用管理器的节点计数
// 性能：O(1) 头部插入，高效
// ============================================================================
void MapReference::targetObjectBuildLink()
{
    // getTarget() 返回 Player 对象，但这里访问的是 Map 的成员
    // 因为这个引用是 Player 持有的，但链接到 Map 的引用管理器
    getTarget()->m_mapRefManager.insertFirst(this);  // 插入到链表头部
    getTarget()->m_mapRefManager.incSize();          // 增加节点计数
}

// ============================================================================
// 销毁目标对象链接
// ============================================================================
// @brief 在引用解除时，减少 Map 引用管理器的节点计数
// @param 无
// @return 无
// 调用时机：由 Reference::unlink() 方法内部调用
// 注意：
//   - 需要检查引用有效性，避免重复操作
//   - 链表节点的移除由基类的析构函数处理，这里只更新计数
// ============================================================================
void MapReference::targetObjectDestroyLink()
{
    // 检查引用是否仍然有效，防止重复操作
    if (isValid())
        getTarget()->m_mapRefManager.decSize();  // 减少节点计数
}

// ============================================================================
// 销毁源对象链接
// ============================================================================
// @brief 当 Player 对象销毁时，减少 Map 引用管理器的节点计数
// @param 无
// @return 无
// 调用时机：由 Reference::invalidate() 方法内部调用
// 场景：Player 对象析构时，需要从 Map 的引用链表中移除
// 注意：与 targetObjectDestroyLink 不同，这里不检查有效性
//       因为源对象销毁时必须清理引用
// ============================================================================
void MapReference::sourceObjectDestroyLink()
{
    // 直接减少节点计数，链表清理由基类处理
    getTarget()->m_mapRefManager.decSize();
}
