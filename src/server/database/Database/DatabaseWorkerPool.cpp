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
 * @file DatabaseWorkerPool.cpp
 * @brief 数据库工作池实现文件
 *
 * 本文件实现了 DatabaseWorkerPool 模板类，提供数据库连接池管理功能。
 * 支持同步和异步数据库操作，包括查询、事务处理和连接保活等功能。
 * 该类是 TrinityCore 数据库系统的核心组件，用于管理 MySQL/MariaDB 连接。
 */

#include "DatabaseWorkerPool.h"
#include "AdhocStatement.h"
#include "Common.h"
#include "Errors.h"
#include "Implementation/LoginDatabase.h"
#include "Implementation/WorldDatabase.h"
#include "Implementation/CharacterDatabase.h"
#include "Log.h"
#include "MySQLPreparedStatement.h"
#include "PreparedStatement.h"
#include "ProducerConsumerQueue.h"
#include "QueryCallback.h"
#include "QueryHolder.h"
#include "QueryResult.h"
#include "SQLOperation.h"
#include "Transaction.h"
#include "MySQLWorkaround.h"
#include <mysqld_error.h>
#ifdef TRINITY_DEBUG
#include <sstream>
#include <boost/stacktrace.hpp>
#endif

/** @defgroup MySQLVersionConstants MySQL 版本常量
 *  @{
 */

/** @brief 支持的最小 MySQL 服务器版本号（数值形式）*/
#define MIN_MYSQL_SERVER_VERSION 50700u

/** @brief 支持的最小 MySQL 服务器版本字符串 */
#define MIN_MYSQL_SERVER_VERSION_STRING "5.7"

/** @brief 支持的最小 MySQL 客户端版本号（数值形式）*/
#define MIN_MYSQL_CLIENT_VERSION 50700u

/** @brief 支持的最小 MySQL 客户端版本字符串 */
#define MIN_MYSQL_CLIENT_VERSION_STRING "5.7"

/** @brief 支持的最小 MariaDB 服务器版本号（数值形式）*/
#define MIN_MARIADB_SERVER_VERSION 100209u

/** @brief 支持的最小 MariaDB 服务器版本字符串 */
#define MIN_MARIADB_SERVER_VERSION_STRING "10.2.9"

/** @brief 支持的最小 MariaDB 客户端版本号（数值形式）*/
#define MIN_MARIADB_CLIENT_VERSION 30003u

/** @brief 支持的最小 MariaDB 客户端版本字符串 */
#define MIN_MARIADB_CLIENT_VERSION_STRING "3.0.3"

/** @} */ // end of MySQLVersionConstants

/**
 * @class PingOperation
 * @brief 数据库连接保活操作类
 *
 * 继承自 SQLOperation，用于实现数据库连接的保活功能。
 * 当工作线程空闲时，通过执行此操作发送 Ping 命令来保持连接活跃状态，
 * 防止连接因超时而被数据库服务器断开。
 */
class PingOperation : public SQLOperation
{
    /**
     * @brief 执行 Ping 操作
     *
     * 向数据库连接发送 Ping 命令，用于保持连接活跃。
     * 此操作由空闲的工作线程执行。
     *
     * @return 始终返回 true，表示操作成功执行
     */
    bool Execute() override
    {
        m_conn->Ping();
        return true;
    }
};

/**
 * @brief 数据库工作池构造函数
 *
 * 初始化数据库工作池，创建生产者-消费者队列用于异步操作。
 * 执行 MySQL/MariaDB 客户端库的版本检查和线程安全检查。
 *
 * @tparam T 数据库连接类型（如 LoginDatabaseConnection, WorldDatabaseConnection, CharacterDatabaseConnection）
 *
 * @note 构造时会检查以下条件：
 *       - MySQL 库必须是线程安全的
 *       - 客户端库版本必须满足最低要求
 *       - 客户端库版本必须与编译时版本匹配
 *
 * @warning 如果版本检查失败，程序将终止运行
 */
