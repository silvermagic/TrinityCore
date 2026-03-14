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
 * @file GroupHandler.cpp
 * @brief 队伍/团队系统网络包处理模块
 *
 * @details 本文件实现了所有与队伍和团队相关的网络包处理函数。
 *          涵盖队伍的创建、邀请、加入、离开、解散、权限管理等核心功能。
 *
 * 主要功能模块：
 *
 * 1. 队伍邀请与加入
 *    - HandleGroupInviteOpcode(): 发送队伍邀请
 *    - HandleGroupAcceptOpcode(): 接受队伍邀请
 *    - HandleGroupDeclineOpcode(): 拒绝队伍邀请
 *
 * 2. 成员管理
 *    - HandleGroupUninviteGuidOpcode(): 通过GUID移除成员
 *    - HandleGroupUninviteOpcode(): 通过名称移除成员
 *    - HandleGroupSetLeaderOpcode(): 设置队长
 *    - HandleGroupAssistantLeaderOpcode(): 设置助手
 *    - HandleGroupChangeSubGroupOpcode(): 更改成员子队伍
 *    - HandlePartyAssignmentOpcode(): 分配职责
 *
 * 3. 队伍操作
 *    - HandleGroupDisbandOpcode(): 解散队伍
 *    - HandleGroupRaidConvertOpcode(): 小队转团队
 *
 * 4. 战利品系统
 *    - HandleLootMethodOpcode(): 设置分配方式
 *    - HandleLootRoll(): 战利品掷骰
 *    - HandleOptOutOfLootOpcode(): 自动放弃战利品
 *
 * 5. 辅助功能
 *    - HandleMinimapPingOpcode(): 小地图标记
 *    - HandleRandomRollOpcode(): 随机掷骰
 *    - HandleRaidTargetUpdateOpcode(): 团队目标标记
 *    - HandleRaidReadyCheckOpcode(): 团队就绪确认
 *    - HandleRaidReadyCheckFinishedOpcode(): 就绪确认完成
 *
 * 6. 状态同步
 *    - BuildPartyMemberStatsChangedPacket(): 构建成员状态变更包
 *    - HandleRequestPartyMemberStatsOpcode(): 请求成员状态
 *
 * @note 所有处理函数都是 WorldSession 类的成员函数
 * @note 网络包数据通过 WorldPacket 类传递
 *
 * 与官方服务器的差异：
 *   - 玩家可以移除自己 - 这是一个有用的功能
 *   - 即使队长离线，玩家也可以接受邀请
 *
 * 待办事项：
 *   - group_destroyed 消息已发送但未显示
 *   - 在团队中减少经验获取
 *   - 任务共享需要修正
 *   - 修复 PartyMemberStats 发送
 */

#include "WorldSession.h"
#include "CharacterCache.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "SocialMgr.h"
#include "SpellAuras.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"
#include "WorldPacket.h"

class Aura;

/**
 * @brief 发送队伍操作结果给客户端
 *
 * @brief 职责：
 *   构建并发送队伍操作结果数据包，通知客户端队伍操作的结果状态
 *
 * @param operation 队伍操作类型（邀请、踢出、离开等）
 * @param member 相关成员名称
 * @param res 操作结果代码
 * @param val 附加数值参数，用于LFD冷却时间相关错误（默认为0）
 *
 * @return 无返回值
 */
void WorldSession::SendPartyResult(PartyOperation operation, const std::string& member, PartyResult res, uint32 val /* = 0 */)
{
    WorldPacket data(SMSG_PARTY_COMMAND_RESULT, 4 + member.size() + 1 + 4 + 4);
    data << uint32(operation);
    data << member;
    data << uint32(res);
    data << uint32(val);                                    // LFD 冷却时间相关（用于 ERR_PARTY_LFG_BOOT_COOLDOWN_S 和 ERR_PARTY_LFG_BOOT_NOT_ELIGIBLE_S）

    SendPacket(&data);
}

/**
 * @brief 处理队伍邀请消息（CMSG_GROUP_INVITE）
 *
 * @brief 职责：
 *   处理玩家发送的队伍邀请请求，进行一系列验证后向目标玩家发送邀请通知
 *
 * @param recvData 接收的网络数据包，包含被邀请玩家的名称
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取并规范化被邀请玩家名称
 *   2. 验证玩家是否存在、是否在线
 *   3. 检查各种限制条件：
 *      - 不能邀请自己
 *      - GM邀请限制（是否允许GM组队）
 *      - 阵营限制（是否允许跨阵营组队）
 *      - 实例限制（不同实例副本限制）
 *      - 难度限制（副本难度不匹配）
 *      - 屏蔽列表检查
 *      - 等级限制检查
 *   4. 检查目标玩家是否已在队伍或已有邀请
 *   5. 检查邀请者权限（队长/助手权限）
 *   6. 检查队伍是否已满
 *   7. 创建新队伍或向现有队伍添加邀请
 *   8. 向被邀请者发送邀请通知
 *   9. 向邀请者发送操作结果
 */
