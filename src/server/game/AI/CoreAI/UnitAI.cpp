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
 * @file UnitAI.cpp
 * @brief UnitAI 基类实现文件
 *
 * 本文件实现了 UnitAI 基类，这是所有单位 AI 的基础类。
 * 提供了目标选择、攻击控制、法术施放等核心 AI 功能。
 * 所有具体的 AI 实现（如 CreatureAI、PlayerAI 等）都继承自此类。
 */

#include "UnitAI.h"
#include "Containers.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

/**
 * @brief 开始攻击指定目标
 *
 * 让单位开始攻击指定受害者，并进入追逐状态。
 * 如果单位处于分心状态，会自动清除该状态并清除移动指令。
 *
 * @param victim 要攻击的目标单位，如果为 nullptr 则不执行任何操作
 *
 * @note 该函数会执行以下操作：
 *       1. 尝试对目标发起攻击（Attack 方法）
 *       2. 如果攻击成功且单位处于分心状态，则清除分心状态
 *       3. 清除当前的移动指令
 *       4. 开始追逐目标
 *
 * @see Unit::Attack()
 * @see MotionMaster::MoveChase()
 */
void UnitAI::AttackStart(Unit* victim)
{
    if (victim && me->Attack(victim, true))
    {
        // 攻击时清除分心状态
        if (me->HasUnitState(UNIT_STATE_DISTRACTED))
        {
            me->ClearUnitState(UNIT_STATE_DISTRACTED);
            me->GetMotionMaster()->Clear();
        }
        me->GetMotionMaster()->MoveChase(victim);
    }
}

/**
 * @brief 初始化 AI
 *
 * 在单位被创建或重置时调用，用于初始化 AI 状态。
 * 如果单位未死亡，则会调用 Reset() 方法重置 AI 状态。
 *
 * @note 该方法通常在以下情况下被调用：
 *       - 单位首次被创建时
 *       - 单位重生时
 *       - 单位从其他 AI 状态转换时
 *
 * @see UnitAI::Reset()
 */
void UnitAI::InitializeAI()
{
    if (!me->isDead())
        Reset();
}

/**
 * @brief 当单位被魅惑时的处理函数
 *
 * 当单位进入或退出魅惑状态时调用此方法。
 * 如果不是新的魅惑状态，则会安排 AI 切换。
 *
 * @param isNew 如果为 true 表示是新进入魅惑状态，
 *              如果为 false 表示魅惑状态结束或改变
 *
 * @note 魅惑机制允许玩家或 NPC 控制其他单位。
 *       当魅惑状态改变时，可能需要切换到不同的 AI 实现。
 *
 * @see Unit::ScheduleAIChange()
 */
void UnitAI::OnCharmed(bool isNew)
{
    if (!isNew)
        me->ScheduleAIChange();
}

/**
 * @brief 法师单位的攻击开始函数
 *
 * 专为施法者单位设计的攻击开始函数，允许单位在指定距离外攻击目标。
 * 与普通 AttackStart 不同，该方法不会立即进行近战攻击，
 * 而是保持在指定距离外进行法术攻击。
 *
 * @param victim 要攻击的目标单位
 * @param dist 与目标保持的距离（码为单位）
 *
 * @note 该函数适用于：
 *       - 法师型 NPC
 *       - 远程攻击者
 *       - 需要保持距离的施法单位
 *
 * @see AttackStart()
 * @see MotionMaster::MoveChase()
 */
void UnitAI::AttackStartCaster(Unit* victim, float dist)
{
    if (victim && me->Attack(victim, false))
        me->GetMotionMaster()->MoveChase(victim, dist);
}

/**
 * @brief 如果准备好则执行近战攻击
 *
 * 检查单位是否准备好进行近战攻击，如果准备好则执行攻击。
 * 该函数会处理主手和副手（如果装备了副手武器）的攻击。
 *
 * @note 该函数的执行流程：
 *       1. 检查单位是否正在施法，如果是则返回
 *       2. 获取当前目标
 *       3. 检查是否在近战范围内
 *       4. 如果主手攻击准备好，执行攻击并重置攻击计时器
 *       5. 如果有副手武器且副手攻击准备好，执行副手攻击并重置副手攻击计时器
 *
 * @warning 该函数应该在每个 AI 更新周期中调用，
 *          通常在 UpdateAI 函数中调用
 *
 * @see Unit::HasUnitState()
 * @see Unit::AttackerStateUpdate()
 * @see Unit::isAttackReady()
 */
