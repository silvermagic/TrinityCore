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
 * @file brewfest.cpp
 * @brief 美酒节事件脚本模块
 *
 * 本模块实现了魔兽世界美酒节（Brewfest）节日活动的核心功能，包括：
 * - 美酒节赛车羊驼系统：控制羊驼的速度、疲劳度和状态
 * - 叫卖任务系统：在指定位置宣传啤酒品牌
 * - 坐骑变形系统：将普通坐骑转换为美酒节主题坐骑
 * - 月度啤酒系统：各种特殊啤酒的效果实现
 *
 * 美酒节每年9月20日至10月6日举办，主要活动地点包括：
 * - 联盟：铁炉堡外的丹莫罗
 * - 部落：奥格瑞玛外的杜隆塔尔
 */

#include "ScriptMgr.h"
#include "CreatureAIImpl.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "World.h"

/**
 * @brief 羊驼赛车系统相关法术ID枚举
 *
 * 定义了美酒节羊驼赛车活动中使用的各种法术ID，
 * 包括速度控制、疲劳度管理和任务相关法术
 */
enum RamBlaBla
{
    SPELL_GIDDYUP                           = 42924,  ///< 驾驶！- 用于控制羊驼加速的核心法术
    SPELL_RENTAL_RACING_RAM                 = 43883,  ///< 租用赛跑羊驼 - 赛车任务中提供的羊驼坐骑
    SPELL_SWIFT_WORK_RAM                    = 43880,  ///< 敏捷工作羊驼 - 接力赛任务中使用的羊驼
    SPELL_RENTAL_RACING_RAM_AURA            = 42146,  ///< 租用赛跑羊驼光环 - 羊驼的基础光环效果
    SPELL_RAM_LEVEL_NEUTRAL                 = 43310,  ///< 羊驼等级-中立 - 羊驼停止状态
    SPELL_RAM_TROT                          = 42992,  ///< 羊驼-慢跑 - 低速状态（绿色区域）
    SPELL_RAM_CANTER                        = 42993,  ///< 羊驼-快跑 - 中速状态（黄色区域）
    SPELL_RAM_GALLOP                        = 42994,  ///< 羊驼-飞奔 - 高速状态（红色区域）
    SPELL_RAM_FATIGUE                       = 43052,  ///< 羊驼疲劳 - 累积的疲劳层数
    SPELL_EXHAUSTED_RAM                     = 43332,  ///< 精疲力竭的羊驼 - 疲劳过度后的减速状态
    SPELL_RELAY_RACE_TURN_IN                = 44501,  ///< 接力赛交还 - 延长坐骑时间的法术

    // Quest - 任务相关法术
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_GREEN  = 43345,  ///< 美酒节任务速度兔子-绿色 - 慢跑速度触发
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_YELLOW = 43346,  ///< 美酒节任务速度兔子-黄色 - 快跑速度触发
    SPELL_BREWFEST_QUEST_SPEED_BUNNY_RED    = 43347   ///< 美酒节任务速度兔子-红色 - 飞奔速度触发
};

/**
 * @brief 驾驶！法术脚本 (Spell ID: 42924)
 *
 * 此光环脚本控制羊驼的加速机制。玩家通过按特定按键（通常是1键）来叠加层数，
 * 每按一次增加一层。根据叠加层数，羊驼会进入不同的速度状态：
 * - 1-5层：慢跑（绿色区域）- 速度较慢但会恢复疲劳
 * - 6-10层：快跑（黄色区域）- 中等速度，疲劳缓慢增加
 * - 11+层：飞奔（红色区域）- 最快速度，疲劳快速增加
 *
 * 光环会定期自动减少层数，模拟羊驼减速的效果。
 *
 * @note 性能考虑：此光环频繁触发，应保持逻辑简洁高效
 */
// 42924 - Giddyup!
class spell_brewfest_giddyup : public AuraScript
{
    PrepareAuraScript(spell_brewfest_giddyup);

