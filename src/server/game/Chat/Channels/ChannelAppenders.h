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
 * @file ChannelAppenders.h
 * @brief 频道通知数据包构建器定义文件
 *
 * 本文件定义了一系列用于构建频道通知数据包的结构体和模板类。
 * 这些构建器遵循策略模式，每个结构体负责一种特定类型的频道通知消息的构建。
 *
 * 设计模式：
 * - 使用模板方法模式，ChannelNameBuilder 作为模板框架
 * - 各个 Append 结构体作为策略实现，负责具体的消息体构建
 * - 每个结构体定义统一的 NotificationType 和 Append 接口
 *
 * 使用场景：
 * - 玩家加入/离开频道通知
 * - 频道权限变更通知（管理员、禁言等）
 * - 频道踢人/封禁通知
 * - 频道邀请通知
 * - 各种错误提示通知
 *
 * @see Channel
 * @see WorldPacket
 */

#ifndef _CHANNELAPPENDERS_H
#define _CHANNELAPPENDERS_H

#include "Channel.h"
#include "CharacterCache.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @class ChannelNameBuilder
 * @brief 频道名称数据包构建器模板类
 *
 * 该模板类用于构建频道通知数据包的初始部分，包括通知类型和频道名称。
 * 它作为数据包构建的框架，将具体的消息体构建委托给 PacketModifier 策略对象。
 *
 * @tparam PacketModifier 数据包修饰器类型，必须提供以下接口：
 *         - static uint8 const NotificationType: 通知类型常量
 *         - void Append(WorldPacket& data) const: 追加消息体数据的函数
 *
 * 工作流程：
 * 1. 初始化数据包，设置操作码为 SMSG_CHANNEL_NOTIFY
 * 2. 写入通知类型（由 PacketModifier 提供）
 * 3. 写入频道名称（根据客户端语言环境本地化）
 * 4. 调用 PacketModifier.Append() 写入具体的消息体数据
 *
 * 使用示例：
 * @code
 * JoinedAppend modifier(playerGuid);
 * ChannelNameBuilder<JoinedAppend> builder(channel, modifier);
 * // 使用 builder 构建数据包...
 * @endcode
 */
template<class PacketModifier>
class ChannelNameBuilder
{
    public:
        /**
         * @brief 构造函数
         *
         * @param source 源频道对象指针，用于获取频道名称等信息
         * @param modifier 数据包修饰器对象，用于追加特定的消息体数据
         */
        ChannelNameBuilder(Channel const* source, PacketModifier const& modifier)
            : _source(source), _modifier(modifier){ }

        /**
         * @brief 函数调用操作符，构建数据包
         *
         * 该操作符构建频道通知数据包，包括通知类型、频道名称和自定义消息体。
         * 数据包将被发送给客户端以通知各种频道事件。
         *
         * @param data [out] 要构建的 WorldPacket 对象引用
         * @param locale 客户端语言环境常量，用于本地化频道名称
         *
         * @note LocalizedPacketDo 发送的是客户端 DBC 语言环境，
         *       需要转换为服务器可用的语言环境
         */
        void operator()(WorldPacket& data, LocaleConstant locale) const
        {
            // LocalizedPacketDo 发送客户端 DBC 语言环境，需要获取服务器可用的语言环境
            LocaleConstant localeIdx = sWorld->GetAvailableDbcLocale(locale);

            data.Initialize(SMSG_CHANNEL_NOTIFY, 60); // 估算大小
            data << uint8(_modifier.NotificationType);
            data << _source->GetName(localeIdx);
            _modifier.Append(data);
        }

        private:
            Channel const* _source;     ///< 源频道对象指针
            PacketModifier _modifier;    ///< 数据包修饰器对象
};

/**
 * @struct JoinedAppend
 * @brief 玩家加入频道通知数据追加器
 *
 * 当玩家加入频道时，该结构体负责向数据包追加加入玩家的 GUID。
 * 该通知会发送给频道内的所有其他成员，告知有人加入了频道。
 *
 * 通知类型: CHAT_JOINED_NOTICE
 * 消息体内容: 加入玩家的 GUID (uint64)
 *
 * 使用场景：玩家成功加入频道后广播给频道内其他成员
 */
