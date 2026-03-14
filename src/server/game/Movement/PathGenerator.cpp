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
 * @file PathGenerator.cpp
 * @brief 寻路系统核心实现模块
 *
 * 本模块实现了基于 Detour 导航网格的寻路算法,包括:
 * - 多边形路径计算(A*算法)
 * - 点路径生成和优化
 * - 平滑路径处理
 * - 特殊地形处理(水面、飞行等)
 *
 * 算法流程:
 * 1. 验证起点和终点坐标有效性
 * 2. 检查导航网格可用性
 * 3. 查找起点和终点的多边形
 * 4. 使用A*算法计算多边形路径
 * 5. 从多边形路径生成路径点
 * 6. 可选的路径平滑处理
 *
 * 性能优化策略:
 * - 路径缓存和增量更新
 * - 射线检测用于短距离
 * - 限制路径长度避免过度计算
 */

#include "PathGenerator.h"
#include "Map.h"
#include "Creature.h"
#include "MMapFactory.h"
#include "MMapManager.h"
#include "Log.h"
#include "DisableMgr.h"
#include "DetourCommon.h"
#include "DetourNavMeshQuery.h"
#include "Metric.h"

////////////////// PathGenerator //////////////////

/**
 * @brief PathGenerator 构造函数实现
 *
 * 初始化路径生成器,加载导航网格数据并设置过滤器。
 * 如果地图启用了寻路功能,则加载对应的导航网格查询对象。
 */
PathGenerator::PathGenerator(WorldObject const* owner) :
    _polyLength(0), _type(PATHFIND_BLANK), _useStraightPath(false),
    _forceDestination(false), _pointPathLimit(MAX_POINT_PATH_LENGTH), _useRaycast(false),
    _endPosition(G3D::Vector3::zero()), _source(owner), _navMesh(nullptr),
    _navMeshQuery(nullptr)
{
    // 初始化多边形引用数组为0
    memset(_pathPolyRefs, 0, sizeof(_pathPolyRefs));

    TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::PathGenerator for {}", _source->GetGUID().ToString());

    // 获取地图ID并检查是否启用寻路
    uint32 mapId = _source->GetMapId();
    if (DisableMgr::IsPathfindingEnabled(mapId))
    {
        // 获取导航网格管理器并加载导航网格数据
        MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
        _navMeshQuery = mmap->GetNavMeshQuery(mapId, _source->GetInstanceId());
        _navMesh = _navMeshQuery ? _navMeshQuery->getAttachedNavMesh() : mmap->GetNavMesh(mapId);
    }

    // 创建查询过滤器
    CreateFilter();
}

/**
 * @brief PathGenerator 析构函数实现
 */
PathGenerator::~PathGenerator()
{
    TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::~PathGenerator() for {}", _source->GetGUID().ToString());
}

/**
 * @brief 计算从起点到终点的移动路径
 *
 * 核心寻路方法,执行完整的寻路流程:
 * 1. 验证坐标有效性
 * 2. 检查导航网格可用性
 * 3. 计算多边形路径
 * 4. 生成点路径
 */
bool PathGenerator::CalculatePath(float destX, float destY, float destZ, bool forceDest)
{
    // 获取单位当前位置作为起点
    float x, y, z;
    _source->GetPosition(x, y, z);

    // 验证起点和终点坐标的有效性
    if (!Trinity::IsValidMapCoord(destX, destY, destZ) || !Trinity::IsValidMapCoord(x, y, z))
        return false;

    // 记录性能指标
    TC_METRIC_DETAILED_EVENT("mmap_events", "CalculatePath", "");

    // 设置终点位置
    G3D::Vector3 dest(destX, destY, destZ);
    SetEndPosition(dest);

    // 设置起点位置
    G3D::Vector3 start(x, y, z);
    SetStartPosition(start);

    // 设置是否强制到达终点
    _forceDestination = forceDest;

    TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::CalculatePath() for {}", _source->GetGUID().ToString());

    // 检查导航网格是否可用:
    // - 导航网格对象是否存在
    // - 导航网格查询对象是否存在
    // - 单位是否忽略寻路
    // - 起点和终点是否有导航网格瓦片
    Unit const* _sourceUnit = _source->ToUnit();
    if (!_navMesh || !_navMeshQuery || (_sourceUnit && _sourceUnit->HasUnitState(UNIT_STATE_IGNORE_PATHFINDING)) ||
        !HaveTile(start) || !HaveTile(dest))
    {
        // 如果导航网格不可用,构建直线路径(不进行寻路)
        BuildShortcut();
        _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
        return true;
    }

    // 更新查询过滤器
    UpdateFilter();

    // 构建多边形路径
    BuildPolyPath(start, dest);
    return true;
}

/**
 * @brief 在多边形路径中查找指定位置的多边形
 *
 * 遍历多边形路径,找到距离指定点最近的多边形。
 * 用于在已有路径中快速定位当前所在的多边形。
 */
