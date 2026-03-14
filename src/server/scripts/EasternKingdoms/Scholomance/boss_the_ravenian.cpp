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
 * @file boss_the_ravenian.cpp
 * @brief 通灵学院BOSS拉文尼亚战斗脚本
 *
 * 本模块实现了通灵学院BOSS拉文尼亚的战斗AI：
 * - 拉文尼亚（巨大的亡灵战士）
 * - 践踏、劈砍、分裂劈砍和击退技能
 *
 * 战斗机制：
 * 1. 践踏：对周围敌人造成物理伤害
 * 2. 劈砍：对前方敌人造成物理伤害
 * 3. 分裂劈砍：强力的劈砍攻击，降低护甲
 * 4. 击退：击退当前目标并降低仇恨
 *
 * 完成度：100%
 * 备注：拉文尼亚是通灵学院6个小BOSS之一
 */

#include "scholomance.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义拉文尼亚使用的所有法术ID
 */
enum Spells
{
    SPELL_TRAMPLE                   = 15550,  // 践踏 - 对周围敌人造成物理伤害
    SPELL_CLEAVE                    = 20691,  // 劈砍 - 对前方敌人造成物理伤害
    SPELL_SUNDERINCLEAVE            = 25174,  // 分裂劈砍 - 劈砍并降低护甲
    SPELL_KNOCKAWAY                 = 10101   // 击退 - 击退目标并降低仇恨
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_TRAMPLE                   = 1,      // 践踏事件
    EVENT_CLEAVE                    = 2,      // 劈砍事件
    EVENT_SUNDERINCLEAVE            = 3,      // 分裂劈砍事件
    EVENT_KNOCKAWAY                 = 4       // 击退事件
};

/**
 * @brief 拉文尼亚脚本类
 *
 * 实现拉文尼亚的战斗AI，包括：
 * - 践踏技能（范围物理伤害）
 * - 劈砍技能（前方物理伤害）
 * - 分裂劈砍技能（护甲削减）
 * - 击退技能（仇恨管理）
 */
class boss_the_ravenian : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_the_ravenian() : CreatureScript("boss_the_ravenian") { }

        /**
         * @brief 拉文尼亚AI结构体
         *
         * 实现拉文尼亚的战斗AI逻辑，继承自BossAI
         */
        struct boss_theravenianAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_theravenianAI(Creature* creature) : BossAI(creature, DATA_THE_RAVENIAN) { }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当拉文尼亚进入战斗时：
             * - 安排践踏技能（24秒后）
             * - 安排劈砍技能（15秒后）
             * - 安排分裂劈砍技能（40秒后）
             * - 安排击退技能（32秒后）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_TRAMPLE, 24s);
                events.ScheduleEvent(EVENT_CLEAVE, 15s);
                events.ScheduleEvent(EVENT_SUNDERINCLEAVE, 40s);
                events.ScheduleEvent(EVENT_KNOCKAWAY, 32s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件（践踏、劈砍、分裂劈砍、击退）
             * - 进行近战攻击
             *
             * 调用时机：每帧由核心代码调用
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())  // 没有战斗目标则返回
                    return;

                events.Update(diff);  // 更新事件计时器

                if (me->HasUnitState(UNIT_STATE_CASTING))  // 正在施法则等待
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_TRAMPLE:
                            // 对当前目标施放践踏
                            DoCastVictim(SPELL_TRAMPLE, true);
                            events.ScheduleEvent(EVENT_TRAMPLE, 10s);  // 10秒后再次施放
                            break;
                        case EVENT_CLEAVE:
                            // 对当前目标施放劈砍
                            DoCastVictim(SPELL_CLEAVE, true);
                            events.ScheduleEvent(EVENT_CLEAVE, 7s);  // 7秒后再次施放
                            break;
                        case EVENT_SUNDERINCLEAVE:
                            // 对当前目标施放分裂劈砍
                            DoCastVictim(SPELL_SUNDERINCLEAVE, true);
                            events.ScheduleEvent(EVENT_SUNDERINCLEAVE, 20s);  // 20秒后再次施放
                            break;
                        case EVENT_KNOCKAWAY:
                            // 对当前目标施放击退
                            DoCastVictim(SPELL_KNOCKAWAY, true);
                            events.ScheduleEvent(EVENT_KNOCKAWAY, 12s);  // 12秒后再次施放
                            break;
                        default:
                            break;
                    }

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
            }
        };

        /**
         * @brief 获取AI
         * @param creature 生物对象指针
         * @return AI对象指针
         *
         * 创建并返回拉文尼亚AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_theravenianAI>(creature);
        }
};

/**
 * @brief 注册拉文尼亚脚本
 *
 * 将拉文尼亚脚本注册到脚本系统中
 */
void AddSC_boss_theravenian()
{
    new boss_the_ravenian();
}
