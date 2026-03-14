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
 * @file VMapManager2.h
 * @brief VMap(虚拟地图)管理器模块
 *
 * 本模块负责管理虚拟地图的加载、卸载和查询功能:
 * - 管理地图的加载与卸载(支持按瓦片加载)
 * - 提供视线检测(Line of Sight)功能
 * - 提供高度计算功能
 * - 提供区域信息查询功能
 * - 提供液体状态查询功能
 *
 * 每个地图实例都有独立的BSP树用于碰撞检测。
 * 加载的模型容器会被包含在这些BSP树中。
 */

#ifndef _VMAPMANAGER2_H
#define _VMAPMANAGER2_H

#include <mutex>
#include <unordered_map>
#include <vector>
#include "Define.h"
#include "IVMapManager.h"

//===========================================================

/// 地图文件扩展名
#define MAP_FILENAME_EXTENSION2 ".vmtree"

/// 文件名缓冲区大小
#define FILENAMEBUFFER_SIZE 500

//===========================================================

namespace G3D
{
    class Vector3;
}

namespace VMAP
{
    class StaticMapTree;
    class WorldModel;

    /**
     * @class ManagedModel
     * @brief 托管模型类，用于管理WorldModel的引用计数
     *
     * 该类封装了WorldModel指针及其引用计数，实现模型的自动生命周期管理。
     * 当引用计数降为0时，模型可以被安全释放。
     */
    class TC_COMMON_API ManagedModel
    {
        public:
            /**
             * @brief 构造函数
             */
            ManagedModel() : iModel(nullptr), iRefCount(0) { }

            /**
             * @brief 设置模型指针
             * @param model WorldModel指针
             */
            void setModel(WorldModel* model) { iModel = model; }

            /**
             * @brief 获取模型指针
             * @return WorldModel指针
             */
            WorldModel* getModel() { return iModel; }

            /**
             * @brief 增加引用计数
             */
            void incRefCount() { ++iRefCount; }

            /**
             * @brief 减少引用计数
             * @return 减少后的引用计数值
             */
            int decRefCount() { return --iRefCount; }
        protected:
            WorldModel* iModel;     ///< 托管的WorldModel指针
            int iRefCount;          ///< 引用计数
    };

    /// 实例树映射表：地图ID -> 静态地图树指针
    typedef std::unordered_map<uint32, StaticMapTree*> InstanceTreeMap;
    /// 模型文件映射表：文件名 -> 托管模型对象
    typedef std::unordered_map<std::string, ManagedModel> ModelFileMap;

    /**
     * @enum DisableTypes
     * @brief VMap功能禁用标志位
     *
     * 用于控制特定地图上VMap功能的开关
     */
    enum DisableTypes
    {
        VMAP_DISABLE_AREAFLAG       = 0x1,  ///< 禁用区域标志查询
        VMAP_DISABLE_HEIGHT         = 0x2,  ///< 禁用高度计算
        VMAP_DISABLE_LOS            = 0x4,  ///< 禁用视线检测
        VMAP_DISABLE_LIQUIDSTATUS   = 0x8   ///< 禁用液体状态查询
    };

    /**
     * @class VMapManager2
     * @brief VMap管理器主类，负责管理地图加载、卸载、视线检测、高度计算等核心功能
     *
     * 该类是VMap系统的核心管理器，继承自IVMapManager接口。
     * 对于每个地图或地图瓦片，它会读取包含模型容器文件的目录文件。
     * 每个全局地图或实例都有自己独立的动态BSP树。
     * 加载的模型容器会被包含在这些BSP树中。
     *
     * 线程安全性：
     * - 支持线程安全环境和非线程安全环境
     * - 在线程安全环境下，iLoadedModelFiles的访问受LoadedModelFilesLock保护
     *
     * 性能注意事项：
     * - 地图加载是重量级操作，应在启动时完成
     * - 视线检测和高度计算是频繁调用的方法，已优化
     * - 使用BSP树加速碰撞检测
     */
    class TC_COMMON_API VMapManager2 : public IVMapManager
    {
        protected:
            /// 已加载的模型文件映射（文件名 -> 托管模型）
            ModelFileMap iLoadedModelFiles;
            /// 实例地图树映射（地图ID -> 静态地图树）
            InstanceTreeMap iInstanceMapTrees;
            /// 是否处于线程安全环境
            bool thread_safe_environment;
            /// 保护iLoadedModelFiles访问的互斥锁
            std::mutex LoadedModelFilesLock;

