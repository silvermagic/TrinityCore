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
 * @file instance_scarlet_monastery.cpp
 * @brief 血色修道院副本实例脚本
 *
 * 本模块实现了血色修道院副本的实例管理功能：
 * - 副本内BOSS和游戏对象的状态管理
 * - 无头骑士事件的处理
 * - 副本数据的保存和加载
 *
 * 血色修道院包含多个独立的区域：
 * - 墓地（Graveyard）- 无头骑士
 * - 大教堂（Cathedral）- 莫格莱尼和怀特迈恩
 * - 军械库（Armory）- 赫罗德
 * - 图书馆（Library）- 阿尔萨斯·索恩（Arcanist Doan）
 *
 * 本脚本主要处理无头骑士事件，其他BOSS由各自的脚本处理
 */

#include "scarlet_monastery.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "EventMap.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"

/**
 * @brief 无头骑士事件相关位置常量
 *
 * 定义无头骑士事件中各种生物的刷新位置
 */
Position const BunnySpawnPosition = { 1776.27f, 1348.74f, 19.20f };                    // 火焰兔刷新位置
Position const EarthBunnySpawnPosition = { 1765.28f, 1347.46f, 18.55f, 6.17f };      // 大地兔刷新位置
Position const HeadlessHorsemanSpawnPosition = { 1765.00f, 1347.00f, 15.00f };       // 无头骑士刷新位置
Position const HeadlessHorsemanHeadSpawnPosition = { 1788.54f, 1348.05f, 18.88f };   // 无头骑士头部刷新位置（估计值）

/**
 * @brief 生物数据映射表
 *
 * 将生物ID映射到数据槽位，用于副本脚本快速访问特定生物
 */
ObjectData const creatureData[] =
{
    { NPC_HEADLESS_HORSEMAN_HEAD, DATA_HORSEMAN_HEAD     },  // 无头骑士头部
    { NPC_HEADLESS_HORSEMAN,      DATA_HEADLESS_HORSEMAN },  // 无头骑士
    { NPC_FLAME_BUNNY,            DATA_FLAME_BUNNY       },  // 火焰兔
    { NPC_EARTH_BUNNY,            DATA_EARTH_BUNNY       },  // 大地兔
    { NPC_SIR_THOMAS,             DATA_THOMAS            },  // 托马斯爵士
    { NPC_MOGRAINE,               DATA_MOGRAINE          },  // 莫格莱尼
    { NPC_VORREL,                 DATA_VORREL            },  // 沃雷尔
    { NPC_WHITEMANE,              DATA_WHITEMANE         },  // 怀特迈恩
    { 0,                          0                      }   // END - 结束标记
};

/**
 * @brief 游戏对象数据映射表
 *
 * 将游戏对象ID映射到数据槽位，用于副本脚本快速访问特定游戏对象
 */
ObjectData const gameObjectData[] =
{
    { GO_PUMPKIN_SHRINE,        DATA_PUMPKIN_SHRINE        },  // 南瓜神殿
    { GO_HIGH_INQUISITORS_DOOR, DATA_HIGH_INQUISITORS_DOOR },  // 高检察官大门
    { GO_LOOSELY_TURNED_SOIL,   DATA_LOOSELY_TURNED_SOIL   },  // 疏松的泥土
    { 0,                        0                          }   // END - 结束标记
};

/**
 * @brief 血色修道院副本实例脚本类
 *
 * 继承自InstanceMapScript，实现血色修道院副本的实例管理
 */
