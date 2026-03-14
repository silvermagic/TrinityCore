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
 * @file MMapManager.cpp
 * @brief MMap管理器实现
 *
 * 本文件实现了MMapManager类，负责导航网格数据的加载、管理和查询。
 * 使用Detour库实现导航功能，支持瓦片级别的按需加载。
 *
 * 主要实现：
 * - 地图数据加载和卸载
 * - 瓦片管理
 * - 实例化查询对象管理
 * - 文件格式验证
 */

#include "MMapManager.h"
#include "Errors.h"
#include "Log.h"
#include "MapDefines.h"

namespace MMAP
{
    /// 地图参数文件路径格式：{basePath}mmaps/{mapId:03}.mmap
    constexpr char MAP_FILE_NAME_FORMAT[] = "{}mmaps/{:03}.mmap";

    /// 瓦片文件路径格式：{basePath}mmaps/{mapId:03}{x:02}{y:02}.mmtile
    constexpr char TILE_FILE_NAME_FORMAT[] = "{}mmaps/{:03}{:02}{:02}.mmtile";

    // ######################## MMapManager ########################

    /**
     * @brief 析构函数实现
     *
     * 遍历所有已加载的地图数据并删除。
     * 注意：如果有瓦片未正确卸载，其数据会在此过程中丢失。
     */
    MMapManager::~MMapManager()
    {
        // 释放所有地图数据对象
        for (std::pair<uint32 const, MMapData*>& loadedMMap : loadedMMaps)
            delete loadedMMap.second;

        // 此时应该没有地图被加载
        // 如果有，MMapData->mmapLoadedTiles中的瓦片实际数据将丢失
    }

    /**
     * @brief 初始化非线程安全模式
     *
     * 预分配所有地图槽位，避免运行时动态分配。
     * 在非线程安全模式下，尝试加载列表外的地图将导致崩溃。
     */
    void MMapManager::InitializeThreadUnsafe(const std::vector<uint32>& mapIds)
    {
        // 调用者必须传入所有将在VMapManager2生命周期中使用的mapId列表
        // 预先在哈希表中插入所有键，避免运行时插入
        for (uint32 const& mapId : mapIds)
            loadedMMaps.insert(MMapDataSet::value_type(mapId, nullptr));

        // 标记为非线程安全环境
        thread_safe_environment = false;
    }

    /**
     * @brief 获取地图数据的迭代器
     *
     * 查找并返回指定地图的数据迭代器。
     * 如果找到但数据为空，返回end()。
     */
    MMapDataSet::const_iterator MMapManager::GetMMapData(uint32 mapId) const
    {
        // 查找地图ID
        MMapDataSet::const_iterator itr = loadedMMaps.find(mapId);

        // 如果找到但数据为空，返回end()
        if (itr != loadedMMaps.cend() && !itr->second)
            itr = loadedMMaps.cend();

        return itr;
    }

