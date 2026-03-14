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
 * @file MapTree.cpp
 * @brief 静态地图树实现文件
 *
 * 本模块实现了基于层次包围盒(BVH)的地图碰撞检测系统。主要功能包括：
 * - 管理地图的静态几何数据结构（StaticMapTree）
 * - 提供射线与地图几何体的相交检测
 * - 支持视线路径检测(LOS)和高度查询
 * - 管理地图瓦片的动态加载和卸载
 * - 支持分块地图和非分块地图（如副本）
 *
 * 核心类：
 * - StaticMapTree: 静态地图树，管理整个地图的碰撞检测
 * - MapRayCallback: 射线相交回调类
 * - AreaInfoCallback: 区域信息查询回调类
 * - LocationInfoCallback: 位置信息查询回调类
 */

#include "MapTree.h"
#include "ModelInstance.h"
#include "VMapManager2.h"
#include "VMapDefinitions.h"
#include "Log.h"
#include "Errors.h"
#include "Metric.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <limits>

using G3D::Vector3;

namespace VMAP
{
    /**
     * @class MapRayCallback
     * @brief 射线相交检测回调类
     *
     * 用于在BIH树遍历过程中执行射线与模型实例的相交检测。
     * 该回调对象会在遍历每个潜在相交的节点时被调用，判断射线是否真正与模型相交。
     */
    class MapRayCallback
    {
        public:
            /**
             * @brief 构造函数
             * @param val 模型实例数组指针
             * @param ignoreFlags 模型忽略标志，用于跳过特定类型的模型
             */
            MapRayCallback(ModelInstance* val, ModelIgnoreFlags ignoreFlags): prims(val), hit(false), flags(ignoreFlags) { }

            /**
             * @brief 函数调用操作符，执行射线相交检测
             * @param ray 待检测的射线
             * @param entry 模型实例在数组中的索引
             * @param distance [输入/输出] 射线最大检测距离，返回实际相交距离
             * @param pStopAtFirstHit 是否在首次命中时停止检测
             * @return true 如果射线与模型相交
             * @return false 如果射线与模型不相交
             */
            bool operator()(const G3D::Ray& ray, uint32 entry, float& distance, bool pStopAtFirstHit = true)
            {
                bool result = prims[entry].intersectRay(ray, distance, pStopAtFirstHit, flags);
                if (result)
                    hit = true;
                return result;
            }

            /**
             * @brief 检查是否发生过命中
             * @return true 如果至少命中一个模型
             */
            bool didHit() { return hit; }
    protected:
        ModelInstance* prims;     ///< 模型实例数组指针
        bool hit;                 ///< 是否命中的标志
        ModelIgnoreFlags flags;   ///< 模型忽略标志
    };

    /**
     * @class AreaInfoCallback
     * @brief 区域信息查询回调类
     *
     * 用于查询给定点所在区域的详细信息，包括地面高度、区域标志等。
     * 该回调在BIH树遍历时对每个潜在包含查询点的模型进行点包含测试。
     */
    class AreaInfoCallback
    {
        public:
            /**
             * @brief 构造函数
             * @param val 模型实例数组指针
             */
            AreaInfoCallback(ModelInstance* val): prims(val) { }

            /**
             * @brief 函数调用操作符，执行点包含检测
             * @param point 待查询的三维点坐标
             * @param entry 模型实例在数组中的索引
             *
             * 该函数会调用模型实例的intersectPoint方法，检测点是否在模型内部，
             * 并更新内部的区域信息结构。
             */
            void operator()(Vector3 const& point, uint32 entry)
            {
#ifdef VMAP_DEBUG
                TC_LOG_DEBUG("maps", "AreaInfoCallback: trying to intersect '{}'", prims[entry].name);
#endif
                prims[entry].intersectPoint(point, aInfo);
            }

            ModelInstance* prims;   ///< 模型实例数组指针
            AreaInfo aInfo;         ///< 区域信息结构，存储查询结果
    };

