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
 * @file UnitAI.h
 * @brief 单位AI模块头文件
 *
 * 本文件定义了单位AI的基础类，是所有AI类的基类。
 * UnitAI 提供了AI系统的核心接口和通用功能，包括：
 * - 攻击管理
 * - 目标选择
 * - 法术施放
 * - 事件响应
 *
 * 这是TrinityCore AI系统的基础架构，所有具体的AI实现（CreatureAI、GameObjectAI等）
 * 都直接或间接继承自此类。
 */

#ifndef TRINITY_UNITAI_H
#define TRINITY_UNITAI_H

#include "Errors.h"
#include "EventMap.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "SpellDefines.h"

/**
 * @brief AI类型转换宏
 *
 * 用于将AI指针安全地转换为特定类型
 * 使用 dynamic_cast，如果类型不匹配返回 nullptr
 *
 * @param a 目标AI类型
 * @param b 要转换的AI指针
 * @return 转换后的指针，失败时返回 nullptr
 */
#define CAST_AI(a, b)   (dynamic_cast<a*>(b))

/**
 * @brief AI类型安全转换宏
 *
 * 用于将AI指针转换为特定类型，如果失败则触发断言
 * 适用于必须成功的类型转换场景
 *
 * @param a 目标AI类型
 * @param b 要转换的AI指针
 * @return 转换后的指针，失败时断言
 */
#define ENSURE_AI(a,b)  (EnsureAI<a>(b))

/**
 * @brief AI指针安全转换模板函数
 *
 * 内部实现函数，用于确保AI类型转换
 *
 * @tparam T 目标AI类型
 * @tparam U 源AI类型
 * @param ai 要转换的AI指针
 * @return 转换后的指针
 * @note 如果转换失败会触发断言
 */
template<class T, class U>
T* EnsureAI(U* ai)
{
    T* cast_ai = dynamic_cast<T*>(ai);
    ASSERT(cast_ai);
    return cast_ai;
}

// 前向声明
class Player;
class Quest;
class SpellInfo;
class Unit;
struct AISpellInfoType;
enum DamageEffectType : uint8;
enum MovementGeneratorType : uint8;
enum SpellEffIndex : uint8;

/**
 * @brief 目标选择方法枚举
 *
 * 定义了在威胁列表中选择目标的不同方法
 * 用于 SelectTarget 和 SelectTargetList 函数
 */
enum class SelectTargetMethod
{
    Random,      ///< 随机选择一个目标
    MaxThreat,   ///< 优先选择威胁值最高的目标（按威胁列表顺序）
    MinThreat,   ///< 优先选择威胁值最低的目标
    MaxDistance, ///< 优先选择距离最远的目标
    MinDistance  ///< 优先选择距离最近的目标
};

/**
 * @brief 默认目标选择器结构体
 *
 * 默认的目标选择谓词函数对象，用于根据距离、玩家类型和光环条件筛选目标。
 * 这是一个通用的目标筛选器，可以用于各种AI目标选择场景。
 */
struct TC_GAME_API DefaultTargetSelector
{
    public:
        /**
         * @brief 构造函数
         *
         * @param unit 参考单位（通常是AI控制的单位）
         * @param dist 距离条件：
         *             - 0: 忽略距离
         *             - > 0: 最大距离（目标必须在指定距离内）
         *             - < 0: 最小距离（目标必须在指定距离外）
         * @param playerOnly 是否只选择玩家
         * @param withMainTank 是否允许选择当前坦克
         * @param aura 光环条件：
         *             - 0: 忽略光环
         *             - > 0: 目标必须具有该光环
         *             - < 0: 目标必须不具有该光环
         */
        DefaultTargetSelector(Unit const* unit, float dist, bool playerOnly, bool withMainTank, int32 aura);

        /**
         * @brief 目标筛选操作符
         *
         * @param target 要检查的目标
         * @return true 如果目标满足所有条件
         * @return false 如果目标不满足条件
         */
        bool operator()(Unit const* target) const;

