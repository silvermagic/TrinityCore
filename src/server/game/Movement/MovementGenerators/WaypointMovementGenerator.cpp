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
 * @file WaypointMovementGenerator.cpp
 * @brief 路点移动生成器实现
 *
 * 实现了路点移动生成器的具体逻辑。
 * 主要功能：
 * - 加载和管理路径数据
 * - 控制生物按路点顺序移动
 * - 处理路点延迟和事件
 * - 通知 AI 路点状态变化
 * - 支持暂停、恢复和中断恢复
 */

#include "WaypointMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Errors.h"
#include "Log.h"
#include "Map.h"
#include "MovementDefines.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "ObjectMgr.h"
#include "Transport.h"
#include "WaypointManager.h"

/**
 * @brief 构造函数 - 从数据库路径 ID 创建路点移动生成器
 *
 * 初始化从数据库加载路径的移动生成器。
 *
 * @param pathId 数据库中的路径 ID
 * @param repeating 是否循环路径
 */
WaypointMovementGenerator<Creature>::WaypointMovementGenerator(uint32 pathId, bool repeating) : _nextMoveTime(0), _pathId(pathId), _repeating(repeating), _loadedFromDB(true)
{
    Mode = MOTION_MODE_DEFAULT;        // 默认移动模式
    Priority = MOTION_PRIORITY_NORMAL; // 普通优先级
    Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为待初始化
    BaseUnitState = UNIT_STATE_ROAMING; // 基础状态为漫游
}

/**
 * @brief 构造函数 - 使用自定义路径创建路点移动生成器
 *
 * 初始化使用自定义路径数据的移动生成器。
 *
 * @param path 自定义路径数据引用
 * @param repeating 是否循环路径
 */
WaypointMovementGenerator<Creature>::WaypointMovementGenerator(WaypointPath& path, bool repeating) : _nextMoveTime(0), _pathId(0), _repeating(repeating), _loadedFromDB(false)
{
    _path = &path; // 直接使用传入的路径指针

    Mode = MOTION_MODE_DEFAULT;        // 默认移动模式
    Priority = MOTION_PRIORITY_NORMAL; // 普通优先级
    Flags = MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING; // 标记为待初始化
    BaseUnitState = UNIT_STATE_ROAMING; // 基础状态为漫游
}

/**
 * @brief 获取移动生成器类型
 * @return 返回路点移动类型枚举值
 */
MovementGeneratorType WaypointMovementGenerator<Creature>::GetMovementGeneratorType() const
{
    return WAYPOINT_MOTION_TYPE;
}

/**
 * @brief 暂停路点移动
 *
 * 支持两种暂停模式：
 * 1. 定时暂停：在指定时间后自动恢复
 * 2. 无限期暂停：需要手动调用 Resume 恢复
 *
 * @param timer 暂停时间（毫秒），0 表示无限期暂停
 */
void WaypointMovementGenerator<Creature>::Pause(uint32 timer/* = 0*/)
{
    if (timer)
    {
        // 如果已经处于暂停状态，不重复暂停
        if (HasFlag(MOVEMENTGENERATOR_FLAG_PAUSED))
            return;

        // 设置定时暂停标志并启动定时器
        AddFlag(MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);
        _nextMoveTime.Reset(timer);
        RemoveFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
    }
    else
    {
        // 设置无限期暂停标志
        AddFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
        // 重置定时器为 1，确保 Update 不会认为节点已到达
        _nextMoveTime.Reset(1);
        RemoveFlag(MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);
    }
}

/**
 * @brief 恢复路点移动
 *
 * 清除暂停标志，可选重置定时器。
 *
 * @param overrideTimer 如果非 0，重置定时器为指定值
 */
void WaypointMovementGenerator<Creature>::Resume(uint32 overrideTimer/* = 0*/)
{
    if (overrideTimer)
        _nextMoveTime.Reset(overrideTimer);

    // 如果定时器已过期，重置为 1 以避免 Update 认为节点已到达
    if (_nextMoveTime.Passed())
        _nextMoveTime.Reset(1);

    // 清除暂停标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_PAUSED);
}

