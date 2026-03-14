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
 * @file boss_instructor_malicia.cpp
 * @brief 通灵学院BOSS讲师玛丽希亚战斗脚本
 *
 * 本模块实现了通灵学院BOSS讲师玛丽希亚的战斗AI：
 * - 讲师玛丽希亚（暗影牧师/治疗者）
 * - 墓穴召唤、腐蚀、快速治疗、恢复、治疗之触等技能
 *
 * 战斗机制：
 * 1. 墓穴召唤：对当前目标施放，召唤墓穴攻击
 * 2. 腐蚀：对随机目标施放，降低护甲并造成持续伤害
 * 3. 恢复：对自己施放，持续恢复生命值
 * 4. 快速治疗：对自己施放，立即恢复生命值（连发3次）
 * 5. 治疗之触：对自己施放，恢复大量生命值（连发3次）
 *
 * 特殊机制：
 * - 快速治疗会连续施放3次，然后进入冷却
 * - 治疗之触也会连续施放3次，然后进入冷却
 * - 需要打断治疗技能以提高击杀效率
 *
 * 完成度：100%
 */

#include "ScriptMgr.h"
#include "scholomance.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义讲师玛丽希亚使用的所有法术ID
 */
enum Spells
{
    SPELL_CALLOFGRAVES          = 17831,  // 墓穴召唤 - 召唤墓穴攻击
    SPELL_CORRUPTION            = 11672,  // 腐蚀 - 降低护甲并造成持续伤害
    SPELL_FLASHHEAL             = 10917,  // 快速治疗 - 立即恢复生命值
    SPELL_RENEW                 = 10929,  // 恢复 - 持续恢复生命值
    SPELL_HEALINGTOUCH          = 9889    // 治疗之触 - 恢复大量生命值
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_CALLOFGRAVES          = 1,      // 墓穴召唤事件
    EVENT_CORRUPTION            = 2,      // 腐蚀事件
    EVENT_FLASHHEAL             = 3,      // 快速治疗事件
    EVENT_RENEW                 = 4,      // 恢复事件
    EVENT_HEALINGTOUCH          = 5       // 治疗之触事件
};

/**
 * @brief 讲师玛丽希亚脚本类
 *
 * 实现玛丽希亚的战斗AI，包括：
 * - 墓穴召唤技能（攻击技能）
 * - 腐蚀技能（减益效果）
 * - 恢复技能（持续治疗）
 * - 快速治疗（连续3次快速治疗）
 * - 治疗之触（连续3次大治疗）
 */
class boss_instructor_malicia : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_instructor_malicia() : CreatureScript("boss_instructor_malicia") { }

        /**
         * @brief 讲师玛丽希亚AI结构体
         *
         * 实现玛丽希亚的战斗AI逻辑，继承自BossAI
         */
        struct boss_instructormaliciaAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 初始化计数器
             */
            boss_instructormaliciaAI(Creature* creature) : BossAI(creature, DATA_INSTRUCTOR_MALICIA)
            {
                Initialize();
            }

            /**
             * @brief 初始化计数器
             *
             * 重置快速治疗和治疗之触的施放计数器
             */
            void Initialize()
            {
                FlashCounter = 0;  // 快速治疗计数器
                TouchCounter = 0;  // 治疗之触计数器
            }

            uint32 FlashCounter;   ///< 快速治疗施放计数器（0-2，共3次）
            uint32 TouchCounter;   ///< 治疗之触施放计数器（0-2，共3次）

            /**
             * @brief 重置AI状态
             *
             * 当战斗重置时调用：
             * - 重置BOSS状态
             * - 重置计数器
             *
             * 调用时机：战斗重置或BOSS脱离战斗时
             */
            void Reset() override
            {
                _Reset();
                Initialize();
            }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当玛丽希亚进入战斗时：
             * - 安排墓穴召唤技能（4秒后）
             * - 安排腐蚀技能（8秒后）
             * - 安排恢复技能（32秒后）
             * - 安排快速治疗技能（38秒后）
             * - 安排治疗之触技能（45秒后）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_CALLOFGRAVES, 4s);
                events.ScheduleEvent(EVENT_CORRUPTION, 8s);
                events.ScheduleEvent(EVENT_RENEW, 32s);
                events.ScheduleEvent(EVENT_FLASHHEAL, 38s);
                events.ScheduleEvent(EVENT_HEALINGTOUCH, 45s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件（墓穴召唤、腐蚀、恢复、快速治疗、治疗之触）
             * - 进行近战攻击
             *
             * 特殊逻辑：
             * - 快速治疗：连续施放3次（5秒间隔），然后冷却30秒
             * - 治疗之触：连续施放3次（5.5秒间隔），然后冷却30秒
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
                        case EVENT_CALLOFGRAVES:
                            // 对当前目标施放墓穴召唤
                            DoCastVictim(SPELL_CALLOFGRAVES, true);
                            events.ScheduleEvent(EVENT_CALLOFGRAVES, 65s);  // 65秒后再次施放
                            break;
                        case EVENT_CORRUPTION:
                            // 对100码内的随机目标施放腐蚀
                            DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_CORRUPTION, true);
                            events.ScheduleEvent(EVENT_CORRUPTION, 24s);  // 24秒后再次施放
                            break;
                        case EVENT_RENEW:
                            // 对自己施放恢复
                            DoCast(me, SPELL_RENEW);
                            events.ScheduleEvent(EVENT_RENEW, 10s);  // 10秒后再次施放
                            break;
                        case EVENT_FLASHHEAL:
                            // 对自己施放快速治疗
                            // 将施放3次快速治疗
                            DoCast(me, SPELL_FLASHHEAL);
                            if (FlashCounter < 2)
                            {
                                // 前2次快速治疗：5秒后再次施放
                                events.ScheduleEvent(EVENT_FLASHHEAL, 5s);
                                ++FlashCounter;
                            }
                            else
                            {
                                // 第3次快速治疗后：重置计数器，30秒后再次开始循环
                                FlashCounter=0;
                                events.ScheduleEvent(EVENT_FLASHHEAL, 30s);
                            }
                            break;
                        case EVENT_HEALINGTOUCH:
                            // 对自己施放治疗之触
                            // 将施放3次治疗之触
                            DoCast(me, SPELL_HEALINGTOUCH);
                            if (TouchCounter < 2)
                            {
                                // 前2次治疗之触：5.5秒后再次施放
                                events.ScheduleEvent(EVENT_HEALINGTOUCH, 5500ms);
                                ++TouchCounter;
                            }
                            else
                            {
                                // 第3次治疗之触后：重置计数器，30秒后再次开始循环
                                TouchCounter=0;
                                events.ScheduleEvent(EVENT_HEALINGTOUCH, 30s);
                            }
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
         * 创建并返回玛丽希亚AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_instructormaliciaAI>(creature);
        }

};

void AddSC_boss_instructormalicia()
{
    new boss_instructor_malicia();
}
