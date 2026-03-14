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
 * @file    HomeMovementGenerator.cpp
 * @brief   回家移动生成器实现文件
 *
 * 实现了单位返回出生位置的移动逻辑，主要包括：
 * - 移动到出生点
 * - 恢复初始朝向
 * - 恢复初始生命值
 * - 加载生物附加组件
 * - 通知AI到达出生点
 *
 * 此生成器主要用于 Creature，Player 的模板实现为空。
 * 回家移动通常在以下情况触发：
 * - 生物脱离战斗
 * - 生物被重置
 * - 生物逃跑（evade）后归位
 */

#include "HomeMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "G3DPosition.hpp"
#include "MotionMaster.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Vehicle.h"

/**
 * @brief   构造函数 - 初始化回家移动生成器
 *
 * 设置回家移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 普通优先级（可被其他移动行为中断）
 * - Flags: 初始化待处理状态
 * - BaseUnitState: 漫游状态
 */
template<class T>
HomeMovementGenerator<T>::HomeMovementGenerator()
{
    this->Mode = MOTION_MODE_DEFAULT;                      // 设置为默认移动模式
    this->Priority = MOTION_PRIORITY_NORMAL;               // 普通优先级
    this->Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING;  // 标记为需要初始化
    this->BaseUnitState = UNIT_STATE_ROAMING;              // 基础状态为漫游状态
}

// 模板显式实例化
template HomeMovementGenerator<Creature>::HomeMovementGenerator();

/**
 * @brief   获取移动生成器类型
 * @return  返回回家移动类型标识
 */
template<class T>
MovementGeneratorType HomeMovementGenerator<T>::GetMovementGeneratorType() const
{
    return HOME_MOTION_TYPE;
}

// 模板显式实例化
template MovementGeneratorType HomeMovementGenerator<Creature>::GetMovementGeneratorType() const;

/**
 * @brief   默认模板实现（无操作）
 */
template<class T>
void HomeMovementGenerator<T>::SetTargetLocation(T*) { }

/**
 * @brief   生物版本 - 设置目标位置并开始回家移动
 * @param   owner - 拥有此移动生成器的生物
 *
 * 设置目标位置的流程：
 * 1. 检查生物状态（是否被定身、眩晕或分心）
 * 2. 清除可擦除的单位状态（保留逃跑状态）
 * 3. 添加漫游移动状态
 * 4. 获取出生位置
 * 5. 更新Z坐标（确保在有效高度）
 * 6. 创建并启动移动样条
 *
 * 注意：
 * - 如果生物处于 ROOT/STUNNED/DISTRACTED 状态，会被标记为中断
 * - 移动速度为奔跑速度
 * - 会设置朝向为出生朝向
 */
template<>
void HomeMovementGenerator<Creature>::SetTargetLocation(Creature* owner)
{
    // 如果生物处于定身、眩晕或分心状态，标记为中断
    // 这样可以防止生物在脱战后卡住（因为光环清除后这些状态可能仍然存在）
    if (owner->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_DISTRACTED))
    {
        AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        return;
    }

    // 清除所有可擦除的单位状态，但保留逃跑状态（UNIT_STATE_EVADE）
    owner->ClearUnitState(UNIT_STATE_ALL_ERASABLE & ~UNIT_STATE_EVADE);
    // 添加漫游移动状态
    owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);

    // 获取出生位置（包括坐标和朝向）
    Position destination = owner->GetHomePosition();
    // 创建移动样条初始化器
    Movement::MoveSplineInit init(owner);

    /*
     * TODO: 这段代码可能从未工作过，谁知道呢
     * top 总是这个生成器自己，所以这段代码调用自己的 GetResetPosition
     *
     * if (owner->GetMotionMaster()->empty() || !owner->GetMotionMaster()->top()->GetResetPosition(owner, x, y, z))
     * {
     *     owner->GetHomePosition(x, y, z, o);
     *     init.SetFacing(o);
     * }
     */

    // 更新Z坐标，确保位置在有效的高度范围内
    owner->UpdateAllowedPositionZ(destination.m_positionX, destination.m_positionY, destination.m_positionZ);
    // 设置移动目标位置
    init.MoveTo(PositionToVector3(destination));
    // 设置到达后的朝向（出生朝向）
    init.SetFacing(destination.GetOrientation());
    // 设置为奔跑速度（非行走）
    init.SetWalk(false);
    // 启动移动
    init.Launch();
}

/**
 * @brief   默认模板实现（无操作）
 */
template<class T>
void HomeMovementGenerator<T>::DoInitialize(T*) { }