    private:
        Unit const* _me;          ///< 参考单位
        float _dist;              ///< 距离条件
        bool _playerOnly;         ///< 是否只选择玩家
        Unit const* _exception;   ///< 排除的单位（通常是当前坦克）
        int32 _aura;              ///< 光环ID条件
};

/**
 * @brief 法术目标选择器结构体
 *
 * 用于检查目标是否可以作为特定法术的有效目标。
 * 包含范围检查、目标类型检查和法术属性检查。
 */
struct TC_GAME_API SpellTargetSelector
{
    public:
        /**
         * @brief 构造函数
         *
         * @param caster 施法者
         * @param spellId 法术ID
         */
        SpellTargetSelector(Unit* caster, uint32 spellId);

        /**
         * @brief 目标筛选操作符
         *
         * @param target 要检查的目标
         * @return true 如果目标是有效的法术目标
         * @return false 如果目标无效
         */
        bool operator()(Unit const* target) const;

    private:
        Unit const* _caster;           ///< 施法者
        SpellInfo const* _spellInfo;   ///< 法术信息
};

/**
 * @brief 非坦克目标选择器结构体
 *
 * 非常简单的目标选择器，只排除当前坦克目标。
 * 主要用于需要攻击非坦克目标的场景（如Boss技能）。
 *
 * @note 当传递给 UnitAI::SelectTarget 时，position 参数应使用 0
 *       因为坦克不会在临时列表中
 */
struct TC_GAME_API NonTankTargetSelector
{
    public:
        /**
         * @brief 构造函数
         *
         * @param source 源单位（通常是Boss）
         * @param playerOnly 是否只选择玩家，默认为 true
         */
        NonTankTargetSelector(Unit* source, bool playerOnly = true) : _source(source), _playerOnly(playerOnly) { }

        /**
         * @brief 目标筛选操作符
         *
         * @param target 要检查的目标
         * @return true 如果目标不是当前坦克
         * @return false 如果目标是当前坦克
         */
        bool operator()(Unit const* target) const;

    private:
        Unit* _source;       ///< 源单位
        bool _playerOnly;    ///< 是否只选择玩家
};

/**
 * @brief 能量使用者目标选择器结构体
 *
 * 用于选择使用特定能量类型的目标（如法力值用户）。
 * 主要用于针对特定职业或能量类型的技能。
 */
struct TC_GAME_API PowerUsersSelector
{
    public:
        /**
         * @brief 构造函数
         *
         * @param unit 参考单位
         * @param power 能量类型（如 POWER_MANA）
         * @param dist 距离条件
         * @param playerOnly 是否只选择玩家
         */
        PowerUsersSelector(Unit const* unit, Powers power, float dist, bool playerOnly) : _me(unit), _power(power), _dist(dist), _playerOnly(playerOnly) { }

        /**
         * @brief 目标筛选操作符
         *
         * @param target 要检查的目标
         * @return true 如果目标使用指定的能量类型
         * @return false 如果目标不使用该能量类型
         */
        bool operator()(Unit const* target) const;

    private:
        Unit const* _me;          ///< 参考单位
        Powers const _power;      ///< 能量类型
        float const _dist;        ///< 距离条件
        bool const _playerOnly;   ///< 是否只选择玩家
};

/**
 * @brief 最远目标选择器结构体
 *
 * 用于选择距离最远的目标。
 * 可以配置是否要求视线检查。
 */
struct TC_GAME_API FarthestTargetSelector
{
    public:
        /**
         * @brief 构造函数
         *
         * @param unit 参考单位
         * @param dist 最大距离
         * @param playerOnly 是否只选择玩家
         * @param inLos 是否要求视线检查
         */
        FarthestTargetSelector(Unit const* unit, float dist, bool playerOnly, bool inLos) : _me(unit), _dist(dist), _playerOnly(playerOnly), _inLos(inLos) {}