dtPolyRef PathGenerator::GetPathPolyByPosition(dtPolyRef const* polyPath, uint32 polyPathSize, float const* point, float* distance) const
{
    if (!polyPath || !polyPathSize)
        return INVALID_POLYREF;

    dtPolyRef nearestPoly = INVALID_POLYREF;
    float minDist = FLT_MAX;

    // 遍历所有多边形,计算到指定点的距离
    for (uint32 i = 0; i < polyPathSize; ++i)
    {
        float closestPoint[VERTEX_SIZE];
        // 在多边形上找到距离指定点最近的点
        if (dtStatusFailed(_navMeshQuery->closestPointOnPoly(polyPath[i], point, closestPoint, nullptr)))
            continue;

        // 计算距离的平方
        float d = dtVdistSqr(point, closestPoint);
        if (d < minDist)
        {
            minDist = d;
            nearestPoly = polyPath[i];
        }

        // 如果距离足够小,提前退出循环
        if (minDist < 1.0f) // shortcut out - close enough for us
            break;
    }

    if (distance)
        *distance = dtMathSqrtf(minDist);

    // 只有当距离小于3.0时才认为找到了有效的多边形
    return (minDist < 3.0f) ? nearestPoly : INVALID_POLYREF;
}

/**
 * @brief 根据位置获取多边形
 *
 * 首先在当前路径中查找,如果未找到则进行全局搜索。
 * 使用分层搜索策略,先用小范围搜索,失败后扩大搜索范围。
 */
dtPolyRef PathGenerator::GetPolyByLocation(float const* point, float* distance) const
{
    // 首先检查当前路径中是否包含该位置的多边形
    // 这样可以避免使用昂贵的 findNearestPoly 操作
    dtPolyRef polyRef = GetPathPolyByPosition(_pathPolyRefs, _polyLength, point, distance);
    if (polyRef != INVALID_POLYREF)
        return polyRef;

    // 当前路径中没有找到,使用 findNearestPoly 进行全局搜索
    // 首先尝试使用较小的搜索范围
    float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};    // 多边形搜索区域边界
    float closestPoint[VERTEX_SIZE] = {0.0f, 0.0f, 0.0f};
    if (dtStatusSucceed(_navMeshQuery->findNearestPoly(point, extents, &_filter, &polyRef, closestPoint)) && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    // 仍然没有找到,尝试使用更大的搜索范围
    // 注意:搜索范围不应覆盖超过128个多边形(参见 dtNavMeshQuery::findNearestPoly)
    extents[1] = 50.0f;

    if (dtStatusSucceed(_navMeshQuery->findNearestPoly(point, extents, &_filter, &polyRef, closestPoint)) && polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }

    // 完全找不到有效的多边形
    *distance = FLT_MAX;
    return INVALID_POLYREF;
}

/**
 * @brief 构建多边形路径
 *
 * 使用 A* 算法在导航网格上查找从起点到终点的多边形序列。
 * 这是寻路的核心方法,处理各种边界情况和优化策略。
 */