struct JoinedAppend
{
    /**
     * @brief 构造函数
     * @param guid 加入频道的玩家 GUID
     */
    explicit JoinedAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_JOINED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 加入频道的玩家 GUID
};

/**
 * @struct LeftAppend
 * @brief 玩家离开频道通知数据追加器
 *
 * 当玩家离开频道时，该结构体负责向数据包追加离开玩家的 GUID。
 * 该通知会发送给频道内的所有其他成员，告知有人离开了频道。
 *
 * 通知类型: CHAT_LEFT_NOTICE
 * 消息体内容: 离开玩家的 GUID (uint64)
 *
 * 使用场景：玩家主动离开或被踢出频道后广播给频道内其他成员
 */
struct LeftAppend
{
    /**
     * @brief 构造函数
     * @param guid 离开频道的玩家 GUID
     */
    explicit LeftAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_LEFT_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 离开频道的玩家 GUID
};

/**
 * @struct YouJoinedAppend
 * @brief 玩家自己加入频道通知数据追加器
 *
 * 当玩家自己成功加入频道时，该结构体负责向数据包追加频道详细信息。
 * 该通知仅发送给加入者本人，告知其已成功加入频道及相关频道信息。
 *
 * 通知类型: CHAT_YOU_JOINED_NOTICE
 * 消息体内容:
 *   - 频道标志 (uint8): 频道的属性标志，如是否为系统频道、是否需要密码等
 *   - 频道 ID (uint32): 频道的唯一标识符
 *   - 未知字段 (uint32): 始终为 0，用途不明
 *
 * 使用场景：玩家成功加入频道后发送给该玩家本人
 */
struct YouJoinedAppend
{
    /**
     * @brief 构造函数
     * @param channel 玩家加入的频道对象指针
     */
    explicit YouJoinedAppend(Channel const* channel) : _channel(channel) { }

    static uint8 const NotificationType = CHAT_YOU_JOINED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint8(_channel->GetFlags());
        data << uint32(_channel->GetChannelId());
        data << uint32(0);
    }

private:
    Channel const* _channel;  ///< 玩家加入的频道对象指针
};

/**
 * @struct YouLeftAppend
 * @brief 玩家自己离开频道通知数据追加器
 *
 * 当玩家自己离开频道时，该结构体负责向数据包追加频道信息。
 * 该通知仅发送给离开者本人，确认其已离开频道。
 *
 * 通知类型: CHAT_YOU_LEFT_NOTICE
 * 消息体内容:
 *   - 频道 ID (uint32): 频道的唯一标识符
 *   - 是否为常驻频道 (uint8): 标识频道是否为系统常驻频道
 *
 * 使用场景：玩家离开频道后发送给该玩家本人确认离开
 */
struct YouLeftAppend
{
    /**
     * @brief 构造函数
     * @param channel 玩家离开的频道对象指针
     */
    explicit YouLeftAppend(Channel const* channel) : _channel(channel) { }

    static uint8 const NotificationType = CHAT_YOU_LEFT_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint32(_channel->GetChannelId());
        data << uint8(_channel->IsConstant());
    }

private:
    Channel const* _channel;  ///< 玩家离开的频道对象指针
};

/**
 * @struct WrongPasswordAppend
 * @brief 频道密码错误通知数据追加器
 *
 * 当玩家尝试加入需要密码的频道时，如果提供的密码不正确，
 * 该结构体负责构建密码错误通知。
 *
 * 通知类型: CHAT_WRONG_PASSWORD_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家输入错误的频道密码时发送给该玩家
 */
