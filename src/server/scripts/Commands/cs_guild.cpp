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
 * @file    cs_guild.cpp
 * @brief   公会管理命令模块
 *
 * @details 本模块实现了所有与公会相关的GM命令，包括：
 *          - 公会的创建、删除、重命名
 *          - 公会成员的邀请、移除、等级设置
 *          - 公会信息的查询和显示
 *
 * @note    这些命令主要用于GM管理公会
 *          所有命令都需要相应的RBAC权限才能执行
 *          支持控制台执行
 *
 * @see     Guild, GuildMgr, Player
 */

/* ScriptData
Name: guild_commandscript
%Complete: 100
Comment: All guild related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

/**
 * @class guild_commandscript
 * @brief 公会命令脚本类
 *
 * @details 继承自 CommandScript，提供所有公会相关的GM命令处理功能。
 *          该类实现了公会的管理操作，包括创建、删除、成员管理等。
 *
 * @note    所有命令都需要对应的RBAC权限
 *          所有命令都支持控制台执行
 */
class guild_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * @param name 脚本名称，固定为 "guild_commandscript"
     */
    guild_commandscript() : CommandScript("guild_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回公会命令表，包含所有子命令及其处理函数
     *
     * @details 定义了以下命令：
     *          - create: 创建公会
     *          - delete: 删除公会
     *          - invite: 邀请玩家加入公会
     *          - uninvite: 将玩家移出公会
     *          - rank: 设置公会成员等级
     *          - rename: 重命名公会
     *          - info: 显示公会信息
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> guildCommandTable =
        {
            { "create",   rbac::RBAC_PERM_COMMAND_GUILD_CREATE,   true, &HandleGuildCreateCommand,           "" },
            { "delete",   rbac::RBAC_PERM_COMMAND_GUILD_DELETE,   true, &HandleGuildDeleteCommand,           "" },
            { "invite",   rbac::RBAC_PERM_COMMAND_GUILD_INVITE,   true, &HandleGuildInviteCommand,           "" },
            { "uninvite", rbac::RBAC_PERM_COMMAND_GUILD_UNINVITE, true, &HandleGuildUninviteCommand,         "" },
            { "rank",     rbac::RBAC_PERM_COMMAND_GUILD_RANK,     true, &HandleGuildRankCommand,             "" },
            { "rename",   rbac::RBAC_PERM_COMMAND_GUILD_RENAME,   true, &HandleGuildRenameCommand,           "" },
            { "info",     rbac::RBAC_PERM_COMMAND_GUILD_INFO,     true, &HandleGuildInfoCommand,             "" },
        };
        static std::vector<ChatCommand> commandTable =
        {
            { "guild", rbac::RBAC_PERM_COMMAND_GUILD,  true, nullptr, "", guildCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 创建公会命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称和公会名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 创建一个新公会并设置指定玩家为会长。
     *          参数格式：玩家名称 "公会名称"
     *          或仅： "公会名称"（使用目标玩家）
     *          - 玩家不能已在公会中
     *          - 公会名称不能重复
     *          - 公会名称必须合法（非保留名且有效）
     *          - 创建失败会清理资源
     *
     * @note 调用时机：GM执行 .guild create 命令时
     *       性能注意事项：涉及数据库操作，创建公会记录
     */
    /** \brief GM command level 3 - Create a guild.
     *
     * This command allows a GM (level 3) to create a guild.
     *
     * The "args" parameter contains the name of the guild leader
     * and then the name of the guild.
     *
     */
    static bool HandleGuildCreateCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // if not guild name only (in "") then player name
        // 如果参数不是以引号开头，则第一个参数是玩家名称
        Player* target;
        if (!handler->extractPlayerTarget(*args != '"' ? (char*)args : nullptr, &target))
            return false;

        // 解析公会名称（必须在引号中）
        char* tailStr = *args != '"' ? strtok(nullptr, "") : (char*)args;
        if (!tailStr)
            return false;

        char* guildStr = handler->extractQuotedArg(tailStr);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;

        // 检查玩家是否已在公会中
        if (target->GetGuildId())
        {
            handler->SendSysMessage(LANG_PLAYER_IN_GUILD);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查公会名称是否已存在
        if (sGuildMgr->GetGuildByName(guildName))
        {
            handler->SendSysMessage(LANG_GUILD_RENAME_ALREADY_EXISTS);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证公会名称是否合法
        if (sObjectMgr->IsReservedName(guildName) || !sObjectMgr->IsValidCharterName(guildName))
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 创建公会
        Guild* guild = new Guild;
        if (!guild->Create(target, guildName))
        {
            // 创建失败，清理资源
            delete guild;
            handler->SendSysMessage(LANG_GUILD_NOT_CREATED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将公会添加到管理器
        sGuildMgr->AddGuild(guild);

        return true;
    }

    /**
     * @brief 删除公会命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含公会名称（必须在引号中）
     * @return 成功返回 true，失败返回 false
     *
     * @details 解散指定的公会。
     *          - 公会名称必须在引号中
     *          - 公会必须存在
     *          - 解散后清除所有成员和公会数据
     *
     * @note 调用时机：GM执行 .guild delete 命令时
     *       性能注意事项：涉及数据库操作，删除公会记录
     */
    static bool HandleGuildDeleteCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // 提取公会名称（必须在引号中）
        char* guildStr = handler->extractQuotedArg((char*)args);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;

        // 查找公会
        Guild* targetGuild = sGuildMgr->GetGuildByName(guildName);
        if (!targetGuild)
            return false;

        // 解散公会
        targetGuild->Disband();
        return true;
    }

    /**
     * @brief 邀请玩家加入公会命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称和公会名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将指定玩家强制加入指定公会。
     *          参数格式：玩家名称 "公会名称"
     *          或仅： "公会名称"（使用目标玩家）
     *          - 玩家不能已在其他公会中
     *          - 公会必须存在
     *
     * @note 调用时机：GM执行 .guild invite 命令时
     *       性能注意事项：涉及数据库操作，添加成员记录
     */
    static bool HandleGuildInviteCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // if not guild name only (in "") then player name
        // 如果参数不是以引号开头，则第一个参数是玩家名称
        ObjectGuid targetGuid;
        if (!handler->extractPlayerTarget(*args != '"' ? (char*)args : nullptr, nullptr, &targetGuid))
            return false;

        // 解析公会名称（必须在引号中）
        char* tailStr = *args != '"' ? strtok(nullptr, "") : (char*)args;
        if (!tailStr)
            return false;

        char* guildStr = handler->extractQuotedArg(tailStr);
        if (!guildStr)
            return false;

        std::string guildName = guildStr;
        Guild* targetGuild = sGuildMgr->GetGuildByName(guildName);
        if (!targetGuild)
            return false;

        // player's guild membership checked in AddMember before add
        // 玩家的公会成员资格在 AddMember 中检查
        CharacterDatabaseTransaction trans(nullptr);
        return targetGuild->AddMember(trans, targetGuid);
    }

    /**
     * @brief 将玩家移出公会命令处理
     * @param handler 聊天命令处理器
     * @param args 参数字符串，包含玩家名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 将指定玩家从其所在公会中移除。
     *          - 玩家必须在某个公会中
     *          - 从公会成员列表中删除
     *
     * @note 调用时机：GM执行 .guild uninvite 命令时
     *       性能注意事项：涉及数据库操作，删除成员记录
     */
    static bool HandleGuildUninviteCommand(ChatHandler* handler, char const* args)
    {
        Player* target;
        ObjectGuid targetGuid;
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid))
            return false;

        // 获取玩家所在公会ID（在线玩家优先，否则查缓存）
        ObjectGuid::LowType guildId = target ? target->GetGuildId() : sCharacterCache->GetCharacterGuildIdByGuid(targetGuid);
        if (!guildId)
            return false;

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
            return false;

        // 从公会删除成员
        CharacterDatabaseTransaction trans(nullptr);
        targetGuild->DeleteMember(trans, targetGuid, false, true);
        return true;
    }

    /**
     * @brief 设置公会成员等级命令处理
     * @param handler 聊天命令处理器
     * @param player 可选的玩家标识，默认为目标或自己
     * @param rank 公会等级
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置指定玩家在公会中的等级。
     *          - 玩家必须在公会中
     *          - 等级必须有效
     *
     * @note 调用时机：GM执行 .guild rank 命令时
     *       性能注意事项：涉及数据库操作，更新成员等级
     */
    static bool HandleGuildRankCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, uint8 rank)
    {
        // 如果未指定玩家，使用目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        // 获取玩家所在公会ID
        ObjectGuid::LowType guildId = player->IsConnected() ? player->GetConnectedPlayer()->GetGuildId() : sCharacterCache->GetCharacterGuildIdByGuid(*player);
        if (!guildId)
            return false;

        Guild* targetGuild = sGuildMgr->GetGuildById(guildId);
        if (!targetGuild)
            return false;

        // 更改成员等级
        return targetGuild->ChangeMemberRank(nullptr, *player, rank);
    }

    /**
     * @brief 重命名公会命令处理
     * @param handler 聊天命令处理器
     * @param _args 参数字符串，包含旧公会名称和新公会名称（都必须在引号中）
     * @return 成功返回 true，失败返回 false
     *
     * @details 更改公会的名称。
     *          参数格式："旧公会名称" "新公会名称"
     *          - 旧公会必须存在
     *          - 新公会名称不能已存在
     *          - 新公会名称必须合法
     *
     * @note 调用时机：GM执行 .guild rename 命令时
     *       性能注意事项：涉及数据库操作，更新公会名称
     */
    static bool HandleGuildRenameCommand(ChatHandler* handler, char const* _args)
    {
        if (!*_args)
            return false;

        char *args = (char *)_args;

        // 提取旧公会名称
        char const* oldGuildStr = handler->extractQuotedArg(args);
        if (!oldGuildStr)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 提取新公会名称
        char const* newGuildStr = handler->extractQuotedArg(strtok(nullptr, ""));
        if (!newGuildStr)
        {
            handler->SendSysMessage(LANG_INSERT_GUILD_NAME);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 查找旧公会
        Guild* guild = sGuildMgr->GetGuildByName(oldGuildStr);
        if (!guild)
        {
            handler->PSendSysMessage(LANG_COMMAND_COULDNOTFIND, oldGuildStr);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查新公会名称是否已存在
        if (sGuildMgr->GetGuildByName(newGuildStr))
        {
            handler->PSendSysMessage(LANG_GUILD_RENAME_ALREADY_EXISTS, newGuildStr);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 设置新公会名称
        if (!guild->SetName(newGuildStr))
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage(LANG_GUILD_RENAME_DONE, oldGuildStr, newGuildStr);
        return true;
    }

    /**
     * @brief 显示公会信息命令处理
     * @param handler 聊天命令处理器
     * @param guildIdentifier 可选的公会标识符，可以是公会ID或名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 显示指定公会的详细信息，包括：
     *          - 公会ID和名称
     *          - 会长名称和GUID
     *          - 创建日期
     *          - 成员数量
     *          - 公会银行金币
     *          - 今日信息（MOTD）
     *          - 额外信息
     *
     *          如果未指定公会，则显示目标玩家所在的公会信息。
     *
     * @note 调用时机：GM执行 .guild info 命令时
     *       性能注意事项：查询数据库获取会长名称和创建日期
     */
    static bool HandleGuildInfoCommand(ChatHandler* handler, Optional<Variant<ObjectGuid::LowType, std::string_view>> const& guildIdentifier)
    {
        Guild* guild = nullptr;

        // 根据标识符查找公会
        if (guildIdentifier)
        {
            if (ObjectGuid::LowType const* guid = std::get_if<ObjectGuid::LowType>(&*guildIdentifier))
                // 按公会ID查找
                guild = sGuildMgr->GetGuildById(*guid);
            else
                // 按公会名称查找
                guild = sGuildMgr->GetGuildByName(guildIdentifier->get<std::string_view>());
        }
        else if (Optional<PlayerIdentifier> target = PlayerIdentifier::FromTargetOrSelf(handler); target && target->IsConnected())
            // 未指定公会，使用目标玩家所在公会
            guild = target->GetConnectedPlayer()->GetGuild();

        if (!guild)
            return false;

        // Display Guild Information
        // 显示公会基本信息
        handler->PSendSysMessage(LANG_GUILD_INFO_NAME, guild->GetName().c_str(), guild->GetId()); // Guild Id + Name

        // 显示会长信息
        std::string guildMasterName;
        if (sCharacterCache->GetCharacterNameByGuid(guild->GetLeaderGUID(), guildMasterName))
            handler->PSendSysMessage(LANG_GUILD_INFO_GUILD_MASTER, guildMasterName.c_str(), guild->GetLeaderGUID().GetCounter()); // Guild Master

        // Format creation date
        // 格式化并显示创建日期
        char createdDateStr[20];
        time_t createdDate = guild->GetCreatedDate();
        tm localTm;
        strftime(createdDateStr, 20, "%Y-%m-%d %H:%M:%S", localtime_r(&createdDate, &localTm));

        handler->PSendSysMessage(LANG_GUILD_INFO_CREATION_DATE, createdDateStr); // Creation Date
        handler->PSendSysMessage(LANG_GUILD_INFO_MEMBER_COUNT, guild->GetMemberCount()); // Number of Members
        // 显示公会银行金币（转换为金币单位，除以100*100）
        handler->PSendSysMessage(LANG_GUILD_INFO_BANK_GOLD, guild->GetBankMoney() / 100 / 100); // Bank Gold (in gold coins)
        handler->PSendSysMessage(LANG_GUILD_INFO_MOTD, guild->GetMOTD().c_str()); // Message of the Day
        handler->PSendSysMessage(LANG_GUILD_INFO_EXTRA_INFO, guild->GetInfo().c_str()); // Extra Information
        return true;
    }
};

/**
 * @brief 注册公会命令脚本
 *
 * @details 创建并注册公会命令脚本实例到脚本系统中。
 *          该函数在服务器启动时被调用，用于初始化所有公会相关命令。
 */
void AddSC_guild_commandscript()
{
    new guild_commandscript();
}
