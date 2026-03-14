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
 * @file SpellAuras.h
 * @brief 光环系统核心头文件 - 定义光环(Aura)及其相关类
 *
 * 本文件是TrinityCore光环系统的核心头文件，定义了以下主要类：
 *
 * - AuraApplication: 光环应用类，管理光环在特定目标上的应用状态
 * - CasterInfo: 施法者信息缓存结构体
 * - Aura: 光环基类，游戏中光环效果的核心管理类
 * - UnitAura: 单位光环类，应用于单位(Unit)的光环
 * - DynObjAura: 动态对象光环类，应用于动态对象的光环
 * - ChargeDropEvent: 充能消耗事件类
 *
 * 光环系统概述：
 * 光环(Aura)是WoW中持续效果的核心概念，包括增益(Buff)、减益(Debuff)、
 * 被动效果、周期性效果等。光环可以附加到单位(玩家/NPC)或动态对象上，
 * 提供各种效果如属性修改、伤害/治疗、控制效果等。
 *
 * 关键概念：
 * - 光环效果(AuraEffect): 光环包含的具体效果，一个光环可以有多个效果
 * - 光环应用(AuraApplication): 光环应用到特定目标的实例
 * - 层数(Stack): 同一光环的叠加数量
 * - 充能(Charges): 光环可触发的次数
 * - Proc: 事件触发的光环效果
 */

#ifndef TRINITY_SPELLAURAS_H
#define TRINITY_SPELLAURAS_H

#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "UniqueTrackablePtr.h"

class SpellInfo;
struct SpellModifier;
struct ProcTriggerSpell;
struct SpellProcEntry;

// forward decl
class Aura;
class AuraEffect;
class AuraScript;
class DamageInfo;
class DispelInfo;
class DynObjAura;
class ChargeDropEvent;
class DynamicObject;
class ProcEventInfo;
class Unit;
class UnitAura;

// 每500毫秒更新光环目标地图,而不是每次更新都执行 - 减少网格搜索器调用次数
#define UPDATE_TARGET_MAP_INTERVAL 500

/**
 * @class AuraApplication
 * @brief 光环应用类 - 管理光环在特定目标上的应用状态
 *
 * 此类负责跟踪光环在单个单位上的应用状态,包括槽位、标志、效果掩码等信息。
 * 每个光环在每个目标上都有一个对应的 AuraApplication 实例。
 */
class TC_GAME_API AuraApplication
{
    friend class Unit;

    private:
        Unit* const _target;                           // 光环应用的目标单位
        Aura* const _base;                             // 光环基础对象
        AuraRemoveMode _removeMode;                    // 存储光环移除原因的信息
        uint8 _slot;                                   // 单位上的光环槽位
        uint8 _flags;                                  // 光环信息标志
        uint8 _effectsToApply;                         // 仅在法术命中时使用,确定应该应用哪些效果
        bool _needClientUpdate;                        // 是否需要客户端更新

        explicit AuraApplication(Unit* target, Unit* caster, Aura* base, uint8 effMask);
        void _Remove();                                // 内部移除方法

        void _InitFlags(Unit* caster, uint8 effMask);  // 初始化光环标志
        void _HandleEffect(uint8 effIndex, bool apply); // 处理指定效果的应用或移除

    public:
        /**
         * @brief 获取光环应用的目标单位
         * @return 目标单位指针，永不为nullptr
         */
        Unit* GetTarget() const { return _target; }

        /**
         * @brief 获取光环基础对象
         * @return 光环对象指针，永不为nullptr
         */
        Aura* GetBase() const { return _base; }

        /**
         * @brief 获取光环在客户端显示的槽位索引
         * @return 槽位索引(0-MAX_AURAS)，MAX_AURAS表示无可用槽位
         * @note 客户端最多显示MAX_AURAS(255)个光环
         */
        uint8 GetSlot() const { return _slot; }

        /**
         * @brief 获取光环标志位
         * @return 标志位组合，包含正面/负面、效果索引、施法者等信息
         * @see AFlags枚举定义
         */
        uint8 GetFlags() const { return _flags; }

        /**
         * @brief 获取已应用效果的掩码
         * @return 效果索引位掩码(0-7位表示效果0-2)
         */
        uint8 GetEffectMask() const { return _flags & (AFLAG_EFF_INDEX_0 | AFLAG_EFF_INDEX_1 | AFLAG_EFF_INDEX_2); }

        /**
         * @brief 检查是否具有指定索引的效果
         * @param effect 效果索引(0-2)
         * @return true如果该效果已应用
         */
        bool HasEffect(uint8 effect) const { ASSERT(effect < MAX_SPELL_EFFECTS); return (_flags & (1 << effect)) != 0; }

        /**
         * @brief 检查光环是否为正面效果
         * @return true如果是正面效果
         */
        bool IsPositive() const { return (_flags & AFLAG_POSITIVE) != 0; }

        /**
         * @brief 检查光环是否为自身施法
         * @return true如果施法者等于目标
         */
        bool IsSelfcast() const { return (_flags & AFLAG_CASTER) != 0; }

        /**
         * @brief 获取待应用的效果掩码
         * @return 效果掩码，用于确定哪些效果应该被应用
         * @note 仅在法术命中时使用，之后与已应用效果掩码相同
         */
        uint8 GetEffectsToApply() const { return _effectsToApply; }

        /**
         * @brief 更新待应用效果掩码
         * @param newEffMask 新的效果掩码
         * @param canHandleNewEffects 是否立即处理新添加的效果
         * @note 如果所有效果都被移除，将移除整个光环应用
         */
        void UpdateApplyEffectMask(uint8 newEffMask, bool canHandleNewEffects);

        /**
         * @brief 设置光环移除模式
         * @param mode 移除原因
         */
        void SetRemoveMode(AuraRemoveMode mode) { _removeMode = mode; }

        /**
         * @brief 获取光环移除模式
         * @return 移除原因
         */
        AuraRemoveMode GetRemoveMode() const { return _removeMode; }

