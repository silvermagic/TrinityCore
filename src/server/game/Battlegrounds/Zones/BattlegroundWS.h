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
 * @file BattlegroundWS.h
 * @brief 战歌峡谷（Warsong Gulch）战场模块
 *
 * 本模块实现了战歌峡谷战场的核心逻辑，包括：
 * - 夺旗战机制
 * - 旗帜携带和掉落系统
 * - 旗帜返回和得分机制
 *
 * 战歌峡谷特点：
 * - 夺旗战：双方争夺对方旗帜并带回己方基地
 * - 先得3分者获胜
 * - 旗帜携带者会受到递增debuff（持久强攻）
 * - 最长时限25分钟，超时按得分判定胜负
 */

#ifndef __BATTLEGROUNDWS_H
#define __BATTLEGROUNDWS_H

#include "Battleground.h"
#include "BattlegroundScore.h"

/**
 * @brief 战歌峡谷计时器和分数常量枚举
 *
 * 定义战场的关键时间参数和分数限制
 */
enum BG_WS_TimerOrScore
{
    BG_WS_MAX_TEAM_SCORE    = 3,       ///< 最大团队得分（先到3分获胜）
    BG_WS_FLAG_RESPAWN_TIME = 23000,   ///< 旗帜重生时间：23秒
    BG_WS_FLAG_DROP_TIME    = 10000,   ///< 旗帜掉落保护时间：10秒
    BG_WS_SPELL_FORCE_TIME  = 600000,  ///< 集中强攻法术时间：10分钟
    BG_WS_SPELL_BRUTAL_TIME = 900000   ///< 残暴强攻法术时间：15分钟
};

enum BG_WS_BroadcastTexts
{
    BG_WS_TEXT_START_ONE_MINUTE         = 10015,
    BG_WS_TEXT_START_HALF_MINUTE        = 10016,
    BG_WS_TEXT_BATTLE_HAS_BEGUN         = 10014,

    BG_WS_TEXT_CAPTURED_HORDE_FLAG      = 9801,
    BG_WS_TEXT_CAPTURED_ALLIANCE_FLAG   = 9802,
    BG_WS_TEXT_FLAGS_PLACED             = 9803,
    BG_WS_TEXT_ALLIANCE_FLAG_PICKED_UP  = 9804,
    BG_WS_TEXT_ALLIANCE_FLAG_DROPPED    = 9805,
    BG_WS_TEXT_HORDE_FLAG_PICKED_UP     = 9807,
    BG_WS_TEXT_HORDE_FLAG_DROPPED       = 9806,
    BG_WS_TEXT_ALLIANCE_FLAG_RETURNED   = 9808,
    BG_WS_TEXT_HORDE_FLAG_RETURNED      = 9809,
};

enum BG_WS_Sound
{
    BG_WS_SOUND_FLAG_CAPTURED_ALLIANCE  = 8173,
    BG_WS_SOUND_FLAG_CAPTURED_HORDE     = 8213,
    BG_WS_SOUND_FLAG_PLACED             = 8232,
    BG_WS_SOUND_FLAG_RETURNED           = 8192,
    BG_WS_SOUND_HORDE_FLAG_PICKED_UP    = 8212,
    BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP = 8174,
    BG_WS_SOUND_FLAGS_RESPAWNED         = 8232
};

enum BG_WS_SpellId
{
    BG_WS_SPELL_WARSONG_FLAG            = 23333,
    BG_WS_SPELL_WARSONG_FLAG_DROPPED    = 23334,
    BG_WS_SPELL_WARSONG_FLAG_PICKED     = 61266,    // fake spell, does not exist but used as timer start event
    BG_WS_SPELL_SILVERWING_FLAG         = 23335,
    BG_WS_SPELL_SILVERWING_FLAG_DROPPED = 23336,
    BG_WS_SPELL_SILVERWING_FLAG_PICKED  = 61265,    // fake spell, does not exist but used as timer start event
    BG_WS_SPELL_FOCUSED_ASSAULT         = 46392,
    BG_WS_SPELL_BRUTAL_ASSAULT          = 46393
};

enum BG_WS_WorldStates
{
    BG_WS_FLAG_UNK_ALLIANCE       = 1545,
    BG_WS_FLAG_UNK_HORDE          = 1546,
//    FLAG_UNK                      = 1547,
    BG_WS_FLAG_CAPTURES_ALLIANCE  = 1581,
    BG_WS_FLAG_CAPTURES_HORDE     = 1582,
    BG_WS_FLAG_CAPTURES_MAX       = 1601,
    BG_WS_FLAG_STATE_HORDE        = 2338,
    BG_WS_FLAG_STATE_ALLIANCE     = 2339,
    BG_WS_STATE_TIMER             = 4248,
    BG_WS_STATE_TIMER_ACTIVE      = 4247
};

