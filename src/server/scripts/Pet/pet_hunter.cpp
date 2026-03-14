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
 * @file pet_hunter.cpp
 * @brief 猎人宠物AI和法术脚本模块
 *
 * 本模块实现猎人职业相关宠物和法术的AI行为，包括：
 * - 蛇陷阱蛇 (Snake Trap)：随机攻击召唤者战斗中的敌人，毒蛇施放毒药
 * - 宠物冲锋 (Charge)：冲锋后移除攻击强度加成光环
 * - 警戒犬 (Guard Dog)：低吼时恢复宠物快乐值并增加威胁
 * - 银背猩猩 (Silverback)：低吼时治疗宠物
 * - 淘汰兽群 (Culling the Herd)：爪击/撕咬/拍击时触发伤害加成
 *
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_hun_".
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "TemporarySummon.h"

/**
 * @brief 猎人法术ID枚举
 *
 * 定义蛇陷阱中蛇使用的毒药法术ID
 */
enum HunterSpells
{
    SPELL_HUNTER_CRIPPLING_POISON       = 30981,  ///< 致残毒药 - 由毒蛇施放，降低移动速度
    SPELL_HUNTER_DEADLY_POISON_PASSIVE  = 34657,  ///< 致命毒药被动 - 毒蛇出生时自动施加
    SPELL_HUNTER_MIND_NUMBING_POISON    = 25810   ///< 麻痹毒药 - 由毒蛇施放，延长施法时间
};

/**
 * @brief 猎人生物ID枚举
 *
 * 定义蛇陷阱中不同类型蛇的NPC ID
 */
enum HunterCreatures
{
    NPC_HUNTER_VIPER                    = 19921   ///< 毒蛇NPC ID - 与普通蛇区分
};

/**
 * @brief 宠物法术杂项枚举
 *
 * 定义宠物天赋相关的法术ID和图标ID
 */
enum PetSpellsMisc
{
    SPELL_PET_GUARD_DOG_HAPPINESS   = 54445,  ///< 警戒犬快乐值法术 - 恢复宠物快乐值
    SPELL_PET_SILVERBACK_RANK_1     = 62800,  ///< 银背猩猩等级1 - 治疗宠物
    SPELL_PET_SILVERBACK_RANK_2     = 62801,  ///< 银背猩猩等级2 - 治疗宠物

    SPELL_PET_SWOOP                 = 52825,  ///< 俯冲法术 - 宠物冲锋技能
    SPELL_PET_CHARGE                = 61685,  ///< 冲锋法术 - 宠物冲锋技能

    PET_ICON_ID_GROWL               = 201,    ///< 低吼技能图标ID - 用于过滤低吼
    PET_ICON_ID_CLAW                = 262,    ///< 爪击技能图标ID - 用于过滤爪击
    PET_ICON_ID_BITE                = 1680,   ///< 撕咬技能图标ID - 用于过滤撕咬
    PET_ICON_ID_SMACK               = 473     ///< 拍击技能图标ID - 用于过滤拍击
};

/**
 * @brief 蛇陷阱蛇AI
 *
 * 继承自ScriptedAI，实现猎人蛇陷阱召唤的蛇的行为逻辑。
 * 蛇会随机选择并攻击召唤者战斗中的敌人。
 * 毒蛇（Viper）会施放毒药法术，普通蛇只进行近战攻击。
 *
 * 特殊行为：
 * - 召唤时设置生命值并施加致命毒药（仅普通蛇）
 * - 随机选择召唤者战斗中的敌人作为目标
 * - 不会打破控制效果
 * - 毒蛇有33%几率每3秒施放毒药
 */
