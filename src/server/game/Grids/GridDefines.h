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
 * @file GridDefines.h
 * @brief 网格系统核心定义文件
 *
 * 本文件定义了TrinityCore地图网格系统的核心常量、类型和坐标计算函数。
 * 网格系统是游戏世界空间划分的基础架构，用于高效管理游戏对象的空间分布。
 *
 * 主要功能:
 * - 定义网格和单元格的尺寸常量
 * - 定义对象类型列表和引用管理器类型
 * - 提供坐标转换和验证工具函数
 *
 * 网格系统层级结构:
 * - 地图(Map): 64x64个网格(NGrid)
 * - 网格(NGrid): 8x8个单元格(Cell)
 * - 单元格(Cell): 存储具体游戏对象的容器
 *
 * 坐标系统:
 * - 世界坐标: 以地图中心为原点的浮点坐标(单位:码)
 * - 网格坐标: 0-63的整数坐标
 * - 单元格坐标: 0-511的整数坐标
 */

#ifndef TRINITY_GRIDDEFINES_H
#define TRINITY_GRIDDEFINES_H

#include "Common.h"
#include "ObjectGuid.h"
#include "NGrid.h"
#include <cmath>

// 前置类声明
class Corpse;
class Creature;
class DynamicObject;
class GameObject;
class Pet;
class Player;

// 每个网格中的最大单元格数量（每个网格划分为 8x8 个单元格）
#define MAX_NUMBER_OF_CELLS     8

// 地图中的最大网格数量（地图划分为 64x64 个网格）
#define MAX_NUMBER_OF_GRIDS      64

// 单个网格的尺寸（游戏单位，约533.3333码）
#define SIZE_OF_GRIDS            533.3333f
// 中心网格ID（32，即64/2，用于坐标转换）
#define CENTER_GRID_ID           (MAX_NUMBER_OF_GRIDS/2)

// 中心网格偏移量（266.66665，即网格尺寸的一半）
#define CENTER_GRID_OFFSET      (SIZE_OF_GRIDS/2)

// 网格最小更新延迟（1分钟，用于网格加载/卸载决策）
#define MIN_GRID_DELAY          (MINUTE*IN_MILLISECONDS)
// 地图最小更新延迟（1毫秒）
#define MIN_MAP_UPDATE_DELAY    1

// 单个单元格的尺寸（约66.6666码，即网格尺寸/单元格数量）
#define SIZE_OF_GRID_CELL       (SIZE_OF_GRIDS/MAX_NUMBER_OF_CELLS)

// 中心单元格ID（256，即8*64/2，用于单元格坐标转换）
#define CENTER_GRID_CELL_ID     (MAX_NUMBER_OF_CELLS*MAX_NUMBER_OF_GRIDS/2)
// 中心单元格偏移量（约33.3333码，即单元格尺寸的一半）
#define CENTER_GRID_CELL_OFFSET (SIZE_OF_GRID_CELL/2)

// 每个地图的总单元格数量（512，即64个网格 * 8个单元格）
#define TOTAL_NUMBER_OF_CELLS_PER_MAP    (MAX_NUMBER_OF_GRIDS*MAX_NUMBER_OF_CELLS)

// 地图分辨率（用于高度图等数据）
#define MAP_RESOLUTION 128

// 地图总尺寸（34133.33码，即533.3333 * 64）
#define MAP_SIZE                (SIZE_OF_GRIDS*MAX_NUMBER_OF_GRIDS)
// 地图半尺寸（17066.665码，用于坐标范围验证）
#define MAP_HALFSIZE            (MAP_SIZE/2)

// 世界对象类型列表：包含玩家、生物(宠物)、尸体(可复活)、动态对象(远视目标)
// 使用Creature代替Pet以简化*::Visit模板（避免为Creature->Pet情况编写重复代码）
typedef TYPELIST_4(Player, Creature/*pets*/, Corpse/*resurrectable*/, DynamicObject/*farsight target*/) AllWorldObjectTypes;
// 网格对象类型列表：包含游戏对象、生物(除宠物外)、动态对象、尸体(骨骼)
typedef TYPELIST_4(GameObject, Creature/*except pets*/, DynamicObject, Corpse/*Bones*/) AllGridObjectTypes;
// 地图存储对象类型列表：包含生物、游戏对象、动态对象、宠物、尸体
typedef TYPELIST_5(Creature, GameObject, DynamicObject, Pet, Corpse) AllMapStoredObjectTypes;

// 网格引用管理器类型定义（用于管理网格中的各种对象）
typedef GridRefManager<Corpse>          CorpseMapType;           // 尸体管理器
typedef GridRefManager<Creature>        CreatureMapType;         // 生物管理器
typedef GridRefManager<DynamicObject>   DynamicObjectMapType;    // 动态对象管理器
typedef GridRefManager<GameObject>      GameObjectMapType;       // 游戏对象管理器
typedef GridRefManager<Player>          PlayerMapType;           // 玩家管理器

