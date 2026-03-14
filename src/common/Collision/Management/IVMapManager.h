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
 * @file IVMapManager.h
 * @brief VMap管理器接口定义
 *
 * 本文件定义了VMap（Virtual Map，虚拟地图）管理器的抽象接口。
 * VMap系统用于处理游戏世界中的碰撞检测、视线计算和高度查询等功能。
 * 该接口提供了加载/卸载地图、视线检测、高度获取等核心功能的最小化抽象。
 *
 * 主要功能：
 * - 地图数据的加载和卸载
 * - 视线（Line of Sight）检测
 * - 地面高度计算
 * - 物体碰撞检测
 * - 区域信息查询
 * - 液体（水面）信息查询
 */

#ifndef _IVMAPMANAGER_H
#define _IVMAPMANAGER_H

#include "Define.h"
#include "ModelIgnoreFlags.h"
#include "Optional.h"
#include <string>

//===========================================================

namespace VMAP
{
    /**
     * @enum VMAP_LOAD_RESULT
     * @brief VMap加载结果枚举（旧版接口）
     *
     * 用于表示地图加载操作的结果状态
     */
    enum VMAP_LOAD_RESULT
    {
        VMAP_LOAD_RESULT_ERROR,     ///< 加载过程中发生错误
        VMAP_LOAD_RESULT_OK,        ///< 加载成功
        VMAP_LOAD_RESULT_IGNORED    ///< 加载请求被忽略（如已加载或配置禁用）
    };

    /**
     * @enum LoadResult
     * @brief VMap加载结果枚举（新版接口）
     *
     * 用于表示地图文件存在性检查的结果状态
     */
    enum class LoadResult : uint8
    {
        Success,        ///< 加载成功
        FileNotFound,   ///< 文件未找到
        VersionMismatch ///< 版本不匹配
    };

    /// 无效高度检查值，用于判断高度值是否有效
    #define VMAP_INVALID_HEIGHT       -100000.0f
    /// 无效高度的实际赋值，当高度未知时使用此值
    #define VMAP_INVALID_HEIGHT_VALUE -200000.0f

    /**
     * @struct AreaAndLiquidData
     * @brief 区域和液体数据结构
     *
     * 用于一次性获取指定位置的区域信息和液体信息，
     * 避免多次VMap查询，提高性能。
     */
    struct AreaAndLiquidData
    {
        /**
         * @struct AreaInfo
         * @brief 区域信息结构
         *
         * 包含WMO（World Model Object）相关的区域标识信息
         */
        struct AreaInfo
        {
            /**
             * @brief 构造函数
             * @param _adtId ADT文件标识符
             * @param _rootId WMO根节点标识符
             * @param _groupId WMO组标识符
             * @param _flags MOGP标志位
             */
            AreaInfo(int32 _adtId, int32 _rootId, int32 _groupId, uint32 _flags) : adtId(_adtId), rootId(_rootId), groupId(_groupId), mogpFlags(_flags) { }
            int32 const adtId;      ///< ADT（地图分块）文件ID
            int32 const rootId;     ///< WMO模型根节点ID
            int32 const groupId;    ///< WMO组ID（用于区分同一WMO的不同部分）
            uint32 const mogpFlags; ///< MOGP（Map Object Group Patch）标志位
        };

        /**
         * @struct LiquidInfo
         * @brief 液体信息结构
         *
         * 包含液体类型和液面高度信息
         */
        struct LiquidInfo
        {
            /**
             * @brief 构造函数
             * @param _type 液体类型（如水、岩浆、淤泥等）
             * @param _level 液面高度
             */
            LiquidInfo(uint32 _type, float _level) : type(_type), level(_level) { }
            uint32 const type;  ///< 液体类型标识
            float const level;  ///< 液面高度（Z坐标）
        };

        float floorZ = VMAP_INVALID_HEIGHT;     ///< 地面高度，初始为无效值
        Optional<AreaInfo> areaInfo;            ///< 区域信息，可能不存在
        Optional<LiquidInfo> liquidInfo;        ///< 液体信息，可能不存在
    };

