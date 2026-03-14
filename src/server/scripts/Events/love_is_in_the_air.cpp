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
 * @file love_is_in_the_air.cpp
 * @brief 爱情冲昏头脑事件脚本模块
 *
 * 本模块实现了魔兽世界情人节（Love is in the Air）节日活动的核心功能，包括：
 * - 浪漫野餐系统：与伴侣共享野餐，获得成就和视觉效果
 * - 爱心糖果系统：制作各种爱心糖果送给其他玩家
 * - 香水分析任务：分析NPC身上的香水，揭露阴谋
 * - 恋爱药剂系统：各种情人节相关法术和效果
 *
 * 情人节每年2月11日至2月16日举办，主要活动包括：
 * - 向各大城市的领袖赠送爱情信物
 * - 完成日常任务获得情人节货币
 * - 与其他玩家互动获得节日成就
 * - 参与特殊Boss战斗获得稀有装备
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 浪漫野餐相关法术ID枚举
 *
 * 定义了浪漫野餐系统使用的各种法术ID。
 * 浪漫野餐是情人节的特色互动功能，允许玩家与伴侣共享美好时光。
 */
enum SpellsPicnic
{
    SPELL_BASKET_CHECK              = 45119,  ///< 节日-情人节-浪漫野餐篮子检查 - 检查是否靠近野餐篮
    SPELL_MEAL_PERIODIC             = 45103,  ///< 节日-情人节-浪漫野餐用餐周期 - 周期性效果触发器
    SPELL_MEAL_EAT_VISUAL           = 45120,  ///< 节日-情人节-浪漫野餐用餐视觉效果 - 吃东西的动画
    // SPELL_MEAL_PARTICLE          = 45114, // 节日-情人节-浪漫野餐用餐粒子效果 - 未使用
    SPELL_DRINK_VISUAL              = 45121,  ///< 节日-情人节-浪漫野餐喝饮料视觉效果 - 喝饮料的动画
    SPELL_ROMANTIC_PICNIC_ACHIEV    = 45123,  ///< 浪漫野餐成就法术 - 每5秒触发一次，用于成就判定
};

/**
 * @brief 浪漫野餐光环脚本 (Spell ID: 45102)
 *
 * 实现情人节浪漫野餐的核心功能。当玩家使用浪漫野餐篮时：
 * 1. 玩家自动坐下
 * 2. 周期性地播放吃/喝的动画
 * 3. 检测附近是否有其他玩家也在野餐
 * 4. 如果两个玩家一起野餐，触发浪漫效果（爱心视觉和成就进度）
 *
 * 游戏设计：这是一个社交功能，鼓励玩家与朋友或伴侣互动，
 * 共同享受节日的浪漫氛围。
 *
 * @note 性能考虑：周期性检测附近玩家，需要合理控制检测范围
 */
// 45102 - Romantic Picnic
class spell_love_is_in_the_air_romantic_picnic : public AuraScript
{
    PrepareAuraScript(spell_love_is_in_the_air_romantic_picnic);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_BASKET_CHECK,
            SPELL_MEAL_PERIODIC,
            SPELL_MEAL_EAT_VISUAL,
            SPELL_DRINK_VISUAL,
            SPELL_ROMANTIC_PICNIC_ACHIEV
        });
    }

    /**
     * @brief 处理光环应用效果
     *
     * 当野餐光环应用时：
     * 1. 强制玩家坐下
     * 2. 启动用餐周期性效果
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        // 强制玩家坐下
        target->SetStandState(UNIT_STAND_STATE_SIT);
        // 启动用餐周期性效果
        target->CastSpell(target, SPELL_MEAL_PERIODIC);
    }

    /**
     * @brief 处理周期性效果
     *
     * 每5秒触发一次，执行以下操作：
     * 1. 检查玩家是否仍然坐着，如果站起则移除所有效果
     * 2. 播放随机的吃/喝动画
     * 3. 检测附近是否有其他玩家也在野餐
     * 4. 如果检测到其他野餐玩家，触发浪漫效果
     *
     * @param aurEff 触发的周期性光环效果
     */
    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        // 每5秒触发一次
        Unit* target = GetTarget();

        // 如果玩家不再坐着，移除所有野餐相关效果
        if (target->GetStandState() != UNIT_STAND_STATE_SIT)
        {
            target->RemoveAurasDueToSpell(SPELL_ROMANTIC_PICNIC_ACHIEV);
            target->RemoveAura(GetAura());
            return;
        }

        // 施放篮子检查法术（目标浪漫野餐篮，具体用途未知）
        target->CastSpell(target, SPELL_BASKET_CHECK);
        // 随机播放吃东西或喝饮料的动画
        target->CastSpell(target, RAND(SPELL_MEAL_EAT_VISUAL, SPELL_DRINK_VISUAL));

        bool foundSomeone = false;
        // 检测附近的玩家，看是否有人也拥有同样的野餐光环
        // 如果有，施放浪漫野餐成就法术，用于成就判定和"爱心"视觉效果
        std::list<Player*> playerList;
        Trinity::AnyPlayerInObjectRangeCheck checker(target, INTERACTION_DISTANCE*2);
        Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(target, playerList, checker);
        Cell::VisitWorldObjects(target, searcher, INTERACTION_DISTANCE * 2);
        for (std::list<Player*>::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            if (Player* playerFound = (*itr))
            {
                // 找到附近也在野餐的玩家
                if (target != playerFound && playerFound->HasAura(GetId()))
                {
                    // 双方都施放浪漫效果法术
                    playerFound->CastSpell(playerFound, SPELL_ROMANTIC_PICNIC_ACHIEV, true);
                    target->CastSpell(target, SPELL_ROMANTIC_PICNIC_ACHIEV, true);
                    foundSomeone = true;
                    break;
                }
            }
        }

        // 如果没有找到同伴，移除浪漫效果
        if (!foundSomeone && target->HasAura(SPELL_ROMANTIC_PICNIC_ACHIEV))
            target->RemoveAurasDueToSpell(SPELL_ROMANTIC_PICNIC_ACHIEV);
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_love_is_in_the_air_romantic_picnic::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_love_is_in_the_air_romantic_picnic::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/*######
## Item 21813: Bag of Heart Candies
## 物品 21813：爱心糖果袋
######*/

