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
 * @file boss_urok_doomhowl.cpp
 * @brief 黑石塔 - 乌洛克 BOSS 脚本
 *
 * 本模块实现了黑石塔下层副本中 BOSS 乌洛克的 AI 行为。
 * 乌洛克是一只强大的食人魔,位于黑石塔下层的大厅区域。
 *
 * 主要功能:
 * - 撕裂: 对目标造成流血伤害
 * - 打击: 强力的近战攻击
 * - 恐吓咆哮: 使周围敌人恐惧(定义了但未在战斗中使用)
 *
 * 特殊说明:
 * 乌洛克需要通过特定的召唤仪式才能出现,玩家需要在食人魔大厅使用召唤法术。
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 乌洛克使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_REND                      = 16509,    ///< 撕裂 - 造成流血伤害
    SPELL_STRIKE                    = 15580,    ///< 打击 - 强力近战攻击
    SPELL_INTIMIDATING_ROAR         = 16508     ///< 恐吓咆哮 - 使敌人恐惧(未使用)
};

/**
 * @brief 乌洛克台词枚举
 */
enum Says
{
    SAY_SUMMON                      = 0,        ///< 召唤时台词
    SAY_AGGRO                       = 1,        ///< 进入战斗时台词
};

/**
 * @brief BOSS 战斗事件 ID 枚举
 */
enum Events
{
    EVENT_REND                      = 1,        ///< 撕裂事件
    EVENT_STRIKE                    = 2,        ///< 打击事件
    EVENT_INTIMIDATING_ROAR         = 3         ///< 恐吓咆哮事件(未使用)
};

/**
 * @brief 乌洛克 BOSS AI 结构体
 *
 * 继承自 BossAI,实现了乌洛克的战斗逻辑。
 * 乌洛克是一名食人魔战士,主要使用近战攻击和流血技能。
 */
struct boss_urok_doomhowl : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_urok_doomhowl(Creature* creature) : BossAI(creature, DATA_UROK_DOOMHOWL) { }

    /**
     * @brief 重置 BOSS 状态
     *
     * 当 BOSS 脱离战斗或重置时调用。
     * 清除所有事件计时器,重置战斗状态。
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief 进入战斗
     * @param who 仇恨目标
     *
     * 当 BOSS 进入战斗时调用。
     * 初始化技能的施放计时器,并喊出战斗台词。
     *
     * 技能施放时机:
     * - 撕裂: 17-20秒后首次施放
     * - 打击: 10-12秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(SPELL_REND, 17s, 20s);
        events.ScheduleEvent(SPELL_STRIKE, 10s, 12s);
        // 喊出战斗台词
        Talk(SAY_AGGRO);
    }

    /**
     * @brief 死亡处理
     * @param killer 击杀者(未使用)
     *
     * 当 BOSS 死亡时调用。
     * 清理战斗状态和事件。
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 每个游戏循环周期调用,处理 BOSS 的战斗行为。
     *
     * 执行流程:
     * 1. 检查是否有有效的战斗目标
     * 2. 更新事件计时器
     * 3. 如果正在施法则等待
     * 4. 处理到期的技能事件
     * 5. 执行近战攻击
     *
     * 性能注意事项:
     * - 使用事件系统管理技能冷却,避免频繁的时间计算
     * - 施法状态检查确保不会打断正在施放的法术
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case SPELL_REND:
                    // 对当前目标施放撕裂
                    DoCastVictim(SPELL_REND);
                    // 8-10秒后再次施放
                    events.ScheduleEvent(SPELL_REND, 8s, 10s);
                    break;
                case SPELL_STRIKE:
                    // 对当前目标施放打击
                    DoCastVictim(SPELL_STRIKE);
                    // 8-10秒后再次施放
                    events.ScheduleEvent(SPELL_STRIKE, 8s, 10s);
                    break;
                default:
                    break;
            }

            // 如果开始施法,则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册 BOSS 脚本
 *
 * 将乌洛克的 AI 注册到脚本系统中。
 */
void AddSC_boss_urok_doomhowl()
{
    RegisterBlackrockSpireCreatureAI(boss_urok_doomhowl);
}
