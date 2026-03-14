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
 * @file MapInstanced.h
 * @brief 实例地图管理器头文件
 *
 * 本模块实现了实例地图的管理系统，负责管理可实例化的地图（地下城、团队副本、战场）。
 *
 * 主要职责：
 * - 管理同一地图ID的多个实例副本
 * - 创建和销毁实例地图
 * - 管理实例地图的生命周期
 * - 协调多个实例之间的网格数据共享
 * - 处理玩家进入实例的请求
 *
 * 设计理念：
 * - MapInstanced 作为基类 Map 的子类，代表一个"可实例化的地图模板"
 * - 每个实例（如一个地下城副本）都是一个独立的 Map 对象
 * - 通过实例ID区分同一地图的不同副本
 * - 共享网格数据以节省内存（多个实例可以共享地形数据）
 *
 * 使用场景：
 * - 地下城（Dungeon）：每个队伍有独立的副本实例
 * - 团队副本（Raid）：每个团队有独立的副本实例
 * - 战场（Battleground）：每场战斗有独立的实例
 * - 竞技场（Arena）：每场比赛有独立的实例
 *
 * 性能考虑：
 * - 网格引用计数机制避免重复加载相同的地形数据
 * - 实例按需创建和销毁，减少内存占用
 * - 使用智能指针管理实例生命周期，防止内存泄漏
 */

#ifndef TRINITY_MAP_INSTANCED_H
#define TRINITY_MAP_INSTANCED_H

#include "DBCEnums.h"
#include "InstanceSaveMgr.h"
#include "Map.h"
#include "UniqueTrackablePtr.h"

/**
 * @class MapInstanced
 * @brief 实例地图管理器类
 *
 * 继承自 Map 类，专门用于管理可以创建多个实例的地图。
 * 每个可实例化的地图（如地下城、团队副本、战场）都有一个对应的 MapInstanced 对象，
 * 该对象负责管理该地图的所有实例副本。
 *
 * 职责：
 * - 维护该地图的所有实例副本（通过实例ID索引）
 * - 创建新的实例副本
 * - 销毁不再使用的实例副本
 * - 管理网格数据的引用计数，允许多个实例共享地形数据
 *
 * 示例：
 * - 地图ID 248（诺森德-纳克萨玛斯）对应一个 MapInstanced 对象
 * - 该对象管理多个纳克萨玛斯的团队副本实例
 * - 每个团队进入时都会创建一个独立的实例副本
 */
class TC_GAME_API MapInstanced : public Map
{
    friend class MapManager;  // MapManager 需要访问私有成员来管理实例

    public:
        /**
         * @typedef InstancedMaps
         * @brief 实例地图映射类型
         *
         * 键：实例ID（uint32）
         * 值：指向具体实例地图的智能指针
         */
        typedef std::unordered_map<uint32, Trinity::unique_trackable_ptr<Map>> InstancedMaps;

        /**
         * @brief 构造函数
         * @param id 地图ID
         * @param expiry 网格过期时间
         *
         * 初始化 MapInstanced 对象，设置基础地图属性。
         */
        MapInstanced(uint32 id, time_t expiry);

        /**
         * @brief 析构函数
         *
         * 清理所有实例地图和资源。
         */
        ~MapInstanced() { }

        /**
         * @brief 更新所有实例地图
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 重写基类 Map 的 Update 方法。
         * 遍历所有实例地图并更新它们。
         *
         * 调用时机：
         * - 每个世界更新周期（World::Update）
         *
         * 性能注意事项：
         * - 更新所有活跃的实例地图，可能消耗较多CPU时间
         * - 空闲实例会被卸载以节省资源
         */
        void Update(uint32 diff) override;

        /**
         * @brief 延迟更新所有实例地图
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 重写基类 Map 的 DelayedUpdate 方法。
         * 执行需要在主更新之后进行的延迟操作。
         *
         * 调用时机：
         * - 在 Map::Update 之后调用
         */
        void DelayedUpdate(uint32 diff) override;

