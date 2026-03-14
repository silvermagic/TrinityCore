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
 * @file Transport.cpp
 * @brief 运输工具（Transport）模块实现文件
 *
 * 本文件实现了 Transport 类的所有方法，包括：
 *   - 对象创建与初始化
 *   - 主更新循环与路径移动
 *   - 乘客管理（添加、移除、位置同步）
 *   - 跨地图传送处理
 *   - 事件触发机制
 *
 * 关键算法：
 *   1. 路径插值：使用 Catmull-Rom 样条曲线平滑移动
 *   2. 加速运动：模拟真实物理的加速/减速过程
 *   3. 网格管理：基于网格活跃状态加载/卸载静态乘客
 *
 * @see Transport.h 头文件定义
 * @see TransportMgr.h 管理器实现
 */

#include "Transport.h"
#include "Cell.h"
#include "CellImpl.h"
#include "Common.h"
#include "DBCStores.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "Spline.h"
#include "Player.h"
#include "Totem.h"
#include "UpdateData.h"
#include "Vehicle.h"
#include <G3D/Vector3.h>

/**
 * @brief Transport 构造函数
 *
 * 初始化运输工具的所有成员变量和更新标志。
 * 运输工具创建时默认处于移动状态，等待路径数据加载。
 *
 * 更新标志说明：
 *   - UPDATEFLAG_TRANSPORT: 标识为运输工具类型
 *   - UPDATEFLAG_LOWGUID: 使用低GUID（优化网络传输）
 *   - UPDATEFLAG_STATIONARY_POSITION: 初始位置固定（后续动态更新）
 *   - UPDATEFLAG_ROTATION: 需要同步旋转信息
 */
Transport::Transport() : GameObject(),
    _transportInfo(nullptr), _isMoving(true), _pendingStop(false),
    _triggeredArrivalEvent(false), _triggeredDepartureEvent(false),
    _passengerTeleportItr(_passengers.begin()), _delayedAddModel(false), _delayedTeleport(false)
{
    // 设置更新标志，告知客户端这是运输工具类型的对象
    m_updateFlag = UPDATEFLAG_TRANSPORT | UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_ROTATION;
}

/**
 * @brief Transport 析构函数
 *
 * 销毁运输工具时执行清理操作：
 *   1. 断言检查确保所有动态乘客已被移除
 *   2. 卸载所有静态乘客
 *
 * @note 动态乘客应该在删除运输工具之前主动离开
 *       或由外部代码显式移除
 */
Transport::~Transport()
{
    // 确保没有动态乘客（玩家等）在运输工具上
    ASSERT(_passengers.empty());
    // 清理所有静态乘客（NPC、游戏对象等）
    UnloadStaticPassengers();
}

/**
 * @brief 创建并初始化运输工具
 *
 * @param guidlow 对象的低GUID值
 * @param entry 运输工具模板ID
 * @param mapid 地图ID
 * @param x X坐标
 * @param y Y坐标
 * @param z Z坐标
 * @param ang 朝向角度
 * @param animprogress 动画进度
 * @return true 创建成功
 * @return false 创建失败
 *
 * 初始化流程：
 *   1. 设置位置并验证有效性
 *   2. 创建对象GUID（Mo_Transport类型）
 *   3. 加载游戏对象模板
 *   4. 加载运输工具模板（路径数据）
 *   5. 初始化关键帧迭代器
 *   6. 设置各项属性（周期、显示ID、状态等）
 *   7. 创建可视化模型
 */
bool Transport::Create(ObjectGuid::LowType guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress)
{
    // 设置初始位置坐标
    Relocate(x, y, z, ang);

    // 验证坐标是否有效（不能越界等）
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.transport", "Transport (GUID: {}) not created. Suggested coordinates isn't valid (X: {} Y: {})",
            guidlow, x, y);
        return false;
    }

    // 创建对象GUID，使用 Mo_Transport 高位类型
    Object::_Create(guidlow, 0, HighGuid::Mo_Transport);

    // 获取游戏对象模板数据
    GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(entry);
    if (!goinfo)
    {
        TC_LOG_ERROR("sql.sql", "Transport not created: entry in `gameobject_template` not found, guidlow: {} map: {}  (X: {} Y: {} Z: {}) ang: {}", guidlow, mapid, x, y, z, ang);
        return false;
    }

    // 设置模板指针和附加数据
    m_goInfo = goinfo;
    m_goTemplateAddon = sObjectMgr->GetGameObjectTemplateAddon(entry);

    // 获取运输工具专用模板（包含路径数据）
    TransportTemplate const* tInfo = sTransportMgr->GetTransportTemplate(entry);
    if (!tInfo)
    {
        TC_LOG_ERROR("sql.sql", "Transport {} (name: {}) will not be created, missing `transport_template` entry.", entry, goinfo->name);
        return false;
    }

    _transportInfo = tInfo;

    // 初始化路径关键帧迭代器
    // _nextFrame 指向第二个关键帧，_currentFrame 指向第一个
    _nextFrame = tInfo->keyFrames.begin();
    _currentFrame = _nextFrame++;
    _triggeredArrivalEvent = false;
    _triggeredDepartureEvent = false;

    // 应用游戏对象覆盖设置（如果有）
    if (GameObjectOverride const* goOverride = GetGameObjectOverride())
    {
        SetFaction(goOverride->Faction);
        ReplaceAllFlags(GameObjectFlags(goOverride->Flags));
    }

    // 初始化路径进度为0（从头开始）
    m_goValue.Transport.PathProgress = 0;
    // 设置对象缩放
    SetObjectScale(goinfo->size);
    // 设置路径周期时间
    SetPeriod(tInfo->pathTime);
    // 设置模板ID
    SetEntry(goinfo->entry);
    // 设置显示模型ID
    SetDisplayId(goinfo->displayId);
    // 设置游戏对象状态：
    // - 如果不可停止，则设为 GO_STATE_READY
    // - 如果可停止，则设为 GO_STATE_ACTIVE（移动中）
    SetGoState(!goinfo->moTransport.canBeStopped ? GO_STATE_READY : GO_STATE_ACTIVE);
    // 设置游戏对象类型为运输工具
    SetGoType(GAMEOBJECT_TYPE_MO_TRANSPORT);
    // 设置动画进度
    SetGoAnimProgress(animprogress);
    // 设置名称
    SetName(goinfo->name);
    // 设置本地旋转（四元数初始化为单位四元数）
    SetLocalRotation(0.0f, 0.0f, 0.0f, 1.0f);
    // 设置父旋转（空四元数）
    SetParentRotation(QuaternionData());

    // 创建可视化模型
    CreateModel();
    return true;
}

