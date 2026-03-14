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
 * @file CombatManager.cpp
 * @brief 战斗管理器实现文件 - 实现战斗状态管理的核心逻辑
 *
 * 本文件实现了 CombatManager、CombatReference 和 PvPCombatReference 的所有功能，
 * 提供了游戏中战斗系统的核心机制。
 *
 * 主要实现内容：
 *   1. CombatManager 类：管理单位的战斗状态
 *      - 战斗关系的建立和解除
 *      - PvP 战斗的超时检测
 *      - 战斗状态的查询和批量操作
 *
 *   2. CombatReference 结构：表示两个单位间的战斗关系
 *      - 战斗状态的抑制和恢复
 *      - 战斗结束的清理工作
 *
 *   3. PvPCombatReference 结构：PvP 战斗的特殊处理
 *      - 战斗超时计时器
 *      - 计时器刷新机制
 *
 * 核心算法：
 *   - CanBeginCombat：判断两个单位是否可以开始战斗（多重条件检查）
 *   - UpdateOwnerCombatState：更新单位的战斗标志，触发进入/退出战斗回调
 *   - EndCombat：完整的战斗结束流程（清理威胁、移除引用、通知AI）
 *
 * 性能考虑：
 *   - 使用 unordered_map 存储战斗引用，查找效率 O(1)
 *   - PvP 超时检查仅在单位更新时执行，避免频繁遍历
 *   - 使用迭代器手动删除避免遍历时的迭代器失效
 */

#include "CombatManager.h"
#include "Containers.h"
#include "Creature.h"
#include "Unit.h"
#include "CreatureAI.h"
#include "Player.h"

/**
 * @brief 检查两个单位是否可以开始战斗
 *
 * 职责：
 *   在创建初始战斗引用之前，验证两个单位之间是否可以建立战斗关系。
 *   这是战斗系统的基础验证函数，确保战斗的合法性。
 *
 * @param a 第一个单位
 * @param b 第二个单位
 *
 * @return true 如果两个单位可以开始战斗
 * @return false 如果两个单位不能开始战斗
 *
 * 主要流程：
 *   1. 检查两个单位是否相同（不能与自己战斗）
 *   2. 检查两个单位是否都在世界中
 *   3. 检查两个单位是否都存活
 *   4. 检查两个单位是否在同一地图
 *   5. 检查两个单位是否在同一相位
 *   6. 检查是否有逃避状态或飞行状态
 *   7. 检查是否被禁止进入战斗
 *   8. 检查两个单位是否敌对
 *   9. 检查是否有GM模式的玩家参与
 */
