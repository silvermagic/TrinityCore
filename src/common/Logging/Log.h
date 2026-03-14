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
 * @file Log.h
 * @brief 日志系统核心管理类头文件
 *
 * 本文件定义了Log类，是TrinityCore日志系统的核心入口点。
 * 提供统一的日志管理功能，支持多种日志输出方式和日志级别过滤。
 *
 * 日志系统架构：
 *   Log (单例管理器)
 *     ├── Logger (日志记录器，按类型分类)
 *     │     └── Appender (日志输出器，可多个)
 *     │           ├── AppenderConsole (控制台输出)
 *     │           └── AppenderFile (文件输出)
 *     └── LogMessage (日志消息载体)
 *
 * 主要特性：
 *   - 单例模式，全局唯一实例
 *   - 支持异步日志写入（通过ASIO）
 *   - 灵活的日志类型层级结构
 *   - 可配置的日志级别和输出目标
 *   - 支持运行时动态调整日志级别
 *
 * 日志级别（从高到低）：
 *   - FATAL (0): 致命错误，程序可能无法继续运行
 *   - ERROR (1): 错误，功能无法正常执行
 *   - WARN  (2): 警告，潜在问题
 *   - INFO  (3): 信息，重要运行信息
 *   - DEBUG (4): 调试，详细调试信息
 *   - TRACE (5): 跟踪，最详细的执行流程
 *
 * 日志类型命名规则：
 *   - root: 根日志器，所有未匹配日志的默认处理器
 *   - server: 服务器核心日志
 *   - entities.player: 玩家相关日志
 *   - network: 网络相关日志
 *   - commands.gm: GM命令审计日志
 *
 * 使用方式：
 *   使用日志宏进行日志输出：
 *   - TC_LOG_FATAL(filter, format, ...)
 *   - TC_LOG_ERROR(filter, format, ...)
 *   - TC_LOG_WARN(filter, format, ...)
 *   - TC_LOG_INFO(filter, format, ...)
 *   - TC_LOG_DEBUG(filter, format, ...)
 *   - TC_LOG_TRACE(filter, format, ...)
 *
 * 性能优化：
 *   - 使用lowestLogLevel进行快速过滤
 *   - 异步写入避免阻塞主线程
 *   - 日志宏在编译时检查格式字符串
 */

#ifndef TRINITYCORE_LOG_H
#define TRINITYCORE_LOG_H

#include "Define.h"
#include "AsioHacksFwd.h"
#include "LogCommon.h"
#include "StringFormat.h"

#include <memory>
#include <unordered_map>
#include <vector>

class Appender;
class Logger;
struct LogMessage;

namespace Trinity
{
    namespace Asio
    {
        class IoContext;
    }
}

#define LOGGER_ROOT "root"

/// Appender创建函数指针类型定义
typedef Appender*(*AppenderCreatorFn)(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& extraArgs);

/// Appender工厂模板函数，用于创建指定类型的Appender实例
template <class AppenderImpl>
Appender* CreateAppender(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& extraArgs)
{
    return new AppenderImpl(id, name, level, flags, extraArgs);
}

/**
 * @brief 日志管理类
 *
 * 提供统一的日志管理功能，支持多种日志输出方式（Appender）和日志级别过滤。
 * 采用单例模式，通过sLog宏访问全局实例。
 * 支持异步日志写入以提高性能。
 */
class TC_COMMON_API Log
{
    typedef std::unordered_map<std::string, Logger> LoggerMap;

    private:
        /// 私有构造函数（单例模式）
        Log();
        /// 私有析构函数
        ~Log();
        /// 禁用拷贝构造
        Log(Log const&) = delete;
        /// 禁用移动构造
        Log(Log&&) = delete;
        /// 禁用拷贝赋值
        Log& operator=(Log const&) = delete;
        /// 禁用移动赋值
        Log& operator=(Log&&) = delete;

    public:
        /// 获取Log单例实例
        static Log* instance();

        /**
         * @brief 初始化日志系统
         * @param ioContext ASIO IO上下文，用于异步日志写入
         */
        void Initialize(Trinity::Asio::IoContext* ioContext);

        /**
         * @brief 设置为同步模式
         * 注意：非线程安全，应在所有线程结束后从main()调用
         */
        void SetSynchronous();

        /**
         * @brief 从配置文件加载日志配置
         */
        void LoadFromConfig();

        /**
         * @brief 关闭日志系统，释放资源
         */
        void Close();

        /**
         * @brief 检查指定类型的日志是否应该被记录
         * @param type 日志类型/过滤器名称
         * @param level 日志级别
         * @return 是否应该记录该日志
         */
        bool ShouldLog(std::string const& type, LogLevel level) const;

        /**
         * @brief 设置日志级别
         * @param name 日志记录器或Appender名称
         * @param level 日志级别值
         * @param isLogger true表示设置Logger级别，false表示设置Appender级别
         * @return 设置是否成功
         */
        bool SetLogLevel(std::string const& name, int32 level, bool isLogger = true);

        /**
         * @brief 输出日志消息
         * @param filter 日志过滤器名称
         * @param level 日志级别
         * @param fmt 格式化字符串
         * @param args 格式化参数
         */
        template<typename... Args>
        void OutMessage(std::string_view filter, LogLevel const level, Trinity::FormatString<Args...> fmt, Args&&... args)
        {
            this->OutMessageImpl(filter, level, fmt, Trinity::MakeFormatArgs(args...));
        }

