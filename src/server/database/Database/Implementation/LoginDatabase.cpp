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
 * @file LoginDatabase.cpp
 * @brief 登录数据库连接实现文件
 *
 * 本文件实现了 LoginDatabaseConnection 类，主要包括：
 * 1. DoPrepareStatements() - 准备所有预编译SQL语句
 * 2. 构造函数和析构函数
 *
 * 预编译语句的执行模式说明：
 * - CONNECTION_SYNCH: 同步执行，阻塞当前线程，立即返回结果
 * - CONNECTION_ASYNC: 异步执行，放入队列，由工作线程处理，不阻塞
 * - CONNECTION_BOTH: 可同步可异步，根据调用方式决定
 *
 * 性能建议：
 * - 查询操作优先使用异步模式，避免阻塞主线程
 * - 需要立即结果的查询使用同步模式
 * - 批量操作使用事务提高效率
 */

#include "LoginDatabase.h"
#include "MySQLPreparedStatement.h"

/**
 * @brief 准备所有预编译语句
 *
 * 本函数在数据库连接建立后调用，准备所有登录数据库操作所需的SQL语句。
 * 使用预编译语句的优势：
 * 1. 性能提升：SQL只需解析一次，多次执行效率更高
 * 2. 安全防护：参数化查询自动处理转义，防止SQL注入
 * 3. 类型安全：编译时检查参数类型匹配
 */
