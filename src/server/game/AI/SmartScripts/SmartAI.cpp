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
 * @file SmartAI.cpp
 * @brief SmartAI 智能AI系统实现文件
 *
 * 本文件实现了 SmartAI 类的所有成员函数，提供了智能AI系统的核心功能。
 * SmartAI 是一个数据驱动的AI系统，允许通过数据库配置创建复杂的生物行为。
 *
 * 主要实现内容：
 * 1. 构造与初始化：SmartAI实例的创建和初始化逻辑
 * 2. 路径系统：路径加载、启动、暂停、恢复、停止等功能
 * 3. 护送系统：NPC护送任务的完整流程控制
 * 4. 跟随系统：NPC跟随目标的实现
 * 5. 战斗系统：战斗相关的AI行为控制
 * 6. 事件处理：各种游戏事件的响应和分发
 * 7. 状态更新：定期更新的AI逻辑（路径、跟随、消失等）
 *
 * 核心工作流程：
 * - UpdateAI() 是主循环，每帧调用
 * - 各种回调函数由游戏引擎在特定事件发生时调用
 * - SmartScript 对象负责处理具体的事件-动作映射
 *
 * 性能注意事项：
 * - UpdateAI() 每帧都会调用，应保持高效
 * - 路径点检查使用定时器，避免频繁的距离计算
 * - 条件检查只在必要时执行
 */

#include "SmartAI.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Vehicle.h"

/**
 * @brief 构造函数
 *
 * 初始化所有成员变量为安全的默认值。
 * 检查生物是否有载具条件配置，这会影响UpdateAI中的条件检查。
 */
SmartAI::SmartAI(Creature* creature) : CreatureAI(creature), _charmed(false), _followCreditType(0), _followArrivedTimer(0), _followCredit(0), _followArrivedEntry(0), _followDistance(0.f), _followAngle(0.f),
    _escortState(SMART_ESCORT_NONE), _escortNPCFlags(0), _escortInvokerCheckTimer(1000), _currentWaypointNode(0), _waypointReached(false), _waypointPauseTimer(0), _waypointPauseForced(false), _repeatWaypointPath(false),
    _OOCReached(false), _waypointPathEnded(false), _run(true), _evadeDisabled(false), _canAutoAttack(true), _canCombatMove(true), _invincibilityHPLevel(0), _despawnTime(0), _despawnState(0), _vehicleConditionsTimer(0),
    _gossipReturn(false), _escortQuestId(0)
{
    // 检查是否有载具条件配置（用于限制谁能骑乘坐骑）
    _vehicleConditions = sConditionMgr->HasConditionsForNotGroupedEntry(CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE, creature->GetEntry());
}

/**
 * @brief 检查AI是否受控
 * @return 如果AI不受魅惑控制返回true，否则返回false
 *
 * 用于区分生物是自主行动还是被玩家魅惑控制。
 * 被魅惑时，SmartAI的大部分功能会被限制，避免与玩家的控制冲突。
 */
bool SmartAI::IsAIControlled() const
{
    return !_charmed;
}

/**
 * @brief 开始路径移动
 * @param run 是否跑步移动
 * @param pathId 路径ID（0表示使用已加载的路径）
 * @param repeat 是否循环路径
 * @param invoker 触发者（通常是玩家），用于护送任务
 * @param nodeId 起始路径点ID
 *
 * 开始沿指定路径移动。这是SmartAI路径系统的核心入口函数。
 *
 * 执行流程：
 * 1. 如果已有路径在运行，先停止当前路径
 * 2. 设置移动模式（跑步/行走）
 * 3. 如果指定了新路径ID，加载路径数据
 * 4. 检查路径是否有效
 * 5. 初始化路径状态（当前点、结束标志、循环标志）
 * 6. 设置护送状态（直接赋值，清除所有之前的状态）
 * 7. 如果有玩家触发者，保存NPC标志并清除交互标志（防止护送期间被打扰）
 * 8. 启动路径移动
 */
void SmartAI::StartPath(bool run/* = false*/, uint32 pathId/* = 0*/, bool repeat/* = false*/, Unit* invoker/* = nullptr*/, uint32 nodeId/* = 1*/)
{
    // 如果正在护送中，先停止当前路径
    if (HasEscortState(SMART_ESCORT_ESCORTING))
        StopPath();

    // 设置移动模式
    SetRun(run);

    // 加载指定路径（如果提供了路径ID）
    if (pathId)
    {
        if (!LoadPath(pathId))
            return;
    }

    // 检查路径是否有效
    if (_path.nodes.empty())
        return;

    // 初始化路径状态
    _currentWaypointNode = nodeId;
    _waypointPathEnded = false;
    _repeatWaypointPath = repeat;

    // 直接设置护送状态为正在护送（清除所有之前的状态）
    // 注意：不使用AddEscortState，因为我们要完全重置状态
    _escortState = SMART_ESCORT_ESCORTING;

    // 如果有玩家触发者，保存并清除NPC标志
    // 这可以防止护送期间玩家与NPC交互（如打开商店、对话等）
    if (invoker && invoker->GetTypeId() == TYPEID_PLAYER)
    {
        _escortNPCFlags = me->GetNpcFlags();
        me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
    }

    // 启动路径移动
    me->GetMotionMaster()->MovePath(_path, _repeatWaypointPath);
}

/**
 * @brief 加载路径数据
 * @param entry 路径ID（对应waypoint_data表）
 * @return 加载成功返回true，失败返回false
 *
 * 从SmartWaypointMgr加载指定路径的点数据。
 * 会规范化坐标并根据run状态设置每个点的移动类型。
 *
 * 注意事项：
 * - 如果正在护送中，不允许加载新路径
 * - 加载失败会清除脚本中的路径ID
 * - 坐标规范化确保坐标在有效范围内
 */
bool SmartAI::LoadPath(uint32 entry)
{
    // 如果正在护送中，不允许加载新路径
    if (HasEscortState(SMART_ESCORT_ESCORTING))
        return false;

    // 从路径管理器获取路径数据
    WaypointPath const* path = sSmartWaypointMgr->GetPath(entry);
    if (!path || path->nodes.empty())
    {
        GetScript()->SetPathId(0);
        return false;
    }

    // 复制路径数据
    _path.id = path->id;
    _path.nodes = path->nodes;

    // 规范化每个路径点的坐标并设置移动类型
    for (WaypointNode& waypoint : _path.nodes)
    {
        // 规范化坐标，确保在地图有效范围内
        Trinity::NormalizeMapCoord(waypoint.x);
        Trinity::NormalizeMapCoord(waypoint.y);
        // 根据run状态设置移动类型
        waypoint.moveType = _run ? WAYPOINT_MOVE_TYPE_RUN : WAYPOINT_MOVE_TYPE_WALK;
    }

    // 更新脚本中的路径ID
    GetScript()->SetPathId(entry);
    return true;
}

/**
 * @brief 暂停路径移动
 * @param delay 暂停时间（毫秒）
 * @param forced 是否强制暂停（立即停止移动并更新出生点）
 *
 * 暂停当前的路径移动，触发暂停事件。
 *
 * 两种暂停模式：
 * 1. 强制暂停（forced=true）：立即停止移动，更新出生点到当前位置
 * 2. 普通暂停（forced=false）：等待到达当前路径点后再暂停
 *
 * 如果不在护送状态，会暂停普通移动并触发事件。
 * 如果已经在暂停状态，会记录错误并忽略。
 */