template <class T>
DatabaseWorkerPool<T>::DatabaseWorkerPool()
    : _queue(new ProducerConsumerQueue<SQLOperation*>()),
      _async_threads(0), _synch_threads(0)
{
    // 检查 MySQL 库是否线程安全
    WPFatal(mysql_thread_safe(), "Used MySQL library isn't thread-safe.");

    // 根据编译配置检查客户端库版本
#if defined(LIBMARIADB) && MARIADB_PACKAGE_VERSION_ID >= 30200
    // MariaDB 版本检查
    WPFatal(mysql_get_client_version() >= MIN_MARIADB_CLIENT_VERSION, "TrinityCore does not support MariaDB versions below " MIN_MARIADB_CLIENT_VERSION_STRING " (found %s id %lu, need id >= %u), please update your MariaDB client library", mysql_get_client_info(), mysql_get_client_version(), MIN_MARIADB_CLIENT_VERSION);
    WPFatal(mysql_get_client_version() == MARIADB_PACKAGE_VERSION_ID, "Used MariaDB library version (%s id %lu) does not match the version id used to compile TrinityCore (id %u). Search on forum for TCE00011.", mysql_get_client_info(), mysql_get_client_version(), MARIADB_PACKAGE_VERSION_ID);
#else
    // MySQL 版本检查
    WPFatal(mysql_get_client_version() >= MIN_MYSQL_CLIENT_VERSION, "TrinityCore does not support MySQL versions below " MIN_MYSQL_CLIENT_VERSION_STRING " (found %s id %lu, need id >= %u), please update your MySQL client library", mysql_get_client_info(), mysql_get_client_version(), MIN_MYSQL_CLIENT_VERSION);
    WPFatal(mysql_get_client_version() == MYSQL_VERSION_ID, "Used MySQL library version (%s id %lu) does not match the version id used to compile TrinityCore (id %u). Search on forum for TCE00011.", mysql_get_client_info(), mysql_get_client_version(), MYSQL_VERSION_ID);
#endif
}

/**
 * @brief 数据库工作池析构函数
 *
 * 取消队列中所有待处理的操作，停止接受新的数据库操作请求。
 * 实际的连接关闭由 Close() 方法完成。
 *
 * @tparam T 数据库连接类型
 */
template <class T>
DatabaseWorkerPool<T>::~DatabaseWorkerPool()
{
    _queue->Cancel();
}

/**
 * @brief 设置数据库连接信息
 *
 * 配置数据库连接池的连接参数和线程数量。
 * 此方法必须在 Open() 之前调用。
 *
 * @tparam T 数据库连接类型
 * @param infoString 数据库连接字符串，格式为：
 *                   hostname;port;username;password;database
 * @param asyncThreads 异步连接线程数量，用于处理异步数据库操作
 * @param synchThreads 同步连接线程数量，用于处理同步数据库操作
 *
 * @note 连接字符串示例："127.0.0.1;3306;trinity;trinity;trinity_world"
 */
template <class T>
void DatabaseWorkerPool<T>::SetConnectionInfo(std::string const& infoString,
    uint8 const asyncThreads, uint8 const synchThreads)
{
    _connectionInfo = std::make_unique<MySQLConnectionInfo>(infoString);

    _async_threads = asyncThreads;
    _synch_threads = synchThreads;
}

/**
 * @brief 打开数据库连接池
 *
 * 初始化所有数据库连接，包括异步连接和同步连接。
 * 检查连接是否成功建立，并验证数据库服务器版本。
 *
 * @tparam T 数据库连接类型
 * @return 返回错误码，0 表示成功，非 0 表示失败
 *
 * @note 此方法会依次打开异步连接和同步连接
 * @warning 必须先调用 SetConnectionInfo() 设置连接信息
 */
template <class T>
uint32 DatabaseWorkerPool<T>::Open()
{
    WPFatal(_connectionInfo.get(), "Connection info was not set!");

    TC_LOG_INFO("sql.driver", "Opening DatabasePool '{}'. "
        "Asynchronous connections: {}, synchronous connections: {}.",
        GetDatabaseName(), _async_threads, _synch_threads);

    // 首先打开异步连接
    uint32 error = OpenConnections(IDX_ASYNC, _async_threads);

    if (error)
        return error;

    // 然后打开同步连接
    error = OpenConnections(IDX_SYNCH, _synch_threads);

    if (!error)
    {
        TC_LOG_INFO("sql.driver", "DatabasePool '{}' opened successfully. "
                    "{} total connections running.", GetDatabaseName(),
                    (_connections[IDX_SYNCH].size() + _connections[IDX_ASYNC].size()));
    }

    return error;
}

/**
 * @brief 关闭数据库连接池
 *
 * 按顺序关闭所有数据库连接。首先关闭异步连接，然后关闭同步连接。
 * 此方法应该在所有其他线程任务都已退出后调用。
 *
 * @tparam T 数据库连接类型
 *
 * @note 关闭顺序很重要：
 *       1. 先关闭异步连接（清空异步连接容器）
 *       2. 再关闭同步连接
 *       由于此时其他线程任务已退出，不需要对连接加锁
 */
