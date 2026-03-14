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
 * @file Cell.h
 * @brief 游戏世界网格单元系统
 *
 * 本文件定义了游戏世界中用于空间划分的网格单元系统。采用层次化网格结构：
 * - Grid（网格）：大尺度空间划分单元，用于地图数据的加载/卸载管理
 * - Cell（单元）：Grid 内部的小尺度划分单元，用于精细化的对象查询和访问
 *
 * 主要功能：
 * 1. 提供坐标转换：世界坐标 <-> Grid坐标 <-> Cell坐标
 * 2. 支持区域查询：基于半径搜索范围内的对象
 * 3. 优化性能：通过空间划分减少不必要的对象遍历
 * 4. 配合访问者模式：实现对网格内对象的高效遍历
 *
 * 网格层次关系：
 * - 每个地图被划分为多个 Grid（通常 64x64 个）
 * - 每个 Grid 被划分为多个 Cell（MAX_NUMBER_OF_CELLS x MAX_NUMBER_OF_CELLS 个）
 * - Cell 是最小的空间管理单元，用于对象存储和查询
 */

#ifndef TRINITY_CELL_H
#define TRINITY_CELL_H

#include "TypeContainer.h"
#include "TypeContainerVisitor.h"

#include "GridDefines.h"

class Map;
class WorldObject;

/**
 * @struct CellArea
 * @brief 表示一个矩形区域内的单元坐标范围
 *
 * 用于定义区域查询时的单元坐标边界，例如基于半径搜索时需要访问的所有 Cell 的范围。
 * 通常由 Cell::CalculateCellArea() 函数创建。
 */
struct CellArea
{
    /**
     * @brief 默认构造函数
     *
     * 创建一个空的 CellArea 对象，low_bound 和 high_bound 都会被初始化为默认值。
     */
    CellArea() { }

    /**
     * @brief 构造指定范围的 CellArea
     * @param low 区域的左下角坐标（包含）
     * @param high 区域的右上角坐标（包含）
     */
    CellArea(CellCoord low, CellCoord high) : low_bound(low), high_bound(high) { }

    /**
     * @brief 检查区域是否为空
     * @return 如果上下边界相同则返回 true，表示区域无效或为空
     *
     * 当 low_bound 和 high_bound 相等时，通常表示没有有效的区域需要处理。
     * 这个运算符常用于区域查询的早期退出判断。
     */
    bool operator!() const { return low_bound == high_bound; }

    /**
     * @brief 获取区域的边界坐标
     * @param[out] begin_cell 输出参数，区域的起始单元坐标（左下角）
     * @param[out] end_cell 输出参数，区域的结束单元坐标（右上角）
     *
     * 用于将区域边界传递给需要遍历的函数，便于进行范围遍历。
     */
    void ResizeBorders(CellCoord& begin_cell, CellCoord& end_cell) const
    {
        begin_cell = low_bound;
        end_cell = high_bound;
    }

    CellCoord low_bound;   ///< 区域的左下角单元坐标（包含边界）
    CellCoord high_bound;  ///< 区域的右上角单元坐标（包含边界）
};

/**
 * @struct Cell
 * @brief 网格单元，表示游戏世界中一个最小的空间管理单元
 *
 * Cell 是游戏世界空间划分的核心结构，用于高效管理和查询游戏对象。
 * 每个 Cell 包含以下信息：
 * 1. Grid 坐标：标识该 Cell 所属的 Grid
 * 2. Cell 坐标：标识在 Grid 内部的位置
 * 3. 创建标志：控制是否可以动态创建该 Cell
 *
 * 设计思想：
 * - 空间局部性：相邻的对象通常在同一或相邻的 Cell 中，便于范围查询
 * - 按需加载：只有当对象进入某个区域时才加载对应的 Grid 和 Cell
 * - 访问者模式：通过 Visit 系列函数遍历 Cell 中的对象
 *
 * 坐标系统：
 * - 世界坐标 (float x, y)：游戏中的实际位置
 * - Grid 坐标 (uint8 grid_x, grid_y)：标识 64x64 的网格位置
 * - Cell 坐标 (uint8 cell_x, cell_y)：标识 Grid 内的单元位置
 * - CellCoord：全局唯一的单元坐标，结合了 Grid 和 Cell 信息
 */
