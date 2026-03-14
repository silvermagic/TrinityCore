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
 * @file ModelIgnoreFlags.h
 * @brief 模型碰撞检测忽略标志位定义
 *
 * 本文件定义了碰撞检测系统中用于控制模型忽略行为的标志位枚举。
 * 在射线碰撞检测和视距计算中，可以通过这些标志位来选择性忽略特定类型的模型。
 *
 * 主要用途：
 * - 视距(Line of Sight)计算时忽略M2模型
 * - 射线碰撞检测时过滤特定模型类型
 * - 优化碰撞检测性能，避免不必要的计算
 */

#ifndef ModelIgnoreFlags_h__
#define ModelIgnoreFlags_h__

#include "Define.h"

namespace VMAP
{
/**
 * @enum ModelIgnoreFlags
 * @brief 模型忽略标志位枚举
 *
 * 定义在碰撞检测过程中需要忽略的模型类型。
 * 这是一个位掩码枚举，可以进行位运算组合。
 *
 * 使用场景：
 * - 视距检测时，可能需要忽略M2模型（如树木、装饰物）
 * - 某些技能效果计算时需要排除特定模型类型
 */
enum class ModelIgnoreFlags : uint32
{
    Nothing = 0x00,  ///< 不忽略任何模型，进行完整的碰撞检测
    M2      = 0x01   ///< 忽略M2模型（游戏对象模型，如树木、岩石等装饰物）
                      ///< M2模型通常用于游戏对象，与WMO（世界模型对象）相对
};

/**
 * @brief 模型忽略标志位的位与运算符重载
 * @param left 左操作数
 * @param right 右操作数
 * @return 位与运算结果
 *
 * 允许使用 & 运算符检查是否设置了特定的忽略标志。
 *
 * 使用示例：
 * @code
 * if ((ignoreFlags & ModelIgnoreFlags::M2) != ModelIgnoreFlags::Nothing)
 * {
 *     // 需要忽略M2模型
 * }
 * @endcode
 */
inline ModelIgnoreFlags operator&(ModelIgnoreFlags left, ModelIgnoreFlags right) { return ModelIgnoreFlags(uint32(left) & uint32(right)); }
}

#endif // ModelIgnoreFlags_h__
