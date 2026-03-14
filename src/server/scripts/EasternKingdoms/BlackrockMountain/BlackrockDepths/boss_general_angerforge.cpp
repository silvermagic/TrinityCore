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
 * @file boss_general_angerforge.cpp
 * @brief 黑石深渊副本BOSS：安格弗将军(General Angerforge)的AI脚本实现
 *
 * 安格弗将军是黑石深渊副本中的一个BOSS，位于锻造区域附近。
 * 他是一个近战型BOSS，具有以下特点：
 * - 使用强力打击、断筋和顺劈斩等近战技能
 * - 血量低于20%时进入第二阶段，召唤医疗兵和援军
 * - 医疗兵只会召唤一次，援军会持续召唤
 *
 * 战斗难点在于处理血量低于20%后不断召唤的援军小怪。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义安格弗将军使用的法术ID
 */
enum Spells
{
    SPELL_MIGHTYBLOW                                       = 14099,  // 强力打击 - 对目标造成大量伤害并击退
    SPELL_HAMSTRING                                        = 9080,   // 断筋 - 降低目标移动速度
    SPELL_CLEAVE                                           = 20691   // 顺劈斩 - 对目标和附近敌人造成伤害
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_MIGHTYBLOW                                       = 1,  // 强力打击事件
    EVENT_HAMSTRING                                        = 2,  // 断筋事件
    EVENT_CLEAVE                                           = 3,  // 顺劈斩事件
    EVENT_MEDIC                                            = 4,  // 召唤医疗兵事件
    EVENT_ADDS                                             = 5   // 召唤援军事件
};

/**
 * @brief 战斗阶段枚举
 * 定义安格弗将军战斗的不同阶段
 */
enum Phases
{
    PHASE_ONE                                              = 1,  // 第一阶段（100%-20%血量）
    PHASE_TWO                                              = 2   // 第二阶段（20%-0%血量）
};

/**
 * @brief 安格弗将军BOSS脚本类
 *
 * 继承自CreatureScript，为安格弗将军提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为、技能释放和小怪召唤。
 * 安格弗将军是一个中等难度的BOSS，主要挑战在于处理第二阶段的援军。
 */
class boss_general_angerforge : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_general_angerforge"
         */
        boss_general_angerforge() : CreatureScript("boss_general_angerforge") { }

        /**
         * @brief 安格弗将军AI结构体
         *
         * 继承自ScriptedAI，实现安格弗将军的战斗AI逻辑。
         * 该AI控制BOSS的技能释放时机和小怪召唤。
         */
        struct boss_general_angerforgeAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类
             */
            boss_general_angerforgeAI(Creature* creature) : ScriptedAI(creature) { }

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
             * 设置战斗阶段为第一阶段，并调度技能释放。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                _events.SetPhase(PHASE_ONE);  // 设置为第一阶段
                _events.ScheduleEvent(EVENT_MIGHTYBLOW, 8s);   // 8秒后首次施放强力打击
                _events.ScheduleEvent(EVENT_HAMSTRING, 12s);   // 12秒后首次施放断筋
                _events.ScheduleEvent(EVENT_CLEAVE, 16s);      // 16秒后首次施放顺劈斩
            }

            /**
             * @brief 受到伤害事件处理
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当BOSS受到伤害时调用，用于检测血量阈值并触发阶段转换。
             * 血量低于20%时进入第二阶段，召唤医疗兵和援军。
             *
             * 调用时机：BOSS受到伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 血量低于20%且还在第一阶段时，进入第二阶段
                if (me->HealthBelowPctDamaged(20, damage) && _events.IsInPhase(PHASE_ONE))
                {
                    _events.SetPhase(PHASE_TWO);  // 进入第二阶段
                    // 立即召唤医疗兵和援军
                    _events.ScheduleEvent(EVENT_MEDIC, 0s, 0, PHASE_TWO);
                    _events.ScheduleEvent(EVENT_ADDS, 0s, 0, PHASE_TWO);
                }
            }

            /**
             * @brief 召唤援军
             * @param victim 援军的攻击目标
             *
             * 在BOSS周围随机位置召唤一只援军（Creature ID: 8901）。
             * 援军会在120秒后自动消失或被击杀后消失。
             *
             * 调用时机：召唤援军事件触发时
             * 性能注意事项：每次召唤一个生物，需要合理控制召唤频率
             */
            void SummonAdd(Unit* victim)
            {
                // 在BOSS周围随机位置召唤援军
                if (Creature* SummonedAdd = DoSpawnCreature(8901, float(irand(-14, 14)), float(irand(-14, 14)), 0, 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s))
                    SummonedAdd->AI()->AttackStart(victim);  // 援军立即攻击目标
            }

            /**
             * @brief 召唤医疗兵
             * @param victim 医疗兵的攻击目标
             *
             * 在BOSS周围随机位置召唤一只医疗兵（Creature ID: 8894）。
             * 医疗兵会在120秒后自动消失或被击杀后消失。
             *
             * 调用时机：召唤医疗兵事件触发时
             * 性能注意事项：每次召唤一个生物，需要合理控制召唤频率
             */
            void SummonMedic(Unit* victim)
            {
                // 在BOSS周围随机位置召唤医疗兵
                if (Creature* SummonedMedic = DoSpawnCreature(8894, float(irand(-9, 9)), float(irand(-9, 9)), 0, 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 120s))
                    SummonedMedic->AI()->AttackStart(victim);  // 医疗兵立即攻击目标
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 每个游戏循环周期调用一次，处理战斗逻辑。
             * 包括检查战斗状态、更新事件计时器、执行技能释放和召唤小怪。
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
                        case EVENT_MIGHTYBLOW:
                            // 施放强力打击
                            DoCastVictim(SPELL_MIGHTYBLOW);
                            _events.ScheduleEvent(EVENT_MIGHTYBLOW, 18s);  // 18秒后再次施放
                            break;
                        case EVENT_HAMSTRING:
                            // 施放断筋
                            DoCastVictim(SPELL_HAMSTRING);
                            _events.ScheduleEvent(EVENT_HAMSTRING, 15s);  // 15秒后再次施放
                            break;
                        case EVENT_CLEAVE:
                            // 施放顺劈斩
                            DoCastVictim(SPELL_CLEAVE);
                            _events.ScheduleEvent(EVENT_CLEAVE, 9s);  // 9秒后再次施放
                            break;
                        case EVENT_MEDIC:
                            // 召唤2只医疗兵（仅在第二阶段召唤一次）
                            for (uint8 i = 0; i < 2; ++i)
                                SummonMedic(me->GetVictim());
                            break;
                        case EVENT_ADDS:
                            // 召唤3只援军
                            for (uint8 i = 0; i < 3; ++i)
                                SummonAdd(me->GetVictim());
                            _events.ScheduleEvent(EVENT_ADDS, 25s, 0, PHASE_TWO);  // 25秒后再次召唤
                            break;
                        default:
                            break;
                    }
                }

                // 如果技能都在冷却中，执行普通攻击
                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;  ///< 事件映射表，用于管理技能释放和小怪召唤的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回安格弗将军的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_general_angerforgeAI>(creature);
        }
};

/**
 * @brief 注册安格弗将军脚本
 *
 * 将安格弗将军BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_general_angerforge()
{
    new boss_general_angerforge();
}