/**
 * @brief 创建爱心糖果法术ID枚举
 *
 * 定义了爱心糖果袋使用时可能获得的各种爱心糖果法术ID。
 * 每种法术会创建一个带有不同浪漫信息的爱心糖果物品。
 */
enum CreateHeartCandy
{
    SPELL_CREATE_HEART_CANDY_1     = 26668,  ///< 创建爱心糖果1 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_2     = 26670,  ///< 创建爱心糖果2 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_3     = 26671,  ///< 创建爱心糖果3 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_4     = 26672,  ///< 创建爱心糖果4 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_5     = 26673,  ///< 创建爱心糖果5 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_6     = 26674,  ///< 创建爱心糖果6 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_7     = 26675,  ///< 创建爱心糖果7 - 带有浪漫信息
    SPELL_CREATE_HEART_CANDY_8     = 26676   ///< 创建爱心糖果8 - 带有浪漫信息
};

/**
 * @brief 创建爱心糖果法术ID数组
 *
 * 存储所有爱心糖果创建法术的ID，用于随机选择。
 */
std::array<uint32, 8> const CreateHeartCandySpells =
{
    SPELL_CREATE_HEART_CANDY_1, SPELL_CREATE_HEART_CANDY_2, SPELL_CREATE_HEART_CANDY_3, SPELL_CREATE_HEART_CANDY_4,
    SPELL_CREATE_HEART_CANDY_5, SPELL_CREATE_HEART_CANDY_6, SPELL_CREATE_HEART_CANDY_7, SPELL_CREATE_HEART_CANDY_8
};

/**
 * @brief 创建爱心糖果法术脚本 (Spell ID: 26678)
 *
 * 当玩家使用爱心糖果袋时触发。随机创建一个带有浪漫信息的爱心糖果。
 * 玩家可以将这些糖果送给其他玩家，表达爱意。
 *
 * 游戏设计：增加节日的趣味性和社交互动，
 * 让玩家通过送糖果的方式与朋友互动。
 */
// 26678 - Create Heart Candy
class spell_love_is_in_the_air_create_heart_candy : public SpellScript
{
    PrepareSpellScript(spell_love_is_in_the_air_create_heart_candy);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CreateHeartCandySpells);
    }

    /**
     * @brief 处理脚本效果
     *
     * 随机选择一种爱心糖果并创建在玩家背包中。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CreateHeartCandySpells));
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_love_is_in_the_air_create_heart_candy::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 24536, 24655: Something Stinks
## 任务 24536, 24655：有些东西发臭了
######*/

/**
 * @brief "有些东西发臭了"任务相关法术ID枚举
 *
 * 这些法术用于情人节特殊任务线，玩家需要分析NPC身上的香水，
 * 揭露皇冠化学公司的阴谋。
 */
enum SomethingStinks
{
    SPELL_HEAVILY_PERFUMED     = 71507  ///< 浓重香水 - NPC身上的香水效果
};

