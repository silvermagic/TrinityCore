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
 * @file PointMovementGenerator.cpp
 * @brief 点位移动生成器实现
 *
 * 实现了点位移动生成器和求援移动生成器的具体逻辑。
 * 主要功能：
 * - 控制单位移动到指定目标点
 * - 处理移动中断和恢复
 * - 触发移动完成回调
 * - 执行生物求援行为
 */

#include "PointMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "MotionMaster.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "World.h"

//----- Point Movement Generator 点位移动生成器

/**
 * @brief 构造函数 - 初始化移动参数和生成器标志
 *
 * 设置移动目标和生成器基础属性：
 * - 移动模式：默认模式
 * - 优先级：普通优先级
 * - 标志：待初始化
 * - 基础状态：漫游状态
 */
template<class T>
PointMovementGenerator<T>::PointMovementGenerator(uint32 id, float x, float y, float z, bool generatePath, float speed, Optional<float> finalOrient) : _movementId(id), _x(x), _y(y), _z(z), _speed(speed), _generatePath(generatePath), _finalOrient(finalOrient)
{
    this->Mode = MOTION_MODE_DEFAULT;        // 默认移动模式
    this->Priority = MOTION_PRIORITY_NORMAL; // 普通优先级
    this->Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为待初始化
    this->BaseUnitState = UNIT_STATE_ROAMING; // 基础状态为漫游
}

/**
 * @brief 获取移动生成器类型
 * @return 返回点位移动类型枚举值
 */
template<class T>
MovementGeneratorType PointMovementGenerator<T>::GetMovementGeneratorType() const
{
    return POINT_MOTION_TYPE;
}

/**
 * @brief 初始化移动生成器 - 启动移动
 *
 * 执行初始化流程：
 * 1. 清除待初始化、过渡、停用标志
 * 2. 设置已初始化标志
 * 3. 特殊处理冲锋预路径事件
 * 4. 检查单位是否可以移动
 * 5. 启动 MoveSpline 移动
 * 6. 通知编队更新
 *
 * @param owner 移动的单位
 */
template<class T>
void PointMovementGenerator<T>::DoInitialize(T* owner)
{
    // 清除初始化相关标志，设置已初始化标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 特殊处理：冲锋预路径事件，仅设置移动状态不实际移动
    if (_movementId == EVENT_CHARGE_PREPATH)
    {
        owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);
        return;
    }

    // 检查单位是否处于不可移动状态（如被定身、施法中等）
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        owner->StopMoving();
        return;
    }

    // 设置单位漫游移动状态
    owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);

    // 初始化 MoveSpline 移动
    Movement::MoveSplineInit init(owner);
    init.MoveTo(_x, _y, _z , _generatePath);

    // 设置自定义速度（如果指定）
    if (_speed > 0.0f)
        init.SetVelocity(_speed);

    // 设置到达后的朝向（如果指定）
    if (_finalOrient)
        init.SetFacing(*_finalOrient);

    // 启动移动
    init.Launch();

    // 如果是生物，通知编队更新移动状态
    if (Creature* creature = owner->ToCreature())
        creature->SignalFormationMovement();
}

/**
 * @brief 重置移动生成器 - 重新开始移动
 *
 * 清除过渡和停用标志，然后重新初始化移动。
 * 通常在移动生成器被重新激活时调用。
 *
 * @param owner 移动的单位
 */
template<class T>
void PointMovementGenerator<T>::DoReset(T* owner)
{
    // 清除过渡和停用标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化移动
    DoInitialize(owner);
}

/**
 * @brief 更新移动生成器 - 每帧更新
 *
 * 核心更新逻辑：
 * 1. 处理冲锋预路径特殊情况
 * 2. 检查单位是否可以移动
 * 3. 处理中断和速度变化
 * 4. 检查移动是否完成
 *
 * @param owner 移动的单位
 * @param diff 距上次更新的时间间隔（毫秒），本函数未使用
 * @return true 继续运行，false 移动已完成
 */
template<class T>
bool PointMovementGenerator<T>::DoUpdate(T* owner, uint32 /*diff*/)
{
    // 单位无效时结束移动生成器
    if (!owner)
        return false;

    // 特殊处理：冲锋预路径事件
    if (_movementId == EVENT_CHARGE_PREPATH)
    {
        if (owner->movespline->Finalized())
        {
            // 移动完成，启用通知标志
            MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED);
            return false;
        }
        return true;
    }

    // 检查单位是否处于不可移动状态
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        owner->StopMoving();
        return true;
    }

    // 处理中断恢复或速度更新
    // 条件1：移动被中断且 spline 已完成
    // 条件2：速度更新待处理且 spline 未完成
    if ((MovementGenerator::HasFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED) && owner->movespline->Finalized()) || (MovementGenerator::HasFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING) && !owner->movespline->Finalized()))
    {
        // 清除中断和速度更新标志
        MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED | MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING);

        owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);

        // 重新启动移动
        Movement::MoveSplineInit init(owner);
        init.MoveTo(_x, _y, _z, _generatePath);
        if (_speed > 0.0f) // 默认值为 0.0，如果为 0.0 则 spline 会使用单位的 GetSpeed
            init.SetVelocity(_speed);
        init.Launch();

        // 通知编队更新
        if (Creature* creature = owner->ToCreature())
            creature->SignalFormationMovement();
    }

    // 检查移动是否已完成
    if (owner->movespline->Finalized())
    {
        MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY);
        MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED);
        return false;
    }
    return true;
}