    /**
     * @brief 加载地图基础数据
     *
     * 加载.mmap文件，初始化dtNavMesh对象。
     * 如果数据已存在则直接返回成功。
     */
    bool MMapManager::loadMapData(std::string const& basePath, uint32 mapId)
    {
        // 检查地图数据是否已加载
        MMapDataSet::iterator itr = loadedMMaps.find(mapId);
        if (itr != loadedMMaps.end())
        {
            if (itr->second)
                return true;  // 数据已存在
        }
        else
        {
            // 地图槽位不存在
            if (thread_safe_environment)
            {
                // 线程安全模式：动态创建槽位
                itr = loadedMMaps.insert(MMapDataSet::value_type(mapId, nullptr)).first;
            }
            else
            {
                // 非线程安全模式：禁止动态创建
                ABORT_MSG("Invalid mapId %u passed to MMapManager after startup in thread unsafe environment", mapId);
            }
        }

        // 加载并初始化dtNavMesh - 从文件读取参数
        std::string fileName = Trinity::StringFormat(MAP_FILE_NAME_FORMAT, basePath, mapId);
        FILE* file = fopen(fileName.c_str(), "rb");
        if (!file)
        {
            TC_LOG_DEBUG("maps", "MMAP:loadMapData: Error: Could not open mmap file '{}'", fileName);
            return false;
        }

        // 读取导航网格参数
        dtNavMeshParams params;
        uint32 count = uint32(fread(&params, sizeof(dtNavMeshParams), 1, file));
        fclose(file);
        if (count != 1)
        {
            TC_LOG_DEBUG("maps", "MMAP:loadMapData: Error: Could not read params from file '{}'", fileName);
            return false;
        }

        // 分配并初始化导航网格
        dtNavMesh* mesh = dtAllocNavMesh();
        ASSERT(mesh);
        if (dtStatusFailed(mesh->init(&params)))
        {
            dtFreeNavMesh(mesh);
            TC_LOG_ERROR("maps", "MMAP:loadMapData: Failed to initialize dtNavMesh for mmap {:03} from file {}", mapId, fileName);
            return false;
        }

        TC_LOG_DEBUG("maps", "MMAP:loadMapData: Loaded {:03}.mmap", mapId);

        // 创建MMapData对象并存储
        MMapData* mmap_data = new MMapData(mesh);

        itr->second = mmap_data;
        return true;
    }

    /**
     * @brief 将瓦片坐标打包为单一ID
     *
     * 使用位运算将X和Y坐标组合成一个32位整数。
     * 格式：高16位=X坐标，低16位=Y坐标
     */
    uint32 MMapManager::packTileID(int32 x, int32 y)
    {
        return uint32(x << 16 | y);
    }

    /**
     * @brief 加载地图瓦片
     *
     * 完整的瓦片加载流程：
     * 1. 确保地图基础数据已加载
     * 2. 检查瓦片是否已存在
     * 3. 读取并验证瓦片文件
     * 4. 将瓦片数据添加到导航网格
     */
    bool MMapManager::loadMap(std::string const& basePath, uint32 mapId, int32 x, int32 y)
    {
        // 确保mmap已加载并准备好加载瓦片
        if (!loadMapData(basePath, mapId))
            return false;

        // 获取此mmap数据
        MMapData* mmap = loadedMMaps[mapId];
        ASSERT(mmap->navMesh);

        // 检查此瓦片是否已加载
        uint32 packedGridPos = packTileID(x, y);
        if (mmap->loadedTileRefs.find(packedGridPos) != mmap->loadedTileRefs.end())
            return false;  // 瓦片已存在

        // 加载此瓦片 :: mmaps/MMMXXYY.mmtile
        std::string fileName = Trinity::StringFormat(TILE_FILE_NAME_FORMAT, basePath, mapId, x, y);
        FILE* file = fopen(fileName.c_str(), "rb");
        if (!file)
        {
            TC_LOG_DEBUG("maps", "MMAP:loadMap: Could not open mmtile file '{}'", fileName);
            return false;
        }

        // 读取文件头
        MmapTileHeader fileHeader;
        if (fread(&fileHeader, sizeof(MmapTileHeader), 1, file) != 1 || fileHeader.mmapMagic != MMAP_MAGIC)
        {
            TC_LOG_ERROR("maps", "MMAP:loadMap: Bad header in mmap {:03}{:02}{:02}.mmtile", mapId, x, y);
            fclose(file);
            return false;
        }

        // 验证版本号
        if (fileHeader.mmapVersion != MMAP_VERSION)
        {
            TC_LOG_ERROR("maps", "MMAP:loadMap: {:03}{:02}{:02}.mmtile was built with generator v{}, expected v{}",
                mapId, x, y, fileHeader.mmapVersion, MMAP_VERSION);
            fclose(file);
            return false;
        }

        // 验证数据大小
        long pos = ftell(file);
        fseek(file, 0, SEEK_END);
        if (pos < 0 || static_cast<int32>(fileHeader.size) > ftell(file) - pos)
        {
            TC_LOG_ERROR("maps", "MMAP:loadMap: {:03}{:02}{:02}.mmtile has corrupted data size", mapId, x, y);
            fclose(file);
            return false;
        }

        fseek(file, pos, SEEK_SET);

        // 分配内存并读取瓦片数据
        unsigned char* data = (unsigned char*)dtAlloc(fileHeader.size, DT_ALLOC_PERM);
        ASSERT(data);

        size_t result = fread(data, fileHeader.size, 1, file);
        if (!result)
        {
            TC_LOG_ERROR("maps", "MMAP:loadMap: Bad header or data in mmap {:03}{:02}{:02}.mmtile", mapId, x, y);
            fclose(file);
            return false;
        }

        fclose(file);

        dtMeshHeader* header = (dtMeshHeader*)data;
        dtTileRef tileRef = 0;

        // 将瓦片添加到导航网格
        // 注意：data的内存现在由detour管理，瓦片移除时会自动释放
        if (dtStatusSucceed(mmap->navMesh->addTile(data, fileHeader.size, DT_TILE_FREE_DATA, 0, &tileRef)))
        {
            // 记录瓦片引用
            mmap->loadedTileRefs.insert(std::pair<uint32, dtTileRef>(packedGridPos, tileRef));
            ++loadedTiles;
            TC_LOG_DEBUG("maps", "MMAP:loadMap: Loaded mmtile {:03}[{:02}, {:02}] into {:03}[{:02}, {:02}]", mapId, x, y, mapId, header->x, header->y);
            return true;
        }
        else
        {
            TC_LOG_ERROR("maps", "MMAP:loadMap: Could not load {:03}{:02}{:02}.mmtile into navmesh", mapId, x, y);
            dtFree(data);
            return false;
        }
    }

