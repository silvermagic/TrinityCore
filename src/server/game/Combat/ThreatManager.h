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
 * @file ThreatManager.h
 * @brief 威胁管理器模块 - 管理单位的威胁列表和仇恨目标选择
 *
 * 模块职责：
 *   ThreatManager 负责管理生物（Creature）的威胁列表，追踪所有对拥有者产生威胁的单位，
 *   并根据威胁值、嘲讽状态、在线状态等因素选择当前应该攻击的目标。
 *
 * 核心概念：
 *   1. ThreatReference（威胁引用）：表示一个单位对另一个单位的威胁关系
 *      - 存储在三个位置：被威胁单位的威胁列表、威胁来源的"威胁我"列表
 *      - 包含基础威胁值、临时修正、嘲讽状态、在线状态等信息
 *
 *   2. 威胁列表结构：
 *      - 未排序列表（unordered_map）：快速查找 O(1)
 *      - 排序列表（Fibonacci Heap）：高效获取最高威胁目标 O(1)
 *
 *   3. 目标状态类型：
 *      - OnlineState（在线状态）：
 *        * ONLINE：正常状态，目标有效且可攻击
 *        * SUPPRESSED：抑制状态，目标有免疫或控制效果，优先级降低
 *        * OFFLINE：离线状态，目标无效（不可见、已死亡、GM模式等）
 *
 *      - TauntState（嘲讽状态）：
 *        * TAUNT：嘲讽，强制攻击该目标
 *        * NONE：无嘲讽状态
 *        * DETAUNT：降嘲讽，降低目标优先级
 *
 *   4. 目标选择优先级（从高到低）：
 *      - 固定目标（Fixate）：强制攻击特定目标
 *      - 在线状态：ONLINE > SUPPRESSED > OFFLINE
 *      - 嘲讽状态：TAUNT > NONE > DETAUNT
 *      - 威胁值：高威胁优先
 *      - 距离因素：近战范围内有额外加成
 *
 * 主要功能：
 *   - 威胁值管理：添加、缩放、重置威胁值
 *   - 目标选择：根据优先级选择当前攻击目标
 *   - 嘲讽系统：处理嘲讽光环和降嘲讽效果
 *   - 威胁转移：将威胁转移给其他目标（如误导、盗贼的佯攻）
 *   - 客户端同步：定期向客户端发送威胁列表更新
 *
 * 与战斗系统的关系：
 *   - 威胁 => 战斗是强保证：添加威胁会自动创建战斗引用
 *   - 结束战斗会自动清除威胁引用
 *   - 只有有战斗关系的单位才可能有威胁关系
 *
 * 性能优化：
 *   - 使用 Fibonacci Heap 实现排序列表，插入/删除效率 O(log n)
 *   - 单学校威胁修正预计算，多学校按需计算并缓存
 *   - 定期更新机制（每1秒），避免频繁更新
 *
 * 使用注意事项：
 *   - 只有 Creature 可以拥有威胁列表（宠物、图腾、触发器除外）
 *   - 威胁列表的迭代器可能在修改时失效，需要谨慎处理
 *   - AI 通知延迟处理，确保状态一致性
 */

 #ifndef TRINITY_THREATMANAGER_H
 #define TRINITY_THREATMANAGER_H

#include "Common.h"
#include "IteratorPair.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

class Creature;
class Unit;
class SpellInfo;

