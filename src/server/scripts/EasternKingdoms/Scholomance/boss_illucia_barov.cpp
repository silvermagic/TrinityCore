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
 * @file boss_illucia_barov.cpp
 * @brief 通灵学院BOSS伊露希亚·巴罗夫战斗脚本
 *
 * 本模块实现了通灵学院BOSS伊露希亚·巴罗夫的战斗AI：
 * - 伊露希亚·巴罗夫（巴罗夫家族成员之一，女性）
 * - 诅咒、暗影震击、沉默、恐惧等暗影系技能
 *
 * 战斗机制：
 * 1. 痛苦诅咒：对当前目标施放，造成持续暗影伤害
 * 2. 暗影震击：对随机目标施放，造成暗影伤害
 * 3. 沉默：对当前目标施放，使其无法施法
 * 4. 恐惧：对当前目标施放，使其恐惧逃跑
 *
 * 特殊机制：
 * - 统御技能（SPELL_DOMINATE）尚未实现，仅用于文档记录
 * - 技能施放间隔较短，需要合理分配治疗和打断
 *
 * 完成度：100%
 */

#include "ScriptMgr.h"
#include "scholomance.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义伊露希亚·巴罗夫使用的所有法术ID
 */
enum Spells
{
    SPELL_CURSEOFAGONY          = 18671,  // 痛苦诅咒 - 持续暗影伤害
    SPELL_DOMINATE              = 7645,   // 统御 - 尚未使用，仅用于文档记录
    SPELL_FEAR                  = 12542,  // 恐惧 - 使目标恐惧逃跑
    SPELL_SHADOWSHOCK           = 17234,  // 暗影震击 - 暗影伤害
    SPELL_SILENCE               = 12528   // 沉默 - 使目标无法施法
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_CURSEOFAGONY          = 1,      // 痛苦诅咒事件
    EVENT_SHADOWSHOCK           = 2,      // 暗影震击事件
    EVENT_SILENCE               = 3,      // 沉默事件
    EVENT_FEAR                  = 4       // 恐惧事件
};

/**
 * @brief 伊露希亚·巴罗夫脚本类
 *
 * 实现伊露希亚的战斗AI，包括：
 * - 痛苦诅咒（持续暗影伤害）
 * - 暗影震击（随机目标暗影伤害）
 * - 沉默（打断施法）
 * - 恐惧（控制技能）
 */
class boss_illucia_barov : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_illucia_barov() : CreatureScript("boss_illucia_barov") { }

        /**
         * @brief 伊露希亚·巴罗夫AI结构体
         *
         * 实现伊露希亚的战斗AI逻辑，继承自BossAI
         */
        struct boss_illuciabarovAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_illuciabarovAI(Creature* creature) : BossAI(creature, DATA_LADY_ILLUCIA_BAROV) { }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当伊露希亚进入战斗时：
             * - 安排痛苦诅咒技能（18秒后）
             * - 安排暗影震击技能（9秒后）
             * - 安排沉默技能（5秒后）
             * - 安排恐惧技能（30秒后）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_CURSEOFAGONY, 18s);
                events.ScheduleEvent(EVENT_SHADOWSHOCK, 9s);
                events.ScheduleEvent(EVENT_SILENCE, 5s);
                events.ScheduleEvent(EVENT_FEAR, 30s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件（痛苦诅咒、暗影震击、沉默、恐惧）
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
                        case EVENT_CURSEOFAGONY:
                            // 对当前目标施放痛苦诅咒
                            DoCastVictim(SPELL_CURSEOFAGONY, true);
                            events.ScheduleEvent(EVENT_CURSEOFAGONY, 30s);  // 30秒后再次施放
                            break;
                        case EVENT_SHADOWSHOCK:
                            // 对100码内的随机目标施放暗影震击
                            DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_SHADOWSHOCK, true);
                            events.ScheduleEvent(EVENT_SHADOWSHOCK, 12s);  // 12秒后再次施放
                            break;
                        case EVENT_SILENCE:
                            // 对当前目标施放沉默
                            DoCastVictim(SPELL_SILENCE, true);
                            events.ScheduleEvent(EVENT_SILENCE, 14s);  // 14秒后再次施放
                            break;
                        case EVENT_FEAR:
                            // 对当前目标施放恐惧
                            DoCastVictim(SPELL_FEAR, true);
                            events.ScheduleEvent(EVENT_FEAR, 30s);  // 30秒后再次施放
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
         * 创建并返回伊露希亚AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_illuciabarovAI>(creature);
        }
};

void AddSC_boss_illuciabarov()
{
    new boss_illucia_barov();
}
