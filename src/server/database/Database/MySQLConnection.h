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
 * @file MySQLConnection.h
 * @brief MySQL数据库连接管理模块
 *
 * 本模块封装了MySQL数据库连接的核心功能，包括：
 * - 数据库连接的建立、断开和重连
 * - SQL语句的执行和查询
 * - 预处理语句的管理
 * - 事务处理
 * - 异步和同步连接支持
 *
 * 主要类：
 * - MySQLConnectionInfo: 连接参数信息结构
 * - MySQLConnection: MySQL连接封装类
 *
 * 使用场景：
 * - DatabaseWorkerPool管理多个连接实例
 * - 同步连接用于主线程直接数据库操作
 * - 异步连接配合工作线程处理后台数据库操作
 *
 * 线程安全：
 * - 每个连接实例通过互斥锁保护
 * - 异步连接由DatabaseWorker线程池管理
 */

#ifndef _MYSQLCONNECTION_H
#define _MYSQLCONNECTION_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

template <typename T>
class ProducerConsumerQueue;

class DatabaseWorker;
class MySQLPreparedStatement;
class SQLOperation;

/**
 * @enum ConnectionFlags
 * @brief 连接类型标志
 *
 * 定义连接的工作模式，用于区分同步和异步操作
 */
enum ConnectionFlags
{
    CONNECTION_ASYNC = 0x1,     // 异步连接标志 - 由工作线程处理
    CONNECTION_SYNCH = 0x2,     // 同步连接标志 - 由调用线程直接处理
    CONNECTION_BOTH = CONNECTION_ASYNC | CONNECTION_SYNCH  // 同时支持两种模式
};

/**
 * @struct MySQLConnectionInfo
 * @brief MySQL连接参数信息结构
 *
 * 存储建立MySQL连接所需的所有参数信息，
 * 从配置字符串解析得到。
 *
 * 配置字符串格式：
 * host;port_or_socket;user;password;database[;ssl]
 *
 * 示例：
 * - TCP连接: "127.0.0.1;3306;root;password;trinity"
 * - Unix Socket: ".;/var/run/mysqld/mysqld.sock;root;password;trinity"
 * - 命名管道(Windows): ".;\\\\.\\pipe\\MySQL;root;password;trinity"
 */
struct TC_DATABASE_API MySQLConnectionInfo
{
    /**
     * @brief 构造函数 - 从连接字符串解析参数
     * @param infoString 连接信息字符串，格式: host;port_or_socket;user;password;database[;ssl]
     */
    explicit MySQLConnectionInfo(std::string const& infoString);

    std::string user;               // 数据库用户名
    std::string password;           // 数据库密码
    std::string database;           // 数据库名称
    std::string host;               // 主机地址，"." 表示使用socket或命名管道
    std::string port_or_socket;     // 端口号或socket路径
    std::string ssl;                // SSL配置，"ssl"表示启用SSL连接
};

/**
 * @class MySQLConnection
 * @brief MySQL数据库连接封装类
 *
 * 封装MySQL C API，提供数据库连接和操作的高级接口。
 * 支持同步和异步两种工作模式，处理连接管理、SQL执行、
 * 预处理语句和事务等功能。
 *
 * 主要功能：
 * - 数据库连接的建立、断开和自动重连
 * - SQL语句的执行（普通和预处理）
 * - 查询结果的获取
 * - 事务管理
 * - 预处理语句缓存
 *
 * 使用方式：
 * - 同步连接：直接创建并使用，适合需要立即返回结果的场景
 * - 异步连接：配合DatabaseWorker和任务队列，适合后台操作
 *
 * 线程安全：
 * - 通过互斥锁保护连接的并发访问
 * - 异步连接由专用工作线程管理
 */
class TC_DATABASE_API MySQLConnection
{
    template <class T> friend class DatabaseWorkerPool;    // 连接池需要访问私有成员
    friend class PingOperation;                            // Ping操作需要访问连接

    public:
        /**
         * @brief 构造函数 - 创建同步连接
         * @param connInfo 连接参数信息引用
         *
         * 同步连接由调用线程直接使用，不创建工作线程
         */
        MySQLConnection(MySQLConnectionInfo& connInfo);

