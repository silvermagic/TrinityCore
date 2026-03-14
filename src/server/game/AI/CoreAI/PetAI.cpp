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
 * @file PetAI.cpp
 *
 * @brief 宠物AI模块实现
 *
 * 本文件实现了PetAI类的具体功能，是宠物系统的核心AI逻辑：
 *
 * 核心机制：
 * - 目标选择：根据命令状态和反应模式智能选择攻击目标
 * - 移动控制：跟随、停留、追击等不同移动模式
 * - 自动施法：根据法术类型（有益/有害）自动选择目标施放
 * - 状态管理：管理宠物的命令状态、反应状态和位置状态
 *
 * 特殊处理：
 * - 控制技能保护：避免打破控制效果
 * - 主人交互：响应主人的攻击和被攻击事件
 * - 距离限制：防止宠物离主人太远而消失
 * - 表情互动：主人对宠物的表情会产生反应（如食尸鬼）
 *
 * 性能优化：
 * - 友方列表定期更新（10秒一次）
 * - 状态检查优化，避免不必要的操作
 */

#include "PetAI.h"
#include "AIException.h"
#include "Creature.h"
#include "Errors.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "Spell.h"
#include "SpellHistory.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Util.h"

/**
 * @brief 判断生物是否可以使用PetAI
 *
 * 职责：静态函数，用于判断给定的生物是否适合使用宠物AI
 *
 * @param creature 要检查的生物常量指针
 *
 * @return int32 返回AI许可等级：
 *               - PERMIT_BASE_PROACTIVE: 玩家拥有的可控守护者（主动AI）
 *               - PERMIT_BASE_REACTIVE: 非玩家拥有的可控守护者（反应AI）
 *               - PERMIT_BASE_NO: 不适合使用PetAI
 *
 * 主要流程：
 * 1. 检查生物是否为可控守护者类型
 * 2. 如果是，进一步检查所有者是否为玩家
 * 3. 根据所有者类型返回相应的许可等级
 */
int32 PetAI::Permissible(Creature const* creature)
{
    if (creature->HasUnitTypeMask(UNIT_MASK_CONTROLABLE_GUARDIAN))
    {
        if (reinterpret_cast<Guardian const*>(creature)->GetOwner()->GetTypeId() == TYPEID_PLAYER)
            return PERMIT_BASE_PROACTIVE;
        return PERMIT_BASE_REACTIVE;
    }

    return PERMIT_BASE_NO;
}

/**
 * @brief PetAI构造函数
 *
 * 职责：初始化宠物AI实例
 *
 * @param creature 要为其创建AI的生物指针
 *
 * 主要流程：
 * 1. 调用父类CreatureAI的构造函数
 * 2. 初始化追踪器（用于视野检测）
 * 3. 验证生物是否具有有效的魅力信息（CharmInfo）
 * 4. 如果没有魅力信息，抛出无效AI异常
 * 5. 初始化盟友列表
 */
PetAI::PetAI(Creature* creature) : CreatureAI(creature), _tracker(TIME_INTERVAL_LOOK)
{
    if (!me->GetCharmInfo())
        throw InvalidAIException("Creature doesn't have a valid charm info");

    UpdateAllies();
}

/**
 * @brief 宠物AI主更新函数
 *
 * 职责：每帧调用，负责宠物的战斗决策、目标选择、自动施法等核心逻辑
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 主要流程：
 * 1. 基础检查：宠物是否存活、是否有魅力信息
 * 2. 更新盟友列表（定时）
 * 3. 战斗状态处理：
 *    - 有目标时：检查控制技能、判断是否需要停止攻击、执行近战攻击
 *    - 无目标时：根据反应模式选择新目标或返回跟随
 * 4. 自动施法处理：遍历自动施法技能，寻找合适目标施放
 * 5. 更新移动速度
 */
