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
 * @file MapTree.h
 * @brief 静态地图树数据结构模块
 *
 * 本模块实现了静态地图的空间索引结构，用于高效的地形碰撞检测：
 * - 使用BIH(Bounding Interval Hierarchy)加速空间查询
 * - 支持瓦片化的地图加载和卸载
 * - 提供视线检测、高度计算、区域查询等功能
 *
 * 性能注意事项：
 * - BIH树构建是重量级操作，应在加载时完成
 * - 查询操作已优化，适合频繁调用
 * - 瓦片化加载可以减少内存占用
 */

#ifndef _MAPTREE_H
#define _MAPTREE_H

#include "Define.h"
#include "BoundingIntervalHierarchy.h"
#include <unordered_map>

namespace VMAP
{
    class ModelInstance;
    class GroupModel;
    class VMapManager2;
    enum class LoadResult : uint8;
    enum class ModelIgnoreFlags : uint32;

    /**
     * @struct LocationInfo
     * @brief 位置信息结构体
     *
     * 用于存储射线与地图模型相交后的详细信息
     */
    struct TC_COMMON_API LocationInfo
    {
        /**
         * @brief 默认构造函数
         *
         * 初始化为无效状态
         */
        LocationInfo(): rootId(-1), hitInstance(nullptr), hitModel(nullptr), ground_Z(-G3D::finf()) { }

        int32 rootId;                   ///< WMO根ID
        ModelInstance const* hitInstance;   ///< 命中的模型实例指针
        GroupModel const* hitModel;         ///< 命中的组模型指针
        float ground_Z;                     ///< 地面高度（Z坐标）
    };

    /**
     * @class StaticMapTree
     * @brief 静态地图树类，管理单个地图的碰撞检测树结构
     *
     * 该类封装了单个地图的BIH树和模型实例数据。
     * 支持瓦片化的按需加载，提供各种空间查询功能。
     *
     * 使用流程：
     * 1. 构造StaticMapTree对象
     * 2. 调用InitMap()加载地图树结构
     * 3. 调用LoadMapTile()加载所需的瓦片
     * 4. 使用查询函数进行碰撞检测
     * 5. 卸载时调用UnloadMapTile()和UnloadMap()
     *
     * 注意事项：
     * - 必须在析构前调用UnloadMap()释放模型引用
     * - 非瓦片地图会在InitMap()时加载全局模型
     */
    class TC_COMMON_API StaticMapTree
    {
        /// 已加载瓦片映射表：瓦片ID -> 是否有文件关联
        typedef std::unordered_map<uint32, bool> loadedTileMap;
        /// 已加载生成点映射表：树索引 -> 引用计数
        typedef std::unordered_map<uint32, uint32> loadedSpawnMap;
        private:
            uint32 iMapID;                  ///< 地图ID
            bool iIsTiled;                  ///< 是否为瓦片化地图
            BIH iTree;                      ///< BIH树结构，用于快速空间查询
            ModelInstance* iTreeValues;     ///< 树节点对应的模型实例数组
            uint32 iNTreeValues;            ///< 树节点总数

            /**
             * @brief 已加载瓦片表
             *
             * 存储该地图已加载的所有瓦片ID。
             * 有些地图不分瓦片，需要确保在所有瓦片移除前不删除地图。
             * 空瓦片没有瓦片文件，因此用map<id, bool>而不是set（用于一致性检查）
             */
            loadedTileMap iLoadedTiles;

            /**
             * @brief 已加载生成点表
             *
             * 存储<树索引, 引用计数>对，用于：
             * - 使树值失效
             * - 卸载地图
             * - 报告错误
             */
            loadedSpawnMap iLoadedSpawns;
            std::string iBasePath;          ///< 地图文件基础路径

        private:
            /**
             * @brief 获取射线与地图的相交时间
             * @param pRay 射线
             * @param pMaxDist 最大距离（输入/输出）
             * @param pStopAtFirstHit 是否在首次命中时停止
             * @param ignoreFlags 忽略的模型标志
             * @return 有交点返回true，否则返回false
             */
            bool getIntersectionTime(const G3D::Ray& pRay, float &pMaxDist, bool pStopAtFirstHit, ModelIgnoreFlags ignoreFlags) const;

        public:
            /**
             * @brief 获取瓦片文件名
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @return 格式化的瓦片文件名（如：001_32_45.vmtile）
             */
            static std::string getTileFileName(uint32 mapID, uint32 tileX, uint32 tileY);

            /**
             * @brief 打包瓦片ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @return 打包后的32位瓦片ID
             */
            static uint32 packTileID(uint32 tileX, uint32 tileY) { return tileX<<16 | tileY; }

            /**
             * @brief 解包瓦片ID
             * @param ID 打包的瓦片ID
             * @param tileX 输出：瓦片X坐标
             * @param tileY 输出：瓦片Y坐标
             */
            static void unpackTileID(uint32 ID, uint32 &tileX, uint32 &tileY) { tileX = ID>>16; tileY = ID&0xFF; }

