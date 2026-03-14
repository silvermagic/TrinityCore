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
 * @file LFGScripts.cpp
 * @brief LFG系统脚本实现 - 玩家和队伍事件处理
 *
 * 本文件实现了LFG系统的脚本钩子函数，处理玩家和队伍的各种事件。
 * 这些脚本将游戏核心事件与LFG系统逻辑连接起来。
 *
 * 主要功能：
 * - 处理玩家登录/登出事件
 * - 处理地图变更（进入/离开副本）
 * - 处理队伍成员变化
 * - 处理队伍解散和队长变更
 * - 处理踢人投票
 *
 * 设计思路：
 * - 通过脚本系统实现松耦合
 * - 事件驱动架构
 * - 自动维护LFG状态一致性
 */

/*
 * 核心与LFG脚本的交互实现
 */

#include "LFGScripts.h"
#include "Common.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

namespace lfg
{

/**
 * @brief 构造函数
 *
 * @details 调用父类构造函数，注册脚本名称"LFGPlayerScript"
 */
LFGPlayerScript::LFGPlayerScript() : PlayerScript("LFGPlayerScript") { }

/**
 * @brief 处理玩家登出事件
 *
 * @param player 登出的玩家
 *
 * @details 处理逻辑：
 *          1. 检查LFG选项是否启用
 *          2. 如果玩家没有队伍：
 *             - 关闭LFR列表
 *             - 让玩家离开LFG队列
 *          3. 如果玩家断线且有队伍：
 *             - 标记为断线状态（传递true参数）
 *
 * @note 断线玩家会保留在队伍中，但不会自动匹配
 */
void LFGPlayerScript::OnLogout(Player* player)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    // 处理无队伍的情况
    if (!player->GetGroup())
    {
        player->GetSession()->SendLfgLfrList(false);  // 关闭LFR列表
        sLFGMgr->LeaveLfg(player->GetGUID());          // 离开LFG
    }
    // 处理断线的情况
    else if (player->GetSession()->PlayerDisconnected())
        sLFGMgr->LeaveLfg(player->GetGUID(), true);    // 标记为断线
}

/**
 * @brief 处理玩家登录事件
 *
 * @param player 登录的玩家
 * @param loginFirst 是否首次登录（未使用）
 *
 * @details 处理逻辑：
 *          1. 检查LFG选项是否启用
 *          2. 检查队伍数据一致性：
 *             - 比较玩家实际队伍与LFG记录的队伍
 *             - 如果不一致，修复数据
 *          3. 设置玩家的阵营信息
 *
 * @note 这是一个临时修复，用于解决队伍数据和LFG数据不同步的问题
 */
void LFGPlayerScript::OnLogin(Player* player, bool /*loginFirst*/)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    // 临时修复：尝试确定队伍数据和LFG数据何时不同步
    ObjectGuid guid = player->GetGUID();
    ObjectGuid gguid = sLFGMgr->GetGroup(guid);  // LFG记录的队伍GUID

    if (Group const* group = player->GetGroup())
    {
        ObjectGuid gguid2 = group->GetGUID();  // 实际队伍GUID
        // 检查数据不一致
        if (gguid != gguid2)
        {
            TC_LOG_ERROR("lfg", "{} on group {} but LFG has group {} saved... Fixing.",
                player->GetSession()->GetPlayerInfo(), gguid2.ToString(), gguid.ToString());
            // 修复：重新设置队伍成员信息
            sLFGMgr->SetupGroupMember(guid, group->GetGUID());
        }
    }

    // 设置玩家阵营（联盟/部落）
    sLFGMgr->SetTeam(player->GetGUID(), player->GetTeam());
    /// @todo - 恢复LfgPlayerData并向玩家发送正确的状态（如果之前在队伍中）
}

