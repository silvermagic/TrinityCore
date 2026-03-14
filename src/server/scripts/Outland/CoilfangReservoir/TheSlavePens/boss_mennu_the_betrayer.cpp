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
 * @file boss_mennu_the_betrayer.cpp
 * @brief 奴隶围栏副本首领"背叛者门努"的AI脚本实现
 *
 * 模块职责：
 * - 实现背叛者门努的战斗AI逻辑
 * - 管理门努的图腾召唤和技能释放
 * - 处理战斗事件调度和时间控制
 *
 * 首领信息：
 * - 位置：奴隶围栏副本（Coilfang Reservoir - The Slave Pens）
 * - 类型：破碎者种族首领
 * - 难度：普通/英雄模式
 * - 战斗特点：召唤多种图腾辅助战斗，使用闪电箭进行远程攻击
 *
 * ScriptData
 * SDName: boss_mennu_the_betrayer
 * SD%Complete: 95%
 * SDComment:
 * SDCategory: Coilfang Reservoir, The Slave Pens
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "the_slave_pens.h"

/**
 * @brief 首领台词ID枚举
 * 定义门努在不同战斗阶段说的话
 */
enum Say
{
    SAY_AGGRO                       = 0,  ///< 进入战斗时的台词
    SAY_SLAY                        = 1,  ///< 击杀玩家时的台词
    SAY_DEATH                       = 2   ///< 死亡时的台词
};

/**
 * @brief 法术ID枚举
 * 定义门努使用的所有法术技能
 */
enum Spells
{
    SPELL_TAINTED_STONESKIN_TOTEM   = 31985, ///< 污染的石肤图腾 - 每30秒施放一次，仅当生命值低于100%时
    SPELL_TAINTED_EARTHGRAB_TOTEM   = 31981, ///< 污染的地缚图腾 - 定身效果
    SPELL_CORRUPTED_NOVA_TOTEM      = 31991, ///< 腐化新星图腾 - 范围伤害
    SPELL_MENNUS_HEALING_WARD       = 34980, ///< 门努的治疗守卫 - 每14-25秒施放
    SPELL_LIGHTNING_BOLT            = 35010  ///< 闪电箭 - 每14-19秒施放，对目标造成自然伤害
};

/**
 * @brief 事件ID枚举
 * 定义用于事件调度器的事件类型
 */
enum Events
{
    EVENT_TAINTED_STONESKIN_TOTEM   = 1,  ///< 污染石肤图腾事件
    EVENT_TAINTED_EARTHGRAB_TOTEM   = 2,  ///< 污染地缚图腾事件
    EVENT_CORRUPTED_NOVA_TOTEM      = 3,  ///< 腐化新星图腾事件
    EVENT_MENNUS_HEALING_WARD       = 4,  ///< 门努治疗守卫事件
    EVENT_LIGHTNING_BOLT            = 5   ///< 闪电箭事件
};

/**
 * @struct boss_mennu_the_betrayer
 * @brief 背叛者门努的AI结构体
 *
 * 继承自BossAI基类，实现门努的完整战斗逻辑。
 * 门努是一名破碎者萨满祭司型首领，擅长使用各种图腾和闪电箭进行战斗。
 */
