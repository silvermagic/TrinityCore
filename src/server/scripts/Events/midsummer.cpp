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
 * @file    midsummer.cpp
 * @brief   仲夏火焰节事件脚本模块
 *
 * 本模块实现了仲夏火焰节（Midsummer Fire Festival）相关的游戏机制，包括：
 * - 火炬投掷训练（Torch Tossing）：玩家向火盆投掷火炬的小游戏
 * - 火炬接球游戏（Torch Catching）：玩家接住抛出的火炬
 * - 彩带柱舞蹈（Ribbon Pole Dance）：围绕彩带柱跳舞获得增益效果
 * - 火焰节杂耍（Juggling Torch）：杂耍火炬的娱乐活动
 *
 * 这些活动是仲夏火焰节期间玩家可以参与的节日任务和小游戏，
 * 完成后可以获得节日奖励和成就。
 */

#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 火炬投掷相关法术ID枚举
 *
 * 定义了火炬投掷训练和练习所需的法术标识符
 */
enum TorchSpells
{
    SPELL_TORCH_TOSSING_TRAINING                    = 45716, ///< 火炬投掷训练（新手任务）
    SPELL_TORCH_TOSSING_PRACTICE                    = 46630, ///< 火炬投掷练习（日常任务）
    SPELL_TORCH_TOSSING_TRAINING_SUCCESS_ALLIANCE   = 45719, ///< 联盟火炬投掷训练成功
    SPELL_TORCH_TOSSING_TRAINING_SUCCESS_HORDE      = 46651, ///< 部落火炬投掷训练成功
    SPELL_TARGET_INDICATOR_COSMETIC                 = 46901, ///< 目标指示器视觉效果
    SPELL_TARGET_INDICATOR                          = 45723, ///< 目标指示器
    SPELL_BRAZIERS_HIT                              = 45724  ///< 击中火盆光环
};

/**
 * @class spell_midsummer_braziers_hit
 * @brief 火盆击中光环脚本 - 处理火炬投掷训练的计分逻辑
 *
 * 当玩家成功将火炬投掷到火盆中时，会获得此光环的叠加层数。
 * 当达到所需层数时（训练模式8层，练习模式20层），
 * 会触发成功完成任务的法术效果。
 */
// 45724 - Braziers Hit!
class spell_midsummer_braziers_hit : public AuraScript
{
    PrepareAuraScript(spell_midsummer_braziers_hit);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true，否则返回false
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_TORCH_TOSSING_TRAINING,
            SPELL_TORCH_TOSSING_PRACTICE,
            SPELL_TORCH_TOSSING_TRAINING_SUCCESS_ALLIANCE,
            SPELL_TORCH_TOSSING_TRAINING_SUCCESS_HORDE
        });
    }

    /**
     * @brief 处理光环效果应用/叠加
     * @param aurEff 光环效果（未使用）
     * @param mode 处理模式（未使用）
     *
     * 每次光环叠加时检查是否达到目标层数：
     * - 训练模式需要8次击中
     * - 练习模式需要20次击中
     * 完成后根据阵营给予成功法术并移除光环
     */
    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        // 检查是否达到所需层数：训练8层，练习20层
        if ((player->HasAura(SPELL_TORCH_TOSSING_TRAINING) && GetStackAmount() == 8) || (player->HasAura(SPELL_TORCH_TOSSING_PRACTICE) && GetStackAmount() == 20))
        {
            // 根据玩家阵营施放不同的成功法术
            if (player->GetTeam() == ALLIANCE)
                player->CastSpell(player, SPELL_TORCH_TOSSING_TRAINING_SUCCESS_ALLIANCE, true);
            else if (player->GetTeam() == HORDE)
                player->CastSpell(player, SPELL_TORCH_TOSSING_TRAINING_SUCCESS_HORDE, true);
            Remove();
        }
    }

    void Register() override
    {
        // 注册光环重新应用时的处理函数（叠加时触发）
        AfterEffectApply += AuraEffectApplyFn(spell_midsummer_braziers_hit::HandleEffectApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAPPLY);
    }
};

/**
 * @class spell_midsummer_torch_target_picker
 * @brief 火炬目标选择器法术脚本 - 为火炬投掷选择目标位置
 *
 * 这个法术用于火炬投掷训练中，选择并标记火炬的目标位置。
 * 它会在目标位置创建视觉指示器，帮助玩家瞄准。
 */
