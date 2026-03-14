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
 * @file ArenaTeam.cpp
 * @brief 竞技场队伍系统实现文件
 *
 * 本文件实现了竞技场队伍的核心功能，包括：
 * - 队伍的创建、解散和管理
 * - 成员的添加、移除和权限管理
 * - 竞技场等级积分（Rating）和匹配等级积分（MMR）的计算
 * - 赛季和周统计数据的跟踪
 * - 竞技场点数的计算和分配
 *
 * 竞技场系统支持三种队伍类型：
 * - 2v2 竞技场（ARENA_TEAM_2v2）
 * - 3v3 竞技场（ARENA_TEAM_3v3）
 * - 5v5 竞技场（ARENA_TEAM_5v5）
 *
 * 积分系统基于 ELO 算法，根据队伍等级积分和对手等级积分计算胜负后的积分变化。
 */

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattlegroundMgr.h"
#include "CalendarPackets.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

/**
 * @brief 构造函数 - 初始化竞技场队伍对象
 *
 * 初始化队伍的基本属性：
 * - TeamId: 队伍ID（初始化为0，后续通过GenerateArenaTeamId生成）
 * - Type: 队伍类型（2v2, 3v3, 5v5）
 * - CaptainGuid: 队长GUID
 * - BackgroundColor, EmblemStyle, EmblemColor, BorderStyle, BorderColor: 队徽相关
 * - Stats: 统计数据（周场次、赛季场次、排名、等级积分等）
 */
ArenaTeam::ArenaTeam()
    : TeamId(0), Type(0), TeamName(), CaptainGuid(), BackgroundColor(0), EmblemStyle(0), EmblemColor(0),
    BorderStyle(0), BorderColor(0), PreviousOpponents(0)
{
    Stats.WeekGames   = 0;   ///< 本周比赛场次
    Stats.SeasonGames = 0;   ///< 本赛季比赛场次
    Stats.Rank        = 0;   ///< 队伍排名
    Stats.Rating      = sWorld->getIntConfig(CONFIG_ARENA_START_RATING);  ///< 队伍等级积分，从配置读取初始值
    Stats.WeekWins    = 0;   ///< 本周胜场
    Stats.SeasonWins  = 0;   ///< 本赛季胜场
}

/**
 * @brief 析构函数
 */
ArenaTeam::~ArenaTeam()
{ }

/**
 * @brief 创建新的竞技场队伍
 *
 * 执行竞技场队伍的创建流程：
 * 1. 验证队长角色是否存在
 * 2. 检查队伍名称是否已被使用
 * 3. 生成唯一的队伍ID
 * 4. 设置队伍属性（名称、类型、队徽等）
 * 5. 将队伍信息保存到数据库
 * 6. 将队长添加为第一个成员
 *
 * @param captainGuid 队长的角色GUID
 * @param type 队伍类型（2=2v2, 3=3v3, 5=5v5）
 * @param teamName 队伍名称（最多24个字符）
 * @param backgroundColor 队徽背景颜色
 * @param emblemStyle 队徽图标样式
 * @param emblemColor 队徽图标颜色
 * @param borderStyle 队徽边框样式
 * @param borderColor 队徽边框颜色
 * @return true 创建成功
 * @return false 创建失败（队长不存在或名称已被占用）
 */
bool ArenaTeam::Create(ObjectGuid captainGuid, uint8 type, std::string const& teamName, uint32 backgroundColor, uint8 emblemStyle, uint32 emblemColor, uint8 borderStyle, uint32 borderColor)
{
    // Check if captain exists
    if (!sCharacterCache->GetCharacterCacheByGuid(captainGuid))
        return false;

    // Check if arena team name is already taken
    if (sArenaTeamMgr->GetArenaTeamByName(teamName))
        return false;

    // Generate new arena team id
    TeamId = sArenaTeamMgr->GenerateArenaTeamId();

    // Assign member variables
    CaptainGuid = captainGuid;
    Type = type;
    TeamName = teamName;
    BackgroundColor = backgroundColor;
    EmblemStyle = emblemStyle;
    EmblemColor = emblemColor;
    BorderStyle = borderStyle;
    BorderColor = borderColor;
    ObjectGuid::LowType captainLowGuid = captainGuid.GetCounter();

    // Save arena team to db
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    stmt->setString(1, TeamName);
    stmt->setUInt32(2, captainLowGuid);
    stmt->setUInt8(3, Type);
    stmt->setUInt16(4, Stats.Rating);
    stmt->setUInt32(5, BackgroundColor);
    stmt->setUInt8(6, EmblemStyle);
    stmt->setUInt32(7, EmblemColor);
    stmt->setUInt8(8, BorderStyle);
    stmt->setUInt32(9, BorderColor);
    CharacterDatabase.Execute(stmt);

    // Add captain as member
    AddMember(CaptainGuid);

    TC_LOG_DEBUG("bg.arena", "New ArenaTeam created [Id: {}, Name: {}] [Type: {}] [Captain low GUID: {}]", GetId(), GetName(), GetType(), captainLowGuid);
    return true;
}

/**
 * @brief 添加成员到竞技场队伍
 *
 * 执行成员添加流程：
 * 1. 检查队伍是否已满（最大成员数 = 队伍类型 * 2，例如3v3队伍最多6人）
 * 2. 获取玩家信息（名称、职业）
 * 3. 检查玩家是否已加入同类型的其他竞技场队伍
 * 4. 设置玩家的个人积分（Personal Rating）和匹配积分（MMR）
 * 5. 移除玩家的其他竞技场申请书签名，避免数据冲突
 * 6. 将成员信息保存到数据库
 * 7. 如果玩家在线，更新其客户端状态
 *
 * @param playerGuid 要添加的玩家GUID
 * @return true 添加成功
 * @return false 添加失败（队伍已满、玩家不存在或已在同类型队伍中）
 */
