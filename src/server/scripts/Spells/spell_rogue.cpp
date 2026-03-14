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
 * @file    spell_rogue.cpp
 * @brief   盗贼职业法术脚本模块
 *
 * 本文件实现了盗贼职业特有的法术脚本，包含以下主要功能：
 * - 攻击技能：剑刃乱舞、杀戮盛宴、毒伤、刺骨等
 * - 生存技能：佯攻、闪避、暗影斗篷等
 * - 控制技能：致盲、凿击、偷袭等
 * - 天赋效果：欺诈、冷酷攻击、野蛮战斗、迅捷毒药等
 *
 * 法术脚本按脚本名称字母顺序排列。
 * 本文件中的脚本名称应以 "spell_rog_" 为前缀。
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellScript.h"

/**
 * @brief 盗贼职业法术ID枚举
 *
 * 定义了盗贼职业相关法术的ID常量，用于法术脚本中引用具体的法术效果。
 * 包含攻击技能、控制技能、天赋效果和套装奖励等法术。
 */
enum RogueSpells
{
    SPELL_ROGUE_BLADE_FLURRY_EXTRA_ATTACK       = 22482,   ///< 剑刃乱舞额外攻击
    SPELL_ROGUE_CHEAT_DEATH_COOLDOWN            = 31231,   ///< 诈死冷却
    SPELL_ROGUE_GLYPH_OF_PREPARATION            = 56819,   ///< 准备雕文
    SPELL_ROGUE_KILLING_SPREE                   = 51690,   ///< 杀戮盛宴
    SPELL_ROGUE_KILLING_SPREE_TELEPORT          = 57840,   ///< 杀戮盛宴传送
    SPELL_ROGUE_KILLING_SPREE_WEAPON_DMG        = 57841,   ///< 杀戮盛宴武器伤害
    SPELL_ROGUE_KILLING_SPREE_DMG_BUFF          = 61851,   ///< 杀戮盛宴伤害增益
    SPELL_ROGUE_PREY_ON_THE_WEAK                = 58670,   ///< 弱点攻击
    SPELL_ROGUE_SHIV_TRIGGERED                  =  5940,   ///< 毒伤触发
    SPELL_ROGUE_TRICKS_OF_THE_TRADE             = 57934,   ///< 嫁祸诀窍
    SPELL_ROGUE_TRICKS_OF_THE_TRADE_DMG_BOOST   = 57933,   ///< 嫁祸诀窍伤害提升
    SPELL_ROGUE_TRICKS_OF_THE_TRADE_PROC        = 59628,   ///< 嫁祸诀窍触发
    SPELL_ROGUE_HONOR_AMONG_THIEVES             = 51698,   ///< 盗贼的荣誉
    SPELL_ROGUE_HONOR_AMONG_THIEVES_PROC        = 52916,   ///< 盗贼的荣誉触发
    SPELL_ROGUE_HONOR_AMONG_THIEVES_2           = 51699,   ///< 盗贼的荣誉2
    SPELL_ROGUE_T10_2P_BONUS                    = 70804,   ///< T10 2件套奖励
    SPELL_ROGUE_GLYPH_OF_BACKSTAB_TRIGGER       = 63975,   ///< 背刺雕文触发
    SPELL_ROGUE_QUICK_RECOVERY_ENERGY           = 31663,   ///< 快速恢复能量
    SPELL_ROGUE_CRIPPLING_POISON                =  3409,   ///< 致残药膏
    SPELL_ROGUE_MASTER_OF_SUBTLETY_BUFF         = 31665,   ///< 欺诈大师增益
    SPELL_ROGUE_OVERKILL_BUFF                   = 58427,   ///< 杀戮充盈增益
    SPELL_ROGUE_STEALTH                         =  1784    ///< 潜行
};

/**
 * @class   spell_rog_blade_flurry
 * @brief   剑刃乱舞光环脚本 (SpellID: 13877, 33735)
 *
 * 处理盗贼剑刃乱舞技能的效果。
 * 剑刃乱舞使盗贼的近战攻击对附近额外的目标造成相同的伤害。
 *
 * 调用时机：当盗贼造成近战伤害时触发
 */