void PetAI::UpdateAI(uint32 diff)
{
    if (!me->IsAlive() || !me->GetCharmInfo())
        return;

    Unit* owner = me->GetCharmerOrOwner();

    // 更新盟友列表定时器
    if (_updateAlliesTimer <= diff)
        // UpdateAllies self set update timer
        UpdateAllies();
    else
        _updateAlliesTimer -= diff;

    // 处理当前目标存在的情况
    if (me->GetVictim() && me->EnsureVictim()->IsAlive())
    {
        // 如果目标有会被伤害打破的控制光环，需要停止施法（但不能退出战斗）
        // 仅当没有引导法术时才中断（Pin、Seduction等引导技能例外）
        // is only necessary to stop casting, the pet must not exit combat
        if (!me->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && // ignore channeled spells (Pin, Seduction)
            me->EnsureVictim()->HasBreakableByDamageCrowdControlAura(me))
        {
            me->InterruptNonMeleeSpells(false);
            return;
        }

        // 检查是否需要停止攻击
        if (NeedToStop())
        {
            TC_LOG_TRACE("scripts.ai.petai", "PetAI::UpdateAI: AI stopped attacking {}", me->GetGUID().ToString());
            StopAttack();
            return;
        }

        // 根据命令状态决定是否进行近战攻击
        // Check before attacking to prevent pets from leaving stay position
        if (me->GetCharmInfo()->HasCommandState(COMMAND_STAY))
        {
            // 停留状态下：只有在收到攻击命令或在停留位置且目标在近战范围内才攻击
            if (me->GetCharmInfo()->IsCommandAttack() || (me->GetCharmInfo()->IsAtStay() && me->IsWithinMeleeRange(me->GetVictim())))
                DoMeleeAttackIfReady();
        }
        else
            DoMeleeAttackIfReady();
    }
    else
    {
        // 无目标时的处理：寻找新目标或返回跟随
        if (me->HasReactState(REACT_AGGRESSIVE) || me->GetCharmInfo()->IsAtStay())
        {
            // 仅在特定情况下需要检查目标：
            // Every update we need to check targets only in certain cases
            // Aggressive - Allow auto select if owner or pet don't have a target
            // Stay - Only pick from pet or owner targets / attackers so targets won't run by
            //   while chasing our owner. Don't do auto select.
            // All other cases (ie: defensive) - Targets are assigned by DamageTaken(), OwnerAttackedBy(), OwnerAttacked(), etc.
            Unit* nextTarget = SelectNextTarget(me->HasReactState(REACT_AGGRESSIVE));

            if (nextTarget)
                AttackStart(nextTarget);
            else
                HandleReturnMovement();
        }
        else
            HandleReturnMovement();
    }

    // 自动施法处理（仅在战斗中或某些持续性法术可随时施放）
    // Autocast (cast only in combat or persistent spells in any state)
    if (!me->HasUnitState(UNIT_STATE_CASTING))
    {
        TargetSpellList targetSpellStore;

        // 遍历所有自动施法技能槽位
        for (uint8 i = 0; i < me->GetPetAutoSpellSize(); ++i)
        {
            uint32 spellID = me->GetPetAutoSpellOnPos(i);
            if (!spellID)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
            if (!spellInfo)
                continue;

            // 检查全局冷却
            if (me->GetSpellHistory()->HasGlobalCooldown(spellInfo))
                continue;

            // 检查法术冷却
            // check spell cooldown
            if (!me->GetSpellHistory()->IsReady(spellInfo))
                continue;

            // 处理有益法术（增益、治疗等）
            if (spellInfo->IsPositive())
            {
                // 检查法术是否可在战斗中使用
                if (spellInfo->CanBeUsedInCombat())
                {
                    // 只有在战斗中或收到攻击命令时才施放
                    // Check if we're in combat or commanded to attack
                    if (!me->IsInCombat() && !me->GetCharmInfo()->IsCommandAttack())
                        continue;
                }

                Spell* spell = new Spell(me, spellInfo, TRIGGERED_NONE);
                bool spellUsed = false;

                // 某些法术可以以敌人或友方为目标（如死亡骑士食尸鬼的跳跃）
                // 优先检查敌人目标（先检查宠物，再检查主人）
                // Some spells can target enemy or friendly (DK Ghoul's Leap)
                // Check for enemy first (pet then owner)
                Unit* target = me->getAttackerForHelper();
                if (!target && owner)
                    target = owner->getAttackerForHelper();

                if (target)
                {
                    if (CanAttack(target) && spell->CanAutoCast(target))
                    {
                        targetSpellStore.push_back(std::make_pair(target, spell));
                        spellUsed = true;
                    }
                }

                // 如果是跳跃法术，宠物必须跳向目标
                if (spellInfo->HasEffect(SPELL_EFFECT_JUMP_DEST))
                {
                    if (!spellUsed)
                        delete spell;
                    continue; // Pets must only jump to target
                }

                // 没有敌人目标，检查友方目标
                // No enemy, check friendly
                if (!spellUsed)
                {
                    for (ObjectGuid target : _allySet)
                    {
                        Unit* ally = ObjectAccessor::GetUnit(*me, target);

                        // 只给战斗中的目标施加增益，除非该法术只能在非战斗中使用
                        //only buff targets that are in combat, unless the spell can only be cast while out of combat
                        if (!ally)
                            continue;

                        if (spell->CanAutoCast(ally))
                        {
                            targetSpellStore.push_back(std::make_pair(ally, spell));
                            spellUsed = true;
                            break;
                        }
                    }
                }

                // 完全没有有效目标
                // No valid targets at all
                if (!spellUsed)
                    delete spell;
            }
            // 处理有害法术（攻击性法术）
            else if (me->GetVictim() && CanAttack(me->GetVictim()) && spellInfo->CanBeUsedInCombat())
            {
                Spell* spell = new Spell(me, spellInfo, TRIGGERED_NONE);
                if (spell->CanAutoCast(me->GetVictim()))
                    targetSpellStore.push_back(std::make_pair(me->GetVictim(), spell));
                else
                    delete spell;
            }
        }

        // 找到了可以施放法术的目标
        // found units to cast on to
        if (!targetSpellStore.empty())
        {
            // 随机选择一个法术施放
            TargetSpellList::iterator it = targetSpellStore.begin();
            std::advance(it, urand(0, targetSpellStore.size() - 1));

            Spell* spell  = (*it).second;
            Unit*  target = (*it).first;

            targetSpellStore.erase(it);

            // 准备施法目标并施放
            SpellCastTargets targets;
            targets.SetUnitTarget(target);

            spell->prepare(targets);
        }

        // 清理未使用的法术对象
        // deleted cached Spell objects
        for (std::pair<Unit*, Spell*> const& unitspellpair : targetSpellStore)
            delete unitspellpair.second;
    }

    // 更新移动速度，防止宠物落后太远导致消失
    // Update speed as needed to prevent dropping too far behind and despawning
    me->UpdateSpeed(MOVE_RUN);
    me->UpdateSpeed(MOVE_WALK);
    me->UpdateSpeed(MOVE_FLIGHT);

}

