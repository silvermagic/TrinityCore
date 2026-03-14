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
 * @file spell_mage.cpp
 * @brief 法师法术脚本模块
 *
 * 本模块实现法师职业相关法术的脚本逻辑，包括：
 * - 奥术系：奥术飞弹、奥术潜能、法力护盾、专注魔法等
 * - 火焰系：点燃、燃烧、炎爆术、活体炸弹、冲击波等
 * - 冰霜系：寒冰屏障、冰霜新星、冰盾、冰冷效果、霜火之箭等
 *
 * 法术脚本主要处理：
 * - 法术触发和连锁效果
 * - 天赋和雕文的特殊处理
 * - 吸收盾的计算和触发
 * - 套装效果的额外处理
 *
 * Scripts for spells with SPELLFAMILY_MAGE and SPELLFAMILY_GENERIC spells used by mage players.
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "spell_mage_".
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "Player.h"
#include "Random.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellScript.h"

/**
 * @brief 法师法术ID枚举
 *
 * 定义法师法术脚本使用的各种法术ID
 */
enum MageSpells
{
    SPELL_MAGE_BLAZING_SPEED                     = 31643,  ///< 炽烈疾速 - 被攻击时触发移动速度提升
    SPELL_MAGE_BURNOUT                           = 44450,  ///< 燃尽 - 法术暴击后恢复法力
    SPELL_MAGE_COLD_SNAP                         = 11958,  ///< 寒冰护体 - 重置冰霜系法术冷却
    SPELL_MAGE_FOCUS_MAGIC_PROC                  = 54648,  ///< 专注魔法触发 - 目标暴击时施法者获得暴击加成
    SPELL_MAGE_FROST_WARDING_R1                  = 11189,  ///< 冰霜导能等级1 - 提升冰霜护盾效果
    SPELL_MAGE_FROST_WARDING_TRIGGERED           = 57776,  ///< 冰霜导能触发 - 吸收伤害并恢复法力
    SPELL_MAGE_INCANTERS_ABSORBTION_R1           = 44394,  ///< 吸收导能等级1 - 吸收伤害时获得法术强度
    SPELL_MAGE_INCANTERS_ABSORBTION_TRIGGERED    = 44413,  ///< 吸收导能触发 - 法术强度提升效果
    SPELL_MAGE_IGNITE                            = 12654,  ///< 点燃 - 火焰法术暴击后的持续伤害
    SPELL_MAGE_MASTER_OF_ELEMENTS_ENERGIZE       = 29077,  ///< 元素大师充能 - 法术暴击后恢复法力
    SPELL_MAGE_SQUIRREL_FORM                     = 32813,  ///< 松鼠形态 - 变形视觉效果
    SPELL_MAGE_GIRAFFE_FORM                      = 32816,  ///< 长颈鹿形态 - 变形视觉效果
    SPELL_MAGE_SERPENT_FORM                      = 32817,  ///< 蛇形态 - 变形视觉效果
    SPELL_MAGE_DRAGONHAWK_FORM                   = 32818,  ///< 龙鹰形态 - 变形视觉效果
    SPELL_MAGE_WORGEN_FORM                       = 32819,  ///< 狼人形态 - 变形视觉效果
    SPELL_MAGE_SHEEP_FORM                        = 32820,  ///< 绵羊形态 - 变形视觉效果
    SPELL_MAGE_GLYPH_OF_ETERNAL_WATER            = 70937,  ///< 永恒之水雕文 - 水元素永久存在
    SPELL_MAGE_SHATTERED_BARRIER                 = 55080,  ///< 破碎屏障 - 冰盾破碎时冰冻周围敌人
    SPELL_MAGE_SUMMON_WATER_ELEMENTAL_PERMANENT  = 70908,  ///< 召唤永久水元素
    SPELL_MAGE_SUMMON_WATER_ELEMENTAL_TEMPORARY  = 70907,  ///< 召唤临时水元素
    SPELL_MAGE_GLYPH_OF_BLAST_WAVE               = 62126,  ///< 冲击波雕文 - 移除击退效果
    SPELL_MAGE_CHILLED                           = 12484,  ///< 冰冷效果 - 冰霜法术减速效果
    SPELL_MAGE_MANA_SURGE                        = 37445,  ///< 法力涌动 - 使用法力宝石后的增益
    SPELL_MAGE_MAGIC_ABSORPTION_MANA             = 29442,  ///< 魔法吸收法力 - 抵抗法术后恢复法力
    SPELL_MAGE_ARCANE_POTENCY_RANK_1             = 57529,  ///< 奥术潜能等级1 - 清爽/气定神闲后的暴击增益
    SPELL_MAGE_ARCANE_POTENCY_RANK_2             = 57531,  ///< 奥术潜能等级2 - 清爽/气定神闲后的暴击增益
    SPELL_MAGE_HOT_STREAK_PROC                   = 48108,  ///< 热连击触发 - 下一个炎爆术瞬发
    SPELL_MAGE_ARCANE_SURGE                      = 37436,  ///< 奥术涌动 - 法力护盾吸收后获得法术强度
    SPELL_MAGE_COMBUSTION                        = 11129,  ///< 燃烧 - 增加火焰法术暴击
    SPELL_MAGE_COMBUSTION_PROC                   = 28682,  ///< 燃烧触发 - 非暴击时叠加暴击buff
    SPELL_MAGE_EMPOWERED_FIRE_PROC               = 67545,  ///< 强化火焰触发 - 点燃触发时恢复法力
    SPELL_MAGE_T10_2P_BONUS                      = 70752,  ///< T10 2件套奖励
    SPELL_MAGE_T10_2P_BONUS_EFFECT               = 70753,  ///< T10 2件套效果 - 法术强度增益
    SPELL_MAGE_T8_4P_BONUS                       = 64869,  ///< T8 4件套奖励 - 降低触发消耗
    SPELL_MAGE_MISSILE_BARRAGE                   = 44401,  ///< 飞弹弹幕 - 奥术飞弹瞬发
    SPELL_MAGE_FINGERS_OF_FROST_AURASTATE_AURA   = 44544,  ///< 冰霜之指状态光环 - 允许对目标施放冰霜法术
    SPELL_MAGE_PERMAFROST_AURA                   = 68391,  ///< 永冻土光环 - 霜火之箭的减速效果
    SPELL_MAGE_ARCANE_MISSILES_R1                = 5143    ///< 奥术飞弹等级1
};

