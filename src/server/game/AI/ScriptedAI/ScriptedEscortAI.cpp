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
 * @file ScriptedEscortAI.cpp
 * @brief 护送AI模块实现文件
 *
 * 本文件实现了EscortAI类,提供护送任务的核心逻辑:
 * - 路径点移动管理
 * - 战斗辅助与脱战返回
 * - 玩家距离检测与任务失败处理
 * - 任务状态管理
 */

#include "ScriptedEscortAI.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementGenerator.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptSystem.h"
#include "World.h"

/**
 * @brief 特殊路径点ID枚举
 *
 * 定义用于内部移动逻辑的特殊路径点ID
 */
enum Points
{
    POINT_LAST_POINT    = 0xFFFFFF,  ///< 最后一个点(用于战斗后返回)
    POINT_HOME          = 0xFFFFFE   ///< 出生点(用于循环路径返回)
};

/**
 * @brief EscortAI构造函数
 * @param creature 关联的生物对象
 *
 * 初始化所有成员变量为默认值:
 * - 暂停计时器: 2.5秒
 * - 玩家检测计时器: 1秒
 * - 护送状态: 无
 * - 最大玩家距离: 100
 * - 默认设置: 结束时消失,玩家过远时消失
 */
EscortAI::EscortAI(Creature* creature) : ScriptedAI(creature), _pauseTimer(2500ms), _playerCheckTimer(1000), _escortState(STATE_ESCORT_NONE), _maxPlayerDistance(DEFAULT_MAX_PLAYER_DISTANCE),
    _escortQuest(nullptr), _activeAttacker(true), _running(false), _instantRespawn(false), _returnToStart(false), _despawnAtEnd(true), _despawnAtFar(true), _manualPath(false),
    _hasImmuneToNPCFlags(false), _started(false), _ended(false), _resume(false)
{
}

/**
 * @brief 视线范围内的单位检测
 * @param who 进入视线范围的单位
 *
 * 当单位进入视线范围时的处理逻辑:
 * 1. 检查单位是否有效
 * 2. 如果正在护送且需要协助玩家战斗,则协助并返回
 * 3. 否则调用基类的视线检测逻辑
 */
void EscortAI::MoveInLineOfSight(Unit* who)
{
    // 参数有效性检查
    if (!who)
        return;

    // 如果正在护送且需要协助玩家战斗,则处理协助逻辑
    if (HasEscortState(STATE_ESCORT_ESCORTING) && AssistPlayerInCombatAgainst(who))
        return;

    // 调用基类的视线检测逻辑
    ScriptedAI::MoveInLineOfSight(who);
}

/**
 * @brief 生物死亡回调
 * @param killer 击杀者(未使用)
 *
 * 当护送NPC死亡时的处理逻辑:
 * 1. 检查是否在护送状态且有关联任务
 * 2. 如果玩家在队伍中,则让所有在地图上的队伍成员任务失败
 * 3. 如果玩家不在队伍中,则只让该玩家任务失败
 */
void EscortAI::JustDied(Unit* /*killer*/)
{
    // 检查是否在护送状态且有玩家和任务
    if (!HasEscortState(STATE_ESCORT_ESCORTING) || !_playerGUID || !_escortQuest)
        return;

    // 获取触发护送的玩家
    if (Player* player = GetPlayerForEscort())
    {
        // 如果玩家在队伍中,让所有在场的队伍成员任务失败
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
            {
                if (Player* member = groupRef->GetSource())
                    if (member->IsInMap(player))
                        member->FailQuest(_escortQuest->GetQuestId());
            }
        }
        else
        {
            // 单人情况下让玩家任务失败
            player->FailQuest(_escortQuest->GetQuestId());
        }
    }
}

/**
 * @brief 初始化AI
 *
 * 在AI创建时进行的初始化操作:
 * 1. 重置护送状态为无
 * 2. 确保战斗移动被启用
 * 3. 设置初始暂停时间为2秒(在到达第一个路径点前)
 * 4. 如果阵营被修改则恢复原始阵营
 * 5. 调用Reset进行派生类的初始化
 */
void EscortAI::InitializeAI()
{
    // 重置护送状态
    _escortState = STATE_ESCORT_NONE;

    // 确保战斗移动被启用
    if (!IsCombatMovementAllowed())
        SetCombatMovement(true);

    // 设置一个短暂延迟后再移动到第一个路径点,这在大多数情况下是正常的
    _pauseTimer = 2s;

    // 如果阵营被修改,恢复为原始阵营
    if (me->GetFaction() != me->GetCreatureTemplate()->faction)
        me->RestoreFaction();

    // 调用派生类的Reset进行自定义初始化
    Reset();
}