template <class T>
void DatabaseWorkerPool<T>::Close()
{
    TC_LOG_INFO("sql.driver", "Closing down DatabasePool '{}'.", GetDatabaseName());

    // 关闭异步连接
    _connections[IDX_ASYNC].clear();

    TC_LOG_INFO("sql.driver", "Asynchronous connections on DatabasePool '{}' terminated. "
                "Proceeding with synchronous connections.",
        GetDatabaseName());

    // 关闭同步连接
    // 由于 Close() 应该在所有其他线程任务退出后调用，此时不存在并发访问，不需要加锁
    _connections[IDX_SYNCH].clear();

    TC_LOG_INFO("sql.driver", "All connections on DatabasePool '{}' closed.", GetDatabaseName());
}

/**
 * @brief 准备预编译语句
 *
 * 在所有数据库连接上准备预编译语句，并记录每个语句的参数数量。
 * 预编译语句可以提高数据库操作的效率和安全性。
 *
 * @tparam T 数据库连接类型
 * @return 返回 true 表示所有语句准备成功，false 表示失败
 *
 * @note 此方法会遍历所有连接（同步和异步），为每个连接准备语句
 *       同时会记录每个预编译语句的参数数量，用于后续的参数绑定验证
 *
 * @warning 如果任一连接准备语句失败，将关闭连接池并返回 false
 */
template <class T>
bool DatabaseWorkerPool<T>::PrepareStatements()
{
    // 遍历所有连接组（同步和异步）
    for (auto& connections : _connections)
    {
        // 遍历组内的每个连接
        for (auto& connection : connections)
        {
            connection->LockIfReady();
            if (!connection->PrepareStatements())
            {
                connection->Unlock();
                Close();
                return false;
            }
            else
                connection->Unlock();

            // 记录预编译语句的参数数量
            size_t const preparedSize = connection->m_stmts.size();
            if (_preparedStatementSize.size() < preparedSize)
                _preparedStatementSize.resize(preparedSize);

            for (size_t i = 0; i < preparedSize; ++i)
            {
                // 如果已经被其他连接设置过，则跳过
                // 每个连接只准备自己类型（同步/异步）的预编译语句
                if (_preparedStatementSize[i] > 0)
                    continue;

                if (MySQLPreparedStatement* stmt = connection->m_stmts[i].get())
                {
                    uint32 const paramCount = stmt->GetParameterCount();

                    // TrinityCore 只支持 uint8 类型的参数索引
                    ASSERT(paramCount < std::numeric_limits<uint8>::max());

                    _preparedStatementSize[i] = static_cast<uint8>(paramCount);
                }
            }
        }
    }

    return true;
}

/**
 * @brief 执行同步 SQL 查询（原生 SQL 语句）
 *
 * 在指定的连接上执行 SQL 查询并返回结果集。
 * 如果未指定连接，则自动获取一个空闲的同步连接。
 *
 * @tparam T 数据库连接类型
 * @param sql 要执行的 SQL 查询语句
 * @param connection 可选参数，指定使用的数据库连接；如果为 nullptr，则自动获取
 * @return 返回查询结果集的智能指针，如果查询失败或无结果则返回空指针
 *
 * @note 此方法会锁定连接直到查询完成
 * @warning 调用者必须确保返回的结果在使用完毕后才被销毁
 */
template <class T>
QueryResult DatabaseWorkerPool<T>::Query(char const* sql, T* connection /*= nullptr*/)
{
    if (!connection)
        connection = GetFreeConnection();

    ResultSet* result = connection->Query(sql);
    connection->Unlock();
    if (!result || !result->GetRowCount() || !result->NextRow())
    {
        delete result;
        return QueryResult(nullptr);
    }

    return QueryResult(result);
}

/**
 * @brief 执行同步预编译语句查询
 *
 * 执行预编译语句查询并返回结果集。
 * 此方法会自动获取空闲连接，执行完毕后释放连接。
 *
 * @tparam T 数据库连接类型
 * @param stmt 预编译语句对象指针
 * @return 返回查询结果集的智能指针，如果查询失败或无结果则返回空指针
 *
 * @note 执行完成后会删除 stmt 对象，调用者不应再使用该对象
 */
