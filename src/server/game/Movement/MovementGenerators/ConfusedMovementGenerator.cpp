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
 * @file    ConfusedMovementGenerator.cpp
 * @brief   混乱移动生成器实现文件
 *
 * 实现了单位的混乱状态移动逻辑，包括：
 * - 随机目标点生成
 * - 碰撞检测和路径计算
 * - 视线（LOS）验证
 * - 移动控制和状态管理
 *
 * 混乱移动的核心机制：
 * 1. 单位以初始位置为中心进行随机移动
 * 2. 移动距离在 -2.0f 到 4.0f 范围内随机
 * 3. 移动方向在 0 到 2π 范围内随机
 * 4. 使用 PathGenerator 避开障碍物
 * 5. 视线检测确保目标点可见
 * 6. 移动速度为行走速度
 */

#include "ConfusedMovementGenerator.h"
#include "Creature.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Random.h"

/**
 * @brief   构造函数 - 初始化混乱移动生成器
 *
 * 设置混乱移动生成器的基本属性：
 * - Mode: 默认移动模式
 * - Priority: 最高优先级（混乱效果会覆盖其他移动）
 * - Flags: 初始化待处理状态
 * - BaseUnitState: 混乱单位状态
 */
template<class T>
ConfusedMovementGenerator<T>::ConfusedMovementGenerator() : _timer(0), _x(0.f), _y(0.f), _z(0.f)
{
    this->Mode = MOTION_MODE_DEFAULT;                      // 设置为默认移动模式
    this->Priority = MOTION_PRIORITY_HIGHEST;              // 最高优先级，覆盖其他移动行为
    this->Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING;  // 标记为需要初始化
    this->BaseUnitState = UNIT_STATE_CONFUSED;             // 基础状态为混乱状态
}

/**
 * @brief   获取移动生成器类型
 * @return  返回混乱移动类型标识
 */
template<class T>
MovementGeneratorType ConfusedMovementGenerator<T>::GetMovementGeneratorType() const
{
    return CONFUSED_MOTION_TYPE;
}

/**
 * @brief   初始化混乱移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 初始化流程：
 * 1. 清除初始化相关标志
 * 2. 设置已初始化标志
 * 3. 为单位添加混乱标志
 * 4. 停止当前移动
 * 5. 记录当前位置作为混乱中心点
 * 6. 重置定时器和路径
 */
template<class T>
void ConfusedMovementGenerator<T>::DoInitialize(T* owner)
{
    // 清除初始化待处理、过渡和已停用标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 添加已初始化标志
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

    // 安全检查：单位必须存在且存活
    if (!owner || !owner->IsAlive())
        return;

    // TODO: UNIT_FIELD_FLAGS 应该不由生成器处理
    // 设置混乱标志，使客户端显示混乱效果
    owner->SetUnitFlag(UNIT_FLAG_CONFUSED);
    // 停止当前移动，准备开始混乱移动
    owner->StopMoving();

    // 重置定时器为0，立即开始第一次移动计算
    _timer.Reset(0);
    // 记录当前位置作为混乱移动的参考中心点
    owner->GetPosition(_x, _y, _z);
    // 清空路径对象，将在第一次更新时创建
    _path = nullptr;
}

/**
 * @brief   重置混乱移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 重置流程：
 * 1. 清除过渡和已停用标志
 * 2. 调用初始化函数重新初始化
 */
template<class T>
void ConfusedMovementGenerator<T>::DoReset(T* owner)
{
    // 清除过渡和已停用状态标志
    MovementGenerator::RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 重新初始化生成器
    DoInitialize(owner);
}

/**
 * @brief   更新混乱移动逻辑
 * @param   owner - 拥有此移动生成器的单位
 * @param   diff - 时间增量（毫秒）
 * @return  是否继续运行（false 表示移除此生成器）
 *
 * 更新流程：
 * 1. 检查单位状态（存活、可移动）
 * 2. 处理移动中断情况
 * 3. 更新定时器
 * 4. 当定时器到期或速度改变时：
 *    a. 生成随机目标点
 *    b. 进行视线检测
 *    c. 计算移动路径
 *    d. 执行移动
 *
 * 性能注意事项：
 * - 路径计算和视线检测可能消耗较多CPU资源
 * - 失败时会延迟重试以避免性能问题
 */
