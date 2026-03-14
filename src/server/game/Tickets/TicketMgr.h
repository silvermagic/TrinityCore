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
 * @file TicketMgr.h
 * @brief GM工单管理系统
 *
 * 本模块实现了游戏内的GM工单系统，支持：
 * - 玩家提交GM求助工单
 * - GM查看、处理、分配、升级工单
 * - 工单的持久化存储和加载
 * - 工单统计和列表显示
 * - 工单调查问卷管理
 *
 * 主要类：
 * - GmTicket：单个工单数据结构和操作
 * - TicketMgr：工单管理器，管理所有工单
 *
 * 使用场景：
 * - 玩家遇到问题需要GM帮助时提交工单
 * - GM通过工单系统处理玩家问题
 * - 支持工单升级、分配、关闭等操作
 */

#ifndef _TICKETMGR_H
#define _TICKETMGR_H

#include "ObjectGuid.h"
#include "DatabaseEnvFwd.h"
#include <map>

class ChatHandler;
class Player;
class WorldPacket;
class WorldSession;

/**
 * @brief GM工单系统状态枚举
 *
 * 定义工单系统的全局启用/禁用状态。
 * 来自暴雪Lua接口定义。
 */
enum GMTicketSystemStatus
{
    GMTICKET_QUEUE_STATUS_DISABLED = 0,  ///< 工单系统禁用，玩家无法提交工单
    GMTICKET_QUEUE_STATUS_ENABLED  = 1   ///< 工单系统启用，玩家可以提交工单
};

/**
 * @brief GM工单状态枚举
 *
 * 定义工单的当前状态，用于客户端显示。
 */
enum GMTicketStatus
{
    GMTICKET_STATUS_HASTEXT                      = 0x06,  ///< 工单有内容（存在未处理的工单）
    GMTICKET_STATUS_DEFAULT                      = 0x0A   ///< 默认状态（无工单）
};

/**
 * @brief GM工单响应码枚举
 *
 * 定义工单操作的响应结果，发送给客户端。
 */
enum GMTicketResponse
{
    GMTICKET_RESPONSE_ALREADY_EXIST               = 1,  ///< 工单已存在
    GMTICKET_RESPONSE_CREATE_SUCCESS              = 2,  ///< 工单创建成功
    GMTICKET_RESPONSE_CREATE_ERROR                = 3,  ///< 工单创建失败
    GMTICKET_RESPONSE_UPDATE_SUCCESS              = 4,  ///< 工单更新成功
    GMTICKET_RESPONSE_UPDATE_ERROR                = 5,  ///< 工单更新失败
    GMTICKET_RESPONSE_TICKET_DELETED              = 9   ///< 工单已删除
};

/**
 * @brief GM工单升级状态枚举
 *
 * 定义工单的分配和升级状态。
 * 来自暴雪Lua接口定义：
 * - GMTICKET_ASSIGNEDTOGM_STATUS_NOT_ASSIGNED = 0; 工单未分配给GM
 * - GMTICKET_ASSIGNEDTOGM_STATUS_ASSIGNED = 1; 工单已分配给普通GM
 * - GMTICKET_ASSIGNEDTOGM_STATUS_ESCALATED = 2; 工单在升级队列中
 * - 3 是自定义值，不应该发送给客户端
 */
enum GMTicketEscalationStatus
{
    TICKET_UNASSIGNED                             = 0,  ///< 未分配
    TICKET_ASSIGNED                               = 1,  ///< 已分配给GM
    TICKET_IN_ESCALATION_QUEUE                    = 2,  ///< 在升级队列中（需要高级GM处理）
    TICKET_ESCALATED_ASSIGNED                     = 3   ///< 已升级并分配（高级GM已接手）
};

/**
 * @brief GM工单是否被打开查看状态枚举
 *
 * 定义工单是否已被GM查看过的状态。
 * 来自暴雪Lua接口定义。
 */
