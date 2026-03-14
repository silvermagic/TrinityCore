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
 * @file ArenaTeamHandler.cpp
 * @brief 竞技场队伍处理模块
 *
 * 本模块处理所有与竞技场队伍相关的网络消息,包括:
 * - 队伍创建、解散、邀请、加入、离开
 * - 队伍信息查询和检查
 * - 队长权限转移和成员管理
 * - 竞技场队伍统计数据查询
 *
 * 竞技场队伍分为三种规模:2v2、3v3、5v5
 * 每个玩家同一时间只能加入一个同规模的竞技场队伍
 */

#include "WorldSession.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattlegroundMgr.h"
#include "CharacterCache.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "SocialMgr.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理检查玩家竞技场队伍信息的消息
 * @param recvData 接收到的数据包,包含目标玩家GUID
 *
 * 当玩家使用检查(Inspect)功能查看其他玩家时调用
 * 需要在检查距离内,且不能是敌对目标
 *
 * 性能注意: 该操作会遍历玩家的所有竞技场队伍槽位
 */
void WorldSession::HandleInspectArenaTeamsOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "MSG_INSPECT_ARENA_TEAMS");

    ObjectGuid guid;
    recvData >> guid;
    TC_LOG_DEBUG("network", "Inspect Arena stats {}", guid.ToString());

    // 查找目标玩家
    Player* player = ObjectAccessor::FindPlayer(guid);

    if (!player)
        return;

    // 检查距离是否在检查范围内
    if (!GetPlayer()->IsWithinDistInMap(player, INSPECT_DISTANCE, false))
        return;

    // 不能检查敌对目标的竞技场信息
    if (GetPlayer()->IsValidAttackTarget(player))
        return;

    // 遍历所有竞技场队伍槽位(2v2, 3v3, 5v5)
    for (uint8 i = 0; i < MAX_ARENA_SLOT; ++i)
    {
        if (uint32 a_id = player->GetArenaTeamId(i))
        {
            // 获取竞技场队伍并发送检查信息
            if (ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(a_id))
                arenaTeam->Inspect(this, player->GetGUID());
        }
    }
}

/**
 * @brief 处理竞技场队伍查询请求
 * @param recvData 接收到的数据包,包含竞技场队伍ID
 *
 * 当客户端请求查询竞技场队伍信息时调用
 * 返回队伍的基本信息和统计数据
 */
void WorldSession::HandleArenaTeamQueryOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ARENA_TEAM_QUERY");

    uint32 arenaTeamId;
    recvData >> arenaTeamId;

    // 查找竞技场队伍并发送查询响应和统计数据
    if (ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId))
    {
        arenaTeam->Query(this);   // 发送队伍基本信息
        arenaTeam->SendStats(this); // 发送队伍统计数据
    }
}

/**
 * @brief 处理竞技场队伍名册查询请求
 * @param recvData 接收到的数据包,包含竞技场队伍ID
 *
 * 当客户端请求查看队伍成员列表时调用
 * 返回队伍的所有成员信息,包括姓名、等级、等级分等
 */
void WorldSession::HandleArenaTeamRosterOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ARENA_TEAM_ROSTER");

    uint32 arenaTeamId;  // 竞技场队伍ID
    recvData >> arenaTeamId;

    // 获取队伍并发送名册信息
    if (ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId))
        arenaTeam->Roster(this);
}

/**
 * @brief 处理竞技场队伍邀请请求
 * @param recvData 接收到的数据包,包含队伍ID和被邀请玩家名称
 *
 * 当队长邀请玩家加入竞技场队伍时调用
 * 执行一系列验证:
 * - 玩家是否存在且在线
 * - 玩家等级是否达到要求
 * - 玩家是否已在同类型队伍中
 * - 玩家是否已有待处理邀请
 * - 队伍是否已满员
 * - 阵营限制检查
 * - 屏蔽列表检查
 *
 * 邀请通过后会在目标玩家客户端显示邀请对话框
 */
void WorldSession::HandleArenaTeamInviteOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_INVITE");

    uint32 arenaTeamId;      // 竞技场队伍ID
    std::string invitedName; // 被邀请玩家名称

    Player* player = nullptr;

    recvData >> arenaTeamId >> invitedName;

    // 验证玩家名称并查找玩家
    if (!invitedName.empty())
    {
        if (!normalizePlayerName(invitedName))
            return;

        player = ObjectAccessor::FindPlayerByName(invitedName);
    }

    // 玩家不存在或离线
    if (!player)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", invitedName, ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S);
        return;
    }

    // 玩家等级不足(必须达到最高等级才能加入竞技场队伍)
    if (!player->IsMaxLevel())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", player->GetName(), ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
        return;
    }

    // 验证竞技场队伍是否存在
    ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    if (!arenaTeam)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM);
        return;
    }

    // 验证邀请者是否在该队伍中
    if (GetPlayer()->GetArenaTeamId(arenaTeam->GetSlot()) != arenaTeamId)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_PERMISSIONS);
        return;
    }

    // 检查目标玩家是否屏蔽了邀请者
    // OK result but don't send invite
    if (player->GetSocial()->HasIgnore(GetPlayer()->GetGUID()))
        return;

    // 检查阵营限制(如果未启用跨阵营交互)
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != GetPlayer()->GetTeam())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        return;
    }

    // 检查玩家是否已在同类型的竞技场队伍中
    if (player->GetArenaTeamId(arenaTeam->GetSlot()))
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", player->GetName(), ERR_ALREADY_IN_ARENA_TEAM_S);
        return;
    }

    // 检查玩家是否已有待处理的邀请
    if (player->GetArenaTeamIdInvited())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", player->GetName(), ERR_ALREADY_INVITED_TO_ARENA_TEAM_S);
        return;
    }

    // 检查队伍是否已满员(队伍类型*2为最大成员数,如2v2最多4人)
    if (arenaTeam->GetMembersSize() >= arenaTeam->GetType() * 2)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, arenaTeam->GetName(), "", ERR_ARENA_TEAM_TOO_MANY_MEMBERS_S);
        return;
    }

    TC_LOG_DEBUG("bg.battleground", "Player {} Invited {} to Join his ArenaTeam", GetPlayer()->GetName(), invitedName);

    // 设置玩家的待处理邀请队伍ID
    player->SetArenaTeamIdInvited(arenaTeam->GetId());

    // 构建并发送邀请数据包给目标玩家
    WorldPacket data(SMSG_ARENA_TEAM_INVITE, (8+10));
    data << GetPlayer()->GetName();
    data << arenaTeam->GetName();
    player->SendDirectMessage(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_ARENA_TEAM_INVITE");
}

/**
 * @brief 处理接受竞技场队伍邀请
 * @param recvData 接收到的数据包(空数据包)
 *
 * 当玩家接受竞技场队伍邀请时调用
 * 执行最后的验证并将玩家添加到队伍中
 */
void WorldSession::HandleArenaTeamAcceptOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_ACCEPT");  // 空操作码

    // 获取玩家被邀请的竞技场队伍
    ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(_player->GetArenaTeamIdInvited());
    if (!arenaTeam)
        return;

    // 检查玩家是否已在另一个同类型的队伍中
    if (_player->GetArenaTeamId(arenaTeam->GetSlot()))
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ALREADY_IN_ARENA_TEAM);
        return;
    }

    // 检查阵营限制(如果未启用跨阵营交互)
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && _player->GetTeam() != sCharacterCache->GetCharacterTeamByGuid(arenaTeam->GetCaptain()))
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        return;
    }

    // 添加玩家到队伍
    if (!arenaTeam->AddMember(_player->GetGUID()))
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_INTERNAL);
        return;
    }

    // 广播加入事件给所有成员
    arenaTeam->BroadcastEvent(ERR_ARENA_TEAM_JOIN_SS, _player->GetGUID(), 2, _player->GetName(), arenaTeam->GetName(), "");
}

