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
 * @file boss_teron_gorefiend.cpp
 * @brief 泰隆·血魔Boss战脚本
 *
 * 本模块实现了泰隆·血魔的完整战斗逻辑，包括：
 * - 泰隆·血魔的技能施放（焚化、粉碎暗影、死亡之影、末日之花）
 * - 末日之花的AI（悬停飞行并施放暗影箭）
 * - 暗影构造体的AI（随机攻击目标并施放萎缩）
 * - 死亡之影法术脚本（玩家死亡后变成复仇之魂）
 * - 复仇之魂的技能系统（灵魂打击、灵魂链、灵魂齐射等）
 * - 玩家控制复仇之魂击杀暗影构造体的机制
 *
 * 战斗机制：
 * 1. 泰隆·血魔会随机施放焚化、粉碎暗影和死亡之影
 * 2. 被死亡之影影响的玩家将在55秒后死亡，变成复仇之魂
 * 3. 玩家控制复仇之魂使用技能击杀4个暗影构造体
 * 4. 末日之花会持续施放暗影箭攻击随机目标
 * 5. 10分钟后进入狂暴状态
 */

#include "ScriptMgr.h"
#include "black_temple.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 对白枚举
 */
enum Says
{
    SAY_INTRO      = 0,  ///< 开场对白
    SAY_AGGRO      = 1,  ///< 战斗开始对白
    SAY_SLAY       = 2,  ///< 击杀玩家对白
    SAY_INCINERATE = 3,  ///< 施放焚化时对白
    SAY_BLOSSOM    = 4,  ///< 召唤末日之花时对白
    SAY_CRUSHING   = 5,  ///< 施放粉碎暗影时对白
    SAY_DEATH      = 6   ///< 死亡对白
};

/**
 * @brief 技能枚举
 */
enum Spells
{
    //泰隆·血魔技能
    SPELL_INCINERATE                 = 40239,  ///< 焚化：造成火焰伤害
    SPELL_CRUSHING_SHADOWS           = 40243,  ///< 粉碎暗影：对多个目标造成暗影伤害
    SPELL_SHADOW_OF_DEATH            = 40251,  ///< 死亡之影：55秒后玩家死亡并变成复仇之魂
    SPELL_SHADOW_OF_DEATH_REMOVE     = 41999,  ///< 死亡之影移除：清理所有相关光环
    SPELL_BERSERK                    = 45078,  ///< 狂暴：10分钟后进入狂暴状态
    SPELL_SUMMON_DOOM_BLOSSOM        = 40188,  ///< 召唤末日之花

    //末日之花技能
    SPELL_SUMMON_BLOSSOM_MOVE_TARGET = 40186,  ///< 末日之花移动目标
    SPELL_SHADOWBOLT                 = 40185,  ///< 暗影箭：对随机目标造成暗影伤害

    //暗影构造体技能
    SPELL_ATROPHY                    = 40327,  ///< 萎缩：降低目标的攻击速度和施法速度

    //玩家技能
    SPELL_SUMMON_SPIRIT              = 40266,  ///< 召唤复仇之魂：玩家死亡后召唤
    SPELL_SPIRITUAL_VENGEANCE        = 40268,  ///< 精神复仇：控制复仇之魂的光环
    SPELL_POSSESS_SPIRIT_IMMUNE      = 40282,  ///< 占据灵魂免疫：使玩家免疫某些效果
    SPELL_SUMMON_SKELETRON_1         = 40270,  ///< 召唤骷髅构造体1
    SPELL_SUMMON_SKELETRON_2         = 41948,  ///< 召唤骷髅构造体2
    SPELL_SUMMON_SKELETRON_3         = 41949,  ///< 召唤骷髅构造体3
    SPELL_SUMMON_SKELETRON_4         = 41950,  ///< 召唤骷髅构造体4

