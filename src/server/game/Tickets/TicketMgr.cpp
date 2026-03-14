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
 * @file TicketMgr.cpp
 * @brief GM工单管理系统实现
 *
 * 实现了工单管理器的核心功能，包括：
 * - 工单的创建、查询、更新、删除
 * - 工单数据的数据库持久化
 * - 工单列表的显示和发送
 * - 工单统计信息管理
 */

#include "TicketMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 计算时间年龄（以天为单位）
 *
 * @param t Unix时间戳
 * @return 从给定时间到现在经过的天数
 *
 * @note 用于在客户端显示工单的"年龄"
 */
inline float GetAge(uint64 t) { return float(GameTime::GetGameTime() - t) / float(DAY); }

// ============================================================================
// GmTicket 类实现
// ============================================================================

/**
 * @brief 默认构造函数
 *
 * 初始化所有成员变量为默认值。
 * 用于从数据库加载工单数据前的对象创建。
 */
GmTicket::GmTicket() : _id(0), _type(TICKET_TYPE_OPEN), _posX(0), _posY(0), _posZ(0), _mapId(0), _createTime(0), _lastModifiedTime(0),
                       _completed(false), _escalatedStatus(TICKET_UNASSIGNED), _viewed(false),
                       _needResponse(false), _needMoreHelp(false) { }

/**
 * @brief 从玩家对象创建新工单
 *
 * @param player 提交工单的玩家
 *
 * 自动设置工单ID、玩家信息和时间戳。
 * 工单ID由TicketMgr自动生成。
 */
GmTicket::GmTicket(Player* player) : _type(TICKET_TYPE_OPEN), _posX(0), _posY(0), _posZ(0), _mapId(0), _createTime(GameTime::GetGameTime()), _lastModifiedTime(GameTime::GetGameTime()),
                       _completed(false), _escalatedStatus(TICKET_UNASSIGNED), _viewed(false),
                       _needResponse(false), _needMoreHelp(false)
{
    _id = sTicketMgr->GenerateTicketId();    // 生成唯一工单ID
    _playerName = player->GetName();          // 记录玩家名称
    _playerGuid = player->GetGUID();          // 记录玩家GUID
}

/// 析构函数
GmTicket::~GmTicket()
{
}

/**
 * @brief 获取提交工单的玩家对象
 * @return 玩家对象指针，如果玩家不在线则返回nullptr
 */
Player* GmTicket::GetPlayer() const
{
    return ObjectAccessor::FindPlayer(_playerGuid);
}

/**
 * @brief 获取分配处理工单的GM玩家对象
 * @return GM玩家对象指针，如果GM不在线则返回nullptr
 */
Player* GmTicket::GetAssignedPlayer() const
{
    return ObjectAccessor::FindPlayer(_assignedTo);
}

/**
 * @brief 从数据库字段加载工单数据
 *
 * @param fields 数据库查询结果字段数组
 * @return true 加载成功
 *
 * 字段顺序（参见 CHAR_SEL_GM_TICKETS）：
 * 0:id, 1:type, 2:playerGuid, 3:name, 4:description, 5:createTime,
 * 6:mapId, 7:posX, 8:posY, 9:posZ, 10:lastModifiedTime, 11:closedBy,
 * 12:assignedTo, 13:comment, 14:response, 15:completed, 16:escalated,
 * 17:viewed, 18:needMoreHelp
 */
