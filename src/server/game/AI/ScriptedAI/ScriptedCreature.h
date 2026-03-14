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

#ifndef TRINITY_SCRIPTEDCREATURE_H
#define TRINITY_SCRIPTEDCREATURE_H

/**
 * @file ScriptedCreature.h
 * @brief 脚本化生物头文件
 *
 * 本文件提供了脚本化生物AI系统的核心类和辅助功能，包括：
 * - SummonList: 召唤生物管理列表
 * - ScriptedAI: 脚本化AI基类
 * - BossAI: Boss AI基类（用于副本Boss）
 * - WorldBossAI: 世界Boss AI基类
 * - 各种辅助函数和谓词类
 *
 * 这些类为生物AI脚本提供了常用的功能封装，简化了Boss和特殊生物的脚本编写。
 */

#include "Creature.h"  // convenience include for scripts, all uses of ScriptedCreature also need Creature (except ScriptedCreature itself doesn't need Creature)
#include "CreatureAI.h"
#include "DBCEnums.h"
#include "TaskScheduler.h"

class InstanceScript;

/**
 * @class SummonList
 * @brief 召唤生物管理列表
 *
 * 用于管理和追踪生物召唤的其他生物。提供了召唤物的添加、移除、查找和批量操作功能。
 * 内部使用ObjectGuid列表存储召唤物引用。
 *
 * 使用示例：
 * @code
 * SummonList summons(me);
 * summons.Summon(summonedCreature);  // 添加召唤物
 * summons.DespawnEntry(12345);       // 消失特定ID的召唤物
 * summons.DespawnAll();              // 消失所有召唤物
 * @endcode
 */
class TC_GAME_API SummonList
{
public:
    typedef GuidList StorageType;              ///< 底层存储类型（ObjectGuid列表）
    typedef StorageType::iterator iterator;    ///< 迭代器类型
    typedef StorageType::const_iterator const_iterator;  ///< 常量迭代器类型
    typedef StorageType::size_type size_type;  ///< 大小类型
    typedef StorageType::value_type value_type; ///< 值类型

    /**
     * @brief 构造函数
     * @param creature 拥有此召唤列表的生物（通常是施法者）
     */
    explicit SummonList(Creature* creature) : _me(creature) { }

    // And here we see a problem of original inheritance approach. People started
    // to exploit presence of std::list members, so I have to provide wrappers

    /**
     * @brief 获取列表开始迭代器
     * @return 指向第一个元素的迭代器
     */
    iterator begin()
    {
        return _storage.begin();
    }

    /**
     * @brief 获取列表开始迭代器（常量版本）
     * @return 指向第一个元素的常量迭代器
     */
    const_iterator begin() const
    {
        return _storage.begin();
    }

    /**
     * @brief 获取列表结束迭代器
     * @return 指向末尾的迭代器
     */
    iterator end()
    {
        return _storage.end();
    }

    /**
     * @brief 获取列表结束迭代器（常量版本）
     * @return 指向末尾的常量迭代器
     */
    const_iterator end() const
    {
        return _storage.end();
    }

    /**
     * @brief 删除指定位置的元素
     * @param i 要删除的元素的迭代器
     * @return 指向被删除元素之后元素的迭代器
     */
    iterator erase(iterator i)
    {
        return _storage.erase(i);
    }

    /**
     * @brief 检查列表是否为空
     * @return 如果列表为空返回true，否则返回false
     */
    bool empty() const
    {
        return _storage.empty();
    }

    /**
     * @brief 获取列表中的元素数量
     * @return 列表中召唤物的数量
     */
    size_type size() const
    {
        return _storage.size();
    }

    /**
     * @brief 清空底层存储
     *
     * 注意：这只会清空存储列表，不会消失生物！
     * 如果需要消失所有生物，请使用 DespawnAll()
     */
    void clear()
    {
        _storage.clear();
    }

    /**
     * @brief 添加一个召唤物到列表
     * @param summon 要添加的召唤物指针
     */
    void Summon(Creature const* summon);

    /**
     * @brief 从列表中移除并消失指定的召唤物
     * @param summon 要消失的召唤物指针
     */
    void Despawn(Creature const* summon);

    /**
     * @brief 消失所有指定ID的召唤物
     * @param entry 生物模板ID（Creature ID）
     */
    void DespawnEntry(uint32 entry);

    /**
     * @brief 消失列表中的所有召唤物
     *
     * 会遍历列表并消失所有召唤物，然后清空列表
     */
    void DespawnAll();

    /**
     * @brief 根据谓词消失召唤物
     * @tparam T 谓词类型
     * @param predicate 判断条件（返回true则移除该召唤物）
     *
     * 使用示例：
     * @code
     * // 消失所有生命值低于50%的召唤物
     * summons.DespawnIf([](ObjectGuid guid) {
     *     Creature* creature = ObjectAccessor::GetCreature(*me, guid);
     *     return creature && creature->HealthBelowPct(50);
     * });
     * @endcode
     */
    template <typename T>
    void DespawnIf(T const& predicate)
    {
        _storage.remove_if(predicate);
    }

