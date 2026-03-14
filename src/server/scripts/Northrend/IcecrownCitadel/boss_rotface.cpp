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
 * @file boss_rotface.cpp
 * @brief 冰冠堡垒瘟疫区第一个首领 - 腐脸的战斗脚本
 *
 * 本模块实现了冰冠堡垒瘟疫区第一个首领腐脸的完整战斗逻辑。
 *
 * 战斗机制概述：
 * 1. 泥浆喷射：周期性对随机方向喷射泥浆，生成小软泥怪
 * 2. 变异感染：随机感染玩家，结束后生成小软泥怪
 * 3. 软泥怪合并：小软泥怪合并成大软泥怪，叠加不稳定软泥
 * 4. 不稳定软泥爆炸：大软泥怪叠加5层后爆炸，造成大量伤害
 *
 * 英雄模式差异：
 * - 增加恶性气体技能
 * - 更频繁的感染
 *
 * 已知问题：
 * - 泥浆喷射没有直接在目标点的动画
 */

#include "icecrown_citadel.h"
#include "Containers.h"
#include "GridNotifiers.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 文本ID枚举
 *
 * 定义腐脸和软泥怪战斗中的各种台词和表情文本
 */
enum Texts
{
    SAY_PRECIOUS_DIES           = 0,  ///< 宝贝死亡时的台词
    SAY_AGGRO                   = 1,  ///< 开怪台词
    EMOTE_SLIME_SPRAY           = 2,  ///< 泥浆喷射表情
    SAY_SLIME_SPRAY             = 3,  ///< 泥浆喷射台词
    SAY_UNSTABLE_EXPLOSION      = 5,  ///< 不稳定软泥爆炸台词
    SAY_KILL                    = 6,  ///< 击杀玩家台词
    SAY_BERSERK                 = 7,  ///< 狂暴台词
    SAY_DEATH                   = 8,  ///< 死亡台词
    EMOTE_MUTATED_INFECTION     = 9,  ///< 变异感染表情

    EMOTE_UNSTABLE_2            = 0,  ///< 不稳定软泥2层表情
    EMOTE_UNSTABLE_3            = 1,  ///< 不稳定软泥3层表情
    EMOTE_UNSTABLE_4            = 2,  ///< 不稳定软泥4层表情
    EMOTE_UNSTABLE_EXPLOSION    = 3,  ///< 不稳定软泥爆炸表情

    EMOTE_PRECIOUS_ZOMBIES      = 0,  ///< 宝贝召唤僵尸表情
};

/**
 * @brief 法术ID枚举
 *
 * 定义战斗中使用的所有法术ID
 */
enum Spells
{
    // Rotface - 腐脸
    SPELL_SLIME_SPRAY                       = 69508,    ///< 泥浆喷射 - 每20秒施放
    SPELL_MUTATED_INFECTION                 = 69674,    ///< 变异感染 - 每1:30加速
    SPELL_VILE_GAS_TRIGGER_SUMMON           = 72287,    ///< 恶性气体触发召唤

    // Oozes - 软泥怪
    SPELL_LITTLE_OOZE_COMBINE               = 69537,    ///< 小软泥合并 - 合并2个小软泥
    SPELL_LARGE_OOZE_COMBINE                = 69552,    ///< 大软泥合并 - 合并2个大软泥
    SPELL_LARGE_OOZE_BUFF_COMBINE           = 69611,    ///< 大软泥增益合并 - 合并大软泥和小软泥
    SPELL_OOZE_MERGE                        = 69889,    ///< 软泥合并 - 2个小软泥召唤大软泥
    SPELL_WEAK_RADIATING_OOZE               = 69750,    ///< 弱辐射软泥 - 被动伤害光环（小）
    SPELL_RADIATING_OOZE                    = 69760,    ///< 辐射软泥 - 被动伤害光环（大）
    SPELL_UNSTABLE_OOZE                     = 69558,    ///< 不稳定软泥 - 伤害加成和爆炸计数器
    SPELL_GREEN_ABOMINATION_HITTIN__YA_PROC = 70001,    ///< 绿色憎恶击中你触发 - 防止被感染击中
    SPELL_UNSTABLE_OOZE_EXPLOSION           = 69839,    ///< 不稳定软泥爆炸
    SPELL_STICKY_OOZE                       = 69774,    ///< 粘稠软泥
    SPELL_UNSTABLE_OOZE_EXPLOSION_TRIGGER   = 69832,    ///< 不稳定软泥爆炸触发
    SPELL_VERTEX_COLOR_PINK                 = 53213,    ///< 粉色顶点着色
    SPELL_VERTEX_COLOR_BRIGHT_RED           = 69844,    ///< 亮红色顶点着色
    SPELL_VERTEX_COLOR_DARK_RED             = 44773,    ///< 暗红色顶点着色