/**
 * @brief 处理拒绝竞技场队伍邀请
 * @param recvData 接收到的数据包(空数据包)
 *
 * 当玩家拒绝竞技场队伍邀请时调用
 * 清除玩家的待处理邀请记录
 */
void WorldSession::HandleArenaTeamDeclineOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_DECLINE");  // 空操作码

    // 清除玩家的待处理邀请
    _player->SetArenaTeamIdInvited(0);
}

/**
 * @brief 处理离开竞技场队伍请求
 * @param recvData 接收到的数据包,包含竞技场队伍ID
 *
 * 当玩家主动离开竞技场队伍时调用
 * 执行以下验证:
 * - 玩家不能在竞技场比赛中离开
 * - 队长在队伍有其他成员时不能直接离开,需要先转让队长
 * - 玩家不能在排队过程中离开
 *
 * 如果队长是唯一成员,离开会导致队伍解散
 */
void WorldSession::HandleArenaTeamLeaveOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_LEAVE");

    uint32 arenaTeamId;
    recvData >> arenaTeamId;

    ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    if (!arenaTeam)
        return;

    // 不允许在竞技场比赛中离开队伍
    if (_player->InArena())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, "", "", ERR_ARENA_TEAM_INTERNAL);
        return;
    }

    // 队长不能直接离开队伍,除非是唯一成员
    if (_player->GetGUID() == arenaTeam->GetCaptain() && arenaTeam->GetMembersSize() > 1)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, "", "", ERR_ARENA_TEAM_LEADER_LEAVE_S);
        return;
    }

    // 检查玩家是否在战场队列中
    if (BattlegroundQueueTypeId bgQueue = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, arenaTeam->GetType()))
    {
        GroupQueueInfo ginfo;
        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(bgQueue);
        if (queue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
            // 如果已经收到战场邀请,不能离开队伍
            if (ginfo.IsInvitedToBGInstanceGUID)
            {
                SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, "", "", ERR_ARENA_TEAMS_LOCKED);
                return;
            }
    }

    // 如果队伍只有队长一人,则解散队伍
    if (_player->GetGUID() == arenaTeam->GetCaptain())
    {
        arenaTeam->Disband(this);
        delete arenaTeam;
        return;
    }
    else
        arenaTeam->DelMember(_player->GetGUID(), true);

    // 广播离开事件
    arenaTeam->BroadcastEvent(ERR_ARENA_TEAM_LEAVE_SS, _player->GetGUID(), 2, _player->GetName(), arenaTeam->GetName(), "");

    // 通知离开的玩家
    SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, arenaTeam->GetName(), "", 0);
}

/**
 * @brief 处理解散竞技场队伍请求
 * @param recvData 接收到的数据包,包含竞技场队伍ID
 *
 * 当队长请求解散竞技场队伍时调用
 * 执行以下验证:
 * - 只有队长可以解散队伍
 * - 队伍不能在排队过程中解散
 * - 队伍不能在比赛中解散
 */
void WorldSession::HandleArenaTeamDisbandOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_DISBAND");

    uint32 arenaTeamId;
    recvData >> arenaTeamId;

    if (ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId))
    {
        // 只有队长可以解散队伍
        if (arenaTeam->GetCaptain() != _player->GetGUID())
            return;

        // 检查队伍是否在战场队列中
        if (BattlegroundQueueTypeId bgQueue = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, arenaTeam->GetType()))
        {
            GroupQueueInfo ginfo;
            BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(bgQueue);
            if (queue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
                // 如果已收到战场邀请,不能解散队伍
                if (ginfo.IsInvitedToBGInstanceGUID)
                    return;
        }

        // 队伍不能在比赛中解散
        if (arenaTeam->IsFighting())
            return;

        // 解散队伍
        arenaTeam->Disband(this);
        delete arenaTeam;
    }
}

