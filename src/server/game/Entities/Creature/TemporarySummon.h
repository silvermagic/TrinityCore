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

#ifndef TRINITYCORE_TEMPSUMMON_H
#define TRINITYCORE_TEMPSUMMON_H

#include "Creature.h"

/**
 * @file TemporarySummon.h
 * @brief 临时召唤生物系统头文件
 *
 * 本文件定义了游戏中所有临时召唤生物的相关类，包括：
 * - TempSummon：临时召唤生物基类，管理生命周期和基础行为
 * - Minion：仆从类，拥有所有者的召唤物
 * - Guardian：守护者类，继承属性的战斗召唤物
 * - Puppet：傀儡类，受召唤者控制的特殊单位
 *
 * 临时召唤生物与普通生物的主要区别：
 * 1. 具有有限的生命周期（可通过定时器或事件触发消失）
 * 2. 与召唤者存在关联关系（可获取召唤者信息）
 * 3. 支持特定的召唤类型和消失机制
 */

/**
 * @brief 宠物条目ID枚举
 *
 * 定义游戏中特殊宠物的 Creature 模板 ID，
 * 用于识别特定的召唤宠物类型。
 */
enum PetEntry : uint32
{
    // Death Knight pets - 死亡骑士宠物
    PET_GHOUL           = 26125,  ///< 食尸鬼（死亡骑士的召唤食尸鬼技能）
    PET_RISEN_ALLY      = 30230,  ///< 复活的盟友（死亡骑士的复活盟友技能）

    // Shaman pet - 萨满宠物
    PET_SPIRIT_WOLF     = 29264   ///< 精灵狼（萨满的野性之魂技能）
};

struct SummonPropertiesEntry;

/**
 * @brief 临时召唤生物基类
 *
 * 所有临时召唤生物的基类，继承自 Creature。
 * 提供临时召唤生物的核心功能：
 * - 生命周期管理（定时消失、条件消失）
 * - 召唤者关联（记录谁召唤了该生物）
 * - 召唤类型控制（不同类型的消失行为）
 *
 * 临时召唤生物通常由法术、技能或脚本创建，
 * 在特定条件下自动消失（时间到期、召唤者死亡等）。
 *
 * 继承关系：
 *   Object -> WorldObject -> Unit -> Creature -> TempSummon
 *
 * @see Creature 基类
 * @see TempSummonType 召唤类型枚举
 */
class TC_GAME_API TempSummon : public Creature
{
    public:
        /**
         * @brief 构造函数
         *
         * 创建一个临时召唤生物实例。
         *
         * @param properties 召唤属性配置（来自 DBC 数据），包含召唤类型、标志等信息
         * @param owner 召唤者（WorldObject 类型，可以是玩家、生物或游戏对象）
         * @param isWorldObject 是否作为 WorldObject 处理（影响可见性和更新范围）
         */
        explicit TempSummon(SummonPropertiesEntry const* properties, WorldObject* owner, bool isWorldObject);

        /**
         * @brief 虚析构函数
         */
        virtual ~TempSummon() { }

        /**
         * @brief 更新临时召唤生物状态
         *
         * 重写 Creature::Update()，在基类更新逻辑前检查生命周期。
         * 如果定时器到期，触发消失流程。
         *
         * @param time 距离上次更新的时间差（毫秒）
         *
         * @note 性能说明：每帧都会调用，应保持高效
         * @note 只有设置了生命周期的召唤物才会检查定时器
         */
        void Update(uint32 time) override;

        /**
         * @brief 初始化召唤生物属性
         *
         * 设置召唤生物的基础属性，包括生命周期定时器。
         * 子类可以重写此方法以添加额外的初始化逻辑。
         *
         * @param lifetime 生命周期时长（毫秒），0 表示永久存在
         *
         * @note 在召唤生物创建后立即调用
         * @note 子类应调用基类实现以确保基础属性正确设置
         */
        virtual void InitStats(uint32 lifetime);

        /**
         * @brief 初始化召唤流程
         *
         * 在属性初始化完成后调用，用于执行召唤时的特殊逻辑。
         * 例如：建立追随行为、设置初始位置等。
         *
         * @note 子类可以重写以添加召唤时的特殊行为
         */
        virtual void InitSummon();