enum BG_WS_ObjectTypes
{
    BG_WS_OBJECT_DOOR_A_1       = 0,
    BG_WS_OBJECT_DOOR_A_2       = 1,
    BG_WS_OBJECT_DOOR_A_3       = 2,
    BG_WS_OBJECT_DOOR_A_4       = 3,
    BG_WS_OBJECT_DOOR_A_5       = 4,
    BG_WS_OBJECT_DOOR_A_6       = 5,
    BG_WS_OBJECT_DOOR_H_1       = 6,
    BG_WS_OBJECT_DOOR_H_2       = 7,
    BG_WS_OBJECT_DOOR_H_3       = 8,
    BG_WS_OBJECT_DOOR_H_4       = 9,
    BG_WS_OBJECT_A_FLAG         = 10,
    BG_WS_OBJECT_H_FLAG         = 11,
    BG_WS_OBJECT_SPEEDBUFF_1    = 12,
    BG_WS_OBJECT_SPEEDBUFF_2    = 13,
    BG_WS_OBJECT_REGENBUFF_1    = 14,
    BG_WS_OBJECT_REGENBUFF_2    = 15,
    BG_WS_OBJECT_BERSERKBUFF_1  = 16,
    BG_WS_OBJECT_BERSERKBUFF_2  = 17,
    BG_WS_OBJECT_MAX            = 18
};

enum BG_WS_ObjectEntry
{
    BG_OBJECT_DOOR_A_1_WS_ENTRY          = 179918,
    BG_OBJECT_DOOR_A_2_WS_ENTRY          = 179919,
    BG_OBJECT_DOOR_A_3_WS_ENTRY          = 179920,
    BG_OBJECT_DOOR_A_4_WS_ENTRY          = 179921,
    BG_OBJECT_DOOR_A_5_WS_ENTRY          = 180322,
    BG_OBJECT_DOOR_A_6_WS_ENTRY          = 180322,
    BG_OBJECT_DOOR_H_1_WS_ENTRY          = 179916,
    BG_OBJECT_DOOR_H_2_WS_ENTRY          = 179917,
    BG_OBJECT_DOOR_H_3_WS_ENTRY          = 180322,
    BG_OBJECT_DOOR_H_4_WS_ENTRY          = 180322,
    BG_OBJECT_A_FLAG_WS_ENTRY            = 179830,
    BG_OBJECT_H_FLAG_WS_ENTRY            = 179831,
    BG_OBJECT_A_FLAG_GROUND_WS_ENTRY     = 179785,
    BG_OBJECT_H_FLAG_GROUND_WS_ENTRY     = 179786
};

/**
 * @brief 旗帜状态枚举
 *
 * 定义旗帜的四种可能状态
 */
enum BG_WS_FlagState
{
    BG_WS_FLAG_STATE_ON_BASE      = 0,  ///< 旗帜在基地
    BG_WS_FLAG_STATE_WAIT_RESPAWN = 1,  ///< 旗帜等待重生
    BG_WS_FLAG_STATE_ON_PLAYER    = 2,  ///< 旗帜被玩家携带
    BG_WS_FLAG_STATE_ON_GROUND    = 3   ///< 旗帜掉落在地上
};

enum BG_WS_Graveyards
{
    WS_GRAVEYARD_FLAGROOM_ALLIANCE = 769,
    WS_GRAVEYARD_FLAGROOM_HORDE    = 770,
    WS_GRAVEYARD_MAIN_ALLIANCE     = 771,
    WS_GRAVEYARD_MAIN_HORDE        = 772
};

enum BG_WS_CreatureTypes
{
    WS_SPIRIT_MAIN_ALLIANCE   = 0,
    WS_SPIRIT_MAIN_HORDE      = 1,

    BG_CREATURES_MAX_WS       = 2
};

enum BG_WS_CarrierDebuffs
{
    WS_SPELL_FOCUSED_ASSAULT   = 46392,
    WS_SPELL_BRUTAL_ASSAULT    = 46393
};

enum BG_WS_Objectives
{
    WS_OBJECTIVE_CAPTURE_FLAG   = 42,
    WS_OBJECTIVE_RETURN_FLAG    = 44
};

#define WS_EVENT_START_BATTLE   8563

/**
 * @struct BattlegroundWGScore
 * @brief 战歌峡谷玩家得分结构
 *
 * 继承自BattlegroundScore，添加战歌峡谷特有的得分统计：
 * - 旗帜夺取次数
 * - 旗帜返回次数
 */
