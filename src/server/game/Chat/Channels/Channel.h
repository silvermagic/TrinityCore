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
 * @file Channel.h
 * @brief 聊天频道管理模块头文件
 *
 * 本模块实现了游戏中的聊天频道系统，包括：
 * - 内置频道（系统频道）：如综合、交易、寻求组队等
 * - 自定义频道：玩家创建的私人频道
 * - 频道成员管理：加入、离开、踢出、封禁
 * - 频道权限控制：房主、管理员、禁言等
 * - 频道属性配置：密码、公告、所有者转移等
 *
 * 主要功能：
 * - 支持跨阵营频道交互（可配置）
 * - 支持频道数据持久化到数据库
 * - 支持 GM 隐身加入频道
 * - 支持频道成员权限管理
 */

#ifndef _CHANNEL_H
#define _CHANNEL_H

#include "Common.h"
#include "ObjectGuid.h"
#include <ctime>
#include <map>
#include <unordered_set>

class Player;
struct AreaTableEntry;

/**
 * @enum ChatNotify
 * @brief 聊天频道通知消息类型枚举
 *
 * 定义了频道系统中所有可能的通知消息类型，用于向客户端发送频道事件通知。
 * 这些通知消息会在频道操作时发送给相关玩家，如加入、离开、踢出、封禁等操作。
 */