            /**
             * @brief 检查地图是否可加载
             * @param basePath 基础路径
             * @param mapID 地图ID
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @return 加载结果
             */
            static LoadResult CanLoadMap(const std::string &basePath, uint32 mapID, uint32 tileX, uint32 tileY);

            /**
             * @brief 构造函数
             * @param mapID 地图ID
             * @param basePath 地图文件基础路径
             */
            StaticMapTree(uint32 mapID, const std::string &basePath);

            /**
             * @brief 析构函数
             *
             * 注意：确保在析构前调用UnloadMap()注销模型引用
             */
            ~StaticMapTree();

            /**
             * @brief 检查两点之间是否有视线遮挡
             * @param pos1 起点（内部坐标表示）
             * @param pos2 终点（内部坐标表示）
             * @param ignoreFlags 忽略的模型标志
             * @return 有视线返回true，被遮挡返回false
             *
             * 性能注意事项：频繁调用，已优化
             */
            bool isInLineOfSight(const G3D::Vector3& pos1, const G3D::Vector3& pos2, ModelIgnoreFlags ignoreFlags) const;

            /**
             * @brief 获取物体命中位置
             * @param pos1 起点（内部坐标表示）
             * @param pos2 终点（内部坐标表示）
             * @param pResultHitPos 输出：命中位置
             * @param pModifyDist 距离修正值
             * @return 命中物体返回true，否则返回false
             */
            bool getObjectHitPos(const G3D::Vector3& pos1, const G3D::Vector3& pos2, G3D::Vector3& pResultHitPos, float pModifyDist) const;

            /**
             * @brief 获取高度
             * @param pPos 位置（内部坐标表示）
             * @param maxSearchDist 最大搜索距离
             * @return 高度值，如果没有高度信息则返回无穷大
             */
            float getHeight(const G3D::Vector3& pPos, float maxSearchDist) const;

            /**
             * @brief 获取区域信息
             * @param pos 位置（内部坐标表示，会更新Z值）
             * @param flags 输出：区域标志
             * @param adtId 输出：ADT ID
             * @param rootId 输出：根ID
             * @param groupId 输出：组ID
             * @return 获取成功返回true，失败返回false
             */
            bool getAreaInfo(G3D::Vector3 &pos, uint32 &flags, int32 &adtId, int32 &rootId, int32 &groupId) const;

            /**
             * @brief 获取位置信息
             * @param pos 位置（内部坐标表示）
             * @param info 输出：位置信息结构体
             * @return 获取成功返回true，失败返回false
             */
            bool GetLocationInfo(const G3D::Vector3 &pos, LocationInfo &info) const;

            /**
             * @brief 初始化地图树
             * @param fname 地图文件名
             * @param vm VMap管理器指针
             * @return 初始化成功返回true，失败返回false
             *
             * 调用时机：构造对象后，加载瓦片前
             */
            bool InitMap(const std::string &fname, VMapManager2* vm);

            /**
             * @brief 卸载整个地图
             * @param vm VMap管理器指针
             *
             * 调用时机：地图不再需要时
             */
            void UnloadMap(VMapManager2* vm);

            /**
             * @brief 加载地图瓦片
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @param vm VMap管理器指针
             * @return 加载成功返回true，失败返回false
             *
             * 调用时机：需要使用某个区域的地形数据时
             */
            bool LoadMapTile(uint32 tileX, uint32 tileY, VMapManager2* vm);

            /**
             * @brief 卸载地图瓦片
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @param vm VMap管理器指针
             *
             * 调用时机：某个区域的地形数据不再需要时
             */
            void UnloadMapTile(uint32 tileX, uint32 tileY, VMapManager2* vm);

            /**
             * @brief 检查是否为瓦片化地图
             * @return 是瓦片化地图返回true，否则返回false
             */
            bool isTiled() const { return iIsTiled; }

            /**
             * @brief 获取已加载瓦片数量
             * @return 已加载瓦片数量
             */
            uint32 numLoadedTiles() const { return uint32(iLoadedTiles.size()); }

            /**
             * @brief 获取模型实例数组和数量
             * @param models 输出：模型实例数组指针
             * @param count 输出：模型实例数量
             */
            void getModelInstances(ModelInstance* &models, uint32 &count);

        private:
            /// 禁用拷贝构造
            StaticMapTree(StaticMapTree const& right) = delete;
            /// 禁用赋值运算符
            StaticMapTree& operator=(StaticMapTree const& right) = delete;
    };

    /**
     * @struct AreaInfo
     * @brief 区域信息结构体
     *
     * 用于存储区域查询的结果信息
     */
    struct TC_COMMON_API AreaInfo
    {
        /**
         * @brief 默认构造函数
         *
         * 初始化为无效状态
         */
        AreaInfo(): result(false), ground_Z(-G3D::finf()), flags(0), adtId(0),
            rootId(0), groupId(0) { }

        bool result;        ///< 查询是否成功
        float ground_Z;     ///< 地面高度
        uint32 flags;       ///< 区域标志
        int32 adtId;        ///< ADT ID
        int32 rootId;       ///< WMO根ID
        int32 groupId;      ///< WMO组ID
    };
}                                                           // VMAP

#endif // _MAPTREE_H
