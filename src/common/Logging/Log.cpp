/**
 * @file Log.cpp
 * @brief 日志系统核心实现文件
 *
 * 本文件实现了TrinityCore日志系统的核心功能，包括：
 * - 日志系统单例管理
 * - 日志输出器（Appender）的创建和管理
 * - 日志记录器（Logger）的创建和管理
 * - 同步和异步日志写入
 * - 配置文件加载和解析
 * - 日志级别动态调整
 *
 * 日志系统架构：
 * - Log: 日志系统单例，管理所有Logger和Appender
 * - Logger: 日志记录器，按类型组织日志（如"entities.player"）
 * - Appender: 日志输出器，控制日志输出目标（控制台、文件、数据库等）
 * - LogMessage: 日志消息对象，包含日志内容和元数据
 * - LogOperation: 异步日志操作封装，用于线程安全的异步写入
 *
 * 支持的特性：
 * - 分层日志器结构（如"entities.player.dump"继承"entities.player"的配置）
 * - 多种输出目标（控制台、文件、数据库）
 * - 可配置的日志级别（TRACE、DEBUG、INFO、WARN、ERROR、FATAL）
 * - 异步日志写入（通过ASIO实现）
 * - 运行时动态调整日志级别
 * - GM命令审计日志
 *
 * @author TrinityCore Team
 * @copyright GNU General Public License version 2
 */

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

#include "Log.h"
#include "AppenderConsole.h"
#include "AppenderFile.h"
#include "Common.h"
#include "Config.h"
#include "Duration.h"
#include "Errors.h"
#include "Logger.h"
#include "LogMessage.h"
#include "LogOperation.h"
#include "Strand.h"
#include "StringConvert.h"
#include "Util.h"
#include <sstream>

/**
 * @brief Log类构造函数
 *
 * 职责：
 *   初始化日志系统单例对象，设置默认值并注册内置的日志输出器类型
 *
 * 初始化内容：
 *   - AppenderId: 输出器ID计数器初始化为0
 *   - lowestLogLevel: 最低日志级别初始化为FATAL（最高级别）
 *   - _ioContext/_strand: 异步IO上下文和线程安全strand初始化为空
 *   - m_logsTimestamp: 生成时间戳字符串用于日志文件命名
 *   - 注册控制台输出器(AppenderConsole)和文件输出器(AppenderFile)到工厂
 */
Log::Log() : AppenderId(0), lowestLogLevel(LOG_LEVEL_FATAL), _ioContext(nullptr), _strand(nullptr)
{
    m_logsTimestamp = "_" + GetTimestampStr();
    RegisterAppender<AppenderConsole>();
    RegisterAppender<AppenderFile>();
}

/**
 * @brief Log类析构函数
 *
 * 职责：
 *   清理日志系统资源，释放异步IO组件并关闭所有日志输出器和记录器
 *
 * 清理流程：
 *   1. 删除strand对象（线程安全调度器）
 *   2. 调用Close()关闭并清理所有日志器和输出器
 */
Log::~Log()
{
    delete _strand;
    Close();
}

/**
 * @brief 获取下一个可用的输出器ID
 *
 * 职责：
 *   生成唯一递增的输出器ID，用于标识每个新创建的日志输出器
 *
 * 返回值：
 *   uint8 - 新的输出器ID（当前计数器值，然后计数器自增）
 *
 * 说明：
 *   使用简单的递增计数器确保每个输出器都有唯一ID
 */
uint8 Log::NextAppenderId()
{
    return AppenderId++;
}

/**
 * @brief 根据名称查找日志输出器
 *
 * 职责：
 *   在已注册的输出器集合中查找指定名称的输出器对象
 *
 * 参数：
 *   name - 要查找的输出器名称
 *
 * 返回值：
 *   Appender* - 找到的输出器指针，未找到则返回nullptr
 *
 * 主要流程：
 *   1. 遍历appenders映射表
 *   2. 比较每个输出器的名称与目标名称
 *   3. 找到则返回输出器指针，否则返回nullptr
 */