/**
 * @brief 删除前的清理工作
 *
 * @param finalCleanup 是否为最终清理
 *
 * 清理流程：
 *   1. 卸载所有静态乘客（NPC、游戏对象）
 *   2. 移除所有动态乘客（玩家、临时召唤物）
 *   3. 调用父类清理方法
 */
void Transport::CleanupsBeforeDelete(bool finalCleanup /*= true*/)
{
    // 先卸载静态乘客
    UnloadStaticPassengers();
    // 移除所有动态乘客
    while (!_passengers.empty())
    {
        WorldObject* obj = *_passengers.begin();
        RemovePassenger(obj);
    }

    // 调用父类清理
    GameObject::CleanupsBeforeDelete(finalCleanup);
}

/**
 * @brief 更新运输工具状态（主更新循环）
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 这是运输工具的核心更新函数，每帧调用一次，负责：
 *   1. AI更新
 *   2. 路径进度推进
 *   3. 关键帧状态检测
 *   4. 位置计算与更新
 *   5. 地图传送处理
 *   6. 静态乘客管理
 *
 * 关键帧状态机：
 *   到达时间 ---- 离开时间 ---- 下一个到达时间
 *        |         停靠延迟          |
 *        |<---停止状态--->|<-移动->|
 *        触发到达事件     触发离开事件
 *
 * @note 位置更新有 200ms 的延迟，减少计算开销
 */
void Transport::Update(uint32 diff)
{
    // 位置更新延迟（毫秒），控制位置计算频率
    uint32 const positionUpdateDelay = 200;

    // 更新AI逻辑
    if (AI())
        AI()->UpdateAI(diff);
    else if (!AIM_Initialize())
        TC_LOG_ERROR("entities.transport", "Could not initialize GameObjectAI for Transport");

    // 如果只有一个或没有关键帧，不需要移动
    if (GetKeyFrames().size() <= 1)
        return;

    // 推进路径进度计时器
    // 如果正在移动或者没有等待停止，则增加进度
    if (IsMoving() || !_pendingStop)
        m_goValue.Transport.PathProgress += diff;

    // 计算当前周期内的相对时间
    uint32 timer = m_goValue.Transport.PathProgress % GetTransportPeriod();
    // 标记是否刚刚停止
    bool justStopped = false;

    // 关键帧状态查找循环
    // 目标状态：_currentFrame->DepartureTime < timer < _nextFrame->ArriveTime
    // 时间线：... 到达 | ... 延迟 ... | 离开 ...
    //              事件/         事件/
    for (;;)
    {
        // 检查是否已到达当前关键帧
        if (timer >= _currentFrame->ArriveTime)
        {
            // 触发到达事件（只触发一次）
            if (!_triggeredArrivalEvent)
            {
                DoEventIfAny(*_currentFrame, false);
                _triggeredArrivalEvent = true;
            }

            // 检查是否在停靠延迟时间内
            if (timer < _currentFrame->DepartureTime)
            {
                // 刚刚停止
                justStopped = IsMoving();
                SetMoving(false);
                // 处理可停止运输工具的逻辑
                if (_pendingStop && GetGoState() != GO_STATE_READY)
                {
                    // 设置为停止状态
                    SetGoState(GO_STATE_READY);
                    // 调整路径进度到当前到达时间
                    m_goValue.Transport.PathProgress = (m_goValue.Transport.PathProgress / GetTransportPeriod());
                    m_goValue.Transport.PathProgress *= GetTransportPeriod();
                    m_goValue.Transport.PathProgress += _currentFrame->ArriveTime;
                }
                break;  // 在停靠点等待
            }
        }

        // 触发离开事件（只触发一次）
        if (timer >= _currentFrame->DepartureTime && !_triggeredDepartureEvent)
        {
            DoEventIfAny(*_currentFrame, true); // 触发离开事件
            _triggeredDepartureEvent = true;
        }

        // 开始移动
        SetMoving(true);

        // 如果是可停止的运输工具，设置为活动状态
        if (GetGOInfo()->moTransport.canBeStopped)
            SetGoState(GO_STATE_ACTIVE);

        // 检查是否找到了当前路径段
        if (timer >= _currentFrame->DepartureTime && timer < _currentFrame->NextArriveTime)
            break;  // 找到了当前路径段

        // 移动到下一个关键帧
        MoveToNextWaypoint();

        // 通知脚本系统位置变更
        sScriptMgr->OnRelocate(this, _currentFrame->Node->NodeIndex, _currentFrame->Node->ContinentID, _currentFrame->Node->Loc.X, _currentFrame->Node->Loc.Y, _currentFrame->Node->Loc.Z);

        TC_LOG_DEBUG("entities.transport", "Transport {} ({}) moved to node {} {} {} {} {}", GetEntry(), GetName(), _currentFrame->Node->NodeIndex, _currentFrame->Node->ContinentID, _currentFrame->Node->Loc.X, _currentFrame->Node->Loc.Y, _currentFrame->Node->Loc.Z);

        // 如果是传送关键帧，执行跨地图传送
        if (_currentFrame->IsTeleportFrame())
            if (TeleportTransport(_nextFrame->Node->ContinentID, _nextFrame->Node->Loc.X, _nextFrame->Node->Loc.Y, _nextFrame->Node->Loc.Z, _nextFrame->InitialOrientation))
                return; // 跨地图传送会在新地图线程继续更新
    }

    // 地图切换完成后，延迟添加模型到地图
    if (_delayedAddModel)
    {
        _delayedAddModel = false;
        if (m_model)
            GetMap()->InsertGameObjectModel(*m_model);
    }

    // 位置更新定时器
    _positionChangeTimer.Update(diff);
    if (_positionChangeTimer.Passed())
    {
        _positionChangeTimer.Reset(positionUpdateDelay);

        if (IsMoving())
        {
            // 计算在当前路径段的位置（0.0 - 1.0）
            float t = !justStopped ? CalculateSegmentPos(float(timer) * 0.001f) : 1.0f;
            G3D::Vector3 pos, dir;
            // 使用样条曲线计算位置
            _currentFrame->Spline->evaluate_percent(_currentFrame->Index, t, pos);
            // 使用样条曲线计算方向（用于朝向）
            _currentFrame->Spline->evaluate_derivative(_currentFrame->Index, t, dir);
            // 更新位置，朝向根据移动方向计算
            UpdatePosition(pos.x, pos.y, pos.z, std::atan2(dir.y, dir.x) + float(M_PI));
        }
        else if (justStopped)
        {
            // 刚停止时，精确定位到关键帧位置
            UpdatePosition(_currentFrame->Node->Loc.X, _currentFrame->Node->Loc.Y, _currentFrame->Node->Loc.Z, _currentFrame->InitialOrientation);
        }
        else
        {
            // 停留状态下，检查网格状态变化
            /* 触发乘客加载/卸载的四种场景：
               1. 运输工具从不活跃网格移动到活跃网格
               2. 运输工具所在的网格变为活跃
               3. 运输工具从活跃网格移动到不活跃网格
               4. 运输工具所在的网格卸载
            */
            bool gridActive = GetMap()->IsGridLoaded(GetPositionX(), GetPositionY());

            if (_staticPassengers.empty() && gridActive) // 场景2：网格变为活跃
                LoadStaticPassengers();
            else if (!_staticPassengers.empty() && !gridActive)
                // 场景4：如果运输工具停在网格边缘，一些乘客可能在活跃网格中
                //       卸载所有静态乘客，否则网格变为活跃时乘客无法正确加载
                UnloadStaticPassengers();
        }
    }

    // 通知脚本系统运输工具更新
    sScriptMgr->OnTransportUpdate(this, diff);
}