/**
 * @brief 处理移除竞技场队伍成员请求
 * @param recvData 接收到的数据包,包含队伍ID和要移除的成员名称
 *
 * 当队长移除队伍成员时调用
 * 执行以下验证:
 * - 只有队长可以移除成员
 * - 不能移除队长自己
 * - 不能在排队过程中移除
 * - 不能在比赛中移除
 */
void WorldSession::HandleArenaTeamRemoveOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_REMOVE");

    uint32 arenaTeamId;
    std::string name;

    recvData >> arenaTeamId;
    recvData >> name;

    // 验证竞技场队伍是否存在
    ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    if (!arenaTeam)
        return;

    // 只有队长可以移除成员
    if (arenaTeam->GetCaptain() != _player->GetGUID())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_PERMISSIONS);
        return;
    }

    // 规范化玩家名称
    if (!normalizePlayerName(name))
        return;

    // 检查成员是否存在于队伍中
    ArenaTeamMember* member = arenaTeam->GetMember(name);
    if (!member)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", name, ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S);
        return;
    }

    // 不能移除队长
    if (arenaTeam->GetCaptain() == member->Guid)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, "", "", ERR_ARENA_TEAM_LEADER_LEAVE_S);
        return;
    }

    // 检查队伍是否在战场队列中
    if (BattlegroundQueueTypeId bgQueue = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, arenaTeam->GetType()))
    {
        GroupQueueInfo ginfo;
        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(bgQueue);
        if (queue.GetPlayerGroupInfoData(_player->GetGUID(), &ginfo))
            // 如果已收到战场邀请,不能移除成员
            if (ginfo.IsInvitedToBGInstanceGUID)
            {
                SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, "", "", ERR_ARENA_TEAMS_LOCKED);
                return;
            }
    }

    // 不能在比赛中移除成员
    if (arenaTeam->IsFighting())
        return;

    // 从队伍中移除成员
    arenaTeam->DelMember(member->Guid, true);

    // 广播移除事件
    arenaTeam->BroadcastEvent(ERR_ARENA_TEAM_REMOVE_SSS, ObjectGuid::Empty, 3, name, arenaTeam->GetName(), _player->GetName());
}

/**
 * @brief 处理转让队长权限请求
 * @param recvData 接收到的数据包,包含队伍ID和新队长名称
 *
 * 当队长转让队长权限给其他成员时调用
 * 执行以下验证:
 * - 只有当前队长可以转让权限
 * - 新队长必须是队伍成员
 * - 不能转让给自己
 */
void WorldSession::HandleArenaTeamLeaderOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_ARENA_TEAM_LEADER");

    uint32 arenaTeamId;
    std::string name;

    recvData >> arenaTeamId;
    recvData >> name;

    // 验证竞技场队伍是否存在
    ArenaTeam* arenaTeam = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    if (!arenaTeam)
        return;

    // 只有队长可以转让权限
    if (arenaTeam->GetCaptain() != _player->GetGUID())
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", "", ERR_ARENA_TEAM_PERMISSIONS);
        return;
    }

    // 规范化玩家名称
    if (!normalizePlayerName(name))
        return;

    // 检查目标成员是否存在于队伍中
    ArenaTeamMember* member = arenaTeam->GetMember(name);
    if (!member)
    {
        SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", name, ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S);
        return;
    }

    // 不能转让给自己
    if (arenaTeam->GetCaptain() == member->Guid)
        return;

    // 设置新队长
    arenaTeam->SetCaptain(member->Guid);

    // 广播队长变更事件
    arenaTeam->BroadcastEvent(ERR_ARENA_TEAM_LEADER_CHANGED_SSS, ObjectGuid::Empty, 3, _player->GetName(), name, arenaTeam->GetName());
}

/**
 * @brief 发送竞技场队伍命令结果给客户端
 * @param teamAction 队伍动作类型(如邀请、离开、解散等)
 * @param team 队伍名称
 * @param player 玩家名称
 * @param errorId 错误ID或结果码
 *
 * 用于向客户端发送竞技场队伍操作的结果反馈
 * 包括成功或失败的各种情况
 */
