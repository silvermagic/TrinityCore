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
 * @file boss_supremus.cpp
 * @brief 苏普雷姆斯Boss战脚本
 *
 * 本模块实现了苏普雷姆斯的完整战斗逻辑，包括：
 * - 多阶段战斗：打击阶段和追逐阶段交替进行
 * - 打击阶段：苏普雷姆斯对近战范围内生命值最高的目标施放憎恨打击
 * - 追逐阶段：苏普雷姆斯免疫嘲讽，随机追逐目标并施放冲锋
 * - 熔岩打击：生成熔岩火焰在场上移动
 * - 火山爆发：在追逐阶段召唤火山，造成范围伤害
 * - 15分钟狂暴计时器
 *
 * 战斗机制：
 * 1. 初始阶段为打击阶段，持续1分钟
 * 2. 打击阶段结束后切换到追逐阶段，持续1分钟
 * 3. 两个阶段交替循环直到战斗结束
 * 4. 熔岩打击在两个阶段都会施放
 * 5. 火山爆发仅在追逐阶段施放
 */

#include "ScriptMgr.h"
#include "black_temple.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "ScriptedCreature.h"

/**
 * @brief 对白枚举
 */
enum Texts
{
    EMOTE_NEW_TARGET          = 0,  ///< 表情：切换到新目标
    EMOTE_PUNCH_GROUND        = 1,  ///< 表情：击打地面
    EMOTE_GROUND_CRACK        = 2   ///< 表情：地面裂开
};

/**
 * @brief 技能枚举
 */
enum Spells
{
    SPELL_MOLTEN_PUNCH        = 40126,  ///< 熔岩打击：生成熔岩火焰
    SPELL_HATEFUL_STRIKE      = 41926,  ///< 憎恨打击：对生命值最高的目标造成伤害
    SPELL_MOLTEN_FLAME        = 40980,  ///< 熔岩火焰：场上移动的火焰造成伤害
    SPELL_VOLCANIC_ERUPTION   = 40117,  ///< 火山爆发：火山施放的伤害光环
    SPELL_VOLCANIC_SUMMON     = 40276,  ///< 召唤火山：在随机位置召唤火山
    SPELL_VOLCANIC_GEYSER     = 42055,  ///< 火山喷泉：火山的视觉效果
    SPELL_BERSERK             = 45078,  ///< 狂暴：15分钟后进入狂暴状态
    SPELL_SNARE_SELF          = 41922,  ///< 自我减速：追逐阶段的移动速度降低
    SPELL_CHARGE              = 41581   ///< 冲锋：冲向目标
};

/**
 * @brief 事件枚举
 */
enum Events
{
    EVENT_BERSERK = 1,              ///< 狂暴事件
    EVENT_SWITCH_PHASE,             ///< 切换阶段事件
    EVENT_FLAME,                    ///< 熔岩打击事件
    EVENT_VOLCANO,                  ///< 火山事件
    EVENT_SWITCH_TARGET,            ///< 切换目标事件
    EVENT_HATEFUL_STRIKE            ///< 憎恨打击事件
};

/**
 * @brief 阶段枚举
 */
enum Phases
{
    PHASE_INITIAL =  1,             ///< 初始阶段
    PHASE_STRIKE  =  2,             ///< 打击阶段：施放憎恨打击，可被嘲讽
    PHASE_CHASE   =  3              ///< 追逐阶段：随机追逐目标，免疫嘲讽
};

/**
 * @brief 动作枚举
 */
enum Actions
{
    ACTION_DISABLE_VULCANO = 1      ///< 禁用火山
};
/**
 * @struct boss_supremus
 * @brief 苏普雷姆斯AI
 *
 * 继承自BossAI，实现苏普雷姆斯的战斗逻辑
 *
 * 战斗流程：
 * 1. 战斗开始进入打击阶段
 * 2. 打击阶段持续1分钟后切换到追逐阶段
 * 3. 追逐阶段持续1分钟后切换回打击阶段
 * 4. 两个阶段循环直到战斗结束或15分钟狂暴
 *
 * 打击阶段特点：
 * - 对近战范围内生命值最高的目标施放憎恨打击
 * - 可以被嘲讽
 * - 每15-20秒施放一次熔岩打击
 *
 * 追逐阶段特点：
 * - 免疫嘲讽
 * - 移动速度降低
 * - 每10秒切换目标并冲锋
 * - 每10秒召唤火山
 */
