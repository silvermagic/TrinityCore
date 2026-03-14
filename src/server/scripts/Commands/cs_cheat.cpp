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
 * @file cs_cheat.cpp
 * @brief GM作弊命令模块
 *
 * 本模块提供了GM调试和测试用的作弊命令，包括：
 * - 上帝模式（无敌）
 * - 施法时间作弊（瞬间施法）
 * - 冷却时间作弊（无冷却）
 * - 能量消耗作弊（免费施法）
 * - 水面行走
 * - 飞行点解锁
 * - 地图探索
 * - 作弊状态查询
 *
 * 主要命令：
 * - .cheat god - 上帝模式开关
 * - .cheat casttime - 施法时间作弊开关
 * - .cheat cooldown - 冷却时间作弊开关
 * - .cheat power - 能量消耗作弊开关
 * - .cheat waterwalk - 水面行走开关
 * - .cheat taxi - 飞行点作弊开关
 * - .cheat explore - 地图探索开关
 * - .cheat status - 查询当前作弊状态
 */

/* ScriptData
Name: cheat_commandscript
%Complete: 100
Comment: All cheat related commands
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
 * @class cheat_commandscript
 * @brief 作弊命令脚本类
 *
 * 该类继承自CommandScript，负责注册和处理所有GM作弊相关的命令。
 * 提供各种调试和测试用的作弊功能，方便GM进行服务器测试。
 */
