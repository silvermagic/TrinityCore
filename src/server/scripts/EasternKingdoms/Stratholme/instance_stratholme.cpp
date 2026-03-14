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
SDName: instance_stratholme
SD%Complete: 50
SDComment: In progress. Undead side 75% implemented. Save/load not implemented.
SDCategory: Stratholme
EndScriptData */

/**
 * @file    instance_stratholme.cpp
 * @brief   斯坦索姆副本实例管理脚本
 *
 * @details 本模块实现了斯坦索姆副本的实例逻辑管理:
 *          - 副本进度状态跟踪和保存
 *          - 巴隆45分钟救援计时事件
 *          - 亡灵侧和血色侧的门和陷阱机制
 *          - 屠宰场事件(消灭憎恶后刷新拉姆斯登)
 *          - 提米残忍BOSS刷新条件
 *          - 亡灵陷阱门机制(召唤鼠群)
 *          - 与游戏对象和生物创建/移除的交互
 *
 * @note 斯坦索姆是魔兽世界经典副本,分为亡灵侧和血色侧两条路线
 */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "EventMap.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "stratholme.h"
#include "Pet.h"

/**
 * @brief 实例事件ID枚举
 * 用于副本事件调度系统
 */
enum InstanceEvents
{
    EVENT_BARON_RUN         = 1,  // 巴隆救援计时事件(45分钟限制)
    EVENT_SLAUGHTER_SQUARE  = 2,  // 屠宰场事件(拉姆斯登死后刷新黑卫士兵)
    EVENT_RAT_TRAP_CLOSE    = 3,  // 鼠群陷阱门关闭事件
};

/**
 * @brief 副本杂项枚举
 */
enum StratholmeMisc
{
    SAY_YSIDA_SAVED         = 0   // 伊希达获救时的台词ID
};

/**
 * @brief 提米残忍BOSS刷新位置
 * 在血色侧入口处击杀足够数量的血色十字军后刷新
 */
Position const timmyTheCruelSpawnPosition = { 3625.358f, -3188.108f, 130.3985f, 4.834562f };

/**
 * @brief 血色侧入口前的椭圆边界
 * 用于检测血色十字军是否在入口区域被击杀(提米刷新条件)
 */
EllipseBoundary const beforeScarletGate(Position(3671.158f, -3181.79f), 60.0f, 40.0f);

/**
 * @enum StratholmeGateTrapType
 * @brief 陷阱门类型枚举
 *
 * @details 定义斯坦索姆中的两种陷阱门类型:
 *          - 血色侧陷阱: 召唤鼠群围攻玩家
 *          - 亡灵侧陷阱: 召唤鼠群围攻玩家
 */
enum class StratholmeGateTrapType : uint8
{
    ScaletSide = 0,  // 血色侧陷阱门
    UndeadSide = 1   // 亡灵侧陷阱门
};

/**
 * @brief 陷阱门位置数组
 *
 * @details 定义两处陷阱门的中心位置:
 *          - [0] 血色侧陷阱门位置
 *          - [1] 亡灵侧陷阱门位置
 *          当玩家靠近这些位置时会触发陷阱
 */
Position const GateTrapPos[] =
{
    { 3612.29f, -3335.39f, 124.077f },  // 血色侧陷阱门
    { 3919.88f, -3545.34f, 134.269f }   // 亡灵侧陷阱门
};

/**
 * @struct GateTrapData
 * @brief 陷阱门数据结构
 *
 * @details 存储每个陷阱门的状态信息:
 *          - 关联的门游戏对象GUID
 *          - 被召唤的鼠群生物GUID集合
 *          - 陷阱是否已被触发
 */
struct GateTrapData
{
    std::array<ObjectGuid, 2> Gates;   // 陷阱门的两个门对象GUID
    GuidUnorderedSet Rats;             // 被召唤的鼠群生物GUID集合
    bool Triggered = false;            // 陷阱是否已被触发
};

/**
 * @class instance_stratholme
 * @brief 斯坦索姆副本实例脚本
 *
 * @details 实现斯坦索姆副本的实例管理:
 *          - 管理副本状态和进度
 *          - 处理游戏对象和生物的创建/销毁
 *          - 实现副本事件(巴隆救援、屠宰场等)
 *          - 管理陷阱门机制
 */
