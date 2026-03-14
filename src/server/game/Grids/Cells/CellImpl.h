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
 * @file CellImpl.h
 * @brief 单元格实现内联函数定义
 *
 * 本文件提供了 Cell 类的内联函数实现，负责游戏世界中网格和单元格的访问操作。
 * 主要功能包括：
 * - 单元格坐标计算和转换
 * - 圆形区域单元格计算
 * - 网格对象访问的访问者模式实现
 * - 基于位置和半径的对象查询优化
 *
 * 单元格系统是游戏世界空间分割的核心，用于优化空间查询和对象管理。
 * 每个网格被划分为多个单元格，通过单元格坐标快速定位和访问游戏对象。
 *
 * @see Cell
 * @see CellCoord
 * @see CellArea
 */

#ifndef TRINITY_CELLIMPL_H
#define TRINITY_CELLIMPL_H

#include <cmath>

#include "Cell.h"
#include "Map.h"
#include "Object.h"

/**
 * @brief 从单元格坐标构造 Cell 对象
 *
 * 将单元格坐标转换为网格坐标和单元格内部坐标。
 * 转换公式：
 * - grid_x/cell_x = x_coord / MAX_NUMBER_OF_CELLS (网格坐标 = 总坐标 / 每网格单元格数)
 * - cell_x/cell_y = x_coord % MAX_NUMBER_OF_CELLS (单元格内坐标 = 总坐标 % 每网格单元格数)
 *
 * @param p 单元格坐标（CellCoord 结构，包含归一化后的 x/y 坐标）
 *
 * @note 该构造函数为内联函数，频繁调用，性能关键
 * @note 坐标转换确保单元格在网格中的正确定位
 */
inline Cell::Cell(CellCoord const& p) : data()
{
    // 计算网格 X 坐标：总坐标除以每个网格的单元格数量
    data.Part.grid_x = p.x_coord / MAX_NUMBER_OF_CELLS;
    // 计算网格 Y 坐标：总坐标除以每个网格的单元格数量
    data.Part.grid_y = p.y_coord / MAX_NUMBER_OF_CELLS;
    // 计算网格内的单元格 X 坐标：取余运算得到相对位置
    data.Part.cell_x = p.x_coord % MAX_NUMBER_OF_CELLS;
    // 计算网格内的单元格 Y 坐标：取余运算得到相对位置
    data.Part.cell_y = p.y_coord % MAX_NUMBER_OF_CELLS;
}

/**
 * @brief 计算圆形区域覆盖的单元格范围
 *
 * 根据中心坐标和半径计算需要访问的单元格区域。
 * 返回包含圆形区域的最小外接矩形单元格范围。
 *
 * @param x 中心点 X 坐标（游戏世界坐标）
 * @param y 中心点 Y 坐标（游戏世界坐标）
 * @param radius 搜索半径（游戏世界单位，码）
 * @return CellArea 覆盖区域的单元格范围（low_bound 到 high_bound）
 *
 * @note 当半径 <= 0 时，仅返回中心单元格
 * @note 计算方式：取圆形外接矩形的左上角和右下角坐标
 * @note 使用 normalize() 确保坐标在有效范围内
 *
 * @see CellArea
 * @see Trinity::ComputeCellCoord
 */
inline CellArea Cell::CalculateCellArea(float x, float y, float radius)
{
    // 半径为 0 或负数时，仅返回中心单元格
    if (radius <= 0.0f)
    {
        CellCoord center = Trinity::ComputeCellCoord(x, y).normalize();
        return CellArea(center, center);
    }

    // 计算圆形外接矩形左上角的单元格坐标（x - radius, y - radius）
    CellCoord centerX = Trinity::ComputeCellCoord(x - radius, y - radius).normalize();
    // 计算圆形外接矩形右下角的单元格坐标（x + radius, y + radius）
    CellCoord centerY = Trinity::ComputeCellCoord(x + radius, y + radius).normalize();

    return CellArea(centerX, centerY);
}

