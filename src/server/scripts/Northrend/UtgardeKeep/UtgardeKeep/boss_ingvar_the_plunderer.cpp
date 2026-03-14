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
 * @file boss_ingvar_the_plunderer.cpp
 * @brief 诺森德副本"乌特加德城堡"最终BOSS：掠夺者英格瓦(Ingvar the Plunderer)的AI脚本
 *
 * 模块职责：
 * 1. 实现掠夺者英格瓦的AI逻辑，包括两阶段战斗（人类形态和亡灵形态）
 * 2. 处理BOSS死亡后的复活机制，由女妖Annhylde召唤并复活
 * 3. 管理战斗事件调度、技能施放和阶段转换
 * 4. 实现暗影之斧召唤物的AI行为
 *
 * 战斗流程：
 * - 第一阶段：人类形态，使用Cleave、Smash、Staggering Roar、Enrage
 * - 过渡阶段：死亡后女妖复活，转换为亡灵形态
 * - 第二阶段：亡灵形态，使用Dark Smash、Dreadful Roar、Woe Strike、Shadow Axe
 *
 * @author TrinityCore Team
 * @date 2026
 */

/* ScriptData
SDName: Boss_Ingvar_The_Plunderer
SD%Complete: 95
SDComment: Blizzlike Timers (just shadow axe summon needs a new timer)
SDCategory: Utgarde Keep
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellScript.h"
#include "utgarde_keep.h"

/**
 * @enum Yells
 * @brief BOSS和NPC的台词文本ID枚举
 */
enum Yells
{
    // Ingvar (Human/Undead) - 英格瓦的台词（人类/亡灵形态通用）
    SAY_AGGRO                   = 0,    // 开怪台词
    SAY_SLAY                    = 1,    // 击杀玩家台词
    SAY_DEATH                   = 2,    // 死亡台词（两阶段各触发一次）

    // Annhylde The Caller - 召唤者安海德的台词
    YELL_RESURRECT              = 0     // 复活英格瓦时的喊话
};

/**
 * @enum Events
 * @brief 事件调度器使用的定时事件ID枚举
 */
enum Events
{
    // 第一阶段（人类形态）事件
    EVENT_CLEAVE = 1,               // 顺劈斩事件
    EVENT_SMASH,                    // 猛击事件
    EVENT_STAGGERING_ROAR,          // 踉跄咆哮事件
    EVENT_ENRAGE,                   // 狂暴事件

    // 第二阶段（亡灵形态）事件
    EVENT_DARK_SMASH,               // 黑暗猛击事件
    EVENT_DREADFUL_ROAR,            // 可怕咆哮事件
    EVENT_WOE_STRIKE,               // 悲伤打击事件
    EVENT_SHADOW_AXE,               // 暗影之斧召唤事件
    EVENT_JUST_TRANSFORMED,         // 形态转换完成事件
    EVENT_SUMMON_BANSHEE,           // 召唤女妖事件

    // 女妖复活流程事件
    EVENT_RESURRECT_1,              // 复活流程第一步
    EVENT_RESURRECT_2               // 复活流程第二步
};

/**
 * @enum Phases
 * @brief BOSS战斗阶段枚举
 */
enum Phases
{
    PHASE_HUMAN = 1,        // 人类形态阶段
    PHASE_UNDEAD,           // 亡灵形态阶段
    PHASE_EVENT             // 特殊事件阶段（形态转换过渡）
};

/**
 * @enum Spells
 * @brief BOSS和相关NPC使用的法术ID枚举
 */
enum Spells
{
    // Ingvar Spells human form - 英格瓦人类形态技能
    SPELL_CLEAVE                                = 42724,    // 顺劈斩：对前方敌人造成物理伤害
    SPELL_SMASH                                 = 42669,    // 猛击：AOE物理伤害
    SPELL_STAGGERING_ROAR                       = 42708,    // 踉跄咆哮：打断施法并造成伤害
    SPELL_ENRAGE                                = 42705,    // 狂暴：提升攻击速度和伤害

    SPELL_INGVAR_FEIGN_DEATH                    = 42795,    // 英格瓦假死：第一阶段"死亡"时触发
    SPELL_SUMMON_BANSHEE                        = 42912,    // 召唤女妖：召唤Annhylde
    SPELL_SCOURG_RESURRECTION                   = 42863,    // 天灾复活：在英格瓦周围生成复活效果

