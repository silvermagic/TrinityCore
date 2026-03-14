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
 * @file RandomMovementGenerator.cpp
 * @brief 随机移动生成器实现
 *
 * 实现了随机移动生成器的具体逻辑。
 * 主要功能：
 * - 在参考点周围随机选择目标位置
 * - 使用寻路系统计算路径
 * - 模拟生物的移动-暂停行为模式
 * - 处理暂停和恢复请求
 */

#include "RandomMovementGenerator.h"
#include "Creature.h"
#include "Map.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "Random.h"

/**
 * @brief 构造函数 - 初始化随机移动参数
 *
 * 设置漫游距离和生成器基础属性：
 * - 移动模式：默认模式
 * - 优先级：普通优先级
 * - 标志：待初始化
 * - 基础状态：漫游状态
 *
 * @param distance 漫游距离（如果为 0，后续会使用生物自身的漫游距离）
 */
template<class T>
RandomMovementGenerator<T>::RandomMovementGenerator(float distance) : _timer(0), _reference(), _wanderDistance(distance), _wanderSteps(0)
{
    this->Mode = MOTION_MODE_DEFAULT;        // 默认移动模式
    this->Priority = MOTION_PRIORITY_NORMAL; // 普通优先级
    this->Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为待初始化
    this->BaseUnitState = UNIT_STATE_ROAMING; // 基础状态为漫游
}

// 模板实例化声明
template RandomMovementGenerator<Creature>::RandomMovementGenerator(float/* distance*/);

/**
 * @brief 获取移动生成器类型
 * @return 返回随机移动类型枚举值
 */
template<class T>
MovementGeneratorType RandomMovementGenerator<T>::GetMovementGeneratorType() const
{
    return RANDOM_MOTION_TYPE;
}

/**
 * @brief 暂停随机移动
 *
 * 支持两种暂停模式：
 * 1. 定时暂停：设置定时器，在指定时间后自动恢复
 * 2. 无限期暂停：设置暂停标志，需要手动恢复
 *
 * @param timer 暂停时间（毫秒），0 表示无限期暂停
 */
template<class T>
void RandomMovementGenerator<T>::Pause(uint32 timer /*= 0*/)
{
    if (timer)
    {
        // 定时暂停模式：设置定时暂停标志并启动定时器
        this->AddFlag(MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);
        _timer.Reset(timer);
        this->RemoveFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
    }
    else
    {
        // 无限期暂停模式：设置暂停标志
        this->AddFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
        this->RemoveFlag(MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);
    }
}

/**
 * @brief 恢复随机移动
 *
 * 清除暂停标志，可选重置定时器。
 *
 * @param overrideTimer 如果非 0，重置定时器为指定值
 */
template<class T>
void RandomMovementGenerator<T>::Resume(uint32 overrideTimer /*= 0*/)
{
    if (overrideTimer)
        _timer.Reset(overrideTimer);

    // 清除所有暂停标志
    this->RemoveFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
}

// 模板实例化声明
template MovementGeneratorType RandomMovementGenerator<Creature>::GetMovementGeneratorType() const;

/**
 * @brief 默认初始化实现 - 空操作
 *
 * Player 版本不执行任何操作，随机移动仅对生物有效。
 */
template<class T>
void RandomMovementGenerator<T>::DoInitialize(T*) { }

/**
 * @brief 初始化生物的随机移动生成器
 *
 * 执行初始化流程：
 * 1. 清除所有临时标志
 * 2. 设置已初始化标志
 * 3. 记录参考位置（生物当前位置）
 * 4. 停止当前移动
 * 5. 设置漫游距离（如果未指定则使用生物默认值）
 * 6. 随机初始化漫游步数（2-10 步）
 *
 * @param owner 进行随机移动的生物
 */