        /**
         * @brief 卸载所有实例地图
         *
         * 重写基类 Map 的 UnloadAll 方法。
         * 卸载所有实例地图的所有网格和资源。
         *
         * 调用时机：
         * - 服务器关闭时
         * - 地图被销毁时
         */
        void UnloadAll() override;

        /**
         * @brief 检查玩家是否可以进入该地图
         * @param player 尝试进入的玩家指针
         * @return 进入状态枚举值
         *
         * 重写基类 Map 的 CannotEnter 方法。
         * 对于 MapInstanced，通常总是允许进入（CAN_ENTER），
         * 因为具体的进入检查在各个实例地图中进行。
         *
         * 调用时机：
         * - 玩家尝试传送进入地图时
         */
        EnterState CannotEnter(Player* /*player*/) override;

        /**
         * @brief 为玩家创建或查找实例地图
         * @param mapId 地图ID
         * @param player 请求进入的玩家
         * @param loginInstanceId 登录时的实例ID（可选，用于玩家重新登录）
         * @return 指向实例地图的指针，失败返回 nullptr
         *
         * 这是玩家进入实例的主要入口点。
         * 根据玩家的队伍、绑定情况和实例存档，查找或创建合适的实例副本。
         *
         * 处理流程：
         * 1. 如果提供了 loginInstanceId，尝试查找该实例
         * 2. 检查玩家是否有绑定的实例
         * 3. 检查玩家的队伍是否有活跃的实例
         * 4. 如果都没有，创建新的实例
         *
         * 调用时机：
         * - 玩家传送进入可实例化地图时
         * - 玩家登录时恢复到上次所在的实例
         *
         * 性能注意事项：
         * - 可能需要查询数据库获取实例存档信息
         * - 创建新实例可能需要初始化大量数据
         */
        Map* CreateInstanceForPlayer(uint32 mapId, Player* player, uint32 loginInstanceId = 0);

        /**
         * @brief 查找指定实例ID的实例地图
         * @param instanceId 实例ID
         * @return 指向实例地图的指针，不存在返回 nullptr
         *
         * 快速查找函数，从实例映射中获取指定的实例地图。
         *
         * 调用时机：
         * - 玩家尝试进入已知实例ID的地图时
         * - 需要访问特定实例时
         *
         * 性能注意事项：
         * - O(1) 时间复杂度（哈希表查找）
         */
        Map* FindInstanceMap(uint32 instanceId) const
        {
            InstancedMaps::const_iterator i = m_InstancedMaps.find(instanceId);
            return(i == m_InstancedMaps.end() ? nullptr : i->second.get());
        }

        /**
         * @brief 销毁指定的实例地图
         * @param itr 指向实例映射中要销毁的实例的迭代器
         * @return 成功返回 true，失败返回 false
         *
         * 卸载并销毁一个实例地图及其所有资源。
         * 只有在实例中没有玩家时才能销毁。
         *
         * 调用时机：
         * - 实例重置时
         * - 所有玩家离开实例一段时间后
         * - 手动触发实例销毁时
         *
         * 性能注意事项：
         * - 需要卸载所有网格和对象，可能耗时较长
         */
        bool DestroyInstance(InstancedMaps::iterator &itr);

        /**
         * @brief 增加网格地图引用计数
         * @param p 网格坐标
         *
         * 当有新的实例使用该网格时，增加引用计数。
         * 引用计数用于跟踪有多少个实例在使用同一网格数据。
         *
         * 工作原理：
         * - 增加指定网格的引用计数
         * - 设置对应对称网格的卸载锁，防止被卸载
         *
         * 调用时机：
         * - 实例地图加载新网格时
         *
         * 性能注意事项：
         * - O(1) 操作，非常快速
         */
        void AddGridMapReference(GridCoord const& p)
        {
            ++GridMapReference[p.x_coord][p.y_coord];
            // 设置对称位置的卸载锁（用于网格卸载优化）
            SetUnloadReferenceLock(GridCoord((MAX_NUMBER_OF_GRIDS - 1) - p.x_coord, (MAX_NUMBER_OF_GRIDS - 1) - p.y_coord), true);
        }

