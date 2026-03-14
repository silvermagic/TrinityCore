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
 * @file    spell_warrior.cpp
 * @brief   战士职业法术脚本模块
 *
 * 本文件实现了战士职业特有的法术脚本，包含以下主要功能：
 * - 战斗技能：冲锋、断筋、压制、斩杀、猛击等
 * - 防御技能：盾牌格挡、破胆怒吼、复仇、穿刺之喉等
 * - 增益效果：血性狂暴、横扫攻击、剑盾合璧等
 * - 天赋效果：深度伤口、猝死、血涌、盾牌掌握等
 *
 * 法术脚本按脚本名称字母顺序排列。
 * 本文件中的脚本名称应以 "spell_warr_" 为前缀。
 */

#include "ScriptMgr.h"
#include "ItemTemplate.h"
#include "Optional.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellScript.h"

/**
 * @brief 战士职业法术ID枚举
 *
 * 定义了战士职业相关法术的ID常量，用于法术脚本中引用具体的法术效果。
 * 包含攻击技能、防御技能、天赋效果和套装奖励等法术。
 */
enum WarriorSpells
{
    SPELL_WARRIOR_BLADESTORM_PERIODIC_WHIRLWIND     = 50622,   ///< 剑刃风暴周期性旋风斩
    SPELL_WARRIOR_BLOODTHIRST                       = 23885,   ///< 嗜血（治疗增益效果）
    SPELL_WARRIOR_BLOODTHIRST_DAMAGE                = 23881,   ///< 嗜血（伤害技能）
    SPELL_WARRIOR_BLOODSURGE_R1                     = 46913,   ///< 血涌（等级1）
    SPELL_WARRIOR_CHARGE                            = 34846,   ///< 冲锋效果
    SPELL_WARRIOR_DAMAGE_SHIELD_DAMAGE              = 59653,   ///< 伤害盾伤害
    SPELL_WARRIOR_DEEP_WOUNDS_RANK_1                = 12162,   ///< 深度伤口（等级1）
    SPELL_WARRIOR_DEEP_WOUNDS_RANK_2                = 12850,   ///< 深度伤口（等级2）
    SPELL_WARRIOR_DEEP_WOUNDS_RANK_3                = 12868,   ///< 深度伤口（等级3）
    SPELL_WARRIOR_DEEP_WOUNDS_PERIODIC              = 12721,   ///< 深度伤口周期性伤害
    SPELL_WARRIOR_EXECUTE                           = 20647,   ///< 斩杀伤害效果
    SPELL_WARRIOR_EXECUTE_GCD_REDUCED               = 71069,   ///< 斩杀GCD减少
    SPELL_WARRIOR_EXTRA_CHARGE                      = 70849,   ///< 额外充能
    SPELL_WARRIOR_GLYPH_OF_EXECUTION                = 58367,   ///< 斩杀雕文
    SPELL_WARRIOR_GLYPH_OF_VIGILANCE                = 63326,   ///< 穿刺之喉雕文
    SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_BUFF        = 65156,   ///< 战神暴击加成buff
    SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_TALENT      = 64976,   ///< 战神暴击加成天赋
    SPELL_WARRIOR_LAST_STAND_TRIGGERED              = 12976,   ///< 破釜沉舟触发效果
    SPELL_WARRIOR_RETALIATION_DAMAGE                = 20240,   ///< 反击风暴伤害
    SPELL_WARRIOR_SLAM                              = 50783,   ///< 猛击伤害
    SPELL_WARRIOR_SLAM_GCD_REDUCED                  = 71072,   ///< 猛击GCD减少
    SPELL_WARRIOR_SUDDEN_DEATH_R1                   = 29723,   ///< 猝死（等级1）
    SPELL_WARRIOR_SUNDER_ARMOR                      = 58567,   ///< 破甲攻击
    SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_1   = 12723,   ///< 横扫攻击额外攻击1（复制伤害）
    SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_2   = 26654,   ///< 横扫攻击额外攻击2（标准化伤害）
    SPELL_WARRIOR_TAUNT                             = 355,     ///< 嘲讽
    SPELL_WARRIOR_UNRELENTING_ASSAULT_RANK_1        = 46859,   ///< 无情突袭（等级1）
    SPELL_WARRIOR_UNRELENTING_ASSAULT_RANK_2        = 46860,   ///< 无情突袭（等级2）
    SPELL_WARRIOR_UNRELENTING_ASSAULT_TRIGGER_1     = 64849,   ///< 无情突袭触发1
    SPELL_WARRIOR_UNRELENTING_ASSAULT_TRIGGER_2     = 64850,   ///< 无情突袭触发2
    SPELL_WARRIOR_VIGILANCE_PROC                    = 50725,   ///< 穿刺之喉触发
    SPELL_WARRIOR_VIGILANCE_REDIRECT_THREAT         = 59665,   ///< 穿刺之喉威胁值重定向
    SPELL_WARRIOR_IMPROVED_SPELL_REFLECTION_TRIGGER = 59725,   ///< 强化法术反射触发
    SPELL_WARRIOR_SECOND_WIND_TRIGGER_1             = 29841,   ///< 强风触发1
    SPELL_WARRIOR_SECOND_WIND_TRIGGER_2             = 29842,   ///< 强风触发2
    SPELL_WARRIOR_GLYPH_OF_BLOCKING                 = 58374,   ///< 格挡雕文
    SPELL_WARRIOR_STOICISM                          = 70845,   ///< 坚忍
    SPELL_WARRIOR_T10_MELEE_4P_BONUS                = 70847,   ///< T10近战4件套奖励
    SPELL_WARRIOR_INTERVENE_THREAT                  = 59667    ///< 援护威胁值
};

/**
 * @brief 战士法术图标ID枚举
 *
 * 定义了战士法术相关的图标ID，用于通过图标ID识别法术效果。
 */
