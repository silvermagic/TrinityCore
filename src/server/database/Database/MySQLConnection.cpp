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
 * @file MySQLConnection.cpp
 * @brief MySQL数据库连接管理实现
 *
 * 本文件实现了MySQLConnection类，提供MySQL数据库连接的核心功能：
 * - 连接建立和管理
 * - SQL语句执行
 * - 预处理语句管理
 * - 事务处理
 * - 错误处理和自动重连
 *
 * 关键设计点：
 * - 支持同步和异步两种工作模式
 * - 自动处理连接断开后的重连
 * - 预处理语句缓存提升性能
 * - 完善的错误处理机制
 */

#include "MySQLConnection.h"
#include "Common.h"
#include "DatabaseWorker.h"
#include "Log.h"
#include "MySQLHacks.h"
#include "MySQLPreparedStatement.h"
#include "PreparedStatement.h"
#include "QueryResult.h"
#include "Timer.h"
#include "Transaction.h"
#include "Util.h"
#include <errmsg.h>
#include "MySQLWorkaround.h"
#include <mysqld_error.h>

/**
 * @brief 构造函数 - 从连接字符串解析参数
 * @param infoString 连接信息字符串
 *
 * 格式：host;port_or_socket;user;password;database[;ssl]
 *
 * 解析过程：
 * 1. 使用分号分隔字符串
 * 2. 必须有5或6个字段
 * 3. 按顺序赋值到各成员变量
 */
MySQLConnectionInfo::MySQLConnectionInfo(std::string const& infoString)
{
    // 使用分号分隔连接字符串
    std::vector<std::string_view> tokens = Trinity::Tokenize(infoString, ';', true);

    // 验证字段数量（5个必填 + 1个可选SSL）
    if (tokens.size() != 5 && tokens.size() != 6)
        return;

    // 按顺序解析各字段
    host.assign(tokens[0]);              // 主机地址
    port_or_socket.assign(tokens[1]);    // 端口或socket路径
    user.assign(tokens[2]);              // 用户名
    password.assign(tokens[3]);          // 密码
    database.assign(tokens[4]);          // 数据库名

    // 可选的SSL配置
    if (tokens.size() == 6)
        ssl.assign(tokens[5]);
}

/**
 * @brief 构造函数 - 创建同步连接
 * @param connInfo 连接参数信息引用
 *
 * 初始化同步连接对象，不创建工作线程
 */
MySQLConnection::MySQLConnection(MySQLConnectionInfo& connInfo) :
m_reconnecting(false),
m_prepareError(false),
m_queue(nullptr),
m_Mysql(nullptr),
m_connectionInfo(connInfo),
m_connectionFlags(CONNECTION_SYNCH) { }

/**
 * @brief 构造函数 - 创建异步连接
 * @param queue SQL操作任务队列指针
 * @param connInfo 连接参数信息引用
 *
 * 初始化异步连接对象并创建工作线程
 */
MySQLConnection::MySQLConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& connInfo) :
m_reconnecting(false),
m_prepareError(false),
m_queue(queue),
m_Mysql(nullptr),
m_connectionInfo(connInfo),
m_connectionFlags(CONNECTION_ASYNC)
{
    // 创建工作线程，从队列中取任务执行
    m_worker = std::make_unique<DatabaseWorker>(m_queue, this);
}

/**
 * @brief 析构函数 - 清理资源
 *
 * 关闭连接、释放所有资源
 */
MySQLConnection::~MySQLConnection()
{
    Close();
}

/**
 * @brief 关闭数据库连接
 *
 * 按顺序清理资源：
 * 1. 停止工作线程
 * 2. 清理预处理语句
 * 3. 关闭MySQL连接
 */
void MySQLConnection::Close()
{
    // 先停止工作线程，确保没有正在执行的操作
    m_worker.reset();

    // 清理预处理语句缓存
    m_stmts.clear();

    // 关闭MySQL连接
    if (m_Mysql)
    {
        mysql_close(m_Mysql);
        m_Mysql = nullptr;
    }
}

/**
 * @brief 打开数据库连接
 * @return 成功返回0，失败返回MySQL错误码
 *
 * 执行流程：
 * 1. 初始化MySQL连接句柄
 * 2. 设置字符集为utf8mb4
 * 3. 根据平台配置连接方式（TCP/Socket/命名管道）
 * 4. 配置SSL（如果需要）
 * 5. 建立实际连接
 * 6. 设置自动提交模式
 */
