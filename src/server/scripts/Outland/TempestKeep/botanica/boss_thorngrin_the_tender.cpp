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
 * @file boss_thorngrin_the_tender.cpp
 * @brief 生态船副本首领"索林格林神行者"的AI脚本实现
 *
 * 模块职责：
 * - 实现索林格林神行者的战斗AI逻辑
 * - 管理献祭、地狱火和激怒技能的施放
 * - 处理生命值阈值触发的阶段转换台词
 *
 * 首领信息：
 * - 位置：生态船副本（Tempest Keep - The Botanica）
 * - 类型：植物人首领
 * - 难度：普通/英雄模式
 * - 战斗特点：献祭玩家、施放地狱火、周期性激怒
 *
 * 战斗机制：
 * - 献祭：随机选择非坦克玩家，使其无法移动并持续受到伤害
 * - 地狱火：对当前目标造成范围火焰伤害
 * - 激怒：提高攻击速度和伤害
 * - 生命值阶段：在50%和20%时播放特定台词
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "the_botanica.h"

/**
 * @brief 台词枚举
 * 定义索林格林神行者在不同战斗阶段说的话
 */
enum Says
{
    SAY_AGGRO                   = 0,  ///< 进入战斗时的台词
    SAY_20_PERCENT_HP           = 1,  ///< 生命值低于20%时的台词
    SAY_KILL                    = 2,  ///< 击杀玩家时的台词
    SAY_CAST_SACRIFICE          = 3,  ///< 施放献祭时的台词
    SAY_50_PERCENT_HP           = 4,  ///< 生命值低于50%时的台词
    SAY_CAST_HELLFIRE           = 5,  ///< 施放地狱火时的台词
    SAY_DEATH                   = 6,  ///< 死亡时的台词
    EMOTE_ENRAGE                = 7   ///< 激怒表情
};

/**
 * @brief 法术枚举
 * 定义索林格林神行者使用的所有法术技能
 */
enum Spells
{
    SPELL_SACRIFICE             = 34661, ///< 献祭 - 禁锢目标并持续造成伤害
    SPELL_HELLFIRE              = 34659, ///< 地狱火 - 对目标和周围单位造成火焰伤害
    SPELL_ENRAGE                = 34670  ///< 激怒 - 提高攻击速度和伤害
};

/**
 * @brief 事件枚举
 * 定义用于事件调度器的事件类型
 */
enum Events
{
    EVENT_SACRIFICE             = 1,  ///< 献祭事件
    EVENT_HELLFIRE              = 2,  ///< 地狱火事件
    EVENT_ENRAGE                = 3   ///< 激怒事件
};

/**
 * @class boss_thorngrin_the_tender
 * @brief 索林格林神行者脚本类
 *
 * 继承自CreatureScript，为索林格林神行者提供脚本注册和AI获取功能。
 * 使用传统的CreatureScript模式而非BossAI宏，实现更细粒度的控制。
 */