enum WarriorSpellIcons
{
    WARRIOR_ICON_ID_SUDDEN_DEATH                    = 1989     ///< 猝死图标ID
};

/**
 * @brief 杂项法术ID枚举
 *
 * 定义了其他职业或通用法术的ID，这些法术在战士脚本中被引用。
 */
enum MiscSpells
{
    SPELL_PALADIN_BLESSING_OF_SANCTUARY             = 20911,   ///< 圣骑士庇护祝福
    SPELL_PALADIN_GREATER_BLESSING_OF_SANCTUARY     = 25899,   ///< 圣骑士强效庇护祝福
    SPELL_PRIEST_RENEWED_HOPE                       = 63944,   ///< 牧师重拾希望
    SPELL_GEN_DAMAGE_REDUCTION_AURA                 = 68066,   ///< 通用伤害减免光环
    SPELL_CATEGORY_SHIELD_SLAM                      = 1209     ///< 盾牌猛击法术分类
};

/**
 * @class   spell_warr_bloodthirst
 * @brief   嗜血法术脚本 (SpellID: 23881)
 *
 * 处理战士嗜血技能的伤害计算和治疗效果施加。
 * 嗜血根据攻击强度造成伤害，并施加一个治疗增益效果。
 *
 * 调用时机：当嗜血法术施放时触发
 */
class spell_warr_bloodthirst : public SpellScript
{
    PrepareSpellScript(spell_warr_bloodthirst);

    /**
     * @brief 处理伤害效果
     * @param effIndex 法术效果索引
     *
     * 计算嗜血伤害值，基于施法者的攻击强度的百分比。
     * 伤害 = 攻击强度 × 效果值百分比
     * 还会考虑目标身上增加攻击强度的光环效果。
     */
    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        uint32 APbonus = GetCaster()->GetTotalAttackPowerValue(BASE_ATTACK);
        if (Unit* victim = GetHitUnit())
            APbonus += victim->GetTotalAuraModifier(SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS);

        SetEffectValue(CalculatePct(APbonus, GetEffectValue()));
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引
     *
     * 施放嗜血治疗增益效果到施法者身上。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_WARRIOR_BLOODTHIRST, true);
    }

    void Register() override
    {
        OnEffectLaunchTarget += SpellEffectFn(spell_warr_bloodthirst::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
        OnEffectHit += SpellEffectFn(spell_warr_bloodthirst::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_bloodthirst_heal
 * @brief   嗜血治疗效果法术脚本 (SpellID: 23880)
 *
 * 处理嗜血技能的治疗效果计算。
 * 治疗量为施法者最大生命值的百分比。
 *
 * 调用时机：当嗜血治疗法术施放时触发
 */
class spell_warr_bloodthirst_heal : public SpellScript
{
    PrepareSpellScript(spell_warr_bloodthirst_heal);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_BLOODTHIRST_DAMAGE });
    }

    /**
     * @brief 处理治疗效果
     * @param effIndex 法术效果索引
     *
     * 计算嗜血治疗量，基于施法者最大生命值的百分比。
     * 治疗百分比从嗜血伤害法术的效果1中获取。
     */
    void HandleHeal(SpellEffIndex /*effIndex*/)
    {
        SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(SPELL_WARRIOR_BLOODTHIRST_DAMAGE);
        int32 const healPct = spellInfo->GetEffect(EFFECT_1).CalcValue(GetCaster());
        SetEffectValue(GetCaster()->CountPctFromMaxHealth(healPct));
    }

    void Register() override
    {
        OnEffectLaunchTarget += SpellEffectFn(spell_warr_bloodthirst_heal::HandleHeal, EFFECT_0, SPELL_EFFECT_HEAL);
    }
};

/**
 * @class   spell_warr_charge
 * @brief   冲锋法术脚本 (SpellID: 100)
 *
 * 处理战士冲锋技能的效果施加。
 * 冲锋会使施法者快速接近目标，造成效果并可能触发战神天赋的暴击加成。
 *
 * 调用时机：当冲锋法术施放时触发
 */