enum GMTicketOpenedByGMStatus
{
    GMTICKET_OPENEDBYGM_STATUS_NOT_OPENED = 0,  ///< 工单从未被GM打开
    GMTICKET_OPENEDBYGM_STATUS_OPENED     = 1   ///< 工单已被GM打开查看
};

/**
 * @brief 延迟报告类型枚举
 *
 * 定义玩家可以报告的延迟问题类型。
 */
enum LagReportType
{
    LAG_REPORT_TYPE_LOOT = 1,          ///< 拾取延迟
    LAG_REPORT_TYPE_AUCTION_HOUSE = 2, ///< 拍卖行延迟
    LAG_REPORT_TYPE_MAIL = 3,          ///< 邮件延迟
    LAG_REPORT_TYPE_CHAT = 4,          ///< 聊天延迟
    LAG_REPORT_TYPE_MOVEMENT = 5,      ///< 移动延迟
    LAG_REPORT_TYPE_SPELL = 6          ///< 法术延迟
};

/**
 * @brief 工单类型枚举
 *
 * 定义工单的内部类型状态。
 */
enum TicketType
{
    TICKET_TYPE_OPEN = 0,            ///< 开放状态（正常工单）
    TICKET_TYPE_CLOSED = 1,          ///< 已关闭
    TICKET_TYPE_CHARACTER_DELETED = 2, ///< 角色已删除
};

/**
 * @brief GM工单类
 *
 * 表示单个GM工单的完整数据结构和操作方法。
 * 包含工单的创建、更新、关闭、加载、保存等功能。
 *
 * 工单生命周期：
 * 1. 玩家创建工单 -> TICKET_TYPE_OPEN
 * 2. GM查看/分配工单 -> 更新_assignedTo和_escalatedStatus
 * 3. GM处理工单（添加评论/响应）
 * 4. GM关闭/解决工单 -> TICKET_TYPE_CLOSED
 *
 * 关键概念：
 * - 关闭(Closed): 工单被关闭，玩家可以查看GM响应
 * - 完成(Completed): 工单被标记为已完成（独立于关闭状态）
 * - 升级(Escalated): 工单被升级到高级GM队列
 */
class TC_GAME_API GmTicket
{
public:
    /// 默认构造函数
    GmTicket();

    /**
     * @brief 从玩家对象创建新工单
     * @param player 提交工单的玩家
     *
     * 自动设置工单ID、玩家信息、创建时间等
     */
    GmTicket(Player* player);

    /// 析构函数
    ~GmTicket();

    // ========== 状态查询方法 ==========

    /// 工单是否已关闭
    bool IsClosed() const { return _type != TICKET_TYPE_OPEN; }

    /// 工单是否已完成
    bool IsCompleted() const { return _completed; }

    /// 工单是否来自指定玩家
    bool IsFromPlayer(ObjectGuid guid) const { return guid == _playerGuid; }

    /// 工单是否已分配
    bool IsAssigned() const { return !_assignedTo.IsEmpty(); }

    /// 工单是否分配给指定GM
    bool IsAssignedTo(ObjectGuid guid) const { return guid == _assignedTo; }

    /// 工单是否已分配但不是给指定GM
    bool IsAssignedNotTo(ObjectGuid guid) const { return IsAssigned() && !IsAssignedTo(guid); }

    // ========== 数据获取方法 ==========

    /// 获取工单ID
    uint32 GetId() const { return _id; }

    /// 获取提交工单的玩家对象（如果在线）
    Player* GetPlayer() const;

    /// 获取玩家名称
    std::string const& GetPlayerName() const { return _playerName; }

    /// 获取工单消息内容
    std::string const& GetMessage() const { return _message; }

    /// 获取分配的GM玩家对象（如果在线）
    Player* GetAssignedPlayer() const;

    /// 获取分配的GM的GUID
    ObjectGuid GetAssignedToGUID() const { return _assignedTo; }

    /// 获取分配的GM名称
    std::string GetAssignedToName() const;

    /// 获取最后修改时间（Unix时间戳）
    uint64 GetLastModifiedTime() const { return _lastModifiedTime; }

    /// 获取升级状态
    GMTicketEscalationStatus GetEscalatedStatus() const { return _escalatedStatus; }