class spell_rog_blade_flurry : public AuraScript
{
    PrepareAuraScript(spell_rog_blade_flurry);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_BLADE_FLURRY_EXTRA_ATTACK });
    }

    /**
     * @brief 检查触发条件
     * @param eventInfo 触发事件信息
     * @return 有附近目标返回true，否则返回false
     *
     * 选择附近的一个目标作为剑刃乱舞的额外目标。
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
     * 对额外目标施放相同伤害的攻击。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        if (DamageInfo* damageInfo = eventInfo.GetDamageInfo())
        {
            CastSpellExtraArgs args(aurEff);
            args.AddSpellBP0(damageInfo->GetDamage());
            GetTarget()->CastSpell(_procTarget, SPELL_ROGUE_BLADE_FLURRY_EXTRA_ATTACK, args);
        }
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_rog_blade_flurry::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_rog_blade_flurry::HandleProc, EFFECT_0, SPELL_AURA_MOD_MELEE_HASTE);
    }

    Unit* _procTarget = nullptr;  ///< 剑刃乱舞的额外目标
};

/**
 * @class   spell_rog_cheat_death
 * @brief   诈死光环脚本 (SpellID: 31228)
 *
 * 处理盗贼诈死天赋的效果。
 * 诈死有几率使原本致命的攻击变得只将生命值降至10%，
 * 并在短时间内完全吸收伤害。
 *
 * 调用时机：当盗贼受到伤害时触发
 */
class spell_rog_cheat_death : public AuraScript
{
    PrepareAuraScript(spell_rog_cheat_death);

    uint32 absorbChance = 0;  ///< 吸收几率

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_CHEAT_DEATH_COOLDOWN });
    }

    /**
     * @brief 加载光环
     * @return 目标是玩家返回true，否则返回false
     *
     * 初始化吸收几率，确保目标类型为玩家。
     */
    bool Load() override
    {
        absorbChance = GetEffectInfo(EFFECT_0).CalcValue();
        return GetUnitOwner()->GetTypeId() == TYPEID_PLAYER;
    }

    /**
     * @brief 计算吸收量
     * @param aurEff 光环效果指针
     * @param amount 吸收量引用
     * @param canBeRecalculated 是否可重新计算引用
     *
     * 设置吸收量为无限（-1），以便在Absorb函数中进行自定义计算。
     */
    void CalculateAmount(AuraEffect const* /*aurEff*/, int32 & amount, bool & /*canBeRecalculated*/)
    {
        // 设置吸收量为无限
        amount = -1;
    }

    /**
     * @brief 处理吸收
     * @param aurEff 光环效果指针
     * @param dmgInfo 伤害信息引用
     * @param absorbAmount 吸收量引用
     *
     * 检查是否触发诈死效果：
     * - 伤害足以致命
     * - 诈死不在冷却中
     * - 通过几率判定
     *
     * 如果触发，将生命值保留在10%，并吸收剩余伤害。
     */
    void Absorb(AuraEffect* /*aurEff*/, DamageInfo & dmgInfo, uint32 & absorbAmount)
    {
        Player* target = GetTarget()->ToPlayer();
        if (dmgInfo.GetDamage() < target->GetHealth() || target->GetSpellHistory()->HasCooldown(SPELL_ROGUE_CHEAT_DEATH_COOLDOWN) || !roll_chance_i(absorbChance))
            return;

        target->CastSpell(target, SPELL_ROGUE_CHEAT_DEATH_COOLDOWN, true);
        target->GetSpellHistory()->AddCooldown(SPELL_ROGUE_CHEAT_DEATH_COOLDOWN, 0, std::chrono::minutes(1));

        uint32 health10 = target->CountPctFromMaxHealth(10);

        // 生命值 > 10% - 吸收伤害直到生命值降至10%
        if (target->GetHealth() > health10)
            absorbAmount = dmgInfo.GetDamage() - target->GetHealth() + health10;
        // 生命值 <= 10% - 吸收所有伤害
        else
            absorbAmount = dmgInfo.GetDamage();
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_rog_cheat_death::CalculateAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
        OnEffectAbsorb += AuraEffectAbsorbFn(spell_rog_cheat_death::Absorb, EFFECT_0);
    }
};

/**
 * @class   spell_rog_cut_to_the_chase
 * @brief   锯齿利刃光环脚本 (SpellID: 51664)
 *
 * 处理盗贼锯齿利刃天赋的效果。
 * 当使用刺骨技能时，刷新切割的持续时间到5个连击点的最大持续时间。
 *
 * 调用时机：当刺骨技能触发时
 */
