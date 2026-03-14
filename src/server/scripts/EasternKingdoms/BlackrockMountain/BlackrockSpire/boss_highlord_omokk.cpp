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
 * @file boss_highlord_omokk.cpp
 * @brief 黑石塔下层首领 - 大领主奥莫克 (Highlord Omokk) AI 实现
 *
 * 本模块实现了黑石塔下层副本中第三个首领大领主奥莫克的战斗AI逻辑。
 * 奥莫克是一个食人魔首领，主要使用狂暴和击退技能与玩家战斗。
 *
 * 主要功能：
 * - 管理首领的战斗周期性技能施放
 * - 实现狂暴技能的定期施放
 * - 实现击退技能的定期施放
 * - 处理首领的战斗状态管理
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义大领主奥莫克使用的所有法术ID
 */
enum Spells
{
    SPELL_FRENZY                    = 8269,  ///< 狂暴 - 提高攻击速度和伤害
    SPELL_KNOCK_AWAY                = 10101  ///< 击退 - 击退当前目标并降低威胁值
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的所有事件ID，用于事件调度系统
 */
enum Events
{
    EVENT_FRENZY                    = 1,     ///< 狂暴事件 - 触发狂暴技能施放
    EVENT_KNOCK_AWAY                = 2      ///< 击退事件 - 触发击退技能施放
};

/**
 * @brief 大领主奥莫克 AI 结构体
 *
 * 继承自 BossAI 基类，实现大领主奥莫克的完整战斗AI。
 * 负责管理技能施放时机、战斗事件调度和状态管理。
 */
struct boss_highlord_omokk : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 BossAI 基类，关联首领数据为 DATA_HIGHLORD_OMOKK
     */
    boss_highlord_omokk(Creature* creature) : BossAI(creature, DATA_HIGHLORD_OMOKK) { }

    /**
     * @brief 重置首领状态
     *
     * 当首领脱离战斗或重置时调用。
     * 调用基类的 _Reset() 方法清理事件队列和重置战斗状态。
     *
     * @note 调用时机：首领脱战、重置副本、首领死亡后重生
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（通常是第一个攻击者）
     *
     * 当首领进入战斗状态时调用。
     * 初始化战斗事件调度：
     * - 狂暴技能：20秒后首次施放
     * - 击退技能：18秒后首次施放
     *
     * @note 调用时机：首领被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_FRENZY, 20s);
        events.ScheduleEvent(EVENT_KNOCK_AWAY, 18s);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀首领的单位（可为nullptr）
     *
     * 当首领死亡时调用。
     * 调用基类的 _JustDied() 方法处理副本状态更新和战利品生成。
     *
     * @note 调用时机：首领生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理战斗AI的主逻辑循环。
     *
     * 执行流程：
     * 1. 检查是否有有效攻击目标，无则返回
     * 2. 更新事件队列
     * 3. 如果正在施法则暂停技能处理
     * 4. 执行到期事件并施放相应技能
     * 5. 如果未施法则进行近战攻击
     *
     * 技能循环：
     * - 狂暴：每60秒施放一次
     * - 击退：每12秒施放一次
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效攻击目标
        if (!UpdateVictim())
            return;

        // 更新事件队列时间
        events.Update(diff);

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FRENZY:
                    // 对当前目标施放狂暴技能
                    DoCastVictim(SPELL_FRENZY);
                    // 安排下一次狂暴，60秒后
                    events.ScheduleEvent(EVENT_FRENZY, 1min);
                    break;
                case EVENT_KNOCK_AWAY:
                    // 对当前目标施放击退技能
                    DoCastVictim(SPELL_KNOCK_AWAY);
                    // 安排下一次击退，12秒后
                    events.ScheduleEvent(EVENT_KNOCK_AWAY, 12s);
                    break;
                default:
                    break;
            }

            // 如果在事件处理过程中开始施法，则退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果没有在施法且准备就绪，进行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册大领主奥莫克 AI
 *
 * 此函数将大领主奥莫克的AI注册到脚本系统中，
 * 使游戏服务器能够正确加载和运行该首领的AI逻辑。
 */
void AddSC_boss_highlordomokk()
{
    RegisterBlackrockSpireCreatureAI(boss_highlord_omokk);
}