struct Cell
{
    /**
     * @brief 默认构造函数，初始化为零值
     *
     * 创建一个所有字段都为 0 的 Cell 对象，通常用于声明变量但尚未确定具体位置时。
     */
    Cell() : data() { }

    /**
     * @brief 从 CellCoord 构造 Cell 对象
     * @param p 单元坐标，包含完整的 Grid 和 Cell 信息
     *
     * 实现在 Cell.cpp 中，负责将一维的 CellCoord 解析为二维的 Grid 和 Cell 坐标。
     * 这是将压缩的坐标信息展开的过程。
     */
    explicit Cell(CellCoord const& p);

    /**
     * @brief 从世界坐标构造 Cell 对象
     * @param x 世界坐标 X 轴（东西方向）
     * @param y 世界坐标 Y 轴（南北方向）
     *
     * 通过 Trinity::ComputeCellCoord 将世界坐标转换为 CellCoord，再转换为 Cell。
     * 这是游戏中常用的构造方式，例如玩家移动到新位置时计算所在的 Cell。
     *
     * @note 性能考虑：涉及浮点运算和坐标转换，但结果可以缓存使用
     */
    explicit Cell(float x, float y) : Cell(Trinity::ComputeCellCoord(x, y)) { }

    /**
     * @brief 计算该 Cell 在整个地图中的绝对坐标
     * @param[out] x 输出参数，X 轴绝对坐标（0 到 TOTAL_NUMBER_OF_CELLS_PER_MAP）
     * @param[out] y 输出参数，Y 轴绝对坐标（0 到 TOTAL_NUMBER_OF_CELLS_PER_MAP）
     *
     * 将 Grid 坐标和 Cell 坐标合并为一个全局唯一的一维坐标系统。
     * 计算公式：绝对坐标 = Grid坐标 * 每Grid的Cell数 + Cell坐标
     *
     * 这个坐标用于访问地图的全局数组或进行跨 Grid 的坐标计算。
     */
    void Compute(uint32 &x, uint32 &y) const
    {
        x = data.Part.grid_x * MAX_NUMBER_OF_CELLS + data.Part.cell_x;
        y = data.Part.grid_y * MAX_NUMBER_OF_CELLS + data.Part.cell_y;
    }

    /**
     * @brief 比较两个 Cell 是否在不同的 Cell 坐标
     * @param cell 要比较的另一个 Cell
     * @return 如果在 Grid 内的 Cell 坐标不同，返回 true；否则返回 false
     *
     * 用于检测对象是否移动到了同一个 Grid 内的不同 Cell。
     * 即使在同一个 Grid 内，不同 Cell 也可能需要更新可见性或触发不同的处理逻辑。
     *
     * @note 只比较 Cell 坐标，不比较 Grid 坐标
     */
    bool DiffCell(Cell const& cell) const
    {
        return(data.Part.cell_x != cell.data.Part.cell_x ||
            data.Part.cell_y != cell.data.Part.cell_y);
    }

    /**
     * @brief 比较两个 Cell 是否在不同的 Grid
     * @param cell 要比较的另一个 Cell
     * @return 如果 Grid 坐标不同，返回 true；否则返回 false
     *
     * 用于检测对象是否跨越了 Grid 边界。跨越 Grid 是一个重要事件：
     * - 可能需要加载新的 Grid 数据
     * - 可能需要卸载远离的 Grid
     * - 可能影响对象的视野范围和消息广播
     *
     * @note 只比较 Grid 坐标，不比较 Cell 坐标
     */
    bool DiffGrid(Cell const& cell) const
    {
        return(data.Part.grid_x != cell.data.Part.grid_x ||
            data.Part.grid_y != cell.data.Part.grid_y);
    }

