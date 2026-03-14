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
 * @file PassiveAI.h
 * @brief 被动AI模块头文件
 *
 * 本文件定义了多种被动型AI类，用于控制不需要主动行为的生物单位。
 * 这些AI类型适用于不同的被动行为场景：
 * - PassiveAI: 基础被动AI，不进行任何主动行为
 * - PossessedAI: 附身AI，用于被玩家控制的生物
 * - NullCreatureAI: 空AI，用于触发器等不需要AI行为的单位
 * - CritterAI: 小动物AI，遇到威胁时逃跑
 * - TriggerAI: 触发器AI，召唤时自动施放法术
 */

#ifndef TRINITY_PASSIVEAI_H
#define TRINITY_PASSIVEAI_H

#include "CreatureAI.h"

/**
 * @brief 被动AI类
 *
 * 最基础的被动AI实现，生物不会主动攻击或响应任何事件。
 * 主要用于需要完全被动行为的生物，如某些特定的NPC或装饰性生物。
 *
 * 特点：
 * - 不会主动进入战斗
 * - 不会响应视线范围内的敌人
 * - 不会发起攻击
 * - 会在脱离战斗状态时自动重置
 *
 * 使用场景：
 * - 任务NPC
 * - 商人NPC
 * - 训练师NPC
 * - 其他需要完全被动行为的生物
 */
class TC_GAME_API PassiveAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化被动AI实例，将生物的反应状态设置为被动模式
         *
         * @param creature 拥有此AI的生物对象
         *
         * @note 构造时会自动调用 SetReactState(REACT_PASSIVE)，
         *       确保生物不会主动攻击敌人
         */
        explicit PassiveAI(Creature* creature);

        /**
         * @brief 视线范围内移动响应函数
         *
         * 当敌对单位进入生物视线范围时调用。
         * 被动AI不对此事件做任何响应。
         *
         * @param who 进入视线范围的单位（未使用）
         *
         * @note 空实现，被动AI不会因视线范围内的敌人而产生任何行为
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief 开始攻击响应函数
         *
         * 当需要开始攻击目标时调用。
         * 被动AI不对此事件做任何响应。
         *
         * @param target 要攻击的目标（未使用）
         *
         * @note 空实现，被动AI不会发起任何攻击行为
         */
        void AttackStart(Unit*) override { }

        /**
         * @brief AI更新函数
         *
         * 每个游戏周期调用的主更新函数。
         * 检查生物是否处于异常的战斗状态，如果是则强制脱离战斗。
         *
         * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
         *
         * 主要逻辑：
         * - 检查生物是否处于"已参与战斗但不在战斗中"的异常状态
         * - 如果存在异常状态，触发脱离战斗模式
         *
         * @note 这种异常状态可能发生在某些特殊情况下，
         *       如生物被强制设置战斗标志但实际没有敌人
         */
        void UpdateAI(uint32) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用被动AI。
         * 默认返回 PERMIT_BASE_NO，表示不主动选择此AI。
         *
         * @param creature 要检查的生物对象（未使用）
         * @return 始终返回 PERMIT_BASE_NO，表示不适用
         *
         * @note 被动AI通常需要显式指定，不会自动选择
         */
        static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }
};

/**
 * @brief 附身AI类
 *
 * 用于被玩家通过精神控制、附身术等效果控制的生物。
 * 此AI允许玩家完全控制生物的攻击行为。
 *
 * 特点：
 * - 不会主动选择目标，由控制者决定
 * - 可以执行近战攻击
 * - 死亡时不可被掠夺
 * - 不会因为正常规则脱离战斗
 *
 * 使用场景：
 * - 精神控制效果（如牧师的精神控制）
 * - 亡灵奴隶（如术士的奴役恶魔）
 * - 临时被玩家控制的生物
 *
 * @note 此AI不会触发正常的脱战逻辑，确保玩家可以持续控制
 */