bool ArenaTeam::AddMember(ObjectGuid playerGuid)
{
    std::string playerName;
    uint8 playerClass;

    // Check if arena team is full (Can't have more than type * 2 players)
    if (GetMembersSize() >= GetType() * 2)
        return false;

    // Get player name and class either from db or character cache
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (player)
    {
        playerClass = player->GetClass();
        playerName = player->GetName();
    }
    else
    {
        CharacterCacheEntry const* cInfo = sCharacterCache->GetCharacterCacheByGuid(playerGuid);
        if (!cInfo)
            return false;

        playerName = cInfo->Name;
        playerClass = cInfo->Class;
    }

    // Check if player is already in a similar arena team
    if ((player && player->GetArenaTeamId(GetSlot())) || sCharacterCache->GetCharacterArenaTeamIdByGuid(playerGuid, GetType()) != 0)
    {
        TC_LOG_DEBUG("bg.arena", "Arena: {} {} already has an arena team of type {}", playerGuid.ToString(), playerName, GetType());
        return false;
    }

    // Set player's personal rating
    uint16 personalRating = 0;

    if (sWorld->getIntConfig(CONFIG_ARENA_START_PERSONAL_RATING) > 0)
        personalRating = uint16(sWorld->getIntConfig(CONFIG_ARENA_START_PERSONAL_RATING));
    else if (GetRating() >= 1000)
        personalRating = 1000;

    // Try to get player's match maker rating from db and fall back to config setting if not found
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MATCH_MAKER_RATING);
    stmt->setUInt32(0, playerGuid.GetCounter());
    stmt->setUInt8(1, GetSlot());
    PreparedQueryResult result = CharacterDatabase.Query(stmt);

    uint32 matchMakerRating;
    if (result)
        matchMakerRating = (*result)[0].GetUInt16();
    else
        matchMakerRating = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);

    // Remove all player signatures from other petitions
    // This will prevent player from joining too many arena teams and corrupt arena team data integrity
    Player::RemovePetitionsAndSigns(playerGuid, static_cast<CharterTypes>(GetType()));

    // Feed data to the struct
    ArenaTeamMember newMember;
    newMember.Name             = playerName;
    newMember.Guid             = playerGuid;
    newMember.Class            = playerClass;
    newMember.SeasonGames      = 0;
    newMember.WeekGames        = 0;
    newMember.SeasonWins       = 0;
    newMember.WeekWins         = 0;
    newMember.PersonalRating   = personalRating;
    newMember.MatchMakerRating = matchMakerRating;

    Members.push_back(newMember);
    sCharacterCache->UpdateCharacterArenaTeamId(playerGuid, GetSlot(), GetId());

    // Save player's arena team membership to db
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ARENA_TEAM_MEMBER);
    stmt->setUInt32(0, TeamId);
    stmt->setUInt32(1, playerGuid.GetCounter());
    stmt->setUInt16(2, personalRating);
    CharacterDatabase.Execute(stmt);

    // Inform player if online
    if (player)
    {
        player->SetInArenaTeam(TeamId, GetSlot(), GetType());
        player->SetArenaTeamIdInvited(0);

        // Hide promote/remove buttons
        if (CaptainGuid != playerGuid)
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 1);
    }

    TC_LOG_DEBUG("bg.arena", "Player: {} [{}] joined arena team type: {} [Id: {}, Name: {}].", playerName, playerGuid.ToString(), GetType(), GetId(), GetName());

    return true;
}

/**
 * @brief 从数据库加载竞技场队伍信息
 *
 * 从数据库查询结果中加载队伍的基本属性，包括：
 * - 队伍ID、名称、队长、类型
 * - 队徽样式（背景色、图标样式/颜色、边框样式/颜色）
 * - 队伍统计数据（等级积分、周场次/胜场、赛季场次/胜场、排名）
 *
 * @param result 数据库查询结果
 * @return true 加载成功
 * @return false 加载失败（查询结果为空）
 */
bool ArenaTeam::LoadArenaTeamFromDB(QueryResult result)
{
    if (!result)
        return false;

    Field* fields = result->Fetch();

    TeamId            = fields[0].GetUInt32();
    TeamName          = fields[1].GetString();
    CaptainGuid       = ObjectGuid(HighGuid::Player, fields[2].GetUInt32());
    Type              = fields[3].GetUInt8();
    BackgroundColor   = fields[4].GetUInt32();
    EmblemStyle       = fields[5].GetUInt8();
    EmblemColor       = fields[6].GetUInt32();
    BorderStyle       = fields[7].GetUInt8();
    BorderColor       = fields[8].GetUInt32();
    Stats.Rating      = fields[9].GetUInt16();
    Stats.WeekGames   = fields[10].GetUInt16();
    Stats.WeekWins    = fields[11].GetUInt16();
    Stats.SeasonGames = fields[12].GetUInt16();
    Stats.SeasonWins  = fields[13].GetUInt16();
    Stats.Rank        = fields[14].GetUInt32();

    return true;
}

/**
 * @brief 从数据库加载竞技场队伍成员列表
 *
 * 遍历数据库查询结果，加载所有成员信息：
 * - 成员GUID、名称、职业
 * - 周场次/胜场、赛季场次/胜场
 * - 个人积分、匹配积分（MMR）
 *
 * 加载过程中执行以下验证：
 * 1. 检查成员名称是否为空（角色可能已删除），若为空则删除该成员
 * 2. 验证队长是否存在于队伍中
 * 3. 如果队伍为空或队长不存在，解散队伍
 *
 * @param result 数据库查询结果（可能包含多个队伍的成员，按队伍ID排序）
 * @return true 加载成功且队伍有效
 * @return false 加载失败、队伍为空或队长不存在
 */
bool ArenaTeam::LoadMembersFromDB(QueryResult result)
{
    if (!result)
        return false;

    bool captainPresentInTeam = false;

    do
    {
        Field* fields = result->Fetch();

        // Prevent crash if db records are broken when all members in result are already processed and current team doesn't have any members
        if (!fields)
            break;

        uint32 arenaTeamId = fields[0].GetUInt32();

        // We loaded all members for this arena_team already, break cycle
        if (arenaTeamId > TeamId)
            break;

        ArenaTeamMember newMember;
        newMember.Guid             = ObjectGuid(HighGuid::Player, fields[1].GetUInt32());
        newMember.WeekGames        = fields[2].GetUInt16();
        newMember.WeekWins         = fields[3].GetUInt16();
        newMember.SeasonGames      = fields[4].GetUInt16();
        newMember.SeasonWins       = fields[5].GetUInt16();
        newMember.Name             = fields[6].GetString();
        newMember.Class            = fields[7].GetUInt8();
        newMember.PersonalRating   = fields[8].GetUInt16();
        newMember.MatchMakerRating = fields[9].GetUInt16() > 0 ? fields[9].GetUInt16() : sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);

        // Delete member if character information is missing
        if (newMember.Name.empty())
        {
            TC_LOG_ERROR("sql.sql", "ArenaTeam {} has member with empty name - probably {} doesn't exist, deleting him from memberlist!", arenaTeamId, newMember.Guid.ToString());
            DelMember(newMember.Guid, true);
            continue;
        }

        // Check if team team has a valid captain
        if (newMember.Guid == GetCaptain())
            captainPresentInTeam = true;

        // Put the player in the team
        Members.push_back(newMember);
        sCharacterCache->UpdateCharacterArenaTeamId(newMember.Guid, GetSlot(), GetId());
    }
    while (result->NextRow());

    if (Empty() || !captainPresentInTeam)
    {
        // Arena team is empty or captain is not in team, delete from db
        TC_LOG_DEBUG("bg.arena", "ArenaTeam {} does not have any members or its captain is not in team, disbanding it...", TeamId);
        return false;
    }

    return true;
}

