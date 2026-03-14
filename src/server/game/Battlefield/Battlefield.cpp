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
 * @file Battlefield.cpp
 * @brief 战场系统核心实现文件
 *
 * 本文件实现了战场系统的核心逻辑，包括：
 * - Battlefield: 战场基类实现
 * - BfCapturePoint: 据点占领系统实现
 * - BfGraveyard: 墓地管理系统实现
 *
 * 主要功能模块：
 * 1. 战斗生命周期管理（开始/结束/定时）
 * 2. 玩家管理（进入/离开/邀请/踢出）
 * 3. 据点占领机制
 * 4. 墓地和复活系统
 * 5. 团队管理系统
 */

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Battleground.h"
#include "CellImpl.h"
#include "CreatureTextMgr.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"
#include <G3D/g3dmath.h>

// ==================== Battlefield 类实现 ====================

/**
 * @brief Battlefield构造函数
 *
 * 初始化战场的基本状态和参数为默认值
 * 具体参数由子类在SetupBattlefield中设置
 */
Battlefield::Battlefield()
{
    m_Timer = 0;                                           // 初始化全局计时器
    m_IsEnabled = true;                                    // 默认启用战场
    m_isActive = false;                                    // 战斗未开始
    m_DefenderTeam = TEAM_NEUTRAL;                         // 初始无防守方

    // 基础配置参数（由子类重设）
    m_TypeId = 0;
    m_BattleId = 0;
    m_ZoneId = 0;
    m_Map = nullptr;
    m_MapId = 0;
    m_MaxPlayer = 0;
    m_MinPlayer = 0;
    m_MinLevel = 0;
    m_BattleTime = 0;
    m_NoWarBattleTime = 0;
    m_RestartAfterCrash = 0;
    m_TimeForAcceptInvite = 20;                            // 默认20秒接受邀请时间
    m_uiKickDontAcceptTimer = 1000;                        // 每秒检查一次未响应玩家

    m_uiKickAfkPlayersTimer = 1000;                        // 每秒检查一次AFK玩家

    m_LastResurrectTimer = 30 * IN_MILLISECONDS;           // 30秒复活周期
    m_StartGroupingTimer = 0;
    m_StartGrouping = false;
}

/**
 * @brief Battlefield析构函数
 *
 * 清理所有据点和墓地对象，防止内存泄漏
 */
Battlefield::~Battlefield()
{
    // 删除所有据点对象
    for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        delete itr->second;

    // 删除所有墓地对象
    for (GraveyardVect::const_iterator itr = m_GraveyardList.begin(); itr != m_GraveyardList.end(); ++itr)
        delete *itr;
}

/**
 * @brief 处理玩家进入战场区域
 * @param player 进入的玩家指针
 * @param zone 区域ID（未使用）
 *
 * 当玩家进入战场区域时由BattlefieldMgr调用
 *
 * 处理逻辑：
 * 1. 如果战斗进行中：
 *    - 有空位：邀请直接参战
 *    - 已满员：加入队列，10秒后踢出（如果未离开）
 * 2. 如果战斗即将开始（<15分钟）：
 *    - 邀请加入队列
 * 3. 将玩家添加到区域玩家列表
 *
 * @note 此函数在玩家切换区域时调用，需要处理各种状态
 */
void Battlefield::HandlePlayerEnterZone(Player* player, uint32 /*zone*/)
{
    // 战斗进行中的处理
    if (IsWarTime())
    {
        // 检查该阵营是否还有空位
        if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
        {
            // 有空位，邀请直接参战
            InvitePlayerToWar(player);
        }
        else
        {
            // 无空位，加入队列并计划踢出
            // TODO: 发送战场已满的提示数据包给玩家
            m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime() + 10;
            InvitePlayerToQueue(player);
        }
    }
    else
    {
        // 战斗未开始，如果距离开始时间<15分钟，邀请加入队列
        if (m_Timer <= m_StartGroupingTimer)
            InvitePlayerToQueue(player);
    }

    // 将玩家添加到区域玩家列表
    m_players[player->GetTeamId()].insert(player->GetGUID());
    OnPlayerEnterZone(player);  // 调用脚本回调
}

/**
 * @brief 处理玩家离开战场区域
 * @param player 离开的玩家指针
 * @param zone 区域ID（未使用）
 *
 * 当玩家离开战场区域时由BattlefieldMgr调用
 *
 * 清理工作：
 * 1. 如果正在参战，从战斗列表和团队中移除
 * 2. 从所有据点的活跃玩家列表中移除
 * 3. 从所有临时列表中移除（邀请、踢出队列等）
 * 4. 清理世界状态和复活队列
 *
 * @note 传送时会触发两次（一次RemoveFromWorld，一次UpdateZone）
 *       所以需要检查玩家是否真的在列表中
 */
void Battlefield::HandlePlayerLeaveZone(Player* player, uint32 /*zone*/)
{
    // 如果战斗进行中且玩家正在参战
    if (IsWarTime())
    {
        if (m_PlayersInWar[player->GetTeamId()].find(player->GetGUID()) != m_PlayersInWar[player->GetTeamId()].end())
        {
            // 从参战列表移除
            m_PlayersInWar[player->GetTeamId()].erase(player->GetGUID());

            // 发送离开战斗的数据包
            player->GetSession()->SendBfLeaveMessage(m_BattleId);

            // 从战场团队中移除
            if (Group* group = player->GetGroup())
                group->RemoveMember(player->GetGUID());

            // 调用脚本回调
            OnPlayerLeaveWar(player);
        }
    }

    // 从所有据点的活跃玩家列表中移除
    for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->HandlePlayerLeave(player);

    // 清理所有临时列表
    m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());
    m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());
    m_players[player->GetTeamId()].erase(player->GetGUID());

    // 清理世界状态和复活队列
    SendRemoveWorldStates(player);
    RemovePlayerFromResurrectQueue(player->GetGUID());

    OnPlayerLeaveZone(player);  // 调用脚本回调
}

/**
 * @brief 战场主更新函数
 * @param diff 距上次更新的时间间隔（毫秒）
 * @return 据点状态是否发生变化
 *
 * 核心更新循环，每秒由BattlefieldMgr调用一次
 *
 * 更新流程：
 * 1. 全局计时器更新（战斗开始/结束）
 * 2. 战斗开始前的组队邀请
 * 3. 战斗进行中：
 *    - AFK玩家检查
 *    - 未响应邀请的玩家踢出
 *    - 据点状态更新
 * 4. 墓地复活定时器
 *
 * @note 性能考虑：此函数频繁调用，避免在其中进行重型操作
 */
