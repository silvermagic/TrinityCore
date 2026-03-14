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
 * @file BattlegroundQueue.h
 * @brief 战场队列模块头文件
 *
 * 本文件定义了战场队列系统的核心类和数据结构。
 * 主要职责包括：
 * - 管理玩家和队伍的排队信息
 * - 匹配合适的队伍进行对战
 * - 处理队列中的邀请和超时
 * - 计算平均等待时间
 * - 管理选择池以平衡阵营
 */

#ifndef __BATTLEGROUNDQUEUE_H
#define __BATTLEGROUNDQUEUE_H

#include "Common.h"
#include "DBCEnums.h"
#include "Battleground.h"
#include "EventProcessor.h"

#include <deque>

//this container can't be deque, because deque doesn't like removing the last element - if you remove it, it invalidates next iterator and crash appears
typedef std::list<Battleground*> BGFreeSlotQueueContainer;  // 空闲槽位队列容器（不能使用deque，因为deque删除最后一个元素会使迭代器失效）

#define COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME 10            // 计算平均等待时间的玩家数量

struct GroupQueueInfo;                                      // 类型前置声明

/**
 * @struct PlayerQueueInfo
 * @brief 玩家队列信息结构体
 *
 * 存储单个玩家在队列中的信息
 */
struct PlayerQueueInfo
{
    uint32 LastOnlineTime;                                  // 最后在线时间，用于跟踪和移除离线玩家（5分钟后）
    GroupQueueInfo* GroupInfo;                              // 关联的队伍队列信息指针
};

/**
 * @struct GroupQueueInfo
 * @brief 队伍队列信息结构体
 *
 * 存储队伍在队列中的信息（单独排队的玩家也使用此结构）
 */
struct GroupQueueInfo
{
    std::map<ObjectGuid, PlayerQueueInfo*> Players;         // 玩家队列信息映射
    uint32  Team;                                           // 玩家阵营（ALLIANCE/HORDE）
    BattlegroundTypeId BgTypeId;                            // 战场类型ID
    bool    IsRated;                                        // 是否为评级比赛
    uint8   ArenaType;                                      // 竞技场类型（2v2, 3v3, 5v5或0表示战场）
    uint32  ArenaTeamId;                                    // 竞技场队伍ID（评级比赛）
    uint32  JoinTime;                                       // 队伍加入队列的时间
    uint32  RemoveInviteTime;                               // 移除邀请的时间
    uint32  IsInvitedToBGInstanceGUID;                      // 被邀请到的战场实例GUID
    uint32  ArenaTeamRating;                                // 竞技场队伍评级
    uint32  ArenaMatchmakerRating;                          // 竞技场匹配等级
    uint32  OpponentsTeamRating;                            // 对手队伍评级（评级竞技场）
    uint32  OpponentsMatchmakerRating;                      // 对手匹配等级（评级竞技场）
    uint32  PreviousOpponentsTeamId;                        // 上一个对手队伍ID（在一定时间内排除）
};

/**
 * @brief 战场队列队伍类型枚举
 *
 * 定义队伍在队列中的分类，用于匹配逻辑
 */
enum BattlegroundQueueGroupTypes
{
    BG_QUEUE_PREMADE_ALLIANCE   = 0,                        // 联盟预组队伍（或联盟评级竞技场队伍）
    BG_QUEUE_PREMADE_HORDE      = 1,                        // 部落预组队伍（或部落评级竞技场队伍）
    BG_QUEUE_NORMAL_ALLIANCE    = 2,                        // 联盟普通队伍（或非评级竞技场）
    BG_QUEUE_NORMAL_HORDE       = 3                         // 部落普通队伍（或非评级竞技场）
};
#define BG_QUEUE_GROUP_TYPES_COUNT 4

/**
 * @brief 战场队列邀请类型枚举
 *
 * 定义队伍邀请时的平衡策略
 */
enum BattlegroundQueueInvitationType
{
    BG_QUEUE_INVITATION_TYPE_NO_BALANCE = 0,                // 不平衡：N+M vs N 玩家
    BG_QUEUE_INVITATION_TYPE_BALANCED   = 1,                // 平衡：N+1 vs N 玩家
    BG_QUEUE_INVITATION_TYPE_EVEN       = 2                 // 均等：N vs N 玩家
};