    /**
     * @brief 对满足条件的召唤物执行动作
     * @tparam Predicate 谓词类型
     * @param info 动作ID（传递给召唤物的AI）
     * @param predicate 筛选条件
     * @param max 最多执行多少个召唤物（0表示无限制）
     *
     * 使用示例：
     * @code
     * // 让所有特定ID的召唤物执行动作1
     * summons.DoAction(1, EntryCheckPredicate(12345));
     * @endcode
     */
    template <class Predicate>
    void DoAction(int32 info, Predicate&& predicate, uint16 max = 0)
    {
        // We need to use a copy of SummonList here, otherwise original SummonList would be modified
        StorageType listCopy;
        std::copy_if(std::begin(_storage), std::end(_storage), std::inserter(listCopy, std::end(listCopy)), predicate);
        DoActionImpl(info, listCopy, max);
    }

    /**
     * @brief 让召唤物进入战斗（对区域内的玩家）
     * @param entry 生物模板ID，0表示所有召唤物
     *
     * 使召唤物攻击附近的玩家，通常用于Boss战斗中召唤物加入战斗
     */
    void DoZoneInCombat(uint32 entry = 0);

    /**
     * @brief 移除列表中已不存在的召唤物
     *
     * 清理列表中那些已经被删除或消失的召唤物引用
     */
    void RemoveNotExisting();

    /**
     * @brief 检查列表中是否包含指定ID的召唤物
     * @param entry 生物模板ID
     * @return 如果存在返回true，否则返回false
     */
    bool HasEntry(uint32 entry) const;

private:
    /**
     * @brief 执行动作的实现函数
     * @param action 动作ID
     * @param summons 目标召唤物列表
     * @param max 最多执行数量
     */
    void DoActionImpl(int32 action, StorageType& summons, uint16 max);

    Creature* _me;          ///< 拥有此列表的生物
    StorageType _storage;   ///< 召唤物GUID存储列表
};

/**
 * @class EntryCheckPredicate
 * @brief 生物ID检查谓词
 *
 * 用于检查ObjectGuid是否匹配指定的生物模板ID。
 * 通常与SummonList的DoAction方法配合使用。
 *
 * 使用示例：
 * @code
 * // 对所有ID为12345的召唤物执行动作
 * summons.DoAction(ACTION_SOMETHING, EntryCheckPredicate(12345));
 * @endcode
 */
class TC_GAME_API EntryCheckPredicate
{
    public:
        /**
         * @brief 构造函数
         * @param entry 要匹配的生物模板ID
         */
        EntryCheckPredicate(uint32 entry) : _entry(entry) { }

        /**
         * @brief 检查GUID是否匹配指定ID
         * @param guid 要检查的ObjectGuid
         * @return 如果GUID的Entry匹配返回true，否则返回false
         */
        bool operator()(ObjectGuid guid) { return guid.GetEntry() == _entry; }

    private:
        uint32 _entry;  ///< 目标生物模板ID
};

/**
 * @class DummyEntryCheckPredicate
 * @brief 空检查谓词（总是返回true）
 *
 * 用于匹配所有召唤物，不进行任何过滤。
 * 通常与SummonList的DoAction方法配合使用。
 *
 * 使用示例：
 * @code
 * // 对所有召唤物执行动作
 * summons.DoAction(ACTION_SOMETHING, DummyEntryCheckPredicate());
 * @endcode
 */
class TC_GAME_API DummyEntryCheckPredicate
{
    public:
        /**
         * @brief 检查函数（总是返回true）
         * @param guid ObjectGuid（未使用）
         * @return 总是返回true
         */
        bool operator()(ObjectGuid) { return true; }
};

/**
 * @struct ScriptedAI
 * @brief 脚本化AI基类
 *
 * 这是所有自定义AI脚本的基类，继承自CreatureAI。
 * 提供了丰富的辅助函数和工具方法，用于简化AI脚本的编写。
 *
 * 主要功能包括：
 * - 战斗移动控制
 * - 威胁管理
 * - 法术施放
 * - 传送功能
 * - 友方单位查找
 * - 难度模式判断
 *
 * 使用示例：
 * @code
 * class MyBossAI : public ScriptedAI
 * {
 * public:
 *     MyBossAI(Creature* creature) : ScriptedAI(creature) { }
 *
 *     void UpdateAI(uint32 diff) override
 *     {
 *         if (!UpdateVictim())
 *             return;
 *
 *         // 自定义AI逻辑
 *         if (HealthBelowPct(50))
 *             DoCast(me, SPELL_ENRAGE);
 *
 *         DoMeleeAttackIfReady();
 *     }
 * };
 * @endcode
 */
