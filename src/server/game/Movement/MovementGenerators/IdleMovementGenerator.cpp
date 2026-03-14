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
 * @file    IdleMovementGenerator.cpp
 * @brief   空闲移动生成器实现文件
 *
 * 实现了多种空闲状态相关的移动生成器，包括：
 * - IdleMovementGenerator：空闲移动生成器，停止单位移动
 * - RotateMovementGenerator：旋转移动生成器，控制单位原地旋转
 * - DistractMovementGenerator：分心移动生成器，控制单位转向并保持一段时间
 * - AssistanceDistractMovementGenerator：援助分心移动生成器，结束后设置为攻击性状态
 *
 * 这些生成器主要用于：
 * - 单位的空闲状态管理
 * - 脚本控制单位转向
 * - 单位被吸引注意力的行为
 * - 援助生物的行为控制
 */

#include "IdleMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "G3DPosition.hpp"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "Unit.h"

//========================================
// IdleMovementGenerator 实现
//========================================

/**
 * @brief   构造函数 - 初始化空闲移动生成器
 *
 * 设置空闲移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 普通优先级
 * - Flags: 已初始化状态（空闲生成器直接初始化，不需要待处理）
 * - BaseUnitState: 0（无特殊状态）
 */
IdleMovementGenerator::IdleMovementGenerator()
{
    Mode = MOTION_MODE_DEFAULT;                    // 设置为默认移动模式
    Priority = MOTION_PRIORITY_NORMAL;             // 普通优先级
    Flags = MOVEMENTGENERATOR_FLAG_INITIALIZED;    // 标记为已初始化
    BaseUnitState = 0;                             // 无特殊状态
}

/**
 * @brief   初始化空闲移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 初始化时停止单位的移动。
 *
 * TODO: "if (!owner->IsStopped())" 是无用的，每个生成器都清理自己的 STATE_MOVE，
 * 结果是 StopMoving 几乎从未被调用。
 * 旧注释："StopMoving 是必需的，以便在单位的最后一个移动生成器过期时停止单位，
 * 但否则不应发送，否则会有很多冗余数据包"
 */
void IdleMovementGenerator::Initialize(Unit* owner)
{
    // 停止单位的移动
    owner->StopMoving();
}

/**
 * @brief   重置空闲移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 重置时停止单位的移动。
 */
void IdleMovementGenerator::Reset(Unit* owner)
{
    // 停止单位的移动
    owner->StopMoving();
}

/**
 * @brief   停用空闲移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 空闲状态下停用不需要特殊处理。
 */
void IdleMovementGenerator::Deactivate(Unit* /*owner*/)
{
}

/**
 * @brief   结束空闲移动生成器
 * @param   owner - 拥有此移动生成器的单位
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 设置已结束标志。
 */
void IdleMovementGenerator::Finalize(Unit* /*owner*/, bool/* active*/, bool/* movementInform*/)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);
}

/**
 * @brief   获取移动生成器类型
 * @return  返回空闲移动类型标识
 */
MovementGeneratorType IdleMovementGenerator::GetMovementGeneratorType() const
{
    return IDLE_MOTION_TYPE;
}

//----------------------------------------------------//
// RotateMovementGenerator 实现
//----------------------------------------------------//

/**
 * @brief   构造函数 - 初始化旋转移动生成器
 * @param   id - 移动ID，用于标识不同的旋转移动
 * @param   time - 旋转持续时间（毫秒）
 * @param   direction - 旋转方向（左或右）
 *
 * 设置旋转移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 普通优先级
 * - Flags: 初始化待处理状态
 * - BaseUnitState: 旋转状态
 */
RotateMovementGenerator::RotateMovementGenerator(uint32 id, uint32 time, RotateDirection direction) : _id(id), _duration(time), _maxDuration(time), _direction(direction)
{
    Mode = MOTION_MODE_DEFAULT;                            // 设置为默认移动模式
    Priority = MOTION_PRIORITY_NORMAL;                     // 普通优先级
    Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为需要初始化
    BaseUnitState = UNIT_STATE_ROTATING;                   // 基础状态为旋转状态
}

/**
 * @brief   初始化旋转移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 初始化流程：
 * 1. 清除初始化待处理和已停用标志
 * 2. 添加已初始化标志
 * 3. 停止单位的移动
 *
 * TODO: 这段代码应该在其他地方处理，比如 MovementInform
 *
 * if (owner->GetVictim())
 *     owner->SetInFront(owner->GetVictim());
 *
 * owner->AttackStop();
 */