/**
 * @brief 法师法术图标ID枚举
 *
 * 用于过滤特定法术，因为某些法术共享家族标志
 */
enum MageSpellIcons
{
    SPELL_ICON_MAGE_SHATTERED_BARRIER = 2945,  ///< 破碎屏障图标ID
    SPELL_ICON_MAGE_PRESENCE_OF_MIND  = 139,   ///< 气定神闲图标ID
    SPELL_ICON_MAGE_CLEARCASTING      = 212,   ///< 清爽图标ID
    SPELL_ICON_MAGE_LIVING_BOMB       = 3000   ///< 活体炸弹图标ID
};

/**
 * @brief 吸收导能基础光环脚本
 *
 * 这是一个基类AuraScript，用于处理吸收导能天赋的效果。
 * 当法师吸收伤害时，根据天赋等级获得法术强度增益。
 *
 * 被以下法术继承使用：
 * - 火焰/冰霜防护结界
 * - 冰盾
 * - 法力护盾
 */
// Incanter's Absorbtion
class spell_mage_incanters_absorbtion_base_AuraScript : public AuraScript
{
    public:
        /**
         * @brief 验证法术信息
         * @param spellInfo 法术信息（未使用）
         * @return 验证是否成功
         *
         * 功能：验证吸收导能相关的法术ID是否有效
         */
        bool Validate(SpellInfo const* /*spellInfo*/) override
        {
            return ValidateSpellInfo(
            {
                SPELL_MAGE_INCANTERS_ABSORBTION_TRIGGERED,
                SPELL_MAGE_INCANTERS_ABSORBTION_R1
            });
        }

        /**
         * @brief 触发吸收导能效果
         * @param aurEff 光环效果
         * @param dmgInfo 伤害信息（未使用）
         * @param absorbAmount 吸收量
         *
         * 调用时机：每次成功吸收伤害后
         *
         * 功能：
         * 1. 检查目标是否有吸收导能天赋
         * 2. 根据吸收量计算法术强度增益
         * 3. 施放吸收导能触发法术
         *
         * 性能注意：每次吸收伤害时调用，应保持高效
         */
        void Trigger(AuraEffect* aurEff, DamageInfo& /*dmgInfo*/, uint32& absorbAmount)
        {
            Unit* target = GetTarget();

            // 检查是否有吸收导能天赋
            if (AuraEffect* talentAurEff = target->GetAuraEffectOfRankedSpell(SPELL_MAGE_INCANTERS_ABSORBTION_R1, EFFECT_0))
            {
                // 根据吸收量计算法术强度增益
                int32 bp = CalculatePct(absorbAmount, talentAurEff->GetAmount());
                CastSpellExtraArgs args(aurEff);
                args.AddSpellBP0(bp);
                target->CastSpell(target, SPELL_MAGE_INCANTERS_ABSORBTION_TRIGGERED, args);
            }
        }
};

/**
 * @brief 奥术飞弹法术脚本 (-5143)
 *
 * AuraScript实现，处理奥术飞弹的各种效果。
 * 主要处理T10 2件套奖励的触发逻辑。
 *
 * 特殊行为：
 * - 正常施放时不触发T10效果
 * - 通过飞弹弹幕触发的瞬发奥术飞弹才会触发T10效果
 * - T10效果在法术结束时施放
 */
// -5143 - Arcane Missiles
class spell_mage_arcane_missiles : public AuraScript
{
    PrepareAuraScript(spell_mage_arcane_missiles);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证是否成功
     *
     * 功能：验证T10套装相关法术ID是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_T10_2P_BONUS, SPELL_MAGE_T10_2P_BONUS_EFFECT });
    }

    /**
     * @brief 光环移除时的回调
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 调用时机：奥术飞弹施放完成时
     *
     * 功能：如果有T10套装且允许触发，施放T10 2件套效果
     */
    void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        // 检查是否有T10套装且允许触发
        if (target->HasAura(SPELL_MAGE_T10_2P_BONUS) && _canProcT10)
            target->CastSpell(nullptr, SPELL_MAGE_T10_2P_BONUS_EFFECT, aurEff);
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_arcane_missiles::OnRemove, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
    }