enum ChatNotify : uint8
{
    CHAT_JOINED_NOTICE                = 0x00,           ///< 玩家加入频道通知："%s joined channel."
    CHAT_LEFT_NOTICE                  = 0x01,           ///< 玩家离开频道通知："%s left channel."
    //CHAT_SUSPENDED_NOTICE             = 0x01,           // "%s left channel."
    CHAT_YOU_JOINED_NOTICE            = 0x02,           ///< 你加入了频道通知："Joined Channel: [%s]"
    //CHAT_YOU_CHANGED_NOTICE           = 0x02,           // "Changed Channel: [%s]";
    CHAT_YOU_LEFT_NOTICE              = 0x03,           ///< 你离开了频道通知："Left Channel: [%s]"
    CHAT_WRONG_PASSWORD_NOTICE        = 0x04,           ///< 密码错误通知："Wrong password for %s."
    CHAT_NOT_MEMBER_NOTICE            = 0x05,           ///< 非频道成员通知："Not on channel %s."
    CHAT_NOT_MODERATOR_NOTICE         = 0x06,           ///< 非管理员通知："Not a moderator of %s."
    CHAT_PASSWORD_CHANGED_NOTICE      = 0x07,           ///< 密码已更改通知："[%s] Password changed by %s."
    CHAT_OWNER_CHANGED_NOTICE         = 0x08,           ///< 所有者已更改通知："[%s] Owner changed to %s."
    CHAT_PLAYER_NOT_FOUND_NOTICE      = 0x09,           ///< 玩家未找到通知："[%s] Player %s was not found."
    CHAT_NOT_OWNER_NOTICE             = 0x0A,           ///< 非所有者通知："[%s] You are not the channel owner."
    CHAT_CHANNEL_OWNER_NOTICE         = 0x0B,           ///< 频道所有者通知："[%s] Channel owner is %s."
    CHAT_MODE_CHANGE_NOTICE           = 0x0C,           ///< 模式变更通知（用于权限变更）
    CHAT_ANNOUNCEMENTS_ON_NOTICE      = 0x0D,           ///< 启用公告通知："[%s] Channel announcements enabled by %s."
    CHAT_ANNOUNCEMENTS_OFF_NOTICE     = 0x0E,           ///< 禁用公告通知："[%s] Channel announcements disabled by %s."
    CHAT_MODERATION_ON_NOTICE         = 0x0F,           ///< 启用审核模式通知："[%s] Channel moderation enabled by %s."
    CHAT_MODERATION_OFF_NOTICE        = 0x10,           ///< 禁用审核模式通知："[%s] Channel moderation disabled by %s."
    CHAT_MUTED_NOTICE                 = 0x11,           ///< 被禁言通知："[%s] You do not have permission to speak."
    CHAT_PLAYER_KICKED_NOTICE         = 0x12,           ///< 玩家被踢出通知："[%s] Player %s kicked by %s."
    CHAT_BANNED_NOTICE                = 0x13,           ///< 被封禁通知："[%s] You are banned from that channel."
    CHAT_PLAYER_BANNED_NOTICE         = 0x14,           ///< 玩家被封禁通知："[%s] Player %s banned by %s."
    CHAT_PLAYER_UNBANNED_NOTICE       = 0x15,           ///< 玩家被解封通知："[%s] Player %s unbanned by %s."
    CHAT_PLAYER_NOT_BANNED_NOTICE     = 0x16,           ///< 玩家未被封禁通知："[%s] Player %s is not banned."
    CHAT_PLAYER_ALREADY_MEMBER_NOTICE = 0x17,           ///< 玩家已是成员通知："[%s] Player %s is already on the channel."
    CHAT_INVITE_NOTICE                = 0x18,           ///< 邀请通知："%2$s has invited you to join the channel '%1$s'."
    CHAT_INVITE_WRONG_FACTION_NOTICE  = 0x19,           ///< 邀请错误阵营通知："Target is in the wrong alliance for %s."
    CHAT_WRONG_FACTION_NOTICE         = 0x1A,           ///< 错误阵营通知："Wrong alliance for %s."
    CHAT_INVALID_NAME_NOTICE          = 0x1B,           ///< 无效频道名通知："Invalid channel name"
    CHAT_NOT_MODERATED_NOTICE         = 0x1C,           ///< 非审核模式通知："%s is not moderated"
    CHAT_PLAYER_INVITED_NOTICE        = 0x1D,           ///< 玩家已邀请通知："[%s] You invited %s to join the channel"
    CHAT_PLAYER_INVITE_BANNED_NOTICE  = 0x1E,           ///< 邀请的玩家已被封禁通知："[%s] %s has been banned."
    CHAT_THROTTLED_NOTICE             = 0x1F,           ///< 频率限制通知："[%s] The number of messages that can be sent to this channel is limited, please wait to send another message."
    CHAT_NOT_IN_AREA_NOTICE           = 0x20,           ///< 不在区域通知："[%s] You are not in the correct area for this channel." 玩家试图向区域特定频道发送消息，但不在该区域
    CHAT_NOT_IN_LFG_NOTICE            = 0x21,           ///< 不在 LFG 系统通知："[%s] You must be queued in looking for group before joining this channel." 玩家必须在 LFG 系统中才能加入 LFG 聊天频道
    CHAT_VOICE_ON_NOTICE              = 0x22,           ///< 启用语音通知："[%s] Channel voice enabled by %s."
    CHAT_VOICE_OFF_NOTICE             = 0x23            ///< 禁用语音通知："[%s] Channel voice disabled by %s."
};

/**
 * @enum ChannelFlags
 * @brief 频道标志位枚举
 *
 * 定义频道的各种属性标志，用于标识频道的类型和特性。
 * 这些标志会影响频道的行为和客户端显示方式。
 */
enum ChannelFlags
{
    CHANNEL_FLAG_NONE       = 0x00,  ///< 无标志
    CHANNEL_FLAG_CUSTOM     = 0x01,  ///< 自定义频道（玩家创建）
    // 0x02
    CHANNEL_FLAG_TRADE      = 0x04,  ///< 交易频道
    CHANNEL_FLAG_NOT_LFG    = 0x08,  ///< 非 LFG 频道
    CHANNEL_FLAG_GENERAL    = 0x10,  ///< 综合频道
    CHANNEL_FLAG_CITY       = 0x20,  ///< 城市频道
    CHANNEL_FLAG_LFG        = 0x40,  ///< 寻求组队频道
    CHANNEL_FLAG_VOICE      = 0x80   ///< 语音频道
    // 组合标志示例：
    // General（综合）              0x18 = 0x10 | 0x08
    // Trade（交易）                0x3C = 0x20 | 0x10 | 0x08 | 0x04
    // LocalDefense（本地防御）     0x18 = 0x10 | 0x08
    // GuildRecruitment（公会招募） 0x38 = 0x20 | 0x10 | 0x08
    // LookingForGroup（寻求组队）  0x50 = 0x40 | 0x10
};

