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
 * @file boss_gizrul_the_slavener.cpp
 * @brief 黑石尖塔副本BOSS：奴役者基兹卢尔(Gizrul the Slavener)的AI脚本实现
 *
 * 奴役者基兹卢尔是黑石尖塔（下）的BOSS之一，与哈雷卡尔一同出现。
 * 他是一只巨大的黑狼，在哈雷卡尔死亡后被召唤出来。
 *
 * BOSS特点：
 * - 在哈雷卡尔死亡后才会出现
 * - 使用致命撕咬、感染撕咬和狂暴等技能
 * - 沿固定路径移动，进入战斗后与玩家交战
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 法术ID枚举
 * 定义奴役者基兹卢尔使用的所有法术ID
 */
enum Spells
{
    SPELL_FATAL_BITE                = 16495,  // 致命撕咬 - 造成物理伤害
    SPELL_INFECTED_BITE             = 16128,  // 感染撕咬 - 造成物理伤害并施加疾病效果
    SPELL_FRENZY                    = 8269    // 狂暴 - 增加攻击强度和攻击速度
};

/**
 * @brief 路径ID枚举
 * 定义BOSS移动使用的路径ID
 */
enum Paths
{
    GIZRUL_PATH                     = 402450   // 奴役者基兹卢尔的移动路径
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_FATAL_BITE                = 1,       // 致命撕咬事件
    EVENT_INFECTED_BITE             = 2,       // 感染撕咬事件
    EVENT_FRENZY                    = 3        // 狂暴事件
};

/**
 * @brief 奴役者基兹卢尔BOSS AI结构体
 *
 * 继承自BossAI，实现奴役者基兹卢尔的战斗AI逻辑。
 * 该AI控制BOSS的法术释放时机、移动行为和战斗状态。
 */
struct boss_gizrul_the_slavener : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * 初始化BOSS AI，关联奴役者基兹卢尔的副本数据
     */
    boss_gizrul_the_slavener(Creature* creature) : BossAI(creature, DATA_GIZRUL_THE_SLAVENER) { }

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
     * @brief 被召唤事件处理
     * @param summoner 召唤者对象（当前未使用）
     *
     * 当BOSS被召唤时（哈雷卡尔死亡后），立即沿固定路径移动。
     * 这使得BOSS会主动接近玩家。
     *
     * 调用时机：BOSS被召唤出来时
     */
    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        // 沿预定义路径移动，不重复
        me->GetMotionMaster()->MovePath(GIZRUL_PATH, false);
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
        events.ScheduleEvent(EVENT_FATAL_BITE, 17s, 20s);   // 17-20秒后首次施放致命撕咬
        events.ScheduleEvent(EVENT_INFECTED_BITE, 10s, 12s); // 10-12秒后首次施放感染撕咬
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
                case EVENT_FATAL_BITE:
                    // 施放致命撕咬，对目标造成物理伤害
                    DoCastVictim(SPELL_FATAL_BITE);
                    events.ScheduleEvent(EVENT_FATAL_BITE, 8s, 10s);
                    break;
                case EVENT_INFECTED_BITE:
                    // 施放感染撕咬，造成物理伤害并施加疾病效果
                    DoCast(me, SPELL_INFECTED_BITE);
                    // 注意：这里重复调度了致命撕咬事件，可能是原代码的bug
                    events.ScheduleEvent(EVENT_FATAL_BITE, 8s, 10s);
                    break;
                default:
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
 * @brief 注册奴役者基兹卢尔脚本
 *
 * 将奴役者基兹卢尔BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_gizrul_the_slavener()
{
    RegisterBlackrockSpireCreatureAI(boss_gizrul_the_slavener);
}
