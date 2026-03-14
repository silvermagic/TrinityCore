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
 * @file GuildHandler.cpp
 * @brief 公会系统网络包处理模块
 *
 * @details 本文件实现了所有与公会相关的网络包处理函数。
 *          涵盖公会的创建、邀请、管理、银行系统等核心功能。
 *
 * 主要功能模块：
 *
 * 1. 公会查询与创建
 *    - HandleGuildQueryOpcode(): 查询公会信息
 *    - HandleGuildCreateOpcode(): 创建公会(已禁用)
 *
 * 2. 成员管理
 *    - HandleGuildInviteOpcode(): 邀请成员加入
 *    - HandleGuildAcceptOpcode(): 接受公会邀请
 *    - HandleGuildDeclineOpcode(): 拒绝公会邀请
 *    - HandleGuildRemoveOpcode(): 移除成员(踢人)
 *    - HandleGuildLeaveOpcode(): 离开公会
 *    - HandleGuildPromoteOpcode(): 晋升成员
 *    - HandleGuildDemoteOpcode(): 降级成员
 *
 * 3. 公会管理
 *    - HandleGuildSetGuildMaster(): 转让会长
 *    - HandleGuildDelete(): 解散公会
 *    - HandleGuildUpdateMotdText(): 更新每日消息(MOTD)
 *    - HandleGuildUpdateInfoText(): 更新公会信息文本
 *    - HandleGuildSetPublicNoteOpcode(): 设置公开备注
 *    - HandleGuildSetOfficerNoteOpcode(): 设置官员备注
 *
 * 4. 等级权限管理
 *    - HandleGuildSetRankPermissions(): 设置等级权限
 *    - HandleGuildAddRankOpcode(): 添加新等级
 *    - HandleGuildDeleteRank(): 删除等级
 *
 * 5. 公会信息查询
 *    - HandleGuildInfoOpcode(): 查询公会基本信息
 *    - HandleGuildRosterOpcode(): 查询公会花名册
 *    - HandleGuildEventLogQueryOpcode(): 查询事件日志
 *
 * 6. 公会银行系统
 *    - HandleGuildBankActivate(): 激活公会银行
 *    - HandleGuildBankQueryTab(): 查询银行标签页
 *    - HandleGuildBankDepositMoney(): 存入金币
 *    - HandleGuildBankWithdrawMoney(): 提取金币
 *    - HandleGuildBankSwapItems(): 物品交换
 *    - HandleGuildBankBuyTab(): 购买银行标签页
 *    - HandleGuildBankUpdateTab(): 更新标签页信息
 *    - HandleGuildBankLogQuery(): 查询银行日志
 *    - HandleGuildBankTextQuery(): 查询标签页文本
 *    - HandleGuildBankSetTabText(): 设置标签页文本
 *    - HandleGuildBankMoneyWithdrawn(): 查询金币提取限额
 *    - HandleGuildPermissionsQuery(): 查询公会权限
 *
 * 7. 公会徽章
 *    - HandleSaveGuildEmblemOpcode(): 保存公会徽章
 *
 * @note 所有处理函数都是 WorldSession 类的成员函数
 * @note 网络包数据通过 WorldPacket 类传递
 */

#include "WorldSession.h"
#include "Common.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"

/**
 * @brief 处理公会查询请求
 *
 * 职责：
 *   响应客户端查询公会信息的请求,返回公会的基本信息包括名称、等级权限等
 *
 * 参数：
 *   @param query 包含要查询的公会ID的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleQuery() 发送查询结果给客户端
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 验证公会ID是否有效(不为0)
 *   3. 根据公会ID查找公会对象
 *   4. 如果公会存在,调用公会的HandleQuery方法发送公会信息
 */
void WorldSession::HandleGuildQueryOpcode(WorldPackets::Guild::QueryGuildInfo& query)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_QUERY [{}]: Guild: {}", GetPlayerInfo(), query.GuildId);
    if (!query.GuildId)
        return;

    if (Guild* guild = sGuildMgr->GetGuildById(query.GuildId))
        guild->HandleQuery(this);
}

/**
 * @brief 处理公会创建请求(已禁用)
 *
 * 职责：
 *   记录可能的作弊行为,客户端不应直接发送公会创建请求
 *   公会的创建应该通过游戏内的正规流程完成
 *
 * 参数：
 *   @param packet 包含要创建的公会名称的数据包
 *
 * 返回值：
 *   void - 仅记录错误日志,不执行任何操作
 *
 * 主要流程：
 *   1. 记录错误日志,标识可能的作弊尝试
 *   2. 不执行任何实际创建操作
 */
