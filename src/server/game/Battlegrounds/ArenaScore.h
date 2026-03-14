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

#ifndef TRINITY_ARENA_SCORE_H
#define TRINITY_ARENA_SCORE_H

#include "BattlegroundScore.h"
#include <sstream>

/**
 * @file ArenaScore.h
 * @brief 竞技场分数系统头文件
 *
 * 本文件定义了竞技场战斗中的分数记录系统，包括：
 * - ArenaScore: 单个玩家在竞技场比赛中的个人成绩记录
 * - ArenaTeamScore: 竞技场战队在比赛中的团队成绩记录
 *
 * 这些数据结构用于存储和传输竞技场比赛结果，包括伤害、治疗、击杀数
 * 以及战队积分变化等信息。
 */

/**
 * @struct ArenaScore
 * @brief 竞技场玩家个人成绩结构体
 *
 * 继承自 BattlegroundScore，用于记录单个玩家在竞技场比赛中的个人表现数据。
 * 除了基础的战场分数信息（伤害、治疗、击杀等）外，还包含玩家的阵营信息。
 *
 * 该结构体主要用于：
 * - 记录玩家在竞技场比赛中的统计数据
 * - 生成发送给客户端的比赛结果数据包
 * - 提供日志记录功能
 *
 * @see BattlegroundScore
 * @see Arena
 */
struct TC_GAME_API ArenaScore : public BattlegroundScore
{
    friend class Arena;

    protected:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场玩家成绩记录。
         *
         * @param playerGuid 玩家的全局唯一标识符（GUID）
         * @param team 玩家所属阵营（ALLIANCE 或 HORDE）
         *
         * @note 阵营参数会被转换为 PVP_TEAM_ALLIANCE 或 PVP_TEAM_HORDE
         *       枚举值存储在 TeamId 成员变量中
         */
        ArenaScore(ObjectGuid playerGuid, uint32 team) : BattlegroundScore(playerGuid), TeamId(team == ALLIANCE ? PVP_TEAM_ALLIANCE : PVP_TEAM_HORDE) { }

        /**
         * @brief 将成绩数据追加到数据包
         *
         * 重写基类方法，将竞技场特定的成绩数据序列化到 WorldPacket 中，
         * 用于发送给客户端显示比赛结果。
         *
         * @param data 要追加数据的 WorldPacket 引用
         */
        void AppendToPacket(WorldPacket& data) final override;

        /**
         * @brief 构建目标数据块
         *
         * 重写基类方法，构建竞技场比赛目标相关的数据块。
         * 竞技场模式通常不使用传统战场的目标系统。
         *
         * @param data 要追加数据的 WorldPacket 引用
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        /**
         * @brief 将成绩转换为字符串表示
         *
         * 用于日志记录目的，生成包含关键统计数据的可读字符串。
         *
         * @return std::string 包含伤害、治疗和击杀数的格式化字符串
         *
         * @note 返回格式示例: "Damage done: 12345, Healing done: 6789, Killing blows: 5"
         */
        std::string ToString() const override
        {
            std::ostringstream stream;
            stream << "Damage done: " << DamageDone << ", Healing done: " << HealingDone << ", Killing blows: " << KillingBlows;
            return stream.str();
        }

        /**
         * @brief 玩家所属阵营ID
         *
         * 存储玩家在竞技场比赛中的阵营归属。
         * 使用 PvPTeamId 枚举值：
         * - PVP_TEAM_ALLIANCE (0): 联盟阵营
         * - PVP_TEAM_HORDE (1): 部落阵营
         *
         * 该值用于：
         * - 确定玩家在竞技场中的显示颜色
         * - 区分胜负队伍
         * - 统计各阵营的比赛数据
         */
        uint8 TeamId; // PvPTeamId
};

