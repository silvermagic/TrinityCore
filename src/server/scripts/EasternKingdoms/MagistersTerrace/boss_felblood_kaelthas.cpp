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
 * @file boss_felblood_kaelthas.cpp
 * @brief 魔导师平台副本最终BOSS - 凯尔萨斯·逐日者（魔血形态）AI脚本
 *
 * 本模块实现了魔导师平台副本中最终BOSS凯尔萨斯的战斗逻辑。
 * 凯尔萨斯是魔导师平台的最终BOSS，拥有两个阶段的战斗：
 * - 第一阶段：使用火球、火焰打击、凤凰等技能
 * - 第二阶段（生命值低于50%）：重力 lapse 阶段，玩家会浮空并受到持续伤害
 *
 * 主要功能：
 * - BOSS的AI逻辑和技能施放
 * - 凤凰召唤物的AI实现
 * - 火焰打击法术效果处理
 * - 多阶段战斗流程控制
 *
 * @note 凯尔萨斯只能通过自杀法术死亡，不能被玩家直接击杀
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "magisters_terrace.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"

/**
 * @brief 凯尔萨斯的对话文本枚举
 *
 * 定义了凯尔萨斯在战斗中各个时机的对话ID
 */
enum Says
{
    // Kael'thas Sunstrider
    SAY_INTRO_1                 = 0,
    SAY_INTRO_2                 = 1,
    SAY_GRAVITY_LAPSE_1         = 2,
    SAY_GRAVITY_LAPSE_2         = 3,
    SAY_POWER_FEEDBACK          = 4,
    SAY_SUMMON_PHOENIX          = 5,
    SAY_ANNOUNCE_PYROBLAST      = 6,
    SAY_FLAME_STRIKE            = 7,
    SAY_DEATH                   = 8
};

/**
 * @brief 凯尔萨斯及相关召唤物的法术ID枚举
 *
 * 包含凯尔萨斯使用的所有法术，以及凤凰和火焰打击相关的法术
 */
enum Spells
{
    // Kael'thas Sunstrider
    SPELL_FIREBALL                              = 44189,
    SPELL_GRAVITY_LAPSE_CENTER_TELEPORT         = 44218,
    SPELL_GRAVITY_LAPSE_LEFT_TELEPORT           = 44219,
    SPELL_GRAVITY_LAPSE_FRONT_LEFT_TELEPORT     = 44220,
    SPELL_GRAVITY_LAPSE_FRONT_TELEPORT          = 44221,
    SPELL_GRAVITY_LAPSE_FRONT_RIGHT_TELEPORT    = 44222,
    SPELL_GRAVITY_LAPSE_RIGHT_TELEPORT          = 44223,
    SPELL_GRAVITY_LAPSE_INITIAL                 = 44224,
    SPELL_GRAVITY_LAPSE_FLY                     = 44227,
    SPELL_GRAVITY_LAPSE_BEAM_VISUAL_PERIODIC    = 44251,
    SPELL_SUMMON_ARCANE_SPHERE                  = 44265,
    SPELL_POWER_FEEDBACK                        = 44233,
    SPELL_FLAME_STRIKE                          = 46162,
    SPELL_SHOCK_BARRIER                         = 46165,
    SPELL_PYROBLAST                             = 36819,
    SPELL_PHOENIX                               = 44194,
    SPELL_EMOTE_TALK_EXCLAMATION                = 48348,
    SPELL_EMOTE_POINT                           = 48349,
    SPELL_EMOTE_ROAR                            = 48350,
    SPELL_CLEAR_FLIGHT                          = 44232,
    SPELL_QUITE_SUICIDE                         = 3617, // Serverside spell

    // Flame Strike
    SPELL_FLAME_STRIKE_DUMMY                    = 44191,
    SPELL_FLAME_STRIKE_DAMAGE                   = 44190,

    // Phoenix
    SPELL_REBIRTH                               = 44196,
    SPELL_BURN                                  = 44197,
    SPELL_EMBER_BLAST                           = 44199,
    SPELL_SUMMON_PHOENIX_EGG                    = 44195, // Serverside spell
    SPELL_FULL_HEAL                             = 17683
};

/**
 * @brief 重力牵引传送法术数组
 *
 * 包含5个不同方向的传送法术，用于将玩家传送到房间周围的不同位置
 * 这些法术会在重力牵引阶段按顺序对玩家施放
 */