class spell_rog_cut_to_the_chase : public AuraScript
{
    PrepareAuraScript(spell_rog_cut_to_the_chase);

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 查找切割光环并将其持续时间刷新到5个连击点的最大持续时间。
     */
    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        // "将切割持续时间刷新到5个连击点的最大持续时间"
        Unit* caster = eventInfo.GetActor();
        // 查找切割光环
        if (AuraEffect const* snd = caster->GetAuraEffect(SPELL_AURA_MOD_MELEE_HASTE, SPELLFAMILY_ROGUE, 0x00040000, 0x00000000, 0x00000000, caster->GetGUID()))
        {
            // 5个连击点的最大持续时间
            uint32 countMax = snd->GetSpellInfo()->GetMaxDuration();

            snd->GetBase()->SetDuration(countMax, true);
            snd->GetBase()->SetMaxDuration(snd->GetBase()->GetDuration());
        }
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_rog_cut_to_the_chase::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_rog_deadly_brew
 * @brief   毒酿光环脚本 (SpellID: 51625)
 *
 * 处理盗贼毒酿天赋的效果。
 * 当对目标施放致命毒药时，自动施放致残药膏效果。
 *
 * 调用时机：当致命毒药触发时
 */
class spell_rog_deadly_brew : public AuraScript
{
    PrepareAuraScript(spell_rog_deadly_brew);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息指针
     * @return 法术信息有效返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_CRIPPLING_POISON });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果指针
     * @param eventInfo 触发事件信息
     *
     * 对目标施放致残药膏效果。
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        eventInfo.GetActor()->CastSpell(eventInfo.GetProcTarget(), SPELL_ROGUE_CRIPPLING_POISON, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_rog_deadly_brew::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @class   spell_rog_deadly_poison
 * @brief   致命毒药法术脚本 (SpellID: 2818)
 *
 * 处理盗贼致命毒药的效果。
 * 当致命毒药达到5层时，触发武器上的其他毒药效果。
 *
 * 调用时机：当致命毒药施放时触发
 */
class spell_rog_deadly_poison : public SpellScript
{
    PrepareSpellScript(spell_rog_deadly_poison);

    /**
     * @brief 加载法术
     * @return 施法者是玩家且有施法物品返回true，否则返回false
     */
    bool Load() override
    {
        // 此时CastItem必须已经初始化
        return GetCaster()->GetTypeId() == TYPEID_PLAYER && GetCastItem();
    }

    /**
     * @brief 处理命中前效果
     * @param missInfo 未命中信息
     *
     * 记录目标身上致命毒药的层数。
     */
    void HandleBeforeHit(SpellMissInfo missInfo)
    {
        if (missInfo != SPELL_MISS_NONE)
            return;

        if (Unit* target = GetHitUnit())
            // 致命毒药
            if (AuraEffect const* aurEff = target->GetAuraEffect(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_ROGUE, 0x10000, 0x80000, 0, GetCaster()->GetGUID()))
                _stackAmount = aurEff->GetBase()->GetStackAmount();
    }

    /**
     * @brief 处理命中后效果
     *
     * 如果致命毒药达到5层，触发武器上的其他毒药效果。
     */
    void HandleAfterHit()
    {
        if (_stackAmount < 5)
            return;

        Player* player = GetCaster()->ToPlayer();

        if (Unit* target = GetHitUnit())
        {

            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);

            if (item == GetCastItem())
                item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

            if (!item)
                return;

            // 物品战斗附魔
            for (uint8 slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
            {
                SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(item->GetEnchantmentId(EnchantmentSlot(slot)));
                if (!enchant)
                    continue;

                for (uint8 s = 0; s < 3; ++s)
                {
                    if (enchant->Effect[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
                        continue;

                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(enchant->EffectArg[s]);
                    if (!spellInfo)
                    {
                        TC_LOG_ERROR("spells", "Player::CastItemCombatSpell Enchant {}, player (Name: {}, {}) cast unknown spell {}", enchant->ID, player->GetName(), player->GetGUID().ToString(), enchant->EffectArg[s]);
                        continue;
                    }

                    // 仅触发盗贼毒药
                    if (spellInfo->SpellFamilyName != SPELLFAMILY_ROGUE || spellInfo->Dispel != DISPEL_POISON)
                        continue;

                    // 不要重复触发致命毒药
                    if (spellInfo->SpellFamilyFlags.IsEqual(0x10000, 0x80000, 0))
                        continue;

                    if (spellInfo->IsPositive())
                        player->CastSpell(player, enchant->EffectArg[s], item);
                    else
                        player->CastSpell(target, enchant->EffectArg[s], item);
                }
            }
        }
    }

    void Register() override
    {
        BeforeHit += BeforeSpellHitFn(spell_rog_deadly_poison::HandleBeforeHit);
        AfterHit += SpellHitFn(spell_rog_deadly_poison::HandleAfterHit);
    }

    uint8 _stackAmount = 0;  ///< 致命毒药层数
};

/**
 * @class   spell_rog_killing_spree
 * @brief   杀戮盛宴法术脚本 (SpellID: 51690)
 *
 * 处理盗贼杀戮盛宴技能的效果。
 * 杀戮盛宴使盗贼在多个目标之间快速传送并造成武器伤害。
 *
 * 调用时机：当杀戮盛宴施放时触发
 */
class spell_rog_killing_spree : public SpellScriptLoader
{
    public:
        static char constexpr const ScriptName[] = "spell_rog_killing_spree";

        spell_rog_killing_spree() : SpellScriptLoader(ScriptName) { }

        /**
         * @class   spell_rog_killing_spree_SpellScript
         * @brief   杀戮盛宴法术脚本内部类
         *
         * 处理目标选择和添加目标到光环脚本。
         */
        class spell_rog_killing_spree_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_rog_killing_spree_SpellScript);

            /**
             * @brief 过滤目标
             * @param targets 目标列表
             *
             * 如果没有目标或盗贼在载具上，施法失败。
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                if (targets.empty() || GetCaster()->GetVehicleBase())
                    FinishCast(SPELL_FAILED_OUT_OF_RANGE);
            }

            /**
             * @brief 处理虚拟效果
             * @param effIndex 法术效果索引
             *
             * 将目标添加到光环脚本的目标列表中。
             */
            void HandleDummy(SpellEffIndex /*effIndex*/)
            {
                if (Aura* aura = GetCaster()->GetAura(SPELL_ROGUE_KILLING_SPREE))
                    if (spell_rog_killing_spree_AuraScript* script = aura->GetScript<spell_rog_killing_spree_AuraScript>(ScriptName))
                        script->AddTarget(GetHitUnit());
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_rog_killing_spree_SpellScript::FilterTargets, EFFECT_1, TARGET_UNIT_DEST_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_rog_killing_spree_SpellScript::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_rog_killing_spree_SpellScript();
        }

        /**
         * @class   spell_rog_killing_spree_AuraScript
         * @brief   杀戮盛宴光环脚本内部类
         *
         * 处理杀戮盛宴的周期性效果，在目标之间传送并造成伤害。
         */
        class spell_rog_killing_spree_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_rog_killing_spree_AuraScript);

            /**
             * @brief 验证法术信息
             * @param spellInfo 法术信息指针
             * @return 法术信息有效返回true，否则返回false
             */
            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo(
                {
                    SPELL_ROGUE_KILLING_SPREE_TELEPORT,
                    SPELL_ROGUE_KILLING_SPREE_WEAPON_DMG,
                    SPELL_ROGUE_KILLING_SPREE_DMG_BUFF
                });
            }

            /**
             * @brief 处理光环施加
             * @param aurEff 光环效果指针
             * @param mode 光环效果处理模式
             *
             * 施放伤害增益效果。
             */
            void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                GetTarget()->CastSpell(GetTarget(), SPELL_ROGUE_KILLING_SPREE_DMG_BUFF, true);
            }

            /**
             * @brief 处理周期性效果
             * @param aurEff 光环效果指针
             *
             * 随机选择一个目标，传送到该目标并造成武器伤害。
             */
            void HandleEffectPeriodic(AuraEffect const* /*aurEff*/)
            {
                while (!_targets.empty())
                {
                    ObjectGuid guid = Trinity::Containers::SelectRandomContainerElement(_targets);
                    if (Unit* target = ObjectAccessor::GetUnit(*GetTarget(), guid))
                    {
                        GetTarget()->CastSpell(target, SPELL_ROGUE_KILLING_SPREE_TELEPORT, true);
                        GetTarget()->CastSpell(target, SPELL_ROGUE_KILLING_SPREE_WEAPON_DMG, true);
                        break;
                    }
                    else
                        _targets.remove(guid);
                }
            }

            /**
             * @brief 处理光环移除
             * @param aurEff 光环效果指针
             * @param mode 光环效果处理模式
             *
             * 移除伤害增益效果。
             */
            void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                GetTarget()->RemoveAurasDueToSpell(SPELL_ROGUE_KILLING_SPREE_DMG_BUFF);
            }

            void Register() override
            {
                AfterEffectApply += AuraEffectApplyFn(spell_rog_killing_spree_AuraScript::HandleApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_rog_killing_spree_AuraScript::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
                AfterEffectRemove += AuraEffectRemoveFn(spell_rog_killing_spree_AuraScript::HandleRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }

        public:
            /**
             * @brief 添加目标
             * @param target 目标单位
             *
             * 将目标添加到目标列表中。
             */
            void AddTarget(Unit* target)
            {
                _targets.push_back(target->GetGUID());
            }

        private:
            GuidList _targets;  ///< 目标GUID列表
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_rog_killing_spree_AuraScript();
        }
};
char constexpr const spell_rog_killing_spree::ScriptName[];

