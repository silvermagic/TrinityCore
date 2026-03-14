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
 * @file VMapManager2.cpp
 * @brief VMap管理器实现文件
 *
 * 本文件实现了VMapManager2类，提供地图加载、视线检测、高度计算等核心功能。
 * 详细说明见VMapManager2.h
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include "VMapManager2.h"
#include "MapTree.h"
#include "ModelInstance.h"
#include "WorldModel.h"
#include <G3D/Vector3.h>
#include "Log.h"
#include "VMapDefinitions.h"
#include "Errors.h"

using G3D::Vector3;

namespace VMAP
{
    VMapManager2::VMapManager2()
    {
        GetLiquidFlagsPtr = &GetLiquidFlagsDummy;
        IsVMAPDisabledForPtr = &IsVMAPDisabledForDummy;
        thread_safe_environment = true;
    }

    VMapManager2::~VMapManager2(void)
    {
        for (std::pair<uint32 const, StaticMapTree*>& iInstanceMapTree : iInstanceMapTrees)
        {
            delete iInstanceMapTree.second;
        }
        for (std::pair<std::string const, ManagedModel>& iLoadedModelFile : iLoadedModelFiles)
        {
            delete iLoadedModelFile.second.getModel();
        }
    }

    /**
     * @brief 初始化非线程安全环境
     * @param mapIds 所有将在VMapManager2生命周期中使用的地图ID列表
     *
     * 调用者必须传入所有将在VMapManager2生命周期中使用的地图ID列表。
     * 这允许在启动时预分配地图树槽位，避免运行时的线程同步开销。
     *
     * 调用后，thread_safe_environment被设为false，允许更高效的单线程操作。
     */
    void VMapManager2::InitializeThreadUnsafe(const std::vector<uint32>& mapIds)
    {
        // 预先插入所有地图ID的槽位
        for (uint32 const& mapId : mapIds)
            iInstanceMapTrees.insert(InstanceTreeMap::value_type(mapId, nullptr));

        // 标记为非线程安全环境
        thread_safe_environment = false;
    }

    /**
     * @brief 将世界坐标转换为内部表示
     * @param x 世界坐标X
     * @param y 世界坐标Y
     * @param z 世界坐标Z
     * @return 转换后的G3D向量
     *
     * 坐标转换公式：
     * - 内部X = 中点 - 世界X
     * - 内部Y = 中点 - 世界Y
     * - 内部Z = 世界Z
     *
     * 这是因为客户端和服务端使用不同的坐标系。
     * 中点值 = 0.5 * 64 * 533.33333 ≈ 17066.6666
     */
    Vector3 VMapManager2::convertPositionToInternalRep(float x, float y, float z) const
    {
        Vector3 pos;
        // 计算坐标系中点（64个瓦片 * 533.33333单位/瓦片 / 2）
        const float mid = 0.5f * 64.0f * 533.33333333f;
        pos.x = mid - x;
        pos.y = mid - y;
        pos.z = z;

        return pos;
    }

    /**
     * @brief 获取地图文件名
     * @param mapId 地图ID
     * @return 格式化的地图文件名（如：001.vmtree）
     *
     * 文件名格式：<3位地图ID>.vmtree
     * 地图ID使用0填充到3位数字
     */
    std::string VMapManager2::getMapFileName(unsigned int mapId)
    {
        std::stringstream fname;
        fname.width(3);
        fname << std::setfill('0') << mapId << std::string(MAP_FILENAME_EXTENSION2);

        return fname.str();
    }

    /**
     * @brief 加载地图瓦片（公共接口）
     * @param basePath 基础路径
     * @param mapId 地图ID
     * @param x 瓦片X坐标
     * @param y 瓦片Y坐标
     * @return 加载结果（VMAP_LOAD_RESULT_OK/ERROR/IGNORED）
     *
     * 这是loadMap的公共接口，会检查是否启用地图加载。
     * 如果地图加载被禁用，返回VMAP_LOAD_RESULT_IGNORED。
     */
    int VMapManager2::loadMap(char const* basePath, unsigned int mapId, int x, int y)
    {
        int result = VMAP_LOAD_RESULT_IGNORED;
        // 检查地图加载是否启用
        if (isMapLoadingEnabled())
        {
            if (_loadMap(mapId, basePath, x, y))
                result = VMAP_LOAD_RESULT_OK;
            else
                result = VMAP_LOAD_RESULT_ERROR;
        }

        return result;
    }