bool GmTicket::LoadFromDB(Field* fields)
{
    uint8 index = 0;
    _id                 = fields[  index].GetUInt32();      // 工单ID
    _type               = TicketType(fields[++index].GetUInt8()); // 工单类型
    _playerGuid         = ObjectGuid(HighGuid::Player, fields[++index].GetUInt32()); // 玩家GUID
    _playerName         = fields[++index].GetString();       // 玩家名称
    _message            = fields[++index].GetString();       // 工单消息
    _createTime         = fields[++index].GetUInt32();       // 创建时间
    _mapId              = fields[++index].GetUInt16();       // 地图ID
    _posX               = fields[++index].GetFloat();        // X坐标
    _posY               = fields[++index].GetFloat();        // Y坐标
    _posZ               = fields[++index].GetFloat();        // Z坐标
    _lastModifiedTime   = fields[++index].GetUInt32();       // 最后修改时间
    _closedBy           = ObjectGuid(uint64(fields[++index].GetInt32())); // 关闭者
    _assignedTo         = ObjectGuid(HighGuid::Player, fields[++index].GetUInt32()); // 分配给的GM
    _comment            = fields[++index].GetString();       // GM评论
    _response           = fields[++index].GetString();       // GM响应
    _completed          = fields[++index].GetBool();         // 是否完成
    _escalatedStatus    = GMTicketEscalationStatus(fields[++index].GetUInt8()); // 升级状态
    _viewed             = fields[++index].GetBool();         // 是否查看
    _needMoreHelp       = fields[++index].GetBool();         // 是否需要更多帮助
    return true;
}

/**
 * @brief 保存工单数据到数据库
 *
 * @param trans 数据库事务（可为空，表示立即执行）
 *
 * 使用REPLACE语句，如果工单已存在则更新，否则插入新记录。
 */
void GmTicket::SaveToDB(CharacterDatabaseTransaction trans) const
{
    uint8 index = 0;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_GM_TICKET);
    stmt->setUInt32(  index, _id);
    stmt->setUInt8 (++index, uint8(_type));
    stmt->setUInt32(++index, _playerGuid.GetCounter());
    stmt->setString(++index, _playerName);
    stmt->setString(++index, _message);
    stmt->setUInt32(++index, uint32(_createTime));
    stmt->setUInt16(++index, _mapId);
    stmt->setFloat (++index, _posX);
    stmt->setFloat (++index, _posY);
    stmt->setFloat (++index, _posZ);
    stmt->setUInt32(++index, uint32(_lastModifiedTime));
    stmt->setInt32 (++index, int32(_closedBy.GetCounter()));
    stmt->setUInt32(++index, _assignedTo.GetCounter());
    stmt->setString(++index, _comment);
    stmt->setString(++index, _response);
    stmt->setBool  (++index, _completed);
    stmt->setUInt8 (++index, uint8(_escalatedStatus));
    stmt->setBool  (++index, _viewed);
    stmt->setBool  (++index, _needMoreHelp);
    stmt->setInt32 (++index, int32(_resolvedBy.GetCounter()));

    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

/**
 * @brief 从数据库删除工单
 *
 * 永久删除工单记录，不可恢复。
 */
void GmTicket::DeleteFromDB()
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GM_TICKET);
    stmt->setUInt32(0, _id);
    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 写入工单数据到网络包
 *
 * @param data 输出的网络包
 *
 * 将工单信息打包发送给客户端，包括：
 * - 工单状态和ID
 * - 工单消息内容
 * - 时间信息（年龄、最后修改时间）
 * - 升级状态和查看状态
 *
 * @note 数据格式需与客户端解析逻辑匹配
 */
void GmTicket::WritePacket(WorldPacket& data) const
{
    data << uint32(GMTICKET_STATUS_HASTEXT);  // 工单状态：有内容
    data << uint32(_id);                       // 工单ID
    data << _message;                          // 工单消息
    data << uint8(_needMoreHelp);              // 是否需要更多帮助
    data << GetAge(_lastModifiedTime);         // 最后修改时间的年龄（天）

    // 包含最老开放工单的年龄信息
    if (GmTicket* ticket = sTicketMgr->GetOldestOpenTicket())
        data << GetAge(ticket->GetLastModifiedTime());
    else
        data << float(0);

    // 工单系统的最后变更时间（不太确定是否完全符合暴雪原版行为）
    data << GetAge(sTicketMgr->GetLastChange());

    // 升级状态（限制在客户端可识别的范围内）
    data << uint8(std::min(_escalatedStatus, TICKET_IN_ESCALATION_QUEUE));
    // 是否已被GM查看
    data << uint8(_viewed ? GMTICKET_OPENEDBYGM_STATUS_OPENED : GMTICKET_OPENEDBYGM_STATUS_NOT_OPENED);
}