/**
 * @brief 延迟更新，处理跨地图传送
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 此函数在主 Update() 之后调用，专门处理需要在新地图线程执行的传送操作。
 * 当运输工具需要跨地图传送时，会设置 _delayedTeleport 标志，
 * 然后在此函数中执行实际的地图切换。
 *
 * 这种设计避免了在主更新循环中执行复杂的地图切换操作。
 */
void Transport::DelayedUpdate(uint32 /*diff*/)
{
    // 如果只有一个或没有关键帧，不需要处理
    if (GetKeyFrames().size() <= 1)
        return;

    // 执行延迟的地图传送
    DelayedTeleportTransport();
}

/**
 * @brief 添加乘客到运输工具
 *
 * @param passenger 要添加的乘客对象
 *
 * 将乘客注册到运输工具的乘客列表中：
 *   1. 检查运输工具是否在世界中
 *   2. 将乘客添加到 _passengers 集合
 *   3. 设置乘客的运输工具引用
 *   4. 添加"在运输工具上"移动标志
 *   5. 记录运输工具GUID
 *   6. 如果是玩家，通知脚本系统
 *
 * 调用时机：
 *   - 玩家登上运输工具时
 *   - 创建运输工具上的临时召唤物时
 */
void Transport::AddPassenger(WorldObject* passenger)
{
    // 运输工具必须在世界中才能接收乘客
    if (!IsInWorld())
        return;

    // 插入乘客并检查是否成功
    if (_passengers.insert(passenger).second)
    {
        // 设置乘客的运输工具引用
        passenger->SetTransport(this);
        // 添加"在运输工具上"移动标志
        passenger->m_movementInfo.AddMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
        // 记录运输工具GUID
        passenger->m_movementInfo.transport.guid = GetGUID();
        TC_LOG_DEBUG("entities.transport", "Object {} boarded transport {}.", passenger->GetName(), GetName());

        // 如果是玩家，通知脚本系统
        if (Player* plr = passenger->ToPlayer())
            sScriptMgr->OnAddPassenger(this, plr);
    }
}

/**
 * @brief 从运输工具移除乘客
 *
 * @param passenger 要移除的乘客对象
 *
 * 从乘客列表中移除乘客：
 *   1. 处理传送过程中可能的迭代器失效问题
 *   2. 从 _passengers 或 _staticPassengers 集合移除
 *   3. 清除运输工具引用
 *   4. 移除"在运输工具上"移动标志
 *   5. 重置运输工具移动信息
 *   6. 如果是玩家，通知脚本系统并重置下落信息
 *
 * 注意：在传送过程中使用特殊的迭代器处理，避免迭代器失效导致崩溃。
 */