/**
 * @brief 返回上一个路径点
 *
 * 使生物移动到上一个记录的位置,通常用于战斗结束后返回战斗前位置
 */
void EscortAI::ReturnToLastPoint()
{
    me->GetMotionMaster()->MovePoint(POINT_LAST_POINT, me->GetHomePosition());
}

/**
 * @brief 进入脱战模式
 * @param why 脱战原因(未使用)
 *
 * 当生物脱离战斗时的处理逻辑:
 * 1. 移除所有光环
 * 2. 停止战斗
 * 3. 清除战利品接收者
 * 4. 结束交战状态
 * 5. 如果正在护送,则设置为返回状态并返回上一个路径点
 * 6. 否则回家并恢复NPC免疫标志
 */
void EscortAI::EnterEvadeMode(EvadeReason /*why*/)
{
    // 移除所有光环效果
    me->RemoveAllAuras();
    // 停止战斗
    me->CombatStop(true);
    // 清除战利品接收者
    me->SetLootRecipient(nullptr);

    // 结束交战状态
    EngagementOver();

    // 如果正在护送中,则返回战斗前的位置
    if (HasEscortState(STATE_ESCORT_ESCORTING))
    {
        // 添加返回状态
        AddEscortState(STATE_ESCORT_RETURNING);
        // 返回上一个路径点
        ReturnToLastPoint();
        TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::EnterEvadeMode: left combat and is now returning to last point ({})", me->GetGUID().ToString());
    }
    else
    {
        // 非护送状态,直接回家
        me->GetMotionMaster()->MoveTargetedHome();
        // 如果之前有NPC免疫标志,则恢复
        if (_hasImmuneToNPCFlags)
            me->SetImmuneToNPC(true);
        // 调用Reset重置AI状态
        Reset();
    }
}

/**
 * @brief 移动完成通知
 * @param type 移动类型(POINT_MOTION_TYPE或WAYPOINT_MOTION_TYPE)
 * @param id 路径点ID
 *
 * 当生物完成移动时的处理逻辑:
 * 1. 点移动类型:
 *    - POINT_LAST_POINT: 从战斗返回完成,恢复移动速度并移除返回状态
 *    - POINT_HOME: 返回出生点完成,重置开始标志以便重新开始
 * 2. 路径点移动类型:
 *    - 到达路径点时记录日志
 *    - 到达最后一个路径点时设置结束标志
 */
void EscortAI::MovementInform(uint32 type, uint32 id)
{
    // 如果不在护送状态,不执行任何操作
    if (!HasEscortState(STATE_ESCORT_ESCORTING))
        return;

    // 处理点移动类型(战斗后返回或回家)
    if (type == POINT_MOTION_TYPE)
    {
        // 如果暂停计时器为0,设置为2秒
        if (_pauseTimer == 0s)
            _pauseTimer = 2s;

        // 处理返回战斗前位置完成
        if (id == POINT_LAST_POINT)
        {
            TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::MovementInform: returned to before combat position ({})", me->GetGUID().ToString());
            // 恢复移动方式(跑步或步行)
            me->SetWalk(!_running);
            // 移除返回状态,继续护送
            RemoveEscortState(STATE_ESCORT_RETURNING);
        }
        // 处理返回出生点完成(循环路径)
        else if (id == POINT_HOME)
        {
            TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::MovementInform: returned to home location and restarting waypoint path ({})", me->GetGUID().ToString());
            // 重置开始标志,以便重新开始路径
            _started = false;
        }
    }
    // 处理路径点移动类型
    else if (type == WAYPOINT_MOTION_TYPE)
    {
        // 确保路径点ID有效
        ASSERT(id < _path.nodes.size(), "EscortAI::MovementInform: referenced movement id (%u) points to non-existing node in loaded path (%s)", id, me->GetGUID().ToString().c_str());
        WaypointNode waypoint = _path.nodes[id];

        TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::MovementInform: waypoint node {} reached ({})", waypoint.id, me->GetGUID().ToString());

        // 到达最后一个路径点
        if (id == _path.nodes.size() - 1)
        {
            _started = false;
            _ended = true;
            // 设置短暂延迟后处理结束逻辑
            _pauseTimer = 1s;
        }
    }
}