struct WrongPasswordAppend
{
    static uint8 const NotificationType = CHAT_WRONG_PASSWORD_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct NotMemberAppend
 * @brief 非频道成员通知数据追加器
 *
 * 当非频道成员尝试执行仅限成员的操作时（如发言、邀请等），
 * 该结构体负责构建非成员错误通知。
 *
 * 通知类型: CHAT_NOT_MEMBER_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：非频道成员尝试执行需要成员权限的操作时发送
 */
struct NotMemberAppend
{
    static uint8 const NotificationType = CHAT_NOT_MEMBER_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct NotModeratorAppend
 * @brief 非管理员权限通知数据追加器
 *
 * 当普通成员尝试执行需要管理员权限的操作时（如踢人、禁言等），
 * 该结构体负责构建权限不足错误通知。
 *
 * 通知类型: CHAT_NOT_MODERATOR_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：非管理员尝试执行需要管理员权限的操作时发送
 */
struct NotModeratorAppend
{
    static uint8 const NotificationType = CHAT_NOT_MODERATOR_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct PasswordChangedAppend
 * @brief 频道密码变更通知数据追加器
 *
 * 当频道的密码被修改时，该结构体负责向数据包追加修改密码的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知频道密码已更改。
 *
 * 通知类型: CHAT_PASSWORD_CHANGED_NOTICE
 * 消息体内容: 修改密码的管理员 GUID (uint64)
 *
 * 使用场景：频道管理员修改频道密码后广播给所有频道成员
 */
struct PasswordChangedAppend
{
    /**
     * @brief 构造函数
     * @param guid 修改密码的管理员 GUID
     */
    explicit PasswordChangedAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_PASSWORD_CHANGED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 修改密码的管理员 GUID
};

/**
 * @struct OwnerChangedAppend
 * @brief 频道所有者变更通知数据追加器
 *
 * 当频道的所有者发生变更时，该结构体负责向数据包追加新所有者的 GUID。
 * 该通知会广播给频道内的所有成员，告知频道所有权已转移。
 *
 * 通知类型: CHAT_OWNER_CHANGED_NOTICE
 * 消息体内容: 新所有者的 GUID (uint64)
 *
 * 使用场景：频道所有权转移时广播给所有频道成员
 */
struct OwnerChangedAppend
{
    /**
     * @brief 构造函数
     * @param guid 新所有者的 GUID
     */
    explicit OwnerChangedAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_OWNER_CHANGED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 新所有者的 GUID
};

/**
 * @struct PlayerNotFoundAppend
 * @brief 玩家未找到通知数据追加器
 *
 * 当尝试对不存在的玩家执行操作时（如邀请不在线的玩家），
 * 该结构体负责向数据包追加未找到的玩家名称。
 *
 * 通知类型: CHAT_PLAYER_NOT_FOUND_NOTICE
 * 消息体内容: 未找到的玩家名称 (string)
 *
 * 使用场景：尝试对不存在的玩家执行频道操作时发送给操作者
 */
struct PlayerNotFoundAppend
{
    /**
     * @brief 构造函数
     * @param playerName 未找到的玩家名称
     */
    explicit PlayerNotFoundAppend(std::string const& playerName) : _playerName(playerName) { }

    static uint8 const NotificationType = CHAT_PLAYER_NOT_FOUND_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << _playerName;
    }

private:
    std::string _playerName;  ///< 未找到的玩家名称
};

/**
 * @struct NotOwnerAppend
 * @brief 非频道所有者通知数据追加器
 *
 * 当非频道所有者尝试执行需要所有者权限的操作时，
 * 该结构体负责构建权限不足错误通知。
 *
 * 通知类型: CHAT_NOT_OWNER_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：非频道所有者尝试执行需要所有者权限的操作时发送
 */
struct NotOwnerAppend
{
    static uint8 const NotificationType = CHAT_NOT_OWNER_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct ChannelOwnerAppend
 * @brief 频道所有者信息通知数据追加器
 *
 * 当查询频道所有者信息时，该结构体负责向数据包追加频道所有者的名称。
 * 如果频道是常驻频道或没有所有者，则显示 "Nobody"。
 *
 * 通知类型: CHAT_CHANNEL_OWNER_NOTICE
 * 消息体内容: 所有者名称 (string)，如果没有所有者则为 "Nobody"
 *
 * 使用场景：玩家查询频道所有者信息时发送
 *
 * @note 构造函数会自动从角色缓存中查询所有者名称
 */
struct ChannelOwnerAppend
{
    /**
     * @brief 构造函数
     *
     * 从角色缓存中查询所有者名称，如果频道是常驻频道或所有者 GUID 为空，
     * 则 _ownerName 保持为空字符串。
     *
     * @param channel 频道对象指针
     * @param ownerGuid 所有者的 GUID
     */
    explicit ChannelOwnerAppend(Channel const* channel, ObjectGuid const& ownerGuid) : _channel(channel), _ownerGuid(ownerGuid)
    {
        if (CharacterCacheEntry const* cInfo = sCharacterCache->GetCharacterCacheByGuid(_ownerGuid))
            _ownerName = cInfo->Name;
    }