    /**
     * @class LocationInfoCallback
     * @brief 位置信息查询回调类
     *
     * 用于查询给定点的详细位置信息，包括所在模型的详细几何信息。
     * 与AreaInfoCallback类似，但返回更详细的模型内部信息。
     */
    class LocationInfoCallback
    {
        public:
            /**
             * @brief 构造函数
             * @param val 模型实例数组指针
             * @param info 位置信息引用，用于存储查询结果
             */
            LocationInfoCallback(ModelInstance* val, LocationInfo &info): prims(val), locInfo(info), result(false) { }

            /**
             * @brief 函数调用操作符，执行位置信息查询
             * @param point 待查询的三维点坐标
             * @param entry 模型实例在数组中的索引
             *
             * 该函数会调用模型实例的GetLocationInfo方法，获取该点在模型中的详细位置信息。
             */
            void operator()(Vector3 const& point, uint32 entry)
            {
#ifdef VMAP_DEBUG
                TC_LOG_DEBUG("maps", "LocationInfoCallback: trying to intersect '{}'", prims[entry].name);
#endif
                if (prims[entry].GetLocationInfo(point, locInfo))
                    result = true;
            }

            ModelInstance* prims;       ///< 模型实例数组指针
            LocationInfo &locInfo;      ///< 位置信息引用，存储查询结果
            bool result;                ///< 是否成功获取位置信息的标志
    };

    //=========================================================

    /**
     * @brief 获取地图瓦片文件名
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @return 格式化的瓦片文件名字符串（格式：MMM_YY_XX.vmtile）
     *
     * 生成瓦片文件名的格式为：地图ID(3位)_瓦片Y(2位)_瓦片X(2位).vmtile
     * 注意：文件名中的坐标顺序为Y_X，这是历史遗留的命名方式
     */
    std::string StaticMapTree::getTileFileName(uint32 mapID, uint32 tileX, uint32 tileY)
    {
        std::stringstream tilefilename;
        tilefilename.fill('0');
        tilefilename << std::setw(3) << mapID << '_';
        // 注意：文件名中使用Y_X的顺序，而非X_Y
        //tilefilename << std::setw(2) << tileX << '_' << std::setw(2) << tileY << ".vmtile";
        tilefilename << std::setw(2) << tileY << '_' << std::setw(2) << tileX << ".vmtile";
        return tilefilename.str();
    }

    /**
     * @brief 获取指定位置的区域信息
     * @param pos [输入/输出] 三维坐标，输出时Z坐标会被更新为地面高度
     * @param flags [输出] 区域标志位
     * @param adtId [输出] ADT（地形瓦片）ID
     * @param rootId [输出] 根模型ID
     * @param groupId [输出] 组模型ID
     * @return true 如果成功找到区域信息
     * @return false 如果该位置没有区域信息
     *
     * 该函数查询给定位置所在的区域信息，包括地面高度、区域标志等。
     * 主要用于获取地形信息和水域判断等。
     */
    bool StaticMapTree::getAreaInfo(Vector3 &pos, uint32 &flags, int32 &adtId, int32 &rootId, int32 &groupId) const
    {
        // 创建区域信息回调对象
        AreaInfoCallback intersectionCallBack(iTreeValues);
        // 在BIH树中执行点查询
        iTree.intersectPoint(pos, intersectionCallBack);

        // 如果找到相交结果
        if (intersectionCallBack.aInfo.result)
        {
            // 提取区域信息
            flags = intersectionCallBack.aInfo.flags;
            adtId = intersectionCallBack.aInfo.adtId;
            rootId = intersectionCallBack.aInfo.rootId;
            groupId = intersectionCallBack.aInfo.groupId;
            // 更新Z坐标为地面高度
            pos.z = intersectionCallBack.aInfo.ground_Z;
            return true;
        }
        return false;
    }

    /**
     * @brief 获取指定位置的详细位置信息
     * @param pos 待查询的三维坐标
     * @param info [输出] 位置信息结构，存储查询结果
     * @return true 如果成功获取位置信息
     * @return false 如果该位置没有模型信息
     *
     * 该函数查询给定位置所在的模型详细信息，比getAreaInfo返回更多的模型内部结构信息。
     */
    bool StaticMapTree::GetLocationInfo(Vector3 const& pos, LocationInfo &info) const
    {
        LocationInfoCallback intersectionCallBack(iTreeValues, info);
        iTree.intersectPoint(pos, intersectionCallBack);
        return intersectionCallBack.result;
    }