// -31130 - Nerves of Steel
class spell_rog_nerves_of_steel : public AuraScript
{
    PrepareAuraScript(spell_rog_nerves_of_steel);

public:
    spell_rog_nerves_of_steel()
    {
        absorbPct = 0;
    }

private:
    uint32 absorbPct;

    bool Load() override
    {
        absorbPct = GetEffectInfo(EFFECT_0).CalcValue(GetCaster());
        return true;
    }

    void CalculateAmount(AuraEffect const* /*aurEff*/, int32 & amount, bool & /*canBeRecalculated*/)
    {
        // Set absorbtion amount to unlimited
        amount = -1;
    }

    void Absorb(AuraEffect* /*aurEff*/, DamageInfo & dmgInfo, uint32 & absorbAmount)
    {
        // reduces all damage taken while stun or fear
        if (GetTarget()->HasUnitFlag(UNIT_FLAG_FLEEING) || (GetTarget()->HasUnitFlag(UNIT_FLAG_STUNNED) && GetTarget()->HasAuraWithMechanic(1<<MECHANIC_STUN)))
            absorbAmount = CalculatePct(dmgInfo.GetDamage(), absorbPct);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_rog_nerves_of_steel::CalculateAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
        OnEffectAbsorb += AuraEffectAbsorbFn(spell_rog_nerves_of_steel::Absorb, EFFECT_0);
    }
};

