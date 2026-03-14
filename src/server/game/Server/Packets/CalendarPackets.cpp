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
 * @file CalendarPackets.cpp
 * @brief 日历系统网络数据包实现
 *
 * 本文件实现了日历系统相关的所有网络数据包的序列化和反序列化方法。
 * 主要功能包括:
 * - 客户端数据包的读取(Read方法)
 * - 服务器数据包的写入(Write方法)
 * - 数据结构的序列化运算符重载
 *
 * 数据包读写流程:
 * 1. 客户端发送请求 -> 服务器接收并Read() -> 处理逻辑
 * 2. 服务器准备响应 -> Write()并发送 -> 客户端接收并显示
 */

#include "CalendarPackets.h"

/**
 * @brief 序列化事件信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param eventInfo 事件信息结构
 * @return 返回字节缓冲区引用
 *
 * 将CalendarSendCalendarEventInfo结构序列化为网络传输格式。
 * 字段顺序必须与客户端解析顺序一致。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarSendCalendarEventInfo const& eventInfo)
{
    data << uint64(eventInfo.EventID);           // 写入事件ID
    data << eventInfo.EventName;                  // 写入事件名称
    data << uint32(eventInfo.EventType);          // 写入事件类型
    data << eventInfo.Date;                       // 写入事件日期
    data << uint32(eventInfo.Flags);              // 写入事件标志
    data << int32(eventInfo.TextureID);           // 写入图标ID
    data << eventInfo.OwnerGuid.WriteAsPacked();  // 写入创建者GUID(压缩格式)

    return data;
}

/**
 * @brief 序列化团队副本锁定信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param lockoutInfo 副本锁定信息结构
 * @return 返回字节缓冲区引用
 *
 * 将团队副本锁定信息序列化,用于在日历中显示副本进度。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarSendCalendarRaidLockoutInfo const& lockoutInfo)
{
    data << int32(lockoutInfo.MapID);            // 写入地图ID
    data << uint32(lockoutInfo.DifficultyID);    // 写入难度ID
    data << int32(lockoutInfo.ExpireTime);       // 写入过期时间
    data << uint64(lockoutInfo.InstanceID);      // 写入实例ID

    return data;
}

/**
 * @brief 序列化邀请信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param inviteInfo 邀请信息结构
 * @return 返回字节缓冲区引用
 *
 * 将邀请概要信息序列化,用于完整日历数据包。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarSendCalendarInviteInfo const& inviteInfo)
{
    data << uint64(inviteInfo.EventID);              // 写入事件ID
    data << uint64(inviteInfo.InviteID);             // 写入邀请ID
    data << uint8(inviteInfo.Status);                // 写入邀请状态
    data << uint8(inviteInfo.Moderator);             // 写入管理员标志
    data << uint8(inviteInfo.InviteType);            // 写入邀请类型
    data << inviteInfo.InviterGuid.WriteAsPacked();  // 写入邀请者GUID(压缩格式)

    return data;
}

/**
 * @brief 序列化节假日信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param holidayInfo 节假日信息结构
 * @return 返回字节缓冲区引用
 *
 * 将节假日活动信息序列化,包含多个日期、持续时间和标志数组。
 * 性能注意:此方法会写入多个数组,数据量较大。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarSendCalendarHolidayInfo const& holidayInfo)
{
    data << uint32(holidayInfo.HolidayID);      // 写入节假日ID
    data << uint32(holidayInfo.Region);         // 写入地区代码
    data << uint32(holidayInfo.Looping);        // 写入是否循环标志
    data << uint32(holidayInfo.Priority);       // 写入优先级
    data << uint32(holidayInfo.FilterType);     // 写入筛选类型

    // 写入所有活动日期
    for (uint8 j = 0; j < MAX_HOLIDAY_DATES; ++j)
        data << holidayInfo.Date[j];

    // 写入持续时间数组和标志数组(直接追加字节)
    data.append(holidayInfo.Duration.data(), holidayInfo.Duration.size());
    data.append(holidayInfo.CalendarFlags.data(), holidayInfo.CalendarFlags.size());

    data << holidayInfo.TextureFilename;        // 写入图标文件名

    return data;
}

/**
 * @brief 序列化团队副本重置信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param raidResetInfo 副本重置信息结构
 * @return 返回字节缓冲区引用
 *
 * 将团队副本每周重置时间信息序列化。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarSendCalendarRaidResetInfo const& raidResetInfo)
{
    data << int32(raidResetInfo.MapID);     // 写入地图ID
    data << int32(raidResetInfo.Duration);  // 写入重置周期
    data << int32(raidResetInfo.Offset);    // 写入偏移时间

    return data;
}

/**
 * @brief 序列化事件邀请详细信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param inviteInfo 邀请详细信息结构
 * @return 返回字节缓冲区引用
 *
 * 将单个邀请的完整信息序列化,用于事件详情数据包。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Calendar::CalendarEventInviteInfo const& inviteInfo)
{
    data << inviteInfo.Guid.WriteAsPacked();    // 写入被邀请者GUID(压缩格式)
    data << uint8(inviteInfo.Level);            // 写入等级
    data << uint8(inviteInfo.Status);           // 写入状态
    data << uint8(inviteInfo.Moderator);        // 写入管理员标志
    data << uint8(inviteInfo.InviteType);       // 写入邀请类型
    data << uint64(inviteInfo.InviteID);        // 写入邀请ID
    data << inviteInfo.ResponseTime;            // 写入响应时间
    data << inviteInfo.Notes;                   // 写入备注信息

    return data;
}

/**
 * @brief 读取获取事件详情请求数据包
 *
 * 从数据包中读取事件ID,用于查询该事件的详细信息。
 */