template <class T>
PreparedQueryResult DatabaseWorkerPool<T>::Query(PreparedStatement<T>* stmt)
{
    auto connection = GetFreeConnection();
    PreparedResultSet* ret = connection->Query(stmt);
    connection->Unlock();

    // 删除代理类对象，不再需要
    delete stmt;

    if (!ret || !ret->GetRowCount())
    {
        delete ret;
        return PreparedQueryResult(nullptr);
    }

    return PreparedQueryResult(ret);
}

/**
 * @brief 执行异步 SQL 查询（原生 SQL 语句）
 *
 * 将查询任务放入队列，由后台工作线程异步执行。
 * 返回一个回调对象，用于在稍后获取查询结果。
 *
 * @tparam T 数据库连接类型
 * @param sql 要执行的 SQL 查询语句
 * @return 返回查询回调对象，可用于获取异步查询结果
 *
 * @note 此方法是非阻塞的，查询在后台线程中执行
 * @note 返回值是 future 对象，可以在主线程中等待结果
 */
template <class T>
QueryCallback DatabaseWorkerPool<T>::AsyncQuery(char const* sql)
{
    BasicStatementTask* task = new BasicStatementTask(sql, true);
    // 在入队之前存储 future 结果，因为任务可能在方法返回前就被处理和删除
    QueryResultFuture result = task->GetFuture();
    Enqueue(task);
    return QueryCallback(std::move(result));
}

/**
 * @brief 执行异步预编译语句查询
 *
 * 将预编译语句查询任务放入队列，由后台工作线程异步执行。
 * 返回一个回调对象，用于在稍后获取查询结果。
 *
 * @tparam T 数据库连接类型
 * @param stmt 预编译语句对象指针
 * @return 返回查询回调对象，可用于获取异步查询结果
 *
 * @note 此方法是非阻塞的，查询在后台线程中执行
 * @note stmt 对象的所有权转移给任务，调用者不应再使用该对象
 */
template <class T>
QueryCallback DatabaseWorkerPool<T>::AsyncQuery(PreparedStatement<T>* stmt)
{
    PreparedStatementTask* task = new PreparedStatementTask(stmt, true);
    // 在入队之前存储 future 结果，因为任务可能在方法返回前就被处理和删除
    PreparedQueryResultFuture result = task->GetFuture();
    Enqueue(task);
    return QueryCallback(std::move(result));
}

/**
 * @brief 执行延迟查询持有器
 *
 * 将查询持有器任务放入队列，由后台工作线程异步执行多个查询。
 * 用于批量执行多个查询并获取结果。
 *
 * @tparam T 数据库连接类型
 * @param holder 查询持有器的共享指针，包含多个查询
 * @return 返回 SQL 查询持有器回调对象，可用于获取所有查询结果
 *
 * @note 此方法是非阻塞的，所有查询在后台线程中执行
 * @see SQLQueryHolder
 */
template <class T>
SQLQueryHolderCallback DatabaseWorkerPool<T>::DelayQueryHolder(std::shared_ptr<SQLQueryHolder<T>> holder)
{
    SQLQueryHolderTask* task = new SQLQueryHolderTask(holder);
    // 在入队之前存储 future 结果，因为任务可能在方法返回前就被处理和删除
    QueryResultHolderFuture result = task->GetFuture();
    Enqueue(task);
    return { std::move(holder), std::move(result) };
}

/**
 * @brief 开始一个数据库事务
 *
 * 创建一个新的事务对象，用于收集多个数据库操作。
 * 事务中的所有操作将作为一个原子单元执行。
 *
 * @tparam T 数据库连接类型
 * @return 返回新创建的事务对象的共享指针
 *
 * @note 使用示例：
 * @code
 * auto trans = database.BeginTransaction();
 * trans->Append("UPDATE accounts SET gold = gold + 100 WHERE id = 1");
 * trans->Append("UPDATE accounts SET gold = gold - 100 WHERE id = 2");
 * database.CommitTransaction(trans);
 * @endcode
 */
template <class T>
SQLTransaction<T> DatabaseWorkerPool<T>::BeginTransaction()
{
    return std::make_shared<Transaction<T>>();
}

/**
 * @brief 提交事务（异步方式）
 *
 * 将事务放入队列，由后台工作线程异步执行。
 * 在 Debug 模式下会检查事务的有效性。
 *
 * @tparam T 数据库连接类型
 * @param transaction 要提交的事务对象
 *
 * @note 此方法是非阻塞的，事务在后台线程中执行
 * @note 在 Debug 模式下会检查：
 *       - 空事务（0 个查询）：记录警告但不执行
 *       - 单查询事务：建议移除事务上下文
 */
