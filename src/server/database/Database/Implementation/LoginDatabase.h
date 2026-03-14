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
 * @file LoginDatabase.h
 * @brief 登录数据库连接定义文件
 *
 * 本文件定义了登录数据库（Login Database）的连接类和预编译语句枚举。
 * 登录数据库负责存储账号认证、服务器列表、权限管理等核心数据。
 *
 * 数据库职责：
 * 1. 账号管理：创建、查询、更新账号信息
 * 2. 认证服务：验证用户登录凭据（SRP6协议）
 * 3. 权限控制：GM等级、RBAC权限管理
 * 4. 安全防护：账号/IP封禁、登录尝试限制
 * 5. 服务器管理：服务器列表、在线状态、人口统计
 *
 * 设计特点：
 * - 使用预编译语句提高性能和安全性
 * - 支持同步（CONNECTION_SYNCH）和异步（CONNECTION_ASYNC）操作
 * - 支持跨服查询（RealmID = -1 表示全局权限）
 *
 * 使用场景：
 * - 认证服务器（authserver）的主要数据源
 * - 世界服务器（worldserver）查询账号权限
 * - 管理工具进行账号管理操作
 */

#ifndef _LOGINDATABASE_H
#define _LOGINDATABASE_H

#include "MySQLConnection.h"

/**
 * @enum LoginDatabaseStatements
 * @brief 登录数据库预编译语句枚举
 *
 * 定义所有登录数据库操作使用的预编译语句ID。
 * 预编译语句的使用可以：
 * 1. 提高数据库性能：避免重复解析SQL语句
 * 2. 防止SQL注入：参数化查询自动转义特殊字符
 * 3. 代码可维护性：集中管理所有SQL语句
 *
 * 命名规范：
 * {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
 * - SEL: SELECT 查询操作
 * - INS: INSERT 插入操作
 * - UPD: UPDATE 更新操作
 * - DEL: DELETE 删除操作
 * - REP: REPLACE 替换操作
 *
 * 当更新多个字段时，考虑使用调用函数名作为后缀。
 */
enum LoginDatabaseStatements : uint32
{
    /*  命名规范示例:
        {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
        当更新多个字段时，考虑查看调用函数名以获取合适的后缀。
    */

    // ==================== 服务器列表相关 ====================
    /**
     * @brief 查询可用服务器列表
     *
     * 查询所有状态正常（flag <> 3 表示非离线/维护状态）的服务器。
     * 返回服务器ID、名称、地址、端口、图标、时区、安全等级等信息。
     * 按服务器名称排序，供客户端选择服务器时显示。
     */
    LOGIN_SEL_REALMLIST,

    // ==================== IP封禁管理 ====================
    /**
     * @brief 删除已过期的IP封禁记录
     *
     * 删除临时封禁且已过期的IP记录（unbandate != bandate 表示非永久封禁）。
     * 在服务器启动或定期清理时调用。
     */
    LOGIN_DEL_EXPIRED_IP_BANS,

    /**
     * @brief 删除已过期的账号封禁记录
     *
     * 将已过期的账号封禁状态设为非激活。
     * 注意：不删除记录，只更新状态，保留封禁历史。
     */
    LOGIN_UPD_EXPIRED_ACCOUNT_BANS,

    /**
     * @brief 查询IP是否被封禁
     *
     * 检查指定IP是否在封禁列表中。
     * 返回封禁状态和过期时间信息。
     */
    LOGIN_SEL_IP_INFO,

    /**
     * @brief 自动封禁IP（登录失败多次）
     *
     * 当同一IP登录失败次数达到阈值时自动封禁。
     * 由认证服务器在检测到暴力破解时调用。
     */
    LOGIN_INS_IP_AUTO_BANNED,

    // ==================== 账号封禁查询 ====================
    /**
     * @brief 查询所有被封禁的账号
     *
     * 查询所有当前处于封禁状态的账号列表。
     * 用于GM管理界面显示。
     */
    LOGIN_SEL_ACCOUNT_BANNED_ALL,

    /**
     * @brief 按过滤器查询被封禁的账号
     *
     * 根据用户名模糊查询被封禁的账号。
     * 支持部分匹配查询。
     */
    LOGIN_SEL_ACCOUNT_BANNED_BY_FILTER,

    /**
     * @brief 按用户名查询是否被封禁
     *
     * 检查指定用户名的账号是否被封禁。
     * 用于登录验证和账号状态检查。
     */
    LOGIN_SEL_ACCOUNT_BANNED_BY_USERNAME,

