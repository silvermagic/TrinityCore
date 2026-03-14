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
 * @file boss_gahzranka.cpp
 * @brief 祖尔格拉布副本 - 加兹兰卡(Boss Gahz'ranka)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中隐藏Boss加兹兰卡的AI逻辑：
 * - 水中巨兽,需要使用召唤道具触发
 * - 主要技能: 冰霜吐息、巨型喷泉、猛击
 * - 简单的输出型Boss,无特殊机制
 */

#include "zulgurub.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_FROSTBREATH = 16099,      /**< 冰霜吐息 - 对前方锥形范围造成冰霜伤害 */
    SPELL_MASSIVEGEYSER = 22421,    /**< 巨型喷泉 - 召唤技能(目前未正常工作) */
    SPELL_SLAM = 24326              /**< 猛击 - 对目标造成物理伤害并击退 */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_FROSTBREATH = 1,          /**< 冰霜吐息事件 */
    EVENT_MASSIVEGEYSER = 2,        /**< 巨型喷泉事件 */
    EVENT_SLAM = 3                  /**< 猛击事件 */
};

/**
 * @brief 加兹兰卡Boss AI
 *
 * 实现加兹兰卡的战斗逻辑,技能简单直接:
 * - 冰霜吐息: 对当前目标施放,造成冰霜伤害
 * - 巨型喷泉: 召唤水柱攻击目标
 * - 猛击: 对当前目标造成物理伤害并击退
 */
struct boss_gahzranka : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_gahzranka(Creature* creature) : BossAI(creature, DATA_GAHZRANKA) { }

    /**
     * @brief 重置Boss状态
     *
     * 调用父类的重置方法,清除所有事件和召唤物
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
     * - 冰霜吐息: 8秒后首次施放
     * - 巨型喷泉: 25秒后首次施放
     * - 猛击: 15秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_FROSTBREATH, 8s);
        events.ScheduleEvent(EVENT_MASSIVEGEYSER, 25s);
        events.ScheduleEvent(EVENT_SLAM, 15s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 冰霜吐息: 对当前目标施放,7-11秒后再次施放
     * - 巨型喷泉: 对当前目标施放,22-32秒后再次施放
     * - 猛击: 对当前目标施放,12-20秒后再次施放
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
                case EVENT_FROSTBREATH:
                    // 对当前目标施放冰霜吐息
                    DoCastVictim(SPELL_FROSTBREATH, true);
                    events.ScheduleEvent(EVENT_FROSTBREATH, 7s, 11s);
                    break;

                case EVENT_MASSIVEGEYSER:
                    // 对当前目标施放巨型喷泉
                    DoCastVictim(SPELL_MASSIVEGEYSER, true);
                    events.ScheduleEvent(EVENT_MASSIVEGEYSER, 22s, 32s);
                    break;

                case EVENT_SLAM:
                    // 对当前目标施放猛击
                    DoCastVictim(SPELL_SLAM, true);
                    events.ScheduleEvent(EVENT_SLAM, 12s, 20s);
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

void AddSC_boss_gahzranka()
{
    RegisterZulGurubCreatureAI(boss_gahzranka);
}