uint32 MySQLConnection::Open()
{
    MYSQL *mysqlInit;

    // 初始化MySQL连接句柄
    mysqlInit = mysql_init(nullptr);
    if (!mysqlInit)
    {
        TC_LOG_ERROR("sql.sql", "Could not initialize Mysql connection to database `{}`", m_connectionInfo.database);
        return CR_UNKNOWN_ERROR;
    }

    int port;
    char const* unix_socket;

    // 设置字符集为utf8mb4，支持完整的Unicode字符
    mysql_options(mysqlInit, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // 平台特定的连接配置
    #ifdef _WIN32
    // Windows平台：支持命名管道连接
    if (m_connectionInfo.host == ".")                                           // 使用命名管道（Windows）
    {
        unsigned int opt = MYSQL_PROTOCOL_PIPE;
        mysql_options(mysqlInit, MYSQL_OPT_PROTOCOL, (char const*)&opt);
        port = 0;
        unix_socket = 0;
    }
    else                                                    // 通用TCP连接
    {
        port = atoi(m_connectionInfo.port_or_socket.c_str());
        unix_socket = 0;
    }
    #else
    // Unix/Linux平台：支持Unix Socket连接
    if (m_connectionInfo.host == ".")                                           // 使用Unix Socket（Unix/Linux）
    {
        unsigned int opt = MYSQL_PROTOCOL_SOCKET;
        mysql_options(mysqlInit, MYSQL_OPT_PROTOCOL, (char const*)&opt);
        m_connectionInfo.host = "localhost";
        port = 0;
        unix_socket = m_connectionInfo.port_or_socket.c_str();
    }
    else                                                    // 通用TCP连接
    {
        port = atoi(m_connectionInfo.port_or_socket.c_str());
        unix_socket = nullptr;
    }
    #endif

    // SSL配置
    if (m_connectionInfo.ssl != "")
    {
#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80000
        // MySQL 8.0+ 使用新的SSL选项
        mysql_ssl_mode opt_use_ssl = SSL_MODE_DISABLED;
        if (m_connectionInfo.ssl == "ssl")
        {
            opt_use_ssl = SSL_MODE_REQUIRED;
        }
        mysql_options(mysqlInit, MYSQL_OPT_SSL_MODE, (char const*)&opt_use_ssl);
#else
        // MySQL 8.0以下或MariaDB使用旧选项
        MySQLBool opt_use_ssl = MySQLBool(0);
        if (m_connectionInfo.ssl == "ssl")
        {
            opt_use_ssl = MySQLBool(1);
        }
        mysql_options(mysqlInit, MYSQL_OPT_SSL_ENFORCE, (char const*)&opt_use_ssl);
#endif
    }

    // 建立实际的MySQL连接
    m_Mysql = reinterpret_cast<MySQLHandle*>(mysql_real_connect(mysqlInit, m_connectionInfo.host.c_str(), m_connectionInfo.user.c_str(),
        m_connectionInfo.password.c_str(), m_connectionInfo.database.c_str(), port, unix_socket, 0));

    if (m_Mysql)
    {
        // 首次连接时记录版本信息
        if (!m_reconnecting)
        {
            TC_LOG_INFO("sql.sql", "MySQL client library: {}", mysql_get_client_info());
            TC_LOG_INFO("sql.sql", "MySQL server ver: {} ", mysql_get_server_info(m_Mysql));
        }

        TC_LOG_INFO("sql.sql", "Connected to MySQL database at {}", m_connectionInfo.host);

        // 设置自动提交模式为开启
        mysql_autocommit(m_Mysql, 1);

        // 再次设置字符集，确保连接使用UTF8
        // 核心发送的数据是UTF8，MySQL必须预期接收UTF8
        mysql_set_character_set(m_Mysql, "utf8mb4");
        return 0;
    }
    else
    {
        // 连接失败，记录错误并返回错误码
        TC_LOG_ERROR("sql.sql", "Could not connect to MySQL database at {}: {}", m_connectionInfo.host, mysql_error(mysqlInit));
        uint32 errorCode = mysql_errno(mysqlInit);
        mysql_close(mysqlInit);
        return errorCode;
    }
}

/**
 * @brief 准备预处理语句
 * @return 成功返回true，有错误返回false
 *
 * 调用子类实现的DoPrepareStatements()注册预处理语句
 */
bool MySQLConnection::PrepareStatements()
{
    DoPrepareStatements();
    return !m_prepareError;
}

/**
 * @brief 执行原始SQL语句
 * @param sql SQL语句字符串
 * @return 成功返回true，失败返回false
 *
 * 执行流程：
 * 1. 检查连接有效性
 * 2. 执行SQL语句
 * 3. 处理可能的错误（包括自动重连）
 * 4. 记录执行时间
 */
bool MySQLConnection::Execute(char const* sql)
{
    if (!m_Mysql)
        return false;

    {
        uint32 _s = getMSTime();  // 记录开始时间

        if (mysql_query(m_Mysql, sql))
        {
            uint32 lErrno = mysql_errno(m_Mysql);

            // 记录SQL和错误信息
            TC_LOG_INFO("sql.sql", "SQL: {}", sql);
            TC_LOG_ERROR("sql.sql", "[{}] {}", lErrno, mysql_error(m_Mysql));

            // 尝试处理错误（可能自动重连后重试）
            if (_HandleMySQLErrno(lErrno))
                return Execute(sql);

            return false;
        }
        else
            // 记录执行时间用于性能分析
            TC_LOG_DEBUG("sql.sql", "[{} ms] SQL: {}", getMSTimeDiff(_s, getMSTime()), sql);
    }

    return true;
}

/**
 * @brief 执行预处理语句
 * @param stmt 预处理语句对象指针
 * @return 成功返回true，失败返回false
 *
 * 执行流程：
 * 1. 获取缓存的预处理语句
 * 2. 绑定参数
 * 3. 执行语句
 * 4. 清理参数绑定
 *
 * 性能优势：
 * - 预处理语句已编译，执行效率高
 * - 参数绑定防止SQL注入
 */
bool MySQLConnection::Execute(PreparedStatementBase* stmt)
{
    if (!m_Mysql)
        return false;

    uint32 index = stmt->GetIndex();

    // 从缓存获取预处理语句
    MySQLPreparedStatement* m_mStmt = GetPreparedStatement(index);
    ASSERT(m_mStmt);            // 只有准备失败、服务器错误或查询错误时才为空

    // 绑定参数到预处理语句
    m_mStmt->BindParameters(stmt);

    MYSQL_STMT* msql_STMT = m_mStmt->GetSTMT();
    MYSQL_BIND* msql_BIND = m_mStmt->GetBind();

    uint32 _s = getMSTime();

    // 绑定参数到MySQL
    if (mysql_stmt_bind_param(msql_STMT, msql_BIND))
    {
        uint32 lErrno = mysql_errno(m_Mysql);
        TC_LOG_ERROR("sql.sql", "SQL(p): {}\n [ERROR]: [{}] {}", m_mStmt->getQueryString(), lErrno, mysql_stmt_error(msql_STMT));

        // 错误处理后重试
        if (_HandleMySQLErrno(lErrno))
            return Execute(stmt);

        m_mStmt->ClearParameters();
        return false;
    }

    // 执行预处理语句
    if (mysql_stmt_execute(msql_STMT))
    {
        uint32 lErrno = mysql_errno(m_Mysql);
        TC_LOG_ERROR("sql.sql", "SQL(p): {}\n [ERROR]: [{}] {}", m_mStmt->getQueryString(), lErrno, mysql_stmt_error(msql_STMT));

        // 错误处理后重试
        if (_HandleMySQLErrno(lErrno))
            return Execute(stmt);

        m_mStmt->ClearParameters();
        return false;
    }

    TC_LOG_DEBUG("sql.sql", "[{} ms] SQL(p): {}", getMSTimeDiff(_s, getMSTime()), m_mStmt->getQueryString());

    // 清理参数绑定，为下次执行做准备
    m_mStmt->ClearParameters();
    return true;
}

/**
 * @brief 执行预处理查询（内部方法）
 * @param stmt 预处理语句对象
 * @param mysqlStmt [out] MySQL预处理语句指针
 * @param pResult [out] 结果集指针
 * @param pRowCount [out] 行数
 * @param pFieldCount [out] 字段数
 * @return 成功返回true，失败返回false
 *
 * 执行预处理查询并返回结果集信息，供PreparedResultSet使用
 */
bool MySQLConnection::_Query(PreparedStatementBase* stmt, MySQLPreparedStatement** mysqlStmt, MySQLResult** pResult, uint64* pRowCount, uint32* pFieldCount)
{
    if (!m_Mysql)
        return false;

    uint32 index = stmt->GetIndex();

    // 获取预处理语句
    MySQLPreparedStatement* m_mStmt = GetPreparedStatement(index);
    ASSERT(m_mStmt);            // 只有准备失败、服务器错误或查询错误时才为空

    // 绑定参数
    m_mStmt->BindParameters(stmt);
    *mysqlStmt = m_mStmt;

    MYSQL_STMT* msql_STMT = m_mStmt->GetSTMT();
    MYSQL_BIND* msql_BIND = m_mStmt->GetBind();

    uint32 _s = getMSTime();

    // 绑定参数
    if (mysql_stmt_bind_param(msql_STMT, msql_BIND))
    {
        uint32 lErrno = mysql_errno(m_Mysql);
        TC_LOG_ERROR("sql.sql", "SQL(p): {}\n [ERROR]: [{}] {}", m_mStmt->getQueryString(), lErrno, mysql_stmt_error(msql_STMT));

        if (_HandleMySQLErrno(lErrno))
            return _Query(stmt, mysqlStmt, pResult, pRowCount, pFieldCount);

        m_mStmt->ClearParameters();
        return false;
    }

    // 执行查询
    if (mysql_stmt_execute(msql_STMT))
    {
        uint32 lErrno = mysql_errno(m_Mysql);
        TC_LOG_ERROR("sql.sql", "SQL(p): {}\n [ERROR]: [{}] {}",
            m_mStmt->getQueryString(), lErrno, mysql_stmt_error(msql_STMT));

        if (_HandleMySQLErrno(lErrno))
            return _Query(stmt, mysqlStmt, pResult, pRowCount, pFieldCount);

        m_mStmt->ClearParameters();
        return false;
    }

    TC_LOG_DEBUG("sql.sql", "[{} ms] SQL(p): {}", getMSTimeDiff(_s, getMSTime()), m_mStmt->getQueryString());

    // 清理参数
    m_mStmt->ClearParameters();

    // 获取结果集元数据
    *pResult = reinterpret_cast<MySQLResult*>(mysql_stmt_result_metadata(msql_STMT));
    *pRowCount = mysql_stmt_num_rows(msql_STMT);
    *pFieldCount = mysql_stmt_field_count(msql_STMT);

    return true;
}

/**
 * @brief 执行查询并返回结果集
 * @param sql SQL查询语句
 * @return 结果集指针，失败返回nullptr
 *
 * 调用者负责删除返回的ResultSet对象
 */
ResultSet* MySQLConnection::Query(char const* sql)
{
    if (!sql)
        return nullptr;

    MySQLResult* result = nullptr;
    MySQLField* fields = nullptr;
    uint64 rowCount = 0;
    uint32 fieldCount = 0;

    // 调用内部查询方法
    if (!_Query(sql, &result, &fields, &rowCount, &fieldCount))
        return nullptr;

    // 创建ResultSet对象
    return new ResultSet(result, fields, rowCount, fieldCount);
}

/**
 * @brief 执行原始SQL查询（内部方法）
 * @param sql SQL语句
 * @param pResult [out] MySQL结果集指针
 * @param pFields [out] 字段数组指针
 * @param pRowCount [out] 行数
 * @param pFieldCount [out] 字段数
 * @return 成功返回true，失败返回false
 *
 * 执行原始SQL查询，返回结果集信息供ResultSet使用
 */
bool MySQLConnection::_Query(const char* sql, MySQLResult** pResult, MySQLField** pFields, uint64* pRowCount, uint32* pFieldCount)
{
    if (!m_Mysql)
        return false;

    {
        uint32 _s = getMSTime();

        if (mysql_query(m_Mysql, sql))
        {
            uint32 lErrno = mysql_errno(m_Mysql);
            TC_LOG_INFO("sql.sql", "SQL: {}", sql);
            TC_LOG_ERROR("sql.sql", "[{}] {}", lErrno, mysql_error(m_Mysql));

            // 错误处理后重试
            if (_HandleMySQLErrno(lErrno))
                return _Query(sql, pResult, pFields, pRowCount, pFieldCount);

            return false;
        }
        else
            TC_LOG_DEBUG("sql.sql", "[{} ms] SQL: {}", getMSTimeDiff(_s, getMSTime()), sql);

        // 获取结果集
        *pResult = reinterpret_cast<MySQLResult*>(mysql_store_result(m_Mysql));
        *pRowCount = mysql_affected_rows(m_Mysql);
        *pFieldCount = mysql_field_count(m_Mysql);
    }

    // 检查结果集有效性
    if (!*pResult )
        return false;

    // 检查是否有数据行
    if (!*pRowCount)
    {
        mysql_free_result(*pResult);
        return false;
    }

    // 获取字段信息
    *pFields = reinterpret_cast<MySQLField*>(mysql_fetch_fields(*pResult));

    return true;
}

/**
 * @brief 开始事务
 *
 * 执行 START TRANSACTION 语句开始一个新事务
 */
void MySQLConnection::BeginTransaction()
{
    Execute("START TRANSACTION");
}

/**
 * @brief 回滚事务
 *
 * 执行 ROLLBACK 语句撤销当前事务的所有修改
 */
void MySQLConnection::RollbackTransaction()
{
    Execute("ROLLBACK");
}

/**
 * @brief 提交事务
 *
 * 执行 COMMIT 语句提交当前事务的所有修改
 */
void MySQLConnection::CommitTransaction()
{
    Execute("COMMIT");
}

/**
 * @brief 执行事务
 * @param transaction 事务对象指针
 * @return 成功返回0，失败返回MySQL错误码，空事务返回-1
 *
 * 执行流程：
 * 1. 检查事务是否为空
 * 2. 开始事务
 * 3. 逐个执行事务中的SQL语句
 * 4. 任一语句失败则回滚整个事务
 * 5. 全部成功则提交事务
 *
 * 注意事项：
 * - 事务执行过程中可能遇到某些错误需要重启事务
 * - 为防止数据丢失，只在全部完成后清理
 * - 清理操作在调用方进行（DatabaseWorkerPool::DirectCommitTransaction和TransactionTask::Execute）
 */
int MySQLConnection::ExecuteTransaction(std::shared_ptr<TransactionBase> transaction)
{
    std::vector<SQLElementData> const& queries = transaction->m_queries;

    // 空事务直接返回
    if (queries.empty())
        return -1;

    // 开始事务
    BeginTransaction();

    // 遍历执行事务中的所有SQL元素
    for (auto itr = queries.begin(); itr != queries.end(); ++itr)
    {
        SQLElementData const& data = *itr;
        switch (itr->type)
        {
            case SQL_ELEMENT_PREPARED:  // 预处理语句
            {
                PreparedStatementBase* stmt = data.element.stmt;
                ASSERT(stmt);
                if (!Execute(stmt))
                {
                    TC_LOG_WARN("sql.sql", "Transaction aborted. {} queries not executed.", (uint32)queries.size());
                    int errorCode = GetLastError();
                    RollbackTransaction();  // 失败时回滚
                    return errorCode;
                }
            }
            break;
            case SQL_ELEMENT_RAW:  // 原始SQL语句
            {
                char const* sql = data.element.query;
                ASSERT(sql);
                if (!Execute(sql))
                {
                    TC_LOG_WARN("sql.sql", "Transaction aborted. {} queries not executed.", (uint32)queries.size());
                    int errorCode = GetLastError();
                    RollbackTransaction();  // 失败时回滚
                    return errorCode;
                }
            }
            break;
        }
    }

    // 我们可能在某些查询期间遇到错误，根据错误类型
    // 我们可能想要重启事务。为了防止数据丢失，我们只在全部完成后清理。
    // 这在调用函数 DatabaseWorkerPool<T>::DirectCommitTransaction 和 TransactionTask::Execute 中完成，
    // 而不是在遍历每个元素时。

    CommitTransaction();  // 提交事务
    return 0;
}

/**
 * @brief 转义字符串
 * @param to 输出缓冲区（需要足够空间，至少2*length+1）
 * @param from 输入字符串
 * @param length 输入字符串长度
 * @return 转义后的字符串长度
 *
 * 使用mysql_real_escape_string转义特殊字符，防止SQL注入攻击
 */
size_t MySQLConnection::EscapeString(char* to, const char* from, size_t length)
{
    return mysql_real_escape_string(m_Mysql, to, from, length);
}

/**
 * @brief 发送Ping保持连接活跃
 *
 * 检查连接是否仍然活跃，如果连接断开会尝试自动重连
 */
void MySQLConnection::Ping()
{
    mysql_ping(m_Mysql);
}

/**
 * @brief 获取最后的错误码
 * @return MySQL错误码
 */
uint32 MySQLConnection::GetLastError()
{
    return mysql_errno(m_Mysql);
}

/**
 * @brief 尝试锁定连接
 * @return 成功获取锁返回true，已被占用返回false
 *
 * 非阻塞方式尝试获取互斥锁，用于连接池的线程安全访问
 */
bool MySQLConnection::LockIfReady()
{
    return m_Mutex.try_lock();
}

/**
 * @brief 解锁连接
 *
 * 释放连接锁，允许其他线程访问此连接
 */
void MySQLConnection::Unlock()
{
    m_Mutex.unlock();
}

/**
 * @brief 获取MySQL服务器版本
 * @return 服务器版本号（格式：主版本*10000 + 次版本*100 + 修订号）
 *
 * 示例：5.7.20 返回 50720
 */
uint32 MySQLConnection::GetServerVersion() const
{
    return mysql_get_server_version(m_Mysql);
}

/**
 * @brief 获取预处理语句对象
 * @param index 预处理语句索引
 * @return 预处理语句对象指针
 *
 * 从缓存中获取已准备好的预处理语句，失败时记录错误日志
 */
MySQLPreparedStatement* MySQLConnection::GetPreparedStatement(uint32 index)
{
    ASSERT(index < m_stmts.size(), "Tried to access invalid prepared statement index %u (max index " SZFMTD ") on database `%s`, connection type: %s",
        index, m_stmts.size(), m_connectionInfo.database.c_str(), (m_connectionFlags & CONNECTION_ASYNC) ? "asynchronous" : "synchronous");

    MySQLPreparedStatement* ret = m_stmts[index].get();
    if (!ret)
        TC_LOG_ERROR("sql.sql", "Could not fetch prepared statement {} on database `{}`, connection type: {}.",
            index, m_connectionInfo.database, (m_connectionFlags & CONNECTION_ASYNC) ? "asynchronous" : "synchronous");

    return ret;
}

/**
 * @brief 准备预处理语句
 * @param index 语句索引
 * @param sql SQL语句字符串
 * @param flags 连接类型标志
 *
 * 根据连接类型决定是否准备该语句：
 * - 异步连接只准备异步语句
 * - 同步连接只准备同步语句
 * - 这样可以节省内存，避免准备不会使用的语句
 */
void MySQLConnection::PrepareStatement(uint32 index, std::string const& sql, ConnectionFlags flags)
{
    // 检查该查询是否应该在此连接上准备
    // 即不在同步连接上准备异步语句，节省不会使用的内存
    if (!(m_connectionFlags & flags))
    {
        m_stmts[index].reset();
        return;
    }

    // 初始化预处理语句句柄
    MYSQL_STMT* stmt = mysql_stmt_init(m_Mysql);
    if (!stmt)
    {
        TC_LOG_ERROR("sql.sql", "In mysql_stmt_init() id: {}, sql: \"{}\"", index, sql);
        TC_LOG_ERROR("sql.sql", "{}", mysql_error(m_Mysql));
        m_prepareError = true;
    }
    else
    {
        // 准备预处理语句
        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())))
        {
            TC_LOG_ERROR("sql.sql", "In mysql_stmt_prepare() id: {}, sql: \"{}\"", index, sql);
            TC_LOG_ERROR("sql.sql", "{}", mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            m_prepareError = true;
        }
        else
            // 创建MySQLPreparedStatement对象并缓存
            m_stmts[index] = std::make_unique<MySQLPreparedStatement>(reinterpret_cast<MySQLStmt*>(stmt), sql);
    }
}