void SmartAI::PausePath(uint32 delay, bool forced)
{
    // 如果不在护送状态，处理普通移动暂停
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
    {
        me->PauseMovement(delay, MOTION_SLOT_DEFAULT, forced);
        // 如果当前是路径移动，触发暂停事件
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
        {
            std::pair<uint32, uint32> waypointInfo = me->GetCurrentWaypointInfo();
            GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_PAUSED, nullptr, waypointInfo.first, waypointInfo.second);
        }
        return;
    }

    // 如果已经在暂停状态，记录错误并忽略
    if (HasEscortState(SMART_ESCORT_PAUSED))
    {
        TC_LOG_ERROR("scripts.ai.sai", "SmartAI::PausePath: Creature wanted to pause waypoint (current waypoint: {}) movement while already paused, ignoring. ({})", _currentWaypointNode, me->GetGUID().ToString());
        return;
    }

    // 设置暂停计时器
    _waypointPauseTimer = delay;

    // 处理强制暂停
    if (forced)
    {
        _waypointPauseForced = forced;
        SetRun(_run);
        me->PauseMovement();
        // 更新出生点到当前位置，以便战斗后返回
        me->SetHomePosition(me->GetPosition());
    }
    else
    {
        // 普通暂停：等待到达当前路径点
        _waypointReached = false;
    }

    // 添加暂停状态并触发事件
    AddEscortState(SMART_ESCORT_PAUSED);
    GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_PAUSED, nullptr, _currentWaypointNode, GetScript()->GetPathId());
}

/**
 * @brief 检查是否可以恢复路径移动
 * @return 如果可以恢复返回true，否则返回false
 *
 * 检查当前是否处于暂停状态且护送正在进行中。
 * 只有同时满足这两个条件才能恢复移动。
 */
bool SmartAI::CanResumePath()
{
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
    {
        // 恢复逻辑不支持非护送状态
        return false;
    }

    return HasEscortState(SMART_ESCORT_PAUSED);
}

/**
 * @brief 停止路径移动
 * @param DespawnTime 消失时间（毫秒），0表示不消失
 * @param quest 关联任务ID，用于任务失败判定
 * @param fail 是否标记为失败
 *
 * 停止当前的路径移动，触发相关事件，并根据参数决定是否消失。
 *
 * 处理两种情况：
 * 1. 非护送状态：停止普通路径移动，触发停止和结束事件
 * 2. 护送状态：停止护送路径，调用EndPath处理任务完成/失败
 */
void SmartAI::StopPath(uint32 DespawnTime, uint32 quest, bool fail)
{
    // 处理非护送状态的路径停止
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
    {
        std::pair<uint32, uint32> waypointInfo = { 0, 0 };
        // 获取当前路径点信息（如果是路径移动）
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
            waypointInfo = me->GetCurrentWaypointInfo();

        // 设置消失时间（如果还没开始消失过程）
        if (_despawnState != 2)
            SetDespawnTime(DespawnTime);

        // 停止移动
        me->GetMotionMaster()->MoveIdle();

        // 触发停止事件
        if (waypointInfo.first)
            GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_STOPPED, nullptr, waypointInfo.first, waypointInfo.second);

        // 如果不是失败，触发结束事件并可能开始消失
        if (!fail)
        {
            if (waypointInfo.first)
                GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_ENDED, nullptr, waypointInfo.first, waypointInfo.second);
            if (_despawnState == 1)
                StartDespawn();
        }
        return;
    }

    // 处理护送状态的路径停止
    if (quest)
        _escortQuestId = quest;

    // 设置消失时间
    if (_despawnState != 2)
        SetDespawnTime(DespawnTime);

    // 停止移动
    me->GetMotionMaster()->MoveIdle();

    // 触发停止事件
    GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_STOPPED, nullptr, _currentWaypointNode, GetScript()->GetPathId());

    // 结束路径
    EndPath(fail);
}

/**
 * @brief 结束路径
 * @param fail 是否标记为失败
 *
 * 清理护送状态，处理任务完成/失败逻辑，触发结束事件。
 *
 * 主要处理流程：
 * 1. 清除所有护送状态标志
 * 2. 清空路径数据
 * 3. 恢复NPC标志（如果之前保存了）
 * 4. 处理护送任务的完成/失败
 * 5. 触发路径结束事件
 * 6. 如果设置了循环，重新开始路径
 * 7. 如果设置了消失，开始消失过程
 */
void SmartAI::EndPath(bool fail)
{
    // 清除所有护送相关状态
    RemoveEscortState(SMART_ESCORT_ESCORTING | SMART_ESCORT_PAUSED | SMART_ESCORT_RETURNING);
    _path.nodes.clear();
    _waypointPauseTimer = 0;

    // 恢复NPC标志（如果之前保存了）
    if (_escortNPCFlags)
    {
        me->ReplaceAllNpcFlags((NPCFlags)_escortNPCFlags);
        _escortNPCFlags = 0;
    }

    // 获取护送目标列表
    ObjectVector const* targets = GetScript()->GetStoredTargetVector(SMART_ESCORT_TARGETS, *me);
    if (targets && _escortQuestId)
    {
        // 处理单个玩家触发者（通常是护送任务的接受者）
        if (targets->size() == 1 && GetScript()->IsPlayer((*targets->begin())))
        {
            Player* player = targets->front()->ToPlayer();
            // 如果成功且玩家在奖励范围内，完成任务
            if (!fail && player->IsAtGroupRewardDistance(me) && !player->HasCorpse())
                player->GroupEventHappens(_escortQuestId, me);

            // 如果失败，任务失败
            if (fail)
                player->FailQuest(_escortQuestId);

            // 处理队友的任务状态
            if (Group* group = player->GetGroup())
            {
                for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
                {
                    Player* groupGuy = groupRef->GetSource();
                    if (!groupGuy->IsInMap(player))
                        continue;

                    // 队友在范围内也能获得任务进度
                    if (!fail && groupGuy->IsAtGroupRewardDistance(me) && !groupGuy->HasCorpse())
                        groupGuy->AreaExploredOrEventHappens(_escortQuestId);
                    else if (fail)
                        groupGuy->FailQuest(_escortQuestId);
                }
            }
        }
        else
        {
            // 处理多个目标的情况
            for (WorldObject* target : *targets)
            {
                if (GetScript()->IsPlayer(target))
                {
                    Player* player = target->ToPlayer();
                    if (!fail && player->IsAtGroupRewardDistance(me) && !player->HasCorpse())
                        player->AreaExploredOrEventHappens(_escortQuestId);
                    else if (fail)
                        player->FailQuest(_escortQuestId);
                }
            }
        }
    }

    // 只有成功结束或被SMART_ACTION_WAYPOINT_STOP停止时才触发结束事件
    if (fail)
        return;

    uint32 pathid = GetScript()->GetPathId();
    GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_ENDED, nullptr, _currentWaypointNode, pathid);

    // 如果设置了循环且AI受控，重新开始路径
    if (_repeatWaypointPath)
    {
        if (IsAIControlled())
            StartPath(_run, GetScript()->GetPathId(), _repeatWaypointPath);
    }
    else if (pathid == GetScript()->GetPathId()) // 如果路径ID没变，清除路径ID；否则脚本可能要启动新路径
        GetScript()->SetPathId(0);

    // 如果设置了消失，开始消失过程
    if (_despawnState == 1)
        StartDespawn();
}

/**
 * @brief 恢复路径移动
 *
 * 从暂停状态恢复路径移动。
 * 触发恢复事件，清除暂停状态，重置相关计时器，恢复移动。
 */
void SmartAI::ResumePath()
{
    // 触发路径恢复事件
    GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_RESUMED, nullptr, _currentWaypointNode, GetScript()->GetPathId());

    // 清除暂停状态
    RemoveEscortState(SMART_ESCORT_PAUSED);

    // 重置暂停相关变量
    _waypointPauseForced = false;
    _waypointReached = false;
    _waypointPauseTimer = 0;

    // 恢复移动
    SetRun(_run);
    me->ResumeMovement();
}

/**
 * @brief 返回最后脱战位置
 *
 * 在战斗结束后，生物移动到最后一次脱战的位置。
 * 只有AI受控时才会执行。
 */
void SmartAI::ReturnToLastOOCPos()
{
    if (!IsAIControlled())
        return;

    // 设置为跑步模式并移动到出生点
    me->SetWalk(false);
    me->GetMotionMaster()->MovePoint(SMART_ESCORT_LAST_OOC_POINT, me->GetHomePosition());
}