void PathGenerator::BuildPolyPath(G3D::Vector3 const& startPos, G3D::Vector3 const& endPos)
{
    // *** 获取起点和终点多边形逻辑 ***

    float distToStartPoly, distToEndPoly;
    // 注意:Detour 使用 YZX 坐标顺序,需要转换
    float startPoint[VERTEX_SIZE] = {startPos.y, startPos.z, startPos.x};
    float endPoint[VERTEX_SIZE] = {endPos.y, endPos.z, endPos.x};

    // 查找起点和终点所在的多边形
    dtPolyRef startPoly = GetPolyByLocation(startPoint, &distToStartPoly);
    dtPolyRef endPoly = GetPolyByLocation(endPoint, &distToEndPoly);

    _type = PathType(PATHFIND_NORMAL);

    // 导航网格中存在空洞(无法找到起点或终点多边形)
    // 构建捷径路径并标记为 NOPATH (飞行和游泳单位除外)
    // 调用者自行决定如何处理这种情况
    if (startPoly == INVALID_POLYREF || endPoly == INVALID_POLYREF)
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: (startPoly == 0 || endPoly == 0)");
        BuildShortcut();

        // 检查单位是否可以飞行
        bool path = _source->GetTypeId() == TYPEID_UNIT && _source->ToCreature()->CanFly();

        // 检查单位是否可以游泳
        bool waterPath = _source->GetTypeId() == TYPEID_UNIT && _source->ToCreature()->CanSwim();
        if (waterPath)
        {
            // 检查起点和终点是否都在水中,如果是则允许移动
            for (uint32 i = 0; i < _pathPoints.size(); ++i)
            {
                ZLiquidStatus status = _source->GetMap()->GetLiquidStatus(_source->GetPhaseMask(), _pathPoints[i].x, _pathPoints[i].y, _pathPoints[i].z, MAP_ALL_LIQUIDS, nullptr, _source->GetCollisionHeight());
                // 如果有一个点不在水中,取消移动
                if (status == LIQUID_MAP_NO_WATER)
                {
                    waterPath = false;
                    break;
                }
            }
        }

        // 如果单位可以飞行或游泳,允许使用捷径
        if (path || waterPath)
        {
            _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
            return;
        }

        // 射线检测不需要终点多边形有效
        if (!_useRaycast)
        {
            _type = PATHFIND_NOPATH;
            return;
        }
    }

    // 检查起点或终点是否远离导航网格多边形(距离大于7码)
    // 这个阈值可能需要调整
    bool startFarFromPoly = distToStartPoly > 7.0f;
    bool endFarFromPoly = distToEndPoly > 7.0f;
    if (startFarFromPoly || endFarFromPoly)
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: farFromPoly distToStartPoly={:.3f} distToEndPoly={:.3f}", distToStartPoly, distToEndPoly);

        bool buildShotrcut = false;

        // 判断是起点还是终点远离多边形
        G3D::Vector3 const& p = (distToStartPoly > 7.0f) ? startPos : endPos;

        // 检查是否在水下
        if (_source->GetMap()->IsUnderWater(_source->GetPhaseMask(), p.x, p.y, p.z))
        {
            TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: underWater case");
            // 如果单位可以游泳,允许构建捷径
            if (Unit const* _sourceUnit = _source->ToUnit())
                if (_sourceUnit->CanSwim())
                    buildShotrcut = true;
        }
        else
        {
            TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: flying case");
            if (Unit const* _sourceUnit = _source->ToUnit())
            {
                // 如果单位可以飞行,允许构建捷径
                if (_sourceUnit->CanFly())
                    buildShotrcut = true;
                // 如果单位正在下落且试图向下移动(如冲锋),允许构建捷径
                else if (_sourceUnit->IsFalling() && endPos.z < startPos.z)
                    buildShotrcut = true;
            }
        }

        if (buildShotrcut)
        {
            // 构建捷径路径
            BuildShortcut();
            _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);

            AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);

            return;
        }
        else
        {
            // 无法构建捷径,将终点设置为最近的多边形边界点
            float closestPoint[VERTEX_SIZE];
            // 可以考虑使用 closestPointOnPolyBoundary 替代
            if (dtStatusSucceed(_navMeshQuery->closestPointOnPoly(endPoly, endPoint, closestPoint, nullptr)))
            {
                dtVcopy(endPoint, closestPoint);
                SetActualEndPosition(G3D::Vector3(endPoint[2], endPoint[0], endPoint[1]));
            }

            // 标记路径为不完整
            _type = PathType(PATHFIND_INCOMPLETE);

            AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);
        }
    }

    // *** poly path generating logic ***

    // start and end are on same polygon
    // handle this case as if they were 2 different polygons, building a line path split in some few points
    if (startPoly == endPoly && !_useRaycast)
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: (startPoly == endPoly)");

        _pathPolyRefs[0] = startPoly;
        _polyLength = 1;

        if (startFarFromPoly || endFarFromPoly)
        {
            _type = PathType(PATHFIND_INCOMPLETE);

            AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);
        }
        else
         _type = PATHFIND_NORMAL;

        BuildPointPath(startPoint, endPoint);
        return;
    }

    // look for startPoly/endPoly in current path
    /// @todo we can merge it with getPathPolyByPosition() loop
    bool startPolyFound = false;
    bool endPolyFound = false;
    uint32 pathStartIndex = 0;
    uint32 pathEndIndex = 0;

    if (_polyLength)
    {
        for (; pathStartIndex < _polyLength; ++pathStartIndex)
        {
            // here to catch few bugs
            if (_pathPolyRefs[pathStartIndex] == INVALID_POLYREF)
            {
                TC_LOG_ERROR("maps.mmaps", "Invalid poly ref in BuildPolyPath. _polyLength: {}, pathStartIndex: {},"
                                     " startPos: {}, endPos: {}, mapid: {}",
                                     _polyLength, pathStartIndex, startPos.toString(), endPos.toString(),
                                     _source->GetMapId());

                break;
            }

            if (_pathPolyRefs[pathStartIndex] == startPoly)
            {
                startPolyFound = true;
                break;
            }
        }

        for (pathEndIndex = _polyLength-1; pathEndIndex > pathStartIndex; --pathEndIndex)
            if (_pathPolyRefs[pathEndIndex] == endPoly)
            {
                endPolyFound = true;
                break;
            }
    }

    if (startPolyFound && endPolyFound)
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: (startPolyFound && endPolyFound)");

        // we moved along the path and the target did not move out of our old poly-path
        // our path is a simple subpath case, we have all the data we need
        // just "cut" it out

        _polyLength = pathEndIndex - pathStartIndex + 1;
        memmove(_pathPolyRefs, _pathPolyRefs + pathStartIndex, _polyLength * sizeof(dtPolyRef));
    }
    else if (startPolyFound && !endPolyFound)
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: (startPolyFound && !endPolyFound)");

        // we are moving on the old path but target moved out
        // so we have atleast part of poly-path ready

        _polyLength -= pathStartIndex;

        // try to adjust the suffix of the path instead of recalculating entire length
        // at given interval the target cannot get too far from its last location
        // thus we have less poly to cover
        // sub-path of optimal path is optimal

        // take ~80% of the original length
        /// @todo play with the values here
        uint32 prefixPolyLength = uint32(_polyLength * 0.8f + 0.5f);
        memmove(_pathPolyRefs, _pathPolyRefs+pathStartIndex, prefixPolyLength * sizeof(dtPolyRef));

        dtPolyRef suffixStartPoly = _pathPolyRefs[prefixPolyLength-1];

        // we need any point on our suffix start poly to generate poly-path, so we need last poly in prefix data
        float suffixEndPoint[VERTEX_SIZE];
        if (dtStatusFailed(_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, nullptr)))
        {
            // we can hit offmesh connection as last poly - closestPointOnPoly() don't like that
            // try to recover by using prev polyref
            --prefixPolyLength;
            suffixStartPoly = _pathPolyRefs[prefixPolyLength-1];
            if (dtStatusFailed(_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, nullptr)))
            {
                // suffixStartPoly is still invalid, error state
                BuildShortcut();
                _type = PATHFIND_NOPATH;
                return;
            }
        }

        // generate suffix
        uint32 suffixPolyLength = 0;

        dtStatus dtResult;
        if (_useRaycast)
        {
            TC_LOG_ERROR("maps.mmaps", "PathGenerator::BuildPolyPath() called with _useRaycast with a previous path for unit {}", _source->GetGUID().ToString());
            BuildShortcut();
            _type = PATHFIND_NOPATH;
            return;
        }
        else
        {
            dtResult = _navMeshQuery->findPath(
                            suffixStartPoly,    // start polygon
                            endPoly,            // end polygon
                            suffixEndPoint,     // start position
                            endPoint,           // end position
                            &_filter,            // polygon search filter
                            _pathPolyRefs + prefixPolyLength - 1,    // [out] path
                            (int*)&suffixPolyLength,
                            MAX_PATH_LENGTH - prefixPolyLength);   // max number of polygons in output path
        }

        if (!suffixPolyLength || dtStatusFailed(dtResult))
        {
            // this is probably an error state, but we'll leave it
            // and hopefully recover on the next Update
            // we still need to copy our preffix
            TC_LOG_ERROR("maps.mmaps", "Path Build failed\n{}", _source->GetDebugInfo());
        }

        TC_LOG_DEBUG("maps.mmaps", "++  m_polyLength={} prefixPolyLength={} suffixPolyLength={}", _polyLength, prefixPolyLength, suffixPolyLength);

        // new path = prefix + suffix - overlap
        _polyLength = prefixPolyLength + suffixPolyLength - 1;
    }
    else
    {
        TC_LOG_DEBUG("maps.mmaps", "++ BuildPolyPath :: (!startPolyFound && !endPolyFound)");

        // either we have no path at all -> first run
        // or something went really wrong -> we aren't moving along the path to the target
        // just generate new path

        // free and invalidate old path data
        Clear();

        dtStatus dtResult;
        if (_useRaycast)
        {
            float hit = 0;
            float hitNormal[3];
            memset(hitNormal, 0, sizeof(hitNormal));

            dtResult = _navMeshQuery->raycast(
                            startPoly,
                            startPoint,
                            endPoint,
                            &_filter,
                            &hit,
                            hitNormal,
                            _pathPolyRefs,
                            (int*)&_polyLength,
                            MAX_PATH_LENGTH);

            if (!_polyLength || dtStatusFailed(dtResult))
            {
                BuildShortcut();
                _type = PATHFIND_NOPATH;
                AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);
                return;
            }

            // raycast() sets hit to FLT_MAX if there is a ray between start and end
            if (hit != FLT_MAX)
            {
                float hitPos[3];

                // Walk back a bit from the hit point to make sure it's in the mesh (sometimes the point is actually outside of the polygons due to float precision issues)
                hit *= 0.99f;
                dtVlerp(hitPos, startPoint, endPoint, hit);

                // if it fails again, clamp to poly boundary
                if (dtStatusFailed(_navMeshQuery->getPolyHeight(_pathPolyRefs[_polyLength - 1], hitPos, &hitPos[1])))
                    _navMeshQuery->closestPointOnPolyBoundary(_pathPolyRefs[_polyLength - 1], hitPos, hitPos);

                _pathPoints.resize(2);
                _pathPoints[0] = GetStartPosition();
                _pathPoints[1] = G3D::Vector3(hitPos[2], hitPos[0], hitPos[1]);

                NormalizePath();
                _type = PATHFIND_INCOMPLETE;
                AddFarFromPolyFlags(startFarFromPoly, false);
                return;
            }
            else
            {
                // clamp to poly boundary if we fail to get the height
                if (dtStatusFailed(_navMeshQuery->getPolyHeight(_pathPolyRefs[_polyLength - 1], endPoint, &endPoint[1])))
                    _navMeshQuery->closestPointOnPolyBoundary(_pathPolyRefs[_polyLength - 1], endPoint, endPoint);

                _pathPoints.resize(2);
                _pathPoints[0] = GetStartPosition();
                _pathPoints[1] = G3D::Vector3(endPoint[2], endPoint[0], endPoint[1]);

                NormalizePath();
                if (startFarFromPoly || endFarFromPoly)
                {
                    _type = PathType(PATHFIND_INCOMPLETE);

                    AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);
                }
                else
                    _type = PATHFIND_NORMAL;
                return;
            }
        }
        else
        {
            dtResult = _navMeshQuery->findPath(
                            startPoly,          // start polygon
                            endPoly,            // end polygon
                            startPoint,         // start position
                            endPoint,           // end position
                            &_filter,           // polygon search filter
                            _pathPolyRefs,     // [out] path
                            (int*)&_polyLength,
                            MAX_PATH_LENGTH);   // max number of polygons in output path
        }

        if (!_polyLength || dtStatusFailed(dtResult))
        {
            // only happens if we passed bad data to findPath(), or navmesh is messed up
            TC_LOG_ERROR("maps.mmaps", "{} Path Build failed: 0 length path", _source->GetGUID().ToString());
            BuildShortcut();
            _type = PATHFIND_NOPATH;
            return;
        }
    }

    // by now we know what type of path we can get
    if (_pathPolyRefs[_polyLength - 1] == endPoly && !(_type & PATHFIND_INCOMPLETE))
        _type = PATHFIND_NORMAL;
    else
        _type = PATHFIND_INCOMPLETE;

    AddFarFromPolyFlags(startFarFromPoly, endFarFromPoly);

    // generate the point-path out of our up-to-date poly-path
    BuildPointPath(startPoint, endPoint);
}