Appender* Log::GetAppenderByName(std::string_view name)
{
    auto it = appenders.begin();
    while (it != appenders.end() && it->second && it->second->getName() != name)
        ++it;

    return it == appenders.end() ? nullptr : it->second.get();
}

/**
 * @brief 从配置文件创建日志输出器
 *
 * 职责：
 *   根据配置文件中的设置创建并初始化日志输出器对象
 *
 * 参数：
 *   appenderName - 配置键名（格式：Appender.xxx）
 *
 * 配置格式：
 *   type, level, flags, optional1, optional2
 *   - type: 输出器类型（Console=1, File=2等）
 *   - level: 日志级别
 *   - flags: 输出器标志
 *   - optional1/optional2: 可选参数（File类型为文件名和模式，Console类型为颜色）
 *
 * 主要流程：
 *   1. 从配置管理器获取配置字符串
 *   2. 解析配置项（类型、级别、标志等）
 *   3. 验证类型和级别的有效性
 *   4. 通过工厂函数创建输出器实例
 *   5. 将输出器注册到appenders映射表
 *
 * 错误处理：
 *   - 配置格式错误时输出错误信息到stderr
 *   - 捕获InvalidAppenderArgsException异常并输出错误信息
 */
void Log::CreateAppenderFromConfig(std::string const& appenderName)
{
    if (appenderName.empty())
        return;

    // Format = type, level, flags, optional1, optional2
    // if type = File. optional1 = file and option2 = mode
    // if type = Console. optional1 = Color
    std::string options = sConfigMgr->GetStringDefault(appenderName, "");

    std::vector<std::string_view> tokens = Trinity::Tokenize(options, ',', true);

    size_t const size = tokens.size();
    std::string name = appenderName.substr(9);

    if (size < 2)
    {
        fprintf(stderr, "Log::CreateAppenderFromConfig: Wrong configuration for appender %s. Config line: %s\n", name.c_str(), options.c_str());
        return;
    }

    AppenderFlags flags = APPENDER_FLAGS_NONE;
    AppenderType type = AppenderType(Trinity::StringTo<uint8>(tokens[0]).value_or(APPENDER_INVALID));
    LogLevel level = LogLevel(Trinity::StringTo<uint8>(tokens[1]).value_or(LOG_LEVEL_INVALID));

    auto factoryFunction = appenderFactory.find(type);
    if (factoryFunction == appenderFactory.end())
    {
        fprintf(stderr, "Log::CreateAppenderFromConfig: Unknown type '%s' for appender %s\n", std::string(tokens[0]).c_str(), name.c_str());
        return;
    }

    if (level > NUM_ENABLED_LOG_LEVELS)
    {
        fprintf(stderr, "Log::CreateAppenderFromConfig: Wrong Log Level '%s' for appender %s\n", std::string(tokens[1]).c_str(), name.c_str());
        return;
    }

    if (size > 2)
    {
        if (Optional<uint8> flagsVal = Trinity::StringTo<uint8>(tokens[2]))
            flags = AppenderFlags(*flagsVal);
        else
        {
            fprintf(stderr, "Log::CreateAppenderFromConfig: Unknown flags '%s' for appender %s\n", std::string(tokens[2]).c_str(), name.c_str());
            return;
        }
    }

    try
    {
        Appender* appender = factoryFunction->second(NextAppenderId(), name, level, flags, tokens);
        appenders[appender->getId()].reset(appender);
    }
    catch (InvalidAppenderArgsException const& iaae)
    {
        fprintf(stderr, "%s\n", iaae.what());
    }
}