class Battleground;

/**
 * @class BattlegroundQueue
 * @brief 战场队列类
 *
 * 管理特定队列类型的所有排队玩家和队伍，负责匹配和对战安排。
 * 主要职责包括：
 * 1. 管理玩家和队伍的排队信息
 * 2. 匹配合适的队伍进行对战
 * 3. 处理队列更新和邀请逻辑
 * 4. 计算平均等待时间
 * 5. 平衡双方阵营的玩家数量
 */
class TC_GAME_API BattlegroundQueue
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化队列的等待时间统计数组
         */
        BattlegroundQueue();

        /**
         * @brief 析构函数
         */
        ~BattlegroundQueue();

        /**
         * @brief 更新战场队列
         * @param diff 时间间隔（毫秒）
         * @param bgTypeId 战场类型ID
         * @param bracket_id 战场分段ID
         * @param arenaType 竞技场类型（默认0）
         * @param isRated 是否为评级比赛（默认false）
         * @param minRating 最小评级（默认0）
         *
         * 主更新函数，检查队列并尝试匹配队伍
         */
        void BattlegroundQueueUpdate(uint32 diff, BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id, uint8 arenaType = 0, bool isRated = false, uint32 minRating = 0);

        /**
         * @brief 更新事件
         * @param diff 时间间隔（毫秒）
         *
         * 处理队列中的定时事件（如邀请超时等）
         */
        void UpdateEvents(uint32 diff);

        /**
         * @brief 填充玩家到战场
         * @param bg 战场指针
         * @param bracket_id 战场分段ID
         *
         * 将队列中的玩家填充到有空位的战场
         */
        void FillPlayersToBG(Battleground* bg, BattlegroundBracketId bracket_id);

        /**
         * @brief 检查预组队伍匹配
         * @param bracket_id 战场分段ID
         * @param MinPlayersPerTeam 每队最小玩家数
         * @param MaxPlayersPerTeam 每队最大玩家数
         * @return 找到匹配返回true
         *
         * 检查是否有足够的预组队伍可以进行对战
         */
        bool CheckPremadeMatch(BattlegroundBracketId bracket_id, uint32 MinPlayersPerTeam, uint32 MaxPlayersPerTeam);

        /**
         * @brief 检查普通队伍匹配
         * @param bg_template 战场模板指针
         * @param bracket_id 战场分段ID
         * @param minPlayers 最小玩家数
         * @param maxPlayers 最大玩家数
         * @return 找到匹配返回true
         *
         * 检查是否有足够的玩家可以组成对战
         */
        bool CheckNormalMatch(Battleground* bg_template, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers);

        /**
         * @brief 检查同阵营的小规模战斗
         * @param bracket_id 战场分段ID
         * @param minPlayersPerTeam 每队最小玩家数
         * @return 找到匹配返回true
         *
         * 用于竞技场，检查是否可以安排同阵营对战（联盟vs联盟或部落vs部落）
         */
        bool CheckSkirmishForSameFaction(BattlegroundBracketId bracket_id, uint32 minPlayersPerTeam);

        /**
         * @brief 添加队伍到队列
         * @param leader 队长指针
         * @param group 队伍指针（单人时为nullptr）
         * @param bgTypeId 战场类型ID
         * @param bracketEntry PvP难度条目
         * @param ArenaType 竞技场类型
         * @param isRated 是否为评级比赛
         * @param isPremade 是否为预组队伍
         * @param ArenaRating 竞技场评级
         * @param MatchmakerRating 匹配等级
         * @param ArenaTeamId 竞技场队伍ID（默认0）
         * @param OpponentsArenaTeamId 对手竞技场队伍ID（默认0）
         * @return 队伍队列信息指针
         */
        GroupQueueInfo* AddGroup(Player* leader, Group* group, BattlegroundTypeId bgTypeId, PvPDifficultyEntry const* bracketEntry, uint8 ArenaType, bool isRated, bool isPremade, uint32 ArenaRating, uint32 MatchmakerRating, uint32 ArenaTeamId = 0, uint32 OpponentsArenaTeamId = 0);

        /**
         * @brief 从队列移除玩家
         * @param guid 玩家GUID
         * @param decreaseInvitedCount 是否减少已邀请计数
         */
        void RemovePlayer(ObjectGuid guid, bool decreaseInvitedCount);

        /**
         * @brief 检查玩家是否被邀请
         * @param pl_guid 玩家GUID
         * @param bgInstanceGuid 战场实例GUID
         * @param removeTime 移除时间
         * @return 已被邀请返回true
         */
        bool IsPlayerInvited(ObjectGuid pl_guid, const uint32 bgInstanceGuid, const uint32 removeTime);

        /**
         * @brief 获取玩家队伍信息数据
         * @param guid 玩家GUID
         * @param ginfo 队伍队列信息指针（输出参数）
         * @return 成功获取返回true
         */
        bool GetPlayerGroupInfoData(ObjectGuid guid, GroupQueueInfo* ginfo);

        /**
         * @brief 更新玩家被邀请到战场后的平均等待时间
         * @param ginfo 队伍队列信息指针
         * @param bracket_id 战场分段ID
         */
        void PlayerInvitedToBGUpdateAverageWaitTime(GroupQueueInfo* ginfo, BattlegroundBracketId bracket_id);

        /**
         * @brief 获取平均队列等待时间
         * @param ginfo 队伍队列信息指针
         * @param bracket_id 战场分段ID
         * @return 平均等待时间（毫秒）
         */
        uint32 GetAverageQueueWaitTime(GroupQueueInfo* ginfo, BattlegroundBracketId bracket_id) const;

        typedef std::map<ObjectGuid, PlayerQueueInfo> QueuedPlayersMap;
        QueuedPlayersMap m_QueuedPlayers;                               // 已排队玩家映射表

        //do NOT use deque because deque.erase() invalidates ALL iterators
        typedef std::list<GroupQueueInfo*> GroupsQueueType;

        /*
        This two dimensional array is used to store All queued groups
        First dimension specifies the bgTypeId
        Second dimension specifies the player's group types -
             BG_QUEUE_PREMADE_ALLIANCE  is used for premade alliance groups and alliance rated arena teams
             BG_QUEUE_PREMADE_HORDE     is used for premade horde groups and horde rated arena teams
             BG_QUEUE_NORMAL_ALLIANCE   is used for normal (or small) alliance groups or non-rated arena matches
             BG_QUEUE_NORMAL_HORDE      is used for normal (or small) horde groups or non-rated arena matches
        */
        GroupsQueueType m_QueuedGroups[MAX_BATTLEGROUND_BRACKETS][BG_QUEUE_GROUP_TYPES_COUNT]; // 二维队列数组：[分段][队伍类型]

        /**
         * @class SelectionPool
         * @brief 选择池类
         *
         * 用于选择和邀请队伍到战场的临时容器
         */
        class SelectionPool
        {
        public:
            SelectionPool(): PlayerCount(0) { }                         // 构造函数

            /**
             * @brief 初始化选择池
             */
            void Init();

            /**
             * @brief 添加队伍到选择池
             * @param ginfo 队伍队列信息指针
             * @param desiredCount 期望的玩家数量
             * @return 成功添加返回true
             */
            bool AddGroup(GroupQueueInfo* ginfo, uint32 desiredCount);

            /**
             * @brief 从选择池移除队伍
             * @param size 要移除的队伍大小
             * @return 成功移除返回true
             */
            bool KickGroup(uint32 size);

            uint32 GetPlayerCount() const {return PlayerCount;}         // 获取选择池中的玩家总数
        public:
            GroupsQueueType SelectedGroups;                             // 已选择的队伍列表
        private:
            uint32 PlayerCount;                                         // 玩家总数
        };

        //one selection pool for horde, other one for alliance
        SelectionPool m_SelectionPools[PVP_TEAMS_COUNT];                // 选择池数组（0-部落，1-联盟）

        /**
         * @brief 获取队列中的玩家数量
         * @param id 阵营ID
         * @return 玩家数量
         */
        uint32 GetPlayersInQueue(TeamId id);
    private:

        /**
         * @brief 邀请队伍到战场
         * @param ginfo 队伍队列信息指针
         * @param bg 战场指针
         * @param side 阵营
         * @return 成功邀请返回true
         */
        bool InviteGroupToBG(GroupQueueInfo* ginfo, Battleground* bg, uint32 side);

        uint32 m_WaitTimes[PVP_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS][COUNT_OF_PLAYERS_TO_AVERAGE_WAIT_TIME]; // 等待时间历史记录
        uint32 m_WaitTimeLastPlayer[PVP_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS];                                // 最后玩家的等待时间
        uint32 m_SumOfWaitTimes[PVP_TEAMS_COUNT][MAX_BATTLEGROUND_BRACKETS];                                    // 等待时间总和

        // Event handler
        EventProcessor m_events;                                        // 事件处理器
};