    /**
     * @brief StaticMapTree构造函数
     * @param mapID 地图ID
     * @param basePath VMap文件的基础路径
     *
     * 初始化静态地图树对象，设置地图ID和文件路径。
     * 如果路径不以分隔符结尾，会自动添加斜杠。
     */
    StaticMapTree::StaticMapTree(uint32 mapID, std::string const& basePath) :
        iMapID(mapID), iIsTiled(false), iTreeValues(nullptr),
        iNTreeValues(0), iBasePath(basePath)
    {
        // 确保路径以分隔符结尾
        if (iBasePath.length() > 0 && iBasePath[iBasePath.length()-1] != '/' && iBasePath[iBasePath.length()-1] != '\\')
        {
            iBasePath.push_back('/');
        }
    }

    //=========================================================
    /**
     * @brief StaticMapTree析构函数
     *
     * 注意：在销毁对象前必须先调用unloadMap()来释放模型引用，
     * 否则会导致模型引用计数错误。
     */
    StaticMapTree::~StaticMapTree()
    {
        delete[] iTreeValues;
    }

    //=========================================================
    /**
     * @brief 计算射线与地图几何的相交时间
     * @param pRay 待检测的射线
     * @param pMaxDist [输入/输出] 最大检测距离，如果相交则返回实际距离
     * @param pStopAtFirstHit 是否在首次命中时停止检测
     * @param ignoreFlags 模型忽略标志
     * @return true 如果在最大距离内找到相交点
     * @return false 如果没有找到相交点
     *
     * 该函数执行射线与地图几何的相交检测，用于实现视线路径检测等功能。
     * 如果找到相交点，pMaxDist会被更新为从射线起点到相交点的距离。
     */
    bool StaticMapTree::getIntersectionTime(const G3D::Ray& pRay, float &pMaxDist, bool pStopAtFirstHit, ModelIgnoreFlags ignoreFlags) const
    {
        float distance = pMaxDist;
        MapRayCallback intersectionCallBack(iTreeValues, ignoreFlags);
        // 在BIH树中执行射线相交检测
        iTree.intersectRay(pRay, intersectionCallBack, distance, pStopAtFirstHit);

        // 如果命中，更新距离参数
        if (intersectionCallBack.didHit())
            pMaxDist = distance;
        return intersectionCallBack.didHit();
    }

    //=========================================================
    /**
     * @brief 检测两点之间是否有视线遮挡
     * @param pos1 起点
     * @param pos2 终点
     * @param ignoreFlag 模型忽略标志
     * @return true 如果两点之间没有遮挡（可以互相看到）
     * @return false 如果两点之间有遮挡物
     *
     * 该函数通过从pos1向pos2发射射线来检测视线路径。
     * 如果射线在到达终点前与地图几何相交，则表示有遮挡。
     */
    bool StaticMapTree::isInLineOfSight(Vector3 const& pos1, Vector3 const& pos2, ModelIgnoreFlags ignoreFlag) const
    {
        float maxDist = (pos2 - pos1).magnitude();

        // 如果距离超过最大浮点值或无效，返回false
        // 这可以防止作弊者传送到宇宙尽头导致的问题
        if (maxDist == std::numeric_limits<float>::max() || !std::isfinite(maxDist))
            return false;

        // 有效的地图坐标不应该产生浮点溢出，但这也会产生NaN值
        ASSERT(maxDist < std::numeric_limits<float>::max());

        // 防止NaN值导致BIH相交检测进入无限循环
        // 如果两点距离极小，认为没有遮挡
        if (maxDist < 1e-10f)
            return true;

        // 构造从pos1指向pos2的单位射线
        G3D::Ray ray = G3D::Ray::fromOriginAndDirection(pos1, (pos2 - pos1)/maxDist);

        // 检测射线是否与地图几何相交
        if (getIntersectionTime(ray, maxDist, true, ignoreFlag))
            return false;  // 有相交，表示有遮挡

        return true;  // 没有相交，视线畅通
    }

