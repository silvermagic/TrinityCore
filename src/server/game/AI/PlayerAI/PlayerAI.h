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
 * @file PlayerAI.h
 * @brief 玩家AI模块头文件
 *
 * 本模块提供了玩家角色被AI控制时的AI实现。主要应用于：
 * - 玩家被魅惑或精神控制时的AI行为
 * - 某些特殊场景下需要AI控制玩家角色
 * - 提供基于职业和天赋的智能技能选择
 *
 * 核心功能：
 * - 根据玩家职业天赋自动选择合适的法术
 * - 提供远程和近战攻击AI行为
 * - 管理变形状态的取消
 *
 * 设计特点：
 * - 继承自UnitAI，复用基础AI框架
 * - 支持基于权重的法术选择系统
 * - 提供SimpleCharmedPlayerAI作为默认实现
 */

#ifndef TRINITY_PLAYERAI_H
#define TRINITY_PLAYERAI_H

#include "Common.h"
#include "UnitAI.h"

class Creature;
class Player;
class Spell;
class Unit;

/**
 * @class PlayerAI
 * @brief 玩家AI基类 - 提供AI控制玩家角色的基础功能
 *
 * 此类为AI控制玩家角色提供核心功能，包括：
 * - 法术验证和选择机制
 * - 职业专精识别
 * - 远程/近战攻击判断
 *
 * 使用场景：
 * - 玩家被魅惑时控制其行为
 * - 特殊任务或场景中的AI控制
 * - 测试和调试用途
 *
 * 性能考虑：
 * - 法术验证涉及较多检查，应避免频繁调用
 * - 缓存专精和治疗者状态以提升性能
 */
class TC_GAME_API PlayerAI : public UnitAI
{
    public:
        /**
         * @brief 构造函数
         * @param player 被AI控制的玩家对象指针
         *
         * 初始化玩家AI，计算玩家的专精、治疗者状态和远程攻击者状态
         */
        explicit PlayerAI(Player* player);

        /**
         * @brief 获取魅惑者
         * @return Creature* 魅惑该玩家的生物指针，如果不存在则返回nullptr
         *
         * 返回控制该玩家的魅惑者（charmer）
         *
         * 调用时机：
         * - 确定攻击目标时
         * - 需要跟随或保护魅惑者时
         */
        Creature* GetCharmer() const;

        /**
         * @brief 获取玩家专精索引
         * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
         * @return uint8 专精索引，范围0-2（0为最左侧专精，2为最右侧）
         *
         * 根据天赋点分布确定玩家的主要专精。如果两个专精天赋点相同，
         * 返回左侧的专精。返回值对应天赋面板中的位置。
         *
         * 调用时机：
         * - 选择合适的法术时
         * - 确定角色定位（治疗/输出）时
         *
         * 性能考虑：
         * - 计算结果在构造时缓存，多次调用开销很低
         */
        uint8 GetSpec(Player const* who = nullptr) const;

        /**
         * @brief 判断玩家是否为治疗者
         * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
         * @return bool 如果是治疗专精返回true
         *
         * 基于专精判断玩家是否为治疗者角色
         *
         * 调用时机：
         * - 选择法术类型时
         * - 确定战斗角色时
         */
        bool IsHealer(Player const* who = nullptr) const;

        /**
         * @brief 判断玩家是否为远程攻击者
         * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
         * @return bool 如果是远程攻击者返回true
         *
         * 基于职业和专精判断玩家是否应该进行远程攻击
         *
         * 调用时机：
         * - 确定攻击方式时
         * - 选择站位时
         */
        bool IsRangedAttacker(Player const* who = nullptr) const;

    protected:
        /**
         * @struct TargetedSpell
         * @brief 目标法术结构体 - 封装法术对象和目标单位
         *
         * 继承自std::pair<Spell*, Unit*>，提供便捷的法术-目标封装。
         * first为法术对象指针，second为目标单位指针。
         */
        struct TargetedSpell : public std::pair<Spell*, Unit*>
        {
            /**
             * @brief 默认构造函数
             *
             * 创建空的法术-目标对
             */
            TargetedSpell() : pair<Spell*, Unit*>() { }

            /**
             * @brief 参数化构造函数
             * @param first 法术对象指针
             * @param second 目标单位指针
             */
            TargetedSpell(Spell* first, Unit* second) : pair<Spell*, Unit*>(first, second) { }