void WorldPackets::Calendar::CalendarGetEvent::Read()
{
    _worldPacket >> EventID;  // 读取要查询的事件ID
}

/**
 * @brief 读取公会筛选条件数据包
 *
 * 从数据包中读取公会成员筛选条件,包括等级范围和职位限制。
 */
void WorldPackets::Calendar::CalendarGuildFilter::Read()
{
    _worldPacket >> MinLevel;       // 读取最小等级
    _worldPacket >> MaxLevel;       // 读取最大等级
    _worldPacket >> MaxRankOrder;   // 读取公会职位上限
}

/**
 * @brief 读取竞技场队伍请求数据包
 *
 * 从数据包中读取竞技场队伍ID,用于获取队伍成员列表。
 */
void WorldPackets::Calendar::CalendarArenaTeam::Read()
{
    _worldPacket >> ArenaTeamId;  // 读取竞技场队伍ID
}

/**
 * @brief 从字节缓冲区反序列化邀请信息
 * @param buffer 字节缓冲区引用
 * @param invite 邀请信息结构引用
 * @return 返回字节缓冲区引用
 *
 * 从数据包中读取创建事件时的邀请信息,包括被邀请者GUID、状态和管理员标志。
 */
ByteBuffer& operator>>(ByteBuffer& buffer, WorldPackets::Calendar::CalendarAddEventInviteInfo& invite)
{
    buffer >> invite.Guid.ReadAsPacked();  // 读取被邀请者GUID(压缩格式)
    buffer >> invite.Status;                // 读取邀请状态
    buffer >> invite.Moderator;             // 读取管理员标志

    return buffer;
}

/**
 * @brief 读取创建事件数据包
 *
 * 从数据包中读取新事件的完整信息,包括标题、描述、时间、图标等属性,
 * 以及初始邀请列表。客户端会在创建事件界面填写完成后发送此数据包。
 */
void WorldPackets::Calendar::CalendarAddEvent::Read()
{
    _worldPacket >> Title;                        // 读取事件标题
    _worldPacket >> Description;                  // 读取事件描述
    _worldPacket >> EventType;                    // 读取事件类型
    _worldPacket.read_skip<uint8>();              // 跳过重复标志(未使用)
    _worldPacket >> MaxSize;                      // 读取最大参与人数
    _worldPacket >> TextureID;                    // 读取图标ID
    _worldPacket >> Time;                         // 读取事件时间
    _worldPacket >> LockDate;                     // 读取锁定时间
    _worldPacket >> Flags;                        // 读取事件标志

    // 读取邀请列表
    Invites.resize(_worldPacket.read<uint32>());  // 读取邀请数量并调整数组大小
    for (CalendarAddEventInviteInfo& invite : Invites)
        _worldPacket >> invite;                   // 逐个读取邀请信息
}