bool Battlefield::Update(uint32 diff)
{
    // ==================== 全局计时器处理 ====================

    if (m_Timer <= diff)
    {
        // 计时器到期
        if (IsWarTime())
            EndBattle(true);        // 战斗时间结束
        else
            StartBattle();          // 开始新的战斗
    }
    else
        m_Timer -= diff;            // 递减计时器

    // ==================== 战斗前组队邀请 ====================

    // 如果不在战斗中且未开始组队，距离战斗开始时间<=StartGroupingTimer
    if (!IsWarTime() && !m_StartGrouping && m_Timer <= m_StartGroupingTimer)
    {
        m_StartGrouping = true;
        InvitePlayersInZoneToQueue();   // 邀请区域内玩家加入队列
        OnStartGrouping();               // 调用脚本回调
    }

    // ==================== 战斗进行中的更新 ====================

    bool objective_changed = false;
    if (IsWarTime())
    {
        // AFK玩家检查（每秒一次）
        if (m_uiKickAfkPlayersTimer <= diff)
        {
            m_uiKickAfkPlayersTimer = 1000;
            KickAfkPlayers();
        }
        else
            m_uiKickAfkPlayersTimer -= diff;

        // 踢出未接受邀请的玩家（每秒检查一次）
        if (m_uiKickDontAcceptTimer <= diff)
        {
            time_t now = GameTime::GetGameTime();

            // 检查已邀请但未响应的玩家
            for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
                for (PlayerTimerMap::iterator itr = m_InvitedPlayers[team].begin(); itr != m_InvitedPlayers[team].end(); ++itr)
                    if (itr->second <= now)  // 邀请已过期
                        KickPlayerFromBattlefield(itr->first);

            // 持续邀请区域内玩家参战
            InvitePlayersInZoneToWar();

            // 检查待踢出列表中的玩家（战场满员时进入的玩家）
            for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
                for (PlayerTimerMap::iterator itr = m_PlayersWillBeKick[team].begin(); itr != m_PlayersWillBeKick[team].end(); ++itr)
                    if (itr->second <= now)
                        KickPlayerFromBattlefield(itr->first);

            m_uiKickDontAcceptTimer = 1000;
        }
        else
            m_uiKickDontAcceptTimer -= diff;

        // 更新所有据点状态
        for (BfCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
            if (itr->second->Update(diff))
                objective_changed = true;
    }

    // ==================== 墓地复活定时器 ====================

    // 每30秒复活一次墓地中的玩家
    if (m_LastResurrectTimer <= diff)
    {
        for (uint8 i = 0; i < m_GraveyardList.size(); i++)
            if (GetGraveyardById(i))
                m_GraveyardList[i]->Resurrect();
        m_LastResurrectTimer = RESURRECTION_INTERVAL;
    }
    else
        m_LastResurrectTimer -= diff;

    return objective_changed;
}

/**
 * @brief 邀请区域内所有玩家加入队列
 *
 * 在战斗开始前几分钟调用，向区域内所有玩家发送队列邀请
 */
void Battlefield::InvitePlayersInZoneToQueue()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                InvitePlayerToQueue(player);
}

/**
 * @brief 邀请单个玩家加入队列
 * @param player 目标玩家
 *
 * 发送加入队列的邀请给玩家
 * 只有在以下情况才会发送邀请：
 * - 本阵营队列人数较少，或对方阵营队列已达到最低要求
 */
void Battlefield::InvitePlayerToQueue(Player* player)
{
    // 检查是否已在队列中
    if (m_PlayersInQueue[player->GetTeamId()].count(player->GetGUID()))
        return;

    // 只在需要平衡时发送邀请
    // 如果本阵营人数很少，或对方阵营人数已足够，才发送邀请
    if (m_PlayersInQueue[player->GetTeamId()].size() <= m_MinPlayer || m_PlayersInQueue[GetOtherTeam(player->GetTeamId())].size() >= m_MinPlayer)
        player->GetSession()->SendBfInvitePlayerToQueue(m_BattleId);
}

/**
 * @brief 邀请队列中的所有玩家加入战斗
 *
 * 战斗开始时调用，清空队列并邀请所有人参战
 */
void Battlefield::InvitePlayersInQueueToWar()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (auto itr = m_PlayersInQueue[team].begin(); itr != m_PlayersInQueue[team].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                // 检查是否还有空位
                if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                    InvitePlayerToWar(player);
                else
                {
                    // 战场已满，不做处理
                }
            }
        }
        // 清空队列
        m_PlayersInQueue[team].clear();
    }
}

/**
 * @brief 邀请区域内所有玩家加入战斗
 *
 * 战斗进行中持续调用，邀请新进入区域的玩家参战
 * 如果战场已满，将玩家加入待踢出列表
 */
void Battlefield::InvitePlayersInZoneToWar()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (auto itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                // 跳过已在战斗中或已被邀请的玩家
                if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
                    continue;

                // 检查是否还有空位
                if (m_PlayersInWar[player->GetTeamId()].size() + m_InvitedPlayers[player->GetTeamId()].size() < m_MaxPlayer)
                    InvitePlayerToWar(player);
                else
                {
                    // 战场已满，加入踢出列表（10秒后踢出）
                    m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime() + 10;
                }
            }
        }
    }
}

/**
 * @brief 邀请单个玩家加入战斗
 * @param player 目标玩家
 *
 * 发送战斗邀请给玩家，玩家有m_TimeForAcceptInvite秒的时间响应
 *
 * 检查项：
 * - 玩家是否在飞行中
 * - 玩家是否在其他战场中
 * - 玩家等级是否达标
 * - 玩家是否已在战斗中
 */