        /**
         * @brief 减少网格地图引用计数
         * @param p 网格坐标
         *
         * 当有实例卸载该网格时，减少引用计数。
         * 如果引用计数降为0，允许卸载该网格数据。
         *
         * 工作原理：
         * - 减少指定网格的引用计数
         * - 如果计数为0，解除对应对称网格的卸载锁
         *
         * 调用时机：
         * - 实例地图卸载网格时
         *
         * 性能注意事项：
         * - O(1) 操作，非常快速
         */
        void RemoveGridMapReference(GridCoord const& p)
        {
            --GridMapReference[p.x_coord][p.y_coord];
            if (!GridMapReference[p.x_coord][p.y_coord])
                SetUnloadReferenceLock(GridCoord((MAX_NUMBER_OF_GRIDS - 1) - p.x_coord, (MAX_NUMBER_OF_GRIDS - 1) - p.y_coord), false);
        }

        /**
         * @brief 获取所有实例地图的映射
         * @return 实例地图映射的引用
         *
         * 用于遍历和管理所有实例地图。
         *
         * 调用时机：
         * - 需要遍历所有实例时
         * - 管理实例生命周期时
         */
        InstancedMaps &GetInstancedMaps() { return m_InstancedMaps; }

        /**
         * @brief 初始化可见距离
         *
         * 重写基类 Map 的 InitVisibilityDistance 方法。
         * 根据地图类型设置合适的可见距离。
         *
         * 调用时机：
         * - 地图对象创建时
         */
        virtual void InitVisibilityDistance() override;

    private:
        /**
         * @brief 创建新的实例地图（地下城或团队副本）
         * @param InstanceId 新实例的ID
         * @param save 实例存档对象（可为 nullptr）
         * @param difficulty 实例难度
         * @param InstanceTeam 实例所属队伍（联盟或部落）
         * @return 指向新创建的 InstanceMap 的指针
         *
         * 创建一个新的 InstanceMap 对象，用于地下城或团队副本。
         *
         * 处理流程：
         * 1. 创建 InstanceMap 对象
         * 2. 初始化实例数据
         * 3. 加载实例脚本
         * 4. 添加到实例映射中
         *
         * 调用时机：
         * - CreateInstanceForPlayer 确定需要创建新实例时
         *
         * 性能注意事项：
         * - 需要初始化大量数据，可能耗时较长
         */
        InstanceMap* CreateInstance(uint32 InstanceId, InstanceSave* save, Difficulty difficulty, TeamId InstanceTeam);

        /**
         * @brief 创建新的战场地图
         * @param InstanceId 新实例的ID
         * @param bg 战场对象指针
         * @return 指向新创建的 BattlegroundMap 的指针
         *
         * 创建一个新的 BattlegroundMap 对象，用于战场或竞技场。
         *
         * 处理流程：
         * 1. 创建 BattlegroundMap 对象
         * 2. 关联战场对象
         * 3. 添加到实例映射中
         *
         * 调用时机：
         * - 战场队列系统创建新战场时
         *
         * 性能注意事项：
         * - 相对轻量级的操作
         */
        BattlegroundMap* CreateBattleground(uint32 InstanceId, Battleground* bg);

        /**
         * @brief 实例地图映射
         *
         * 存储该地图的所有实例副本。
         * 键：实例ID
         * 值：指向实例地图的智能指针
         */
        InstancedMaps m_InstancedMaps;

        /**
         * @brief 网格地图引用计数数组
         *
         * 跟踪每个网格被多少个实例使用。
         * 用于共享网格数据，避免重复加载。
         *
         * 大小：MAX_NUMBER_OF_GRIDS x MAX_NUMBER_OF_GRIDS（通常为 64x64）
         */
        uint16 GridMapReference[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];
};
#endif