/**
 * @brief 更新AI
 * @param diff 距上次更新的时间差（毫秒）
 *
 * 这是SmartAI的主循环函数，每个世界更新周期调用一次。
 *
 * 执行顺序：
 * 1. 检查生物是否存活
 * 2. 检查载具条件
 * 3. 更新战斗目标
 * 4. 更新SmartScript
 * 5. 更新路径、跟随、消失状态
 * 6. 如果有战斗目标且允许自动攻击，执行近战攻击
 *
 * 性能注意事项：
 * - 此函数每帧调用，应保持高效
 * - 各个子更新函数内部有状态检查，避免不必要的计算
 */
void SmartAI::UpdateAI(uint32 diff)
{
    // 如果生物已死亡，结束战斗状态并返回
    if (!me->IsAlive())
    {
        if (IsEngaged())
            EngagementOver();
        return;
    }

    // 检查载具条件（限制谁能骑乘坐骑）
    CheckConditions(diff);

    // 更新战斗目标
    bool hasVictim = UpdateVictim();

    // 更新SmartScript（处理事件和计时器）
    GetScript()->OnUpdate(diff);

    // 更新路径、跟随、消失状态
    UpdatePath(diff);
    UpdateFollow(diff);
    UpdateDespawn(diff);

    // 如果AI不受控（被魅惑），不执行攻击逻辑
    if (!IsAIControlled())
        return;

    // 如果没有战斗目标，不执行攻击逻辑
    if (!hasVictim)
        return;

    // 如果允许自动攻击，执行近战攻击
    if (_canAutoAttack)
        DoMeleeAttackIfReady();
}

/**
 * @brief 检查护送触发者是否在范围内
 * @return 如果在范围内返回true，否则返回false
 *
 * 检查护送任务的触发玩家是否在最大距离内。
 * 在副本中，允许距离是普通情况的两倍（更宽容）。
 * 会检查队伍中所有成员，只要有一个在范围内就返回true。
 *
 * @note 如果没有存储触发者，总是返回true（跳过范围检查）
 */
bool SmartAI::IsEscortInvokerInRange()
{
    if (ObjectVector const* targets = GetScript()->GetStoredTargetVector(SMART_ESCORT_TARGETS, *me))
    {
        // 副本中允许距离加倍
        float checkDist = me->GetInstanceScript() ? SMART_ESCORT_MAX_PLAYER_DIST * 2 : SMART_ESCORT_MAX_PLAYER_DIST;

        // 处理单个玩家触发者
        if (targets->size() == 1 && GetScript()->IsPlayer((*targets->begin())))
        {
            Player* player = (*targets->begin())->ToPlayer();
            if (me->GetDistance(player) <= checkDist)
                return true;

            // 检查队伍成员
            if (Group* group = player->GetGroup())
            {
                for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
                {
                    Player* groupGuy = groupRef->GetSource();
                    if (groupGuy->IsInMap(player) && me->GetDistance(groupGuy) <= checkDist)
                        return true;
                }
            }
        }
        else
        {
            // 处理多个目标的情况
            for (WorldObject* target : *targets)
            {
                if (GetScript()->IsPlayer(target))
                {
                    if (me->GetDistance(target->ToPlayer()) <= checkDist)
                        return true;
                }
            }
        }

        // 没有找到有效目标
        return false;
    }

    // 没有存储玩家触发者，跳过范围检查
    return true;
}

/**
 * @brief 路径点到达回调
 * @param nodeId 到达的路径点ID
 * @param pathId 路径ID
 *
 * 当生物到达路径点时调用。
 * 触发到达事件，并根据设置决定是否暂停移动。
 */
void SmartAI::WaypointReached(uint32 nodeId, uint32 pathId)
{
    // 如果不在护送状态，直接触发事件
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
    {
        GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_REACHED, nullptr, nodeId, pathId);
        return;
    }

    // 更新当前路径点ID
    _currentWaypointNode = nodeId;

    // 触发路径点到达事件
    GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_REACHED, nullptr, _currentWaypointNode, pathId);

    // 如果设置了暂停计时器且非强制暂停，在此处暂停
    if (_waypointPauseTimer && !_waypointPauseForced)
    {
        _waypointReached = true;
        me->PauseMovement();
        // 更新出生点，以便战斗后返回
        me->SetHomePosition(me->GetPosition());
    }
    else if (HasEscortState(SMART_ESCORT_ESCORTING) && me->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
    {
        // 检查是否到达路径终点
        if (_currentWaypointNode == _path.nodes.size())
            _waypointPathEnded = true;
        else
            SetRun(_run); // 确保移动模式正确
    }
}

/**
 * @brief 路径结束回调
 * @param nodeId 最后的路径点ID
 * @param pathId 路径ID
 *
 * 当路径的所有点都走完时调用。
 * 触发结束事件。
 *
 * @todo 需要将护送相关逻辑移到更合适的地方
 */
///@todo move escort related logic
void SmartAI::WaypointPathEnded(uint32 nodeId, uint32 pathId)
{
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
    {
        GetScript()->ProcessEventsFor(SMART_EVENT_WAYPOINT_ENDED, nullptr, nodeId, pathId);
        return;
    }
}

/**
 * @brief 移动信息回调
 * @param type 移动类型
 * @param id 移动数据ID
 *
 * 当路径点到达或点移动完成时调用。
 * 处理返回脱战位置的特殊情况。
 * 触发 SMART_EVENT_MOVEMENTINFORM 事件。
 */
void SmartAI::MovementInform(uint32 type, uint32 id)
{
    // 如果是返回脱战位置移动完成，清除逃避状态
    if (type == POINT_MOTION_TYPE && id == SMART_ESCORT_LAST_OOC_POINT)
        me->ClearUnitState(UNIT_STATE_EVADE);

    // 触发移动信息事件
    GetScript()->ProcessEventsFor(SMART_EVENT_MOVEMENTINFORM, nullptr, type, id);

    // 处理护送状态
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
        return;

    // 如果是返回脱战位置移动完成，标记已到达
    if (type == POINT_MOTION_TYPE && id == SMART_ESCORT_LAST_OOC_POINT)
        _OOCReached = true;
}

/**
 * @brief 进入逃避模式回调
 * @param why 逃避原因
 *
 * 当生物脱战时调用。
 * 根据当前状态决定返回出生点、继续护送或跟随目标。
 */
void SmartAI::EnterEvadeMode(EvadeReason /*why*/)
{
    // 如果逃避被禁用，仅触发事件
    if (_evadeDisabled)
    {
        GetScript()->ProcessEventsFor(SMART_EVENT_EVADE);
        return;
    }

    // 如果AI不受控，仅停止攻击
    if (!IsAIControlled())
    {
        me->AttackStop();
        return;
    }

    // 调用基类的逃避模式进入函数
    if (!_EnterEvadeMode())
        return;

    // 添加逃避状态
    me->AddUnitState(UNIT_STATE_EVADE);

    // 触发逃避事件（必须在_EnterEvadeMode之后，因为会清除法术和光环）
    GetScript()->ProcessEventsFor(SMART_EVENT_EVADE);

    // 恢复移动模式
    SetRun(_run);

    // 根据不同情况选择逃避后的行为
    if (Unit* owner = me->GetCharmerOrOwner())
    {
        // 有主人，跟随主人（如宠物）
        me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
        me->ClearUnitState(UNIT_STATE_EVADE);
    }
    else if (HasEscortState(SMART_ESCORT_ESCORTING))
    {
        // 正在护送中，返回脱战位置
        AddEscortState(SMART_ESCORT_RETURNING);
        ReturnToLastOOCPos();
    }
    else if (Unit* target = _followGUID ? ObjectAccessor::GetUnit(*me, _followGUID) : nullptr)
    {
        // 正在跟随某目标，继续跟随
        me->GetMotionMaster()->MoveFollow(target, _followDistance, _followAngle);
        // MoveFollow不会清除逃避状态，所以需要手动清除
        me->ClearUnitState(UNIT_STATE_EVADE);
    }
    else
    {
        // 默认行为：返回出生点
        me->GetMotionMaster()->MoveTargetedHome();
    }

    // 如果没有逃避状态，重置脚本
    if (!me->HasUnitState(UNIT_STATE_EVADE))
        GetScript()->OnReset();
}