// 网格地图类型掩码枚举（用于标识不同类型的对象）
enum GridMapTypeMask
{
    GRID_MAP_TYPE_MASK_CORPSE           = 0x01,  // 尸体掩码
    GRID_MAP_TYPE_MASK_CREATURE         = 0x02,  // 生物掩码
    GRID_MAP_TYPE_MASK_DYNAMICOBJECT    = 0x04,  // 动态对象掩码
    GRID_MAP_TYPE_MASK_GAMEOBJECT       = 0x08,  // 游戏对象掩码
    GRID_MAP_TYPE_MASK_PLAYER           = 0x10,  // 玩家掩码
    GRID_MAP_TYPE_MASK_ALL              = 0x1F   // 所有对象掩码（0x01|0x02|0x04|0x08|0x10）
};

// 网格模板类的外部声明（在cpp文件中实例化）
extern template class Grid<Player, AllWorldObjectTypes, AllGridObjectTypes>;
extern template class NGrid<MAX_NUMBER_OF_CELLS, Player, AllWorldObjectTypes, AllGridObjectTypes>;

// 类型映射容器的外部声明
extern template class TypeMapContainer<AllGridObjectTypes>;
extern template class TypeMapContainer<AllWorldObjectTypes>;

// 网格类型定义（Grid是单个网格单元，包含世界对象和网格对象）
typedef Grid<Player, AllWorldObjectTypes, AllGridObjectTypes> GridType;
// NGrid类型定义（NGrid是网格的集合，包含MAX_NUMBER_OF_CELLS个单元格）
typedef NGrid<MAX_NUMBER_OF_CELLS, Player, AllWorldObjectTypes, AllGridObjectTypes> NGridType;

// 类型映射容器类型定义
typedef TypeMapContainer<AllGridObjectTypes> GridTypeMapContainer;    // 网格对象容器
typedef TypeMapContainer<AllWorldObjectTypes> WorldTypeMapContainer;  // 世界对象容器

// 坐标对模板结构体，用于表示二维坐标
// LIMIT参数用于限制坐标的最大值
template<uint32 LIMIT>
struct CoordPair
{
    // 默认构造函数，初始化x和y坐标为0
    CoordPair(uint32 x=0, uint32 y=0)
        : x_coord(x), y_coord(y)
    { }

    // 拷贝构造函数
    CoordPair(const CoordPair<LIMIT> &obj)
        : x_coord(obj.x_coord), y_coord(obj.y_coord)
    { }

    // 赋值运算符重载
    CoordPair<LIMIT> & operator=(const CoordPair<LIMIT> &obj)
    {
        x_coord = obj.x_coord;
        y_coord = obj.y_coord;
        return *this;
    }

    // X坐标减法，确保不会下溢出小于0
    void dec_x(uint32 val)
    {
        if (x_coord > val)
            x_coord -= val;
        else
            x_coord = 0;
    }

    // X坐标加法，确保不会上溢出超过LIMIT
    void inc_x(uint32 val)
    {
        if (x_coord + val < LIMIT)
            x_coord += val;
        else
            x_coord = LIMIT - 1;
    }

    // Y坐标减法，确保不会下溢出小于0
    void dec_y(uint32 val)
    {
        if (y_coord > val)
            y_coord -= val;
        else
            y_coord = 0;
    }

    // Y坐标加法，确保不会上溢出超过LIMIT
    void inc_y(uint32 val)
    {
        if (y_coord + val < LIMIT)
            y_coord += val;
        else
            y_coord = LIMIT - 1;
    }

    // 检查坐标是否有效（在LIMIT范围内）
    bool IsCoordValid() const
    {
        return x_coord < LIMIT && y_coord < LIMIT;
    }

    // 将坐标规范化到有效范围内
    CoordPair& normalize()
    {
        x_coord = std::min(x_coord, LIMIT - 1);
        y_coord = std::min(y_coord, LIMIT - 1);
        return *this;
    }

    // 获取唯一ID（将二维坐标转换为一维ID）
    uint32 GetId() const
    {
        return y_coord * LIMIT + x_coord;
    }

    // 相等比较运算符（C++20默认实现）
    friend bool operator==(CoordPair const& p1, CoordPair const& p2) = default;

    uint32 x_coord;  // X坐标
    uint32 y_coord;  // Y坐标
};

// 网格坐标类型（LIMIT=64，表示网格坐标范围是0-63）
typedef CoordPair<MAX_NUMBER_OF_GRIDS> GridCoord;
// 单元格坐标类型（LIMIT=512，表示单元格坐标范围是0-511）
typedef CoordPair<TOTAL_NUMBER_OF_CELLS_PER_MAP> CellCoord;