/**
 * @brief 构建点路径
 *
 * 从多边形路径生成具体的路径点数组。
 * 支持直线路径和平滑路径两种模式。
 */
void PathGenerator::BuildPointPath(const float *startPoint, const float *endPoint)
{
    float pathPoints[MAX_POINT_PATH_LENGTH*VERTEX_SIZE];
    uint32 pointCount = 0;
    dtStatus dtResult = DT_FAILURE;

    // 射线检测模式不支持构建多点路径,只能返回起点和终点/撞击点
    if (_useRaycast)
    {
        TC_LOG_ERROR("maps.mmaps", "PathGenerator::BuildPointPath() called with _useRaycast for unit {}", _source->GetGUID().ToString());
        BuildShortcut();
        _type = PATHFIND_NOPATH;
        return;
    }
    else if (_useStraightPath)
    {
        // 使用直线路径模式:沿多边形边界生成路径点
        dtResult = _navMeshQuery->findStraightPath(
                startPoint,         // 起点位置
                endPoint,           // 终点位置
                _pathPolyRefs,     // 当前多边形路径
                _polyLength,       // 多边形路径长度
                pathPoints,         // [out] 路径拐点
                nullptr,               // [out] 标志
                nullptr,               // [out] 缩短路径
                (int*)&pointCount,
                _pointPathLimit);   // 最大路径点数量
    }
    else
    {
        // 使用平滑路径模式:生成平滑的曲线路径
        dtResult = FindSmoothPath(
                startPoint,         // 起点位置
                endPoint,           // 终点位置
                _pathPolyRefs,     // 当前多边形路径
                _polyLength,       // 多边形路径长度
                pathPoints,         // [out] 平滑路径点
                (int*)&pointCount,
                _pointPathLimit);   // 最大路径点数量
    }

    // 特殊情况:起点和终点非常接近,只生成了一个点
    if (_polyLength == 1 && pointCount == 1)
    {
        // 第一个点是起点位置,追加终点位置
        dtVcopy(&pathPoints[1 * VERTEX_SIZE], endPoint);
        pointCount++;
    }
    else if ( pointCount < 2 || dtStatusFailed(dtResult))
    {
        // 只在传递错误数据到 findStraightPath 或导航网格损坏时发生
        // 单点路径可能在这里生成
        TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::BuildPointPath FAILED! path sized {} returned\n", pointCount);
        BuildShortcut();
        _type = PathType(_type | PATHFIND_NOPATH);
        return;
    }
    else if (pointCount >= _pointPathLimit)
    {
        // 路径点数量达到限制
        TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::BuildPointPath FAILED! path sized {} returned, lower than limit set to {}", pointCount, _pointPathLimit);
        BuildShortcut();
        _type = PathType(_type | PATHFIND_SHORT);
        return;
    }

    // 将 Detour 坐标系(Y,Z,X)转换为游戏坐标系(X,Y,Z)
    _pathPoints.resize(pointCount);
    for (uint32 i = 0; i < pointCount; ++i)
        _pathPoints[i] = G3D::Vector3(pathPoints[i*VERTEX_SIZE+2], pathPoints[i*VERTEX_SIZE], pathPoints[i*VERTEX_SIZE+1]);

    // 规范化路径(修正Z坐标)
    NormalizePath();

    // 第一个点总是当前位置,我们需要的是最后一个点作为实际终点
    SetActualEndPosition(_pathPoints[pointCount-1]);

    // 如果需要强制到达目标点
    if (_forceDestination &&
        (!(_type & PATHFIND_NORMAL) || !InRange(GetEndPosition(), GetActualEndPosition(), 1.0f, 1.0f)))
    {
        // 可能需要保留部分子路径
        if (Dist3DSqr(GetActualEndPosition(), GetEndPosition()) < 0.3f * Dist3DSqr(GetStartPosition(), GetEndPosition()))
        {
            // 实际终点接近目标终点,直接修改最后一个点
            SetActualEndPosition(GetEndPosition());
            _pathPoints[_pathPoints.size()-1] = GetEndPosition();
        }
        else
        {
            // 实际终点距离目标终点较远,构建捷径
            SetActualEndPosition(GetEndPosition());
            BuildShortcut();
        }

        _type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
    }

    TC_LOG_DEBUG("maps.mmaps", "++ PathGenerator::BuildPointPath path type {} size {} poly-size {}", _type, pointCount, _polyLength);
}