/**
 * @brief 视线内移动回调
 * @param who 进入视线范围的单位
 *
 * 当单位进入生物视线范围时调用。
 * 如果在护送中，会尝试协助玩家战斗。
 */
void SmartAI::MoveInLineOfSight(Unit* who)
{
    if (!who)
        return;

    // 通知脚本有单位进入视线
    GetScript()->OnMoveInLineOfSight(who);

    // 如果AI不受控，不执行额外逻辑
    if (!IsAIControlled())
        return;

    // 如果在护送中，尝试协助玩家战斗
    if (HasEscortState(SMART_ESCORT_ESCORTING) && AssistPlayerInCombatAgainst(who))
        return;

    // 调用基类实现
    CreatureAI::MoveInLineOfSight(who);
}

/**
 * @brief 协助玩家战斗
 * @param who 潜在的敌对目标
 * @return 如果协助成功返回true，否则返回false
 *
 * 在护送过程中检查是否应该协助玩家攻击敌对目标。
 * 只有满足以下所有条件才会协助：
 * - 生物不是被动反应状态
 * - AI受控（未被魅惑）
 * - 有有效的敌对目标
 * - 敌对目标正在攻击玩家
 * - 生物类型标记允许协助
 * - 敌对目标在可到达的位置
 * - 可以攻击该目标
 * - 双方都不在逃避模式
 * - 玩家是有效的协助目标
 * - 距离和视线符合要求
 */
bool SmartAI::AssistPlayerInCombatAgainst(Unit* who)
{
    // 检查基本条件
    if (me->HasReactState(REACT_PASSIVE) || !IsAIControlled())
        return false;

    if (!who || !who->GetVictim())
        return false;

    // 检查生物类型标志（是否允许协助）
    if (!(me->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_CAN_ASSIST))
        return false;

    // 检查受害者是否是玩家
    if (!who->EnsureVictim()->GetCharmerOrOwnerPlayerOrPlayerItself())
        return false;

    // 检查敌对目标是否在可到达的位置
    if (!who->isInAccessiblePlaceFor(me))
        return false;

    // 检查是否可以攻击该目标
    if (!CanAIAttack(who))
        return false;

    // 不能在逃避模式下攻击
    if (me->IsInEvadeMode())
        return false;

    // 敌人也不能在逃避模式下
    if (who->GetTypeId() == TYPEID_UNIT && who->ToCreature()->IsInEvadeMode())
        return false;

    // 检查受害者是否是有效的协助目标
    if (!me->IsValidAssistTarget(who->GetVictim()))
        return false;

    // 检查距离和视线
    if (me->IsWithinDistInMap(who, SMART_MAX_AID_DIST) && me->IsWithinLOSInMap(who))
    {
        me->EngageWithTarget(who);
        return true;
    }

    return false;
}

/**
 * @brief 初始化AI回调
 *
 * 当AI首次初始化时调用。
 * 初始化SmartScript，重置所有状态变量。
 * 注意：跟随相关变量不完全重置，因为战斗逃避后需要恢复跟随。
 */
void SmartAI::InitializeAI()
{
    // 初始化SmartScript
    GetScript()->OnInitialize(me);

    // 重置所有状态变量
    _despawnTime = 0;
    _despawnState = 0;
    _escortState = SMART_ESCORT_NONE;
    _followGUID.Clear(); // 不在Reset()中重置跟随者，战斗逃避后需要它
    _followDistance = 0;
    _followAngle = 0;
    _followCredit = 0;
    _followArrivedTimer = 1000;
    _followArrivedEntry = 0;
    _followCreditType = 0;
}

/**
 * @brief 出现回调
 *
 * 当生物完全添加到世界时调用。
 * 触发重生事件并重置脚本。
 */
void SmartAI::JustAppeared()
{
    CreatureAI::JustAppeared();

    // 如果生物已死亡，不处理
    if (me->isDead())
        return;

    // 触发重生事件
    GetScript()->ProcessEventsFor(SMART_EVENT_RESPAWN);
    // 重置脚本状态
    GetScript()->OnReset();
}

/**
 * @brief 到达出生点回调
 *
 * 在生物逃避后返回出生点时调用。
 * 重置AI状态并恢复默认行为（如路径巡逻）。
 */
void SmartAI::JustReachedHome()
{
    // 重置脚本并触发到达出生点事件
    GetScript()->OnReset();
    GetScript()->ProcessEventsFor(SMART_EVENT_REACHED_HOME);

    // 检查生物编队
    CreatureGroup* formation = me->GetFormation();
    if (!formation || formation->GetLeader() == me || !formation->IsFormed())
    {
        // 如果不是编队成员或是编队队长，恢复默认行为
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType(MOTION_SLOT_DEFAULT) != WAYPOINT_MOTION_TYPE)
        {
            // 如果有预设路径，开始巡逻
            if (me->GetWaypointPath())
                me->GetMotionMaster()->MovePath(me->GetWaypointPath(), true);
        }

        // 恢复移动
        me->ResumeMovement();
    }
    else if (formation->IsFormed())
    {
        // 如果是编队成员，等待队长的指令
        me->GetMotionMaster()->MoveIdle();
    }
}

/**
 * @brief 进入战斗回调
 * @param enemy 敌对目标（可能为nullptr）
 *
 * 当生物首次进入战斗时调用。
 * 如果AI受控，会中断非近战法术，然后触发进入战斗事件。
 */
void SmartAI::JustEngagedWith(Unit* enemy)
{
    // 如果AI受控，中断非近战法术（必须在触发事件之前）
    if (IsAIControlled())
        me->InterruptNonMeleeSpells(false);

    // 触发进入战斗事件
    GetScript()->ProcessEventsFor(SMART_EVENT_AGGRO, enemy);
}

/**
 * @brief 死亡回调
 * @param killer 击杀者
 *
 * 当生物死亡时调用。
 * 如果在护送中，会结束护送路径（失败）。
 * 触发死亡事件。
 */
void SmartAI::JustDied(Unit* killer)
{
    // 如果在护送中，结束护送（失败）
    if (HasEscortState(SMART_ESCORT_ESCORTING))
        EndPath(true);

    // 触发死亡事件
    GetScript()->ProcessEventsFor(SMART_EVENT_DEATH, killer);
}

/**
 * @brief 击杀单位回调
 * @param victim 被击杀的单位
 *
 * 当生物击杀其他单位时调用。
 * 触发击杀事件。
 */
void SmartAI::KilledUnit(Unit* victim)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_KILL, victim);
}

/**
 * @brief 召唤生物回调
 * @param creature 被召唤的生物
 *
 * 当生物成功召唤其他生物时调用。
 * 触发召唤事件。
 */
void SmartAI::JustSummoned(Creature* creature)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMONED_UNIT, creature);
}

/**
 * @brief 召唤物死亡回调
 * @param summon 死亡的召唤物
 * @param killer 击杀者
 *
 * 当被召唤的单位死亡时调用。
 * 触发召唤物死亡事件。
 */
void SmartAI::SummonedCreatureDies(Creature* summon, Unit* /*killer*/)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMONED_UNIT_DIES, summon);
}

/**
 * @brief 开始攻击
 * @param who 攻击目标
 *
 * 指示生物攻击并追击目标。
 * 如果AI不受控（被魅惑），仅设置攻击目标不移动。
 * 如果允许战斗移动，会追击目标。
 */