        /**
         * @brief 标记需要向客户端发送更新
         */
        void SetNeedClientUpdate() { _needClientUpdate = true;}

        /**
         * @brief 检查是否需要客户端更新
         * @return true如果需要发送更新包
         */
        bool IsNeedClientUpdate() const { return _needClientUpdate;}

        /**
         * @brief 构建光环更新数据包
         * @param data 输出字节缓冲区
         * @param remove 是否为移除操作
         */
        void BuildUpdatePacket(ByteBuffer& data, bool remove) const;

        /**
         * @brief 向客户端发送光环更新
         * @param remove 是否为移除操作，默认false表示添加/更新
         */
        void ClientUpdate(bool remove = false);

        /**
         * @brief 获取调试信息字符串
         * @return 包含光环和目标信息的字符串
         */
        std::string GetDebugInfo() const;
};

/**
 * @struct CasterInfo
 * @brief 施法者信息缓存结构体
 *
 * 缓存施法者的一些信息,因为施法者可能已经不存在了。
 * 这些信息在光环生命周期内需要保持可用。
 */
struct CasterInfo
{
    float CritChance = 0.f;        // 暴击几率
    float BonusDonePct = 0.f;      // 造成伤害/治疗效果加成百分比
    uint8 Level = 0;               // 施法者等级
    bool  ApplyResilience = false; // 是否应用韧性
};

/**
 * @class Aura
 * @brief 光环类 - 游戏中光环效果的核心管理类
 *
 * Aura 类是 TrinityCore 中光环系统的核心,负责管理法术光环的生命周期、
 * 效果应用、持续时间、层数、充能等各个方面。光环可以应用于单位(Unit)
 * 或动态对象(DynamicObject),提供各种增益、减益或特殊效果。
 *
 * 主要功能:
 * - 光环的创建、应用、更新和移除
 * - 管理光环效果(AuraEffect)
 * - 处理光环的持续时间和刷新
 * - 管理光环的层数(Stack)和充能(Charges)
 * - 处理光环的触发(Proc)机制
 * - 与AuraScript脚本系统交互
 */
class TC_GAME_API Aura
{
    friend class Unit;

    public:
        // 应用映射类型定义 - 将目标GUID映射到光环应用对象
        typedef std::unordered_map<ObjectGuid, AuraApplication*> ApplicationMap;

        /**
         * @brief 为拥有者构建效果掩码
         * @param spellProto 法术信息
         * @param availableEffectMask 可用效果掩码
         * @param owner 拥有者对象(单位或动态对象)
         * @return 过滤后的效果掩码
         * @note 根据拥有者类型过滤效果，单位只能拥有单位光环效果，动态对象只能拥有区域光环效果
         */
        static uint8 BuildEffectMaskForOwner(SpellInfo const* spellProto, uint8 availableEffectMask, WorldObject* owner);

        /**
         * @brief 尝试刷新现有光环层数或创建新光环
         * @param createInfo 光环创建信息
         * @param updateEffectMask 是否更新效果掩码，默认true
         * @return 返回现有或新创建的光环指针，失败返回nullptr
         * @note 首先尝试找到可叠加的现有光环，如果找到则刷新层数和持续时间
         *       如果未找到则创建新光环
         */
        static Aura* TryRefreshStackOrCreate(AuraCreateInfo& createInfo, bool updateEffectMask = true);

        /**
         * @brief 尝试创建光环
         * @param createInfo 光环创建信息
         * @return 光环指针，失败返回nullptr
         * @note 先构建效果掩码，如果有效则调用Create
         */
        static Aura* TryCreate(AuraCreateInfo& createInfo);

        /**
         * @brief 创建光环对象
         * @param createInfo 光环创建信息
         * @return 光环指针，失败返回nullptr
         * @note 根据拥有者类型创建UnitAura或DynObjAura
         *       性能注意事项: 创建光环可能触发脚本，导致光环立即被移除
         */
        static Aura* Create(AuraCreateInfo& createInfo);

        /**
         * @brief 光环构造函数
         * @param createInfo 光环创建信息
         * @note 初始化光环的基本属性：持续时间、充能、施法者信息等
         */
        explicit Aura(AuraCreateInfo const& createInfo);

        /**
         * @brief 初始化光环效果
         * @param effMask 效果掩码
         * @param caster 施法者
         * @param baseAmount 基础数值数组
         * @note 必须在构造函数外调用，因为AuraEffect构造函数使用多态
         */
        void _InitEffects(uint8 effMask, Unit* caster, int32 const* baseAmount);

        /**
         * @brief 保存施法者信息到缓存
         * @param caster 施法者单位
         * @note 缓存施法者等级、暴击几率等信息，因为施法者可能在光环生命周期内消失
         */
        void SaveCasterInfo(Unit* caster);

        /**
         * @brief 虚析构函数
         */
        virtual ~Aura();

        /**
         * @brief 获取法术信息
         * @return 法术信息指针，永不为nullptr
         */
        SpellInfo const* GetSpellInfo() const { return m_spellInfo; }

        /**
         * @brief 获取法术ID
         * @return 法术ID
         */
        uint32 GetId() const{ return GetSpellInfo()->Id; }

        /**
         * @brief 获取施法物品GUID
         * @return 施法物品的GUID，无物品则为空GUID
         */
        ObjectGuid GetCastItemGUID() const { return m_castItemGuid; }

        /**
         * @brief 获取施法者GUID
         * @return 施法者的GUID
         */
        ObjectGuid GetCasterGUID() const { return m_casterGuid; }

        /**
         * @brief 获取施法者
         * @return 施法者单位指针，可能为nullptr（施法者已离开世界）
         */
        Unit* GetCaster() const;

        /**
         * @brief 获取光环拥有者
         * @return 拥有者指针(单位或动态对象)
         */
        WorldObject* GetOwner() const { return m_owner; }

