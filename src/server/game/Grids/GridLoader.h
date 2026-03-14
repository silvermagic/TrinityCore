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
 * @file GridLoader.h
 * @brief 网格加载器模块 - 负责网格中对象类型的加载与卸载管理
 *
 * 本文件定义了 GridLoader 模板类,用于协调 Grid(网格)的对象生命周期管理。
 * 网格加载器采用模板方法模式,将实际的加载/卸载工作委托给具体的加载器和卸载器,
 * 实现了关注点分离和可扩展性设计。
 *
 * 主要职责:
 * - 管理网格对象的加载时机和流程
 * - 调度网格对象的卸载操作
 * - 协调本地网格和远程网格的管理
 * - 通过线程锁保证网格操作的线程安全性
 *
 * 设计说明:
 * - 使用模板方法模式,将具体加载逻辑委托给外部加载器
 * - 支持多种对象类型(WORLD_OBJECT_TYPES 和 GRID_OBJECT_TYPES)
 * - 卸载操作支持取消机制(当感兴趣的对象重新进入网格时)
 *
 * @note 当前实现已被注释,可能考虑使用 Grid::Visit 替代
 * @see Grid 网格核心数据结构
 * @see TypeContainerVisitor 类型容器访问器
 */

#ifndef TRINITY_GRIDLOADER_H
#define TRINITY_GRIDLOADER_H

/**
 * @class GridLoader
 * @brief 网格加载器模板类 - 管理网格中对象的加载、停止和卸载流程
 *
 * GridLoader 与 Grid 协同工作,负责在对象进入网格时加载对象类型(一个或多个),
 * 并在适当时候卸载对象。卸载操作是可调度的,如果感兴趣的对象重新进入网格,
 * 则可以取消卸载操作。
 *
 * 模板参数:
 * - ACTIVE_OBJECT: 活动对象类型,表示网格中的核心活动实体
 * - WORLD_OBJECT_TYPES: 世界对象类型集合,包含可见的交互对象
 * - GRID_OBJECT_TYPES: 网格对象类型集合,包含网格内的所有对象类型
 *
 * 工作流程:
 * 1. 当对象进入网格时,触发加载操作
 * 2. 加载过程中锁定网格,确保线程安全
 * 3. 委托给具体的加载器执行实际加载逻辑
 * 4. 卸载操作采用延迟调度机制,支持取消
 *
 * 线程安全性:
 * - 所有操作都在加锁状态下进行,保证多线程环境下的数据一致性
 * - 使用 Grid::LockGrid() 和 Grid::UnlockGrid() 实现互斥访问
 *
 * @note 该类不执行实际的加载和卸载工作,而是采用模板方法模式,
 *       将具体实现委托给实际的加载器(LOADER)和卸载器(UNLOADER)
 */

// 注意: 该实现已被注释,可能考虑使用 Grid::Visit 替代
// 原因: Grid::Visit 可能提供更简洁的实现方式
/*
#include "Define.h"
#include "Grid.h"
#include "TypeContainerVisitor.h"

template
<
class ACTIVE_OBJECT,          // 活动对象类型 - 网格中需要主动更新的核心实体
class WORLD_OBJECT_TYPES,     // 世界对象类型集合 - 网格中可见的交互对象类型容器
class GRID_OBJECT_TYPES       // 网格对象类型集合 - 网格中所有对象类型的容器
>
class GridLoader
{
    public:

        // @brief 加载网格中的对象
        //
        // 将指定网格中的对象进行加载初始化。该函数会在加载前锁定网格,
        // 执行加载操作后解锁,确保整个加载过程的线程安全性。
        //
        // @tparam LOADER 加载器类型,必须实现 Load(Grid&) 方法
        // @param grid 目标网格对象的引用,包含要加载的对象类型容器
        // @param loader 加载器实例,负责执行具体的加载逻辑
        //
        // @note 调用时机: 当对象首次进入网格或网格需要重新加载时
        // @note 性能说明: 加载期间会锁定网格,可能阻塞其他线程访问
        // @note 线程安全: 通过 Grid::LockGrid/UnlockGrid 保证线程安全
        //
        // @see Grid::LockGrid() 锁定网格
        // @see Grid::UnlockGrid() 解锁网格
        template<class LOADER>
            void Load(Grid<ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> &grid, LOADER &loader)
        {
            grid.LockGrid();        // 锁定网格,防止并发访问冲突
            loader.Load(grid);      // 委托给具体加载器执行加载逻辑
            grid.UnlockGrid();      // 解锁网格,允许其他线程访问
        }

        // @brief 停止网格中的对象活动
        //
        // 停止网格中正在运行的对象活动,通常用于网格卸载前的清理工作。
        // 该函数会在停止前锁定网格,执行停止操作后解锁。
        //
        // @tparam STOPER 停止器类型,必须实现 Stop(Grid&) 方法
        // @param grid 目标网格对象的引用
        // @param stoper 停止器实例,负责执行具体的停止逻辑
        //
        // @note 调用时机: 当网格即将卸载或需要暂停网格活动时
        // @note 性能说明: 停止期间会锁定网格,操作应尽可能快速完成
        // @note 线程安全: 通过网格锁机制保证线程安全
        //
        // @see Grid::LockGrid() 锁定网格
        // @see Grid::UnlockGrid() 解锁网格
        template<class STOPER>
            void Stop(Grid<ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> &grid, STOPER &stoper)
        {
            grid.LockGrid();        // 锁定网格,防止并发修改
            stoper.Stop(grid);      // 委托给具体停止器执行停止逻辑
            grid.UnlockGrid();      // 解锁网格
        }

        // @brief 卸载网格中的对象
        //
        // 卸载网格中的对象,释放相关资源。该函数会在卸载前锁定网格,
        // 执行卸载操作后解锁,确保卸载过程的完整性。
        //
        // 卸载是可调度操作,如果感兴趣的对象在卸载前重新进入网格,
        // 则可以取消卸载操作。
        //
        // @tparam UNLOADER 卸载器类型,必须实现 Unload(Grid&) 方法
        // @param grid 目标网格对象的引用
        // @param unloader 卸载器实例,负责执行具体的卸载逻辑
        //
        // @note 调用时机: 当网格不再被使用或需要释放资源时
        // @note 性能说明: 卸载期间会锁定网格,应避免长时间阻塞
        // @note 取消机制: 卸载是延迟调度的,可能被后续的加载操作取消
        // @note 线程安全: 通过网格锁机制保证卸载过程的原子性
        //
        // @see Grid::LockGrid() 锁定网格
        // @see Grid::UnlockGrid() 解锁网格
        template<class UNLOADER>
            void Unload(Grid<ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> &grid, UNLOADER &unloader)
        {
            grid.LockGrid();        // 锁定网格,确保卸载过程不被打断
            unloader.Unload(grid);  // 委托给具体卸载器执行卸载逻辑
            grid.UnlockGrid();      // 解锁网格,完成卸载流程
        }
};
*/
#endif