        /**
         * @brief 目标筛选操作符
         *
         * @param target 要检查的目标
         * @return true 如果目标满足距离和视线条件
         * @return false 如果目标不满足条件
         */
        bool operator()(Unit const* target) const;

    private:
        Unit const* _me;       ///< 参考单位
        float _dist;           ///< 最大距离
        bool _playerOnly;      ///< 是否只选择玩家
        bool _inLos;           ///< 是否要求视线检查
};

/**
 * @brief 单位AI基类
 *
 * 这是所有AI类的基类，提供了AI系统的核心功能接口。
 * 包含攻击管理、目标选择、法术施放、事件响应等基础功能。
 *
 * 主要职责：
 * - 定义AI的基本行为接口
 * - 提供通用的目标选择机制
 * - 管理法术施放逻辑
 * - 处理战斗状态变化
 *
 * 继承关系：
 * - UnitAI 是基类
 * - CreatureAI 继承自 UnitAI，添加生物特有功能
 * - 各种具体AI（如 PassiveAI、ReactorAI 等）继承自 CreatureAI
 *
 * 使用场景：
 * - 所有需要AI行为的单位都应该有相应的AI实例
 * - 通过 Unit::GetAI() 获取AI实例
 * - 通过 Unit::SetAI() 设置AI实例
 */
class TC_GAME_API UnitAI
{
    protected:
        Unit* const me;  ///< AI控制的单位指针，const表示指针本身不可修改

    public:
        /**
         * @brief 构造函数
         *
         * @param unit AI控制的单位
         */
        explicit UnitAI(Unit* unit) : me(unit) { }

        /**
         * @brief 虚析构函数
         */
        virtual ~UnitAI() { }

        /**
         * @brief 检查AI是否可以攻击指定目标
         *
         * @param target 要检查的目标
         * @return true 如果可以攻击
         * @return false 如果不能攻击
         *
         * @note 子类可以重写此函数以实现自定义的攻击条件
         */
        virtual bool CanAIAttack(Unit const* /*target*/) const { return true; }

        /**
         * @brief 开始攻击目标
         *
         * 当AI控制的单位需要开始攻击指定目标时调用。
         * 默认实现会设置攻击目标并开始追击。
         *
         * @param target 要攻击的目标
         *
         * 主要流程：
         * - 验证目标有效性
         * - 开始近战攻击
         * - 清除分心状态
         * - 开始追击移动
         */
        virtual void AttackStart(Unit* /*target*/);

        /**
         * @brief 更新AI逻辑
         *
         * 每个游戏周期调用的主更新函数。
         * 这是AI的核心函数，所有AI逻辑都在这里执行。
         *
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * @note 纯虚函数，所有派生类必须实现此函数
         */
        virtual void UpdateAI(uint32 diff) = 0;

        /**
         * @brief 初始化AI
         *
         * 当AI被创建或重置时调用。
         * 默认实现会在单位未死亡时调用 Reset()。
         */
        virtual void InitializeAI();

        /**
         * @brief 重置AI状态
         *
         * 重置AI到初始状态。通常在脱离战斗或初始化时调用。
         *
         * @note 子类应该重写此函数以实现自定义的重置逻辑
         */
        virtual void Reset() { }

        /**
         * @brief 被附身状态变化通知
         *
         * 当单位的附身状态发生变化时调用。
         *
         * @param isNew true 表示新附身状态，false 表示附身结束
         *
         * 实现说明：
         * - 如果 isNew 为 false，应该调用 me->ScheduleAIChange() 来请求AI更换
         * - 如果调用了 ScheduleAIChange()，AI会在下一个时钟周期被替换
         * - 替换时会再次调用此函数，参数 isNew 为 true
         */
        virtual void OnCharmed(bool isNew);

        /**
         * @brief 执行动作
         *
         * 用于在AI之间传递参数或触发特定动作。
         *
         * @param param 动作参数，含义由具体实现定义
         */
        virtual void DoAction(int32 /*param*/) { }