/**
 * @brief 从配置文件创建日志记录器
 *
 * 职责：
 *   根据配置文件中的设置创建日志记录器并关联相应的输出器
 *
 * 参数：
 *   appenderName - 配置键名（格式：Logger.xxx）
 *
 * 配置格式：
 *   LogLevel, AppenderList
 *   - LogLevel: 日志级别
 *   - AppenderList: 空格分隔的输出器名称列表
 *
 * 主要流程：
 *   1. 从配置管理器获取配置字符串
 *   2. 解析日志级别和输出器列表
 *   3. 检查日志器是否已存在（避免重复定义）
 *   4. 创建Logger实例并设置日志级别
 *   5. 更新系统最低日志级别
 *   6. 将配置中的输出器关联到日志器
 *
 * 错误处理：
 *   - 配置缺失时输出错误信息
 *   - 配置格式错误时输出错误信息
 *   - 日志器重复定义时输出错误信息
 *   - 输出器不存在时输出错误信息
 */
void Log::CreateLoggerFromConfig(std::string const& appenderName)
{
    if (appenderName.empty())
        return;

    LogLevel level = LOG_LEVEL_DISABLED;

    std::string options = sConfigMgr->GetStringDefault(appenderName, "");
    std::string name = appenderName.substr(7);

    if (options.empty())
    {
        fprintf(stderr, "Log::CreateLoggerFromConfig: Missing config option Logger.%s\n", name.c_str());
        return;
    }

    std::vector<std::string_view> tokens = Trinity::Tokenize(options, ',', true);

    if (tokens.size() != 2)
    {
        fprintf(stderr, "Log::CreateLoggerFromConfig: Wrong config option Logger.%s=%s\n", name.c_str(), options.c_str());
        return;
    }

    std::unique_ptr<Logger>& logger = loggers[name];
    if (logger)
    {
        fprintf(stderr, "Error while configuring Logger %s. Already defined\n", name.c_str());
        return;
    }

    level = LogLevel(Trinity::StringTo<uint8>(tokens[0]).value_or(LOG_LEVEL_INVALID));
    if (level > NUM_ENABLED_LOG_LEVELS)
    {
        fprintf(stderr, "Log::CreateLoggerFromConfig: Wrong Log Level '%s' for logger %s\n", std::string(tokens[0]).c_str(), name.c_str());
        return;
    }

    if (level < lowestLogLevel)
        lowestLogLevel = level;

    logger = std::make_unique<Logger>(name, level);
    //fprintf(stdout, "Log::CreateLoggerFromConfig: Created Logger %s, Level %u\n", name.c_str(), level);

    for (std::string_view appenderName : Trinity::Tokenize(tokens[1], ' ', false))
    {
        if (Appender* appender = GetAppenderByName(appenderName))
        {
            logger->addAppender(appender->getId(), appender);
            //fprintf(stdout, "Log::CreateLoggerFromConfig: Added Appender %s to Logger %s\n", appender->getName().c_str(), name.c_str());
        }
        else
            fprintf(stderr, "Error while configuring Appender %s in Logger %s. Appender does not exist\n", std::string(appenderName).c_str(), name.c_str());
    }
}

/**
 * @brief 从配置文件读取所有输出器配置
 *
 * 职责：
 *   遍历配置文件中所有以"Appender."开头的配置项并创建相应的输出器
 *
 * 主要流程：
 *   1. 从配置管理器获取所有以"Appender."开头的键名
 *   2. 遍历每个键名调用CreateAppenderFromConfig创建输出器
 */
void Log::ReadAppendersFromConfig()
{
    std::vector<std::string> keys = sConfigMgr->GetKeysByString("Appender.");
    for (std::string const& appenderName : keys)
        CreateAppenderFromConfig(appenderName);
}

/**
 * @brief 从配置文件读取所有日志记录器配置
 *
 * 职责：
 *   遍历配置文件中所有以"Logger."开头的配置项并创建相应的日志记录器
 *   如果配置无效，则创建默认配置确保系统可用
 *
 * 主要流程：
 *   1. 从配置管理器获取所有以"Logger."开头的键名
 *   2. 遍历每个键名调用CreateLoggerFromConfig创建日志器
 *   3. 检查是否存在根日志器(LOGGER_ROOT)
 *   4. 如果配置无效，创建默认配置：
 *      - 控制台输出器（DEBUG级别）
 *      - 根日志器（ERROR级别）
 *      - server日志器（INFO级别）
 *
 * 默认配置说明：
 *   当配置文件缺少根日志器时，系统自动创建基本的日志配置
 *   确保即使配置错误，日志系统也能正常工作
 */
