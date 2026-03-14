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
 * @file CalendarMgr.cpp
 * @brief 日历系统管理模块实现文件
 *
 * 本文件实现了游戏内日历系统的核心功能，包括：
 * - 事件和邀请的数据库加载与保存
 * - 事件的创建、修改、删除操作
 * - 邀请的发送、回复、移除操作
 * - 与客户端的网络通信
 * - ID分配和回收机制
 * - 过期事件的自动清理
 *
 * 实现细节：
 * - 使用单例模式管理全局日历状态
 * - 支持公会事件和个人事件
 * - 提供完整的网络包通信接口
 */

#include "CalendarMgr.h"
#include "CalendarPackets.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Log.h"
#include "Mail.h"
#include "MapUtils.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringConvert.h"
#include "WorldSession.h"
#include "WowTime.h"

/**
 * @brief CalendarInvite 默认构造函数
 *
 * 初始化邀请对象的所有字段为默认值：
 * - inviteId = 1（临时值）
 * - status = INVITED（等待回复）
 * - rank = PLAYER（普通参与者）
 */
CalendarInvite::CalendarInvite() : _inviteId(1), _eventId(0), _invitee(), _senderGUID(), _responseTime(0),
_status(CALENDAR_STATUS_INVITED), _rank(CALENDAR_RANK_PLAYER), _note() { }

/**
 * @brief CalendarInvite 析构函数
 *
 * 自动释放邀请ID到回收队列
 * 注意：只对有效的邀请（inviteId != 0 且 eventId != 0）进行回收
 */
CalendarInvite::~CalendarInvite()
{
    // 仅当是真实邀请而非预邀请或公会公告时才释放ID
    if (_inviteId != 0 && _eventId != 0)
        sCalendarMgr->FreeInviteId(_inviteId);
}

/**
 * @brief CalendarEvent 析构函数
 *
 * 自动释放事件ID到回收队列
 */
CalendarEvent::~CalendarEvent()
{
    sCalendarMgr->FreeEventId(_eventId);
}

/**
 * @brief CalendarMgr 构造函数
 *
 * 初始化最大ID为0，后续由LoadFromDB加载实际值
 */
CalendarMgr::CalendarMgr() : _maxEventId(0), _maxInviteId(0) { }

/**
 * @brief CalendarMgr 析构函数
 *
 * 清理所有事件和邀请对象，释放内存
 */
CalendarMgr::~CalendarMgr()
{
    // 删除所有事件对象
    for (CalendarEventStore::iterator itr = _events.begin(); itr != _events.end(); ++itr)
        delete *itr;

    // 删除所有邀请对象
    for (CalendarEventInviteStore::iterator itr = _invites.begin(); itr != _invites.end(); ++itr)
        for (CalendarInviteStore::iterator itr2 = itr->second.begin(); itr2 != itr->second.end(); ++itr2)
            delete *itr2;
}

/**
 * @brief 获取CalendarMgr单例实例
 * @return 单例指针
 *
 * 使用C++11静态局部变量实现线程安全的单例模式
 * 在首次调用时构造，后续调用直接返回
 */
CalendarMgr* CalendarMgr::instance()
{
    static CalendarMgr instance;
    return &instance;
}

/**
 * @brief 从数据库加载日历数据
 *
 * 在服务器启动时调用，执行以下操作：
 * 1. 加载所有日历事件数据
 * 2. 加载所有邀请数据
 * 3. 确定最大事件ID和邀请ID
 * 4. 构建ID回收队列（用于ID重用）
 *
 * 性能注意：
 * - 一次性加载所有数据到内存
 * - 使用SELECT查询全表，数据量大时可能较慢
 * - 内存占用与事件和邀请数量成正比
 */