    /**
     * @brief 处理光环层数变化
     *
     * 当光环层数改变时调用，根据当前层数切换羊驼的速度状态。
     *
     * @param aurEff 触发光环效果
     * @param mode 光环效果处理模式
     *
     * 调用时机：
     * - 光环首次应用时
     * - 光环层数增加或减少时
     * - 光环被移除时
     */
    void OnChange(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();

        // 检查玩家是否仍在使用羊驼坐骑
        if (!target->HasAura(SPELL_RENTAL_RACING_RAM) && !target->HasAura(SPELL_SWIFT_WORK_RAM))
        {
            target->RemoveAura(GetId());
            return;
        }

        // 如果羊驼处于精疲力竭状态，不允许加速
        if (target->HasAura(SPELL_EXHAUSTED_RAM))
            return;

        // 根据层数设置对应的羊驼速度状态
        switch (GetStackAmount())
        {
            case 1: // green - 慢跑速度（绿色区域）
                target->RemoveAura(SPELL_RAM_LEVEL_NEUTRAL);
                target->RemoveAura(SPELL_RAM_CANTER);
                target->CastSpell(target, SPELL_RAM_TROT, true);
                break;
            case 6: // yellow - 快跑速度（黄色区域）
                target->RemoveAura(SPELL_RAM_TROT);
                target->RemoveAura(SPELL_RAM_GALLOP);
                target->CastSpell(target, SPELL_RAM_CANTER, true);
                break;
            case 11: // red - 飞奔速度（红色区域）
                target->RemoveAura(SPELL_RAM_CANTER);
                target->CastSpell(target, SPELL_RAM_GALLOP, true);
                break;
            default:
                break;
        }

        // 当光环正常移除时（不是被驱散），切换到中立状态
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_DEFAULT)
        {
            target->RemoveAura(SPELL_RAM_TROT);
            target->CastSpell(target, SPELL_RAM_LEVEL_NEUTRAL, true);
        }
    }

    /**
     * @brief 处理周期性效果
     *
     * 每个周期自动移除一层，模拟羊驼逐渐减速的效果。
     * 这创造了玩家需要持续按加速键来维持速度的游戏机制。
     *
     * @param aurEff 触发的周期性光环效果
     */
    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->RemoveAuraFromStack(GetId());
    }

    /**
     * @brief 注册光环效果回调函数
     *
     * 注册三个回调：
     * 1. 光环应用时的变化处理
     * 2. 光环移除时的变化处理
     * 3. 周期性效果触发处理
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_brewfest_giddyup::OnChange, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_CHANGE_AMOUNT_MASK);
        OnEffectRemove += AuraEffectRemoveFn(spell_brewfest_giddyup::OnChange, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_CHANGE_AMOUNT_MASK);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_brewfest_giddyup::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 羊驼速度状态法术脚本
 *
 * 处理羊驼的四种速度状态：
 * - 中立状态 (SPELL_RAM_LEVEL_NEUTRAL, 43310)
 * - 慢跑状态 (SPELL_RAM_TROT, 42992) - 绿色区域
 * - 快跑状态 (SPELL_RAM_CANTER, 42993) - 黄色区域
 * - 飞奔状态 (SPELL_RAM_GALLOP, 42994) - 红色区域
 *
 * 每种状态有不同的疲劳度变化规则：
 * - 中立：恢复疲劳（减少4层）
 * - 慢跑：恢复疲劳（减少2层）
 * - 快跑：增加疲劳（增加1层/周期）
 * - 飞奔：大幅增加疲劳（增加4-5层/周期）
 *
 * 同时追踪在不同速度下保持的时间，用于任务目标检测
 */
// 43310 - Ram Level - Neutral
// 42992 - Ram - Trot
// 42993 - Ram - Canter
// 42994 - Ram - Gallop
class spell_brewfest_ram : public AuraScript
{
    PrepareAuraScript(spell_brewfest_ram);

    /**
     * @brief 处理周期性效果
     *
     * 根据当前羊驼速度状态，周期性地更新疲劳度和任务进度。
     * 这是羊驼赛车玩法的核心逻辑。
     *
     * @param aurEff 触发的周期性光环效果
     *
     * 调用时机：每个周期触发（通常每秒）
     */
    void OnPeriodic(AuraEffect const* aurEff)
    {
        Unit* target = GetTarget();

        // 精疲力竭状态下不处理疲劳度变化
        if (target->HasAura(SPELL_EXHAUSTED_RAM))
            return;

        switch (GetId())
        {
            case SPELL_RAM_LEVEL_NEUTRAL:
                // 中立状态：大幅恢复疲劳度
                if (Aura* aura = target->GetAura(SPELL_RAM_FATIGUE))
                    aura->ModStackAmount(-4);
                break;
            case SPELL_RAM_TROT: // green - 慢跑状态
                // 慢跑状态：适度恢复疲劳度
                if (Aura* aura = target->GetAura(SPELL_RAM_FATIGUE))
                    aura->ModStackAmount(-2);
                // 在第4个周期触发绿色速度任务目标（保持慢跑4秒）
                if (aurEff->GetTickNumber() == 4)
                    target->CastSpell(target, SPELL_BREWFEST_QUEST_SPEED_BUNNY_GREEN, true);
                break;
            case SPELL_RAM_CANTER:
            {
                // 快跑状态：增加疲劳度（每次+1层）
                CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
                args.AddSpellMod(SPELLVALUE_AURA_STACK, 1);
                target->CastSpell(target, SPELL_RAM_FATIGUE, args);
                // 在第8个周期触发黄色速度任务目标（保持快跑8秒）
                if (aurEff->GetTickNumber() == 8)
                    target->CastSpell(target, SPELL_BREWFEST_QUEST_SPEED_BUNNY_YELLOW, true);
                break;
            }
            case SPELL_RAM_GALLOP:
            {
                // 飞奔状态：大幅增加疲劳度
                // 如果已有疲劳光环则+4层，否则+5层（游戏机制的Hack）
                CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
                args.AddSpellMod(SPELLVALUE_AURA_STACK, target->HasAura(SPELL_RAM_FATIGUE) ? 4 : 5 /*Hack*/);
                target->CastSpell(target, SPELL_RAM_FATIGUE, args);
                // 在第8个周期触发红色速度任务目标（保持飞奔8秒）
                if (aurEff->GetTickNumber() == 8)
                    target->CastSpell(target, SPELL_BREWFEST_QUEST_SPEED_BUNNY_RED, true);
                break;
            }
            default:
                break;
        }

    }