private:
    bool _canProcT10 = false;  ///< 是否允许触发T10效果，默认为false

public:
    /**
     * @brief 允许T10效果触发
     *
     * 调用时机：由飞弹弹幕脚本调用，表示这是瞬发奥术飞弹
     */
    void AllowT10Proc()
    {
        _canProcT10 = true;
    }
};

/**
 * @brief 奥术潜能天赋光环脚本 (-31571)
 *
 * AuraScript实现，处理奥术潜能天赋效果。
 * 当法师处于清爽或气定神闲状态时，下一个法术获得暴击几率加成。
 *
 * 触发条件：施放法术时处于清爽或气定神闲状态
 * 由于家族标志与其他法术共享，使用图标ID进行过滤
 */
// -31571 - Arcane Potency
class spell_mage_arcane_potency : public AuraScript
{
    PrepareAuraScript(spell_mage_arcane_potency);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证是否成功
     *
     * 功能：验证奥术潜能等级1和2的法术ID是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MAGE_ARCANE_POTENCY_RANK_1,
            SPELL_MAGE_ARCANE_POTENCY_RANK_2
        });
    }

    /**
     * @brief 检查是否可以触发
     * @param eventInfo 触发事件信息
     * @return 是否可以触发
     *
     * 调用时机：每次触发事件时
     *
     * 功能：过滤只有清爽或气定神闲才能触发此效果
     * 由于家族标志与冰冻之脑/飞弹弹幕共享，使用图标ID进行过滤
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // 由于家族标志与冰冻之脑/飞弹弹幕共享，需要通过图标ID过滤
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || (spellInfo->SpellIconID != SPELL_ICON_MAGE_CLEARCASTING && spellInfo->SpellIconID != SPELL_ICON_MAGE_PRESENCE_OF_MIND))
            return false;

        return true;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 调用时机：施放法术时处于清爽或气定神闲状态
     *
     * 功能：根据天赋等级施放对应的暴击加成法术
     * - 等级1：施放 SPELL_MAGE_ARCANE_POTENCY_RANK_1
     * - 等级2：施放 SPELL_MAGE_ARCANE_POTENCY_RANK_2
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        // 根据天赋等级选择触发法术
        static uint32 const triggerSpell[2] = { SPELL_MAGE_ARCANE_POTENCY_RANK_1, SPELL_MAGE_ARCANE_POTENCY_RANK_2 };

        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        // 获取对应等级的法术ID
        uint32 spellId = triggerSpell[GetSpellInfo()->GetRank() - 1];
        caster->CastSpell(caster, spellId, aurEff);
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_arcane_potency::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_arcane_potency::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -11113 - Blast Wave
class spell_mage_blast_wave : public SpellScript
{
    PrepareSpellScript(spell_mage_blast_wave);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_GLYPH_OF_BLAST_WAVE });
    }

    void HandleKnockBack(SpellEffIndex effIndex)
    {
        if (GetCaster()->HasAura(SPELL_MAGE_GLYPH_OF_BLAST_WAVE))
            PreventHitDefaultEffect(effIndex);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_mage_blast_wave::HandleKnockBack, EFFECT_2, SPELL_EFFECT_KNOCK_BACK);
    }
};

// -31641 - Blazing Speed
class spell_mage_blazing_speed : public AuraScript
{
    PrepareAuraScript(spell_mage_blazing_speed);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_BLAZING_SPEED });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        if (Unit* target = eventInfo.GetActionTarget())
            target->CastSpell(target, SPELL_MAGE_BLAZING_SPEED, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_blazing_speed::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// -54747 - Burning Determination
// 54748 - Burning Determination
class spell_mage_burning_determination : public AuraScript
{
    PrepareAuraScript(spell_mage_burning_determination);

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        if (SpellInfo const* spellInfo = eventInfo.GetSpellInfo())
            if (spellInfo->GetAllEffectsMechanicMask() & ((1 << MECHANIC_INTERRUPT) | (1 << MECHANIC_SILENCE)))
                return true;

        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_burning_determination::CheckProc);
    }
};

// -44449 - Burnout
class spell_mage_burnout : public AuraScript
{
    PrepareAuraScript(spell_mage_burnout);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_BURNOUT });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        DamageInfo* damageInfo = eventInfo.GetDamageInfo();
        if (!damageInfo || !damageInfo->GetSpellInfo())
            return false;

        return true;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        int32 mana = eventInfo.GetDamageInfo()->GetSpellInfo()->CalcPowerCost(GetTarget(), eventInfo.GetDamageInfo()->GetSchoolMask());
        mana = CalculatePct(mana, aurEff->GetAmount());

        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(mana);
        GetTarget()->CastSpell(GetTarget(), SPELL_MAGE_BURNOUT, args);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_burnout::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_burnout::HandleProc, EFFECT_1, SPELL_AURA_DUMMY);
    }
};

// 11958 - Cold Snap
class spell_mage_cold_snap : public SpellScript
{
    PrepareSpellScript(spell_mage_cold_snap);

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->GetSpellHistory()->ResetCooldowns([](SpellHistory::CooldownStorageType::iterator itr) -> bool
        {
            SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(itr->first);
            return spellInfo->SpellFamilyName == SPELLFAMILY_MAGE && (spellInfo->GetSchoolMask() & SPELL_SCHOOL_MASK_FROST) &&
                spellInfo->Id != SPELL_MAGE_COLD_SNAP && spellInfo->GetRecoveryTime() > 0;
        }, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_mage_cold_snap::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 11129 - Combustion
class spell_mage_combustion : public AuraScript
{
    PrepareAuraScript(spell_mage_combustion);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_COMBUSTION_PROC });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // Do not take charges, add a stack of crit buff
        if (!(eventInfo.GetHitMask() & PROC_HIT_CRITICAL))
        {
            eventInfo.GetActor()->CastSpell(nullptr, SPELL_MAGE_COMBUSTION_PROC, true);
            return false;
        }

        return true;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_combustion::CheckProc);
    }
};

// 28682 - Combustion proc
class spell_mage_combustion_proc : public AuraScript
{
    PrepareAuraScript(spell_mage_combustion_proc);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_COMBUSTION });
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_MAGE_COMBUSTION);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_combustion_proc::OnRemove, EFFECT_0, SPELL_AURA_ADD_FLAT_MODIFIER, AURA_EFFECT_HANDLE_REAL);
    }
};

// -31661 - Dragon's Breath
class spell_mage_dragon_breath : public AuraScript
{
    PrepareAuraScript(spell_mage_dragon_breath);

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // Dont proc with Living Bomb explosion
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (spellInfo && spellInfo->SpellIconID == SPELL_ICON_MAGE_LIVING_BOMB && spellInfo->SpellFamilyName == SPELLFAMILY_MAGE)
            return false;
        return true;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_dragon_breath::CheckProc);
    }
};

// -11185 - Improved Blizzard
class spell_mage_imp_blizzard : public AuraScript
{
    PrepareAuraScript(spell_mage_imp_blizzard);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_CHILLED });
    }

    void HandleChill(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        uint32 triggerSpellId = sSpellMgr->GetSpellWithRank(SPELL_MAGE_CHILLED, GetSpellInfo()->GetRank());
        eventInfo.GetActor()->CastSpell(eventInfo.GetProcTarget(), triggerSpellId, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_imp_blizzard::HandleChill, EFFECT_0, SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    }
};

// 37447 - Improved Mana Gems
// 61062 - Improved Mana Gems
class spell_mage_imp_mana_gems : public AuraScript
{
    PrepareAuraScript(spell_mage_imp_mana_gems);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_MANA_SURGE });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        eventInfo.GetActor()->CastSpell(nullptr, SPELL_MAGE_MANA_SURGE, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_imp_mana_gems::HandleProc, EFFECT_1, SPELL_AURA_OVERRIDE_CLASS_SCRIPTS);
    }
};

// -31656 - Empowered Fire
class spell_mage_empowered_fire : public AuraScript
{
    PrepareAuraScript(spell_mage_empowered_fire);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_EMPOWERED_FIRE_PROC });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        if (SpellInfo const* spellInfo = eventInfo.GetSpellInfo())
            if (spellInfo->Id == SPELL_MAGE_IGNITE)
                return true;

        return false;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* target = GetTarget();
        CastSpellExtraArgs args(aurEff);
        uint8 percent = sSpellMgr->AssertSpellInfo(SPELL_MAGE_EMPOWERED_FIRE_PROC)->GetEffect(EFFECT_0).CalcValue();
        args.AddSpellBP0(CalculatePct(target->GetCreateMana(), percent));
        target->CastSpell(target, SPELL_MAGE_EMPOWERED_FIRE_PROC, args);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_empowered_fire::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_empowered_fire::HandleProc, EFFECT_0, SPELL_AURA_ADD_FLAT_MODIFIER);
    }
};

// 74396 - Fingers of Frost
class spell_mage_fingers_of_frost : public AuraScript
{
    PrepareAuraScript(spell_mage_fingers_of_frost);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_FINGERS_OF_FROST_AURASTATE_AURA });
    }

    void HandleDummy(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        eventInfo.GetActor()->RemoveAuraFromStack(GetId());
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_MAGE_FINGERS_OF_FROST_AURASTATE_AURA);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_fingers_of_frost::HandleDummy, EFFECT_0, SPELL_AURA_DUMMY);
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_fingers_of_frost::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// -543  - Fire Ward
// -6143 - Frost Ward
class spell_mage_fire_frost_ward : public spell_mage_incanters_absorbtion_base_AuraScript
{
    PrepareAuraScript(spell_mage_fire_frost_ward);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MAGE_FROST_WARDING_TRIGGERED,
            SPELL_MAGE_FROST_WARDING_R1
        });
    }

    void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        if (Unit* caster = GetCaster())
        {
            // +80.68% from sp bonus
            float bonus = 0.8068f;

            bonus *= caster->SpellBaseHealingBonusDone(GetSpellInfo()->GetSchoolMask());
            bonus *= caster->CalculateSpellpowerCoefficientLevelPenalty(GetSpellInfo());

            amount += int32(bonus);
        }
    }

    void Absorb(AuraEffect* aurEff, DamageInfo& dmgInfo, uint32& absorbAmount)
    {
        Unit* target = GetTarget();
        if (AuraEffect* talentAurEff = target->GetAuraEffectOfRankedSpell(SPELL_MAGE_FROST_WARDING_R1, EFFECT_0))
        {
            int32 chance = talentAurEff->GetSpellInfo()->GetEffect(EFFECT_1).CalcValue(); // SPELL_EFFECT_DUMMY with NO_TARGET

            if (roll_chance_i(chance))
            {
                int32 bp = dmgInfo.GetDamage();
                dmgInfo.AbsorbDamage(bp);
                CastSpellExtraArgs args(aurEff);
                args.AddSpellBP0(bp);
                target->CastSpell(target, SPELL_MAGE_FROST_WARDING_TRIGGERED, args);
                absorbAmount = 0;
                PreventDefaultAction();
            }
        }
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_mage_fire_frost_ward::CalculateAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
        OnEffectAbsorb += AuraEffectAbsorbFn(spell_mage_fire_frost_ward::Absorb, EFFECT_0);
        AfterEffectAbsorb += AuraEffectAbsorbFn(spell_mage_fire_frost_ward::Trigger, EFFECT_0);
    }
};

// -44614 - Frostfire Bolt
class spell_mage_frostfire_bolt : public AuraScript
{
    PrepareAuraScript(spell_mage_frostfire_bolt);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_PERMAFROST_AURA });
    }

    void ApplyPermafrost(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
            caster->CastSpell(GetTarget(), SPELL_MAGE_PERMAFROST_AURA, aurEff);
    }

    void RemovePermafrost(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_MAGE_PERMAFROST_AURA);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_mage_frostfire_bolt::ApplyPermafrost, EFFECT_0, SPELL_AURA_MOD_DECREASE_SPEED, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_frostfire_bolt::RemovePermafrost, EFFECT_0, SPELL_AURA_MOD_DECREASE_SPEED, AURA_EFFECT_HANDLE_REAL);
    }
};

// 54646 - Focus Magic
class spell_mage_focus_magic : public AuraScript
{
    PrepareAuraScript(spell_mage_focus_magic);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_FOCUS_MAGIC_PROC });
    }

    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        _procTarget = GetCaster();
        return _procTarget && _procTarget->IsAlive();
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(_procTarget, SPELL_MAGE_FOCUS_MAGIC_PROC, aurEff);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_focus_magic::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_focus_magic::HandleProc, EFFECT_0, SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    }

    Unit* _procTarget = nullptr;
};

// 48108 - Hot Streak
// 57761 - Fireball!
class spell_mage_gen_extra_effects : public AuraScript
{
    PrepareAuraScript(spell_mage_gen_extra_effects);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MAGE_T10_2P_BONUS,
            SPELL_MAGE_T10_2P_BONUS_EFFECT,
            SPELL_MAGE_T8_4P_BONUS
        });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        Unit* caster = eventInfo.GetActor();
        // Prevent double proc for Arcane missiles
        if (caster == eventInfo.GetProcTarget())
            return false;

        // Proc chance is unknown, we'll just use dummy aura amount
        if (AuraEffect const* aurEff = caster->GetAuraEffect(SPELL_MAGE_T8_4P_BONUS, EFFECT_0))
            if (roll_chance_i(aurEff->GetAmount()))
                return false;

        return true;
    }

    void HandleProc(ProcEventInfo& eventInfo)
    {
        Unit* caster = eventInfo.GetActor();

        if (caster->HasAura(SPELL_MAGE_T10_2P_BONUS))
            caster->CastSpell(nullptr, SPELL_MAGE_T10_2P_BONUS_EFFECT, true);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_gen_extra_effects::CheckProc);
        OnProc += AuraProcFn(spell_mage_gen_extra_effects::HandleProc);
    }
};

// 56375 - Glyph of Polymorph
class spell_mage_glyph_of_polymorph : public AuraScript
{
    PrepareAuraScript(spell_mage_glyph_of_polymorph);

    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* target = eventInfo.GetProcTarget();
        target->RemoveAurasByType(SPELL_AURA_PERIODIC_DAMAGE, ObjectGuid::Empty, target->GetAura(32409)); // SW:D shall not be removed.
        target->RemoveAurasByType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
        target->RemoveAurasByType(SPELL_AURA_PERIODIC_LEECH);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_glyph_of_polymorph::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 56374 - Glyph of Icy Veins
class spell_mage_glyph_of_icy_veins : public AuraScript
{
    PrepareAuraScript(spell_mage_glyph_of_icy_veins);

    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        caster->RemoveAurasByType(SPELL_AURA_HASTE_SPELLS, ObjectGuid::Empty, 0, true, false);
        caster->RemoveAurasByType(SPELL_AURA_MOD_DECREASE_SPEED);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_glyph_of_icy_veins::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 56372 - Glyph of Ice Block
class spell_mage_glyph_of_ice_block : public AuraScript
{
    PrepareAuraScript(spell_mage_glyph_of_ice_block);

    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        Unit* caster = eventInfo.GetActor();
        caster->GetSpellHistory()->ResetCooldowns([](SpellHistory::CooldownStorageType::iterator itr) -> bool
        {
            SpellInfo const* cdSpell = sSpellMgr->GetSpellInfo(itr->first);
            if (!cdSpell || cdSpell->SpellFamilyName != SPELLFAMILY_MAGE
                || !(cdSpell->SpellFamilyFlags[0] & 0x00000040))
                return false;
            return true;
        }, true);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_glyph_of_ice_block::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -44445 - Hot Streak
class spell_mage_hot_streak : public AuraScript
{
    PrepareAuraScript(spell_mage_hot_streak);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_HOT_STREAK_PROC });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        AuraEffect* counter = GetEffect(EFFECT_1);
        if (!counter)
            return;

        // Count spell criticals in a row in second aura
        if (eventInfo.GetHitMask() & PROC_HIT_CRITICAL)
        {
            counter->SetAmount(counter->GetAmount() * 2);
            if (counter->GetAmount() < 100) // not enough
                return;

            // roll chance
            if (!roll_chance_i(aurEff->GetAmount()))
                return;

            Unit* caster = eventInfo.GetActor();
            caster->CastSpell(caster, SPELL_MAGE_HOT_STREAK_PROC, aurEff);
        }

        // reset counter
        counter->SetAmount(25);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_hot_streak::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -11426 - Ice Barrier
class spell_mage_ice_barrier : public spell_mage_incanters_absorbtion_base_AuraScript
{
    PrepareAuraScript(spell_mage_ice_barrier);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_SHATTERED_BARRIER });
    }

    void CalculateAmount(AuraEffect const* aurEff, int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        if (Unit* caster = GetCaster())
        {
            // +80.68% from sp bonus
            float bonus = 0.8068f;

            bonus *= caster->SpellBaseHealingBonusDone(GetSpellInfo()->GetSchoolMask());

            // Glyph of Ice Barrier: its weird having a SPELLMOD_ALL_EFFECTS here but its blizzards doing :)
            // Glyph of Ice Barrier is only applied at the spell damage bonus because it was already applied to the base value in CalculateSpellDamage
            bonus = caster->ApplyEffectModifiers(GetSpellInfo(), aurEff->GetEffIndex(), bonus);

            bonus *= caster->CalculateSpellpowerCoefficientLevelPenalty(GetSpellInfo());

            amount += int32(bonus);
        }
    }

    void AfterRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        // Shattered Barrier
        // Procs only if removed by damage.
        if (aurEff->GetAmount() <= 0)
            if (Unit* caster = GetCaster())
                if (AuraEffect* dummy = caster->GetDummyAuraEffect(SPELLFAMILY_MAGE, SPELL_ICON_MAGE_SHATTERED_BARRIER, EFFECT_0))
                    if (roll_chance_i(dummy->GetSpellInfo()->ProcChance))
                        caster->CastSpell(GetTarget(), SPELL_MAGE_SHATTERED_BARRIER, aurEff);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_mage_ice_barrier::CalculateAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
        AfterEffectAbsorb += AuraEffectAbsorbFn(spell_mage_ice_barrier::Trigger, EFFECT_0);
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_ice_barrier::AfterRemove, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB, AURA_EFFECT_HANDLE_REAL);
    }
};

// 45438 - Ice Block
class spell_mage_ice_block : public SpellScript
{
    PrepareSpellScript(spell_mage_ice_block);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->ExcludeCasterAuraSpell });
    }

    void TriggerHypothermia()
    {
        GetCaster()->CastSpell(nullptr, GetSpellInfo()->ExcludeCasterAuraSpell, true);
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_mage_ice_block::TriggerHypothermia);
    }
};

// -11119 - Ignite
class spell_mage_ignite : public AuraScript
{
    PrepareAuraScript(spell_mage_ignite);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_IGNITE });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        return eventInfo.GetDamageInfo() && eventInfo.GetProcTarget();
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        SpellInfo const* igniteDot = sSpellMgr->AssertSpellInfo(SPELL_MAGE_IGNITE);
        int32 pct = 8 * GetSpellInfo()->GetRank();

        ASSERT(igniteDot->GetMaxTicks() > 0);
        int32 amount = int32(CalculatePct(eventInfo.GetDamageInfo()->GetDamage(), pct) / igniteDot->GetMaxTicks());

        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(amount);
        GetTarget()->CastSpell(eventInfo.GetProcTarget(), SPELL_MAGE_IGNITE, args);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_ignite::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_ignite::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -44457 - Living Bomb
class spell_mage_living_bomb : public AuraScript
{
    PrepareAuraScript(spell_mage_living_bomb);

    bool Validate(SpellInfo const* spell) override
    {
        return ValidateSpellInfo({ static_cast<uint32>(spell->GetEffect(EFFECT_1).CalcValue()) });
    }

    void AfterRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        AuraRemoveMode removeMode = GetTargetApplication()->GetRemoveMode();
        if (removeMode != AURA_REMOVE_BY_ENEMY_SPELL && removeMode != AURA_REMOVE_BY_EXPIRE)
            return;

        if (Unit* caster = GetCaster())
            caster->CastSpell(GetTarget(), uint32(aurEff->GetAmount()), aurEff);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_living_bomb::AfterRemove, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// -29441 - Magic Absorption
class spell_mage_magic_absorption : public AuraScript
{
    PrepareAuraScript(spell_mage_magic_absorption);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_MAGIC_ABSORPTION_MANA });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        Unit* caster = eventInfo.GetActionTarget();
        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(CalculatePct(caster->GetMaxPower(POWER_MANA), aurEff->GetAmount()));
        caster->CastSpell(caster, SPELL_MAGE_MAGIC_ABSORPTION_MANA, args);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_mage_magic_absorption::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -1463 - Mana Shield
class spell_mage_mana_shield : public spell_mage_incanters_absorbtion_base_AuraScript
{
    PrepareAuraScript(spell_mage_mana_shield);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_ARCANE_SURGE }) &&
            spell_mage_incanters_absorbtion_base_AuraScript::Validate(spellInfo);
    }

    void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        if (Unit* caster = GetCaster())
        {
            // +80.53% from sp bonus
            float bonus = 0.8053f;

            bonus *= caster->SpellBaseHealingBonusDone(GetSpellInfo()->GetSchoolMask());
            bonus *= caster->CalculateSpellpowerCoefficientLevelPenalty(GetSpellInfo());

            amount += int32(bonus);
        }
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        Unit* caster = eventInfo.GetActionTarget();
        caster->CastSpell(caster, SPELL_MAGE_ARCANE_SURGE, aurEff);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_mage_mana_shield::CalculateAmount, EFFECT_0, SPELL_AURA_MANA_SHIELD);
        AfterEffectManaShield += AuraEffectManaShieldFn(spell_mage_mana_shield::Trigger, EFFECT_0);

        OnEffectProc += AuraEffectProcFn(spell_mage_mana_shield::HandleProc, EFFECT_0, SPELL_AURA_MANA_SHIELD);
    }
};

// -29074 - Master of Elements
class spell_mage_master_of_elements : public AuraScript
{
    PrepareAuraScript(spell_mage_master_of_elements);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_MASTER_OF_ELEMENTS_ENERGIZE });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        DamageInfo* damageInfo = eventInfo.GetDamageInfo();
        if (!damageInfo || !damageInfo->GetSpellInfo())
            return false;

        return true;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        int32 mana = eventInfo.GetDamageInfo()->GetSpellInfo()->CalcPowerCost(GetTarget(), eventInfo.GetDamageInfo()->GetSchoolMask());
        mana = CalculatePct(mana, aurEff->GetAmount());

        if (mana > 0)
        {
            CastSpellExtraArgs args(aurEff);
            args.AddSpellBP0(mana);
            GetTarget()->CastSpell(GetTarget(), SPELL_MAGE_MASTER_OF_ELEMENTS_ENERGIZE, args);
        }
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_master_of_elements::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_mage_master_of_elements::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 55342 - Mirror Image
class spell_mage_mirror_image : public AuraScript
{
    PrepareAuraScript(spell_mage_mirror_image);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_2).TriggerSpell });
    }

    void PeriodicTick(AuraEffect const* aurEff)
    {
        // Set name of summons to name of caster
        GetTarget()->CastSpell(nullptr, aurEff->GetSpellEffectInfo().TriggerSpell, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_mage_mirror_image::PeriodicTick, EFFECT_2, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// -44404 - Missile Barrage
class spell_mage_missile_barrage : public AuraScript
{
    PrepareAuraScript(spell_mage_missile_barrage);

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo)
            return false;

        // Arcane Blast - full chance
        if (spellInfo->SpellFamilyFlags[0] & 0x20000000)
            return true;

        // Rest of spells have half chance
        return roll_chance_i(50);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_missile_barrage::CheckProc);
    }
};

// 44401 - Missile Barrage
class spell_mage_missile_barrage_proc : public AuraScript
{
    PrepareAuraScript(spell_mage_missile_barrage_proc);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGE_T10_2P_BONUS, SPELL_MAGE_T8_4P_BONUS });
    }

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        Unit* caster = eventInfo.GetActor();
        // Prevent double proc for Arcane missiles
        if (caster == eventInfo.GetProcTarget())
            return false;

        // Proc chance is unknown, we'll just use dummy aura amount
        if (AuraEffect const* aurEff = caster->GetAuraEffect(SPELL_MAGE_T8_4P_BONUS, EFFECT_0))
            if (roll_chance_i(aurEff->GetAmount()))
                return false;

        return true;
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetTarget();
        if (caster->HasAura(SPELL_MAGE_T10_2P_BONUS))
            if (Aura* aura = caster->GetAuraOfRankedSpell(SPELL_MAGE_ARCANE_MISSILES_R1))
                if (spell_mage_arcane_missiles* missiles = aura->GetScript<spell_mage_arcane_missiles>(ScriptName))
                    missiles->AllowT10Proc();
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_mage_missile_barrage_proc::CheckProc);
        AfterEffectRemove += AuraEffectRemoveFn(spell_mage_missile_barrage_proc::OnRemove, EFFECT_0, SPELL_AURA_ADD_FLAT_MODIFIER, AURA_EFFECT_HANDLE_REAL);
    }

public:
    static char constexpr const ScriptName[] = "spell_mage_arcane_missiles";
};

enum SilvermoonPolymorph
{
    NPC_AUROSALIA   = 18744,
};

/// @todo move out of here and rename - not a mage spell
// 32826 - Polymorph (Visual)
class spell_mage_polymorph_cast_visual : public SpellScriptLoader
{
    public:
        spell_mage_polymorph_cast_visual() : SpellScriptLoader("spell_mage_polymorph_visual") { }

        class spell_mage_polymorph_cast_visual_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_mage_polymorph_cast_visual_SpellScript);

            static const uint32 PolymorphForms[6];

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo(PolymorphForms);
            }

            void HandleDummy(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetCaster()->FindNearestCreature(NPC_AUROSALIA, 30.0f))
                    if (target->GetTypeId() == TYPEID_UNIT)
                        target->CastSpell(target, PolymorphForms[urand(0, 5)], true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_mage_polymorph_cast_visual_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_mage_polymorph_cast_visual_SpellScript();
        }
};

const uint32 spell_mage_polymorph_cast_visual::spell_mage_polymorph_cast_visual_SpellScript::PolymorphForms[6] =
{
    SPELL_MAGE_SQUIRREL_FORM,
    SPELL_MAGE_GIRAFFE_FORM,
    SPELL_MAGE_SERPENT_FORM,
    SPELL_MAGE_DRAGONHAWK_FORM,
    SPELL_MAGE_WORGEN_FORM,
    SPELL_MAGE_SHEEP_FORM
};

// 31687 - Summon Water Elemental
class spell_mage_summon_water_elemental : public SpellScript
{
    PrepareSpellScript(spell_mage_summon_water_elemental);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MAGE_GLYPH_OF_ETERNAL_WATER,
            SPELL_MAGE_SUMMON_WATER_ELEMENTAL_TEMPORARY,
            SPELL_MAGE_SUMMON_WATER_ELEMENTAL_PERMANENT
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        // Glyph of Eternal Water
        if (caster->HasAura(SPELL_MAGE_GLYPH_OF_ETERNAL_WATER))
            caster->CastSpell(caster, SPELL_MAGE_SUMMON_WATER_ELEMENTAL_PERMANENT, true);
        else
            caster->CastSpell(caster, SPELL_MAGE_SUMMON_WATER_ELEMENTAL_TEMPORARY, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_mage_summon_water_elemental::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册法师法术脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有法师法术脚本到脚本系统
 */