/**
 * @brief 设置竞技场队伍名称
 *
 * 验证并更新队伍名称，执行以下检查：
 * 1. 新名称不能与旧名称相同
 * 2. 名称不能为空
 * 3. 名称长度不能超过24个字符
 * 4. 名称不能是保留名称（如GM、Admin等）
 * 5. 名称必须符合竞技场队伍命名规范
 *
 * @param name 新的队伍名称
 * @return true 设置成功
 * @return false 设置失败（名称无效）
 */
bool ArenaTeam::SetName(std::string const& name)
{
    if (TeamName == name || name.empty() || name.length() > 24 || sObjectMgr->IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        return false;

    TeamName = name;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_NAME);
    stmt->setString(0, TeamName);
    stmt->setUInt32(1, GetId());
    CharacterDatabase.Execute(stmt);
    return true;
}

/**
 * @brief 设置新的队伍队长
 *
 * 执行队长转让流程：
 * 1. 禁用旧队长的管理按钮（提升/移除成员）
 * 2. 更新队长GUID到新玩家
 * 3. 更新数据库中的队长信息
 * 4. 启用新队长的管理按钮
 * 5. 记录日志
 *
 * @param guid 新队长的角色GUID
 */
void ArenaTeam::SetCaptain(ObjectGuid guid)
{
    // Disable remove/promote buttons
    Player* oldCaptain = ObjectAccessor::FindPlayer(GetCaptain());
    if (oldCaptain)
        oldCaptain->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 1);

    // Set new captain
    CaptainGuid = guid;

    // Update database
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_CAPTAIN);
    stmt->setUInt32(0, guid.GetCounter());
    stmt->setUInt32(1, GetId());
    CharacterDatabase.Execute(stmt);

    // Enable remove/promote buttons
    if (Player* newCaptain = ObjectAccessor::FindPlayer(guid))
    {
        newCaptain->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 0);
        if (oldCaptain)
        {
            TC_LOG_DEBUG("bg.arena", "Player: {} {} promoted player: {} {} to leader of arena team [Id: {}, Name: {}] [Type: {}].",
                oldCaptain->GetName(), oldCaptain->GetGUID().ToString(), newCaptain->GetName(),
                newCaptain->GetGUID().ToString(), GetId(), GetName(), GetType());
        }
    }
}

/**
 * @brief 从竞技场队伍中移除成员
 *
 * 执行成员移除流程：
 * 1. 遍历队伍成员，找到要移除的成员
 * 2. 移除该成员所在队伍的竞技场队列（如果成员在队列中）
 * 3. 从成员列表中删除该成员
 * 4. 更新角色缓存中的竞技场队伍ID
 * 5. 通知在线玩家（发送退出消息）
 * 6. 清除玩家客户端中的竞技场队伍信息
 * 7. 根据参数决定是否从数据库中删除成员记录
 *
 * @param guid 要移除的玩家GUID
 * @param cleanDb 是否从数据库中删除成员记录（true=单个成员删除，false=队伍解散时批量删除）
 */
void ArenaTeam::DelMember(ObjectGuid guid, bool cleanDb)
{
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    Group* group = (player && player->GetGroup()) ? player->GetGroup() : nullptr;

    // Remove member from team
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // Remove queues of members
        if (Player* playerMember = ObjectAccessor::FindConnectedPlayer(itr->Guid))
        {
            if (group && playerMember->GetGroup() && group->GetGUID() == playerMember->GetGroup()->GetGUID())
            {
                if (BattlegroundQueueTypeId bgQueue = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, GetType()))
                {
                    GroupQueueInfo ginfo;
                    BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(bgQueue);
                    if (queue.GetPlayerGroupInfoData(playerMember->GetGUID(), &ginfo))
                        if (!ginfo.IsInvitedToBGInstanceGUID)
                        {
                            WorldPacket data;
                            playerMember->RemoveBattlegroundQueueId(bgQueue);
                            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, nullptr, playerMember->GetBattlegroundQueueIndex(bgQueue), STATUS_NONE, 0, 0, 0, 0);
                            queue.RemovePlayer(playerMember->GetGUID(), true);
                            playerMember->GetSession()->SendPacket(&data);
                        }
                }
            }
        }

        if (itr->Guid == guid)
        {
            Members.erase(itr);
            sCharacterCache->UpdateCharacterArenaTeamId(guid, GetSlot(), 0);
            break;
        }
    }

    // Inform player and remove arena team info from player data
    if (player)
    {
        player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, GetName(), "", 0);
        // delete all info regarding this team
        for (uint32 i = 0; i < ARENA_TEAM_END; ++i)
            player->SetArenaTeamInfoField(GetSlot(), ArenaTeamInfoType(i), 0);
        TC_LOG_DEBUG("bg.arena", "Player: {} {} left arena team type: {} [Id: {}, Name: {}].", player->GetName(), player->GetGUID().ToString(), GetType(), GetId(), GetName());
    }

    // Only used for single member deletion, for arena team disband we use a single query for more efficiency
    if (cleanDb)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBER);
        stmt->setUInt32(0, GetId());
        stmt->setUInt32(1, guid.GetCounter());
        CharacterDatabase.Execute(stmt);
    }
}