class spell_warr_charge : public SpellScript
{
    PrepareSpellScript(spell_warr_charge);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_TALENT, SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_BUFF, SPELL_WARRIOR_CHARGE });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引
     *
     * 施放冲锋效果到施法者身上，并检查是否拥有战神天赋。
     * 如果拥有战神天赋，则施加暴击加成buff。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(GetEffectValue());
        caster->CastSpell(caster, SPELL_WARRIOR_CHARGE, args);

        // 战神天赋暴击加成
        if (caster->HasAura(SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_TALENT))
            caster->CastSpell(caster, SPELL_WARRIOR_JUGGERNAUT_CRIT_BONUS_BUFF, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_charge::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_concussion_blow
 * @brief   震荡猛击法术脚本 (SpellID: 12809)
 *
 * 处理震荡猛击技能的伤害计算。
 * 伤害基于施法者攻击强度的百分比。
 *
 * 调用时机：当震荡猛击法术施放时触发
 */
class spell_warr_concussion_blow : public SpellScript
{
    PrepareSpellScript(spell_warr_concussion_blow);

    /**
     * @brief 处理伤害效果
     * @param effIndex 法术效果索引
     *
     * 计算震荡猛击伤害值，基于施法者攻击强度的百分比。
     * 伤害百分比从效果2中获取。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        SetEffectValue(CalculatePct(GetCaster()->GetTotalAttackPowerValue(BASE_ATTACK), GetEffectInfo(EFFECT_2).CalcValue()));
    }

    void Register() override
    {
        OnEffectLaunchTarget += SpellEffectFn(spell_warr_concussion_blow::HandleDummy, EFFECT_1, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

/**
 * @class   spell_warr_damage_shield
 * @brief   伤害盾光环脚本 (SpellID: 58872)
 *
 * 处理战士伤害盾天赋的效果。
 * 当战士成功格挡攻击时，对攻击者造成基于格挡值百分比的伤害。
 *
 * 调用时机：当战士格挡攻击时触发
 */
class spell_warr_damage_shield : public AuraScript
{
    PrepareAuraScript(spell_warr_damage_shield);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_DAMAGE_SHIELD_DAMAGE });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 当格挡触发时，对攻击者造成伤害。
     * 伤害量 = 格挡值 × 效果值百分比
     */
    void OnProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        // 格挡值的百分比
        int32 damage = CalculatePct(int32(GetTarget()->GetShieldBlockValue()), aurEff->GetAmount());
        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(damage);
        GetTarget()->CastSpell(eventInfo.GetProcTarget(), SPELL_WARRIOR_DAMAGE_SHIELD_DAMAGE, args);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_warr_damage_shield::OnProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_deep_wounds
 * @brief   深度伤口法术脚本 (SpellID: 12162)
 *
 * 处理深度伤口天赋触发的周期性伤害效果。
 * 深度伤口会造成基于武器伤害的流血效果，伤害分摊到多个周期中。
 *
 * 调用时机：当深度伤口触发时
 */
class spell_warr_deep_wounds : public SpellScript
{
    PrepareSpellScript(spell_warr_deep_wounds);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WARRIOR_DEEP_WOUNDS_RANK_1,
            SPELL_WARRIOR_DEEP_WOUNDS_RANK_2,
            SPELL_WARRIOR_DEEP_WOUNDS_RANK_3,
            SPELL_WARRIOR_DEEP_WOUNDS_PERIODIC
        });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引
     *
     * 计算并施放深度伤口周期性伤害。
     * 伤害 = 效果值 × (16 × 天赋等级)%
     * 然后除以周期数，得出每个周期的伤害值。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        int32 damage = GetEffectValue();
        Unit* caster = GetCaster();
        if (Unit* target = GetHitUnit())
        {
            ApplyPct(damage, 16 * GetSpellInfo()->GetRank());

            SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(SPELL_WARRIOR_DEEP_WOUNDS_PERIODIC);

            ASSERT(spellInfo->GetMaxTicks() > 0);
            damage /= spellInfo->GetMaxTicks();

            CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
            args.AddSpellBP0(damage);
            caster->CastSpell(target, SPELL_WARRIOR_DEEP_WOUNDS_PERIODIC, args);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_deep_wounds::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_deep_wounds_aura
 * @brief   深度伤口光环脚本 (SpellID: 12834)
 *
 * 处理深度伤口天赋的触发检测。
 * 当玩家造成暴击时，检测是否触发深度伤口效果。
 *
 * 调用时机：当玩家攻击造成暴击时触发
 */
class spell_warr_deep_wounds_aura : public AuraScript
{
    PrepareAuraScript(spell_warr_deep_wounds_aura);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_0).TriggerSpell });
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 满足触发条件返回true，否则返回false
     *
     * 检查是否有伤害信息，并且触发者是玩家。
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        DamageInfo* damageInfo = eventInfo.GetDamageInfo();
        if (!damageInfo)
            return false;

        return eventInfo.GetActor()->GetTypeId() == TYPEID_PLAYER;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 根据攻击类型（主手/副手）计算平均武器伤害，
     * 并施放深度伤口触发法术。
     */
    void OnProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        Unit* actor = eventInfo.GetActor();
        float damage = 0.f;

        // 根据攻击类型计算平均武器伤害
        if (eventInfo.GetDamageInfo()->GetAttackType() == OFF_ATTACK)
            damage = (actor->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE) + actor->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE)) / 2.f;
        else
            damage = (actor->GetFloatValue(UNIT_FIELD_MINDAMAGE) + actor->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.f;

        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(damage);
        actor->CastSpell(eventInfo.GetProcTarget(), GetEffectInfo(EFFECT_0).TriggerSpell, args);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_warr_deep_wounds_aura::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_warr_deep_wounds_aura::OnProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

/**
 * @class   spell_warr_execute
 * @brief   斩杀法术脚本 (SpellID: 5308)
 *
 * 处理战士斩杀技能的伤害计算和怒气消耗。
 * 斩杀将剩余怒气转化为额外伤害，并考虑猝死天赋和斩杀雕文的效果。
 *
 * 调用时机：当斩杀法术施放时触发
 */
class spell_warr_execute : public SpellScript
{
    PrepareSpellScript(spell_warr_execute);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_EXECUTE, SPELL_WARRIOR_GLYPH_OF_EXECUTION });
    }

    /**
     * @brief 处理法术效果
     * @param effIndex 法术效果索引
     *
     * 计算斩杀伤害，包括：
     * - 基础伤害
     * - 消耗怒气转化的伤害
     * - 攻击强度加成
     * - 猝死天赋的怒气保留效果
     * - 斩杀雕文的额外怒气加成
     */
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (Unit* target = GetHitUnit())
        {
            SpellInfo const* spellInfo = GetSpellInfo();
            int32 rageUsed = std::min<int32>(300 - spellInfo->CalcPowerCost(caster, SpellSchoolMask(spellInfo->SchoolMask)), caster->GetPower(POWER_RAGE));
            int32 newRage = std::max<int32>(0, caster->GetPower(POWER_RAGE) - rageUsed);

            // 猝死天赋怒气保留
            if (AuraEffect* aurEff = caster->GetAuraEffect(SPELL_AURA_PROC_TRIGGER_SPELL, SPELLFAMILY_GENERIC, WARRIOR_ICON_ID_SUDDEN_DEATH, EFFECT_0))
            {
                int32 ragesave = aurEff->GetSpellInfo()->GetEffect(EFFECT_1).CalcValue() * 10;
                newRage = std::max(newRage, ragesave);
            }

            caster->SetPower(POWER_RAGE, uint32(newRage));
            // 斩杀雕文加成
            if (AuraEffect* aurEff = caster->GetAuraEffect(SPELL_WARRIOR_GLYPH_OF_EXECUTION, EFFECT_0))
                rageUsed += aurEff->GetAmount() * 10;

            int32 bp = GetEffectValue() + int32(rageUsed * GetEffectInfo().DamageMultiplier + caster->GetTotalAttackPowerValue(BASE_ATTACK) * 0.2f);
            CastSpellExtraArgs args(GetOriginalCaster()->GetGUID());
            args.AddSpellBP0(bp);
            caster->CastSpell(target, SPELL_WARRIOR_EXECUTE, args);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_execute::HandleEffect, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_extra_proc
 * @brief   猝死/血涌额外触发光环脚本 (SpellID: 29723, 46913)
 *
 * 处理猝死和血涌天赋的T10套装奖励触发。
 * 当拥有T10 4件套奖励时，有几率获得额外的法术充能和GCD减少效果。
 *
 * 调用时机：当猝死或血涌触发时
 */
class spell_warr_extra_proc : public AuraScript
{
    PrepareAuraScript(spell_warr_extra_proc);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WARRIOR_T10_MELEE_4P_BONUS,
            SPELL_WARRIOR_EXTRA_CHARGE,
            SPELL_WARRIOR_SLAM_GCD_REDUCED,
            SPELL_WARRIOR_EXECUTE_GCD_REDUCED
        });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 检查是否有T10 4件套奖励，如果有则按几率触发额外充能。
     * 根据是血涌还是猝死，施放对应的GCD减少效果。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        Unit* target = GetTarget();
        AuraEffect const* bonusAurEff = target->GetAuraEffect(SPELL_WARRIOR_T10_MELEE_4P_BONUS, EFFECT_0);
        if (!bonusAurEff)
            return;

        if (!roll_chance_i(bonusAurEff->GetAmount()))
            return;

        target->CastSpell(nullptr, SPELL_WARRIOR_EXTRA_CHARGE, aurEff);

        SpellInfo const* auraInfo = aurEff->GetSpellInfo();
        // 血涌施放猛击GCD减少
        if (auraInfo->IsRankOf(sSpellMgr->AssertSpellInfo(SPELL_WARRIOR_BLOODSURGE_R1)))
            target->CastSpell(nullptr, SPELL_WARRIOR_SLAM_GCD_REDUCED, aurEff);
        // 猝死施放斩杀GCD减少
        else if (auraInfo->IsRankOf(sSpellMgr->AssertSpellInfo(SPELL_WARRIOR_SUDDEN_DEATH_R1)))
            target->CastSpell(nullptr, SPELL_WARRIOR_EXECUTE_GCD_REDUCED, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_warr_extra_proc::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

/**
 * @class   spell_warr_glyph_of_blocking
 * @brief   格挡雕文光环脚本 (SpellID: 58375)
 *
 * 处理格挡雕文的效果。
 * 当战士使用盾牌猛击时，获得格挡值提升效果。
 *
 * 调用时机：当盾牌猛击施放时触发
 */
class spell_warr_glyph_of_blocking : public AuraScript
{
    PrepareAuraScript(spell_warr_glyph_of_blocking);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_GLYPH_OF_BLOCKING });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 施放格挡值提升效果到施法者。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        caster->CastSpell(caster, SPELL_WARRIOR_GLYPH_OF_BLOCKING, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_warr_glyph_of_blocking::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_glyph_of_sunder_armor
 * @brief   破甲雕文光环脚本 (SpellID: 58387)
 *
 * 处理破甲雕文的效果。
 * 该雕文使破甲攻击可以影响额外的目标。
 *
 * 调用时机：当计算法术修正时
 */
class spell_warr_glyph_of_sunder_armor : public AuraScript
{
    PrepareAuraScript(spell_warr_glyph_of_sunder_armor);

    /**
     * @brief 计算法术修正
     * @param aurEff 光环效果指针
     * @param spellMod 法术修正器引用
     *
     * 创建并设置法术修正器，用于增加破甲攻击的目标数量。
     */
    void HandleEffectCalcSpellMod(AuraEffect const* aurEff, SpellModifier*& spellMod)
    {
        if (!spellMod)
        {
            spellMod = new SpellModifier(aurEff->GetBase());
            spellMod->op = SpellModOp(aurEff->GetMiscValue());
            spellMod->type = SPELLMOD_FLAT;
            spellMod->spellId = GetId();
            spellMod->mask = aurEff->GetSpellEffectInfo().SpellClassMask;
        }

        spellMod->value = aurEff->GetAmount();
    }

    void Register() override
    {
        DoEffectCalcSpellMod += AuraEffectCalcSpellModFn(spell_warr_glyph_of_sunder_armor::HandleEffectCalcSpellMod, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_improved_spell_reflection
 * @brief   强化法术反射光环脚本 (SpellID: 59088)
 *
 * 处理强化法术反射天赋的效果。
 * 当战士使用法术反射时，有几率使附近队友也获得法术反射效果。
 *
 * 调用时机：当法术反射触发时
 */
class spell_warr_improved_spell_reflection : public AuraScript
{
    PrepareAuraScript(spell_warr_improved_spell_reflection);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_IMPROVED_SPELL_REFLECTION_TRIGGER });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 施放强化法术反射效果，影响附近队友。
     * 目标数量由天赋效果值决定。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        CastSpellExtraArgs args(aurEff);
        args.AddSpellMod(SPELLVALUE_MAX_TARGETS, aurEff->GetAmount());
        caster->CastSpell(caster, SPELL_WARRIOR_IMPROVED_SPELL_REFLECTION_TRIGGER, args);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_warr_improved_spell_reflection::HandleProc, EFFECT_1, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_intervene
 * @brief   援护法术脚本 (SpellID: 3411)
 *
 * 处理战士援护技能的威胁值转移效果。
 * 援护使战士快速移动到目标身边，并为目标承担部分威胁值。
 *
 * 调用时机：当援护法术施放时触发
 */
class spell_warr_intervene : public SpellScript
{
    PrepareSpellScript(spell_warr_intervene);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_INTERVENE_THREAT });
    }

    /**
     * @brief 处理威胁值转移
     * @param effIndex 法术效果索引
     *
     * 为被援护的目标施放威胁值转移效果。
     */
    void HandleThreat(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->CastSpell(target, SPELL_WARRIOR_INTERVENE_THREAT, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_intervene::HandleThreat, EFFECT_0, SPELL_EFFECT_CHARGE);
    }
};

