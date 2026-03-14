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
 * @file MapManager.h
 * @brief 地图管理器头文件
 *
 * 本模块实现了游戏世界的地图管理系统，作为所有地图的全局管理器。
 *
 * 主要职责：
 * - 管理所有地图实例的生命周期
 * - 创建和销毁地图对象
 * - 协调地图的更新和卸载
 * - 管理实例ID的分配和回收
 * - 提供全局地图访问接口
 * - 管理多线程地图更新
 *
 * 设计模式：
 * - 单例模式：全局唯一实例，通过 sMapMgr 宏访问
 * - 工厂模式：负责创建各种类型的地图对象
 * - 线程安全：使用互斥锁保护共享数据
 *
 * 核心功能：
 * - 地图创建：CreateBaseMap, CreateMap
 * - 地图查找：FindMap, FindBaseMap
 * - 地图更新：Update, DoDelayedMovesAndRemoves
 * - 实例管理：GenerateInstanceId, FreeInstanceId
 * - 遍历接口：DoForAllMaps, DoForAllMapsWithMapId
 *
 * 使用场景：
 * - 玩家传送时查找或创建目标地图
 * - 世界更新时更新所有地图
 * - 服务器启动时初始化地图系统
 * - 服务器关闭时清理所有地图资源
 *
 * 性能考虑：
 * - 使用多线程并行更新地图（MapUpdater）
 * - 实例ID使用位图管理，分配和回收高效
 * - 按需创建地图，避免不必要的内存占用
 */

#ifndef TRINITY_MAPMANAGER_H
#define TRINITY_MAPMANAGER_H

#include "Object.h"
#include "Map.h"
#include "MapInstanced.h"
#include "GridStates.h"
#include "MapUpdater.h"
#include "UniqueTrackablePtr.h"
#include <boost/dynamic_bitset.hpp>

class Transport;
struct TransportCreatureProto;

/**
 * @class MapManager
 * @brief 地图管理器类
 *
 * 地图管理器是整个地图系统的核心，负责管理所有地图实例的生命周期。
 * 使用单例模式，全局只有一个实例。
 *
 * 职责：
 * - 管理所有地图对象（包括普通地图和实例地图）
 * - 分配和回收实例ID
 * - 协调地图的多线程更新
 * - 提供地图查找和创建接口
 * - 管理网格清理和卸载策略
 *
 * 线程安全：
 * - 使用 _mapsLock 互斥锁保护地图映射
 * - 使用原子计数器管理脚本调度
 *
 * 访问方式：
 * - 通过 sMapMgr 宏访问全局单例实例
 */
class TC_GAME_API MapManager
{
    public:
        /**
         * @brief 获取单例实例
         * @return MapManager 单例指针
         *
         * 使用惰性初始化创建全局唯一的 MapManager 实例。
         *
         * 调用时机：
         * - 任何需要访问地图管理器的地方
         */
        static MapManager* instance();

        /**
         * @brief 创建或获取基础地图
         * @param mapId 地图ID
         * @return 指向基础地图的指针
         *
         * 创建或返回已存在的基础地图对象。
         * 对于可实例化的地图，返回 MapInstanced 对象。
         * 对于普通地图，返回普通 Map 对象。
         *
         * 处理流程：
         * 1. 检查地图是否已存在
         * 2. 如果不存在，创建新的地图对象
         * 3. 根据地图类型创建 Map 或 MapInstanced
         *
         * 调用时机：
         * - 需要访问地图的地形数据时
         * - 查询区域、高度等信息时
         * - 创建实例地图前
         *
         * 性能注意事项：
         * - 首次创建地图可能需要加载配置数据
         * - 使用锁保护，避免并发创建问题
         */
        Map* CreateBaseMap(uint32 mapId);

        /**
         * @brief 查找基础非实例地图
         * @param mapId 地图ID
         * @return 指向地图的指针，不存在返回 nullptr
         *
         * 查找非实例化的基础地图（如艾泽拉斯大陆）。
         * 如果地图是可实例化的，返回 nullptr。
         *
         * 调用时机：
         * - 需要访问非实例地图时
         */
        Map* FindBaseNonInstanceMap(uint32 mapId) const;