    /**
     * @brief 注册周期性效果回调
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_brewfest_ram::OnPeriodic, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 羊驼疲劳法术脚本 (Spell ID: 43052)
 *
 * 追踪羊驼的疲劳程度。当疲劳层数达到101层时，
 * 羊驼会进入精疲力竭状态，速度大幅降低。
 *
 * 疲劳度管理是美酒节赛车活动的核心策略元素：
 * - 玩家需要平衡速度和疲劳度
 * - 过度使用高速会导致精疲力竭，反而更慢
 * - 需要适时降低速度来恢复疲劳度
 */
// 43052 - Ram Fatigue
class spell_brewfest_ram_fatigue : public AuraScript
{
    PrepareAuraScript(spell_brewfest_ram_fatigue);

    /**
     * @brief 处理疲劳度应用
     *
     * 当疲劳层数累积到101层时，触发精疲力竭状态。
     * 这会清除所有速度状态并强制羊驼进入极慢的精疲力竭状态。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();

        // 疲劳达到临界值（101层），触发精疲力竭状态
        if (GetStackAmount() == 101)
        {
            // 清除所有速度相关状态
            target->RemoveAura(SPELL_RAM_LEVEL_NEUTRAL);
            target->RemoveAura(SPELL_RAM_TROT);
            target->RemoveAura(SPELL_RAM_CANTER);
            target->RemoveAura(SPELL_RAM_GALLOP);
            target->RemoveAura(SPELL_GIDDYUP);

            // 应用精疲力竭状态
            target->CastSpell(target, SPELL_EXHAUSTED_RAM, true);
        }
    }

    /**
     * @brief 注册光环应用回调
     *
     * 使用 REAL_OR_REAPPLY_MASK 确保每次层数变化都会触发检查
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_brewfest_ram_fatigue::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

/**
 * @brief 美酒节苹果陷阱法术脚本 (Spell ID: 43450)
 *
 * 当玩家驾驶羊驼经过赛道上的苹果陷阱时触发。
 * 苹果陷阱会完全清除羊驼的疲劳度，是赛道上的重要恢复点。
 *
 * 游戏设计：玩家需要合理规划路线，经过苹果陷阱来恢复疲劳度，
 * 从而能够以更快的速度完成比赛。
 */
// 43450 - Brewfest - apple trap - friendly DND
class spell_brewfest_apple_trap : public AuraScript
{
    PrepareAuraScript(spell_brewfest_apple_trap);

    /**
     * @brief 处理苹果陷阱应用效果
     *
     * 当玩家进入苹果陷阱区域时，清除所有疲劳层数。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAura(SPELL_RAM_FATIGUE);
    }

    /**
     * @brief 注册光环应用回调
     */
    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_brewfest_apple_trap::OnApply, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 精疲力竭的羊驼法术脚本 (Spell ID: 43332)
 *
 * 当羊驼疲劳度达到临界值后触发此状态。
 * 精疲力竭状态下，羊驼速度大幅降低，无法加速。
 * 持续一段时间后会自动恢复到中立状态。
 */
// 43332 - Exhausted Ram
class spell_brewfest_exhausted_ram : public AuraScript
{
    PrepareAuraScript(spell_brewfest_exhausted_ram);

    /**
     * @brief 处理精疲力竭状态移除
     *
     * 当精疲力竭状态结束时，将羊驼恢复到中立状态，
     * 允许玩家重新开始加速。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_RAM_LEVEL_NEUTRAL, true);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        OnEffectRemove += AuraEffectApplyFn(spell_brewfest_exhausted_ram::OnRemove, EFFECT_0, SPELL_AURA_MOD_DECREASE_SPEED, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 接力赛强制投掷法术脚本 (Spell ID: 43714)
 *
 * 在美酒节接力赛任务中，强制玩家投掷酒桶。
 * 这是一个辅助法术，用于任务流程中的强制动作。
 *
 * 设计说明：此法术触发的法术需要消耗材料，
 * 因此使用 TRIGGERED_FULL_MASK 但排除了忽略材料的标志，
 * 确保材料被正确消耗。
 */
// 43714 - Brewfest - Relay Race - Intro - Force - Player to throw- DND
class spell_brewfest_relay_race_intro_force_player_to_throw : public SpellScript
{
    PrepareSpellScript(spell_brewfest_relay_race_intro_force_player_to_throw);