    //===========================================================
    /**
     * @class IVMapManager
     * @brief VMap管理器抽象接口
     *
     * 这是VMapManager的最小化接口，定义了虚拟地图管理器的核心功能。
     * 提供地图加载、视线检测、高度计算、碰撞检测等功能。
     *
     * 设计模式：接口模式（抽象基类）
     * 实现类：VMapManager2
     *
     * 主要功能：
     * 1. 地图生命周期管理：加载、卸载单个瓦片或整个地图
     * 2. 视线检测：判断两点之间是否有障碍物
     * 3. 高度计算：获取指定坐标的地面高度
     * 4. 碰撞检测：计算射线与物体的交点
     * 5. 区域查询：获取区域标志和液体信息
     *
     * 性能注意事项：
     * - 地图加载操作较重，应在地图初始化时完成
     * - 视线和高度检测调用频繁，需要优化
     * - 建议在服务器启动时预加载常用地图
     */
    class TC_COMMON_API IVMapManager
    {
        private:
            bool iEnableLineOfSightCalc;   ///< 是否启用视线计算功能
            bool iEnableHeightCalc;        ///< 是否启用高度计算功能

        public:
            /**
             * @brief 构造函数
             *
             * 默认启用视线计算和高度计算功能
             */
            IVMapManager() : iEnableLineOfSightCalc(true), iEnableHeightCalc(true) { }

            /**
             * @brief 虚析构函数
             *
             * 确保派生类正确释放资源
             */
            virtual ~IVMapManager(void) { }

            /**
             * @brief 加载指定地图的瓦片数据
             *
             * @param pBasePath 数据文件基础路径
             * @param pMapId 地图ID
             * @param x 瓦片X坐标（网格坐标）
             * @param y 瓦片Y坐标（网格坐标）
             * @return 加载结果（VMAP_LOAD_RESULT）
             *
             * 调用时机：地图实例创建时或玩家进入新区域时
             * 性能注意事项：此操作涉及文件I/O，应避免在游戏循环中调用
             */
            virtual int loadMap(char const* pBasePath, unsigned int pMapId, int x, int y) = 0;

            /**
             * @brief 检查地图文件是否存在
             *
             * @param pBasePath 数据文件基础路径
             * @param pMapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 文件检查结果
             *
             * 调用时机：预加载检查或错误诊断时
             */
            virtual LoadResult existsMap(char const* pBasePath, unsigned int pMapId, int x, int y) = 0;

            /**
             * @brief 卸载指定地图的单个瓦片
             *
             * @param pMapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             *
             * 调用时机：地图卸载或内存清理时
             */
            virtual void unloadMap(unsigned int pMapId, int x, int y) = 0;

            /**
             * @brief 卸载指定地图的所有瓦片
             *
             * @param pMapId 地图ID
             *
             * 调用时机：地图实例销毁时
             */
            virtual void unloadMap(unsigned int pMapId) = 0;

            /**
             * @brief 检测两点之间是否有视线阻挡
             *
             * @param pMapId 地图ID
             * @param x1,y1,z1 起点坐标
             * @param x2,y2,z2 终点坐标
             * @param ignoreFlags 忽略的模型类型标志
             * @return true表示视线被阻挡，false表示视线畅通
             *
             * 调用时机：技能施放、NPC攻击判定、玩家交互检测等
             * 性能注意事项：高频调用，需优化数据结构
             */
            virtual bool isInLineOfSight(unsigned int pMapId, float x1, float y1, float z1, float x2, float y2, float z2, ModelIgnoreFlags ignoreFlags) = 0;

            /**
             * @brief 获取指定位置的地面高度
             *
             * @param pMapId 地图ID
             * @param x,y,z 查询坐标（z为搜索起点高度）
             * @param maxSearchDist 最大搜索距离
             * @return 地面高度，如果未找到返回VMAP_INVALID_HEIGHT
             *
             * 调用时机：生物移动、落地检测、高度校验等
             * 性能注意事项：高频调用，核心路径需优化
             */
            virtual float getHeight(unsigned int pMapId, float x, float y, float z, float maxSearchDist) = 0;