struct npc_pet_hunter_snake_trap : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_hunter_snake_trap(Creature* creature) : ScriptedAI(creature), _isViper(false), _spellTimer(0) { }

    /**
     * @brief 进入战斗回调（空实现）
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* /*who*/) override { }

    /**
     * @brief 出现回调
     *
     * 调用时机：蛇被召唤出现时
     *
     * 功能：
     * 1. 判断是否为毒蛇（不同NPC ID）
     * 2. 设置生命值（基于等级计算）
     * 3. 添加攻击时间随机偏移（避免所有蛇同时攻击）
     * 4. 施加致命毒药光环（仅普通蛇）
     *
     * 性能注意：召唤时一次性调用
     */
    void JustAppeared() override
    {
        // 判断是否为毒蛇
        _isViper = me->GetEntry() == NPC_HUNTER_VIPER ? true : false;

        // 设置生命值：公式为107 * (等级-40) * 0.025
        me->SetMaxHealth(uint32(107 * (me->GetLevel() - 40) * 0.025f));
        // 添加随机时间偏移，使所有蛇不会同时攻击
        me->SetAttackTime(BASE_ATTACK, me->GetAttackTime(BASE_ATTACK) + urandms(0,6));

        // 如果不是毒蛇，施加致命毒药被动光环
        if (!_isViper && !me->HasAura(SPELL_HUNTER_DEADLY_POISON_PASSIVE))
            DoCast(me, SPELL_HUNTER_DEADLY_POISON_PASSIVE, true);
    }

    /**
     * @brief 视线内移动回调（空实现）
     * @param who 移动进入视野的单位
     *
     * 重写为空，因为蛇使用随机目标选择逻辑，不使用默认视线检测
     */
    // Redefined for random target selection:
    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 调用时机：每个游戏循环tick
     *
     * 功能：
     * 1. 检查当前目标是否有可打破的控制效果，如果有则停止攻击
     * 2. 如果没有固定目标，随机选择召唤者战斗中的敌人
     * 3. 毒蛇每3秒有33%几率施放毒药
     * 4. 执行近战攻击
     *
     * 目标选择优先级：
     * - 优先PvP目标
     * - 如果没有PvP目标，选择PvE目标
     * - 随机选择一个有效目标进行固定攻击
     *
     * 性能注意：每帧调用，涉及战斗引用遍历
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查当前目标是否有可被伤害打破的控制光环
        if (me->GetVictim() && me->GetVictim()->HasBreakableByDamageCrowdControlAura())
        {
            // 不打破控制效果
            me->GetThreatManager().ClearFixate();
            me->InterruptNonMeleeSpells(false);
            me->AttackStop();
            return;
        }

        // 如果没有固定目标，寻找新目标
        if (me->IsSummon() && !me->GetThreatManager().GetFixateTarget())
        {
            Unit* summoner = me->ToTempSummon()->GetSummonerUnit();

            std::vector<Unit*> targets;

            // Lambda函数：添加有效目标到列表
            auto addTargetIfValid = [this, &targets, summoner](CombatReference* ref) mutable
            {
                Unit* enemy = ref->GetOther(summoner);
                // 检查目标是否有效：没有控制效果、可攻击、在攻击距离内
                if (!enemy->HasBreakableByDamageCrowdControlAura() && me->CanCreatureAttack(enemy) && me->IsWithinDistInMap(enemy, me->GetAttackDistance(enemy)))
                    targets.push_back(enemy);
            };

            // 收集PvP目标
            for (std::pair<ObjectGuid const, PvPCombatReference*> const& pair : summoner->GetCombatManager().GetPvPCombatRefs())
                addTargetIfValid(pair.second);

            // 如果没有PvP目标，收集PvE目标
            if (targets.empty())
                for (std::pair<ObjectGuid const, CombatReference*> const& pair : summoner->GetCombatManager().GetPvECombatRefs())
                    addTargetIfValid(pair.second);

            // 与所有目标建立战斗关系
            for (Unit* target : targets)
                me->EngageWithTarget(target);

            // 随机选择一个目标进行固定攻击
            if (!targets.empty())
            {
                Unit* target = Trinity::Containers::SelectRandomContainerElement(targets);
                me->GetThreatManager().FixateTarget(target);
            }
        }

        // 检查是否有有效目标
        if (!UpdateVictim())
            return;

        // 毒蛇施放毒药逻辑
        if (_isViper)
        {
            if (_spellTimer <= diff)
            {
                // 33%几率施放毒药
                if (!urand(0, 2))
                    DoCastVictim(RAND(SPELL_HUNTER_MIND_NUMBING_POISON, SPELL_HUNTER_CRIPPLING_POISON));

                _spellTimer = 3000;
            }
            else
                _spellTimer -= diff;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    bool _isViper;       ///< 是否为毒蛇（不同行为）
    uint32 _spellTimer;  ///< 毒药施放计时器（毫秒）
};