struct boss_mennu_the_betrayer : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化基类BossAI，并设置首领数据ID为DATA_MENNU_THE_BETRAYER
     */
    boss_mennu_the_betrayer(Creature* creature) : BossAI(creature, DATA_MENNU_THE_BETRAYER) { }

    /**
     * @brief 重置函数
     *
     * 当首领脱离战斗或重置时调用。
     * 调用基类的_Reset()方法清理战斗状态、事件调度器和召唤物。
     *
     * 调用时机：
     * - 首领重置时
     * - 首领脱离战斗时
     * - 团队重置副本时
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief 死亡处理函数
     * @param killer 击杀者（未使用）
     *
     * 当门努死亡时调用，执行以下操作：
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
     * @brief 进入战斗处理函数
     * @param who 触发战斗的单位
     *
     * 当门努被玩家攻击并进入战斗状态时调用。
     * 设置所有技能事件的初始调度时间：
     * - 污染石肤图腾：30秒后首次施放
     * - 污染地缚图腾：20秒后首次施放
     * - 腐化新星图腾：1分钟后首次施放
     * - 门努治疗守卫：14-25秒后随机首次施放
     * - 闪电箭：14-19秒后随机首次施放
     *
     * 调用时机：首领首次进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_TAINTED_STONESKIN_TOTEM, 30s);
        events.ScheduleEvent(EVENT_TAINTED_EARTHGRAB_TOTEM, 20s);
        events.ScheduleEvent(EVENT_CORRUPTED_NOVA_TOTEM, 1min);
        events.ScheduleEvent(EVENT_MENNUS_HEALING_WARD, 14s, 25s);
        events.ScheduleEvent(EVENT_LIGHTNING_BOLT, 14s, 19s);
        Talk(SAY_AGGRO);
    }

    /**
     * @brief 击杀单位处理函数
     * @param victim 被击杀的单位（未使用）
     *
     * 当门努杀死一个玩家时调用，播放击杀台词。
     *
     * 调用时机：门努杀死玩家单位时
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_SLAY);
    }

    /**
     * @brief AI更新函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理门努的主要战斗逻辑。
     *
     * 处理流程：
     * 1. 检查是否有有效的战斗目标，如果没有则返回
     * 2. 更新事件调度器时间
     * 3. 如果正在施法则暂停处理事件
     * 4. 循环处理所有到期的事件：
     *    - EVENT_TAINTED_STONESKIN_TOTEM: 当生命值低于100%时施放石肤图腾
     *    - EVENT_TAINTED_EARTHGRAB_TOTEM: 施放地缚图腾
     *    - EVENT_CORRUPTED_NOVA_TOTEM: 施放新星图腾
     *    - EVENT_MENNUS_HEALING_WARD: 施放治疗守卫
     *    - EVENT_LIGHTNING_BOLT: 对当前目标施放闪电箭
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
                case EVENT_TAINTED_STONESKIN_TOTEM:
                    // 仅当生命值低于100%时才施放石肤图腾
                    if (HealthBelowPct(100))
                        DoCast(me, SPELL_TAINTED_STONESKIN_TOTEM);
                    // 重新调度事件，30秒后再次施放
                    events.ScheduleEvent(EVENT_TAINTED_STONESKIN_TOTEM, 30s);
                    break;
                case EVENT_TAINTED_EARTHGRAB_TOTEM:
                    // 施放地缚图腾，用于定身玩家
                    DoCast(me, SPELL_TAINTED_EARTHGRAB_TOTEM);
                    break;
                case EVENT_CORRUPTED_NOVA_TOTEM:
                    // 施放新星图腾，造成范围伤害
                    DoCast(me, SPELL_CORRUPTED_NOVA_TOTEM);
                    break;
                case EVENT_MENNUS_HEALING_WARD:
                    // 施放治疗守卫，为自己恢复生命值
                    DoCast(me, SPELL_MENNUS_HEALING_WARD);
                    // 重新调度事件，14-25秒后随机再次施放
                    events.ScheduleEvent(EVENT_MENNUS_HEALING_WARD, 14s, 25s);
                    break;
                case EVENT_LIGHTNING_BOLT:
                    // 对当前仇恨目标施放闪电箭（强制施法，不触发GCD）
                    DoCastVictim(SPELL_LIGHTNING_BOLT, true);
                    // 重新调度事件，14-25秒后再次施放
                    // 注意：注释中写的是14-19秒，但代码使用的是14-25秒
                    events.ScheduleEvent(EVENT_LIGHTNING_BOLT, 14s, 25s);
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
};

/**
 * @brief 注册脚本函数
 *
 * 这是脚本的入口点函数，用于将门努的AI注册到脚本系统中。
 * 当服务器启动时，脚本系统会调用此函数来注册所有首领AI。
 *
 * 调用时机：服务器启动时，在脚本初始化阶段
 */
void AddSC_boss_mennu_the_betrayer()
{
    // 使用宏注册门努的AI到奴隶围栏副本脚本系统
    RegisterSlavePensCreatureAI(boss_mennu_the_betrayer);
}
