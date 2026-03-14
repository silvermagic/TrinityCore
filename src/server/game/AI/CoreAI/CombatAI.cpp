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
 * @file CombatAI.cpp
 *
 * @brief 战斗AI模块实现
 *
 * 本文件实现了CombatAI.h中定义的所有AI类的具体逻辑：
 * - AggressorAI: 简单的近战攻击行为
 * - CombatAI: 法术施放、事件调度和战斗管理
 * - CasterAI: 远程法术攻击和距离控制
 * - ArcherAI: 远程/近战切换的混合攻击
 * - TurretAI: 固定位置的远程攻击
 * - VehicleAI: 载具的条件检查和解散逻辑
 *
 * 每个AI类都实现了特定的战斗模式，为游戏中的生物提供了丰富的行为选择。
 */

#include "CombatAI.h"
#include "ConditionMgr.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Vehicle.h"

/////////////////
// AggressorAI
/////////////////

/**
 * @brief 判断AggressorAI是否适用于指定生物
 *
 * @param creature 要检查的生物
 * @return int32 返回AI适用性等级
 *         - PERMIT_BASE_REACTIVE: 该生物具有敌对阵营，可以使用AggressorAI
 *         - PERMIT_BASE_NO: 该生物是平民或对所有阵营中立，不适用此AI
 *
 * @note 此函数在选择AI类型时被调用，用于确定是否应该使用AggressorAI
 *       AggressorAI适用于那些有敌对阵营的生物
 */
int32 AggressorAI::Permissible(Creature const* creature)
{
    // have some hostile factions, it will be selected by IsHostileTo check at MoveInLineOfSight
    // 如果生物不是平民且不是对所有人中立，则可以使用此AI
    // 实际的敌对判断将在MoveInLineOfSight中通过IsHostileTo检查完成
    if (!creature->IsCivilian() && !creature->IsNeutralToAll())
        return PERMIT_BASE_REACTIVE;

    return PERMIT_BASE_NO;
}

/**
 * @brief AggressorAI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有当前目标（UpdateVictim）
 *          2. 如果有目标，执行近战攻击
 *
 * @note AggressorAI是一个简单的攻击型AI，只执行基本的近战攻击
 *       不包含任何法术或特殊技能逻辑
 */
void AggressorAI::UpdateAI(uint32 /*diff*/)
{
    if (!UpdateVictim())
        return;

    DoMeleeAttackIfReady();
}

/////////////////
// CombatAI
/////////////////

/**
 * @brief 初始化CombatAI，加载生物的法术技能
 *
 * @param void
 * @return void
 *
 * @details 主要流程：
 *          1. 遍历生物的法术列表（m_spells）
 *          2. 验证每个法术ID是否有效
 *          3. 将有效法术添加到_spells列表中
 *          4. 调用父类的InitializeAI完成其他初始化
 *
 * @note 生物的法术列表来自CreatureTemplate，在数据库中定义
 *       只有有效的法术才会被加入AI的法术列表
 */
void CombatAI::InitializeAI()
{
    for (uint32 spell : me->m_spells)
        if (spell && sSpellMgr->GetSpellInfo(spell))
            _spells.push_back(spell);

    CreatureAI::InitializeAI();
}

/**
 * @brief 重置CombatAI状态
 *
 * @param void
 * @return void
 *
 * @details 重置事件调度器，清除所有计划中的事件
 *
 * @note 在生物脱离战斗、重生等情况下调用
 */
void CombatAI::Reset()
{
    _events.Reset();
}

/**
 * @brief 生物死亡时的处理函数
 *
 * @param killer 击杀者（可能是玩家或生物）
 * @return void
 *
 * @details 主要流程：
 *          1. 遍历AI的所有法术
 *          2. 检查每个法术的条件是否为AICOND_DIE（死亡触发）
 *          3. 对击杀者施放符合条件的法术
 *
 * @note 死亡触发的法术通常用于：死亡效果、掉落物品、触发事件等
 */
void CombatAI::JustDied(Unit* killer)
{
    for (uint32 spell : _spells)
    {
        if (AISpellInfo[spell].condition == AICOND_DIE)
            me->CastSpell(killer, spell, true);
    }
}

