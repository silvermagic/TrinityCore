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
 * @file NGrid.h
 * @brief 网格(NGrid)类定义
 *
 * 本文件定义了NGrid类和GridInfo类，用于管理地图中的网格单元。
 * NGrid是网格系统的核心数据结构，封装了NxN个单元格(Cell)的集合。
 *
 * 主要功能:
 * - 管理网格的状态(激活、空闲、卸载中等)
 * - 维护网格的加载/卸载计时器
 * - 提供单元格访问接口
 * - 支持网格中对象的遍历操作
 *
 * 网格状态机:
 * - INVALID: 无效状态，网格刚创建
 * - ACTIVE: 激活状态，有玩家在附近
 * - IDLE: 空闲状态，没有玩家但数据仍保留
 * - REMOVAL: 移除状态，准备卸载
 *
 * 内存管理:
 * - 网格使用引用计数和锁机制防止过早卸载
 * - 支持延迟卸载以优化性能
 */

#ifndef TRINITY_NGRID_H
#define TRINITY_NGRID_H

/** NGrid is nothing more than a wrapper of the Grid with an NxN cells
 *  NGrid只是Grid的封装，包含NxN个单元格
 */

#include "Grid.h"
#include "GridReference.h"
#include "Timer.h"
#include "Util.h"

/**
 * @brief 默认的可见性通知周期(毫秒)
 *
 * 用于控制网格内对象位置更新的频率
 */
#define DEFAULT_VISIBILITY_NOTIFY_PERIOD      1000

/**
 * @class GridInfo
 * @brief 网格信息类，管理单个网格的状态和计时器
 *
 * GridInfo存储网格的运行时信息，包括:
 * - 卸载计时器：控制网格何时可以卸载
 * - 可见性更新计时器：控制对象重定位通知
 * - 卸载锁：防止网格被意外卸载
 *
 * 卸载保护机制:
 * - 活动对象锁：防止有活动生成点的网格被卸载
 * - 显式锁：手动锁定或配置锁定的网格
 * - 引用锁：实例地图副本引用的网格
 */
class TC_GAME_API GridInfo
{
public:
    /**
     * @brief 默认构造函数
     *
     * 初始化网格信息，设置默认的卸载时间和可见性更新周期
     */
    GridInfo();

    /**
     * @brief 构造函数，指定过期时间
     *
     * @param expiry 网格过期时间(秒)
     * @param unload 是否允许卸载，默认为true
     */
    GridInfo(time_t expiry, bool unload = true);

    /**
     * @brief 获取卸载计时器
     *
     * @return TimeTracker const& 卸载计时器的常量引用
     *
     * 用于检查网格距离卸载还有多长时间
     */
    TimeTracker const& getTimeTracker() const { return i_timer; }

    /**
     * @brief 检查网格是否被锁定不能卸载
     *
     * @return bool 如果网格被锁定返回true，否则返回false
     *
     * 任一锁被设置时，网格都不能被卸载:
     * - i_unloadActiveLockCount > 0: 有活动对象生成点
     * - i_unloadExplicitLock: 手动锁定
     * - i_unloadReferenceLock: 实例引用锁定
     */
    bool getUnloadLock() const { return i_unloadActiveLockCount || i_unloadExplicitLock || i_unloadReferenceLock; }

    /**
     * @brief 设置显式卸载锁
     *
     * @param on true锁定，false解锁
     *
     * 用于手动控制特定网格的卸载行为，或根据配置锁定网格
     */
    void setUnloadExplicitLock(bool on) { i_unloadExplicitLock = on; }

    /**
     * @brief 设置引用卸载锁
     *
     * @param on true锁定，false解锁
     *
     * 用于实例地图副本引用的网格锁定
     */
    void setUnloadReferenceLock(bool on) { i_unloadReferenceLock = on; }

    /**
     * @brief 增加活动对象锁计数
     *
     * 每个活动对象生成点会调用此方法锁定网格
     */
    void incUnloadActiveLock() { ++i_unloadActiveLockCount; }

    /**
     * @brief 减少活动对象锁计数
     *
     * 活动对象生成点移除时调用此方法解锁网格
     * 注意: 计数不会低于0
     */
    void decUnloadActiveLock() { if (i_unloadActiveLockCount) --i_unloadActiveLockCount; }

    /**
     * @brief 设置计时器
     *
     * @param pTimer 新的计时器值
     */
    void setTimer(TimeTracker const& pTimer) { i_timer = pTimer; }

