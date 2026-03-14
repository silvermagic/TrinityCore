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
 * @file Channel.cpp
 * @brief 聊天频道管理模块实现文件
 *
 * 本文件实现了 Channel 类的所有功能，包括：
 * - 频道的创建和初始化（内置频道和自定义频道）
 * - 玩家加入/离开频道的处理
 * - 频道成员管理（踢出、封禁、解封）
 * - 频道权限管理（管理员、禁言）
 * - 频道属性设置（密码、所有者、公告）
 * - 频道消息广播
 * - 频道数据持久化
 *
 * 设计要点：
 * - 内置频道由系统管理，不保存到数据库
 * - 自定义频道支持持久化，包括设置和封禁列表
 * - 支持 GM 隐身加入频道
 * - 支持跨阵营频道交互（可配置）
 */

#include "Channel.h"
#include "AccountMgr.h"
#include "ChannelAppenders.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SocialMgr.h"
#include "StringConvert.h"
#include "World.h"

/**
 * @brief 内置频道构造函数实现
 *
 * 初始化内置频道（系统频道），这些频道由 DBC 定义。
 * 内置频道的特点：
 * - 不保存到数据库
 * - 不启用公告和所有权管理
 * - 根据频道类型设置相应的标志
 */
Channel::Channel(uint32 channelId, uint32 team /*= 0*/, AreaTableEntry const* zoneEntry /*= nullptr*/) :
    _isDirty(false),
    _nextActivityUpdateTime(0),
    _announceEnabled(false),                                        // 内置频道不启用加入/离开公告
    _ownershipEnabled(false),                                       // 内置频道不启用所有权管理
    _isOwnerInvisible(false),
    _channelFlags(CHANNEL_FLAG_GENERAL),                            // 所有内置频道都设置通用标志
    _channelId(channelId),
    _channelTeam(team),
    _ownerGuid(),
    _channelName(),
    _channelPassword(),
    _zoneEntry(zoneEntry)
{
    // 从 DBC 获取频道配置
    ChatChannelsEntry const* channelEntry = sChatChannelsStore.AssertEntry(channelId);

    // 根据频道类型设置相应的标志
    if (channelEntry->Flags & CHANNEL_DBC_FLAG_TRADE)              // 交易频道
        _channelFlags |= CHANNEL_FLAG_TRADE;

    if (channelEntry->Flags & CHANNEL_DBC_FLAG_CITY_ONLY2)         // 仅城市频道
        _channelFlags |= CHANNEL_FLAG_CITY;

    if (channelEntry->Flags & CHANNEL_DBC_FLAG_LFG)                // LFG 频道
        _channelFlags |= CHANNEL_FLAG_LFG;
    else                                                            // 其他所有频道
        _channelFlags |= CHANNEL_FLAG_NOT_LFG;
}

/**
 * @brief 自定义频道构造函数实现
 *
 * 初始化玩家创建的自定义频道。
 * 自定义频道的特点：
 * - 启用公告和所有权管理
 * - 可以设置密码
 * - 数据持久化到数据库
 * - 支持封禁列表
 */
Channel::Channel(std::string const& name, uint32 team /*= 0*/, std::string const& banList) :
    _isDirty(false),
    _nextActivityUpdateTime(0),
    _announceEnabled(true),                                         // 自定义频道默认启用公告
    _ownershipEnabled(true),                                        // 自定义频道默认启用所有权管理
    _isOwnerInvisible(false),
    _channelFlags(CHANNEL_FLAG_CUSTOM),                             // 标记为自定义频道
    _channelId(0),                                                  // 自定义频道 ID 为 0
    _channelTeam(team),
    _ownerGuid(),
    _channelName(name),
    _channelPassword(),
    _zoneEntry(nullptr)
{
    // 解析封禁列表（空格分隔的 GUID 字符串）
    for (std::string_view guid : Trinity::Tokenize(banList, ' ', false))
    {
        ObjectGuid banned(Trinity::StringTo<uint64>(guid).value_or(0));
        if (!banned)
            continue;

        TC_LOG_DEBUG("chat.system", "Channel({}) loaded player {} into bannedStore", name, banned.ToString());
        _bannedStore.insert(banned);
    }
}

/**
 * @brief 获取频道名称（静态方法）
 *
 * 根据频道类型和区域生成本地化的频道名称。
 * 对于区域依赖频道，名称会包含区域名称（如"综合 - 暴风城"）。
 *
 * @param channelName 输出参数，返回生成的频道名称
 * @param channelId 频道 ID
 * @param locale 语言区域设置
 * @param zoneEntry 区域信息
 */