/**
 * @brief 获取重置位置
 *
 * 返回当前路点的位置，用于生物被拉回时定位。
 *
 * @param owner 拥有此移动生成器的单位（未使用）
 * @param x 输出参数，X 坐标
 * @param y 输出参数，Y 坐标
 * @param z 输出参数，Z 坐标
 * @return true 表示成功获取位置，false 表示路径为空
 */
bool WaypointMovementGenerator<Creature>::GetResetPosition(Unit* /*owner*/, float& x, float& y, float& z)
{
    // 检查路径是否为空，防止崩溃
    if (!_path || _path->nodes.empty())
        return false;

    // 断言当前节点索引有效
    ASSERT(_currentNode < _path->nodes.size(), "WaypointMovementGenerator::GetResetPosition: tried to reference a node id (%u) which is not included in path (%u)", _currentNode, _path->id);
    WaypointNode const &waypoint = _path->nodes.at(_currentNode);

    // 返回当前路点坐标
    x = waypoint.x;
    y = waypoint.y;
    z = waypoint.z;
    return true;
}

/**
 * @brief 初始化路点移动生成器
 *
 * 执行初始化流程：
 * 1. 清除临时标志
 * 2. 如果是从数据库加载，获取路径数据
 * 3. 停止当前移动
 * 4. 设置初始延迟
 *
 * @param owner 拥有此移动生成器的生物
 */
void WaypointMovementGenerator<Creature>::DoInitialize(Creature* owner)
{
    // 清除初始化相关标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 如果是从数据库加载路径
    if (_loadedFromDB)
    {
        // 如果未指定路径 ID，使用生物的默认路径
        if (!_pathId)
            _pathId = owner->GetWaypointPath();

        // 从路径管理器获取路径数据
        _path = sWaypointMgr->GetPath(_pathId);
    }

    // 检查路径是否有效
    if (!_path)
    {
        TC_LOG_ERROR("sql.sql", "WaypointMovementGenerator::DoInitialize: couldn't load path for creature ({}) (_pathId: {})", owner->GetGUID().ToString(), _pathId);
        return;
    }

    // 停止当前移动
    owner->StopMoving();

    // 设置初始延迟为 1 秒
    _nextMoveTime.Reset(1000);
}

/**
 * @brief 重置路点移动生成器
 *
 * 当移动生成器被重新激活时调用。
 * 清除标志并准备恢复移动。
 *
 * @param owner 拥有此移动生成器的生物
 */
void WaypointMovementGenerator<Creature>::DoReset(Creature* owner)
{
    // 清除过渡和停用标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_DEACTIVATED);

    // 停止当前移动
    owner->StopMoving();

    // 如果未结束且定时器已过期，重置定时器
    if (!HasFlag(MOVEMENTGENERATOR_FLAG_FINALIZED) && _nextMoveTime.Passed())
        _nextMoveTime.Reset(1); // 确保 Update 不会认为节点已到达
}

/**
 * @brief 更新路点移动生成器
 *
 * 核心更新方法，每帧调用。执行以下逻辑：
 * 1. 检查生物状态（存活、暂停等）
 * 2. 处理中断恢复
 * 3. 更新移动中的生物
 * 4. 处理定时器到期
 * 5. 触发路点到达事件
 *
 * 状态机逻辑：
 * - 移动中：更新位置，处理速度变化
 * - 等待中：更新定时器，到期后移动到下一节点
 * - 已到达：触发 OnArrived，开始下一次移动
 *
 * @param owner 拥有此移动生成器的生物
 * @param diff 距上次更新的时间间隔（毫秒）
 * @return true 表示移动生成器继续运行
 */
