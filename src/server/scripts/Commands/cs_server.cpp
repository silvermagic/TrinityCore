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

/* ScriptData
Name: server_commandscript
%Complete: 100
Comment: All server related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_server.cpp
 * @brief 服务器管理命令模块
 *
 * 本模块实现了所有与服务器管理相关的GM命令,包括:
 * - 服务器关闭和重启控制
 * - 服务器信息查询
 * - MOTD(每日消息)管理
 * - 玩家数量限制管理
 * - 日志级别设置
 * - 服务器状态控制(开启/关闭)
 * - 尸体清理
 * - 调试信息显示
 *
 * 这些命令通常需要较高的权限等级才能执行,用于服务器运维和管理。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DatabaseLoader.h"
#include "GameTime.h"
#include "GitRevision.h"
#include "Language.h"
#include "Log.h"
#include "MySQLThreading.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "Realm.h"
#include "ServerMotd.h"
#include "UpdateTime.h"
#include "Util.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "World.h"
#include "WorldSession.h"
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <numeric>

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class server_commandscript
 * @brief 服务器管理命令脚本类
 *
 * 继承自CommandScript,提供服务器管理相关的所有GM命令处理函数。
 * 包括服务器关闭、重启、信息查询、配置管理等功能。
 */
class server_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化服务器命令脚本,注册脚本名称为"server_commandscript"
     */
    server_commandscript() : CommandScript("server_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回服务器命令表结构
     *
     * 构建并返回所有服务器相关命令的层次结构,包括:
     * - server corpses: 清理尸体
     * - server debug: 显示调试信息
     * - server exit: 立即退出服务器
     * - server idlerestart: 空闲重启
     * - server idleshutdown: 空闲关闭
     * - server info: 显示服务器信息
     * - server motd: 显示/设置每日消息
     * - server plimit: 设置玩家数量限制
     * - server restart: 重启服务器
     * - server shutdown: 关闭服务器
     * - server set: 设置服务器参数
     *
     * @note 此函数在服务器启动时调用一次,构建的命令表会被缓存
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        // 空闲重启命令表 - 当服务器空闲时重启
        static std::vector<ChatCommand> serverIdleRestartCommandTable =
        {
            { "cancel", rbac::RBAC_PERM_COMMAND_SERVER_IDLERESTART_CANCEL, true, &HandleServerShutDownCancelCommand, "" },
            { ""   ,    rbac::RBAC_PERM_COMMAND_SERVER_IDLERESTART,        true, &HandleServerIdleRestartCommand,    "" },
        };

        // 空闲关闭命令表 - 当服务器空闲时关闭
        static std::vector<ChatCommand> serverIdleShutdownCommandTable =
        {
            { "cancel", rbac::RBAC_PERM_COMMAND_SERVER_IDLESHUTDOWN_CANCEL, true, &HandleServerShutDownCancelCommand, "" },
            { ""   ,    rbac::RBAC_PERM_COMMAND_SERVER_IDLESHUTDOWN,        true, &HandleServerIdleShutDownCommand,   "" },
        };

        // 重启命令表 - 立即或延迟重启服务器
        static std::vector<ChatCommand> serverRestartCommandTable =
        {
            { "cancel", rbac::RBAC_PERM_COMMAND_SERVER_RESTART_CANCEL, true, &HandleServerShutDownCancelCommand, "" },
            { "force",  rbac::RBAC_PERM_COMMAND_SERVER_RESTART_FORCE,  true, &HandleServerForceRestartCommand,   "" },
            { ""   ,    rbac::RBAC_PERM_COMMAND_SERVER_RESTART,        true, &HandleServerRestartCommand,        "" },
        };

        // 关闭命令表 - 立即或延迟关闭服务器
        static std::vector<ChatCommand> serverShutdownCommandTable =
        {
            { "cancel", rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_CANCEL, true, &HandleServerShutDownCancelCommand, "" },
            { "force",  rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE,  true, &HandleServerForceShutDownCommand,  "" },
            { ""   ,    rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN,        true, &HandleServerShutDownCommand,       "" },
        };

        // 设置命令表 - 修改服务器参数
        static std::vector<ChatCommand> serverSetCommandTable =
        {
            { "loglevel", rbac::RBAC_PERM_COMMAND_SERVER_SET_LOGLEVEL, true, &HandleServerSetLogLevelCommand, "" },
            { "motd",     rbac::RBAC_PERM_COMMAND_SERVER_SET_MOTD,     true, &HandleServerSetMotdCommand,     "" },
            { "closed",   rbac::RBAC_PERM_COMMAND_SERVER_SET_CLOSED,   true, &HandleServerSetClosedCommand,   "" },
        };

        // 主服务器命令表
        static std::vector<ChatCommand> serverCommandTable =
        {
            { "corpses",      rbac::RBAC_PERM_COMMAND_SERVER_CORPSES,      true, &HandleServerCorpsesCommand, "" },
            { "debug",        rbac::RBAC_PERM_COMMAND_SERVER_DEBUG,        true, &HandleServerDebugCommand,   "" },
            { "exit",         rbac::RBAC_PERM_COMMAND_SERVER_EXIT,         true, &HandleServerExitCommand,    "" },
            { "idlerestart",  rbac::RBAC_PERM_COMMAND_SERVER_IDLERESTART,  true, nullptr,                     "", serverIdleRestartCommandTable },
            { "idleshutdown", rbac::RBAC_PERM_COMMAND_SERVER_IDLESHUTDOWN, true, nullptr,                     "", serverIdleShutdownCommandTable },
            { "info",         rbac::RBAC_PERM_COMMAND_SERVER_INFO,         true, &HandleServerInfoCommand,    "" },
            { "motd",         rbac::RBAC_PERM_COMMAND_SERVER_MOTD,         true, &HandleServerMotdCommand,    "" },
            { "plimit",       rbac::RBAC_PERM_COMMAND_SERVER_PLIMIT,       true, &HandleServerPLimitCommand,  "" },
            { "restart",      rbac::RBAC_PERM_COMMAND_SERVER_RESTART,      true, nullptr,                     "", serverRestartCommandTable },
            { "shutdown",     rbac::RBAC_PERM_COMMAND_SERVER_SHUTDOWN,     true, nullptr,                     "", serverShutdownCommandTable },
            { "set",          rbac::RBAC_PERM_COMMAND_SERVER_SET,          true, nullptr,                     "", serverSetCommandTable },
        };

        // 根命令表
        static std::vector<ChatCommand> commandTable =
        {
            { "server", rbac::RBAC_PERM_COMMAND_SERVER, true, nullptr, "", serverCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理尸体清理命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server corpses命令时调用
     *
     * @par 功能说明:
     * 触发服务器进行尸体过期检查,立即清理所有过期的玩家尸体
     * 这比等待自然的清理周期更快,适用于需要立即清理服务器资源的情况
     *
     * @par 性能注意事项:
     * - 可能会遍历所有地图上的尸体对象
     * - 如果尸体数量很多,可能会导致短暂的性能影响
     */
    static bool HandleServerCorpsesCommand(ChatHandler* /*handler*/, char const* /*args*/)
    {
        // 调用世界管理器移除旧尸体
        sWorld->RemoveOldCorpses();
        return true;
    }

    /**
     * @brief 处理服务器调试信息显示命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server debug命令时调用
     *
     * @par 功能说明:
     * 显示服务器的详细调试信息,包括:
     * - 版本信息(Git修订版本、编译信息)
     * - 依赖库版本(SSL、Boost、MySQL、CMake)
     * - 数据库更新配置
     * - 网络端口信息
     * - 地图数据状态(VMAPs、MMAPs)
     * - DBC语言环境
     * - 数据库队列大小
     *
     * @par 性能注意事项:
     * - 会查询数据库获取端口配置
     * - 会遍历文件系统计算地图目录大小
     * - 对于运维诊断很有用,但不应该在正常运行时频繁使用
     */
    static bool HandleServerDebugCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 获取世界服务器监听端口
        uint16 worldPort = uint16(sWorld->getIntConfig(CONFIG_PORT_WORLD));
        std::string dbPortOutput;

        // 查询登录数据库中配置的端口
        {
            uint16 dbPort = 0;
            if (QueryResult res = LoginDatabase.PQuery("SELECT port FROM realmlist WHERE id = {}", realm.Id.Realm))
                dbPort = (*res)[0].GetUInt16();

            if (dbPort)
                dbPortOutput = Trinity::StringFormat("Realmlist (Realm Id: {}) configured in port {}", realm.Id.Realm, dbPort);
            else
                dbPortOutput = Trinity::StringFormat("Realm Id: {} not found in `realmlist` table. Please check your setup", realm.Id.Realm);
        }

        // 显示版本信息
        handler->PSendSysMessage("%s", GitRevision::GetFullVersion());
        handler->PSendSysMessage("Using SSL version: %s (library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
        handler->PSendSysMessage("Using Boost version: %i.%i.%i", BOOST_VERSION / 100000, BOOST_VERSION / 100 % 1000, BOOST_VERSION % 100);
        handler->PSendSysMessage("Using MySQL version: %u", MySQL::GetLibraryVersion());
        handler->PSendSysMessage("Using CMake version: %s", GitRevision::GetCMakeVersion());

        handler->PSendSysMessage("Compiled on: %s", GitRevision::GetHostOSVersion());

        // 检查数据库自动更新配置
        uint32 updateFlags = sConfigMgr->GetIntDefault("Updates.EnableDatabases", DatabaseLoader::DATABASE_NONE);
        if (!updateFlags)
            handler->SendSysMessage("Automatic database updates are disabled for all databases!");
        else
        {
            static char const* const databaseNames[3 /*TOTAL_DATABASES*/] =
            {
                "Auth",
                "Characters",
                "World"
            };

            std::string availableUpdateDatabases;
            for (uint32 i = 0; i < 3 /* TOTAL_DATABASES*/; ++i)
            {
                if (!(updateFlags & (1 << i)))
                    continue;

                availableUpdateDatabases += databaseNames[i];
                if (i != 3 /*TOTAL_DATABASES*/ - 1)
                    availableUpdateDatabases += ", ";
            }

            handler->PSendSysMessage("Automatic database updates are enabled for the following databases: %s", availableUpdateDatabases.c_str());
        }

        // 显示网络端口信息
        handler->PSendSysMessage("Worldserver listening connections on port {}", worldPort);
        handler->PSendSysMessage("%s", dbPortOutput.c_str());

        // 检查虚拟地图(VMaps)配置
        bool vmapIndoorCheck = sWorld->getBoolConfig(CONFIG_VMAP_INDOOR_CHECK);
        bool vmapLOSCheck = VMAP::VMapFactory::createOrGetVMapManager()->isLineOfSightCalcEnabled();
        bool vmapHeightCheck = VMAP::VMapFactory::createOrGetVMapManager()->isHeightCalcEnabled();

        // 检查移动地图(MMaps)配置
        bool mmapEnabled = sWorld->getBoolConfig(CONFIG_ENABLE_MMAPS);

        // 计算地图数据目录大小
        std::string dataDir = sWorld->GetDataPath();
        std::vector<std::string> subDirs;
        subDirs.emplace_back("maps");
        if (vmapIndoorCheck || vmapLOSCheck || vmapHeightCheck)
        {
            handler->PSendSysMessage("VMAPs status: Enabled. LineOfSight: %i, getHeight: %i, indoorCheck: %i", vmapLOSCheck, vmapHeightCheck, vmapIndoorCheck);
            subDirs.emplace_back("vmaps");
        }
        else
            handler->SendSysMessage("VMAPs status: Disabled");

        if (mmapEnabled)
        {
            handler->SendSysMessage("MMAPs status: Enabled");
            subDirs.emplace_back("mmaps");
        }
        else
            handler->SendSysMessage("MMAPs status: Disabled");

        // 遍历地图目录并计算总大小
        for (std::string const& subDir : subDirs)
        {
            boost::filesystem::path mapPath(dataDir);
            mapPath /= subDir;

            if (!boost::filesystem::exists(mapPath))
            {
                handler->PSendSysMessage("%s directory doesn't exist!. Using path: %s", subDir.c_str(), mapPath.generic_string().c_str());
                continue;
            }

            auto end = boost::filesystem::directory_iterator();
            std::size_t folderSize = std::accumulate(boost::filesystem::directory_iterator(mapPath), end, std::size_t(0), [](std::size_t val, boost::filesystem::path const& mapFile)
            {
                boost::system::error_code ec;
                if (boost::filesystem::is_regular_file(mapFile, ec))
                    val += boost::filesystem::file_size(mapFile);
                return val;
            });

            handler->PSendSysMessage("%s directory located in %s. Total size: " SZFMTD " bytes", subDir.c_str(), mapPath.generic_string().c_str(), folderSize);
        }

        // 显示DBC语言环境信息
        LocaleConstant defaultLocale = sWorld->GetDefaultDbcLocale();
        uint32 availableLocalesMask = (1 << defaultLocale);

        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            LocaleConstant locale = static_cast<LocaleConstant>(i);
            if (locale == defaultLocale)
                continue;

            if (sWorld->GetAvailableDbcLocale(locale) != defaultLocale)
                availableLocalesMask |= (1 << locale);
        }

        std::string availableLocales;
        for (uint8 i = 0; i < TOTAL_LOCALES; ++i)
        {
            if (!(availableLocalesMask & (1 << i)))
                continue;

            availableLocales += localeNames[i];
            if (i != TOTAL_LOCALES - 1)
                availableLocales += " ";
        }

        handler->PSendSysMessage("Using %s DBC Locale as default. All available DBC locales: %s", localeNames[defaultLocale], availableLocales.c_str());

        // 显示数据库版本信息
        handler->PSendSysMessage("Using World DB: %s", sWorld->GetDBVersion());

        // 显示数据库队列大小
        handler->PSendSysMessage("LoginDatabase queue size: %zu", LoginDatabase.QueueSize());
        handler->PSendSysMessage("CharacterDatabase queue size: %zu", CharacterDatabase.QueueSize());
        handler->PSendSysMessage("WorldDatabase queue size: %zu", WorldDatabase.QueueSize());
        return true;
    }

    /**
     * @brief 处理服务器信息显示命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server info命令时调用
     *
     * @par 功能说明:
     * 显示服务器的基本运行信息,包括:
     * - 版本信息
     * - 在线玩家数量和最大玩家数量
     * - 活跃会话和队列会话数量
     * - 服务器运行时间
     * - 最后更新时间差
     * - 如果正在关闭,显示剩余时间
     *
     * @note 这是常用的服务器状态查询命令
     */
    static bool HandleServerInfoCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 收集服务器统计信息
        uint32 playersNum           = sWorld->GetPlayerCount();           // 当前在线玩家数
        uint32 maxPlayersNum        = sWorld->GetMaxPlayerCount();        // 最大玩家数记录
        uint32 activeClientsNum     = sWorld->GetActiveSessionCount();    // 活跃会话数
        uint32 queuedClientsNum     = sWorld->GetQueuedSessionCount();    // 队列中的会话数
        uint32 maxActiveClientsNum  = sWorld->GetMaxActiveSessionCount(); // 最大活跃会话数记录
        uint32 maxQueuedClientsNum  = sWorld->GetMaxQueuedSessionCount(); // 最大队列会话数记录
        std::string uptime          = secsToTimeString(GameTime::GetUptime()); // 运行时间字符串
        uint32 updateTime           = sWorldUpdateTime.GetLastUpdateTime();    // 最后更新时间

        // 显示版本和统计信息
        handler->PSendSysMessage("%s", GitRevision::GetFullVersion());
        handler->PSendSysMessage(LANG_CONNECTED_PLAYERS, playersNum, maxPlayersNum);
        handler->PSendSysMessage(LANG_CONNECTED_USERS, activeClientsNum, maxActiveClientsNum, queuedClientsNum, maxQueuedClientsNum);
        handler->PSendSysMessage(LANG_UPTIME, uptime.c_str());
        handler->PSendSysMessage(LANG_UPDATE_DIFF, updateTime);

        // 如果服务器正在关闭,显示剩余时间
        // 不能使用sWorld->ShutdownMsg,因为可能是从控制台执行的命令
        if (sWorld->IsShuttingDown())
            handler->PSendSysMessage(LANG_SHUTDOWN_TIMELEFT, secsToTimeString(sWorld->GetShutDownTimeLeft()).c_str());

        return true;
    }

    /**
     * @brief 处理MOTD(每日消息)显示命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server motd命令时调用
     *
     * @par 功能说明:
     * 显示当前服务器的每日消息(Message of the Day)
     * MOTD会在玩家登录时显示,也可以通过此命令查看
     */
    // Display the 'Message of the day' for the realm
    static bool HandleServerMotdCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->PSendSysMessage(LANG_MOTD_CURRENT, Motd::GetMotd());
        return true;
    }

    /**
     * @brief 处理玩家数量限制设置命令
     * @param handler 聊天处理器指针
     * @param args 命令参数,可以是数字或关键字
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server plimit [参数]命令时调用
     *
     * @par 功能说明:
     * 设置或显示服务器玩家数量限制,支持以下参数:
     * - player: 只允许玩家级别账户登录
     * - moderator: 只允许版主及以上级别账户登录
     * - gamemaster: 只允许GM及以上级别账户登录
     * - administrator: 只允许管理员级别账户登录
     * - reset: 重置为配置文件中的默认值
     * - 数字: 设置具体的玩家数量上限
     * - 负数: 设置最小账户安全级别限制
     * - 无参数: 显示当前限制
     *
     * @par 权限说明:
     * 需要管理员权限才能修改限制设置
     */
    static bool HandleServerPLimitCommand(ChatHandler* handler, char const* args)
    {
        // 如果提供了参数,则设置限制
        if (*args)
        {
            char* paramStr = strtok((char*)args, " ");
            if (!paramStr)
                return false;

            int32 limit = strlen(paramStr);

            // 根据不同的关键字设置不同的安全级别限制
            if (strncmp(paramStr, "player", limit) == 0)
                sWorld->SetPlayerSecurityLimit(SEC_PLAYER);
            else if (strncmp(paramStr, "moderator", limit) == 0)
                sWorld->SetPlayerSecurityLimit(SEC_MODERATOR);
            else if (strncmp(paramStr, "gamemaster", limit) == 0)
                sWorld->SetPlayerSecurityLimit(SEC_GAMEMASTER);
            else if (strncmp(paramStr, "administrator", limit) == 0)
                sWorld->SetPlayerSecurityLimit(SEC_ADMINISTRATOR);
            else if (strncmp(paramStr, "reset", limit) == 0)
            {
                // 重置为配置文件的默认值
                sWorld->SetPlayerAmountLimit(sConfigMgr->GetIntDefault("PlayerLimit", 100));
                sWorld->LoadDBAllowedSecurityLevel();
            }
            else
            {
                // 尝试解析为数字
                int32 value = atoi(paramStr);
                if (value < 0)
                    sWorld->SetPlayerSecurityLimit(AccountTypes(-value)); // 负数表示安全级别
                else
                    sWorld->SetPlayerAmountLimit(uint32(value)); // 正数表示玩家数量
            }
        }

        // 显示当前的玩家限制设置
        uint32 playerAmountLimit = sWorld->GetPlayerAmountLimit();
        AccountTypes allowedAccountType = sWorld->GetPlayerSecurityLimit();
        char const* secName = "";
        switch (allowedAccountType)
        {
            case SEC_PLAYER:
                secName = "Player";
                break;
            case SEC_MODERATOR:
                secName = "Moderator";
                break;
            case SEC_GAMEMASTER:
                secName = "Gamemaster";
                break;
            case SEC_ADMINISTRATOR:
                secName = "Administrator";
                break;
            default:
                secName = "<unknown>";
                break;
        }
        handler->PSendSysMessage("Player limits: amount %u, min. security level %s.", playerAmountLimit, secName);

        return true;
    }

    /**
     * @brief 处理取消服务器关闭/重启命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server shutdown cancel、.server restart cancel等取消命令时调用
     *
     * @par 功能说明:
     * 取消正在进行的服务器关闭或重启倒计时
     * 如果没有正在进行的关闭操作,则不会有任何效果
     */
    static bool HandleServerShutDownCancelCommand(ChatHandler* handler, char const* /*args*/)
    {
        // 尝试取消关闭倒计时,并返回取消时的剩余时间
        if (uint32 timer = sWorld->ShutdownCancel())
            handler->PSendSysMessage(LANG_SHUTDOWN_CANCELLED, timer);

        return true;
    }

    /**
     * @brief 检查当前会话是否是唯一连接的用户
     * @param mySession 当前会话指针
     * @return true 是唯一用户, false 有其他用户在线
     *
     * @par 功能说明:
     * 检查服务器上是否只有当前会话连接,用于判断是否可以安全地立即关闭服务器
     * 通过比较所有会话的远程地址来判断
     *
     * @note 控制台会话被视为不同的地址,因此如果控制台执行命令,有玩家在线时也会返回false
     */
    static bool IsOnlyUser(WorldSession* mySession)
    {
        // 检查是否有来自不同地址的会话连接
        std::string myAddr = mySession ? mySession->GetRemoteAddress() : "";
        SessionMap const& sessions = sWorld->GetAllSessions();
        for (SessionMap::value_type const& session : sessions)
            if (session.second && myAddr != session.second->GetRemoteAddress())
                return false;
        return true;
    }

    /**
     * @brief 处理服务器关闭命令
     * @param handler 聊天处理器指针
     * @param args 命令参数,格式: <delay> [exitcode] [reason]
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server shutdown命令时调用
     *
     * @par 功能说明:
     * 启动服务器关闭流程,支持延迟关闭和自定义退出码
     */
    static bool HandleServerShutDownCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, 0, SHUTDOWN_EXIT_CODE);
    }

    /**
     * @brief 处理服务器重启命令
     * @param handler 聊天处理器指针
     * @param args 命令参数,格式: <delay> [exitcode] [reason]
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server restart命令时调用
     *
     * @par 功能说明:
     * 启动服务器重启流程,使用重启退出码
     */
    static bool HandleServerRestartCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, SHUTDOWN_MASK_RESTART, RESTART_EXIT_CODE);
    }

    /**
     * @brief 处理强制服务器关闭命令
     * @param handler 聊天处理器指针
     * @param args 命令参数
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server shutdown force命令时调用
     *
     * @par 功能说明:
     * 强制关闭服务器,忽略"有其他玩家在线"的安全检查
     */
    static bool HandleServerForceShutDownCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, SHUTDOWN_MASK_FORCE, SHUTDOWN_EXIT_CODE);
    }

    /**
     * @brief 处理强制服务器重启命令
     * @param handler 聊天处理器指针
     * @param args 命令参数
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server restart force命令时调用
     *
     * @par 功能说明:
     * 强制重启服务器,使用强制和重启标志
     */
    static bool HandleServerForceRestartCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, SHUTDOWN_MASK_FORCE | SHUTDOWN_MASK_RESTART, RESTART_EXIT_CODE);
    }

    /**
     * @brief 处理空闲时服务器关闭命令
     * @param handler 聊天处理器指针
     * @param args 命令参数
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server idleshutdown命令时调用
     *
     * @par 功能说明:
     * 当服务器变为空闲状态时关闭服务器
     * 如果在倒计时期间有玩家登录,会取消关闭
     */
    static bool HandleServerIdleShutDownCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, SHUTDOWN_MASK_IDLE, SHUTDOWN_EXIT_CODE);
    }

    /**
     * @brief 处理空闲时服务器重启命令
     * @param handler 聊天处理器指针
     * @param args 命令参数
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server idlerestart命令时调用
     *
     * @par 功能说明:
     * 当服务器变为空闲状态时重启服务器
     */
    static bool HandleServerIdleRestartCommand(ChatHandler* handler, char const* args)
    {
        return ShutdownServer(handler, args, SHUTDOWN_MASK_RESTART | SHUTDOWN_MASK_IDLE, RESTART_EXIT_CODE);
    }

    /**
     * @brief 处理服务器立即退出命令
     * @param handler 聊天处理器指针
     * @param args 命令参数(未使用)
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server exit命令时调用
     *
     * @par 功能说明:
     * 立即停止服务器,不进行任何延迟或警告
     * 这是最快速的关闭方式,通常只在紧急情况下使用
     *
     * @warning 此命令不会保存玩家数据,可能导致数据丢失
     */
    // Exit the realm
    static bool HandleServerExitCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->SendSysMessage(LANG_COMMAND_EXIT);
        World::StopNow(SHUTDOWN_EXIT_CODE);
        return true;
    }

    /**
     * @brief 处理设置MOTD命令
     * @param handler 聊天处理器指针
     * @param args 新的MOTD文本
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.server set motd <text>命令时调用
     *
     * @par 功能说明:
     * 设置服务器的每日消息(Message of the Day)
     * 新的MOTD会立即生效,对后续登录的玩家可见
     */
    // Define the 'Message of the day' for the realm
    static bool HandleServerSetMotdCommand(ChatHandler* handler, char const* args)
    {
        Motd::SetMotd(args);
        handler->PSendSysMessage(LANG_MOTD_NEW, args);
        return true;
    }

    /**
     * @brief 处理设置服务器关闭状态命令
     * @param handler 聊天处理器指针
     * @param args 参数: on 或 off
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server set closed <on|off>命令时调用
     *
     * @par 功能说明:
     * 设置服务器是否对新客户端关闭:
     * - on: 服务器关闭,新客户端无法登录
     * - off: 服务器开放,允许新客户端登录
     *
     * @note 已在线的玩家不受影响,只影响新的登录尝试
     */
    // Set whether we accept new clients
    static bool HandleServerSetClosedCommand(ChatHandler* handler, char const* args)
    {
        if (strncmp(args, "on", 3) == 0)
        {
            handler->SendSysMessage(LANG_WORLD_CLOSED);
            sWorld->SetClosed(true);
            return true;
        }
        else if (strncmp(args, "off", 4) == 0)
        {
            handler->SendSysMessage(LANG_WORLD_OPENED);
            sWorld->SetClosed(false);
            return true;
        }

        handler->SendSysMessage(LANG_USE_BOL);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 处理设置日志级别命令
     * @param handler 聊天处理器指针
     * @param type 日志类型: "a"(appender)或"l"(logger)
     * @param name 日志器或appender的名称
     * @param level 日志级别(0-6)
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 当GM执行.server set loglevel <type> <name> <level>命令时调用
     *
     * @par 功能说明:
     * 动态调整日志级别,无需重启服务器
     * - type="a": 设置appender级别
     * - type="l": 设置logger级别
     * - level: 日志级别(0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=Disabled)
     *
     * @note 这是一个运行时配置,服务器重启后会恢复配置文件中的设置
     */
    // Set the level of logging
    static bool HandleServerSetLogLevelCommand(ChatHandler* /*handler*/, std::string const& type, std::string const& name, int32 level)
    {
        // 参数验证
        if (name.empty() || level < 0 || (type != "a" && type != "l"))
            return false;

        // 设置日志级别
        sLog->SetLogLevel(name, level, type == "l");
        return true;
    }

private:
    /**
     * @brief 解析退出码字符串
     * @param exitCodeStr 退出码字符串
     * @param exitCode 输出参数,解析后的退出码
     * @return true 解析成功, false 退出码无效
     *
     * @par 功能说明:
     * 将字符串转换为退出码整数,并进行有效性验证
     * 退出码必须在0-125范围内,避免与Shell的特殊返回码冲突
     *
     * @note 退出码126-255被许多Shell保留用于特殊用途
     */
    static bool ParseExitCode(char const* exitCodeStr, int32& exitCode)
    {
        exitCode = atoi(exitCodeStr);

        // 处理atoi()转换错误的情况
        if (exitCode == 0 && (exitCodeStr[0] != '0' || exitCodeStr[1] != '\0'))
            return false;

        // 退出码必须在0-125范围内
        // 126-255被许多Shell用于特殊返回码
        // 大于255的退出码在许多系统中不被支持
        if (exitCode < 0 || exitCode > 125)
            return false;

        return true;
    }

    /**
     * @brief 执行服务器关闭/重启的核心逻辑
     * @param handler 聊天处理器指针
     * @param args 命令参数,格式: <delay> [exitcode] [reason]
     * @param shutdownMask 关闭掩码,控制关闭行为(重启/强制/空闲)
     * @param defaultExitCode 默认退出码
     * @return true 命令执行成功, false 参数错误
     *
     * @par 调用时机:
     * 由各种关闭/重启命令的处理函数调用
     *
     * @par 功能说明:
     * 解析参数并启动服务器关闭流程:
     * 1. 解析延迟时间(秒数或时间字符串如"1h30m")
     * 2. 解析可选的退出码
     * 3. 解析可选的关闭原因
     * 4. 如果有玩家在线且延迟时间较短,自动延长到配置的最小时间
     * 5. 调用World::ShutdownServ启动关闭流程
     *
     * @par 参数格式:
     * - delay: 可以是数字(秒)或时间字符串(如"1h30m", "2h", "30s")
     * - exitcode: 可选的退出码(0-125)
     * - reason: 可选的关闭原因文本
     *
     * @par 安全机制:
     * - 如果不是强制关闭且有其他玩家在线,会自动延长延迟时间到配置的最小值
     * - 控制台执行的命令可以覆盖这个安全机制
     */
    static bool ShutdownServer(ChatHandler* handler, char const* args, uint32 shutdownMask, int32 defaultExitCode)
    {
        // 参数验证
        if (!*args)
            return false;

        if (strlen(args) > 255)
            return false;

        // 解析延迟时间参数
        // 格式: #delay [#exit_code] [reason]
        int32 delay = 0;
        char* delayStr = strtok((char*)args, " ");
        if (!delayStr)
            return false;

        // 判断延迟参数是数字还是时间字符串
        if (isNumeric(delayStr))
        {
            delay = atoi(delayStr);
            // 防止错误地将无效参数解释为0秒关闭时间
            if ((delay == 0 && (delayStr[0] != '0' || delayStr[1] != '\0')) || delay < 0)
                return false;
        }
        else
        {
            // 将时间字符串转换为秒数(如"1h30m" -> 5400秒)
            delay = TimeStringToSecs(std::string(delayStr));

            if (delay == 0)
                return false;
        }

        char* exitCodeStr = nullptr;
        char reason[256] = { 0 };

        // 解析剩余参数:退出码和关闭原因
        while (char* nextToken = strtok(nullptr, " "))
        {
            if (isNumeric(nextToken))
                exitCodeStr = nextToken;  // 数字参数作为退出码
            else
            {
                // 非数字参数作为关闭原因
                strcat(reason, nextToken);
                if (char* remainingTokens = strtok(nullptr, "\0"))
                {
                    strcat(reason, " ");
                    strcat(reason, remainingTokens);
                }
                break;
            }
        }

        // 解析退出码
        int32 exitCode = defaultExitCode;
        if (exitCodeStr)
            if (!ParseExitCode(exitCodeStr, exitCode))
                return false;

        // 安全检查:如果有玩家在线且不是强制关闭,自动延长延迟时间
        // Override parameter "delay" with the configuration value if there are still players connected and "force" parameter was not specified
        if (delay < (int32)sWorld->getIntConfig(CONFIG_FORCE_SHUTDOWN_THRESHOLD) && !(shutdownMask & SHUTDOWN_MASK_FORCE) && !IsOnlyUser(handler->GetSession()))
        {
            delay = (int32)sWorld->getIntConfig(CONFIG_FORCE_SHUTDOWN_THRESHOLD);
            handler->PSendSysMessage(LANG_SHUTDOWN_DELAYED, delay);
        }

        // 启动服务器关闭流程
        sWorld->ShutdownServ(delay, shutdownMask, static_cast<uint8>(exitCode), std::string(reason));

        return true;
    }
};

/**
 * @brief 注册服务器命令脚本
 *
 * 此函数在服务器启动时被脚本系统调用,用于注册服务器管理命令脚本。
 * 创建server_commandscript实例并将其添加到命令处理系统中。
 */
void AddSC_server_commandscript()
{
    new server_commandscript();
}