            /**
             * @brief 布尔转换运算符
             * @return 如果包含有效的法术对象返回true
             *
             * 允许在条件表达式中直接使用TargetedSpell对象
             */
            explicit operator bool() { return !!first; }
        };

        /// 可能释放的法术类型：法术-目标对及其权重
        typedef std::pair<TargetedSpell, uint32> PossibleSpell;

        /// 可能释放的法术列表类型
        typedef std::vector<PossibleSpell> PossibleSpellVector;

        /// 被AI控制的玩家对象指针（const，初始化后不可更改）
        Player* const me;

        /**
         * @brief 设置是否为远程攻击者
         * @param state true表示是远程攻击者
         *
         * 允许子类覆盖默认的远程攻击者检测逻辑
         *
         * 调用时机：
         * - 子类构造函数中
         * - 需要动态改变攻击方式时
         */
        void SetIsRangedAttacker(bool state) { _isSelfRangedAttacker = state; }

        /**
         * @enum SpellTarget
         * @brief 法术目标类型枚举
         *
         * 定义法术目标的预定义类型，简化目标选择逻辑
         */
        enum SpellTarget
        {
            TARGET_NONE,        ///< 无目标
            TARGET_VICTIM,      ///< 当前目标
            TARGET_CHARMER,     ///< 魅惑者
            TARGET_SELF         ///< 自身
        };

        /**
         * @brief 验证法术是否可以施放（指定单位目标）
         * @param spellId 法术ID
         * @param target 目标单位指针
         * @return TargetedSpell 包含法术和目标的pair，如果无法施放则为空
         *
         * 检查指定法术是否可以在指定目标上施放，包括：
         * - 法术是否有效
         * - 目标是否有效
         * - 冷却、资源、距离等限制条件
         *
         * 注意：调用者负责清理返回的Spell对象指针
         *
         * 性能考虑：
         * - 涉及法术信息查询和条件检查，应避免频繁调用
         */
        TargetedSpell VerifySpellCast(uint32 spellId, Unit* target);

        /**
         * @brief 验证法术是否可以施放（指定目标类型）
         * @param spellId 法术ID
         * @param target 目标类型枚举
         * @return TargetedSpell 包含法术和目标的pair，如果无法施放则为空
         *
         * 根据目标类型枚举解析实际目标，然后调用验证逻辑
         *
         * 注意：调用者负责清理返回的Spell对象指针
         */
        TargetedSpell VerifySpellCast(uint32 spellId, SpellTarget target);

        /**
         * @brief 验证并添加法术到候选列表
         * @param spells 法术候选列表（引用）
         * @param spellId 法术ID
         * @param target 目标（可以是Unit*或SpellTarget）
         * @param weight 法术权重（用于后续选择）
         *
         * 模板方法，验证法术后如果有效则添加到列表。
         * 权重用于从多个候选法术中选择时的概率计算。
         *
         * 调用时机：
         * - 构建可释放法术池时
         * - 需要收集多个可选法术时
         */
        template<typename T> inline void VerifyAndPushSpellCast(PossibleSpellVector& spells, uint32 spellId, T target, uint32 weight)
        {
            if (TargetedSpell spell = VerifySpellCast(spellId, target))
                spells.push_back({ spell,weight });
        }

        /**
         * @brief 从候选列表中选择一个法术
         * @param spells 法术候选列表（引用，会被清空）
         * @return TargetedSpell 被选中的法术-目标对
         *
         * 根据权重随机选择一个法术，删除列表中其他所有法术对象。
         * 此方法会使传入的vector失效并清空，防止误用。
         *
         * 调用时机：
         * - 决定释放哪个法术时
         * - 完成法术选择后
         *
         * 性能考虑：
         * - 会清理所有未选中的法术对象，确保无内存泄漏
         */
        TargetedSpell SelectSpellCast(PossibleSpellVector& spells);

        /**
         * @brief 对目标施放法术
         * @param spell 包含法术和目标的pair
         *
         * 辅助方法，执行选定的法术施放
         *
         * 调用时机：
         * - 选定要释放的法术后
         */
        void DoCastAtTarget(TargetedSpell spell);

