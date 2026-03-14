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
 * @file Battleground.h
 * @brief 战场核心模块头文件
 *
 * 本文件定义了战场系统的核心类和相关数据结构，提供战场实例的基本框架。
 * 主要职责包括：
 * - 定义战场基类Battleground，提供所有战场类型的通用功能
 * - 管理战场中的玩家信息、分数统计、复活队列等
 * - 提供战场生命周期管理（初始化、启动、运行、结束）
 * - 定义战场状态、时间间隔、Buff对象等枚举常量
 * - 支持普通战场和竞技场的统一接口
 */

#ifndef __BATTLEGROUND_H
#define __BATTLEGROUND_H

#include "ArenaScore.h"
#include "DBCEnums.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "SharedDefines.h"
#include "UniqueTrackablePtr.h"
#include <deque>
#include <map>

namespace WorldPackets
{
    namespace WorldState
    {
        class InitWorldStates;
    }
}

class BattlegroundMap;
class Creature;
class GameObject;
class Group;
class Player;
class Transport;
class Unit;
class WorldObject;
class WorldPacket;

struct BattlegroundScore;
struct PvPDifficultyEntry;
struct WorldSafeLocsEntry;

/**
 * @brief 战场逃离类型枚举
 *
 * 定义玩家离开战场的不同方式，用于记录和惩罚逃跑行为
 */
enum BattlegroundDesertionType
{
    BG_DESERTION_TYPE_LEAVE_BG        = 0, // 玩家主动离开战场
    BG_DESERTION_TYPE_OFFLINE         = 1, // 玩家掉线被踢出战场
    BG_DESERTION_TYPE_LEAVE_QUEUE     = 2, // 玩家被邀请加入但拒绝
    BG_DESERTION_TYPE_NO_ENTER_BUTTON = 3, // 玩家被邀请加入但无操作（超时）
    BG_DESERTION_TYPE_INVITE_LOGOUT   = 4, // 玩家被邀请加入但登出游戏
};

/**
 * @brief 战场成就条件ID枚举
 *
 * 定义战场中需要检查的成就条件类型
 */
enum BattlegroundCriteriaId
{
    BG_CRITERIA_CHECK_RESILIENT_VICTORY,      // 检查"韧性胜利"成就
    BG_CRITERIA_CHECK_SAVE_THE_DAY,           // 检查"力挽狂澜"成就
    BG_CRITERIA_CHECK_EVERYTHING_COUNTS,      // 检查"一切都重要"成就
    BG_CRITERIA_CHECK_AV_PERFECTION,          // 检查"奥特兰克山谷完美胜利"成就
    BG_CRITERIA_CHECK_DEFENSE_OF_THE_ANCIENTS,// 检查"远古防御者"成就
    BG_CRITERIA_CHECK_NOT_EVEN_A_SCRATCH,     // 检查"毫发无损"成就
};

/**
 * @brief 战场广播文本ID枚举
 *
 * 定义战场中使用的广播文本ID，用于向玩家发送消息
 */
enum BattlegroundBroadcastTexts
{
    BG_TEXT_ALLIANCE_WINS       = 10633,      // 联盟获胜
    BG_TEXT_HORDE_WINS          = 10634,      // 部落获胜

    BG_TEXT_START_TWO_MINUTES   = 18193,      // 战斗将在2分钟后开始
    BG_TEXT_START_ONE_MINUTE    = 18194,      // 战斗将在1分钟后开始
    BG_TEXT_START_HALF_MINUTE   = 18195,      // 战斗将在30秒后开始
    BG_TEXT_BATTLE_HAS_BEGUN    = 18196,      // 战斗已经开始
};

/**
 * @brief 战场音效ID枚举
 *
 * 定义战场中播放的各种音效ID
 */
enum BattlegroundSounds
{
    SOUND_HORDE_WINS                = 8454,   // 部落获胜音效
    SOUND_ALLIANCE_WINS             = 8455,   // 联盟获胜音效
    SOUND_BG_START                  = 3439,   // 战场开始音效
    SOUND_BG_START_L70ETC           = 11803   // 70级乐队音效
};

/**
 * @brief 战场任务奖励法术ID枚举
 *
 * 定义各战场完成任务后的奖励法术ID
 */
enum BattlegroundQuests
{
    SPELL_WS_QUEST_REWARD           = 43483,  // 战歌峡谷任务奖励
    SPELL_AB_QUEST_REWARD           = 43484,  // 阿拉希盆地任务奖励
    SPELL_AV_QUEST_REWARD           = 43475,  // 奥特兰克山谷任务奖励
    SPELL_AV_QUEST_KILLED_BOSS      = 23658,  // 奥特兰克山谷击杀Boss奖励
    SPELL_EY_QUEST_REWARD           = 43477,  // 风暴之眼任务奖励
    SPELL_SA_QUEST_REWARD           = 61213,  // 远海滩任务奖励
    SPELL_AB_QUEST_REWARD_4_BASES   = 24061,  // 阿拉希盆地占领4个基地奖励
    SPELL_AB_QUEST_REWARD_5_BASES   = 24064   // 阿拉希盆地占领5个基地奖励
};

/**
 * @brief 战场荣誉徽章法术和物品ID枚举
 *
 * 定义战场胜负奖励的徽章法术和物品ID
 */
enum BattlegroundMarks
{
    SPELL_WS_MARK_LOSER             = 24950,  // 战歌峡谷失败者徽章法术
    SPELL_WS_MARK_WINNER            = 24951,  // 战歌峡谷胜利者徽章法术
    SPELL_AB_MARK_LOSER             = 24952,  // 阿拉希盆地失败者徽章法术
    SPELL_AB_MARK_WINNER            = 24953,  // 阿拉希盆地胜利者徽章法术
    SPELL_AV_MARK_LOSER             = 24954,  // 奥特兰克山谷失败者徽章法术
    SPELL_AV_MARK_WINNER            = 24955,  // 奥特兰克山谷胜利者徽章法术
    SPELL_SA_MARK_WINNER            = 61160,  // 远海滩胜利者徽章法术
    SPELL_SA_MARK_LOSER             = 61159,  // 远海滩失败者徽章法术
    ITEM_AV_MARK_OF_HONOR           = 20560,  // 奥特兰克山谷荣誉徽章物品
    ITEM_WS_MARK_OF_HONOR           = 20558,  // 战歌峡谷荣誉徽章物品
    ITEM_AB_MARK_OF_HONOR           = 20559,  // 阿拉希盆地荣誉徽章物品
    ITEM_EY_MARK_OF_HONOR           = 29024,  // 风暴之眼荣誉徽章物品
    ITEM_SA_MARK_OF_HONOR           = 42425   // 远海滩荣誉徽章物品
};

