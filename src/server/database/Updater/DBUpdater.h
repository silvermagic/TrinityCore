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
 * @file DBUpdater.h
 * @brief 数据库更新器模块
 *
 * 本文件提供了数据库架构更新和版本控制的核心功能。主要包含以下组件：
 *
 * 1. UpdateException - 数据库更新异常类
 *    用于在数据库更新过程中报告错误和异常情况
 *
 * 2. DBUpdaterUtil - 数据库更新工具类
 *    提供 MySQL 可执行文件路径检查和配置功能
 *
 * 3. DBUpdater<T> - 数据库更新器模板类
 *    核心更新器，负责管理数据库的创建、更新和填充操作
 *
 * 数据库更新流程：
 * - 启动时检查数据库版本（通过 updates 表）
 * - 扫描 sql/updates 目录中的增量更新文件
 * - 按照命名规则和版本顺序应用更新
 * - 记录更新历史以支持回滚和审计
 *
 * 支持的数据库类型：
 * - LoginDatabase (认证数据库)
 * - WorldDatabase (世界数据库)
 * - CharacterDatabase (角色数据库)
 *
 * @see DatabaseWorkerPool
 * @see UpdateException
 * @see DBUpdaterUtil
 */

#ifndef DBUpdater_h__
#define DBUpdater_h__

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <string>

template <class T>
class DatabaseWorkerPool;

namespace boost
{
    namespace filesystem
    {
        class path;
    }
}

/**
 * @brief 数据库更新异常类
 *
 * 当数据库更新过程中发生错误时抛出的异常类。
 * 继承自 std::exception，提供标准的异常处理机制。
 *
 * 异常抛出场景：
 * - 数据库连接失败（主机不可达、认证失败、权限不足）
 * - SQL 文件不存在或无法读取
 * - SQL 语法错误或约束冲突
 * - MySQL 客户端工具未安装或不可用
 * - 基础数据库文件下载失败
 * - 数据库版本不兼容
 * - 更新文件哈希校验失败
 *
 * 异常消息格式：
 * - 通常包含错误描述和上下文信息
 * - 可能包含失败的 SQL 文件路径
 * - 可能包含 MySQL 错误码和描述
 * - 示例："Failed to apply update '2024_01_15_00_world.sql': Table 'creature' doesn't exist"
 *
 * 异常处理策略：
 * @code
 * try
 * {
 *     DBUpdater<WorldDatabaseConnection>::Update(worldPool);
 * }
 * catch (UpdateException const& e)
 * {
 *     // 记录详细错误日志
 *     LOG_FATAL("sql.updates", "{}", e.what());
 *
 *     // 通知管理员
 *     SendAlertToAdmins(e.what());
 *
 *     // 根据情况决定是否继续启动服务器
 *     if (IsCriticalError(e.what()))
 *     {
 *         return false; // 服务器启动失败
 *     }
 * }
 * @endcode
 *
 * @warning 捕获此异常后应谨慎决定是否继续执行
 * @warning 数据库更新失败可能导致数据不一致
 * @warning 建议在异常处理中提供恢复建议
 *
 * @note 异常消息使用英文，便于国际化和问题追踪
 * @note 继承自 std::exception，可使用标准异常处理机制
 *
 * @see DBUpdater 数据库更新器主类
 */
class TC_DATABASE_API UpdateException : public std::exception
{
public:
    /**
     * @brief 构造函数
     * @param msg 异常消息字符串，描述错误详情
     *
     * 构造异常对象并存储错误消息。
     * 消息应该包含足够的信息帮助诊断问题。
     *
     * 消息内容建议：
     * - 包含操作失败的上下文（哪个数据库、哪个文件）
     * - 包含底层错误信息（MySQL 错误、系统错误）
     * - 提供可能的解决方案提示
     *
     * 示例消息：
     * @code
     * "Failed to connect to database 'world' at 'localhost:3306': Access denied for user 'trinity'@'localhost'"
     * "SQL file '/path/to/updates/2024_01_15_00_world.sql' not found"
     * "Error executing SQL: Duplicate entry '12345' for key 'PRIMARY' in table 'creature_template'"
     * @endcode
     *
     * @param msg 异常消息内容，将被存储在对象内部
     *
     * @note 消息通过 const 引用传递，会进行拷贝
     * @note 消息内容在异常对象生命周期内保持有效
     */
    UpdateException(std::string const& msg) : _msg(msg) { }