void RotateMovementGenerator::Initialize(Unit* owner)
{
    // 清除初始化待处理和已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 添加已初始化标志
    AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 停止单位的移动
    owner->StopMoving();

    /*
     * TODO: 这段代码应该在其他地方处理，比如 MovementInform
     *
     * if (owner->GetVictim())
     *     owner->SetInFront(owner->GetVictim());
     *
     * owner->AttackStop();
     */
}

/**
 * @brief   重置旋转移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 清除已停用标志并重新初始化。
 */
void RotateMovementGenerator::Reset(Unit* owner)
{
    // 清除已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化
    Initialize(owner);
}

/**
 * @brief   更新旋转移动逻辑
 * @param   owner - 拥有此移动生成器的单位
 * @param   diff - 时间增量（毫秒）
 * @return  是否继续运行（时间到期返回 false）
 *
 * 更新流程：
 * 1. 获取当前朝向
 * 2. 根据旋转方向计算新的朝向角度
 * 3. 使用移动样条设置朝向
 * 4. 更新剩余时间
 * 5. 如果时间到期，设置通知启用标志并返回 false
 *
 * 旋转速度计算：
 * - 每毫秒旋转的角度 = 2π / 最大持续时间
 * - 这样可以在指定时间内完成一整圈旋转
 *
 * 性能注意事项：
 * - 每次更新都会创建移动样条，可能影响性能
 * - 角度归一化使用 while 循环，通常只需要一次迭代
 */
bool RotateMovementGenerator::Update(Unit* owner, uint32 diff)
{
    // 安全检查
    if (!owner)
        return false;

    // 获取当前朝向角度
    float angle = owner->GetOrientation();

    // 根据旋转方向计算新的角度
    if (_direction == ROTATE_DIRECTION_LEFT)
    {
        // 向左旋转（逆时针，角度增加）
        angle += float(diff) * float(M_PI) * 2.f / float(_maxDuration);
        // 角度归一化，保持在 [0, 2π) 范围内
        while (angle >= float(M_PI) * 2.f)
            angle -= float(M_PI) * 2.f;
    }
    else
    {
        // 向右旋转（顺时针，角度减少）
        angle -= float(diff) * float(M_PI) * 2.f / float(_maxDuration);
        // 角度归一化，保持在 [0, 2π) 范围内
        while (angle < 0.f)
            angle += float(M_PI) * 2.f;
    }

    // 创建移动样条初始化器
    Movement::MoveSplineInit init(owner);
    // 移动到当前位置（不实际移动，只改变朝向）
    init.MoveTo(PositionToVector3(*owner), false);

    // 如果单位在载具上，禁用载具路径转换
    if (owner->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && owner->GetTransGUID())
        init.DisableTransportPathTransformations();

    // 设置新的朝向角度
    init.SetFacing(angle);
    // 启动移动样条（应用朝向变化）
    init.Launch();

    // 更新剩余时间
    if (_duration > diff)
    {
        _duration -= diff;
        return true;  // 继续旋转
    }
    else
    {
        // 时间到期，设置通知启用标志
        AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED);
        return false;  // 结束旋转
    }
}

/**
 * @brief   停用旋转移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 设置已停用标志。
 */
void RotateMovementGenerator::Deactivate(Unit*)
{
    // 设置已停用标志
    AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
}

/**
 * @brief   结束旋转移动生成器
 * @param   owner - 拥有此移动生成器的单位
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 设置已结束标志，如果是生物且需要通知，则通知AI旋转完成。
 */
void RotateMovementGenerator::Finalize(Unit* owner, bool/* active*/, bool movementInform)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果需要通知且是生物，通知AI旋转完成
    if (movementInform && owner->GetTypeId() == TYPEID_UNIT)
        owner->ToCreature()->AI()->MovementInform(ROTATE_MOTION_TYPE, _id);
}

/**
 * @brief   获取移动生成器类型
 * @return  返回旋转移动类型标识
 */
MovementGeneratorType RotateMovementGenerator::GetMovementGeneratorType() const
{
    return ROTATE_MOTION_TYPE;
}

//----------------------------------------------------//
// DistractMovementGenerator 实现
//----------------------------------------------------//

/**
 * @brief   构造函数 - 初始化分心移动生成器
 * @param   timer - 分心持续时间（毫秒）
 * @param   orientation - 目标朝向（弧度）
 *
 * 设置分心移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 最高优先级（会覆盖其他移动）
 * - Flags: 初始化待处理状态
 * - BaseUnitState: 分心状态
 */
DistractMovementGenerator::DistractMovementGenerator(uint32 timer, float orientation) : _timer(timer), _orientation(orientation)
{
    Mode = MOTION_MODE_DEFAULT;                            // 设置为默认移动模式
    Priority = MOTION_PRIORITY_HIGHEST;                    // 最高优先级
    Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为需要初始化
    BaseUnitState = UNIT_STATE_DISTRACTED;                 // 基础状态为分心状态
}