/**
 * @brief 读取更新事件数据包
 *
 * 从数据包中读取事件更新信息。只有事件创建者或管理员可以发送此数据包。
 */
void WorldPackets::Calendar::CalendarUpdateEvent::Read()
{
    _worldPacket >> EventID;                      // 读取要更新的事件ID
    _worldPacket >> ModeratorID;                  // 读取执行更新的管理员ID
    _worldPacket >> Title;                        // 读取更新后的标题
    _worldPacket >> Description;                  // 读取更新后的描述
    _worldPacket >> EventType;                    // 读取更新后的类型
    _worldPacket.read_skip<uint8>();              // 跳过重复标志(未使用)
    _worldPacket >> MaxSize;                      // 读取更新后的最大人数
    _worldPacket >> TextureID;                    // 读取更新后的图标ID
    _worldPacket >> Time;                         // 读取更新后的时间
    _worldPacket >> LockDate;                     // 读取更新后的锁定时间
    _worldPacket >> Flags;                        // 读取更新后的标志
}

/**
 * @brief 读取删除事件数据包
 *
 * 从数据包中读取事件删除请求。只有事件创建者或管理员可以删除事件。
 */
void WorldPackets::Calendar::CalendarRemoveEvent::Read()
{
    _worldPacket >> EventID;       // 读取要删除的事件ID
    _worldPacket >> ModeratorID;   // 读取执行删除的管理员ID
    _worldPacket >> IsSignUp;      // 读取是否为报名事件
}

/**
 * @brief 读取复制事件数据包
 *
 * 从数据包中读取事件复制请求,包括源事件ID和目标日期。
 */
void WorldPackets::Calendar::CalendarCopyEvent::Read()
{
    _worldPacket >> EventID;       // 读取源事件ID
    _worldPacket >> ModeratorID;   // 读取执行复制的管理员ID
    _worldPacket >> Date;          // 读取复制到的新日期
}

/**
 * @brief 读取事件响应数据包
 *
 * 从数据包中读取玩家对邀请的响应,包括接受、拒绝或暂定。
 */
void WorldPackets::Calendar::CalendarRSVP::Read()
{
    _worldPacket >> EventID;    // 读取事件ID
    _worldPacket >> InviteID;   // 读取邀请ID
    _worldPacket >> Status;     // 读取响应状态
}

/**
 * @brief 读取邀请玩家数据包
 *
 * 从数据包中读取邀请信息,用于向事件添加新的被邀请者。
 */
void WorldPackets::Calendar::CalendarInvite::Read()
{
    _worldPacket >> EventID;       // 读取事件ID
    _worldPacket >> ModeratorID;   // 读取执行邀请的管理员ID
    _worldPacket >> Name;          // 读取被邀请者名字
    _worldPacket >> Creating;      // 读取是否正在创建事件
    _worldPacket >> IsSignUp;      // 读取是否为报名事件
}

/**
 * @brief 读取事件报名数据包
 *
 * 从数据包中读取事件报名请求,用于公开事件的自主报名。
 */
void WorldPackets::Calendar::CalendarEventSignUp::Read()
{
    _worldPacket >> EventID;     // 读取要报名的事件ID
    _worldPacket >> Tentative;   // 读取是否为暂定报名
}

/**
 * @brief 读取移除邀请数据包
 *
 * 从数据包中读取移除邀请请求,只有管理员可以移除其他玩家的邀请。
 */
void WorldPackets::Calendar::CalendarRemoveInvite::Read()
{
    _worldPacket >> Guid.ReadAsPacked();  // 读取要移除的玩家GUID(压缩格式)
    _worldPacket >> InviteID;              // 读取要移除的邀请ID
    _worldPacket >> ModeratorID;           // 读取执行移除的管理员ID
    _worldPacket >> EventID;               // 读取事件ID
}

