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
 * @file DatabaseWorkerPool.h
 * @brief 数据库工作连接池模块
 *
 * 本模块提供数据库连接池的核心实现，管理多个数据库连接和工作线程，
 * 提供同步和异步两种数据库访问方式。
 *
 * 核心功能：
 * - 管理异步和同步两类数据库连接
 * - 提供统一的数据库操作接口
 * - 支持预处理语句的高效执行
 * - 支持事务处理
 * - 自动管理连接的创建、复用和释放
 *
 * 连接池架构：
 * - 异步连接（IDX_ASYNC）：由工作线程使用，处理异步操作
 * - 同步连接（IDX_SYNCH）：由调用线程直接使用，处理同步操作
 *
 * 三种主要数据库类型：
 * - LoginDatabaseConnection：登录认证数据库
 * - CharacterDatabaseConnection：角色数据数据库
 * - WorldDatabaseConnection：世界数据数据库
 *
 * 性能优化：
 * - 预处理语句减少SQL解析开销
 * - 连接复用减少连接创建开销
 * - 异步操作避免阻塞主线程
 */

#ifndef _DATABASEWORKERPOOL_H
#define _DATABASEWORKERPOOL_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "StringFormat.h"
#include <array>
#include <string>
#include <vector>

template <typename T>
class ProducerConsumerQueue;

class SQLOperation;
struct MySQLConnectionInfo;

/**
 * @class DatabaseWorkerPool
 * @brief 数据库工作连接池 - 管理数据库连接和SQL操作的统一入口
 *
 * DatabaseWorkerPool 是 TrinityCore 数据库系统的核心类，负责：
 * - 管理异步和同步两类数据库连接池
 * - 提供统一的数据库操作API
 * - 协调工作线程执行异步操作
 * - 管理预处理语句的生命周期
 *
 * @tparam T 数据库连接类型（LoginDatabaseConnection/CharacterDatabaseConnection/WorldDatabaseConnection）
 *
 * 设计模式：
 * - 对象池模式：复用数据库连接，减少创建开销
 * - 生产者-消费者模式：异步操作通过队列传递给工作线程
 * - RAII：自动管理资源生命周期
 *
 * 使用示例：
 * @code
 * // 异步执行（不等待结果）
 * LoginDatabase.Execute("UPDATE account SET online = 1 WHERE id = {}", accountId);
 *
 * // 同步查询（等待结果）
 * QueryResult result = WorldDatabase.Query("SELECT * FROM creature WHERE id = {}", creatureId);
 *
 * // 预处理语句（效率更高）
 * PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER);
 * stmt->setUInt32(0, guid);
 * PreparedQueryResult result = CharacterDatabase.Query(stmt);
 * @endcode
 */
template <class T>
class DatabaseWorkerPool
{
    private:
        /**
         * @enum InternalIndex
         * @brief 内部连接索引 - 区分异步和同步连接
         */
        enum InternalIndex
        {
            IDX_ASYNC,  ///< 异步连接索引，供工作线程使用
            IDX_SYNCH,  ///< 同步连接索引，供调用线程直接使用
            IDX_SIZE    ///< 连接类型数量（用于数组大小）
        };

    public:
        /**
         * @brief 构造函数 - 初始化数据库连接池
         *
         * 初始化成员变量，实际的连接创建在 Open() 中进行
         */
        DatabaseWorkerPool();

        /**
         * @brief 析构函数 - 关闭连接池并释放资源
         *
         * 关闭所有数据库连接，释放工作线程和相关资源
         */
        ~DatabaseWorkerPool();

        /**
         * @brief 设置连接信息
         * @param infoString    数据库连接字符串（格式：host;port;user;password;database）
         * @param asyncThreads  异步工作线程数量（1-32）
         * @param synchThreads  同步连接数量
         *
         * @brief 此方法在 Open() 之前调用，配置连接池参数
         */
        void SetConnectionInfo(std::string const& infoString, uint8 const asyncThreads, uint8 const synchThreads);

        /**
         * @brief 打开数据库连接池
         * @return 0 表示成功，其他值为 MySQL 错误码
         *
         * @brief 创建所有异步和同步连接：
         * 1. 创建异步连接和工作线程
         * 2. 创建同步连接
         * 3. 连接到 MySQL 服务器
         */
        uint32 Open();