        /**
         * @brief 为玩家创建或查找地图
         * @param mapId 地图ID
         * @param player 请求的玩家
         * @param loginInstanceId 登录时的实例ID（可选）
         * @return 指向地图的指针
         *
         * 为玩家创建或查找合适的地图实例。
         * 这是玩家进入地图的主要接口。
         *
         * 处理流程：
         * 1. 获取基础地图
         * 2. 如果是普通地图，直接返回基础地图
         * 3. 如果是可实例化地图，调用 MapInstanced::CreateInstanceForPlayer
         *
         * 调用时机：
         * - 玩家传送进入地图时
         * - 玩家登录时
         *
         * 性能注意事项：
         * - 可能创建新的实例地图，耗时较长
         */
        Map* CreateMap(uint32 mapId, Player* player, uint32 loginInstanceId=0);

        /**
         * @brief 查找指定地图和实例的地图对象
         * @param mapId 地图ID
         * @param instanceId 实例ID（0表示基础地图）
         * @return 指向地图的指针，不存在返回 nullptr
         *
         * 快速查找特定实例的地图对象。
         *
         * 调用时机：
         * - 需要访问已知地图和实例ID的对象时
         *
         * 性能注意事项：
         * - O(1) 时间复杂度（哈希表查找）
         */
        Map* FindMap(uint32 mapId, uint32 instanceId) const;

