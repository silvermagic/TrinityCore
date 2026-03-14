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
 * @file DatabaseLoader.cpp
 * @brief 数据库加载器实现
 *
 * 本文件实现了 DatabaseLoader 类，提供数据库初始化的统一管理。
 * 采用延迟执行模式，在 AddDatabase() 时注册操作，在 Load() 时统一执行。
 *
 * 核心流程：
 * 1. 构造函数读取配置
 * 2. AddDatabase() 注册数据库到队列（延迟执行）
 * 3. Load() 按顺序执行所有操作
 * 4. 失败时自动回滚，关闭已打开的连接
 */

#include "DatabaseLoader.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DBUpdater.h"
#include "Log.h"

#include <mysqld_error.h>

/**
 * @brief 构造函数 - 初始化数据库加载器
 * @param logger            日志记录器名称，用于输出加载过程中的日志信息
 * @param defaultUpdateMask 默认的数据库更新标志掩码
 *
 * @brief 从配置文件读取以下配置：
 * - Updates.AutoSetup: 是否自动创建不存在的数据库（默认 true）
 * - Updates.EnableDatabases: 启用更新的数据库掩码（默认使用 defaultUpdateMask）
 */
DatabaseLoader::DatabaseLoader(std::string const& logger, uint32 const defaultUpdateMask)
    : _logger(logger), _autoSetup(sConfigMgr->GetBoolDefault("Updates.AutoSetup", true)),
    _updateFlags(sConfigMgr->GetIntDefault("Updates.EnableDatabases", defaultUpdateMask))
{
}

/**
 * @brief 添加数据库连接池到加载队列（模板方法）
 * @tparam T    数据库连接类型
 * @param pool  数据库连接池引用
 * @param name  数据库配置名称前缀
 * @return DatabaseLoader 引用，支持链式调用
 *
 * @brief 此方法采用延迟执行模式：
 * - 不立即执行数据库操作
 * - 将操作封装为 Lambda 表达式，加入相应队列
 * - 在 Load() 调用时才真正执行
 *
 * @brief 注册的操作包括：
 * 1. 打开连接操作（_open 队列）
 * 2. 填充数据操作（_populate 队列，仅启用更新的数据库）
 * 3. 更新数据库操作（_update 队列，仅启用更新的数据库）
 * 4. 预编译语句操作（_prepare 队列）
 */
template <class T>
DatabaseLoader& DatabaseLoader::AddDatabase(DatabaseWorkerPool<T>& pool, std::string const& name)
{
    // 检查该数据库是否启用了更新功能
    bool const updatesEnabledForThis = DBUpdater<T>::IsEnabled(_updateFlags);

    // 注册打开数据库操作
    _open.push([this, name, updatesEnabledForThis, &pool]() -> bool
    {
        // 从配置文件读取数据库连接字符串
        std::string const dbString = sConfigMgr->GetStringDefault(name + "DatabaseInfo", "");
        if (dbString.empty())
        {
            TC_LOG_ERROR(_logger, "Database {} not specified in configuration file!", name);
            return false;
        }

        // 读取异步工作线程数，限制在 1-32 之间
        uint8 const asyncThreads = uint8(sConfigMgr->GetIntDefault(name + "Database.WorkerThreads", 1));
        if (asyncThreads < 1 || asyncThreads > 32)
        {
            TC_LOG_ERROR(_logger, "{} database: invalid number of worker threads specified. "
                "Please pick a value between 1 and 32.", name);
            return false;
        }

        // 读取同步连接数
        uint8 const synchThreads = uint8(sConfigMgr->GetIntDefault(name + "Database.SynchThreads", 1));

        // 设置连接信息
        pool.SetConnectionInfo(dbString, asyncThreads, synchThreads);

        // 尝试打开数据库连接
        if (uint32 error = pool.Open())
        {
            // 数据库不存在错误
            if ((error == ER_BAD_DB_ERROR) && updatesEnabledForThis && _autoSetup)
            {
                // 如果启用了自动设置，尝试创建数据库并重新连接
                if (DBUpdater<T>::Create(pool) && (!pool.Open()))
                    error = 0;
            }

            // 如果错误未被处理，则失败
            if (error)
            {
                TC_LOG_ERROR("sql.driver", "\nDatabasePool {} NOT opened. There were errors opening the MySQL connections. Check your SQLDriverLogFile "
                    "for specific errors. Read wiki at http://www.trinitycore.info/display/tc/TrinityCore+Home", name);

                return false;
            }
        }

        // 注册关闭操作，失败时需要关闭已打开的连接
        _close.push([&pool]
        {
            pool.Close();
        });
        return true;
    });

    // 仅对启用更新的数据库注册填充和更新操作
    if (updatesEnabledForThis)
    {
        // 注册填充数据库操作
        _populate.push([this, name, &pool]() -> bool
        {
            if (!DBUpdater<T>::Populate(pool))
            {
                TC_LOG_ERROR(_logger, "Could not populate the {} database, see log for details.", name);
                return false;
            }
            return true;
        });

        // 注册更新数据库操作
        _update.push([this, name, &pool]() -> bool
        {
            if (!DBUpdater<T>::Update(pool))
            {
                TC_LOG_ERROR(_logger, "Could not update the {} database, see log for details.", name);
                return false;
            }
            return true;
        });
    }

    // 注册预编译语句操作（所有数据库都需要）
    _prepare.push([this, name, &pool]() -> bool
    {
        if (!pool.PrepareStatements())
        {
            TC_LOG_ERROR(_logger, "Could not prepare statements of the {} database, see log for details.", name);
            return false;
        }
        return true;
    });

    return *this;
}

