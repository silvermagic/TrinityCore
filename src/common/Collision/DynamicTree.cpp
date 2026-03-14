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
 * @file DynamicTree.cpp
 * @brief 动态地图树实现文件
 *
 * 本文件实现了动态地图树（DynamicMapTree），用于管理游戏世界中的动态游戏对象模型。
 * 主要功能：
 * - 管理动态游戏对象的碰撞检测
 * - 提供射线相交、视线检测、高度查询等功能
 * - 支持游戏对象的动态添加和删除
 * - 自动平衡树结构以优化查询性能
 *
 * 技术实现：
 * - 基于 RegularGrid2D 和 BIHWrap 的组合结构
 * - 使用定时器控制树的平衡频率
 * - 支持相位掩码过滤
 */

#include "DynamicTree.h"
#include "BoundingIntervalHierarchyWrapper.h"
#include "GameObjectModel.h"
#include "MapTree.h"
#include "ModelIgnoreFlags.h"
#include "RegularGrid.h"
#include "Timer.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "WorldModel.h"
#include <G3D/AABox.h>
#include <G3D/Ray.h>
#include <G3D/Vector3.h>

using VMAP::ModelInstance;

namespace {

/// 树检查周期（毫秒），用于定时器控制平衡频率
int CHECK_TREE_PERIOD = 200;

} // namespace

/**
 * @brief GameObjectModel 的哈希特征模板特化
 *
 * 为 GameObjectModel 提供哈希函数，用于在哈希表中存储对象。
 */
template<> struct HashTrait< GameObjectModel>{
    /**
     * @brief 计算对象哈希值
     * @param g 游戏对象模型
     * @return 对象地址作为哈希值
     */
    static size_t hashCode(GameObjectModel const& g) { return (size_t)(void*)&g; }
};

/**
 * @brief GameObjectModel 的位置特征模板特化
 *
 * 为 GameObjectModel 提供位置获取函数，用于空间查询。
 */
template<> struct PositionTrait< GameObjectModel> {
    /**
     * @brief 获取对象位置
     * @param g 游戏对象模型
     * @param p 输出参数，对象位置
     */
    static void getPosition(GameObjectModel const& g, G3D::Vector3& p) { p = g.getPosition(); }
};

/**
 * @brief GameObjectModel 的包围盒特征模板特化
 *
 * 为 GameObjectModel 提供包围盒获取函数，用于空间索引。
 */
template<> struct BoundsTrait< GameObjectModel> {
    /**
     * @brief 获取对象包围盒（引用版本）
     * @param g 游戏对象模型
     * @param out 输出参数，对象包围盒
     */
    static void getBounds(GameObjectModel const& g, G3D::AABox& out) { out = g.getBounds();}

    /**
     * @brief 获取对象包围盒（指针版本）
     * @param g 游戏对象模型指针
     * @param out 输出参数，对象包围盒
     */
    static void getBounds2(GameObjectModel const* g, G3D::AABox& out) { out = g->getBounds();}
};

/*
static bool operator==(GameObjectModel const& mdl, GameObjectModel const& mdl2){
    return &mdl == &mdl2;
}
*/

/// 父树类型定义：使用规则网格 + BIH 包装器的组合结构
typedef RegularGrid2D<GameObjectModel, BIHWrap<GameObjectModel> > ParentTree;

/**
 * @brief 动态树实现结构
 *
 * 继承自 RegularGrid2D，实现了动态游戏对象的空间索引和管理。
 * 提供了插入、删除、平衡和更新等核心功能。
 */
struct DynTreeImpl : public ParentTree/*, public Intersectable*/
{
    typedef GameObjectModel Model;  ///< 模型类型别名
    typedef ParentTree base;        ///< 基类类型别名

    /**
     * @brief 构造函数
     *
     * 初始化平衡定时器和未平衡计数器
     */
    DynTreeImpl() :
        rebalance_timer(CHECK_TREE_PERIOD),
        unbalanced_times(0)
    {
    }

    /**
     * @brief 插入模型
     * @param mdl 游戏对象模型
     *
     * @note 增加未平衡计数，表示树需要重建
     */
    void insert(Model const& mdl)
    {
        base::insert(mdl);
        ++unbalanced_times;
    }

    /**
     * @brief 删除模型
     * @param mdl 游戏对象模型
     *
     * @note 增加未平衡计数，表示树需要重建
     */
    void remove(Model const& mdl)
    {
        base::remove(mdl);
        ++unbalanced_times;
    }

    /**
     * @brief 平衡树
     *
     * 重建树结构以优化查询性能，并重置未平衡计数器
     */
    void balance()
    {
        base::balance();
        unbalanced_times = 0;
    }

