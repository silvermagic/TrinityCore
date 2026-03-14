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
 * @file boss_quartermaster_zigris.cpp
 * @brief 黑石塔 - 军需官兹格雷斯 BOSS 脚本
 *
 * 本模块实现了黑石塔下层副本中 BOSS 军需官兹格雷斯的 AI 行为。
 * 兹格雷斯是一名巨魔军需官,位于黑石塔下层的军械库区域。
 *
 * 主要功能:
 * - 射击: 远程物理攻击
 * - 眩晕炸弹: 使周围敌人昏迷
 *
 * 特殊说明:
 * 兹格雷斯主要是一个远程攻击者,会使用射击和眩晕炸弹来控制战场。
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 军需官兹格雷斯使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_SHOOT                     = 16496,    ///< 射击 - 远程物理攻击
    SPELL_STUNBOMB                  = 16497,    ///< 眩晕炸弹 - 使周围敌人昏迷
    SPELL_HEALING_POTION            = 15504,    ///< 治疗药水 - 恢复生命值(定义了但未使用)
    SPELL_HOOKEDNET                 = 15609     ///< 钩网 - 定身目标(定义了但未使用)
};

/**
 * @brief BOSS 战斗事件 ID 枚举
 */
enum Events
{
    EVENT_SHOOT                     = 1,        ///< 射击事件
    EVENT_STUN_BOMB                 = 2         ///< 眩晕炸弹事件
};

/**
 * @brief 军需官兹格雷斯 BOSS AI 结构体
 *
 * 继承自 BossAI,实现了军需官兹格雷斯的战斗逻辑。
 * 兹格雷斯是一名远程攻击者,会频繁使用射击技能。
 */
struct quartermaster_zigris : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    quartermaster_zigris(Creature* creature) : BossAI(creature, DATA_QUARTERMASTER_ZIGRIS) { }

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
     * - 射击: 1秒后首次施放
     * - 眩晕炸弹: 16秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_SHOOT, 1s);
        events.ScheduleEvent(EVENT_STUN_BOMB, 16s);
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
     * - 射击技能的冷却时间非常短(500毫秒),这意味着兹格雷斯会频繁射击
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
                case EVENT_SHOOT:
                    // 对当前目标施放射击
                    DoCastVictim(SPELL_SHOOT);
                    // 0.5秒后再次施放(非常短的冷却时间)
                    events.ScheduleEvent(EVENT_SHOOT, 500ms);
                    break;
                case EVENT_STUN_BOMB:
                    // 对当前目标施放眩晕炸弹
                    DoCastVictim(SPELL_STUNBOMB);
                    // 14秒后再次施放
                    events.ScheduleEvent(EVENT_STUN_BOMB, 14s);
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
 * 将军需官兹格雷斯的 AI 注册到脚本系统中。
 */
void AddSC_boss_quatermasterzigris()
{
    RegisterBlackrockSpireCreatureAI(quartermaster_zigris);
}