        /**
         * @brief 创建时更新对象可见性
         *
         * 重写基类方法，在召唤生物创建时更新周围玩家的可见性。
         * 确保召唤生物能够立即被附近的玩家看到。
         */
        void UpdateObjectVisibilityOnCreate() override;

        /**
         * @brief 触发召唤生物消失
         *
         * 启动消失流程，可以立即消失或延迟消失。
         * 这是对外的主要消失接口，会清理相关状态并最终移除对象。
         *
         * @param msTime 延迟消失时间（毫秒），0 表示立即消失
         *
         * @note 对于延迟消失，会创建 ForcedUnsummonDelayEvent 事件
         * @note 消失时会触发相关的事件和回调
         */
        virtual void UnSummon(uint32 msTime = 0);

        /**
         * @brief 从世界中移除
         *
         * 重写基类方法，在移除前清理召唤者的引用。
         * 确保召唤者不会再引用已移除的召唤物。
         */
        void RemoveFromWorld() override;

        /**
         * @brief 设置临时召唤类型
         *
         * 配置召唤生物的消失行为类型。
         *
         * @param type 召唤类型（参见 TempSummonType 枚举）
         *             - TEMPSUMMON_TIMED_OR_DEAD_DESPAWN：定时消失或召唤者死亡时消失
         *             - TEMPSUMMON_TIMED_OOC_DESPAWN：脱战后定时消失
         *             - TEMPSUMMON_TIMED_DESPAWN：纯定时消失
         *             - TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT：脱战后定时消失
         *             - TEMPSUMMON_CORPSE_TIMED_DESPAWN：死亡后尸体定时消失
         *             - TEMPSUMMON_CORPSE_DESPAWN：死亡后立即消失
         *             - TEMPSUMMON_DEAD_DESPAWN：死亡后立即消失（无尸体）
         *             - TEMPSUMMON_MANUAL_DESPAWN：仅手动消失
         */
        void SetTempSummonType(TempSummonType type);

        /**
         * @brief 保存到数据库（空实现）
         *
         * 临时召唤生物不应持久化到数据库，
         * 因此重写为空实现。
         *
         * @param mapid 地图ID（忽略）
         * @param spawnMask 生成掩码（忽略）
         * @param phaseMask 相位掩码（忽略）
         */
        void SaveToDB(uint32 /*mapid*/, uint8 /*spawnMask*/, uint32 /*phaseMask*/) override { }

        /**
         * @brief 获取召唤者对象
         *
         * 返回召唤此生物的 WorldObject（可能为 nullptr）。
         *
         * @return 召唤者对象指针，如果召唤者已不存在则返回 nullptr
         *
         * @note 需要在世界中查找对象，性能开销较高
         * @see GetSummonerGUID() 如果只需要 GUID，应使用此方法
         */
        WorldObject* GetSummoner() const;

        /**
         * @brief 获取召唤者（Unit 类型）
         *
         * 便捷方法，尝试将召唤者转换为 Unit 类型。
         *
         * @return 如果召唤者是 Unit（玩家或生物），返回其指针；否则返回 nullptr
         */
        Unit* GetSummonerUnit() const;

        /**
         * @brief 获取召唤者（Creature 类型）
         *
         * 便捷方法，尝试将召唤者转换为 Creature 类型。
         *
         * @return 如果召唤者是 Creature，返回其指针；否则返回 nullptr
         */
        Creature* GetSummonerCreatureBase() const;

        /**
         * @brief 获取召唤者（GameObject 类型）
         *
         * 便捷方法，尝试将召唤者转换为 GameObject 类型。
         *
         * @return 如果召唤者是 GameObject，返回其指针；否则返回 nullptr
         */
        GameObject* GetSummonerGameObject() const;

        /**
         * @brief 获取召唤者 GUID
         *
         * 内联获取召唤者的全局唯一标识符。
         * 性能优于 GetSummoner()，因为不需要查找对象。
         *
         * @return 召唤者的 ObjectGuid
         */
        ObjectGuid GetSummonerGUID() const { return m_summonerGUID; }

        /**
         * @brief 获取召唤类型
         *
         * @return 当前召唤类型
         */
        TempSummonType GetSummonType() const { return m_type; }

        /**
         * @brief 获取剩余时间
         *
         * 返回消失定时器的当前值。
         *
         * @return 剩余时间（毫秒）
         */
        uint32 GetTimer() const { return m_timer; }