    // Precious - 宝贝（房间内的小怪）
    SPELL_MORTAL_WOUND                      = 71127,    ///< 致命伤口 - 治疗减益
    SPELL_DECIMATE                          = 71123,    ///< 毁灭 - 降低生命值
    SPELL_AWAKEN_PLAGUED_ZOMBIES            = 71159,    ///< 唤醒瘟疫僵尸

    // Professor Putricide - 普崔塞德教授
    SPELL_VILE_GAS_H                        = 72272,    ///< 恶性气体（英雄）
    SPELL_VILE_GAS_TRIGGER                  = 72285,    ///< 恶性气体触发
};

#define MUTATED_INFECTION RAID_MODE<int32>(69674, 71224, 73022, 73023)

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中的各种计时器事件
 */
enum Events
{
    // Rotface - 腐脸
    EVENT_SLIME_SPRAY       = 1,  ///< 泥浆喷射事件
    EVENT_HASTEN_INFECTIONS = 2,  ///< 加速感染事件
    EVENT_MUTATED_INFECTION = 3,  ///< 变异感染事件
    EVENT_VILE_GAS          = 4,  ///< 恶性气体事件

    // Precious - 宝贝
    EVENT_DECIMATE          = 5,  ///< 毁灭事件
    EVENT_MORTAL_WOUND      = 6,  ///< 致命伤口事件
    EVENT_SUMMON_ZOMBIES    = 7,  ///< 召唤僵尸事件

    EVENT_STICKY_OOZE       = 8,  ///< 粘稠软泥事件
};

struct boss_rotface : public BossAI
{
    boss_rotface(Creature* creature) : BossAI(creature, DATA_ROTFACE)
    {
        infectionStage = 0;
        infectionCooldown = 14s;
    }

    void Reset() override
    {
        _Reset();
        events.ScheduleEvent(EVENT_SLIME_SPRAY, 20s);
        events.ScheduleEvent(EVENT_HASTEN_INFECTIONS, 90s);
        events.ScheduleEvent(EVENT_MUTATED_INFECTION, 14s);
        if (IsHeroic())
            events.ScheduleEvent(EVENT_VILE_GAS, 22s, 27s);

        infectionStage = 0;
        infectionCooldown = 14s;
    }

    void JustEngagedWith(Unit* who) override
    {
        if (!instance->CheckRequiredBosses(DATA_ROTFACE, who->ToPlayer()))
        {
            EnterEvadeMode(EVADE_REASON_OTHER);
            instance->DoCastSpellOnPlayers(LIGHT_S_HAMMER_TELEPORT);
            return;
        }

        me->setActive(true);
        Talk(SAY_AGGRO);
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->DoAction(ACTION_ROTFACE_COMBAT);

        DoZoneInCombat();
        DoCastSelf(SPELL_GREEN_ABOMINATION_HITTIN__YA_PROC, true);
    }

    void JustDied(Unit* /*killer*/) override
    {
        instance->DoRemoveAurasDueToSpellOnPlayers(MUTATED_INFECTION);
        _JustDied();
        Talk(SAY_DEATH);
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->DoAction(ACTION_ROTFACE_DEATH);
    }

    void JustReachedHome() override
    {
        _JustReachedHome();
        instance->SetBossState(DATA_ROTFACE, FAIL);
        instance->SetData(DATA_OOZE_DANCE_ACHIEVEMENT, uint32(true));   // reset
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        ScriptedAI::EnterEvadeMode(why);
        if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            professor->AI()->EnterEvadeMode();
    }