    // Ingvar Spells undead form - 英格瓦亡灵形态技能
    SPELL_DARK_SMASH                            = 42723,    // 黑暗猛击：强力的单体物理攻击
    SPELL_DREADFUL_ROAR                         = 42729,    // 可怕咆哮：沉默并造成伤害
    SPELL_WOE_STRIKE                            = 42730,    // 悲伤打击：造成伤害并附加Debuff
    SPELL_WOE_STRIKE_EFFECT                     = 42739,    // 悲伤打击效果：治疗时触发伤害

    SPELL_SHADOW_AXE_SUMMON                     = 42748,    // 召唤暗影之斧
    SPELL_SHADOW_AXE_PERIODIC_DAMAGE            = 42750,    // 暗影之斧周期性伤害

    // Spells for Annhylde - 召唤者安海德的法术
    SPELL_SCOURG_RESURRECTION_HEAL              = 42704,    // 天灾复活治疗：满血复活+虚拟光环
    SPELL_SCOURG_RESURRECTION_BEAM              = 42857,    // 天灾复活光束：安海德引导的光束
    SPELL_SCOURG_RESURRECTION_DUMMY             = 42862,    // 天灾复活虚拟效果：表情虚拟效果
    SPELL_INGVAR_TRANSFORM                      = 42796     // 英格瓦变形：转换为亡灵形态
};

/**
 * @enum Misc
 * @brief 其他常量定义
 */
enum Misc
{
    ACTION_START_PHASE_2        // 启动第二阶段的动作ID
};

/**
 * @struct boss_ingvar_the_plunderer
 * @brief 掠夺者英格瓦的AI结构体
 *
 * 职责：
 * - 管理英格瓦的两阶段战斗逻辑（人类形态和亡灵形态）
 * - 处理死亡后由女妖复活的过渡流程
 * - 调度并执行各阶段的技能施放
 * - 管理战斗状态和阶段转换
 *
 * 继承自：BossAI（提供基础BOSS AI功能）
 */
