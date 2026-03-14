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
 * @file OutdoorPvPSI.cpp
 * @brief 希利苏斯户外PvP系统实现 - 希利希斯资源争夺战
 *
 * 本模块实现了希利苏斯地区的资源争夺机制：
 * - 玩家收集希利希斯尘埃并运送到己方营地
 * - 先达到200资源的阵营获得区域增益效果
 * - 增加塞纳里奥议会声望奖励
 * - 击杀敌方玩家不掉落旗帜，但会在远处生成希利希斯堆
 *
 * 主要功能：
 * - 希利希斯资源收集和运送机制
 * - 区域触发器处理资源交付
 * - 资源计数和阵营增益效果
 * - 旗帜掉落处理和希利希斯堆生成
 */

#include "OutdoorPvPSI.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Language.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldStatePackets.h"

/** 最大资源数量，先达到此数量的阵营获得增益效果 */
uint32 const SI_MAX_RESOURCES = 200;

/** 联盟资源交付区域触发器ID */
uint32 const SI_AREATRIGGER_H = 4168;

/** 部落资源交付区域触发器ID */
uint32 const SI_AREATRIGGER_A = 4162;

/** 联盟任务完成积分标记 */
uint32 const SI_TURNIN_QUEST_CM_A = 17090;

/** 部落任务完成积分标记 */
uint32 const SI_TURNIN_QUEST_CM_H = 18199;

/** 希利希斯堆游戏对象ID */
uint32 const SI_SILITHYST_MOUND = 181597;

/** 受增益效果影响的区域数量 */
uint8 const OutdoorPvPSIBuffZonesNum = 3;

/** 受增益效果影响的区域ID数组 */
uint32 const OutdoorPvPSIBuffZones[OutdoorPvPSIBuffZonesNum] = { 1377, 3428, 3429 };

/**
 * @brief 构造函数 - 初始化希利苏斯户外PvP实例
 */
OutdoorPvPSI::OutdoorPvPSI()
{
    m_TypeId = OUTDOOR_PVP_SI;
    m_Gathered_A = 0;      // 联盟收集的资源数量
    m_Gathered_H = 0;      // 部落收集的资源数量
    m_LastController = 0;  // 最后控制者阵营
}

void OutdoorPvPSI::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(SI_GATHERED_A, m_Gathered_A);
    packet.Worldstates.emplace_back(SI_GATHERED_H, m_Gathered_H);
    packet.Worldstates.emplace_back(SI_SILITHYST_MAX, SI_MAX_RESOURCES);
}

void OutdoorPvPSI::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(SI_GATHERED_A, 0);
    player->SendUpdateWorldState(SI_GATHERED_H, 0);
    player->SendUpdateWorldState(SI_SILITHYST_MAX, 0);
}

void OutdoorPvPSI::UpdateWorldState()
{
    SendUpdateWorldState(SI_GATHERED_A, m_Gathered_A);
    SendUpdateWorldState(SI_GATHERED_H, m_Gathered_H);
    SendUpdateWorldState(SI_SILITHYST_MAX, SI_MAX_RESOURCES);
}

bool OutdoorPvPSI::SetupOutdoorPvP()
{
    SetMapFromZone(OutdoorPvPSIBuffZones[0]);

    for (uint8 i = 0; i < OutdoorPvPSIBuffZonesNum; ++i)
        RegisterZone(OutdoorPvPSIBuffZones[i]);
    return true;
}

bool OutdoorPvPSI::Update(uint32 /*diff*/)
{
    return false;
}

void OutdoorPvPSI::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    if (player->GetTeam() == m_LastController)
        player->CastSpell(player, SI_CENARION_FAVOR, true);
    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

void OutdoorPvPSI::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    // remove buffs
    player->RemoveAurasDueToSpell(SI_CENARION_FAVOR);
    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