// 45907 - Torch Target Picker
class spell_midsummer_torch_target_picker : public SpellScript
{
    PrepareSpellScript(spell_midsummer_torch_target_picker);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_TARGET_INDICATOR_COSMETIC, SPELL_TARGET_INDICATOR });
    }

    /**
     * @brief 处理脚本效果 - 为目标添加指示器
     * @param effIndex 效果索引（未使用）
     *
     * 在目标位置施放视觉指示器法术，用于显示火炬即将落下的位置
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        // 施放视觉效果和功能指示器
        target->CastSpell(target, SPELL_TARGET_INDICATOR_COSMETIC, true);
        target->CastSpell(target, SPELL_TARGET_INDICATOR, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_midsummer_torch_target_picker::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_midsummer_torch_toss_land
 * @brief 火炬落地法术脚本 - 处理火炬落在目标位置的效果
 *
 * 当火炬落到目标位置时触发，使目标（通常是火盆）对施法者施放
 * "火盆击中"光环，从而增加玩家的击中计数。
 */
// 46054 - Torch Toss (land)
class spell_midsummer_torch_toss_land : public SpellScript
{
    PrepareSpellScript(spell_midsummer_torch_toss_land);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BRAZIERS_HIT });
    }

    /**
     * @brief 处理脚本效果 - 让目标对施法者施放火盆击中光环
     * @param effIndex 效果索引（未使用）
     *
     * 火炬落地时，目标（火盆）会对施法者（玩家）施放火盆击中光环，
     * 从而增加玩家的火炬投掷命中计数
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetCaster(), SPELL_BRAZIERS_HIT, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_midsummer_torch_toss_land::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 彩带柱相关数据枚举
 *
 * 定义了彩带柱舞蹈活动所需的法术标识符
 */
enum RibbonPoleData
{
    SPELL_HAS_FULL_MIDSUMMER_SET      = 58933, ///< 拥有完整仲夏套装
    SPELL_BURNING_HOT_POLE_DANCE      = 58934, ///< 燃烧热舞（成就奖励）
    SPELL_RIBBON_POLE_PERIODIC_VISUAL = 45406, ///< 彩带柱周期性视觉效果
    SPELL_RIBBON_DANCE                = 29175, ///< 彩带舞蹈增益
    SPELL_TEST_RIBBON_POLE_1          = 29705, ///< 测试彩带柱通道1
    SPELL_TEST_RIBBON_POLE_2          = 29726, ///< 测试彩带柱通道2
    SPELL_TEST_RIBBON_POLE_3          = 29727  ///< 测试彩带柱通道3
};

/**
 * @class spell_midsummer_test_ribbon_pole_channel
 * @brief 彩带柱通道光环脚本 - 处理围绕彩带柱跳舞的增益累积
 *
 * 当玩家围绕彩带柱跳舞时，这个光环会周期性地：
 * 1. 更新视觉效果
 * 2. 增加彩带舞蹈增益的持续时间（每次3分钟，最高60分钟）
 * 3. 如果达到最大持续时间且穿着完整仲夏套装，施放"燃烧热舞"成就奖励
 */
// 29705, 29726, 29727 - Test Ribbon Pole Channel
class spell_midsummer_test_ribbon_pole_channel : public AuraScript
{
    PrepareAuraScript(spell_midsummer_test_ribbon_pole_channel);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_RIBBON_POLE_PERIODIC_VISUAL,
            SPELL_BURNING_HOT_POLE_DANCE,
            SPELL_HAS_FULL_MIDSUMMER_SET,
            SPELL_RIBBON_DANCE
        });
    }

    /**
     * @brief 处理光环移除 - 清理视觉效果
     * @param aurEff 光环效果（未使用）
     * @param mode 处理模式（未使用）
     */
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_RIBBON_POLE_PERIODIC_VISUAL);
    }

    /**
     * @brief 周期性触发效果 - 更新视觉效果和增益持续时间
     * @param aurEff 光环效果（未使用）
     *
     * 每次周期触发时：
     * 1. 施放周期性视觉效果
     * 2. 如果已有彩带舞蹈增益，增加其最大持续时间（最多60分钟）
     * 3. 如果达到60分钟且穿着完整仲夏套装，给予"燃烧热舞"奖励
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_RIBBON_POLE_PERIODIC_VISUAL, true);

        if (Aura* aur = target->GetAura(SPELL_RIBBON_DANCE))
        {
            // 增加最大持续时间，每次3分钟（180000毫秒），最多60分钟（3600000毫秒）
            aur->SetMaxDuration(std::min(3600000, aur->GetMaxDuration() + 180000));
            aur->RefreshDuration();

            // 如果达到最大持续时间且穿着完整仲夏套装，给予燃烧热舞成就
            if (aur->GetMaxDuration() == 3600000 && target->HasAura(SPELL_HAS_FULL_MIDSUMMER_SET))
                target->CastSpell(target, SPELL_BURNING_HOT_POLE_DANCE, true);
        }
        else
            target->CastSpell(target, SPELL_RIBBON_DANCE, true);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_midsummer_test_ribbon_pole_channel::HandleRemove, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_midsummer_test_ribbon_pole_channel::PeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @class spell_midsummer_ribbon_pole_periodic_visual
 * @brief 彩带柱周期性视觉效果光环脚本
 *
 * 这个光环负责维护彩带柱舞蹈的视觉效果。
 * 当玩家停止跳舞（不再持有彩带柱通道光环）时，
 * 这个视觉效果会被自动移除。
 */
