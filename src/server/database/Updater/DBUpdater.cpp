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
 * @file DBUpdater.cpp
 * @brief 数据库更新器实现文件
 *
 * 本文件实现了 TrinityCore 的数据库自动更新系统，负责：
 * - 检测和应用数据库架构更新
 * - 创建新数据库
 * - 填充基础数据库数据
 * - 管理 MySQL 可执行文件的路径
 *
 * 支持三种类型的数据库连接：
 * - LoginDatabaseConnection: 认证数据库
 * - WorldDatabaseConnection: 世界数据库
 * - CharacterDatabaseConnection: 角色数据库
 *
 * @see DBUpdater.h
 * @see UpdateFetcher
 */

#include "DBUpdater.h"
#include "BuiltInConfig.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DatabaseLoader.h"
#include "GitRevision.h"
#include "Log.h"
#include "QueryResult.h"
#include "StartProcess.h"
#include "UpdateFetcher.h"
#include <boost/filesystem/operations.hpp>
#include <fstream>
#include <iostream>

/**
 * @brief 获取修正后的 MySQL 可执行文件路径
 *
 * 首先检查是否有已修正的路径缓存，如果有则直接返回。
 * 否则从内置配置中获取 MySQL 可执行文件的默认路径。
 *
 * @return 返回 MySQL 可执行文件的完整路径
 *
 * @note 此函数会缓存找到的正确路径，避免重复搜索
 * @see CheckExecutable()
 * @see BuiltInConfig::GetMySQLExecutable()
 */
std::string DBUpdaterUtil::GetCorrectedMySQLExecutable()
{
    if (!corrected_path().empty())
        return corrected_path();
    else
        return BuiltInConfig::GetMySQLExecutable();
}

/**
 * @brief 检查 MySQL 可执行文件是否存在且可执行
 *
 * 验证 MySQL 客户端程序是否存在且可访问。如果配置文件中指定的路径
 * 不存在，会尝试在系统 PATH 环境变量中搜索 mysql 可执行文件。
 *
 * 执行流程：
 * 1. 检查配置文件中指定的路径是否为有效文件
 * 2. 如果不是，在系统 PATH 中搜索 "mysql" 可执行文件
 * 3. 如果找到，缓存该路径并返回 true
 * 4. 如果未找到，记录致命错误并返回 false
 *
 * @return 如果找到有效的 MySQL 可执行文件则返回 true，否则返回 false
 *
 * @note 找到有效路径后会自动缓存，避免重复搜索
 * @warning 如果找不到可执行文件，会记录 FATAL 级别日志并阻止服务器启动
 */
bool DBUpdaterUtil::CheckExecutable()
{
    boost::filesystem::path exe(GetCorrectedMySQLExecutable());
    if (!is_regular_file(exe))
    {
        exe = Trinity::SearchExecutableInPath("mysql");
        if (!exe.empty() && is_regular_file(exe))
        {
            // Correct the path to the cli
            corrected_path() = absolute(exe).generic_string();
            return true;
        }

        TC_LOG_FATAL("sql.updates", "Didn't find any executable MySQL binary at \'{}\' or in path, correct the path in the *.conf (\"MySQLExecutable\").",
            absolute(exe).generic_string());

        return false;
    }
    return true;
}

/**
 * @brief 获取修正路径的静态引用
 *
 * 使用静态局部变量实现单例模式，用于缓存找到的正确 MySQL 可执行文件路径。
 * 该路径在首次成功检查后会被缓存，后续调用直接返回缓存值。
 *
 * @return 返回修正路径字符串的静态引用
 *
 * @note 此函数使用 Meyers' Singleton 模式，线程安全（C++11 起）
 */
std::string& DBUpdaterUtil::corrected_path()
{
    static std::string path;
    return path;
}

/// @addtogroup database_updates 数据库更新系统
/// @{

// ============================================================================
// Auth Database 认证数据库模板特化
// ============================================================================

/**
 * @brief 获取认证数据库的配置项名称
 *
 * 模板特化函数，返回认证数据库在配置文件中的配置项名称。
 *
 * @return 配置项名称字符串 "Updates.Auth"
 *
 * @note 配置项控制是否启用认证数据库的自动更新
 */