class cheat_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化作弊命令脚本，注册脚本名称为"cheat_commandscript"
     */
    cheat_commandscript() : CommandScript("cheat_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有作弊相关的命令及其处理函数
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的命令
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable cheatCommandTable =
        {
            { "god",            HandleGodModeCheatCommand,   rbac::RBAC_PERM_COMMAND_CHEAT_GOD,       Console::No },
            { "casttime",       HandleCasttimeCheatCommand,  rbac::RBAC_PERM_COMMAND_CHEAT_CASTTIME,  Console::No },
            { "cooldown",       HandleCoolDownCheatCommand,  rbac::RBAC_PERM_COMMAND_CHEAT_COOLDOWN,  Console::No },
            { "power",          HandlePowerCheatCommand,     rbac::RBAC_PERM_COMMAND_CHEAT_POWER,     Console::No },
            { "waterwalk",      HandleWaterWalkCheatCommand, rbac::RBAC_PERM_COMMAND_CHEAT_WATERWALK, Console::No },
            { "status",         HandleCheatStatusCommand,    rbac::RBAC_PERM_COMMAND_CHEAT_STATUS,    Console::No },
            { "taxi",           HandleTaxiCheatCommand,      rbac::RBAC_PERM_COMMAND_CHEAT_TAXI,      Console::No },
            { "explore",        HandleExploreCheatCommand,   rbac::RBAC_PERM_COMMAND_CHEAT_EXPLORE,   Console::No },

        };

        static ChatCommandTable commandTable =
        {
            { "cheat", cheatCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理.cheat god命令
     *
     * 开关上帝模式（无敌状态）
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后玩家不会受到任何伤害
     */
    static bool HandleGodModeCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 确定新状态：如果指定了参数则使用参数，否则切换当前状态
        bool enable = !handler->GetSession()->GetPlayer()->GetCommandStatus(CHEAT_GOD);
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启上帝模式
            handler->GetSession()->GetPlayer()->SetCommandStatusOn(CHEAT_GOD);
            handler->SendSysMessage("Godmode is ON. You won't take damage.");
        }
        else
        {
            // 关闭上帝模式
            handler->GetSession()->GetPlayer()->SetCommandStatusOff(CHEAT_GOD);
            handler->SendSysMessage("Godmode is OFF. You can take damage.");
        }

        return true;
    }

    /**
     * @brief 处理.cheat casttime命令
     *
     * 开关施法时间作弊（瞬间施法）
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后所有法术瞬间施法，无施法时间
     */
    static bool HandleCasttimeCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 确定新状态
        bool enable = !handler->GetSession()->GetPlayer()->GetCommandStatus(CHEAT_CASTTIME);
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启施法时间作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOn(CHEAT_CASTTIME);
            handler->SendSysMessage("CastTime Cheat is ON. Your spells won't have a casttime.");
        }
        else
        {
            // 关闭施法时间作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOff(CHEAT_CASTTIME);
            handler->SendSysMessage("CastTime Cheat is OFF. Your spells will have a casttime.");
        }

        return true;
    }

    /**
     * @brief 处理.cheat cooldown命令
     *
     * 开关冷却时间作弊（无冷却）
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后法术无冷却时间和公共CD
     */
    static bool HandleCoolDownCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 确定新状态
        bool enable = !handler->GetSession()->GetPlayer()->GetCommandStatus(CHEAT_COOLDOWN);
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启冷却时间作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOn(CHEAT_COOLDOWN);
            handler->SendSysMessage("Cooldown Cheat is ON. You are not on the global cooldown.");
        }
        else
        {
            // 关闭冷却时间作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOff(CHEAT_COOLDOWN);
            handler->SendSysMessage("Cooldown Cheat is OFF. You are on the global cooldown.");
        }

        return true;
    }

    /**
     * @brief 处理.cheat power命令
     *
     * 开关能量消耗作弊（免费施法）
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后施法不消耗法力/怒气/能量等资源
     */
    static bool HandlePowerCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 确定新状态
        bool enable = !handler->GetSession()->GetPlayer()->GetCommandStatus(CHEAT_POWER);
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启能量消耗作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOn(CHEAT_POWER);
            handler->SendSysMessage("Power Cheat is ON. You don't need mana/rage/energy to use spells.");
        }
        else
        {
            // 关闭能量消耗作弊
            handler->GetSession()->GetPlayer()->SetCommandStatusOff(CHEAT_POWER);
            handler->SendSysMessage("Power Cheat is OFF. You need mana/rage/energy to use spells.");
        }

        return true;
    }

    /**
     * @brief 处理.cheat status命令
     *
     * 显示当前所有作弊功能的状态
     *
     * @param handler 聊天命令处理器
     * @return bool 命令执行成功返回true
     *
     * @note 显示内容包括：上帝模式、冷却时间、施法时间、能量消耗、
     *       水面行走、飞行点等所有作弊功能的状态
     */
    static bool HandleCheatStatusCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();

        char const* enabled = "ON";
        char const* disabled = "OFF";

        // 显示所有作弊功能的状态
        handler->SendSysMessage(LANG_COMMAND_CHEAT_STATUS);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_GOD, player->GetCommandStatus(CHEAT_GOD) ? enabled : disabled);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_CD, player->GetCommandStatus(CHEAT_COOLDOWN) ? enabled : disabled);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_CT, player->GetCommandStatus(CHEAT_CASTTIME) ? enabled : disabled);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_POWER, player->GetCommandStatus(CHEAT_POWER) ? enabled : disabled);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_WW, player->GetCommandStatus(CHEAT_WATERWALK) ? enabled : disabled);
        handler->PSendSysMessage(LANG_COMMAND_CHEAT_TAXINODES, player->isTaxiCheater() ? enabled : disabled);

        return true;
    }

    /**
     * @brief 处理.cheat waterwalk命令
     *
     * 开关水面行走功能
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后可以在水面上行走
     */
    static bool HandleWaterWalkCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 确定新状态
        bool enable = !handler->GetSession()->GetPlayer()->GetCommandStatus(CHEAT_WATERWALK);
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启水面行走
            handler->GetSession()->GetPlayer()->SetCommandStatusOn(CHEAT_WATERWALK);
            handler->GetSession()->GetPlayer()->SetMovement(MOVE_WATER_WALK);               // 设置水面行走移动模式
            handler->SendSysMessage("Waterwalking is ON. You can walk on water.");
        }
        else
        {
            // 关闭水面行走
            handler->GetSession()->GetPlayer()->SetCommandStatusOff(CHEAT_WATERWALK);
            handler->GetSession()->GetPlayer()->SetMovement(MOVE_LAND_WALK);                // 恢复陆地行走移动模式
            handler->SendSysMessage("Waterwalking is OFF. You can't walk on water.");
        }

        return true;
    }

    /**
     * @brief 处理.cheat taxi命令
     *
     * 开关飞行点作弊（解锁所有飞行点）
     *
     * @param handler 聊天命令处理器
     * @param enableArg 是否启用，可选参数，未指定时切换当前状态
     * @return bool 命令执行成功返回true
     *
     * @note 开启后可以使用所有飞行点，无需探索解锁；
     *       可以对目标玩家使用，未指定目标时对自己使用
     */
    static bool HandleTaxiCheatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        // 获取目标玩家，未指定时对自己使用
        Player* chr = handler->getSelectedPlayer();

        if (!chr)
            chr = handler->GetSession()->GetPlayer();
        else if (handler->HasLowerSecurity(chr, ObjectGuid::Empty)) // 检查在线权限
            return false;

        // 确定新状态
        bool enable = !chr->isTaxiCheater();
        if (enableArg)
            enable = *enableArg;

        if (enable)
        {
            // 开启飞行点作弊
            chr->SetTaxiCheater(true);
            handler->PSendSysMessage(LANG_YOU_GIVE_TAXIS, handler->GetNameLink(chr).c_str());
            if (handler->needReportToTarget(chr))
                ChatHandler(chr->GetSession()).PSendSysMessage(LANG_YOURS_TAXIS_ADDED, handler->GetNameLink().c_str());
        }
        else
        {
            // 关闭飞行点作弊
            chr->SetTaxiCheater(false);
            handler->PSendSysMessage(LANG_YOU_REMOVE_TAXIS, handler->GetNameLink(chr).c_str());
            if (handler->needReportToTarget(chr))
                ChatHandler(chr->GetSession()).PSendSysMessage(LANG_YOURS_TAXIS_REMOVED, handler->GetNameLink().c_str());
        }

        return true;
    }

    /**
     * @brief 处理.cheat explore命令
     *
     * 开关地图探索状态（显示/隐藏所有地图区域）
     *
     * @param handler 聊天命令处理器
     * @param reveal true表示探索所有区域，false表示隐藏所有区域
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 探索所有区域可以显示整个地图；
     *       隐藏所有区域可以重置地图探索进度
     */
    static bool HandleExploreCheatCommand(ChatHandler* handler, bool reveal)
    {
        // 获取目标玩家
        Player* chr = handler->getSelectedPlayer();
        if (!chr)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 显示操作结果
        if (reveal)
        {
            handler->PSendSysMessage(LANG_YOU_SET_EXPLORE_ALL, handler->GetNameLink(chr).c_str());
            if (handler->needReportToTarget(chr))
            ChatHandler(chr->GetSession()).PSendSysMessage(LANG_YOURS_EXPLORE_SET_ALL, handler->GetNameLink().c_str());
        }
        else
        {
            handler->PSendSysMessage(LANG_YOU_SET_EXPLORE_NOTHING, handler->GetNameLink(chr).c_str());
            if (handler->needReportToTarget(chr))
                ChatHandler(chr->GetSession()).PSendSysMessage(LANG_YOURS_EXPLORE_SET_NOTHING, handler->GetNameLink().c_str());
        }

        // 更新所有探索区域的标记
        for (uint8 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
        {
            if (reveal)
                handler->GetSession()->GetPlayer()->SetFlag(PLAYER_EXPLORED_ZONES_1+i, 0xFFFFFFFF); // 设置所有位为1，表示全部探索
            else
                handler->GetSession()->GetPlayer()->SetFlag(PLAYER_EXPLORED_ZONES_1+i, 0); // 设置所有位为0，表示全部未探索
        }

        return true;
    }
};

/**
 * @brief 注册作弊命令脚本
 *
 * 创建并注册作弊命令脚本实例到系统
 */
void AddSC_cheat_commandscript()
{
    new cheat_commandscript();
}