void Log::ReadLoggersFromConfig()
{
    std::vector<std::string> keys = sConfigMgr->GetKeysByString("Logger.");
    for (std::string const& loggerName : keys)
        CreateLoggerFromConfig(loggerName);

    // Bad config configuration, creating default config
    if (loggers.find(LOGGER_ROOT) == loggers.end())
    {
        fprintf(stderr, "Wrong Loggers configuration. Review your Logger config section.\n"
                        "Creating default loggers [root (Error), server (Info)] to console\n");

        Close(); // Clean any Logger or Appender created

        AppenderConsole* appender = new AppenderConsole(NextAppenderId(), "Console", LOG_LEVEL_DEBUG, APPENDER_FLAGS_NONE, {});
        appenders[appender->getId()].reset(appender);

        Logger* rootLogger = new Logger(LOGGER_ROOT, LOG_LEVEL_ERROR);
        rootLogger->addAppender(appender->getId(), appender);
        loggers[LOGGER_ROOT].reset(rootLogger);

        Logger* serverLogger = new Logger("server", LOG_LEVEL_INFO);
        serverLogger->addAppender(appender->getId(), appender);
        loggers["server"].reset(serverLogger);
    }
}

/**
 * @brief 注册输出器创建函数到工厂映射表
 *
 * 职责：
 *   将输出器类型索引和对应的创建函数注册到工厂映射表中，用于后续动态创建输出器实例
 *
 * 参数：
 *   index - 输出器类型索引（AppenderType枚举值）
 *   appenderCreateFn - 输出器创建函数指针
 *
 * 说明：
 *   使用断言确保同一类型不会被重复注册
 */
void Log::RegisterAppender(uint8 index, AppenderCreatorFn appenderCreateFn)
{
    auto itr = appenderFactory.find(index);
    ASSERT(itr == appenderFactory.end());
    appenderFactory[index] = appenderCreateFn;
}

/**
 * @brief 输出日志消息的实现方法
 *
 * 职责：
 *   创建日志消息对象并通过write方法写入日志系统
 *
 * 参数：
 *   filter - 日志过滤器类型（如"entities.player", "network"等）
 *   level - 日志级别（TRACE, DEBUG, INFO, WARN, ERROR, FATAL等）
 *   messageFormat - 消息格式字符串
 *   messageFormatArgs - 格式化参数
 *
 * 主要流程：
 *   1. 使用格式化参数创建日志消息字符串
 *   2. 创建LogMessage对象
 *   3. 调用write方法写入日志系统
 */
void Log::OutMessageImpl(std::string_view filter, LogLevel level, Trinity::FormatStringView messageFormat, Trinity::FormatArgs messageFormatArgs)
{
    write(std::make_unique<LogMessage>(level, filter, Trinity::StringVFormat(messageFormat, messageFormatArgs)));
}

/**
 * @brief 输出GM命令日志的实现方法
 *
 * 职责：
 *   记录GM（游戏管理员）执行的命令，用于审计和管理
 *
 * 参数：
 *   account - 执行命令的账号ID
 *   messageFormat - 消息格式字符串
 *   messageFormatArgs - 格式化参数
 *
 * 主要流程：
 *   1. 使用格式化参数创建命令消息字符串
 *   2. 创建LogMessage对象，设置日志类型为"commands.gm"，级别为INFO
 *   3. 将账号ID作为附加参数存储在消息中
 *   4. 调用write方法写入日志系统
 */
void Log::OutCommandImpl(uint32 account, Trinity::FormatStringView messageFormat, Trinity::FormatArgs messageFormatArgs)
{
    write(std::make_unique<LogMessage>(LOG_LEVEL_INFO, "commands.gm", Trinity::StringVFormat(messageFormat, messageFormatArgs), Trinity::ToString(account)));
}