// 45406 - Holiday - Midsummer, Ribbon Pole Periodic Visual
class spell_midsummer_ribbon_pole_periodic_visual : public AuraScript
{
    PrepareAuraScript(spell_midsummer_ribbon_pole_periodic_visual);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_TEST_RIBBON_POLE_1,
            SPELL_TEST_RIBBON_POLE_2,
            SPELL_TEST_RIBBON_POLE_3
        });
    }

    /**
     * @brief 周期性检查玩家是否仍在跳舞
     * @param aurEff 光环效果（未使用）
     *
     * 如果玩家不再持有任何彩带柱通道光环，则移除此视觉效果
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        // 如果玩家不再持有任何彩带柱通道光环，移除视觉效果
        if (!target->HasAura(SPELL_TEST_RIBBON_POLE_1) && !target->HasAura(SPELL_TEST_RIBBON_POLE_2) && !target->HasAura(SPELL_TEST_RIBBON_POLE_3))
            Remove();
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_midsummer_ribbon_pole_periodic_visual::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 杂耍火炬相关法术和任务枚举
 *
 * 定义了杂耍火炬活动所需的法术标识符和任务ID
 */
enum JugglingTorch
{
    SPELL_JUGGLE_TORCH_SLOW          = 45792, ///< 慢速杂耍火炬
    SPELL_JUGGLE_TORCH_MEDIUM        = 45806, ///< 中速杂耍火炬
    SPELL_JUGGLE_TORCH_FAST          = 45816, ///< 快速杂耍火炬
    SPELL_JUGGLE_TORCH_SELF          = 45638, ///< 自身杂耍火炬

    SPELL_JUGGLE_TORCH_SHADOW_SLOW   = 46120, ///< 慢速杂耍火炬阴影
    SPELL_JUGGLE_TORCH_SHADOW_MEDIUM = 46118, ///< 中速杂耍火炬阴影
    SPELL_JUGGLE_TORCH_SHADOW_FAST   = 46117, ///< 快速杂耍火炬阴影
    SPELL_JUGGLE_TORCH_SHADOW_SELF   = 46121, ///< 自身杂耍火炬阴影

    SPELL_GIVE_TORCH                 = 45280, ///< 给予火炬
    QUEST_TORCH_CATCHING_A           = 11657, ///< 火炬接球（联盟）
    QUEST_TORCH_CATCHING_H           = 11923, ///< 火炬接球（部落）
    QUEST_MORE_TORCH_CATCHING_A      = 11924, ///< 更多火炬接球（联盟）
    QUEST_MORE_TORCH_CATCHING_H      = 11925  ///< 更多火炬接球（部落）
};

/**
 * @class spell_midsummer_juggle_torch
 * @brief 杂耍火炬法术脚本 - 根据距离选择不同的火炬投掷速度
 *
 * 当玩家投掷火炬时，根据目标距离自动选择合适的投掷速度：
 * - 距离 <= 1.5码：自身投掷
 * - 距离 <= 10码：慢速投掷
 * - 距离 <= 20码：中速投掷
 * - 距离 > 20码：快速投掷
 */
