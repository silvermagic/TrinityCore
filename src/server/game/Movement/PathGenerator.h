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
 * @file PathGenerator.h
 * @brief 寻路系统核心模块
 *
 * 本模块实现了基于 Detour 导航网格的寻路功能,为游戏单位提供智能的移动路径计算。
 * 是移动系统的核心组件,负责计算从起点到终点的最优路径。
 *
 * 主要职责:
 * - 使用 Detour 导航网格库进行寻路计算
 * - 支持直线路径和曲线平滑路径
 * - 处理地形、障碍物和可通行区域
 * - 优化路径以获得自然的移动效果
 *
 * 寻路算法:
 * - 多边形路径:使用 A* 算法在导航网格上查找多边形序列
 * - 点路径:从多边形路径生成具体的路径点
 * - 平滑路径:优化路径点使移动更自然
 *
 * 性能优化:
 * - 路径缓存:复用已计算的路径
 * - 增量更新:仅重新计算变化的部分路径
 * - 射线检测:短距离使用快速射线检测
 *
 * 相关模块:
 * - MotionMaster: 使用路径生成器控制单位移动
 * - MMapManager: 提供导航网格数据
 * - Detour库: 底层寻路算法实现
 */

#ifndef _PATH_GENERATOR_H
#define _PATH_GENERATOR_H

#include "MapDefines.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MoveSplineInitArgs.h"
#include <G3D/Vector3.h>

class Unit;
class WorldObject;

// ============================================================================
// 寻路常量定义
// ============================================================================

/// 最大多边形路径长度: 74*4.0f=296码,远超实际逃避范围
#define MAX_PATH_LENGTH         74

/// 最大点路径长度:与多边形路径长度相同
#define MAX_POINT_PATH_LENGTH   74

/// 平滑路径步进大小:4.0码,用于路径平滑处理
#define SMOOTH_PATH_STEP_SIZE   4.0f

/// 平滑路径容差:0.3码,用于判断点是否接近目标
#define SMOOTH_PATH_SLOP        0.3f

/// 顶点尺寸:每个顶点包含3个浮点数(x,y,z)
#define VERTEX_SIZE       3

/// 无效多边形引用:表示未找到有效的导航网格多边形
#define INVALID_POLYREF   0

/**
 * @enum PathType
 * @brief 路径类型枚举
 *
 * 描述寻路结果的类型和状态,可以组合使用多个标志位
 */
enum PathType
{
    PATHFIND_BLANK             = 0x00,  ///< 路径尚未构建
    PATHFIND_NORMAL            = 0x01,  ///< 正常路径,完全可通行
    PATHFIND_SHORTCUT          = 0x02,  ///< 捷径:穿越障碍物、地形或空中(旧行为)
    PATHFIND_INCOMPLETE        = 0x04,  ///< 不完整路径:部分路径可达,正在接近目标
    PATHFIND_NOPATH            = 0x08,  ///< 无路径:无法找到有效路径或生成失败
    PATHFIND_NOT_USING_PATH    = 0x10,  ///< 不使用导航网格:飞行、游泳或地图无mmap数据
    PATHFIND_SHORT             = 0x20,  ///< 短路径:路径长度达到限制值
    PATHFIND_FARFROMPOLY_START = 0x40,  ///< 起点远离导航网格多边形
    PATHFIND_FARFROMPOLY_END   = 0x80,  ///< 终点远离导航网格多边形
    PATHFIND_FARFROMPOLY       = PATHFIND_FARFROMPOLY_START | PATHFIND_FARFROMPOLY_END, ///< 起点或终点远离多边形
};

