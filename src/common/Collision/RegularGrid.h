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
 * @file RegularGrid.h
 * @brief 规则网格（Regular Grid）空间索引结构
 *
 * 本文件定义了 RegularGrid2D 类，实现了基于规则网格的空间索引结构。
 * 将二维空间划分为固定大小的网格单元，每个单元可以包含一个节点结构。
 *
 * 主要特点：
 * - 简单高效，适合均匀分布的对象
 * - 支持快速的插入、删除和查询操作
 * - 与 BIH 等树结构结合使用，形成多级索引
 *
 * 使用场景：
 * - 游戏对象的粗粒度空间划分
 * - 射线追踪的加速结构
 * - 碰撞检测的预处理
 */

#ifndef _REGULAR_GRID_H
#define _REGULAR_GRID_H

#include "Errors.h"
#include "IteratorPair.h"
#include <G3D/Ray.h>
#include <G3D/BoundsTrait.h>
#include <G3D/PositionTrait.h>
#include <unordered_map>

/**
 * @brief 节点创建器模板
 *
 * 提供默认的节点创建方法，可以被自定义创建器覆盖
 *
 * @tparam Node 节点类型
 */
template<class Node>
struct NodeCreator{
    /**
     * @brief 创建节点
     * @param x 网格 x 坐标（未使用）
     * @param y 网格 y 坐标（未使用）
     * @return 新创建的节点指针
     */
    static Node * makeNode(int /*x*/, int /*y*/) { return new Node();}
};

/**
 * @brief 二维规则网格模板类
 *
 * 将二维空间划分为固定大小的网格单元，每个单元可以包含一个节点结构。
 * 支持对象的插入、删除和空间查询操作。
 *
 * @tparam T 对象类型
 * @tparam Node 节点类型，通常为 BIHWrap<T>
 * @tparam NodeCreatorFunc 节点创建器，默认为 NodeCreator<Node>
 * @tparam BoundsFunc 获取对象包围盒的函数对象类型，默认为 BoundsTrait<T>
 * @tparam PositionFunc 获取对象位置的函数对象类型，默认为 PositionTrait<T>
 *
 * 网格参数：
 * - CELL_NUMBER: 64，每维度的网格数量
 * - HGRID_MAP_SIZE: 533.33333 * 64，地图总大小
 * - CELL_SIZE: 地图大小 / 网格数量，单个网格大小
 *
 * 使用场景：
 * - 游戏对象的粗粒度空间划分
 * - 射线追踪的加速结构
 * - 碰撞检测的预处理
 */
template<class T,
class Node,
class NodeCreatorFunc = NodeCreator<Node>,
class BoundsFunc = BoundsTrait<T>,
class PositionFunc = PositionTrait<T>
>
class TC_COMMON_API RegularGrid2D
{
public:

    enum{
        CELL_NUMBER = 64,  ///< 每维度的网格数量
    };

    #define HGRID_MAP_SIZE  (533.33333f * 64.f)     ///< 地图总大小，不应修改
    #define CELL_SIZE       float(HGRID_MAP_SIZE/(float)CELL_NUMBER)  ///< 单个网格大小

    typedef std::unordered_multimap<const T*, Node*> MemberTable;  ///< 成员表类型定义

    MemberTable memberTable;                    ///< 对象到节点的多映射表
    Node* nodes[CELL_NUMBER][CELL_NUMBER];      ///< 网格节点数组

    /**
     * @brief 构造函数
     *
     * 初始化所有网格节点为 nullptr
     */
    RegularGrid2D()
    {
        memset(nodes, 0, sizeof(nodes));
    }

    /**
     * @brief 析构函数
     *
     * 删除所有已分配的网格节点
     */
    ~RegularGrid2D()
    {
        for (int x = 0; x < CELL_NUMBER; ++x)
            for (int y = 0; y < CELL_NUMBER; ++y)
                delete nodes[x][y];
    }

    /**
     * @brief 插入对象
     *
     * 根据对象包围盒将其插入到所有重叠的网格单元中
     *
     * @param value 要插入的对象
     *
     * @note 一个对象可能被插入到多个网格单元中（当对象跨越网格边界时）
     */
    void insert(const T& value)
    {
        G3D::AABox bounds;
        BoundsFunc::getBounds(value, bounds);
        Cell low = Cell::ComputeCell(bounds.low().x, bounds.low().y);
        Cell high = Cell::ComputeCell(bounds.high().x, bounds.high().y);

        // 遍历所有重叠的网格单元
        for (int x = low.x; x <= high.x; ++x)
        {
            for (int y = low.y; y <= high.y; ++y)
            {
                Node& node = getGrid(x, y);
                node.insert(value);
                memberTable.emplace(&value, &node);
            }
        }
    }