    void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_SLIME_SPRAY)
            Talk(SAY_SLIME_SPRAY);
    }

    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_VILE_GAS_STALKER)
            if (Creature* professor = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
                professor->CastSpell(summon, SPELL_VILE_GAS_H, true);

        summons.Summon(summon);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SLIME_SPRAY:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                    {
                        DoSummon(NPC_OOZE_SPRAY_STALKER, *target, 8s, TEMPSUMMON_TIMED_DESPAWN);
                        Talk(EMOTE_SLIME_SPRAY);
                        DoCastSelf(SPELL_SLIME_SPRAY);
                    }
                    events.ScheduleEvent(EVENT_SLIME_SPRAY, 20s);
                    break;
                case EVENT_HASTEN_INFECTIONS:
                    if (infectionStage++ < 4)
                    {
                        infectionCooldown -= 2s;
                        events.ScheduleEvent(EVENT_HASTEN_INFECTIONS, 90s);
                    }
                    break;
                case EVENT_MUTATED_INFECTION:
                    DoCastAOE(SPELL_MUTATED_INFECTION);
                    events.ScheduleEvent(EVENT_MUTATED_INFECTION, infectionCooldown);
                    break;
                case EVENT_VILE_GAS:
                    DoCastAOE(SPELL_VILE_GAS_TRIGGER);
                    events.ScheduleEvent(EVENT_VILE_GAS, 30s, 35s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    Milliseconds infectionCooldown;
    uint32 infectionStage;
};

struct npc_little_ooze : public ScriptedAI
{
    npc_little_ooze(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(WorldObject* summonerWO) override
    {
        Unit* summoner = summonerWO->ToUnit();
        if (!summoner)
            return;
        DoCastSelf(SPELL_LITTLE_OOZE_COMBINE, true);
        DoCastSelf(SPELL_WEAK_RADIATING_OOZE, true);
        DoCastSelf(SPELL_GREEN_ABOMINATION_HITTIN__YA_PROC, true);
        events.ScheduleEvent(EVENT_STICKY_OOZE, 5s);
        AddThreat(summoner, 500000.0f);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (events.ExecuteEvent() == EVENT_STICKY_OOZE)
        {
            DoCastVictim(SPELL_STICKY_OOZE);
            events.ScheduleEvent(EVENT_STICKY_OOZE, 15s);
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap events;
};

struct npc_big_ooze : public ScriptedAI
{
    npc_big_ooze(Creature* creature) : ScriptedAI(creature), instance(creature->GetInstanceScript()) { }

    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        DoCastSelf(SPELL_LARGE_OOZE_COMBINE, true);
        DoCastSelf(SPELL_LARGE_OOZE_BUFF_COMBINE, true);
        DoCastSelf(SPELL_RADIATING_OOZE, true);
        DoCastSelf(SPELL_UNSTABLE_OOZE, true);
        DoCastSelf(SPELL_GREEN_ABOMINATION_HITTIN__YA_PROC, true);
        events.ScheduleEvent(EVENT_STICKY_OOZE, 5s);
        // register in Rotface's summons - not summoned with Rotface as owner
        if (Creature* rotface = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_ROTFACE)))
            rotface->AI()->JustSummoned(me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* rotface = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_ROTFACE)))
            rotface->AI()->SummonedCreatureDespawn(me);
        me->DespawnOrUnsummon();
    }

    void DoAction(int32 action) override
    {
        if (action == EVENT_STICKY_OOZE)
            events.CancelEvent(EVENT_STICKY_OOZE);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_STICKY_OOZE:
                    DoCastVictim(SPELL_STICKY_OOZE);
                    events.ScheduleEvent(EVENT_STICKY_OOZE, 15s);
                    break;
                default:
                    break;
            }
        }

        if (me->IsVisible())
            DoMeleeAttackIfReady();
    }

private:
    EventMap events;
    InstanceScript* instance;
};

struct npc_precious_icc : public ScriptedAI
{
    npc_precious_icc(Creature* creature) : ScriptedAI(creature), _summons(me), _instance(creature->GetInstanceScript()) { }

    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_DECIMATE, 20s, 25s);
        _events.ScheduleEvent(EVENT_MORTAL_WOUND, 3s, 7s);
        _events.ScheduleEvent(EVENT_SUMMON_ZOMBIES, 20s, 22s);
        _summons.DespawnAll();
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            summon->AI()->AttackStart(target);
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        _summons.Despawn(summon);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _summons.DespawnAll();
        if (Creature* rotface = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_ROTFACE)))
            if (rotface->IsAlive())
                rotface->AI()->Talk(SAY_PRECIOUS_DIES);
    }

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
                case EVENT_DECIMATE:
                    DoCastVictim(SPELL_DECIMATE);
                    _events.ScheduleEvent(EVENT_DECIMATE, 20s, 25s);
                    break;
                case EVENT_MORTAL_WOUND:
                    DoCastVictim(SPELL_MORTAL_WOUND);
                    _events.ScheduleEvent(EVENT_MORTAL_WOUND, 10s, 12500ms);
                    break;
                case EVENT_SUMMON_ZOMBIES:
                    Talk(EMOTE_PRECIOUS_ZOMBIES);
                    for (uint32 i = 0; i < 11; ++i)
                        DoCastSelf(SPELL_AWAKEN_PLAGUED_ZOMBIES, false);
                    _events.ScheduleEvent(EVENT_SUMMON_ZOMBIES, 20s, 22s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;
    SummonList _summons;
    InstanceScript* _instance;
};