            /**
             * @brief 内部地图加载函数
             * @param mapId 地图ID
             * @param basePath 基础路径
             * @param tileX 瓦片X坐标
             * @param tileY 瓦片Y坐标
             * @return 加载成功返回true，失败返回false
             *
             * 调用时机：由loadMap()内部调用
             */
            bool _loadMap(uint32 mapId, const std::string& basePath, uint32 tileX, uint32 tileY);
            /* void _unloadMap(uint32 pMapId, uint32 x, uint32 y); */

            /**
             * @brief 获取液体标志的空实现
             * @return 返回0
             */
            static uint32 GetLiquidFlagsDummy(uint32) { return 0; }

            /**
             * @brief 检查VMAP是否被禁用的空实现
             * @return 返回false（未禁用）
             */
            static bool IsVMAPDisabledForDummy(uint32 /*entry*/, uint8 /*flags*/) { return false; }

            /**
             * @brief 获取地图树迭代器
             * @param mapId 地图ID
             * @return 地图树迭代器，如果不存在或为空则返回end()
             */
            InstanceTreeMap::const_iterator GetMapTree(uint32 mapId) const;

        public:
            /**
             * @brief 将世界坐标转换为内部表示
             * @param x 世界坐标X
             * @param y 世界坐标Y
             * @param z 世界坐标Z
             * @return 转换后的G3D向量
             *
             * 公开访问用于调试目的
             */
            G3D::Vector3 convertPositionToInternalRep(float x, float y, float z) const;

            /**
             * @brief 获取地图文件名
             * @param mapId 地图ID
             * @return 格式化的地图文件名（如：001.vmtree）
             */
            static std::string getMapFileName(unsigned int mapId);

            /**
             * @brief 构造函数
             *
             * 初始化VMap管理器，设置默认的液体标志和禁用检查函数
             */
            VMapManager2();

            /**
             * @brief 析构函数
             *
             * 清理所有已加载的地图树和模型文件
             */
            ~VMapManager2(void);

            /**
             * @brief 初始化非线程安全环境
             * @param mapIds 所有将在VMapManager生命周期中使用的地图ID列表
             *
             * 调用时机：在单线程环境下，启动时调用一次
             * 性能注意事项：调用后可以避免运行时的线程同步开销
             */
            void InitializeThreadUnsafe(const std::vector<uint32>& mapIds);

            /**
             * @brief 加载地图瓦片
             * @param pBasePath 基础路径
             * @param mapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 加载结果（VMAP_LOAD_RESULT_OK/ERROR/IGNORED）
             *
             * 调用时机：当需要加载某个地图瓦片时调用
             */
            int loadMap(char const* pBasePath, unsigned int mapId, int x, int y) override;

            /**
             * @brief 卸载地图瓦片
             * @param mapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             *
             * 调用时机：当地图瓦片不再需要时调用
             */
            void unloadMap(unsigned int mapId, int x, int y) override;

            /**
             * @brief 卸载整个地图
             * @param mapId 地图ID
             *
             * 调用时机：当整个地图不再需要时调用
             */
            void unloadMap(unsigned int mapId) override;

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
             * 调用时机：技能施放、AI视线检测等
             * 性能注意事项：频繁调用，已优化
             */
            bool isInLineOfSight(unsigned int mapId, float x1, float y1, float z1, float x2, float y2, float z2, ModelIgnoreFlags ignoreFlags) override ;

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
             * 调用时机：移动碰撞检测
             */
            bool getObjectHitPos(unsigned int mapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float& ry, float& rz, float modifyDist) override;