/**
 * @brief   初始化分心移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 初始化流程：
 * 1. 清除初始化待处理和已停用标志
 * 2. 添加已初始化标志
 * 3. 如果单位不是站立状态，设置为站立状态
 * 4. 使用移动样条设置朝向
 */
void DistractMovementGenerator::Initialize(Unit* owner)
{
    // 清除初始化待处理和已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 添加已初始化标志
    AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 如果单位不是站立状态，设置为站立状态
    // 分心的生物会站起来查看
    if (!owner->IsStandState())
        owner->SetStandState(UNIT_STAND_STATE_STAND);

    // 创建移动样条初始化器
    Movement::MoveSplineInit init(owner);
    // 移动到当前位置（不实际移动，只改变朝向）
    init.MoveTo(PositionToVector3(*owner), false);

    // 如果单位在载具上，禁用载具路径转换
    if (owner->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && owner->GetTransGUID())
        init.DisableTransportPathTransformations();

    // 设置目标朝向
    init.SetFacing(_orientation);
    // 启动移动样条（应用朝向变化）
    init.Launch();
}

/**
 * @brief   重置分心移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 清除已停用标志并重新初始化。
 */
void DistractMovementGenerator::Reset(Unit* owner)
{
    // 清除已停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化
    Initialize(owner);
}

/**
 * @brief   更新分心移动逻辑
 * @param   owner - 拥有此移动生成器的单位
 * @param   diff - 时间增量（毫秒）
 * @return  是否继续运行（时间到期返回 false）
 *
 * 更新剩余时间，如果时间到期则返回 false 结束分心。
 */
bool DistractMovementGenerator::Update(Unit* owner, uint32 diff)
{
    // 安全检查
    if (!owner)
        return false;

    // 检查时间是否到期
    if (diff > _timer)
    {
        // 时间到期，设置通知启用标志
        AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED);
        return false;  // 结束分心
    }

    // 更新剩余时间
    _timer -= diff;
    return true;  // 继续分心
}

/**
 * @brief   停用分心移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 设置已停用标志。
 */
void DistractMovementGenerator::Deactivate(Unit*)
{
    // 设置已停用标志
    AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
}

/**
 * @brief   结束分心移动生成器
 * @param   owner - 拥有此移动生成器的单位
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 设置已结束标志，如果是生物且通知已启用，则转向出生朝向。
 *
 * TODO: 这段代码应该在其他地方处理
 */
void DistractMovementGenerator::Finalize(Unit* owner, bool/* active*/, bool movementInform)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // TODO: 这段代码应该在其他地方处理
    // 如果是生物，则将朝向返回到原始位置（对于空闲移动的生物）
    if (movementInform && HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED) && owner->GetTypeId() == TYPEID_UNIT)
    {
        // 获取出生朝向
        float angle = owner->ToCreature()->GetHomePosition().GetOrientation();
        // 转向出生朝向
        owner->SetFacingTo(angle);
    }
}

/**
 * @brief   获取移动生成器类型
 * @return  返回分心移动类型标识
 */
MovementGeneratorType DistractMovementGenerator::GetMovementGeneratorType() const
{
    return DISTRACT_MOTION_TYPE;
}

//----------------------------------------------------//
// AssistanceDistractMovementGenerator 实现
//----------------------------------------------------//

/**
 * @brief   构造函数 - 初始化援助分心移动生成器
 * @param   timer - 分心持续时间（毫秒）
 * @param   orientation - 目标朝向（弧度）
 *
 * 调用父类构造函数，但将优先级设置为普通（低于普通分心移动）。
 */
AssistanceDistractMovementGenerator::AssistanceDistractMovementGenerator(uint32 timer, float orientation) : DistractMovementGenerator(timer, orientation)
{
    // 将优先级设置为普通（父类设置为最高）
    Priority = MOTION_PRIORITY_NORMAL;
}

/**
 * @brief   结束援助分心移动生成器
 * @param   owner - 拥有此移动生成器的单位
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 设置已结束标志，如果是生物且通知已启用，则设置为攻击性状态。
 */
void AssistanceDistractMovementGenerator::Finalize(Unit* owner, bool/* active*/, bool movementInform)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果需要通知且是生物，设置为攻击性状态
    if (movementInform && HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED) && owner->GetTypeId() == TYPEID_UNIT)
        owner->ToCreature()->SetReactState(REACT_AGGRESSIVE);
}

/**
 * @brief   获取移动生成器类型
 * @return  返回援助分心移动类型标识
 */
MovementGeneratorType AssistanceDistractMovementGenerator::GetMovementGeneratorType() const
{
    return ASSISTANCE_DISTRACT_MOTION_TYPE;
}