    /**
     * @brief 删除对象
     *
     * 从所有包含该对象的网格单元中移除对象
     *
     * @param value 要删除的对象
     *
     * @note 会查找并移除所有与该对象相关的映射关系
     */
    void remove(const T& value)
    {
        for (auto& p : Trinity::Containers::MapEqualRange(memberTable, &value))
            p.second->remove(value);
        // 移除成员记录
        memberTable.erase(&value);
    }

    /**
     * @brief 平衡所有网格节点
     *
     * 对每个非空的网格节点调用其 balance() 方法，
     * 用于优化内部数据结构
     */
    void balance()
    {
        for (int x = 0; x < CELL_NUMBER; ++x)
            for (int y = 0; y < CELL_NUMBER; ++y)
                if (Node* n = nodes[x][y])
                    n->balance();
    }

    /**
     * @brief 检查是否包含指定对象
     * @param value 要检查的对象
     * @return true 包含该对象
     * @return false 不包含该对象
     */
    bool contains(const T& value) const { return memberTable.count(&value) > 0; }

    /**
     * @brief 检查网格是否为空
     * @return true 网格为空
     * @return false 网格不为空
     */
    bool empty() const { return memberTable.empty(); }

    /**
     * @brief 网格单元结构
     *
     * 表示网格中的一个单元位置
     */
    struct Cell
    {
        int x, y;  ///< 网格坐标

        /**
         * @brief 相等比较运算符
         * @param c2 另一个网格单元
         * @return true 两个单元相同
         * @return false 两个单元不同
         */
        bool operator==(Cell const& c2) const
        {
            return x == c2.x && y == c2.y;
        }

        /**
         * @brief 计算世界坐标对应的网格单元
         *
         * @param fx 世界坐标 x
         * @param fy 世界坐标 y
         * @return 对应的网格单元
         *
         * @note 网格中心在原点，坐标会偏移 CELL_NUMBER/2
         */
        static Cell ComputeCell(float fx, float fy)
        {
            Cell c = { int(fx * (1.f / CELL_SIZE) + (CELL_NUMBER / 2)), int(fy * (1.f / CELL_SIZE) + (CELL_NUMBER / 2)) };
            return c;
        }

        /**
         * @brief 检查网格单元是否有效
         * @return true 在有效范围内
         * @return false 超出网格边界
         */
        bool isValid() const { return x >= 0 && x < CELL_NUMBER && y >= 0 && y < CELL_NUMBER; }
    };

    /**
     * @brief 获取或创建网格节点
     *
     * @param x 网格 x 坐标
     * @param y 网格 y 坐标
     * @return 网格节点的引用
     *
     * @note 如果节点不存在，会自动创建
     * @note 使用断言检查坐标有效性
     */
    Node& getGrid(int x, int y)
    {
        ASSERT(x < CELL_NUMBER && y < CELL_NUMBER);
        if (!nodes[x][y])
            nodes[x][y] = NodeCreatorFunc::makeNode(x, y);
        return *nodes[x][y];
    }

    /**
     * @brief 射线相交测试（简化版本）
     *
     * @tparam RayCallback 回调函数类型
     * @param ray 射线
     * @param intersectCallback 相交回调函数
     * @param max_dist 最大检测距离
     *
     * @note 内部调用完整版本的 intersectRay
     */
    template<typename RayCallback>
    void intersectRay(const G3D::Ray& ray, RayCallback& intersectCallback, float max_dist)
    {
        intersectRay(ray, intersectCallback, max_dist, ray.origin() + ray.direction() * max_dist);
    }

