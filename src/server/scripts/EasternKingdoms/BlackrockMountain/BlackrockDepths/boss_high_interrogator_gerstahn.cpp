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
 * @file boss_high_interrogator_gerstahn.cpp
 * @brief 黑石深渊副本BOSS：高阶审讯官格斯塔恩(High Interrogator Gerstahn)的AI脚本实现
 *
 * 高阶审讯官格斯塔恩是黑石深渊副本中的一个BOSS，位于监狱区域。
 * 她是一个牧师型BOSS，具有以下特点：
 * - 使用暗言术：痛持续伤害目标
 * - 使用法力燃烧消耗目标的法力值
 * - 使用心灵尖吼恐惧周围敌人
 * - 使用暗影护盾保护自己
 *
 * 战斗难点在于处理法力燃烧和心灵尖吼的影响，对法系职业尤其具有威胁。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义高阶审讯官格斯塔恩使用的法术ID
 */
enum Spells
{
    SPELL_SHADOWWORDPAIN                                   = 14032,  // 暗言术：痛 - 持续暗影伤害
    SPELL_MANABURN                                         = 14033,  // 法力燃烧 - 消耗目标法力并造成伤害
    SPELL_PSYCHICSCREAM                                    = 13704,  // 心灵尖吼 - 恐惧周围敌人
    SPELL_SHADOWSHIELD                                     = 12040   // 暗影护盾 - 反弹近战伤害
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_SHADOW_WORD_PAIN                                 = 1,  // 暗言术：痛事件
    EVENT_MANABURN                                         = 2,  // 法力燃烧事件
    EVENT_PSYCHIC_SCREAM                                   = 3,  // 心灵尖吼事件
    EVENT_SHADOWSHIELD                                     = 4   // 暗影护盾事件
};

/**
 * @brief 高阶审讯官格斯塔恩BOSS脚本类
 *
 * 继承自CreatureScript，为高阶审讯官格斯塔恩提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为和技能释放。
 * 格斯塔恩是一个中等难度的BOSS，主要威胁在于法力燃烧和心灵尖吼。
 */
class boss_high_interrogator_gerstahn : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_high_interrogator_gerstahn"
         */
        boss_high_interrogator_gerstahn() : CreatureScript("boss_high_interrogator_gerstahn") { }

        /**
         * @brief 高阶审讯官格斯塔恩AI结构体
         *
         * 继承自ScriptedAI，实现高阶审讯官格斯塔恩的战斗AI逻辑。
         * 该AI控制BOSS的技能释放时机和目标选择。
         */
        struct boss_high_interrogator_gerstahnAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类
             */
            boss_high_interrogator_gerstahnAI(Creature* creature) : ScriptedAI(creature) { }

            /**
             * @brief 重置AI状态
             *
             * 当BOSS脱离战斗或重置时调用。
             * 清空所有已调度的事件，使BOSS恢复初始状态。
             *
             * 调用时机：BOSS脱离战斗、团灭重置、实例重置时
             */
            void Reset() override
            {
                _events.Reset();  // 重置事件映射表
            }

            /**
             * @brief 进入战斗事件处理
             * @param who 进入战斗的目标单位（当前未使用）
             *
             * 当BOSS进入战斗状态时调用。
             * 调度所有技能的初始释放时间。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                _events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 4s);   // 4秒后首次施放暗言术：痛
                _events.ScheduleEvent(EVENT_MANABURN, 14s);          // 14秒后首次施放法力燃烧
                _events.ScheduleEvent(EVENT_PSYCHIC_SCREAM, 32s);    // 32秒后首次施放心灵尖吼
                _events.ScheduleEvent(EVENT_SHADOWSHIELD, 8s);       // 8秒后首次施放暗影护盾
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 每个游戏循环周期调用一次，处理战斗逻辑。
             * 包括检查战斗状态、更新事件计时器、执行技能释放。
             *
             * 调用时机：每个游戏Tick（约每50毫秒）
             * 性能注意事项：频繁调用，需要优化处理逻辑
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有有效的攻击目标，则不执行任何操作
                if (!UpdateVictim())
                    return;

                // 更新事件计时器
                _events.Update(diff);

                // 执行所有到期的事件
                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SHADOW_WORD_PAIN:
                            // 随机选择一个有法力值的目标施放暗言术：痛
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                                DoCast(target, SPELL_SHADOWWORDPAIN);
                            _events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 7s);  // 7秒后再次施放
                            break;
                        case EVENT_PSYCHIC_SCREAM:
                            // 对当前目标施放心灵尖吼，恐惧周围敌人
                            DoCastVictim(SPELL_PSYCHICSCREAM);
                            _events.ScheduleEvent(EVENT_PSYCHIC_SCREAM, 30s);  // 30秒后再次施放
                            break;
                        case EVENT_MANABURN:
                            // 随机选择一个有法力值的目标施放法力燃烧
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                                DoCast(target, SPELL_MANABURN);
                            _events.ScheduleEvent(EVENT_MANABURN, 10s);  // 10秒后再次施放
                            break;
                        case EVENT_SHADOWSHIELD:
                            // 对自己施放暗影护盾，反弹近战伤害
                            DoCast(me, SPELL_SHADOWSHIELD);
                            _events.ScheduleEvent(EVENT_SHADOWSHIELD, 25s);  // 25秒后再次施放
                            break;
                        default:
                            break;
                    }
                }

                // 如果技能都在冷却中，执行普通攻击
                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;  ///< 事件映射表，用于管理技能释放的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回高阶审讯官格斯塔恩的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_high_interrogator_gerstahnAI>(creature);
        }
};

/**
 * @brief 注册高阶审讯官格斯塔恩脚本
 *
 * 将高阶审讯官格斯塔恩BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_high_interrogator_gerstahn()
{
    new boss_high_interrogator_gerstahn();
}
