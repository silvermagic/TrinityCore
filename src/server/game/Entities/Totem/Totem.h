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
 * @file Totem.h
 * @brief 图腾实体类定义
 *
 * 本文件定义了图腾（Totem）类，用于处理萨满图腾的生命周期管理、
 * 法术施放、持续时间追踪等核心功能。图腾是萨满职业特有的临时召唤物，
 * 提供各种增益效果、伤害输出或控制能力。
 *
 * 主要功能：
 * - 图腾类型管理（被动/主动/雕像）
 * - 持续时间追踪和自动过期
 * - 图腾法术的自动施放
 * - 特殊免疫规则处理
 *
 * @see Minion 图腾继承自仆从基类
 * @see TemporarySummon 临时召唤基类
 */

#ifndef TRINITYCORE_TOTEM_H
#define TRINITYCORE_TOTEM_H

#include "TemporarySummon.h"

/**
 * @brief 图腾类型枚举
 *
 * 定义了游戏中图腾的三种类型，影响图腾的行为模式和法术施放方式。
 * 不同类型的图腾在AI处理、法术触发机制上有所区别。
 */
enum TotemType
{
    TOTEM_PASSIVE    = 0,  /**< 被动图腾 - 自动施放光环效果，无需手动激活 */
    TOTEM_ACTIVE     = 1,  /**< 主动图腾 - 需要通过AI主动施放法术 */
    TOTEM_STATUE     = 2   /**< 雕像类型图腾 - 特殊图腾类型（从MaNGOS继承，部分功能待完善） */
};

/**
 * @name 图腾法术和条目ID常量
 * @{
 * 某些图腾的特殊法术ID未在生物数据库中配置，需要在此硬编码定义。
 * 这些常量用于图腾的初始化和法术施放逻辑。
 */

/** 哨兵图腾法术ID - 提供远程视野能力 */
#define SENTRY_TOTEM_SPELLID  6495

/** 哨兵图腾条目ID - 数据库中的生物条目编号 */
#define SENTRY_TOTEM_ENTRY    3968

/** 石爪图腾法术ID - 提供护盾保护效果 */
#define SENTRY_STONECLAW_SPELLID  55277

/** 束缚视野法术ID - 允许玩家通过图腾观察远处 */
#define SENTRY_BIND_SIGHT_SPELLID  6277

/** @} */

/**
 * @class Totem
 * @brief 图腾实体类 - 萨满职业的临时召唤物
 *
 * 继承自 Minion（仆从）类，专门用于处理萨满图腾的创建、管理和行为。
 * 图腾是游戏中萨满职业特有的召唤物，提供各种增益效果、伤害输出或控制能力。
 *
 * 继承关系：
 *     Object -> WorldObject -> Unit -> Creature -> TempSummon -> Minion -> Totem
 *
 * 核心特性：
 * - 固定位置：图腾召唤后在原地不动，无法移动
 * - 持续时间：图腾有固定的存活时间，到期后自动消失
 * - 法术施放：根据类型（被动/主动）自动施放法术
 * - 特殊免疫：对部分负面效果有免疫能力
 * - 主人绑定：始终跟随施放者，受其属性加成影响
 *
 * 生命周期：
 * 1. 施放法术 -> 2. 创建图腾对象 -> 3. InitStats() 初始化属性
 * 4. InitSummon() 设置法术 -> 5. Update() 持续更新 -> 6. UnSummon() 移除
 *
 * @note 图腾的属性（生命值、护甲、抗性等）不从 Creature 基类继承标准计算逻辑，
 *       而是使用固定的简化规则。
 *
 * @see Minion 仆从基类，提供主人关联逻辑
 * @see TempSummon 临时召唤基类，提供持续时间管理
 * @see TotemType 图腾类型枚举
 */
class TC_GAME_API Totem : public Minion
{
    public:
        /**
         * @brief 构造函数 - 初始化图腾对象
         *
         * 创建图腾实例并设置基础的召唤属性。构造函数仅进行基础初始化，
         * 完整的属性设置在 InitStats() 和 InitSummon() 中完成。
         *
         * @param properties 召唤属性信息，来自 DBC 数据，包含召唤类型、标志等
         * @param owner 图腾的所有者，通常是施放图腾的萨满玩家
         *
         * @note 性能说明：构造函数执行快速，不进行数据库查询或复杂计算。
         * @see InitStats 后续的属性初始化方法
         */
        Totem(SummonPropertiesEntry const* properties, Unit* owner);

        /**
         * @brief 虚析构函数
         *
         * 确保派生类对象通过基类指针删除时正确析构。
         * 图腾的生命周期由召唤系统管理，通常不直接调用析构函数。
         */
        virtual ~Totem() { }

