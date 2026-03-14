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
 * @file ReactorAI.cpp
 * @brief 反应型AI模块实现文件
 *
 * 本文件实现了反应型AI类，用于控制只会被动响应攻击的生物。
 * 这是一种简单的AI实现，适用于平民NPC和中立生物。
 */

#include "ReactorAI.h"
#include "Creature.h"

/**
 * @brief 检查反应型AI是否适用于指定生物
 *
 * 静态函数，判断是否应该为此生物使用反应型AI。
 * 根据生物的标志和阵营关系返回优先级。
 *
 * @param creature 要检查的生物指针
 * @return int32 返回权限值：
 *         - PERMIT_BASE_REACTIVE: 如果是平民或对所有人中立
 *         - PERMIT_BASE_NO: 其他情况
 *
 * 判断逻辑：
 * 1. IsCivilian(): 检查生物是否为平民类型
 *    - 平民是城镇中的NPC，如市民、村民等
 *    - 他们不会主动攻击玩家
 *    - 但被攻击时会自卫反击
 *    - 平民通常有自己的阵营，但不属于主要阵营系统
 *
 * 2. IsNeutralToAll(): 检查生物是否对所有人中立
 *    - 这些生物没有敌对阵营
 *    - 不会主动攻击任何玩家
 *    - 可能是某些特殊NPC或任务相关生物
 *    - 被攻击时仍会正常反击
 *
 * 优先级说明：
 * - PERMIT_BASE_REACTIVE 是反应型AI的基础优先级
 * - 这个优先级相对较低，通常在主动型AI之后
 * - 确保只有明确符合条件的生物才会使用此AI
 *
 * 设计考虑：
 * - 反应型AI提供简单的战斗行为
 * - 适用于不需要复杂AI逻辑的生物
 * - 可以作为后备AI使用
 *
 * 性能注意事项：
 * - 此函数在AI选择过程中会被频繁调用
 * - IsCivilian() 和 IsNeutralToAll() 都是比较轻量的检查
 */
int32 ReactorAI::Permissible(Creature const* creature)
{
    // 平民类型或对所有人中立的生物使用反应型AI
    if (creature->IsCivilian() || creature->IsNeutralToAll())
        return PERMIT_BASE_REACTIVE;

    // 其他情况不适用
    return PERMIT_BASE_NO;
}

/**
 * @brief 反应型AI更新函数
 *
 * 每个游戏周期调用的主更新函数。
 * 处理反应型AI的战斗逻辑，非常简单直接的战斗行为。
 *
 * @param diff 自上次更新以来经过的时间（毫秒），当前未使用
 *
 * 主要逻辑：
 * 1. UpdateVictim(): 更新并验证当前攻击目标
 *    - 检查是否有当前目标
 *    - 验证目标是否仍然有效（未死亡、可见、可攻击）
 *    - 如果目标无效，尝试选择新目标
 *    - 如果没有有效目标，返回 false
 *
 * 2. DoMeleeAttackIfReady(): 执行近战攻击
 *    - 只有在 UpdateVictim 返回 true 时才执行
 *    - 检查攻击冷却时间是否就绪
 *    - 检查是否在近战范围内
 *    - 执行主手和副手攻击（如果装备了副手武器）
 *
 * 战斗行为特点：
 * - 反应型AI不会主动选择目标
 * - 目标通常通过以下方式设置：
 *   - 被玩家攻击时自动进入战斗
 *   - 仇恨系统自动管理目标选择
 * - 不执行任何复杂的战斗策略
 * - 没有法术施放、技能使用等行为
 *
 * 与主动型AI的区别：
 * - 主动型AI：会主动攻击视线范围内的敌人
 * - 反应型AI：只有被攻击时才会反击
 * - 两者在战斗中的行为基本相同，主要区别在于是否主动寻找目标
 *
 * 性能注意事项：
 * - 这是一个非常轻量的AI实现
 * - 只执行必要的战斗检查和攻击操作
 * - 适合大量NPC同时存在时使用
 *
 * @note 这是简化版的战斗AI，适用于不需要复杂行为的生物
 *       如需要特殊技能或战斗策略，应使用其他AI类型或自定义AI
 */
void ReactorAI::UpdateAI(uint32 /*diff*/)
{
    // 尝试更新当前攻击目标，如果没有有效目标则返回
    if (!UpdateVictim())
        return;

    // 如果在近战范围内且攻击冷却就绪，执行近战攻击
    DoMeleeAttackIfReady();
}
