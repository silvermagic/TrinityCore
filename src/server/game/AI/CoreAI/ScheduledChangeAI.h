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
 * @file ScheduledChangeAI.h
 * @brief 计划变更AI模块头文件
 *
 * 本模块提供了ScheduledChangeAI类的定义，这是一个特殊的AI实现，
 * 用于处理需要进行AI类型变更的生物。此类本身不提供任何实际的AI行为，
 * 所有方法都是空实现，主要用于作为AI切换过程中的临时占位符。
 *
 * 典型应用场景：
 * - 生物需要在运行时切换到不同的AI类型
 * - 作为AI变更过程中的中间状态
 * - 防止在AI切换期间发生意外的行为
 */

#ifndef TRINITY_SCHEDULEDCHANGEAI_H
#define TRINITY_SCHEDULEDCHANGEAI_H

#include "CreatureAI.h"

/**
 * @class ScheduledChangeAI
 * @brief 计划变更AI类 - 用于AI切换期间的临时AI实现
 *
 * 这是一个final类，不可被继承。它继承自CreatureAI，但所有虚函数
 * 都被重写为空实现。当生物计划更改其AI类型时，会临时使用此AI。
 *
 * 设计特点：
 * - 所有事件处理函数均为空实现
 * - Permissible方法返回PERMIT_BASE_NO，表示不应被优先选择
 * - 用于防止AI切换期间的意外行为
 *
 * 性能考虑：
 * - 极低的开销，适合作为临时占位符
 * - 不执行任何实际的游戏逻辑
 */
class TC_GAME_API ScheduledChangeAI final : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 关联的生物对象指针
         *
         * 初始化ScheduledChangeAI实例，调用基类CreatureAI的构造函数
         */
        explicit ScheduledChangeAI(Creature* creature);

        /**
         * @brief 视线内移动事件处理（空实现）
         * @param Unit 进入视线的单位指针（未使用）
         *
         * 当单位进入生物视线范围时调用
         * 在此AI中不执行任何操作
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief 攻击开始事件处理（空实现）
         * @param Unit 攻击目标指针（未使用）
         *
         * 当生物开始攻击目标时调用
         * 在此AI中不执行任何操作
         */
        void AttackStart(Unit*) override { }

        /**
         * @brief 威胁开始事件处理（空实现）
         * @param Unit 开始威胁的单位指针（未使用）
         *
         * 当某单位开始对生物产生威胁时调用
         * 在此AI中不执行任何操作
         */
        void JustStartedThreateningMe(Unit*) override { }

        /**
         * @brief 进入战斗事件处理（空实现）
         * @param Unit 进入战斗的对象指针（未使用）
         *
         * 当生物进入战斗状态时调用
         * 在此AI中不执行任何操作
         */
        void JustEnteredCombat(Unit*) override { }

        /**
         * @brief 更新AI事件处理（空实现）
         * @param uint32 距离上次更新的时间差（毫秒）（未使用）
         *
         * 每个世界更新周期调用一次
         * 在此AI中不执行任何操作
         */
        void UpdateAI(uint32) override { }

        /**
         * @brief 生物出现事件处理（空实现）
         *
         * 当生物生成或重新出现时调用
         * 在此AI中不执行任何操作
         */
        void JustAppeared() override { }

        /**
         * @brief 进入躲避模式事件处理（空实现）
         * @param EvadeReason 躲避原因枚举值（未使用）
         *
         * 当生物因各种原因脱离战斗时调用
         * 在此AI中不执行任何操作
         */
        void EnterEvadeMode(EvadeReason /*why*/) override { }

        /**
         * @brief 被魅惑状态变更事件处理（空实现）
         * @param bool 是否为新的魅惑状态（未使用）
         *
         * 当生物的魅惑状态改变时调用
         * 在此AI中不执行任何操作
         */
        void OnCharmed(bool /*isNew*/) override { }

        /**
         * @brief 检查AI是否适用于指定生物（静态方法）
         * @param creature 要检查的生物指针（未使用）
         * @return int 返回PERMIT_BASE_NO，表示此AI不应被自动选择
         *
         * 此方法用于AI选择系统判断此AI是否适合指定生物。
         * 返回PERMIT_BASE_NO确保此AI不会被自动分配给生物，
         * 只能通过显式代码使用。
         *
         * 调用时机：
         * - AI工厂创建AI实例时的适用性检查
         * - 不应被自动选择，仅供内部机制使用
         */
        static int Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }
};

#endif
