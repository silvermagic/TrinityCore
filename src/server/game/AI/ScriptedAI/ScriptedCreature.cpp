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
 * @file ScriptedCreature.cpp
 * @brief 脚本化生物实现文件
 *
 * 本文件实现了 TrinityCore 中脚本化生物系统的核心功能，包括：
 * - SummonList: 召唤生物列表管理类
 * - ScriptedAI: 脚本化 AI 基类，提供常用 AI 行为辅助函数
 * - BossAI: 副本 Boss AI 基类，提供 Boss 战斗管理功能
 * - WorldBossAI: 野外 Boss AI 基类，用于非副本 Boss
 *
 * 这些类为脚本开发者提供了丰富的工具函数，简化了怪物 AI 的开发流程。
 */

#include "ScriptedCreature.h"
#include "AreaBoundary.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Containers.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"

/**
 * @struct TSpellSummary
 * @brief 法术摘要结构体，用于存储法术的目标类型和效果类型信息
 *
 * 该结构体用于 ScriptedAI::SelectSpell 函数中，存储法术的选择目标类型和选择效果类型。
 * 在服务器启动时初始化，提供快速的法术查询能力。
 */
struct TSpellSummary
{
    uint8 Targets; ///< 目标类型集合，对应 SelectTarget 枚举
    uint8 Effects; ///< 效果类型集合，对应 SelectEffect 枚举
};

extern TSpellSummary* SpellSummary; ///< 全局法术摘要数组指针

/**
 * @brief 将召唤生物添加到列表中
 *
 * 将召唤生物的 GUID 添加到存储列表中进行管理。
 *
 * @param summon 要添加的召唤生物指针
 */
void SummonList::Summon(Creature const* summon)
{
    _storage.push_back(summon->GetGUID());
}

/**
 * @brief 从列表中移除召唤生物
 *
 * 从存储列表中移除指定召唤生物的 GUID（不进行 despawn 操作）。
 *
 * @param summon 要移除的召唤生物指针
 */
void SummonList::Despawn(Creature const* summon)
{
    _storage.remove(summon->GetGUID());
}

/**
 * @brief 让召唤生物进入战斗状态
 *
 * 让列表中的所有召唤生物（或指定 entry 的召唤生物）进入战斗区域。
 * 调用召唤生物 AI 的 DoZoneInCombat 方法，使其攻击附近的敌对目标。
 *
 * @param entry 生物 ID，如果为 0 则作用于所有召唤生物，否则只作用于指定 ID 的生物
 */
void SummonList::DoZoneInCombat(uint32 entry)
{
    for (StorageType::iterator i = _storage.begin(); i != _storage.end();)
    {
        Creature* summon = ObjectAccessor::GetCreature(*_me, *i);
        ++i;
        if (summon && summon->IsAIEnabled()
                && (!entry || summon->GetEntry() == entry))
        {
            summon->AI()->DoZoneInCombat(nullptr);
        }
    }
}

/**
 * @brief 消失指定 entry 的所有召唤生物
 *
 * 从列表中移除并消失（despawn）所有指定 entry 的召唤生物。
 *
 * @param entry 要消失的生物 ID
 */
void SummonList::DespawnEntry(uint32 entry)
{
    for (StorageType::iterator i = _storage.begin(); i != _storage.end();)
    {
        Creature* summon = ObjectAccessor::GetCreature(*_me, *i);
        if (!summon)
            i = _storage.erase(i);
        else if (summon->GetEntry() == entry)
        {
            i = _storage.erase(i);
            summon->DespawnOrUnsummon();
        }
        else
            ++i;
    }
}

/**
 * @brief 消失所有召唤生物
 *
 * 清空列表并消失（despawn）所有被管理的召唤生物。
 */
void SummonList::DespawnAll()
{
    while (!_storage.empty())
    {
        Creature* summon = ObjectAccessor::GetCreature(*_me, _storage.front());
        _storage.pop_front();
        if (summon)
            summon->DespawnOrUnsummon();
    }
}

/**
 * @brief 移除不存在的召唤生物
 *
 * 从列表中移除所有已经不存在（已被消失或无效）的召唤生物 GUID。
 * 这是一个清理函数，用于维护列表的有效性。
 */
void SummonList::RemoveNotExisting()
{
    for (StorageType::iterator i = _storage.begin(); i != _storage.end();)
    {
        if (ObjectAccessor::GetCreature(*_me, *i))
            ++i;
        else
            i = _storage.erase(i);
    }
}

/**
 * @brief 检查是否存在指定 entry 的召唤生物
 *
 * 检查列表中是否有指定 entry 的召唤生物仍然存在。
 *
 * @param entry 要检查的生物 ID
 * @return true 如果存在指定 entry 的召唤生物
 * @return false 如果不存在
 */
