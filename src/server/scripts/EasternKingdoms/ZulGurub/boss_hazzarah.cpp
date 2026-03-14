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
 * @file boss_hazzarah.cpp
 * @brief 祖尔格拉布副本 - 哈扎拉(Boss Hazzarah)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中疯狂边缘Boss哈扎拉的AI逻辑：
 * - 梦魇之龙,隐藏Boss之一
 * - 主要技能: 法力燃烧、睡眠、召唤幻象
 * - 特殊机制: 召唤3个幻象攻击随机玩家
 */

#include "zulgurub.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_MANABURN = 26046,     /**< 法力燃烧 - 燃烧目标的法力值并造成伤害 */
    SPELL_SLEEP = 24664         /**< 睡眠 - 使目标进入睡眠状态 */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_MANABURN = 1,         /**< 法力燃烧事件 */
    EVENT_SLEEP = 2,            /**< 睡眠事件 */
    EVENT_ILLUSIONS = 3         /**< 召唤幻象事件 */
};

/**
 * @brief 哈扎拉Boss AI
 *
 * 实现哈扎拉的战斗逻辑,包括:
 * - 法力燃烧: 对当前目标施放,燃烧法力并造成伤害
 * - 睡眠: 使当前目标进入睡眠
 * - 召唤幻象: 召唤3个幻象攻击随机玩家
 */
struct boss_hazzarah : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_hazzarah(Creature* creature) : BossAI(creature, DATA_EDGE_OF_MADNESS) { }

    /**
     * @brief 重置Boss状态
     *
     * 调用父类的重置方法,清除所有事件
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 调用父类的死亡方法,更新副本进度
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 安排各技能事件:
     * - 法力燃烧: 4-10秒后首次施放
     * - 睡眠: 10-18秒后首次施放
     * - 召唤幻象: 10-18秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_MANABURN, 4s, 10s);
        events.ScheduleEvent(EVENT_SLEEP, 10s, 18s);
        events.ScheduleEvent(EVENT_ILLUSIONS, 10s, 18s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 法力燃烧: 对当前目标施放,8-16秒后再次施放
     * - 睡眠: 对当前目标施放,12-20秒后再次施放
     * - 召唤幻象: 召唤3个幻象攻击随机玩家,15-25秒后再次施放
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法,不执行其他操作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_MANABURN:
                    // 对当前目标施放法力燃烧
                    DoCastVictim(SPELL_MANABURN, true);
                    events.ScheduleEvent(EVENT_MANABURN, 8s, 16s);
                    break;

                case EVENT_SLEEP:
                    // 对当前目标施放睡眠
                    DoCastVictim(SPELL_SLEEP, true);
                    events.ScheduleEvent(EVENT_SLEEP, 12s, 20s);
                    break;

                case EVENT_ILLUSIONS:
                    // 召唤3个幻象,每个幻象会出现在一个随机玩家身边并攻击该玩家
                    // 目前只使用一个模型
                    for (uint8 i = 0; i < 3; ++i)
                    {
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.f, true))
                            if (TempSummon* illusion = me->SummonCreature(NPC_NIGHTMARE_ILLUSION, target->GetPosition(), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30s))
                                illusion->AI()->AttackStart(target);
                    }
                    events.ScheduleEvent(EVENT_ILLUSIONS, 15s, 25s);
                    break;

                default:
                    break;
            }

            // 如果正在施法,退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

void AddSC_boss_hazzarah()
{
    RegisterZulGurubCreatureAI(boss_hazzarah);
}
