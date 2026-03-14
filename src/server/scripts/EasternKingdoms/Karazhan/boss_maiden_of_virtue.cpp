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
 * @file boss_maiden_of_virtue.cpp
 * @brief 卡拉赞副本 - 虚伪圣女(Maiden of Virtue)BOSS脚本模块
 *
 * 本模块实现了虚伪圣女BOSS的战斗逻辑，包括:
 * - 虚伪圣女BOSS AI，管理战斗阶段和技能释放
 * - 周期性施放忏悔、神圣之火、神圣愤怒等技能
 *
 * 战斗机制:
 * - 圣女会施放神圣之地光环，持续对周围玩家造成伤害
 * - 周期性施放忏悔，使所有玩家昏迷
 * - 随机对玩家施放神圣之火和神圣愤怒
 * - 10分钟狂暴时间限制
 */

#include "ScriptMgr.h"
#include "karazhan.h"
#include "ScriptedCreature.h"

/**
 * @brief 虚伪圣女技能ID枚举
 *
 * 定义虚伪圣女使用的所有法术技能ID
 */
enum Spells
{
    SPELL_REPENTANCE    = 29511,  ///< 忏悔：使所有敌人昏迷12秒
    SPELL_HOLYFIRE      = 29522,  ///< 神圣之火：对目标造成神圣伤害并灼烧
    SPELL_HOLYWRATH     = 32445,  ///< 神圣愤怒：对目标及其周围敌人造成连锁神圣伤害
    SPELL_HOLYGROUND    = 29523,  ///< 神圣之地：被动光环，对附近敌人持续造成伤害
    SPELL_BERSERK       = 26662   ///< 狂暴：10分钟后施放，大幅增加伤害
};

/**
 * @brief 虚伪圣女台词枚举
 *
 * 定义虚伪圣女在战斗中各个阶段说的话
 */
enum Yells
{
    SAY_AGGRO           = 0,  ///< 进入战斗台词
    SAY_SLAY            = 1,  ///< 击杀玩家台词
    SAY_REPENTANCE      = 2,  ///< 施放忏悔台词
    SAY_DEATH           = 3   ///< 死亡台词
};

/**
 * @brief 虚伪圣女事件枚举
 *
 * 定义虚伪圣女战斗中的各种事件ID，用于事件调度器
 */
enum Events
{
    EVENT_REPENTANCE    = 1,  ///< 忏悔事件
    EVENT_HOLYFIRE      = 2,  ///< 神圣之火事件
    EVENT_HOLYWRATH     = 3,  ///< 神圣愤怒事件
    EVENT_ENRAGE        = 4   ///< 狂暴事件
};

/**
 * @class boss_maiden_of_virtue
 * @brief 虚伪圣女BOSS脚本类
 *
 * 继承自CreatureScript，用于注册虚伪圣女BOSS的AI脚本
 */
class boss_maiden_of_virtue : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册虚伪圣女脚本名称
     */
    boss_maiden_of_virtue() : CreatureScript("boss_maiden_of_virtue") { }

    /**
     * @struct boss_maiden_of_virtueAI
     * @brief 虚伪圣女BOSS的AI实现
     *
     * 继承自BossAI，实现虚伪圣女的战斗逻辑:
     * - 施放神圣之地光环，持续对周围玩家造成伤害
     * - 周期性施放忏悔，使所有玩家昏迷
     * - 随机对玩家施放神圣之火和神圣愤怒
     * - 10分钟狂暴机制
     */
    struct boss_maiden_of_virtueAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化虚伪圣女AI，设置副本数据ID
         */
        boss_maiden_of_virtueAI(Creature* creature) : BossAI(creature, DATA_MAIDEN_OF_VIRTUE) { }

        /**
         * @brief 击杀单位回调
         * @param Victim 被击杀的单位(未使用)
         *
         * 调用时机: 当虚伪圣女杀死一个单位时
         * 功能: 50%概率播放击杀台词
         */
        void KilledUnit(Unit* /*Victim*/) override
        {
            if (roll_chance_i(50))
                Talk(SAY_SLAY);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 虚伪圣女死亡时
         * 功能: 播放死亡台词，触发死亡事件
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            _JustDied();
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         *
         * 调用时机: 虚伪圣女进入战斗时
         * 功能:
         * - 调用父类的进入战斗方法
         * - 播放战斗开始台词
         * - 施放神圣之地光环
         * - 调度忏悔事件(33-45秒后)
         * - 调度神圣之火事件(8秒后)
         * - 调度神圣愤怒事件(15-25秒后)
         * - 调度狂暴事件(10分钟后)
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);
            Talk(SAY_AGGRO);

            // 立即施放神圣之地光环
            DoCastSelf(SPELL_HOLYGROUND, true);
            events.ScheduleEvent(EVENT_REPENTANCE, 33s, 45s);
            events.ScheduleEvent(EVENT_HOLYFIRE, 8s);
            events.ScheduleEvent(EVENT_HOLYWRATH, 15s, 25s);
            events.ScheduleEvent(EVENT_ENRAGE, 10min);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能:
         * - 检查是否有有效攻击目标
         * - 更新事件调度器
         * - 处理施法状态，避免打断施法
         * - 执行事件并施放相应技能
         * - 进行近战攻击
         *
         * 性能注意: 每帧调用，保持简洁高效
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有有效攻击目标，直接返回
            if (!UpdateVictim())
                return;

            events.Update(diff);

            // 如果正在施法，等待施法完成
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // 执行事件队列中的事件
            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_REPENTANCE:
                        // 对当前目标施放忏悔
                        DoCastVictim(SPELL_REPENTANCE);
                        Talk(SAY_REPENTANCE);
                        events.Repeat(Seconds(35));
                        break;
                    case EVENT_HOLYFIRE:
                        // 随机选择50码内的敌对目标施放神圣之火
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50, true))
                            DoCast(target, SPELL_HOLYFIRE);
                        events.Repeat(Seconds(8), Seconds(19));
                        break;
                    case EVENT_HOLYWRATH:
                        // 随机选择80码内的敌对目标施放神圣愤怒
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80, true))
                            DoCast(target, SPELL_HOLYWRATH);
                        events.Repeat(Seconds(15), Seconds(25));
                        break;
                    case EVENT_ENRAGE:
                        // 10分钟后进入狂暴状态
                        DoCastSelf(SPELL_BERSERK, true);
                        break;
                    default:
                        break;
                }

                // 如果施放了法术，退出循环等待下一帧
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }

            // 没有正在施法的法术，进行近战攻击
            DoMeleeAttackIfReady();
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回虚伪圣女AI实例
     *
     * 调用时机: 服务器创建虚伪圣女生物时
     * 功能: 创建并返回虚伪圣女AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_maiden_of_virtueAI>(creature);
    }
};

/**
 * @brief 注册虚伪圣女BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建虚伪圣女脚本实例，注册到脚本系统
 */
void AddSC_boss_maiden_of_virtue()
{
    new boss_maiden_of_virtue();
}
