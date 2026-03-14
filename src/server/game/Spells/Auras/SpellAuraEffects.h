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
 * @file SpellAuraEffects.h
 * @brief 光环效果系统头文件 - 定义光环效果(AuraEffect)类及其处理器
 *
 * 本文件定义了光环效果系统的核心类：
 *
 * - AuraEffect: 光环效果类，管理单个光环效果的行为
 * - pAuraEffectHandler: 光环效果处理器函数指针类型
 * - AbsorbAuraOrderPred: 吸收光环排序谓词
 *
 * 光环效果概述：
 * 每个光环(Aura)可以包含多个光环效果(AuraEffect)，每个效果对应法术的一个效果索引。
 * 光环效果负责实际的游戏逻辑，如：
 * - 属性修改（增加攻击力、护甲等）
 * - 周期性效果（持续伤害、持续治疗）
 * - 触发效果（攻击时触发法术）
 * - 控制效果（昏迷、沉默、减速）
 * - 特殊效果（变形、飞行、隐形）
 *
 * 效果处理器：
 * AuraEffect类为每种光环类型定义了对应的处理函数，用于应用/移除效果。
 * 处理器通过AuraEffectHandler数组进行索引和调用。
 */

#ifndef TRINITY_SPELLAURAEFFECTS_H
#define TRINITY_SPELLAURAEFFECTS_H

#include "SpellAuras.h"

class AuraEffect;
class Unit;

/**
 * @typedef pAuraEffectHandler
 * @brief 光环效果处理器函数指针类型
 *
 * 用于定义处理光环效果应用/移除的成员函数指针。
 * 每种光环类型都有对应的处理器函数。
 *
 * @param aurApp 光环应用对象
 * @param mode 处理模式（实时、客户端同步、数值变化等）
 * @param apply true表示应用效果，false表示移除效果
 */
typedef void(AuraEffect::*pAuraEffectHandler)(AuraApplication const* aurApp, uint8 mode, bool apply) const;

/**
 * @class AuraEffect
 * @brief 光环效果类 - 管理光环的单一效果
 *
 * AuraEffect 类代表光环(Aura)中的一个具体效果。每个光环可以有最多3个效果
 * （对应MAX_SPELL_EFFECTS），每个效果由AuraEffect类实例管理。
 *
 * 核心职责：
 * - 管理效果的数值计算和存储
 * - 处理周期性效果（DoT/HoT）的触发
 * - 处理Proc触发效果
 * - 应用/移除效果到目标
 * - 管理法术修改器（SpellModifier）
 *
 * 效果类型：
 * 游戏中有超过300种光环效果类型，包括：
 * - 属性修改（SPELL_AURA_MOD_STAT, SPELL_AURA_MOD_RESISTANCE等）
 * - 周期性效果（SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_HEAL等）
 * - 触发效果（SPELL_AURA_PROC_TRIGGER_SPELL等）
 * - 控制效果（SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT等）
 * - 特殊效果（SPELL_AURA_MOD_SHAPESHIFT, SPELL_AURA_MOD_STEALTH等）
 *
 * 数值计算流程：
 * 1. 创建时调用CalculateAmount()计算基础数值
 * 2. 脚本可修改计算结果
 * 3. 层数变化时重新计算
 * 4. 可通过SetAmount()直接设置
 *
 * 周期性效果：
 * - _periodicTimer: 当前周期计时器
 * - _amplitude: 周期间隔
 * - _ticksDone: 已完成的tick次数
 * - Update()方法中处理tick触发
 */
class TC_GAME_API AuraEffect
{
    friend void Aura::_InitEffects(uint8 effMask, Unit* caster, int32 const* baseAmount);
    friend Aura::~Aura();
    friend class Unit;

    private:
        /**
         * @brief 析构函数
         * @note 清理法术修改器
         */
        ~AuraEffect();

