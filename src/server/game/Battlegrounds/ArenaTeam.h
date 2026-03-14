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

#ifndef TRINITYCORE_ARENATEAM_H
#define TRINITYCORE_ARENATEAM_H

#include "QueryResult.h"
#include "ObjectGuid.h"
#include <list>
#include <map>

class WorldSession;
class WorldPacket;
class Player;
class Group;

/**
 * @file ArenaTeam.h
 * @brief 竞技场战队系统头文件
 *
 * 本文件定义了竞技场战队相关的数据结构、枚举类型和核心类。
 * 竞技场战队是玩家组织的PvP团队，可以参加2v2、3v3或5v5的竞技场战斗。
 * 战队拥有等级、成员列表、战绩统计等属性。
 */

/**
 * @enum ArenaTeamCommandTypes
 * @brief 竞技场战队命令类型枚举
 *
 * 定义了竞技场战队操作的成功响应类型，
 * 用于向客户端发送操作成功的通知消息。
 */
enum ArenaTeamCommandTypes
{
    ERR_ARENA_TEAM_CREATE_S                 = 0x00,  ///< 竞技场战队创建成功
    ERR_ARENA_TEAM_INVITE_SS                = 0x01,  ///< 竞技场战队邀请成功
    ERR_ARENA_TEAM_QUIT_S                   = 0x03,  ///< 退出竞技场战队成功
    ERR_ARENA_TEAM_FOUNDER_S                = 0x0E   ///< 竞技场战队创建者标识
};

/**
 * @enum ArenaTeamCommandErrors
 * @brief 竞技场战队命令错误枚举
 *
 * 定义了竞技场战队操作过程中可能出现的各种错误代码，
 * 用于向客户端反馈操作失败的原因。
 */
enum ArenaTeamCommandErrors
{
    ERR_ARENA_TEAM_INTERNAL                 = 0x01,  ///< 内部错误
    ERR_ALREADY_IN_ARENA_TEAM               = 0x02,  ///< 玩家已在竞技场战队中
    ERR_ALREADY_IN_ARENA_TEAM_S             = 0x03,  ///< 玩家已在竞技场战队中（带字符串参数）
    ERR_INVITED_TO_ARENA_TEAM               = 0x04,  ///< 玩家已被邀请加入竞技场战队
    ERR_ALREADY_INVITED_TO_ARENA_TEAM_S     = 0x05,  ///< 玩家已被邀请加入竞技场战队（带字符串参数）
    ERR_ARENA_TEAM_NAME_INVALID             = 0x06,  ///< 竞技场战队名称无效
    ERR_ARENA_TEAM_NAME_EXISTS_S            = 0x07,  ///< 竞技场战队名称已存在
    ERR_ARENA_TEAM_LEADER_LEAVE_S           = 0x08,  ///< 竞技场战队队长不能离开战队
    ERR_ARENA_TEAM_PERMISSIONS              = 0x08,  ///< 权限不足
    ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM       = 0x09,  ///< 玩家不在竞技场战队中
    ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM_SS    = 0x0A,  ///< 玩家不在竞技场战队中（带字符串参数）
    ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S       = 0x0B,  ///< 找不到玩家
    ERR_ARENA_TEAM_NOT_ALLIED               = 0x0C,  ///< 玩家不是同盟阵营
    ERR_ARENA_TEAM_IGNORING_YOU_S           = 0x13,  ///< 玩家已将你加入忽略列表
    ERR_ARENA_TEAM_TARGET_TOO_LOW_S         = 0x15,  ///< 目标玩家等级过低
    ERR_ARENA_TEAM_TARGET_TOO_HIGH_S        = 0x16,  ///< 目标玩家等级过高
    ERR_ARENA_TEAM_TOO_MANY_MEMBERS_S       = 0x17,  ///< 竞技场战队成员数量已满
    ERR_ARENA_TEAM_NOT_FOUND                = 0x1B,  ///< 找不到竞技场战队
    ERR_ARENA_TEAMS_LOCKED                  = 0x1E   ///< 竞技场战队已锁定
};