struct TC_GAME_API ScriptedAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 拥有此AI的生物
         *
         * 初始化AI的基本属性，包括难度设置、英雄模式判断等
         */
        explicit ScriptedAI(Creature* creature);

        /**
         * @brief 虚析构函数
         */
        virtual ~ScriptedAI() { }

        // *************
        // CreatureAI Functions
        // *************

        /**
         * @brief 开始攻击但不移动
         * @param target 目标单位
         *
         * 与AttackStart不同，此方法会让生物开始攻击但不会移动追逐目标。
         * 适用于远程攻击者或固定位置的生物。
         */
        void AttackStartNoMove(Unit* target);

        /**
         * @brief 更新AI（每帧调用）
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 这是AI的主循环函数，每帧被调用一次。
         * 默认实现只执行基本的近战攻击。
         */
        virtual void UpdateAI(uint32 diff) override;

        // *************
        // Variables
        // *************

        /**
         * @brief 是否正在逃跑
         *
         * 用于标记生物是否处于逃跑状态。
         * 通常用于低生命值时的逃跑行为。
         */
        bool IsFleeing;

        // *************
        // Pure virtual functions
        // *************

        /**
         * @brief 开始攻击目标
         * @param target 目标单位
         *
         * 当生物开始攻击目标时调用。
         * 在JustEngagedWith之前调用，甚至在生物进入战斗之前。
         */
        void AttackStart(Unit* /*target*/) override;

        // *************
        // AI Helper Functions
        // *************

        /**
         * @brief 开始向目标移动
         * @param target 目标单位
         * @param distance 与目标的距离（默认为攻击距离）
         * @param angle 与目标的角度（弧度，默认为0）
         *
         * 让生物开始向目标移动，可以指定距离和角度。
         * 适用于需要保持特定距离或角度的战斗场景。
         */
        void DoStartMovement(Unit* target, float distance = 0.0f, float angle = 0.0f);

        /**
         * @brief 停止向目标移动
         * @param target 目标单位
         *
         * 停止生物向目标的移动，但保持攻击状态。
         * 用于需要生物原地攻击的场景。
         */
        void DoStartNoMovement(Unit* target);

        /**
         * @brief 停止攻击当前目标
         *
         * 让生物停止攻击当前目标，清除攻击目标。
         */
        void DoStopAttack();

        /**
         * @brief 施放法术
         * @param target 目标单位
         * @param spellInfo 法术信息
         * @param triggered 是否为触发法术（默认false）
         *
         * 使用指定的法术信息施放法术。
         * 触发法术不会消耗资源或触发冷却。
         */
        void DoCastSpell(Unit* target, SpellInfo const* spellInfo, bool triggered = false);

        /**
         * @brief 播放声音给附近所有玩家
         * @param source 声音源对象
         * @param soundId 声音ID
         *
         * 向source附近的所有玩家播放指定的声音。
         * 常用于Boss战斗中的语音提示。
         */
        void DoPlaySoundToSet(WorldObject* source, uint32 soundId);

        /**
         * @brief 直接添加威胁值（忽略重定向效果）
         * @param victim 目标单位
         * @param amount 威胁值数量
         * @param who 产生威胁的单位（默认为nullptr，表示当前生物）
         *
         * 直接向目标的威胁列表添加指定数量的威胁值。
         * 如果目标不在战斗中，会使其进入战斗并开始攻击。
         * 此方法忽略威胁重定向效果（如误导）。
         */
        void AddThreat(Unit* victim, float amount, Unit* who = nullptr);

        /**
         * @brief 按百分比修改威胁值
         * @param victim 目标单位
         * @param pct 百分比（正数增加，负数减少）
         * @param who 产生威胁的单位（默认为nullptr，表示当前生物）
         *
         * 按照指定的百分比增加或减少目标的威胁值。
         * 例如：pct=50会增加50%的威胁，pct=-50会减少50%的威胁。
         */
        void ModifyThreatByPercent(Unit* victim, int32 pct, Unit* who = nullptr);

        /**
         * @brief 重置目标的威胁值为零
         * @param victim 目标单位
         * @param who 产生威胁的单位（默认为nullptr，表示当前生物）
         *
         * 将指定目标对当前生物（或who）的威胁值重置为零。
         * 不会删除威胁列表中的条目，只是将其威胁值设为0。
         */
        void ResetThreat(Unit* victim, Unit* who = nullptr);

        /**
         * @brief 重置整个威胁列表
         * @param who 目标单位（默认为nullptr，表示当前生物）
         *
         * 重置指定单位的所有威胁值为零。
         * 不会删除威胁列表中的条目，只是将所有威胁值设为0。
         */
        void ResetThreatList(Unit* who = nullptr);

        /**
         * @brief 获取目标的威胁值
         * @param victim 目标单位
         * @param who 产生威胁的单位（默认为nullptr，表示当前生物）
         * @return 目标对当前生物的威胁值
         */
        float GetThreat(Unit const* victim, Unit const* who = nullptr);

        /**
         * @brief 强制停止战斗
         * @param who 目标生物
         * @param reset 是否重置（默认true）
         *
         * 强制让指定生物停止战斗，忽略常规限制。
         */
        void ForceCombatStop(Creature* who, bool reset = true);

        /**
         * @brief 强制停止指定ID生物的战斗
         * @param entry 生物模板ID
         * @param maxSearchRange 最大搜索范围（默认250.0f）
         * @param samePhase 是否要求相同相位（默认true）
         * @param reset 是否重置（默认true）
         *
         * 在指定范围内搜索特定ID的生物，并强制它们停止战斗。
         */
        void ForceCombatStopForCreatureEntry(uint32 entry, float maxSearchRange = 250.0f, bool samePhase = true, bool reset = true);

        /**
         * @brief 强制停止多个ID生物的战斗
         * @param creatureEntries 生物模板ID列表
         * @param maxSearchRange 最大搜索范围（默认250.0f）
         * @param samePhase 是否要求相同相位（默认true）
         * @param reset 是否重置（默认true）
         *
         * 在指定范围内搜索多个ID的生物，并强制它们停止战斗。
         */
        void ForceCombatStopForCreatureEntry(std::vector<uint32> creatureEntries, float maxSearchRange = 250.0f, bool samePhase = true, bool reset = true);

        /**
         * @brief 传送生物到指定位置
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param time 传送延迟时间（毫秒，默认0）
         */
        void DoTeleportTo(float x, float y, float z, uint32 time = 0);

        /**
         * @brief 传送生物到指定位置（数组版本）
         * @param pos 位置数组[x, y, z, orientation]
         */
        void DoTeleportTo(float const pos[4]);

        /**
         * @brief 传送玩家到指定位置（不丢失威胁）
         * @param unit 目标单位（必须是玩家）
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         *
         * 传送玩家到指定位置，但不会丢失威胁值。
         * 只能传送到同一地图。
         */
        void DoTeleportPlayer(Unit* unit, float x, float y, float z, float o);

        /**
         * @brief 传送所有攻击者到指定位置
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param o 朝向
         *
         * 传送所有在威胁列表中的玩家到指定位置。
         */
        void DoTeleportAll(float x, float y, float z, float o);

        /**
         * @brief 选择生命值最低的友方单位
         * @param range 搜索范围
         * @param minHPDiff 最小生命值差（默认1）
         * @return 生命值缺失最多的友方单位
         *
         * 在指定范围内查找生命值缺失最多的友方单位。
         * 常用于治疗AI选择治疗目标。
         */
        Unit* DoSelectLowestHpFriendly(float range, uint32 minHPDiff = 1);

        /**
         * @brief 选择生命值低于指定百分比的友方单位
         * @param entry 生物模板ID
         * @param range 搜索范围
         * @param hpPct 生命值百分比阈值（默认1%）
         * @param excludeSelf 是否排除自己（默认true）
         * @return 符合条件的友方单位
         */
        Unit* DoSelectBelowHpPctFriendlyWithEntry(uint32 entry, float range, uint8 hpPct = 1, bool excludeSelf = true);

        /**
         * @brief 查找范围内被控制的友方单位
         * @param range 搜索范围
         * @return 被控制的友方生物列表
         *
         * 查找范围内所有处于控制状态（如变形、恐惧等）的友方单位。
         * 常用于解控AI。
         */
        std::list<Creature*> DoFindFriendlyCC(float range);

        /**
         * @brief 查找范围内缺少指定Buff的友方单位
         * @param range 搜索范围
         * @param spellId 法术ID
         * @return 缺少指定Buff的友方生物列表
         *
         * 查找范围内所有没有指定Buff的友方单位。
         * 常用于Buff AI。
         */
        std::list<Creature*> DoFindFriendlyMissingBuff(float range, uint32 spellId);

        /**
         * @brief 获取距离最近的玩家
         * @param minRange 最小距离
         * @return 距离最近的玩家
         *
         * 返回距离当前生物至少指定距离的最近玩家。
         */
        Player* GetPlayerAtMinimumRange(float minRange);

        /**
         * @brief 在相对位置生成生物
         * @param entry 生物模板ID
         * @param offsetX X偏移量
         * @param offsetY Y偏移量
         * @param offsetZ Z偏移量
         * @param angle 角度（弧度）
         * @param type 生成类型（TempSummonType）
         * @param despawntime 消失时间（毫秒）
         * @return 生成的生物指针
         *
         * 在当前生物的相对位置生成一个新的生物。
         */
        Creature* DoSpawnCreature(uint32 entry, float offsetX, float offsetY, float offsetZ, float angle, uint32 type, Milliseconds despawntime);

        /**
         * @brief 检查生命值是否低于指定百分比
         * @param pct 百分比值
         * @return 如果当前生命值百分比低于指定值返回true，否则返回false
         */
        bool HealthBelowPct(uint32 pct) const;

        /**
         * @brief 检查生命值是否高于指定百分比
         * @param pct 百分比值
         * @return 如果当前生命值百分比高于指定值返回true，否则返回false
         */
        bool HealthAbovePct(uint32 pct) const;

        /**
         * @brief 从生物法术列表中选择符合条件的法术
         * @param target 目标单位
         * @param school 法术学派
         * @param mechanic 法术机制
         * @param targets 目标类型
         * @param powerCostMin 最小消耗
         * @param powerCostMax 最大消耗
         * @param rangeMin 最小范围
         * @param rangeMax 最大范围
         * @param effect 效果类型
         * @return 符合条件的法术信息，如果没有则返回nullptr
         *
         * 根据多个条件从生物的法术列表中选择一个合适的法术。
         * 这是一个高级法术选择函数，通常用于复杂的AI决策。
         */
        SpellInfo const* SelectSpell(Unit* target, uint32 school, uint32 mechanic, SelectTargetType targets, uint32 powerCostMin, uint32 powerCostMax, float rangeMin, float rangeMax, SelectEffect effect);

        /**
         * @brief 设置装备槽
         * @param loadDefault 是否加载默认装备
         * @param mainHand 主手武器（EQUIP_NO_CHANGE表示不改变）
         * @param offHand 副手武器（EQUIP_NO_CHANGE表示不改变）
         * @param ranged 远程武器（EQUIP_NO_CHANGE表示不改变）
         *
         * 动态更改生物的装备。
         * 可以用于Boss在不同阶段切换武器。
         */
        void SetEquipmentSlots(bool loadDefault, int32 mainHand = EQUIP_NO_CHANGE, int32 offHand = EQUIP_NO_CHANGE, int32 ranged = EQUIP_NO_CHANGE);

        /**
         * @brief 设置是否允许战斗移动
         * @param allowMovement 是否允许移动
         *
         * 用于控制AttackStart()中是否使用MoveChase()。
         * 某些生物不追逐目标（如炮塔、固定位置的生物）。
         *
         * @note 如果生物已经在战斗中，调用此函数不会有任何效果，
         *       此函数只影响AttackStart。如果需要在战斗中改变行为，
         *       需要手动处理。此设置在Reset()时不会被重置，会保持最后的设置值。
         */
        void SetCombatMovement(bool allowMovement);

        /**
         * @brief 检查是否允许战斗移动
         * @return 如果允许战斗移动返回true，否则返回false
         */
        bool IsCombatMovementAllowed() const { return _isCombatMovementAllowed; }

        /**
         * @brief 检查是否为英雄模式
         * @return 如果是英雄模式返回true，否则返回false
         *
         * 英雄模式的定义：
         * - 地下城：10人英雄模式
         * - 团队副本：10人英雄模式
         * - 团队副本：25人英雄模式
         *
         * @warning 不要使用此函数检查25人普通模式！
         */
        bool IsHeroic() const { return _isHeroic; }

        /**
         * @brief 获取当前难度
         * @return 当前地下城或团队副本的难度
         */
        Difficulty GetDifficulty() const { return _difficulty; }

        /**
         * @brief 检查是否为25人模式
         * @return 如果是25人或25人英雄模式返回true，否则返回false
         */
        bool Is25ManRaid() const { return _difficulty & RAID_DIFFICULTY_MASK_25MAN; }

        /**
         * @brief 根据地下城难度选择值
         * @tparam T 值类型
         * @param normal5 5人普通模式的值
         * @param heroic10 5人英雄模式的值
         * @return 根据当前难度返回对应的值
         *
         * 使用示例：
         * @code
         * uint32 damage = DUNGEON_MODE(1000, 2000);
         * // 5人普通返回1000，5人英雄返回2000
         * @endcode
         */
        template <class T>
        inline T const& DUNGEON_MODE(T const& normal5, T const& heroic10) const
        {
            switch (_difficulty)
            {
                case DUNGEON_DIFFICULTY_NORMAL:
                    return normal5;
                case DUNGEON_DIFFICULTY_HEROIC:
                    return heroic10;
                default:
                    break;
            }

            return heroic10;
        }

        /**
         * @brief 根据团队难度选择值（仅普通模式）
         * @tparam T 值类型
         * @param normal10 10人普通模式的值
         * @param normal25 25人普通模式的值
         * @return 根据当前难度返回对应的值
         *
         * 使用示例：
         * @code
         * uint32 damage = RAID_MODE(1000, 1500);
         * // 10人普通返回1000，25人普通返回1500
         * @endcode
         */
        template <class T>
        inline T const& RAID_MODE(T const& normal10, T const& normal25) const
        {
            switch (_difficulty)
            {
                case RAID_DIFFICULTY_10MAN_NORMAL:
                    return normal10;
                case RAID_DIFFICULTY_25MAN_NORMAL:
                    return normal25;
                default:
                    break;
            }

            return normal25;
        }

        /**
         * @brief 根据团队难度选择值（包含英雄模式）
         * @tparam T 值类型
         * @param normal10 10人普通模式的值
         * @param normal25 25人普通模式的值
         * @param heroic10 10人英雄模式的值
         * @param heroic25 25人英雄模式的值
         * @return 根据当前难度返回对应的值
         *
         * 使用示例：
         * @code
         * uint32 damage = RAID_MODE(1000, 1500, 2000, 3000);
         * // 10人普通返回1000，25人普通返回1500
         * // 10人英雄返回2000，25人英雄返回3000
         * @endcode
         */
        template <class T>
        inline T const& RAID_MODE(T const& normal10, T const& normal25, T const& heroic10, T const& heroic25) const
        {
            switch (_difficulty)
            {
                case RAID_DIFFICULTY_10MAN_NORMAL:
                    return normal10;
                case RAID_DIFFICULTY_25MAN_NORMAL:
                    return normal25;
                case RAID_DIFFICULTY_10MAN_HEROIC:
                    return heroic10;
                case RAID_DIFFICULTY_25MAN_HEROIC:
                    return heroic25;
                default:
                    break;
            }

            return heroic25;
        }

    private:
        Difficulty _difficulty;              ///< 当前副本难度
        bool _isCombatMovementAllowed;       ///< 是否允许战斗移动
        bool _isHeroic;                      ///< 是否为英雄模式
};