/**
 * @brief 更新AI主函数
 * @param diff 距离上次更新的时间差(毫秒)
 *
 * 核心更新逻辑分为三大部分:
 * 1. 路径点移动更新:
 *    - 检查是否在护送状态、未战斗、未返回
 *    - 处理暂停计时器
 *    - 处理护送结束逻辑(消失、返回出生点、重生)
 *    - 开始或恢复路径移动
 *
 * 2. 玩家距离检测:
 *    - 定期检查玩家或队伍成员是否在范围内
 *    - 如果玩家不在范围内且设置了远距离消失,则消失或重生
 *
 * 3. 调用派生类的更新逻辑
 *
 * @note 每帧调用,需注意性能优化
 */
void EscortAI::UpdateAI(uint32 diff)
{
    // ==================== 路径点移动更新 ====================
    // 仅在护送中、未战斗、未返回状态时处理
    if (HasEscortState(STATE_ESCORT_ESCORTING) && !me->IsEngaged() && !HasEscortState(STATE_ESCORT_RETURNING))
    {
        // 检查暂停计时器是否到期
        if (_pauseTimer.count() <= diff)
        {
            // 如果未暂停,则处理路径移动
            if (!HasEscortState(STATE_ESCORT_PAUSED))
            {
                _pauseTimer = 0s;

                // 处理护送结束逻辑
                if (_ended)
                {
                    _ended = false;
                    // 停止移动
                    me->GetMotionMaster()->MoveIdle();

                    // 如果设置了结束时消失
                    if (_despawnAtEnd)
                    {
                        TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::UpdateAI: reached end of waypoints, despawning at end ({})", me->GetGUID().ToString());

                        // 如果设置为返回起点(循环路径)
                        if (_returnToStart)
                        {
                            // 获取重生位置
                            Position respawnPosition;
                            float orientation = 0.f;
                            me->GetRespawnPosition(respawnPosition.m_positionX, respawnPosition.m_positionY, respawnPosition.m_positionZ, &orientation);
                            respawnPosition.SetOrientation(orientation);
                            // 移动到重生位置
                            me->GetMotionMaster()->MovePoint(POINT_HOME, respawnPosition);
                            TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::UpdateAI: returning to spawn location: {} ({})", respawnPosition.ToString(), me->GetGUID().ToString());
                        }
                        // 如果设置为立即重生
                        else if (_instantRespawn)
                            me->Respawn(true);
                        // 否则直接消失
                        else
                            me->DespawnOrUnsummon();
                    }

                    TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::UpdateAI: reached end of waypoints ({})", me->GetGUID().ToString());
                    // 移除护送状态
                    RemoveEscortState(STATE_ESCORT_ESCORTING);
                    return;
                }

                // 开始路径移动
                if (!_started)
                {
                    _started = true;
                    // 开始沿路径移动(不重复)
                    me->GetMotionMaster()->MovePath(_path, false);
                }
                // 恢复移动(从暂停恢复)
                else if (_resume)
                {
                    _resume = false;
                    // 获取当前移动生成器并恢复
                    if (MovementGenerator* movementGenerator = me->GetMotionMaster()->GetCurrentMovementGenerator(MOTION_SLOT_DEFAULT))
                        movementGenerator->Resume(0);
                }
            }
        }
        else
        {
            // 计时器未到期,减少剩余时间
            _pauseTimer -= Milliseconds(diff);
        }
    }

    // ==================== 玩家距离检测 ====================
    // 仅在设置了远距离消失、护送中、有玩家、未战斗、未返回时检测
    if (_despawnAtFar && HasEscortState(STATE_ESCORT_ESCORTING) && _playerGUID && !me->IsEngaged() && !HasEscortState(STATE_ESCORT_RETURNING))
    {
        // 检查玩家检测计时器是否到期
        if (_playerCheckTimer <= diff)
        {
            // 检查玩家或队伍成员是否在范围内
            if (!IsPlayerOrGroupInRange())
            {
                TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::UpdateAI: failed because player/group was to far away or not found ({})", me->GetGUID().ToString());

                // 检查是否为护送任务NPC
                bool isEscort = false;
                if (CreatureData const* creatureData = me->GetCreatureData())
                    isEscort = (sWorld->getBoolConfig(CONFIG_RESPAWN_DYNAMIC_ESCORTNPC) && (creatureData->spawnGroupData->flags & SPAWNGROUP_FLAG_ESCORTQUESTNPC));

                // 根据设置处理消失或重生
                if (_instantRespawn)
                {
                    if (!isEscort)
                        me->DespawnOrUnsummon(0s, 1s);  // 1秒后重生
                    else
                        me->GetMap()->Respawn(SPAWN_TYPE_CREATURE, me->GetSpawnId());  // 立即重生
                }
                else
                    me->DespawnOrUnsummon();  // 普通消失

                return;
            }

            // 重置玩家检测计时器为1秒
            _playerCheckTimer = 1000;
        }
        else
        {
            // 计时器未到期,减少剩余时间
            _playerCheckTimer -= diff;
        }
    }

    // ==================== 调用派生类更新逻辑 ====================
    UpdateEscortAI(diff);
}