void Channel::GetChannelName(std::string& channelName, uint32 channelId, LocaleConstant locale, AreaTableEntry const* zoneEntry)
{
    if (channelId)
    {
        ChatChannelsEntry const* channelEntry = sChatChannelsStore.AssertEntry(channelId);

        // 全局频道直接使用 DBC 中的名称
        if (!(channelEntry->Flags & CHANNEL_DBC_FLAG_GLOBAL))
        {
            // 仅城市频道使用城市名称
            if (channelEntry->Flags & CHANNEL_DBC_FLAG_CITY_ONLY)
                channelName = fmt::sprintf(channelEntry->Name[locale], sObjectMgr->GetTrinityString(LANG_CHANNEL_CITY, locale));
            else
                // 区域依赖频道使用区域名称
                channelName = fmt::sprintf(channelEntry->Name[locale], ASSERT_NOTNULL(zoneEntry)->AreaName[locale]);
        }
        else
            channelName = channelEntry->Name[locale];
    }
}

/**
 * @brief 获取频道名称（成员方法）
 *
 * 返回当前频道的本地化名称。
 *
 * @param locale 语言区域设置，默认为 DEFAULT_LOCALE
 * @return 频道名称字符串
 */
std::string Channel::GetName(LocaleConstant locale /*= DEFAULT_LOCALE*/) const
{
    std::string result = _channelName;
    Channel::GetChannelName(result, _channelId, locale, _zoneEntry);

    return result;
}

/**
 * @brief 更新频道数据到数据库
 *
 * 定期调用以保存自定义频道的设置和更新活动时间。
 * 分两种情况：
 * 1. 频道被标记为脏数据：保存完整设置（密码、公告、封禁列表等）
 * 2. 频道未被标记为脏数据：只更新活动时间戳（如果频道有成员）
 *
 * 使用随机延迟更新策略，避免数据库写入压力。
 */
void Channel::UpdateChannelInDB()
{
    time_t const now = GameTime::GetGameTime();

    // 如果频道有变更需要保存
    if (_isDirty)
    {
        // 构建封禁列表字符串（空格分隔的 GUID）
        std::ostringstream banlist;
        for (BannedContainer::const_iterator iter = _bannedStore.begin(); iter != _bannedStore.end(); ++iter)
            banlist << iter->GetRawValue() << ' ';

        std::string banListStr = banlist.str();

        // 更新频道完整设置
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL);
        stmt->setString(0, _channelName);
        stmt->setUInt32(1, _channelTeam);
        stmt->setBool(2, _announceEnabled);
        stmt->setBool(3, _ownershipEnabled);
        stmt->setString(4, _channelPassword);
        stmt->setString(5, banListStr);
        CharacterDatabase.Execute(stmt);
    }
    // 如果到达更新时间且频道有成员，更新活动时间戳
    else if (_nextActivityUpdateTime <= now)
    {
        if (!_playersStore.empty())
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL_USAGE);
            stmt->setString(0, _channelName);
            stmt->setUInt32(1, _channelTeam);
            CharacterDatabase.Execute(stmt);
        }
    }
    else
        return;

    // 重置脏标志和下次更新时间
    _isDirty = false;
    // 随机 1-6 分钟，乘以配置的保存间隔
    _nextActivityUpdateTime = now + urand(1 * MINUTE, 6 * MINUTE) * std::max(1u, sWorld->getIntConfig(CONFIG_PRESERVE_CUSTOM_CHANNEL_INTERVAL));
}

/**
 * @brief 玩家加入频道
 *
 * 处理玩家加入频道的完整流程：
 * 1. 检查是否已在频道中
 * 2. 检查是否被封禁
 * 3. 验证密码
 * 4. 检查 LFG 频道限制
 * 5. 添加到成员列表并设置权限
 * 6. 发送加入通知
 * 7. 处理所有权转移（自定义频道）
 *
 * @param player 加入的玩家指针
 * @param pass 频道密码，默认为空
 */