/**
 * @class PathGenerator
 * @brief 路径生成器
 *
 * 基于 Detour 导航网格实现寻路功能,为游戏单位计算从起点到终点的移动路径。
 * 支持多种寻路模式,包括直线、平滑曲线和射线检测。
 *
 * 使用流程:
 * 1. 构造 PathGenerator 对象
 * 2. 设置寻路选项(可选)
 * 3. 调用 CalculatePath 计算路径
 * 4. 获取路径结果
 *
 * 性能考虑:
 * - 路径计算相对耗时,避免频繁调用
 * - 短距离移动可以使用射线检测优化
 * - 利用路径缓存减少重复计算
 */
class TC_GAME_API PathGenerator
{
    public:
        /**
         * @brief 构造函数
         * @param owner 路径所有者(通常是移动的单位)
         *
         * 初始化路径生成器,加载导航网格数据
         */
        explicit PathGenerator(WorldObject const* owner);

        /**
         * @brief 析构函数
         */
        ~PathGenerator();

        /**
         * @brief 计算路径
         * @param destX 目标点X坐标
         * @param destY 目标点Y坐标
         * @param destZ 目标点Z坐标
         * @param forceDest 是否强制到达目标点
         * @return true 表示成功计算新路径,false 表示无需改变
         *
         * 核心寻路方法,计算从当前位置到目标的移动路径。
         * 调用时机:单位需要移动到新位置时
         * 性能注意:计算密集型操作,避免频繁调用
         */
        bool CalculatePath(float destX, float destY, float destZ, bool forceDest = false);

        /**
         * @brief 检查目标Z坐标是否无效
         * @param target 目标单位
         * @return true 表示目标的Z坐标与实际终点差异过大
         *
         * 用于检测路径终点是否与目标位置有较大高度差。
         */
        bool IsInvalidDestinationZ(Unit const* target) const;

        // ========================================================================
        // 选项设置方法
        // ========================================================================

        /**
         * @brief 设置是否使用直线路径
         * @param useStraightPath true 使用直线,false 使用平滑路径
         */
        void SetUseStraightPath(bool useStraightPath) { _useStraightPath = useStraightPath; }

        /**
         * @brief 设置路径长度限制
         * @param distance 最大路径距离
         *
         * 限制生成的路径点数量,避免过长的路径。
         */
        void SetPathLengthLimit(float distance) { _pointPathLimit = std::min<uint32>(uint32(distance/SMOOTH_PATH_STEP_SIZE), MAX_POINT_PATH_LENGTH); }

        /**
         * @brief 设置是否使用射线检测
         * @param useRaycast true 使用射线检测,false 使用标准寻路
         *
         * 射线检测适用于短距离直线移动,性能更好。
         */
        void SetUseRaycast(bool useRaycast) { _useRaycast = useRaycast; }

        // ========================================================================
        // 结果获取方法
        // ========================================================================

        /**
         * @brief 获取起点位置
         * @return 起点坐标向量
         */
        G3D::Vector3 const& GetStartPosition() const { return _startPosition; }

        /**
         * @brief 获取终点位置
         * @return 终点坐标向量
         */
        G3D::Vector3 const& GetEndPosition() const { return _endPosition; }

        /**
         * @brief 获取实际终点位置
         * @return 实际可达的终点坐标向量
         *
         * 当无法直接到达目标时,返回最接近的可达点。
         */
        G3D::Vector3 const& GetActualEndPosition() const { return _actualEndPosition; }

        /**
         * @brief 获取路径点数组
         * @return 路径点数组的常量引用
         */
        Movement::PointsArray const& GetPath() const { return _pathPoints; }

        /**
         * @brief 获取路径类型
         * @return 路径类型枚举值
         */
        PathType GetPathType() const { return _type; }

        /**
         * @brief 缩短路径直到距离目标指定距离
         * @param point 目标点
         * @param dist 期望的距离
         *
         * 用于在接近目标时提前停止,避免直接到达目标点。
         */
        void ShortenPathUntilDist(G3D::Vector3 const& point, float dist);

    private:
        // ========================================================================
        // 成员变量
        // ========================================================================