    /**
     * @brief 处理强制施法效果
     *
     * 强制目标施放触发的法术，同时保留材料消耗。
     *
     * @param effIndex 法术效果索引
     */
    void HandleForceCast(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        // 这些法术触发的法术需要材料；如果触发的法术以"triggered"方式施放，
        // 材料不会被消耗，因此我们需要保留材料消耗标志
        GetHitUnit()->CastSpell(nullptr, GetEffectInfo().TriggerSpell, TriggerCastFlags(TRIGGERED_FULL_MASK & ~TRIGGERED_IGNORE_POWER_AND_REAGENT_COST));
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_brewfest_relay_race_intro_force_player_to_throw::HandleForceCast, EFFECT_0, SPELL_EFFECT_FORCE_CAST);
    }
};

/**
 * @brief 接力赛交还法术脚本 (Spell ID: 43755)
 *
 * 处理美酒节接力赛中玩家将酒桶交给NPC的逻辑。
 * 成功交还后，延长羊驼坐骑的持续时间，让玩家可以继续运送。
 */
// 43755 - Brewfest - Daily - Relay Race - Player - Increase Mount Duration - DND
class spell_brewfest_relay_race_turn_in : public SpellScript
{
    PrepareSpellScript(spell_brewfest_relay_race_turn_in);

