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
 * @file MapBuilder.cpp
 * @brief 移动地图(MMap)生成器核心实现
 *
 * 本模块是TrinityCore服务器寻路系统的核心组件，负责生成服务器端的
 * 导航网格数据(NavMesh)，用于生物和玩家的寻路计算。
 *
 * 主要功能:
 * 1. 从地图文件(.map)和虚拟地图文件(.vmap)读取地形和碰撞数据
 * 2. 使用Recast库构建导航网格高度场
 * 3. 生成多边形网格和细节网格
 * 4. 输出.mmap和.mmtile文件供服务器运行时使用
 *
 * 技术架构:
 * - 使用多线程并行处理不同的地图瓦片
 * - 基于Recast/Detour库实现导航网格生成
 * - 支持陆地、液体(水面)和自定义OffMesh连接
 *
 * 导航网格区域类型:
 * - NAV_AREA_GROUND: 普通可行走地面(坡度<=55度)
 * - NAV_AREA_GROUND_STEEP: 陡峭地面(55<坡度<=70度)，仅战斗中的生物可通行
 *
 * 调用时机:
 * - 服务器部署前，由管理员运行mmaps_generator工具生成
 * - 可选择性地重新生成特定地图或瓦片
 *
 * 性能注意事项:
 * - 大型地图(如大陆地图)生成时间较长，建议使用多线程
 * - 生成的.mmtile文件需要定期更新以匹配客户端版本
 */

#include "MapBuilder.h"
#include "IntermediateValues.h"
#include "MapDefines.h"
#include "MapTree.h"
#include "ModelInstance.h"
#include "PathCommon.h"
#include "StringFormat.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <climits>

namespace MMAP
{
    /**
     * @brief TileBuilder构造函数
     *
     * 初始化地图瓦片构建器，创建工作线程和地形构建器。
     *
     * @param mapBuilder 所属的MapBuilder实例，用于访问全局配置和任务队列
     * @param skipLiquid 是否跳过液体(水面)数据的处理
     * @param bigBaseUnit 是否使用大基础单位(影响导航网格精度)
     * @param debugOutput 是否输出调试信息(生成OBJ文件用于可视化)
     *
     * 调用时机:
     * - MapBuilder初始化时为每个工作线程创建一个TileBuilder
     *
     * 性能注意事项:
     * - bigBaseUnit=true时精度较低但生成速度更快
     * - debugOutput会额外生成可视化文件，影响性能
     */
    TileBuilder::TileBuilder(MapBuilder* mapBuilder, bool skipLiquid, bool bigBaseUnit, bool debugOutput) :
        m_bigBaseUnit(bigBaseUnit),       // 基础单位尺寸设置，影响网格精度
        m_debugOutput(debugOutput),       // 调试输出开关
        m_mapBuilder(mapBuilder),         // 父构建器引用
        m_terrainBuilder(nullptr),        // 地形构建器，稍后初始化
        m_workerThread(&TileBuilder::WorkerThread, this),  // 启动工作线程
        m_rcContext(nullptr)              // Recast上下文，用于性能分析
    {
        // 创建地形构建器，负责加载和处理地形数据
        m_terrainBuilder = new TerrainBuilder(skipLiquid);
        // 创建Recast上下文，false表示不启用性能计时
        m_rcContext = new rcContext(false);
    }

    /**
     * @brief TileBuilder析构函数
     *
     * 清理资源，确保工作线程完成后再销毁对象。
     */
    TileBuilder::~TileBuilder()
    {
        // 等待工作线程完成当前任务
        WaitCompletion();

        // 释放地形构建器和Recast上下文
        delete m_terrainBuilder;
        delete m_rcContext;
    }

    /**
     * @brief 等待工作线程完成
     *
     * 阻塞当前线程，直到工作线程完成当前任务。
     * 用于确保资源被正确释放前所有任务已完成。
     *
     * 调用时机:
     * - 析构时确保线程安全退出
     * - 需要同步等待任务完成时
     */
    void TileBuilder::WaitCompletion()
    {
        // 如果工作线程是可合并的，则等待其完成
        if (m_workerThread.joinable())
            m_workerThread.join();
    }

    /**
     * @brief MapBuilder构造函数
     *
     * 初始化地图构建器，设置各种过滤选项和配置参数。
     *
     * @param maxWalkableAngle 最大可行走坡度角度(默认55度，最大85度)
     *                         超过此角度的表面将被标记为不可行走
     * @param maxWalkableAngleNotSteep 非陡峭可行走角度阈值(默认55度)
     *                                  小于此角度为普通地面，介于两者之间为陡峭地面
     * @param skipLiquid 是否跳过液体表面(水面)的处理
     * @param skipContinents 是否跳过大陆地图(东部王国、卡利姆多、外域、诺森德)
     * @param skipJunkMaps 是否跳过垃圾/测试地图
     * @param skipBattlegrounds 是否跳过战场地图
     * @param debugOutput 是否输出调试信息
     * @param bigBaseUnit 是否使用大基础单位(降低精度但提高生成速度)
     * @param mapid 指定要生成的地图ID，-1表示生成所有地图
     * @param offMeshFilePath OffMesh连接文件路径(用于自定义路径点)
     * @param threads 工作线程数量
     *
     * 调用时机:
     * - mmaps_generator工具启动时创建
     */
    MapBuilder::MapBuilder(Optional<float> maxWalkableAngle, Optional<float> maxWalkableAngleNotSteep, bool skipLiquid,
        bool skipContinents, bool skipJunkMaps, bool skipBattlegrounds,
        bool debugOutput, bool bigBaseUnit, int mapid, char const* offMeshFilePath, unsigned int threads) :
        m_terrainBuilder     (nullptr),           // 地形构建器，稍后初始化
        m_debugOutput        (debugOutput),       // 调试输出开关
        m_offMeshFilePath    (offMeshFilePath),   // OffMesh连接文件路径
        m_threads            (threads),           // 工作线程数
        m_skipContinents     (skipContinents),    // 跳过大陆地图标志
        m_skipJunkMaps       (skipJunkMaps),      // 跳过垃圾地图标志
        m_skipBattlegrounds  (skipBattlegrounds), // 跳过战场标志
        m_skipLiquid         (skipLiquid),        // 跳过液体的标志
        m_maxWalkableAngle   (maxWalkableAngle),  // 最大可行走角度
        m_maxWalkableAngleNotSteep (maxWalkableAngleNotSteep),  // 非陡峭角度阈值
        m_bigBaseUnit        (bigBaseUnit),       // 大基础单位标志
        m_mapid              (mapid),             // 指定地图ID
        m_totalTiles         (0u),                // 总瓦片计数
        m_totalTilesProcessed(0u),                // 已处理瓦片计数
        m_rcContext          (nullptr),           // Recast上下文
        _cancelationToken    (false)              // 取消令牌，用于安全终止线程
    {
        // 创建地形构建器实例
        m_terrainBuilder = new TerrainBuilder(skipLiquid);

        // 创建Recast上下文，false表示不启用性能计时
        m_rcContext = new rcContext(false);

        // 确保至少有1个工作线程
        // At least 1 thread is needed
        m_threads = std::max(1u, m_threads);

        // 发现并收集所有需要处理的地图瓦片
        discoverTiles();
    }