/*static*/ bool CombatManager::CanBeginCombat(Unit const* a, Unit const* b)
{
    // 检查战斗有效性的前置条件
    // 要使战斗有效...

    // ...两个单位必须是不同的（不能与自己战斗）
    if (a == b)
        return false;

    // ...两个单位必须都在世界中
    if (!a->IsInWorld() || !b->IsInWorld())
        return false;

    // ...两个单位必须都存活
    if (!a->IsAlive() || !b->IsAlive())
        return false;

    // ...两个单位必须在同一地图
    if (a->GetMap() != b->GetMap())
        return false;

    // ...两个单位必须在同一相位
    if (!WorldObject::InSamePhase(a, b))
        return false;

    // ...两个单位都不能处于逃避状态
    if (a->HasUnitState(UNIT_STATE_EVADE) || b->HasUnitState(UNIT_STATE_EVADE))
        return false;

    // ...两个单位都不能处于飞行状态
    if (a->HasUnitState(UNIT_STATE_IN_FLIGHT) || b->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return false;

    // ...两个单位都必须被允许进入战斗
    if (a->IsCombatDisallowed() || b->IsCombatDisallowed())
        return false;

    // ...两个单位必须互为敌对关系
    if (a->IsFriendlyTo(b) || b->IsFriendlyTo(a))
        return false;

    // 获取单位对应的玩家（如果是宠物/召唤物则获取主人）
    Player const* playerA = a->GetCharmerOrOwnerPlayerOrPlayerItself();
    Player const* playerB = b->GetCharmerOrOwnerPlayerOrPlayerItself();

    // ...两个单位都不能是（或属于）开启了GM模式的玩家
    if ((playerA && playerA->IsGameMaster()) || (playerB && playerB->IsGameMaster()))
        return false;

    return true;
}

/**
 * @brief 结束战斗引用
 *
 * 职责：
 *   终止两个单位之间的战斗关系，清理威胁列表，移除战斗引用，
 *   并通知AI退出战斗。这是结束战斗的核心函数。
 *
 * 主要流程：
 *   1. 清除双方单位的威胁关系
 *   2. 从双方的战斗管理器中移除引用
 *   3. 更新双方的战斗状态
 *   4. 通知AI退出战斗（如果需要）
 *   5. 删除战斗引用对象
 *
 * 注意：
 *   - 执行顺序非常重要，AI可能会执行复杂操作
 *   - 必须在移交控制权前确保引用处于一致状态
 */
void CombatReference::EndCombat()
{
    // 执行顺序很重要 - AI可能会执行一些棘手的操作，
    // 所以在移交控制权之前确保引用处于一致状态！

    // 首先，清除所有仍存在的威胁...
    first->GetThreatManager().ClearThreat(second);
    second->GetThreatManager().ClearThreat(first);

    // ...然后，从双方的战斗管理器中移除引用...
    first->GetCombatManager().PurgeReference(second->GetGUID(), _isPvP);
    second->GetCombatManager().PurgeReference(first->GetGUID(), _isPvP);

    // ...更新战斗状态，这可能会移除 IN_COMBAT 标志...
    bool const needFirstAI = first->GetCombatManager().UpdateOwnerCombatState();
    bool const needSecondAI = second->GetCombatManager().UpdateOwnerCombatState();

    // ...如果发生了状态改变，也通知AI...
    if (needFirstAI)
        if (UnitAI* firstAI = first->GetAI())
            firstAI->JustExitedCombat();
    if (needSecondAI)
        if (UnitAI* secondAI = second->GetAI())
            secondAI->JustExitedCombat();

    // ...最后清理引用对象
    delete this;
}

/**
 * @brief 刷新战斗引用
 *
 * 职责：
 *   恢复被抑制的战斗状态，并通知AI重新进入战斗。
 *   当战斗引用被临时抑制后，可以通过此方法恢复。
 *
 * 主要流程：
 *   1. 检查第一个单位是否被抑制，如果是则解除抑制并更新战斗状态
 *   2. 检查第二个单位是否被抑制，如果是则解除抑制并更新战斗状态
 *   3. 如果状态发生变化，通知AI进入战斗
 */
void CombatReference::Refresh()
{
    bool needFirstAI = false, needSecondAI = false;

    // 检查并解除第一个单位的抑制状态
    if (_suppressFirst)
    {
        _suppressFirst = false;
        needFirstAI = first->GetCombatManager().UpdateOwnerCombatState();
    }

    // 检查并解除第二个单位的抑制状态
    if (_suppressSecond)
    {
        _suppressSecond = false;
        needSecondAI = second->GetCombatManager().UpdateOwnerCombatState();
    }

    // 如果状态改变，通知AI重新进入战斗
    if (needFirstAI)
        CombatManager::NotifyAICombat(first, second);
    if (needSecondAI)
        CombatManager::NotifyAICombat(second, first);
}

/**
 * @brief 为指定单位抑制战斗引用
 *
 * 职责：
 *   临时抑制指定单位的战斗引用，使其暂时退出战斗状态，
 *   并通知AI退出战斗。
 *
 * @param who 要抑制战斗的单位
 *
 * 主要流程：
 *   1. 抑制指定单位的战斗引用
 *   2. 更新单位的战斗状态
 *   3. 如果状态改变，通知AI退出战斗
 */
void CombatReference::SuppressFor(Unit* who)
{
    // 抑制指定单位的战斗引用
    Suppress(who);

    // 更新战斗状态，如果状态改变则通知AI
    if (who->GetCombatManager().UpdateOwnerCombatState())
        if (UnitAI* ai = who->GetAI())
            ai->JustExitedCombat();
}

/**
 * @brief 更新PvP战斗引用
 *
 * 职责：
 *   更新PvP战斗的超时计时器，判断战斗是否应该因超时结束。
 *   PvP战斗有一个超时机制，如果超时则自动结束战斗。
 *
 * @param tdiff 经过的毫秒数
 *
 * @return true 如果战斗仍然有效（未超时）
 * @return false 如果战斗已超时，应该结束
 *
 * 主要流程：
 *   1. 检查计时器是否已到期
 *   2. 如果到期，返回false表示战斗应结束
 *   3. 否则，减去经过的时间并返回true
 */
bool PvPCombatReference::Update(uint32 tdiff)
{
    // 如果计时器已到期，返回false表示战斗应结束
    if (_combatTimer <= tdiff)
        return false;

    // 减去经过的时间
    _combatTimer -= tdiff;
    return true;
}

/**
 * @brief 刷新PvP战斗计时器
 *
 * 职责：
 *   重置PvP战斗的超时计时器为默认值。
 *   当玩家在PvP中有新活动时调用，延长战斗持续时间。
 */
void PvPCombatReference::RefreshTimer()
{
    _combatTimer = PVP_COMBAT_TIMEOUT;
}

/**
 * @brief 战斗管理器析构函数
 *
 * 职责：
 *   销毁战斗管理器，确保所有战斗引用都已清理完毕。
 *   如果仍有战斗引用存在，说明存在内存泄漏或逻辑错误。
 *
 * 注意：
 *   - 析构时断言检查所有PvE和PvP战斗引用列表必须为空
 *   - 如果不为空，将输出错误信息并终止程序
 */
CombatManager::~CombatManager()
{
    ASSERT(_pveRefs.empty(), "CombatManager::~CombatManager - %s: we still have %zu PvE combat references, one of them is with %s", _owner->GetGUID().ToString().c_str(), _pveRefs.size(), _pveRefs.begin()->first.ToString().c_str());
    ASSERT(_pvpRefs.empty(), "CombatManager::~CombatManager - %s: we still have %zu PvP combat references, one of them is with %s", _owner->GetGUID().ToString().c_str(), _pvpRefs.size(), _pvpRefs.begin()->first.ToString().c_str());
}

/**
 * @brief 更新战斗管理器
 *
 * 职责：
 *   定期更新战斗管理器，处理PvP战斗的超时机制。
 *   检查所有PvP战斗引用，如果超时则结束战斗。
 *
 * @param tdiff 经过的毫秒数
 *
 * 主要流程：
 *   1. 遍历所有PvP战斗引用
 *   2. 仅当当前单位是战斗引用的第一个参与者时才更新（避免重复递减）
 *   3. 如果战斗超时，从引用列表中移除并结束战斗
 *   4. 继续处理下一个引用
 *
 * 注意：
 *   - 只更新PvP战斗，PvE战斗没有超时机制
 *   - 使用双重检查避免重复更新
 */
void CombatManager::Update(uint32 tdiff)
{
    auto it = _pvpRefs.begin(), end = _pvpRefs.end();
    while (it != end)
    {
        PvPCombatReference* const ref = it->second;

        // 只有当我们是第一个参与单位时才更新（否则会重复递减计时器）
        if (ref->first == _owner && !ref->Update(tdiff))
        {
            // 先从我们的引用列表中移除，以避免迭代器失效
            it = _pvpRefs.erase(it), end = _pvpRefs.end();

            // 这将从另一端也移除引用
            ref->EndCombat();
        }
        else
            ++it;
    }
}

/**
 * @brief 检查是否有PvE战斗
 *
 * 职责：
 *   检查单位当前是否处于任何PvE战斗中。
 *   只要有至少一个未被抑制的PvE战斗引用，就返回true。
 *
 * @return true 如果处于PvE战斗中
 * @return false 如果不处于任何PvE战斗中
 */
bool CombatManager::HasPvECombat() const
{
    for (auto const& [guid, ref] : _pveRefs)
        if (!ref->IsSuppressedFor(_owner))
            return true;
    return false;
}

/**
 * @brief 检查是否与玩家处于PvE战斗中
 *
 * 职责：
 *   检查单位是否与玩家处于PvE战斗中。
 *   这在判断NPC是否在与玩家战斗时很有用。
 *
 * @return true 如果与玩家处于PvE战斗中
 * @return false 如果不与任何玩家处于PvE战斗中
 */
bool CombatManager::HasPvECombatWithPlayers() const
{
    for (std::pair<ObjectGuid const, CombatReference*> const& reference : _pveRefs)
        if (!reference.second->IsSuppressedFor(_owner) && reference.second->GetOther(_owner)->GetTypeId() == TYPEID_PLAYER)
            return true;

    return false;
}

/**
 * @brief 检查是否有PvP战斗
 *
 * 职责：
 *   检查单位当前是否处于任何PvP战斗中。
 *   只要有至少一个未被抑制的PvP战斗引用，就返回true。
 *
 * @return true 如果处于PvP战斗中
 * @return false 如果不处于任何PvP战斗中
 */
bool CombatManager::HasPvPCombat() const
{
    for (auto const& pair : _pvpRefs)
        if (!pair.second->IsSuppressedFor(_owner))
            return true;
    return false;
}

/**
 * @brief 获取任意战斗目标
 *
 * 职责：
 *   获取当前单位正在战斗的任意一个目标。
 *   优先返回PvE战斗目标，如果没有则返回PvP战斗目标。
 *
 * @return Unit* 战斗目标的指针，如果没有战斗则返回nullptr
 */
Unit* CombatManager::GetAnyTarget() const
{
    // 优先检查PvE战斗引用
    for (auto const& pair : _pveRefs)
        if (!pair.second->IsSuppressedFor(_owner))
            return pair.second->GetOther(_owner);

    // 如果没有PvE战斗，检查PvP战斗引用
    for (auto const& pair : _pvpRefs)
        if (!pair.second->IsSuppressedFor(_owner))
            return pair.second->GetOther(_owner);

    return nullptr;
}

/**
 * @brief 设置与目标进入战斗
 *
 * 职责：
 *   建立本单位与目标单位之间的战斗关系。这是进入战斗的核心函数。
 *   如果已存在战斗关系则刷新，否则创建新的战斗引用。
 *
 * @param who 要进入战斗的目标单位
 * @param addSecondUnitSuppressed 是否抑制第二个单位的战斗状态（默认false）
 *
 * @return true 如果成功建立或刷新了战斗关系
 * @return false 如果无法进入战斗（不满足战斗条件）
 *
 * 主要流程：
 *   1. 检查是否已存在战斗关系，如果存在则刷新并返回
 *   2. 验证是否可以开始战斗
 *   3. 根据双方是否为玩家控制创建PvP或PvE战斗引用
 *   4. 如果需要，抑制第二个单位的战斗状态
 *   5. 将引用插入双方的战斗管理器
 *   6. 更新双方的战斗状态
 *   7. 通知AI进入战斗（如果需要）
 *
 * 注意：
 *   - 执行顺序很重要，先更新状态再通知AI
 *   - PvP战斗有超时机制，PvE战斗没有
 */
bool CombatManager::SetInCombatWith(Unit* who, bool addSecondUnitSuppressed)
{
    // 检查是否已经处于战斗中？如果是，刷新PvP战斗计时器
    if (PvPCombatReference* existingPvpRef = Trinity::Containers::MapGetValuePtr(_pvpRefs, who->GetGUID()))
    {
        existingPvpRef->RefreshTimer();
        existingPvpRef->Refresh();
        return true;
    }

    // 检查是否已存在PvE战斗引用
    if (CombatReference* existingPveRef = Trinity::Containers::MapGetValuePtr(_pveRefs, who->GetGUID()))
    {
        existingPveRef->Refresh();
        return true;
    }

    // 否则，检查战斗有效性...
    if (!CombatManager::CanBeginCombat(_owner, who))
        return false;

    // ...然后创建新的战斗引用
    CombatReference* ref;
    if (_owner->IsControlledByPlayer() && who->IsControlledByPlayer())
        ref = new PvPCombatReference(_owner, who);  // 双方都是玩家控制，创建PvP引用
    else
        ref = new CombatReference(_owner, who);     // 否则创建PvE引用

    // 如果需要，抑制第二个单位的战斗状态
    if (addSecondUnitSuppressed)
        ref->Suppress(who);

    // ...并将引用插入双方的战斗管理器
    PutReference(who->GetGUID(), ref);
    who->GetCombatManager().PutReference(_owner->GetGUID(), ref);

    // 现在，执行顺序很重要 - 首先更新战斗状态，这将设置双方进入战斗状态并执行非AI的战斗开始操作
    bool const needSelfAI  = UpdateOwnerCombatState();
    bool const needOtherAI = who->GetCombatManager().UpdateOwnerCombatState();

    // 然后，最终通知AI（如果需要）并让其安全地执行任何操作
    if (needSelfAI)
        NotifyAICombat(_owner, who);
    if (needOtherAI)
        NotifyAICombat(who, _owner);

    return IsInCombatWith(who);
}

/**
 * @brief 检查是否与指定GUID的单位处于战斗中
 *
 * 职责：
 *   检查本单位是否与指定GUID的单位存在战斗关系。
 *
 * @param guid 目标单位的GUID
 *
 * @return true 如果存在战斗关系
 * @return false 如果不存在战斗关系
 */
bool CombatManager::IsInCombatWith(ObjectGuid const& guid) const
{
    return (_pveRefs.find(guid) != _pveRefs.end()) || (_pvpRefs.find(guid) != _pvpRefs.end());
}

/**
 * @brief 检查是否与指定单位处于战斗中
 *
 * 职责：
 *   检查本单位是否与指定单位存在战斗关系。
 *
 * @param who 目标单位
 *
 * @return true 如果存在战斗关系
 * @return false 如果不存在战斗关系
 */
bool CombatManager::IsInCombatWith(Unit const* who) const
{
    return IsInCombatWith(who->GetGUID());
}

/**
 * @brief 从另一个单位继承战斗状态
 *
 * 职责：
 *   继承指定单位的所有战斗关系。这通常用于召唤物、宠物或替身
 *   需要继承主人的战斗状态的场景。
 *
 * @param who 要继承战斗状态的源单位
 *
 * 主要流程：
 *   1. 遍历源单位的PvE战斗引用
 *   2. 如果尚未与目标战斗且不被免疫，则建立战斗关系
 *   3. 遍历源单位的PvP战斗引用
 *   4. 同样处理PvP战斗关系
 *
 * 注意：
 *   - 会检查免疫状态，避免与免疫的目标建立战斗
 *   - 免疫PC的NPC不会继承与玩家的战斗关系
 *   - 免疫NPC的NPC不会继承与其他NPC的战斗关系
 */
void CombatManager::InheritCombatStatesFrom(Unit const* who)
{
    CombatManager const& mgr = who->GetCombatManager();

    // 继承PvE战斗状态
    for (auto& ref : mgr._pveRefs)
    {
        if (!IsInCombatWith(ref.first))
        {
            Unit* target = ref.second->GetOther(who);

            // 检查免疫状态，跳过免疫的目标
            if ((_owner->IsImmuneToPC() && target->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED)) ||
                (_owner->IsImmuneToNPC() && !target->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED)))
                continue;

            SetInCombatWith(target);
        }
    }

    // 继承PvP战斗状态
    for (auto& ref : mgr._pvpRefs)
    {
        Unit* target = ref.second->GetOther(who);

        // 检查免疫状态，跳过免疫的目标
        if ((_owner->IsImmuneToPC() && target->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED)) ||
            (_owner->IsImmuneToNPC() && !target->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED)))
            continue;

        SetInCombatWith(target);
    }
}

