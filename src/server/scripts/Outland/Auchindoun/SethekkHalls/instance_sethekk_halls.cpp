/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it; and/or modify it
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
 * @file instance_sethekk_halls.cpp
 * @brief 赛泰克大厅副本实例脚本实现
 *
 * 本模块实现了赛泰克大厅副本的实例管理逻辑，包括：
 * - BOSS状态追踪和保存
 * - 门的状态管理（根据BOSS击杀状态自动开关）
 * - 安苏（Anzu）特殊处理（英雄模式隐藏BOSS）
 * - 利爪之王宝箱（Talon King Coffer）状态管理
 *
 * 赛泰克大厅包含以下BOSS：
 * 1. 拉伊（Raven God）- 安苏（英雄模式隐藏BOSS）
 * 2. 利爪之王艾吉斯（Talon King Ikiss）
 *
 * 特殊机制：
 * - 击杀利爪之王艾吉斯后，可以打开利爪之王宝箱获取战利品
 * - 安苏是英雄模式独有的召唤BOSS
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "sethekk_halls.h"

/**
 * @brief 门数据数组
 *
 * 定义了副本中需要根据BOSS状态自动控制的门。
 * 门会在对应BOSS被击杀后打开。
 */
DoorData const doorData[] =
{
    { GO_IKISS_DOOR, DATA_TALON_KING_IKISS, DOOR_TYPE_PASSAGE },  ///< 艾吉斯门 - 利爪之王艾吉斯击杀后开启
    { 0,             0,                     DOOR_TYPE_ROOM    }   ///< 结束标记
};

/**
 * @brief 游戏对象数据数组
 *
 * 定义了副本中需要追踪的游戏对象。
 */
ObjectData const gameObjectData[] =
{
    { GO_TALON_KING_COFFER, DATA_TALON_KING_COFFER },  ///< 利爪之王宝箱
    { 0,                    0                      }   ///< 结束标记
};

/**
 * @brief 赛泰克大厅实例脚本类
 *
 * 继承自InstanceMapScript，提供赛泰克大厅副本的实例管理功能。
 * 负责追踪BOSS状态、管理游戏对象、处理生物创建事件等。
 */
class instance_sethekk_halls : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册赛泰克大厅实例脚本，地图ID为556。
         */
        instance_sethekk_halls() : InstanceMapScript(SHScriptName, 556) { }

        /**
         * @brief 赛泰克大厅实例地图脚本
         *
         * 继承自InstanceScript，实现具体的副本管理逻辑。
         */
        struct instance_sethekk_halls_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * 初始化实例脚本，设置数据头、BOSS数量、加载门数据和游戏对象数据。
             */
            instance_sethekk_halls_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);                 // 设置数据保存头
                SetBossNumber(EncounterCount);          // 设置BOSS数量
                LoadDoorData(doorData);                 // 加载门数据
                LoadObjectData(nullptr, gameObjectData); // 加载游戏对象数据
            }

            /**
             * @brief 生物创建回调
             * @param creature 新创建的生物
             *
             * 当副本中有生物被创建时调用。负责：
             * - 处理安苏（Anzu）的特殊生成逻辑
             *
             * 安苏是英雄模式的隐藏BOSS，由玩家召唤生成。
             * 如果安苏已被击杀，新生成的安苏会被立即移除。
             * 如果安苏未被击杀，新生成的安苏会触发战斗。
             *
             * @调用时机 当生物在副本中生成时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                if (creature->GetEntry() == NPC_ANZU)
                {
                    // 处理安苏的特殊生成
                    if (GetBossState(DATA_ANZU) == DONE)
                    {
                        // 如果安苏已被击杀，新生成的安苏消失
                        creature->DisappearAndDie();
                    }
                    else
                    {
                        // 如果安苏未被击杀，设置状态为进行中
                        SetBossState(DATA_ANZU, IN_PROGRESS);
                    }
                }
            }

            /**
             * @brief 设置BOSS状态回调
             * @param type BOSS类型标识
             * @param state BOSS状态（进行中、完成等）
             * @return 如果设置成功返回true
             *
             * 当BOSS状态改变时调用。负责：
             * - 调用基类方法保存状态
             * - 处理利爪之王艾吉斯击杀后的特殊逻辑（开启宝箱）
             *
             * @调用时机 当BOSS状态改变时
             * @性能注意事项 需要访问游戏对象，但只在BOSS击杀时触发，性能影响较小
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                // 调用基类方法设置状态
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_TALON_KING_IKISS:
                        if (state == DONE)
                        {
                            /// @workaround: GO_FLAG_INTERACT_COND 标志仍在游戏对象上，但在此情况下处理不正确
                            ///              游戏对象应该有 GO_DYNFLAG_LO_ACTIVATE 标志，这使得带有 GO_FLAG_INTERACT_COND 的游戏对象可以交互
                            ///              所以这里直接移除 GO_FLAG_INTERACT_COND 标志
                            /// 利爪之王艾吉斯击杀后，移除宝箱的交互条件标志和不可选择标志，使玩家可以打开宝箱
                            if (GameObject* coffer = GetGameObject(DATA_TALON_KING_COFFER))
                                coffer->RemoveFlag(GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                    default:
                        break;
                }
                return true;
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         *
         * 工厂方法，创建并返回赛泰克大厅的实例脚本。
         *
         * @调用时机 当副本实例被创建时
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_sethekk_halls_InstanceMapScript(map);
        }
};

/**
 * @brief 注册赛泰克大厅实例脚本
 *
 * 此函数在服务器启动时被调用，注册赛泰克大厅实例脚本。
 */
void AddSC_instance_sethekk_halls()
{
    new instance_sethekk_halls();
}