void WorldSession::HandleGuildCreateOpcode(WorldPackets::Guild::GuildCreate& packet)
{
    TC_LOG_ERROR("entities.player.cheat", "CMSG_GUILD_CREATE: Possible hacking-attempt: {} tried to create a guild [Name: {}] using cheats", GetPlayerInfo(), packet.GuildName);
}

/**
 * @brief 处理公会邀请请求
 *
 * 职责：
 *   允许公会成员邀请其他玩家加入公会
 *   验证邀请者是否有权限邀请,被邀请者是否可以加入
 *
 * 参数：
 *   @param packet 包含被邀请玩家名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleInviteMember() 发送邀请或错误消息
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 标准化玩家名称(首字母大写,其余小写)
 *   3. 获取邀请者所在的公会
 *   4. 如果玩家在公会中,调用公会邀请成员方法处理邀请逻辑
 */
void WorldSession::HandleGuildInviteOpcode(WorldPackets::Guild::GuildInviteByName& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_INVITE [{}]: Invited: {}", GetPlayerInfo(), packet.Name);
    if (normalizePlayerName(packet.Name))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleInviteMember(this, packet.Name);
}

/**
 * @brief 处理公会成员移除请求(踢人)
 *
 * 职责：
 *   允许有权限的公会成员将其他成员从公会中移除
 *   验证操作者权限和目标成员是否可以被移除
 *
 * 参数：
 *   @param packet 包含要移除的成员名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleRemoveMember() 执行移除或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 标准化目标玩家名称
 *   3. 获取操作者所在的公会
 *   4. 如果公会存在,调用公会移除成员方法处理移除逻辑
 */
void WorldSession::HandleGuildRemoveOpcode(WorldPackets::Guild::GuildOfficerRemoveMember& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_REMOVE [{}]: Target: {}", GetPlayerInfo(), packet.Removee);

    if (normalizePlayerName(packet.Removee))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleRemoveMember(this, packet.Removee);
}

/**
 * @brief 处理接受公会邀请
 *
 * 职责：
 *   玩家接受公会的邀请,正式加入公会
 *   验证玩家是否已被邀请且当前不在任何公会中
 *
 * 参数：
 *   @param invite 邀请确认数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->HandleAcceptMember() 完成加入流程
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 检查玩家是否已经在一个公会中(不允许重复加入)
 *   3. 获取玩家被邀请加入的公会ID
 *   4. 查找邀请公会的对象
 *   5. 如果公会存在,调用公会的接受成员方法完成加入
 */
