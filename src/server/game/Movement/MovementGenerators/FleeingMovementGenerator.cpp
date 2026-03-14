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
 * @file    FleeingMovementGenerator.cpp
 * @brief   逃跑移动生成器实现文件
 *
 * 实现了单位的逃跑状态移动逻辑，包括：
 * - 逃跑方向和距离计算
 * - 碰撞检测和路径计算
 * - 视线（LOS）验证
 * - 移动控制和状态管理
 * - 定时逃跑逻辑
 *
 * 逃跑移动的核心机制：
 * 1. 单位会远离逃跑目标（通常是施法者）
 * 2. 使用"安静距离"概念控制逃跑行为
 * 3. 使用 PathGenerator 避开障碍物
 * 4. 视线检测确保目标点可见
 * 5. 移动速度为奔跑速度
 */

#include "FleeingMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Unit.h"

/// 最小安静距离：单位与逃跑目标的最小安全距离
/// 如果距离小于此值，单位会继续远离目标
#define MIN_QUIET_DISTANCE 28.0f

/// 最大安静距离：单位与逃跑目标的最大影响距离
/// 如果距离大于此值，单位可能会回头或侧移
#define MAX_QUIET_DISTANCE 43.0f

/**
 * @brief   构造函数 - 初始化逃跑移动生成器
 * @param   fleeTargetGUID - 逃跑目标的GUID
 *
 * 设置逃跑移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 最高优先级（逃跑效果会覆盖其他移动）
 * - Flags: 初始化待处理状态
 * - BaseUnitState: 逃跑单位状态
 */
template<class T>
FleeingMovementGenerator<T>::FleeingMovementGenerator(ObjectGuid fleeTargetGUID) : _fleeTargetGUID(fleeTargetGUID), _timer(0)
{
    this->Mode = MOTION_MODE_DEFAULT;                      // 设置为默认移动模式
    this->Priority = MOTION_PRIORITY_HIGHEST;              // 最高优先级，覆盖其他移动行为
    this->Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING;  // 标记为需要初始化
    this->BaseUnitState = UNIT_STATE_FLEEING;              // 基础状态为逃跑状态
}

/**
 * @brief   获取移动生成器类型
 * @return  返回逃跑移动类型标识
 */
template<class T>
MovementGeneratorType FleeingMovementGenerator<T>::GetMovementGeneratorType() const
{
    return FLEEING_MOTION_TYPE;
}

/**
 * @brief   初始化逃跑移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 初始化流程：
 * 1. 清除初始化相关标志
 * 2. 设置已初始化标志
 * 3. 为单位添加逃跑标志
 * 4. 初始化路径生成器
 * 5. 开始第一次逃跑移动
 */
template<class T>
void FleeingMovementGenerator<T>::DoInitialize(T* owner)
{
    // 清除初始化待处理、过渡和已停用标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 添加已初始化标志
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 安全检查：单位必须存在且存活
    if (!owner || !owner->IsAlive())
        return;

    // TODO: UNIT_FIELD_FLAGS 应该不由生成器处理
    // 设置逃跑标志，使客户端显示逃跑效果
    owner->SetUnitFlag(UNIT_FLAG_FLEEING);

    // 清空路径对象，将在设置目标位置时创建
    _path = nullptr;
    // 设置初始逃跑目标位置
    SetTargetLocation(owner);
}

/**
 * @brief   重置逃跑移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 重置流程：
 * 1. 清除过渡和已停用标志
 * 2. 调用初始化函数重新初始化
 */
template<class T>
void FleeingMovementGenerator<T>::DoReset(T* owner)
{
    // 清除过渡和已停用状态标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化生成器
    DoInitialize(owner);
}

/**
 * @brief   更新逃跑移动逻辑
 * @param   owner - 拥有此移动生成器的单位
 * @param   diff - 时间增量（毫秒）
 * @return  是否继续运行（false 表示移除此生成器）
 *
 * 更新流程：
 * 1. 检查单位状态（存活、可移动）
 * 2. 处理移动中断情况
 * 3. 更新定时器
 * 4. 当定时器到期或速度改变时，计算新的逃跑路径
 *
 * 性能注意事项：
 * - 路径计算和视线检测可能消耗较多CPU资源
 * - 失败时会延迟重试以避免性能问题
 */
template<class T>
bool FleeingMovementGenerator<T>::DoUpdate(T* owner, uint32 diff)
{
    // 安全检查：单位必须存在且存活
    if (!owner || !owner->IsAlive())
        return false;

    // 检查单位是否处于不可移动状态或正在施法（施法可能阻止移动）
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        // 设置中断标志
        MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        // 停止移动
        owner->StopMoving();
        // 清空路径
        _path = nullptr;
        return true;  // 继续保持生成器，但不移动
    }
    else
    {
        // 清除中断标志
        MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
    }

    // 更新定时器
    _timer.Update(diff);

    // 检查是否需要计算新的移动路径
    // 条件1：速度更新待处理且移动未结束
    // 条件2：定时器到期且移动已结束
    if ((MovementGenerator::HasFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING) && !owner->movespline->Finalized()) || (_timer.Passed() && owner->movespline->Finalized()))
    {
        // 清除过渡标志
        MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY);
        // 设置新的逃跑目标位置
        SetTargetLocation(owner);
    }

    return true;
}