class TC_GAME_API PossessedAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化附身AI实例，将生物的反应状态设置为被动模式
         *
         * @param creature 拥有此AI的生物对象
         *
         * @note 构造时会自动调用 SetReactState(REACT_PASSIVE)，
         *       但AI本身可以执行攻击命令（由控制者发出）
         */
        explicit PossessedAI(Creature* creature);

        /**
         * @brief 视线范围内移动响应函数
         *
         * 附身AI不主动响应视线范围内的敌人。
         *
         * @param who 进入视线范围的单位（未使用）
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief 开始攻击函数
         *
         * 当控制者命令生物攻击目标时调用。
         * 立即开始对目标进行近战攻击。
         *
         * @param target 要攻击的目标单位
         *
         * 主要逻辑：
         * - 调用生物的 Attack 方法，参数 true 表示近战攻击
         * - 不进行任何额外的威胁管理或移动控制
         */
        void AttackStart(Unit* target) override;

        /**
         * @brief 进入战斗通知函数
         *
         * 当生物进入战斗状态时调用。
         * 启动战斗管理系统。
         *
         * @param who 导致进入战斗的单位
         */
        void JustEnteredCombat(Unit* who) override { EngagementStart(who); }

        /**
         * @brief 退出战斗通知函数
         *
         * 当生物退出战斗状态时调用。
         * 结束战斗管理系统。
         */
        void JustExitedCombat() override { EngagementOver(); }

        /**
         * @brief 威胁开始通知函数
         *
         * 当某个单位开始对此生物产生威胁时调用。
         * 附身AI忽略此事件。
         *
         * @param who 开始产生威胁的单位（未使用）
         */
        void JustStartedThreateningMe(Unit*) override { }

        /**
         * @brief AI更新函数
         *
         * 每个游戏周期调用的主更新函数。
         * 处理附身生物的攻击逻辑。
         *
         * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
         *
         * 主要逻辑：
         * - 检查当前目标是否有效
         * - 如果目标无效（死亡、友方、不可见等），停止攻击
         * - 如果目标有效且在近战范围内，执行近战攻击
         */
        void UpdateAI(uint32) override;

        /**
         * @brief 脱离战斗模式函数
         *
         * 当生物需要脱离战斗时调用。
         * 附身AI忽略此事件，不会自动脱战。
         *
         * @param why 脱战原因（未使用）
         *
         * @note 空实现，确保附身生物不会自动脱战
         */
        void EnterEvadeMode(EvadeReason /*why*/) override { }

        /**
         * @brief 死亡通知函数
         *
         * 当生物死亡时调用。
         * 移除可掠夺标志，防止玩家从附身生物尸体上获得战利品。
         *
         * @param killer 击杀者（未使用）
         *
         * @note 这是附身生物的重要特性：
         *       玩家控制的生物死亡后不会掉落战利品，
         *       防止滥用精神控制获取额外物品
         */
        void JustDied(Unit*) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用附身AI。
         * 默认返回 PERMIT_BASE_NO，表示不主动选择此AI。
         *
         * @param creature 要检查的生物对象（未使用）
         * @return 始终返回 PERMIT_BASE_NO，表示不适用
         *
         * @note 附身AI通常在精神控制效果施加时动态设置
         */
        static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }
};

/**
 * @brief 空生物AI类
 *
 * 完全不执行任何行为的AI实现。
 * 用于不需要任何AI逻辑的生物，如触发器、效果产生器等。
 *
 * 特点：
 * - 所有方法都是空实现
 * - 不会进入战斗
 * - 不会响应任何事件
 * - 不会脱战或重置
 *
 * 使用场景：
 * - 触发器生物（Trigger）
 * - 法术效果产生器
 * - 纯装饰性生物
 * - 点击型NPC（如任务物品）
 *
 * @note 这是最低优先级的AI之一，仅在明确不需要行为时使用
 */
class TC_GAME_API NullCreatureAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化空AI实例，将生物的反应状态设置为被动模式
         *
         * @param creature 拥有此AI的生物对象
         */
        explicit NullCreatureAI(Creature* creature);

        /**
         * @brief 视线范围内移动响应函数（空实现）
         * @param who 进入视线范围的单位（未使用）
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief 开始攻击响应函数（空实现）
         * @param target 要攻击的目标（未使用）
         */
        void AttackStart(Unit*) override { }

        /**
         * @brief 威胁开始通知函数（空实现）
         * @param who 开始产生威胁的单位（未使用）
         */
        void JustStartedThreateningMe(Unit*) override { }

        /**
         * @brief 进入战斗通知函数（空实现）
         * @param who 导致进入战斗的单位（未使用）
         */
        void JustEnteredCombat(Unit*) override { }

        /**
         * @brief AI更新函数（空实现）
         * @param diff 自上次更新以来经过的时间（毫秒，未使用）
         */
        void UpdateAI(uint32) override { }

        /**
         * @brief 生物出现通知函数（空实现）
         *
         * 当生物在世界中生成或重新出现时调用。
         */
        void JustAppeared() override { }

        /**
         * @brief 脱离战斗模式函数（空实现）
         * @param why 脱战原因（未使用）
         */
        void EnterEvadeMode(EvadeReason /*why*/) override { }

        /**
         * @brief 被附身状态变化通知函数（空实现）
         * @param isNew 是否是新附身状态（未使用）
         */
        void OnCharmed(bool /*isNew*/) override { }

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用空AI。
         *
         * @param creature 要检查的生物对象
         * @return 返回优先级值：
         *         - PERMIT_BASE_PROACTIVE + 50: 如果生物有法术点击标志（如任务物品）
         *         - PERMIT_BASE_PROACTIVE: 如果生物是触发器
         *         - PERMIT_BASE_IDLE: 默认情况
         *
         * 优先级说明：
         * - 法术点击生物优先级最高，因为它们不需要AI行为
         * - 触发器生物次之
         * - 其他生物使用最低优先级，作为后备选项
         */
        static int32 Permissible(Creature const* creature);
};