    //=========================================================
    /**
     * @brief 获取从pos1移动到pos2时的碰撞位置
     * @param pPos1 起始位置
     * @param pPos2 目标位置
     * @param pResultHitPos [输出] 实际碰撞位置或目标位置
     * @param pModifyDist 位置修正距离（正值向前，负值向后）
     * @return true 如果发生了碰撞
     * @return false 如果没有碰撞
     *
     * 该函数检测从pos1向pos2移动时是否会碰到障碍物。
     * 如果碰到障碍物，返回碰撞点位置（可根据pModifyDist进行偏移调整）。
     * 如果没有碰撞，返回目标位置pos2。
     */
    bool StaticMapTree::getObjectHitPos(Vector3 const& pPos1, Vector3 const& pPos2, Vector3& pResultHitPos, float pModifyDist) const
    {
        bool result = false;
        float maxDist = (pPos2 - pPos1).magnitude();

        // 有效的地图坐标不应该产生浮点溢出，但这也会产生NaN值
        ASSERT(maxDist < std::numeric_limits<float>::max());

        // 防止NaN值导致BIH相交检测进入无限循环
        if (maxDist < 1e-10f)
        {
            pResultHitPos = pPos2;
            return false;
        }

        // 计算移动方向的单位向量
        Vector3 dir = (pPos2 - pPos1)/maxDist;              // 方向向量，长度为1
        G3D::Ray ray(pPos1, dir);
        float dist = maxDist;

        // 检测是否与障碍物相交
        if (getIntersectionTime(ray, dist, false, ModelIgnoreFlags::Nothing))
        {
            // 计算碰撞点位置
            pResultHitPos = pPos1 + dir * dist;

            // 根据pModifyDist调整碰撞点位置
            if (pModifyDist < 0)
            {
                // 负值表示向后偏移（远离碰撞点）
                if ((pResultHitPos - pPos1).magnitude() > -pModifyDist)
                {
                    pResultHitPos = pResultHitPos + dir*pModifyDist;
                }
                else
                {
                    // 如果偏移距离超过了起点到碰撞点的距离，返回起点
                    pResultHitPos = pPos1;
                }
            }
            else
            {
                // 正值表示向前偏移（靠近碰撞点方向）
                pResultHitPos = pResultHitPos + dir*pModifyDist;
            }
            result = true;
        }
        else
        {
            // 没有碰撞，返回目标位置
            pResultHitPos = pPos2;
            result = false;
        }
        return result;
    }

    //=========================================================

    /**
     * @brief 获取指定位置的地面高度
     * @param pPos 待查询位置（Z坐标会被忽略）
     * @param maxSearchDist 最大向下搜索距离
     * @return 地面高度，如果没有找到则返回无穷大
     *
     * 该函数从给定位置向下发射射线，检测最近的地面高度。
     * 主要用于地形高度查询和角色落地检测。
     */
    float StaticMapTree::getHeight(Vector3 const& pPos, float maxSearchDist) const
    {
        float height = G3D::finf();
        Vector3 dir = Vector3(0, 0, -1);  // 向下的方向向量
        G3D::Ray ray(pPos, dir);           // 方向长度为1
        float maxDist = maxSearchDist;

        // 向下发射射线检测地面
        if (getIntersectionTime(ray, maxDist, false, ModelIgnoreFlags::Nothing))
        {
            // 计算地面高度：当前位置高度 - 到地面的距离
            height = pPos.z - maxDist;
        }
        return(height);
    }