template<>
std::string DBUpdater<LoginDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Auth";
}

/**
 * @brief 获取认证数据库的表名称标识
 *
 * 用于日志输出和用户提示，标识当前操作的是认证数据库。
 *
 * @return 数据库名称字符串 "Auth"
 */
template<>
std::string DBUpdater<LoginDatabaseConnection>::GetTableName()
{
    return "Auth";
}

/**
 * @brief 获取认证数据库的基础 SQL 文件路径
 *
 * 返回用于创建认证数据库的基础 SQL 文件的完整路径。
 * 该文件包含认证数据库的完整架构和初始数据。
 *
 * @return 基础 SQL 文件的完整路径，格式为 "<源码目录>/sql/base/auth_database.sql"
 *
 * @see BuiltInConfig::GetSourceDirectory()
 */
template<>
std::string DBUpdater<LoginDatabaseConnection>::GetBaseFile()
{
    return BuiltInConfig::GetSourceDirectory() +
        "/sql/base/auth_database.sql";
}

/**
 * @brief 检查认证数据库更新是否启用
 *
 * 根据更新掩码判断是否应该对认证数据库执行更新操作。
 *
 * @param updateMask 更新掩码，由 DatabaseLoader 设置的数据库类型标志
 * @return 如果掩码包含 DATABASE_LOGIN 标志则返回 true，否则返回 false
 *
 * @see DatabaseLoader::DATABASE_LOGIN
 */
template<>
bool DBUpdater<LoginDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_LOGIN) ? true : false;
}

// ============================================================================
// World Database 世界数据库模板特化
// ============================================================================

/**
 * @brief 获取世界数据库的配置项名称
 *
 * 模板特化函数，返回世界数据库在配置文件中的配置项名称。
 *
 * @return 配置项名称字符串 "Updates.World"
 *
 * @note 配置项控制是否启用世界数据库的自动更新
 */
template<>
std::string DBUpdater<WorldDatabaseConnection>::GetConfigEntry()
{
    return "Updates.World";
}

/**
 * @brief 获取世界数据库的表名称标识
 *
 * 用于日志输出和用户提示，标识当前操作的是世界数据库。
 *
 * @return 数据库名称字符串 "World"
 */
template<>
std::string DBUpdater<WorldDatabaseConnection>::GetTableName()
{
    return "World";
}

/**
 * @brief 获取世界数据库的基础 SQL 文件路径
 *
 * 返回用于创建世界数据库的基础 SQL 文件的完整路径。
 * 世界数据库使用发布版本的完整数据库文件（TDB_full），而不是存储在代码仓库中。
 *
 * @return 基础 SQL 文件的完整路径（从 GitRevision 获取）
 *
 * @note 世界数据库文件较大，通常需要单独下载
 * @see GitRevision::GetFullDatabase()
 */
template<>
std::string DBUpdater<WorldDatabaseConnection>::GetBaseFile()
{
    return GitRevision::GetFullDatabase();
}

/**
 * @brief 检查世界数据库更新是否启用
 *
 * 根据更新掩码判断是否应该对世界数据库执行更新操作。
 *
 * @param updateMask 更新掩码，由 DatabaseLoader 设置的数据库类型标志
 * @return 如果掩码包含 DATABASE_WORLD 标志则返回 true，否则返回 false
 *
 * @see DatabaseLoader::DATABASE_WORLD
 */
template<>
bool DBUpdater<WorldDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_WORLD) ? true : false;
}

/**
 * @brief 获取世界数据库基础文件的位置类型
 *
 * 世界数据库的基础文件需要从外部下载，而不是存储在代码仓库中。
 *
 * @return 返回 LOCATION_DOWNLOAD，表示需要从外部下载基础文件
 *
 * @note 其他数据库返回 LOCATION_REPOSITORY，表示文件在代码仓库中
 * @see BaseLocation 枚举定义
 */
template<>
BaseLocation DBUpdater<WorldDatabaseConnection>::GetBaseLocationType()
{
    return LOCATION_DOWNLOAD;
}