struct BattlegroundWGScore final : public BattlegroundScore
{
    friend class BattlegroundWS;

    protected:
        /**
         * @brief 构造函数
         * @param playerGuid 玩家GUID
         */
        BattlegroundWGScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), FlagCaptures(0), FlagReturns(0) { }

        /**
         * @brief 更新得分
         * @param type 得分类型
         * @param value 得分值
         *
         * 处理战歌峡谷特有的得分类型：
         * - SCORE_FLAG_CAPTURES：夺取旗帜
         * - SCORE_FLAG_RETURNS：返回旗帜
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_FLAG_CAPTURES:   // 旗帜夺取
                    FlagCaptures += value;
                    break;
                case SCORE_FLAG_RETURNS:    // 旗帜返回
                    FlagReturns += value;
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         * @param data 数据包
         *
         * 将玩家得分数据序列化到数据包中
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        uint32 GetAttr1() const final override { return FlagCaptures; }
        uint32 GetAttr2() const final override { return FlagReturns; }

        uint32 FlagCaptures;  ///< 旗帜夺取次数
        uint32 FlagReturns;   ///< 旗帜返回次数
};

/**
 * @class BattlegroundWS
 * @brief 战歌峡谷战场管理类
 *
 * 继承自Battleground，实现战歌峡谷战场的完整逻辑：
 *
 * 核心机制：
 * - 夺旗战：双方各有一面旗帜
 * - 得分规则：携带敌方旗帜回到己方基地并触碰己方旗帜得分
 * - 胜利条件：先得3分或时间结束时得分高者获胜
 *
 * 特殊机制：
 * - 旗帜掉落：携带者死亡时旗帜掉落，10秒后返回基地
 * - 旗帜保护：掉落的旗帜可被队友拾取或敌人返回
 * - 持久强攻：携带旗帜10分钟获得集中强攻debuff
 * - 残暴强攻：携带旗帜15分钟升级为残暴强攻debuff
 * - 时间限制：最长25分钟
 *
 * 基地布局：
 * - 联盟基地：东北方
 * - 部落基地：西南方
 * - 中间区域：包含增益buff
 */
class BattlegroundWS : public Battleground
{
    public:
        /* 构造和析构 */

        /**
         * @brief 构造函数
         *
         * 初始化战场成员变量，设置初始状态
         */
        BattlegroundWS();

        /**
         * @brief 析构函数
         */
        ~BattlegroundWS();

        /* 继承自Battleground的虚函数 */

        /**
         * @brief 添加玩家到战场
         * @param player 要添加的玩家
         *
         * 创建玩家得分记录
         *
         * 调用时机：玩家进入战场时
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 关门事件（准备阶段）
         *
         * 关闭所有大门，初始化世界状态
         *
         * 调用时机：战场准备阶段开始
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开门事件（战斗开始）
         *
         * 打开大门，刷新旗帜和增益效果，启动成就计时
         *
         * 调用时机：战场正式开始
         */
        void StartingEventOpenDoors() override;

        /* 旗帜管理 */

        /**
         * @brief 获取旗帜携带者GUID
         * @param team 团队ID（TEAM_ALLIANCE或TEAM_HORDE）
         * @return 旗帜携带者的GUID，无携带者返回空GUID
         */
        ObjectGuid GetFlagPickerGUID(int32 team) const override
        {
            if (team == TEAM_ALLIANCE || team == TEAM_HORDE)
                return m_FlagKeepers[team];
            return ObjectGuid::Empty;
        }

        /**
         * @brief 设置联盟旗帜携带者
         * @param guid 携带者GUID
         */
        void SetAllianceFlagPicker(ObjectGuid guid) { m_FlagKeepers[TEAM_ALLIANCE] = guid; }

        /**
         * @brief 设置部落旗帜携带者
         * @param guid 携带者GUID
         */
        void SetHordeFlagPicker(ObjectGuid guid)    { m_FlagKeepers[TEAM_HORDE] = guid; }

        /**
         * @brief 检查联盟旗帜是否被拾取
         * @return 被拾取返回true，否则返回false
         */
        bool IsAllianceFlagPickedup() const         { return !m_FlagKeepers[TEAM_ALLIANCE].IsEmpty(); }

        /**
         * @brief 检查部落旗帜是否被拾取
         * @return 被拾取返回true，否则返回false
         */
        bool IsHordeFlagPickedup() const            { return !m_FlagKeepers[TEAM_HORDE].IsEmpty(); }