bool WaypointMovementGenerator<Creature>::DoUpdate(Creature* owner, uint32 diff)
{
    // 检查生物有效性
    if (!owner || !owner->IsAlive())
        return true;

    // 如果已结束、暂停或路径无效，直接返回
    if (HasFlag(MOVEMENTGENERATOR_FLAG_FINALIZED | MOVEMENTGENERATOR_FLAG_PAUSED) || !_path || _path->nodes.empty())
        return true;

    // 检查生物是否处于不可移动状态
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE | UNIT_STATE_LOST_CONTROL) || owner->IsMovementPreventedByCasting())
    {
        AddFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
        owner->StopMoving();
        return true;
    }

    // 处理中断恢复
    if (HasFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED))
    {
        /*
         * 重新启动的条件：
         * - 有定时器？ -> 是否在移动时被中断？需要检查：
         *   -> 有定时器 - 是否因为等待下一节点？
         *   -> 有定时器 - 是否因为在移动时设置的（如定时暂停）？
         *
         * - 无定时器？ -> 移动是否有效？
         *
         * TODO: ((_nextMoveTime.Passed() && VALID_MOVEMENT) || (!_nextMoveTime.Passed() && !HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED)))
         */
        if (HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED) && (_nextMoveTime.Passed() || !HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED)))
        {
            StartMove(owner, true);
            return true;
        }

        RemoveFlag(MOVEMENTGENERATOR_FLAG_INTERRUPTED);
    }

    // 如果正在移动
    if (!owner->movespline->Finalized())
    {
        // 更新家庭位置（每个 MotionMaster::UpdateMotion）
        // 注意：仅在不在交通工具上时更新
        if (!owner->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) || owner->GetTransGUID().IsEmpty())
            owner->SetHomePosition(owner->GetPosition());

        // 如果速度变化，重新启动移动
        if (HasFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING))
            StartMove(owner, true);
    }
    else if (!_nextMoveTime.Passed()) // 未移动，但有定时器（等待延迟）
    {
        if (UpdateTimer(diff))
        {
            // 定时器到期
            if (!HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED)) // 初始移动调用
            {
                StartMove(owner);
                return true;
            }
            else if (!HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED)) // 定时器在节点到达前设置，现在恢复
            {
                StartMove(owner, true);
                return true;
            }
        }
        else
            return true; // 继续等待
    }
    else // 未移动，无定时器
    {
        // 如果已初始化但未启用通知，触发到达事件
        if (HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED) && !HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED))
        {
            OnArrived(owner); // 触发钩子和等待定时器重置
            AddFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED); // 通知未来的 StartMove 已到达节点
        }

        // 如果 OnArrived 未设置定时器，开始下一次移动
        if (_nextMoveTime.Passed())
            StartMove(owner); // 检查路径状态，获取下一节点并在可能时移动
    }

    return true;
}

/**
 * @brief 停用路点移动生成器
 *
 * 设置停用标志并清除漫游移动状态。
 *
 * @param owner 拥有此移动生成器的生物
 */
void WaypointMovementGenerator<Creature>::DoDeactivate(Creature* owner)
{
    AddFlag(MOVEMENTGENERATOR_FLAG_DEACTIVATED);
    owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
}

/**
 * @brief 结束路点移动生成器
 *
 * 清理状态并停止移动。
 *
 * @param owner 拥有此移动生成器的生物
 * @param active 是否处于活跃状态
 * @param movementInform 移动完成通知标志（未使用）
 */
void WaypointMovementGenerator<Creature>::DoFinalize(Creature* owner, bool active, bool/* movementInform*/)
{
    AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);

    if (active)
    {
        // 清除漫游移动状态
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);

        // TODO: 研究是否需要此修改，很可能不需要
        // 设置为非行走状态
        owner->SetWalk(false);
    }
}

/**
 * @brief 触发路点移动完成通知
 *
 * 通知生物 AI 当前路点已到达。
 *
 * @param owner 拥有此移动生成器的生物
 */
void WaypointMovementGenerator<Creature>::MovementInform(Creature* owner)
{
    // 调用 AI 的 MovementInform 方法
    if (owner->AI())
        owner->AI()->MovementInform(WAYPOINT_MOTION_TYPE, _currentNode);
}

/**
 * @brief 处理到达路点事件
 *
 * 当生物到达一个路点时调用，执行以下操作：
 * 1. 设置延迟定时器（如果路点有延迟）
 * 2. 触发路点事件脚本（如果配置了事件且概率命中）
 * 3. 通知 AI 路点已到达
 * 4. 更新生物的当前路点信息
 *
 * @param owner 拥有此移动生成器的生物
 */