void Channel::JoinChannel(Player* player, std::string const& pass)
{
    ObjectGuid guid = player->GetGUID();

    // 检查玩家是否已在频道中
    if (IsOn(guid))
    {
        // 仅对自定义频道发送错误消息，内置频道不发送
        if (!IsConstant())
        {
            PlayerAlreadyMemberAppend appender(guid);
            ChannelNameBuilder<PlayerAlreadyMemberAppend> builder(this, appender);
            SendToOne(builder, guid);
        }
        return;
    }

    // 检查玩家是否被封禁
    if (IsBanned(guid))
    {
        BannedAppend appender;
        ChannelNameBuilder<BannedAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 验证密码
    if (!CheckPassword(pass))
    {
        WrongPasswordAppend appender;
        ChannelNameBuilder<WrongPasswordAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查 LFG 频道限制
    // 如果配置了限制，玩家必须不在队伍中才能加入
    if (HasFlag(CHANNEL_FLAG_LFG) &&
        sWorld->getBoolConfig(CONFIG_RESTRICTED_LFG_CHANNEL) &&
        AccountMgr::IsPlayerAccount(player->GetSession()->GetSecurity()) && //FIXME: Move to RBAC
        player->GetGroup())
    {
        NotInLFGAppend appender;
        ChannelNameBuilder<NotInLFGAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 记录玩家已加入频道
    player->JoinedChannel(this);

    // 如果启用了公告且玩家没有静默加入权限，广播加入消息
    if (_announceEnabled && !player->GetSession()->HasPermission(rbac::RBAC_PERM_SILENTLY_JOIN_CHANNEL))
    {
        JoinedAppend appender(guid);
        ChannelNameBuilder<JoinedAppend> builder(this, appender);
        SendToAll(builder);
    }

    // 检查是否是新频道（第一个加入的玩家）
    bool newChannel = _playersStore.empty();
    if (newChannel)
        _nextActivityUpdateTime = 0; // 强制在下次频道 tick 时更新活动状态

    // 添加玩家到成员列表
    PlayerInfo& pinfo = _playersStore[guid];
    pinfo.flags = MEMBER_FLAG_NONE;
    pinfo.invisible = !player->isGMVisible();  // 记录玩家的 GM 隐身状态

    // 发送"你已加入频道"消息
    YouJoinedAppend appender(this);
    ChannelNameBuilder<YouJoinedAppend> builder(this, appender);
    SendToOne(builder, guid);

    // 通知其他成员有新玩家加入
    JoinNotify(guid);

    // 自定义频道的所有权处理
    if (!IsConstant())
    {
        // 如果频道还没有所有者，或者所有者是隐身 GM，且当前玩家不是隐身 GM（除非频道是空的）
        // 则将所有权转移给当前玩家
        if (_ownershipEnabled && (newChannel || !pinfo.IsInvisible()) && (!_ownerGuid || _isOwnerInvisible))
        {
            _isOwnerInvisible = pinfo.IsInvisible();

            SetOwner(guid, !newChannel && !_isOwnerInvisible);
            pinfo.SetModerator(true);
        }
    }
}

/**
 * @brief 玩家离开频道
 *
 * 处理玩家离开频道的完整流程：
 * 1. 检查玩家是否在频道中
 * 2. 发送离开消息
 * 3. 从成员列表移除
 * 4. 广播离开通知
 * 5. 处理所有权转移（如果所有者离开）
 *
 * @param player 离开的玩家指针
 * @param send 是否发送离开通知，默认为 true
 */
void Channel::LeaveChannel(Player* player, bool send)
{
    ObjectGuid guid = player->GetGUID();

    // 检查玩家是否在频道中
    if (!IsOn(guid))
    {
        if (send)
        {
            NotMemberAppend appender;
            ChannelNameBuilder<NotMemberAppend> builder(this, appender);
            SendToOne(builder, guid);
        }
        return;
    }

    // 发送"你已离开频道"消息
    if (send)
    {
        YouLeftAppend appender(this);
        ChannelNameBuilder<YouLeftAppend> builder(this, appender);
        SendToOne(builder, guid);

        player->LeftChannel(this);
    }

    // 记录玩家是否为所有者，并从成员列表移除
    PlayerInfo& info = _playersStore.at(guid);
    bool changeowner = info.IsOwner();
    _playersStore.erase(guid);

    // 如果启用了公告且玩家没有静默离开权限，广播离开消息
    if (_announceEnabled && !player->GetSession()->HasPermission(rbac::RBAC_PERM_SILENTLY_JOIN_CHANNEL))
    {
        LeftAppend appender(guid);
        ChannelNameBuilder<LeftAppend> builder(this, appender);
        SendToAll(builder);
    }

    // 通知其他成员有玩家离开
    LeaveNotify(guid);

    // 自定义频道的所有权转移处理
    if (!IsConstant())
    {
        // 如果所有者离开且频道还有成员，选择新的所有者
        // 优先选择可见的玩家，如果都是隐身 GM 则选择第一个
        if (changeowner && _ownershipEnabled && !_playersStore.empty())
        {
            PlayerContainer::iterator itr;
            for (itr = _playersStore.begin(); itr != _playersStore.end(); ++itr)
            {
                if (!itr->second.IsInvisible())
                    break;
            }

            // 如果没有可见玩家，选择第一个成员（可能是隐身 GM）
            if (itr == _playersStore.end())
                itr = _playersStore.begin();

            ObjectGuid newOwner = itr->first;
            itr->second.SetModerator(true);

            SetOwner(newOwner);

            // 如果新所有者是隐身 GM，设置标志以便在有可见玩家加入时自动转移所有权
            if (itr->second.IsInvisible())
                _isOwnerInvisible = true;
        }
    }
}

/**
 * @brief 踢出或封禁玩家
 *
 * 处理踢出和封禁玩家的逻辑。封禁会将玩家加入封禁列表并踢出。
 * 需要：
 * - 操作者必须是频道成员
 * - 操作者必须是管理员或有特殊权限
 * - 不能踢出/封禁所有者（除非操作者就是所有者）
 *
 * @param player 执行操作的玩家
 * @param badname 目标玩家名称
 * @param ban true 为封禁，false 为仅踢出
 */
void Channel::KickOrBan(Player const* player, std::string const& badname, bool ban)
{
    ObjectGuid good = player->GetGUID();

    // 检查操作者是否在频道中
    if (!IsOn(good))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 检查操作者是否有权限（管理员或特殊权限）
    PlayerInfo& info = _playersStore.at(good);
    if (!info.IsModerator() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR))
    {
        NotModeratorAppend appender;
        ChannelNameBuilder<NotModeratorAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 查找目标玩家
    Player* bad = ObjectAccessor::FindConnectedPlayerByName(badname);
    ObjectGuid victim = bad ? bad->GetGUID() : ObjectGuid::Empty;

    // 检查目标是否存在并在频道中
    if (!bad || !victim || !IsOn(victim))
    {
        PlayerNotFoundAppend appender(badname);
        ChannelNameBuilder<PlayerNotFoundAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 检查目标是否为所有者
    bool changeowner = _ownerGuid == victim;

    // 不能踢出所有者（除非操作者自己就是所有者）
    if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR) && changeowner && good != _ownerGuid)
    {
        NotOwnerAppend appender;
        ChannelNameBuilder<NotOwnerAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 如果是封禁操作，将目标加入封禁列表
    if (ban && !IsBanned(victim))
    {
        _bannedStore.insert(victim);
        _isDirty = true;  // 标记需要保存到数据库

        // 广播封禁消息（除非有静默权限）
        if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_SILENTLY_JOIN_CHANNEL))
        {
            PlayerBannedAppend appender(good, victim);
            ChannelNameBuilder<PlayerBannedAppend> builder(this, appender);
            SendToAll(builder);
        }
    }
    // 否则只广播踢出消息
    else if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_SILENTLY_JOIN_CHANNEL))
    {
        PlayerKickedAppend appender(good, victim);
        ChannelNameBuilder<PlayerKickedAppend> builder(this, appender);
        SendToAll(builder);
    }

    // 从成员列表移除目标
    _playersStore.erase(victim);
    bad->LeftChannel(this);

    // 如果目标是所有者，转移所有权给操作者
    if (changeowner && _ownershipEnabled && !_playersStore.empty())
    {
        ObjectGuid newowner = good;
        info.SetModerator(true);
        SetOwner(newowner);
    }
}