bool SummonList::HasEntry(uint32 entry) const
{
    for (ObjectGuid const& guid : _storage)
    {
        Creature* summon = ObjectAccessor::GetCreature(*_me, guid);
        if (summon && summon->GetEntry() == entry)
            return true;
    }

    return false;
}

/**
 * @brief 对召唤生物执行动作的实现函数
 *
 * 对召唤生物列表执行指定的动作。可以选择限制执行动作的最大数量，
 * 如果指定了最大数量，将随机选择指定数量的召唤生物执行动作。
 *
 * @param action 要执行的动作 ID
 * @param summons 召唤生物列表的引用
 * @param max 最大执行数量，如果为 0 则对全部召唤生物执行
 */
void SummonList::DoActionImpl(int32 action, StorageType& summons, uint16 max)
{
    if (max)
        Trinity::Containers::RandomResize(summons, max);

    for (ObjectGuid const& guid : summons)
    {
        Creature* summon = ObjectAccessor::GetCreature(*_me, guid);
        if (summon && summon->IsAIEnabled())
            summon->AI()->DoAction(action);
    }
}

/**
 * @brief ScriptedAI 构造函数
 *
 * 初始化脚本化 AI 的基本属性，包括难度设置和英雄模式检测。
 *
 * @param creature 关联的生物对象指针
 */
ScriptedAI::ScriptedAI(Creature* creature) : CreatureAI(creature), IsFleeing(false), _isCombatMovementAllowed(true)
{
    _isHeroic = me->GetMap()->IsHeroic();
    _difficulty = Difficulty(me->GetMap()->GetSpawnMode());
}

/**
 * @brief 开始攻击但不移动
 *
 * 开始攻击指定目标，但保持当前位置不进行追逐移动。
 * 适用于需要原地攻击的场景，如远程攻击或特定技能施放。
 *
 * @param who 要攻击的目标单位
 */
void ScriptedAI::AttackStartNoMove(Unit* who)
{
    if (!who)
        return;

    if (me->Attack(who, true))
        DoStartNoMovement(who);
}

/**
 * @brief 开始攻击
 *
 * 根据是否允许战斗移动来决定攻击行为：
 * - 如果允许战斗移动，调用基类的 AttackStart（会追逐目标）
 * - 如果不允许战斗移动，调用 AttackStartNoMove（原地攻击）
 *
 * @param who 要攻击的目标单位
 */
void ScriptedAI::AttackStart(Unit* who)
{
    if (IsCombatMovementAllowed())
        CreatureAI::AttackStart(who);
    else
        AttackStartNoMove(who);
}

/**
 * @brief 更新 AI
 *
 * 基本的 AI 更新逻辑，检查是否有有效目标并进行近战攻击。
 * 这是一个可以被派生类重写的基础实现。
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 */
void ScriptedAI::UpdateAI(uint32 /*diff*/)
{
    // Check if we have a current target
    if (!UpdateVictim())
        return;

    DoMeleeAttackIfReady();
}

/**
 * @brief 开始追逐目标
 *
 * 使生物开始追逐指定目标进行移动。
 * 可以指定追逐的距离和角度。
 *
 * @param victim 要追逐的目标单位
 * @param distance 追逐距离，默认使用生物的战斗到达距离
 * @param angle 追逐角度，默认为 0（正对目标）
 */
void ScriptedAI::DoStartMovement(Unit* victim, float distance, float angle)
{
    if (victim)
        me->GetMotionMaster()->MoveChase(victim, distance, angle);
}

/**
 * @brief 停止移动
 *
 * 使生物停止移动，进入空闲状态。
 * 通常用于需要原地施法或特殊行为的场景。
 *
 * @param victim 目标单位（用于验证，可以为 nullptr）
 */
void ScriptedAI::DoStartNoMovement(Unit* victim)
{
    if (!victim)
        return;

    me->GetMotionMaster()->MoveIdle();
}

/**
 * @brief 停止攻击
 *
 * 如果当前有攻击目标，停止攻击行为。
 */
void ScriptedAI::DoStopAttack()
{
    if (me->GetVictim())
        me->AttackStop();
}

/**
 * @brief 施放法术
 *
 * 向目标施放指定的法术。如果生物正在施放其他法术，则不会施放。
 * 施放前会停止移动。
 *
 * @param target 目标单位
 * @param spellInfo 法术信息
 * @param triggered 是否为触发法术（触发法术无视冷却和消耗）
 */
void ScriptedAI::DoCastSpell(Unit* target, SpellInfo const* spellInfo, bool triggered)
{
    if (!target || me->IsNonMeleeSpellCast(false))
        return;

    me->StopMoving();
    me->CastSpell(target, spellInfo->Id, triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE);
}

/**
 * @brief 播放声音
 *
 * 在指定源对象上播放指定的声音 ID。
 * 如果声音 ID 无效，会记录错误日志。
 *
 * @param source 声音源对象
 * @param soundId 声音 ID
 */