bool OutdoorPvPSI::HandleAreaTrigger(Player* player, uint32 trigger)
{
    switch (trigger)
    {
    case SI_AREATRIGGER_A:
        if (player->GetTeam() == ALLIANCE && player->HasAura(SI_SILITHYST_FLAG))
        {
            // remove aura
            player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG);
            ++ m_Gathered_A;
            if (m_Gathered_A >= SI_MAX_RESOURCES)
            {
                TeamApplyBuff(TEAM_ALLIANCE, SI_CENARION_FAVOR);
                /// @todo: confirm this text
                m_map->SendZoneText(OutdoorPvPSIBuffZones[0], sObjectMgr->GetTrinityStringForDBCLocale(LANG_OPVP_SI_CAPTURE_A));
                m_LastController = ALLIANCE;
                m_Gathered_A = 0;
                m_Gathered_H = 0;
            }
            UpdateWorldState();
            // reward player
            player->CastSpell(player, SI_TRACES_OF_SILITHYST, true);
            // add 19 honor
            player->RewardHonor(nullptr, 1, 19);
            // add 20 cenarion circle repu
            player->GetReputationMgr().ModifyReputation(sFactionStore.LookupEntry(609), 20);
            // complete quest
            player->KilledMonsterCredit(SI_TURNIN_QUEST_CM_A);
        }
        return true;
    case SI_AREATRIGGER_H:
        if (player->GetTeam() == HORDE && player->HasAura(SI_SILITHYST_FLAG))
        {
            // remove aura
            player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG);
            ++ m_Gathered_H;
            if (m_Gathered_H >= SI_MAX_RESOURCES)
            {
                TeamApplyBuff(TEAM_HORDE, SI_CENARION_FAVOR);
                /// @todo: confirm this text
                m_map->SendZoneText(OutdoorPvPSIBuffZones[0], sObjectMgr->GetTrinityStringForDBCLocale(LANG_OPVP_SI_CAPTURE_H));
                m_LastController = HORDE;
                m_Gathered_A = 0;
                m_Gathered_H = 0;
            }
            UpdateWorldState();
            // reward player
            player->CastSpell(player, SI_TRACES_OF_SILITHYST, true);
            // add 19 honor
            player->RewardHonor(nullptr, 1, 19);
            // add 20 cenarion circle repu
            player->GetReputationMgr().ModifyReputation(sFactionStore.LookupEntry(609), 20);
            // complete quest
            player->KilledMonsterCredit(SI_TURNIN_QUEST_CM_H);
        }
        return true;
    }
    return false;
}

bool OutdoorPvPSI::HandleDropFlag(Player* player, uint32 spellId)
{
    if (spellId == SI_SILITHYST_FLAG)
    {
        // if it was dropped away from the player's turn-in point, then create a silithyst mound, if it was dropped near the areatrigger, then it was dispelled by the outdoorpvp, so do nothing
        switch (player->GetTeam())
        {
            case ALLIANCE:
            {
                AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(SI_AREATRIGGER_A);
                if (atEntry)
                {
                    // 5.0f is safe-distance
                    if (player->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z) > 5.0f + atEntry->Radius)
                    {
                        // he dropped it further, summon mound
                        GameObject* go = new GameObject;
                        Map* map = player->GetMap();

                        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), SI_SILITHYST_MOUND, map, player->GetPhaseMask(), *player, QuaternionData(), 255, GO_STATE_READY))
                        {
                            delete go;
                            return true;
                        }

                        go->SetRespawnTime(0);

                        if (!map->AddToMap(go))
                        {
                            delete go;
                            return true;
                        }
                    }
                }
                break;
            }
            case HORDE:
            {
                AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(SI_AREATRIGGER_H);
                if (atEntry)
                {
                    // 5.0f is safe-distance
                    if (player->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z) > 5.0f + atEntry->Radius)
                    {
                        // he dropped it further, summon mound
                        GameObject* go = new GameObject;
                        Map* map = player->GetMap();

                        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), SI_SILITHYST_MOUND, map, player->GetPhaseMask(), *player, QuaternionData(), 255, GO_STATE_READY))
                        {
                            delete go;
                            return true;
                        }

                        go->SetRespawnTime(0);

                        if (!map->AddToMap(go))
                        {
                            delete go;
                            return true;
                        }
                    }
                }
                break;
            }
        }
        return true;
    }
    return false;
}

bool OutdoorPvPSI::HandleCustomSpell(Player* player, uint32 spellId, GameObject* go)
{
    if (!go || spellId != SI_SILITHYST_FLAG_GO_SPELL)
        return false;
    player->CastSpell(player, SI_SILITHYST_FLAG, true);
    if (go->GetGOInfo()->entry == SI_SILITHYST_MOUND)
    {
        // despawn go
        go->SetRespawnTime(0);
        go->Delete();
    }
    return true;
}

class OutdoorPvP_silithus : public OutdoorPvPScript
{
    public:
        OutdoorPvP_silithus() : OutdoorPvPScript("outdoorpvp_si") { }

        OutdoorPvP* GetOutdoorPvP() const override
        {
            return new OutdoorPvPSI();
        }
};

void AddSC_outdoorpvp_si()
{
    new OutdoorPvP_silithus();
}