void SmartAI::AttackStart(Unit* who)
{
    // 不允许被魅惑的NPC自主行动
    if (!IsAIControlled())
    {
        if (who)
            me->Attack(who, _canAutoAttack);
        return;
    }

    // 尝试攻击目标
    if (who && me->Attack(who, _canAutoAttack))
    {
        // 清除普通优先级的移动并暂停移动
        me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);
        me->PauseMovement();

        // 如果允许战斗移动，追击目标
        if (_canCombatMove)
        {
            SetRun(_run);
            me->GetMotionMaster()->MoveChase(who);
        }
    }
}

/**
 * @brief 被法术命中回调
 * @param caster 施法者
 * @param spellInfo 法术信息
 *
 * 当生物被法术命中时调用。
 * 触发法术命中事件。
 */
void SmartAI::SpellHit(WorldObject* caster, SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SPELLHIT, caster->ToUnit(), 0, 0, false, spellInfo, caster->ToGameObject());
}

/**
 * @brief 法术命中目标回调
 * @param target 目标对象
 * @param spellInfo 法术信息
 *
 * 当生物施放的法术命中目标时调用。
 * 触发法术命中目标事件。
 */
void SmartAI::SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SPELLHIT_TARGET, target->ToUnit(), 0, 0, false, spellInfo, target->ToGameObject());
}

/**
 * @brief 法术施放完成回调
 * @param spellInfo 法术信息
 *
 * 当法术施放完成时调用。
 * 触发法术施放事件。
 */
void SmartAI::OnSpellCast(SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ON_SPELL_CAST, nullptr, 0, 0, false, spellInfo);
}

/**
 * @brief 法术施放失败回调
 * @param spellInfo 法术信息
 *
 * 当法术施放失败时调用。
 * 触发法术失败事件。
 */
void SmartAI::OnSpellFailed(SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ON_SPELL_FAILED, nullptr, 0, 0, false, spellInfo);
}

/**
 * @brief 法术开始施放回调
 * @param spellInfo 法术信息
 *
 * 当法术开始施放时调用。
 * 触发法术开始事件。
 */
void SmartAI::OnSpellStart(SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ON_SPELL_START, nullptr, 0, 0, false, spellInfo);
}

/**
 * @brief 受到伤害回调
 * @param doneBy 伤害来源
 * @param damage 伤害值（可修改）
 * @param damageType 伤害类型
 * @param spellInfo 法术信息（可能为nullptr）
 *
 * 当生物受到伤害时调用（伤害应用前）。
 * 触发受伤事件。
 * 如果设置了无敌HP等级，会限制伤害不超过该值。
 */
void SmartAI::DamageTaken(Unit* doneBy, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/)
{
    // 触发受伤事件
    GetScript()->ProcessEventsFor(SMART_EVENT_DAMAGED, doneBy, damage);

    // 如果AI不受控，不应用无敌逻辑（防止玩家利用无敌单位）
    if (!IsAIControlled())
        return;

    // 如果设置了无敌HP等级，限制伤害
    if (_invincibilityHPLevel && (damage >= me->GetHealth() - _invincibilityHPLevel))
        damage = me->GetHealth() - _invincibilityHPLevel; // 伤害不应完全抵消，因为有玩家伤害需求
}

/**
 * @brief 接受治疗回调
 * @param doneBy 治疗来源
 * @param addhealth 治疗量
 *
 * 当生物接受治疗时调用。
 * 触发接受治疗事件。
 */
void SmartAI::HealReceived(Unit* doneBy, uint32& addhealth)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_RECEIVE_HEAL, doneBy, addhealth);
}

/**
 * @brief 接收表情回调
 * @param player 发送表情的玩家
 * @param textEmote 表情ID
 *
 * 当玩家对生物做表情时调用。
 * 触发接收表情事件。
 */
void SmartAI::ReceiveEmote(Player* player, uint32 textEmote)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_RECEIVE_EMOTE, player, textEmote);
}

/**
 * @brief 被召唤回调
 * @param summoner 召唤者
 *
 * 当生物被其他单位召唤时调用。
 * 触发被召唤事件。
 */
void SmartAI::IsSummonedBy(WorldObject* summoner)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_JUST_SUMMONED, summoner->ToUnit(), 0, 0, false, nullptr, summoner->ToGameObject());
}

/**
 * @brief 造成伤害回调
 * @param doneTo 受害者
 * @param damage 伤害值
 * @param damagetype 伤害类型
 *
 * 当生物对其他单位造成伤害时调用（伤害应用前）。
 * 触发造成伤害事件。
 */
void SmartAI::DamageDealt(Unit* doneTo, uint32& damage, DamageEffectType /*damagetype*/)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_DAMAGED_TARGET, doneTo, damage);
}

/**
 * @brief 召唤物消失回调
 * @param unit 消失的召唤物
 *
 * 当召唤的生物消失时调用。
 * 触发召唤物消失事件。
 */
void SmartAI::SummonedCreatureDespawn(Creature* unit)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMON_DESPAWNED, unit, unit->GetEntry());
}

/**
 * @brief 尸体移除回调
 * @param respawnDelay 重生延迟（可修改）
 *
 * 当生物尸体被移除时调用。
 * 触发尸体移除事件。
 */
void SmartAI::CorpseRemoved(uint32& respawnDelay)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_CORPSE_REMOVED, nullptr, respawnDelay);
}

/**
 * @brief 消失回调
 *
 * 当生物即将从世界中移除时调用（消失、网格卸载、尸体消失）。
 * 触发消失事件。
 */
void SmartAI::OnDespawn()
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ON_DESPAWN);
}

/**
 * @brief 乘客登载回调
 * @param who 乘客单位
 * @param seatId 座位ID
 * @param apply true=登载，false=离开
 *
 * 当玩家或生物进入/离开载具时调用。
 * 触发相应的事件。
 */
void SmartAI::PassengerBoarded(Unit* who, int8 seatId, bool apply)
{
    GetScript()->ProcessEventsFor(apply ? SMART_EVENT_PASSENGER_BOARDED : SMART_EVENT_PASSENGER_REMOVED, who, uint32(seatId), 0, apply);
}

/**
 * @brief 被魅惑回调
 * @param isNew 是否是新魅惑
 *
 * 当生物被魅惑或魅惑解除时调用。
 * 处理护送路径的停止和恢复，以及魅惑后的行为。
 */
void SmartAI::OnCharmed(bool isNew)
{
    bool const charmed = me->IsCharmed();

    // 如果被魅惑，在改变魅惑状态之前处理（因为魅惑状态可能阻止某些处理）
    if (charmed)
    {
        // 如果在护送中，结束护送（失败）
        if (HasEscortState(SMART_ESCORT_ESCORTING | SMART_ESCORT_PAUSED | SMART_ESCORT_RETURNING))
            EndPath(true);
    }

    // 更新魅惑状态
    _charmed = charmed;

    // 如果被魅惑但未被附身且不是载具，跟随魅惑者
    if (charmed && !me->isPossessed() && !me->IsVehicle())
        me->GetMotionMaster()->MoveFollow(me->GetCharmer(), PET_FOLLOW_DIST, me->GetFollowAngle());

    // 如果魅惑解除且不在逃避模式
    if (!charmed && !me->IsInEvadeMode())
    {
        // 如果设置了循环路径，重新开始路径
        if (_repeatWaypointPath)
            StartPath(_run, GetScript()->GetPathId(), true);
        else
            me->SetWalk(!_run);

        // 如果有最后的魅惑者，尝试攻击他
        if (me->LastCharmerGUID)
        {
            if (!me->HasReactState(REACT_PASSIVE))
                if (Unit* lastCharmer = ObjectAccessor::GetUnit(*me, me->LastCharmerGUID))
                    me->EngageWithTarget(lastCharmer);
            me->LastCharmerGUID.Clear();

            // 如果不在战斗中，进入逃避模式
            if (!me->IsInCombat())
                EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
        }
    }

    // 触发魅惑事件
    GetScript()->ProcessEventsFor(SMART_EVENT_CHARMED, nullptr, 0, 0, charmed);

    // 如果没有带 SMART_EVENT_FLAG_WHILE_CHARMED 标志的事件，可以改变AI
    if (!GetScript()->HasAnyEventWithFlag(SMART_EVENT_FLAG_WHILE_CHARMED))
        UnitAI::OnCharmed(isNew);
}