void ScriptedAI::DoPlaySoundToSet(WorldObject* source, uint32 soundId)
{
    if (!source)
        return;

    if (!sSoundEntriesStore.LookupEntry(soundId))
    {
        TC_LOG_ERROR("scripts.ai", "ScriptedAI::DoPlaySoundToSet: Invalid soundId {} used in DoPlaySoundToSet (Source: {})", soundId, source->GetGUID().ToString());
        return;
    }

    source->PlayDirectSound(soundId);
}

/**
 * @brief 添加仇恨值
 *
 * 向指定目标添加仇恨值。如果未指定 who，则默认为当前生物。
 *
 * @param victim 目标单位
 * @param amount 仇恨值数量
 * @param who 拥有仇恨列表的单位，默认为 nullptr（使用当前生物）
 */
void ScriptedAI::AddThreat(Unit* victim, float amount, Unit* who)
{
    if (!victim)
        return;
    if (!who)
        who = me;
    who->GetThreatManager().AddThreat(victim, amount, nullptr, true, true);
}

/**
 * @brief 按百分比修改仇恨值
 *
 * 按指定百分比修改目标的仇恨值。
 * 正百分比增加仇恨，负百分比减少仇恨。
 *
 * @param victim 目标单位
 * @param pct 百分比数值
 * @param who 拥有仇恨列表的单位，默认为 nullptr（使用当前生物）
 */
void ScriptedAI::ModifyThreatByPercent(Unit* victim, int32 pct, Unit* who)
{
    if (!victim)
        return;
    if (!who)
        who = me;
    who->GetThreatManager().ModifyThreatByPercent(victim, pct);
}

/**
 * @brief 重置目标的仇恨值
 *
 * 将指定目标在仇恨列表中的仇恨值重置为 0。
 *
 * @param victim 目标单位
 * @param who 拥有仇恨列表的单位，默认为 nullptr（使用当前生物）
 */
void ScriptedAI::ResetThreat(Unit* victim, Unit* who)
{
    if (!victim)
        return;
    if (!who)
        who = me;
    who->GetThreatManager().ResetThreat(victim);
}

/**
 * @brief 重置整个仇恨列表
 *
 * 清空仇恨列表，移除所有目标的仇恨值。
 *
 * @param who 拥有仇恨列表的单位，默认为 nullptr（使用当前生物）
 */
void ScriptedAI::ResetThreatList(Unit* who)
{
    if (!who)
        who = me;
    who->GetThreatManager().ResetAllThreat();
}

/**
 * @brief 获取目标的仇恨值
 *
 * 获取指定目标在仇恨列表中的当前仇恨值。
 *
 * @param victim 目标单位
 * @param who 拥有仇恨列表的单位，默认为 nullptr（使用当前生物）
 * @return float 当前仇恨值，如果目标无效则返回 0.0f
 */
float ScriptedAI::GetThreat(Unit const* victim, Unit const* who)
{
    if (!victim)
        return 0.0f;
    if (!who)
        who = me;
    return who->GetThreatManager().GetThreat(victim);
}

/**
 * @brief 强制停止战斗
 *
 * 强制使指定生物脱离战斗状态。
 * 可选择是否重置生物的状态（重新加载 addon、清除掉落接收者等）。
 *
 * @param who 要停止战斗的生物指针
 * @param reset 是否重置生物状态，默认为 true
 */
void ScriptedAI::ForceCombatStop(Creature* who, bool reset /*= true*/)
{
    if (!who || !who->IsInCombat())
        return;

    who->CombatStop(true);
    who->DoNotReacquireSpellFocusTarget();
    who->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);

    if (reset) {
        who->LoadCreaturesAddon();
        who->SetLootRecipient(nullptr);
        who->ResetPlayerDamageReq();
        who->SetLastDamagedTime(0);
        who->SetCannotReachTarget(false);
    }
}

/**
 * @brief 强制停止指定 entry 生物的战斗
 *
 * 在指定范围内搜索所有指定 entry 的生物，并强制使它们脱离战斗状态。
 *
 * @param entry 生物 ID
 * @param maxSearchRange 最大搜索范围，默认为 250.0f
 * @param samePhase 是否只搜索相同相位，默认为 true
 * @param reset 是否重置生物状态，默认为 true
 */
void ScriptedAI::ForceCombatStopForCreatureEntry(uint32 entry, float maxSearchRange /*= 250.0f*/, bool samePhase /*= true*/, bool reset /*= true*/)
{
    TC_LOG_DEBUG("scripts.ai", "ScriptedAI::ForceCombatStopForCreatureEntry: called on '{}'. Debug info: {}", me->GetGUID().ToString(), me->GetDebugInfo());

    std::list<Creature*> creatures;
    Trinity::AllCreaturesOfEntryInRange check(me, entry, maxSearchRange);
    Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(me, creatures, check);

    if (!samePhase)
        searcher.i_phaseMask = PHASEMASK_ANYWHERE;

    Cell::VisitGridObjects(me, searcher, maxSearchRange);

    for (Creature* creature : creatures)
        ForceCombatStop(creature, reset);
}

