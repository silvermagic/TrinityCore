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
 * @file scholomance.h
 * @brief 通灵学院副本通用头文件
 *
 * 本头文件定义了通灵学院副本脚本使用的通用数据：
 * - 副本脚本名称和数据头标识
 * - BOSS遭遇战数据类型枚举
 * - 生物ID枚举
 * - 游戏对象ID枚举
 * - AI获取辅助函数
 *
 * 通灵学院是位于西瘟疫之地的一个5人副本，
 * 包含8个BOSS遭遇战，其中黑暗院长甘德林是最终BOSS。
 */

#ifndef DEF_SCHOLOMANCE_H
#define DEF_SCHOLOMANCE_H

#include "CreatureAIImpl.h"

/**
 * @brief 副本脚本名称
 *
 * 用于注册和获取通灵学院副本实例脚本
 */
#define ScholomanceScriptName "instance_scholomance"

/**
 * @brief 存档数据头标识
 *
 * 用于验证副本存档数据的正确性
 */
#define DataHeader "SC"

/**
 * @brief BOSS遭遇战数量
 *
 * 通灵学院包含8个BOSS遭遇战：
 * 1. 塞欧克瑞拉斯博士
 * 2. 讲师玛丽希亚
 * 3. 伊露希亚·巴罗夫
 * 4. 阿莱克斯·巴罗夫领主
 * 5. 博学者波尔凯尔特
 * 6. 拉文尼亚
 * 7. 黑暗院长甘德林（最终BOSS）
 * 8. 基尔图诺斯（可选BOSS）
 */
uint32 const EncounterCount             = 8;

/**
 * @brief BOSS数据类型枚举
 *
 * 定义通灵学院副本中各个BOSS的数据索引
 * 这些索引用于：
 * - 获取和设置BOSS状态
 * - 在实例脚本中标识不同的BOSS
 * - 存档和读取BOSS状态
 */
enum SCDataTypes
{
    DATA_DOCTOR_THEOLEN_KRASTINOV       = 0,  // 塞欧克瑞拉斯博士 - 小BOSS之一
    DATA_INSTRUCTOR_MALICIA             = 1,  // 讲师玛丽希亚 - 小BOSS之一
    DATA_LADY_ILLUCIA_BAROV             = 2,  // 伊露希亚·巴罗夫 - 小BOSS之一
    DATA_LORD_ALEXEI_BAROV              = 3,  // 阿莱克斯·巴罗夫领主 - 小BOSS之一
    DATA_LOREKEEPER_POLKELT             = 4,  // 博学者波尔凯尔特 - 小BOSS之一
    DATA_THE_RAVENIAN                   = 5,  // 拉文尼亚 - 小BOSS之一
    DATA_DARKMASTER_GANDLING            = 6,  // 黑暗院长甘德林 - 最终BOSS
    DATA_KIRTONOS                       = 7   // 基尔图诺斯 - 可选BOSS（需要使用火盆召唤）
};

/**
 * @brief 生物ID枚举
 *
 * 定义通灵学院副本中使用的生物ID
 */
enum SCCreatureIds
{
    NPC_DARKMASTER_GANDLING             = 1853,  // 黑暗院长甘德林 - 最终BOSS
    NPC_BONE_MINION                     = 16119  // 骨骼仆从 - 甘德林战斗中召唤的小怪
};

/**
 * @brief 游戏对象ID枚举
 *
 * 定义通灵学院副本中使用的游戏对象ID
 * 这些大门在暗影传送门事件中会被控制开闭
 */
enum SCGameobjectIds
{
    GO_GATE_KIRTONOS                    = 175570,  // 基尔图诺斯大门 - 控制进入基尔图诺斯房间的门
    GO_GATE_GANDLING                    = 177374,  // 甘德林大门 - 控制进入主厅的门
    GO_GATE_RAVENIAN                    = 177372,  // 拉文尼亚大门 - 拉文尼亚密室的门
    GO_GATE_THEOLEN                     = 177377,  // 塞欧克瑞拉斯大门 - 塞欧克瑞拉斯密室的门
    GO_GATE_ILLUCIA                     = 177371,  // 伊露希亚大门 - 伊露希亚密室的门
    GO_GATE_MALICIA                     = 177375,  // 玛丽希亚大门 - 玛丽希亚密室的门
    GO_GATE_BAROV                       = 177373,  // 巴罗夫大门 - 巴罗夫密室的门
    GO_GATE_POLKELT                     = 177376,  // 波尔凯尔特大门 - 波尔凯尔特密室的门
    GO_BRAZIER_OF_THE_HERALD            = 175564   // 先兆火盆 - 用于召唤基尔图诺斯
};

/**
 * @brief 获取通灵学院AI辅助函数
 *
 * @tparam AI AI类型
 * @tparam T 对象类型（Creature或GameObject）
 * @param obj 生物或游戏对象指针
 * @return AI指针，如果不在通灵学院副本则返回nullptr
 *
 * 此函数封装了GetInstanceAI调用，自动使用ScholomanceScriptName
 * 作为实例脚本名称。用于在通灵学院副本中获取正确的AI对象。
 *
 * 使用示例：
 * @code
 * CreatureAI* GetAI(Creature* creature) const override
 * {
 *     return GetScholomanceAI<boss_vectusAI>(creature);
 * }
 * @endcode
 */
template <class AI, class T>
inline AI* GetScholomanceAI(T* obj)
{
    return GetInstanceAI<AI>(obj, ScholomanceScriptName);
}

#endif