/**
 * @brief 结束超出范围的战斗
 *
 * 职责：
 *   结束所有超出指定距离的战斗关系。这通常用于逃避机制，
 *   当单位距离敌人太远时自动脱离战斗。
 *
 * @param range 判断距离的范围值
 * @param includingPvP 是否同时处理PvP战斗（默认情况）
 *
 * 主要流程：
 *   1. 遍历所有PvE战斗引用
 *   2. 如果双方距离超过指定范围，结束战斗
 *   3. 如果需要，同样处理PvP战斗引用
 *
 * 注意：
 *   - 手动删除迭代器以避免迭代器失效
 *   - 距离检查考虑地图边界和有效距离
 */
void CombatManager::EndCombatBeyondRange(float range, bool includingPvP)
{
    // 处理PvE战斗引用
    auto it = _pveRefs.begin(), end = _pveRefs.end();
    while (it != end)
    {
        CombatReference* const ref = it->second;
        if (!ref->first->IsWithinDistInMap(ref->second, range))
        {
            // 手动擦除以避免迭代器失效
            it = _pveRefs.erase(it), end = _pveRefs.end();
            ref->EndCombat();
        }
        else
            ++it;
    }

    // 如果不处理PvP战斗，直接返回
    if (!includingPvP)
        return;

    // 处理PvP战斗引用
    auto it2 = _pvpRefs.begin(), end2 = _pvpRefs.end();
    while (it2 != end2)
    {
        CombatReference* const ref = it2->second;
        if (!ref->first->IsWithinDistInMap(ref->second, range))
        {
            // 手动擦除以避免迭代器失效
            it2 = _pvpRefs.erase(it2), end2 = _pvpRefs.end();
            ref->EndCombat();
        }
        else
            ++it2;
    }
}