        /**
         * @brief 更新图腾状态 - 每帧调用
         *
         * 负责图腾的核心逻辑更新，包括：
         * - 持续时间递减和过期检查
         * - 主动图腾的法术施放决策
         * - AI 更新和目标选择
         *
         * @param time 距离上次更新的时间差（毫秒）
         *
         * @note 调用时机：服务器主循环每帧调用，频率约 50ms（MSTIME_UPDATE）
         * @note 性能说明：此方法是热路径，每秒调用多次，应避免复杂计算。
         *       对于不存在目标或持续时间结束的图腾，应尽早返回。
         *
         * @see Creature::Update 基类更新方法
         */
        void Update(uint32 time) override;

        /**
         * @brief 初始化图腾属性统计
         *
         * 在图腾被召唤时调用，设置图腾的基础属性，包括：
         * - 设置召唤类型标志
         * - 配置持续时间
         * - 设置基础属性（生命值等）
         * - 配置 PvP 标记
         *
         * @param duration 图腾的持续时间（毫秒），0 表示永久存在
         *
         * @note 调用时机：在图腾对象创建后、进入世界前调用。
         * @note 图腾的属性统计计算不同于普通生物，不使用标准的属性公式。
         *
         * @see TempSummon::InitStats 基类初始化方法
         */
        void InitStats(uint32 duration) override;

        /**
         * @brief 初始化召唤 - 设置图腾类型和法术
         *
         * 图腾成功召唤后调用，执行图腾特有的初始化逻辑：
         * - 根据召唤属性确定图腾类型（被动/主动/雕像）
         * - 设置图腾的法术（从召唤者的天赋、装备等获取）
         * - 初始化 AI 和法术施放器
         *
         * @note 调用时机：在 InitStats() 之后、图腾进入世界时调用。
         * @note 关键逻辑：图腾的法术强度等属性继承自主人的当前状态。
         *
         * @see TempSummon::InitSummon 基类初始化方法
         */
        void InitSummon() override;

        /**
         * @brief 解除召唤图腾 - 移除图腾
         *
         * 移除图腾并执行清理工作，包括：
         * - 移除图腾施加的光环效果
         * - 清理主人对图腾的引用
         * - 触发相关的召唤解除事件
         * - 从世界中移除图腾对象
         *
         * @param msTime 延迟移除时间（毫秒），0 表示立即移除
         *
         * @note 使用场景：
         *       - 玩家手动召回图腾
         *       - 图腾持续时间结束
         *       - 图腾被摧毁
         *       - 施放新图腾替换旧图腾（同元素类型）
         *
         * @see TempSummon::UnSummon 基类解除召唤方法
         */
        void UnSummon(uint32 msTime = 0) override;

        /**
         * @brief 获取图腾法术ID
         *
         * 图腾可以拥有多个法术（如被动光环 + 主动技能），通过槽位索引访问。
         * 大多数图腾只有 0 号槽位的法术。
         *
         * @param slot 法术槽位索引，默认为 0（主法术）
         * @return uint32 法术ID，0 表示该槽位无法术
         *
         * @note 性能说明：内联方法，直接数组访问，性能极佳。
         */
        uint32 GetSpell(uint8 slot = 0) const { return m_spells[slot]; }

        /**
         * @brief 获取图腾剩余持续时间
         *
         * 返回图腾的剩余存活时间。当持续时间降为 0 时，图腾将自动消失。
         *
         * @return uint32 剩余持续时间（毫秒）
         *
         * @note 调用时机：用于 UI 显示或判断图腾是否即将过期。
         */
        uint32 GetTotemDuration() const { return m_duration; }

        /**
         * @brief 设置图腾持续时间
         *
         * 修改图腾的剩余持续时间。可用于延长或缩短图腾存活时间。
         *
         * @param duration 新的持续时间（毫秒）
         *
         * @note 使用场景：
         *       - 天赋或装备效果延长图腾持续时间
         *       - 特殊法术减少图腾持续时间
         *       - 调试或脚本需要修改持续时间
         */
        void SetTotemDuration(uint32 duration) { m_duration = duration; }

        /**
         * @brief 获取图腾类型
         *
         * 返回图腾的类型，决定其行为模式：
         * - TOTEM_PASSIVE: 被动图腾，自动施放光环，无需 AI 决策
         * - TOTEM_ACTIVE: 主动图腾，需要 AI 选择目标并施放法术
         * - TOTEM_STATUE: 雕像类型，特殊行为
         *
         * @return TotemType 图腾类型枚举值
         *
         * @see TotemType 图腾类型枚举定义
         */
        TotemType GetTotemType() const { return m_type; }