    /**
     * @brief 析构函数
     *
     * 声明为不抛出异常（throw()），符合标准异常类规范。
     * 析构时自动清理内部消息字符串。
     *
     * @note 使用默认实现即可，std::string 会自动管理内存
     * @note throw() 说明符确保析构过程不会抛出异常
     */
    ~UpdateException() throw() { }

    /**
     * @brief 获取异常消息
     * @return 返回异常消息的 C 风格字符串指针（const char*）
     *
     * 重写 std::exception::what() 方法，返回存储的异常消息。
     * 这是异常处理的标准接口，用于获取错误详情。
     *
     * 返回值保证：
     * - 返回的指针在异常对象生命周期内有效
     * - 指向以 null 结尾的 C 字符串
     * - 字符串内容不可修改（const）
     *
     * 使用示例：
     * @code
     * catch (UpdateException const& e)
     * {
     *     std::cout << "Database update failed: " << e.what() << std::endl;
     *     LOG_ERROR("sql", "Update error: {}", e.what());
     * }
     * @endcode
     *
     * @note 返回的指针在异常对象销毁后变为无效
     * @note 如果需要长期保存消息，应进行拷贝
     * @note 线程安全：可在多线程环境中调用
     *
     * @performance 返回内部字符串指针，无拷贝开销
     */
    char const* what() const throw() override { return _msg.c_str(); }

private:
    std::string const _msg;  ///< 异常消息内容，不可变
};

/**
 * @brief 基础数据库文件位置枚举
 *
 * 指定基础数据库文件的获取位置，用于数据库初始化和更新操作。
 * 不同位置适用于不同的部署环境和网络条件。
 *
 * 使用场景：
 * - LOCATION_REPOSITORY: 适用于离线环境、版本控制严格的环境
 * - LOCATION_DOWNLOAD: 适用于持续集成、自动部署环境
 *
 * 配置方式：
 * 通过 worldserver.conf 中的 Updates.BaseLocation 配置项设置。
 * 建议根据实际环境选择合适的位置：
 * - 生产环境：推荐使用 LOCATION_REPOSITORY（可预测、可审计）
 * - 开发/测试环境：可使用 LOCATION_DOWNLOAD（便捷）
 *
 * 文件大小参考：
 * - WorldDatabase 基础文件：约 100-200 MB
 * - CharacterDatabase 基础文件：约 1-5 MB
 * - LoginDatabase 基础文件：约 1-2 MB
 *
 * @see DBUpdater::GetBaseLocationType() 获取配置的位置类型
 * @see DBUpdater::Populate() 使用基础文件填充数据库
 */
enum BaseLocation
{
    /**
     * @brief 从代码仓库的 sql/base 目录加载基础数据库文件
     *
     * 文件来源：
     * - 位于源代码仓库的 sql/base/ 目录
     * - 通常随源码一起克隆或下载
     * - 文件名格式：TDB_<database>_<version>.sql
     *
     * 优点：
     * - 不依赖网络连接，可离线部署
     * - 文件版本与代码版本匹配，可重现
     * - 适合版本控制和审计
     * - 避免下载失败的风险
     *
     * 缺点：
     * - 需要手动更新基础文件
     * - 占用磁盘空间（源码目录）
     * - 克隆仓库时增加下载时间
     *
     * 适用场景：
     * - 生产环境部署
     * - 离线环境
     * - 需要严格控制版本的环境
     * - 多次部署相同版本的环境
     */
    LOCATION_REPOSITORY,

