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
 * @file ArenaTeamMgr.cpp
 * @brief 竞技场战队管理器实现文件
 *
 * 本文件实现了竞技场战队管理器(ArenaTeamMgr)的核心功能,包括:
 * - 竞技场战队的创建、删除和查询
 * - 从数据库加载竞技场战队数据
 * - 竞技场点数的分发
 * - 竞技场战队ID的生成
 *
 * 竞技场战队是《魔兽世界》竞技场系统的核心组成部分,支持2v2、3v3和5v5三种模式。
 */

#include "Define.h"
#include "ArenaTeamMgr.h"
#include "World.h"
#include "Log.h"
#include "DatabaseEnv.h"
#include "Language.h"
#include "Player.h"
#include "ObjectAccessor.h"

/**
 * @brief 构造函数 - 初始化竞技场战队管理器
 *
 * 将下一个可用的竞技场战队ID初始化为1。
 * 竞技场战队ID从1开始递增分配。
 */
ArenaTeamMgr::ArenaTeamMgr()
{
    NextArenaTeamId = 1;
}

/**
 * @brief 析构函数 - 清理所有竞技场战队对象
 *
 * 遍历并删除所有存储在管理器中的竞技场战队对象,
 * 防止内存泄漏。
 */
ArenaTeamMgr::~ArenaTeamMgr()
{
    for (ArenaTeamContainer::iterator itr = ArenaTeamStore.begin(); itr != ArenaTeamStore.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 获取竞技场战队管理器的单例实例
 *
 * 使用静态局部变量实现单例模式,确保整个服务器进程中
 * 只有一个ArenaTeamMgr实例存在。
 *
 * @return ArenaTeamMgr* 返回竞技场战队管理器的单例指针
 */
ArenaTeamMgr* ArenaTeamMgr::instance()
{
    static ArenaTeamMgr instance;
    return &instance;
}

/**
 * @brief 根据ID获取竞技场战队
 *
 * 从管理器的容器中查找指定ID的竞技场战队。
 * 这是最常用的查询方式,时间复杂度为O(log n)。
 *
 * @param arenaTeamId 竞技场战队的唯一标识符
 * @return ArenaTeam* 如果找到返回战队指针,否则返回nullptr
 */
ArenaTeam* ArenaTeamMgr::GetArenaTeamById(uint32 arenaTeamId) const
{
    ArenaTeamContainer::const_iterator itr = ArenaTeamStore.find(arenaTeamId);
    if (itr != ArenaTeamStore.end())
        return itr->second;
    return nullptr;
}

/**
 * @brief 根据名称获取竞技场战队
 *
 * 遍历所有竞技场战队,查找名称匹配的战队。
 * 名称比较不区分大小写。
 * 时间复杂度为O(n),适用于名称唯一性检查。
 *
 * @param arenaTeamName 要查找的竞技场战队名称
 * @return ArenaTeam* 如果找到返回战队指针,否则返回nullptr
 */
ArenaTeam* ArenaTeamMgr::GetArenaTeamByName(std::string_view arenaTeamName) const
{
    for (auto [teamId, team] : ArenaTeamStore)
        if (StringEqualI(arenaTeamName, team->GetName()))
            return team;
    return nullptr;
}

/**
 * @brief 根据队长获取竞技场战队
 *
 * 遍历所有竞技场战队,查找指定玩家作为队长的战队。
 * 一个玩家只能是一个竞技场战队的队长。
 * 时间复杂度为O(n)。
 *
 * @param guid 队长的玩家GUID
 * @return ArenaTeam* 如果找到返回战队指针,否则返回nullptr
 */
ArenaTeam* ArenaTeamMgr::GetArenaTeamByCaptain(ObjectGuid guid) const
{
    for (auto [teamId, team] : ArenaTeamStore)
        if (team->GetCaptain() == guid)
            return team;
    return nullptr;
}

/**
 * @brief 添加竞技场战队到管理器
 *
 * 将指定的竞技场战队添加到管理器的容器中进行管理。
 * 使用断言检查防止添加重复ID的战队,确保数据完整性。
 *
 * @param arenaTeam 要添加的竞技场战队指针
 * @note 如果已存在相同ID的战队且不是同一个对象,将触发断言失败
 */
void ArenaTeamMgr::AddArenaTeam(ArenaTeam* arenaTeam)
{
    ArenaTeam*& team = ArenaTeamStore[arenaTeam->GetId()];
    ASSERT((team == nullptr) || (team == arenaTeam), "Duplicate arena team with ID %u", arenaTeam->GetId());
    team = arenaTeam;
}

/**
 * @brief 从管理器中移除竞技场战队
 *
 * 从管理器的容器中删除指定ID的竞技场战队记录。
 * 注意:此函数仅从容器中移除,不删除战队对象本身。
 *
 * @param arenaTeamId 要移除的竞技场战队ID
 */
void ArenaTeamMgr::RemoveArenaTeam(uint32 arenaTeamId)
{
    ArenaTeamStore.erase(arenaTeamId);
}

/**
 * @brief 生成新的竞技场战队ID
 *
 * 生成一个唯一的竞技场战队ID。ID从1开始递增。
 * 当ID接近最大值(0xFFFFFFFE)时,会记录错误日志并关闭服务器,
 * 因为竞技场战队ID溢出会导致严重的数据问题。
 *
 * @return uint32 新生成的竞技场战队ID
 * @note 理论上ID不会溢出,但在长期运行的服务器上仍需检查
 */
uint32 ArenaTeamMgr::GenerateArenaTeamId()
{
    if (NextArenaTeamId >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("bg.battleground", "Arena team ids overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return NextArenaTeamId++;
}

/**
 * @brief 从数据库加载所有竞技场战队
 *
 * 这是服务器启动时调用的核心加载函数,负责:
 * 1. 清理数据库中的孤立成员记录(没有对应战队的成员)
 * 2. 从arena_team表加载战队基本信息
 * 3. 从arena_team_member表加载战队成员信息
 * 4. 关联角色名称和竞技场统计数据
 * 5. 创建并初始化所有竞技场战队对象
 *
 * 加载过程:
 * - 第一个查询获取战队的ID、名称、队长、类型(2v2/3v3/5v5)、
 *   战队徽章样式和颜色、等级、战绩等基本信息
 * - 第二个查询获取成员的GUID、战绩、个人等级、匹配等级等信息
 * - 通过LEFT JOIN关联角色名称和职业信息
 * - 通过LEFT JOIN关联角色竞技场统计数据
 *
 * 如果加载失败(如数据不完整),会自动解散并删除该战队。
 *
 * @note 此函数仅在服务器启动时调用一次
 */
void ArenaTeamMgr::LoadArenaTeams()
{
    uint32 oldMSTime = getMSTime();

    // Clean out the trash before loading anything
    // 清理无效的竞技场战队成员记录(没有对应战队的成员)
    CharacterDatabase.DirectExecute("DELETE FROM arena_team_member WHERE arenaTeamId NOT IN (SELECT arenaTeamId FROM arena_team)");       // One-time query

    //                                                        0        1         2         3          4              5            6            7           8
    // 查询所有竞技场战队的基本信息:
    // arenaTeamId(战队ID), name(战队名), captainGuid(队长GUID), type(类型2/3/5),
    // backgroundColor(背景色), emblemStyle(徽章样式), emblemColor(徽章颜色),
    // borderStyle(边框样式), borderColor(边框颜色)
    QueryResult result = CharacterDatabase.Query("SELECT arenaTeamId, name, captainGuid, type, backgroundColor, emblemStyle, emblemColor, borderStyle, borderColor, "
    //      9        10        11         12           13       14
    // rating(等级分), weekGames(本周场次), weekWins(本周胜场),
    // seasonGames(赛季场次), seasonWins(赛季胜场), rank(排名)
        "rating, weekGames, weekWins, seasonGames, seasonWins, `rank` FROM arena_team ORDER BY arenaTeamId ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 arena teams. DB table `arena_team` is empty!");
        return;
    }

    // 查询所有竞技场战队成员信息:
    // arenaTeamId(战队ID), atm.guid(成员GUID),
    // atm.weekGames(本周场次), atm.weekWins(本周胜场),
    // atm.seasonGames(赛季场次), atm.seasonWins(赛季胜场),
    // c.name(角色名), class(职业), personalRating(个人等级),
    // matchMakerRating(匹配等级)
    QueryResult result2 = CharacterDatabase.Query(
        //              0              1           2             3              4                 5          6     7          8                  9
        "SELECT arenaTeamId, atm.guid, atm.weekGames, atm.weekWins, atm.seasonGames, atm.seasonWins, c.name, class, personalRating, matchMakerRating FROM arena_team_member atm"
        " INNER JOIN arena_team ate USING (arenaTeamId)"
        " LEFT JOIN characters AS c ON atm.guid = c.guid"
        // 关联角色竞技场统计表,根据战队类型匹配对应的槽位:
        // type=2(2v2)对应slot=0, type=3(3v3)对应slot=1, type=5(5v5)对应slot=2
        " LEFT JOIN character_arena_stats AS cas ON c.guid = cas.guid AND (cas.slot = 0 AND ate.type = 2 OR cas.slot = 1 AND ate.type = 3 OR cas.slot = 2 AND ate.type = 5)"
        " ORDER BY atm.arenateamid ASC");

    uint32 count = 0;
    do
    {
        ArenaTeam* newArenaTeam = new ArenaTeam;

        // 加载战队基本信息和成员信息,如果任一失败则解散战队
        if (!newArenaTeam->LoadArenaTeamFromDB(result) || !newArenaTeam->LoadMembersFromDB(result2))
        {
            newArenaTeam->Disband(nullptr);
            delete newArenaTeam;
            continue;
        }

        AddArenaTeam(newArenaTeam);

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} arena teams in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 分发竞技场点数
 *
 * 这是每周维护时调用的核心函数,根据玩家上周的竞技场表现分发竞技场点数。
 * 竞技场点数是《魔兽世界》中用于购买PvP装备的货币。
 *
 * 分发流程:
 * 1. 向全服玩家发送分发开始通知
 * 2. 遍历所有竞技场战队,计算每个成员应得的点数
 *    - 玩家可以从多个战队中获得点数,取最高值
 * 3. 为在线玩家直接添加点数,为离线玩家更新数据库
 * 4. 更新所有战队的周战绩(重置本周数据)
 * 5. 保存战队状态到数据库
 * 6. 发送分发完成通知
 *
 * 点数计算规则:
 * - 基于玩家的个人等级(Team Rating)
 * - 2v2、3v3、5v5分别计算,取最高值
 * - 等级越高,获得的点数越多
 *
 * @note 此函数通常在每周服务器维护时自动调用
 * @note 数据库事务确保点数分发的原子性和一致性
 */
void ArenaTeamMgr::DistributeArenaPoints()
{
    // Used to distribute arena points based on last week's stats
    // 向全服发送竞技场点数分发开始通知
    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_START);

    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_ONLINE_START);

    // Temporary structure for storing maximum points to add values for all players
    // 临时存储结构:记录每个玩家应该获得的最大点数(从所有战队中取最高值)
    std::map<uint32, uint32> PlayerPoints;

    // At first update all points for all team members
    // 首先计算所有战队成员应得的竞技场点数
    for (auto [teamId, team] : ArenaTeamStore)
        team->UpdateArenaPointsHelper(PlayerPoints);

    // 创建数据库事务,确保所有点数更新的原子性
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt;

    // Cycle that gives points to all players
    // 遍历所有玩家,分发竞技场点数
    for (std::map<uint32, uint32>::iterator playerItr = PlayerPoints.begin(); playerItr != PlayerPoints.end(); ++playerItr)
    {
        // Add points to player if online
        // 如果玩家在线,直接修改玩家对象的竞技场点数
        if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid(HighGuid::Player, playerItr->first)))
            player->ModifyArenaPoints(playerItr->second, trans);
        else    // Update database
        {
            // 如果玩家离线,直接更新数据库中的竞技场点数
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_CHAR_ARENA_POINTS);
            stmt->setUInt32(0, playerItr->second);
            stmt->setUInt32(1, playerItr->first);
            trans->Append(stmt);
        }
    }

    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);

    // 清理临时数据
    PlayerPoints.clear();

    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_ONLINE_END);

    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_TEAM_START);
    // 遍历所有战队,完成周结算
    for (auto [teamId, team] : ArenaTeamStore)
    {
        // FinishWeek返回true表示需要保存(有战绩数据)
        // 这会重置战队的本周战绩统计
        if (team->FinishWeek())
            team->SaveToDB(true);

        // 通知客户端更新战队统计数据
        team->NotifyStatsChanged();
    }

    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_TEAM_END);

    sWorld->SendWorldText(LANG_DIST_ARENA_POINTS_END);
}
