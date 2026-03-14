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
 * @file zulgurub.cpp
 * @brief 祖尔格拉布副本游戏对象脚本实现
 *
 * 本文件包含祖尔格拉布副本中特殊游戏对象的脚本实现。
 * 主要处理疯狂之缘(Edge of Madness)区域的疯狂之篝火(Brazier of Madness)。
 *
 * 疯狂之缘机制：
 * 疯狂之缘是祖尔格拉布的一个特殊区域，包含四个可选Boss。
 * 这些Boss不固定出现，而是通过游戏事件系统按周期轮换：
 *
 * 1. 格里克(Grilek) - 战士型Boss
 * 2. 哈扎拉(Hazzarah) - 法师型Boss
 * 3. 雷纳塔基(Renataki) - 盗贼型Boss
 * 4. 乌苏雷(Wushoolay) - 萨满型Boss
 *
 * 玩家需要通过点燃疯狂之篝火来召唤当前活动事件对应的Boss。
 * 每个Boss只在特定的游戏事件期间可被召唤。
 *
 * @note 这些Boss的出现由服务器的游戏事件日历控制，通常每两周轮换一次
 */

#include "ScriptMgr.h"
#include "zulgurub.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"

/*######
 ## go_brazier_of_madness - 疯狂之篝火游戏对象脚本
 ######*/

/**
 * @brief 疯狂之缘游戏事件ID枚举
 *
 * 这些ID对应game_event表中的事件，控制哪个Boss在当前周期可被召唤
 */
enum EventGameIds
{
    // ids from game_event table - 来自game_event表的ID
    EVENT_EDGE_OF_MADNESS_GRILEK    = 27,  ///< 格里克激活事件
    EVENT_EDGE_OF_MADNESS_HAZZARAH  = 28,  ///< 哈扎拉激活事件
    EVENT_EDGE_OF_MADNESS_RENATAKI  = 29,  ///< 雷纳塔基激活事件
    EVENT_EDGE_OF_MADNESS_WUSHOOLAY = 30   ///< 乌苏雷激活事件
};

/**
 * @brief 事件ID与NPC ID的配对类型
 *
 * 将游戏事件ID映射到对应的Boss NPC ID
 */
using EventPair = std::pair<EventGameIds, ZGCreatureIds>;

/**
 * @brief 疯狂之篝火事件生物映射数组
 *
 * 定义了每个游戏事件对应的Boss NPC。
 * 当某个事件激活时，点燃篝火会召唤对应的Boss。
 */
constexpr EventPair BrazierOfMadnessEventCreatures[] =
{
    { EVENT_EDGE_OF_MADNESS_GRILEK,     NPC_GRILEK      },  ///< 格里克
    { EVENT_EDGE_OF_MADNESS_HAZZARAH,   NPC_HAZZARAH    },  ///< 哈扎拉
    { EVENT_EDGE_OF_MADNESS_RENATAKI,   NPC_RENATAKI    },  ///< 雷纳塔基
    { EVENT_EDGE_OF_MADNESS_WUSHOOLAY,  NPC_WUSHOOLAY   }   ///< 乌苏雷
};

/**
 * @brief 疯狂之缘Boss的生成位置
 *
 * Boss被召唤时的固定位置坐标（在疯狂之缘区域的中心）
 */
Position const MadnessSpawnPos = { -11901.229f, -1906.366f, 65.358f, 0.942f };

/**
 * @brief 疯狂之篝火游戏对象脚本
 *
 * 处理玩家与疯狂之篝火交互时的逻辑。
 * 当玩家点击篝火时，根据当前激活的游戏事件召唤对应的Boss。
 */
class go_brazier_of_madness : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册游戏对象脚本，名称为"go_brazier_of_madness"
     */
    go_brazier_of_madness() : GameObjectScript("go_brazier_of_madness") { }

    /**
     * @brief 疯狂之篝火AI结构体
     *
     * 实现游戏对象的交互逻辑
     */
    struct go_brazier_of_madnessAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_brazier_of_madnessAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 处理玩家与游戏对象的交互
         * @param player 与游戏对象交互的玩家（未使用）
         * @return 返回false表示不拦截默认行为
         *
         * 当玩家点击疯狂之篝火时：
         * 1. 遍历所有疯狂之缘事件
         * 2. 检查哪个事件当前处于激活状态
         * 3. 召唤对应事件的Boss到固定位置
         * 4. Boss会在死亡后2小时消失
         *
         * @note 同时只能有一个疯狂之缘事件处于激活状态
         */
        bool OnGossipHello(Player* /*player*/) override
        {
            // 遍历所有疯狂之缘事件与Boss的映射
            for (auto const& [eventId, npcEntry] : BrazierOfMadnessEventCreatures)
            {
                // 检查该事件是否当前激活
                if (sGameEventMgr->IsActiveEvent(eventId))
                {
                    // 召唤对应的Boss
                    // TEMPSUMMON_CORPSE_TIMED_DESPAWN: Boss死亡后尸体保留一段时间后消失
                    // 2h: 尸体保留2小时
                    me->SummonCreature(npcEntry, MadnessSpawnPos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2h);
                    break;  // 找到激活的事件后立即退出循环
                }
            }
            return false;
        }
    };

    /**
     * @brief 获取游戏对象AI
     * @param go 游戏对象指针
     * @return 新创建的AI对象
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_brazier_of_madnessAI(go);
    }
};

/**
 * @brief 注册祖尔格拉布游戏对象脚本
 *
 * 将疯狂之篝火的脚本注册到脚本系统中，使其在游戏中可以被正确调用。
 */
void AddSC_zulgurub()
{
    new go_brazier_of_madness();
}