void WorldSession::HandleGroupInviteOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_INVITE");

    std::string membername;
    recvData >> membername;
    recvData.read_skip<uint32>();

    // 尝试添加选中的玩家

    // 作弊检查：名称规范化失败
    if (!normalizePlayerName(membername))
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    Player* invitingPlayer = GetPlayer();
    Player* invitedPlayer = ObjectAccessor::FindPlayerByName(membername);

    // 玩家不存在
    if (!invitedPlayer)
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // 玩家试图邀请自己（很可能是作弊）
    if (invitedPlayer == invitingPlayer)
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // GM邀请限制：不允许普通玩家邀请GM组队（除非配置允许）
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_GM_GROUP) && !invitingPlayer->IsGameMaster() && invitedPlayer->IsGameMaster())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_BAD_PLAYER_NAME_S);
        return;
    }

    // 阵营限制：不允许跨阵营组队（除非配置允许或邀请者是GM）
    if (!invitingPlayer->IsGameMaster() && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP) && invitingPlayer->GetTeam() != invitedPlayer->GetTeam())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_PLAYER_WRONG_FACTION);
        return;
    }

    // 实例限制：双方都在实例中但不在同一个实例实例ID，且在同一地图
    if (invitingPlayer->GetInstanceId() != 0 && invitedPlayer->GetInstanceId() != 0 && invitingPlayer->GetInstanceId() != invitedPlayer->GetInstanceId() && invitingPlayer->GetMapId() == invitedPlayer->GetMapId())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_TARGET_NOT_IN_INSTANCE_S);
        return;
    }

    // 难度限制：被邀请者在实例中且副本难度不匹配
    if (invitedPlayer->GetInstanceId() != 0 && invitedPlayer->GetDungeonDifficulty() != invitingPlayer->GetDungeonDifficulty())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_IGNORING_YOU_S);
        return;
    }

    // 屏蔽列表检查：被邀请者已屏蔽邀请者
    if (invitedPlayer->GetSocial()->HasIgnore(invitingPlayer->GetGUID()))
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_IGNORING_YOU_S);
        return;
    }

    // 等级限制检查：邀请者等级不足（除非是好友关系）
    if (!invitedPlayer->GetSocial()->HasFriend(invitingPlayer->GetGUID()) && invitingPlayer->GetLevel() < sWorld->getIntConfig(CONFIG_PARTY_LEVEL_REQ))
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_INVITE_RESTRICTED);
        return;
    }

    // 获取邀请者的队伍信息（处理战场队伍情况）
    Group* group = invitingPlayer->GetGroup();
    if (group && group->isBGGroup())
        group = invitingPlayer->GetOriginalGroup();
    if (!group)
        group = invitingPlayer->GetGroupInvite();

    // 获取被邀请者的队伍信息（处理战场队伍情况）
    Group* group2 = invitedPlayer->GetGroup();
    if (group2 && group2->isBGGroup())
        group2 = invitedPlayer->GetOriginalGroup();

    // 被邀请者已在其他队伍中或已有邀请
    if (group2 || invitedPlayer->GetGroupInvite())
    {
        SendPartyResult(PARTY_OP_INVITE, membername, ERR_ALREADY_IN_GROUP_S);

        if (group2)
        {
            // 通知被邀请者：有人尝试邀请但因已在队伍中而失败
            WorldPacket data(SMSG_GROUP_INVITE, 10);                // 估算大小
            data << uint8(0);                                       // 已被邀请/已在队伍中标志
            data << invitingPlayer->GetName();                      // 最大长度 48
            data << uint32(0);                                      // 未使用
            data << uint8(0);                                       // 计数
            data << uint32(0);                                      // 未使用
            invitedPlayer->SendDirectMessage(&data);
        }

        return;
    }

    // 检查现有队伍的情况
    if (group)
    {
        // 权限检查：邀请者不是队长也不是助手
        if (!group->IsLeader(invitingPlayer->GetGUID()) && !group->IsAssistant(invitingPlayer->GetGUID()))
        {
            if (group->IsCreated())
                SendPartyResult(PARTY_OP_INVITE, "", ERR_NOT_LEADER);
            return;
        }

        // 队伍已满
        if (group->IsFull())
        {
            SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
            return;
        }
    }

    // 队伍不存在，创建新队伍
    // 但不立即创建和保存到数据库，直到至少有一人接受邀请
    if (!group)
    {
        group = new Group();

        // 新队伍：如果添加失败则删除
        if (!group->AddLeaderInvite(invitingPlayer))
        {
            delete group;
            return;
        }

        if (!group->AddInvite(invitedPlayer))
        {
            group->RemoveAllInvites();
            delete group;
            return;
        }
    }
    else
    {
        // 已存在的队伍：如果添加失败则直接返回
        if (!group->AddInvite(invitedPlayer))
        {
            return;
        }
    }

    // 邀请成功，向被邀请者发送邀请通知
    WorldPacket data(SMSG_GROUP_INVITE, 10);                // 估算大小
    data << uint8(1);                                       // 邀请标志（非已在队伍中）
    data << invitingPlayer->GetName();                         // 最大长度 48
    data << uint32(0);                                      // 未使用
    data << uint8(0);                                       // 计数
    data << uint32(0);                                      // 未使用
    invitedPlayer->SendDirectMessage(&data);

    // 向邀请者发送成功结果
    SendPartyResult(PARTY_OP_INVITE, membername, ERR_PARTY_RESULT_OK);
}

/**
 * @brief 处理接受队伍邀请消息（CMSG_GROUP_ACCEPT）
 *
 * @brief 职责：
 *   处理玩家接受队伍邀请的请求，完成队伍创建或加入现有队伍
 *
 * @param recvData 接收的网络数据包（包含未使用的uint32参数）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 获取玩家收到的队伍邀请
 *   2. 从被邀请列表中移除当前玩家
 *   3. 验证邀请的有效性（不能接受自己发起的邀请）
 *   4. 检查队伍是否已满
 *   5. 如果是新建队伍：
 *      - 验证队长是否在线
 *      - 从邀请列表移除队长
 *      - 创建并注册队伍
 *   6. 将玩家添加为队伍成员
 *   7. 广播队伍更新
 */
void WorldSession::HandleGroupAcceptOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_ACCEPT");

    recvData.read_skip<uint32>();
    Group* group = GetPlayer()->GetGroupInvite();

    // 没有收到邀请
    if (!group)
        return;

    // 无论如何都从被邀请列表中移除玩家
    group->RemoveInvite(GetPlayer());

    // 验证：队长不能接受自己发起的邀请
    if (group->GetLeaderGUID() == GetPlayer()->GetGUID())
    {
        TC_LOG_ERROR("network", "HandleGroupAcceptOpcode: player {} {} tried to accept an invite to his own group", GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    // 队伍已满
    if (group->IsFull())
    {
        SendPartyResult(PARTY_OP_INVITE, "", ERR_GROUP_FULL);
        return;
    }

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());

    // 形成新队伍，创建它
    if (!group->IsCreated())
    {
        // 如果队长正在传送过程中可能发生这种情况
        // 一旦实现了传送延迟动作，应移除此检查
        if (!leader)
        {
            group->RemoveAllInvites();
            return;
        }

        // 创建队伍时队长应该在场
        ASSERT(leader);
        group->RemoveInvite(leader);
        group->Create(leader);
        sGroupMgr->AddGroup(group);
    }

    // 一切正常，添加成员（注意：玩家的队伍在ADDMEMBER中设置）
    if (!group->AddMember(GetPlayer()))
        return;

    group->BroadcastGroupUpdate();
}

