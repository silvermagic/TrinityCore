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
 * @file CinematicMgr.cpp
 * @brief 过场动画管理器实现文件
 *
 * 本文件实现了 CinematicMgr 类，负责管理玩家的过场动画播放流程。
 * 过场动画使用飞越相机（FlyBy Camera）系统，通过创建临时可视对象
 * 作为摄像机视角点，实现游戏内电影效果。
 *
 * 主要功能包括：
 * - 过场动画的启动和结束控制
 * - 摄像机路径的插值计算和位置更新
 * - 视角切换和远程视野对象管理
 *
 * @see CinematicMgr.h 头文件定义
 * @see FlyByCamera 飞越相机数据结构
 * @see Player 玩家实体类
 */

#include "CinematicMgr.h"
#include "Map.h"
#include "M2Stores.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "TemporarySummon.h"

/**
 * @brief 构造函数 - 初始化过场动画管理器
 *
 * 初始化所有成员变量为默认状态，建立与玩家对象的关联。
 * 此构造函数在 Player 对象创建时调用。
 *
 * @param playerref 关联的玩家对象指针，管理器将为此玩家处理过场动画
 *
 * @note 管理器生命周期与玩家对象绑定，玩家销毁时管理器随之销毁
 * @note 初始状态下没有活动的过场动画
 */
CinematicMgr::CinematicMgr(Player* playerref)
{
    player = playerref;                                   // 关联的玩家对象
    m_cinematicDiff = 0;                                  // 当前动画时间戳（毫秒）
    m_lastCinematicCheck = 0;                             // 上次检查时间戳
    m_activeCinematicCameraId = 0;                        // 当前活动的摄像机ID（0表示无活动动画）
    m_cinematicLength = 0;                                // 动画总时长（毫秒）
    m_cinematicCamera = nullptr;                          // 飞越相机数据指针
    m_remoteSightPosition = Position(0.0f, 0.0f, 0.0f);   // 远程视野位置（用于视角切换）
    m_CinematicObjectGUID = ObjectGuid::Empty;            // 过场动画对象的GUID
}

/**
 * @brief 析构函数 - 清理过场动画资源
 *
 * 如果有过场动画正在播放，确保正确结束动画并清理资源。
 * 这包括移除视角对象和恢复玩家正常视角。
 *
 * @note 析构时如果有活动的摄像机，会调用 EndCinematic() 确保资源正确释放
 * @warning 不要手动删除管理器，它由玩家对象管理
 */
CinematicMgr::~CinematicMgr()
{
    // 如果有过场动画正在播放，强制结束
    if (m_cinematicCamera && m_activeCinematicCameraId)
        EndCinematic();
}

/**
 * @brief 开始播放过场动画
 *
 * 初始化过场动画的播放流程，包括：
 * 1. 从数据库加载飞越相机路径数据
 * 2. 在相机起始位置创建临时可视对象作为视角点
 * 3. 将玩家视角切换到该临时对象
 * 4. 记录动画总时长
 *
 * 此函数在玩家触发过场动画时调用，例如进入新区域或完成任务。
 *
 * @pre 必须先设置 m_activeCinematicCameraId 为有效的摄像机ID
 * @post 如果成功，玩家将进入过场动画视角模式
 *
 * @note 使用 VISUAL_WAYPOINT（可视路点）作为摄像机对象，该对象对玩家不可见
 * @note 临时对象会在 5 分钟后自动消失（安全机制）
 * @note 动画时间戳从 0 开始计数
 *
 * @see EndCinematic() 结束过场动画
 * @see UpdateCinematicLocation() 更新摄像机位置
 */