/**
 * @brief 解除封禁
 *
 * 将玩家从频道封禁列表中移除。
 * 需要：
 * - 操作者必须是频道成员
 * - 操作者必须是管理员或有特殊权限
 *
 * @param player 执行操作的玩家
 * @param badname 要解封的玩家名称
 */
void Channel::UnBan(Player const* player, std::string const& badname)
{
    ObjectGuid good = player->GetGUID();

    // 检查操作者是否在频道中
    if (!IsOn(good))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 检查操作者是否有权限
    PlayerInfo& info = _playersStore.at(good);
    if (!info.IsModerator() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR))
    {
        NotModeratorAppend appender;
        ChannelNameBuilder<NotModeratorAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 查找要解封的玩家
    Player* bad = ObjectAccessor::FindConnectedPlayerByName(badname);
    ObjectGuid victim = bad ? bad->GetGUID() : ObjectGuid::Empty;

    // 检查玩家是否在封禁列表中
    if (!victim || !IsBanned(victim))
    {
        PlayerNotFoundAppend appender(badname);
        ChannelNameBuilder<PlayerNotFoundAppend> builder(this, appender);
        SendToOne(builder, good);
        return;
    }

    // 从封禁列表移除
    _bannedStore.erase(victim);

    // 广播解封消息
    PlayerUnbannedAppend appender(good, victim);
    ChannelNameBuilder<PlayerUnbannedAppend> builder(this, appender);
    SendToAll(builder);

    // 标记需要保存到数据库
    _isDirty = true;
}

/**
 * @brief 设置频道密码
 *
 * 修改频道的访问密码。需要管理员权限。
 *
 * @param player 执行操作的玩家
 * @param pass 新密码
 */
void Channel::Password(Player const* player, std::string const& pass)
{
    ObjectGuid guid = player->GetGUID();

    ChatHandler chat(player->GetSession());

    // 检查操作者是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查操作者是否有权限
    PlayerInfo& info = _playersStore.at(guid);
    if (!info.IsModerator() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR))
    {
        NotModeratorAppend appender;
        ChannelNameBuilder<NotModeratorAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 设置新密码
    _channelPassword = pass;

    // 广播密码变更消息
    PasswordChangedAppend appender(guid);
    ChannelNameBuilder<PasswordChangedAppend> builder(this, appender);
    SendToAll(builder);

    // 标记需要保存到数据库
    _isDirty = true;
}

