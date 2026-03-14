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
 * @file boss_ambassador_flamelash.cpp
 * @brief 黑石深渊副本BOSS：大使弗莱拉斯(Ambassador Flamelash)的AI脚本实现
 *
 * 大使弗莱拉斯是黑石深渊副本中的一个BOSS，位于暗影熔炉附近。
 * 他是一个火焰元素BOSS，主要技能包括火焰冲击和召唤火焰之灵。
 * 战斗中他会周期性地召唤火焰之灵协助战斗，增加战斗难度。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义大使弗莱拉斯使用的法术ID
 */
enum Spells
{
    SPELL_FIREBLAST                                        = 15573   // 火焰冲击 - 对目标造成火焰伤害
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_FIREBLAST                                        = 1,  // 火焰冲击事件
    EVENT_SUMMON_SPIRITS                                   = 2   // 召唤火焰之灵事件
};

/**
 * @brief 大使弗莱拉斯BOSS脚本类
 *
 * 继承自CreatureScript，为大使弗莱拉斯提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为、技能释放和小怪召唤。
 * 大使弗莱拉斯是一个中等难度的BOSS，主要挑战在于处理他召唤的火焰之灵。
 */
class boss_ambassador_flamelash : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_ambassador_flamelash"
         */
        boss_ambassador_flamelash() : CreatureScript("boss_ambassador_flamelash") { }

        /**
         * @brief 大使弗莱拉斯AI结构体
         *
         * 继承自ScriptedAI，实现大使弗莱拉斯的战斗AI逻辑。
         * 该AI控制BOSS的技能释放时机和火焰之灵的召唤。
         */
        struct boss_ambassador_flamelashAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类
             */
            boss_ambassador_flamelashAI(Creature* creature) : ScriptedAI(creature) { }

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
                _events.Reset();
            }

            /**
             * @brief 进入战斗事件处理
             * @param who 进入战斗的目标单位（当前未使用）
             *
             * 当BOSS进入战斗状态时调用。
             * 调度火焰冲击和召唤火焰之灵事件的初始释放时间。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                _events.ScheduleEvent(EVENT_FIREBLAST, 2s);      // 2秒后首次施放火焰冲击
                _events.ScheduleEvent(EVENT_SUMMON_SPIRITS, 24s); // 24秒后首次召唤火焰之灵
            }

            /**
             * @brief 召唤火焰之灵
             * @param victim 火焰之灵的攻击目标
             *
             * 在BOSS周围随机位置召唤一只火焰之灵（Creature ID: 9178）。
             * 火焰之灵会在60秒后自动消失或被击杀后消失。
             * 召唤位置为BOSS周围-9到9范围内的随机偏移。
             *
             * 调用时机：召唤火焰之灵事件触发时
             * 性能注意事项：每次召唤一个生物，需要合理控制召唤频率
             */
            void SummonSpirit(Unit* victim)
            {
                // 在BOSS周围随机位置召唤火焰之灵
                if (Creature* spirit = DoSpawnCreature(9178, frand(-9, 9), frand(-9, 9), 0, 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 60s))
                    spirit->AI()->AttackStart(victim);  // 火焰之灵立即攻击目标
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
                        case EVENT_FIREBLAST:
                            // 施放火焰冲击，对当前目标造成火焰伤害
                            DoCastVictim(SPELL_FIREBLAST);
                            _events.ScheduleEvent(EVENT_FIREBLAST, 7s);  // 7秒后再次施放
                            break;
                        case EVENT_SUMMON_SPIRITS:
                            // 召唤4只火焰之灵协助战斗
                            for (uint32 i = 0; i < 4; ++i)
                                SummonSpirit(me->GetVictim());
                            _events.ScheduleEvent(EVENT_SUMMON_SPIRITS, 30s);  // 30秒后再次召唤
                            break;
                        default:
                            break;
                    }
                }

                // 如果技能都在冷却中，执行普通攻击
                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;  ///< 事件映射表，用于管理技能释放和召唤的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回大使弗莱拉斯的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_ambassador_flamelashAI>(creature);
        }
};

/**
 * @brief 注册大使弗莱拉斯脚本
 *
 * 将大使弗莱拉斯BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_ambassador_flamelash()
{
    new boss_ambassador_flamelash();
}