void WorldSession::HandleGuildAcceptOpcode(WorldPackets::Guild::AcceptGuildInvite& /*invite*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_ACCEPT [{}]", GetPlayer()->GetName());

    if (!GetPlayer()->GetGuildId())
        if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildIdInvited()))
            guild->HandleAcceptMember(this);
}

/**
 * @brief 处理拒绝公会邀请
 *
 * 职责：
 *   玩家拒绝公会的邀请,清除邀请状态
 *
 * 参数：
 *   @param decline 拒绝邀请数据包(未使用)
 *
 * 返回值：
 *   void - 清除玩家的被邀请状态
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 检查玩家是否已经在公会中(如果在则直接返回)
 *   3. 清除玩家被邀请的公会ID(设置为0)
 */
void WorldSession::HandleGuildDeclineOpcode(WorldPackets::Guild::GuildDeclineInvitation& /*decline*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_DECLINE [{}]", GetPlayerInfo());
    if (GetPlayer()->GetGuildId())
        return;

    GetPlayer()->SetGuildIdInvited(0);
}

/**
 * @brief 处理公会基本信息查询请求
 *
 * 职责：
 *   向客户端发送公会的基本信息,包括创建日期、成员数量等
 *
 * 参数：
 *   @param packet 请求数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->SendInfo() 发送公会信息
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,发送公会基本信息给客户端
 */
void WorldSession::HandleGuildInfoOpcode(WorldPackets::Guild::GuildGetInfo& /*packet*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_INFO [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendInfo(this);
}

/**
 * @brief 处理公会花名册查询请求
 *
 * 职责：
 *   向客户端发送公会所有成员的详细列表信息
 *
 * 参数：
 *   @param packet 请求数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->HandleRoster() 发送成员列表或错误消息
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,发送公会成员列表
 *   4. 如果玩家不在公会中,发送错误提示"未加入公会"
 */
void WorldSession::HandleGuildRosterOpcode(WorldPackets::Guild::GuildGetRoster& /*packet*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_ROSTER [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleRoster(this);
    else
        Guild::SendCommandResult(this, GUILD_COMMAND_ROSTER, ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

/**
 * @brief 处理公会成员晋升请求
 *
 * 职责：
 *   允许有权限的公会成员提升其他成员的公会等级
 *   验证操作者权限和目标是否可以被晋升
 *
 * 参数：
 *   @param promote 包含要晋升的成员名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleUpdateMemberRank() 执行晋升或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 标准化目标玩家名称
 *   3. 获取操作者所在的公会
 *   4. 如果公会存在,调用公会更新等级方法(降级参数为false表示晋升)
 */
void WorldSession::HandleGuildPromoteOpcode(WorldPackets::Guild::GuildPromoteMember& promote)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_PROMOTE [{}]: Target: {}", GetPlayerInfo(), promote.Promotee);

    if (normalizePlayerName(promote.Promotee))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleUpdateMemberRank(this, promote.Promotee, false);
}

/**
 * @brief 处理公会成员降级请求
 *
 * 职责：
 *   允许有权限的公会成员降低其他成员的公会等级
 *   验证操作者权限和目标是否可以被降级
 *
 * 参数：
 *   @param demote 包含要降级的成员名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleUpdateMemberRank() 执行降级或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 标准化目标玩家名称
 *   3. 获取操作者所在的公会
 *   4. 如果公会存在,调用公会更新等级方法(降级参数为true表示降级)
 */
void WorldSession::HandleGuildDemoteOpcode(WorldPackets::Guild::GuildDemoteMember& demote)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_DEMOTE [{}]: Target: {}", GetPlayerInfo(), demote.Demotee);

    if (normalizePlayerName(demote.Demotee))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleUpdateMemberRank(this, demote.Demotee, true);
}

/**
 * @brief 处理离开公会请求
 *
 * 职责：
 *   允许玩家主动离开当前所在的公会
 *   如果是公会会长离开,可能触发公会解散或转让会长权限
 *
 * 参数：
 *   @param leave 离开公会数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->HandleLeaveMember() 处理离开流程
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会的离开成员方法处理离开逻辑
 */
void WorldSession::HandleGuildLeaveOpcode(WorldPackets::Guild::GuildLeave& /*leave*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_LEAVE [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleLeaveMember(this);
}

/**
 * @brief 处理解散公会请求
 *
 * 职责：
 *   允许公会会长解散整个公会
 *   解散后所有成员将失去公会身份,公会数据将被删除
 *
 * 参数：
 *   @param packet 解散公会数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->HandleDisband() 执行解散或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会的解散方法处理解散逻辑
 */
void WorldSession::HandleGuildDelete(WorldPackets::Guild::GuildDelete& /*packet*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_DISBAND [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleDisband(this);
}

/**
 * @brief 处理转让会长权限请求
 *
 * 职责：
 *   允许当前公会会长将会长职位转让给其他公会成员
 *   验证操作者是否为会长,目标成员是否在公会中
 *
 * 参数：
 *   @param packet 包含新会长名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetLeader() 执行转让或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 标准化新会长名称
 *   3. 获取当前会长所在的公会
 *   4. 如果公会存在,调用公会的设置会长方法处理转让逻辑
 */
void WorldSession::HandleGuildSetGuildMaster(WorldPackets::Guild::GuildSetGuildMaster& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_LEADER [{}]: Target: {}", GetPlayerInfo(), packet.NewMasterName);

    if (normalizePlayerName(packet.NewMasterName))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleSetLeader(this, packet.NewMasterName);
}

/**
 * @brief 处理更新公会每日消息(MOTD)请求
 *
 * 职责：
 *   允许有权限的公会成员更新公会的每日消息
 *   MOTD会在成员登录时显示
 *
 * 参数：
 *   @param packet 包含新的MOTD文本的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetMOTD() 更新MOTD或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志(包含新的MOTD内容)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会的设置MOTD方法更新消息
 */
void WorldSession::HandleGuildUpdateMotdText(WorldPackets::Guild::GuildUpdateMotdText& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_MOTD [{}]: MOTD: {}", GetPlayerInfo(), packet.MotdText);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleSetMOTD(this, packet.MotdText);
}

/**
 * @brief 处理设置公会成员公开备注请求
 *
 * 职责：
 *   允许有权限的公会成员为其他成员设置公开备注
 *   公开备注对所有公会成员可见
 *
 * 参数：
 *   @param packet 包含目标成员名称和备注内容的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetMemberNote() 更新备注或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志(包含目标成员和备注内容)
 *   2. 标准化目标玩家名称
 *   3. 获取操作者所在的公会
 *   4. 如果公会存在,调用公会设置备注方法(isOfficerNote=false表示公开备注)
 */
void WorldSession::HandleGuildSetPublicNoteOpcode(WorldPackets::Guild::GuildSetMemberNote& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_SET_PUBLIC_NOTE [{}]: Target: {}, Note: {}",
         GetPlayerInfo(), packet.NoteeName, packet.Note);

    if (normalizePlayerName(packet.NoteeName))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleSetMemberNote(this, packet.NoteeName, packet.Note, false);
}

/**
 * @brief 处理设置公会成员官员备注请求
 *
 * 职责：
 *   允许有权限的公会成员为其他成员设置官员备注
 *   官员备注只对有相应权限的成员可见(通常是官员级别)
 *
 * 参数：
 *   @param packet 包含目标成员名称和备注内容的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetMemberNote() 更新备注或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志(包含目标成员和备注内容)
 *   2. 标准化目标玩家名称
 *   3. 获取操作者所在的公会
 *   4. 如果公会存在,调用公会设置备注方法(isOfficerNote=true表示官员备注)
 */
void WorldSession::HandleGuildSetOfficerNoteOpcode(WorldPackets::Guild::GuildSetMemberNote& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_SET_OFFICER_NOTE [{}]: Target: {}, Note: {}",
         GetPlayerInfo(), packet.NoteeName, packet.Note);

    if (normalizePlayerName(packet.NoteeName))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleSetMemberNote(this, packet.NoteeName, packet.Note, true);
}

/**
 * @brief 处理设置公会等级权限请求
 *
 * 职责：
 *   允许公会会长或有权限的成员设置某个公会等级的权限和限制
 *   包括银行标签页访问权限、金币提取限制、物品提取限制等
 *
 * 参数：
 *   @param packet 包含等级ID、等级名称、权限标志、金币限制、标签页权限的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetRankInfo() 更新等级权限
 *
 * 主要流程：
 *   1. 获取玩家所在的公会,如果不存在则直接返回
 *   2. 构建每个银行标签页的权限和提取槽位限制数组
 *   3. 遍历所有银行标签页(GUILD_BANK_MAX_TABS),设置每个标签页的权限
 *   4. 记录调试日志(包含等级名称和ID)
 *   5. 调用公会设置等级信息方法更新权限设置
 */
void WorldSession::HandleGuildSetRankPermissions(WorldPackets::Guild::GuildSetRankPermissions& packet)
{
    Guild* guild = GetPlayer()->GetGuild();
    if (!guild)
        return;

    std::array<GuildBankRightsAndSlots, GUILD_BANK_MAX_TABS> rightsAndSlots;
    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
        rightsAndSlots[tabId] = GuildBankRightsAndSlots(tabId, uint8(packet.TabFlags[tabId]), uint32(packet.TabWithdrawItemLimit[tabId]));

    TC_LOG_DEBUG("guild", "CMSG_GUILD_RANK [{}]: Rank: {} ({})", GetPlayerInfo(), packet.RankName, packet.RankID);

    guild->HandleSetRankInfo(this, packet.RankID, packet.RankName, packet.Flags, packet.WithdrawGoldLimit, rightsAndSlots);
}

/**
 * @brief 处理添加新公会等级请求
 *
 * 职责：
 *   允许公会会长在公会等级体系中添加新的等级
 *   通常在默认等级不够时添加自定义等级
 *
 * 参数：
 *   @param packet 包含新等级名称的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleAddNewRank() 添加等级或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志(包含新等级名称)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会添加新等级方法处理添加逻辑
 */
void WorldSession::HandleGuildAddRankOpcode(WorldPackets::Guild::GuildAddRank& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_ADD_RANK [{}]: Rank: {}", GetPlayerInfo(), packet.Name);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleAddNewRank(this, packet.Name);
}

/**
 * @brief 处理删除公会等级请求
 *
 * 职责：
 *   允许公会会长删除公会中最低的等级
 *   只能删除最低等级,且该等级下不能有成员
 *
 * 参数：
 *   @param packet 删除等级数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->HandleRemoveLowestRank() 删除等级或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会移除最低等级方法处理删除逻辑
 */
void WorldSession::HandleGuildDeleteRank(WorldPackets::Guild::GuildDeleteRank& /*packet*/)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_DEL_RANK [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleRemoveLowestRank(this);
}

/**
 * @brief 处理更新公会信息文本请求
 *
 * 职责：
 *   允许有权限的公会成员更新公会的描述信息文本
 *   该信息显示在公会信息界面中
 *
 * 参数：
 *   @param packet 包含新的信息文本的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetInfo() 更新信息文本或返回错误
 *
 * 主要流程：
 *   1. 记录调试日志(包含新的信息文本内容)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,调用公会设置信息方法更新文本
 */
void WorldSession::HandleGuildUpdateInfoText(WorldPackets::Guild::GuildUpdateInfoText& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_INFO_TEXT [{}]: {}", GetPlayerInfo(), packet.InfoText);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->HandleSetInfo(this, packet.InfoText);
}

/**
 * @brief 处理保存公会徽章请求
 *
 * 职责：
 *   允许公会会长在公会注册员NPC处设计和保存公会徽章
 *   徽章包括样式、颜色、边框等外观设置
 *
 * 参数：
 *   @param packet 包含NPC GUID和徽章设计信息的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetEmblem() 保存徽章或发送错误结果
 *
 * 主要流程：
 *   1. 从数据包中读取徽章信息(样式、颜色、边框等)
 *   2. 记录调试日志(包含所有徽章参数)
 *   3. 验证玩家是否可以与公会注册员NPC交互
 *   4. 如果玩家处于假死状态,移除假死光环
 *   5. 如果玩家在公会中,调用公会设置徽章方法保存设计
 *   6. 如果玩家不在公会中,发送"未加入公会"错误
 *   7. 如果NPC无效,发送"无效的徽章商人"错误
 */
void WorldSession::HandleSaveGuildEmblemOpcode(WorldPackets::Guild::SaveGuildEmblem& packet)
{
    EmblemInfo emblemInfo;
    emblemInfo.ReadPacket(packet);

    TC_LOG_DEBUG("guild", "MSG_SAVE_GUILD_EMBLEM [{}]: Guid: [{}] Style: {}, Color: {}, BorderStyle: {}, BorderColor: {}, BackgroundColor: {}"
        , GetPlayerInfo(), packet.Vendor.ToString(), emblemInfo.GetStyle()
        , emblemInfo.GetColor(), emblemInfo.GetBorderStyle()
        , emblemInfo.GetBorderColor(), emblemInfo.GetBackgroundColor());

    if (GetPlayer()->GetNPCIfCanInteractWith(packet.Vendor, UNIT_NPC_FLAG_TABARDDESIGNER))
    {
        // Remove fake death
        // 移除假死状态
        if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
            GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleSetEmblem(this, emblemInfo);
        else
            Guild::SendSaveEmblemResult(this, ERR_GUILDEMBLEM_NOGUILD); // "You are not part of a guild!";
    }
    else
        Guild::SendSaveEmblemResult(this, ERR_GUILDEMBLEM_INVALIDVENDOR); // "That's not an emblem vendor!"
}

/**
 * @brief 处理公会事件日志查询请求
 *
 * 职责：
 *   向客户端发送公会事件日志,记录公会的重要事件
 *   如成员加入、离开、晋升、降级等操作历史
 *
 * 参数：
 *   @param packet 查询数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->SendEventLog() 发送事件日志
 *
 * 主要流程：
 *   1. 记录调试日志
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,发送公会事件日志给客户端
 */
void WorldSession::HandleGuildEventLogQueryOpcode(WorldPackets::Guild::GuildEventLogQuery& /*packet*/)
{
    TC_LOG_DEBUG("guild", "MSG_GUILD_EVENT_LOG_QUERY [{}]", GetPlayerInfo());

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendEventLog(this);
}

/**
 * @brief 处理公会银行金币提取限额查询请求
 *
 * 职责：
 *   向客户端发送玩家当日可提取的金币数量信息
 *   根据玩家等级权限和当日已提取数量计算剩余额度
 *
 * 参数：
 *   @param packet 查询数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->SendMoneyInfo() 发送金币提取信息
 *
 * 主要流程：
 *   1. 获取玩家所在的公会
 *   2. 如果公会存在,发送金币提取限额信息给客户端
 */
void WorldSession::HandleGuildBankMoneyWithdrawn(WorldPackets::Guild::GuildBankRemainingWithdrawMoneyQuery& /*packet*/)
{
    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendMoneyInfo(this);
}

/**
 * @brief 处理公会权限查询请求
 *
 * 职责：
 *   向客户端发送玩家在公会中的权限信息
 *   包括银行访问权限、官员权限等
 *
 * 参数：
 *   @param packet 查询数据包(未使用)
 *
 * 返回值：
 *   void - 通过 guild->SendPermissions() 发送权限信息
 *
 * 主要流程：
 *   1. 获取玩家所在的公会
 *   2. 如果公会存在,发送权限信息给客户端
 */
void WorldSession::HandleGuildPermissionsQuery(WorldPackets::Guild::GuildPermissionsQuery& /* packet */)
{
    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendPermissions(this);
}

/**
 * @brief 处理激活公会银行请求(点击公会银行游戏对象)
 *
 * 职责：
 *   当玩家点击公会银行游戏对象时,发送银行标签页信息
 *   这是打开公会银行界面的第一步操作
 *
 * 参数：
 *   @param packet 包含银行游戏对象GUID和是否完整更新的数据包
 *
 * 返回值：
 *   void - 通过 guild->SendBankTabsInfo() 发送银行信息或错误消息
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID和是否完整更新标志)
 *   2. 验证玩家是否可以与公会银行游戏对象交互
 *   3. 获取玩家所在的公会
 *   4. 如果玩家不在公会中,发送"未加入公会"错误
 *   5. 如果公会存在,发送银行标签页信息给客户端
 */
// Called when clicking on Guild bank gameobject
// 当点击公会银行游戏对象时调用
void WorldSession::HandleGuildBankActivate(WorldPackets::Guild::GuildBankActivate& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANKER_ACTIVATE [{}]: [{}] AllSlots: {}"
        , GetPlayerInfo(), packet.Banker.ToString(), packet.FullUpdate);

    GameObject const* const go = GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK);
    if (!go)
        return;

    Guild* const guild = GetPlayer()->GetGuild();
    if (!guild)
    {
        Guild::SendCommandResult(this, GUILD_COMMAND_VIEW_TAB, ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    guild->SendBankTabsInfo(this, packet.FullUpdate);
}

/**
 * @brief 处理查询公会银行标签页请求(仅第一个标签页)
 *
 * 职责：
 *   当玩家打开公会银行标签页时,发送该标签页的详细内容
 *   包括标签页中的物品列表和数量
 *
 * 参数：
 *   @param packet 包含银行GUID、标签页ID和是否完整更新的数据包
 *
 * 返回值：
 *   void - 通过 guild->SendBankTabData() 发送标签页数据
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID、标签页ID和更新标志)
 *   2. 验证玩家是否可以与公会银行游戏对象交互
 *   3. 获取玩家所在的公会
 *   4. 如果公会存在,发送指定标签页的数据给客户端
 *
 * 注意事项：
 *   - HACK: 客户端在本次会话中如果已经收到SMSG_GUILD_BANK_LIST,
 *     则不会查询完整的标签页内容
 *   - 但服务器在任何人修改银行时会向整个公会广播更新,
 *     导致客户端标签页内容只包含那次变更的数据,初始化不完整
 *   - 因此强制使用true作为FullUpdate参数
 */
// Called when opening guild bank tab only (first one)
// 仅在打开公会银行标签页时调用(第一个标签页)
void WorldSession::HandleGuildBankQueryTab(WorldPackets::Guild::GuildBankQueryTab& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANK_QUERY_TAB [{}]: {}, TabId: {}, ShowTabs: {}"
        , GetPlayerInfo(), packet.Banker.ToString(), packet.Tab, packet.FullUpdate);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->SendBankTabData(this, packet.Tab, true /*packet.FullUpdate*/);
                                                          // HACK: client doesn't query entire tab content if it had received SMSG_GUILD_BANK_LIST in this session
                                                          // but we broadcast bank updates to entire guild when *ANYONE* changes anything, incorrectly initializing clients
                                                          // tab content with only data for that change
                                                          // HACK: 客户端在本次会话中如果已经收到SMSG_GUILD_BANK_LIST,则不会查询完整的标签页内容
                                                          // 但服务器在任何人修改银行时会向整个公会广播更新,导致客户端标签页内容只包含那次变更的数据,初始化不完整
}

/**
 * @brief 处理公会银行存入金币请求
 *
 * 职责：
 *   允许公会成员将金币存入公会银行
 *   验证玩家是否有足够的金币
 *
 * 参数：
 *   @param packet 包含银行GUID和存入金币数量的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleMemberDepositMoney() 执行存入操作
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID和金币数量)
 *   2. 验证玩家是否可以与公会银行游戏对象交互
 *   3. 检查金币数量是否大于0且玩家有足够金币
 *   4. 获取玩家所在的公会
 *   5. 如果公会存在,执行存入金币操作
 */
void WorldSession::HandleGuildBankDepositMoney(WorldPackets::Guild::GuildBankDepositMoney& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANK_DEPOSIT_MONEY [{}]: [{}], money: {}",
        GetPlayerInfo(), packet.Banker.ToString(), packet.Money);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
        if (packet.Money && GetPlayer()->HasEnoughMoney(packet.Money))
            if (Guild* guild = GetPlayer()->GetGuild())
                guild->HandleMemberDepositMoney(this, packet.Money);
}