    static uint8 const NotificationType = CHAT_CHANNEL_OWNER_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     *
     * 如果频道是常驻频道或没有所有者，写入 "Nobody"，否则写入所有者名称
     */
    void Append(WorldPacket& data) const
    {
        data << ((_channel->IsConstant() || !_ownerGuid) ? "Nobody" : _ownerName);
    }

private:
    Channel const* _channel;   ///< 频道对象指针
    ObjectGuid _ownerGuid;      ///< 所有者 GUID

    std::string _ownerName;     ///< 所有者名称（从角色缓存查询）
};

/**
 * @struct ModeChangeAppend
 * @brief 频道成员权限模式变更通知数据追加器
 *
 * 当频道成员的权限标志发生变更时（如被设为管理员、被禁言等），
 * 该结构体负责向数据包追加相关玩家的 GUID 和权限变更信息。
 * 该通知会广播给频道内的所有成员。
 *
 * 通知类型: CHAT_MODE_CHANGE_NOTICE
 * 消息体内容:
 *   - 成员 GUID (uint64): 权限发生变更的玩家 GUID
 *   - 旧权限标志 (uint8): 变更前的权限标志
 *   - 新权限标志 (uint8): 变更后的权限标志
 *
 * 使用场景：频道管理员修改成员权限时广播给所有频道成员
 *
 * 权限标志包括：管理员、禁言、语音等
 */
struct ModeChangeAppend
{
    /**
     * @brief 构造函数
     * @param guid 权限发生变更的玩家 GUID
     * @param oldFlags 变更前的权限标志
     * @param newFlags 变更后的权限标志
     */
    explicit ModeChangeAppend(ObjectGuid const& guid, uint8 oldFlags, uint8 newFlags) : _guid(guid), _oldFlags(oldFlags), _newFlags(newFlags) { }