/**
 * @class BGQueueInviteEvent
 * @brief 战场队列邀请事件类
 *
 * 当玩家第一次被邀请后一分钟，再次提醒玩家加入战场。
 * 能够处理各种可能的情况（玩家在线、离线、已接受等）
 */
class TC_GAME_API BGQueueInviteEvent : public BasicEvent
{
    public:
        /**
         * @brief 构造函数
         * @param pl_guid 玩家GUID
         * @param BgInstanceGUID 战场实例GUID
         * @param BgTypeId 战场类型ID
         * @param arenaType 竞技场类型
         * @param removeTime 移除时间
         */
        BGQueueInviteEvent(ObjectGuid pl_guid, uint32 BgInstanceGUID, BattlegroundTypeId BgTypeId, uint8 arenaType, uint32 removeTime) :
          m_PlayerGuid(pl_guid), m_BgInstanceGUID(BgInstanceGUID), m_BgTypeId(BgTypeId), m_ArenaType(arenaType), m_RemoveTime(removeTime)
          { }

        virtual ~BGQueueInviteEvent() { }

        /**
         * @brief 执行事件
         * @param e_time 事件时间
         * @param p_time 处理时间
         * @return 执行成功返回true
         */
        virtual bool Execute(uint64 e_time, uint32 p_time) override;