/**
 * @class   spell_warr_intimidating_shout
 * @brief   破胆怒吼法术脚本 (SpellID: 5246)
 *
 * 处理破胆怒吼的目标过滤。
 * 破胆怒吼会使周围敌人恐惧，但主要目标除外。
 *
 * 调用时机：当破胆怒吼施放时触发
 */
class spell_warr_intimidating_shout : public SpellScript
{
    PrepareSpellScript(spell_warr_intimidating_shout);

    /**
     * @brief 过滤目标
     * @param unitList 目标列表
     *
     * 从目标列表中移除主要目标，使其不受恐惧效果影响。
     */
    void FilterTargets(std::list<WorldObject*>& unitList)
    {
        unitList.remove(GetExplTargetWorldObject());
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_warr_intimidating_shout::FilterTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_warr_intimidating_shout::FilterTargets, EFFECT_2, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

/**
 * @class   spell_warr_item_t10_prot_4p_bonus
 * @brief   战士T10防护4件套奖励光环脚本 (SpellID: 70844)
 *
 * 处理战士T10防护套装4件套奖励的效果。
 * 当战士的盾牌猛击造成暴击时，有几率治疗自己。
 *
 * 调用时机：当盾牌猛击暴击时触发
 */
class spell_warr_item_t10_prot_4p_bonus : public AuraScript
{
    PrepareAuraScript(spell_warr_item_t10_prot_4p_bonus);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_STOICISM });
    }

    /**
     * @brief 处理触发效果
     * @param eventInfo 触发事件信息
     *
     * 根据目标最大生命值的百分比治疗战士。
     */
    void HandleProc(ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        Unit* target = eventInfo.GetActionTarget();
        int32 bp0 = CalculatePct(target->GetMaxHealth(), GetEffectInfo(EFFECT_1).CalcValue());
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(bp0);
        target->CastSpell(nullptr, SPELL_WARRIOR_STOICISM, args);
    }

    void Register() override
    {
        OnProc += AuraProcFn(spell_warr_item_t10_prot_4p_bonus::HandleProc);
    }
};