    /**
     * @brief 射线相交测试（完整版本）
     *
     * 使用 3D DDA 算法遍历网格，测试射线与网格中所有对象的相交情况。
     * 该算法高效地遍历射线经过的所有网格单元。
     *
     * @tparam RayCallback 回调函数类型
     * @param ray 射线
     * @param intersectCallback 相交回调函数
     * @param max_dist 输入/输出参数，最大检测距离
     * @param end 射线终点
     *
     * @note 算法流程：
     *       1. 计算起点和终点所在的网格单元
     *       2. 如果在同一单元，直接测试该单元
     *       3. 否则使用 3D DDA 算法遍历经过的所有网格单元
     *       4. 对每个非空网格单元执行相交测试
     *
     * @note 性能优化：
     *       - 只遍历射线实际经过的网格单元
     *       - 提前终止：当相交距离小于当前网格距离时停止
     *
     * @note 时间复杂度：O(遍历的网格单元数 * 单元内查询时间)
     */
    template<typename RayCallback>
    void intersectRay(const G3D::Ray& ray, RayCallback& intersectCallback, float& max_dist, const G3D::Vector3& end)
    {
        Cell cell = Cell::ComputeCell(ray.origin().x, ray.origin().y);
        if (!cell.isValid())
            return;

        Cell last_cell = Cell::ComputeCell(end.x, end.y);

        // 特殊情况：起点和终点在同一网格单元内
        if (cell == last_cell)
        {
            if (Node* node = nodes[cell.x][cell.y])
                node->intersectRay(ray, intersectCallback, max_dist);
            return;
        }

        // 初始化 3D DDA 算法参数
        float voxel = (float)CELL_SIZE;
        float kx_inv = ray.invDirection().x, bx = ray.origin().x;
        float ky_inv = ray.invDirection().y, by = ray.origin().y;

        int stepX, stepY;
        float tMaxX, tMaxY;

        // 计算 x 方向的步进参数
        if (kx_inv >= 0)
        {
            stepX = 1;
            float x_border = (cell.x+1) * voxel;
            tMaxX = (x_border - bx) * kx_inv;
        }
        else
        {
            stepX = -1;
            float x_border = (cell.x-1) * voxel;
            tMaxX = (x_border - bx) * kx_inv;
        }

        // 计算 y 方向的步进参数
        if (ky_inv >= 0)
        {
            stepY = 1;
            float y_border = (cell.y+1) * voxel;
            tMaxY = (y_border - by) * ky_inv;
        }
        else
        {
            stepY = -1;
            float y_border = (cell.y-1) * voxel;
            tMaxY = (y_border - by) * ky_inv;
        }

        //int Cycles = std::max((int)ceilf(max_dist/tMaxX),(int)ceilf(max_dist/tMaxY));
        //int i = 0;

        // 计算每步的距离增量
        float tDeltaX = voxel * std::fabs(kx_inv);
        float tDeltaY = voxel * std::fabs(ky_inv);

        // 主遍历循环
        do
        {
            if (Node* node = nodes[cell.x][cell.y])
            {
                //float enterdist = max_dist;
                node->intersectRay(ray, intersectCallback, max_dist);
            }
            if (cell == last_cell)
                break;

            // 选择下一个网格单元（基于哪个边界更近）
            if (tMaxX < tMaxY)
            {
                tMaxX += tDeltaX;
                cell.x += stepX;
            }
            else
            {
                tMaxY += tDeltaY;
                cell.y += stepY;
            }
            //++i;
        } while (cell.isValid());
    }

    /**
     * @brief 点相交测试
     *
     * 查找包含指定点的所有对象
     *
     * @tparam IsectCallback 回调函数类型
     * @param point 点坐标
     * @param intersectCallback 相交回调函数
     *
     * @note 只需要查找点所在的网格单元，性能高效
     */
    template<typename IsectCallback>
    void intersectPoint(const G3D::Vector3& point, IsectCallback& intersectCallback)
    {
        Cell cell = Cell::ComputeCell(point.x, point.y);
        if (!cell.isValid())
            return;
        if (Node* node = nodes[cell.x][cell.y])
            node->intersectPoint(point, intersectCallback);
    }

    /**
     * @brief 垂直射线相交测试（优化版本）
     *
     * 专门优化用于垂直方向射线的相交测试。
     * 由于垂直射线不会跨越多个网格单元，可以直接查询单个单元。
     *
     * @tparam RayCallback 回调函数类型
     * @param ray 射线（假设方向为垂直）
     * @param intersectCallback 相交回调函数
     * @param max_dist 最大检测距离
     *
     * @note 性能优化：避免了完整的 DDA 遍历，直接查询单个网格单元
     */
    template<typename RayCallback>
    void intersectZAllignedRay(const G3D::Ray& ray, RayCallback& intersectCallback, float& max_dist)
    {
        Cell cell = Cell::ComputeCell(ray.origin().x, ray.origin().y);
        if (!cell.isValid())
            return;
        if (Node* node = nodes[cell.x][cell.y])
            node->intersectRay(ray, intersectCallback, max_dist);
    }
};

#undef CELL_SIZE
#undef HGRID_MAP_SIZE

#endif
