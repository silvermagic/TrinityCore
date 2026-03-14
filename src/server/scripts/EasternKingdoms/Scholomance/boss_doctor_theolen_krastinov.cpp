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
 * @file boss_doctor_theolen_krastinov.cpp
 * @brief 通灵学院BOSS医生西奥多·克拉斯提诺夫战斗脚本
 *
 * 本模块实现了通灵学院BOSS医生西奥多·克拉斯提诺夫的战斗AI：
 * - 医生西奥多·克拉斯提诺夫（疯狂的实验医生）
 * - 撕裂、反手一击、狂暴等近战技能
 *
 * 战斗机制：
 * 1. 撕裂：对当前目标施放，造成持续流血伤害
 * 2. 反手一击：对当前目标施放，造成物理伤害
 * 3. 狂暴：提升攻击强度，持续2分钟
 *
 * 特殊机制：
 * - 狂暴技能冷却时间长达120秒
 * - 进入狂暴状态会发出表情提示
 * - 主要依靠近战攻击输出
 *
 * 完成度：100%
 */

#include "ScriptMgr.h"
#include "scholomance.h"
#include "ScriptedCreature.h"

/**
 * @brief 对话文本ID枚举
 *
 * 定义克拉斯提诺夫在战斗中的对话文本ID
 */
enum Say
{
    EMOTE_FRENZY_KILL           = 0,    // 狂暴时的表情提示
};

/**
 * @brief 法术ID枚举
 *
 * 定义克拉斯提诺夫使用的所有法术ID
 */
enum Spells
{
    SPELL_REND                  = 16509,  // 撕裂 - 持续流血伤害
    SPELL_BACKHAND              = 18103,  // 反手一击 - 物理伤害
    SPELL_FRENZY                = 8269    // 狂暴 - 提升攻击强度
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_REND                  = 1,      // 撕裂事件
    EVENT_BACKHAND              = 2,      // 反手一击事件
    EVENT_FRENZY                = 3       // 狂暴事件
};

/**
 * @brief 医生西奥多·克拉斯提诺夫脚本类
 *
 * 实现克拉斯提诺夫的战斗AI，包括：
 * - 撕裂技能（持续流血伤害）
 * - 反手一击（物理伤害）
 * - 狂暴技能（提升攻击强度）
 */
class boss_doctor_theolen_krastinov : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_doctor_theolen_krastinov() : CreatureScript("boss_doctor_theolen_krastinov") { }

        /**
         * @brief 克拉斯提诺夫AI结构体
         *
         * 实现克拉斯提诺夫的战斗AI逻辑，继承自BossAI
         */
        struct boss_theolenkrastinovAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_theolenkrastinovAI(Creature* creature) : BossAI(creature, DATA_DOCTOR_THEOLEN_KRASTINOV) { }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当克拉斯提诺夫进入战斗时：
             * - 安排撕裂技能（8秒后）
             * - 安排反手一击技能（9秒后）
             * - 安排狂暴技能（1秒后，立即狂暴）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_REND, 8s);
                events.ScheduleEvent(EVENT_BACKHAND, 9s);
                events.ScheduleEvent(EVENT_FRENZY, 1s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件（撕裂、反手一击、狂暴）
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
                        case EVENT_REND:
                            // 对当前目标施放撕裂
                            DoCastVictim(SPELL_REND, true);
                            events.ScheduleEvent(EVENT_REND, 10s);  // 10秒后再次施放
                            break;
                        case EVENT_BACKHAND:
                            // 对当前目标施放反手一击
                            DoCastVictim(SPELL_BACKHAND, true);
                            events.ScheduleEvent(EVENT_BACKHAND, 10s);  // 10秒后再次施放
                            break;
                        case EVENT_FRENZY:
                            // 对自己施放狂暴
                            DoCast(me, SPELL_FRENZY, true);
                            Talk(EMOTE_FRENZY_KILL);  // 发出狂暴表情提示
                            events.ScheduleEvent(EVENT_FRENZY, 120s);  // 120秒后再次施放
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
         * 创建并返回克拉斯提诺夫AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_theolenkrastinovAI>(creature);
        }

};

void AddSC_boss_theolenkrastinov()
{
    new boss_doctor_theolen_krastinov();
}
