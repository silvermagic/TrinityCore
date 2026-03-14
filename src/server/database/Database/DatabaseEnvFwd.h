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
 * @file DatabaseEnvFwd.h
 * @brief 数据库环境前向声明头文件
 *
 * 本文件提供了 TrinityCore 数据库系统的所有前向声明和类型别名定义。
 * 通过前向声明，可以减少头文件之间的编译依赖，提高编译速度。
 *
 * 主要包含以下组件：
 * - 查询结果相关类型（ResultSet, PreparedResultSet）
 * - 数据库连接类型（Character, Login, World）
 * - 预处理语句类型
 * - 异步查询回调处理
 * - 事务处理类型
 * - MySQL 底层结构前向声明
 *
 * @note 本文件不应包含任何实现代码，仅用于声明和类型定义
 */

#ifndef DatabaseEnvFwd_h__
#define DatabaseEnvFwd_h__

#include <future>
#include <memory>

/**
 * @brief 查询结果字段的元数据结构
 *
 * 存储数据库字段的元信息，如字段名称、类型、长度等属性。
 */
struct QueryResultFieldMetadata;

/**
 * @brief 字段类
 *
 * 表示查询结果集中的单个字段，提供类型安全的数据访问方法。
 * 支持从数据库结果集中提取各种数据类型的值。
 */
class Field;

/**
 * @brief 结果集类
 *
 * 封装数据库查询返回的结果集，提供对查询结果行的遍历和访问。
 * 用于普通 SQL 查询（非预处理语句）的结果处理。
 */
class ResultSet;

/**
 * @brief 查询结果类型别名
 *
 * 使用共享指针管理 ResultSet 对象，确保结果集的生命周期正确管理。
 */
using QueryResult = std::shared_ptr<ResultSet>;

/**
 * @brief 查询结果的 Future 类型
 *
 * 用于异步数据库操作，允许在后台执行查询并稍后获取结果。
 */
using QueryResultFuture = std::future<QueryResult>;

/**
 * @brief 查询结果的 Promise 类型
 *
 * 与 QueryResultFuture 配对使用，用于在异步操作中设置查询结果。
 */
using QueryResultPromise = std::promise<QueryResult>;

/**
 * @brief 角色数据库连接类
 *
 * 管理与角色数据库（characters）的连接，存储玩家角色数据、物品、任务进度等。
 */
class CharacterDatabaseConnection;

/**
 * @brief 登录数据库连接类
 *
 * 管理与登录数据库（auth）的连接，存储账号信息、权限、会话等。
 */
class LoginDatabaseConnection;

/**
 * @brief 世界数据库连接类
 *
 * 管理与世界数据库（world）的连接，存储游戏世界配置、NPC、物品模板、法术等静态数据。
 */
class WorldDatabaseConnection;

/**
 * @brief 预处理语句基类
 *
 * 提供预处理语句的通用接口和基础功能。
 * 预处理语句可以提高查询性能并防止 SQL 注入攻击。
 */
class PreparedStatementBase;

/**
 * @brief 预处理语句模板类
 *
 * 特定数据库连接类型的预处理语句，提供类型安全的参数绑定和执行。
 * 模板参数 T 指定关联的数据库连接类型。
 *
 * @tparam T 数据库连接类型（CharacterDatabaseConnection、LoginDatabaseConnection 或 WorldDatabaseConnection）
 */
template<typename T>
class PreparedStatement;

/**
 * @brief 角色数据库预处理语句类型
 *
 * 用于角色数据库的预处理语句，处理玩家角色相关的查询操作。
 */
using CharacterDatabasePreparedStatement = PreparedStatement<CharacterDatabaseConnection>;

/**
 * @brief 登录数据库预处理语句类型
 *
 * 用于登录数据库的预处理语句，处理账号认证相关的查询操作。
 */
using LoginDatabasePreparedStatement = PreparedStatement<LoginDatabaseConnection>;

/**
 * @brief 世界数据库预处理语句类型
 *
 * 用于世界数据库的预处理语句，处理游戏世界配置和静态数据的查询操作。
 */
using WorldDatabasePreparedStatement = PreparedStatement<WorldDatabaseConnection>;

/**
 * @brief 预处理语句结果集类
 *
 * 封装预处理语句执行后返回的结果集，提供类型安全的字段访问。
 * 相比普通 ResultSet，预处理语句结果集具有更好的性能和安全性。
 */
class PreparedResultSet;

/**
 * @brief 预处理查询结果类型别名
 *
 * 使用共享指针管理 PreparedResultSet 对象。
 */
using PreparedQueryResult = std::shared_ptr<PreparedResultSet>;

/**
 * @brief 预处理查询结果的 Future 类型
 *
 * 用于异步预处理语句操作，允许在后台执行查询并稍后获取结果。
 */
using PreparedQueryResultFuture = std::future<PreparedQueryResult>;

/**
 * @brief 预处理查询结果的 Promise 类型
 *
 * 与 PreparedQueryResultFuture 配对使用，用于在异步操作中设置查询结果。
 */
using PreparedQueryResultPromise = std::promise<PreparedQueryResult>;

/**
 * @brief 查询回调类
 *
 * 封装异步查询的回调处理逻辑，支持查询完成后的后续操作。
 * 用于非阻塞式数据库查询的场景。
 */
class QueryCallback;

/**
 * @brief 异步回调处理器模板类
 *
 * 管理和处理异步回调队列，提供回调的注册、执行和结果处理。
 *
 * @tparam T 回调类型
 */
template<typename T>
class AsyncCallbackProcessor;

/**
 * @brief 查询回调处理器类型别名
 *
 * 专门用于处理 QueryCallback 的异步回调处理器。
 */