        /**
         * @brief 获取区域ID
         * @param phaseMask 相位掩码
         * @param mapid 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @return 区域ID
         *
         * 便捷函数，通过坐标获取区域ID。
         */
        uint32 GetAreaId(uint32 phaseMask, uint32 mapid, float x, float y, float z) const
        {
            Map const* m = const_cast<MapManager*>(this)->CreateBaseMap(mapid);
            return m->GetAreaId(phaseMask, x, y, z);
        }
        uint32 GetAreaId(uint32 phaseMask, uint32 mapid, Position const& pos) const { return GetAreaId(phaseMask, mapid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        uint32 GetAreaId(uint32 phaseMask, WorldLocation const& loc) const { return GetAreaId(phaseMask, loc.GetMapId(), loc); }

        /**
         * @brief 获取子区域ID
         * @param phaseMask 相位掩码
         * @param mapid 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @return 子区域ID
         *
         * 便捷函数，通过坐标获取子区域ID。
         */
        uint32 GetZoneId(uint32 phaseMask, uint32 mapid, float x, float y, float z) const
        {
            Map const* m = const_cast<MapManager*>(this)->CreateBaseMap(mapid);
            return m->GetZoneId(phaseMask, x, y, z);
        }
        uint32 GetZoneId(uint32 phaseMask, uint32 mapid, Position const& pos) const { return GetZoneId(phaseMask, mapid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        uint32 GetZoneId(uint32 phaseMask, WorldLocation const& loc) const { return GetZoneId(phaseMask, loc.GetMapId(), loc); }

        /**
         * @brief 获取区域和子区域ID
         * @param phaseMask 相位掩码
         * @param zoneid 输出：子区域ID
         * @param areaid 输出：区域ID
         * @param mapid 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         *
         * 便捷函数，同时获取区域和子区域ID。
         */
        void GetZoneAndAreaId(uint32 phaseMask, uint32& zoneid, uint32& areaid, uint32 mapid, float x, float y, float z) const
        {
            Map const* m = const_cast<MapManager*>(this)->CreateBaseMap(mapid);
            m->GetZoneAndAreaId(phaseMask, zoneid, areaid, x, y, z);
        }
        void GetZoneAndAreaId(uint32 phaseMask, uint32& zoneid, uint32& areaid, uint32 mapid, Position const& pos) const { GetZoneAndAreaId(phaseMask, zoneid, areaid, mapid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()); }
        void GetZoneAndAreaId(uint32 phaseMask, uint32& zoneid, uint32& areaid, WorldLocation const& loc) const { GetZoneAndAreaId(phaseMask, zoneid, areaid, loc.GetMapId(), loc); }

        /**
         * @brief 初始化地图管理器
         *
         * 在服务器启动时调用，初始化地图系统。
         *
         * 处理流程：
         * 1. 初始化网格清理延迟
         * 2. 初始化地图更新间隔
         * 3. 初始化实例ID管理器
         * 4. 创建地图更新器
         *
         * 调用时机：
         * - World::SetInitialWorldSettings 中调用
         */
        void Initialize(void);

        /**
         * @brief 更新所有地图
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 每个世界更新周期调用，更新所有地图的状态。
         *
         * 处理流程：
         * 1. 更新计时器
         * 2. 遍历所有地图并更新
         * 3. 清理过期网格
         *
         * 调用时机：
         * - World::Update 中调用
         *
         * 性能注意事项：
         * - 这是主要的性能瓶颈之一
         * - 使用多线程并行更新（MapUpdater）
         */
        void Update(uint32);

        /**
         * @brief 设置网格清理延迟
         * @param t 延迟时间（毫秒）
         *
         * 设置网格在卸载前等待的时间。
         * 防止频繁加载卸载造成的性能问题。
         *
         * 调用时机：
         * - 加载配置时
         */
        void SetGridCleanUpDelay(uint32 t)
        {
            if (t < MIN_GRID_DELAY)
                i_gridCleanUpDelay = MIN_GRID_DELAY;
            else
                i_gridCleanUpDelay = t;
        }

        /**
         * @brief 设置地图更新间隔
         * @param t 更新间隔（毫秒）
         *
         * 设置地图更新的时间间隔。
         * 较小的间隔提供更高的精度但消耗更多CPU。
         *
         * 调用时机：
         * - 加载配置时
         */
        void SetMapUpdateInterval(uint32 t)
        {
            if (t < MIN_MAP_UPDATE_DELAY)
                t = MIN_MAP_UPDATE_DELAY;

            i_timer.SetInterval(t);
            i_timer.Reset();
        }

        /**
         * @brief 卸载所有地图
         *
         * 卸载所有地图的所有网格和资源。
         *
         * 调用时机：
         * - 服务器关闭时
         */
        void UnloadAll();

        /**
         * @brief 检查地图文件是否存在
         * @param mapid 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @return 存在返回 true，否则返回 false
         *
         * 静态函数，检查指定位置的地图文件和虚拟地图文件是否存在。
         */
        static bool ExistMapAndVMap(uint32 mapid, float x, float y);

        /**
         * @brief 检查地图ID是否有效
         * @param mapid 地图ID
         * @param startUp 是否为启动时检查
         * @return 有效返回 true，否则返回 false
         *
         * 静态函数，验证地图ID是否在有效范围内。
         */
        static bool IsValidMAP(uint32 mapid, bool startUp);

        /**
         * @brief 验证地图坐标是否有效
         * @param mapid 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @return 有效返回 true，否则返回 false
         *
         * 静态函数，验证地图ID和坐标是否在有效范围内。
         */
        static bool IsValidMapCoord(uint32 mapid, float x, float y)
        {
            return IsValidMAP(mapid, false) && Trinity::IsValidMapCoord(x, y);
        }

        static bool IsValidMapCoord(uint32 mapid, float x, float y, float z)
        {
            return IsValidMAP(mapid, false) && Trinity::IsValidMapCoord(x, y, z);
        }

        static bool IsValidMapCoord(uint32 mapid, float x, float y, float z, float o)
        {
            return IsValidMAP(mapid, false) && Trinity::IsValidMapCoord(x, y, z, o);
        }

        static bool IsValidMapCoord(uint32 mapid, Position const& pos)
        {
            return IsValidMapCoord(mapid, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation());
        }

        static bool IsValidMapCoord(WorldLocation const& loc)
        {
            return IsValidMapCoord(loc.GetMapId(), loc);
        }

        /**
         * @brief 执行延迟的移动和移除操作
         *
         * 处理所有地图的延迟操作队列。
         *
         * 调用时机：
         * - 每次世界更新后
         */
        void DoDelayedMovesAndRemoves();

        /**
         * @brief 检查玩家是否可以进入指定地图
         * @param mapid 地图ID
         * @param player 玩家指针
         * @param loginCheck 是否为登录检查
         * @return 进入状态枚举值
         *
         * 验证玩家是否满足进入地图的条件。
         *
         * 检查内容：
         * - 地图是否存在
         * - 玩家等级是否满足要求
         * - 实例人数限制
         * - 队伍要求
         * - 实例绑定状态
         *
         * 调用时机：
         * - 玩家尝试传送时
         * - 玩家登录时
         */
        Map::EnterState PlayerCannotEnter(uint32 mapid, Player* player, bool loginCheck = false);

        /**
         * @brief 初始化所有地图的可见距离信息
         *
         * 根据配置设置所有地图的可见距离。
         *
         * 调用时机：
         * - 服务器启动时
         */
        void InitializeVisibilityDistanceInfo();

        /**
         * @brief 获取实例数量
         * @return 当前活跃的实例数量
         *
         * 统计所有实例地图的数量。
         */
        uint32 GetNumInstances();

        /**
         * @brief 获取实例中的玩家数量
         * @return 所有实例地图中的玩家总数
         *
         * 统计所有实例地图中的玩家数量。
         */
        uint32 GetNumPlayersInInstances();

        /**
         * @brief 初始化实例ID管理器
         *
         * 初始化实例ID位图和计数器。
         *
         * 调用时机：
         * - MapManager::Initialize 中调用
         */
        void InitInstanceIds();

        /**
         * @brief 生成新的实例ID
         * @return 新的实例ID
         *
         * 从可用ID池中分配一个新的实例ID。
         *
         * 性能注意事项：
         * - 使用位图管理，分配和回收都是 O(1)
         */
        uint32 GenerateInstanceId();

        /**
         * @brief 注册实例ID
         * @param instanceId 要注册的实例ID
         *
         * 标记指定的实例ID为已使用。
         * 用于加载已有实例时。
         */
        void RegisterInstanceId(uint32 instanceId);

        /**
         * @brief 释放实例ID
         * @param instanceId 要释放的实例ID
         *
         * 回收实例ID，使其可以被重新分配。
         *
         * 调用时机：
         * - 实例地图销毁时
         */
        void FreeInstanceId(uint32 instanceId);

        /**
         * @brief 获取地图更新器
         * @return 地图更新器指针
         *
         * 用于多线程地图更新。
         */
        MapUpdater * GetMapUpdater() { return &m_updater; }

        /**
         * @brief 对所有地图执行操作
         * @param worker 工作函数对象
         *
         * 遍历所有地图（包括实例）并执行指定操作。
         *
         * 使用示例：
         * sMapMgr->DoForAllMaps([](Map* map) {
         *     // 对每个地图执行操作
         * });
         *
         * 性能注意事项：
         * - 需要锁保护，避免并发修改
         * - 操作应尽量快速，避免阻塞
         */
        template<typename Worker>
        void DoForAllMaps(Worker&& worker);

        /**
         * @brief 对指定地图ID的所有实例执行操作
         * @param mapId 地图ID
         * @param worker 工作函数对象
         *
         * 遍历指定地图ID的所有实例并执行操作。
         *
         * 使用示例：
         * sMapMgr->DoForAllMapsWithMapId(248, [](Map* map) {
         *     // 对所有纳克萨玛斯实例执行操作
         * });
         */
        template<typename Worker>
        void DoForAllMapsWithMapId(uint32 mapId, Worker&& worker);

        /**
         * @brief 增加计划脚本计数
         *
         * 原子操作，增加计划执行的脚本数量。
         */
        void IncreaseScheduledScriptsCount() { ++_scheduledScripts; }

        /**
         * @brief 减少计划脚本计数
         *
         * 原子操作，减少计划执行的脚本数量。
         */
        void DecreaseScheduledScriptCount() { --_scheduledScripts; }

        /**
         * @brief 批量减少计划脚本计数
         * @param count 要减少的数量
         *
         * 原子操作，批量减少计划执行的脚本数量。
         */
        void DecreaseScheduledScriptCount(std::size_t count) { _scheduledScripts -= count; }

        /**
         * @brief 检查是否有计划脚本
         * @return 有计划脚本返回 true，否则返回 false
         *
         * 用于判断是否还有脚本等待执行。
         */
        bool IsScriptScheduled() const { return _scheduledScripts > 0; }

    private:
        /**
         * @typedef MapMapType
         * @brief 地图映射类型定义
         *
         * 键：地图ID
         * 值：指向地图对象的智能指针
         */
        typedef std::unordered_map<uint32, Trinity::unique_trackable_ptr<Map>> MapMapType;

        /**
         * @typedef InstanceIds
         * @brief 实例ID位图类型定义
         *
         * 使用位图管理实例ID的分配状态。
         * 每一位代表一个实例ID是否已被使用。
         */
        typedef boost::dynamic_bitset<size_t> InstanceIds;

        /**
         * @brief 私有构造函数
         *
         * 单例模式，构造函数私有化。
         * 初始化成员变量。
         */
        MapManager();

        /**
         * @brief 析构函数
         *
         * 清理所有地图资源。
         */
        ~MapManager();

        /**
         * @brief 查找基础地图
         * @param mapId 地图ID
         * @return 指向地图的指针，不存在返回 nullptr
         *
         * 私有函数，从地图映射中查找基础地图。
         */
        Map* FindBaseMap(uint32 mapId) const
        {
            MapMapType::const_iterator iter = i_maps.find(mapId);
            return (iter == i_maps.end() ? nullptr : iter->second.get());
        }

        // 禁止拷贝构造
        MapManager(MapManager const&) = delete;
        // 禁止赋值操作
        MapManager& operator=(MapManager const&) = delete;

        std::mutex _mapsLock;                   ///< 地图映射锁，保护 i_maps 的并发访问
        uint32 i_gridCleanUpDelay;              ///< 网格清理延迟（毫秒），网格在卸载前等待的时间
        MapMapType i_maps;                      ///< 地图映射，存储所有基础地图
        IntervalTimer i_timer;                  ///< 地图更新计时器，控制更新频率

        InstanceIds _freeInstanceIds;           ///< 实例ID位图，标记哪些ID已被使用
        uint32 _nextInstanceId;                 ///< 下一个可用的实例ID
        MapUpdater m_updater;                   ///< 地图更新器，用于多线程更新

        std::atomic<std::size_t> _scheduledScripts;  ///< 原子计数器，跟踪计划执行的脚本数量
};

/**
 * @brief 对所有地图执行操作的模板实现
 * @param worker 工作函数对象
 *
 * 遍历所有地图（包括基础地图和实例地图）并执行指定操作。
 *
 * 工作流程：
 * 1. 锁定地图映射
 * 2. 遍历所有基础地图
 * 3. 如果是可实例化地图，遍历其所有实例
 * 4. 对每个地图执行 worker 操作
 */
template<typename Worker>
void MapManager::DoForAllMaps(Worker&& worker)
{
    std::lock_guard<std::mutex> lock(_mapsLock);

    for (auto& mapPair : i_maps)
    {
        Map* map = mapPair.second.get();
        // 检查是否为可实例化地图
        if (MapInstanced* mapInstanced = map->ToMapInstanced())
        {
            // 遍历该地图的所有实例
            MapInstanced::InstancedMaps& instances = mapInstanced->GetInstancedMaps();
            for (auto& instancePair : instances)
                worker(instancePair.second.get());
        }
        else
        {
            // 普通地图，直接执行操作
            worker(map);
        }
    }
}

/**
 * @brief 对指定地图ID的所有实例执行操作的模板实现
 * @param mapId 地图ID
 * @param worker 工作函数对象
 *
 * 遍历指定地图ID的所有实例并执行操作。
 *
 * 工作流程：
 * 1. 锁定地图映射
 * 2. 查找指定ID的基础地图
 * 3. 如果是可实例化地图，遍历其所有实例
 * 4. 对每个地图执行 worker 操作
 */
template<typename Worker>
inline void MapManager::DoForAllMapsWithMapId(uint32 mapId, Worker&& worker)
{
    std::lock_guard<std::mutex> lock(_mapsLock);

    auto itr = i_maps.find(mapId);
    if (itr != i_maps.end())
    {
        Map* map = itr->second.get();
        // 检查是否为可实例化地图
        if (MapInstanced* mapInstanced = map->ToMapInstanced())
        {
            // 遍历该地图的所有实例
            MapInstanced::InstancedMaps& instances = mapInstanced->GetInstancedMaps();
            for (auto& p : instances)
                worker(p.second.get());
        }
        else
        {
            // 普通地图，直接执行操作
            worker(map);
        }
    }
}

/**
 * @def sMapMgr
 * @brief 地图管理器全局访问宏
 *
 * 提供便捷的全局访问方式。
 * 使用方式：sMapMgr->CreateMap(...)
 */
#define sMapMgr MapManager::instance()
#endif