void Battlefield::InvitePlayerToWar(Player* player)
{
    if (!player)
        return;

    // 飞行中的玩家无法参战
    if (player->IsInFlight())
        return;

    // 已在其他战场中的玩家无法参战
    if (player->GetBattleground())
        return;

    // 等级不足的玩家将被踢出
    if (player->GetLevel() < m_MinLevel)
    {
        if (m_PlayersWillBeKick[player->GetTeamId()].count(player->GetGUID()) == 0)
            m_PlayersWillBeKick[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime() + 10;
        return;
    }

    // 检查是否已在战斗中或已被邀请
    if (m_PlayersInWar[player->GetTeamId()].count(player->GetGUID()) || m_InvitedPlayers[player->GetTeamId()].count(player->GetGUID()))
        return;

    // 从踢出列表移除（如果有）
    m_PlayersWillBeKick[player->GetTeamId()].erase(player->GetGUID());

    // 添加到已邀请列表，设置过期时间
    m_InvitedPlayers[player->GetTeamId()][player->GetGUID()] = GameTime::GetGameTime() + m_TimeForAcceptInvite;

    // 发送邀请数据包
    player->GetSession()->SendBfInvitePlayerToWar(m_BattleId, m_ZoneId, m_TimeForAcceptInvite);
}

/**
 * @brief 初始化追踪者NPC
 * @param entry 生物模板ID
 * @param pos 生成位置
 *
 * 追踪者NPC用于发送区域消息（如战斗开始/结束警告）
 */
void Battlefield::InitStalker(uint32 entry, Position const& pos)
{
    if (Creature* creature = SpawnCreature(entry, pos))
        StalkerGuid = creature->GetGUID();
    else
        TC_LOG_ERROR("bg.battlefield", "Battlefield::InitStalker: Could not spawn Stalker (Creature entry {}), zone messages will be unavailable!", entry);
}

/**
 * @brief 踢出所有AFK玩家
 *
 * 遍历所有参战玩家，检查是否处于AFK状态
 * AFK玩家将被传送出战场
 */
void Battlefield::KickAfkPlayers()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->isAFK())
                    KickPlayerFromBattlefield(*itr);
}

/**
 * @brief 将玩家踢出战场
 * @param guid 玩家GUID
 *
 * 将玩家传送到KickPosition位置（通常是战场外的安全地点）
 */
void Battlefield::KickPlayerFromBattlefield(ObjectGuid guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
        if (player->GetZoneId() == GetZoneId())
            player->TeleportTo(KickPosition);
}

/**
 * @brief 开始战斗
 *
 * 初始化战斗状态并邀请玩家参战
 *
 * 执行流程：
 * 1. 清空参战列表和团队列表
 * 2. 设置计时器为战斗时长
 * 3. 激活战场
 * 4. 邀请区域内和队列中的玩家参战
 * 5. 调用脚本回调OnBattleStart
 */
void Battlefield::StartBattle()
{
    // 防止重复开始
    if (m_isActive)
        return;

    // 清空上一场的数据
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        m_PlayersInWar[team].clear();
        m_Groups[team].clear();
    }

    // 设置战斗参数
    m_Timer = m_BattleTime;
    m_isActive = true;

    // 邀请玩家参战
    InvitePlayersInZoneToWar();
    InvitePlayersInQueueToWar();

    // 调用脚本回调
    OnBattleStart();
}

/**
 * @brief 结束战斗
 * @param endByTimer 是否因时间到期结束
 *
 * 执行流程：
 * 1. 设置战场为非活跃状态
 * 2. 如果非定时结束（被攻破），更新防守方
 * 3. 调用脚本回调OnBattleEnd
 * 4. 重置计时器为下一场战斗的准备时间
 *
 * @param endByTimer true=时间到期，防守方获胜；false=据点被攻破，进攻方获胜
 */
void Battlefield::EndBattle(bool endByTimer)
{
    if (!m_isActive)
        return;

    m_isActive = false;
    m_StartGrouping = false;

    // 如果不是定时结束，说明进攻方获胜
    if (!endByTimer)
        SetDefenderTeam(GetAttackerTeam());

    // 调用脚本回调
    OnBattleEnd(endByTimer);

    // 重置计时器，准备下一场战斗
    m_Timer = m_NoWarBattleTime;
    SendInitWorldStatesToAll();
}

/**
 * @brief 向所有参战玩家播放音效
 * @param soundID 音效ID
 */
void Battlefield::DoPlaySoundToAll(uint32 soundID)
{
    BroadcastPacketToWar(WorldPackets::Misc::PlaySound(soundID).Write());
}

/**
 * @brief 检查玩家是否在战场区域内
 * @param player 玩家指针
 * @return 是否在区域内
 */
bool Battlefield::HasPlayer(Player* player) const
{
    return m_players[player->GetTeamId()].find(player->GetGUID()) != m_players[player->GetTeamId()].end();
}

/**
 * @brief 玩家接受加入队列邀请
 * @param player 玩家指针
 *
 * 由WorldSession::HandleBfQueueInviteResponse调用
 * 将玩家添加到队列并发送确认消息
 */
void Battlefield::PlayerAcceptInviteToQueue(Player* player)
{
    // 将玩家添加到队列
    m_PlayersInQueue[player->GetTeamId()].insert(player->GetGUID());
    // 发送队列确认消息
    player->GetSession()->SendBfQueueInviteResponse(m_BattleId, m_ZoneId);
}

/**
 * @brief 玩家请求离开队列
 * @param player 玩家指针
 *
 * 由WorldSession::HandleBfQueueExitRequest调用
 */
void Battlefield::AskToLeaveQueue(Player* player)
{
    // 从队列中移除玩家
    m_PlayersInQueue[player->GetTeamId()].erase(player->GetGUID());
}

/**
 * @brief 玩家请求离开战场
 * @param player 玩家指针
 *
 * 由WorldSession::HandleHearthAndResurrect调用
 * 将玩家传送到达拉然
 */
void Battlefield::PlayerAskToLeave(Player* player)
{
    // 玩家离开冬拥湖，传送到达拉然
    // TODO: 确认传送目标位置
    player->TeleportTo(571, 5804.1499f, 624.7710f, 647.7670f, 1.6400f);
}

/**
 * @brief 玩家接受加入战斗邀请
 * @param player 玩家指针
 *
 * 由WorldSession::HandleBfEntryInviteResponse调用
 * 将玩家加入战斗并编入团队
 */
