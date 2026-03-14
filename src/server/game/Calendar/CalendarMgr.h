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
 * @file CalendarMgr.h
 * @brief 日历系统管理模块头文件
 *
 * 本模块实现了游戏内的日历系统功能，包括：
 * - 日历事件的创建、修改、删除和管理
 * - 事件邀请的发送、回复和管理
 * - 公会事件的特殊处理
 * - 与客户端的网络包通信
 * - 数据库持久化存储
 *
 * 日历系统允许玩家创建和管理游戏内活动，支持：
 * - 个人事件和公会事件
 * - 邀请其他玩家参与事件
 * - 设置事件类型（副本、PvP、会议等）
 * - 事件提醒和通知功能
 */

#ifndef TRINITY_CALENDARMGR_H
#define TRINITY_CALENDARMGR_H

#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <deque>
#include <map>
#include <set>
#include <vector>

class Player;
class WorldPacket;

/**
 * @enum CalendarMailAnswers
 * @brief 日历邮件主题类型枚举
 *
 * 定义日历系统发送邮件时的主题类型
 */
enum CalendarMailAnswers
{
    // 事件被删除时的邮件主题
    CALENDAR_EVENT_REMOVED_MAIL_SUBJECT     = 0,
    // 邀请被移除时的邮件主题
    CALENDAR_INVITE_REMOVED_MAIL_SUBJECT    = 0x100
};

/**
 * @enum CalendarFlags
 * @brief 日历事件标志枚举
 *
 * 定义日历事件的各种标志属性
 */
enum CalendarFlags
{
    CALENDAR_FLAG_ALL_ALLOWED       = 0x001,  ///< 允许所有操作
    CALENDAR_FLAG_INVITES_LOCKED    = 0x010,  ///< 邀请已锁定，不能再邀请
    CALENDAR_FLAG_WITHOUT_INVITES   = 0x040,  ///< 无邀请列表（公会公告）
    CALENDAR_FLAG_GUILD_EVENT       = 0x400   ///< 公会事件标志
};

/**
 * @enum CalendarModerationRank
 * @brief 日历事件权限等级枚举
 *
 * 定义玩家在日历事件中的权限等级
 */
enum CalendarModerationRank
{
    CALENDAR_RANK_PLAYER            = 0,  ///< 普通参与者
    CALENDAR_RANK_MODERATOR         = 1,  ///< 管理员（可管理邀请）
    CALENDAR_RANK_OWNER             = 2   ///< 所有者（完全控制权限）
};

/**
 * @enum CalendarSendEventType
 * @brief 日历事件发送类型枚举
 *
 * 定义向客户端发送日历事件时的类型
 */
enum CalendarSendEventType
{
    CALENDAR_SENDTYPE_GET           = 0,  ///< 获取现有事件
    CALENDAR_SENDTYPE_ADD           = 1,  ///< 添加新事件
    CALENDAR_SENDTYPE_COPY          = 2   ///< 复制事件
};

/**
 * @enum CalendarEventType
 * @brief 日历事件类型枚举
 *
 * 定义日历事件的类型，用于区分不同性质的活动
 */
enum CalendarEventType
{
    CALENDAR_TYPE_RAID              = 0,  ///< 团队副本活动
    CALENDAR_TYPE_DUNGEON           = 1,  ///< 地下城活动
    CALENDAR_TYPE_PVP               = 2,  ///< PvP活动
    CALENDAR_TYPE_MEETING           = 3,  ///< 会议
    CALENDAR_TYPE_OTHER             = 4   ///< 其他类型活动
};

/**
 * @enum CalendarRepeatType
 * @brief 日历事件重复类型枚举
 *
 * 定义事件是否重复以及重复的频率
 */
enum CalendarRepeatType
{
    CALENDAR_REPEAT_NEVER           = 0,  ///< 从不重复
    CALENDAR_REPEAT_WEEKLY          = 1,  ///< 每周重复
    CALENDAR_REPEAT_BIWEEKLY        = 2,  ///< 每两周重复
    CALENDAR_REPEAT_MONTHLY         = 3   ///< 每月重复
};

