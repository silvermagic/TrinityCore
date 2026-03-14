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
 * @file DynamicTree.h
 * @brief 动态地图树头文件
 *
 * 定义了动态地图树（DynamicMapTree）类，用于管理游戏世界中的动态游戏对象。
 * 该类是对底层空间索引结构的高级封装，提供简洁的 API 接口。
 *
 * 主要功能：
 * - 游戏对象的插入、删除、查询
 * - 射线相交测试
 * - 视线检测
 * - 高度查询
 * - 区域信息查询
 * - 液体数据查询
 *
 * 使用场景：
 * - 游戏对象的碰撞检测
 * - 射线技能的命中检测
 * - 角色移动的视线检测
 * - 地形高度查询
 */

#ifndef _DYNTREE_H
#define _DYNTREE_H

#include "Define.h"

namespace G3D
{
    class Ray;
    class Vector3;
}

class GameObjectModel;
struct DynTreeImpl;

namespace VMAP
{
    struct AreaAndLiquidData;
}

/**
 * @brief 动态地图树类
 *
 * 管理游戏世界中动态游戏对象的空间索引结构。
 * 使用 PIMPL 模式封装底层实现，提供稳定的 API 接口。
 *
 * 线程安全：
 * - 该类不是线程安全的，调用者需要自行处理同步
 *
 * 生命周期：
 * - 通常与地图实例的生命周期绑定
 */
class TC_COMMON_API DynamicMapTree
{
    DynTreeImpl *impl;  ///< 实现指针（PIMPL 模式）

public:

    /**
     * @brief 构造函数
     *
     * 创建一个空的动态地图树
     */
    DynamicMapTree();

    /**
     * @brief 析构函数
     *
     * 销毁动态地图树及其所有资源
     */
    ~DynamicMapTree();

    /**
     * @brief 检查两点之间是否有视线
     *
     * @param x1,y1,z1 第一个点的坐标
     * @param x2,y2,z2 第二个点的坐标
     * @param phasemask 相位掩码，用于过滤可见对象
     * @return true 有视线（无障碍物）
     * @return false 无视线（有障碍物）
     *
     * @note 当两点重合时返回 true
     * @note 性能：平均 O(log n)，最坏 O(n)
     */
    bool isInLineOfSight(float x1, float y1, float z1, float x2, float y2,
                         float z2, uint32 phasemask) const;

    /**
     * @brief 获取射线相交时间
     *
     * @param phasemask 相位掩码
     * @param ray 射线
     * @param endPos 射线终点
     * @param maxDist 输入/输出参数，最大检测距离，可能会被更新为实际相交距离
     * @return true 发生相交
     * @return false 未相交
     *
     * @note 性能：平均 O(log n)，最坏 O(n)
     */
    bool getIntersectionTime(uint32 phasemask, const G3D::Ray& ray,
                             const G3D::Vector3& endPos, float& maxDist) const;

    /**
     * @brief 获取区域信息
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
     */
    bool getAreaInfo(float x, float y, float& z, uint32 phasemask, uint32& flags, int32& adtId, int32& rootId, int32& groupId) const;

    /**
     * @brief 获取区域和液体数据
     *
     * @param x,y,z 查询点坐标
     * @param phasemask 相位掩码
     * @param reqLiquidType 请求的液体类型，为 0 表示所有类型
     * @param data 输出参数，区域和液体数据
     */
    void getAreaAndLiquidData(float x, float y, float z, uint32 phasemask, uint8 reqLiquidType, VMAP::AreaAndLiquidData& data) const;

    /**
     * @brief 获取对象命中位置
     *
     * @param phasemask 相位掩码
     * @param pPos1 起点位置
     * @param pPos2 终点位置
     * @param pResultHitPos 输出参数，相交点位置
     * @param pModifyDist 距离修正值，正数向前偏移，负数向后偏移
     * @return true 发生相交
     * @return false 未相交
     *
     * @note 使用 pModifyDist 可以调整命中点的位置，例如避免贴墙移动
     */
    bool getObjectHitPos(uint32 phasemask, const G3D::Vector3& pPos1,
                         const G3D::Vector3& pPos2, G3D::Vector3& pResultHitPos,
                         float pModifyDist) const;

    /**
     * @brief 获取高度
     *
     * @param x,y,z 查询点坐标
     * @param maxSearchDist 最大搜索距离
     * @param phasemask 相位掩码
     * @return 地形高度，如果未命中则返回负无穷
     *
     * @note 从指定点向下发射射线进行查询
     */
    float getHeight(float x, float y, float z, float maxSearchDist, uint32 phasemask) const;

    /**
     * @brief 插入游戏对象模型
     * @param mdl 游戏对象模型
     *
     * @note 插入后树会标记为需要平衡，在下次查询或更新时自动重建
     */
    void insert(GameObjectModel const&);

    /**
     * @brief 删除游戏对象模型
     * @param mdl 游戏对象模型
     *
     * @note 删除后树会标记为需要平衡，在下次查询或更新时自动重建
     */
    void remove(GameObjectModel const&);

    /**
     * @brief 检查是否包含指定游戏对象模型
     * @param mdl 游戏对象模型
     * @return true 包含该模型
     * @return false 不包含该模型
     */
    bool contains(GameObjectModel const&) const;

    /**
     * @brief 平衡树结构
     *
     * 重建树以优化查询性能
     *
     * @note 通常不需要手动调用，update() 会自动处理
     */
    void balance();

    /**
     * @brief 更新树状态
     * @param diff 时间增量（毫秒）
     *
     * 定时检查并自动平衡树，建议在地图更新循环中调用
     */
    void update(uint32 diff);
};

#endif // _DYNTREE_H