void CalendarMgr::LoadFromDB()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = 0;
    _maxEventId = 0;
    _maxInviteId = 0;

    // 加载日历事件数据
    // SQL字段: 0=id, 1=creator, 2=title, 3=description, 4=type, 5=dungeon, 6=eventtime, 7=flags, 8=time2
    if (QueryResult result = CharacterDatabase.Query("SELECT id, creator, title, description, type, dungeon, eventtime, flags, time2 FROM calendar_events"))
        do
        {
            Field* fields = result->Fetch();

            uint64 eventID          = fields[0].GetUInt64();
            ObjectGuid ownerGUID    = ObjectGuid::Create<HighGuid::Player>(fields[1].GetUInt32());
            std::string title       = fields[2].GetString();
            std::string description = fields[3].GetString();
            CalendarEventType type  = CalendarEventType(fields[4].GetUInt8());
            int32 textureID         = fields[5].GetInt32();
            time_t date             = fields[6].GetUInt32();
            uint32 flags            = fields[7].GetUInt32();
            time_t lockDate         = fields[8].GetUInt32();
            ObjectGuid::LowType guildID = UI64LIT(0);

            // 对于公会事件或公会公告，获取创建者的公会ID
            if (flags & CALENDAR_FLAG_GUILD_EVENT || flags & CALENDAR_FLAG_WITHOUT_INVITES)
                guildID = sCharacterCache->GetCharacterGuildIdByGuid(ownerGUID);

            // 创建事件对象并加入集合
            CalendarEvent* calendarEvent = new CalendarEvent(eventID, ownerGUID, guildID, type, textureID, date, flags, title, description, lockDate);
            _events.insert(calendarEvent);

            // 更新最大事件ID
            _maxEventId = std::max(_maxEventId, eventID);

            ++count;
        }
        while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} calendar events in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    count = 0;
    oldMSTime = getMSTime();

    // 加载日历邀请数据
    // SQL字段: 0=id, 1=event, 2=invitee, 3=sender, 4=status, 5=statustime, 6=rank, 7=text
    if (QueryResult result = CharacterDatabase.Query("SELECT id, event, invitee, sender, status, statustime, `rank`, text FROM calendar_invites"))
        do
        {
            Field* fields = result->Fetch();

            uint64 inviteId             = fields[0].GetUInt64();
            uint64 eventId              = fields[1].GetUInt64();
            ObjectGuid invitee          = ObjectGuid::Create<HighGuid::Player>(fields[2].GetUInt32());
            ObjectGuid senderGUID       = ObjectGuid::Create<HighGuid::Player>(fields[3].GetUInt32());
            CalendarInviteStatus status = CalendarInviteStatus(fields[4].GetUInt8());
            time_t responseTime         = fields[5].GetUInt32();
            CalendarModerationRank rank = CalendarModerationRank(fields[6].GetUInt8());
            std::string note            = fields[7].GetString();

            // 创建邀请对象并加入映射
            CalendarInvite* invite = new CalendarInvite(inviteId, eventId, invitee, senderGUID, responseTime, status, rank, note);
            _invites[eventId].push_back(invite);

            // 更新最大邀请ID
            _maxInviteId = std::max(_maxInviteId, inviteId);

            ++count;
        }
        while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} calendar invites in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

    // 构建事件ID回收队列
    // 找出所有未使用的ID（数据库中删除后留下的空隙）
    for (uint64 i = 1; i < _maxEventId; ++i)
        if (!GetEvent(i))
            _freeEventIds.push_back(i);

    // 构建邀请ID回收队列
    for (uint64 i = 1; i < _maxInviteId; ++i)
        if (!GetInvite(i))
            _freeInviteIds.push_back(i);
}

/**
 * @brief 添加新事件到管理器
 * @param calendarEvent 事件指针
 * @param sendType 发送类型
 *
 * 执行流程：
 * 1. 将事件加入内存集合
 * 2. 保存事件到数据库
 * 3. 发送事件数据给创建者客户端
 */
void CalendarMgr::AddEvent(CalendarEvent* calendarEvent, CalendarSendEventType sendType)
{
    _events.insert(calendarEvent);
    UpdateEvent(calendarEvent);
    SendCalendarEvent(calendarEvent->GetOwnerGUID(), *calendarEvent, sendType);
}

/**
 * @brief 添加邀请到事件
 * @param calendarEvent 关联的事件
 * @param invite 邀请对象
 * @param trans 数据库事务（可选）
 *
 * 执行流程：
 * 1. 发送邀请通知给事件相关人员
 * 2. 发送邀请提醒给被邀请者（公会事件广播给全体成员）
 * 3. 将邀请加入内存映射并保存到数据库
 *
 * 注意：公会公告不保存邀请记录
 */
void CalendarMgr::AddInvite(CalendarEvent* calendarEvent, CalendarInvite* invite, CharacterDatabaseTransaction trans)
{
    // 公会公告不发送邀请通知
    if (!calendarEvent->IsGuildAnnouncement())
        SendCalendarEventInvite(*invite);

    // 发送邀请提醒
    // 对于公会事件，只有创建者收到提醒；对于个人事件，被邀请者收到提醒
    if (!calendarEvent->IsGuildEvent() || invite->GetInviteeGUID() == calendarEvent->GetOwnerGUID())
        SendCalendarEventInviteAlert(*calendarEvent, *invite);

    // 公会公告不保存邀请记录
    if (!calendarEvent->IsGuildAnnouncement())
    {
        _invites[invite->GetEventId()].push_back(invite);
        UpdateInvite(invite, trans);
    }
}

/**
 * @brief 删除事件（通过ID）
 * @param eventId 事件ID
 * @param remover 删除者GUID
 *
 * 先验证事件存在，然后调用RemoveEvent删除
 */
void CalendarMgr::RemoveEvent(uint64 eventId, ObjectGuid remover)
{
    CalendarEvent* calendarEvent = GetEvent(eventId);

    if (!calendarEvent)
    {
        SendCalendarCommandResult(remover, CALENDAR_ERROR_EVENT_INVALID);
        return;
    }

    RemoveEvent(calendarEvent, remover);
}

/**
 * @brief 删除事件（核心实现）
 * @param calendarEvent 事件指针
 * @param remover 删除者GUID
 *
 * 执行流程：
 * 1. 发送事件删除提醒给所有相关人员
 * 2. 删除所有相关邀请并通知被邀请者
 * 3. 给非删除者的被邀请者发送邮件通知
 * 4. 从数据库删除事件和所有邀请
 * 5. 从内存移除事件对象
 *
 * 性能注意：
 * - 使用数据库事务批量删除
 * - 可能发送大量邮件，耗时较长
 */