/**
 * @class   spell_warr_last_stand
 * @brief   破釜沉舟法术脚本 (SpellID: 12975)
 *
 * 处理战士破釜沉舟技能的生命值提升效果。
 * 破釜沉舟会临时提升战士的最大生命值。
 *
 * 调用时机：当破釜沉舟法术施放时触发
 */
class spell_warr_last_stand : public SpellScript
{
    PrepareSpellScript(spell_warr_last_stand);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_LAST_STAND_TRIGGERED });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引
     *
     * 施放破釜沉舟触发效果，增加施法者最大生命值的百分比。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(caster->CountPctFromMaxHealth(GetEffectValue()));
        caster->CastSpell(caster, SPELL_WARRIOR_LAST_STAND_TRIGGERED, args);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_warr_last_stand::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_overpower
 * @brief   压制法术脚本 (SpellID: 7384, 7887, 11584, 11585)
 *
 * 处理战士压制技能的特殊效果。
 * 压制可以触发无情突袭天赋效果，打断目标的施法。
 *
 * 调用时机：当压制法术施放时触发
 */
class spell_warr_overpower : public SpellScript
{
    PrepareSpellScript(spell_warr_overpower);

    /**
     * @brief 处理法术效果
     * @param effIndex 法术效果索引
     *
     * 检查是否拥有无情突袭天赋，如果目标正在施法则施放打断效果。
     * UNIT_STATE_CASTING不应该在这里使用，因为它在瞬发法术的一帧内也会出现。
     */
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        uint32 spellId = 0;
        if (GetCaster()->HasAura(SPELL_WARRIOR_UNRELENTING_ASSAULT_RANK_1))
            spellId = SPELL_WARRIOR_UNRELENTING_ASSAULT_TRIGGER_1;
        else if (GetCaster()->HasAura(SPELL_WARRIOR_UNRELENTING_ASSAULT_RANK_2))
            spellId = SPELL_WARRIOR_UNRELENTING_ASSAULT_TRIGGER_2;

        if (!spellId)
            return;

        if (Player* target = GetHitPlayer())
            // UNIT_STATE_CASTING不应该在这里使用，它在瞬发法术的一帧内也会出现
            if (target->IsNonMeleeSpellCast(false, false, true))
                target->CastSpell(target, spellId, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_overpower::HandleEffect, EFFECT_0, SPELL_EFFECT_ANY);
    }
};

/**
 * @class   spell_warr_rend
 * @brief   撕裂光环脚本 (SpellID: 772)
 *
 * 处理战士撕裂技能的周期性伤害计算。
 * 撕裂造成基于武器伤害和攻击强度的流血效果。
 * 如果目标生命值高于75%，则撕裂伤害增加（仅限等级9及以上）。
 *
 * 调用时机：当撕裂光环效果计算伤害时
 */