/**
 * @enum CalendarInviteStatus
 * @brief 日历邀请状态枚举
 *
 * 定义被邀请者对事件邀请的响应状态
 */
enum CalendarInviteStatus
{
    CALENDAR_STATUS_INVITED         = 0,  ///< 已邀请，等待回复
    CALENDAR_STATUS_ACCEPTED        = 1,  ///< 已接受邀请
    CALENDAR_STATUS_DECLINED        = 2,  ///< 已拒绝邀请
    CALENDAR_STATUS_CONFIRMED       = 3,  ///< 已确认参加
    CALENDAR_STATUS_OUT             = 4,  ///< 不再参加
    CALENDAR_STATUS_STANDBY         = 5,  ///< 候补状态
    CALENDAR_STATUS_SIGNED_UP       = 6,  ///< 已报名
    CALENDAR_STATUS_NOT_SIGNED_UP   = 7,  ///< 未报名
    CALENDAR_STATUS_TENTATIVE       = 8,  ///< 暂定参加
    CALENDAR_STATUS_REMOVED         = 9   ///< 已移除
};

/**
 * @enum CalendarError
 * @brief 日历操作错误码枚举
 *
 * 定义日历系统各种操作可能返回的错误码
 */
enum CalendarError
{
    CALENDAR_OK                                 = 0,   ///< 操作成功
    CALENDAR_ERROR_GUILD_EVENTS_EXCEEDED        = 1,   ///< 公会事件数量超限
    CALENDAR_ERROR_EVENTS_EXCEEDED              = 2,   ///< 事件数量超限
    CALENDAR_ERROR_SELF_INVITES_EXCEEDED        = 3,   ///< 自身邀请数量超限
    CALENDAR_ERROR_OTHER_INVITES_EXCEEDED       = 4,   ///< 其他邀请数量超限
    CALENDAR_ERROR_PERMISSIONS                  = 5,   ///< 权限不足
    CALENDAR_ERROR_EVENT_INVALID                = 6,   ///< 无效的事件
    CALENDAR_ERROR_NOT_INVITED                  = 7,   ///< 未被邀请
    CALENDAR_ERROR_INTERNAL                     = 8,   ///< 内部错误
    CALENDAR_ERROR_GUILD_PLAYER_NOT_IN_GUILD    = 9,   ///< 玩家不在公会中
    CALENDAR_ERROR_ALREADY_INVITED_TO_EVENT_S   = 10,  ///< 已被邀请到此事件
    CALENDAR_ERROR_PLAYER_NOT_FOUND             = 11,  ///< 找不到玩家
    CALENDAR_ERROR_NOT_ALLIED                   = 12,  ///< 非同盟关系
    CALENDAR_ERROR_IGNORING_YOU_S               = 13,  ///< 对方屏蔽了你
    CALENDAR_ERROR_INVITES_EXCEEDED             = 14,  ///< 邀请数量超限
    CALENDAR_ERROR_INVALID_DATE                 = 16,  ///< 无效日期
    CALENDAR_ERROR_INVALID_TIME                 = 17,  ///< 无效时间

    CALENDAR_ERROR_NEEDS_TITLE                  = 19,  ///< 需要标题
    CALENDAR_ERROR_EVENT_PASSED                 = 20,  ///< 事件已过期
    CALENDAR_ERROR_EVENT_LOCKED                 = 21,  ///< 事件已锁定
    CALENDAR_ERROR_DELETE_CREATOR_FAILED        = 22,  ///< 删除创建者失败
    CALENDAR_ERROR_SYSTEM_DISABLED              = 24,  ///< 系统已禁用
    CALENDAR_ERROR_RESTRICTED_ACCOUNT           = 25,  ///< 账号受限
    CALENDAR_ERROR_ARENA_EVENTS_EXCEEDED        = 26,  ///< 竞技场事件超限
    CALENDAR_ERROR_RESTRICTED_LEVEL             = 27,  ///< 等级受限
    CALENDAR_ERROR_USER_SQUELCHED               = 28,  ///< 用户被禁言
    CALENDAR_ERROR_NO_INVITE                    = 29,  ///< 没有邀请