/**
 * @brief 写入日志消息的核心方法
 *
 * 职责：
 *   将日志消息写入到对应的日志记录器，支持同步和异步两种模式
 *
 * 参数：
 *   msg - 日志消息对象的唯一指针
 *
 * 主要流程：
 *   1. 根据消息类型获取对应的日志记录器
 *   2. 如果配置了异步IO上下文：
 *      - 创建LogOperation对象包装日志操作
 *      - 通过strand确保线程安全地投递到IO线程执行
 *   3. 否则直接同步调用logger->write()写入日志
 *
 * 线程安全：
 *   使用strand确保异步模式下日志写入的顺序性和线程安全
 */
void Log::write(std::unique_ptr<LogMessage> msg) const
{
    Logger const* logger = GetLoggerByType(msg->type);

    if (_ioContext)
    {
        std::shared_ptr<LogOperation> logOperation = std::make_shared<LogOperation>(logger, std::move(msg));
        Trinity::Asio::post(*_ioContext, Trinity::Asio::bind_executor(*_strand, [logOperation]() { logOperation->call(); }));
    }
    else
        logger->write(msg.get());
}

/**
 * @brief 根据类型获取日志记录器
 *
 * 职责：
 *   根据日志类型字符串查找对应的日志记录器，支持层级查找机制
 *
 * 参数：
 *   type - 日志类型字符串（如"entities.player.dump"）
 *
 * 返回值：
 *   Logger const* - 找到的日志记录器指针，未找到则返回nullptr
 *
 * 主要流程：
 *   1. 在loggers映射表中精确查找type对应的记录器
 *   2. 如果找到则直接返回
 *   3. 如果type是根日志器(LOGGER_ROOT)但未找到，返回nullptr
 *   4. 否则进行层级查找：
 *      - 提取父类型（去掉最后一个"."后的部分）
 *      - 递归查找父类型日志器
 *
 * 层级查找示例：
 *   查找"entities.player.dump" -> 查找"entities.player" -> 查找"entities" -> 查找"root"
 */
Logger const* Log::GetLoggerByType(std::string const& type) const
{
    auto it = loggers.find(type);
    if (it != loggers.end())
        return it->second.get();

    if (type == LOGGER_ROOT)
        return nullptr;

    std::string parentLogger = LOGGER_ROOT;
    size_t found = type.find_last_of('.');
    if (found != std::string::npos)
        parentLogger = type.substr(0, found);

    return GetLoggerByType(parentLogger);
}

/**
 * @brief 获取当前时间戳字符串
 *
 * 职责：
 *   生成当前系统时间的格式化字符串，用于日志文件命名等场景
 *
 * 返回值：
 *   std::string - 格式化的时间戳字符串（YYYY-MM-DD_HH-MM-SS）
 *
 * 时间格式：
 *   YYYY - 4位年份
 *   MM - 2位月份（01-12）
 *   DD - 2位日期（01-31）
 *   HH - 2位小时（00-23）
 *   MM - 2位分钟（00-59）
 *   SS - 2位秒钟（00-59）
 *
 * 示例：
 *   2024-03-15_14-30-25
 */
std::string Log::GetTimestampStr()
{
    time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::tm aTm;
    localtime_r(&tt, &aTm);

    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    return Trinity::StringFormat("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}",
        aTm.tm_year + 1900, aTm.tm_mon + 1, aTm.tm_mday, aTm.tm_hour, aTm.tm_min, aTm.tm_sec);
}