    /**
     * @brief 获取地图树迭代器
     * @param mapId 地图ID
     * @return 地图树迭代器，如果不存在或为空则返回end()
     *
     * 辅助函数，用于安全地获取地图树指针。
     * 如果找到的地图树为nullptr，返回end()以表示无效。
     */
    InstanceTreeMap::const_iterator VMapManager2::GetMapTree(uint32 mapId) const
    {
        InstanceTreeMap::const_iterator itr = iInstanceMapTrees.find(mapId);
        // 如果找到了但指针为空，返回end()
        if (itr != iInstanceMapTrees.cend() && !itr->second)
            itr = iInstanceMapTrees.cend();

        return itr;
    }

    /**
     * @brief 内部地图加载函数
     * @param mapId 地图ID
     * @param basePath 基础路径
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @return 加载成功返回true，失败返回false
     *
     * 执行流程：
     * 1. 查找或创建地图树槽位
     *    - 线程安全环境：动态插入新的地图树
     *    - 非线程安全环境：必须在InitializeThreadUnsafe中预先插入，否则abort
     * 2. 如果地图树未初始化，创建并初始化它
     * 3. 加载指定的瓦片
     */
    bool VMapManager2::_loadMap(uint32 mapId, const std::string& basePath, uint32 tileX, uint32 tileY)
    {
        InstanceTreeMap::iterator instanceTree = iInstanceMapTrees.find(mapId);
        if (instanceTree == iInstanceMapTrees.end())
        {
            // 在线程安全环境下，动态插入新的地图树
            if (thread_safe_environment)
                instanceTree = iInstanceMapTrees.insert(InstanceTreeMap::value_type(mapId, nullptr)).first;
            else
                // 在非线程安全环境下，mapId必须在InitializeThreadUnsafe中预先注册
                ABORT_MSG("Invalid mapId %u tile [%u, %u] passed to VMapManager2 after startup in thread unsafe environment",
                mapId, tileX, tileY);
        }

        // 如果地图树未初始化，创建并初始化它
        if (!instanceTree->second)
        {
            std::string mapFileName = getMapFileName(mapId);
            StaticMapTree* newTree = new StaticMapTree(mapId, basePath);
            // 初始化地图树（读取.vmtree文件）
            if (!newTree->InitMap(mapFileName, this))
            {
                delete newTree;
                return false;
            }
            instanceTree->second = newTree;
        }

        // 加载指定的瓦片
        return instanceTree->second->LoadMapTile(tileX, tileY, this);
    }

    /**
     * @brief 卸载整个地图
     * @param mapId 地图ID
     *
     * 卸载指定地图的所有瓦片和模型。
     * 如果卸载后没有已加载的瓦片，删除地图树实例。
     */
    void VMapManager2::unloadMap(unsigned int mapId)
    {
        InstanceTreeMap::iterator instanceTree = iInstanceMapTrees.find(mapId);
        if (instanceTree != iInstanceMapTrees.end() && instanceTree->second)
        {
            // 卸载所有瓦片和模型引用
            instanceTree->second->UnloadMap(this);
            // 如果没有已加载的瓦片，删除地图树
            if (instanceTree->second->numLoadedTiles() == 0)
            {
                delete instanceTree->second;
                instanceTree->second = nullptr;
            }
        }
    }

    /**
     * @brief 卸载地图瓦片
     * @param mapId 地图ID
     * @param x 瓦片X坐标
     * @param y 瓦片Y坐标
     *
     * 卸载指定地图的单个瓦片。
     * 如果卸载后地图没有已加载的瓦片，删除地图树实例。
     */
    void VMapManager2::unloadMap(unsigned int mapId, int x, int y)
    {
        InstanceTreeMap::iterator instanceTree = iInstanceMapTrees.find(mapId);
        if (instanceTree != iInstanceMapTrees.end() && instanceTree->second)
        {
            // 卸载指定瓦片
            instanceTree->second->UnloadMapTile(x, y, this);
            // 如果没有已加载的瓦片，删除地图树
            if (instanceTree->second->numLoadedTiles() == 0)
            {
                delete instanceTree->second;
                instanceTree->second = nullptr;
            }
        }
    }