/**
 * @class BossAI
 * @brief Boss AI基类
 *
 * 这是副本Boss的专用AI基类，继承自ScriptedAI。
 * 提供了Boss战斗的常用功能，包括：
 * - 事件调度（EventMap）
 * - 召唤物管理（SummonList）
 * - 任务调度（TaskScheduler）
 * - 副本脚本集成（InstanceScript）
 *
 * 使用示例：
 * @code
 * class MyBossAI : public BossAI
 * {
 * public:
 *     MyBossAI(Creature* creature) : BossAI(creature, BOSS_MY_BOSS) { }
 *
 *     void ScheduleTasks() override
 *     {
 *         // 在战斗开始时调度事件
 *         scheduler.Schedule(5s, [this](TaskContext context)
 *         {
 *             DoCastVictim(SPELL_FIREBALL);
 *             context.Repeat(10s);
 *         });
 *     }
 *
 *     void ExecuteEvent(uint32 eventId) override
 *     {
 *         switch (eventId)
 *         {
 *             case EVENT_SUMMON_ADDS:
 *                 SummonAdds();
 *                 events.ScheduleEvent(EVENT_SUMMON_ADDS, 30s);
 *                 break;
 *         }
 *     }
 * };
 * @endcode
 */
class TC_GAME_API BossAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature Boss生物
         * @param bossId Boss ID（用于副本脚本）
         */
        BossAI(Creature* creature, uint32 bossId);

        /**
         * @brief 虚析构函数
         */
        virtual ~BossAI() { }

        InstanceScript* const instance;  ///< 副本脚本实例

        /**
         * @brief 召唤物生成时的回调
         * @param summon 生成的召唤物
         *
         * 自动将召唤物添加到summons列表中。
         */
        void JustSummoned(Creature* summon) override;

        /**
         * @brief 召唤物消失时的回调
         * @param summon 消失的召唤物
         *
         * 自动从summons列表中移除召唤物。
         */
        void SummonedCreatureDespawn(Creature* summon) override;

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 自动执行EventMap中的事件和TaskScheduler中的任务。
         */
        virtual void UpdateAI(uint32 diff) override;

        /**
         * @brief 执行事件钩子
         * @param eventId 事件ID
         *
         * 用于执行EventMap中调度的事件，无需重写UpdateAI。
         *
         * @note 如果事件需要多次执行，必须在此方法内重新调度。
         */
        virtual void ExecuteEvent(uint32 /*eventId*/) { }

        /**
         * @brief 调度任务钩子
         *
         * 在战斗开始时调用，用于调度初始任务。
         * 使用TaskScheduler系统进行任务调度。
         */
        virtual void ScheduleTasks() { }

        /**
         * @brief 重置Boss状态
         *
         * 调用_Reset()执行默认的重置逻辑。
         */
        void Reset() override { _Reset(); }

        /**
         * @brief 进入战斗
         * @param who 目标
         *
         * 调用_JustEngagedWith()执行默认的进入战斗逻辑。
         */
        void JustEngagedWith(Unit* who) override { _JustEngagedWith(who); }

        /**
         * @brief 死亡回调
         * @param killer 击杀者
         *
         * 调用_JustDied()执行默认的死亡逻辑。
         */
        void JustDied(Unit* /*killer*/) override { _JustDied(); }

        /**
         * @brief 返回初始位置回调
         *
         * 调用_JustReachedHome()执行默认的返回逻辑。
         */
        void JustReachedHome() override { _JustReachedHome(); }

        /**
         * @brief 检查AI是否可以攻击目标
         * @param target 目标单位
         * @return 如果可以攻击返回true，否则返回false
         */
        bool CanAIAttack(Unit const* target) const override;

    protected:
        /**
         * @brief 重置Boss状态（内部实现）
         *
         * 清空事件映射、召唤列表和任务调度器。
         * 由Reset()调用。
         */
        void _Reset();

        /**
         * @brief 进入战斗（内部实现）
         * @param who 目标单位
         *
         * 通知副本脚本Boss已进入战斗，并调度初始任务。
         * 由JustEngagedWith()调用。
         */
        void _JustEngagedWith(Unit* who);

        /**
         * @brief 死亡处理（内部实现）
         *
         * 通知副本脚本Boss已死亡，消失所有召唤物。
         * 由JustDied()调用。
         */
        void _JustDied();

        /**
         * @brief 返回初始位置处理（内部实现）
         *
         * 设置Boss的威胁列表，并准备重新开始战斗。
         * 由JustReachedHome()调用。
         */
        void _JustReachedHome();

        /**
         * @brief 在逃脱后消失Boss
         * @param delayToRespawn 重生延迟时间（秒，默认30秒）
         * @param who 要消失的生物（默认为nullptr，表示自己）
         *
         * 当Boss逃脱战斗后，消失并安排重生。
         */
        void _DespawnAtEvade(Seconds delayToRespawn = 30s,  Creature* who = nullptr);

        /**
         * @brief 传送作弊者
         *
         * 将不在正确位置的玩家传送回正确位置。
         * 用于防止玩家卡位置或利用地形Bug。
         */
        void TeleportCheaters();

        EventMap events;          ///< 事件映射（用于基于时间的战斗逻辑）
        SummonList summons;       ///< 召唤物列表
        TaskScheduler scheduler;  ///< 任务调度器（用于复杂的任务调度）

    private:
        uint32 const _bossId;     ///< Boss ID（用于副本脚本标识）
};