    //=========================================================
    /**
     * @brief 检查地图是否可以加载
     * @param vmapPath VMap文件路径
     * @param mapID 地图ID
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @return 加载结果状态码
     *
     * 该函数检查指定地图和瓦片文件是否存在且版本匹配。
     * 主要用于在加载地图前进行预检查，避免无效加载。
     * 对于分块地图，会同时检查主地图文件和瓦片文件。
     */
    LoadResult StaticMapTree::CanLoadMap(const std::string &vmapPath, uint32 mapID, uint32 tileX, uint32 tileY)
    {
        std::string basePath = vmapPath;
        // 确保路径以分隔符结尾
        if (basePath.length() > 0 && basePath[basePath.length()-1] != '/' && basePath[basePath.length()-1] != '\\')
            basePath.push_back('/');

        // 构造地图文件名
        std::string fullname = basePath + VMapManager2::getMapFileName(mapID);

        LoadResult result = LoadResult::Success;

        // 尝试打开地图文件
        FILE* rf = fopen(fullname.c_str(), "rb");
        if (!rf)
            return LoadResult::FileNotFound;

        char tiled;
        char chunk[8];
        // 读取文件头和分块标志
        if (!readChunk(rf, chunk, VMAP_MAGIC, 8) || fread(&tiled, sizeof(char), 1, rf) != 1)
        {
            fclose(rf);
            return LoadResult::VersionMismatch;
        }

        // 如果是分块地图，还需要检查瓦片文件
        if (tiled)
        {
            std::string tilefile = basePath + getTileFileName(mapID, tileX, tileY);
            FILE* tf = fopen(tilefile.c_str(), "rb");
            if (!tf)
                result = LoadResult::FileNotFound;
            else
            {
                // 检查瓦片文件的版本标识
                if (!readChunk(tf, chunk, VMAP_MAGIC, 8))
                    result = LoadResult::VersionMismatch;
                fclose(tf);
            }
        }
        fclose(rf);
        return result;
    }

    //=========================================================

    /**
     * @brief 初始化地图树
     * @param fname 地图文件名
     * @param vm VMap管理器指针
     * @return true 如果初始化成功
     * @return false 如果初始化失败
     *
     * 该函数从文件加载地图树的BIH结构和模型实例数据。
     * 对于非分块地图（如副本），会加载全局模型数据。
     * 对于分块地图，只加载树结构，具体瓦片数据在LoadMapTile中加载。
     */
    bool StaticMapTree::InitMap(const std::string &fname, VMapManager2* vm)
    {
        TC_LOG_DEBUG("maps", "StaticMapTree::InitMap() : initializing StaticMapTree '{}'", fname);
        bool success = false;
        std::string fullname = iBasePath + fname;
        FILE* rf = fopen(fullname.c_str(), "rb");
        if (!rf)
            return false;

        char chunk[8];
        char tiled = '\0';

        // 读取文件头：VMAP_MAGIC + tiled标志 + NODE块 + BIH树数据 + GOBJ块
        if (readChunk(rf, chunk, VMAP_MAGIC, 8) && fread(&tiled, sizeof(char), 1, rf) == 1 &&
            readChunk(rf, chunk, "NODE", 4) && iTree.readFromFile(rf))
        {
            // 分配模型实例数组
            iNTreeValues = iTree.primCount();
            iTreeValues = new ModelInstance[iNTreeValues];
            success = readChunk(rf, chunk, "GOBJ", 4);
        }

        iIsTiled = tiled != '\0';

        // 全局模型生成点（Global model spawns）
        // 只有非分块地图才有全局模型，通常只有一个（如副本）
        ModelSpawn spawn;
#ifdef VMAP_DEBUG
        TC_LOG_DEBUG("maps", "StaticMapTree::InitMap() : map isTiled: {}", static_cast<uint32>(iIsTiled));
#endif
        if (!iIsTiled && ModelSpawn::readFromFile(rf, spawn))
        {
            // 获取模型实例
            WorldModel* model = vm->acquireModelInstance(iBasePath, spawn.name, spawn.flags);
            TC_LOG_DEBUG("maps", "StaticMapTree::InitMap() : loading {}", spawn.name);
            if (model)
            {
                // 假设全局模型总是第一个也是唯一的树节点值（可改进）
                iTreeValues[0] = ModelInstance(spawn, model);
                iLoadedSpawns[0] = 1;
            }
            else
            {
                success = false;
                TC_LOG_ERROR("misc", "StaticMapTree::InitMap() : could not acquire WorldModel pointer for '{}'", spawn.name);
            }
        }

        fclose(rf);
        return success;
    }

    //=========================================================

