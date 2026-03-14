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
 * @file MapBuilder.h
 * @brief 移动地图(MMap)生成器核心类定义
 *
 * 本头文件定义了导航网格生成器的核心类，包括:
 * - MapTiles: 地图瓦片信息结构
 * - Tile: 瓦片构建中间数据结构
 * - TileConfig: 瓦片配置参数
 * - TileInfo: 瓦片任务信息
 * - TileBuilder: 瓦片构建器(工作线程)
 * - MapBuilder: 地图构建器(主控制器)
 *
 * 设计模式:
 * - 生产者-消费者模式: MapBuilder生产任务，TileBuilder消费任务
 * - 多线程并行处理: 每个TileBuilder在独立线程中运行
 *
 * @see MapBuilder.cpp 实现文件
 */

#ifndef _MAP_BUILDER_H
#define _MAP_BUILDER_H

#include "TerrainBuilder.h"

#include "Recast.h"
#include "DetourNavMesh.h"
#include "Optional.h"
#include "ProducerConsumerQueue.h"

#include <vector>
#include <set>
#include <list>
#include <atomic>
#include <thread>

using namespace VMAP;

namespace MMAP
{
    /**
     * @struct MapTiles
     * @brief 地图瓦片信息结构
     *
     * 存储单个地图的ID及其所有瓦片ID的集合。
     * 用于在构建过程中跟踪每个地图需要处理的瓦片。
     */
    struct MapTiles
    {
        MapTiles() : m_mapId(uint32(-1)), m_tiles(nullptr) {}

        MapTiles(uint32 id, std::set<uint32>* tiles) : m_mapId(id), m_tiles(tiles) {}
        ~MapTiles() {}

        uint32 m_mapId;              ///< 地图ID
        std::set<uint32>* m_tiles;   ///< 该地图的瓦片ID集合(使用packTileID打包)

        /**
         * @brief 相等比较运算符
         * @param id 要比较的地图ID
         * @return 如果地图ID匹配则返回true
         */
        bool operator==(uint32 id) const
        {
            return m_mapId == id;
        }
    };

    typedef std::list<MapTiles> TileList;

    /**
     * @struct Tile
     * @brief 瓦片构建中间数据结构
     *
     * 存储单个瓦片在导航网格构建过程中生成的所有中间数据。
     * 这些数据按Recast处理管线的顺序依次生成和释放。
     */
    struct Tile
    {
        Tile() : chf(nullptr), solid(nullptr), cset(nullptr), pmesh(nullptr), dmesh(nullptr) {}
        ~Tile()
        {
            // 释放所有Recast数据结构
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreeHeightField(solid);
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
        }
        rcCompactHeightfield* chf;     ///< 紧凑高度场 - 高度场的优化表示
        rcHeightfield* solid;          ///< 高度场 - 体素网格表示
        rcContourSet* cset;            ///< 轮廓集 - 区域边界轮廓
        rcPolyMesh* pmesh;             ///< 多边形网格 - 最终导航网格
        rcPolyMeshDetail* dmesh;       ///< 细节网格 - 用于精确高度采样
    };

    /**
     * @struct TileConfig
     * @brief 瓦片配置参数结构
     *
     * 存储瓦片生成所需的各种尺寸参数。
     * 根据bigBaseUnit设置调整精度和性能的平衡。
     */
    struct TileConfig
    {
        TileConfig(bool bigBaseUnit)
        {
            // 这些是基于世界单位的度量
            // 基础单位尺寸必须能整除GRID_SIZE(533.3333f)
            // 可选值: 0.5333, 0.2666, 0.3333, 0.1333 等
            // these are WORLD UNIT based metrics
            // this are basic unit dimentions
            // value have to divide GRID_SIZE(533.3333f) ( aka: 0.5333, 0.2666, 0.3333, 0.1333, etc )
            BASE_UNIT_DIM = bigBaseUnit ? 0.5333333f : 0.2666666f;

            // 所有值都以单位度量计算
            // All are in UNIT metrics!
            VERTEX_PER_MAP = int(GRID_SIZE / BASE_UNIT_DIM + 0.5f);
            VERTEX_PER_TILE = bigBaseUnit ? 40 : 80;  // 必须能整除VERTEX_PER_MAP
            // must divide VERTEX_PER_MAP
            TILES_PER_MAP = VERTEX_PER_MAP / VERTEX_PER_TILE;
        }

        float BASE_UNIT_DIM;     ///< 基础单位尺寸(世界单位)
        int VERTEX_PER_MAP;      ///< 每个地图的顶点数
        int VERTEX_PER_TILE;     ///< 每个瓦片的顶点数
        int TILES_PER_MAP;       ///< 每个地图的瓦片数
    };

    /**
     * @struct TileInfo
     * @brief 瓦片任务信息结构
     *
     * 用于在生产者-消费者队列中传递瓦片构建任务。
     * 包含工作线程处理瓦片所需的所有信息。
     */
    struct TileInfo
    {
        TileInfo() : m_mapId(uint32(-1)), m_tileX(), m_tileY(), m_navMeshParams() {}