    static uint8 const NotificationType = CHAT_MODE_CHANGE_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
        data << uint8(_oldFlags);
        data << uint8(_newFlags);
    }

private:
    ObjectGuid _guid;   ///< 权限发生变更的玩家 GUID
    uint8 _oldFlags;     ///< 变更前的权限标志
    uint8 _newFlags;     ///< 变更后的权限标志
};

/**
 * @struct AnnouncementsOnAppend
 * @brief 频道公告功能开启通知数据追加器
 *
 * 当频道开启公告功能时，该结构体负责向数据包追加开启公告的管理员 GUID。
 * 公告功能开启后，玩家加入/离开频道时会向所有频道成员广播通知。
 *
 * 通知类型: CHAT_ANNOUNCEMENTS_ON_NOTICE
 * 消息体内容: 开启公告的管理员 GUID (uint64)
 *
 * 使用场景：频道管理员开启公告功能后广播给所有频道成员
 */
struct AnnouncementsOnAppend
{
    /**
     * @brief 构造函数
     * @param guid 开启公告功能的管理员 GUID
     */
    explicit AnnouncementsOnAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_ANNOUNCEMENTS_ON_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 开启公告功能的管理员 GUID
};

/**
 * @struct AnnouncementsOffAppend
 * @brief 频道公告功能关闭通知数据追加器
 *
 * 当频道关闭公告功能时，该结构体负责向数据包追加关闭公告的管理员 GUID。
 * 公告功能关闭后，玩家加入/离开频道时不会向其他频道成员广播通知。
 *
 * 通知类型: CHAT_ANNOUNCEMENTS_OFF_NOTICE
 * 消息体内容: 关闭公告的管理员 GUID (uint64)
 *
 * 使用场景：频道管理员关闭公告功能后广播给所有频道成员
 */
struct AnnouncementsOffAppend
{
    /**
     * @brief 构造函数
     * @param guid 关闭公告功能的管理员 GUID
     */
    explicit AnnouncementsOffAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_ANNOUNCEMENTS_OFF_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 关闭公告功能的管理员 GUID
};

/**
 * @struct MutedAppend
 * @brief 玩家被禁言通知数据追加器
 *
 * 当玩家在频道中被禁言时，该结构体负责构建禁言通知。
 * 被禁言的玩家无法在频道中发送消息。
 *
 * 通知类型: CHAT_MUTED_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家尝试在频道发言但已被禁言时发送给该玩家
 */
struct MutedAppend
{
    static uint8 const NotificationType = CHAT_MUTED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct PlayerKickedAppend
 * @brief 玩家被踢出频道通知数据追加器
 *
 * 当玩家被管理员踢出频道时，该结构体负责向数据包追加被踢玩家和执行踢人操作的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知有人被踢出频道。
 *
 * 通知类型: CHAT_PLAYER_KICKED_NOTICE
 * 消息体内容:
 *   - 被踢玩家 GUID (uint64): 被踢出频道的玩家 GUID
 *   - 执行踢人的管理员 GUID (uint64): 执行踢人操作的管理员 GUID
 *
 * 使用场景：管理员踢出频道成员后广播给所有频道成员
 */
struct PlayerKickedAppend
{
    /**
     * @brief 构造函数
     * @param kicker 执行踢人操作的管理员 GUID
     * @param kickee 被踢出频道的玩家 GUID
     */
    explicit PlayerKickedAppend(ObjectGuid const& kicker, ObjectGuid const& kickee) : _kicker(kicker), _kickee(kickee) { }

    static uint8 const NotificationType = CHAT_PLAYER_KICKED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     *
     * 注意：数据包中先写入被踢玩家的 GUID，再写入执行踢人的管理员 GUID
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_kickee);
        data << uint64(_kicker);
    }

private:
    ObjectGuid _kicker;   ///< 执行踢人操作的管理员 GUID
    ObjectGuid _kickee;   ///< 被踢出频道的玩家 GUID
};

/**
 * @struct BannedAppend
 * @brief 玩家已被封禁通知数据追加器
 *
 * 当被封禁的玩家尝试加入频道时，该结构体负责构建已被封禁的通知。
 * 封禁列表中的玩家无法加入频道。
 *
 * 通知类型: CHAT_BANNED_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：被封禁玩家尝试加入频道时发送给该玩家
 */
struct BannedAppend
{
    static uint8 const NotificationType = CHAT_BANNED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct PlayerBannedAppend
 * @brief 玩家被加入封禁名单通知数据追加器
 *
 * 当管理员将玩家加入频道封禁名单时，该结构体负责向数据包追加被封禁玩家和执行封禁操作的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知有人被加入封禁名单。
 *
 * 通知类型: CHAT_PLAYER_BANNED_NOTICE
 * 消息体内容:
 *   - 被封禁玩家 GUID (uint64): 被加入封禁名单的玩家 GUID
 *   - 执行封禁的管理员 GUID (uint64): 执行封禁操作的管理员 GUID
 *
 * 使用场景：管理员将玩家加入频道封禁名单后广播给所有频道成员
 */
struct PlayerBannedAppend
{
    /**
     * @brief 构造函数
     * @param moderator 执行封禁操作的管理员 GUID
     * @param banned 被封禁的玩家 GUID
     */
    explicit PlayerBannedAppend(ObjectGuid const& moderator, ObjectGuid const& banned) : _moderator(moderator), _banned(banned) { }