uint32 gravityLapseTeleportSpells[] =
{
    SPELL_GRAVITY_LAPSE_LEFT_TELEPORT,
    SPELL_GRAVITY_LAPSE_FRONT_LEFT_TELEPORT,
    SPELL_GRAVITY_LAPSE_FRONT_TELEPORT,
    SPELL_GRAVITY_LAPSE_FRONT_RIGHT_TELEPORT,
    SPELL_GRAVITY_LAPSE_RIGHT_TELEPORT
};

/**
 * @brief 重力牵引伤害法术宏定义
 *
 * 根据副本难度返回不同的法术ID
 * 普通难度: 49887
 * 英雄难度: 44226
 */
#define SPELL_GRAVITY_LAPSE_DAMAGE  RAID_MODE<uint32>(49887, 44226)

/**
 * @brief 凯尔萨斯和凤凰的事件ID枚举
 *
 * 定义了战斗中各种事件的时间调度ID
 */
enum Events
{
    // Kael'thas Sunstrider
    EVENT_TALK_INTRO_1 = 1,
    EVENT_TALK_INTRO_2,
    EVENT_LAUGH_EMOTE,
    EVENT_FINISH_INTRO,
    EVENT_FIREBALL,
    EVENT_FLAME_STRIKE,
    EVENT_SHOCK_BARRIER,
    EVENT_PYROBLAST,
    EVENT_PHOENIX,
    EVENT_PREPARE_GRAVITY_LAPSE,
    EVENT_GRAVITY_LAPSE_CENTER_TELEPORT,
    EVENT_GRAVITY_LAPSE,
    EVENT_GRAVITY_LAPSE_BEAM_VISUAL_PERIODIC,
    EVENT_SUMMON_ARCANE_SPHERE,
    EVENT_POWER_FEEDBACK,
    EVENT_TALK_NEXT_GRAVITY_LAPSE,
    EVENT_EMOTE_TALK_EXCLAMATION,
    EVENT_EMOTE_POINT,
    EVENT_EMOTE_ROAR,
    EVENT_QUITE_SUICIDE,

    // Phoenix
    EVENT_ATTACK_PLAYERS,
    EVENT_HATCH_FROM_EGG,
    EVENT_REBIRTH,
    EVENT_PREPARE_REENGAGE
};

/**
 * @brief 凯尔萨斯战斗阶段枚举
 *
 * 定义了凯尔萨斯战斗的四个主要阶段
 */
enum Phases
{
    PHASE_INTRO = 0,
    PHASE_ONE   = 1,
    PHASE_TWO   = 2,
    PHASE_OUTRO = 3
};

/**
 * @brief 凯尔萨斯·逐日者（魔血形态）BOSS AI
 *
 * 实现了凯尔萨斯的所有战斗逻辑，包括：
 * - 开场对话序列
 * - 第一阶段：基础技能循环（火球、火焰打击、凤凰召唤）
 * - 第二阶段：重力牵引机制（50%生命值触发）
 * - 死亡序列：凯尔萨斯只能通过自杀法术死亡
 *
 * 战斗特点：
 * - 英雄难度会额外施放冲击屏障和炎爆术
 * - 重力牵引阶段会将玩家浮空并召唤奥术宝珠
 * - 凤凰会周期性死亡并重生
 */
