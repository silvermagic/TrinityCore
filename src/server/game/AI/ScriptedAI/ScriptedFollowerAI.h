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
 * @file ScriptedFollowerAI.h
 * @brief 跟随AI模块头文件
 *
 * 本模块提供了跟随玩家的AI实现,用于处理NPC跟随玩家移动的任务场景。
 * 主要功能包括:
 * - 跟随玩家移动
 * - 战斗辅助
 * - 任务状态监控和失败处理
 * - 暂停和恢复跟随
 * - 距离检测和自动消失
 */

#ifndef TRINITY_SCRIPTEDFOLLOWERAI_H
#define TRINITY_SCRIPTEDFOLLOWERAI_H

#include "ScriptedCreature.h"

class Quest;

/**
 * @brief 跟随状态枚举
 *
 * 定义了跟随任务的各种状态,使用位掩码实现状态组合
 */
enum FollowerState : uint32
{
    STATE_FOLLOW_NONE       = 0x000, ///< 无跟随任务进行中
    STATE_FOLLOW_INPROGRESS = 0x001, ///< 跟随进行中(必须始终有此状态才能跟随)
    STATE_FOLLOW_PAUSED     = 0x002, ///< 跟随已暂停(禁用跟随移动)
    STATE_FOLLOW_COMPLETE   = 0x004, ///< 跟随已完成,可以结束
    STATE_FOLLOW_PREEVENT   = 0x008, ///< 前置事件(未实现,允许在跟随开始前运行事件)
    STATE_FOLLOW_POSTEVENT  = 0x010  ///< 后置事件(可在完成时设置,允许在结束后运行事件)
};

/**
 * @brief 跟随AI类
 *
 * 提供NPC跟随玩家的AI实现,继承自ScriptedAI。
 * 支持跟随移动、战斗辅助、任务状态监控等功能。
 *
 * 主要职责:
 * - 管理NPC跟随玩家移动
 * - 处理战斗辅助和玩家受攻击时的响应
 * - 检测玩家距离和任务状态
 * - 提供暂停、恢复和完成跟随的功能
 * - 支持前置和后置事件
 */
class TC_GAME_API FollowerAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 关联的生物对象指针
         */
        explicit FollowerAI(Creature* creature);

        /**
         * @brief 析构函数
         */
        ~FollowerAI() { }

        /**
         * @brief 视线范围内的单位检测
         * @param who 进入视线范围的单位
         *
         * 当单位进入视线范围时调用,用于检测是否需要协助玩家战斗
         *
         * @note 调用时机: 每个游戏周期检测视线范围内的单位
         */
        void MoveInLineOfSight(Unit*) override;

        /**
         * @brief 生物死亡回调
         * @param killer 击杀者(未使用)
         *
         * 当跟随NPC死亡时调用,如果有关联任务则使玩家任务失败
         *
         * @note 调用时机: 生物死亡时
         */
        void JustDied(Unit*) override;

        /**
         * @brief 返回出生点完成回调
         *
         * 当生物返回出生点后调用,如果在跟随中则继续跟随玩家
         *
         * @note 调用时机: 生物完成回家移动时
         */
        void JustReachedHome() override;

        /**
         * @brief 主人被攻击回调
         * @param other 攻击者
         *
         * 当跟随的玩家被攻击时调用,用于协助玩家反击
         *
         * @note 调用时机: 玩家被攻击时
         */
        void OwnerAttackedBy(Unit* other) override;

        /**
         * @brief 更新AI主函数
         * @param uiDiff 距离上次更新的时间差(毫秒)
         *
         * 内部更新函数,处理距离检测、任务状态检查等,
         * 然后调用UpdateFollowerAI进行派生类的自定义逻辑
         *
         * @note 调用时机: 每个游戏周期调用
         * @note 性能注意: 包含计时器操作和距离检测
         */
        void UpdateAI(uint32) override;

        /**
         * @brief 更新跟随AI(可重写)
         * @param uiDiff 距离上次更新的时间差(毫秒)
         *
         * 虚函数,供派生类实现自定义的更新逻辑,如技能、脚本事件等
         *
         * @note 调用时机: 由UpdateAI内部调用
         */
        virtual void UpdateFollowerAI(uint32);

        /**
         * @brief 开始跟随
         * @param player 要跟随的玩家
         * @param factionForFollower 跟随者的阵营ID(默认0,不改变)
         * @param quest 关联的任务ID(默认0)
         *
         * 启动跟随模式,设置跟随参数并开始跟随玩家
         *
         * @note 调用时机: 玩家接受任务或触发跟随事件时
         */
        void StartFollow(Player* player, uint32 factionForFollower = 0, uint32 quest = 0);

        /**
         * @brief 设置跟随暂停状态
         * @param paused true为暂停,false为继续
         *
         * 如果特殊事件需要在跟随过程中暂停或恢复跟随
         *
         * @note 调用时机: 需要在跟随过程中停止或恢复移动时
         */
        void SetFollowPaused(bool paused);

        /**
         * @brief 设置跟随完成
         * @param withEndEvent 是否执行后置事件(默认false)
         *
         * 标记跟随完成,可选择是否执行后置事件
         *
         * @note 调用时机: 任务完成或跟随结束时
         */
        void SetFollowComplete(bool withEndEvent = false);

        /**
         * @brief 检查是否正在护送(跟随)
         * @return 如果正在跟随返回true,否则返回false
         */
        bool IsEscorted() const override { return HasFollowState(STATE_FOLLOW_INPROGRESS); }

        /**
         * @brief 检查是否具有指定跟随状态
         * @param uiFollowState 要检查的状态(位掩码)
         * @return 如果具有该状态返回true,否则返回false
         */
        bool HasFollowState(uint32 uiFollowState) const { return (_followState & uiFollowState) != 0; }

    protected:
        /**
         * @brief 获取跟随的领导者玩家对象
         * @return 玩家指针,如果玩家不存在或不在线返回nullptr
         *
         * 如果领导者已死亡但队伍中有其他存活成员在范围内,
         * 会自动切换到新的领导者
         */
        Player* GetLeaderForFollower();

    private:
        /**
         * @brief 添加跟随状态
         * @param followState 要添加的状态(位掩码)
         */
        void AddFollowState(uint32 followState) { _followState |= followState; }

        /**
         * @brief 移除跟随状态
         * @param followState 要移除的状态(位掩码)
         */
        void RemoveFollowState(uint32 followState) { _followState &= ~followState; }

        /**
         * @brief 检查是否应该协助玩家战斗
         * @param who 敌对单位
         * @return 如果应该协助返回true,否则返回false
         *
         * 检查条件包括: 类型标志、位置可达性、脱战状态、距离和视线等
         */
        bool ShouldAssistPlayerInCombatAgainst(Unit* who) const;

        ObjectGuid _leaderGUID;           ///< 跟随的领导者玩家GUID
        uint32 _updateFollowTimer;        ///< 更新跟随的计时器
        uint32 _followState;              ///< 当前跟随状态(位掩码组合)
        uint32 _questForFollow;           ///< 关联的任务ID(0表示无任务)
};

#endif