void UnitAI::DoMeleeAttackIfReady()
{
    // 如果正在施法，则不进行近战攻击
    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    Unit* victim = me->GetVictim();

    // 检查是否在近战范围内
    if (!me->IsWithinMeleeRange(victim))
        return;

    // 确保攻击已准备好且当前没有在施法，然后检查距离
    if (me->isAttackReady())
    {
        me->AttackerStateUpdate(victim);
        me->resetAttackTimer();
    }

    // 如果有副手武器，处理副手攻击
    if (me->haveOffhandWeapon() && me->isAttackReady(OFF_ATTACK))
    {
        me->AttackerStateUpdate(victim, OFF_ATTACK);
        me->resetAttackTimer(OFF_ATTACK);
    }
}

/**
 * @brief 如果准备好则执行法术攻击
 *
 * 检查单位是否准备好施放指定的攻击法术，如果准备好则施放。
 * 该函数主要用于施法者类型的单位。
 *
 * @param spell 要施放的法术 ID
 * @return bool 如果成功施放法术返回 true，如果目标超出范围或其他原因无法施放返回 false
 *
 * @note 该函数的执行流程：
 *       1. 检查是否正在施法或攻击是否准备好
 *       2. 获取法术信息
 *       3. 检查目标是否在法术的最大射程内
 *       4. 如果在范围内，施放法术并重置攻击计时器
 *
 * @warning 返回 true 不一定表示法术施放成功，只表示满足了施放条件并尝试施放
 *
 * @see Unit::CastSpell()
 * @see SpellInfo::GetMaxRange()
 */
bool UnitAI::DoSpellAttackIfReady(uint32 spell)
{
    // 如果正在施法或攻击未准备好，返回 true（表示不需要追逐目标）
    if (me->HasUnitState(UNIT_STATE_CASTING) || !me->isAttackReady())
        return true;

    if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell))
    {
        // 检查目标是否在法术射程内
        if (me->IsWithinCombatRange(me->GetVictim(), spellInfo->GetMaxRange(false)))
        {
            me->CastSpell(me->GetVictim(), spell, false);
            me->resetAttackTimer();
            return true;
        }
    }

    return false;
}

/**
 * @brief 选择目标单位
 *
 * 根据指定的选择方法和条件选择一个目标单位。
 * 这是一个便捷方法，内部使用 DefaultTargetSelector 选择器。
 *
 * @param targetType 目标选择方法（如最大威胁、最小威胁、随机等）
 * @param position 目标在威胁列表中的位置（从 0 开始）
 * @param dist 最大距离限制，正数表示在该距离内，负数表示在该距离外，0 表示不限制
 * @param playerOnly 是否只选择玩家目标
 * @param withTank 是否包含当前坦克（当前受害者）
 * @param aura 光环筛选条件，正数表示必须具有该光环，负数表示不能具有该光环，0 表示不筛选
 * @return Unit* 选中的目标单位，如果没有符合条件的目标则返回 nullptr
 *
 * @see SelectTargetMethod
 * @see DefaultTargetSelector
 */
Unit* UnitAI::SelectTarget(SelectTargetMethod targetType, uint32 position, float dist, bool playerOnly, bool withTank, int32 aura)
{
    return SelectTarget(targetType, position, DefaultTargetSelector(me, dist, playerOnly, withTank, aura));
}