/**
 * @brief 芳香空气分析法术脚本 (Spell ID: 70192)
 *
 * 用于情人节任务"有些东西发臭了"。玩家使用分析工具检测NPC身上的香水。
 * 成功分析后，移除目标身上的浓重香水效果，并标记为已分析。
 *
 * 任务背景：玩家需要揭露皇冠化学公司在各大城市散布的催情香水的阴谋。
 */
// 70192 - Fragrant Air Analysis
class spell_love_is_in_the_air_fragrant_air_analysis : public SpellScript
{
    PrepareSpellScript(spell_love_is_in_the_air_fragrant_air_analysis);

    /**
     * @brief 验证法术依赖
     *
     * 验证效果值指定的法术是否存在。
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    /**
     * @brief 处理脚本效果
     *
     * 移除目标身上由效果值指定的光环（浓重香水效果）。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(uint32(GetEffectValue()));
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_love_is_in_the_air_fragrant_air_analysis::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 浓重香水的光环脚本 (Spell ID: 71507)
 *
 * NPC身上的香水效果。当此光环被移除时（被玩家分析后），
 * 会触发一个后续法术，标记该NPC已被分析过。
 *
 * 任务设计：防止玩家重复分析同一个NPC，同时让NPC在一定时间后恢复香水效果。
 */
// 71507 - Heavily Perfumed
class spell_love_is_in_the_air_heavily_perfumed : public AuraScript
{
    PrepareAuraScript(spell_love_is_in_the_air_heavily_perfumed);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当浓重香水效果被移除时，施放效果值指定的法术
     * （通常是"最近分析过"标记法术）。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(GetTarget(), uint32(GetEffectInfo(EFFECT_0).CalcValue()));
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_love_is_in_the_air_heavily_perfumed::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 最近分析过光环脚本 (Spell ID: 71508)
 *
 * 标记NPC最近被分析过，防止重复分析。
 * 当此光环自然过期时，NPC会重新获得浓重香水效果，
 * 允许玩家再次分析。
 *
 * 任务机制：创造一个冷却时间，防止玩家反复刷任务。
 */
// 71508 - Recently Analyzed
class spell_love_is_in_the_air_recently_analyzed : public AuraScript
{
    PrepareAuraScript(spell_love_is_in_the_air_recently_analyzed);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HEAVILY_PERFUMED });
    }

    /**
     * @brief 处理光环移除效果
     *
     * 如果光环是因为过期而移除（不是被驱散），则重新施放浓重香水效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 仅在光环自然过期时重新施放香水效果
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
            GetTarget()->CastSpell(GetTarget(), SPELL_HEAVILY_PERFUMED);
    }

    /**
     * @brief 注册光环移除回调
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_love_is_in_the_air_recently_analyzed::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/*######
## Quest 24629, 24635, 24636: A Perfect Puff of Perfume & A Cloudlet of Classy Cologne & Bonbon Blitz
## 任务 24629, 24635, 24636：完美的香水喷雾 & 优雅的古龙水云 & 糖果轰炸
######*/

/**
 * @brief 样品满意度光环脚本 (Spell ID: 69438)
 *
 * 用于情人节日常任务中分发样品时的效果管理。
 * 光环有30%概率在每次周期时自动移除，模拟样品效果的不稳定性。
 *
 * 任务设计：样品效果是临时的，增加任务的随机性和趣味性。
 */
// 69438 - Sample Satisfaction
class spell_love_is_in_the_air_sample_satisfaction : public AuraScript
{
    PrepareAuraScript(spell_love_is_in_the_air_sample_satisfaction);

    /**
     * @brief 处理周期性效果
     *
     * 每次周期触发时，有30%概率移除此光环。
     *
     * @param aurEff 触发的周期性光环效果
     */
    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        if (roll_chance_i(30))
            Remove();
    }

    /**
     * @brief 注册周期性效果回调
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_love_is_in_the_air_sample_satisfaction::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/*######
## Quest 24541, 24656: Pilfering Perfume
## 任务 24541, 24656：偷取香水
######*/

/**
 * @brief 偷取香水任务相关法术和模型ID枚举
 *
 * 玩家需要伪装成皇冠快递服务公司的员工，潜入公司窃取证据。
 */
enum PilferingPerfume
{
    SPELL_SERVICE_UNIFORM       = 71450,  ///< 皇冠快递服务制服 - 伪装法术

    MODEL_GOBLIN_MALE           = 31002,  ///< 地精男性模型ID - 伪装用
    MODEL_GOBLIN_FEMALE         = 31003   ///< 地精女性模型ID - 伪装用
};

/**
 * @brief 皇冠快递服务制服光环脚本 (Spell ID: 71450)
 *
 * 将玩家伪装成皇冠快递服务公司的地精员工。
 * 根据玩家性别选择对应的地精模型。
 * 光环移除时，同时清除相关的伪装效果。
 *
 * 任务设计：玩家需要伪装才能进入皇冠化学公司的设施执行任务。
 */