        /**
         * @brief 关闭数据库连接池
         *
         * 关闭所有连接，停止工作线程，释放资源
         */
        void Close();

        /**
         * @brief 预编译所有预处理语句
         * @return true 表示成功，false 表示失败
         *
         * @brief 将所有预处理语句发送到 MySQL 服务器进行编译
         * 预编译后的语句执行效率更高，且可防止 SQL 注入
         */
        bool PrepareStatements();

        /**
         * @brief 获取连接信息
         * @return MySQL 连接信息结构体指针
         */
        inline MySQLConnectionInfo const* GetConnectionInfo() const
        {
            return _connectionInfo.get();
        }

        //============================================================================
        // 异步单向语句方法（无返回值）
        //============================================================================

        /**
         * @brief 异步执行原始SQL语句（无返回值）
         * @param sql SQL语句字符串
         *
         * @brief 将SQL语句封装为 BasicStatementTask 加入异步队列
         * 工作线程会在后台执行此语句，调用者不等待结果
         *
         * @warning 此方法仅适用于一次性执行的查询（如服务器启动时）
         *          频繁执行的查询应使用预处理语句
         */
        void Execute(char const* sql);

        /**
         * @brief 异步执行格式化SQL语句（无返回值）
         * @tparam Args 可变参数类型
         * @param sql  格式化SQL字符串
         * @param args 格式化参数
         *
         * @brief 使用 StringFormat 格式化SQL后异步执行
         * 适用于动态构建的SQL语句
         *
         * @warning 同样仅适用于一次性执行的查询
         */
        template<typename... Args>
        void PExecute(Trinity::FormatString<Args...> sql, Args&&... args)
        {
            if (Trinity::IsFormatEmptyOrNull(sql))
                return;

            this->Execute(Trinity::StringFormat(sql, std::forward<Args>(args)...).c_str());
        }

        /**
         * @brief 异步执行预处理语句（无返回值）
         * @param stmt 预处理语句指针
         *
         * @brief 将预处理语句封装为 PreparedStatementTask 加入异步队列
         * 预处理语句必须使用 CONNECTION_ASYNC 标志创建
         *
         * @note 预处理语句效率高于原始SQL，适合频繁执行的操作
         * @note 语句执行完毕后会自动删除
         */
        void Execute(PreparedStatement<T>* stmt);

        //============================================================================
        // 同步单向语句方法（无返回值，阻塞调用）
        //============================================================================

        /**
         * @brief 同步执行原始SQL语句（无返回值，阻塞）
         * @param sql SQL语句字符串
         *
         * @brief 直接在调用线程执行SQL语句，阻塞直到完成
         * 仅适用于启动时的一次性操作
         *
         * @warning 会阻塞调用线程，不要在主循环中使用
         */
        void DirectExecute(char const* sql);

        /**
         * @brief 同步执行格式化SQL语句（无返回值，阻塞）
         * @tparam Args 可变参数类型
         * @param sql  格式化SQL字符串
         * @param args 格式化参数
         */
        template<typename... Args>
        void DirectPExecute(Trinity::FormatString<Args...> sql, Args&&... args)
        {
            if (Trinity::IsFormatEmptyOrNull(sql))
                return;

            this->DirectExecute(Trinity::StringFormat(sql, std::forward<Args>(args)...).c_str());
        }

        /**
         * @brief 同步执行预处理语句（无返回值，阻塞）
         * @param stmt 预处理语句指针
         *
         * @brief 预处理语句必须使用 CONNECTION_SYNCH 标志创建
         * 执行完毕后会自动删除语句对象
         */
        void DirectExecute(PreparedStatement<T>* stmt);

        //============================================================================
        // 同步查询方法（有返回值，阻塞调用）
        //============================================================================

        /**
         * @brief 同步执行原始SQL查询（有返回值，阻塞）
         * @param sql        SQL查询字符串
         * @param connection 指定的连接（可选，nullptr表示自动选择）
         * @return QueryResult 查询结果智能指针，失败时为空
         *
         * @brief 直接执行查询并返回结果，阻塞调用线程
         * 结果使用智能指针管理，无需手动释放
         */
        QueryResult Query(char const* sql, T* connection = nullptr);