// 69782, 69796, 69798, 69801 - Ooze Flood
class spell_rotface_ooze_flood : public SpellScript
{
    PrepareSpellScript(spell_rotface_ooze_flood);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (!GetHitUnit())
            return;

        std::list<Creature*> triggers;
        GetHitUnit()->GetCreatureListWithEntryInGrid(triggers, GetHitUnit()->GetEntry(), 12.5f);

        if (triggers.empty())
            return;

        triggers.sort(Trinity::ObjectDistanceOrderPred(GetHitUnit()));
        GetHitUnit()->CastSpell(triggers.back(), uint32(GetEffectValue()), GetOriginalCaster() ? GetOriginalCaster()->GetGUID() : ObjectGuid::Empty);
    }

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        // get 2 targets except 2 nearest
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));

        // .resize() runs pop_back();
        if (targets.size() > 5)
            targets.resize(5);

        while (targets.size() > 2)
            targets.pop_front();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rotface_ooze_flood::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_rotface_ooze_flood::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

// 69674, 71224, 73022, 73023 - Mutated Infection
class spell_rotface_mutated_infection : public SpellScript
{
    PrepareSpellScript(spell_rotface_mutated_infection);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        // remove targets with this aura already
        // tank is not on this list
        targets.remove_if(Trinity::UnitAuraCheck(true, GetSpellInfo()->Id));
        if (targets.empty())
            return;

        WorldObject* target = Trinity::Containers::SelectRandomContainerElement(targets);
        targets.clear();
        targets.push_back(target);
    }

    void NotifyTargets()
    {
        if (Creature* caster = GetCaster()->ToCreature())
            if (Unit* target = GetHitUnit())
                caster->AI()->Talk(EMOTE_MUTATED_INFECTION, target);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_rotface_mutated_infection::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
        AfterHit += SpellHitFn(spell_rotface_mutated_infection::NotifyTargets);
    }
};

class spell_rotface_mutated_infection_aura : public AuraScript
{
    PrepareAuraScript(spell_rotface_mutated_infection_aura);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ static_cast<uint32>(spellInfo->GetEffect(EFFECT_2).CalcValue()) });
    }

    void HandleEffectRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, uint32(GetEffectInfo(EFFECT_2).CalcValue()), { aurEff, GetCasterGUID() });
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_rotface_mutated_infection_aura::HandleEffectRemove, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

// 69538 - Small Ooze Combine
class spell_rotface_little_ooze_combine : public SpellScript
{
    PrepareSpellScript(spell_rotface_little_ooze_combine);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* caster = GetCaster()->ToCreature();
        if (!(GetHitCreature() && GetHitUnit()->IsAlive()) || !caster)
            return;

        caster->RemoveAurasDueToSpell(SPELL_LITTLE_OOZE_COMBINE);
        GetHitCreature()->RemoveAurasDueToSpell(SPELL_LITTLE_OOZE_COMBINE);
        GetHitCreature()->CastSpell(caster, SPELL_OOZE_MERGE, true);
        GetHitCreature()->DespawnOrUnsummon();
        caster->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rotface_little_ooze_combine::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 69553 - Large Ooze Combine
class spell_rotface_large_ooze_combine : public SpellScript
{
    PrepareSpellScript(spell_rotface_large_ooze_combine);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* caster = GetCaster()->ToCreature();
        Creature* target = GetHitCreature();
        if (!(target && target->IsAlive()) || !caster)
            return;