    // ========== 数据设置方法 ==========

    /// 设置升级状态
    void SetEscalatedStatus(GMTicketEscalationStatus escalatedStatus) { _escalatedStatus = escalatedStatus; }

    /**
     * @brief 设置工单分配
     * @param guid GM的GUID
     * @param isAdmin 是否为管理员（高级GM）
     *
     * 根据是否为管理员自动更新升级状态
     */
    void SetAssignedTo(ObjectGuid guid, bool isAdmin)
    {
        _assignedTo = guid;
        if (isAdmin && _escalatedStatus == TICKET_IN_ESCALATION_QUEUE)
            _escalatedStatus = TICKET_ESCALATED_ASSIGNED;
        else if (_escalatedStatus == TICKET_UNASSIGNED)
            _escalatedStatus = TICKET_ASSIGNED;
    }

    /// 设置关闭者并标记为已关闭
    void SetClosedBy(ObjectGuid value) { _closedBy = value; _type = TICKET_TYPE_CLOSED; }

    /// 设置解决者
    void SetResolvedBy(ObjectGuid value) { _resolvedBy = value; }

    /// 标记为已完成
    void SetCompleted() { _completed = true; }

    /// 设置工单消息（同时更新最后修改时间）
    void SetMessage(std::string const& message);

    /// 设置GM评论
    void SetComment(std::string const& comment) { _comment = comment; }

    /// 标记为已查看
    void SetViewed() { _viewed = true; }

    /// 取消分配
    void SetUnassigned();

    /// 设置工单位置
    void SetPosition(uint32 mapId, float x, float y, float z);

    /// 设置GM操作标记
    void SetGmAction(uint32 needResponse, bool needMoreHelp);

    /// 追加响应内容
    void AppendResponse(std::string const& response) { _response += response; }

    // ========== 数据库操作方法 ==========

    /**
     * @brief 从数据库字段加载工单数据
     * @param fields 数据库查询结果字段数组
     * @return true 加载成功
     *
     * 字段顺序参见 CHAR_SEL_GM_TICKETS 预处理语句
     */
    bool LoadFromDB(Field* fields);

    /**
     * @brief 保存工单数据到数据库
     * @param trans 数据库事务（可为空）
     *
     * 使用REPLACE语句，支持插入和更新
     */
    void SaveToDB(CharacterDatabaseTransaction trans) const;

    /// 从数据库删除工单
    void DeleteFromDB();

    // ========== 网络通信方法 ==========

    /**
     * @brief 写入工单数据到网络包
     * @param data 输出的网络包
     *
     * 用于发送工单状态给客户端
     */
    void WritePacket(WorldPacket& data) const;

    /**
     * @brief 发送GM响应给客户端
     * @param session 目标会话
     *
     * 发送GM的响应消息，响应内容最多分成4段发送
     */
    void SendResponse(WorldSession* session) const;

    // ========== 其他功能方法 ==========

    /**
     * @brief 传送玩家到工单位置
     * @param player 要传送的玩家
     */
    void TeleportTo(Player* player) const;

    /**
     * @brief 格式化工单消息字符串（简要信息）
     * @param handler 聊天处理器
     * @param detailed 是否显示详细信息
     * @return 格式化的字符串
     */
    std::string FormatMessageString(ChatHandler& handler, bool detailed = false) const;

    /**
     * @brief 格式化工单消息字符串（带状态信息）
     * @param handler 聊天处理器
     * @param szClosedName 关闭者名称（可为空）
     * @param szAssignedToName 分配给谁（可为空）
     * @param szUnassignedName 取消分配者名称（可为空）
     * @param szDeletedName 删除者名称（可为空）
     * @param szCompletedName 完成者名称（可为空）
     * @return 格式化的字符串
     */
    std::string FormatMessageString(ChatHandler& handler, char const* szClosedName, char const* szAssignedToName, char const* szUnassignedName, char const* szDeletedName, char const* szCompletedName) const;