    /**
     * @brief 检查两点之间是否有视线遮挡
     * @param mapId 地图ID
     * @param x1 起点X坐标
     * @param y1 起点Y坐标
     * @param z1 起点Z坐标
     * @param x2 终点X坐标
     * @param y2 终点Y坐标
     * @param z2 终点Z坐标
     * @param ignoreFlags 忽略的模型标志
     * @return 有视线返回true，被遮挡返回false
     *
     * 执行流程：
     * 1. 检查视线计算是否启用
     * 2. 检查该地图的视线功能是否被禁用
     * 3. 转换坐标到内部表示
     * 4. 调用地图树的视线检测
     *
     * 性能优化：
     * - 如果两点相同，直接返回true
     * - 提前检查禁用标志避免不必要的计算
     */
    bool VMapManager2::isInLineOfSight(unsigned int mapId, float x1, float y1, float z1, float x2, float y2, float z2, ModelIgnoreFlags ignoreFlags)
    {
        // 检查视线计算是否启用，以及该地图的LOS是否被禁用
        if (!isLineOfSightCalcEnabled() || IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_LOS))
            return true;

        InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
        if (instanceTree != iInstanceMapTrees.end())
        {
            // 转换坐标到内部表示
            Vector3 pos1 = convertPositionToInternalRep(x1, y1, z1);
            Vector3 pos2 = convertPositionToInternalRep(x2, y2, z2);
            // 如果两点相同，直接返回true
            if (pos1 != pos2)
            {
                return instanceTree->second->isInLineOfSight(pos1, pos2, ignoreFlags);
            }
        }