template <class T>
void DatabaseWorkerPool<T>::CommitTransaction(SQLTransaction<T> transaction)
{
#ifdef TRINITY_DEBUG
    // 只在 Debug 模式下分析事务的潜在问题
    // 理想情况下我们在 Debug 模式下发现并修正问题，
    // 这样在 Release 模式下就不需要浪费 CPU 周期
    switch (transaction->GetSize())
    {
    case 0:
        TC_LOG_DEBUG("sql.driver", "Transaction contains 0 queries. Not executing.");
        return;
    case 1:
        TC_LOG_DEBUG("sql.driver", "Warning: Transaction only holds 1 query, consider removing Transaction context in code.");
        break;
    default:
        break;
    }
#endif // TRINITY_DEBUG

    Enqueue(new TransactionTask(transaction));
}

/**
 * @brief 提交事务并获取结果（异步方式）
 *
 * 将事务放入队列异步执行，并返回回调对象用于获取执行结果。
 * 在 Debug 模式下会检查事务的有效性。
 *
 * @tparam T 数据库连接类型
 * @param transaction 要提交的事务对象
 * @return 返回事务回调对象，可用于获取执行结果
 *
 * @note 此方法是非阻塞的，事务在后台线程中执行
 * @note 使用此方法可以判断事务是否成功提交
 */
template <class T>
TransactionCallback DatabaseWorkerPool<T>::AsyncCommitTransaction(SQLTransaction<T> transaction)
{
#ifdef TRINITY_DEBUG
    // 只在 Debug 模式下分析事务的潜在问题
    // 理想情况下我们在 Debug 模式下发现并修正问题，
    // 这样在 Release 模式下就不需要浪费 CPU 周期
    switch (transaction->GetSize())
    {
        case 0:
            TC_LOG_DEBUG("sql.driver", "Transaction contains 0 queries. Not executing.");
            break;
        case 1:
            TC_LOG_DEBUG("sql.driver", "Warning: Transaction only holds 1 query, consider removing Transaction context in code.");
            break;
        default:
            break;
    }
#endif // TRINITY_DEBUG

    TransactionWithResultTask* task = new TransactionWithResultTask(transaction);
    TransactionFuture result = task->GetFuture();
    Enqueue(task);
    return TransactionCallback(std::move(result));
}

/**
 * @brief 直接提交事务（同步方式）
 *
 * 在当前线程中同步执行事务，阻塞直到事务完成。
 * 如果遇到死锁，会自动重试最多 5 次。
 *
 * @tparam T 数据库连接类型
 * @param transaction 要提交的事务对象引用
 *
 * @note 此方法是阻塞的，应在性能要求不高的场景使用
 * @note 对于 MySQL 错误码 1213 (ER_LOCK_DEADLOCK)，会自动重试
 * @warning 重试机制仅处理简单的死锁情况，复杂的死锁可能需要更高层的处理
 */
template <class T>
void DatabaseWorkerPool<T>::DirectCommitTransaction(SQLTransaction<T>& transaction)
{
    T* connection = GetFreeConnection();
    int errorCode = connection->ExecuteTransaction(transaction);
    if (!errorCode)
    {
        connection->Unlock();      // 操作成功
        return;
    }

    // 处理 MySQL 错误码 1213 (死锁)，不将死锁扩展到核心代码本身
    /// @todo 需要更优雅的处理方式
    if (errorCode == ER_LOCK_DEADLOCK)
    {
        // TODO: 处理多个同步线程以类似于异步线程的方式死锁
        uint8 loopBreaker = 5;
        for (uint8 i = 0; i < loopBreaker; ++i)
        {
            if (!connection->ExecuteTransaction(transaction))
                break;
        }
    }

    // 立即清理事务
    transaction->Cleanup();

    connection->Unlock();
}

/**
 * @brief 获取预编译语句对象
 *
 * 根据索引创建预编译语句对象，用于参数绑定和执行。
 *
 * @tparam T 数据库连接类型
 * @param index 预编译语句的索引（枚举类型）
 * @return 返回新创建的预编译语句对象指针
 *
 * @note 调用者负责删除返回的预编译语句对象
 * @note 参数数量从 _preparedStatementSize 数组中获取
 */
template <class T>
PreparedStatement<T>* DatabaseWorkerPool<T>::GetPreparedStatement(PreparedStatementIndex index)
{
    return new PreparedStatement<T>(index, _preparedStatementSize[index]);
}