/**
 * @brief 解散竞技场队伍（带会话通知）
 *
 * 执行队伍解散流程：
 * 1. 移除所有成员（不单独清理数据库，稍后批量删除）
 * 2. 如果提供了会话，广播解散消息
 * 3. 从数据库中删除队伍记录和所有成员记录
 * 4. 从ArenaTeamMgr中移除队伍对象
 *
 * @param session 发起解散的玩家会话（用于广播消息）
 */
void ArenaTeam::Disband(WorldSession* session)
{
    // Remove all members from arena team
    while (!Members.empty())
        DelMember(Members.front().Guid, false);

    // Broadcast update
    if (session)
    {
        BroadcastEvent(ERR_ARENA_TEAM_DISBANDED_S, ObjectGuid::Empty, 2, session->GetPlayerName(), GetName(), "");

        if (Player* player = session->GetPlayer())
            TC_LOG_DEBUG("bg.arena", "Player: {} {} disbanded arena team type: {} [Id: {}, Name: {}].", player->GetName(), player->GetGUID().ToString(), GetType(), GetId(), GetName());
    }

    // Update database
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBERS);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    // Remove arena team from ArenaTeamMgr
    sArenaTeamMgr->RemoveArenaTeam(TeamId);
}

/**
 * @brief 解散竞技场队伍（无会话通知）
 *
 * 执行队伍解散流程，不广播解散消息。
 * 通常用于服务器内部清理无效队伍。
 */
void ArenaTeam::Disband()
{
    // Remove all members from arena team
    while (!Members.empty())
        DelMember(Members.front().Guid, false);

    // Update database
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBERS);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    // Remove arena team from ArenaTeamMgr
    sArenaTeamMgr->RemoveArenaTeam(TeamId);
}

/**
 * @brief 发送竞技场队伍成员列表给客户端
 *
 * 构建并发送SMSG_ARENA_TEAM_ROSTER数据包，包含：
 * - 队伍ID、成员数量、队伍类型
 * - 每个成员的详细信息：
 *   - GUID、在线状态、名称
 *   - 是否为队长、等级、职业
 *   - 本周场次/胜场、本赛季场次/胜场
 *   - 个人积分
 *
 * @param session 目标会话（接收数据的客户端）
 */
void ArenaTeam::Roster(WorldSession* session)
{
    Player* player = nullptr;

    uint8 unk308 = 0;

    WorldPacket data(SMSG_ARENA_TEAM_ROSTER, 100);
    data << uint32(GetId());                                    // team id
    data << uint8(unk308);                                      // 3.0.8 unknown value but affect packet structure
    data << uint32(GetMembersSize());                           // members count
    data << uint32(GetType());                                  // arena team type?

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        player = ObjectAccessor::FindConnectedPlayer(itr->Guid);

        data << uint64(itr->Guid);                              // guid
        data << uint8((player ? 1 : 0));                        // online flag
        data << itr->Name;                                      // member name
        data << uint32((itr->Guid == GetCaptain() ? 0 : 1));    // captain flag 0 captain 1 member
        data << uint8((player ? player->GetLevel() : 0));       // unknown, level?
        data << uint8(itr->Class);                              // class
        data << uint32(itr->WeekGames);                         // played this week
        data << uint32(itr->WeekWins);                          // wins this week
        data << uint32(itr->SeasonGames);                       // played this season
        data << uint32(itr->SeasonWins);                        // wins this season
        data << uint32(itr->PersonalRating);                    // personal rating
        //if (unk308)
        //{
        //    data << float(0.0f);                              // 308 unk
        //    data << float(0.0f);                              // 308 unk
        //}
    }

    session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_ARENA_TEAM_ROSTER");
}

/**
 * @brief 响应竞技场队伍查询请求
 *
 * 构建并发送SMSG_ARENA_TEAM_QUERY_RESPONSE数据包，包含：
 * - 队伍ID、名称、类型（2v2, 3v3, 5v5）
 * - 队徽样式信息（背景色、图标样式/颜色、边框样式/颜色）
 *
 * @param session 目标会话（接收数据的客户端）
 */
void ArenaTeam::Query(WorldSession* session)
{
    WorldPacket data(SMSG_ARENA_TEAM_QUERY_RESPONSE, 4*7+GetName().size()+1);
    data << uint32(GetId());                                // team id
    data << GetName();                                      // team name
    data << uint32(GetType());                              // arena team type (2=2x2, 3=3x3 or 5=5x5)
    data << uint32(BackgroundColor);                        // background color
    data << uint32(EmblemStyle);                            // emblem style
    data << uint32(EmblemColor);                            // emblem color
    data << uint32(BorderStyle);                            // border style
    data << uint32(BorderColor);                            // border color
    session->SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_ARENA_TEAM_QUERY_RESPONSE");
}

/**
 * @brief 发送竞技场队伍统计数据给客户端
 *
 * 构建并发送SMSG_ARENA_TEAM_STATS数据包，包含：
 * - 队伍ID、等级积分
 * - 本周场次/胜场、本赛季场次/胜场
 * - 队伍排名
 *
 * @param session 目标会话（接收数据的客户端）
 */
void ArenaTeam::SendStats(WorldSession* session)
{
    WorldPacket data(SMSG_ARENA_TEAM_STATS, 4*7);
    data << uint32(GetId());                                // team id
    data << uint32(Stats.Rating);                           // rating
    data << uint32(Stats.WeekGames);                        // games this week
    data << uint32(Stats.WeekWins);                         // wins this week
    data << uint32(Stats.SeasonGames);                      // played this season
    data << uint32(Stats.SeasonWins);                       // wins this season
    data << uint32(Stats.Rank);                             // rank
    session->SendPacket(&data);
}

/**
 * @brief 通知所有在线成员统计数据已更新
 *
 * 在竞技场比赛结束后调用，向所有在线的队伍成员发送最新的统计数据。
 * 注意：此方法会通知所有成员，即使他们没有参与该场比赛。
 */
void ArenaTeam::NotifyStatsChanged()
{
    // This is called after a rated match ended
    // Updates arena team stats for every member of the team (not only the ones who participated!)
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindConnectedPlayer(itr->Guid))
            SendStats(player->GetSession());
}

