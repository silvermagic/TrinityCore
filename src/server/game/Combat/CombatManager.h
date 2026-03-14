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
 * @file CombatManager.h
 * @brief 战斗管理器模块 - 管理单位之间的战斗状态关系
 *
 * 模块职责：
 *   CombatManager 负责维护和管理单位之间的战斗状态关系。每个单位（Unit）都拥有一个
 *   CombatManager 实例，用于追踪该单位与哪些敌人处于战斗状态。
 *
 * 核心概念：
 *   1. CombatReference（战斗引用）：表示两个单位之间的战斗关系
 *      - 一个单位"处于战斗中"当且仅当其 CombatManager 中有至少一个非抑制的 CombatReference
 *      - 战斗引用是双向的，存在于双方单位的 CombatManager 中
 *
 *   2. 战斗类型：
 *      - PvE 战斗：玩家/宠物与 NPC/生物之间的战斗
 *      - PvP 战斗：玩家与玩家之间的战斗（有超时机制，默认5秒）
 *
 *   3. 战斗抑制机制：
 *      - 用于暂时性地"隐藏"战斗状态，例如消失、假死等技能
 *      - 被抑制的战斗引用不会生成战斗状态，但引用关系仍然存在
 *
 * 主要功能：
 *   - 建立和结束战斗关系（SetInCombatWith, EndCombat）
 *   - 查询战斗状态（IsInCombatWith, HasCombat）
 *   - 批量结束战斗（EndAllCombat, EndCombatBeyondRange）
 *   - 战斗状态抑制和恢复（SuppressFor, Refresh）
 *   - 继承战斗状态（宠物继承主人的战斗）
 *
 * 与威胁系统的关系：
 *   - 威胁（Threat）和战斗（Combat）是紧密关联的
 *   - 添加威胁会自动创建战斗引用（如果不存在）
 *   - 结束战斗会自动清除威胁引用
 *   - 这确保了威胁系统始终在有战斗关系的基础上运行
 *
 * 使用注意事项：
 *   - 所有战斗引用都是动态分配的，生命周期由 CombatManager 管理
 *   - 结束战斗会修改双方的 CombatRefs 映射，可能导致迭代器失效
 *   - 在遍历战斗引用时，要小心迭代器失效问题
 */

#ifndef TRINITY_COMBATMANAGER_H
#define TRINITY_COMBATMANAGER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <unordered_map>

class Unit;

/********************************************************************************************************************************************************\
 *                                                           DEV DOCUMENTATION: COMBAT SYSTEM                                                           *
 *                                            (future devs: please keep this up-to-date if you change the system)                                       *
 * CombatManager maintains a list of dynamically allocated CombatReference entries. Each entry represents a combat state between two distinct units.    *
 * A unit is "in combat" iff it has one or more non-suppressed CombatReference entries in its CombatManager. No exceptions.                             *
 *                                                                                                                                                      *
 * A CombatReference object carries the following implicit guarantees by existing:                                                                      *
 *  - Both CombatReference.first and CombatReference.second are valid Units, distinct, not nullptr and currently in the world.                          *
 *  - If the CombatReference was retrieved from the CombatManager of Unit* A, then exactly one of .first and .second is equal to A.                     *
 *    - Note: Use CombatReference::GetOther to quickly get the other unit for a given reference.                                                        *
 *  - Both .first and .second are currently in combat (IsInCombat will always be true) if either of the following hold:                                 *
 *    - IsSuppressedFor returns false for the respective unit                                                                                           *
 *                                                                                                                                                      *
 * To end combat between two units, find their CombatReference and call EndCombat.                                                                      *
 *  - Keep in mind that this modifies the CombatRefs maps on both ends, which may cause iterators to be invalidated.                                    *
 *                                                                                                                                                      *
 * To put two units in combat with each other, call SetInCombatWith. Note that this is not guaranteed to succeed.                                       *
 *  - The return value of SetInCombatWith is the new combat state between the units (identical to calling IsInCombatWith at that time).                 *
 *                                                                                                                                                      *
 * Note that (threat => combat) is a strong guarantee provided in conjunction with ThreatManager. Thus:                                                 *
 *  - Ending combat between two units will also delete any threat references that may exist between them.                                               *
 *  - Adding threat will also create a combat reference between the units if one doesn't exist yet.                                                     *
\********************************************************************************************************************************************************/

// 请查看 Game/Combat/CombatManager.h 获取该类的详细文档说明！
// CombatReference 结构体：表示两个单位之间的战斗引用关系
// 该结构体承载以下隐式保证：
// - first 和 second 都是有效的 Unit 指针，互不相同，不为 nullptr，且当前存在于世界中
// - 如果从 Unit* A 的 CombatManager 中检索到此引用，则 .first 和 .second 中恰好有一个等于 A
// - 如果 IsSuppressedFor 对相应单位返回 false，则该单位当前处于战斗状态
struct TC_GAME_API CombatReference
{
    Unit* const first;                  // 战斗关系中的第一个单位
    Unit* const second;                 // 战斗关系中的第二个单位
    bool const _isPvP;                  // 是否为 PvP（玩家对玩家）战斗引用