    CALENDAR_ERROR_EVENT_WRONG_SERVER           = 36,  ///< 事件服务器错误
    CALENDAR_ERROR_INVITE_WRONG_SERVER          = 37,  ///< 邀请服务器错误
    CALENDAR_ERROR_NO_GUILD_INVITES             = 38,  ///< 无公会邀请权限
    CALENDAR_ERROR_INVALID_SIGNUP               = 39,  ///< 无效报名
    CALENDAR_ERROR_NO_MODERATOR                 = 40   ///< 无管理员
};

/**
 * @enum CalendarLimits
 * @brief 日历系统限制常量枚举
 *
 * 定义日历系统的各种数量和时间限制
 */
enum CalendarLimits
{
    CALENDAR_MAX_EVENTS = 30,              ///< 单个玩家最大事件数量
    CALENDAR_MAX_GUILD_EVENTS = 100,       ///< 公会最大事件数量
    CALENDAR_MAX_INVITES = 100,            ///< 单个事件最大邀请数量
    CALENDAR_CREATE_EVENT_COOLDOWN = 5,    ///< 创建事件冷却时间（秒）
    CALENDAR_OLD_EVENTS_DELETION_TIME = 1 * MONTH,  ///< 旧事件删除时间（1个月）
};

/// 默认响应时间（2000年1月1日 00:00:00）
#define CALENDAR_DEFAULT_RESPONSE_TIME  946684800

/**
 * @struct CalendarInvite
 * @brief 日历邀请结构体
 *
 * 表示一个日历事件的邀请记录，包含被邀请者信息、
 * 邀请状态、权限等级等数据。
 */
struct TC_GAME_API CalendarInvite
{
    public:
        /**
         * @brief 复制构造函数（带新ID）
         * @param calendarInvite 源邀请对象
         * @param inviteId 新的邀请ID
         * @param eventId 新的事件ID
         *
         * 用于复制邀请时创建新的邀请对象
         */
        CalendarInvite(CalendarInvite const& calendarInvite, uint64 inviteId, uint64 eventId)
        {
            _inviteId = inviteId;
            _eventId = eventId;
            _invitee = calendarInvite.GetInviteeGUID();
            _senderGUID = calendarInvite.GetSenderGUID();
            _responseTime = calendarInvite.GetResponseTime();
            _status = calendarInvite.GetStatus();
            _rank = calendarInvite.GetRank();
            _note = calendarInvite.GetNote();
        }

        /// 默认构造函数
        CalendarInvite();

        /**
         * @brief 完整构造函数
         * @param inviteId 邀请唯一标识符
         * @param eventId 关联的事件ID
         * @param invitee 被邀请者GUID
         * @param senderGUID 邀请发送者GUID
         * @param statusTime 响应时间
         * @param status 邀请状态
         * @param rank 权限等级
         * @param note 备注
         */
        CalendarInvite(uint64 inviteId, uint64 eventId, ObjectGuid invitee, ObjectGuid senderGUID, time_t statusTime,
            CalendarInviteStatus status, CalendarModerationRank rank, std::string note) :
            _inviteId(inviteId), _eventId(eventId), _invitee(invitee), _senderGUID(senderGUID), _responseTime(statusTime),
            _status(status), _rank(rank), _note(std::move(note)) { }

        CalendarInvite(CalendarInvite const&) = delete;       ///< 禁用拷贝构造
        CalendarInvite(CalendarInvite&&) = delete;            ///< 禁用移动构造

        CalendarInvite& operator=(CalendarInvite const&) = delete;  ///< 禁用拷贝赋值
        CalendarInvite& operator=(CalendarInvite&&) = delete;       ///< 禁用移动赋值

        /// 析构函数，自动释放邀请ID
        ~CalendarInvite();

        /// 设置邀请ID
        void SetInviteId(uint64 inviteId) { _inviteId = inviteId; }
        /// 获取邀请ID
        uint64 GetInviteId() const { return _inviteId; }