/**
 * @brief   停用逃跑移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 设置停用标志并清除单位的逃跑移动状态。
 * 调用时机：移动生成器被暂时停用时（如被其他移动行为覆盖）
 */
template<class T>
void FleeingMovementGenerator<T>::DoDeactivate(T* owner)
{
    // 设置已停用标志
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 清除单位的逃跑移动状态
    owner->ClearUnitState(UNIT_STATE_FLEEING_MOVE);
}

/**
 * @brief   默认结束函数模板（无操作）
 */
template<class T>
void FleeingMovementGenerator<T>::DoFinalize(T*, bool, bool)
{
}

/**
 * @brief   玩家版本的结束函数特化
 * @param   owner - 玩家对象
 * @param   active - 是否处于激活状态
 * @param   movementInform - 移动通知标志（未使用）
 *
 * 清理玩家的逃跑状态：
 * - 设置已结束标志
 * - 移除逃跑标志
 * - 清除逃跑移动状态
 * - 停止移动
 */
template<>
void FleeingMovementGenerator<Player>::DoFinalize(Player* owner, bool active, bool/* movementInform*/)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 仅在激活状态下执行清理
    if (active)
    {
        // 移除逃跑标志，玩家恢复控制
        owner->RemoveUnitFlag(UNIT_FLAG_FLEEING);
        // 清除逃跑移动状态
        owner->ClearUnitState(UNIT_STATE_FLEEING_MOVE);
        // 停止当前移动
        owner->StopMoving();
    }
}

/**
 * @brief   生物版本的结束函数特化
 * @param   owner - 生物对象
 * @param   active - 是否处于激活状态
 * @param   movementInform - 移动通知标志（未使用）
 *
 * 清理生物的逃跑状态：
 * - 设置已结束标志
 * - 移除逃跑标志
 * - 清除逃跑移动状态
 * - 如果有攻击目标，重新设置目标（恢复战斗行为）
 */
template<>
void FleeingMovementGenerator<Creature>::DoFinalize(Creature* owner, bool active, bool/* movementInform*/)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 仅在激活状态下执行清理
    if (active)
    {
        // 移除逃跑标志
        owner->RemoveUnitFlag(UNIT_FLAG_FLEEING);
        // 清除逃跑移动状态
        owner->ClearUnitState(UNIT_STATE_FLEEING_MOVE);
        // 如果生物有攻击目标，重新设置目标（恢复战斗行为）
        if (owner->GetVictim())
            owner->SetTarget(owner->EnsureVictim()->GetGUID());
    }
}

/**
 * @brief   设置目标位置并开始逃跑移动
 * @param   owner - 拥有此移动生成器的单位
 *
 * 设置目标位置的流程：
 * 1. 安全检查（单位存在、存活、可移动）
 * 2. 调用 GetPoint 计算逃跑目标点
 * 3. 视线检测（LOS）
 * 4. 创建路径生成器（如果尚未创建）
 * 5. 计算移动路径
 * 6. 验证路径有效性
 * 7. 启动移动样条
 * 8. 设置下次移动的定时器
 *
 * 性能注意事项：
 * - 路径计算可能消耗较多CPU资源
 * - 失败时会延迟重试（100-200毫秒）
 */
template<class T>
void FleeingMovementGenerator<T>::SetTargetLocation(T* owner)
{
    // 安全检查：单位必须存在且存活
    if (!owner || !owner->IsAlive())
        return;

    // 检查单位是否处于不可移动状态或正在施法
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        // 设置中断标志
        MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        // 停止移动
        owner->StopMoving();
        // 清空路径
        _path = nullptr;
        return;
    }

    // 获取当前位置作为起点
    Position destination = owner->GetPosition();
    // 计算逃跑目标点
    GetPoint(owner, destination);

    // 添加视线检测，确保目标点可见
    if (!owner->IsWithinLOS(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()))
    {
        // 视线被阻挡，200毫秒后重试
        _timer.Reset(200);
        return;
    }

    // 如果路径生成器未创建，则创建一个
    if (!_path)
    {
        _path = std::make_unique<PathGenerator>(owner);
        // 设置路径长度限制为30码，防止单位逃跑太远
        _path->SetPathLengthLimit(30.0f);
    }

    // 计算到目标点的路径
    bool result = _path->CalculatePath(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
    // 检查路径计算是否成功，以及路径类型是否可用
    if (!result || (_path->GetPathType() & PATHFIND_NOPATH)      // 无路径
                || (_path->GetPathType() & PATHFIND_SHORTCUT)    // 快捷路径（可能穿过墙壁）
                || (_path->GetPathType() & PATHFIND_FARFROMPOLY)) // 远离导航网格
    {
        // 路径不可用，100毫秒后重试
        _timer.Reset(100);
        return;
    }

    // 设置单位的逃跑移动状态
    owner->AddUnitState(UNIT_STATE_FLEEING_MOVE);

    // 创建移动样条初始化器
    Movement::MoveSplineInit init(owner);
    // 按路径移动
    init.MovebyPath(_path->GetPath());
    // 设置为奔跑速度（非行走），与混乱移动不同
    init.SetWalk(false);
    // 启动移动并获取移动时间
    int32 traveltime = init.Launch();
    // 设置下次移动的定时器：移动时间 + 随机延迟（800-1500毫秒）
    _timer.Reset(traveltime + urand(800, 1500));
}