/********************************************************************************************************************************************************\
 *                                                           DEV DOCUMENTATION: THREAT SYSTEM                                                           *
 *                                            (future devs: please keep this up-to-date if you change the system)                                       *
 * The threat system works based on dynamically allocated threat list entries.                                                                          *
 *                                                                                                                                                      *
 * Each such entry is a ThreatReference object, which is always stored in exactly three places:                                                         *
 *  - The threatened unit's (from now: reference "owner") sorted and unsorted threat lists                                                              *
 *  - The threatening unit's (from now: reference "victim") threatened-by-me list                                                                       *
 * A ThreatReference object carries the following implicit guarantees:                                                                                  *
 *  - Both owner and victim are valid units, which are currently in the world. Neither can be nullptr.                                                  *
 *  - There is an active combat reference between owner and victim.                                                                                     *
 *                                                                                                                                                      *
 * Note that (threat => combat) is a strong guarantee provided in conjunction with CombatManager. Thus:                                                 *
 *  - Adding threat will also create a combat reference between the units if one doesn't exist yet (even if the owner can't have a threat list!)        *
 *  - Ending combat between two units will also delete any threat references that may exist between them.                                               *
 *                                                                                                                                                      *
 * To manage a creature's threat list, ThreatManager maintains a heap of threat reference const pointers.                                               *
 * This heap is kept well-structured in all methods that modify ThreatReference, and is used to select the next target.                                 *
 *                                                                                                                                                      *
 * Selection uses the following properties on ThreatReference, in order:                                                                                *
 * - Online state (one of ONLINE, SUPPRESSED, OFFLINE):                                                                                                 *
 *   - ONLINE:     Normal threat state, target is valid and attackable                                                                                  *
 *   - SUPPRESSED: Target is attackable, but inopportune. This is used for targets under immunity effects and damage-breaking CC.                       *
 *                 Targets with SUPPRESSED threat can still be valid targets, but any target with ONLINE threat will be preferred.                      *
 *   - OFFLINE:    The target is, for whatever reason, not valid at this time (for example, IMMUNE_TO_X flags or game master state).                    *
 *                 These targets can never be selected, and GetCurrentVictim will return nullptr if all targets are OFFLINE (typically causing evade).  *
 *   - Related methods: GetOnlineState, IsOnline, IsAvailable, IsOffline                                                                                *
 * - Taunt state (one of TAUNT, NONE, DETAUNT), the names speak for themselves                                                                          *
 *   - Related methods: GetTauntState, IsTaunting, IsDetaunted                                                                                          *
 * - Actual threat value (GetThreat)                                                                                                                    *
 *                                                                                                                                                      *
 * The current (= last selected) victim can be accessed using GetCurrentVictim.                                                                         *
 * Beyond that, ThreatManager has a variety of helpers and notifiers, which are documented inline below.                                                *
 *                                                                                                                                                      *
 * SPECIAL NOTE: Please be aware that any iterator may be invalidated if you modify a ThreatReference. The heap holds const pointers for a reason, but  *
 *                 that doesn't mean you're scot free. A variety of actions (casting spells, teleporting units, and so forth) can cause changes to      *
 *                 the threat list. Use with care - or default to GetModifiableThreatList(), which inherently copies entries.                           *
\********************************************************************************************************************************************************/

class ThreatReference;
struct CompareThreatLessThan
{
    CompareThreatLessThan() {}
    bool operator()(ThreatReference const* a, ThreatReference const* b) const;
};

/**
 * @brief 威胁管理器类 - 管理单位的威胁列表和威胁值
 *
 * ThreatManager 负责管理生物（Creature）的威胁列表，追踪所有对拥有者产生威胁的单位。
 * 主要功能包括：
 * - 维护威胁列表（按威胁值排序）
 * - 计算和更新威胁值
 * - 选择当前目标（最高威胁的单位）
 * - 处理嘲讽、威胁转移等特殊机制
 * - 同步威胁状态到客户端
 *
 * 威胁系统工作原理：
 * - 使用动态分配的 ThreatReference 对象存储威胁列表条目
 * - 每个 ThreatReference 存储在三个位置：
 *   1. 被威胁单位（owner）的已排序和未排序威胁列表
 *   2. 威胁来源单位（victim）的"威胁我"列表
 * - 威胁和战斗是关联的：添加威胁会创建战斗引用，结束战斗会删除威胁引用
 *
 * 目标选择优先级（从高到低）：
 * 1. 在线状态（ONLINE > SUPPRESSED > OFFLINE）
 * 2. 嘲讽状态（TAUNT > NONE > DETAUNT）
 * 3. 实际威胁值
 */
// Please check Game/Combat/ThreatManager.h for documentation on how this class works!
class TC_GAME_API ThreatManager
{
    public:
        class Heap;                    // 威胁列表堆结构的前向声明
        class ThreatListIterator;      // 威胁列表迭代器的前向声明

        /**
         * @brief 威胁列表更新间隔（毫秒）
         *
         * 每隔 1000ms（1秒）更新一次威胁列表和当前目标。
         * 这个间隔平衡了性能和响应性，避免频繁的威胁计算。
         */
        static const uint32 THREAT_UPDATE_INTERVAL = 1000u;