/**
 * @brief 宠物冲锋法术脚本 (57627)
 *
 * AuraScript实现，处理宠物冲锋技能的攻击强度加成移除逻辑。
 * 当宠物进行近战攻击后，移除冲锋带来的攻击强度加成光环。
 *
 * 触发条件：宠物冲锋后的下一次近战攻击
 */
// 57627 - Charge
class spell_pet_charge : public AuraScript
{
    PrepareAuraScript(spell_pet_charge);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证是否成功
     *
     * 功能：验证俯冲和冲锋法术ID是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_PET_SWOOP,
            SPELL_PET_CHARGE
        });
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 调用时机：宠物进行近战攻击时触发
     *
     * 功能：
     * 1. 阻止默认动作
     * 2. 查找宠物身上的俯冲或冲锋攻击强度加成光环
     * 3. 移除该光环的一个层数
     *
     * 性能注意：每次近战攻击触发一次
     */
    void HandleDummy(AuraEffect const* /*aurEff*/, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        // 移除+%攻击强度光环
        Unit* pet = eventInfo.GetActor();
        Aura* aura = pet->GetAura(SPELL_PET_SWOOP, pet->GetGUID());
        if (!aura)
            aura = pet->GetAura(SPELL_PET_CHARGE, pet->GetGUID());

        if (!aura)
            return;

        // 移除一层光环
        aura->DropCharge(AURA_REMOVE_BY_EXPIRE);
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_pet_charge::HandleDummy, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @brief 警戒犬天赋光环脚本 (-53178)
 *
 * AuraScript实现，处理猎人宠物警戒犬天赋效果。
 * 当宠物施放低吼时，恢复宠物快乐值并增加额外威胁。
 *
 * 触发条件：宠物施放低吼技能（通过图标ID过滤）
 */
// -53178 - Guard Dog
class spell_pet_guard_dog : public AuraScript
{
    PrepareAuraScript(spell_pet_guard_dog);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证是否成功
     *
     * 功能：验证警戒犬快乐值法术ID是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PET_GUARD_DOG_HAPPINESS });
    }

    /**
     * @brief 检查是否可以触发
     * @param eventInfo 触发事件信息
     * @return 是否可以触发
     *
     * 调用时机：每次触发事件时
     *
     * 功能：过滤只有低吼技能才能触发此效果
     * 由于低吼与其他技能共享家族标志，使用技能图标ID进行过滤
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // 低吼与其他法术共享家族标志
        // 改用技能图标ID过滤
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || spellInfo->SpellIconID != PET_ICON_ID_GROWL)
            return false;

        return true;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 调用时机：低吼施放成功时
     *
     * 功能：
     * 1. 恢复宠物快乐值
     * 2. 根据天赋效果百分比增加额外威胁值
     *
     * 性能注意：每次低吼触发一次
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        // 恢复宠物快乐值
        Unit* caster = eventInfo.GetActor();
        caster->CastSpell(nullptr, SPELL_PET_GUARD_DOG_HAPPINESS, aurEff);

        // 增加额外威胁
        Unit* target = eventInfo.GetProcTarget();
        if (!target->CanHaveThreatList())
            return;
        // 计算额外威胁：基础威胁 * 天赋百分比
        float addThreat = CalculatePct(ASSERT_NOTNULL(eventInfo.GetSpellInfo())->GetEffect(EFFECT_0).CalcValue(caster), aurEff->GetAmount());
        target->GetThreatManager().AddThreat(caster, addThreat, GetSpellInfo(), false, true);
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pet_guard_dog::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_pet_guard_dog::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @brief 银背猩猩天赋光环脚本 (-62764)
 *
 * AuraScript实现，处理猎人宠物银背猩猩天赋效果。
 * 当宠物施放低吼时，治疗宠物。
 *
 * 触发条件：宠物施放低吼技能（通过图标ID过滤）
 * 治疗量根据天赋等级不同而不同
 */