// 45819 - Throw Torch
class spell_midsummer_juggle_torch : public SpellScript
{
    PrepareSpellScript(spell_midsummer_juggle_torch);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({
            SPELL_JUGGLE_TORCH_SLOW, SPELL_JUGGLE_TORCH_MEDIUM, SPELL_JUGGLE_TORCH_FAST,
            SPELL_JUGGLE_TORCH_SELF, SPELL_JUGGLE_TORCH_SHADOW_SLOW, SPELL_JUGGLE_TORCH_SHADOW_MEDIUM,
            SPELL_JUGGLE_TORCH_SHADOW_FAST, SPELL_JUGGLE_TORCH_SHADOW_SELF
        });
    }

    /**
     * @brief 处理虚拟效果 - 根据距离选择火炬投掷速度
     * @param effIndex 效果索引（未使用）
     *
     * 根据施法者到目标位置的距离选择合适的火炬法术，
     * 并同时施放火炬和阴影效果以获得更好的视觉效果
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (!GetExplTargetDest())
            return;

        Position spellDest = *GetExplTargetDest();
        float distance = GetCaster()->GetExactDist2d(spellDest.GetPositionX(), spellDest.GetPositionY());

        uint32 torchSpellID = 0;
        uint32 torchShadowSpellID = 0;

        // 根据距离选择不同的火炬投掷速度
        if (distance <= 1.5f)
        {
            // 近距离：自身投掷
            torchSpellID = SPELL_JUGGLE_TORCH_SELF;
            torchShadowSpellID = SPELL_JUGGLE_TORCH_SHADOW_SELF;
            spellDest = GetCaster()->GetPosition();
        }
        else if (distance <= 10.0f)
        {
            // 短距离：慢速投掷
            torchSpellID = SPELL_JUGGLE_TORCH_SLOW;
            torchShadowSpellID = SPELL_JUGGLE_TORCH_SHADOW_SLOW;
        }
        else if (distance <= 20.0f)
        {
            // 中距离：中速投掷
            torchSpellID = SPELL_JUGGLE_TORCH_MEDIUM;
            torchShadowSpellID = SPELL_JUGGLE_TORCH_SHADOW_MEDIUM;
        }
        else
        {
            // 远距离：快速投掷
            torchSpellID = SPELL_JUGGLE_TORCH_FAST;
            torchShadowSpellID = SPELL_JUGGLE_TORCH_SHADOW_FAST;
        }

        // 施放火炬和阴影效果
        GetCaster()->CastSpell(spellDest, torchSpellID);
        GetCaster()->CastSpell(spellDest, torchShadowSpellID);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_midsummer_juggle_torch::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_midsummer_torch_catch
 * @brief 火炬接球法术脚本 - 处理玩家接住杂耍火炬
 *
 * 当玩家成功接住杂耍火炬时，如果已完成火炬接球任务，
 * 会给予玩家一个新的火炬以便继续游戏。
 */
// 45644 - Juggle Torch (Catch)
class spell_midsummer_torch_catch : public SpellScript
{
    PrepareSpellScript(spell_midsummer_torch_catch);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_GIVE_TORCH });
    }

    /**
     * @brief 处理虚拟效果 - 给予火炬
     * @param effIndex 效果索引（未使用）
     *
     * 如果玩家已完成火炬接球任务，则给予一个新的火炬
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player)
            return;

        // 只有已完成火炬接球任务的玩家才会获得火炬
        if (player->GetQuestStatus(QUEST_TORCH_CATCHING_A) == QUEST_STATUS_REWARDED || player->GetQuestStatus(QUEST_TORCH_CATCHING_H) == QUEST_STATUS_REWARDED)
            player->CastSpell(player, SPELL_GIVE_TORCH);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_midsummer_torch_catch::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 抛掷火炬相关法术枚举
 *
 * 定义了火炬接球任务所需的法术标识符
 */
enum FlingTorch
{
    SPELL_FLING_TORCH_TRIGGERED           = 45669, ///< 抛掷火炬触发
    SPELL_FLING_TORCH_SHADOW              = 46105, ///< 抛掷火炬阴影
    SPELL_JUGGLE_TORCH_MISSED             = 45676, ///< 杂耍火炬未接住
    SPELL_TORCHES_CAUGHT                  = 45693, ///< 已接住的火炬计数
    SPELL_TORCH_CATCHING_SUCCESS_ALLIANCE = 46081, ///< 火炬接球成功（联盟）
    SPELL_TORCH_CATCHING_SUCCESS_HORDE    = 46654, ///< 火炬接球成功（部落）
    SPELL_TORCH_CATCHING_REMOVE_TORCHES   = 46084  ///< 移除火炬
};