/**
 * @brief 响应角色观察时的竞技场队伍信息查询
 *
 * 构建并发送MSG_INSPECT_ARENA_TEAMS数据包，包含：
 * - 被观察玩家的GUID和竞技场槽位
 * - 队伍ID、等级积分
 * - 队伍本赛季场次/胜场
 * - 被观察成员的赛季场次和个人积分
 *
 * @param session 目标会话（发起观察的客户端）
 * @param guid 被观察玩家的GUID
 */
void ArenaTeam::Inspect(WorldSession* session, ObjectGuid guid)
{
    ArenaTeamMember* member = GetMember(guid);
    if (!member)
        return;

    WorldPacket data(MSG_INSPECT_ARENA_TEAMS, 8+1+4*6);
    data << uint64(guid);                                   // player guid
    data << uint8(GetSlot());                               // slot (0...2)
    data << uint32(GetId());                                // arena team id
    data << uint32(Stats.Rating);                           // rating
    data << uint32(Stats.SeasonGames);                      // season played
    data << uint32(Stats.SeasonWins);                       // season wins
    data << uint32(member->SeasonGames);                    // played (count of all games, that the inspected member participated...)
    data << uint32(member->PersonalRating);                 // personal rating
    session->SendPacket(&data);
}

/**
 * @brief 修改成员的个人积分（Personal Rating）
 *
 * 更新成员的个人积分，确保不会低于0。
 * 如果玩家在线，同时更新客户端字段和成就进度。
 *
 * @param player 玩家对象（可以为nullptr）
 * @param mod 积分变化量（可正可负）
 * @param type 竞技场队伍类型（用于确定槽位）
 */
void ArenaTeamMember::ModifyPersonalRating(Player* player, int32 mod, uint32 type)
{
    if (int32(PersonalRating) + mod < 0)
        PersonalRating = 0;
    else
        PersonalRating += mod;

    if (player)
    {
        player->SetArenaTeamInfoField(ArenaTeam::GetSlotByType(type), ARENA_TEAM_PERSONAL_RATING, PersonalRating);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING, PersonalRating, type);
    }
}

/**
 * @brief 修改成员的匹配积分（Matchmaker Rating）
 *
 * 更新成员的匹配积分（MMR），确保不会低于0。
 * MMR用于匹配系统，决定对手的强弱。
 *
 * @param mod 积分变化量（可正可负）
 * @param slot 竞技场槽位（未使用）
 */
void ArenaTeamMember::ModifyMatchmakerRating(int32 mod, uint32 /*slot*/)
{
    if (int32(MatchMakerRating) + mod < 0)
        MatchMakerRating = 0;
    else
        MatchMakerRating += mod;
}

/**
 * @brief 向所有在线成员广播数据包
 *
 * 遍历成员列表，向每个在线的成员发送指定的数据包。
 * 用于队伍内部的消息广播。
 *
 * @param packet 要发送的数据包
 */
void ArenaTeam::BroadcastPacket(WorldPacket* packet)
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindConnectedPlayer(itr->Guid))
            player->SendDirectMessage(packet);
}

/**
 * @brief 广播竞技场队伍事件消息
 *
 * 构建并发送SMSG_ARENA_TEAM_EVENT数据包，向所有在线成员广播事件消息。
 * 支持包含0-3个字符串参数的事件消息。
 *
 * 常见事件类型：
 * - ERR_ARENA_TEAM_JOIN_SS: 成员加入
 * - ERR_ARENA_TEAM_LEAVE_SS: 成员离开
 * - ERR_ARENA_TEAM_REMOVE_SSS: 成员被移除
 * - ERR_ARENA_TEAM_LEADER_CHANGED_SSS: 队长变更
 * - ERR_ARENA_TEAM_DISBANDED_S: 队伍解散
 *
 * @param event 事件类型
 * @param guid 相关角色的GUID（可选）
 * @param strCount 字符串参数数量（0-3）
 * @param str1 第一个字符串参数
 * @param str2 第二个字符串参数
 * @param str3 第三个字符串参数
 */
void ArenaTeam::BroadcastEvent(ArenaTeamEvents event, ObjectGuid guid, uint8 strCount, std::string const& str1, std::string const& str2, std::string const& str3)
{
    WorldPacket data(SMSG_ARENA_TEAM_EVENT, 1+1+1);
    data << uint8(event);
    data << uint8(strCount);
    switch (strCount)
    {
        case 0:
            break;
        case 1:
            data << str1;
            break;
        case 2:
            data << str1 << str2;
            break;
        case 3:
            data << str1 << str2 << str3;
            break;
        default:
            TC_LOG_ERROR("bg.arena", "Unhandled strCount {} in ArenaTeam::BroadcastEvent", strCount);
            return;
    }

    if (guid)
        data << uint64(guid);

    BroadcastPacket(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_ARENA_TEAM_EVENT");
}

/**
 * @brief 批量邀请队伍成员到日历事件
 *
 * 用于竞技场队伍的日历事件邀请功能。
 * 向除发起者外的所有成员发送日历事件邀请。
 *
 * @param session 发起邀请的会话
 */
void ArenaTeam::MassInviteToEvent(WorldSession* session)
{
    WorldPackets::Calendar::CalendarEventInitialInvites packet(false);

    for (ArenaTeamMember const& member : Members)
        if (member.Guid != session->GetPlayer()->GetGUID())
            packet.Invites.emplace_back(member.Guid, 0);

    session->SendPacket(packet.Write());
}

/**
 * @brief 根据竞技场队伍类型获取槽位索引
 *
 * 将竞技场队伍类型映射到玩家数据中的槽位索引：
 * - ARENA_TEAM_2v2 (2) -> 槽位 0
 * - ARENA_TEAM_3v3 (3) -> 槽位 1
 * - ARENA_TEAM_5v5 (5) -> 槽位 2
 *
 * @param type 竞技场队伍类型（2, 3, 或 5）
 * @return uint8 槽位索引（0-2），如果类型无效则返回0xFF
 */
uint8 ArenaTeam::GetSlotByType(uint32 type)
{
    switch (type)
    {
        case ARENA_TEAM_2v2: return 0;
        case ARENA_TEAM_3v3: return 1;
        case ARENA_TEAM_5v5: return 2;
        default:
            break;
    }
    TC_LOG_ERROR("bg.arena", "FATAL: Unknown arena team type {} for some arena team", type);
    return 0xFF;
}

/**
 * @brief 检查指定玩家是否为队伍成员
 *
 * 遍历成员列表，检查指定GUID的玩家是否存在于队伍中。
 *
 * @param guid 要检查的玩家GUID
 * @return true 是队伍成员
 * @return false 不是队伍成员
 */
bool ArenaTeam::IsMember(ObjectGuid guid) const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return true;

    return false;
}

