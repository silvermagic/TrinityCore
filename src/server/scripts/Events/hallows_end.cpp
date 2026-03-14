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
 * @file hallows_end.cpp
 * @brief 万圣节事件脚本模块
 *
 * 本模块实现了魔兽世界万圣节（Hallow's End）节日活动的核心功能，包括：
 * - 万圣节糖果系统：使用糖果后获得随机变身效果
 * - 恶作剧/款待系统：与旅店老板互动获取糖果或恶作剧
 * - 魔杖变身系统：使用节日魔杖将目标变成各种造型
 * - 欺诈款待系统：吃太多糖果会导致胃部不适
 *
 * 万圣节每年10月18日至11月1日举办，主要活动包括：
 * - 在各地旅店向NPC要糖果
 * - 使用魔杖对其他玩家进行变身
 * - 参加无头骑士活动
 * - 完成节日成就获得称号和奖励
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 万圣节糖果法术ID枚举
 *
 * 定义了万圣节糖果使用后可能获得的变身效果法术。
 * 每种糖果都会给玩家带来独特的视觉效果和能力增益。
 */
enum HallowEndCandysSpells
{
    SPELL_HALLOWS_END_CANDY_ORANGE_GIANT          = 24924,  ///< 橙色巨人 - 效果1：体型增大30%
    SPELL_HALLOWS_END_CANDY_SKELETON              = 24925,  ///< 骷髅 - 效果1：模型变为骷髅，效果2：水下呼吸
    SPELL_HALLOWS_END_CANDY_PIRATE                = 24926,  ///< 海盗 - 效果1：游泳速度提高50%
    SPELL_HALLOWS_END_CANDY_GHOST                 = 24927,  ///< 幽灵 - 效果1：漂浮/悬停，效果2：缓落，效果3：水上行走
    SPELL_HALLOWS_END_CANDY_FEMALE_DEFIAS_PIRATE  = 44742,  ///< 女性迪菲亚海盗 - 效果1：模型变为女性海盗，效果2：游泳速度提高50%
    SPELL_HALLOWS_END_CANDY_MALE_DEFIAS_PIRATE    = 44743   ///< 男性迪菲亚海盗 - 效果1：模型变为男性海盗，效果2：游泳速度提高50%
};

/**
 * @brief 万圣节糖果法术ID数组
 *
 * 存储基础糖果效果法术ID，用于随机选择变身效果。
 */
std::array<uint32, 4> const CandysSpells =
{
    SPELL_HALLOWS_END_CANDY_ORANGE_GIANT,  ///< 橙色巨人
    SPELL_HALLOWS_END_CANDY_SKELETON,      ///< 骷髅
    SPELL_HALLOWS_END_CANDY_PIRATE,        ///< 海盗
    SPELL_HALLOWS_END_CANDY_GHOST          ///< 幽灵
};

/**
 * @brief 万圣节糖果法术脚本 (Spell ID: 24930)
 *
 * 当玩家使用万圣节糖果时触发。随机施放一种变身效果法术。
 * 糖果效果包括：橙色巨人、骷髅、海盗、幽灵等。
 *
 * 游戏设计：增加节日的趣味性，让玩家体验不同的变身效果。
 */
// 24930 - Hallow's End Candy
class spell_hallow_end_candy : public SpellScript
{
    PrepareSpellScript(spell_hallow_end_candy);

    /**
     * @brief 验证法术依赖
     *
     * @param spellInfo 法术信息
     * @return 所有依赖法术都有效时返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CandysSpells);
    }

    /**
     * @brief 处理虚拟效果
     *
     * 随机选择一种糖果变身效果并施放在玩家身上。
     *
     * @param effIndex 法术效果索引
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CandysSpells), true);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_hallow_end_candy::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 万圣节糖果海盗光环脚本 (Spell ID: 24926)
 *
 * 处理海盗变身效果的细节。当玩家获得海盗变身时，
 * 会根据玩家性别自动应用对应的男性或女性海盗模型。
 *
 * 设计细节：游戏需要区分男女性别以应用正确的模型，
 * 这确保了变身的视觉一致性。
 */
// 24926 - Hallow's End Candy
class spell_hallow_end_candy_pirate : public AuraScript
{
    PrepareAuraScript(spell_hallow_end_candy_pirate);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_HALLOWS_END_CANDY_FEMALE_DEFIAS_PIRATE,
            SPELL_HALLOWS_END_CANDY_MALE_DEFIAS_PIRATE
        });
    }

    /**
     * @brief 处理光环应用效果
     *
     * 当海盗光环应用时，根据玩家性别施放对应的迪菲亚海盗模型法术。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 根据性别选择对应的海盗模型
        uint32 spell = GetTarget()->GetNativeGender() == GENDER_FEMALE ? SPELL_HALLOWS_END_CANDY_FEMALE_DEFIAS_PIRATE : SPELL_HALLOWS_END_CANDY_MALE_DEFIAS_PIRATE;
        GetTarget()->CastSpell(GetTarget(), spell, true);
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当海盗光环移除时，同时移除对应的迪菲亚海盗模型效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 移除对应性别的海盗模型
        uint32 spell = GetTarget()->GetNativeGender() == GENDER_FEMALE ? SPELL_HALLOWS_END_CANDY_FEMALE_DEFIAS_PIRATE : SPELL_HALLOWS_END_CANDY_MALE_DEFIAS_PIRATE;
        GetTarget()->RemoveAurasDueToSpell(spell);
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_hallow_end_candy_pirate::HandleApply, EFFECT_0, SPELL_AURA_MOD_INCREASE_SWIM_SPEED, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_hallow_end_candy_pirate::HandleRemove, EFFECT_0, SPELL_AURA_MOD_INCREASE_SWIM_SPEED, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 恶作剧法术ID枚举
 *
 * 定义了万圣节恶作剧魔杖使用的各种变身法术。
 * 这些法术可以将目标变成不同的节日造型。
 */