/**
 * @brief 选择目标列表
 *
 * 根据指定的选择方法和条件选择多个目标单位，并将结果存入列表。
 * 这是一个便捷方法，内部使用 DefaultTargetSelector 选择器。
 *
 * @param[out] targetList 用于存储选中目标的列表
 * @param num 要选择的目标数量
 * @param targetType 目标选择方法
 * @param offset 从威胁列表中的第几个目标开始选择（跳过前 offset 个目标）
 * @param dist 最大距离限制，正数表示在该距离内，负数表示在该距离外，0 表示不限制
 * @param playerOnly 是否只选择玩家目标
 * @param withTank 是否包含当前坦克
 * @param aura 光环筛选条件，正数表示必须具有该光环，负数表示不能具有该光环，0 表示不筛选
 *
 * @see SelectTargetMethod
 * @see DefaultTargetSelector
 */
void UnitAI::SelectTargetList(std::list<Unit*>& targetList, uint32 num, SelectTargetMethod targetType, uint32 offset, float dist, bool playerOnly, bool withTank, int32 aura)
{
    SelectTargetList(targetList, num, targetType, offset, DefaultTargetSelector(me, dist, playerOnly, withTank, aura));
}

/**
 * @brief 根据法术信息自动选择目标并施放法术
 *
 * 根据法术的 AI 目标类型（AISpellInfo）自动选择合适的目标并施放法术。
 * 这是一种智能施法方式，会根据法术属性自动确定目标。
 *
 * @param spellId 要施放的法术 ID
 * @return SpellCastResult 法术施放结果
 *
 * @note 该函数根据 AISpellInfo 中的目标类型选择目标：
 *       - AITARGET_SELF: 目标为自身
 *       - AITARGET_VICTIM: 目标为当前受害者
 *       - AITARGET_ENEMY: 目标为随机敌对目标
 *       - AITARGET_ALLY: 目标为友方单位（默认为自身）
 *       - AITARGET_BUFF: 目标为需要增益的单位（默认为自身）
 *       - AITARGET_DEBUFF: 目标为需要减益的敌对目标
 *
 * @warning 如果没有找到合适的目标，会返回 SPELL_FAILED_BAD_TARGETS
 *
 * @see AISpellInfoType
 * @see Unit::CastSpell()
 */
SpellCastResult UnitAI::DoCast(uint32 spellId)
{
    Unit* target = nullptr;

    // 根据法术的 AI 目标类型选择目标
    switch (AISpellInfo[spellId].target)
    {
        default:
        case AITARGET_SELF:
            target = me;
            break;
        case AITARGET_VICTIM:
            target = me->GetVictim();
            break;
        case AITARGET_ENEMY:
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId))
            {
                bool playerOnly = spellInfo->HasAttribute(SPELL_ATTR3_ONLY_TARGET_PLAYERS);
                target = SelectTarget(SelectTargetMethod::Random, 0, spellInfo->GetMaxRange(false), playerOnly);
            }
            break;
        }
        case AITARGET_ALLY:
            target = me;
            break;
        case AITARGET_BUFF:
            target = me;
            break;
        case AITARGET_DEBUFF:
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId))
            {
                bool playerOnly = spellInfo->HasAttribute(SPELL_ATTR3_ONLY_TARGET_PLAYERS);
                float range = spellInfo->GetMaxRange(false);

                // 创建目标选择器，选择没有该减益效果的目标
                DefaultTargetSelector targetSelector(me, range, playerOnly, true, -(int32)spellId);
                // 优先尝试对当前受害者施放（如果法术允许且目标符合条件）
                if (!(spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_VICTIM)
                    && targetSelector(me->GetVictim()))
                    target = me->GetVictim();
                else
                    target = SelectTarget(SelectTargetMethod::Random, 0, targetSelector);
            }
            break;
        }
    }

    if (target)
        return me->CastSpell(target, spellId, false);

    return SPELL_FAILED_BAD_TARGETS;
}

/**
 * @brief 对指定目标施放法术
 *
 * 对指定目标施放指定的法术，可以传入额外的施法参数。
 * 该方法会检查单位是否正在施法，除非参数指定忽略正在施法状态。
 *
 * @param victim 法术目标单位
 * @param spellId 要施放的法术 ID
 * @param args 施法额外参数，如触发标志、施法物品等
 * @return SpellCastResult 法术施放结果
 *
 * @note 如果单位正在施法且参数中未设置 TRIGGERED_IGNORE_CAST_IN_PROGRESS，
 *       则会返回 SPELL_FAILED_SPELL_IN_PROGRESS
 *
 * @see CastSpellExtraArgs
 * @see Unit::CastSpell()
 */
