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
 * @file cs_ban.cpp
 * @brief 封禁系统命令模块
 *
 * 本模块实现了游戏内封禁系统的所有GM命令，包括：
 * - 账号封禁/解封
 * - 角色封禁/解封
 * - IP地址封禁/解封
 * - 封禁信息查询
 * - 封禁列表查看
 *
 * 这些命令为GM提供了完整的封禁管理功能，用于维护游戏秩序和处理违规行为。
 */

/* ScriptData
Name: ban_commandscript
%Complete: 100
Comment: All ban related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

/**
 * @class ban_commandscript
 * @brief 封禁命令脚本类
 *
 * 提供所有封禁相关的GM命令处理功能，包括账号、角色、IP的封禁、解封和信息查询。
 * 所有命令都需要相应的RBAC权限才能执行。
 */
class ban_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化封禁命令脚本，注册脚本名称为"ban_commandscript"
     */
    ban_commandscript() : CommandScript("ban_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回封禁相关命令的命令表
     *
     * 注册以下命令组：
     * - ban: 封禁命令（账号、角色、玩家账号、IP）
     * - baninfo: 查询封禁信息
     * - banlist: 列出封禁列表
     * - unban: 解除封禁
     *
     * 所有命令均支持控制台执行（Console::Yes）
     */
    ChatCommandTable GetCommands() const override
    {
        // 解封命令表
        static ChatCommandTable unbanCommandTable =
        {
            { "account",        HandleUnBanAccountCommand,              rbac::RBAC_PERM_COMMAND_UNBAN_ACCOUNT,          Console::Yes },
            { "character",      HandleUnBanCharacterCommand,            rbac::RBAC_PERM_COMMAND_UNBAN_CHARACTER,        Console::Yes },
            { "playeraccount",  HandleUnBanAccountByCharCommand,        rbac::RBAC_PERM_COMMAND_UNBAN_PLAYERACCOUNT,    Console::Yes },
            { "ip",             HandleUnBanIPCommand,                   rbac::RBAC_PERM_COMMAND_UNBAN_IP,               Console::Yes },
        };
        // 封禁列表命令表
        static ChatCommandTable banlistCommandTable =
        {
            { "account",        HandleBanListAccountCommand,            rbac::RBAC_PERM_COMMAND_BANLIST_ACCOUNT,        Console::Yes },
            { "character",      HandleBanListCharacterCommand,          rbac::RBAC_PERM_COMMAND_BANLIST_CHARACTER,      Console::Yes },
            { "ip",             HandleBanListIPCommand,                 rbac::RBAC_PERM_COMMAND_BANLIST_IP,             Console::Yes },
        };
        // 封禁信息查询命令表
        static ChatCommandTable baninfoCommandTable =
        {
            { "account",        HandleBanInfoAccountCommand,            rbac::RBAC_PERM_COMMAND_BANINFO_ACCOUNT,        Console::Yes },
            { "character",      HandleBanInfoCharacterCommand,          rbac::RBAC_PERM_COMMAND_BANINFO_CHARACTER,      Console::Yes },
            { "ip",             HandleBanInfoIPCommand,                 rbac::RBAC_PERM_COMMAND_BANINFO_IP,             Console::Yes },
        };
        // 封禁命令表
        static ChatCommandTable banCommandTable =
        {
            { "account",        HandleBanAccountCommand,                rbac::RBAC_PERM_COMMAND_BAN_ACCOUNT,            Console::Yes },
            { "character",      HandleBanCharacterCommand,              rbac::RBAC_PERM_COMMAND_BAN_CHARACTER,          Console::Yes },
            { "playeraccount",  HandleBanAccountByCharCommand,          rbac::RBAC_PERM_COMMAND_BAN_PLAYERACCOUNT,      Console::Yes },
            { "ip",             HandleBanIPCommand,                     rbac::RBAC_PERM_COMMAND_BAN_IP,                 Console::Yes },
        };
        // 主命令表
        static ChatCommandTable commandTable =
        {
            { "ban",        banCommandTable },
            { "baninfo",    baninfoCommandTable },
            { "banlist",    banlistCommandTable },
            { "unban",      unbanCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理账号封禁命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（账号名 持续时间 原因）
     * @return 封禁成功返回true，否则返回false
     *
     * 命令格式: .ban account <账号名> <持续时间> <原因>
     * 调用时机: GM执行账号封禁命令时
     *
     * 示例:
     * - .ban account testaccount 1d 违规行为
     * - .ban account testaccount 0 永久封禁（永久）
     */
    static bool HandleBanAccountCommand(ChatHandler* handler, char const* args)
    {
        return HandleBanHelper(BAN_ACCOUNT, args, handler);
    }

    /**
     * @brief 处理角色封禁命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（角色名 持续时间 原因）
     * @return 封禁成功返回true，否则返回false
     *
     * 命令格式: .ban character <角色名> <持续时间> <原因>
     * 调用时机: GM执行角色封禁命令时
     * 性能注意: 会查询数据库验证角色是否存在
     *
     * 处理流程:
     * 1. 解析命令参数（角色名、持续时间、原因）
     * 2. 验证角色名格式并规范化
     * 3. 执行角色封禁
     * 4. 根据配置决定是否在世界频道广播
     */
    static bool HandleBanCharacterCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析角色名
        char* nameStr = strtok((char*)args, " ");
        if (!nameStr)
            return false;

        std::string name = nameStr;

        // 解析封禁持续时间
        char* durationStr = strtok(nullptr, " ");
        if (!durationStr || !atoi(durationStr))
            return false;

        // 解析封禁原因
        char* reasonStr = strtok(nullptr, "");
        if (!reasonStr)
            return false;

        // 规范化角色名（首字母大写，其余小写）
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取执行者名称（玩家或服务器）
        std::string author = handler->GetSession() ? handler->GetSession()->GetPlayerName() : "Server";

        // 执行角色封禁
        switch (sWorld->BanCharacter(name, durationStr, reasonStr, author))
        {
            case BAN_SUCCESS:
            {
                // 封禁成功，根据持续时间显示不同消息
                if (atoi(durationStr) > 0)
                {
                    // 临时封禁
                    if (sWorld->getBoolConfig(CONFIG_SHOW_BAN_IN_WORLD))
                        sWorld->SendWorldText(LANG_BAN_CHARACTER_YOUBANNEDMESSAGE_WORLD, author.c_str(), name.c_str(), secsToTimeString(TimeStringToSecs(durationStr), TimeFormat::ShortText).c_str(), reasonStr);
                    else
                        handler->PSendSysMessage(LANG_BAN_YOUBANNED, name.c_str(), secsToTimeString(TimeStringToSecs(durationStr), TimeFormat::ShortText).c_str(), reasonStr);
                }
                else
                {
                    // 永久封禁
                    if (sWorld->getBoolConfig(CONFIG_SHOW_BAN_IN_WORLD))
                        sWorld->SendWorldText(LANG_BAN_CHARACTER_YOUPERMBANNEDMESSAGE_WORLD, author.c_str(), name.c_str(), reasonStr);
                    else
                        handler->PSendSysMessage(LANG_BAN_YOUPERMBANNED, name.c_str(), reasonStr);
                }
                break;
            }
            case BAN_NOTFOUND:
            {
                // 角色未找到
                handler->PSendSysMessage(LANG_BAN_NOTFOUND, "character", name.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
            default:
                break;
        }

        return true;
    }

    /**
     * @brief 通过角色名封禁其所在账号
     * @param handler 聊天命令处理器
     * @param args 命令参数（角色名 持续时间 原因）
     * @return 封禁成功返回true，否则返回false
     *
     * 命令格式: .ban playeraccount <角色名> <持续时间> <原因>
     * 调用时机: GM只知道角色名但需要封禁整个账号时
     *
     * 此命令会查找角色对应的账号并进行封禁
     */
    static bool HandleBanAccountByCharCommand(ChatHandler* handler, char const* args)
    {
        return HandleBanHelper(BAN_CHARACTER, args, handler);
    }

    /**
     * @brief 处理IP封禁命令
     * @param handler 聊天命令处理器
     * @param args 命令参数（IP地址 持续时间 原因）
     * @return 封禁成功返回true，否则返回false
     *
     * 命令格式: .ban ip <IP地址> <持续时间> <原因>
     * 调用时机: GM需要封禁特定IP地址时
     *
     * IP封禁将阻止该IP地址的所有连接请求
     */
    static bool HandleBanIPCommand(ChatHandler* handler, char const* args)
    {
        return HandleBanHelper(BAN_IP, args, handler);
    }

    /**
     * @brief 封禁辅助函数
     * @param mode 封禁模式（账号/角色/IP）
     * @param args 命令参数字符串
     * @param handler 聊天命令处理器
     * @return 封禁成功返回true，否则返回false
     *
     * 统一处理账号、角色、IP三种封禁模式的通用逻辑
     * 性能注意: 包含数据库查询操作
     *
     * 处理流程:
     * 1. 解析参数（名称/IP、持续时间、原因）
     * 2. 根据封禁模式验证输入格式
     * 3. 执行封禁操作
     * 4. 返回操作结果并广播消息
     */
    static bool HandleBanHelper(BanMode mode, char const* args, ChatHandler* handler)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析名称或IP地址
        char* cnameOrIP = strtok((char*)args, " ");
        if (!cnameOrIP)
            return false;

        std::string nameOrIP = cnameOrIP;

        // 解析封禁持续时间
        char* durationStr = strtok(nullptr, " ");
        if (!durationStr || !atoi(durationStr))
            return false;

        // 解析封禁原因
        char* reasonStr = strtok(nullptr, "");
        if (!reasonStr)
            return false;

        // 根据封禁模式验证输入
        switch (mode)
        {
            case BAN_ACCOUNT:
                // 账号名转换为标准格式（大写）
                if (!Utf8ToUpperOnlyLatin(nameOrIP))
                {
                    handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, nameOrIP.c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                break;
            case BAN_CHARACTER:
                // 角色名规范化
                if (!normalizePlayerName(nameOrIP))
                {
                    handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                break;
            case BAN_IP:
                // 验证IP地址格式
                if (!IsIPAddress(nameOrIP.c_str()))
                    return false;
                break;
        }

        // 获取执行者名称
        std::string author = handler->GetSession() ? handler->GetSession()->GetPlayerName() : "Server";

        // 执行封禁操作
        switch (sWorld->BanAccount(mode, nameOrIP, durationStr, reasonStr, author))
        {
            case BAN_SUCCESS:
                // 封禁成功
                if (atoi(durationStr) > 0)
                {
                    // 临时封禁
                    if (sWorld->getBoolConfig(CONFIG_SHOW_BAN_IN_WORLD))
                        sWorld->SendWorldText(LANG_BAN_ACCOUNT_YOUBANNEDMESSAGE_WORLD, author.c_str(), nameOrIP.c_str(), secsToTimeString(TimeStringToSecs(durationStr), TimeFormat::ShortText).c_str(), reasonStr);
                    else
                        handler->PSendSysMessage(LANG_BAN_YOUBANNED, nameOrIP.c_str(), secsToTimeString(TimeStringToSecs(durationStr), TimeFormat::ShortText).c_str(), reasonStr);
                }
                else
                {
                    // 永久封禁
                    if (sWorld->getBoolConfig(CONFIG_SHOW_BAN_IN_WORLD))
                        sWorld->SendWorldText(LANG_BAN_ACCOUNT_YOUPERMBANNEDMESSAGE_WORLD, author.c_str(), nameOrIP.c_str(), reasonStr);
                    else
                        handler->PSendSysMessage(LANG_BAN_YOUPERMBANNED, nameOrIP.c_str(), reasonStr);
                }
                break;
            case BAN_SYNTAX_ERROR:
                // 语法错误
                return false;
            case BAN_NOTFOUND:
                // 未找到目标
                switch (mode)
                {
                    default:
                        handler->PSendSysMessage(LANG_BAN_NOTFOUND, "account", nameOrIP.c_str());
                        break;
                    case BAN_CHARACTER:
                        handler->PSendSysMessage(LANG_BAN_NOTFOUND, "character", nameOrIP.c_str());
                        break;
                    case BAN_IP:
                        handler->PSendSysMessage(LANG_BAN_NOTFOUND, "ip", nameOrIP.c_str());
                        break;
                }
                handler->SetSentErrorMessage(true);
                return false;
            case BAN_EXISTS:
                // 封禁已存在
                handler->PSendSysMessage(LANG_BAN_EXISTS);
                break;
        }

        return true;
    }

    /**
     * @brief 查询账号封禁信息
     * @param handler 聊天命令处理器
     * @param args 命令参数（账号名）
     * @return 查询成功返回true，否则返回false
     *
     * 命令格式: .baninfo account <账号名>
     * 调用时机: GM需要查看账号的封禁历史记录时
     * 性能注意: 会查询登录数据库获取封禁记录
     *
     * 显示信息包括:
     * - 封禁时间
     * - 解封时间
     * - 封禁原因
     * - 执行封禁的GM
     * - 封禁是否仍处于激活状态
     */
    static bool HandleBanInfoAccountCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析账号名
        char* nameStr = strtok((char*)args, "");
        if (!nameStr)
            return false;

        std::string accountName = nameStr;
        // 转换为大写格式
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取账号ID
        uint32 accountId = AccountMgr::GetId(accountName);
        if (!accountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            return true;
        }

        // 调用辅助函数显示封禁信息
        return HandleBanInfoHelper(accountId, accountName.c_str(), handler);
    }

    /**
     * @brief 封禁信息查询辅助函数
     * @param accountId 账号ID
     * @param accountName 账号名称
     * @param handler 聊天命令处理器
     * @return 查询成功返回true
     *
     * 从数据库查询并显示账号的所有封禁历史记录
     * 性能注意: 执行数据库查询，按封禁日期排序显示
     *
     * 显示内容:
     * - 封禁日期
     * - 封禁时长
     * - 是否激活
     * - 封禁原因
     * - 执行者
     */
    static bool HandleBanInfoHelper(uint32 accountId, char const* accountName, ChatHandler* handler)
    {
        // 查询账号封禁历史，按封禁日期升序排列
        QueryResult result = LoginDatabase.PQuery("SELECT FROM_UNIXTIME(bandate), unbandate-bandate, active, unbandate, banreason, bannedby FROM account_banned WHERE id = '{}' ORDER BY bandate ASC", accountId);
        if (!result)
        {
            handler->PSendSysMessage(LANG_BANINFO_NOACCOUNTBAN, accountName);
            return true;
        }

        // 显示封禁历史标题
        handler->PSendSysMessage(LANG_BANINFO_BANHISTORY, accountName);

        // 遍历所有封禁记录
        do
        {
            Field* fields = result->Fetch();

            time_t unbanDate = time_t(fields[3].GetUInt32());
            bool active = false;
            // 判断封禁是否仍处于激活状态
            if (fields[2].GetBool() && (fields[1].GetUInt64() == uint64(0) || unbanDate >= GameTime::GetGameTime()))
                active = true;

            // 判断是否永久封禁
            bool permanent = (fields[1].GetUInt64() == uint64(0));
            std::string banTime = permanent ? handler->GetTrinityString(LANG_BANINFO_INFINITE) : secsToTimeString(fields[1].GetUInt64(), TimeFormat::ShortText);

            // 显示单条封禁记录
            handler->PSendSysMessage(LANG_BANINFO_HISTORYENTRY,
                fields[0].GetCString(), banTime.c_str(), active ? handler->GetTrinityString(LANG_YES) : handler->GetTrinityString(LANG_NO), fields[4].GetCString(), fields[5].GetCString());
        }
        while (result->NextRow());

        return true;
    }

    /**
     * @brief 查询角色封禁信息
     * @param handler 聊天命令处理器
     * @param args 命令参数（角色名）
     * @return 查询成功返回true，否则返回false
     *
     * 命令格式: .baninfo character <角色名>
     * 调用时机: GM需要查看角色的封禁历史记录时
     * 性能注意: 会查询角色数据库和缓存
     *
     * 处理流程:
     * 1. 规范化角色名
     * 2. 查找角色（在线优先，离线查缓存）
     * 3. 查询角色的封禁记录
     * 4. 显示封禁历史
     */
    static bool HandleBanInfoCharacterCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        std::string name(args);
        // 规范化角色名
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage(LANG_BANINFO_NOCHARACTER);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 尝试查找在线玩家
        Player* target = ObjectAccessor::FindPlayerByName(name);
        ObjectGuid::LowType targetGuid = 0;

        if (!target)
        {
            // 角色不在线，从缓存查找
            ObjectGuid fullGuid = sCharacterCache->GetCharacterGuidByName(name);
            if (fullGuid.IsEmpty())
            {
                handler->SendSysMessage(LANG_BANINFO_NOCHARACTER);
                handler->SetSentErrorMessage(true);
                return false;
            }

            targetGuid = fullGuid.GetCounter();
        }
        else
            targetGuid = target->GetGUID().GetCounter();

        // 查询角色封禁记录
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BANINFO);
        stmt->setUInt32(0, targetGuid);
        PreparedQueryResult result = CharacterDatabase.Query(stmt);
        if (!result)
        {
            handler->PSendSysMessage(LANG_CHAR_NOT_BANNED, name.c_str());
            return true;
        }

        // 显示封禁历史
        handler->PSendSysMessage(LANG_BANINFO_BANHISTORY, name.c_str());
        do
        {
            Field* fields = result->Fetch();
            time_t unbanDate = time_t(fields[3].GetUInt32());
            bool active = false;
            // 判断封禁是否仍处于激活状态
            if (fields[2].GetUInt8() && (!fields[1].GetUInt32() || unbanDate >= GameTime::GetGameTime()))
                active = true;

            // 判断是否永久封禁
            bool permanent = (fields[1].GetUInt32() == uint32(0));
            std::string banTime = permanent ? handler->GetTrinityString(LANG_BANINFO_INFINITE) : secsToTimeString(fields[1].GetUInt32(), TimeFormat::ShortText);

            // 显示单条封禁记录
            handler->PSendSysMessage(LANG_BANINFO_HISTORYENTRY,
                TimeToTimestampStr(fields[0].GetUInt32()).c_str(), banTime.c_str(), active ? handler->GetTrinityString(LANG_YES) : handler->GetTrinityString(LANG_NO), fields[4].GetCString(), fields[5].GetCString());
        }
        while (result->NextRow());

        return true;
    }

    /**
     * @brief 查询IP封禁信息
     * @param handler 聊天命令处理器
     * @param args 命令参数（IP地址）
     * @return 查询成功返回true，否则返回false
     *
     * 命令格式: .baninfo ip <IP地址>
     * 调用时机: GM需要查看IP地址的封禁信息时
     * 性能注意: 会查询登录数据库并转义IP字符串防止SQL注入
     *
     * 显示信息包括:
     * - IP地址
     * - 封禁日期
     * - 解封日期
     * - 封禁时长
     * - 封禁原因
     * - 执行者
     */
    static bool HandleBanInfoIPCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析IP地址
        char* ipStr = strtok((char*)args, "");
        if (!ipStr)
            return false;

        // 验证IP地址格式
        if (!IsIPAddress(ipStr))
            return false;

        std::string IP = ipStr;

        // 转义IP字符串防止SQL注入
        LoginDatabase.EscapeString(IP);
        // 查询IP封禁信息
        QueryResult result = LoginDatabase.PQuery("SELECT ip, FROM_UNIXTIME(bandate), FROM_UNIXTIME(unbandate), unbandate-UNIX_TIMESTAMP(), banreason, bannedby, unbandate-bandate FROM ip_banned WHERE ip = '{}'", IP);
        if (!result)
        {
            handler->PSendSysMessage(LANG_BANINFO_NOIP);
            return true;
        }

        // 显示IP封禁详细信息
        Field* fields = result->Fetch();
        bool permanent = !fields[6].GetUInt64();
        handler->PSendSysMessage(LANG_BANINFO_IPENTRY,
            fields[0].GetCString(), fields[1].GetCString(), permanent ? handler->GetTrinityString(LANG_BANINFO_NEVER) : fields[2].GetCString(),
            permanent ? handler->GetTrinityString(LANG_BANINFO_INFINITE) : secsToTimeString(fields[3].GetUInt64(), TimeFormat::ShortText).c_str(), fields[4].GetCString(), fields[5].GetCString());

        return true;
    }

    /**
     * @brief 列出账号封禁列表
     * @param handler 聊天命令处理器
     * @param args 命令参数（可选的过滤字符串）
     * @return 查询成功返回true
     *
     * 命令格式: .banlist account [过滤字符串]
     * 调用时机: GM需要查看所有被封禁的账号时
     * 性能注意: 会先清理过期的封禁记录，然后查询数据库
     *
     * 支持功能:
     * - 不带参数: 显示所有封禁账号
     * - 带过滤参数: 显示匹配过滤字符串的封禁账号
     */
    static bool HandleBanListAccountCommand(ChatHandler* handler, char const* args)
    {
        LoginDatabasePreparedStatement* stmt = nullptr;

        // 清理过期的IP封禁记录
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_EXPIRED_IP_BANS);
        LoginDatabase.Execute(stmt);

        // 解析过滤参数
        char* filterStr = strtok((char*)args, " ");
        std::string filter = filterStr ? filterStr : "";

        PreparedQueryResult result;

        // 根据是否有过滤条件执行不同查询
        if (filter.empty())
        {
            // 查询所有封禁账号
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BANNED_ALL);
            result = LoginDatabase.Query(stmt);
        }
        else
        {
            // 按过滤条件查询
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BANNED_BY_FILTER);
            stmt->setString(0, filter);
            result = LoginDatabase.Query(stmt);
        }

        if (!result)
        {
            handler->PSendSysMessage(LANG_BANLIST_NOACCOUNT);
            return true;
        }

        // 调用辅助函数显示结果
        return HandleBanListHelper(result, handler);
    }

    /**
     * @brief 封禁列表显示辅助函数
     * @param result 查询结果集
     * @param handler 聊天命令处理器
     * @return 始终返回true
     *
     * 根据执行环境（游戏内聊天/控制台）以不同格式显示封禁列表
     * 性能注意: 可能执行多次数据库查询获取详细信息
     *
     * 显示格式:
     * - 游戏内聊天: 简短输出，仅显示账号名
     * - 控制台: 详细表格输出，包含封禁时间、解封时间、执行者、原因等
     */
    static bool HandleBanListHelper(PreparedQueryResult result, ChatHandler* handler)
    {
        handler->PSendSysMessage(LANG_BANLIST_MATCHINGACCOUNT);

        // 游戏内聊天短格式输出
        if (handler->GetSession())
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 accountid = fields[0].GetUInt32();

                // 查询账号名称
                QueryResult banResult = LoginDatabase.PQuery("SELECT account.username FROM account, account_banned WHERE account_banned.id='{}' AND account_banned.id = account.id", accountid);
                if (banResult)
                {
                    Field* fields2 = banResult->Fetch();
                    handler->PSendSysMessage("%s", fields2[0].GetCString());
                }
            }
            while (result->NextRow());
        }
        // 控制台详细表格输出
        else
        {
            handler->SendSysMessage(LANG_BANLIST_ACCOUNTS);
            handler->SendSysMessage(" ===============================================================================");
            handler->SendSysMessage(LANG_BANLIST_ACCOUNTS_HEADER);
            do
            {
                handler->SendSysMessage("-------------------------------------------------------------------------------");
                Field* fields = result->Fetch();
                uint32 accountId = fields[0].GetUInt32();

                std::string accountName;

                // 根据查询结果字段数判断获取账号名的方式
                if (result->GetFieldCount() > 1)
                    // 从查询结果直接获取账号名
                    accountName = fields[1].GetString();
                else
                    // 从账号管理器获取账号名
                    AccountMgr::GetName(accountId, accountName);

                // 查询封禁详细信息（id为uint32，无SQL注入风险）
                QueryResult banInfo = LoginDatabase.PQuery("SELECT bandate, unbandate, bannedby, banreason FROM account_banned WHERE id = {} ORDER BY unbandate", accountId);
                if (banInfo)
                {
                    Field* fields2 = banInfo->Fetch();
                    do
                    {
                        // 转换封禁时间
                        time_t timeBan = time_t(fields2[0].GetUInt32());
                        tm tmBan;
                        localtime_r(&timeBan, &tmBan);

                        // 判断是否永久封禁
                        if (fields2[0].GetUInt32() == fields2[1].GetUInt32())
                        {
                            // 永久封禁格式
                            handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|   permanent  |%-15.15s|%-15.15s|",
                                accountName.c_str(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                                fields2[2].GetCString(), fields2[3].GetCString());
                        }
                        else
                        {
                            // 临时封禁格式
                            time_t timeUnban = time_t(fields2[1].GetUInt32());
                            tm tmUnban;
                            localtime_r(&timeUnban, &tmUnban);
                            handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|%02d-%02d-%02d %02d:%02d|%-15.15s|%-15.15s|",
                                accountName.c_str(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                                tmUnban.tm_year%100, tmUnban.tm_mon+1, tmUnban.tm_mday, tmUnban.tm_hour, tmUnban.tm_min,
                                fields2[2].GetCString(), fields2[3].GetCString());
                        }
                    }
                    while (banInfo->NextRow());
                }
            }
            while (result->NextRow());

            handler->SendSysMessage(" ===============================================================================");
        }

        return true;
    }

    /**
     * @brief 列出角色封禁列表
     * @param handler 聊天命令处理器
     * @param args 命令参数（过滤字符串）
     * @return 查询成功返回true，否则返回false
     *
     * 命令格式: .banlist character <过滤字符串>
     * 调用时机: GM需要查看被封禁的角色列表时
     * 性能注意: 会查询角色数据库，支持模糊匹配
     *
     * 必须提供过滤参数来限制结果范围
     */
    static bool HandleBanListCharacterCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析过滤字符串
        char* filterStr = strtok((char*)args, " ");
        if (!filterStr)
            return false;

        std::string filter(filterStr);
        // 按名称过滤查询被封禁的角色
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUID_BY_NAME_FILTER);
        stmt->setString(0, filter);
        PreparedQueryResult result = CharacterDatabase.Query(stmt);
        if (!result)
        {
            handler->PSendSysMessage(LANG_BANLIST_NOCHARACTER);
            return true;
        }

        handler->PSendSysMessage(LANG_BANLIST_MATCHINGCHARACTER);

        // 游戏内聊天短格式输出
        if (handler->GetSession())
        {
            do
            {
                Field* fields = result->Fetch();
                // 查询被封禁的角色名
                CharacterDatabasePreparedStatement* stmt2 = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BANNED_NAME);
                stmt2->setUInt32(0, fields[0].GetUInt32());
                PreparedQueryResult banResult = CharacterDatabase.Query(stmt2);
                if (banResult)
                    handler->PSendSysMessage("%s", (*banResult)[0].GetCString());
            }
            while (result->NextRow());
        }
        // 控制台详细表格输出
        else
        {
            handler->SendSysMessage(LANG_BANLIST_CHARACTERS);
            handler->SendSysMessage(" =============================================================================== ");
            handler->SendSysMessage(LANG_BANLIST_CHARACTERS_HEADER);
            do
            {
                handler->SendSysMessage("-------------------------------------------------------------------------------");

                Field* fields = result->Fetch();

                std::string char_name = fields[1].GetString();

                // 查询角色的封禁详细信息
                CharacterDatabasePreparedStatement* stmt2 = CharacterDatabase.GetPreparedStatement(CHAR_SEL_BANINFO_LIST);
                stmt2->setUInt32(0, fields[0].GetUInt32());
                PreparedQueryResult banInfo = CharacterDatabase.Query(stmt2);
                if (banInfo)
                {
                    Field* banFields = banInfo->Fetch();
                    do
                    {
                        // 转换封禁时间
                        time_t timeBan = time_t(banFields[0].GetUInt32());
                        tm tmBan;
                        localtime_r(&timeBan, &tmBan);

                        // 判断是否永久封禁
                        if (banFields[0].GetUInt32() == banFields[1].GetUInt32())
                        {
                            // 永久封禁格式
                            handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|   permanent  |%-15.15s|%-15.15s|",
                                char_name.c_str(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                                banFields[2].GetCString(), banFields[3].GetCString());
                        }
                        else
                        {
                            // 临时封禁格式
                            time_t timeUnban = time_t(banFields[1].GetUInt32());
                            tm tmUnban;
                            localtime_r(&timeUnban, &tmUnban);
                            handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|%02d-%02d-%02d %02d:%02d|%-15.15s|%-15.15s|",
                                char_name.c_str(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                                tmUnban.tm_year%100, tmUnban.tm_mon+1, tmUnban.tm_mday, tmUnban.tm_hour, tmUnban.tm_min,
                                banFields[2].GetCString(), banFields[3].GetCString());
                        }
                    }
                    while (banInfo->NextRow());
                }
            }
            while (result->NextRow());
            handler->SendSysMessage(" =============================================================================== ");
        }

        return true;
    }

    /**
     * @brief 列出IP封禁列表
     * @param handler 聊天命令处理器
     * @param args 命令参数（可选的IP过滤字符串）
     * @return 查询成功返回true
     *
     * 命令格式: .banlist ip [IP地址过滤]
     * 调用时机: GM需要查看被封禁的IP地址列表时
     * 性能注意: 会清理过期封禁记录，转义过滤字符串防止SQL注入
     *
     * 支持功能:
     * - 不带参数: 显示所有封禁的IP
     * - 带IP参数: 显示匹配的IP封禁记录
     */
    static bool HandleBanListIPCommand(ChatHandler* handler, char const* args)
    {
        // 清理过期的IP封禁记录
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_EXPIRED_IP_BANS);
        LoginDatabase.Execute(stmt);

        // 解析过滤参数
        char* filterStr = strtok((char*)args, " ");
        std::string filter = filterStr ? filterStr : "";
        // 转义过滤字符串防止SQL注入
        LoginDatabase.EscapeString(filter);

        PreparedQueryResult result;

        // 根据是否有过滤条件执行不同查询
        if (filter.empty())
        {
            // 查询所有封禁的IP
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_IP_BANNED_ALL);
            result = LoginDatabase.Query(stmt);
        }
        else
        {
            // 按IP地址过滤查询
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_IP_BANNED_BY_IP);
            stmt->setString(0, filter);
            result = LoginDatabase.Query(stmt);
        }

        if (!result)
        {
            handler->PSendSysMessage(LANG_BANLIST_NOIP);
            return true;
        }

        handler->PSendSysMessage(LANG_BANLIST_MATCHINGIP);

        // 游戏内聊天短格式输出
        if (handler->GetSession())
        {
            do
            {
                Field* fields = result->Fetch();
                handler->PSendSysMessage("%s", fields[0].GetCString());
            }
            while (result->NextRow());
        }
        // 控制台详细表格输出
        else
        {
            handler->SendSysMessage(LANG_BANLIST_IPS);
            handler->SendSysMessage(" ===============================================================================");
            handler->SendSysMessage(LANG_BANLIST_IPS_HEADER);
            do
            {
                handler->SendSysMessage("-------------------------------------------------------------------------------");
                Field* fields = result->Fetch();
                // 转换封禁时间
                time_t timeBan = time_t(fields[1].GetUInt32());
                tm tmBan;
                localtime_r(&timeBan, &tmBan);

                // 判断是否永久封禁
                if (fields[1].GetUInt32() == fields[2].GetUInt32())
                {
                    // 永久封禁格式
                    handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|   permanent  |%-15.15s|%-15.15s|",
                        fields[0].GetCString(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                        fields[3].GetCString(), fields[4].GetCString());
                }
                else
                {
                    // 临时封禁格式
                    time_t timeUnban = time_t(fields[2].GetUInt32());
                    tm tmUnban;
                    localtime_r(&timeUnban, &tmUnban);
                    handler->PSendSysMessage("|%-15.15s|%02d-%02d-%02d %02d:%02d|%02d-%02d-%02d %02d:%02d|%-15.15s|%-15.15s|",
                        fields[0].GetCString(), tmBan.tm_year%100, tmBan.tm_mon+1, tmBan.tm_mday, tmBan.tm_hour, tmBan.tm_min,
                        tmUnban.tm_year%100, tmUnban.tm_mon+1, tmUnban.tm_mday, tmUnban.tm_hour, tmUnban.tm_min,
                        fields[3].GetCString(), fields[4].GetCString());
                }
            }
            while (result->NextRow());

            handler->SendSysMessage(" ===============================================================================");
        }

        return true;
    }

    /**
     * @brief 解除账号封禁
     * @param handler 聊天命令处理器
     * @param args 命令参数（账号名）
     * @return 解封成功返回true，否则返回false
     *
     * 命令格式: .unban account <账号名>
     * 调用时机: GM需要解除账号封禁时
     */
    static bool HandleUnBanAccountCommand(ChatHandler* handler, char const* args)
    {
        return HandleUnBanHelper(BAN_ACCOUNT, args, handler);
    }

    /**
     * @brief 解除角色封禁
     * @param handler 聊天命令处理器
     * @param args 命令参数（角色名）
     * @return 解封成功返回true，否则返回false
     *
     * 命令格式: .unban character <角色名>
     * 调用时机: GM需要解除角色封禁时
     * 性能注意: 会查询数据库验证角色是否存在
     */
    static bool HandleUnBanCharacterCommand(ChatHandler* handler, char const* args)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析角色名
        char* nameStr = strtok((char*)args, " ");
        if (!nameStr)
            return false;

        std::string name = nameStr;

        // 规范化角色名
        if (!normalizePlayerName(name))
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 执行解封操作
        if (!sWorld->RemoveBanCharacter(name))
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 显示成功消息
        handler->PSendSysMessage(LANG_UNBAN_UNBANNED, name.c_str());
        return true;
    }

    /**
     * @brief 通过角色名解除其所在账号的封禁
     * @param handler 聊天命令处理器
     * @param args 命令参数（角色名）
     * @return 解封成功返回true，否则返回false
     *
     * 命令格式: .unban playeraccount <角色名>
     * 调用时机: GM只知道角色名但需要解除账号封禁时
     */
    static bool HandleUnBanAccountByCharCommand(ChatHandler* handler, char const* args)
    {
        return HandleUnBanHelper(BAN_CHARACTER, args, handler);
    }

    /**
     * @brief 解除IP封禁
     * @param handler 聊天命令处理器
     * @param args 命令参数（IP地址）
     * @return 解封成功返回true，否则返回false
     *
     * 命令格式: .unban ip <IP地址>
     * 调用时机: GM需要解除IP封禁时
     */
    static bool HandleUnBanIPCommand(ChatHandler* handler, char const* args)
    {
        return HandleUnBanHelper(BAN_IP, args, handler);
    }

    /**
     * @brief 解封辅助函数
     * @param mode 解封模式（账号/角色/IP）
     * @param args 命令参数字符串
     * @param handler 聊天命令处理器
     * @return 解封成功返回true，否则返回false
     *
     * 统一处理账号、角色、IP三种解封模式的通用逻辑
     * 性能注意: 包含数据库查询操作
     *
     * 处理流程:
     * 1. 解析参数（名称/IP）
     * 2. 根据解封模式验证输入格式
     * 3. 执行解封操作
     * 4. 返回操作结果
     */
    static bool HandleUnBanHelper(BanMode mode, char const* args, ChatHandler* handler)
    {
        // 参数验证
        if (!*args)
            return false;

        // 解析名称或IP地址
        char* nameOrIPStr = strtok((char*)args, " ");
        if (!nameOrIPStr)
            return false;

        std::string nameOrIP = nameOrIPStr;

        // 根据解封模式验证输入
        switch (mode)
        {
            case BAN_ACCOUNT:
                // 账号名转换为标准格式（大写）
                if (!Utf8ToUpperOnlyLatin(nameOrIP))
                {
                    handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, nameOrIP.c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                break;
            case BAN_CHARACTER:
                // 角色名规范化
                if (!normalizePlayerName(nameOrIP))
                {
                    handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                break;
            case BAN_IP:
                // 验证IP地址格式
                if (!IsIPAddress(nameOrIP.c_str()))
                    return false;
                break;
        }

        // 执行解封操作
        if (sWorld->RemoveBanAccount(mode, nameOrIP))
            handler->PSendSysMessage(LANG_UNBAN_UNBANNED, nameOrIP.c_str());
        else
            handler->PSendSysMessage(LANG_UNBAN_ERROR, nameOrIP.c_str());

        return true;
    }
};

/**
 * @brief 注册封禁命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册封禁命令脚本实例。
 * 这是脚本系统的标准入口点，将脚本添加到命令处理系统中。
 */
void AddSC_ban_commandscript()
{
    new ban_commandscript();
}