    //复仇之魂技能
    SPELL_SPIRIT_STRIKE              = 40325,  ///< 灵魂打击：近战攻击技能
    SPELL_SPIRIT_CHAINS              = 40175,  ///< 灵魂锁链：控制构造体
    SPELL_SPIRIT_VOLLEY              = 40314,  ///< 灵魂齐射：范围暗影伤害
    SPELL_SPIRIT_SHIELD              = 40322,  ///< 灵魂护盾：保护玩家
    SPELL_SPIRIT_LANCE               = 40157   ///< 灵魂长矛：远程攻击
};

/**
 * @brief NPC枚举
 */
enum Npcs
{
    NPC_DOOM_BLOSSOM      = 23123,  ///< 末日之花
    NPC_SHADOWY_CONSTRUCT = 23111,  ///< 暗影构造体（玩家需要击杀的目标）
    NPC_VENGEFUL_SPIRIT   = 23109   ///< 复仇之魂（玩家控制的单位）
};

/**
 * @brief 事件枚举
 */
enum Events
{
    EVENT_ENRAGE = 1,              ///< 狂暴事件
    EVENT_INCINERATE,              ///< 焚化事件
    EVENT_SUMMON_DOOM_BLOSSOM,     ///< 召唤末日之花事件
    EVENT_SHADOW_DEATH,            ///< 死亡之影事件
    EVENT_CRUSHING_SHADOWS         ///< 粉碎暗影事件
};

/**
 * @brief 动作枚举
 */
enum Actions
{
    ACTION_START_INTRO = 1         ///< 开始开场对白
};

/**
 * @brief 骷髅构造体召唤法术数组
 *
 * 用于在玩家死亡后召唤4个暗影构造体
 */
uint32 const SkeletronSpells[4] =
{
    SPELL_SUMMON_SKELETRON_1,
    SPELL_SUMMON_SKELETRON_2,
    SPELL_SUMMON_SKELETRON_3,
    SPELL_SUMMON_SKELETRON_4
};

/**
 * @struct boss_teron_gorefiend
 * @brief 泰隆·血魔AI
 *
 * 继承自BossAI，实现泰隆·血魔的战斗逻辑
 *
 * 战斗流程：
 * 1. 战斗开始时召唤末日之花、施放焚化和粉碎暗影
 * 2. 定期对随机玩家施放死亡之影，玩家死亡后变成复仇之魂
 * 3. 10分钟后进入狂暴状态
 */