void WorldSession::SendArenaTeamCommandResult(uint32 teamAction, const std::string& team, const std::string& player, uint32 errorId)
{
    WorldPacket data(SMSG_ARENA_TEAM_COMMAND_RESULT, 4+team.length()+1+player.length()+1+4);
    data << uint32(teamAction);
    data << team;
    data << player;
    data << uint32(errorId);
    SendPacket(&data);
}

/**
 * @brief 发送"不在竞技场队伍中"错误包
 * @param type 竞技场类型(2=2v2, 3=3v3, 5=5v5)
 *
 * 当玩家尝试执行竞技场队伍操作但没有加入相应类型的队伍时调用
 */
void WorldSession::SendNotInArenaTeamPacket(uint8 type)
{
    WorldPacket data(SMSG_ARENA_ERROR, 4+1);  // 886 - You are not in a %uv%u arena team
    uint32 unk = 0;
    data << uint32(unk);                       // 未知字段(0)
    if (!unk)
        data << uint8(type);                   // 队伍类型(2=2v2, 3=3v3, 5=5v5),可用于自定义类型
    SendPacket(&data);
}

/*
+ERR_ARENA_NO_TEAM_II "You are not in a %dv%d arena team"

+ERR_ARENA_TEAM_CREATE_S "%s created.  To disband, use /teamdisband [2v2, 3v3, 5v5]."
+ERR_ARENA_TEAM_INVITE_SS "You have invited %s to join %s"
+ERR_ARENA_TEAM_QUIT_S "You are no longer a member of %s"
ERR_ARENA_TEAM_FOUNDER_S "Congratulations, you are a founding member of %s!  To leave, use /teamquit [2v2, 3v3, 5v5]."

+ERR_ARENA_TEAM_INTERNAL "Internal arena team error"
+ERR_ALREADY_IN_ARENA_TEAM "You are already in an arena team of that size"
+ERR_ALREADY_IN_ARENA_TEAM_S "%s is already in an arena team of that size"
+ERR_INVITED_TO_ARENA_TEAM "You have already been invited into an arena team"
+ERR_ALREADY_INVITED_TO_ARENA_TEAM_S "%s has already been invited to an arena team"
+ERR_ARENA_TEAM_NAME_INVALID "That name contains invalid characters, please enter a new name"
+ERR_ARENA_TEAM_NAME_EXISTS_S "There is already an arena team named \"%s\""
+ERR_ARENA_TEAM_LEADER_LEAVE_S "You must promote a new team captain using /teamcaptain before leaving the team"
+ERR_ARENA_TEAM_PERMISSIONS "You don't have permission to do that"
+ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM "You are not in an arena team of that size"
+ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM_SS "%s is not in %s"
+ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S "\"%s\" not found"
+ERR_ARENA_TEAM_NOT_ALLIED "You cannot invite players from the opposing alliance"

+ERR_ARENA_TEAM_JOIN_SS "%s has joined %s"
+ERR_ARENA_TEAM_YOU_JOIN_S "You have joined %s.  To leave, use /teamquit [2v2, 3v3, 5v5]."

+ERR_ARENA_TEAM_LEAVE_SS "%s has left %s"

+ERR_ARENA_TEAM_LEADER_IS_SS "%s is the captain of %s"
+ERR_ARENA_TEAM_LEADER_CHANGED_SSS "%s has made %s the new captain of %s"

+ERR_ARENA_TEAM_REMOVE_SSS "%s has been kicked out of %s by %s"

+ERR_ARENA_TEAM_DISBANDED_S "%s has disbanded %s"

ERR_ARENA_TEAM_TARGET_TOO_LOW_S "%s is not high enough level to join your team"

ERR_ARENA_TEAM_TOO_MANY_MEMBERS_S "%s is full"

ERR_ARENA_TEAM_LEVEL_TOO_LOW_I "You must be level %d to form an arena team"
*/