SpellCastResult UnitAI::DoCast(Unit* victim, uint32 spellId, CastSpellExtraArgs const& args)
{
    if (me->HasUnitState(UNIT_STATE_CASTING) && !(args.TriggerFlags & TRIGGERED_IGNORE_CAST_IN_PROGRESS))
        return SPELL_FAILED_SPELL_IN_PROGRESS;

    return me->CastSpell(victim, spellId, args);
}

/**
 * @brief 对当前受害者施放法术
 *
 * 对当前威胁列表顶部的目标（当前受害者）施放指定法术。
 *
 * @param spellId 要施放的法术 ID
 * @param args 施法额外参数
 * @return SpellCastResult 法术施放结果
 *         如果没有当前受害者，返回 SPELL_FAILED_BAD_TARGETS
 *
 * @see DoCast()
 * @see Unit::GetVictim()
 */
SpellCastResult UnitAI::DoCastVictim(uint32 spellId, CastSpellExtraArgs const& args)
{
    if (Unit* victim = me->GetVictim())
        return DoCast(victim, spellId, args);

    return SPELL_FAILED_BAD_TARGETS;
}

/**
 * @brief 获取法术的最大施法距离
 *
 * 获取指定法术的最大施法距离，根据法术是正面效果还是负面效果返回不同的距离。
 *
 * @param spellId 法术 ID
 * @param positive 是否为正面效果（增益法术），true 表示正面效果，false 表示负面效果
 * @return float 法术的最大施法距离，如果法术不存在则返回 0
 *
 * @see SpellInfo::GetMaxRange()
 */
float UnitAI::DoGetSpellMaxRange(uint32 spellId, bool positive)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    return spellInfo ? spellInfo->GetMaxRange(positive) : 0;
}

/**
 * @brief 更新目标类型的宏定义
 *
 * 如果当前 AI 信息的目标类型小于指定类型，则更新为目标类型。
 * 这个宏用于在分析法术效果时确定最合适的目标类型。
 *
 * @param a 目标类型常量（如 AITARGET_VICTIM, AITARGET_ENEMY 等）
 */
#define UPDATE_TARGET(a) {if (AIInfo->target<a) AIInfo->target=a;}

/**
 * @brief 填充 AI 法术信息数组
 *
 * 初始化并填充 AISpellInfo 数组，为每个法术设置 AI 相关信息。
 * 该函数分析所有法术的效果和属性，确定每个法术的使用条件和目标类型。
 *
 * @note 该函数应该在 AI 初始化时调用一次。
 *       它会分析所有法术并设置以下信息：
 *       - condition: 使用条件（战斗中、死亡时、进入战斗时）
 *       - cooldown: 冷却时间
 *       - target: 目标类型
 *       - realCooldown: 实际冷却时间
 *       - maxRange: 最大射程
 *
 * @warning 该函数会分配内存创建 AISpellInfo 数组，
 *          需要在 AI 析构时释放
 */