/**
 * @brief 停用移动生成器 - 切换到后台状态
 *
 * 当移动生成器被更高优先级的移动替代时调用。
 * 设置停用标志并清除单位的漫游移动状态。
 *
 * @param owner 移动的单位
 */
template<class T>
void PointMovementGenerator<T>::DoDeactivate(T* owner)
{
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
}

/**
 * @brief 结束移动生成器 - 清理和通知
 *
 * 当移动生成器从 MotionMaster 移除时调用。
 * 执行清理工作并触发移动完成通知。
 *
 * @param owner 移动的单位
 * @param active 是否处于活跃状态
 * @param movementInform 是否触发移动完成回调
 */
template<class T>
void PointMovementGenerator<T>::DoFinalize(T* owner, bool active, bool movementInform)
{
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果处于活跃状态，清除漫游移动状态
    if (active)
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);

    // 如果启用了通知标志且需要触发回调，则调用 MovementInform
    if (movementInform && MovementGenerator::HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED))
        MovementInform(owner);
}

/**
 * @brief 默认移动完成通知 - 空实现
 *
 * 模板默认版本不做任何处理，由特化版本实现具体逻辑。
 */
template<class T>
void PointMovementGenerator<T>::MovementInform(T*) { }

/**
 * @brief 生物移动完成通知 - 触发 AI 回调
 *
 * 通知生物 AI 移动已完成，让 AI 可以执行后续逻辑。
 *
 * @param owner 完成移动的生物
 */
template <>
void PointMovementGenerator<Creature>::MovementInform(Creature* owner)
{
    // 调用 AI 的 MovementInform 方法，传递移动类型和移动 ID
    if (owner->AI())
        owner->AI()->MovementInform(POINT_MOTION_TYPE, _movementId);
}

// 模板实例化 - 显式实例化 Player 和 Creature 版本
template PointMovementGenerator<Player>::PointMovementGenerator(uint32, float, float, float, bool, float, Optional<float>);
template PointMovementGenerator<Creature>::PointMovementGenerator(uint32, float, float, float, bool, float, Optional<float>);
template MovementGeneratorType PointMovementGenerator<Player>::GetMovementGeneratorType() const;
template MovementGeneratorType PointMovementGenerator<Creature>::GetMovementGeneratorType() const;
template void PointMovementGenerator<Player>::DoInitialize(Player*);
template void PointMovementGenerator<Creature>::DoInitialize(Creature*);
template void PointMovementGenerator<Player>::DoReset(Player*);
template void PointMovementGenerator<Creature>::DoReset(Creature*);
template bool PointMovementGenerator<Player>::DoUpdate(Player*, uint32);
template bool PointMovementGenerator<Creature>::DoUpdate(Creature*, uint32);
template void PointMovementGenerator<Player>::DoDeactivate(Player*);
template void PointMovementGenerator<Creature>::DoDeactivate(Creature*);
template void PointMovementGenerator<Player>::DoFinalize(Player*, bool, bool);
template void PointMovementGenerator<Creature>::DoFinalize(Creature*, bool, bool);

//---- AssistanceMovementGenerator 求援移动生成器

/**
 * @brief 结束求援移动 - 触发求援行为
 *
 * 当生物到达求援位置后执行：
 * 1. 启用求援功能
 * 2. 呼叫附近的同伴协助
 * 3. 如果生物仍存活，启动求援分心定时器
 *
 * @param owner 移动的单位
 * @param active 是否处于活跃状态
 * @param movementInform 是否触发移动完成回调
 */
void AssistanceMovementGenerator::Finalize(Unit* owner, bool active, bool movementInform)
{
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果处于活跃状态，清除漫游移动状态
    if (active)
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);

    // 如果启用了通知标志且需要触发回调，执行求援逻辑
    if (movementInform && HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED))
    {
        Creature* ownerCreature = owner->ToCreature();

        // 启用求援功能
        ownerCreature->SetNoCallAssistance(false);

        // 呼叫附近的同伴协助
        ownerCreature->CallAssistance();

        // 如果生物仍存活，启动求援分心定时器
        // 这让生物在等待同伴到达时暂时停止攻击
        if (ownerCreature->IsAlive())
            ownerCreature->GetMotionMaster()->MoveSeekAssistanceDistract(sWorld->getIntConfig(CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY));
    }
}

/**
 * @brief 获取移动生成器类型
 * @return 返回求援移动类型枚举值
 */
MovementGeneratorType AssistanceMovementGenerator::GetMovementGeneratorType() const
{
    return ASSISTANCE_MOTION_TYPE;
}