    /**
     * @brief 从远程服务器下载基础数据库文件
     *
     * 文件来源：
     * - TrinityCore 官方服务器或镜像站
     * - URL 在源码中硬编码或通过配置指定
     * - 使用 HTTP/HTTPS 协议下载
     *
     * 优点：
     * - 无需在源码中存储大文件
     * - 自动获取最新版本
     * - 减少仓库克隆时间
     * - 适合 CI/CD 自动化流程
     *
     * 缺点：
     * - 需要稳定的网络连接
     * - 下载可能失败（网络问题、服务器故障）
     * - 大文件下载耗时较长
     * - 文件版本可能与代码不完全匹配
     *
     * 适用场景：
     * - 开发和测试环境
     * - 持续集成环境
     * - 快速原型开发
     * - 偶尔部署的环境
     *
     * @note 下载失败时会回退到 LOCATION_REPOSITORY
     * @note 下载的文件会缓存到本地，避免重复下载
     */
    LOCATION_DOWNLOAD
};

/**
 * @brief 数据库更新器工具类
 *
 * 提供数据库更新过程中的辅助工具函数，主要用于检查和配置 MySQL 可执行文件路径。
 * 该类为静态工具类，所有方法均为静态方法，不可实例化。
 *
 * 主要职责：
 * - 检测系统中 MySQL 客户端工具的安装位置
 * - 处理不同操作系统和发行版的 MySQL 工具命名差异
 * - 验证 MySQL 工具的可用性和版本兼容性
 * - 缓存 MySQL 工具路径以提高性能
 *
 * MySQL 客户端工具名称在不同系统上的差异：
 * - Linux: 通常为 'mysql'
 * - macOS (Homebrew): 可能为 'mysql' 或 'mysql5' 等
 * - Windows: 通常为 'mysql.exe'
 * - 某些发行版: 可能为 'mariadb' 命令
 *
 * 配置方式：
 * - 通过 worldserver.conf 中的 Updates.MysqlExecutablePath 配置项指定路径
 * - 如果未配置，则在系统 PATH 中自动查找
 *
 * 使用示例：
 * @code
 * // 检查 MySQL 工具是否可用
 * if (DBUpdaterUtil::CheckExecutable())
 * {
 *     // 获取 MySQL 工具路径
 *     std::string mysqlPath = DBUpdaterUtil::GetCorrectedMySQLExecutable();
 *     // 使用 mysqlPath 执行数据库操作
 * }
 * else
 * {
 *     // 提示用户安装 MySQL 客户端工具
 * }
 * @endcode
 *
 * @note 此类不可实例化，所有方法均为静态方法
 * @note 首次调用时会缓存 MySQL 路径，后续调用直接返回缓存值
 *
 * @see DBUpdater 数据库更新器主类
 */
class DBUpdaterUtil
{
public:
    /**
     * @brief 获取修正后的 MySQL 可执行文件路径
     * @return 返回 MySQL 可执行文件的完整路径字符串
     *
     * 检查系统中 MySQL 客户端工具的可用性，并返回正确的可执行文件路径。
     * 此方法实现了智能查找逻辑，确保在大多数环境下都能找到 MySQL 工具。
     *
     * 查找顺序：
     * 1. 检查配置文件中的 Updates.MysqlExecutablePath 配置项
     * 2. 如果配置了路径，验证文件是否存在且可执行
     * 3. 如果未配置或配置无效，在系统 PATH 中查找 'mysql'
     * 4. 尝试常见替代名称：'mysql5', 'mysql8', 'mariadb' 等
     * 5. 在常见安装位置查找（Windows: C:\Program Files\MySQL\...）
     *
     * 路径修正处理：
     * - 移除路径中的引号（配置文件中可能包含）
     * - 处理路径中的空格（Windows 系统常见）
     * - 转换相对路径为绝对路径
     * - 验证路径的可执行权限
     *
     * 缓存机制：
     * - 首次调用时执行查找逻辑
     * - 结果存储在静态变量中
     * - 后续调用直接返回缓存值，避免重复查找
     *
     * @note 如果找不到 MySQL 工具，返回空字符串
     * @note 在 Linux/macOS 上，路径通常已包含在 PATH 中
     * @note 在 Windows 上，可能需要手动配置路径
     *
     * @performance 首次调用可能需要 10-100ms（查找文件系统）
     * @performance 后续调用返回缓存值，几乎无开销
     *
     * @see CheckExecutable() 检查 MySQL 工具是否可用
     */
    static std::string GetCorrectedMySQLExecutable();