/**
 * @enum ChannelDBCFlags
 * @brief 频道 DBC 配置标志枚举
 *
 * 这些标志来自 ChatChannels.dbc 文件，用于定义频道的系统级属性。
 * 与 ChannelFlags 不同，这些标志是只读的，由 DBC 数据决定。
 */
enum ChannelDBCFlags
{
    CHANNEL_DBC_FLAG_NONE       = 0x00000,  ///< 无标志
    CHANNEL_DBC_FLAG_INITIAL    = 0x00001,  ///< 初始频道（玩家登录自动加入）：General, Trade, LocalDefense, LFG
    CHANNEL_DBC_FLAG_ZONE_DEP   = 0x00002,  ///< 区域依赖频道：General, Trade, LocalDefense, GuildRecruitment
    CHANNEL_DBC_FLAG_GLOBAL     = 0x00004,  ///< 全局频道：WorldDefense
    CHANNEL_DBC_FLAG_TRADE      = 0x00008,  ///< 交易频道：Trade, LFG
    CHANNEL_DBC_FLAG_CITY_ONLY  = 0x00010,  ///< 仅城市频道：Trade, GuildRecruitment, LFG
    CHANNEL_DBC_FLAG_CITY_ONLY2 = 0x00020,  ///< 仅城市频道标志2：Trade, GuildRecruitment, LFG
    CHANNEL_DBC_FLAG_DEFENSE    = 0x10000,  ///< 防御频道：LocalDefense, WorldDefense
    CHANNEL_DBC_FLAG_GUILD_REQ  = 0x20000,  ///< 公会招募频道：GuildRecruitment
    CHANNEL_DBC_FLAG_LFG        = 0x40000,  ///< 寻求组队频道：LFG
    CHANNEL_DBC_FLAG_UNK1       = 0x80000   ///< 未知标志1：General
};

/**
 * @enum ChannelMemberFlags
 * @brief 频道成员权限标志枚举
 *
 * 定义频道成员的各种权限标志，用于控制成员在频道中的行为能力。
 */
enum ChannelMemberFlags
{
    MEMBER_FLAG_NONE        = 0x00,  ///< 无标志
    MEMBER_FLAG_OWNER       = 0x01,  ///< 频道所有者
    MEMBER_FLAG_MODERATOR   = 0x02,  ///< 频道管理员
    MEMBER_FLAG_VOICED      = 0x04,  ///< 有发言权（在审核模式下可以发言）
    MEMBER_FLAG_MUTED       = 0x08,  ///< 被禁言
    MEMBER_FLAG_CUSTOM      = 0x10,  ///< 自定义标志
    MEMBER_FLAG_MIC_MUTED   = 0x20   ///< 麦克风被禁用
    // 0x40
    // 0x80
};

/**
 * @class Channel
 * @brief 聊天频道类，管理单个聊天频道的所有功能
 *
 * Channel 类实现了游戏中的一个聊天频道，可以是：
 * 1. 内置频道（系统频道）：如综合、交易、寻求组队、本地防御等
 * 2. 自定义频道：玩家创建的私人频道，可以设置密码和所有者
 *
 * 主要职责：
 * - 管理频道成员列表和成员权限
 * - 处理玩家的加入、离开、踢出、封禁操作
 * - 控制频道属性（密码、公告、所有者等）
 * - 向频道成员广播消息和通知
 * - 维护频道数据的持久化（自定义频道）
 */
class TC_GAME_API Channel
{
    /**
     * @struct PlayerInfo
     * @brief 频道成员信息结构
     *
     * 存储单个玩家在频道中的状态信息，包括权限标志和隐身状态。
     */
    struct PlayerInfo
    {
        uint8 flags;       ///< 成员权限标志（ChannelMemberFlags 组合）
        bool invisible;    ///< 是否处于隐身状态（GM 隐身）