/**
 * @brief 处理玩家地图变更事件
 *
 * @param player 变更地图的玩家
 *
 * @details 该函数在玩家传送后调用，处理进入/离开副本的逻辑：
 *          1. 进入LFG副本：
 *             - 检查队伍有效性（防止崩溃）
 *             - 发送队伍成员名称查询
 *             - 如果是随机副本，施放"幸运抽奖"增益
 *          2. 离开LFG副本：
 *             - 移除"幸运抽奖"增益
 *             - 如果队伍只剩1人，解散队伍
 *
 * @note 该函数也会在玩家登录时调用（如果玩家在副本中）
 */
void LFGPlayerScript::OnMapChanged(Player* player)
{
    Map const* map = player->GetMap();

    // 检查是否在LFG副本地图中
    if (sLFGMgr->inLfgDungeonMap(player->GetGUID(), map->GetId(), map->GetDifficulty()))
    {
        Group* group = player->GetGroup();
        // 此函数也会在玩家登录时调用
        // 如果LFG系统认为玩家在LFG副本中，但玩家没有有效队伍，
        // 则传送回绑定点以防止崩溃或其他未定义行为
        if (!group)
        {
            sLFGMgr->LeaveLfg(player->GetGUID());
            player->RemoveAurasDueToSpell(LFG_SPELL_LUCK_OF_THE_DRAW);  // 移除"幸运抽奖"
            // 传送回绑定点
            player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, 0.0f);
            TC_LOG_ERROR("lfg", "LFGPlayerScript::OnMapChanged, Player {} {} is in LFG dungeon map but does not have a valid group! "
                "Teleporting to homebind.", player->GetName(), player->GetGUID().ToString());
            return;
        }

        // 发送队伍成员名称查询（用于客户端显示）
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            if (Player* member = itr->GetSource())
                player->GetSession()->SendNameQueryOpcode(member->GetGUID());

        // 如果选择了随机副本，施放"幸运抽奖"增益
        if (sLFGMgr->selectedRandomLfgDungeon(player->GetGUID()))
            player->CastSpell(player, LFG_SPELL_LUCK_OF_THE_DRAW, true);
    }
    else
    {
        // 离开LFG副本
        Group* group = player->GetGroup();
        // 如果队伍只剩1人，解散队伍
        if (group && group->GetMembersCount() == 1)
        {
            sLFGMgr->LeaveLfg(group->GetGUID());
            group->Disband();
            TC_LOG_DEBUG("lfg", "LFGPlayerScript::OnMapChanged, Player {}({}) is last in the lfggroup so we disband the group.",
                player->GetName(), player->GetGUID().ToString());
        }
        // 移除"幸运抽奖"增益
        player->RemoveAurasDueToSpell(LFG_SPELL_LUCK_OF_THE_DRAW);
    }
}

/**
 * @brief 构造函数
 *
 * @details 调用父类构造函数，注册脚本名称"LFGGroupScript"
 */
LFGGroupScript::LFGGroupScript() : GroupScript("LFGGroupScript") { }

/**
 * @brief 处理成员加入队伍事件
 *
 * @param group 队伍指针
 * @param guid 加入的玩家GUID
 *
 * @details 处理逻辑：
 *          1. 检查LFG选项是否启用
 *          2. 如果加入的是队长：
 *             - 设置LFG队长信息
 *          3. 如果加入的是普通成员：
 *             - 如果该成员在队列中，让其离开队列
 *             - 如果队伍在队列中，让队伍离开队列
 *          4. 更新LFG队伍数据
 *
 * @note 新成员加入会导致队伍离开队列，这是预期行为
 */