    /**
     * @brief 检查 MySQL 可执行文件是否可用
     * @return 如果 MySQL 客户端工具可用返回 true，否则返回 false
     *
     * 验证系统是否安装并配置了 MySQL 客户端工具，确保数据库更新操作可以执行。
     * 这是在执行数据库更新前的预检查步骤。
     *
     * 检查内容：
     * 1. 调用 GetCorrectedMySQLExecutable() 获取路径
     * 2. 检查路径是否为空（空表示未找到）
     * 3. 在 Linux/macOS 上检查文件执行权限
     * 4. 尝试执行 'mysql --version' 验证工具可用性
     * 5. 检查版本兼容性（可选）
     *
     * 失败原因：
     * - MySQL 客户端未安装
     * - MySQL 工具不在系统 PATH 中
     * - 配置的路径不正确或文件不存在
     * - 文件存在但没有执行权限
     * - 工具损坏或版本不兼容
     *
     * 错误处理：
     * - 检查失败会记录详细的错误日志
     * - 包含失败原因和建议的解决方案
     * - 在服务器启动时显示警告信息
     *
     * @note 建议在执行任何数据库操作前调用此方法
     * @note 即使检查通过，执行 SQL 时仍可能失败（权限、网络等）
     *
     * @performance 执行时间通常为 10-50ms
     * @performance 包含进程创建和命令执行开销
     *
     * @see GetCorrectedMySQLExecutable() 获取 MySQL 路径
     */
    static bool CheckExecutable();

private:
    /**
     * @brief 获取修正路径的静态引用
     * @return 返回修正后的 MySQL 可执行文件路径的静态引用
     *
     * 内部辅助函数，用于实现路径缓存机制。
     * 使用函数内静态局部变量实现线程安全的延迟初始化。
     *
     * 实现原理：
     * - 使用 Meyers' Singleton 模式
     * - 静态局部变量在首次调用时初始化
     * - C++11 保证静态局部变量的线程安全初始化
     * - 返回引用允许外部修改缓存值（一般不需要）
     *
     * 缓存生命周期：
     * - 程序启动时：变量未初始化
     * - 首次调用 GetCorrectedMySQLExecutable()：初始化并查找路径
     * - 程序运行期间：保持缓存值不变
     * - 程序结束：自动销毁
     *
     * @note 仅被 GetCorrectedMySQLExecutable() 内部调用
     * @note 不需要手动清除缓存（程序运行期间 MySQL 路径不应改变）
     *
     * @performance 返回引用，无内存拷贝开销
     */
    static std::string& corrected_path();

    // 禁止实例化
    DBUpdaterUtil() = delete;  ///< 删除构造函数，禁止实例化
    ~DBUpdaterUtil() = delete;  ///< 删除析构函数，禁止实例化
};

/**
 * @brief 数据库更新器模板类
 *
 * 负责管理数据库架构的创建、更新和填充操作。
 * 这是一个模板类，可以处理不同类型的数据库连接池（LoginDatabase、WorldDatabase、CharacterDatabase）。
 *
 * 主要功能包括：
 * - 创建数据库表结构
 * - 应用增量更新（SQL 更新文件）
 * - 填充基础数据
 * - 管理数据库版本控制
 *
 * 使用模板特化机制为不同数据库类型提供具体实现。
 * 每个特化版本需要提供以下静态方法的实现：
 * - GetConfigEntry(): 返回配置文件中的数据库连接配置项名称
 * - GetTableName(): 返回数据库更新记录表的名称
 * - GetBaseFile(): 返回基础数据库 SQL 文件名
 *
 * @tparam T 数据库连接类型，必须是具体的数据库连接类
 *         支持的类型：
 *         - WorldDatabaseConnection: 世界数据库连接
 *         - LoginDatabaseConnection: 登录数据库连接
 *         - CharacterDatabaseConnection: 角色数据库连接
 *
 * @note 更新操作时机：
 *       - 服务器启动时自动执行（如果配置启用）
 *       - 手动调用 worldserver 命令触发
 *
 * @warning 数据库更新是不可逆操作，建议在更新前备份重要数据
 *
 * @example
 * // 使用 DBUpdater 更新世界数据库
 * DatabaseWorkerPool<WorldDatabaseConnection> worldPool;
 * DBUpdater<WorldDatabaseConnection>::Update(worldPool);
 *
 * @example
 * // 创建新的数据库架构
 * DBUpdater<WorldDatabaseConnection>::Create(worldPool);
 */