        /**
         * @brief 检查是否处于隐身状态
         * @return true 如果玩家隐身
         */
        bool IsInvisible() const { return invisible; }

        /**
         * @brief 设置隐身状态
         * @param on true 为隐身，false 为可见
         */
        void SetInvisible(bool on) { invisible = on; }

        /**
         * @brief 检查是否拥有指定权限标志
         * @param flag 要检查的权限标志
         * @return true 如果拥有该权限
         */
        bool HasFlag(uint8 flag) const { return (flags & flag) != 0; }

        /**
         * @brief 添加权限标志
         * @param flag 要添加的权限标志
         */
        void SetFlag(uint8 flag) { flags |= flag; }

        /**
         * @brief 检查是否为频道所有者
         * @return true 如果是所有者
         */
        bool IsOwner() const { return (flags & MEMBER_FLAG_OWNER) != 0; }

        /**
         * @brief 设置所有者状态
         * @param state true 为所有者，false 为非所有者
         */
        void SetOwner(bool state)
        {
            if (state) flags |= MEMBER_FLAG_OWNER;
            else flags &= ~MEMBER_FLAG_OWNER;
        }

        /**
         * @brief 检查是否为管理员
         * @return true 如果是管理员
         */
        bool IsModerator() const { return (flags & MEMBER_FLAG_MODERATOR) != 0; }

        /**
         * @brief 设置管理员状态
         * @param state true 为管理员，false 为非管理员
         */
        void SetModerator(bool state)
        {
            if (state) flags |= MEMBER_FLAG_MODERATOR;
            else flags &= ~MEMBER_FLAG_MODERATOR;
        }

        /**
         * @brief 检查是否被禁言
         * @return true 如果被禁言
         */
        bool IsMuted() const { return (flags & MEMBER_FLAG_MUTED) != 0; }

        /**
         * @brief 设置禁言状态
         * @param state true 为禁言，false 为解除禁言
         */
        void SetMuted(bool state)
        {
            if (state) flags |= MEMBER_FLAG_MUTED;
            else flags &= ~MEMBER_FLAG_MUTED;
        }
    };

    public:
        /**
         * @brief 内置频道构造函数
         * @param channelId 频道 ID（来自 ChatChannels.dbc）
         * @param team 阵营（ALLIANCE 或 HORDE），默认为 0
         * @param zoneEntry 区域信息，用于区域相关频道，默认为 nullptr
         *
         * 用于创建系统内置频道，如综合、交易、寻求组队等。
         * 这些频道由 DBC 定义，自动管理。
         */
        Channel(uint32 channelId, uint32 team = 0, AreaTableEntry const* zoneEntry = nullptr);

        /**
         * @brief 自定义频道构造函数
         * @param name 频道名称
         * @param team 阵营（ALLIANCE 或 HORDE）
         * @param banList 封禁列表（空格分隔的 GUID 列表），默认为空
         *
         * 用于创建玩家自定义的私人频道。
         * 自定义频道可以设置密码、所有者，并持久化到数据库。
         */
        Channel(std::string const& name, uint32 team, std::string const& banList = "");

        /**
         * @brief 获取频道名称
         * @param channelName 输出参数，返回频道名称
         * @param channelId 频道 ID
         * @param locale 语言区域设置
         * @param zoneEntry 区域信息
         *
         * 根据频道类型和区域信息生成本地化的频道名称。
         * 对于区域依赖频道，名称会包含区域名称。
         */
        static void GetChannelName(std::string& channelName, uint32 channelId, LocaleConstant locale, AreaTableEntry const* zoneEntry);

        /**
         * @brief 获取频道名称
         * @param locale 语言区域设置，默认为 DEFAULT_LOCALE
         * @return 频道名称字符串
         */
        std::string GetName(LocaleConstant locale = DEFAULT_LOCALE) const;

        /**
         * @brief 获取频道 ID
         * @return 频道 ID，0 表示自定义频道
         */
        uint32 GetChannelId() const { return _channelId; }