struct boss_teron_gorefiend : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_teron_gorefiend(Creature* creature) : BossAI(creature, DATA_TERON_GOREFIEND) { }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当泰隆·血魔进入战斗时调用，初始化战斗事件：
     * - 播放战斗开始对白
     * - 安排狂暴计时器（10分钟）
     * - 安排焚化技能（12秒）
     * - 安排召唤末日之花（8秒）
     * - 安排死亡之影（8秒）
     * - 安排粉碎暗影（18秒）
     *
     * 调用时机：生物首次进入战斗状态
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        events.ScheduleEvent(EVENT_ENRAGE, 10min);
        events.ScheduleEvent(EVENT_INCINERATE, 12s);
        events.ScheduleEvent(EVENT_SUMMON_DOOM_BLOSSOM, 8s);
        events.ScheduleEvent(EVENT_SHADOW_DEATH, 8s);
        events.ScheduleEvent(EVENT_CRUSHING_SHADOWS, 18s);
    }

    /**
     * @brief 进入脱战模式回调
     * @param why 脱战原因
     *
     * 当泰隆·血魔脱战时调用，清理所有召唤物和相关光环
     *
     * 调用时机：战斗重置或脱战时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        DoCast(SPELL_SHADOW_OF_DEATH_REMOVE);
        summons.DespawnAll();
        _DespawnAtEvade();
    }

    /**
     * @brief 执行动作回调
     * @param action 动作ID
     *
     * 处理外部发来的动作指令，如触发开场对白
     *
     * 调用时机：由区域触发器或其他脚本调用
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_START_INTRO && me->IsAlive())
            Talk(SAY_INTRO);
    }

    /**
     * @brief 击杀单位回调
     * @param victim 被击杀的单位
     *
     * 当泰隆·血魔击杀玩家时播放击杀对白
     *
     * 调用时机：泰隆·血魔击杀单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者
     *
     * 当泰隆·血魔死亡时调用：
     * - 播放死亡对白
     * - 移除所有玩家的死亡之影效果
     * - 触发Boss死亡事件
     *
     * 调用时机：泰隆·血魔死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);
        DoCast(SPELL_SHADOW_OF_DEATH_REMOVE);
        _JustDied();
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每帧调用，处理事件调度和技能施放：
     * - 焚化：对随机目标造成火焰伤害
     * - 召唤末日之花：召唤会施放暗影箭的花朵
     * - 死亡之影：标记玩家在55秒后死亡
     * - 粉碎暗影：对多个目标造成暗影伤害
     * - 狂暴：10分钟后进入狂暴状态
     *
     * 性能注意事项：每帧调用，需保持高效
     */
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
                case EVENT_ENRAGE:
                    // 进入狂暴状态，提高伤害
                    DoCast(SPELL_BERSERK);
                    break;
                case EVENT_INCINERATE:
                    // 对随机目标施放焚化
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_INCINERATE);
                    Talk(SAY_INCINERATE);
                    events.Repeat(Seconds(12), Seconds(20));
                    break;
                case EVENT_SUMMON_DOOM_BLOSSOM:
                    // 召唤末日之花
                    DoCastSelf(SPELL_SUMMON_DOOM_BLOSSOM, true);
                    Talk(SAY_BLOSSOM);
                    events.Repeat(Seconds(30), Seconds(40));
                    break;
                case EVENT_SHADOW_DEATH:
                    // 对随机玩家施放死亡之影（排除已经是复仇之魂的玩家）
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100.0f, true, true, -SPELL_SPIRITUAL_VENGEANCE))
                        DoCast(target, SPELL_SHADOW_OF_DEATH);
                    events.Repeat(Seconds(30), Seconds(35));
                    break;
                case EVENT_CRUSHING_SHADOWS:
                    // 对最多5个目标施放粉碎暗影
                    DoCastSelf(SPELL_CRUSHING_SHADOWS, { SPELLVALUE_MAX_TARGETS, 5 });
                    Talk(SAY_CRUSHING);
                    events.Repeat(Seconds(18), Seconds(30));
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_doom_blossom
 * @brief 末日之花AI
 *
 * 继承自NullCreatureAI，实现末日之花的行为逻辑
 *
 * 功能：
 * - 生成后上升8码悬停在空中
 * - 每2秒对随机目标施放暗影箭
 * - 持续战斗直到被击杀
 */
struct npc_doom_blossom : public NullCreatureAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_doom_blossom(Creature* creature) : NullCreatureAI(creature), _instance(me->GetInstanceScript()) { }

    /**
     * @brief 重置回调
     *
     * 初始化末日之花：
     * - 悬停到空中（上升8码）
     * - 施放移动目标法术
     * - 进入战斗状态
     * - 安排暗影箭施放（每2秒一次）
     *
     * 调用时机：生物重置或生成时
     */
    void Reset() override
    {
        /* 临时解决方案 - 直到SMSG_SET_PLAY_HOVER_ANIM被实现 */
        Position pos;
        pos.Relocate(me);
        pos.m_positionZ += 8.0f;
        me->GetMotionMaster()->MoveTakeoff(0, pos);

        DoCast(SPELL_SUMMON_BLOSSOM_MOVE_TARGET);
        _scheduler.CancelAll();
        DoZoneInCombat();
        _scheduler.Schedule(Seconds(12), [this](TaskContext shadowBolt)
        {
            // 对随机目标施放暗影箭
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_SHADOWBOLT);

            shadowBolt.Repeat(Seconds(2));
        });
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
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;       ///< 任务调度器，用于安排暗影箭施放
    InstanceScript* _instance;      ///< 副本实例脚本指针
};