void UnitAI::FillAISpellInfo()
{
    AISpellInfo = new AISpellInfoType[sSpellMgr->GetSpellInfoStoreSize()];

    AISpellInfoType* AIInfo = AISpellInfo;
    for (uint32 i = 0; i < sSpellMgr->GetSpellInfoStoreSize(); ++i, ++AIInfo)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(i);
        if (!spellInfo)
            continue;

        // 根据法术属性确定使用条件
        if (spellInfo->HasAttribute(SPELL_ATTR0_CASTABLE_WHILE_DEAD))
            AIInfo->condition = AICOND_DIE;
        else if (spellInfo->IsPassive() || spellInfo->GetDuration() == -1)
            AIInfo->condition = AICOND_AGGRO;
        else
            AIInfo->condition = AICOND_COMBAT;

        // 设置冷却时间
        if (AIInfo->cooldown < spellInfo->RecoveryTime)
            AIInfo->cooldown = spellInfo->RecoveryTime;

        // 分析法术效果，确定目标类型
        if (spellInfo->GetMaxRange(false))
        {
            for (SpellEffectInfo const& effect : spellInfo->GetEffects())
            {
                uint32 targetType = effect.TargetA.GetTarget();

                // 根据目标类型确定 AI 目标类型
                if (targetType == TARGET_UNIT_TARGET_ENEMY
                    || targetType == TARGET_DEST_TARGET_ENEMY)
                    UPDATE_TARGET(AITARGET_VICTIM)
                else if (targetType == TARGET_UNIT_DEST_AREA_ENEMY)
                    UPDATE_TARGET(AITARGET_ENEMY)

                // 处理光环效果
                if (effect.Effect == SPELL_EFFECT_APPLY_AURA)
                {
                    if (targetType == TARGET_UNIT_TARGET_ENEMY)
                        UPDATE_TARGET(AITARGET_DEBUFF)
                    else if (spellInfo->IsPositive())
                        UPDATE_TARGET(AITARGET_BUFF)
                }
            }
        }
        // 设置实际冷却时间和最大射程（实际使用的射程为法术最大射程的 3/4）
        AIInfo->realCooldown = spellInfo->RecoveryTime + spellInfo->StartRecoveryTime;
        AIInfo->maxRange = spellInfo->GetMaxRange(false) * 3 / 4;
    }
}

/**
 * @brief 完成目标选择的最终步骤
 *
 * 从已筛选的目标列表中根据选择方法返回最终选中的目标。
 *
 * @param targetList 已筛选的目标列表
 * @param targetType 目标选择方法
 * @return Unit* 最终选中的目标，如果列表为空则返回 nullptr
 *
 * @note 对于不同的选择方法：
 *       - MaxThreat/MinThreat/MaxDistance/MinDistance: 返回列表第一个元素
 *       - Random: 从列表中随机选择一个元素
 */
Unit* UnitAI::FinalizeTargetSelection(std::list<Unit*>& targetList, SelectTargetMethod targetType)
{
    // 可能没有任何目标符合谓词条件
    if (targetList.empty())
        return nullptr;

    switch (targetType)
    {
        case SelectTargetMethod::MaxThreat:
        case SelectTargetMethod::MinThreat:
        case SelectTargetMethod::MaxDistance:
        case SelectTargetMethod::MinDistance:
            return targetList.front();
        case SelectTargetMethod::Random:
            return Trinity::Containers::SelectRandomContainerElement(targetList);
        default:
            break;
    }

    return nullptr;
}

/**
 * @brief 准备目标列表选择的初始步骤
 *
 * 从威胁管理器中获取所有可能的目标，并根据选择方法和偏移量进行初步筛选。
 *
 * @param[out] targetList 用于存储候选目标的列表
 * @param targetType 目标选择方法
 * @param offset 要跳过的目标数量
 * @return bool 如果有足够的目标可供选择返回 true，否则返回 false
 *
 * @note 该函数的执行流程：
 *       1. 清空目标列表
 *       2. 检查威胁列表大小是否足够
 *       3. 根据选择方法从威胁管理器获取目标
 *       4. 对目标列表进行排序（如果需要）
 *       5. 跳过前 offset 个目标
 *
 * @see ThreatManager
 * @see ThreatReference
 */