    static uint8 const NotificationType = CHAT_PLAYER_BANNED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     *
     * 注意：数据包中先写入被封禁玩家的 GUID，再写入执行封禁的管理员 GUID
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_banned);
        data << uint64(_moderator);
    }

private:
    ObjectGuid _moderator;  ///< 执行封禁操作的管理员 GUID
    ObjectGuid _banned;      ///< 被封禁的玩家 GUID
};

/**
 * @struct PlayerUnbannedAppend
 * @brief 玩家被移出封禁名单通知数据追加器
 *
 * 当管理员将玩家从频道封禁名单中移除时，该结构体负责向数据包追加被解封玩家和执行解封操作的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知有人被从封禁名单中移除。
 *
 * 通知类型: CHAT_PLAYER_UNBANNED_NOTICE
 * 消息体内容:
 *   - 被解封玩家 GUID (uint64): 被移出封禁名单的玩家 GUID
 *   - 执行解封的管理员 GUID (uint64): 执行解封操作的管理员 GUID
 *
 * 使用场景：管理员将玩家从频道封禁名单中移除后广播给所有频道成员
 */
struct PlayerUnbannedAppend
{
    /**
     * @brief 构造函数
     * @param moderator 执行解封操作的管理员 GUID
     * @param unbanned 被解封的玩家 GUID
     */
    explicit PlayerUnbannedAppend(ObjectGuid const& moderator, ObjectGuid const& unbanned) : _moderator(moderator), _unbanned(unbanned) { }

    static uint8 const NotificationType = CHAT_PLAYER_UNBANNED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     *
     * 注意：数据包中先写入被解封玩家的 GUID，再写入执行解封的管理员 GUID
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_unbanned);
        data << uint64(_moderator);
    }

private:
    ObjectGuid _moderator;   ///< 执行解封操作的管理员 GUID
    ObjectGuid _unbanned;    ///< 被解封的玩家 GUID
};

/**
 * @struct PlayerNotBannedAppend
 * @brief 玩家未被封禁通知数据追加器
 *
 * 当管理员尝试解封一个未在封禁名单中的玩家时，该结构体负责向数据包追加该玩家的名称。
 * 这表示该玩家本来就不在频道的封禁名单中。
 *
 * 通知类型: CHAT_PLAYER_NOT_BANNED_NOTICE
 * 消息体内容: 玩家名称 (string)
 *
 * 使用场景：管理员尝试解封未被封禁的玩家时发送给操作者
 */
struct PlayerNotBannedAppend
{
    /**
     * @brief 构造函数
     * @param playerName 未被封禁的玩家名称
     */
    explicit PlayerNotBannedAppend(std::string const& playerName) : _playerName(playerName) { }

    static uint8 const NotificationType = CHAT_PLAYER_NOT_BANNED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << _playerName;
    }

private:
    std::string _playerName;  ///< 未被封禁的玩家名称
};

/**
 * @struct PlayerAlreadyMemberAppend
 * @brief 玩家已是频道成员通知数据追加器
 *
 * 当尝试邀请已经在频道中的玩家时，该结构体负责向数据包追加该玩家的 GUID。
 * 这表示该玩家已经是频道成员，无需重复邀请。
 *
 * 通知类型: CHAT_PLAYER_ALREADY_MEMBER_NOTICE
 * 消息体内容: 已是成员的玩家 GUID (uint64)
 *
 * 使用场景：尝试邀请已在频道中的玩家时发送给邀请者
 */
struct PlayerAlreadyMemberAppend
{
    /**
     * @brief 构造函数
     * @param guid 已是频道成员的玩家 GUID
     */
    explicit PlayerAlreadyMemberAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_PLAYER_ALREADY_MEMBER_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 已是频道成员的玩家 GUID
};

/**
 * @struct InviteAppend
 * @brief 频道邀请通知数据追加器
 *
 * 当玩家被邀请加入频道时，该结构体负责向数据包追加邀请者的 GUID。
 * 该通知会发送给被邀请的玩家，告知其有人邀请加入频道。
 *
 * 通知类型: CHAT_INVITE_NOTICE
 * 消息体内容: 邀请者的 GUID (uint64)
 *
 * 使用场景：频道成员邀请其他玩家加入频道时发送给被邀请者
 */