template<class T>
bool ConfusedMovementGenerator<T>::DoUpdate(T* owner, uint32 diff)
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

        // 以记录的参考点为基准
        Position destination(_x, _y, _z);
        // 计算随机距离：范围 [-2.0, 4.0]
        // 这允许单位向前或向后移动，创造混乱效果
        float distance = 4.0f * frand(0.0f, 1.0f) - 2.0f;
        // 计算随机角度：范围 [0, 2π]
        float angle = frand(0.0f, 1.0f) * float(M_PI) * 2.0f;
        // 使用碰撞检测计算目标位置，确保不会穿过障碍物
        owner->MovePositionToFirstCollision(destination, distance, angle);

        // 检查目标点是否在视线内（LOS检测）
        if (!owner->IsWithinLOS(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()))
        {
            // 视线被阻挡，稍后重试
            _timer.Reset(200);  // 200毫秒后重试
            return true;
        }

        // 如果路径生成器未创建，则创建一个
        if (!_path)
        {
            _path = std::make_unique<PathGenerator>(owner);
            // 设置路径长度限制为30码，防止混乱移动太远
            _path->SetPathLengthLimit(30.0f);
        }

        // 计算到目标点的路径
        bool result = _path->CalculatePath(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
        // 检查路径计算是否成功，以及路径类型是否可用
        if (!result || (_path->GetPathType() & PATHFIND_NOPATH)      // 无路径
                    || (_path->GetPathType() & PATHFIND_SHORTCUT)    // 快捷路径（可能穿过墙壁）
                    || (_path->GetPathType() & PATHFIND_FARFROMPOLY)) // 远离导航网格
        {
            // 路径不可用，稍后重试
            _timer.Reset(100);  // 100毫秒后重试
            return true;
        }

        // 设置单位的混乱移动状态
        owner->AddUnitState(UNIT_STATE_CONFUSED_MOVE);

        // 创建移动样条初始化器
        Movement::MoveSplineInit init(owner);
        // 按路径移动
        init.MovebyPath(_path->GetPath());
        // 设置为行走速度（非奔跑）
        init.SetWalk(true);
        // 启动移动并获取移动时间
        int32 traveltime = init.Launch();
        // 设置下次移动的定时器：移动时间 + 随机延迟（800-1500毫秒）
        _timer.Reset(traveltime + urand(800, 1500));
    }

    return true;
}

/**
 * @brief   停用混乱移动生成器
 * @param   owner - 拥有此移动生成器的单位
 *
 * 设置停用标志并清除单位的混乱移动状态。
 * 调用时机：移动生成器被暂时停用时（如被其他移动行为覆盖）
 */
template<class T>
void ConfusedMovementGenerator<T>::DoDeactivate(T* owner)
{
    // 设置已停用标志
    MovementGenerator::AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    // 清除单位的混乱移动状态
    owner->ClearUnitState(UNIT_STATE_CONFUSED_MOVE);
}

/**
 * @brief   默认结束函数模板（无操作）
 */
template<class T>
void ConfusedMovementGenerator<T>::DoFinalize(T*, bool, bool) { }

/**
 * @brief   玩家版本的结束函数特化
 * @param   owner - 玩家对象
 * @param   active - 是否处于激活状态
 * @param   movementInform - 移动通知标志（未使用）
 *
 * 清理玩家的混乱状态：
 * - 设置已结束标志
 * - 移除混乱标志
 * - 停止移动
 */
template<>
void ConfusedMovementGenerator<Player>::DoFinalize(Player* owner, bool active, bool/* movementInform*/)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 仅在激活状态下执行清理
    if (active)
    {
        // 移除混乱标志，玩家恢复控制
        owner->RemoveUnitFlag(UNIT_FLAG_CONFUSED);
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
 * 清理生物的混乱状态：
 * - 设置已结束标志
 * - 移除混乱标志
 * - 清除混乱移动状态
 * - 如果有攻击目标，重新设置目标
 */
template<>
void ConfusedMovementGenerator<Creature>::DoFinalize(Creature* owner, bool active, bool/* movementInform*/)
{
    // 设置已结束标志
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    // 仅在激活状态下执行清理
    if (active)
    {
        // 移除混乱标志
        owner->RemoveUnitFlag(UNIT_FLAG_CONFUSED);
        // 清除混乱移动状态
        owner->ClearUnitState(UNIT_STATE_CONFUSED_MOVE);
        // 如果生物有攻击目标，重新设置目标（恢复战斗行为）
        if (owner->GetVictim())
            owner->SetTarget(owner->EnsureVictim()->GetGUID());
    }
}

template ConfusedMovementGenerator<Player>::ConfusedMovementGenerator();
template ConfusedMovementGenerator<Creature>::ConfusedMovementGenerator();
template MovementGeneratorType ConfusedMovementGenerator<Player>::GetMovementGeneratorType() const;
template MovementGeneratorType ConfusedMovementGenerator<Creature>::GetMovementGeneratorType() const;
template void ConfusedMovementGenerator<Player>::DoInitialize(Player*);
template void ConfusedMovementGenerator<Creature>::DoInitialize(Creature*);
template void ConfusedMovementGenerator<Player>::DoReset(Player*);
template void ConfusedMovementGenerator<Creature>::DoReset(Creature*);
template bool ConfusedMovementGenerator<Player>::DoUpdate(Player*, uint32);
template bool ConfusedMovementGenerator<Creature>::DoUpdate(Creature*, uint32);
template void ConfusedMovementGenerator<Player>::DoDeactivate(Player*);
template void ConfusedMovementGenerator<Creature>::DoDeactivate(Creature*);