    /**
     * @brief 更新树状态
     *
     * 定时检查是否需要平衡树，如果有未处理的修改则自动平衡
     *
     * @param difftime 时间增量（毫秒）
     *
     * @note 只有当树不为空时才会执行更新
     */
    void update(uint32 difftime)
    {
        if (empty())
            return;

        rebalance_timer.Update(difftime);
        if (rebalance_timer.Passed())
        {
            rebalance_timer.Reset(CHECK_TREE_PERIOD);
            if (unbalanced_times > 0)
                balance();
        }
    }

    TimeTracker rebalance_timer;  ///< 平衡定时器，控制平衡频率
    int unbalanced_times;         ///< 未平衡的修改次数
};

/**
 * @brief 构造函数
 *
 * 创建动态地图树实例，初始化实现对象
 */
DynamicMapTree::DynamicMapTree() : impl(new DynTreeImpl()) { }

/**
 * @brief 析构函数
 *
 * 销毁动态地图树实例，释放实现对象
 */
DynamicMapTree::~DynamicMapTree()
{
    delete impl;
}

/**
 * @brief 插入游戏对象模型
 * @param mdl 游戏对象模型
 */
void DynamicMapTree::insert(GameObjectModel const& mdl)
{
    impl->insert(mdl);
}

/**
 * @brief 删除游戏对象模型
 * @param mdl 游戏对象模型
 */
void DynamicMapTree::remove(GameObjectModel const& mdl)
{
    impl->remove(mdl);
}

/**
 * @brief 检查是否包含指定游戏对象模型
 * @param mdl 游戏对象模型
 * @return true 包含该模型
 * @return false 不包含该模型
 */
bool DynamicMapTree::contains(GameObjectModel const& mdl) const
{
    return impl->contains(mdl);
}

/**
 * @brief 平衡树结构
 *
 * 重建树以优化查询性能
 */
void DynamicMapTree::balance()
{
    impl->balance();
}

/**
 * @brief 更新树状态
 * @param t_diff 时间增量（毫秒）
 *
 * 定时检查并自动平衡树
 */
void DynamicMapTree::update(uint32 t_diff)
{
    impl->update(t_diff);
}

/**
 * @brief 动态树相交回调结构
 *
 * 用于射线相交测试的回调函数对象，记录是否发生相交
 */
struct DynamicTreeIntersectionCallback
{
    bool did_hit;       ///< 是否发生相交
    uint32 phase_mask;  ///< 相位掩码，用于过滤对象

    /**
     * @brief 构造函数
     * @param phasemask 相位掩码
     */
    DynamicTreeIntersectionCallback(uint32 phasemask) : did_hit(false), phase_mask(phasemask) { }

    /**
     * @brief 调用运算符，执行实际的相交测试
     * @param r 射线
     * @param obj 游戏对象模型
     * @param distance 距离（输入/输出）
     * @return true 发生相交
     * @return false 未相交
     */
    bool operator()(G3D::Ray const& r, GameObjectModel const& obj, float& distance)
    {
        did_hit = obj.intersectRay(r, distance, true, phase_mask, VMAP::ModelIgnoreFlags::Nothing);
        return did_hit;
    }

    /**
     * @brief 检查是否发生相交
     * @return true 发生相交
     * @return false 未相交
     */
    bool didHit() const { return did_hit;}
};

/**
 * @brief 动态树区域信息回调结构
 *
 * 用于点查询的回调函数对象，收集区域信息
 */
struct DynamicTreeAreaInfoCallback
{
    /**
     * @brief 构造函数
     * @param phaseMask 相位掩码
     */
    DynamicTreeAreaInfoCallback(uint32 phaseMask) : _phaseMask(phaseMask) {}

    /**
     * @brief 调用运算符，执行点相交测试并收集区域信息
     * @param p 点坐标
     * @param obj 游戏对象模型
     */
    void operator()(G3D::Vector3 const& p, GameObjectModel const& obj)
    {
        obj.intersectPoint(p, _areaInfo, _phaseMask);
    }

    /**
     * @brief 获取区域信息
     * @return 区域信息的常量引用
     */
    VMAP::AreaInfo const& GetAreaInfo() const { return _areaInfo; }

private:
    uint32 _phaseMask;        ///< 相位掩码
    VMAP::AreaInfo _areaInfo; ///< 区域信息
};

/**
 * @brief 动态树位置信息回调结构
 *
 * 用于点查询的回调函数对象，收集位置和液体信息
 */