void Battlefield::PlayerAcceptInviteToWar(Player* player)
{
    // 检查战斗是否仍在进行
    if (!IsWarTime())
        return;

    // 将玩家加入正确的团队
    if (AddOrSetPlayerToCorrectBfGroup(player))
    {
        // 发送战斗确认消息
        player->GetSession()->SendBfEntered(m_BattleId);

        // 更新玩家状态
        m_PlayersInWar[player->GetTeamId()].insert(player->GetGUID());
        m_InvitedPlayers[player->GetTeamId()].erase(player->GetGUID());

        // 如果玩家是AFK状态，取消AFK
        if (player->isAFK())
            player->ToggleAFK();

        // 调用脚本回调
        OnPlayerJoinWar(player);
    }
}

/**
 * @brief 为指定阵营的所有参战玩家施放或移除法术
 * @param team 目标阵营
 * @param spellId 法术ID（正数=施加，负数=移除）
 */
void Battlefield::TeamCastSpell(TeamId team, int32 spellId)
{
    if (spellId > 0)
    {
        // 施加增益法术
        for (auto itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->CastSpell(player, uint32(spellId), true);
    }
    else
    {
        // 移除法术效果
        for (auto itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->RemoveAuraFromStack(uint32(-spellId));
    }
}

/**
 * @brief 向区域内所有玩家广播数据包
 * @param data 数据包指针
 */
void Battlefield::BroadcastPacketToZone(WorldPacket const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendDirectMessage(data);
}

/**
 * @brief 向队列中的玩家广播数据包
 * @param data 数据包指针
 */
void Battlefield::BroadcastPacketToQueue(WorldPacket const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_PlayersInQueue[team].begin(); itr != m_PlayersInQueue[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(*itr))
                player->SendDirectMessage(data);
}

/**
 * @brief 向参战玩家广播数据包
 * @param data 数据包指针
 */
void Battlefield::BroadcastPacketToWar(WorldPacket const* data) const
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendDirectMessage(data);
}

/**
 * @brief 发送战场警告消息
 * @param id 消息文本ID
 * @param target 目标对象（可选）
 *
 * 通过Stalker NPC发送区域聊天消息
 */
void Battlefield::SendWarning(uint8 id, WorldObject const* target /*= nullptr*/)
{
    if (Creature* stalker = GetCreature(StalkerGuid))
        sCreatureTextMgr->SendChat(stalker, id, target);
}

/**
 * @brief 向指定玩家发送初始世界状态
 * @param player 目标玩家
 *
 * 初始化世界状态数据包并发送给玩家
 */
void Battlefield::SendInitWorldStatesTo(Player* player)
{
    WorldPackets::WorldState::InitWorldStates packet;
    packet.MapID = m_MapId;
    packet.ZoneID = m_ZoneId;
    packet.AreaID = player->GetAreaId();
    FillInitialWorldStates(packet);

    player->SendDirectMessage(packet.Write());
}

/**
 * @brief 向区域内所有玩家更新世界状态
 * @param field 世界状态字段ID
 * @param value 新值
 */
void Battlefield::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 i = 0; i < PVP_TEAMS_COUNT; ++i)
        for (auto itr = m_players[i].begin(); itr != m_players[i].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

/**
 * @brief 注册区域到战场管理器
 * @param zoneId 区域ID
 *
 * 将指定区域与当前战场关联，玩家进入该区域时触发战场事件
 */
void Battlefield::RegisterZone(uint32 zoneId)
{
    sBattlefieldMgr->AddZone(zoneId, this);
}

/**
 * @brief 隐藏NPC
 * @param creature 生物指针
 *
 * 通过相位和可见性设置隐藏NPC
 * 用于战斗结束后隐藏阵营特定的NPC
 */
void Battlefield::HideNpc(Creature* creature)
{
    creature->CombatStop();                                         // 停止战斗
    creature->SetReactState(REACT_PASSIVE);                         // 设置为被动反应
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);  // 设置不可攻击/交互标志
    creature->SetPhaseMask(2, true);                                // 设置为相位2（不可见）
    creature->DisappearAndDie();                                    // 消失并死亡
    creature->SetVisible(false);                                    // 设置为不可见
}

/**
 * @brief 显示NPC
 * @param creature 生物指针
 * @param aggressive 是否为攻击型
 *
 * 显示之前隐藏的NPC
 */
void Battlefield::ShowNpc(Creature* creature, bool aggressive)
{
    creature->SetPhaseMask(1, true);                                // 设置为相位1（可见）
    creature->SetVisible(true);                                     // 设置为可见
    creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);  // 移除不可攻击/交互标志

    // 如果死亡则复活
    if (!creature->IsAlive())
        creature->Respawn(true);

    // 设置反应类型
    if (aggressive)
        creature->SetReactState(REACT_AGGRESSIVE);
    else
    {
        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        creature->SetReactState(REACT_PASSIVE);
    }
}

// ****************************************************
// ******************* 团队系统 *******************
// ****************************************************

/**
 * @brief 寻找或创建可用的战场团队
 * @param TeamId 目标阵营
 * @return 未满的团队指针，没有则返回nullptr
 *
 * 遍历该阵营的所有团队，找到第一个未满的团队
 * 如果所有团队都已满，返回nullptr
 */
Group* Battlefield::GetFreeBfRaid(TeamId TeamId)
{
    for (auto itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(itr->GetCounter()))
            if (!group->IsFull())
                return group;

    return nullptr;
}

/**
 * @brief 获取玩家所在的战场团队
 * @param guid 玩家GUID
 * @param TeamId 玩家阵营
 * @return 团队指针，未找到返回nullptr
 */
Group* Battlefield::GetGroupPlayer(ObjectGuid guid, TeamId TeamId)
{
    for (auto itr = m_Groups[TeamId].begin(); itr != m_Groups[TeamId].end(); ++itr)
        if (Group* group = sGroupMgr->GetGroupByGUID(itr->GetCounter()))
            if (group->IsMember(guid))
                return group;

    return nullptr;
}

/**
 * @brief 将玩家加入正确的战场团队
 * @param player 玩家指针
 * @return 是否成功加入
 *
 * 执行流程：
 * 1. 检查玩家是否在世界中
 * 2. 如果玩家已有团队，先移除
 * 3. 寻找未满的战场团队
 * 4. 如果没有，创建新团队
 * 5. 将玩家加入团队
 */