        return true;
    }

    /**
     * @brief 获取射线命中位置
     * @param mapId 地图ID
     * @param x1 起点X坐标
     * @param y1 起点Y坐标
     * @param z1 起点Z坐标
     * @param x2 终点X坐标
     * @param y2 终点Y坐标
     * @param z2 终点Z坐标
     * @param rx 输出：命中点X坐标
     * @param ry 输出：命中点Y坐标
     * @param rz 输出：命中点Z坐标
     * @param modifyDist 距离修正值
     * @return 命中物体返回true，否则返回false（此时输出坐标为终点）
     *
     * 用于移动碰撞检测，返回射线与物体的第一个交点。
     * modifyDist用于调整碰撞点位置（如留出一点间隙）。
     */
    bool VMapManager2::getObjectHitPos(unsigned int mapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float &ry, float& rz, float modifyDist)
    {
        if (isLineOfSightCalcEnabled() && !IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_LOS))
        {
            InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
            if (instanceTree != iInstanceMapTrees.end())
            {
                // 转换坐标到内部表示
                Vector3 pos1 = convertPositionToInternalRep(x1, y1, z1);
                Vector3 pos2 = convertPositionToInternalRep(x2, y2, z2);
                Vector3 resultPos;
                bool result = instanceTree->second->getObjectHitPos(pos1, pos2, resultPos, modifyDist);
                // 将结果转换回世界坐标
                resultPos = convertPositionToInternalRep(resultPos.x, resultPos.y, resultPos.z);
                rx = resultPos.x;
                ry = resultPos.y;
                rz = resultPos.z;
                return result;
            }
        }

        // 如果未命中，返回终点
        rx = x2;
        ry = y2;
        rz = z2;

        return false;
    }

    /**
     * @brief 获取指定位置的高度
     * @param mapId 地图ID
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标（参考高度）
     * @param maxSearchDist 最大搜索距离
     * @return 高度值，如果没有高度信息则返回VMAP_INVALID_HEIGHT_VALUE
     *
     * 从指定位置向下发射射线，寻找第一个与地面的交点。
     * 如果没有找到地面（高度为无穷大），返回VMAP_INVALID_HEIGHT_VALUE。
     */
    float VMapManager2::getHeight(unsigned int mapId, float x, float y, float z, float maxSearchDist)
    {
        // 检查高度计算是否启用，以及该地图的高度功能是否被禁用
        if (isHeightCalcEnabled() && !IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_HEIGHT))
        {
            InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
            if (instanceTree != iInstanceMapTrees.end())
            {
                Vector3 pos = convertPositionToInternalRep(x, y, z);
                float height = instanceTree->second->getHeight(pos, maxSearchDist);
                // 如果高度为无穷大，说明没有找到地面
                if (!(height < G3D::finf()))
                    return height = VMAP_INVALID_HEIGHT_VALUE; // 无高度数据

                return height;
            }
        }

        return VMAP_INVALID_HEIGHT_VALUE;
    }

    /**
     * @brief 获取区域信息
     * @param mapId 地图ID
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标（输入/输出：会更新为地面高度）
     * @param flags 输出：区域标志
     * @param adtId 输出：ADT ID
     * @param rootId 输出：根ID
     * @param groupId 输出：组ID
     * @return 获取成功返回true，失败返回false
     *
     * 查询指定位置的区域属性，包括地面高度和区域标志等。
     */
    bool VMapManager2::getAreaInfo(uint32 mapId, float x, float y, float& z, uint32& flags, int32& adtId, int32& rootId, int32& groupId) const
    {
        // 检查区域标志查询是否被禁用
        if (!IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_AREAFLAG))
        {
            InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
            if (instanceTree != iInstanceMapTrees.end())
            {
                Vector3 pos = convertPositionToInternalRep(x, y, z);
                bool result = instanceTree->second->getAreaInfo(pos, flags, adtId, rootId, groupId);
                // z坐标在convertPositionToInternalRep()中不会改变，直接复制
                z = pos.z;
                return result;
            }
        }

        return false;
    }

    /**
     * @brief 获取液体水位信息
     * @param mapId 地图ID
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标
     * @param reqLiquidType 请求的液体类型
     * @param level 输出：液体高度
     * @param floor 输出：地面高度
     * @param type 输出：液体类型
     * @param mogpFlags 输出：WMO组标志
     * @return 该位置存在液体返回true，否则返回false
     *
     * 执行流程：
     * 1. 获取位置信息
     * 2. 从命中模型获取液体类型
     * 3. 检查液体类型是否匹配请求
     * 4. 获取液体高度
     */
    bool VMapManager2::GetLiquidLevel(uint32 mapId, float x, float y, float z, uint8 reqLiquidType, float& level, float& floor, uint32& type, uint32& mogpFlags) const
    {
        // 检查液体状态查询是否被禁用
        if (!IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_LIQUIDSTATUS))
        {
            InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
            if (instanceTree != iInstanceMapTrees.end())
            {
                LocationInfo info;
                Vector3 pos = convertPositionToInternalRep(x, y, z);
                // 获取位置信息
                if (instanceTree->second->GetLocationInfo(pos, info))
                {
                    floor = info.ground_Z;
                    ASSERT(floor < std::numeric_limits<float>::max());
                    ASSERT(info.hitModel);
                    // 从LiquidType.dbc获取液体类型
                    type = info.hitModel->GetLiquidType();
                    mogpFlags = info.hitModel->GetMogpFlags();
                    // 检查液体类型是否匹配请求
                    if (reqLiquidType && !(GetLiquidFlagsPtr(type) & reqLiquidType))
                        return false;
                    ASSERT(info.hitInstance);
                    // 获取液体高度
                    if (info.hitInstance->GetLiquidLevel(pos, info, level))
                        return true;
                }
            }
        }

        return false;
    }

    /**
     * @brief 获取区域和液体数据
     * @param mapId 地图ID
     * @param x X坐标
     * @param y Y坐标
     * @param z Z坐标
     * @param reqLiquidType 请求的液体类型
     * @param data 输出：区域和液体数据结构
     *
     * 一次性获取区域和液体信息，避免多次查询。
     * 如果液体状态查询被禁用，只获取区域信息。
     */
    void VMapManager2::getAreaAndLiquidData(unsigned int mapId, float x, float y, float z, uint8 reqLiquidType, AreaAndLiquidData& data) const
    {
        // 如果液体状态查询被禁用，只获取区域信息
        if (IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_LIQUIDSTATUS))
        {
            data.floorZ = z;
            int32 adtId, rootId, groupId;
            uint32 flags;
            if (getAreaInfo(mapId, x, y, data.floorZ, flags, adtId, rootId, groupId))
                data.areaInfo.emplace(adtId, rootId, groupId, flags);
            return;
        }

        InstanceTreeMap::const_iterator instanceTree = GetMapTree(mapId);
        if (instanceTree != iInstanceMapTrees.end())
        {
            LocationInfo info;
            Vector3 pos = convertPositionToInternalRep(x, y, z);
            // 获取位置信息
            if (instanceTree->second->GetLocationInfo(pos, info))
            {
                ASSERT(info.hitModel);
                ASSERT(info.hitInstance);
                data.floorZ = info.ground_Z;
                uint32 liquidType = info.hitModel->GetLiquidType();
                float liquidLevel;
                // 检查液体类型并获取液体高度
                if (!reqLiquidType || (GetLiquidFlagsPtr(liquidType) & reqLiquidType))
                    if (info.hitInstance->GetLiquidLevel(pos, info, liquidLevel))
                        data.liquidInfo.emplace(liquidType, liquidLevel);

                // 如果区域标志查询未被禁用，获取区域信息
                if (!IsVMAPDisabledForPtr(mapId, VMAP_DISABLE_AREAFLAG))
                    data.areaInfo.emplace(info.hitInstance->adtId, info.rootId, info.hitModel->GetWmoID(), info.hitModel->GetMogpFlags());
            }
        }
    }

    /**
     * @brief 获取模型实例（增加引用计数）
     * @param basepath 基础路径
     * @param filename 文件名
     * @param flags 模型标志（仅在创建时使用）
     * @return WorldModel指针，加载失败返回nullptr
     *
     * 线程安全：通过LoadedModelFilesLock保护iLoadedModelFiles的访问。
     *
     * 执行流程：
     * 1. 查找已加载的模型
     * 2. 如果未找到，加载新的模型文件
     * 3. 增加引用计数
     * 4. 返回模型指针
     */
    WorldModel* VMapManager2::acquireModelInstance(const std::string& basepath, const std::string& filename, uint32 flags/* Only used when creating the model */)
    {
        //! 临界区，线程安全访问iLoadedModelFiles
        std::lock_guard<std::mutex> lock(LoadedModelFilesLock);

        ModelFileMap::iterator model = iLoadedModelFiles.find(filename);
        if (model == iLoadedModelFiles.end())
        {
            // 模型未加载，创建新的WorldModel
            WorldModel* worldmodel = new WorldModel();
            if (!worldmodel->readFile(basepath + filename + ".vmo"))
            {
                TC_LOG_ERROR("misc", "VMapManager2: could not load '{}{}.vmo'", basepath, filename);
                delete worldmodel;
                return nullptr;
            }
            TC_LOG_DEBUG("maps", "VMapManager2: loading file '{}{}'", basepath, filename);

            // 设置模型标志
            worldmodel->Flags = flags;

            // 插入到已加载模型映射表
            model = iLoadedModelFiles.insert(std::pair<std::string, ManagedModel>(filename, ManagedModel())).first;
            model->second.setModel(worldmodel);
        }
        // 增加引用计数
        model->second.incRefCount();
        return model->second.getModel();
    }

    /**
     * @brief 释放模型实例（减少引用计数）
     * @param filename 文件名
     *
     * 线程安全：通过LoadedModelFilesLock保护iLoadedModelFiles的访问。
     *
     * 当引用计数降为0时，删除模型并从映射表中移除。
     */
    void VMapManager2::releaseModelInstance(const std::string &filename)
    {
        //! 临界区，线程安全访问iLoadedModelFiles
        std::lock_guard<std::mutex> lock(LoadedModelFilesLock);

        ModelFileMap::iterator model = iLoadedModelFiles.find(filename);
        if (model == iLoadedModelFiles.end())
        {
            TC_LOG_ERROR("misc", "VMapManager2: trying to unload non-loaded file '{}'", filename);
            return;
        }
        // 减少引用计数，如果为0则删除模型
        if (model->second.decRefCount() == 0)
        {
            TC_LOG_DEBUG("maps", "VMapManager2: unloading file '{}'", filename);
            delete model->second.getModel();
            iLoadedModelFiles.erase(model);
        }
    }

    /**
     * @brief 检查地图是否存在
     * @param basePath 基础路径
     * @param mapId 地图ID
     * @param x 瓦片X坐标
     * @param y 瓦片Y坐标
     * @return 加载结果（Success/FileNotFound/VersionMismatch）
     *
     * 不实际加载地图，只检查地图文件是否存在和版本是否匹配。
     */
    LoadResult VMapManager2::existsMap(char const* basePath, unsigned int mapId, int x, int y)
    {
        return StaticMapTree::CanLoadMap(std::string(basePath), mapId, x, y);
    }

    /**
     * @brief 获取实例地图树的副本
     * @param instanceMapTree 输出：实例树映射
     */
    void VMapManager2::getInstanceMapTree(InstanceTreeMap &instanceMapTree)
    {
        instanceMapTree = iInstanceMapTrees;
    }

} // namespace VMAP