        /**
         * @brief 输出GM命令日志
         * @param account 账号ID
         * @param fmt 格式化字符串
         * @param args 格式化参数
         */
        template<typename... Args>
        void OutCommand(uint32 account, Trinity::FormatString<Args...> fmt, Args&&... args)
        {
            if (!ShouldLog("commands.gm", LOG_LEVEL_INFO))
                return;

            this->OutCommandImpl(account, fmt, Trinity::MakeFormatArgs(args...));
        }

        /**
         * @brief 输出角色转储数据
         * @param str 转储字符串数据
         * @param account_id 账号ID
         * @param guid 角色GUID
         * @param name 角色名称
         */
        void OutCharDump(char const* str, uint32 account_id, uint64 guid, char const* name);

        /**
         * @brief 设置当前Realm ID
         * @param id Realm ID
         */
        void SetRealmId(uint32 id);

        /**
         * @brief 注册Appender类型
         * 模板函数，用于注册自定义Appender类型到日志系统
         */
        template<class AppenderImpl>
        void RegisterAppender()
        {
            this->RegisterAppender(AppenderImpl::type, &CreateAppender<AppenderImpl>);
        }

        /// 获取日志文件存储目录
        std::string const& GetLogsDir() const { return m_logsDir; }
        /// 获取日志时间戳字符串
        std::string const& GetLogsTimestamp() const { return m_logsTimestamp; }

    private:
        /// 获取当前时间戳字符串
        static std::string GetTimestampStr();
        /// 写入日志消息（异步）
        void write(std::unique_ptr<LogMessage> msg) const;

        /// 根据类型获取Logger实例
        Logger const* GetLoggerByType(std::string const& type) const;
        /// 根据名称获取Appender实例
        Appender* GetAppenderByName(std::string_view name);
        /// 获取下一个可用的Appender ID
        uint8 NextAppenderId();
        /// 从配置创建Appender
        void CreateAppenderFromConfig(std::string const& name);
        /// 从配置创建Logger
        void CreateLoggerFromConfig(std::string const& name);
        /// 从配置读取所有Appender
        void ReadAppendersFromConfig();
        /// 从配置读取所有Logger
        void ReadLoggersFromConfig();
        /// 注册Appender创建函数
        void RegisterAppender(uint8 index, AppenderCreatorFn appenderCreateFn);
        /// OutMessage的实现函数
        void OutMessageImpl(std::string_view filter, LogLevel level, Trinity::FormatStringView messageFormat, Trinity::FormatArgs messageFormatArgs);
        /// OutCommand的实现函数
        void OutCommandImpl(uint32 account, Trinity::FormatStringView messageFormat, Trinity::FormatArgs messageFormatArgs);

        /// Appender工厂映射表，存储Appender类型与创建函数的映射
        std::unordered_map<uint8, AppenderCreatorFn> appenderFactory;
        /// Appender实例映射表，ID -> Appender实例
        std::unordered_map<uint8, std::unique_ptr<Appender>> appenders;
        /// Logger实例映射表，名称 -> Logger实例
        std::unordered_map<std::string, std::unique_ptr<Logger>> loggers;
        /// 当前Appender ID计数器
        uint8 AppenderId;
        /// 最低日志级别，用于快速过滤
        LogLevel lowestLogLevel;

        /// 日志文件存储目录路径
        std::string m_logsDir;
        /// 日志时间戳字符串
        std::string m_logsTimestamp;

        /// ASIO IO上下文，用于异步日志写入
        Trinity::Asio::IoContext* _ioContext;
        /// ASIO Strand，用于保证线程安全的异步写入
        Trinity::Asio::Strand* _strand;
};

/// 全局日志实例访问宏
#define sLog Log::instance()

#ifdef PERFORMANCE_PROFILING
/// 性能分析模式下禁用日志消息体
#define TC_LOG_MESSAGE_BODY(filterType__, level__, ...) ((void)0)
#elif TRINITY_PLATFORM != TRINITY_PLATFORM_WINDOWS

/// 日志消息体宏，在编译时捕获格式错误
#define TC_LOG_MESSAGE_BODY(filterType__, level__, ...)                 \
        do {                                                            \
            if (sLog->ShouldLog(filterType__, level__))                 \
                sLog->OutMessage(filterType__, level__, __VA_ARGS__);   \
        } while (0)
#else
#define TC_LOG_MESSAGE_BODY(filterType__, level__, ...)                 \
        __pragma(warning(push))                                         \
        __pragma(warning(disable:4127))                                 \
        do {                                                            \
            if (sLog->ShouldLog(filterType__, level__))                 \
                sLog->OutMessage(filterType__, level__, __VA_ARGS__);   \
        } while (0)                                                     \
        __pragma(warning(pop))
#endif

/// 跟踪级别日志宏
#define TC_LOG_TRACE(filterType__, ...) \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_TRACE, __VA_ARGS__)

/// 调试级别日志宏
#define TC_LOG_DEBUG(filterType__, ...) \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_DEBUG, __VA_ARGS__)

/// 信息级别日志宏
#define TC_LOG_INFO(filterType__, ...)  \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_INFO, __VA_ARGS__)

/// 警告级别日志宏
#define TC_LOG_WARN(filterType__, ...)  \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_WARN, __VA_ARGS__)

/// 错误级别日志宏
#define TC_LOG_ERROR(filterType__, ...) \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_ERROR, __VA_ARGS__)

/// 致命错误级别日志宏
#define TC_LOG_FATAL(filterType__, ...) \
    TC_LOG_MESSAGE_BODY(filterType__, LOG_LEVEL_FATAL, __VA_ARGS__)

#endif