        dtPolyRef _pathPolyRefs[MAX_PATH_LENGTH];   ///< Detour多边形引用数组,存储路径上的多边形序列
        uint32 _polyLength;                         ///< 路径中的多边形数量

        Movement::PointsArray _pathPoints;          ///< 实际的(x,y,z)路径点数组,单位移动的路线
        PathType _type;                             ///< 路径类型,描述路径的状态和特征

        bool _useStraightPath;      ///< 是否生成直线路径(不进行平滑处理)
        bool _forceDestination;     ///< 是否强制到达指定终点(即使需要穿越障碍)
        uint32 _pointPathLimit;     ///< 路径点数量限制,取min(计算值, MAX_POINT_PATH_LENGTH)
        bool _useRaycast;           ///< 是否使用射线检测进行快速寻路

        G3D::Vector3 _startPosition;        ///< 起点{x, y, z}坐标
        G3D::Vector3 _endPosition;          ///< 目标终点{x, y, z}坐标
        G3D::Vector3 _actualEndPosition;    ///< 实际可达终点{x, y, z}坐标(可能不同于目标终点)

        WorldObject const* const _source;       ///< 移动对象的指针(不可变)
        dtNavMesh const* _navMesh;              ///< 导航网格指针,提供地形数据
        dtNavMeshQuery const* _navMeshQuery;    ///< 导航网格查询对象,用于寻路查询

        dtQueryFilter _filter;  ///< 查询过滤器,定义可通行的区域类型

        // ========================================================================
        // 私有辅助方法
        // ========================================================================

        /**
         * @brief 设置起点位置
         * @param point 起点坐标
         */
        void SetStartPosition(G3D::Vector3 const& point) { _startPosition = point; }

        /**
         * @brief 设置终点位置
         * @param point 终点坐标
         *
         * 同时设置目标和实际终点
         */
        void SetEndPosition(G3D::Vector3 const& point) { _actualEndPosition = point; _endPosition = point; }

        /**
         * @brief 设置实际终点位置
         * @param point 实际终点坐标
         */
        void SetActualEndPosition(G3D::Vector3 const& point) { _actualEndPosition = point; }

        /**
         * @brief 规范化路径
         *
         * 对路径点进行Z坐标修正,确保路径在地形上可行
         */
        void NormalizePath();

        /**
         * @brief 清空路径数据
         *
         * 重置多边形长度和路径点数组
         */
        void Clear()
        {
            _polyLength = 0;
            _pathPoints.clear();
        }

        /**
         * @brief 检查两点是否在指定范围内
         * @param p1 第一个点
         * @param p2 第二个点
         * @param r 水平距离阈值
         * @param h 高度差阈值
         * @return true 表示在范围内
         */
        bool InRange(G3D::Vector3 const& p1, G3D::Vector3 const& p2, float r, float h) const;

        /**
         * @brief 计算两点间的3D距离平方
         * @param p1 第一个点
         * @param p2 第二个点
         * @return 距离的平方值
         */
        float Dist3DSqr(G3D::Vector3 const& p1, G3D::Vector3 const& p2) const;

        /**
         * @brief 检查两点是否在指定范围内(YZX坐标系)
         * @param v1 第一个点数组(Y,Z,X顺序)
         * @param v2 第二个点数组(Y,Z,X顺序)
         * @param r 水平距离阈值
         * @param h 高度差阈值
         * @return true 表示在范围内
         */
        bool InRangeYZX(float const* v1, float const* v2, float r, float h) const;

        /**
         * @brief 在多边形路径中查找指定位置的多边形
         * @param polyPath 多边形路径数组
         * @param polyPathSize 多边形路径大小
         * @param Point 目标点
         * @param Distance [out] 到最近多边形的距离
         * @return 最近的多边形引用,如果未找到返回INVALID_POLYREF
         */
        dtPolyRef GetPathPolyByPosition(dtPolyRef const* polyPath, uint32 polyPathSize, float const* Point, float* Distance = nullptr) const;