struct boss_ingvar_the_plunderer : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_ingvar_the_plunderer(Creature* creature) : BossAI(creature, DATA_INGVAR) { }

    /**
     * @brief 重置BOSS状态
     *
     * 调用时机：
     * - BOSS脱战时
     * - 团队重置副本时
     * - BOSS被击杀后重生时
     *
     * 功能：
     * - 确保使用人类形态的NPC ID
     * - 移除无敌和不可交互标志
     * - 取消对玩家的免疫状态
     * - 重置事件调度器和战斗状态
     */
    void Reset() override
    {
        // 如果当前不是人类形态，则转换回人类形态
        if (me->GetEntry() != NPC_INGVAR)
            me->UpdateEntry(NPC_INGVAR);

        // 移除无敌标志，允许玩家攻击和交互
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
        me->SetImmuneToPC(false);

        // 调用父类的Reset方法，重置事件调度器
        _Reset();
    }

    /**
     * @brief 处理伤害接收事件
     * @param doneBy 造成伤害的单位
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息（如果有）
     *
     * 功能：
     * - 检测第一阶段"死亡"：当生命值归零且处于人类形态时触发假死
     * - 防止在过渡阶段受到伤害
     * - 启动复活流程
     */
    void DamageTaken(Unit* /*doneBy*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 检查是否应该触发第一阶段"死亡"
        if (damage >= me->GetHealth() && events.IsInPhase(PHASE_HUMAN))
        {
            // 切换到事件阶段（过渡阶段）
            events.SetPhase(PHASE_EVENT);
            // 安排3秒后召唤女妖
            events.ScheduleEvent(EVENT_SUMMON_BANSHEE, 3s, 0, PHASE_EVENT);

            // 移除所有光环并停止移动
            me->RemoveAllAuras();
            me->StopMoving();
            // 施放假死效果
            DoCast(me, SPELL_INGVAR_FEIGN_DEATH, true);

            // 设置无敌标志，防止玩家继续攻击
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
            me->SetImmuneToPC(true, true);

            // 播放死亡台词
            Talk(SAY_DEATH);
        }

        // 在事件阶段将伤害设为0，防止真正的死亡
        if (events.IsInPhase(PHASE_EVENT))
            damage = 0;
    }

    /**
     * @brief 处理外部动作事件
     * @param actionId 动作ID
     *
     * 调用时机：
     * - 由女妖Annhylde在复活流程结束时调用，启动第二阶段
     *
     * 功能：
     * - 接收ACTION_START_PHASE_2动作，启动亡灵阶段
     */
    void DoAction(int32 actionId) override
    {
        if (actionId == ACTION_START_PHASE_2)
            StartZombiePhase();
    }

    /**
     * @brief 启动亡灵阶段（第二阶段）
     *
     * 功能：
     * - 移除假死光环
     * - 施放变形法术，转换为亡灵形态
     * - 更新NPC ID为亡灵英格瓦
     * - 安排形态转换完成事件
     */
    void StartZombiePhase()
    {
        // 移除假死光环
        me->RemoveAura(SPELL_INGVAR_FEIGN_DEATH);
        // 施放变形效果
        DoCast(me, SPELL_INGVAR_TRANSFORM, true);
        // 更新为亡灵形态的NPC ID
        me->UpdateEntry(NPC_INGVAR_UNDEAD);
        // 500毫秒后触发转换完成事件
        events.ScheduleEvent(EVENT_JUST_TRANSFORMED, 500ms, 0, PHASE_EVENT);
    }

    /**
     * @brief 进入战斗时的处理函数
     * @param who 触发战斗的单位（通常是首个攻击者）
     *
     * 调用时机：
     * - BOSS被玩家攻击或主动攻击玩家时
     *
     * 功能：
     * - 防止在事件阶段或亡灵阶段重复进入战斗
     * - 设置为人类形态阶段
     * - 安排第一阶段所有技能的事件调度
     */
    void JustEngagedWith(Unit* who) override
    {
        // 英格瓦可能会收到多次JustEngagedWith调用，需要防止重复处理
        if (events.IsInPhase(PHASE_EVENT) || events.IsInPhase(PHASE_UNDEAD))
            return;
        BossAI::JustEngagedWith(who);

        // 播放开怪台词
        Talk(SAY_AGGRO);
        // 设置为人类形态阶段
        events.SetPhase(PHASE_HUMAN);
        // 安排第一阶段技能的事件
        events.ScheduleEvent(EVENT_CLEAVE, 6s, 12s, 0, PHASE_HUMAN);            // 顺劈斩：6-12秒
        events.ScheduleEvent(EVENT_STAGGERING_ROAR, 18s, 21s, 0, PHASE_HUMAN);  // 踉跄咆哮：18-21秒
        events.ScheduleEvent(EVENT_ENRAGE, 7s, 14s, 0, PHASE_HUMAN);            // 狂暴：7-14秒
        events.ScheduleEvent(EVENT_SMASH, 12s, 17s, 0, PHASE_HUMAN);            // 猛击：12-17秒
    }

    /**
     * @brief 攻击开始处理函数
     * @param who 攻击目标
     *
     * 调用时机：
     * - AI尝试开始攻击某个目标时
     *
     * 功能：
     * - 防止在过渡阶段（形态转换时）开始攻击，避免打断动画
     */
    void AttackStart(Unit* who) override
    {
        // 在事件阶段（过渡阶段）阻止攻击，防止英格瓦在转换时追击玩家
        if (events.IsInPhase(PHASE_EVENT))
            return;
        BossAI::AttackStart(who);
    }

    /**
     * @brief 死亡时的处理函数
     * @param killer 击杀者
     *
     * 调用时机：
     * - BOSS在第二阶段（亡灵形态）真正死亡时
     *
     * 功能：
     * - 通知实例BOSS已死亡
     * - 播放死亡台词
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 安排第二阶段（亡灵形态）的技能事件
     *
     * 调用时机：
     * - 形态转换完成后，由EVENT_JUST_TRANSFORMED事件处理中调用
     *
     * 功能：
     * - 切换到亡灵阶段
     * - 安排第二阶段所有技能的事件调度
     */
    void ScheduleSecondPhase()
    {
        // 设置为亡灵形态阶段
        events.SetPhase(PHASE_UNDEAD);
        // 安排第二阶段技能的事件
        events.ScheduleEvent(EVENT_DARK_SMASH, 14s, 18s, 0, PHASE_UNDEAD);      // 黑暗猛击：14-18秒
        events.ScheduleEvent(EVENT_DREADFUL_ROAR, 0ms, 0, PHASE_UNDEAD);        // 可怕咆哮：立即施放
        events.ScheduleEvent(EVENT_WOE_STRIKE, 10s, 14s, 0, PHASE_UNDEAD);      // 悲伤打击：10-14秒
        events.ScheduleEvent(EVENT_SHADOW_AXE, 30s, 0, PHASE_UNDEAD);           // 暗影之斧：30秒
    }

    /**
     * @brief 击杀单位时的处理函数
     * @param who 被击杀的单位
     *
     * 调用时机：
     * - BOSS击杀任何单位时
     *
     * 功能：
     * - 如果击杀的是玩家，播放击杀台词
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 主更新函数，每帧调用一次
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 调用时机：
     * - 每个服务器tick（通常为每秒多次）
     *
     * 功能：
     * - 更新事件调度器
     * - 执行到期的事件
     * - 处理两阶段所有技能的施放
     * - 在非事件阶段执行近战攻击
     *
     * 性能注意事项：
     * - 此函数高频调用，应避免复杂计算
     * - 施法状态下跳过事件处理，减少CPU占用
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果不在事件阶段且没有战斗目标，则不执行后续逻辑
        if (!events.IsInPhase(PHASE_EVENT) && !UpdateVictim())
            return;

        // 更新事件调度器
        events.Update(diff);

        // 如果正在施法，暂停事件处理，避免打断施法
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                // ========== 第一阶段（人类形态）技能 ==========
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    events.ScheduleEvent(EVENT_CLEAVE, 6s, 12s, 0, PHASE_HUMAN);
                    break;
                case EVENT_STAGGERING_ROAR:
                    // 施放踉跄咆哮（AOE打断和伤害）
                    DoCast(me, SPELL_STAGGERING_ROAR);
                    events.ScheduleEvent(EVENT_STAGGERING_ROAR, 18s, 22s, 0, PHASE_HUMAN);
                    break;
                case EVENT_ENRAGE:
                    // 施放狂暴（提升攻击速度和伤害）
                    DoCast(me, SPELL_ENRAGE);
                    events.ScheduleEvent(EVENT_ENRAGE, 7s, 14s, 0, PHASE_HUMAN);
                    break;
                case EVENT_SMASH:
                    // 施放猛击（AOE物理伤害）
                    DoCastAOE(SPELL_SMASH);
                    events.ScheduleEvent(EVENT_SMASH, 12s, 16s, 0, PHASE_HUMAN);
                    break;
                case EVENT_JUST_TRANSFORMED:
                    // 形态转换完成，准备进入第二阶段
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
                    me->SetImmuneToPC(false);
                    ScheduleSecondPhase();
                    Talk(SAY_AGGRO);
                    // 重新检测战斗范围内的玩家
                    DoZoneInCombat();
                    return;
                case EVENT_SUMMON_BANSHEE:
                    // 召唤女妖Annhylde开始复活流程
                    DoCast(me, SPELL_SUMMON_BANSHEE);
                    return;

                // ========== 第二阶段（亡灵形态）技能 ==========
                case EVENT_DARK_SMASH:
                    // 对当前目标施放黑暗猛击
                    DoCastVictim(SPELL_DARK_SMASH);
                    events.ScheduleEvent(EVENT_DARK_SMASH, 12s, 16s, 0, PHASE_UNDEAD);
                    break;
                case EVENT_DREADFUL_ROAR:
                    // 施放可怕咆哮（沉默和伤害）
                    DoCast(me, SPELL_DREADFUL_ROAR);
                    events.ScheduleEvent(EVENT_DREADFUL_ROAR, 18s, 22s, 0, PHASE_UNDEAD);
                    break;
                case EVENT_WOE_STRIKE:
                    // 对当前目标施放悲伤打击
                    DoCastVictim(SPELL_WOE_STRIKE);
                    events.ScheduleEvent(EVENT_WOE_STRIKE, 10s, 14s, 0, PHASE_UNDEAD);
                    break;
                case EVENT_SHADOW_AXE:
                    // 随机选择一个玩家目标，召唤暗影之斧
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                        DoCast(target, SPELL_SHADOW_AXE_SUMMON);
                    events.ScheduleEvent(EVENT_SHADOW_AXE, 30s, 0, PHASE_UNDEAD);
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，暂停后续事件处理
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 在非事件阶段执行近战攻击
        if (!events.IsInPhase(PHASE_EVENT))
            DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_annhylde_the_caller
 * @brief 召唤者安海德（女妖）的AI结构体
 *
 * 职责：
 * - 执行英格瓦的复活流程动画
 * - 从空中下降，施放复活光束
 * - 引导复活过程完成后返回空中
 *
 * 继承自：ScriptedAI（提供基础AI功能）
 */
struct npc_annhylde_the_caller : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_annhylde_the_caller(Creature* creature) : ScriptedAI(creature)
    {
        // 初始化坐标为0
        x = 0.f;
        y = 0.f;
        z = 0.f;
        // 获取实例脚本指针
        _instance = creature->GetInstanceScript();
    }

    /**
     * @brief 重置AI状态
     *
     * 调用时机：
     * - NPC生成时
     * - AI重置时
     *
     * 功能：
     * - 重置事件调度器
     * - 记录当前位置
     * - 开始下降到英格瓦位置的动画
     */
    void Reset() override
    {
        _events.Reset();

        // 记录当前位置（空中的生成位置）
        me->GetPosition(x, y, z);
        // 向下移动15单位，接近英格瓦的位置
        me->GetMotionMaster()->MovePoint(1, x, y, z - 15.0f);
    }

    /**
     * @brief 移动完成通知函数
     * @param type 移动类型
     * @param id 移动点ID
     *
     * 调用时机：
     * - NPC到达移动目标点时
     *
     * 功能：
     * - 处理下降到英格瓦位置后的复活流程
     * - 处理返回空中后的消失流程
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        switch (id)
        {
            case 1:
                // 到达英格瓦位置，开始复活流程
                Talk(YELL_RESURRECT);
                if (Creature* ingvar = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_INGVAR)))
                {
                    // 移除召唤女妖的光环
                    ingvar->RemoveAura(SPELL_SUMMON_BANSHEE);
                    // 施放复活虚拟效果
                    ingvar->CastSpell(ingvar, SPELL_SCOURG_RESURRECTION_DUMMY, true);
                    // 安海德引导复活光束
                    DoCast(ingvar, SPELL_SCOURG_RESURRECTION_BEAM);
                }
                // 8秒后执行复活第一步
                _events.ScheduleEvent(EVENT_RESURRECT_1, 8s);
                break;
            case 2:
                // 返回空中完成，消失
                me->DespawnOrUnsummon();
                break;
            default:
                break;
        }
    }

    // 女妖不参与战斗，空实现这些函数
    void AttackStart(Unit* /*who*/) override { }
    void MoveInLineOfSight(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }

    /**
     * @brief 主更新函数
     * @param diff 时间差（毫秒）
     *
     * 功能：
     * - 处理复活流程的两个步骤
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_RESURRECT_1:
                    // 复活流程第一步：移除假死效果，施放治疗
                    if (Creature* ingvar = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_INGVAR)))
                    {
                        // 移除假死光环
                        ingvar->RemoveAura(SPELL_INGVAR_FEIGN_DEATH);
                        // 施放复活治疗（恢复满血）
                        ingvar->CastSpell(ingvar, SPELL_SCOURG_RESURRECTION_HEAL, false);
                    }
                    // 3秒后执行第二步
                    _events.ScheduleEvent(EVENT_RESURRECT_2, 3s);
                    break;
                case EVENT_RESURRECT_2:
                    // 复活流程第二步：启动第二阶段
                    if (Creature* ingvar = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_INGVAR)))
                    {
                        // 移除复活虚拟效果
                        ingvar->RemoveAurasDueToSpell(SPELL_SCOURG_RESURRECTION_DUMMY);
                        // 通知英格瓦启动第二阶段
                        ingvar->AI()->DoAction(ACTION_START_PHASE_2);
                    }

                    // 返回空中
                    me->GetMotionMaster()->MovePoint(2, x, y, z + 15.0f);
                    break;
                default:
                    break;
            }
        }
    }

private:
    InstanceScript* _instance;      // 实例脚本指针，用于获取BOSS GUID
    EventMap _events;               // 事件调度器
    float x, y, z;                  // 初始位置坐标（空中）
};


/**
 * @struct npc_ingvar_throw_dummy
 * @brief 暗影之斧投掷假人的AI结构体
 *
 * 职责：
 * - 模拟暗影之斧的飞行轨迹
 * - 到达目标位置后施放周期性伤害光环
 * - 10秒后自动消失
 *
 * 继承自：ScriptedAI
 */