void CinematicMgr::BeginCinematic()
{
    // 安全检查：确保已设置活动的摄像机ID
    if (m_activeCinematicCameraId == 0)
        return;

    // 从 M2Stores 获取飞越相机数据
    if (std::vector<FlyByCamera> const* flyByCameras = GetFlyByCameras(m_activeCinematicCameraId))
    {
        // 初始化时间戳，并设置相机数据引用
        m_cinematicDiff = 0;
        m_cinematicCamera = flyByCameras;

        auto camitr = m_cinematicCamera->begin();
        if (camitr != m_cinematicCamera->end())
        {
            Position const& pos = camitr->locations;
            // 验证起始位置是否有效
            if (!pos.IsPositionValid())
                return;

            // 预加载地图网格以确保相机位置所在的区域已加载
            player->GetMap()->LoadGrid(pos.GetPositionX(), pos.GetPositionY());

            // 在相机起始位置召唤临时可视对象作为摄像机视角点
            // VISUAL_WAYPOINT 是一个不可见的辅助对象，用于承载视角
            if (TempSummon* cinematicObject = player->SummonCreature(VISUAL_WAYPOINT, pos.m_positionX, pos.m_positionY, pos.m_positionZ, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 5min))
            {
                m_CinematicObjectGUID = cinematicObject->GetGUID();
                // 激活对象以确保其正常更新
                cinematicObject->setActive(true);
                // 将玩家视角切换到此临时对象
                player->SetViewpoint(cinematicObject, true);
            }

            // 获取动画总时长（最后一个相机点的时间戳）
            m_cinematicLength = flyByCameras->back().timeStamp;
        }
    }
}

/**
 * @brief 结束过场动画
 *
 * 清理过场动画相关资源并恢复玩家正常视角：
 * 1. 重置所有动画相关状态变量
 * 2. 恢复玩家视角到正常状态
 * 3. 移除临时创建的摄像机对象
 *
 * 此函数在以下情况调用：
 * - 动画自然播放完成
 * - 玩家主动跳过动画
 * - 管理器析构时强制结束
 * - 动画超时（10秒容错）
 *
 * @pre 必须有活动的过场动画（m_activeCinematicCameraId != 0）
 * @post 所有动画状态被重置，临时对象被标记删除
 *
 * @note 使用 AddObjectToRemoveList 而非立即删除，保证对象安全移除
 * @note 如果视角对象已不存在，仍然会清理状态但不影响玩家
 *
 * @see BeginCinematic() 开始过场动画
 */
void CinematicMgr::EndCinematic()
{
    // 安全检查：如果没有活动的过场动画则直接返回
    if (m_activeCinematicCameraId == 0)
        return;

    // 重置所有动画状态
    m_cinematicDiff = 0;
    m_cinematicCamera = nullptr;
    m_activeCinematicCameraId = 0;

    // 清理临时摄像机对象
    if (!m_CinematicObjectGUID.IsEmpty())
    {
        // 先恢复玩家视角
        if (WorldObject* vpObject = player->GetViewpoint())
            if (vpObject->GetGUID() == m_CinematicObjectGUID)
                player->SetViewpoint(vpObject, false);

        // 标记临时对象为待删除状态
        // 使用 ObjectAccessor 安全地获取对象，即使对象已被删除也不会出错
        if (WorldObject* cinematicObject = ObjectAccessor::GetWorldObject(*player, m_CinematicObjectGUID))
            cinematicObject->AddObjectToRemoveList();
    }
}

/**
 * @brief 更新过场动画摄像机位置
 *
 * 根据当前动画时间戳计算摄像机应在的位置，并通过移动视角对象来实现平滑的相机运动。
 * 使用线性插值算法在相机路径点之间平滑过渡。
 *
 * 核心算法流程：
 * 1. 计算当前时间点对应的相机位置和朝向
 * 2. 应用前瞻偏移（Lookahead）以提前调整视角方向
 * 3. 在相邻两个关键帧之间进行线性插值
 * 4. 移动视角对象到插值位置
 * 5. 检测动画超时并强制结束
 *
 * @param diff 距离上次更新的时间间隔（毫秒），当前未使用但保留接口一致性
 *
 * @pre 必须有活动的过场动画和有效的相机数据
 * @post 摄像机位置更新到当前时间戳对应的位置
 *
 * @note 前瞻机制（CINEMATIC_LOOKAHEAD）用于处理相机朝向，
 *       某些种族的相机角度需要根据前进方向调整
 * @note 使用 500.0f 的固定移动速度确保摄像机平滑移动
 * @note 设置了 10 秒超时机制，防止客户端未发送结束包导致动画卡住
 *
 * @warning 如果视角对象丢失，会自动结束动画
 *
 * @see BeginCinematic() 开始过场动画（初始化相机）
 * @see EndCinematic() 结束过场动画
 */