            /**
             * @brief 获取指定位置的高度
             * @param mapId 地图ID
             * @param x X坐标
             * @param y Y坐标
             * @param z Z坐标（参考高度）
             * @param maxSearchDist 最大搜索距离
             * @return 高度值，如果没有高度信息则返回VMAP_INVALID_HEIGHT_VALUE
             *
             * 调用时机：实体落地检测、高度验证等
             * 性能注意事项：频繁调用，已优化
             */
            float getHeight(unsigned int mapId, float x, float y, float z, float maxSearchDist) override;

            /**
             * @brief 处理命令（保留用于调试和扩展）
             * @param command 命令字符串
             * @return 当前总是返回false
             */
            bool processCommand(char* /*command*/) override { return false; }

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
             * 调用时机：查询某个位置的区域属性
             */
            bool getAreaInfo(uint32 mapId, float x, float y, float& z, uint32& flags, int32& adtId, int32& rootId, int32& groupId) const override;

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
             * 调用时机：游泳检测、液体交互等
             */
            bool GetLiquidLevel(uint32 mapId, float x, float y, float z, uint8 reqLiquidType, float& level, float& floor, uint32& type, uint32& mogpFlags) const override;

            /**
             * @brief 获取区域和液体数据
             * @param mapId 地图ID
             * @param x X坐标
             * @param y Y坐标
             * @param z Z坐标
             * @param reqLiquidType 请求的液体类型
             * @param data 输出：区域和液体数据结构
             *
             * 调用时机：一次性获取区域和液体信息
             */
            void getAreaAndLiquidData(unsigned int mapId, float x, float y, float z, uint8 reqLiquidType, AreaAndLiquidData& data) const override;

            /**
             * @brief 获取模型实例（增加引用计数）
             * @param basepath 基础路径
             * @param filename 文件名
             * @param flags 模型标志（仅在创建时使用）
             * @return WorldModel指针，加载失败返回nullptr
             *
             * 调用时机：加载地图瓦片时需要获取模型实例
             * 线程安全：通过LoadedModelFilesLock保证
             */
            WorldModel* acquireModelInstance(const std::string& basepath, const std::string& filename, uint32 flags = 0);

            /**
             * @brief 释放模型实例（减少引用计数）
             * @param filename 文件名
             *
             * 调用时机：卸载地图瓦片时释放模型实例
             * 当引用计数降为0时，模型会被删除
             * 线程安全：通过LoadedModelFilesLock保证
             */
            void releaseModelInstance(const std::string& filename);

            /**
             * @brief 获取目录文件名
             * @param mapId 地图ID
             * @return 地图文件名
             *
             * 注意：参数x和y未使用，仅为了接口兼容性保留
             */
            virtual std::string getDirFileName(unsigned int mapId, int /*x*/, int /*y*/) const override
            {
                return getMapFileName(mapId);
            }

            /**
             * @brief 检查地图是否存在
             * @param basePath 基础路径
             * @param mapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 加载结果（Success/FileNotFound/VersionMismatch）
             */
            virtual LoadResult existsMap(char const* basePath, unsigned int mapId, int x, int y) override;

            /**
             * @brief 获取实例地图树的副本
             * @param instanceMapTree 输出：实例树映射
             */
            void getInstanceMapTree(InstanceTreeMap &instanceMapTree);

            /// 获取液体标志的函数指针类型
            typedef uint32(*GetLiquidFlagsFn)(uint32 liquidType);
            /// 获取液体标志的函数指针
            GetLiquidFlagsFn GetLiquidFlagsPtr;

            /// 检查VMAP是否被禁用的函数指针类型
            typedef bool(*IsVMAPDisabledForFn)(uint32 entry, uint8 flags);
            /// 检查VMAP是否被禁用的函数指针
            IsVMAPDisabledForFn IsVMAPDisabledForPtr;
    };
}

#endif