    /**
     * @brief 获取 Cell 在 Grid 内的 X 坐标
     * @return Cell X 坐标（0 到 MAX_NUMBER_OF_CELLS-1）
     */
    uint32 CellX() const { return data.Part.cell_x; }

    /**
     * @brief 获取 Cell 在 Grid 内的 Y 坐标
     * @return Cell Y 坐标（0 到 MAX_NUMBER_OF_CELLS-1）
     */
    uint32 CellY() const { return data.Part.cell_y; }

    /**
     * @brief 获取 Grid 在地图中的 X 坐标
     * @return Grid X 坐标（0 到 MAX_NUMBER_OF_GRIDS-1）
     */
    uint32 GridX() const { return data.Part.grid_x; }

    /**
     * @brief 获取 Grid 在地图中的 Y 坐标
     * @return Grid Y 坐标（0 到 MAX_NUMBER_OF_GRIDS-1）
     */
    uint32 GridY() const { return data.Part.grid_y; }

    /**
     * @brief 检查是否禁止创建该 Cell
     * @return 如果设置了 nocreate 标志，返回 true
     *
     * nocreate 标志用于控制动态对象创建行为：
     * - 某些场景下需要查询 Cell 但不触发创建（例如预检查）
     * - 防止在不应存在的区域创建对象
     * - 优化性能，避免不必要的对象实例化
     */
    bool NoCreate() const { return data.Part.nocreate; }

    /**
     * @brief 设置禁止创建标志
     *
     * 标记该 Cell 为"不可创建"状态，后续访问该 Cell 时应该遵循此标志。
     * 通常在查询操作前设置，用于区分"查询但不创建"和"查询并创建"的场景。
     */
    void SetNoCreate() { data.Part.nocreate = 1; }

    /**
     * @brief 获取该 Cell 的全局单元坐标
     * @return CellCoord 对象，包含全局唯一的单元标识
     *
     * 将 Grid 坐标和 Cell 坐标编码为一个一维坐标。
     * 这个坐标在整个地图范围内唯一标识一个 Cell。
     *
     * 计算公式：CellCoord = GridCoord * MAX_NUMBER_OF_CELLS + CellCoord
     *
     * @note 返回的 CellCoord 可用于数组索引或跨 Grid 的坐标计算
     */
    CellCoord GetCellCoord() const
    {
        return CellCoord(
            data.Part.grid_x * MAX_NUMBER_OF_CELLS+data.Part.cell_x,
            data.Part.grid_y * MAX_NUMBER_OF_CELLS+data.Part.cell_y);
    }

    /**
     * @brief 比较两个 Cell 是否完全相同
     * @param cell 要比较的另一个 Cell
     * @return 如果所有字段都相同，返回 true
     *
     * 直接比较整个 64 位数据，包括 Grid 坐标、Cell 坐标和 nocreate 标志。
     * 这是高效的位级比较，无需逐字段比较。
     */
    bool operator == (Cell const& cell) const { return (data.All == cell.data.All); }

    /**
     * @brief Cell 数据的联合体
     *
     * 使用联合体实现两种访问方式：
     * 1. Part：按字段访问，用于读取和修改特定坐标
     * 2. All：整体访问，用于快速比较和赋值
     *
     * 这种设计兼顾了灵活性和性能。
     */
    union
    {
        /**
         * @brief 分字段访问结构
         */
        struct
        {
            uint8 grid_x;     ///< Grid 在地图中的 X 坐标（0 到 MAX_NUMBER_OF_GRIDS-1）
            uint8 grid_y;     ///< Grid 在地图中的 Y 坐标（0 到 MAX_NUMBER_OF_GRIDS-1）
            uint8 cell_x;     ///< Cell 在 Grid 内的 X 坐标（0 到 MAX_NUMBER_OF_CELLS-1）
            uint8 cell_y;     ///< Cell 在 Grid 内的 Y 坐标（0 到 MAX_NUMBER_OF_CELLS-1）
            uint8 nocreate;   ///< 禁止创建标志：非零表示禁止创建该 Cell 的对象
        } Part;
        uint64 All;           ///< 完整的 64 位数据，用于快速比较和复制
    } data;

