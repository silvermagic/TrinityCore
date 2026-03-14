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
 * @file boss_mother_shahraz.cpp
 * @brief 莎赫拉丝主母Boss战脚本
 *
 * 本模块实现了莎赫拉丝主母的完整战斗逻辑，包括：
 * - 致命吸引：传送3个玩家到随机位置并连接，距离过远会造成大量伤害
 * - 沉默尖啸：对主目标施放沉默
 * - 棱光护盾：周期性切换不同类型的伤害吸收护盾
 * - 随机光束：随机施放邪恶、恶毒、罪恶、堕落四种光束
 * - 狂暴：10分钟后进入狂暴状态，生命值低于10%时也会触发
 *
 * 战斗机制：
 * 1. 每15秒切换一次棱光护盾（暗影、火焰、自然、奥术、冰霜、神圣）
 * 2. 每30秒施放一次随机光束组合
 * 3. 每30秒施放一次致命吸引
 * 4. 每18-30秒施放一次沉默尖啸
 * 5. 生命值低于10%时进入狂暴状态，施放随机光束
 */
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "black_temple.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "GridNotifiers.h"

/**
 * @brief 对白枚举
 */
enum Texts
{
    SAY_TAUNT     = 0,  ///< 嘲讽对白
    SAY_AGGRO     = 1,  ///< 战斗开始对白
    SAY_SPELL     = 2,  ///< 施放致命吸引时对白
    SAY_SLAY      = 3,  ///< 击杀玩家对白
    SAY_ENRAGE    = 4,  ///< 狂暴对白
    SAY_DEATH     = 5,  ///< 死亡对白
    EMOTE_ENRAGE  = 6,  ///< 狂暴表情
    EMOTE_BERSERK = 7   ///< 狂暴计时器表情
};

/**
 * @brief 技能枚举
 */
enum Spells
{
    SPELL_FATAL_ATTRACTION_DAMAGE   = 40871,  ///< 致命吸引伤害：连接的玩家之间造成伤害
    SPELL_SILENCING_SHRIEK          = 40823,  ///< 沉默尖啸：沉默目标
    SPELL_SABER_LASH_IMMUNITY       = 43690,  ///< 军刀猛击免疫：免疫致命吸引
    SPELL_FATAL_ATTRACTION_TELEPORT = 40869,  ///< 致命吸引传送：传送3个玩家
    SPELL_BERSERK                   = 45078,  ///< 狂暴：10分钟后进入狂暴状态
    SPELL_FATAL_ATTRACTION          = 41001,  ///< 致命吸引：连接玩家的光环
    SPELL_SINISTER_PERIODIC         = 40863,  ///< 邪恶光束周期：触发邪恶光束
    SPELL_VILE_PERIODIC             = 40865,  ///< 恶毒光束周期：触发恶毒光束
    SPELL_RANDOM_PERIODIC           = 40867,  ///< 随机光束周期：狂暴时随机光束
    SPELL_WICKED_PERIODIC           = 40866,  ///< 恶意光束周期：触发恶意光束
    SPELL_SINFUL_PERIODIC           = 40862,  ///< 罪恶光束周期：触发罪恶光束
    SPELL_PRISMATIC_AURA_SHADOW     = 40880,  ///< 棱光护盾-暗影：吸收暗影伤害
    SPELL_PRISMATIC_AURA_FIRE       = 40882,  ///< 棱光护盾-火焰：吸收火焰伤害
    SPELL_PRISMATIC_AURA_NATURE     = 40883,  ///< 棱光护盾-自然：吸收自然伤害
    SPELL_PRISMATIC_AURA_ARCANE     = 40891,  ///< 棱光护盾-奥术：吸收奥术伤害
    SPELL_PRISMATIC_AURA_FROST      = 40896,  ///< 棱光护盾-冰霜：吸收冰霜伤害
    SPELL_PRISMATIC_AURA_HOLY       = 40897,  ///< 棱光护盾-神圣：吸收神圣伤害
    SPELL_BEAM_SINISTER             = 40859,  ///< 邪恶光束：造成伤害
    SPELL_BEAM_VILE                 = 40860,  ///< 恶毒光束：造成伤害
    SPELL_BEAM_WICKED               = 40861,  ///< 恶意光束：造成伤害
    SPELL_BEAM_SINFUL               = 40827   ///< 罪恶光束：造成伤害
};

/**
 * @brief 事件枚举
 */
enum Events
{
    EVENT_RANDOM_BEAM  = 1,              ///< 随机光束事件
    EVENT_PRISMATIC_SHIELD,              ///< 棱光护盾事件
    EVENT_FATAL_ATTRACTION,              ///< 致命吸引事件
    EVENT_SILENCING_SHRIEK,              ///< 沉默尖啸事件
    EVENT_TAUNT,                         ///< 嘲讽事件
    EVENT_BERSERK                        ///< 狂暴事件
};