template <class T>
class TC_DATABASE_API DBUpdater
{
public:
    using Path = boost::filesystem::path;  ///< 文件路径类型别名，使用 Boost 文件系统库

    /**
     * @brief 获取数据库配置项名称
     * @return 返回配置文件中对应数据库的配置项名称字符串
     *
     * 根据模板参数 T 返回相应的配置项名称。
     * 例如：
     * - WorldDatabaseConnection -> "WorldDatabaseInfo"
     * - LoginDatabaseConnection -> "LoginDatabaseInfo"
     * - CharacterDatabaseConnection -> "CharacterDatabaseInfo"
     *
     * @note 纯虚函数，必须在模板特化版本中实现
     * @note 配置项名称对应 worldserver.conf 中的数据库连接配置节
     *
     * @performance 返回静态字符串引用，无性能开销
     */
    static inline std::string GetConfigEntry();

    /**
     * @brief 获取数据库更新记录表名
     * @return 返回用于记录数据库更新历史的表名字符串
     *
     * 返回存储数据库更新版本信息的表名。
     * 每个数据库类型使用不同的表名以避免冲突：
     * - WorldDatabase: 通常为 "updates" 或 "updates_include"
     * - LoginDatabase: 通常为 "updates"
     * - CharacterDatabase: 通常为 "updates"
     *
     * 该表记录了所有已应用的 SQL 更新文件信息，包括：
     * - 更新文件名
     * - 应用时间戳
     * - 更新哈希值（用于验证完整性）
     *
     * @note 纯虚函数，必须在模板特化版本中实现
     * @note 表结构在 Create() 方法中创建
     *
     * @performance 返回静态字符串引用，无性能开销
     */
    static inline std::string GetTableName();

    /**
     * @brief 获取基础数据库文件名
     * @return 返回基础数据库 SQL 文件的名称字符串
     *
     * 返回用于初始化数据库的基础 SQL 文件名。
     * 该文件包含完整的数据库架构和基础数据，通常由 TrinityCore 项目发布。
     * 文件命名格式示例：
     * - WorldDatabase: "TDB_world_335.24101_2024_09_23.sql"
     * - LoginDatabase: "TDB_auth_335.xxx.sql"
     * - CharacterDatabase: "TDB_characters_335.xxx.sql"
     *
     * 基础文件包含：
     * - 完整的表结构定义（CREATE TABLE）
     * - 初始数据插入（INSERT）
     * - 索引和约束定义
     * - 存储过程和触发器（如果有）
     *
     * @note 文件位于 sql/base/ 目录下
     * @note 如果本地文件不存在，可能需要从远程下载（取决于 GetBaseLocationType()）
     *
     * @performance 返回静态字符串，无文件系统访问开销
     */
    static std::string GetBaseFile();

    /**
     * @brief 检查数据库更新是否启用
     * @param updateMask 更新掩码，用于控制更新行为的位标志
     * @return 如果该数据库的更新功能已启用返回 true，否则返回 false
     *
     * 根据配置文件中的设置和更新掩码判断是否应该执行数据库更新操作。
     * 管理员可以在 worldserver.conf 中配置：
     * - Updates.EnableUpdates: 全局启用/禁用更新
     * - Updates.AutoSetup: 自动创建和填充数据库
     * - Updates.Redundancy: 更新文件冗余检查
     *
     * updateMask 参数用于细粒度控制：
     * - 位标志可以指定特定数据库的更新权限
     * - 支持运行时动态启用/禁用更新
     *
     * @note 禁用更新不影响数据库的正常使用，但可能导致版本不匹配
     * @note 生产环境建议在测试服务器验证更新后再应用到生产环境
     *
     * @performance 直接读取配置文件，开销极小
     */
    static bool IsEnabled(uint32 const updateMask);