bool UnitAI::PrepareTargetListSelection(std::list<Unit*>& targetList, SelectTargetMethod targetType, uint32 offset)
{
    targetList.clear();
    ThreatManager& mgr = me->GetThreatManager();
    // 快捷方式：我们要忽略前 <offset> 个元素，而总共只有 <offset> 个元素，所以全部忽略 - 无事可做
    if (mgr.GetThreatListSize() <= offset)
        return false;

    // 根据目标类型从威胁管理器获取目标
    if (targetType == SelectTargetMethod::MaxDistance || targetType == SelectTargetMethod::MinDistance)
    {
        // 对于距离类型，使用未排序的威胁列表
        for (ThreatReference const* ref : mgr.GetUnsortedThreatList())
        {
            if (ref->IsOffline())
                continue;

            targetList.push_back(ref->GetVictim());
        }
    }
    else
    {
        // 对于威胁类型，使用排序的威胁列表，当前受害者放在第一位
        Unit* currentVictim = mgr.GetCurrentVictim();
        if (currentVictim)
            targetList.push_back(currentVictim);

        for (ThreatReference const* ref : mgr.GetSortedThreatList())
        {
            if (ref->IsOffline())
                continue;

            Unit* thisTarget = ref->GetVictim();
            if (thisTarget != currentVictim)
                targetList.push_back(thisTarget);
        }
    }

    // 快捷方式：列表不会再变大了
    if (targetList.size() <= offset)
    {
        targetList.clear();
        return false;
    }

    // 对于 DISTANCE 类型，列表当前未排序 - 按 SelectTargetMethod::MaxDistance 重新排序
    if (targetType == SelectTargetMethod::MaxDistance || targetType == SelectTargetMethod::MinDistance)
        targetList.sort(Trinity::ObjectDistanceOrderPred(me, targetType == SelectTargetMethod::MinDistance));

    // 现在列表是 MAX 排序的，对于 MIN 类型需要反转
    if (targetType == SelectTargetMethod::MinThreat)
        targetList.reverse();

    // 忽略前 <offset> 个元素
    while (offset)
    {
        targetList.pop_front();
        --offset;
    }

    return true;
}

/**
 * @brief 完成目标列表选择的最终步骤
 *
 * 对已筛选的目标列表进行最终处理，确保列表包含指定数量的目标。
 *
 * @param[in,out] targetList 目标列表，函数会修改其大小
 * @param num 需要的目标数量
 * @param targetType 目标选择方法
 *
 * @note 对于随机选择方法，会随机保留指定数量的目标；
 *       对于其他方法，会保留列表前 num 个目标
 */
void UnitAI::FinalizeTargetListSelection(std::list<Unit*>& targetList, uint32 num, SelectTargetMethod targetType)
{
    if (targetList.size() <= num)
        return;

    if (targetType == SelectTargetMethod::Random)
        Trinity::Containers::RandomResize(targetList, num);
    else
        targetList.resize(num);
}

/**
 * @brief 获取调试信息
 *
 * 生成包含 AI 基本信息的调试字符串，用于日志记录和调试。
 *
 * @return std::string 包含 AI 调试信息的字符串
 *
 * @note 返回的信息包括：
 *       - me: 单位的基本调试信息
 */
std::string UnitAI::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << std::boolalpha
         << "Me: " << (me ? me->GetDebugInfo() : "NULL");
    return sstr.str();
}

/**
 * @brief 默认目标选择器构造函数
 *
 * 创建一个默认的目标选择器，用于根据多种条件筛选目标。
 *
 * @param unit 选择器的拥有者单位
 * @param dist 距离限制，正数表示在该距离内，负数表示在该距离外，0 表示不限制
 * @param playerOnly 是否只选择玩家
 * @param withTank 是否包含坦克（当前受害者）
 * @param aura 光环筛选条件，正数表示必须具有该光环，负数表示不能具有该光环
 *
 * @note 如果 withTank 为 false，坦克会被设置为异常目标并被排除
 */
DefaultTargetSelector::DefaultTargetSelector(Unit const* unit, float dist, bool playerOnly, bool withTank, int32 aura)
    : _me(unit), _dist(dist), _playerOnly(playerOnly), _exception(!withTank ? unit->GetThreatManager().GetLastVictim() : nullptr), _aura(aura)
{
}

/**
 * @brief 默认目标选择器的筛选操作符
 *
 * 判断指定目标是否符合选择器的筛选条件。
 *
 * @param target 要检查的目标单位
 * @return bool 如果目标符合所有条件返回 true，否则返回 false
 *
 * @note 检查的条件包括：
 *       1. 选择器和目标都不为 nullptr
 *       2. 目标不是异常目标（通常是坦克）
 *       3. 如果指定只选玩家，目标必须是玩家
 *       4. 距离检查
 *       5. 光环检查
 */