void LFGGroupScript::OnAddMember(Group* group, ObjectGuid guid)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    ObjectGuid gguid = group->GetGUID();
    ObjectGuid leader = group->GetLeaderGUID();

    // 如果加入的是队长
    if (leader == guid)
    {
        TC_LOG_DEBUG("lfg", "LFGScripts::OnAddMember [{}]: added [{}] leader [{}]", gguid.ToString(), guid.ToString(), leader.ToString());
        sLFGMgr->SetLeader(gguid, guid);
    }
    else
    {
        // 如果加入的是普通成员
        LfgState gstate = sLFGMgr->GetState(gguid);  // 队伍状态
        LfgState state = sLFGMgr->GetState(guid);    // 成员状态
        TC_LOG_DEBUG("lfg", "LFGScripts::OnAddMember [{}]: added [{}] leader [{}] gstate: {}, state: {}", gguid.ToString(), guid.ToString(), leader.ToString(), gstate, state);

        // 如果成员在队列中，让其离开队列
        if (state == LFG_STATE_QUEUED)
            sLFGMgr->LeaveLfg(guid);

        // 如果队伍在队列中，让队伍离开队列
        if (gstate == LFG_STATE_QUEUED)
            sLFGMgr->LeaveLfg(gguid);
    }

    // 更新LFG队伍数据
    sLFGMgr->SetGroup(guid, gguid);
    sLFGMgr->AddPlayerToGroup(gguid, guid);
}

/**
 * @brief 处理成员离开队伍事件
 *
 * @param group 队伍指针
 * @param guid 离开的玩家GUID
 * @param method 离开方式（自愿、被踢、断线等）
 * @param kicker 踢人的玩家GUID（如果是被踢）
 * @param reason 离开原因
 *
 * @details 处理逻辑：
 *          1. 如果是LFG队伍且被踢：
 *             - 初始化踢人投票
 *          2. 否则：
 *             - 让玩家离开LFG
 *             - 如果是自愿离开且在副本中，施放"逃亡者"减益
 *             - 如果是被LFG踢出，移除副本冷却
 *             - 传送玩家出副本
 *          3. 如果副本未完成，向队长发送继续招募提示
 *
 * @note LFG队伍的踢人有特殊限制（需要投票）
 */
void LFGGroupScript::OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, char const* reason)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    ObjectGuid gguid = group->GetGUID();
    TC_LOG_DEBUG("lfg", "LFGScripts::OnRemoveMember [{}]: remove [{}] Method: {} Kicker: [{}] Reason: {}",
        gguid.ToString(), guid.ToString(), method, kicker.ToString(), (reason ? reason : ""));

    bool isLFG = group->isLFGGroup();

    // 如果是LFG队伍且被踢，初始化踢人投票
    if (isLFG && method == GROUP_REMOVEMETHOD_KICK)
    {
        /// @todo - 更新踢人者的内部踢人冷却时间
        std::string str_reason = "";
        if (reason)
            str_reason = std::string(reason);
        sLFGMgr->InitBoot(gguid, kicker, guid, str_reason);
        return;
    }

    LfgState state = sLFGMgr->GetState(gguid);

    // 如果队伍正在形成（提案成功后），只移除数据
    if (state == LFG_STATE_PROPOSAL && method == GROUP_REMOVEMETHOD_DEFAULT)
    {
        // LfgData: 从队伍中移除玩家
        sLFGMgr->SetGroup(guid, ObjectGuid::Empty);
        sLFGMgr->RemovePlayerFromGroup(gguid, guid);
        return;
    }

    // 让玩家离开LFG
    sLFGMgr->LeaveLfg(guid);
    sLFGMgr->SetGroup(guid, ObjectGuid::Empty);
    uint8 players = sLFGMgr->RemovePlayerFromGroup(gguid, guid);

    // 如果玩家在线，执行以下逻辑
    if (Player* player = ObjectAccessor::FindPlayer(guid))
    {
        // 如果是自愿离开且在副本中且队伍人数足够，施放"逃亡者"减益
        if (method == GROUP_REMOVEMETHOD_LEAVE && state == LFG_STATE_DUNGEON &&
            players >= LFG_GROUP_KICK_VOTES_NEEDED)
            player->CastSpell(player, LFG_SPELL_DUNGEON_DESERTER, true);
        // 如果是被LFG踢出，移除副本冷却
        else if (method == GROUP_REMOVEMETHOD_KICK_LFG)
            player->RemoveAurasDueToSpell(LFG_SPELL_DUNGEON_COOLDOWN);
        //else if (state == LFG_STATE_BOOT)
            // 更新被踢者的内部踢人冷却时间

        // 发送更新给客户端
        player->GetSession()->SendLfgUpdateParty(LfgUpdateData(LFG_UPDATETYPE_LEADER_UNK1));
        // 如果在副本中，传送出副本
        if (isLFG && player->GetMap()->IsDungeon())
            sLFGMgr->TeleportPlayer(player, true);
    }

    // 如果副本未完成，向队长发送继续招募提示
    if (isLFG && state != LFG_STATE_FINISHED_DUNGEON)
        if (Player* leader = ObjectAccessor::FindConnectedPlayer(sLFGMgr->GetLeader(gguid)))
            leader->GetSession()->SendLfgOfferContinue(sLFGMgr->GetDungeon(gguid, false));
}