/**
 * @brief 处理公会银行提取金币请求
 *
 * 职责：
 *   允许有权限的公会成员从公会银行提取金币
 *   验证玩家权限和当日提取限额
 *
 * 参数：
 *   @param packet 包含银行GUID和提取金币数量的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleMemberWithdrawMoney() 执行提取操作
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID和金币数量)
 *   2. 验证金币数量大于0且玩家可以与公会银行游戏对象交互
 *   3. 获取玩家所在的公会
 *   4. 如果公会存在,执行提取金币操作(会验证权限和限额)
 */
void WorldSession::HandleGuildBankWithdrawMoney(WorldPackets::Guild::GuildBankWithdrawMoney& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANK_WITHDRAW_MONEY [{}]: [{}], money: {}",
        GetPlayerInfo(), packet.Banker.ToString(), packet.Money);

    if (packet.Money && GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleMemberWithdrawMoney(this, packet.Money);
}

/**
 * @brief 处理公会银行物品交换请求
 *
 * 职责：
 *   处理玩家与公会银行之间的物品交换操作
 *   包括:银行内物品移动、玩家背包与银行之间的物品转移
 *
 * 参数：
 *   @param packet 包含银行GUID、源位置、目标位置、物品数量等的数据包
 *
 * 返回值：
 *   void - 通过 guild->SwapItems() 或 guild->SwapItemsWithInventory() 执行交换
 *
 * 主要流程：
 *   1. 验证玩家是否可以与公会银行游戏对象交互
 *   2. 获取玩家所在的公会
 *   3. 判断操作类型:
 *      a. 如果是BankOnly=true,执行银行内部物品移动
 *      b. 否则,执行玩家背包与银行之间的物品交换
 *   4. 对于玩家与银行的交换:
 *      - 解析玩家背包位置和槽位信息
 *      - 检查是否为自动存储模式
 *      - 验证物品位置是否在背包范围内
 *      - 执行物品交换操作
 *
 * 注意事项：
 *   - 只允许操作背包中的物品,不允许直接装备银行物品
 *   - 支持物品堆叠分割功能
 */
