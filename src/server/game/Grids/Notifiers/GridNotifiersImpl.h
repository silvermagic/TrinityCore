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
 * @file GridNotifiersImpl.h
 * @brief 网格通知器模板实现文件
 *
 * 本文件实现了网格系统中使用的各种通知器和搜索器的模板函数。
 * 这些通知器用于在网格中遍历、搜索和处理游戏对象,包括:
 * - 可见性更新通知器
 * - 对象搜索器(单个对象、最后一个对象、对象列表)
 * - 本地化数据包发送器
 *
 * 采用访问者模式,通过 Visit 方法访问不同类型的对象集合。
 */

#ifndef TRINITY_GRIDNOTIFIERSIMPL_H
#define TRINITY_GRIDNOTIFIERSIMPL_H

#include "GridNotifiers.h"
#include "Corpse.h"
#include "CreatureAI.h"
#include "Player.h"
#include "SpellAuras.h"
#include "UpdateData.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief 访问网格对象管理器并更新玩家可见性
 *
 * 遍历网格中的所有对象,更新玩家对这些对象的可见性状态。
 * 对于每个对象,从待确认的 GUID 集合中移除,并调用玩家的可见性更新方法。
 *
 * @tparam T 网格对象类型(Player, Creature, GameObject 等)
 * @param m 网格对象引用管理器
 *
 * @note 该方法会修改 vis_guids、i_data 和 i_visibleNow 成员
 * @note 性能: 线性遍历,复杂度 O(n),n 为网格中的对象数量
 */
template<class T>
inline void Trinity::VisibleNotifier::Visit(GridRefManager<T> &m)
{
    for (typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        // 从待确认集合中移除该对象(表示该对象在当前网格中存在)
        vis_guids.erase(iter->GetSource()->GetGUID());
        // 更新玩家对该对象的可见性状态
        i_player.UpdateVisibilityOf(iter->GetSource(), i_data, i_visibleNow);
    }
}

// ============================================================================
// 搜索器、列表搜索器和工作器
// ============================================================================

// ----------------------------------------------------------------------------
// 世界对象搜索器和工作者
// ----------------------------------------------------------------------------

/**
 * @brief 访问游戏对象映射并搜索符合条件的第一个对象
 *
 * 遍历网格中的所有游戏对象,查找第一个满足检查条件的对象。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型,用于判断对象是否符合条件
 * @param m 游戏对象映射容器
 *
 * @note 调用时机: 在网格遍历过程中被调用
 * @note 性能: 找到第一个匹配对象后立即返回,平均复杂度取决于匹配对象位置
 */
template<class Check>
void Trinity::WorldObjectSearcher<Check>::Visit(GameObjectMapType &m)
{
    // 检查是否需要搜索此类型的对象
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_GAMEOBJECT))
        return;

    // 如果已经找到对象,直接返回(避免重复搜索)
    if (i_object)
        return;

    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        // 相位掩码检查,不同相位的对象不可见
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        // 使用检查器判断对象是否符合条件
        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return; // 找到第一个符合条件的对象,立即返回
        }
    }
}

/**
 * @brief 访问玩家映射并搜索符合条件的第一个玩家对象
 *
 * 遍历网格中的所有玩家,查找第一个满足检查条件的玩家。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @see Visit(GameObjectMapType &m)
 */
template<class Check>
void Trinity::WorldObjectSearcher<Check>::Visit(PlayerMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_PLAYER))
        return;

    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问生物映射并搜索符合条件的第一个生物对象
 *
 * 遍历网格中的所有生物,查找第一个满足检查条件的生物。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @see Visit(GameObjectMapType &m)
 */
template<class Check>
void Trinity::WorldObjectSearcher<Check>::Visit(CreatureMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CREATURE))
        return;

    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问尸体映射并搜索符合条件的第一个尸体对象
 *
 * 遍历网格中的所有尸体,查找第一个满足检查条件的尸体。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 尸体映射容器
 *
 * @see Visit(GameObjectMapType &m)
 */