/**
 * @brief 转义字符串（防止 SQL 注入）
 *
 * 对字符串进行转义处理，使其可以安全地用于 SQL 语句中。
 * 特殊字符（如单引号、反斜杠等）会被正确转义。
 *
 * @tparam T 数据库连接类型
 * @param str 要转义的字符串（输入输出参数）
 *
 * @note 如果字符串为空，则不进行任何处理
 * @note 转义后的字符串长度可能增加（最多为原来的 2 倍 + 1）
 */
template <class T>
void DatabaseWorkerPool<T>::EscapeString(std::string& str)
{
    if (str.empty())
        return;

    char* buf = new char[str.size() * 2 + 1];
    EscapeString(buf, str.c_str(), uint32(str.size()));
    str = buf;
    delete[] buf;
}

/**
 * @brief 保持连接活跃
 *
 * 向所有数据库连接发送 Ping 命令，防止连接因长时间空闲而被服务器断开。
 * 对同步连接直接 Ping，对异步连接则将 Ping 操作放入队列。
 *
 * @tparam T 数据库连接类型
 *
 * @note 对于同步连接，会遍历所有连接并发送 Ping
 * @note 对于异步连接，假设所有工作线程都空闲，每个线程会收到一个 Ping 操作请求
 *       如果某些线程忙碌，Ping 操作分配可能不均匀，但这不影响功能
 *       唯一目的是防止连接空闲超时
 */
template <class T>
void DatabaseWorkerPool<T>::KeepAlive()
{
    // Ping 同步连接
    for (auto& connection : _connections[IDX_SYNCH])
    {
        if (connection->LockIfReady())
        {
            connection->Ping();
            connection->Unlock();
        }
    }

    // 假设所有工作线程都空闲，每个线程会收到 1 个 Ping 操作请求
    // 如果一个或多个工作线程忙碌，Ping 操作不会均匀分配，但这不影响
    // 因为唯一目的是防止连接空闲
    auto const count = _connections[IDX_ASYNC].size();
    for (uint8 i = 0; i < count; ++i)
        Enqueue(new PingOperation);
}

/**
 * @brief 打开指定类型的数据库连接
 *
 * 创建并打开指定数量和类型（同步/异步）的数据库连接。
 * 检查连接是否成功以及服务器版本是否满足要求。
 *
 * @tparam T 数据库连接类型
 * @param type 连接类型索引（IDX_ASYNC 或 IDX_SYNCH）
 * @param numConnections 要创建的连接数量
 * @return 返回错误码，0 表示成功，非 0 表示失败
 *
 * @note 异步连接会关联到工作队列，同步连接则不关联
 * @note 如果任一连接打开失败或版本不满足要求，将清空该类型的所有连接
 */
template <class T>
uint32 DatabaseWorkerPool<T>::OpenConnections(InternalIndex type, uint8 numConnections)
{
    for (uint8 i = 0; i < numConnections; ++i)
    {
        // 创建连接
        auto connection = [&] {
            switch (type)
            {
            case IDX_ASYNC:
                // 异步连接需要关联到工作队列
                return std::make_unique<T>(_queue.get(), *_connectionInfo);
            case IDX_SYNCH:
                // 同步连接不需要工作队列
                return std::make_unique<T>(*_connectionInfo);
            default:
                ABORT();
            }
        }();

        if (uint32 error = connection->Open())
        {
            // 连接打开失败或版本无效，中止并清理
            _connections[type].clear();
            return error;
        }
#ifndef LIBMARIADB
        else if (connection->GetServerVersion() < MIN_MYSQL_SERVER_VERSION)
#else
        else if (connection->GetServerVersion() < MIN_MARIADB_SERVER_VERSION)
#endif
        {
            // 服务器版本不满足要求
#ifndef LIBMARIADB
            TC_LOG_ERROR("sql.driver", "TrinityCore does not support MySQL versions below " MIN_MYSQL_SERVER_VERSION_STRING " (found id {}, need id >= {}), please update your MySQL server", connection->GetServerVersion(), MIN_MYSQL_SERVER_VERSION);
#else
            TC_LOG_ERROR("sql.driver", "TrinityCore does not support MariaDB versions below " MIN_MARIADB_SERVER_VERSION_STRING " (found id {}, need id >= {}), please update your MySQL server", connection->GetServerVersion(), MIN_MARIADB_SERVER_VERSION);
#endif

            return 1;
        }
        else
        {
            // 连接创建成功，添加到连接池
            _connections[type].push_back(std::move(connection));
        }
    }

    // 一切正常
    return 0;
}