/**
 * @brief 进入战斗时的处理函数
 *
 * @param who 战斗目标
 * @return void
 *
 * @details 主要流程：
 *          1. 遍历AI的所有法术
 *          2. 对于AICOND_AGGRO（仇恨触发）的法术，立即对目标施放
 *          3. 对于AICOND_COMBAT（战斗中施放）的法术，安排到事件调度器中
 *             - 初始冷却时间为：基础冷却 + 随机延迟（0到基础冷却之间）
 *
 * @note 进入战斗时的初始行为，如战斗开始的喊话、初始增益等
 *       AICOND_COMBAT法术会被安排周期性施放
 */
void CombatAI::JustEngagedWith(Unit* who)
{
    for (uint32 spell : _spells)
    {
        if (AISpellInfo[spell].condition == AICOND_AGGRO)
            me->CastSpell(who, spell, false);
        else if (AISpellInfo[spell].condition == AICOND_COMBAT)
            _events.ScheduleEvent(spell, Milliseconds(AISpellInfo[spell].cooldown + rand32() % AISpellInfo[spell].cooldown));
    }
}

/**
 * @brief CombatAI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有当前目标（UpdateVictim），没有则返回
 *          2. 更新事件调度器
 *          3. 检查生物是否正在施法，如果是则等待施法完成
 *          4. 尝试执行计划中的法术事件
 *             - 如果有法术事件，施放法术并重新安排下次施放时间
 *          5. 如果没有法术事件需要处理，执行近战攻击
 *
 * @note 这是CombatAI的核心逻辑，平衡法术施放和近战攻击
 *       法术优先级高于近战攻击
 */
void CombatAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    _events.Update(diff);

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    if (uint32 spellId = _events.ExecuteEvent())
    {
        DoCast(spellId);
        _events.ScheduleEvent(spellId, Milliseconds(AISpellInfo[spellId].cooldown + rand32() % AISpellInfo[spellId].cooldown));
    }
    else
        DoMeleeAttackIfReady();
}

/**
 * @brief 法术被打断时的处理函数
 *
 * @param spellId 被打断的法术ID
 * @param unTimeMs 重新施放前的延迟时间（毫秒）
 * @return void
 *
 * @details 当法术被打断时，重新安排该法术的施放时间
 *          使用RescheduleEvent而非ScheduleEvent，确保不会重复安排
 *
 * @note 用于处理玩家打断生物施法的情况
 *       生物会在指定时间后再次尝试施放该法术
 */
void CombatAI::SpellInterrupted(uint32 spellId, uint32 unTimeMs)
{
    _events.RescheduleEvent(spellId, Milliseconds(unTimeMs));
}

/////////////////
// CasterAI
/////////////////

/**
 * @brief 初始化施法者AI，设置攻击距离
 *
 * @param void
 * @return void
 *
 * @details 主要流程：
 *          1. 调用父类CombatAI的初始化
 *          2. 设置默认攻击距离为30码
 *          3. 遍历所有战斗法术，找到最小射程
 *          4. 如果所有法术射程都是30码，则设置为近战距离
 *
 * @note 施法者AI会根据法术射程调整攻击距离
 *       确保生物能够在最远射程处施法，而不是进入近战范围
 */
void CasterAI::InitializeAI()
{
    CombatAI::InitializeAI();

    _attackDistance = 30.0f;

    for (uint32 spell : _spells)
    {
        if (AISpellInfo[spell].condition == AICOND_COMBAT && _attackDistance > GetAISpellInfo(spell)->maxRange)
            _attackDistance = GetAISpellInfo(spell)->maxRange;
    }

    if (_attackDistance == 30.0f)
        _attackDistance = MELEE_RANGE;
}

/**
 * @brief 施法者进入战斗时的处理函数
 *
 * @param who 战斗目标
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有法术，没有则直接返回
 *          2. 随机选择一个法术作为首发法术
 *          3. 遍历所有法术：
 *             - AICOND_AGGRO法术：立即施放
 *             - AICOND_COMBAT法术：安排到事件调度器
 *               - 首发法术会立即施放并加上施法时间
 *               - 其他法术按照冷却时间安排
 *
 * @note 与CombatAI的区别：
 *       - CasterAI会随机选择一个首发法术立即施放
 *       - 首发法术的冷却时间包含施法时间
 */