class spell_warr_rend : public AuraScript
{
    PrepareAuraScript(spell_warr_rend);

    /**
     * @brief 计算伤害量
     * @param aurEff 光环效果指针
     * @param amount 伤害量引用
     * @param canBeRecalculated 是否可重新计算引用
     *
     * 计算撕裂的周期性伤害，基于：
     * - 基础伤害
     * - 武器平均伤害
     * - 攻击强度加成
     * - 目标生命值高于75%时的额外伤害（仅等级9及以上）
     */
    void CalculateAmount(AuraEffect const* aurEff, int32& amount, bool& canBeRecalculated)
    {
        if (Unit* caster = GetCaster())
        {
            canBeRecalculated = false;

            // $0.2 * (($MWB + $mwb) / 2 + $AP / 14 * $MWS) 每周期伤害加成
            float ap = caster->GetTotalAttackPowerValue(BASE_ATTACK);
            int32 mws = caster->GetAttackTime(BASE_ATTACK);
            float mwbMin = 0.f;
            float mwbMax = 0.f;
            for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
            {
                mwbMin += caster->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE, i);
                mwbMax += caster->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE, i);
            }

            float mwb = ((mwbMin + mwbMax) / 2 + ap * mws / 14000) * 0.2f;
            amount += int32(caster->ApplyEffectModifiers(GetSpellInfo(), aurEff->GetEffIndex(), mwb));

            // "如果目标生命值高于75%，撕裂造成35%额外伤害。"
            // 仅限3.1.3版本中等级9以上的撕裂（工具提示可能有误？）
            if (GetSpellInfo()->GetRank() >= 9)
            {
                if (GetUnitOwner()->HasAuraState(AURA_STATE_HEALTH_ABOVE_75_PERCENT, GetSpellInfo(), caster))
                    AddPct(amount, GetEffectInfo(EFFECT_2).CalcValue(caster));
            }
        }
    }

    void Register() override
    {
         DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_warr_rend::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

/**
 * @class   spell_warr_retaliation
 * @brief   反击风暴光环脚本 (SpellID: 20230)
 *
 * 处理战士反击风暴技能的效果。
 * 当战士被近战攻击命中时，对攻击者进行反击。
 * 只对来自前方的攻击生效，战士被眩晕时无法触发。
 *
 * 调用时机：当战士被近战攻击命中时触发
 */
class spell_warr_retaliation : public AuraScript
{
    PrepareAuraScript(spell_warr_retaliation);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_RETALIATION_DAMAGE });
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 满足触发条件返回true，否则返回false
     *
     * 检查攻击是否来自前方，并且战士是否处于眩晕状态。
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // 检查攻击是否来自前方，并且战士未被眩晕
        return GetTarget()->isInFront(eventInfo.GetActor(), float(M_PI)) && !GetTarget()->HasUnitState(UNIT_STATE_STUNNED);
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 对攻击者施放反击伤害效果。
     */
    void HandleEffectProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(eventInfo.GetProcTarget(), SPELL_WARRIOR_RETALIATION_DAMAGE, aurEff);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_warr_retaliation::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_warr_retaliation::HandleEffectProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_second_wind
 * @brief   强风光环脚本 (SpellID: 29834)
 *
 * 处理战士强风天赋的效果。
 * 当战士被眩晕或定身时，会获得治疗和怒气。
 *
 * 调用时机：当战士受到眩晕或定身效果时触发
 */
class spell_warr_second_wind : public AuraScript
{
    PrepareAuraScript(spell_warr_second_wind);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WARRIOR_SECOND_WIND_TRIGGER_1,
            SPELL_WARRIOR_SECOND_WIND_TRIGGER_2
        });
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 满足触发条件返回true，否则返回false
     *
     * 检查触发法术是否包含眩晕或定身机制。
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo)
            return false;

        return (spellInfo->GetAllEffectsMechanicMask() & ((1 << MECHANIC_ROOT) | (1 << MECHANIC_STUN))) != 0;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 根据天赋等级施放对应的强风触发效果。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        static uint32 const triggeredSpells[2] = { SPELL_WARRIOR_SECOND_WIND_TRIGGER_1, SPELL_WARRIOR_SECOND_WIND_TRIGGER_2 };

        PreventDefaultAction();
        Unit* caster = eventInfo.GetActionTarget();
        uint32 spellId = triggeredSpells[GetSpellInfo()->GetRank() - 1];
        caster->CastSpell(caster, spellId, aurEff);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_warr_second_wind::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_warr_second_wind::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_warr_shattering_throw
 * @brief   碎裂投掷法术脚本 (SpellID: 64380, 65941)
 *
 * 处理战士碎裂投掷技能的效果。
 * 碎裂投掷可以移除目标的免疫护盾效果（如圣盾术、寒冰屏障等）。
 *
 * 调用时机：当碎裂投掷法术施放时触发
 */
class spell_warr_shattering_throw : public SpellScript
{
    PrepareSpellScript(spell_warr_shattering_throw);