        /**
         * @brief 获取单位拥有者
         * @return 单位指针
         * @note 仅用于UNIT_AURA_TYPE类型的光环，否则触发断言
         */
        Unit* GetUnitOwner() const { ASSERT(GetType() == UNIT_AURA_TYPE); return m_owner->ToUnit(); }

        /**
         * @brief 获取动态对象拥有者
         * @return 动态对象指针
         * @note 仅用于DYNOBJ_AURA_TYPE类型的光环，否则触发断言
         */
        DynamicObject* GetDynobjOwner() const { ASSERT(GetType() == DYNOBJ_AURA_TYPE); return m_owner->ToDynObject(); }

        /**
         * @brief 获取光环对象类型
         * @return UNIT_AURA_TYPE或DYNOBJ_AURA_TYPE
         */
        AuraObjectType GetType() const;

        /**
         * @brief 为目标应用光环（内部方法）
         * @param target 目标单位
         * @param caster 施法者
         * @param auraApp 光环应用对象
         * @note 将光环应用注册到目标的aura应用列表
         */
        virtual void _ApplyForTarget(Unit* target, Unit* caster, AuraApplication * auraApp);

        /**
         * @brief 为目标取消应用光环（内部方法）
         * @param target 目标单位
         * @param caster 施法者
         * @param auraApp 光环应用对象
         * @note 从目标的aura应用列表中移除
         */
        virtual void _UnapplyForTarget(Unit* target, Unit* caster, AuraApplication * auraApp);

        /**
         * @brief 内部移除方法
         * @param removeMode 移除原因
         * @note 设置移除标志，清理所有应用
         */
        void _Remove(AuraRemoveMode removeMode);

        /**
         * @brief 移除光环（纯虚函数）
         * @param removeMode 移除原因，默认AURA_REMOVE_BY_DEFAULT
         * @note 由子类实现具体的移除逻辑
         */
        virtual void Remove(AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT) = 0;

        /**
         * @brief 填充目标映射（纯虚函数）
         * @param targets 输出目标映射（单位指针到效果掩码）
         * @param caster 施法者
         * @note UnitAura实现为返回自身，DynObjAura实现为返回区域内单位
         */
        virtual void FillTargetMap(std::unordered_map<Unit*, uint8>& targets, Unit* caster) = 0;

        /**
         * @brief 更新目标映射
         * @param caster 施法者
         * @param apply 是否应用光环，默认true
         * @note 根据FillTargetMap获取的目标，应用或移除光环
         *       性能注意事项: 区域光环每500ms更新一次目标映射以减少性能消耗
         */
        void UpdateTargetMap(Unit* caster, bool apply = true);

        /**
         * @brief 为目标注册光环（不应用效果）
         * @note 创建光环时使用，只注册不应用效果
         */
        void _RegisterForTargets() { Unit* caster = GetCaster(); UpdateTargetMap(caster, false); }

        /**
         * @brief 为目标应用光环
         * @note 应用效果到所有目标
         */
        void ApplyForTargets() { Unit* caster = GetCaster(); UpdateTargetMap(caster, true); }

        /**
         * @brief 为所有目标应用指定效果
         * @param effIndex 效果索引
         */
        void _ApplyEffectForTargets(uint8 effIndex);

        /**
         * @brief 更新光环拥有者
         * @param diff 时间差（毫秒）
         * @param owner 拥有者
         * @note 入口点，调用Update处理光环逻辑
         */
        void UpdateOwner(uint32 diff, WorldObject* owner);

        /**
         * @brief 更新光环状态
         * @param diff 时间差（毫秒）
         * @param caster 施法者
         * @note 每帧调用，处理：
         *       - 持续时间递减
         *       - 周期性效果触发
         *       - 每秒能量消耗
         *       - 目标映射更新
         */
        void Update(uint32 diff, Unit* caster);

        /**
         * @brief 获取光环应用时间
         * @return 应用时间戳
         */
        time_t GetApplyTime() const { return m_applyTime; }

        /**
         * @brief 获取最大持续时间
         * @return 最大持续时间（毫秒），-1表示永久
         */
        int32 GetMaxDuration() const { return m_maxDuration; }

        /**
         * @brief 设置最大持续时间
         * @param duration 持续时间（毫秒）
         */
        void SetMaxDuration(int32 duration) { m_maxDuration = duration; }

        /**
         * @brief 计算最大持续时间（使用当前施法者）
         * @return 计算后的最大持续时间
         */
        int32 CalcMaxDuration() const { return CalcMaxDuration(GetCaster()); }

        /**
         * @brief 计算最大持续时间（指定施法者）
         * @param caster 施法者
         * @return 计算后的最大持续时间，考虑施法者等级、天赋等加成
         */
        int32 CalcMaxDuration(Unit* caster) const;

        /**
         * @brief 静态方法：计算最大持续时间
         * @param spellInfo 法术信息
         * @param caster 施法者
         * @return 计算后的最大持续时间
         */
        static int32 CalcMaxDuration(SpellInfo const* spellInfo, WorldObject* caster);

        /**
         * @brief 获取当前持续时间
         * @return 当前剩余时间（毫秒），-1表示永久，0表示已过期
         */
        int32 GetDuration() const { return m_duration; }

        /**
         * @brief 设置当前持续时间
         * @param duration 持续时间（毫秒）
         * @param withMods 是否应用持续时间修正，默认false
         */
        void SetDuration(int32 duration, bool withMods = false);

        /**
         * @brief 刷新持续时间
         * @param withMods 是否应用持续时间修正，默认false
         * @note 将持续时间重置为最大持续时间
         */
        void RefreshDuration(bool withMods = false);

        /**
         * @brief 刷新所有计时器
         * @param resetPeriodicTimer 是否重置周期性计时器
         */
        void RefreshTimers(bool resetPeriodicTimer);

        /**
         * @brief 检查光环是否已过期
         * @return true如果持续时间归零且没有延迟消耗事件
         */
        bool IsExpired() const { return !GetDuration() && !m_dropEvent; }

