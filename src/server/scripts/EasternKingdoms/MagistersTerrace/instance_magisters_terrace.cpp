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
 * @file instance_magisters_terrace.cpp
 * @brief 魔导师平台副本实例脚本
 *
 * 本模块实现了魔导师平台副本的实例管理逻辑。
 * 管理副本中的BOSS状态、门控制、生物生成和事件处理。
 *
 * 主要功能：
 * - BOSS状态管理（塞林·火心、维萨鲁斯、女祭司德莉萨、凯尔萨斯）
 * - 门控制（根据BOSS状态自动开启/关闭）
 * - 凯尔萨斯前置小怪管理
 * - 卡雷苟斯生成事件
 * - 德莉萨手下死亡计数
 *
 * BOSS顺序：
 * 0 - 塞林·火心（Selin Fireheart）
 * 1 - 维萨鲁斯（Vexallus）
 * 2 - 女祭司德莉萨（Priestess Delrissa）
 * 3 - 凯尔萨斯·逐日者（Kael'thas Sunstrider）
 *
 * @note 卡雷苟斯会在副本完成后的特定位置生成并飞行
 */

#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "EventMap.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "magisters_terrace.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "TemporarySummon.h"

/**
 * @brief BOSS顺序说明
 *
 * 0  - 塞林·火心（Selin Fireheart）
 * 1  - 维萨鲁斯（Vexallus）
 * 2  - 女祭司德莉萨（Priestess Delrissa）
 * 3  - 凯尔萨斯·逐日者（Kael'thas Sunstrider）
 */

/**
 * @brief 生物数据映射数组
 *
 * 将生物ID映射到数据ID，用于实例脚本管理BOSS和重要NPC
 */
ObjectData const creatureData[] =
{
    { BOSS_SELIN_FIREHEART,         DATA_SELIN_FIREHEART        },
    { BOSS_VEXALLUS,                DATA_VEXALLUS               },
    { BOSS_PRIESTESS_DELRISSA,      DATA_PRIESTESS_DELRISSA     },
    { BOSS_KAELTHAS_SUNSTRIDER,     DATA_KAELTHAS_SUNSTRIDER    },
    { NPC_KALECGOS,                 DATA_KALECGOS               },
    { NPC_HUMAN_KALECGOS,           DATA_KALECGOS               },
    { 0,                            0                           } // END（结束标记）
};

/**
 * @brief 游戏对象数据映射数组
 *
 * 将游戏对象ID映射到数据ID，用于实例脚本管理重要游戏对象
 */
ObjectData const gameObjectData[] =
{
    { GO_ESCAPE_ORB,                DATA_ESCAPE_ORB             },
    { 0,                            0                           } // END（结束标记）
};

/**
 * @brief 门数据数组
 *
 * 定义了副本中的门与BOSS状态的关联
 * 门会在对应BOSS被击杀或进入战斗时自动开启/关闭
 */
DoorData const doorData[] =
{
    { GO_SUNWELL_RAID_GATE_2  , DATA_SELIN_FIREHEART,       DOOR_TYPE_PASSAGE   },  // 塞林·火心后的通道门
    { GO_ASSEMBLY_CHAMBER_DOOR, DATA_SELIN_FIREHEART,       DOOR_TYPE_ROOM      },  // 塞林·火心的房间门
    { GO_SUNWELL_RAID_GATE_5,   DATA_VEXALLUS,              DOOR_TYPE_PASSAGE   },  // 维萨鲁斯后的通道门
    { GO_SUNWELL_RAID_GATE_4,   DATA_PRIESTESS_DELRISSA,    DOOR_TYPE_PASSAGE   },  // 德莉萨后的通道门
    { GO_ASYLUM_DOOR,           DATA_KAELTHAS_SUNSTRIDER,   DOOR_TYPE_ROOM      },  // 凯尔萨斯的房间门
    { 0,                        0,                          DOOR_TYPE_ROOM      } // END（结束标记）
};

Position const KalecgosSpawnPos = { 164.3747f, -397.1197f, 2.151798f, 1.66219f };  ///< 卡雷苟斯生成位置
Position const KaelthasTrashGroupDistanceComparisonPos = { 150.0f, 141.0f, -14.4f }; ///< 凯尔萨斯前置小怪组距离比较位置

/**
 * @brief 魔导师平台实例脚本
 *
 * 实现了魔导师平台副本的实例管理逻辑
 */
class instance_magisters_terrace : public InstanceMapScript
{
    public:
        instance_magisters_terrace() : InstanceMapScript(MGTScriptName, 585) { }