    /**
     * @brief 处理脚本效果
     * @param effIndex 法术效果索引
     *
     * 移除目标的免疫护盾光环，但仍会显示对伤害部分的免疫。
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        // 移除护盾，但仍会显示对伤害部分的免疫
        if (Unit* target = GetHitUnit())
            target->RemoveAurasWithMechanic(1 << MECHANIC_IMMUNE_SHIELD, AURA_REMOVE_BY_ENEMY_SPELL);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_shattering_throw::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class   spell_warr_slam
 * @brief   猛击法术脚本 (SpellID: 1464)
 *
 * 处理战士猛击技能的伤害效果。
 * 猛击造成基于武器伤害和效果值的伤害。
 *
 * 调用时机：当猛击法术施放时触发
 */
class spell_warr_slam : public SpellScript
{
    PrepareSpellScript(spell_warr_slam);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_SLAM });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引
     *
     * 施放猛击伤害效果到目标。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (!GetHitUnit())
            return;
        CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
        args.AddSpellBP0(GetEffectValue());
        GetCaster()->CastSpell(GetHitUnit(), SPELL_WARRIOR_SLAM, args);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_slam::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class   spell_warr_sweeping_strikes
 * @brief   横扫攻击光环脚本 (SpellID: 12328, 18765, 35429)
 *
 * 处理战士横扫攻击技能的效果。
 * 横扫攻击使战士的近战攻击对附近额外的目标造成伤害。
 *
 * 调用时机：当战士造成近战伤害时触发
 */
class spell_warr_sweeping_strikes : public AuraScript
{
    PrepareAuraScript(spell_warr_sweeping_strikes);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_1, SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_2 });
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 有附近目标返回true，否则返回false
     *
     * 选择附近的一个目标作为横扫攻击的目标。
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        _procTarget = eventInfo.GetActor()->SelectNearbyTarget(eventInfo.GetProcTarget());
        return _procTarget != nullptr;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 根据触发来源施放不同的额外攻击效果：
     * - 剑刃风暴或斩杀（目标不在20%生命以下）：标准化武器伤害
     * - 其他情况：复制原始伤害
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        if (DamageInfo* damageInfo = eventInfo.GetDamageInfo())
        {
            SpellInfo const* spellInfo = damageInfo->GetSpellInfo();
            if (spellInfo && (spellInfo->Id == SPELL_WARRIOR_BLADESTORM_PERIODIC_WHIRLWIND || (spellInfo->Id == SPELL_WARRIOR_EXECUTE && !_procTarget->HasAuraState(AURA_STATE_HEALTHLESS_20_PERCENT))))
            {
                // 如果由斩杀（目标不在20%生命以下）或剑刃风暴触发，造成标准化武器伤害
                GetTarget()->CastSpell(_procTarget, SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_2, aurEff);
            }
            else
            {
                // 其他情况复制原始伤害
                CastSpellExtraArgs args(aurEff);
                args.AddSpellBP0(damageInfo->GetDamage());
                GetTarget()->CastSpell(_procTarget, SPELL_WARRIOR_SWEEPING_STRIKES_EXTRA_ATTACK_1, args);
            }
        }
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_warr_sweeping_strikes::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_warr_sweeping_strikes::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }

    Unit* _procTarget = nullptr;  ///< 横扫攻击的额外目标
};

/**
 * @class   spell_warr_sword_and_board
 * @brief   剑盾合璧光环脚本 (SpellID: 46951)
 *
 * 处理战士剑盾合璧天赋的效果。
 * 当毁灭打击或复仇造成暴击时，重置盾牌猛击的冷却时间。
 *
 * 调用时机：当毁灭打击或复仇暴击时触发
 */
class spell_warr_sword_and_board : public AuraScript
{
    PrepareAuraScript(spell_warr_sword_and_board);

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 重置盾牌猛击的冷却时间。
     */
    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        // 重置盾牌猛击的冷却时间
        GetTarget()->GetSpellHistory()->ResetCooldowns([](SpellHistory::CooldownStorageType::iterator itr) -> bool
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
            return spellInfo && spellInfo->GetCategory() == SPELL_CATEGORY_SHIELD_SLAM;
        }, true);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_warr_sword_and_board::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

/**
 * @class   spell_warr_t3_prot_8p_bonus
 * @brief   战士T3防护8件套奖励光环脚本 (SpellID: 28845)
 *
 * 处理战士T3套装8件套奖励的欺诈死亡效果。
 * 当战士生命值低于20%时，有几率触发欺诈死亡效果。
 *
 * 调用时机：当战士受到伤害时触发
 */
class spell_warr_t3_prot_8p_bonus : public AuraScript
{
    PrepareAuraScript(spell_warr_t3_prot_8p_bonus);

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 满足触发条件返回true，否则返回false
     *
     * 检查目标是否已经低于20%生命值，或者受到伤害后会降到20%以下。
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        if (eventInfo.GetActionTarget()->HealthBelowPct(20))
            return true;

        DamageInfo* damageInfo = eventInfo.GetDamageInfo();
        if (damageInfo && damageInfo->GetDamage())
            if (GetTarget()->HealthBelowPctDamaged(20, damageInfo->GetDamage()))
                return true;

        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_warr_t3_prot_8p_bonus::CheckProc);
    }
};

/**
 * @class   spell_warr_vigilance
 * @brief   穿刺之喉光环脚本 (SpellID: 50720)
 *
 * 处理战士穿刺之喉技能的效果。
 * 穿刺之喉使目标获得伤害减免，并将部分威胁值转移给战士。
 * 当目标被攻击时，战士获得嘲讽冷却重置效果。
 *
 * 调用时机：当穿刺之喉光环施加或触发时
 */