/**
 * @brief 读取状态更新数据包
 *
 * 从数据包中读取邀请状态更新请求,允许玩家更改自己的响应状态。
 */
void WorldPackets::Calendar::CalendarStatus::Read()
{
    _worldPacket >> Guid.ReadAsPacked();  // 读取玩家GUID(压缩格式)
    _worldPacket >> EventID;               // 读取事件ID
    _worldPacket >> InviteID;              // 读取邀请ID
    _worldPacket >> ModeratorID;           // 读取管理员ID
    _worldPacket >> Status;                // 读取新状态
}

/**
 * @brief 读取设置副本延期数据包
 *
 * 从数据包中读取副本锁定延期设置,允许玩家延长副本进度保留时间。
 */
void WorldPackets::Calendar::SetSavedInstanceExtend::Read()
{
    _worldPacket >> MapID;          // 读取地图ID
    _worldPacket >> DifficultyID;   // 读取难度ID
    _worldPacket >> Extend;         // 读取是否延期
}

/**
 * @brief 读取管理员状态查询数据包
 *
 * 从数据包中读取管理员权限变更请求,只有事件创建者可以更改管理员权限。
 */
void WorldPackets::Calendar::CalendarModeratorStatusQuery::Read()
{
    _worldPacket >> Guid.ReadAsPacked();  // 读取被变更权限的玩家GUID(压缩格式)
    _worldPacket >> EventID;               // 读取事件ID
    _worldPacket >> InviteID;              // 读取邀请ID
    _worldPacket >> ModeratorID;           // 读取执行操作的管理员ID
    _worldPacket >> Status;                // 读取新的管理员状态
}

/**
 * @brief 写入邀请添加通知数据包
 * @return 返回写入完成的数据包指针
 *
 * 将新邀请信息写入数据包,根据邀请类型决定是否包含响应时间。
 * Type为1表示已有响应,需要写入响应时间。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteAdded::Write()
{
    _worldPacket << InviteGuid.WriteAsPacked();  // 写入被邀请者GUID(压缩格式)
    _worldPacket << uint64(EventID);              // 写入事件ID
    _worldPacket << uint64(InviteID);             // 写入邀请ID
    _worldPacket << uint8(Level);                 // 写入被邀请者等级
    _worldPacket << uint8(Status);                // 写入邀请状态
    _worldPacket << uint8(Type);                  // 写入邀请类型

    // 如果Type为1,表示邀请已有响应,需要写入响应时间
    if (Type == 1)
        _worldPacket << ResponseTime;

    _worldPacket << uint8(ClearPending);          // 写入清除待处理标志

    return &_worldPacket;
}

/**
 * @brief 写入完整日历数据包
 * @return 返回写入完成的数据包指针
 *
 * 将所有日历信息写入数据包,包括邀请、事件、副本锁定、重置和节假日。
 * 这是日历系统中数据量最大的数据包。
 *
 * 性能注意:
 * - 此方法会遍历多个列表并写入大量数据
 * - 避免频繁发送此数据包
 * - 考虑使用增量更新机制
 */
WorldPacket const* WorldPackets::Calendar::CalendarSendCalendar::Write()
{
    // 写入邀请列表
    _worldPacket << uint32(Invites.size());
    for (CalendarSendCalendarInviteInfo const& invite : Invites)
        _worldPacket << invite;

    // 写入事件列表
    _worldPacket << uint32(Events.size());
    for (CalendarSendCalendarEventInfo const& event : Events)
        _worldPacket << event;

    // 写入服务器时间信息
    _worldPacket << uint32(ServerNow);
    _worldPacket << ServerTime;

    // 写入团队副本锁定列表
    _worldPacket << uint32(RaidLockouts.size());
    for (CalendarSendCalendarRaidLockoutInfo const& lockout : RaidLockouts)
        _worldPacket << lockout;

    _worldPacket << uint32(RaidOrigin);

    // 写入团队副本重置列表
    _worldPacket << uint32(RaidResets.size());
    for (CalendarSendCalendarRaidResetInfo const& reset : RaidResets)
        _worldPacket << reset;

    // 写入节假日列表
    _worldPacket << uint32(Holidays.size());
    for (CalendarSendCalendarHolidayInfo const& holiday : Holidays)
        _worldPacket << holiday;

    return &_worldPacket;
}