/**
 * @brief 更新护送AI(派生类可重写)
 * @param diff 距离上次更新的时间差(毫秒)
 *
 * 默认实现为简单的近战攻击逻辑,派生类可重写以添加自定义行为
 */
void EscortAI::UpdateEscortAI(uint32 /*diff*/)
{
    // 如果没有有效的攻击目标,则返回
    if (!UpdateVictim())
        return;

    // 如果准备就绪则进行近战攻击
    DoMeleeAttackIfReady();
}

/**
 * @brief 添加路径点
 * @param id 路径点ID
 * @param x X坐标
 * @param y Y坐标
 * @param z Z坐标
 * @param orientation 朝向角度(默认为0)
 * @param waitTime 在此路径点的等待时间(默认为0)
 *
 * 向路径中添加一个新的路径点:
 * 1. 对坐标进行地图标准化处理
 * 2. 创建路径点节点并设置属性
 * 3. 根据当前移动方式设置移动类型
 * 4. 标记为手动路径
 */
void EscortAI::AddWaypoint(uint32 id, float x, float y, float z, float orientation/* = 0*/, Milliseconds waitTime/* = 0s*/)
{
    // 标准化地图坐标,确保在有效范围内
    Trinity::NormalizeMapCoord(x);
    Trinity::NormalizeMapCoord(y);

    // 创建路径点节点
    WaypointNode waypoint;
    waypoint.id = id;
    waypoint.x = x;
    waypoint.y = y;
    waypoint.z = z;
    waypoint.orientation = orientation;
    // 根据当前移动方式设置移动类型(跑步或步行)
    waypoint.moveType = _running ? WAYPOINT_MOVE_TYPE_RUN : WAYPOINT_MOVE_TYPE_WALK;
    waypoint.delay = waitTime.count();
    waypoint.eventId = 0;          // 无关联事件
    waypoint.eventChance = 100;    // 事件触发几率100%

    // 添加到路径列表
    _path.nodes.push_back(std::move(waypoint));

    // 标记为手动路径
    _manualPath = true;
}

/**
 * @brief 开始护送任务
 * @param isActiveAttacker 是否为主动攻击者(已过时,由阵营决定)
 * @param run 是否跑步(默认false,步行)
 * @param playerGUID 触发护送的玩家GUID
 * @param quest 关联的任务对象(可为nullptr)
 * @param instantRespawn 是否立即重生(默认false)
 * @param canLoopPath 是否循环路径(默认false)
 * @param resetWaypoints 是否重置路径点(默认true)
 *
 * 启动护送任务的核心流程:
 * 1. 如果是护送任务NPC,保存重生时间
 * 2. 检查前置条件(未战斗、未在护送中)
 * 3. 加载或重置路径点
 * 4. 设置护送参数
 * 5. 清空移动生成器并清除NPC标志
 * 6. 设置初始移动方式
 * 7. 设置护送状态为进行中
 *
 * @todo 减少函数参数数量
 */
