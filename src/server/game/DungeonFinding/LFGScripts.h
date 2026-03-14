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
 * @file LFGScripts.h
 * @brief LFG系统脚本接口 - 与游戏核心的交互钩子
 *
 * 本文件定义了LFG系统的脚本类，用于监听和处理玩家和队伍的事件。
 * 通过脚本系统，LFG模块可以在不修改核心代码的情况下响应游戏事件。
 *
 * 主要功能：
 * - 监听玩家登录、登出、地图变更事件
 * - 监听队伍成员变化、解散、队长变更事件
 * - 自动更新LFG系统状态
 * - 处理LFG相关的特殊逻辑
 *
 * 设计模式：
 * - 观察者模式：监听游戏事件
 * - 回调机制：通过脚本引擎调用LFG逻辑
 */

/*
 * 核心与LFG脚本的交互接口
 */

#include "Common.h"
#include "SharedDefines.h"
#include "ScriptMgr.h"

class Player;
class Group;

namespace lfg
{

/**
 * @brief LFG玩家脚本类 - 处理玩家相关事件
 *
 * 该类继承自PlayerScript，用于监听玩家的各种事件并做出相应处理。
 * 主要处理：
 * - 玩家登录：恢复LFG数据，检查队伍状态一致性
 * - 玩家登出：清理LFG数据，处理断线情况
 * - 地图变更：处理进入/离开副本，施放"幸运抽奖"增益
 */
class TC_GAME_API LFGPlayerScript : public PlayerScript
{
    public:
        LFGPlayerScript();

        /**
         * @brief 玩家登出事件
         * @param player 登出的玩家
         *
         * @details 处理逻辑：
         *          - 如果玩家没有队伍，离开LFG队列
         *          - 如果玩家断线且有队伍，标记为断线状态
         */
        void OnLogout(Player* player) override;

        /**
         * @brief 玩家登录事件
         * @param player 登录的玩家
         * @param loginFirst 是否首次登录
         *
         * @details 处理逻辑：
         *          - 检查队伍数据与LFG数据的一致性
         *          - 修复不一致的数据
         *          - 设置玩家阵营信息
         */
        void OnLogin(Player* player, bool loginFirst) override;

        /**
         * @brief 玩家地图变更事件
         * @param player 变更地图的玩家
         *
         * @details 处理逻辑：
         *          - 如果进入LFG副本：
         *            * 检查队伍有效性
         *            * 施放"幸运抽奖"增益（如果是随机副本）
         *          - 如果离开LFG副本：
         *            * 移除"幸运抽奖"增益
         *            * 如果队伍只剩1人，解散队伍
         */
        void OnMapChanged(Player* player) override;
};

/**
 * @brief LFG队伍脚本类 - 处理队伍相关事件
 *
 * 该类继承自GroupScript，用于监听队伍的各种事件并做出相应处理。
 * 主要处理：
 * - 成员加入/移除：更新LFG队伍数据
 * - 队伍解散：清理LFG数据
 * - 队长变更：更新LFG队长信息
 * - 成员邀请：处理邀请时的LFG逻辑
 */
class TC_GAME_API LFGGroupScript : public GroupScript
{
    public:
        LFGGroupScript();

        /**
         * @brief 成员加入队伍事件
         * @param group 队伍指针
         * @param guid 加入的玩家GUID
         *
         * @details 处理逻辑：
         *          - 如果加入的是队长，设置LFG队长
         *          - 如果加入的是普通成员：
         *            * 如果该成员在队列中，让其离开队列
         *            * 如果队伍在队列中，让队伍离开队列
         *          - 更新LFG队伍数据
         */
        void OnAddMember(Group* group, ObjectGuid guid) override;

        /**
         * @brief 成员离开队伍事件
         * @param group 队伍指针
         * @param guid 离开的玩家GUID
         * @param method 离开方式（自愿离开、被踢、断线等）
         * @param kicker 踢人的玩家GUID（如果是被踢）
         * @param reason 离开原因（如果有）
         *
         * @details 处理逻辑：
         *          - 如果是LFG队伍且被踢：
         *            * 初始化踢人投票
         *          - 否则：
         *            * 让玩家离开LFG
         *            * 如果是自愿离开且在副本中，施放"逃亡者"减益
         *            * 如果是被LFG踢出，移除副本冷却
         *            * 传送玩家出副本
         *          - 如果副本未完成，向队长发送继续招募提示
         */
        void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, char const* reason) override;

        /**
         * @brief 队伍解散事件
         * @param group 队伍指针
         *
         * @details 处理逻辑：
         *          - 清理队伍的所有LFG数据
         */
        void OnDisband(Group* group) override;

        /**
         * @brief 队长变更事件
         * @param group 队伍指针
         * @param newLeaderGuid 新队长的GUID
         * @param oldLeaderGuid 旧队长的GUID
         *
         * @details 处理逻辑：
         *          - 更新LFG系统的队长信息
         */
        void OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid) override;

        /**
         * @brief 邀请成员事件
         * @param group 队伍指针
         * @param guid 被邀请玩家的GUID
         *
         * @details 处理逻辑：
         *          - 如果队长在LFG队列中，让其离开队列
         */
        void OnInviteMember(Group* group, ObjectGuid guid) override;
};

/**
 * @brief 注册LFG脚本（内部函数）
 *
 * @details 该函数由脚本系统在启动时调用，创建并注册LFG脚本实例。
 *          不要手动调用此函数。
 */
/*keep private*/ void AddSC_LFGScripts();

} // namespace lfg