/**
 * @brief 强制停止多个 entry 生物的战斗
 *
 * 对多个生物 entry 执行强制停止战斗操作。
 * 这是一个批量处理函数，内部循环调用单 entry 版本。
 *
 * @param creatureEntries 生物 ID 列表
 * @param maxSearchRange 最大搜索范围，默认为 250.0f
 * @param samePhase 是否只搜索相同相位，默认为 true
 * @param reset 是否重置生物状态，默认为 true
 */
void ScriptedAI::ForceCombatStopForCreatureEntry(std::vector<uint32> creatureEntries, float maxSearchRange /*= 250.0f*/, bool samePhase /*= true*/, bool reset /*= true*/)
{
    for (uint32 const entry : creatureEntries)
        ForceCombatStopForCreatureEntry(entry, maxSearchRange, samePhase, reset);
}

/**
 * @brief 生成一个召唤生物
 *
 * 在相对于当前生物位置的指定偏移处生成一个召唤生物。
 *
 * @param entry 召唤生物的 ID
 * @param offsetX X 轴偏移量
 * @param offsetY Y 轴偏移量
 * @param offsetZ Z 轴偏移量
 * @param angle 朝向角度
 * @param type 召唤类型（对应 TempSummonType 枚举）
 * @param despawntime 消失时间（毫秒）
 * @return Creature* 生成的生物指针，失败返回 nullptr
 */
Creature* ScriptedAI::DoSpawnCreature(uint32 entry, float offsetX, float offsetY, float offsetZ, float angle, uint32 type, Milliseconds despawntime)
{
    return me->SummonCreature(entry, me->GetPositionX() + offsetX, me->GetPositionY() + offsetY, me->GetPositionZ() + offsetZ, angle, TempSummonType(type), despawntime);
}

/**
 * @brief 检查生命值是否低于指定百分比
 *
 * 检查当前生物的生命值是否低于指定的百分比。
 *
 * @param pct 百分比值（0-100）
 * @return true 如果生命值低于指定百分比
 * @return false 如果生命值不低于指定百分比
 */
bool ScriptedAI::HealthBelowPct(uint32 pct) const
{
    return me->HealthBelowPct(pct);
}

/**
 * @brief 检查生命值是否高于指定百分比
 *
 * 检查当前生物的生命值是否高于指定的百分比。
 *
 * @param pct 百分比值（0-100）
 * @return true 如果生命值高于指定百分比
 * @return false 如果生命值不高于指定百分比
 */
bool ScriptedAI::HealthAbovePct(uint32 pct) const
{
    return me->HealthAbovePct(pct);
}

/**
 * @brief 选择合适的法术
 *
 * 根据多种条件从生物的法术列表中选择一个合适的法术施放。
 * 这是一个智能法术选择函数，会考虑目标类型、效果类型、魔法学派、
 * 法术机制、法力消耗、施法距离等多种因素。
 *
 * @param target 目标单位
 * @param school 魔法学派掩码，0 表示不限学派
 * @param mechanic 法术机制，0 表示不限机制
 * @param targets 目标类型，对应 SelectTargetType 枚举
 * @param powerCostMin 最小法力消耗
 * @param powerCostMax 最大法力消耗
 * @param rangeMin 最小施法距离
 * @param rangeMax 最大施法距离
 * @param effects 效果类型，对应 SelectEffect 枚举
 * @return SpellInfo const* 选择的法术信息指针，如果没有合适的法术则返回 nullptr
 *
 * @note 该函数会从生物的 m_spells 数组中遍历所有法术，进行多层过滤后随机选择一个。
 *       过滤顺序：目标类型 -> 效果类型 -> 魔法学派 -> 法术机制 ->
 *                法力消耗范围 -> 当前法力是否足够 -> 距离范围 -> 目标是否在施法距离内
 */