// 31666 - Master of Subtlety
// 58428 - Overkill - aura remove spell (SERVERSIDE)
template <uint32 RemoveSpellId>
class spell_rog_overkill_mos : public AuraScript
{
    PrepareAuraScript(spell_rog_overkill_mos);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ RemoveSpellId });
    }

    void AfterApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        if (Aura* visualAura = GetTarget()->GetAura(RemoveSpellId))
        {
            int32 duration = aurEff->GetBase()->GetDuration();
            visualAura->SetDuration(duration);
            visualAura->SetMaxDuration(duration);
        }
    }

    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->RemoveAurasDueToSpell(RemoveSpellId);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_rog_overkill_mos::AfterApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_rog_overkill_mos::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// 14185 - Preparation
class spell_rog_preparation : public SpellScript
{
    PrepareSpellScript(spell_rog_preparation);

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_GLYPH_OF_PREPARATION });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->GetSpellHistory()->ResetCooldowns([caster](SpellHistory::CooldownStorageType::iterator itr) -> bool
        {
            SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(itr->first);
            if (spellInfo->SpellFamilyName != SPELLFAMILY_ROGUE)
                return false;

            return (spellInfo->SpellFamilyFlags[1] & SPELLFAMILYFLAG1_ROGUE_COLDB_SHADOWSTEP ||  // Cold Blood, Shadowstep
                spellInfo->SpellFamilyFlags[0] & SPELLFAMILYFLAG_ROGUE_VAN_EVAS_SPRINT) ||       // Vanish, Evasion, Sprint
                (caster->HasAura(SPELL_ROGUE_GLYPH_OF_PREPARATION) &&
                (spellInfo->SpellFamilyFlags[1] & SPELLFAMILYFLAG1_ROGUE_DISMANTLE ||            // Dismantle
                spellInfo->SpellFamilyFlags[0] & SPELLFAMILYFLAG_ROGUE_KICK ||                   // Kick
                (spellInfo->SpellFamilyFlags[0] & SPELLFAMILYFLAG_ROGUE_BLADE_FLURRY &&          // Blade Flurry
                spellInfo->SpellFamilyFlags[1] & SPELLFAMILYFLAG1_ROGUE_BLADE_FLURRY)));
        }, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rog_preparation::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// -51685 - Prey on the Weak
class spell_rog_prey_on_the_weak : public AuraScript
{
    PrepareAuraScript(spell_rog_prey_on_the_weak);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_PREY_ON_THE_WEAK });
    }

    void HandleEffectPeriodic(AuraEffect const* aurEff)
    {
        Unit* target = GetTarget();
        Unit* victim = target->GetVictim();
        if (victim && (target->GetHealthPct() > victim->GetHealthPct()))
        {
            if (!target->HasAura(SPELL_ROGUE_PREY_ON_THE_WEAK))
            {
                CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
                args.AddSpellBP0(aurEff->GetSpellEffectInfo().CalcValue());
                target->CastSpell(target, SPELL_ROGUE_PREY_ON_THE_WEAK, args);
            }
        }
        else
            target->RemoveAurasDueToSpell(SPELL_ROGUE_PREY_ON_THE_WEAK);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_rog_prey_on_the_weak::HandleEffectPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// -31244 - Quick Recovery
class spell_rog_quick_recovery : public AuraScript
{
    PrepareAuraScript(spell_rog_quick_recovery);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_QUICK_RECOVERY_ENERGY });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo)
            return;

        Unit* caster = eventInfo.GetActor();
        int32 amount = CalculatePct(spellInfo->CalcPowerCost(caster, spellInfo->GetSchoolMask()), aurEff->GetAmount());
        CastSpellExtraArgs args(aurEff);
        args.AddSpellBP0(amount);
        caster->CastSpell(nullptr, SPELL_ROGUE_QUICK_RECOVERY_ENERGY, args);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_rog_quick_recovery::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// -1943 - Rupture
