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
 * @file CombatAI.h
 *
 * @brief 战斗AI模块
 *
 * 本文件定义了多个战斗相关的AI类，用于控制生物在战斗中的行为：
 * - AggressorAI: 基础侵略者AI，主动攻击敌对目标
 * - CombatAI: 通用战斗AI，支持法术施放和事件调度
 * - CasterAI: 施法者AI，保持距离进行远程法术攻击
 * - ArcherAI: 弓箭手AI，远程物理攻击，近战时切换为近战
 * - TurretAI: 炮塔AI，固定位置的远程攻击单位
 * - VehicleAI: 载具AI，管理载具的条件检查和解散逻辑
 *
 * 这些AI类为游戏中不同类型的战斗生物提供了标准化的行为模式。
 */

#ifndef TRINITY_COMBATAI_H
#define TRINITY_COMBATAI_H

#include "CreatureAI.h"

class Creature;

/**
 * @brief 侵略者AI类
 *
 * 简单的攻击型AI，生物会主动攻击视野内的敌对目标。
 * 主要用于基础怪物AI实现。
 */
class TC_GAME_API AggressorAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         */
        explicit AggressorAI(Creature* creature) : CreatureAI(creature) { }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 每帧调用，处理攻击目标的查找和攻击行为
         */
        void UpdateAI(uint32) override;

        /**
         * @brief 检查此AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 权限值，值越大表示越适合使用此AI
         */
        static int32 Permissible(Creature const* creature);
};

/**
 * @brief 法术ID向量类型定义
 *
 * 用于存储生物可施放的法术ID列表
 */
typedef std::vector<uint32> SpellVector;

/**
 * @brief 战斗AI类
 *
 * 提供基于法术和事件的战斗AI实现。
 * 支持自动施放预设的法术列表，并通过事件系统管理战斗逻辑。
 * 适用于大多数具有多种技能的生物。
 */
class TC_GAME_API CombatAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         */
        explicit CombatAI(Creature* creature) : CreatureAI(creature) { }

        /**
         * @brief 初始化AI
         *
         * 在AI创建后调用，用于初始化法术列表和事件
         */
        void InitializeAI() override;

        /**
         * @brief 重置AI状态
         *
         * 重置所有战斗相关的状态，清除事件等
         */
        void Reset() override;

        /**
         * @brief 进入战斗时的回调
         * @param who 进入战斗的目标
         *
         * 当生物进入战斗状态时调用
         */
        void JustEngagedWith(Unit* who) override;

        /**
         * @brief 死亡时的回调
         * @param killer 击杀者
         *
         * 当生物死亡时调用
         */
        void JustDied(Unit* killer) override;

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 每帧调用，处理法术施放、移动和攻击等战斗逻辑
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 法术被打断时的回调
         * @param spellId 被打断的法术ID
         * @param unTimeMs 未使用的时间（毫秒）
         *
         * 当生物正在施放的法术被打断时调用
         */
        void SpellInterrupted(uint32 spellId, uint32 unTimeMs) override;

        /**
         * @brief 检查此AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 权限值，默认返回PERMIT_BASE_NO表示不自动选择
         */
        static int Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }

    protected:
        EventMap _events;    ///< 事件映射表，用于管理技能冷却和战斗事件
        SpellVector _spells; ///< 生物可施放的法术ID列表
};

/**
 * @brief 法师AI类
 *
 * 继承自CombatAI，专为施法者设计的AI。
 * 会保持与目标的距离，在合适的距离施放法术。
 * 适用于远程法术攻击型生物。
 */