void LoginDatabaseConnection::DoPrepareStatements()
{
    // 如果不是重连，则初始化预编译语句数组大小
    // 重连时数组已分配，只需重新准备语句即可
    if (!m_reconnecting)
        m_stmts.resize(MAX_LOGINDATABASE_STATEMENTS);

    // ==================== 服务器列表查询 ====================
    // 查询所有可用服务器（flag <> 3 表示非离线/维护状态）
    // 用于客户端登录时显示服务器选择界面
    // 按服务器名称排序
    PrepareStatement(LOGIN_SEL_REALMLIST, "SELECT id, name, address, localAddress, localSubnetMask, port, icon, flag, timezone, allowedSecurityLevel, population, gamebuild FROM realmlist WHERE flag <> 3 ORDER BY name", CONNECTION_SYNCH);

    // ==================== 封禁清理 ====================
    // 删除已过期的IP封禁记录
    // unbandate <> bandate 表示非永久封禁
    // 定期执行以清理过期的临时封禁
    PrepareStatement(LOGIN_DEL_EXPIRED_IP_BANS, "DELETE FROM ip_banned WHERE unbandate<>bandate AND unbandate<=UNIX_TIMESTAMP()", CONNECTION_ASYNC);

    // 更新已过期的账号封禁状态
    // 不删除记录，只将active设为0，保留封禁历史
    PrepareStatement(LOGIN_UPD_EXPIRED_ACCOUNT_BANS, "UPDATE account_banned SET active = 0 WHERE active = 1 AND unbandate<>bandate AND unbandate<=UNIX_TIMESTAMP()", CONNECTION_ASYNC);

    // ==================== IP验证 ====================
    // 检查指定IP是否被封禁
    // 返回封禁状态和过期时间
    // 登录验证时调用
    PrepareStatement(LOGIN_SEL_IP_INFO, "SELECT unbandate > UNIX_TIMESTAMP() OR unbandate = bandate AS banned, NULL as country FROM ip_banned WHERE ip = ?", CONNECTION_ASYNC);

    // 自动封禁IP（登录失败多次触发）
    // 由认证服务器在检测到暴力破解时调用
    PrepareStatement(LOGIN_INS_IP_AUTO_BANNED, "INSERT INTO ip_banned (ip, bandate, unbandate, bannedby, banreason) VALUES (?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP()+?, 'Trinity Auth', 'Failed login autoban')", CONNECTION_ASYNC);

    // ==================== IP封禁管理 ====================
    // 查询所有当前有效的IP封禁列表
    // 用于GM管理界面显示
    PrepareStatement(LOGIN_SEL_IP_BANNED_ALL, "SELECT ip, bandate, unbandate, bannedby, banreason FROM ip_banned WHERE (bandate = unbandate OR unbandate > UNIX_TIMESTAMP()) ORDER BY unbandate", CONNECTION_SYNCH);

    // 按IP地址模糊查询封禁记录
    PrepareStatement(LOGIN_SEL_IP_BANNED_BY_IP, "SELECT ip, bandate, unbandate, bannedby, banreason FROM ip_banned WHERE (bandate = unbandate OR unbandate > UNIX_TIMESTAMP()) AND ip LIKE CONCAT('%%', ?, '%%') ORDER BY unbandate", CONNECTION_SYNCH);

    // ==================== 账号封禁查询 ====================
    // 查询所有当前被封禁的账号
    PrepareStatement(LOGIN_SEL_ACCOUNT_BANNED_ALL, "SELECT account.id, username FROM account, account_banned WHERE account.id = account_banned.id AND active = 1 GROUP BY account.id", CONNECTION_SYNCH);

    // 按用户名过滤器查询被封禁账号
    PrepareStatement(LOGIN_SEL_ACCOUNT_BANNED_BY_FILTER, "SELECT account.id, username FROM account, account_banned WHERE account.id = account_banned.id AND active = 1 AND username LIKE CONCAT('%%', ?, '%%') GROUP BY account.id", CONNECTION_SYNCH);

    // 按完整用户名查询是否被封禁
    PrepareStatement(LOGIN_SEL_ACCOUNT_BANNED_BY_USERNAME, "SELECT account.id, username FROM account, account_banned WHERE account.id = account_banned.id AND active = 1 AND username = ? GROUP BY account.id", CONNECTION_SYNCH);

    // 自动封禁账号（登录失败次数过多）
    PrepareStatement(LOGIN_INS_ACCOUNT_AUTO_BANNED, "INSERT INTO account_banned (id, bandate, unbandate, bannedby, banreason, active) VALUES (?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP()+?, 'Trinity Auth', 'Failed login autoban', 1)", CONNECTION_ASYNC);

    // 解除账号封禁（删除封禁记录）
    PrepareStatement(LOGIN_DEL_ACCOUNT_BANNED, "DELETE FROM account_banned WHERE id = ?", CONNECTION_ASYNC);

    // ==================== SRP6认证相关 ====================
    // 更新登录凭据（salt和verifier）
    // 用于密码修改或账号创建
    PrepareStatement(LOGIN_UPD_LOGON, "UPDATE account SET salt = ?, verifier = ? WHERE id = ?", CONNECTION_ASYNC);

    // 认证成功后更新账号信息
    // 更新会话密钥、最后登录IP、时间、重置失败计数
    PrepareStatement(LOGIN_UPD_LOGONPROOF, "UPDATE account SET session_key_auth = ?, last_ip = ?, last_login = NOW(), locale = ?, failed_logins = 0, os = ?, timezone_offset = ? WHERE username = ?", CONNECTION_SYNCH);

    // 查询登录挑战数据
    // 获取SRP6认证所需的数据：salt、verifier、封禁状态、安全等级等
    // LEFT JOIN确保即使没有权限记录也能查询到账号
    PrepareStatement(LOGIN_SEL_LOGONCHALLENGE, "SELECT a.id, a.username, a.locked, a.lock_country, a.last_ip, a.failed_logins, ab.unbandate > UNIX_TIMESTAMP() OR ab.unbandate = ab.bandate, "
        "ab.unbandate = ab.bandate, aa.SecurityLevel, a.totp_secret, a.salt, a.verifier "
        "FROM account a LEFT JOIN account_access aa ON a.id = aa.AccountID LEFT JOIN account_banned ab ON ab.id = a.id AND ab.active = 1 WHERE a.username = ?", CONNECTION_ASYNC);

    // 查询重连挑战数据
    // 用于已登录账号的快速重连认证
    // 只查询已有会话密钥的账号
    PrepareStatement(LOGIN_SEL_RECONNECTCHALLENGE, "SELECT a.id, UPPER(a.username), a.locked, a.lock_country, a.last_ip, a.failed_logins, ab.unbandate > UNIX_TIMESTAMP() OR ab.unbandate = ab.bandate, "
        "ab.unbandate = ab.bandate, aa.SecurityLevel, a.session_key_auth "
        "FROM account a LEFT JOIN account_access aa ON a.id = aa.AccountID LEFT JOIN account_banned ab ON ab.id = a.id AND ab.active = 1 WHERE a.username = ? AND a.session_key_auth IS NOT NULL", CONNECTION_ASYNC);

    // 增加失败登录次数
    PrepareStatement(LOGIN_UPD_FAILEDLOGINS, "UPDATE account SET failed_logins = failed_logins + 1 WHERE username = ?", CONNECTION_ASYNC);

    // ==================== 账号查询 ====================
    // 按用户名查询账号ID
    PrepareStatement(LOGIN_SEL_ACCOUNT_ID_BY_NAME, "SELECT id FROM account WHERE username = ?", CONNECTION_SYNCH);

    // 按用户名查询账号列表（返回ID和用户名）
    PrepareStatement(LOGIN_SEL_ACCOUNT_LIST_BY_NAME, "SELECT id, username FROM account WHERE username = ?", CONNECTION_SYNCH);

    // 查询账号详细信息（用于登录初始化）
    // 包含会话密钥、安全等级、扩展版本、静音时间、推荐人等
    // RealmID IN (-1, ?) 表示查询全局权限或当前服务器权限
    PrepareStatement(LOGIN_SEL_ACCOUNT_INFO_BY_NAME, "SELECT a.id, a.session_key_auth, a.last_ip, a.locked, a.lock_country, a.expansion, a.mutetime, a.locale, a.recruiter, a.os, a.timezone_offset, aa.SecurityLevel, "
        "ab.unbandate > UNIX_TIMESTAMP() OR ab.unbandate = ab.bandate, r.id FROM account a LEFT JOIN account_access aa ON a.id = aa.AccountID AND aa.RealmID IN (-1, ?) "
        "LEFT JOIN account_banned ab ON a.id = ab.id AND ab.active = 1 LEFT JOIN account r ON a.id = r.recruiter WHERE a.username = ? AND a.session_key_auth IS NOT NULL ORDER BY aa.RealmID DESC LIMIT 1", CONNECTION_ASYNC);

    // 按邮箱查询账号列表
    PrepareStatement(LOGIN_SEL_ACCOUNT_LIST_BY_EMAIL, "SELECT id, username FROM account WHERE email = ?", CONNECTION_SYNCH);

    // 查询账号在各服务器的角色数量
    PrepareStatement(LOGIN_SEL_REALM_CHARACTER_COUNTS, "SELECT realmid, numchars FROM realmcharacters WHERE  acctid = ?", CONNECTION_ASYNC);

    // 按IP查询使用该IP的账号列表
    PrepareStatement(LOGIN_SEL_ACCOUNT_BY_IP, "SELECT id, username FROM account WHERE last_ip = ?", CONNECTION_SYNCH);

    // 按ID检查账号是否存在
    PrepareStatement(LOGIN_SEL_ACCOUNT_BY_ID, "SELECT 1 FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 添加IP封禁记录
    PrepareStatement(LOGIN_INS_IP_BANNED, "INSERT INTO ip_banned (ip, bandate, unbandate, bannedby, banreason) VALUES (?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP()+?, ?, ?)", CONNECTION_ASYNC);

    // 移除IP封禁
    PrepareStatement(LOGIN_DEL_IP_NOT_BANNED, "DELETE FROM ip_banned WHERE ip = ?", CONNECTION_ASYNC);

    // 添加账号封禁记录
    PrepareStatement(LOGIN_INS_ACCOUNT_BANNED, "INSERT INTO account_banned (id, bandate, unbandate, bannedby, banreason, active) VALUES (?, UNIX_TIMESTAMP(), UNIX_TIMESTAMP()+?, ?, ?, 1)", CONNECTION_ASYNC);

    // 解除账号封禁（设置active=0）
    PrepareStatement(LOGIN_UPD_ACCOUNT_NOT_BANNED, "UPDATE account_banned SET active = 0 WHERE id = ? AND active != 0", CONNECTION_ASYNC);

    // 删除服务器角色计数记录
    PrepareStatement(LOGIN_DEL_REALM_CHARACTERS, "DELETE FROM realmcharacters WHERE acctid = ?", CONNECTION_ASYNC);

    // 更新服务器角色计数（使用REPLACE实现插入或更新）
    PrepareStatement(LOGIN_REP_REALM_CHARACTERS, "REPLACE INTO realmcharacters (numchars, acctid, realmid) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    // 查询账号总角色数
    PrepareStatement(LOGIN_SEL_SUM_REALM_CHARACTERS, "SELECT SUM(numchars) FROM realmcharacters WHERE acctid = ?", CONNECTION_ASYNC);

    // 创建新账号
    // 插入用户名、salt、verifier、注册邮箱、当前邮箱和创建时间
    PrepareStatement(LOGIN_INS_ACCOUNT, "INSERT INTO account(username, salt, verifier, reg_mail, email, joindate) VALUES(?, ?, ?, ?, ?, NOW())", CONNECTION_SYNCH);

    // 初始化所有服务器的角色计数为0
    // 为新账号或缺失角色计数的服务器补充记录
    PrepareStatement(LOGIN_INS_REALM_CHARACTERS_INIT, "INSERT INTO realmcharacters (realmid, acctid, numchars) SELECT realmlist.id, account.id, 0 FROM realmlist, account LEFT JOIN realmcharacters ON acctid = account.id WHERE acctid IS NULL", CONNECTION_ASYNC);

    // 更新账号扩展版本
    PrepareStatement(LOGIN_UPD_EXPANSION, "UPDATE account SET expansion = ? WHERE id = ?", CONNECTION_ASYNC);

    // 锁定/解锁账号
    PrepareStatement(LOGIN_UPD_ACCOUNT_LOCK, "UPDATE account SET locked = ? WHERE id = ?", CONNECTION_ASYNC);

    // 设置账号国家锁定
    PrepareStatement(LOGIN_UPD_ACCOUNT_LOCK_COUNTRY, "UPDATE account SET lock_country = ? WHERE id = ?", CONNECTION_ASYNC);

    // 插入日志记录
    PrepareStatement(LOGIN_INS_LOG, "INSERT INTO logs (time, realm, type, level, string) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    // 更新用户名
    PrepareStatement(LOGIN_UPD_USERNAME, "UPDATE account SET username = ? WHERE id = ?", CONNECTION_ASYNC);

    // 更新邮箱
    PrepareStatement(LOGIN_UPD_EMAIL, "UPDATE account SET email = ? WHERE id = ?", CONNECTION_ASYNC);

    // 更新注册邮箱
    PrepareStatement(LOGIN_UPD_REG_EMAIL, "UPDATE account SET reg_mail = ? WHERE id = ?", CONNECTION_ASYNC);

    // 更新静音时间和原因
    PrepareStatement(LOGIN_UPD_MUTE_TIME, "UPDATE account SET mutetime = ? , mutereason = ? , muteby = ? WHERE id = ?", CONNECTION_ASYNC);

    // 更新静音时间（登录时简化版本）
    PrepareStatement(LOGIN_UPD_MUTE_TIME_LOGIN, "UPDATE account SET mutetime = ? WHERE id = ?", CONNECTION_ASYNC);

    // 更新最后登录IP
    PrepareStatement(LOGIN_UPD_LAST_IP, "UPDATE account SET last_ip = ? WHERE username = ?", CONNECTION_ASYNC);

    // 更新最后尝试登录IP
    PrepareStatement(LOGIN_UPD_LAST_ATTEMPT_IP, "UPDATE account SET last_attempt_ip = ? WHERE username = ?", CONNECTION_ASYNC);

    // 设置账号在线状态
    PrepareStatement(LOGIN_UPD_ACCOUNT_ONLINE, "UPDATE account SET online = 1 WHERE id = ?", CONNECTION_ASYNC);

    // 更新服务器运行时间和最大在线人数
    PrepareStatement(LOGIN_UPD_UPTIME_PLAYERS, "UPDATE uptime SET uptime = ?, maxplayers = ? WHERE realmid = ? AND starttime = ?", CONNECTION_ASYNC);

    // 删除过期日志
    PrepareStatement(LOGIN_DEL_OLD_LOGS, "DELETE FROM logs WHERE (time + ?) < ? AND realm = ?", CONNECTION_ASYNC);

    // ==================== 权限管理 ====================
    // 删除账号所有权限记录
    PrepareStatement(LOGIN_DEL_ACCOUNT_ACCESS, "DELETE FROM account_access WHERE AccountID = ?", CONNECTION_ASYNC);

    // 删除账号在指定服务器（或全局）的权限
    PrepareStatement(LOGIN_DEL_ACCOUNT_ACCESS_BY_REALM, "DELETE FROM account_access WHERE AccountID = ? AND (RealmID = ? OR RealmID = -1)", CONNECTION_ASYNC);

    // 添加账号权限
    PrepareStatement(LOGIN_INS_ACCOUNT_ACCESS, "INSERT INTO account_access (AccountID, SecurityLevel, RealmID) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    // 按用户名查询账号ID
    PrepareStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME, "SELECT id FROM account WHERE username = ?", CONNECTION_SYNCH);

    // 查询账号在指定服务器的GM等级
    // RealmID = -1 表示全局权限
    // ORDER BY RealmID DESC 确保服务器特定权限优先于全局权限
    PrepareStatement(LOGIN_GET_GMLEVEL_BY_REALMID, "SELECT SecurityLevel FROM account_access WHERE AccountID = ? AND (RealmID = ? OR RealmID = -1) ORDER BY RealmID DESC", CONNECTION_BOTH);

    // 按ID查询用户名
    PrepareStatement(LOGIN_GET_USERNAME_BY_ID, "SELECT username FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 验证密码（按账号ID）
    PrepareStatement(LOGIN_SEL_CHECK_PASSWORD, "SELECT salt, verifier FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 验证密码（按用户名）
    PrepareStatement(LOGIN_SEL_CHECK_PASSWORD_BY_NAME, "SELECT salt, verifier FROM account WHERE username = ?", CONNECTION_SYNCH);

    // ==================== GM工具相关 ====================
    // 查询账号详细信息（PINFO命令）
    // 包含用户名、GM等级、邮箱、最后IP、登录时间、静音信息等
    PrepareStatement(LOGIN_SEL_PINFO, "SELECT a.username, aa.SecurityLevel, a.email, a.reg_mail, a.last_ip, DATE_FORMAT(a.last_login, '%Y-%m-%d %T'), a.mutetime, a.mutereason, a.muteby, a.failed_logins, a.locked, a.OS FROM account a LEFT JOIN account_access aa ON (a.id = aa.AccountID AND (aa.RealmID = ? OR aa.RealmID = -1)) WHERE a.id = ?", CONNECTION_SYNCH);

    // 查询账号封禁历史（PINFO命令）
    PrepareStatement(LOGIN_SEL_PINFO_BANS, "SELECT unbandate, bandate = unbandate, bannedby, banreason FROM account_banned WHERE id = ? AND active ORDER BY bandate ASC LIMIT 1", CONNECTION_SYNCH);

    // 查询指定安全等级以上的GM账号
    PrepareStatement(LOGIN_SEL_GM_ACCOUNTS, "SELECT a.username, aa.SecurityLevel FROM account a, account_access aa WHERE a.id = aa.AccountID AND aa.SecurityLevel >= ? AND (aa.RealmID = -1 OR aa.RealmID = ?)", CONNECTION_SYNCH);

    // 查询账号基本信息
    PrepareStatement(LOGIN_SEL_ACCOUNT_INFO, "SELECT a.username, a.last_ip, aa.SecurityLevel, a.expansion FROM account a LEFT JOIN account_access aa ON a.id = aa.AccountID WHERE a.id = ?", CONNECTION_SYNCH);

    // 检查账号是否有指定等级以上权限
    PrepareStatement(LOGIN_SEL_ACCOUNT_ACCESS_SECLEVEL_TEST, "SELECT 1 FROM account_access WHERE AccountID = ? AND SecurityLevel > ?", CONNECTION_SYNCH);

    // 查询账号权限信息
    PrepareStatement(LOGIN_SEL_ACCOUNT_ACCESS, "SELECT a.id, aa.SecurityLevel, aa.RealmID FROM account a LEFT JOIN account_access aa ON a.id = aa.AccountID WHERE a.username = ?", CONNECTION_SYNCH);

    // WHOIS查询账号信息
    PrepareStatement(LOGIN_SEL_ACCOUNT_WHOIS, "SELECT username, email, last_ip FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 查询最后尝试登录IP
    PrepareStatement(LOGIN_SEL_LAST_ATTEMPT_IP, "SELECT last_attempt_ip FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 查询最后登录IP
    PrepareStatement(LOGIN_SEL_LAST_IP, "SELECT last_ip FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 查询服务器所需安全等级
    PrepareStatement(LOGIN_SEL_REALMLIST_SECURITY_LEVEL, "SELECT allowedSecurityLevel from realmlist WHERE id = ?", CONNECTION_SYNCH);

    // 删除账号
    PrepareStatement(LOGIN_DEL_ACCOUNT, "DELETE FROM account WHERE id = ?", CONNECTION_ASYNC);

    // 查询自动广播消息
    // realmid = -1 表示全局广播
    PrepareStatement(LOGIN_SEL_AUTOBROADCAST, "SELECT id, weight, text FROM autobroadcast WHERE realmid = ? OR realmid = -1", CONNECTION_SYNCH);

    // 按ID查询邮箱
    PrepareStatement(LOGIN_GET_EMAIL_BY_ID, "SELECT email FROM account WHERE id = ?", CONNECTION_SYNCH);

    // ==================== IP日志记录 ====================
    // 记录账号登录删除相关IP日志
    // 完整名称: Login_Insert_AccountLoginDeLete_IP_Logging
    // 参数: account_id, character_guid, realm_id, type, account_id(用于子查询), systemnote
    PrepareStatement(LOGIN_INS_ALDL_IP_LOGGING, "INSERT INTO logs_ip_actions (account_id, character_guid, realm_id, type, ip, systemnote, unixtime, time) VALUES (?, ?, ?, ?, (SELECT last_ip FROM account WHERE id = ?), ?, unix_timestamp(NOW()), NOW())", CONNECTION_ASYNC);

    // 记录登录失败IP日志
    // 完整名称: Login_Insert_FailedAccountLogin_IP_Logging
    // 参数: account_id, character_guid, realm_id, type, account_id(用于子查询), systemnote
    PrepareStatement(LOGIN_INS_FACL_IP_LOGGING, "INSERT INTO logs_ip_actions (account_id, character_guid, realm_id, type, ip, systemnote, unixtime, time) VALUES (?, ?, ?, ?, (SELECT last_attempt_ip FROM account WHERE id = ?), ?, unix_timestamp(NOW()), NOW())", CONNECTION_ASYNC);

    // 记录角色删除IP日志
    // 完整名称: Login_Insert_CharacterDelete_IP_Logging
    // 参数: account_id, character_guid, realm_id, type, ip, systemnote
    PrepareStatement(LOGIN_INS_CHAR_IP_LOGGING, "INSERT INTO logs_ip_actions (account_id, character_guid, realm_id, type, ip, systemnote, unixtime, time) VALUES (?, ?, ?, ?, ?, ?, unix_timestamp(NOW()), NOW())", CONNECTION_ASYNC);

    // 记录密码错误导致的登录失败IP日志
    // 完整名称: Login_Insert_Failed_Account_Login_due_password_IP_Logging
    // 参数: account_id, ip, systemnote
    PrepareStatement(LOGIN_INS_FALP_IP_LOGGING, "INSERT INTO logs_ip_actions (account_id, character_guid, realm_id, type, ip, systemnote, unixtime, time) VALUES (?, 0, 0, 1, ?, ?, unix_timestamp(NOW()), NOW())", CONNECTION_ASYNC);

    // ==================== RBAC权限系统 ====================
    // 查询账号权限（按ID和服务器）
    PrepareStatement(LOGIN_SEL_ACCOUNT_ACCESS_BY_ID, "SELECT SecurityLevel, RealmID FROM account_access WHERE AccountID = ? and (RealmID = ? OR RealmID = -1) ORDER BY SecurityLevel desc", CONNECTION_SYNCH);

    // 查询账号RBAC权限列表
    // 返回权限ID、是否授予、服务器ID
    // 按权限ID和服务器ID排序
    PrepareStatement(LOGIN_SEL_RBAC_ACCOUNT_PERMISSIONS, "SELECT permissionId, granted FROM rbac_account_permissions WHERE accountId = ? AND (realmId = ? OR realmId = -1) ORDER BY permissionId, realmId", CONNECTION_BOTH);

    // 添加或更新RBAC权限
    // 使用 ON DUPLICATE KEY UPDATE 实现插入或更新
    PrepareStatement(LOGIN_INS_RBAC_ACCOUNT_PERMISSION, "INSERT INTO rbac_account_permissions (accountId, permissionId, granted, realmId) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE granted = VALUES(granted)", CONNECTION_ASYNC);

    // 删除RBAC权限
    PrepareStatement(LOGIN_DEL_RBAC_ACCOUNT_PERMISSION, "DELETE FROM rbac_account_permissions WHERE accountId = ? AND permissionId = ? AND (realmId = ? OR realmId = -1)", CONNECTION_ASYNC);

    // ==================== 静音管理 ====================
    // 添加静音记录
    PrepareStatement(LOGIN_INS_ACCOUNT_MUTE, "INSERT INTO account_muted VALUES (?, UNIX_TIMESTAMP(), ?, ?, ?)", CONNECTION_ASYNC);

    // 查询静音历史
    PrepareStatement(LOGIN_SEL_ACCOUNT_MUTE_INFO, "SELECT mutedate, mutetime, mutereason, mutedby FROM account_muted WHERE guid = ? ORDER BY mutedate ASC", CONNECTION_SYNCH);

    // 删除静音记录
    PrepareStatement(LOGIN_DEL_ACCOUNT_MUTED, "DELETE FROM account_muted WHERE guid = ?", CONNECTION_ASYNC);

    // ==================== 安全验证 ====================
    // 查询安全摘要（用于密码恢复等）
    PrepareStatement(LOGIN_SEL_SECRET_DIGEST, "SELECT digest FROM secret_digest WHERE id = ?", CONNECTION_SYNCH);

    // 插入安全摘要
    PrepareStatement(LOGIN_INS_SECRET_DIGEST, "INSERT INTO secret_digest (id, digest) VALUES (?,?)", CONNECTION_ASYNC);

    // 删除安全摘要
    PrepareStatement(LOGIN_DEL_SECRET_DIGEST, "DELETE FROM secret_digest WHERE id = ?", CONNECTION_ASYNC);

    // 查询TOTP密钥（双因素认证）
    PrepareStatement(LOGIN_SEL_ACCOUNT_TOTP_SECRET, "SELECT totp_secret FROM account WHERE id = ?", CONNECTION_SYNCH);

    // 更新TOTP密钥
    PrepareStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET, "UPDATE account SET totp_secret = ? WHERE id = ?", CONNECTION_ASYNC);
}

/**
 * @brief 同步连接构造函数
 *
 * 创建同步执行的数据库连接。
 * 同步连接直接在调用线程执行SQL操作，阻塞直到操作完成。
 *
 * @param connInfo 数据库连接配置信息
 */
LoginDatabaseConnection::LoginDatabaseConnection(MySQLConnectionInfo& connInfo) : MySQLConnection(connInfo)
{
}

/**
 * @brief 异步连接构造函数
 *
 * 创建异步执行的数据库连接。
 * 异步连接通过队列接收操作请求，在工作线程中执行。
 *
 * @param q SQL操作队列（生产者-消费者队列）
 * @param connInfo 数据库连接配置信息
 */
LoginDatabaseConnection::LoginDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo) : MySQLConnection(q, connInfo)
{
}

/**
 * @brief 析构函数
 *
 * 清理数据库连接资源。
 * 基类 MySQLConnection 会自动关闭连接和清理预编译语句。
 */
LoginDatabaseConnection::~LoginDatabaseConnection()
{
}