/**
 * @brief 光束触发法术数组
 *
 * 存储4种光束的周期性触发法术ID
 */
uint32 const BeamTriggers[4] =
{
    SPELL_SINISTER_PERIODIC,
    SPELL_VILE_PERIODIC,
    SPELL_WICKED_PERIODIC,
    SPELL_SINFUL_PERIODIC
};

/**
 * @brief 随机光束法术数组
 *
 * 存储4种光束的法术ID
 */
uint32 const RandomBeam[4] =
{
    SPELL_BEAM_SINISTER,
    SPELL_BEAM_VILE,
    SPELL_BEAM_WICKED,
    SPELL_BEAM_SINFUL
};

/**
 * @brief 棱光护盾法术数组
 *
 * 存储6种类型的棱光护盾法术ID
 */
uint32 const PrismaticAuras[6]=
{
    SPELL_PRISMATIC_AURA_SHADOW,
    SPELL_PRISMATIC_AURA_FIRE,
    SPELL_PRISMATIC_AURA_NATURE,
    SPELL_PRISMATIC_AURA_ARCANE,
    SPELL_PRISMATIC_AURA_FROST,
    SPELL_PRISMATIC_AURA_HOLY
};

/**
 * @struct boss_mother_shahraz
 * @brief 莎赫拉丝主母AI
 *
 * 继承自BossAI，实现莎赫拉丝主母的战斗逻辑
 *
 * 战斗流程：
 * 1. 战斗开始后每6秒施放一次光束组合
 * 2. 每15秒切换一次棱光护盾
 * 3. 每30秒施放一次致命吸引
 * 4. 每22秒施放一次沉默尖啸
 * 5. 10分钟后进入狂暴状态
 * 6. 生命值低于10%时也会触发狂暴
 *
 * 特殊机制：
 * - 棱光护盾：随机吸收一种类型的伤害
 * - 致命吸引：传送3个玩家并连接，距离过远造成大量伤害
 * - 随机光束：4种光束随机施放，造成不同效果
 */
struct boss_mother_shahraz : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_mother_shahraz(Creature* creature) : BossAI(creature, DATA_MOTHER_SHAHRAZ), _enraged(false) { }

    /**
     * @brief 重置回调
     *
     * 初始化莎赫拉丝主母：
     * - 重置事件和召唤物
     * - 重置狂暴状态
     *
     * 调用时机：生物重置或脱战时
     */
    void Reset() override
    {
        _Reset();
        _enraged = false;
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当莎赫拉丝主母进入战斗时调用：
     * - 播放战斗开始对白
     * - 安排沉默尖啸（22秒）
     * - 安排棱光护盾（15秒）
     * - 安排致命吸引（35秒）
     * - 安排随机光束（6秒）
     * - 安排狂暴计时器（10分钟）
     * - 安排嘲讽（35秒）
     *
     * 调用时机：生物首次进入战斗状态
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        events.ScheduleEvent(EVENT_SILENCING_SHRIEK, 22s);
        events.ScheduleEvent(EVENT_PRISMATIC_SHIELD, 15s);
        events.ScheduleEvent(EVENT_FATAL_ATTRACTION, 35s);
        events.ScheduleEvent(EVENT_RANDOM_BEAM, 6s);
        events.ScheduleEvent(EVENT_BERSERK, 10min);
        events.ScheduleEvent(EVENT_TAUNT, 35s);
    }

    /**
     * @brief 击杀单位回调
     * @param victim 被击杀的单位
     *
     * 当莎赫拉丝主母击杀玩家时播放击杀对白
     *
     * 调用时机：莎赫拉丝主母击杀单位时
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
     * 当莎赫拉丝主母死亡时调用：
     * - 触发Boss死亡事件
     * - 播放死亡对白
     *
     * 调用时机：莎赫拉丝主母死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 进入脱战模式回调
     * @param why 脱战原因
     *
     * 当莎赫拉丝主母脱战时调用，消失所有召唤物
     *
     * 调用时机：战斗重置或脱战时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        _DespawnAtEvade();
    }

    /**
     * @brief 受到伤害回调
     * @param attacker 攻击者
     * @param damage 伤害值
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 当莎赫拉丝主母受到伤害时检查生命值：
     * - 生命值低于10%时触发狂暴
     * - 施放随机光束周期效果
     * - 播放狂暴表情和对白
     *
     * 调用时机：莎赫拉丝主母受到伤害时
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!_enraged && me->HealthBelowPctDamaged(10, damage))
        {
            _enraged = true;
            DoCastSelf(SPELL_RANDOM_PERIODIC, true);
            Talk(EMOTE_ENRAGE, me);
            Talk(SAY_ENRAGE);
        }
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 处理各种战斗事件：
     * - EVENT_RANDOM_BEAM：施放随机光束组合
     * - EVENT_PRISMATIC_SHIELD：施放棱光护盾
     * - EVENT_FATAL_ATTRACTION：施放致命吸引
     * - EVENT_SILENCING_SHRIEK：施放沉默尖啸
     * - EVENT_TAUNT：播放嘲讽对白
     * - EVENT_BERSERK：进入狂暴状态
     *
     * 调用时机：事件调度器触发时
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_RANDOM_BEAM:
                // 随机施放一种光束组合
                DoCastSelf(BeamTriggers[urand(0, 3)]);
                events.Repeat(Seconds(30));
                break;
            case EVENT_PRISMATIC_SHIELD:
                // 随机施放一种棱光护盾
                DoCastSelf(PrismaticAuras[urand(0, 5)]);
                events.Repeat(Seconds(15));
                break;
            case EVENT_FATAL_ATTRACTION:
                // 施放致命吸引，传送并连接3个玩家
                Talk(SAY_SPELL);
                DoCastSelf(SPELL_FATAL_ATTRACTION_TELEPORT, { SPELLVALUE_MAX_TARGETS, 3 });
                events.Repeat(Seconds(30));
                break;
            case EVENT_SILENCING_SHRIEK:
                // 对主目标施放沉默尖啸
                DoCastVictim(SPELL_SILENCING_SHRIEK);
                events.Repeat(Seconds(18), Seconds(30));
                break;
            case EVENT_TAUNT:
                // 播放嘲讽对白
                Talk(SAY_TAUNT);
                events.Repeat(Seconds(30), Seconds(40));
                break;
            case EVENT_BERSERK:
                // 进入狂暴状态
                Talk(EMOTE_BERSERK, me);
                DoCastSelf(SPELL_BERSERK);
                break;
            default:
                break;
        }
    }

private:
    bool _enraged;                  ///< 是否已进入狂暴状态
};

/**
 * @class spell_mother_shahraz_fatal_attraction
 * @brief 致命吸引法术脚本
 *
 * 法术ID: 40869
 *
 * 功能：
 * - 选择3个目标（排除有军刀猛击免疫的目标）
 * - 将目标传送到施法者附近随机位置
 * - 施放致命吸引光环连接这些目标
 *
 * 机制：
 * 1. 过滤目标：移除有军刀猛击免疫的目标
 * 2. 设置传送目的地：施法者附近50码范围内随机位置
 * 3. 传送目标并施加致命吸引光环
 */