        /**
         * @brief 中止事件
         * @param e_time 事件时间
         */
        virtual void Abort(uint64 e_time) override;
    private:
        ObjectGuid m_PlayerGuid;                                        // 玩家GUID
        uint32 m_BgInstanceGUID;                                        // 战场实例GUID
        BattlegroundTypeId m_BgTypeId;                                  // 战场类型ID
        uint8  m_ArenaType;                                             // 竞技场类型
        uint32 m_RemoveTime;                                            // 移除时间
};

/**
 * @class BGQueueRemoveEvent
 * @brief 战场队列移除事件类
 *
 * 在玩家第一次被邀请后1分20秒，从队列中移除玩家。
 * 必须存储removeInvite时间，因为玩家可能离开队列并重新加入。
 * 必须存储bgQueueTypeId，因为当玩家进入战场时，战场可能已被删除。
 */
class TC_GAME_API BGQueueRemoveEvent : public BasicEvent
{
    public:
        /**
         * @brief 构造函数
         * @param pl_guid 玩家GUID
         * @param bgInstanceGUID 战场实例GUID
         * @param BgTypeId 战场类型ID
         * @param bgQueueTypeId 战场队列类型ID
         * @param removeTime 移除时间
         */
        BGQueueRemoveEvent(ObjectGuid pl_guid, uint32 bgInstanceGUID, BattlegroundTypeId BgTypeId, BattlegroundQueueTypeId bgQueueTypeId, uint32 removeTime)
            : m_PlayerGuid(pl_guid), m_BgInstanceGUID(bgInstanceGUID), m_RemoveTime(removeTime), m_BgTypeId(BgTypeId), m_BgQueueTypeId(bgQueueTypeId)
        { }

        virtual ~BGQueueRemoveEvent() { }

        /**
         * @brief 执行事件
         * @param e_time 事件时间
         * @param p_time 处理时间
         * @return 执行成功返回true
         */
        virtual bool Execute(uint64 e_time, uint32 p_time) override;

        /**
         * @brief 中止事件
         * @param e_time 事件时间
         */
        virtual void Abort(uint64 e_time) override;
    private:
        ObjectGuid m_PlayerGuid;                                        // 玩家GUID
        uint32 m_BgInstanceGUID;                                        // 战场实例GUID
        uint32 m_RemoveTime;                                            // 移除时间
        BattlegroundTypeId m_BgTypeId;                                  // 战场类型ID
        BattlegroundQueueTypeId m_BgQueueTypeId;                        // 战场队列类型ID
};

#endif