struct boss_felblood_kaelthas : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_felblood_kaelthas(Creature* creature) : BossAI(creature, DATA_KAELTHAS_SUNSTRIDER)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 重置重力牵引相关的计数器和标志位
     * 在构造函数和Reset时调用
     */
    void Initialize()
    {
        _gravityLapseTargetCount = 0;  // 重力牵引目标计数器
        _firstGravityLapse = true;      // 是否为第一次重力牵引
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 初始化第一阶段的事件调度：
     * - 立即施放火球术
     * - 44秒后施放火焰打击
     * - 12秒后召唤凤凰
     * - 英雄难度：61秒后施放冲击屏障
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.SetPhase(PHASE_ONE);
        events.ScheduleEvent(EVENT_FIREBALL, 1ms, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_FLAME_STRIKE, 44s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_PHOENIX, 12s, 0, PHASE_ONE);
        if (IsHeroic())
            events.ScheduleEvent(EVENT_SHOCK_BARRIER, 1min + 1s, 0, PHASE_ONE);
    }

    /**
     * @brief 重置BOSS状态
     *
     * 调用基类重置函数，初始化变量，并设置回介绍阶段
     */
    void Reset() override
    {
        _Reset();
        Initialize();
        events.SetPhase(PHASE_INTRO);
    }

    /**
     * @brief BOSS死亡时调用
     * @param killer 击杀者（未使用）
     *
     * 不调用_JustDied()以避免重置事件导致死亡序列触发两次
     * 直接设置BOSS状态为完成
     */
    void JustDied(Unit* /*killer*/) override
    {
        // No _JustDied() here because otherwise we would reset the events which will trigger the death sequence twice.
        instance->SetBossState(DATA_KAELTHAS_SUNSTRIDER, DONE);
    }

    /**
     * @brief 进入脱战模式时调用
     * @param why 脱战原因
     *
     * 清除所有玩家的飞行状态，脱战并移除所有召唤物
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        DoCastAOE(SPELL_CLEAR_FLIGHT, true);
        _EnterEvadeMode();
        summons.DespawnAll();
        _DespawnAtEvade();
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 处理关键战斗逻辑：
     * 1. 致命伤害检查：触发死亡序列（PHASE_OUTRO）
     * 2. 50%生命值检查：进入第二阶段（重力牵引）
     * 3. 防止非自杀死亡：凯尔萨斯只能通过自杀法术死亡
     */
    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // Checking for lethal damage first so we trigger the outro phase without triggering phase two in case of oneshot attacks
        if (damage >= me->GetHealth() && !events.IsInPhase(PHASE_OUTRO))
        {
            me->AttackStop();
            me->SetReactState(REACT_PASSIVE);
            me->InterruptNonMeleeSpells(true);
            me->RemoveAurasDueToSpell(SPELL_POWER_FEEDBACK);
            summons.DespawnAll();
            DoCastAOE(SPELL_CLEAR_FLIGHT);
            Talk(SAY_DEATH);
            events.SetPhase(PHASE_OUTRO);
            events.ScheduleEvent(EVENT_EMOTE_TALK_EXCLAMATION, 1s, 0, PHASE_OUTRO);
            events.ScheduleEvent(EVENT_EMOTE_POINT, 3s + 800ms, 0, PHASE_OUTRO);
            events.ScheduleEvent(EVENT_EMOTE_ROAR, 7s + 400ms, 0, PHASE_OUTRO);
            events.ScheduleEvent(EVENT_EMOTE_ROAR, 10s, 0, PHASE_OUTRO);
            events.ScheduleEvent(EVENT_QUITE_SUICIDE, 11s, 0, PHASE_OUTRO);
        }

        // Phase two checks. Skip phase two if we are in the outro already
        if (me->HealthBelowPctDamaged(50, damage) && !events.IsInPhase(PHASE_TWO) && !events.IsInPhase(PHASE_OUTRO))
        {
            events.SetPhase(PHASE_TWO);
            events.ScheduleEvent(EVENT_PREPARE_GRAVITY_LAPSE, 1ms, 0, PHASE_TWO);
        }

        // Kael'thas may only kill himself via Quite Suicide
        if (damage >= me->GetHealth() && attacker != me)
            damage = me->GetHealth() - 1;
    }

    /**
     * @brief 设置数据时调用
     * @param type 数据类型
     * @param data 数据值
     *
     * 处理介绍阶段的触发：
     * - 当Kael'thas的小怪全部死亡时，实例脚本会调用此函数
     * - 触发开场对话序列
     */
    void SetData(uint32 type, uint32 /*data*/) override
    {
        if (type == DATA_KAELTHAS_INTRO)
        {
            // skip the intro if Kael'thas is engaged already
            if (!events.IsInPhase(PHASE_INTRO))
                return;

            me->SetImmuneToPC(true);
            events.ScheduleEvent(EVENT_TALK_INTRO_1, 6s, 0, PHASE_INTRO);
        }
    }

    /**
     * @brief 法术命中目标时调用
     * @param target 目标对象
     * @param spellInfo 法术信息
     *
     * 处理重力牵引相关法术效果：
     * - SPELL_GRAVITY_LAPSE_INITIAL：传送玩家到不同位置并施加飞行和伤害效果
     * - SPELL_CLEAR_FLIGHT：移除飞行和重力牵引伤害效果
     */
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        Unit* unitTarget = target->ToUnit();
        if (!unitTarget)
            return;

        switch (spellInfo->Id)
        {
            case SPELL_GRAVITY_LAPSE_INITIAL:
            {
                DoCast(unitTarget, gravityLapseTeleportSpells[_gravityLapseTargetCount], true);
                uint32 gravityLapseDamageSpell = SPELL_GRAVITY_LAPSE_DAMAGE;
                target->m_Events.AddEventAtOffset([target, gravityLapseDamageSpell]()
                {
                    target->CastSpell(target, gravityLapseDamageSpell);
                    target->CastSpell(target, SPELL_GRAVITY_LAPSE_FLY);

                }, 400ms);
                _gravityLapseTargetCount++;
                break;
            }
            case SPELL_CLEAR_FLIGHT:
                unitTarget->RemoveAurasDueToSpell(SPELL_GRAVITY_LAPSE_FLY);
                unitTarget->RemoveAurasDueToSpell(SPELL_GRAVITY_LAPSE_DAMAGE);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 召唤生物时调用
     * @param summon 被召唤的生物
     *
     * 处理召唤物的初始化：
     * - 奥术宝珠：跟随随机目标
     * - 火焰打击：施放火焰打击虚拟法术并在15秒后消失
     */
    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);

        switch (summon->GetEntry())
        {
            case NPC_ARCANE_SPHERE:
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 70.0f, true))
                    summon->GetMotionMaster()->MoveFollow(target, 0.0f, 0.0f);
                break;
            case NPC_FLAME_STRIKE:
                summon->CastSpell(summon, SPELL_FLAME_STRIKE_DUMMY);
                summon->DespawnOrUnsummon(15s);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要战斗逻辑循环：
     * - 检查是否有目标或处于介绍阶段
     * - 更新事件计时器
     * - 检查是否正在施法
     * - 处理各种事件的执行
     *
     * 事件处理包括：
     * - 介绍对话序列
     * - 火球术施放
     * - 火焰打击施放
     * - 冲击屏障和炎爆术（英雄难度）
     * - 凤凰召唤
     * - 重力牵引阶段处理
     * - 死亡序列动画
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() && !events.IsInPhase(PHASE_INTRO))
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_TALK_INTRO_1:
                    Talk(SAY_INTRO_1);
                    me->SetEmoteState(EMOTE_STATE_TALK);
                    events.ScheduleEvent(EVENT_TALK_INTRO_2, 20s + 600ms, 0, PHASE_INTRO);
                    events.ScheduleEvent(EVENT_LAUGH_EMOTE, 15s + 600ms, 0, PHASE_INTRO);
                    break;
                case EVENT_TALK_INTRO_2:
                    Talk(SAY_INTRO_2);
                    events.ScheduleEvent(EVENT_FINISH_INTRO, 15s + 500ms, 0, PHASE_INTRO);
                    break;
                case EVENT_LAUGH_EMOTE:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH_NO_SHEATHE);
                    break;
                case EVENT_FINISH_INTRO:
                    me->SetEmoteState(EMOTE_ONESHOT_NONE);
                    me->SetImmuneToPC(false);
                    break;
                case EVENT_FIREBALL:
                    DoCastVictim(SPELL_FIREBALL);
                    events.Repeat(2s + 500ms);
                    break;
                case EVENT_FLAME_STRIKE:
                    Talk(SAY_FLAME_STRIKE);
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 40.0f, true))
                        DoCast(target, SPELL_FLAME_STRIKE);
                    events.Repeat(44s);
                    break;
                case EVENT_SHOCK_BARRIER:
                    Talk(SAY_ANNOUNCE_PYROBLAST);
                    DoCastSelf(SPELL_SHOCK_BARRIER);
                    events.RescheduleEvent(EVENT_FIREBALL, 2s + 500ms, 0, PHASE_ONE);
                    events.ScheduleEvent(EVENT_PYROBLAST, 2s, 0, PHASE_ONE);
                    events.Repeat(1min);
                    break;
                case EVENT_PYROBLAST:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 40.0f, true))
                        DoCast(target, SPELL_PYROBLAST);
                    break;
                case EVENT_PHOENIX:
                    Talk(SAY_SUMMON_PHOENIX);
                    DoCastSelf(SPELL_PHOENIX);
                    events.Repeat(45s);
                    break;
                case EVENT_PREPARE_GRAVITY_LAPSE:
                    Talk(_firstGravityLapse ? SAY_GRAVITY_LAPSE_1 : SAY_GRAVITY_LAPSE_2);
                    _firstGravityLapse = false;
                    me->SetReactState(REACT_PASSIVE);
                    me->AttackStop();
                    me->GetMotionMaster()->Clear();
                    events.ScheduleEvent(EVENT_GRAVITY_LAPSE_CENTER_TELEPORT, 1s, 0, PHASE_TWO);
                    break;
                case EVENT_GRAVITY_LAPSE_CENTER_TELEPORT:
                    DoCastSelf(SPELL_GRAVITY_LAPSE_CENTER_TELEPORT);
                    events.ScheduleEvent(EVENT_GRAVITY_LAPSE, 1s, 0, PHASE_TWO);
                    break;
                case EVENT_GRAVITY_LAPSE:
                    _gravityLapseTargetCount = 0;
                    DoCastAOE(SPELL_GRAVITY_LAPSE_INITIAL);
                    events.ScheduleEvent(EVENT_SUMMON_ARCANE_SPHERE, 4s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_GRAVITY_LAPSE_BEAM_VISUAL_PERIODIC, 5s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_POWER_FEEDBACK, 35s, 0, PHASE_TWO);
                    break;
                case EVENT_GRAVITY_LAPSE_BEAM_VISUAL_PERIODIC:
                    DoCastAOE(SPELL_GRAVITY_LAPSE_BEAM_VISUAL_PERIODIC);
                    break;
                case EVENT_SUMMON_ARCANE_SPHERE:
                    for (uint8 i = 0; i < 3; i++)
                        DoCastSelf(SPELL_SUMMON_ARCANE_SPHERE, true);
                    break;
                case EVENT_POWER_FEEDBACK:
                    Talk(SAY_POWER_FEEDBACK);
                    DoCastAOE(SPELL_CLEAR_FLIGHT);
                    DoCastSelf(SPELL_POWER_FEEDBACK);
                    summons.DespawnEntry(NPC_ARCANE_SPHERE);
                    events.ScheduleEvent(EVENT_PREPARE_GRAVITY_LAPSE, 11s, 0, PHASE_TWO);
                    break;
                case EVENT_EMOTE_TALK_EXCLAMATION:
                    DoCastSelf(SPELL_EMOTE_TALK_EXCLAMATION);
                    break;
                case EVENT_EMOTE_POINT:
                    DoCastSelf(SPELL_EMOTE_POINT);
                    break;
                case EVENT_EMOTE_ROAR:
                    DoCastSelf(SPELL_EMOTE_ROAR);
                    break;
                case EVENT_QUITE_SUICIDE:
                    DoCastSelf(SPELL_QUITE_SUICIDE);
                    break;
                default:
                    break;
            }
        }
    }

