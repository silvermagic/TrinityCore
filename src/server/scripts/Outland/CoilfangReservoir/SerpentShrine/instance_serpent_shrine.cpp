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
 * @file instance_serpent_shrine.cpp
 * @brief 毒蛇神殿副本实例脚本
 *
 * 本文件实现了毒蛇神殿副本的实例管理逻辑，包括：
 * - BOSS战斗状态管理（6个BOSS）
 * - 副本内游戏对象管理（控制台、桥梁）
 * - 水面状态管理（沸腾水/鱼人群）
 * - 瓦斯琪BOSS战护盾发生器状态追踪
 * - 副本小怪击杀计数
 *
 * 副本BOSS列表：
 * 0 - 不稳定的海度斯（Hydross The Unstable）
 * 1 - 盲眼者莱欧瑟拉斯（Leotheras The Blind）
 * 2 - 潜伏者（The Lurker Below）
 * 3 - 深水领主卡拉瑟雷斯（Fathom-Lord Karathress）
 * 4 - 莫洛格里·踏潮者（Morogrim Tidewalker）
 * 5 - 瓦斯琪女士（Lady Vashj）
 *
 * @see serpent_shrine.h 副本相关定义头文件
 */

/* ScriptData
SDName: Instance_Serpent_Shrine
SD%Complete: 100
SDComment: Instance Data Scripts and functions to acquire mobs and set encounter status for use in various Serpent Shrine Scripts
SDCategory: Coilfang Resevoir, Serpent Shrine Cavern
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "serpent_shrine.h"
#include "TemporarySummon.h"

// 副本中BOSS遭遇战数量
#define MAX_ENCOUNTER 6

/**
 * @brief 副本杂项枚举定义
 */
enum Misc
{
    // 法术ID
    SPELL_SCALDINGWATER             = 37284,  // 沸腾水法术（对水中玩家造成伤害）

    // 生物ID
    NPC_COILFANG_FRENZY             = 21508,  // 盘牙狂鱼（水中生成的攻击性鱼群）
    NPC_COILFANG_PRIESTESS          = 21220,  // 盘牙女祭司（平台上的小怪）
    NPC_COILFANG_SHATTERER          = 21301,  // 盘牙粉碎者（平台上的小怪）

    // 杂项
    MIN_KILLS                       = 30      // 最小击杀数量（激活沸腾水需要击杀的平台小怪数）
};

/**
 * 平台小怪说明：
 * 副本中有6个平台，每个平台有3个粉碎者和2个女祭司，总共30个精英怪
 * 击杀全部30个平台小怪后，水中的鱼群将停止生成，水面变为沸腾状态
 * 只计算平台上的小怪，其他位置的小怪不计入
 */

/* Serpentshrine cavern encounters:
0 - Hydross The Unstable event
1 - Leotheras The Blind Event
2 - The Lurker Below Event
3 - Fathom-Lord Karathress Event
4 - Morogrim Tidewalker Event
5 - Lady Vashj Event
*/

class go_bridge_console : public GameObjectScript
{
    public:
        go_bridge_console() : GameObjectScript("go_bridge_console") { }

        struct go_bridge_consoleAI : public GameObjectAI
        {
            go_bridge_consoleAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;

            bool OnGossipHello(Player* /*player*/) override
            {
                if (instance)
                    instance->SetData(DATA_CONTROL_CONSOLE, DONE);
                return true;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetSerpentshrineCavernAI<go_bridge_consoleAI>(go);
        }
};

class instance_serpent_shrine : public InstanceMapScript
{
    public:
        instance_serpent_shrine() : InstanceMapScript(SSCScriptName, 548) { }

        struct instance_serpentshrine_cavern_InstanceMapScript : public InstanceScript
        {
            instance_serpentshrine_cavern_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(MAX_ENCOUNTER);

                StrangePool = 0;
                Water = WATERSTATE_FRENZY;

