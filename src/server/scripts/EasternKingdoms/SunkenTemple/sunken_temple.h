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
 * @file    sunken_temple.h
 * @brief   沉没的神庙副本头文件
 * @details 定义沉没的神庙副本的核心数据结构，包括：
 *          - Boss ID 枚举定义
 *          - 生物 NPC ID 枚举定义
 *          - 游戏对象 ID 枚举定义
 *          - 事件类型枚举定义
 *          - 共享法术 ID 枚举定义
 *          - AI 辅助函数模板
 *
 *          沉没的神庙（Sunken Temple，又称阿塔哈卡神庙）是一个 50 级左右的 5 人副本，
 *          位于悲伤沼泽。副本包含多个 Boss 和独特的雕像谜题机制。
 */

#ifndef DEF_SUNKEN_TEMPLE_H
#define DEF_SUNKEN_TEMPLE_H

#include "CreatureAIImpl.h"

/// 副本脚本名称，用于注册和查找实例脚本
#define STScriptName "instance_sunken_temple"

/// 数据存储头标识，用于序列化副本进度数据
#define DataHeader "ST"

/**
 * @enum STBossIds
 * @brief Boss 遭遇战 ID 枚举
 * @details 定义副本中所有 Boss 遭遇战的唯一标识符。
 *          这些 ID 用于追踪 Boss 的击杀状态和副本进度。
 *          数值对应 Boss 在副本进度数据中的存储索引。
 */
enum STBossIds
{
    BOSS_AVATAR_OF_HAKKAR       = 0,  ///< 哈卡的化身 - 最终 Boss
    BOSS_JAMMALAN_THE_PROPHET   = 1,  ///< 先知贾玛兰 - 副本主要 Boss
    BOSS_DREAMSCYTHE            = 2,  ///< 梦镰 - 绿龙 Boss
    BOSS_WEAVER                 = 3,  ///< 织者 - 绿龙 Boss
    BOSS_MORPHAZ                = 4,  ///< 莫弗拉斯 - 绿龙 Boss
    BOSS_HAZZAS                 = 5,  ///< 哈扎斯 - 绿龙 Boss
    BOSS_SHADE_OF_ERANIKUS      = 6,  ///< 伊兰尼库斯之影 - 副本最终 Boss
    BOSS_ATALALARION            = 7,  ///< 阿塔拉里恩 - 雕像谜题召唤的小 Boss
    BOSS_EVENT_ELITE_TROLLS     = 8,  ///< 精英巨魔事件 - 击杀 6 个精英巨魔的小 Boss 事件

    MAX_ENCOUNTER                     ///< Boss 遭遇战总数，用于数组大小定义
};

/**
 * @enum STCreatureIds
 * @brief 生物 NPC ID 枚举
 * @details 定义副本中出现的所有重要生物的 ID。
 *          包括 Boss、小 Boss 和关键 NPC。
 */
enum STCreatureIds
{
    NPC_AVATAR_OF_HAKKAR        = 8443,  ///< 哈卡的化身
    NPC_JAMMALAN_THE_PROPHET    = 5710,  ///< 先知贾玛兰
    NPC_DREAMSCYTHE             = 5721,  ///< 梦镰 - 绿龙 Boss
    NPC_WEAVER                  = 5720,  ///< 织者 - 绿龙 Boss
    NPC_MORPHAZ                 = 5719,  ///< 莫弗拉斯 - 绿龙 Boss
    NPC_HAZZAS                  = 5722,  ///< 哈扎斯 - 绿龙 Boss
    NPC_SHADE_OF_ERANIKUS       = 5709,  ///< 伊兰尼库斯之影
    NPC_ATALALARION             = 8580,  ///< 阿塔拉里恩 - 雕像谜题召唤
    NPC_ZOLO                    = 5712,  ///< 佐洛 - 精英巨魔
    NPC_GASHER                  = 5713,  ///< 加什尔 - 精英巨魔
    NPC_LORO                    = 5714,  ///< 洛罗 - 精英巨魔
    NPC_HUKKU                   = 5715,  ///< 胡库 - 精英巨魔
    NPC_ZUL_LOR                 = 5716,  ///< 祖尔洛 - 精英巨魔
    NPC_MIJAN                   = 5717,  ///< 米詹 - 精英巨魔
};

/**
 * @enum STGameObjectIds
 * @brief 游戏对象 ID 枚举
 * @details 定义副本中所有重要游戏对象的 ID。
 *          主要包括雕像谜题相关的对象和力场门。
 */
enum STGameObjectIds
{
    GO_ATALAI_STATUE1 = 148830,  ///< 阿塔莱雕像 1 - 第一个需要激活的雕像
    GO_ATALAI_STATUE2 = 148831,  ///< 阿塔莱雕像 2 - 第二个需要激活的雕像
    GO_ATALAI_STATUE3 = 148832,  ///< 阿塔莱雕像 3 - 第三个需要激活的雕像
    GO_ATALAI_STATUE4 = 148833,  ///< 阿塔莱雕像 4 - 第四个需要激活的雕像
    GO_ATALAI_STATUE5 = 148834,  ///< 阿塔莱雕像 5 - 第五个需要激活的雕像
    GO_ATALAI_STATUE6 = 148835,  ///< 阿塔莱雕像 6 - 最后一个需要激活的雕像
    GO_ATALAI_LIGHT1  = 148883,  ///< 阿塔莱之光 1 - 激活雕像时出现的光效
    GO_ATALAI_LIGHT2  = 148937,  ///< 阿塔莱之光 2 - 完成谜题后所有雕像周围的光效
    GO_FORCEFIELD     = 149431,  ///< 力场屏障 - 阻挡通往先知贾玛兰的通道
};

/**
 * @enum STEvents
 * @brief 副本事件类型枚举
 * @details 定义副本中使用的自定义事件类型。
 *          这些事件用于实例脚本内部的状态管理和通信。
 */
enum STEvents
{
    EVENT_STATE = 1  ///< 雕像状态事件 - 用于触发雕像谜题的进度更新
};

/**
 * @enum STShareSpells
 * @brief 共享法术 ID 枚举
 * @details 定义副本中多个脚本共用的法术 ID。
 */
enum STShareSpells
{
    SPELL_SUPPRESSION      = 12623,  ///< 压制法术 - 可能用于控制玩家
    SPELL_GREEN_CHANNELING = 13540   ///< 绿色引导法术 - 先知贾玛兰施放的视觉效果
};

/**
 * @brief  获取沉没的神庙副本 AI 实例
 * @tparam AI  AI 类型，如具体的 Boss AI 类
 * @tparam T   对象类型，通常是 Creature 或 GameObject
 * @param  obj 游戏对象指针（生物或游戏对象）
 * @return 返回类型为 AI* 的实例脚本指针，如果对象不在副本中则返回 nullptr
 * @details 此模板函数封装了获取副本 AI 的通用逻辑。
 *          通过检查对象所在地图是否为沉没的神庙副本，
 *          返回相应的实例脚本 AI 指针。
 *          使用示例：
 *          @code
 *          MyBossAI* ai = GetSunkenTempleAI<MyBossAI>(creature);
 *          @endcode
 */
template <class AI, class T>
inline AI* GetSunkenTempleAI(T* obj)
{
    return GetInstanceAI<AI>(obj, STScriptName);
}

#endif