/**
 * @brief 规范化路径
 *
 * 对路径中每个点的Z坐标进行修正,确保路径在地形上可行。
 * 调用单位的 UpdateAllowedPositionZ 方法调整高度。
 */
void PathGenerator::NormalizePath()
{
    for (uint32 i = 0; i < _pathPoints.size(); ++i)
        _source->UpdateAllowedPositionZ(_pathPoints[i].x, _pathPoints[i].y, _pathPoints[i].z);
}

/**
 * @brief 构建直线路径(捷径)
 *
 * 创建从起点到终点的直线路径,不进行寻路。
 * 适用于短距离移动、飞行、游泳或无法使用导航网格的情况。
 * 注意:不进行碰撞检测,可能穿越障碍物
 */
void PathGenerator::BuildShortcut()
{
    TC_LOG_DEBUG("maps.mmaps", "++ BuildShortcut :: making shortcut");

    Clear();

    // 创建两点路径:起点和终点
    _pathPoints.resize(2);

    // 设置起点和终点
    _pathPoints[0] = GetStartPosition();
    _pathPoints[1] = GetActualEndPosition();

    // 规范化路径
    NormalizePath();

    _type = PATHFIND_SHORTCUT;
}

/**
 * @brief 创建查询过滤器
 *
 * 根据单位类型和移动能力创建导航网格查询过滤器。
 * 定义单位可以通行的区域类型(地面、水面、岩浆等)。
 */