    /**
     * @brief 处理交还效果
     *
     * 当玩家成功交还酒桶时：
     * 1. 延长羊驼坐骑持续时间30秒
     * 2. 触发交还任务法术（用于任务进度）
     *
     * @param effIndex 法术效果索引
     */
    void HandleDummy(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        if (Aura* aura = GetHitUnit()->GetAura(SPELL_SWIFT_WORK_RAM))
        {
            // 延长坐骑持续时间30秒
            aura->SetDuration(aura->GetDuration() + 30 * IN_MILLISECONDS);
            // 触发交还法术，更新任务进度
            GetCaster()->CastSpell(GetHitUnit(), SPELL_RELAY_RACE_TURN_IN, TRIGGERED_FULL_MASK);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_brewfest_relay_race_turn_in::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 下马法术脚本 (Spell ID: 43876)
 *
 * 简单的法术脚本，用于移除玩家的羊驼坐骑。
 * 在玩家完成或放弃赛车任务时调用。
 */
// 43876 - Dismount Ram
class spell_brewfest_dismount_ram : public SpellScript
{
    PrepareSpellScript(spell_brewfest_dismount_ram);

    /**
     * @brief 处理下马效果
     *
     * 移除租用的赛跑羊驼坐骑。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAura(SPELL_RENTAL_RACING_RAM);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_brewfest_dismount_ram::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 叫卖任务相关数据枚举
 *
 * 定义了美酒节叫卖任务的ID和对应的喊话文本。
 * 玩家需要骑乘羊驼在指定地点喊话宣传不同的啤酒品牌。
 */
enum RamBlub
{
    // Horde - 部落任务
    QUEST_BARK_FOR_DROHNS_DISTILLERY        = 11407,  ///< 为德罗恩酿酒厂叫卖！
    QUEST_BARK_FOR_TCHALIS_VOODOO_BREWERY   = 11408,  ///< 为特查利巫毒酿酒厂叫卖！

    // Alliance - 联盟任务
    QUEST_BARK_BARLEYBREW                   = 11293,  ///< 为大麦 brew叫卖！
    QUEST_BARK_FOR_THUNDERBREWS             = 11294,  ///< 为雷酿叫卖！

    // Bark for Drohn's Distillery! - 德罗恩酿酒厂喊话
    SAY_DROHN_DISTILLERY_1                  = 23520,  ///< 喊话1
    SAY_DROHN_DISTILLERY_2                  = 23521,  ///< 喊话2
    SAY_DROHN_DISTILLERY_3                  = 23522,  ///< 喊话3
    SAY_DROHN_DISTILLERY_4                  = 23523,  ///< 喊话4

    // Bark for T'chali's Voodoo Brewery! - 特查利巫毒酿酒厂喊话
    SAY_TCHALIS_VOODOO_1                    = 23524,  ///< 喊话1
    SAY_TCHALIS_VOODOO_2                    = 23525,  ///< 喊话2
    SAY_TCHALIS_VOODOO_3                    = 23526,  ///< 喊话3
    SAY_TCHALIS_VOODOO_4                    = 23527,  ///< 喊话4

    // Bark for the Barleybrews! - 大麦 brew喊话
    SAY_BARLEYBREW_1                        = 23464,  ///< 喊话1
    SAY_BARLEYBREW_2                        = 23465,  ///< 喊话2
    SAY_BARLEYBREW_3                        = 23466,  ///< 喊话3
    SAY_BARLEYBREW_4                        = 22941,  ///< 喊话4

    // Bark for the Thunderbrews! - 雷酿喊话
    SAY_THUNDERBREWS_1                      = 23467,  ///< 喊话1
    SAY_THUNDERBREWS_2                      = 23468,  ///< 喊话2
    SAY_THUNDERBREWS_3                      = 23469,  ///< 喊话3
    SAY_THUNDERBREWS_4                      = 22942   ///< 喊话4
};

/**
 * @brief 叫卖兔子光环脚本
 *
 * 用于美酒节叫卖任务。当玩家到达指定位置时，触发对应的喊话。
 * 法术ID：
 * - 43259: 美酒节叫卖兔子1
 * - 43260: 美酒节叫卖兔子2
 * - 43261: 美酒节叫卖兔子3
 * - 43262: 美酒节叫卖兔子4
 *
 * 玩家骑乘羊驼经过这些位置时，会自动喊出对应啤酒品牌的宣传语。
 */
// 43259 Brewfest  - Barker Bunny 1
// 43260 Brewfest  - Barker Bunny 2
// 43261 Brewfest  - Barker Bunny 3
// 43262 Brewfest  - Barker Bunny 4
class spell_brewfest_barker_bunny : public AuraScript
{
    PrepareAuraScript(spell_brewfest_barker_bunny);

    /**
     * @brief 加载检查
     *
     * 确保此光环只应用于玩家单位。
     *
     * @return 如果目标是玩家返回true，否则返回false
     */
    bool Load() override
    {
        return GetUnitOwner()->GetTypeId() == TYPEID_PLAYER;
    }

    /**
     * @brief 处理光环应用效果
     *
     * 当玩家进入叫卖区域时：
     * 1. 检查玩家当前进行的叫卖任务
     * 2. 根据任务类型选择对应的喊话文本
     * 3. 让玩家喊出随机的宣传语
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Player* target = GetTarget()->ToPlayer();

        uint32 BroadcastTextId = 0;

        // 检查部落任务 - 德罗恩酿酒厂
        if (target->GetQuestStatus(QUEST_BARK_FOR_DROHNS_DISTILLERY) == QUEST_STATUS_INCOMPLETE ||
            target->GetQuestStatus(QUEST_BARK_FOR_DROHNS_DISTILLERY) == QUEST_STATUS_COMPLETE)
            BroadcastTextId = RAND(SAY_DROHN_DISTILLERY_1, SAY_DROHN_DISTILLERY_2, SAY_DROHN_DISTILLERY_3, SAY_DROHN_DISTILLERY_4);

        // 检查部落任务 - 特查利巫毒酿酒厂
        if (target->GetQuestStatus(QUEST_BARK_FOR_TCHALIS_VOODOO_BREWERY) == QUEST_STATUS_INCOMPLETE ||
            target->GetQuestStatus(QUEST_BARK_FOR_TCHALIS_VOODOO_BREWERY) == QUEST_STATUS_COMPLETE)
            BroadcastTextId = RAND(SAY_TCHALIS_VOODOO_1, SAY_TCHALIS_VOODOO_2, SAY_TCHALIS_VOODOO_3, SAY_TCHALIS_VOODOO_4);

        // 检查联盟任务 - 大麦 brew
        if (target->GetQuestStatus(QUEST_BARK_BARLEYBREW) == QUEST_STATUS_INCOMPLETE ||
            target->GetQuestStatus(QUEST_BARK_BARLEYBREW) == QUEST_STATUS_COMPLETE)
            BroadcastTextId = RAND(SAY_BARLEYBREW_1, SAY_BARLEYBREW_2, SAY_BARLEYBREW_3, SAY_BARLEYBREW_4);

        // 检查联盟任务 - 雷酿
        if (target->GetQuestStatus(QUEST_BARK_FOR_THUNDERBREWS) == QUEST_STATUS_INCOMPLETE ||
            target->GetQuestStatus(QUEST_BARK_FOR_THUNDERBREWS) == QUEST_STATUS_COMPLETE)
            BroadcastTextId = RAND(SAY_THUNDERBREWS_1, SAY_THUNDERBREWS_2, SAY_THUNDERBREWS_3, SAY_THUNDERBREWS_4);

        // 如果有对应的喊话文本，执行喊话
        if (BroadcastTextId)
            target->Talk(BroadcastTextId, CHAT_MSG_SAY, sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY), target);
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_brewfest_barker_bunny::OnApply, EFFECT_1, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 美酒节坐骑变形相关法术ID枚举
 *
 * 定义了美酒节期间将普通坐骑转换为节日主题坐骑的法术。
 * 联盟玩家获得羊驼坐骑，部落玩家获得科多兽坐骑。
 */
enum BrewfestMountTransformation
{
    SPELL_MOUNT_RAM_100                         = 43900,  ///< 羊驼坐骑（100%速度）
    SPELL_MOUNT_RAM_60                          = 43899,  ///< 羊驼坐骑（60%速度）
    SPELL_MOUNT_KODO_100                        = 49379,  ///< 科多兽坐骑（100%速度）
    SPELL_MOUNT_KODO_60                         = 49378,  ///< 科多兽坐骑（60%速度）
    SPELL_BREWFEST_MOUNT_TRANSFORM              = 49357,  ///< 美酒节坐骑变形（常规）
    SPELL_BREWFEST_MOUNT_TRANSFORM_REVERSE      = 52845,  ///< 美酒节坐骑变形（阵营交换）
};

/**
 * @brief 美酒节坐骑变形法术脚本
 *
 * 将玩家当前的坐骑转换为美酒节主题坐骑：
 * - 法术 49357: 常规变形 - 联盟获得羊驼，部落获得科多兽
 * - 法术 52845: 阵营交换变形 - 部落获得羊驼，联盟获得科多兽
 *
 * 变形后的坐骑速度与原坐骑速度相匹配：
 * - 100%速度地面坐骑 -> 美酒节100%坐骑
 * - 60%速度地面坐骑 -> 美酒节60%坐骑
 */
// 49357 - Brewfest Mount Transformation
// 52845 - Brewfest Mount Transformation (Faction Swap)
class spell_brewfest_mount_transformation : public SpellScript
{
    PrepareSpellScript(spell_brewfest_mount_transformation);

    /**
     * @brief 验证法术依赖
     *
     * @param spell 法术信息
     * @return 所有依赖法术都有效时返回true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MOUNT_RAM_100,
            SPELL_MOUNT_RAM_60,
            SPELL_MOUNT_KODO_100,
            SPELL_MOUNT_KODO_60
        });
    }

    /**
     * @brief 处理坐骑变形效果
     *
     * 根据玩家的阵营和当前坐骑速度，施放对应的变形坐骑法术。
     *
     * @param effIndex 法术效果索引
     */
    void HandleDummy(SpellEffIndex /* effIndex */)
    {
        Player* caster = GetCaster()->ToPlayer();

        // 检查玩家是否已骑乘坐骑
        if (caster->HasAuraType(SPELL_AURA_MOUNTED))
        {
            // 移除当前坐骑
            caster->RemoveAurasByType(SPELL_AURA_MOUNTED);
            uint32 spell_id;

            switch (GetSpellInfo()->Id)
            {
                case SPELL_BREWFEST_MOUNT_TRANSFORM:
                    // 常规变形：联盟得羊驼，部落得科多兽
                    if (caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                        spell_id = caster->GetTeam() == ALLIANCE ? SPELL_MOUNT_RAM_100 : SPELL_MOUNT_KODO_100;
                    else
                        spell_id = caster->GetTeam() == ALLIANCE ? SPELL_MOUNT_RAM_60 : SPELL_MOUNT_KODO_60;
                    break;
                case SPELL_BREWFEST_MOUNT_TRANSFORM_REVERSE:
                    // 交换变形：部落得羊驼，联盟得科多兽
                    if (caster->GetSpeedRate(MOVE_RUN) >= 2.0f)
                        spell_id = caster->GetTeam() == HORDE ? SPELL_MOUNT_RAM_100 : SPELL_MOUNT_KODO_100;
                    else
                        spell_id = caster->GetTeam() == HORDE ? SPELL_MOUNT_RAM_60 : SPELL_MOUNT_KODO_60;
                    break;
                default:
                    return;
            }
            caster->CastSpell(caster, spell_id, true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_brewfest_mount_transformation::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 月度啤酒系统说明
 *
 * 月度啤酒（Brew of the Month）是美酒节的一个成就奖励系统。
 * 完成成就的玩家每月可以购买一款特殊啤酒，每款啤酒都有独特的效果：
 *
 * 一月   [Wild Winter Pilsner] - 野生冬日皮尔森
 *    spell_brewfest_botm_the_beast_within - 释放野兽效果
 * 二月   [Izzard's Ever Flavor] - 艾扎德的永恒风味
 *    spell_brewfest_botm_gassy - 打嗝效果
 * 三月   [Aromatic Honey Brew] - 芳香蜂蜜啤酒
 *    无需脚本实现
 * 四月   [Metok's Bubble Bock] - 梅托克气泡博克
 *    spell_brewfest_botm_bloated - 膨胀效果
 *    不完整（需要法术 49828, 49827, 49830, 49837）
 * 五月   [Springtime Stout] - 春日黑啤
 *    无需脚本实现
 * 六月   [Blackrock Lager] - 黑石啤酒
 *    spell_brewfest_botm_internal_combustion - 内燃效果（喷火打嗝）
 * 七月   [Stranglethorn Brew] - 荆棘谷酿酒
 *    spell_brewfest_botm_jungle_madness - 丛林疯狂效果
 * 八月   [Draenic Pale Ale] - 德莱尼淡啤酒
 *    尚未实现（NYI）
 * 九月   [Binary Brew] - 二进制啤酒
 *    spell_brewfest_botm_teach_language - 教授语言（侏儒/地精二进制语言）
 * 十月   [Autumnal Acorn Ale] - 秋季橡子啤酒
 *    尚未实现（NYI）
 * 十一月 [Bartlett's Bitter Brew] - 巴特利特苦啤酒
 *    尚未实现（NYI）
 * 十二月 [Lord of Frost's Private Label] - 霜之领主私酿
 *    无需脚本实现
 */

/**
 * @brief 野生冬日皮尔森相关法术
 */
enum WildWinterPilsner
{
    SPELL_BOTM_UNLEASH_THE_BEAST    = 50099  ///< 释放野兽 - 变身效果法术
};

/**
 * @brief 内心的野兽光环脚本 (Spell ID: 50098)
 *
 * 野生冬日皮尔森啤酒的持续效果。
 * 当效果结束时，触发"释放野兽"变形法术。
 */
// 50098 - The Beast Within
class spell_brewfest_botm_the_beast_within : public AuraScript
{
    PrepareAuraScript(spell_brewfest_botm_the_beast_within);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_UNLEASH_THE_BEAST });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当"内心的野兽"光环结束时，施放"释放野兽"变形法术。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_UNLEASH_THE_BEAST);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_brewfest_botm_the_beast_within::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 艾扎德的永恒风味相关法术
 */
enum IzzardsEverFlavor
{
    SPELL_BOTM_BELCH_BREW_BELCH_VISUAL    = 49860  ///< 打嗝视觉效果
};

/**
 * @brief 胀气光环脚本 (Spell ID: 49864)
 *
 * 艾扎德的永恒风味啤酒的持续效果。
 * 当效果结束时，玩家会打出一个响亮的啤酒嗝。
 */
// 49864 - Gassy
class spell_brewfest_botm_gassy : public AuraScript
{
    PrepareAuraScript(spell_brewfest_botm_gassy);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BELCH_BREW_BELCH_VISUAL });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当"胀气"光环结束时，触发打嗝视觉效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BELCH_BREW_BELCH_VISUAL, true);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_brewfest_botm_gassy::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 梅托克气泡博克相关法术
 */
enum MetoksBubbleBock
{
    SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE    = 50072  ///< 气泡啤酒触发投射物
};

/**
 * @brief 膨胀光环脚本 (Spell ID: 49822)
 *
 * 梅托克气泡博克啤酒的持续效果。
 * 当效果结束时，触发气泡投射物效果。
 */
// 49822 - Bloated
class spell_brewfest_botm_bloated : public AuraScript
{
    PrepareAuraScript(spell_brewfest_botm_bloated);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当"膨胀"光环结束时，触发气泡投射物效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BUBBLE_BREW_TRIGGER_MISSILE, true);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_brewfest_botm_bloated::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 黑石啤酒相关法术
 */
enum BlackrockLager
{
    SPELL_BOTM_BELCH_FIRE_VISUAL    = 49737  ///< 喷火打嗝视觉效果
};

/**
 * @brief 内燃光环脚本 (Spell ID: 49738)
 *
 * 黑石啤酒的持续效果。
 * 当效果结束时，玩家会打出一个火焰嗝。
 */
// 49738 - Internal Combustion
class spell_brewfest_botm_internal_combustion : public AuraScript
{
    PrepareAuraScript(spell_brewfest_botm_internal_combustion);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_BELCH_FIRE_VISUAL });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当"内燃"光环结束时，触发喷火打嗝视觉效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_BOTM_BELCH_FIRE_VISUAL, true);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_brewfest_botm_internal_combustion::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 荆棘谷酿酒相关法术
 */
enum StranglethornBrew
{
    SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT    = 50010  ///< 丛林酿酒视觉效果
};

/**
 * @brief 丛林疯狂法术脚本 (Spell ID: 49962)
 *
 * 荆棘谷酿酒使用后触发的效果。
 * 施放丛林视觉特效，模拟丛林中的幻觉效果。
 */
// 49962 - Jungle Madness!
class spell_brewfest_botm_jungle_madness : public SpellScript
{
    PrepareSpellScript(spell_brewfest_botm_jungle_madness);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT });
    }

    /**
     * @brief 处理施法后效果
     *
     * 施法完成后，触发丛林视觉特效。
     */
    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_BOTM_JUNGLE_BREW_VISION_EFFECT, true);
    }

    /**
     * @brief 注册施法回调
     */
    void Register() override
    {
        AfterCast += SpellCastFn(spell_brewfest_botm_jungle_madness::HandleAfterCast);
    }
};

/**
 * @brief 二进制啤酒相关法术
 */
enum BinaryBrew
{
    SPELL_LEARN_GNOMISH_BINARY      = 50242,  ///< 学习侏儒二进制语言
    SPELL_LEARN_GOBLIN_BINARY       = 50246   ///< 学习地精二进制语言
};

/**
 * @brief 教授语言法术脚本 (Spell ID: 50243)
 *
 * 二进制啤酒的特殊效果。
 * 根据玩家阵营教授对应的二进制语言：
 * - 联盟玩家学习侏儒二进制语言
 * - 部落玩家学习地精二进制语言
 *
 * 这是一种趣味性语言，玩家可以用这种语言交流，
 * 但只有同样喝过二进制啤酒的玩家才能理解。
 */
// 50243 - Teach Language
class spell_brewfest_botm_teach_language : public SpellScript
{
    PrepareSpellScript(spell_brewfest_botm_teach_language);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_LEARN_GNOMISH_BINARY, SPELL_LEARN_GOBLIN_BINARY });
    }