/**
 * @brief 抑制所有PvP战斗
 *
 * 职责：
 *   抑制当前单位的所有PvP战斗引用，使其暂时退出PvP战斗状态，
 *   并通知AI退出战斗。
 *
 * 主要流程：
 *   1. 遍历所有PvP战斗引用并抑制
 *   2. 更新单位的战斗状态
 *   3. 如果状态改变，通知AI退出战斗
 */
void CombatManager::SuppressPvPCombat()
{
    // 抑制所有PvP战斗引用
    for (auto const& pair : _pvpRefs)
        pair.second->Suppress(_owner);

    // 更新战斗状态，如果改变则通知AI
    if (UpdateOwnerCombatState())
        if (UnitAI* ownerAI = _owner->GetAI())
            ownerAI->JustExitedCombat();
}

/**
 * @brief 结束所有PvE战斗
 *
 * 职责：
 *   终止当前单位与所有敌人的PvE战斗关系。
 *   这会清除所有威胁列表并结束所有PvE战斗引用。
 *
 * 主要流程：
 *   1. 从所有威胁列表中移除自己
 *   2. 清除自己的威胁列表
 *   3. 逐个结束所有PvE战斗引用
 *
 * 注意：
 *   - 不能在没有战斗的情况下保留威胁
 *   - 清除威胁是战斗结束前的必要操作
 */
