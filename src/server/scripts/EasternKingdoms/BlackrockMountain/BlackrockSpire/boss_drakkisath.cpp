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
 * @file boss_drakkisath.cpp
 * @brief 黑石尖塔副本BOSS：达基萨斯将军(General Drakkisath)的AI脚本实现
 *
 * 达基萨斯将军是黑石尖塔（上）的最终BOSS，位于黑石尖塔的最深处。
 * 他是一个强大的龙人战士，掌握火焰和雷电的力量。
 * 击败他是进入黑翼之巢的关键步骤之一。
 *
 * BOSS特点：
 * - 使用火焰新星、顺劈斩、火焰冲击和雷霆一击等技能
 * - 战斗强度较高，需要团队配合
 * - 掉落通往黑翼之巢的关键道具
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义达基萨斯将军使用的所有法术ID
 */
enum Spells
{
    SPELL_FIRENOVA                  = 23462,  // 火焰新星 - 对周围敌人造成火焰伤害
    SPELL_CLEAVE                    = 20691,  // 顺劈斩 - 对前方敌人造成物理伤害
    SPELL_CONFLIGURATION            = 16805,  // 火焰冲击 - 对目标造成火焰伤害并使其燃烧
    SPELL_THUNDERCLAP               = 15548,  // 雷霆一击 - 对周围敌人造成自然伤害并降低攻击速度
                                              // 注意：ID可能不准确，23931是一个更困难的版本
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_FIRE_NOVA                = 1,       // 火焰新星事件
    EVENT_CLEAVE                   = 2,       // 顺劈斩事件
    EVENT_CONFLIGURATION           = 3,       // 火焰冲击事件
    EVENT_THUNDERCLAP              = 4,       // 雷霆一击事件
};

/**
 * @brief 达基萨斯将军BOSS AI结构体
 *
 * 继承自BossAI，实现达基萨斯将军的战斗AI逻辑。
 * 该AI控制BOSS的法术释放时机和战斗行为。
 */
struct boss_drakkisath : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * 初始化BOSS AI，关联达基萨斯将军的副本数据
     */
    boss_drakkisath(Creature* creature) : BossAI(creature, DATA_GENERAL_DRAKKISATH) { }

    /**
     * @brief 重置AI状态
     *
     * 当BOSS脱离战斗或重置时调用。
     * 调用基类的重置方法，清空所有已调度的事件。
     *
     * 调用时机：BOSS脱离战斗、团灭重置、实例重置时
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief 进入战斗事件处理
     * @param who 进入战斗的目标单位
     *
     * 当BOSS进入战斗状态时调用。
     * 调用基类的进入战斗方法，并调度所有战斗法术的初始释放时间。
     *
     * 调用时机：BOSS被攻击或主动攻击玩家时
     * 性能注意事项：事件调度为轻量级操作，无性能影响
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_FIRE_NOVA, 6s);         // 6秒后首次施放火焰新星
        events.ScheduleEvent(EVENT_CLEAVE, 8s);            // 8秒后首次施放顺劈斩
        events.ScheduleEvent(EVENT_CONFLIGURATION, 15s);   // 15秒后首次施放火焰冲击
        events.ScheduleEvent(EVENT_THUNDERCLAP, 17s);      // 17秒后首次施放雷霆一击
    }

    /**
     * @brief 死亡事件处理
     * @param killer 击杀者单位（当前未使用）
     *
     * 当BOSS死亡时调用基类的死亡处理方法。
     * 标记BOSS状态为已死亡，更新副本进度。
     *
     * 调用时机：BOSS被击杀时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每个游戏循环周期调用一次，处理战斗逻辑。
     * 包括检查战斗状态、更新事件计时器、执行法术释放。
     *
     * 调用时机：每个游戏Tick（约每50毫秒）
     * 性能注意事项：频繁调用，已优化处理逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有有效的攻击目标，则不执行任何操作
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法，则不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FIRE_NOVA:
                    // 施放火焰新星，对周围敌人造成火焰伤害
                    DoCastVictim(SPELL_FIRENOVA);
                    events.ScheduleEvent(EVENT_FIRE_NOVA, 10s);
                    break;
                case EVENT_CLEAVE:
                    // 施放顺劈斩，对前方敌人造成物理伤害
                    DoCastVictim(SPELL_CLEAVE);
                    events.ScheduleEvent(EVENT_CLEAVE, 8s);
                    break;
                case EVENT_CONFLIGURATION:
                    // 施放火焰冲击，对目标造成火焰伤害并使其燃烧
                    DoCastVictim(SPELL_CONFLIGURATION);
                    events.ScheduleEvent(EVENT_CONFLIGURATION, 18s);
                    break;
                case EVENT_THUNDERCLAP:
                    // 施放雷霆一击，对周围敌人造成自然伤害并降低攻击速度
                    DoCastVictim(SPELL_THUNDERCLAP);
                    events.ScheduleEvent(EVENT_THUNDERCLAP, 20s);
                    break;
            }

            // 如果施法后进入施法状态，则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册达基萨斯将军脚本
 *
 * 将达基萨斯将军BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_drakkisath()
{
    RegisterBlackrockSpireCreatureAI(boss_drakkisath);
}