    /**
     * @brief 处理教授语言效果
     *
     * 根据玩家阵营教授对应的二进制语言。
     *
     * @param effIndex 法术效果索引
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            caster->CastSpell(caster, caster->GetTeam() == ALLIANCE ? SPELL_LEARN_GNOMISH_BINARY : SPELL_LEARN_GOBLIN_BINARY, true);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_brewfest_botm_teach_language::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 创建空啤酒瓶相关法术
 */
enum CreateEmptyBrewBottle
{
    SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE    = 51655  ///< 创建空啤酒瓶
};

/**
 * @brief 弱酒精法术脚本
 *
 * 处理多种弱酒精饮料（如各种美酒节日常饮用的啤酒）。
 * 饮用后会在玩家背包中创建一个空酒瓶。
 *
 * 法术ID包括：42254, 42255, 42256, 42257, 42258, 42259, 42260, 42261, 42263, 42264, 43959, 43961
 */
// 42254, 42255, 42256, 42257, 42258, 42259, 42260, 42261, 42263, 42264, 43959, 43961 - Weak Alcohol
class spell_brewfest_botm_weak_alcohol : public SpellScript
{
    PrepareSpellScript(spell_brewfest_botm_weak_alcohol);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE });
    }

    /**
     * @brief 处理施法后效果
     *
     * 施法完成后，在玩家背包中创建一个空啤酒瓶。
     */
    void HandleAfterCast()
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_BOTM_CREATE_EMPTY_BREW_BOTTLE, true);
    }

    /**
     * @brief 注册施法回调
     */
    void Register() override
    {
        AfterCast += SpellCastFn(spell_brewfest_botm_weak_alcohol::HandleAfterCast);
    }
};

