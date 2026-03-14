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
 * @file CalendarPackets.h
 * @brief 日历系统网络数据包定义
 *
 * 本文件定义了游戏内日历系统相关的所有客户端和服务器端网络数据包。
 * 日历系统允许玩家创建、管理和参加游戏内事件,支持公会活动、副本锁定管理等功能。
 *
 * 主要功能包括:
 * - 事件创建、更新、删除和复制
 * - 邀请玩家参与事件
 * - 事件报名和回复
 * - 团队副本锁定信息管理
 * - 节假日活动显示
 * - 投诉不当邀请
 *
 * 数据包分为两大类:
 * 1. ClientPacket: 客户端发送到服务器的数据包(CMSG_*)
 * 2. ServerPacket: 服务器发送到客户端的数据包(SMSG_*)
 */

#ifndef CalendarPackets_h__
#define CalendarPackets_h__

#include "Packet.h"
#include "CalendarMgr.h"
#include "DBCStructure.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "PacketUtilities.h"
#include "WowTime.h"

namespace WorldPackets
{
    /**
     * @namespace Calendar
     * @brief 日历系统相关数据包命名空间
     *
     * 包含所有日历功能的网络数据包类,用于客户端和服务器之间的通信。
     */
    namespace Calendar
    {
        /**
         * @class CalendarGetCalendar
         * @brief 客户端请求获取完整日历数据的数据包
         *
         * 当玩家打开日历界面时发送此请求,服务器会返回所有相关的日历信息,
         * 包括邀请、事件、副本锁定、节假日等。
         *
         * 调用时机:玩家打开日历界面时
         */
        class CalendarGetCalendar final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarGetCalendar(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_GET_CALENDAR, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此数据包不包含任何额外数据,仅作为请求标识。
             */
            void Read() override { }
        };

        /**
         * @class CalendarGetEvent
         * @brief 客户端请求获取单个事件详细信息的数据包
         *
         * 当玩家点击某个日历事件查看详情时发送,服务器返回该事件的完整信息。
         *
         * 调用时机:玩家点击日历事件查看详情时
         */
        class CalendarGetEvent final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarGetEvent(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_GET_EVENT, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件ID。
             */
            void Read() override;

            uint64 EventID = 0;  ///< 要查询的事件ID
        };

        /**
         * @class CalendarGuildFilter
         * @brief 客户端请求获取符合筛选条件的公会成员列表数据包
         *
         * 用于在创建事件时选择邀请对象,可按等级和公会职位进行筛选。
         *
         * 调用时机:玩家在日历界面选择"邀请公会成员"功能时
         */
        class CalendarGuildFilter final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarGuildFilter(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_GUILD_FILTER, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取筛选条件参数。
             */
            void Read() override;

            uint32 MinLevel = 1;       ///< 最小等级限制
            uint32 MaxLevel = 100;     ///< 最大等级限制
            uint32 MaxRankOrder = 0;   ///< 公会职位排序上限
        };

        /**
         * @class CalendarArenaTeam
         * @brief 客户端请求获取竞技场队伍成员列表数据包
         *
         * 用于邀请竞技场队伍成员参加日历事件。
         *
         * 调用时机:玩家在日历界面选择"邀请竞技场队伍"功能时
         */
        class CalendarArenaTeam final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarArenaTeam(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_ARENA_TEAM, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取竞技场队伍ID。
             */
            void Read() override;

            uint32 ArenaTeamId = 0;  ///< 竞技场队伍ID
        };

        /**
         * @struct CalendarAddEventInviteInfo
         * @brief 创建事件时的邀请信息结构
         *
         * 包含被邀请者的GUID、状态和是否为管理员等信息。
         * 在创建新事件时批量添加邀请列表使用。
         */
        struct CalendarAddEventInviteInfo
        {
            ObjectGuid Guid;       ///< 被邀请玩家的GUID
            uint8 Status = 0;      ///< 邀请状态(待确认、已接受、已拒绝等)
            uint8 Moderator = 0;   ///< 是否为管理员(0=否, 非0=是)
        };

        /**
         * @class CalendarAddEvent
         * @brief 客户端创建新日历事件的数据包
         *
         * 当玩家创建新的日历事件时发送,包含事件的完整信息和初始邀请列表。
         * 服务器会验证玩家权限并创建事件,然后通知所有被邀请者。
         *
         * 调用时机:玩家点击"创建事件"按钮并填写完所有信息后
         */
        class CalendarAddEvent final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarAddEvent(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_ADD_EVENT, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件标题、描述、类型、时间、图标、锁定日期、标志和邀请列表。
             */
            void Read() override;

            uint32 MaxSize = 100;                                                    ///< 最大参与人数
            std::string Title;                                                       ///< 事件标题
            std::string Description;                                                 ///< 事件描述
            uint8 EventType = 0;                                                     ///< 事件类型(公会活动、副本等)
            int32 TextureID = 0;                                                     ///< 事件图标ID
            WowTime Time;                                                            ///< 事件开始时间
            WowTime LockDate;                                                        ///< 锁定时间(锁定后不可更改状态)
            uint32 Flags = 0;                                                        ///< 事件标志(如无邀请、无大量邀请等)
            Array<CalendarAddEventInviteInfo, CALENDAR_MAX_INVITES> Invites;         ///< 初始邀请列表
        };