void CombatManager::EndAllPvECombat()
{
    // 没有战斗就不能有威胁
    _owner->GetThreatManager().RemoveMeFromThreatLists();
    _owner->GetThreatManager().ClearAllThreat();

    // 结束所有PvE战斗引用
    while (!_pveRefs.empty())
        _pveRefs.begin()->second->EndCombat();
}

/**
 * @brief 重新验证所有战斗关系
 *
 * 职责：
 *   检查所有现有的战斗关系是否仍然有效。如果任何战斗关系
 *   不再满足战斗条件（例如单位死亡、离开地图、改变阵营等），
 *   则结束该战斗关系。
 *
 * 主要流程：
 *   1. 遍历所有PvE战斗引用
 *   2. 验证是否仍满足战斗条件
 *   3. 如果不满足，结束该战斗
 *   4. 同样处理所有PvP战斗引用
 *
 * 注意：
 *   - 手动删除迭代器以避免迭代器失效
 *   - 使用CanBeginCombat验证战斗条件
 */
void CombatManager::RevalidateCombat()
{
    // 验证PvE战斗引用
    auto it = _pveRefs.begin(), end = _pveRefs.end();
    while (it != end)
    {
        CombatReference* const ref = it->second;
        if (!CanBeginCombat(_owner, ref->GetOther(_owner)))
        {
            // 手动擦除以避免迭代器失效
            it = _pveRefs.erase(it), end = _pveRefs.end();
            ref->EndCombat();
        }
        else
            ++it;
    }

    // 验证PvP战斗引用
    auto it2 = _pvpRefs.begin(), end2 = _pvpRefs.end();
    while (it2 != end2)
    {
        CombatReference* const ref = it2->second;
        if (!CanBeginCombat(_owner, ref->GetOther(_owner)))
        {
            // 手动擦除以避免迭代器失效
            it2 = _pvpRefs.erase(it2), end2 = _pvpRefs.end();
            ref->EndCombat();
        }
        else
            ++it2;
    }
}