/**
 * @brief 战场徽章数量枚举
 *
 * 定义胜负双方获得的徽章数量
 */
enum BattlegroundMarksCount
{
    ITEM_WINNER_COUNT               = 3,      // 胜利者获得的徽章数量
    ITEM_LOSER_COUNT                = 1       // 失败者获得的徽章数量
};

/**
 * @brief 战场生物ID枚举
 *
 * 定义战场中的特殊生物ID
 */
enum BattlegroundCreatures
{
    BG_CREATURE_ENTRY_A_SPIRITGUIDE      = 13116, // 联盟灵魂医者
    BG_CREATURE_ENTRY_H_SPIRITGUIDE      = 13117  // 部落灵魂医者
};

/**
 * @brief 战场法术ID枚举
 *
 * 定义战场中使用的各种法术ID
 */
enum BattlegroundSpells
{
    SPELL_WAITING_FOR_RESURRECT     = 2584,  // 等待复活
    SPELL_SPIRIT_HEAL_CHANNEL       = 22011, // 灵魂治疗通道
    SPELL_SPIRIT_HEAL               = 22012, // 灵魂治疗
    SPELL_RESURRECTION_VISUAL       = 24171, // 复活视觉效果
    SPELL_ARENA_PREPARATION         = 32727, // 竞技场准备（使用此ID，32728不正确）
    SPELL_PREPARATION               = 44521, // 准备
    SPELL_SPIRIT_HEAL_MANA          = 44535, // 灵魂治疗法力
    SPELL_RECENTLY_DROPPED_FLAG     = 42792, // 最近掉落的旗帜
    SPELL_AURA_PLAYER_INACTIVE      = 43681, // 不活跃状态
    SPELL_HONORABLE_DEFENDER_25Y    = 68652, // 荣誉防御者（25码半径，+50%荣誉）
    SPELL_HONORABLE_DEFENDER_60Y    = 66157  // 荣誉防御者（60码半径，+50%荣誉，用于40+玩家战场）
};

/**
 * @brief 战场时间间隔枚举
 *
 * 定义战场中各种时间间隔常量（毫秒或秒）
 */
enum BattlegroundTimeIntervals
{
    CHECK_PLAYER_POSITION_INVERVAL  = 1000,  // 检查玩家位置间隔（毫秒）
    RESURRECTION_INTERVAL           = 30000, // 复活间隔（毫秒）
    INVITATION_REMIND_TIME          = 20000, // 邀请提醒时间（毫秒）
    INVITE_ACCEPT_WAIT_TIME         = 60000, // 邀请接受等待时间（毫秒）
    TIME_TO_AUTOREMOVE              = 120000,// 自动移除时间（毫秒）
    MAX_OFFLINE_TIME                = 300,   // 最大离线时间（秒）
    RESPAWN_ONE_DAY                 = 86400, // 一天重生时间（秒）
    RESPAWN_IMMEDIATELY             = 0,     // 立即重生（秒）
    BUFF_RESPAWN_TIME               = 180    // Buff重生时间（秒）
};

/**
 * @brief 战场开始延迟时间枚举
 *
 * 定义战场开始前的倒计时阶段（毫秒）
 */
enum BattlegroundStartTimeIntervals
{
    BG_START_DELAY_2M               = 120000,// 2分钟延迟
    BG_START_DELAY_1M               = 60000, // 1分钟延迟
    BG_START_DELAY_30S              = 30000, // 30秒延迟
    BG_START_DELAY_15S              = 15000, // 15秒延迟（仅用于竞技场）
    BG_START_DELAY_NONE             = 0      // 无延迟
};

/**
 * @brief 战场Buff对象ID枚举
 *
 * 定义战场中刷新的Buff游戏对象ID
 */
enum BattlegroundBuffObjects
{
    BG_OBJECTID_SPEEDBUFF_ENTRY     = 179871,// 速度Buff
    BG_OBJECTID_REGENBUFF_ENTRY     = 179904,// 回复Buff
    BG_OBJECTID_BERSERKERBUFF_ENTRY = 179905 // 狂暴Buff
};

const uint32 Buff_Entries[3] = { BG_OBJECTID_SPEEDBUFF_ENTRY, BG_OBJECTID_REGENBUFF_ENTRY, BG_OBJECTID_BERSERKERBUFF_ENTRY };

/**
 * @brief 战场状态枚举
 *
 * 定义战场的各种运行状态
 */
enum BattlegroundStatus
{
    STATUS_NONE         = 0,                // 初始状态，表示战场不是实例
    STATUS_WAIT_QUEUE   = 1,                // 战场为空，等待队列
    STATUS_WAIT_JOIN    = 2,                // 战场已经开始，等待更多玩家加入
    STATUS_IN_PROGRESS  = 3,                // 战场正在进行
    STATUS_WAIT_LEAVE   = 4                 // 某一方获胜，战场即将结束
};

/**
 * @brief 战场玩家信息结构体
 *
 * 存储战场中玩家的基本信息
 */
struct BattlegroundPlayer
{
    time_t OfflineRemoveTime;               // 离线移除时间，用于跟踪和移除离线玩家（5分钟后）
    uint32 Team;                            // 玩家所属阵营（联盟/部落）
};

/**
 * @brief 战场对象信息结构体
 *
 * 存储战场中游戏对象的相关信息
 */
struct BattlegroundObjectInfo
{
    BattlegroundObjectInfo() : object(nullptr), timer(0), spellid(0) { }

    GameObject  *object;                    // 游戏对象指针
    int32       timer;                      // 计时器
    uint32      spellid;                    // 关联的法术ID
};

/**
 * @brief 竞技场类型枚举
 *
 * 定义竞技场的人数类型
 */
enum ArenaType
{
    ARENA_TYPE_2v2          = 2,            // 2v2竞技场
    ARENA_TYPE_3v3          = 3,            // 3v3竞技场
    ARENA_TYPE_5v5          = 5             // 5v5竞技场
};

/**
 * @brief 战场开始事件标志枚举
 *
 * 定义战场开始事件的标志位
 */
enum BattlegroundStartingEvents
{
    BG_STARTING_EVENT_NONE  = 0x00,         // 无事件
    BG_STARTING_EVENT_1     = 0x01,         // 开始事件1
    BG_STARTING_EVENT_2     = 0x02,         // 开始事件2
    BG_STARTING_EVENT_3     = 0x04,         // 开始事件3
    BG_STARTING_EVENT_4     = 0x08          // 开始事件4
};

/**
 * @brief 战场开始事件ID枚举
 *
 * 定义战场开始事件的索引ID
 */
enum BattlegroundStartingEventsIds
{
    BG_STARTING_EVENT_FIRST     = 0,        // 第一个开始事件
    BG_STARTING_EVENT_SECOND    = 1,        // 第二个开始事件
    BG_STARTING_EVENT_THIRD     = 2,        // 第三个开始事件
    BG_STARTING_EVENT_FOURTH    = 3         // 第四个开始事件
};
#define BG_STARTING_EVENT_COUNT 4

