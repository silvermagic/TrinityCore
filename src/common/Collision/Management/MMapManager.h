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
 * @file MMapManager.h
 * @brief MMap（Move Map）管理器定义
 *
 * 本文件定义了MMap系统的核心管理类，负责导航网格数据的加载、管理和查询。
 * MMap系统基于Recast/Detour库实现，为游戏提供高效的寻路功能。
 *
 * 核心概念：
 * - dtNavMesh：Detour导航网格，存储地图的导航数据结构
 * - dtNavMeshQuery：导航网格查询对象，用于路径搜索等操作
 * - Tile（瓦片）：地图被分割成多个瓦片，支持按需加载
 * - Instance（实例）：同一地图可能有多个副本实例
 *
 * 文件格式：
 * - .mmap文件：存储地图的导航网格参数
 * - .mmtile文件：存储单个瓦片的导航数据
 *
 * 架构设计：
 * - MMapData：每个地图的导航数据容器
 * - MMapManager：全局管理器，管理所有地图数据
 * - 每个地图实例有独立的dtNavMeshQuery以保证线程安全
 */

#ifndef _MMAP_MANAGER_H
#define _MMAP_MANAGER_H

#include "Define.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace MMAP
{
    /// 瓦片引用映射表：packedTileID -> dtTileRef
    typedef std::unordered_map<uint32, dtTileRef> MMapTileSet;

    /// 导航网格查询映射表：instanceId -> dtNavMeshQuery*
    typedef std::unordered_map<uint32, dtNavMeshQuery*> NavMeshQuerySet;

    /**
     * @struct MMapData
     * @brief 单个地图的MMap数据容器
     *
     * 存储单个地图的所有导航相关数据，包括：
     * - 导航网格对象（dtNavMesh）
     * - 已加载瓦片引用
     * - 各实例的查询对象
     *
     * 内存管理：
     * - 析构时自动释放所有dtNavMeshQuery和dtNavMesh
     * - 使用Detour的内存管理函数释放资源
     */
    struct TC_COMMON_API MMapData
    {
        /**
         * @brief 构造函数
         * @param mesh 导航网格指针，接管所有权
         */
        MMapData(dtNavMesh* mesh) : navMesh(mesh) { }

        /**
         * @brief 析构函数
         *
         * 释放所有导航网格查询对象和导航网格本身。
         * 使用Detour的内存释放函数确保正确释放。
         */
        ~MMapData()
        {
            // 释放所有实例的查询对象
            for (NavMeshQuerySet::iterator i = navMeshQueries.begin(); i != navMeshQueries.end(); ++i)
                dtFreeNavMeshQuery(i->second);

            // 释放导航网格
            if (navMesh)
                dtFreeNavMesh(navMesh);
        }

        /**
         * @brief 导航网格查询映射表
         *
         * 每个地图实例对应一个独立的查询对象。
         * 由于dtNavMeshQuery非线程安全，必须为每个实例创建独立查询对象。
         *
         * Key: 实例ID（InstanceId）
         * Value: dtNavMeshQuery指针
         */
        NavMeshQuerySet navMeshQueries;

        /**
         * @brief 导航网格对象
         *
         * 存储地图的导航网格数据，所有瓦片加载到此网格中。
         * 同一地图的所有实例共享同一个导航网格。
         */
        dtNavMesh* navMesh;

        /**
         * @brief 已加载瓦片引用映射
         *
         * 存储瓦片坐标到Detour瓦片引用的映射。
         * 用于瓦片的卸载和管理。
         *
         * Key: 打包的瓦片坐标（packTileID结果）
         * Value: Detour瓦片引用
         */
        MMapTileSet loadedTileRefs;
    };

    /// 地图数据映射表：mapId -> MMapData*
    typedef std::unordered_map<uint32, MMapData*> MMapDataSet;

    /**
     * @class MMapManager
     * @brief MMap系统核心管理器
     *
     * 单例类，负责所有导航地图数据的加载、卸载和查询。
     * 管理所有已加载地图的导航网格和查询对象。
     *
     * 主要职责：
     * 1. 地图数据生命周期管理（加载/卸载）
     * 2. 瓦片级别的按需加载
     * 3. 多实例支持（每个副本有独立查询对象）
     * 4. 提供导航网格和查询接口
     *
     * 线程安全说明：
     * - 默认模式：线程安全，支持运行时动态加载地图
     * - 优化模式：非线程安全，在初始化时预分配所有地图槽位
     *
     * 使用流程：
     * @code
     * MMapManager* manager = MMapFactory::createOrGetMMapManager();
     *
     * // 加载地图瓦片
     * manager->loadMap(basePath, mapId, tileX, tileY);
     *
     * // 为实例创建查询对象
     * manager->loadMapInstance(basePath, mapId, instanceId);
     *
     * // 获取查询对象进行寻路
     * dtNavMeshQuery const* query = manager->GetNavMeshQuery(mapId, instanceId);
     *
     * // 卸载
     * manager->unloadMapInstance(mapId, instanceId);
     * manager->unloadMap(mapId, tileX, tileY);
     * @endcode
     */
    class TC_COMMON_API MMapManager
    {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化管理器，默认启用线程安全模式。
             */
            MMapManager() : loadedTiles(0), thread_safe_environment(true) {}

            /**
             * @brief 析构函数
             *
             * 清理所有已加载的地图数据。
             */
            ~MMapManager();

            /**
             * @brief 初始化非线程安全模式
             *
             * @param mapIds 预期的地图ID列表
             *
             * 在启动时预分配所有地图槽位，避免运行时动态分配。
             * 启用此模式后，加载不在列表中的地图将导致错误。
             *
             * 调用时机：服务器启动时，在加载任何地图之前
             * 性能优化：避免运行时的哈希表扩容
             */
            void InitializeThreadUnsafe(const std::vector<uint32>& mapIds);

            /**
             * @brief 加载指定地图的单个瓦片
             *
             * @param basePath 数据文件基础路径
             * @param mapId 地图ID
             * @param x 瓦片X坐标（网格坐标）
             * @param y 瓦片Y坐标（网格坐标）
             * @return 加载成功返回true
             *
             * 加载流程：
             * 1. 确保地图数据已加载（loadMapData）
             * 2. 检查瓦片是否已加载
             * 3. 读取.mmtile文件
             * 4. 验证文件头和版本
             * 5. 将瓦片添加到导航网格
             *
             * 调用时机：玩家进入新区域、地图激活时
             * 性能注意事项：涉及文件I/O，不应在主游戏循环中调用
             */
            bool loadMap(std::string const& basePath, uint32 mapId, int32 x, int32 y);

            /**
             * @brief 为指定地图实例加载导航数据
             *
             * @param basePath 数据文件基础路径
             * @param mapId 地图ID
             * @param instanceId 实例ID
             * @return 加载成功返回true
             *
             * 为地图实例创建独立的导航查询对象。
             * 同一地图可以有多个实例，每个实例需要独立的查询对象。
             *
             * 调用时机：地图实例创建时
             */
            bool loadMapInstance(std::string const& basePath, uint32 mapId, uint32 instanceId);

            /**
             * @brief 卸载指定地图的单个瓦片
             *
             * @param mapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 卸载成功返回true
             *
             * 调用时机：玩家离开区域、地图卸载时
             */
            bool unloadMap(uint32 mapId, int32 x, int32 y);

            /**
             * @brief 卸载指定地图的所有瓦片
             *
             * @param mapId 地图ID
             * @return 卸载成功返回true
             *
             * 卸载指定地图的所有瓦片，释放MMapData但保留槽位。
             *
             * 调用时机：地图完全卸载时
             */
            bool unloadMap(uint32 mapId);

            /**
             * @brief 卸载指定地图实例的查询对象
             *
             * @param mapId 地图ID
             * @param instanceId 实例ID
             * @return 卸载成功返回true
             *
             * 调用时机：地图实例销毁时
             */
            bool unloadMapInstance(uint32 mapId, uint32 instanceId);

            /**
             * @brief 获取导航网格查询对象
             *
             * @param mapId 地图ID
             * @param instanceId 实例ID
             * @return 查询对象指针，未找到返回nullptr
             *
             * 返回的查询对象非线程安全，仅供单个实例使用。
             *
             * 调用时机：寻路计算时
             * 性能注意事项：高频调用，直接哈希查找
             */
            dtNavMeshQuery const* GetNavMeshQuery(uint32 mapId, uint32 instanceId);

            /**
             * @brief 获取导航网格对象
             *
             * @param mapId 地图ID
             * @return 导航网格指针，未找到返回nullptr
             *
             * 调用时机：需要直接访问导航网格数据时
             */
            dtNavMesh const* GetNavMesh(uint32 mapId);

            /**
             * @brief 获取已加载瓦片总数
             * @return 瓦片数量
             */
            uint32 getLoadedTilesCount() const { return loadedTiles; }

            /**
             * @brief 获取已加载地图数量
             * @return 地图数量（包括已卸载但保留槽位的地图）
             */
            uint32 getLoadedMapsCount() const { return uint32(loadedMMaps.size()); }

        private:
            /**
             * @brief 加载地图基础数据（.mmap文件）
             *
             * @param basePath 数据文件基础路径
             * @param mapId 地图ID
             * @return 加载成功返回true
             *
             * 加载导航网格参数文件，创建dtNavMesh对象。
             * 如果地图数据已存在则直接返回成功。
             */
            bool loadMapData(std::string const& basePath, uint32 mapId);

            /**
             * @brief 将瓦片坐标打包为单一ID
             *
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 打包后的32位ID
             *
             * 格式：高16位存储X，低16位存储Y
             */
            uint32 packTileID(int32 x, int32 y);

            /**
             * @brief 获取地图数据的迭代器
             *
             * @param mapId 地图ID
             * @return 迭代器，未找到返回end()
             *
             * 内部辅助函数，用于查找地图数据。
             */
            MMapDataSet::const_iterator GetMMapData(uint32 mapId) const;

            /// 已加载地图数据映射表
            MMapDataSet loadedMMaps;

            /// 已加载瓦片总数
            uint32 loadedTiles;

            /// 是否运行在线程安全环境
            bool thread_safe_environment;
    };
}

#endif