SpellInfo const* ScriptedAI::SelectSpell(Unit* target, uint32 school, uint32 mechanic, SelectTargetType targets, uint32 powerCostMin, uint32 powerCostMax, float rangeMin, float rangeMax, SelectEffect effects)
{
    // No target so we can't cast
    if (!target)
        return nullptr;

    // Silenced so we can't cast
    if (me->HasUnitFlag(UNIT_FLAG_SILENCED))
        return nullptr;

    // Using the extended script system we first create a list of viable spells
    SpellInfo const* apSpell[MAX_CREATURE_SPELLS];
    memset(apSpell, 0, MAX_CREATURE_SPELLS * sizeof(SpellInfo*));

    uint32 spellCount = 0;

    SpellInfo const* tempSpell = nullptr;

    // Check if each spell is viable(set it to null if not)
    for (uint32 spell : me->m_spells)
    {
        tempSpell = sSpellMgr->GetSpellInfo(spell);

        // This spell doesn't exist
        if (!tempSpell)
            continue;

        // Targets and Effects checked first as most used restrictions
        // Check the spell targets if specified
        if (targets && !(SpellSummary[spell].Targets & (1 << (targets-1))))
            continue;

        // Check the type of spell if we are looking for a specific spell type
        if (effects && !(SpellSummary[spell].Effects & (1 << (effects-1))))
            continue;

        // Check for school if specified
        if (school && (tempSpell->SchoolMask & school) == 0)
            continue;

        // Check for spell mechanic if specified
        if (mechanic && tempSpell->Mechanic != mechanic)
            continue;

        // Make sure that the spell uses the requested amount of power
        if (powerCostMin && tempSpell->ManaCost < powerCostMin)
            continue;

        if (powerCostMax && tempSpell->ManaCost > powerCostMax)
            continue;

        // Continue if we don't have the mana to actually cast this spell
        if (tempSpell->ManaCost > me->GetPower(tempSpell->PowerType))
            continue;

        // Check if the spell meets our range requirements
        if (rangeMin && me->GetSpellMinRangeForTarget(target, tempSpell) < rangeMin)
            continue;
        if (rangeMax && me->GetSpellMaxRangeForTarget(target, tempSpell) > rangeMax)
            continue;

        // Check if our target is in range
        if (me->IsWithinDistInMap(target, float(me->GetSpellMinRangeForTarget(target, tempSpell))) || !me->IsWithinDistInMap(target, float(me->GetSpellMaxRangeForTarget(target, tempSpell))))
            continue;

        // All good so lets add it to the spell list
        apSpell[spellCount] = tempSpell;
        ++spellCount;
    }

    // We got our usable spells so now lets randomly pick one
    if (!spellCount)
        return nullptr;

    return apSpell[urand(0, spellCount - 1)];
}

/**
 * @brief 传送到指定位置（带移动动画）
 *
 * 将生物传送到指定位置，并生成移动动画。
 * 移动速度根据距离和时间计算。
 *
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param z 目标 Z 坐标
 * @param time 移动时间（毫秒），用于计算移动速度
 */
void ScriptedAI::DoTeleportTo(float x, float y, float z, uint32 time)
{
    me->Relocate(x, y, z);
    float speed = me->GetDistance(x, y, z) / ((float)time * 0.001f);
    me->MonsterMoveWithSpeed(x, y, z, speed);
}

/**
 * @brief 传送到指定位置（立即传送）
 *
 * 将生物立即传送到指定位置和朝向，不产生移动动画。
 *
 * @param position 目标位置数组，包含 [x, y, z, orientation]
 */
void ScriptedAI::DoTeleportTo(const float position[4])
{
    me->NearTeleportTo(position[0], position[1], position[2], position[3]);
}

/**
 * @brief 传送玩家到指定位置
 *
 * 将玩家单位传送到指定位置。如果目标不是玩家，会记录错误日志。
 * 传送时保持玩家在战斗状态。
 *
 * @param unit 要传送的单位（必须是玩家）
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param z 目标 Z 坐标
 * @param o 目标朝向
 */
void ScriptedAI::DoTeleportPlayer(Unit* unit, float x, float y, float z, float o)
{
    if (!unit)
        return;

    if (Player* player = unit->ToPlayer())
        player->TeleportTo(unit->GetMapId(), x, y, z, o, TELE_TO_NOT_LEAVE_COMBAT);
    else
        TC_LOG_ERROR("scripts.ai", "ScriptedAI::DoTeleportPlayer: Creature {} Tried to teleport non-player unit ({}) to x: {} y:{} z: {} o: {}. Aborted.",
            me->GetGUID().ToString(), unit->GetGUID().ToString(), x, y, z, o);
}

/**
 * @brief 传送所有玩家到指定位置
 *
 * 将地图上的所有存活玩家传送到指定位置。
 * 仅对副本地图有效。
 *
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param z 目标 Z 坐标
 * @param o 目标朝向
 */
void ScriptedAI::DoTeleportAll(float x, float y, float z, float o)
{
    Map* map = me->GetMap();
    if (!map->IsDungeon())
        return;

    for (MapReference const& mapref : map->GetPlayers())
        if (Player* player = mapref.GetSource())
            if (player->IsAlive())
                player->TeleportTo(me->GetMapId(), x, y, z, o, TELE_TO_NOT_LEAVE_COMBAT);
}

/**
 * @brief 选择生命值最低的友方单位
 *
 * 在指定范围内搜索生命值缺失最多的友方单位。
 * 可用于治疗 AI 选择治疗目标。
 *
 * @param range 搜索范围
 * @param minHPDiff 最小生命值缺失量，默认为 0
 * @return Unit* 生命值缺失最多的友方单位，如果没有则返回 nullptr
 */