    /**
     * @brief 自动封禁账号（登录失败多次）
     *
     * 当账号登录失败次数达到阈值时自动封禁。
     */
    LOGIN_INS_ACCOUNT_AUTO_BANNED,

    /**
     * @brief 解除账号封禁
     *
     * 删除指定账号的封禁记录。
     */
    LOGIN_DEL_ACCOUNT_BANNED,

    // ==================== 认证协议相关 ====================
    /**
     * @brief 更新登录凭据（SRP6）
     *
     * 在账号创建或密码重置时更新salt和verifier。
     * SRP6安全认证协议的核心数据。
     */
    LOGIN_UPD_LOGON,

    /**
     * @brief 更新登录证明（认证成功后）
     *
     * 认证成功后更新会话密钥、最后登录IP、时间等信息。
     * 重置失败登录计数为0。
     */
    LOGIN_UPD_LOGONPROOF,

    /**
     * @brief 查询登录挑战数据
     *
     * 获取账号的SRP6认证所需数据：salt、verifier等。
     * 用于处理客户端的登录挑战请求。
     */
    LOGIN_SEL_LOGONCHALLENGE,

    /**
     * @brief 查询重连挑战数据
     *
     * 获取已登录账号的重连认证数据。
     * 用于处理客户端断线重连时的快速认证。
     */
    LOGIN_SEL_RECONNECTCHALLENGE,

    /**
     * @brief 增加失败登录次数
     *
     * 记录登录失败，用于检测暴力破解攻击。
     * 失败次数过多可能触发自动封禁。
     */
    LOGIN_UPD_FAILEDLOGINS,

    // ==================== 账号查询相关 ====================
    /**
     * @brief 按用户名查询账号ID
     */
    LOGIN_SEL_ACCOUNT_ID_BY_NAME,

    /**
     * @brief 按用户名查询账号列表
     */
    LOGIN_SEL_ACCOUNT_LIST_BY_NAME,

    /**
     * @brief 按用户名查询账号详细信息
     *
     * 查询账号的会话密钥、安全等级、扩展版本、静音状态等。
     * 用于登录后的账号状态初始化。
     */
    LOGIN_SEL_ACCOUNT_INFO_BY_NAME,

    /**
     * @brief 按邮箱查询账号列表
     */
    LOGIN_SEL_ACCOUNT_LIST_BY_EMAIL,

    /**
     * @brief 查询账号在各服务器的角色数量
     *
     * 用于角色列表显示和角色数量限制检查。
     */
    LOGIN_SEL_REALM_CHARACTER_COUNTS,

    /**
     * @brief 按IP查询使用该IP的账号
     *
     * 用于安全审计和查找多账号用户。
     */
    LOGIN_SEL_ACCOUNT_BY_IP,

    // ==================== IP封禁管理 ====================
    /**
     * @brief 添加IP封禁
     */
    LOGIN_INS_IP_BANNED,

    /**
     * @brief 移除IP封禁
     */
    LOGIN_DEL_IP_NOT_BANNED,

    /**
     * @brief 查询所有被封禁的IP
     */
    LOGIN_SEL_IP_BANNED_ALL,

    /**
     * @brief 按IP地址查询封禁状态
     */
    LOGIN_SEL_IP_BANNED_BY_IP,

    // ==================== 账号信息查询 ====================
    /**
     * @brief 按ID查询账号是否存在
     */
    LOGIN_SEL_ACCOUNT_BY_ID,

    /**
     * @brief 添加账号封禁记录
     */
    LOGIN_INS_ACCOUNT_BANNED,

    /**
     * @brief 解除账号封禁状态
     */
    LOGIN_UPD_ACCOUNT_NOT_BANNED,

    // ==================== 角色计数管理 ====================
    /**
     * @brief 删除服务器角色计数记录
     */
    LOGIN_DEL_REALM_CHARACTERS,

    /**
     * @brief 更新服务器角色计数
     */
    LOGIN_REP_REALM_CHARACTERS,

    /**
     * @brief 查询账号总角色数
     */
    LOGIN_SEL_SUM_REALM_CHARACTERS,

    // ==================== 账号创建和更新 ====================
    /**
     * @brief 创建新账号
     */
    LOGIN_INS_ACCOUNT,

    /**
     * @brief 初始化服务器角色计数
     */
    LOGIN_INS_REALM_CHARACTERS_INIT,

    /**
     * @brief 更新账号扩展版本
     */
    LOGIN_UPD_EXPANSION,

    /**
     * @brief 锁定/解锁账号
     */
    LOGIN_UPD_ACCOUNT_LOCK,

    /**
     * @brief 设置账号国家锁定
     */
    LOGIN_UPD_ACCOUNT_LOCK_COUNTRY,

