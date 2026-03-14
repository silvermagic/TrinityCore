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
 * @file Grid.h
 * @brief 游戏世界网格管理模块
 *
 * 本文件定义了 Grid 类模板，用于管理游戏世界中的一个逻辑分段。
 * Grid 是游戏世界中对象存储和访问的基础单元，支持多种类型对象的高效管理。
 * 网格系统是 TrinityCore 世界分区和对象可见性管理的核心组件。
 */

#ifndef TRINITY_GRID_H
#define TRINITY_GRID_H

/**
 * @class Grid
 * @brief 游戏世界网格类，管理游戏世界中的一个逻辑分段
 *
 * Grid 是 TrinityCore 中游戏世界的逻辑分段单元，负责存储和管理特定区域内的游戏对象。
 * 该类模板在编译时绑定到特定类型的对象集合，通过类型容器实现对不同类型对象的高效管理。
 *
 * 网格系统支持三种对象类型分类：
 * - ACTIVE_OBJECT: 活动对象类型，通常是玩家等需要主动更新的对象
 * - WORLD_OBJECT_TYPES: 世界对象类型集合，包含玩家可见的动态对象
 * - GRID_OBJECT_TYPES: 网格对象类型集合，包含地形、静态对象等
 *
 * 网格加载机制：
 * - 动态加载器 (Dynamic Loader): 根据玩家位置动态加载和卸载网格
 * - 静态加载器 (Static Loader): 服务器启动时加载并常驻内存
 * - 按需加载器 (On-demand Loader): 根据需求实时加载
 *
 * 设计模式：
 * - 使用访问者模式 (Visitor Pattern) 实现对网格内对象的遍历
 * - 使用类型容器 (TypeContainer) 实现类型安全的对象存储
 * - 友元类 GridLoader 用于网格的加载和卸载操作
 *
 * @tparam ACTIVE_OBJECT 活动对象类型，通常是 Player 等需要主动更新的对象类型
 * @tparam WORLD_OBJECT_TYPES 世界对象类型集合，使用 TypeList 定义
 * @tparam GRID_OBJECT_TYPES 网格对象类型集合，使用 TypeList 定义
 */

#include "Define.h"
#include "TypeContainer.h"
#include "TypeContainerVisitor.h"

/// 前向声明：GridLoader 类用于网格的加载和卸载
template<class A, class T, class O> class GridLoader;

template
<
class ACTIVE_OBJECT,
class WORLD_OBJECT_TYPES,
class GRID_OBJECT_TYPES
>
class Grid
{
    /// 允许 GridLoader 访问私有成员，用于网格加载和卸载操作
    template<class A, class T, class O> friend class GridLoader;
    public:

        /**
         * @brief 析构函数，清理网格资源
         *
         * 当网格对象销毁时自动调用，负责释放网格持有的资源。
         * 注意：析构时不主动卸载网格内的对象，对象的生命周期由上层管理器控制。
         */
        ~Grid() { }

        /** an object of interested enters the grid
         */
        template<class SPECIFIC_OBJECT> void AddWorldObject(SPECIFIC_OBJECT *obj)
        {
            i_objects.template insert<SPECIFIC_OBJECT>(obj);
            ASSERT(obj->IsInGrid());
        }

        /** an object of interested exits the grid
         */
        //Actually an unlink is enough, no need to go through the container
        //template<class SPECIFIC_OBJECT> void RemoveWorldObject(SPECIFIC_OBJECT *obj)
        //{
        //    ASSERT(obj->GetGridRef().isValid());
        //    i_objects.template remove<SPECIFIC_OBJECT>(obj);
        //    ASSERT(!obj->GetGridRef().isValid());
        //}

        /** Refreshes/update the grid. This required for remote grids.
         */
        //void RefreshGrid(void) { /* TBI */}

        /** Locks a grid.  Any object enters must wait until the grid is unlock.
         */
        //void LockGrid(void) { /* TBI */ }

        /** Unlocks the grid.
         */
        //void UnlockGrid(void) { /* TBI */ }

        // Visit grid objects
        template<class T>
        void Visit(TypeContainerVisitor<T, TypeMapContainer<GRID_OBJECT_TYPES> > &visitor)
        {
            visitor.Visit(i_container);
        }

        // Visit world objects
        template<class T>
        void Visit(TypeContainerVisitor<T, TypeMapContainer<WORLD_OBJECT_TYPES> > &visitor)
        {
            visitor.Visit(i_objects);
        }

        /** Returns the number of object within the grid.
         */
        //unsigned int ActiveObjectsInGrid(void) const { return i_objects.template Count<ACTIVE_OBJECT>(); }
        template<class T>
        uint32 GetWorldObjectCountInGrid() const
        {
            return i_objects.template Count<T>();
        }

        /** Inserts a container type object into the grid.
         */
        template<class SPECIFIC_OBJECT> void AddGridObject(SPECIFIC_OBJECT *obj)
        {
            i_container.template insert<SPECIFIC_OBJECT>(obj);
            ASSERT(obj->IsInGrid());
        }

        /** Removes a containter type object from the grid
         */
        //template<class SPECIFIC_OBJECT> void RemoveGridObject(SPECIFIC_OBJECT *obj)
        //{
        //    ASSERT(obj->GetGridRef().isValid());
        //    i_container.template remove<SPECIFIC_OBJECT>(obj);
        //    ASSERT(!obj->GetGridRef().isValid());
        //}

        /*bool NoWorldObjectInGrid() const
        {
            return i_objects.GetElements().isEmpty();
        }

        bool NoGridObjectInGrid() const
        {
            return i_container.GetElements().isEmpty();
        }*/
    private:

        TypeMapContainer<GRID_OBJECT_TYPES> i_container;
        TypeMapContainer<WORLD_OBJECT_TYPES> i_objects;
        //typedef std::set<void*> ActiveGridObjects;
        //ActiveGridObjects m_activeGridObjects;
};
#endif