void Transport::RemovePassenger(WorldObject* passenger)
{
    bool erased = false;
    // 检查是否在传送过程中
    if (_passengerTeleportItr != _passengers.end())
    {
        // 查找乘客
        PassengerSet::iterator itr = _passengers.find(passenger);
        if (itr != _passengers.end())
        {
            // 如果找到的是当前传送迭代器，先递增避免失效
            if (itr == _passengerTeleportItr)
                ++_passengerTeleportItr;

            _passengers.erase(itr);
            erased = true;
        }
    }
    else
        // 非传送状态，直接删除
        erased = _passengers.erase(passenger) > 0;

    // 检查是否从静态乘客集合删除（网格卸载时静态乘客可能自己移除）
    if (erased || _staticPassengers.erase(passenger))
    {
        // 清除运输工具引用
        passenger->SetTransport(nullptr);
        // 移除"在运输工具上"移动标志
        passenger->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
        // 重置运输工具移动信息
        passenger->m_movementInfo.transport.Reset();
        TC_LOG_DEBUG("entities.transport", "Object {} removed from transport {}.", passenger->GetName(), GetName());

        // 如果是玩家，特殊处理
        if (Player* plr = passenger->ToPlayer())
        {
            // 通知脚本系统
            sScriptMgr->OnRemovePassenger(this, plr);
            // 重置下落信息（防止下落伤害计算错误）
            plr->SetFallInformation(0, plr->GetPositionZ());
        }
    }
}

/**
 * @brief 创建NPC乘客
 *
 * @param guid NPC的生成ID（对应 creature 表的主键）
 * @param data NPC的生成数据
 * @return 创建的生物指针，失败返回 nullptr
 *
 * 从数据库数据创建NPC并将其放置在运输工具上。
 * NPC的位置坐标是相对于运输工具的偏移量。
 *
 * 创建流程：
 *   1. 检查是否已过复活时间
 *   2. 从数据库加载生物数据
 *   3. 设置运输工具相关属性
 *   4. 计算世界坐标
 *   5. 添加到地图
 *   6. 注册为静态乘客
 *
 * @note HACK: 设置 UNIT_STATE_IGNORE_PATHFINDING 标志，
 *       因为运输工具模型无法动态加入地图的视线计算
 */
Creature* Transport::CreateNPCPassenger(ObjectGuid::LowType guid, CreatureData const* data)
{
    Map* map = GetMap();
    // 检查生物是否在复活等待期
    if (map->GetCreatureRespawnTime(guid))
        return nullptr;

    // 创建生物对象
    Creature* creature = new Creature();

    // 从数据库加载生物数据
    if (!creature->LoadFromDB(guid, map, false, false))
    {
        delete creature;
        return nullptr;
    }

    // 获取相对于运输工具的本地坐标
    float x, y, z, o;
    data->spawnPoint.GetPosition(x, y, z, o);

    // 设置运输工具相关属性
    creature->SetTransport(this);
    creature->AddUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
    creature->m_movementInfo.transport.guid = GetGUID();
    creature->m_movementInfo.transport.pos.Relocate(x, y, z, o);

    // 计算世界坐标
    CalculatePassengerPosition(x, y, z, &o);
    creature->Relocate(x, y, z, o);

    // 设置家位置（用于AI返回、脱战等）
    creature->SetHomePosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation());
    // 设置运输工具上的家位置
    creature->SetTransportHomePosition(creature->m_movementInfo.transport.pos);

    /// @HACK - 运输工具模型不会加入地图的动态视线计算
    ///         因为当前的 GameObjectModel 无法在不重建的情况下移动
    ///         所以让生物忽略寻路检查
    creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);

    // 验证位置有效性
    if (!creature->IsPositionValid())
    {
        TC_LOG_ERROR("entities.transport", "Creature {} not created. Suggested coordinates aren't valid (X: {} Y: {})", creature->GetGUID().ToString(), creature->GetPositionX(), creature->GetPositionY());
        delete creature;
        return nullptr;
    }

    // 添加到地图
    if (!map->AddToMap(creature))
    {
        delete creature;
        return nullptr;
    }

    // 注册为静态乘客
    _staticPassengers.insert(creature);
    // 通知脚本系统
    sScriptMgr->OnAddCreaturePassenger(this, creature);
    return creature;
}

/**
 * @brief 创建游戏对象乘客
 *
 * @param guid 游戏对象的生成ID（对应 gameobject 表的主键）
 * @param data 游戏对象的生成数据
 * @return 创建的游戏对象指针，失败返回 nullptr
 *
 * 从数据库数据创建游戏对象并将其放置在运输工具上。
 * 游戏对象的位置坐标是相对于运输工具的偏移量。
 *
 * 创建流程：
 *   1. 检查是否已过复活时间
 *   2. 从数据库加载游戏对象数据
 *   3. 设置运输工具相关属性
 *   4. 计算世界坐标
 *   5. 添加到地图
 *   6. 注册为静态乘客
 */
GameObject* Transport::CreateGOPassenger(ObjectGuid::LowType guid, GameObjectData const* data)
{
    Map* map = GetMap();
    // 检查游戏对象是否在复活等待期
    if (map->GetGORespawnTime(guid))
        return nullptr;

    // 创建游戏对象
    GameObject* go = new GameObject();

    // 从数据库加载游戏对象数据
    if (!go->LoadFromDB(guid, map, false))
    {
        delete go;
        return nullptr;
    }

    ASSERT(data);

    // 获取相对于运输工具的本地坐标
    float x, y, z, o;
    data->spawnPoint.GetPosition(x, y, z, o);

    // 设置运输工具相关属性
    go->SetTransport(this);
    go->m_movementInfo.transport.guid = GetGUID();
    go->m_movementInfo.transport.pos.Relocate(x, y, z, o);

    // 计算世界坐标
    CalculatePassengerPosition(x, y, z, &o);
    go->Relocate(x, y, z, o);
    // 设置固定位置（用于某些游戏对象的重置）
    go->RelocateStationaryPosition(x, y, z, o);

    // 验证位置有效性
    if (!go->IsPositionValid())
    {
        TC_LOG_ERROR("entities.transport", "GameObject {} not created. Suggested coordinates aren't valid (X: {} Y: {})", go->GetGUID().ToString(), go->GetPositionX(), go->GetPositionY());
        delete go;
        return nullptr;
    }

    // 添加到地图
    if (!map->AddToMap(go))
    {
        delete go;
        return nullptr;
    }

    // 注册为静态乘客
    _staticPassengers.insert(go);
    return go;
}