    /**
     * @brief 重置计时器
     *
     * @param interval 新的时间间隔(毫秒)
     *
     * 重置卸载计时器到指定间隔，通常用于延长网格的生命周期
     */
    void ResetTimeTracker(time_t interval) { i_timer.Reset(interval); }

    /**
     * @brief 更新计时器
     *
     * @param diff 经历的时间(毫秒)
     *
     * 在地图更新循环中调用，递减卸载计时器
     */
    void UpdateTimeTracker(time_t diff) { i_timer.Update(diff); }

    /**
     * @brief 获取重定位计时器
     *
     * @return PeriodicTimer& 可见性更新计时器的引用
     *
     * 用于控制网格内对象位置更新的周期
     */
    PeriodicTimer& getRelocationTimer() { return vis_Update; }

private:
    TimeTracker i_timer;              ///< 卸载计时器，控制网格何时可以卸载
    PeriodicTimer vis_Update;         ///< 可见性更新计时器，控制对象重定位通知周期

    uint16 i_unloadActiveLockCount;   ///< 活动对象锁计数，防止有活动生成点的网格被卸载
    bool   i_unloadExplicitLock    : 1;  ///< 显式锁，手动锁定或配置设置
    bool   i_unloadReferenceLock   : 1;  ///< 引用锁，来自实例地图副本
};

/**
 * @enum grid_state_t
 * @brief 网格状态枚举
 *
 * 定义网格可能处于的所有状态，用于网格状态机管理。
 */
typedef enum
{
    GRID_STATE_INVALID = 0,  ///< 无效状态，网格刚创建或重置后的初始状态
    GRID_STATE_ACTIVE = 1,   ///< 激活状态，网格中有玩家或被玩家关注
    GRID_STATE_IDLE = 2,     ///< 空闲状态，网格中没有玩家但数据仍保留在内存中
    GRID_STATE_REMOVAL= 3,   ///< 移除状态，网格准备卸载，正在进行清理工作
    MAX_GRID_STATE = 4       ///< 状态总数，用于数组边界检查
} grid_state_t;

/**
 * @class NGrid
 * @brief 网格类，封装NxN个单元格的集合
 *
 * @tparam N 每个维度上的单元格数量(通常为8)
 * @tparam ACTIVE_OBJECT 活动对象类型(通常是Player)
 * @tparam WORLD_OBJECT_TYPES 世界对象类型列表
 * @tparam GRID_OBJECT_TYPES 网格对象类型列表
 *
 * NGrid是网格系统的核心数据结构。每个地图被划分为64x64个网格(NGrid)，
 * 每个网格包含8x8个单元格(Cell)。网格是地图对象管理的基本单位。
 *
 * 主要职责:
 * - 存储和管理单元格集合
 * - 维护网格的状态和生命周期
 * - 提供对象访问和遍历接口
 * - 管理网格加载/卸载逻辑
 *
 * 线程安全:
 * - 网格操作需要在地图锁保护下进行
 * - 读操作可以并发进行
 * - 写操作需要独占访问
 *
 * 使用示例:
 * @code
 * // 获取单元格
 * GridType& cell = grid.GetGridType(x, y);
 *
 * // 遍历网格中的所有单元格
 * TypeContainerVisitor<SomeVisitor, GridTypeMapContainer> visitor(...);
 * grid.VisitAllGrids(visitor);
 * @endcode
 */
template
<
uint32 N,
class ACTIVE_OBJECT,
class WORLD_OBJECT_TYPES,
class GRID_OBJECT_TYPES
>
class NGrid
{
    public:
        /// 单元格类型定义
        typedef Grid<ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> GridType;

        /**
         * @brief 构造函数
         *
         * @param id 网格ID(0-4095，对应64x64网格)
         * @param x 网格X坐标(0-63)
         * @param y 网格Y坐标(0-63)
         * @param expiry 网格过期时间(秒)
         * @param unload 是否允许卸载，默认为true
         *
         * 初始化网格及其所有单元格，设置初始状态为INVALID
         */
        NGrid(uint32 id, int32 x, int32 y, time_t expiry, bool unload = true) :
            i_gridId(id), i_GridInfo(GridInfo(expiry, unload)), i_x(x), i_y(y),
            i_cellstate(GRID_STATE_INVALID), i_GridObjectDataLoaded(false)
        { }