        /**
         * @brief 同步执行格式化SQL查询（有返回值，阻塞）
         * @tparam Args 可变参数类型
         * @param sql  格式化SQL字符串
         * @param conn 指定的连接
         * @param args 格式化参数
         * @return QueryResult 查询结果智能指针
         */
        template<typename... Args>
        QueryResult PQuery(Trinity::FormatString<Args...> sql, T* conn, Args&&... args)
        {
            if (Trinity::IsFormatEmptyOrNull(sql))
                return QueryResult(nullptr);

            return this->Query(Trinity::StringFormat(sql, std::forward<Args>(args)...).c_str(), conn);
        }

        /**
         * @brief 同步执行格式化SQL查询（有返回值，阻塞，自动选择连接）
         * @tparam Args 可变参数类型
         * @param sql  格式化SQL字符串
         * @param args 格式化参数
         * @return QueryResult 查询结果智能指针
         */
        template<typename... Args>
        QueryResult PQuery(Trinity::FormatString<Args...> sql, Args&&... args)
        {
            if (Trinity::IsFormatEmptyOrNull(sql))
                return QueryResult(nullptr);

            return this->Query(Trinity::StringFormat(sql, std::forward<Args>(args)...).c_str());
        }

        /**
         * @brief 同步执行预处理语句查询（有返回值，阻塞）
         * @param stmt 预处理语句指针
         * @return PreparedQueryResult 预处理查询结果智能指针
         *
         * @brief 预处理语句必须使用 CONNECTION_SYNCH 标志创建
         * 结果使用智能指针管理，语句执行后自动删除
         */
        PreparedQueryResult Query(PreparedStatement<T>* stmt);

        //============================================================================
        // 异步查询方法（有返回值，通过回调获取结果）
        //============================================================================

        /**
         * @brief 异步执行原始SQL查询（有返回值）
         * @param sql SQL查询字符串
         * @return QueryCallback 回调对象，用于异步获取结果
         *
         * @brief 查询结果通过回调机制返回，不阻塞调用线程
         * 返回值在 ProcessQueryCallback 方法中处理
         */
        QueryCallback AsyncQuery(char const* sql);

        /**
         * @brief 异步执行预处理语句查询（有返回值）
         * @param stmt 预处理语句指针
         * @return QueryCallback 回调对象
         *
         * @brief 预处理语句必须使用 CONNECTION_ASYNC 标志创建
         */
        QueryCallback AsyncQuery(PreparedStatement<T>* stmt);

        /**
         * @brief 异步执行多个查询（批量查询）
         * @param holder SQL查询持有器，包含多个查询
         * @return SQLQueryHolderCallback 回调对象
         *
         * @brief 将多个查询批量执行，结果通过持有器统一返回
         * 所有预处理语句必须使用 CONNECTION_ASYNC 标志创建
         */
        SQLQueryHolderCallback DelayQueryHolder(std::shared_ptr<SQLQueryHolder<T>> holder);

        //============================================================================
        // 事务处理方法
        //============================================================================

        /**
         * @brief 开始一个事务
         * @return SQLTransaction 事务对象
         *
         * @brief 创建自动管理的事务对象
         * 如果未提交则自动回滚（设置 Autocommit=0）
         */
        SQLTransaction<T> BeginTransaction();

        /**
         * @brief 异步提交事务
         * @param transaction 事务对象
         *
         * @brief 将事务中的所有SQL操作异步执行
         * 操作按添加顺序执行，保证原子性
         */
        void CommitTransaction(SQLTransaction<T> transaction);

        /**
         * @brief 异步提交事务（带回调）
         * @param transaction 事务对象
         * @return TransactionCallback 回调对象
         *
         * @brief 异步执行事务，完成后通过回调通知
         */
        TransactionCallback AsyncCommitTransaction(SQLTransaction<T> transaction);

        /**
         * @brief 同步提交事务（阻塞）
         * @param transaction 事务对象引用
         *
         * @brief 直接执行事务中的所有操作，阻塞调用线程
         * 操作按添加顺序执行
         */
        void DirectCommitTransaction(SQLTransaction<T>& transaction);