void PathGenerator::CreateFilter()
{
    uint16 includeFlags = 0;
    uint16 excludeFlags = 0;

    if (_source->GetTypeId() == TYPEID_UNIT)
    {
        Creature* creature = (Creature*)_source;
        // 生物可以行走
        if (creature->CanWalk())
            includeFlags |= NAV_GROUND;          // 地面

        // 生物不会受到环境伤害,可以进入各种液体
        if (creature->CanEnterWater())
            includeFlags |= (NAV_WATER | NAV_MAGMA_SLIME);                 // 游泳
    }
    else // 假设是玩家
    {
        // 玩家支持不可能完美,保持安全设置
        includeFlags |= (NAV_GROUND | NAV_WATER | NAV_MAGMA_SLIME);
    }

    _filter.setIncludeFlags(includeFlags);
    _filter.setExcludeFlags(excludeFlags);

    // 更新过滤器
    UpdateFilter();
}

/**
 * @brief 更新查询过滤器
 *
 * 根据单位当前状态动态调整可通行区域。
 * 允许生物在被强制移动到无法正常移动的地形时使用不同的移动类型。
 */
void PathGenerator::UpdateFilter()
{
    // 允许生物作弊:如果被强制移动到无法正常移动的地形,使用不同的移动类型
    if (Unit const* _sourceUnit = _source->ToUnit())
    {
        // 如果单位在水中或水下
        if (_sourceUnit->IsInWater() || _sourceUnit->IsUnderWater())
        {
            uint16 includedFlags = _filter.getIncludeFlags();
            // 添加当前位置的地形类型
            includedFlags |= GetNavTerrain(_source->GetPositionX(),
                                           _source->GetPositionY(),
                                           _source->GetPositionZ());

            _filter.setIncludeFlags(includedFlags);
        }

        // 生物在战斗或逃避模式下,可以通行陡峭地形
        if (Creature const* _sourceCreature = _source->ToCreature())
            if (_sourceCreature->IsInCombat() || _sourceCreature->IsInEvadeMode())
                _filter.setIncludeFlags(_filter.getIncludeFlags() | NAV_GROUND_STEEP);
    }
}

NavTerrainFlag PathGenerator::GetNavTerrain(float x, float y, float z)
{
    LiquidData data;
    ZLiquidStatus liquidStatus = _source->GetMap()->GetLiquidStatus(_source->GetPhaseMask(), x, y, z, MAP_ALL_LIQUIDS, &data, _source->GetCollisionHeight());
    if (liquidStatus == LIQUID_MAP_NO_WATER)
        return NAV_GROUND;

    switch (data.type_flags)
    {
        case MAP_LIQUID_TYPE_WATER:
        case MAP_LIQUID_TYPE_OCEAN:
            return NAV_WATER;
        case MAP_LIQUID_TYPE_MAGMA:
        case MAP_LIQUID_TYPE_SLIME:
            return NAV_MAGMA_SLIME;
        default:
            return NAV_GROUND;
    }
}

bool PathGenerator::HaveTile(const G3D::Vector3& p) const
{
    int tx = -1, ty = -1;
    float point[VERTEX_SIZE] = {p.y, p.z, p.x};

    _navMesh->calcTileLoc(point, &tx, &ty);

    /// Workaround
    /// For some reason, often the tx and ty variables wont get a valid value
    /// Use this check to prevent getting negative tile coords and crashing on getTileAt
    if (tx < 0 || ty < 0)
        return false;

    return (_navMesh->getTileAt(tx, ty, 0) != nullptr);
}