template<class Check>
void Trinity::WorldObjectSearcher<Check>::Visit(CorpseMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CORPSE))
        return;

    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (CorpseMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问动态对象映射并搜索符合条件的第一个动态对象
 *
 * 遍历网格中的所有动态对象,查找第一个满足检查条件的动态对象。
 * 找到后立即返回,不再继续搜索。
 * 动态对象通常由法术效果创建,如治疗之雨图腾等。
 *
 * @tparam Check 检查器类型
 * @param m 动态对象映射容器
 *
 * @see Visit(GameObjectMapType &m)
 */
template<class Check>
void Trinity::WorldObjectSearcher<Check>::Visit(DynamicObjectMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_DYNAMICOBJECT))
        return;

    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (DynamicObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问游戏对象映射并搜索最后一个符合条件的对象
 *
 * 与 WorldObjectSearcher 不同,此搜索器会遍历所有对象,
 * 最终返回最后一个满足检查条件的对象。即使找到匹配对象也会继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 游戏对象映射容器
 *
 * @note 性能: 总是遍历所有对象,复杂度 O(n)
 */
template<class Check>
void Trinity::WorldObjectLastSearcher<Check>::Visit(GameObjectMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_GAMEOBJECT))
        return;

    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        // 持续更新,保留最后一个符合条件的对象
        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问玩家映射并搜索最后一个符合条件的玩家
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @see Visit(GameObjectMapType &m) 了解 WorldObjectLastSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectLastSearcher<Check>::Visit(PlayerMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_PLAYER))
        return;

    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问生物映射并搜索最后一个符合条件的生物
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @see Visit(GameObjectMapType &m) 了解 WorldObjectLastSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectLastSearcher<Check>::Visit(CreatureMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CREATURE))
        return;

    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问尸体映射并搜索最后一个符合条件的尸体
 *
 * @tparam Check 检查器类型
 * @param m 尸体映射容器
 *
 * @see Visit(GameObjectMapType &m) 了解 WorldObjectLastSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectLastSearcher<Check>::Visit(CorpseMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CORPSE))
        return;

    for (CorpseMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问动态对象映射并搜索最后一个符合条件的动态对象
 *
 * @tparam Check 检查器类型
 * @param m 动态对象映射容器
 *
 * @see Visit(GameObjectMapType &m) 了解 WorldObjectLastSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectLastSearcher<Check>::Visit(DynamicObjectMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_DYNAMICOBJECT))
        return;

    for (DynamicObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问玩家映射并收集所有符合条件的玩家
 *
 * 遍历网格中的所有玩家,将符合条件的玩家添加到结果列表中。
 * 与 Searcher 不同,ListSearcher 会收集所有匹配对象,而非仅返回第一个或最后一个。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @note 性能: 总是遍历所有对象并可能分配内存,复杂度 O(n)
 * @note 注意: 此函数未进行相位检查,由检查器负责相位过滤
 */
template<class Check>
void Trinity::WorldObjectListSearcher<Check>::Visit(PlayerMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_PLAYER))
        return;

    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (i_check(itr->GetSource()))
            Insert(itr->GetSource());
}

/**
 * @brief 访问生物映射并收集所有符合条件的生物
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @see Visit(PlayerMapType &m) 了解 WorldObjectListSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectListSearcher<Check>::Visit(CreatureMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CREATURE))
        return;

    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (i_check(itr->GetSource()))
            Insert(itr->GetSource());
}

/**
 * @brief 访问尸体映射并收集所有符合条件的尸体
 *
 * @tparam Check 检查器类型
 * @param m 尸体映射容器
 *
 * @see Visit(PlayerMapType &m) 了解 WorldObjectListSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectListSearcher<Check>::Visit(CorpseMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CORPSE))
        return;

    for (CorpseMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (i_check(itr->GetSource()))
            Insert(itr->GetSource());
}

/**
 * @brief 访问游戏对象映射并收集所有符合条件的游戏对象
 *
 * @tparam Check 检查器类型
 * @param m 游戏对象映射容器
 *
 * @see Visit(PlayerMapType &m) 了解 WorldObjectListSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectListSearcher<Check>::Visit(GameObjectMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_GAMEOBJECT))
        return;

    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (i_check(itr->GetSource()))
            Insert(itr->GetSource());
}

/**
 * @brief 访问动态对象映射并收集所有符合条件的动态对象
 *
 * @tparam Check 检查器类型
 * @param m 动态对象映射容器
 *
 * @see Visit(PlayerMapType &m) 了解 WorldObjectListSearcher 的工作原理
 */
template<class Check>
void Trinity::WorldObjectListSearcher<Check>::Visit(DynamicObjectMapType &m)
{
    if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_DYNAMICOBJECT))
        return;

    for (DynamicObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (i_check(itr->GetSource()))
            Insert(itr->GetSource());
}

// ----------------------------------------------------------------------------
// 游戏对象搜索器
// ----------------------------------------------------------------------------

