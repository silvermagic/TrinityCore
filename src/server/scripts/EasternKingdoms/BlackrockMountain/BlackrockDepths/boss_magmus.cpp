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
 * @file boss_magmus.cpp
 * @brief 黑石深渊副本BOSS：马格姆斯(Magmus)的AI脚本实现
 *
 * 马格姆斯是黑石深渊副本中的一个大型火焰元素BOSS，守卫在铁厅区域。
 * 他具有以下特点：
 * - 使用烈焰冲击对目标造成火焰伤害
 * - 血量低于50%时开始使用战争践踏
 * - 死亡后开启通往大帝宝座的门
 * - 铁厅的熔岩守护者在战斗开始时也会激活
 *
 * 马格姆斯是通往最终BOSS达格兰·索瑞森大帝的必经之路。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义马格姆斯使用的法术ID
 */
enum Spells
{
    SPELL_FIERYBURST        = 13900,  // 烈焰冲击 - 对目标造成火焰伤害
    SPELL_WARSTOMP          = 24375   // 战争践踏 - 对周围敌人造成伤害并眩晕
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_FIERY_BURST       = 1,  // 烈焰冲击事件
    EVENT_WARSTOMP          = 2   // 战争践踏事件
};

/**
 * @brief 战斗阶段枚举
 * 定义马格姆斯战斗的不同阶段
 */
enum Phases
{
    PHASE_ONE               = 1,  // 第一阶段（100%-50%血量）
    PHASE_TWO               = 2   // 第二阶段（50%-0%血量）
};

/**
 * @brief 马格姆斯BOSS脚本类
 *
 * 继承自CreatureScript，为马格姆斯提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为、技能释放和副本进度更新。
 * 马格姆斯是一个中等难度的BOSS，主要挑战在于处理第二阶段的战争践踏。
 */
class boss_magmus : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_magmus"
         */
        boss_magmus() : CreatureScript("boss_magmus") { }

        /**
         * @brief 马格姆斯AI结构体
         *
         * 继承自ScriptedAI，实现马格姆斯的战斗AI逻辑。
         * 该AI控制BOSS的技能释放时机和副本进度更新。
         */
        struct boss_magmusAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类
             */
            boss_magmusAI(Creature* creature) : ScriptedAI(creature) { }

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
             * 设置铁厅区域为进行中状态，设置战斗阶段，并调度技能释放。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                // 设置铁厅区域为进行中状态
                if (InstanceScript* instance = me->GetInstanceScript())
                    instance->SetData(TYPE_IRON_HALL, IN_PROGRESS);

                _events.SetPhase(PHASE_ONE);  // 设置为第一阶段
                _events.ScheduleEvent(EVENT_FIERY_BURST, 5s);  // 5秒后首次施放烈焰冲击
            }

            /**
             * @brief 受到伤害事件处理
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当BOSS受到伤害时调用，用于检测血量阈值并触发阶段转换。
             * 血量低于50%时进入第二阶段，开始使用战争践踏。
             *
             * 调用时机：BOSS受到伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 血量低于50%且还在第一阶段时，进入第二阶段
                if (me->HealthBelowPctDamaged(50, damage) && _events.IsInPhase(PHASE_ONE))
                {
                    _events.SetPhase(PHASE_TWO);  // 进入第二阶段
                    _events.ScheduleEvent(EVENT_WARSTOMP, 0s, 0, PHASE_TWO);  // 立即施放战争践踏
                }
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
                        case EVENT_FIERY_BURST:
                            // 施放烈焰冲击
                            DoCastVictim(SPELL_FIERYBURST);
                            _events.ScheduleEvent(EVENT_FIERY_BURST, 6s);  // 6秒后再次施放
                            break;
                        case EVENT_WARSTOMP:
                            // 施放战争践踏
                            DoCastVictim(SPELL_WARSTOMP);
                            _events.ScheduleEvent(EVENT_WARSTOMP, 8s, 0, PHASE_TWO);  // 8秒后再次施放
                            break;
                        default:
                            break;
                    }
                }

                // 如果技能都在冷却中，执行普通攻击
                DoMeleeAttackIfReady();
            }

            /**
             * @brief 死亡事件处理
             * @param killer 击杀者（当前未使用）
             *
             * 当BOSS死亡时调用。
             * 开启通往大帝宝座的门，并标记铁厅区域为已完成。
             *
             * 调用时机：BOSS死亡时
             */
            void JustDied(Unit* /*killer*/) override
            {
                if (InstanceScript* instance = me->GetInstanceScript())
                {
                    // 开启通往大帝宝座的门
                    instance->HandleGameObject(instance->GetGuidData(DATA_THRONE_DOOR), true);
                    // 标记铁厅区域为已完成
                    instance->SetData(TYPE_IRON_HALL, DONE);
                }
            }

        private:
            EventMap _events;  ///< 事件映射表，用于管理技能释放的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回马格姆斯的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_magmusAI>(creature);
        }
};

