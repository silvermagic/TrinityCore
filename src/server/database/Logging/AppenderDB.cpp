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
 * @file AppenderDB.cpp
 * @brief 数据库日志追加器实现文件
 *
 * 本文件实现了 AppenderDB 类,该类是 TrinityCore 日志系统的一部分,
 * 负责将日志消息写入 LoginDatabase 数据库中。
 *
 * 主要功能:
 * - 将日志消息持久化存储到数据库的 logs 表中
 * - 支持按领域(realm)分类存储日志
 * - 自动过滤 SQL 类型的日志以避免无限循环
 *
 * @see AppenderDB.h
 * @see Appender
 * @see LogMessage
 */

#include "AppenderDB.h"
#include "DatabaseEnv.h"
#include "LogMessage.h"
#include "PreparedStatement.h"

/**
 * @brief 构造函数 - 初始化数据库日志追加器
 *
 * 创建一个新的数据库追加器实例。该追加器初始状态下处于禁用状态,
 * 直到通过 setRealmId() 设置领域 ID 后才会启用。
 *
 * @param id 追加器的唯一标识符,用于在日志系统中标识此追加器
 * @param name 追加器的名称,用于日志配置和调试
 * @param level 此追加器处理的最低日志级别,低于此级别的日志将被忽略
 * @param flags 追加器标志位(当前未使用)
 * @param args 额外的配置参数(当前未使用)
 *
 * @note 追加器创建后默认处于禁用状态(enabled = false)
 * @note realmId 初始化为 0,需要后续设置有效的领域 ID
 *
 * @example
 * @code
 * // 创建一个处理 INFO 及以上级别的数据库追加器
 * AppenderDB* appender = new AppenderDB(1, "DBAppender", LogLevel::LOG_LEVEL_INFO);
 * @endcode
 */
AppenderDB::AppenderDB(uint8 id, std::string const& name, LogLevel level, AppenderFlags /*flags*/, std::vector<std::string_view> const& /*args*/)
    : Appender(id, name, level), realmId(0), enabled(false) { }

/**
 * @brief 析构函数 - 清理数据库日志追加器资源
 *
 * 销毁数据库追加器实例,释放相关资源。
 * 由于该类不管理任何动态分配的内存或系统资源,
 * 析构函数不需要执行特殊的清理操作。
 */
AppenderDB::~AppenderDB() { }

/**
 * @brief 将日志消息写入数据库
 *
 * 这是核心日志写入方法,实现了将日志消息持久化到 LoginDatabase 的功能。
 * 日志将被插入到 logs 表中,包含时间戳、领域 ID、日志类型、级别和文本内容。
 *
 * @param message 指向要写入的日志消息对象的指针,包含完整的日志信息
 *
 * @note 该方法会自动过滤包含 "sql" 的日志类型,以防止无限循环:
 *       - 当执行 SQL 语句时,可能会产生 "sql.sql" 类型的日志
 *       - 如果将这些日志再次写入数据库,会触发新的 SQL 执行
 *       - 这会导致无限递归,因此必须过滤此类日志
 * @note 如果追加器未启用(enabled = false),则不会写入任何日志
 *
 * @warning 该方法不应直接调用,而是由 Appender 基类的 write() 方法调用
 *
 * @par 数据库表结构:
 * logs 表应包含以下字段:
 * - time: 日志时间戳(毫秒级 UNIX 时间戳)
 * - realm: 领域 ID
 * - type: 日志类型字符串(如 "server", "entities.player" 等)
 * - level: 日志级别(转换为 uint8 存储)
 * - string: 日志文本内容
 *
 * @par 执行流程:
 * 1. 检查追加器是否已启用
 * 2. 检查日志类型是否包含 "sql",若是则直接返回
 * 3. 创建预编译语句 LOGIN_INS_LOG
 * 4. 绑定日志消息的各项参数
 * 5. 执行数据库插入操作
 */
void AppenderDB::_write(LogMessage const* message)
{
    // 避免无限循环: PExecute 会触发 "sql.sql" 类型的日志记录
    // 如果不过滤 SQL 类型日志,写入日志会触发 SQL 执行,
    // SQL 执行又会产生新的日志,形成无限递归
    if (!enabled || (message->type.find("sql") != std::string::npos))
        return;

    // 创建预编译 SQL 语句,用于插入日志记录
    // LOGIN_INS_LOG 应定义为: INSERT INTO logs (time, realm, type, level, string) VALUES (?, ?, ?, ?, ?)
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_LOG);

    // 绑定参数 0: 日志时间戳(毫秒级 UNIX 时间戳)
    stmt->setUInt64(0, message->mtime);

    // 绑定参数 1: 领域 ID,用于区分不同领域的日志
    stmt->setUInt32(1, realmId);

    // 绑定参数 2: 日志类型,如 "server", "entities.player", "network" 等
    stmt->setString(2, message->type);

    // 绑定参数 3: 日志级别,转换为 uint8 存储
    // 级别定义见 LogLevel 枚举: LOG_LEVEL_TRACE, LOG_LEVEL_DEBUG, LOG_LEVEL_INFO 等
    stmt->setUInt8(3, uint8(message->level));

    // 绑定参数 4: 日志文本内容,即实际的日志消息
    stmt->setString(4, message->text);

    // 执行预编译语句,将日志写入数据库
    // 使用异步执行方式,不等待结果返回
    LoginDatabase.Execute(stmt);
}

/**
 * @brief 设置领域 ID 并启用追加器
 *
 * 配置此追加器关联的领域 ID,并启用日志写入功能。
 * 此方法通常在服务器启动时调用,设置当前领域的 ID 后,
 * 该追加器才会开始将日志写入数据库。
 *
 * @param _realmId 要设置的领域 ID,用于标识日志所属的领域
 *
 * @note 调用此方法后,追加器将自动启用(enabled = true)
 * @note 领域 ID 会被写入每条日志记录中,便于后续按领域查询和过滤日志
 *
 * @par 使用场景:
 * - 在 worldserver 启动时,会调用此方法设置当前领域的 ID
 * - 多领域环境中,可以通过领域 ID 区分不同领域的日志
 * - 便于日志分析和故障排查时定位问题发生的领域
 *
 * @example
 * @code
 * // 设置领域 ID 并启用追加器
 * appenderDB->setRealmId(1);  // 设置为领域 1
 * // 此时追加器已启用,日志将被写入数据库
 * @endcode
 */
void AppenderDB::setRealmId(uint32 _realmId)
{
    enabled = true;      // 启用追加器,允许写入日志
    realmId = _realmId;  // 设置领域 ID
}