    /**
     * @brief 卸载地图数据
     * @param vm VMap管理器指针
     *
     * 该函数释放所有已加载的模型实例，清除加载的瓦片和生成点记录。
     * 在销毁StaticMapTree对象前必须调用此函数，以正确释放模型引用。
     */
    void StaticMapTree::UnloadMap(VMapManager2* vm)
    {
        // 遍历所有已加载的生成点
        for (std::pair<uint32 const, uint32>& iLoadedSpawn : iLoadedSpawns)
        {
            // 标记模型为已卸载状态
            iTreeValues[iLoadedSpawn.first].setUnloaded();
            // 根据引用计数释放模型实例
            for (uint32 refCount = 0; refCount < iLoadedSpawn.second; ++refCount)
                vm->releaseModelInstance(iTreeValues[iLoadedSpawn.first].name);
        }
        // 清空加载记录
        iLoadedSpawns.clear();
        iLoadedTiles.clear();
    }

    //=========================================================

    /**
     * @brief 加载地图瓦片数据
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @param vm VMap管理器指针
     * @return true 如果加载成功
     * @return false 如果加载失败
     *
     * 该函数加载指定瓦片的模型数据。对于分块地图，瓦片文件包含该区域的模型实例。
     * 对于非分块地图，核心会为所有地图创建网格，因此需要"假"瓦片加载来跟踪何时卸载地图几何。
     */
    bool StaticMapTree::LoadMapTile(uint32 tileX, uint32 tileY, VMapManager2* vm)
    {
        // 非分块地图的处理
        if (!iIsTiled)
        {
            // 当前核心为所有地图创建网格，无论是否有地形瓦片
            // 因此需要"假"瓦片加载来知道何时可以卸载地图几何
            iLoadedTiles[packTileID(tileX, tileY)] = false;
            return true;
        }

        // 检查树是否已初始化
        if (!iTreeValues)
        {
            TC_LOG_ERROR("misc", "StaticMapTree::LoadMapTile() : tree has not been initialized [{}, {}]", tileX, tileY);
            return false;
        }
        bool result = true;

        // 构造瓦片文件名并尝试打开
        std::string tilefile = iBasePath + getTileFileName(iMapID, tileX, tileY);
        FILE* tf = fopen(tilefile.c_str(), "rb");
        if (tf)
        {
            char chunk[8];

            // 读取文件头并验证
            if (!readChunk(tf, chunk, VMAP_MAGIC, 8))
                result = false;

            // 读取生成点数量
            uint32 numSpawns = 0;
            if (result && fread(&numSpawns, sizeof(uint32), 1, tf) != 1)
                result = false;

            // 加载所有模型生成点
            for (uint32 i=0; i<numSpawns && result; ++i)
            {
                // 读取模型生成点数据
                ModelSpawn spawn;
                result = ModelSpawn::readFromFile(tf, spawn);
                if (result)
                {
                    // 获取模型实例
                    WorldModel* model = vm->acquireModelInstance(iBasePath, spawn.name, spawn.flags);
                    if (!model)
                        TC_LOG_ERROR("misc", "StaticMapTree::LoadMapTile() : could not acquire WorldModel pointer [{}, {}]", tileX, tileY);

                    // 更新树结构
                    uint32 referencedVal;

                    // 读取树节点索引
                    if (fread(&referencedVal, sizeof(uint32), 1, tf) == 1)
                    {
                        // 检查该节点是否已加载
                        if (!iLoadedSpawns.count(referencedVal))
                        {
                            // 验证索引有效性
                            if (referencedVal > iNTreeValues)
                            {
                                TC_LOG_ERROR("maps", "StaticMapTree::LoadMapTile() : invalid tree element ({}/{}) referenced in tile {}", referencedVal, iNTreeValues, tilefile);
                                continue;
                            }

                            // 首次加载，创建模型实例
                            iTreeValues[referencedVal] = ModelInstance(spawn, model);
                            iLoadedSpawns[referencedVal] = 1;
                        }
                        else
                        {
                            // 已加载过，增加引用计数
                            ++iLoadedSpawns[referencedVal];
#ifdef VMAP_DEBUG
                            // 调试检查：确保是同一个生成点
                            if (iTreeValues[referencedVal].ID != spawn.ID)
                                TC_LOG_DEBUG("maps", "StaticMapTree::LoadMapTile() : trying to load wrong spawn in node");
                            else if (iTreeValues[referencedVal].name != spawn.name)
                                TC_LOG_DEBUG("maps", "StaticMapTree::LoadMapTile() : name collision on GUID={}", spawn.ID);
#endif
                        }
                    }
                    else
                        result = false;
                }
            }
            // 标记瓦片为已加载
            iLoadedTiles[packTileID(tileX, tileY)] = true;
            fclose(tf);
        }
        else
        {
            // 瓦片文件不存在，标记为未加载
            iLoadedTiles[packTileID(tileX, tileY)] = false;
        }

        // 记录度量事件
        TC_METRIC_EVENT("map_events", "LoadMapTile",
            "Map: " + std::to_string(iMapID) + " TileX: " + std::to_string(tileX) + " TileY: " + std::to_string(tileY));
        return result;
    }