        /**
         * @brief 选择攻击目标
         * @return Unit* 选中的攻击目标指针
         *
         * 虚函数，子类可重写以提供不同的目标选择逻辑。
         * 基类实现返回魅惑者的攻击目标。
         *
         * 调用时机：
         * - 需要确定攻击目标时
         * - 战斗开始时
         */
        virtual Unit* SelectAttackTarget() const;

        /**
         * @brief 如果准备好则执行远程攻击
         *
         * 检查并执行远程自动攻击（如射击、投掷等）
         *
         * 调用时机：
         * - UpdateAI中，针对远程职业
         */
        void DoRangedAttackIfReady();

        /**
         * @brief 如果准备好则执行自动攻击
         *
         * 检查并执行近战自动攻击
         *
         * 调用时机：
         * - UpdateAI中，针对近战职业
         */
        void DoAutoAttackIfReady();

        /**
         * @brief 取消所有变形状态
         *
         * 取消玩家可以主动取消的所有变形效果。
         * 用于确保玩家处于正常形态以执行某些动作。
         *
         * 调用时机：
         * - 需要玩家回到正常形态时
         * - 某些特殊技能使用前
         */
        void CancelAllShapeshifts();

    private:
        uint8 const _selfSpec;              ///< 玩家专精索引（构造时计算并缓存）
        bool const _isSelfHealer;           ///< 是否为治疗者（构造时计算并缓存）
        bool _isSelfRangedAttacker;         ///< 是否为远程攻击者（可被SetIsRangedAttacker覆盖）
};

/**
 * @class SimpleCharmedPlayerAI
 * @brief 简单魅惑玩家AI - 玩家被魅惑时的默认AI实现
 *
 * 提供玩家被魅惑时的基本AI行为，包括：
 * - 跟随魅惑者
 * - 协助攻击魅惑者的目标
 * - 根据专精选择合适的法术
 * - 处理施法、移动和面向
 *
 * 使用场景：
 * - 玩家被精神控制法术影响时
 * - 某些BOSS战中的特殊机制
 *
 * 设计特点：
 * - 继承自PlayerAI，复用基础玩家AI功能
 * - 实现基于专精的智能法术选择
 * - 处理施法检查定时器
 */
class TC_GAME_API SimpleCharmedPlayerAI : public PlayerAI
{
    public:
        /**
         * @brief 构造函数
         * @param player 被魅惑的玩家指针
         *
         * 初始化魅惑AI的状态：
         * - 施法检查定时器设为2500毫秒
         * - 追踪近战标志为false
         * - 强制面向为true
         * - 跟随状态为false
         */
        SimpleCharmedPlayerAI(Player* player) : PlayerAI(player), _castCheckTimer(2500), _chaseCloser(false), _forceFacing(true), _isFollowing(false) { }

        /**
         * @brief 更新AI状态
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 每个世界更新周期调用，执行AI逻辑：
         * - 检查并更新施法定时器
         * - 选择并施放合适的法术
         * - 处理移动和面向
         * - 执行自动攻击
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 魅惑状态变更处理
         * @param isNew 是否为新的魅惑状态
         *
         * 当玩家的魅惑状态改变时调用，用于初始化或清理AI状态
         */
        void OnCharmed(bool isNew) override;

    protected:
        /**
         * @brief 判断是否可以攻击指定目标
         * @param who 目标单位指针
         * @return bool 如果可以攻击返回true
         *
         * 重写基类方法，提供魅惑状态下的攻击条件检查
         */
        bool CanAIAttack(Unit const* who) const override;

        /**
         * @brief 选择攻击目标
         * @return Unit* 选中的攻击目标
         *
         * 重写基类方法，选择魅惑者的当前目标作为攻击目标
         */
        Unit* SelectAttackTarget() const override;

    private:
        /**
         * @brief 根据专精选择合适的法术
         * @return TargetedSpell 选中的法术-目标对
         *
         * 根据玩家的职业和专精，选择最合适的法术施放。
         * 包括治疗、伤害和辅助法术的智能选择。
         *
         * 调用时机：
         * - 施法检查定时器到期时
         * - 需要决定下一个施放的法术时
         */
        TargetedSpell SelectAppropriateCastForSpec();

        uint32 _castCheckTimer;    ///< 施法检查定时器（毫秒）
        bool _chaseCloser;         ///< 是否追踪近战目标
        bool _forceFacing;         ///< 是否强制面向目标
        bool _isFollowing;         ///< 是否正在跟随魅惑者
};

#endif