        /**
         * @brief 检查是否为永久光环
         * @return true如果最大持续时间为-1
         */
        bool IsPermanent() const { return GetMaxDuration() == -1; }

        /**
         * @brief 获取充能次数
         * @return 当前充能次数，0表示无充能或无限
         */
        uint8 GetCharges() const { return m_procCharges; }

        /**
         * @brief 设置充能次数
         * @param charges 新的充能次数
         */
        void SetCharges(uint8 charges);

        /**
         * @brief 计算最大充能次数（指定施法者）
         * @param caster 施法者
         * @return 最大充能次数
         */
        uint8 CalcMaxCharges(Unit* caster) const;

        /**
         * @brief 计算最大充能次数（使用当前施法者）
         * @return 最大充能次数
         */
        uint8 CalcMaxCharges() const { return CalcMaxCharges(GetCaster()); }

        /**
         * @brief 修改充能次数
         * @param num 修改值，负数表示减少
         * @param removeMode 移除原因（充能耗尽时使用）
         * @return true如果光环仍然存在，false如果光环被移除
         */
        bool ModCharges(int32 num, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        /**
         * @brief 消耗一次充能
         * @param removeMode 移除原因
         * @return true如果光环仍然存在
         */
        bool DropCharge(AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT) { return ModCharges(-1, removeMode); }

        /**
         * @brief 延迟修改充能次数
         * @param num 修改值，负数表示减少
         * @param removeMode 移除原因
         * @note 创建延迟事件，在事件触发时才真正修改充能
         *       用于某些需要延迟消耗充能的技能
         */
        void ModChargesDelayed(int32 num, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        /**
         * @brief 延迟消耗充能
         * @param delay 延迟时间（毫秒）
         * @param removeMode 移除原因
         */
        void DropChargeDelayed(uint32 delay, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        /**
         * @brief 获取叠加层数
         * @return 当前叠加层数（1-255）
         */
        uint8 GetStackAmount() const { return m_stackAmount; }

        /**
         * @brief 设置叠加层数
         * @param num 新的层数
         * @note 会触发效果数值的重新计算
         */
        void SetStackAmount(uint8 num);

        /**
         * @brief 修改叠加层数
         * @param num 修改值，正数增加，负数减少
         * @param removeMode 层数归零时的移除原因
         * @param resetPeriodicTimer 是否重置周期性计时器，默认true
         * @return true如果光环仍然存在
         */
        bool ModStackAmount(int32 num, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT, bool resetPeriodicTimer = true);

        /**
         * @brief 检查是否可以应用韧性
         * @return true如果应该应用韧性计算
         */
        bool  CanApplyResilience() const { return _casterInfo.ApplyResilience; }

        /**
         * @brief 设置是否应用韧性
         * @param val 是否应用韧性标志
         */
        void SetCanApplyResilience(bool val) { _casterInfo.ApplyResilience = val; }

        /**
         * @brief 获取施法者等级
         * @return 施法者在施法时的等级
         * @note 缓存值，即使施法者等级变化也保持不变
         */
        uint8 GetCasterLevel() const { return _casterInfo.Level; }

        /**
         * @brief 获取暴击几率
         * @return 施法者的暴击几率
         */
        float GetCritChance() const { return _casterInfo.CritChance; }

        /**
         * @brief 设置暴击几率
         * @param val 新的暴击几率
         */
        void SetCritChance(float val) { _casterInfo.CritChance = val; }

        /**
         * @brief 获取造成伤害/治疗百分比加成
         * @return 百分比加成值
         */
        float GetDonePct() const { return _casterInfo.BonusDonePct; }

        /**
         * @brief 设置造成伤害/治疗百分比加成
         * @param val 新的百分比加成
         */
        void SetDonePct(float val) { _casterInfo.BonusDonePct = val; }

        /**
         * @brief 检查指定类型的效果是否多于一个
         * @param auraType 光环效果类型
         * @return true如果有多个相同类型的效果
         */
        bool HasMoreThanOneEffectForType(AuraType auraType) const;

        /**
         * @brief 检查是否为区域光环
         * @return true如果光环有区域效果
         */
        bool IsArea() const;

        /**
         * @brief 检查是否为被动光环
         * @return true如果是被动技能的光环
         */
        bool IsPassive() const;

        /**
         * @brief 检查是否在死亡后持续存在
         * @return true如果光环在死亡后仍然存在
         */
        bool IsDeathPersistent() const;

        /**
         * @brief 检查在变形丢失后是否被移除
         * @param target 目标单位
         * @return true如果目标失去当前变形时光环应该被移除
         */
        bool IsRemovedOnShapeLost(Unit* target) const;

        /**
         * @brief 检查光环是否可以被保存到数据库
         * @return true如果可以保存
         * @note 某些光环（如临时效果）不应被保存
         */
        bool CanBeSaved() const;

        /**
         * @brief 检查光环是否已被移除
         * @return true如果光环已标记为移除
         */
        bool IsRemoved() const { return m_isRemoved; }

        /**
         * @brief 检查光环是否可以发送到客户端
         * @return true如果客户端需要知道这个光环
         */
        bool CanBeSentToClient() const;

        // ========== 单目标光环辅助方法 ==========

        /**
         * @brief 检查是否为单目标光环
         * @return true如果是单目标光环
         * @note 单目标光环只能同时存在于一个目标上
         */
        bool IsSingleTarget() const {return m_isSingleTarget; }

        /**
         * @brief 检查是否与另一个光环为单目标共存
         * @param aura 另一个光环
         * @return true如果两个光环可以共存
         */
        bool IsSingleTargetWith(Aura const* aura) const;

        /**
         * @brief 设置单目标标志
         * @param val 单目标标志值
         */
        void SetIsSingleTarget(bool val) { m_isSingleTarget = val; }

        /**
         * @brief 取消注册单目标光环
         * @note 从施法者的单目标光环列表中移除
         */
        void UnregisterSingleTarget();

        /**
         * @brief 计算被驱散的几率
         * @param auraTarget 光环目标
         * @param offensive 是否为进攻性驱散
         * @return 驱散几率（百分比）
         */
        int32 CalcDispelChance(Unit const* auraTarget, bool offensive) const;

        /**
         * @brief 设置加载状态
         * @param maxduration 最大持续时间
         * @param duration 当前持续时间
         * @param charges 充能次数
         * @param stackamount 叠加层数
         * @param recalculateMask 需要重新计算的效果掩码
         * @param critChance 暴击几率
         * @param applyResilience 是否应用韧性
         * @param amount 效果数值数组
         * @note 从数据库加载光环时使用，恢复保存的状态
         */
        void SetLoadedState(int32 maxduration, int32 duration, int32 charges, uint8 stackamount, uint8 recalculateMask, float critChance, bool applyResilience, int32* amount);

        // ========== 光环效果辅助方法 ==========

        /**
         * @brief 检查周期性效果是否可以暴击
         * @param caster 施法者
         * @return true如果周期性效果可以暴击
         */
        bool CanPeriodicTickCrit(Unit const* caster) const;

        /**
         * @brief 计算周期性效果的暴击几率
         * @param caster 施法者
         * @return 暴击几率（百分比）
         */
        float CalcPeriodicCritChance(Unit const* caster) const;

        /**
         * @brief 检查是否有指定索引的效果
         * @param effIndex 效果索引(0-2)
         * @return true如果该效果存在
         */
        bool HasEffect(uint8 effIndex) const { return GetEffect(effIndex) != nullptr; }

        /**
         * @brief 检查是否有指定类型的效果
         * @param type 光环效果类型
         * @return true如果存在该类型的效果
         */
        bool HasEffectType(AuraType type) const;

        /**
         * @brief 获取指定索引的效果
         * @param effIndex 效果索引(0-2)
         * @return 效果指针，不存在则返回nullptr
         */
        AuraEffect* GetEffect(uint8 effIndex) const { ASSERT (effIndex < MAX_SPELL_EFFECTS); return m_effects[effIndex]; }

        /**
         * @brief 获取所有效果的有效掩码
         * @return 效果位掩码
         */
        uint8 GetEffectMask() const { uint8 effMask = 0; for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i) if (m_effects[i]) effMask |= 1<<i; return effMask; }

        /**
         * @brief 重新计算所有效果的数值
         * @note 当影响效果数值的条件变化时调用
         */
        void RecalculateAmountOfEffects();

        /**
         * @brief 处理所有效果的应用或移除
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         * @param apply true表示应用，false表示移除
         */
        void HandleAllEffects(AuraApplication * aurApp, uint8 mode, bool apply);

        // ========== 目标辅助方法 ==========

        /**
         * @brief 获取应用映射
         * @return 所有目标的应用映射（GUID -> AuraApplication）
         */
        ApplicationMap const& GetApplicationMap() { return m_applications; }

        /**
         * @brief 获取应用向量
         * @param applicationVector 输出向量
         */
        void GetApplicationVector(std::vector<AuraApplication*>& applicationVector) const;

        /**
         * @brief 获取指定目标的应用对象（常量版本）
         * @param guid 目标GUID
         * @return 应用对象指针，不存在则返回nullptr
         */
        AuraApplication const* GetApplicationOfTarget(ObjectGuid guid) const { ApplicationMap::const_iterator itr = m_applications.find(guid); if (itr != m_applications.end()) return itr->second; return nullptr; }

        /**
         * @brief 获取指定目标的应用对象
         * @param guid 目标GUID
         * @return 应用对象指针，不存在则返回nullptr
         */
        AuraApplication* GetApplicationOfTarget(ObjectGuid guid) { ApplicationMap::iterator itr = m_applications.find(guid); if (itr != m_applications.end()) return itr->second; return nullptr; }

        /**
         * @brief 检查是否应用于指定目标
         * @param guid 目标GUID
         * @return true如果光环应用于该目标
         */
        bool IsAppliedOnTarget(ObjectGuid guid) const { return m_applications.find(guid) != m_applications.end(); }

        /**
         * @brief 为所有目标标记需要客户端更新
         */
        void SetNeedClientUpdateForTargets() const;

        /**
         * @brief 处理光环特定的修改
         * @param aurApp 光环应用对象
         * @param caster 施法者
         * @param apply true表示应用，false表示移除
         * @param onReapply 是否为重新应用
         */
        void HandleAuraSpecificMods(AuraApplication const* aurApp, Unit* caster, bool apply, bool onReapply);

        /**
         * @brief 检查光环是否可以应用于指定目标
         * @param target 目标单位
         * @return true如果可以应用
         */
        bool CanBeAppliedOn(Unit* target);

        /**
         * @brief 检查区域目标是否有效
         * @param target 目标单位
         * @return true如果目标有效
         */
        bool CheckAreaTarget(Unit* target);

        /**
         * @brief 检查是否可以与现有光环叠加
         * @param existingAura 现有光环
         * @return true如果可以叠加或共存
         */
        bool CanStackWith(Aura const* existingAura) const;

        // ========== Proc系统方法 ==========

        /**
         * @brief 检查Proc是否在冷却中
         * @param now 当前时间点
         * @return true如果在冷却中
         */
        bool IsProcOnCooldown(TimePoint now) const;

        /**
         * @brief 添加Proc冷却
         * @param cooldownEnd 冷却结束时间点
         */
        void AddProcCooldown(TimePoint cooldownEnd);

        /**
         * @brief 重置Proc冷却
         */
        void ResetProcCooldown();

        /**
         * @brief 检查是否使用充能
         * @return true如果光环使用充能系统
         */
        bool IsUsingCharges() const { return m_isUsingCharges; }

        /**
         * @brief 设置是否使用充能
         * @param val 是否使用充能标志
         */
        void SetUsingCharges(bool val) { m_isUsingCharges = val; }

        /**
         * @brief 准备触发Proc
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @param now 当前时间点
         * @note 消耗充能并设置冷却
         */
        void PrepareProcToTrigger(AuraApplication* aurApp, ProcEventInfo& eventInfo, TimePoint now);

        /**
         * @brief 获取Proc效果掩码
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @param now 当前时间点
         * @return 应该触发的效果掩码
         */
        uint8 GetProcEffectMask(AuraApplication* aurApp, ProcEventInfo& eventInfo, TimePoint now) const;

        /**
         * @brief 计算Proc触发几率
         * @param procEntry Proc配置信息
         * @param eventInfo 触发事件信息
         * @return 触发几率（0.0-100.0）
         */
        float CalcProcChance(SpellProcEntry const& procEntry, ProcEventInfo& eventInfo) const;

        /**
         * @brief 在事件上触发Proc
         * @param procEffectMask 效果掩码
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void TriggerProcOnEvent(uint8 procEffectMask, AuraApplication* aurApp, ProcEventInfo& eventInfo);

        // ========== AuraScript脚本接口 ==========

        /**
         * @brief 加载关联的AuraScript脚本
         */
        void LoadScripts();

        /**
         * @brief 调用脚本区域目标检查处理器
         * @param target 目标单位
         * @return true如果脚本允许该目标
         */
        bool CallScriptCheckAreaTargetHandlers(Unit* target);

        /**
         * @brief 调用脚本驱散处理器
         * @param dispelInfo 驱散信息
         */
        void CallScriptDispel(DispelInfo* dispelInfo);

        /**
         * @brief 调用脚本驱散后处理器
         * @param dispelInfo 驱散信息
         */
        void CallScriptAfterDispel(DispelInfo* dispelInfo);

        /**
         * @brief 调用脚本效果应用处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         * @return true如果脚本处理了该效果
         */
        bool CallScriptEffectApplyHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, AuraEffectHandleModes mode);

        /**
         * @brief 调用脚本效果移除处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         * @return true如果脚本处理了该效果
         */
        bool CallScriptEffectRemoveHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, AuraEffectHandleModes mode);