        /**
         * @class CalendarUpdateEvent
         * @brief 客户端更新日历事件的数据包
         *
         * 当玩家修改现有日历事件时发送,只有事件创建者或管理员可以更新事件。
         * 服务器会通知所有被邀请者事件已更新。
         *
         * 调用时机:事件创建者或管理员编辑事件信息后保存
         */
        class CalendarUpdateEvent final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarUpdateEvent(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_UPDATE_EVENT, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件ID、管理员ID和更新后的事件信息。
             */
            void Read() override;

            uint64 EventID = 0;         ///< 要更新的事件ID
            uint64 ModeratorID = 0;     ///< 执行更新的管理员ID
            std::string Title;          ///< 更新后的事件标题
            std::string Description;    ///< 更新后的事件描述
            uint8 EventType = 0;        ///< 更新后的事件类型
            uint32 TextureID = 0;       ///< 更新后的事件图标ID
            WowTime Time;               ///< 更新后的事件时间
            WowTime LockDate;           ///< 更新后的锁定时间
            uint32 Flags = 0;           ///< 更新后的事件标志
            uint32 MaxSize = 0;         ///< 更新后的最大参与人数
        };

        /**
         * @class CalendarRemoveEvent
         * @brief 客户端删除日历事件的数据包
         *
         * 当玩家删除日历事件时发送,只有事件创建者或管理员可以删除事件。
         * 服务器会通知所有被邀请者事件已删除。
         *
         * 调用时机:事件创建者或管理员删除事件时
         */
        class CalendarRemoveEvent final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarRemoveEvent(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_REMOVE_EVENT, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件ID和管理员ID。
             */
            void Read() override;

            uint64 ModeratorID = 0;  ///< 执行删除的管理员ID
            uint64 EventID = 0;      ///< 要删除的事件ID
            bool IsSignUp = false;   ///< 是否为报名事件
        };

        /**
         * @class CalendarCopyEvent
         * @brief 客户端复制日历事件的数据包
         *
         * 允许玩家复制现有事件到新的日期,新事件会保留原事件的大部分属性。
         * 只有事件创建者可以复制事件。
         *
         * 调用时机:玩家选择复制事件并选择新日期时
         */
        class CalendarCopyEvent final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarCopyEvent(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_COPY_EVENT, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取源事件ID、管理员ID和目标日期。
             */
            void Read() override;

            uint64 ModeratorID = 0;  ///< 执行复制的管理员ID
            uint64 EventID = 0;      ///< 要复制的源事件ID
            WowTime Date;            ///< 复制到的新日期
        };

        /**
         * @class CalendarInviteAdded
         * @brief 服务器通知客户端有新邀请添加到事件的数据包
         *
         * 当有人被邀请到事件时,服务器会向事件创建者和管理员发送此数据包,
         * 通知他们新的邀请信息。
         *
         * 调用时机:服务器添加新邀请到事件后
         */
        class CalendarInviteAdded final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始大小。
             */
            CalendarInviteAdded() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE, 43) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将邀请信息序列化到数据包中,根据Type字段决定是否写入响应时间。
             */
            WorldPacket const* Write() override;