/**
 * @brief 访问指定对象周围的单元格区域
 *
 * 该重载版本自动将对象的战斗触及范围添加到搜索半径中，
 * 确保大型生物（如世界BOSS）能够正确检测到附近的玩家。
 *
 * @tparam T 访问者类型
 * @tparam CONTAINER 类型容器类型
 * @param standing_cell 当前单元格坐标
 * @param visitor 类型容器访问者，用于访问单元格中的对象
 * @param map 地图引用，用于执行访问操作
 * @param obj 中心对象（WorldObject），用于获取位置和战斗触及范围
 * @param radius 搜索半径（游戏世界单位，码）
 *
 * @note 半径会自动增加 obj.GetCombatReach()，确保大型生物的正确检测
 * @note 对于巨大生物，如果不增加半径可能导致无法攻击最近的玩家
 *
 * @see Visit(CellCoord const&, TypeContainerVisitor<T, CONTAINER>&, Map&, float, float, float) const
 */
template<class T, class CONTAINER>
inline void Cell::Visit(CellCoord const& standing_cell, TypeContainerVisitor<T, CONTAINER>& visitor, Map& map, WorldObject const& obj, float radius) const
{
    // 必须增加搜索半径，增加对象的战斗触及范围
    // 否则对于巨大的生物，可能无法攻击最近的玩家等问题
    Visit(standing_cell, visitor, map, obj.GetPositionX(), obj.GetPositionY(), radius + obj.GetCombatReach());
}

/**
 * @brief 访问指定坐标周围的单元格区域
 *
 * 核心访问函数，根据坐标和半径访问周围所有相关单元格。
 * 实现了优化的单元格访问策略：
 * - 半径为 0 时仅访问当前单元格
 * - 半径较大时使用优化的圆形访问算法（VisitCircle）
 * - 一般情况使用矩形遍历算法
 *
 * @tparam T 访问者类型
 * @tparam CONTAINER 类型容器类型
 * @param standing_cell 当前单元格坐标
 * @param visitor 类型容器访问者，用于访问单元格中的对象
 * @param map 地图引用，用于执行访问操作
 * @param x_off 中心点 X 坐标（游戏世界坐标）
 * @param y_off 中心点 Y 坐标（游戏世界坐标）
 * @param radius 搜索半径（游戏世界单位，码）
 *
 * @note 半径限制在 SIZE_OF_GRIDS 以内，避免跨网格搜索
 * @note 当前单元格总是优先访问（重要优化）
 * @note 当区域超过 4x4 单元格时，自动切换到优化的圆形访问算法
 * @note 动态对象可能传递 radius = 0.0f（数据库问题？），此处进行了容错处理
 *
 * @see CalculateCellArea
 * @see VisitCircle
 */
template<class T, class CONTAINER>
inline void Cell::Visit(CellCoord const& standing_cell, TypeContainerVisitor<T, CONTAINER>& visitor, Map& map, float x_off, float y_off, float radius) const
{
    // 坐标无效时直接返回
    if (!standing_cell.IsCoordValid())
        return;

    // 半径为 0 或负数时的处理
    // 曾考虑添加断言，但动态对象可能传递 radius = 0.0f（可能是数据库问题）
    // 更好的做法是直接返回或仅访问当前单元格
    if (radius <= 0.0f)
    {
        map.Visit(*this, visitor);
        return;
    }

    // 限制搜索半径上限为网格大小，避免跨网格搜索
    if (radius > SIZE_OF_GRIDS)
        radius = SIZE_OF_GRIDS;

    // 计算覆盖的单元格区域
    CellArea area = Cell::CalculateCellArea(x_off, y_off, radius);

    // 如果半径在当前单元格内（area 为空），仅访问当前单元格
    if (!area)
    {
        map.Visit(*this, visitor);
        return;
    }

    // 访问 CalculateCellArea() 计算出的所有单元格
    // 如果半径覆盖的区域超过 4x4 单元格，则调用优化的 VisitCircle
    // 目前该优化技术适用于 MAX_NUMBER_OF_CELLS >= 16 的情况
    // 对于更小的值，SIZE_OF_GRID_CELL 太大，没有优化空间
    if ((area.high_bound.x_coord > (area.low_bound.x_coord + 4)) && (area.high_bound.y_coord > (area.low_bound.y_coord + 4)))
    {
        VisitCircle(visitor, map, area.low_bound, area.high_bound);
        return;
    }

    // 总是优先访问当前单元格！！！
    // 对于小半径搜索，优先访问当前单元格非常重要
    map.Visit(*this, visitor);

    // 循环遍历单元格范围
    for (uint32 x = area.low_bound.x_coord; x <= area.high_bound.x_coord; ++x)
    {
        for (uint32 y = area.low_bound.y_coord; y <= area.high_bound.y_coord; ++y)
        {
            CellCoord cellCoord(x, y);
            // 跳过当前单元格，因为已经访问过了
            if (cellCoord != standing_cell)
            {
                Cell r_zone(cellCoord);
                // 继承 nocreate 标志，避免创建新单元格
                r_zone.data.Part.nocreate = this->data.Part.nocreate;
                map.Visit(r_zone, visitor);
            }
        }
    }
}