/**
 * @struct npc_shadowy_construct
 * @brief 暗影构造体AI
 *
 * 继承自ScriptedAI，实现暗影构造体的行为逻辑
 *
 * 功能：
 * - 玩家死亡后生成4个暗影构造体
 * - 随机选择目标攻击（优先攻击非复仇之魂的玩家）
 * - 定期施放萎缩技能降低目标攻击速度
 * - 目标死亡后切换到新目标
 *
 * 设计要点：
 * - 构造体是玩家需要击杀的目标
 * - 玩家控制复仇之魂使用技能击杀构造体
 */
struct npc_shadowy_construct : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_shadowy_construct(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    /**
     * @brief 重置回调
     *
     * 初始化暗影构造体：
     * - 检查Boss战斗状态，如非战斗中则消失
     * - 安排萎缩技能施放（每10-12秒）
     * - 安排目标检查（每200毫秒）
     * - 注册为泰隆的召唤物
     * - 选择初始攻击目标
     *
     * 调用时机：生物重置或生成时
     */
    void Reset() override
    {
        // 如果Boss不在战斗中，构造体直接消失
        if (_instance->GetBossState(DATA_TERON_GOREFIEND) != IN_PROGRESS)
        {
            me->DespawnOrUnsummon();
            return;
        }

        targetGUID.Clear();
        _scheduler.CancelAll();
        _scheduler.Schedule(Seconds(12), [this](TaskContext atrophy)
        {
            // 对当前目标施放萎缩，降低其攻击速度
            DoCastVictim(SPELL_ATROPHY);
            atrophy.Repeat(Seconds(10), Seconds(12));
        });
        _scheduler.Schedule(Milliseconds(200), [this](TaskContext checkPlayer)
        {
            // 检查当前目标是否仍然有效
            if (Unit* target = ObjectAccessor::GetUnit(*me, targetGUID))
            {
                if (!target->IsAlive() || !me->CanCreatureAttack(target))
                    SelectNewTarget();
            }
            else
                SelectNewTarget();

            checkPlayer.Repeat(Seconds(1));
        });

        // 注册为泰隆的召唤物，使其可以被正确管理
        if (Creature* teron = _instance->GetCreature(DATA_TERON_GOREFIEND))
            teron->AI()->JustSummoned(me);

        SelectNewTarget();
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每帧调用，更新任务调度器并执行近战攻击
     *
     * 性能注意事项：每帧调用，需保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        _scheduler.Update(diff, [this]
        {
            DoMeleeAttackIfReady();
        });
    }

    /**
     * @brief 选择新目标
     *
     * 为暗影构造体选择新的攻击目标：
     * - 优先攻击非复仇之魂状态的玩家
     * - 如果所有玩家都是复仇之魂，则随机攻击
     * - 重置威胁列表并建立大量威胁值确保目标锁定
     *
     * 调用时机：当前目标死亡或无效时
     */
    void SelectNewTarget()
    {
        if (Creature* teron = _instance->GetCreature(DATA_TERON_GOREFIEND))
        {
            // 优先选择非复仇之魂的玩家
            Unit* target = teron->AI()->SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true, true, -SPELL_SPIRITUAL_VENGEANCE);
            // 如果没有可用玩家，则攻击复仇之魂
            if (!target)
                target = teron->AI()->SelectTarget(SelectTargetMethod::Random, 0);

            if (target)
            {
                ResetThreatList();
                AttackStart(target);
                AddThreat(target, 1000000.0f);
                targetGUID = target->GetGUID();
            }
        }
    }