/**
 * @brief 小动物AI类
 *
 * 专门用于小动物（Critter）的AI实现。
 * 小动物在遇到威胁时会逃跑，不会反击。
 *
 * 特点：
 * - 继承自被动AI
 * - 被攻击时立即进入逃跑状态
 * - 逃跑完成后脱战重置
 *
 * 使用场景：
 * - 各种小动物（如兔子、松鼠、老鼠等）
 * - 非战斗型的中立生物
 *
 * @note 小动物通常具有很低的血量，设计为被击杀而非战斗
 */
class TC_GAME_API CritterAI : public PassiveAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化小动物AI实例
         *
         * @param creature 拥有此AI的小动物对象
         */
        explicit CritterAI(Creature* creature) : PassiveAI(creature) { }

        /**
         * @brief 进入战斗通知函数
         *
         * 当小动物被攻击进入战斗时调用。
         * 立即触发逃跑行为，小动物不会反击。
         *
         * @param who 攻击者（未使用）
         *
         * 主要逻辑：
         * - 检查是否已经在逃跑状态
         * - 如果不在逃跑状态，设置控制状态为逃跑
         * - 逃跑会持续一段时间后自动停止
         */
        void JustEngagedWith(Unit* /*who*/) override;

        /**
         * @brief 脱离战斗模式函数
         *
         * 当小动物需要脱离战斗时调用。
         * 清除逃跑状态并执行正常的脱战流程。
         *
         * @param why 脱战原因
         *
         * 主要逻辑：
         * - 检查是否在逃跑状态
         * - 如果在逃跑状态，解除逃跑控制
         * - 调用父类的脱战函数完成重置
         */
        void EnterEvadeMode(EvadeReason why) override;

        /**
         * @brief 移动完成通知函数
         *
         * 当小动物完成一个移动动作时调用。
         * 用于处理逃跑移动完成后的脱战。
         *
         * @param type 移动类型
         * @param id 移动标识（未使用）
         *
         * 主要逻辑：
         * - 检查移动类型是否为定时逃跑类型
         * - 如果是逃跑移动完成，触发脱战
         */
        void MovementInform(uint32 type, uint32 id) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用小动物AI。
         *
         * @param creature 要检查的生物对象
         * @return 返回优先级值：
         *         - PERMIT_BASE_PROACTIVE: 如果是小动物且不是守护者
         *         - PERMIT_BASE_NO: 其他情况
         *
         * @note 守护者类型的小动物（如某些宠物）不使用此AI，
         *       因为它们可能需要战斗行为
         */
        static int32 Permissible(Creature const* creature);
};

/**
 * @brief 触发器AI类
 *
 * 专门用于触发器生物的AI实现。
 * 触发器在召唤时会自动施放预设的法术。
 *
 * 特点：
 * - 继承自空AI
 * - 召唤时自动施放第一个预设法术
 * - 不执行其他任何行为
 *
 * 使用场景：
 * - 法术效果触发器
 * - 区域触发器
 * - 任务触发器
 *
 * @note 触发器生物通常设置在 creature_template 的 m_spells[0] 字段中
 */
class TC_GAME_API TriggerAI : public NullCreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化触发器AI实例
         *
         * @param creature 拥有此AI的触发器对象
         */
        explicit TriggerAI(Creature* creature) : NullCreatureAI(creature) { }

        /**
         * @brief 被召唤通知函数
         *
         * 当触发器被召唤到世界时调用。
         * 自动施放预设的第一个法术。
         *
         * @param summoner 召唤者对象
         *
         * 主要逻辑：
         * - 检查是否设置了第一个法术槽（m_spells[0]）
         * - 如果设置了法术，对自身施放该法术
         * - 设置原始施法者为召唤者，用于正确的仇恨和效果归属
         *
         * @note 法术的原始施法者设置为召唤者，
         *       确保法术效果（如伤害、光环）正确归属到召唤者
         */
        void IsSummonedBy(WorldObject* summoner) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用触发器AI。
         *
         * @param creature 要检查的生物对象
         * @return 返回优先级值：
         *         - PERMIT_BASE_SPECIAL: 如果是触发器且有预设法术
         *         - PERMIT_BASE_NO: 其他情况
         *
         * @note PERMIT_BASE_SPECIAL 是较高优先级，确保触发器正确使用此AI
         */
        static int32 Permissible(Creature const* creature);
};

#endif