/**
 * @brief 转义字符串（底层实现）
 *
 * 将源字符串转义后复制到目标缓冲区，用于防止 SQL 注入攻击。
 * 使用第一个同步连接执行转义操作。
 *
 * @tparam T 数据库连接类型
 * @param to 目标缓冲区，用于存储转义后的字符串
 * @param from 源字符串，需要转义的原始字符串
 * @param length 源字符串的长度
 * @return 返回转义后字符串的长度
 *
 * @note 如果任何参数为空或长度为 0，返回 0
 * @note 目标缓冲区必须足够大，建议至少为 length * 2 + 1
 */
template <class T>
unsigned long DatabaseWorkerPool<T>::EscapeString(char* to, char const* from, unsigned long length)
{
    if (!to || !from || !length)
        return 0;

    return _connections[IDX_SYNCH].front()->EscapeString(to, from, length);
}

/**
 * @brief 将操作加入队列
 *
 * 将 SQL 操作对象放入生产者-消费者队列，等待后台工作线程处理。
 *
 * @tparam T 数据库连接类型
 * @param op SQL 操作对象指针
 *
 * @note 操作对象的所有权转移给队列，由工作线程负责删除
 * @note 此方法是线程安全的
 */
template <class T>
void DatabaseWorkerPool<T>::Enqueue(SQLOperation* op)
{
    _queue->Push(op);
}

/**
 * @brief 获取队列大小
 *
 * 返回当前队列中待处理的 SQL 操作数量。
 *
 * @tparam T 数据库连接类型
 * @return 返回队列中的操作数量
 *
 * @note 此方法是线程安全的
 */
template <class T>
size_t DatabaseWorkerPool<T>::QueueSize() const
{
    return _queue->Size();
}

/**
 * @brief 获取空闲的同步连接
 *
 * 从同步连接池中获取一个可用的连接。
 * 使用轮询方式选择连接，如果所有连接都忙碌则阻塞等待。
 *
 * @tparam T 数据库连接类型
 * @return 返回可用连接的指针
 *
 * @note 在 Debug 模式下，如果启用警告，会记录同步查询的调用栈
 * @note 返回的连接已被锁定，使用完毕后必须调用 Unlock()，否则会导致死锁
 * @warning 必须与 Unlock() 配对使用，否则会导致死锁
 */
template <class T>
T* DatabaseWorkerPool<T>::GetFreeConnection()
{
#ifdef TRINITY_DEBUG
    // 在 Debug 模式下，如果启用同步查询警告，记录调用栈
    if (_warnSyncQueries)
    {
        std::ostringstream ss;
        ss << boost::stacktrace::stacktrace();
        TC_LOG_WARN("sql.performances", "Sync query at:\n{}", ss.str());
    }
#endif

    uint8 i = 0;
    auto const num_cons = _connections[IDX_SYNCH].size();
    T* connection = nullptr;
    // 永远阻塞直到有空闲连接可用
    for (;;)
    {
        connection = _connections[IDX_SYNCH][++i % num_cons].get();
        // 必须与 Unlock() 配对使用，否则会导致死锁
        if (connection->LockIfReady())
            break;
    }

    return connection;
}

/**
 * @brief 获取数据库名称
 *
 * 返回当前连接池所连接的数据库名称。
 *
 * @tparam T 数据库连接类型
 * @return 返回数据库名称的 C 字符串指针
 */
template <class T>
char const* DatabaseWorkerPool<T>::GetDatabaseName() const
{
    return _connectionInfo->database.c_str();
}

/**
 * @brief 异步执行 SQL 语句（原生 SQL）
 *
 * 将 SQL 语句放入队列，由后台工作线程异步执行。
 * 此方法不返回结果，适用于不需要关注执行结果的场景。
 *
 * @tparam T 数据库连接类型
 * @param sql 要执行的 SQL 语句
 *
 * @note 此方法是非阻塞的
 * @note 如果 SQL 语句为空或 null，则不执行任何操作
 */
template <class T>
void DatabaseWorkerPool<T>::Execute(char const* sql)
{
    if (Trinity::IsFormatEmptyOrNull(sql))
        return;

    BasicStatementTask* task = new BasicStatementTask(sql);
    Enqueue(task);
}

/**
 * @brief 异步执行预编译语句
 *
 * 将预编译语句放入队列，由后台工作线程异步执行。
 * 此方法不返回结果，适用于不需要关注执行结果的场景。
 *
 * @tparam T 数据库连接类型
 * @param stmt 预编译语句对象指针
 *
 * @note 此方法是非阻塞的
 * @note stmt 对象的所有权转移给任务，调用者不应再使用该对象
 */