/**
 * @brief 结束所有PvP战斗
 *
 * 职责：
 *   终止当前单位与所有敌人的PvP战斗关系。
 *   这会逐个结束所有PvP战斗引用。
 */
void CombatManager::EndAllPvPCombat()
{
    // 结束所有PvP战斗引用
    while (!_pvpRefs.empty())
        _pvpRefs.begin()->second->EndCombat();
}

/**
 * @brief 通知AI进入战斗（静态方法）
 *
 * 职责：
 *   通知单位的AI已经进入与目标的战斗。
 *   这是一个辅助函数，用于在建立战斗关系后通知AI。
 *
 * @param me 进入战斗的单位
 * @param other 战斗的目标单位
 */
/*static*/ void CombatManager::NotifyAICombat(Unit* me, Unit* other)
{
    if (UnitAI* ai = me->GetAI())
        ai->JustEnteredCombat(other);
}

/**
 * @brief 插入战斗引用到管理器
 *
 * 职责：
 *   将战斗引用插入到本单位的战斗引用映射表中。
 *   根据战斗类型（PvP或PvE）插入到不同的映射表。
 *
 * @param guid 战斗目标的GUID
 * @param ref 战斗引用指针
 *
 * 注意：
 *   - 会断言检查是否已存在相同引用，防止内存泄漏
 *   - PvP引用和PvE引用存储在不同的映射表中
 */