        /// 设置事件ID
        void SetEventId(uint64 eventId) { _eventId = eventId; }
        /// 获取事件ID
        uint64 GetEventId() const { return _eventId; }

        /// 设置邀请发送者GUID
        void SetSenderGUID(ObjectGuid guid) { _senderGUID = guid; }
        /// 获取邀请发送者GUID
        ObjectGuid GetSenderGUID() const { return _senderGUID; }

        /// 设置被邀请者GUID
        void SetInvitee(ObjectGuid guid) { _invitee = guid; }
        /// 获取被邀请者GUID
        ObjectGuid GetInviteeGUID() const { return _invitee; }

        /// 设置响应时间
        void SetResponseTime(time_t statusTime) { _responseTime = statusTime; }
        /// 获取响应时间
        time_t GetResponseTime() const { return _responseTime; }

        /// 设置备注
        void SetNote(std::string const& note) { _note = note; }
        /// 获取备注
        std::string GetNote() const { return _note; }

        /// 设置邀请状态
        void SetStatus(CalendarInviteStatus status) { _status = status; }
        /// 获取邀请状态
        CalendarInviteStatus GetStatus() const { return _status; }

        /// 设置权限等级
        void SetRank(CalendarModerationRank rank) { _rank = rank; }
        /// 获取权限等级
        CalendarModerationRank GetRank() const { return _rank; }

    private:
        uint64 _inviteId;              ///< 邀请唯一标识符
        uint64 _eventId;               ///< 关联的事件ID
        ObjectGuid _invitee;           ///< 被邀请者GUID
        ObjectGuid _senderGUID;        ///< 邀请发送者GUID
        time_t _responseTime;          ///< 最后响应时间
        CalendarInviteStatus _status;  ///< 当前邀请状态
        CalendarModerationRank _rank;  ///< 权限等级
        std::string _note;             ///< 玩家备注信息
};

/**
 * @struct CalendarEvent
 * @brief 日历事件结构体
 *
 * 表示一个日历事件，包含事件的所有基本信息，
 * 如时间、地点、描述、参与者列表等。
 */
struct TC_GAME_API CalendarEvent
{
    public:
        /**
         * @brief 复制构造函数（带新ID）
         * @param calendarEvent 源事件对象
         * @param eventId 新的事件ID
         *
         * 用于复制事件时创建新的事件对象
         */
        CalendarEvent(CalendarEvent const& calendarEvent, uint64 eventId)
        {
            _eventId = eventId;
            _ownerGUID = calendarEvent.GetOwnerGUID();
            _eventGuildId = calendarEvent.GetGuildId();
            _eventType = calendarEvent.GetType();
            _textureId = calendarEvent.GetTextureId();
            _date = calendarEvent.GetDate();
            _flags = calendarEvent.GetFlags();
            _title = calendarEvent.GetTitle();
            _description = calendarEvent.GetDescription();
            _lockDate = calendarEvent.GetLockDate();
        }

        /**
         * @brief 完整构造函数
         * @param eventId 事件唯一标识符
         * @param ownerGUID 事件创建者GUID
         * @param guildId 关联公会ID（公会事件）
         * @param type 事件类型
         * @param textureId 事件图标/纹理ID
         * @param date 事件发生时间
         * @param flags 事件标志位
         * @param title 事件标题
         * @param description 事件描述
         * @param lockDate 锁定时间（锁定后不可修改）
         */
        CalendarEvent(uint64 eventId, ObjectGuid ownerGUID, ObjectGuid::LowType guildId, CalendarEventType type, int32 textureId,
            time_t date, uint32 flags, std::string title, std::string description, time_t lockDate) :
            _eventId(eventId), _ownerGUID(ownerGUID), _eventGuildId(guildId), _eventType(type), _textureId(textureId),
            _date(date), _flags(flags), _title(std::move(title)), _description(std::move(description)), _lockDate(lockDate) { }