/**
 * @brief 设置玩家权限模式
 *
 * 设置或取消玩家的管理员或禁言状态。
 * 需要：
 * - 操作者必须是频道成员
 * - 操作者必须是管理员或有特殊权限
 * - 不能修改所有者的权限
 * - 不能给自己设置管理员权限（所有者除外）
 *
 * @param player 执行操作的玩家
 * @param p2n 目标玩家名称
 * @param mod true 为管理员权限，false 为禁言权限
 * @param set true 为设置权限，false 为取消权限
 */
void Channel::SetMode(Player const* player, std::string const& p2n, bool mod, bool set)
{
    ObjectGuid guid = player->GetGUID();

    // 检查操作者是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查操作者是否有权限
    PlayerInfo& info = _playersStore.at(guid);
    if (!info.IsModerator() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR))
    {
        NotModeratorAppend appender;
        ChannelNameBuilder<NotModeratorAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 所有者不能给自己设置管理员权限
    if (guid == _ownerGuid && std::string(p2n) == player->GetName() && mod)
        return;

    // 查找目标玩家
    Player* newp = ObjectAccessor::FindConnectedPlayerByName(p2n);
    ObjectGuid victim = newp ? newp->GetGUID() : ObjectGuid::Empty;

    // 检查目标是否存在并在频道中，以及阵营限制
    if (!newp || !victim || !IsOn(victim) ||
        (player->GetTeam() != newp->GetTeam() &&
        (!player->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL) ||
        !newp->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL))))
    {
        PlayerNotFoundAppend appender(p2n);
        ChannelNameBuilder<PlayerNotFoundAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 不能修改所有者的权限（除非操作者就是所有者）
    if (_ownerGuid == victim && _ownerGuid != guid)
    {
        NotOwnerAppend appender;
        ChannelNameBuilder<NotOwnerAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 根据参数设置相应的权限
    if (mod)
        SetModerator(newp->GetGUID(), set);
    else
        SetMute(newp->GetGUID(), set);
}

/**
 * @brief 设置玩家隐身状态
 *
 * 更新玩家在频道中的隐身状态。
 * 如果玩家是所有者，也会更新所有者隐身标志。
 *
 * @param player 目标玩家
 * @param on true 为隐身，false 为可见
 */
void Channel::SetInvisible(Player const* player, bool on)
{
    auto itr = _playersStore.find(player->GetGUID());
    if (itr == _playersStore.end())
        return;

    itr->second.SetInvisible(on);

    // 如果玩家是所有者，更新所有者隐身标志
    if (_ownerGuid == player->GetGUID())
        _isOwnerInvisible = on;
}

/**
 * @brief 设置管理员权限（内部方法）
 *
 * 内部方法，用于设置玩家的管理员权限并广播变更通知。
 *
 * @param guid 目标玩家 GUID
 * @param set true 设置，false 取消
 */
void Channel::SetModerator(ObjectGuid guid, bool set)
{
    if (!IsOn(guid))
        return;

    PlayerInfo& playerInfo = _playersStore.at(guid);

    // 仅在权限状态发生变化时才处理
    if (playerInfo.IsModerator() != set)
    {
        uint8 oldFlag = GetPlayerFlags(guid);
        playerInfo.SetModerator(set);

        // 广播权限变更通知
        ModeChangeAppend appender(guid, oldFlag, GetPlayerFlags(guid));
        ChannelNameBuilder<ModeChangeAppend> builder(this, appender);
        SendToAll(builder);
    }
}

/**
 * @brief 设置禁言状态（内部方法）
 *
 * 内部方法，用于设置玩家的禁言状态并广播变更通知。
 *
 * @param guid 目标玩家 GUID
 * @param set true 禁言，false 解除
 */
void Channel::SetMute(ObjectGuid guid, bool set)
{
    if (!IsOn(guid))
        return;

    PlayerInfo& playerInfo = _playersStore.at(guid);

    // 仅在禁言状态发生变化时才处理
    if (playerInfo.IsMuted() != set)
    {
        uint8 oldFlag = GetPlayerFlags(guid);
        playerInfo.SetMuted(set);

        // 广播权限变更通知
        ModeChangeAppend appender(guid, oldFlag, GetPlayerFlags(guid));
        ChannelNameBuilder<ModeChangeAppend> builder(this, appender);
        SendToAll(builder);
    }
}

/**
 * @brief 设置频道所有者（玩家命令）
 *
 * 由玩家命令调用，转移频道所有权给指定玩家。
 * 需要：
 * - 操作者必须是当前所有者或有特殊权限
 * - 目标玩家必须在频道中
 *
 * @param player 执行操作的玩家
 * @param newname 新所有者的名称
 */
void Channel::SetOwner(Player const* player, std::string const& newname)
{
    ObjectGuid guid = player->GetGUID();

    // 检查操作者是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查操作者是否为所有者或有特殊权限
    if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR) && guid != _ownerGuid)
    {
        NotOwnerAppend appender;
        ChannelNameBuilder<NotOwnerAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 查找新所有者
    Player* newp = ObjectAccessor::FindConnectedPlayerByName(newname);
    ObjectGuid victim = newp ? newp->GetGUID() : ObjectGuid::Empty;

    // 检查目标是否存在并在频道中，以及阵营限制
    if (!newp || !victim || !IsOn(victim) ||
        (player->GetTeam() != newp->GetTeam() &&
        (!player->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL) ||
        !newp->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL))))
    {
        PlayerNotFoundAppend appender(newname);
        ChannelNameBuilder<PlayerNotFoundAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 设置新所有者
    PlayerInfo& info = _playersStore.at(victim);
    info.SetModerator(true);  // 新所有者自动获得管理员权限
    SetOwner(victim);
}