        /**
         * @brief 根据位置获取多边形
         * @param Point 目标点
         * @param Distance [out] 到多边形的距离
         * @return 最近的可行走多边形引用
         *
         * 优先在当前路径中查找,如果未找到则进行全局搜索
         */
        dtPolyRef GetPolyByLocation(float const* Point, float* Distance) const;

        /**
         * @brief 检查指定位置是否有导航网格瓦片
         * @param p 目标位置
         * @return true 表示该位置有导航网格数据
         */
        bool HaveTile(G3D::Vector3 const& p) const;

        /**
         * @brief 构建多边形路径
         * @param startPos 起点位置
         * @param endPos 终点位置
         *
         * 使用A*算法在导航网格上查找多边形序列
         */
        void BuildPolyPath(G3D::Vector3 const& startPos, G3D::Vector3 const& endPos);

        /**
         * @brief 构建点路径
         * @param startPoint 起点坐标数组
         * @param endPoint 终点坐标数组
         *
         * 从多边形路径生成具体的路径点
         */
        void BuildPointPath(float const* startPoint, float const* endPoint);

        /**
         * @brief 构建捷径路径
         *
         * 创建从起点到终点的直线捷径(不进行寻路)
         */
        void BuildShortcut();

        /**
         * @brief 获取导航地形标志
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @return 导航地形标志(地面、水面、岩浆等)
         */
        NavTerrainFlag GetNavTerrain(float x, float y, float z);

        /**
         * @brief 创建查询过滤器
         *
         * 根据单位类型初始化可通行区域过滤器
         */
        void CreateFilter();

        /**
         * @brief 更新查询过滤器
         *
         * 根据单位当前状态动态调整可通行区域
         */
        void UpdateFilter();

        // ========================================================================
        // 平滑路径辅助函数
        // ========================================================================

        /**
         * @brief 修复走廊
         * @param path 当前路径
         * @param npath 当前路径长度
         * @param maxPath 最大路径长度
         * @param visited 访问过的多边形
         * @param nvisited 访问过的多边形数量
         * @return 新的路径长度
         *
         * 优化多边形路径,移除不必要的绕行
         */
        uint32 FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath, dtPolyRef const* visited, uint32 nvisited);

        /**
         * @brief 获取转向目标
         * @param startPos 起点
         * @param endPos 终点
         * @param minTargetDist 最小目标距离
         * @param path 多边形路径
         * @param pathSize 路径大小
         * @param steerPos [out] 转向位置
         * @param steerPosFlag [out] 转向位置标志
         * @param steerPosRef [out] 转向位置多边形引用
         * @return true 表示找到转向目标
         */
        bool GetSteerTarget(float const* startPos, float const* endPos, float minTargetDist, dtPolyRef const* path, uint32 pathSize, float* steerPos,
                            unsigned char& steerPosFlag, dtPolyRef& steerPosRef);

        /**
         * @brief 查找平滑路径
         * @param startPos 起点
         * @param endPos 终点
         * @param polyPath 多边形路径
         * @param polyPathSize 多边形路径大小
         * @param smoothPath [out] 平滑路径点数组
         * @param smoothPathSize [out] 平滑路径点数量
         * @param smoothPathMaxSize 平滑路径最大容量
         * @return Detour状态码
         *
         * 使用平滑算法生成自然的移动路径
         */
        dtStatus FindSmoothPath(float const* startPos, float const* endPos,
                              dtPolyRef const* polyPath, uint32 polyPathSize,
                              float* smoothPath, int* smoothPathSize, uint32 smoothPathMaxSize);

        /**
         * @brief 添加远离多边形标志
         * @param startFarFromPoly 起点是否远离多边形
         * @param endFarFromPoly 终点是否远离多边形
         */
        void AddFarFromPolyFlags(bool startFarFromPoly, bool endFarFromPoly);
};

#endif