void CalendarMgr::RemoveEvent(CalendarEvent* calendarEvent, ObjectGuid remover)
{
    if (!calendarEvent)
    {
        SendCalendarCommandResult(remover, CALENDAR_ERROR_EVENT_INVALID);
        return;
    }

    // 通知所有相关人员事件已删除
    SendCalendarEventRemovedAlert(*calendarEvent);

    // 使用数据库事务批量删除
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt;

    // 删除所有邀请
    CalendarInviteStore& eventInvites = _invites[calendarEvent->GetEventId()];
    for (size_t i = 0; i < eventInvites.size(); ++i)
    {
        CalendarInvite* invite = eventInvites[i];

        // 添加删除邀请的SQL语句到事务
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CALENDAR_INVITE);
        stmt->setUInt64(0, invite->GetInviteId());
        trans->Append(stmt);

        // 发送邮件通知被邀请者（删除者不发给自己的逻辑）
        // 注：公会事件可能需要检查邀请状态
        if (!remover.IsEmpty() && invite->GetInviteeGUID() != remover)
        {
            MailDraft mail(calendarEvent->BuildCalendarMailSubject(remover), calendarEvent->BuildCalendarMailBody(ObjectAccessor::FindConnectedPlayer(invite->GetInviteeGUID())));
            mail.SendMailTo(trans, MailReceiver(invite->GetInviteeGUID().GetCounter()), calendarEvent, MAIL_CHECK_MASK_COPIED);
        }

        delete invite;
    }

    // 从映射中移除事件的所有邀请
    _invites.erase(calendarEvent->GetEventId());

    // 添加删除事件的SQL语句到事务
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CALENDAR_EVENT);
    stmt->setUInt64(0, calendarEvent->GetEventId());
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    // 从内存集合移除事件
    _events.erase(calendarEvent);
    delete calendarEvent;
}

/**
 * @brief 删除邀请
 * @param inviteId 邀请ID
 * @param eventId 事件ID
 * @param remover 删除者GUID（未使用）
 *
 * 执行流程：
 * 1. 查找邀请在列表中的位置
 * 2. 从数据库删除邀请
 * 3. 发送邀请移除通知
 * 4. 从内存移除邀请对象
 */
void CalendarMgr::RemoveInvite(uint64 inviteId, uint64 eventId, ObjectGuid /*remover*/)
{
    CalendarEvent* calendarEvent = GetEvent(eventId);

    if (!calendarEvent)
        return;

    // 在邀请列表中查找指定ID的邀请
    CalendarInviteStore::iterator itr = _invites[eventId].begin();
    for (; itr != _invites[eventId].end(); ++itr)
        if ((*itr)->GetInviteId() == inviteId)
            break;

    if (itr == _invites[eventId].end())
        return;

    // 从数据库删除邀请
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CALENDAR_INVITE);
    stmt->setUInt64(0, (*itr)->GetInviteId());
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    // 发送邀请移除提醒给被邀请者（公会事件不发送）
    if (!calendarEvent->IsGuildEvent())
        SendCalendarEventInviteRemoveAlert((*itr)->GetInviteeGUID(), *calendarEvent, CALENDAR_STATUS_REMOVED);

    // 发送邀请移除通知给事件相关人员
    SendCalendarEventInviteRemove(*calendarEvent, **itr, calendarEvent->GetFlags());

    // TODO: 需要研究如何使用CALENDAR_INVITE_REMOVED_MAIL_SUBJECT让客户端显示不同的邮件
    //if ((*itr)->GetInviteeGUID() != remover)
    //    MailDraft(calendarEvent->BuildCalendarMailSubject(remover), calendarEvent->BuildCalendarMailBody())
    //        .SendMailTo(trans, MailReceiver((*itr)->GetInvitee()), calendarEvent, MAIL_CHECK_MASK_COPIED);

    // 释放内存并从列表移除
    delete *itr;
    _invites[eventId].erase(itr);
}

/**
 * @brief 更新事件到数据库
 * @param calendarEvent 要更新的事件
 *
 * 使用REPLACE语句，如果事件不存在则插入，存在则更新
 */
void CalendarMgr::UpdateEvent(CalendarEvent* calendarEvent)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CALENDAR_EVENT);
    stmt->setUInt64(0, calendarEvent->GetEventId());
    stmt->setUInt32(1, calendarEvent->GetOwnerGUID().GetCounter());
    stmt->setString(2, calendarEvent->GetTitle());
    stmt->setString(3, calendarEvent->GetDescription());
    stmt->setUInt8(4, calendarEvent->GetType());
    stmt->setInt32(5, calendarEvent->GetTextureId());
    stmt->setUInt32(6, calendarEvent->GetDate());
    stmt->setUInt32(7, calendarEvent->GetFlags());
    stmt->setUInt32(8, calendarEvent->GetLockDate());
    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 更新邀请到数据库
 * @param invite 要更新的邀请
 * @param trans 数据库事务（可选）
 *
 * 使用REPLACE语句，如果邀请不存在则插入，存在则更新
 * 如果提供了事务，则追加到事务中；否则立即执行
 */
void CalendarMgr::UpdateInvite(CalendarInvite* invite, CharacterDatabaseTransaction trans)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CALENDAR_INVITE);
    stmt->setUInt64(0, invite->GetInviteId());
    stmt->setUInt64(1, invite->GetEventId());
    stmt->setUInt32(2, invite->GetInviteeGUID().GetCounter());
    stmt->setUInt32(3, invite->GetSenderGUID().GetCounter());
    stmt->setUInt8(4, invite->GetStatus());
    stmt->setUInt32(5, invite->GetResponseTime());
    stmt->setUInt8(6, invite->GetRank());
    stmt->setString(7, invite->GetNote());
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

/**
 * @brief 删除玩家的所有事件和邀请
 * @param guid 玩家GUID
 *
 * 当删除角色时调用，清理该玩家创建的所有事件和收到的所有邀请
 * 注意：不发送邮件通知，因为角色已被删除
 */