        /**
         * @brief 是否可以追随所有者
         *
         * @return true 如果召唤物可以追随召唤者
         */
        bool CanFollowOwner() const { return m_canFollowOwner; }

        /**
         * @brief 设置是否可以追随所有者
         *
         * 控制召唤物是否自动追随召唤者移动。
         *
         * @param can true 允许追随，false 禁止追随
         */
        void SetCanFollowOwner(bool can) { m_canFollowOwner = can; }

        /**
         * @brief 设置仅对召唤者可见
         *
         * 控制召唤物的可见性范围。
         *
         * @param visibleBySummonerOnly true 表示只有召唤者能看到
         */
        void SetVisibleBySummonerOnly(bool visibleBySummonerOnly) { m_visibleBySummonerOnly = visibleBySummonerOnly; }

        /**
         * @brief 是否仅对召唤者可见
         *
         * @return true 如果只有召唤者能看到此召唤物
         */
        bool IsVisibleBySummonerOnly() const { return m_visibleBySummonerOnly; }

        SummonPropertiesEntry const* const m_Properties;  ///< 召唤属性配置（来自 DBC）

        /**
         * @brief 获取调试信息
         *
         * 重写基类方法，添加临时召唤相关的调试信息。
         *
         * @return 包含调试信息的字符串
         */
        std::string GetDebugInfo() const override;

    private:
        TempSummonType m_type;              ///< 召唤类型，决定消失行为
        uint32 m_timer;                     ///< 消失倒计时定时器（毫秒）
        uint32 m_lifetime;                  ///< 总生命周期（毫秒）
        ObjectGuid m_summonerGUID;          ///< 召唤者 GUID
        bool m_canFollowOwner;              ///< 是否可以追随召唤者
        bool m_visibleBySummonerOnly;       ///< 是否仅对召唤者可见
};

/**
 * @brief 仆从类
 *
 * 继承自 TempSummon，表示拥有明确所有者的召唤生物。
 * 与基类的主要区别：
 * - 所有者必须是 Unit 类型（玩家或生物）
 * - 死亡状态处理与所有者相关
 * - 支持追随角度控制
 *
 * 典型应用场景：
 * - 猎人的宠物
 * - 术士的恶魔
 * - 死亡骑士的食尸鬼
 * - 萨满的元素精灵
 *
 * 继承关系：
 *   TempSummon -> Minion
 */
class TC_GAME_API Minion : public TempSummon
{
    public:
        /**
         * @brief 构造函数
         *
         * @param properties 召唤属性配置
         * @param owner 所有者（必须是 Unit 类型）
         * @param isWorldObject 是否作为 WorldObject 处理
         */
        Minion(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject);

        /**
         * @brief 初始化仆从属性
         *
         * 重写基类方法，添加仆从特有的初始化逻辑。
         *
         * @param duration 持续时间（毫秒）
         */
        void InitStats(uint32 duration) override;

        /**
         * @brief 从世界中移除
         *
         * 重写基类方法，在移除前清理所有者的仆从列表引用。
         */
        void RemoveFromWorld() override;

        /**
         * @brief 设置死亡状态
         *
         * 重写基类方法，处理仆从死亡时的特殊逻辑。
         * 例如：通知所有者、触发相关效果等。
         *
         * @param s 新的死亡状态
         */
        void setDeathState(DeathState s) override;

        /**
         * @brief 获取所有者
         *
         * @return 所有者 Unit 指针（永不为空）
         */
        Unit* GetOwner() const { return m_owner; }

        /**
         * @brief 获取追随角度
         *
         * 返回仆从在追随所有者时的相对位置角度。
         *
         * @return 追随角度（弧度）
         */
        float GetFollowAngle() const override { return m_followAngle; }

        /**
         * @brief 设置追随角度
         *
         * 配置仆从追随时的相对位置角度。
         *
         * @param angle 角度（弧度），0 为正后方，PI/2 为右侧
         */
        void SetFollowAngle(float angle) { m_followAngle = angle; }

        // Death Knight pets - 死亡骑士宠物判断方法
        /**
         * @brief 是否为食尸鬼
         *
         * 判断此仆从是否为死亡骑士召唤的食尸鬼。
         * 食尸鬼可能是守护者或宠物类型。
         *
         * @return true 如果是食尸鬼
         */
        bool IsPetGhoul() const { return GetEntry() == PET_GHOUL; }