// ============================================================================
// Character Database 角色数据库模板特化
// ============================================================================

/**
 * @brief 获取角色数据库的配置项名称
 *
 * 模板特化函数，返回角色数据库在配置文件中的配置项名称。
 *
 * @return 配置项名称字符串 "Updates.Character"
 *
 * @note 配置项控制是否启用角色数据库的自动更新
 */
template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Character";
}

/**
 * @brief 获取角色数据库的表名称标识
 *
 * 用于日志输出和用户提示，标识当前操作的是角色数据库。
 *
 * @return 数据库名称字符串 "Character"
 */
template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetTableName()
{
    return "Character";
}

/**
 * @brief 获取角色数据库的基础 SQL 文件路径
 *
 * 返回用于创建角色数据库的基础 SQL 文件的完整路径。
 * 该文件包含角色数据库的完整架构。
 *
 * @return 基础 SQL 文件的完整路径，格式为 "<源码目录>/sql/base/characters_database.sql"
 *
 * @see BuiltInConfig::GetSourceDirectory()
 */
template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetBaseFile()
{
    return BuiltInConfig::GetSourceDirectory() +
        "/sql/base/characters_database.sql";
}

/**
 * @brief 检查角色数据库更新是否启用
 *
 * 根据更新掩码判断是否应该对角色数据库执行更新操作。
 *
 * @param updateMask 更新掩码，由 DatabaseLoader 设置的数据库类型标志
 * @return 如果掩码包含 DATABASE_CHARACTER 标志则返回 true，否则返回 false
 *
 * @see DatabaseLoader::DATABASE_CHARACTER
 */
template<>
bool DBUpdater<CharacterDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_CHARACTER) ? true : false;
}

// ============================================================================
// 通用模板实现 (适用于所有数据库类型)
// ============================================================================

/**
 * @brief 获取基础数据库文件的默认位置类型
 *
 * 默认实现返回 LOCATION_REPOSITORY，表示基础文件存储在代码仓库中。
 * 只有世界数据库会覆盖此方法，返回 LOCATION_DOWNLOAD。
 *
 * @tparam T 数据库连接类型
 * @return 返回 LOCATION_REPOSITORY
 *
 * @see WorldDatabaseConnection 的特化版本
 */
template<class T>
BaseLocation DBUpdater<T>::GetBaseLocationType()
{
    return LOCATION_REPOSITORY;
}

/**
 * @brief 创建新的数据库
 *
 * 当指定的数据库不存在时，提示用户是否创建新数据库。
 * 如果用户同意，创建一个使用 utf8mb4 字符集的新数据库。
 *
 * 执行流程：
 * 1. 提示用户确认是否创建数据库
 * 2. 创建临时 SQL 文件，包含 CREATE DATABASE 语句
 * 3. 通过 MySQL 命令行工具执行 SQL 语句
 * 4. 清理临时文件
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @return 如果成功创建数据库返回 true，否则返回 false
 *
 * @warning 需要数据库用户具有 CREATE、ALTER、DROP、INSERT 和 DELETE 权限
 * @note 数据库使用 utf8mb4 字符集和 utf8mb4_unicode_ci 排序规则
 */
template<class T>
bool DBUpdater<T>::Create(DatabaseWorkerPool<T>& pool)
{
    TC_LOG_INFO("sql.updates", "Database \"{}\" does not exist, do you want to create it? [yes (default) / no]: ",
        pool.GetConnectionInfo()->database);

    std::string answer;
    std::getline(std::cin, answer);
    if (!answer.empty() && !(answer.substr(0, 1) == "y"))
        return false;

    TC_LOG_INFO("sql.updates", "Creating database \"{}\"...", pool.GetConnectionInfo()->database);

    // Path of temp file
    static Path const temp("create_table.sql");

    // Create temporary query to use external MySQL CLi
    std::ofstream file(temp.generic_string());
    if (!file.is_open())
    {
        TC_LOG_FATAL("sql.updates", "Failed to create temporary query file \"{}\"!", temp.generic_string());
        return false;
    }

    file << "CREATE DATABASE `" << pool.GetConnectionInfo()->database << "` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci\n\n";

    file.close();

    try
    {
        DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
            pool.GetConnectionInfo()->port_or_socket, "", pool.GetConnectionInfo()->ssl, temp);
    }
    catch (UpdateException&)
    {
        TC_LOG_FATAL("sql.updates", "Failed to create database {}! Does the user (named in *.conf) have `CREATE`, `ALTER`, `DROP`, `INSERT` and `DELETE` privileges on the MySQL server?", pool.GetConnectionInfo()->database);
        boost::filesystem::remove(temp);
        return false;
    }

    TC_LOG_INFO("sql.updates", "Done.");
    boost::filesystem::remove(temp);
    return true;
}

