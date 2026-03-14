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
 * @file    cs_honor.cpp
 * @brief   荣誉系统管理命令模块
 *
 * @details 本模块实现了所有与荣誉相关的GM命令，包括：
 *          - 荣誉点的添加和奖励
 *          - 击杀荣誉的奖励
 *          - 荣誉字段的更新
 *
 * @note    这些命令主要用于GM测试和调整玩家荣誉
 *          所有命令都需要相应的RBAC权限才能执行
 *          仅支持游戏中使用，不支持控制台执行
 *
 * @see     Player, Honor
 */

/* ScriptData
Name: honor_commandscript
%Complete: 100
Comment: All honor related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class honor_commandscript
 * @brief 荣誉命令脚本类
 *
 * @details 继承自 CommandScript，提供所有荣誉相关的GM命令处理功能。
 *          该类实现了荣誉的管理操作，包括添加荣誉点、奖励击杀荣誉等。
 *
 * @note    所有命令都需要对应的RBAC权限
 *          所有命令仅支持游戏中使用，不支持控制台执行
 */
class honor_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * @param name 脚本名称，固定为 "honor_commandscript"
     */
    honor_commandscript() : CommandScript("honor_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回荣誉命令表，包含所有子命令及其处理函数
     *
     * @details 定义了以下命令：
     *          - add: 添加荣誉点数
     *          - add kill: 模拟击杀奖励荣誉
     *          - update: 更新荣誉字段
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable honorAddCommandTable =
        {
            { "kill", HandleHonorAddKillCommand, rbac::RBAC_PERM_COMMAND_HONOR_ADD_KILL, Console::No },
            { "",     HandleHonorAddCommand,     rbac::RBAC_PERM_COMMAND_HONOR_ADD,      Console::No },
        };

        static ChatCommandTable honorCommandTable =
        {
            { "add",    honorAddCommandTable },
            { "update", HandleHonorUpdateCommand, rbac::RBAC_PERM_COMMAND_HONOR_UPDATE, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "honor", honorCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 添加荣誉点数命令处理
     * @param handler 聊天命令处理器
     * @param amount 要添加的荣誉点数
     * @return 成功返回 true，失败返回 false
     *
     * @details 给选中的玩家添加指定数量的荣誉点。
     *          - 必须选中一个在线玩家
     *          - 不能对权限更高的玩家使用
     *          - 荣誉点数直接奖励给目标玩家
     *
     * @note 调用时机：GM执行 .honor add 命令时
     *       性能注意事项：更新玩家荣誉数据
     */
    static bool HandleHonorAddCommand(ChatHandler* handler, uint32 amount)
    {
        // 获取选中的玩家
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        // 检查权限，不能对权限更高的玩家使用
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 奖励荣誉点数
        target->RewardHonor(nullptr, 1, amount);
        return true;
    }

    /**
     * @brief 模拟击杀奖励荣誉命令处理
     * @param handler 聊天命令处理器
     * @return 成功返回 true，失败返回 false
     *
     * @details 模拟一次击杀，给予GM荣誉奖励。
     *          - 选中目标后，模拟击杀该目标
     *          - 如果目标是玩家，根据PvP规则计算荣誉奖励
     *          - 如果目标是NPC，按普通击杀处理
     *          - 不能对权限更高的玩家使用
     *
     * @note 调用时机：GM执行 .honor add kill 命令时
     *       性能注意事项：计算荣誉奖励
     */
    static bool HandleHonorAddKillCommand(ChatHandler* handler)
    {
        // 获取选中的单位（可以是玩家或NPC）
        Unit* target = handler->getSelectedUnit();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        // 如果目标是玩家，检查权限
        if (Player* player = target->ToPlayer())
            if (handler->HasLowerSecurity(player, ObjectGuid::Empty))
                return false;

        // 对目标执行击杀荣誉奖励
        handler->GetSession()->GetPlayer()->RewardHonor(target, 1);
        return true;
    }

    /**
     * @brief 更新荣誉字段命令处理
     * @param handler 聊天命令处理器
     * @return 成功返回 true，失败返回 false
     *
     * @details 强制更新选中玩家的荣誉相关字段。
     *          - 重新计算并更新玩家的荣誉点数
     *          - 用于修复荣誉数据不一致问题
     *          - 必须选中一个在线玩家
     *          - 不能对权限更高的玩家使用
     *
     * @note 调用时机：GM执行 .honor update 命令时
     *       性能注意事项：更新玩家荣誉相关字段
     */
    static bool HandleHonorUpdateCommand(ChatHandler* handler)
    {
        // 获取选中的玩家
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        // 检查权限
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 更新荣誉字段
        target->UpdateHonorFields();
        return true;
    }
};

/**
 * @brief 注册荣誉命令脚本
 *
 * @details 创建并注册荣誉命令脚本实例到脚本系统中。
 *          该函数在服务器启动时被调用，用于初始化所有荣誉相关命令。
 */
void AddSC_honor_commandscript()
{
    new honor_commandscript();
}