void WaypointMovementGenerator<Creature>::OnArrived(Creature* owner)
{
    // 检查路径有效性
    if (!_path || _path->nodes.empty())
        return;

    // 断言当前节点索引有效
    ASSERT(_currentNode < _path->nodes.size(), "WaypointMovementGenerator::OnArrived: tried to reference a node id (%u) which is not included in path (%u)", _currentNode, _path->id);

    WaypointNode const& waypoint = _path->nodes[_currentNode];

    // 如果路点有延迟，设置定时器
    if (waypoint.delay)
    {
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        _nextMoveTime.Reset(waypoint.delay);
    }

    // 脚本可能会使当前路径失效，保存需要的信息
    uint32 waypointId = waypoint.id;
    uint32 pathId = _path->id;

    // 触发路点事件（如果配置了事件且概率命中）
    if (waypoint.eventId && urand(0, 99) < waypoint.eventChance)
    {
        TC_LOG_DEBUG("maps.script", "Creature movement start script {} at point {} for {}.", waypoint.eventId, _currentNode, owner->GetGUID().ToString());
        owner->ClearUnitState(UNIT_STATE_ROAMING_MOVE);
        owner->GetMap()->ScriptsStart(sWaypointScripts, waypoint.eventId, owner, nullptr);
    }

    // 通知 AI 路点已到达
    if (CreatureAI* AI = owner->AI())
    {
        AI->MovementInform(WAYPOINT_MOTION_TYPE, _currentNode);
        AI->WaypointReached(waypointId, pathId);
    }

    // 更新生物的当前路点信息
    owner->UpdateCurrentWaypointInfo(waypointId, pathId);
}

/**
 * @brief 开始移动到下一个路点
 *
 * 核心方法，负责：
 * 1. 检查移动前置条件
 * 2. 计算下一个路点
 * 3. 启动 MoveSpline 移动
 * 4. 处理路径完成事件
 * 5. 处理交通工具上的特殊情况
 *
 * 状态处理：
 * - 首次调用：初始化并开始移动到第一个节点
 * - 正常流程：移动到下一节点
 * - 中断恢复：重新启动当前节点的移动
 * - 路径完成：设置家庭位置并通知 AI
 *
 * @param owner 拥有此移动生成器的生物
 * @param relaunch 是否为重新启动（从中断恢复）
 */