        /**
         * @brief 检查单位是否可以拥有威胁列表
         *
         * 只有非宠物、非图腾、非触发器的 Creature 才能拥有威胁列表。
         * 玩家召唤的仆从也不能拥有威胁列表。
         *
         * @param who 要检查的单位
         * @return true 如果该单位可以拥有威胁列表
         */
        static bool CanHaveThreatList(Unit const* who);

        /**
         * @brief 构造函数
         * @param owner 拥有此威胁管理器的单位（不能为 nullptr）
         */
        ThreatManager(Unit* owner);

        /**
         * @brief 析构函数
         *
         * 销毁威胁管理器时，会断言检查所有威胁列表是否已清空，
         * 如果还有未清理的威胁引用，会输出错误信息。
         */
        ~ThreatManager();

        /**
         * @brief 初始化威胁管理器
         *
         * 从 ::Create 方法中调用，在构造完成后调用（此时拥有者的所有字段已填充）。
         * 不应从其他地方调用。
         *
         * 初始化内容包括：
         * - 设置拥有者是否可以拥有威胁列表
         */
        void Initialize();

        /**
         * @brief 更新威胁管理器
         *
         * 从 Creature::Update 调用（只有生物才能拥有威胁列表）。
         * 不应从其他地方调用。
         *
         * @param tdiff 自上次更新以来经过的时间（毫秒）
         *
         * 主要功能：
         * - 定时更新当前目标（每1秒）
         * - 发送威胁列表更新到客户端
         */
        void Update(uint32 tdiff);

        /**
         * @brief 获取拥有者单位
         * @return 拥有者单位指针，永远不为 nullptr
         */
        Unit* GetOwner() const { return _owner; }

        /**
         * @brief 拥有者是否可以拥有威胁列表
         * @return 等同于 ThreatManager::CanHaveThreatList(GetOwner())
         */
        bool CanHaveThreatList() const { return _ownerCanHaveThreatList; }

        /**
         * @brief 获取当前仇恨目标
         *
         * 返回威胁值最高的有效目标。如果威胁列表为空或仅有离线目标，则返回 nullptr。
         * 该方法会自动更新威胁状态，确保返回有效的目标。
         *
         * @return 当前仇恨目标指针，如果没有有效目标则返回 nullptr
         *
         * 调用时机：AI 需要选择攻击目标时调用
         * 性能注意：可能触发威胁列表更新，不要频繁调用
         */
        Unit* GetCurrentVictim();

        /**
         * @brief 获取上一个仇恨目标
         *
         * 返回最后选择的仇恨目标，即使该目标当前可能已离线。
         *
         * @return 上一个仇恨目标指针，如果没有则返回 nullptr
         */
        Unit* GetLastVictim() const;

        /**
         * @brief 获取任意一个有效的威胁目标
         *
         * 从威胁列表中返回任意一个非离线的目标。
         * 通常用于快速检查是否有可攻击的目标。
         *
         * @return 任意一个有效目标指针，如果没有则返回 nullptr
         */
        Unit* GetAnyTarget() const;

        /**
         * @brief 检查威胁列表是否为空
         *
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果威胁列表为空（或没有在线目标）
         */
        bool IsThreatListEmpty(bool includeOffline = false) const;

        /**
         * @brief 检查指定单位是否在我们的威胁列表中
         *
         * @param who 目标单位的 GUID
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果该单位在我们的威胁列表中
         */
        bool IsThreatenedBy(ObjectGuid const& who, bool includeOffline = false) const;

        /**
         * @brief 检查指定单位是否在我们的威胁列表中
         *
         * @param who 目标单位
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果该单位在我们的威胁列表中
         */
        bool IsThreatenedBy(Unit const* who, bool includeOffline = false) const;

        /**
         * @brief 获取指定单位的威胁值
         *
         * @param who 目标单位
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return 该单位的威胁值，如果不存在则返回 0.0f
         */
        float GetThreat(Unit const* who, bool includeOffline = false) const;

        /**
         * @brief 获取威胁列表的大小
         * @return 威胁列表中的条目数量
         */
        size_t GetThreatListSize() const;