/**
 * @brief 在运输工具上临时召唤生物
 *
 * @param entry 生物模板ID
 * @param pos 相对于运输工具的位置
 * @param summonType 召唤类型
 * @param properties 召唤属性
 * @param duration 持续时间（毫秒）
 * @param summoner 召唤者
 * @param spellId 召唤法术ID
 * @param vehId 载具ID（覆盖模板）
 * @return 召唤的生物指针，失败返回 nullptr
 *
 * 根据召唤属性确定生物类型（守护者、图腾、小宠物等），
 * 然后在运输工具上创建并初始化该生物。
 *
 * 召唤类型与生物类型对应关系：
 *   - SUMMON_CATEGORY_PET -> Guardian（守护者）
 *   - SUMMON_CATEGORY_PUPPET -> Puppet（傀儡）
 *   - SUMMON_CATEGORY_VEHICLE -> Minion（随从）
 *   - SUMMON_TYPE_TOTEM/LIGHTWELL -> Totem（图腾）
 *   - SUMMON_TYPE_MINIPET -> Minion（小宠物）
 */
TempSummon* Transport::SummonPassenger(uint32 entry, Position const& pos, TempSummonType summonType, SummonPropertiesEntry const* properties /*= nullptr*/, uint32 duration /*= 0*/, Unit* summoner /*= nullptr*/, uint32 spellId /*= 0*/, uint32 vehId /*= 0*/)
{
    Map* map = FindMap();
    if (!map)
        return nullptr;

    // 根据召唤属性确定生物类型掩码
    uint32 mask = UNIT_MASK_SUMMON;
    if (properties)
    {
        switch (properties->Control)
        {
            case SUMMON_CATEGORY_PET:
                mask = UNIT_MASK_GUARDIAN;
                break;
            case SUMMON_CATEGORY_PUPPET:
                mask = UNIT_MASK_PUPPET;
                break;
            case SUMMON_CATEGORY_VEHICLE:
                mask = UNIT_MASK_MINION;
                break;
            case SUMMON_CATEGORY_WILD:
            case SUMMON_CATEGORY_ALLY:
            case SUMMON_CATEGORY_UNK:
            {
                switch (properties->Title)
                {
                    case SUMMON_TYPE_MINION:
                    case SUMMON_TYPE_GUARDIAN:
                    case SUMMON_TYPE_GUARDIAN2:
                        mask = UNIT_MASK_GUARDIAN;
                        break;
                    case SUMMON_TYPE_TOTEM:
                    case SUMMON_TYPE_LIGHTWELL:
                        mask = UNIT_MASK_TOTEM;
                        break;
                    case SUMMON_TYPE_VEHICLE:
                    case SUMMON_TYPE_VEHICLE2:
                        mask = UNIT_MASK_SUMMON;
                        break;
                    case SUMMON_TYPE_MINIPET:
                        mask = UNIT_MASK_MINION;
                        break;
                    default:
                        // 特殊标志：镜像、石像鬼等
                        if (properties->Flags & 512)
                            mask = UNIT_MASK_GUARDIAN;
                        break;
                }
                break;
            }
            default:
                return nullptr;
        }
    }

    // 获取相位掩码
    uint32 phase = PHASEMASK_NORMAL;
    if (summoner)
        phase = summoner->GetPhaseMask();

    // 根据掩码创建相应类型的临时召唤物
    TempSummon* summon = nullptr;
    switch (mask)
    {
        case UNIT_MASK_SUMMON:
            summon = new TempSummon(properties, summoner, false);
            break;
        case UNIT_MASK_GUARDIAN:
            summon = new Guardian(properties, summoner, false);
            break;
        case UNIT_MASK_PUPPET:
            summon = new Puppet(properties, summoner);
            break;
        case UNIT_MASK_TOTEM:
            summon = new Totem(properties, summoner);
            break;
        case UNIT_MASK_MINION:
            summon = new Minion(properties, summoner, false);
            break;
    }

    // 计算世界坐标
    float x, y, z, o;
    pos.GetPosition(x, y, z, o);
    CalculatePassengerPosition(x, y, z, &o);

    // 创建生物实体
    if (!summon->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, phase, entry, { x, y, z, o }, nullptr, vehId))
    {
        delete summon;
        return nullptr;
    }

    // 设置创建法术
    summon->SetCreatedBySpell(spellId);

    // 设置运输工具相关属性
    summon->SetTransport(this);
    summon->AddUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
    summon->m_movementInfo.transport.guid = GetGUID();
    summon->m_movementInfo.transport.pos.Relocate(pos);
    summon->Relocate(x, y, z, o);
    summon->SetHomePosition(x, y, z, o);
    summon->SetTransportHomePosition(pos);

    /// @HACK - 运输工具模型不会加入地图的动态视线计算
    ///         因为当前的 GameObjectModel 无法在不重建的情况下移动
    summon->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);

    // 初始化属性
    summon->InitStats(duration);

    // 添加到地图
    if (!map->AddToMap<Creature>(summon))
    {
        delete summon;
        return nullptr;
    }

    // 注册为静态乘客
    _staticPassengers.insert(summon);

    // 初始化召唤
    summon->InitSummon();
    summon->SetTempSummonType(summonType);

    return summon;
}