/**
 * @brief 执行动作
 * @param param 动作参数
 *
 * 用于脚本间通信。
 * 触发动作完成事件。
 */
void SmartAI::DoAction(int32 param)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ACTION_DONE, nullptr, param);
}

/**
 * @brief 获取数据
 * @param id 数据ID
 * @return 数据值（当前实现总是返回0）
 *
 * 用于脚本间数据共享。
 */
uint32 SmartAI::GetData(uint32 /*id*/) const
{
    return 0;
}

/**
 * @brief 设置数据（带触发者）
 * @param id 数据ID
 * @param value 数据值
 * @param invoker 触发者
 *
 * 用于脚本间数据共享。
 * 触发数据设置事件。
 */
void SmartAI::SetData(uint32 id, uint32 value, Unit* invoker)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_DATA_SET, invoker, id, value);
}

/**
 * @brief 设置GUID
 * @param guid GUID值
 * @param id GUID的标识ID
 *
 * 用于脚本间共享GUID数据（当前实现为空）。
 */
void SmartAI::SetGUID(ObjectGuid const& /*guid*/, int32 /*id*/) { }

/**
 * @brief 获取GUID
 * @param id GUID的标识ID
 * @return GUID值（当前实现总是返回空GUID）
 *
 * 用于脚本间共享GUID数据。
 */
ObjectGuid SmartAI::GetGUID(int32 /*id*/) const
{
    return ObjectGuid::Empty;
}

/**
 * @brief 设置跑步/行走模式
 * @param run true=跑步，false=行走
 *
 * 设置生物的移动模式，并更新所有路径点的移动类型。
 */
void SmartAI::SetRun(bool run)
{
    me->SetWalk(!run);
    _run = run;
    // 更新所有路径点的移动类型
    for (auto& node : _path.nodes)
        node.moveType = run ? WAYPOINT_MOVE_TYPE_RUN : WAYPOINT_MOVE_TYPE_WALK;
}

/**
 * @brief 设置禁用重力
 * @param fly true=禁用重力（飞行），false=启用重力
 *
 * 控制生物是否受重力影响。
 */
void SmartAI::SetDisableGravity(bool fly)
{
    me->SetDisableGravity(fly);
}

/**
 * @brief 设置禁用逃避
 * @param disable true=禁用逃避，false=允许逃避
 *
 * 控制生物在脱战时是否逃避回出生点。
 */
void SmartAI::SetEvadeDisabled(bool disable)
{
    _evadeDisabled = disable;
}

/**
 * @brief 对话问候回调
 * @param player 打开对话的玩家
 * @return 如果处理了对话返回true，否则返回false
 *
 * 当玩家打开与生物的对话时调用。
 * 触发对话问候事件。
 */
bool SmartAI::OnGossipHello(Player* player)
{
    _gossipReturn = false;
    GetScript()->ProcessEventsFor(SMART_EVENT_GOSSIP_HELLO, player);
    return _gossipReturn;
}

/**
 * @brief 对话选项选择回调
 * @param player 选择对话的玩家
 * @param menuId 菜单ID
 * @param gossipListId 对话列表ID
 * @return 如果处理了选择返回true，否则返回false
 *
 * 当玩家选择对话选项时调用。
 * 触发对话选择事件。
 */
bool SmartAI::OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId)
{
    _gossipReturn = false;
    GetScript()->ProcessEventsFor(SMART_EVENT_GOSSIP_SELECT, player, menuId, gossipListId);
    return _gossipReturn;
}

/**
 * @brief 对话代码输入回调
 * @param player 输入代码的玩家
 * @param menuId 菜单ID
 * @param gossipListId 对话列表ID
 * @param code 输入的代码字符串
 * @return 当前实现总是返回false
 *
 * 当玩家在对话中输入代码时调用（当前未实现）。
 */
bool SmartAI::OnGossipSelectCode(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/, char const* /*code*/)
{
    return false;
}

/**
 * @brief 任务接受回调
 * @param player 接受任务的玩家
 * @param quest 任务对象
 *
 * 当玩家接受任务时调用。
 * 触发任务接受事件。
 */
void SmartAI::OnQuestAccept(Player* player, Quest const* quest)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ACCEPTED_QUEST, player, quest->GetQuestId());
}

/**
 * @brief 任务奖励回调
 * @param player 完成任务的玩家
 * @param quest 任务对象
 * @param opt 选项索引
 *
 * 当玩家获得任务奖励时调用。
 * 触发任务奖励事件。
 */
void SmartAI::OnQuestReward(Player* player, Quest const* quest, uint32 opt)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_REWARD_QUEST, player, quest->GetQuestId(), opt);
}

/**
 * @brief 设置战斗移动状态
 * @param on true允许战斗中移动，false禁止战斗中移动
 * @param stopMoving 是否立即停止移动（当禁止移动时）
 *
 * 控制生物在战斗中是否追击目标。
 * 禁止移动时，生物会在原地攻击。
 */
void SmartAI::SetCombatMove(bool on, bool stopMoving)
{
    // 如果状态没变，直接返回
    if (_canCombatMove == on)
        return;

    _canCombatMove = on;

    // 如果AI不受控，不执行移动操作
    if (!IsAIControlled())
        return;

    // 如果在战斗中
    if (me->IsEngaged())
    {
        if (on)
        {
            // 允许移动：开始追击目标
            // 检查是否已经有普通优先级的追击移动
            if (!me->HasReactState(REACT_PASSIVE) && me->GetVictim() && !me->GetMotionMaster()->HasMovementGenerator([](MovementGenerator const* movement) -> bool
                {
                    return movement->GetMovementGeneratorType() == CHASE_MOTION_TYPE && movement->Mode == MOTION_MODE_DEFAULT && movement->Priority == MOTION_PRIORITY_NORMAL;
                }))
            {
                SetRun(_run);
                me->GetMotionMaster()->MoveChase(me->GetVictim());
            }
        }
        else if (MovementGenerator* movement = me->GetMotionMaster()->GetMovementGenerator([](MovementGenerator const* a) -> bool
            {
                return a->GetMovementGeneratorType() == CHASE_MOTION_TYPE && a->Mode == MOTION_MODE_DEFAULT && a->Priority == MOTION_PRIORITY_NORMAL;
            }))
        {
            // 禁止移动：移除追击移动
            me->GetMotionMaster()->Remove(movement);
            if (stopMoving)
                me->StopMoving();
        }
    }
}

/**
 * @brief 设置跟随目标
 * @param target 跟随目标（nullptr表示停止跟随）
 * @param dist 跟随距离
 * @param angle 跟随角度
 * @param credit 任务积分ID（用于完成任务目标）
 * @param end 到达特定生物entry时结束跟随
 * @param creditType 积分类型（0=奖励组，1=组事件）
 *
 * 设置生物跟随指定目标移动。
 */
void SmartAI::SetFollow(Unit* target, float dist, float angle, uint32 credit, uint32 end, uint32 creditType)
{
    if (!target)
    {
        StopFollow(false);
        return;
    }

    // 设置跟随参数
    _followGUID = target->GetGUID();
    _followDistance = dist;
    _followAngle = angle;
    _followArrivedTimer = 1000;
    _followCredit = credit;
    _followArrivedEntry = end;
    _followCreditType = creditType;

    // 开始跟随
    SetRun(_run);
    me->GetMotionMaster()->MoveFollow(target, _followDistance, _followAngle);
}

/**
 * @brief 停止跟随
 * @param complete 是否完成跟随任务
 *
 * 停止跟随目标。
 * 如果complete为true会给予任务积分并消失。
 */