// 71450 - Crown Parcel Service Uniform
class spell_love_is_in_the_air_service_uniform : public AuraScript
{
    PrepareAuraScript(spell_love_is_in_the_air_service_uniform);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    /**
     * @brief 处理光环应用效果
     *
     * 将玩家变成地精造型，根据性别选择对应的模型。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            // 根据性别选择对应的地精模型
            if (target->GetNativeGender() == GENDER_MALE)
                target->SetDisplayId(MODEL_GOBLIN_MALE);
            else
                target->SetDisplayId(MODEL_GOBLIN_FEMALE);
        }
    }

    /**
     * @brief 处理光环移除效果
     *
     * 当制服光环移除时，同时移除效果值指定的相关法术效果。
     *
     * @param aurEff 触发的光环效果
     * @param mode 光环效果处理模式
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(uint32(GetEffectInfo(EFFECT_0).CalcValue()));
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectRemoveFn(spell_love_is_in_the_air_service_uniform::AfterApply, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_love_is_in_the_air_service_uniform::AfterRemove, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 取消服务制服法术脚本
 *
 * 用于任务物品"皇冠化学公司物资"使用时，移除玩家的伪装效果。
 * 法术ID：
 * - 71522: 皇冠化学公司物资
 * - 71539: 皇冠化学公司物资
 *
 * 任务机制：玩家获取物资时会暴露身份，需要移除伪装。
 */
// 71522 - Crown Chemical Co. Supplies
// 71539 - Crown Chemical Co. Supplies
class spell_love_is_in_the_air_cancel_service_uniform : public SpellScript
{
    PrepareSpellScript(spell_love_is_in_the_air_cancel_service_uniform);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SERVICE_UNIFORM });
    }

    /**
     * @brief 处理脚本效果
     *
     * 移除目标身上的服务制服伪装效果。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_SERVICE_UNIFORM);
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_love_is_in_the_air_cancel_service_uniform::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Item 49351, 49352: Perfume Neutralizer & Cologne Neutralizer
## 物品 49351, 49352：香水中和剂 & 古龙水中和剂
######*/

/**
 * @brief 香水/古龙水免疫法术脚本
 *
 * 用于情人节节日物品"香水中和剂"和"古龙水中和剂"。
 * 使用后移除玩家身上的香水和古龙水效果。
 *
 * 法术ID：
 * - 68529: 香水免疫
 * - 68530: 古龙水免疫
 *
 * 游戏设计：允许玩家清除不喜欢或不再需要的香水/古龙水效果。
 */
// 68529 - Perfume Immune
// 68530 - Cologne Immune
class spell_love_is_in_the_air_perfume_cologne_immune : public SpellScript
{
    PrepareSpellScript(spell_love_is_in_the_air_perfume_cologne_immune);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo(
        {
            uint32(spellInfo->GetEffect(EFFECT_0).CalcValue()),
            uint32(spellInfo->GetEffect(EFFECT_1).CalcValue())
        });
    }

    /**
     * @brief 处理脚本效果
     *
     * 移除施法者身上由效果值指定的光环。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(uint32(GetEffectValue()));
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_love_is_in_the_air_perfume_cologne_immune::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
        OnEffectHit += SpellEffectFn(spell_love_is_in_the_air_perfume_cologne_immune::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册所有情人节脚本
 *
 * 此函数在服务器启动时被调用，用于注册本文件中定义的所有法术脚本。
 * 将脚本与对应的法术ID关联起来，使游戏能够正确处理情人节相关的法术效果。
 */
void AddSC_event_love_is_in_the_air()
{
    // 浪漫野餐系统
    RegisterSpellScript(spell_love_is_in_the_air_romantic_picnic);

    // 爱心糖果系统
    RegisterSpellScript(spell_love_is_in_the_air_create_heart_candy);

    // 香水分析任务
    RegisterSpellScript(spell_love_is_in_the_air_fragrant_air_analysis);
    RegisterSpellScript(spell_love_is_in_the_air_heavily_perfumed);
    RegisterSpellScript(spell_love_is_in_the_air_recently_analyzed);

    // 样品分发任务
    RegisterSpellScript(spell_love_is_in_the_air_sample_satisfaction);

    // 偷取香水任务
    RegisterSpellScript(spell_love_is_in_the_air_service_uniform);
    RegisterSpellScript(spell_love_is_in_the_air_cancel_service_uniform);

    // 香水中和剂
    RegisterSpellScript(spell_love_is_in_the_air_perfume_cologne_immune);
}