        /**
         * @brief 构造函数 - 创建异步连接
         * @param queue SQL操作任务队列指针
         * @param connInfo 连接参数信息引用
         *
         * 异步连接会创建工作线程从队列中取任务执行
         */
        MySQLConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& connInfo);

        /**
         * @brief 虚析构函数 - 清理资源
         *
         * 关闭连接、清理预处理语句、停止工作线程
         */
        virtual ~MySQLConnection();

        /**
         * @brief 打开数据库连接
         * @return 成功返回0，失败返回MySQL错误码
         *
         * 建立实际的MySQL连接，设置字符集等参数
         * 调用时机：连接对象创建后，使用前调用
         */
        virtual uint32 Open();

        /**
         * @brief 关闭数据库连接
         *
         * 关闭MySQL连接，清理预处理语句，停止工作线程
         */
        void Close();

        /**
         * @brief 准备预处理语句
         * @return 成功返回true，有错误返回false
         *
         * 调用DoPrepareStatements()让子类注册预处理语句
         */
        bool PrepareStatements();

        /**
         * @brief 执行原始SQL语句（无返回结果）
         * @param sql SQL语句字符串
         * @return 成功返回true，失败返回false
         *
         * 用于执行INSERT、UPDATE、DELETE等不返回结果集的语句
         */
        bool Execute(char const* sql);

        /**
         * @brief 执行预处理语句（无返回结果）
         * @param stmt 预处理语句对象指针
         * @return 成功返回true，失败返回false
         */
        bool Execute(PreparedStatementBase* stmt);

        /**
         * @brief 执行查询并返回结果集
         * @param sql SQL查询语句
         * @return 结果集指针，失败返回nullptr
         *
         * 调用者负责释放返回的ResultSet对象
         */
        ResultSet* Query(char const* sql);

        /**
         * @brief 执行预处理查询并返回结果集
         * @param stmt 预处理语句对象指针
         * @return 预处理结果集指针，失败返回nullptr
         *
         * 调用者负责释放返回的PreparedResultSet对象
         */
        PreparedResultSet* Query(PreparedStatementBase* stmt);

        /**
         * @brief 内部查询实现（原始SQL）
         * @param sql SQL语句
         * @param pResult [out] MySQL结果集指针
         * @param pFields [out] 字段数组指针
         * @param pRowCount [out] 行数
         * @param pFieldCount [out] 字段数
         * @return 成功返回true，失败返回false
         *
         * 底层查询方法，供ResultSet使用
         */
        bool _Query(char const* sql, MySQLResult** pResult, MySQLField** pFields, uint64* pRowCount, uint32* pFieldCount);

        /**
         * @brief 内部查询实现（预处理语句）
         * @param stmt 预处理语句对象
         * @param mysqlStmt [out] MySQL预处理语句指针
         * @param pResult [out] MySQL结果集指针
         * @param pRowCount [out] 行数
         * @param pFieldCount [out] 字段数
         * @return 成功返回true，失败返回false
         */
        bool _Query(PreparedStatementBase* stmt, MySQLPreparedStatement** mysqlStmt, MySQLResult** pResult, uint64* pRowCount, uint32* pFieldCount);

        /**
         * @brief 开始事务
         *
         * 执行 START TRANSACTION 语句
         */
        void BeginTransaction();

        /**
         * @brief 回滚事务
         *
         * 执行 ROLLBACK 语句
         */
        void RollbackTransaction();

        /**
         * @brief 提交事务
         *
         * 执行 COMMIT 语句
         */
        void CommitTransaction();

        /**
         * @brief 执行事务
         * @param transaction 事务对象指针
         * @return 成功返回0，失败返回MySQL错误码
         *
         * 批量执行事务中的所有SQL语句，失败时自动回滚
         */
        int ExecuteTransaction(std::shared_ptr<TransactionBase> transaction);

        /**
         * @brief 转义字符串
         * @param to 输出缓冲区
         * @param from 输入字符串
         * @param length 输入字符串长度
         * @return 转义后的字符串长度
         *
         * 使用mysql_real_escape_string转义特殊字符，防止SQL注入
         */
        size_t EscapeString(char* to, const char* from, size_t length);

        /**
         * @brief 发送Ping保持连接活跃
         *
         * 检查连接是否仍然活跃，断开时会自动重连
         */
        void Ping();

        /**
         * @brief 获取最后的错误码
         * @return MySQL错误码
         */
        uint32 GetLastError();

    protected:
        /**
         * @brief 尝试锁定连接
         * @return 成功获取锁返回true，已被占用返回false
         *
         * 用于连接池的线程安全访问，非阻塞尝试获取锁
         */
        bool LockIfReady();

        /**
         * @brief 解锁连接
         *
         * 释放连接锁，允许其他线程访问
         */
        void Unlock();

        /**
         * @brief 获取MySQL服务器版本
         * @return 服务器版本号（如 50700 表示 5.7.0）
         */
        uint32 GetServerVersion() const;

        /**
         * @brief 获取预处理语句对象
         * @param index 预处理语句索引
         * @return 预处理语句对象指针
         *
         * 从缓存中获取已准备好的预处理语句
         */
        MySQLPreparedStatement* GetPreparedStatement(uint32 index);

        /**
         * @brief 准备预处理语句
         * @param index 语句索引
         * @param sql SQL语句字符串
         * @param flags 连接类型标志
         *
         * 根据连接类型决定是否准备该语句，
         * 异步连接只准备异步语句，同步连接只准备同步语句
         */
        void PrepareStatement(uint32 index, std::string const& sql, ConnectionFlags flags);

        /**
         * @brief 执行预处理语句准备（纯虚函数）
         *
         * 由子类实现，注册该连接类型需要的预处理语句
         */
        virtual void DoPrepareStatements() = 0;

        typedef std::vector<std::unique_ptr<MySQLPreparedStatement>> PreparedStatementContainer;

        PreparedStatementContainer           m_stmts;         // 预处理语句存储容器
        bool                                 m_reconnecting;  // 是否正在重连
        bool                                 m_prepareError;  // 准备语句时是否有错误

    private:
        /**
         * @brief 处理MySQL错误码
         * @param errNo MySQL错误码
         * @param attempts 重试次数（默认5次）
         * @return 已处理返回true，未处理返回false
         *
         * 根据错误类型采取相应措施：
         * - 连接丢失：尝试重连
         * - 死锁：返回false让调用者处理
         * - 致命错误：终止程序
         */
        bool _HandleMySQLErrno(uint32 errNo, uint8 attempts = 5);

        ProducerConsumerQueue<SQLOperation*>* m_queue;      // 异步操作队列（与其他异步连接共享）
        std::unique_ptr<DatabaseWorker> m_worker;           // 工作线程对象
        MySQLHandle*          m_Mysql;                      // MySQL连接句柄
        MySQLConnectionInfo&  m_connectionInfo;             // 连接信息（用于日志）
        ConnectionFlags       m_connectionFlags;            // 连接类型标志
        std::mutex            m_Mutex;                      // 连接访问互斥锁

        // 禁止拷贝
        MySQLConnection(MySQLConnection const& right) = delete;
        MySQLConnection& operator=(MySQLConnection const& right) = delete;
};

#endif