/**
 * @brief   计算逃跑目标点
 * @param   owner - 拥有此移动生成器的单位
 * @param   position - 输出参数，计算得到的目标位置
 *
 * 逃跑距离和方向的计算策略：
 *
 * 1. 获取逃跑目标的位置信息：
 *    - 计算与逃跑目标的距离
 *    - 计算相对于逃跑目标的角度
 *
 * 2. 根据距离决定逃跑方向：
 *    a) 距离 < MIN_QUIET_DISTANCE（太近）：
 *       - 继续远离目标
 *       - 移动距离 = (MIN_QUIET_DISTANCE - 当前距离) * 随机系数(0.4-1.3)
 *       - 方向 = 远离目标的角度 + 小范围随机偏移(±π/8)
 *
 *    b) 距离 > MAX_QUIET_DISTANCE（太远）：
 *       - 可能会回头或侧移
 *       - 移动距离 = (MAX_QUIET_DISTANCE - MIN_QUIET_DISTANCE) * 随机系数(0.4-1.0)
 *       - 方向 = 朝向目标的角度 + 大范围随机偏移(±π/4)
 *
 *    c) 在安静距离范围内：
 *       - 随机方向移动
 *       - 移动距离 = (MAX_QUIET_DISTANCE - MIN_QUIET_DISTANCE) * 随机系数(0.6-1.2)
 *       - 方向 = 完全随机(0-2π)
 *
 * 3. 使用碰撞检测确定最终位置
 */
template<class T>
void FleeingMovementGenerator<T>::GetPoint(T* owner, Position &position)
{
    float casterDistance, casterAngle;

    // 尝试获取逃跑目标对象
    if (Unit* fleeTarget = ObjectAccessor::GetUnit(*owner, _fleeTargetGUID))
    {
        // 计算与逃跑目标的距离
        casterDistance = fleeTarget->GetDistance(owner);
        // 计算相对于逃跑目标的角度
        if (casterDistance > 0.2f)
        {
            // 获取逃跑目标指向单位的角度（即远离目标的方向）
            casterAngle = fleeTarget->GetAbsoluteAngle(owner);
        }
        else
        {
            // 距离太近，使用随机角度避免卡住
            casterAngle = frand(0.0f, 2.0f * float(M_PI));
        }
    }
    else
    {
        // 逃跑目标不存在（可能已死亡或消失）
        casterDistance = 0.0f;
        // 使用随机角度
        casterAngle = frand(0.0f, 2.0f * float(M_PI));
    }

    float distance, angle;

    // 根据与逃跑目标的距离决定逃跑策略
    if (casterDistance < MIN_QUIET_DISTANCE)
    {
        // 情况1：距离目标太近，继续远离
        // 移动距离：基于距离差的随机值，确保远离目标
        distance = frand(0.4f, 1.3f) * (MIN_QUIET_DISTANCE - casterDistance);
        // 移动方向：基本远离目标，带有小范围随机偏移
        angle = casterAngle + frand(-float(M_PI) / 8.0f, float(M_PI) / 8.0f);
    }
    else if (casterDistance > MAX_QUIET_DISTANCE)
    {
        // 情况2：距离目标太远，可能会回头或侧移
        // 移动距离：固定范围的随机值
        distance = frand(0.4f, 1.0f) * (MAX_QUIET_DISTANCE - MIN_QUIET_DISTANCE);
        // 移动方向：反向（朝向目标）+ 大范围随机偏移
        angle = -casterAngle + frand(-float(M_PI) / 4.0f, float(M_PI) / 4.0f);
    }
    else
    {
        // 情况3：在安静距离范围内，随机方向移动
        // 移动距离：固定范围的随机值
        distance = frand(0.6f, 1.2f) * (MAX_QUIET_DISTANCE - MIN_QUIET_DISTANCE);
        // 移动方向：完全随机
        angle = frand(0.0f, 2.0f * float(M_PI));
    }

    // 使用碰撞检测计算最终目标位置，确保不会穿过障碍物
    owner->MovePositionToFirstCollision(position, distance, angle);
}