                ShieldGeneratorDeactivated[0] = false;
                ShieldGeneratorDeactivated[1] = false;
                ShieldGeneratorDeactivated[2] = false;
                ShieldGeneratorDeactivated[3] = false;
                FishingTimer = 1000;
                WaterCheckTimer = 500;
                FrenzySpawnTimer = 2000;
                DoSpawnFrenzy = false;
                TrashCount = 0;
            }

            void Update(uint32 diff) override
            {
                //Water checks
                if (WaterCheckTimer <= diff)
                {
                    if (TrashCount >= MIN_KILLS)
                        Water = WATERSTATE_SCALDING;
                    else
                        Water = WATERSTATE_FRENZY;

                    Map::PlayerList const& PlayerList = instance->GetPlayers();
                    if (PlayerList.isEmpty())
                        return;
                    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                    {
                        if (Player* player = i->GetSource())
                        {
                            if (player->IsAlive() && /*i->GetSource()->GetPositionZ() <= -21.434931f*/player->IsInWater())
                            {
                                if (Water == WATERSTATE_SCALDING)
                                {
                                    if (!player->HasAura(SPELL_SCALDINGWATER))
                                        player->CastSpell(player, SPELL_SCALDINGWATER, true);

                                }
                                else
                                {
                                    //spawn frenzy
                                    if (DoSpawnFrenzy)
                                    {
                                        if (Creature* frenzy = player->SummonCreature(NPC_COILFANG_FRENZY, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 2s))
                                        {
                                            frenzy->Attack(player, false);
                                            frenzy->SetSwim(true);
                                            frenzy->SetDisableGravity(true);
                                        }
                                        DoSpawnFrenzy = false;
                                    }
                                }
                            }
                            if (!player->IsInWater())
                                player->RemoveAurasDueToSpell(SPELL_SCALDINGWATER);
                        }

                    }
                    WaterCheckTimer = 500;//remove stress from core
                }
                else
                    WaterCheckTimer -= diff;

                if (FrenzySpawnTimer <= diff)
                {
                    DoSpawnFrenzy = true;
                    FrenzySpawnTimer = 2000;
                }
                else
                    FrenzySpawnTimer -= diff;
            }

            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case 184568:
                        ControlConsole = go->GetGUID();
                        go->setActive(true);
                        go->SetFarVisible(true);
                        break;
                    case 184203:
                        BridgePart[0] = go->GetGUID();
                        go->setActive(true);
                        go->SetFarVisible(true);
                        break;
                    case 184204:
                        BridgePart[1] = go->GetGUID();
                        go->setActive(true);
                        go->SetFarVisible(true);
                        break;
                    case 184205:
                        BridgePart[2] = go->GetGUID();
                        go->setActive(true);
                        go->SetFarVisible(true);
                        break;
                    default:
                        break;
                }
            }

            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case 21212:
                        LadyVashj = creature->GetGUID();
                        break;
                    case 21214:
                        Karathress = creature->GetGUID();
                        break;
                    case 21966:
                        Sharkkis = creature->GetGUID();
                        break;
                    case 21217:
                        LurkerBelow = creature->GetGUID();
                        break;
                    case 21965:
                        Tidalvess = creature->GetGUID();
                        break;
                    case 21964:
                        Caribdis = creature->GetGUID();
                        break;
                    case 21215:
                        LeotherasTheBlind = creature->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            void SetGuidData(uint32 type, ObjectGuid data) override
            {
                if (type == DATA_KARATHRESSEVENT_STARTER)
                    KarathressEvent_Starter = data;
                if (type == DATA_LEOTHERAS_EVENT_STARTER)
                    LeotherasEventStarter = data;
            }

            ObjectGuid GetGuidData(uint32 identifier) const override
            {
                switch (identifier)
                {
                    case DATA_THELURKERBELOW:
                        return LurkerBelow;
                    case DATA_SHARKKIS:
                        return Sharkkis;
                    case DATA_TIDALVESS:
                        return Tidalvess;
                    case DATA_CARIBDIS:
                        return Caribdis;
                    case DATA_LADYVASHJ:
                        return LadyVashj;
                    case DATA_KARATHRESS:
                        return Karathress;
                    case DATA_KARATHRESSEVENT_STARTER:
                        return KarathressEvent_Starter;
                    case DATA_LEOTHERAS:
                        return LeotherasTheBlind;
                    case DATA_LEOTHERAS_EVENT_STARTER:
                        return LeotherasEventStarter;
                    default:
                        break;
                }
                return ObjectGuid::Empty;
            }

            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_STRANGE_POOL:
                        StrangePool = data;
                        break;
                    case DATA_CONTROL_CONSOLE:
                        if (data == DONE)
                        {
                            HandleGameObject(BridgePart[0], true);
                            HandleGameObject(BridgePart[1], true);
                            HandleGameObject(BridgePart[2], true);
                        }
                        break;
                    case DATA_TRASH:
                        if (data == 1 && TrashCount < MIN_KILLS)
                            ++TrashCount;//+1 died
                        SaveToDB();
                        break;
                    case DATA_WATER:
                        Water = data;
                        break;
                    case DATA_SHIELDGENERATOR1:
                        ShieldGeneratorDeactivated[0] = data != 0;
                        break;
                    case DATA_SHIELDGENERATOR2:
                        ShieldGeneratorDeactivated[1] = data != 0;
                        break;
                    case DATA_SHIELDGENERATOR3:
                        ShieldGeneratorDeactivated[2] = data != 0;
                        break;
                    case DATA_SHIELDGENERATOR4:
                        ShieldGeneratorDeactivated[3] = data != 0;
                        break;
                    default:
                        break;
                }
            }

            bool SetBossState(uint32 id, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(id, state))
                    return false;

                if (id == BOSS_LADY_VASHJ && state == NOT_STARTED)
                {
                    ShieldGeneratorDeactivated[0] = false;
                    ShieldGeneratorDeactivated[1] = false;
                    ShieldGeneratorDeactivated[2] = false;
                    ShieldGeneratorDeactivated[3] = false;
                }

                return true;
            }

            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_SHIELDGENERATOR1:
                        return ShieldGeneratorDeactivated[0];
                    case DATA_SHIELDGENERATOR2:
                        return ShieldGeneratorDeactivated[1];
                    case DATA_SHIELDGENERATOR3:
                        return ShieldGeneratorDeactivated[2];
                    case DATA_SHIELDGENERATOR4:
                        return ShieldGeneratorDeactivated[3];
                    case DATA_CANSTARTPHASE3:
                        if (ShieldGeneratorDeactivated[0] && ShieldGeneratorDeactivated[1] && ShieldGeneratorDeactivated[2] && ShieldGeneratorDeactivated[3])
                            return 1;
                        break;
                    case DATA_STRANGE_POOL:
                        return StrangePool;
                    case DATA_WATER:
                        return Water;
                    default:
                        break;
                }

                return 0;
            }

            void WriteSaveDataMore(std::ostringstream& stream) override
            {
                stream << TrashCount;
            }

            void ReadSaveDataMore(std::istringstream& stream) override
            {
                stream >> TrashCount;
            }

        private:
            ObjectGuid LurkerBelow;
            ObjectGuid Sharkkis;
            ObjectGuid Tidalvess;
            ObjectGuid Caribdis;
            ObjectGuid LadyVashj;
            ObjectGuid Karathress;
            ObjectGuid KarathressEvent_Starter;
            ObjectGuid LeotherasTheBlind;
            ObjectGuid LeotherasEventStarter;

            ObjectGuid ControlConsole;
            ObjectGuid BridgePart[3];
            uint32 StrangePool;
            uint32 FishingTimer;
            uint32 WaterCheckTimer;
            uint32 FrenzySpawnTimer;
            uint32 Water;
            uint32 TrashCount;

            bool ShieldGeneratorDeactivated[4];
            bool DoSpawnFrenzy;
        };

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_serpentshrine_cavern_InstanceMapScript(map);
        }
};

void AddSC_instance_serpentshrine_cavern()
{
    new instance_serpent_shrine();
    new go_bridge_console();
}