class instance_scarlet_monastery : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称和地图ID（189为血色修道院地图ID）
         */
        instance_scarlet_monastery() : InstanceMapScript(SMScriptName, 189) { }

        /**
         * @brief 血色修道院实例脚本实现类
         *
         * 继承自InstanceScript，实现副本的具体管理逻辑：
         * - 无头骑士事件管理
         * - BOSS状态跟踪
         * - 游戏对象状态管理
         */
        struct instance_scarlet_monastery_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图对象指针
             *
             * 初始化副本脚本：
             * - 设置数据头（用于存档验证）
             * - 设置BOSS数量
             * - 加载生物和游戏对象数据映射
             * - 初始化无头骑士状态
             */
            instance_scarlet_monastery_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);                          // 设置存档数据头
                SetBossNumber(EncounterCount);                   // 设置BOSS遭遇战数量
                LoadObjectData(creatureData, gameObjectData);    // 加载对象数据映射
                _horsemanState = NOT_STARTED;                    // 初始化无头骑士状态为未开始
            }

            /**
             * @brief 处理无头骑士事件开始
             *
             * 当玩家触发无头骑士事件时调用：
             * 1. 设置事件状态为进行中
             * 2. 将南瓜神殿和疏松泥土设为不可选中
             * 3. 召唤无头骑士头部、火焰兔、大地兔
             * 4. 安排事件时间表（大地爆炸、召唤无头骑士、移除对象）
             * 5. 移除托马斯爵士
             *
             * 调用时机：当玩家与南瓜神殿或疏松泥土交互时
             */
            void HandleStartEvent()
            {
                _horsemanState = IN_PROGRESS;

                // 将南瓜神殿和疏松泥土设为不可选中
                for (uint32 data : {DATA_PUMPKIN_SHRINE, DATA_LOOSELY_TURNED_SOIL})
                    if (GameObject* gob = GetGameObject(data))
                        gob->SetFlag(GO_FLAG_NOT_SELECTABLE);

                // 召唤相关生物
                instance->SummonCreature(NPC_HEADLESS_HORSEMAN_HEAD, HeadlessHorsemanHeadSpawnPosition);
                instance->SummonCreature(NPC_FLAME_BUNNY, BunnySpawnPosition);
                instance->SummonCreature(NPC_EARTH_BUNNY, EarthBunnySpawnPosition);

                // 安排事件时间表
                _events.ScheduleEvent(EVENT_ACTIVE_EARTH_EXPLOSION, 1s + 500ms);  // 1.5秒后触发大地爆炸
                _events.ScheduleEvent(EVENT_SPAWN_HEADLESS_HORSEMAN, 3s);         // 3秒后召唤无头骑士
                _events.ScheduleEvent(EVENT_DESPAWN_OBJECTS, 10s);                // 10秒后移除对象

                // 移除托马斯爵士
                if (Creature* thomas = GetCreature(DATA_THOMAS))
                    thomas->DespawnOrUnsummon();
            }

            /**
             * @brief 设置数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 处理外部脚本对副本数据的设置请求：
             * - DATA_START_HORSEMAN_EVENT: 启动无头骑士事件
             * - DATA_HORSEMAN_EVENT_STATE: 设置无头骑士事件状态
             * - DATA_PREPARE_RESET: 准备重置事件
             *
             * 调用时机：由其他脚本（如无头骑士AI）调用以控制事件流程
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_START_HORSEMAN_EVENT:
                        if (_horsemanState != IN_PROGRESS)
                            HandleStartEvent();
                        break;
                    case DATA_HORSEMAN_EVENT_STATE:
                        _horsemanState = data;
                        break;
                    case DATA_PREPARE_RESET:
                        _horsemanState = NOT_STARTED;
                        // 移除火焰兔和大地兔
                        for (uint32 data : {DATA_FLAME_BUNNY, DATA_EARTH_BUNNY})
                            if (Creature* bunny = GetCreature(data))
                                bunny->DespawnOrUnsummon();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 数据值
             *
             * 处理外部脚本对副本数据的查询请求：
             * - DATA_HORSEMAN_EVENT_STATE: 返回无头骑士事件状态
             *
             * 调用时机：由其他脚本调用以查询事件状态
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_HORSEMAN_EVENT_STATE:
                        return _horsemanState;
                    default:
                        return 0;
                }
            }

            /**
             * @brief 更新副本
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理副本事件：
             * - EVENT_ACTIVE_EARTH_EXPLOSION: 触发大地爆炸特效
             * - EVENT_SPAWN_HEADLESS_HORSEMAN: 召唤无头骑士
             * - EVENT_DESPAWN_OBJECTS: 移除南瓜神殿和疏松泥土
             *
             * 调用时机：每帧由核心代码调用
             */
            void Update(uint32 diff) override
            {
                if (_events.Empty())
                    return;

                _events.Update(diff);

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_ACTIVE_EARTH_EXPLOSION:
                            // 让大地兔施放大地爆炸法术
                            if (Creature* earthBunny = GetCreature(DATA_EARTH_BUNNY))
                                earthBunny->CastSpell(earthBunny, SPELL_EARTH_EXPLOSION);
                            break;
                        case EVENT_SPAWN_HEADLESS_HORSEMAN:
                            // 召唤无头骑士并通知开始事件
                            if (TempSummon* horseman = instance->SummonCreature(NPC_HEADLESS_HORSEMAN, HeadlessHorsemanSpawnPosition))
                                horseman->AI()->DoAction(ACTION_HORSEMAN_EVENT_START);
                            break;
                        case EVENT_DESPAWN_OBJECTS:
                            // 移除南瓜神殿和疏松泥土
                            for (uint32 data : {DATA_PUMPKIN_SHRINE, DATA_LOOSELY_TURNED_SOIL})
                                if (GameObject* gob = GetGameObject(data))
                                    gob->RemoveFromWorld();
                            break;
                        default:
                            break;
                    }
                }
            }

        private:
            EventMap _events;        // 事件映射表，用于定时事件管理
            uint32 _horsemanState;   // 无头骑士事件状态
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图对象指针
         * @return 实例脚本对象指针
         *
         * 创建并返回血色修道院实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_scarlet_monastery_InstanceMapScript(map);
        }
};

/**
 * @brief 注册副本脚本
 *
 * 将血色修道院副本脚本注册到脚本系统中
 */
void AddSC_instance_scarlet_monastery()
{
    new instance_scarlet_monastery();
}