void WorldSession::HandleGuildBankSwapItems(WorldPackets::Guild::GuildBankSwapItems& packet)
{
    if (!GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    Guild* guild = GetPlayer()->GetGuild();
    if (!guild)
        return;

    if (packet.BankOnly)
        guild->SwapItems(GetPlayer(), packet.BankTab1, packet.BankSlot1, packet.BankTab, packet.BankSlot, packet.BankItemCount);
    else
    {
        uint8 playerBag = NULL_BAG;
        uint8 playerSlotId = NULL_SLOT;
        uint8 toChar = 1;
        uint32 splitedAmount = 0;

        if (!packet.AutoStore)
        {
            playerBag = packet.ContainerSlot;
            playerSlotId = packet.ContainerItemSlot;
            toChar = packet.ToSlot;
            splitedAmount = packet.StackCount;
        }

        // Player <-> Bank
        // Allow to work with inventory only
        // 玩家 <-> 银行
        // 只允许操作背包中的物品
        if (!Player::IsInventoryPos(playerBag, playerSlotId) && !(playerBag == NULL_BAG && playerSlotId == NULL_SLOT))
            GetPlayer()->SendEquipError(EQUIP_ERR_NONE, nullptr);
        else
            guild->SwapItemsWithInventory(GetPlayer(), toChar != 0, packet.BankTab, packet.BankSlot, playerBag, playerSlotId, splitedAmount);
    }
}

/**
 * @brief 处理购买公会银行标签页请求
 *
 * 职责：
 *   允许公会会长购买新的公会银行标签页
 *   需要消耗金币,价格随已购买标签页数量递增
 *
 * 参数：
 *   @param packet 包含银行GUID和要购买的标签页ID的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleBuyBankTab() 执行购买操作
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID和标签页ID)
 *   2. 验证玩家是否可以与公会银行游戏对象交互
 *   3. 获取玩家所在的公会
 *   4. 如果公会存在,执行购买银行标签页操作
 */
void WorldSession::HandleGuildBankBuyTab(WorldPackets::Guild::GuildBankBuyTab& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANK_BUY_TAB [{}]: [{}[, TabId: {}", GetPlayerInfo(), packet.Banker .ToString(), packet.BankTab);

    if (GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
        if (Guild* guild = GetPlayer()->GetGuild())
            guild->HandleBuyBankTab(this, packet.BankTab);
}

/**
 * @brief 处理更新公会银行标签页信息请求
 *
 * 职责：
 *   允许有权限的公会成员修改银行标签页的名称和图标
 *   用于个性化设置银行标签页
 *
 * 参数：
 *   @param packet 包含银行GUID、标签页ID、新名称和图标路径的数据包
 *
 * 返回值：
 *   void - 通过 guild->HandleSetBankTabInfo() 更新标签页信息
 *
 * 主要流程：
 *   1. 记录调试日志(包含银行GUID、标签页ID、名称和图标)
 *   2. 验证名称和图标不为空
 *   3. 验证玩家是否可以与公会银行游戏对象交互
 *   4. 获取玩家所在的公会
 *   5. 如果公会存在,更新银行标签页的名称和图标
 */
void WorldSession::HandleGuildBankUpdateTab(WorldPackets::Guild::GuildBankUpdateTab& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_GUILD_BANK_UPDATE_TAB [{}]: [{}], TabId: {}, Name: {}, Icon: {}"
        , GetPlayerInfo(), packet.Banker.ToString(), packet.BankTab, packet.Name, packet.Icon);

    if (!packet.Name.empty() && !packet.Icon.empty())
        if (GetPlayer()->GetGameObjectIfCanInteractWith(packet.Banker, GAMEOBJECT_TYPE_GUILD_BANK))
            if (Guild* guild = GetPlayer()->GetGuild())
                guild->HandleSetBankTabInfo(this, packet.BankTab, packet.Name, packet.Icon);
}

/**
 * @brief 处理公会银行日志查询请求
 *
 * 职责：
 *   向客户端发送指定银行标签页的操作日志
 *   记录该标签页的存取物品、存取金币等历史操作
 *
 * 参数：
 *   @param packet 包含要查询的标签页ID的数据包
 *
 * 返回值：
 *   void - 通过 guild->SendBankLog() 发送银行日志
 *
 * 主要流程：
 *   1. 记录调试日志(包含标签页ID)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,发送指定标签页的操作日志给客户端
 */
void WorldSession::HandleGuildBankLogQuery(WorldPackets::Guild::GuildBankLogQuery& packet)
{
    TC_LOG_DEBUG("guild", "MSG_GUILD_BANK_LOG_QUERY [{}]: TabId: {}", GetPlayerInfo(), packet.Tab);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendBankLog(this, packet.Tab);
}

/**
 * @brief 处理公会银行标签页文本查询请求
 *
 * 职责：
 *   向客户端发送指定银行标签页的说明文本
 *   用于显示标签页的描述或备注信息
 *
 * 参数：
 *   @param packet 包含要查询的标签页ID的数据包
 *
 * 返回值：
 *   void - 通过 guild->SendBankTabText() 发送标签页文本
 *
 * 主要流程：
 *   1. 记录调试日志(包含标签页ID)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,发送指定标签页的说明文本给客户端
 */
void WorldSession::HandleGuildBankTextQuery(WorldPackets::Guild::GuildBankTextQuery& packet)
{
    TC_LOG_DEBUG("guild", "MSG_QUERY_GUILD_BANK_TEXT [{}]: TabId: {}", GetPlayerInfo(), packet.Tab);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SendBankTabText(this, packet.Tab);
}

/**
 * @brief 处理设置公会银行标签页文本请求
 *
 * 职责：
 *   允许有权限的公会成员为银行标签页设置说明文本
 *   用于标记标签页的用途或存放物品类型等信息
 *
 * 参数：
 *   @param packet 包含标签页ID和文本内容的数据包
 *
 * 返回值：
 *   void - 通过 guild->SetBankTabText() 更新标签页文本
 *
 * 主要流程：
 *   1. 记录调试日志(包含标签页ID和文本内容)
 *   2. 获取玩家所在的公会
 *   3. 如果公会存在,设置指定标签页的说明文本
 */
void WorldSession::HandleGuildBankSetTabText(WorldPackets::Guild::GuildBankSetTabText& packet)
{
    TC_LOG_DEBUG("guild", "CMSG_SET_GUILD_BANK_TEXT [{}]: TabId: {}, Text: {}", GetPlayerInfo(), packet.Tab, packet.TabText);

    if (Guild* guild = GetPlayer()->GetGuild())
        guild->SetBankTabText(packet.Tab, packet.TabText);
}