        /**
         * @brief 检查是否为内置频道
         * @return true 如果是内置频道
         */
        bool IsConstant() const { return _channelId != 0; }

        /**
         * @brief 检查是否为 LFG 频道
         * @return true 如果是 LFG 频道
         */
        bool IsLFG() const { return (GetFlags() & CHANNEL_FLAG_LFG) != 0; }

        /**
         * @brief 检查是否启用了公告
         * @return true 如果启用公告
         */
        bool IsAnnounce() const { return _announceEnabled; }

        /**
         * @brief 设置公告开关
         * @param announce true 启用，false 禁用
         */
        void SetAnnounce(bool announce) { _announceEnabled = announce; }

        /**
         * @brief 标记频道为脏数据
         *
         * 标记频道需要在下次保存间隔时保存到数据库。
         * 在修改频道属性（密码、公告、封禁列表等）后调用。
         */
        void SetDirty() { _isDirty = true; }

        /**
         * @brief 更新频道数据到数据库
         *
         * 定期调用以保存自定义频道的设置和活动状态。
         * 如果频道被标记为脏数据，会保存完整设置；
         * 否则只更新活动时间戳。
         */
        void UpdateChannelInDB();

        /**
         * @brief 设置频道密码
         * @param password 新密码
         */
        void SetPassword(std::string const& password) { _channelPassword = password; }

        /**
         * @brief 检查密码是否正确
         * @param password 待验证的密码
         * @return true 如果密码正确或频道无密码
         */
        bool CheckPassword(std::string const& password) const { return _channelPassword.empty() || (_channelPassword == password); }

        /**
         * @brief 获取频道中的玩家数量
         * @return 玩家数量
         */
        uint32 GetNumPlayers() const { return _playersStore.size(); }

        /**
         * @brief 获取频道标志
         * @return 频道标志位组合
         */
        uint8 GetFlags() const { return _channelFlags; }

        /**
         * @brief 检查是否拥有指定标志
         * @param flag 要检查的标志
         * @return true 如果拥有该标志
         */
        bool HasFlag(uint8 flag) const { return (_channelFlags & flag) != 0; }

        /**
         * @brief 获取区域信息
         * @return 区域表条目指针，如果不是区域频道则为 nullptr
         */
        AreaTableEntry const* GetZoneEntry() const { return _zoneEntry; }

        /**
         * @brief 玩家加入频道
         * @param player 加入的玩家指针
         * @param pass 频道密码，默认为空
         *
         * 处理玩家加入频道的完整流程：
         * 1. 检查是否已在频道中
         * 2. 检查是否被封禁
         * 3. 验证密码
         * 4. 检查 LFG 频道限制
         * 5. 添加到成员列表
         * 6. 发送加入通知
         * 7. 处理所有权转移（自定义频道）
         */
        void JoinChannel(Player* player, std::string const& pass = "");

        /**
         * @brief 玩家离开频道
         * @param player 离开的玩家指针
         * @param send 是否发送离开通知，默认为 true
         *
         * 处理玩家离开频道的完整流程：
         * 1. 检查是否在频道中
         * 2. 发送离开通知
         * 3. 从成员列表移除
         * 4. 处理所有权转移（如果所有者离开）
         */
        void LeaveChannel(Player* player, bool send = true);

        /**
         * @brief 踢出或封禁玩家
         * @param player 执行操作的玩家
         * @param badname 目标玩家名称
         * @param ban true 为封禁，false 为仅踢出
         *
         * 由 Kick 和 Ban 方法调用，执行实际的踢出/封禁逻辑。
         */
        void KickOrBan(Player const* player, std::string const& badname, bool ban);

        /**
         * @brief 踢出玩家
         * @param player 执行操作的玩家
         * @param badname 目标玩家名称
         */
        void Kick(Player const* player, std::string const& badname) { KickOrBan(player, badname, false); }

        /**
         * @brief 封禁玩家
         * @param player 执行操作的玩家
         * @param badname 目标玩家名称
         */
        void Ban(Player const* player, std::string const& badname) { KickOrBan(player, badname, true); }

