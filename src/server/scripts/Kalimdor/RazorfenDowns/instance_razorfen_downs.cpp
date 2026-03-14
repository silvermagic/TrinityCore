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
 * @file instance_razorfen_downs.cpp
 * @brief 剃刀高地副本实例脚本
 *
 * 本模块实现了剃刀高地副本的实例数据管理，主要包括：
 * - BOSS状态管理（Tuten'kash、Mordresh Fire Eye、Glutton、Amnennar）
 * - 锣事件管理：敲锣后召唤波次怪物
 * - 偶像灭火事件：与Belnistrasz相关的任务事件
 * - 游戏对象状态管理（锣、偶像火焰等）
 *
 * 锣事件机制：
 * 1. 玩家点击锣触发第一波召唤（10个墓穴恶魔）
 * 2. 击杀完第一波后锣可再次点击
 * 3. 第二波召唤4个墓穴掠夺者
 * 4. 击杀完第二波后锣可再次点击
 * 5. 第三波召唤BOSS Tuten'kash
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "razorfen_downs.h"
#include "TemporarySummon.h"

/**
 * @brief Tuten'kash召唤位置数组
 *
 * 定义了锣事件中召唤怪物的位置：
 * - 索引0-9：墓穴恶魔（Tomb Fiend，NPC 7349）的10个刷新位置
 * - 索引10-13：墓穴掠夺者（Tomb Reaver，NPC 7351）的4个刷新位置
 * - 索引14：Tuten'kash（NPC 7355）的刷新位置
 */
Position const PosSummonTutenkash[15] =
{
    // 墓穴恶魔（Tomb Fiend）位置
    { 2487.339f, 805.9111f, 43.08361f, 2.844887f  },
    { 2485.405f, 804.1145f, 43.68511f, 3.054326f  },
    { 2488.431f, 801.2809f, 42.70374f, 4.29351f   },
    { 2489.914f, 804.7949f, 43.25175f, 1.658063f  },
    { 2541.246f, 907.0941f, 46.64201f, 2.024582f  },
    { 2544.701f, 907.6331f, 46.38007f, 1.605703f  },
    { 2541.49f,  911.1756f, 46.26493f, 4.817109f  },
    { 2544.693f, 912.8887f, 46.39912f, 2.129302f  },
    { 2524.036f, 834.4852f, 48.37031f, 0.8028514f },
    { 2527.017f, 829.9793f, 48.06498f, 0.6981317f },
    // 墓穴掠夺者（Tomb Reaver）位置
    { 2542.818f, 904.9359f, 46.80911f, 4.642576f  },
    { 2543.287f, 911.2448f, 46.32785f, 0.6806784f },
    { 2489.083f, 806.5914f, 43.21102f, 3.682645f  },
    { 2486.828f, 802.8737f, 43.19883f, 2.9147f    },
    // Tuten'kash位置
    { 2487.939f, 804.2224f, 43.10735f, 1.692969f  }
};

/**
 * @class instance_razorfen_downs
 * @brief 剃刀高地副本脚本类
 *
 * 继承自 InstanceMapScript，负责创建和管理剃刀高地的实例脚本
 */