    // 获取战斗关系中的另一个单位
    // @param me: 当前单位指针
    // @return: 返回战斗关系中另一个单位的指针
    Unit* GetOther(Unit const* me) const { return (first == me) ? second : first; }

    // 结束这两个单位之间的战斗
    // 注意：这会修改双方的 CombatRefs 映射，可能导致迭代器失效
    void EndCombat();

    // 抑制战斗引用 - 被抑制的战斗引用不会为关系的一侧生成战斗状态
    // 用于：消失、假死、已发射但尚未着陆的法术导弹
    // @param who: 要抑制战斗状态的单位
    void SuppressFor(Unit* who);

    // 检查指定单位的战斗状态是否被抑制
    // @param who: 要检查的单位
    // @return: 如果该单位的战斗状态被抑制则返回 true
    bool IsSuppressedFor(Unit const* who) const { return (who == first) ? _suppressFirst : _suppressSecond; }

    CombatReference(CombatReference const&) = delete;
    CombatReference& operator=(CombatReference const&) = delete;

protected:
    // 构造函数 - 创建战斗引用
    // @param a: 第一个单位
    // @param b: 第二个单位
    // @param pvp: 是否为 PvP 战斗，默认为 false
    CombatReference(Unit* a, Unit* b, bool pvp = false) : first(a), second(b), _isPvP(pvp) { }

    // 刷新战斗引用（重新激活被抑制的战斗状态）
    void Refresh();

    // 抑制指定单位的战斗状态
    // @param who: 要抑制的单位
    void Suppress(Unit* who) { (who == first ? _suppressFirst : _suppressSecond) = true; }

    bool _suppressFirst = false;        // 是否抑制第一个单位的战斗状态
    bool _suppressSecond = false;       // 是否抑制第二个单位的战斗状态

    friend class CombatManager;
};

// 请查看 Game/Combat/CombatManager.h 获取该类的详细文档说明！
// PvPCombatReference 结构体：表示 PvP（玩家对玩家）战斗引用
// 继承自 CombatReference，增加了战斗超时机制
struct TC_GAME_API PvPCombatReference : public CombatReference
{
    static const uint32 PVP_COMBAT_TIMEOUT = 5 * IN_MILLISECONDS;   // PvP 战斗超时时间（5秒）

private:
    // 构造函数 - 创建 PvP 战斗引用
    // @param first: 第一个单位
    // @param second: 第二个单位
    PvPCombatReference(Unit* first, Unit* second) : CombatReference(first, second, true) { }

    // 更新战斗引用状态
    // @param tdiff: 自上次更新以来经过的时间（毫秒）
    // @return: 如果战斗应该结束则返回 true
    bool Update(uint32 tdiff);

    // 刷新战斗计时器（重置为 PVP_COMBAT_TIMEOUT）
    void RefreshTimer();

    uint32 _combatTimer = PVP_COMBAT_TIMEOUT;   // 战斗计时器，倒计时结束后战斗自动结束

    friend class CombatManager;
};

// 请查看 Game/Combat/CombatManager.h 获取该类的详细文档说明！
// CombatManager 类：管理单位的战斗状态
// 该类维护一个动态分配的 CombatReference 条目列表，每个条目代表两个不同单位之间的战斗状态
// 一个单位"处于战斗中"当且仅当其 CombatManager 中有一个或多个非抑制的 CombatReference 条目
//
// 重要说明：
// - 结束两个单位之间的战斗需要找到它们的 CombatReference 并调用 EndCombat
//   注意：这会修改双方的 CombatRefs 映射，可能导致迭代器失效
// - 要让两个单位进入战斗，调用 SetInCombatWith，注意这并不保证成功
//   SetInCombatWith 的返回值是单位之间的新战斗状态
// - 威胁与战斗的关系：威胁 => 战斗 是一个强保证
//   结束两个单位之间的战斗也会删除它们之间可能存在的任何威胁引用
//   添加威胁也会在单位之间创建战斗引用（如果尚不存在）
class TC_GAME_API CombatManager
{
    public:
        // 检查两个单位是否可以开始战斗
        // @param a: 第一个单位
        // @param b: 第二个单位
        // @return: 如果可以开始战斗则返回 true
        static bool CanBeginCombat(Unit const* a, Unit const* b);

        // 构造函数
        // @param owner: 拥有此 CombatManager 的单位
        CombatManager(Unit* owner) : _owner(owner) { }

        // 析构函数 - 清理所有战斗引用
        ~CombatManager();

