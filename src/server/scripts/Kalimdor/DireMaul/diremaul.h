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
 * @file    diremaul.h
 * @brief   厄运之槌（Dire Maul）副本脚本公共头文件
 *
 * 本文件定义了厄运之槌副本所需的数据类型、枚举常量和辅助函数。
 * 厄运之槌是一个位于菲拉斯的高级副本，分为东、西、北三个区域。
 *
 * 主要内容：
 * - 数据类型标识符（水晶、力场等）
 * - NPC ID 枚举（Immol'thar、Prince Tortheldrin 等）
 * - 游戏对象 ID 枚举（水晶、力场等）
 * - AI 实例获取辅助函数
 */

#ifndef DEF_DIREMAUL_H
#define DEF_DIREMAUL_H

#include "CreatureAIImpl.h"

/// 厄运之槌实例脚本名称
#define DiremaulScriptName "instance_diremaul"

/// 厄运之槌数据头标识符，用于实例数据序列化
#define DataHeader "DM"

/**
 * @enum DMDataTypes
 * @brief 厄运之槌副本数据类型枚举
 *
 * 定义副本中各种Boss和机制的标识符，用于追踪副本进度和状态。
 * 编号延续Boss ID序列（0-16为各个Boss）。
 */
enum DMDataTypes
{
    DATA_CRYSTAL_01                     = 17,  ///< 第一块水晶 - 西区监狱
    DATA_CRYSTAL_02                     = 18,  ///< 第二块水晶 - 西区监狱
    DATA_CRYSTAL_03                     = 19,  ///< 第三块水晶 - 西区监狱
    DATA_CRYSTAL_04                     = 20,  ///< 第四块水晶 - 西区监狱
    DATA_CRYSTAL_05                     = 21,  ///< 第五块水晶 - 西区监狱
    DATA_FORCEFIELD                     = 22   ///< 力场监狱 - 禁锢Immol'thar的能量力场
};

/**
 * @enum DMCreatureIds
 * @brief 厄运之槌副本中的生物ID枚举
 *
 * 定义副本中关键NPC的Creature ID。
 */
enum DMCreatureIds
{
    NPC_IMMOLTHAR                       = 11496,  ///< Immol'thar（伊莫塔尔）- 西区最终Boss，被囚禁的恶魔
    NPC_TORTHELDRIN                     = 11486,  ///< Prince Tortheldrin（托塞德林王子）- 西区最终Boss，疯狂的精灵王子
    NPC_ARCANE_ABERRATION               = 11480,  ///< Arcane Aberration（奥术畸变体）- 守护水晶的怪物
    NPC_MANA_REMNANT                    = 11483   ///< Mana Remnant（法力残渣）- 守护水晶的怪物
};

/**
 * @enum DMGameobjectIds
 * @brief 厄运之槌副本中的游戏对象ID枚举
 *
 * 定义副本中关键游戏对象的ID，包括水晶和力场装置。
 */
enum DMGameobjectIds
{
    GO_FORCEFIELD                       = 179503,  ///< 力场监狱 - 禁锢Immol'thar的能量力场
    GO_CRYSTAL_01                       = 177259,  ///< 第一块水晶 - 需要击杀守护怪物后激活
    GO_CRYSTAL_02                       = 177257,  ///< 第二块水晶 - 需要击杀守护怪物后激活
    GO_CRYSTAL_03                       = 177258,  ///< 第三块水晶 - 需要击杀守护怪物后激活
    GO_CRYSTAL_04                       = 179504,  ///< 第四块水晶 - 需要击杀守护怪物后激活
    GO_CRYSTAL_05                       = 179505   ///< 第五块水晶 - 需要击杀守护怪物后激活
};

/**
 * @brief 获取厄运之槌实例AI指针的辅助函数模板
 *
 * 用于获取指定类型的实例AI指针，确保AI运行在正确的副本实例中。
 *
 * @tparam AI  AI类型，必须继承自InstanceScript
 * @tparam T   对象类型（通常是InstanceMap*）
 * @param obj  实例地图指针
 * @return AI* 返回指定类型的AI指针，如果脚本名称不匹配则返回nullptr
 *
 * @note 使用此函数可以确保AI只在正确的副本实例中创建和运行
 */
template <class AI, class T>
inline AI* GetDiremaulAI(T* obj)
{
    return GetInstanceAI<AI>(obj, DiremaulScriptName);
}

#endif