            /**
             * @brief 计算射线与物体的交点位置
             *
             * @param pMapId 地图ID
             * @param x1,y1,z1 射线起点
             * @param x2,y2,z2 射线终点
             * @param rx,ry,rz [out] 碰撞点坐标，无碰撞时返回终点
             * @param pModifyDist 距离修正值（向起点方向偏移）
             * @return true表示发生碰撞，false表示无碰撞
             *
             * 调用时机：投射物计算、移动碰撞检测等
             */
            virtual bool getObjectHitPos(unsigned int pMapId, float x1, float y1, float z1, float x2, float y2, float z2, float& rx, float &ry, float& rz, float pModifyDist) = 0;

            /**
             * @brief 处理调试命令
             *
             * @param pCommand 命令字符串
             * @return 命令执行结果
             *
             * 调用时机：GM调试或开发测试时
             */
            virtual bool processCommand(char *pCommand)= 0;

            /**
             * @brief 设置是否启用视线计算
             *
             * @param pVal true启用，false禁用
             *
             * 注意：游戏中途启用需要手动加载地图数据
             * 调用时机：配置加载或GM命令
             */
            void setEnableLineOfSightCalc(bool pVal) { iEnableLineOfSightCalc = pVal; }

            /**
             * @brief 设置是否启用高度计算
             *
             * @param pVal true启用，false禁用
             *
             * 注意：游戏中途启用需要手动加载地图数据
             * 调用时机：配置加载或GM命令
             */
            void setEnableHeightCalc(bool pVal) { iEnableHeightCalc = pVal; }

            /**
             * @brief 检查视线计算是否启用
             * @return 启用状态
             */
            bool isLineOfSightCalcEnabled() const { return(iEnableLineOfSightCalc); }

            /**
             * @brief 检查高度计算是否启用
             * @return 启用状态
             */
            bool isHeightCalcEnabled() const { return(iEnableHeightCalc); }

            /**
             * @brief 检查是否需要加载地图数据
             * @return 如果视线或高度任一功能启用则返回true
             */
            bool isMapLoadingEnabled() const { return(iEnableLineOfSightCalc || iEnableHeightCalc  ); }

            /**
             * @brief 获取地图数据文件的完整路径
             *
             * @param pMapId 地图ID
             * @param x 瓦片X坐标
             * @param y 瓦片Y坐标
             * @return 文件路径字符串
             */
            virtual std::string getDirFileName(unsigned int pMapId, int x, int y) const =0;

            /**
             * @brief 查询世界模型区域信息
             *
             * @param mapId 地图ID
             * @param x,y 查询坐标
             * @param z [in/out] 输入查询高度，输出地面高度
             * @param flags [out] 区域标志位
             * @param adtId [out] ADT文件ID
             * @param rootId [out] WMO根节点ID
             * @param groupId [out] WMO组ID
             * @return 是否找到有效区域信息
             *
             * 调用时机：区域判定、室内/室外检测等
             */
            virtual bool getAreaInfo(uint32 mapId, float x, float y, float &z, uint32 &flags, int32 &adtId, int32 &rootId, int32 &groupId) const=0;

            /**
             * @brief 获取液体（水面）信息
             *
             * @param mapId 地图ID
             * @param x,y,z 查询坐标
             * @param reqLiquidType 请求的液体类型（0表示任意类型）
             * @param level [out] 液面高度
             * @param floor [out] 液体底部高度
             * @param type [out] 液体类型
             * @param mogpFlags [out] MOGP标志
             * @return 是否找到液体
             *
             * 调用时机：游泳判定、水下呼吸、液体效果等
             */
            virtual bool GetLiquidLevel(uint32 mapId, float x, float y, float z, uint8 reqLiquidType, float& level, float& floor, uint32& type, uint32& mogpFlags) const=0;

            /**
             * @brief 一次性获取区域和液体数据
             *
             * @param mapId 地图ID
             * @param x,y,z 查询坐标
             * @param reqLiquidType 请求的液体类型
             * @param data [out] 区域和液体数据结构
             *
             * 优化说明：单次查询获取两种信息，减少VMap查询次数
             * 调用时机：需要同时获取区域和液体信息时
             */
            virtual void getAreaAndLiquidData(unsigned int mapId, float x, float y, float z, uint8 reqLiquidType, AreaAndLiquidData& data) const=0;
    };

}
#endif
