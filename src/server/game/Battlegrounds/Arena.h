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
 * @file Arena.h
 * @brief 竞技场战场类定义文件
 *
 * 本文件定义了竞技场(Arena)类，继承自战场(Battleground)基类。
 * 竞技场是游戏中的小规模PvP战斗场所，支持2v2、3v3、5v5等不同规模的对战。
 *
 * 主要功能包括:
 * - 竞技场比赛的开始、进行和结束流程控制
 * - 玩家加入和离开竞技场的处理
 * - 竞技场积分和个人评分的计算与更新
 * - 战斗胜利条件的判定
 * - 世界状态的同步与更新
 *
 * @see Battleground
 * @see ArenaTeam
 */

#ifndef TRINITY_ARENA_H
#define TRINITY_ARENA_H

#include "Battleground.h"

/**
 * @brief 竞技场广播文本ID枚举
 *
 * 定义竞技场比赛开始倒计时的广播文本ID，
 * 用于向玩家发送比赛开始前的倒计时提示。
 */
enum ArenaBroadcastTexts
{
    ARENA_TEXT_START_ONE_MINUTE             = 15740,  ///< 比赛开始前1分钟提示
    ARENA_TEXT_START_THIRTY_SECONDS         = 15741,  ///< 比赛开始前30秒提示
    ARENA_TEXT_START_FIFTEEN_SECONDS        = 15739,  ///< 比赛开始前15秒提示
    ARENA_TEXT_START_BATTLE_HAS_BEGUN       = 15742,  ///< 比赛开始提示
};

/**
 * @brief 竞技场法术ID枚举
 *
 * 定义竞技场中使用的特殊法术ID，
 * 主要包括队伍旗帜和成就相关法术。
 */
enum ArenaSpellIds
{
    SPELL_ALLIANCE_GOLD_FLAG                = 32724,  ///< 联盟金色旗帜（联盟队伍使用）
    SPELL_ALLIANCE_GREEN_FLAG               = 32725,  ///< 联盟绿色旗帜
    SPELL_HORDE_GOLD_FLAG                   = 35774,  ///< 部落金色旗帜
    SPELL_HORDE_GREEN_FLAG                  = 35775,  ///< 部落绿色旗帜（部落队伍使用）

    SPELL_LAST_MAN_STANDING                 = 26549   ///< 最后的幸存者成就法术（5v5竞技场中唯一存活玩家获得）
};

/**
 * @brief 竞技场世界状态枚举
 *
 * 定义竞技场界面显示的世界状态ID，
 * 用于同步双方队伍的存活玩家数量。
 */
enum ArenaWorldStates
{
    ARENA_WORLD_STATE_ALIVE_PLAYERS_GREEN   = 3600,   ///< 绿色方存活玩家数量（世界状态ID）
    ARENA_WORLD_STATE_ALIVE_PLAYERS_GOLD    = 3601    ///< 金色方存活玩家数量（世界状态ID）
};

/**
 * @class Arena
 * @brief 竞技场类，管理竞技场比赛的全流程
 *
 * 继承自 Battleground 基类，实现竞技场特有的逻辑。
 * 竞技场是小规模的PvP战斗场所，与普通战场相比有以下特点:
 * - 参战人数固定（2v2/3v3/5v5）
 * - 有积分系统和匹配机制
 * - 比赛时间限制为45分钟
 * - 一方全灭即判定胜负
 *
 * 职责:
 * - 管理竞技场比赛的生命周期
 * - 处理玩家的加入和离开
 * - 计算和更新竞技场积分
 * - 判定比赛胜负条件
 * - 同步比赛状态到客户端
 */
class TC_GAME_API Arena : public Battleground
{
    protected:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场的基本设置，包括:
         * - 设置比赛开始倒计时延迟
         * - 设置比赛开始提示消息
         *
         * @note 此构造函数为保护类型，只能由派生类或工厂方法调用
         */
        Arena();