    /**
     * @brief 访问指定中心点和半径范围内的所有对象（基于 WorldObject）
     * @tparam T 访问者类型，必须实现 Visit 方法和 getVisitor 方法
     * @tparam CONTAINER 容器类型，通常是 TypeMapContainer
     * @param center 中心单元坐标
     * @param visitor 访问者对象，用于处理找到的对象
     * @param map 当前地图实例
     * @param obj 中心 WorldObject，用于确定搜索中心和可见性检查
     * @param radius 搜索半径（游戏单位）
     *
     * 这是最常用的对象查询方法，例如：
     * - 施法时查找范围内的目标
     * - AI 感知周围的敌人或友军
     * - 查找附近的可交互对象
     *
     * 实现流程：
     * 1. 根据半径计算需要访问的 CellArea
     * 2. 遍历范围内的所有 Cell
     * 3. 对每个 Cell 中的对象调用 visitor 进行处理
     * 4. 对于圆形区域，还需要额外过滤距离超过半径的对象
     *
     * @note 性能考虑：radius 越大，需要访问的 Cell 越多，应避免过大的搜索半径
     * @see CalculateCellArea()
     */
    template<class T, class CONTAINER> void Visit(CellCoord const&, TypeContainerVisitor<T, CONTAINER>& visitor, Map&, WorldObject const& obj, float radius) const;

    /**
     * @brief 访问指定中心点和半径范围内的所有对象（基于坐标）
     * @tparam T 访问者类型
     * @tparam CONTAINER 容器类型
     * @param center 中心单元坐标
     * @param visitor 访问者对象
     * @param map 当前地图实例
     * @param x 中心点 X 坐标（世界坐标）
     * @param y 中心点 Y 坐标（世界坐标）
     * @param radius 搜索半径（游戏单位）
     *
     * 与基于 WorldObject 的版本类似，但不依赖特定对象作为中心。
     * 适用于需要在任意位置进行查询的场景，例如：
     * - 区域效果法术的中心点
     * - 触发器或游戏对象的感知范围
     * - 地图区域检查
     *
     * @note 由于没有 WorldObject，无法进行可见性检查
     */
    template<class T, class CONTAINER> void Visit(CellCoord const&, TypeContainerVisitor<T, CONTAINER>& visitor, Map&, float x, float y, float radius) const;

    /**
     * @brief 计算覆盖指定圆形区域所需的所有 Cell 范围
     * @param x 中心点 X 坐标（世界坐标）
     * @param y 中心点 Y 坐标（世界坐标）
     * @param radius 搜索半径（游戏单位）
     * @return CellArea 对象，包含所有需要访问的 Cell 的边界
     *
     * 将圆形区域转换为矩形的 Cell 范围，这是一种保守估计：
     * - 计算圆形的外接矩形
     * - 将矩形边界转换为 Cell 坐标
     * - 后续在访问时会进一步过滤超出半径的对象
     *
     * 性能优化：
     * - 返回的 CellArea 包含最小和最大坐标，避免遍历所有 Cell
     * - 使用整数运算加速坐标转换
     *
     * @note 返回的范围可能比实际圆形区域大，需要后续的距离检查
     */
    static CellArea CalculateCellArea(float x, float y, float radius);

    /**
     * @brief 访问指定 WorldObject 周围的 Grid 对象（玩家、尸体等）
     * @tparam T 访问者类型
     * @param obj 中心 WorldObject
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * Grid 对象类型包括：Player, Corpse, DynamicObject, SceneObject, Conversation 等
     * 这类对象通常需要跨 Grid 访问或特殊处理。
     *
     * @note dont_load=true 时，只访问已加载的 Grid，避免触发 Grid 加载
     */
    template<class T> static void VisitGridObjects(WorldObject const* obj, T& visitor, float radius, bool dont_load = true);