// 40869 - Fatal Attraction
class spell_mother_shahraz_fatal_attraction : public SpellScript
{
    PrepareSpellScript(spell_mother_shahraz_fatal_attraction);

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
            SPELL_SABER_LASH_IMMUNITY,
            SPELL_FATAL_ATTRACTION
        });
    }

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 移除有军刀猛击免疫光环的目标
     *
     * 调用时机：选择目标后
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_SABER_LASH_IMMUNITY));
    }

    /**
     * @brief 设置目的地
     * @param dest 目的地引用
     *
     * 设置传送目的地为施法者附近50码范围内的随机位置
     *
     * 调用时机：计算传送目的地时
     */
    void SetDest(SpellDestination& dest)
    {
        dest.Relocate(GetCaster()->GetRandomNearPosition(50.0f));
    }

    /**
     * @brief 处理传送
     * @param effIndex 效果索引
     *
     * 对目标施放致命吸引光环
     *
     * 调用时机：法术效果命中目标时
     */
    void HandleTeleport(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_FATAL_ATTRACTION, true);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_mother_shahraz_fatal_attraction::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(spell_mother_shahraz_fatal_attraction::SetDest, EFFECT_1, TARGET_DEST_CASTER_RANDOM);
        OnEffectHitTarget += SpellEffectFn(spell_mother_shahraz_fatal_attraction::HandleTeleport, EFFECT_1, SPELL_EFFECT_TELEPORT_UNITS);
    }
};

/**
 * @class spell_mother_shahraz_fatal_attraction_link
 * @brief 致命吸引连接法术脚本
 *
 * 法术ID: 40870
 *
 * 功能：
 * - 作为致命吸引的视觉效果
 * - 触发致命吸引伤害法术
 *
 * 机制：
 * - 当法术命中时，施放致命吸引伤害法术
 */
// 40870 - Fatal Attraction Dummy Visual
class spell_mother_shahraz_fatal_attraction_link : public SpellScript
{
    PrepareSpellScript(spell_mother_shahraz_fatal_attraction_link);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return 验证是否通过
     *
     * 确保所有需要的法术都存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_FATAL_ATTRACTION_DAMAGE });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 效果索引
     *
     * 施放致命吸引伤害法术
     *
     * 调用时机：法术效果命中目标时
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_FATAL_ATTRACTION_DAMAGE, true);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_mother_shahraz_fatal_attraction_link::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_mother_shahraz_saber_lash
 * @brief 军刀猛击法术脚本
 *
 * 法术ID: 40816
 *
 * 功能：
 * - 周期性触发军刀猛击
 * - 对随机目标造成伤害
 *
 * 机制：
 * - 每次触发时选择随机目标
 * - 施放军刀猛击法术
 */