        /// 默认构造函数，初始化为默认值
        CalendarEvent() : _eventId(1), _ownerGUID(), _eventGuildId(UI64LIT(0)), _eventType(CALENDAR_TYPE_OTHER), _textureId(-1), _date(0),
            _flags(0), _title(), _description(), _lockDate(0) { }

        CalendarEvent(CalendarEvent const&) = delete;       ///< 禁用拷贝构造
        CalendarEvent(CalendarEvent&&) = delete;            ///< 禁用移动构造

        CalendarEvent& operator=(CalendarEvent const&) = delete;  ///< 禁用拷贝赋值
        CalendarEvent& operator=(CalendarEvent&&) = delete;       ///< 禁用移动赋值

        /// 析构函数，自动释放事件ID
        ~CalendarEvent();

        /// 设置事件ID
        void SetEventId(uint64 eventId) { _eventId = eventId; }
        /// 获取事件ID
        uint64 GetEventId() const { return _eventId; }

        /// 设置事件创建者GUID
        void SetOwnerGUID(ObjectGuid guid) { _ownerGUID = guid; }
        /// 获取事件创建者GUID
        ObjectGuid GetOwnerGUID() const { return _ownerGUID; }

        /// 设置关联公会ID
        void SetGuildId(ObjectGuid::LowType guildId) { _eventGuildId = guildId; }
        /// 获取关联公会ID
        ObjectGuid::LowType GetGuildId() const { return _eventGuildId; }

        /// 设置事件标题
        void SetTitle(std::string const& title) { _title = title; }
        /// 获取事件标题
        std::string GetTitle() const { return _title; }

        /// 设置事件描述
        void SetDescription(std::string const& description) { _description = description; }
        /// 获取事件描述
        std::string GetDescription() const { return _description; }

        /// 设置事件类型
        void SetType(CalendarEventType eventType) { _eventType = eventType; }
        /// 获取事件类型
        CalendarEventType GetType() const { return _eventType; }

        /// 设置纹理ID（事件图标）
        void SetTextureId(int32 textureId) { _textureId = textureId; }
        /// 获取纹理ID
        int32 GetTextureId() const { return _textureId; }

        /// 设置事件时间
        void SetDate(time_t date) { _date = date; }
        /// 获取事件时间
        time_t GetDate() const { return _date; }

        /// 设置事件标志
        void SetFlags(uint32 flags) { _flags = flags; }
        /// 获取事件标志
        uint32 GetFlags() const { return _flags; }

        /// 判断是否为公会事件
        bool IsGuildEvent() const { return (_flags & CALENDAR_FLAG_GUILD_EVENT) != 0; }
        /// 判断是否为公会公告（无邀请列表）
        bool IsGuildAnnouncement() const { return (_flags & CALENDAR_FLAG_WITHOUT_INVITES) != 0; }
        /// 判断事件是否已锁定
        bool IsLocked() const { return (_flags & CALENDAR_FLAG_INVITES_LOCKED) != 0; }

        /// 设置锁定时间
        void SetLockDate(time_t lockDate) { _lockDate = lockDate; }
        /// 获取锁定时间
        time_t GetLockDate() const { return _lockDate; }

        /// 静态方法：判断标志是否表示公会事件
        static bool IsGuildEvent(uint32 flags) { return (flags & CALENDAR_FLAG_GUILD_EVENT) != 0; }
        /// 静态方法：判断标志是否表示公会公告
        static bool IsGuildAnnouncement(uint32 flags) { return (flags & CALENDAR_FLAG_WITHOUT_INVITES) != 0; }

        /**
         * @brief 构建日历邮件主题
         * @param remover 删除事件的人的GUID
         * @return 格式化的邮件主题字符串
         */
        std::string BuildCalendarMailSubject(ObjectGuid remover) const;

        /**
         * @brief 构建日历邮件正文
         * @param invitee 被邀请者（用于时区转换）
         * @return 格式化的邮件正文
         */
        std::string BuildCalendarMailBody(Player const* invitee) const;