/**
 * @brief 使用八边形填充算法访问圆形区域内的单元格
 *
 * 该函数实现了优化的圆形区域访问算法，采用八边形填充策略：
 * 1. 计算中心条带的宽度
 * 2. 访问固定宽度的中心条带
 * 3. 访问两侧的梯形区域，形成完整的八边形
 *
 * 这种算法比直接遍历矩形区域更高效，减少了不必要的单元格访问。
 * 适用于大半径搜索（超过 4x4 单元格）的场景。
 *
 * @tparam T 访问者类型
 * @tparam CONTAINER 类型容器类型
 * @param visitor 类型容器访问者，用于访问单元格中的对象
 * @param map 地图引用，用于执行访问操作
 * @param begin_cell 区域起始单元格坐标（左上角）
 * @param end_cell 区域结束单元格坐标（右下角）
 *
 * @note 算法复杂度优于完整的矩形遍历
 * @note 通过对称性访问减少循环次数
 * @note x_shift 计算基于 0.3f 的经验值，用于确定中心条带宽度
 *
 * @see Visit
 */
template<class T, class CONTAINER>
inline void Cell::VisitCircle(TypeContainerVisitor<T, CONTAINER>& visitor, Map& map, CellCoord const& begin_cell, CellCoord const& end_cell) const
{
    // 八边形填充算法
    // 计算中心条带的宽度偏移量
    // 使用 0.3f 作为经验系数，平衡中心条带和两侧梯形的宽度
    uint32 x_shift = (uint32)ceilf((end_cell.x_coord - begin_cell.x_coord) * 0.3f - 0.5f);

    // 计算中心条带的起始和结束 X 坐标
    const uint32 x_start = begin_cell.x_coord + x_shift;
    const uint32 x_end = end_cell.x_coord - x_shift;

    // 访问固定宽度的中心条带
    for (uint32 x = x_start; x <= x_end; ++x)
    {
        for (uint32 y = begin_cell.y_coord; y <= end_cell.y_coord; ++y)
        {
            CellCoord cellCoord(x, y);
            Cell r_zone(cellCoord);
            // 继承 nocreate 标志
            r_zone.data.Part.nocreate = this->data.Part.nocreate;
            map.Visit(r_zone, visitor);
        }
    }

    // 如果 x_shift == 0，说明区域太小，已经在上一步访问完毕
    if (x_shift == 0)
        return;

    uint32 y_start = end_cell.y_coord;
    uint32 y_end = begin_cell.y_coord;

    // 访问八边形的边框区域（两个梯形）
    for (uint32 step = 1; step <= (x_start - begin_cell.x_coord); ++step)
    {
        // 每一步减少条带高度 2 个单元格
        y_end += 1;
        y_start -= 1;

        for (uint32 y = y_start; y >= y_end; --y)
        {
            // 对称访问两侧的单元格：从中心向两侧，从上到下
            // 填充中心条带后的两个梯形区域

            // 左侧梯形单元格访问
            CellCoord cellCoord_left(x_start - step, y);
            Cell r_zone_left(cellCoord_left);
            r_zone_left.data.Part.nocreate = this->data.Part.nocreate;
            map.Visit(r_zone_left, visitor);

            // 右侧梯形单元格访问
            CellCoord cellCoord_right(x_end + step, y);
            Cell r_zone_right(cellCoord_right);
            r_zone_right.data.Part.nocreate = this->data.Part.nocreate;
            map.Visit(r_zone_right, visitor);
        }
    }
}

