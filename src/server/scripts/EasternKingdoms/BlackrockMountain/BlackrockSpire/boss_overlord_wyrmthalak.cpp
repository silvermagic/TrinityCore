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
 * @file boss_overlord_wyrmthalak.cpp
 * @brief 黑石塔下层首领 - 维姆萨拉克霸主 (Overlord Wyrmthalak) AI 实现
 *
 * 本模块实现了黑石塔下层副本中第一个首领维姆萨拉克霸主的战斗AI逻辑。
 * 维姆萨拉克霸主是一个强大的龙人战士，使用各种近战技能并在生命值低于50%时召唤援军。
 *
 * 主要功能：
 * - 管理首领的战斗周期性技能施放
 * - 实现爆炸波、吼叫、顺劈斩和击退技能
 * - 实现生命值阈值触发的援军召唤机制
 * - 处理召唤生物的战斗参与
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 法术ID枚举
 *
 * 定义维姆萨拉克霸主使用的所有法术ID
 */
enum Spells
{
    SPELL_BLASTWAVE                 = 11130, ///< 爆炸波 - 对周围敌人造成火焰伤害并击退
    SPELL_SHOUT                     = 23511, ///< 吼叫 - 降低周围敌人的护甲值
    SPELL_CLEAVE                    = 20691, ///< 顺劈斩 - 对目标和附近敌人造成伤害
    SPELL_KNOCKAWAY                 = 20686  ///< 击退 - 击退当前目标并降低威胁值
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的所有事件ID，用于事件调度系统
 */
enum Events
{
    EVENT_BLAST_WAVE                = 1,     ///< 爆炸波事件
    EVENT_SHOUT                     = 2,     ///< 吼叫事件
    EVENT_CLEAVE                    = 3,     ///< 顺劈斩事件
    EVENT_KNOCK_AWAY                = 4      ///< 击退事件
};

/**
 * @brief 召唤生物ID枚举
 *
 * 定义召唤的援军生物ID
 */
enum Adds
{
    NPC_SPIRESTONE_WARLORD          = 9216,  ///< 尖石军阀 - 召唤的强力近战单位
    NPC_SMOLDERTHORN_BERSERKER      = 9268   ///< 燃棘狂战士 - 召唤的狂暴近战单位
};

/// 尖石军阀召唤位置
const Position SummonLocation1 = { -39.355f, -513.456f, 88.472f, 4.679f };
/// 燃棘狂战士召唤位置
const Position SummonLocation2 = { -49.875f, -511.896f, 88.195f, 4.613f };

/**
 * @brief 维姆萨拉克霸主 AI 结构体
 *
 * 继承自 BossAI 基类，实现维姆萨拉克霸主的完整战斗AI。
 * 负责管理技能施放时机和召唤援军机制。
 *
 * 战斗机制：
 * - 生命值低于51%时召唤两个援军：尖石军阀和燃棘狂战士
 * - 召唤只会发生一次，通过 Summoned 标记控制
 */
struct boss_overlord_wyrmthalak : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 BossAI 基类，关联首领数据为 DATA_OVERLORD_WYRMTHALAK，
     * 并调用 Initialize() 初始化成员变量
     */
    boss_overlord_wyrmthalak(Creature* creature) : BossAI(creature, DATA_OVERLORD_WYRMTHALAK)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将召唤标记重置为 false，用于控制援军召唤。
     * 应在构造函数和 Reset() 中调用。
     */
    void Initialize()
    {
        Summoned = false;  ///< 援军召唤标记
    }

    bool Summoned;  ///< 援军是否已召唤的标记

    /**
     * @brief 重置首领状态
     *
     * 当首领脱离战斗或重置时调用。
     * 调用基类的 _Reset() 方法清理事件队列和重置战斗状态，
     * 并重新初始化成员变量。
     *
     * @note 调用时机：首领脱战、重置副本、首领死亡后重生
     */
    void Reset() override
    {
        _Reset();
        Initialize();
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（通常是第一个攻击者）
     *
     * 当首领进入战斗状态时调用。
     * 初始化战斗事件调度：
     * - 爆炸波：20秒后首次施放
     * - 吼叫：2秒后首次施放
     * - 顺劈斩：6秒后首次施放
     * - 击退：12秒后首次施放
     *
     * @note 调用时机：首领被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_BLAST_WAVE, 20s);
        events.ScheduleEvent(EVENT_SHOUT, 2s);
        events.ScheduleEvent(EVENT_CLEAVE, 6s);
        events.ScheduleEvent(EVENT_KNOCK_AWAY, 12s);
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
     * 2. 检查生命值是否低于51%，触发召唤援军
     * 3. 更新事件队列
     * 4. 如果正在施法则暂停技能处理
     * 5. 执行到期事件并施放相应技能
     * 6. 如果未施法则进行近战攻击
     *
     * 技能循环：
     * - 爆炸波：每20秒施放一次
     * - 吼叫：每10秒施放一次
     * - 顺劈斩：每7秒施放一次
     * - 击退：每14秒施放一次
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效攻击目标
        if (!UpdateVictim())
            return;

        // 检查生命值是否低于51%且未召唤过援军
        if (!Summoned && HealthBelowPct(51))
        {
            // 随机选择一个有效目标
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
            {
                // 召唤尖石军阀并攻击目标
                if (Creature* warlord = me->SummonCreature(NPC_SPIRESTONE_WARLORD, SummonLocation1, TEMPSUMMON_TIMED_DESPAWN, 5min))
                    warlord->AI()->AttackStart(target);
                // 召唤燃棘狂战士并攻击目标
                if (Creature* berserker = me->SummonCreature(NPC_SMOLDERTHORN_BERSERKER, SummonLocation2, TEMPSUMMON_TIMED_DESPAWN, 5min))
                    berserker->AI()->AttackStart(target);
                // 标记已召唤，防止重复召唤
                Summoned = true;
            }
        }

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
                case EVENT_BLAST_WAVE:
                    // 对当前目标施放爆炸波
                    DoCastVictim(SPELL_BLASTWAVE);
                    // 安排下一次爆炸波，20秒后
                    events.ScheduleEvent(EVENT_BLAST_WAVE, 20s);
                    break;
                case EVENT_SHOUT:
                    // 对当前目标施放吼叫
                    DoCastVictim(SPELL_SHOUT);
                    // 安排下一次吼叫，10秒后
                    events.ScheduleEvent(EVENT_SHOUT, 10s);
                    break;
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    // 安排下一次顺劈斩，7秒后
                    events.ScheduleEvent(EVENT_CLEAVE, 7s);
                    break;
                case EVENT_KNOCK_AWAY:
                    // 对当前目标施放击退
                    DoCastVictim(SPELL_KNOCKAWAY);
                    // 安排下一次击退，14秒后
                    events.ScheduleEvent(EVENT_KNOCK_AWAY, 14s);
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
 * @brief 注册维姆萨拉克霸主 AI
 *
 * 此函数将维姆萨拉克霸主的AI注册到脚本系统中，
 * 使游戏服务器能够正确加载和运行该首领的AI逻辑。
 */
void AddSC_boss_overlordwyrmthalak()
{
    RegisterBlackrockSpireCreatureAI(boss_overlord_wyrmthalak);
}
