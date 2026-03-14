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
 * @file CinematicMgr.h
 * @brief 过场动画管理器模块
 *
 * 本文件定义了 CinematicMgr 类，负责管理玩家观看过场动画时的摄像机系统。
 * 包括摄像机位置更新、远程视野处理、飞越摄像机路径计算等功能。
 * 该系统确保玩家在观看过场动画时能够看到正确的场景视角。
 */

#ifndef CinematicMgr_h__
#define CinematicMgr_h__

#include "Define.h"
#include "Object.h"

/**
 * @brief 摄像机前向预测时间（毫秒）
 *
 * 用于在过场动画播放时提前计算摄像机位置，确保视角转换的平滑性。
 * 设置为2秒，表示摄像机系统会提前2秒计算下一个视角位置。
 */
#define CINEMATIC_LOOKAHEAD (2 * IN_MILLISECONDS)

/**
 * @brief 摄像机位置更新间隔（毫秒）
 *
 * 定义过场动画摄像机位置更新的最小时间间隔。
 * 设置为500毫秒（0.5秒），平衡性能与视角平滑度。
 */
#define CINEMATIC_UPDATEDIFF 500

class Player;
struct FlyByCamera;

/**
 * @class CinematicMgr
 * @brief 过场动画管理器
 *
 * 负责管理玩家观看过场动画时的摄像机系统和远程视野。
 * 该类与 Player 类为友元关系，Player 可以直接访问其私有成员。
 *
 * 主要职责：
 * - 管理过场动画的启动和结束
 * - 控制摄像机位置更新和路径追踪
 * - 处理远程视野位置计算
 * - 维护摄像机对象的生命周期
 *
 * 使用流程：
 * 1. 调用 BeginCinematic() 启动过场动画
 * 2. 定期调用 UpdateCinematicLocation() 更新摄像机位置
 * 3. 调用 EndCinematic() 结束过场动画
 */
class TC_GAME_API CinematicMgr
{
    friend class Player;  ///< Player 类可以访问私有成员

public:
    /**
     * @brief 构造函数
     * @param playerref 关联的玩家对象指针
     *
     * 初始化过场动画管理器，关联指定的玩家对象。
     * 该管理器由 Player 对象拥有，生命周期与 Player 绑定。
     */
    explicit CinematicMgr(Player* playerref);

    /**
     * @brief 析构函数
     *
     * 清理过场动画相关资源，确保摄像机对象被正确释放。
     */
    ~CinematicMgr();

    /**
     * @brief 获取当前活动的过场动画摄像机ID
     * @return 当前摄像机ID，如果没有活动摄像机则返回0
     *
     * 此方法是内联函数，性能开销极小，可频繁调用。
     */
    uint32 GetActiveCinematicCamera() const { return m_activeCinematicCameraId; }

    /**
     * @brief 设置当前活动的过场动画摄像机ID
     * @param cinematicCameraId 摄像机ID，默认为0表示无摄像机
     *
     * 直接设置摄像机ID，不触发其他逻辑。
     * 主要用于内部状态管理。
     */
    void SetActiveCinematicCamera(uint32 cinematicCameraId = 0) { m_activeCinematicCameraId = cinematicCameraId; }

    /**
     * @brief 检查玩家是否正在观看过场动画
     * @return 如果正在观看过场动画返回true，否则返回false
     *
     * 通过检查摄像机指针是否为空来判断状态。
     * 此方法是内联函数，性能开销极小。
     */
    bool IsOnCinematic() const { return (m_cinematicCamera != nullptr); }

    /**
     * @brief 启动过场动画
     *
     * 初始化过场动画系统，加载摄像机路径数据。
     * 设置必要的远程视野对象，使玩家能够看到摄像机视角。
     *
     * 调用时机：当玩家开始观看过场动画时由 Player 类调用。
     */
    void BeginCinematic();

    /**
     * @brief 结束过场动画
     *
     * 清理过场动画相关资源，释放摄像机对象。
     * 将玩家视角恢复到正常状态。
     *
     * 调用时机：当过场动画播放完成或被中断时调用。
     */
    void EndCinematic();

    /**
     * @brief 更新摄像机位置
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 根据时间进度更新摄像机位置和视角。
     * 使用插值算法计算摄像机路径上的当前位置。
     *
     * 性能考虑：
     * - 使用 CINEMATIC_UPDATEDIFF 控制更新频率
     * - 仅在过场动画期间调用
     * - 采用前向预测优化视角转换
     *
     * 调用时机：由 Player::Update() 定期调用，通常每个世界更新周期调用一次。
     */
    void UpdateCinematicLocation(uint32 diff);

private:
    Player*     player;  ///< 关联的玩家对象，用于访问玩家数据和操作玩家状态

protected:
    uint32      m_cinematicDiff;           ///< 当前摄像机时间偏移量（毫秒），用于计算摄像机在路径上的位置
    uint32      m_lastCinematicCheck;      ///< 上次摄像机位置检查的时间戳，用于控制更新频率
    uint32      m_activeCinematicCameraId; ///< 当前活动的过场动画摄像机ID，对应 CinematicCamera.dbc
    uint32      m_cinematicLength;         ///< 过场动画总时长（毫秒），用于判断动画是否结束
    std::vector<FlyByCamera> const* m_cinematicCamera; ///< 飞越摄像机路径数据指针，包含摄像机位置序列
    Position    m_remoteSightPosition;     ///< 远程视野位置，存储摄像机当前位置和朝向
    ObjectGuid  m_CinematicObjectGUID;     ///< 摄像机对象的GUID，用于创建临时的摄像机视野对象
};

#endif