        /**
         * @brief 是否为复活的盟友
         *
         * 判断此仆从是否为死亡骑士复活的盟友。
         *
         * @return true 如果是复活的盟友
         */
        bool IsRisenAlly() const { return GetEntry() == PET_RISEN_ALLY; }

        // Shaman pet - 萨满宠物判断方法
        /**
         * @brief 是否为精灵狼
         *
         * 判断此仆从是否为萨满野性之魂召唤的精灵狼。
         *
         * @return true 如果是精灵狼
         */
        bool IsSpiritWolf() const { return GetEntry() == PET_SPIRIT_WOLF; }

        /**
         * @brief 是否为守护者宠物
         *
         * 判断此仆从是否属于守护者类型的宠物。
         * 守护者宠物有特殊的属性继承和 UI 显示规则。
         *
         * @return true 如果是守护者宠物
         */
        bool IsGuardianPet() const;

        /**
         * @brief 获取调试信息
         *
         * @return 包含仆从相关调试信息的字符串
         */
        std::string GetDebugInfo() const override;

    protected:
        Unit* const m_owner;    ///< 所有者 Unit（构造时设置，永不为空）
        float m_followAngle;    ///< 追随时的相对角度（弧度）
};

/**
 * @brief 守护者类
 *
 * 继承自 Minion，是具有属性继承机制的战斗召唤物。
 *
 * 守护者的特点：
 * - 从召唤者继承属性（力量、敏捷、耐力等）
 * - 属性值随召唤者属性变化而动态更新
 * - 拥有额外的法术伤害加成机制
 *
 * 典型应用场景：
 * - 萨满的火元素/土元素
 * - 法师的水元素
 * - 死亡骑士的食尸鬼（作为守护者时）
 * - 德鲁伊的树人
 *
 * 继承关系：
 *   TempSummon -> Minion -> Guardian
 */
class TC_GAME_API Guardian : public Minion
{
    public:
        /**
         * @brief 构造函数
         *
         * @param properties 召唤属性配置
         * @param owner 所有者（Unit 类型）
         * @param isWorldObject 是否作为 WorldObject 处理
         */
        Guardian(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject);

        /**
         * @brief 初始化守护者属性
         *
         * 重写基类方法，初始化守护者特有的属性继承机制。
         *
         * @param duration 持续时间（毫秒）
         */
        void InitStats(uint32 duration) override;

        /**
         * @brief 根据等级初始化属性
         *
         * 根据守护者的等级设置基础属性值。
         * 守护者的等级通常与召唤者相同或有特定规则。
         *
         * @param level 目标等级
         * @return true 如果初始化成功
         */
        bool InitStatsForLevel(uint8 level);

        /**
         * @brief 初始化召唤流程
         *
         * 重写基类方法，在召唤完成后触发属性更新。
         * 确保守护者的属性与召唤者同步。
         */
        void InitSummon() override;

        /**
         * @brief 更新指定属性
         *
         * 重写基类方法，更新单个属性值。
         * 守护者会从召唤者继承部分属性值。
         *
         * @param stat 要更新的属性类型
         * @return true 如果属性发生了变化
         */
        bool UpdateStats(Stats stat) override;

        /**
         * @brief 更新所有属性
         *
         * 重写基类方法，批量更新所有属性。
         * 用于初始化或召唤者属性大幅变化时。
         *
         * @return true 如果有任何属性发生变化
         */
        bool UpdateAllStats() override;

        /**
         * @brief 更新抗性
         *
         * 更新指定魔法学派的抗性值。
         * 守护者的抗性通常与召唤者相关。
         *
         * @param school 魔法学派（如火焰、冰霜等）
         */
        void UpdateResistances(uint32 school) override;

        /**
         * @brief 更新护甲值
         *
         * 计算并更新守护者的护甲值。
         * 护甲通常基于敏捷和其他装备加成计算。
         */
        void UpdateArmor() override;

        /**
         * @brief 更新最大生命值
         *
         * 计算并更新守护者的最大生命值。
         * 生命值受耐力影响，守护者从召唤者继承耐力加成。
         */
        void UpdateMaxHealth() override;

        /**
         * @brief 更新最大能量值
         *
         * 更新指定能量类型的最大值（法力、怒气等）。
         *
         * @param power 能量类型
         */
        void UpdateMaxPower(Powers power) override;

