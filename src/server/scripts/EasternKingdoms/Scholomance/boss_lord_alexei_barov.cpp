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
 * @file boss_lord_alexei_barov.cpp
 * @brief 通灵学院BOSS阿莱克斯·巴罗夫领主战斗脚本
 *
 * 本模块实现了通灵学院BOSS阿莱克斯·巴罗夫领主的战斗AI：
 * - 阿莱克斯·巴罗夫领主（巴罗夫家族成员之一）
 * - 献祭和暗影帷幕技能
 * - 邪恶光环被动效果
 *
 * 战斗机制：
 * 1. 献祭：对随机目标施放，造成持续火焰伤害
 * 2. 暗影帷幕：对当前目标施放，降低暗影抗性
 * 3. 邪恶光环：被动效果，对周围敌人造成暗影伤害
 *
 * 特殊机制：
 * - 邪恶光环由数据库定义，无需脚本施放
 * - 技能施放需要检查施法状态避免打断
 *
 * 完成度：100%
 * 备注：邪恶光环由数据库应用/定义
 */

#include "scholomance.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义阿莱克斯·巴罗夫领主使用的所有法术ID
 */
enum Spells
{
    SPELL_IMMOLATE                  = 20294,  // 献祭 - 持续火焰伤害
    SPELL_VEILOFSHADOW              = 17820,  // 暗影帷幕 - 降低暗影抗性
    SPELL_UNHOLY_AURA               = 17467   // 邪恶光环 - 被动暗影伤害光环
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_IMMOLATE                  = 1,      // 献祭事件
    EVENT_VEILOFSHADOW              = 2       // 暗影帷幕事件
};

/**
 * @brief 阿莱克斯·巴罗夫领主脚本类
 *
 * 实现阿莱克斯领主的战斗AI，包括：
 * - 献祭技能（随机目标持续火焰伤害）
 * - 暗影帷幕技能（降低目标暗影抗性）
 * - 邪恶光环被动效果
 */
class boss_lord_alexei_barov : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_lord_alexei_barov() : CreatureScript("boss_lord_alexei_barov") { }

        /**
         * @brief 阿莱克斯·巴罗夫领主AI结构体
         *
         * 实现阿莱克斯领主的战斗AI逻辑，继承自BossAI
         */
        struct boss_lordalexeibarovAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_lordalexeibarovAI(Creature* creature) : BossAI(creature, DATA_LORD_ALEXEI_BAROV) { }

            /**
             * @brief 重置AI状态
             *
             * 当战斗重置时调用：
             * - 重置BOSS状态
             * - 确保邪恶光环存在
             *
             * 调用时机：战斗重置或BOSS脱离战斗时
             */
            void Reset() override
            {
                _Reset();

                // 如果没有邪恶光环，则施放
                if (!me->HasAura(SPELL_UNHOLY_AURA))
                    DoCast(me, SPELL_UNHOLY_AURA);
            }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当阿莱克斯领主进入战斗时：
             * - 安排献祭技能（7秒后）
             * - 安排暗影帷幕技能（15秒后）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_IMMOLATE, 7s);
                events.ScheduleEvent(EVENT_VEILOFSHADOW, 15s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件（献祭、暗影帷幕）
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
                        case EVENT_IMMOLATE:
                            // 对100码内的随机目标施放献祭
                            DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_IMMOLATE, true);
                            events.ScheduleEvent(EVENT_IMMOLATE, 12s);  // 12秒后再次施放
                            break;
                        case EVENT_VEILOFSHADOW:
                            // 对当前目标施放暗影帷幕
                            DoCastVictim(SPELL_VEILOFSHADOW, true);
                            events.ScheduleEvent(EVENT_VEILOFSHADOW, 20s);  // 20秒后再次施放
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
         * 创建并返回阿莱克斯领主AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_lordalexeibarovAI>(creature);
        }
};

void AddSC_boss_lordalexeibarov()
{
    new boss_lord_alexei_barov();
}