        /**
         * @brief 获取指定位置的单元格(可修改版本)
         *
         * @param x 单元格X坐标(0到N-1)
         * @param y 单元格Y坐标(0到N-1)
         * @return GridType& 单元格的引用
         *
         * @note 坐标越界会触发断言失败
         *
         * 调用时机: 在需要修改单元格内容时使用
         */
        GridType& GetGridType(const uint32 x, const uint32 y)
        {
            ASSERT(x < N && y < N);
            return i_cells[x][y];
        }

        /**
         * @brief 获取指定位置的单元格(只读版本)
         *
         * @param x 单元格X坐标(0到N-1)
         * @param y 单元格Y坐标(0到N-1)
         * @return GridType const& 单元格的常量引用
         *
         * @note 坐标越界会触发断言失败
         *
         * 调用时机: 在只需要读取单元格内容时使用
         */
        GridType const& GetGridType(const uint32 x, const uint32 y) const
        {
            ASSERT(x < N && y < N);
            return i_cells[x][y];
        }

        /**
         * @brief 获取网格ID
         *
         * @return uint32 网格的唯一标识符(0-4095)
         */
        uint32 GetGridId(void) const { return i_gridId; }

        /**
         * @brief 获取网格状态
         *
         * @return grid_state_t 网格的当前状态
         *
         * 用于网格状态机管理，决定是否加载/卸载网格
         */
        grid_state_t GetGridState(void) const { return i_cellstate; }

        /**
         * @brief 设置网格状态
         *
         * @param s 新的网格状态
         *
         * 在网格状态转换时调用，如从IDLE到ACTIVE
         */
        void SetGridState(grid_state_t s) { i_cellstate = s; }

        /**
         * @brief 获取网格X坐标
         *
         * @return int32 网格在地图中的X坐标(0-63)
         */
        int32 getX() const { return i_x; }

        /**
         * @brief 获取网格Y坐标
         *
         * @return int32 网格在地图中的Y坐标(0-63)
         */
        int32 getY() const { return i_y; }

        /**
         * @brief 将网格链接到引用管理器
         *
         * @param pTo 目标引用管理器
         *
         * 建立网格与地图引用管理器之间的链接关系
         */
        void link(GridRefManager<NGrid<N, ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> >* pTo)
        {
            i_Reference.link(pTo, this);
        }

        /**
         * @brief 检查网格对象数据是否已加载
         *
         * @return bool 如果数据已加载返回true，否则返回false
         *
         * 用于判断网格是否需要从数据库加载数据
         */
        bool isGridObjectDataLoaded() const { return i_GridObjectDataLoaded; }

        /**
         * @brief 设置网格对象数据加载状态
         *
         * @param pLoaded true表示已加载，false表示未加载
         *
         * 在网格数据加载完成或卸载时调用
         */
        void setGridObjectDataLoaded(bool pLoaded) { i_GridObjectDataLoaded = pLoaded; }

        /**
         * @brief 获取网格信息引用
         *
         * @return GridInfo* 网格信息对象的指针
         *
         * 用于访问网格的计时器和状态信息
         */
        GridInfo* getGridInfoRef() { return &i_GridInfo; }

        /**
         * @brief 获取卸载计时器
         *
         * @return TimeTracker const& 卸载计时器的常量引用
         */
        TimeTracker const& getTimeTracker() const { return i_GridInfo.getTimeTracker(); }

        /**
         * @brief 检查网格是否被锁定不能卸载
         *
         * @return bool 如果被锁定返回true
         */
        bool getUnloadLock() const { return i_GridInfo.getUnloadLock(); }

        /**
         * @brief 设置显式卸载锁
         *
         * @param on true锁定，false解锁
         */
        void setUnloadExplicitLock(bool on) { i_GridInfo.setUnloadExplicitLock(on); }

        /**
         * @brief 设置引用卸载锁
         *
         * @param on true锁定，false解锁
         */
        void setUnloadReferenceLock(bool on) { i_GridInfo.setUnloadReferenceLock(on); }

        /**
         * @brief 增加活动对象锁计数
         */
        void incUnloadActiveLock() { i_GridInfo.incUnloadActiveLock(); }

        /**
         * @brief 减少活动对象锁计数
         */
        void decUnloadActiveLock() { i_GridInfo.decUnloadActiveLock(); }

        /**
         * @brief 重置卸载计时器
         *
         * @param interval 新的时间间隔(毫秒)
         */
        void ResetTimeTracker(time_t interval) { i_GridInfo.ResetTimeTracker(interval); }

        /**
         * @brief 更新卸载计时器
         *
         * @param diff 经历的时间(毫秒)
         */
        void UpdateTimeTracker(time_t diff) { i_GridInfo.UpdateTimeTracker(diff); }