/**
 * @brief 发送GM响应给客户端
 *
 * @param session 目标会话
 *
 * 发送GM对工单的响应消息。
 * 响应内容最多分成4段，每段最大3999字符。
 *
 * @note 数据格式：responseID(1) + ticketID + 原消息 + 4段响应文本
 */
void GmTicket::SendResponse(WorldSession* session) const
{
    WorldPacket data(SMSG_GMRESPONSE_RECEIVED);
    data << uint32(1);          // responseID（固定为1）
    data << uint32(_id);        // ticketID
    data << _message.c_str();   // 原工单消息

    // 将响应内容分成最多4段发送
    size_t len = _response.size();
    char const* s = _response.c_str();

    for (int i = 0; i < 4; i++)
    {
        if (len)
        {
            // 每段最多3999字符
            size_t writeLen = std::min<size_t>(len, 3999);
            data.append(s, writeLen);

            len -= writeLen;
            s += writeLen;
        }

        data << uint8(0); // 每段以空字符结尾
    }

    session->SendPacket(&data);
}

/**
 * @brief 格式化工单消息字符串（简要/详细信息）
 *
 * @param handler 聊天处理器，用于本地化字符串
 * @param detailed 是否显示详细信息（消息、评论、响应）
 * @return 格式化的工单信息字符串
 *
 * 简要信息包括：ID、玩家名、创建时间、最后修改时间、分配状态
 * 详细信息增加：消息内容、评论、响应
 */
std::string GmTicket::FormatMessageString(ChatHandler& handler, bool detailed) const
{
    time_t curTime = GameTime::GetGameTime();

    std::stringstream ss;
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTGUID, _id);
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTNAME, _playerName.c_str());
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTAGECREATE, (secsToTimeString(curTime - _createTime, TimeFormat::ShortText)).c_str());
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTAGE, (secsToTimeString(curTime - _lastModifiedTime, TimeFormat::ShortText)).c_str());

    // 显示分配信息（如果已分配）
    std::string name;
    if (sCharacterCache->GetCharacterNameByGuid(_assignedTo, name))
        ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTASSIGNEDTO, name.c_str());

    // 详细模式下显示完整消息
    if (detailed)
    {
        ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTMESSAGE, _message.c_str());
        if (!_comment.empty())
            ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTCOMMENT, _comment.c_str());
        if (!_response.empty())
            ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTRESPONSE, _response.c_str());
    }
    return ss.str();
}

/**
 * @brief 格式化工单消息字符串（带状态变更信息）
 *
 * @param handler 聊天处理器
 * @param szClosedName 关闭者名称（可为空）
 * @param szAssignedToName 分配给的GM名称（可为空）
 * @param szUnassignedName 取消分配者名称（可为空）
 * @param szDeletedName 删除者名称（可为空）
 * @param szCompletedName 完成者名称（可为空）
 * @return 格式化的工单信息字符串
 *
 * 用于显示工单状态变更的详细信息
 */
std::string GmTicket::FormatMessageString(ChatHandler& handler, char const* szClosedName, char const* szAssignedToName, char const* szUnassignedName, char const* szDeletedName, char const* szCompletedName) const
{
    std::stringstream ss;
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTGUID, _id);
    ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTNAME, _playerName.c_str());

    // 根据状态显示不同的操作信息
    if (szClosedName)
        ss << handler.PGetParseString(LANG_COMMAND_TICKETCLOSED, szClosedName);
    if (szAssignedToName)
        ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTASSIGNEDTO, szAssignedToName);
    if (szUnassignedName)
        ss << handler.PGetParseString(LANG_COMMAND_TICKETLISTUNASSIGNED, szUnassignedName);
    if (szDeletedName)
        ss << handler.PGetParseString(LANG_COMMAND_TICKETDELETED, szDeletedName);
    if (szCompletedName)
        ss << handler.PGetParseString(LANG_COMMAND_TICKETCOMPLETED, szCompletedName);
    return ss.str();
}