/**
 * @brief 宠物击杀单位时的回调函数
 *
 * 职责：处理宠物或主人击杀目标后的清理和目标切换逻辑
 *
 * @param victim 被击杀的单位指针
 *
 * 主要流程：
 * 1. 检查宠物是否还有其他目标（可能是主人击杀了这个目标）
 * 2. 停止攻击并中断非近战法术
 * 3. 选择下一个目标或返回跟随主人
 *
 * 注意：不使用StopAttack()因为它会激活移动处理器并忽略下一个目标选择
 */
void PetAI::KilledUnit(Unit* victim)
{
    // 从Unit::Kill()中调用，用于处理宠物或主人击杀目标的情况
    // Called from Unit::Kill() in case where pet or owner kills something
    // if owner killed this victim, pet may still be attacking something else
    if (me->GetVictim() && me->GetVictim() != victim)
        return;

    // 清除目标以防万一。可能有助于解决生命值/集中值/法力值
    // 回复卡住的问题。同时重置攻击命令。
    // 不能使用StopAttack()因为它会激活移动处理器并忽略
    // 下一个目标选择
    // Clear target just in case. May help problem where health / focus / mana
    // regen gets stuck. Also resets attack command.
    // Can't use StopAttack() because that activates movement handlers and ignores
    // next target selection
    me->AttackStop();
    me->InterruptNonMeleeSpells(false);

    // 在返回主人之前，检查是否还有其他目标可以攻击
    // Before returning to owner, see if there are more things to attack
    if (Unit* nextTarget = SelectNextTarget(false))
        AttackStart(nextTarget);
    else
        HandleReturnMovement(); // Return
}

/**
 * @brief 开始攻击目标
 *
 * 职责：覆盖Unit::AttackStart以防止宠物切换已分配的目标
 *
 * @param target 要攻击的目标单位指针
 *
 * 主要流程：
 * 1. 验证目标有效性（非空且不是自己）
 * 2. 检查是否已有存活的受害者（防止切换目标）
 * 3. 调用内部攻击启动函数
 *
 * 注意：此函数确保宠物不会轻易切换目标，保持对已分配目标的专注
 */
void PetAI::AttackStart(Unit* target)
{
    // 覆盖Unit::AttackStart以防止宠物切换已分配的目标
    // Overrides Unit::AttackStart to prevent pet from switching off its assigned target
    if (!target || target == me)
        return;

    // 如果已有存活的受害者，不切换目标
    if (me->GetVictim() && me->EnsureVictim()->IsAlive())
        return;

    _AttackStart(target);
}

/**
 * @brief 内部攻击启动函数
 *
 * 职责：执行实际的攻击启动逻辑，检查所有宠物状态以决定是否攻击目标
 *
 * @param target 要攻击的目标单位指针
 *
 * 主要流程：
 * 1. 检查是否可以攻击该目标（通过CanAttack检查所有状态）
 * 2. 根据命令状态决定是否追击目标
 *
 * 追击条件：未处于停留状态，或处于停留状态但收到了攻击命令
 */
void PetAI::_AttackStart(Unit* target)
{
    // 检查所有宠物状态以决定是否可以攻击该目标
    // Check all pet states to decide if we can attack this target
    if (!CanAttack(target))
        return;

    // 只有在未被告知停留，或被告知停留但收到攻击命令时才追击
    // Only chase if not commanded to stay or if stay but commanded to attack
    DoAttack(target, (!me->GetCharmInfo()->HasCommandState(COMMAND_STAY) || me->GetCharmInfo()->IsCommandAttack()));
}

/**
 * @brief 主人被攻击时的回调函数
 *
 * 职责：处理主人受到伤害时宠物的反应，防止宠物仅因主人获得仇恨就跑开
 *
 * @param attacker 攻击主人的单位指针
 *
 * 主要流程：
 * 1. 验证攻击者有效性和宠物存活状态
 * 2. 被动模式的宠物不采取任何行动
 * 3. 如果宠物已有存活的受害者，不切换目标
 * 4. 评估并开始攻击攻击者
 */
void PetAI::OwnerAttackedBy(Unit* attacker)
{
    // 当主人受到伤害时调用。此函数帮助防止宠物跑开
    // 仅仅因为主人获得了仇恨。
    // Called when owner takes damage. This function helps keep pets from running off
    //  simply due to owner gaining aggro.

    if (!attacker || !me->IsAlive())
        return;

    // 被动模式的宠物不做任何事
    // Passive pets don't do anything
    if (me->HasReactState(REACT_PASSIVE))
        return;

    // 防止宠物脱离当前目标
    // Prevent pet from disengaging from current target
    if (me->GetVictim() && me->EnsureVictim()->IsAlive())
        return;

    // 继续评估并在必要时攻击
    // Continue to evaluate and attack if necessary
    AttackStart(attacker);
}