struct InviteAppend
{
    /**
     * @brief 构造函数
     * @param guid 邀请者的 GUID
     */
    explicit InviteAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_INVITE_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 邀请者的 GUID
};

/**
 * @struct InviteWrongFactionAppend
 * @brief 邀请错误阵营玩家通知数据追加器
 *
 * 当尝试邀请对立阵营的玩家加入阵营限制频道时，该结构体负责构建阵营错误通知。
 * 某些频道限制了阵营，只能邀请相同阵营的玩家。
 *
 * 通知类型: CHAT_INVITE_WRONG_FACTION_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：尝试邀请对立阵营玩家加入阵营限制频道时发送给邀请者
 */
struct InviteWrongFactionAppend
{
    static uint8 const NotificationType = CHAT_INVITE_WRONG_FACTION_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct WrongFactionAppend
 * @brief 阵营错误通知数据追加器
 *
 * 当对立阵营的玩家尝试加入阵营限制频道时，该结构体负责构建阵营错误通知。
 * 某些频道限制了阵营，只允许特定阵营的玩家加入。
 *
 * 通知类型: CHAT_WRONG_FACTION_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：对立阵营玩家尝试加入阵营限制频道时发送给该玩家
 */
struct WrongFactionAppend
{
    static uint8 const NotificationType = CHAT_WRONG_FACTION_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct InvalidNameAppend
 * @brief 频道名称无效通知数据追加器
 *
 * 当玩家尝试创建或加入名称无效的频道时，该结构体负责构建名称无效通知。
 * 频道名称可能因为包含非法字符、过长或过短等原因被判定为无效。
 *
 * 通知类型: CHAT_INVALID_NAME_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家输入无效的频道名称时发送给该玩家
 */
struct InvalidNameAppend
{
    static uint8 const NotificationType = CHAT_INVALID_NAME_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct NotModeratedAppend
 * @brief 频道未开启管理模式通知数据追加器
 *
 * 当玩家尝试在非管理模式频道中执行仅限管理模式的操作时，
 * 该结构体负责构建频道未开启管理模式的错误通知。
 *
 * 通知类型: CHAT_NOT_MODERATED_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家在非管理模式频道中尝试执行需要管理模式权限的操作时发送
 */
struct NotModeratedAppend
{
    static uint8 const NotificationType = CHAT_NOT_MODERATED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct PlayerInvitedAppend
 * @brief 玩家已收到邀请通知数据追加器
 *
 * 当玩家成功邀请其他玩家加入频道后，该结构体负责向数据包追加被邀请玩家的名称。
 * 该通知会发送给邀请者，确认邀请已成功发送。
 *
 * 通知类型: CHAT_PLAYER_INVITED_NOTICE
 * 消息体内容: 被邀请玩家的名称 (string)
 *
 * 使用场景：玩家成功邀请其他玩家加入频道后发送给邀请者确认
 */
struct PlayerInvitedAppend
{
    /**
     * @brief 构造函数
     * @param playerName 被邀请玩家的名称
     */
    explicit PlayerInvitedAppend(std::string const& playerName) : _playerName(playerName) { }

    static uint8 const NotificationType = CHAT_PLAYER_INVITED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << _playerName;
    }

private:
    std::string _playerName;  ///< 被邀请玩家的名称
};

/**
 * @struct PlayerInviteBannedAppend
 * @brief 邀请被封禁玩家通知数据追加器
 *
 * 当玩家尝试邀请在频道封禁名单中的玩家时，该结构体负责向数据包追加被邀请玩家的名称。
 * 这表示被邀请的玩家无法加入频道，因为其已在封禁名单中。
 *
 * 通知类型: CHAT_PLAYER_INVITE_BANNED_NOTICE
 * 消息体内容: 被邀请玩家的名称 (string)
 *
 * 使用场景：尝试邀请在频道封禁名单中的玩家时发送给邀请者
 */
struct PlayerInviteBannedAppend
{
    /**
     * @brief 构造函数
     * @param playerName 被邀请但已在封禁名单中的玩家名称
     */
    explicit PlayerInviteBannedAppend(std::string const& playerName) : _playerName(playerName) { }