/**
 * @brief 更新运输工具位置
 *
 * @param x 新X坐标
 * @param y 新Y坐标
 * @param z 新Z坐标
 * @param o 新朝向
 *
 * 更新运输工具的世界位置并同步所有乘客的位置。
 * 同时检查网格状态变化，决定加载或卸载静态乘客。
 *
 * 四种触发乘客加载/卸载的场景：
 *   1. 运输工具从不活跃网格移动到活跃网格
 *   2. 运输工具所在的网格变为活跃（在Update中处理）
 *   3. 运输工具从活跃网格移动到不活跃网格
 *   4. 运输工具所在的网格卸载（由网格卸载处理）
 */
void Transport::UpdatePosition(float x, float y, float z, float o)
{
    // 检查新位置是否在活跃网格中
    bool newActive = GetMap()->IsGridLoaded(x, y);
    // 记录旧网格位置
    Cell oldCell(GetPositionX(), GetPositionY());

    // 更新运输工具位置
    Relocate(x, y, z, o);
    // 更新模型位置（用于渲染和碰撞）
    UpdateModelPosition();

    // 更新所有动态乘客的位置
    UpdatePassengerPositions(_passengers);

    /* 触发乘客加载/卸载的四种场景：
       1. 运输工具从不活跃网格移动到活跃网格
       2. 运输工具所在的网格变为活跃（在Update中处理）
       3. 运输工具从活跃网格移动到不活跃网格
       4. 运输工具所在的网格卸载
    */
    if (_staticPassengers.empty() && newActive) // 场景1：移动到活跃网格
        LoadStaticPassengers();
    else if (!_staticPassengers.empty() && !newActive && oldCell.DiffGrid(Cell(GetPositionX(), GetPositionY()))) // 场景3：移动到不活跃网格
        UnloadStaticPassengers();
    else
        // 否则只更新静态乘客位置
        UpdatePassengerPositions(_staticPassengers);
    // 场景4由网格卸载处理
}

/**
 * @brief 加载静态乘客
 *
 * 当运输工具移动到活跃网格时，从数据库加载预定义的NPC和游戏对象。
 * 静态乘客是预先定义在运输工具关联地图上的固定实体。
 *
 * 加载流程：
 *   1. 获取运输工具关联的地图ID
 *   2. 获取该地图的所有对象GUID
 *   3. 遍历并创建所有游戏对象和生物
 */
void Transport::LoadStaticPassengers()
{
    // 获取运输工具关联的地图ID（用于静态乘客数据）
    uint32 mapId = GetGOInfo()->moTransport.mapID;
    if (!mapId)
        return;

    // 获取地图上所有对象的GUID
    CellObjectGuidsMap const* cells = sObjectMgr->GetMapObjectGuids(mapId, GetMap()->GetSpawnMode());
    if (!cells)
        return;

    // 遍历所有单元格的对象
    for (auto const& [cellId, guids] : *cells)
    {
        // 创建运输工具上的游戏对象
        for (ObjectGuid::LowType spawnId : guids.gameobjects)
            CreateGOPassenger(spawnId, sObjectMgr->GetGameObjectData(spawnId));

        // 创建运输工具上的生物
        for (ObjectGuid::LowType spawnId : guids.creatures)
            CreateNPCPassenger(spawnId, sObjectMgr->GetCreatureData(spawnId));
    }
}

/**
 * @brief 卸载静态乘客
 *
 * 当运输工具进入非活跃网格或被删除时，卸载所有静态乘客。
 * 通过将对象添加到移除列表来触发卸载过程。
 */
void Transport::UnloadStaticPassengers()
{
    // 循环移除所有静态乘客
    while (!_staticPassengers.empty())
    {
        WorldObject* obj = *_staticPassengers.begin();
        // 添加到移除列表（也会从 _staticPassengers 中移除）
        obj->AddObjectToRemoveList();
    }
}

/**
 * @brief 启用/禁用移动
 *
 * @param enabled true启用移动，false停止移动
 *
 * 用于可停止的运输工具（如电梯、可控制船只）。
 * 设置 _pendingStop 标志，运输工具将在下一个停靠点停止。
 *
 * 前置条件：运输工具模板的 canBeStopped 属性为 true
 */
void Transport::EnableMovement(bool enabled)
{
    // 只有可停止的运输工具才能调用此方法
    if (!GetGOInfo()->moTransport.canBeStopped)
        return;

    // 设置等待停止标志
    _pendingStop = !enabled;
}

/**
 * @brief 移动到下一个路径关键帧
 *
 * 更新 _currentFrame 和 _nextFrame 迭代器，
 * 并重置到达/离开事件标志，为下一帧做准备。
 *
 * 如果到达路径末尾，_nextFrame 会循环回到开始。
 */
void Transport::MoveToNextWaypoint()
{
    // 重置事件触发标志
    _triggeredArrivalEvent = false;
    _triggeredDepartureEvent = false;

    // 移动关键帧迭代器
    _currentFrame = _nextFrame++;
    // 循环路径
    if (_nextFrame == GetKeyFrames().end())
        _nextFrame = GetKeyFrames().begin();
}

/**
 * @brief 计算当前路径段的位置百分比
 *
 * @param now 当前时间（秒）
 * @return 在当前路径段的位置百分比 [0.0, 1.0]
 *
 * 根据加速/减速运动学公式，计算运输工具在当前路径段的精确位置。
 * 物理模型：
 *   - 从停靠点出发：匀加速运动，直到达到最大速度
 *   - 匀速阶段：以最大速度匀速运动
 *   - 到达停靠点：匀减速运动，直到停止
 *
 * 计算方式：选择从最近的停靠点开始计算，减少累积误差
 */