class boss_thorngrin_the_tender : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化生物脚本，设置脚本名称为"thorngrin_the_tender"。
         */
        boss_thorngrin_the_tender() : CreatureScript("thorngrin_the_tender") { }

        /**
         * @struct boss_thorngrin_the_tenderAI
         * @brief 索林格林神行者AI结构体
         *
         * 继承自BossAI基类，实现索林格林神行者的完整战斗逻辑。
         * 包含献祭、地狱火、激怒三个主要技能，以及生命值阈值触发的阶段转换。
         *
         * 战斗流程：
         * 1. 进入战斗后开始调度所有技能
         * 2. 献祭随机非坦克玩家
         * 3. 地狱火攻击当前目标
         * 4. 周期性激怒提升战斗力
         * 5. 生命值低于50%和20%时播放阶段转换台词
         */
        struct boss_thorngrin_the_tenderAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 初始化基类BossAI，设置首领数据ID为DATA_THORNGRIN_THE_TENDER，
             * 并调用Initialize()初始化阶段标志。
             */
            boss_thorngrin_the_tenderAI(Creature* creature) : BossAI(creature, DATA_THORNGRIN_THE_TENDER)
            {
                Initialize();
            }

            /**
             * @brief 初始化函数
             *
             * 初始化阶段标志，用于控制生命值阈值触发的台词。
             * - _phase1: 控制生命值低于50%时的台词（仅触发一次）
             * - _phase2: 控制生命值低于20%时的台词（仅触发一次）
             */
            void Initialize()
            {
                _phase1 = true;  // 50%生命值阶段标志
                _phase2 = true;  // 20%生命值阶段标志
            }

            /**
             * @brief 重置函数
             *
             * 当首领脱离战斗或重置时调用。
             * 执行以下操作：
             * 1. 调用基类_Reset方法清理战斗状态
             * 2. 重新初始化阶段标志
             *
             * 调用时机：
             * - 首领重置时
             * - 首领脱离战斗时
             */
            void Reset() override
            {
                _Reset();
                Initialize();
            }

            /**
             * @brief 进入战斗处理函数
             * @param who 触发战斗的单位
             *
             * 当索林格林神行者进入战斗状态时调用。
             * 设置所有技能事件的初始调度时间：
             * - 献祭：5.7秒后首次施放，之后每29.4秒施放一次
             * - 地狱火：普通模式18秒后首次施放，英雄模式17.4-19.3秒后首次施放
             * - 激怒：12秒后首次施放，之后每33秒施放一次
             *
             * 调用时机：首领首次进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_AGGRO);  // 播放进入战斗台词
                events.ScheduleEvent(EVENT_SACRIFICE, 5700ms);  // 献祭首次调度

                // 地狱火首次调度：英雄模式时间范围随机，普通模式固定18秒
                if (IsHeroic())
                    events.ScheduleEvent(EVENT_HELLFIRE, 17400ms, 19300ms);
                else
                    events.ScheduleEvent(EVENT_HELLFIRE, 18s);

                events.ScheduleEvent(EVENT_ENRAGE, 12s);  // 激怒首次调度
            }

            /**
             * @brief 击杀单位处理函数
             * @param victim 被击杀的单位（未使用）
             *
             * 当索林格林神行者杀死一个玩家时调用，播放击杀台词。
             *
             * 调用时机：首领杀死玩家单位时
             */
            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_KILL);
            }

            /**
             * @brief 死亡处理函数
             * @param killer 击杀者（未使用）
             *
             * 当索林格林神行者死亡时调用，执行以下操作：
             * 1. 调用基类JustDied处理标准死亡逻辑（如战利品生成）
             * 2. 播放死亡台词
             *
             * 调用时机：首领先命值降为0时
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                Talk(SAY_DEATH);
            }

            /**
             * @brief 受到伤害处理函数
             * @param killer 造成伤害的单位（未使用）
             * @param damage 伤害值（引用，可能被修改）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当索林格林神行者受到伤害时调用。
             * 检查伤害后的生命值百分比，在特定阈值时播放台词。
             *
             * 处理的阶段：
             * - 生命值低于50%：播放SAY_50_PERCENT_HP台词（仅一次）
             * - 生命值低于20%：播放SAY_20_PERCENT_HP台词（仅一次）
             *
             * 使用HealthBelowPctDamaged确保台词在正确的伤害阈值时触发，
             * 而不是简单地在生命值低于阈值时触发。
             *
             * 调用时机：首领受到伤害时
             *
             * 性能注意事项：
             * - 每次伤害都会调用，但只有简单的布尔检查
             * - 使用标志避免重复触发
             */
            void DamageTaken(Unit* /*killer*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 检查生命值是否在此次伤害后低于50%
                if (me->HealthBelowPctDamaged(50, damage) && _phase1)
                {
                    _phase1 = false;      // 标记已触发，避免重复
                    Talk(SAY_50_PERCENT_HP);
                }
                // 检查生命值是否在此次伤害后低于20%
                if (me->HealthBelowPctDamaged(20, damage) && _phase2)
                {
                    _phase2 = false;      // 标记已触发，避免重复
                    Talk(SAY_20_PERCENT_HP);
                }
            }

            /**
             * @brief AI更新函数
             * @param diff 自上次更新以来经过的时间（毫秒）
             *
             * 每个服务器tick调用一次，处理索林格林神行者的主要战斗逻辑。
             *
             * 处理流程：
             * 1. 检查是否有有效的战斗目标
             * 2. 更新事件调度器时间
             * 3. 如果正在施法则暂停处理事件
             * 4. 循环处理所有到期的事件：
             *    - EVENT_SACRIFICE: 献祭随机非坦克玩家
             *    - EVENT_HELLFIRE: 对当前目标施放地狱火
             *    - EVENT_ENRAGE: 施放激怒提升战斗力
             * 5. 如果施法中则退出循环
             * 6. 如果没有施法则进行近战攻击
             *
             * 性能注意事项：
             * - 该函数每帧都会被调用，应避免复杂计算
             * - 使用事件调度器优化技能施放时机
             */
            void UpdateAI(uint32 diff) override
            {
                // 检查是否有有效的攻击目标
                if (!UpdateVictim())
                    return;

                // 更新事件调度器
                events.Update(diff);

                // 如果正在施法，等待施法完成再处理下一个事件
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理所有到期的事件
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SACRIFICE:
                            // 选择随机非坦克玩家（距离不限，必须是玩家）
                            // SelectTargetMethod::Random: 随机选择
                            // 1: 跳过第一个目标（坦克）
                            // 0.0f: 距离限制（0表示无限制）
                            // true: 必须是玩家
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                            {
                                Talk(SAY_CAST_SACRIFICE);  // 播放献祭台词
                                DoCast(target, SPELL_SACRIFICE, true);  // 强制施放，不触发GCD
                            }
                            // 重新调度事件，29.4秒后再次施放
                            events.ScheduleEvent(EVENT_SACRIFICE, 29400ms);
                            break;
                        case EVENT_HELLFIRE:
                            Talk(SAY_CAST_HELLFIRE);  // 播放地狱火台词
                            DoCastVictim(SPELL_HELLFIRE, true);  // 对当前目标施放地狱火
                            // 重新调度事件：英雄模式随机时间，普通模式固定时间
                            if (IsHeroic())
                                events.ScheduleEvent(EVENT_HELLFIRE, 17400ms, 19300ms);
                            else
                                events.ScheduleEvent(EVENT_HELLFIRE, 18s);
                            break;
                        case EVENT_ENRAGE:
                            Talk(EMOTE_ENRAGE);  // 播放激怒表情
                            DoCast(me, SPELL_ENRAGE);  // 对自己施放激怒
                            // 重新调度事件，33秒后再次施放
                            events.ScheduleEvent(EVENT_ENRAGE, 33s);
                            break;
                        default:
                            break;
                    }

                    // 如果施法状态改变，退出事件处理循环
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 如果没有在施法且目标在近战范围内，进行近战攻击
                DoMeleeAttackIfReady();
            }

        private:
            bool _phase1;  ///< 50%生命值阶段标志，控制台词仅触发一次
            bool _phase2;  ///< 20%生命值阶段标志，控制台词仅触发一次
        };

        /**
         * @brief 获取AI函数
         * @param creature 生物对象指针
         * @return 索林格林神行者AI实例
         *
         * 创建并返回索林格林神行者的AI实例。
         * 使用GetBotanicaAI模板函数确保正确的类型转换。
         *
         * 调用时机：当生物需要获取其AI时
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBotanicaAI<boss_thorngrin_the_tenderAI>(creature);
        }
};

/**
 * @brief 注册脚本函数
 *
 * 这是脚本的入口点函数，用于将索林格林神行者的脚本注册到脚本系统中。
 * 当服务器启动时，脚本系统会调用此函数来注册首领脚本。
 *
 * 调用时机：服务器启动时，在脚本初始化阶段
 */
void AddSC_boss_thorngrin_the_tender()
{
    new boss_thorngrin_the_tender();
}