void CalendarMgr::RemoveAllPlayerEventsAndInvites(ObjectGuid guid)
{
    // 删除玩家创建的所有事件（不发送邮件）
    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end();)
    {
        CalendarEvent* event = *itr;
        ++itr;
        if (event->GetOwnerGUID() == guid)
            RemoveEvent(event, ObjectGuid::Empty); // 空GUID表示不发送邮件
    }

    // 删除玩家收到的所有邀请
    CalendarInviteStore playerInvites = GetPlayerInvites(guid);
    for (CalendarInviteStore::const_iterator itr = playerInvites.begin(); itr != playerInvites.end(); ++itr)
        RemoveInvite((*itr)->GetInviteId(), (*itr)->GetEventId(), guid);
}

/**
 * @brief 删除玩家的公会事件和报名
 * @param guid 玩家GUID
 * @param guildId 公会ID
 *
 * 当玩家退出公会时调用，清理：
 * 1. 玩家创建的公会事件和公会公告
 * 2. 玩家在该公会事件中的邀请记录
 */
void CalendarMgr::RemovePlayerGuildEventsAndSignups(ObjectGuid guid, ObjectGuid::LowType guildId)
{
    // 删除玩家创建的公会事件和公告
    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end(); ++itr)
        if ((*itr)->GetOwnerGUID() == guid && ((*itr)->IsGuildEvent() || (*itr)->IsGuildAnnouncement()))
            RemoveEvent((*itr)->GetEventId(), guid);

    // 删除玩家在公会事件中的邀请记录
    CalendarInviteStore playerInvites = GetPlayerInvites(guid);
    for (CalendarInviteStore::const_iterator itr = playerInvites.begin(); itr != playerInvites.end(); ++itr)
        if (CalendarEvent* calendarEvent = GetEvent((*itr)->GetEventId()))
            if (calendarEvent->IsGuildEvent() && calendarEvent->GetGuildId() == guildId)
                RemoveInvite((*itr)->GetInviteId(), (*itr)->GetEventId(), guid);
}

/**
 * @brief 根据ID获取事件
 * @param eventId 事件ID
 * @return 事件指针，未找到返回nullptr
 *
 * 线性搜索事件集合，性能与事件数量成正比
 */
CalendarEvent* CalendarMgr::GetEvent(uint64 eventId) const
{
    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end(); ++itr)
        if ((*itr)->GetEventId() == eventId)
            return *itr;

    TC_LOG_DEBUG("calendar", "CalendarMgr::GetEvent: [{}] not found!", eventId);
    return nullptr;
}

/**
 * @brief 根据ID获取邀请
 * @param inviteId 邀请ID
 * @return 邀请指针，未找到返回nullptr
 *
 * 双层循环搜索所有邀请，性能与邀请总数成正比
 * 性能注意：数据量大时应考虑优化数据结构
 */
CalendarInvite* CalendarMgr::GetInvite(uint64 inviteId) const
{
    for (CalendarEventInviteStore::const_iterator itr = _invites.begin(); itr != _invites.end(); ++itr)
        for (CalendarInviteStore::const_iterator itr2 = itr->second.begin(); itr2 != itr->second.end(); ++itr2)
            if ((*itr2)->GetInviteId() == inviteId)
                return *itr2;

    TC_LOG_DEBUG("calendar", "CalendarMgr::GetInvite: [{}] not found!", inviteId);
    return nullptr;
}

/**
 * @brief 释放事件ID到回收队列
 * @param id 要释放的事件ID
 *
 * 如果是最大ID，则直接减少max计数；否则加入回收队列等待重用
 */
void CalendarMgr::FreeEventId(uint64 id)
{
    if (id == _maxEventId)
        --_maxEventId;
    else
        _freeEventIds.push_back(id);
}

/**
 * @brief 获取可用的事件ID
 * @return 可用的事件ID
 *
 * 优先使用回收队列中的ID，队列为空则分配新ID
 */
uint64 CalendarMgr::GetFreeEventId()
{
    if (_freeEventIds.empty())
        return ++_maxEventId;

    uint64 eventId = _freeEventIds.front();
    _freeEventIds.pop_front();
    return eventId;
}

/**
 * @brief 释放邀请ID到回收队列
 * @param id 要释放的邀请ID
 *
 * 如果是最大ID，则直接减少max计数；否则加入回收队列等待重用
 */
void CalendarMgr::FreeInviteId(uint64 id)
{
    if (id == _maxInviteId)
        --_maxInviteId;
    else
        _freeInviteIds.push_back(id);
}

/**
 * @brief 获取可用的邀请ID
 * @return 可用的邀请ID
 *
 * 优先使用回收队列中的ID，队列为空则分配新ID
 */
uint64 CalendarMgr::GetFreeInviteId()
{
    if (_freeInviteIds.empty())
        return ++_maxInviteId;

    uint64 inviteId = _freeInviteIds.front();
    _freeInviteIds.pop_front();
    return inviteId;
}

/**
 * @brief 删除过期事件
 *
 * 删除所有开始时间早于当前时间减去旧事件删除时间的事件
 * 通常由定时任务定期调用
 *
 * 删除时间常量：CALENDAR_OLD_EVENTS_DELETION_TIME（1个月）
 */
void CalendarMgr::DeleteOldEvents()
{
    time_t oldEventsTime = GameTime::GetGameTime() - CALENDAR_OLD_EVENTS_DELETION_TIME;

    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end();)
    {
        CalendarEvent* event = *itr;
        ++itr;
        if (event->GetDate() < oldEventsTime)
            RemoveEvent(event, ObjectGuid::Empty);
    }
}