class spell_rog_rupture : public SpellScriptLoader
{
    public:
        static char constexpr const ScriptName[] = "spell_rog_rupture";

        spell_rog_rupture() : SpellScriptLoader(ScriptName) { }

        class spell_rog_rupture_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_rog_rupture_AuraScript);

            bool Load() override
            {
                Unit* caster = GetCaster();
                BonusDuration = 0;
                return caster && caster->GetTypeId() == TYPEID_PLAYER;
            }

            void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& canBeRecalculated)
            {
                if (Unit* caster = GetCaster())
                {
                    canBeRecalculated = false;

                    float const attackpowerPerCombo[6] =
                    {
                        0.0f,
                        0.015f,         // 1 point:  ${($m1 + $b1*1 + 0.015 * $AP) * 4} damage over 8 secs
                        0.024f,         // 2 points: ${($m1 + $b1*2 + 0.024 * $AP) * 5} damage over 10 secs
                        0.03f,          // 3 points: ${($m1 + $b1*3 + 0.03 * $AP) * 6} damage over 12 secs
                        0.03428571f,    // 4 points: ${($m1 + $b1*4 + 0.03428571 * $AP) * 7} damage over 14 secs
                        0.0375f         // 5 points: ${($m1 + $b1*5 + 0.0375 * $AP) * 8} damage over 16 secs
                    };

                    uint8 cp = caster->ToPlayer()->GetComboPoints();
                    if (cp > 5)
                        cp = 5;

                    amount += int32(caster->GetTotalAttackPowerValue(BASE_ATTACK) * attackpowerPerCombo[cp]);
                }
            }

            void ResetDuration(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                BonusDuration = 0;
            }

            void Register() override
            {
                DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_rog_rupture_AuraScript::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
                AfterEffectApply += AuraEffectApplyFn(spell_rog_rupture_AuraScript::ResetDuration, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAPPLY);
            }

        public:
            // For Glyph of Backstab use
            uint32 BonusDuration;
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_rog_rupture_AuraScript();
        }
};
char constexpr const spell_rog_rupture::ScriptName[];

// 56800 - Glyph of Backstab (dummy)
class spell_rog_glyph_of_backstab : public AuraScript
{
    PrepareAuraScript(spell_rog_glyph_of_backstab);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_GLYPH_OF_BACKSTAB_TRIGGER });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        eventInfo.GetActor()->CastSpell(eventInfo.GetProcTarget(), SPELL_ROGUE_GLYPH_OF_BACKSTAB_TRIGGER, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_rog_glyph_of_backstab::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// 63975 - Glyph of Backstab (triggered - serverside)
class spell_rog_glyph_of_backstab_triggered : public SpellScriptLoader
{
    public:
        spell_rog_glyph_of_backstab_triggered() : SpellScriptLoader("spell_rog_glyph_of_backstab_triggered") { }

        class spell_rog_glyph_of_backstab_triggered_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_rog_glyph_of_backstab_triggered_SpellScript);

            typedef spell_rog_rupture::spell_rog_rupture_AuraScript RuptureAuraScript;

            void HandleScript(SpellEffIndex effIndex)
            {
                PreventHitDefaultEffect(effIndex);

                Unit* caster = GetCaster();
                // search our Rupture aura on target
                if (AuraEffect* aurEff = GetHitUnit()->GetAuraEffect(SPELL_AURA_PERIODIC_DAMAGE, SPELLFAMILY_ROGUE, 0x00100000, 0, 0, caster->GetGUID()))
                {
                    RuptureAuraScript* ruptureAuraScript = aurEff->GetBase()->GetScript<RuptureAuraScript>(spell_rog_rupture::ScriptName);
                    if (!ruptureAuraScript)
                        return;

                    uint32& bonusDuration = ruptureAuraScript->BonusDuration;

                    // already includes duration mod from Glyph of Rupture
                    uint32 countMin = aurEff->GetBase()->GetMaxDuration();
                    uint32 countMax = countMin - bonusDuration;

                    // this glyph
                    countMax += 6000;

                    if (countMin < countMax)
                    {
                        bonusDuration += 2000;

                        aurEff->GetBase()->SetDuration(aurEff->GetBase()->GetDuration() + 2000);
                        aurEff->GetBase()->SetMaxDuration(countMin + 2000);
                    }

                }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_rog_glyph_of_backstab_triggered_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_rog_glyph_of_backstab_triggered_SpellScript();
        }
};