/**
 * @brief 空酒瓶投掷相关法术
 */
enum EmptyBottleThrow
{
    SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE    = 51695,  ///< 空酒瓶投掷命中生物
    SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND      = 51697   ///< 空酒瓶投掷命中地面
};

/**
 * @brief 空酒瓶投掷结算法术脚本 (Spell ID: 51694)
 *
 * 处理玩家投掷空酒瓶的效果。
 * 根据命中目标类型（生物或地面）触发不同的效果法术。
 */
// 51694 - BOTM - Empty Bottle Throw - Resolve
class spell_brewfest_botm_empty_bottle_throw_resolve : public SpellScript
{
    PrepareSpellScript(spell_brewfest_botm_empty_bottle_throw_resolve);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE,
            SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND
        });
    }

    /**
     * @brief 处理投掷结算效果
     *
     * 根据是否命中单位，选择不同的效果：
     * - 命中单位：触发命中生物效果
     * - 命中地面：触发命中地面效果
     *
     * @param effIndex 法术效果索引
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();

        if (Unit* target = GetHitUnit())
            caster->CastSpell(target, SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_CREATURE, true);
        else
            caster->CastSpell(GetHitDest()->GetPosition(), SPELL_BOTM_EMPTY_BOTTLE_THROW_IMPACT_GROUND, true);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_brewfest_botm_empty_bottle_throw_resolve::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册所有美酒节脚本
 *
 * 此函数在服务器启动时被调用，用于注册本文件中定义的所有法术脚本。
 * 将脚本与对应的法术ID关联起来，使游戏能够正确处理美酒节相关的法术效果。
 */