    /**
     * @brief 插入日志记录
     */
    LOGIN_INS_LOG,

    /**
     * @brief 更新用户名
     */
    LOGIN_UPD_USERNAME,

    /**
     * @brief 更新邮箱
     */
    LOGIN_UPD_EMAIL,

    /**
     * @brief 更新注册邮箱
     */
    LOGIN_UPD_REG_EMAIL,

    /**
     * @brief 更新静音时间
     */
    LOGIN_UPD_MUTE_TIME,

    /**
     * @brief 更新静音时间（登录时）
     */
    LOGIN_UPD_MUTE_TIME_LOGIN,

    /**
     * @brief 更新最后登录IP
     */
    LOGIN_UPD_LAST_IP,

    /**
     * @brief 更新最后尝试登录IP
     */
    LOGIN_UPD_LAST_ATTEMPT_IP,

    /**
     * @brief 设置账号在线状态
     */
    LOGIN_UPD_ACCOUNT_ONLINE,

    /**
     * @brief 更新服务器运行时间和最大在线人数
     */
    LOGIN_UPD_UPTIME_PLAYERS,

    /**
     * @brief 删除过期日志
     */
    LOGIN_DEL_OLD_LOGS,

    // ==================== 权限管理 ====================
    /**
     * @brief 删除账号所有权限
     */
    LOGIN_DEL_ACCOUNT_ACCESS,

    /**
     * @brief 删除账号在指定服务器的权限
     */
    LOGIN_DEL_ACCOUNT_ACCESS_BY_REALM,

    /**
     * @brief 添加账号权限
     */
    LOGIN_INS_ACCOUNT_ACCESS,

    /**
     * @brief 按用户名查询账号ID
     */
    LOGIN_GET_ACCOUNT_ID_BY_USERNAME,

    /**
     * @brief 查询账号在指定服务器的GM等级
     */
    LOGIN_GET_GMLEVEL_BY_REALMID,

    /**
     * @brief 按ID查询用户名
     */
    LOGIN_GET_USERNAME_BY_ID,

    /**
     * @brief 验证密码（按ID）
     */
    LOGIN_SEL_CHECK_PASSWORD,

    /**
     * @brief 验证密码（按用户名）
     */
    LOGIN_SEL_CHECK_PASSWORD_BY_NAME,

    // ==================== 账号信息查询（GM工具） ====================
    /**
     * @brief 查询账号详细信息（PINFO命令）
     *
     * 查询用户名、GM等级、邮箱、最后IP、登录时间、静音信息等。
     */
    LOGIN_SEL_PINFO,

    /**
     * @brief 查询账号封禁信息（PINFO命令）
     */
    LOGIN_SEL_PINFO_BANS,

    /**
     * @brief 查询所有GM账号
     */
    LOGIN_SEL_GM_ACCOUNTS,

    /**
     * @brief 查询账号基本信息
     */
    LOGIN_SEL_ACCOUNT_INFO,

    /**
     * @brief 检查账号是否有指定权限
     */
    LOGIN_SEL_ACCOUNT_ACCESS_SECLEVEL_TEST,

    /**
     * @brief 查询账号权限信息
     */
    LOGIN_SEL_ACCOUNT_ACCESS,

    /**
     * @brief 查询账号WHOIS信息
     */
    LOGIN_SEL_ACCOUNT_WHOIS,

    /**
     * @brief 查询服务器所需安全等级
     */
    LOGIN_SEL_REALMLIST_SECURITY_LEVEL,

    /**
     * @brief 删除账号
     */
    LOGIN_DEL_ACCOUNT,

    /**
     * @brief 查询自动广播消息
     */
    LOGIN_SEL_AUTOBROADCAST,

    /**
     * @brief 查询最后尝试登录IP
     */
    LOGIN_SEL_LAST_ATTEMPT_IP,

    /**
     * @brief 查询最后登录IP
     */
    LOGIN_SEL_LAST_IP,

    /**
     * @brief 按ID查询邮箱
     */
    LOGIN_GET_EMAIL_BY_ID,

    // ==================== IP日志记录 ====================
    /**
     * @brief 记录账号登录删除IP日志
     * 完整名称: Login_Insert_AccountLoginDeLete_IP_Logging
     */
    LOGIN_INS_ALDL_IP_LOGGING,

    /**
     * @brief 记录登录失败IP日志
     * 完整名称: Login_Insert_FailedAccountLogin_IP_Logging
     */
    LOGIN_INS_FACL_IP_LOGGING,