    static uint8 const NotificationType = CHAT_PLAYER_INVITE_BANNED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << _playerName;
    }

private:
    std::string _playerName;  ///< 被邀请但已在封禁名单中的玩家名称
};

/**
 * @struct ThrottledAppend
 * @brief 频道操作限流通知数据追加器
 *
 * 当玩家在短时间内执行过多频道操作时，该结构体负责构建限流通知。
 * 为了防止滥用，服务器会对频道操作频率进行限制。
 *
 * 通知类型: CHAT_THROTTLED_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家操作过于频繁触发限流机制时发送给该玩家
 */
struct ThrottledAppend
{
    static uint8 const NotificationType = CHAT_THROTTLED_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct NotInAreaAppend
 * @brief 不在区域范围内通知数据追加器
 *
 * 当玩家尝试加入区域限制频道但不在指定区域时，该结构体负责构建区域错误通知。
 * 某些频道（如地区频道）只允许特定区域的玩家加入。
 *
 * 通知类型: CHAT_NOT_IN_AREA_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家不在指定区域尝试加入区域限制频道时发送
 */
struct NotInAreaAppend
{
    static uint8 const NotificationType = CHAT_NOT_IN_AREA_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct NotInLFGAppend
 * @brief 不在随机副本队列通知数据追加器
 *
 * 当玩家尝试加入随机副本频道但不在随机副本队列中时，该结构体负责构建错误通知。
 * 随机副本频道只允许正在排队或已进入随机副本的玩家加入。
 *
 * 通知类型: CHAT_NOT_IN_LFG_NOTICE
 * 消息体内容: 无额外数据
 *
 * 使用场景：玩家不在随机副本队列尝试加入随机副本频道时发送
 */
struct NotInLFGAppend
{
    static uint8 const NotificationType = CHAT_NOT_IN_LFG_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象（未使用）
     *
     * 该通知不需要额外的消息体数据，因此函数体为空
     */
    void Append(WorldPacket& /*data*/) const { }
};

/**
 * @struct VoiceOnAppend
 * @brief 开启语音功能通知数据追加器
 *
 * 当频道开启语音功能时，该结构体负责向数据包追加开启语音的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知频道已启用语音功能。
 *
 * 通知类型: CHAT_VOICE_ON_NOTICE
 * 消息体内容: 开启语音功能的管理员 GUID (uint64)
 *
 * 使用场景：频道管理员开启频道语音功能后广播给所有频道成员
 */
struct VoiceOnAppend
{
    /**
     * @brief 构造函数
     * @param guid 开启语音功能的管理员 GUID
     */
    explicit VoiceOnAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_VOICE_ON_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 开启语音功能的管理员 GUID
};

/**
 * @struct VoiceOffAppend
 * @brief 关闭语音功能通知数据追加器
 *
 * 当频道关闭语音功能时，该结构体负责向数据包追加关闭语音的管理员 GUID。
 * 该通知会广播给频道内的所有成员，告知频道已禁用语音功能。
 *
 * 通知类型: CHAT_VOICE_OFF_NOTICE
 * 消息体内容: 关闭语音功能的管理员 GUID (uint64)
 *
 * 使用场景：频道管理员关闭频道语音功能后广播给所有频道成员
 */
struct VoiceOffAppend
{
    /**
     * @brief 构造函数
     * @param guid 关闭语音功能的管理员 GUID
     */
    explicit VoiceOffAppend(ObjectGuid const& guid) : _guid(guid) { }

    static uint8 const NotificationType = CHAT_VOICE_OFF_NOTICE;  ///< 通知类型常量

    /**
     * @brief 追加消息体数据到数据包
     * @param data [out] 要追加数据的 WorldPacket 对象
     */
    void Append(WorldPacket& data) const
    {
        data << uint64(_guid);
    }

private:
    ObjectGuid _guid;  ///< 关闭语音功能的管理员 GUID
};

#endif // _CHANNELAPPENDERS_H
