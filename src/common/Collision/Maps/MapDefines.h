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
 * @file MapDefines.h
 * @brief 地图导航网格定义模块
 *
 * 本模块定义了移动地图(MMap)系统的核心数据结构和常量:
 * - MMap文件头格式
 * - 导航区域类型定义
 * - 导航地形标志位
 *
 * MMap系统用于NPC和玩家的寻路功能，基于Recast/Detour导航网格技术。
 */

#ifndef _MAPDEFINES_H
#define _MAPDEFINES_H

#include "Define.h"
#include "DetourNavMesh.h"

/// MMap文件魔数，用于标识MMap文件格式 ('MMAP'的十六进制表示)
const uint32 MMAP_MAGIC = 0x4d4d4150;

/// MMap文件格式版本号，当格式变更时需要更新
#define MMAP_VERSION 15

/**
 * @struct MmapTileHeader
 * @brief MMap瓦片文件头结构
 *
 * 存储在.mmtile文件开头，包含文件格式验证和元数据信息。
 * 所有字段都必须正确初始化，确保mmaps_generator能生成二进制一致的文件。
 */
struct MmapTileHeader
{
    uint32 mmapMagic;       ///< 文件魔数，用于验证文件格式
    uint32 dtVersion;       ///< Detour导航网格版本
    uint32 mmapVersion;     ///< MMap版本号
    uint32 size;            ///< 数据大小
    char usesLiquids;       ///< 是否使用液体（水体）导航
    char padding[3];        ///< 填充字节，确保结构体对齐

    /**
     * @brief 默认构造函数
     *
     * 初始化所有字段为默认值：
     * - 魔数设为MMAP_MAGIC
     * - Detour版本设为当前版本
     * - MMap版本设为当前版本
     * - 大小初始化为0
     * - 默认启用液体导航
     */
    MmapTileHeader() : mmapMagic(MMAP_MAGIC), dtVersion(DT_NAVMESH_VERSION),
        mmapVersion(MMAP_VERSION), size(0), usesLiquids(true), padding() { }
};

// 静态断言：确保MmapTileHeader结构体大小正确（20字节）
static_assert(sizeof(MmapTileHeader) == 20, "MmapTileHeader size is not correct, adjust the padding field size");
// 静态断言：确保所有字段都已初始化，没有未初始化的填充字段
static_assert(sizeof(MmapTileHeader) == (sizeof(MmapTileHeader::mmapMagic) +
                                         sizeof(MmapTileHeader::dtVersion) +
                                         sizeof(MmapTileHeader::mmapVersion) +
                                         sizeof(MmapTileHeader::size) +
                                         sizeof(MmapTileHeader::usesLiquids) +
                                         sizeof(MmapTileHeader::padding)), "MmapTileHeader has uninitialized padding fields");

/**
 * @enum NavArea
 * @brief 导航区域类型枚举
 *
 * 定义了导航网格中不同区域的类型。
 * 数值越大，优先级越高。当表面非常接近时，Recast会选择高优先级的区域。
 * 地面(GROUND)设为最高值，以确保浅水区域会优先选择地面而不是水面。
 */
enum NavArea
{
    NAV_AREA_EMPTY          = 0,    ///< 空区域（不可通行）
    // 区域1-60预留给可破坏区域（目前在vmaps中跳过，标志为1的WMO）
    // 地面设为最高值，使recast在合并非常接近的表面时优先选择地面而不是水（浅水应该可行走）
    NAV_AREA_GROUND         = 11,   ///< 普通地面（可通行）
    NAV_AREA_GROUND_STEEP   = 10,   ///< 陡峭地面（可能滑落）
    NAV_AREA_WATER          = 9,    ///< 水域（可游泳）
    NAV_AREA_MAGMA_SLIME    = 8,    ///< 岩浆/粘液区域（不可通行或造成伤害）
    NAV_AREA_MAX_VALUE      = NAV_AREA_GROUND,  ///< 最大区域值
    NAV_AREA_MIN_VALUE      = NAV_AREA_MAGMA_SLIME,  ///< 最小区域值
    NAV_AREA_ALL_MASK       = 0x3F  ///< 区域掩码（最大允许值，6位）
};

/**
 * @enum NavTerrainFlag
 * @brief 导航地形标志位
 *
 * 用于快速判断导航区域的类型。
 * 标志位基于NavArea值计算，用于位运算优化。
 */
enum NavTerrainFlag
{
    NAV_EMPTY        = 0x00,  ///< 空地形标志
    NAV_GROUND       = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_GROUND),       ///< 地形标志：普通地面
    NAV_GROUND_STEEP = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_GROUND_STEEP), ///< 地形标志：陡峭地面
    NAV_WATER        = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_WATER),        ///< 地形标志：水域
    NAV_MAGMA_SLIME  = 1 << (NAV_AREA_MAX_VALUE - NAV_AREA_MAGMA_SLIME)   ///< 地形标志：岩浆/粘液
};

#endif /* _MAPDEFINES_H */