        /**
         * @brief 魔导师平台实例脚本结构体
         *
         * 继承自InstanceScript，实现副本的具体管理逻辑
         */
        struct instance_magisters_terrace_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化实例脚本，设置数据头、BOSS数量，加载生物和游戏对象数据，加载门数据
             */
            instance_magisters_terrace_InstanceMapScript(InstanceMap* map) : InstanceScript(map), _delrissaDeathCount(0)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadObjectData(creatureData, gameObjectData);
                LoadDoorData(doorData);
            }

            /**
             * @brief 获取实例数据
             * @param type 数据类型
             * @return 数据值
             *
             * 返回德莉萨手下死亡计数
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_DELRISSA_DEATH_COUNT:
                        return _delrissaDeathCount;
                    default:
                        break;
                }
                return 0;
            }

            /**
             * @brief 设置实例数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 处理德莉萨手下死亡计数：
             * - SPECIAL：增加计数
             * - 其他：重置计数为0
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_DELRISSA_DEATH_COUNT:
                        if (data == SPECIAL)
                            _delrissaDeathCount++;
                        else
                            _delrissaDeathCount = 0;
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 生物创建时调用
             * @param creature 创建的生物
             *
             * 处理凯尔萨斯前置小怪的GUID存储：
             * - 检查生物ID是否为凯尔萨斯前置小怪类型
             * - 如果生物在凯尔萨斯前置小怪组范围内，将其GUID添加到集合中
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);

                switch (creature->GetEntry())
                {
                    case NPC_COILSKAR_WITCH:
                    case NPC_SUNBLADE_WARLOCK:
                    case NPC_SUNBLADE_MAGE_GUARD:
                    case NPC_SISTER_OF_TORMENT:
                    case NPC_ETHEREUM_SMUGGLER:
                    case NPC_SUNBLADE_BLOOD_KNIGHT:
                        if (creature->GetDistance(KaelthasTrashGroupDistanceComparisonPos) < 10.0f)
                            _kaelthasPreTrashGUIDs.insert(creature->GetGUID());
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 单位死亡时调用
             * @param unit 死亡的单位
             *
             * 处理凯尔萨斯前置小怪死亡事件：
             * - 检查死亡单位是否为凯尔萨斯前置小怪
             * - 如果是，从GUID集合中移除
             * - 当所有前置小怪都死亡时，触发凯尔萨斯的介绍事件
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (unit->GetTypeId() != TYPEID_UNIT)
                    return;

                switch (unit->GetEntry())
                {
                    case NPC_COILSKAR_WITCH:
                    case NPC_SUNBLADE_WARLOCK:
                    case NPC_SUNBLADE_MAGE_GUARD:
                    case NPC_SISTER_OF_TORMENT:
                    case NPC_ETHEREUM_SMUGGLER:
                    case NPC_SUNBLADE_BLOOD_KNIGHT:
                        if (_kaelthasPreTrashGUIDs.find(unit->GetGUID()) != _kaelthasPreTrashGUIDs.end())
                        {
                            _kaelthasPreTrashGUIDs.erase(unit->GetGUID());
                            if (_kaelthasPreTrashGUIDs.size() == 0)
                                if (Creature* kaelthas = GetCreature(DATA_KAELTHAS_SUNSTRIDER))
                                    kaelthas->AI()->SetData(DATA_KAELTHAS_INTRO, IN_PROGRESS);
                        }
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建时调用
             * @param go 创建的游戏对象
             *
             * 处理逃脱传送球的可见性：
             * - 如果凯尔萨斯已被击杀，移除不可选择标志，允许玩家使用
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_ESCAPE_ORB:
                        if (GetBossState(DATA_KAELTHAS_SUNSTRIDER) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 处理事件
             * @param obj 触发事件的对象（未使用）
             * @param eventId 事件ID
             *
             * 处理卡雷苟斯生成事件：
             * - 当事件触发且卡雷苟斯未生成且事件队列为空时
             * - 延迟1分钟后生成卡雷苟斯
             */
            void ProcessEvent(WorldObject* /*obj*/, uint32 eventId) override
            {
                if (eventId == EVENT_SPAWN_KALECGOS)
                    if (!GetCreature(DATA_KALECGOS) && _events.Empty())
                        _events.ScheduleEvent(EVENT_SPAWN_KALECGOS, 1min);
            }

            /**
             * @brief 更新实例逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 处理卡雷苟斯生成事件：
             * - 更新事件计时器
             * - 执行卡雷苟斯生成事件
             * - 让卡雷苟斯沿飞行路径移动并喊出对话
             */
            void Update(uint32 diff) override
            {
                _events.Update(diff);

                if (_events.ExecuteEvent() == EVENT_SPAWN_KALECGOS)
                {
                    if (Creature* kalecgos = instance->SummonCreature(NPC_KALECGOS, KalecgosSpawnPos))
                    {
                        kalecgos->GetMotionMaster()->MovePath(PATH_KALECGOS_FLIGHT, false);
                        kalecgos->AI()->Talk(SAY_KALECGOS_SPAWN);
                    }
                }
            }

            /**
             * @brief 设置BOSS状态
             * @param type BOSS类型
             * @param state 遭遇状态
             * @return 设置是否成功
             *
             * 处理BOSS状态变化：
             * - 德莉萨战斗开始时重置手下死亡计数
             * - 凯尔萨斯被击杀时，启用逃脱传送球
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_PRIESTESS_DELRISSA:
                        if (state == IN_PROGRESS)
                            _delrissaDeathCount = 0;
                        break;
                    case DATA_KAELTHAS_SUNSTRIDER:
                        if (state == DONE)
                            if (GameObject* orb = GetGameObject(DATA_ESCAPE_ORB))
                                orb->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }
                return true;
            }

        protected:
            EventMap _events;                     ///< 事件映射表，用于调度卡雷苟斯生成事件
            GuidSet _kaelthasPreTrashGUIDs;       ///< 凯尔萨斯前置小怪GUID集合
            uint8 _delrissaDeathCount;            ///< 德莉萨手下死亡计数
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 实例脚本指针
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_magisters_terrace_InstanceMapScript(map);
        }
};

/**
 * @brief 注册魔导师平台实例脚本
 *
 * 创建魔导师平台实例脚本对象
 */
void AddSC_instance_magisters_terrace()
{
    new instance_magisters_terrace();
}