/**
 * @brief 战场荣誉模式枚举
 *
 * 定义战场荣誉的获取模式
 */
enum BGHonorMode
{
    BG_NORMAL = 0,                          // 普通模式
    BG_HOLIDAY,                             // 节日模式
    BG_HONOR_MODE_NUM                       // 荣誉模式数量
};

#define BG_AWARD_ARENA_POINTS_MIN_LEVEL 71  // 授予竞技场点的最低等级
#define ARENA_TIMELIMIT_POINTS_LOSS    -16  // 竞技场时间限制点数损失

/**
 * @class Battleground
 * @brief 战场基类
 *
 * 这是所有战场和竞技场实例的基类，提供了战场系统的核心功能。
 * 主要职责包括：
 * 1. 管理战场生命周期（创建、启动、运行、结束）
 * 2. 处理玩家加入和离开战场
 * 3. 维护玩家列表、分数统计、复活队列
 * 4. 处理战场事件（击杀、占领旗帜等）
 * 5. 管理战场对象（游戏对象、生物）
 * 6. 处理战场通信（消息、音效、世界状态）
 * 7. 计算和发放奖励（荣誉、经验、徽章等）
 *
 * 子类需要实现具体的战场逻辑（如SetupBattleground、ResetBGSubclass等）
 */