        /**
         * @brief 更新属性统计 - 覆盖父类方法
         *
         * 图腾不使用标准的属性计算系统（力量、敏捷等），
         * 其属性由召唤时的固定值决定。此方法直接返回 true，
         * 表示"更新成功"但实际不进行任何计算。
         *
         * @param stat 属性类型（如力量、敏捷等）
         * @return bool 始终返回 true
         *
         * @note 设计原因：图腾的生命值、伤害等属性来自法术效果和主人属性加成，
         *       不受自身基础属性影响，因此跳过标准计算流程。
         *
         * @see Unit::UpdateStats 基类方法
         */
        bool UpdateStats(Stats /*stat*/) override { return true; }

        /**
         * @brief 更新所有属性统计 - 覆盖父类方法
         *
         * 同 UpdateStats()，图腾不使用标准属性系统，直接返回 true。
         *
         * @return bool 始终返回 true
         *
         * @see Unit::UpdateAllStats 基类方法
         */
        bool UpdateAllStats() override { return true; }

        /**
         * @brief 更新抗性 - 覆盖父类方法
         *
         * 图腾不使用标准的抗性计算系统。图腾的抗性由法术效果决定，
         * 不受护甲、抗性属性等影响。
         *
         * @param school 魔法学校类型（火焰、冰霜、自然等）
         *
         * @see Unit::UpdateResistances 基类方法
         */
        void UpdateResistances(uint32 /*school*/) override { }

        /**
         * @brief 更新护甲 - 覆盖父类方法
         *
         * 图腾不使用标准的护甲计算系统。图腾的物理减伤由特殊规则决定，
         * 不受敏捷、装备等影响。
         *
         * @see Unit::UpdateArmor 基类方法
         */
        void UpdateArmor() override { }

        /**
         * @brief 更新最大生命值 - 覆盖父类方法
         *
         * 图腾的生命值在 InitStats() 中固定设置，不使用标准的体质加成公式。
         * 图腾的生命值通常是主人生命值的固定百分比。
         *
         * @see Unit::UpdateMaxHealth 基类方法
         */
        void UpdateMaxHealth() override { }

        /**
         * @brief 更新最大能量值 - 覆盖父类方法
         *
         * 图腾不使用能量系统（法力、怒气等），因此跳过能量更新。
         *
         * @param power 能量类型（法力、怒气、能量等）
         *
         * @see Unit::UpdateMaxPower 基类方法
         */
        void UpdateMaxPower(Powers /*power*/) override { }

        /**
         * @brief 更新攻击强度和伤害 - 覆盖父类方法
         *
         * 图腾的伤害由法术效果决定，不使用标准的攻击强度公式。
         * 图腾不进行普通攻击，仅施放法术。
         *
         * @param ranged 是否为远程攻击（true=远程，false=近战）
         *
         * @see Unit::UpdateAttackPowerAndDamage 基类方法
         */
        void UpdateAttackPowerAndDamage(bool /*ranged*/) override { }

        /**
         * @brief 更新物理伤害 - 覆盖父类方法
         *
         * 图腾不进行物理攻击，跳过物理伤害计算。
         *
         * @param attType 武器攻击类型（主手、副手、远程）
         *
         * @see Unit::UpdateDamagePhysical 基类方法
         */
        void UpdateDamagePhysical(WeaponAttackType /*attType*/) override { }

        /**
         * @brief 检查图腾是否免疫某法术效果
         *
         * 图腾对部分负面效果有特殊的免疫规则，以保持其功能性：
         * - 对大多数群体控制效果免疫
         * - 对某些驱散效果免疫
         * - 对特定法术类型有选择性的免疫
         *
         * @param spellInfo 法术信息，包含法术的基本属性
         * @param spellEffectInfo 法术效果信息，包含具体的效果类型和参数
         * @param caster 施法者，可为 nullptr（环境效果）
         * @param requireImmunityPurgesEffectAttribute 是否需要免疫清除效果属性，
         *                                             true 表示严格检查免疫标志
         * @return bool 如果图腾免疫该法术效果返回 true，否则返回 false
         *
         * @note 调用时机：在法术命中判定流程中调用，决定法术是否生效。
         * @note 性能说明：此方法可能被频繁调用（大量法术判定），
         *       应保持逻辑简洁，避免复杂计算。
         *
         * @see Unit::IsImmunedToSpellEffect 基类方法
         */
        bool IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const override;

    protected:
        TotemType m_type;      /**< 图腾类型 - 决定图腾的行为模式（被动/主动/雕像） */
        uint32 m_duration;     /**< 图腾剩余持续时间 - 毫秒为单位，0 表示永久存在 */
};
#endif