/**
 * @enum ArenaTeamEvents
 * @brief 竞技场战队事件枚举
 *
 * 定义了竞技场战队中发生的各种事件类型，
 * 用于向战队成员广播通知消息。
 */
enum ArenaTeamEvents
{
    ERR_ARENA_TEAM_JOIN_SS                  = 3,     ///< 成员加入战队事件（玩家名称 + 战队名称）
    ERR_ARENA_TEAM_LEAVE_SS                 = 4,     ///< 成员离开战队事件（玩家名称 + 战队名称）
    ERR_ARENA_TEAM_REMOVE_SSS               = 5,     ///< 成员被移除事件（玩家名称 + 战队名称 + 队长名称）
    ERR_ARENA_TEAM_LEADER_IS_SS             = 6,     ///< 队长标识事件（玩家名称 + 战队名称）
    ERR_ARENA_TEAM_LEADER_CHANGED_SSS       = 7,     ///< 队长变更事件（旧队长 + 新队长 + 战队名称）
    ERR_ARENA_TEAM_DISBANDED_S              = 8      ///< 战队解散事件（队长名称 + 战队名称）
};

/*
need info how to send these ones:
ERR_ARENA_TEAM_YOU_JOIN_S - client show it automatically when accept invite
ERR_ARENA_TEAM_TARGET_TOO_LOW_S
ERR_ARENA_TEAM_TOO_MANY_MEMBERS_S
ERR_ARENA_TEAM_LEVEL_TOO_LOW_I
*/

/**
 * @enum ArenaTeamTypes
 * @brief 竞技场战队类型枚举
 *
 * 定义了竞技场战队的规模类型，决定战队成员数量上限和比赛形式。
 * 枚举值同时代表每队参与战斗的玩家数量。
 */
enum ArenaTeamTypes
{
    ARENA_TEAM_2v2      = 2,  ///< 2v2竞技场战队（每队2名玩家）
    ARENA_TEAM_3v3      = 3,  ///< 3v3竞技场战队（每队3名玩家）
    ARENA_TEAM_5v5      = 5   ///< 5v5竞技场战队（每队5名玩家）
};

/**
 * @struct ArenaTeamMember
 * @brief 竞技场战队成员数据结构
 *
 * 存储竞技场战队成员的详细信息，包括玩家标识、职业、
 * 战绩统计和个人等级等数据。每个成员都有独立的个人等级和匹配等级。
 */
struct TC_GAME_API ArenaTeamMember
{
    ObjectGuid Guid;              ///< 成员的GUID（全局唯一标识符）
    std::string Name;             ///< 成员角色名称
    uint8 Class;                  ///< 成员职业（战士、法师等）
    uint16 WeekGames;             ///< 本周参赛场次
    uint16 WeekWins;              ///< 本周获胜场次
    uint16 SeasonGames;           ///< 本赛季参赛场次
    uint16 SeasonWins;            ///< 本赛季获胜场次
    uint16 PersonalRating;        ///< 个人等级（PR）
    uint16 MatchMakerRating;      ///< 匹配等级（MMR）

    /**
     * @brief 修改个人等级
     *
     * 调整成员的个人等级（Personal Rating），会进行边界检查和数据库更新。
     *
     * @param player 玩家对象指针，用于日志记录和通知
     * @param mod 等级修改值（可为正数或负数）
     * @param type 竞技场类型（2v2、3v3、5v5）
     */
    void ModifyPersonalRating(Player* player, int32 mod, uint32 type);

    /**
     * @brief 修改匹配等级
     *
     * 调整成员的匹配等级（Matchmaker Rating），影响匹配系统的对手选择。
     *
     * @param mod 等级修改值（可为正数或负数）
     * @param slot 竞技场槽位（0=2v2, 1=3v3, 2=5v5）
     */
    void ModifyMatchmakerRating(int32 mod, uint32 slot);
};

/**
 * @struct ArenaTeamStats
 * @brief 竞技场战队统计数据结构
 *
 * 存储竞技场战队的整体统计数据，包括战队等级、
 * 胜负场次和排名等信息。这些数据用于展示战队的整体实力和表现。
 */