/**
 * @brief 写入事件详情数据包
 * @return 返回写入完成的数据包指针
 *
 * 将单个事件的完整信息写入数据包,包括事件属性和所有邀请详情。
 */
WorldPacket const* WorldPackets::Calendar::CalendarSendEvent::Write()
{
    _worldPacket << uint8(EventType);                          // 写入事件类型
    _worldPacket << OwnerGuid.WriteAsPacked();                 // 写入创建者GUID(压缩格式)
    _worldPacket << uint64(EventID);                           // 写入事件ID
    _worldPacket << EventName;                                 // 写入事件名称
    _worldPacket << Description;                               // 写入事件描述
    _worldPacket << uint8(GetEventType);                       // 写入获取事件类型
    _worldPacket << uint8(CALENDAR_REPEAT_NEVER);              // 写入重复类型(永不重复)
    _worldPacket << uint32(CALENDAR_MAX_INVITES);              // 写入最大邀请数
    _worldPacket << int32(TextureID);                          // 写入图标ID
    _worldPacket << uint32(Flags);                             // 写入事件标志
    _worldPacket << Date;                                      // 写入事件日期
    _worldPacket << LockDate;                                  // 写入锁定日期
    _worldPacket << uint32(EventGuildID);                      // 写入公会ID

    // 写入邀请列表
    _worldPacket << uint32(Invites.size());
    for (CalendarEventInviteInfo const& invite : Invites)
        _worldPacket << invite;

    return &_worldPacket;
}

/**
 * @brief 写入邀请警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请通知信息写入数据包,用于向被邀请者显示邀请弹窗。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteAlert::Write()
{
    _worldPacket << uint64(EventID);                 // 写入事件ID
    _worldPacket << EventName;                       // 写入事件名称
    _worldPacket << Date;                            // 写入事件日期
    _worldPacket << uint32(Flags);                   // 写入事件标志
    _worldPacket << uint32(EventType);               // 写入事件类型
    _worldPacket << int32(TextureID);                // 写入图标ID
    _worldPacket << uint64(InviteID);                // 写入邀请ID
    _worldPacket << uint8(Status);                   // 写入邀请状态
    _worldPacket << uint8(ModeratorStatus);          // 写入管理员状态
    _worldPacket << OwnerGuid.WriteAsPacked();       // 写入创建者GUID(压缩格式)
    _worldPacket << InvitedByGuid.WriteAsPacked();   // 写入邀请者GUID(压缩格式)

    return &_worldPacket;
}

/**
 * @brief 写入邀请状态变更数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请状态更新信息写入数据包,用于通知事件管理员邀请状态变化。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteStatus::Write()
{
    _worldPacket << InviteGuid.WriteAsPacked();  // 写入邀请者GUID(压缩格式)
    _worldPacket << uint64(EventID);              // 写入事件ID
    _worldPacket << Date;                         // 写入事件日期
    _worldPacket << uint32(Flags);                // 写入事件标志
    _worldPacket << uint8(Status);                // 写入新状态
    _worldPacket << uint8(ClearPending);          // 写入清除待处理标志
    _worldPacket << ResponseTime;                 // 写入响应时间

    return &_worldPacket;
}

/**
 * @brief 写入邀请移除数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请移除通知写入数据包,用于通知相关玩家邀请已被移除。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteRemoved::Write()
{
    _worldPacket << InviteGuid.WriteAsPacked();  // 写入被移除邀请的玩家GUID(压缩格式)
    _worldPacket << uint64(EventID);              // 写入事件ID
    _worldPacket << uint32(Flags);                // 写入事件标志
    _worldPacket << uint8(ClearPending);          // 写入清除待处理标志

    return &_worldPacket;
}

/**
 * @brief 写入管理员状态数据包
 * @return 返回写入完成的数据包指针
 *
 * 将管理员权限变更通知写入数据包,用于通知玩家其管理员权限已变更。
 */