    /**
     * @brief 记录角色删除IP日志
     * 完整名称: Login_Insert_CharacterDelete_IP_Logging
     */
    LOGIN_INS_CHAR_IP_LOGGING,

    /**
     * @brief 记录密码错误登录失败IP日志
     * 完整名称: Login_Insert_Failed_Account_Login_due_password_IP_Logging
     */
    LOGIN_INS_FALP_IP_LOGGING,

    // ==================== RBAC权限系统 ====================
    /**
     * @brief 查询账号RBAC权限
     */
    LOGIN_SEL_ACCOUNT_ACCESS_BY_ID,

    /**
     * @brief 查询账号RBAC权限列表
     */
    LOGIN_SEL_RBAC_ACCOUNT_PERMISSIONS,

    /**
     * @brief 添加RBAC权限
     */
    LOGIN_INS_RBAC_ACCOUNT_PERMISSION,

    /**
     * @brief 删除RBAC权限
     */
    LOGIN_DEL_RBAC_ACCOUNT_PERMISSION,

    // ==================== 账号静音管理 ====================
    /**
     * @brief 添加静音记录
     */
    LOGIN_INS_ACCOUNT_MUTE,

    /**
     * @brief 查询静音历史
     */
    LOGIN_SEL_ACCOUNT_MUTE_INFO,

    /**
     * @brief 删除静音记录
     */
    LOGIN_DEL_ACCOUNT_MUTED,

    // ==================== 安全验证相关 ====================
    /**
     * @brief 查询安全摘要
     */
    LOGIN_SEL_SECRET_DIGEST,

    /**
     * @brief 插入安全摘要
     */
    LOGIN_INS_SECRET_DIGEST,

    /**
     * @brief 删除安全摘要
     */
    LOGIN_DEL_SECRET_DIGEST,

    /**
     * @brief 查询TOTP密钥（双因素认证）
     */
    LOGIN_SEL_ACCOUNT_TOTP_SECRET,

    /**
     * @brief 更新TOTP密钥
     */
    LOGIN_UPD_ACCOUNT_TOTP_SECRET,

    // 枚举最大值，用于数组大小定义
    MAX_LOGINDATABASE_STATEMENTS
};

/**
 * @class LoginDatabaseConnection
 * @brief 登录数据库连接类
 *
 * 继承自 MySQLConnection，实现登录数据库特有的预编译语句准备。
 * 支持同步和异步两种连接模式：
 * - 同步连接：用于需要立即获取结果的查询（如认证验证）
 * - 异步连接：用于可以延迟执行的操作（如日志记录、状态更新）
 *
 * 线程安全：
 * - 同步连接应在主线程或专用线程使用
 * - 异步连接通过生产者-消费者队列与工作线程通信
 *
 * 性能优化：
 * - 预编译语句只准备一次，重复使用
 * - 重连时不重新分配语句数组，只重新准备语句
 */
class TC_DATABASE_API LoginDatabaseConnection : public MySQLConnection
{
public:
    /**
     * @brief 预编译语句类型别名
     *
     * 方便在基类中引用本数据库的语句枚举类型。
     */
    typedef LoginDatabaseStatements Statements;

    /**
     * @brief 构造同步数据库连接
     *
     * @param connInfo 数据库连接信息（地址、端口、用户名、密码、数据库名）
     *
     * 用于创建同步执行的数据库连接。
     * 同步连接会阻塞当前线程直到操作完成。
     */
    LoginDatabaseConnection(MySQLConnectionInfo& connInfo);

    /**
     * @brief 构造异步数据库连接
     *
     * @param q SQL操作队列（生产者-消费者队列）
     * @param connInfo 数据库连接信息
     *
     * 用于创建异步执行的数据库连接。
     * 异步连接从队列中获取操作请求，在工作线程中执行。
     */
    LoginDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo);

    /**
     * @brief 析构函数
     */
    ~LoginDatabaseConnection();

    /**
     * @brief 准备数据库特有的预编译语句
     *
     * 重写基类虚函数，准备所有登录数据库操作的预编译语句。
     * 在连接建立后自动调用。
     *
     * 实现细节：
     * - 检查是否重连，非重连时初始化语句数组大小
     * - 为每个预编译语句调用 PrepareStatement()
     * - 指定语句执行模式（同步/异步/两者皆可）
     *
     * 调用时机：
     * - 首次建立数据库连接时
     * - 连接断开后重新连接时
     *
     * 性能注意事项：
     * - 预编译语句准备是相对耗时操作，应尽量复用连接
     * - 重连时复用已分配的数组内存，仅重新准备语句
     */
    void DoPrepareStatements() override;
};

#endif