        /*
        // 已注释: 以下方法提供直接的对象添加/移除功能，但当前未使用
        // 所有对象管理都通过GridRefManager进行

        template<class SPECIFIC_OBJECT> void AddWorldObject(const uint32 x, const uint32 y, SPECIFIC_OBJECT *obj)
        {
            GetGridType(x, y).AddWorldObject(obj);
        }

        template<class SPECIFIC_OBJECT> void RemoveWorldObject(const uint32 x, const uint32 y, SPECIFIC_OBJECT *obj)
        {
            GetGridType(x, y).RemoveWorldObject(obj);
        }

        template<class SPECIFIC_OBJECT> void AddGridObject(const uint32 x, const uint32 y, SPECIFIC_OBJECT *obj)
        {
            GetGridType(x, y).AddGridObject(obj);
        }

        template<class SPECIFIC_OBJECT> void RemoveGridObject(const uint32 x, const uint32 y, SPECIFIC_OBJECT *obj)
        {
            GetGridType(x, y).RemoveGridObject(obj);
        }
        */

        /**
         * @brief 访问网格中的所有单元格
         *
         * @tparam T 访问者类型
         * @tparam TT 类型容器类型
         * @param visitor 类型容器访问者
         *
         * 遍历网格中的所有单元格，对每个单元格执行访问者操作。
         *
         * 调用时机:
         * - 网格卸载时清理所有对象
         * - 批量操作网格中的所有对象
         *
         * 性能注意: O(N^2)复杂度，遍历所有单元格
         */
        template<class T, class TT>
        void VisitAllGrids(TypeContainerVisitor<T, TypeMapContainer<TT> > &visitor)
        {
            for (uint32 x = 0; x < N; ++x)
                for (uint32 y = 0; y < N; ++y)
                    GetGridType(x, y).Visit(visitor);
        }

        /**
         * @brief 访问网格中的单个单元格
         *
         * @tparam T 访问者类型
         * @tparam TT 类型容器类型
         * @param x 单元格X坐标(0到N-1)
         * @param y 单元格Y坐标(0到N-1)
         * @param visitor 类型容器访问者
         *
         * 对指定的单元格执行访问者操作。
         *
         * 调用时机:
         * - 加载特定单元格的对象
         * - 更新特定单元格内的对象
         *
         * 性能注意: O(1)复杂度，直接访问单个单元格
         */
        template<class T, class TT>
        void VisitGrid(const uint32 x, const uint32 y, TypeContainerVisitor<T, TypeMapContainer<TT> > &visitor)
        {
            GetGridType(x, y).Visit(visitor);
        }

        /*
        // 已注释: 获取网格中的活动对象计数
        // 禁用以避免混淆，活动对象通常有其他含义

        uint32 GetActiveObjectCountInGrid() const
        {
            uint32 count = 0;
            for (uint32 x = 0; x < N; ++x)
                for (uint32 y = 0; y < N; ++y)
                    count += i_cells[x][y].ActiveObjectsInGrid();
            return count;
        }
        */

        /**
         * @brief 获取网格中特定类型的世界对象数量
         *
         * @tparam T 要统计的对象类型
         * @return uint32 对象数量
         *
         * 遍历所有单元格，统计指定类型的世界对象数量。
         *
         * 调用时机: 调试、统计或性能分析时使用
         */
        template<class T>
        uint32 GetWorldObjectCountInNGrid() const
        {
            uint32 count = 0;
            for (uint32 x = 0; x < N; ++x)
                for (uint32 y = 0; y < N; ++y)
                    count += i_cells[x][y].template GetWorldObjectCountInGrid<T>();
            return count;
        }

    private:
        uint32 i_gridId;        ///< 网格ID，唯一标识地图中的网格(0-4095)
        GridInfo i_GridInfo;    ///< 网格信息，包含状态和计时器

        /// 网格引用，用于将网格链接到地图的引用管理器
        GridReference<NGrid<N, ACTIVE_OBJECT, WORLD_OBJECT_TYPES, GRID_OBJECT_TYPES> > i_Reference;

        int32 i_x;              ///< 网格X坐标(0-63)
        int32 i_y;              ///< 网格Y坐标(0-63)
        grid_state_t i_cellstate;  ///< 网格当前状态

        /// 单元格数组，存储网格中的所有单元格(N x N)
        GridType i_cells[N][N];

        /// 网格对象数据是否已从数据库加载
        bool i_GridObjectDataLoaded;
};
#endif
