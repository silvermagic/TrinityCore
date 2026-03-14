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
 * @file GameObjectAI.cpp
 *
 * @brief 游戏对象AI实现
 *
 * 本文件实现了游戏对象AI的基础类和空AI类。
 * 游戏对象AI是游戏世界中可交互对象的大脑，负责处理玩家交互、
 * 状态变化和各种事件回调。
 *
 * 主要功能：
 * - 提供游戏对象AI的基础接口实现
 * - 实现空游戏对象AI（NullGameObjectAI）作为默认AI
 */

#include "GameObjectAI.h"
#include "CreatureAI.h"

/**
 * @brief 检查游戏对象AI的适用性
 *
 * @param go 要检查的游戏对象（未使用）
 * @return int32 返回PERMIT_BASE_NO，表示基类AI不自动应用于任何游戏对象
 *
 * @note 基类GameObjectAI不应该被自动选择，子类应覆盖此函数
 *       并根据游戏对象的类型返回适当的权限值
 *
 * @see CreatureAI::Permissible
 */
int32 GameObjectAI::Permissible(GameObject const* /*go*/)
{
    return PERMIT_BASE_NO;
}

/**
 * @brief NullGameObjectAI构造函数
 *
 * @param go 要关联的游戏对象指针
 *
 * @details 调用父类GameObjectAI的构造函数，初始化游戏对象关联
 *
 * @note NullGameObjectAI是一个空实现，不执行任何AI逻辑
 *       适用于那些不需要任何特殊行为的游戏对象
 */
NullGameObjectAI::NullGameObjectAI(GameObject* go) : GameObjectAI(go) { }

/**
 * @brief 检查NullGameObjectAI的适用性
 *
 * @param go 要检查的游戏对象（未使用）
 * @return int32 返回PERMIT_BASE_IDLE，表示这是一个空闲/默认AI
 *
 * @details 当没有其他AI匹配时，NullGameObjectAI作为默认选择
 *          PERMIT_BASE_IDLE的值较低，确保只有在没有更好的AI选择时
 *          才会使用这个空AI
 *
 * @see CreatureAI::Permissible
 * @see PERMIT_BASE_IDLE
 */
int32 NullGameObjectAI::Permissible(GameObject const* /*go*/)
{
    return PERMIT_BASE_IDLE;
}