    /**
     * @brief 获取基础数据库文件的位置类型
     * @return 返回基础数据库文件的来源位置枚举值
     *
     * 确定从哪里获取基础数据库文件：
     * - LOCATION_REPOSITORY: 从本地 sql/base 目录加载
     *   * 适用于离线环境或手动管理更新的场景
     *   * 文件由源码管理或手动复制提供
     *
     * - LOCATION_DOWNLOAD: 从远程服务器下载
     *   * 适用于持续集成或自动部署环境
     *   * 从 TrinityCore 官方服务器获取最新基础文件
     *
     * 此设置通过配置项 Updates.BaseLocation 控制。
     *
     * @note 下载失败时会回退到本地仓库查找
     * @note 大型数据库基础文件（如 WorldDatabase）可能超过 100MB
     *
     * @performance 仅返回配置值，无 I/O 操作
     */
    static BaseLocation GetBaseLocationType();

    /**
     * @brief 创建数据库架构
     * @param pool 目标数据库连接池引用
     * @return 创建成功返回 true，失败返回 false
     *
     * 创建数据库的基础架构，包括所有必要的表结构。
     * 这是数据库初始化的第一步，通常在数据库为空时执行。
     *
     * 执行流程：
     * 1. 连接到目标数据库
     * 2. 检查数据库是否已存在表结构
     * 3. 创建 updates 表（如果不存在）
     * 4. 执行基础数据库文件中的 CREATE TABLE 语句
     * 5. 创建必要的索引和约束
     *
     * @warning 此操作会创建表结构，但如果表已存在通常不会删除重建
     * @warning 建议在执行前备份数据库
     *
     * @note 如果数据库已完全初始化，此方法会快速返回
     * @note 执行时间取决于数据库服务器性能和网络延迟
     *
     * @performance 对于空数据库，执行时间通常为几秒到几十秒
     * @performance 大型数据库（WorldDatabase）可能需要更长时间
     *
     * @throws UpdateException 当数据库连接失败或 SQL 执行错误时抛出
     */
    static bool Create(DatabaseWorkerPool<T>& pool);

    /**
     * @brief 更新数据库
     * @param pool 目标数据库连接池引用
     * @return 更新成功返回 true，失败返回 false
     *
     * 应用所有待执行的增量 SQL 更新文件到数据库。
     * 这是数据库维护的核心方法，确保数据库架构与最新代码版本匹配。
     *
     * 详细执行流程：
     * 1. 检查 updates 表是否存在（不存在则创建）
     * 2. 从 updates 表读取已应用的更新文件列表
     * 3. 扫描 sql/updates/<database>/ 目录查找所有更新文件
     * 4. 比对文件名和哈希值，确定待应用的更新列表
     * 5. 按照文件名排序（日期格式 YYYY_MM_DD_i_database.sql）
     * 6. 逐个应用更新文件，每个文件在独立事务中执行
     * 7. 更新成功后，在 updates 表中记录更新信息
     * 8. 如果某个更新失败，停止后续更新并报告错误
     *
     * 更新文件命名规则：
     * - 格式：YYYY_MM_DD_i_database.sql
     * - YYYY: 年份（如 2024）
     * - MM: 月份（01-12）
     * - DD: 日期（01-31）
     * - i: 当天的更新序号（00, 01, 02...）
     * - database: 目标数据库名（world, characters, auth）
     *
     * @warning 更新操作可能需要锁定表，在高负载期间可能影响服务
     * @warning 大型更新可能需要较长时间，建议在维护窗口执行
     * @warning 更新失败后需要手动修复或恢复备份
     *
     * @note 服务器启动时会自动调用此方法（如果更新已启用）
     * @note 可以通过配置项 Updates.AllowReapplyUpdates 控制是否允许重新应用
     *
     * @performance 执行时间取决于更新文件数量和大小
     * @performance WorldDatabase 更新可能包含数百个文件，耗时较长
     * @performance LoginDatabase 和 CharacterDatabase 更新通常较快
     *
     * @throws UpdateException 当更新文件缺失、SQL 语法错误或约束冲突时抛出
     *
     * @see Create() 创建数据库架构
     * @see Populate() 填充基础数据
     */
    static bool Update(DatabaseWorkerPool<T>& pool);