    /**
     * @brief 为地图实例创建导航查询对象
     *
     * 每个地图实例需要独立的查询对象以保证线程安全。
     * 如果实例已存在则直接返回成功。
     */
    bool MMapManager::loadMapInstance(std::string const& basePath, uint32 mapId, uint32 instanceId)
    {
        // 确保地图数据已加载
        if (!loadMapData(basePath, mapId))
            return false;

        MMapData* mmap = loadedMMaps[mapId];

        // 尝试插入新的查询对象槽位
        auto [queryItr, inserted] = mmap->navMeshQueries.try_emplace(instanceId, nullptr);
        if (!inserted)
            return true;  // 实例已存在

        // 分配并初始化导航网格查询对象
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        ASSERT(query);
        if (dtStatusFailed(query->init(mmap->navMesh, 1024)))
        {
            dtFreeNavMeshQuery(query);
            mmap->navMeshQueries.erase(queryItr);
            TC_LOG_ERROR("maps", "MMAP:GetNavMeshQuery: Failed to initialize dtNavMeshQuery for mapId {:03} instanceId {}", mapId, instanceId);
            return false;
        }

        TC_LOG_DEBUG("maps", "MMAP:GetNavMeshQuery: created dtNavMeshQuery for mapId {:03} instanceId {}", mapId, instanceId);
        queryItr->second = query;
        return true;
    }

    /**
     * @brief 卸载单个瓦片
     *
     * 从导航网格中移除指定瓦片。
     * 如果移除失败可能导致内存泄漏。
     */
    bool MMapManager::unloadMap(uint32 mapId, int32 x, int32 y)
    {
        // 检查地图是否已加载
        MMapDataSet::const_iterator itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
        {
            // 文件可能不存在，因此未加载
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh map. {:03}{:02}{:02}.mmtile", mapId, x, y);
            return false;
        }

        MMapData* mmap = itr->second;

        // 检查瓦片是否已加载
        uint32 packedGridPos = packTileID(x, y);
        if (mmap->loadedTileRefs.find(packedGridPos) == mmap->loadedTileRefs.end())
        {
            // 文件可能不存在，因此未加载
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh tile. {:03}{:02}{:02}.mmtile", mapId, x, y);
            return false;
        }

        dtTileRef tileRef = mmap->loadedTileRefs[packedGridPos];

        // 从导航网格移除瓦片
        if (dtStatusFailed(mmap->navMesh->removeTile(tileRef, nullptr, nullptr)))
        {
            // 这实际上是内存泄漏
            // 如果网格稍后重新加载，dtNavMesh::addTile将返回错误但不会使用额外内存
            // 我们无法从此错误恢复 - 断言退出
            TC_LOG_ERROR("maps", "MMAP:unloadMap: Could not unload {:03}{:02}{:02}.mmtile from navmesh", mapId, x, y);
            ABORT();
        }
        else
        {
            // 从已加载列表中移除
            mmap->loadedTileRefs.erase(packedGridPos);
            --loadedTiles;
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded mmtile {:03}[{:02}, {:02}] from {:03}", mapId, x, y, mapId);
            return true;
        }

        return false;
    }