        /**
         * @brief 构造函数
         * @param base 所属光环对象
         * @param spellEfffectInfo 法术效果信息
         * @param baseAmount 基础数值（可选）
         * @param caster 施法者
         * @note 初始化效果数值、周期性属性和法术修改器
         */
        explicit AuraEffect(Aura* base, SpellEffectInfo const& spellEfffectInfo, int32 const* baseAmount, Unit* caster);

    public:
        // ========== 基础访问器 ==========

        /**
         * @brief 获取施法者
         * @return 施法者指针，可能为nullptr
         */
        Unit* GetCaster() const { return GetBase()->GetCaster(); }

        /**
         * @brief 获取施法者GUID
         * @return 施法者GUID
         */
        ObjectGuid GetCasterGUID() const { return GetBase()->GetCasterGUID(); }

        /**
         * @brief 获取所属光环
         * @return 光环指针
         */
        Aura* GetBase() const { return m_base; }

        /**
         * @brief 获取目标列表
         * @tparam Container 容器类型
         * @param targetContainer 输出容器
         * @note 返回所有应用了此效果的目标单位
         */
        template <typename Container>
        void GetTargetList(Container& targetContainer) const;

        /**
         * @brief 获取光环应用列表
         * @tparam Container 容器类型
         * @param applicationContainer 输出容器
         */
        template <typename Container>
        void GetApplicationList(Container& applicationContainer) const;

        /**
         * @brief 获取法术信息
         * @return 法术信息指针
         */
        SpellInfo const* GetSpellInfo() const { return m_spellInfo; }

        /**
         * @brief 获取法术ID
         * @return 法术ID
         */
        uint32 GetId() const { return m_spellInfo->Id; }

        /**
         * @brief 获取效果索引
         * @return 效果索引（0-2）
         */
        SpellEffIndex GetEffIndex() const { return m_spellEffectInfo.EffectIndex; }

        /**
         * @brief 获取基础数值
         * @return 效果基础数值
         */
        int32 GetBaseAmount() const { return m_baseAmount; }

        /**
         * @brief 获取周期间隔
         * @return 周期间隔（毫秒）
         */
        int32 GetAmplitude() const { return _amplitude; }

        /**
         * @brief 获取MiscValueB
         * @return 效果的MiscValueB值
         * @note 用于各种效果特定的参数
         */
        int32 GetMiscValueB() const { return GetSpellEffectInfo().MiscValueB; }

        /**
         * @brief 获取MiscValue
         * @return 效果的MiscValue值
         * @note 用于各种效果特定的参数
         */
        int32 GetMiscValue() const { return GetSpellEffectInfo().MiscValue; }

        /**
         * @brief 获取光环效果类型
         * @return 光环效果类型枚举值
         * @see AuraType枚举
         */
        AuraType GetAuraType() const { return GetSpellEffectInfo().ApplyAuraName; }

        /**
         * @brief 获取当前数值
         * @return 效果计算后的数值
         */
        int32 GetAmount() const { return _amount; }

        /**
         * @brief 设置数值
         * @param amount 新数值
         * @note 设置后不再可重新计算
         */
        void SetAmount(int32 amount) { _amount = amount; m_canBeRecalculated = false; }

        /**
         * @brief 获取周期计时器
         * @return 当前周期剩余时间（毫秒）
         */
        int32 GetPeriodicTimer() const { return _periodicTimer; }

        /**
         * @brief 设置周期计时器
         * @param periodicTimer 新的周期计时器值
         */
        void SetPeriodicTimer(int32 periodicTimer) { _periodicTimer = periodicTimer; }

        // ========== 数值计算方法 ==========

        /**
         * @brief 计算效果数值
         * @param caster 施法者
         * @return 计算后的效果数值
         * @note 考虑基础数值、施法者属性、天赋等因素
         */
        int32 CalculateAmount(Unit* caster);

        /**
         * @brief 计算周期性属性
         * @param caster 施法者
         * @param resetPeriodicTimer 是否重置周期计时器，默认true
         * @param load 是否从数据库加载，默认false
         * @note 设置周期性效果的时间间隔
         */
        void CalculatePeriodic(Unit* caster, bool resetPeriodicTimer = true, bool load = false);