/**
 * @brief 更新数据库架构
 *
 * 检查并应用数据库更新脚本，保持数据库架构与代码版本同步。
 * 这是最核心的更新函数，负责管理整个数据库更新流程。
 *
 * 执行流程：
 * 1. 检查 MySQL 可执行文件是否可用
 * 2. 验证源码目录是否存在
 * 3. 创建 UpdateFetcher 对象，配置回调函数
 * 4. 执行更新，处理新更新和归档更新
 * 5. 输出更新统计信息
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @return 如果更新成功（或无需更新）返回 true，否则返回 false
 *
 * @note 配置选项说明：
 *   - Updates.Redundancy: 是否允许冗余更新检查
 *   - Updates.AllowRehash: 是否允许重新计算哈希
 *   - Updates.ArchivedRedundancy: 是否允许归档更新的冗余检查
 *   - Updates.CleanDeadRefMaxCount: 清理死引用的最大数量
 *
 * @see UpdateFetcher
 * @see UpdateResult
 */
template<class T>
bool DBUpdater<T>::Update(DatabaseWorkerPool<T>& pool)
{
    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Updating {} database...", DBUpdater<T>::GetTableName());

    Path const sourceDirectory(BuiltInConfig::GetSourceDirectory());

    if (!is_directory(sourceDirectory))
    {
        TC_LOG_ERROR("sql.updates", "DBUpdater: The given source directory {} does not exist, change the path to the directory where your sql directory exists (for example c:\\source\\trinitycore). Shutting down.", sourceDirectory.generic_string());
        return false;
    }

    UpdateFetcher updateFetcher(sourceDirectory, [&](std::string const& query) { DBUpdater<T>::Apply(pool, query); },
        [&](Path const& file) { DBUpdater<T>::ApplyFile(pool, file); },
            [&](std::string const& query) -> QueryResult { return DBUpdater<T>::Retrieve(pool, query); });

    UpdateResult result;
    try
    {
        result = updateFetcher.Update(
            sConfigMgr->GetBoolDefault("Updates.Redundancy", true),
            sConfigMgr->GetBoolDefault("Updates.AllowRehash", true),
            sConfigMgr->GetBoolDefault("Updates.ArchivedRedundancy", false),
            sConfigMgr->GetIntDefault("Updates.CleanDeadRefMaxCount", 3));
    }
    catch (UpdateException&)
    {
        return false;
    }

    std::string const info = Trinity::StringFormat("Containing {} new and {} archived updates.",
        result.recent, result.archived);

    if (!result.updated)
        TC_LOG_INFO("sql.updates", ">> {} database is up-to-date! {}", DBUpdater<T>::GetTableName(), info);
    else
        TC_LOG_INFO("sql.updates", ">> Applied {} {}. {}", result.updated, result.updated == 1 ? "query" : "queries", info);

    return true;
}

/**
 * @brief 填充空数据库的基础数据
 *
 * 检查数据库是否为空，如果为空则应用基础 SQL 文件。
 * 这通常在首次安装或重新初始化数据库时使用。
 *
 * 执行流程：
 * 1. 检查数据库中是否有表（通过 SHOW TABLES）
 * 2. 如果有表，说明数据库已填充，直接返回
 * 3. 获取基础 SQL 文件路径
 * 4. 检查基础文件是否存在
 *   - 对于世界数据库：提示从 GitHub Releases 下载
 *   - 对于其他数据库：提示重新克隆代码仓库
 * 5. 应用基础 SQL 文件
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @return 如果填充成功或数据库非空返回 true，否则返回 false
 *
 * @note 世界数据库的基础文件需要单独下载，不在代码仓库中
 * @see GetBaseFile()
 * @see GetBaseLocationType()
 */