bool DefaultTargetSelector::operator()(Unit const* target) const
{
    if (!_me)
        return false;

    if (!target)
        return false;

    // 排除异常目标（通常是坦克）
    if (_exception && target == _exception)
        return false;

    // 检查是否只选择玩家
    if (_playerOnly && (target->GetTypeId() != TYPEID_PLAYER))
        return false;

    // 检查距离条件
    if (_dist > 0.0f && !_me->IsWithinCombatRange(target, _dist))
        return false;

    if (_dist < 0.0f && _me->IsWithinCombatRange(target, -_dist))
        return false;

    // 检查光环条件
    if (_aura)
    {
        if (_aura > 0)
        {
            // 正数：目标必须具有该光环
            if (!target->HasAura(_aura))
                return false;
        }
        else
        {
            // 负数：目标不能具有该光环
            if (target->HasAura(-_aura))
                return false;
        }
    }

    return true;
}

/**
 * @brief 法术目标选择器构造函数
 *
 * 创建一个用于法术目标筛选的选择器。
 *
 * @param caster 施法者单位
 * @param spellId 法术 ID
 *
 * @note 该选择器会根据法术信息自动处理难度调整
 */
SpellTargetSelector::SpellTargetSelector(Unit* caster, uint32 spellId) :
    _caster(caster), _spellInfo(sSpellMgr->GetSpellForDifficultyFromSpell(sSpellMgr->GetSpellInfo(spellId), caster))
{
    ASSERT(_spellInfo);
}

/**
 * @brief 法术目标选择器的筛选操作符
 *
 * 检查目标是否可以作为法术的有效目标，包括目标类型检查和距离检查。
 *
 * @param target 要检查的目标单位
 * @return bool 如果目标是有效的法术目标返回 true，否则返回 false
 *
 * @note 该函数的检查包括：
 *       1. 目标有效性检查
 *       2. 法术目标类型检查（CheckTarget）
 *       3. 法术射程检查（包括最小和最大射程）
 *       4. 移动状态对射程的影响
 *
 * @see SpellInfo::CheckTarget()
 */
bool SpellTargetSelector::operator()(Unit const* target) const
{
    if (!target || _spellInfo->CheckTarget(_caster, target) != SPELL_CAST_OK)
        return false;

    // 复制自 Spell::CheckRange 的距离检查逻辑
    float minRange = 0.0f;
    float maxRange = 0.0f;
    float rangeMod = 0.0f;
    if (_spellInfo->RangeEntry)
    {
        if (_spellInfo->RangeEntry->Flags & SPELL_RANGE_MELEE)
        {
            // 近战范围计算
            rangeMod = _caster->GetCombatReach() + 4.0f / 3.0f;
            rangeMod += target->GetCombatReach();

            rangeMod = std::max(rangeMod, NOMINAL_MELEE_RANGE);
        }
        else
        {
            float meleeRange = 0.0f;
            if (_spellInfo->RangeEntry->Flags & SPELL_RANGE_RANGED)
            {
                // 远程范围计算
                meleeRange = _caster->GetCombatReach() + 4.0f / 3.0f;
                meleeRange += target->GetCombatReach();

                meleeRange = std::max(meleeRange, NOMINAL_MELEE_RANGE);
            }

            minRange = _caster->GetSpellMinRangeForTarget(target, _spellInfo) + meleeRange;
            maxRange = _caster->GetSpellMaxRangeForTarget(target, _spellInfo);

            rangeMod = _caster->GetCombatReach();
            rangeMod += target->GetCombatReach();

            if (minRange > 0.0f && !(_spellInfo->RangeEntry->Flags & SPELL_RANGE_RANGED))
                minRange += rangeMod;
        }

        // 如果施法者和目标都在移动（非行走状态），增加射程修正
        if (_caster->isMoving() && target->isMoving() && !_caster->IsWalking() && !target->IsWalking() &&
            (_spellInfo->RangeEntry->Flags & SPELL_RANGE_MELEE || target->GetTypeId() == TYPEID_PLAYER))
            rangeMod += 8.0f / 3.0f;
    }

    maxRange += rangeMod;

    // 使用平方距离进行比较（避免开方运算）
    minRange *= minRange;
    maxRange *= maxRange;

    if (target != _caster)
    {
        // 检查最大距离
        if (_caster->GetExactDistSq(target) > maxRange)
            return false;

        // 检查最小距离
        if (minRange > 0.0f && _caster->GetExactDistSq(target) < minRange)
            return false;
    }

    return true;
}