WorldPacket const* WorldPackets::Calendar::CalendarModeratorStatus::Write()
{
    _worldPacket << InviteGuid.WriteAsPacked();  // 写入被变更权限的玩家GUID(压缩格式)
    _worldPacket << uint64(EventID);              // 写入事件ID
    _worldPacket << uint8(Status);                // 写入新的管理员状态
    _worldPacket << uint8(ClearPending);          // 写入清除待处理标志

    return &_worldPacket;
}

/**
 * @brief 写入邀请移除警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请移除警报写入数据包,用于通知被移除的玩家。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteRemovedAlert::Write()
{
    _worldPacket << uint64(EventID);    // 写入事件ID
    _worldPacket << Date;               // 写入事件日期
    _worldPacket << uint32(Flags);      // 写入事件标志
    _worldPacket << uint8(Status);      // 写入邀请状态

    return &_worldPacket;
}

/**
 * @brief 写入事件更新警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将事件更新通知写入数据包,用于通知所有被邀请者事件已更新。
 * 包含原始日期和更新后的日期,便于客户端显示变更。
 */
WorldPacket const* WorldPackets::Calendar::CalendarEventUpdatedAlert::Write()
{
    _worldPacket << uint8(ClearPending);            // 写入清除待处理标志
    _worldPacket << uint64(EventID);                // 写入事件ID
    _worldPacket << OriginalDate;                   // 写入原始日期(用于显示变更)
    _worldPacket << uint32(Flags);                  // 写入事件标志
    _worldPacket << Date;                           // 写入更新后的日期
    _worldPacket << uint8(EventType);               // 写入事件类型
    _worldPacket << uint32(TextureID);              // 写入图标ID
    _worldPacket << EventName;                      // 写入事件名称
    _worldPacket << Description;                    // 写入事件描述
    _worldPacket << uint8(CALENDAR_REPEAT_NEVER);   // 写入重复类型(永不重复)
    _worldPacket << uint32(CALENDAR_MAX_INVITES);   // 写入最大邀请数
    _worldPacket << LockDate;                       // 写入锁定日期

    return &_worldPacket;
}

/**
 * @brief 写入事件删除警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将事件删除通知写入数据包,用于通知所有被邀请者事件已取消。
 */
WorldPacket const* WorldPackets::Calendar::CalendarEventRemovedAlert::Write()
{
    _worldPacket << uint8(ClearPending);  // 写入清除待处理标志
    _worldPacket << uint64(EventID);      // 写入被删除的事件ID
    _worldPacket << Date;                 // 写入事件日期

    return &_worldPacket;
}

/**
 * @brief 写入待处理邀请数量数据包
 * @return 返回写入完成的数据包指针
 *
 * 将待处理邀请数量写入数据包,用于更新客户端小地图图标上的提示数字。
 */
WorldPacket const* WorldPackets::Calendar::CalendarSendNumPending::Write()
{
    _worldPacket << uint32(NumPending);  // 写入待处理邀请数量
    return &_worldPacket;
}

/**
 * @brief 写入命令结果数据包
 * @return 返回写入完成的数据包指针
 *
 * 将命令执行结果写入数据包,用于通知客户端操作失败的原因。
 * 格式:命令类型, 空字符串, 玩家名字, 结果代码
 */
WorldPacket const* WorldPackets::Calendar::CalendarCommandResult::Write()
{
    _worldPacket << uint32(Command);  // 写入命令类型
    _worldPacket << "";               // 写入空字符串(预留字段)
    _worldPacket << Name;             // 写入相关玩家名字
    _worldPacket << uint32(Result);   // 写入结果代码(错误码)

    return &_worldPacket;
}

/**
 * @brief 写入团队副本锁定添加数据包
 * @return 返回写入完成的数据包指针
 *
 * 将新副本锁定信息写入数据包,用于在日历中显示副本进度。
 */
WorldPacket const* WorldPackets::Calendar::CalendarRaidLockoutAdded::Write()
{
    _worldPacket << ServerTime;            // 写入服务器时间
    _worldPacket << int32(MapID);          // 写入地图ID
    _worldPacket << uint32(DifficultyID);  // 写入难度ID
    _worldPacket << int32(TimeRemaining);  // 写入剩余时间
    _worldPacket << uint64(InstanceID);    // 写入实例ID

    return &_worldPacket;
}

