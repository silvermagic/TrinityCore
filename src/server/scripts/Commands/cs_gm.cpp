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
 * @file cs_gm.cpp
 * @brief GM（游戏管理员）模式管理命令模块
 *
 * 本模块实现了GM命令系统中用于管理游戏管理员模式和状态的命令。
 * 这些命令允许GM控制自己的游戏状态，如显示/隐藏、飞行模式、GM徽章等。
 *
 * 主要功能:
 * - 开启/关闭GM模式
 * - 开启/关闭飞行模式
 * - 开启/关闭GM可见性
 * - 开启/关闭GM聊天徽章
 * - 列出在线的GM列表
 * - 查询所有GM账号列表
 *
 * 使用场景:
 * - GM需要低调处理玩家问题（隐藏模式）
 * - GM需要快速移动（飞行模式）
 * - GM需要表明身份（GM徽章）
 * - 查看当前在线的其他管理员
 */
/* ScriptData
Name: gm_commandscript
%Complete: 100
Comment: All gm related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Realm.h"
#include "World.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class gm_commandscript
 * @brief GM模式管理命令脚本类
 *
 * 继承自CommandScript基类，提供GM模式管理功能的命令实现。
 * 该类负责注册和处理所有与GM状态相关的命令。
 */
class gm_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化GM命令脚本，设置脚本名称为"gm_commandscript"
     */
    gm_commandscript() : CommandScript("gm_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回命令表的常量引用
     *
     * 注册所有GM模式相关的命令，包括：
     * - gm on：开启GM模式
     * - gm off：关闭GM模式
     * - gm chat：开启/关闭GM聊天徽章
     * - gm fly：开启/关闭飞行模式
     * - gm visible：开启/关闭可见性
     * - gm ingame：列出在线GM
     * - gm list：列出所有GM账号
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable gmCommandTable =
        {
            { "chat",       HandleGMChatCommand,        rbac::RBAC_PERM_COMMAND_GM_CHAT,        Console::No },
            { "fly",        HandleGMFlyCommand,         rbac::RBAC_PERM_COMMAND_GM_FLY,         Console::No },
            { "ingame",     HandleGMListIngameCommand,  rbac::RBAC_PERM_COMMAND_GM_INGAME,      Console::Yes },
            { "list",       HandleGMListFullCommand,    rbac::RBAC_PERM_COMMAND_GM_LIST,        Console::Yes },
            { "visible",    HandleGMVisibleCommand,     rbac::RBAC_PERM_COMMAND_GM_VISIBLE,     Console::No },
            { "on",         HandleGMOnCommand,          rbac::RBAC_PERM_COMMAND_GM,             Console::No },
            { "off",        HandleGMOffCommand,         rbac::RBAC_PERM_COMMAND_GM,             Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "gm", gmCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理GM聊天徽章命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param enableArg 可选参数，开启(true)或关闭(false)，不提供则查询当前状态
     * @return 成功返回true，失败返回false
     *
     * 开启或关闭GM在聊天频道中的员工徽章显示。
     * 当开启时，玩家的聊天消息会显示特殊的GM标识。
     * 如果不提供参数，则显示当前徽章状态。
     *
     * 调用时机：当GM执行 .gm chat 命令时
     * 性能注意：仅修改玩家状态标志，无数据库操作
     */
    // Enables or disables the staff badge
    static bool HandleGMChatCommand(ChatHandler* handler, Optional<bool> enableArg)
    {
        if (WorldSession* session = handler->GetSession())
        {
            // 如果未提供参数，查询并显示当前状态
            if (!enableArg)
            {
                if (session->HasPermission(rbac::RBAC_PERM_CHAT_USE_STAFF_BADGE) && session->GetPlayer()->isGMChat())
                    session->SendNotification(LANG_GM_CHAT_ON);
                else
                    session->SendNotification(LANG_GM_CHAT_OFF);
                return true;
            }

            // 根据参数设置GM聊天模式
            if (*enableArg)
            {
                session->GetPlayer()->SetGMChat(true);
                session->SendNotification(LANG_GM_CHAT_ON);
                return true;
            }
            else
            {
                session->GetPlayer()->SetGMChat(false);
                session->SendNotification(LANG_GM_CHAT_OFF);
                return true;
            }
        }

        // 无会话时提示错误
        handler->SendSysMessage(LANG_USE_BOL);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 处理GM飞行模式命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param enable 是否开启飞行模式
     * @return 总是返回true
     *
     * 为指定目标（或自己）开启或关闭飞行模式。
     * 开启后玩家可以在空中自由飞行，不受地形限制。
     *
     * 调用时机：当GM执行 .gm fly 命令时
     * 性能注意：发送网络包更新客户端状态
     */
    static bool HandleGMFlyCommand(ChatHandler* handler, bool enable)
    {
        // 获取目标玩家，如果没有选择目标则使用自己
        Player* target = handler->getSelectedPlayer();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        // 构建并发送飞行模式数据包
        WorldPacket data(12);
        if (enable)
            data.SetOpcode(SMSG_MOVE_SET_CAN_FLY);    // 设置可以飞行
        else
            data.SetOpcode(SMSG_MOVE_UNSET_CAN_FLY);  // 取消飞行能力

        data << target->GetPackGUID();
        data << uint32(0);                                      // unknown，未知字段
        target->SendMessageToSet(&data, true);                  // 广播给周围玩家
        handler->PSendSysMessage(LANG_COMMAND_FLYMODE_STATUS, handler->GetNameLink(target).c_str(), enable ? "on" : "off");
        return true;
    }

    /**
     * @brief 处理显示在线GM列表命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @return 总是返回true
     *
     * 列出当前在线的所有游戏管理员。
     * 只显示满足以下条件的GM：
     * 1. 开启了GM模式的玩家
     * 2. 或者有权限出现在GM列表中且等级不超过配置限制
     * 3. 对于玩家执行命令，只能看到全局可见的GM
     *
     * 调用时机：当GM执行 .gm ingame 命令时
     * 性能注意：遍历所有在线玩家，加读锁保护
     */
    static bool HandleGMListIngameCommand(ChatHandler* handler)
    {
        bool first = true;
        bool footer = false;

        // 加读锁保护玩家列表的遍历
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [playerGuid, player] : ObjectAccessor::GetPlayers())
        {
            AccountTypes playerSec = player->GetSession()->GetSecurity();
            // 检查玩家是否应该出现在GM列表中
            if ((player->IsGameMaster() ||
                (player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMANDS_APPEAR_IN_GM_LIST) &&
                    playerSec <= AccountTypes(sWorld->getIntConfig(CONFIG_GM_LEVEL_IN_GM_LIST)))) &&
                (!handler->GetSession() || player->IsVisibleGloballyFor(handler->GetSession()->GetPlayer())))
            {
                if (first)
                {
                    // 第一次找到GM，显示表头
                    first = false;
                    footer = true;
                    handler->SendSysMessage(LANG_GMS_ON_SRV);
                    handler->SendSysMessage("========================");
                }
                // 格式化显示GM信息
                std::string const& name = player->GetName();
                uint8 size = uint8(name.size());
                uint8 security = playerSec;
                uint8 max = ((16 - size) / 2);
                uint8 max2 = max;
                if ((max + max2 + size) == 16)
                    max2 = max - 1;
                // 根据是否在游戏中显示不同格式
                if (handler->GetSession())
                    handler->PSendSysMessage("|    %s GMLevel %u", name.c_str(), security);
                else
                    handler->PSendSysMessage("|%*s%s%*s|   %u  |", max, " ", name.c_str(), max2, " ", security);
            }
        }
        // 显示表尾
        if (footer)
            handler->SendSysMessage("========================");
        // 如果没有找到任何GM
        if (first)
            handler->SendSysMessage(LANG_GMS_NOT_LOGGED);
        return true;
    }

    /**
     * @brief 处理显示所有GM账号列表命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @return 总是返回true
     *
     * 从数据库查询并显示所有GM权限等级的账号列表。
     * 显示账号名和GM等级信息，无论账号是否在线。
     *
     * 调用时机：当GM执行 .gm list 命令时
     * 性能注意：执行数据库查询，可能返回大量结果
     */
    /// Display the list of GMs
    static bool HandleGMListFullCommand(ChatHandler* handler)
    {
        ///- Get the accounts with GM Level >0
        // 查询数据库获取所有GM等级大于0的账号
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_GM_ACCOUNTS);
        stmt->setUInt8(0, uint8(SEC_MODERATOR));  // 最低GM等级
        stmt->setInt32(1, int32(realm.Id.Realm)); // 当前 realm ID
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        if (result)
        {
            handler->SendSysMessage(LANG_GMLIST);
            handler->SendSysMessage("========================");
            ///- Cycle through them. Display username and GM level
            // 遍历结果集，显示每个GM账号的信息
            do
            {
                Field* fields = result->Fetch();
                char const* name = fields[0].GetCString();
                uint8 security = fields[1].GetUInt8();
                // 计算格式化间距
                uint8 max = (16 - strlen(name)) / 2;
                uint8 max2 = max;
                if ((max + max2 + strlen(name)) == 16)
                    max2 = max - 1;
                // 根据是否在游戏中显示不同格式
                if (handler->GetSession())
                    handler->PSendSysMessage("|    %s GMLevel %u", name, security);
                else
                    handler->PSendSysMessage("|%*s%s%*s|   %u  |", max, " ", name, max2, " ", security);
            } while (result->NextRow());
            handler->SendSysMessage("========================");
        }
        else
            // 没有找到任何GM账号
            handler->PSendSysMessage(LANG_GMLIST_EMPTY);
        return true;
    }

    /**
     * @brief 处理GM可见性命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param visibleArg 可选参数，显示(true)或隐藏(false)，不提供则查询当前状态
     * @return 成功返回true，失败返回false
     *
     * 开启或关闭GM的可见性状态。
     * 隐藏时，普通玩家无法看到GM，GM会获得一个特殊的视觉光环。
     * 如果不提供参数，则显示当前的可见性状态。
     *
     * 调用时机：当GM执行 .gm visible 命令时
     * 性能注意：更新玩家对象可见性，可能影响周围玩家的视野更新
     */
    //Enable\Disable Invisible mode
    static bool HandleGMVisibleCommand(ChatHandler* handler, Optional<bool> visibleArg)
    {
        Player* _player = handler->GetSession()->GetPlayer();

        // 如果未提供参数，查询并显示当前状态
        if (!visibleArg)
        {
            handler->PSendSysMessage(LANG_YOU_ARE, _player->isGMVisible() ? handler->GetTrinityString(LANG_VISIBLE) : handler->GetTrinityString(LANG_INVISIBLE));
            return true;
        }

        // GM隐形时显示的视觉光环效果
        const uint32 VISUAL_AURA = 37800;

        if (*visibleArg)
        {
            // 开启可见性：移除隐形光环，设置可见标志
            if (_player->HasAura(VISUAL_AURA))
                _player->RemoveAurasDueToSpell(VISUAL_AURA);

            _player->SetGMVisible(true);
            _player->UpdateObjectVisibility();  // 更新对象可见性
            handler->GetSession()->SendNotification(LANG_INVISIBLE_VISIBLE);
        }
        else
        {
            // 关闭可见性：添加隐形光环，设置不可见标志
            _player->AddAura(VISUAL_AURA, _player);
            _player->SetGMVisible(false);
            _player->UpdateObjectVisibility();  // 更新对象可见性
            handler->GetSession()->SendNotification(LANG_INVISIBLE_INVISIBLE);
        }

        return true;
    }

    /**
     * @brief 处理开启GM模式命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @return 总是返回true
     *
     * 开启GM模式，启用所有GM特权。
     * 开启后GM可以：
     * - 看到隐藏的游戏对象
     * - 使用GM专用命令
     * - 看到触发器等调试信息
     *
     * 调用时机：当GM执行 .gm on 命令时
     * 性能注意：更新触发器可见性
     */
    static bool HandleGMOnCommand(ChatHandler* handler)
    {
        // 设置玩家为GM模式
        handler->GetPlayer()->SetGameMaster(true);
        // 更新触发器可见性，使GM能看到调试用的触发器
        handler->GetPlayer()->UpdateTriggerVisibility();
        handler->GetSession()->SendNotification(LANG_GM_ON);
        return true;
    }

    /**
     * @brief 处理关闭GM模式命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @return 总是返回true
     *
     * 关闭GM模式，禁用所有GM特权。
     * 关闭后GM将恢复为普通玩家状态。
     *
     * 调用时机：当GM执行 .gm off 命令时
     * 性能注意：更新触发器可见性
     */
    static bool HandleGMOffCommand(ChatHandler* handler)
    {
        // 取消玩家的GM模式
        handler->GetPlayer()->SetGameMaster(false);
        // 更新触发器可见性，隐藏调试用的触发器
        handler->GetPlayer()->UpdateTriggerVisibility();
        handler->GetSession()->SendNotification(LANG_GM_OFF);
        return true;
    }
};

/**
 * @brief 注册GM命令脚本
 *
 * 此函数用于将GM命令脚本注册到服务器中，
 * 在服务器启动时被调用以初始化命令系统
 */
void AddSC_gm_commandscript()
{
    new gm_commandscript();
}