struct boss_supremus : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_supremus(Creature* creature) : BossAI(creature, DATA_SUPREMUS) { }

    /**
     * @brief 重置回调
     *
     * 初始化苏普雷姆斯：
     * - 重置事件和阶段
     * - 允许被嘲讽
     *
     * 调用时机：生物重置或脱战时
     */
    void Reset() override
    {
        _Reset();
        events.SetPhase(PHASE_INITIAL);
        me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, false);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, false);
    }

    /**
     * @brief 进入脱战模式回调
     * @param why 脱战原因
     *
     * 当苏普雷姆斯脱战时调用，消失所有召唤物
     *
     * 调用时机：战斗重置或脱战时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        summons.DespawnAll();
        _DespawnAtEvade();
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当苏普雷姆斯进入战斗时调用：
     * - 切换到打击阶段
     * - 安排狂暴计时器（15分钟）
     * - 安排熔岩打击（20秒）
     *
     * 调用时机：生物首次进入战斗状态
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        ChangePhase();
        events.ScheduleEvent(EVENT_BERSERK, 15min);
        events.ScheduleEvent(EVENT_FLAME, 20s);
    }

    /**
     * @brief 切换阶段
     *
     * 在打击阶段和追逐阶段之间切换：
     *
     * 切换到打击阶段：
     * - 禁用所有火山
     * - 安排憎恨打击（每5秒）
     * - 移除自我减速
     * - 允许被嘲讽
     *
     * 切换到追逐阶段：
     * - 安排火山召唤（每10秒）
     * - 安排目标切换（每10秒）
     * - 免疫嘲讽
     * - 施放自我减速
     *
     * 共同操作：
     * - 重置威胁列表
     * - 重新进入战斗
     * - 安排下次阶段切换（1分钟）
     *
     * 调用时机：战斗开始或阶段切换事件触发时
     */
    void ChangePhase()
    {
        if (events.IsInPhase(PHASE_INITIAL) || events.IsInPhase(PHASE_CHASE))
        {
            // 切换到打击阶段
            events.SetPhase(PHASE_STRIKE);
            DummyEntryCheckPredicate pred;
            summons.DoAction(ACTION_DISABLE_VULCANO, pred);
            events.ScheduleEvent(EVENT_HATEFUL_STRIKE, Seconds(2), 0, PHASE_STRIKE);
            me->RemoveAurasDueToSpell(SPELL_SNARE_SELF);
            me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, false);
            me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, false);
        }
        else
        {
            // 切换到追逐阶段
            events.SetPhase(PHASE_CHASE);
            events.ScheduleEvent(EVENT_VOLCANO, Seconds(5), 0, PHASE_CHASE);
            events.ScheduleEvent(EVENT_SWITCH_TARGET, Seconds(10), 0, PHASE_CHASE);
            me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, true);
            me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, true);
            DoCast(SPELL_SNARE_SELF);
        }
        ResetThreatList();
        DoZoneInCombat();
        events.ScheduleEvent(EVENT_SWITCH_PHASE, 1min);
    }

    /**
     * @brief 计算憎恨打击目标
     * @return 选中的目标指针，如果没有合适目标则返回nullptr
     *
     * 选择近战范围内生命值最高的目标：
     * - 遍历所有威胁列表中的目标
     * - 仅考虑近战范围内的目标
     * - 选择生命值最高的目标
     *
     * 调用时机：施放憎恨打击时
     *
     * 性能注意事项：遍历威胁列表，O(n)复杂度
     */
    Unit* CalculateHatefulStrikeTarget()
    {
        uint32 health = 0;
        Unit* target = nullptr;

        for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
        {
            Unit* unit = ref->GetVictim();
            if (me->IsWithinMeleeRange(unit))
            {
                if (unit->GetHealth() > health)
                {
                    health = unit->GetHealth();
                    target = unit;
                }
            }
        }

        return target;
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 处理各种战斗事件：
     * - EVENT_BERSERK：进入狂暴状态
     * - EVENT_FLAME：施放熔岩打击
     * - EVENT_HATEFUL_STRIKE：施放憎恨打击
     * - EVENT_SWITCH_TARGET：切换追逐目标
     * - EVENT_VOLCANO：召唤火山
     * - EVENT_SWITCH_PHASE：切换阶段
     *
     * 调用时机：事件调度器触发时
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_BERSERK:
                // 进入狂暴状态
                DoCastSelf(SPELL_BERSERK, true);
                break;
            case EVENT_FLAME:
                // 施放熔岩打击，生成熔岩火焰
                DoCast(SPELL_MOLTEN_PUNCH);
                events.Repeat(Seconds(15), Seconds(20));
                break;
            case EVENT_HATEFUL_STRIKE:
                // 对生命值最高的近战目标施放憎恨打击
                if (Unit* target = CalculateHatefulStrikeTarget())
                    DoCast(target, SPELL_HATEFUL_STRIKE);
                events.Repeat(Seconds(5));
                break;
            case EVENT_SWITCH_TARGET:
                // 随机选择目标并冲锋
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100.0f, true))
                {
                    ResetThreatList();
                    AddThreat(target, 1000000.0f);
                    DoCast(target, SPELL_CHARGE);
                    Talk(EMOTE_NEW_TARGET);
                }
                events.Repeat(Seconds(10));
                break;
            case EVENT_VOLCANO:
                // 召唤火山
                DoCastAOE(SPELL_VOLCANIC_SUMMON, true);
                Talk(EMOTE_GROUND_CRACK);
                events.Repeat(Seconds(10));
                break;
            case EVENT_SWITCH_PHASE:
                // 切换阶段
                ChangePhase();
                break;
            default:
                break;
        }
    }
};