/**
 * @brief 设置工单消息内容
 *
 * @param message 新的消息内容
 *
 * 同时更新最后修改时间
 */
void GmTicket::SetMessage(std::string const& message)
{
    _message = message;
    _lastModifiedTime = uint64(GameTime::GetGameTime());
}

/**
 * @brief 取消工单分配
 *
 * 清空分配对象，并根据当前升级状态更新状态码：
 * - TICKET_ASSIGNED -> TICKET_UNASSIGNED
 * - TICKET_ESCALATED_ASSIGNED -> TICKET_IN_ESCALATION_QUEUE
 */
void GmTicket::SetUnassigned()
{
    _assignedTo.Clear();
    switch (_escalatedStatus)
    {
        case TICKET_ASSIGNED: _escalatedStatus = TICKET_UNASSIGNED; break;
        case TICKET_ESCALATED_ASSIGNED: _escalatedStatus = TICKET_IN_ESCALATION_QUEUE; break;
        case TICKET_UNASSIGNED:
        case TICKET_IN_ESCALATION_QUEUE:
        default:
            break;
    }
}

/**
 * @brief 设置工单位置
 *
 * @param mapId 地图ID
 * @param x X坐标
 * @param y Y坐标
 * @param z Z坐标
 *
 * 记录玩家提交工单时的位置，便于GM传送
 */
void GmTicket::SetPosition(uint32 mapId, float x, float y, float z)
{
    _mapId = mapId;
    _posX = x;
    _posY = y;
    _posZ = z;
}

/**
 * @brief 设置GM操作标记
 *
 * @param needResponse 是否需要GM响应（17=true, 1=false，17是默认值）
 * @param needMoreHelp 是否需要更多帮助
 *
 * needMoreHelp表示玩家在GM响应后请求进一步帮助，相当于"有新工单"
 */
void GmTicket::SetGmAction(uint32 needResponse, bool needMoreHelp)
{
    _needResponse = (needResponse == 17);   // 需要GM响应，17=true，1=false
    _needMoreHelp = needMoreHelp;           // 请求GM对已响应工单的进一步交互
}

/**
 * @brief 传送玩家到工单位置
 *
 * @param player 要传送的玩家
 *
 * 将GM传送到玩家提交工单时的位置
 */
void GmTicket::TeleportTo(Player* player) const
{
    player->TeleportTo(_mapId, _posX, _posY, _posZ, 0.0f, 0);
}

/**
 * @brief 设置聊天日志
 *
 * @param time 时间戳列表
 * @param log 日志内容
 *
 * 将聊天日志按时间戳格式化存储
 */
void GmTicket::SetChatLog(std::list<uint32> time, std::string const& log)
{
    std::stringstream ss(log);
    std::stringstream newss;
    std::string line;

    // 将日志内容与时间戳配对
    while (std::getline(ss, line) && !time.empty())
    {
        newss << secsToTimeString(time.front()) << ": " << line << "\n";
        time.pop_front();
    }

    _chatLog = newss.str();
}

// ============================================================================
// TicketMgr 类实现
// ============================================================================

/**
 * @brief 构造函数
 *
 * 初始化工单管理器的成员变量。
 * 默认启用工单系统，重置ID计数器。
 */
TicketMgr::TicketMgr() : _status(true), _lastTicketId(0), _lastSurveyId(0), _openTicketCount(0),
    _lastChange(GameTime::GetGameTime()) { }

/**
 * @brief 析构函数
 *
 * 清理所有工单对象的内存。
 */
