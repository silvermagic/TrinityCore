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
 * @file GridStates.h
 * @brief 网格状态模式实现
 *
 * 本文件实现了网格状态模式(State Pattern)，用于管理地图网格的生命周期状态转换。
 * 网格状态系统是地图网格加载/卸载机制的核心，负责控制网格的内存使用和性能优化。
 *
 * 主要功能:
 * - 定义网格状态基类和具体状态类
 * - 实现网格状态机转换逻辑
 * - 控制网格的加载、激活、空闲和卸载过程
 *
 * 状态机设计:
 * - GridState: 抽象基类，定义状态更新接口
 * - InvalidState: 无效状态，网格刚创建时的初始状态
 * - ActiveState: 激活状态，有玩家在网格内或附近
 * - IdleState: 空闲状态，没有玩家但数据仍保留在内存中
 * - RemovalState: 移除状态，准备卸载网格数据
 *
 * 状态转换流程:
 * @code
 * InvalidState (创建)
 *      ↓ (网格加载)
 * ActiveState (有玩家)
 *      ↓ (无玩家且无活动对象)
 * IdleState (空闲等待)
 *      ↓ (空闲超时)
 * RemovalState (准备卸载)
 *      ↓ (卸载完成或被锁定)
 * ActiveState (被玩家激活) 或 网格卸载
 * @endcode
 *
 * 性能优化:
 * - 使用状态模式避免大量条件判断
 * - 空闲状态延迟卸载，避免频繁加载/卸载
 * - 卸载锁机制防止重要网格被意外卸载
 * - 定期检查而非每帧检查，减少CPU开销
 *
 * 使用示例:
 * @code
 * // 状态更新调用
 * GridState* state = sInstanceState; // 或其他状态
 * state->Update(map, grid, gridInfo, diff);
 * @endcode
 *
 * @see GridInfo 网格信息类，管理计时器和锁
 * @see NGrid 网格类，存储单元格数据
 * @see Map 地图类，管理所有网格
 */

#ifndef TRINITY_GRIDSTATES_H
#define TRINITY_GRIDSTATES_H

#include "GridDefines.h"
#include "NGrid.h"

class Map;

/**
 * @class GridState
 * @brief 网格状态抽象基类
 *
 * GridState是网格状态模式的抽象基类，定义了所有网格状态的公共接口。
 * 每个具体状态类(InvalidState、ActiveState、IdleState、RemovalState)负责
 * 实现该状态下网格的更新逻辑和状态转换决策。
 *
 * 设计模式:
 * - 使用状态模式(State Pattern)，将状态相关的行为封装到独立类中
 * - 避免在Map类中使用大量条件语句判断网格状态
 * - 支持运行时状态切换，符合开闭原则
 *
 * 线程安全:
 * - 状态对象本身是无状态的(无成员变量)
 * - 所有状态实例共享，不需要同步
 * - 网格操作在Map锁保护下进行
 *
 * 生命周期:
 * - 状态对象是全局单例或静态对象
 * - 通过Map的网格管理器持有状态引用
 */
class TC_GAME_API GridState
{
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类对象可以通过基类指针正确析构
         */
        virtual ~GridState() { };

        /**
         * @brief 更新网格状态
         *
         * @param map 网格所属的地图对象
         * @param grid 要更新的网格对象
         * @param info 网格信息对象，包含计时器和锁状态
         * @param t_diff 自上次更新以来经过的时间(毫秒)
         *
         * 这是网格状态机的核心方法，每个具体状态类实现自己的更新逻辑:
         * - InvalidState: 不执行任何操作
         * - ActiveState: 检查网格是否仍活跃，可能转换为IdleState
         * - IdleState: 重置计时器并转换为RemovalState
         * - RemovalState: 执行网格卸载或延迟卸载
         *
         * 调用时机:
         * - 在Map::Update中被周期性调用
         * - 每个地图更新周期调用一次(通常每帧)
         *
         * 性能考虑:
         * - ActiveState使用计时器避免每帧检查玩家存在性
         * - 只对需要更新的网格调用，已卸载网格不调用
         */
        virtual void Update(Map &, NGridType&, GridInfo &, uint32 t_diff) const = 0;
};