enum TrickSpells
{
    SPELL_PIRATE_COSTUME_MALE           = 24708,  ///< 海盗造型（男性）
    SPELL_PIRATE_COSTUME_FEMALE         = 24709,  ///< 海盗造型（女性）
    SPELL_NINJA_COSTUME_MALE            = 24710,  ///< 忍者造型（男性）
    SPELL_NINJA_COSTUME_FEMALE          = 24711,  ///< 忍者造型（女性）
    SPELL_LEPER_GNOME_COSTUME_MALE      = 24712,  ///< 麻风侏儒造型（男性）
    SPELL_LEPER_GNOME_COSTUME_FEMALE    = 24713,  ///< 麻风侏儒造型（女性）
    SPELL_SKELETON_COSTUME              = 24723,  ///< 骷髅造型（无性别差异）
    SPELL_GHOST_COSTUME_MALE            = 24735,  ///< 幽灵造型（男性）
    SPELL_GHOST_COSTUME_FEMALE          = 24736,  ///< 幽灵造型（女性）
    SPELL_TRICK_BUFF                    = 24753,  ///< 恶作剧增益效果（无变身）
};

/**
 * @brief 恶作剧法术脚本 (Spell ID: 24750)
 *
 * 当玩家受到恶作剧效果时，随机获得一种变身效果。
 * 变身类型包括：海盗、忍者、麻风侏儒、骷髅、幽灵，或者仅获得恶作剧增益效果。
 *
 * 游戏机制：
 * - 50%概率获得某种变身（随机选择5种之一）
 * - 16.7%概率仅获得恶作剧增益效果（无变身）
 * - 变身效果会根据目标性别选择对应的模型
 */
// 24750 - Trick
class spell_hallow_end_trick : public SpellScript
{
    PrepareSpellScript(spell_hallow_end_trick);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_PIRATE_COSTUME_MALE,
            SPELL_PIRATE_COSTUME_FEMALE,
            SPELL_NINJA_COSTUME_MALE,
            SPELL_NINJA_COSTUME_FEMALE,
            SPELL_LEPER_GNOME_COSTUME_MALE,
            SPELL_LEPER_GNOME_COSTUME_FEMALE,
            SPELL_SKELETON_COSTUME,
            SPELL_GHOST_COSTUME_MALE,
            SPELL_GHOST_COSTUME_FEMALE,
            SPELL_TRICK_BUFF
        });
    }

    /**
     * @brief 处理脚本效果
     *
     * 随机选择一种变身效果并应用到目标玩家身上。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (Player* target = GetHitPlayer())
        {
            uint8 gender = target->GetNativeGender();
            uint32 spellId = SPELL_TRICK_BUFF;  // 默认为恶作剧增益效果

            // 随机选择变身类型（0-5，共6种可能）
            switch (urand(0, 5))
            {
                case 1:  // 麻风侏儒变身
                    spellId = gender == GENDER_FEMALE ? SPELL_LEPER_GNOME_COSTUME_FEMALE : SPELL_LEPER_GNOME_COSTUME_MALE;
                    break;
                case 2:  // 海盗变身
                    spellId = gender == GENDER_FEMALE ? SPELL_PIRATE_COSTUME_FEMALE : SPELL_PIRATE_COSTUME_MALE;
                    break;
                case 3:  // 幽灵变身
                    spellId = gender == GENDER_FEMALE ? SPELL_GHOST_COSTUME_FEMALE : SPELL_GHOST_COSTUME_MALE;
                    break;
                case 4:  // 忍者变身
                    spellId = gender == GENDER_FEMALE ? SPELL_NINJA_COSTUME_FEMALE : SPELL_NINJA_COSTUME_MALE;
                    break;
                case 5:  // 骷髅变身（无性别差异）
                    spellId = SPELL_SKELETON_COSTUME;
                    break;
                default:  // case 0: 恶作剧增益效果
                    break;
            }

            caster->CastSpell(target, spellId, true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_hallow_end_trick::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 恶作剧或款待法术ID枚举
 *
 * 定义了万圣节"要糖果"互动系统中的法术ID。
 */