/// @todo get rid of this many variables passed in function.
void EscortAI::Start(bool isActiveAttacker /* = true*/, bool run /* = false */, ObjectGuid playerGUID /* = 0 */, Quest const* quest /* = nullptr */, bool instantRespawn /* = false */, bool canLoopPath /* = false */, bool resetWaypoints /* = true */)
{
    // 如果是护送任务NPC,从开始时就保存重生时间
    if (CreatureData const* cdata = me->GetCreatureData())
    {
        if (sWorld->getBoolConfig(CONFIG_RESPAWN_DYNAMIC_ESCORTNPC) && (cdata->spawnGroupData->flags & SPAWNGROUP_FLAG_ESCORTQUESTNPC))
            me->SaveRespawnTime(me->GetRespawnDelay());
    }

    // 检查是否在战斗中
    if (me->IsEngaged())
    {
        TC_LOG_ERROR("scripts.ai.escortai", "EscortAI::Start: (script: {}) attempts to Start while in combat ({})", me->GetScriptName(), me->GetGUID().ToString());
        return;
    }

    // 检查是否已经在护送中
    if (HasEscortState(STATE_ESCORT_ESCORTING))
    {
        TC_LOG_ERROR("scripts.ai.escortai", "EscortAI::Start: (script: {}) attempts to Start while already escorting ({})", me->GetScriptName(), me->GetGUID().ToString());
        return;
    }

    // 设置移动方式
    _running = run;

    // 如果不是手动路径且需要重置路径点,从脚本系统加载路径
    if (!_manualPath && resetWaypoints)
        FillPointMovementListForCreature();

    // 检查路径点是否为空
    if (_path.nodes.empty())
    {
        TC_LOG_ERROR("scripts.ai.escortai", "EscortAI::Start: (script: {}) is set to return home after waypoint end and instant respawn at waypoint end. Creature will never despawn ({})", me->GetScriptName(), me->GetGUID().ToString());
        return;
    }

    // 设置护送参数
    _activeAttacker = isActiveAttacker;
    _playerGUID = playerGUID;
    _escortQuest = quest;
    _instantRespawn = instantRespawn;
    _returnToStart = canLoopPath;

    // 检查配置冲突: 同时设置返回起点和立即重生会导致永不消失
    if (_returnToStart && _instantRespawn)
        TC_LOG_ERROR("scripts.ai.escortai", "EscortAI::Start: (script: {}) is set to return home after waypoint end and instant respawn at waypoint end. Creature will never despawn ({})", me->GetScriptName(), me->GetGUID().ToString());

    // 清空移动生成器
    me->GetMotionMaster()->MoveIdle();
    me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);

    // 移除所有NPC标志,防止玩家交互
    me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
    // 如果有NPC免疫标志,记录并移除
    if (me->IsImmuneToNPC())
    {
        _hasImmuneToNPCFlags = true;
        me->SetImmuneToNPC(false);
    }

    // 记录护送开始信息
    TC_LOG_DEBUG("scripts.ai.escortai", "EscortAI::Start: (script: {}) started with {} waypoints. ActiveAttacker = {}, Run = {}, Player = {} ({})",
        me->GetScriptName(), uint32(_path.nodes.size()), _activeAttacker, _running, _playerGUID.ToString(), me->GetGUID().ToString());

    // 设置初始移动速度
    me->SetWalk(!_running);

    // 设置护送状态
    _started = false;
    AddEscortState(STATE_ESCORT_ESCORTING);
}

/**
 * @brief 设置移动方式
 * @param on true为跑步,false为步行
 *
 * 切换生物的移动方式,并更新所有路径点的移动类型
 */
void EscortAI::SetRun(bool on)
{
    // 如果移动方式未改变,直接返回
    if (on == _running)
        return;

    // 更新所有路径点的移动类型
    for (auto& node : _path.nodes)
        node.moveType = on ? WAYPOINT_MOVE_TYPE_RUN : WAYPOINT_MOVE_TYPE_WALK;

    // 设置生物的移动方式
    me->SetWalk(!on);
    _running = on;
}

/**
 * @brief 设置护送暂停状态
 * @param on true为暂停,false为继续
 *
 * 暂停时添加暂停状态并暂停移动生成器,
 * 继续时移除暂停状态并设置恢复标志
 */
void EscortAI::SetEscortPaused(bool on)
{
    // 检查是否在护送中
    if (!HasEscortState(STATE_ESCORT_ESCORTING))
        return;

    if (on)
    {
        // 添加暂停状态
        AddEscortState(STATE_ESCORT_PAUSED);
        // 暂停移动生成器
        if (MovementGenerator* movementGenerator = me->GetMotionMaster()->GetCurrentMovementGenerator(MOTION_SLOT_DEFAULT))
            movementGenerator->Pause(0);
    }
    else
    {
        // 移除暂停状态
        RemoveEscortState(STATE_ESCORT_PAUSED);
        // 设置恢复标志,在下次更新时恢复移动
        _resume = true;
    }
}

/**
 * @brief 获取护送玩家对象
 * @return 玩家指针,如果玩家不存在则返回nullptr
 *
 * 通过玩家GUID从对象访问器获取玩家对象
 */
Player* EscortAI::GetPlayerForEscort()
{
    return ObjectAccessor::GetPlayer(*me, _playerGUID);
}

