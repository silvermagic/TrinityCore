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
 * @file ReactorAI.h
 * @brief 反应型AI模块头文件
 *
 * 本文件定义了反应型AI类，用于控制只会被动响应攻击的生物。
 * 这类生物不会主动攻击，但当被攻击时会进行反击。
 *
 * 主要特点：
 * - 被动响应攻击：只有被攻击时才会反击
 * - 简单战斗逻辑：只执行基础近战攻击
 * - 适用于平民和中立生物：这些生物通常不会主动攻击玩家
 */

#ifndef TRINITY_REACTORAI_H
#define TRINITY_REACTORAI_H

#include "CreatureAI.h"

/**
 * @brief 反应型AI类
 *
 * 一种简单的AI实现，用于控制只会被动响应攻击的生物。
 * 这类生物不会主动寻找敌人，但当受到攻击时会进行反击。
 *
 * 特点：
 * - 不会主动进入战斗：不会因为视线范围内的敌人而攻击
 * - 只响应攻击：只有被攻击时才会反击
 * - 简单战斗行为：只执行基础近战攻击，没有特殊技能
 * - 适用于平民生物：如城镇中的NPC、中立生物等
 *
 * 使用场景：
 * - 平民NPC（如城镇居民）
 * - 中立生物（不会主动攻击玩家）
 * - 需要简单反击行为的生物
 *
 * 与其他AI的区别：
 * - 被动AI：完全不会反击
 * - 反应型AI：被攻击时会反击
 * - 主动型AI：会主动攻击视线范围内的敌人
 */
class TC_GAME_API ReactorAI : public CreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化反应型AI实例
         *
         * @param creature 拥有此AI的生物对象
         *
         * @note 不设置反应状态，使用默认配置
         */
        explicit ReactorAI(Creature* creature) : CreatureAI(creature) { }

        /**
         * @brief 视线范围内移动响应函数
         *
         * 当敌对单位进入生物视线范围时调用。
         * 反应型AI不主动响应视线范围内的敌人。
         *
         * @param who 进入视线范围的单位（未使用）
         *
         * @note 空实现，确保生物不会主动攻击视线范围内的敌人
         *       这是反应型AI的核心特征：被动响应而非主动攻击
         */
        void MoveInLineOfSight(Unit*) override { }

        /**
         * @brief AI更新函数
         *
         * 每个游戏周期调用的主更新函数。
         * 处理反应型AI的战斗逻辑。
         *
         * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
         *
         * 主要逻辑：
         * - UpdateVictim(): 检查并更新当前攻击目标
         *   - 如果没有当前目标，返回 false
         *   - 如果当前目标无效（死亡、消失等），选择新目标
         * - DoMeleeAttackIfReady(): 如果在近战范围内，执行近战攻击
         *
         * 攻击触发方式：
         * - 反应型AI不会主动选择目标
         * - 目标通常通过以下方式设置：
         *   - 被攻击时自动进入战斗
         *   - 通过脚本强制攻击
         *
         * @note 这是简化版的战斗AI，适用于不需要复杂战斗行为的生物
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，判断是否应该为此生物使用反应型AI。
         *
         * @param creature 要检查的生物对象
         * @return 返回优先级值：
         *         - PERMIT_BASE_REACTIVE: 如果是平民或对所有人中立
         *         - PERMIT_BASE_NO: 其他情况
         *
         * 判断逻辑：
         * 1. IsCivilian(): 检查生物是否为平民类型
         *    - 平民通常是城镇中的NPC，不会主动攻击
         *    - 但被攻击时会反击自卫
         * 2. IsNeutralToAll(): 检查生物是否对所有人中立
         *    - 这些生物没有阵营倾向
         *    - 不会主动攻击任何玩家
         *    - 但被攻击时仍会反击
         *
         * 优先级说明：
         * - PERMIT_BASE_REACTIVE 是反应型AI的基础优先级
         * - 这是较低的优先级，只有明确符合条件的生物才会使用
         *
         * @note 此函数确保只有合适的生物使用反应型AI
         */
        static int32 Permissible(Creature const* creature);
};

#endif