        /**
         * @brief 获取未排序的威胁列表（最快的遍历方式）
         *
         * 返回的迭代器对用于遍历威胁列表，顺序是任意的（unordered_map 的顺序）。
         * 适用于不需要排序的遍历场景。
         *
         * @return 未排序威胁列表的迭代器对
         *
         * 注意：
         * - 迭代器在添加/删除威胁列表条目时会失效
         * - 比 GetSortedThreatList 稍微不那么容易失效
         */
        Trinity::IteratorPair<ThreatListIterator, std::nullptr_t> GetUnsortedThreatList() const;

        /**
         * @brief 获取排序的威胁列表（稍慢但有序）
         *
         * 返回按威胁值排序的威胁列表迭代器。
         * 只在需要排序属性时使用。
         *
         * @return 排序威胁列表的迭代器对
         *
         * 注意：
         * - 迭代器在任何修改（甚至是间接修改）威胁列表时都会失效
         * - 施法等操作可能导致迭代器失效
         * - 当前坦克不保证是列表中的第一个条目，需要单独检查 GetLastVictim
         */
        Trinity::IteratorPair<ThreatListIterator, std::nullptr_t> GetSortedThreatList() const;

        /**
         * @brief 获取可修改的威胁列表（最慢但可修改）
         *
         * 返回一个可修改的威胁引用向量，同时是排序的。
         * 用于需要修改威胁引用的场景。
         *
         * @return 可修改的威胁引用向量（已排序）
         *
         * 注意：这是三种获取方式中最慢的，因为需要复制整个列表
         */
        std::vector<ThreatReference*> GetModifiableThreatList();

        /**
         * @brief 检查是否有任何单位威胁我们
         *
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果有任何单位威胁我们
         */
        bool IsThreateningAnyone(bool includeOffline = false) const;

        /**
         * @brief 检查我们是否威胁指定单位
         *
         * @param who 目标单位的 GUID
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果我们威胁该单位
         */
        bool IsThreateningTo(ObjectGuid const& who, bool includeOffline = false) const;

        /**
         * @brief 检查我们是否威胁指定单位
         *
         * @param who 目标单位
         * @param includeOffline 是否包含离线目标（默认 false）
         * @return true 如果我们威胁该单位
         */
        bool IsThreateningTo(Unit const* who, bool includeOffline = false) const;

        /**
         * @brief 获取"威胁我"列表（我们威胁的其他单位的列表）
         * @return "威胁我"列表的常量引用
         */
        auto const& GetThreatenedByMeList() const { return _threatenedByMe; }

        /**
         * @brief 评估抑制状态
         *
         * 通知威胁管理器，其拥有者现在可能在其他单位的威胁列表上被抑制
         * （免疫或伤害可破坏的控制效果被施加）。
         *
         * @param canExpire 是否允许抑制状态过期（默认 false）
         */
        void EvaluateSuppressed(bool canExpire = false);
        ///== AFFECT MY THREAT LIST == 影响我的威胁列表的方法 ==

        /**
         * @brief 添加威胁值到指定目标
         *
         * 这是威胁系统的核心函数，用于增加目标单位的威胁值。
         *
         * @param target 目标单位（威胁来源）
         * @param amount 基础威胁值增加量
         * @param spell 触发威胁的法术信息（可为 nullptr）
         * @param ignoreModifiers 是否忽略威胁修正（如法术效果、光环等）
         * @param ignoreRedirects 是否忽略威胁转移效果
         *
         * 主要处理流程：
         * 1. 如果威胁引用不存在，创建新的 ThreatReference
         * 2. 应用威胁修正值（除非 ignoreModifiers 为 true）
         * 3. 处理威胁转移（除非 ignoreRedirects 为 true）
         * 4. 更新威胁列表排序
         * 5. 触发战斗状态（如果尚未在战斗中）
         *
         * 调用时机：造成伤害、治疗、使用仇恨技能时
         * 性能注意：可能导致威胁列表更新和客户端同步
         */
        void AddThreat(Unit* target, float amount, SpellInfo const* spell = nullptr, bool ignoreModifiers = false, bool ignoreRedirects = false);