class TC_GAME_API Battleground
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化战场的基本属性和成员变量
         */
        Battleground();

        /**
         * @brief 析构函数
         *
         * 清理战场资源，释放关联的对象
         */
        virtual ~Battleground();

        /**
         * @brief 更新战场状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 主更新循环，每帧调用一次，处理战场的所有逻辑更新
         * 包括：复活队列、离线队列、开始倒计时、结束条件检查等
         */
        void Update(uint32 diff);

        /**
         * @brief 设置战场（纯虚函数）
         * @return 设置成功返回true，失败返回false
         *
         * 必须在战场子类中实现，用于初始化战场特定的对象和状态
         * 例如：创建旗帜、基地、Boss等战场对象
         */
        virtual bool SetupBattleground()
        {
            return true;
        }

        /**
         * @brief 重置战场
         *
         * 重置所有战场的通用属性，必须在战场子类中实现并调用
         * 用于战场实例重用或新一轮开始
         */
        virtual void Reset();

        /**
         * @brief 开始事件-关门
         *
         * 在战场开始前关闭大门，防止玩家提前进入战场区域
         */
        virtual void StartingEventCloseDoors() { }

        /**
         * @brief 开始事件-开门
         *
         * 在战场开始时打开大门，允许玩家进入战场区域
         */
        virtual void StartingEventOpenDoors() { }

        /**
         * @brief 重置战场子类（纯虚函数）
         *
         * 必须在战场子类中实现，用于重置特定战场的状态
         */
        virtual void ResetBGSubclass() { }

        /**
         * @brief 摧毁大门
         * @param player 摧毁大门的玩家
         * @param go 被摧毁的游戏对象
         *
         * 处理战场大门被摧毁的事件（如远海滩）
         */
        virtual void DestroyGate(Player* /*player*/, GameObject* /*go*/) { }

        /* achievement req. */
        /**
         * @brief 检查是否所有节点被某阵营控制
         * @param team 阵营ID
         * @return 如果所有节点都被该阵营控制返回true
         *
         * 用于成就检查（如"完美胜利"）
         */
        virtual bool IsAllNodesControlledByTeam(uint32 /*team*/) const { return false; }

        /**
         * @brief 启动计时成就
         * @param type 成就类型
         * @param entry 成就条目ID
         *
         * 启动需要计时完成的成就
         */
        void StartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry);

        /**
         * @brief 检查成就条件是否满足
         * @param criteriaId 成就条件ID
         * @param player 玩家指针
         * @param target 目标单位（可选）
         * @param miscvalue1 杂项值1（可选）
         * @return 条件满足返回true
         */
        virtual bool CheckAchievementCriteriaMeet(uint32 /*criteriaId*/, Player const* /*player*/, Unit const* /*target*/ = nullptr, uint32 /*miscvalue1*/ = 0);

        /* Battleground */
        // Get methods:
        std::string const& GetName() const  { return m_Name; }                       // 获取战场名称
        BattlegroundTypeId GetTypeID(bool GetRandom = false) const { return GetRandom ? m_RandomTypeID : m_TypeID; } // 获取战场类型ID
        BattlegroundBracketId GetBracketId() const { return m_BracketId; }           // 获取战场等级分段ID
        uint32 GetInstanceID() const        { return m_InstanceID; }                 // 获取战场实例ID
        BattlegroundStatus GetStatus() const { return m_Status; }                    // 获取战场状态
        uint32 GetClientInstanceID() const  { return m_ClientInstanceID; }           // 获取客户端实例ID
        uint32 GetStartTime() const         { return m_StartTime; }                 // 获取战场开始时间
        uint32 GetEndTime() const           { return m_EndTime; }                   // 获取战场结束时间
        uint32 GetLastResurrectTime() const { return m_LastResurrectTime; }         // 获取上次复活时间
        uint32 GetMaxPlayers() const        { return m_MaxPlayers; }                // 获取最大玩家数
        uint32 GetMinPlayers() const        { return m_MinPlayers; }                // 获取最小玩家数

        uint32 GetMinLevel() const          { return m_LevelMin; }                  // 获取最低等级
        uint32 GetMaxLevel() const          { return m_LevelMax; }                  // 获取最高等级

        uint32 GetMaxPlayersPerTeam() const { return m_MaxPlayersPerTeam; }         // 获取每队最大玩家数
        uint32 GetMinPlayersPerTeam() const { return m_MinPlayersPerTeam; }         // 获取每队最小玩家数

        int32 GetStartDelayTime() const     { return m_StartDelayTime; }            // 获取开始延迟时间
        uint8 GetArenaType() const          { return m_ArenaType; }                 // 获取竞技场类型（2v2/3v3/5v5）
        PvPTeamId GetWinner() const { return _winnerTeamId; }                       // 获取获胜方
        uint32 GetScriptId() const          { return ScriptId; }                    // 获取脚本ID

        /**
         * @brief 根据击杀数计算奖励荣誉值
         * @param kills 击杀数
         * @return 奖励的荣誉值
         */
        uint32 GetBonusHonorFromKill(uint32 kills) const;

        bool IsRandom() const { return m_IsRandom; }                                 // 是否为随机战场

        // Set methods:
        void SetName(std::string const& name) { m_Name = name; }                     // 设置战场名称
        void SetTypeID(BattlegroundTypeId TypeID) { m_TypeID = TypeID; }             // 设置战场类型ID
        void SetRandomTypeID(BattlegroundTypeId TypeID) { m_RandomTypeID = TypeID; } // 设置随机战场类型ID

        /**
         * @brief 设置战场等级分段
         * @param bracketEntry PvP难度条目
         *
         * 根据PvP难度条目设置战场的等级范围和分段ID
         */
        void SetBracket(PvPDifficultyEntry const* bracketEntry);

        void SetInstanceID(uint32 InstanceID) { m_InstanceID = InstanceID; }         // 设置实例ID
        void SetStatus(BattlegroundStatus Status) { m_Status = Status; }             // 设置战场状态
        void SetClientInstanceID(uint32 InstanceID) { m_ClientInstanceID = InstanceID; } // 设置客户端实例ID
        void SetStartTime(uint32 Time)      { m_StartTime = Time; }                  // 设置开始时间
        void SetEndTime(uint32 Time)        { m_EndTime = Time; }                    // 设置结束时间
        void SetLastResurrectTime(uint32 Time) { m_LastResurrectTime = Time; }       // 设置上次复活时间
        void SetMaxPlayers(uint32 MaxPlayers) { m_MaxPlayers = MaxPlayers; }         // 设置最大玩家数
        void SetMinPlayers(uint32 MinPlayers) { m_MinPlayers = MinPlayers; }         // 设置最小玩家数
        void SetLevelRange(uint32 min, uint32 max) { m_LevelMin = min; m_LevelMax = max; } // 设置等级范围
        void SetRated(bool state)           { m_IsRated = state; }                   // 设置是否为评级比赛
        void SetArenaType(uint8 type)       { m_ArenaType = type; }                  // 设置竞技场类型
        void SetArenaorBGType(bool _isArena) { m_IsArena = _isArena; }               // 设置是竞技场还是战场
        void SetWinner(PvPTeamId winnerTeamId) { _winnerTeamId = winnerTeamId; }     // 设置获胜方
        void SetScriptId(uint32 scriptId)   { ScriptId = scriptId; }                 // 设置脚本ID

        void ModifyStartDelayTime(int diff) { m_StartDelayTime -= diff; }            // 修改开始延迟时间
        void SetStartDelayTime(int Time)    { m_StartDelayTime = Time; }             // 设置开始延迟时间

        void SetMaxPlayersPerTeam(uint32 MaxPlayers) { m_MaxPlayersPerTeam = MaxPlayers; } // 设置每队最大玩家数
        void SetMinPlayersPerTeam(uint32 MinPlayers) { m_MinPlayersPerTeam = MinPlayers; } // 设置每队最小玩家数

        /**
         * @brief 将战场添加到空闲槽位队列
         *
         * 当战场有空位时，将其添加到队列中以便快速匹配
         */
        void AddToBGFreeSlotQueue();

        /**
         * @brief 从空闲槽位队列中移除战场
         *
         * 当战场满员或销毁时，从队列中移除
         * 如果有其他空闲战场可用，可能会删除整个战场实例
         */
        void RemoveFromBGFreeSlotQueue();

        void DecreaseInvitedCount(uint32 team)      { (team == ALLIANCE) ? --m_InvitedAlliance : --m_InvitedHorde; } // 减少已邀请玩家计数
        void IncreaseInvitedCount(uint32 team)      { (team == ALLIANCE) ? ++m_InvitedAlliance : ++m_InvitedHorde; } // 增加已邀请玩家计数

        void SetRandom(bool isRandom) { m_IsRandom = isRandom; }                     // 设置是否为随机战场
        uint32 GetInvitedCount(uint32 team) const   { return (team == ALLIANCE) ? m_InvitedAlliance : m_InvitedHorde; } // 获取已邀请玩家计数

        /**
         * @brief 检查战场是否有空闲槽位
         * @return 有空闲槽位返回true
         */
        bool HasFreeSlots() const;

        /**
         * @brief 获取指定阵营的空闲槽位数
         * @param Team 阵营ID
         * @return 空闲槽位数量
         */
        uint32 GetFreeSlotsForTeam(uint32 Team) const;

        bool isArena() const        { return m_IsArena; }                            // 是否为竞技场
        bool isBattleground() const { return !m_IsArena; }                          // 是否为战场
        bool isRated() const        { return m_IsRated; }                           // 是否为评级比赛

        typedef std::map<ObjectGuid, BattlegroundPlayer> BattlegroundPlayerMap;
        BattlegroundPlayerMap const& GetPlayers() const { return m_Players; }       // 获取所有玩家映射
        uint32 GetPlayersSize() const { return m_Players.size(); }                  // 获取玩家数量

        typedef std::map<uint32, BattlegroundScore*> BattlegroundScoreMap;
        uint32 GetPlayerScoresSize() const { return PlayerScores.size(); }          // 获取玩家分数数量

        uint32 GetReviveQueueSize() const { return m_ReviveQueue.size(); }          // 获取复活队列大小

        /**
         * @brief 将玩家添加到复活队列
         * @param npc_guid 灵魂医者GUID
         * @param player_guid 玩家GUID
         *
         * 当玩家死亡后，将其添加到指定灵魂医者的复活队列中
         */
        void AddPlayerToResurrectQueue(ObjectGuid npc_guid, ObjectGuid player_guid);

        /**
         * @brief 从复活队列中移除玩家
         * @param player_guid 玩家GUID
         *
         * 当玩家复活或离开战场时，从复活队列中移除
         */
        void RemovePlayerFromResurrectQueue(ObjectGuid player_guid);

        /**
         * @brief 重新定位所有复活队列中的玩家到最近的墓地
         * @param guideGuid 灵魂医者GUID
         *
         * 将所有在指定灵魂医者复活队列中的死亡玩家传送到最近的墓地
         */
        void RelocateDeadPlayers(ObjectGuid guideGuid);

        /**
         * @brief 启动战场
         *
         * 初始化战场并开始战场的开始倒计时流程
         */
        void StartBattleground();

        /**
         * @brief 获取战场游戏对象
         * @param type 对象类型索引
         * @param logError 是否记录错误日志
         * @return 游戏对象指针，如果不存在返回nullptr
         */
        GameObject* GetBGObject(uint32 type, bool logError = true);

        /**
         * @brief 获取战场生物
         * @param type 生物类型索引
         * @param logError 是否记录错误日志
         * @return 生物指针，如果不存在返回nullptr
         */
        Creature* GetBGCreature(uint32 type, bool logError = true);

        // Location
        void SetMapId(uint32 MapID) { m_MapId = MapID; }                            // 设置地图ID
        uint32 GetMapId() const { return m_MapId; }                                 // 获取地图ID

        // Map pointers
        void SetBgMap(BattlegroundMap* map) { m_Map = map; }                        // 设置战场地图指针
        BattlegroundMap* GetBgMap() const { ASSERT(m_Map); return m_Map; }         // 获取战场地图指针（断言非空）
        BattlegroundMap* FindBgMap() const { return m_Map; }                        // 查找战场地图指针（可能为空）

        /**
         * @brief 设置阵营起始位置
         * @param teamId 阵营ID
         * @param pos 位置信息
         */
        void SetTeamStartPosition(TeamId teamId, Position const& pos);

        /**
         * @brief 获取阵营起始位置
         * @param teamId 阵营ID
         * @return 起始位置指针
         */
        Position const* GetTeamStartPosition(TeamId teamId) const;

        void SetStartMaxDist(float startMaxDist) { m_StartMaxDist = startMaxDist; } // 设置最大起始距离
        float GetStartMaxDist() const { return m_StartMaxDist; }                    // 获取最大起始距离

        // Packet Transfer
        /**
         * @brief 填充初始世界状态数据包
         * @param packet 世界状态数据包
         *
         * 应该在战场子类中实现，填充该战场特有的世界状态值
         */
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) { }

        /**
         * @brief 向指定阵营发送数据包
         * @param TeamID 阵营ID
         * @param packet 数据包指针
         * @param sender 发送者（可选）
         * @param self 是否发送给自己（可选）
         */
        void SendPacketToTeam(uint32 TeamID, WorldPacket const* packet, Player* sender = nullptr, bool self = true);

        /**
         * @brief 向所有玩家发送数据包
         * @param packet 数据包指针
         */
        void SendPacketToAll(WorldPacket const* packet);

        /**
         * @brief 发送聊天消息
         * @param source 消息来源生物
         * @param textId 文本ID
         * @param target 目标对象（可选）
         */
        void SendChatMessage(Creature* source, uint8 textId, WorldObject* target = nullptr);

        /**
         * @brief 发送广播文本
         * @param id 广播文本ID
         * @param msgType 消息类型
         * @param target 目标对象（可选）
         */
        void SendBroadcastText(uint32 id, ChatMsg msgType, WorldObject const* target = nullptr);

        template<class Do>
        void BroadcastWorker(Do& _do);

        /**
         * @brief 向指定阵营播放音效
         * @param soundID 音效ID
         * @param teamID 阵营ID
         */
        void PlaySoundToTeam(uint32 soundID, uint32 teamID);

        /**
         * @brief 向所有玩家播放音效
         * @param soundID 音效ID
         */
        void PlaySoundToAll(uint32 soundID);

        /**
         * @brief 对指定阵营施放法术
         * @param SpellID 法术ID
         * @param TeamID 阵营ID
         */
        void CastSpellOnTeam(uint32 SpellID, uint32 TeamID);

        /**
         * @brief 移除指定阵营的光环
         * @param SpellID 法术ID
         * @param TeamID 阵营ID
         */
        void RemoveAuraOnTeam(uint32 SpellID, uint32 TeamID);

        /**
         * @brief 向指定阵营奖励荣誉
         * @param Honor 荣誉值
         * @param TeamID 阵营ID
         */
        void RewardHonorToTeam(uint32 Honor, uint32 TeamID);

        /**
         * @brief 向指定阵营奖励声望
         * @param faction_id 阵营ID
         * @param Reputation 声望值
         * @param TeamID 玩家阵营ID
         */
        void RewardReputationToTeam(uint32 faction_id, uint32 Reputation, uint32 TeamID);

        /**
         * @brief 更新世界状态变量
         * @param variable 变量ID
         * @param value 新值
         */
        void UpdateWorldState(uint32 variable, uint32 value);

        /**
         * @brief 结束战场
         * @param winner 获胜阵营
         *
         * 处理战场结束逻辑，包括奖励发放、数据统计、清理等
         */
        virtual void EndBattleground(uint32 winner);

        /**
         * @brief 阻止玩家移动
         * @param player 玩家指针
         *
         * 用于战场开始前的准备阶段
         */
        void BlockMovement(Player* player);

        /**
         * @brief 向所有玩家发送警告消息
         * @param entry 消息条目ID
         * @param ... 可变参数
         */
        void SendWarningToAll(uint32 entry, ...);

        /**
         * @brief 向所有玩家发送消息
         * @param entry 消息条目ID
         * @param type 聊天消息类型
         * @param source 来源玩家（可选）
         */
        void SendMessageToAll(uint32 entry, ChatMsg type, Player const* source = nullptr);

        /**
         * @brief 向所有玩家发送格式化消息
         * @param entry 消息条目ID
         * @param type 聊天消息类型
         * @param source 来源玩家
         * @param ... 可变参数
         */
        void PSendMessageToAll(uint32 entry, ChatMsg type, Player const* source, ...);

        // Raid Group
        Group* GetBgRaid(uint32 TeamID) const { return TeamID == ALLIANCE ? m_BgRaids[TEAM_ALLIANCE] : m_BgRaids[TEAM_HORDE]; } // 获取战场团队
        void SetBgRaid(uint32 TeamID, Group* bg_raid);                              // 设置战场团队

        /**
         * @brief 构建PvP日志数据包
         * @param data 数据包引用
         *
         * 构建包含战场统计信息的数据包，用于客户端显示战场记分板
         */
        void BuildPvPLogDataPacket(WorldPacket& data);

        /**
         * @brief 更新玩家分数
         * @param player 玩家指针
         * @param type 分数类型
         * @param value 分数值
         * @param doAddHonor 是否添加荣誉
         * @return 更新成功返回true
         */
        virtual bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true);

        static TeamId GetTeamIndexByTeamId(uint32 Team) { return Team == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE; } // 根据阵营ID获取阵营索引
        uint32 GetPlayersCountByTeam(uint32 Team) const { return m_PlayersCount[GetTeamIndexByTeamId(Team)]; }   // 获取阵营玩家数量

        /**
         * @brief 获取指定阵营的存活玩家数量
         * @param Team 阵营ID
         * @return 存活玩家数量
         *
         * 用于竞技场中正确处理救赎之魂/破釜沉舟等技能死亡情况
         */
        uint32 GetAlivePlayersCountByTeam(uint32 Team) const;

        /**
         * @brief 更新阵营玩家数量
         * @param Team 阵营ID
         * @param remove true为减少，false为增加
         */
        void UpdatePlayersCountByTeam(uint32 Team, bool remove)
        {
            if (remove)
                --m_PlayersCount[GetTeamIndexByTeamId(Team)];
            else
                ++m_PlayersCount[GetTeamIndexByTeamId(Team)];
        }

        /**
         * @brief 检查胜利条件
         *
         * 在战场子类中实现，检查是否满足胜利条件（如旗帜数量、分数等）
         */
        virtual void CheckWinConditions() { }

        // used for rated arena battles
        void SetArenaTeamIdForTeam(uint32 Team, uint32 ArenaTeamId) { m_ArenaTeamIds[GetTeamIndexByTeamId(Team)] = ArenaTeamId; } // 设置阵营的竞技场队伍ID
        uint32 GetArenaTeamIdForTeam(uint32 Team) const             { return m_ArenaTeamIds[GetTeamIndexByTeamId(Team)]; }        // 获取阵营的竞技场队伍ID
        uint32 GetArenaTeamIdByIndex(uint32 index) const { return m_ArenaTeamIds[index]; }                                       // 根据索引获取竞技场队伍ID
        void SetArenaMatchmakerRating(uint32 Team, uint32 MMR){ m_ArenaTeamMMR[GetTeamIndexByTeamId(Team)] = MMR; }              // 设置阵营的匹配等级
        uint32 GetArenaMatchmakerRating(uint32 Team) const          { return m_ArenaTeamMMR[GetTeamIndexByTeamId(Team)]; }        // 获取阵营的匹配等级

        // Triggers handle
        /**
         * @brief 处理区域触发器
         * @param player 玩家指针
         * @param Trigger 触发器ID
         *
         * 必须在战场子类中实现，处理玩家进入特定区域的事件
         */
        virtual void HandleAreaTrigger(Player* /*player*/, uint32 /*Trigger*/);

        /**
         * @brief 处理玩家击杀玩家
         * @param player 被击杀的玩家
         * @param killer 击杀者
         *
         * 如果需要在战场子类中处理，必须调用基类的通用代码
         */
        virtual void HandleKillPlayer(Player* player, Player* killer);

        /**
         * @brief 处理击杀单位
         * @param creature 被击杀的生物
         * @param killer 击杀者
         *
         * 在战场子类中实现，处理击杀NPC的事件
         */
        virtual void HandleKillUnit(Creature* /*creature*/, Player* /*killer*/) { }

        // Battleground events
        /**
         * @brief 事件：玩家掉落旗帜
         * @param player 玩家指针
         *
         * 处理玩家主动掉落旗帜或死亡掉落旗帜的事件
         */
        virtual void EventPlayerDroppedFlag(Player* /*player*/) { }

        /**
         * @brief 事件：玩家点击旗帜
         * @param player 玩家指针
         * @param target_obj 目标游戏对象
         *
         * 处理玩家点击战场旗帜的事件
         */
        virtual void EventPlayerClickedOnFlag(Player* /*player*/, GameObject* /*target_obj*/) { }

        /**
         * @brief 事件：玩家登录
         * @param player 玩家指针
         *
         * 处理玩家在战场中登录的事件
         */
        void EventPlayerLoggedIn(Player* player);

        /**
         * @brief 事件：玩家登出
         * @param player 玩家指针
         *
         * 处理玩家在战场中登出的事件
         */
        void EventPlayerLoggedOut(Player* player);

        /**
         * @brief 处理事件
         * @param obj 世界对象
         * @param eventId 事件ID
         * @param invoker 触发者（可选）
         *
         * 处理战场中的自定义事件
         */
        virtual void ProcessEvent(WorldObject* /*obj*/, uint32 /*eventId*/, WorldObject* /*invoker*/ = nullptr) { }

        // this function can be used by spell to interact with the BG map
        /**
         * @brief 执行动作
         * @param action 动作ID
         * @param var 对象GUID
         *
         * 允许法术与战场地图交互
         */
        virtual void DoAction(uint32 /*action*/, ObjectGuid /*var*/) { }

        /**
         * @brief 处理玩家复活
         * @param player 玩家指针
         *
         * 处理玩家在战场中复活的事件
         */
        virtual void HandlePlayerResurrect(Player* /*player*/) { }

        // Death related
        /**
         * @brief 获取最近的墓地
         * @param player 玩家指针
         * @return 墓地条目指针
         *
         * 返回玩家最近的墓地位置信息
         */
        virtual WorldSafeLocsEntry const* GetClosestGraveyard(Player* player);

        /**
         * @brief 添加玩家到战场
         * @param player 玩家指针
         *
         * 必须在战场子类中实现，处理玩家加入战场的逻辑
         */
        virtual void AddPlayer(Player* player);

        /**
         * @brief 将玩家添加或设置到正确的战场队伍
         * @param player 玩家指针
         * @param team 阵营ID
         *
         * 确保玩家在正确的战场团队中
         */
        void AddOrSetPlayerToCorrectBgGroup(Player* player, uint32 team);

        /**
         * @brief 玩家离开时移除玩家
         * @param guid 玩家GUID
         * @param Transport 是否传送
         * @param SendPacket 是否发送数据包
         *
         * 可以在战场子类中扩展，处理玩家离开战场的逻辑
         */
        virtual void RemovePlayerAtLeave(ObjectGuid guid, bool Transport, bool SendPacket);

        /**
         * @brief 处理触发Buff
         * @param go_guid 游戏对象GUID
         *
         * 处理玩家拾取战场Buff的事件
         */
        void HandleTriggerBuff(ObjectGuid go_guid);

        /**
         * @brief 设置节日状态
         * @param is_holiday 是否为节日战场
         *
         * 在战场节日周末期间获得额外荣誉
         */
        void SetHoliday(bool is_holiday);

        /// @todo make this protected:
        GuidVector BgObjects;                                                       // 战场游戏对象GUID向量
        GuidVector BgCreatures;                                                     // 战场生物GUID向量

        /**
         * @brief 生成战场游戏对象
         * @param type 对象类型索引
         * @param respawntime 重生时间
         */
        void SpawnBGObject(uint32 type, uint32 respawntime);

        /**
         * @brief 添加游戏对象到战场
         * @param type 对象类型索引
         * @param entry 对象条目ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         * @param rotation0-3 旋转参数
         * @param respawnTime 重生时间
         * @param goState 游戏对象状态
         * @return 成功返回true
         */
        virtual bool AddObject(uint32 type, uint32 entry, float x, float y, float z, float o, float rotation0, float rotation1, float rotation2, float rotation3, uint32 respawnTime = 0, GOState goState = GO_STATE_READY);
        bool AddObject(uint32 type, uint32 entry, Position const& pos, float rotation0, float rotation1, float rotation2, float rotation3, uint32 respawnTime = 0, GOState goState = GO_STATE_READY);

        /**
         * @brief 添加生物到战场
         * @param entry 生物条目ID
         * @param type 生物类型索引
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         * @param teamId 阵营ID
         * @param respawntime 重生时间
         * @param transport 运输工具（可选）
         * @return 生物指针
         */
        virtual Creature* AddCreature(uint32 entry, uint32 type, float x, float y, float z, float o, TeamId teamId = TEAM_NEUTRAL, uint32 respawntime = 0, Transport* transport = nullptr);
        Creature* AddCreature(uint32 entry, uint32 type, Position const& pos, TeamId teamId = TEAM_NEUTRAL, uint32 respawntime = 0, Transport* transport = nullptr);

        /**
         * @brief 删除战场生物
         * @param type 生物类型索引
         * @return 成功返回true
         */
        bool DelCreature(uint32 type);

        /**
         * @brief 删除战场游戏对象
         * @param type 对象类型索引
         * @return 成功返回true
         */
        bool DelObject(uint32 type);

        /**
         * @brief 从世界中移除对象
         * @param type 对象类型索引
         * @return 成功返回true
         */
        bool RemoveObjectFromWorld(uint32 type);

        /**
         * @brief 添加灵魂医者到战场
         * @param type 生物类型索引
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         * @param teamId 阵营ID
         * @return 成功返回true
         */
        virtual bool AddSpiritGuide(uint32 type, float x, float y, float z, float o, TeamId teamId = TEAM_NEUTRAL);
        bool AddSpiritGuide(uint32 type, Position const& pos, TeamId teamId = TEAM_NEUTRAL);

        /**
         * @brief 获取对象类型
         * @param guid 对象GUID
         * @return 对象类型索引，未找到返回-1
         */
        int32 GetObjectType(ObjectGuid guid);

        /**
         * @brief 打开大门
         * @param type 大门类型索引
         */
        void DoorOpen(uint32 type);

        /**
         * @brief 关闭大门
         * @param type 大门类型索引
         */
        void DoorClose(uint32 type);

        /**
         * @brief 处理地图下方的玩家
         * @param player 玩家指针
         * @return 处理成功返回true
         *
         * 处理玩家掉落到地图下方的情况
         */
        virtual bool HandlePlayerUnderMap(Player* /*player*/) { return false; }

        // since arenas can be AvA or Hvh, we have to get the "temporary" team of a player
        /**
         * @brief 获取玩家在战场中的阵营
         * @param guid 玩家GUID
         * @return 阵营ID
         *
         * 因为竞技场可能是联盟对联盟或部落对部落，需要获取玩家的"临时"阵营
         */
        uint32 GetPlayerTeam(ObjectGuid guid) const;

        /**
         * @brief 获取对方阵营
         * @param teamId 阵营ID
         * @return 对方阵营ID
         */
        uint32 GetOtherTeam(uint32 teamId) const;

        /**
         * @brief 检查玩家是否在战场中
         * @param guid 玩家GUID
         * @return 在战场中返回true
         */
        bool IsPlayerInBattleground(ObjectGuid guid) const;

        bool ToBeDeleted() const { return m_SetDeleteThis; }                        // 是否待删除
        void SetDeleteThis() { m_SetDeleteThis = true; }                            // 设置待删除标志

        /**
         * @brief 在击杀时奖励经验值
         * @param killer 击杀者
         * @param victim 受害者
         */
        void RewardXPAtKill(Player* killer, Player* victim);

        bool CanAwardArenaPoints() const { return m_LevelMin >= BG_AWARD_ARENA_POINTS_MIN_LEVEL; } // 是否可以授予竞技场点数

        virtual ObjectGuid GetFlagPickerGUID(int32 /*team*/ = -1) const { return ObjectGuid::Empty; } // 获取旗帜携带者GUID
        virtual void SetDroppedFlagGUID(ObjectGuid /*guid*/, int32 /*team*/ = -1) { }                 // 设置掉落旗帜GUID
        virtual void HandleQuestComplete(uint32 /*questid*/, Player* /*player*/) { }                  // 处理任务完成
        virtual bool CanActivateGO(int32 /*entry*/, uint32 /*team*/) const { return true; }           // 是否可以激活游戏对象
        virtual bool IsSpellAllowed(uint32 /*spellId*/, Player const* /*player*/) const { return true; } // 是否允许使用法术

        /**
         * @brief 获取阵营分数
         * @param TeamID 阵营ID
         * @return 分数
         */
        uint32 GetTeamScore(uint32 TeamID) const;

        /**
         * @brief 获取过早结束的获胜方
         * @return 获胜阵营
         *
         * 当战场因人数不足而过早结束时，决定获胜方
         */
        virtual uint32 GetPrematureWinner();

        // because BattleGrounds with different types and same level range has different m_BracketId
        /**
         * @brief 获取唯一分段ID
         * @return 分段ID
         *
         * 因为不同类型但相同等级范围的战场有不同的分段ID
         */
        uint8 GetUniqueBracketId() const;

        Trinity::unique_weak_ptr<Battleground> GetWeakPtr() const { return m_weakRef; }              // 获取弱引用指针
        void SetWeakPtr(Trinity::unique_weak_ptr<Battleground> weakRef) { m_weakRef = std::move(weakRef); } // 设置弱引用指针

    protected:
        /**
         * @brief 立即结束战场
         *
         * 当战场无法生成自己的灵魂医者或出现问题时调用此方法
         * 正确地结束战场
         */
        void EndNow();

        /**
         * @brief 检查玩家加入的战场是否正在运行
         * @param player 玩家指针
         *
         * 当玩家加入战场时检查战场状态，如果战场已经开始则进行相应处理
         */
        void PlayerAddedToBGCheckIfBGIsRunning(Player* player);

        /**
         * @brief 获取玩家指针（内部方法）
         * @param guid 玩家GUID
         * @param offlineRemove 是否离线移除
         * @param context 上下文字符串（用于日志）
         * @return 玩家指针
         */
        Player* _GetPlayer(ObjectGuid guid, bool offlineRemove, char const* context) const;
        Player* _GetPlayer(BattlegroundPlayerMap::iterator itr, char const* context) { return _GetPlayer(itr->first, itr->second.OfflineRemoveTime != 0, context); }
        Player* _GetPlayer(BattlegroundPlayerMap::const_iterator itr, char const* context) const { return _GetPlayer(itr->first, itr->second.OfflineRemoveTime != 0, context); }
        Player* _GetPlayerForTeam(uint32 teamId, BattlegroundPlayerMap::const_iterator itr, char const* context) const;

        /**
         * @brief 处理离线队列
         *
         * 检查并移除离线时间过长的玩家
         */
        void _ProcessOfflineQueue();

        /**
         * @brief 处理复活逻辑
         * @param diff 时间间隔（毫秒）
         *
         * 处理战场中的玩家复活队列
         */
        void _ProcessResurrect(uint32 diff);

        /**
         * @brief 处理战场进行中的逻辑
         * @param diff 时间间隔（毫秒）
         *
         * 处理战场运行时的更新逻辑
         */
        void _ProcessProgress(uint32 diff);

        /**
         * @brief 处理玩家离开逻辑
         * @param diff 时间间隔（毫秒）
         *
         * 处理玩家离开战场的逻辑，包括过早结束检测
         */
        void _ProcessLeave(uint32 diff);

        /**
         * @brief 处理玩家加入逻辑
         * @param diff 时间间隔（毫秒）
         *
         * 处理玩家加入战场的逻辑，包括开始倒计时
         */
        void _ProcessJoin(uint32 diff);

        /**
         * @brief 检查安全位置
         * @param diff 时间间隔（毫秒）
         *
         * 检查玩家是否在安全区域内，防止作弊
         */
        void _CheckSafePositions(uint32 diff);

        // Scorekeeping
        BattlegroundScoreMap PlayerScores;                                          // 玩家分数映射

        /**
         * @brief 移除玩家（纯虚函数）
         * @param player 玩家指针
         * @param guid 玩家GUID
         * @param team 阵营ID
         *
         * 必须在战场子类中实现，处理移除玩家时的特定逻辑
         */
        virtual void RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/, uint32 /*team*/) { }

        // Player lists, those need to be accessible by inherited classes
        BattlegroundPlayerMap m_Players;                                            // 玩家映射表
        // Spirit Guide guid + Player list GUIDS
        std::map<ObjectGuid, GuidVector> m_ReviveQueue;                             // 复活队列：灵魂医者GUID -> 玩家GUID列表

        // these are important variables used for starting messages
        uint8 m_Events;                                                             // 事件标志
        BattlegroundStartTimeIntervals StartDelayTimes[BG_STARTING_EVENT_COUNT];   // 开始延迟时间数组
        // this must be filled in constructors!
        uint32 StartMessageIds[BG_STARTING_EVENT_COUNT];                            // 开始消息ID数组，必须在构造函数中填充

        bool   m_BuffChange;                                                        // Buff是否改变
        bool   m_IsRandom;                                                          // 是否为随机战场

        BGHonorMode m_HonorMode;                                                    // 荣誉模式
        int32 m_TeamScores[PVP_TEAMS_COUNT];                                        // 阵营分数数组

        ArenaTeamScore _arenaTeamScores[PVP_TEAMS_COUNT];                          // 竞技场队伍分数数组

    private:
        // Battleground
        BattlegroundTypeId m_TypeID;                                                // 战场类型ID
        BattlegroundTypeId m_RandomTypeID;                                          // 随机战场类型ID
        uint32 m_InstanceID;                                                        // 战场实例ID（GUID）
        BattlegroundStatus m_Status;                                                // 战场状态
        uint32 m_ClientInstanceID;                                                  // 发送给客户端的实例ID（无其他内部用途）
        uint32 m_StartTime;                                                         // 战场开始时间
        uint32 m_ResetStatTimer;                                                    // 重置统计计时器
        uint32 m_ValidStartPositionTimer;                                           // 有效起始位置计时器
        int32 m_EndTime;                                                            // 结束时间（战场结束时设为120000并递减）
        uint32 m_LastResurrectTime;                                                 // 上次复活时间
        BattlegroundBracketId m_BracketId;                                          // 战场等级分段ID
        uint8  m_ArenaType;                                                         // 竞技场类型（2=2v2, 3=3v3, 5=5v5）
        bool   m_InBGFreeSlotQueue;                                                 // 是否在空闲槽位队列中（确保只插入一次）
        bool   m_SetDeleteThis;                                                     // 设置删除标志（用于安全删除战场）
        bool   m_IsArena;                                                           // 是否为竞技场
        PvPTeamId _winnerTeamId;                                                    // 获胜队伍ID
        int32  m_StartDelayTime;                                                    // 开始延迟时间
        bool   m_IsRated;                                                           // 是否为评级比赛
        bool   m_PrematureCountDown;                                                // 是否过早倒计时
        uint32 m_PrematureCountDownTimer;                                           // 过早倒计时计时器
        std::string m_Name;                                                         // 战场名称

        /* Pre- and post-update hooks */

        /**
         * @brief Pre-update hook.
         *
         * Will be called before battleground update is started. Depending on
         * the result of this call actual update body may be skipped.
         *
         * @param diff a time difference between two worldserver update loops in
         * milliseconds.
         *
         * @return @c true if update must be performed, @c false otherwise.
         *
         * @see Update(), PostUpdateImpl().
         */
        virtual bool PreUpdateImpl(uint32 /* diff */) { return true; }

        /**
         * @brief Post-update hook.
         *
         * Will be called after battleground update has passed. May be used to
         * implement custom update effects in subclasses.
         *
         * @param diff a time difference between two worldserver update loops in
         * milliseconds.
         *
         * @see Update(), PreUpdateImpl().
         */
        virtual void PostUpdateImpl(uint32 /* diff */) { }

        // Player lists
        GuidVector m_ResurrectQueue;                                                // 复活队列（玩家GUID）
        std::deque<ObjectGuid> m_OfflineQueue;                                      // 离线队列（玩家GUID）

        // Invited counters are useful for player invitation to BG - do not allow, if BG is started to one faction to have 2 more players than another faction
        // Invited counters will be changed only when removing already invited player from queue, removing player from battleground and inviting player to BG
        // Invited players counters
        uint32 m_InvitedAlliance;                                                   // 已邀请联盟玩家计数
        uint32 m_InvitedHorde;                                                      // 已邀请部落玩家计数

        // Raid Group
        Group* m_BgRaids[PVP_TEAMS_COUNT];                                          // 战场团队数组（0-联盟，1-部落）

        // Players count by team
        uint32 m_PlayersCount[PVP_TEAMS_COUNT];                                     // 阵营玩家数量数组

        // Arena team ids by team
        uint32 m_ArenaTeamIds[PVP_TEAMS_COUNT];                                     // 竞技场队伍ID数组

        uint32 m_ArenaTeamMMR[PVP_TEAMS_COUNT];                                     // 竞技场队伍匹配等级数组

        // Limits
        uint32 m_LevelMin;                                                          // 最低等级
        uint32 m_LevelMax;                                                          // 最高等级
        uint32 m_MaxPlayersPerTeam;                                                 // 每队最大玩家数
        uint32 m_MaxPlayers;                                                        // 最大玩家总数
        uint32 m_MinPlayersPerTeam;                                                 // 每队最小玩家数
        uint32 m_MinPlayers;                                                        // 最小玩家总数

        // Start location
        uint32 m_MapId;                                                             // 地图ID
        BattlegroundMap* m_Map;                                                     // 战场地图指针
        Position StartPosition[PVP_TEAMS_COUNT];                                    // 阵营起始位置数组
        float m_StartMaxDist;                                                       // 最大起始距离
        uint32 ScriptId;                                                            // 脚本ID

        Trinity::unique_weak_ptr<Battleground> m_weakRef;                           // 弱引用指针
};
#endif