// -62764 - Silverback
class spell_pet_silverback : public AuraScript
{
    PrepareAuraScript(spell_pet_silverback);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 验证是否成功
     *
     * 功能：验证银背猩猩相关法术ID是否有效
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PET_GUARD_DOG_HAPPINESS });
    }

    /**
     * @brief 检查是否可以触发
     * @param eventInfo 触发事件信息
     * @return 是否可以触发
     *
     * 调用时机：每次触发事件时
     *
     * 功能：过滤只有低吼技能才能触发此效果
     * 由于低吼与其他技能共享家族标志，使用技能图标ID进行过滤
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // 低吼与其他法术共享家族标志
        // 改用技能图标ID过滤
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo || spellInfo->SpellIconID != PET_ICON_ID_GROWL)
            return false;

        return true;
    }

    /**
     * @brief 处理触发效果
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 调用时机：低吼施放成功时
     *
     * 功能：根据天赋等级施放对应的治疗法术
     * - 等级1：施放 SPELL_PET_SILVERBACK_RANK_1
     * - 等级2：施放 SPELL_PET_SILVERBACK_RANK_2
     *
     * 性能注意：每次低吼触发一次
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        // 根据天赋等级选择治疗法术
        static uint32 const triggerSpell[2] = { SPELL_PET_SILVERBACK_RANK_1, SPELL_PET_SILVERBACK_RANK_2 };

        PreventDefaultAction();

        // 获取对应等级的法术ID
        uint32 spellId = triggerSpell[GetSpellInfo()->GetRank() - 1];
        eventInfo.GetActor()->CastSpell(nullptr, spellId, aurEff);
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pet_silverback::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_pet_silverback::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @brief 淘汰兽群天赋光环脚本 (-61680)
 *
 * AuraScript实现，处理猎人宠物淘汰兽群天赋效果。
 * 当宠物施放爪击、撕咬或拍击时，触发伤害加成效果。
 *
 * 触发条件：宠物施放爪击、撕咬或拍击技能（通过图标ID过滤）
 * 由于这些技能与其他技能共享家族标志，使用技能图标ID进行过滤
 */
// -61680 - Culling the Herd
class spell_pet_culling_the_herd : public AuraScript
{
    PrepareAuraScript(spell_pet_culling_the_herd);

    /**
     * @brief 检查是否可以触发
     * @param eventInfo 触发事件信息
     * @return 是否可以触发
     *
     * 调用时机：每次触发事件时
     *
     * 功能：过滤只有爪击、撕咬或拍击才能触发此效果
     * 由于这些技能与其他法术共享家族标志，使用技能图标ID进行过滤
     */
    bool CheckProc(ProcEventInfo& eventInfo)
    {
        // 爪击、撕咬和拍击与其他法术共享家族标志
        // 改用技能图标ID过滤
        SpellInfo const* spellInfo = eventInfo.GetSpellInfo();
        if (!spellInfo)
            return false;

        // 检查是否为有效的技能图标
        switch (spellInfo->SpellIconID)
        {
            case PET_ICON_ID_CLAW:   // 爪击
            case PET_ICON_ID_BITE:   // 撕咬
            case PET_ICON_ID_SMACK:  // 拍击
                break;
            default:
                return false;
        }

        return true;
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_pet_culling_the_herd::CheckProc);
    }
};

/**
 * @brief 注册猎人宠物脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有猎人宠物AI和法术脚本到脚本系统
 */
void AddSC_hunter_pet_scripts()
{
    RegisterCreatureAI(npc_pet_hunter_snake_trap);
    RegisterSpellScript(spell_pet_charge);
    RegisterSpellScript(spell_pet_guard_dog);
    RegisterSpellScript(spell_pet_silverback);
    RegisterSpellScript(spell_pet_culling_the_herd);
}