    /**
     * @brief 卸载整个地图
     *
     * 卸载指定地图的所有瓦片，删除MMapData对象但保留槽位。
     */
    bool MMapManager::unloadMap(uint32 mapId)
    {
        MMapDataSet::iterator itr = loadedMMaps.find(mapId);
        if (itr == loadedMMaps.end() || !itr->second)
        {
            // 文件可能不存在，因此未加载
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh map {:03}", mapId);
            return false;
        }

        // 卸载此地图的所有瓦片
        MMapData* mmap = itr->second;
        for (MMapTileSet::iterator i = mmap->loadedTileRefs.begin(); i != mmap->loadedTileRefs.end(); ++i)
        {
            // 解包坐标用于日志
            uint32 x = (i->first >> 16);
            uint32 y = (i->first & 0x0000FFFF);
            if (dtStatusFailed(mmap->navMesh->removeTile(i->second, nullptr, nullptr)))
                TC_LOG_ERROR("maps", "MMAP:unloadMap: Could not unload {:03}{:02}{:02}.mmtile from navmesh", mapId, x, y);
            else
            {
                --loadedTiles;
                TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded mmtile {:03}[{:02}, {:02}] from {:03}", mapId, x, y, mapId);
            }
        }

        // 删除MMapData对象（会自动释放navMesh和所有查询对象）
        delete mmap;
        itr->second = nullptr;  // 保留槽位，但清空指针
        TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded {:03}.mmap", mapId);

        return true;
    }

    /**
     * @brief 卸载地图实例的查询对象
     *
     * 释放指定实例的导航查询对象。
     */
    bool MMapManager::unloadMapInstance(uint32 mapId, uint32 instanceId)
    {
        // 检查地图是否已加载
        MMapDataSet::const_iterator itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
        {
            // 文件可能不存在，因此未加载
            TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Asked to unload not loaded navmesh map {:03}", mapId);
            return false;
        }

        MMapData* mmap = itr->second;

        // 查找实例的查询对象
        auto queryItr = mmap->navMeshQueries.find(instanceId);
        if (queryItr == mmap->navMeshQueries.end())
        {
            TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Asked to unload not loaded dtNavMeshQuery mapId {:03} instanceId {}", mapId, instanceId);
            return false;
        }

        // 释放查询对象
        dtFreeNavMeshQuery(queryItr->second);
        mmap->navMeshQueries.erase(queryItr);
        TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Unloaded mapId {:03} instanceId {}", mapId, instanceId);

        return true;
    }

    /**
     * @brief 获取导航网格对象
     *
     * 返回指定地图的导航网格指针。
     */
    dtNavMesh const* MMapManager::GetNavMesh(uint32 mapId)
    {
        MMapDataSet::const_iterator itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
            return nullptr;

        return itr->second->navMesh;
    }

    /**
     * @brief 获取导航网格查询对象
     *
     * 返回指定地图实例的查询对象。
     * 注意：返回的指针非线程安全，仅供单实例使用。
     */
    dtNavMeshQuery const* MMapManager::GetNavMeshQuery(uint32 mapId, uint32 instanceId)
    {
        auto itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
            return nullptr;

        auto queryItr = itr->second->navMeshQueries.find(instanceId);
        if (queryItr == itr->second->navMeshQueries.end())
            return nullptr;

        return queryItr->second;
    }
}