        /**
         * @brief 缩放指定目标的威胁值
         *
         * @param target 目标单位
         * @param factor 缩放因子（1.0 = 不变，0.5 = 减半，2.0 = 翻倍）
         */
        void ScaleThreat(Unit* target, float factor);

        /**
         * @brief 按百分比修改目标威胁值
         *
         * @param target 目标单位
         * @param percent 百分比（例如 50 表示增加 50%，-30 表示减少 30%）
         */
        void ModifyThreatByPercent(Unit* target, int32 percent) { if (percent) ScaleThreat(target, 0.01f*float(100 + percent)); }

        /**
         * @brief 重置指定单位的威胁值为零
         *
         * @param target 目标单位
         */
        void ResetThreat(Unit* target) { ScaleThreat(target, 0.0f); }

        /**
         * @brief 将指定单位的威胁值设置为与威胁列表中最高条目相等
         *
         * @param target 目标单位
         */
        void MatchUnitThreatToHighestThreat(Unit* target);

        /**
         * @brief 更新嘲讽状态
         *
         * 通知威胁管理器我们有了新的嘲讽光环（或嘲讽光环过期）。
         * 会重新评估所有威胁引用的嘲讽状态。
         */
        void TauntUpdate();

        /**
         * @brief 重置所有威胁
         *
         * 将拥有者威胁列表中所有威胁引用的威胁值设置为零。
         */
        void ResetAllThreat();

        /**
         * @brief 清除指定目标的威胁
         *
         * 从威胁列表中移除指定目标。
         *
         * @param target 要移除的目标
         */
        void ClearThreat(Unit* target);

        /**
         * @brief 清除指定威胁引用
         *
         * @param ref 要清除的威胁引用
         */
        void ClearThreat(ThreatReference* ref);

        /**
         * @brief 清除所有威胁列表
         *
         * 移除所有目标（会导致 UpdateVictim 触发脱战）。
         * 通常在生物脱战或死亡时调用。
         */
        void ClearAllThreat();

        /**
         * @brief 固定攻击特定目标
         *
         * 强制攻击指定目标，直到固定被清除。
         * 如果目标不在威胁列表中，则不做任何操作。
         *
         * @param target 要固定的目标（nullptr 表示清除固定）
         */
        void FixateTarget(Unit* target);

        /**
         * @brief 清除固定目标
         */
        void ClearFixate() { FixateTarget(nullptr); }

        /**
         * @brief 获取固定目标
         *
         * @return 固定目标指针，如果没有则返回 nullptr
         */
        Unit* GetFixateTarget() const;

        ///== AFFECT OTHERS' THREAT LISTS == 影响其他人威胁列表的方法 ==

        /**
         * @brief 为协助我的单位转发威胁
         *
         * 对所有威胁我们的单位调用 AddThreat，参数为指定的协助单位。
         * 用于处理宠物、图腾等协助单位的威胁。
         *
         * @param assistant 协助单位
         * @param baseAmount 基础威胁值
         * @param spell 触发威胁的法术信息（可为 nullptr）
         * @param ignoreModifiers 是否忽略威胁修正（默认 false）
         */
        void ForwardThreatForAssistingMe(Unit* assistant, float baseAmount, SpellInfo const* spell = nullptr, bool ignoreModifiers = false);

        /**
         * @brief 从所有威胁列表中移除自己
         *
         * 删除所有 victim == owner 的威胁引用。
         * 通常在单位死亡或脱战时调用。
         */
        void RemoveMeFromThreatLists();

        /**
         * @brief 更新我的临时威胁修正值
         *
         * 重新计算来自自身光环的临时威胁修正值（SPELL_AURA_MOD_TOTAL_THREAT）。
         */
        void UpdateMyTempModifiers();

        /**
         * @brief 更新我的法术学校威胁修正值
         *
         * 重新计算 SPELL_AURA_MOD_THREAT 修正值。
         */
        void UpdateMySpellSchoolModifiers();

        ///== REDIRECT SYSTEM == 威胁重定向系统 ==

        /**
         * @brief 注册威胁重定向效果
         *
         * 注册一个重定向效果，将拥有者产生的 pct% 威胁重定向到 victim。
         *
         * @param spellId 法术 ID
         * @param victim 重定向目标
         * @param pct 重定向百分比（0-100）
         */
        void RegisterRedirectThreat(uint32 spellId, ObjectGuid const& victim, uint32 pct);