/**
 * @brief 处理拒绝队伍邀请消息（CMSG_GROUP_DECLINE）
 *
 * @brief 职责：
 *   处理玩家拒绝队伍邀请的请求，取消邀请并通知队长
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 获取玩家收到的队伍邀请
 *   2. 记住队长信息（因为取消邀请后队伍可能被删除）
 *   3. 取消邀请（可能导致队伍解散）
 *   4. 向队长发送拒绝通知
 */
void WorldSession::HandleGroupDeclineOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_DECLINE");

    Group* group = GetPlayer()->GetGroupInvite();
    if (!group)
        return;

    // 记住队长信息（队伍解散后group指针将无效）
    Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());

    // 取消邀请，队伍可能被删除
    GetPlayer()->UninviteFromGroup();

    // 验证队长是否在线
    if (!leader || !leader->GetSession())
        return;

    // 向队长发送拒绝通知
    WorldPacket data(SMSG_GROUP_DECLINE, GetPlayer()->GetName().length());
    data << GetPlayer()->GetName();
    leader->SendDirectMessage(&data);
}

/**
 * @brief 处理通过GUID移除队伍成员消息（CMSG_GROUP_UNINVITE_GUID）
 *
 * @brief 职责：
 *   处理通过GUID移除队伍成员或取消邀请的请求
 *
 * @param recvData 接收的网络数据包，包含目标玩家GUID和原因
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取目标玩家GUID和移除原因
 *   2. 验证不能移除自己
 *   3. 检查移除权限
 *   4. 如果是队伍成员，执行踢出操作
 *   5. 如果是被邀请者，取消其邀请
 */
void WorldSession::HandleGroupUninviteGuidOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_UNINVITE_GUID");

    ObjectGuid guid;
    std::string reason;
    recvData >> guid;
    recvData >> reason;

    // 不能移除自己
    if (guid == GetPlayer()->GetGUID())
    {
        TC_LOG_ERROR("network", "WorldSession::HandleGroupUninviteGuidOpcode: leader {} {} tried to uninvite himself from the group.",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    // 检查移除权限
    PartyResult res = GetPlayer()->CanUninviteFromGroup(guid);
    if (res != ERR_PARTY_RESULT_OK)
    {
        SendPartyResult(PARTY_OP_UNINVITE, "", res);
        return;
    }

    Group* grp = GetPlayer()->GetGroup();
    // 队伍指针已在CanUninviteFromGroup()中检查
    ASSERT(grp);

    // 如果是队伍成员，踢出队伍
    if (grp->IsMember(guid))
    {
        Player::RemoveFromGroup(grp, guid, GROUP_REMOVEMETHOD_KICK, GetPlayer()->GetGUID(), reason.c_str());
        return;
    }

    // 如果是被邀请者，取消邀请
    if (Player* player = grp->GetInvited(guid))
    {
        player->UninviteFromGroup();
        return;
    }

    // 目标不在队伍中
    SendPartyResult(PARTY_OP_UNINVITE, "", ERR_TARGET_NOT_IN_GROUP_S);
}

/**
 * @brief 处理通过名称移除队伍成员消息（CMSG_GROUP_UNINVITE）
 *
 * @brief 职责：
 *   处理通过玩家名称移除队伍成员或取消邀请的请求
 *
 * @param recvData 接收的网络数据包，包含目标玩家名称
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取并规范化目标玩家名称
 *   2. 验证不能移除自己
 *   3. 检查移除权限
 *   4. 如果是队伍成员，执行踢出操作
 *   5. 如果是被邀请者，取消其邀请
 */
void WorldSession::HandleGroupUninviteOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_UNINVITE");

    std::string membername;
    recvData >> membername;

    // 玩家名称规范化失败
    if (!normalizePlayerName(membername))
        return;

    // 不能移除自己
    if (GetPlayer()->GetName() == membername)
    {
        TC_LOG_ERROR("network", "WorldSession::HandleGroupUninviteOpcode: leader {} {} tried to uninvite himself from the group.",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        return;
    }

    // 检查移除权限
    PartyResult res = GetPlayer()->CanUninviteFromGroup();
    if (res != ERR_PARTY_RESULT_OK)
    {
        SendPartyResult(PARTY_OP_UNINVITE, "", res);
        return;
    }

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
        return;

    // 如果是队伍成员，踢出队伍
    if (ObjectGuid guid = grp->GetMemberGUID(membername))
    {
        Player::RemoveFromGroup(grp, guid, GROUP_REMOVEMETHOD_KICK, GetPlayer()->GetGUID());
        return;
    }

    // 如果是被邀请者，取消邀请
    if (Player* player = grp->GetInvited(membername))
    {
        player->UninviteFromGroup();
        return;
    }

    // 目标不在队伍中
    SendPartyResult(PARTY_OP_UNINVITE, membername, ERR_TARGET_NOT_IN_GROUP_S);
}

/**
 * @brief 处理设置队伍队长消息（CMSG_GROUP_SET_LEADER）
 *
 * @brief 职责：
 *   处理将队伍队长转让给其他成员的请求
 *
 * @param recvData 接收的网络数据包，包含新队长的GUID
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取目标玩家GUID
 *   2. 验证当前玩家是否为队长
 *   3. 验证目标玩家是否在同一队伍
 *   4. 更改队长并发送队伍更新
 */
void WorldSession::HandleGroupSetLeaderOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_SET_LEADER");

    ObjectGuid guid;
    recvData >> guid;

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    Group* group = GetPlayer()->GetGroup();

    // 验证队伍和目标玩家是否存在
    if (!group || !player)
        return;

    // 验证：只有队长可以转让队长权限，且目标必须在同一队伍
    if (!group->IsLeader(GetPlayer()->GetGUID()) || player->GetGroup() != group)
        return;

    // 一切正常，更改队长
    group->ChangeLeader(guid);
    group->SendUpdate();
}

/**
 * @brief 处理离开/解散队伍消息（CMSG_GROUP_DISBAND）
 *
 * @brief 职责：
 *   处理玩家离开队伍或取消队伍创建的请求
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 检查玩家是否在队伍或有待处理的邀请
 *   2. 验证玩家不在战场中
 *   3. 如果在队伍中，执行离开操作
 *   4. 如果有待处理的队伍创建且玩家是发起者，解散队伍
 */