bool Battlefield::AddOrSetPlayerToCorrectBfGroup(Player* player)
{
    // 玩家必须在世界中
    if (!player->IsInWorld())
        return false;

    // 如果玩家已有团队，先移除
    if (Group* group = player->GetGroup())
        group->RemoveMember(player->GetGUID());

    // 寻找未满的战场团队
    Group* group = GetFreeBfRaid(player->GetTeamId());
    if (!group)
    {
        // 没有未满的团队，创建新团队
        group = new Group;
        group->SetBattlefieldGroup(this);
        group->Create(player);
        sGroupMgr->AddGroup(group);
        m_Groups[player->GetTeamId()].insert(group->GetGUID());
    }
    else if (group->IsMember(player->GetGUID()))
    {
        // 玩家已在该团队中，设置团队引用
        uint8 subgroup = group->GetMemberGroup(player->GetGUID());
        player->SetBattlegroundOrBattlefieldRaid(group, subgroup);
    }
    else
    {
        // 将玩家加入团队
        group->AddMember(player);
    }

    return true;
}

//*************** 团队系统结束 *******************

//*****************************************************
//*************** 灵魂医者系统 *******************
//*****************************************************

//--------------------
//- Battlefield 墓地方法 -
//--------------------

/**
 * @brief 根据ID获取墓地对象
 * @param id 墓地ID
 * @return 墓地对象指针，失败返回nullptr
 */
BfGraveyard* Battlefield::GetGraveyardById(uint32 id) const
{
    if (id < m_GraveyardList.size())
    {
        if (BfGraveyard* graveyard = m_GraveyardList.at(id))
            return graveyard;
        else
            TC_LOG_ERROR("bg.battlefield", "Battlefield::GetGraveyardById Id:{} does not exist.", id);
    }
    else
        TC_LOG_ERROR("bg.battlefield", "Battlefield::GetGraveyardById Id:{} could not be found.", id);

    return nullptr;
}

/**
 * @brief 获取最近的友方墓地
 * @param player 需要复活的玩家
 * @return 墓地位置数据，未找到返回nullptr
 *
 * 遍历所有墓地，找到：
 * 1. 由玩家阵营控制的
 * 2. 距离玩家最近的
 */
WorldSafeLocsEntry const* Battlefield::GetClosestGraveyard(Player* player)
{
    BfGraveyard* closestGY = nullptr;
    float maxdist = -1;

    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (m_GraveyardList[i])
        {
            // 跳过敌对阵营控制的墓地
            if (m_GraveyardList[i]->GetControlTeamId() != player->GetTeamId())
                continue;

            float dist = m_GraveyardList[i]->GetDistance(player);
            if (dist < maxdist || maxdist < 0)
            {
                closestGY = m_GraveyardList[i];
                maxdist = dist;
            }
        }
    }

    if (closestGY)
        return sWorldSafeLocsStore.LookupEntry(closestGY->GetGraveyardId());

    return nullptr;
}

/**
 * @brief 将玩家添加到复活队列
 * @param npcGuid 灵魂医者NPC的GUID
 * @param playerGuid 玩家GUID
 *
 * 遍历所有墓地，找到包含该NPC的墓地，将玩家添加到其复活队列
 */
void Battlefield::AddPlayerToResurrectQueue(ObjectGuid npcGuid, ObjectGuid playerGuid)
{
    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (!m_GraveyardList[i])
            continue;

        if (m_GraveyardList[i]->HasNpc(npcGuid))
        {
            m_GraveyardList[i]->AddPlayer(playerGuid);
            break;
        }
    }
}

/**
 * @brief 将玩家从复活队列移除
 * @param playerGuid 玩家GUID
 *
 * 遍历所有墓地，找到包含该玩家的墓地，将其从复活队列移除
 */
void Battlefield::RemovePlayerFromResurrectQueue(ObjectGuid playerGuid)
{
    for (uint8 i = 0; i < m_GraveyardList.size(); i++)
    {
        if (!m_GraveyardList[i])
            continue;

        if (m_GraveyardList[i]->HasPlayer(playerGuid))
        {
            m_GraveyardList[i]->RemovePlayer(playerGuid);
            break;
        }
    }
}

/**
 * @brief 发送灵魂医者查询响应
 * @param player 玩家指针
 * @param guid 灵魂医者GUID
 *
 * 告知玩家距离下次复活还有多少时间
 */
void Battlefield::SendAreaSpiritHealerQueryOpcode(Player* player, ObjectGuid guid)
{
    WorldPacket data(SMSG_AREA_SPIRIT_HEALER_TIME, 12);
    uint32 time = m_LastResurrectTimer;  // 每30秒复活一次

    data << guid << time;
    player->SendDirectMessage(&data);
}

// ----------------------
// - BfGraveyard 方法 -
// ----------------------

/**
 * @brief BfGraveyard构造函数
 * @param bf 所属战场指针
 */
BfGraveyard::BfGraveyard(Battlefield* bf)
{
    m_Bf = bf;
    m_GraveyardId = 0;
    m_ControlTeam = TEAM_NEUTRAL;
}

BfGraveyard::~BfGraveyard() = default;

/**
 * @brief 初始化墓地
 * @param startControl 初始控制阵营
 * @param graveyardId 墓地ID
 */
void BfGraveyard::Initialize(TeamId startControl, uint32 graveyardId)
{
    m_ControlTeam = startControl;
    m_GraveyardId = graveyardId;
}

/**
 * @brief 设置灵魂医者
 * @param spirit 灵魂医者生物指针
 * @param team 该灵魂医者所属阵营
 *
 * 每个墓地有两个灵魂医者（联盟和部落各一个）
 */
void BfGraveyard::SetSpirit(Creature* spirit, TeamId team)
{
    if (!spirit)
    {
        TC_LOG_ERROR("bg.battlefield", "BfGraveyard::SetSpirit: Invalid Spirit.");
        return;
    }

    m_SpiritGuide[team] = spirit->GetGUID();
    spirit->SetReactState(REACT_PASSIVE);  // 灵魂医者不主动攻击
}

/**
 * @brief 计算墓地到玩家的距离
 * @param player 目标玩家
 * @return 距离值
 */
float BfGraveyard::GetDistance(Player* player)
{
    WorldSafeLocsEntry const* safeLoc = sWorldSafeLocsStore.LookupEntry(m_GraveyardId);
    return player->GetDistance2d(safeLoc->Loc.X, safeLoc->Loc.Y);
}