        /**
         * @brief 重生旗帜
         * @param Team 阵营ID（ALLIANCE或HORDE）
         * @param captured 是否被夺取
         *
         * 将旗帜重置到基地，更新状态和播放音效
         */
        void RespawnFlag(uint32 Team, bool captured);

        /**
         * @brief 旗帜掉落后重生
         * @param Team 阵营ID（ALLIANCE或HORDE）
         *
         * 处理掉落旗帜10秒后自动返回基地的逻辑
         */
        void RespawnFlagAfterDrop(uint32 Team);

        /**
         * @brief 获取旗帜状态
         * @param team 阵营ID
         * @return 旗帜状态（BG_WS_FlagState枚举值）
         */
        uint8 GetFlagState(uint32 team)             { return _flagState[GetTeamIndexByTeamId(team)]; }

        /* 战场事件 */

        /**
         * @brief 玩家掉落旗帜事件
         * @param player 掉落旗帜的玩家
         *
         * 处理玩家死亡或主动掉落旗帜的情况：
         * - 移除旗帜光环
         * - 在地上创建旗帜对象
         * - 启动10秒自动返回计时器
         *
         * 调用时机：携带旗帜的玩家死亡或离开战场
         */
        void EventPlayerDroppedFlag(Player* player) override;

        /**
         * @brief 玩家点击旗帜事件
         * @param player 点击旗帜的玩家
         * @param target_obj 被点击的旗帜对象
         *
         * 处理玩家与旗帜的交互：
         * - 拾取基地旗帜
         * - 拾取地上的旗帜
         * - 返回己方旗帜
         *
         * 调用时机：玩家点击旗帜对象时
         */
        void EventPlayerClickedOnFlag(Player* player, GameObject* target_obj) override;

        /**
         * @brief 玩家夺取旗帜事件
         * @param player 夺取旗帜的玩家
         *
         * 处理旗帜成功夺取：
         * - 移除旗帜光环和debuff
         * - 增加团队得分
         * - 发放荣誉奖励
         * - 判定胜利条件
         *
         * 调用时机：玩家携带敌方旗帜触碰己方旗帜时
         */
        void EventPlayerCapturedFlag(Player* player);

        /**
         * @brief 处理旗室捕获点
         * @param team 团队ID
         *
         * 检查对方旗帜携带者是否在己方旗室，如果在则触发夺旗
         */
        void HandleFlagRoomCapturePoint(int32 team);

        /**
         * @brief 移除玩家
         * @param player 离开的玩家
         * @param guid 玩家GUID
         * @param team 玩家阵营
         *
         * 处理玩家离开战场，如果玩家携带旗帜则掉落旗帜
         *
         * 调用时机：玩家离开战场时
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发
         * @param player 触发区域的玩家
         * @param trigger 区域触发器ID
         *
         * 处理玩家进入特定区域的事件，主要是旗室捕获点
         *
         * 调用时机：玩家进入区域触发器范围时
         */
        void HandleAreaTrigger(Player* player, uint32 trigger) override;

        /**
         * @brief 处理击杀玩家
         * @param player 被击杀的玩家
         * @param killer 击杀者
         *
         * 处理玩家被击杀事件，使携带者掉落旗帜
         *
         * 调用时机：玩家被击杀时
         */
        void HandleKillPlayer(Player* player, Player* killer) override;

        /**
         * @brief 设置战场
         * @return 设置成功返回true，否则返回false
         *
         * 创建所有战场游戏对象：
         * - 双方旗帜
         * - 大门
         * - 增益buff
         * - 灵魂healer
         *
         * 调用时机：战场初始化时
         */
        bool SetupBattleground() override;

        /**
         * @brief 重置战场
         *
         * 重置所有状态变量和旗帜状态
         *
         * 调用时机：战场重置时
         */
        void Reset() override;

        /**
         * @brief 结束战场
         * @param winner 获胜方阵营ID（ALLIANCE/HORDE/0表示平局）
         *
         * 发放荣誉奖励，调用基类结束逻辑
         *
         * 调用时机：战斗结束判定时
         */
        void EndBattleground(uint32 winner) override;

        /**
         * @brief 获取最近的墓地
         * @param player 需要复活的玩家
         * @return 最近的墓地位置信息
         *
         * 根据玩家阵营返回对应墓地：
         * - 战斗中：主墓地
         * - 准备阶段：旗室墓地
         *
         * 调用时机：玩家死亡需要选择复活点时
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /**
         * @brief 更新旗帜状态
         * @param team 阵营ID
         * @param value 状态值
         *
         * 更新世界状态，通知客户端旗帜状态变化
         */
        void UpdateFlagState(uint32 team, uint32 value);