/**
 * @brief 计算竞技场点数奖励
 *
 * 根据队伍类型和积分计算每周可获得的竞技场点数。
 * 计算公式基于暴雪的官方算法：
 *
 * - 积分 <= 1500:
 *   - 第6赛季之前: points = rating * 0.22 + 14
 *   - 第6赛季及之后: points = 344（固定值）
 * - 积分 > 1500:
 *   - points = 1511.26 / (1 + 1639.28 * exp(-0.00412 * rating))
 *
 * 同时应用队伍类型惩罚系数：
 * - 2v2: 乘以 0.76
 * - 3v3: 乘以 0.88
 * - 5v5: 无惩罚
 *
 * @param memberRating 成员的个人积分（如果成员积分+150 < 队伍积分，使用成员积分；否则使用队伍积分）
 * @return uint32 计算得出的竞技场点数
 */
uint32 ArenaTeam::GetPoints(uint32 memberRating)
{
    // Returns how many points would be awarded with this team type with this rating
    float points;

    uint32 rating = memberRating + 150 < Stats.Rating ? memberRating : Stats.Rating;

    if (rating <= 1500)
    {
        if (sWorld->getIntConfig(CONFIG_ARENA_SEASON_ID) < 6)
            points = (float)rating * 0.22f + 14.0f;
        else
            points = 344;
    }
    else
        points = 1511.26f / (1.0f + 1639.28f * std::exp(-0.00412f * float(rating)));

    // Type penalties for teams < 5v5
    if (Type == ARENA_TEAM_2v2)
        points *= 0.76f;
    else if (Type == ARENA_TEAM_3v3)
        points *= 0.88f;

    points *= sWorld->getRate(RATE_ARENA_POINTS);

    return (uint32) points;
}

/**
 * @brief 计算队伍的平均匹配积分（MMR）
 *
 * 遍历队伍成员，计算在线且在指定队伍中的成员的平均MMR。
 * 用于匹配系统确定对手的强弱。
 *
 * 计算规则：
 * - 只统计在线玩家
 * - 只统计属于指定队伍的成员
 * - 平均值 = 所有符合条件成员的MMR总和 / 成员数量
 *
 * @param group 当前队伍对象
 * @return uint32 平均MMR，如果没有符合条件的成员返回0
 */
uint32 ArenaTeam::GetAverageMMR(Group* group) const
{
    if (!group)
        return 0;

    uint32 matchMakerRating = 0;
    uint32 playerDivider = 0;
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // Skip if player is not online
        if (!ObjectAccessor::FindConnectedPlayer(itr->Guid))
            continue;

        // Skip if player is not member of group
        if (!group->IsMember(itr->Guid))
            continue;

        matchMakerRating += itr->MatchMakerRating;
        ++playerDivider;
    }

    // x/0 = crash
    if (playerDivider == 0)
        playerDivider = 1;

    matchMakerRating /= playerDivider;

    return matchMakerRating;
}

/**
 * @brief 计算获胜概率（基于ELO系统）
 *
 * 根据己方积分和对手积分，计算己方的获胜概率。
 * 公式：P = 1 / (1 + exp(ln(10) * (opponentRating - ownRating) / 650))
 *
 * 这是标准ELO系统的一个变种，用于计算积分变化的基础。
 *
 * @param ownRating 己方积分
 * @param opponentRating 对手积分
 * @return float 获胜概率（0.0 - 1.0）
 */
float ArenaTeam::GetChanceAgainst(uint32 ownRating, uint32 opponentRating)
{
    // Returns the chance to win against a team with the given rating, used in the rating adjustment calculation
    // ELO system
    return 1.0f / (1.0f + std::exp(std::log(10.0f) * (float(opponentRating) - float(ownRating)) / 650.0f));
}

/**
 * @brief 计算匹配积分（MMR）变化量
 *
 * 基于ELO系统计算匹配积分的变化量。
 * 积分变化 = (实际结果 - 预期胜率) * 配置修正系数
 *
 * 实际结果：
 * - 胜利: 1.0
 * - 失败: 0.0
 *
 * @param ownRating 己方MMR
 * @param opponentRating 对手MMR
 * @param won 是否获胜
 * @return int32 MMR变化量（向上取整）
 */
int32 ArenaTeam::GetMatchmakerRatingMod(uint32 ownRating, uint32 opponentRating, bool won /*, float& confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;
    float mod = won_mod - chance;

    // Work in progress:
    /*
    // This is a simulation, as there is not much info on how it really works
    float confidence_mod = min(1.0f - fabs(mod), 0.5f);

    // Apply confidence factor to the mod:
    mod *= confidence_factor

    // And only after that update the new confidence factor
    confidence_factor -= ((confidence_factor - 1.0f) * confidence_mod) / confidence_factor;
    */

    // Real rating modification
    mod *= sWorld->getFloatConfig(CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER);

    return (int32)ceil(mod);
}

/**
 * @brief 计算队伍积分（Rating）变化量
 *
 * 基于ELO系统计算队伍积分的变化量。
 * 根据当前积分区间使用不同的修正系数：
 *
 * 胜利时：
 * - 积分 < 1000: 使用 ARENA_WIN_RATING_MODIFIER_1
 * - 1000 <= 积分 < 1300: 渐变修正系数
 * - 积分 >= 1300: 使用 ARENA_WIN_RATING_MODIFIER_2
 *
 * 失败时：
 * - 使用 ARENA_LOSE_RATING_MODIFIER
 *
 * @param ownRating 己方积分
 * @param opponentRating 对手积分
 * @param won 是否获胜
 * @return int32 积分变化量（向上取整）
 */