/**
 * @brief 获取指定玩家创建的事件
 * @param guid 玩家GUID
 * @param includeGuildEvents 是否包含公会事件
 * @return 事件集合
 *
 * 如果不包含公会事件，则只返回个人事件
 */
CalendarEventStore CalendarMgr::GetEventsCreatedBy(ObjectGuid guid, bool includeGuildEvents) const
{
    CalendarEventStore result;
    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end(); ++itr)
        if ((*itr)->GetOwnerGUID() == guid && (includeGuildEvents || (!(*itr)->IsGuildEvent() && !(*itr)->IsGuildAnnouncement())))
            result.insert(*itr);

    return result;
}

/**
 * @brief 获取公会的所有事件
 * @param guildId 公会ID
 * @return 事件集合
 *
 * 只返回公会事件和公会公告
 */
CalendarEventStore CalendarMgr::GetGuildEvents(ObjectGuid::LowType guildId) const
{
    CalendarEventStore result;

    if (!guildId)
        return result;

    for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end(); ++itr)
        if ((*itr)->IsGuildEvent() || (*itr)->IsGuildAnnouncement())
            if ((*itr)->GetGuildId() == guildId)
                result.insert(*itr);

    return result;
}

/**
 * @brief 获取玩家参与的所有事件
 * @param guid 玩家GUID
 * @return 事件集合
 *
 * 包括：
 * 1. 玩家被邀请的所有事件
 * 2. 玩家所在公会的所有公会事件
 */
CalendarEventStore CalendarMgr::GetPlayerEvents(ObjectGuid guid) const
{
    CalendarEventStore events;

    // 查找玩家被邀请的所有事件
    for (CalendarEventInviteStore::const_iterator itr = _invites.begin(); itr != _invites.end(); ++itr)
        for (CalendarInviteStore::const_iterator itr2 = itr->second.begin(); itr2 != itr->second.end(); ++itr2)
            if ((*itr2)->GetInviteeGUID() == guid)
                if (CalendarEvent* event = GetEvent(itr->first)) // NULL检查，修复#11512问题
                    events.insert(event);

    // 查找玩家所在公会的所有事件
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        if (player->GetGuildId())
            for (CalendarEventStore::const_iterator itr = _events.begin(); itr != _events.end(); ++itr)
                if ((*itr)->GetGuildId() == player->GetGuildId())
                    events.insert(*itr);

    return events;
}

/**
 * @brief 获取事件的所有邀请
 * @param eventId 事件ID
 * @return 邀请列表
 *
 * 使用MapGetValuePtr安全获取，不存在返回空列表
 */
CalendarInviteStore CalendarMgr::GetEventInvites(uint64 eventId) const
{
    CalendarInviteStore invites;
    if (CalendarInviteStore const* invitesStore = Trinity::Containers::MapGetValuePtr(_invites, eventId))
        invites = *invitesStore;

    return invites;
}

/**
 * @brief 获取玩家的所有邀请
 * @param guid 玩家GUID
 * @return 邀请列表
 *
 * 遍历所有事件的邀请列表，找出该玩家的邀请
 */
CalendarInviteStore CalendarMgr::GetPlayerInvites(ObjectGuid guid) const
{
    CalendarInviteStore invites;

    for (CalendarEventInviteStore::const_iterator itr = _invites.begin(); itr != _invites.end(); ++itr)
        for (CalendarInviteStore::const_iterator itr2 = itr->second.begin(); itr2 != itr->second.end(); ++itr2)
            if ((*itr2)->GetInviteeGUID() == guid)
                invites.push_back(*itr2);

    return invites;
}

/**
 * @brief 获取玩家待处理邀请数量
 * @param guid 玩家GUID
 * @return 待处理的邀请数量
 *
 * 统计以下状态的邀请：
 * - INVITED: 已邀请，等待回复
 * - TENTATIVE: 暂定参加
 * - NOT_SIGNED_UP: 未报名
 *
 * 这些状态表示玩家还未做出最终决定
 */
uint32 CalendarMgr::GetPlayerNumPending(ObjectGuid guid)
{
    CalendarInviteStore const& invites = GetPlayerInvites(guid);

    uint32 pendingNum = 0;
    for (CalendarInviteStore::const_iterator itr = invites.begin(); itr != invites.end(); ++itr)
    {
        switch ((*itr)->GetStatus())
        {
            case CALENDAR_STATUS_INVITED:
            case CALENDAR_STATUS_TENTATIVE:
            case CALENDAR_STATUS_NOT_SIGNED_UP:
                ++pendingNum;
                break;
            default:
                break;
        }
    }

    return pendingNum;
}

/**
 * @brief 构建日历邮件主题
 * @param remover 删除事件的人的GUID
 * @return 格式为 "GUID:事件标题" 的字符串
 *
 * 用于删除事件时发送邮件通知
 */
std::string CalendarEvent::BuildCalendarMailSubject(ObjectGuid remover) const
{
    return Trinity::StringFormat("{}:{}", remover.GetRawValue(), _title);
}

/**
 * @brief 构建日历邮件正文
 * @param invitee 被邀请者（用于时区转换）
 * @return 事件时间的打包字符串
 *
 * 将事件时间转换为被邀请者时区，然后打包为字符串格式
 */
std::string CalendarEvent::BuildCalendarMailBody(Player const* invitee) const
{
    WowTime time;
    time.SetUtcTimeFromUnixTime(_date);
    if (invitee)
        time += invitee->GetSession()->GetTimezoneOffset();

    return Trinity::ToString(time.GetPackedTime());
}