        /**
         * @brief 计算法术修改器
         * @note 创建或更新法术修改器对象
         */
        void CalculateSpellMod();

        /**
         * @brief 改变效果数值
         * @param newAmount 新数值
         * @param mark 是否标记需要客户端更新，默认true
         * @param onStackOrReapply 是否为叠加或重新应用，默认false
         */
        void ChangeAmount(int32 newAmount, bool mark = true, bool onStackOrReapply = false);

        /**
         * @brief 重新计算数值（使用当前施法者）
         * @note 仅在可重新计算时执行
         */
        void RecalculateAmount() { if (!CanBeRecalculated()) return; ChangeAmount(CalculateAmount(GetCaster()), false); }

        /**
         * @brief 重新计算数值（指定施法者）
         * @param caster 施法者
         */
        void RecalculateAmount(Unit* caster) { if (!CanBeRecalculated()) return; ChangeAmount(CalculateAmount(caster), false); }

        /**
         * @brief 检查是否可重新计算
         * @return true如果数值可以重新计算
         */
        bool CanBeRecalculated() const { return m_canBeRecalculated; }

        /**
         * @brief 设置是否可重新计算
         * @param val 可重新计算标志
         */
        void SetCanBeRecalculated(bool val) { m_canBeRecalculated = val; }

        // ========== 效果应用方法 ==========

        /**
         * @brief 处理效果应用/移除
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         * @param apply true表示应用，false表示移除
         */
        void HandleEffect(AuraApplication * aurApp, uint8 mode, bool apply);

        /**
         * @brief 处理效果应用/移除（指定目标）
         * @param target 目标单位
         * @param mode 处理模式
         * @param apply true表示应用，false表示移除
         */
        void HandleEffect(Unit* target, uint8 mode, bool apply);

        /**
         * @brief 应用法术修改器
         * @param target 目标单位
         * @param apply true表示应用，false表示移除
         * @note 修改目标的法术属性（如施法时间、伤害加成等）
         */
        void ApplySpellMod(Unit* target, bool apply);

        /**
         * @brief 更新效果状态
         * @param diff 时间差（毫秒）
         * @param caster 施法者
         * @note 每帧调用，处理周期性效果的tick触发
         */
        void Update(uint32 diff, Unit* caster);

        // ========== 周期性效果方法 ==========

        /**
         * @brief 获取已完成的tick次数
         * @return tick计数
         */
        uint32 GetTickNumber() const { return _ticksDone; }

        /**
         * @brief 获取剩余tick次数
         * @return 剩余tick次数
         */
        uint32 GetRemainingTicks() const { return GetTotalTicks() - _ticksDone; }

        /**
         * @brief 获取总tick次数
         * @return 总tick次数
         */
        uint32 GetTotalTicks() const;

        /**
         * @brief 重置周期性状态
         * @param resetPeriodicTimer 是否重置周期计时器，默认false
         */
        void ResetPeriodic(bool resetPeriodicTimer = false);

        /**
         * @brief 重置tick计数
         */
        void ResetTicks() { _ticksDone = 0; }

        /**
         * @brief 检查是否为周期性效果
         * @return true如果是周期性效果
         */
        bool IsPeriodic() const { return m_isPeriodic; }

        /**
         * @brief 设置是否为周期性效果
         * @param isPeriodic 周期性标志
         */
        void SetPeriodic(bool isPeriodic) { m_isPeriodic = isPeriodic; }

        /**
         * @brief 检查是否影响指定法术
         * @param spell 法术信息
         * @return true如果此效果影响该法术
         * @note 基于SpellClassMask检查
         */
        bool IsAffectedOnSpell(SpellInfo const* spell) const;

        /**
         * @brief 检查是否有法术类别掩码
         * @return true如果有SpellClassMask
         */
        bool HasSpellClassMask() const { return GetSpellEffectInfo().SpellClassMask; }

        /**
         * @brief 发送免疫tick消息
         * @param target 目标单位
         * @param caster 施法者
         */
        void SendTickImmune(Unit* target, Unit* caster) const;