void AddSC_mage_spell_scripts()
{
    RegisterSpellScript(spell_mage_arcane_potency);
    RegisterSpellScript(spell_mage_arcane_missiles);
    RegisterSpellScript(spell_mage_blast_wave);
    RegisterSpellScript(spell_mage_blazing_speed);
    RegisterSpellScript(spell_mage_burning_determination);
    RegisterSpellScript(spell_mage_burnout);
    RegisterSpellScript(spell_mage_cold_snap);
    RegisterSpellScript(spell_mage_combustion);
    RegisterSpellScript(spell_mage_combustion_proc);
    RegisterSpellScript(spell_mage_dragon_breath);
    RegisterSpellScript(spell_mage_imp_blizzard);
    RegisterSpellScript(spell_mage_imp_mana_gems);
    RegisterSpellScript(spell_mage_empowered_fire);
    RegisterSpellScript(spell_mage_fingers_of_frost);
    RegisterSpellScript(spell_mage_fire_frost_ward);
    RegisterSpellScript(spell_mage_frostfire_bolt);
    RegisterSpellScript(spell_mage_focus_magic);
    RegisterSpellScript(spell_mage_gen_extra_effects);
    RegisterSpellScript(spell_mage_glyph_of_polymorph);
    RegisterSpellScript(spell_mage_glyph_of_icy_veins);
    RegisterSpellScript(spell_mage_glyph_of_ice_block);
    RegisterSpellScript(spell_mage_hot_streak);
    RegisterSpellScript(spell_mage_ice_barrier);
    RegisterSpellScript(spell_mage_ice_block);
    RegisterSpellScript(spell_mage_ignite);
    RegisterSpellScript(spell_mage_living_bomb);
    RegisterSpellScript(spell_mage_magic_absorption);
    RegisterSpellScript(spell_mage_mana_shield);
    RegisterSpellScript(spell_mage_master_of_elements);
    RegisterSpellScript(spell_mage_mirror_image);
    RegisterSpellScript(spell_mage_missile_barrage);
    RegisterSpellScript(spell_mage_missile_barrage_proc);
    new spell_mage_polymorph_cast_visual();
    RegisterSpellScript(spell_mage_summon_water_elemental);
}