        /**
         * @brief 添加玩家到竞技场
         *
         * 将玩家加入竞技场比赛，执行以下操作:
         * - 调用基类的添加玩家方法
         * - 创建玩家的竞技场得分记录
         * - 根据队伍分配对应的旗帜视觉效果
         * - 更新存活玩家数量的世界状态
         *
         * @param player 要添加的玩家指针
         *
         * @note 调用时机: 玩家进入竞技场时
         * @note 性能说明: 时间复杂度 O(1)，但可能触发世界状态更新
         * @see Battleground::AddPlayer
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 从竞技场移除玩家
         *
         * 处理玩家离开竞技场的逻辑:
         * - 检查比赛状态，如果正在等待离开则直接返回
         * - 更新存活玩家数量的世界状态
         * - 检查胜利条件（一方全灭）
         *
         * @param player 要移除的玩家指针（未使用）
         * @param guid 玩家的GUID（未使用）
         * @param team 玩家所属队伍（未使用）
         *
         * @note 调用时机: 玩家离开竞技场或断线时
         * @note 性能说明: 时间复杂度 O(1)，但会触发胜利条件检查
         * @see Battleground::RemovePlayer
         */
        void RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/, uint32 /*team*/) override;

        /**
         * @brief 填充初始世界状态数据包
         *
         * 将竞技场的初始世界状态填充到数据包中，
         * 主要包括双方队伍的初始存活玩家数量。
         *
         * @param packet 世界状态初始化数据包引用
         *
         * @note 调用时机: 玩家进入竞技场时，用于同步初始状态
         * @see Battleground::FillInitialWorldStates
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 更新竞技场世界状态
         *
         * 更新客户端显示的双方队伍存活玩家数量。
         * 绿色方对应部落，金色方对应联盟。
         *
         * @note 调用时机: 玩家加入、离开、死亡时调用
         * @note 性能说明: 时间复杂度 O(n)，n为队伍中的玩家数量
         */
        void UpdateArenaWorldState();

        /**
         * @brief 处理玩家击杀事件
         *
         * 当玩家在竞技场中被击杀时调用:
         * - 检查比赛是否正在进行
         * - 调用基类的击杀处理方法
         * - 更新存活玩家数量的世界状态
         * - 检查胜利条件
         *
         * @param player 被击杀的玩家指针
         * @param killer 击杀者玩家指针
         *
         * @note 调用时机: 玩家在竞技场中死亡时
         * @note 性能说明: 时间复杂度 O(n)，需要遍历计算存活玩家
         * @see Battleground::HandleKillPlayer
         */
        void HandleKillPlayer(Player* player, Player* killer) override;

    private:
        /**
         * @brief 玩家离开时移除玩家
         *
         * 处理玩家离开竞技场的完整流程:
         * - 如果是积分赛且比赛正在进行，计算积分损失
         * - 标记离线玩家为失败者
         * - 调用基类的移除玩家方法
         *
         * @param guid 玩家的GUID
         * @param transport 是否传送（未使用）
         * @param sendPacket 是否发送数据包通知
         *
         * @note 调用时机: 玩家主动离开、断线或被踢出竞技场时
         * @note 性能说明: 时间复杂度 O(log n)，涉及积分计算和数据库操作
         * @see Battleground::RemovePlayerAtLeave
         */
        void RemovePlayerAtLeave(ObjectGuid guid, bool transport, bool sendPacket) override;

        /**
         * @brief 检查胜利条件
         *
         * 检查竞技场是否满足胜利条件:
         * - 如果联盟方全灭且部落方仍有玩家，部落获胜
         * - 如果部落方全灭且联盟方仍有玩家，联盟获胜
         *
         * @note 调用时机: 玩家死亡或离开竞技场时
         * @note 性能说明: 时间复杂度 O(n)，需要统计双方存活玩家数量
         */
        void CheckWinConditions() override;

        /**
         * @brief 结束竞技场比赛
         *
         * 结束竞技场比赛并进行积分结算:
         * - 如果是积分赛，计算双方队伍的积分变化
         * - 更新个人评分和匹配评分
         * - 处理平局情况（比赛超时）
         * - 更新成就进度
         * - 保存队伍数据到数据库
         * - 发送统计信息给所有队员
         *
         * @param winner 获胜队伍ID（ALLIANCE/HORDE，0表示平局）
         *
         * @note 调用时机: 一方全灭或比赛时间达到上限时
         * @note 性能说明: 时间复杂度 O(n*m)，n为玩家数量，m为队伍成员数量
         * @note 积分计算使用ELO评分系统
         * @see Battleground::EndBattleground
         */
        void EndBattleground(uint32 winner) override;
};

#endif // TRINITY_ARENA_H