void CasterAI::JustEngagedWith(Unit* who)
{
    if (_spells.empty())
        return;

    uint32 spell = rand32() % _spells.size();
    uint32 count = 0;
    for (auto itr = _spells.begin(); itr != _spells.end(); ++itr, ++count)
    {
        if (AISpellInfo[*itr].condition == AICOND_AGGRO)
            me->CastSpell(who, *itr, false);
        else if (AISpellInfo[*itr].condition == AICOND_COMBAT)
        {
            uint32 cooldown = GetAISpellInfo(*itr)->realCooldown;
            if (count == spell)
            {
                DoCast(_spells[spell]);
                cooldown += me->GetCurrentSpellCastTime(*itr);
            }
            _events.ScheduleEvent(*itr, Milliseconds(cooldown));
        }
    }
}

/**
 * @brief 施法者AI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有当前目标，没有则返回
 *          2. 更新事件调度器
 *          3. 检查目标是否有可被伤害打破的控制效果光环
 *             - 如果有，中断所有非近战法术，避免打破控制
 *          4. 检查是否正在施法，是则返回
 *          5. 执行计划中的法术事件
 *             - 施放法术后，安排下次施放时间
 *             - 冷却时间 = 施法时间（如果有）或500ms + 实际冷却时间
 *
 * @note 与CombatAI的区别：
 *       - 增加了控制效果检查，避免意外打破控制技能
 *       - 冷却时间计算包含施法时间，更精确
 *       - 不执行近战攻击（施法者保持距离）
 */
void CasterAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    _events.Update(diff);

    if (me->GetVictim() && me->EnsureVictim()->HasBreakableByDamageCrowdControlAura(me))
    {
        me->InterruptNonMeleeSpells(false);
        return;
    }

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    if (uint32 spellId = _events.ExecuteEvent())
    {
        DoCast(spellId);
        uint32 casttime = me->GetCurrentSpellCastTime(spellId);
        _events.ScheduleEvent(spellId, (casttime ? Milliseconds(casttime) : 500ms) + Milliseconds(GetAISpellInfo(spellId)->realCooldown));
    }
}

//////////////
// ArcherAI
//////////////

/**
 * @brief 弓箭手AI的构造函数
 *
 * @param creature 要控制生物
 *
 * @details 主要流程：
 *          1. 检查生物是否有法术0，没有则记录错误日志
 *          2. 获取法术的最小射程，如果没有则使用近战距离
 *          3. 设置生物的战斗距离和视野距离为法术最大射程
 *
 * @note ArcherAI适用于远程物理攻击生物（如弓箭手）
 *       法术0是主要攻击技能，必须在数据库中设置
 */
ArcherAI::ArcherAI(Creature* creature) : CreatureAI(creature)
{
    if (!creature->m_spells[0])
        TC_LOG_ERROR("scripts.ai", "ArcherAI set for creature with spell1 = 0. AI will do nothing ({})", creature->GetGUID().ToString());

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(creature->m_spells[0]);
    _minimumRange = spellInfo ? spellInfo->GetMinRange(false) : 0;

    if (!_minimumRange)
        _minimumRange = MELEE_RANGE;
    creature->m_CombatDistance = spellInfo ? spellInfo->GetMaxRange(false) : 0;
    creature->m_SightDistance = creature->m_CombatDistance;
}

/**
 * @brief 弓箭手开始攻击的处理函数
 *
 * @param who 攻击目标
 * @return void
 *
 * @details 主要流程：
 *          1. 检查目标是否有效
 *          2. 判断目标是否在最小射程内：
 *             - 在最小射程内：使用近战攻击（melee attack = true），追击目标
 *             - 不在最小射程内：使用远程攻击（melee attack = false），保持战斗距离追击
 *          3. 如果目标是飞行状态，停止移动
 *
 * @note 此函数决定了弓箭手的战斗行为：
 *       - 近距离切换为近战攻击
 *       - 远距离使用法术攻击
 *       - 飞行目标无法追击
 */
void ArcherAI::AttackStart(Unit* who)
{
    if (!who)
        return;

    if (me->IsWithinCombatRange(who, _minimumRange))
    {
        // 目标在最小射程内，切换为近战攻击
        if (me->Attack(who, true) && !who->IsFlying())
            me->GetMotionMaster()->MoveChase(who);
    }
    else
    {
        // 目标在射程外，使用远程攻击并保持距离
        if (me->Attack(who, false) && !who->IsFlying())
            me->GetMotionMaster()->MoveChase(who, me->m_CombatDistance);
    }

    if (who->IsFlying())
        me->GetMotionMaster()->MoveIdle();
}

/**
 * @brief 弓箭手AI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有当前目标，没有则返回
 *          2. 判断目标是否在最小射程外：
 *             - 是：使用法术攻击（远程）
 *             - 否：使用近战攻击
 *
 * @note 弓箭手会根据距离自动切换攻击方式
 *       与CasterAI不同，弓箭手在近距离会使用近战攻击
 */
