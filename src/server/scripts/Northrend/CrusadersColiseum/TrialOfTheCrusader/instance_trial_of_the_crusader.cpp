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
 * @file instance_trial_of_the_crusader.cpp
 * @brief 十字军试炼副本实例脚本
 *
 * 本模块实现了十字军试炼副本的核心实例逻辑，包括：
 * - BOSS 战斗状态管理
 * - 副本进度跟踪和保存
 * - 英雄模式尝试次数计数器
 * - 成就系统支持
 * - 事件流程控制（开场动画、BOSS 切换等）
 * - 游戏对象管理（门、宝箱、地板等）
 *
 * 副本结构：
 * 1. 北伐野兽（Gormok, Acidmaw/Dreadscale, Icehowl）
 * 2. 加拉克索斯大王
 * 3. 阵营勇士
 * 4. 双子瓦尔基里
 * 5. 阿努巴拉克
 *
 * 英雄模式特点：
 * - 有限尝试次数（50 次）
 * - 玩家死亡会影响"不朽"成就
 * - 根据剩余尝试次数生成贡品宝箱
 *
 * @author TrinityCore Team
 */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "trial_of_the_crusader.h"

 // 待办事项：移除事件魔法数字

/**
 * @brief BOSS 边界数据
 *
 * 定义每个 BOSS 的战斗区域边界
 * - 前四个 BOSS 使用圆形边界
 * - 阿努巴拉克使用椭圆形边界（更大的战斗区域）
 */
BossBoundaryData const boundaries =
{
    { DATA_NORTHREND_BEASTS,  new CircleBoundary(Position(563.26f, 139.6f), 75.0)        },  ///< 北伐野兽：圆形边界
    { DATA_JARAXXUS,          new CircleBoundary(Position(563.26f, 139.6f), 75.0)        },  ///< 加拉克索斯：圆形边界
    { DATA_FACTION_CRUSADERS, new CircleBoundary(Position(563.26f, 139.6f), 75.0)        },  ///< 阵营勇士：圆形边界
    { DATA_TWIN_VALKIRIES,    new CircleBoundary(Position(563.26f, 139.6f), 75.0)        },  ///< 双子瓦尔基里：圆形边界
    { DATA_ANUBARAK,          new EllipseBoundary(Position(746.0f, 135.0f), 100.0, 75.0) }   ///< 阿努巴拉克：椭圆形边界
};

/**
 * @brief 生物对象数据映射
 *
 * 将 NPC ID 映射到数据 ID，用于快速查找和状态管理
 */
ObjectData const creatureData[] =
{
    { NPC_GORMOK,                   DATA_GORMOK_THE_IMPALER    },  ///< Gormok the Impaler（穿刺者戈莫克）
    { NPC_ACIDMAW,                  DATA_ACIDMAW               },  ///< Acidmaw（酸喉）
    { NPC_DREADSCALE,               DATA_DREADSCALE            },  ///< Dreadscale（恐鳞）
    { NPC_ICEHOWL,                  DATA_ICEHOWL               },  ///< Icehowl（冰吼）
    { NPC_BEASTS_COMBAT_STALKER,    DATA_BEASTS_COMBAT_STALKER },  ///< 北伐野兽战斗追踪者
    { NPC_FURIOUS_CHARGE_STALKER,   DATA_FURIOUS_CHARGE        },  ///< 狂暴冲锋追踪者
    { NPC_JARAXXUS,                 DATA_JARAXXUS              },  ///< Lord Jaraxxus（加拉克索斯大王）
    { NPC_CHAMPIONS_CONTROLLER,     DATA_FACTION_CRUSADERS     },  ///< 阵营勇士控制器
    { NPC_FJOLA_LIGHTBANE,          DATA_FJOLA_LIGHTBANE       },  ///< Fjola Lightbane（菲奥拉·光誓）
    { NPC_EYDIS_DARKBANE,           DATA_EYDIS_DARKBANE        },  ///< Eydis Darkbane（艾狄斯·暗誓）
    { NPC_LICH_KING,                DATA_LICH_KING             },  ///< The Lich King（巫妖王）
    { NPC_ANUBARAK,                 DATA_ANUBARAK              },  ///< Anub'arak（阿努巴拉克）
    { NPC_TIRION_FORDRING,          DATA_FORDRING              },  ///< Tirion Fordring（提里奥·弗丁）
    { NPC_TIRION_FORDRING_ANUBARAK, DATA_FORDRING_ANUBARAK     },  ///< Tirion Fordring（阿努巴拉克阶段）
    { NPC_VARIAN,                   DATA_VARIAN                },  ///< Varian Wrynn（瓦里安·乌瑞恩）
    { NPC_GARROSH,                  DATA_GARROSH               },  ///< Garrosh Hellscream（加尔鲁什·地狱咆哮）
    { NPC_FIZZLEBANG,               DATA_FIZZLEBANG            },  ///< Wilfred Fizzlebang（威尔弗雷德·菲佐班）
    { NPC_LICH_KING_VOICE,          DATA_LICH_KING_VOICE       },  ///< Lich King Voice（巫妖王声音）
    { 0,                            0                          }   // 结束标记
};

