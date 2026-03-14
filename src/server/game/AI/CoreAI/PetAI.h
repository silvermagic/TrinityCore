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
 * @file PetAI.h
 *
 * @brief 宠物AI模块
 *
 * 本文件定义了宠物AI类，用于控制玩家拥有的宠物、守护者、图腾等生物。
 *
 * 宠物AI的主要功能：
 * - 自动攻击和跟随主人
 * - 响应主人的攻击和被攻击事件
 * - 管理宠物的命令状态（攻击、跟随、停留）
 * - 管理宠物的反应模式（被动、防御、侵略）
 * - 自动施放技能（增益、治疗、攻击法术）
 * - 管理友方单位列表（用于有益法术的目标选择）
 *
 * 宠物AI适用于：
 * - 猎人的宠物
 * - 术士的恶魔
 * - 死亡骑士的食尸鬼
 * - 法师的水元素
 * - 各类临时守护者和图腾
 */

#ifndef TRINITY_PETAI_H
#define TRINITY_PETAI_H

#include "CreatureAI.h"
#include "Timer.h"

class Creature;
class Spell;

/// 目标-法术列表类型定义，用于存储目标和施放法术的配对
typedef std::vector<std::pair<Unit*, Spell*>> TargetSpellList;

/**
 * @brief 宠物AI类
 *
 * 宠物AI负责控制宠物、守护者、图腾和某些被控制生物的行为。
 * 主要功能包括：
 * - 自动攻击和跟随主人
 * - 响应主人的攻击和被攻击事件
 * - 管理友方单位列表
 * - 处理宠物的移动和返回行为
 */
class TC_GAME_API PetAI : public CreatureAI
{
    public:
        /**
         * @brief 检查该AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 如果适用返回正值，否则返回0或负值
         */
        static int32 Permissible(Creature const* creature);

        /**
         * @brief 构造函数
         * @param creature 拥有此AI的生物
         */
        explicit PetAI(Creature* creature);

        /**
         * @brief 主更新函数，每帧调用
         * @param diff 自上次更新以来经过的时间（毫秒）
         */
        void UpdateAI(uint32) override;

        /**
         * @brief 当宠物杀死一个单位时调用
         * @param victim 被杀死的单位
         */
        void KilledUnit(Unit* /*victim*/) override;

        /**
         * @brief 开始攻击目标（仅在未攻击其他目标时）
         * @param target 要攻击的目标
         * @note 只有在当前没有攻击其他目标时才会开始攻击
         */
        void AttackStart(Unit* target) override;

        /**
         * @brief 强制开始攻击目标
         * @param target 要攻击的目标
         * @note 总是尝试开始攻击，忽略当前是否正在攻击其他目标
         */
        void _AttackStart(Unit* target);

        /**
         * @brief 移动通知回调
         * @param type 移动类型
         * @param id 移动标识符
         */
        void MovementInform(uint32 type, uint32 id) override;

        /**
         * @brief 当主人被攻击时调用
         * @param attacker 攻击主人的单位
         */
        void OwnerAttackedBy(Unit* attacker) override;

        /**
         * @brief 当主人攻击某目标时调用
         * @param target 主人攻击的目标
         */
        void OwnerAttacked(Unit* target) override;

        /**
         * @brief 当宠物受到伤害时调用
         * @param attacker 造成伤害的攻击者
         * @param damage 伤害值（可修改）
         * @param damageType 伤害类型
         * @param spellInfo 造成伤害的法术信息
         */
        void DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override { AttackStart(attacker); }

        /**
         * @brief 接收表情时调用
         * @param player 发送表情的玩家
         * @param textEmote 表情类型ID
         */
        void ReceiveEmote(Player* player, uint32 textEmote) override;

        /**
         * @brief 刚进入战斗时调用
         * @param who 使宠物进入战斗的单位
         */
        void JustEnteredCombat(Unit* who) override { EngagementStart(who); }

        /**
         * @brief 刚退出战斗时调用
         */
        void JustExitedCombat() override { EngagementOver(); }

        /**
         * @brief 当宠物的被控制状态改变时调用
         * @param isNew 是否是新进入被控制状态
         */
        void OnCharmed(bool isNew) override;

        // 以下函数不被PetAI使用，但需要定义以覆盖
        // CreatureAI的默认函数，避免干扰PetAI的行为

        /**
         * @brief 视线内移动检测（空实现）
         * @param who 进入视线范围的单位
         * @note CreatureAI的默认实现会干扰返回中的宠物
         */
        void MoveInLineOfSight(Unit* /*who*/) override { } // CreatureAI interferes with returning pets

        /**
         * @brief 安全的视线内移动检测（空实现）
         * @param who 进入视线范围的单位
         */
        void MoveInLineOfSight_Safe(Unit* /*who*/) { } // CreatureAI interferes with returning pets

        /**
         * @brief 宠物出现时调用（空实现）
         * @note 跟随行为由PetAI手动控制
         */
        void JustAppeared() override { } // we will control following manually

        /**
         * @brief 进入逃避模式（空实现）
         * @param why 逃避原因
         * @note 宠物不使用这种逃避机制来处理逃跑行为
         */
        void EnterEvadeMode(EvadeReason /*why*/) override { } // For fleeing, pets don't use this type of Evade mechanic

    private:
        /**
         * @brief 检查是否需要停止当前攻击
         * @return 如果需要停止攻击返回true
         */
        bool NeedToStop();

        /**
         * @brief 停止攻击并重置相关状态
         */
        void StopAttack();

        /**
         * @brief 更新友方单位列表
         */
        void UpdateAllies();

        /**
         * @brief 选择下一个攻击目标
         * @param allowAutoSelect 是否允许自动选择目标
         * @return 选中的目标单位，如果没有合适目标返回nullptr
         */
        Unit* SelectNextTarget(bool allowAutoSelect) const;

        /**
         * @brief 处理返回主人的移动逻辑
         */
        void HandleReturnMovement();

        /**
         * @brief 执行攻击动作
         * @param target 攻击目标
         * @param chase 是否追击目标
         */
        void DoAttack(Unit* target, bool chase);

        /**
         * @brief 检查是否可以攻击指定目标
         * @param target 要检查的目标
         * @return 如果可以攻击返回true
         */
        bool CanAttack(Unit* target);

        /**
         * @brief 快速清除所有CharmInfo标志
         * @note 将所有标志设置为FALSE
         */
        void ClearCharmInfoFlags();

        /// 时间追踪器，用于控制更新间隔
        TimeTracker _tracker;
        /// 友方单位GUID集合，存储宠物认为是盟友的单位
        GuidSet _allySet;
        /// 更新友方列表的定时器（毫秒）
        uint32 _updateAlliesTimer;
};

#endif