/**
 * @brief 访问指定对象周围的网格对象
 *
 * 便捷函数，访问中心对象周围指定半径内的网格对象（GridTypeMapContainer）。
 * 网格对象通常包括：Creature（生物）、GameObject（游戏对象）、DynamicObject（动态对象）等。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param center_obj 中心对象指针，作为搜索的中心点
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 使用 GridTypeMapContainer，仅访问网格级别对象
 * @note 自动计算中心对象的单元格坐标
 *
 * @see Visit
 * @see GridTypeMapContainer
 */
template<class T>
inline void Cell::VisitGridObjects(WorldObject const* center_obj, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算中心对象所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(center_obj->GetPositionX(), center_obj->GetPositionY()));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 创建网格类型容器访问者
    TypeContainerVisitor<T, GridTypeMapContainer> gnotifier(visitor);
    // 执行访问操作
    cell.Visit(p, gnotifier, *center_obj->GetMap(), *center_obj, radius);
}

/**
 * @brief 访问指定对象周围的世界对象
 *
 * 便捷函数，访问中心对象周围指定半径内的世界对象（WorldTypeMapContainer）。
 * 世界对象通常包括：Player（玩家）、Corpse（尸体）等需要跨网格交互的对象。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param center_obj 中心对象指针，作为搜索的中心点
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 使用 WorldTypeMapContainer，仅访问世界级别对象
 * @note 自动计算中心对象的单元格坐标
 *
 * @see Visit
 * @see WorldTypeMapContainer
 */
template<class T>
inline void Cell::VisitWorldObjects(WorldObject const* center_obj, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算中心对象所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(center_obj->GetPositionX(), center_obj->GetPositionY()));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 创建世界类型容器访问者
    TypeContainerVisitor<T, WorldTypeMapContainer> wnotifier(visitor);
    // 执行访问操作
    cell.Visit(p, wnotifier, *center_obj->GetMap(), *center_obj, radius);
}

/**
 * @brief 访问指定对象周围的所有对象
 *
 * 便捷函数，访问中心对象周围指定半径内的所有对象类型。
 * 该函数会依次访问世界对象和网格对象，确保覆盖所有类型的游戏对象。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param center_obj 中心对象指针，作为搜索的中心点
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 该函数执行两次访问操作：先访问世界对象，再访问网格对象
 * @note 可能会对同一个单元格访问两次，但访问不同类型的对象容器
 * @note 自动计算中心对象的单元格坐标
 *
 * @see VisitWorldObjects
 * @see VisitGridObjects
 * @see WorldTypeMapContainer
 * @see GridTypeMapContainer
 */
template<class T>
inline void Cell::VisitAllObjects(WorldObject const* center_obj, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算中心对象所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(center_obj->GetPositionX(), center_obj->GetPositionY()));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 首先访问世界类型容器中的对象（Player、Corpse 等）
    TypeContainerVisitor<T, WorldTypeMapContainer> wnotifier(visitor);
    cell.Visit(p, wnotifier, *center_obj->GetMap(), *center_obj, radius);

    // 然后访问网格类型容器中的对象（Creature、GameObject 等）
    TypeContainerVisitor<T, GridTypeMapContainer> gnotifier(visitor);
    cell.Visit(p, gnotifier, *center_obj->GetMap(), *center_obj, radius);
}