    //=========================================================

    /**
     * @brief 卸载地图瓦片数据
     * @param tileX 瓦片X坐标
     * @param tileY 瓦片Y坐标
     * @param vm VMap管理器指针
     *
     * 该函数卸载指定瓦片的模型数据，释放模型实例引用。
     * 当一个模型的引用计数降为0时，会从树中移除该模型。
     */
    void StaticMapTree::UnloadMapTile(uint32 tileX, uint32 tileY, VMapManager2* vm)
    {
        uint32 tileID = packTileID(tileX, tileY);
        loadedTileMap::iterator tile = iLoadedTiles.find(tileID);

        // 检查瓦片是否已加载
        if (tile == iLoadedTiles.end())
        {
            TC_LOG_ERROR("misc", "StaticMapTree::UnloadMapTile() : trying to unload non-loaded tile - Map:{} X:{} Y:{}", iMapID, tileX, tileY);
            return;
        }

        // 如果瓦片有关联的文件
        if (tile->second)
        {
            std::string tilefile = iBasePath + getTileFileName(iMapID, tileX, tileY);
            FILE* tf = fopen(tilefile.c_str(), "rb");
            if (tf)
            {
                bool result = true;
                char chunk[8];

                // 读取并验证文件头
                if (!readChunk(tf, chunk, VMAP_MAGIC, 8))
                    result = false;

                // 读取生成点数量
                uint32 numSpawns;
                if (fread(&numSpawns, sizeof(uint32), 1, tf) != 1)
                    result = false;

                // 卸载所有模型生成点
                for (uint32 i=0; i<numSpawns && result; ++i)
                {
                    // 读取模型生成点数据
                    ModelSpawn spawn;
                    result = ModelSpawn::readFromFile(tf, spawn);
                    if (result)
                    {
                        // 释放模型实例引用
                        vm->releaseModelInstance(spawn.name);

                        // 更新树结构
                        uint32 referencedNode;

                        if (fread(&referencedNode, sizeof(uint32), 1, tf) != 1)
                            result = false;
                        else
                        {
                            // 检查引用是否存在
                            if (!iLoadedSpawns.count(referencedNode))
                                TC_LOG_ERROR("misc", "StaticMapTree::UnloadMapTile() : trying to unload non-referenced model '{}' (ID:{})", spawn.name, spawn.ID);
                            else if (--iLoadedSpawns[referencedNode] == 0)
                            {
                                // 引用计数降为0，从树中移除模型
                                iTreeValues[referencedNode].setUnloaded();
                                iLoadedSpawns.erase(referencedNode);
                            }
                        }
                    }
                }
                fclose(tf);
            }
        }

        // 从加载记录中移除瓦片
        iLoadedTiles.erase(tile);

        // 记录度量事件
        TC_METRIC_EVENT("map_events", "UnloadMapTile",
            "Map: " + std::to_string(iMapID) + " TileX: " + std::to_string(tileX) + " TileY: " + std::to_string(tileY));
    }

    /**
     * @brief 获取模型实例数组和数量
     * @param models [输出] 模型实例数组指针
     * @param count [输出] 模型实例数量
     *
     * 该函数返回地图树中的所有模型实例，用于外部访问和调试。
     */
    void StaticMapTree::getModelInstances(ModelInstance* &models, uint32 &count)
    {
        models = iTreeValues;
        count = iNTreeValues;
    }
}