// 40816 - Saber Lash
class spell_mother_shahraz_saber_lash : public AuraScript
{
    PrepareAuraScript(spell_mother_shahraz_saber_lash);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证是否通过
     *
     * 确保触发的法术存在
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_1).TriggerSpell });
    }

    /**
     * @brief 触发回调
     * @param aurEff 光环效果
     *
     * 阻止默认行为，手动选择目标并施放军刀猛击
     *
     * 调用时机：周期性触发时
     */
    void OnTrigger(AuraEffect const* aurEff)
    {
        PreventDefaultAction();

        uint32 triggerSpell = aurEff->GetSpellEffectInfo().TriggerSpell;
        if (Unit* target = GetUnitOwner()->GetAI()->SelectTarget(SelectTargetMethod::Random, 0))
            GetUnitOwner()->CastSpell(target, triggerSpell, true);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_mother_shahraz_saber_lash::OnTrigger, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @class spell_mother_shahraz_generic_periodic
 * @brief 通用周期性光束法术脚本
 *
 * 法术ID:
 * - 40863 - 邪恶周期
 * - 40865 - 恶毒周期
 * - 40866 - 恶意周期
 * - 40862 - 罪恶周期
 *
 * 功能：
 * - 周期性触发对应的光束法术
 * - 对随机目标施放
 *
 * 机制：
 * - 每次触发时选择随机目标
 * - 施放对应的光束法术
 */
/* 40863 - Sinister Periodic
   40865 - Vile Periodic
   40866 - Wicked Periodic
   40862 - Sinful Periodic */
class spell_mother_shahraz_generic_periodic : public AuraScript
{
    PrepareAuraScript(spell_mother_shahraz_generic_periodic);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证是否通过
     *
     * 确保触发的法术存在
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_0).TriggerSpell });
    }

    /**
     * @brief 触发回调
     * @param aurEff 光环效果
     *
     * 阻止默认行为，手动选择目标并施放光束
     *
     * 调用时机：周期性触发时
     */
    void OnTrigger(AuraEffect const* aurEff)
    {
        PreventDefaultAction();

        uint32 triggerSpell = aurEff->GetSpellEffectInfo().TriggerSpell;
        if (Unit* target = GetUnitOwner()->GetAI()->SelectTarget(SelectTargetMethod::Random, 0))
            GetUnitOwner()->CastSpell(target, triggerSpell, true);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_mother_shahraz_generic_periodic::OnTrigger, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @class spell_mother_shahraz_random_periodic
 * @brief 随机周期性光束法术脚本
 *
 * 法术ID: 40867
 *
 * 功能：
 * - 狂暴时使用的随机光束
 * - 周期性随机施放4种光束之一
 *
 * 机制：
 * - 每次触发时随机选择一种光束
 * - 对自己施放
 */
// 40867 - Random Periodic
class spell_mother_shahraz_random_periodic : public AuraScript
{
    PrepareAuraScript(spell_mother_shahraz_random_periodic);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证是否通过
     *
     * 确保所有光束法术都存在
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(RandomBeam);
    }

    /**
     * @brief 周期性回调
     * @param aurEffect 光环效果
     *
     * 阻止默认行为，随机施放一种光束
     *
     * 调用时机：周期性触发时
     */
    void OnPeriodic(AuraEffect const* /*aurEffect*/)
    {
        PreventDefaultAction();
        GetUnitOwner()->CastSpell(GetUnitOwner(), RandomBeam[urand(0, 3)], true);
    }

    /**
     * @brief 注册回调函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_mother_shahraz_random_periodic::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 注册莎赫拉丝主母脚本
 *
 * 在服务器启动时调用，注册以下脚本：
 * - 莎赫拉丝主母AI
 * - 致命吸引法术脚本
 * - 致命吸引连接法术脚本
 * - 军刀猛击法术脚本
 * - 通用周期性光束法术脚本
 * - 随机周期性光束法术脚本
 */
void AddSC_boss_mother_shahraz()
{
    RegisterBlackTempleCreatureAI(boss_mother_shahraz);
    RegisterSpellScript(spell_mother_shahraz_fatal_attraction);
    RegisterSpellScript(spell_mother_shahraz_fatal_attraction_link);
    RegisterSpellScript(spell_mother_shahraz_saber_lash);
    RegisterSpellScript(spell_mother_shahraz_generic_periodic);
    RegisterSpellScript(spell_mother_shahraz_random_periodic);
}
