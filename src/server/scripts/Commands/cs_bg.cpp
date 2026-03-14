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
 * @file cs_bg.cpp
 * @brief 战场控制命令模块
 *
 * 本模块实现了战场相关的GM命令，用于控制战场的启动和停止。
 * 提供的功能包括：
 * - 立即启动战场（跳过等待时间）
 * - 强制结束战场
 *
 * 这些命令主要用于测试和调试战场系统，或在特殊情况下手动控制战场状态。
 */

#include "Battleground.h"
#include "Chat.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"

using namespace Trinity::ChatCommands;

/**
 * @class bg_commandscript
 * @brief 战场控制命令脚本类
 *
 * 提供战场相关的GM命令处理功能，用于控制战场的启动和停止。
 * 所有命令仅限游戏内使用，不支持控制台执行。
 */
class bg_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化战场命令脚本，注册脚本名称为"bg_commandscript"
     */
    bg_commandscript() : CommandScript("bg_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回战场相关命令的命令表
     *
     * 注册以下战场命令：
     * - bg start: 立即启动当前战场
     * - bg stop: 强制结束当前战场
     *
     * 所有命令仅限游戏内使用（Console::No）
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "bg start",   HandleBgStartCommand,   LANG_COMMAND_BG_START_HELP, rbac::RBAC_PERM_COMMAND_BG_START,   Console::No },
            { "bg stop",    HandleBgStopCommand,    LANG_COMMAND_BG_STOP_HELP,  rbac::RBAC_PERM_COMMAND_BG_STOP,    Console::No }
        };
        return commandTable;
    }

    /**
     * @brief 立即启动战场
     * @param handler 聊天命令处理器
     * @return 启动成功返回true，否则返回false
     *
     * 命令格式: .bg start
     * 调用时机: GM需要立即开始战场战斗时
     *
     * 将战场的开始延迟时间设置为0，立即启动战斗。
     * 玩家必须在战场内才能使用此命令。
     *
     * 用途:
     * - 测试战场时跳过等待时间
     * - 在特殊情况下快速开始战斗
     */
    static bool HandleBgStartCommand(ChatHandler* handler)
    {
        // 获取玩家当前所在的战场
        Battleground* bg = handler->GetPlayer()->GetBattleground();
        if (!bg)
        {
            // 玩家不在战场中
            handler->SendSysMessage(LANG_COMMAND_NO_BATTLEGROUND_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将开始延迟时间设置为0，立即启动战斗
        bg->SetStartDelayTime(0);

        return true;
    }

    /**
     * @brief 强制结束战场
     * @param handler 聊天命令处理器
     * @return 结束成功返回true，否则返回false
     *
     * 命令格式: .bg stop
     * 调用时机: GM需要强制结束战场时
     *
     * 强制结束当前战场，参数0表示没有获胜方。
     * 玩家必须在战场内才能使用此命令。
     *
     * 用途:
     * - 测试战场时提前结束战斗
     * - 在特殊情况下终止战场
     *
     * 注意: 结束时不会宣布获胜方，可能导致双方都失败
     */
    static bool HandleBgStopCommand(ChatHandler* handler)
    {
        // 获取玩家当前所在的战场
        Battleground* bg = handler->GetPlayer()->GetBattleground();
        if (!bg)
        {
            // 玩家不在战场中
            handler->SendSysMessage(LANG_COMMAND_NO_BATTLEGROUND_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 结束战场，参数0表示没有获胜方
        bg->EndBattleground(0);

        return true;
    }
};

/**
 * @brief 注册战场命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册战场命令脚本实例。
 * 这是脚本系统的标准入口点，将脚本添加到命令处理系统中。
 */
void AddSC_bg_commandscript()
{
    new bg_commandscript();
}