        /**
         * @brief 取消所有目标的威胁重定向
         *
         * @param spellId 法术 ID
         */
        void UnregisterRedirectThreat(uint32 spellId);

        /**
         * @brief 取消特定目标的威胁重定向
         *
         * @param spellId 法术 ID
         * @param victim 重定向目标
         */
        void UnregisterRedirectThreat(uint32 spellId, ObjectGuid const& victim);

    private:
        Unit* const _owner;                                                 // 拥有者单位（该威胁管理器所属的单位），永远不为 nullptr
        bool _ownerCanHaveThreatList;                                       // 拥有者是否可以拥有威胁列表（只有生物才能拥有威胁列表）

        /**
         * @brief 威胁比较函数对象（用于堆排序）
         */
        static const CompareThreatLessThan CompareThreat;

        /**
         * @brief 比较两个威胁引用的大小
         *
         * @param a 第一个威胁引用
         * @param b 第二个威胁引用
         * @param aWeight 第一个引用的权重（用于 110%/130% 规则）
         * @return true 如果 a 的威胁小于 b（按优先级排序）
         */
        static bool CompareReferencesLT(ThreatReference const* a, ThreatReference const* b, float aWeight);

        /**
         * @brief 计算修正后的威胁值
         *
         * 应用法术威胁修正、学校威胁修正等。
         *
         * @param threat 基础威胁值
         * @param victim 目标单位
         * @param spell 触发威胁的法术（可为 nullptr）
         * @return 修正后的威胁值
         */
        static float CalculateModifiedThreat(float threat, Unit const* victim, SpellInfo const* spell);

        //== 发送消息到客户端（仅用于自己的威胁列表） ==

        /**
         * @brief 发送清除所有威胁的消息到客户端
         */
        void SendClearAllThreatToClients() const;

        /**
         * @brief 发送移除指定目标的消息到客户端
         *
         * @param victim 要移除的目标
         */
        void SendRemoveToClients(Unit const* victim) const;

        /**
         * @brief 发送威胁列表更新到客户端
         *
         * @param newHighest 是否有新的最高威胁目标
         */
        void SendThreatListToClients(bool newHighest) const;

        ///== MY THREAT LIST == 我的威胁列表管理方法 ==

        /**
         * @brief 插入威胁引用到我的威胁列表
         *
         * @param guid 目标单位的 GUID
         * @param ref 威胁引用指针
         */
        void PutThreatListRef(ObjectGuid const& guid, ThreatReference* ref);

        /**
         * @brief 从我的威胁列表中清除威胁引用
         *
         * @param guid 目标单位的 GUID
         */
        void PurgeThreatListRef(ObjectGuid const& guid);

        bool _needClientUpdate;                                             // 是否需要向客户端发送威胁列表更新
        uint32 _updateTimer;                                                // 更新计时器，用于定期更新威胁列表（THREAT_UPDATE_INTERVAL = 1000ms）
        std::unique_ptr<Heap> _sortedThreatList;                            // 按威胁值排序的威胁列表堆结构（Fibonacci Heap），用于快速选择最高威胁目标
        std::unordered_map<ObjectGuid, ThreatReference*> _myThreatListEntries; // 拥有者的威胁列表（未排序），键为目标单位的 GUID

        /**
         * @brief 处理 AI 更新通知
         *
         * AI 通知被延迟处理，以确保在调用任意逻辑之前我们处于一致状态。
         * 威胁引用可能在调用 ::UpdateOffline() 时注册到这里，
         * 务必在退出威胁管理器逻辑之前处理此列表。
         */
        void ProcessAIUpdates();

        /**
         * @brief 注册需要 AI 更新的目标
         *
         * @param guid 目标单位的 GUID
         */
        void RegisterForAIUpdate(ObjectGuid const& guid) { _needsAIUpdate.push_back(guid); }

        std::vector<ObjectGuid> _needsAIUpdate;                             // 需要 AI 更新的目标 GUID 列表

        /**
         * @brief 更新当前仇恨目标
         *
         * 从 ::Update 定期调用，重新选择当前应该攻击的目标。
         */
        void UpdateVictim();