// -13983 - Setup
class spell_rog_setup : public AuraScript
{
    PrepareAuraScript(spell_rog_setup);

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        if (Player* target = GetTarget()->ToPlayer())
            if (eventInfo.GetActor() == target->GetSelectedUnit())
                return true;

        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_rog_setup::CheckProc);
    }
};

// 5938 - Shiv
class spell_rog_shiv : public SpellScript
{
    PrepareSpellScript(spell_rog_shiv);

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_SHIV_TRIGGERED });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (Unit* unitTarget = GetHitUnit())
            caster->CastSpell(unitTarget, SPELL_ROGUE_SHIV_TRIGGERED, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_rog_shiv::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 57934 - Tricks of the Trade
class spell_rog_tricks_of_the_trade_aura : public AuraScript
{
    PrepareAuraScript(spell_rog_tricks_of_the_trade_aura);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_ROGUE_TRICKS_OF_THE_TRADE_DMG_BOOST,
            SPELL_ROGUE_TRICKS_OF_THE_TRADE_PROC
        });
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_DEFAULT || !GetTarget()->HasAura(SPELL_ROGUE_TRICKS_OF_THE_TRADE_PROC))
            GetTarget()->GetThreatManager().UnregisterRedirectThreat(SPELL_ROGUE_TRICKS_OF_THE_TRADE);
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* rogue = GetTarget();
        Unit* target = ObjectAccessor::GetUnit(*rogue, _redirectTarget);
        if (target)
        {
            rogue->CastSpell(target, SPELL_ROGUE_TRICKS_OF_THE_TRADE_DMG_BOOST, aurEff);
            rogue->CastSpell(rogue, SPELL_ROGUE_TRICKS_OF_THE_TRADE_PROC, aurEff);
        }
        Remove(AURA_REMOVE_BY_DEFAULT);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_rog_tricks_of_the_trade_aura::OnRemove, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectProc += AuraEffectProcFn(spell_rog_tricks_of_the_trade_aura::HandleProc, EFFECT_1, SPELL_AURA_DUMMY);
    }

    ObjectGuid _redirectTarget;
public:
    void SetRedirectTarget(ObjectGuid const& guid) { _redirectTarget = guid; }
};

// 57934 - Tricks of the Trade
class spell_rog_tricks_of_the_trade : public SpellScript
{
    PrepareSpellScript(spell_rog_tricks_of_the_trade);

    void DoAfterHit()
    {
        if (Aura* aura = GetHitAura())
            if (auto* script = aura->GetScript<spell_rog_tricks_of_the_trade_aura>("spell_rog_tricks_of_the_trade"))
            {
                if (Unit* explTarget = GetExplTargetUnit())
                    script->SetRedirectTarget(explTarget->GetGUID());
                else
                    script->SetRedirectTarget(ObjectGuid::Empty);
            }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_rog_tricks_of_the_trade::DoAfterHit);
    }
};

// 59628 - Tricks of the Trade (Proc)
class spell_rog_tricks_of_the_trade_proc : public AuraScript
{
    PrepareAuraScript(spell_rog_tricks_of_the_trade_proc);

    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->GetThreatManager().UnregisterRedirectThreat(SPELL_ROGUE_TRICKS_OF_THE_TRADE);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_rog_tricks_of_the_trade_proc::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// -51698, 51700, 51701 - Honor Among Thieves
class spell_rog_honor_among_thieves : public AuraScript
{
    PrepareAuraScript(spell_rog_honor_among_thieves);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo(
        {
            SPELL_ROGUE_HONOR_AMONG_THIEVES_2,
            spellInfo->GetEffect(EFFECT_0).TriggerSpell
        });
    }

    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        Unit* caster = GetCaster();
        if (!caster || caster->HasAura(SPELL_ROGUE_HONOR_AMONG_THIEVES_2))
            return false;