struct ArenaTeamStats
{
    uint16 Rating;        ///< 战队当前等级
    uint16 WeekGames;     ///< 本周总参赛场次
    uint16 WeekWins;      ///< 本周总获胜场次
    uint16 SeasonGames;   ///< 本赛季总参赛场次
    uint16 SeasonWins;    ///< 本赛季总获胜场次
    uint32 Rank;          ///< 战队排名
};

#define MAX_ARENA_SLOT 3                                    // 0..2 slots

/**
 * @class ArenaTeam
 * @brief 竞技场战队管理类
 *
 * 管理竞技场战队的核心类，负责战队的创建、成员管理、
 * 等级计算、战绩统计、数据库持久化等所有功能。
 * 每个竞技场战队对应一个ArenaTeam对象实例。
 *
 * 竞技场战队系统支持三种规模：2v2、3v3和5v5。
 * 战队拥有独特的等级系统，包括战队等级和个人等级。
 * 战队还支持自定义的徽章、边框和颜色方案。
 */
class TC_GAME_API ArenaTeam
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化竞技场战队对象的所有成员变量为默认值。
         */
        ArenaTeam();

        /**
         * @brief 析构函数
         *
         * 清理竞技场战队对象占用的资源。
         */
        ~ArenaTeam();

        /**
         * @brief 创建新的竞技场战队
         *
         * 初始化一个新的竞技场战队，设置队长、类型、名称和视觉样式。
         * 成功创建后会自动将队长添加为第一个成员。
         *
         * @param captainGuid 战队队长的GUID
         * @param type 战队类型（2=2v2, 3=3v3, 5=5v5）
         * @param teamName 战队名称
         * @param backgroundColor 背景颜色（ARGB格式）
         * @param emblemStyle 徽章样式ID
         * @param emblemColor 徽章颜色（ARGB格式）
         * @param borderStyle 边框样式ID
         * @param borderColor 边框颜色（ARGB格式）
         * @return true 创建成功
         * @return false 创建失败（名称已存在或参数无效）
         */
        bool Create(ObjectGuid captainGuid, uint8 type, std::string const& teamName, uint32 backgroundColor, uint8 emblemStyle, uint32 emblemColor, uint8 borderStyle, uint32 borderColor);

        /**
         * @brief 解散竞技场战队（带会话）
         *
         * 解散当前的竞技场战队，移除所有成员并从数据库中删除战队记录。
         * 会向所有在线成员发送解散通知。
         *
         * @param session 发起解散操作的会话（用于权限验证）
         */
        void Disband(WorldSession* session);

        /**
         * @brief 解散竞技场战队（无会话）
         *
         * 解散当前的竞技场战队，直接执行解散逻辑。
         * 通常用于服务器内部操作或清理。
         */
        void Disband();

        typedef std::list<ArenaTeamMember> MemberList;

        /**
         * @brief 获取战队ID
         * @return 战队的唯一标识ID
         */
        uint32 GetId() const { return TeamId; }

        /**
         * @brief 获取战队类型
         * @return 战队类型（2=2v2, 3=3v3, 5=5v5）
         */
        uint32 GetType() const { return Type; }

        /**
         * @brief 获取战队槽位
         * @return 战队槽位索引（0..2）
         */
        uint8 GetSlot() const { return GetSlotByType(GetType()); }

        /**
         * @brief 根据类型获取槽位索引
         *
         * 将战队类型转换为槽位索引。
         * 2v2 -> 槽位0，3v3 -> 槽位1，5v5 -> 槽位2
         *
         * @param type 战队类型
         * @return 对应的槽位索引
         */
        static uint8 GetSlotByType(uint32 type);

        /**
         * @brief 获取队长GUID
         * @return 战队队长的GUID
         */
        ObjectGuid GetCaptain() const  { return CaptainGuid; }

        /**
         * @brief 获取战队名称
         * @return 战队名称的常量引用
         */
        std::string const& GetName() const { return TeamName; }

        /**
         * @brief 获取战队统计数据
         * @return 战队统计数据的常量引用
         */
        ArenaTeamStats const& GetStats() const { return Stats; }

        /**
         * @brief 获取战队等级
         * @return 当前战队等级
         */
        uint32 GetRating() const          { return Stats.Rating; }

        /**
         * @brief 计算队伍的平均匹配等级
         *
         * 计算指定队伍中所有成员的平均匹配等级（MMR）。
         * 用于匹配系统评估队伍实力。
         *
         * @param group 队伍对象指针
         * @return 平均匹配等级
         */
        uint32 GetAverageMMR(Group* group) const;

        /**
         * @brief 设置战队队长
         *
         * 将战队队长权限转移给指定玩家。
         * 新队长必须是战队的现有成员。
         *
         * @param guid 新队长的GUID
         */
        void SetCaptain(ObjectGuid guid);

        /**
         * @brief 设置战队名称
         *
         * 修改战队的显示名称。
         * 会进行名称有效性检查和唯一性验证。
         *
         * @param name 新的战队名称
         * @return true 设置成功
         * @return false 设置失败（名称无效或已存在）
         */
        bool SetName(std::string const& name);

        /**
         * @brief 添加新成员
         *
         * 将指定玩家添加到战队成员列表中。
         * 会检查战队成员数量上限和玩家是否已在其他战队。
         *
         * @param PlayerGuid 要添加的玩家GUID
         * @return true 添加成功
         * @return false 添加失败（战队已满或玩家已在战队中）
         */
        bool AddMember(ObjectGuid PlayerGuid);

        /**
         * @brief 删除成员
         *
         * 从战队中移除指定玩家。
         * 可以选择是否同时从数据库中删除成员记录。
         *
         * @param guid 要移除的玩家GUID
         * @param cleanDb 是否从数据库中删除记录
         */
        void DelMember(ObjectGuid guid, bool cleanDb);

        /**
         * @brief 获取成员数量
         * @return 当前战队成员数量
         */
        size_t GetMembersSize() const { return Members.size(); }

        /**
         * @brief 检查战队是否为空
         * @return true 战队没有成员
         * @return false 战队至少有一个成员
         */
        bool Empty() const { return Members.empty(); }

        /**
         * @brief 获取成员列表起始迭代器
         * @return 成员列表的起始迭代器
         */
        MemberList::iterator m_membersBegin() { return Members.begin(); }

        /**
         * @brief 获取成员列表结束迭代器
         * @return 成员列表的结束迭代器
         */
        MemberList::iterator m_membersEnd() { return Members.end(); }

        /**
         * @brief 检查玩家是否为战队成员
         *
         * @param guid 要检查的玩家GUID
         * @return true 玩家是战队成员
         * @return false 玩家不是战队成员
         */
        bool IsMember(ObjectGuid guid) const;

        /**
         * @brief 根据GUID获取成员信息
         *
         * 查找并返回指定GUID对应的成员数据结构。
         *
         * @param guid 玩家GUID
         * @return ArenaTeamMember* 成员指针，如果找不到则返回nullptr
         */
        ArenaTeamMember* GetMember(ObjectGuid guid);

        /**
         * @brief 根据名称获取成员信息
         *
         * 查找并返回指定名称对应的成员数据结构。
         *
         * @param name 玩家角色名称
         * @return ArenaTeamMember* 成员指针，如果找不到则返回nullptr
         */
        ArenaTeamMember* GetMember(std::string const& name);

        /**
         * @brief 检查战队是否正在战斗中
         *
         * 检查战队中是否有成员正在进行竞技场比赛。
         *
         * @return true 战队正在战斗中
         * @return false 战队没有成员在战斗
         */
        bool IsFighting() const;

        /**
         * @brief 从数据库加载战队数据
         *
         * 从数据库查询结果中加载战队的基本信息。
         *
         * @param arenaTeamDataResult 数据库查询结果
         * @return true 加载成功
         * @return false 加载失败
         */
        bool LoadArenaTeamFromDB(QueryResult arenaTeamDataResult);

        /**
         * @brief 从数据库加载成员列表
         *
         * 从数据库查询结果中加载战队的所有成员信息。
         *
         * @param arenaTeamMembersResult 数据库查询结果
         * @return true 加载成功
         * @return false 加载失败
         */
        bool LoadMembersFromDB(QueryResult arenaTeamMembersResult);

        /**
         * @brief 从数据库加载战队统计
         *
         * 加载指定战队的统计数据（等级、排名等）。
         *
         * @param ArenaTeamId 战队ID
         */
        void LoadStatsFromDB(uint32 ArenaTeamId);

        /**
         * @brief 保存战队数据到数据库
         *
         * 将战队的当前状态（包括成员信息）保存到数据库。
         *
         * @param forceMemberSave 是否强制保存成员数据（即使没有变化）
         */
        void SaveToDB(bool forceMemberSave = false);

        /**
         * @brief 广播数据包
         *
         * 向所有在线的战队成员发送指定的数据包。
         *
         * @param packet 要发送的数据包指针
         */
        void BroadcastPacket(WorldPacket* packet);

        /**
         * @brief 广播战队事件
         *
         * 向战队成员广播通知事件，如成员加入、离开、队长变更等。
         *
         * @param event 事件类型
         * @param guid 相关玩家的GUID
         * @param strCount 字符串参数数量
         * @param str1 第一个字符串参数
         * @param str2 第二个字符串参数
         * @param str3 第三个字符串参数
         */
        void BroadcastEvent(ArenaTeamEvents event, ObjectGuid guid, uint8 strCount, std::string const& str1, std::string const& str2, std::string const& str3);

        /**
         * @brief 通知统计数据变化
         *
         * 通知所有在线成员战队的统计数据已更新。
         */
        void NotifyStatsChanged();

        /**
         * @brief 批量邀请成员参加活动
         *
         * 向所有在线的战队成员发送活动邀请。
         *
         * @param session 发起邀请的会话
         */
        void MassInviteToEvent(WorldSession* session);

        /**
         * @brief 发送战队花名册
         *
         * 向指定玩家发送战队的成员列表信息。
         *
         * @param session 目标会话
         */
        void Roster(WorldSession* session);

        /**
         * @brief 响应战队查询请求
         *
         * 处理客户端对战队的查询请求，返回战队基本信息。
         *
         * @param session 请求会话
         */
        void Query(WorldSession* session);

        /**
         * @brief 发送战队统计数据
         *
         * 向指定玩家发送战队的详细统计数据。
         *
         * @param session 目标会话
         */
        void SendStats(WorldSession* session);

        /**
         * @brief 查看玩家竞技场信息
         *
         * 提供查看指定玩家的竞技场战队信息功能。
         *
         * @param session 查看者的会话
         * @param guid 被查看玩家的GUID
         */
        void Inspect(WorldSession* session, ObjectGuid guid);

        /**
         * @brief 计算竞技场点数
         *
         * 根据成员的个人等级计算每周可获得的竞技场点数。
         * 等级越高，获得的点数越多。
         *
         * @param MemberRating 成员的个人等级
         * @return 应获得的竞技场点数
         */
        uint32 GetPoints(uint32 MemberRating);

        /**
         * @brief 计算匹配等级修正值
         *
         * 根据双方匹配等级和比赛结果，计算匹配等级的变化量。
         * 使用Elo等级系统变种进行计算。
         *
         * @param ownRating 己方匹配等级
         * @param opponentRating 对方匹配等级
         * @param won 是否获胜
         * @return 匹配等级变化量
         */
        int32 GetMatchmakerRatingMod(uint32 ownRating, uint32 opponentRating, bool won);

        /**
         * @brief 计算战队等级修正值
         *
         * 根据双方战队等级和比赛结果，计算战队等级的变化量。
         * 使用Elo等级系统变种进行计算。
         *
         * @param ownRating 己方战队等级
         * @param opponentRating 对方战队等级
         * @param won 是否获胜
         * @return 战队等级变化量
         */
        int32 GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won);

        /**
         * @brief 计算获胜概率
         *
         * 根据双方等级计算己方的理论获胜概率。
         * 用于等级系统的计算基础。
         *
         * @param ownRating 己方等级
         * @param opponentRating 对方等级
         * @return 获胜概率（0.0-1.0）
         */
        float GetChanceAgainst(uint32 ownRating, uint32 opponentRating);

        /**
         * @brief 处理比赛获胜
         *
         * 当战队在比赛中获胜时调用，计算并更新等级变化。
         *
         * @param Own_MMRating 己方匹配等级
         * @param Opponent_MMRating 对方匹配等级
         * @param rating_change [out] 战队等级变化量
         * @return 匹配等级变化量
         */
        int32 WonAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change);

        /**
         * @brief 记录成员获胜
         *
         * 更新获胜成员的战绩统计和等级。
         *
         * @param player 获胜的玩家对象
         * @param againstMatchmakerRating 对方的匹配等级
         * @param MatchmakerRatingChange 匹配等级变化量
         */
        void MemberWon(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange);

        /**
         * @brief 处理比赛失败
         *
         * 当战队在比赛中失败时调用，计算并更新等级变化。
         *
         * @param Own_MMRating 己方匹配等级
         * @param Opponent_MMRating 对方匹配等级
         * @param rating_change [out] 战队等级变化量
         * @return 匹配等级变化量
         */
        int32 LostAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change);

        /**
         * @brief 记录成员失败
         *
         * 更新失败成员的战绩统计和等级。
         *
         * @param player 失败的玩家对象
         * @param againstMatchmakerRating 对方的匹配等级
         * @param MatchmakerRatingChange 匹配等级变化量（默认-12）
         */
        void MemberLost(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange = -12);

        /**
         * @brief 记录离线成员失败
         *
         * 更新离线成员的战绩统计和等级。
         * 用于处理成员在比赛过程中掉线的情况。
         *
         * @param guid 离线成员的GUID
         * @param againstMatchmakerRating 对方的匹配等级
         * @param MatchmakerRatingChange 匹配等级变化量（默认-12）
         */
        void OfflineMemberLost(ObjectGuid guid, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange = -12);

        /**
         * @brief 更新竞技场点数辅助函数
         *
         * 辅助计算并更新所有成员的竞技场点数。
         * 在每周结算时调用。
         *
         * @param PlayerPoints [out] 玩家竞技场点数映射表
         */
        void UpdateArenaPointsHelper(std::map<uint32, uint32> & PlayerPoints);

        /**
         * @brief 结算本周数据
         *
         * 执行每周结算，重置周统计数据，计算排名。
         *
         * @return true 战队本周有比赛记录
         * @return false 战队本周没有比赛
         */
        bool FinishWeek();

        /**
         * @brief 结束比赛
         *
         * 在比赛结束后更新战队的统计数据。
         *
         * @param mod 战队等级变化量
         */
        void FinishGame(int32 mod);

        /**
         * @brief 设置上一个对手战队ID
         *
         * 记录最近一次比赛的对手战队，用于防止连续匹配同一对手。
         *
         * @param arenaTeamId 对手战队的ID
         */
        void SetPreviousOpponents(uint32 arenaTeamId) { PreviousOpponents = arenaTeamId; }

        /**
         * @brief 获取上一个对手战队ID
         *
         * 返回最近一次比赛的对手战队ID。
         *
         * @return 对手战队ID
         */
        uint32 GetPreviousOpponents() { return PreviousOpponents; }

    protected:
        uint32 TeamId;                  ///< 战队唯一标识ID
        uint8 Type;                     ///< 战队类型（2=2v2, 3=3v3, 5=5v5）
        std::string TeamName;           ///< 战队名称
        ObjectGuid CaptainGuid;         ///< 战队队长的GUID

        uint32 BackgroundColor;         ///< 背景颜色（ARGB格式）
        uint8 EmblemStyle;              ///< 徽章样式ID
        uint32 EmblemColor;             ///< 徽章颜色（ARGB格式）
        uint8 BorderStyle;              ///< 边框样式ID
        uint32 BorderColor;             ///< 边框颜色（ARGB格式）

        MemberList Members;             ///< 战队成员列表
        ArenaTeamStats Stats;           ///< 战队统计数据

        uint32 PreviousOpponents;       ///< 上一个对手的战队ID
};
#endif