/**
 * @struct ArenaTeamScore
 * @brief 竞技场战队成绩结构体
 *
 * 用于记录竞技场战队在单场比赛中的团队成绩数据。
 * 包含积分变化、匹配积分和战队名称等关键信息。
 *
 * 该结构体主要用于：
 * - 存储战队比赛结果
 * - 计算和记录积分变化
 * - 生成发送给客户端的战队信息数据包
 *
 * @note 此类与 ArenaScore 不同，它记录的是团队级别的数据，
 *       而非单个玩家的数据
 *
 * @see Arena
 * @see Battleground
 */
struct TC_GAME_API ArenaTeamScore
{
    friend class Arena;
    friend class Battleground;

    protected:
        /**
         * @brief 默认构造函数
         *
         * 初始化战队成绩记录，将积分变化和匹配积分设为0。
         */
        ArenaTeamScore() : RatingChange(0), MatchmakerRating(0) { }

        /**
         * @brief 虚析构函数
         *
         * 确保派生类对象能够正确析构。
         */
        virtual ~ArenaTeamScore() { }

        /**
         * @brief 重置战队成绩数据
         *
         * 将所有成员变量重置为初始状态：
         * - RatingChange 重置为 0
         * - MatchmakerRating 重置为 0
         * - TeamName 清空
         *
         * 通常在开始新比赛前调用，确保数据清洁。
         */
        void Reset()
        {
            RatingChange = 0;
            MatchmakerRating = 0;
            TeamName.clear();
        }

        /**
         * @brief 分配战队成绩数据
         *
         * 设置战队在比赛中的成绩信息。
         *
         * @param ratingChange 本场比赛导致的积分变化值
         *                     正值表示获得积分，负值表示失去积分
         * @param matchMakerRating 匹配系统积分（MMR）
         *                         用于匹配对手的隐藏积分值
         * @param teamName 战队名称
         *
         * @note 该方法通常在比赛结束时调用，用于记录最终结果
         */
        void Assign(int32 ratingChange, uint32 matchMakerRating, std::string const& teamName)
        {
            RatingChange = ratingChange;
            MatchmakerRating = matchMakerRating;
            TeamName = teamName;
        }

        /**
         * @brief 构建积分信息数据块
         *
         * 将战队积分相关信息序列化到数据包中，
         * 用于发送给客户端显示比赛结果。
         *
         * @param data 要追加数据的 WorldPacket 引用
         *
         * @note 包含积分变化和匹配积分等信息
         */
        void BuildRatingInfoBlock(WorldPacket& data);

        /**
         * @brief 构建战队信息数据块
         *
         * 将战队基本信息序列化到数据包中，
         * 用于发送给客户端显示比赛结果。
         *
         * @param data 要追加数据的 WorldPacket 引用
         *
         * @note 主要包含战队名称等信息
         */
        void BuildTeamInfoBlock(WorldPacket& data);

        /**
         * @brief 积分变化值
         *
         * 记录本场所比赛导致的战队积分变化。
         * - 正值: 获得积分（胜利）
         * - 负值: 失去积分（失败）
         * - 零: 积分不变
         *
         * 该值会在比赛结束时计算，并显示在结算界面。
         * 积分变化的多少取决于双方的匹配积分差距。
         */
        int32 RatingChange;

        /**
         * @brief 匹配系统积分（Matchmaker Rating, MMR）
         *
         * 这是一个隐藏积分值，用于匹配系统为战队寻找合适的对手。
         *
         * MMR 的特点：
         * - 比显示积分更准确地反映战队真实实力
         * - 根据比赛结果动态调整
         * - 影响匹配对手的范围
         * - 决定积分变化的幅度
         *
         * 例如：
         * - 战胜高 MMR 的对手会获得更多积分
         * - 输给低 MMR 的对手会失去更多积分
         */
        uint32 MatchmakerRating;

        /**
         * @brief 战队名称
         *
         * 存储竞技场战队的名称，用于：
         * - 比赛结算界面显示
         * - 日志记录
         * - 客户端数据展示
         */
        std::string TeamName;
};

#endif // TRINITY_ARENA_SCORE_H