TicketMgr::~TicketMgr()
{
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 初始化工单管理器
 *
 * 根据世界配置设置工单系统状态。
 * 在服务器启动时调用。
 */
void TicketMgr::Initialize()
{
    SetStatus(sWorld->getBoolConfig(CONFIG_ALLOW_TICKETS));
}

/**
 * @brief 重置所有工单
 *
 * 删除所有已关闭的工单，重置ID计数器，清空数据库表。
 * 用于GM命令重置工单系统。
 */
void TicketMgr::ResetTickets()
{
    // 遍历并删除所有已关闭的工单
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end();)
    {
        if (itr->second->IsClosed())
        {
            uint32 ticketId = itr->second->GetId();
            ++itr;
            sTicketMgr->RemoveTicket(ticketId);
        }
        else
            ++itr;
    }

    // 重置工单ID计数器
    _lastTicketId = 0;

    // 清空数据库工单表
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ALL_GM_TICKETS);

    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 获取单例实例
 *
 * @return TicketMgr单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
TicketMgr* TicketMgr::instance()
{
    static TicketMgr instance;
    return &instance;
}

/**
 * @brief 从数据库加载所有工单
 *
 * 服务器启动时调用。加载并缓存所有工单数据。
 * 同时更新最大工单ID和开放工单计数。
 *
 * 性能注意事项：
 * - 所有工单会被加载到内存中
 * - 工单数量大时可能影响启动时间
 */
void TicketMgr::LoadTickets()
{
    uint32 oldMSTime = getMSTime();

    // 清理现有工单数据
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
        delete itr->second;
    _ticketList.clear();

    _lastTicketId = 0;
    _openTicketCount = 0;

    // 查询所有工单
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GM_TICKETS);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 GM tickets. DB table `gm_ticket` is empty!");

        return;
    }

    // 逐个加载工单
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        GmTicket* ticket = new GmTicket();
        if (!ticket->LoadFromDB(fields))
        {
            delete ticket;
            continue;
        }

        // 统计开放工单
        if (!ticket->IsClosed())
            ++_openTicketCount;

        // 更新最大工单ID
        uint32 id = ticket->GetId();
        if (_lastTicketId < id)
            _lastTicketId = id;

        _ticketList[id] = ticket;
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} GM tickets in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

}

/**
 * @brief 加载调查问卷ID计数器
 *
 * 从数据库获取最大的调查问卷ID，用于生成新ID。
 * 不加载调查问卷内容到内存，因为调查问卷数据量较大且使用频率低。
 */
void TicketMgr::LoadSurveys()
{
    // 不需要将调查问卷加载到内存，只获取最大ID
    _lastSurveyId = 0;

    uint32 oldMSTime = getMSTime();
    if (QueryResult result = CharacterDatabase.Query("SELECT MAX(surveyId) FROM gm_survey"))
        _lastSurveyId = (*result)[0].GetUInt32();

    TC_LOG_INFO("server.loading", ">> Loaded GM Survey count from database in {} ms", GetMSTimeDiffToNow(oldMSTime));

}

/**
 * @brief 添加新工单
 *
 * @param ticket 工单对象指针
 *
 * 添加到内存列表并立即保存到数据库。
 * 如果工单是开放的，更新开放工单计数。
 */
void TicketMgr::AddTicket(GmTicket* ticket)
{
    _ticketList[ticket->GetId()] = ticket;
    if (!ticket->IsClosed())
        ++_openTicketCount;

    // 保存到数据库（事务为空表示立即执行）
    CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr);
    ticket->SaveToDB(trans);
}

/**
 * @brief 关闭工单
 *
 * @param ticketId 工单ID
 * @param source 关闭者GUID
 *
 * 设置关闭者并保存到数据库。
 * 如果source有效，更新开放工单计数。
 */
void TicketMgr::CloseTicket(uint32 ticketId, ObjectGuid source)
{
    if (GmTicket* ticket = GetTicket(ticketId))
    {
        CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr);
        ticket->SetClosedBy(source);
        if (source)
            --_openTicketCount;
        ticket->SaveToDB(trans);
    }
}

/**
 * @brief 解决并关闭工单
 *
 * @param ticketId 工单ID
 * @param source 解决者GUID
 *
 * 同时设置关闭者和解决者。用于GM通过简单关闭工单来解决的情况。
 * 更新开放工单计数。
 */