void AddSC_event_brewfest()
{
    // 羊驼赛车系统
    RegisterSpellScript(spell_brewfest_giddyup);
    RegisterSpellScript(spell_brewfest_ram);
    RegisterSpellScript(spell_brewfest_ram_fatigue);
    RegisterSpellScript(spell_brewfest_apple_trap);
    RegisterSpellScript(spell_brewfest_exhausted_ram);

    // 接力赛任务
    RegisterSpellScript(spell_brewfest_relay_race_intro_force_player_to_throw);
    RegisterSpellScript(spell_brewfest_relay_race_turn_in);
    RegisterSpellScript(spell_brewfest_dismount_ram);

    // 叫卖任务
    RegisterSpellScript(spell_brewfest_barker_bunny);

    // 坐骑变形
    RegisterSpellScript(spell_brewfest_mount_transformation);

    // 月度啤酒系统
    RegisterSpellScript(spell_brewfest_botm_the_beast_within);
    RegisterSpellScript(spell_brewfest_botm_gassy);
    RegisterSpellScript(spell_brewfest_botm_bloated);
    RegisterSpellScript(spell_brewfest_botm_internal_combustion);
    RegisterSpellScript(spell_brewfest_botm_jungle_madness);
    RegisterSpellScript(spell_brewfest_botm_teach_language);
    RegisterSpellScript(spell_brewfest_botm_weak_alcohol);
    RegisterSpellScript(spell_brewfest_botm_empty_bottle_throw_resolve);
}