/**
 * @brief 主人攻击目标时的回调函数
 *
 * 职责：处理主人攻击某物时的情况，允许防御模式的宠物知道需要协助
 *
 * @param target 主人攻击的目标单位指针
 *
 * 主要流程：
 * 1. 验证目标有效性和宠物存活状态
 * 2. 被动模式的宠物不采取任何行动
 * 3. 如果宠物已有存活的受害者，不切换目标
 * 4. 评估并开始攻击目标
 *
 * 注意：如果从施法中调用且施法目标无效，target可能为NULL
 */
void PetAI::OwnerAttacked(Unit* target)
{
    // 当主人攻击某物时调用。允许防御模式的宠物知道
    // 它们需要协助
    // Called when owner attacks something. Allows defensive pets to know
    //  that they need to assist

    // 如果从施法中调用且施法目标无效，目标可能为NULL
    // Target might be NULL if called from spell with invalid cast targets
    if (!target || !me->IsAlive())
        return;

    // 被动模式的宠物不做任何事
    // Passive pets don't do anything
    if (me->HasReactState(REACT_PASSIVE))
        return;

    // 防止宠物脱离当前目标
    // Prevent pet from disengaging from current target
    if (me->GetVictim() && me->EnsureVictim()->IsAlive())
        return;

    // 继续评估并在必要时攻击
    // Continue to evaluate and attack if necessary
    AttackStart(target);
}

/**
 * @brief 选择下一个攻击目标
 *
 * 职责：在当前目标死亡后提供下一个目标选择，仅在AI内部调用
 *
 * @param allowAutoSelect 是否允许自动选择目标（用于禁用侵略性宠物的自动目标选择）
 *
 * @return Unit* 返回选中的目标指针，如果没有有效目标则返回nullptr
 *
 * 主要流程：
 * 1. 被动模式的宠物不进行目标选择
 * 2. 优先检查宠物自己的攻击者（避免引怪给主人）
 * 3. 检查主人的攻击者
 * 4. 检查主人当前的受害者目标
 * 5. 如果是侵略性模式且允许自动选择，在仇恨范围内选择最近的敌对单位
 *
 * 目标有效性检查在_CanAttack()中进行，不在此函数评估
 */
Unit* PetAI::SelectNextTarget(bool allowAutoSelect) const
{
    // 在当前目标死亡后提供下一个目标选择。
    // 此函数仅应在AI内部调用
    // 目标在此处不进行有效性评估，那在_CanAttack()中完成
    // 参数：allowAutoSelect让我们在特定情况下禁用侵略性宠物的自动目标选择
    // Provides next target selection after current target death.
    // This function should only be called internally by the AI
    // Targets are not evaluated here for being valid targets, that is done in _CanAttack()
    // The parameter: allowAutoSelect lets us disable aggressive pet auto targeting for certain situations

    // 被动模式的宠物不进行下一个目标选择
    // Passive pets don't do next target selection
    if (me->HasReactState(REACT_PASSIVE))
        return nullptr;

    // 优先检查宠物的攻击者，这样就不会把一堆目标引向主人
    // Check pet attackers first so we don't drag a bunch of targets to the owner
    if (Unit* myAttacker = me->getAttackerForHelper())
        if (!myAttacker->HasBreakableByDamageCrowdControlAura())
            return myAttacker;

    // 不确定为什么我们会没有主人，但以防万一...
    // Not sure why we wouldn't have an owner but just in case...
    if (!me->GetCharmerOrOwner())
        return nullptr;

    // 检查主人的攻击者
    // Check owner attackers
    if (Unit* ownerAttacker = me->GetCharmerOrOwner()->getAttackerForHelper())
        if (!ownerAttacker->HasBreakableByDamageCrowdControlAura())
            return ownerAttacker;

    // 检查主人的受害者
    // 3.0.2版本 - 宠物现在在猎人攻击时就开始在防御模式下攻击主人的受害者
    // Check owner victim
    // 3.0.2 - Pets now start attacking their owners victim in defensive mode as soon as the hunter does
    if (Unit* ownerVictim = me->GetCharmerOrOwner()->GetVictim())
            return ownerVictim;

    // 宠物和主人都没有目标，侵略性宠物可以选择任何目标
    // 为防止侵略性宠物连续选择目标并跑开，我们
    // 只在满足特定条件时选择随机目标
    // Neither pet or owner had a target and aggressive pets can pick any target
    // To prevent aggressive pets from chain selecting targets and running off, we
    //  only select a random target if certain conditions are met.
    if (me->HasReactState(REACT_AGGRESSIVE) && allowAutoSelect)
    {
        if (!me->GetCharmInfo()->IsReturning() || me->GetCharmInfo()->IsFollowing() || me->GetCharmInfo()->IsAtStay())
            if (Unit* nearTarget = me->SelectNearestHostileUnitInAggroRange(true, true))
                return nearTarget;
    }

    // 默认 - 没有有效目标
    // Default - no valid targets
    return nullptr;
}