int32 ArenaTeam::GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won /*, float confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);

    // Calculate the rating modification
    float mod;

    /// @todo Replace this hack with using the confidence factor (limiting the factor to 2.0f)
    if (won)
    {
        if (ownRating < 1300)
        {
            float win_rating_modifier1 = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_1);

            if (ownRating < 1000)
                mod =  win_rating_modifier1 * (1.0f - chance);
            else
                mod = ((win_rating_modifier1 / 2.0f) + ((win_rating_modifier1 / 2.0f) * (1300.0f - float(ownRating)) / 300.0f)) * (1.0f - chance);
        }
        else
            mod = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_2) * (1.0f - chance);
    }
    else
        mod = sWorld->getFloatConfig(CONFIG_ARENA_LOSE_RATING_MODIFIER) * (-chance);

    return (int32)ceil(mod);
}

/**
 * @brief 结束一场比赛，更新队伍统计数据
 *
 * 在每场比赛（无论胜负）后调用，执行以下操作：
 * 1. 更新队伍等级积分（确保不低于0）
 * 2. 检查并更新在线成员的成就进度（最高队伍积分）
 * 3. 增加本周和本赛季的比赛场次
 * 4. 重新计算队伍排名（遍历所有同类型队伍，统计积分高于本队的数量）
 *
 * @param mod 积分变化量（可为负数）
 */
void ArenaTeam::FinishGame(int32 mod)
{
    // Rating can only drop to 0
    if (int32(Stats.Rating) + mod < 0)
        Stats.Rating = 0;
    else
    {
        Stats.Rating += mod;

        // Check if rating related achivements are met
        for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
            if (Player* member = ObjectAccessor::FindConnectedPlayer(itr->Guid))
                member->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING, Stats.Rating, Type);
    }

    // Update number of games played per season or week
    Stats.WeekGames += 1;
    Stats.SeasonGames += 1;

    // Update team's rank, start with rank 1 and increase until no team with more rating was found
    Stats.Rank = 1;
    for (auto [teamId, team] : sArenaTeamMgr->GetArenaTeams())
        if (team->GetType() == Type && team->GetStats().Rating > Stats.Rating)
            ++Stats.Rank;
}

/**
 * @brief 处理比赛胜利
 *
 * 在队伍获胜后调用，执行以下操作：
 * 1. 计算并返回MMR变化量
 * 2. 计算队伍积分变化量（通过引用参数返回）
 * 3. 调用FinishGame更新队伍统计数据
 * 4. 增加本周和本赛季的胜场
 *
 * @param Own_MMRating 己方MMR
 * @param Opponent_MMRating 对手MMR
 * @param rating_change 输出参数：队伍积分变化量
 * @return int32 MMR变化量
 */
int32 ArenaTeam::WonAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change)
{
    // Called when the team has won
    // Change in Matchmaker rating
    int32 mod = GetMatchmakerRatingMod(Own_MMRating, Opponent_MMRating, true);

    // Change in Team Rating
    rating_change = GetRatingMod(Stats.Rating, Opponent_MMRating, true);

    // Modify the team stats accordingly
    FinishGame(rating_change);

    // Update number of wins per season and week
    Stats.WeekWins += 1;
    Stats.SeasonWins += 1;

    // Return the rating change, used to display it on the results screen
    return mod;
}

/**
 * @brief 处理比赛失败
 *
 * 在队伍失败后调用，执行以下操作：
 * 1. 计算并返回MMR变化量
 * 2. 计算队伍积分变化量（通过引用参数返回）
 * 3. 调用FinishGame更新队伍统计数据
 *
 * @param Own_MMRating 己方MMR
 * @param Opponent_MMRating 对手MMR
 * @param rating_change 输出参数：队伍积分变化量
 * @return int32 MMR变化量
 */
int32 ArenaTeam::LostAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change)
{
    // Called when the team has lost
    // Change in Matchmaker Rating
    int32 mod = GetMatchmakerRatingMod(Own_MMRating, Opponent_MMRating, false);

    // Change in Team Rating
    rating_change = GetRatingMod(Stats.Rating, Opponent_MMRating, false);

    // Modify the team stats accordingly
    FinishGame(rating_change);

    // return the rating change, used to display it on the results screen
    return mod;
}

/**
 * @brief 处理在线成员比赛失败
 *
 * 在比赛失败后，为指定的在线成员更新个人数据：
 * 1. 更新个人积分（基于对手MMR计算）
 * 2. 更新MMR
 * 3. 增加本周和本赛季的比赛场次
 * 4. 更新客户端显示字段
 *
 * @param player 参赛玩家对象
 * @param againstMatchmakerRating 对手的MMR
 * @param MatchmakerRatingChange MMR变化量
 */
void ArenaTeam::MemberLost(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    // Called for each participant of a match after losing
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            // Update personal rating
            int32 mod = GetRatingMod(itr->PersonalRating, againstMatchmakerRating, false);
            itr->ModifyPersonalRating(player, mod, GetType());

            // Update matchmaker rating
            itr->ModifyMatchmakerRating(MatchmakerRatingChange, GetSlot());

            // Update personal played stats
            itr->WeekGames +=1;
            itr->SeasonGames +=1;

            // update the unit fields
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_WEEK,  itr->WeekGames);
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_SEASON,  itr->SeasonGames);
            return;
        }
    }
}

/**
 * @brief 处理离线成员比赛失败
 *
 * 在比赛失败后，为指定的离线成员更新个人数据。
 * 与MemberLost类似，但不更新客户端字段。
 *
 * @param guid 离线玩家的GUID
 * @param againstMatchmakerRating 对手的MMR
 * @param MatchmakerRatingChange MMR变化量
 */
void ArenaTeam::OfflineMemberLost(ObjectGuid guid, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    // Called for offline player after ending rated arena match!
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == guid)
        {
            // update personal rating
            int32 mod = GetRatingMod(itr->PersonalRating, againstMatchmakerRating, false);
            itr->ModifyPersonalRating(nullptr, mod, GetType());

            // update matchmaker rating
            itr->ModifyMatchmakerRating(MatchmakerRatingChange, GetSlot());

            // update personal played stats
            itr->WeekGames += 1;
            itr->SeasonGames += 1;
            return;
        }
    }
}

/**
 * @brief 处理在线成员比赛胜利
 *
 * 在比赛胜利后，为指定的在线成员更新个人数据：
 * 1. 更新个人积分（基于对手MMR计算）
 * 2. 更新MMR
 * 3. 增加本周和本赛季的比赛场次和胜场
 * 4. 更新客户端显示字段
 *
 * @param player 参赛玩家对象
 * @param againstMatchmakerRating 对手的MMR
 * @param MatchmakerRatingChange MMR变化量
 */