    private:
        uint64 _eventId;              ///< 事件唯一标识符
        ObjectGuid _ownerGUID;        ///< 事件创建者GUID
        ObjectGuid::LowType _eventGuildId;  ///< 关联公会ID（如果是公会事件）
        CalendarEventType _eventType; ///< 事件类型（副本、PvP等）
        int32 _textureId;             ///< 事件图标纹理ID
        time_t _date;                 ///< 事件开始时间（Unix时间戳）
        uint32 _flags;                ///< 事件标志位
        std::string _title;           ///< 事件标题
        std::string _description;     ///< 事件详细描述
        time_t _lockDate;             ///< 锁定时间（锁定后不可修改）
};

/// 日历邀请存储类型（向量）
typedef std::vector<CalendarInvite*> CalendarInviteStore;
/// 日历事件存储类型（集合）
typedef std::set<CalendarEvent*> CalendarEventStore;
/// 事件邀请映射类型（事件ID -> 邀请列表）
typedef std::map<uint64 /* eventId */, CalendarInviteStore > CalendarEventInviteStore;

/**
 * @class CalendarMgr
 * @brief 日历系统管理器类（单例模式）
 *
 * 负责管理整个游戏世界的日历系统，包括：
 * - 所有日历事件的创建、查询、修改、删除
 * - 事件邀请的发送和管理
 * - 数据库持久化操作
 * - 网络包发送和接收
 * - ID分配和回收
 * - 过期事件清理
 *
 * 单例模式实现，通过 sCalendarMgr 宏访问全局实例
 */
class TC_GAME_API CalendarMgr
{
    private:
        /// 私有构造函数（单例模式）
        CalendarMgr();
        /// 私有析构函数
        ~CalendarMgr();

        CalendarEventStore _events;            ///< 所有日历事件集合
        CalendarEventInviteStore _invites;     ///< 事件ID到邀请列表的映射

        std::deque<uint64> _freeEventIds;      ///< 可重用的事件ID队列
        std::deque<uint64> _freeInviteIds;     ///< 可重用的邀请ID队列
        uint64 _maxEventId;                    ///< 当前最大事件ID
        uint64 _maxInviteId;                   ///< 当前最大邀请ID

    public:
        CalendarMgr(CalendarMgr const&) = delete;       ///< 禁用拷贝构造
        CalendarMgr(CalendarMgr&&) = delete;            ///< 禁用移动构造

        CalendarMgr& operator=(CalendarMgr const&) = delete;  ///< 禁用拷贝赋值
        CalendarMgr& operator=(CalendarMgr&&) = delete;       ///< 禁用移动赋值

        /**
         * @brief 获取单例实例
         * @return CalendarMgr单例指针
         *
         * 使用静态局部变量实现线程安全的单例模式
         */
        static CalendarMgr* instance();

        /**
         * @brief 从数据库加载日历数据
         *
         * 在服务器启动时调用，加载所有日历事件和邀请数据
         * 同时初始化ID回收队列
         */
        void LoadFromDB();

        /**
         * @brief 根据ID获取事件
         * @param eventId 事件ID
         * @return 事件指针，未找到返回nullptr
         */
        CalendarEvent* GetEvent(uint64 eventId) const;

        /**
         * @brief 获取所有事件
         * @return 事件集合的常引用
         */
        CalendarEventStore const& GetEvents() const { return _events; }

        /**
         * @brief 获取指定玩家创建的事件
         * @param guid 玩家GUID
         * @param includeGuildEvents 是否包含公会事件
         * @return 事件集合
         */
        CalendarEventStore GetEventsCreatedBy(ObjectGuid guid, bool includeGuildEvents = false) const;

        /**
         * @brief 获取玩家参与的所有事件
         * @param guid 玩家GUID
         * @return 事件集合
         *
         * 包括玩家被邀请的事件和玩家所在公会的事件
         */
        CalendarEventStore GetPlayerEvents(ObjectGuid guid) const;

        /**
         * @brief 获取公会的所有事件
         * @param guildId 公会ID
         * @return 事件集合
         */
        CalendarEventStore GetGuildEvents(ObjectGuid::LowType guildId) const;