void CombatManager::PutReference(ObjectGuid const& guid, CombatReference* ref)
{
    if (ref->_isPvP)
    {
        auto& inMap = _pvpRefs[guid];
        ASSERT(!inMap, "Duplicate combat state at %p being inserted for %s vs %s - memory leak!", ref, _owner->GetGUID().ToString().c_str(), guid.ToString().c_str());
        inMap = static_cast<PvPCombatReference*>(ref);
    }
    else
    {
        auto& inMap = _pveRefs[guid];
        ASSERT(!inMap, "Duplicate combat state at %p being inserted for %s vs %s - memory leak!", ref, _owner->GetGUID().ToString().c_str(), guid.ToString().c_str());
        inMap = ref;
    }
}

/**
 * @brief 从管理器中清除战斗引用
 *
 * 职责：
 *   从战斗引用映射表中移除指定GUID的战斗引用。
 *   这是EndCombat流程的一部分。
 *
 * @param guid 要清除的战斗目标GUID
 * @param pvp 是否为PvP战斗引用
 */
void CombatManager::PurgeReference(ObjectGuid const& guid, bool pvp)
{
    if (pvp)
        _pvpRefs.erase(guid);
    else
        _pveRefs.erase(guid);
}

/**
 * @brief 更新拥有者的战斗状态
 *
 * 职责：
 *   根据当前战斗引用情况更新单位的战斗状态标志。
 *   如果战斗状态发生变化，会设置或移除UNIT_FLAG_IN_COMBAT标志，
 *   并调用相应的进入/退出战斗回调函数。
 *
 * @return true 如果战斗状态发生了改变
 * @return false 如果战斗状态未改变
 *
 * 主要流程：
 *   1. 检查当前是否有战斗（HasCombat）
 *   2. 与当前战斗状态标志比较
 *   3. 如果状态改变：
 *      a. 进入战斗：设置标志、调用AtEnterCombat、非Unit类型调用AtEngage
 *      b. 退出战斗：移除标志、调用AtExitCombat、非Unit类型调用AtDisengage
 *   4. 更新主人（如果是宠物/召唤物）的宠物战斗状态
 *
 * 注意：
 *   - 状态改变时会触发相关回调和事件
 *   - 主人的宠物战斗状态需要同步更新
 */
bool CombatManager::UpdateOwnerCombatState() const
{
    // 检查当前是否应该处于战斗状态
    bool const combatState = HasCombat();

    // 如果状态未改变，直接返回false
    if (combatState == _owner->IsInCombat())
        return false;

    if (combatState)
    {
        // 进入战斗：设置战斗标志
        _owner->SetUnitFlag(UNIT_FLAG_IN_COMBAT);
        _owner->AtEnterCombat();

        // 如果不是Creature类型，调用AtEngage
        if (_owner->GetTypeId() != TYPEID_UNIT)
            _owner->AtEngage(GetAnyTarget());
    }
    else
    {
        // 退出战斗：移除战斗标志
        _owner->RemoveUnitFlag(UNIT_FLAG_IN_COMBAT);
        _owner->AtExitCombat();

        // 如果不是Creature类型，调用AtDisengage
        if (_owner->GetTypeId() != TYPEID_UNIT)
            _owner->AtDisengage();
    }

    // 如果有主人（宠物/召唤物），更新主人的宠物战斗状态
    if (Unit* master = _owner->GetCharmerOrOwner())
        master->UpdatePetCombatState();

    return true;
}