    /**
     * @brief 填充数据库
     * @param pool 目标数据库连接池引用
     * @return 填充成功返回 true，失败返回 false
     *
     * 使用基础数据库文件填充数据库结构和数据。
     * 这是数据库初始化的核心方法，在空数据库上执行完整的初始导入。
     *
     * 执行流程：
     * 1. 检查数据库是否已有数据（避免意外覆盖）
     * 2. 确定基础文件位置（本地或远程）
     * 3. 如果配置为下载，从远程服务器获取基础文件
     * 4. 连接到数据库服务器
     * 5. 使用 MySQL 客户端工具执行基础 SQL 文件
     * 6. 验证关键表是否创建成功
     * 7. 在 updates 表中记录基础文件为已应用
     *
     * 此方法与 Create() 的区别：
     * - Create(): 仅创建表结构，不导入数据
     * - Populate(): 导入完整的架构和数据
     * - Create() + Populate() = 完整初始化
     *
     * @warning 此操作会导入大量数据，可能覆盖现有数据
     * @warning 仅在数据库为空或需要重置时使用
     * @warning 执行时间较长，大型数据库可能需要数分钟
     *
     * @note 通常在首次部署或数据库损坏需要重建时执行
     * @note 可通过配置项 Updates.AutoSetup 启用自动填充
     *
     * @performance WorldDatabase 基础文件约 100MB+，填充需要 5-15 分钟
     * @performance LoginDatabase 和 CharacterDatabase 较小，通常在 1 分钟内完成
     * @performance 性能受数据库服务器性能和网络带宽影响
     *
     * @throws UpdateException 当基础文件缺失、下载失败或 SQL 执行错误时抛出
     *
     * @see Create() 创建数据库架构
     * @see Update() 应用增量更新
     */
    static bool Populate(DatabaseWorkerPool<T>& pool);

private:
    /**
     * @brief 从数据库检索查询结果
     * @param pool 目标数据库连接池引用
     * @param query 要执行的 SQL 查询字符串（SELECT 语句）
     * @return 返回查询结果集指针，如果查询失败或无结果则返回 nullptr
     *
     * 执行指定的 SQL 查询并返回结果集。
     * 这是内部辅助方法，主要用于：
     * - 检查数据库版本信息
     * - 查询 updates 表中的已应用更新列表
     * - 验证表结构是否存在
     * - 获取数据库配置信息
     *
     * @note 使用同步方式执行查询，会阻塞当前线程
     * @note 返回的 QueryResult 需要调用者检查是否为 nullptr
     * @note 仅用于内部查询，不处理复杂的用户查询
     *
     * @performance 小型查询通常在毫秒级完成
     * @performance 复杂查询可能需要较长时间，取决于索引和数据量
     *
     * @see Apply() 执行不返回结果的 SQL 语句
     */
    static QueryResult Retrieve(DatabaseWorkerPool<T>& pool, std::string const& query);

    /**
     * @brief 执行 SQL 语句
     * @param pool 目标数据库连接池引用
     * @param query 要执行的 SQL 语句字符串（INSERT/UPDATE/DELETE 等）
     *
     * 执行不返回结果集的 SQL 语句。
     * 这是内部辅助方法，用于更新数据库状态。
     *
     * 执行流程：
     * 1. 从连接池获取数据库连接
     * 2. 准备 SQL 语句（防止 SQL 注入）
     * 3. 执行语句
     * 4. 释放连接回连接池
     *
     * 主要用于：
     * - 向 updates 表插入更新记录
     * - 更新数据库版本信息
     * - 执行维护性 SQL 语句
     *
     * @note 异步执行，不等待执行结果
     * @note 语句执行失败会记录到错误日志
     * @note 不返回执行结果或影响行数
     *
     * @performance 执行时间取决于语句复杂度和数据量
     * @performance 简单 INSERT 通常在毫秒级完成
     *
     * @see Retrieve() 执行返回结果的查询
     * @see ApplyFile() 执行 SQL 文件
     */
    static void Apply(DatabaseWorkerPool<T>& pool, std::string const& query);