// 模板显式实例化：为 Player 和 Creature 类型生成具体的模板代码
template FleeingMovementGenerator<Player>::FleeingMovementGenerator(ObjectGuid);
template FleeingMovementGenerator<Creature>::FleeingMovementGenerator(ObjectGuid);
template MovementGeneratorType FleeingMovementGenerator<Player>::GetMovementGeneratorType() const;
template MovementGeneratorType FleeingMovementGenerator<Creature>::GetMovementGeneratorType() const;
template void FleeingMovementGenerator<Player>::DoInitialize(Player*);
template void FleeingMovementGenerator<Creature>::DoInitialize(Creature*);
template void FleeingMovementGenerator<Player>::DoReset(Player*);
template void FleeingMovementGenerator<Creature>::DoReset(Creature*);
template bool FleeingMovementGenerator<Player>::DoUpdate(Player*, uint32);
template bool FleeingMovementGenerator<Creature>::DoUpdate(Creature*, uint32);
template void FleeingMovementGenerator<Player>::DoDeactivate(Player*);
template void FleeingMovementGenerator<Creature>::DoDeactivate(Creature*);
template void FleeingMovementGenerator<Player>::SetTargetLocation(Player*);
template void FleeingMovementGenerator<Creature>::SetTargetLocation(Creature*);
template void FleeingMovementGenerator<Player>::GetPoint(Player*, Position &);
template void FleeingMovementGenerator<Creature>::GetPoint(Creature*, Position &);

//---- TimedFleeingMovementGenerator（定时逃跑移动生成器）----

/**
 * @brief   更新定时逃跑移动逻辑
 * @param   owner - 拥有此移动生成器的单位
 * @param   diff - 时间增量（毫秒）
 * @return  是否继续运行（时间到期返回 false，移除此生成器）
 *
 * 更新流程：
 * 1. 安全检查（单位存在且存活）
 * 2. 更新总逃跑时间计时器
 * 3. 如果时间到期，返回 false 触发结束流程
 * 4. 调用父类的 DoUpdate 继续逃跑移动
 */
bool TimedFleeingMovementGenerator::Update(Unit* owner, uint32 diff)
{
    // 安全检查：单位必须存在且存活
    if (!owner || !owner->IsAlive())
        return false;

    // 更新总逃跑时间计时器
    _totalFleeTime.Update(diff);
    // 检查逃跑时间是否已到期
    if (_totalFleeTime.Passed())
        return false;  // 返回 false，移除此移动生成器

    // 调用父类的更新函数继续逃跑移动
    return FleeingMovementGenerator<Creature>::DoUpdate(owner->ToCreature(), diff);
}

/**
 * @brief   结束定时逃跑移动
 * @param   owner - 拥有此移动生成器的单位
 * @param   active - 是否处于激活状态
 * @param   movementInform - 是否需要发送移动通知
 *
 * 结束流程：
 * 1. 设置已结束标志
 * 2. 如果处于激活状态：
 *    - 移除逃跑标志
 *    - 停止移动
 *    - 如果有受害者且单位存活，恢复攻击行为
 * 3. 如果需要发送通知，通知AI逃跑结束
 *
 * 调用时机：逃跑时间到期或被提前移除时
 */
void TimedFleeingMovementGenerator::Finalize(Unit* owner, bool active, bool movementInform)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 如果不处于激活状态，直接返回
    if (!active)
        return;

    // 移除逃跑标志
    owner->RemoveUnitFlag(UNIT_FLAG_FLEEING);
    // 停止移动
    owner->StopMoving();

    // 如果有受害者且单位存活，恢复攻击行为
    if (Unit* victim = owner->GetVictim())
    {
        if (owner->IsAlive())
        {
            // 停止当前攻击
            owner->AttackStop();
            // 让AI重新开始攻击受害者
            owner->ToCreature()->AI()->AttackStart(victim);
        }
    }

    // 如果需要发送移动通知
    if (movementInform)
    {
        Creature* ownerCreature = owner->ToCreature();
        // 通知AI定时逃跑结束
        if (CreatureAI* AI = ownerCreature ? ownerCreature->AI() : nullptr)
            AI->MovementInform(TIMED_FLEEING_MOTION_TYPE, 0);
    }
}

/**
 * @brief   获取移动生成器类型
 * @return  返回定时逃跑移动类型标识
 */
MovementGeneratorType TimedFleeingMovementGenerator::GetMovementGeneratorType() const
{
    return TIMED_FLEEING_MOTION_TYPE;
}