enum TrickOrTreatSpells
{
    SPELL_TRICK                 = 24714,  ///< 恶作剧 - 使目标获得随机变身效果
    SPELL_TREAT                 = 24715,  ///< 款待 - 给目标一个万圣节糖果
    SPELL_TRICKED_OR_TREATED    = 24755,  ///< 已恶作剧或已款待 - 防止重复互动的冷却标记
    SPELL_TRICKY_TREAT_SPEED    = 42919,  ///< 欺诈款待速度 - 提高移动速度的效果
    SPELL_TRICKY_TREAT_TRIGGER  = 42965,  ///< 欺诈款待触发 - 用于检测吃糖果数量的触发器
    SPELL_UPSET_TUMMY           = 42966   ///< 胃部不适 - 吃太多糖果后的负面效果
};

/**
 * @brief 恶作剧或款待法术脚本 (Spell ID: 24751)
 *
 * 当玩家与旅店老板互动"要糖果"时触发。
 * 50%概率获得恶作剧效果，50%概率获得糖果款待。
 * 同时会标记玩家已参与过互动，防止短时间内重复互动。
 */
// 24751 - Trick or Treat
class spell_hallow_end_trick_or_treat : public SpellScript
{
    PrepareSpellScript(spell_hallow_end_trick_or_treat);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_TRICK, SPELL_TREAT, SPELL_TRICKED_OR_TREATED });
    }

    /**
     * @brief 处理脚本效果
     *
     * 50%概率施放恶作剧法术，50%概率施放款待法术。
     * 同时施放冷却标记法术，防止玩家频繁互动。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        if (Player* target = GetHitPlayer())
        {
            // 50%概率恶作剧，50%概率款待
            caster->CastSpell(target, roll_chance_i(50) ? SPELL_TRICK : SPELL_TREAT, true);
            // 施放冷却标记，防止重复互动
            caster->CastSpell(target, SPELL_TRICKED_OR_TREATED, true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_hallow_end_trick_or_treat::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 欺诈款待法术脚本 (Spell ID: 44436)
 *
 * 处理万圣节特殊糖果"欺诈款待"的效果。
 * 这种糖果会提供速度增益，但如果玩家在短时间内吃太多，
 * 会导致"胃部不适"的负面效果。
 *
 * 游戏机制：
 * - 吃糖果时获得速度增益
 * - 如果已有触发器光环且速度增益超过3层，有33%概率导致胃部不适
 * - 胃部不适会让玩家无法继续享受糖果的效果
 */
// 44436 - Tricky Treat
class spell_hallow_end_tricky_treat : public SpellScript
{
    PrepareSpellScript(spell_hallow_end_tricky_treat);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_TRICKY_TREAT_SPEED,
            SPELL_TRICKY_TREAT_TRIGGER,
            SPELL_UPSET_TUMMY
        });
    }

    /**
     * @brief 处理脚本效果
     *
     * 检查玩家是否吃太多糖果，如果是，有概率导致胃部不适。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        // 如果玩家已有触发器且速度增益超过3层，有33%概率胃部不适
        if (caster->HasAura(SPELL_TRICKY_TREAT_TRIGGER) && caster->GetAuraCount(SPELL_TRICKY_TREAT_SPEED) > 3 && roll_chance_i(33))
            caster->CastSpell(caster, SPELL_UPSET_TUMMY, true);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_hallow_end_tricky_treat::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 圣洁魔杖法术ID枚举
 *
 * 定义了万圣节期间可获得的圣洁魔杖法术。
 * 这些魔杖可以对其他玩家使用，使其获得对应的变身效果。
 */
enum HallowendData
{
    SPELL_HALLOWED_WAND_PIRATE             = 24717,  ///< 圣洁魔杖：海盗
    SPELL_HALLOWED_WAND_NINJA              = 24718,  ///< 圣洁魔杖：忍者
    SPELL_HALLOWED_WAND_LEPER_GNOME        = 24719,  ///< 圣洁魔杖：麻风侏儒
    SPELL_HALLOWED_WAND_RANDOM             = 24720,  ///< 圣洁魔杖：随机变身
    SPELL_HALLOWED_WAND_SKELETON           = 24724,  ///< 圣洁魔杖：骷髅
    SPELL_HALLOWED_WAND_WISP               = 24733,  ///< 圣洁魔杖：鬼火
    SPELL_HALLOWED_WAND_GHOST              = 24737,  ///< 圣洁魔杖：幽灵
    SPELL_HALLOWED_WAND_BAT                = 24741   ///< 圣洁魔杖：蝙蝠
};