/**
 * @brief 将玩家添加到复活队列
 * @param playerGuid 玩家GUID
 *
 * 玩家死亡后选择在此墓地复活时调用
 */
void BfGraveyard::AddPlayer(ObjectGuid playerGuid)
{
    // 避免重复添加
    if (!m_ResurrectQueue.count(playerGuid))
    {
        m_ResurrectQueue.insert(playerGuid);

        // 给玩家施加"等待复活"的视觉效果
        if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
            player->CastSpell(player, SPELL_WAITING_FOR_RESURRECT, true);
    }
}

/**
 * @brief 将玩家从复活队列移除
 * @param playerGuid 玩家GUID
 */
void BfGraveyard::RemovePlayer(ObjectGuid playerGuid)
{
    m_ResurrectQueue.erase(m_ResurrectQueue.find(playerGuid));

    // 移除"等待复活"的视觉效果
    if (Player* player = ObjectAccessor::FindPlayer(playerGuid))
        player->RemoveAurasDueToSpell(SPELL_WAITING_FOR_RESURRECT);
}

/**
 * @brief 复活所有排队中的玩家
 *
 * 每30秒调用一次，复活所有等待中的玩家
 */
void BfGraveyard::Resurrect()
{
    if (m_ResurrectQueue.empty())
        return;

    for (GuidSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
    {
        // 从GUID获取玩家对象
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (!player)
            continue;

        // 检查玩家是否在世界中且在正确的墓地
        if (player->IsInWorld())
            if (Creature* spirit = m_Bf->GetCreature(m_SpiritGuide[m_ControlTeam]))
                spirit->CastSpell(spirit, SPELL_SPIRIT_HEAL, true);

        // 复活玩家
        player->CastSpell(player, SPELL_RESURRECTION_VISUAL, true);  // 复活视觉效果
        player->ResurrectPlayer(1.0f);                                 // 满血复活
        player->CastSpell(player, 6962, true);                        // 施放复活法术
        player->CastSpell(player, SPELL_SPIRIT_HEAL_MANA, true);      // 恢复法力

        // 生成骨头（移除尸体）
        player->SpawnCorpseBones(false);
    }

    // 清空复活队列
    m_ResurrectQueue.clear();
}

/**
 * @brief 将墓地控制权移交给指定阵营
 * @param team 新的控制阵营
 *
 * 当墓地被占领时调用
 */
void BfGraveyard::GiveControlTo(TeamId team)
{
    // 灵魂医者切换
    // 注意：可见性变化通过相位实现
    /*if (m_SpiritGuide[1 - team])
        m_SpiritGuide[1 - team]->SetVisible(false);
    if (m_SpiritGuide[team])
        m_SpiritGuide[team]->SetVisible(true);*/

    m_ControlTeam = team;

    // 将正在等待复活的敌对阵营玩家传送到最近的友方墓地
    RelocateDeadPlayers();
}

/**
 * @brief 重新定位死亡玩家
 *
 * 当墓地控制权变更时，将敌对阵营的死亡玩家传送到最近的友方墓地
 */
void BfGraveyard::RelocateDeadPlayers()
{
    WorldSafeLocsEntry const* closestGrave = nullptr;

    for (GuidSet::const_iterator itr = m_ResurrectQueue.begin(); itr != m_ResurrectQueue.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindPlayer(*itr);
        if (!player)
            continue;

        // 传送到最近的友方墓地
        if (closestGrave)
            player->TeleportTo(player->GetMapId(), closestGrave->Loc.X, closestGrave->Loc.Y, closestGrave->Loc.Z, player->GetOrientation());
        else
        {
            closestGrave = m_Bf->GetClosestGraveyard(player);
            if (closestGrave)
                player->TeleportTo(player->GetMapId(), closestGrave->Loc.X, closestGrave->Loc.Y, closestGrave->Loc.Z, player->GetOrientation());
        }
    }
}

/**
 * @brief 检查墓地是否有指定的NPC（灵魂医者）
 * @param guid NPC的GUID
 * @return 是否存在该NPC
 */
bool BfGraveyard::HasNpc(ObjectGuid guid)
{
    if (!m_SpiritGuide[TEAM_ALLIANCE] || !m_SpiritGuide[TEAM_HORDE])
        return false;

    if (!m_Bf->GetCreature(m_SpiritGuide[TEAM_ALLIANCE]) ||
        !m_Bf->GetCreature(m_SpiritGuide[TEAM_HORDE]))
        return false;

    return (m_SpiritGuide[TEAM_ALLIANCE] == guid || m_SpiritGuide[TEAM_HORDE] == guid);
}

// *******************************************************
// *************** 灵魂医者系统结束 ***************
// *******************************************************
// ********************** 杂项功能 ***************************
// *******************************************************

/**
 * @brief 在战场地图上生成生物
 * @param entry 生物模板ID
 * @param pos 生成位置
 * @return 生成的生物指针，失败返回nullptr
 *
 * 创建生物并将其添加到地图
 */
Creature* Battlefield::SpawnCreature(uint32 entry, Position const& pos)
{
    // 获取地图对象
    Map* map = sMapMgr->CreateBaseMap(m_MapId);
    if (!map)
    {
        TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnCreature: Can't create creature entry: {}, map not found.", entry);
        return nullptr;
    }

    // 创建生物对象
    Creature* creature = new Creature();
    if (!creature->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, PHASEMASK_NORMAL, entry, pos))
    {
        TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnCreature: Can't create creature entry: {}", entry);
        delete creature;
        return nullptr;
    }

    creature->SetHomePosition(pos);

    // 将生物添加到地图
    map->AddToMap(creature);
    creature->setActive(true);          // 设置为活跃对象，确保更新
    creature->SetFarVisible(true);      // 设置远距离可见

    return creature;
}

/**
 * @brief 在战场地图上生成游戏对象
 * @param entry 游戏对象模板ID
 * @param pos 生成位置
 * @param rot 旋转四元数
 * @return 生成的游戏对象指针，失败返回nullptr
 */