template<class T>
bool DBUpdater<T>::Populate(DatabaseWorkerPool<T>& pool)
{
    {
        QueryResult const result = Retrieve(pool, "SHOW TABLES");
        if (result && (result->GetRowCount() > 0))
            return true;
    }

    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Database {} is empty, auto populating it...", DBUpdater<T>::GetTableName());

    std::string const p = DBUpdater<T>::GetBaseFile();
    if (p.empty())
    {
        TC_LOG_INFO("sql.updates", ">> No base file provided, skipped!");
        return true;
    }

    Path const base(p);
    if (!exists(base))
    {
        switch (DBUpdater<T>::GetBaseLocationType())
        {
            case LOCATION_REPOSITORY:
            {
                TC_LOG_ERROR("sql.updates", ">> Base file \"{}\" is missing. Try fixing it by cloning the source again.",
                    base.generic_string());

                break;
            }
            case LOCATION_DOWNLOAD:
            {
                std::string const filename = base.filename().generic_string();
                std::string const workdir = boost::filesystem::current_path().generic_string();
                TC_LOG_ERROR("sql.updates", ">> File \"{}\" is missing, download it from \"https://github.com/TrinityCore/TrinityCore/releases\"" \
                    " uncompress it and place the file \"{}\" in the directory \"{}\".", filename, filename, workdir);
                break;
            }
        }
        return false;
    }

    // Update database
    TC_LOG_INFO("sql.updates", ">> Applying \'{}\'...", base.generic_string());
    try
    {
        ApplyFile(pool, base);
    }
    catch (UpdateException&)
    {
        return false;
    }

    TC_LOG_INFO("sql.updates", ">> Done!");
    return true;
}

/**
 * @brief 执行查询并返回结果
 *
 * 在数据库上执行指定的 SQL 查询语句，并返回查询结果。
 * 主要用于检查数据库状态或获取元数据。
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @param query 要执行的 SQL 查询语句
 * @return 返回查询结果对象，如果查询失败返回空指针
 *
 * @note 此函数用于 SELECT 类型的查询，返回结果集
 * @see Apply() 用于执行不返回结果的查询
 */
template<class T>
QueryResult DBUpdater<T>::Retrieve(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    return pool.Query(query.c_str());
}

/**
 * @brief 直接执行 SQL 语句
 *
 * 在数据库上直接执行指定的 SQL 语句，不返回结果。
 * 主要用于执行更新、插入、删除等修改数据的操作。
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @param query 要执行的 SQL 语句
 *
 * @note 此函数使用 DirectExecute，会同步执行并等待完成
 * @see Retrieve() 用于需要返回结果的查询
 */
template<class T>
void DBUpdater<T>::Apply(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    pool.DirectExecute(query.c_str());
}

/**
 * @brief 应用 SQL 文件到数据库（简化版本）
 *
 * 使用数据库连接池中的连接信息，将指定的 SQL 文件应用到数据库。
 * 这是重载版本的便捷函数，自动从连接池获取连接参数。
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用
 * @param path SQL 文件路径
 *
 * @see ApplyFile() 完整版本
 */
template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, Path const& path)
{
    DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
        pool.GetConnectionInfo()->port_or_socket, pool.GetConnectionInfo()->database, pool.GetConnectionInfo()->ssl, path);
}