        /**
         * @brief 设置最后夺旗方
         * @param team 阵营ID
         *
         * 记录最后夺旗的阵营，用于平局时判定胜负
         */
        void SetLastFlagCapture(uint32 team)                { _lastFlagCaptureTeam = team; }

        /**
         * @brief 更新团队得分
         * @param team 团队ID
         *
         * 更新世界状态，通知客户端得分变化
         */
        void UpdateTeamScore(uint32 team);

        /**
         * @brief 更新玩家得分
         * @param player 玩家
         * @param type 得分类型
         * @param value 得分值
         * @param doAddHonor 是否添加荣誉
         * @return 更新成功返回true，否则返回false
         *
         * 更新玩家得分并触发成就进度
         */
        bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true) override;

        /**
         * @brief 设置掉落旗帜GUID
         * @param guid 旗帜游戏对象GUID
         * @param team 团队ID（-1表示不设置）
         */
        void SetDroppedFlagGUID(ObjectGuid guid, int32 team = -1) override
        {
            if (team == TEAM_ALLIANCE || team == TEAM_HORDE)
                m_DroppedFlagGUID[team] = guid;
        }

        /**
         * @brief 获取掉落旗帜GUID
         * @param TeamID 阵营ID
         * @return 掉落旗帜的游戏对象GUID
         */
        ObjectGuid GetDroppedFlagGUID(uint32 TeamID)             { return m_DroppedFlagGUID[GetTeamIndexByTeamId(TeamID)]; }

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         *
         * 向客户端发送战场的初始世界状态信息
         *
         * 调用时机：玩家进入战场时
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /* 得分管理 */

        void AddPoint(uint32 TeamID, uint32 Points = 1)     { m_TeamScores[GetTeamIndexByTeamId(TeamID)] += Points; }
        void SetTeamPoint(uint32 TeamID, uint32 Points = 0) { m_TeamScores[GetTeamIndexByTeamId(TeamID)] = Points; }
        void RemovePoint(uint32 TeamID, uint32 Points = 1)  { m_TeamScores[GetTeamIndexByTeamId(TeamID)] -= Points; }

        /**
         * @brief 获取提前结束的获胜方
         * @return 获胜方阵营ID
         *
         * 当战场提前结束时，根据得分判定获胜方
         */
        uint32 GetPrematureWinner() override;

        /* 成就系统 */

        /**
         * @brief 检查成就条件是否满足
         * @param criteriaId 成就条件ID
         * @param source 检查成就的玩家
         * @param target 目标单位
         * @param miscvalue1 额外数值
         * @return 满足条件返回true，否则返回false
         *
         * 检查战歌峡谷特有成就：
         * - BG_CRITERIA_CHECK_SAVE_THE_DAY：拯救日成就
         */
        bool CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* source, Unit const* target = nullptr, uint32 miscvalue1 = 0) override;

    private:
        ObjectGuid m_FlagKeepers[2];           ///< 旗帜携带者GUID数组 [0]=联盟, [1]=部落
        ObjectGuid m_DroppedFlagGUID[2];       ///< 掉落旗帜的游戏对象GUID数组

        uint8 _flagState[2];                   ///< 旗帜状态数组 [0]=联盟, [1]=部落
        int32 _flagsTimer[2];                  ///< 旗帜重生计时器数组（毫秒）
        int32 _flagsDropTimer[2];              ///< 旗帜掉落计时器数组（毫秒）
        uint32 _lastFlagCaptureTeam;           ///< 最后夺旗的团队（用于平局判定）

        uint32 m_ReputationCapture;            ///< 夺旗声望奖励
        uint32 m_HonorWinKills;                ///< 胜利荣誉击杀奖励
        uint32 m_HonorEndKills;                ///< 结束荣誉击杀奖励

        int32 _flagSpellForceTimer;            ///< 旗帜法术强制计时器（毫秒）
        bool _bothFlagsKept;                   ///< 双方旗帜是否都被携带
        uint8 _flagDebuffState;                ///< 旗帜debuff状态：0=无, 1=集中强攻, 2=残暴强攻
        uint8 _minutesElapsed;                 ///< 已过分钟数（用于计时器显示）

        /**
         * @brief 战场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 核心更新逻辑，处理：
         * - 战斗时限检查（25分钟）
         * - 旗帜重生和掉落计时
         * - 旗帜debuff施加（集中强攻/残暴强攻）
         *
         * 调用时机：每次战场更新循环
         */
        void PostUpdateImpl(uint32 diff) override;
};
#endif
