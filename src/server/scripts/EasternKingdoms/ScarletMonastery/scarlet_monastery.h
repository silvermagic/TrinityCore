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
 * @file scarlet_monastery.h
 * @brief 血色修道院副本脚本公共头文件
 *
 * 本头文件定义了血色修道院副本脚本所需的公共数据结构、常量和宏：
 * - 副本数据类型枚举（BOSS、游戏对象、事件等）
 * - 生物ID枚举
 * - 游戏对象ID枚举
 * - 法术和事件ID枚举
 * - AI注册宏
 *
 * 血色修道院（Scarlet Monastery）简介：
 * 血色修道院是位于提瑞斯法林地的经典副本，分为四个独立区域：
 * 1. 墓地（Graveyard）- 无头骑士（万圣节节日BOSS）
 * 2. 大教堂（Cathedral）- 血色指挥官莫格莱尼和大检察官怀特迈恩
 * 3. 军械库（Armory）- 赫罗德（Herod）
 * 4. 图书馆（Library）- 审讯官维希斯、血法师萨尔诺斯、驯犬者洛克希、大检察官费尔班克斯
 *
 * 各脚本文件职责：
 * - instance_scarlet_monastery.cpp: 副本实例管理，无头骑士事件
 * - boss_interrogator_vishas.cpp: 审讯官维希斯BOSS战
 * - boss_mograine_and_whitemane.cpp: 莫格莱尼和怀特迈恩BOSS战
 * - boss_herod.cpp: 赫罗德BOSS战
 * - boss_arcanist_doan.cpp: 大法师多安BOSS战
 */

#ifndef SCARLET_M_
#define SCARLET_M_

#include "CreatureAIImpl.h"
#include "Position.h"

/**
 * @brief 副本脚本名称
 *
 * 用于注册和查找副本脚本实例
 */
#define SMScriptName "instance_scarlet_monastery"

/**
 * @brief 副本存档数据头
 *
 * 用于验证存档数据的有效性
 */
#define DataHeader "SM"

/**
 * @brief 遭遇战数量
 *
 * 定义副本中的BOSS遭遇战总数，用于副本脚本初始化
 */
uint32 const EncounterCount = 9;

/**
 * @brief 外部引用的位置常量
 *
 * 这些位置在 instance_scarlet_monastery.cpp 中定义，用于无头骑士事件
 */
extern Position const BunnySpawnPosition;                  // 火焰兔刷新位置
extern Position const EarthBunnySpawnPosition;             // 大地兔刷新位置
extern Position const HeadlessHorsemanSpawnPosition;       // 无头骑士刷新位置
extern Position const HeadlessHorsemanHeadSpawnPosition;   // 无头骑士头部刷新位置

/**
 * @brief 血色修道院数据类型枚举
 *
 * 定义副本中所有BOSS、游戏对象和事件的数据槽位ID
 * 这些ID用于在副本脚本中快速访问特定的游戏对象和生物
 */
enum SMDataTypes
{
    // 主要BOSS数据槽位（前7个对应DungeonEncounter.dbc）
    DATA_INTERROGATOR_VISHAS = 0,       // 审讯官维希斯 - 图书馆BOSS
    DATA_BLOODMAGE_THALNOS,             // 血法师萨尔诺斯 - 墓地BOSS
    DATA_HOUNDMASTER_LOKSEY,            // 驯犬者洛克希 - 图书馆BOSS
    DATA_ARCANIST_DOAN,                 // 大法师多安 - 图书馆最终BOSS
    DATA_HEROD,                         // 赫罗德 - 军械库最终BOSS
    DATA_HIGH_INQUISITOR_FAIRBANKS,     // 大检察官费尔班克斯 - 大教堂隐藏BOSS
    DATA_MOGRAINE_AND_WHITE_EVENT,      // 莫格莱尼和怀特迈恩 - 大教堂最终BOSS（最后一个DungeonEncounter.dbc条目）

    // 辅助生物数据槽位
    DATA_AZSHIR,                        // 阿兹希尔 - 稀有精英
    DATA_SCORN,                         // 斯科恩 - 稀有精英
    DATA_MOGRAINE,                      // 血色指挥官莫格莱尼
    DATA_VORREL,                        // 沃雷尔 - 被审讯官维希斯折磨致死的囚犯
    DATA_WHITEMANE,                     // 大检察官怀特迈恩

