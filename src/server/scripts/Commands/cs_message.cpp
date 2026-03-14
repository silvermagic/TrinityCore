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
 * @file cs_message.cpp
 * @brief 消息命令模块
 *
 * 本模块实现了游戏内各种消息通知和通信相关的命令,包括:
 * - 频道所有权管理
 * - 全服公告(带名字和不带名字)
 * - GM专用公告
 * - 屏幕通知(面向所有玩家和仅GM)
 * - GM密语管理
 *
 * 这些命令主要用于服务器管理和玩家沟通。
 */

/* ScriptData
Name: message_commandscript
%Complete: 100
Comment: All message related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "World.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class message_commandscript
 * @brief 消息命令脚本类
 *
 * 继承自 CommandScript,负责注册和处理所有消息相关的 GM 命令。
 * 提供公告、通知、频道管理和密语控制等功能。
 */
class message_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化消息命令脚本,设置脚本名称为 "message_commandscript"
     */
    message_commandscript() : CommandScript("message_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回所有消息命令的注册表
     *
     * 注册所有消息相关的命令,包括:
     * - channel set ownership: 设置频道所有权
     * - nameannounce: 带名字的全服公告
     * - gmnameannounce: 带名字的GM公告
     * - announce: 全服系统公告
     * - gmannounce: GM专用公告
     * - notify: 全服屏幕通知
     * - gmnotify: GM屏幕通知
     * - whispers: GM密语管理
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "channel set ownership",  HandleChannelSetOwnership,      rbac::RBAC_PERM_COMMAND_CHANNEL_SET_OWNERSHIP,  Console::No },
            { "nameannounce",           HandleNameAnnounceCommand,      rbac::RBAC_PERM_COMMAND_NAMEANNOUNCE,           Console::Yes },
            { "gmnameannounce",         HandleGMNameAnnounceCommand,    rbac::RBAC_PERM_COMMAND_GMNAMEANNOUNCE,         Console::Yes },
            { "announce",               HandleAnnounceCommand,          rbac::RBAC_PERM_COMMAND_ANNOUNCE,               Console::Yes },
            { "gmannounce",             HandleGMAnnounceCommand,        rbac::RBAC_PERM_COMMAND_GMANNOUNCE,             Console::Yes },
            { "notify",                 HandleNotifyCommand,            rbac::RBAC_PERM_COMMAND_NOTIFY,                 Console::Yes },
            { "gmnotify",               HandleGMNotifyCommand,          rbac::RBAC_PERM_COMMAND_GMNOTIFY,               Console::Yes },
            { "whispers",               HandleWhispersCommand,          rbac::RBAC_PERM_COMMAND_WHISPERS,               Console::No },
        };
        return commandTable;
    }

    /**
     * @brief 处理频道所有权设置命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param channelName 频道名称
     * @param grantOwnership true 表示启用所有权,false 表示禁用所有权
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .channel set ownership 命令时
     * 性能注意事项:
     * - 遍历聊天频道表和区域表查找匹配的频道
     * - 更新数据库中的频道所有权设置
     * - 如果频道已加载,同时更新内存中的设置
     *
     * 功能说明:
     * - 启用所有权后,频道主人可以将其他玩家踢出频道
     * - 禁用所有权后,频道将没有管理员权限
     */
    static bool HandleChannelSetOwnership(ChatHandler* handler, std::string channelName, bool grantOwnership)
    {
        uint32 channelId = 0;
        for (uint32 i = 0; i < sChatChannelsStore.GetNumRows(); ++i)
        {
            ChatChannelsEntry const* entry = sChatChannelsStore.LookupEntry(i);
            if (!entry)
                continue;

            if (StringContainsStringI(entry->Name[handler->GetSessionDbcLocale()], channelName))
            {
                channelId = i;
                break;
            }
        }

        AreaTableEntry const* zoneEntry = nullptr;
        for (uint32 i = 0; i < sAreaTableStore.GetNumRows(); ++i)
        {
            AreaTableEntry const* entry = sAreaTableStore.LookupEntry(i);
            if (!entry)
                continue;

            if (StringContainsStringI(entry->AreaName[handler->GetSessionDbcLocale()], channelName))
            {
                zoneEntry = entry;
                break;
            }
        }

        Player* player = handler->GetSession()->GetPlayer();
        Channel* channel = nullptr;

        if (ChannelMgr* cMgr = ChannelMgr::forTeam(player->GetTeam()))
            channel = cMgr->GetChannel(channelId, channelName, player, false, zoneEntry);

        if (grantOwnership)
        {
            if (channel)
                channel->SetOwnership(true);
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL_OWNERSHIP);
            stmt->setUInt8 (0, 1);
            stmt->setString(1, channelName);
            CharacterDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_CHANNEL_ENABLE_OWNERSHIP, channelName.c_str());
        }
        else
        {
            if (channel)
                channel->SetOwnership(false);
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL_OWNERSHIP);
            stmt->setUInt8 (0, 0);
            stmt->setString(1, channelName);
            CharacterDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_CHANNEL_DISABLE_OWNERSHIP, channelName.c_str());
        }

        return true;
    }

    /**
     * @brief 处理带名字的全服公告命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param message 公告内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .nameannounce 命令时
     * 性能注意事项:
     * - 向所有在线玩家发送消息,性能开销取决于在线玩家数量
     * - 使用 SendWorldText 进行广播
     *
     * 输出格式: "[发送者名字] 消息内容"
     * - 若从控制台发送,显示 "Console"
     * - 若从游戏内发送,显示玩家角色名
     */
    static bool HandleNameAnnounceCommand(ChatHandler* handler, Tail message)
    {
        if (message.empty())
            return false;

        std::string name("Console");
        if (WorldSession* session = handler->GetSession())
            name = session->GetPlayer()->GetName();

        sWorld->SendWorldText(LANG_ANNOUNCE_COLOR, name.c_str(), message.data());
        return true;
    }

    /**
     * @brief 处理带名字的GM公告命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param message 公告内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .gmnameannounce 命令时
     * 性能注意事项:
     * - 仅向在线的 GM 发送消息,性能开销较小
     * - 使用 SendGMText 进行定向广播
     *
     * 输出格式: "[发送者名字] 消息内容" (仅 GM 可见)
     */
    static bool HandleGMNameAnnounceCommand(ChatHandler* handler, Tail message)
    {
        if (message.empty())
            return false;

        std::string name("Console");
        if (WorldSession* session = handler->GetSession())
            name = session->GetPlayer()->GetName();

        sWorld->SendGMText(LANG_GM_ANNOUNCE_COLOR, name.c_str(), message.data());
        return true;
    }

    /**
     * @brief 处理全服系统公告命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param message 公告内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .announce 命令时
     * 性能注意事项:
     * - 向所有在线玩家发送服务器消息
     * - 使用 SendServerMessage 进行广播
     *
     * 输出格式: 屏幕中央显示系统消息,不包含发送者名字
     * 用途: 适合发送不带个人色彩的重要系统通知
     */
    // global announce
    static bool HandleAnnounceCommand(ChatHandler* handler, Tail message)
    {
        if (message.empty())
            return false;

        sWorld->SendServerMessage(SERVER_MSG_STRING, handler->PGetParseString(LANG_SYSTEMMESSAGE, message.data()));
        return true;
    }

    /**
     * @brief 处理GM专用公告命令
     * @param handler 聊天处理器(未使用)
     * @param message 公告内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .gmannounce 命令时
     * 性能注意事项:
     * - 仅向在线的 GM 发送消息,性能开销较小
     * - 使用 SendGMText 进行定向广播
     *
     * 输出格式: 仅 GM 可见的广播消息
     * 用途: GM 之间的内部沟通
     */
    // announce to logged in GMs
    static bool HandleGMAnnounceCommand(ChatHandler* /*handler*/, Tail message)
    {
        if (message.empty())
            return false;

        sWorld->SendGMText(LANG_GM_BROADCAST, message.data());
        return true;
    }

    /**
     * @brief 处理全服屏幕通知命令
     * @param handler 聊天处理器,用于获取本地化字符串
     * @param message 通知内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .notify 命令时
     * 性能注意事项:
     * - 向所有在线玩家发送数据包
     * - 使用 SMSG_NOTIFICATION 数据包类型
     *
     * 输出格式: 屏幕中央显示通知消息
     * 特点: 消息显示在屏幕中央,比聊天框消息更显眼
     */
    // send on-screen notification to players
    static bool HandleNotifyCommand(ChatHandler* handler, Tail message)
    {
        if (message.empty())
            return false;

        std::string str = handler->GetTrinityString(LANG_GLOBAL_NOTIFY);
        str += message;

        WorldPacket data(SMSG_NOTIFICATION, (str.size() + 1));
        data << str;
        sWorld->SendGlobalMessage(&data);

        return true;
    }

    /**
     * @brief 处理GM屏幕通知命令
     * @param handler 聊天处理器,用于获取本地化字符串
     * @param message 通知内容
     * @return true 表示命令执行成功,false 表示消息为空
     *
     * 调用时机: 当 GM 使用 .gmnotify 命令时
     * 性能注意事项:
     * - 仅向在线的 GM 发送数据包,性能开销较小
     * - 使用 SMSG_NOTIFICATION 数据包类型
     *
     * 输出格式: 屏幕中央显示通知消息(仅 GM 可见)
     * 用途: 向在线 GM 发送重要通知
     */
    // send on-screen notification to GMs
    static bool HandleGMNotifyCommand(ChatHandler* handler, Tail message)
    {
        if (message.empty())
            return false;

        std::string str = handler->GetTrinityString(LANG_GM_NOTIFY);
        str += message;

        WorldPacket data(SMSG_NOTIFICATION, (str.size() + 1));
        data << str;
        sWorld->SendGlobalGMMessage(&data);

        return true;
    }

    /**
     * @brief 处理GM密语管理命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param operationArg 操作参数,可以是:
     *        - 无参数: 查询当前密语接受状态
     *        - true: 启用接受密语
     *        - false: 禁用接受密语
     *        - "remove": 从白名单移除指定玩家
     * @param playerNameArg 玩家名称(仅在 operationArg 为 "remove" 时使用)
     * @return true 表示命令执行成功,false 表示参数错误或玩家不存在
     *
     * 调用时机: 当 GM 使用 .whispers 命令时
     * 性能注意事项:
     * - 查找在线玩家时需要遍历玩家列表
     * - 白名单操作为内存操作,性能开销小
     *
     * 功能说明:
     * - GM 可以关闭接受所有玩家的密语,仅接受白名单中的玩家密语
     * - 关闭密语时会清空白名单
     * - 支持从白名单中移除特定玩家
     */
    // Enable/Disable accepting whispers (for GM)
    static bool HandleWhispersCommand(ChatHandler* handler, Optional<Variant<bool, EXACT_SEQUENCE("remove")>> operationArg, Optional<std::string> playerNameArg)
    {
        if (!operationArg)
        {
            handler->PSendSysMessage(LANG_COMMAND_WHISPERACCEPTING, handler->GetSession()->GetPlayer()->isAcceptWhispers() ?  handler->GetTrinityString(LANG_ON) : handler->GetTrinityString(LANG_OFF));
            return true;
        }

        if (operationArg->holds_alternative<bool>())
        {
            if (operationArg->get<bool>())
            {
                handler->GetSession()->GetPlayer()->SetAcceptWhispers(true);
                handler->SendSysMessage(LANG_COMMAND_WHISPERON);
                return true;
            }
            else
            {
                // Remove all players from the Gamemaster's whisper whitelist
                handler->GetSession()->GetPlayer()->ClearWhisperWhiteList();
                handler->GetSession()->GetPlayer()->SetAcceptWhispers(false);
                handler->SendSysMessage(LANG_COMMAND_WHISPEROFF);
                return true;
            }
        }

        if (operationArg->holds_alternative<EXACT_SEQUENCE("remove")>())
        {
            if (!playerNameArg)
                return false;

            if (normalizePlayerName(*playerNameArg))
            {
                if (Player* player = ObjectAccessor::FindPlayerByName(*playerNameArg))
                {
                    handler->GetSession()->GetPlayer()->RemoveFromWhisperWhiteList(player->GetGUID());
                    handler->PSendSysMessage(LANG_COMMAND_WHISPEROFFPLAYER, playerNameArg->c_str());
                    return true;
                }
                else
                {
                    handler->PSendSysMessage(LANG_PLAYER_NOT_FOUND, playerNameArg->c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }
        }
        handler->SendSysMessage(LANG_USE_BOL);
        handler->SetSentErrorMessage(true);
        return false;
    }
};

/**
 * @brief 注册消息命令脚本
 *
 * 此函数由脚本系统在启动时调用,用于创建并注册 message_commandscript 实例。
 * 使消息命令在游戏中可用。
 */
void AddSC_message_commandscript()
{
    new message_commandscript();
}