        /**
         * @brief 解除封禁
         * @param player 执行操作的玩家
         * @param badname 目标玩家名称
         */
        void UnBan(Player const* player, std::string const& badname);

        /**
         * @brief 设置频道密码
         * @param player 执行操作的玩家
         * @param pass 新密码
         */
        void Password(Player const* player, std::string const& pass);

        /**
         * @brief 设置玩家权限模式
         * @param player 执行操作的玩家
         * @param p2n 目标玩家名称
         * @param mod true 为管理员权限，false 为禁言权限
         * @param set true 为设置权限，false 为取消权限
         */
        void SetMode(Player const* player, std::string const& p2n, bool mod, bool set);

        /**
         * @brief 设置管理员
         * @param player 执行操作的玩家
         * @param newname 目标玩家名称
         */
        void SetModerator(Player const* player, std::string const& newname) { SetMode(player, newname, true, true); }

        /**
         * @brief 取消管理员
         * @param player 执行操作的玩家
         * @param newname 目标玩家名称
         */
        void UnsetModerator(Player const* player, std::string const& newname) { SetMode(player, newname, true, false); }

        /**
         * @brief 禁言玩家
         * @param player 执行操作的玩家
         * @param newname 目标玩家名称
         */
        void SetMute(Player const* player, std::string const& newname) { SetMode(player, newname, false, true); }

        /**
         * @brief 解除禁言
         * @param player 执行操作的玩家
         * @param newname 目标玩家名称
         */
        void UnsetMute(Player const* player, std::string const& newname) { SetMode(player, newname, false, false); }

        /**
         * @brief 设置玩家隐身状态
         * @param player 目标玩家
         * @param on true 为隐身，false 为可见
         *
         * 更新玩家的隐身状态，如果玩家是所有者，也会更新所有者隐身标志。
         */
        void SetInvisible(Player const* player, bool on);

        /**
         * @brief 设置频道所有者（内部方法）
         * @param guid 新所有者的 GUID
         * @param exclaim 是否广播所有者变更通知，默认为 true
         */
        void SetOwner(ObjectGuid guid, bool exclaim = true);

        /**
         * @brief 设置频道所有者（玩家命令）
         * @param player 执行操作的玩家
         * @param name 新所有者的名称
         */
        void SetOwner(Player const* player, std::string const& name);

        /**
         * @brief 发送所有者信息查询响应
         * @param guid 查询玩家的 GUID
         */
        void SendWhoOwner(ObjectGuid guid);

        /**
         * @brief 列出频道成员
         * @param player 请求列表的玩家
         *
         * 发送频道成员列表给指定玩家，包括成员的 GUID 和权限标志。
         * GM 可以看到所有成员，普通玩家只能看到权限等级较低或相等的成员。
         */
        void List(Player const* player) const;

        /**
         * @brief 切换公告开关
         * @param player 执行操作的玩家
         */
        void Announce(Player const* player);

        /**
         * @brief 在频道中发言
         * @param guid 发言玩家的 GUID
         * @param what 消息内容
         * @param lang 语言类型
         *
         * 向频道所有成员广播消息。
         * 会检查玩家是否被禁言。
         * 支持跨阵营语言通用化（可配置）。
         */
        void Say(ObjectGuid guid, std::string const& what, uint32 lang) const;

        /**
         * @brief 邀请玩家加入频道
         * @param player 发起邀请的玩家
         * @param newp 被邀请玩家的名称
         */
        void Invite(Player const* player, std::string const& newp);

        /**
         * @brief 启用语音（未实现）
         * @param guid1 操作者 GUID
         * @param guid2 目标 GUID
         */
        void Voice(ObjectGuid guid1, ObjectGuid guid2) const;

        /**
         * @brief 禁用语音（未实现）
         * @param guid1 操作者 GUID
         * @param guid2 目标 GUID
         */
        void DeVoice(ObjectGuid guid1, ObjectGuid guid2) const;