/**
 * @brief 非坦克目标选择器的筛选操作符
 *
 * 筛选出非当前坦克（非最高威胁目标）的目标。
 * 通常用于选择非主要仇恨目标进行特殊技能施放。
 *
 * @param target 要检查的目标单位
 * @return bool 如果目标不是坦克且符合条件返回 true，否则返回 false
 *
 * @note 该选择器的用途：
 *       - 选择副坦克或 DPS 目标
 *       - 施放需要切换目标的技能
 *       - 避免对主目标施放的技能
 *
 * @see ThreatManager::GetCurrentVictim()
 */
bool NonTankTargetSelector::operator()(Unit const* target) const
{
    if (!target)
        return false;

    // 检查是否只选择玩家
    if (_playerOnly && target->GetTypeId() != TYPEID_PLAYER)
        return false;

    // 获取当前坦克（最高威胁目标）
    if (Unit* currentVictim = _source->GetThreatManager().GetCurrentVictim())
        return target != currentVictim;

    // 如果威胁管理器中没有当前受害者，则使用单位的当前受害者
    return target != _source->GetVictim();
}

/**
 * @brief 能量类型用户选择器的筛选操作符
 *
 * 筛选出使用特定能量类型的目标（如法力、怒气、能量等）。
 *
 * @param target 要检查的目标单位
 * @return bool 如果目标使用指定的能量类型且符合其他条件返回 true，否则返回 false
 *
 * @note 该选择器常用于：
 *       - 选择法力用户进行法力燃烧
 *       - 选择怒气用户进行特殊技能
 *       - 能量窃取技能
 *
 * @see Powers
 * @see Unit::GetPowerType()
 */
bool PowerUsersSelector::operator()(Unit const* target) const
{
    if (!_me || !target)
        return false;

    // 检查能量类型是否匹配
    if (target->GetPowerType() != _power)
        return false;

    // 检查是否只选择玩家
    if (_playerOnly && target->GetTypeId() != TYPEID_PLAYER)
        return false;

    // 检查距离条件
    if (_dist > 0.0f && !_me->IsWithinCombatRange(target, _dist))
        return false;

    if (_dist < 0.0f && _me->IsWithinCombatRange(target, -_dist))
        return false;

    return true;
}

/**
 * @brief 最远目标选择器的筛选操作符
 *
 * 筛选出符合条件的目标，通常用于在所有符合条件的目标中选择最远的一个。
 *
 * @param target 要检查的目标单位
 * @return bool 如果目标符合基本条件返回 true，否则返回 false
 *
 * @note 该选择器的筛选条件：
 *       1. 目标和选择器拥有者都不为 nullptr
 *       2. 如果指定只选玩家，目标必须是玩家
 *       3. 距离检查（在指定距离内）
 *       4. 视线检查（如果启用）
 *
 * @warning 该选择器只进行基本筛选，实际的"最远"选择需要在筛选后的列表中进行排序
 *
 * @see Unit::IsWithinLOSInMap()
 */
bool FarthestTargetSelector::operator()(Unit const* target) const
{
    if (!_me || !target)
        return false;

    // 检查是否只选择玩家
    if (_playerOnly && target->GetTypeId() != TYPEID_PLAYER)
        return false;

    // 检查是否在指定距离内
    if (_dist > 0.0f && !_me->IsWithinCombatRange(target, _dist))
        return false;

    // 检查视线（如果启用）
    if (_inLos && !_me->IsWithinLOSInMap(target))
        return false;

    return true;
}
