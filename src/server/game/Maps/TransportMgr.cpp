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
 * @file TransportMgr.cpp
 * @brief 运输工具管理器（TransportMgr）模块实现文件
 *
 * 本文件实现了 TransportMgr 类的所有方法，包括：
 *   - 运输工具模板的加载和缓存
 *   - 路径生成算法（样条曲线、运动学计算）
 *   - 运输工具实例的创建和管理
 *   - 动画数据的加载
 *
 * 关键算法说明：
 *
 * 1. 路径生成算法（GeneratePath）：
 *    - 从 TaxiPathNode 数据构建路径
 *    - 使用 Catmull-Rom 样条曲线平滑路径
 *    - 计算加速/减速运动参数
 *    - 处理停靠点和传送点
 *
 * 2. 运动学模型：
 *    - 从停靠点出发：匀加速 -> 匀速 -> 匀减速
 *    - 加速度和最大速度由游戏对象模板定义
 *    - 时间计算考虑加速距离和减速距离
 *
 * @see TransportMgr.h 头文件定义
 * @see Transport 运输工具类
 */

#include "TransportMgr.h"
#include "DatabaseEnv.h"
#include "InstanceScript.h"
#include "Log.h"
#include "MapManager.h"
#include "MoveSplineInitArgs.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Spline.h"
#include "Transport.h"

/**
 * @brief TransportTemplate 析构函数
 *
 * 清理模板数据。关键帧和样条曲线会自动释放。
 */
TransportTemplate::~TransportTemplate()
{
}

/**
 * @brief TransportMgr 构造函数
 *
 * 初始化空的运输工具管理器。
 * 实际数据加载在 LoadTransportTemplates() 中完成。
 */
TransportMgr::TransportMgr() { }

/**
 * @brief TransportMgr 析构函数
 *
 * 清理所有运输工具模板和动画数据。
 */
TransportMgr::~TransportMgr() { }

/**
 * @brief 获取单例实例
 *
 * @return TransportMgr 单例指针
 *
 * 使用 C++11 的静态局部变量实现线程安全的单例模式。
 * 首次调用时创建实例，后续调用直接返回已创建的实例。
 */
TransportMgr* TransportMgr::instance()
{
    static TransportMgr instance;
    return &instance;
}

/**
 * @brief 卸载所有数据
 *
 * 清空运输工具模板容器和动画容器。
 * 通常在服务器关闭时调用。
 */
void TransportMgr::Unload()
{
    _transportTemplates.clear();
}

/**
 * @brief 加载运输工具模板
 *
 * 从数据库加载所有运输工具模板并生成路径数据。
 *
 * 加载流程：
 *   1. 查询所有 type=15（MO_TRANSPORT）的游戏对象
 *   2. 验证每个运输工具有效性（模板存在、路径有效）
 *   3. 生成完整路径并存储到模板容器
 *   4. 分类存储副本运输工具ID
 *
 * 性能考虑：
 *   - 路径预计算避免了运行时的重复计算
 *   - 所有运输工具在启动时一次性加载
 *
 * 调用时机：服务器启动时
 */