void WorldSession::HandleGroupDisbandOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_DISBAND");

    Group* grp = GetPlayer()->GetGroup();
    Group* grpInvite = GetPlayer()->GetGroupInvite();
    if (!grp && !grpInvite)
        return;

    // 战场中不能离开队伍
    if (_player->InBattleground())
        return;

    /** 错误处理 **/
    /********************/

    // 一切正常，执行操作
    if (grp)
    {
        // 离开现有队伍
        SendPartyResult(PARTY_OP_LEAVE, GetPlayer()->GetName(), ERR_PARTY_RESULT_OK);
        GetPlayer()->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
    }
    else if (grpInvite && grpInvite->GetLeaderGUID() == GetPlayer()->GetGUID())
    {
        // 取消待处理的队伍创建
        SendPartyResult(PARTY_OP_LEAVE, GetPlayer()->GetName(), ERR_PARTY_RESULT_OK);
        grpInvite->Disband();
    }
}

/**
 * @brief 处理设置分配方式消息（CMSG_LOOT_METHOD）
 *
 * @brief 职责：
 *   处理设置队伍战利品分配方式的请求
 *
 * @param recvData 接收的网络数据包，包含分配方式、主分配者GUID、品质阈值
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取分配方式、主分配者GUID和品质阈值
 *   2. 验证玩家是否为队长
 *   3. 验证不是随机副本队伍
 *   4. 验证分配方式和品质阈值的合法性
 *   5. 如果是主分配模式，验证主分配者在队伍中
 *   6. 更新队伍分配设置
 */
void WorldSession::HandleLootMethodOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LOOT_METHOD");

    uint32 lootMethod;
    ObjectGuid lootMaster;
    uint32 lootThreshold;
    recvData >> lootMethod >> lootMaster >> lootThreshold;

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    /** 错误处理 **/
    // 只有队长可以设置分配方式
    if (!group->IsLeader(GetPlayer()->GetGUID()))
        return;

    // 随机副本队伍不能更改分配方式
    if (group->isLFGGroup())
        return;

    // 验证分配方式的合法性
    if (lootMethod > NEED_BEFORE_GREED)
        return;

    // 验证品质阈值的合法性
    if (lootThreshold < ITEM_QUALITY_UNCOMMON || lootThreshold > ITEM_QUALITY_ARTIFACT)
        return;

    // 主分配模式下，主分配者必须在队伍中
    if (lootMethod == MASTER_LOOT && !group->IsMember(lootMaster))
        return;
    /********************/

    // 一切正常，更新分配设置
    group->SetLootMethod((LootMethod)lootMethod);
    group->SetMasterLooterGuid(lootMaster);
    group->SetLootThreshold((ItemQualities)lootThreshold);
    group->SendUpdate();
}

/**
 * @brief 处理战利品掷骰消息
 *
 * @brief 职责：
 *   处理玩家对战利品进行掷骰（需求/贪婪/放弃）的请求
 *
 * @param recvData 接收的网络数据包，包含物品GUID、物品槽位、掷骰类型
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取物品GUID、物品槽位和掷骰类型
 *   2. 记录掷骰投票
 *   3. 根据掷骰类型更新成就进度
 */
void WorldSession::HandleLootRoll(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 itemSlot;
    uint8  rollType;
    recvData >> guid;                  // 掷骰物品的GUID
    recvData >> itemSlot;
    recvData >> rollType;              // 0: 放弃, 1: 需求, 2: 贪婪

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // 记录掷骰投票
    if (!group->CountRollVote(GetPlayer()->GetGUID(), guid, rollType))
        return;

    // 根据掷骰类型更新成就进度
    switch (rollType)
    {
        case ROLL_NEED:
            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED, 1);
            break;
        case ROLL_GREED:
            GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED, 1);
            break;
    }
}

/**
 * @brief 处理小地图标记消息（MSG_MINIMAP_PING）
 *
 * @brief 职责：
 *   处理玩家在小地图上标记位置并广播给队伍成员的请求
 *
 * @param recvData 接收的网络数据包，包含标记的坐标(x, y)
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 读取标记坐标
 *   3. 向队伍成员广播标记信息
 */
void WorldSession::HandleMinimapPingOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received MSG_MINIMAP_PING");

    if (!GetPlayer()->GetGroup())
        return;

    float x, y;
    recvData >> x;
    recvData >> y;

    //TC_LOG_DEBUG("Received opcode MSG_MINIMAP_PING X: {}, Y: {}", x, y);

    /** 错误处理 **/
    /********************/

    // 一切正常，向队伍广播标记
    WorldPacket data(MSG_MINIMAP_PING, (8+4+4));
    data << uint64(GetPlayer()->GetGUID());
    data << float(x);
    data << float(y);
    GetPlayer()->GetGroup()->BroadcastPacket(&data, true, -1, GetPlayer()->GetGUID());
}

/**
 * @brief 处理随机掷骰消息（RandomRollClient）
 *
 * @brief 职责：
 *   处理玩家请求执行随机掷骰的请求
 *
 * @param packet 接收的网络数据包，包含最小值和最大值
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取掷骰范围（最小值和最大值）
 *   2. 验证参数合法性
 *   3. 执行随机掷骰
 */
void WorldSession::HandleRandomRollOpcode(WorldPackets::Misc::RandomRollClient& packet)
{
    uint32 minimum, maximum;
    minimum = packet.Min;
    maximum = packet.Max;

    /** 错误处理 **/
    // 验证参数合法性（最大值不能超过10000，用于urand调用）
    if (minimum > maximum || maximum > 10000)                // < 32768 for urand call
        return;
    /********************/

    GetPlayer()->DoRandomRoll(minimum, maximum);
}

/**
 * @brief 处理团队目标标记更新消息（MSG_RAID_TARGET_UPDATE）
 *
 * @brief 职责：
 *   处理团队目标标记的请求或更新操作
 *
 * @param recvData 接收的网络数据包，包含标记类型和目标GUID
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 如果是请求标记列表(0xFF)，发送当前标记
 *   3. 如果是更新标记：
 *      - 验证权限（团队中需要队长或助手权限）
 *      - 验证目标是否有效
 *      - 更新目标标记
 */
void WorldSession::HandleRaidTargetUpdateOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received MSG_RAID_TARGET_UPDATE");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    uint8  x;
    recvData >> x;

    /** 错误处理 **/
    /********************/

    // 一切正常，执行操作
    if (x == 0xFF)                                           // 目标图标请求
    {
        group->SendTargetIconList(this);
    }
    else                                                    // 目标图标更新
    {
        // 团队模式下，只有队长和助手可以设置标记
        if (group->isRaidGroup() && !group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
            return;

        ObjectGuid guid;
        recvData >> guid;

        // 验证目标（如果是玩家，必须在线且不敌对）
        if (guid.IsPlayer())
        {
            Player* target = ObjectAccessor::FindConnectedPlayer(guid);

            if (!target || target->IsHostileTo(GetPlayer()))
                return;
        }

        group->SetTargetIcon(x, _player->GetGUID(), guid);
    }
}

/**
 * @brief 处理小队转团队消息（CMSG_GROUP_RAID_CONVERT）
 *
 * @brief 职责：
 *   处理将小队转换为团队的请求
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 验证玩家不在战场中
 *   3. 验证玩家是队长且队伍至少有2人
 *   4. 执行转换操作
 */
void WorldSession::HandleGroupRaidConvertOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_RAID_CONVERT");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // 战场中不能转换
    if (_player->InBattleground())
        return;

    /** 错误处理 **/
    // 只有队长可以转换，且队伍至少需要2人
    if (!group->IsLeader(GetPlayer()->GetGUID()) || group->GetMembersCount() < 2)
        return;
    /********************/

    // 一切正常，执行转换
    SendPartyResult(PARTY_OP_INVITE, "", ERR_PARTY_RESULT_OK);
    group->ConvertToRaid();
}

/**
 * @brief 处理更改团队成员子队伍消息（CMSG_GROUP_CHANGE_SUB_GROUP）
 *
 * @brief 职责：
 *   处理将团队成员移动到不同子队伍（小队）的请求
 *
 * @param recvData 接收的网络数据包，包含玩家名称和目标子队伍编号
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 读取玩家名称和目标子队伍编号
 *   3. 验证子队伍编号合法性
 *   4. 验证玩家权限（队长或助手）
 *   5. 验证目标子队伍有空位
 *   6. 查找目标玩家GUID
 *   7. 执行子队伍变更
 */
void WorldSession::HandleGroupChangeSubGroupOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_CHANGE_SUB_GROUP");

    // 这里会获得正确的队伍指针，无需检查是否为战场团队
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    std::string name;
    uint8 groupNr;
    recvData >> name;
    recvData >> groupNr;

    // 规范化玩家名称
    if (!normalizePlayerName(name))
        return;

    // 验证子队伍编号合法性（最多8个小队）
    if (groupNr >= MAX_RAID_SUBGROUPS)
        return;

    // 验证权限（只有队长和助手可以移动成员）
    ObjectGuid senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid))
        return;

    // 验证目标子队伍有空位
    if (!group->HasFreeSlotSubGroup(groupNr))
        return;

    // 查找目标玩家GUID（先尝试在线查找，失败则查数据库缓存）
    ObjectGuid guid;
    if (Player* movedPlayer = ObjectAccessor::FindConnectedPlayerByName(name))
        guid = movedPlayer->GetGUID();
    else
        guid = sCharacterCache->GetCharacterGuidByName(name);

    // 玩家不存在
    if (guid.IsEmpty())
        return;

    // 执行子队伍变更
    group->ChangeMembersGroup(guid, groupNr);
}

/**
 * @brief 处理设置团队助手消息（CMSG_GROUP_ASSISTANT_LEADER）
 *
 * @brief 职责：
 *   处理设置或取消团队成员助手权限的请求
 *
 * @param recvData 接收的网络数据包，包含目标玩家GUID和是否设置标志
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 验证玩家是否为队长
 *   3. 读取目标玩家GUID和设置标志
 *   4. 设置或取消助手权限
 */
void WorldSession::HandleGroupAssistantLeaderOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GROUP_ASSISTANT_LEADER");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // 只有队长可以设置助手
    if (!group->IsLeader(GetPlayer()->GetGUID()))
        return;

    ObjectGuid guid;
    bool apply;
    recvData >> guid;
    recvData >> apply;

    // 设置或取消助手权限
    group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_ASSISTANT);
}

/**
 * @brief 处理队伍职责分配消息（MSG_PARTY_ASSIGNMENT）
 *
 * @brief 职责：
 *   处理设置团队成员职责（主坦克/主辅助）的请求
 *
 * @param recvData 接收的网络数据包，包含职责类型、设置标志和目标GUID
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 验证权限（队长或助手）
 *   3. 读取职责类型、设置标志和目标GUID
 *   4. 根据职责类型设置相应的成员标志
 *   5. 发送队伍更新
 */
void WorldSession::HandlePartyAssignmentOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received MSG_PARTY_ASSIGNMENT");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // 验证权限（只有队长和助手可以设置职责）
    ObjectGuid senderGuid = GetPlayer()->GetGUID();
    if (!group->IsLeader(senderGuid) && !group->IsAssistant(senderGuid))
        return;

    uint8 assignment;
    bool apply;
    ObjectGuid guid;
    recvData >> assignment >> apply;
    recvData >> guid;

    // 根据职责类型设置成员标志
    switch (assignment)
    {
        case GROUP_ASSIGN_MAINASSIST:
            // 移除当前主辅助标志（主辅助只能有一个）
            group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINASSIST);
            group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINASSIST);
            break;
        case GROUP_ASSIGN_MAINTANK:
            // 移除当前主坦克标志（主坦克只能有一个）
            group->RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINTANK);           // 如果有的话，移除当前主辅助标志
            group->SetGroupMemberFlag(guid, apply, MEMBER_FLAG_MAINTANK);
            break;
        default:
            break;
    }

    group->SendUpdate();
}