/**
 * @brief 处理宠物返回移动
 *
 * 职责：处理宠物返回停留位置或跟随主人的移动逻辑
 *
 * 主要流程：
 * 1. 检查是否被魅惑（如"野兽之眼"技能），被魅惑时不激活移动
 * 2. 验证魅力信息是否存在
 * 3. 根据命令状态处理：
 *    - 停留命令：返回之前点击停留的位置
 *    - 跟随命令：跟随主人
 * 4. 移除战斗中宠物标志（表示正在返回而非追击目标）
 */
void PetAI::HandleReturnMovement()
{
    // 处理将宠物移回停留位置或主人
    // Handles moving the pet back to stay or owner

    // 在被法术控制时防止激活移动
    // 例如"野兽之眼"
    // Prevent activating movement when under control of spells
    // such as "Eyes of the Beast"
    if (me->IsCharmed())
        return;

    if (!me->GetCharmInfo())
    {
        TC_LOG_WARN("scripts.ai.petai", "me->GetCharmInfo() is NULL in PetAI::HandleReturnMovement(). Debug info: {}", GetDebugInfo());
        return;
    }

    // 处理停留命令状态
    if (me->GetCharmInfo()->HasCommandState(COMMAND_STAY))
    {
        if (!me->GetCharmInfo()->IsAtStay() && !me->GetCharmInfo()->IsReturning())
        {
            // 返回到之前点击停留的位置
            // Return to previous position where stay was clicked
            float x, y, z;

            me->GetCharmInfo()->GetStayPosition(x, y, z);
            ClearCharmInfoFlags();
            me->GetCharmInfo()->SetIsReturning(true);

            // 如果正在追击，移除追击移动类型
            if (me->HasUnitState(UNIT_STATE_CHASE))
                me->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

            me->GetMotionMaster()->MovePoint(me->GetGUID().GetCounter(), x, y, z);
        }
    }
    else // 跟随命令 COMMAND_FOLLOW
    {
        if (!me->GetCharmInfo()->IsFollowing() && !me->GetCharmInfo()->IsReturning())
        {
            ClearCharmInfoFlags();
            me->GetCharmInfo()->SetIsReturning(true);

            // 如果正在追击，移除追击移动类型
            if (me->HasUnitState(UNIT_STATE_CHASE))
                me->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

            me->GetMotionMaster()->MoveFollow(me->GetCharmerOrOwner(), PET_FOLLOW_DIST, me->GetFollowAngle());
        }
    }
    // 移除宠物战斗标志 - 在玩家宠物上，此标志表示我们正在主动追击目标 - 我们正在返回，所以移除它
    me->RemoveUnitFlag(UNIT_FLAG_PET_IN_COMBAT); // on player pets, this flag indicates that we're actively going after a target - we're returning, so remove it
}

/**
 * @brief 执行攻击动作
 *
 * 职责：处理带或不带追击的攻击，并重置标志为下一次更新/生物击杀做准备
 *
 * @param target 要攻击的目标单位指针
 * @param chase 是否追击目标（true追击，false原地攻击）
 *
 * 主要流程：
 * 1. 发起攻击
 * 2. 设置宠物战斗标志
 * 3. 如果是侵略性模式且非命令攻击，播放音效通知玩家
 * 4. 根据是否追击：
 *    - 追击模式：保存攻击命令标志，移除跟随移动，启动追击移动
 *    - 原地模式（停留且在近战范围内）：设置停留标志，移除跟随移动，保持空闲
 *
 * 注意：有远程攻击能力的宠物不关心追击角度
 */
void PetAI::DoAttack(Unit* target, bool chase)
{
    // 处理带追击或不带追击的攻击，并重置标志
    // 为下一次更新/生物击杀做准备
    // Handles attack with or without chase and also resets flags
    // for next update / creature kill

    if (me->Attack(target, true))
    {
        // 设置宠物战斗标志 - 在玩家宠物上，此标志表示我们正在主动追击目标
        me->SetUnitFlag(UNIT_FLAG_PET_IN_COMBAT); // on player pets, this flag indicates we're actively going after a target - that's what we're doing, so set it
        // 播放音效让玩家知道宠物自己选择了攻击目标
        // Play sound to let the player know the pet is attacking something it picked on its own
        if (me->HasReactState(REACT_AGGRESSIVE) && !me->GetCharmInfo()->IsCommandAttack())
            me->SendPetAIReaction(me->GetGUID());

        if (chase)
        {
            // 需要在清除其他标志后重置攻击命令标志
            bool oldCmdAttack = me->GetCharmInfo()->IsCommandAttack(); // This needs to be reset after other flags are cleared
            ClearCharmInfoFlags();
            // 对于被命令攻击的被动宠物，这样它们才能使用技能
            me->GetCharmInfo()->SetIsCommandAttack(oldCmdAttack); // For passive pets commanded to attack so they will use spells

            if (me->HasUnitState(UNIT_STATE_FOLLOW))
                me->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

            // 有远程攻击能力的宠物根本不应该关心追击角度
            // Pets with ranged attacks should not care about the chase angle at all.
            float chaseDistance = me->GetPetChaseDistance();
            float angle = chaseDistance == 0.f ? float(M_PI) : 0.f;
            float tolerance = chaseDistance == 0.f ? float(M_PI_4) : float(M_PI * 2);
            me->GetMotionMaster()->MoveChase(target, ChaseRange(0.f, chaseDistance), ChaseAngle(angle, tolerance));
        }
        else // (停留 && ((侵略性 || 防御性) && 在近战范围内)))
             // (Stay && ((Aggressive || Defensive) && In Melee Range)))
        {
            ClearCharmInfoFlags();
            me->GetCharmInfo()->SetIsAtStay(true);

            if (me->HasUnitState(UNIT_STATE_FOLLOW))
                me->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

            me->GetMotionMaster()->MoveIdle();
        }
    }
}