private:
    TaskScheduler _scheduler;       ///< 任务调度器，用于安排技能施放和目标检查
    InstanceScript* _instance;      ///< 副本实例脚本指针
    ObjectGuid targetGUID;          ///< 当前目标的GUID
};

/**
 * @class spell_teron_gorefiend_shadow_of_death
 * @brief 死亡之影法术脚本
 *
 * 法术ID: 40251
 *
 * 功能：
 * - 施放在玩家身上，55秒后触发死亡
 * - 玩家死亡时召唤复仇之魂供其控制
 * - 同时召唤4个暗影构造体作为击杀目标
 * - 阻止玩家在光环期间受到伤害（吸收效果）
 *
 * 机制：
 * 1. 光环持续55秒
 * 2. 光环到期时触发一系列召唤法术
 * 3. 玩家变成复仇之魂，获得新的技能栏
 */
// 40251 - Shadow of Death
class spell_teron_gorefiend_shadow_of_death : public AuraScript
{
    PrepareAuraScript(spell_teron_gorefiend_shadow_of_death);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return 验证是否通过
     *
     * 确保所有需要的法术都存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_SPIRIT,
            SPELL_POSSESS_SPIRIT_IMMUNE,
            SPELL_SPIRITUAL_VENGEANCE,
            SPELL_SUMMON_SKELETRON_1,
            SPELL_SUMMON_SKELETRON_2,
            SPELL_SUMMON_SKELETRON_3,
            SPELL_SUMMON_SKELETRON_4
        });
    }

    /**
     * @brief 吸收伤害回调
     * @param aurEff 光环效果
     * @param dmgInfo 伤害信息
     * @param absorbAmount 吸收量
     *
     * 阻止默认吸收行为，防止玩家在光环期间受到伤害
     */
    void Absorb(AuraEffect* /*aurEff*/, DamageInfo& /*dmgInfo*/, uint32& /*absorbAmount*/)
    {
        PreventDefaultAction();
    }

    /**
     * @brief 光环移除回调
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 当光环到期（非提前移除）时：
     * - 召唤复仇之魂
     * - 召唤4个暗影构造体
     * - 施加免疫和精神复仇光环
     *
     * 调用时机：光环自然到期时
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 仅当光环自然到期时触发
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
        {
            Unit* target = GetTarget();
            // 召唤复仇之魂
            target->CastSpell(target, SPELL_SUMMON_SPIRIT, true);

            // 召唤4个暗影构造体
            for (uint8 i = 0; i < 4; ++i)
                target->CastSpell(target, SkeletronSpells[i], true);

            // 施加免疫和精神复仇光环
            target->CastSpell(target, SPELL_POSSESS_SPIRIT_IMMUNE, true);
            target->CastSpell(target, SPELL_SPIRITUAL_VENGEANCE, true);
        }
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnEffectAbsorb += AuraEffectAbsorbFn(spell_teron_gorefiend_shadow_of_death::Absorb, EFFECT_0);
        AfterEffectRemove += AuraEffectRemoveFn(spell_teron_gorefiend_shadow_of_death::OnRemove, EFFECT_1, SPELL_AURA_OVERRIDE_CLASS_SCRIPTS, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_teron_gorefiend_spiritual_vengeance
 * @brief 精神复仇法术脚本
 *
 * 法术ID: 40268
 *
 * 功能：
 * - 允许玩家控制复仇之魂
 * - 光环移除时（战斗结束或复仇之魂死亡）杀死玩家
 *
 * 机制：
 * - 当复仇之魂死亡或战斗结束时，光环被移除
 * - 玩家立即死亡，无法复活直到战斗结束
 */
// 40268 - Spiritual Vengeance
class spell_teron_gorefiend_spiritual_vengeance : public AuraScript
{
    PrepareAuraScript(spell_teron_gorefiend_spiritual_vengeance);