struct DynamicTreeLocationInfoCallback
{
    /**
     * @brief 构造函数
     * @param phaseMask 相位掩码
     */
    DynamicTreeLocationInfoCallback(uint32 phaseMask) : _phaseMask(phaseMask), _hitModel(nullptr) {}

    /**
     * @brief 调用运算符，执行位置查询并收集信息
     * @param p 点坐标
     * @param obj 游戏对象模型
     */
    void operator()(G3D::Vector3 const& p, GameObjectModel const& obj)
    {
        if (obj.GetLocationInfo(p, _locationInfo, _phaseMask))
            _hitModel = &obj;
    }

    /**
     * @brief 获取位置信息
     * @return 位置信息的引用
     */
    VMAP::LocationInfo& GetLocationInfo() { return _locationInfo; }

    /**
     * @brief 获取命中的模型
     * @return 命中的游戏对象模型指针
     */
    GameObjectModel const* GetHitModel() const { return _hitModel; }

private:
    uint32 _phaseMask;               ///< 相位掩码
    VMAP::LocationInfo _locationInfo; ///< 位置信息
    GameObjectModel const* _hitModel; ///< 命中的模型
};

/**
 * @brief 获取射线相交时间
 *
 * 计算射线与场景中对象的相交时间（距离）
 *
 * @param phasemask 相位掩码，用于过滤对象
 * @param ray 射线
 * @param endPos 射线终点
 * @param maxDist 输入/输出参数，最大检测距离，可能会被更新为实际相交距离
 * @return true 发生相交
 * @return false 未相交
 */
bool DynamicMapTree::getIntersectionTime(const uint32 phasemask, const G3D::Ray& ray,
                                         const G3D::Vector3& endPos, float& maxDist) const
{
    float distance = maxDist;
    DynamicTreeIntersectionCallback callback(phasemask);
    impl->intersectRay(ray, callback, distance, endPos);
    if (callback.didHit())
        maxDist = distance;
    return callback.didHit();
}

/**
 * @brief 获取对象命中位置
 *
 * 计算从起点到终点的射线上，与场景对象相交的位置
 *
 * @param phasemask 相位掩码
 * @param startPos 起点位置
 * @param endPos 终点位置
 * @param resultHit 输出参数，相交点位置
 * @param modifyDist 距离修正值，正数向前偏移，负数向后偏移
 * @return true 发生相交
 * @return false 未相交
 *
 * @note 使用 modifyDist 可以调整命中点的位置，例如避免贴墙移动
 * @note 会进行 NaN 检查，防止无限循环
 */
bool DynamicMapTree::getObjectHitPos(const uint32 phasemask, const G3D::Vector3& startPos,
                                     const G3D::Vector3& endPos, G3D::Vector3& resultHit,
                                     float modifyDist) const
{
    bool result = false;
    float maxDist = (endPos - startPos).magnitude();

    // 有效的地图坐标不应该产生浮点溢出，但这也会产生 NaN
    ASSERT(maxDist < std::numeric_limits<float>::max());

    // 防止 NaN 值导致 BIH 相交进入无限循环
    if (maxDist < 1e-10f)
    {
        resultHit = endPos;
        return false;
    }

    G3D::Vector3 dir = (endPos - startPos)/maxDist;  // 单位方向向量
    G3D::Ray ray(startPos, dir);
    float dist = maxDist;

    if (getIntersectionTime(phasemask, ray, endPos, dist))
    {
        resultHit = startPos + dir * dist;
        if (modifyDist < 0)
        {
            // 向后偏移，确保不超出起点
            if ((resultHit - startPos).magnitude() > -modifyDist)
                resultHit = resultHit + dir*modifyDist;
            else
                resultHit = startPos;
        }
        else
            resultHit = resultHit + dir*modifyDist;  // 向前偏移

        result = true;
    }
    else
    {
        resultHit = endPos;
        result = false;
    }
    return result;
}

/**
 * @brief 检查两点之间是否有视线
 *
 * 判断两点之间是否有障碍物阻挡视线
 *
 * @param x1,y1,z1 第一个点的坐标
 * @param x2,y2,z2 第二个点的坐标
 * @param phasemask 相位掩码
 * @return true 有视线（无障碍物）
 * @return false 无视线（有障碍物）
 *
 * @note 当两点重合时返回 true
 */
bool DynamicMapTree::isInLineOfSight(float x1, float y1, float z1, float x2, float y2, float z2, uint32 phasemask) const
{
    G3D::Vector3 v1(x1, y1, z1), v2(x2, y2, z2);

    float maxDist = (v2 - v1).magnitude();

    // 两点重合，有视线
    if (!G3D::fuzzyGt(maxDist, 0) )
        return true;

    G3D::Ray r(v1, (v2-v1) / maxDist);
    DynamicTreeIntersectionCallback callback(phasemask);
    impl->intersectRay(r, callback, maxDist, v2);

    // 未相交 = 有视线
    return !callback.did_hit;
}