Unit* ScriptedAI::DoSelectLowestHpFriendly(float range, uint32 minHPDiff)
{
    Unit* unit = nullptr;
    Trinity::MostHPMissingInRange u_check(me, range, minHPDiff);
    Trinity::UnitLastSearcher<Trinity::MostHPMissingInRange> searcher(me, unit, u_check);
    Cell::VisitAllObjects(me, searcher, range);

    return unit;
}

/**
 * @brief 选择指定 entry 且生命值低于百分比的友方单位
 *
 * 在指定范围内搜索指定 entry 且生命值低于指定百分比的友方单位。
 * 可用于为特定类型的友方单位提供支援。
 *
 * @param entry 生物 ID
 * @param range 搜索范围
 * @param minHPDiff 最小生命值百分比阈值
 * @param excludeSelf 是否排除自身，默认为 true
 * @return Unit* 符合条件的友方单位，如果没有则返回 nullptr
 */
Unit* ScriptedAI::DoSelectBelowHpPctFriendlyWithEntry(uint32 entry, float range, uint8 minHPDiff, bool excludeSelf)
{
    Unit* unit = nullptr;
    Trinity::FriendlyBelowHpPctEntryInRange u_check(me, entry, range, minHPDiff, excludeSelf);
    Trinity::UnitLastSearcher<Trinity::FriendlyBelowHpPctEntryInRange> searcher(me, unit, u_check);
    Cell::VisitAllObjects(me, searcher, range);

    return unit;
}

/**
 * @brief 查找受控制效果的友方生物
 *
 * 在指定范围内查找受到控制效果（如昏迷、变形等）的友方生物。
 * 可用于驱散或救援友方单位。
 *
 * @param range 搜索范围
 * @return std::list<Creature*> 受到控制效果的友方生物列表
 */
std::list<Creature*> ScriptedAI::DoFindFriendlyCC(float range)
{
    std::list<Creature*> list;
    Trinity::FriendlyCCedInRange u_check(me, range);
    Trinity::CreatureListSearcher<Trinity::FriendlyCCedInRange> searcher(me, list, u_check);
    Cell::VisitAllObjects(me, searcher, range);

    return list;
}

/**
 * @brief 查找缺少指定增益的友方生物
 *
 * 在指定范围内查找没有指定增益法术的友方生物。
 * 可用于 Buff AI 选择增益目标。
 *
 * @param range 搜索范围
 * @param uiSpellid 增益法术 ID
 * @return std::list<Creature*> 缺少该增益的友方生物列表
 */
std::list<Creature*> ScriptedAI::DoFindFriendlyMissingBuff(float range, uint32 uiSpellid)
{
    std::list<Creature*> list;
    Trinity::FriendlyMissingBuffInRange u_check(me, range, uiSpellid);
    Trinity::CreatureListSearcher<Trinity::FriendlyMissingBuffInRange> searcher(me, list, u_check);
    Cell::VisitAllObjects(me, searcher, range);

    return list;
}

/**
 * @brief 获取指定最小距离外的玩家
 *
 * 在指定最小距离外搜索玩家。如果范围内有玩家，返回其中一个。
 * 可用于检测玩家是否在安全距离外。
 *
 * @param minimumRange 最小距离
 * @return Player* 找到的玩家指针，如果没有则返回 nullptr
 */
Player* ScriptedAI::GetPlayerAtMinimumRange(float minimumRange)
{
    Player* player = nullptr;

    Trinity::PlayerAtMinimumRangeAway check(me, minimumRange);
    Trinity::PlayerSearcher<Trinity::PlayerAtMinimumRangeAway> searcher(me, player, check);
    Cell::VisitWorldObjects(me, searcher, minimumRange);

    return player;
}

/**
 * @brief 设置装备栏
 *
 * 设置生物的装备显示。可以选择加载默认装备或手动设置各个装备槽。
 * 三个装备槽分别对应：主手、副手、远程武器。
 *
 * @param loadDefault 是否加载默认装备，如果为 true 则忽略其他参数
 * @param mainHand 主手装备 ID，EQUIP_NO_CHANGE 表示不改变，EQUIP_UNEQUIP 表示卸下
 * @param offHand 副手装备 ID，EQUIP_NO_CHANGE 表示不改变，EQUIP_UNEQUIP 表示卸下
 * @param ranged 远程武器装备 ID，EQUIP_NO_CHANGE 表示不改变，EQUIP_UNEQUIP 表示卸下
 */