/**
 * @brief   生物版本 - 初始化回家移动生成器
 * @param   owner - 拥有此移动生成器的生物
 *
 * 初始化流程：
 * 1. 清除初始化待处理和已停用标志
 * 2. 添加已初始化标志
 * 3. 允许生物搜索援助
 * 4. 设置目标位置并开始移动
 */
template<>
void HomeMovementGenerator<Creature>::DoInitialize(Creature* owner)
{
    // 清除初始化待处理和已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 添加已初始化标志
    AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 设置允许搜索援助标志，生物在回家途中可能会呼叫援助
    owner->SetNoSearchAssistance(false);

    // 设置目标位置并开始移动
    SetTargetLocation(owner);
}

/**
 * @brief   默认模板实现（无操作）
 */
template<class T>
void HomeMovementGenerator<T>::DoReset(T*) { }

/**
 * @brief   生物版本 - 重置回家移动生成器
 * @param   owner - 拥有此移动生成器的生物
 *
 * 重置流程：
 * 1. 清除已停用标志
 * 2. 调用初始化函数重新初始化
 */
template<>
void HomeMovementGenerator<Creature>::DoReset(Creature* owner)
{
    // 清除已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化生成器
    DoInitialize(owner);
}

/**
 * @brief   默认模板实现（返回 false，立即结束）
 */
template<class T>
bool HomeMovementGenerator<T>::DoUpdate(T*, uint32)
{
    return false;
}

/**
 * @brief   生物版本 - 更新回家移动逻辑
 * @param   owner - 拥有此移动生成器的生物
 * @param   diff - 时间增量（毫秒，未使用）
 * @return  是否继续运行（false 表示移动已完成或被中断）
 *
 * 更新流程：
 * 1. 检查是否被中断或移动已完成
 * 2. 如果是，设置通知启用标志并返回 false
 * 3. 否则返回 true 继续移动
 *
 * 性能注意事项：
 * - 此函数非常轻量，主要是状态检查
 */
template<>
bool HomeMovementGenerator<Creature>::DoUpdate(Creature* owner, uint32 /*diff*/)
{
    // 检查是否被中断或移动样条已完成
    if (HasFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED) || owner->movespline->Finalized())
    {
        // 设置通知启用标志，以便在结束时通知AI
        AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED);
        // 返回 false，结束此移动生成器
        return false;
    }
    // 返回 true，继续移动
    return true;
}

/**
 * @brief   默认模板实现（无操作）
 */
template<class T>
void HomeMovementGenerator<T>::DoDeactivate(T*) { }

/**
 * @brief   生物版本 - 停用回家移动生成器
 * @param   owner - 拥有此移动生成器的生物
 *
 * 设置停用标志并清除生物的漫游移动状态。
 * 调用时机：移动生成器被暂时停用时
 */
template<>
void HomeMovementGenerator<Creature>::DoDeactivate(Creature* owner)
{
    // 设置已停用标志
    AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 清除漫游移动状态
    owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
}

/**
 * @brief   默认模板实现（无操作）
 */
template<class T>
void HomeMovementGenerator<T>::DoFinalize(T*, bool, bool) { }

/**
 * @brief   生物版本 - 结束回家移动生成器
 * @param   owner - 拥有此移动生成器的生物
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 结束流程：
 * 1. 设置已结束标志
 * 2. 如果处于激活状态，清除漫游移动和逃跑状态
 * 3. 如果需要通知且通知已启用：
 *    a. 处理游泳标志
 *    b. 恢复初始生命值
 *    c. 加载生物附加组件
 *    d. 如果是载具，重置载具
 *    e. 通知AI已到达出生点
 *
 * 调用时机：移动完成或被移除时
 */
template<>
void HomeMovementGenerator<Creature>::DoFinalize(Creature* owner, bool active, bool movementInform)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果处于激活状态，清除状态
    if (active)
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE | UNIT_STATE_EVADE);

    // 如果需要发送移动通知且通知已启用
    if (movementInform && HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED))
    {
        // 如果生物没有战斗外的游泳标志，移除游泳标志
        if (!owner->HasCanSwimFlagOutOfCombat())
            owner->RemoveUnitFlag(UNIT_FLAG_CAN_SWIM);

        // 恢复到出生时的生命值
        owner->SetSpawnHealth();
        // 重新加载生物附加组件（可能包括光环、装备等）
        owner->LoadCreaturesAddon();

        // 如果是载具，重置载具
        if (owner->IsVehicle())
            owner->GetVehicleKit()->Reset(true);

        // 通知AI已到达出生点
        owner->AI()->JustReachedHome();
    }
}