uint32 PathGenerator::FixupCorridor(dtPolyRef* path, uint32 npath, uint32 maxPath, dtPolyRef const* visited, uint32 nvisited)
{
    int32 furthestPath = -1;
    int32 furthestVisited = -1;

    // Find furthest common polygon.
    for (int32 i = npath-1; i >= 0; --i)
    {
        bool found = false;
        for (int32 j = nvisited-1; j >= 0; --j)
        {
            if (path[i] == visited[j])
            {
                furthestPath = i;
                furthestVisited = j;
                found = true;
            }
        }
        if (found)
            break;
    }

    // If no intersection found just return current path.
    if (furthestPath == -1 || furthestVisited == -1)
        return npath;

    // Concatenate paths.

    // Adjust beginning of the buffer to include the visited.
    uint32 req = nvisited - furthestVisited;
    uint32 orig = uint32(furthestPath + 1) < npath ? furthestPath + 1 : npath;
    uint32 size = npath > orig ? npath - orig : 0;
    if (req + size > maxPath)
        size = maxPath-req;

    if (size)
        memmove(path + req, path + orig, size * sizeof(dtPolyRef));

    // Store visited
    for (uint32 i = 0; i < req; ++i)
        path[i] = visited[(nvisited - 1) - i];

    return req+size;
}

bool PathGenerator::GetSteerTarget(float const* startPos, float const* endPos,
                              float minTargetDist, dtPolyRef const* path, uint32 pathSize,
                              float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef)
{
    // Find steer target.
    static const uint32 MAX_STEER_POINTS = 3;
    float steerPath[MAX_STEER_POINTS*VERTEX_SIZE];
    unsigned char steerPathFlags[MAX_STEER_POINTS];
    dtPolyRef steerPathPolys[MAX_STEER_POINTS];
    uint32 nsteerPath = 0;
    dtStatus dtResult = _navMeshQuery->findStraightPath(startPos, endPos, path, pathSize,
                                                steerPath, steerPathFlags, steerPathPolys, (int*)&nsteerPath, MAX_STEER_POINTS);
    if (!nsteerPath || dtStatusFailed(dtResult))
        return false;

    // Find vertex far enough to steer to.
    uint32 ns = 0;
    while (ns < nsteerPath)
    {
        // Stop at Off-Mesh link or when point is further than slop away.
        if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
            !InRangeYZX(&steerPath[ns*VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
            break;
        ns++;
    }
    // Failed to find good point to steer to.
    if (ns >= nsteerPath)
        return false;

    dtVcopy(steerPos, &steerPath[ns*VERTEX_SIZE]);
    steerPos[1] = startPos[1];  // keep Z value
    steerPosFlag = steerPathFlags[ns];
    steerPosRef = steerPathPolys[ns];

    return true;
}

dtStatus PathGenerator::FindSmoothPath(float const* startPos, float const* endPos,
                                     dtPolyRef const* polyPath, uint32 polyPathSize,
                                     float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
{
    *smoothPathSize = 0;
    uint32 nsmoothPath = 0;

    dtPolyRef polys[MAX_PATH_LENGTH];
    memcpy(polys, polyPath, sizeof(dtPolyRef)*polyPathSize);
    uint32 npolys = polyPathSize;

    float iterPos[VERTEX_SIZE], targetPos[VERTEX_SIZE];

    if (polyPathSize > 1)
    {
        // Pick the closest points on poly border
        if (dtStatusFailed(_navMeshQuery->closestPointOnPolyBoundary(polys[0], startPos, iterPos)))
            return DT_FAILURE;

        if (dtStatusFailed(_navMeshQuery->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos)))
            return DT_FAILURE;
    }
    else
    {
        // Case where the path is on the same poly
        dtVcopy(iterPos, startPos);
        dtVcopy(targetPos, endPos);
    }

    dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
    nsmoothPath++;

    // Move towards target a small advancement at a time until target reached or
    // when ran out of memory to store the path.
    while (npolys && nsmoothPath < maxSmoothPathSize)
    {
        // Find location to steer towards.
        float steerPos[VERTEX_SIZE];
        unsigned char steerPosFlag;
        dtPolyRef steerPosRef = INVALID_POLYREF;

        if (!GetSteerTarget(iterPos, targetPos, SMOOTH_PATH_SLOP, polys, npolys, steerPos, steerPosFlag, steerPosRef))
            break;

        bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) != 0;
        bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;

        // Find movement delta.
        float delta[VERTEX_SIZE];
        dtVsub(delta, steerPos, iterPos);
        float len = dtMathSqrtf(dtVdot(delta, delta));
        // If the steer target is end of path or off-mesh link, do not move past the location.
        if ((endOfPath || offMeshConnection) && len < SMOOTH_PATH_STEP_SIZE)
            len = 1.0f;
        else
            len = SMOOTH_PATH_STEP_SIZE / len;

        float moveTgt[VERTEX_SIZE];
        dtVmad(moveTgt, iterPos, delta, len);

        // Move
        float result[VERTEX_SIZE];
        const static uint32 MAX_VISIT_POLY = 16;
        dtPolyRef visited[MAX_VISIT_POLY];

        uint32 nvisited = 0;
        if (dtStatusFailed(_navMeshQuery->moveAlongSurface(polys[0], iterPos, moveTgt, &_filter, result, visited, (int*)&nvisited, MAX_VISIT_POLY)))
            return DT_FAILURE;
        npolys = FixupCorridor(polys, npolys, MAX_PATH_LENGTH, visited, nvisited);

        if (dtStatusFailed(_navMeshQuery->getPolyHeight(polys[0], result, &result[1])))
            TC_LOG_DEBUG("maps.mmaps", "Cannot find height at position X: {} Y: {} Z: {} for {}", result[2], result[0], result[1], _source->GetDebugInfo());
        result[1] += 0.5f;
        dtVcopy(iterPos, result);

        // Handle end of path and off-mesh links when close enough.
        if (endOfPath && InRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Reached end of path.
            dtVcopy(iterPos, targetPos);
            if (nsmoothPath < maxSmoothPathSize)
            {
                dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
                nsmoothPath++;
            }
            break;
        }
        else if (offMeshConnection && InRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Advance the path up to and over the off-mesh connection.
            dtPolyRef prevRef = INVALID_POLYREF;
            dtPolyRef polyRef = polys[0];
            uint32 npos = 0;
            while (npos < npolys && polyRef != steerPosRef)
            {
                prevRef = polyRef;
                polyRef = polys[npos];
                npos++;
            }

            for (uint32 i = npos; i < npolys; ++i)
                polys[i-npos] = polys[i];

            npolys -= npos;

            // Handle the connection.
            float connectionStartPos[VERTEX_SIZE], connectionEndPos[VERTEX_SIZE];
            if (dtStatusSucceed(_navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, connectionStartPos, connectionEndPos)))
            {
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], connectionStartPos);
                    nsmoothPath++;
                }
                // Move position at the other side of the off-mesh link.
                dtVcopy(iterPos, connectionEndPos);
                if (dtStatusFailed(_navMeshQuery->getPolyHeight(polys[0], iterPos, &iterPos[1])))
                    return DT_FAILURE;
                iterPos[1] += 0.5f;
            }
        }

        // Store results.
        if (nsmoothPath < maxSmoothPathSize)
        {
            dtVcopy(&smoothPath[nsmoothPath*VERTEX_SIZE], iterPos);
            nsmoothPath++;
        }
    }

    *smoothPathSize = nsmoothPath;

    // this is most likely a loop
    return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;
}