void ArcherAI::UpdateAI(uint32 /*diff*/)
{
    if (!UpdateVictim())
        return;

    if (!me->IsWithinCombatRange(me->GetVictim(), _minimumRange))
        DoSpellAttackIfReady(me->m_spells[0]);
    else
        DoMeleeAttackIfReady();
}

//////////////
// TurretAI
//////////////

/**
 * @brief 炮塔AI的构造函数
 *
 * @param creature 要控制的生物
 *
 * @details 主要流程：
 *          1. 检查生物是否有法术0，没有则记录错误日志
 *          2. 获取法术的最小射程
 *          3. 设置生物的战斗距离和视野距离为法术最大射程
 *
 * @note TurretAI适用于固定位置的远程攻击单位（如炮塔、固定防御设施）
 *       炮塔不会移动，只能在固定位置攻击
 */
TurretAI::TurretAI(Creature* creature) : CreatureAI(creature)
{
    if (!creature->m_spells[0])
        TC_LOG_ERROR("scripts.ai", "TurretAI set for creature with spell1 = 0. AI will do nothing ({})", creature->GetGUID().ToString());

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(creature->m_spells[0]);
    _minimumRange = spellInfo ? spellInfo->GetMinRange(false) : 0;
    creature->m_CombatDistance = spellInfo ? spellInfo->GetMaxRange(false) : 0;
    creature->m_SightDistance = creature->m_CombatDistance;
}

/**
 * @brief 判断炮塔是否可以攻击指定目标
 *
 * @param who 要检查的目标
 * @return bool true表示可以攻击，false表示不能攻击
 *
 * @details 主要流程：
 *          1. 检查目标是否在最大射程内
 *          2. 检查目标是否在最小射程外（如果有最小射程）
 *          3. 只有满足射程条件才返回true
 *
 * @note 炮塔只能攻击射程内的目标，不会追击
 *       这与普通生物的攻击行为不同
 */
bool TurretAI::CanAIAttack(Unit const* who) const
{
    /// @todo use one function to replace it
    // 目标必须在最大射程内，且（如果没有最小射程限制）不在最小射程内
    if (!me->IsWithinCombatRange(who, me->m_CombatDistance) || (_minimumRange && me->IsWithinCombatRange(who, _minimumRange)))
        return false;
    return true;
}

/**
 * @brief 炮塔开始攻击的处理函数
 *
 * @param who 攻击目标
 * @return void
 *
 * @details 主要流程：
 *          1. 检查目标是否有效
 *          2. 设置攻击目标，但不启用近战攻击（melee attack = false）
 *
 * @note 与ArcherAI的区别：
 *       - 炮塔不会追击目标，不会移动
 *       - 炮塔只使用远程法术攻击
 *       - 传入false参数表示不进入近战模式
 */
void TurretAI::AttackStart(Unit* who)
{
    if (who)
        me->Attack(who, false);
}

/**
 * @brief 炮塔AI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查是否有当前目标，没有则返回
 *          2. 使用法术攻击（远程）
 *
 * @note 炮塔只使用法术攻击，不会近战
 *       法术ID来自m_spells[0]
 */
void TurretAI::UpdateAI(uint32 /*diff*/)
{
    if (!UpdateVictim())
        return;

    DoSpellAttackIfReady(me->m_spells[0]);
}

//////////////
// VehicleAI
//////////////

/**
 * @brief 载具AI的构造函数
 *
 * @param creature 要控制的生物
 *
 * @details 主要流程：
 *          1. 初始化成员变量
 *          2. 加载载具条件（用于判断乘客是否满足使用条件）
 *          3. 设置解散状态为false
 *          4. 设置解散计时器
 *
 * @note VehicleAI用于控制载具类型生物（如坐骑、载具、炮台等）
 *       载具AI会检查乘客是否满足使用条件，不满足则踢出乘客
 */
VehicleAI::VehicleAI(Creature* creature) : CreatureAI(creature), _hasConditions(false), _conditionsTimer(VEHICLE_CONDITION_CHECK_TIME)
{
    LoadConditions();
    _dismiss = false;
    _dismissTimer = VEHICLE_DISMISS_TIME;
}