        /**
         * @brief 根据ID获取邀请
         * @param inviteId 邀请ID
         * @return 邀请指针，未找到返回nullptr
         */
        CalendarInvite* GetInvite(uint64 inviteId) const;

        /**
         * @brief 获取所有邀请
         * @return 邀请映射的常引用
         */
        CalendarEventInviteStore const& GetInvites() const { return _invites; }

        /**
         * @brief 获取事件的所有邀请
         * @param eventId 事件ID
         * @return 邀请列表
         */
        CalendarInviteStore GetEventInvites(uint64 eventId) const;

        /**
         * @brief 获取玩家的所有邀请
         * @param guid 玩家GUID
         * @return 邀请列表
         */
        CalendarInviteStore GetPlayerInvites(ObjectGuid guid) const;

        /**
         * @brief 释放事件ID到回收队列
         * @param id 要释放的事件ID
         */
        void FreeEventId(uint64 id);

        /**
         * @brief 获取可用的事件ID
         * @return 可用的事件ID
         *
         * 优先使用回收队列中的ID，队列为空则使用新ID
         */
        uint64 GetFreeEventId();

        /**
         * @brief 释放邀请ID到回收队列
         * @param id 要释放的邀请ID
         */
        void FreeInviteId(uint64 id);

        /**
         * @brief 获取可用的邀请ID
         * @return 可用的邀请ID
         *
         * 优先使用回收队列中的ID，队列为空则使用新ID
         */
        uint64 GetFreeInviteId();

        /**
         * @brief 删除过期事件
         *
         * 删除超过 CALENDAR_OLD_EVENTS_DELETION_TIME 的旧事件
         * 通常由定时任务调用
         */
        void DeleteOldEvents();

        /**
         * @brief 获取玩家待处理邀请数量
         * @param guid 玩家GUID
         * @return 待处理的邀请数量
         *
         * 统计状态为 INVITED、TENTATIVE、NOT_SIGNED_UP 的邀请数量
         */
        uint32 GetPlayerNumPending(ObjectGuid guid);

        /**
         * @brief 添加新事件
         * @param calendarEvent 事件指针
         * @param sendType 发送类型（添加/复制）
         *
         * 将事件添加到管理器并保存到数据库，然后通知客户端
         */
        void AddEvent(CalendarEvent* calendarEvent, CalendarSendEventType sendType);

        /**
         * @brief 删除事件（通过ID）
         * @param eventId 事件ID
         * @param remover 删除者GUID
         *
         * 删除事件并通知所有相关玩家
         */
        void RemoveEvent(uint64 eventId, ObjectGuid remover);

        /**
         * @brief 删除事件（通过指针）
         * @param calendarEvent 事件指针
         * @param remover 删除者GUID
         *
         * 删除事件、所有相关邀请，并通知相关玩家
         * 同时发送邮件通知被邀请者
         */
        void RemoveEvent(CalendarEvent* calendarEvent, ObjectGuid remover);

        /**
         * @brief 更新事件到数据库
         * @param calendarEvent 要更新的事件
         */
        void UpdateEvent(CalendarEvent* calendarEvent);

        /**
         * @brief 添加邀请
         * @param calendarEvent 关联的事件
         * @param invite 邀请对象
         * @param trans 数据库事务（可选）
         *
         * 发送邀请通知给相关玩家并保存到数据库
         */
        void AddInvite(CalendarEvent* calendarEvent, CalendarInvite* invite, CharacterDatabaseTransaction trans = nullptr);

        /**
         * @brief 删除邀请
         * @param inviteId 邀请ID
         * @param eventId 事件ID
         * @param remover 删除者GUID
         *
         * 删除邀请并通知相关玩家
         */
        void RemoveInvite(uint64 inviteId, uint64 eventId, ObjectGuid remover);

        /**
         * @brief 更新邀请到数据库
         * @param invite 要更新的邀请
         * @param trans 数据库事务（可选）
         */
        void UpdateInvite(CalendarInvite* invite, CharacterDatabaseTransaction trans = nullptr);

