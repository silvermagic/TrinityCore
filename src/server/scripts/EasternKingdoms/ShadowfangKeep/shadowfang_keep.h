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
 * @file shadowfang_keep.h
 * @brief 影牙城堡副本脚本公共头文件
 *
 * 该头文件定义影牙城堡副本脚本所需的公共数据类型和宏：
 * - 数据类型枚举：副本进度数据类型标识
 * - 生物枚举：副本内NPC和BOSS的ID
 * - 游戏对象枚举：副本内门和机关的ID
 * - 辅助函数模板：获取影牙城堡AI实例的工厂函数
 *
 * @note 所有影牙城堡相关脚本都应包含此头文件
 */

#ifndef DEF_SHADOWFANG_H
#define DEF_SHADOWFANG_H

#include "CreatureAIImpl.h"

/// 影牙城堡副本脚本名称，用于实例脚本注册和查询
#define SFKScriptName "instance_shadowfang_keep"

/// 数据头标识，用于副本存档数据的标识前缀
#define DataHeader "SK"

/**
 * @brief 影牙城堡数据类型枚举
 *
 * 定义副本进度数据类型标识符，用于追踪副本内各事件和BOSS的状态
 */
enum SKDataTypes
{
    TYPE_FREE_NPC               = 1,    ///< NPC自由事件类型 - 追踪Ash和Ada护送任务进度
    TYPE_RETHILGORE             = 2,    ///< Rethilgore事件类型 - 追踪第一个小BOSS状态
    TYPE_FENRUS                 = 3,    ///< Fenrus事件类型 - 追踪吞噬者芬鲁斯BOSS状态
    TYPE_NANDOS                 = 4,    ///< Nandos事件类型 - 追踪狼王南多斯BOSS状态
    BOSS_ARUGAL                 = 5,    ///< 阿鲁高BOSS类型 - 追踪大法师阿鲁高BOSS状态
    DATA_APOTHECARY_HUMMEL      = 6,    ///< 药剂师哈梅尔数据 - 情人节事件BOSS
    DATA_SPAWN_VALENTINE_ADDS   = 7     ///< 生成情人节小怪数据 - 情人节事件相关
};

/**
 * @brief 影牙城堡生物枚举
 *
 * 定义副本内所有特殊NPC和BOSS的生物ID
 */
enum SKCreatures
{
    NPC_ASH                             = 3850,  ///< Ash - 影牙城堡囚犯NPC，护送任务相关
    NPC_ADA                             = 3849,  ///< Ada - 影牙城堡囚犯NPC，护送任务相关
    NPC_ARCHMAGE_ARUGAL                 = 4275,  ///< 大法师阿鲁高 - 副本最终BOSS
    NPC_ARUGAL_VOIDWALKER               = 4627,  ///< 阿鲁高的虚空行者 - 芬鲁斯死后召唤的小怪
    NPC_DND_CRAZED_APOTHECARY_GENERATOR = 36212  ///< 疯狂药剂师生成器 - 情人节事件使用
};

/**
 * @brief 影牙城堡游戏对象枚举
 *
 * 定义副本内重要门和机关的游戏对象ID
 */
enum SKGameObjects
{
    GO_COURTYARD_DOOR   = 18895, ///< 庭院大门 - 与NPC对话后开启的门
    GO_SORCERER_DOOR    = 18972, ///< 巫师门 - 击杀吞噬者芬鲁斯后开启
    GO_ARUGAL_DOOR      = 18971  ///< 阿鲁高门 - 击杀狼王南多斯后开启
};

/**
 * @brief 获取影牙城堡AI实例的工厂函数模板
 *
 * 用于创建特定类型的AI实例，并自动绑定到影牙城堡副本实例
 *
 * @tparam AI AI类型，必须继承自CreatureAI
 * @tparam T 生物或游戏对象类型
 * @param obj 生物或游戏对象指针
 * @return AI* 返回绑定到影牙城堡实例的AI指针
 *
 * @note 该函数通过SFKScriptName确保AI只在该副本实例中工作
 */
template <class AI, class T>
inline AI* GetShadowfangKeepAI(T* obj)
{
    return GetInstanceAI<AI>(obj, SFKScriptName);
}

/**
 * @brief 注册影牙城堡生物AI的宏
 *
 * 简化生物AI的注册过程，自动使用GetShadowfangKeepAI工厂函数
 *
 * @param ai_name AI类名
 *
 * @example
 * RegisterShadowfangKeepCreatureAI(boss_archmage_arugalAI);
 */
#define RegisterShadowfangKeepCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetShadowfangKeepAI)

#endif