/**
 * @brief 协助玩家战斗
 * @param who 敌对单位
 * @return 如果成功协助返回true,否则返回false
 *
 * 检查条件并协助玩家攻击敌对单位:
 * 1. 检查单位有效性和目标
 * 2. 检查生物反应模式(非被动)
 * 3. 检查生物类型标志(可协助)
 * 4. 检查被攻击者是否为玩家
 * 5. 检查敌对单位位置可达性
 * 6. 检查是否可攻击敌对单位
 * 7. 检查脱战状态
 * 8. 检查是否为有效的协助目标
 * 9. 检查距离和视线
 * 10. 满足所有条件则攻击敌对单位
 *
 * @see FollowerAI::ShouldAssistPlayerInCombatAgainst 类似实现
 */
// see followerAI
bool EscortAI::AssistPlayerInCombatAgainst(Unit* who)
{
    // 检查单位有效性和攻击目标
    if (!who || !who->GetVictim())
        return false;

    // 检查生物反应模式,被动模式不协助
    if (me->HasReactState(REACT_PASSIVE))
        return false;

    // 检查生物类型标志,必须有CAN_ASSIST标志
    if (!(me->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_CAN_ASSIST))
        return false;

    // 检查被攻击者是否为玩家(包括宠物和魅惑单位的主人)
    if (!who->EnsureVictim()->GetCharmerOrOwnerPlayerOrPlayerItself())
        return false;

    // 检查敌对单位位置对当前生物是否可达
    if (!who->isInAccessiblePlaceFor(me))
        return false;

    // 检查是否可以攻击敌对单位
    if (!CanAIAttack(who))
        return false;

    // 检查生物是否在脱战状态,脱战状态不能攻击
    if (me->IsInEvadeMode())
        return false;

    // 检查敌对单位是否在脱战状态
    if (who->GetTypeId() == TYPEID_UNIT && who->ToCreature()->IsInEvadeMode())
        return false;

    // 检查被攻击者是否为有效的协助目标
    if (!me->IsValidAssistTarget(who->GetVictim()))
        return false;

    // 检查距离和视线,在范围内且有视线则攻击
    if (me->IsWithinDistInMap(who, GetMaxPlayerDistance()) && me->IsWithinLOSInMap(who))
    {
        // 与敌对单位交战
        me->EngageWithTarget(who);
        return true;
    }

    return false;
}

/**
 * @brief 检查玩家或队伍成员是否在范围内
 * @return 如果玩家或任一队伍成员在范围内返回true,否则返回false
 *
 * 检查逻辑:
 * 1. 获取触发护送的玩家
 * 2. 如果玩家在队伍中,检查所有队伍成员
 * 3. 只要有一个成员在范围内就返回true
 * 4. 如果玩家不在队伍中,只检查玩家本人
 */
bool EscortAI::IsPlayerOrGroupInRange()
{
    // 获取触发护送的玩家
    if (Player* player = GetPlayerForEscort())
    {
        // 如果玩家在队伍中
        if (Group* group = player->GetGroup())
        {
            // 遍历所有队伍成员
            for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
            {
                if (Player* member = groupRef->GetSource())
                    // 如果任一成员在范围内,返回true
                    if (me->IsWithinDistInMap(member, GetMaxPlayerDistance()))
                        return true;
            }
        }
        // 单人情况,检查玩家本人
        else if (me->IsWithinDistInMap(player, GetMaxPlayerDistance()))
            return true;
    }

    return false;
}

/**
 * @brief 从脚本系统加载路径点
 *
 * 从ScriptSystemMgr加载生物的预设路径:
 * 1. 根据生物入口ID获取路径
 * 2. 如果路径存在,遍历所有节点
 * 3. 对坐标进行标准化处理
 * 4. 根据当前移动方式设置移动类型
 * 5. 添加到路径列表
 */
void EscortAI::FillPointMovementListForCreature()
{
    // 从脚本系统获取生物的路径
    WaypointPath const* path = sScriptSystemMgr->GetPath(me->GetEntry());
    if (!path)
        return;

    // 遍历路径中的所有节点
    for (WaypointNode const& value : path->nodes)
    {
        // 复制节点
        WaypointNode node = value;
        // 标准化坐标
        Trinity::NormalizeMapCoord(node.x);
        Trinity::NormalizeMapCoord(node.y);
        // 设置移动类型
        node.moveType = _running ? WAYPOINT_MOVE_TYPE_RUN : WAYPOINT_MOVE_TYPE_WALK;

        // 添加到路径列表
        _path.nodes.push_back(std::move(node));
    }
}