/**
 * @brief 执行预处理查询并返回结果集
 * @param stmt 预处理语句对象指针
 * @return 预处理结果集指针，失败返回nullptr
 *
 * 调用者负责删除返回的PreparedResultSet对象
 */
PreparedResultSet* MySQLConnection::Query(PreparedStatementBase* stmt)
{
    MySQLPreparedStatement* mysqlStmt = nullptr;
    MySQLResult* result = nullptr;
    uint64 rowCount = 0;
    uint32 fieldCount = 0;

    // 调用内部查询方法
    if (!_Query(stmt, &mysqlStmt, &result, &rowCount, &fieldCount))
        return nullptr;

    // 处理可能的多结果集
    if (mysql_more_results(m_Mysql))
    {
        mysql_next_result(m_Mysql);
    }

    // 创建PreparedResultSet对象
    return new PreparedResultSet(mysqlStmt->GetSTMT(), result, rowCount, fieldCount);
}

/**
 * @brief 处理MySQL错误码
 * @param errNo MySQL错误码
 * @param attempts 重试次数（默认5次）
 * @return 已处理返回true，未处理返回false
 *
 * 错误处理策略：
 * 1. 连接丢失错误：尝试自动重连
 * 2. 死锁错误：返回false让调用者处理
 * 3. 查询错误：跳过查询
 * 4. 数据库结构错误：终止程序
 * 5. 未处理错误：记录日志
 */