/**
 * @class WorldBossAI
 * @brief 世界Boss AI基类
 *
 * 这是世界Boss的专用AI基类，继承自ScriptedAI。
 * 与BossAI不同，世界Boss不在副本中，通常在开放世界中出现。
 *
 * 主要特点：
 * - 不依赖InstanceScript
 * - 支持事件调度（EventMap）
 * - 支持召唤物管理（SummonList）
 * - 适用于开放世界的Boss战斗
 *
 * 使用示例：
 * @code
 * class MyWorldBossAI : public WorldBossAI
 * {
 * public:
 *     MyWorldBossAI(Creature* creature) : WorldBossAI(creature) { }
 *
 *     void ExecuteEvent(uint32 eventId) override
 *     {
 *         switch (eventId)
 *         {
 *             case EVENT_BOSS_ABILITY:
 *                 DoCastVictim(SPELL_ABILITY);
 *                 events.ScheduleEvent(EVENT_BOSS_ABILITY, 15s);
 *                 break;
 *         }
 *     }
 * };
 * @endcode
 */
class TC_GAME_API WorldBossAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 世界Boss生物
         */
        WorldBossAI(Creature* creature);

        /**
         * @brief 虚析构函数
         */
        virtual ~WorldBossAI() { }

        /**
         * @brief 召唤物生成时的回调
         * @param summon 生成的召唤物
         *
         * 自动将召唤物添加到summons列表中。
         */
        void JustSummoned(Creature* summon) override;

        /**
         * @brief 召唤物消失时的回调
         * @param summon 消失的召唤物
         *
         * 自动从summons列表中移除召唤物。
         */
        void SummonedCreatureDespawn(Creature* summon) override;

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 自动执行EventMap中的事件。
         */
        virtual void UpdateAI(uint32 diff) override;

        /**
         * @brief 执行事件钩子
         * @param eventId 事件ID
         *
         * 用于执行EventMap中调度的事件，无需重写UpdateAI。
         *
         * @note 如果事件需要多次执行，必须在此方法内重新调度。
         */
        virtual void ExecuteEvent(uint32 /*eventId*/) { }

        /**
         * @brief 重置Boss状态
         *
         * 调用_Reset()执行默认的重置逻辑。
         */
        void Reset() override { _Reset(); }

        /**
         * @brief 进入战斗
         * @param who 目标
         *
         * 调用_JustEngagedWith()执行默认的进入战斗逻辑。
         */
        void JustEngagedWith(Unit* /*who*/) override { _JustEngagedWith(); }

        /**
         * @brief 死亡回调
         * @param killer 击杀者
         *
         * 调用_JustDied()执行默认的死亡逻辑。
         */
        void JustDied(Unit* /*killer*/) override { _JustDied(); }

    protected:
        /**
         * @brief 重置Boss状态（内部实现）
         *
         * 清空事件映射和召唤列表。
         * 由Reset()调用。
         */
        void _Reset();

        /**
         * @brief 进入战斗（内部实现）
         *
         * 执行进入战斗的初始化逻辑。
         * 由JustEngagedWith()调用。
         */
        void _JustEngagedWith();

        /**
         * @brief 死亡处理（内部实现）
         *
         * 消失所有召唤物。
         * 由JustDied()调用。
         */
        void _JustDied();

        EventMap events;    ///< 事件映射（用于基于时间的战斗逻辑）
        SummonList summons; ///< 召唤物列表
};