void ScriptedAI::SetEquipmentSlots(bool loadDefault, int32 mainHand /*= EQUIP_NO_CHANGE*/, int32 offHand /*= EQUIP_NO_CHANGE*/, int32 ranged /*= EQUIP_NO_CHANGE*/)
{
    if (loadDefault)
    {
        me->LoadEquipment(me->GetOriginalEquipmentId(), true);
        return;
    }

    if (mainHand >= 0)
        me->SetVirtualItem(0, uint32(mainHand));

    if (offHand >= 0)
        me->SetVirtualItem(1, uint32(offHand));

    if (ranged >= 0)
        me->SetVirtualItem(2, uint32(ranged));
}

/**
 * @brief 设置战斗移动状态
 *
 * 控制生物在战斗中是否允许移动。
 * 这会影响 AttackStart 函数的行为。
 *
 * @param allowMovement true 允许移动，false 禁止移动
 */
void ScriptedAI::SetCombatMovement(bool allowMovement)
{
    _isCombatMovementAllowed = allowMovement;
}

///============================================================================
/// BossAI - 副本 Boss AI 实现
///============================================================================

/**
 * @brief BossAI 构造函数
 *
 * 初始化副本 Boss AI 的基本属性，包括实例脚本引用、召唤列表、
 * Boss ID 和边界设置。同时设置调度器验证器，确保在施法时不执行新任务。
 *
 * @param creature Boss 生物对象指针
 * @param bossId Boss ID，用于实例脚本中标识 Boss
 */
BossAI::BossAI(Creature* creature, uint32 bossId) : ScriptedAI(creature), instance(creature->GetInstanceScript()), summons(creature), _bossId(bossId)
{
    if (instance)
        SetBoundary(instance->GetBossBoundary(bossId));
    scheduler.SetValidator([this]
    {
        return !me->HasUnitState(UNIT_STATE_CASTING);
    });
}

/**
 * @brief Boss 重置函数
 *
 * 当 Boss 重置时调用（如脱离战斗、初始化等）。
 * 清理战斗相关状态，消失所有召唤生物，取消所有计划任务，
 * 并将 Boss 状态设置为 NOT_STARTED（如果尚未完成）。
 */
void BossAI::_Reset()
{
    if (!me->IsAlive())
        return;

    me->SetCombatPulseDelay(0);
    me->ResetLootMode();
    events.Reset();
    summons.DespawnAll();
    scheduler.CancelAll();
    if (instance && instance->GetBossState(_bossId) != DONE)
        instance->SetBossState(_bossId, NOT_STARTED);
}

/**
 * @brief Boss 死亡函数
 *
 * 当 Boss 死亡时调用。重置事件和调度器，消失所有召唤生物，
 * 并将 Boss 状态设置为 DONE。
 */
void BossAI::_JustDied()
{
    events.Reset();
    summons.DespawnAll();
    scheduler.CancelAll();
    if (instance)
        instance->SetBossState(_bossId, DONE);
}

/**
 * @brief Boss 返回出生点函数
 *
 * 当 Boss 返回出生点时调用（通常在脱离战斗后）。
 * 将生物设置为非活跃状态以节省资源。
 */
void BossAI::_JustReachedHome()
{
    me->setActive(false);
}

/**
 * @brief Boss 进入战斗函数
 *
 * 当 Boss 进入战斗时调用。检查必要的前置 Boss 是否已击败，
 * 设置 Boss 状态为 IN_PROGRESS，激活生物，让 Boss 及其召唤物进入战斗，
 * 并调度任务。
 *
 * @param who 首先攻击 Boss 的单位
 */
void BossAI::_JustEngagedWith(Unit* who)
{
    if (instance)
    {
        // bosses do not respawn, check only on enter combat
        if (!instance->CheckRequiredBosses(_bossId, who->ToPlayer()))
        {
            EnterEvadeMode(EVADE_REASON_SEQUENCE_BREAK);
            return;
        }
        instance->SetBossState(_bossId, IN_PROGRESS);
    }

    me->SetCombatPulseDelay(5);
    me->setActive(true);
    DoZoneInCombat();
    ScheduleTasks();
}

/**
 * @brief 传送作弊者
 *
 * 将所有在 Boss 边界外与 Boss 战斗的玩家传送回 Boss 位置。
 * 用于防止玩家利用地形或机制作弊。
 */
void BossAI::TeleportCheaters()
{
    float x, y, z;
    me->GetPosition(x, y, z);

    for (auto const& pair : me->GetCombatManager().GetPvECombatRefs())
    {
        Unit* target = pair.second->GetOther(me);
        if (target->IsControlledByPlayer() && !IsInBoundary(target))
            target->NearTeleportTo(x, y, z, 0);
    }
}

/**
 * @brief 召唤生物回调函数
 *
 * 当 Boss 召唤生物时调用。将召唤生物添加到召唤列表中，
 * 如果 Boss 已进入战斗，则让召唤物也进入战斗。
 *
 * @param summon 被召唤的生物
 */
void BossAI::JustSummoned(Creature* summon)
{
    summons.Summon(summon);
    if (me->IsEngaged())
        DoZoneInCombat(summon);
}

