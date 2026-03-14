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
 * @file GridStates.cpp
 * @brief 网格状态模式实现文件
 *
 * 本文件实现了网格状态模式中各个具体状态类的更新方法。
 * 网格状态系统通过状态模式管理地图网格的生命周期，实现网格的动态加载和卸载，
 * 从而优化内存使用和服务器性能。
 *
 * 实现的状态类:
 * - InvalidState: 无效状态，网格刚创建时的初始状态，不执行任何更新操作
 * - ActiveState: 激活状态，网格中有玩家或活动对象，定期检查活跃度
 * - IdleState: 空闲状态，网格刚变为空闲，立即转换到移除状态
 * - RemovalState: 移除状态，等待卸载计时器到期后卸载网格
 *
 * 状态转换流程示例:
 * @code
 * 玩家进入 -> ActiveState (网格加载并激活)
 * 玩家离开 -> ActiveState检测到无玩家 -> IdleState (立即)
 * IdleState -> RemovalState (等待卸载计时器)
 * 计时器到期 -> RemovalState卸载网格 或 玩家返回 -> ActiveState (重新激活)
 * @endcode
 *
 * 性能优化要点:
 * 1. ActiveState使用计时器间隔检查，避免每帧遍历对象列表
 * 2. IdleState作为过渡状态，记录日志并快速转换
 * 3. RemovalState支持卸载锁和延迟卸载，防止频繁IO
 * 4. 使用访问者模式停止网格中的对象活动
 *
 * @see GridStates.h 状态类定义和详细说明
 * @see GridInfo 网格信息管理类
 * @see Map::Update 网格状态更新的调用入口
 */

#include "GridStates.h"
#include "GridNotifiers.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGridLoader.h"

/**
 * @brief 更新无效状态的网格
 *
 * @param map 网格所属的地图对象(未使用)
 * @param grid 网格对象(未使用)
 * @param info 网格信息对象(未使用)
 * @param t_diff 时间增量(未使用)
 *
 * 无效状态是网格创建后的初始状态，此方法不执行任何操作。
 * 网格在此状态下等待被加载并转换为ActiveState。
 *
 * 实现说明:
 * - 所有参数都被忽略，因为无效状态不需要更新
 * - 网格数据加载后会直接设置状态为ActiveState
 * - 一旦离开无效状态，网格不会回到此状态
 *
 * 调用时机:
 * - 在Map::Update中被周期性调用
 * - 通常情况下，网格加载完成后不会再调用此方法
 *
 * 性能: O(1)，立即返回，无任何操作
 */
void InvalidState::Update(Map&, NGridType&, GridInfo&, uint32) const
{
}

/**
 * @brief 更新激活状态的网格
 *
 * @param map 网格所属的地图对象
 * @param grid 要更新的网格对象
 * @param info 网格信息对象，包含计时器和锁状态
 * @param diff 自上次更新以来经过的时间(毫秒)
 *
 * 激活状态表示网格正在被使用，需要定期检查网格是否仍有玩家或活动对象。
 * 当网格变为空闲时，将网格转换为IdleState以准备卸载。
 *
 * 更新流程:
 * 1. 更新网格计时器(递减diff毫秒)
 * 2. 当计时器到期时检查网格活跃度:
 *    a) 检查网格中是否还有玩家
 *    b) 检查网格附近是否有活动对象(如被关注的生物)
 * 3. 如果都没有活动对象:
 *    - 使用ObjectGridStoper访问者停止网格中所有对象的活动
 *    - 将网格状态转换为GRID_STATE_IDLE
 *    - 记录调试日志
 * 4. 如果仍有活动对象:
 *    - 重置计时器，延长活跃周期(设置为原过期时间的10%)
 *
 * 性能优化:
 * - 不是每帧检查，而是每隔(grid_expiry/10)毫秒检查一次
 * - 避免频繁遍历网格中的所有对象
 * - 使用访问者模式统一处理网格中的对象
 *
 * 调用时机: 每帧调用(通常每秒20-50次，取决于服务器帧率)
 *
 * 性能分析:
 * - 计时器未到期: O(1)，仅更新计时器
 * - 计时器到期且无活动对象: O(N)，N为网格中对象数量(遍历停止活动)
 * - 计时器到期且有活动对象: O(M)，M为附近活动对象检查范围
 */
void ActiveState::Update(Map& map, NGridType& grid, GridInfo& info, uint32 diff) const
{
    // 性能优化: 只每隔(grid_expiry/10)毫秒检查一次网格活跃度
    // 避免每帧检查，因为这种检查相对昂贵(需要遍历对象列表)
    info.UpdateTimeTracker(diff);
    if (info.getTimeTracker().Passed())
    {
        // 计时器到期，检查网格是否仍活跃
        // 检查两个条件: 1)网格中是否有玩家  2)附近是否有活动对象
        if (!grid.GetWorldObjectCountInNGrid<Player>() && !map.ActiveObjectsNearGrid(grid))
        {
            // 网格变为空闲: 停止网格中所有对象的活动
            // 使用访问者模式遍历网格中的所有对象类型
            ObjectGridStoper worker;
            TypeContainerVisitor<ObjectGridStoper, GridTypeMapContainer> visitor(worker);
            grid.VisitAllGrids(visitor);

            // 状态转换: ActiveState -> IdleState
            grid.SetGridState(GRID_STATE_IDLE);
            TC_LOG_DEBUG("maps", "Grid[{}, {}] on map {} moved to IDLE state", grid.getX(), grid.getY(), map.GetId());
        }
        else
        {
            // 网格仍活跃: 重置计时器，延长活跃周期
            // 参数0.1f表示重置为原过期时间的10%，减少检查频率
            map.ResetGridExpiry(grid, 0.1f);
        }
    }
}