    // 无头骑士事件相关数据槽位
    DATA_HORSEMAN_HEAD,                 // 无头骑士头部
    DATA_HEADLESS_HORSEMAN,             // 无头骑士本体
    DATA_PUMPKIN_SHRINE,                // 南瓜神殿（万圣节期间召唤无头骑士）
    DATA_HIGH_INQUISITORS_DOOR,         // 高检察官大门（莫格莱尼战斗时控制）
    DATA_LOOSELY_TURNED_SOIL,            // 疏松的泥土（召唤无头骑士的另一种方式）
    DATA_START_HORSEMAN_EVENT,          // 启动无头骑士事件
    DATA_FLAME_BUNNY,                   // 火焰兔（无头骑士召唤特效）
    DATA_EARTH_BUNNY,                   // 大地兔（无头骑士召唤特效）
    DATA_HORSEMAN_EVENT_STATE,          // 无头骑士事件状态
    DATA_PREPARE_RESET,                 // 准备重置事件
    DATA_THOMAS                         // 托马斯爵士（无头骑士事件相关NPC）
};

/**
 * @brief 血色修道院生物ID枚举
 *
 * 定义副本中所有重要生物的NPC ID
 */
enum SMCreatureIds
{
    NPC_MOGRAINE               = 3976,   // 血色指挥官莫格莱尼 - 大教堂BOSS
    NPC_WHITEMANE              = 3977,   // 大检察官怀特迈恩 - 大教堂BOSS
    NPC_VORREL                 = 3981,   // 沃雷尔 - 图书馆囚犯
    NPC_HEADLESS_HORSEMAN      = 23682,  // 无头骑士 - 万圣节节日BOSS
    NPC_HEADLESS_HORSEMAN_HEAD = 23775,  // 无头骑士头部 - 战斗机制中分离的头部
    NPC_PULSING_PUMPKIN        = 23694,  // 跳动的南瓜 - 无头骑士战斗中召唤
    NPC_PUMPKIN_FIEND          = 23545,  // 南瓜恶魔 - 无头骑士战斗中召唤
    NPC_FLAME_BUNNY            = 23686,  // 火焰兔 - 无头骑士召唤特效生物
    NPC_EARTH_BUNNY            = 23758,  // 大地兔 - 无头骑士召唤特效生物
    NPC_SIR_THOMAS             = 23904   // 托马斯爵士 - 无头骑士事件相关NPC
};

/**
 * @brief 血色修道院生物杂项枚举
 *
 * 定义副本中使用的法术ID、事件ID和动作ID
 */
enum SMCreatureMisc
{
    SPELL_EARTH_EXPLOSION         = 42373,  // 大地爆炸 - 无头骑士召唤特效法术
    EVENT_ACTIVE_EARTH_EXPLOSION  = 1,      // 激活大地爆炸事件
    EVENT_SPAWN_HEADLESS_HORSEMAN = 2,      // 召唤无头骑士事件
    EVENT_DESPAWN_OBJECTS         = 3,      // 移除对象事件
    ACTION_HORSEMAN_EVENT_START   = 101     // 无头骑士事件开始动作
};

/**
 * @brief 血色修道院游戏对象ID枚举
 *
 * 定义副本中所有重要游戏对象的ID
 */
enum SMGameObjectIds
{
    GO_HIGH_INQUISITORS_DOOR = 104600,  // 高检察官大门 - 控制怀特迈恩密室的门
    GO_PUMPKIN_SHRINE        = 186267,  // 南瓜神殿 - 万圣节期间召唤无头骑士
    GO_LOOSELY_TURNED_SOIL   = 186314   // 疏松的泥土 - 召唤无头骑士的另一种方式
};

/**
 * @brief 获取血色修道院AI实例
 * @tparam AI AI类型
 * @tparam T 对象类型（Creature或GameObject）
 * @param obj 对象指针
 * @return AI实例指针
 *
 * 辅助函数，用于从生物或游戏对象获取血色修道院副本AI实例
 */
template <class AI, class T>
inline AI* GetScarletMonasteryAI(T* obj)
{
    return GetInstanceAI<AI>(obj, SMScriptName);
}

/**
 * @brief 注册血色修道院生物AI宏
 *
 * 使用工厂模式注册生物AI，自动绑定到血色修道院副本实例
 */
#define RegisterScarletMonasteryCreatureAI(ai) RegisterCreatureAIWithFactory(ai, GetScarletMonasteryAI)

/**
 * @brief 注册血色修道院游戏对象AI宏
 *
 * 使用工厂模式注册游戏对象AI，自动绑定到血色修道院副本实例
 */
#define RegisterScarletMonasteryGameObjectAI(ai) RegisterGameObjectAIWithFactory(ai, GetScarletMonasteryAI)

#endif // SCARLET_M_