        /**
         * @brief 更新攻击强度和伤害
         *
         * 更新守护者的攻击强度（AP），并重新计算武器伤害。
         *
         * @param ranged 是否更新远程攻击强度
         */
        void UpdateAttackPowerAndDamage(bool ranged = false) override;

        /**
         * @brief 更新物理伤害
         *
         * 计算并更新指定攻击类型的物理伤害范围。
         *
         * @param attType 攻击类型（主手、副手、远程）
         */
        void UpdateDamagePhysical(WeaponAttackType attType) override;

        /**
         * @brief 获取额外法术伤害加成
         *
         * @return 额外法术伤害值
         */
        int32 GetBonusDamage() const { return m_bonusSpellDamage; }

        /**
         * @brief 获取从召唤者继承的属性值
         *
         * 返回从召唤者继承的指定属性的加成值。
         *
         * @param stat 属性类型
         * @return 继承的属性值加成
         */
        float GetBonusStatFromOwner(Stats stat) const { return m_statFromOwner[stat]; }

        /**
         * @brief 设置额外法术伤害加成
         *
         * 设置守护者的额外法术伤害，用于技能和效果计算。
         *
         * @param damage 额外伤害值
         */
        void SetBonusDamage(int32 damage);

        /**
         * @brief 获取调试信息
         *
         * @return 包含守护者相关调试信息的字符串
         */
        std::string GetDebugInfo() const override;

    protected:
        int32   m_bonusSpellDamage;             ///< 额外法术伤害加成
        float   m_statFromOwner[MAX_STATS];     ///< 从召唤者继承的各项属性值数组
};

/**
 * @brief 傀儡类
 *
 * 继承自 Minion，是一种受召唤者直接控制的特殊召唤物。
 *
 * 傀儡的特点：
 * - 行为完全受召唤者控制（如通过脚本）
 * - 通常有特殊的 AI 行为模式
 * - 不一定追随召唤者，而是执行特定任务
 *
 * 典型应用场景：
 * - 特殊任务中的控制对象
 * - 脚本控制的事件生物
 *
 * 继承关系：
 *   TempSummon -> Minion -> Puppet
 */
class TC_GAME_API Puppet : public Minion
{
    public:
        /**
         * @brief 构造函数
         *
         * @param properties 召唤属性配置
         * @param owner 所有者（Unit 类型）
         */
        Puppet(SummonPropertiesEntry const* properties, Unit* owner);

        /**
         * @brief 初始化傀儡属性
         *
         * 重写基类方法，设置傀儡特有的属性。
         *
         * @param duration 持续时间（毫秒）
         */
        void InitStats(uint32 duration) override;

        /**
         * @brief 初始化召唤流程
         *
         * 重写基类方法，执行傀儡特有的初始化逻辑。
         */
        void InitSummon() override;

        /**
         * @brief 更新傀儡状态
         *
         * 重写基类方法，添加傀儡特有的更新逻辑。
         *
         * @param time 距离上次更新的时间差（毫秒）
         */
        void Update(uint32 time) override;
};

/**
 * @brief 强制延迟消失事件
 *
 * 继承自 BasicEvent，用于处理延迟消失逻辑。
 * 当召唤生物需要延迟消失时（如播放消失动画），
 * 使用此事件在指定时间后触发消失。
 *
 * 工作流程：
 * 1. TempSummon::UnSummon(msTime) 被调用，msTime > 0
 * 2. 创建 ForcedUnsummonDelayEvent 并添加到事件队列
 * 3. 定时器到期后 Execute() 被调用
 * 4. 执行实际的消失逻辑
 *
 * @see TempSummon::UnSummon() 触发此事件的方法
 */
class TC_GAME_API ForcedUnsummonDelayEvent : public BasicEvent
{
public:
    /**
     * @brief 构造函数
     *
     * @param owner 关联的临时召唤生物引用
     */
    ForcedUnsummonDelayEvent(TempSummon& owner) : BasicEvent(), m_owner(owner) { }

    /**
     * @brief 执行事件
     *
     * 当延迟时间到达时，触发召唤生物的立即消失。
     *
     * @param e_time 事件执行时间（时间戳）
     * @param p_time 距离上次更新的时间差
     * @return true 如果事件已处理完成
     */
    bool Execute(uint64 e_time, uint32 p_time) override;

private:
    TempSummon& m_owner;  ///< 关联的临时召唤生物引用
};

#endif // TRINITYCORE_TEMPSUMMON_H