private:
    uint8 _gravityLapseTargetCount;  ///< 重力牵引目标计数器，用于分配不同的传送位置
    bool _firstGravityLapse;         ///< 是否为第一次重力牵引，用于选择不同的对话文本
};

/**
 * @brief 凯尔萨斯的凤凰召唤物AI
 *
 * 实现凤凰的战斗逻辑：
 * - 持续燃烧效果（对周围造成伤害）
 * - 死亡时变为凤凰蛋
 * - 15秒后从蛋中重生
 * - 如果蛋被摧毁，凤凰会消失
 */
struct npc_felblood_kaelthas_phoenix : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_felblood_kaelthas_phoenix(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 设置凤凰为被动反应状态，初始化蛋状态标志
     */
    void Initialize()
    {
        me->SetReactState(REACT_PASSIVE);  // 设置为被动反应，等待事件激活
        _isInEgg = false;                   // 初始状态不是蛋形态
    }

    /**
     * @brief 被召唤时调用
     * @param summoner 召唤者
     *
     * 凤凰被召唤时：
     * - 进入战斗状态
     * - 施放燃烧效果
     * - 施放重生效果
     * - 2秒后开始攻击玩家
     */
    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        DoZoneInCombat();
        DoCastSelf(SPELL_BURN);
        DoCastSelf(SPELL_REBIRTH);
        _events.ScheduleEvent(EVENT_ATTACK_PLAYERS, 2s);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标（未使用）
     *
     * 空实现，因为凤凰在召唤时就已经进入战斗
     */
    void JustEngagedWith(Unit* /*who*/) override { }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 处理凤凰死亡逻辑：
     * - 致命伤害时变为凤凰蛋
     * - 施放余烬爆炸
     * - 召唤凤凰蛋
     * - 15秒后从蛋中重生
     * - 防止真正死亡（将伤害设为生命值-1）
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth())
        {
            if (!_isInEgg)
            {
                me->AttackStop();
                me->SetReactState(REACT_PASSIVE);
                me->RemoveAllAuras();
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                DoCastSelf(SPELL_EMBER_BLAST);
                // DoCastSelf(SPELL_SUMMON_PHOENIX_EGG); -- We do a manual summon for now. Feel free to move it to spelleffect_dbc
                if (Creature* egg = DoSummon(NPC_PHOENIX_EGG, me->GetPosition(), 0s))
                {
                    if (Creature* kaelthas = _instance->GetCreature(DATA_KAELTHAS_SUNSTRIDER))
                    {
                        kaelthas->AI()->JustSummoned(egg);
                        _eggGUID = egg->GetGUID();
                    }
                }

                _events.ScheduleEvent(EVENT_HATCH_FROM_EGG, 15s);
                _isInEgg = true;
            }
            damage = me->GetHealth() - 1;
        }

    }

    /**
     * @brief 召唤物死亡时调用
     * @param summon 死亡的召唤物
     * @param killer 击杀者
     *
     * 当凤凰蛋在15秒内被摧毁时，凤凰会消失
     */
    void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/) override
    {
        // Egg has been destroyed within 15 seconds so we lose the phoenix.
        me->DespawnOrUnsummon();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 处理凤凰的各种状态转换：
     * - 攻击玩家事件：激活攻击行为
     * - 从蛋中孵化事件：移除蛋并重生
     * - 重生事件：施放重生效果
     * - 重新参战事件：恢复满血，移除蛋状态标志，重新施放燃烧效果
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ATTACK_PLAYERS:
                    me->SetReactState(REACT_AGGRESSIVE);
                    break;
                case EVENT_HATCH_FROM_EGG:
                    if (Creature* egg = ObjectAccessor::GetCreature(*me, _eggGUID))
                        egg->DespawnOrUnsummon();
                    me->RemoveAllAuras();
                    _events.ScheduleEvent(EVENT_REBIRTH, 2s);
                    break;
                case EVENT_REBIRTH:
                    DoCastSelf(SPELL_REBIRTH);
                    _events.ScheduleEvent(EVENT_PREPARE_REENGAGE, 2s);
                    break;
                case EVENT_PREPARE_REENGAGE:
                    _isInEgg = false;
                    DoCastSelf(SPELL_FULL_HEAL);
                    DoCastSelf(SPELL_BURN);
                    me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    _events.ScheduleEvent(EVENT_ATTACK_PLAYERS, 2s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }
private:
    InstanceScript* _instance;     ///< 副本脚本实例指针
    EventMap _events;              ///< 事件映射表，用于调度凤凰的各种行为
    bool _isInEgg;                 ///< 是否处于蛋形态
    ObjectGuid _eggGUID;           ///< 凤凰蛋的GUID
};

