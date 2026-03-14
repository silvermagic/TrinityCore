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
 * @file DatabaseLoader.h
 * @brief 数据库加载器模块
 *
 * 本模块提供数据库初始化和加载的统一管理功能，负责协调多个数据库连接池的启动。
 *
 * 核心功能：
 * - 统一管理 Login/Character/World 三个数据库的初始化
 * - 支持数据库自动创建和更新
 * - 提供延迟初始化机制，确保正确的加载顺序
 * - 失败时自动清理已打开的连接
 *
 * 加载顺序：
 * 1. OpenDatabases()   - 打开数据库连接
 * 2. PopulateDatabases() - 填充基础数据（如必需的表结构和初始数据）
 * 3. UpdateDatabases()   - 执行数据库更新脚本
 * 4. PrepareStatements() - 预编译SQL语句
 *
 * 设计模式：
 * - 使用命令队列模式，延迟执行数据库操作
 * - 支持链式调用，简化配置代码
 */

#ifndef DatabaseLoader_h__
#define DatabaseLoader_h__

#include "Define.h"

#include <functional>
#include <queue>
#include <stack>
#include <string>

template <class T>
class DatabaseWorkerPool;

/**
 * @class DatabaseLoader
 * @brief 数据库加载器 - 统一管理多个数据库连接池的初始化
 *
 * 该类负责协调 TrinityCore 中三个主要数据库的启动流程：
 * - LoginDatabase：账号认证相关数据
 * - CharacterDatabase：角色数据
 * - WorldDatabase：游戏世界数据
 *
 * 特性：
 * - 延迟执行：AddDatabase() 仅注册操作，实际执行在 Load() 时
 * - 错误处理：任一步骤失败时，自动关闭已打开的连接
 * - 自动配置：可自动创建不存在的数据库
 *
 * 使用示例：
 * @code
 * DatabaseLoader loader("server.worldserver", DATABASE_MASK_ALL);
 * loader.AddDatabase(LoginDatabase, "Login")
 *       .AddDatabase(CharacterDatabase, "Character")
 *       .AddDatabase(WorldDatabase, "World");
 * if (!loader.Load()) {
 *     // 处理加载失败
 * }
 * @endcode
 */
class TC_DATABASE_API DatabaseLoader
{
public:
    /**
     * @brief 构造函数
     * @param logger            日志记录器名称，用于输出加载过程中的日志
     * @param defaultUpdateMask 默认的数据库更新标志掩码，控制哪些数据库启用自动更新
     *
     * @note 从配置文件读取以下配置项：
     *       - Updates.AutoSetup: 是否自动创建不存在的数据库
     *       - Updates.EnableDatabases: 启用更新的数据库掩码
     */
    DatabaseLoader(std::string const& logger, uint32 const defaultUpdateMask);

    /**
     * @brief 注册数据库连接池到加载器（延迟执行）
     * @tparam T    数据库连接类型（LoginDatabaseConnection/CharacterDatabaseConnection/WorldDatabaseConnection）
     * @param pool  数据库连接池引用
     * @param name  数据库配置名称前缀（如 "Login"、"Character"、"World"）
     * @return DatabaseLoader 引用，支持链式调用
     *
     * @brief 此方法不会立即执行，而是将操作加入队列，在 Load() 时统一执行
     *
     * @brief 配置项读取：
     * - {name}DatabaseInfo: 数据库连接字符串
     * - {name}Database.WorkerThreads: 异步工作线程数（1-32）
     * - {name}Database.SynchThreads: 同步连接数
     *
     * @brief 加载阶段：
     * 1. 打开连接 -> 加入 _open 队列
     * 2. 填充数据 -> 加入 _populate 队列（仅启用了更新的数据库）
     * 3. 更新数据库 -> 加入 _update 队列（仅启用了更新的数据库）
     * 4. 预编译语句 -> 加入 _prepare 队列
     */
    template <class T>
    DatabaseLoader& AddDatabase(DatabaseWorkerPool<T>& pool, std::string const& name);

    /**
     * @brief 加载所有注册的数据库
     * @return true 表示全部成功，false 表示任一步骤失败
     *
     * @brief 执行顺序：
     * 1. OpenDatabases()     - 建立数据库连接
     * 2. PopulateDatabases() - 导入基础数据
     * 3. UpdateDatabases()   - 执行更新脚本
     * 4. PrepareStatements() - 预编译SQL语句
     *
     * @note 失败时会自动关闭已打开的连接
     * @note 此方法应在服务器启动时调用
     */
    bool Load();

    /**
     * @enum DatabaseTypeFlags
     * @brief 数据库类型标志 - 控制哪些数据库启用自动更新
     */
    enum DatabaseTypeFlags
    {
        DATABASE_NONE       = 0,    ///< 无数据库

        DATABASE_LOGIN      = 1,    ///< 登录数据库（账号认证数据）
        DATABASE_CHARACTER  = 2,    ///< 角色数据库（玩家角色数据）
        DATABASE_WORLD      = 4,    ///< 世界数据库（游戏配置、NPC、物品等）

        DATABASE_MASK_ALL   = DATABASE_LOGIN | DATABASE_CHARACTER | DATABASE_WORLD  ///< 所有数据库
    };

private:
    /**
     * @brief 打开所有注册的数据库连接
     * @return true 表示全部成功，false 表示任一失败
     *
     * @brief 执行流程：
     * - 从配置读取连接信息
     * - 尝试建立数据库连接
     * - 如果数据库不存在且启用自动创建，则尝试创建
     */
    bool OpenDatabases();

    /**
     * @brief 填充数据库基础数据
     * @return true 表示成功，false 表示失败
     *
     * @brief 执行数据库初始化脚本，导入必需的基础数据
     * （如表结构、默认配置数据等）
     */
    bool PopulateDatabases();

    /**
     * @brief 更新数据库结构
     * @return true 表示成功，false 表示失败
     *
     * @brief 执行数据库迁移脚本，更新表结构和数据
     * 以匹配当前版本的核心代码
     */
    bool UpdateDatabases();

    /**
     * @brief 预编译所有SQL语句
     * @return true 表示成功，false 表示失败
     *
     * @brief 预编译所有预处理语句，提高后续查询效率
     * 必须在数据库打开后、实际使用前执行
     */
    bool PrepareStatements();

    /// 操作谓词类型 - 返回 bool 的函数对象
    using Predicate = std::function<bool()>;
    /// 关闭操作类型 - 无返回值的函数对象
    using Closer = std::function<void()>;

    /**
     * @brief 处理操作队列
     * @param queue 待执行的操作队列
     * @return true 表示全部成功，false 表示任一失败
     *
     * @brief 执行流程：
     * 1. 依次执行队列中的操作
     * 2. 任一操作失败时，执行所有已注册的关闭操作
     * 3. 返回执行结果
     *
     * @note 失败时会清理所有已打开的资源
     */
    bool Process(std::queue<Predicate>& queue);

    std::string const _logger;      ///< 日志记录器名称
    bool const _autoSetup;          ///< 是否自动创建不存在的数据库
    uint32 const _updateFlags;      ///< 启用更新的数据库标志掩码

    std::queue<Predicate> _open;    ///< 数据库打开操作队列
    std::queue<Predicate> _populate;///< 数据填充操作队列
    std::queue<Predicate> _update;  ///< 数据库更新操作队列
    std::queue<Predicate> _prepare; ///< 语句预编译操作队列
    std::stack<Closer> _close;      ///< 关闭操作栈（LIFO顺序关闭）
};

#endif // DatabaseLoader_h__