// SD2 grid searchers.

/**
 * @brief 获取最近的指定ID生物
 * @param source 搜索源对象
 * @param entry 生物模板ID
 * @param maxSearchRange 最大搜索范围
 * @param alive 是否只搜索活着的生物（默认true）
 * @return 找到的最近生物指针，如果没有找到返回nullptr
 *
 * 在指定范围内查找最近的指定ID的生物。
 *
 * 使用示例：
 * @code
 * Creature* creature = GetClosestCreatureWithEntry(me, 12345, 100.0f);
 * if (creature)
 *     creature->DespawnOrUnsummon();
 * @endcode
 */
inline Creature* GetClosestCreatureWithEntry(WorldObject* source, uint32 entry, float maxSearchRange, bool alive = true)
{
    return source->FindNearestCreature(entry, maxSearchRange, alive);
}

/**
 * @brief 获取最近的指定ID游戏对象
 * @param source 搜索源对象
 * @param entry 游戏对象模板ID
 * @param maxSearchRange 最大搜索范围
 * @param spawnedOnly 是否只搜索已生成的对象（默认true）
 * @return 找到的最近游戏对象指针，如果没有找到返回nullptr
 *
 * 在指定范围内查找最近的指定ID的游戏对象。
 *
 * 使用示例：
 * @code
 * GameObject* go = GetClosestGameObjectWithEntry(me, 12345, 100.0f);
 * if (go)
 *     go->Use(me);
 * @endcode
 */