using QueryCallbackProcessor = AsyncCallbackProcessor<QueryCallback>;

/**
 * @brief 事务基类
 *
 * 提供数据库事务的基础功能，用于将多个 SQL 操作组合为一个原子操作单元。
 * 确保事务内的所有操作要么全部成功，要么全部回滚。
 */
class TransactionBase;

/**
 * @brief 事务结果的 Future 类型
 *
 * 用于异步事务操作，返回 bool 值表示事务是否成功提交。
 */
using TransactionFuture = std::future<bool>;

/**
 * @brief 事务结果的 Promise 类型
 *
 * 与 TransactionFuture 配对使用，用于在异步事务操作中设置执行结果。
 */
using TransactionPromise = std::promise<bool>;

/**
 * @brief 事务模板类
 *
 * 特定数据库连接类型的事务实现，提供事务的开始、提交、回滚等操作。
 * 模板参数 T 指定关联的数据库连接类型。
 *
 * @tparam T 数据库连接类型
 */
template<typename T>
class Transaction;

/**
 * @brief 事务回调类
 *
 * 用于处理异步事务完成后的回调逻辑，支持事务完成通知和后续处理。
 */
class TransactionCallback;

/**
 * @brief SQL 事务类型别名
 *
 * 使用共享指针管理 Transaction 对象，确保事务对象的生命周期正确管理。
 *
 * @tparam T 数据库连接类型
 */
template<typename T>
using SQLTransaction = std::shared_ptr<Transaction<T>>;

/**
 * @brief 角色数据库事务类型
 *
 * 用于角色数据库的事务操作，确保玩家数据修改的原子性和一致性。
 */
using CharacterDatabaseTransaction = SQLTransaction<CharacterDatabaseConnection>;

/**
 * @brief 登录数据库事务类型
 *
 * 用于登录数据库的事务操作，确保账号数据修改的原子性和一致性。
 */
using LoginDatabaseTransaction = SQLTransaction<LoginDatabaseConnection>;

/**
 * @brief 世界数据库事务类型
 *
 * 用于世界数据库的事务操作，确保世界数据修改的原子性和一致性。
 */
using WorldDatabaseTransaction = SQLTransaction<WorldDatabaseConnection>;

/**
 * @brief SQL 查询持有者基类
 *
 * 用于持有和管理多个 SQL 查询的集合，支持批量查询和延迟执行。
 * 可以将多个查询打包在一起执行，提高查询效率。
 */
class SQLQueryHolderBase;

/**
 * @brief 查询结果持有者的 Future 类型
 *
 * 用于异步批量查询操作，查询完成后通过 future 通知调用方。
 */
using QueryResultHolderFuture = std::future<void>;

/**
 * @brief 查询结果持有者的 Promise 类型
 *
 * 与 QueryResultHolderFuture 配对使用，用于在异步批量查询中设置完成状态。
 */
using QueryResultHolderPromise = std::promise<void>;

/**
 * @brief SQL 查询持有者模板类
 *
 * 特定数据库连接类型的查询持有者，用于管理和执行多个相关查询。
 * 模板参数 T 指定关联的数据库连接类型。
 *
 * @tparam T 数据库连接类型
 */
template<typename T>
class SQLQueryHolder;

/**
 * @brief 角色数据库查询持有者类型
 *
 * 用于角色数据库的批量查询操作管理。
 */
using CharacterDatabaseQueryHolder = SQLQueryHolder<CharacterDatabaseConnection>;

/**
 * @brief 登录数据库查询持有者类型
 *
 * 用于登录数据库的批量查询操作管理。
 */
using LoginDatabaseQueryHolder = SQLQueryHolder<LoginDatabaseConnection>;

/**
 * @brief 世界数据库查询持有者类型
 *
 * 用于世界数据库的批量查询操作管理。
 */
using WorldDatabaseQueryHolder = SQLQueryHolder<WorldDatabaseConnection>;

/**
 * @brief SQL 查询持有者回调类
 *
 * 用于处理异步批量查询完成后的回调逻辑，支持查询完成通知和结果处理。
 */
class SQLQueryHolderCallback;

/**
 * @defgroup MySQLTypes MySQL 底层类型
 * @brief MySQL 客户端库的底层结构前向声明
 *
 * 这些结构体是 MySQL C API 的核心类型，TrinityCore 通过封装这些底层结构
 * 提供类型安全的数据库操作接口。
 * @{
 */

/**
 * @brief MySQL 连接句柄结构
 *
 * 表示与 MySQL 服务器的连接，是所有数据库操作的基础。
 * 对应 MySQL 库中的 MYSQL 结构。
 */
struct MySQLHandle;

/**
 * @brief MySQL 结果集结构
 *
 * 存储查询返回的结果数据，提供结果集的访问接口。
 * 对应 MySQL 库中的 MYSQL_RES 结构。
 */
struct MySQLResult;

/**
 * @brief MySQL 字段结构
 *
 * 表示结果集中单个字段的元数据信息。
 * 对应 MySQL 库中的 MYSQL_FIELD 结构。
 */
struct MySQLField;

/**
 * @brief MySQL 参数绑定结构
 *
 * 用于预处理语句的参数绑定，实现参数化查询。
 * 对应 MySQL 库中的 MYSQL_BIND 结构。
 */
struct MySQLBind;

/**
 * @brief MySQL 预处理语句结构
 *
 * 表示预处理语句对象，支持参数化查询和高效执行。
 * 对应 MySQL 库中的 MYSQL_STMT 结构。
 */
struct MySQLStmt;

/** @} */ // end of MySQLTypes group

#endif // DatabaseEnvFwd_h__