bool MySQLConnection::_HandleMySQLErrno(uint32 errNo, uint8 attempts /*= 5*/)
{
    switch (errNo)
    {
        // 连接丢失相关错误
        case CR_SERVER_GONE_ERROR:      // 服务器已断开连接
        case CR_SERVER_LOST:            // 查询期间丢失连接
        case CR_SERVER_LOST_EXTENDED:   // 扩展的服务器丢失错误
        {
            if (m_Mysql)
            {
                TC_LOG_ERROR("sql.sql", "Lost the connection to the MySQL server!");

                // 关闭现有连接
                mysql_close(m_Mysql);
                m_Mysql = nullptr;
            }
            [[fallthrough]];  // 继续执行重连逻辑
        }
        case CR_CONN_HOST_ERROR:  // 连接主机错误
        {
            TC_LOG_INFO("sql.sql", "Attempting to reconnect to the MySQL server...");

            m_reconnecting = true;

            // 尝试重新打开连接
            uint32 const lErrno = Open();
            if (!lErrno)
            {
                // 不要移除'this'指针，除非你想跳过加载所有预处理语句...
                if (!this->PrepareStatements())
                {
                    TC_LOG_FATAL("sql.sql", "Could not re-prepare statements!");
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    ABORT();
                }

                TC_LOG_INFO("sql.sql", "Successfully reconnected to {} @{}:{} ({}).",
                    m_connectionInfo.database, m_connectionInfo.host, m_connectionInfo.port_or_socket,
                        (m_connectionFlags & CONNECTION_ASYNC) ? "asynchronous" : "synchronous");

                m_reconnecting = false;
                return true;
            }

            // 检查是否还有重试次数
            if ((--attempts) == 0)
            {
                // 当MySQL服务器长时间不可达时关闭服务器
                // 以防止数据损坏
                TC_LOG_FATAL("sql.sql", "Failed to reconnect to the MySQL server, "
                             "terminating the server to prevent data corruption!");

                // 也可以使用 std::raise(SIGTERM) 来启动关闭
                std::this_thread::sleep_for(std::chrono::seconds(10));
                ABORT();
            }
            else
            {
                // 这个尝试重连可能会抛出2006错误
                // 为了防止疯狂的递归调用，在这里休眠
                std::this_thread::sleep_for(std::chrono::seconds(3)); // 休眠3秒
                return _HandleMySQLErrno(lErrno, attempts); // 递归调用自身
            }
        }

        // 死锁错误 - 在TransactionTask::Execute和DatabaseWorkerPool<T>::DirectCommitTransaction中实现
        case ER_LOCK_DEADLOCK:
            return false;

        // 查询相关错误 - 跳过查询
        case ER_WRONG_VALUE_COUNT:  // 列数不匹配
        case ER_DUP_ENTRY:          // 重复条目
            return false;

        // 表或数据库结构过时 - 终止核心
        case ER_BAD_FIELD_ERROR:    // 字段不存在
        case ER_NO_SUCH_TABLE:      // 表不存在
            TC_LOG_ERROR("sql.sql", "Your database structure is not up to date. Please make sure you've executed all queries in the sql/updates folders.");
            std::this_thread::sleep_for(std::chrono::seconds(10));
            ABORT();
            return false;

        // SQL解析错误
        case ER_PARSE_ERROR:
            TC_LOG_ERROR("sql.sql", "Error while parsing SQL. Core fix required.");
            std::this_thread::sleep_for(std::chrono::seconds(10));
            ABORT();
            return false;

        // 未处理的错误
        default:
            TC_LOG_ERROR("sql.sql", "Unhandled MySQL errno {}. Unexpected behaviour possible.", errNo);
            return false;
    }
}