            uint64 InviteID = 0;         ///< 邀请ID
            WowTime ResponseTime;        ///< 响应时间(Type为1时有效)
            uint8 Level = 100;           ///< 被邀请者等级
            ObjectGuid InviteGuid;       ///< 被邀请者GUID
            uint64 EventID = 0;          ///< 事件ID
            uint8 Type = 0;              ///< 邀请类型(0=新邀请, 1=已响应)
            bool ClearPending = false;   ///< 是否清除待处理标志
            uint8 Status = 0;            ///< 邀请状态
        };

        /**
         * @struct CalendarSendCalendarInviteInfo
         * @brief 发送完整日历时的邀请信息结构
         *
         * 用于CalendarSendCalendar数据包,包含玩家收到的所有邀请的概要信息。
         */
        struct CalendarSendCalendarInviteInfo
        {
            uint64 EventID = 0;                                      ///< 事件ID
            uint64 InviteID = 0;                                     ///< 邀请ID
            ObjectGuid InviterGuid;                                  ///< 邀请者GUID
            uint8 Status = 0;                                        ///< 邀请状态
            uint8 Moderator = 0;                                     ///< 是否为管理员
            uint8 InviteType = 0;                                    ///< 邀请类型
            bool IgnoreFriendAndGuildRestriction = false;           ///< 是否忽略好友和公会限制
        };

        /**
         * @struct CalendarSendCalendarRaidLockoutInfo
         * @brief 团队副本锁定信息结构
         *
         * 包含玩家的团队副本锁定信息,用于在日历中显示副本进度。
         */
        struct CalendarSendCalendarRaidLockoutInfo
        {
            uint64 InstanceID = 0;   ///< 副本实例ID
            int32 MapID = 0;         ///< 地图ID
            uint32 DifficultyID = 0; ///< 难度ID
            int32 ExpireTime = 0;    ///< 过期时间(剩余秒数)
        };

        /**
         * @struct CalendarSendCalendarEventInfo
         * @brief 发送完整日历时的事件信息结构
         *
         * 用于CalendarSendCalendar数据包,包含事件的概要信息。
         */
        struct CalendarSendCalendarEventInfo
        {
            uint64 EventID = 0;       ///< 事件ID
            std::string EventName;    ///< 事件名称
            uint8 EventType = 0;      ///< 事件类型
            WowTime Date;             ///< 事件日期
            uint32 Flags = 0;         ///< 事件标志
            int32 TextureID = 0;      ///< 图标ID
            ObjectGuid OwnerGuid;     ///< 创建者GUID
        };

        /**
         * @struct CalendarSendCalendarRaidResetInfo
         * @brief 团队副本重置信息结构
         *
         * 包含团队副本的每周重置时间信息,用于在日历中显示副本重置计划。
         */
        struct CalendarSendCalendarRaidResetInfo
        {
            int32 MapID = 0;      ///< 地图ID
            uint32 Duration = 0;  ///< 重置周期(秒)
            int32 Offset = 0;     ///< 偏移时间
        };

        /**
         * @struct CalendarSendCalendarHolidayInfo
         * @brief 节假日活动信息结构
         *
         * 包含游戏内节假日活动的完整信息,如暗月马戏团、节日活动等。
         * 这些活动会在日历中自动显示。
         */
        struct CalendarSendCalendarHolidayInfo
        {
            int32 HolidayID = 0;                                    ///< 节假日ID
            int32 Region = 0;                                       ///< 地区代码
            int32 Looping = 0;                                      ///< 是否循环
            int32 Priority = 0;                                     ///< 显示优先级
            int32 FilterType = 0;                                   ///< 筛选类型
            std::string_view TextureFilename;                       ///< 图标文件名
            std::array<WowTime, MAX_HOLIDAY_DATES> Date = { };      ///< 活动日期列表
            std::array<int32, MAX_HOLIDAY_DURATIONS> Duration = { };///< 活动持续时间列表
            std::array<int32, MAX_HOLIDAY_FLAGS> CalendarFlags = { };///< 节假日标志列表
        };

        /**
         * @class CalendarSendCalendar
         * @brief 服务器发送完整日历数据的数据包
         *
         * 这是日历系统中最重要的数据包之一,包含了玩家需要知道的所有日历信息。
         * 当玩家打开日历界面或请求日历数据时发送。
         *
         * 调用时机:
         * - 玩家打开日历界面
         * - 玩家请求日历刷新
         *
         * 性能注意事项:此数据包可能包含大量数据,应避免频繁发送。
         */
        class CalendarSendCalendar final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarSendCalendar() : ServerPacket(SMSG_CALENDAR_SEND_CALENDAR, 338) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将所有日历信息序列化到数据包中,包括邀请、事件、副本锁定、重置和节假日。
             */
            WorldPacket const* Write() override;

            WowTime ServerTime;                                           ///< 服务器当前时间
            std::vector<CalendarSendCalendarInviteInfo> Invites;          ///< 邀请列表
            std::vector<CalendarSendCalendarRaidLockoutInfo> RaidLockouts;///< 团队副本锁定列表
            std::vector<CalendarSendCalendarEventInfo> Events;            ///< 事件列表
            time_t ServerNow = time_t(0);                                 ///< 服务器当前时间戳
            time_t RaidOrigin = time_t(0);                                ///< 团队副本重置起始时间
            std::vector<CalendarSendCalendarRaidResetInfo> RaidResets;    ///< 团队副本重置列表
            std::vector<CalendarSendCalendarHolidayInfo> Holidays;        ///< 节假日列表
        };

        /**
         * @struct CalendarEventInviteInfo
         * @brief 事件邀请详细信息结构
         *
         * 包含单个邀请的完整信息,用于CalendarSendEvent数据包。
         */
        struct CalendarEventInviteInfo
        {
            ObjectGuid Guid;          ///< 被邀请者GUID
            uint64 InviteID = 0;      ///< 邀请ID
            WowTime ResponseTime;     ///< 响应时间
            uint8 Level = 1;          ///< 被邀请者等级
            uint8 Status = 0;         ///< 邀请状态
            uint8 Moderator = 0;      ///< 是否为管理员
            uint8 InviteType = 0;     ///< 邀请类型
            std::string Notes;        ///< 备注信息
        };

        /**
         * @class CalendarSendEvent
         * @brief 服务器发送单个事件详细信息的数据包
         *
         * 当玩家查看某个事件的详细信息时发送,包含事件的所有属性和邀请列表。
         *
         * 调用时机:玩家点击查看事件详情时
         */
        class CalendarSendEvent final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarSendEvent() : ServerPacket(SMSG_CALENDAR_SEND_EVENT, 93) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将事件的完整信息和邀请列表序列化到数据包中。
             */
            WorldPacket const* Write() override;

            ObjectGuid OwnerGuid;                          ///< 创建者GUID
            ObjectGuid::LowType EventGuildID = 0;          ///< 关联公会ID(如果是公会活动)
            uint64 EventID = 0;                            ///< 事件ID
            WowTime Date;                                  ///< 事件日期
            WowTime LockDate;                              ///< 锁定日期
            uint32 Flags = 0;                              ///< 事件标志
            int32 TextureID = 0;                           ///< 图标ID
            uint8 GetEventType = 0;                        ///< 获取事件类型
            uint8 EventType = 0;                           ///< 事件类型
            std::string Description;                       ///< 事件描述
            std::string EventName;                         ///< 事件名称
            std::vector<CalendarEventInviteInfo> Invites;  ///< 邀请列表
        };

        /**
         * @class CalendarInviteAlert
         * @brief 服务器通知玩家被邀请参加事件的数据包
         *
         * 当玩家被邀请到某个日历事件时,服务器会向该玩家发送此数据包,
         * 显示邀请弹窗并添加到日历中。
         *
         * 调用时机:有新邀请发送给玩家时
         */
        class CalendarInviteAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarInviteAlert() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_ALERT, 80) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将邀请的详细信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            ObjectGuid OwnerGuid;          ///< 事件创建者GUID
            ObjectGuid InvitedByGuid;      ///< 邀请者GUID
            uint64 InviteID = 0;           ///< 邀请ID
            uint64 EventID = 0;            ///< 事件ID
            uint32 Flags = 0;              ///< 事件标志
            WowTime Date;                  ///< 事件日期
            int32 TextureID = 0;           ///< 事件图标ID
            uint8 Status = 0;              ///< 邀请状态
            uint32 EventType = 0;          ///< 事件类型
            uint8 ModeratorStatus = 0;     ///< 管理员状态
            std::string EventName;         ///< 事件名称
        };

        /**
         * @class CalendarInvite
         * @brief 客户端邀请玩家参加事件的数据包
         *
         * 当事件创建者或管理员邀请其他玩家参加事件时发送。
         * 服务器会验证邀请权限,并向被邀请者发送邀请通知。
         *
         * 调用时机:事件管理员在事件界面点击"邀请玩家"并输入玩家名字后
         */
        class CalendarInvite final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarInvite(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_INVITE, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件ID、管理员ID和被邀请者名字等信息。
             */
            void Read() override;

            uint64 ModeratorID = 0;   ///< 执行邀请的管理员ID
            bool IsSignUp = false;    ///< 是否为报名事件
            bool Creating = true;     ///< 是否正在创建事件
            uint64 EventID = 0;       ///< 事件ID
            std::string Name;         ///< 被邀请者名字
        };

        /**
         * @class CalendarRSVP
         * @brief 客户端响应事件邀请的数据包
         *
         * 当被邀请者接受、拒绝或标记为暂定某个邀请时发送。
         * 服务器会更新邀请状态并通知事件创建者和其他管理员。
         *
         * 调用时机:玩家在日历界面点击"接受"、"拒绝"或"暂定"按钮时
         */
        class CalendarRSVP final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarRSVP(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_RSVP, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取邀请ID、事件ID和响应状态。
             */
            void Read() override;

            uint64 InviteID = 0;  ///< 邀请ID
            uint64 EventID = 0;   ///< 事件ID
            uint8 Status = 0;     ///< 响应状态(接受、拒绝、暂定等)
        };

        /**
         * @class CalendarInviteStatus
         * @brief 服务器通知邀请状态变更的数据包
         *
         * 当某个邀请的状态发生变化时(如玩家接受或拒绝邀请),
         * 服务器会向事件创建者和其他管理员发送此数据包。
         *
         * 调用时机:邀请状态更新后
         */
        class CalendarInviteStatus final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarInviteStatus() : ServerPacket(SMSG_CALENDAR_EVENT_STATUS, 41) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将状态变更信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint32 Flags = 0;              ///< 事件标志
            uint64 EventID = 0;            ///< 事件ID
            uint8 Status = 0;              ///< 新状态
            bool ClearPending = false;     ///< 是否清除待处理标志
            WowTime ResponseTime;          ///< 响应时间
            WowTime Date;                  ///< 事件日期
            ObjectGuid InviteGuid;         ///< 邀请者GUID
        };

        /**
         * @class CalendarInviteRemoved
         * @brief 服务器通知邀请被移除的数据包
         *
         * 当邀请从事件中移除时,服务器会向相关玩家发送此数据包。
         * 只有事件创建者和管理员可以移除邀请。
         *
         * 调用时机:管理员移除某个邀请后
         */
        class CalendarInviteRemoved final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarInviteRemoved() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_REMOVED, 29) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将邀请移除信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            ObjectGuid InviteGuid;      ///< 被移除邀请的玩家GUID
            uint64 EventID = 0;         ///< 事件ID
            uint32 Flags = 0;           ///< 事件标志
            bool ClearPending = false;  ///< 是否清除待处理标志
        };

        /**
         * @class CalendarModeratorStatus
         * @brief 服务器通知管理员状态变更的数据包
         *
         * 当玩家的管理员权限被授予或撤销时,服务器会向相关玩家发送此数据包。
         * 管理员可以编辑事件、邀请玩家和移除邀请。
         *
         * 调用时机:管理员权限变更后
         */
        class CalendarModeratorStatus final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarModeratorStatus() : ServerPacket(SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT, 26) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将管理员状态变更信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            ObjectGuid InviteGuid;      ///< 被变更管理员权限的玩家GUID
            uint64 EventID = 0;         ///< 事件ID
            uint8 Status = 0;           ///< 新的管理员状态
            bool ClearPending = false;  ///< 是否清除待处理标志
        };

        /**
         * @class CalendarInviteRemovedAlert
         * @brief 服务器通知被邀请者邀请被移除的数据包
         *
         * 当玩家被从事件邀请列表中移除时,服务器向该玩家发送此数据包。
         *
         * 调用时机:邀请被移除后通知被移除的玩家
         */
        class CalendarInviteRemovedAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarInviteRemovedAlert() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT, 17) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将邀请移除通知信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 EventID = 0;   ///< 事件ID
            WowTime Date;         ///< 事件日期
            uint32 Flags = 0;     ///< 事件标志
            uint8 Status = 0;     ///< 邀请状态
        };

        /**
         * @class CalendarClearPendingAction
         * @brief 服务器通知清除待处理操作的数据包
         *
         * 用于通知客户端清除日历界面上的待处理操作提示。
         * 这是一个空数据包,仅包含操作码。
         *
         * 调用时机:待处理操作完成或取消后
         */
        class CalendarClearPendingAction final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码,大小为0。
             */
            CalendarClearPendingAction() : ServerPacket(SMSG_CALENDAR_CLEAR_PENDING_ACTION, 0) { }

            /**
             * @brief 写入数据包内容
             * @return 返回数据包指针
             *
             * 此数据包不包含额外数据,直接返回空数据包。
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class CalendarEventUpdatedAlert
         * @brief 服务器通知事件已更新的数据包
         *
         * 当事件信息被更新时,服务器会向所有被邀请者发送此数据包,
         * 通知他们事件的变更内容。
         *
         * 调用时机:事件创建者或管理员更新事件后
         */
        class CalendarEventUpdatedAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarEventUpdatedAlert() : ServerPacket(SMSG_CALENDAR_EVENT_UPDATED_ALERT, 32) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将事件更新信息序列化到数据包中,包括新旧日期和更新后的属性。
             */
            WorldPacket const* Write() override;

            uint64 EventID = 0;          ///< 事件ID
            WowTime Date;                ///< 更新后的日期
            uint32 Flags = 0;            ///< 更新后的标志
            WowTime LockDate;            ///< 更新后的锁定日期
            WowTime OriginalDate;        ///< 原始日期(用于显示变更)
            int32 TextureID = 0;         ///< 更新后的图标ID
            uint8 EventType = 0;         ///< 更新后的事件类型
            bool ClearPending = false;   ///< 是否清除待处理标志
            std::string Description;     ///< 更新后的描述
            std::string EventName;       ///< 更新后的名称
        };

        /**
         * @class CalendarEventRemovedAlert
         * @brief 服务器通知事件已删除的数据包
         *
         * 当事件被删除时,服务器会向所有被邀请者发送此数据包,
         * 通知他们事件已取消。
         *
         * 调用时机:事件创建者或管理员删除事件后
         */
        class CalendarEventRemovedAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和初始预估大小。
             */
            CalendarEventRemovedAlert() : ServerPacket(SMSG_CALENDAR_EVENT_REMOVED_ALERT, 13) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将事件删除信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 EventID = 0;         ///< 被删除的事件ID
            WowTime Date;               ///< 事件日期
            bool ClearPending = false;  ///< 是否清除待处理标志
        };

        /**
         * @class CalendarSendNumPending
         * @brief 服务器发送待处理邀请数量的数据包
         *
         * 通知客户端当前有多少待处理的日历邀请。
         * 这个数字通常会显示在小地图日历图标上。
         *
         * 调用时机:
         * - 玩家登录时
         * - 收到新邀请时
         * - 玩家请求待处理数量时
         */
        class CalendarSendNumPending final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarSendNumPending() : ServerPacket(SMSG_CALENDAR_SEND_NUM_PENDING, 4) { }

            /**
             * @brief 带参数的构造函数
             * @param numPending 待处理邀请数量
             *
             * 初始化服务器数据包并设置待处理数量。
             */
            CalendarSendNumPending(uint32 numPending) : ServerPacket(SMSG_CALENDAR_SEND_NUM_PENDING, 4), NumPending(numPending) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将待处理数量序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint32 NumPending = 0;  ///< 待处理的邀请数量
        };

        /**
         * @class CalendarGetNumPending
         * @brief 客户端请求获取待处理邀请数量的数据包
         *
         * 当客户端需要更新待处理邀请数量显示时发送。
         *
         * 调用时机:客户端需要刷新待处理邀请数量时
         */
        class CalendarGetNumPending final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarGetNumPending(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_GET_NUM_PENDING, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此数据包不包含任何额外数据,仅作为请求标识。
             */
            void Read() override { }
        };

        /**
         * @class CalendarEventSignUp
         * @brief 客户端报名参加事件的数据包
         *
         * 当玩家在公开事件中报名参加时发送。
         * 某些事件允许玩家自行报名,无需单独邀请。
         *
         * 调用时机:玩家点击事件列表中的"报名"按钮时
         */
        class CalendarEventSignUp final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarEventSignUp(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_SIGNUP, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取事件ID和报名类型。
             */
            void Read() override;

            bool Tentative = false;  ///< 是否为暂定报名
            uint64 EventID = 0;      ///< 要报名的事件ID
        };

        /**
         * @class CalendarRemoveInvite
         * @brief 客户端移除邀请的数据包
         *
         * 当事件创建者或管理员移除某个邀请时发送。
         * 服务器会验证权限并向被移除的玩家发送通知。
         *
         * 调用时机:事件管理员在邀请列表中移除某个玩家时
         */
        class CalendarRemoveInvite final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarRemoveInvite(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_REMOVE_INVITE, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取要移除的邀请信息。
             */
            void Read() override;

            ObjectGuid Guid;          ///< 要移除的玩家GUID
            uint64 EventID = 0;       ///< 事件ID
            uint64 ModeratorID = 0;   ///< 执行移除的管理员ID
            uint64 InviteID = 0;      ///< 要移除的邀请ID
        };

        /**
         * @class CalendarStatus
         * @brief 客户端更新邀请状态的数据包
         *
         * 当玩家通过事件详情界面更改自己的邀请状态时发送。
         * 这是对CalendarRSVP的补充,提供更多上下文信息。
         *
         * 调用时机:玩家在事件详情界面更改状态时
         */
        class CalendarStatus final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarStatus(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_STATUS, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取状态更新信息。
             */
            void Read() override;

            ObjectGuid Guid;          ///< 玩家GUID
            uint64 EventID = 0;       ///< 事件ID
            uint64 ModeratorID = 0;   ///< 管理员ID
            uint64 InviteID = 0;      ///< 邀请ID
            uint8 Status = 0;         ///< 新状态
        };

        /**
         * @class SetSavedInstanceExtend
         * @brief 客户端设置副本锁定延期的数据包
         *
         * 当玩家选择延长或取消延长某个副本的锁定时间时发送。
         * 允许玩家保留副本进度到下一个周期。
         *
         * 调用时机:玩家在角色信息界面的副本锁定标签中操作时
         */
        class SetSavedInstanceExtend final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            SetSavedInstanceExtend(WorldPacket&& packet) : ClientPacket(CMSG_SET_SAVED_INSTANCE_EXTEND, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取副本延期设置信息。
             */
            void Read() override;

            int32 MapID = 0;          ///< 地图ID
            bool Extend = false;      ///< 是否延长(false=取消延长)
            uint32 DifficultyID = 0;  ///< 难度ID
        };

        /**
         * @class CalendarModeratorStatusQuery
         * @brief 客户端请求更改管理员状态的数据包
         *
         * 当事件创建者授予或撤销某玩家的管理员权限时发送。
         * 管理员可以编辑事件、邀请玩家和移除邀请。
         *
         * 调用时机:事件创建者在邀请列表中设置管理员权限时
         */
        class CalendarModeratorStatusQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarModeratorStatusQuery(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_EVENT_MODERATOR_STATUS, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取管理员状态变更信息。
             */
            void Read() override;

            ObjectGuid Guid;          ///< 被更改权限的玩家GUID
            uint64 EventID = 0;       ///< 事件ID
            uint64 InviteID = 0;      ///< 邀请ID
            uint64 ModeratorID = 0;   ///< 执行操作的管理员ID
            uint8 Status = 0;         ///< 新的管理员状态
        };

        /**
         * @class CalendarCommandResult
         * @brief 服务器发送命令执行结果的数据包
         *
         * 当客户端发送的日历操作请求失败时,服务器会发送此数据包通知失败原因。
         * 例如邀请不存在的玩家、权限不足等情况。
         *
         * 调用时机:日历操作失败时
         */
        class CalendarCommandResult final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarCommandResult() : ServerPacket(SMSG_CALENDAR_COMMAND_RESULT, 3) { }

            /**
             * @brief 带参数的构造函数
             * @param command 命令类型
             * @param result 结果代码
             * @param name 相关玩家名字
             *
             * 初始化服务器数据包并设置命令结果信息。
             */
            CalendarCommandResult(uint8 command, uint8 result, std::string const& name) : ServerPacket(SMSG_CALENDAR_COMMAND_RESULT, 3), Command(command), Result(result), Name(name) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将命令执行结果序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint32 Command = 0;   ///< 命令类型(如邀请、删除等)
            uint32 Result = 0;    ///< 结果代码(错误码)
            std::string Name;     ///< 相关玩家名字
        };

        /**
         * @class CalendarRaidLockoutAdded
         * @brief 服务器通知团队副本锁定已添加的数据包
         *
         * 当玩家获得新的团队副本锁定时(如完成副本或首领击杀),
         * 服务器会发送此数据包通知客户端更新日历显示。
         *
         * 调用时机:玩家获得新的副本锁定时
         */
        class CalendarRaidLockoutAdded final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarRaidLockoutAdded() : ServerPacket(SMSG_CALENDAR_RAID_LOCKOUT_ADDED, 8 + 4 + 4 + 4 + 4) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将副本锁定信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 InstanceID = 0;     ///< 副本实例ID
            uint32 DifficultyID = 0;   ///< 难度ID
            int32 TimeRemaining = 0;   ///< 剩余时间(秒)
            WowTime ServerTime;        ///< 服务器时间
            int32 MapID = 0;           ///< 地图ID
        };

        /**
         * @class CalendarRaidLockoutRemoved
         * @brief 服务器通知团队副本锁定已移除的数据包
         *
         * 当团队副本锁定过期或被手动移除时,服务器会发送此数据包。
         *
         * 调用时机:副本锁定过期或被移除时
         */
        class CalendarRaidLockoutRemoved final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarRaidLockoutRemoved() : ServerPacket(SMSG_CALENDAR_RAID_LOCKOUT_REMOVED, 8 + 4 + 4) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将副本锁定移除信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 InstanceID = 0;     ///< 副本实例ID
            int32 MapID = 0;           ///< 地图ID
            uint32 DifficultyID = 0;   ///< 难度ID
            int32 TimeRemaining = 0;   ///< 剩余时间(秒)
        };

        /**
         * @class CalendarRaidLockoutUpdated
         * @brief 服务器通知团队副本锁定已更新的数据包
         *
         * 当副本锁定时间发生变化时(如延期操作),服务器会发送此数据包。
         *
         * 调用时机:副本锁定时间更新时
         */
        class CalendarRaidLockoutUpdated final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarRaidLockoutUpdated() : ServerPacket(SMSG_CALENDAR_RAID_LOCKOUT_UPDATED, 20) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将副本锁定更新信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            WowTime ServerTime;            ///< 服务器时间
            int32 MapID = 0;               ///< 地图ID
            uint32 DifficultyID = 0;       ///< 难度ID
            int32 NewTimeRemaining = 0;    ///< 新的剩余时间
            int32 OldTimeRemaining = 0;    ///< 旧的剩余时间
        };

        /**
         * @struct CalendarEventInitialInviteInfo
         * @brief 初始邀请信息结构
         *
         * 用于公会或竞技场队伍邀请列表,包含玩家的GUID和等级。
         */
        struct CalendarEventInitialInviteInfo
        {
            /**
             * @brief 构造函数
             * @param inviteGuid 邀请目标GUID
             * @param level 玩家等级
             */
            CalendarEventInitialInviteInfo(ObjectGuid inviteGuid, uint8 level) : InviteGuid(inviteGuid), Level(level) { }

            ObjectGuid InviteGuid;   ///< 被邀请者GUID
            uint8 Level = 100;       ///< 玩家等级
        };

        /**
         * @class CalendarEventInitialInvites
         * @brief 服务器发送公会或竞技场队伍成员列表的数据包
         *
         * 当玩家请求公会或竞技场队伍成员列表用于邀请时,
         * 服务器会发送此数据包包含所有符合条件成员的信息。
         *
         * 调用时机:响应CalendarGuildFilter或CalendarArenaTeam请求时
         */
        class CalendarEventInitialInvites final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param guild 是否为公会邀请(true=公会, false=竞技场队伍)
             *
             * 根据邀请类型设置相应的操作码。
             */
            CalendarEventInitialInvites(bool guild) : ServerPacket(guild ? SMSG_CALENDAR_FILTER_GUILD : SMSG_CALENDAR_ARENA_TEAM, 17) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将成员列表序列化到数据包中。
             */
            WorldPacket const* Write() override;

            std::vector<CalendarEventInitialInviteInfo> Invites;  ///< 成员邀请列表
        };

        /**
         * @class CalendarInviteStatusAlert
         * @brief 服务器发送邀请状态警报的数据包
         *
         * 用于通知客户端某个邀请的状态发生了重要变化。
         *
         * 调用时机:邀请状态需要特别通知时
         */
        class CalendarInviteStatusAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarInviteStatusAlert() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT, 5) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将状态警报信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 EventID = 0;   ///< 事件ID
            uint32 Flags = 0;     ///< 事件标志
            WowTime Date;         ///< 事件日期
            uint8 Status = 0;     ///< 邀请状态
        };

        /**
         * @class CalendarInviteNotesAlert
         * @brief 服务器发送邀请备注警报的数据包
         *
         * 用于通知某个邀请的备注信息。
         * 备注可以包含玩家对事件的说明或注意事项。
         *
         * 调用时机:邀请备注更新时
         */
        class CalendarInviteNotesAlert final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarInviteNotesAlert() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT, 9) { }

            /**
             * @brief 带参数的构造函数
             * @param eventID 事件ID
             * @param notes 备注内容
             *
             * 初始化服务器数据包并设置备注信息。
             */
            CalendarInviteNotesAlert(uint64 eventID, std::string const& notes) : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT, 8 + notes.size()), EventID(eventID), Notes(notes) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将备注信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            uint64 EventID = 0;   ///< 事件ID
            std::string Notes;    ///< 备注内容
        };

        /**
         * @class CalendarInviteNotes
         * @brief 服务器发送邀请备注的数据包
         *
         * 向所有相关玩家通知某个邀请的备注信息变更。
         *
         * 调用时机:邀请备注更新时广播
         */
        class CalendarInviteNotes final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,设置操作码和大小。
             */
            CalendarInviteNotes() : ServerPacket(SMSG_CALENDAR_EVENT_INVITE_NOTES, 26) { }

            /**
             * @brief 写入数据包内容
             * @return 返回写入完成的数据包指针
             *
             * 将备注信息序列化到数据包中。
             */
            WorldPacket const* Write() override;

            ObjectGuid InviteGuid;      ///< 邀请者GUID
            uint64 EventID = 0;         ///< 事件ID
            std::string Notes;          ///< 备注内容
            bool ClearPending = false;  ///< 是否清除待处理标志
        };

        /**
         * @class CalendarComplain
         * @brief 客户端投诉日历事件邀请的数据包
         *
         * 当玩家认为某个邀请是垃圾信息或滥用行为时,可以发送此投诉。
         * 服务器会记录投诉并可能对违规者采取行动。
         *
         * 调用时机:玩家点击邀请的"投诉"按钮时
         */
        class CalendarComplain final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CalendarComplain(WorldPacket&& packet) : ClientPacket(CMSG_CALENDAR_COMPLAIN, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从数据包中读取投诉相关信息。
             */
            void Read() override;

            ObjectGuid InvitedByGUID;  ///< 邀请者GUID
            uint64 InviteID = 0;       ///< 邀请ID
            uint64 EventID = 0;        ///< 事件ID
        };
    }
}

#endif // CalendarPackets_h__