    /**
     * @brief 光环移除回调
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 当光环被移除时，杀死目标玩家
     *
     * 调用时机：光环被移除时
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->KillSelf();
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_teron_gorefiend_spiritual_vengeance::OnRemove, EFFECT_0, SPELL_AURA_MOD_POSSESS, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_teron_gorefiend_shadow_of_death_remove
 * @brief 死亡之影移除法术脚本
 *
 * 法术ID: 41999
 *
 * 功能：
 * - 清理玩家身上的死亡之影相关光环
 * - 在战斗结束或Boss死亡时调用
 *
 * 机制：
 * - 移除占据灵魂免疫光环
 * - 移除精神复仇光环
 * - 移除死亡之影光环
 */
// 41999 - Shadow of Death Remove
class spell_teron_gorefiend_shadow_of_death_remove : public SpellScript
{
    PrepareSpellScript(spell_teron_gorefiend_shadow_of_death_remove);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return 验证是否通过
     *
     * 确保所有需要的法术都存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SHADOW_OF_DEATH,
            SPELL_POSSESS_SPIRIT_IMMUNE,
            SPELL_SPIRITUAL_VENGEANCE
        });
    }

    /**
     * @brief 移除光环
     *
     * 从目标身上移除所有死亡之影相关的光环
     *
     * 调用时机：法术命中目标时
     */
    void RemoveAuras()
    {
        Unit* target = GetHitUnit();

        target->RemoveAurasDueToSpell(SPELL_POSSESS_SPIRIT_IMMUNE);
        target->RemoveAurasDueToSpell(SPELL_SPIRITUAL_VENGEANCE);
        target->RemoveAurasDueToSpell(SPELL_SHADOW_OF_DEATH);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnHit += SpellHitFn(spell_teron_gorefiend_shadow_of_death_remove::RemoveAuras);
    }
};

/**
 * @class at_teron_gorefiend_entrance
 * @brief 泰隆·血魔入口区域触发器
 *
 * 继承自OnlyOnceAreaTriggerScript，确保每个玩家只触发一次
 *
 * 功能：
 * - 当玩家首次进入泰隆·血魔的房间时
 * - 触发泰隆的开场对白
 */
class at_teron_gorefiend_entrance : public OnlyOnceAreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_teron_gorefiend_entrance() : OnlyOnceAreaTriggerScript("at_teron_gorefiend_entrance") { }

    /**
     * @brief 尝试处理触发（仅一次）
     * @param player 触发区域触发器的玩家
     * @param areaTrigger 区域触发器数据
     * @return 是否处理成功
     *
     * 当玩家进入区域时：
     * - 获取副本实例脚本
     * - 获取泰隆·血魔生物
     * - 触发开场对白动作
     *
     * 调用时机：玩家进入区域触发器范围时
     */
    bool TryHandleOnce(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
    {
        if (InstanceScript* instance = player->GetInstanceScript())
            if (Creature* teron = instance->GetCreature(DATA_TERON_GOREFIEND))
                teron->AI()->DoAction(ACTION_START_INTRO);

        return true;
    }
};

/**
 * @brief 注册泰隆·血魔脚本
 *
 * 在服务器启动时调用，注册以下脚本：
 * - 泰隆·血魔AI
 * - 末日之花AI
 * - 暗影构造体AI
 * - 死亡之影法术脚本
 * - 精神复仇法术脚本
 * - 死亡之影移除法术脚本
 * - 入口区域触发器
 */
void AddSC_boss_teron_gorefiend()
{
    RegisterBlackTempleCreatureAI(boss_teron_gorefiend);
    RegisterBlackTempleCreatureAI(npc_doom_blossom);
    RegisterBlackTempleCreatureAI(npc_shadowy_construct);
    RegisterSpellScript(spell_teron_gorefiend_shadow_of_death);
    RegisterSpellScript(spell_teron_gorefiend_spiritual_vengeance);
    RegisterSpellScript(spell_teron_gorefiend_shadow_of_death_remove);
    new at_teron_gorefiend_entrance();
}
