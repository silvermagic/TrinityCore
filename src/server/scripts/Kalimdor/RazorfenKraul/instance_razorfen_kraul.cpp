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

/* ScriptData
SDName: Instance_Razorfen_Kraul
SD%Complete:
SDComment:
SDCategory: Razorfen Kraul
EndScriptData */

/**
 * @file instance_razorfen_kraul.cpp
 * @brief 剃刀沼泽副本实例脚本
 *
 * 本模块实现了剃刀沼泽副本的实例数据管理，主要包括：
 * - 守卫者之门机制：需要击杀2个守卫者才能打开大门
 * - 游戏对象状态管理
 *
 * 守卫者之门机制：
 * 1. 副本中有两个守卫者NPC（Ward Keeper）
 * 2. 玩家需要击杀这两个守卫者
 * 3. 当两个守卫者都被击杀后，大门自动打开
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "razorfen_kraul.h"

/// 需要击杀的守卫者数量
#define WARD_KEEPERS_NR 2

/**
 * @class instance_razorfen_kraul
 * @brief 剃刀沼泽副本脚本类
 *
 * 继承自 InstanceMapScript，负责创建和管理剃刀沼泽的实例脚本
 */
class instance_razorfen_kraul : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，地图ID为47（剃刀沼泽）
     */
    instance_razorfen_kraul() : InstanceMapScript(RFKScriptName, 47) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     *
     * @调用时机 当副本地图创建时由核心调用
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_razorfen_kraul_InstanceMapScript(map);
    }

    /**
     * @class instance_razorfen_kraul_InstanceMapScript
     * @brief 剃刀沼泽实例脚本实现类
     *
     * 继承自 InstanceScript，实现剃刀沼泽的实例数据管理
     */
    struct instance_razorfen_kraul_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化实例脚本和数据头
         */
        instance_razorfen_kraul_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            WardKeeperDeath = 0;  ///< 已击杀的守卫者数量
        }

        ObjectGuid DoorWardGUID;     ///< 守卫者之门的GUID
        int WardKeeperDeath;         ///< 已击杀的守卫者数量

        /**
         * @brief 获取副本中的玩家
         * @return 第一个找到的玩家指针，如果没有玩家则返回nullptr
         *
         * @调用时机 需要获取副本中玩家时
         * @性能注意事项 遍历玩家列表，时间复杂度O(n)
         */
        Player* GetPlayerInMap()
        {
            Map::PlayerList const& players = instance->GetPlayers();
            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                if (Player* player = itr->GetSource())
                    return player;
            }
            TC_LOG_DEBUG("scripts", "Instance Razorfen Kraul: GetPlayerInMap, but PlayerList is empty!");
            return nullptr;
        }

        /**
         * @brief 游戏对象创建回调
         * @param go 新创建的游戏对象
         *
         * 当游戏对象在副本中创建时调用：
         * - 21099：守卫者之门，保存GUID用于后续开门
         * - 20920：设置阵营为无（hack，使其不可交互）
         *
         * @调用时机 游戏对象创建时
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case 21099: DoorWardGUID = go->GetGUID(); break;  // 保存守卫者之门GUID
                case 20920: go->SetFaction(FACTION_NONE); break;  // 设置为无阵营（hack）
            }
        }

        /**
         * @brief 更新回调
         * @param diff 时间差（毫秒，未使用）
         *
         * 每帧检查守卫者击杀数量，当达到要求时打开大门
         * @调用时机 每帧更新
         * @性能注意事项 该函数每帧调用，但只有简单的条件判断
         */
        void Update(uint32 /*diff*/) override
        {
            // 当两个守卫者都被击杀，打开大门
            if (WardKeeperDeath == WARD_KEEPERS_NR)
                if (GameObject* go = instance->GetGameObject(DoorWardGUID))
                {
                    go->ReplaceAllFlags(GO_FLAG_IN_USE | GO_FLAG_NODESPAWN);  // 移除使用中和不可销毁标志
                    go->SetGoState(GO_STATE_ACTIVE);  // 设置为激活状态（打开）
                }
        }

        /**
         * @brief 设置自定义数据
         * @param type 数据类型
         * @param data 数据值（未使用）
         *
         * 当守卫者被击杀时增加计数
         *
         * @调用时机 守卫者被击杀时
         */
        void SetData(uint32 type, uint32 /*data*/) override
        {
            switch (type)
            {
                case EVENT_WARD_KEEPER: WardKeeperDeath++; break;  // 守卫者被击杀，计数+1
            }
        }

    };

};

/**
 * @brief 注册脚本
 *
 * 将剃刀沼泽实例脚本注册到脚本系统
 */
void AddSC_instance_razorfen_kraul()
{
    new instance_razorfen_kraul();
}