        /**
         * @brief 玩家加入通知
         * @param guid 加入玩家的 GUID
         *
         * 向频道成员广播玩家加入通知。
         * 对于内置频道，不通知加入者本人。
         */
        void JoinNotify(ObjectGuid guid) const;

        /**
         * @brief 玩家离开通知
         * @param guid 离开玩家的 GUID
         *
         * 向频道成员广播玩家离开通知。
         * 对于内置频道，不通知离开者本人。
         */
        void LeaveNotify(ObjectGuid guid) const;

        /**
         * @brief 设置所有权功能开关
         * @param ownership true 启用，false 禁用
         */
        void SetOwnership(bool ownership) { _ownershipEnabled = ownership; }

    private:

        /**
         * @brief 向所有频道成员发送消息
         * @param builder 消息构建器
         * @param guid 排除的玩家 GUID（用于忽略列表检查），默认为空
         *
         * 遍历所有频道成员并发送消息，支持本地化。
         * 会检查玩家的忽略列表。
         */
        template<class Builder>
        void SendToAll(Builder&, ObjectGuid guid = ObjectGuid::Empty) const;

        /**
         * @brief 向除指定玩家外的所有成员发送消息
         * @param builder 消息构建器
         * @param who 要排除的玩家 GUID
         */
        template<class Builder>
        void SendToAllButOne(Builder& builder, ObjectGuid who) const;

        /**
         * @brief 向指定玩家发送消息
         * @param builder 消息构建器
         * @param who 目标玩家 GUID
         */
        template<class Builder>
        void SendToOne(Builder& builder, ObjectGuid who) const;

        /**
         * @brief 检查玩家是否在频道中
         * @param who 玩家 GUID
         * @return true 如果玩家在频道中
         */
        bool IsOn(ObjectGuid who) const { return _playersStore.find(who) != _playersStore.end(); }

        /**
         * @brief 检查玩家是否被封禁
         * @param guid 玩家 GUID
         * @return true 如果玩家被封禁
         */
        bool IsBanned(ObjectGuid guid) const { return _bannedStore.find(guid) != _bannedStore.end(); }

        /**
         * @brief 获取玩家权限标志
         * @param guid 玩家 GUID
         * @return 权限标志，如果玩家不在频道中返回 0
         */
        uint8 GetPlayerFlags(ObjectGuid guid) const
        {
            PlayerContainer::const_iterator itr = _playersStore.find(guid);
            return itr != _playersStore.end() ? itr->second.flags : 0;
        }

        /**
         * @brief 设置管理员权限（内部方法）
         * @param guid 目标玩家 GUID
         * @param set true 设置，false 取消
         */
        void SetModerator(ObjectGuid guid, bool set);

        /**
         * @brief 设置禁言状态（内部方法）
         * @param guid 目标玩家 GUID
         * @param set true 禁言，false 解除
         */
        void SetMute(ObjectGuid guid, bool set);

        typedef std::map<ObjectGuid, PlayerInfo> PlayerContainer;  ///< 玩家容器类型
        typedef GuidUnorderedSet BannedContainer;                  ///< 封禁列表容器类型

        bool _isDirty;                    ///< 是否需要保存到数据库
        time_t _nextActivityUpdateTime;   ///< 下次活动更新时间

        bool _announceEnabled;            ///< 是否启用加入/离开公告
        bool _ownershipEnabled;           ///< 是否启用所有权管理
        bool _isOwnerInvisible;           ///< 所有者是否为隐身 GM

        uint8 _channelFlags;              ///< 频道标志
        uint32 _channelId;                ///< 频道 ID（0 为自定义频道）
        uint32 _channelTeam;              ///< 频道所属阵营
        ObjectGuid _ownerGuid;            ///< 所有者 GUID
        std::string _channelName;         ///< 频道名称
        std::string _channelPassword;     ///< 频道密码
        PlayerContainer _playersStore;    ///< 玩家成员列表
        BannedContainer _bannedStore;     ///< 封禁玩家列表

        AreaTableEntry const* _zoneEntry; ///< 区域信息（用于区域相关频道）
};
#endif
