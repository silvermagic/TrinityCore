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
 * @file uldaman.cpp
 * @brief Uldaman 副本游戏对象脚本实现
 *
 * 本模块实现了 Uldaman 副本中特定游戏对象的交互逻辑，主要是：
 * - 基石 (Keystone) 的交互处理
 *
 * 基石用于开启通往艾隆纳亚的密封门，玩家插入基石后会触发
 * 门开启动画和艾隆纳亚的激活流程。
 */

/* ScriptData
SDName: Uldaman
SD%Complete: 100
SDCategory: Uldaman
EndScriptData */

/* ContentData
go_keystone_chamber
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "uldaman.h"

/*######
## go_keystone_chamber
######*/

/**
 * @brief 基石游戏对象脚本类
 *
 * 基石位于艾隆纳亚房间入口处，玩家交互后会：
 * 1. 触发密封门开启动画（27秒）
 * 2. 激活艾隆纳亚
 * 3. 阻止基石再次被使用
 *
 * 该脚本只负责触发事件，实际的计时和状态管理由实例脚本处理。
 */
class go_keystone_chamber : public GameObjectScript
{
    public:
        go_keystone_chamber() : GameObjectScript("go_keystone_chamber") { }

        /**
         * @brief 基石AI结构体
         */
        struct go_keystone_chamberAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             */
            go_keystone_chamberAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;   ///< 实例脚本指针，用于与副本交互

            /**
             * @brief 玩家点击游戏对象时的处理
             * @param player 点击基石的玩家（未使用）
             * @return false 表示允许默认行为继续
             *
             * 通知实例脚本开始艾隆纳亚密封门的开启流程。
             * 实例脚本会处理：
             * - 27秒的门开启动画
             * - 艾隆纳亚的激活
             * - 基石的锁定（防止重复使用）
             */
            bool OnGossipHello(Player* /*player*/) override
            {
                instance->SetData(DATA_IRONAYA_SEAL, IN_PROGRESS); // 门动画并保存状态
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
            return GetUldamanAI<go_keystone_chamberAI>(go);
        }
};

/**
 * @brief 脚本注册函数
 *
 * 此函数在脚本初始化时被调用一次。
 * 注册本文件中定义的所有脚本类。
 */
void AddSC_uldaman()
{
    new go_keystone_chamber();
}