/**
 * @brief 圣洁魔杖法术脚本
 *
 * 处理各种圣洁魔杖的使用效果。魔杖可以对其他玩家使用，
 * 根据魔杖类型将目标变成对应的造型。
 *
 * 支持的魔杖类型：
 * - 24717: 圣洁魔杖-海盗
 * - 24718: 圣洁魔杖-忍者
 * - 24719: 圣洁魔杖-麻风侏儒
 * - 24720: 圣洁魔杖-随机
 * - 24724: 圣洁魔杖-骷髅
 * - 24733: 圣洁魔杖-鬼火
 * - 24737: 圣洁魔杖-幽灵
 * - 24741: 圣洁魔杖-蝙蝠
 *
 * 注意：随机魔杖会随机选择一种变身效果。
 */
// 24717, 24718, 24719, 24720, 24724, 24733, 24737, 24741
class spell_hallow_end_wand : public SpellScript
{
    PrepareSpellScript(spell_hallow_end_wand);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellEntry*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_PIRATE_COSTUME_MALE,
            SPELL_PIRATE_COSTUME_FEMALE,
            SPELL_NINJA_COSTUME_MALE,
            SPELL_NINJA_COSTUME_FEMALE,
            SPELL_LEPER_GNOME_COSTUME_MALE,
            SPELL_LEPER_GNOME_COSTUME_FEMALE,
            SPELL_GHOST_COSTUME_MALE,
            SPELL_GHOST_COSTUME_FEMALE
        });
    }

    /**
     * @brief 处理脚本效果
     *
     * 根据使用的魔杖类型，施放对应的变身法术。
     * 对于有性别差异的变身，会根据目标性别选择正确的模型。
     */
    void HandleScriptEffect()
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        uint32 spellId = 0;
        uint8 gender = target->GetNativeGender();

        // 根据魔杖类型选择对应的变身效果
        switch (GetSpellInfo()->Id)
        {
            case SPELL_HALLOWED_WAND_LEPER_GNOME:
                // 麻风侏儒变身，区分性别
                spellId = gender ? SPELL_LEPER_GNOME_COSTUME_FEMALE : SPELL_LEPER_GNOME_COSTUME_MALE;
                break;
            case SPELL_HALLOWED_WAND_PIRATE:
                // 海盗变身，区分性别
                spellId = gender ? SPELL_PIRATE_COSTUME_FEMALE : SPELL_PIRATE_COSTUME_MALE;
                break;
            case SPELL_HALLOWED_WAND_GHOST:
                // 幽灵变身，区分性别
                spellId = gender ? SPELL_GHOST_COSTUME_FEMALE : SPELL_GHOST_COSTUME_MALE;
                break;
            case SPELL_HALLOWED_WAND_NINJA:
                // 忍者变身，区分性别
                spellId = gender ? SPELL_NINJA_COSTUME_FEMALE : SPELL_NINJA_COSTUME_MALE;
                break;
            case SPELL_HALLOWED_WAND_RANDOM:
                // 随机魔杖：随机选择一种魔杖效果（递归调用自身）
                spellId = RAND(SPELL_HALLOWED_WAND_PIRATE, SPELL_HALLOWED_WAND_NINJA, SPELL_HALLOWED_WAND_LEPER_GNOME, SPELL_HALLOWED_WAND_SKELETON, SPELL_HALLOWED_WAND_WISP, SPELL_HALLOWED_WAND_GHOST, SPELL_HALLOWED_WAND_BAT);
                break;
            default:
                return;
        }
        caster->CastSpell(target, spellId, true);
    }

    /**
     * @brief 注册法术命中回调
     */
    void Register() override
    {
        AfterHit += SpellHitFn(spell_hallow_end_wand::HandleScriptEffect);
    }
};

/**
 * @brief 注册所有万圣节脚本
 *
 * 此函数在服务器启动时被调用，用于注册本文件中定义的所有法术脚本。
 * 将脚本与对应的法术ID关联起来，使游戏能够正确处理万圣节相关的法术效果。
 */
void AddSC_event_hallows_end()
{
    // 糖果系统
    RegisterSpellScript(spell_hallow_end_candy);
    RegisterSpellScript(spell_hallow_end_candy_pirate);

    // 恶作剧系统
    RegisterSpellScript(spell_hallow_end_trick);
    RegisterSpellScript(spell_hallow_end_trick_or_treat);
    RegisterSpellScript(spell_hallow_end_tricky_treat);

    // 魔杖系统
    RegisterSpellScript(spell_hallow_end_wand);
}