float Transport::CalculateSegmentPos(float now)
{
    KeyFrame const& frame = *_currentFrame;
    const float speed = float(m_goInfo->moTransport.moveSpeed);      // 最大速度
    const float accel = float(m_goInfo->moTransport.accelRate);       // 加速度

    // 计算从上一个停靠点经过的时间和到下一个停靠点的时间
    float timeSinceStop = frame.TimeFrom + (now - (1.0f / float(IN_MILLISECONDS)) * frame.DepartureTime);
    float timeUntilStop = frame.TimeTo - (now - (1.0f / float(IN_MILLISECONDS)) * frame.DepartureTime);

    float segmentPos, dist;
    float accelTime = _transportInfo->accelTime;   // 加速到最大速度所需时间
    float accelDist = _transportInfo->accelDist;   // 加速阶段移动的距离

    // 从最近的停靠点开始计算，减少计算复杂度
    if (timeSinceStop < timeUntilStop)
    {
        // 正在离开停靠点（加速或刚达到匀速）
        if (timeSinceStop < accelTime)
            // 匀加速阶段：s = 0.5 * a * t^2
            dist = 0.5f * accel * timeSinceStop * timeSinceStop;
        else
            // 匀速阶段：加速距离 + 匀速距离
            dist = accelDist + (timeSinceStop - accelTime) * speed;
        segmentPos = dist - frame.DistSinceStop;
    }
    else
    {
        // 正在接近停靠点（减速或刚从匀速开始减速）
        if (timeUntilStop < _transportInfo->accelTime)
            // 匀减速阶段：s = 0.5 * a * t^2
            dist = 0.5f * accel * timeUntilStop * timeUntilStop;
        else
            // 匀速阶段：加速距离 + 匀速距离
            dist = accelDist + (timeUntilStop - accelTime) * speed;
        segmentPos = frame.DistUntilStop - dist;
    }

    // 返回在当前段的百分比位置
    return segmentPos / frame.NextDistFromPrev;
}

/**
 * @brief 传送运输工具到新地图
 *
 * @param newMapid 目标地图ID
 * @param x 目标X坐标
 * @param y 目标Y坐标
 * @param z 目标Z坐标
 * @param o 目标朝向
 * @return true 需要延迟传送（跨地图）
 * @return false 已完成传送（同地图）
 *
 * 处理运输工具的地图切换：
 *   - 同地图：直接更新位置并传送乘客中的玩家
 *   - 跨地图：设置延迟传送标志，卸载静态乘客，在 DelayedTeleportTransport() 中处理
 *
 * 调用时机：到达传送类型的关键帧时
 */
bool Transport::TeleportTransport(uint32 newMapid, float x, float y, float z, float o)
{
    Map const* oldMap = GetMap();

    // 检查是否跨地图传送
    if (oldMap->GetId() != newMapid)
    {
        // 跨地图传送：设置延迟标志，在 DelayedUpdate 中处理
        _delayedTeleport = true;
        // 卸载静态乘客（新地图会重新加载）
        UnloadStaticPassengers();
        return true;
    }
    else
    {
        // 同地图传送：直接传送玩家并更新位置
        // 传送玩家（玩家需要知道位置变化）
        for (PassengerSet::iterator itr = _passengers.begin(); itr != _passengers.end(); ++itr)
        {
            if ((*itr)->GetTypeId() == TYPEID_PLAYER)
            {
                // 如果玩家在载具上，且载具也在运输工具上，
                // 则由载具更新乘客位置，跳过
                if (Unit* veh = (*itr)->ToUnit()->GetVehicleBase())
                    if (veh->GetTransport() == this)
                        continue;

                // 计算玩家目标位置
                float destX, destY, destZ, destO;
                (*itr)->m_movementInfo.transport.pos.GetPosition(destX, destY, destZ, destO);
                TransportBase::CalculatePassengerPosition(destX, destY, destZ, &destO, x, y, z, o);

                // 传送玩家
                (*itr)->ToPlayer()->TeleportTo(newMapid, destX, destY, destZ, destO,
                    TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET | TELE_TO_TRANSPORT_TELEPORT);
            }
        }

        // 更新运输工具位置
        UpdatePosition(x, y, z, o);
        return false;
    }
}

/**
 * @brief 执行延迟的地图传送
 *
 * 在跨地图传送时，实际的地图切换操作在此执行：
 *   1. 从当前地图移除运输工具
 *   2. 设置新地图
 *   3. 传送所有乘客到新地图
 *   4. 将运输工具添加到新地图
 *
 * 乘客处理规则：
 *   - 玩家：执行跨地图传送
 *   - 动态对象：直接删除
 *   - 其他：从运输工具移除
 *
 * 调用时机：DelayedUpdate() 中检测到 _delayedTeleport 标志时
 */
void Transport::DelayedTeleportTransport()
{
    // 检查是否有延迟传送请求
    if (!_delayedTeleport)
        return;

    // 清除延迟传送标志
    _delayedTeleport = false;

    // 获取目标地图
    Map* newMap = sMapMgr->CreateBaseMap(_nextFrame->Node->ContinentID);
    // 从当前地图移除运输工具
    GetMap()->RemoveFromMap<Transport>(this, false);
    // 设置新地图
    SetMap(newMap);

    // 获取目标位置
    float x = _nextFrame->Node->Loc.X,
          y = _nextFrame->Node->Loc.Y,
          z = _nextFrame->Node->Loc.Z,
          o = _nextFrame->InitialOrientation;

    // 传送所有乘客
    for (_passengerTeleportItr = _passengers.begin(); _passengerTeleportItr != _passengers.end();)
    {
        WorldObject* obj = (*_passengerTeleportItr++);

        // 计算乘客目标位置
        float destX, destY, destZ, destO;
        obj->m_movementInfo.transport.pos.GetPosition(destX, destY, destZ, destO);
        TransportBase::CalculatePassengerPosition(destX, destY, destZ, &destO, x, y, z, o);

        // 根据对象类型处理
        switch (obj->GetTypeId())
        {
            case TYPEID_PLAYER:
                // 传送玩家到新地图
                if (!obj->ToPlayer()->TeleportTo(_nextFrame->Node->ContinentID, destX, destY, destZ, destO, TELE_TO_NOT_LEAVE_TRANSPORT))
                    RemovePassenger(obj);  // 传送失败则移除
                break;
            case TYPEID_DYNAMICOBJECT:
                // 动态对象直接删除
                obj->AddObjectToRemoveList();
                break;
            default:
                // 其他对象直接从运输工具移除
                RemovePassenger(obj);
                break;
        }
    }

    // 更新运输工具位置
    Relocate(x, y, z, o);
    // 添加到新地图
    GetMap()->AddToMap<Transport>(this);
}