        uint32 m_mapId;                  ///< 地图ID
        uint32 m_tileX;                  ///< 瓦片X坐标(0-63)
        uint32 m_tileY;                  ///< 瓦片Y坐标(0-63)
        dtNavMeshParams m_navMeshParams; ///< 导航网格参数(用于初始化本地navMesh)
    };

    // ToDo: move this to its own file. For now it will stay here to keep the changes to a minimum, especially in the cpp file
    class MapBuilder;

    /**
     * @class TileBuilder
     * @brief 瓦片构建器类
     *
     * 负责实际执行导航网格瓦片的构建工作。
     * 每个TileBuilder运行在独立的工作线程中，从任务队列获取瓦片任务并处理。
     *
     * 主要职责:
     * - 从任务队列获取瓦片信息
     * - 加载地形和碰撞数据
     * - 执行Recast导航网格生成管线
     * - 输出.mmtile文件
     *
     * 线程安全:
     * - 每个TileBuilder实例独立工作，互不干扰
     * - 通过原子计数器更新进度
     */
    class TileBuilder
    {
        public:
            /**
             * @brief 构造函数
             * @param mapBuilder 父MapBuilder实例
             * @param skipLiquid 是否跳过液体处理
             * @param bigBaseUnit 是否使用大基础单位
             * @param debugOutput 是否输出调试信息
             */
            TileBuilder(MapBuilder* mapBuilder,
                bool skipLiquid,
                bool bigBaseUnit,
                bool debugOutput);

            TileBuilder(TileBuilder&&) = default;
            ~TileBuilder();

            /**
             * @brief 工作线程主循环
             *
             * 从队列获取任务并执行，直到收到取消信号。
             */
            void WorkerThread();

            /**
             * @brief 等待工作线程完成
             */
            void WaitCompletion();

            /**
             * @brief 构建单个瓦片的导航网格
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @param navMesh 导航网格实例
             */
            void buildTile(uint32 mapID, uint32 tileX, uint32 tileY, dtNavMesh* navMesh);

            /**
             * @brief 构建移动地图瓦片(核心生成函数)
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @param meshData 网格数据
             * @param bmin 最小边界
             * @param bmax 最大边界
             * @param navMesh 导航网格实例
             */
            // move map building
            void buildMoveMapTile(uint32 mapID,
                uint32 tileX,
                uint32 tileY,
                MeshData& meshData,
                float bmin[3],
                float bmax[3],
                dtNavMesh* navMesh);

            /**
             * @brief 判断是否应跳过瓦片
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @return 如果瓦片已存在且有效则返回true
             */
            bool shouldSkipTile(uint32 mapID, uint32 tileX, uint32 tileY) const;

        private:
            bool m_bigBaseUnit;              ///< 是否使用大基础单位
            bool m_debugOutput;              ///< 是否输出调试信息

            MapBuilder* m_mapBuilder;        ///< 父MapBuilder指针
            TerrainBuilder* m_terrainBuilder; ///< 地形构建器
            std::thread m_workerThread;      ///< 工作线程
            // build performance - not really used for now
            rcContext* m_rcContext;          ///< Recast上下文(用于性能分析)
    };

    /**
     * @class MapBuilder
     * @brief 地图构建器主控制器类
     *
     * 负责协调整个导航网格生成过程，包括:
     * - 发现和管理地图瓦片信息
     * - 创建和管理工作线程(TileBuilder)
     * - 分发瓦片构建任务
     * - 跟踪生成进度
     *
     * 设计模式:
     * - 生产者: 将瓦片任务推送到队列
     * - 控制器: 管理工作线程生命周期
     *
     * 使用流程:
     * 1. 构造时调用discoverTiles()发现所有地图和瓦片
     * 2. 调用buildMaps()开始生成过程
     * 3. 创建TileBuilder工作线程
     * 4. 将瓦片任务推送到队列
     * 5. 等待所有任务完成
     * 6. 析构时清理资源
     */
    class MapBuilder
    {
        friend class TileBuilder;

        public:
            /**
             * @brief 构造函数
             * @param maxWalkableAngle 最大可行走坡度角度(可选)
             * @param maxWalkableAngleNotSteep 非陡峭可行走角度(可选)
             * @param skipLiquid 是否跳过液体处理
             * @param skipContinents 是否跳过大陆地图
             * @param skipJunkMaps 是否跳过垃圾/测试地图
             * @param skipBattlegrounds 是否跳过战场地图
             * @param debugOutput 是否输出调试信息
             * @param bigBaseUnit 是否使用大基础单位
             * @param mapid 指定地图ID(-1表示所有地图)
             * @param offMeshFilePath OffMesh连接文件路径
             * @param threads 工作线程数量
             */
            MapBuilder(Optional<float> maxWalkableAngle,
                Optional<float> maxWalkableAngleNotSteep,
                bool skipLiquid,
                bool skipContinents,
                bool skipJunkMaps,
                bool skipBattlegrounds,
                bool debugOutput,
                bool bigBaseUnit,
                int mapid,
                char const* offMeshFilePath,
                unsigned int threads);