/**
 * @class InvalidState
 * @brief 无效状态类，网格创建后的初始状态
 *
 * InvalidState表示网格刚创建但尚未加载任何数据的状态。
 * 这个状态是一个占位状态，网格数据加载后会直接转换为ActiveState。
 *
 * 状态特征:
 * - 网格对象已创建，但没有加载任何游戏对象
 * - i_GridObjectDataLoaded标志为false
 * - 不会执行任何更新操作
 *
 * 状态转换:
 * - 进入条件: 网格对象刚创建
 * - 离开条件: 网格数据加载完成，转换为ActiveState
 * - 永不转换回: 一旦离开，网格不会回到此状态(除非销毁重建)
 */
class TC_GAME_API InvalidState : public GridState
{
    public:
        /**
         * @brief 更新无效状态的网格
         *
         * @param map 网格所属的地图对象(未使用)
         * @param grid 网格对象(未使用)
         * @param info 网格信息对象(未使用)
         * @param t_diff 时间增量(未使用)
         *
         * 无效状态不执行任何更新操作，所有参数都被忽略。
         * 网格在此状态下等待被加载并转换为ActiveState。
         *
         * 性能说明: O(1)，立即返回
         */
        void Update(Map &, NGridType &, GridInfo &, uint32 t_diff) const override;
};

/**
 * @class ActiveState
 * @brief 激活状态类，网格中有玩家或被玩家关注
 *
 * ActiveState表示网格正在被使用，网格中的游戏对象需要保持活跃状态。
 * 此状态下，网格会定期检查是否仍有玩家或活动对象，以决定是否进入空闲状态。
 *
 * 状态特征:
 * - 网格中有至少一个玩家
 * - 或附近有活动对象(如被关注的生物)
 * - 网格中的所有对象都处于活跃状态
 * - 定期检查网格活跃度
 *
 * 状态转换:
 * - 进入条件: 玩家进入网格或网格数据刚加载完成
 * - 离开条件: 网格中没有玩家且附近无活动对象，转换为IdleState
 * - 可能返回: 玩家重新进入空闲网格时，直接转换回ActiveState
 *
 * 性能优化:
 * - 不是每帧检查，而是每隔(grid_expiry/10)毫秒检查一次
 * - 使用计时器控制检查频率，避免频繁遍历对象列表
 */
class TC_GAME_API ActiveState : public GridState
{
    public:
        /**
         * @brief 更新激活状态的网格
         *
         * @param map 网格所属的地图对象
         * @param grid 要更新的网格对象
         * @param info 网格信息对象，包含计时器和锁状态
         * @param diff 自上次更新以来经过的时间(毫秒)
         *
         * 更新逻辑:
         * 1. 更新网格计时器(递减)
         * 2. 当计时器到期时检查:
         *    - 网格中是否还有玩家
         *    - 附近是否有活动对象
         * 3. 如果都没有活动对象:
         *    - 停止网格中所有对象的活动
         *    - 转换到IdleState
         * 4. 如果仍有活动对象:
         *    - 重置计时器，延长活跃周期
         *
         * 调用时机: 每帧调用，但只在计时器到期时执行实际检查
         *
         * 性能说明:
         * - 计时器未到期: O(1)，仅更新计时器
         * - 计时器到期: O(N+M)，N为网格中对象数，M为附近活动对象检查
         */
        void Update(Map &, NGridType &, GridInfo &, uint32 t_diff) const override;
};