/**
 * @brief 发送所有者信息查询响应
 *
 * 响应玩家查询频道所有者的请求。
 *
 * @param guid 查询玩家的 GUID
 */
void Channel::SendWhoOwner(ObjectGuid guid)
{
    if (IsOn(guid))
    {
        // 发送所有者信息
        ChannelOwnerAppend appender(this, _ownerGuid);
        ChannelNameBuilder<ChannelOwnerAppend> builder(this, appender);
        SendToOne(builder, guid);
    }
    else
    {
        // 玩家不在频道中
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
    }
}

/**
 * @brief 列出频道成员
 *
 * 向玩家发送频道成员列表，包括成员的 GUID 和权限标志。
 * 成员列表会根据权限等级进行过滤：
 * - GM 可以看到所有成员
 * - 普通玩家只能看到权限等级较低或相等的成员
 *
 * @param player 请求列表的玩家
 */
void Channel::List(Player const* player) const
{
    ObjectGuid guid = player->GetGUID();

    // 检查玩家是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    std::string channelName = GetName(player->GetSession()->GetSessionDbcLocale());
    TC_LOG_DEBUG("chat.system", "SMSG_CHANNEL_LIST {} Channel: {}",
        player->GetSession()->GetPlayerInfo(), channelName);

    // 构建成员列表数据包
    WorldPacket data(SMSG_CHANNEL_LIST, 1 + (channelName.size() + 1) + 1 + 4 + _playersStore.size() * (8 + 1));
    data << uint8(1);                                   // 频道类型
    data << channelName;                                // 频道名称
    data << uint8(GetFlags());                          // 频道标志

    size_t pos = data.wpos();
    data << uint32(0);                                  // 成员数量占位符

    uint32 gmLevelInWhoList = sWorld->getIntConfig(CONFIG_GM_LEVEL_IN_WHO_LIST);

    uint32 count  = 0;
    for (PlayerContainer::const_iterator i = _playersStore.begin(); i != _playersStore.end(); ++i)
    {
        Player* member = ObjectAccessor::FindConnectedPlayer(i->first);

        // 权限过滤：普通玩家不能看到 GM，GM 可以看到所有成员
        if (member &&
            (player->GetSession()->HasPermission(rbac::RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS) ||
             member->GetSession()->GetSecurity() <= AccountTypes(gmLevelInWhoList)) &&
            member->IsVisibleGloballyFor(player))
        {
            data << uint64(i->first);          // 成员 GUID
            data << uint8(i->second.flags);    // 成员权限标志
            ++count;
        }
    }

    // 填入实际的成员数量
    data.put<uint32>(pos, count);
    player->SendDirectMessage(&data);
}

/**
 * @brief 切换公告开关
 *
 * 切换频道的加入/离开公告功能。
 * 启用后，当玩家加入或离开频道时会向所有成员广播消息。
 *
 * @param player 执行操作的玩家
 */
void Channel::Announce(Player const* player)
{
    ObjectGuid guid = player->GetGUID();

    // 检查玩家是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查玩家是否有权限
    PlayerInfo& info = _playersStore.at(guid);
    if (!info.IsModerator() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR))
    {
        NotModeratorAppend appender;
        ChannelNameBuilder<NotModeratorAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 切换公告状态
    _announceEnabled = !_announceEnabled;

    // 广播公告状态变更
    if (_announceEnabled)
    {
        AnnouncementsOnAppend appender(guid);
        ChannelNameBuilder<AnnouncementsOnAppend> builder(this, appender);
        SendToAll(builder);
    }
    else
    {
        AnnouncementsOffAppend appender(guid);
        ChannelNameBuilder<AnnouncementsOffAppend> builder(this, appender);
        SendToAll(builder);
    }

    // 标记需要保存到数据库
    _isDirty = true;
}

/**
 * @brief 在频道中发言
 *
 * 向频道所有成员广播消息。
 * 会检查玩家是否被禁言。
 * 支持跨阵营语言通用化（可配置）。
 *
 * @param guid 发言玩家的 GUID
 * @param what 消息内容
 * @param lang 语言类型
 */
