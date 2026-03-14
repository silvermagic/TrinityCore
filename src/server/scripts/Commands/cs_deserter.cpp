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
 * @file cs_deserter.cpp
 * @brief 逃亡者debuff管理命令模块
 *
 * 本模块提供了管理逃亡者debuff的GM命令，包括：
 * - 为玩家添加副本逃亡者debuff
 * - 为玩家添加战场逃亡者debuff
 * - 移除玩家的逃亡者debuff
 *
 * 主要命令：
 * - .deserter instance add [时间] - 添加副本逃亡者debuff
 * - .deserter instance remove - 移除副本逃亡者debuff
 * - .deserter bg add [时间] - 添加战场逃亡者debuff
 * - .deserter bg remove - 移除战场逃亡者debuff
 *
 * @note 逃亡者debuff会阻止玩家进入相应的副本或战场队列
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellAuras.h"

using namespace Trinity::ChatCommands;

/**
 * @enum Spells
 * @brief 逃亡者相关的法术ID枚举
 */
enum Spells
{
    LFG_SPELL_DUNGEON_DESERTER = 71041,  ///< 副本逃亡者debuff法术ID
    BG_SPELL_DESERTER = 26013            ///< 战场逃亡者debuff法术ID
};

/**
 * @class deserter_commandscript
 * @brief 逃亡者命令脚本类
 *
 * 该类继承自CommandScript，负责注册和处理所有与逃亡者debuff相关的GM命令。
 * 允许GM对玩家添加或移除逃亡者debuff，用于测试或管理目的。
 */
class deserter_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化逃亡者命令脚本，注册脚本名称为"deserter_commandscript"
     */
    deserter_commandscript() : CommandScript("deserter_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有逃亡者相关的命令及其处理函数
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的命令
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable deserterInstanceCommandTable =
        {
            { "add",      HandleDeserterInstanceAdd,    rbac::RBAC_PERM_COMMAND_DESERTER_INSTANCE_ADD,    Console::No },
            { "remove",   HandleDeserterInstanceRemove, rbac::RBAC_PERM_COMMAND_DESERTER_INSTANCE_REMOVE, Console::No },
        };
        static ChatCommandTable deserterBGCommandTable =
        {
            { "add",      HandleDeserterBGAdd,    rbac::RBAC_PERM_COMMAND_DESERTER_BG_ADD,          Console::No },
            { "remove",   HandleDeserterBGRemove, rbac::RBAC_PERM_COMMAND_DESERTER_BG_REMOVE,       Console::No },
        };

        static ChatCommandTable deserterCommandTable =
        {
            { "instance", deserterInstanceCommandTable },
            { "bg",       deserterBGCommandTable },
        };
        static ChatCommandTable commandTable =
        {
            { "deserter", deserterCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 为玩家添加逃亡者debuff
     *
     * 给选中的目标玩家添加指定类型的逃亡者debuff，持续指定时间
     *
     * @param handler 聊天命令处理器
     * @param time debuff持续时间（秒）
     * @param isInstance 是否为副本逃亡者debuff（true为副本，false为战场）
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 需要先选中一个玩家目标；
     *       时间参数必须大于0；
     *       副本逃亡者debuff：LFG_SPELL_DUNGEON_DESERTER (71041)
     *       战场逃亡者debuff：BG_SPELL_DESERTER (26013)
     *
     * @example
     * .deserter instance add 3600  // 添加1小时的副本逃亡者debuff
     * .deserter bg add 1800        // 添加30分钟的战场逃亡者debuff
     */
    static bool HandleDeserterAdd(ChatHandler* handler, uint32 time, bool isInstance)
    {
        // 获取选中的目标玩家
        Player* player = handler->getSelectedPlayer();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证时间参数
        if (!time)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 根据类型添加对应的逃亡者debuff
        Aura* aura = player->AddAura(isInstance ? LFG_SPELL_DUNGEON_DESERTER : BG_SPELL_DESERTER, player);

        if (!aura)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 设置debuff持续时间（转换为毫秒）
        aura->SetDuration(time * IN_MILLISECONDS);

        return true;
    }

    /**
     * @brief 移除玩家的逃亡者debuff
     *
     * 移除选中目标玩家的指定类型的逃亡者debuff
     *
     * @param handler 聊天命令处理器
     * @param isInstance 是否为副本逃亡者debuff（true为副本，false为战场）
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 需要先选中一个玩家目标
     *
     * @example
     * .deserter instance remove  // 移除副本逃亡者debuff
     * .deserter bg remove        // 移除战场逃亡者debuff
     */
    static bool HandleDeserterRemove(ChatHandler* handler, bool isInstance)
    {
        // 获取选中的目标玩家
        Player* player = handler->getSelectedPlayer();
        if (!player)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 移除对应类型的逃亡者debuff
        player->RemoveAura(isInstance ? LFG_SPELL_DUNGEON_DESERTER : BG_SPELL_DESERTER);

        return true;
    }

    /**
     * @brief 处理.deserter instance add命令
     *
     * 为选中的玩家添加副本逃亡者debuff
     *
     * @param handler 聊天命令处理器
     * @param time debuff持续时间（秒）
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleDeserterAdd
     */
    static bool HandleDeserterInstanceAdd(ChatHandler* handler, uint32 time)
    {
        return HandleDeserterAdd(handler, time, true);
    }

    /**
     * @brief 处理.deserter bg add命令
     *
     * 为选中的玩家添加战场逃亡者debuff
     *
     * @param handler 聊天命令处理器
     * @param time debuff持续时间（秒）
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleDeserterAdd
     */
    static bool HandleDeserterBGAdd(ChatHandler* handler, uint32 time)
    {
        return HandleDeserterAdd(handler, time, false);
    }

    /**
     * @brief 处理.deserter instance remove命令
     *
     * 移除选中玩家的副本逃亡者debuff
     *
     * @param handler 聊天命令处理器
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleDeserterRemove
     */
    static bool HandleDeserterInstanceRemove(ChatHandler* handler)
    {
        return HandleDeserterRemove(handler, true);
    }

    /**
     * @brief 处理.deserter bg remove命令
     *
     * 移除选中玩家的战场逃亡者debuff
     *
     * @param handler 聊天命令处理器
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleDeserterRemove
     */
    static bool HandleDeserterBGRemove(ChatHandler* handler)
    {
        return HandleDeserterRemove(handler, false);
    }
};

/**
 * @brief 注册逃亡者命令脚本
 *
 * 创建并注册逃亡者命令脚本实例到系统
 */
void AddSC_deserter_commandscript()
{
    new deserter_commandscript();
}