        /**
         * @brief 获取数据
         *
         * 用于从AI获取数据。
         *
         * @param id 数据标识
         * @return 数据值
         */
        virtual uint32 GetData(uint32 /*id = 0*/) const { return 0; }

        /**
         * @brief 设置数据
         *
         * 用于向AI传递数据。
         *
         * @param id 数据标识
         * @param value 数据值
         */
        virtual void SetData(uint32 /*id*/, uint32 /*value*/) { }

        /**
         * @brief 设置GUID
         *
         * 用于向AI传递GUID数据。
         *
         * @param guid GUID值
         * @param id 标识符，默认为 0
         */
        virtual void SetGUID(ObjectGuid const& /*guid*/, int32 /*id*/ = 0) { }

        /**
         * @brief 获取GUID
         *
         * 用于从AI获取GUID数据。
         *
         * @param id 标识符，默认为 0
         * @return GUID值，默认返回空GUID
         */
        virtual ObjectGuid GetGUID(int32 /*id*/ = 0) const { return ObjectGuid::Empty; }

        /**
         * @brief 选择目标（简化版本）
         *
         * 从威胁列表中选择满足条件的目标。
         *
         * @param targetType 选择方法
         * @param position 跳过前 N 个目标（默认为 0）
         * @param dist 距离条件（默认为 0，忽略距离）
         * @param playerOnly 是否只选择玩家（默认为 false）
         * @param withTank 是否包含坦克（默认为 true）
         * @param aura 光环条件（默认为 0，忽略光环）
         * @return 选中的目标，如果没有满足条件的目标返回 nullptr
         */
        Unit* SelectTarget(SelectTargetMethod targetType, uint32 offset = 0, float dist = 0.0f, bool playerOnly = false, bool withTank = true, int32 aura = 0);

        /**
         * @brief 选择目标（自定义谓词版本）
         *
         * 从威胁列表中使用自定义谓词选择目标。
         *
         * @tparam PREDICATE 谓词类型
         * @param targetType 选择方法
         * @param offset 跳过前 N 个目标
         * @param predicate 筛选谓词
         * @return 选中的目标，如果没有满足条件的目标返回 nullptr
         */
        template<class PREDICATE>
        Unit* SelectTarget(SelectTargetMethod targetType, uint32 offset, PREDICATE const& predicate)
        {
            std::list<Unit*> targetList;
            SelectTargetList(targetList, std::numeric_limits<uint32>::max(), targetType, offset, predicate);

            return FinalizeTargetSelection(targetList, targetType);
        }

        /**
         * @brief 选择目标列表（简化版本）
         *
         * 从威胁列表中选择满足条件的多个目标。
         *
         * @param targetList 输出的目标列表（会先清空）
         * @param num 最大目标数量
         * @param targetType 选择方法
         * @param offset 跳过前 N 个目标（默认为 0）
         * @param dist 距离条件（默认为 0，忽略距离）
         * @param playerOnly 是否只选择玩家（默认为 false）
         * @param withTank 是否包含坦克（默认为 true）
         * @param aura 光环条件（默认为 0，忽略光环）
         */
        void SelectTargetList(std::list<Unit*>& targetList, uint32 num, SelectTargetMethod targetType, uint32 offset = 0, float dist = 0.0f, bool playerOnly = false, bool withTank = true, int32 aura = 0);

        /**
         * @brief 选择目标列表（自定义谓词版本）
         *
         * 从威胁列表中使用自定义谓词选择多个目标。
         *
         * @tparam PREDICATE 谓词类型
         * @param targetList 输出的目标列表（会先清空）
         * @param num 最大目标数量
         * @param targetType 选择方法
         * @param offset 跳过前 N 个目标
         * @param predicate 筛选谓词
         */
        template <class PREDICATE>
        void SelectTargetList(std::list<Unit*>& targetList, uint32 num, SelectTargetMethod targetType, uint32 offset, PREDICATE const& predicate)
        {
            if (!PrepareTargetListSelection(targetList, targetType, offset))
                return;

            // 使用谓词过滤目标列表
            targetList.remove_if([&predicate](Unit* target) { return !predicate(target); });

            FinalizeTargetListSelection(targetList, num, targetType);
        }