GameObject* Battlefield::SpawnGameObject(uint32 entry, Position const& pos, QuaternionData const& rot)
{
    // 获取地图对象
    Map* map = sMapMgr->CreateBaseMap(m_MapId);
    if (!map)
        return nullptr;

    // 创建游戏对象
    GameObject* go = new GameObject;
    if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), entry, map, PHASEMASK_NORMAL, pos, rot, 255, GO_STATE_READY))
    {
        TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnGameObject: Gameobject template {} could not be found in the database! Battlefield has not been created!", entry);
        TC_LOG_ERROR("bg.battlefield", "Battlefield::SpawnGameObject: Could not create gameobject template {}! Battlefield has not been created!", entry);
        delete go;
        return nullptr;
    }

    // 添加到地图
    map->AddToMap(go);
    go->setActive(true);
    go->SetFarVisible(true);

    return go;
}

/**
 * @brief 根据GUID获取生物
 * @param guid 生物GUID
 * @return 生物指针
 */
Creature* Battlefield::GetCreature(ObjectGuid guid)
{
    if (!m_Map)
        return nullptr;
    return m_Map->GetCreature(guid);
}

/**
 * @brief 根据GUID获取游戏对象
 * @param guid 游戏对象GUID
 * @return 游戏对象指针
 */
GameObject* Battlefield::GetGameObject(ObjectGuid guid)
{
    if (!m_Map)
        return nullptr;
    return m_Map->GetGameObject(guid);
}

// *******************************************************
// ******************* 据点系统 **********************
// *******************************************************

/**
 * @brief BfCapturePoint构造函数
 * @param bf 所属战场指针
 */
BfCapturePoint::BfCapturePoint(Battlefield* bf) : m_Bf(bf), m_capturePointGUID()
{
    m_team = TEAM_NEUTRAL;
    m_value = 0;
    m_minValue = 0.0f;
    m_maxValue = 0.0f;
    m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_OldState = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL;
    m_capturePointEntry = 0;
    m_neutralValuePct = 0;
    m_maxSpeed = 0;
}

BfCapturePoint::~BfCapturePoint() = default;

/**
 * @brief 处理玩家进入据点区域
 * @param player 进入的玩家指针
 * @return 是否成功添加（玩家是否已存在）
 *
 * 当玩家进入据点有效范围时调用
 * 发送占领进度条的初始状态给玩家
 */
bool BfCapturePoint::HandlePlayerEnter(Player* player)
{
    if (m_capturePointGUID)
    {
        if (GameObject* capturePoint = m_Bf->GetGameObject(m_capturePointGUID))
        {
            // 发送占领进度条的世界状态更新
            player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldState1, 1);
            // 发送当前进度（百分比）
            player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate2, uint32(ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f)));
            // 发送中立区域百分比
            player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
        }
    }

    return m_activePlayers[player->GetTeamId()].insert(player->GetGUID()).second;
}

/**
 * @brief 处理玩家离开据点区域
 * @param player 离开的玩家指针
 * @return 指向下一个玩家的迭代器
 *
 * 当玩家离开据点范围时调用
 * 隐藏占领进度条
 */
GuidSet::iterator BfCapturePoint::HandlePlayerLeave(Player* player)
{
    if (m_capturePointGUID)
        if (GameObject* capturePoint = m_Bf->GetGameObject(m_capturePointGUID))
            player->SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldState1, 0);  // 隐藏进度条

    GuidSet::iterator current = m_activePlayers[player->GetTeamId()].find(player->GetGUID());

    if (current == m_activePlayers[player->GetTeamId()].end())
        return current; // 返回 end()

    m_activePlayers[player->GetTeamId()].erase(current++);
    return current;
}

/**
 * @brief 发送占领进度阶段变化
 *
 * 向所有在场玩家更新占领进度条的显示
 */
void BfCapturePoint::SendChangePhase()
{
    if (!m_capturePointGUID)
        return;

    if (GameObject* capturePoint = m_Bf->GetGameObject(m_capturePointGUID))
    {
        // 发送进度条显示状态（有时会消失，这里强制显示）
        SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldState1, 1);
        // 发送当前进度百分比
        SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate2, (uint32) std::ceil((m_value + m_maxValue) / (2 * m_maxValue) * 100.0f));
        // 发送中立区域百分比（有时会重置）
        SendUpdateWorldState(capturePoint->GetGOInfo()->capturePoint.worldstate3, m_neutralValuePct);
    }
}

/**
 * @brief 设置据点数据
 * @param capturePoint 据点对应的游戏对象指针
 * @return 是否设置成功
 *
 * 从游戏对象模板中读取据点配置，初始化占领参数
 */
bool BfCapturePoint::SetCapturePointData(GameObject* capturePoint)
{
    ASSERT(capturePoint);

    TC_LOG_DEBUG("bg.battlefield", "Creating capture point {}", capturePoint->GetEntry());

    m_capturePointGUID = ObjectGuid(HighGuid::GameObject, capturePoint->GetEntry(), capturePoint->GetGUID().GetCounter());

    // 检查游戏对象信息是否存在
    GameObjectTemplate const* goinfo = capturePoint->GetGOInfo();
    if (goinfo->type != GAMEOBJECT_TYPE_CAPTURE_POINT)
    {
        TC_LOG_ERROR("misc", "OutdoorPvP: GO {} is not a capture point!", capturePoint->GetEntry());
        return false;
    }

    // 从模板获取配置参数
    m_maxValue = goinfo->capturePoint.maxTime;                                    // 最大占领值
    m_maxSpeed = m_maxValue / (goinfo->capturePoint.minTime ? goinfo->capturePoint.minTime : 60);  // 最大占领速度
    m_neutralValuePct = goinfo->capturePoint.neutralPercent;                      // 中立百分比
    m_minValue = m_maxValue * goinfo->capturePoint.neutralPercent / 100;          // 最小值（中立边界）
    m_capturePointEntry = capturePoint->GetEntry();

    // 根据初始阵营设置状态
    if (m_team == TEAM_ALLIANCE)
    {
        m_value = m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
    }
    else
    {
        m_value = -m_maxValue;
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
    }

    return true;
}

/**
 * @brief 获取据点对应的游戏对象
 * @return 游戏对象指针
 */
GameObject* BfCapturePoint::GetCapturePointGo()
{
    return m_Bf->GetGameObject(m_capturePointGUID);
}

/**
 * @brief 删除据点游戏对象
 * @return 是否删除成功
 */