/**
 * @brief 应用 SQL 文件到数据库（完整版本）
 *
 * 通过外部 MySQL 命令行工具执行指定的 SQL 文件。
 * 这是数据库更新的底层实现函数，负责构建和执行 MySQL 命令。
 *
 * 执行流程：
 * 1. 构建 MySQL 命令行参数列表
 * 2. 配置连接参数（主机、用户、密码、端口/套接字）
 * 3. 设置字符集为 utf8mb4
 * 4. 设置最大允许包大小为 1GB
 * 5. 配置 SSL 连接（如果需要）
 * 6. 使用 SOURCE 命令执行 SQL 文件
 * 7. 检查执行结果
 *
 * @tparam T 数据库连接类型
 * @param pool 数据库工作池引用（仅用于日志记录）
 * @param host 数据库主机地址
 * @param user 数据库用户名
 * @param password 数据库密码
 * @param port_or_socket 端口号或套接字路径（Unix 系统）
 * @param database 数据库名称
 * @param ssl SSL 配置（"ssl" 表示启用）
 * @param path SQL 文件路径
 *
 * @throws UpdateException 如果执行失败抛出异常
 *
 * @note 平台差异：
 *   - Windows: 使用命名管道或 TCP/IP 连接
 *   - Unix/Linux: 可以使用 Unix 套接字或 TCP/IP 连接
 * @note MySQL 8.0+ 和 MariaDB 使用不同的 SSL 参数格式
 * @warning 执行失败会记录 FATAL 级别日志并抛出异常
 *
 * @see Trinity::StartProcess()
 */
template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, std::string const& host, std::string const& user,
    std::string const& password, std::string const& port_or_socket, std::string const& database, std::string const& ssl,
    Path const& path)
{
    std::vector<std::string> args;
    args.reserve(9);

    // CLI Client connection info
    args.emplace_back("-h" + host);
    args.emplace_back("-u" + user);

    if (!password.empty())
        args.emplace_back("-p" + password);

    // Check if we want to connect through ip or socket (Unix only)
#ifdef _WIN32

    if (host == ".")
        args.emplace_back("--protocol=PIPE");
    else
        args.emplace_back("-P" + port_or_socket);

#else

    if (!std::isdigit(port_or_socket[0]))
    {
        // We can't check if host == "." here, because it is named localhost if socket option is enabled
        args.emplace_back("-P0");
        args.emplace_back("--protocol=SOCKET");
        args.emplace_back("-S" + port_or_socket);
    }
    else
        // generic case
        args.emplace_back("-P" + port_or_socket);

#endif

    // Set the default charset to utf8
    args.emplace_back("--default-character-set=utf8mb4");

    // Set max allowed packet to 1 GB
    args.emplace_back("--max-allowed-packet=1GB");

#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80000

    if (ssl == "ssl")
        args.emplace_back("--ssl-mode=REQUIRED");

#else

    if (ssl == "ssl")
        args.emplace_back("--ssl");

#endif

    // Execute sql file
    args.emplace_back("-e");
    args.emplace_back(Trinity::StringFormat("BEGIN; SOURCE {}; COMMIT;", path.generic_string()));

    // Database
    if (!database.empty())
        args.emplace_back(database);

    // Invokes a mysql process which doesn't leak credentials to logs
    int const ret = Trinity::StartProcess(DBUpdaterUtil::GetCorrectedMySQLExecutable(), args,
                                 "sql.updates", "", true);

    if (ret != EXIT_SUCCESS)
    {
        TC_LOG_FATAL("sql.updates", "Applying of file \'{}\' to database \'{}\' failed!" \
            " If you are a user, please pull the latest revision from the repository. "
            "Also make sure you have not applied any of the databases with your sql client. "
            "You cannot use auto-update system and import sql files from TrinityCore repository with your sql client. "
            "If you are a developer, please fix your sql query.",
            path.generic_string(), pool.GetConnectionInfo()->database);

        throw UpdateException("update failed");
    }
}

/// @} // end of database_updates group

// ============================================================================
// 模板实例化
// ============================================================================

/**
 * @brief 显式实例化 DBUpdater 模板类
 *
 * 为三种数据库连接类型显式实例化 DBUpdater 模板类。
 * 这使得模板实现可以放在 .cpp 文件中，而不是头文件。
 *
 * 实例化的类型：
 * - DBUpdater<LoginDatabaseConnection>: 认证数据库更新器
 * - DBUpdater<WorldDatabaseConnection>: 世界数据库更新器
 * - DBUpdater<CharacterDatabaseConnection>: 角色数据库更新器
 */
template class TC_DATABASE_API DBUpdater<LoginDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<WorldDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<CharacterDatabaseConnection>;