void ArenaTeam::MemberWon(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    // called for each participant after winning a match
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            // update personal rating
            int32 mod = GetRatingMod(itr->PersonalRating, againstMatchmakerRating, true);
            itr->ModifyPersonalRating(player, mod, GetType());

            // update matchmaker rating
            itr->ModifyMatchmakerRating(MatchmakerRatingChange, GetSlot());

            // update personal stats
            itr->WeekGames +=1;
            itr->SeasonGames +=1;
            itr->SeasonWins += 1;
            itr->WeekWins += 1;
            // update unit fields
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_WEEK, itr->WeekGames);
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_SEASON, itr->SeasonGames);
            return;
        }
    }
}

/**
 * @brief 辅助函数：计算并更新成员的竞技场点数
 *
 * 在每周竞技场点数结算时调用，为符合条件的成员计算竞技场点数。
 * 计算规则：
 * 1. 队伍本周至少需要打10场比赛
 * 2. 成员至少参与30%的队伍比赛
 * 3. 成员可获得的最大点数取决于其个人积分或队伍积分
 *
 * 该函数将结果存入playerPoints映射表，每个玩家只保留最高点数
 * （因为一个玩家可能同时拥有多支同类型队伍）。
 *
 * @param playerPoints 输出参数：玩家GUID到竞技场点数的映射表
 */
void ArenaTeam::UpdateArenaPointsHelper(std::map<uint32, uint32>& playerPoints)
{
    // Called after a match has ended and the stats are already modified
    // Helper function for arena point distribution (this way, when distributing, no actual calculation is required, just a few comparisons)
    // 10 played games per week is a minimum
    if (Stats.WeekGames < 10)
        return;

    // To get points, a player has to participate in at least 30% of the matches
    uint32 requiredGames = (uint32)ceil(Stats.WeekGames * 0.3f);

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // The player participated in enough games, update his points
        uint32 pointsToAdd = 0;
        if (itr->WeekGames >= requiredGames)
            pointsToAdd = GetPoints(itr->PersonalRating);

        std::map<uint32, uint32>::iterator plr_itr = playerPoints.find(itr->Guid.GetCounter());
        if (plr_itr != playerPoints.end())
        {
            // Check if there is already more points
            if (plr_itr->second < pointsToAdd)
                playerPoints[itr->Guid.GetCounter()] = pointsToAdd;
        }
        else
            playerPoints[itr->Guid.GetCounter()] = pointsToAdd;
    }
}

/**
 * @brief 将队伍和成员数据保存到数据库
 *
 * 在比赛结束后或竞技场点数结算时调用，执行以下保存操作：
 * 1. 更新队伍统计数据（等级积分、周场次/胜场、赛季场次/胜场、排名）
 * 2. 更新每个成员的个人数据（如果本周有比赛或强制保存）：
 *    - 个人积分、周场次/胜场、赛季场次/胜场
 *    - 匹配积分（MMR）
 *
 * 使用事务确保数据一致性。
 *
 * @param forceMemberSave 是否强制保存所有成员数据（即使本周未参赛）
 */
void ArenaTeam::SaveToDB(bool forceMemberSave)
{
    // Save team and member stats to db
    // Called after a match has ended or when calculating arena_points

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_STATS);
    stmt->setUInt16(0, Stats.Rating);
    stmt->setUInt16(1, Stats.WeekGames);
    stmt->setUInt16(2, Stats.WeekWins);
    stmt->setUInt16(3, Stats.SeasonGames);
    stmt->setUInt16(4, Stats.SeasonWins);
    stmt->setUInt32(5, Stats.Rank);
    stmt->setUInt32(6, GetId());
    trans->Append(stmt);

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // Save the effort and go
        if (itr->WeekGames == 0 && !forceMemberSave)
            continue;

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_MEMBER);
        stmt->setUInt16(0, itr->PersonalRating);
        stmt->setUInt16(1, itr->WeekGames);
        stmt->setUInt16(2, itr->WeekWins);
        stmt->setUInt16(3, itr->SeasonGames);
        stmt->setUInt16(4, itr->SeasonWins);
        stmt->setUInt32(5, GetId());
        stmt->setUInt32(6, itr->Guid.GetCounter());
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHARACTER_ARENA_STATS);
        stmt->setUInt32(0, itr->Guid.GetCounter());
        stmt->setUInt8(1, GetSlot());
        stmt->setUInt16(2, itr->MatchMakerRating);
        trans->Append(stmt);
    }

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 结算并重置本周数据
 *
 * 在每周竞技场点数结算后调用，重置本周统计数据：
 * - 队伍的本周场次和胜场
 * - 所有成员的本周场次和胜场
 *
 * @return true 成功重置（本周有比赛）
 * @return false 无需重置（本周无比赛）
 */
bool ArenaTeam::FinishWeek()
{
    // No need to go further than this
    if (Stats.WeekGames == 0)
        return false;

    // Reset team stats
    Stats.WeekGames = 0;
    Stats.WeekWins = 0;

    // Reset member stats
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        itr->WeekGames = 0;
        itr->WeekWins = 0;
    }

    return true;
}

/**
 * @brief 检查队伍是否有成员正在比赛中
 *
 * 遍历成员列表，检查是否有在线成员当前处于竞技场比赛中。
 * 用于判断队伍是否可以加入新的比赛队列。
 *
 * @return true 有成员正在比赛中
 * @return false 没有成员在比赛中
 */
bool ArenaTeam::IsFighting() const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(itr->Guid))
            if (player->GetMap()->IsBattleArena())
                return true;

    return false;
}

/**
 * @brief 根据名称获取成员信息
 *
 * 遍历成员列表，查找指定名称的成员。
 *
 * @param name 成员名称
 * @return ArenaTeamMember* 成员指针，如果未找到返回nullptr
 */
ArenaTeamMember* ArenaTeam::GetMember(const std::string& name)
{
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Name == name)
            return &(*itr);

    return nullptr;
}

/**
 * @brief 根据GUID获取成员信息
 *
 * 遍历成员列表，查找指定GUID的成员。
 *
 * @param guid 成员GUID
 * @return ArenaTeamMember* 成员指针，如果未找到返回nullptr
 */
ArenaTeamMember* ArenaTeam::GetMember(ObjectGuid guid)
{
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return &(*itr);

    return nullptr;
}