/**
 * @brief 访问指定坐标周围的网格对象
 *
 * 便捷函数，访问指定坐标周围半径内的网格对象（GridTypeMapContainer）。
 * 网格对象通常包括：Creature（生物）、GameObject（游戏对象）、DynamicObject（动态对象）等。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param x 中心点 X 坐标（游戏世界坐标）
 * @param y 中心点 Y 坐标（游戏世界坐标）
 * @param map 地图指针，用于执行访问操作
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 使用 GridTypeMapContainer，仅访问网格级别对象
 * @note 适用于没有中心对象的场景（如技能施放点的对象查询）
 *
 * @see Visit
 * @see GridTypeMapContainer
 */
template<class T>
inline void Cell::VisitGridObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算指定坐标所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(x, y));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 创建网格类型容器访问者
    TypeContainerVisitor<T, GridTypeMapContainer> gnotifier(visitor);
    // 执行访问操作
    cell.Visit(p, gnotifier, *map, x, y, radius);
}

/**
 * @brief 访问指定坐标周围的世界对象
 *
 * 便捷函数，访问指定坐标周围半径内的世界对象（WorldTypeMapContainer）。
 * 世界对象通常包括：Player（玩家）、Corpse（尸体）等需要跨网格交互的对象。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param x 中心点 X 坐标（游戏世界坐标）
 * @param y 中心点 Y 坐标（游戏世界坐标）
 * @param map 地图指针，用于执行访问操作
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 使用 WorldTypeMapContainer，仅访问世界级别对象
 * @note 适用于没有中心对象的场景（如特定坐标点的玩家查询）
 *
 * @see Visit
 * @see WorldTypeMapContainer
 */
template<class T>
inline void Cell::VisitWorldObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算指定坐标所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(x, y));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 创建世界类型容器访问者
    TypeContainerVisitor<T, WorldTypeMapContainer> wnotifier(visitor);
    // 执行访问操作
    cell.Visit(p, wnotifier, *map, x, y, radius);
}

/**
 * @brief 访问指定坐标周围的所有对象
 *
 * 便捷函数，访问指定坐标周围半径内的所有对象类型。
 * 该函数会依次访问世界对象和网格对象，确保覆盖所有类型的游戏对象。
 *
 * @tparam T 访问者类型，必须实现 Visit 方法的访问者对象
 * @param x 中心点 X 坐标（游戏世界坐标）
 * @param y 中心点 Y 坐标（游戏世界坐标）
 * @param map 地图指针，用于执行访问操作
 * @param visitor 访问者对象引用，用于处理访问到的对象
 * @param radius 搜索半径（游戏世界单位，码）
 * @param dont_load 是否禁止加载单元格（默认 true），设置为 true 可避免不必要的单元格加载
 *
 * @note 默认 dont_load = true，不加载未激活的单元格
 * @note 该函数执行两次访问操作：先访问世界对象，再访问网格对象
 * @note 可能会对同一个单元格访问两次，但访问不同类型的对象容器
 * @note 适用于没有中心对象的场景（如特定坐标点的全面对象查询）
 *
 * @see VisitWorldObjects
 * @see VisitGridObjects
 * @see WorldTypeMapContainer
 * @see GridTypeMapContainer
 */
template<class T>
inline void Cell::VisitAllObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load /*= true*/)
{
    // 计算指定坐标所在的单元格坐标
    CellCoord p(Trinity::ComputeCellCoord(x, y));
    Cell cell(p);

    // 设置不创建标志，避免加载未激活的单元格
    if (dont_load)
        cell.SetNoCreate();

    // 首先访问世界类型容器中的对象（Player、Corpse 等）
    TypeContainerVisitor<T, WorldTypeMapContainer> wnotifier(visitor);
    cell.Visit(p, wnotifier, *map, x, y, radius);

    // 然后访问网格类型容器中的对象（Creature、GameObject 等）
    TypeContainerVisitor<T, GridTypeMapContainer> gnotifier(visitor);
    cell.Visit(p, gnotifier, *map, x, y, radius);
}

#endif