/**
 * @brief 铁手守护者相关枚举
 * 定义铁手守护者使用的事件和法术
 */
enum IronhandGuardian
{
    EVENT_GOUTOFFLAME = 1,      // 火焰喷射事件
    SPELL_GOUTOFFLAME = 15529   // 火焰喷射 - 范围火焰伤害
};

/**
 * @brief 铁手守护者NPC脚本类
 *
 * 继承自CreatureScript，为铁手守护者提供AI逻辑支持。
 * 铁手守护者是马格姆斯BOSS战中的辅助小怪，在战斗开始后激活。
 * 它们会周期性地施放火焰喷射技能，增加战斗难度。
 */
class npc_ironhand_guardian : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     * 初始化NPC脚本，注册脚本名称为"npc_ironhand_guardian"
     */
    npc_ironhand_guardian() : CreatureScript("npc_ironhand_guardian") { }

    /**
     * @brief 铁手守护者AI结构体
     *
     * 继承自ScriptedAI，实现铁手守护者的AI逻辑。
     * 守护者在马格姆斯战斗开始后激活，并持续施放火焰喷射。
     */
    struct npc_ironhand_guardianAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_ironhand_guardianAI(Creature* creature) : ScriptedAI(creature)
        {
            _instance = me->GetInstanceScript();  // 获取副本实例脚本
            _active = false;  // 初始化为未激活状态
        }

        /**
         * @brief 重置AI状态
         *
         * 重置事件计时器。
         */
        void Reset() override
        {
            _events.Reset();
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 监测铁厅区域的状态，一旦战斗开始则激活守护者。
         * 激活后周期性地施放火焰喷射技能。
         *
         * 调用时机：每个游戏Tick
         * 性能注意事项：守护者激活后会持续施法，需要合理控制施法频率
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果还未激活
            if (!_active)
            {
                // 检查铁厅区域状态
                if (_instance->GetData(TYPE_IRON_HALL) == NOT_STARTED)
                    return;  // 未开始则不执行任何操作

                // 一旦BOSS战斗开始，守护者会保持激活状态直到下次副本重置
                _events.ScheduleEvent(EVENT_GOUTOFFLAME, 0s, 10s);  // 0-10秒后首次施放火焰喷射
                _active = true;  // 标记为已激活
            }

            // 更新事件计时器
            _events.Update(diff);

            // 执行所有到期的事件
            while (uint32 eventId = _events.ExecuteEvent())
            {
                if (eventId == EVENT_GOUTOFFLAME)
                {
                    // 施放火焰喷射（AOE技能）
                    DoCastAOE(SPELL_GOUTOFFLAME);
                    _events.Repeat(16s, 21s);  // 16-21秒后再次施放
                }
            }
        }

    private:
        EventMap _events;          ///< 事件映射表，用于管理技能释放的计时和调度
        InstanceScript* _instance; ///< 副本实例脚本指针，用于访问副本数据
        bool _active;              ///< 是否已激活标志
    };

    /**
     * @brief 获取AI实例
     * @param creature 需要获取AI的生物对象
     * @return 返回铁手守护者的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_ironhand_guardianAI>(creature);
    }
};

/**
 * @brief 注册马格姆斯脚本
 *
 * 将马格姆斯BOSS脚本和铁手守护者NPC脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_magmus()
{
    new boss_magmus();              // 马格姆斯BOSS
    new npc_ironhand_guardian();    // 铁手守护者
}