void SmartAI::StopFollow(bool complete)
{
    // 清除跟随参数
    _followGUID.Clear();
    _followDistance = 0;
    _followAngle = 0;
    _followCredit = 0;
    _followArrivedTimer = 1000;
    _followArrivedEntry = 0;
    _followCreditType = 0;

    // 停止移动
    me->GetMotionMaster()->Clear();
    me->GetMotionMaster()->MoveIdle();

    if (!complete)
        return;

    // 如果完成，给予任务积分
    Player* player = ObjectAccessor::GetPlayer(*me, _followGUID);
    if (player)
    {
        if (!_followCreditType)
            player->RewardPlayerAndGroupAtEvent(_followCredit, me);
        else
            player->GroupEventHappens(_followCredit, me);
    }

    // 设置消失并触发跟随完成事件
    SetDespawnTime(5000);
    StartDespawn();
    GetScript()->ProcessEventsFor(SMART_EVENT_FOLLOW_COMPLETED, player);
}

/**
 * @brief 设置定时动作列表
 * @param e SmartScript持有对象引用
 * @param entry 动作列表ID
 * @param invoker 触发者
 *
 * 设置一个定时执行的SmartScript动作列表。
 */
void SmartAI::SetTimedActionList(SmartScriptHolder& e, uint32 entry, Unit* invoker)
{
    GetScript()->SetTimedActionList(e, entry, invoker);
}

/**
 * @brief 游戏事件回调
 * @param start true=事件开始，false=事件结束
 * @param eventId 游戏事件ID
 *
 * 当游戏事件开始或结束时调用。
 * 触发相应的事件。
 */
void SmartAI::OnGameEvent(bool start, uint16 eventId)
{
    GetScript()->ProcessEventsFor(start ? SMART_EVENT_GAME_EVENT_START : SMART_EVENT_GAME_EVENT_END, nullptr, eventId);
}

/**
 * @brief 法术点击回调
 * @param clicker 点击者
 * @param spellClickHandled 法术点击是否已处理
 *
 * 当玩家点击生物触发法术时调用。
 * 触发法术点击事件。
 */
void SmartAI::OnSpellClick(Unit* clicker, bool spellClickHandled)
{
    if (!spellClickHandled)
        return;

    GetScript()->ProcessEventsFor(SMART_EVENT_ON_SPELLCLICK, clicker);
}

/**
 * @brief 检查载具条件
 * @param diff 时间差（毫秒）
 *
 * 定期检查载具乘客是否满足条件。
 * 如果乘客不满足条件，会被踢出载具。
 */
void SmartAI::CheckConditions(uint32 diff)
{
    // 如果没有载具条件配置，直接返回
    if (!_vehicleConditions)
        return;

    // 更新计时器
    if (_vehicleConditionsTimer <= diff)
    {
        // 获取载具组件
        if (Vehicle* vehicleKit = me->GetVehicleKit())
        {
            // 检查每个座位的乘客
            for (std::pair<int8 const, VehicleSeat>& seat : vehicleKit->Seats)
                if (Unit* passenger = ObjectAccessor::GetUnit(*me, seat.second.Passenger.Guid))
                {
                    // 检查玩家是否满足条件
                    if (Player* player = passenger->ToPlayer())
                    {
                        if (!sConditionMgr->IsObjectMeetingNotGroupedConditions(CONDITION_SOURCE_TYPE_CREATURE_TEMPLATE_VEHICLE, me->GetEntry(), player, me))
                        {
                            // 不满足条件，踢出载具
                            player->ExitVehicle();
                            return; // 在下一帧检查其他乘客
                        }
                    }
                }
        }

        // 重置检查计时器
        _vehicleConditionsTimer = 1000;
    }
    else
        _vehicleConditionsTimer -= diff;
}

/**
 * @brief 更新路径状态
 * @param diff 时间差（毫秒）
 *
 * 处理护送路径的更新逻辑：
 * - 检查护送触发者是否在范围内
 * - 处理路径点暂停计时器
 * - 处理战斗后返回脱战位置
 * - 处理路径结束
 */
void SmartAI::UpdatePath(uint32 diff)
{
    // 如果不在护送中，直接返回
    if (!HasEscortState(SMART_ESCORT_ESCORTING))
        return;

    // 检查护送触发者距离
    if (_escortInvokerCheckTimer < diff)
    {
        // 触发者超出范围，护送失败
        if (!IsEscortInvokerInRange())
        {
            StopPath(0, _escortQuestId, true);

            // 触发死亡事件，允许正确处理消失动作（通常与死亡执行相同操作）
            GetScript()->ProcessEventsFor(SMART_EVENT_DEATH, me);
            me->DespawnOrUnsummon();
            return;
        }
        _escortInvokerCheckTimer = 1000;
    }
    else
        _escortInvokerCheckTimer -= diff;

    // 处理暂停状态
    if (HasEscortState(SMART_ESCORT_PAUSED) && (_waypointReached || _waypointPauseForced))
    {
        // 只有设置了暂停计时器时才恢复
        if (_waypointPauseTimer && !me->IsInCombat() && !HasEscortState(SMART_ESCORT_RETURNING))
        {
            if (_waypointPauseTimer <= diff)
                ResumePath();
            else
                _waypointPauseTimer -= diff;
        }
    }
    else if (_waypointPathEnded) // 路径结束
    {
        _waypointPathEnded = false;
        StopPath();
        return;
    }

    // 处理返回脱战位置状态
    if (HasEscortState(SMART_ESCORT_RETURNING))
    {
        if (_OOCReached) // 到达脱战位置
        {
            _OOCReached = false;
            RemoveEscortState(SMART_ESCORT_RETURNING);
            // 如果不在暂停状态，恢复路径
            if (!HasEscortState(SMART_ESCORT_PAUSED))
                ResumePath();
        }
    }
}

/**
 * @brief 更新跟随状态
 * @param diff 时间差（毫秒）
 *
 * 处理跟随逻辑：
 * - 检查是否到达目标生物
 * - 到达后完成跟随并给予任务积分
 */
void SmartAI::UpdateFollow(uint32 diff)
{
    if (_followGUID)
    {
        // 更新到达检查计时器
        if (_followArrivedTimer < diff)
        {
            // 检查是否到达目标生物
            if (me->FindNearestCreature(_followArrivedEntry, INTERACTION_DISTANCE, true))
            {
                StopFollow(true);
                return;
            }

            _followArrivedTimer = 1000;
        }
        else
            _followArrivedTimer -= diff;
    }
}

/**
 * @brief 更新消失状态
 * @param diff 时间差（毫秒）
 *
 * 处理生物消失过程：
 * - 状态2：隐藏生物模型
 * - 状态3：彻底移除生物
 */
void SmartAI::UpdateDespawn(uint32 diff)
{
    // 检查消失状态是否有效
    if (_despawnState <= 1 || _despawnState > 3)
        return;

    // 更新消失计时器
    if (_despawnTime < diff)
    {
        if (_despawnState == 2)
        {
            // 状态2：隐藏生物模型
            me->SetVisible(false);
            _despawnTime = 5000;
            _despawnState++;
        }
        else
        {
            // 状态3：彻底移除生物
            me->DespawnOrUnsummon();
        }
    }
    else
        _despawnTime -= diff;
}

// ============================================================================
// SmartGameObjectAI 实现
// ============================================================================

/**
 * @brief 更新AI
 * @param diff 距上次更新的时间差（毫秒）
 *
 * 每个世界更新周期调用一次，更新SmartScript。
 */
void SmartGameObjectAI::UpdateAI(uint32 diff)
{
    GetScript()->OnUpdate(diff);
}

/**
 * @brief 初始化AI
 *
 * 初始化SmartScript，如果游戏对象已生成，触发重生事件。
 */
void SmartGameObjectAI::InitializeAI()
{
    GetScript()->OnInitialize(me);
    // 如果游戏对象已生成，触发重生事件
    if (me->isSpawned())
        GetScript()->ProcessEventsFor(SMART_EVENT_RESPAWN);
    //Reset();
}

/**
 * @brief 重置AI
 *
 * 重置SmartScript状态。
 */
void SmartGameObjectAI::Reset()
{
    GetScript()->OnReset();
}

