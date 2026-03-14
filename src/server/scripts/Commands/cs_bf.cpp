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
 * @file cs_bf.cpp
 * @brief 战场控制命令模块
 *
 * 本模块实现了战场相关的GM命令，主要用于冬拥湖等大型战场的控制。
 * 提供的功能包括：
 * - 战场启动和停止
 * - 阵营切换
 * - 战场计时器设置
 * - 战场启用/禁用控制
 *
 * 这些命令主要用于测试和调试战场系统，以及在特殊情况下手动控制战场状态。
 */

/* ScriptData
Name: bf_commandscript
%Complete: 100
Comment: All bf related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "BattlefieldMgr.h"
#include "Chat.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

/**
 * @class bf_commandscript
 * @brief 战场控制命令脚本类
 *
 * 提供战场相关的GM命令处理功能，主要用于冬拥湖等大型战场的控制。
 * 所有命令仅限游戏内使用，不支持控制台执行。
 */
class bf_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化战场命令脚本，注册脚本名称为"bf_commandscript"
     */
    bf_commandscript() : CommandScript("bf_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回战场相关命令的命令表
     *
     * 注册以下战场命令：
     * - bf start: 启动战场
     * - bf stop: 停止战场
     * - bf switch: 切换阵营（交换攻守）
     * - bf timer: 设置战场计时器
     * - bf enable: 启用/禁用战场
     *
     * 所有命令仅限游戏内使用（Console::No）
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable battlefieldcommandTable =
        {
            { "start",      HandleBattlefieldStart,  rbac::RBAC_PERM_COMMAND_BF_START,  Console::No },
            { "stop",       HandleBattlefieldEnd,    rbac::RBAC_PERM_COMMAND_BF_STOP,   Console::No },
            { "switch",     HandleBattlefieldSwitch, rbac::RBAC_PERM_COMMAND_BF_SWITCH, Console::No },
            { "timer",      HandleBattlefieldTimer,  rbac::RBAC_PERM_COMMAND_BF_TIMER,  Console::No },
            { "enable",     HandleBattlefieldEnable, rbac::RBAC_PERM_COMMAND_BF_ENABLE, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "bf", battlefieldcommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 启动战场
     * @param handler 聊天命令处理器
     * @param battleId 战场ID（如冬拥湖为1）
     * @return 启动成功返回true，否则返回false
     *
     * 命令格式: .bf start <战场ID>
     * 调用时机: GM需要手动启动战场时
     *
     * 强制启动指定战场，无视正常的时间安排。
     * 战场ID=1对应冬拥湖（Wintergrasp）。
     */
    static bool HandleBattlefieldStart(ChatHandler* handler, uint32 battleId)
    {
        // 根据ID获取战场实例
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
            return false;

        // 启动战场
        bf->StartBattle();

        // 冬拥湖特殊消息处理
        if (battleId == 1)
            handler->SendGlobalGMSysMessage("Wintergrasp (Command start used)");

        return true;
    }

    /**
     * @brief 结束战场
     * @param handler 聊天命令处理器
     * @param battleId 战场ID
     * @return 结束成功返回true，否则返回false
     *
     * 命令格式: .bf stop <战场ID>
     * 调用时机: GM需要手动结束战场时
     *
     * 强制结束指定的战场战斗。
     * 参数true表示强制结束，不进行正常的结算流程。
     */
    static bool HandleBattlefieldEnd(ChatHandler* handler, uint32 battleId)
    {
        // 根据ID获取战场实例
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
            return false;

        // 强制结束战场
        bf->EndBattle(true);

        // 冬拥湖特殊消息处理
        if (battleId == 1)
            handler->SendGlobalGMSysMessage("Wintergrasp (Command stop used)");

        return true;
    }

    /**
     * @brief 启用或禁用战场
     * @param handler 聊天命令处理器
     * @param battleId 战场ID
     * @return 操作成功返回true，否则返回false
     *
     * 命令格式: .bf enable <战场ID>
     * 调用时机: GM需要切换战场启用状态时
     *
     * 切换战场的启用/禁用状态：
     * - 如果战场已启用，则禁用它
     * - 如果战场已禁用，则启用它
     *
     * 禁用战场将阻止该战场自动开启。
     */
    static bool HandleBattlefieldEnable(ChatHandler* handler, uint32 battleId)
    {
        // 根据ID获取战场实例
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
            return false;

        // 根据当前状态切换启用/禁用
        if (bf->IsEnabled())
        {
            // 当前已启用，执行禁用
            bf->ToggleBattlefield(false);
            if (battleId == 1)
                handler->SendGlobalGMSysMessage("Wintergrasp is disabled");
        }
        else
        {
            // 当前已禁用，执行启用
            bf->ToggleBattlefield(true);
            if (battleId == 1)
                handler->SendGlobalGMSysMessage("Wintergrasp is enabled");
        }

        return true;
    }

    /**
     * @brief 切换战场阵营
     * @param handler 聊天命令处理器
     * @param battleId 战场ID
     * @return 切换成功返回true，否则返回false
     *
     * 命令格式: .bf switch <战场ID>
     * 调用时机: GM需要切换战场攻守方时
     *
     * 结束当前战斗并交换攻守双方阵营。
     * 参数false表示正常结束，会进行阵营切换。
     * 用于测试或调整战场平衡性。
     */
    static bool HandleBattlefieldSwitch(ChatHandler* handler, uint32 battleId)
    {
        // 根据ID获取战场实例
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
            return false;

        // 结束战场并进行阵营切换
        bf->EndBattle(false);
        if (battleId == 1)
            handler->SendGlobalGMSysMessage("Wintergrasp (Command switch used)");

        return true;
    }

    /**
     * @brief 设置战场计时器
     * @param handler 聊天命令处理器
     * @param battleId 战场ID
     * @param time 时间（秒）
     * @return 设置成功返回true，否则返回false
     *
     * 命令格式: .bf timer <战场ID> <时间(秒)>
     * 调用时机: GM需要调整战场剩余时间时
     *
     * 设置战场计时器的剩余时间，单位为秒。
     * 时间会被转换为毫秒存储（乘以IN_MILLISECONDS）。
     * 设置后会立即更新所有玩家的世界状态显示。
     *
     * 示例:
     * - .bf timer 1 300  (将冬拥湖计时器设置为5分钟)
     */
    static bool HandleBattlefieldTimer(ChatHandler* handler, uint32 battleId, uint32 time)
    {
        // 根据ID获取战场实例
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
            return false;

        // 设置计时器（转换为毫秒）
        bf->SetTimer(time * IN_MILLISECONDS);
        // 向所有玩家发送更新后的世界状态
        bf->SendInitWorldStatesToAll();
        if (battleId == 1)
            handler->SendGlobalGMSysMessage("Wintergrasp (Command timer used)");

        return true;
    }
};

/**
 * @brief 注册战场命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册战场命令脚本实例。
 * 这是脚本系统的标准入口点，将脚本添加到命令处理系统中。
 */
void AddSC_bf_commandscript()
{
    new bf_commandscript();
}