/**
 * @brief 火焰打击法术光环脚本
 *
 * 处理火焰打击法术的效果：
 * - 火焰打击先施放一个虚拟法术（SPELL_FLAME_STRIKE_DUMMY）
 * - 光环移除后施放实际伤害法术（SPELL_FLAME_STRIKE_DAMAGE）
 * - 这造成了视觉预警和实际伤害之间的延迟效果
 */
// 44191 - Flame Strike
class spell_felblood_kaelthas_flame_strike : public AuraScript
{
    PrepareAuraScript(spell_felblood_kaelthas_flame_strike);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息
     * @return 验证是否成功
     *
     * 验证火焰打击伤害法术是否存在
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLAME_STRIKE_DAMAGE });
    }

    /**
     * @brief 光环移除后调用
     * @param aurEff 光环效果
     * @param mode 光环效果处理模式
     *
     * 光环移除时施放实际的火焰打击伤害法术
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* target = GetTarget())
            target->CastSpell(target, SPELL_FLAME_STRIKE_DAMAGE);
    }

    /**
     * @brief 注册光环脚本
     *
     * 注册光环移除后的回调函数
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_felblood_kaelthas_flame_strike::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册凯尔萨斯相关脚本
 *
 * 注册以下脚本：
 * - boss_felblood_kaelthas：凯尔萨斯BOSS AI
 * - npc_felblood_kaelthas_phoenix：凤凰召唤物AI
 * - spell_felblood_kaelthas_flame_strike：火焰打击法术脚本
 */
void AddSC_boss_felblood_kaelthas()
{
    RegisterMagistersTerraceCreatureAI(boss_felblood_kaelthas);
    RegisterMagistersTerraceCreatureAI(npc_felblood_kaelthas_phoenix);
    RegisterSpellScript(spell_felblood_kaelthas_flame_strike);
}