/**
 * @brief 获取高度
 *
 * 从指定点向下发射射线，获取该位置的地形高度
 *
 * @param x,y,z 查询点坐标
 * @param maxSearchDist 最大搜索距离
 * @param phasemask 相位掩码
 * @return 地形高度（z - maxSearchDist），如果未命中则返回负无穷
 *
 * @note 使用垂直向下的射线进行查询，适用于地形高度检测
 */
float DynamicMapTree::getHeight(float x, float y, float z, float maxSearchDist, uint32 phasemask) const
{
    G3D::Vector3 v(x, y, z);
    G3D::Ray r(v, G3D::Vector3(0, 0, -1));  // 向下的射线
    DynamicTreeIntersectionCallback callback(phasemask);
    impl->intersectZAllignedRay(r, callback, maxSearchDist);

    if (callback.didHit())
        return v.z - maxSearchDist;
    else
        return -G3D::finf();
}

/**
 * @brief 获取区域信息
 *
 * 查询指定位置的区域信息，包括标志、ADT ID、根 ID、组 ID 等
 *
 * @param x,y 查询点的 x,y 坐标
 * @param z 输入/输出参数，输入查询高度，输出地面高度
 * @param phasemask 相位掩码
 * @param flags 输出参数，区域标志
 * @param adtId 输出参数，ADT ID
 * @param rootId 输出参数，根 ID
 * @param groupId 输出参数，组 ID
 * @return true 成功获取区域信息
 * @return false 未找到区域信息
 *
 * @note 查询点会向上偏移 0.5 单位，以提高检测精度
 */
bool DynamicMapTree::getAreaInfo(float x, float y, float& z, uint32 phasemask, uint32& flags, int32& adtId, int32& rootId, int32& groupId) const
{
    G3D::Vector3 v(x, y, z + 0.5f);  // 向上偏移 0.5 单位
    DynamicTreeAreaInfoCallback intersectionCallBack(phasemask);
    impl->intersectPoint(v, intersectionCallBack);
    if (intersectionCallBack.GetAreaInfo().result)
    {
        flags = intersectionCallBack.GetAreaInfo().flags;
        adtId = intersectionCallBack.GetAreaInfo().adtId;
        rootId = intersectionCallBack.GetAreaInfo().rootId;
        groupId = intersectionCallBack.GetAreaInfo().groupId;
        z = intersectionCallBack.GetAreaInfo().ground_Z;
        return true;
    }
    return false;
}

/**
 * @brief 获取区域和液体数据
 *
 * 查询指定位置的区域信息和液体信息
 *
 * @param x,y,z 查询点坐标
 * @param phasemask 相位掩码
 * @param reqLiquidType 请求的液体类型，为 0 表示所有类型
 * @param data 输出参数，区域和液体数据
 *
 * @note 查询点会向上偏移 0.5 单位，以提高检测精度
 * @note 如果指定了液体类型，会进行类型标志检查
 */
void DynamicMapTree::getAreaAndLiquidData(float x, float y, float z, uint32 phasemask, uint8 reqLiquidType, VMAP::AreaAndLiquidData& data) const
{
    G3D::Vector3 v(x, y, z + 0.5f);  // 向上偏移 0.5 单位
    DynamicTreeLocationInfoCallback intersectionCallBack(phasemask);
    impl->intersectPoint(v, intersectionCallBack);

    if (intersectionCallBack.GetLocationInfo().hitModel)
    {
        data.floorZ = intersectionCallBack.GetLocationInfo().ground_Z;
        uint32 liquidType = intersectionCallBack.GetLocationInfo().hitModel->GetLiquidType();
        float liquidLevel;

        // 检查液体类型是否符合要求
        if (!reqLiquidType || VMAP::VMapFactory::createOrGetVMapManager()->GetLiquidFlagsPtr(liquidType) & reqLiquidType)
            if (intersectionCallBack.GetHitModel()->GetLiquidLevel(v, intersectionCallBack.GetLocationInfo(), liquidLevel))
                data.liquidInfo.emplace(liquidType, liquidLevel);

        // 填充区域信息
        data.areaInfo.emplace(0,
            intersectionCallBack.GetLocationInfo().rootId,
            intersectionCallBack.GetLocationInfo().hitModel->GetWmoID(),
            intersectionCallBack.GetLocationInfo().hitModel->GetMogpFlags());
    }
}