        /**
         * @brief 执行或追加原始SQL语句（智能事务处理）
         * @param trans 事务对象引用
         * @param sql   SQL语句
         *
         * @brief 如果事务有效，将SQL追加到事务中
         * 否则直接异步执行
         *
         * @note 用于统一处理事务和非事务场景
         */
        void ExecuteOrAppend(SQLTransaction<T>& trans, char const* sql);

        /**
         * @brief 执行或追加预处理语句（智能事务处理）
         * @param trans 事务对象引用
         * @param stmt  预处理语句
         */
        void ExecuteOrAppend(SQLTransaction<T>& trans, PreparedStatement<T>* stmt);

        //============================================================================
        // 其他方法
        //============================================================================

        /// 预处理语句索引类型（从连接类型中获取）
        typedef typename T::Statements PreparedStatementIndex;

        /**
         * @brief 获取预处理语句对象
         * @param index 预处理语句索引
         * @return PreparedStatement 预处理语句指针
         *
         * @brief 返回自动管理的预处理语句对象
         * 语句在执行后自动删除
         *
         * @note 此对象尚未绑定到 MySQL 连接，直到执行时才绑定
         */
        PreparedStatement<T>* GetPreparedStatement(PreparedStatementIndex index);

        /**
         * @brief 转义字符串（防止SQL注入）
         * @param str 待转义的字符串（输入输出参数）
         *
         * @brief 对字符串进行转义处理，用于当前字符集（UTF-8）
         */
        void EscapeString(std::string& str);

        /**
         * @brief 保活所有数据库连接
         *
         * @brief 定期发送保活查询，防止服务器因超时断开连接
         * 通常由定时器调用
         */
        void KeepAlive();

        /**
         * @brief 设置是否警告同步查询
         * @param warn 是否警告
         *
         * @brief 仅在调试模式下有效
         * 用于检测主线程中的同步查询（可能导致卡顿）
         */
        void WarnAboutSyncQueries([[maybe_unused]] bool warn)
        {
#ifdef TRINITY_DEBUG
            _warnSyncQueries = warn;
#endif
        }

        /**
         * @brief 获取异步队列大小
         * @return 队列中待处理的操作数量
         */
        size_t QueueSize() const;

    private:
        /**
         * @brief 打开指定类型的数据库连接
         * @param type          连接类型（异步或同步）
         * @param numConnections 连接数量
         * @return 0 表示成功，其他为 MySQL 错误码
         */
        uint32 OpenConnections(InternalIndex type, uint8 numConnections);

        /**
         * @brief 转义字符串（底层实现）
         * @param to     目标缓冲区
         * @param from   源字符串
         * @param length 源字符串长度
         * @return 转义后的字符串长度
         */
        unsigned long EscapeString(char* to, char const* from, unsigned long length);

        /**
         * @brief 将操作加入异步队列
         * @param op SQL操作指针
         */
        void Enqueue(SQLOperation* op);

        /**
         * @brief 获取空闲的同步连接
         * @return MySQL 连接指针
         *
         * @brief 从同步连接池中获取一个可用连接
         * 调用者必须在使用后调用 Unlock() 解锁
         *
         * @warning 必须解锁，否则会导致死锁
         */
        T* GetFreeConnection();

        /**
         * @brief 获取数据库名称
         * @return 数据库名称字符串
         */
        char const* GetDatabaseName() const;

        //------------------------------------------------------------------------
        // 成员变量
        //------------------------------------------------------------------------

        /// 异步操作队列，由所有异步工作线程共享
        std::unique_ptr<ProducerConsumerQueue<SQLOperation*>> _queue;

        /// 连接数组：[IDX_ASYNC] 异步连接列表，[IDX_SYNCH] 同步连接列表
        std::array<std::vector<std::unique_ptr<T>>, IDX_SIZE> _connections;

        /// MySQL 连接信息（主机、端口、用户名、密码、数据库名）
        std::unique_ptr<MySQLConnectionInfo> _connectionInfo;

        /// 预处理语句大小信息（用于绑定参数）
        std::vector<uint8> _preparedStatementSize;

        /// 异步工作线程数量
        uint8 _async_threads;
        /// 同步连接数量
        uint8 _synch_threads;

#ifdef TRINITY_DEBUG
        /// 调试模式：是否警告同步查询（线程局部存储）
        static inline thread_local bool _warnSyncQueries = false;
#endif
};

#endif
