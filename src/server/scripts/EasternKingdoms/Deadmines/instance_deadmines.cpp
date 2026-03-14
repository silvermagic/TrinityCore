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
 * @file    instance_deadmines.cpp
 * @brief   死亡矿坑副本实例脚本实现
 *
 * @details 该模块实现了死亡矿坑副本的实例管理功能，主要包括：
 *          - Boss击杀状态管理
 *          - 门的状态控制
 *          - 铁门爆破事件的完整流程
 *          - 召唤海盗并让他们进入副本
 *          - Mr. Smite的警报对话触发
 *
 *          铁门爆破事件流程：
 *          1. 玩家使用迪菲亚火药在大炮处
 *          2. 3秒后大炮开火，炸毁铁门
 *          3. 召唤两个海盗在门前
 *          4. 1秒后海盗冲入副本
 *          5. Mr. Smite发出警报对话
 *
 * @note    该副本有6个Boss：Rhahk'zor、Sneed、Gilnid、Mr. Smite、Greenskin、Cookie
 *
 * ScriptData
 * SDName: Instance_Deadmines
 * SD%Complete: 100
 * SDComment:
 * SDCategory: Deadmines
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "deadmines.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "TemporarySummon.h"

/**
 * @brief 音效ID枚举定义
 */
enum Sounds
{
    SOUND_CANNONFIRE                                     = 1400,  ///< 大炮开火音效
    SOUND_DESTROYDOOR                                    = 3079   ///< 铁门被炸毁音效
};

/**
 * @brief 定时器和延迟相关常量定义
 */
enum Misc
{
    DATA_CANNON_BLAST_TIMER                                = 3000,  ///< 大炮开火延迟时间（3秒）
    DATA_PIRATES_DELAY_TIMER                               = 1000,  ///< 海盗移动延迟时间（1秒）
    DATA_SMITE_ALARM_DELAY_TIMER                           = 5000   ///< Smite警报延迟时间（5秒）
};

/**
 * @brief 门数据配置表
 *
 * @details 定义了副本中需要根据Boss状态自动控制开关的门。
 *          每个条目包含：GameObject ID、Boss ID、门类型。
 */
DoorData const doorData[] =
{
    { GO_FACTORY_DOOR,      BOSS_RHAHKZOR,   DOOR_TYPE_PASSAGE },  ///< 工厂门 - Rhahk'zor击杀后开启
    { GO_MAST_ROOM_DOOR,    BOSS_SNEED,      DOOR_TYPE_PASSAGE },  ///< 桅杆室门 - Sneed击杀后开启
    { GO_FOUNDRY_DOOR,      BOSS_GILNID,     DOOR_TYPE_PASSAGE },  ///< 铸造厂门 - Gilnid击杀后开启
    { 0,                    0,               DOOR_TYPE_ROOM    }   ///< 结束标记
};

/**
 * @class instance_deadmines
 * @brief 死亡矿坑副本实例脚本类
 *
 * @details 该类负责创建和管理死亡矿坑副本实例。
 *          它继承自InstanceMapScript，提供了创建实例脚本的接口。
 */