struct npc_ingvar_throw_dummy : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_ingvar_throw_dummy(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 调用时机：
     * - NPC生成时
     *
     * 功能：
     * - 查找投掷目标点
     * - 冲向目标位置
     */
    void Reset() override
    {
        // 查找最近的投掷目标点（200码范围内）
        if (Creature* target = me->FindNearestCreature(NPC_THROW_TARGET, 200.0f))
        {
            float x, y, z;
            target->GetPosition(x, y, z);
            // 冲向目标位置
            me->GetMotionMaster()->MoveCharge(x, y, z);
            // 移除目标点
            target->DespawnOrUnsummon();
        }
        else
            // 如果没有找到目标点，直接消失
            me->DespawnOrUnsummon();
    }

    /**
     * @brief 移动完成通知函数
     * @param type 移动类型
     * @param id 移动事件ID
     *
     * 调用时机：
     * - 冲锋到达目标位置时
     *
     * 功能：
     * - 施放周期性伤害光环
     * - 安排10秒后消失
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == EFFECT_MOTION_TYPE && id == EVENT_CHARGE)
        {
            // 施放暗影之斧周期性伤害光环
            me->CastSpell(me, SPELL_SHADOW_AXE_PERIODIC_DAMAGE, true);
            // 10秒后消失
            me->DespawnOrUnsummon(10s);
        }
    }
};

/**
 * @class spell_ingvar_summon_banshee
 * @brief 召唤女妖法术脚本（Spell ID: 42912）
 *
 * 职责：
 * - 修改召唤位置，使女妖在英格瓦背后上方30单位处生成
 *
 * 继承自：SpellScript
 */
// 42912 - Summon Banshee
class spell_ingvar_summon_banshee : public SpellScript
{
    PrepareSpellScript(spell_ingvar_summon_banshee);

    /**
     * @brief 设置召唤目标位置
     * @param dest 目标位置（可修改）
     *
     * 功能：
     * - 将召唤位置从施法者背后偏移30单位高度
     */
    void SetDest(SpellDestination& dest)
    {
        dest.RelocateOffset({ 0.0f, 0.0f, 30.0f, 0.0f });
    }

    /**
     * @brief 注册法术效果钩子
     */
    void Register() override
    {
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(spell_ingvar_summon_banshee::SetDest, EFFECT_0, TARGET_DEST_CASTER_BACK);
    }
};

/**
 * @class spell_ingvar_woe_strike
 * @brief 悲伤打击光环脚本（Spell ID: 42730, 59735）
 *
 * 职责：
 * - 处理悲伤打击的触发效果
 * - 当受影响的目标受到治疗时，对治疗者造成伤害
 *
 * 继承自：AuraScript
 */
// 42730, 59735 - Woe Strike
class spell_ingvar_woe_strike : public AuraScript
{
    PrepareAuraScript(spell_ingvar_woe_strike);

    /**
     * @brief 验证依赖的法术是否存在
     * @param spellInfo 法术信息
     * @return 法术是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WOE_STRIKE_EFFECT });
    }

    /**
     * @brief 检查是否应该触发效果
     * @param eventInfo 触发事件信息
     * @return 是否触发
     *
     * 功能：
     * - 只在受到治疗时触发
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        HealInfo* healInfo = eventInfo.GetHealInfo();
        if (!healInfo || !healInfo->GetHeal())
            return false;

        return true;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 功能：
     * - 阻止默认动作
     * - 对治疗者施放悲伤打击效果
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(eventInfo.GetActor(), SPELL_WOE_STRIKE_EFFECT, aurEff);
    }

    /**
     * @brief 注册光环效果钩子
     */
    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_ingvar_woe_strike::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_ingvar_woe_strike::HandleProc, EFFECT_1, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

/**
 * @brief 注册所有AI和法术脚本
 *
 * 调用时机：
 * - 服务器启动时，脚本加载系统会调用此函数
 *
 * 功能：
 * - 注册掠夺者英格瓦BOSS AI
 * - 注册召唤者安海德NPC AI
 * - 注册暗影之斧投掷假人NPC AI
 * - 注册召唤女妖法术脚本
 * - 注册悲伤打击光环脚本
 */
void AddSC_boss_ingvar_the_plunderer()
{
    RegisterUtgardeKeepCreatureAI(boss_ingvar_the_plunderer);
    RegisterUtgardeKeepCreatureAI(npc_annhylde_the_caller);
    RegisterUtgardeKeepCreatureAI(npc_ingvar_throw_dummy);
    RegisterSpellScript(spell_ingvar_summon_banshee);
    RegisterSpellScript(spell_ingvar_woe_strike);
}