            ~MapBuilder();

            /**
             * @brief 从文件构建导航网格
             * @param name 文件路径
             */
            void buildMeshFromFile(char* name);

            /**
             * @brief 构建单个瓦片
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             */
            // builds an mmap tile for the specified map and its mesh
            void buildSingleTile(uint32 mapID, uint32 tileX, uint32 tileY);

            /**
             * @brief 构建所有或指定地图
             * @param mapID 可选的地图ID，为空则构建所有地图
             */
            // builds list of maps, then builds all of mmap tiles (based on the skip settings)
            void buildMaps(Optional<uint32> mapID);

        private:
            /**
             * @brief 构建指定地图的所有瓦片
             * @param mapID 地图ID
             */
            // builds all mmap tiles for the specified map id (ignores skip settings)
            void buildMap(uint32 mapID);

            /**
             * @brief 发现所有地图和瓦片
             */
            // detect maps and tiles
            void discoverTiles();

            /**
             * @brief 获取指定地图的瓦片列表
             * @param mapID 地图ID
             * @return 瓦片ID集合指针
             */
            std::set<uint32>* getTileList(uint32 mapID);

            /**
             * @brief 创建导航网格实例
             * @param mapID 地图ID
             * @param navMesh [out] 导航网格指针
             */
            void buildNavMesh(uint32 mapID, dtNavMesh* &navMesh);

            /**
             * @brief 获取瓦片边界
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @param verts 顶点数组
             * @param vertCount 顶点数量
             * @param bmin [out] 最小边界
             * @param bmax [out] 最大边界
             */
            void getTileBounds(uint32 tileX, uint32 tileY,
                float* verts, int vertCount,
                float* bmin, float* bmax) const;

            /**
             * @brief 获取地图网格边界
             * @param mapID 地图ID
             * @param minX [out] 最小X坐标
             * @param minY [out] 最小Y坐标
             * @param maxX [out] 最大X坐标
             * @param maxY [out] 最大Y坐标
             */
            void getGridBounds(uint32 mapID, uint32 &minX, uint32 &minY, uint32 &maxX, uint32 &maxY) const;

            /**
             * @brief 判断是否应跳过地图
             * @param mapID 地图ID
             * @return 如果应跳过则返回true
             */
            bool shouldSkipMap(uint32 mapID) const;

            /**
             * @brief 判断是否为运输工具地图
             * @param mapID 地图ID
             * @return 如果是运输工具地图则返回true
             */
            bool isTransportMap(uint32 mapID) const;

            /**
             * @brief 判断是否为大陆地图
             * @param mapID 地图ID
             * @return 如果是大陆地图则返回true
             */
            bool isContinentMap(uint32 mapID) const;

            /**
             * @brief 获取地图特定的Recast配置
             * @param mapID 地图ID
             * @param bmin 最小边界
             * @param bmax 最大边界
             * @param tileConfig 瓦片配置
             * @return Recast配置结构
             */
            rcConfig GetMapSpecificConfig(uint32 mapID, float bmin[3], float bmax[3], const TileConfig &tileConfig) const;

            /**
             * @brief 计算完成百分比
             * @param totalTiles 总瓦片数
             * @param totalTilesDone 已完成瓦片数
             * @return 完成百分比
             */
            uint32 percentageDone(uint32 totalTiles, uint32 totalTilesDone) const;

            /**
             * @brief 获取当前完成百分比
             * @return 当前完成百分比
             */
            uint32 currentPercentageDone() const;

            TerrainBuilder* m_terrainBuilder;      ///< 地形构建器
            TileList m_tiles;                       ///< 地图瓦片列表

            bool m_debugOutput;                     ///< 调试输出标志

            char const* m_offMeshFilePath;          ///< OffMesh连接文件路径
            unsigned int m_threads;                 ///< 工作线程数量
            bool m_skipContinents;                  ///< 跳过大陆地图标志
            bool m_skipJunkMaps;                    ///< 跳过垃圾地图标志
            bool m_skipBattlegrounds;               ///< 跳过战场标志
            bool m_skipLiquid;                      ///< 跳过液体标志

            Optional<float> m_maxWalkableAngle;             ///< 最大可行走角度
            Optional<float> m_maxWalkableAngleNotSteep;     ///< 非陡峭可行走角度
            bool m_bigBaseUnit;                             ///< 大基础单位标志

            int32 m_mapid;                          ///< 指定地图ID

            std::atomic<uint32> m_totalTiles;           ///< 总瓦片计数(原子变量)
            std::atomic<uint32> m_totalTilesProcessed;  ///< 已处理瓦片计数(原子变量)

            // build performance - not really used for now
            rcContext* m_rcContext;                 ///< Recast上下文

            std::vector<TileBuilder*> m_tileBuilders;   ///< 瓦片构建器数组
            ProducerConsumerQueue<TileInfo> _queue;     ///< 任务队列
            std::atomic<bool> _cancelationToken;        ///< 取消令牌(原子变量)
    };
}

#endif