/**
 * @brief 访问游戏对象映射并搜索符合条件的第一个游戏对象
 *
 * 专门用于搜索游戏对象的搜索器,查找第一个满足条件的游戏对象。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 游戏对象映射容器
 *
 * @note 与 WorldObjectSearcher<GameObject> 不同,此类专门优化用于游戏对象
 */
template<class Check>
void Trinity::GameObjectSearcher<Check>::Visit(GameObjectMapType &m)
{
    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问游戏对象映射并搜索最后一个符合条件的游戏对象
 *
 * 遍历所有游戏对象,返回最后一个满足条件的游戏对象。
 *
 * @tparam Check 检查器类型
 * @param m 游戏对象映射容器
 *
 * @note 性能: 总是遍历所有对象,复杂度 O(n)
 */
template<class Check>
void Trinity::GameObjectLastSearcher<Check>::Visit(GameObjectMapType &m)
{
    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问游戏对象映射并收集所有符合条件的游戏对象
 *
 * 遍历所有游戏对象,将符合条件的对象添加到结果列表中。
 * 在添加前会检查相位掩码,确保对象在正确的相位中。
 *
 * @tparam Check 检查器类型
 * @param m 游戏对象映射容器
 *
 * @note 与 WorldObjectListSearcher 不同,此函数包含相位检查
 */
template<class Check>
void Trinity::GameObjectListSearcher<Check>::Visit(GameObjectMapType &m)
{
    for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (itr->GetSource()->InSamePhase(i_phaseMask))
            if (i_check(itr->GetSource()))
                Insert(itr->GetSource());
}

// ----------------------------------------------------------------------------
// 单位搜索器 (Unit = Player + Creature)
// ----------------------------------------------------------------------------

/**
 * @brief 访问生物映射并搜索符合条件的第一个单位(生物)
 *
 * UnitSearcher 用于搜索 Unit 类型对象(包括 Player 和 Creature)。
 * 此函数处理生物类型,查找第一个满足条件的生物。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @note 单位搜索器常用于战斗系统、仇恨系统等需要同时处理玩家和生物的场景
 */
template<class Check>
void Trinity::UnitSearcher<Check>::Visit(CreatureMapType &m)
{
    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问玩家映射并搜索符合条件的第一个单位(玩家)
 *
 * UnitSearcher 的玩家版本,查找第一个满足条件的玩家。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @see Visit(CreatureMapType &m)
 */
template<class Check>
void Trinity::UnitSearcher<Check>::Visit(PlayerMapType &m)
{
    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问生物映射并搜索最后一个符合条件的单位(生物)
 *
 * 遍历所有生物,返回最后一个满足条件的生物。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @note 性能: 总是遍历所有对象,复杂度 O(n)
 */
template<class Check>
void Trinity::UnitLastSearcher<Check>::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问玩家映射并搜索最后一个符合条件的单位(玩家)
 *
 * 遍历所有玩家,返回最后一个满足条件的玩家。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @see Visit(CreatureMapType &m)
 */
template<class Check>
void Trinity::UnitLastSearcher<Check>::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问玩家映射并收集所有符合条件的单位(玩家)
 *
 * 遍历所有玩家,将符合条件的玩家添加到结果列表中。
 * 在添加前会检查相位掩码。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @note 性能: 总是遍历所有对象并可能分配内存,复杂度 O(n)
 */
template<class Check>
void Trinity::UnitListSearcher<Check>::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (itr->GetSource()->InSamePhase(i_phaseMask))
            if (i_check(itr->GetSource()))
                Insert(itr->GetSource());
}

/**
 * @brief 访问生物映射并收集所有符合条件的单位(生物)
 *
 * 遍历所有生物,将符合条件的生物添加到结果列表中。
 * 在添加前会检查相位掩码。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @see Visit(PlayerMapType &m)
 */
template<class Check>
void Trinity::UnitListSearcher<Check>::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (itr->GetSource()->InSamePhase(i_phaseMask))
            if (i_check(itr->GetSource()))
                Insert(itr->GetSource());
}

// ----------------------------------------------------------------------------
// 生物搜索器
// ----------------------------------------------------------------------------

/**
 * @brief 访问生物映射并搜索符合条件的第一个生物
 *
 * 专门用于搜索生物的搜索器,查找第一个满足条件的生物。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @note 常用于查找特定类型的 NPC、怪物等
 */
template<class Check>
void Trinity::CreatureSearcher<Check>::Visit(CreatureMapType &m)
{
    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问生物映射并搜索最后一个符合条件的生物
 *
 * 遍历所有生物,返回最后一个满足条件的生物。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @note 性能: 总是遍历所有对象,复杂度 O(n)
 */
template<class Check>
void Trinity::CreatureLastSearcher<Check>::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

/**
 * @brief 访问生物映射并收集所有符合条件的生物
 *
 * 遍历所有生物,将符合条件的生物添加到结果列表中。
 * 在添加前会检查相位掩码。
 *
 * @tparam Check 检查器类型
 * @param m 生物映射容器
 *
 * @note 性能: 总是遍历所有对象并可能分配内存,复杂度 O(n)
 */
template<class Check>
void Trinity::CreatureListSearcher<Check>::Visit(CreatureMapType &m)
{
    for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (itr->GetSource()->InSamePhase(i_phaseMask))
            if (i_check(itr->GetSource()))
                Insert(itr->GetSource());
}

/**
 * @brief 访问玩家映射并收集所有符合条件的玩家
 *
 * 专门用于收集玩家的列表搜索器。
 * 遍历所有玩家,将符合条件的玩家添加到结果列表中。
 * 在添加前会检查相位掩码。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @note 常用于范围技能效果、群体事件等需要找到周围所有玩家的场景
 */
template<class Check>
void Trinity::PlayerListSearcher<Check>::Visit(PlayerMapType &m)
{
    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
        if (itr->GetSource()->InSamePhase(i_phaseMask))
            if (i_check(itr->GetSource()))
                Insert(itr->GetSource());
}

/**
 * @brief 访问玩家映射并搜索符合条件的第一个玩家
 *
 * 专门用于搜索玩家的搜索器,查找第一个满足条件的玩家。
 * 找到后立即返回,不再继续搜索。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @note 常用于查找特定玩家、组队系统、交易系统等
 */
template<class Check>
void Trinity::PlayerSearcher<Check>::Visit(PlayerMapType &m)
{
    // 如果已经找到对象,直接返回
    if (i_object)
        return;

    for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
        {
            i_object = itr->GetSource();
            return;
        }
    }
}

/**
 * @brief 访问玩家映射并搜索最后一个符合条件的玩家
 *
 * 遍历所有玩家,返回最后一个满足条件的玩家。
 *
 * @tparam Check 检查器类型
 * @param m 玩家映射容器
 *
 * @note 性能: 总是遍历所有对象,复杂度 O(n)
 */
template<class Check>
void Trinity::PlayerLastSearcher<Check>::Visit(PlayerMapType& m)
{
    for (PlayerMapType::iterator itr = m.begin(); itr != m.end(); ++itr)
    {
        if (!itr->GetSource()->InSamePhase(i_phaseMask))
            continue;

        if (i_check(itr->GetSource()))
            i_object = itr->GetSource();
    }
}

template<class Builder>
void Trinity::LocalizedPacketDo<Builder>::operator()(Player* p)
{
    LocaleConstant loc_idx = p->GetSession()->GetSessionDbLocaleIndex();
    uint32 cache_idx = loc_idx+1;
    WorldPacket* data;

    // create if not cached yet
    if (i_data_cache.size() < cache_idx + 1 || !i_data_cache[cache_idx])
    {
        if (i_data_cache.size() < cache_idx + 1)
            i_data_cache.resize(cache_idx + 1);

        data = new WorldPacket();

        i_builder(*data, loc_idx);

        i_data_cache[cache_idx] = data;
    }
    else
        data = i_data_cache[cache_idx];

    p->SendDirectMessage(data);
}

template<class Builder>
void Trinity::LocalizedPacketListDo<Builder>::operator()(Player* p)
{
    LocaleConstant loc_idx = p->GetSession()->GetSessionDbLocaleIndex();
    uint32 cache_idx = loc_idx+1;
    WorldPacketList* data_list;

    // create if not cached yet
    if (i_data_cache.size() < cache_idx+1 || i_data_cache[cache_idx].empty())
    {
        if (i_data_cache.size() < cache_idx+1)
            i_data_cache.resize(cache_idx+1);

        data_list = &i_data_cache[cache_idx];

        i_builder(*data_list, loc_idx);
    }
    else
        data_list = &i_data_cache[cache_idx];

    for (size_t i = 0; i < data_list->size(); ++i)
        p->SendDirectMessage((*data_list)[i]);
}

#endif                                                      // TRINITY_GRIDNOTIFIERSIMPL_H