class instance_deadmines : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数，注册副本脚本
         *
         * @details 参数说明：
         *          - DMScriptName: 脚本名称
         *          - 36: 死亡矿坑的地图ID
         */
        instance_deadmines() : InstanceMapScript(DMScriptName, 36) { }

        /**
         * @struct instance_deadmines_InstanceMapScript
         * @brief 死亡矿坑副本实例脚本实现
         *
         * @details 管理副本内的所有状态，包括：
         *          - Boss击杀状态
         *          - 门的开闭状态
         *          - 铁门爆破事件流程
         *          - 关键GameObject和Creature的GUID存储
         */
        struct instance_deadmines_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数，初始化副本实例
             *
             * @param map 副本地图指针
             *
             * @details 初始化副本脚本，设置数据头、Boss数量、门数据。
             *          初始化事件状态和计时器。
             */
            instance_deadmines_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);           // 设置数据头标识
                SetBossNumber(EncounterCount);    // 设置Boss数量
                LoadDoorData(doorData);           // 加载门数据配置

                // 初始化事件状态和计时器
                State = CANNON_NOT_USED;
                CannonBlast_Timer = 0;
                PiratesDelay_Timer = 0;
                SmiteAlarmDelay_Timer = 0;
            }

            ObjectGuid IronCladDoorGUID;          ///< 铁门GUID
            ObjectGuid DefiasCannonGUID;          ///< 迪菲亚大炮GUID
            ObjectGuid DoorLeverGUID;             ///< 门拉杆GUID
            ObjectGuid DefiasPirate1GUID;         ///< 第一个迪菲亚海盗GUID
            ObjectGuid DefiasPirate2GUID;         ///< 第二个迪菲亚海盗GUID
            ObjectGuid MrSmiteGUID;               ///< Mr. Smite Boss的GUID

            uint32 State;                         ///< 事件状态机当前状态
            uint32 CannonBlast_Timer;             ///< 大炮开火计时器
            uint32 PiratesDelay_Timer;            ///< 海盗移动延迟计时器
            uint32 SmiteAlarmDelay_Timer;         ///< Smite警报延迟计时器
            ObjectGuid uiSmiteChestGUID;          ///< Smite的武器箱GUID

            /**
             * @brief 更新副本实例状态
             *
             * @param diff 自上次更新以来的时间差（毫秒）
             *
             * @details 每帧调用，负责处理铁门爆破事件的流程：
             *          1. CANNON_GUNPOWDER_USED: 玩家使用了火药，准备开火
             *          2. CANNON_BLAST_INITIATED: 大炮开火，炸毁铁门，召唤海盗
             *          3. PIRATES_ATTACK: 海盗冲入副本
             *          4. SMITE_ALARMED: Mr. Smite发出警报
             *          5. EVENT_DONE: 事件完成
             *
             * @note 性能注意事项：该函数每帧调用，应保持高效。
             */
            virtual void Update(uint32 diff) override
            {
                // 如果关键GameObject不存在，直接返回
                if (!IronCladDoorGUID || !DefiasCannonGUID || !DoorLeverGUID)
                    return;

                // 获取铁门GameObject，如果不存在则返回
                GameObject* pIronCladDoor = instance->GetGameObject(IronCladDoorGUID);
                if (!pIronCladDoor)
                    return;

                // 根据当前状态执行不同的逻辑
                switch (State)
                {
                    case CANNON_GUNPOWDER_USED:
                        // 玩家使用了火药，设置大炮开火延迟时间
                        CannonBlast_Timer = DATA_CANNON_BLAST_TIMER;
                        State = CANNON_BLAST_INITIATED;
                        break;
                    case CANNON_BLAST_INITIATED:
                        // 等待大炮开火延迟时间
                        PiratesDelay_Timer = DATA_PIRATES_DELAY_TIMER;
                        SmiteAlarmDelay_Timer = DATA_SMITE_ALARM_DELAY_TIMER;
                        if (CannonBlast_Timer <= diff)
                        {
                            // 时间到，执行爆炸事件
                            SummonCreatures();      // 召唤海盗
                            ShootCannon();          // 大炮开火
                            BlastOutDoor();         // 炸毁铁门
                            LeverStucked();         // 锁定拉杆
                            instance->LoadGrid(-22.8f, -797.24f); // 加载Mr. Smite所在的网格，确保其存在
                            // Mr. Smite发出第一次警报对话
                            if (Creature* smite = instance->GetCreature(MrSmiteGUID))
                                smite->AI()->Talk(SAY_ALARM1);
                            State = PIRATES_ATTACK;  // 进入海盗攻击状态
                        } else CannonBlast_Timer -= diff;
                        break;
                    case PIRATES_ATTACK:
                        // 等待海盗移动延迟时间
                        if (PiratesDelay_Timer <= diff)
                        {
                            MoveCreaturesInside();  // 让海盗冲入副本
                            State = SMITE_ALARMED;  // 进入Smite警报状态
                        } else PiratesDelay_Timer -= diff;
                        break;
                    case SMITE_ALARMED:
                        // 等待Smite警报延迟时间
                        if (SmiteAlarmDelay_Timer <= diff)
                        {
                            // Mr. Smite发出第二次警报对话
                            if (Creature* smite = instance->GetCreature(MrSmiteGUID))
                                smite->AI()->Talk(SAY_ALARM2);
                            State = EVENT_DONE;     // 事件完成
                        } else SmiteAlarmDelay_Timer -= diff;
                        break;
                }
            }

            /**
             * @brief 召唤迪菲亚海盗
             *
             * @details 在铁门前召唤两个迪菲亚海盗。
             *          海盗会在爆炸后冲入副本内部。
             */
            void SummonCreatures()
            {
                if (GameObject* pIronCladDoor = instance->GetGameObject(IronCladDoorGUID))
                {
                    // 在铁门前召唤两个海盗，位置略有偏移
                    Creature* DefiasPirate1 = pIronCladDoor->SummonCreature(657, pIronCladDoor->GetPositionX() - 2, pIronCladDoor->GetPositionY()-7, pIronCladDoor->GetPositionZ(), 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);
                    Creature* DefiasPirate2 = pIronCladDoor->SummonCreature(657, pIronCladDoor->GetPositionX() + 3, pIronCladDoor->GetPositionY()-6, pIronCladDoor->GetPositionZ(), 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 3s);

                    // 保存海盗的GUID以便后续移动
                    DefiasPirate1GUID = DefiasPirate1->GetGUID();
                    DefiasPirate2GUID = DefiasPirate2->GetGUID();
                }
            }

            /**
             * @brief 移动海盗进入副本
             *
             * @details 让两个召唤的海盗冲入副本内部。
             */
            void MoveCreaturesInside()
            {
                // 检查海盗是否存在
                if (!DefiasPirate1GUID || !DefiasPirate2GUID)
                    return;

                Creature* pDefiasPirate1 = instance->GetCreature(DefiasPirate1GUID);
                Creature* pDefiasPirate2 = instance->GetCreature(DefiasPirate2GUID);
                if (!pDefiasPirate1 || !pDefiasPirate2)
                    return;

                // 移动两个海盗进入副本
                MoveCreatureInside(pDefiasPirate1);
                MoveCreatureInside(pDefiasPirate2);
            }

            /**
             * @brief 移动单个生物进入副本
             *
             * @param creature 生物对象指针
             *
             * @details 设置生物为跑步状态，并移动到副本内部的指定位置。
             */
            void MoveCreatureInside(Creature* creature)
            {
                creature->SetWalk(false);  // 设置为跑步模式
                // 移动到副本内部的指定坐标
                creature->GetMotionMaster()->MovePoint(0, -102.7f, -655.9f, creature->GetPositionZ());
            }

            /**
             * @brief 大炮开火
             *
             * @details 激活大炮GameObject并播放开火音效。
             */
            void ShootCannon()
            {
                if (GameObject* pDefiasCannon = instance->GetGameObject(DefiasCannonGUID))
                {
                    pDefiasCannon->SetGoState(GO_STATE_ACTIVE);          // 激活大炮
                    pDefiasCannon->PlayDirectSound(SOUND_CANNONFIRE);     // 播放开火音效
                }
            }

            /**
             * @brief 炸毁铁门
             *
             * @details 将铁门状态设置为已摧毁，并播放爆炸音效。
             */
            void BlastOutDoor()
            {
                if (GameObject* pIronCladDoor = instance->GetGameObject(IronCladDoorGUID))
                {
                    pIronCladDoor->SetGoState(GO_STATE_DESTROYED);       // 设置为摧毁状态
                    pIronCladDoor->PlayDirectSound(SOUND_DESTROYDOOR);    // 播放爆炸音效
                }
            }

            /**
             * @brief 锁定门拉杆
             *
             * @details 设置拉杆为不可交互状态，防止玩家重复触发事件。
             */
            void LeverStucked()
            {
                if (GameObject* pDoorLever = instance->GetGameObject(DoorLeverGUID))
                    pDoorLever->SetFlag(GO_FLAG_INTERACT_COND);  // 设置交互条件标志，使其不可用
            }

            /**
             * @brief 生物创建回调
             *
             * @param creature 新创建的生物
             *
             * @details 当副本中创建生物时触发。
             *          记录关键Boss的GUID，如Mr. Smite。
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);

                switch (creature->GetEntry())
                {
                    case NPC_MR_SMITE:
                        MrSmiteGUID = creature->GetGUID();  // 记录Mr. Smite的GUID
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief GameObject创建回调
             *
             * @param go 新创建的GameObject
             *
             * @details 当副本中创建GameObject时触发。
             *          记录关键GameObject的GUID，如铁门、大炮、拉杆、武器箱等。
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_IRONCLAD_DOOR:
                        IronCladDoorGUID = go->GetGUID();      // 记录铁门GUID
                        break;
                    case GO_DEFIAS_CANNON:
                        DefiasCannonGUID = go->GetGUID();      // 记录大炮GUID
                        break;
                    case GO_DOOR_LEVER:
                        DoorLeverGUID = go->GetGUID();         // 记录拉杆GUID
                        break;
                    case GO_MR_SMITE_CHEST:
                        uiSmiteChestGUID = go->GetGUID();      // 记录Smite武器箱GUID
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 单位死亡回调
             *
             * @param unit 死亡的单位
             *
             * @details 当副本中有单位死亡时触发。
             *          检查死亡单位是否为Boss，如果是则设置Boss状态为完成。
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (Creature* creature = unit->ToCreature())
                {
                    switch (creature->GetEntry())
                    {
                        case NPC_RHAHKZOR:
                            SetBossState(BOSS_RHAHKZOR, DONE);  // Rhahk'zor击杀
                            break;
                        case NPC_SNEED:
                            SetBossState(BOSS_SNEED, DONE);     // Sneed击杀
                            break;
                        case NPC_GILNID:
                            SetBossState(BOSS_GILNID, DONE);    // Gilnid击杀
                            break;
                        case NPC_MR_SMITE:
                            SetBossState(BOSS_MR_SMITE, DONE);  // Mr. Smite击杀
                            break;
                        case NPC_GREENSKIN:
                            SetBossState(BOSS_GREENSKIN, DONE); // Greenskin击杀
                            break;
                        case NPC_COOKIE:
                            SetBossState(BOSS_COOKIE, DONE);    // Cookie击杀
                            break;
                        default:
                            break;
                    }
                }
            }

            /**
             * @brief 设置数据回调
             *
             * @param type 数据类型
             * @param data 数据值
             *
             * @details 用于外部脚本设置副本实例数据。
             *          主要用于设置铁门爆破事件的状态。
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case EVENT_STATE:
                        // 设置事件状态，前提是大炮和铁门都存在
                        if (DefiasCannonGUID && IronCladDoorGUID)
                            State = data;
                        break;
                }
            }

            /**
             * @brief 获取数据回调
             *
             * @param type 数据类型
             * @return uint32 返回对应的数据值
             *
             * @details 用于外部脚本获取副本实例数据。
             *          主要用于获取铁门爆破事件的状态。
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case EVENT_STATE:
                        return State;  // 返回当前事件状态
                }

                return 0;
            }

            /**
             * @brief 获取GUID数据回调
             *
             * @param data 数据类型标识
             * @return ObjectGuid 返回对应的GUID
             *
             * @details 用于外部脚本获取副本中关键对象的GUID。
             *          如Mr. Smite的武器箱GUID。
             */
            ObjectGuid GetGuidData(uint32 data) const override
            {
                switch (data)
                {
                    case DATA_SMITE_CHEST:
                        return uiSmiteChestGUID;  // 返回Smite武器箱GUID
                }

                return ObjectGuid::Empty;
            }
        };

        /**
         * @brief 获取实例脚本对象
         *
         * @param map 副本地图指针
         * @return InstanceScript* 返回新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_deadmines_InstanceMapScript(map);
        }
};

void AddSC_instance_deadmines()
{
    new instance_deadmines();
}
