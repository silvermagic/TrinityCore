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
 * @file GuardAI.cpp
 *
 * @brief 守卫AI模块实现
 *
 * 本文件实现了GuardAI类的具体功能：
 * - 守卫的目标选择和攻击逻辑
 * - 持续追踪交战玩家的机制（防止脱战）
 * - 死亡时发送区域受攻击消息
 * - 脱战后返回出生点的逻辑
 *
 * 守卫AI是主城和重要区域保护者的核心AI实现。
 */

#include "GuardAI.h"
#include "Creature.h"
#include "Errors.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Player.h"

/**
 * @brief GuardAI 构造函数
 *
 * 职责：
 *   初始化卫兵AI对象，调用父类ScriptedAI的构造函数
 *
 * 参数：
 *   creature - 要应用此AI的生物对象指针
 *
 * 返回值：
 *   无
 */
GuardAI::GuardAI(Creature* creature) : ScriptedAI(creature)
{
}

/**
 * @brief 判断是否允许对指定生物使用GuardAI
 *
 * 职责：
 *   检查给定生物是否为卫兵类型，以确定是否可以使用此AI
 *
 * 参数：
 *   creature - 要检查的生物对象（const指针）
 *
 * 返回值：
 *   PERMIT_BASE_PROACTIVE - 如果生物是卫兵，允许使用此AI
 *   PERMIT_BASE_NO - 如果生物不是卫兵，不允许使用此AI
 *
 * 主要流程：
 *   1. 调用creature->IsGuard()检查生物是否为卫兵类型
 *   2. 如果是卫兵，返回PERMIT_BASE_PROACTIVE
 *   3. 否则返回PERMIT_BASE_NO
 */
int32 GuardAI::Permissible(Creature const* creature)
{
    if (creature->IsGuard())
        return PERMIT_BASE_PROACTIVE;

    return PERMIT_BASE_NO;
}

/**
 * @brief 卫兵AI更新函数 - 核心AI逻辑
 *
 * 职责：
 *   每帧调用，处理卫兵的战斗行为。如果卫兵有攻击目标，
 *   则执行近战攻击。
 *
 * 参数：
 *   diff - 自上次更新以来经过的时间（毫秒），未使用
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 调用UpdateVictim()检查并更新当前攻击目标
 *      - 如果没有有效的攻击目标，直接返回
 *   2. 如果有有效目标，调用DoMeleeAttackIfReady()
 *      - 检查攻击冷却是否就绪
 *      - 如果就绪，执行近战攻击
 */
void GuardAI::UpdateAI(uint32 /*diff*/)
{
    if (!UpdateVictim())
        return;

    DoMeleeAttackIfReady();
}

/**
 * @brief 判断是否始终能看到指定对象
 *
 * 职责：
 *   确定卫兵是否应该始终能够"看到"某个世界对象。
 *   主要用于确保卫兵能够持续追踪正在攻击它的玩家控制单位。
 *
 * 参数：
 *   obj - 要检查的世界对象（const指针）
 *
 * 返回值：
 *   true - 卫兵应始终能看到此对象
 *   false - 卫兵不应始终看到此对象
 *
 * 主要流程：
 *   1. 尝试将对象转换为Unit类型
 *   2. 检查该单位是否由玩家控制（IsControlledByPlayer）
 *   3. 检查该单位是否正在与卫兵交战（IsEngagedBy）
 *   4. 如果两个条件都满足，返回true，表示卫兵应始终"看到"该单位
 *      这确保了玩家控制的单位一旦与卫兵交战，卫兵就不会丢失目标
 */
bool GuardAI::CanSeeAlways(WorldObject const* obj)
{
    if (Unit const* unit = obj->ToUnit())
        if (unit->IsControlledByPlayer() && me->IsEngagedBy(unit))
            return true;
    return false;
}

/**
 * @brief 进入脱战模式（脱离战斗状态）
 *
 * 职责：
 *   当卫兵需要脱离战斗时调用，清理战斗状态并返回初始位置。
 *   这可能在以下情况发生：
 *   - 目标死亡或消失
 *   - 目标逃离卫兵的追击范围
 *   - 其他导致脱战的事件
 *
 * 参数：
 *   why - 脱战原因（未使用，用于日志和调试）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 检查卫兵是否已死亡
 *      - 如果已死亡：
 *        a. 停止所有移动（MoveIdle）
 *        b. 停止战斗状态（CombatStop）
 *        c. 结束战斗参与状态（EngagementOver）
 *        d. 直接返回
 *   2. 记录脱战日志
 *   3. 移除所有光环效果（RemoveAllAuras）
 *   4. 停止战斗状态（CombatStop）
 *   5. 结束战斗参与状态（EngagementOver）
 *   6. 移动回出生点（MoveTargetedHome）
 */
void GuardAI::EnterEvadeMode(EvadeReason /*why*/)
{
    if (!me->IsAlive())
    {
        me->GetMotionMaster()->MoveIdle();
        me->CombatStop(true);
        EngagementOver();
        return;
    }

    TC_LOG_TRACE("scripts.ai", "GuardAI::EnterEvadeMode: {} enters evade mode.", me->GetGUID().ToString());

    me->RemoveAllAuras();
    me->CombatStop(true);
    EngagementOver();

    me->GetMotionMaster()->MoveTargetedHome();
}

/**
 * @brief 卫兵死亡时的回调函数
 *
 * 职责：
 *   当卫兵被杀死时调用，向击杀者（如果是玩家）发送
 *   区域受到攻击的消息通知。
 *
 * 参数：
 *   killer - 击杀卫兵的单位指针，可能为nullptr
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 检查击杀者是否存在
 *   2. 尝试获取击杀者关联的玩家（可能是：
 *      - 击杀者本身就是玩家
 *      - 击杀者的主人或控制者是玩家
 *      - 例如：玩家的宠物击杀了卫兵）
 *   3. 如果找到关联玩家，发送区域受攻击消息
 *      - 这通常用于PvP区域战斗通知
 */
void GuardAI::JustDied(Unit* killer)
{
    if (killer)
        if (Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
            me->SendZoneUnderAttackMessage(player);
}