template <class T>
void DatabaseWorkerPool<T>::Execute(PreparedStatement<T>* stmt)
{
    PreparedStatementTask* task = new PreparedStatementTask(stmt);
    Enqueue(task);
}

/**
 * @brief 同步执行 SQL 语句（原生 SQL）
 *
 * 在当前线程中同步执行 SQL 语句，阻塞直到执行完成。
 * 此方法不返回结果，适用于不需要结果但需要确保执行的同步操作。
 *
 * @tparam T 数据库连接类型
 * @param sql 要执行的 SQL 语句
 *
 * @note 此方法是阻塞的
 * @note 如果 SQL 语句为空或 null，则不执行任何操作
 */
template <class T>
void DatabaseWorkerPool<T>::DirectExecute(char const* sql)
{
    if (Trinity::IsFormatEmptyOrNull(sql))
        return;

    T* connection = GetFreeConnection();
    connection->Execute(sql);
    connection->Unlock();
}

/**
 * @brief 同步执行预编译语句
 *
 * 在当前线程中同步执行预编译语句，阻塞直到执行完成。
 * 此方法不返回结果，适用于不需要结果但需要确保执行的同步操作。
 *
 * @tparam T 数据库连接类型
 * @param stmt 预编译语句对象指针
 *
 * @note 此方法是阻塞的
 * @note 执行完成后会删除 stmt 对象，调用者不应再使用该对象
 */
template <class T>
void DatabaseWorkerPool<T>::DirectExecute(PreparedStatement<T>* stmt)
{
    T* connection = GetFreeConnection();
    connection->Execute(stmt);
    connection->Unlock();

    // 删除代理类对象，不再需要
    delete stmt;
}

/**
 * @brief 执行或追加 SQL 语句（原生 SQL）
 *
 * 根据是否存在事务上下文，决定是立即执行还是追加到事务中。
 * 如果事务存在，将 SQL 追加到事务；否则立即异步执行。
 *
 * @tparam T 数据库连接类型
 * @param trans 事务对象的引用
 * @param sql 要执行的 SQL 语句
 *
 * @note 此方法提供了灵活的执行方式，适用于既可以在事务中也可以独立执行的操作
 */
template <class T>
void DatabaseWorkerPool<T>::ExecuteOrAppend(SQLTransaction<T>& trans, char const* sql)
{
    if (!trans)
        Execute(sql);
    else
        trans->Append(sql);
}

/**
 * @brief 执行或追加预编译语句
 *
 * 根据是否存在事务上下文，决定是立即执行还是追加到事务中。
 * 如果事务存在，将预编译语句追加到事务；否则立即异步执行。
 *
 * @tparam T 数据库连接类型
 * @param trans 事务对象的引用
 * @param stmt 预编译语句对象指针
 *
 * @note 此方法提供了灵活的执行方式，适用于既可以在事务中也可以独立执行的操作
 * @note 如果立即执行，stmt 对象的所有权转移给任务；如果追加到事务，由事务管理
 */
template <class T>
void DatabaseWorkerPool<T>::ExecuteOrAppend(SQLTransaction<T>& trans, PreparedStatement<T>* stmt)
{
    if (!trans)
        Execute(stmt);
    else
        trans->Append(stmt);
}

/** @defgroup DatabaseWorkerPoolInstantiation 数据库工作池模板实例化
 *  @{
 */

/**
 * @brief LoginDatabaseConnection 工作池模板实例化
 *
 * 实例化用于登录数据库的 DatabaseWorkerPool 模板类。
 * 用于处理与账户认证相关的数据库操作。
 */
template class TC_DATABASE_API DatabaseWorkerPool<LoginDatabaseConnection>;

/**
 * @brief WorldDatabaseConnection 工作池模板实例化
 *
 * 实例化用于世界数据库的 DatabaseWorkerPool 模板类。
 * 用于处理游戏世界相关的数据库操作（如 NPC、物品、任务等）。
 */
template class TC_DATABASE_API DatabaseWorkerPool<WorldDatabaseConnection>;

/**
 * @brief CharacterDatabaseConnection 工作池模板实例化
 *
 * 实例化用于角色数据库的 DatabaseWorkerPool 模板类。
 * 用于处理玩家角色相关的数据库操作（如角色数据、背包、技能等）。
 */
template class TC_DATABASE_API DatabaseWorkerPool<CharacterDatabaseConnection>;

/** @} */ // end of DatabaseWorkerPoolInstantiation