void Channel::Say(ObjectGuid guid, std::string const& what, uint32 lang) const
{
    if (what.empty())
        return;

    // 如果配置了跨阵营频道交互，使用通用语言
    // TODO: Add proper RBAC check
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
        lang = LANG_UNIVERSAL;

    // 检查玩家是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查玩家是否被禁言
    PlayerInfo const& info = _playersStore.at(guid);
    if (info.IsMuted())
    {
        MutedAppend appender;
        ChannelNameBuilder<MutedAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 构建聊天消息
    auto builder = [&](WorldPacket& data, LocaleConstant locale)
    {
        LocaleConstant localeIdx = sWorld->GetAvailableDbcLocale(locale);

        if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
            ChatHandler::BuildChatPacket(data, CHAT_MSG_CHANNEL, Language(lang), player, player, what, 0, GetName(localeIdx));
        else
            ChatHandler::BuildChatPacket(data, CHAT_MSG_CHANNEL, Language(lang), guid, guid, what, 0, "", "", 0, false, GetName(localeIdx));
    };

    // 广播消息，如果发言者不是管理员，则检查其他玩家的忽略列表
    SendToAll(builder, !info.IsModerator() ? guid : ObjectGuid::Empty);
}

/**
 * @brief 邀请玩家加入频道
 *
 * 邀请指定玩家加入频道。会检查：
 * - 目标玩家是否存在且可见
 * - 目标玩家是否被封禁
 * - 阵营限制
 * - 目标玩家是否已在频道中
 * - 目标玩家的忽略列表
 *
 * @param player 发起邀请的玩家
 * @param newname 被邀请玩家的名称
 */
void Channel::Invite(Player const* player, std::string const& newname)
{
    ObjectGuid guid = player->GetGUID();

    // 检查邀请者是否在频道中
    if (!IsOn(guid))
    {
        NotMemberAppend appender;
        ChannelNameBuilder<NotMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 查找目标玩家
    Player* newp = ObjectAccessor::FindConnectedPlayerByName(newname);
    if (!newp || !newp->isGMVisible())
    {
        PlayerNotFoundAppend appender(newname);
        ChannelNameBuilder<PlayerNotFoundAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查目标是否被封禁
    if (IsBanned(newp->GetGUID()))
    {
        PlayerInviteBannedAppend appender(newname);
        ChannelNameBuilder<PlayerInviteBannedAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查阵营限制
    if (newp->GetTeam() != player->GetTeam() &&
        (!player->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL) ||
        !newp->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL)))
    {
        InviteWrongFactionAppend appender;
        ChannelNameBuilder<InviteWrongFactionAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查目标是否已在频道中
    if (IsOn(newp->GetGUID()))
    {
        PlayerAlreadyMemberAppend appender(newp->GetGUID());
        ChannelNameBuilder<PlayerAlreadyMemberAppend> builder(this, appender);
        SendToOne(builder, guid);
        return;
    }

    // 检查目标玩家的忽略列表
    if (!newp->GetSocial()->HasIgnore(guid))
    {
        // 发送邀请给目标玩家
        InviteAppend appender(guid);
        ChannelNameBuilder<InviteAppend> builder(this, appender);
        SendToOne(builder, newp->GetGUID());
    }

    // 发送邀请成功消息给邀请者
    PlayerInvitedAppend appender(newp->GetName());
    ChannelNameBuilder<PlayerInvitedAppend> builder(this, appender);
    SendToOne(builder, guid);
}

/**
 * @brief 设置频道所有者（内部方法）
 *
 * 内部方法，用于转移频道所有权。
 * 会清除旧所有者的标志，设置新所有者的标志，并广播变更通知。
 *
 * @param guid 新所有者的 GUID
 * @param exclaim 是否广播所有者变更通知，默认为 true
 */
void Channel::SetOwner(ObjectGuid guid, bool exclaim)
{
    // 清除旧所有者的标志
    if (_ownerGuid)
    {
        auto itr = _playersStore.find(_ownerGuid);
        if (itr != _playersStore.end())
            itr->second.SetOwner(false);
    }

    // 设置新所有者
    _ownerGuid = guid;
    if (_ownerGuid)
    {
        uint8 oldFlag = GetPlayerFlags(_ownerGuid);
        auto itr = _playersStore.find(_ownerGuid);
        if (itr == _playersStore.end())
            return;

        // 新所有者自动获得管理员和所有者标志
        itr->second.SetModerator(true);
        itr->second.SetOwner(true);

        // 广播权限变更
        ModeChangeAppend appender(_ownerGuid, oldFlag, GetPlayerFlags(_ownerGuid));
        ChannelNameBuilder<ModeChangeAppend> builder(this, appender);
        SendToAll(builder);

        // 如果需要，广播所有者变更通知
        if (exclaim)
        {
            OwnerChangedAppend ownerAppender(_ownerGuid);
            ChannelNameBuilder<OwnerChangedAppend> ownerBuilder(this, ownerAppender);
            SendToAll(ownerBuilder);
        }

        // 标记需要保存到数据库
        _isDirty = true;
    }
}

/**
 * @brief 启用语音（未实现）
 *
 * 此功能在当前版本未实现。
 *
 * @param guid1 操作者 GUID
 * @param guid2 目标 GUID
 */
void Channel::Voice(ObjectGuid /*guid1*/, ObjectGuid /*guid2*/) const
{

}

/**
 * @brief 禁用语音（未实现）
 *
 * 此功能在当前版本未实现。
 *
 * @param guid1 操作者 GUID
 * @param guid2 目标 GUID
 */
void Channel::DeVoice(ObjectGuid /*guid1*/, ObjectGuid /*guid2*/) const
{

}

/**
 * @brief 玩家加入通知
 *
 * 向频道成员广播玩家加入通知。
 * 对于内置频道，不通知加入者本人。
 *
 * @param guid 加入玩家的 GUID
 */
void Channel::JoinNotify(ObjectGuid guid) const
{
    auto builder = [&](WorldPacket& data, LocaleConstant locale)
    {
        LocaleConstant localeIdx = sWorld->GetAvailableDbcLocale(locale);

        // 内置频道使用 SMSG_USERLIST_ADD，自定义频道使用 SMSG_USERLIST_UPDATE
        data.Initialize(IsConstant() ? SMSG_USERLIST_ADD : SMSG_USERLIST_UPDATE, 8 + 1 + 1 + 4 + 30 /*channelName buffer*/);
        data << uint64(guid);
        data << uint8(GetPlayerFlags(guid));
        data << uint8(GetFlags());
        data << uint32(GetNumPlayers());
        data << GetName(localeIdx);
    };

    // 内置频道不通知加入者本人，自定义频道通知所有人
    if (IsConstant())
        SendToAllButOne(builder, guid);
    else
        SendToAll(builder);
}

/**
 * @brief 玩家离开通知
 *
 * 向频道成员广播玩家离开通知。
 * 对于内置频道，不通知离开者本人。
 *
 * @param guid 离开玩家的 GUID
 */
void Channel::LeaveNotify(ObjectGuid guid) const
{
    auto builder = [&](WorldPacket& data, LocaleConstant locale)
    {
        LocaleConstant localeIdx = sWorld->GetAvailableDbcLocale(locale);

        data.Initialize(SMSG_USERLIST_REMOVE, 8 + 1 + 4 + 30 /*channelName buffer*/);
        data << uint64(guid);
        data << uint8(GetFlags());
        data << uint32(GetNumPlayers());
        data << GetName(localeIdx);
    };

    // 内置频道不通知离开者本人，自定义频道通知所有人
    if (IsConstant())
        SendToAllButOne(builder, guid);
    else
        SendToAll(builder);
}

/**
 * @brief 向所有频道成员发送消息
 *
 * 遍历所有频道成员并发送消息，支持本地化。
 * 会检查玩家的忽略列表（如果提供了排除 GUID）。
 *
 * @tparam Builder 消息构建器类型
 * @param builder 消息构建器
 * @param guid 排除的玩家 GUID（用于忽略列表检查），默认为空
 */
template<class Builder>
void Channel::SendToAll(Builder& builder, ObjectGuid guid /*= ObjectGuid::Empty*/) const
{
    Trinity::LocalizedPacketDo<Builder> localizer(builder);

    for (PlayerContainer::const_iterator i = _playersStore.begin(); i != _playersStore.end(); ++i)
    {
        if (Player* player = ObjectAccessor::FindConnectedPlayer(i->first))
        {
            // 如果提供了 GUID，检查玩家的忽略列表
            if (!guid || !player->GetSocial()->HasIgnore(guid))
                localizer(player);
        }
    }
}

/**
 * @brief 向除指定玩家外的所有成员发送消息
 *
 * 遍历所有频道成员（除指定玩家外）并发送消息，支持本地化。
 *
 * @tparam Builder 消息构建器类型
 * @param builder 消息构建器
 * @param who 要排除的玩家 GUID
 */
template<class Builder>
void Channel::SendToAllButOne(Builder& builder, ObjectGuid who) const
{
    Trinity::LocalizedPacketDo<Builder> localizer(builder);

    for (PlayerContainer::const_iterator i = _playersStore.begin(); i != _playersStore.end(); ++i)
    {
        if (i->first != who)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(i->first))
                localizer(player);
        }
    }
}

/**
 * @brief 向指定玩家发送消息
 *
 * 向指定玩家发送消息，支持本地化。
 *
 * @tparam Builder 消息构建器类型
 * @param builder 消息构建器
 * @param who 目标玩家 GUID
 */
template<class Builder>
void Channel::SendToOne(Builder& builder, ObjectGuid who) const
{
    Trinity::LocalizedPacketDo<Builder> localizer(builder);

    if (Player* player = ObjectAccessor::FindConnectedPlayer(who))
        localizer(player);
}