void WaypointMovementGenerator<Creature>::StartMove(Creature* owner, bool relaunch/* = false*/)
{
    // 完整性检查
    if (!owner || !owner->IsAlive() || HasFlag(MOVEMENTGENERATOR_FLAG_FINALIZED) || !_path || _path->nodes.empty() || (relaunch && (HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED) || !HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED))))
        return;

    // 检查是否可以移动（状态、施法、编队限制）
    if (owner->HasUnitState(UNIT_STATE_NOT_MOVE) || owner->IsMovementPreventedByCasting() || (owner->IsFormationLeader() && !owner->IsFormationLeaderMoveAllowed()))
    {
        _nextMoveTime.Reset(1000); // 延迟 1 秒后重试
        return;
    }

    // 检查是否在交通工具上
    bool const transportPath = owner->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT) && !owner->GetTransGUID().IsEmpty();

    // 如果已到达节点（INFORM_ENABLED）且已初始化
    if (HasFlag(MOVEMENTGENERATOR_FLAG_INFORM_ENABLED) && HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED))
    {
        // 计算下一个节点
        if (ComputeNextNode())
        {
            // 成功计算下一节点
            ASSERT(_currentNode < _path->nodes.size(), "WaypointMovementGenerator::StartMove: tried to reference a node id (%u) which is not included in path (%u)", _currentNode, _path->id);

            // 通知 AI 开始移动到新节点
            if (CreatureAI* AI = owner->AI())
                AI->WaypointStarted(_path->nodes[_currentNode].id, _path->id);
        }
        else
        {
            // 路径已完成（非循环路径已到达终点）
            WaypointNode const &waypoint = _path->nodes[_currentNode];
            float x = waypoint.x;
            float y = waypoint.y;
            float z = waypoint.z;
            float o = owner->GetOrientation();

            // 设置家庭位置
            if (!transportPath)
            {
                owner->SetHomePosition(x, y, z, o);
            }
            else
            {
                // 在交通工具上，需要转换坐标
                if (Transport* trans = owner->GetTransport())
                {
                    o -= trans->GetOrientation();
                    owner->SetTransportHomePosition(x, y, z, o);
                    trans->CalculatePassengerPosition(x, y, z, &o);
                    owner->SetHomePosition(x, y, z, o);
                }
                // else if (vehicle) - 这不应该发生，载具偏移是常量
            }

            // 标记为已结束
            AddFlag(MOVEMENTGENERATOR_FLAG_FINALIZED);
            owner->UpdateCurrentWaypointInfo(0, 0);

            // 通知 AI 路径已完成
            if (CreatureAI* AI = owner->AI())
                AI->WaypointPathEnded(waypoint.id, _path->id);
            return;
        }
    }
    else if (!HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED))
    {
        // 首次初始化
        AddFlag(MOVEMENTGENERATOR_FLAG_INITIALIZED);

        // 通知 AI 开始移动到第一个节点
        if (CreatureAI* AI = owner->AI())
            AI->WaypointStarted(_path->nodes[_currentNode].id, _path->id);
    }

    // 获取当前路点
    ASSERT(_currentNode < _path->nodes.size(), "WaypointMovementGenerator::StartMove: tried to reference a node id (%u) which is not included in path (%u)", _currentNode, _path->id);
    WaypointNode const &waypoint = _path->nodes[_currentNode];

    // 清除临时标志
    RemoveFlag(MOVEMENTGENERATOR_FLAG_TRANSITORY | MOVEMENTGENERATOR_FLAG_INFORM_ENABLED | MOVEMENTGENERATOR_FLAG_TIMED_PAUSED);

    // 设置漫游移动状态
    owner->AddUnitState(UNIT_STATE_ROAMING_MOVE);

    // 初始化 MoveSpline
    Movement::MoveSplineInit init(owner);

    // 如果生物在交通工具上，数据库中的路点已经是交通工具偏移
    if (transportPath)
        init.DisableTransportPathTransformations();

    // 设置目标位置
    // 注意：不要使用 formationDest，MoveTo 需要交通工具偏移（因为调用了 DisableTransportPathTransformations）
    // 但 formationDest 包含全局坐标
    init.MoveTo(waypoint.x, waypoint.y, waypoint.z);

    // 设置朝向（如果指定且有延迟）
    if (waypoint.orientation.has_value() && waypoint.delay > 0)
        init.SetFacing(*waypoint.orientation);

    // 设置移动类型
    switch (waypoint.moveType)
    {
        case WAYPOINT_MOVE_TYPE_LAND:
            init.SetAnimation(AnimTier::Ground); // 着陆动画
            break;
        case WAYPOINT_MOVE_TYPE_TAKEOFF:
            init.SetAnimation(AnimTier::Hover); // 起飞/悬停动画
            break;
        case WAYPOINT_MOVE_TYPE_RUN:
            init.SetWalk(false); // 奔跑
            break;
        case WAYPOINT_MOVE_TYPE_WALK:
            init.SetWalk(true); // 行走
            break;
        default:
            break;
    }

    // 启动移动
    init.Launch();

    // 通知编队更新移动状态
    owner->SignalFormationMovement();
}

/**
 * @brief 计算下一个路点节点
 *
 * 更新当前路点索引到下一个节点。
 *
 * 逻辑：
 * - 如果当前是最后一个节点且不循环，返回 false
 * - 否则，将索引前进到下一个节点（循环路径会回到起点）
 *
 * @return true 表示成功计算下一个节点，false 表示路径已完成
 */
bool WaypointMovementGenerator<Creature>::ComputeNextNode()
{
    // 如果是最后一个节点且不循环，路径完成
    if ((_currentNode == _path->nodes.size() - 1) && !_repeating)
        return false;

    // 前进到下一节点（循环时回到起点）
    _currentNode = (_currentNode + 1) % _path->nodes.size();
    return true;
}

/**
 * @brief 获取调试信息
 *
 * 返回包含路径信息和移动生成器信息的调试字符串。
 *
 * @return 调试信息字符串
 */
std::string WaypointMovementGenerator<Creature>::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << PathMovementBase::GetDebugInfo() << "\n"
        << MovementGeneratorMedium::GetDebugInfo();
    return sstr.str();
}