/**
 * @brief 更新所有乘客位置
 *
 * @param passengers 要更新的乘客集合
 *
 * 遍历乘客集合，根据运输工具的新位置计算并更新每个乘客的世界坐标。
 * 处理不同类型乘客的位置更新：
 *   - 生物：使用 CreatureRelocation，同时更新家位置
 *   - 玩家：使用 PlayerRelocation，跳过正在传送的玩家
 *   - 游戏对象：使用 GameObjectRelocation，同时更新固定位置
 *   - 动态对象：使用 DynamicObjectRelocation
 *
 * 注意事项：
 *   - 跳过不在同一地图的乘客（传送过程中）
 *   - 跳过在载具上的乘客（由载具更新）
 *   - 不使用 Unit::UpdatePosition 避免移除光环
 *   - 更新载具乘客的位置（递归）
 */
void Transport::UpdatePassengerPositions(PassengerSet& passengers)
{
    for (PassengerSet::iterator itr = passengers.begin(); itr != passengers.end(); ++itr)
    {
        WorldObject* passenger = *itr;

        // 运输工具已传送但乘客还未传送（玩家可能延迟）
        if (passenger->GetMap() != GetMap())
            continue;

        // 如果乘客在载具上，假设载具也在运输工具上
        // 由载具来更新其乘客
        if (Unit* unit = passenger->ToUnit())
            if (unit->GetVehicle())
                continue;

        // 不使用 Unit::UpdatePosition，我们不希望像普通移动那样移除光环
        float x, y, z, o;
        passenger->m_movementInfo.transport.pos.GetPosition(x, y, z, o);
        CalculatePassengerPosition(x, y, z, &o);

        // 根据类型更新位置
        switch (passenger->GetTypeId())
        {
            case TYPEID_UNIT:
            {
                Creature* creature = passenger->ToCreature();
                // 重定位生物
                GetMap()->CreatureRelocation(creature, x, y, z, o, false);
                // 更新家位置
                creature->GetTransportHomePosition(x, y, z, o);
                CalculatePassengerPosition(x, y, z, &o);
                creature->SetHomePosition(x, y, z, o);
                break;
            }
            case TYPEID_PLAYER:
                // 只重定位在世界中的玩家，跳过正在登录/传送的玩家
                if (passenger->IsInWorld() && !passenger->ToPlayer()->IsBeingTeleported())
                {
                    GetMap()->PlayerRelocation(passenger->ToPlayer(), x, y, z, o);
                    // 更新下落信息
                    passenger->ToPlayer()->SetFallInformation(0, passenger->GetPositionZ());
                }
                break;
            case TYPEID_GAMEOBJECT:
                // 重定位游戏对象
                GetMap()->GameObjectRelocation(passenger->ToGameObject(), x, y, z, o, false);
                // 更新固定位置
                passenger->ToGameObject()->RelocateStationaryPosition(x, y, z, o);
                break;
            case TYPEID_DYNAMICOBJECT:
                // 重定位动态对象
                GetMap()->DynamicObjectRelocation(passenger->ToDynObject(), x, y, z, o);
                break;
            default:
                break;
        }

        // 如果乘客是载具，递归更新载具上的乘客
        if (Unit* unit = passenger->ToUnit())
            if (Vehicle* vehicle = unit->GetVehicleKit())
                vehicle->RelocatePassengers();
    }
}

/**
 * @brief 触发关键帧事件（如果有）
 *
 * @param node 关键帧数据
 * @param departure true为离开事件，false为到达事件
 *
 * 检查关键帧是否有到达/离开事件ID，如果有则触发相应的脚本事件。
 * 事件由 sEventScripts 表定义，可执行各种脚本动作。
 *
 * 调用时机：到达或离开关键帧时
 */
void Transport::DoEventIfAny(KeyFrame const& node, bool departure)
{
    // 获取事件ID（根据到达或离开类型）
    if (uint32 eventid = departure ? node.Node->DepartureEventID : node.Node->ArrivalEventID)
    {
        TC_LOG_DEBUG("maps.script", "Taxi {} event {} of node {} of {} path", departure ? "departure" : "arrival", eventid, node.Node->NodeIndex, GetName());
        // 启动事件脚本
        GetMap()->ScriptsStart(sEventScripts, eventid, this, this);
        // 通知游戏对象事件触发
        EventInform(eventid);
    }
}

/**
 * @brief 构建更新数据包
 *
 * @param data_map 更新数据映射表（玩家 -> 更新数据）
 *
 * 为地图上的所有玩家构建运输工具的更新数据包。
 * 运输工具对所有可见玩家发送相同的更新数据。
 *
 * 调用时机：对象状态变化需要同步给客户端时
 */
void Transport::BuildUpdate(UpdateDataMapType& data_map)
{
    // 获取地图上的所有玩家
    Map::PlayerList const& players = GetMap()->GetPlayers();
    if (players.isEmpty())
        return;

    // 为每个玩家构建更新数据
    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        BuildFieldsUpdate(itr->GetSource(), data_map);

    // 清除更新掩码
    ClearUpdateMask(true);
}

/**
 * @brief 获取调试信息
 *
 * @return 调试信息字符串
 *
 * 返回包含运输工具详细信息的字符串，用于GM调试。
 * 目前直接返回父类的调试信息。
 */
std::string Transport::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << GameObject::GetDebugInfo();
    return sstr.str();
}
