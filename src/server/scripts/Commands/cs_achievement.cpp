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
 * @file cs_achievement.cpp
 * @brief 成就管理命令模块
 *
 * 本模块实现了成就相关的游戏命令，主要用于GM调试和测试。
 * 主要功能包括：
 * - 为玩家添加成就
 *
 * 成就系统说明：
 * - 成就是游戏中玩家达成的特定目标的记录
 * - 成就完成可能奖励称号、物品、坐骑等
 * - 成就数据存储在数据库中，跨账号或角色共享
 *
 * 命令列表：
 * - .achievement add: 为选中的玩家添加指定成就
 */

/* ScriptData
Name: achievement_commandscript
%Complete: 100
Comment: All achievement related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AchievementMgr.h"
#include "Chat.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

/**
 * @class achievement_commandscript
 * @brief 成就命令脚本类
 *
 * 继承自 CommandScript 基类，实现成就管理相关的游戏命令。
 * 该类提供了GM用于测试和管理成就系统的命令接口。
 */
class achievement_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化成就命令脚本，设置脚本名称为 "achievement_commandscript"
     */
    achievement_commandscript() : CommandScript("achievement_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有成就相关的命令及其权限要求。
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的成就命令
     *
     * 命令结构：
     * - achievement add: 为选中的玩家添加指定成就
     *   - 权限要求: RBAC_PERM_COMMAND_ACHIEVEMENT_ADD
     *   - 仅游戏内可用（Console::No）
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "achievement add", HandleAchievementAddCommand, LANG_COMMAND_ACHIEVEMENT_ADD_HELP ,rbac::RBAC_PERM_COMMAND_ACHIEVEMENT_ADD, Console::No },
        };
        return commandTable;
    }

    /**
     * @brief 为玩家添加成就
     *
     * 为选中的目标玩家添加指定的成就。该命令主要用于GM调试和测试成就系统。
     *
     * @param handler 聊天命令处理器，用于发送消息和获取目标玩家
     * @param achievementEntry 成就条目指针，包含成就信息
     *
     * @return bool 添加成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 获取当前选中的目标玩家
     * 2. 验证目标玩家是否存在
     * 3. 调用目标玩家的 CompletedAchievement 方法完成成就
     *
     * 注意事项：
     * - 必须在游戏中选中一个玩家作为目标
     * - 成就ID通过参数自动解析为成就条目
     * - 完成成就会触发相关的奖励和通知
     * - 如果玩家已完成该成就，不会重复完成
     *
     * 调用时机：GM执行 .achievement add <achievementId> 命令时
     *
     * @example
     * .achievement add 1234  // 为选中的玩家添加ID为1234的成就
     */
    static bool HandleAchievementAddCommand(ChatHandler* handler, AchievementEntry const* achievementEntry)
    {
        // 获取选中的目标玩家
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            // 没有选中玩家，发送错误消息
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 为目标玩家完成指定成就
        target->CompletedAchievement(achievementEntry);

        return true;
    }
};

/**
 * @brief 注册成就命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册成就命令脚本实例。
 * 该函数由脚本管理系统自动调用。
 *
 * 调用时机：服务器初始化时，由脚本加载系统调用
 */
void AddSC_achievement_commandscript()
{
    new achievement_commandscript();
}