class TC_GAME_API CasterAI : public CombatAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         *
         * 初始化攻击距离为近战范围
         */
        explicit CasterAI(Creature* creature) : CombatAI(creature) { _attackDistance = MELEE_RANGE; }

        /**
         * @brief 初始化AI
         *
         * 初始化法术列表，设置施法距离
         */
        void InitializeAI() override;

        /**
         * @brief 开始攻击
         * @param victim 攻击目标
         *
         * 使用施法者的攻击开始方式，保持攻击距离
         */
        void AttackStart(Unit* victim) override { AttackStartCaster(victim, _attackDistance); }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理施法逻辑，保持距离并施放法术
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 进入战斗时的回调
         * @param who 进入战斗的目标
         */
        void JustEngagedWith(Unit* /*who*/) override;

    private:
        float _attackDistance; ///< 攻击距离，生物会尝试保持与此目标的距离
};

/**
 * @brief 弓箭手AI结构体
 *
 * 远程物理攻击型AI，会保持与目标的最小距离。
 * 当目标进入最小射程内时会后退以保持攻击距离。
 */
struct TC_GAME_API ArcherAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         */
        explicit ArcherAI(Creature* creature);

        /**
         * @brief 开始攻击
         * @param who 攻击目标
         */
        void AttackStart(Unit* who) override;

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理远程攻击逻辑，保持最小攻击距离
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 检查此AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 权限值，默认返回PERMIT_BASE_NO表示不自动选择
         */
        static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }

    protected:
        float _minimumRange; ///< 最小攻击距离，目标进入此范围内会后退
};

/**
 * @brief 炮塔AI结构体
 *
 * 固定位置的防御型AI，不会移动，只攻击射程内的敌对目标。
 * 适用于固定炮塔、防御塔等不可移动的攻击性物体。
 */
struct TC_GAME_API TurretAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         */
        explicit TurretAI(Creature* creature);

        /**
         * @brief 检查AI是否可以攻击指定目标
         * @param who 目标单位
         * @return 如果可以攻击返回true
         *
         * 检查目标是否在射程内且满足其他攻击条件
         */
        bool CanAIAttack(Unit const* who) const override;

        /**
         * @brief 开始攻击
         * @param who 攻击目标
         */
        void AttackStart(Unit* who) override;

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理攻击逻辑，炮塔不会移动
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 检查此AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 权限值，默认返回PERMIT_BASE_NO表示不自动选择
         */
        static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }

    protected:
        float _minimumRange; ///< 最小攻击距离，目标进入此范围内则无法攻击
};

/// 载具条件检查时间间隔（毫秒）
#define VEHICLE_CONDITION_CHECK_TIME 1000

/// 载具解散时间（毫秒）
#define VEHICLE_DISMISS_TIME 5000

/**
 * @brief 载具AI结构体
 *
 * 专门用于载具（坐骑、战车等）的AI实现。
 * 管理载具的条件检查、解散逻辑以及被控制时的状态。
 */
struct TC_GAME_API VehicleAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 使用此AI的生物对象
         */
        explicit VehicleAI(Creature* creature);

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 定期检查载具条件和解散计时
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 视线内移动检测（空实现）
         * @param 进入视线范围的单位
         *
         * 载具不响应视线检测
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief 开始攻击（空实现）
         * @param 攻击目标
         *
         * 载具不主动攻击
         */
        void AttackStart(Unit*) override { }

        /**
         * @brief 被魅惑/控制时的回调
         * @param isNew 是否是新进入魅惑状态
         *
         * 当载具被玩家控制或解除控制时调用
         */
        void OnCharmed(bool isNew) override;

        /**
         * @brief 检查此AI是否适用于指定生物
         * @param creature 要检查的生物
         * @return 权限值
         */
        static int32 Permissible(Creature const* creature);

    private:
        /**
         * @brief 加载载具条件
         *
         * 从数据库加载载具相关的条件数据
         */
        void LoadConditions();

        /**
         * @brief 检查载具条件
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 定期检查载具是否满足使用条件
         */
        void CheckConditions(uint32 diff);

        bool _hasConditions;      ///< 是否有条件需要检查
        uint32 _conditionsTimer;  ///< 条件检查计时器
        bool _dismiss;            ///< 是否正在解散中
        uint32 _dismissTimer;     ///< 解散计时器
};

#endif