/**
 * @class spell_midsummer_fling_torch
 * @brief 抛掷火炬法术脚本 - 随机抛出火炬供玩家接住
 *
 * 在火炬接球任务中，玩家使用此法术将火炬抛向随机方向，
 * 然后需要移动到火炬落点接住它。
 */
// 46747 - Fling torch
class spell_midsummer_fling_torch : public SpellScript
{
    PrepareSpellScript(spell_midsummer_fling_torch);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLING_TORCH_TRIGGERED, SPELL_FLING_TORCH_SHADOW });
    }

    /**
     * @brief 处理虚拟效果 - 随机方向抛出火炬
     * @param effIndex 效果索引（未使用）
     *
     * 计算一个随机的目标位置（30码内的随机方向），
     * 然后向该位置抛出火炬和阴影效果
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        // 计算随机方向的目标位置，最大距离30码
        Position dest = GetCaster()->GetFirstCollisionPosition(30.0f, (float)rand_norm() * static_cast<float>(2 * M_PI));
        GetCaster()->CastSpell(dest, SPELL_FLING_TORCH_TRIGGERED, true);
        GetCaster()->CastSpell(dest, SPELL_FLING_TORCH_SHADOW);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_midsummer_fling_torch::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_midsummer_fling_torch_triggered
 * @brief 抛掷火炬触发法术脚本 - 检测玩家是否成功接住火炬
 *
 * 当火炬落地时，检测施法者（玩家）是否在火炬落点附近。
 * 如果玩家距离太远（超过3码），则视为未接住，重置计数。
 */
// 45669 - Fling Torch
class spell_midsummer_fling_torch_triggered : public SpellScript
{
    PrepareSpellScript(spell_midsummer_fling_torch_triggered);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_JUGGLE_TORCH_MISSED });
    }

    /**
     * @brief 处理触发飞弹效果 - 检测是否成功接住
     * @param effIndex 效果索引
     *
     * 如果玩家距离火炬落点超过3码，则视为未接住：
     * - 阻止默认效果
     * - 施放"未接住"法术
     * - 移除已接住火炬的计数光环
     */
    void HandleTriggerMissile(SpellEffIndex effIndex)
    {
        if (Position const* pos = GetHitDest())
        {
            // 如果玩家距离火炬落点超过3码，视为未接住
            if (GetCaster()->GetExactDist2d(pos) > 3.0f)
            {
                PreventHitEffect(effIndex);
                GetCaster()->CastSpell(*GetExplTargetDest(), SPELL_JUGGLE_TORCH_MISSED);
                GetCaster()->RemoveAura(SPELL_TORCHES_CAUGHT);
            }
        }
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_midsummer_fling_torch_triggered::HandleTriggerMissile, EFFECT_0, SPELL_EFFECT_TRIGGER_MISSILE);
    }
};

/**
 * @class spell_midsummer_fling_torch_catch
 * @brief 抛掷火炬接球法术脚本 - 处理火炬接球任务的核心逻辑
 *
 * 这是火炬接球任务的核心脚本，处理：
 * 1. 验证是否是施法者自己接住了火炬
 * 2. 根据任务类型确定需要接住的次数
 * 3. 达到目标后完成任务
 * 4. 未达到目标时继续抛出下一个火炬
 */
// 45671 - Juggle Torch (Catch, Quest)
class spell_midsummer_fling_torch_catch : public SpellScript
{
    PrepareSpellScript(spell_midsummer_fling_torch_catch);

    /**
     * @brief 验证所需法术是否存在
     * @param spellInfo 法术信息（未使用）
     * @return 所有依赖法术都存在返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({
            SPELL_FLING_TORCH_TRIGGERED,
            SPELL_TORCH_CATCHING_SUCCESS_ALLIANCE,
            SPELL_TORCH_CATCHING_SUCCESS_HORDE,
            SPELL_TORCH_CATCHING_REMOVE_TORCHES,
            SPELL_FLING_TORCH_SHADOW
        });
    }

    /**
     * @brief 处理脚本效果 - 接住火炬并继续游戏
     * @param effIndex 效果索引（未使用）
     *
     * 当玩家成功接住火炬时：
     * 1. 验证是否是施法者自己（只有施法者能接住自己的火炬）
     * 2. 确定所需接住次数（普通任务4次，日常任务10次）
     * 3. 如果达到目标，完成任务并清理
     * 4. 如果未达到目标，增加计数并抛出下一个火炬
     */
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Player* player = GetHitPlayer();
        if (!player)
            return;