namespace Trinity
{
    // 通用坐标计算模板函数
    // RET_TYPE: 返回的坐标类型（GridCoord或CellCoord）
    // CENTER_VAL: 中心坐标值（用于坐标转换）
    // x, y: 游戏世界坐标
    // center_offset: 中心偏移量
    // size: 单元尺寸（网格尺寸或单元格尺寸）
    template<class RET_TYPE, int CENTER_VAL>
    inline RET_TYPE Compute(float x, float y, float center_offset, float size)
    {
        // 使用double格式计算和存储临时值，以确保与MySQL计算结果一致
        double x_offset = (double(x) - center_offset)/size;
        double y_offset = (double(y) - center_offset)/size;

        // 加上0.5进行四舍五入
        int x_val = int(x_offset + CENTER_VAL + 0.5);
        int y_val = int(y_offset + CENTER_VAL + 0.5);
        return RET_TYPE(x_val, y_val);
    }

    // 根据游戏世界坐标计算网格坐标
    // x, y: 游戏世界坐标（码）
    // 返回: GridCoord对象，表示网格坐标（0-63范围）
    inline GridCoord ComputeGridCoord(float x, float y)
    {
        return Compute<GridCoord, CENTER_GRID_ID>(x, y, CENTER_GRID_OFFSET, SIZE_OF_GRIDS);
    }

    // 简化版的网格坐标计算（用于特定场景）
    // 注意：此函数使用不同的计算方式，可能产生与ComputeGridCoord不同的结果
    inline GridCoord ComputeGridCoordSimple(float x, float y)
    {
        int gx = (int)(CENTER_GRID_ID - x / SIZE_OF_GRIDS);
        int gy = (int)(CENTER_GRID_ID - y / SIZE_OF_GRIDS);
        return GridCoord((MAX_NUMBER_OF_GRIDS - 1) - gx, (MAX_NUMBER_OF_GRIDS - 1) - gy);
    }

    // 根据游戏世界坐标计算单元格坐标
    // x, y: 游戏世界坐标（码）
    // 返回: CellCoord对象，表示单元格坐标（0-511范围）
    inline CellCoord ComputeCellCoord(float x, float y)
    {
        return Compute<CellCoord, CENTER_GRID_CELL_ID>(x, y, CENTER_GRID_CELL_OFFSET, SIZE_OF_GRID_CELL);
    }

    // 根据游戏世界坐标计算单元格坐标，并返回单元格内偏移量
    // x, y: 游戏世界坐标（码）
    // x_off, y_off: 输出参数，返回在单元格内的偏移量
    // 返回: CellCoord对象，表示单元格坐标
    inline CellCoord ComputeCellCoord(float x, float y, float &x_off, float &y_off)
    {
        double x_offset = (double(x) - CENTER_GRID_CELL_OFFSET)/SIZE_OF_GRID_CELL;
        double y_offset = (double(y) - CENTER_GRID_CELL_OFFSET)/SIZE_OF_GRID_CELL;

        int x_val = int(x_offset + CENTER_GRID_CELL_ID + 0.5f);
        int y_val = int(y_offset + CENTER_GRID_CELL_ID + 0.5f);
        // 计算在单元格内的偏移量
        x_off = (float(x_offset) - float(x_val) + CENTER_GRID_CELL_ID) * SIZE_OF_GRID_CELL;
        y_off = (float(y_offset) - float(y_val) + CENTER_GRID_CELL_ID) * SIZE_OF_GRID_CELL;
        return CellCoord(x_val, y_val);
    }

    // 规范化地图坐标，确保坐标在有效范围内
    // c: 要规范化的坐标（输入/输出参数）
    inline void NormalizeMapCoord(float &c)
    {
        if (c > MAP_HALFSIZE - 0.5f)
            c = MAP_HALFSIZE - 0.5f;
        else if (c < -(MAP_HALFSIZE - 0.5f))
            c = -(MAP_HALFSIZE - 0.5f);
    }

    // 检查单个地图坐标是否有效
    // c: 要检查的坐标
    // 返回: 如果坐标有效返回true，否则返回false
    inline bool IsValidMapCoord(float c)
    {
        return std::isfinite(c) && (std::fabs(c) <= MAP_HALFSIZE - 0.5f);
    }

    // 检查二维地图坐标是否有效
    inline bool IsValidMapCoord(float x, float y)
    {
        return IsValidMapCoord(x) && IsValidMapCoord(y);
    }

    // 检查三维地图坐标是否有效
    inline bool IsValidMapCoord(float x, float y, float z)
    {
        return IsValidMapCoord(x, y) && IsValidMapCoord(z);
    }

    // 检查四维地图坐标是否有效（x, y, z坐标 + 朝向）
    inline bool IsValidMapCoord(float x, float y, float z, float o)
    {
        return IsValidMapCoord(x, y, z) && std::isfinite(o);
    }
}
#endif