void CinematicMgr::UpdateCinematicLocation(uint32 /*diff*/)
{
    // 前置检查：确保有活动的过场动画和有效的相机数据
    if (m_activeCinematicCameraId == 0 || !m_cinematicCamera || m_cinematicCamera->size() == 0)
        return;

    // 用于存储相邻两个关键帧的位置和时间戳
    Position lastPosition;      // 前一个关键帧位置
    uint32 lastTimestamp = 0;   // 前一个关键帧时间戳
    Position nextPosition;      // 后一个关键帧位置
    uint32 nextTimestamp = 0;   // 后一个关键帧时间戳

    // 第一遍遍历：计算当前相机位置和移动方向
    for (FlyByCamera cam : *m_cinematicCamera)
    {
        if (cam.timeStamp > m_cinematicDiff)
        {
            // 找到当前时间点之后的第一个关键帧
            nextPosition.Relocate(cam.locations);
            nextTimestamp = cam.timeStamp;
            break;
        }
        // 记录当前时间点之前最后一个关键帧
        lastPosition.Relocate(cam.locations);
        lastTimestamp = cam.timeStamp;
    }

    // 计算相机移动方向的相对角度
    // 这个角度用于调整前瞻偏移量
    float angle = lastPosition.GetAbsoluteAngle(&nextPosition);
    angle -= lastPosition.GetOrientation();
    if (angle < 0)
        angle += 2 * float(M_PI);

    // 计算前瞻时间偏移
    // 前瞻机制让相机提前"看"向前进方向，提供更自然的视角过渡
    int32 workDiff = m_cinematicDiff;

    // 根据相机朝向调整前瞻量
    // 例如人类种族的相机朝向后方，需要不同的前瞻处理
    workDiff += static_cast<int32>(float(CINEMATIC_LOOKAHEAD) * cos(angle));

    // 边界检查：确保不超过动画末尾时间
    auto endItr = m_cinematicCamera->rbegin();
    if (endItr != m_cinematicCamera->rend() && workDiff > static_cast<int32>(endItr->timeStamp))
        workDiff = endItr->timeStamp;

    // 边界检查：不能回溯到动画开始之前
    if (workDiff < 0)
        workDiff = m_cinematicDiff;

    // 第二遍遍历：基于调整后的时间戳重新查找关键帧
    for (FlyByCamera cam : *m_cinematicCamera)
    {
        if (static_cast<int32>(cam.timeStamp) >= workDiff)
        {
            nextPosition.Relocate(cam.locations);
            nextTimestamp = cam.timeStamp;
            break;
        }
        lastPosition.Relocate(cam.locations);
        lastTimestamp = cam.timeStamp;
    }

    // 确保不超过下一个关键帧的时间
    if (workDiff > static_cast<int32>(nextTimestamp))
        workDiff = static_cast<int32>(nextTimestamp);

    // 线性插值计算当前位置
    // 公式：position = lastPos + (nextPos - lastPos) * (elapsedTime / totalTime)
    uint32 timeDiff = nextTimestamp - lastTimestamp;    // 两帧间的时间差
    uint32 interDiff = workDiff - lastTimestamp;        // 已经过的时间
    float xDiff = nextPosition.m_positionX - lastPosition.m_positionX;
    float yDiff = nextPosition.m_positionY - lastPosition.m_positionY;
    float zDiff = nextPosition.m_positionZ - lastPosition.m_positionZ;

    // 计算插值位置
    Position interPosition(
        lastPosition.m_positionX + (xDiff * (float(interDiff) / float(timeDiff))),
        lastPosition.m_positionY + (yDiff * (float(interDiff) / float(timeDiff))),
        lastPosition.m_positionZ + (zDiff * (float(interDiff) / float(timeDiff)))
    );

    // 验证视角对象是否仍然有效
    WorldObject* vpObject = player ? player->GetViewpoint() : nullptr;
    if (!vpObject || vpObject->GetGUID() != m_CinematicObjectGUID)
    {
        // 视角对象丢失，强制结束动画
        EndCinematic();
        return;
    }

    // 移动视角对象到插值位置
    // 使用高速（500.0f）确保摄像机能跟上动画进度
    // 远程视野对象通过移动向玩家发送视角更新
    if (vpObject->IsCreature() && interPosition.IsPositionValid())
        vpObject->ToCreature()->MonsterMoveWithSpeed(interPosition.m_positionX, interPosition.m_positionY, interPosition.m_positionZ, 500.0f, false, true);

    // 超时保护机制
    // 如果动画时间超过总时长 10 秒仍未收到结束包，强制结束动画
    // 这可以防止客户端异常导致的动画卡死
    if (m_cinematicDiff > m_cinematicLength + 10 * IN_MILLISECONDS)
        EndCinematic();
}
