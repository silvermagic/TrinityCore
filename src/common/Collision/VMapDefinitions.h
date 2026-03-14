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
 * @file VMapDefinitions.h
 * @brief VMap（虚拟地图）定义文件
 *
 * 本文件定义了 VMap 系统使用的常量、魔数字符串和通用工具函数。
 * VMap 系统用于管理游戏世界的静态地形和建筑碰撞数据。
 *
 * 主要功能：
 * - 定义 VMap 文件格式的版本标识
 * - 定义游戏对象模型文件名称
 * - 提供数据块读取工具函数
 *
 * VMap 文件格式：
 * - 使用魔数字符串标识文件版本
 * - 分块存储地形和模型数据
 * - 支持快速的空间查询和碰撞检测
 */

#ifndef _VMAPDEFINITIONS_H
#define _VMAPDEFINITIONS_H

#include <cstdio>

/// 液体瓦片大小（每个瓦片的实际大小，单位：游戏单位）
/// 计算公式：地图块大小 / 液体网格数量 = 533.333 / 128
#define LIQUID_TILE_SIZE (533.333f / 128.f)

namespace VMAP
{
    /// VMap 文件魔数字符串，标识文件版本为 4.8
    const char VMAP_MAGIC[] = "VMAP_4.8";

    /// 原始 VMap 文件魔数字符串，用于提取的原始数据文件
    const char RAW_VMAP_MAGIC[] = "VMAP048";

    /// 游戏对象模型文件名，存储动态对象的碰撞树数据
    const char GAMEOBJECT_MODELS[] = "GameObjectModels.dtree";

    /**
     * @brief 读取并验证数据块
     *
     * 从文件中读取指定长度的数据块，并验证其魔数字符串是否匹配
     *
     * @param rf 文件指针（已打开）
     * @param dest 输出缓冲区，用于存储读取的数据
     * @param compare 期望的魔数字符串
     * @param len 读取长度
     * @return true 读取成功且魔数匹配
     * @return false 读取失败或魔数不匹配
     *
     * @note 该函数定义在 TileAssembler.cpp 中
     * @note 用于验证 VMap 文件的完整性和版本兼容性
     *
     * 使用示例：
     * @code
     * FILE* f = fopen("map.vmap", "rb");
     * char buffer[8];
     * if (!readChunk(f, buffer, VMAP_MAGIC, strlen(VMAP_MAGIC))) {
     *     // 文件版本不匹配或读取失败
     * }
     * @endcode
     */
    bool readChunk(FILE* rf, char *dest, const char *compare, uint32 len);
}

#endif