    /**
     * @brief 设置聊天日志
     * @param time 时间戳列表
     * @param log 日志内容
     */
    void SetChatLog(std::list<uint32> time, std::string const& log);

    /// 获取聊天日志
    std::string const& GetChatLog() const { return _chatLog; }

private:
    // ========== 成员变量 ==========

    uint32 _id;                    ///< 工单唯一ID
    TicketType _type;              ///< 工单类型：0=开放, 1=已关闭, 2=角色已删除
    ObjectGuid _playerGuid;        ///< 提交工单的玩家GUID
    std::string _playerName;       ///< 提交工单的玩家名称
    float _posX;                   ///< 工单位置X坐标
    float _posY;                   ///< 工单位置Y坐标
    float _posZ;                   ///< 工单位置Z坐标
    uint16 _mapId;                 ///< 工单位置地图ID
    std::string _message;          ///< 工单消息内容
    uint64 _createTime;            ///< 创建时间（Unix时间戳）
    uint64 _lastModifiedTime;      ///< 最后修改时间（Unix时间戳）
    ObjectGuid _closedBy;          ///< 关闭者GUID：0=开放或控制台关闭，玩家GUID=关闭工单的GM或放弃工单的玩家
    ObjectGuid _resolvedBy;        ///< 解决者GUID：0=开放或控制台解决，玩家GUID=解决工单的GM
    ObjectGuid _assignedTo;        ///< 分配给的GM的GUID
    std::string _comment;          ///< GM评论
    bool _completed;               ///< 是否已完成
    GMTicketEscalationStatus _escalatedStatus; ///< 升级状态
    bool _viewed;                  ///< 是否已被查看
    bool _needResponse;            ///< 是否需要响应（待确认用途）
    bool _needMoreHelp;            ///< 是否需要更多帮助
    std::string _response;         ///< GM响应内容
    std::string _chatLog;          ///< 聊天日志（不存储到数据库，每次会话刷新）
};

/// 工单列表类型定义（工单ID -> 工单对象指针）
typedef std::map<uint32, GmTicket*> GmTicketList;

/**
 * @brief GM工单管理器
 *
 * 单例类，负责管理所有GM工单的生命周期。
 * 提供工单的加载、存储、查询、添加、删除等功能。
 *
 * 主要职责：
 * - 从数据库加载和缓存所有工单
 * - 管理工单ID生成
 * - 统计开放工单数量
 * - 提供工单列表显示功能
 * - 管理工单调查问卷ID
 *
 * 使用方式：
 * 通过 sTicketMgr 宏访问单例实例
 */
class TC_GAME_API TicketMgr
{
private:
    /// 私有构造函数（单例模式）
    TicketMgr();

    /// 析构函数，清理所有工单内存
    ~TicketMgr();

public:
    /**
     * @brief 获取单例实例
     * @return TicketMgr单例指针
     */
    static TicketMgr* instance();

    /**
     * @brief 从数据库加载所有工单
     *
     * 服务器启动时调用，加载并缓存所有工单数据。
     * 同时更新最大工单ID和开放工单计数。
     */
    void LoadTickets();

    /**
     * @brief 加载调查问卷ID计数器
     *
     * 从数据库获取最大的调查问卷ID，用于生成新ID。
     * 不实际加载调查问卷内容到内存。
     */
    void LoadSurveys();

    // ========== 工单查询方法 ==========

    /**
     * @brief 根据ID获取工单
     * @param ticketId 工单ID
     * @return 工单对象指针，不存在返回nullptr
     */
    GmTicket* GetTicket(uint32 ticketId)
    {
        GmTicketList::iterator itr = _ticketList.find(ticketId);
        if (itr != _ticketList.end())
            return itr->second;

        return nullptr;
    }

    /**
     * @brief 根据玩家GUID获取工单
     * @param playerGuid 玩家GUID
     * @return 该玩家的开放工单，不存在返回nullptr
     *
     * @note 每个玩家同时只能有一个开放工单
     */
    GmTicket* GetTicketByPlayer(ObjectGuid playerGuid)
    {
        for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
            if (itr->second && itr->second->IsFromPlayer(playerGuid) && !itr->second->IsClosed())
                return itr->second;

        return nullptr;
    }