/**
 * @brief 写入团队副本锁定移除数据包
 * @return 返回写入完成的数据包指针
 *
 * 将副本锁定移除信息写入数据包,用于通知客户端更新日历显示。
 */
WorldPacket const* WorldPackets::Calendar::CalendarRaidLockoutRemoved::Write()
{
    _worldPacket << int32(MapID);          // 写入地图ID
    _worldPacket << uint32(DifficultyID);  // 写入难度ID
    _worldPacket << int32(TimeRemaining);  // 写入剩余时间
    _worldPacket << uint64(InstanceID);    // 写入实例ID

    return &_worldPacket;
}

/**
 * @brief 写入团队副本锁定更新数据包
 * @return 返回写入完成的数据包指针
 *
 * 将副本锁定时间更新信息写入数据包,用于通知延期等操作。
 */
WorldPacket const* WorldPackets::Calendar::CalendarRaidLockoutUpdated::Write()
{
    _worldPacket << ServerTime;              // 写入服务器时间
    _worldPacket << int32(MapID);            // 写入地图ID
    _worldPacket << uint32(DifficultyID);    // 写入难度ID
    _worldPacket << int32(OldTimeRemaining); // 写入旧的剩余时间
    _worldPacket << int32(NewTimeRemaining); // 写入新的剩余时间

    return &_worldPacket;
}

/**
 * @brief 写入初始邀请列表数据包
 * @return 返回写入完成的数据包指针
 *
 * 将公会或竞技场队伍成员列表写入数据包,用于邀请选择界面。
 * 每个成员包含GUID和等级信息。
 */
WorldPacket const* WorldPackets::Calendar::CalendarEventInitialInvites::Write()
{
    _worldPacket << uint32(Invites.size());  // 写入成员数量

    // 遍历写入每个成员的信息
    for (CalendarEventInitialInviteInfo const& invite : Invites)
    {
        _worldPacket << invite.InviteGuid.WriteAsPacked();  // 写入成员GUID(压缩格式)
        _worldPacket << uint8(invite.Level);                // 写入成员等级
    }

    return &_worldPacket;
}

/**
 * @brief 写入邀请状态警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请状态警报信息写入数据包,用于特别通知状态变更。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteStatusAlert::Write()
{
    _worldPacket << uint64(EventID);    // 写入事件ID
    _worldPacket << Date;               // 写入事件日期
    _worldPacket << uint32(Flags);      // 写入事件标志
    _worldPacket << uint8(Status);      // 写入邀请状态

    return &_worldPacket;
}

/**
 * @brief 写入邀请备注警报数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请备注信息写入数据包,用于显示或更新备注内容。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteNotesAlert::Write()
{
    _worldPacket << uint64(EventID);  // 写入事件ID
    _worldPacket << Notes;            // 写入备注内容

    return &_worldPacket;
}

/**
 * @brief 写入邀请备注数据包
 * @return 返回写入完成的数据包指针
 *
 * 将邀请备注广播信息写入数据包,用于向所有相关玩家同步备注变更。
 */
WorldPacket const* WorldPackets::Calendar::CalendarInviteNotes::Write()
{
    _worldPacket << InviteGuid.WriteAsPacked();  // 写入邀请者GUID(压缩格式)
    _worldPacket << uint64(EventID);              // 写入事件ID
    _worldPacket << Notes;                        // 写入备注内容
    _worldPacket << uint8(ClearPending);          // 写入清除待处理标志

    return &_worldPacket;
}

/**
 * @brief 读取投诉数据包
 *
 * 从数据包中读取投诉信息,用于举报滥用邀请功能的行为。
 */
void WorldPackets::Calendar::CalendarComplain::Read()
{
    _worldPacket >> InvitedByGUID;  // 读取邀请者GUID
    _worldPacket >> EventID;        // 读取事件ID
    _worldPacket >> InviteID;       // 读取邀请ID
}