/**
 * @brief 发送邀请通知给事件相关人员
 * @param invite 邀请对象
 *
 * 当有新邀请时，通知：
 * - 预邀请：通知邀请发送者
 * - 正式邀请：通知事件所有者和管理员
 *
 * 不通知被邀请者自己
 */
void CalendarMgr::SendCalendarEventInvite(CalendarInvite const& invite) const
{
    CalendarEvent* calendarEvent = GetEvent(invite.GetEventId());

    ObjectGuid invitee = invite.GetInviteeGUID();
    Player* player = ObjectAccessor::FindConnectedPlayer(invitee);

    // 获取被邀请者等级（在线玩家优先，否则从缓存获取）
    uint8 level = player ? player->GetLevel() : sCharacterCache->GetCharacterLevelByGuid(invitee);

    // Lambda函数：构建并发送邀请数据包
    auto packetBuilder = [&](Player const* receiver)
    {
        WorldPackets::Calendar::CalendarInviteAdded packet;
        packet.EventID = calendarEvent ? calendarEvent->GetEventId() : 0;
        packet.InviteGuid = invitee;
        packet.InviteID = calendarEvent ? invite.GetInviteId() : 0;
        packet.Level = level;
        packet.ResponseTime.SetUtcTimeFromUnixTime(invite.GetResponseTime());
        packet.ResponseTime += receiver->GetSession()->GetTimezoneOffset();
        packet.Status = invite.GetStatus();
        packet.Type = calendarEvent ? calendarEvent->IsGuildEvent() : 0;
        packet.ClearPending = invite.GetSenderGUID() != invite.GetInviteeGUID();

        receiver->SendDirectMessage(packet.Write());
    };

    if (!calendarEvent) // 预邀请（事件尚未创建）
    {
        if (Player* playerSender = ObjectAccessor::FindConnectedPlayer(invite.GetSenderGUID()))
            packetBuilder(playerSender);
    }
    else
    {
        // 通知所有事件相关人员（除了被邀请者自己）
        if (calendarEvent->GetOwnerGUID() != invite.GetInviteeGUID())
            for (Player* receiver : GetAllEventRelatives(*calendarEvent))
                packetBuilder(receiver);
    }
}

/**
 * @brief 发送事件更新通知
 * @param calendarEvent 更新后的事件
 * @param originalDate 原始时间
 *
 * 通知所有相关人员事件信息已更新，包括时间、标题、描述等
 */
void CalendarMgr::SendCalendarEventUpdateAlert(CalendarEvent const& calendarEvent, time_t originalDate) const
{
    // Lambda函数：构建并发送更新通知包
    auto packetBuilder = [&](Player const* receiver)
    {
        WorldPackets::Calendar::CalendarEventUpdatedAlert packet;
        packet.ClearPending = calendarEvent.GetOwnerGUID() == receiver->GetGUID();
        packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
        packet.Date += receiver->GetSession()->GetTimezoneOffset();
        packet.Description = calendarEvent.GetDescription();
        packet.EventID = calendarEvent.GetEventId();
        packet.EventName = calendarEvent.GetTitle();
        packet.EventType = calendarEvent.GetType();
        packet.Flags = calendarEvent.GetFlags();
        packet.LockDate.SetUtcTimeFromUnixTime(calendarEvent.GetLockDate());
        if (calendarEvent.GetLockDate())
            packet.LockDate += receiver->GetSession()->GetTimezoneOffset();
        packet.OriginalDate.SetUtcTimeFromUnixTime(originalDate);
        packet.OriginalDate += receiver->GetSession()->GetTimezoneOffset();
        packet.TextureID = calendarEvent.GetTextureId();

        receiver->SendDirectMessage(packet.Write());
    };

    // 通知所有相关人员
    for (Player* receiver : GetAllEventRelatives(calendarEvent))
        packetBuilder(receiver);
}

/**
 * @brief 发送邀请状态更新通知
 * @param calendarEvent 事件对象
 * @param invite 邀请对象
 *
 * 当被邀请者修改响应状态（接受/拒绝等）时，通知其他参与者
 */