bool PathGenerator::InRangeYZX(float const* v1, float const* v2, float r, float h) const
{
    const float dx = v2[0] - v1[0];
    const float dy = v2[1] - v1[1]; // elevation
    const float dz = v2[2] - v1[2];
    return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
}

bool PathGenerator::InRange(G3D::Vector3 const& p1, G3D::Vector3 const& p2, float r, float h) const
{
    G3D::Vector3 d = p1 - p2;
    return (d.x * d.x + d.y * d.y) < r * r && fabsf(d.z) < h;
}

float PathGenerator::Dist3DSqr(G3D::Vector3 const& p1, G3D::Vector3 const& p2) const
{
    return (p1 - p2).squaredLength();
}

void PathGenerator::ShortenPathUntilDist(G3D::Vector3 const& target, float dist)
{
    if (GetPathType() == PATHFIND_BLANK || _pathPoints.size() < 2)
    {
        TC_LOG_ERROR("maps.mmaps", "PathGenerator::ReducePathLengthByDist called before path was successfully built");
        return;
    }

    float const distSq = dist * dist;

    // the first point of the path must be outside the specified range
    // (this should have really been checked by the caller...)
    if ((_pathPoints[0] - target).squaredLength() < distSq)
        return;

    // check if we even need to do anything
    if ((*_pathPoints.rbegin() - target).squaredLength() >= distSq)
        return;

    size_t i = _pathPoints.size()-1;
    float x, y, z, collisionHeight = _source->GetCollisionHeight();
    // find the first i s.t.:
    //  - _pathPoints[i] is still too close
    //  - _pathPoints[i-1] is too far away
    // => the end point is somewhere on the line between the two
    while (true)
    {
        // we know that pathPoints[i] is too close already (from the previous iteration)
        if ((_pathPoints[i-1] - target).squaredLength() >= distSq)
            break; // bingo!

        // check if the shortened path is still in LoS with the target
        _source->GetHitSpherePointFor({ _pathPoints[i - 1].x, _pathPoints[i - 1].y, _pathPoints[i - 1].z + collisionHeight }, x, y, z);
        if (!_source->GetMap()->isInLineOfSight(x, y, z, _pathPoints[i - 1].x, _pathPoints[i - 1].y, _pathPoints[i - 1].z + collisionHeight, _source->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
        {
            // whenver we find a point that is not in LoS anymore, simply use last valid path
            _pathPoints.resize(i + 1);
            return;
        }

        if (!--i)
        {
            // no point found that fulfills the condition
            _pathPoints[0] = _pathPoints[1];
            _pathPoints.resize(2);
            return;
        }
    }

    // ok, _pathPoints[i] is too close, _pathPoints[i-1] is not, so our target point is somewhere between the two...
    //   ... settle for a guesstimate since i'm not confident in doing trig on every chase motion tick...
    // (@todo review this)
    _pathPoints[i] += (_pathPoints[i - 1] - _pathPoints[i]).direction() * (dist - (_pathPoints[i] - target).length());
    _pathPoints.resize(i+1);
}

bool PathGenerator::IsInvalidDestinationZ(Unit const* target) const
{
    return (target->GetPositionZ() - GetActualEndPosition().z) > 5.0f;
}

void PathGenerator::AddFarFromPolyFlags(bool startFarFromPoly, bool endFarFromPoly)
{
    if (startFarFromPoly)
        _type = PathType(_type | PATHFIND_FARFROMPOLY_START);
    if (endFarFromPoly)
        _type = PathType(_type | PATHFIND_FARFROMPOLY_END);
}