/**
 * @brief 动态设置日志级别
 *
 * 职责：
 *   运行时动态修改日志记录器或输出器的日志级别
 *
 * 参数：
 *   name - 日志器或输出器的名称
 *   newLeveli - 新的日志级别值
 *   isLogger - true表示设置日志器级别，false表示设置输出器级别（默认为true）
 *
 * 返回值：
 *   bool - 设置成功返回true，失败返回false
 *
 * 主要流程：
 *   如果isLogger为true（设置日志器级别）：
 *   1. 在loggers中查找指定名称的日志器
 *   2. 设置日志器的新级别
 *   3. 更新系统最低日志级别（如果需要）
 *
 *   如果isLogger为false（设置输出器级别）：
 *   1. 查找指定名称的输出器
 *   2. 设置输出器的新级别
 *
 * 错误处理：
 *   - 级别值为负数时返回false
 *   - 找不到指定名称的日志器或输出器时返回false
 */
bool Log::SetLogLevel(std::string const& name, int32 newLeveli, bool isLogger /* = true */)
{
    if (newLeveli < 0)
        return false;

    LogLevel newLevel = LogLevel(newLeveli);

    if (isLogger)
    {
        auto it = loggers.begin();
        while (it != loggers.end() && it->second->getName() != name)
            ++it;

        if (it == loggers.end())
            return false;

        it->second->setLogLevel(newLevel);

        if (newLevel != LOG_LEVEL_DISABLED && newLevel < lowestLogLevel)
            lowestLogLevel = newLevel;
    }
    else
    {
        Appender* appender = GetAppenderByName(name);
        if (!appender)
            return false;

        appender->setLogLevel(newLevel);
    }

    return true;
}

/**
 * @brief 输出角色数据转储日志
 *
 * 职责：
 *   记录玩家角色的详细数据转储信息，用于调试和数据恢复
 *
 * 参数：
 *   str - 要转储的数据字符串
 *   accountId - 账号ID
 *   guid - 角色GUID
 *   name - 角色名称
 *
 * 主要流程：
 *   1. 检查数据字符串有效性和日志级别是否满足条件
 *   2. 构建转储消息，包含账号、GUID、名称等信息
 *   3. 创建LogMessage对象，类型为"entities.player.dump"
 *   4. 设置附加参数（GUID_角色名格式）
 *   5. 调用write方法写入日志系统
 *
 * 输出格式：
 *   == START DUMP == (account: xxx guid: xxx name: xxx)
 *   [转储数据]
 *   == END DUMP ==
 */
void Log::OutCharDump(char const* str, uint32 accountId, uint64 guid, char const* name)
{
    if (!str || !ShouldLog("entities.player.dump", LOG_LEVEL_INFO))
        return;

    std::ostringstream ss;
    ss << "== START DUMP == (account: " << accountId << " guid: " << guid << " name: " << name
       << ")\n" << str << "\n== END DUMP ==\n";

    std::unique_ptr<LogMessage> msg(new LogMessage(LOG_LEVEL_INFO, "entities.player.dump", ss.str()));
    std::ostringstream param;
    param << guid << '_' << name;

    msg->param1 = param.str();

    write(std::move(msg));
}

/**
 * @brief 设置领域ID
 *
 * 职责：
 *   为所有输出器设置领域ID，用于多领域服务器的日志标识
 *
 * 参数：
 *   id - 领域ID
 *
 * 说明：
 *   遍历所有输出器，调用其setRealmId方法设置领域ID
 */
void Log::SetRealmId(uint32 id)
{
    for (std::pair<uint8 const, std::unique_ptr<Appender>>& appender : appenders)
        appender.second->setRealmId(id);
}

/**
 * @brief 关闭日志系统
 *
 * 职责：
 *   清理所有日志记录器和输出器，关闭日志系统
 *
 * 说明：
 *   清空loggers和appenders映射表，释放所有资源
 */
void Log::Close()
{
    loggers.clear();
    appenders.clear();
}

/**
 * @brief 判断是否应该记录指定级别的日志
 *
 * 职责：
 *   快速判断是否需要记录特定类型和级别的日志，用于性能优化
 *
 * 参数：
 *   type - 日志类型字符串
 *   level - 日志级别
 *
 * 返回值：
 *   bool - true表示应该记录日志，false表示不记录
 *
 * 主要流程：
 *   1. 快速检查：如果日志级别低于系统最低日志级别，直接返回false
 *   2. 获取对应的日志记录器
 *   3. 检查日志器是否存在且未禁用
 *   4. 比较日志级别是否满足记录条件
 *
 * 性能优化：
 *   先进行快速检查避免不必要的日志器查找操作
 *   TODO: 使用缓存存储类型映射关系以加速查找
 */