/**
 * @brief 召唤生物消失回调函数
 *
 * 当召唤的生物消失时调用。从召唤列表中移除该生物。
 *
 * @param summon 消失的生物
 */
void BossAI::SummonedCreatureDespawn(Creature* summon)
{
    summons.Despawn(summon);
}

/**
 * @brief Boss AI 更新函数
 *
 * 更新 Boss AI 状态，处理事件和调度任务。
 * 如果正在施法则不执行新事件。
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 */
void BossAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    events.Update(diff);

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    while (uint32 eventId = events.ExecuteEvent())
    {
        ExecuteEvent(eventId);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;
    }

    DoMeleeAttackIfReady();
}

/**
 * @brief 检查是否可以攻击目标
 *
 * 检查目标是否在 Boss 的战斗边界内。
 * 用于防止 Boss 攻击边界外的目标。
 *
 * @param target 目标单位
 * @return true 如果可以攻击（目标在边界内）
 * @return false 如果不能攻击（目标在边界外）
 */
bool BossAI::CanAIAttack(Unit const* target) const
{
    return IsInBoundary(target);
}

/**
 * @brief 脱离战斗后消失
 *
 * 当 Boss 脱离战斗后，延迟一定时间后消失并重新刷新。
 * 如果是临时召唤生物，则直接消失。同时将 Boss 状态设置为 FAIL。
 *
 * @param delayToRespawn 重生延迟时间（秒），默认为 30 秒
 * @param who 要消失的生物，默认为 nullptr（使用当前生物）
 */
void BossAI::_DespawnAtEvade(Seconds delayToRespawn /*= 30s*/, Creature* who /*= nullptr*/)
{
    if (delayToRespawn < 2s)
    {
        TC_LOG_ERROR("scripts.ai", "BossAI::_DespawnAtEvade: called with delay of {} seconds, defaulting to 2 (me: {})", delayToRespawn.count(), me->GetGUID().ToString());
        delayToRespawn = 2s;
    }

    if (!who)
        who = me;

    if (TempSummon* whoSummon = who->ToTempSummon())
    {
        TC_LOG_WARN("scripts.ai", "BossAI::_DespawnAtEvade: called on a temporary summon (who: {})", who->GetGUID().ToString());
        whoSummon->UnSummon();
        return;
    }

    who->DespawnOrUnsummon(0s, delayToRespawn);

    if (instance && who == me)
        instance->SetBossState(_bossId, FAIL);
}

///============================================================================
/// WorldBossAI - 野外 Boss AI 实现
///============================================================================

/**
 * @brief WorldBossAI 构造函数
 *
 * 初始化野外 Boss AI 的基本属性，包括召唤列表。
 * 野外 Boss 不关联实例脚本，适用于开放世界的 Boss。
 *
 * @param creature Boss 生物对象指针
 */
WorldBossAI::WorldBossAI(Creature* creature) : ScriptedAI(creature), summons(creature) { }

/**
 * @brief 野外 Boss 重置函数
 *
 * 当野外 Boss 重置时调用。清理事件和消失所有召唤生物。
 */
void WorldBossAI::_Reset()
{
    if (!me->IsAlive())
        return;

    events.Reset();
    summons.DespawnAll();
}

/**
 * @brief 野外 Boss 死亡函数
 *
 * 当野外 Boss 死亡时调用。重置事件并消失所有召唤生物。
 */
void WorldBossAI::_JustDied()
{
    events.Reset();
    summons.DespawnAll();
}

/**
 * @brief 野外 Boss 进入战斗函数
 *
 * 当野外 Boss 进入战斗时调用。
 * 随机选择一个玩家作为攻击目标并开始攻击。
 */
void WorldBossAI::_JustEngagedWith()
{
    Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true);
    if (target)
        AttackStart(target);
}

/**
 * @brief 召唤生物回调函数
 *
 * 当野外 Boss 召唤生物时调用。将召唤生物添加到召唤列表，
 * 并让其随机攻击一个玩家。
 *
 * @param summon 被召唤的生物
 */
void WorldBossAI::JustSummoned(Creature* summon)
{
    summons.Summon(summon);
    Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true);
    if (target)
        summon->AI()->AttackStart(target);
}

/**
 * @brief 召唤生物消失回调函数
 *
 * 当召唤的生物消失时调用。从召唤列表中移除该生物。
 *
 * @param summon 消失的生物
 */
void WorldBossAI::SummonedCreatureDespawn(Creature* summon)
{
    summons.Despawn(summon);
}

/**
 * @brief 野外 Boss AI 更新函数
 *
 * 更新野外 Boss AI 状态，处理事件。
 * 如果正在施法则不执行新事件。
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 */
void WorldBossAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    events.Update(diff);

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    while (uint32 eventId = events.ExecuteEvent())
    {
        ExecuteEvent(eventId);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;
    }

    DoMeleeAttackIfReady();
}