/**
 * @brief 游戏对象数据映射
 *
 * 将游戏对象 ID 映射到数据 ID
 * 包括宝箱、门、地板等关键对象
 */
ObjectData const gameObjectData[] =
{
    { GO_CRUSADERS_CACHE_10,    DATA_CRUSADERS_CHEST },  ///< 十字军宝箱（10人普通）
    { GO_CRUSADERS_CACHE_25,    DATA_CRUSADERS_CHEST },  ///< 十字军宝箱（25人普通）
    { GO_CRUSADERS_CACHE_10_H,  DATA_CRUSADERS_CHEST },  ///< 十字军宝箱（10人英雄）
    { GO_CRUSADERS_CACHE_25_H,  DATA_CRUSADERS_CHEST },  ///< 十字军宝箱（25人英雄）
    { GO_ARGENT_COLISEUM_FLOOR, DATA_COLISEUM_FLOOR  },  ///< 十字军竞技场地板
    { GO_MAIN_GATE_DOOR,        DATA_MAIN_GATE       },  ///< 主大门
    { GO_EAST_PORTCULLIS,       DATA_EAST_PORTCULLIS },  ///< 东侧栅栏门
    { GO_WEB_DOOR,              DATA_WEB_DOOR        },  ///< 蛛网门（阿努巴拉克）
    { GO_TRIBUTE_CHEST_10H_25,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（10H，25+尝试）
    { GO_TRIBUTE_CHEST_10H_45,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（10H，45+尝试）
    { GO_TRIBUTE_CHEST_10H_50,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（10H，50尝试）
    { GO_TRIBUTE_CHEST_10H_99,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（10H，0次失败）
    { GO_TRIBUTE_CHEST_25H_25,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（25H，25+尝试）
    { GO_TRIBUTE_CHEST_25H_45,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（25H，45+尝试）
    { GO_TRIBUTE_CHEST_25H_50,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（25H，50尝试）
    { GO_TRIBUTE_CHEST_25H_99,  DATA_TRIBUTE_CHEST   },  ///< 贡品宝箱（25H，0次失败）
    { 0,                        0                    }   // 结束标记
};

/**
 * @brief 门数据映射
 *
 * 定义门与 BOSS 状态的关联
 * 当 BOSS 进入战斗时，对应的门会关闭
 * 当 BOSS 被击败或失败时，门会打开
 */
DoorData const doorData[] =
{
    { GO_EAST_PORTCULLIS, DATA_NORTHREND_BEASTS,  DOOR_TYPE_ROOM },  ///< 北伐野兽门
    { GO_EAST_PORTCULLIS, DATA_JARAXXUS,          DOOR_TYPE_ROOM },  ///< 加拉克索斯门
    { GO_EAST_PORTCULLIS, DATA_FACTION_CRUSADERS, DOOR_TYPE_ROOM },  ///< 阵营勇士门
    { GO_EAST_PORTCULLIS, DATA_TWIN_VALKIRIES,    DOOR_TYPE_ROOM },  ///< 双子瓦尔基里门
    { GO_EAST_PORTCULLIS, DATA_LICH_KING,         DOOR_TYPE_ROOM },  ///< 巫妖王门
    { GO_WEB_DOOR,        DATA_ANUBARAK,          DOOR_TYPE_ROOM },  ///< 阿努巴拉克蛛网门
    { 0,                  0,                      DOOR_TYPE_ROOM }   // 结束标记
};

/**
 * @class instance_trial_of_the_crusader
 * @brief 十字军试炼副本实例脚本
 *
 * 管理整个副本的状态和流程：
 * - BOSS 状态跟踪和保存
 * - 英雄模式尝试次数管理
 * - 事件流程控制（剧情对话、BOSS 出场等）
 * - 成就系统支持
 */
class instance_trial_of_the_crusader : public InstanceMapScript
{
    public:
        instance_trial_of_the_crusader() : InstanceMapScript(ToCrScriptName, 649) { }

        /**
         * @struct instance_trial_of_the_crusader_InstanceMapScript
         * @brief 副本实例脚本实现
         *
         * 继承自 InstanceScript，实现副本特定的逻辑
         */
        struct instance_trial_of_the_crusader_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化所有成员变量和加载基础数据
             */
            instance_trial_of_the_crusader_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadBossBoundaries(boundaries);
                LoadObjectData(creatureData, gameObjectData);
                LoadDoorData(doorData);
                TrialCounter = 50;                      ///< 英雄模式尝试次数计数器（初始 50 次）
                EventStage = 0;                         ///< 当前事件阶段
                NorthrendBeasts = NOT_STARTED;          ///< 北伐野兽状态
                NorthrendBeastsCount = 4;               ///< 北伐野兽数量计数器
                Team = TEAM_OTHER;                      ///< 玩家团队阵营
                EventTimer = 1000;                      ///< 事件计时器
                NotOneButTwoJormungarsTimer = 0;        ///< "不只是一只虫子"成就计时器
                ResilienceWillFixItTimer = 0;           ///< "韧性会解决它"成就计时器
                SnoboldCount = 0;                       ///< Snobold 数量（成就相关）
                MistressOfPainCount = 0;                ///< 痛苦女士数量（成就相关）
                TributeToImmortalityEligible = true;    ///< 是否符合"不朽贡品"成就条件
                NeedSave = false;                       ///< 是否需要保存副本进度
                CrusadersSpecialState = false;          ///< 阵营勇士特殊状态
                TributeToDedicatedInsanity = false;     ///< "疯狂贡品"成就状态（未实现）
            }

            /**
             * @brief 玩家进入副本事件
             * @param player 进入的玩家
             *
             * 当玩家进入副本时：
             * - 英雄模式：显示尝试次数 UI
             * - 记录玩家阵营
             * - 如果正在进行 Gormok 战斗，为玩家创建载具
             */
            void OnPlayerEnter(Player* player) override
            {
                if (instance->IsHeroic())
                {
                    player->SendUpdateWorldState(UPDATE_STATE_UI_SHOW, 1);
                    player->SendUpdateWorldState(UPDATE_STATE_UI_COUNT, GetData(TYPE_COUNTER));
                }
                else
                    player->SendUpdateWorldState(UPDATE_STATE_UI_SHOW, 0);

                if (Team == TEAM_OTHER)
                    Team = player->GetTeam();

                if (NorthrendBeasts == GORMOK_IN_PROGRESS)
                    player->CreateVehicleKit(PLAYER_VEHICLE_ID, 0);
            }

            /**
             * @brief 生物创建事件
             * @param creature 创建的生物
             *
             * 当生物被创建时，将 Snobold 的 GUID 加入追踪列表
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);
                if (creature->GetEntry() == NPC_SNOBOLD_VASSAL)
                    snoboldGUIDS.push_back(creature->GetGUID());
            }

            /**
             * @brief 获取生物条目（防止英雄模式召唤）
             * @param guidLow 生物 GUID 低位（未使用）
             * @param data 生物数据
             * @return 生物 ID，如果尝试次数用尽则返回 0
             *
             * 英雄模式下，如果尝试次数用尽，阻止生物刷新
             */
            uint32 GetCreatureEntry(ObjectGuid::LowType /*guidLow*/, CreatureData const* data) override
            {
                if (!TrialCounter)
                    return 0;

                return data->id;
            }

            /**
             * @brief 游戏对象创建事件
             * @param go 创建的游戏对象
             *
             * 当竞技场地板被创建时，如果巫妖王战斗已完成，设置为损坏状态
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);
                if (go->GetEntry() == GO_ARGENT_COLISEUM_FLOOR)
                    if (GetBossState(DATA_LICH_KING) == DONE)
                        go->SetDestructibleState(GO_DESTRUCTIBLE_DAMAGED);
            }

            /**
             * @brief 单位死亡事件
             * @param unit 死亡的单位
             *
             * 如果玩家在战斗中死亡，取消"不朽贡品"成就资格
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (unit->GetTypeId() == TYPEID_PLAYER && IsEncounterInProgress())
                    TributeToImmortalityEligible = false;

            }

            /**
             * @brief 设置 BOSS 状态
             * @param type BOSS 类型 ID
             * @param state 新状态
             * @return 是否成功设置
             *
             * 核心 BOSS 状态管理函数，负责：
             * - 处理各个 BOSS 的状态变化
             * - 英雄模式下扣除尝试次数
             * - 触发相应的剧情事件
             * - 生成贡品宝箱
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_NORTHREND_BEASTS:
                        break;
                    case DATA_JARAXXUS:
                        if (state == FAIL)
                        {
                            if (Creature* fordring = GetCreature(DATA_FORDRING))
                                fordring->AI()->DoAction(ACTION_JARAXXUS_WIPE);
                            MistressOfPainCount = 0;
                        }
                        else if (state == DONE)
                        {
                            if (Creature* fordring = GetCreature(DATA_FORDRING))
                                fordring->AI()->DoAction(ACTION_JARAXXUS_DEFEATED);
                            EventStage = 2000;
                        }
                        break;
                    case DATA_FACTION_CRUSADERS:
                        switch (state)
                        {
                            case IN_PROGRESS:
                                ResilienceWillFixItTimer = 0;
                                break;
                            case FAIL:
                                CrusadersSpecialState = false;
                                if (Creature* fordring = GetCreature(DATA_FORDRING))
                                    fordring->AI()->DoAction(ACTION_FACTION_WIPE);
                                break;
                            case DONE:
                                DoUpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, SPELL_DEFEAT_FACTION_CHAMPIONS);
                                if (ResilienceWillFixItTimer > 0)
                                    DoUpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, SPELL_CHAMPIONS_KILLED_IN_MINUTE);
                                DoRespawnGameObject(GetGuidData(DATA_CRUSADERS_CHEST), 7_days);
                                if (GameObject* cache = GetGameObject(DATA_CRUSADERS_CHEST))
                                    cache->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                                if (Creature* fordring = GetCreature(DATA_FORDRING))
                                    fordring->AI()->DoAction(ACTION_CHAMPIONS_DEFEATED);
                                EventStage = 3100;
                                break;
                            default:
                                break;
                        }
                        break;
                    case DATA_TWIN_VALKIRIES:
                        // Cleanup chest
                        if (GameObject* cache = GetGameObject(DATA_CRUSADERS_CHEST))
                            cache->Delete();
                        switch (state)
                        {
                            case FAIL:
                                if (Creature* fordring = GetCreature(DATA_FORDRING))
                                    fordring->AI()->DoAction(ACTION_VALKYR_WIPE);
                                break;
                            case DONE:
                                if (Creature* fordring = GetCreature(DATA_FORDRING))
                                    fordring->AI()->DoAction(ACTION_VALKYR_DEFEATED);
                                break;
                            default:
                                break;
                        }
                        break;
                    case DATA_LICH_KING:
                        break;
                    case DATA_ANUBARAK:
                        switch (state)
                        {
                            case DONE:
                            {
                                EventStage = 6000;
                                uint32 tributeChest = 0;
                                if (instance->GetSpawnMode() == RAID_DIFFICULTY_10MAN_HEROIC)
                                {
                                    if (TrialCounter >= 50)
                                        tributeChest = GO_TRIBUTE_CHEST_10H_99;
                                    else
                                    {
                                        if (TrialCounter >= 45)
                                            tributeChest = GO_TRIBUTE_CHEST_10H_50;
                                        else
                                        {
                                            if (TrialCounter >= 25)
                                                tributeChest = GO_TRIBUTE_CHEST_10H_45;
                                            else
                                                tributeChest = GO_TRIBUTE_CHEST_10H_25;
                                        }
                                    }
                                }
                                else if (instance->GetSpawnMode() == RAID_DIFFICULTY_25MAN_HEROIC)
                                {
                                    if (TrialCounter >= 50)
                                        tributeChest = GO_TRIBUTE_CHEST_25H_99;
                                    else
                                    {
                                        if (TrialCounter >= 45)
                                            tributeChest = GO_TRIBUTE_CHEST_25H_50;
                                        else
                                        {
                                            if (TrialCounter >= 25)
                                                tributeChest = GO_TRIBUTE_CHEST_25H_45;
                                            else
                                                tributeChest = GO_TRIBUTE_CHEST_25H_25;
                                        }
                                    }
                                }

                                if (tributeChest)
                                    if (Creature* tirion =  GetCreature(DATA_FORDRING))
                                        if (GameObject* chest = tirion->SummonGameObject(tributeChest, 805.62f, 134.87f, 142.16f, 3.27f, QuaternionData(), 7_days))
                                            chest->SetRespawnTime(chest->GetRespawnDelay());
                                break;
                            }
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }

                if (type < EncounterCount)
                {
                    TC_LOG_DEBUG("scripts", "[ToCr] BossState(type {}) {} = state {};", type, GetBossState(type), state);
                    if (state == FAIL)
                    {
                        if (instance->IsHeroic())
                        {
                            --TrialCounter;
                            // decrease attempt counter at wipe
                            Map::PlayerList const& PlayerList = instance->GetPlayers();
                            for (Map::PlayerList::const_iterator itr = PlayerList.begin(); itr != PlayerList.end(); ++itr)
                                if (Player* player = itr->GetSource())
                                    player->SendUpdateWorldState(UPDATE_STATE_UI_COUNT, TrialCounter);

                            // if theres no more attemps allowed
                            if (!TrialCounter)
                            {
                                if (Creature* anubarak = GetCreature(DATA_ANUBARAK))
                                    anubarak->DespawnOrUnsummon();
                            }
                        }
                        NeedSave = true;
                        EventStage = (type == DATA_NORTHREND_BEASTS ? 666 : 0);
                        state = NOT_STARTED;
                    }

                    if (state == DONE || NeedSave)
                        Save();
                }
                return true;
            }

            /**
             * @brief 处理北伐野兽完成
             *
             * 减少北伐野兽计数器，当所有野兽被击败时：
             * - 设置 BOSS 状态为完成
             * - 清理 Snobold
             * - 移除玩家载具
             * - 触发下一个 BOSS 的剧情
             */
            void HandleNorthrendBeastsDone()
            {
                --NorthrendBeastsCount;
                if (!NorthrendBeastsCount)
                {
                    SetData(TYPE_NORTHREND_BEASTS, DONE);
                    SetBossState(DATA_NORTHREND_BEASTS, DONE);
                    SetData(DATA_DESPAWN_SNOBOLDS, 0);
                    EventStage = 400;
                    if (Creature* combatStalker = GetCreature(DATA_BEASTS_COMBAT_STALKER))
                        combatStalker->DespawnOrUnsummon();
                    HandlePlayerVehicle(false);
                    if (Creature* fordring = GetCreature(DATA_FORDRING))
                        fordring->AI()->DoAction(ACTION_NORTHREND_BEASTS_DEFEATED);
                }
            }

            /**
             * @brief 处理玩家载具
             * @param apply 是否应用载具
             *
             * 为所有在副本中的玩家创建或移除载具
             * 用于 Gormok 战斗中 Snobold 跳到玩家身上的机制
             */
            void HandlePlayerVehicle(bool apply)
            {
                Map::PlayerList const &players = instance->GetPlayers();
                for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                    if (Player* player = itr->GetSource())
                    {
                        if (apply)
                            player->CreateVehicleKit(PLAYER_VEHICLE_ID, 0);
                        else
                            player->RemoveVehicleKit();
                    }
            }

            /**
             * @brief 设置数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 处理各种副本数据的设置：
             * - 尝试次数计数器
             * - 事件阶段
             * - 北伐野兽状态
             * - 成就相关计数
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case TYPE_COUNTER:
                        TrialCounter = data;
                        data = DONE;
                        break;
                    case TYPE_EVENT:
                        EventStage = data;
                        data = NOT_STARTED;
                        break;
                    case TYPE_EVENT_TIMER:
                        EventTimer = data;
                        data = NOT_STARTED;
                        break;
                    case TYPE_NORTHREND_BEASTS:
                        NorthrendBeasts = data;
                        switch (data)
                        {
                            case GORMOK_IN_PROGRESS:
                                SetBossState(DATA_NORTHREND_BEASTS, IN_PROGRESS);
                                NorthrendBeastsCount = 4;
                                HandlePlayerVehicle(true);
                                break;
                            case GORMOK_DONE:
                                if (Creature* tirion = GetCreature(DATA_FORDRING))
                                    tirion->AI()->DoAction(ACTION_START_JORMUNGARS);
                                HandleNorthrendBeastsDone();
                                break;
                            case SNAKES_IN_PROGRESS:
                                NotOneButTwoJormungarsTimer = 0;
                                break;
                            case SNAKES_SPECIAL:
                                NotOneButTwoJormungarsTimer = 10*IN_MILLISECONDS;
                                HandleNorthrendBeastsDone();
                                break;
                            case SNAKES_DONE:
                                if (NotOneButTwoJormungarsTimer > 0)
                                    DoUpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, SPELL_WORMS_KILLED_IN_10_SECONDS);
                                if (Creature* tirion = GetCreature(DATA_FORDRING))
                                    tirion->AI()->DoAction(ACTION_START_ICEHOWL);
                                HandleNorthrendBeastsDone();
                                break;
                            case ICEHOWL_DONE:
                                HandleNorthrendBeastsDone();
                                break;
                            case FAIL:
                                HandlePlayerVehicle(false);
                                SetBossState(DATA_NORTHREND_BEASTS, FAIL);
                                if (Creature* tirion = GetCreature(DATA_FORDRING))
                                    tirion->AI()->DoAction(ACTION_NORTHREND_BEASTS_WIPE);
                                SnoboldCount = 0;
                                break;
                            default:
                                break;
                        }
                        break;
                    case DATA_DESPAWN_SNOBOLDS:
                        for (ObjectGuid guid : snoboldGUIDS)
                            if (Creature* snobold = instance->GetCreature(guid))
                                snobold->DespawnOrUnsummon();
                        snoboldGUIDS.clear();
                        break;
                    //Achievements
                    case DATA_SNOBOLD_COUNT:
                        if (data == INCREASE)
                            ++SnoboldCount;
                        else if (data == DECREASE)
                            --SnoboldCount;
                        break;
                    case DATA_MISTRESS_OF_PAIN_COUNT:
                        if (data == INCREASE)
                            ++MistressOfPainCount;
                        else if (data == DECREASE)
                            --MistressOfPainCount;
                        break;
                    case DATA_FACTION_CRUSADERS: // Achivement Resilience will Fix
                        ResilienceWillFixItTimer = 60 * IN_MILLISECONDS;
                        CrusadersSpecialState = true;
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 对应的数据值
             *
             * 查询副本数据：
             * - 团队阵营
             * - 尝试次数
             * - 事件阶段
             * - 北伐野兽状态
             * - 事件 NPC ID（根据事件阶段返回不同的 NPC）
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_TEAM:
                        return Team;
                    case TYPE_COUNTER:
                        return TrialCounter;
                    case TYPE_EVENT:
                        return EventStage;
                    case TYPE_NORTHREND_BEASTS:
                        return NorthrendBeasts;
                    case TYPE_EVENT_TIMER:
                        return EventTimer;
                    case TYPE_EVENT_NPC:
                        // 根据事件阶段返回对应的 NPC ID
                        switch (EventStage)
                        {
                            case 110:
                            case 140:
                            case 150:
                            case 155:
                            case 200:
                            case 205:
                            case 210:
                            case 220:
                            case 300:
                            case 305:
                            case 310:
                            case 315:
                            case 400:
                            case 666:
                            case 1010:
                            case 1180:
                            case 2000:
                            case 2030:
                            case 3000:
                            case 3001:
                            case 3060:
                            case 3061:
                            case 3090:
                            case 3091:
                            case 3092:
                            case 3100:
                            case 3110:
                            case 4000:
                            case 4010:
                            case 4015:
                            case 4016:
                            case 4040:
                            case 4050:
                            case 5000:
                            case 5005:
                            case 5020:
                            case 6000:
                            case 6005:
                            case 6010:
                                return NPC_TIRION_FORDRING;     ///< 提里奥·弗丁的对话事件
                                break;
                            case 5010:
                            case 5030:
                            case 5040:
                            case 5050:
                            case 5060:
                            case 5070:
                            case 5080:
                                return NPC_LICH_KING;          ///< 巫妖王的对话事件
                                break;
                            case 120:
                            case 122:
                            case 2020:
                            case 3080:
                            case 3051:
                            case 3071:
                            case 4020:
                                return NPC_VARIAN;             ///< 瓦里安的对话事件
                                break;
                            case 130:
                            case 132:
                            case 2010:
                            case 3050:
                            case 3070:
                            case 3081:
                            case 4030:
                                return NPC_GARROSH;            ///< 加尔鲁什的对话事件
                                break;
                            case 1110:
                            case 1120:
                            case 1130:
                            case 1132:
                            case 1134:
                            case 1135:
                            case 1140:
                            case 1142:
                            case 1144:
                            case 1150:
                                return NPC_FIZZLEBANG;        ///< 菲佐班的对话事件
                                break;
                            default:
                                return NPC_TIRION_FORDRING;
                                break;
                        };
                    default:
                        break;
                }

                return 0;
            }

            /**
             * @brief 更新副本状态
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 周期性更新函数，负责：
             * - 更新"不只是一只虫子"成就计时器
             * - 更新"韧性会解决它"成就计时器
             */
            void Update(uint32 diff) override
            {
                if (GetData(TYPE_NORTHREND_BEASTS) == SNAKES_SPECIAL && NotOneButTwoJormungarsTimer)
                {
                    if (NotOneButTwoJormungarsTimer <= diff)
                        NotOneButTwoJormungarsTimer = 0;
                    else
                        NotOneButTwoJormungarsTimer -= diff;
                }

                if (CrusadersSpecialState && ResilienceWillFixItTimer)
                {
                    if (ResilienceWillFixItTimer <= diff)
                        ResilienceWillFixItTimer = 0;
                    else
                        ResilienceWillFixItTimer -= diff;
                }
            }

            /**
             * @brief 保存副本进度
             *
             * 保存副本进度到数据库，并重置保存标志
             */
            void Save()
            {
                SaveToDB();
                NeedSave = false;
            }

            /**
             * @brief 写入额外保存数据
             * @param data 输出字符串流
             *
             * 将尝试次数和成就相关状态写入保存数据
             */
            void WriteSaveDataMore(std::ostringstream& data) override
            {
                data << TrialCounter << ' '
                    << uint32(TributeToImmortalityEligible ? 1 : 0) << ' '
                    << uint32(TributeToDedicatedInsanity ? 1 : 0);
            }

            /**
             * @brief 读取额外保存数据
             * @param data 输入字符串流
             *
             * 从保存数据中恢复尝试次数和成就相关状态
             */
            void ReadSaveDataMore(std::istringstream& data) override
            {
                uint32 temp = 0;

                data >> TrialCounter;

                data >> temp;
                TributeToImmortalityEligible = temp != 0;

                data >> temp;
                TributeToDedicatedInsanity = temp != 0;
            }

            /**
             * @brief 检查成就条件是否满足
             * @param criteria_id 成就条件 ID
             * @param source 来源玩家（未使用）
             * @param target 目标单位（未使用）
             * @param miscvalue1 杂项值（未使用）
             * @return 是否满足条件
             *
             * 检查各种成就条件：
             * - Upper Back Pain：保留多个 Snobold
             * - 360 Pain Spike：击杀多个痛苦女士
             * - A Tribute to Skill：剩余 25+ 尝试次数
             * - A Tribute to Mad Skill：剩余 45+ 尝试次数
             * - A Tribute to Insanity：剩余 50 尝试次数
             * - A Tribute to Immortality：剩余 50 次且无玩家死亡
             */
            bool CheckAchievementCriteriaMeet(uint32 criteria_id, Player const* /*source*/, Unit const* /*target*/, uint32 /*miscvalue1*/) override
            {
                switch (criteria_id)
                {
                    case UPPER_BACK_PAIN_10_PLAYER:
                    case UPPER_BACK_PAIN_10_PLAYER_HEROIC:
                        return SnoboldCount >= 2;
                    case UPPER_BACK_PAIN_25_PLAYER:
                    case UPPER_BACK_PAIN_25_PLAYER_HEROIC:
                        return SnoboldCount >= 4;
                    case THREE_SIXTY_PAIN_SPIKE_10_PLAYER:
                    case THREE_SIXTY_PAIN_SPIKE_10_PLAYER_HEROIC:
                    case THREE_SIXTY_PAIN_SPIKE_25_PLAYER:
                    case THREE_SIXTY_PAIN_SPIKE_25_PLAYER_HEROIC:
                        return MistressOfPainCount >= 2;
                    case A_TRIBUTE_TO_SKILL_10_PLAYER:
                    case A_TRIBUTE_TO_SKILL_25_PLAYER:
                        return TrialCounter >= 25;
                    case A_TRIBUTE_TO_MAD_SKILL_10_PLAYER:
                    case A_TRIBUTE_TO_MAD_SKILL_25_PLAYER:
                        return TrialCounter >= 45;
                    case A_TRIBUTE_TO_INSANITY_10_PLAYER:
                    case A_TRIBUTE_TO_INSANITY_25_PLAYER:
                    case REALM_FIRST_GRAND_CRUSADER:
                        return TrialCounter == 50;
                    case A_TRIBUTE_TO_IMMORTALITY_HORDE:
                    case A_TRIBUTE_TO_IMMORTALITY_ALLIANCE:
                        return TrialCounter == 50 && TributeToImmortalityEligible;
                    case A_TRIBUTE_TO_DEDICATED_INSANITY:
                        return false/*TrialCounter == 50 && TributeToDedicatedInsanity*/;
                    default:
                        break;
                }

                return false;
            }

            protected:
                uint32 TrialCounter;                        ///< 英雄模式尝试次数计数器（初始 50）
                uint32 EventStage;                          ///< 当前事件阶段（控制剧情流程）
                uint32 EventTimer;                          ///< 事件计时器（控制事件间隔）
                uint32 NorthrendBeasts;                     ///< 北伐野兽战斗状态
                uint32 Team;                                ///< 玩家团队阵营（联盟/部落）
                bool NeedSave;                              ///< 是否需要保存副本进度
                bool CrusadersSpecialState;                 ///< 阵营勇士特殊状态（用于"韧性会解决它"成就）
                GuidVector snoboldGUIDS;                    ///< Snobold GUID 列表（用于清理）

                // 成就相关数据
                uint32 NotOneButTwoJormungarsTimer;         ///< "不只是一只虫子"成就计时器（10秒内击杀两只虫子）
                uint32 ResilienceWillFixItTimer;            ///< "韧性会解决它"成就计时器（60秒内击杀所有勇士）
                uint8 SnoboldCount;                         ///< 当前存活的 Snobold 数量
                uint8 MistressOfPainCount;                  ///< 当前存活的痛苦女士数量
                uint8 NorthrendBeastsCount;                 ///< 北伐野兽剩余数量
                bool TributeToImmortalityEligible;          ///< 是否符合"不朽贡品"成就条件（无玩家死亡）
                bool TributeToDedicatedInsanity;            ///< "疯狂贡品"成就状态（未实现）
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_trial_of_the_crusader_InstanceMapScript(map);
        }
};

/**
 * @brief 注册十字军试炼副本实例脚本
 *
 * 创建并注册副本实例脚本到脚本系统
 */
void AddSC_instance_trial_of_the_crusader()
{
    new instance_trial_of_the_crusader();
}