template<>
void RandomMovementGenerator<Creature>::DoInitialize(Creature* owner)
{
    // 清除初始化相关标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED | MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);
    AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 检查生物有效性
    if (!owner || !owner->IsAlive())
        return;

    // 记录当前位置作为参考点（漫游圆心）
    _reference = owner->GetPosition();

    // 停止当前移动
    owner->StopMoving();

    // 如果未指定漫游距离，使用生物的默认漫游距离
    if (_wanderDistance == 0.f)
        _wanderDistance = owner->GetWanderDistance();

    // 官方行为：生物会移动 2 到 10 次后暂停
    _wanderSteps = urand(2, 10);

    // 重置定时器和路径
    _timer.Reset(0);
    _path = nullptr;
}

/**
 * @brief 默认重置实现 - 空操作
 */
template<class T>
void RandomMovementGenerator<T>::DoReset(T*) { }

/**
 * @brief 重置生物的随机移动生成器
 *
 * 清除临时标志并重新初始化。
 *
 * @param owner 进行随机移动的生物
 */
template<>
void RandomMovementGenerator<Creature>::DoReset(Creature* owner)
{
    // 清除过渡和停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化
    DoInitialize(owner);
}

/**
 * @brief 默认设置随机位置实现 - 空操作
 */
template<class T>
void RandomMovementGenerator<T>::SetRandomLocation(T*) { }

/**
 * @brief 为生物设置随机目标位置并启动移动
 *
 * 核心方法，实现随机移动的核心逻辑：
 *
 * 1. 检查生物是否可以移动
 * 2. 在参考点周围的漫游距离内随机选择目标点
 * 3. 检查目标点是否在视线范围内
 * 4. 使用寻路系统计算路径
 * 5. 启动 MoveSpline 移动
 * 6. 更新漫游步数和定时器
 *
 * @param owner 进行随机移动的生物
 */
template<>
void RandomMovementGenerator<Creature>::SetRandomLocation(Creature* owner)
{
    if (!owner)
        return;

    // 检查生物是否处于不可移动状态
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE | UNIT_STATE_LOST_CONTROL) || owner->IsMovementPreventedByCasting())
    {
        AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        owner->StopMoving();
        _path = nullptr;
        return;
    }

    // 从参考点开始，在漫游距离内随机选择目标位置
    Position position(_reference);
    float distance = frand(0.f, _wanderDistance);  // 随机距离 [0, wanderDistance]
    float angle = frand(0.f, float(M_PI * 2));      // 随机角度 [0, 2π]
    owner->MovePositionToFirstCollision(position, distance, angle); // 计算碰撞点

    // 检查目标点是否在视线范围内，如果不在则稍后重试
    if (!owner->IsWithinLOS(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ()))
    {
        // 200 毫秒后重试
        _timer.Reset(200);
        return;
    }

    // 创建或复用寻路生成器
    if (!_path)
    {
        _path = std::make_unique<PathGenerator>(owner);
        _path->SetPathLengthLimit(30.0f); // 限制路径长度为 30 码
    }

    // 计算到目标点的路径
    bool result = _path->CalculatePath(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ());

    // 检查路径有效性
    // PATHFIND_FARFROMPOLY 不应检查，因为水中的生物通常远离导航网格
    if (!result || (_path->GetPathType() & PATHFIND_NOPATH)
                || (_path->GetPathType() & PATHFIND_SHORTCUT)
                /*|| (_path->GetPathType() & PATHFIND_FARFROMPOLY)*/)
    {
        // 路径无效，100 毫秒后重试
        _timer.Reset(100);
        return;
    }

    // 清除过渡标志，准备开始移动
    RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);

    // 设置漫游移动状态
    owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);

    // 根据生物的移动模板确定是否行走
    bool walk = true;
    switch (owner->GetMovementTemplate().GetRandom())
    {
        case CreatureRandomMovementType::CanRun:
            // 根据当前状态决定（保持当前行走/奔跑状态）
            walk = owner->IsWalking();
            break;
        case CreatureRandomMovementType::AlwaysRun:
            // 始终奔跑
            walk = false;
            break;
        default:
            // 默认行走
            break;
    }

    // 启动 MoveSpline 移动
    Movement::MoveSplineInit init(owner);
    init.MovebyPath(_path->GetPath());
    init.SetWalk(walk);
    int32 splineDuration = init.Launch();

    // 更新漫游步数
    --_wanderSteps;
    if (_wanderSteps)
    {
        // 还有步数剩余，移动完成后立即开始下一次移动
        _timer.Reset(splineDuration);
    }
    else
    {
        // 已完成所有步数，触发暂停
        // 官方行为：暂停 4-10 秒（使用整数秒数）
        _timer.Reset(splineDuration + urand(4, 10) * IN_MILLISECONDS);
        // 重置漫游步数
        _wanderSteps = urand(2, 10);
    }

    // 通知编队更新移动状态
    owner->SignalFormationMovement();
}