/**
 * @class IdleState
 * @brief 空闲状态类，网格中没有玩家但数据仍保留
 *
 * IdleState是一个过渡状态，表示网格刚刚变为空闲(无玩家)，
 * 但数据仍在内存中，等待一段时间后再卸载。这个延迟可以避免
 * 频繁的加载/卸载操作，提高性能。
 *
 * 状态特征:
 * - 网格中没有玩家
 * - 网格中的对象已停止活动(CombatAI停止等)
 * - 数据仍在内存中
 * - 准备进入卸载流程
 *
 * 状态转换:
 * - 进入条件: 从ActiveState转换而来(无玩家且无活动对象)
 * - 离开条件: 立即转换到RemovalState
 * - 可能返回: 玩家重新进入时，从RemovalState或IdleState转换回ActiveState
 *
 * 设计意图:
 * - 提供一个明确的状态转换点
 * - 为未来的空闲优化预留扩展空间
 * - 记录状态转换日志便于调试
 */
class TC_GAME_API IdleState : public GridState
{
    public:
        /**
         * @brief 更新空闲状态的网格
         *
         * @param map 网格所属的地图对象
         * @param grid 要更新的网格对象
         * @param info 网格信息对象(未使用)
         * @param diff 时间增量(未使用)
         *
         * 更新逻辑:
         * 1. 重置网格卸载计时器到默认值
         * 2. 立即将网格状态转换为RemovalState
         * 3. 记录状态转换日志
         *
         * 调用时机: 进入IdleState后第一次Update调用时
         *
         * 性能说明: O(1)，立即转换到RemovalState
         *
         * 注意: 此状态存在时间极短，通常只持续一帧
         */
        void Update(Map &, NGridType &, GridInfo &, uint32 t_diff) const override;
};

/**
 * @class RemovalState
 * @brief 移除状态类，网格准备卸载
 *
 * RemovalState表示网格正在等待卸载。在此状态下，网格会等待卸载计时器
 * 到期且卸载锁释放后才真正卸载。如果在此期间有玩家进入，网格会
 * 重新激活，取消卸载流程。
 *
 * 状态特征:
 * - 网格中没有玩家(通常情况)
 * - 正在等待卸载计时器到期
 * - 可能被卸载锁阻止卸载
 * - 随时可能被重新激活
 *
 * 状态转换:
 * - 进入条件: 从IdleState转换而来
 * - 离开条件:
 *   - 卸载成功: 网格从地图中移除
 *   - 被激活: 转换回ActiveState
 *   - 卸载延迟: 重置计时器继续等待
 *
 * 卸载保护机制:
 * - i_unloadActiveLockCount: 活动对象生成点锁
 * - i_unloadExplicitLock: 手动设置的显式锁
 * - i_unloadReferenceLock: 实例引用锁
 * - 任一锁被设置，网格都不会被卸载
 *
 * 性能优化:
 * - 延迟卸载减少频繁IO操作
 * - 卸载锁保护重要网格
 */
class TC_GAME_API RemovalState : public GridState
{
    public:
        /**
         * @brief 更新移除状态的网格
         *
         * @param map 网格所属的地图对象
         * @param grid 要更新的网格对象
         * @param info 网格信息对象，包含计时器和锁状态
         * @param diff 自上次更新以来经过的时间(毫秒)
         *
         * 更新逻辑:
         * 1. 检查卸载锁状态:
         *    - 如果被锁定，不执行任何操作(等待解锁)
         * 2. 如果未被锁定:
         *    - 更新卸载计时器
         *    - 当计时器到期时尝试卸载网格
         * 3. 卸载结果处理:
         *    - 卸载成功: 网格从地图移除
         *    - 卸载失败(有玩家进入): 重置计时器，延长等待
         *
         * 调用时机: 每帧调用，但只在计时器到期且未锁定时尝试卸载
         *
         * 性能说明:
         * - 被锁定: O(1)，立即返回
         * - 计时器未到期: O(1)，仅更新计时器
         * - 尝试卸载: O(N)，N为网格中对象数量
         *
         * 卸载失败原因:
         * - 有玩家进入网格范围
         * - 网格被临时锁定
         * - 网格中有未完成的活动对象
         */
        void Update(Map &, NGridType &, GridInfo &, uint32 t_diff) const override;
};
#endif