        // 更新战斗管理器状态 - 从 Unit::Update 调用
        // @param tdiff: 自上次更新以来经过的时间（毫秒）
        void Update(uint32 tdiff);

        // 获取拥有此 CombatManager 的单位
        // @return: 拥有者单位指针
        Unit* GetOwner() const { return _owner; }

        // 检查单位是否处于战斗状态（PvE 或 PvP）
        // @return: 如果处于任何战斗状态则返回 true
        bool HasCombat() const { return HasPvECombat() || HasPvPCombat(); }

        // 检查单位是否处于 PvE 战斗状态
        // @return: 如果处于 PvE 战斗则返回 true
        bool HasPvECombat() const;

        // 检查单位是否与玩家处于 PvE 战斗状态
        // @return: 如果与玩家处于战斗则返回 true
        bool HasPvECombatWithPlayers() const;

        // 获取所有 PvE 战斗引用
        // @return: PvE 战斗引用映射的常量引用
        std::unordered_map<ObjectGuid, CombatReference*> const& GetPvECombatRefs() const { return _pveRefs; }

        // 检查单位是否处于 PvP 战斗状态
        // @return: 如果处于 PvP 战斗则返回 true
        bool HasPvPCombat() const;

        // 获取所有 PvP 战斗引用
        // @return: PvP 战斗引用映射的常量引用
        std::unordered_map<ObjectGuid, PvPCombatReference*> const& GetPvPCombatRefs() const { return _pvpRefs; }

        // 如果单位处于战斗中，返回一个与其战斗的任意单位；否则返回 nullptr
        // @return: 战斗目标单位指针，如果没有战斗则返回 nullptr
        Unit* GetAnyTarget() const;

        // 将单位设置为与指定单位战斗
        // 返回值与调用后立即调用 IsInCombatWith 的结果相同
        // @param who: 要与之战斗的单位
        // @param addSecondUnitSuppressed: 是否以抑制状态添加第二个单位，默认为 false
        // @return: 返回新的战斗状态（true 表示正在战斗）
        bool SetInCombatWith(Unit* who, bool addSecondUnitSuppressed = false);

        // 检查是否与指定 GUID 的单位处于战斗状态
        // @param who: 单位的 GUID
        // @return: 如果处于战斗状态则返回 true
        bool IsInCombatWith(ObjectGuid const& who) const;

        // 检查是否与指定单位处于战斗状态
        // @param who: 单位指针
        // @return: 如果处于战斗状态则返回 true
        bool IsInCombatWith(Unit const* who) const;

        // 从指定单位继承所有战斗状态
        // @param who: 要继承战斗状态的单位
        void InheritCombatStatesFrom(Unit const* who);

        // 结束超出指定范围的所有战斗
        // @param range: 战斗范围
        // @param includingPvP: 是否包括 PvP 战斗，默认为 false
        void EndCombatBeyondRange(float range, bool includingPvP = false);

        // 抑制所有 PvP 战斗 - 在拥有者一侧标记所有 PvP 引用为抑制状态
        // 这些引用在被刷新前不会生成战斗状态
        void SuppressPvPCombat();

        // 结束所有 PvE 战斗
        void EndAllPvECombat();

        // 重新验证战斗状态（检查所有战斗引用的有效性）
        void RevalidateCombat();

        // 结束所有 PvP 战斗
        void EndAllPvPCombat();

        // 结束所有战斗（PvE 和 PvP）
        void EndAllCombat() { EndAllPvECombat(); EndAllPvPCombat(); }

        CombatManager(CombatManager const&) = delete;
        CombatManager& operator=(CombatManager const&) = delete;

    private:
        // 通知 AI 进入战斗
        // @param me: 当前单位
        // @param other: 战斗目标单位
        static void NotifyAICombat(Unit* me, Unit* other);

        // 添加战斗引用到管理器中
        // @param guid: 目标单位的 GUID
        // @param ref: 战斗引用指针
        void PutReference(ObjectGuid const& guid, CombatReference* ref);

        // 从管理器中移除战斗引用
        // @param guid: 目标单位的 GUID
        // @param pvp: 是否为 PvP 战斗引用
        void PurgeReference(ObjectGuid const& guid, bool pvp);

        // 更新拥有者的战斗状态
        // @return: 如果战斗状态发生变化则返回 true
        bool UpdateOwnerCombatState() const;

        Unit* const _owner;                                                 // 拥有此 CombatManager 的单位
        std::unordered_map<ObjectGuid, CombatReference*> _pveRefs;          // PvE 战斗引用映射（GUID -> CombatReference）
        std::unordered_map<ObjectGuid, PvPCombatReference*> _pvpRefs;       // PvP 战斗引用映射（GUID -> PvPCombatReference）

    friend struct CombatReference;
    friend struct PvPCombatReference;
};

#endif