void CalendarMgr::SendCalendarEventStatus(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const
{
    // Lambda函数：构建并发送状态更新包
    auto packetBuilder = [&](Player const* receiver)
    {
        WorldPackets::Calendar::CalendarInviteStatus packet;
        packet.ClearPending = invite.GetInviteeGUID() == receiver->GetGUID();
        packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
        packet.Date += receiver->GetSession()->GetTimezoneOffset();
        packet.EventID = calendarEvent.GetEventId();
        packet.Flags = calendarEvent.GetFlags();
        packet.InviteGuid = invite.GetInviteeGUID();
        packet.ResponseTime.SetUtcTimeFromUnixTime(invite.GetResponseTime());
        packet.ResponseTime += receiver->GetSession()->GetTimezoneOffset();
        packet.Status = invite.GetStatus();

        receiver->SendDirectMessage(packet.Write());
    };

    // 通知所有相关人员
    for (Player* receiver : GetAllEventRelatives(calendarEvent))
        packetBuilder(receiver);
}

/**
 * @brief 发送事件删除提醒
 * @param calendarEvent 被删除的事件
 *
 * 通知所有相关人员事件已被删除
 */
void CalendarMgr::SendCalendarEventRemovedAlert(CalendarEvent const& calendarEvent) const
{
    // Lambda函数：构建并发送删除提醒包
    auto packetBuilder = [&](Player const* receiver)
    {
        WorldPackets::Calendar::CalendarEventRemovedAlert packet;
        packet.ClearPending = calendarEvent.GetOwnerGUID() == receiver->GetGUID();
        packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
        packet.Date += receiver->GetSession()->GetTimezoneOffset();
        packet.EventID = calendarEvent.GetEventId();

        receiver->SendDirectMessage(packet.Write());
    };

    // 通知所有相关人员
    for (Player* receiver : GetAllEventRelatives(calendarEvent))
        packetBuilder(receiver);
}

/**
 * @brief 发送邀请移除通知
 * @param calendarEvent 事件对象
 * @param invite 被移除的邀请
 * @param flags 事件标志
 *
 * 通知事件相关人员某个邀请已被移除
 */
void CalendarMgr::SendCalendarEventInviteRemove(CalendarEvent const& calendarEvent, CalendarInvite const& invite, uint32 flags) const
{
    WorldPackets::Calendar::CalendarInviteRemoved packet;
    packet.ClearPending = true; // FIXME: 待确认具体逻辑
    packet.EventID = calendarEvent.GetEventId();
    packet.Flags = flags;
    packet.InviteGuid = invite.GetInviteeGUID();

    SendPacketToAllEventRelatives(packet.Write(), calendarEvent);
}

/**
 * @brief 发送管理员状态变更通知
 * @param calendarEvent 事件对象
 * @param invite 邀请对象
 *
 * 当玩家的权限等级变更时通知相关人员
 */
void CalendarMgr::SendCalendarEventModeratorStatusAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const
{
    WorldPackets::Calendar::CalendarModeratorStatus packet;
    packet.ClearPending = true; // FIXME: 待确认具体逻辑
    packet.EventID = calendarEvent.GetEventId();
    packet.InviteGuid = invite.GetInviteeGUID();
    packet.Status = invite.GetStatus();

    SendPacketToAllEventRelatives(packet.Write(), calendarEvent);
}

/**
 * @brief 发送邀请提醒给被邀请者
 * @param calendarEvent 事件对象
 * @param invite 邀请对象
 *
 * 对于公会事件或公会公告：广播给所有公会成员
 * 对于个人事件：发送给被邀请者
 */
void CalendarMgr::SendCalendarEventInviteAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite) const
{
    // Lambda函数：构建并发送邀请提醒包
    auto packetBuilder = [&](Player const* receiver)
    {
        WorldPackets::Calendar::CalendarInviteAlert packet;
        packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
        packet.Date += receiver->GetSession()->GetTimezoneOffset();
        packet.EventID = calendarEvent.GetEventId();
        packet.EventName = calendarEvent.GetTitle();
        packet.EventType = calendarEvent.GetType();
        packet.Flags = calendarEvent.GetFlags();
        packet.InviteID = invite.GetInviteId();
        packet.InvitedByGuid = invite.GetSenderGUID();
        packet.ModeratorStatus = invite.GetRank();
        packet.OwnerGuid = calendarEvent.GetOwnerGUID();
        packet.Status = invite.GetStatus();
        packet.TextureID = calendarEvent.GetTextureId();

        receiver->SendDirectMessage(packet.Write());
    };

    // 公会事件或公告：广播给所有公会成员
    if (calendarEvent.IsGuildEvent() || calendarEvent.IsGuildAnnouncement())
    {
        if (Guild* guild = sGuildMgr->GetGuildById(calendarEvent.GetGuildId()))
            guild->BroadcastWorker(packetBuilder);
    }
    // 个人事件：发送给被邀请者
    else if (Player* player = ObjectAccessor::FindConnectedPlayer(invite.GetInviteeGUID()))
        packetBuilder(player);
}

/**
 * @brief 发送完整事件数据给玩家
 * @param guid 目标玩家GUID
 * @param calendarEvent 事件对象
 * @param sendType 发送类型
 *
 * 发送事件的完整信息，包括所有邀请列表
 * 客户端使用此数据更新日历界面
 */
void CalendarMgr::SendCalendarEvent(ObjectGuid guid, CalendarEvent const& calendarEvent, CalendarSendEventType sendType) const
{
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player)
        return;

    WorldPackets::Calendar::CalendarSendEvent packet;
    packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
    packet.Date += player->GetSession()->GetTimezoneOffset();
    packet.Description = calendarEvent.GetDescription();
    packet.EventID = calendarEvent.GetEventId();
    packet.EventName = calendarEvent.GetTitle();
    packet.EventType = sendType;
    packet.Flags = calendarEvent.GetFlags();
    packet.GetEventType = calendarEvent.GetType();
    packet.LockDate.SetUtcTimeFromUnixTime(calendarEvent.GetLockDate());
    if (calendarEvent.GetLockDate())
        packet.LockDate += player->GetSession()->GetTimezoneOffset();
    packet.OwnerGuid = calendarEvent.GetOwnerGUID();
    packet.TextureID = calendarEvent.GetTextureId();
    packet.EventGuildID = calendarEvent.GetGuildId();

    // 添加所有邀请信息
    if (CalendarInviteStore const* eventInviteeList = Trinity::Containers::MapGetValuePtr(_invites, calendarEvent.GetEventId()))
    {
        for (CalendarInvite const* calendarInvite : *eventInviteeList)
        {
            ObjectGuid inviteeGuid = calendarInvite->GetInviteeGUID();
            Player* invitee = ObjectAccessor::FindPlayer(inviteeGuid);

            // 获取被邀请者信息
            uint8 inviteeLevel = invitee ? invitee->GetLevel() : sCharacterCache->GetCharacterLevelByGuid(inviteeGuid);
            ObjectGuid::LowType inviteeGuildId = invitee ? invitee->GetGuildId() : sCharacterCache->GetCharacterGuildIdByGuid(inviteeGuid);

            // 构建邀请信息结构
            WorldPackets::Calendar::CalendarEventInviteInfo inviteInfo;
            inviteInfo.Guid = inviteeGuid;
            inviteInfo.Level = inviteeLevel;
            inviteInfo.Status = calendarInvite->GetStatus();
            inviteInfo.Moderator = calendarInvite->GetRank();
            inviteInfo.InviteType = calendarEvent.IsGuildEvent() && calendarEvent.GetGuildId() == inviteeGuildId;
            inviteInfo.InviteID = calendarInvite->GetInviteId();
            inviteInfo.ResponseTime.SetUtcTimeFromUnixTime(calendarInvite->GetResponseTime());
            inviteInfo.ResponseTime += player->GetSession()->GetTimezoneOffset();
            inviteInfo.Notes = calendarInvite->GetNote();

            packet.Invites.push_back(inviteInfo);
        }
    }

    player->SendDirectMessage(packet.Write());
}

