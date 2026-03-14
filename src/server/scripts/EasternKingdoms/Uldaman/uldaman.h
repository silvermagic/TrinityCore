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
 * @file uldaman.h
 * @brief Uldaman 副本公共定义头文件
 *
 * 本头文件定义了 Uldaman 副本脚本模块共用的数据类型、游戏对象ID和工具宏。
 * 所有 Uldaman 相关脚本都应包含此文件以访问统一的定义。
 */

#ifndef DEF_ULDAMAN_H
#define DEF_ULDAMAN_H

#include "CreatureAIImpl.h"

/// Uldaman 实例脚本名称
#define UldamanScriptName "instance_uldaman"

/// 存档数据头标识
#define DataHeader "UD"

/// 最大首领战斗数量（守护者祭坛门、古代宝库门、艾隆纳亚门）
#define MAX_ENCOUNTER                   3

/**
 * @brief Uldaman 数据类型枚举
 *
 * 定义实例脚本中使用的数据标识符，用于 SetData/GetData 调用。
 */
enum UDDataTypes
{
    DATA_ALTAR_DOORS                    = 1,  ///< 守护者祭坛门状态（石像守卫全部击杀后开启）
    DATA_ANCIENT_DOOR                   = 2,  ///< 古代宝库门状态（阿扎达斯死亡后开启）
    DATA_IRONAYA_DOOR                   = 3,  ///< 艾隆纳亚密封门状态（基石激活后开启）
    DATA_STONE_KEEPERS                  = 4,  ///< 石像守卫激活触发
    DATA_MINIONS                        = 5,  ///< 阿扎达斯小怪状态管理
    DATA_IRONAYA_SEAL                   = 6,  ///< 艾隆纳亚密封门激活触发（基石交互）
};

/**
 * @brief Uldaman 游戏对象ID枚举
 *
 * 定义副本中重要的游戏对象ID，用于门和机关的控制。
 */
enum UDGameObjectIds
{
    GO_ARCHAEDAS_TEMPLE_DOOR            = 141869,  ///< 阿扎达斯神殿门 - 石像守卫区域入口
    GO_ALTAR_OF_THE_KEEPER_TEMPLE_DOOR  = 124367,  ///< 守护者祭坛神殿门 - 守护者祭坛区域入口
    GO_ANCIENT_VAULT_DOOR               = 124369,  ///< 古代宝库门 - 阿扎达斯死亡后开启
    GO_IRONAYA_SEAL_DOOR                = 124372,  ///< 艾隆纳亚密封门 - 基石激活后开启
    GO_KEYSTONE                         = 124371,  ///< 基石 - 用于激活艾隆纳亚密封门
};

/**
 * @brief 获取 Uldaman 实例AI的模板函数
 * @tparam AI AI类型
 * @tparam T 对象类型（Creature 或 GameObject）
 * @param obj 对象指针
 * @return 如果对象在 Uldaman 实例中则返回AI指针，否则返回nullptr
 *
 * 此函数封装了 GetInstanceAI 调用，自动使用 UldamanScriptName 作为脚本名称。
 */
template <class AI, class T>
inline AI* GetUldamanAI(T* obj)
{
    return GetInstanceAI<AI>(obj, UldamanScriptName);
}

/**
 * @brief 注册 Uldaman 生物AI的便捷宏
 * @param ai_name AI类名称
 *
 * 使用此宏可以简化生物AI的注册过程，自动关联到 Uldaman 实例。
 * 示例用法：
 * @code
 * RegisterUldamanCreatureAI(boss_ironaya);
 * @endcode
 */
#define RegisterUldamanCreatureAI(ai_name) RegisterCreatureAIWithFactory(ai_name, GetUldamanAI)

#endif