/**
 * @struct npc_molten_flame
 * @brief 熔岩火焰AI
 *
 * 继承自NullCreatureAI，实现熔岩火焰的行为逻辑
 *
 * 功能：
 * - 生成后随机移动100码距离
 * - 施放熔岩火焰光环，对接触的玩家造成伤害
 * - 持续存在直到战斗结束
 */
struct npc_molten_flame : public NullCreatureAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_molten_flame(Creature* creature) : NullCreatureAI(creature) { }

    /**
     * @brief 初始化AI
     *
     * 初始化熔岩火焰：
     * - 计算随机移动方向（随机角度）
     * - 移动到100码外的目标点
     * - 施放熔岩火焰光环
     *
     * 调用时机：AI初始化时
     */
    void InitializeAI() override
    {
        float x, y, z;
        me->GetNearPoint(me, x, y, z, 100.0f, frand(0.f, 2.f * float(M_PI)));
        me->GetMotionMaster()->MovePoint(0, x, y, z);
        DoCastSelf(SPELL_MOLTEN_FLAME, true);
    }
};

/**
 * @struct npc_volcano
 * @brief 火山AI
 *
 * 继承自NullCreatureAI，实现火山的行为逻辑
 *
 * 功能：
 * - 生成3秒后开始喷发
 * - 施放火山爆发光环，造成范围伤害
 * - 可被苏普雷姆斯禁用（切换到打击阶段时）
 */
struct npc_volcano : public NullCreatureAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_volcano(Creature* creature) : NullCreatureAI(creature) { }

    /**
     * @brief 重置回调
     *
     * 初始化火山：
     * - 安排3秒后喷发
     *
     * 调用时机：生物重置或生成时
     */
    void Reset() override
    {
        _scheduler.Schedule(Seconds(3), [this](TaskContext /*context*/)
        {
            // 施放火山爆发，造成范围伤害
            DoCastSelf(SPELL_VOLCANIC_ERUPTION);
        });
    }

    /**
     * @brief 执行动作回调
     * @param action 动作ID
     *
     * 处理外部发来的动作指令：
     * - ACTION_DISABLE_VULCANO：移除所有火山效果，停止造成伤害
     *
     * 调用时机：苏普雷姆斯切换到打击阶段时
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_DISABLE_VULCANO)
        {
            me->RemoveAurasDueToSpell(SPELL_VOLCANIC_ERUPTION);
            me->RemoveAurasDueToSpell(SPELL_VOLCANIC_GEYSER);
        }
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每帧调用，更新任务调度器
     *
     * 性能注意事项：每帧调用，需保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;       ///< 任务调度器，用于安排火山喷发
};

/**
 * @brief 注册苏普雷姆斯脚本
 *
 * 在服务器启动时调用，注册以下脚本：
 * - 苏普雷姆斯AI
 * - 熔岩火焰AI
 * - 火山AI
 */
void AddSC_boss_supremus()
{
    RegisterBlackTempleCreatureAI(boss_supremus);
    RegisterBlackTempleCreatureAI(npc_molten_flame);
    RegisterBlackTempleCreatureAI(npc_volcano);
}