/**
 * @brief 移动完成通知函数
 *
 * 职责：接收宠物到达停留位置或跟随主人位置的通知
 *
 * @param type 移动类型（POINT_MOTION_TYPE或FOLLOW_MOTION_TYPE）
 * @param id 移动目标ID（用于验证是否为预期的移动）
 *
 * 主要流程：
 * 1. 根据移动类型处理：
 *    - 点移动类型：宠物返回点击停留的位置，验证ID后设置停留标志
 *    - 跟随移动类型：宠物到达跟随点，验证是主人后设置跟随标志
 */
void PetAI::MovementInform(uint32 type, uint32 id)
{
    // 当宠物到达停留位置或跟随主人时接收通知
    // Receives notification when pet reaches stay or follow owner
    switch (type)
    {
        case POINT_MOTION_TYPE:
        {
            // 宠物正在返回点击停留的位置。数据应该是
            // 宠物的GUIDLow，因为我们将其设置为路径点ID
            // Pet is returning to where stay was clicked. data should be
            // pet's GUIDLow since we set that as the waypoint ID
            if (id == me->GetGUID().GetCounter() && me->GetCharmInfo()->IsReturning())
            {
                ClearCharmInfoFlags();
                me->GetCharmInfo()->SetIsAtStay(true);
                me->GetMotionMaster()->MoveIdle();
            }
            break;
        }
        case FOLLOW_MOTION_TYPE:
        {
            // 如果数据是主人的GUIDLow，则我们已到达跟随点，
            // 否则我们可能正在追击一个生物
            // If data is owner's GUIDLow then we've reached follow point,
            // otherwise we're probably chasing a creature
            if (me->GetCharmerOrOwner() && me->GetCharmInfo() && id == me->GetCharmerOrOwner()->GetGUID().GetCounter() && me->GetCharmInfo()->IsReturning())
            {
                ClearCharmInfoFlags();
                me->GetCharmInfo()->SetIsFollowing(true);
            }
            break;
        }
        default:
            break;
    }
}

/**
 * @brief 判断宠物是否可以攻击指定目标
 *
 * 职责：根据命令状态、反应状态和其他标志评估宠物是否可以攻击特定目标
 *
 * @param target 要评估的目标单位指针
 *
 * @return bool true表示可以攻击，false表示不能攻击
 *
 * 主要流程（检查顺序很重要）：
 * 1. 验证目标有效性和存活状态
 * 2. 验证魅力信息存在性
 * 3. 被动模式：只有在被命令攻击时才能攻击
 * 4. 控制状态：被控制的目标只有在主人命令时才能攻击
 * 5. 返回状态：只有在主人没有点击跟随时才能攻击
 * 6. 停留状态：只有目标在近战范围内或被命令攻击时才能攻击
 * 7. 已有目标：只有在主人选择并命令攻击新目标时才能切换
 * 8. 跟随状态：只有在没有返回时才能攻击
 *
 * 重要提示：检查的顺序很重要，添加或删除检查时要小心
 */