    /**
     * @brief 访问指定 WorldObject 周围的 World 对象（Creature、GameObject 等）
     * @tparam T 访问者类型
     * @param obj 中心 WorldObject
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * World 对象类型包括：Creature, GameObject, Item, DynamicObject 等
     * 这是最常用的对象类型，包括 NPC、怪物、可交互对象等。
     *
     * @note 这是游戏中绝大多数 AI 和交互逻辑使用的查询方法
     */
    template<class T> static void VisitWorldObjects(WorldObject const* obj, T& visitor, float radius, bool dont_load = true);

    /**
     * @brief 访问指定 WorldObject 周围的所有对象（Grid + World 对象）
     * @tparam T 访问者类型
     * @param obj 中心 WorldObject
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * 同时访问 Grid 对象和 World 对象，适用于需要处理所有可能对象的场景。
     * 性能开销最大，应谨慎使用。
     *
     * @warning 由于访问的对象类型最多，性能影响最大，应避免在高频调用中使用
     */
    template<class T> static void VisitAllObjects(WorldObject const* obj, T& visitor, float radius, bool dont_load = true);

    /**
     * @brief 访问指定坐标周围的 Grid 对象（玩家、尸体等）
     * @tparam T 访问者类型
     * @param x 中心点 X 坐标（世界坐标）
     * @param y 中心点 Y 坐标（世界坐标）
     * @param map 目标地图实例
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * 基于坐标版本的 Grid 对象查询，不依赖特定 WorldObject。
     */
    template<class T> static void VisitGridObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load = true);

    /**
     * @brief 访问指定坐标周围的 World 对象（Creature、GameObject 等）
     * @tparam T 访问者类型
     * @param x 中心点 X 坐标（世界坐标）
     * @param y 中心点 Y 坐标（世界坐标）
     * @param map 目标地图实例
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * 基于坐标版本的 World 对象查询，不依赖特定 WorldObject。
     */
    template<class T> static void VisitWorldObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load = true);

    /**
     * @brief 访问指定坐标周围的所有对象（Grid + World 对象）
     * @tparam T 访问者类型
     * @param x 中心点 X 坐标（世界坐标）
     * @param y 中心点 Y 坐标（世界坐标）
     * @param map 目标地图实例
     * @param visitor 访问者对象
     * @param radius 搜索半径
     * @param dont_load 是否禁止加载未激活的 Grid，默认为 true
     *
     * 基于坐标版本的全部对象查询，不依赖特定 WorldObject。
     */
    template<class T> static void VisitAllObjects(float x, float y, Map* map, T& visitor, float radius, bool dont_load = true);

private:
    /**
     * @brief 访问圆形区域内的对象（内部实现）
     * @tparam T 访问者类型
     * @tparam CONTAINER 容器类型
     * @param visitor 访问者对象
     * @param map 当前地图实例
     * @param lower_bound 圆形区域的左下角 Cell 坐标
     * @param upper_bound 圆形区域的右上角 Cell 坐标
     *
     * 这是 Visit 方法的核心实现，负责遍历圆形区域内的所有 Cell 并应用访问者。
     * 只被同类的 Visit 方法内部调用，不对外暴露。
     *
     * 实现细节：
     * 1. 遍历从 lower_bound 到 upper_bound 的所有 Cell
     * 2. 对每个 Cell 进行距离检查（是否在圆内）
     * 3. 如果在范围内，调用 visitor 处理该 Cell 中的对象
     * 4. 使用 Bresenham 圆算法优化圆形边界的遍历
     *
     * @note 私有方法，外部代码不应直接调用
     */
    template<class T, class CONTAINER> void VisitCircle(TypeContainerVisitor<T, CONTAINER> &, Map &, CellCoord const&, CellCoord const&) const;
};

#endif