/**
 * @brief 处理团队就绪确认消息（MSG_RAID_READY_CHECK）
 *
 * @brief 职责：
 *   处理发起或响应团队就绪检查的请求
 *
 * @param recvData 接收的网络数据包，空包表示发起检查，非空包表示响应
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 如果是发起检查（空包）：
 *      - 验证权限（队长或助手）
 *      - 向所有成员广播就绪检查请求
 *      - 对离线成员执行就绪检查
 *   3. 如果是响应检查（非空包）：
 *      - 读取就绪状态
 *      - 广播就绪确认
 */
void WorldSession::HandleRaidReadyCheckOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received MSG_RAID_READY_CHECK");

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    if (recvData.empty())                                   // 发起就绪检查请求
    {
        /** 错误处理 **/
        // 只有队长和助手可以发起就绪检查
        if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
            return;
        /********************/

        // 一切正常，向所有成员广播就绪检查请求
        WorldPacket data(MSG_RAID_READY_CHECK, 8);
        data << GetPlayer()->GetGUID();
        group->BroadcastPacket(&data, false, -1);

        // 对离线成员执行就绪检查
        group->OfflineReadyCheck();
    }
    else                                                    // 响应就绪检查
    {
        uint8 state;
        recvData >> state;

        // 一切正常，广播就绪确认
        WorldPacket data(MSG_RAID_READY_CHECK_CONFIRM, 9);
        data << uint64(GetPlayer()->GetGUID());
        data << uint8(state);
        group->BroadcastReadyCheck(&data);
    }
}

/**
 * @brief 处理团队就绪检查完成消息（MSG_RAID_READY_CHECK_FINISHED）
 *
 * @brief 职责：
 *   处理就绪检查完成通知，广播给所有成员
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 验证玩家是否在队伍中
 *   2. 验证权限（队长或助手）
 *   3. 广播就绪检查完成消息
 */
void WorldSession::HandleRaidReadyCheckFinishedOpcode(WorldPacket & /*recvData*/)
{
    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // 只有队长和助手可以发送完成通知
    if (!group->IsLeader(GetPlayer()->GetGUID()) && !group->IsAssistant(GetPlayer()->GetGUID()))
        return;

    // 广播就绪检查完成消息
    WorldPacket data(MSG_RAID_READY_CHECK_FINISHED);
    group->BroadcastPacket(&data, true, -1);
}

/**
 * @brief 构建队伍成员状态变更数据包
 *
 * @brief 职责：
 *   构建包含队伍成员状态变更信息的数据包，用于同步给其他队伍成员
 *
 * @param player 目标玩家对象
 * @param data 输出的网络数据包指针
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 获取玩家的更新标志掩码
 *   2. 根据更新标志扩展相关联的更新标志（如能量类型更新需要同步当前/最大能量）
 *   3. 计算数据包大小并初始化
 *   4. 根据更新标志写入相应的状态数据：
 *      - 玩家状态（在线、PvP、死亡、幽灵、AFK、DND等）
 *      - 生命值（当前/最大）
 *      - 能量值（当前/最大/类型）
 *      - 等级、区域、位置
 *      - 光环信息
 *      - 宠物信息（GUID、名称、模型、生命、能量、光环）
 *      - 载具座位信息
 */