        if (Aura* unstable = caster->GetAura(SPELL_UNSTABLE_OOZE))
        {
            if (Aura* targetAura = target->GetAura(SPELL_UNSTABLE_OOZE))
                unstable->ModStackAmount(targetAura->GetStackAmount());
            else
                unstable->ModStackAmount(1);

            if (unstable->GetStackAmount() >= 5)
            {
                caster->RemoveAurasDueToSpell(SPELL_LARGE_OOZE_BUFF_COMBINE);
                caster->RemoveAurasDueToSpell(SPELL_LARGE_OOZE_COMBINE);
                if (InstanceScript* instance = caster->GetInstanceScript())
                {
                    if (Creature* rotface = ObjectAccessor::GetCreature(*caster, instance->GetGuidData(DATA_ROTFACE)))
                    {
                        instance->SetData(DATA_OOZE_DANCE_ACHIEVEMENT, uint32(false));
                        if (rotface->IsAlive())
                            rotface->AI()->Talk(SAY_UNSTABLE_EXPLOSION);
                    }
                }

                caster->AI()->DoAction(EVENT_STICKY_OOZE);
                caster->CastSpell(caster, SPELL_UNSTABLE_OOZE_EXPLOSION, CastSpellExtraArgs().SetOriginalCaster(caster->GetGUID()));
            }
        }
        target->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rotface_large_ooze_combine::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 69610 - Large Ooze Buff Combine
class spell_rotface_large_ooze_buff_combine : public SpellScript
{
    PrepareSpellScript(spell_rotface_large_ooze_buff_combine);

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_UNIT;
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (!(GetHitCreature() && GetHitCreature()->IsAlive()))
            return;

        Creature* caster = GetCaster()->ToCreature();
        if (Aura* unstable = caster->GetAura(SPELL_UNSTABLE_OOZE))
        {
            uint8 newStack = uint8(unstable->GetStackAmount()+1);
            unstable->SetStackAmount(newStack);

            switch (newStack)
            {
                case 2:
                    caster->AI()->Talk(EMOTE_UNSTABLE_2, caster);
                    caster->CastSpell(caster, SPELL_VERTEX_COLOR_PINK, true);
                    break;
                case 3:
                    caster->AI()->Talk(EMOTE_UNSTABLE_3, caster);
                    caster->CastSpell(caster, SPELL_VERTEX_COLOR_BRIGHT_RED, true);
                    break;
                case 4:
                    caster->AI()->Talk(EMOTE_UNSTABLE_4, caster);
                    caster->CastSpell(caster, SPELL_VERTEX_COLOR_DARK_RED, true);
                    break;
                case 5:
                {
                    caster->AI()->Talk(EMOTE_UNSTABLE_EXPLOSION);
                    caster->RemoveAurasDueToSpell(SPELL_LARGE_OOZE_BUFF_COMBINE);
                    caster->RemoveAurasDueToSpell(SPELL_LARGE_OOZE_COMBINE);
                    if (InstanceScript* instance = caster->GetInstanceScript())
                    {
                        instance->SetData(DATA_OOZE_DANCE_ACHIEVEMENT, uint32(false));
                        if (Creature* rotface = ObjectAccessor::GetCreature(*caster, instance->GetGuidData(DATA_ROTFACE)))
                            if (rotface->IsAlive())
                                rotface->AI()->Talk(SAY_UNSTABLE_EXPLOSION);
                    }

                    caster->AI()->DoAction(EVENT_STICKY_OOZE);
                    caster->CastSpell(caster, SPELL_UNSTABLE_OOZE_EXPLOSION, CastSpellExtraArgs().SetOriginalCaster(caster->GetGUID()));
                    break;
                }
                default:
                    break;
            }
        }

        GetHitCreature()->DespawnOrUnsummon();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rotface_large_ooze_buff_combine::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 69839 - Unstable Ooze Explosion
class spell_rotface_unstable_ooze_explosion_init : public SpellScript
{
    PrepareSpellScript(spell_rotface_unstable_ooze_explosion_init);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_UNSTABLE_OOZE_EXPLOSION_TRIGGER });
    }

    void HandleCast(SpellEffIndex effIndex)
    {
        PreventHitEffect(effIndex);
        if (!GetHitUnit())
            return;

        float x, y, z;
        GetHitUnit()->GetPosition(x, y, z);
        Creature* dummy = GetCaster()->SummonCreature(NPC_UNSTABLE_EXPLOSION_STALKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 1min);
        GetCaster()->CastSpell(dummy, SPELL_UNSTABLE_OOZE_EXPLOSION_TRIGGER, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rotface_unstable_ooze_explosion_init::HandleCast, EFFECT_0, SPELL_EFFECT_FORCE_CAST);
    }
};

// 69832 - Unstable Ooze Explosion
class spell_rotface_unstable_ooze_explosion : public SpellScript
{
    PrepareSpellScript(spell_rotface_unstable_ooze_explosion);