bool PetAI::CanAttack(Unit* target)
{
    // 根据命令状态、反应状态和其他标志评估宠物是否可以攻击特定目标
    // 重要：检查的顺序很重要，添加或删除检查时要小心
    // Evaluates wether a pet can attack a specific target based on CommandState, ReactState and other flags
    // IMPORTANT: The order in which things are checked is important, be careful if you add or remove checks

    // 嗯...
    // Hmmm...
    if (!target)
        return false;

    if (!target->IsAlive())
    {
        // 如果目标无效，宠物应该自动脱战
        // 清除目标以防止卡在死亡目标上
        // if target is invalid, pet should evade automaticly
        // Clear target to prevent getting stuck on dead targets
        //me->AttackStop();
        //me->InterruptNonMeleeSpells(false);
        return false;
    }

    if (!me->GetCharmInfo())
    {
        TC_LOG_WARN("scripts.ai.petai", "me->GetCharmInfo() is NULL in PetAI::CanAttack(). Debug info: {}", GetDebugInfo());
        return false;
    }

    // 被动 - 被动宠物只有在被命令时才能攻击
    // Passive - passive pets can attack if told to
    if (me->HasReactState(REACT_PASSIVE))
        return me->GetCharmInfo()->IsCommandAttack();

    // 控制技能 - 被人群控制的怪物只有在主人命令时才能攻击
    // CC - mobs under crowd control can be attacked if owner commanded
    if (target->HasBreakableByDamageCrowdControlAura())
        return me->GetCharmInfo()->IsCommandAttack();

    // 返回 - 宠物只有在主人没有点击跟随时才忽略攻击
    // Returning - pets ignore attacks only if owner clicked follow
    if (me->GetCharmInfo()->IsReturning())
        return !me->GetCharmInfo()->IsCommandFollow();

    // 停留 - 如果目标在范围内或被命令攻击则可以攻击
    // Stay - can attack if target is within range or commanded to
    if (me->GetCharmInfo()->HasCommandState(COMMAND_STAY))
        return (me->IsWithinMeleeRange(target) || me->GetCharmInfo()->IsCommandAttack());

    // 正在攻击某物（或追击）的宠物只有在主人告诉它们时才应该切换目标
    //  Pets attacking something (or chasing) should only switch targets if owner tells them to
    if (me->GetVictim() && me->GetVictim() != target)
    {
        // 检查主人是否选择了这个目标并点击了"攻击"
        // Check if our owner selected this target and clicked "attack"
        Unit* ownerTarget = nullptr;
        if (Player* owner = me->GetCharmerOrOwner()->ToPlayer())
            ownerTarget = owner->GetSelectedUnit();
        else
            ownerTarget = me->GetCharmerOrOwner()->GetVictim();

        if (ownerTarget && me->GetCharmInfo()->IsCommandAttack())
            return (target->GetGUID() == ownerTarget->GetGUID());
    }

    // 跟随
    // Follow
    if (me->GetCharmInfo()->HasCommandState(COMMAND_FOLLOW))
        return !me->GetCharmInfo()->IsReturning();

    // 默认情况，虽然我们不应该到达这里
    // default, though we shouldn't ever get here
    return false;
}

/**
 * @brief 接收表情动作
 *
 * 职责：处理玩家对宠物使用的表情动作，产生对应的情感反应
 *
 * @param player 使用表情的玩家指针
 * @param emote 表情类型ID
 *
 * 主要流程：
 * 1. 验证玩家是否为宠物的主人
 * 2. 根据不同的表情类型执行对应动作：
 *    - 畏缩（COWER）：死亡骑士食尸鬼做出咆哮动作
 *    - 愤怒（ANGRY）：死亡骑士食尸鬼做出畏缩动作
 *    - 怒视（GLARE）：死亡骑士食尸鬼做出眩晕状态
 *    - 安抚（SOOTHE）：死亡骑士食尸鬼做出咆哮动作
 *
 * 注意：仅对死亡骑士的食尸鬼宠物有效
 */
void PetAI::ReceiveEmote(Player* player, uint32 emote)
{
    // 只有宠物的主人才能触发表情反应
    if (me->GetOwnerGUID() != player->GetGUID())
        return;

    switch (emote)
    {
        case TEXT_EMOTE_COWER:
            // 畏缩表情 - 食尸鬼咆哮回应
            if (me->IsPet() && me->ToPet()->IsPetGhoul())
                me->HandleEmoteCommand(/*EMOTE_ONESHOT_ROAR*/EMOTE_ONESHOT_OMNICAST_GHOUL);
            break;
        case TEXT_EMOTE_ANGRY:
            // 愤怒表情 - 食尸鬼畏缩回应
            if (me->IsPet() && me->ToPet()->IsPetGhoul())
                me->HandleEmoteCommand(/*EMOTE_ONESHOT_COWER*/EMOTE_STATE_STUN);
            break;
        case TEXT_EMOTE_GLARE:
            // 怒视表情 - 食尸鬼眩晕状态
            if (me->IsPet() && me->ToPet()->IsPetGhoul())
                me->HandleEmoteCommand(EMOTE_STATE_STUN);
            break;
        case TEXT_EMOTE_SOOTHE:
            // 安抚表情 - 食尸鬼咆哮回应
            if (me->IsPet() && me->ToPet()->IsPetGhoul())
                me->HandleEmoteCommand(EMOTE_ONESHOT_OMNICAST_GHOUL);
            break;
    }
}

/**
 * @brief 判断是否需要停止攻击
 *
 * 职责：检查当前状态是否需要停止攻击
 *
 * @return bool true表示需要停止攻击，false表示可以继续攻击
 *
 * 主要流程：
 * 1. 检查被魅惑的生物是否在攻击魅惑者（防止自相残杀）
 * 2. 检查宠物是否离主人太远（超过视野范围-10码）
 * 3. 检查当前目标是否为有效攻击目标
 */
bool PetAI::NeedToStop()
{
    // 对于被魅惑的生物是必需的，因为一旦它们的目标被重置，其他效果可能触发仇恨
    // This is needed for charmed creatures, as once their target was reset other effects can trigger threat
    if (me->IsCharmed() && me->GetVictim() == me->GetCharmer())
        return true;

    // 不允许宠物跟随距离主人太远的目标
    // dont allow pets to follow targets far away from owner
    if (Unit* owner = me->GetCharmerOrOwner())
        if (owner->GetExactDist(me) >= (owner->GetVisibilityRange() - 10.0f))
            return true;

    // 检查目标是否仍然有效
    return !me->IsValidAttackTarget(me->GetVictim());
}