void WorldSession::BuildPartyMemberStatsChangedPacket(Player* player, WorldPacket* data)
{
    uint32 mask = player->GetGroupUpdateFlag();

    // 没有需要更新的标志
    if (mask == GROUP_UPDATE_FLAG_NONE)
        return;

    // 如果更新能量类型，也需要更新当前/最大能量
    if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)                // if update power type, update current/max power also
        mask |= (GROUP_UPDATE_FLAG_CUR_POWER | GROUP_UPDATE_FLAG_MAX_POWER);

    // 宠物同理
    if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)            // same for pets
        mask |= (GROUP_UPDATE_FLAG_PET_CUR_POWER | GROUP_UPDATE_FLAG_PET_MAX_POWER);

    // 计算数据包所需字节数
    uint32 byteCount = 0;
    for (int i = 1; i < GROUP_UPDATE_FLAGS_COUNT; ++i)
        if (mask & (1 << i))
            byteCount += GroupUpdateLength[i];

    data->Initialize(SMSG_PARTY_MEMBER_STATS, 8 + 4 + byteCount);
    *data << player->GetPackGUID();
    *data << uint32(mask);

    // 写入玩家状态
    if (mask & GROUP_UPDATE_FLAG_STATUS)
    {
        uint16 playerStatus = MEMBER_STATUS_ONLINE;
        if (player->IsPvP())
            playerStatus |= MEMBER_STATUS_PVP;

        if (!player->IsAlive())
        {
            if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                playerStatus |= MEMBER_STATUS_GHOST;
            else
                playerStatus |= MEMBER_STATUS_DEAD;
        }

        if (player->IsFFAPvP())
            playerStatus |= MEMBER_STATUS_PVP_FFA;

        if (player->isAFK())
            playerStatus |= MEMBER_STATUS_AFK;

        if (player->isDND())
            playerStatus |= MEMBER_STATUS_DND;

        *data << uint16(playerStatus);
    }

    // 写入当前生命值
    if (mask & GROUP_UPDATE_FLAG_CUR_HP)
        *data << uint32(player->GetHealth());

    // 写入最大生命值
    if (mask & GROUP_UPDATE_FLAG_MAX_HP)
        *data << uint32(player->GetMaxHealth());

    // 写入能量类型和数值
    Powers powerType = player->GetPowerType();
    if (mask & GROUP_UPDATE_FLAG_POWER_TYPE)
        *data << uint8(powerType);

    if (mask & GROUP_UPDATE_FLAG_CUR_POWER)
        *data << uint16(player->GetPower(powerType));

    if (mask & GROUP_UPDATE_FLAG_MAX_POWER)
        *data << uint16(player->GetMaxPower(powerType));

    // 写入等级
    if (mask & GROUP_UPDATE_FLAG_LEVEL)
        *data << uint16(player->GetLevel());

    // 写入区域ID
    if (mask & GROUP_UPDATE_FLAG_ZONE)
        *data << uint16(player->GetZoneId());

    // 写入位置坐标
    if (mask & GROUP_UPDATE_FLAG_POSITION)
    {
        *data << uint16(player->GetPositionX());
        *data << uint16(player->GetPositionY());
    }

    // 写入光环信息
    if (mask & GROUP_UPDATE_FLAG_AURAS)
    {
        uint64 auramask = player->GetAuraUpdateMaskForRaid();
        *data << uint64(auramask);
        for (uint32 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
        {
            if (auramask & (uint64(1) << i))
            {
                AuraApplication const* aurApp = player->GetVisibleAura(i);
                *data << uint32(aurApp ? aurApp->GetBase()->GetId() : 0);
                *data << uint8(1);
            }
        }
    }

    // 写入宠物信息
    Pet* pet = player->GetPet();
    if (mask & GROUP_UPDATE_FLAG_PET_GUID)
    {
        if (pet)
            *data << (uint64) pet->GetGUID();
        else
            *data << (uint64) 0;
    }

    if (mask & GROUP_UPDATE_FLAG_PET_NAME)
    {
        if (pet)
            *data << pet->GetName();
        else
            *data << uint8(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MODEL_ID)
    {
        if (pet)
            *data << uint16(pet->GetDisplayId());
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_CUR_HP)
    {
        if (pet)
            *data << uint32(pet->GetHealth());
        else
            *data << uint32(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MAX_HP)
    {
        if (pet)
            *data << uint32(pet->GetMaxHealth());
        else
            *data << uint32(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_POWER_TYPE)
    {
        if (pet)
            *data << uint8(pet->GetPowerType());
        else
            *data << uint8(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_CUR_POWER)
    {
        if (pet)
            *data << uint16(pet->GetPower(pet->GetPowerType()));
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_MAX_POWER)
    {
        if (pet)
            *data << uint16(pet->GetMaxPower(pet->GetPowerType()));
        else
            *data << uint16(0);
    }

    if (mask & GROUP_UPDATE_FLAG_PET_AURAS)
    {
        if (pet)
        {
            uint64 auramask = pet->GetAuraUpdateMaskForRaid();
            *data << uint64(auramask);
            for (uint32 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
            {
                if (auramask & (uint64(1) << i))
                {
                    AuraApplication const* aurApp = pet->GetVisibleAura(i);
                    *data << uint32(aurApp ? aurApp->GetBase()->GetId() : 0);
                    *data << uint8(aurApp ? aurApp->GetFlags() : 0);
                }
            }
        }
        else
            *data << uint64(0);
    }

    // 写入载具座位信息
    if (mask & GROUP_UPDATE_FLAG_VEHICLE_SEAT)
    {
        if (Vehicle* veh = player->GetVehicle())
            *data << uint32(veh->GetVehicleInfo()->SeatID[player->m_movementInfo.transport.seat]);
        else
            *data << uint32(0);
    }
}

/**
 * @brief 处理请求队伍成员状态消息（CMSG_REQUEST_PARTY_MEMBER_STATS）
 *
 * @brief 职责：
 *   处理客户端请求获取指定队伍成员完整状态信息的请求
 *
 * @param recvData 接收的网络数据包，包含目标玩家GUID
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取目标玩家GUID
 *   2. 如果玩家不在线，发送离线状态包
 *   3. 如果玩家在线，构建包含完整状态信息的数据包：
 *      - 玩家状态（在线、PvP、死亡等）
 *      - 生命值、能量值、等级、区域、位置
 *      - 光环信息
 *      - 宠物完整信息
 *      - 载具座位信息
 *   4. 发送数据包
 */
/*this procedure handles clients CMSG_REQUEST_PARTY_MEMBER_STATS request*/
void WorldSession::HandleRequestPartyMemberStatsOpcode(WorldPacket &recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_REQUEST_PARTY_MEMBER_STATS");
    ObjectGuid Guid;
    recvData >> Guid;

    Player* player = ObjectAccessor::FindConnectedPlayer(Guid);

    // 玩家不在线，发送离线状态
    if (!player)
    {
        WorldPacket data(SMSG_PARTY_MEMBER_STATS_FULL, 3+4+2);
        data << uint8(0);                                   // 仅用于 SMSG_PARTY_MEMBER_STATS_FULL，可能与竞技场/战场相关
        data << Guid.WriteAsPacked();
        data << uint32(GROUP_UPDATE_FLAG_STATUS);
        data << uint16(MEMBER_STATUS_OFFLINE);
        SendPacket(&data);
        return;
    }

    Pet* pet = player->GetPet();
    Powers powerType = player->GetPowerType();

    WorldPacket data(SMSG_PARTY_MEMBER_STATS_FULL, 4+2+2+2+1+2*6+8+1+8);
    data << uint8(0);                                       // 仅用于 SMSG_PARTY_MEMBER_STATS_FULL，可能与竞技场/战场相关
    data << player->GetPackGUID();

    // 设置更新标志（请求完整状态，包含所有常用字段）
    uint32 updateFlags = GROUP_UPDATE_FLAG_STATUS | GROUP_UPDATE_FLAG_CUR_HP | GROUP_UPDATE_FLAG_MAX_HP
                      | GROUP_UPDATE_FLAG_CUR_POWER | GROUP_UPDATE_FLAG_MAX_POWER | GROUP_UPDATE_FLAG_LEVEL
                      | GROUP_UPDATE_FLAG_ZONE | GROUP_UPDATE_FLAG_POSITION | GROUP_UPDATE_FLAG_AURAS
                      | GROUP_UPDATE_FLAG_PET_NAME | GROUP_UPDATE_FLAG_PET_MODEL_ID | GROUP_UPDATE_FLAG_PET_AURAS;

    // 如果能量类型不是法力，需要包含能量类型字段
    if (powerType != POWER_MANA)
        updateFlags |= GROUP_UPDATE_FLAG_POWER_TYPE;

    // 如果有宠物，包含宠物相关字段
    if (pet)
        updateFlags |= GROUP_UPDATE_FLAG_PET_GUID | GROUP_UPDATE_FLAG_PET_CUR_HP | GROUP_UPDATE_FLAG_PET_MAX_HP
                    | GROUP_UPDATE_FLAG_PET_POWER_TYPE | GROUP_UPDATE_FLAG_PET_CUR_POWER | GROUP_UPDATE_FLAG_PET_MAX_POWER;

    // 如果在载具中，包含载具座位信息
    if (player->GetVehicle())
        updateFlags |= GROUP_UPDATE_FLAG_VEHICLE_SEAT;

    // 构建玩家状态标志
    uint16 playerStatus = MEMBER_STATUS_ONLINE;
    if (player->IsPvP())
        playerStatus |= MEMBER_STATUS_PVP;

    if (!player->IsAlive())
    {
        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            playerStatus |= MEMBER_STATUS_GHOST;
        else
            playerStatus |= MEMBER_STATUS_DEAD;
    }

    if (player->IsFFAPvP())
        playerStatus |= MEMBER_STATUS_PVP_FFA;

    if (player->isAFK())
        playerStatus |= MEMBER_STATUS_AFK;

    if (player->isDND())
        playerStatus |= MEMBER_STATUS_DND;

    // 写入状态数据
    data << uint32(updateFlags);
    data << uint16(playerStatus);                           // GROUP_UPDATE_FLAG_STATUS
    data << uint32(player->GetHealth());                    // GROUP_UPDATE_FLAG_CUR_HP
    data << uint32(player->GetMaxHealth());                 // GROUP_UPDATE_FLAG_MAX_HP
    if (updateFlags & GROUP_UPDATE_FLAG_POWER_TYPE)
        data << uint8(powerType);

    data << uint16(player->GetPower(powerType));            // GROUP_UPDATE_FLAG_CUR_POWER
    data << uint16(player->GetMaxPower(powerType));         // GROUP_UPDATE_FLAG_MAX_POWER
    data << uint16(player->GetLevel());                     // GROUP_UPDATE_FLAG_LEVEL
    data << uint16(player->GetZoneId());                    // GROUP_UPDATE_FLAG_ZONE
    data << uint16(player->GetPositionX());                 // GROUP_UPDATE_FLAG_POSITION
    data << uint16(player->GetPositionY());                 // GROUP_UPDATE_FLAG_POSITION

    // 写入光环信息
    uint64 auraMask = 0;
    size_t maskPos = data.wpos();
    data << uint64(auraMask);                               // 占位符
    for (uint8 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
    {
        if (AuraApplication const* aurApp = player->GetVisibleAura(i))
        {
            auraMask |= uint64(1) << i;
            data << uint32(aurApp->GetBase()->GetId());
            data << uint8(aurApp->GetFlags());
        }
    }

    data.put<uint64>(maskPos, auraMask);                    // GROUP_UPDATE_FLAG_AURAS

    // 写入宠物信息
    if (updateFlags & GROUP_UPDATE_FLAG_PET_GUID)
        data << uint64(ASSERT_NOTNULL(pet)->GetGUID());

    data << std::string(pet ? pet->GetName() : "");         // GROUP_UPDATE_FLAG_PET_NAME
    data << uint16(pet ? pet->GetDisplayId() : 0);          // GROUP_UPDATE_FLAG_PET_MODEL_ID

    if (updateFlags & GROUP_UPDATE_FLAG_PET_CUR_HP)
        data << uint32(pet->GetHealth());

    if (updateFlags & GROUP_UPDATE_FLAG_PET_MAX_HP)
        data << uint32(pet->GetMaxHealth());

    if (updateFlags & GROUP_UPDATE_FLAG_PET_POWER_TYPE)
        data << (uint8)pet->GetPowerType();

    if (updateFlags & GROUP_UPDATE_FLAG_PET_CUR_POWER)
        data << uint16(pet->GetPower(pet->GetPowerType()));

    if (updateFlags & GROUP_UPDATE_FLAG_PET_MAX_POWER)
        data << uint16(pet->GetMaxPower(pet->GetPowerType()));

    // 写入宠物光环信息
    uint64 petAuraMask = 0;
    maskPos = data.wpos();
    data << uint64(petAuraMask);                            // 占位符
    if (pet)
    {
        for (uint8 i = 0; i < MAX_AURAS_GROUP_UPDATE; ++i)
        {
            if (AuraApplication const* aurApp = pet->GetVisibleAura(i))
            {
                petAuraMask |= uint64(1) << i;
                data << uint32(aurApp->GetBase()->GetId());
                data << uint8(aurApp->GetFlags());
            }
        }
    }

    data.put<uint64>(maskPos, petAuraMask);                 // GROUP_UPDATE_FLAG_PET_AURAS

    // 写入载具座位信息
    if (updateFlags & GROUP_UPDATE_FLAG_VEHICLE_SEAT)
        data << uint32(player->GetVehicle()->GetVehicleInfo()->SeatID[player->m_movementInfo.transport.seat]);

    SendPacket(&data);
}

/**
 * @brief 处理请求团队信息消息（CMSG_REQUEST_RAID_INFO）
 *
 * @brief 职责：
 *   处理玩家请求团队信息的请求（通常在查看角色选择界面时发送）
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   向玩家发送团队信息
 */
/*!*/void WorldSession::HandleRequestRaidInfoOpcode(WorldPacket & /*recvData*/)
{
    // 每次玩家查看角色选择界面时调用
    _player->SendRaidInfo();
}

/*void WorldSession::HandleGroupCancelOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("WORLD: got CMSG_GROUP_CANCEL.");
}*/

/**
 * @brief 处理自动放弃战利品消息（CMSG_OPT_OUT_OF_LOOT）
 *
 * @brief 职责：
 *   处理玩家设置自动放弃战利品的请求
 *
 * @param recvData 接收的网络数据包，包含是否自动放弃标志
 *
 * @return 无返回值
 *
 * @brief 主要流程：
 *   1. 读取自动放弃标志（1=总是放弃，0=不自动放弃）
 *   2. 验证玩家是否已加载
 *   3. 设置玩家的自动放弃战利品标志
 */
void WorldSession::HandleOptOutOfLootOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_OPT_OUT_OF_LOOT");

    uint32 passOnLoot;
    recvData >> passOnLoot; // 1 总是放弃, 0 不放弃

    // 忽略未加载的玩家
    if (!GetPlayer())                                        // 因为 STATUS_AUTHED 状态需要检查
    {
        if (passOnLoot != 0)
            TC_LOG_ERROR("network", "CMSG_OPT_OUT_OF_LOOT value<>0 for not-loaded character!");
        return;
    }

    GetPlayer()->SetPassOnGroupLoot(passOnLoot != 0);
}