    void CheckTarget(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (!GetExplTargetDest())
            return;

        uint32 triggered_spell_id = GetEffectInfo().TriggerSpell;

        // let Rotface handle the cast - caster dies before this executes
        if (InstanceScript* script = GetCaster()->GetInstanceScript())
            if (Creature* rotface = script->instance->GetCreature(script->GetGuidData(DATA_ROTFACE)))
                rotface->CastSpell(*GetExplTargetDest(), triggered_spell_id, GetCaster()->GetGUID());
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_rotface_unstable_ooze_explosion::CheckTarget, EFFECT_0, SPELL_EFFECT_TRIGGER_MISSILE);
    }
};

// 71441 - Unstable Ooze Explosion Suicide Trigger
class spell_rotface_unstable_ooze_explosion_suicide : public AuraScript
{
    PrepareAuraScript(spell_rotface_unstable_ooze_explosion_suicide);

    void DespawnSelf(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();
        Unit* target = GetTarget();
        if (target->GetTypeId() != TYPEID_UNIT)
            return;

        target->RemoveAllAuras();
        target->SetVisible(false);
        target->ToCreature()->DespawnOrUnsummon(60s);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_rotface_unstable_ooze_explosion_suicide::DespawnSelf, EFFECT_2, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

// 72285, 72288 - Vile Gas Trigger
class spell_rotface_vile_gas_trigger : public SpellScript
{
    PrepareSpellScript(spell_rotface_vile_gas_trigger);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));
        if (targets.empty())
            return;

        std::list<WorldObject*> ranged, melee;
        std::list<WorldObject*>::iterator itr = targets.begin();
        while (itr != targets.end() && (*itr)->GetDistance(GetCaster()) < 5.0f)
        {
            melee.push_back((*itr)->ToUnit());
            ++itr;
        }

        while (itr != targets.end())
        {
            ranged.push_back((*itr)->ToUnit());
            ++itr;
        }

        uint32 minTargets = GetCaster()->GetMap()->Is25ManRaid() ? 8 : 3;
        while (ranged.size() < minTargets)
        {
            if (melee.empty())
                break;

            WorldObject* target = Trinity::Containers::SelectRandomContainerElement(melee);
            ranged.push_back(target);
            melee.remove(target);
        }

        if (!ranged.empty())
            Trinity::Containers::RandomResize(ranged, GetCaster()->GetMap()->Is25ManRaid() ? 3 : 1);

        targets.swap(ranged);
    }

    void HandleDummy(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        GetCaster()->CastSpell(GetHitUnit(), SPELL_VILE_GAS_TRIGGER_SUMMON);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_rotface_vile_gas_trigger::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget += SpellEffectFn(spell_rotface_vile_gas_trigger::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 69507, 71213, 73189, 73190 - Slime Spray
class spell_rotface_slime_spray : public SpellScript
{
    PrepareSpellScript(spell_rotface_slime_spray);

    void HandleResidue()
    {
        Player* target = GetHitPlayer();
        if (!target)
            return;

        if (target->HasAura(SPELL_GREEN_BLIGHT_RESIDUE))
            return;

        uint32 questId = target->GetMap()->Is25ManRaid() ? QUEST_RESIDUE_RENDEZVOUS_25 : QUEST_RESIDUE_RENDEZVOUS_10;
        if (target->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            return;

        target->CastSpell(target, SPELL_GREEN_BLIGHT_RESIDUE, TRIGGERED_FULL_MASK);
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_rotface_slime_spray::HandleResidue);
    }
};

void AddSC_boss_rotface()
{
    // Creatures
    RegisterIcecrownCitadelCreatureAI(boss_rotface);
    RegisterIcecrownCitadelCreatureAI(npc_little_ooze);
    RegisterIcecrownCitadelCreatureAI(npc_big_ooze);
    RegisterIcecrownCitadelCreatureAI(npc_precious_icc);

    // Spells
    RegisterSpellScript(spell_rotface_ooze_flood);
    RegisterSpellAndAuraScriptPair(spell_rotface_mutated_infection, spell_rotface_mutated_infection_aura);
    RegisterSpellScript(spell_rotface_little_ooze_combine);
    RegisterSpellScript(spell_rotface_large_ooze_combine);
    RegisterSpellScript(spell_rotface_large_ooze_buff_combine);
    RegisterSpellScript(spell_rotface_unstable_ooze_explosion_init);
    RegisterSpellScript(spell_rotface_unstable_ooze_explosion);
    RegisterSpellScript(spell_rotface_unstable_ooze_explosion_suicide);
    RegisterSpellScript(spell_rotface_vile_gas_trigger);
    RegisterSpellScript(spell_rotface_slime_spray);
}