void TicketMgr::ResolveAndCloseTicket(uint32 ticketId, ObjectGuid source)
{
    if (GmTicket* ticket = GetTicket(ticketId))
    {
        CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr);
        ticket->SetClosedBy(source);
        ticket->SetResolvedBy(source);
        if (source)
            --_openTicketCount;
        ticket->SaveToDB(trans);
    }
}

/**
 * @brief 删除工单
 *
 * @param ticketId 工单ID
 *
 * 从内存和数据库中永久删除工单。
 */
void TicketMgr::RemoveTicket(uint32 ticketId)
{
    if (GmTicket* ticket = GetTicket(ticketId))
    {
        ticket->DeleteFromDB();
        _ticketList.erase(ticketId);
        delete ticket;
    }
}

/**
 * @brief 更新最后变更时间
 *
 * 设置最后变更时间为当前游戏时间。
 * 用于客户端显示工单系统的"年龄"信息。
 */
void TicketMgr::UpdateLastChange()
{
    _lastChange = uint64(GameTime::GetGameTime());
}

/**
 * @brief 显示工单列表
 *
 * @param handler 聊天处理器
 * @param onlineOnly 是否只显示在线玩家的工单
 *
 * 显示所有未关闭、未完成的工单。
 * 可选只显示玩家在线的工单。
 */
void TicketMgr::ShowList(ChatHandler& handler, bool onlineOnly) const
{
    handler.SendSysMessage(onlineOnly ? LANG_COMMAND_TICKETSHOWONLINELIST : LANG_COMMAND_TICKETSHOWLIST);
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
        if (!itr->second->IsClosed() && !itr->second->IsCompleted())
            if (!onlineOnly || itr->second->GetPlayer())
                handler.SendSysMessage(itr->second->FormatMessageString(handler).c_str());
}

/**
 * @brief 显示已关闭工单列表
 *
 * @param handler 聊天处理器
 *
 * 显示所有已关闭的工单
 */
void TicketMgr::ShowClosedList(ChatHandler& handler) const
{
    handler.SendSysMessage(LANG_COMMAND_TICKETSHOWCLOSEDLIST);
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
        if (itr->second->IsClosed())
            handler.SendSysMessage(itr->second->FormatMessageString(handler).c_str());
}

/**
 * @brief 显示升级队列工单列表
 *
 * @param handler 聊天处理器
 *
 * 显示所有未关闭且在升级队列中的工单。
 * 这些工单需要高级GM处理。
 */
void TicketMgr::ShowEscalatedList(ChatHandler& handler) const
{
    handler.SendSysMessage(LANG_COMMAND_TICKETSHOWESCALATEDLIST);
    for (GmTicketList::const_iterator itr = _ticketList.begin(); itr != _ticketList.end(); ++itr)
        if (!itr->second->IsClosed() && itr->second->GetEscalatedStatus() == TICKET_IN_ESCALATION_QUEUE)
            handler.SendSysMessage(itr->second->FormatMessageString(handler).c_str());
}

/**
 * @brief 发送工单数据给客户端
 *
 * @param session 目标会话
 * @param ticket 工单对象（可为空）
 *
 * 发送工单状态包给客户端。
 * 工单为空时发送默认状态（无工单）。
 */
void TicketMgr::SendTicket(WorldSession* session, GmTicket* ticket) const
{
    WorldPacket data(SMSG_GMTICKET_GETTICKET, (4 + 4 + 1 + 4 + 4 + 4 + 1 + 1));

    if (ticket)
        ticket->WritePacket(data);
    else
        data << uint32(GMTICKET_STATUS_DEFAULT); // 默认状态：无工单

    session->SendPacket(&data);
}

/**
 * @brief 获取分配给的GM名称
 *
 * @return GM名称字符串，未分配返回空字符串
 *
 * 通过角色缓存查询，避免直接数据库查询。
 */
std::string GmTicket::GetAssignedToName() const
{
    std::string name;
    // 未分配时不查询
    if (_assignedTo)
        sCharacterCache->GetCharacterNameByGuid(_assignedTo, name);

    return name;
}