        /**
         * @brief 执行周期性tick
         * @param aurApp 光环应用对象
         * @param caster 施法者
         * @note 处理DoT/HoT等周期性效果的一次触发
         */
        void PeriodicTick(AuraApplication* aurApp, Unit* caster) const;

        // ========== Proc触发方法 ==========

        /**
         * @brief 检查效果Proc条件
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果应该触发
         */
        bool CheckEffectProc(AuraApplication* aurApp, ProcEventInfo& eventInfo) const;

        /**
         * @brief 处理Proc触发
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 清理触发的法术
         * @param target 目标单位
         * @note 移除由此效果触发的无限持续时间法术
         */
        void CleanupTriggeredSpells(Unit* target);

        /**
         * @brief 处理变形增益
         * @param target 目标单位
         * @param apply true表示应用，false表示移除
         * @note 添加/移除SPELL_AURA_MOD_SHAPESHIFT关联的光环
         */
        void HandleShapeshiftBoosts(Unit* target, bool apply) const;

        /**
         * @brief 获取法术效果信息
         * @return 法术效果信息引用
         */
        SpellEffectInfo const& GetSpellEffectInfo() const { return m_spellEffectInfo; }

    private:
        // ========== 核心数据成员 ==========
        Aura* const m_base;                              // 所属光环对象
        SpellInfo const* const m_spellInfo;              // 法术信息
        SpellEffectInfo const& m_spellEffectInfo;        // 法术效果信息引用
        int32 const m_baseAmount;                        // 基础数值（来自法术数据或施法时传入）

        int32 _amount;                                   // 当前计算后的效果数值

        SpellModifier* m_spellmod;                       // 法术修改器（用于修改其他法术属性）

        // ========== 周期性效果数据 ==========
        int32 _periodicTimer;                            // 周期计时器 - 距离下次tick的剩余时间
        int32 _amplitude;                                // 周期间隔 - 两次tick之间的时间间隔（毫秒）
        uint32 _ticksDone;                               // 已完成tick计数

        bool m_canBeRecalculated;                        // 是否可重新计算数值
        bool m_isPeriodic;                               // 是否为周期性效果

        /**
         * @brief 获取对指定目标的暴击几率
         * @param caster 施法者
         * @param target 目标
         * @return 暴击几率
         */
        float GetCritChanceFor(Unit const* caster, Unit const* target) const;

    public:
        // ========== 光环效果处理器 ==========
        // 以下函数用于处理各种光环类型的应用和移除
        // 每个处理器对应一种或多种光环效果类型
        // 参数说明:
        //   aurApp - 光环应用对象
        //   mode - 处理模式(AURA_EFFECT_HANDLE_*)
        //   apply - true表示应用效果，false表示移除效果

        // ---------- 特殊处理器 ----------
        /**
         * @brief 空处理器 - 用于未实现的光环类型
         */
        void HandleNULL(AuraApplication const* /*aurApp*/, uint8 /*mode*/, bool /*apply*/) const
        {
            // not implemented
        }

        /**
         * @brief 未使用处理器 - 用于不再使用的光环类型
         */
        void HandleUnused(AuraApplication const* /*aurApp*/, uint8 /*mode*/, bool /*apply*/) const
        {
            // useless
        }

        /**
         * @brief 无即时效果处理器 - 用于在其他地方处理的光环类型
         * @note 某些光环类型没有即时的应用/移除效果，而是通过ID在其他代码位置处理
         */
        void HandleNoImmediateEffect(AuraApplication const* /*aurApp*/, uint8 /*mode*/, bool /*apply*/) const
        {
            // aura type not have immediate effect at add/remove and handled by ID in other code place
        }

