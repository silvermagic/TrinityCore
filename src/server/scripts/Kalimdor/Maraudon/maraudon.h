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
 * @file maraudon.h
 * @brief 玛拉顿副本通用定义头文件
 *
 * 该头文件提供了玛拉顿副本脚本所需的通用定义和工具函数。
 *
 * 主要功能:
 * - 定义玛拉顿实例脚本名称
 * - 提供获取玛拉顿AI实例的模板函数
 *
 * 玛拉顿是位于卡利姆多大陆的一个中级副本,
 * 适合等级30-40的玩家,包含多个BOSS和任务。
 */

#ifndef maraudon_h__
#define maraudon_h__

#include "CreatureAIImpl.h"

/**
 * @brief 玛拉顿实例脚本名称
 *
 * 该宏定义了玛拉顿实例脚本的名称,用于在系统中标识和查找脚本
 */
#define MaraudonScriptName "instance_maraudon"

/**
 * @brief 获取玛拉顿AI实例的模板函数
 * @tparam AI AI类型
 * @tparam T 对象类型(通常是Creature或GameObject)
 * @param obj 需要获取AI的对象指针
 * @return 返回指定类型的AI实例指针
 *
 * 该模板函数封装了 GetInstanceAI 调用,简化了AI获取流程。
 * 如果当前地图是玛拉顿实例,返回实际的AI实例;
 * 否则返回nullptr。
 *
 * @note 该函数通常用于BOSS脚本中获取AI实例
 *
 * @example
 * @code
 * CreatureAI* GetAI(Creature* creature) const override
 * {
 *     return GetMaraudonAI<boss_celebras_the_cursedAI>(creature);
 * }
 * @endcode
 */
template <class AI, class T>
inline AI* GetMaraudonAI(T* obj)
{
    return GetInstanceAI<AI>(obj, MaraudonScriptName);
}

#endif // maraudon_h__