/**
 * @brief 加载所有数据库
 * @return true 表示全部成功，false 表示任一步骤失败
 *
 * @brief 执行顺序（严格按此顺序执行）：
 * 1. OpenDatabases()     - 建立数据库连接
 * 2. PopulateDatabases() - 导入基础数据（表结构、初始数据）
 * 3. UpdateDatabases()   - 执行数据库迁移更新
 * 4. PrepareStatements() - 预编译所有SQL语句
 *
 * @note 任一步骤失败都会导致整体失败
 * @note 必须在所有 AddDatabase() 调用之后执行
 */
bool DatabaseLoader::Load()
{
    // 检查是否禁用了所有数据库更新
    if (!_updateFlags)
        TC_LOG_INFO("sql.updates", "Automatic database updates are disabled for all databases!");

    // 按顺序执行各阶段
    if (!OpenDatabases())
        return false;

    if (!PopulateDatabases())
        return false;

    if (!UpdateDatabases())
        return false;

    if (!PrepareStatements())
        return false;

    return true;
}

/**
 * @brief 打开所有数据库连接
 * @return true 表示全部成功，false 表示任一失败
 *
 * @brief 执行 _open 队列中的所有打开操作
 * 失败时会自动执行已注册的关闭操作
 */
bool DatabaseLoader::OpenDatabases()
{
    return Process(_open);
}

/**
 * @brief 填充数据库基础数据
 * @return true 表示成功，false 表示失败
 *
 * @brief 执行 _populate 队列中的所有填充操作
 * 用于导入表结构和必需的初始数据
 */
bool DatabaseLoader::PopulateDatabases()
{
    return Process(_populate);
}

/**
 * @brief 更新数据库结构
 * @return true 表示成功，false 表示失败
 *
 * @brief 执行 _update 队列中的所有更新操作
 * 运行数据库迁移脚本，更新到最新版本
 */
bool DatabaseLoader::UpdateDatabases()
{
    return Process(_update);
}

/**
 * @brief 预编译所有SQL语句
 * @return true 表示成功，false 表示失败
 *
 * @brief 执行 _prepare 队列中的所有预编译操作
 * 将所有预处理语句发送给MySQL服务器编译，提高后续执行效率
 */
bool DatabaseLoader::PrepareStatements()
{
    return Process(_prepare);
}

/**
 * @brief 处理操作队列
 * @param queue 待执行的操作队列
 * @return true 表示全部成功，false 表示任一失败
 *
 * @brief 执行流程：
 * 1. 依次执行队列中的所有操作
 * 2. 任一操作失败时，停止执行后续操作
 * 3. 执行所有已注册的关闭操作（LIFO顺序）
 * 4. 返回执行结果
 *
 * @note 关闭操作使用栈存储，确保按后进先出（LIFO）顺序关闭
 *       这保证了资源释放的正确顺序
 */
bool DatabaseLoader::Process(std::queue<Predicate>& queue)
{
    while (!queue.empty())
    {
        // 执行队首操作
        if (!queue.front()())
        {
            // 操作失败，关闭所有已打开的数据库
            while (!_close.empty())
            {
                _close.top()();
                _close.pop();
            }

            return false;
        }

        // 移除已执行的操作
        queue.pop();
    }
    return true;
}

// ============================================================================
// 模板显式实例化
// 为三种数据库连接类型生成具体代码，使得模板方法可被外部调用
// ============================================================================
template TC_DATABASE_API
DatabaseLoader& DatabaseLoader::AddDatabase<LoginDatabaseConnection>(DatabaseWorkerPool<LoginDatabaseConnection>&, std::string const&);
template TC_DATABASE_API
DatabaseLoader& DatabaseLoader::AddDatabase<CharacterDatabaseConnection>(DatabaseWorkerPool<CharacterDatabaseConnection>&, std::string const&);
template TC_DATABASE_API
DatabaseLoader& DatabaseLoader::AddDatabase<WorldDatabaseConnection>(DatabaseWorkerPool<WorldDatabaseConnection>&, std::string const&);