void TransportMgr::LoadTransportTemplates()
{
    uint32 oldMSTime = getMSTime();

    // 查询所有运输工具类型的游戏对象（type=15 表示 MO_TRANSPORT）
    QueryResult result = WorldDatabase.Query("SELECT entry FROM gameobject_template WHERE type = 15 ORDER BY entry ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 transport templates. DB table `gameobject_template` has no transports!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();

        // 获取游戏对象模板
        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(entry);
        if (goInfo == nullptr)
        {
            TC_LOG_ERROR("sql.sql", "Transport {} has no associated GameObjectTemplate from `gameobject_template` , skipped.", entry);
            continue;
        }

        // 验证路径ID有效性
        if (goInfo->moTransport.taxiPathId >= sTaxiPathNodesByPath.size())
        {
            TC_LOG_ERROR("sql.sql", "Transport {} (name: {}) has an invalid path specified in `gameobject_template`.`data0` ({}) field, skipped.", entry, goInfo->name, goInfo->moTransport.taxiPathId);
            continue;
        }

        // 为每个模板生成路径数据
        // 路径按模板生成，避免副本运输工具重复计算
        TransportTemplate& transport = _transportTemplates[entry];
        transport.entry = entry;
        GeneratePath(goInfo, &transport);

        // 副本运输工具只在一个地图上，记录到副本映射表
        if (transport.inInstance)
            _instanceTransports[*transport.mapsUsed.begin()].insert(entry);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} transport templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 加载运输工具动画和旋转数据
 *
 * 从 DBC 存储加载运输工具的动画数据：
 *   - TransportAnimationStore：位置关键帧
 *   - TransportRotationStore：旋转关键帧
 *
 * 这些数据用于动态运输工具（如电梯）的精确动画播放，
 * 代替静态路径计算。
 *
 * 调用时机：服务器启动时，在 LoadTransportTemplates 之后
 */
void TransportMgr::LoadTransportAnimationAndRotation()
{
    // 加载动画位置数据
    for (uint32 i = 0; i < sTransportAnimationStore.GetNumRows(); ++i)
        if (TransportAnimationEntry const* anim = sTransportAnimationStore.LookupEntry(i))
            AddPathNodeToTransport(anim->TransportID, anim->TimeIndex, anim);

    // 加载动画旋转数据
    for (uint32 i = 0; i < sTransportRotationStore.GetNumRows(); ++i)
        if (TransportRotationEntry const* rot = sTransportRotationStore.LookupEntry(i))
            AddPathRotationToTransport(rot->GameObjectsID, rot->TimeIndex, rot);
}

/**
 * @brief 样条原始初始化器
 *
 * 用于初始化样条曲线的辅助类，设置样条的参数和点集。
 * 主要用于计算关键帧的初始朝向。
 */
class SplineRawInitializer
{
public:
    /**
     * @brief 构造函数
     *
     * @param points 路径点数组
     */
    SplineRawInitializer(Movement::PointsArray& points) : _points(points) { }

    /**
     * @brief 函数调用操作符
     *
     * @param mode [out] 样条模式（设为 Catmull-Rom）
     * @param cyclic [out] 是否循环（设为非循环）
     * @param points [out] 输出点集
     * @param lo [out] 低索引边界
     * @param hi [out] 高索引边界
     *
     * 配置样条曲线使用 Catmull-Rom 插值模式，
     * 这是一种经过所有控制点的插值曲线。
     */
    void operator()(uint8& mode, bool& cyclic, Movement::PointsArray& points, int& lo, int& hi) const
    {
        mode = Movement::SplineBase::ModeCatmullrom;
        cyclic = false;
        points.assign(_points.begin(), _points.end());
        lo = 1;
        hi = points.size() - 2;
    }

    Movement::PointsArray& _points;
};

/**
 * @brief 生成运输工具路径
 *
 * @param goInfo 游戏对象模板数据
 * @param transport 输出的运输工具模板
 *
 * 核心算法：为运输工具生成完整的路径数据
 *
 * 处理流程：
 *   1. 加载出租车路径节点
 *   2. 添加额外的控制点（用于导数计算）
 *   3. 创建朝向计算的样条曲线
 *   4. 处理地图变更节点（传送帧）
 *   5. 移除无效的首尾帧
 *   6. 创建各路径段的样条曲线
 *   7. 计算距离数据
 *   8. 计算时间数据（运动学）
 *
 * 运动学模型：
 *   - 加速阶段：s = 0.5 * a * t^2
 *   - 匀速阶段：s = v * t
 *   - 减速阶段：s = 0.5 * a * t^2
 *
 * 性能优化：所有计算在此预完成，避免运行时计算
 */
void TransportMgr::GeneratePath(GameObjectTemplate const* goInfo, TransportTemplate* transport)
{
    // ========== 第一阶段：加载路径节点 ==========

    // 获取出租车路径ID和节点列表
    uint32 pathId = goInfo->moTransport.taxiPathId;
    TaxiPathNodeList const& path = sTaxiPathNodesByPath[pathId];
    std::vector<KeyFrame>& keyFrames = transport->keyFrames;
    Movement::PointsArray splinePath, allPoints;
    bool mapChange = false;

    // 收集所有路径节点位置
    for (size_t i = 0; i < path.size(); ++i)
        allPoints.push_back(G3D::Vector3(path[i]->Loc.X, path[i]->Loc.Y, path[i]->Loc.Z));

    // ========== 第二阶段：添加额外控制点 ==========

    // 添加额外的控制点以支持所有节点的导数计算
    // Catmull-Rom 样条需要额外的端点来计算边界切线
    allPoints.insert(allPoints.begin(), allPoints.front().lerp(allPoints[1], -0.2f));
    allPoints.push_back(allPoints.back().lerp(allPoints[allPoints.size() - 2], -0.2f));
    allPoints.push_back(allPoints.back().lerp(allPoints[allPoints.size() - 2], -1.0f));

    // 创建用于朝向计算的样条曲线
    SplineRawInitializer initer(allPoints);
    TransportSpline orientationSpline;
    orientationSpline.init_spline_custom(initer);
    orientationSpline.initLengths();

    // ========== 第三阶段：处理每个路径节点 ==========

    for (size_t i = 0; i < path.size(); ++i)
    {
        if (!mapChange)
        {
            TaxiPathNodeEntry const* node_i = path[i];

            // 检查是否需要地图切换（传送帧）
            // 标志位1表示传送点，或者相邻节点在不同地图
            if (i != path.size() - 1 && (node_i->Flags & 1 || node_i->ContinentID != path[i + 1]->ContinentID))
            {
                // 将上一帧标记为传送帧
                keyFrames.back().Teleport = true;
                mapChange = true;
            }
            else
            {
                // 创建普通关键帧
                KeyFrame k(node_i);

                // 计算初始朝向（使用样条曲线导数）
                G3D::Vector3 h;
                orientationSpline.evaluate_derivative(i + 1, 0.0f, h);
                k.InitialOrientation = Position::NormalizeOrientation(std::atan2(h.y, h.x) + float(M_PI));

                keyFrames.push_back(k);
                splinePath.push_back(G3D::Vector3(node_i->Loc.X, node_i->Loc.Y, node_i->Loc.Z));
                transport->mapsUsed.insert(k.Node->ContinentID);
            }
        }
        else
        {
            // 跳过地图变更后的第一个节点（已在上一帧处理）
            mapChange = false;
        }
    }

    // ========== 第四阶段：优化首尾帧 ==========

    // 移除特殊的首尾帧（如果没有停靠或事件，可以优化掉）
    if (splinePath.size() >= 2)
    {
        // 检查首帧是否可以移除
        if (!keyFrames.front().IsStopFrame() && !keyFrames.front().Node->ArrivalEventID && !keyFrames.front().Node->DepartureEventID)
        {
            splinePath.erase(splinePath.begin());
            keyFrames.erase(keyFrames.begin());
        }
        // 检查尾帧是否可以移除
        if (!keyFrames.back().IsStopFrame() && !keyFrames.back().Node->ArrivalEventID && !keyFrames.back().Node->DepartureEventID)
        {
            splinePath.pop_back();
            keyFrames.pop_back();
        }
    }

    // 确保至少有一个关键帧
    ASSERT(!keyFrames.empty());

    // ========== 第五阶段：判断是否为副本运输工具 ==========

    if (transport->mapsUsed.size() > 1)
    {
        // 跨地图的运输工具不能在任何副本中
        for (std::set<uint32>::const_iterator itr = transport->mapsUsed.begin(); itr != transport->mapsUsed.end(); ++itr)
            ASSERT(!sMapStore.LookupEntry(*itr)->Instanceable());

        transport->inInstance = false;
    }
    else
    {
        // 单地图运输工具：检查地图是否为副本
        transport->inInstance = sMapStore.LookupEntry(*transport->mapsUsed.begin())->Instanceable();
    }

    // 最后一帧总是传送帧（回到起点）
    keyFrames.back().Teleport = true;

    // ========== 第六阶段：计算运动学参数 ==========

    const float speed = float(goInfo->moTransport.moveSpeed);       // 最大速度
    const float accel = float(goInfo->moTransport.accelRate);        // 加速度
    const float accel_dist = 0.5f * speed * speed / accel;           // 加速到最大速度的距离

    transport->accelTime = speed / accel;    // 加速时间 = v / a
    transport->accelDist = accel_dist;       // 加速距离

    // ========== 第七阶段：计算距离和样条 ==========

    int32 firstStop = -1;   // 第一个停靠点的索引
    int32 lastStop = -1;    // 最后一个停靠点的索引

    // 第一帧通过传送到达
    keyFrames[0].DistFromPrev = 0;
    keyFrames[0].Index = 1;

    // 记录第一个停靠点
    if (keyFrames[0].IsStopFrame())
    {
        firstStop = 0;
        lastStop = 0;
    }

    // 为每个路径段创建样条曲线并计算距离
    size_t start = 0;
    for (size_t i = 1; i < keyFrames.size(); ++i)
    {
        // 遇到传送帧或最后一帧时，创建一个路径段
        if (keyFrames[i - 1].Teleport || i + 1 == keyFrames.size())
        {
            size_t extra = !keyFrames[i - 1].Teleport ? 1 : 0;

            // 创建该路径段的样条曲线
            std::shared_ptr<TransportSpline> spline = std::make_shared<TransportSpline>();
            spline->init_spline(&splinePath[start], i - start + extra, Movement::SplineBase::ModeCatmullrom);
            spline->initLengths();

            // 计算该段内每个关键帧的距离
            for (size_t j = start; j < i + extra; ++j)
            {
                keyFrames[j].Index = j - start + 1;
                keyFrames[j].DistFromPrev = float(spline->length(j - start, j + 1 - start));
                if (j > 0)
                    keyFrames[j - 1].NextDistFromPrev = keyFrames[j].DistFromPrev;
                keyFrames[j].Spline = spline;
            }

            // 处理传送帧
            if (keyFrames[i - 1].Teleport)
            {
                keyFrames[i].Index = i - start + 1;
                keyFrames[i].DistFromPrev = 0.0f;
                keyFrames[i - 1].NextDistFromPrev = 0.0f;
                keyFrames[i].Spline = spline;
            }

            start = i;
        }

        // 记录停靠点索引
        if (keyFrames[i].IsStopFrame())
        {
            if (firstStop == -1)
                firstStop = i;
            lastStop = i;
        }
    }

    // 最后一帧到第一帧的距离
    keyFrames.back().NextDistFromPrev = keyFrames.front().DistFromPrev;

    // 如果没有停靠点，使用起点作为停靠点
    if (firstStop == -1 || lastStop == -1)
        firstStop = lastStop = 0;

    // ========== 第八阶段：计算距离停靠点的距离 ==========

    // 从上一个停靠点出发后的累计距离
    float tmpDist = 0.0f;
    for (size_t i = 0; i < keyFrames.size(); ++i)
    {
        int32 j = (i + lastStop) % keyFrames.size();
        if (keyFrames[j].IsStopFrame() || j == lastStop)
            tmpDist = 0.0f;
        else
            tmpDist += keyFrames[j].DistFromPrev;
        keyFrames[j].DistSinceStop = tmpDist;
    }

    // 到下一个停靠点的累计距离
    tmpDist = 0.0f;
    for (int32 i = int32(keyFrames.size()) - 1; i >= 0; i--)
    {
        int32 j = (i + firstStop) % keyFrames.size();
        tmpDist += keyFrames[(j + 1) % keyFrames.size()].DistFromPrev;
        keyFrames[j].DistUntilStop = tmpDist;
        if (keyFrames[j].IsStopFrame() || j == firstStop)
            tmpDist = 0.0f;
    }

    // ========== 第九阶段：计算时间数据（运动学） ==========

    for (size_t i = 0; i < keyFrames.size(); ++i)
    {
        float total_dist = keyFrames[i].DistSinceStop + keyFrames[i].DistUntilStop;

        if (total_dist < 2 * accel_dist)
        {
            // 总距离太短，无法达到最大速度
            // 只有加速和减速两个阶段
            if (keyFrames[i].DistSinceStop < keyFrames[i].DistUntilStop)
            {
                // 仍在加速阶段
                // 计算加速+减速总时间，减去已加速时间
                float segment_time = 2.0f * std::sqrt((keyFrames[i].DistUntilStop + keyFrames[i].DistSinceStop) / accel);
                keyFrames[i].TimeTo = segment_time - std::sqrt(2 * keyFrames[i].DistSinceStop / accel);
            }
            else
            {
                // 已经在减速阶段
                keyFrames[i].TimeTo = std::sqrt(2 * keyFrames[i].DistUntilStop / accel);
            }
        }
        else if (keyFrames[i].DistSinceStop < accel_dist)
        {
            // 正在加速，但能达到最大速度
            // 计算加速+匀速+减速总时间，减去已加速时间
            float segment_time = (keyFrames[i].DistUntilStop + keyFrames[i].DistSinceStop) / speed + (speed / accel);
            keyFrames[i].TimeTo = segment_time - std::sqrt(2 * keyFrames[i].DistSinceStop / accel);
        }
        else if (keyFrames[i].DistUntilStop < accel_dist)
        {
            // 正在减速（曾达到最大速度）
            keyFrames[i].TimeTo = std::sqrt(2 * keyFrames[i].DistUntilStop / accel);
        }
        else
        {
            // 已达到最大速度，正在匀速运动
            keyFrames[i].TimeTo = (keyFrames[i].DistUntilStop / speed) + (0.5f * speed / accel);
        }
    }

    // 从 TimeTo 计算 TimeFrom
    float segmentTime = 0.0f;
    for (size_t i = 0; i < keyFrames.size(); ++i)
    {
        int32 j = (i + lastStop) % keyFrames.size();
        if (keyFrames[j].IsStopFrame() || j == lastStop)
            segmentTime = keyFrames[j].TimeTo;
        keyFrames[j].TimeFrom = segmentTime - keyFrames[j].TimeTo;
    }

    // ========== 第十阶段：计算路径时间 ==========

    keyFrames[0].ArriveTime = 0;
    float curPathTime = 0.0f;

    // 处理第一帧的停靠延迟
    if (keyFrames[0].IsStopFrame())
    {
        curPathTime = float(keyFrames[0].Node->Delay);
        keyFrames[0].DepartureTime = uint32(curPathTime * float(IN_MILLISECONDS));
    }

    // 计算每个关键帧的到达和离开时间
    for (size_t i = 1; i < keyFrames.size(); ++i)
    {
        curPathTime += keyFrames[i - 1].TimeTo;

        if (keyFrames[i].IsStopFrame())
        {
            // 停靠帧：记录到达时间，加上延迟得到离开时间
            keyFrames[i].ArriveTime = uint32(curPathTime * float(IN_MILLISECONDS));
            keyFrames[i - 1].NextArriveTime = keyFrames[i].ArriveTime;
            curPathTime += float(keyFrames[i].Node->Delay);
            keyFrames[i].DepartureTime = uint32(curPathTime * float(IN_MILLISECONDS));
        }
        else
        {
            // 普通帧：到达时间=离开时间
            curPathTime -= keyFrames[i].TimeTo;
            keyFrames[i].ArriveTime = uint32(curPathTime * float(IN_MILLISECONDS));
            keyFrames[i - 1].NextArriveTime = keyFrames[i].ArriveTime;
            keyFrames[i].DepartureTime = keyFrames[i].ArriveTime;
        }
    }

    // 最后一帧的下一到达时间
    keyFrames.back().NextArriveTime = keyFrames.back().DepartureTime;

    // 完整路径时间
    transport->pathTime = keyFrames.back().DepartureTime;
}

/**
 * @brief 添加路径节点到运输工具动画
 *
 * @param transportEntry 运输工具模板ID
 * @param timeSeg 时间段索引
 * @param node 动画节点数据
 *
 * 将 DBC 中的动画节点添加到运输工具动画数据中。
 * 同时更新动画的总时长。
 */
void TransportMgr::AddPathNodeToTransport(uint32 transportEntry, uint32 timeSeg, TransportAnimationEntry const* node)
{
    TransportAnimation& animNode = _transportAnimations[transportEntry];
    // 更新总时间（取最大值）
    if (animNode.TotalTime < timeSeg)
        animNode.TotalTime = timeSeg;

    // 存储动画节点
    animNode.Path[timeSeg] = node;
}

/**
 * @brief 创建运输工具实例
 *
 * @param entry 运输工具模板ID
 * @param guid 对象GUID低值（可选，0表示自动生成）
 * @param map 目标地图指针（可选，nullptr表示从模板获取）
 * @return 创建的运输工具指针，失败返回 nullptr
 *
 * 创建流程：
 *   1. 副本情况：执行脚本钩子获取实际模板ID
 *   2. 获取运输工具模板
 *   3. 创建 Transport 对象
 *   4. 初始化位置（第一个关键帧位置）
 *   5. 创建游戏对象基类
 *   6. 验证地图类型匹配
 *   7. 设置地图和区域脚本
 *   8. 注册到全局持有器并添加到地图
 *
 * 调用时机：
 *   - SpawnContinentTransports() 中创建大地图运输工具
 *   - CreateInstanceTransports() 中创建副本运输工具
 */
Transport* TransportMgr::CreateTransport(uint32 entry, ObjectGuid::LowType guid /*= 0*/, Map* map /*= nullptr*/)
{
    // 副本情况：执行 GetGameObjectEntry 钩子
    if (map)
    {
        // SetZoneScript() 在添加到地图后调用，所以使用地图获取脚本
        if (map->IsDungeon())
            if (InstanceScript* instance = static_cast<InstanceMap*>(map)->GetInstanceScript())
                entry = instance->GetGameObjectEntry(0, entry);

        // 脚本可能返回0表示不创建
        if (!entry)
            return nullptr;
    }

    // 获取运输工具模板
    TransportTemplate const* tInfo = GetTransportTemplate(entry);
    if (!tInfo)
    {
        TC_LOG_ERROR("sql.sql", "Transport {} will not be loaded, `transport_template` missing", entry);
        return nullptr;
    }

    // 创建运输工具对象
    Transport* trans = new Transport();

    // 在第一个关键帧位置创建
    TaxiPathNodeEntry const* startNode = tInfo->keyFrames.begin()->Node;
    uint32 mapId = startNode->ContinentID;
    float x = startNode->Loc.X;
    float y = startNode->Loc.Y;
    float z = startNode->Loc.Z;
    float o = tInfo->keyFrames.begin()->InitialOrientation;

    // 初始化游戏对象基类
    ObjectGuid::LowType guidLow = guid ? guid : sObjectMgr->GetGenerator<HighGuid::Mo_Transport>().Generate();

    if (!trans->Create(guidLow, entry, mapId, x, y, z, o, 255))
    {
        delete trans;
        return nullptr;
    }

    // 验证地图类型匹配
    if (MapEntry const* mapEntry = sMapStore.LookupEntry(mapId))
    {
        if (mapEntry->Instanceable() != tInfo->inInstance)
        {
            TC_LOG_ERROR("entities.transport", "Transport {} (name: {}) attempted creation in instance map (id: {}) but it is not an instanced transport!", entry, trans->GetName(), mapId);
            delete trans;
            return nullptr;
        }
    }

    // 设置地图：副本使用预设地图，大地图创建新地图
    trans->SetMap(map ? map : sMapMgr->CreateMap(mapId, nullptr));
    if (map && map->IsDungeon())
        trans->m_zoneScript = map->ToInstanceMap()->GetInstanceScript();

    // 注册到全局持有器（乘客会在玩家靠近时加载）
    HashMapHolder<Transport>::Insert(trans);
    trans->GetMap()->AddToMap<Transport>(trans);
    return trans;
}

/**
 * @brief 生成所有大地图运输工具
 *
 * 在服务器启动时创建所有大地图（非副本）上的运输工具。
 * 这些运输工具持久存在于游戏世界中。
 *
 * 从 transports 表读取预定义的运输工具GUID和模板ID，
 * 确保运输工具的GUID在服务器重启后保持一致。
 *
 * 调用时机：服务器启动时，地图初始化完成后
 */
void TransportMgr::SpawnContinentTransports()
{
    // 如果没有运输工具模板，直接返回
    if (_transportTemplates.empty())
        return;

    uint32 oldMSTime = getMSTime();

    // 从数据库查询大地图运输工具
    QueryResult result = WorldDatabase.Query("SELECT guid, entry FROM transports");

    uint32 count = 0;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            ObjectGuid::LowType guid = fields[0].GetUInt32();
            uint32 entry = fields[1].GetUInt32();

            // 只创建大地图运输工具（非副本）
            if (TransportTemplate const* tInfo = GetTransportTemplate(entry))
                if (!tInfo->inInstance)
                    if (CreateTransport(entry, guid))
                        ++count;

        } while (result->NextRow());
    }

    TC_LOG_INFO("server.loading", ">> Spawned {} continent transports in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 为副本创建所有运输工具
 *
 * @param map 副本地图指针
 *
 * 当副本地图创建时，为其创建预定义的所有运输工具。
 * 运输工具随副本销毁而销毁。
 *
 * 调用时机：副本地图创建时
 */
void TransportMgr::CreateInstanceTransports(Map* map)
{
    // 查找该地图的运输工具列表
    TransportInstanceMap::const_iterator mapTransports = _instanceTransports.find(map->GetId());

    // 该地图没有运输工具
    if (mapTransports == _instanceTransports.end() || mapTransports->second.empty())
        return;

    // 创建所有运输工具
    for (std::set<uint32>::const_iterator itr = mapTransports->second.begin(); itr != mapTransports->second.end(); ++itr)
        CreateTransport(*itr, 0, map);
}

/**
 * @brief 获取指定时间的动画节点
 *
 * @param time 时间点（毫秒）
 * @return 动画节点指针，如果找不到返回 nullptr
 *
 * 使用 lower_bound 查找大于等于指定时间的第一个节点。
 * 用于动态运输工具的位置插值。
 */
TransportAnimationEntry const* TransportAnimation::GetAnimNode(uint32 time) const
{
    auto itr = Path.lower_bound(time);
    if (itr != Path.end())
        return itr->second;

    return nullptr;
}

/**
 * @brief 获取指定时间的旋转数据
 *
 * @param time 时间点（毫秒）
 * @return 旋转数据指针，如果找不到返回 nullptr
 *
 * 使用 lower_bound 查找大于等于指定时间的第一个旋转节点。
 * 用于动态运输工具的朝向插值。
 */
TransportRotationEntry const* TransportAnimation::GetAnimRotation(uint32 time) const
{
    auto itr = Rotations.lower_bound(time);
    if (itr != Rotations.end())
        return itr->second;

    return nullptr;
}
