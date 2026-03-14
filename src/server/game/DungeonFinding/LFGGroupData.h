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
 * @file LFGGroupData.h
 * @brief 寻组系统队伍数据管理模块
 *
 * 本文件定义了 LfgGroupData 类，用于存储和管理寻组系统中队伍的核心数据。
 * 它是寻组系统（LFG）的核心组件之一，负责维护队伍在排队、匹配和副本进行过程中的状态信息。
 *
 * 主要职责:
 * - 维护队伍的寻组状态（排队中、匹配成功、副本中等）
 * - 管理队伍成员列表和队长信息
 * - 存储队伍所排队的副本信息
 * - 处理投票踢人机制的状态和计数
 *
 * 相关模块:
 * - LFGMgr: 寻组系统管理器，协调所有寻组功能
 * - LFGPlayerData: 玩家寻组数据，存储单个玩家的寻组信息
 * - LFGQueue: 寻组队列，处理匹配逻辑
 *
 * @see LFGMgr.h 寻组系统管理器
 * @see LFGPlayerData.h 玩家寻组数据
 */

#ifndef _LFGGROUPDATA_H
#define _LFGGROUPDATA_H

#include "LFG.h"

namespace lfg
{

/**
 * @brief 寻组系统组枚举常量
 *
 * 定义了寻组系统中与队伍相关的常量值
 */
enum LfgGroupEnum
{
    LFG_GROUP_MAX_KICKS                           = 3,    ///< 队伍最大踢人次数限制
};

/**
 * @brief 寻组系统队伍数据管理类
 *
 * 该类存储并管理寻组系统中队伍的所有必要数据，包括队伍状态、成员列表、
 * 副本信息和投票踢人机制等。每个参与寻组系统的队伍都会有一个此类的实例
 * 来维护其寻组相关的数据。
 *
 * 主要功能包括:
 * - 管理队伍在寻组系统中的状态转换
 * - 维护队伍成员列表和队长信息
 * - 存储队伍当前排队的副本信息
 * - 处理投票踢人机制的计数和状态
 */
class TC_GAME_API LfgGroupData
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化寻组队伍数据，设置默认状态和初始值
         */
        LfgGroupData();

        /**
         * @brief 析构函数
         */
        ~LfgGroupData();

        /**
         * @brief 检查是否为寻组系统创建的队伍
         *
         * @return true 如果队伍是通过寻组系统创建的
         * @return false 如果队伍是普通队伍
         */
        bool IsLfgGroup();

        // ==================== 通用功能 ====================

        /**
         * @brief 设置队伍的寻组状态
         *
         * 将队伍当前状态保存为旧状态，并设置新的状态
         *
         * @param state 新的寻组状态
         * @see LfgState
         */
        void SetState(LfgState state);

        /**
         * @brief 恢复队伍的前一个状态
         *
         * 将队伍状态恢复到之前保存的旧状态
         */
        void RestoreState();

        /**
         * @brief 添加玩家到队伍
         *
         * 将指定玩家添加到队伍成员列表中
         *
         * @param guid 玩家的 GUID
         */
        void AddPlayer(ObjectGuid guid);

        /**
         * @brief 从队伍中移除玩家
         *
         * 从队伍成员列表中移除指定玩家
         *
         * @param guid 要移除的玩家的 GUID
         * @return uint8 移除后队伍中的玩家数量
         */
        uint8 RemovePlayer(ObjectGuid guid);

        /**
         * @brief 移除队伍中的所有玩家
         *
         * 清空队伍成员列表
         */
        void RemoveAllPlayers();

        /**
         * @brief 设置队伍队长
         *
         * @param guid 新队长的 GUID
         */
        void SetLeader(ObjectGuid guid);

        // ==================== 副本功能 ====================

        /**
         * @brief 设置队伍排队的副本
         *
         * @param dungeon 副本ID或副本条目ID
         */
        void SetDungeon(uint32 dungeon);

        // ==================== 投票踢人功能 ====================

        /**
         * @brief 减少剩余踢人次数
         *
         * 每次成功踢出玩家后调用，递减剩余踢人次数
         */
        void DecreaseKicksLeft();

        // ==================== 通用功能 - 查询接口 ====================

        /**
         * @brief 获取队伍当前的寻组状态
         *
         * @return LfgState 当前状态
         */
        LfgState GetState() const;

        /**
         * @brief 获取队伍的前一个状态
         *
         * @return LfgState 旧状态
         */
        LfgState GetOldState() const;

        /**
         * @brief 获取队伍成员列表
         *
         * @return GuidSet const& 队伍成员GUID集合的常量引用
         */
        GuidSet const& GetPlayers() const;

        /**
         * @brief 获取队伍成员数量
         *
         * @return uint8 队伍成员数量
         */
        uint8 GetPlayerCount() const;

        /**
         * @brief 获取当前队长
         *
         * @return ObjectGuid 队长的GUID
         */
        ObjectGuid GetLeader() const;

        // ==================== 副本功能 - 查询接口 ====================

        /**
         * @brief 获取队伍排队的副本
         *
         * @param asId true 返回副本ID，false 返回副本条目ID
         * @return uint32 副本ID或副本条目ID
         */
        uint32 GetDungeon(bool asId = true) const;

        // ==================== 投票踢人功能 - 查询接口 ====================

        /**
         * @brief 获取剩余踢人次数
         *
         * @return uint8 剩余踢人次数
         */
        uint8 GetKicksLeft() const;

        /**
         * @brief 设置投票踢人是否激活
         *
         * @param active true 激活投票踢人，false 取消激活
         */
        void SetVoteKick(bool active);

        /**
         * @brief 检查投票踢人是否处于激活状态
         *
         * @return true 投票踢人正在进行中
         * @return false 投票踢人未激活
         */
        bool IsVoteKickActive() const;

    private:
        // ==================== 通用数据 ====================
        LfgState m_State;                                  ///< 队伍在寻组系统中的当前状态
        LfgState m_OldState;                               ///< 队伍的前一个状态（用于状态恢复）
        ObjectGuid m_Leader;                               ///< 队长的 GUID
        GuidSet m_Players;                                 ///< 队伍成员 GUID 集合

        // ==================== 副本数据 ====================
        uint32 m_Dungeon;                                  ///< 队伍排队的副本条目ID

        // ==================== 投票踢人数据 ====================
        uint8 m_KicksLeft;                                 ///< 剩余踢人次数
        bool m_VoteKickActive;                             ///< 投票踢人是否激活
};

} // namespace lfg

#endif