        /**
         * @brief 调用脚本效果应用后处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         */
        void CallScriptAfterEffectApplyHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, AuraEffectHandleModes mode);

        /**
         * @brief 调用脚本效果移除后处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param mode 处理模式
         */
        void CallScriptAfterEffectRemoveHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, AuraEffectHandleModes mode);

        /**
         * @brief 调用脚本周期性效果处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @return true如果脚本处理了该效果
         */
        bool CallScriptEffectPeriodicHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp);

        /**
         * @brief 调用脚本周期性效果更新处理器
         * @param aurEff 光环效果
         */
        void CallScriptEffectUpdatePeriodicHandlers(AuraEffect* aurEff);

        /**
         * @brief 调用脚本效果数值计算处理器
         * @param aurEff 光环效果
         * @param amount 输入/输出数值
         * @param canBeRecalculated 输入/输出是否可重算标志
         */
        void CallScriptEffectCalcAmountHandlers(AuraEffect const* aurEff, int32& amount, bool& canBeRecalculated);

        /**
         * @brief 调用脚本周期性计算处理器
         * @param aurEff 光环效果
         * @param isPeriodic 输入/输出是否周期性标志
         * @param amplitude 输入/输出周期间隔
         */
        void CallScriptEffectCalcPeriodicHandlers(AuraEffect const* aurEff, bool& isPeriodic, int32& amplitude);

        /**
         * @brief 调用脚本法术修改器计算处理器
         * @param aurEff 光环效果
         * @param spellMod 输出法术修改器
         */
        void CallScriptEffectCalcSpellModHandlers(AuraEffect const* aurEff, SpellModifier*& spellMod);

        /**
         * @brief 调用脚本吸收效果处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param dmgInfo 伤害信息
         * @param absorbAmount 输入/输出吸收量
         * @param defaultPrevented 输出是否阻止默认处理
         */
        void CallScriptEffectAbsorbHandlers(AuraEffect* aurEff, AuraApplication const* aurApp, DamageInfo& dmgInfo, uint32& absorbAmount, bool& defaultPrevented);

        /**
         * @brief 调用脚本吸收效果后处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param dmgInfo 伤害信息
         * @param absorbAmount 吸收量
         */
        void CallScriptEffectAfterAbsorbHandlers(AuraEffect* aurEff, AuraApplication const* aurApp, DamageInfo& dmgInfo, uint32& absorbAmount);

        /**
         * @brief 调用脚本法力护盾处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param dmgInfo 伤害信息
         * @param absorbAmount 输入/输出吸收量
         * @param defaultPrevented 输出是否阻止默认处理
         */
        void CallScriptEffectManaShieldHandlers(AuraEffect* aurEff, AuraApplication const* aurApp, DamageInfo& dmgInfo, uint32& absorbAmount, bool& defaultPrevented);

        /**
         * @brief 调用脚本法力护盾后处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param dmgInfo 伤害信息
         * @param absorbAmount 吸收量
         */
        void CallScriptEffectAfterManaShieldHandlers(AuraEffect* aurEff, AuraApplication const* aurApp, DamageInfo& dmgInfo, uint32& absorbAmount);

        /**
         * @brief 调用脚本分裂效果处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param dmgInfo 伤害信息
         * @param splitAmount 输入/输出分裂量
         */
        void CallScriptEffectSplitHandlers(AuraEffect* aurEff, AuraApplication const* aurApp, DamageInfo& dmgInfo, uint32& splitAmount);

        // ========== Proc钩子方法 ==========

        /**
         * @brief 调用脚本Proc检查处理器
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果脚本允许触发
         */
        bool CallScriptCheckProcHandlers(AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本效果Proc检查处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果脚本允许该效果触发
         */
        bool CallScriptCheckEffectProcHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本准备Proc处理器
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果脚本处理成功
         */
        bool CallScriptPrepareProcHandlers(AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本Proc处理器
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果脚本处理了触发
         */
        bool CallScriptProcHandlers(AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本Proc后处理器
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void CallScriptAfterProcHandlers(AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本效果Proc处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         * @return true如果脚本处理了触发
         */
        bool CallScriptEffectProcHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        /**
         * @brief 调用脚本效果Proc后处理器
         * @param aurEff 光环效果
         * @param aurApp 光环应用对象
         * @param eventInfo 触发事件信息
         */
        void CallScriptAfterEffectProcHandlers(AuraEffect const* aurEff, AuraApplication const* aurApp, ProcEventInfo& eventInfo);

        // ========== 类型转换方法 ==========

        /**
         * @brief 转换为单位光环类型
         * @return 如果是UnitAura则返回转换后的指针，否则返回nullptr
         */
        UnitAura* ToUnitAura() { if (GetType() == UNIT_AURA_TYPE) return reinterpret_cast<UnitAura*>(this); else return nullptr; }

        /**
         * @brief 转换为单位光环类型（常量版本）
         * @return 如果是UnitAura则返回转换后的常量指针，否则返回nullptr
         */
        UnitAura const* ToUnitAura() const { if (GetType() == UNIT_AURA_TYPE) return reinterpret_cast<UnitAura const*>(this); else return nullptr; }

        /**
         * @brief 转换为动态对象光环类型
         * @return 如果是DynObjAura则返回转换后的指针，否则返回nullptr
         */
        DynObjAura* ToDynObjAura() { if (GetType() == DYNOBJ_AURA_TYPE) return reinterpret_cast<DynObjAura*>(this); else return nullptr; }

        /**
         * @brief 转换为动态对象光环类型（常量版本）
         * @return 如果是DynObjAura则返回转换后的常量指针，否则返回nullptr
         */
        DynObjAura const* ToDynObjAura() const { if (GetType() == DYNOBJ_AURA_TYPE) return reinterpret_cast<DynObjAura const*>(this); else return nullptr; }

        /**
         * @brief 获取指定名称的脚本对象
         * @tparam Script 脚本类型
         * @param scriptName 脚本名称
         * @return 脚本指针，如果不存在则返回nullptr
         */
        template <class Script>
        Script* GetScript(std::string const& scriptName) const
        {
            return dynamic_cast<Script*>(GetScriptByName(scriptName));
        }

        /** @brief 已加载的脚本列表 */
        std::vector<AuraScript*> m_loadedScripts;

        /**
         * @brief 获取调试信息
         * @return 包含光环详细信息的字符串
         */
        virtual std::string GetDebugInfo() const;

        /**
         * @brief 获取弱指针
         * @return 用于脚本系统引用的弱指针
         */
        Trinity::unique_weak_ptr<Aura> GetWeakPtr() const { return m_scriptRef; }

        // 禁止拷贝和移动
        Aura(Aura const&) = delete;
        Aura(Aura&&) = delete;
        Aura& operator=(Aura const&) = delete;
        Aura& operator=(Aura&&) = delete;

    private:
        /**
         * @brief 根据名称获取脚本
         * @param scriptName 脚本名称
         * @return 脚本指针，如果不存在则返回nullptr
         */
        AuraScript* GetScriptByName(std::string const& scriptName) const;

        /**
         * @brief 删除已移除的应用对象
         * @note 清理待删除列表中的应用对象
         */
        void _DeleteRemovedApplications();

    protected:
        // ========== 光环核心数据成员 ==========
        SpellInfo const* const m_spellInfo;               // 法术信息 - 光环对应的法术数据（只读）
        ObjectGuid const m_casterGuid;                    // 施法者GUID（只读）
        ObjectGuid const m_castItemGuid;                  // 施法物品GUID（只读）- 保存指针不安全，因为物品可能被删除
        time_t const m_applyTime;                         // 应用时间（只读）- 光环应用到目标的时间戳
        WorldObject* const m_owner;                       // 拥有者（只读）- 光环所属的世界对象（单位或动态对象）

        // ========== 持续时间相关 ==========
        int32 m_maxDuration;                              // 最大持续时间（毫秒）- -1表示永久
        int32 m_duration;                                 // 当前剩余持续时间（毫秒）- 0表示已过期
        int32 m_timeCla;                                  // 每秒能量计时器 - 用于ManaPerSecond计算
        int32 m_updateTargetMapInterval;                  // 目标映射更新计时器 - 区域光环使用

        // ========== 施法者信息和光环状态 ==========
        CasterInfo _casterInfo;                           // 施法者信息缓存 - 暴击、等级、韧性等
        uint8 m_procCharges;                              // 光环充能次数 - 0表示无限充能
        uint8 m_stackAmount;                              // 光环叠加层数 - 默认为1

        // ========== 效果和目标管理 ==========
        AuraEffect* m_effects[MAX_SPELL_EFFECTS];         // 光环效果数组 - 每个索引对应一个效果
        ApplicationMap m_applications;                    // 应用映射 - 目标GUID到光环应用对象的映射

        // ========== 状态标志位 ==========
        bool m_isRemoved:1;                               // 已移除标志 - 防止重复移除
        bool m_isSingleTarget:1;                          // 单目标法术标志 - 在施法者处注册
        bool m_isUsingCharges:1;                          // 使用充能标志

        // ========== 事件和冷却 ==========
        ChargeDropEvent* m_dropEvent;                     // 延迟充能消耗事件 - 用于延迟DropCharge
        TimePoint m_procCooldown;                         // Proc冷却结束时间点

    private:
        std::vector<AuraApplication*> _removedApplications; // 已移除的应用对象列表 - 等待清理

        /**
         * @struct NoopAuraDeleter
         * @brief 空操作删除器 - 用于智能指针但不管理生命周期
         * @note 光环由Unit管理生命周期，脚本引用使用弱指针
         */
        struct NoopAuraDeleter { void operator()(Aura*) const { /*noop - not managed*/ } };
        Trinity::unique_trackable_ptr<Aura> m_scriptRef;    // 脚本引用指针 - 用于脚本系统获取弱引用
};

/**
 * @class UnitAura
 * @brief 单位光环类 - 应用于单位的光环
 *
 * 继承自 Aura 类，专门处理应用于单位(Unit)的光环效果。
 * 大多数光环都是UnitAura类型，包括：
 * - 增益/减益效果（Buff/Debuff）
 * - 被动技能效果
 * - 持续伤害/治疗效果（DoT/HoT）
 * - 控制效果（昏迷、减速等）
 *
 * 特有功能：
 * - 递减规则（Diminishing Returns）管理
 * - 静态应用跟踪（非区域光环）
 */
class TC_GAME_API UnitAura : public Aura
{
    friend Aura* Aura::Create(AuraCreateInfo& createInfo);
    protected:
        /**
         * @brief 构造函数
         * @param createInfo 光环创建信息
         */
        explicit UnitAura(AuraCreateInfo const& createInfo);
    public:
        /**
         * @brief 为目标应用光环（重写）
         * @param target 目标单位
         * @param caster 施法者
         * @param aurApp 光环应用对象
         * @note 注册到单位的auras列表，更新单目标追踪
         */
        void _ApplyForTarget(Unit* target, Unit* caster, AuraApplication* aurApp) override;

        /**
         * @brief 为目标取消应用光环（重写）
         * @param target 目标单位
         * @param caster 施法者
         * @param aurApp 光环应用对象
         * @note 从单位的auras列表中移除，更新单目标追踪
         */
        void _UnapplyForTarget(Unit* target, Unit* caster, AuraApplication* aurApp) override;

        /**
         * @brief 移除光环（重写）
         * @param removeMode 移除原因
         * @note 从拥有者单位移除光环
         */
        void Remove(AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT) override;

        /**
         * @brief 填充目标映射（重写）
         * @param targets 输出目标映射
         * @param caster 施法者
         * @note UnitAura只返回拥有者自身作为目标
         */
        void FillTargetMap(std::unordered_map<Unit*, uint8>& targets, Unit* caster) override;

        // ========== 递减规则管理 ==========

        /**
         * @brief 设置递减组
         * @param group 递减组类型
         * @note 用于PvP控制效果的递减规则
         */
        void SetDiminishGroup(DiminishingGroup group) { m_AuraDRGroup = group; }

        /**
         * @brief 获取递减组
         * @return 递减组类型
         */
        DiminishingGroup GetDiminishGroup() const { return m_AuraDRGroup; }

        /**
         * @brief 添加静态应用
         * @param target 目标单位
         * @param effMask 效果掩码
         * @note 非区域光环在创建时记录静态应用，用于后续检查
         */
        void AddStaticApplication(Unit* target, uint8 effMask);

    private:
        DiminishingGroup m_AuraDRGroup;                           // 递减规则组 - 控制效果类型
        std::unordered_map<ObjectGuid, uint8> _staticApplications; // 静态应用映射 - 记录非区域光环的应用
};

/**
 * @class DynObjAura
 * @brief 动态对象光环类 - 应用于动态对象的光环
 *
 * 继承自 Aura 类，专门处理应用于动态对象(DynamicObject)的光环效果。
 * 动态对象光环用于区域性法术效果，特点：
 * - 光环绑定到动态对象而非单位
 * - 自动影响动态对象范围内的所有目标
 * - 目标列表会定期更新
 *
 * 典型应用：
 * - 奉献（Paladin的Consecration）
 * - 暴风雪（Mage的Blizzard）
 * - 治疗之雨（Shaman的Healing Rain）
 * - 等区域性持续效果
 */
class TC_GAME_API DynObjAura : public Aura
{
    friend Aura* Aura::Create(AuraCreateInfo& createInfo);
    protected:
        /**
         * @brief 构造函数
         * @param createInfo 光环创建信息
         */
        explicit DynObjAura(AuraCreateInfo const& createInfo);
    public:
        /**
         * @brief 移除光环（重写）
         * @param removeMode 移除原因
         * @note 从动态对象移除光环，同时移除所有目标上的应用
         */
        void Remove(AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT) override;

        /**
         * @brief 填充目标映射（重写）
         * @param targets 输出目标映射
         * @param caster 施法者
         * @note 搜索动态对象范围内所有有效目标
         *       性能注意事项：使用GridSearcher搜索，每500ms更新一次
         */
        void FillTargetMap(std::unordered_map<Unit*, uint8>& targets, Unit* caster) override;
};

/**
 * @class ChargeDropEvent
 * @brief 充能消耗事件类 - 处理延迟消耗光环充能的事件
 *
 * 继承自 BasicEvent，用于延迟消耗光环充能。
 * 某些技能需要在一段时间后才消耗充能，例如：
 * - 神圣之盾（Divine Shield）取消后的延迟
 * - 某些触发效果的延迟处理
 *
 * 工作流程：
 * 1. 调用DropChargeDelayed创建事件并添加到事件队列
 * 2. 等待指定的延迟时间
 * 3. 事件执行时调用DropCharge消耗充能
 * 4. 如果充能耗尽，光环被移除
 */
class TC_GAME_API ChargeDropEvent : public BasicEvent
{
    friend class Aura;
    protected:
        /**
         * @brief 构造函数
         * @param base 光环基础对象
         * @param mode 移除模式
         */
        ChargeDropEvent(Aura* base, AuraRemoveMode mode) : _base(base), _mode(mode) { }

        /**
         * @brief 执行事件
         * @param e_time 事件执行时间
         * @param p_time 事件处理时间
         * @return true表示事件处理成功
         * @note 调用光环的DropCharge方法消耗充能
         */
        bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override;

    private:
        Aura* _base;                   // 光环基础对象 - 要消耗充能的光环
        AuraRemoveMode _mode;          // 移除模式 - 充能耗尽时使用
};
#endif