        /**
         * @brief 删除玩家的所有事件和邀请
         * @param guid 玩家GUID
         *
         * 当删除角色时调用
         */
        void RemoveAllPlayerEventsAndInvites(ObjectGuid guid);

        /**
         * @brief 删除玩家的公会事件和报名
         * @param guid 玩家GUID
         * @param guildId 公会ID
         *
         * 当玩家退出公会时调用
         */
        void RemovePlayerGuildEventsAndSignups(ObjectGuid guid, ObjectGuid::LowType guildId);

        /**
         * @brief 发送日历事件数据给玩家
         * @param guid 目标玩家GUID
         * @param calendarEvent 事件对象
         * @param sendType 发送类型
         */
        void SendCalendarEvent(ObjectGuid guid, CalendarEvent const& calendarEvent, CalendarSendEventType sendType) const;

        /**
         * @brief 发送邀请通知给事件相关人员
         * @param invite 邀请对象
         *
         * 通知事件的创建者和其他管理员有新的邀请
         */
        void SendCalendarEventInvite(CalendarInvite const& invite) const;

        /**
         * @brief 发送邀请提醒给被邀请者
         * @param calendarEvent 事件对象
         * @param invite 邀请对象
         *
         * 对于公会事件，广播给所有公会成员
         */
        void SendCalendarEventInviteAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const;

        /**
         * @brief 发送邀请移除通知
         * @param calendarEvent 事件对象
         * @param invite 被移除的邀请
         * @param flags 事件标志
         */
        void SendCalendarEventInviteRemove(CalendarEvent const& calendarEvent, CalendarInvite const& invite, uint32 flags) const;

        /**
         * @brief 发送邀请移除提醒给被邀请者
         * @param guid 被邀请者GUID
         * @param calendarEvent 事件对象
         * @param status 邀请状态
         */
        void SendCalendarEventInviteRemoveAlert(ObjectGuid guid, CalendarEvent const& calendarEvent, CalendarInviteStatus status) const;

        /**
         * @brief 发送事件更新通知
         * @param calendarEvent 更新后的事件
         * @param originalDate 原始时间
         *
         * 通知所有相关人员事件已更新
         */
        void SendCalendarEventUpdateAlert(CalendarEvent const& calendarEvent, time_t originalDate) const;

        /**
         * @brief 发送邀请状态更新通知
         * @param calendarEvent 事件对象
         * @param invite 邀请对象
         *
         * 当被邀请者修改响应状态时通知其他人
         */
        void SendCalendarEventStatus(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const;

        /**
         * @brief 发送事件删除提醒
         * @param calendarEvent 被删除的事件
         *
         * 通知所有相关人员事件已被删除
         */
        void SendCalendarEventRemovedAlert(CalendarEvent const& calendarEvent) const;

        /**
         * @brief 发送管理员状态变更通知
         * @param calendarEvent 事件对象
         * @param invite 邀请对象
         *
         * 当玩家的权限等级变更时通知相关人员
         */
        void SendCalendarEventModeratorStatusAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const;

        /**
         * @brief 发送清除待处理操作命令
         * @param guid 目标玩家GUID
         */
        void SendCalendarClearPendingAction(ObjectGuid guid) const;

        /**
         * @brief 发送日历命令执行结果
         * @param guid 目标玩家GUID
         * @param err 错误码
         * @param param 附加参数（可选）
         */
        void SendCalendarCommandResult(ObjectGuid guid, CalendarError err, char const* param = nullptr) const;

        /**
         * @brief 发送数据包给所有事件相关人员
         * @param packet 要发送的数据包
         * @param calendarEvent 事件对象
         */
        void SendPacketToAllEventRelatives(WorldPacket const* packet, CalendarEvent const& calendarEvent) const;

        /**
         * @brief 获取所有事件相关人员列表
         * @param calendarEvent 事件对象
         * @return 相关玩家列表
         *
         * 包括：公会成员（公会事件）、所有被邀请者
         */
        std::vector<Player*> GetAllEventRelatives(CalendarEvent const& calendarEvent) const;
};

#define sCalendarMgr CalendarMgr::instance()

#endif