/**
 * @brief 停止攻击
 *
 * 职责：处理宠物停止攻击的逻辑，根据宠物存活状态采取不同措施
 *
 * 主要流程：
 * 1. 如果宠物已死亡：
 *    - 清除移动主
 *    - 设置为空闲移动
 *    - 停止战斗
 * 2. 如果宠物存活：
 *    - 停止攻击
 *    - 中断非近战法术
 *    - 重置攻击命令标志
 *    - 清除魅力信息标志
 *    - 处理返回移动
 */
void PetAI::StopAttack()
{
    if (!me->IsAlive())
    {
        // 宠物死亡时的处理
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
        me->CombatStop();
        return;
    }

    // 宠物存活时的处理
    me->AttackStop();
    me->InterruptNonMeleeSpells(false);
    me->GetCharmInfo()->SetIsCommandAttack(false);
    ClearCharmInfoFlags();
    HandleReturnMovement();
}

/**
 * @brief 更新盟友列表
 *
 * 职责：定期更新宠物可以施放有益法术的盟友列表（包括宠物自身、主人和队友）
 *
 * 主要流程：
 * 1. 设置更新定时器（每10秒更新一次，减少检查频率提高性能）
 * 2. 获取主人和队伍信息
 * 3. 如果盟友数量未变化，跳过更新
 * 4. 清空当前盟友列表
 * 5. 添加宠物自身
 * 6. 如果主人在队伍中：
 *    - 遍历队伍成员
 *    - 添加同一小队的成员
 * 7. 如果主人不在队伍中：
 *    - 仅添加主人
 *
 * 性能优化：减少不必要的列表重建操作
 */
void PetAI::UpdateAllies()
{
    // 每10秒更新一次友方目标，减少检查可提高性能
    _updateAlliesTimer = 10 * IN_MILLISECONDS; // update friendly targets every 10 seconds, lesser checks increase performance

    Unit* owner = me->GetCharmerOrOwner();
    if (!owner)
        return;

    // 获取主人的队伍信息
    Group* group = nullptr;
    if (Player* player = owner->ToPlayer())
        group = player->GetGroup();

    // 仅宠物和主人/不在队伍中->无需更新
    // only pet and owner/not in group->ok
    if (_allySet.size() == 2 && !group)
        return;

    // 主人在队伍中；队伍成员已填充（非团队->小队数量=总数量）
    // owner is in group; group members filled in already (no raid -> subgroupcount = whole count)
    if (group && !group->isRaidGroup() && _allySet.size() == (group->GetMembersCount() + 2))
        return;

    // 重建盟友列表
    _allySet.clear();
    _allySet.insert(me->GetGUID()); // 添加宠物自身
    if (group) // 添加队伍成员
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* Target = itr->GetSource();
            // 跳过无效目标、不在同一地图的目标、不在同一小队的成员
            if (!Target || !Target->IsInMap(owner) || !group->SameSubGroup(owner->ToPlayer(), Target))
                continue;

            // 跳过主人（稍后单独添加）
            if (Target->GetGUID() == owner->GetGUID())
                continue;

            _allySet.insert(Target->GetGUID());
        }
    }
    else // 主人不在队伍中，添加主人
        _allySet.insert(owner->GetGUID());
}

/**
 * @brief 被魅惑时的回调函数
 *
 * 职责：处理宠物被魅惑时的状态变化
 *
 * @param isNew 是否为新魅惑状态
 *
 * 主要流程：
 * 1. 如果宠物没有被玩家附身且处于魅惑状态，启动跟随移动
 * 2. 调用父类的OnCharmed方法
 *
 * 注意：这是处理宠物被其他玩家或NPC控制时的情况
 */
void PetAI::OnCharmed(bool isNew)
{
    // 如果没有被玩家附身且被魅惑，让宠物跟随魅惑者
    if (!me->isPossessedByPlayer() && me->IsCharmed())
        me->GetMotionMaster()->MoveFollow(me->GetCharmer(), PET_FOLLOW_DIST, me->GetFollowAngle());

    CreatureAI::OnCharmed(isNew);
}

/**
 * @brief 清除魅力信息标志
 *
 * 职责：重置所有宠物状态标志为false，为新的状态设置做准备
 *
 * 主要流程：
 * 1. 获取魅力信息对象
 * 2. 如果存在，清除所有状态标志：
 *    - IsAtStay: 是否在停留位置
 *    - IsCommandAttack: 是否被命令攻击
 *    - IsCommandFollow: 是否被命令跟随
 *    - IsFollowing: 是否正在跟随
 *    - IsReturning: 是否正在返回
 *
 * 注意：通常在状态切换前调用，确保旧状态不会干扰新状态
 */
void PetAI::ClearCharmInfoFlags()
{
    CharmInfo* ci = me->GetCharmInfo();
    if (ci)
    {
        ci->SetIsAtStay(false);         // 清除停留状态
        ci->SetIsCommandAttack(false);  // 清除攻击命令
        ci->SetIsCommandFollow(false);  // 清除跟随命令
        ci->SetIsFollowing(false);      // 清除跟随状态
        ci->SetIsReturning(false);      // 清除返回状态
    }
}