        // ---------- 可见性与相位 ----------
        void HandleModInvisibilityDetect(AuraApplication const* aurApp, uint8 mode, bool apply) const;    // 隐形侦测
        void HandleModInvisibility(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 隐形
        void HandleModStealth(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 潜行
        void HandleModStealthLevel(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 潜行等级
        void HandleModStealthDetect(AuraApplication const* aurApp, uint8 mode, bool apply) const;          // 潜行侦测
        void HandleDetectAmore(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 情感检测
        void HandleSpiritOfRedemption(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 救赎之魂
        void HandleAuraGhost(AuraApplication const* aurApp, uint8 mode, bool apply) const;                 // 幽灵形态
        void HandlePhase(AuraApplication const* aurApp, uint8 mode, bool apply) const;                     // 相位

        // ---------- 单位模型 ----------
        void HandleAuraModShapeshift(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 变形
        void HandleAuraTransform(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 变身
        void HandleAuraModScale(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 模型缩放
        void HandleAuraCloneCaster(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 克隆施法者

        // ---------- 战斗状态 ----------
        void HandleFeignDeath(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 假死
        void HandleModUnattackable(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 不可攻击
        void HandleAuraModDisarm(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 缴械
        void HandleAuraModSilence(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 沉默
        void HandleAuraModPacify(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 平静
        void HandleAuraModPacifyAndSilence(AuraApplication const* aurApp, uint8 mode, bool apply) const;   // 平静并沉默
        void HandleAuraAllowOnlyAbility(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 只允许特定技能

        // ---------- 追踪 ----------
        void HandleAuraTrackResources(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 追踪资源
        void HandleAuraTrackCreatures(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 追踪生物
        void HandleAuraTrackStealthed(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 追踪潜行
        void HandleAuraModStalked(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 被追踪
        void HandleAuraUntrackable(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 不可追踪

        // ---------- 技能与天赋 ----------
        void HandleAuraModPetTalentsPoints(AuraApplication const* aurApp, uint8 mode, bool apply) const;   // 宠物天赋点数
        void HandleAuraModSkill(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 技能加成

        // ---------- 移动 ----------
        void HandleAuraMounted(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 骑乘
        void HandleAuraAllowFlight(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 允许飞行
        void HandleAuraWaterWalk(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 水上行走
        void HandleAuraFeatherFall(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 缓落
        void HandleAuraHover(AuraApplication const* aurApp, uint8 mode, bool apply) const;                 // 悬停
        void HandleWaterBreathing(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 水下呼吸
        void HandleForceMoveForward(AuraApplication const* aurApp, uint8 mode, bool apply) const;          // 强制前进

        // ---------- 威胁 ----------
        void HandleModThreat(AuraApplication const* aurApp, uint8 mode, bool apply) const;                 // 威胁值修正
        void HandleAuraModTotalThreat(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 总威胁修正
        void HandleModTaunt(AuraApplication const* aurApp, uint8 mode, bool apply) const;                  // 嘲讽
        void HandleModDetaunt(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 反嘲讽

        // ---------- 控制 ----------
        void HandleModConfuse(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 混乱
        void HandleModFear(AuraApplication const* aurApp, uint8 mode, bool apply) const;                   // 恐惧
        void HandleAuraModStun(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 昏迷
        void HandleAuraModRoot(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 定身
        void HandlePreventFleeing(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 阻止逃跑

        // ---------- 操控 ----------
        void HandleModPossess(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 附身
        void HandleModPossessPet(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 附身宠物
        void HandleModCharm(AuraApplication const* aurApp, uint8 mode, bool apply) const;                  // 魅惑
        void HandleCharmConvert(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 魅惑转化
        void HandleAuraControlVehicle(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 控制载具

        // ---------- 速度修正 ----------
        void HandleAuraModIncreaseSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 增加速度
        void HandleAuraModIncreaseMountedSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 增加骑乘速度
        void HandleAuraModIncreaseFlightSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 增加飞行速度
        void HandleAuraModIncreaseSwimSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;  // 增加游泳速度
        void HandleAuraModDecreaseSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 减少速度
        void HandleAuraModUseNormalSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 使用正常速度

        // ---------- 免疫 ----------
        void HandleModMechanicImmunityMask(AuraApplication const* aurApp, uint8 mode, bool apply) const;   // 机制免疫掩码
        void HandleModMechanicImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 机制免疫
        void HandleAuraModEffectImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 效果免疫
        void HandleAuraModStateImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 状态免疫
        void HandleAuraModSchoolImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 学校免疫
        void HandleAuraModDmgImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 伤害免疫
        void HandleAuraModDispelImmunity(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 驱散免疫

        // ---------- 属性修正 - 抗性 ----------
        void HandleAuraModResistanceExclusive(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 独占抗性
        void HandleAuraModResistance(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 抗性
        void HandleAuraModBaseResistancePCT(AuraApplication const* aurApp, uint8 mode, bool apply) const;  // 基础抗性百分比
        void HandleModResistancePercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 抗性百分比
        void HandleModBaseResistance(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 基础抗性
        void HandleModTargetResistance(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 目标抗性

        // ---------- 属性修正 - 属性 ----------
        void HandleAuraModStat(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 属性
        void HandleModPercentStat(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 属性百分比
        void HandleModSpellDamagePercentFromStat(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 属性转法伤百分比
        void HandleModSpellHealingPercentFromStat(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 属性转治疗百分比
        void HandleModSpellDamagePercentFromAttackPower(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 攻强转法伤
        void HandleModSpellHealingPercentFromAttackPower(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 攻强转治疗
        void HandleModHealingDone(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 治疗加成
        void HandleModTotalPercentStat(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 总属性百分比
        void HandleAuraModResistenceOfStatPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 属性转抗性
        void HandleAuraModExpertise(AuraApplication const* aurApp, uint8 mode, bool apply) const;          // 精准

        // ---------- 属性修正 - 生命与能量 ----------
        void HandleModPowerRegen(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 能量恢复
        void HandleModPowerRegenPCT(AuraApplication const* aurApp, uint8 mode, bool apply) const;          // 能量恢复百分比
        void HandleModManaRegen(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 法力恢复
        void HandleAuraModIncreaseHealth(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 增加生命值
        void HandleAuraModIncreaseMaxHealth(AuraApplication const* aurApp, uint8 mode, bool apply) const;  // 增加最大生命值
        void HandleAuraModIncreaseEnergy(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 增加能量
        void HandleAuraModIncreaseEnergyPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 能量百分比
        void HandleAuraModIncreaseHealthPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 生命百分比
        void HandleAuraIncreaseBaseHealthPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 基础生命百分比

        // ---------- 属性修正 - 战斗 ----------
        void HandleAuraModParryPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 招架百分比
        void HandleAuraModDodgePercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 躲闪百分比
        void HandleAuraModBlockPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 格挡百分比
        void HandleAuraModRegenInterrupt(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 回复中断
        void HandleAuraModWeaponCritPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;  // 武器暴击百分比
        void HandleModSpellHitChance(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 法术命中
        void HandleModSpellCritChance(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 法术暴击
        void HandleModSpellCritChanceShool(AuraApplication const* aurApp, uint8 mode, bool apply) const;   // 学校法术暴击
        void HandleAuraModCritPct(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 暴击百分比

        // ---------- 属性修正 - 攻击速度 ----------
        void HandleModCastingSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 施法速度
        void HandleModMeleeRangedSpeedPct(AuraApplication const* aurApp, uint8 mode, bool apply) const;    // 近战远程速度百分比
        void HandleModCombatSpeedPct(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 战斗速度百分比
        void HandleModAttackSpeed(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 攻击速度
        void HandleModMeleeSpeedPct(AuraApplication const* aurApp, uint8 mode, bool apply) const;          // 近战速度百分比
        void HandleAuraModRangedHaste(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 远程急速
        void HandleRangedAmmoHaste(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 远程弹药急速

        // ---------- 属性修正 - 战斗等级 ----------
        void HandleModRating(AuraApplication const* aurApp, uint8 mode, bool apply) const;                 // 等级修正
        void HandleModRatingFromStat(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 属性转等级

        // ---------- 属性修正 - 攻击强度 ----------
        void HandleAuraModAttackPower(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 攻击强度
        void HandleAuraModRangedAttackPower(AuraApplication const* aurApp, uint8 mode, bool apply) const;   // 远程攻击强度
        void HandleAuraModAttackPowerPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;  // 攻击强度百分比
        void HandleAuraModRangedAttackPowerPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 远程攻击强度百分比
        void HandleAuraModRangedAttackPowerOfStatPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 属性转远程攻强
        void HandleAuraModAttackPowerOfStatPercent(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 属性转攻强

        // ---------- 伤害加成 ----------
        void HandleModDamageDone(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 伤害加成
        void HandleModDamagePercentDone(AuraApplication const* aurApp, uint8 mode, bool apply) const;       // 伤害百分比
        void HandleModOffhandDamagePercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;    // 副手伤害百分比
        void HandleShieldBlockValue(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 盾牌格挡值
        void HandleShieldBlockValuePercent(AuraApplication const* aurApp, uint8 mode, bool apply) const;    // 盾牌格挡值百分比

        // ---------- 能量消耗 ----------
        void HandleModPowerCostPCT(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 能量消耗百分比
        void HandleModPowerCost(AuraApplication const* aurApp, uint8 mode, bool apply) const;               // 能量消耗
        void HandleArenaPreparation(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 竞技场准备
        void HandleNoReagentUseAura(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 无需材料
        void HandleAuraRetainComboPoints(AuraApplication const* aurApp, uint8 mode, bool apply) const;      // 保留连击点

        // ---------- 其他效果 ----------
        void HandleAuraDummy(AuraApplication const* aurApp, uint8 mode, bool apply) const;                  // 虚拟效果（脚本处理）
        void HandleChannelDeathItem(AuraApplication const* aurApp, uint8 mode, bool apply) const;           // 引导死亡物品
        void HandleBindSight(AuraApplication const* aurApp, uint8 mode, bool apply) const;                  // 绑定视野
        void HandleForceReaction(AuraApplication const* aurApp, uint8 mode, bool apply) const;              // 强制声望
        void HandleAuraEmpathy(AuraApplication const* aurApp, uint8 mode, bool apply) const;                // 共情
        void HandleAuraModFaction(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 修改阵营
        void HandleComprehendLanguage(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 理解语言
        void HandleAuraConvertRune(AuraApplication const* aurApp, uint8 mode, bool apply) const;            // 转换符文
        void HandleAuraLinked(AuraApplication const* aurApp, uint8 mode, bool apply) const;                 // 关联光环
        void HandleAuraOpenStable(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 打开兽栏
        void HandleAuraModFakeInebriation(AuraApplication const* aurApp, uint8 mode, bool apply) const;     // 虚假醉酒
        void HandleAuraOverrideSpells(AuraApplication const* aurApp, uint8 mode, bool apply) const;         // 覆盖法术
        void HandleAuraPreventRegeneratePower(AuraApplication const* aurApp, uint8 mode, bool apply) const; // 阻止能量恢复
        void HandleAuraSetVehicle(AuraApplication const* aurApp, uint8 mode, bool apply) const;             // 设置载具
        void HandlePreventResurrection(AuraApplication const* aurApp, uint8 mode, bool apply) const;        // 阻止复活

        // ========== 周期性效果处理器 ==========
        // 以下方法处理周期性效果（DoT/HoT等）的每次tick

        /**
         * @brief 周期性触发法术
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicTriggerSpellAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性触发带数值法术
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicTriggerSpellWithValueAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性伤害
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicDamageAurasTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性生命吸取
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicHealthLeechAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性生命输送
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicHealthFunnelAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性治疗
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicHealAurasTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性法力吸取
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicManaLeechAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 观察性能量修改
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandleObsModPowerAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性能量充能
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicEnergizeAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 周期性能量燃烧
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandlePeriodicPowerBurnAuraTick(Unit* target, Unit* caster) const;

        /**
         * @brief 护甲转攻强tick
         * @param target 目标单位
         * @param caster 施法者
         */
        void HandleModAttackPowerOfArmorAuraTick(Unit* target, Unit* caster) const;

        // ========== Proc触发处理器 ==========
        // 以下方法处理事件触发的光环效果

        /**
         * @brief 可打破的控制效果Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleBreakableCCAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 触发法术Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleProcTriggerSpellAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 带数值触发法术Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleProcTriggerSpellWithValueAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 触发伤害Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleProcTriggerDamageAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 团队充能Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleRaidProcFromChargeAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 团队带数值充能Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void HandleRaidProcFromChargeWithValueAuraProc(AuraApplication* aurApp, ProcEventInfo& eventInfo);
};

/**
 * @namespace Trinity
 * @brief TrinityCore命名空间 - 包含通用工具类和函数
 */
namespace Trinity
{
    /**
     * @class AbsorbAuraOrderPred
     * @brief 吸收光环排序谓词 - 用于确定吸收效果的优先级顺序
     *
     * 此类用于对吸收光环效果进行排序，确保特定吸收效果按正确顺序消耗。
     * 某些吸收效果有特殊优先级，需要优先消耗或最后消耗。
     *
     * 优先级规则：
     * 1. 法师/术士的防护罩（Category 56）- 最高优先级
     * 2. 神圣之盾（Sacred Shield, ID: 58597）
     * 3. 恶魔花（Fel Blossom, ID: 28527）
     * 4. 神圣庇护（Divine Aegis, ID: 47753）
     * 5. 冰霜屏障（Ice Barrier, Category 471）
     * 6. 术士牺牲（Sacrifice, SpellIconID: 693）
     *
     * 使用场景：
     * 当单位受到伤害时，需要确定哪个吸收效果先被消耗。
     * 正确的顺序对于某些技能的机制至关重要。
     */
    class AbsorbAuraOrderPred
    {
        public:
            AbsorbAuraOrderPred() { }

            /**
             * @brief 比较两个吸收效果的优先级
             * @param aurEffA 第一个吸收效果
             * @param aurEffB 第二个吸收效果
             * @return true如果A应该排在B前面
             */
            bool operator() (AuraEffect* aurEffA, AuraEffect* aurEffB) const
            {
                SpellInfo const* spellProtoA = aurEffA->GetSpellInfo();
                SpellInfo const* spellProtoB = aurEffB->GetSpellInfo();

                // 防护罩（法师/术士）- 法力盾、牺牲护盾等
                if ((spellProtoA->SpellFamilyName == SPELLFAMILY_MAGE) ||
                    (spellProtoA->SpellFamilyName == SPELLFAMILY_WARLOCK))
                    if (spellProtoA->GetCategory() == 56)
                        return true;
                if ((spellProtoB->SpellFamilyName == SPELLFAMILY_MAGE) ||
                    (spellProtoB->SpellFamilyName == SPELLFAMILY_WARLOCK))
                    if (spellProtoB->GetCategory() == 56)
                        return false;

                // 神圣之盾（圣骑士）
                if (spellProtoA->Id == 58597)
                    return true;
                if (spellProtoB->Id == 58597)
                    return false;

                // 恶魔花（术士物品）
                if (spellProtoA->Id == 28527)
                    return true;
                if (spellProtoB->Id == 28527)
                    return false;

                // 神圣庇护（牧师）
                if (spellProtoA->Id == 47753)
                    return true;
                if (spellProtoB->Id == 47753)
                    return false;

                // 冰霜屏障（法师）
                if (spellProtoA->GetCategory() == 471)
                    return true;
                if (spellProtoB->GetCategory() == 471)
                    return false;

                // 牺牲（术士）
                if ((spellProtoA->SpellFamilyName == SPELLFAMILY_WARLOCK) &&
                    (spellProtoA->SpellIconID == 693))
                    return true;
                if ((spellProtoB->SpellFamilyName == SPELLFAMILY_WARLOCK) &&
                    (spellProtoB->SpellIconID == 693))
                    return false;

                return false;
            }
    };
}
#endif