        if (!GetExplTargetDest())
            return;

        // 只有施法者自己可以接住火炬
        if (player->GetGUID() != GetCaster()->GetGUID())
            return;

        uint8 requiredCatches = 0;
        // 根据任务确定需要接住的次数：普通任务需要4次（层数3+1），日常任务需要10次（层数9+1）
        if (player->GetQuestStatus(QUEST_TORCH_CATCHING_A) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(QUEST_TORCH_CATCHING_H) == QUEST_STATUS_INCOMPLETE)
            requiredCatches = 3;
        else if (player->GetQuestStatus(QUEST_MORE_TORCH_CATCHING_A) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(QUEST_MORE_TORCH_CATCHING_H) == QUEST_STATUS_INCOMPLETE)
            requiredCatches = 9;

        // 如果没有任务，不执行任何操作
        if (requiredCatches == 0)
            return;

        // 检查是否达到所需接住次数
        if (player->GetAuraCount(SPELL_TORCHES_CAUGHT) >= requiredCatches)
        {
            // 达到目标：施放成功法术并清理
            player->CastSpell(player, (player->GetTeam() == ALLIANCE) ? SPELL_TORCH_CATCHING_SUCCESS_ALLIANCE : SPELL_TORCH_CATCHING_SUCCESS_HORDE);
            player->CastSpell(player, SPELL_TORCH_CATCHING_REMOVE_TORCHES);
            player->RemoveAura(SPELL_TORCHES_CAUGHT);
        }
        else
        {
            // 未达到目标：增加计数并抛出下一个火炬
            Position dest = player->GetFirstCollisionPosition(15.0f, (float)rand_norm() * static_cast<float>(2 * M_PI));
            player->CastSpell(player, SPELL_TORCHES_CAUGHT);
            player->CastSpell(dest, SPELL_FLING_TORCH_TRIGGERED, true);
            player->CastSpell(dest, SPELL_FLING_TORCH_SHADOW);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_midsummer_fling_torch_catch::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_midsummer_fling_torch_missed
 * @brief 抛掷火炬未接住法术脚本 - 处理玩家未接住火炬的情况
 *
 * 当玩家未能接住抛出的火炬时触发。这个法术只影响施法者自己，
 * 会重置接球计数，玩家需要重新开始。
 */
// 45676 - Juggle Torch (Quest, Missed)
class spell_midsummer_fling_torch_missed : public SpellScript
{
    PrepareSpellScript(spell_midsummer_fling_torch_missed);

    /**
     * @brief 过滤目标 - 只允许施法者自己受影响
     * @param targets 目标列表
     *
     * 从目标列表中移除所有非施法者的目标，
     * 确保此法术只影响施法者自己
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        // 此法术只影响施法者自己
        targets.remove_if([this](WorldObject* obj)
            {
                return obj->GetGUID() != GetCaster()->GetGUID();
            });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_midsummer_fling_torch_missed::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENTRY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_midsummer_fling_torch_missed::FilterTargets, EFFECT_2, TARGET_UNIT_DEST_AREA_ENTRY);
    }
};

/**
 * @brief 注册仲夏火焰节事件法术脚本
 *
 * 此函数在服务器启动时被调用，用于注册所有仲夏火焰节相关的法术脚本。
 * 每个法术脚本需要通过RegisterSpellScript宏进行注册，
 * 这样对应的法术才能使用自定义的脚本逻辑。
 */
void AddSC_event_midsummer()
{
    RegisterSpellScript(spell_midsummer_braziers_hit);
    RegisterSpellScript(spell_midsummer_torch_target_picker);
    RegisterSpellScript(spell_midsummer_torch_toss_land);
    RegisterSpellScript(spell_midsummer_test_ribbon_pole_channel);
    RegisterSpellScript(spell_midsummer_ribbon_pole_periodic_visual);
    RegisterSpellScript(spell_midsummer_juggle_torch);
    RegisterSpellScript(spell_midsummer_torch_catch);
    RegisterSpellScript(spell_midsummer_fling_torch);
    RegisterSpellScript(spell_midsummer_fling_torch_triggered);
    RegisterSpellScript(spell_midsummer_fling_torch_catch);
    RegisterSpellScript(spell_midsummer_fling_torch_missed);
}