inline GameObject* GetClosestGameObjectWithEntry(WorldObject* source, uint32 entry, float maxSearchRange, bool spawnedOnly = true)
{
    return source->FindNearestGameObject(entry, maxSearchRange, spawnedOnly);
}

/**
 * @brief 获取指定范围内的所有指定ID生物列表
 * @tparam Container 容器类型
 * @param container 输出容器
 * @param source 搜索源对象
 * @param entry 生物模板ID
 * @param maxSearchRange 最大搜索范围
 *
 * 在指定范围内查找所有指定ID的生物，并添加到容器中。
 *
 * 使用示例：
 * @code
 * std::list<Creature*> creatures;
 * GetCreatureListWithEntryInGrid(creatures, me, 12345, 50.0f);
 * for (Creature* creature : creatures)
 *     creature->DespawnOrUnsummon();
 * @endcode
 */
template <typename Container>
inline void GetCreatureListWithEntryInGrid(Container& container, WorldObject* source, uint32 entry, float maxSearchRange)
{
    source->GetCreatureListWithEntryInGrid(container, entry, maxSearchRange);
}

/**
 * @brief 获取指定范围内的所有指定ID游戏对象列表
 * @tparam Container 容器类型
 * @param container 输出容器
 * @param source 搜索源对象
 * @param entry 游戏对象模板ID
 * @param maxSearchRange 最大搜索范围
 *
 * 在指定范围内查找所有指定ID的游戏对象，并添加到容器中。
 *
 * 使用示例：
 * @code
 * std::list<GameObject*> gameobjects;
 * GetGameObjectListWithEntryInGrid(gameobjects, me, 12345, 50.0f);
 * for (GameObject* go : gameobjects)
 *     go->SetGoState(GO_STATE_ACTIVE);
 * @endcode
 */
template <typename Container>
inline void GetGameObjectListWithEntryInGrid(Container& container, WorldObject* source, uint32 entry, float maxSearchRange)
{
    source->GetGameObjectListWithEntryInGrid(container, entry, maxSearchRange);
}

/**
 * @brief 获取指定范围内的所有玩家列表
 * @tparam Container 容器类型
 * @param container 输出容器
 * @param source 搜索源对象
 * @param maxSearchRange 最大搜索范围
 * @param alive 是否只搜索活着的玩家（默认true）
 *
 * 在指定范围内查找所有玩家，并添加到容器中。
 *
 * 使用示例：
 * @code
 * std::list<Player*> players;
 * GetPlayerListInGrid(players, me, 50.0f);
 * for (Player* player : players)
 *     player->CastSpell(player, SPELL_BUFF, true);
 * @endcode
 */
template <typename Container>
inline void GetPlayerListInGrid(Container& container, WorldObject* source, float maxSearchRange, bool alive = true)
{
    source->GetPlayerListInGrid(container, maxSearchRange, alive);
}

#endif // TRINITY_SCRIPTEDCREATURE_H