class instance_stratholme : public InstanceMapScript
{
    public:
        instance_stratholme() : InstanceMapScript(StratholmeScriptName, 329) { }

        /**
         * @struct instance_stratholme_InstanceMapScript
         * @brief 斯坦索姆实例脚本实现
         *
         * @details 继承自InstanceScript,实现副本状态管理和事件处理
         */
        struct instance_stratholme_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * @details 初始化副本状态:
             *          - 设置数据头标识
             *          - 初始化所有Boss战状态为未开始
             *          - 初始化白银之手骑士死亡状态
             *          - 重置提米相关计数器
             *          - 启动陷阱门检测事件
             */
            instance_stratholme_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);

                // 初始化所有遭遇战状态为未开始
                for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                    EncounterState[i] = NOT_STARTED;

                // 初始化白银之手骑士死亡状态(任务相关)
                for (uint8 i = 0; i < 5; ++i)
                    IsSilverHandDead[i] = false;

                // 初始化提米相关状态
                timmySpawned = false;
                scarletsKilled = 0;

                // 启动陷阱门检测事件(每1秒检查一次)
                events.ScheduleEvent(EVENT_RAT_TRAP_CLOSE, 15s);
            }

            // ==================== 成员变量 ====================

            uint32 EncounterState[MAX_ENCOUNTER];  // Boss战状态数组
            uint8 scarletsKilled;                  // 血色侧击杀计数(提米刷新条件)

            bool IsSilverHandDead[5];              // 白银之手骑士死亡状态(任务相关)
            bool timmySpawned;                     // 提米是否已刷新

            // 游戏对象GUID
            ObjectGuid serviceEntranceGUID;        // 服务入口门GUID
            ObjectGuid gauntletGate1GUID;          // 死亡奔跑第一道门GUID
            ObjectGuid ziggurat1GUID;              // 灵通1(男爵夫人)GUID
            ObjectGuid ziggurat2GUID;              // 灵通2(奈幽布)GUID
            ObjectGuid ziggurat3GUID;              // 灵通3(苍白者)GUID
            ObjectGuid ziggurat4GUID;              // 灵通4(通往巴隆)GUID
            ObjectGuid ziggurat5GUID;              // 灵通5(通往巴隆)GUID
            ObjectGuid portGauntletGUID;           // 死亡奔跑门GUID
            ObjectGuid portSlaugtherGUID;          // 屠宰场门GUID
            ObjectGuid portElderGUID;              // 长者大门GUID
            ObjectGuid ysidaCageGUID;              // 伊希达笼子GUID

            // 生物GUID
            ObjectGuid baronGUID;                  // 巴隆·瑞文戴尔GUID
            ObjectGuid ysidaGUID;                  // 伊希达GUID
            ObjectGuid ysidaTriggerGUID;           // 伊希达触发器GUID
            GuidSet crystalsGUID;                  // 水晶生物GUID集合
            GuidSet abomnationGUID;                // 憎恶生物GUID集合
            EventMap events;                       // 事件调度映射

            std::array<GateTrapData, 2> TrapGates; // 陷阱门数据数组(血色侧和亡灵侧)

            /**
             * @brief 单位死亡处理
             * @param who 死亡的单位
             *
             * @details 当单位死亡时调用,处理特殊逻辑:
             *          1. 血色十字军死亡: 检查是否满足提米刷新条件
             *          2. 感染鼠死亡: 检查是否是陷阱门召唤的鼠,若是则打开门
             */
            void OnUnitDeath(Unit* who) override
            {
                switch (who->GetEntry())
                {
                    case NPC_CRIMSON_GUARDSMAN:     // 血色卫士
                    case NPC_CRIMSON_CONJUROR:      // 血色咒术师
                    case NPC_CRIMSON_INITATE:       // 血色新手
                    case NPC_CRIMSON_GALLANT:       // 血色骑士
                    {
                        // 检查提米是否已刷新
                        if (!timmySpawned)
                        {
                            Position pos = who->ToCreature()->GetHomePosition();
                            // 检查是否在血色入口区域
                            if (beforeScarletGate.IsWithinBoundary(pos))
                            {
                                // 增加击杀计数,达到要求则刷新提米
                                if (++scarletsKilled >= TIMMY_THE_CRUEL_CRUSADERS_REQUIRED)
                                {
                                    instance->SummonCreature(NPC_TIMMY_THE_CRUEL, timmyTheCruelSpawnPosition);
                                    timmySpawned = true;
                                }
                            }
                        }
                        break;
                    }
                    case NPC_PLAGUED_RAT:  // 感染鼠
                    {
                        // 检查是否是陷阱门召唤的鼠
                        for (GateTrapData& trapGate : TrapGates)
                        {
                            auto el = trapGate.Rats.find(who->GetGUID());
                            if (el != trapGate.Rats.end())
                            {
                                // 从集合中移除并打开门
                                trapGate.Rats.erase(el);
                                for (ObjectGuid gate : trapGate.Gates)
                                    UpdateGoState(gate, GO_STATE_ACTIVE);
                            }
                        }
                        break;
                    }
                }
            }

            /**
             * @brief 启动屠宰场事件
             * @return 是否成功启动
             *
             * @details 检查三个水晶(男爵夫人、奈幽布、苍白者)是否已激活:
             *          - 如果都已激活,打开屠宰场门和死亡奔跑门
             *          - 注意: 目前使用IN_PROGRESS,水晶实现后应改为DONE
             */
            bool StartSlaugtherSquare()
            {
                //change to DONE when crystals implemented
                // 水晶实现后应改为DONE
                if (EncounterState[1] == IN_PROGRESS && EncounterState[2] == IN_PROGRESS && EncounterState[3] == IN_PROGRESS)
                {
                    HandleGameObject(portGauntletGUID, true);
                    HandleGameObject(portSlaugtherGUID, true);
                    return true;
                }

                TC_LOG_DEBUG("scripts", "Instance Stratholme: Cannot open slaugther square yet.");
                return false;
            }

            /**
             * @brief 更新游戏对象状态
             * @param goGuid 游戏对象GUID
             * @param newState 新状态
             * @param restoreTime 恢复时间(毫秒),非0则会在指定时间后恢复原状态
             *
             * @details 更新游戏对象的状态:
             *          - 如果restoreTime为0: 直接设置状态
             *          - 如果restoreTime非0: 使用门/按钮机制,会在指定时间后自动恢复
             */
            //if restoreTime is not 0, then newState will be ignored and GO should be restored to original state after "restoreTime" millisecond
            void UpdateGoState(ObjectGuid goGuid, uint32 newState, uint32 restoreTime = 0u)
            {
                if (!goGuid)
                    return;
                if (GameObject* go = instance->GetGameObject(goGuid))
                {
                    if (restoreTime)
                        go->UseDoorOrButton(restoreTime);
                    else
                        go->SetGoState((GOState)newState);
                }
            }

            /**
             * @brief 执行陷阱门机制
             * @param type 陷阱门类型(血色侧或亡灵侧)
             * @param where 触发陷阱的单位
             *
             * @details 陷阱门触发逻辑:
             *          1. 关闭陷阱门(20秒后自动打开)
             *          2. 召唤30只感染鼠围攻玩家
             *          3. 标记陷阱已触发
             *          玩家需要击杀所有老鼠才能打开门
             */
            void DoGateTrap(StratholmeGateTrapType type, Unit* where)
            {
                // close the gate, but in two minutes it will open on its own
                // 关闭门,但20秒后会自动打开
                for (ObjectGuid trapGateGuid : TrapGates[AsUnderlyingType(type)].Gates)
                    UpdateGoState(trapGateGuid, GO_STATE_READY, 20 * IN_MILLISECONDS);

                // 召唤30只感染鼠
                for (uint8 i = 0; i < 30; ++i)
                {
                    Position summonPos = where->GetRandomPoint(GateTrapPos[AsUnderlyingType(type)], 5.0f);
                    if (Creature* creature = where->SummonCreature(NPC_PLAGUED_RAT, summonPos, TEMPSUMMON_DEAD_DESPAWN, 0s))
                    {
                        TrapGates[AsUnderlyingType(type)].Rats.insert(creature->GetGUID());
                        creature->EngageWithTarget(where);  // 让老鼠攻击玩家
                    }
                }

                TrapGates[AsUnderlyingType(type)].Triggered = true;
            }

            /**
             * @brief 生物创建处理
             * @param creature 新创建的生物
             *
             * @details 当生物在副本中创建时调用,保存重要生物的GUID:
             *          - 巴隆: 最终Boss
             *          - 伊希达触发器: 用于触发伊希达事件
             *          - 水晶: 三个灵通的水晶NPC
             *          - 憎恶: 屠宰场前的憎恶群
             *          - 伊希达: 救援任务NPC
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_BARON:
                        baronGUID = creature->GetGUID();
                        break;
                    case NPC_YSIDA_TRIGGER:
                        ysidaTriggerGUID = creature->GetGUID();
                        break;
                    case NPC_CRYSTAL:
                        crystalsGUID.insert(creature->GetGUID());
                        break;
                    case NPC_ABOM_BILE:    // 胆汁憎恶
                    case NPC_ABOM_VENOM:   // 毒液憎恶
                        abomnationGUID.insert(creature->GetGUID());
                        break;
                    case NPC_YSIDA:
                        ysidaGUID = creature->GetGUID();
                        // 移除任务给予者标记,任务在救援成功后才可接取
                        creature->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                        break;
                }
            }

            /**
             * @brief 生物移除处理
             * @param creature 被移除的生物
             *
             * @details 当生物从副本中移除时调用,从相应集合中删除GUID
             */
            void OnCreatureRemove(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_CRYSTAL:
                        crystalsGUID.erase(creature->GetGUID());
                        break;
                    case NPC_ABOM_BILE:
                    case NPC_ABOM_VENOM:
                        abomnationGUID.erase(creature->GetGUID());
                        break;
                }
            }

            /**
             * @brief 游戏对象创建处理
             * @param go 新创建的游戏对象
             *
             * @details 当游戏对象在副本中创建时调用:
             *          - 保存重要门和对象的GUID
             *          - 根据副本进度状态设置门的开关状态
             *          - 处理陷阱门注册
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_SERVICE_ENTRANCE:  // 服务入口
                        serviceEntranceGUID = go->GetGUID();
                        break;
                    case GO_GAUNTLET_GATE1:  // 死亡奔跑第一道门
                        //weird, but unless flag is set, client will not respond as expected. DB bug?
                        // 奇怪,但除非设置此标志,否则客户端不会按预期响应。数据库Bug?
                        go->SetFlag(GO_FLAG_LOCKED);
                        gauntletGate1GUID = go->GetGUID();
                        break;
                    case GO_ZIGGURAT1:  // 灵通1(男爵夫人)
                        ziggurat1GUID = go->GetGUID();
                        if (GetData(TYPE_BARONESS) == IN_PROGRESS)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_ZIGGURAT2:  // 灵通2(奈幽布)
                        ziggurat2GUID = go->GetGUID();
                        if (GetData(TYPE_NERUB) == IN_PROGRESS)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_ZIGGURAT3:  // 灵通3(苍白者)
                        ziggurat3GUID = go->GetGUID();
                        if (GetData(TYPE_PALLID) == IN_PROGRESS)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_ZIGGURAT4:  // 灵通4(通往巴隆)
                        ziggurat4GUID = go->GetGUID();
                        if (GetData(TYPE_BARON) == DONE || GetData(TYPE_RAMSTEIN) == DONE)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_ZIGGURAT5:  // 灵通5(通往巴隆)
                        ziggurat5GUID = go->GetGUID();
                        if (GetData(TYPE_BARON) == DONE || GetData(TYPE_RAMSTEIN) == DONE)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_PORT_GAUNTLET:  // 死亡奔跑门
                        portGauntletGUID = go->GetGUID();
                        if (GetData(TYPE_BARONESS) == IN_PROGRESS && GetData(TYPE_NERUB) == IN_PROGRESS && GetData(TYPE_PALLID) == IN_PROGRESS)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_PORT_SLAUGTHER:  // 屠宰场门
                        portSlaugtherGUID = go->GetGUID();
                        if (GetData(TYPE_BARONESS) == IN_PROGRESS && GetData(TYPE_NERUB) == IN_PROGRESS && GetData(TYPE_PALLID) == IN_PROGRESS)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;
                    case GO_PORT_ELDERS:  // 长者大门
                        portElderGUID = go->GetGUID();
                        break;
                    case GO_YSIDA_CAGE:  // 伊希达笼子
                        ysidaCageGUID = go->GetGUID();
                        break;
                    case GO_PORT_TRAP_GATE_1:  // 陷阱门1(血色侧)
                        TrapGates[AsUnderlyingType(StratholmeGateTrapType::ScaletSide)].Gates[0] = go->GetGUID();
                        break;
                    case GO_PORT_TRAP_GATE_2:  // 陷阱门2(血色侧)
                        TrapGates[AsUnderlyingType(StratholmeGateTrapType::ScaletSide)].Gates[1] = go->GetGUID();
                        break;
                    case GO_PORT_TRAP_GATE_3:  // 陷阱门3(亡灵侧)
                        TrapGates[AsUnderlyingType(StratholmeGateTrapType::UndeadSide)].Gates[0] = go->GetGUID();
                        break;
                    case GO_PORT_TRAP_GATE_4:  // 陷阱门4(亡灵侧)
                        TrapGates[AsUnderlyingType(StratholmeGateTrapType::UndeadSide)].Gates[1] = go->GetGUID();
                        break;
                }
            }

            /**
             * @brief 设置副本数据
             * @param type 数据类型(Boss战状态类型)
             * @param data 数据值(状态值)
             *
             * @details 设置副本中各种状态,包括:
             *          - 巴隆救援计时状态(45分钟任务)
             *          - 三个水晶Boss状态(男爵夫人、奈幽布、苍白者)
             *          - 拉姆斯登和巴隆战状态
             *          - 白银之手骑士死亡状态(任务相关)
             *
             * @par 状态说明:
             *          - NOT_STARTED: 未开始
             *          - IN_PROGRESS: 进行中(或已完成,水晶相关)
             *          - DONE: 完成
             *          - FAIL: 失败
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case TYPE_BARON_RUN:  // 巴隆救援计时事件
                        switch (data)
                        {
                            case IN_PROGRESS:
                                // 如果已在进行或已失败,不重复启动
                                if (EncounterState[0] == IN_PROGRESS || EncounterState[0] == FAIL)
                                    break;
                                EncounterState[0] = data;
                                // 启动45分钟倒计时
                                events.ScheduleEvent(EVENT_BARON_RUN, 45min);
                                TC_LOG_DEBUG("scripts", "Instance Stratholme: Baron run in progress.");
                                break;
                            case FAIL:
                                // 移除玩家身上的最后通牒光环
                                DoRemoveAurasDueToSpellOnPlayers(SPELL_BARON_ULTIMATUM);
                                // 让伊希达死亡(装死)
                                if (Creature* ysida = instance->GetCreature(ysidaGUID))
                                    ysida->CastSpell(ysida, SPELL_PERM_FEIGN_DEATH, true);
                                EncounterState[0] = data;
                                break;
                            case DONE:
                                EncounterState[0] = data;

                                if (Creature* ysida = instance->GetCreature(ysidaGUID))
                                {
                                    // 打开笼子
                                    if (GameObject* cage = instance->GetGameObject(ysidaCageGUID))
                                        cage->UseDoorOrButton();

                                    float x, y, z;
                                    //! This spell handles the Dead man's plea quest completion
                                    // 此法术处理死者的恳求任务完成
                                    ysida->CastSpell(nullptr, SPELL_YSIDA_SAVED, true);
                                    ysida->SetWalk(true);
                                    ysida->AI()->Talk(SAY_YSIDA_SAVED);
                                    ysida->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                                    // 获取伊希达前方的点并移动
                                    ysida->GetClosePoint(x, y, z, ysida->GetObjectScale() / 3, 4.0f);
                                    ysida->GetMotionMaster()->MovePoint(1, x, y, z);

                                    // 给所有玩家施放任务完成法术
                                    Map::PlayerList const& players = instance->GetPlayers();

                                    for (auto const& i : players)
                                    {
                                        if (Player* player = i.GetSource())
                                        {
                                            if (player->IsGameMaster())
                                                continue;

                                            //! im not quite sure what this one is supposed to do
                                            //! this is server-side spell
                                            // 不太确定这个法术的作用,这是服务器端法术
                                            player->CastSpell(ysida, SPELL_YSIDA_CREDIT_EFFECT, true);
                                        }
                                    }
                                }
                                // 取消救援计时事件
                                events.CancelEvent(EVENT_BARON_RUN);
                                break;
                        }
                        break;
                    case TYPE_BARONESS:  // 男爵夫人(水晶1)
                        EncounterState[1] = data;
                        if (data == IN_PROGRESS)
                        {
                            HandleGameObject(ziggurat1GUID, true);
                            //change to DONE when crystals implemented
                            // 水晶实现后应改为DONE
                            StartSlaugtherSquare();
                        }
                        break;
                    case TYPE_NERUB:  // 奈幽布(水晶2)
                        EncounterState[2] = data;
                        if (data == IN_PROGRESS)
                        {
                            HandleGameObject(ziggurat2GUID, true);
                            //change to DONE when crystals implemented
                            StartSlaugtherSquare();
                        }
                        break;
                    case TYPE_PALLID:  // 苍白者(水晶3)
                        EncounterState[3] = data;
                        if (data == IN_PROGRESS)
                        {
                            HandleGameObject(ziggurat3GUID, true);
                            //change to DONE when crystals implemented
                            StartSlaugtherSquare();
                        }
                        break;
                    case TYPE_RAMSTEIN:  // 拉姆斯登
                        if (data == IN_PROGRESS)
                        {
                            // 关闭死亡奔跑门
                            HandleGameObject(portGauntletGUID, false);

                            // 统计存活的憎恶数量
                            uint32 count = abomnationGUID.size();
                            for (GuidSet::const_iterator i = abomnationGUID.begin(); i != abomnationGUID.end(); ++i)
                            {
                                if (Creature* pAbom = instance->GetCreature(*i))
                                    if (!pAbom->IsAlive())
                                        --count;
                            }

                            // 如果所有憎恶已死亡,刷新拉姆斯登
                            if (!count)
                            {
                                //a bit itchy, it should close the door after 10 secs, but it doesn't. skipping it for now.
                                // 有点棘手,应该在10秒后关门,但实际没有。暂时跳过。
                                //UpdateGoState(ziggurat4GUID, 0, true);
                                if (Creature* pBaron = instance->GetCreature(baronGUID))
                                    pBaron->SummonCreature(NPC_RAMSTEIN, 4032.84f, -3390.24f, 119.73f, 4.71f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30min);
                                TC_LOG_DEBUG("scripts", "Instance Stratholme: Ramstein spawned.");
                            }
                            else
                                TC_LOG_DEBUG("scripts", "Instance Stratholme: {} Abomnation left to kill.", count);
                        }

                        if (data == NOT_STARTED)
                            HandleGameObject(portGauntletGUID, true);

                        if (data == DONE)
                        {
                            // 拉姆斯登死后1分钟刷新黑卫士
                            events.ScheduleEvent(EVENT_SLAUGHTER_SQUARE, 1min);
                            TC_LOG_DEBUG("scripts", "Instance Stratholme: Slaugther event will continue in 1 minute.");
                        }
                        EncounterState[4] = data;
                        break;
                    case TYPE_BARON:  // 巴隆·瑞文戴尔
                        if (data == IN_PROGRESS)
                        {
                            // 关闭通往巴隆的门
                            HandleGameObject(ziggurat4GUID, false);
                            HandleGameObject(ziggurat5GUID, false);
                        }
                        if (data == DONE || data == NOT_STARTED)
                        {
                            // 打开通往巴隆的门
                            HandleGameObject(ziggurat4GUID, true);
                            HandleGameObject(ziggurat5GUID, true);
                        }
                        if (data == DONE)
                        {
                            HandleGameObject(portGauntletGUID, true);
                            // 如果救援计时还在进行,移除光环并完成
                            if (GetData(TYPE_BARON_RUN) == IN_PROGRESS)
                                DoRemoveAurasDueToSpellOnPlayers(SPELL_BARON_ULTIMATUM);

                            SetData(TYPE_BARON_RUN, DONE);
                        }
                        EncounterState[5] = data;
                        break;
                    case TYPE_SH_AELMAR:  // 白银之手骑士 - 埃尔玛
                        IsSilverHandDead[0] = (data) ? true : false;
                        break;
                    case TYPE_SH_CATHELA:  // 白银之手骑士 - 卡瑟拉
                        IsSilverHandDead[1] = (data) ? true : false;
                        break;
                    case TYPE_SH_GREGOR:  // 白银之手骑士 - 格雷戈
                        IsSilverHandDead[2] = (data) ? true : false;
                        break;
                    case TYPE_SH_NEMAS:  // 白银之手骑士 - 尼玛斯
                        IsSilverHandDead[3] = (data) ? true : false;
                        break;
                    case TYPE_SH_VICAR:  // 白银之手骑士 - 维卡
                        IsSilverHandDead[4] = (data) ? true : false;
                        break;
                }

                // 如果状态为完成,保存到数据库
                if (data == DONE)
                    SaveToDB();
            }

            /**
             * @brief 获取保存数据
             * @return 序列化的副本状态字符串
             *
             * @details 将副本状态序列化为字符串用于保存到数据库:
             *          - 保存6个Boss战状态
             *          - 格式: "状态0 状态1 状态2 状态3 状态4 状态5"
             */
            std::string GetSaveData() override
            {
                OUT_SAVE_INST_DATA;

                std::ostringstream saveStream;
                saveStream << EncounterState[0] << ' ' << EncounterState[1] << ' ' << EncounterState[2] << ' '
                    << EncounterState[3] << ' ' << EncounterState[4] << ' ' << EncounterState[5];

                OUT_SAVE_INST_DATA_COMPLETE;
                return saveStream.str();
            }

            /**
             * @brief 加载保存的数据
             * @param in 序列化的副本状态字符串
             *
             * @details 从数据库加载副本状态:
             *          - 解析保存的状态字符串
             *          - 恢复Boss战状态
             *          - 重置进行中的战斗为未开始(防止卡住)
             *          - 注意: 状态1、2、3不会重置,因为水晶尚未实现
             */
            void Load(char const* in) override
            {
                if (!in)
                {
                    OUT_LOAD_INST_DATA_FAIL;
                    return;
                }

                OUT_LOAD_INST_DATA(in);

                std::istringstream loadStream(in);
                loadStream >> EncounterState[0] >> EncounterState[1] >> EncounterState[2] >> EncounterState[3]
                >> EncounterState[4] >> EncounterState[5];

                // Do not reset 1, 2 and 3. they are not set to done, yet .
                // 不重置状态1、2、3,它们尚未设置为完成
                if (EncounterState[0] == IN_PROGRESS)
                    EncounterState[0] = NOT_STARTED;
                if (EncounterState[4] == IN_PROGRESS)
                    EncounterState[4] = NOT_STARTED;
                if (EncounterState[5] == IN_PROGRESS)
                    EncounterState[5] = NOT_STARTED;

                OUT_LOAD_INST_DATA_COMPLETE;
            }

            /**
             * @brief 获取副本数据
             * @param type 数据类型
             * @return 对应的数据值
             *
             * @details 查询副本状态:
             *          - 白银之手任务: 检查所有骑士是否死亡
             *          - 各种Boss战状态
             */
            uint32 GetData(uint32 type) const override
            {
                  switch (type)
                  {
                      case TYPE_SH_QUEST:  // 白银之手任务
                          // 检查所有5个骑士是否都已死亡
                          if (IsSilverHandDead[0] && IsSilverHandDead[1] && IsSilverHandDead[2] && IsSilverHandDead[3] && IsSilverHandDead[4])
                              return 1;
                          return 0;
                      case TYPE_BARON_RUN:  // 巴隆救援计时
                          return EncounterState[0];
                      case TYPE_BARONESS:  // 男爵夫人
                          return EncounterState[1];
                      case TYPE_NERUB:  // 奈幽布
                          return EncounterState[2];
                      case TYPE_PALLID:  // 苍白者
                          return EncounterState[3];
                      case TYPE_RAMSTEIN:  // 拉姆斯登
                          return EncounterState[4];
                      case TYPE_BARON:  // 巴隆
                          return EncounterState[5];
                  }
                  return 0;
            }

            /**
             * @brief 获取GUID数据
             * @param data 数据类型
             * @return 对应的生物GUID
             *
             * @details 根据数据类型返回对应的生物GUID
             */
            ObjectGuid GetGuidData(uint32 data) const override
            {
                switch (data)
                {
                    case DATA_BARON:
                        return baronGUID;
                    case DATA_YSIDA_TRIGGER:
                        return ysidaTriggerGUID;
                    case NPC_YSIDA:
                        return ysidaGUID;
                }
                return ObjectGuid::Empty;
            }

            /**
             * @brief 更新实例
             * @param diff 距离上次更新的时间差(毫秒)
             *
             * @details 主循环逻辑,定期调用:
             *          - 更新事件调度器
             *          - 处理事件:
             *            1. 巴隆救援计时到期: 如果未完成则标记失败
             *            2. 屠宰场事件: 拉姆斯登死后刷新4个黑卫士
             *            3. 陷阱门检测: 检查玩家是否靠近陷阱门并触发
             */
            void Update(uint32 diff) override
            {
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_BARON_RUN:  // 巴隆救援计时到期
                            // 如果救援未完成,标记失败
                            if (GetData(TYPE_BARON_RUN) != DONE)
                                SetData(TYPE_BARON_RUN, FAIL);
                            TC_LOG_DEBUG("scripts", "Instance Stratholme: Baron run event reached end. Event has state {}.", GetData(TYPE_BARON_RUN));
                            break;
                        case EVENT_SLAUGHTER_SQUARE:  // 屠宰场事件
                            // 刷新4个黑卫士并打开通往巴隆的门
                            if (Creature* baron = instance->GetCreature(baronGUID))
                            {
                                for (uint8 i = 0; i < 4; ++i)
                                    baron->SummonCreature(NPC_BLACK_GUARD, 4032.84f, -3390.24f, 119.73f, 4.71f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30min);

                                HandleGameObject(ziggurat4GUID, true);
                                HandleGameObject(ziggurat5GUID, true);
                                TC_LOG_DEBUG("scripts", "Instance Stratholme: Black guard sentries spawned. Opening gates to baron.");
                            }
                            break;
                        case EVENT_RAT_TRAP_CLOSE:  // 陷阱门检测
                        {
                            // 遍历所有陷阱门位置
                            for (uint8 i = 0; i < std::size(GateTrapPos); ++i)
                            {
                                // 如果陷阱已触发,跳过
                                if (TrapGates[i].Triggered)
                                    continue;

                                Position const* gateTrapPos = &GateTrapPos[i];
                                // Check that the trap is not on cooldown, if so check if player/pet is in range
                                // 检查陷阱是否在冷却中,若否则检查玩家/宠物是否在范围内
                                for (MapReference const& itr : instance->GetPlayers())
                                {
                                    Player* player = itr.GetSource();
                                    // 跳过GM
                                    if (player->IsGameMaster())
                                        continue;

                                    // 检查玩家是否在陷阱范围内(5.5码)
                                    if (player->IsWithinDist2d(gateTrapPos, 5.5f))
                                    {
                                        DoGateTrap(StratholmeGateTrapType(i), player);
                                        break;
                                    }

                                    // 检查宠物是否在陷阱范围内
                                    Pet* pet = player->GetPet();
                                    if (pet && pet->IsWithinDist2d(gateTrapPos, 5.5f))
                                    {
                                        DoGateTrap(StratholmeGateTrapType(i), pet);
                                        break;
                                    }
                                }

                            }
                            //if you haven't already fallen into the trap, update it
                            // 如果还有未触发的陷阱,继续检测
                            if (std::any_of(TrapGates.begin(), TrapGates.end(), [](GateTrapData const& trap) { return !trap.Triggered; }))
                                events.ScheduleEvent(EVENT_RAT_TRAP_CLOSE, 1s);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本指针
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_stratholme_InstanceMapScript(map);
        }
};

/**
 * @brief 注册脚本
 *
 * @details 将斯坦索姆实例脚本注册到脚本系统
 */
void AddSC_instance_stratholme()
{
    new instance_stratholme();
}