    /**
     * @brief 获取最早的开放工单
     * @return 最早的未关闭、未完成的工单，不存在返回nullptr
     *
     * 用于显示"下一个需要处理的工单"
     */
    GmTicket* GetOldestOpenTicket()
    {
        for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
            if (itr->second && !itr->second->IsClosed() && !itr->second->IsCompleted())
                return itr->second;

        return nullptr;
    }

    // ========== 工单操作方法 ==========

    /**
     * @brief 添加新工单
     * @param ticket 工单对象指针
     *
     * 添加到内存列表并保存到数据库
     */
    void AddTicket(GmTicket* ticket);

    /**
     * @brief 关闭工单
     * @param ticketId 工单ID
     * @param source 关闭者GUID
     *
     * 设置关闭者并保存，更新开放工单计数
     */
    void CloseTicket(uint32 ticketId, ObjectGuid source);

    /**
     * @brief 解决并关闭工单
     * @param ticketId 工单ID
     * @param source 解决者GUID
     *
     * 同时设置关闭者和解决者，更新开放工单计数。
     * 用于GM通过简单关闭来处理工单的情况。
     */
    void ResolveAndCloseTicket(uint32 ticketId, ObjectGuid source);

    /**
     * @brief 删除工单
     * @param ticketId 工单ID
     *
     * 从内存和数据库中永久删除工单
     */
    void RemoveTicket(uint32 ticketId);

    // ========== 状态管理方法 ==========

    /// 获取工单系统状态（启用/禁用）
    bool GetStatus() const { return _status; }

    /// 设置工单系统状态
    void SetStatus(bool status) { _status = status; }

    /// 获取最后变更时间
    uint64 GetLastChange() const { return _lastChange; }

    /// 更新最后变更时间为当前时间
    void UpdateLastChange();

    /// 生成新的工单ID
    uint32 GenerateTicketId() { return ++_lastTicketId; }

    /// 获取开放工单数量
    uint32 GetOpenTicketCount() const { return _openTicketCount; }

    /// 生成新的调查问卷ID
    uint32 GetNextSurveyID() { return ++_lastSurveyId; }

    /**
     * @brief 初始化工单管理器
     *
     * 根据世界配置设置工单系统状态
     */
    void Initialize();

    /**
     * @brief 重置所有工单
     *
     * 删除所有已关闭的工单，重置ID计数器，清空数据库表
     */
    void ResetTickets();

    // ========== 显示方法 ==========

    /**
     * @brief 显示工单列表
     * @param handler 聊天处理器
     * @param onlineOnly 是否只显示在线玩家的工单
     */
    void ShowList(ChatHandler& handler, bool onlineOnly) const;

    /**
     * @brief 显示已关闭工单列表
     * @param handler 聊天处理器
     */
    void ShowClosedList(ChatHandler& handler) const;

    /**
     * @brief 显示升级队列工单列表
     * @param handler 聊天处理器
     */
    void ShowEscalatedList(ChatHandler& handler) const;

    /**
     * @brief 发送工单数据给客户端
     * @param session 目标会话
     * @param ticket 工单对象（可为空）
     *
     * 发送工单状态包给客户端，工单为空时发送默认状态
     */
    void SendTicket(WorldSession* session, GmTicket* ticket) const;

private:
    // ========== 成员变量 ==========

    GmTicketList _ticketList;    ///< 工单列表（ID -> 工单对象）

    bool   _status;              ///< 工单系统状态（启用/禁用）
    uint32 _lastTicketId;        ///< 最后使用的工单ID
    uint32 _lastSurveyId;        ///< 最后使用的调查问卷ID
    uint32 _openTicketCount;     ///< 开放工单数量
    uint64 _lastChange;          ///< 最后变更时间（Unix时间戳）
};

/// 工单管理器单例访问宏
#define sTicketMgr TicketMgr::instance()

#endif // _TICKETMGR_H