bool BfCapturePoint::DelCapturePoint()
{
    if (m_capturePointGUID)
    {
        if (GameObject* capturePoint = m_Bf->GetGameObject(m_capturePointGUID))
        {
            capturePoint->SetRespawnTime(0);                  // 不保存重生时间
            capturePoint->Delete();
            capturePoint = nullptr;
        }
        m_capturePointGUID.Clear();
    }

    return true;
}

/**
 * @brief 更新据点状态
 * @param diff 距上次更新的时间间隔（毫秒）
 * @return 据点状态是否发生变化（如所有权变更）
 *
 * 核心更新函数，每秒调用一次
 *
 * 执行流程：
 * 1. 检测并移除离开范围的玩家
 * 2. 检测并添加进入范围的玩家
 * 3. 计算占领进度变化（基于双方玩家数量差）
 * 4. 更新据点状态和所属阵营
 *
 * 占领机制：
 * - 每个玩家贡献一个单位的占领力量
 * - 占领进度根据双方人数差推进
 * - 进度到达边界时变更所有权
 */
bool BfCapturePoint::Update(uint32 diff)
{
    if (!m_capturePointGUID)
        return false;

    if (GameObject* capturePoint = m_Bf->GetGameObject(m_capturePointGUID))
    {
        float radius = capturePoint->GetGOInfo()->capturePoint.radius;

        // 第一步：移除离开范围或不再活跃的玩家
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end();)
            {
                if (Player* player = ObjectAccessor::FindPlayer(*itr))
                {
                    // 检查玩家是否还在范围内且处于活跃状态
                    if (!capturePoint->IsWithinDistInMap(player, radius) || !player->IsOutdoorPvPActive())
                        itr = HandlePlayerLeave(player);
                    else
                        ++itr;
                }
                else
                    ++itr;
            }
        }

        // 第二步：检测新进入范围的玩家
        std::list<Player*> players;
        Trinity::AnyPlayerInObjectRangeCheck checker(capturePoint, radius);
        Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(capturePoint, players, checker);
        Cell::VisitWorldObjects(capturePoint, searcher, radius);

        for (std::list<Player*>::iterator itr = players.begin(); itr != players.end(); ++itr)
            if ((*itr)->IsOutdoorPvPActive())
                if (m_activePlayers[(*itr)->GetTeamId()].insert((*itr)->GetGUID()).second)
                    HandlePlayerEnter(*itr);
    }

    // 第三步：计算占领进度变化
    // 根据双方玩家数量差计算进度变化
    float fact_diff = ((float) m_activePlayers[TEAM_ALLIANCE].size() - (float) m_activePlayers[TEAM_HORDE].size()) * diff / float(BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL);
    if (G3D::fuzzyEq(fact_diff, 0.0f))
        return false;

    uint32 Challenger = 0;
    float maxDiff = m_maxSpeed * diff;

    // 限制进度变化速度
    if (fact_diff < 0)
    {
        // 部落占多数
        // 如果已经是部落完全占领且部落仍在多数，无需变化
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE && m_value <= -m_maxValue)
            return false;

        if (fact_diff < -maxDiff)
            fact_diff = -maxDiff;

        Challenger = HORDE;
    }
    else
    {
        // 联盟占多数
        // 如果已经是联盟完全占领且联盟仍在多数，无需变化
        if (m_State == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE && m_value >= m_maxValue)
            return false;

        if (fact_diff > maxDiff)
            fact_diff = maxDiff;

        Challenger = ALLIANCE;
    }

    // 保存旧状态用于检测变化
    float oldValue = m_value;
    TeamId oldTeam = m_team;

    m_OldState = m_State;

    // 更新进度值
    m_value += fact_diff;

    // 第四步：根据进度值更新据点状态
    if (m_value < -m_minValue)                              // 部落控制区域
    {
        if (m_value < -m_maxValue)
            m_value = -m_maxValue;                           // 限制最大值
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE;
        m_team = TEAM_HORDE;
    }
    else if (m_value > m_minValue)                          // 联盟控制区域
    {
        if (m_value > m_maxValue)
            m_value = m_maxValue;                            // 限制最大值
        m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE;
        m_team = TEAM_ALLIANCE;
    }
    else if (oldValue * m_value <= 0)                       // 中立区域，经过了中点
    {
        // 根据挑战方确定状态
        if (Challenger == ALLIANCE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE;
        else if (Challenger == HORDE)
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }
    else                                                    // 中立区域，未经过中点
    {
        // 双方在同一侧，一方挑战另一方
        if (Challenger == ALLIANCE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;
        else if (Challenger == HORDE && (m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE || m_OldState == BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE))
            m_State = BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE;
        m_team = TEAM_NEUTRAL;
    }

    // 如果进度值发生变化，更新客户端显示
    if (G3D::fuzzyNe(m_value, oldValue))
        SendChangePhase();

    // 如果状态发生变化
    if (m_OldState != m_State)
    {
        // 如果所有权变更，调用回调
        if (oldTeam != m_team)
            ChangeTeam(oldTeam);
        return true;
    }

    return false;
}

/**
 * @brief 向所有在场玩家发送世界状态更新
 * @param field 世界状态字段ID
 * @param value 新值
 */
void BfCapturePoint::SendUpdateWorldState(uint32 field, uint32 value)
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SendUpdateWorldState(field, value);
}

/**
 * @brief 发送目标完成通知
 * @param id 击杀怪物信贷ID
 * @param guid 相关对象的GUID
 *
 * 向控制该据点的阵营玩家发送任务完成通知
 */
void BfCapturePoint::SendObjectiveComplete(uint32 id, ObjectGuid guid)
{
    uint8 team;
    switch (m_State)
    {
        case BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE:
            team = TEAM_ALLIANCE;
            break;
        case BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE:
            team = TEAM_HORDE;
            break;
        default:
            return;
    }

    // 向所有在场玩家发送任务信贷
    for (GuidSet::iterator itr = m_activePlayers[team].begin(); itr != m_activePlayers[team].end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(*itr))
            player->KilledMonsterCredit(id, guid);
}

/**
 * @brief 检查玩家是否在据点范围内
 * @param player 要检查的玩家
 * @return 是否在范围内
 */
bool BfCapturePoint::IsInsideObjective(Player* player) const
{
    return m_activePlayers[player->GetTeamId()].find(player->GetGUID()) != m_activePlayers[player->GetTeamId()].end();
}