class instance_razorfen_downs : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，地图ID为129（剃刀高地）
     */
    instance_razorfen_downs() : InstanceMapScript(RFDScriptName, 129) { }

    /**
     * @class instance_razorfen_downs_InstanceMapScript
     * @brief 剃刀高地实例脚本实现类
     *
     * 继承自 InstanceScript，实现剃刀高地的实例数据管理
     */
    struct instance_razorfen_downs_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化实例脚本，设置BOSS数量和各状态变量
         */
        instance_razorfen_downs_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            gongWave = 0;           ///< 当前锣波次（0=第一波，1=第二波，2=第三波）
            fiendsKilled = 0;       ///< 已击杀的墓穴恶魔数量
            reaversKilled = 0;      ///< 已击杀的墓穴掠夺者数量
            summonLowRange = 0;     ///< 召唤位置范围下限
            summonHighRange = 0;    ///< 召唤位置范围上限
            summonCreature = 0;     ///< 召唤的生物ID
        }

        /**
         * @brief 游戏对象创建回调
         * @param gameObject 新创建的游戏对象
         *
         * 当游戏对象在副本中创建时调用，用于保存对象引用并设置初始状态
         * @调用时机 游戏对象创建时
         */
        void OnGameObjectCreate(GameObject* gameObject) override
        {
            switch (gameObject->GetEntry())
            {
                case GO_GONG:
                    // 保存锣的GUID，用于后续波次控制
                    goGongGUID = gameObject->GetGUID();
                    // 如果Tuten'kash已被击杀，将锣设为不可选择
                    if (GetBossState(DATA_TUTEN_KASH) == DONE)
                        gameObject->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                case GO_IDOL_OVEN_FIRE:
                case GO_IDOL_CUP_FIRE:
                case GO_IDOL_MOUTH_FIRE:
                    // 偶像火焰，如果灭火任务已完成则删除
                    if (GetBossState(DATA_EXTINGUISHING_THE_IDOL) == DONE)
                        gameObject->Delete();
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 设置BOSS状态
         * @param type BOSS类型ID
         * @param state 新状态
         * @return 是否设置成功
         *
         * 当BOSS状态改变时调用，用于处理BOSS击杀后的特殊逻辑
         * @调用时机 BOSS状态改变时
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                 return false;

            switch (type)
            {
                case DATA_TUTEN_KASH:
                case DATA_MORDRESH_FIRE_EYE:
                case DATA_GLUTTON:
                case DATA_AMNENNAR_THE_COLD_BRINGER:
                case DATA_GONG:
                case DATA_WAVE:
                case DATA_EXTINGUISHING_THE_IDOL:
                    break;
                default:
                    break;
            }
            return true;
        }

        /**
         * @brief 设置自定义数据
         * @param type 数据类型
         * @param data 数据值
         *
         * 用于处理锣事件的波次召唤和怪物击杀计数
         *
         * 波次机制：
         * - IN_PROGRESS：开始新波次召唤
         * - NPC_TOMB_FIEND：墓穴恶魔被击杀，计数+1
         * - NPC_TOMB_REAVER：墓穴掠夺者被击杀，计数+1
         *
         * @调用时机 锣事件触发、怪物死亡时
         */
        void SetData(uint32 type, uint32 data) override
        {
            if (type == DATA_WAVE)
            {
                switch (data)
                {
                    case IN_PROGRESS:
                    {
                        // 开始新波次：将锣设为不可选择
                        if (GameObject* go = instance->GetGameObject(goGongGUID))
                            go->SetFlag(GO_FLAG_NOT_SELECTABLE);

                        // 根据当前波次设置召唤参数
                        switch (gongWave)
                        {
                            case 0:  // 第一波：召唤10个墓穴恶魔
                                summonLowRange = 0;
                                summonHighRange = 10;
                                summonCreature = NPC_TOMB_FIEND;
                                break;
                            case 1:  // 第二波：召唤4个墓穴掠夺者
                                summonLowRange = 10;
                                summonHighRange = 14;
                                summonCreature = NPC_TOMB_REAVER;
                                break;
                            case 2:  // 第三波：召唤BOSS Tuten'kash
                                summonLowRange = 14;
                                summonHighRange = 15;
                                summonCreature = NPC_TUTEN_KASH;
                                break;
                        }

                        // 在指定位置召唤怪物
                        if (GameObject* go = instance->GetGameObject(goGongGUID))
                        {
                            for (uint8 i = summonLowRange; i < summonHighRange; ++i)
                            {
                                Creature* creature = go->SummonCreature(summonCreature, PosSummonTutenkash[i]);
                                // 召唤后让怪物移动到锣附近（带随机偏移）
                                creature->GetMotionMaster()->MovePoint(0, 2533.479f + float(irand(-5, 5)), 870.020f + float(irand(-5, 5)), 47.678f);
                            }
                        }

                        ++gongWave;  // 进入下一波次
                        break;
                    }
                    case NPC_TOMB_FIEND:
                        // 墓穴恶魔被击杀，计数+1，击杀10个后解锁锣
                        if (++fiendsKilled == 10)
                        {
                            fiendsKilled = 0;
                            if (GameObject* go = instance->GetGameObject(goGongGUID))
                                go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                    case NPC_TOMB_REAVER:
                        // 墓穴掠夺者被击杀，计数+1，击杀4个后解锁锣
                        if (++reaversKilled == 4)
                        {
                            reaversKilled = 0;
                            if (GameObject* go = instance->GetGameObject(goGongGUID))
                                go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                }
            }
        }

    protected:
        ObjectGuid goGongGUID;      ///< 锣的GUID，用于波次控制
        uint16 gongWave;            ///< 当前锣波次
        uint8  fiendsKilled;        ///< 已击杀的墓穴恶魔数量
        uint8  reaversKilled;       ///< 已击杀的墓穴掠夺者数量
        uint8  summonLowRange;      ///< 召唤位置范围下限
        uint8  summonHighRange;     ///< 召唤位置范围上限
        uint32 summonCreature;      ///< 召唤的生物ID
    };

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     *
     * @调用时机 当副本地图创建时由核心调用
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_razorfen_downs_InstanceMapScript(map);
    }
};

/**
 * @brief 注册脚本
 *
 * 将剃刀高地实例脚本注册到脚本系统
 */
void AddSC_instance_razorfen_downs()
{
    new instance_razorfen_downs();
}