bool Log::ShouldLog(std::string const& type, LogLevel level) const
{
    // TODO: Use cache to store "Type.sub1.sub2": "Type" equivalence, should
    // Speed up in cases where requesting "Type.sub1.sub2" but only configured
    // Logger "Type"

    // Don't even look for a logger if the LogLevel is lower than lowest log levels across all loggers
    if (level < lowestLogLevel)
        return false;

    Logger const* logger = GetLoggerByType(type);
    if (!logger)
        return false;

    LogLevel logLevel = logger->getLogLevel();
    return logLevel != LOG_LEVEL_DISABLED && logLevel <= level;
}

/**
 * @brief 获取日志系统单例实例
 *
 * 职责：
 *   提供全局访问点，返回日志系统的单例对象
 *
 * 返回值：
 *   Log* - 日志系统单例指针
 *
 * 实现方式：
 *   使用静态局部变量实现线程安全的单例模式（Meyers' Singleton）
 */
Log* Log::instance()
{
    static Log instance;
    return &instance;
}

/**
 * @brief 初始化日志系统
 *
 * 职责：
 *   初始化日志系统的异步IO上下文并加载配置
 *
 * 参数：
 *   ioContext - ASIO IO上下文指针（可选，为nullptr时使用同步模式）
 *
 * 主要流程：
 *   1. 如果提供了ioContext：
 *      - 保存ioContext指针
 *      - 创建strand对象用于线程安全的异步日志写入
 *   2. 调用LoadFromConfig从配置文件加载日志配置
 *
 * 运行模式：
 *   - 异步模式：提供了ioContext，日志写入通过异步IO投递
 *   - 同步模式：ioContext为nullptr，日志直接同步写入
 */
void Log::Initialize(Trinity::Asio::IoContext* ioContext)
{
    if (ioContext)
    {
        _ioContext = ioContext;
        _strand = new Trinity::Asio::Strand(*ioContext);
    }

    LoadFromConfig();
}

/**
 * @brief 设置为同步模式
 *
 * 职责：
 *   将日志系统从异步模式切换为同步模式
 *
 * 主要流程：
 *   1. 删除strand对象
 *   2. 将strand和ioContext指针置空
 *
 * 说明：
 *   调用此方法后，日志写入将直接同步执行，不再通过异步IO投递
 */
void Log::SetSynchronous()
{
    delete _strand;
    _strand = nullptr;
    _ioContext = nullptr;
}

/**
 * @brief 从配置文件加载日志系统配置
 *
 * 职责：
 *   清理现有配置并从配置文件重新加载日志系统的完整配置
 *
 * 主要流程：
 *   1. 关闭现有日志系统，清理所有日志器和输出器
 *   2. 重置最低日志级别为FATAL
 *   3. 重置输出器ID计数器为0
 *   4. 从配置文件读取日志目录路径：
 *      - 获取"LogsDir"配置项
 *      - 确保路径以分隔符结尾
 *   5. 从配置文件读取所有输出器配置
 *   6. 从配置文件读取所有日志记录器配置
 *
 * 说明：
 *   该方法可以在运行时调用以重新加载日志配置
 */
void Log::LoadFromConfig()
{
    Close();

    lowestLogLevel = LOG_LEVEL_FATAL;
    AppenderId = 0;
    m_logsDir = sConfigMgr->GetStringDefault("LogsDir", "");
    if (!m_logsDir.empty())
        if ((m_logsDir.at(m_logsDir.length() - 1) != '/') && (m_logsDir.at(m_logsDir.length() - 1) != '\\'))
            m_logsDir.push_back('/');

    ReadAppendersFromConfig();
    ReadLoggersFromConfig();
}