        /**
         * @brief 重新选择仇恨目标
         *
         * 根据威胁值、嘲讽状态、距离等因素重新选择目标。
         *
         * @return 新的仇恨目标引用，如果没有则返回 nullptr
         */
        ThreatReference const* ReselectVictim();

        ThreatReference const* _currentVictimRef;                           // 当前目标的威胁引用（最后一次选择的目标）
        ThreatReference const* _fixateRef;                                  // 固定目标的威胁引用（用于强制攻击特定目标）

        ///== OTHERS' THREAT LISTS == 其他人的威胁列表管理方法 ==

        /**
         * @brief 插入"威胁我"引用
         *
         * @param guid 被威胁单位的 GUID
         * @param ref 威胁引用指针
         */
        void PutThreatenedByMeRef(ObjectGuid const& guid, ThreatReference* ref);

        /**
         * @brief 清除"威胁我"引用
         *
         * @param guid 被威胁单位的 GUID
         */
        void PurgeThreatenedByMeRef(ObjectGuid const& guid);

        std::unordered_map<ObjectGuid, ThreatReference*> _threatenedByMe;   // "威胁我"列表，这些引用是我们在其他单位威胁列表中的条目
        std::array<float, MAX_SPELL_SCHOOL> _singleSchoolModifiers;         // 单法术学校威胁修正值，预先计算并存储（大多数法术是单学校）
        mutable std::unordered_map<std::underlying_type<SpellSchoolMask>::type, float> _multiSchoolModifiers; // 多法术学校威胁修正值，按需计算

        ///== REDIRECT SYSTEM == 威胁重定向系统 ==

        /**
         * @brief 更新威胁重定向信息
         *
         * 从注册表中更新当前的重定向目标和百分比。
         * 这个系统比较笨拙，因为没有任何重定向法术具有相关的光环效果，
         * 所以法术脚本需要手动处理。
         */
        void UpdateRedirectInfo();

        std::vector<std::pair<ObjectGuid, uint32>> _redirectInfo;           // 当前重定向目标和百分比列表，从注册表更新
        std::unordered_map<uint32, std::unordered_map<ObjectGuid, uint32>> _redirectRegistry; // 法术ID -> (目标 -> 百分比)；我们身上的所有重定向效果

    public:
        ThreatManager(ThreatManager const&) = delete;
        ThreatManager& operator=(ThreatManager const&) = delete;

        class ThreatListIterator
        {
        private:
            std::function<ThreatReference const* ()> _generator;
            ThreatReference const* _current;

            friend ThreatManager;
            explicit ThreatListIterator(std::function<ThreatReference const* ()>&& generator)
                : _generator(std::move(generator)), _current(_generator()) {}

        public:
            ThreatReference const* operator*() const { return _current; }
            ThreatReference const* operator->() const { return _current; }
            ThreatListIterator& operator++() { _current = _generator(); return *this; }
            bool operator==(ThreatListIterator const& o) const { return _current == o._current; }
            bool operator!=(ThreatListIterator const& o) const { return _current != o._current; }
            bool operator==(std::nullptr_t) const { return _current == nullptr; }
            bool operator!=(std::nullptr_t) const { return _current != nullptr; }
        };

    friend class ThreatReference;
    friend class ThreatReferenceImpl;
    friend struct CompareThreatLessThan;
    friend class debug_commandscript;
};

/**
 * @brief 威胁引用类 - 表示单个威胁列表条目
 *
 * ThreatReference 是威胁系统的核心数据结构，表示一个单位对另一个单位的威胁关系。
 * 每个 ThreatReference 对象存储在三个位置：
 * 1. 被威胁单位（owner）的已排序和未排序威胁列表
 * 2. 威胁来源单位（victim）的"威胁我"列表
 *
 * 威胁引用的生命周期：
 * - 当单位A对单位B产生威胁时创建
 * - 当两个单位之间的战斗状态结束时销毁
 *
 * 状态类型：
 * - 在线状态（OnlineState）：ONLINE（正常）、SUPPRESSED（抑制）、OFFLINE（离线）
 * - 嘲讽状态（TauntState）：TAUNT（嘲讽）、NONE（无）、DETAUNT（降嘲讽）
 */