/**
 * @brief 默认更新实现 - 直接返回 false
 *
 * Player 版本不执行任何操作，随机移动仅对生物有效。
 */
template<class T>
bool RandomMovementGenerator<T>::DoUpdate(T*, uint32)
{
    return false;
}

/**
 * @brief 更新生物的随机移动生成器
 *
 * 每帧调用，执行以下检查：
 * 1. 检查生物是否存活
 * 2. 检查是否处于暂停或已结束状态
 * 3. 检查生物是否可以移动
 * 4. 更新定时器
 * 5. 在适当时机触发新的随机移动
 *
 * @param owner 进行随机移动的生物
 * @param diff 距上次更新的时间间隔（毫秒）
 * @return true 表示移动生成器继续运行
 */
template<>
bool RandomMovementGenerator<Creature>::DoUpdate(Creature* owner, uint32 diff)
{
    // 检查生物有效性
    if (!owner || !owner->IsAlive())
        return true;

    // 如果已结束或暂停，直接返回
    if (HasFlag(MOVEMENTGENERATOR_FLAG_FINALIZED | MOVEMENTGENERATOR_FLAG_PAUSED))
        return true;

    // 检查生物是否处于不可移动状态
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting())
    {
        AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        owner->StopMoving();
        _path = nullptr;
        return true;
    }
    else
    {
        // 清除中断标志
        RemoveFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
    }

    // 更新定时器
    _timer.Update(diff);

    // 触发新移动的条件：
    // 1. 速度更新待处理且移动未完成
    // 2. 定时器已过期且移动已完成
    if ((HasFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING) && !owner->movespline->Finalized()) || (_timer.Passed() && owner->movespline->Finalized()))
        SetRandomLocation(owner);

    return true;
}

/**
 * @brief 默认停用实现 - 空操作
 */
template<class T>
void RandomMovementGenerator<T>::DoDeactivate(T*) { }

/**
 * @brief 停用生物的随机移动生成器
 *
 * 设置停用标志并清除漫游移动状态。
 *
 * @param owner 进行随机移动的生物
 */
template<>
void RandomMovementGenerator<Creature>::DoDeactivate(Creature* owner)
{
    AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
}

/**
 * @brief 默认结束实现 - 空操作
 */
template<class T>
void RandomMovementGenerator<T>::DoFinalize(T*, bool, bool) { }

/**
 * @brief 结束生物的随机移动生成器
 *
 * 清理状态并停止移动。
 *
 * @param owner 进行随机移动的生物
 * @param active 是否处于活跃状态
 * @param movementInform 移动完成通知标志（未使用）
 */
template<>
void RandomMovementGenerator<Creature>::DoFinalize(Creature* owner, bool active, bool/* movementInform*/)
{
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    if (active)
    {
        // 清除漫游移动状态
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        // 停止当前移动
        owner->StopMoving();

        // TODO: 研究是否需要此修改，很可能不需要
        // 设置为非行走状态
        owner->SetWalk(false);
    }
}