/**
 * @brief 更新空闲状态的网格
 *
 * @param map 网格所属的地图对象
 * @param grid 要更新的网格对象
 * @param info 网格信息对象(未使用)
 * @param diff 时间增量(未使用)
 *
 * 空闲状态是一个短暂的过渡状态，表示网格刚刚变为空闲(无玩家)。
 * 此方法会立即将网格转换为RemovalState，准备卸载。
 *
 * 更新流程:
 * 1. 重置网格卸载计时器到默认值(通常为配置的网格卸载延迟时间)
 * 2. 立即将网格状态转换为GRID_STATE_REMOVAL
 * 3. 记录状态转换的调试日志
 *
 * 设计意图:
 * - IdleState作为ActiveState和RemovalState之间的明确边界
 * - 为未来的空闲优化预留扩展空间(如空闲状态的低优先级更新)
 * - 记录状态转换日志，便于调试和性能分析
 *
 * 调用时机:
 * - 网格刚从ActiveState转换到IdleState后的第一次Update调用
 * - 通常只执行一次，然后立即转换到RemovalState
 *
 * 状态持续时间:
 * - 极短，通常只持续一帧
 * - 在下一帧Update调用时立即转换到RemovalState
 *
 * 性能: O(1)，仅重置计时器和设置状态
 */
void IdleState::Update(Map& map, NGridType& grid, GridInfo&, uint32) const
{
    // 重置网格卸载计时器到默认值
    // 此时设置的是完整的卸载延迟时间，用于RemovalState等待
    map.ResetGridExpiry(grid);

    // 状态转换: IdleState -> RemovalState
    // 进入RemovalState后，网格将等待卸载计时器到期
    grid.SetGridState(GRID_STATE_REMOVAL);
    TC_LOG_DEBUG("maps", "Grid[{}, {}] on map {} moved to REMOVAL state", grid.getX(), grid.getY(), map.GetId());
}

/**
 * @brief 更新移除状态的网格
 *
 * @param map 网格所属的地图对象
 * @param grid 要更新的网格对象
 * @param info 网格信息对象，包含计时器和锁状态
 * @param diff 自上次更新以来经过的时间(毫秒)
 *
 * 移除状态表示网格正在等待卸载。此方法会检查卸载锁和计时器，
 * 在条件满足时卸载网格数据，释放内存。
 *
 * 更新流程:
 * 1. 检查卸载锁状态:
 *    - 如果被锁定(i_unloadLock为true)，不执行任何操作
 *    - 卸载锁可能由活动对象生成点、显式锁或实例引用设置
 * 2. 如果未被锁定:
 *    - 更新卸载计时器(递减diff毫秒)
 *    - 当计时器到期时尝试卸载网格(map.UnloadGrid)
 * 3. 卸载结果处理:
 *    - 卸载成功: 网格从地图中移除，Update不再被调用
 *    - 卸载失败: 表示有玩家进入或网格被重新激活，重置计时器继续等待
 *
 * 卸载失败原因:
 * - 有玩家进入网格范围(玩家会取消卸载流程)
 * - 网格被临时锁定(如实例传送门、战斗中的生物等)
 * - 网格中有未完成的活动对象
 * - 卸载过程中出现错误
 *
 * 卸载保护机制:
 * - i_unloadActiveLockCount: 活动对象生成点锁(如副本入口)
 * - i_unloadExplicitLock: 手动设置的显式锁(脚本或GM命令)
 * - i_unloadReferenceLock: 实例引用锁(玩家在实例中)
 * - 任一锁被设置，info.getUnloadLock()返回true，阻止卸载
 *
 * 调用时机: 每帧调用，但只在计时器到期且未锁定时尝试卸载
 *
 * 性能分析:
 * - 被锁定: O(1)，立即返回
 * - 计时器未到期: O(1)，仅更新计时器
 * - 尝试卸载: O(N)，N为网格中对象数量(需要遍历并保存/删除对象)
 *
 * 内存优化:
 * - 卸载网格可以释放大量内存(网格中所有对象的内存)
 * - 延迟卸载减少频繁IO操作
 * - 卸载锁保护重要网格不被意外卸载
 */
void RemovalState::Update(Map& map, NGridType& grid, GridInfo& info, uint32 diff) const
{
    // 检查卸载锁: 如果网格被锁定，不执行卸载操作
    // 卸载锁用于保护重要网格(如玩家附近、活动区域、实例等)
    if (!info.getUnloadLock())
    {
        // 网格未被锁定: 更新卸载计时器
        info.UpdateTimeTracker(diff);

        // 计时器到期: 尝试卸载网格
        if (info.getTimeTracker().Passed() && !map.UnloadGrid(grid, false))
        {
            // 卸载失败: 有玩家进入或网格被重新激活
            // 重置计时器，延长等待时间
            // 参数false表示非强制卸载，有玩家时会失败并返回false
            TC_LOG_DEBUG("maps", "Grid[{}, {}] for map {} differed unloading due to players or active objects nearby", grid.getX(), grid.getY(), map.GetId());
            map.ResetGridExpiry(grid);
        }
        // 卸载成功: map.UnloadGrid返回true，网格已从地图移除
        // 此后该网格的Update方法不再被调用
    }
}
