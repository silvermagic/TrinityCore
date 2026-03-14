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
 * @file BattlefieldHandler.cpp
 * @brief 战场（Battlefield）网络消息处理器实现文件
 *
 * 本文件实现了 WorldSession 类中与战场系统相关的网络消息处理函数，
 * 包括战场邀请、排队、进入战场、离开战场等核心交互流程。
 *
 * 主要功能模块：
 * - 发送战场邀请消息（排队邀请、参战邀请）
 * - 处理玩家对邀请的响应（接受/拒绝）
 * - 管理战场排队状态
 * - 处理玩家离开战场的请求
 *
 * 相关模块：
 * - Battlefield: 战场基类，定义战场核心逻辑
 * - BattlefieldMgr: 战场管理器，管理所有战场实例
 * - WorldSession: 客户端会话，处理网络消息收发
 *
 * @note 本文件主要处理 Wintergrasp（冬拥湖）和 Tol Barad（托巴拉德）
 *       等世界战场相关的网络通信
 */

#include "WorldSession.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "GameTime.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"

/**
 * @fn void WorldSession::SendBfInvitePlayerToWar(uint32 battleId, uint32 zoneId, uint32 acceptTime)
 *
 * @brief This send to player windows for invite player to join the war.
 *
 * @param battleId      The BattleId of Bf
 * @param zoneId        The zone where the battle is (4197 for wg)
 * @param acceptTime    Time in second that the player have for accept
 */
void WorldSession::SendBfInvitePlayerToWar(uint32 battleId, uint32 zoneId, uint32 acceptTime)
{
    WorldPacket data(SMSG_BATTLEFIELD_MGR_ENTRY_INVITE, 12);
    data << uint32(battleId);
    data << uint32(zoneId);
    data << uint32(GameTime::GetGameTime() + acceptTime);
    SendPacket(&data);
}

/**
 * @fn void WorldSession::SendBfInvitePlayerToQueue(uint32 battleId)
 *
 * @brief This send invitation to player to join the queue.
 *
 * @param battleId      The BattleId of Bf
 */
void WorldSession::SendBfInvitePlayerToQueue(uint32 battleId)
{
    WorldPacket data(SMSG_BATTLEFIELD_MGR_QUEUE_INVITE, 5);
    data << uint32(battleId);
    data << uint8(1); // warmup ? used ?
    SendPacket(&data);
}

/**
 * @fn void WorldSession::SendBfQueueInviteResponse(uint32 battleId, uint32 zoneId, bool canQueue, bool full)
 *
 * @brief This send packet for inform player that he join queue.
 *
 * @param battleId      The BattleId of Bf
 * @param zoneId        The zone where the battle is (4197 for wg)
 * @param canQueue      if able to queue
 * @param full          on log in is full
 */
void WorldSession::SendBfQueueInviteResponse(uint32 battleId, uint32 zoneId, bool canQueue, bool full)
{
    WorldPacket data(SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE, 11);
    data << uint32(battleId);
    data << uint32(zoneId);
    data << uint8(canQueue ? 1 : 0); // Accepted    // 0 you cannot queue wg    // 1 you are queued
    data << uint8(full ? 0 : 1);     // Logging In  // 0 wg full                // 1 queue for upcoming
    data << uint8(1);                // Warmup
    SendPacket(&data);
}

/**
 * @fn void WorldSession::SendBfEntered(uint32 battleId)
 *
 * @brief This is call when player accept to join war.
 *
 * @param battleId      The BattleId of Bf
 */
void WorldSession::SendBfEntered(uint32 battleId)
{
    WorldPacket data(SMSG_BATTLEFIELD_MGR_ENTERED, 7);
    data << uint32(battleId);
    data << uint8(1);                           // unk
    data << uint8(1);                           // unk
    data << uint8(_player->isAFK() ? 1 : 0);    // Clear AFK
    SendPacket(&data);
}

/**
 * @fn void WorldSession::SendBfLeaveMessage(uint32 battleId, BFLeaveReason reason)
 *
 * @brief This is call when player leave battlefield zone.
 *
 * @param battleId      The BattleId of Bf
 * @param reason        Reason why player left battlefield
 */
void WorldSession::SendBfLeaveMessage(uint32 battleId, BFLeaveReason reason /*= BF_LEAVE_REASON_EXITED*/)
{
    WorldPacket data(SMSG_BATTLEFIELD_MGR_EJECTED, 7);
    data << uint32(battleId);
    data << uint8(reason);  // byte Reason
    data << uint8(2);       // byte BattleStatus
    data << uint8(0);       // bool Relocated
    SendPacket(&data);
}

/**
 * @fn void WorldSession::HandleBfQueueInviteResponse(WorldPacket& recvData)
 *
 * @brief Send by client when he click on accept for queue.
 */
void WorldSession::HandleBfQueueInviteResponse(WorldPacket& recvData)
{
    uint32 battleId;
    uint8 accepted;

    recvData >> battleId >> accepted;

    TC_LOG_DEBUG("misc", "HandleBfQueueInviteResponse: BattleID:{} Accepted:{}", battleId, accepted);

    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);
    if (!bf)
        return;

    if (accepted)
        bf->PlayerAcceptInviteToQueue(_player);
}

/**
 * @fn void WorldSession::HandleBfEntryInviteResponse(WorldPacket& recvData)
 *
 * @brief Send by client on clicking in accept or refuse of invitation windows for join game.
 */
void WorldSession::HandleBfEntryInviteResponse(WorldPacket& recvData)
{
    uint32 battleId;
    uint8 accepted;

    recvData >> battleId >> accepted;

    TC_LOG_DEBUG("misc", "HandleBfEntryInviteResponse: battleId: {}, accepted: {}", battleId, accepted);

    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);
    if (!bf)
        return;

    // If player accept invitation
    if (accepted)
    {
        bf->PlayerAcceptInviteToWar(_player);
    }
    else
    {
        if (_player->GetZoneId() == bf->GetZoneId())
            bf->KickPlayerFromBattlefield(_player->GetGUID());
    }
}

/**
 * @fn void WorldSession::HandleBfQueueExitRequest(WorldPacket& recvData)
 *
 * @brief Send by client when exited battlefield
 */
void WorldSession::HandleBfQueueExitRequest(WorldPacket& recvData)
{
    uint32 battleId;

    recvData >> battleId;

    TC_LOG_DEBUG("misc", "HandleBfQueueExitRequest: battleId: {} ", battleId);

    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);
    if (!bf)
        return;

    bf->AskToLeaveQueue(_player);
}