/**
 * @brief 发送邀请移除提醒给被邀请者
 * @param guid 被邀请者GUID
 * @param calendarEvent 事件对象
 * @param status 邀请状态
 *
 * 通知被邀请者他的邀请已被移除
 */
void CalendarMgr::SendCalendarEventInviteRemoveAlert(ObjectGuid guid, CalendarEvent const& calendarEvent, CalendarInviteStatus status) const
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
    {
        WorldPackets::Calendar::CalendarInviteRemovedAlert packet;
        packet.Date.SetUtcTimeFromUnixTime(calendarEvent.GetDate());
        packet.Date += player->GetSession()->GetTimezoneOffset();
        packet.EventID = calendarEvent.GetEventId();
        packet.Flags = calendarEvent.GetFlags();
        packet.Status = status;

        player->SendDirectMessage(packet.Write());
    }
}

/**
 * @brief 发送清除待处理操作命令
 * @param guid 目标玩家GUID
 *
 * 清除客户端的待处理状态
 */
void CalendarMgr::SendCalendarClearPendingAction(ObjectGuid guid) const
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->SendDirectMessage(WorldPackets::Calendar::CalendarClearPendingAction().Write());
}

/**
 * @brief 发送日历命令执行结果
 * @param guid 目标玩家GUID
 * @param err 错误码
 * @param param 附加参数（可选）
 *
 * 发送操作结果给客户端，用于显示错误消息
 */
void CalendarMgr::SendCalendarCommandResult(ObjectGuid guid, CalendarError err, char const* param /*= nullptr*/) const
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
    {
        WorldPackets::Calendar::CalendarCommandResult packet;
        packet.Command = 1; // FIXME: 待确认具体含义
        packet.Result = err;

        // 某些错误需要附加参数（如玩家名字）
        switch (err)
        {
            case CALENDAR_ERROR_OTHER_INVITES_EXCEEDED:
            case CALENDAR_ERROR_ALREADY_INVITED_TO_EVENT_S:
            case CALENDAR_ERROR_IGNORING_YOU_S:
                packet.Name = param;
                break;
            default:
                break;
        }

        player->SendDirectMessage(packet.Write());
    }
}

/**
 * @brief 发送数据包给所有事件相关人员
 * @param packet 要发送的数据包
 * @param calendarEvent 事件对象
 *
 * 遍历所有相关人员并发送数据包
 */
void CalendarMgr::SendPacketToAllEventRelatives(WorldPacket const* packet, CalendarEvent const& calendarEvent) const
{
    for (Player* player : GetAllEventRelatives(calendarEvent))
        player->SendDirectMessage(packet);
}

/**
 * @brief 获取所有事件相关人员列表
 * @param calendarEvent 事件对象
 * @return 相关玩家列表
 *
 * 包括：
 * 1. 公会事件/公告：所有在线公会成员
 * 2. 所有被邀请者（公会事件排除已在公会中的成员，避免重复）
 *
 * 此函数用于确定需要接收事件通知的玩家范围
 */
std::vector<Player*> CalendarMgr::GetAllEventRelatives(CalendarEvent const& calendarEvent) const
{
    std::vector<Player*> relatedPlayers;

    // 公会事件或公告：收集所有在线公会成员
    if (calendarEvent.IsGuildEvent() || calendarEvent.IsGuildAnnouncement())
    {
        if (Guild* guild = sGuildMgr->GetGuildById(calendarEvent.GetGuildId()))
        {
            auto memberCollector = [&](Player* player) { relatedPlayers.push_back(player); };
            guild->BroadcastWorker(memberCollector);
        }
    }

    // 收集被邀请者
    // 对于公会事件，只收集非公会成员的被邀请者（避免重复）
    // 对于个人事件，收集所有被邀请者
    if (auto itr =_invites.find(calendarEvent.GetEventId()); itr != _invites.end())
    {
        CalendarInviteStore invites = itr->second;
        for (CalendarInvite const* invite : invites)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(invite->GetInviteeGUID()))
                if (!calendarEvent.IsGuildEvent() || player->GetGuildId() != calendarEvent.GetGuildId())
                    relatedPlayers.push_back(player);
    }

    return relatedPlayers;
}