class spell_warr_vigilance : public AuraScript
{
    PrepareAuraScript(spell_warr_vigilance);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_WARRIOR_GLYPH_OF_VIGILANCE,
            SPELL_WARRIOR_VIGILANCE_PROC,
            SPELL_WARRIOR_VIGILANCE_REDIRECT_THREAT,
            SPELL_GEN_DAMAGE_REDUCTION_AURA
        });
    }

    /**
     * @brief 处理光环施加
     * @param aurEff 光环效果指针
     * @param mode 光环效果处理模式
     *
     * 施加伤害减免光环和威胁值重定向效果。
     */
    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_GEN_DAMAGE_REDUCTION_AURA, true);

        if (Unit* caster = GetCaster())
            target->CastSpell(caster, SPELL_WARRIOR_VIGILANCE_REDIRECT_THREAT, true);
    }

    /**
     * @brief 处理光环移除
     * @param aurEff 光环效果指针
     * @param mode 光环效果处理模式
     *
     * 移除伤害减免光环（如果没有其他类似效果）和威胁值重定向。
     */
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        // 只有在没有其他类似效果时才移除伤害减免光环
        if (target->HasAura(SPELL_GEN_DAMAGE_REDUCTION_AURA) &&
            !(target->HasAura(SPELL_PALADIN_BLESSING_OF_SANCTUARY) ||
            target->HasAura(SPELL_PALADIN_GREATER_BLESSING_OF_SANCTUARY) ||
            target->HasAura(SPELL_PRIEST_RENEWED_HOPE)))
        {
            target->RemoveAurasDueToSpell(SPELL_GEN_DAMAGE_REDUCTION_AURA);
        }

        target->GetThreatManager().UnregisterRedirectThreat(SPELL_WARRIOR_VIGILANCE_REDIRECT_THREAT, GetCasterGUID());
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 有施法者返回true，否则返回false
     */
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        _procTarget = GetCaster();
        return _procTarget != nullptr;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 触发穿刺之喉效果，重置战士的嘲讽冷却。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(_procTarget, SPELL_WARRIOR_VIGILANCE_PROC, aurEff);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_warr_vigilance::HandleApply, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        OnEffectRemove += AuraEffectRemoveFn(spell_warr_vigilance::HandleRemove, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        DoCheckProc += AuraCheckProcFn(spell_warr_vigilance::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_warr_vigilance::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }

    Unit* _procTarget = nullptr;  ///< 触发目标（战士）
};

/**
 * @class   spell_warr_vigilance_redirect_threat
 * @brief   穿刺之喉威胁值重定向法术脚本 (SpellID: 59665)
 *
 * 处理穿刺之喉威胁值重定向的效果。
 * 将目标的部分威胁值转移给战士，雕文可以增加转移比例。
 *
 * 调用时机：当穿刺之喉威胁值重定向法术施放时触发
 */
class spell_warr_vigilance_redirect_threat : public SpellScript
{
    PrepareSpellScript(spell_warr_vigilance_redirect_threat);

    /**
     * @brief 检查雕文加成
     * @param effIndex 法术效果索引
     *
     * 如果战士拥有穿刺之喉雕文，增加威胁值转移比例。
     */
    void CheckGlyph(SpellEffIndex /*effIndex*/)
    {
        if (Unit* warrior = GetHitUnit())
            if (AuraEffect const* glyph = warrior->GetAuraEffect(SPELL_WARRIOR_GLYPH_OF_VIGILANCE, EFFECT_0))
                SetEffectValue(GetEffectValue() + glyph->GetAmount());
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_vigilance_redirect_threat::CheckGlyph, EFFECT_0, SPELL_EFFECT_REDIRECT_THREAT);
    }
};

/**
 * @class   spell_warr_vigilance_trigger
 * @brief   穿刺之喉触发效果法术脚本 (SpellID: 50725)
 *
 * 处理穿刺之喉触发的嘲讽冷却重置效果。
 * 当被穿刺之喉保护的目标受到攻击时，战士的嘲讽技能冷却被重置。
 *
 * 调用时机：当穿刺之喉触发效果施放时
 */
class spell_warr_vigilance_trigger : public SpellScript
{
    PrepareSpellScript(spell_warr_vigilance_trigger);

    /**
     * @brief 处理脚本效果
     * @param effIndex 法术效果索引
     *
     * 重置战士嘲讽技能的冷却时间。
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        // 移除嘲讽的冷却时间
        if (Player* target = GetHitPlayer())
            target->GetSpellHistory()->ResetCooldown(SPELL_WARRIOR_TAUNT, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_warr_vigilance_trigger::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册战士法术脚本
 *
 * 注册所有战士职业相关的法术脚本到系统中。
 * 每个脚本对应一个或多个战士技能的特殊处理逻辑。
 */
void AddSC_warrior_spell_scripts()
{
    RegisterSpellScript(spell_warr_bloodthirst);
    RegisterSpellScript(spell_warr_bloodthirst_heal);
    RegisterSpellScript(spell_warr_charge);
    RegisterSpellScript(spell_warr_concussion_blow);
    RegisterSpellScript(spell_warr_damage_shield);
    RegisterSpellScript(spell_warr_deep_wounds);
    RegisterSpellScript(spell_warr_deep_wounds_aura);
    RegisterSpellScript(spell_warr_execute);
    RegisterSpellScript(spell_warr_extra_proc);
    RegisterSpellScript(spell_warr_glyph_of_blocking);
    RegisterSpellScript(spell_warr_glyph_of_sunder_armor);
    RegisterSpellScript(spell_warr_improved_spell_reflection);
    RegisterSpellScript(spell_warr_intervene);
    RegisterSpellScript(spell_warr_intimidating_shout);
    RegisterSpellScript(spell_warr_item_t10_prot_4p_bonus);
    RegisterSpellScript(spell_warr_last_stand);
    RegisterSpellScript(spell_warr_overpower);
    RegisterSpellScript(spell_warr_rend);
    RegisterSpellScript(spell_warr_retaliation);
    RegisterSpellScript(spell_warr_second_wind);
    RegisterSpellScript(spell_warr_shattering_throw);
    RegisterSpellScript(spell_warr_slam);
    RegisterSpellScript(spell_warr_sweeping_strikes);
    RegisterSpellScript(spell_warr_sword_and_board);
    RegisterSpellScript(spell_warr_t3_prot_8p_bonus);
    RegisterSpellScript(spell_warr_vigilance);
    RegisterSpellScript(spell_warr_vigilance_redirect_threat);
    RegisterSpellScript(spell_warr_vigilance_trigger);
}