    /**
     * @brief 执行 SQL 文件（使用连接池参数）
     * @param pool 目标数据库连接池引用
     * @param path 要执行的 SQL 文件的文件系统路径
     *
     * 读取并执行指定路径的 SQL 文件。
     * 使用数据库连接池中配置的连接参数连接到数据库。
     *
     * 执行流程：
     * 1. 从连接池获取连接参数（主机、端口、用户名、密码、数据库名）
     * 2. 构造 MySQL 命令行调用
     * 3. 使用系统调用执行 mysql 客户端工具
     * 4. 重定向 SQL 文件内容到 mysql 标准输入
     * 5. 捕获执行输出和错误信息
     * 6. 检查执行结果并记录日志
     *
     * SQL 文件执行方式：
     * - 使用 MySQL 命令行客户端：mysql -h host -u user -p<password> database < file.sql
     * - 支持大型文件（不受内存限制）
     * - 自动处理文件编码（UTF-8）
     *
     * @warning 文件中的错误可能导致部分 SQL 执行失败
     * @warning 不在事务中执行，失败可能留下部分更新的状态
     * @warning 需要 MySQL 客户端工具在系统 PATH 中可用
     *
     * @note 文件大小无限制，适合大型基础文件
     * @note 执行时间取决于文件大小和复杂度
     *
     * @performance 对于大文件（>10MB），此方法比逐条执行更高效
     * @performance 通过管道传输，避免临时文件创建
     *
     * @throws UpdateException 当文件不存在或 MySQL 客户端执行失败时抛出
     *
     * @see ApplyFile(pool, host, user, ...) 使用自定义连接参数执行
     */
    static void ApplyFile(DatabaseWorkerPool<T>& pool, Path const& path);

    /**
     * @brief 执行 SQL 文件（使用自定义连接参数）
     * @param pool 数据库连接池引用（用于日志记录和辅助功能）
     * @param host 数据库服务器主机名或 IP 地址
     *             示例：'localhost', '192.168.1.100', 'db.example.com'
     * @param user 数据库用户名
     *             需要有足够权限执行文件中的所有 SQL 语句
     * @param password 数据库用户密码
     *                 明文传递，确保在安全环境中使用
     * @param port_or_socket TCP 端口号或 Unix socket 文件路径
     *                       数字字符串表示端口（如 "3306"）
     *                       文件路径表示 Unix socket（如 "/var/run/mysqld/mysqld.sock"）
     * @param database 目标数据库名称
     *                 SQL 文件中的语句将在此数据库中执行
     * @param ssl SSL/TLS 连接配置参数字符串
     *            格式取决于 MySQL 客户端版本
     *            空字符串表示不使用 SSL
     * @param path 要执行的 SQL 文件的文件系统路径
     *             支持绝对路径和相对路径
     *
     * 使用显式指定的连接参数执行 SQL 文件。
     * 这个重载版本提供了更灵活的连接控制，适用于：
     * - 连接到与连接池配置不同的数据库实例
     * - 使用管理员权限执行特殊操作
     * - 在数据库初始化阶段（连接池未完全配置）
     * - 执行跨数据库的操作
     *
     * 与简化版本的差异：
     * - 简化版本：使用连接池中的连接参数
     * - 此版本：使用传入的参数，可能完全不同
     *
     * MySQL 客户端调用示例：
     * mysql -h localhost -u root -pPassword -P 3306 -D world < file.sql
     * mysql -h localhost -u root -pPassword --socket=/var/run/mysql.sock -D world < file.sql
     *
     * @warning 密码以明文形式传递，可能出现在进程列表中
     * @warning 建议仅在受信任的环境中使用
     * @warning 确保 MySQL 客户端工具已正确安装
     *
     * @note 支持通过 Unix socket 连接，提高本地连接性能
     * @note SSL 参数用于加密连接，增强安全性
     *
     * @performance Unix socket 连接比 TCP 连接更快
     * @performance 本地执行可减少网络延迟
     *
     * @throws UpdateException 当连接失败或 SQL 执行错误时抛出
     *
     * @see ApplyFile(pool, path) 使用连接池参数执行
     */
    static void ApplyFile(DatabaseWorkerPool<T>& pool, std::string const& host, std::string const& user,
        std::string const& password, std::string const& port_or_socket, std::string const& database, std::string const& ssl,
        Path const& path);
};

#endif // DBUpdater_h__