    /**
     * @brief MapBuilder析构函数
     *
     * 清理所有资源，包括工作线程、地形构建器和瓦片数据。
     * 首先设置取消标志，通知所有工作线程停止工作。
     */
    MapBuilder::~MapBuilder()
    {
        // 设置取消标志，通知工作线程退出
        _cancelationToken = true;

        // 取消任务队列，唤醒等待的线程
        _queue.Cancel();

        // 删除所有瓦片构建器
        for (auto& builder : m_tileBuilders)
            delete builder;

        m_tileBuilders.clear();

        // 清理瓦片列表
        for (TileList::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
        {
            (*it).m_tiles->clear();
            delete (*it).m_tiles;
        }

        // 释放地形构建器和Recast上下文
        delete m_terrainBuilder;
        delete m_rcContext;
    }

    /**
     * @brief 发现并收集所有地图和瓦片信息
     *
     * 扫描maps和vmaps目录，收集所有可用的地图ID及其瓦片信息。
     * 这些信息用于后续的导航网格生成过程。
     *
     * 扫描目录:
     * - maps/: 存放.map文件(地形高度图数据)
     * - vmaps/: 存放.vmtree和.vmtile文件(虚拟地图碰撞数据)
     *
     * 处理流程:
     * 1. 扫描maps目录获取所有地图ID
     * 2. 扫描vmaps目录获取所有.vmtree文件中的地图ID
     * 3. 为每个地图收集其所有瓦片ID
     * 4. 对于没有瓦片文件的地图，通过边界计算生成瓦片列表
     *
     * 性能注意事项:
     * - 仅在构造时调用一次
     * - 文件系统IO操作较多，可能影响启动速度
     */
    void MapBuilder::discoverTiles()
    {
        std::vector<std::string> files;
        uint32 mapID, tileX, tileY, tileID, count = 0;
        char filter[12];

        // 第一步：发现所有地图
        printf("Discovering maps... ");
        // 扫描maps目录获取地图列表
        getDirContents(files, "maps");
        for (uint32 i = 0; i < files.size(); ++i)
        {
            // 从文件名提取地图ID(前3位数字)
            mapID = uint32(atoi(files[i].substr(0,3).c_str()));
            // 避免重复添加同一个地图
            if (std::find(m_tiles.begin(), m_tiles.end(), mapID) == m_tiles.end())
            {
                m_tiles.emplace_back(MapTiles(mapID, new std::set<uint32>));
                count++;
            }
        }

        // 扫描vmaps目录获取虚拟地图列表
        files.clear();
        getDirContents(files, "vmaps", "*.vmtree");
        for (uint32 i = 0; i < files.size(); ++i)
        {
            // 从文件名提取地图ID
            mapID = uint32(atoi(files[i].substr(0,3).c_str()));
            if (std::find(m_tiles.begin(), m_tiles.end(), mapID) == m_tiles.end())
            {
                m_tiles.emplace_back(MapTiles(mapID, new std::set<uint32>));
                count++;
            }
        }
        printf("found %u.\n", count);

        // 第二步：发现每个地图的瓦片
        count = 0;
        printf("Discovering tiles... ");
        for (TileList::iterator itr = m_tiles.begin(); itr != m_tiles.end(); ++itr)
        {
            std::set<uint32>* tiles = (*itr).m_tiles;
            mapID = (*itr).m_mapId;

            // 从vmaps目录收集瓦片文件
            // 文件名格式: XXXYYZZ.vmtile (XXX=地图ID, YY=tileY, ZZ=tileX)
            sprintf(filter, "%03u*.vmtile", mapID);
            files.clear();
            getDirContents(files, "vmaps", filter);
            for (uint32 i = 0; i < files.size(); ++i)
            {
                // 解析文件名中的瓦片坐标
                tileX = uint32(atoi(files[i].substr(7,2).c_str()));
                tileY = uint32(atoi(files[i].substr(4,2).c_str()));
                // 将坐标打包为单个ID
                tileID = StaticMapTree::packTileID(tileY, tileX);

                tiles->insert(tileID);
                count++;
            }

            // 从maps目录收集瓦片文件
            // 文件名格式: XXXYYZZ.map (XXX=地图ID, YY=tileY, ZZ=tileX)
            sprintf(filter, "%03u*", mapID);
            files.clear();
            getDirContents(files, "maps", filter);
            for (uint32 i = 0; i < files.size(); ++i)
            {
                // 解析文件名中的瓦片坐标
                tileY = uint32(atoi(files[i].substr(3,2).c_str()));
                tileX = uint32(atoi(files[i].substr(5,2).c_str()));
                tileID = StaticMapTree::packTileID(tileX, tileY);

                // 使用insert的返回值避免重复计数
                if (tiles->insert(tileID).second)
                    count++;
            }

            // 对于没有瓦片文件的地图(如某些室内地图)
            // make sure we process maps which don't have tiles
            if (tiles->empty())
            {
                // 通过模型边界计算网格范围
                // convert coord bounds to grid bounds
                uint32 minX, minY, maxX, maxY;
                getGridBounds(mapID, minX, minY, maxX, maxY);

                // 添加边界内的所有瓦片
                // add all tiles within bounds to tile list.
                for (uint32 i = minX; i <= maxX; ++i)
                    for (uint32 j = minY; j <= maxY; ++j)
                        if (tiles->insert(StaticMapTree::packTileID(i, j)).second)
                            count++;
            }
        }
        printf("found %u.\n\n", count);

        // 计算需要处理的总瓦片数(排除跳过的地图)
        // Calculate tiles to process in total
        for (TileList::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
        {
            if (!shouldSkipMap(it->m_mapId))
                m_totalTiles += it->m_tiles->size();
        }
    }

    /**
     * @brief 获取指定地图的瓦片列表
     *
     * 从瓦片列表中查找指定地图ID的瓦片集合，如果不存在则创建新的集合。
     *
     * @param mapID 地图ID
     * @return 该地图的瓦片ID集合指针
     *
     * 调用时机:
     * - buildMap时获取地图的瓦片列表
     * - buildNavMesh时确定瓦片数量
     */
    std::set<uint32>* MapBuilder::getTileList(uint32 mapID)
    {
        // 在现有列表中查找
        TileList::iterator itr = std::find(m_tiles.begin(), m_tiles.end(), mapID);
        if (itr != m_tiles.end())
            return (*itr).m_tiles;

        // 未找到则创建新的瓦片集合
        std::set<uint32>* tiles = new std::set<uint32>();
        m_tiles.emplace_back(MapTiles(mapID, tiles));
        return tiles;
    }

    /**
     * @brief 工作线程主循环
     *
     * 从任务队列中获取瓦片构建任务并执行。
     * 这是多线程瓦片生成的核心函数，每个TileBuilder在独立线程中运行此函数。
     *
     * 工作流程:
     * 1. 从队列中等待并获取瓦片信息
     * 2. 检查取消标志，如需退出则返回
     * 3. 创建导航网格实例
     * 4. 调用buildTile处理瓦片
     * 5. 释放导航网格资源
     *
     * 性能注意事项:
     * - 使用生产者-消费者队列实现任务分发
     * - 每个瓦片独立处理，线程间无锁竞争
     */
    void TileBuilder::WorkerThread()
    {
        while (true)
        {
            TileInfo tileInfo;

            // 从队列中等待获取任务(阻塞操作)
            m_mapBuilder->_queue.WaitAndPop(tileInfo);

            // 检查取消标志
            if (m_mapBuilder->_cancelationToken)
                return;

            // 为当前瓦片创建导航网格实例
            dtNavMesh* navMesh = dtAllocNavMesh();
            if (!navMesh->init(&tileInfo.m_navMeshParams))
            {
                printf("[Map %03i] Failed creating navmesh for tile %i,%i !\n", tileInfo.m_mapId, tileInfo.m_tileX, tileInfo.m_tileY);
                dtFreeNavMesh(navMesh);
                return;
            }

            // 执行瓦片构建
            buildTile(tileInfo.m_mapId, tileInfo.m_tileX, tileInfo.m_tileY, navMesh);

            // 释放导航网格资源
            dtFreeNavMesh(navMesh);
        }
    }

    /**
     * @brief 构建所有或指定地图的导航网格
     *
     * 这是地图构建的主入口函数，负责创建工作线程并分发任务。
     *
     * @param mapID 可选参数，指定要构建的地图ID。为空时构建所有地图
     *
     * 执行流程:
     * 1. 创建指定数量的工作线程(TileBuilder)
     * 2. 将所有需要处理的地图加入任务队列
     * 3. 等待所有任务完成
     * 4. 清理工作线程
     *
     * 调用时机:
     * - mmaps_generator主程序调用，开始地图生成过程
     *
     * 性能注意事项:
     * - 线程数建议设置为CPU核心数
     * - 大型地图需要较长时间处理
     */
    void MapBuilder::buildMaps(Optional<uint32> mapID)
    {
        printf("Using %u threads to generate mmaps\n", m_threads);

        // 创建工作线程池
        for (unsigned int i = 0; i < m_threads; ++i)
        {
            m_tileBuilders.push_back(new TileBuilder(this, m_skipLiquid, m_bigBaseUnit, m_debugOutput));
        }

        // 构建指定地图或所有地图
        if (mapID)
        {
            buildMap(*mapID);
        }
        else
        {
            // Build all maps if no map id has been specified
            for (TileList::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
            {
                if (!shouldSkipMap(it->m_mapId))
                    buildMap(it->m_mapId);
            }
        }

        // 等待任务队列清空
        while (!_queue.Empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        // 设置取消标志并清理工作线程
        _cancelationToken = true;

        _queue.Cancel();

        for (auto& builder : m_tileBuilders)
            delete builder;

        m_tileBuilders.clear();
    }

    /**
     * @brief 获取地图的网格边界
     *
     * 通过加载WDT(World Map Tree)数据计算地图的边界范围，
     * 用于确定需要生成导航网格的瓦片区域。
     *
     * @param mapID 地图ID
     * @param minX [out] 最小X坐标(瓦片索引)
     * @param minY [out] 最小Y坐标(瓦片索引)
     * @param maxX [out] 最大X坐标(瓦片索引)
     * @param maxY [out] 最大Y坐标(瓦片索引)
     *
     * 调用时机:
     * - discoverTiles处理没有瓦片文件的地图时
     *
     * 性能注意事项:
     * - 需要加载WDT文件，涉及磁盘IO
     * - 对于没有实际数据的地图，返回无效边界
     */
    void MapBuilder::getGridBounds(uint32 mapID, uint32 &minX, uint32 &minY, uint32 &maxX, uint32 &maxY) const
    {
        // 初始化为无效值，确保调用者在没有有效数据时不会进入循环
        // min and max are initialized to invalid values so the caller iterating the [min, max] range
        // will never enter the loop unless valid min/max values are found
        maxX = 0;
        maxY = 0;
        minX = std::numeric_limits<uint32>::max();
        minY = std::numeric_limits<uint32>::max();

        // 初始化边界数组
        float bmin[3] = { 0, 0, 0 };    // 固体表面最小边界
        float bmax[3] = { 0, 0, 0 };    // 固体表面最大边界
        float lmin[3] = { 0, 0, 0 };    // 液体表面最小边界
        float lmax[3] = { 0, 0, 0 };    // 液体表面最大边界
        MeshData meshData;

        // 加载WDT数据，使用(64,64)作为中心点坐标
        // make sure we process maps which don't have tiles
        // initialize the static tree, which loads WDT models
        if (!m_terrainBuilder->loadVMap(mapID, 64, 64, meshData))
            return;

        // 检查是否有有效的顶点数据
        // get the coord bounds of the model data
        if (meshData.solidVerts.size() + meshData.liquidVerts.size() == 0)
            return;

        // 计算模型数据的坐标边界
        // get the coord bounds of the model data
        if (meshData.solidVerts.size() && meshData.liquidVerts.size())
        {
            // 同时存在固体和液体数据，分别计算后合并
            rcCalcBounds(meshData.solidVerts.getCArray(), meshData.solidVerts.size() / 3, bmin, bmax);
            rcCalcBounds(meshData.liquidVerts.getCArray(), meshData.liquidVerts.size() / 3, lmin, lmax);
            // 合并边界
            rcVmin(bmin, lmin);  // 取最小值
            rcVmax(bmax, lmax);  // 取最大值
        }
        else if (meshData.solidVerts.size())
            // 只有固体数据
            rcCalcBounds(meshData.solidVerts.getCArray(), meshData.solidVerts.size() / 3, bmin, bmax);
        else
            // 只有液体数据
            rcCalcBounds(meshData.liquidVerts.getCArray(), meshData.liquidVerts.size() / 3, lmin, lmax);

        // 将坐标边界转换为网格边界
        // 世界坐标系统: 中心为(0,0)，向右为+X，向上为+Y
        // 网格坐标系统: 左上角为(0,0)，向右为+X，向下为+Y
        // convert coord bounds to grid bounds
        maxX = 32 - bmin[0] / GRID_SIZE;
        maxY = 32 - bmin[2] / GRID_SIZE;
        minX = 32 - bmax[0] / GRID_SIZE;
        minY = 32 - bmax[2] / GRID_SIZE;
    }

    /**
     * @brief 从文件构建导航网格
     *
     * 从自定义的二进制文件读取顶点和索引数据，用于特殊场景的导航网格生成。
     * 主要用于调试或导入自定义几何数据。
     *
     * @param name 文件路径
     *
     * 文件格式:
     * - int mapId: 地图ID
     * - int tileX: 瓦片X坐标
     * - int tileY: 瓦片Y坐标
     * - uint32 verticesCount: 顶点数量
     * - uint32 indicesCount: 索引数量
     * - float[] vertices: 顶点数据
     * - int[] indices: 索引数据
     *
     * 调用时机:
     * - 用户指定自定义mesh文件时调用
     */
    void MapBuilder::buildMeshFromFile(char* name)
    {
        FILE* file = fopen(name, "rb");
        if (!file)
            return;

        printf("Building mesh from file\n");
        int tileX, tileY, mapId;

        // 读取文件头信息
        if (fread(&mapId, sizeof(int), 1, file) != 1)
        {
            fclose(file);
            return;
        }
        if (fread(&tileX, sizeof(int), 1, file) != 1)
        {
            fclose(file);
            return;
        }
        if (fread(&tileY, sizeof(int), 1, file) != 1)
        {
            fclose(file);
            return;
        }

        // 创建导航网格
        dtNavMesh* navMesh = nullptr;
        buildNavMesh(mapId, navMesh);
        if (!navMesh)
        {
            printf("Failed creating navmesh!              \n");
            fclose(file);
            return;
        }

        // 读取顶点和索引数量
        uint32 verticesCount, indicesCount;
        if (fread(&verticesCount, sizeof(uint32), 1, file) != 1)
        {
            fclose(file);
            return;
        }

        if (fread(&indicesCount, sizeof(uint32), 1, file) != 1)
        {
            fclose(file);
            return;
        }

        // 分配内存并读取顶点数据
        float* verts = new float[verticesCount];
        int* inds = new int[indicesCount];

        if (fread(verts, sizeof(float), verticesCount, file) != verticesCount)
        {
            fclose(file);
            delete[] verts;
            delete[] inds;
            return;
        }

        // 读取索引数据
        if (fread(inds, sizeof(int), indicesCount, file) != indicesCount)
        {
            fclose(file);
            delete[] verts;
            delete[] inds;
            return;
        }

        // 构建MeshData结构
        MeshData data;

        for (uint32 i = 0; i < verticesCount; ++i)
            data.solidVerts.append(verts[i]);
        delete[] verts;

        for (uint32 i = 0; i < indicesCount; ++i)
            data.solidTris.append(inds[i]);
        delete[] inds;

        // 清理未使用的顶点
        TerrainBuilder::cleanVertices(data.solidVerts, data.solidTris);

        // 计算瓦片边界
        // get bounds of current tile
        float bmin[3], bmax[3];
        getTileBounds(tileX, tileY, data.solidVerts.getCArray(), data.solidVerts.size() / 3, bmin, bmax);

        // 构建导航网格瓦片
        // build navmesh tile
        TileBuilder tileBuilder = TileBuilder(this, m_skipLiquid, m_bigBaseUnit, m_debugOutput);
        tileBuilder.buildMoveMapTile(mapId, tileX, tileY, data, bmin, bmax, navMesh);
        fclose(file);
    }

    /**
     * @brief 构建单个瓦片的导航网格
     *
     * 用于重新生成指定地图的特定瓦片，而不需要处理整个地图。
     * 适用于增量更新或调试特定区域。
     *
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     *
     * 调用时机:
     * - 用户指定--tile参数时调用
     *
     * 性能注意事项:
     * - 仅处理单个瓦片，速度很快
     * - 不会删除旧的瓦片文件(待改进)
     */
    void MapBuilder::buildSingleTile(uint32 mapID, uint32 tileX, uint32 tileY)
    {
        // 创建导航网格实例
        dtNavMesh* navMesh = nullptr;
        buildNavMesh(mapID, navMesh);
        if (!navMesh)
        {
            printf("Failed creating navmesh!              \n");
            return;
        }

        // ToDo: delete the old tile as the user clearly wants to rebuild it
        // 注意: 当前实现不会删除旧瓦片文件

        // 构建单个瓦片
        TileBuilder tileBuilder = TileBuilder(this, m_skipLiquid, m_bigBaseUnit, m_debugOutput);
        tileBuilder.buildTile(mapID, tileX, tileY, navMesh);
        dtFreeNavMesh(navMesh);

        // 设置取消标志，终止其他可能的任务
        _cancelationToken = true;

        _queue.Cancel();
    }

    /**
     * @brief 构建指定地图的所有导航网格瓦片
     *
     * 为指定地图创建导航网格并生成所有瓦片的构建任务。
     * 任务被推送到队列中，由工作线程异步处理。
     *
     * @param mapID 地图ID
     *
     * 执行流程:
     * 1. 获取地图的瓦片列表
     * 2. 创建导航网格实例
     * 3. 将每个瓦片的构建任务推送到队列
     *
     * 调用时机:
     * - buildMaps遍历所有地图时调用
     * - 用户指定特定地图ID时调用
     *
     * 性能注意事项:
     * - 任务推送到队列后立即返回，实际构建由工作线程完成
     */
    void MapBuilder::buildMap(uint32 mapID)
    {
        // 获取该地图的瓦片列表
        std::set<uint32>* tiles = getTileList(mapID);

        if (!tiles->empty())
        {
            // 创建导航网格实例
            // build navMesh
            dtNavMesh* navMesh = nullptr;
            buildNavMesh(mapID, navMesh);
            if (!navMesh)
            {
                printf("[Map %03i] Failed creating navmesh!\n", mapID);
                // 即使失败也需要更新计数，避免进度条卡住
                m_totalTilesProcessed += tiles->size();
                return;
            }

            // 将所有瓦片推送到任务队列
            // now start building mmtiles for each tile
            printf("[Map %03i] We have %u tiles.                          \n", mapID, (unsigned int)tiles->size());
            for (std::set<uint32>::iterator it = tiles->begin(); it != tiles->end(); ++it)
            {
                uint32 tileX, tileY;

                // 解包瓦片坐标
                // unpack tile coords
                StaticMapTree::unpackTileID((*it), tileX, tileY);

                // 构建瓦片信息并推送到队列
                TileInfo tileInfo;
                tileInfo.m_mapId = mapID;
                tileInfo.m_tileX = tileX;
                tileInfo.m_tileY = tileY;
                // 复制导航网格参数，用于工作线程创建本地实例
                memcpy(&tileInfo.m_navMeshParams, navMesh->getParams(), sizeof(dtNavMeshParams));
                _queue.Push(tileInfo);
            }

            // 释放导航网格(工作线程会创建自己的实例)
            dtFreeNavMesh(navMesh);
        }
    }

    /**
     * @brief 构建单个瓦片的导航网格
     *
     * 这是导航网格生成的核心函数，负责加载地形数据、处理几何体并生成最终的导航网格瓦片。
     *
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标(0-63)
     * @param tileY 瓦片Y坐标(0-63)
     * @param navMesh 导航网格实例
     *
     * 处理流程:
     * 1. 检查是否需要跳过该瓦片(已存在且版本匹配)
     * 2. 加载地图高度图数据(.map文件)
     * 3. 加载虚拟地图碰撞数据(.vmap文件)
     * 4. 清理未使用的顶点
     * 5. 计算瓦片边界
     * 6. 加载OffMesh连接
     * 7. 调用buildMoveMapTile生成导航网格
     *
     * 调用时机:
     * - 工作线程从队列获取任务后调用
     * - buildSingleTile直接调用
     *
     * 性能注意事项:
     * - 单个瓦片处理时间取决于地形复杂度
     * - 涉及磁盘IO读取.map和.vmap文件
     */
    void TileBuilder::buildTile(uint32 mapID, uint32 tileX, uint32 tileY, dtNavMesh* navMesh)
    {
        // 检查是否应该跳过该瓦片(已存在且有效)
        if(shouldSkipTile(mapID, tileX, tileY))
        {
            ++m_mapBuilder->m_totalTilesProcessed;
            return;
        }

        // 输出进度信息
        printf("%u%% [Map %03i] Building tile [%02u,%02u]\n", m_mapBuilder->currentPercentageDone(), mapID, tileX, tileY);

        MeshData meshData;

        // 加载地图高度图数据
        // get heightmap data
        m_terrainBuilder->loadMap(mapID, tileX, tileY, meshData);

        // 加载虚拟地图碰撞数据
        // 注意: loadVMap的参数顺序是(tileY, tileX)，与loadMap不同
        // get model data
        m_terrainBuilder->loadVMap(mapID, tileY, tileX, meshData);

        // 如果没有任何数据，跳过该瓦片
        // if there is no data, give up now
        if (!meshData.solidVerts.size() && !meshData.liquidVerts.size())
        {
            ++m_mapBuilder->m_totalTilesProcessed;
            return;
        }

        // 清理未使用的顶点，减少内存占用和计算量
        // remove unused vertices
        TerrainBuilder::cleanVertices(meshData.solidVerts, meshData.solidTris);
        TerrainBuilder::cleanVertices(meshData.liquidVerts, meshData.liquidTris);

        // 合并所有顶点用于边界计算
        // gather all mesh data for final data check, and bounds calculation
        G3D::Array<float> allVerts;
        allVerts.append(meshData.liquidVerts);
        allVerts.append(meshData.solidVerts);

        // 最终检查是否有有效数据
        if (!allVerts.size())
        {
            ++m_mapBuilder->m_totalTilesProcessed;
            return;
        }

        // 计算当前瓦片的边界
        // get bounds of current tile
        float bmin[3], bmax[3];
        m_mapBuilder->getTileBounds(tileX, tileY, allVerts.getCArray(), allVerts.size() / 3, bmin, bmax);

        // 加载OffMesh连接(自定义路径点)
        m_terrainBuilder->loadOffMeshConnections(mapID, tileX, tileY, meshData, m_mapBuilder->m_offMeshFilePath);

        // 构建导航网格瓦片
        // build navmesh tile
        buildMoveMapTile(mapID, tileX, tileY, meshData, bmin, bmax, navMesh);

        // 更新进度计数
        ++m_mapBuilder->m_totalTilesProcessed;
    }

    /**
     * @brief 创建导航网格实例
     *
     * 为指定地图创建导航网格对象，并写入.mmap文件头。
     * 导航网格是整个地图的容器，包含该地图的所有瓦片。
     *
     * @param mapID 地图ID
     * @param navMesh [out] 创建的导航网格指针
     *
     * 生成的文件:
     * - mmaps/XXX.mmap: 导航网格头文件，包含导航网格参数
     *
     * 导航网格参数包括:
     * - tileWidth/tileHeight: 瓦片尺寸(GRID_SIZE = 533.333)
     * - orig: 原点坐标
     * - maxTiles: 最大瓦片数
     * - maxPolys: 每个瓦片的最大多边形数
     *
     * 调用时机:
     * - buildMap时为每个地图调用
     * - buildSingleTile时调用
     * - buildMeshFromFile时调用
     */
    void MapBuilder::buildNavMesh(uint32 mapID, dtNavMesh* &navMesh)
    {
        std::set<uint32>* tiles = getTileList(mapID);

        // 多边形ID的位数配置(静态配置)
        // old code for non-statically assigned bitmask sizes:
        ///*** calculate number of bits needed to store tiles & polys ***/
        //int tileBits = dtIlog2(dtNextPow2(tiles->size()));
        //if (tileBits < 1) tileBits = 1;                                     // need at least one bit!
        //int polyBits = sizeof(dtPolyRef)*8 - SALT_MIN_BITS - tileBits;

        int polyBits = DT_POLY_BITS;

        int maxTiles = tiles->size();
        int maxPolysPerTile = 1 << polyBits;

        /***          calculate bounds of map         ***/

        // 计算地图的边界范围
        uint32 tileXMin = 64, tileYMin = 64, tileXMax = 0, tileYMax = 0, tileX, tileY;
        for (std::set<uint32>::iterator it = tiles->begin(); it != tiles->end(); ++it)
        {
            StaticMapTree::unpackTileID(*it, tileX, tileY);

            // 更新X方向边界
            if (tileX > tileXMax)
                tileXMax = tileX;
            else if (tileX < tileXMin)
                tileXMin = tileX;

            // 更新Y方向边界
            if (tileY > tileYMax)
                tileYMax = tileY;
            else if (tileY < tileYMin)
                tileYMin = tileY;
        }

        // 使用最大值计算原点坐标
        // use Max because '32 - tileX' is negative for values over 32
        float bmin[3], bmax[3];
        getTileBounds(tileXMax, tileYMax, nullptr, 0, bmin, bmax);

        /***       now create the navmesh       ***/

        // 设置导航网格创建参数
        // navmesh creation params
        dtNavMeshParams navMeshParams;
        memset(&navMeshParams, 0, sizeof(dtNavMeshParams));
        navMeshParams.tileWidth = GRID_SIZE;    // 瓦片宽度(533.333游戏单位)
        navMeshParams.tileHeight = GRID_SIZE;   // 瓦片高度(533.333游戏单位)
        rcVcopy(navMeshParams.orig, bmin);      // 导航网格原点
        navMeshParams.maxTiles = maxTiles;      // 最大瓦片数
        navMeshParams.maxPolys = maxPolysPerTile;  // 每瓦片最大多边形数

        // 分配并初始化导航网格
        navMesh = dtAllocNavMesh();
        printf("[Map %03i] Creating navMesh...\n", mapID);
        if (!navMesh->init(&navMeshParams))
        {
            printf("[Map %03i] Failed creating navmesh!                \n", mapID);
            return;
        }

        // 将导航网格参数写入.mmap文件
        char fileName[25];
        sprintf(fileName, "mmaps/%03u.mmap", mapID);

        FILE* file = fopen(fileName, "wb");
        if (!file)
        {
            dtFreeNavMesh(navMesh);
            char message[1024];
            sprintf(message, "[Map %03i] Failed to open %s for writing!\n", mapID, fileName);
            perror(message);
            return;
        }

        // 现在导航网格参数已验证有效，写入文件
        // now that we know navMesh params are valid, we can write them to file
        fwrite(&navMeshParams, sizeof(dtNavMeshParams), 1, file);
        fclose(file);
    }

    /**
     * @brief 构建移动地图瓦片(核心导航网格生成函数)
     *
     * 这是导航网格生成最核心的函数，使用Recast库将原始几何数据转换为可寻路的多边形网格。
     *
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @param meshData 网格数据(包含顶点、三角形、液体数据等)
     * @param bmin 瓦片最小边界
     * @param bmax 瓦片最大边界
     * @param navMesh 导航网格实例
     *
     * 处理流程(Recast管线):
     * 1. 光栅化三角形到高度场(Heightfield)
     * 2. 过滤可行走表面
     * 3. 构建紧凑高度场(Compact Heightfield)
     * 4. 腐蚀可行走区域
     * 5. 构建距离场和区域
     * 6. 生成轮廓集(Contour Set)
     * 7. 构建多边形网格(PolyMesh)
     * 8. 构建细节网格(PolyMeshDetail)
     * 9. 合并子瓦片网格
     * 10. 创建Detour导航网格数据
     * 11. 写入.mmtile文件
     *
     * 导航区域标记:
     * - NAV_AREA_GROUND: 坡度<=55度的可行走表面
     * - NAV_AREA_GROUND_STEEP: 55<坡度<=70度的陡峭表面
     *
     * 调用时机:
     * - buildTile最后调用
     *
     * 性能注意事项:
     * - 这是最耗时的处理步骤
     * - 内存使用量取决于瓦片复杂度
     * - 输出文件大小取决于多边形数量
     */
    void TileBuilder::buildMoveMapTile(uint32 mapID, uint32 tileX, uint32 tileY,
        MeshData &meshData, float bmin[3], float bmax[3],
        dtNavMesh* navMesh)
    {
        // 准备输出标识字符串
        // console output
        char tileString[20];
        sprintf(tileString, "[Map %03i] [%02i,%02i]: ", mapID, tileX, tileY);
        printf("%s Building movemap tiles...\n", tileString);

        // 创建中间值存储对象
        IntermediateValues iv;

        // 提取固体表面数据
        float* tVerts = meshData.solidVerts.getCArray();   // 固体顶点数组
        int tVertCount = meshData.solidVerts.size() / 3;   // 固体顶点数量(每个顶点3个浮点数)
        int* tTris = meshData.solidTris.getCArray();       // 固体三角形索引
        int tTriCount = meshData.solidTris.size() / 3;     // 固体三角形数量

        // 提取液体表面数据
        float* lVerts = meshData.liquidVerts.getCArray();  // 液体顶点数组
        int lVertCount = meshData.liquidVerts.size() / 3;  // 液体顶点数量
        int* lTris = meshData.liquidTris.getCArray();      // 液体三角形索引
        int lTriCount = meshData.liquidTris.size() / 3;    // 液体三角形数量
        uint8* lTriFlags = meshData.liquidType.getCArray(); // 液体类型标志

        // 获取瓦片配置
        const TileConfig tileConfig = TileConfig(m_bigBaseUnit);
        int TILES_PER_MAP = tileConfig.TILES_PER_MAP;       // 每个地图的瓦片数量
        float BASE_UNIT_DIM = tileConfig.BASE_UNIT_DIM;     // 基础单位尺寸

        // 获取地图特定配置(包含坡度角度、可行走高度等参数)
        rcConfig config = m_mapBuilder->GetMapSpecificConfig(mapID, bmin, bmax, tileConfig);

        // 计算高度场的网格尺寸
        // this sets the dimensions of the heightfield - should maybe happen before border padding
        rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

        // 分配子瓦片数组(每个导航瓦片被分割为多个子瓦片处理)
        // allocate subregions : tiles
        Tile* tiles = new Tile[TILES_PER_MAP * TILES_PER_MAP];

        // 初始化每个子瓦片的配置
        // Initialize per tile config.
        rcConfig tileCfg = config;
        tileCfg.width = config.tileSize + config.borderSize*2;   // 加上边界填充
        tileCfg.height = config.tileSize + config.borderSize*2;

        // 准备合并多边形网格和细节网格的数组
        // merge per tile poly and detail meshes
        rcPolyMesh** pmmerge = new rcPolyMesh*[TILES_PER_MAP * TILES_PER_MAP];
        rcPolyMeshDetail** dmmerge = new rcPolyMeshDetail*[TILES_PER_MAP * TILES_PER_MAP];
        int nmerge = 0;
        // 遍历所有子瓦片，逐个构建导航网格
        // build all tiles
        for (int y = 0; y < TILES_PER_MAP; ++y)
        {
            for (int x = 0; x < TILES_PER_MAP; ++x)
            {
                Tile& tile = tiles[x + y * TILES_PER_MAP];

                // 计算当前子瓦片的边界框
                // Calculate the per tile bounding box.
                tileCfg.bmin[0] = config.bmin[0] + x * float(config.tileSize * config.cs);
                tileCfg.bmin[2] = config.bmin[2] + y * float(config.tileSize * config.cs);
                tileCfg.bmax[0] = config.bmin[0] + (x + 1) * float(config.tileSize * config.cs);
                tileCfg.bmax[2] = config.bmin[2] + (y + 1) * float(config.tileSize * config.cs);

                // 添加边界填充，确保相邻瓦片边缘平滑
                tileCfg.bmin[0] -= tileCfg.borderSize * tileCfg.cs;
                tileCfg.bmin[2] -= tileCfg.borderSize * tileCfg.cs;
                tileCfg.bmax[0] += tileCfg.borderSize * tileCfg.cs;
                tileCfg.bmax[2] += tileCfg.borderSize * tileCfg.cs;

                // 第一步: 创建高度场
                // 高度场是一个体素网格，存储每个位置的高度信息
                // build heightfield
                tile.solid = rcAllocHeightfield();
                if (!tile.solid || !rcCreateHeightfield(m_rcContext, *tile.solid, tileCfg.width, tileCfg.height, tileCfg.bmin, tileCfg.bmax, tileCfg.cs, tileCfg.ch))
                {
                    printf("%s Failed building heightfield!            \n", tileString);
                    continue;
                }

                // 第二步: 标记可行走的三角形
                // 根据坡度角度将三角形标记为不同区域类型
                // mark all walkable tiles, both liquids and solids

                /* 区域标记逻辑:
                 * 我们希望坡度小于walkableSlopeAngleNotSteep (<= 55度)的三角形被标记为NAV_AREA_GROUND
                 * 坡度介于walkableSlopeAngleNotSteep和walkableSlopeAngle之间(55 < .. <= 70度)的标记为NAV_AREA_GROUND_STEEP
                 *
                 * 实现方法:
                 * 1. 首先将所有三角形标记为NAV_AREA_GROUND_STEEP
                 * 2. 调用rcClearUnwalkableTriangles清除坡度>70度的三角形(设为RC_NULL_AREA)
                 * 3. 调用rcMarkWalkableTriangles将坡度<=55度的三角形标记为NAV_AREA_GROUND
                 *
                 * 结果:
                 * - NAV_AREA_GROUND: 玩家和空闲生物可通行
                 * - NAV_AREA_GROUND_STEEP: 仅战斗中的生物可通行(用于追逐玩家)
                 * - RC_NULL_AREA: 不可通行
                 */
                /* we want to have triangles with slope less than walkableSlopeAngleNotSteep (<= 55) to have NAV_AREA_GROUND
                 * and with slope between walkableSlopeAngleNotSteep and walkableSlopeAngle (55 < .. <= 70) to have NAV_AREA_GROUND_STEEP.
                 * we achieve this using recast API: memset everything to NAV_AREA_GROUND_STEEP, call rcClearUnwalkableTriangles with 70 so
                 * any area above that will get RC_NULL_AREA (unwalkable), then call rcMarkWalkableTriangles with 55 to set NAV_AREA_GROUND
                 * on anything below 55 . Players and idle Creatures can use NAV_AREA_GROUND, while Creatures in combat can use NAV_AREA_GROUND_STEEP.
                 */
                unsigned char* triFlags = new unsigned char[tTriCount];
                // 初始化所有三角形为陡峭地面
                memset(triFlags, NAV_AREA_GROUND_STEEP, tTriCount*sizeof(unsigned char));
                // 清除超过最大坡度的三角形(不可行走)
                rcClearUnwalkableTriangles(m_rcContext, tileCfg.walkableSlopeAngle, tVerts, tVertCount, tTris, tTriCount, triFlags);
                // 标记小于非陡峭阈值的三角形为普通地面
                rcMarkWalkableTriangles(m_rcContext, tileCfg.walkableSlopeAngleNotSteep, tVerts, tVertCount, tTris, tTriCount, triFlags, NAV_AREA_GROUND);
                // 将三角形光栅化到高度场
                rcRasterizeTriangles(m_rcContext, tVerts, tVertCount, tTris, triFlags, tTriCount, *tile.solid, config.walkableClimb);
                delete[] triFlags;

                // 第三步: 应用可行走过滤器
                // 过滤低矮障碍物(如矮台阶)
                rcFilterLowHangingWalkableObstacles(m_rcContext, config.walkableClimb, *tile.solid);
                // 过滤边缘跨度(悬崖边缘)
                rcFilterLedgeSpans(m_rcContext, tileCfg.walkableHeight, tileCfg.walkableClimb, *tile.solid);
                // 过滤低高度跨度(角色无法站立的空间)
                rcFilterWalkableLowHeightSpans(m_rcContext, tileCfg.walkableHeight, *tile.solid);

                // 第四步: 光栅化液体三角形
                // 液体表面通常也是可行走的(如水面)
                // add liquid triangles
                rcRasterizeTriangles(m_rcContext, lVerts, lVertCount, lTris, lTriFlags, lTriCount, *tile.solid, config.walkableClimb);

                // 第五步: 构建紧凑高度场
                // 紧凑高度场是高度场的优化表示，只存储可行走的体素
                // compact heightfield spans
                tile.chf = rcAllocCompactHeightfield();
                if (!tile.chf || !rcBuildCompactHeightfield(m_rcContext, tileCfg.walkableHeight, tileCfg.walkableClimb, *tile.solid, *tile.chf))
                {
                    printf("%s Failed compacting heightfield!            \n", tileString);
                    continue;
                }

                // 第六步到第十步: 构建多边形网格中间体

                // 腐蚀可行走区域
                // 腐蚀操作确保角色不会太靠近边缘(根据角色半径)
                // build polymesh intermediates
                if (!rcErodeWalkableArea(m_rcContext, config.walkableRadius, *tile.chf))
                {
                    printf("%s Failed eroding area!                    \n", tileString);
                    continue;
                }

                // 中值滤波平滑可行走区域
                // 减少噪点和不规则区域
                if (!rcMedianFilterWalkableArea(m_rcContext, *tile.chf))
                {
                    printf("%s Failed filtering area!                  \n", tileString);
                    continue;
                }

                // 构建距离场
                // 用于确定区域边界和区域合并优先级
                if (!rcBuildDistanceField(m_rcContext, *tile.chf))
                {
                    printf("%s Failed building distance field!         \n", tileString);
                    continue;
                }

                // 构建区域
                // 将可行走区域分割为连通的区域，每个区域对应一组相邻的多边形
                if (!rcBuildRegions(m_rcContext, *tile.chf, tileCfg.borderSize, tileCfg.minRegionArea, tileCfg.mergeRegionArea))
                {
                    printf("%s Failed building regions!                \n", tileString);
                    continue;
                }

                // 第七步: 构建轮廓集
                // 从区域边界生成简化的轮廓线
                tile.cset = rcAllocContourSet();
                if (!tile.cset || !rcBuildContours(m_rcContext, *tile.chf, tileCfg.maxSimplificationError, tileCfg.maxEdgeLen, *tile.cset))
                {
                    printf("%s Failed building contours!               \n", tileString);
                    continue;
                }

                // 第八步: 构建多边形网格
                // 将轮廓转换为凸多边形网格
                // build polymesh
                tile.pmesh = rcAllocPolyMesh();
                if (!tile.pmesh || !rcBuildPolyMesh(m_rcContext, *tile.cset, tileCfg.maxVertsPerPoly, *tile.pmesh))
                {
                    printf("%s Failed building polymesh!               \n", tileString);
                    continue;
                }

                // 第九步: 构建细节网格
                // 细节网格提供更精确的高度信息，用于实际寻路时的高度采样
                tile.dmesh = rcAllocPolyMeshDetail();
                if (!tile.dmesh || !rcBuildPolyMeshDetail(m_rcContext, *tile.pmesh, *tile.chf, tileCfg.detailSampleDist, tileCfg.detailSampleMaxError, *tile.dmesh))
                {
                    printf("%s Failed building polymesh detail!        \n", tileString);
                    continue;
                }

                // 第十步: 释放中间数据结构
                // 这些数据不再需要，释放内存
                // free those up
                // we may want to keep them in the future for debug
                // but right now, we don't have the code to merge them
                rcFreeHeightField(tile.solid);
                tile.solid = nullptr;
                rcFreeCompactHeightfield(tile.chf);
                tile.chf = nullptr;
                rcFreeContourSet(tile.cset);
                tile.cset = nullptr;

                // 将生成的网格添加到合并队列
                pmmerge[nmerge] = tile.pmesh;
                dmmerge[nmerge] = tile.dmesh;
                nmerge++;
            }
        }

        // 第十一步: 合并所有子瓦片的多边形网格
        // 分配合并后的多边形网格
        iv.polyMesh = rcAllocPolyMesh();
        if (!iv.polyMesh)
        {
            printf("%s alloc iv.polyMesh FAILED!\n", tileString);
            delete[] pmmerge;
            delete[] dmmerge;
            delete[] tiles;
            return;
        }
        // 合并所有子瓦片的多边形网格为一个完整的网格
        rcMergePolyMeshes(m_rcContext, pmmerge, nmerge, *iv.polyMesh);

        // 分配合并后的细节网格
        iv.polyMeshDetail = rcAllocPolyMeshDetail();
        if (!iv.polyMeshDetail)
        {
            printf("%s alloc m_dmesh FAILED!\n", tileString);
            delete[] pmmerge;
            delete[] dmmerge;
            delete[] tiles;
            return;
        }
        // 合并所有子瓦片的细节网格
        rcMergePolyMeshDetails(m_rcContext, dmmerge, nmerge, *iv.polyMeshDetail);

        // 释放临时数组
        // free things up
        delete[] pmmerge;
        delete[] dmmerge;
        delete[] tiles;

        // 第十二步: 设置多边形标志
        // 根据区域类型设置多边形的导航标志
        // 这些标志用于寻路时过滤可通行的区域
        // set polygons as walkable
        // TODO: special flags for DYNAMIC polygons, ie surfaces that can be turned on and off
        for (int i = 0; i < iv.polyMesh->npolys; ++i)
        {
            // 检查区域类型(保留低6位区域掩码)
            if (uint8 area = iv.polyMesh->areas[i] & NAV_AREA_ALL_MASK)
            {
                // 根据区域值计算标志位
                // 标志位用于寻路时的区域过滤
                if (area >= NAV_AREA_MIN_VALUE)
                    iv.polyMesh->flags[i] = 1 << (NAV_AREA_MAX_VALUE - area);
                else
                    iv.polyMesh->flags[i] = NAV_GROUND; // TODO: these will be dynamic in future
            }
        }

        // 第十三步: 设置导航网格创建参数
        // 配置Detour导航网格的所有必要参数
        // setup mesh parameters
        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));

        // 设置多边形网格数据
        params.verts = iv.polyMesh->verts;              // 顶点数组
        params.vertCount = iv.polyMesh->nverts;          // 顶点数量
        params.polys = iv.polyMesh->polys;               // 多边形索引数组
        params.polyAreas = iv.polyMesh->areas;           // 多边形区域类型
        params.polyFlags = iv.polyMesh->flags;           // 多边形标志
        params.polyCount = iv.polyMesh->npolys;          // 多边形数量
        params.nvp = iv.polyMesh->nvp;                   // 每个多边形的顶点数

        // 设置细节网格数据
        params.detailMeshes = iv.polyMeshDetail->meshes;         // 细节网格信息
        params.detailVerts = iv.polyMeshDetail->verts;           // 细节顶点
        params.detailVertsCount = iv.polyMeshDetail->nverts;     // 细节顶点数量
        params.detailTris = iv.polyMeshDetail->tris;             // 细节三角形
        params.detailTriCount = iv.polyMeshDetail->ntris;        // 细节三角形数量

        // 设置OffMesh连接(自定义路径点，如传送门、电梯等)
        params.offMeshConVerts = meshData.offMeshConnections.getCArray();
        params.offMeshConCount = meshData.offMeshConnections.size()/6;  // 每个连接6个浮点数(起点+终点)
        params.offMeshConRad = meshData.offMeshConnectionRads.getCArray();     // 连接半径
        params.offMeshConDir = meshData.offMeshConnectionDirs.getCArray();      // 连接方向(单向/双向)
        params.offMeshConAreas = meshData.offMeshConnectionsAreas.getCArray();  // 连接区域类型
        params.offMeshConFlags = meshData.offMeshConnectionsFlags.getCArray();  // 连接标志

        // 设置角色物理参数(转换为世界单位)
        params.walkableHeight = BASE_UNIT_DIM*config.walkableHeight;    // 角色高度
        params.walkableRadius = BASE_UNIT_DIM*config.walkableRadius;    // 角色半径
        params.walkableClimb = BASE_UNIT_DIM*config.walkableClimb;      // 可攀爬高度(需小于角色高度!)
        // keep less that walkableHeight (aka agent height)!

        // 计算瓦片在导航网格中的位置
        params.tileX = (((bmin[0] + bmax[0]) / 2) - navMesh->getParams()->orig[0]) / GRID_SIZE;
        params.tileY = (((bmin[2] + bmax[2]) / 2) - navMesh->getParams()->orig[2]) / GRID_SIZE;

        // 设置边界和网格参数
        rcVcopy(params.bmin, bmin);
        rcVcopy(params.bmax, bmax);
        params.cs = config.cs;              // 单元格大小
        params.ch = config.ch;              // 单元格高度
        params.tileLayer = 0;               // 瓦片层(用于多层导航网格)
        params.buildBvTree = true;          // 构建包围盒树(加速空间查询)

        // 用于存储最终的导航网格数据
        // will hold final navmesh
        unsigned char* navData = nullptr;
        int navDataSize = 0;

        // 第十四步: 创建并写入导航网格瓦片
        // 使用do-while(false)模式实现错误处理，任何失败都会跳出到结尾
        do
        {
            // 参数验证
            // these values are checked within dtCreateNavMeshData - handle them here
            // so we have a clear error message

            // 检查每个多边形的顶点数是否有效
            if (params.nvp > DT_VERTS_PER_POLYGON)
            {
                printf("%s Invalid verts-per-polygon value!        \n", tileString);
                break;
            }
            // 检查顶点数是否超过限制(65535)
            if (params.vertCount >= 0xffff)
            {
                printf("%s Too many vertices!                      \n", tileString);
                break;
            }
            // 检查是否有有效顶点
            // 这种情况通常发生在相邻瓦片有模型加载但未延伸到当前瓦片
            if (!params.vertCount || !params.verts)
            {
                // occurs mostly when adjacent tiles have models
                // loaded but those models don't span into this tile

                // message is an annoyance
                //printf("%sNo vertices to build tile!              \n", tileString);
                break;
            }
            // 检查是否有有效多边形
            // 平坦瓦片没有实际几何体，不需要构建
            if (!params.polyCount || !params.polys)
            {
                // we have flat tiles with no actual geometry - don't build those, its useless
                // keep in mind that we do output those into debug info
                printf("%s No polygons to build on tile!              \n", tileString);
                break;
            }
            // 检查细节网格数据
            if (!params.detailMeshes || !params.detailVerts || !params.detailTris)
            {
                printf("%s No detail mesh to build tile!           \n", tileString);
                break;
            }

            // 创建导航网格数据
            printf("%s Building navmesh tile...\n", tileString);
            if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
            {
                printf("%s Failed building navmesh tile!           \n", tileString);
                break;
            }

            // 将瓦片添加到导航网格
            dtTileRef tileRef = 0;
            printf("%s Adding tile to navmesh...\n", tileString);
            // DT_TILE_FREE_DATA标志告诉Detour在removeTile时释放内存
            // DT_TILE_FREE_DATA tells detour to unallocate memory when the tile
            // is removed via removeTile()
            dtStatus dtResult = navMesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, &tileRef);
            if (!tileRef || dtResult != DT_SUCCESS)
            {
                printf("%s Failed adding tile to navmesh!           \n", tileString);
                break;
            }

            // 写入.mmtile文件
            // 文件名格式: mmaps/XXXYYZZ.mmtile (XXX=地图ID, YY=tileY, ZZ=tileX)
            // file output
            char fileName[255];
            sprintf(fileName, "mmaps/%03u%02i%02i.mmtile", mapID, tileY, tileX);
            FILE* file = fopen(fileName, "wb");
            if (!file)
            {
                char message[1024];
                sprintf(message, "[Map %03i] Failed to open %s for writing!\n", mapID, fileName);
                perror(message);
                navMesh->removeTile(tileRef, nullptr, nullptr);
                break;
            }

            printf("%s Writing to file...\n", tileString);

            // 写入瓦片文件头
            // write header
            MmapTileHeader header;
            header.usesLiquids = m_terrainBuilder->usesLiquids();  // 是否使用液体
            header.size = uint32(navDataSize);                      // 数据大小
            fwrite(&header, sizeof(MmapTileHeader), 1, file);

            /*
            // 调试: 输出多边形数量
            dtMeshHeader* navDataHeader = (dtMeshHeader*)navData;
            printf("Poly count: %d\n", navDataHeader->polyCount);
            */

            // 写入导航网格数据
            // write data
            fwrite(navData, sizeof(unsigned char), navDataSize, file);
            fclose(file);

            // 瓦片已写入磁盘，可以卸载内存中的副本
            // now that tile is written to disk, we can unload it
            navMesh->removeTile(tileRef, nullptr, nullptr);
        }
        while (false);

        // 第十五步: 可选的调试输出
        // 生成OBJ文件用于可视化导航网格
        if (m_debugOutput)
        {
            // 恢复边界填充，使调试可视化正确显示
            // restore padding so that the debug visualization is correct
            for (int i = 0; i < iv.polyMesh->nverts; ++i)
            {
                unsigned short* v = &iv.polyMesh->verts[i*3];
                v[0] += (unsigned short)config.borderSize;
                v[2] += (unsigned short)config.borderSize;
            }

            // 生成OBJ文件和中间值文件
            iv.generateObjFile(mapID, tileX, tileY, meshData);
            iv.writeIV(mapID, tileX, tileY);
        }
    }

    /**
     * @brief 获取瓦片的边界坐标
     *
     * 计算指定瓦片在世界坐标系中的边界范围。
     * 边界用于导航网格生成和空间划分。
     *
     * @param tileX 瓦片X坐标(0-63)
     * @param tileY 瓦片Y坐标(0-63)
     * @param verts 顶点数组(可选)，用于计算精确的高度边界
     * @param vertCount 顶点数量
     * @param bmin [out] 最小边界坐标
     * @param bmax [out] 最大边界坐标
     *
     * 坐标系统说明:
     * - 世界坐标原点在地图中心
     * - 瓦片索引(0,0)位于地图左上角
     * - 瓦片索引(63,63)位于地图右下角
     * - GRID_SIZE = 533.333游戏单位
     */
    void MapBuilder::getTileBounds(uint32 tileX, uint32 tileY, float* verts, int vertCount, float* bmin, float* bmax) const
    {
        // 计算高度边界(从顶点数据)
        // this is for elevation
        if (verts && vertCount)
            rcCalcBounds(verts, vertCount, bmin, bmax);
        else
        {
            // 没有顶点数据时，高度边界设为无限
            bmin[1] = FLT_MIN;
            bmax[1] = FLT_MAX;
        }

        // 计算XY平面边界(基于瓦片坐标)
        // 世界坐标公式: (32 - tileIndex) * GRID_SIZE
        // this is for width and depth
        bmax[0] = (32 - int(tileX)) * GRID_SIZE;
        bmax[2] = (32 - int(tileY)) * GRID_SIZE;
        bmin[0] = bmax[0] - GRID_SIZE;
        bmin[2] = bmax[2] - GRID_SIZE;
    }

    /**
     * @brief 判断是否应该跳过指定地图
     *
     * 根据命令行参数和地图类型判断是否应该跳过该地图的生成。
     *
     * @param mapID 地图ID
     * @return true表示应该跳过，false表示需要处理
     *
     * 跳过规则(按优先级):
     * 1. 如果指定了特定地图ID，只处理该地图
     * 2. 如果设置了跳过大陆标志，跳过大陆地图
     * 3. 如果设置了跳过垃圾地图标志，跳过测试/开发地图
     * 4. 如果设置了跳过战场标志，跳过战场地图
     */
    bool MapBuilder::shouldSkipMap(uint32 mapID) const
    {
        // 如果指定了特定地图ID，只处理该地图
        if (m_mapid >= 0)
            return static_cast<uint32>(m_mapid) != mapID;

        // 检查是否应该跳过大陆地图
        if (m_skipContinents)
            if (isContinentMap(mapID))
                return true;

        // 检查是否应该跳过垃圾/测试地图
        if (m_skipJunkMaps)
            switch (mapID)
            {
                case 13:    // test.wdt - 测试地图
                case 25:    // ScottTest.wdt - Scott测试地图
                case 29:    // Test.wdt - 测试地图
                case 42:    // Colin.wdt - Colin测试地图
                case 169:   // EmeraldDream.wdt - 翡翠梦魇(未使用，且非常大)
                case 451:   // development.wdt - 开发地图
                case 573:   // ExteriorTest.wdt - 外部测试地图
                case 597:   // CraigTest.wdt - Craig测试地图
                case 605:   // development_nonweighted.wdt - 非加权开发地图
                case 606:   // QA_DVD.wdt - QA测试地图
                    return true;
                default:
                    // 跳过运输工具地图(船只、飞艇等)
                    if (isTransportMap(mapID))
                        return true;
                    break;
            }

        // 检查是否应该跳过战场地图
        if (m_skipBattlegrounds)
            switch (mapID)
            {
                case 30:    // AV - 奥特兰克山谷
                case 37:    // ? - 未知战场
                case 489:   // WSG - 战歌峡谷
                case 529:   // AB - 阿拉希盆地
                case 566:   // EotS - 风暴之眼
                case 607:   // SotA - 远祖滩头
                case 628:   // IoC - 征服之岛
                    return true;
                default:
                    break;
            }

        return false;
    }

    /**
     * @brief 判断是否为运输工具地图
     *
     * 运输工具地图是指船只、飞艇等可移动的载具内部地图。
     * 这些地图通常不需要生成导航网格，因为它们会随着运输工具移动。
     *
     * @param mapID 地图ID
     * @return true表示是运输工具地图，false表示不是
     */
    bool MapBuilder::isTransportMap(uint32 mapID) const
    {
        switch (mapID)
        {
            // transport maps - 运输工具地图
            case 582:   // 船只/飞艇等运输工具
            case 584:
            case 586:
            case 587:
            case 588:
            case 589:
            case 590:
            case 591:
            case 592:
            case 593:
            case 594:
            case 596:
            case 610:
            case 612:
            case 613:
            case 614:
            case 620:
            case 621:
            case 622:
            case 623:
            case 641:
            case 642:
            case 647:
            case 672:
            case 673:
            case 712:
            case 713:
            case 718:
                return true;
            default:
                return false;
        }
    }

    /**
     * @brief 判断是否为大陆地图
     *
     * 大陆地图是指主要的开放世界区域，包括:
     * - 东部王国(0)
     * - 卡利姆多(1)
     * - 外域(530)
     * - 诺森德(571)
     *
     * 这些地图通常较大，生成时间较长。
     *
     * @param mapID 地图ID
     * @return true表示是大陆地图，false表示不是
     */
    bool MapBuilder::isContinentMap(uint32 mapID) const
    {
        switch (mapID)
        {
            case 0:     // 东部王国 (Eastern Kingdoms)
            case 1:     // 卡利姆多 (Kalimdor)
            case 530:   // 外域 (Outland)
            case 571:   // 诺森德 (Northrend)
                return true;
            default:
                return false;
        }
    }

    /**
     * @brief 判断是否应该跳过指定瓦片
     *
     * 检查瓦片文件是否已存在且版本匹配，如果是则跳过重新生成。
     * 这允许增量生成，避免重复处理已有的有效瓦片。
     *
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @return true表示应该跳过，false表示需要生成
     *
     * 检查内容:
     * 1. 文件是否存在
     * 2. 魔数(MMAP_MAGIC)是否匹配
     * 3. Detour版本是否匹配
     * 4. MMap版本是否匹配
     */
    bool TileBuilder::shouldSkipTile(uint32 mapID, uint32 tileX, uint32 tileY) const
    {
        // 构建瓦片文件名
        char fileName[255];
        sprintf(fileName, "mmaps/%03u%02i%02i.mmtile", mapID, tileY, tileX);
        FILE* file = fopen(fileName, "rb");
        if (!file)
            return false;  // 文件不存在，需要生成

        // 读取瓦片头部信息
        MmapTileHeader header;
        int count = fread(&header, sizeof(MmapTileHeader), 1, file);
        fclose(file);
        if (count != 1)
            return false;  // 读取失败，需要重新生成

        // 检查魔数和版本号
        if (header.mmapMagic != MMAP_MAGIC || header.dtVersion != uint32(DT_NAVMESH_VERSION))
            return false;  // 版本不匹配，需要重新生成

        if (header.mmapVersion != MMAP_VERSION)
            return false;  // MMap版本不匹配，需要重新生成

        return true;  // 所有检查通过，可以跳过
    }

    /**
     * @brief 获取地图特定的Recast配置
     *
     * 根据地图ID返回定制的Recast配置参数。
     * 不同地图可能需要不同的参数以获得最佳的导航网格效果。
     *
     * @param mapID 地图ID
     * @param bmin 最小边界
     * @param bmax 最大边界
     * @param tileConfig 瓦片配置
     * @return 配置好的rcConfig结构
     *
     * 配置参数说明:
     * - walkableSlopeAngle: 最大可行走坡度角度
     * - walkableRadius: 角色半径(用于腐蚀)
     * - walkableHeight: 角色高度
     * - walkableClimb: 最大可攀爬高度
     * - maxSimplificationError: 轮廓简化误差
     * - detailSampleDist: 细节采样距离
     *
     * 特殊地图配置:
     * - 562(刀锋山竞技场): walkableRadius=0，允许角色在绳索上行走
     * - 48(黑暗深渊): ch*=2，减少地下层级的出现
     */
    rcConfig MapBuilder::GetMapSpecificConfig(uint32 mapID, float bmin[3], float bmax[3], const TileConfig &tileConfig) const
    {
        rcConfig config;
        memset(&config, 0, sizeof(rcConfig));

        // 设置边界
        rcVcopy(config.bmin, bmin);
        rcVcopy(config.bmax, bmax);

        // 基本网格参数
        config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;  // 每个多边形最大顶点数(通常6)
        config.cs = tileConfig.BASE_UNIT_DIM;           // 单元格水平尺寸
        config.ch = tileConfig.BASE_UNIT_DIM;           // 单元格垂直尺寸

        // 可行走坡度角度配置
        // 将这两个角度设为相同值可以大大减少多边形数量
        // 55度是最小建议值，70度可能也可以(注意闪烁技能使用mmap)
        // 85度对玩家来说太陡了
        // Keeping these 2 slope angles the same reduces a lot the number of polys.
        // 55 should be the minimum, maybe 70 is ok (keep in mind blink uses mmaps), 85 is too much for players
        config.walkableSlopeAngle = m_maxWalkableAngle ? *m_maxWalkableAngle : 55;
        config.walkableSlopeAngleNotSteep = m_maxWalkableAngleNotSteep ? *m_maxWalkableAngleNotSteep : 55;

        // 瓦片和边界参数
        config.tileSize = tileConfig.VERTEX_PER_TILE;
        config.walkableRadius = m_bigBaseUnit ? 1 : 2;          // 角色半径(单元格数)
        config.borderSize = config.walkableRadius + 3;          // 边界填充大小
        config.maxEdgeLen = tileConfig.VERTEX_PER_TILE + 1;     // 最大边缘长度(大于瓦片尺寸即可)
        // anything bigger than tileSize

        // 高度相关参数
        config.walkableHeight = m_bigBaseUnit ? 3 : 6;          // 角色高度(单元格数)
        // 攀爬高度配置:
        // >= 3|6 允许NPC跨过某些栅栏
        // >= 4|8 允许NPC跨过所有栅栏
        // a value >= 3|6 allows npcs to walk over some fences
        // a value >= 4|8 allows npcs to walk over all fences
        config.walkableClimb = m_bigBaseUnit ? 3 : 6;

        // 区域参数
        config.minRegionArea = rcSqr(60);       // 最小区域面积
        config.mergeRegionArea = rcSqr(50);     // 合并区域面积阈值

        // 简化和细节参数
        config.maxSimplificationError = 1.8f;   // 最大简化误差(消除大多数锯齿边缘)
        config.detailSampleDist = config.cs * 16;       // 细节采样距离
        config.detailSampleMaxError = config.ch * 1;    // 细节采样最大误差

        // 地图特定配置覆盖
        switch (mapID)
        {
            // Blade's Edge Arena - 刀锋山竞技场
            case 562:
                // This allows to walk on the ropes to the pillars
                // 设置半径为0，允许角色在连接柱子的绳索上行走
                config.walkableRadius = 0;
                break;
            // Blackfathom Deeps - 黑暗深渊
            case 48:
                // Reduce the chance to have underground levels
                // 增加单元格高度，减少地下层级的出现概率
                config.ch *= 2;
                break;
            default:
                break;
        }

        return config;
    }

    /**
     * @brief 计算完成百分比
     *
     * 根据已处理的瓦片数和总瓦片数计算进度百分比。
     *
     * @param totalTiles 总瓦片数量
     * @param totalTilesBuilt 已构建的瓦片数量
     * @return 完成百分比(0-100)
     */
    uint32 MapBuilder::percentageDone(uint32 totalTiles, uint32 totalTilesBuilt) const
    {
        if (totalTiles)
            return totalTilesBuilt * 100 / totalTiles;

        return 0;
    }

    /**
     * @brief 获取当前完成百分比
     *
     * 使用成员变量计算当前的生成进度。
     *
     * @return 当前完成百分比(0-100)
     *
     * 调用时机:
     * - buildTile输出进度信息时
     */
    uint32 MapBuilder::currentPercentageDone() const
    {
        return percentageDone(m_totalTiles, m_totalTilesProcessed);
    }

}