/**
 * @brief 处理队伍解散事件
 *
 * @param group 队伍指针
 *
 * @details 清理队伍的所有LFG数据
 */
void LFGGroupScript::OnDisband(Group* group)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    ObjectGuid gguid = group->GetGUID();
    TC_LOG_DEBUG("lfg", "LFGScripts::OnDisband [{}]", gguid.ToString());

    // 清理队伍的所有LFG数据
    sLFGMgr->RemoveGroupData(gguid);
}

/**
 * @brief 处理队长变更事件
 *
 * @param group 队伍指针
 * @param newLeaderGuid 新队长的GUID
 * @param oldLeaderGuid 旧队长的GUID
 *
 * @details 更新LFG系统的队长信息
 */
void LFGGroupScript::OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    ObjectGuid gguid = group->GetGUID();

    TC_LOG_DEBUG("lfg", "LFGScripts::OnChangeLeader [{}]: old [{}] new [{}]",
        gguid.ToString(), newLeaderGuid.ToString(), oldLeaderGuid.ToString());

    // 更新LFG队长信息
    sLFGMgr->SetLeader(gguid, newLeaderGuid);
}

/**
 * @brief 处理邀请成员事件
 *
 * @param group 队伍指针
 * @param guid 被邀请玩家的GUID
 *
 * @details 处理逻辑：
 *          - 如果队长在LFG队列中，让其离开队列
 *
 * @note 特殊情况说明：
 *       - 无gguid：新队伍正在形成
 *       - 无leader：队伍创建后第一个邀请成为新队长
 *       - 有leader且无gguid：队长加入新队伍后的第一个邀请（真正的邀请）
 */
void LFGGroupScript::OnInviteMember(Group* group, ObjectGuid guid)
{
    // 检查LFG系统是否启用
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    ObjectGuid gguid = group->GetGUID();
    ObjectGuid leader = group->GetLeaderGUID();
    TC_LOG_DEBUG("lfg", "LFGScripts::OnInviteMember [{}]: invite [{}] leader [{}]",
        gguid.ToString(), guid.ToString(), leader.ToString());

    // 无gguid = 新队伍正在形成
    // 无leader = 队伍创建后第一个邀请成为新队长
    // 有leader且无gguid = 队长加入新队伍后的第一个邀请（这是真正的邀请）
    if (leader && !gguid)
        sLFGMgr->LeaveLfg(leader);  // 邀请成员时，队长离开LFG队列
}

/**
 * @brief 注册LFG脚本
 *
 * @details 创建并注册LFG玩家脚本和队伍脚本实例
 *          该函数由脚本系统在服务器启动时自动调用
 */
void AddSC_LFGScripts()
{
    new LFGPlayerScript();   // 创建玩家脚本实例
    new LFGGroupScript();    // 创建队伍脚本实例
}

} // namespace lfg
