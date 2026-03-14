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
 * @file boss_warmaster_voone.cpp
 * @brief 黑石塔 - 军需官沃恩 BOSS 脚本
 *
 * 本模块实现了黑石塔下层副本中 BOSS 军需官沃恩的 AI 行为。
 * 沃恩是一名巨魔战士,位于黑石塔下层的军械库区域。
 *
 * 主要功能:
 * - 快速踢击: 使目标昏迷
 * - 顺劈斩: 对前方敌人造成物理伤害
 * - 上勾拳: 强力的近战攻击
 * - 致死打击: 对目标造成重伤,降低治疗效果
 * - 拳击: 打断施法
 * - 投掷斧: 远程攻击
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 军需官沃恩使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_SNAPKICK                  = 15618,    ///< 快速踢击 - 使目标昏迷
    SPELL_CLEAVE                    = 15284,    ///< 顺劈斩 - 对前方敌人造成物理伤害
    SPELL_UPPERCUT                  = 10966,    ///< 上勾拳 - 强力的近战攻击
    SPELL_MORTALSTRIKE              = 16856,    ///< 致死打击 - 造成重伤并降低治疗效果
    SPELL_PUMMEL                    = 15615,    ///< 拳击 - 打断施法
    SPELL_THROWAXE                  = 16075     ///< 投掷斧 - 远程攻击
};

/**
 * @brief BOSS 战斗事件 ID 枚举
 */
enum Events
{
    EVENT_SNAP_KICK                 = 1,        ///< 快速踢击事件
    EVENT_CLEAVE                    = 2,        ///< 顺劈斩事件
    EVENT_UPPERCUT                  = 3,        ///< 上勾拳事件
    EVENT_MORTAL_STRIKE             = 4,        ///< 致死打击事件
    EVENT_PUMMEL                    = 5,        ///< 拳击事件
    EVENT_THROW_AXE                 = 6         ///< 投掷斧事件
};

/**
 * @brief 军需官沃恩 BOSS AI 结构体
 *
 * 继承自 BossAI,实现了军需官沃恩的战斗逻辑。
 * 沃恩是一名多才多艺的战士,会使用多种近战和远程技能。
 */
struct boss_warmaster_voone : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_warmaster_voone(Creature* creature) : BossAI(creature, DATA_WARMASTER_VOONE) { }

    /**
     * @brief 重置 BOSS 状态
     *
     * 当 BOSS 脱离战斗或重置时调用。
     * 清除所有事件计时器,重置战斗状态。
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief 进入战斗
     * @param who 仇恨目标
     *
     * 当 BOSS 进入战斗时调用。
     * 初始化所有技能的施放计时器。
     *
     * 技能施放时机:
     * - 快速踢击: 8秒后首次施放
     * - 顺劈斩: 14秒后首次施放
     * - 上勾拳: 20秒后首次施放
     * - 致死打击: 12秒后首次施放
     * - 拳击: 32秒后首次施放
     * - 投掷斧: 1秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_SNAP_KICK, 8s);
        events.ScheduleEvent(EVENT_CLEAVE, 14s);
        events.ScheduleEvent(EVENT_UPPERCUT, 20s);
        events.ScheduleEvent(EVENT_MORTAL_STRIKE, 12s);
        events.ScheduleEvent(EVENT_PUMMEL, 32s);
        events.ScheduleEvent(EVENT_THROW_AXE, 1s);
    }

    /**
     * @brief 死亡处理
     * @param killer 击杀者(未使用)
     *
     * 当 BOSS 死亡时调用。
     * 清理战斗状态和事件。
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 每个游戏循环周期调用,处理 BOSS 的战斗行为。
     *
     * 执行流程:
     * 1. 检查是否有有效的战斗目标
     * 2. 更新事件计时器
     * 3. 如果正在施法则等待
     * 4. 处理到期的技能事件
     * 5. 执行近战攻击
     *
     * 性能注意事项:
     * - 使用事件系统管理技能冷却,避免频繁的时间计算
     * - 施法状态检查确保不会打断正在施放的法术
     * - 注意:拳击事件处理中使用了错误的 ID(EVENT_MORTAL_STRIKE),这可能是一个 bug
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SNAP_KICK:
                    // 对当前目标施放快速踢击
                    DoCastVictim(SPELL_SNAPKICK);
                    // 6秒后再次施放
                    events.ScheduleEvent(EVENT_SNAP_KICK, 6s);
                    break;
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    // 12秒后再次施放
                    events.ScheduleEvent(EVENT_CLEAVE, 12s);
                    break;
                case EVENT_UPPERCUT:
                    // 对当前目标施放上勾拳
                    DoCastVictim(SPELL_UPPERCUT);
                    // 14秒后再次施放
                    events.ScheduleEvent(EVENT_UPPERCUT, 14s);
                    break;
                case EVENT_MORTAL_STRIKE:
                    // 对当前目标施放致死打击
                    DoCastVictim(SPELL_MORTALSTRIKE);
                    // 10秒后再次施放
                    events.ScheduleEvent(EVENT_MORTAL_STRIKE, 10s);
                    break;
                case EVENT_PUMMEL:
                    // 对当前目标施放拳击
                    DoCastVictim(SPELL_PUMMEL);
                    // 注意:这里使用了错误的 ID,应该是 EVENT_PUMMEL 而不是 EVENT_MORTAL_STRIKE
                    // 这是一个已知的 bug,会导致拳击冷却时间错误
                    events.ScheduleEvent(EVENT_MORTAL_STRIKE, 16s);
                    break;
                case EVENT_THROW_AXE:
                    // 对当前目标施放投掷斧
                    DoCastVictim(SPELL_THROWAXE);
                    // 8秒后再次施放
                    events.ScheduleEvent(EVENT_THROW_AXE, 8s);
                    break;
            }

            // 如果开始施法,则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册 BOSS 脚本
 *
 * 将军需官沃恩的 AI 注册到脚本系统中。
 */
void AddSC_boss_warmastervoone()
{
    RegisterBlackrockSpireCreatureAI(boss_warmaster_voone);
}