/**
 * @brief 对话问候回调
 * @param player 打开对话的玩家
 * @return 如果处理了对话返回true，否则返回false
 *
 * 当玩家打开与游戏对象的对话时调用。
 * 触发对话问候事件。
 */
bool SmartGameObjectAI::OnGossipHello(Player* player)
{
    _gossipReturn = false;
    GetScript()->ProcessEventsFor(SMART_EVENT_GOSSIP_HELLO, player, 0, 0, false, nullptr, me);
    return _gossipReturn;
}

/**
 * @brief 对话选项选择回调
 * @param player 选择对话的玩家
 * @param sender 发送者ID（menuId）
 * @param action 动作ID（gossipListId）
 * @return 如果处理了选择返回true，否则返回false
 *
 * 当玩家选择对话选项时调用。
 * 触发对话选择事件。
 */
bool SmartGameObjectAI::OnGossipSelect(Player* player, uint32 sender, uint32 action)
{
    _gossipReturn = false;
    GetScript()->ProcessEventsFor(SMART_EVENT_GOSSIP_SELECT, player, sender, action, false, nullptr, me);
    return _gossipReturn;
}

/**
 * @brief 对话代码输入回调
 * @param player 输入代码的玩家
 * @param menuId 菜单ID
 * @param gossipListId 对话列表ID
 * @param code 输入的代码字符串
 * @return 当前实现总是返回false
 *
 * 当玩家在对话中输入代码时调用（当前未实现）。
 */
bool SmartGameObjectAI::OnGossipSelectCode(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/, char const* /*code*/)
{
    return false;
}

/**
 * @brief 任务接受回调
 * @param player 接受任务的玩家
 * @param quest 任务对象
 *
 * 当玩家从游戏对象接受任务时调用。
 * 触发任务接受事件。
 */
void SmartGameObjectAI::OnQuestAccept(Player* player, Quest const* quest)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_ACCEPTED_QUEST, player, quest->GetQuestId(), 0, false, nullptr, me);
}

/**
 * @brief 任务奖励回调
 * @param player 完成任务的玩家
 * @param quest 任务对象
 * @param opt 选项索引
 *
 * 当玩家从游戏对象获得任务奖励时调用。
 * 触发任务奖励事件。
 */
void SmartGameObjectAI::OnQuestReward(Player* player, Quest const* quest, uint32 opt)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_REWARD_QUEST, player, quest->GetQuestId(), opt, false, nullptr, me);
}

/**
 * @brief 报告使用回调
 * @param player 使用游戏对象的玩家
 * @return 如果处理了使用返回true，否则返回false
 *
 * 当玩家报告使用游戏对象时调用。
 * 触发对话问候事件（带特殊参数）。
 */
bool SmartGameObjectAI::OnReportUse(Player* player)
{
    _gossipReturn = false;
    GetScript()->ProcessEventsFor(SMART_EVENT_GOSSIP_HELLO, player, 1, 0, false, nullptr, me);
    return _gossipReturn;
}

/**
 * @brief 被摧毁回调
 * @param attacker 攻击者（可能为nullptr）
 * @param eventId 事件ID
 *
 * 当可破坏的游戏对象被摧毁时调用。
 * 触发死亡事件。
 */
void SmartGameObjectAI::Destroyed(WorldObject* attacker, uint32 eventId)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_DEATH, attacker ? attacker->ToUnit() : nullptr, eventId, 0, false, nullptr, me);
}

/**
 * @brief 设置数据（带触发者）
 * @param id 数据ID
 * @param value 数据值
 * @param invoker 触发者
 *
 * 触发数据设置事件。
 */
void SmartGameObjectAI::SetData(uint32 id, uint32 value, Unit* invoker)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_DATA_SET, invoker, id, value);
}

/**
 * @brief 设置定时动作列表
 * @param e SmartScript持有对象引用
 * @param entry 动作列表ID
 * @param invoker 触发者
 */
void SmartGameObjectAI::SetTimedActionList(SmartScriptHolder& e, uint32 entry, Unit* invoker)
{
    GetScript()->SetTimedActionList(e, entry, invoker);
}

/**
 * @brief 游戏事件回调
 * @param start true=事件开始，false=事件结束
 * @param eventId 游戏事件ID
 *
 * 触发相应的事件。
 */
void SmartGameObjectAI::OnGameEvent(bool start, uint16 eventId)
{
    GetScript()->ProcessEventsFor(start ? SMART_EVENT_GAME_EVENT_START : SMART_EVENT_GAME_EVENT_END, nullptr, eventId);
}

/**
 * @brief 拾取状态改变回调
 * @param state 新的拾取状态
 * @param unit 相关单位
 *
 * 当游戏对象的拾取状态改变时调用。
 * 触发拾取状态改变事件。
 */
void SmartGameObjectAI::OnLootStateChanged(uint32 state, Unit* unit)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_GO_LOOT_STATE_CHANGED, unit, state);
}

/**
 * @brief 事件通知回调
 * @param eventId 事件ID
 *
 * 当游戏对象收到事件通知时调用。
 * 触发事件通知事件。
 */
void SmartGameObjectAI::EventInform(uint32 eventId)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_GO_EVENT_INFORM, nullptr, eventId);
}

/**
 * @brief 被法术命中回调
 * @param caster 施法者
 * @param spellInfo 法术信息
 *
 * 触发法术命中事件。
 */
void SmartGameObjectAI::SpellHit(WorldObject* caster, SpellInfo const* spellInfo)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SPELLHIT, caster->ToUnit(), 0, 0, false, spellInfo);
}

/**
 * @brief 召唤生物回调
 * @param creature 被召唤的生物
 *
 * 当游戏对象召唤其他生物时调用。
 * 触发召唤事件。
 */
void SmartGameObjectAI::JustSummoned(Creature* creature)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMONED_UNIT, creature);
}

/**
 * @brief 召唤物死亡回调
 * @param summon 死亡的召唤物
 * @param killer 击杀者
 *
 * 触发召唤物死亡事件。
 */
void SmartGameObjectAI::SummonedCreatureDies(Creature* summon, Unit* /*killer*/)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMONED_UNIT_DIES, summon);
}

/**
 * @brief 召唤物消失回调
 * @param unit 消失的召唤物
 *
 * 触发召唤物消失事件。
 */
void SmartGameObjectAI::SummonedCreatureDespawn(Creature* unit)
{
    GetScript()->ProcessEventsFor(SMART_EVENT_SUMMON_DESPAWNED, unit, unit->GetEntry());
}

// ============================================================================
// SmartTrigger - 区域触发器脚本
// ============================================================================

/**
 * @class SmartTrigger
 * @brief 智能区域触发器脚本
 *
 * 实现智能AI的区域触发器功能。
 * 当玩家进入区域触发器时，触发相应的SmartAI事件。
 */
class SmartTrigger : public AreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册触发器脚本名称为"SmartTrigger"
         */
        SmartTrigger() : AreaTriggerScript("SmartTrigger") { }

        /**
         * @brief 触发回调
         * @param player 进入触发器的玩家
         * @param trigger 区域触发器数据
         * @return 总是返回true
         *
         * 当玩家进入区域触发器时调用。
         * 创建临时SmartScript实例并触发区域触发器事件。
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* trigger) override
        {
            // 如果玩家已死亡，不处理
            if (!player->IsAlive())
                return false;

            TC_LOG_DEBUG("scripts.ai", "AreaTrigger {} is using SmartTrigger script", trigger->ID);

            // 创建临时SmartScript实例
            SmartScript script;
            script.OnInitialize(player, trigger);
            // 触发区域触发器事件
            script.ProcessEventsFor(SMART_EVENT_AREATRIGGER_ONTRIGGER, player, trigger->ID);
            return true;
        }
};

/**
 * @brief 注册SmartAI脚本
 *
 * 注册SmartAI系统需要的脚本，包括区域触发器脚本。
 */
void AddSC_SmartScripts()
{
    new SmartTrigger();
}