        /**
         * @brief 进入战斗通知
         *
         * 当单位进入战斗状态时调用。
         *
         * @param who 导致进入战斗的单位
         *
         * @note 注意：生物的战斗逻辑应该放在 JustEngagedWith 中，
         *       而不是这里。JustEngagedWith 在威胁建立后触发。
         */
        virtual void JustEnteredCombat(Unit* /*who*/) { }

        /**
         * @brief 退出战斗通知
         *
         * 当单位退出战斗状态时调用。
         */
        virtual void JustExitedCombat() { }

        /**
         * @brief 消失通知
         *
         * 当单位即将从世界中移除时调用。
         * 包括：消失、网格卸载、尸体消失、玩家登出等。
         */
        virtual void OnDespawn() { }

        /**
         * @brief 造成伤害通知
         *
         * 当单位对任何受害者造成伤害时调用（伤害应用前）。
         *
         * @param victim 受害者
         * @param damage 伤害值（可修改）
         * @param damageType 伤害类型
         */
        virtual void DamageDealt(Unit* /*victim*/, uint32& /*damage*/, DamageEffectType /*damageType*/) { }

        /**
         * @brief 受到伤害通知
         *
         * 当单位受到任何攻击者的伤害时调用（伤害应用前）。
         * 可用于重新计算伤害或对伤害做出特殊反应。
         *
         * @param attacker 攻击者
         * @param damage 伤害值（可修改）
         * @param damageType 伤害类型
         * @param spellInfo 法术信息，如果不是法术伤害则为 nullptr
         */
        virtual void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) { }

        /**
         * @brief 接受治疗通知
         *
         * 当生物接受治疗时调用。
         *
         * @param done_by 治疗来源
         * @param addhealth 治疗量（可修改）
         */
        virtual void HealReceived(Unit* /*done_by*/, uint32& /*addhealth*/) { }

        /**
         * @brief 造成治疗通知
         *
         * 当单位治疗其他单位时调用。
         *
         * @param done_to 治疗目标
         * @param addhealth 治疗量（可修改）
         */
        virtual void HealDone(Unit* /*done_to*/, uint32& /*addhealth*/) { }

        /**
         * @brief 法术被打断通知
         *
         * 当法术被 Spell::EffectInterruptCast 打断时调用。
         * 用于重新安排下一次计划施放的法术。
         *
         * @param spellId 被打断的法术ID
         * @param unTimeMs 未使用的时间（毫秒）
         */
        virtual void SpellInterrupted(uint32 /*spellId*/, uint32 /*unTimeMs*/) { }

        /**
         * @brief 施法者开始攻击
         *
         * 用于施法者类型的AI开始攻击目标。
         * 会保持指定的距离进行施法。
         *
         * @param victim 攻击目标
         * @param dist 保持的距离
         */
        void AttackStartCaster(Unit* victim, float dist);

        /**
         * @brief 施放法术（自动选择目标）
         *
         * 根据法术的AI信息自动选择目标并施放法术。
         *
         * @param spellId 法术ID
         * @return 施法结果
         */
        SpellCastResult DoCast(uint32 spellId);

        /**
         * @brief 施放法术（指定目标）
         *
         * 对指定目标施放法术。
         *
         * @param victim 目标单位
         * @param spellId 法术ID
         * @param args 额外参数
         * @return 施法结果
         */
        SpellCastResult DoCast(Unit* victim, uint32 spellId, CastSpellExtraArgs const& args = {});

        /**
         * @brief 对自己施放法术
         *
         * @param spellId 法术ID
         * @param args 额外参数
         * @return 施法结果
         */
        SpellCastResult DoCastSelf(uint32 spellId, CastSpellExtraArgs const& args = {}) { return DoCast(me, spellId, args); }

        /**
         * @brief 对当前目标施放法术
         *
         * @param spellId 法术ID
         * @param args 额外参数
         * @return 施法结果
         */
        SpellCastResult DoCastVictim(uint32 spellId, CastSpellExtraArgs const& args = {});

        /**
         * @brief 施放范围法术
         *
         * @param spellId 法术ID
         * @param args 额外参数
         * @return 施法结果
         */
        SpellCastResult DoCastAOE(uint32 spellId, CastSpellExtraArgs const& args = {}) { return DoCast(nullptr, spellId, args); }

        /**
         * @brief 获取法术最大射程
         *
         * @param spellId 法术ID
         * @param positive 是否为正面效果射程，默认为 false
         * @return 法术最大射程
         */
        float DoGetSpellMaxRange(uint32 spellId, bool positive = false);

        /**
         * @brief 是否应该与目标切磋
         *
         * @param target 目标
         * @return true 如果应该切磋
         */
        virtual bool ShouldSparWith(Unit const* /*target*/) const { return false; }

        /**
         * @brief 如果就绪则执行近战攻击
         *
         * 检查攻击冷却时间，如果就绪则执行近战攻击。
         * 包括主手和副手攻击。
         */
        void DoMeleeAttackIfReady();

        /**
         * @brief 如果就绪则施放法术攻击
         *
         * 检查攻击冷却时间，如果就绪则对当前目标施放指定法术。
         *
         * @param spell 法术ID
         * @return true 如果施法成功或正在施法中
         * @return false 如果目标不在范围内
         */
        bool DoSpellAttackIfReady(uint32 spell);

        /// AI法术信息数组，存储所有法术的AI使用信息
        static AISpellInfoType* AISpellInfo;

        /**
         * @brief 填充AI法术信息
         *
         * 静态函数，初始化所有法术的AI信息。
         * 在服务器启动时调用一次。
         */
        static void FillAISpellInfo();

        /**
         * @brief 游戏事件通知
         *
         * 当游戏事件开始或结束时调用。
         *
         * @param start true 表示事件开始，false 表示事件结束
         * @param eventId 事件ID
         */
        virtual void OnGameEvent(bool /*start*/, uint16 /*eventId*/) { }

        /**
         * @brief 获取调试信息
         *
         * 返回AI的调试信息字符串。
         *
         * @return 调试信息字符串
         */
        virtual std::string GetDebugInfo() const;

    private:
        /// 禁用拷贝构造
        UnitAI(UnitAI const& right) = delete;
        /// 禁用赋值操作
        UnitAI& operator=(UnitAI const& right) = delete;

        /**
         * @brief 完成目标选择
         *
         * 从目标列表中根据选择方法返回最终目标。
         *
         * @param targetList 目标列表
         * @param targetType 选择方法
         * @return 选中的目标，如果没有目标返回 nullptr
         */
        Unit* FinalizeTargetSelection(std::list<Unit*>& targetList, SelectTargetMethod targetType);

        /**
         * @brief 准备目标列表选择
         *
         * 从威胁管理器中准备目标列表。
         *
         * @param targetList 输出的目标列表
         * @param targetType 选择方法
         * @param offset 跳过的目标数量
         * @return true 如果有目标可供选择
         * @return false 如果没有目标
         */
        bool PrepareTargetListSelection(std::list<Unit*>& targetList, SelectTargetMethod targetType, uint32 offset);

        /**
         * @brief 完成目标列表选择
         *
         * 根据数量限制和选择方法调整目标列表大小。
         *
         * @param targetList 目标列表
         * @param num 最大目标数量
         * @param targetType 选择方法
         */
        void FinalizeTargetListSelection(std::list<Unit*>& targetList, uint32 num, SelectTargetMethod targetType);
};

#endif