/**
 * @brief 载具AI的主更新函数
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查载具条件（踢出不满足条件的乘客）
 *          2. 如果处于解散状态：
 *             - 检查解散计时器
 *             - 计时器到期后，解散或消失载具
 *             - 否则减少计时器
 *
 * @note 载具AI即使在载具被骑乘时也会运行
 *       这是与其他AI的区别，因为载具需要持续检查条件
 */
// NOTE: VehicleAI::UpdateAI runs even while the vehicle is mounted
void VehicleAI::UpdateAI(uint32 diff)
{
    CheckConditions(diff);

    if (_dismiss)
    {
        if (_dismissTimer < diff)
        {
            _dismiss = false;
            me->DespawnOrUnsummon();
        }
        else
            _dismissTimer -= diff;
    }
}

/**
 * @brief 载具被魅惑（骑乘）状态改变时的处理函数
 *
 * @param isNew 是否是新魅惑状态（未使用）
 * @return void
 *
 * @details 主要流程：
 *          1. 检查载具是否被魅惑
 *          2. 如果载具未被使用且不再被魅惑，但有条件限制：
 *             - 设置解散标志，准备解散载具
 *          3. 如果载具被魅惑：
 *             - 取消解散标志，载具正在使用
 *          4. 重置解散计时器
 *
 * @note 此函数处理载具的使用/停止使用状态转换
 *       当玩家下马时，如果载具有条件限制，会触发解散流程
 */
void VehicleAI::OnCharmed(bool /*isNew*/)
{
    bool const charmed = me->IsCharmed();
    if (!me->GetVehicleKit()->IsVehicleInUse() && !charmed && _hasConditions) // was used and has conditions
    {
        _dismiss = true; // needs reset
    }
    else if (charmed)
        _dismiss = false; // in use again

    _dismissTimer = VEHICLE_DISMISS_TIME; // reset timer
}

/**
 * @brief 加载载具的条件配置
 *
 * @param void
 * @return void
 *
 * @details 检查是否有针对该载具模板的条件配置
 *          条件用于限制谁能使用该载具
 *
 * @note 条件从ConditionMgr加载，存储在数据库中
 *       例如：需要特定成就、任务、物品等才能使用的载具
 */
void VehicleAI::LoadConditions()
{
    _hasConditions = sConditionMgr->HasConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE, me->GetEntry());
}

/**
 * @brief 检查载具乘客是否满足使用条件
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 * @return void
 *
 * @details 主要流程：
 *          1. 如果没有条件限制，直接返回
 *          2. 检查计时器是否到期：
 *             - 未到期：减少计时器
 *             - 到期：执行检查
 *               a. 遍历载具的所有座位
 *               b. 检查每个乘客是否为玩家
 *               c. 验证玩家是否满足条件
 *               d. 不满足条件的玩家被踢出载具
 *               e. 重置计时器
 *
 * @note 条件检查是周期性的，不是每帧检查，以优化性能
 *       每次只踢出一个乘客，下次检查再踢出下一个
 */
void VehicleAI::CheckConditions(uint32 diff)
{
    if (!_hasConditions)
        return;

    if (_conditionsTimer <= diff)
    {
        if (Vehicle* vehicleKit = me->GetVehicleKit())
        {
            for (auto const& [i, vehicleSeat] : vehicleKit->Seats)
            {
                if (Unit* passenger = ObjectAccessor::GetUnit(*me, vehicleSeat.Passenger.Guid))
                {
                    if (Player* player = passenger->ToPlayer())
                    {
                        if (!sConditionMgr->IsObjectMeetingNotGroupedConditions(CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE, me->GetEntry(), player, me))
                        {
                            player->ExitVehicle();
                            return; // check other pessanger in next tick
                        }
                    }
                }
            }
        }

        _conditionsTimer = VEHICLE_CONDITION_CHECK_TIME;
    }
    else
        _conditionsTimer -= diff;
}

/**
 * @brief 判断VehicleAI是否适用于指定生物
 *
 * @param creature 要检查的生物
 * @return int32 返回AI适用性等级
 *         - PERMIT_BASE_SPECIAL: 该生物是载具，可以使用VehicleAI
 *         - PERMIT_BASE_NO: 该生物不是载具，不适用此AI
 *
 * @note 此函数在选择AI类型时被调用，用于确定是否应该使用VehicleAI
 */
int32 VehicleAI::Permissible(Creature const* creature)
{
    if (creature->IsVehicle())
        return PERMIT_BASE_SPECIAL;

    return PERMIT_BASE_NO;
}