// Please check Game/Combat/ThreatManager.h for documentation on how this class works!
class TC_GAME_API ThreatReference
{
    public:
        enum TauntState : uint32 { TAUNT_STATE_DETAUNT = 0, TAUNT_STATE_NONE = 1, TAUNT_STATE_TAUNT = 2 };
        enum OnlineState { ONLINE_STATE_ONLINE = 2, ONLINE_STATE_SUPPRESSED = 1, ONLINE_STATE_OFFLINE = 0 };

        Creature* GetOwner() const { return _owner; }  // 获取拥有者（被威胁的生物）
        Unit* GetVictim() const { return _victim; }  // 获取威胁来源单位
        float GetThreat() const { return std::max<float>(_baseAmount + (float)_tempModifier, 0.0f); }  // 获取当前威胁值（基础威胁 + 临时修正，最小为0）
        OnlineState GetOnlineState() const { return _online; }  // 获取在线状态
        bool IsOnline() const { return (_online >= ONLINE_STATE_ONLINE); }  // 是否在线（正常状态）
        bool IsAvailable() const { return (_online > ONLINE_STATE_OFFLINE); }  // 是否可用（在线或抑制状态）
        bool IsSuppressed() const { return (_online == ONLINE_STATE_SUPPRESSED); }  // 是否被抑制（目标不可攻击但优先级低于在线目标）
        bool IsOffline() const { return (_online <= ONLINE_STATE_OFFLINE); }  // 是否离线（目标无效）
        TauntState GetTauntState() const { return IsTaunting() ? TAUNT_STATE_TAUNT : _taunted; }  // 获取嘲讽状态
        bool IsTaunting() const { return _taunted >= TAUNT_STATE_TAUNT; }  // 是否嘲讽中
        bool IsDetaunted() const { return _taunted == TAUNT_STATE_DETAUNT; }  // 是否被降嘲讽

        void AddThreat(float amount);
        void ScaleThreat(float factor);
        void ModifyThreatByPercent(int32 percent) { if (percent) ScaleThreat(0.01f*float(100 + percent)); }
        void UpdateOffline();

        void ClearThreat(); // dealloc's this

    protected:
        static bool FlagsAllowFighting(Unit const* a, Unit const* b);

        explicit ThreatReference(ThreatManager* mgr, Unit* victim) :
            _owner(reinterpret_cast<Creature*>(mgr->_owner)), _mgr(*mgr), _victim(victim),
            _baseAmount(0.0f), _tempModifier(0), _taunted(TAUNT_STATE_NONE)
        {
            _online = ONLINE_STATE_OFFLINE;
        }

        virtual ~ThreatReference() = default;

        void UnregisterAndFree();

        bool ShouldBeOffline() const;
        bool ShouldBeSuppressed() const;
        void UpdateTauntState(TauntState state = TAUNT_STATE_NONE);
        Creature* const _owner;  // 拥有者（被威胁的生物），永远不为 nullptr
        ThreatManager& _mgr;  // 所属的威胁管理器引用
        void HeapNotifyIncreased();
        void HeapNotifyDecreased();
        Unit* const _victim;  // 威胁来源单位（目标），永远不为 nullptr
        OnlineState _online;  // 在线状态（ONLINE/SUPPRESSED/OFFLINE）
        float _baseAmount;  // 基础威胁值
        int32 _tempModifier; // Temporary effects (auras with SPELL_AURA_MOD_TOTAL_THREAT) - set from victim's threatmanager in ThreatManager::UpdateMyTempModifiers
                             // 临时威胁修正值（来自光环效果 SPELL_AURA_MOD_TOTAL_THREAT），从目标的威胁管理器中设置
        TauntState _taunted;  // 嘲讽状态（TAUNT/NONE/DETAUNT）

    public:
        ThreatReference(ThreatReference const&) = delete;
        ThreatReference& operator=(ThreatReference const&) = delete;

    friend class ThreatManager;
    friend struct CompareThreatLessThan;
};

inline bool CompareThreatLessThan::operator()(ThreatReference const* a, ThreatReference const* b) const { return ThreatManager::CompareReferencesLT(a, b, 1.0f); }

 #endif