        return true;
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* caster = GetCaster();
        if (!caster)
            return;

        Unit* target = GetTarget();
        target->CastSpell(target, aurEff->GetSpellEffectInfo().TriggerSpell, { aurEff, caster->GetGUID() });
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_rog_honor_among_thieves::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_rog_honor_among_thieves::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// 52916 - Honor Among Thieves (Proc)
class spell_rog_honor_among_thieves_proc : public SpellScript
{
    PrepareSpellScript(spell_rog_honor_among_thieves_proc);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_HONOR_AMONG_THIEVES_PROC });
    }

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.clear();

        Unit* target = GetOriginalCaster();
        if (!target)
            return;

        targets.push_back(target);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_rog_honor_among_thieves_proc::FilterTargets, EFFECT_0, TARGET_UNIT_CASTER_AREA_PARTY);
    }
};

class spell_rog_honor_among_thieves_proc_aura : public AuraScript
{
    PrepareAuraScript(spell_rog_honor_among_thieves_proc_aura);

    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        if (Player* player = caster->ToPlayer())
            player->CastSpell(nullptr, SPELL_ROGUE_HONOR_AMONG_THIEVES_2, true);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_rog_honor_among_thieves_proc_aura::HandleEffectApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

// -51627 - Turn the Tables
class spell_rog_turn_the_tables : public AuraScript
{
    PrepareAuraScript(spell_rog_turn_the_tables);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_0).TriggerSpell });
    }

    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* caster = GetCaster();
        if (!caster)
            return;

        caster->CastSpell(nullptr, aurEff->GetSpellEffectInfo().TriggerSpell, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_rog_turn_the_tables::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

// -11327 - Vanish
class spell_rog_vanish : public AuraScript
{
    PrepareAuraScript(spell_rog_vanish);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROGUE_STEALTH });
    }

    void ApplyStealth(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* unitTarget = GetTarget();
        unitTarget->RemoveAurasByType(SPELL_AURA_MOD_STALKED);

        // See if we already are stealthed. If so, we're done.
        if (unitTarget->HasAura(SPELL_ROGUE_STEALTH))
            return;

        // Reset cooldown on stealth if needed
        if (unitTarget->GetSpellHistory()->HasCooldown(SPELL_ROGUE_STEALTH))
            unitTarget->GetSpellHistory()->ResetCooldown(SPELL_ROGUE_STEALTH);

        unitTarget->CastSpell(nullptr, SPELL_ROGUE_STEALTH, true);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_rog_vanish::ApplyStealth, EFFECT_1, SPELL_AURA_MOD_STEALTH, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

void AddSC_rogue_spell_scripts()
{
    RegisterSpellScript(spell_rog_blade_flurry);
    RegisterSpellScript(spell_rog_cheat_death);
    RegisterSpellScript(spell_rog_cut_to_the_chase);
    RegisterSpellScript(spell_rog_deadly_brew);
    RegisterSpellScript(spell_rog_deadly_poison);
    new spell_rog_killing_spree();
    RegisterSpellScript(spell_rog_nerves_of_steel);
    RegisterSpellScriptWithArgs(spell_rog_overkill_mos<SPELL_ROGUE_OVERKILL_BUFF>, "spell_rog_overkill");
    RegisterSpellScriptWithArgs(spell_rog_overkill_mos<SPELL_ROGUE_MASTER_OF_SUBTLETY_BUFF>, "spell_rog_master_of_subtlety");
    RegisterSpellScript(spell_rog_preparation);
    RegisterSpellScript(spell_rog_prey_on_the_weak);
    RegisterSpellScript(spell_rog_quick_recovery);
    new spell_rog_rupture();
    RegisterSpellScript(spell_rog_glyph_of_backstab);
    new spell_rog_glyph_of_backstab_triggered();
    RegisterSpellScript(spell_rog_setup);
    RegisterSpellScript(spell_rog_shiv);
    RegisterSpellAndAuraScriptPair(spell_rog_tricks_of_the_trade, spell_rog_tricks_of_the_trade_aura);
    RegisterSpellScript(spell_rog_tricks_of_the_trade_proc);
    RegisterSpellScript(spell_rog_honor_among_thieves);
    RegisterSpellAndAuraScriptPair(spell_rog_honor_among_thieves_proc, spell_rog_honor_among_thieves_proc_aura);
    RegisterSpellScript(spell_rog_turn_the_tables);
    RegisterSpellScript(spell_rog_vanish);
}
