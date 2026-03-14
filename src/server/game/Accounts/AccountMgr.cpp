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
 * @file AccountMgr.cpp
 * @brief 账户管理器实现
 *
 * 本文件实现了账户管理器的所有核心功能：
 * - 账户的创建、删除和修改操作
 * - 使用SRP6协议进行安全的密码认证
 * - RBAC权限系统的加载和管理
 *
 * 主要处理流程：
 * 1. 服务器启动时加载所有RBAC权限定义和关联关系
 * 2. 玩家登录时验证账户信息和权限
 * 3. 管理操作时更新账户数据
 */

#include "AccountMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "CryptoHash.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Realm.h"
#include "ScriptMgr.h"
#include "SRP6.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

/**
 * @brief 构造函数
 */
AccountMgr::AccountMgr() { }

/**
 * @brief 析构函数，清理RBAC权限数据
 */
AccountMgr::~AccountMgr()
{
    ClearRBAC();
}

/**
 * @brief 获取单例实例
 * @return AccountMgr单例指针
 */
AccountMgr* AccountMgr::instance()
{
    static AccountMgr instance;
    return &instance;
}

/**
 * @brief 创建新账户
 * @param username 用户名
 * @param password 密码
 * @param email 邮箱
 * @return 操作结果
 *
 * 处理流程：
 * 1. 验证用户名长度（不超过16字符）
 * 2. 验证密码长度（不超过16字符）
 * 3. 转换为大写（仅拉丁字符）
 * 4. 检查用户名是否已存在
 * 5. 使用SRP6算法生成salt和verifier
 * 6. 插入账户记录到数据库
 * 7. 初始化该账户在各领域的角色计数记录
 */
AccountOpResult AccountMgr::CreateAccount(std::string username, std::string password, std::string email /*= ""*/)
{
    // 验证用户名长度
    if (utf8length(username) > MAX_ACCOUNT_STR)
        return AccountOpResult::AOR_NAME_TOO_LONG;                           // 用户名过长

    // 验证密码长度
    if (utf8length(password) > MAX_PASS_STR)
        return AccountOpResult::AOR_PASS_TOO_LONG;                           // 密码过长

    // 将用户名、密码、邮箱转换为大写（仅处理拉丁字符）
    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(password);
    Utf8ToUpperOnlyLatin(email);

    // 检查用户名是否已存在
    if (GetId(username))
        return AccountOpResult::AOR_NAME_ALREADY_EXIST;                       // 用户名已存在

    // 准备插入账户的SQL语句
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_ACCOUNT);

    stmt->setString(0, username);
    // 使用SRP6算法生成认证数据：salt和verifier
    auto [salt, verifier] = Trinity::Crypto::SRP6::MakeRegistrationData(username, password);
    stmt->setBinary(1, salt);
    stmt->setBinary(2, verifier);
    stmt->setString(3, email);
    stmt->setString(4, email);

    // 直接执行插入，确保账户创建成功，否则后续添加权限组会失败
    LoginDatabase.DirectExecute(stmt);

    // 初始化该账户在各领域的角色计数记录
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_REALM_CHARACTERS_INIT);
    LoginDatabase.Execute(stmt);

    return AccountOpResult::AOR_OK;                                          // 操作成功
}

/**
 * @brief 删除账户
 * @param accountId 账户ID
 * @return 操作结果
 *
 * 处理流程：
 * 1. 检查账户是否存在
 * 2. 获取该账户的所有角色
 * 3. 对每个在线角色：踢出玩家并登出
 * 4. 删除角色数据（包括教程、账户数据、封禁记录等）
 * 5. 使用事务删除登录数据库中的账户相关记录
 */
AccountOpResult AccountMgr::DeleteAccount(uint32 accountId)
{
    // 检查账户是否存在
    LoginDatabasePreparedStatement* loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_ID);
    loginStmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(loginStmt);

    if (!result)
        return AccountOpResult::AOR_NAME_NOT_EXIST;

    // 获取该账户的所有角色
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);

    stmt->setUInt32(0, accountId);

    result = CharacterDatabase.Query(stmt);

    if (result)
    {
        do
        {
            // 构建角色GUID
            ObjectGuid guid(HighGuid::Player, (*result)[0].GetUInt32());

            // 如果玩家在线，则踢出
            if (Player* p = ObjectAccessor::FindConnectedPlayer(guid))
            {
                WorldSession* s = p->GetSession();
                // 标记会话为待移除状态
                s->KickPlayer("AccountMgr::DeleteAccount Deleting the account");
                // 立即登出玩家，不等待下次会话列表更新
                s->LogoutPlayer(false);
            }

            // 从数据库删除角色数据，不需要更新领域角色计数（因为账户整体删除）
            Player::DeleteFromDB(guid, accountId, false);
        } while (result->NextRow());
    }

    // 删除账户相关的教程数据（领域特定但账户下所有角色共享）
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_TUTORIALS);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    // 删除账户数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ACCOUNT_DATA);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    // 删除角色封禁记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_BAN);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    // 使用事务删除登录数据库中的账户相关记录
    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();

    // 删除账户主记录
    loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT);
    loginStmt->setUInt32(0, accountId);
    trans->Append(loginStmt);

    // 删除账户访问权限记录
    loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS);
    loginStmt->setUInt32(0, accountId);
    trans->Append(loginStmt);

    // 删除领域角色计数记录
    loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_REALM_CHARACTERS);
    loginStmt->setUInt32(0, accountId);
    trans->Append(loginStmt);

    // 删除账户封禁记录
    loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_BANNED);
    loginStmt->setUInt32(0, accountId);
    trans->Append(loginStmt);

    // 删除账户禁言记录
    loginStmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_MUTED);
    loginStmt->setUInt32(0, accountId);
    trans->Append(loginStmt);

    // 提交事务
    LoginDatabase.CommitTransaction(trans);

    return AccountOpResult::AOR_OK;
}

/**
 * @brief 修改用户名和密码
 * @param accountId 账户ID
 * @param newUsername 新用户名
 * @param newPassword 新密码
 * @return 操作结果
 *
 * 处理流程：
 * 1. 检查账户是否存在
 * 2. 验证新用户名和密码长度
 * 3. 更新用户名
 * 4. 使用SRP6算法重新生成认证数据
 */
AccountOpResult AccountMgr::ChangeUsername(uint32 accountId, std::string newUsername, std::string newPassword)
{
    // 检查账户是否存在
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
        return AccountOpResult::AOR_NAME_NOT_EXIST;

    // 验证新用户名长度
    if (utf8length(newUsername) > MAX_ACCOUNT_STR)
        return AccountOpResult::AOR_NAME_TOO_LONG;

    // 验证新密码长度
    if (utf8length(newPassword) > MAX_PASS_STR)
        return AccountOpResult::AOR_PASS_TOO_LONG;

    // 转换为大写
    Utf8ToUpperOnlyLatin(newUsername);
    Utf8ToUpperOnlyLatin(newPassword);

    // 更新用户名
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_USERNAME);
    stmt->setString(0, newUsername);
    stmt->setUInt32(1, accountId);
    LoginDatabase.Execute(stmt);

    // 使用新用户名和密码重新生成SRP6认证数据
    auto [salt, verifier] = Trinity::Crypto::SRP6::MakeRegistrationData(newUsername, newPassword);
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LOGON);
    stmt->setBinary(0, salt);
    stmt->setBinary(1, verifier);
    stmt->setUInt32(2, accountId);
    LoginDatabase.Execute(stmt);

    return AccountOpResult::AOR_OK;
}

/**
 * @brief 修改密码
 * @param accountId 账户ID
 * @param newPassword 新密码
 * @return 操作结果
 *
 * 处理流程：
 * 1. 获取账户用户名
 * 2. 验证新密码长度
 * 3. 使用SRP6算法重新生成认证数据
 * 4. 触发脚本回调
 */
AccountOpResult AccountMgr::ChangePassword(uint32 accountId, std::string newPassword)
{
    std::string username;

    // 获取账户用户名
    if (!GetName(accountId, username))
    {
        // 触发密码修改失败脚本回调
        sScriptMgr->OnFailedPasswordChange(accountId);
        return AccountOpResult::AOR_NAME_NOT_EXIST;                          // 账户不存在
    }

    // 验证新密码长度
    if (utf8length(newPassword) > MAX_PASS_STR)
    {
        sScriptMgr->OnFailedPasswordChange(accountId);
        return AccountOpResult::AOR_PASS_TOO_LONG;
    }

    // 转换为大写
    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(newPassword);
    // 使用SRP6算法生成新的salt和verifier
    auto [salt, verifier] = Trinity::Crypto::SRP6::MakeRegistrationData(username, newPassword);

    // 更新数据库中的认证数据
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LOGON);
    stmt->setBinary(0, salt);
    stmt->setBinary(1, verifier);
    stmt->setUInt32(2, accountId);;
    LoginDatabase.Execute(stmt);

    // 触发密码修改成功脚本回调
    sScriptMgr->OnPasswordChange(accountId);
    return AccountOpResult::AOR_OK;
}

/**
 * @brief 修改邮箱
 * @param accountId 账户ID
 * @param newEmail 新邮箱
 * @return 操作结果
 */
AccountOpResult AccountMgr::ChangeEmail(uint32 accountId, std::string newEmail)
{
    std::string username;

    if (!GetName(accountId, username))
    {
        sScriptMgr->OnFailedEmailChange(accountId);
        return AccountOpResult::AOR_NAME_NOT_EXIST;                          // 账户不存在
    }

    // 验证邮箱长度
    if (utf8length(newEmail) > MAX_EMAIL_STR)
    {
        sScriptMgr->OnFailedEmailChange(accountId);
        return AccountOpResult::AOR_EMAIL_TOO_LONG;
    }

    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(newEmail);

    // 更新邮箱
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_EMAIL);

    stmt->setString(0, newEmail);
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    // 触发邮箱修改成功脚本回调
    sScriptMgr->OnEmailChange(accountId);
    return AccountOpResult::AOR_OK;
}

/**
 * @brief 修改注册邮箱
 * @param accountId 账户ID
 * @param newEmail 新邮箱
 * @return 操作结果
 */
AccountOpResult AccountMgr::ChangeRegEmail(uint32 accountId, std::string newEmail)
{
    std::string username;

    if (!GetName(accountId, username))
    {
        sScriptMgr->OnFailedEmailChange(accountId);
        return AccountOpResult::AOR_NAME_NOT_EXIST;                          // 账户不存在
    }

    // 验证邮箱长度
    if (utf8length(newEmail) > MAX_EMAIL_STR)
    {
        sScriptMgr->OnFailedEmailChange(accountId);
        return AccountOpResult::AOR_EMAIL_TOO_LONG;
    }

    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(newEmail);

    // 更新注册邮箱
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_REG_EMAIL);

    stmt->setString(0, newEmail);
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    // 触发邮箱修改成功脚本回调
    sScriptMgr->OnEmailChange(accountId);
    return AccountOpResult::AOR_OK;
}

/**
 * @brief 根据用户名获取账户ID
 * @param username 用户名
 * @return 账户ID，不存在返回0
 */
uint32 AccountMgr::GetId(std::string_view username)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME);
    stmt->setStringView(0, username);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return (result) ? (*result)[0].GetUInt32() : 0;
}

/**
 * @brief 获取账户安全等级（同步）
 * @param accountId 账户ID
 * @param realmId 领域ID，-1表示所有领域
 * @return 安全等级（GM等级），不存在返回SEC_PLAYER
 */
uint32 AccountMgr::GetSecurity(uint32 accountId, int32 realmId)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_GMLEVEL_BY_REALMID);
    stmt->setUInt32(0, accountId);
    stmt->setInt32(1, realmId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return (result) ? (*result)[0].GetUInt8() : uint32(SEC_PLAYER);
}

/**
 * @brief 获取账户安全等级（异步）
 * @param accountId 账户ID
 * @param realmId 领域ID，-1表示所有领域
 * @param callback 回调函数，接收安全等级参数
 * @return 查询回调对象
 */
QueryCallback AccountMgr::GetSecurityAsync(uint32 accountId, int32 realmId, std::function<void(uint32)> callback)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_GMLEVEL_BY_REALMID);
    stmt->setUInt32(0, accountId);
    stmt->setInt32(1, realmId);
    return LoginDatabase.AsyncQuery(stmt).WithPreparedCallback([callback = std::move(callback)](PreparedQueryResult result)
    {
        callback(result ? uint32((*result)[0].GetUInt8()) : uint32(SEC_PLAYER));
    });
}

/**
 * @brief 根据账户ID获取用户名
 * @param accountId 账户ID
 * @param name [out] 输出用户名
 * @return 是否成功获取
 */
bool AccountMgr::GetName(uint32 accountId, std::string& name)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_USERNAME_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
    {
        name = (*result)[0].GetString();
        return true;
    }

    return false;
}

/**
 * @brief 获取账户邮箱
 * @param accountId 账户ID
 * @param email [out] 输出邮箱
 * @return 是否成功获取
 */
bool AccountMgr::GetEmail(uint32 accountId, std::string& email)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_EMAIL_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
    {
        email = (*result)[0].GetString();
        return true;
    }

    return false;
}

/**
 * @brief 验证密码
 * @param accountId 账户ID
 * @param password 待验证的密码
 * @return 密码是否正确
 *
 * 使用SRP6协议验证密码，从数据库获取salt和verifier进行比对
 */
bool AccountMgr::CheckPassword(uint32 accountId, std::string password)
{
    std::string username;

    // 获取用户名
    if (!GetName(accountId, username))
        return false;

    Utf8ToUpperOnlyLatin(username);
    Utf8ToUpperOnlyLatin(password);

    // 查询账户的认证数据
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CHECK_PASSWORD);
    stmt->setUInt32(0, accountId);

    if (PreparedQueryResult result = LoginDatabase.Query(stmt))
    {
        // 获取存储的salt和verifier
        Trinity::Crypto::SRP6::Salt salt = (*result)[0].GetBinary<Trinity::Crypto::SRP6::SALT_LENGTH>();
        Trinity::Crypto::SRP6::Verifier verifier = (*result)[1].GetBinary<Trinity::Crypto::SRP6::VERIFIER_LENGTH>();
        // 使用SRP6算法验证密码
        if (Trinity::Crypto::SRP6::CheckLogin(username, password, salt, verifier))
            return true;
    }

    return false;
}

/**
 * @brief 验证邮箱
 * @param accountId 账户ID
 * @param newEmail 待验证的邮箱
 * @return 邮箱是否匹配
 */
bool AccountMgr::CheckEmail(uint32 accountId, std::string newEmail)
{
    std::string oldEmail;

    // 如果邮箱不存在，直接返回false
    if (!GetEmail(accountId, oldEmail))
        return false;

    Utf8ToUpperOnlyLatin(oldEmail);
    Utf8ToUpperOnlyLatin(newEmail);

    // 比较邮箱（区分大小写）
    if (strcmp(oldEmail.c_str(), newEmail.c_str()) == 0)
        return true;

    return false;
}

/**
 * @brief 获取账户下的角色数量
 * @param accountId 账户ID
 * @return 角色数量
 */
uint32 AccountMgr::GetCharactersCount(uint32 accountId)
{
    // 查询账户的角色计数
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);

    return (result) ? (*result)[0].GetUInt64() : 0;
}

/**
 * @brief 检查账户是否被封禁
 * @param name 用户名
 * @return 是否被封禁
 */
bool AccountMgr::IsBannedAccount(std::string const& name)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BANNED_BY_USERNAME);
    stmt->setString(0, name);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
        return false;

    return true;
}

/**
 * @brief 检查是否为普通玩家账户
 * @param gmlevel GM等级
 * @return 是否为普通玩家
 */
bool AccountMgr::IsPlayerAccount(uint32 gmlevel)
{
    return gmlevel == SEC_PLAYER;
}

/**
 * @brief 检查是否为管理员账户
 * @param gmlevel GM等级
 * @return 是否为管理员（SEC_ADMINISTRATOR到SEC_CONSOLE之间）
 */
bool AccountMgr::IsAdminAccount(uint32 gmlevel)
{
    return gmlevel >= SEC_ADMINISTRATOR && gmlevel <= SEC_CONSOLE;
}

/**
 * @brief 检查是否为控制台账户
 * @param gmlevel GM等级
 * @return 是否为控制台账户
 */
bool AccountMgr::IsConsoleAccount(uint32 gmlevel)
{
    return gmlevel == SEC_CONSOLE;
}

/**
 * @brief 加载RBAC权限数据
 *
 * 调用时机：服务器启动时
 *
 * 处理流程：
 * 1. 清理旧的权限数据
 * 2. 从rbac_permissions表加载权限定义
 * 3. 从rbac_linked_permissions表加载权限关联关系
 * 4. 从rbac_default_permissions表加载默认权限
 *
 * 性能注意事项：一次性加载所有权限到内存
 */
void AccountMgr::LoadRBAC()
{
    // 清理旧的权限数据
    ClearRBAC();

    TC_LOG_DEBUG("rbac", "AccountMgr::LoadRBAC");
    uint32 oldMSTime = getMSTime();
    uint32 count1 = 0;
    uint32 count2 = 0;
    uint32 count3 = 0;

    // 第一步：加载权限定义
    TC_LOG_DEBUG("rbac", "AccountMgr::LoadRBAC: Loading permissions");
    QueryResult result = LoginDatabase.Query("SELECT id, name FROM rbac_permissions");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 account permission definitions. DB table `rbac_permissions` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        // 创建权限对象并添加到容器
        _permissions[id] = new rbac::RBACPermission(id, field[1].GetString());
        ++count1;
    }
    while (result->NextRow());

    // 第二步：加载权限关联关系
    TC_LOG_DEBUG("rbac", "AccountMgr::LoadRBAC: Loading linked permissions");
    result = LoginDatabase.Query("SELECT id, linkedId FROM rbac_linked_permissions ORDER BY id ASC");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 linked permissions. DB table `rbac_linked_permissions` is empty.");
        return;
    }

    uint32 permissionId = 0;
    rbac::RBACPermission* permission = nullptr;

    do
    {
        Field* field = result->Fetch();
        uint32 newId = field[0].GetUInt32();
        // 如果ID变化，更新当前权限对象指针
        if (permissionId != newId)
        {
            permissionId = newId;
            permission = _permissions[newId];
        }

        uint32 linkedPermissionId = field[1].GetUInt32();
        // 检查是否自引用
        if (linkedPermissionId == permissionId)
        {
            TC_LOG_ERROR("sql.sql", "RBAC Permission {} has itself as linked permission. Ignored", permissionId);
            continue;
        }
        // 添加关联权限
        permission->AddLinkedPermission(linkedPermissionId);
        ++count2;
    }
    while (result->NextRow());

    // 第三步：加载默认权限
    TC_LOG_DEBUG("rbac", "AccountMgr::LoadRBAC: Loading default permissions");
    result = LoginDatabase.PQuery("SELECT secId, permissionId FROM rbac_default_permissions WHERE (realmId = {} OR realmId = -1) ORDER BY secId ASC", realm.Id.Realm);
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 default permission definitions. DB table `rbac_default_permissions` is empty.");
        return;
    }

    uint8 secId = 255;
    rbac::RBACPermissionContainer* permissions = nullptr;
    do
    {
        Field* field = result->Fetch();
        uint32 newId = field[0].GetUInt32();
        // 如果安全等级变化，更新当前权限容器指针
        if (secId != newId || permissions == nullptr)
        {
            secId = newId;
            permissions = &_defaultPermissions[secId];
        }

        // 添加默认权限到对应安全等级
        permissions->insert(field[1].GetUInt32());
        ++count3;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} permission definitions, {} linked permissions and {} default permissions in {} ms", count1, count2, count3, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 更新账户访问权限
 * @param rbac RBAC数据对象（可为nullptr）
 * @param accountId 账户ID
 * @param securityLevel 新的安全等级
 * @param realmId 领域ID，-1表示所有领域
 *
 * 处理流程：
 * 1. 更新内存中的RBAC数据（如果提供了rbac对象）
 * 2. 删除旧的权限记录
 * 3. 添加新的权限记录（如果安全等级不为0）
 */
void AccountMgr::UpdateAccountAccess(rbac::RBACData* rbac, uint32 accountId, uint8 securityLevel, int32 realmId)
{
    // 如果提供了RBAC对象且安全等级发生变化，更新内存中的数据
    if (rbac && securityLevel != rbac->GetSecurityLevel())
        rbac->SetSecurityLevel(securityLevel);

    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();

    // 删除旧的安全等级记录
    if (realmId == -1)
    {
        // 删除所有领域的权限记录
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS);
        stmt->setUInt32(0, accountId);
        trans->Append(stmt);
    }
    else
    {
        // 只删除指定领域的权限记录
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS_BY_REALM);
        stmt->setUInt32(0, accountId);
        stmt->setUInt32(1, realmId);
        trans->Append(stmt);
    }

    // 添加新的安全等级（如果安全等级不为0）
    if (securityLevel)
    {
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_ACCOUNT_ACCESS);
        stmt->setUInt32(0, accountId);
        stmt->setUInt8(1, securityLevel);
        stmt->setInt32(2, realmId);
        trans->Append(stmt);
    }

    LoginDatabase.CommitTransaction(trans);
}

/**
 * @brief 获取RBAC权限对象
 * @param permissionId 权限ID
 * @return 权限对象指针，不存在返回nullptr
 */
rbac::RBACPermission const* AccountMgr::GetRBACPermission(uint32 permissionId) const
{
    TC_LOG_TRACE("rbac", "AccountMgr::GetRBACPermission: {}", permissionId);
    rbac::RBACPermissionsContainer::const_iterator it = _permissions.find(permissionId);
    if (it != _permissions.end())
        return it->second;

    return nullptr;
}

/**
 * @brief 检查账户是否拥有指定权限
 * @param accountId 账户ID
 * @param permissionId 权限ID
 * @param realmId 领域ID
 * @return 是否拥有权限
 *
 * 性能注意事项：创建临时RBACData对象并从数据库加载权限，开销较大
 */
bool AccountMgr::HasPermission(uint32 accountId, uint32 permissionId, uint32 realmId)
{
    // 检查账户ID有效性
    if (!accountId)
    {
        TC_LOG_ERROR("rbac", "AccountMgr::HasPermission: Wrong accountId 0");
        return false;
    }

    // 创建临时RBACData对象
    rbac::RBACData rbac(accountId, "", realmId, GetSecurity(accountId, realmId));
    // 从数据库加载权限数据
    rbac.LoadFromDB();
    bool hasPermission = rbac.HasPermission(permissionId);

    TC_LOG_DEBUG("rbac", "AccountMgr::HasPermission [AccountId: {}, PermissionId: {}, realmId: {}]: {}",
                   accountId, permissionId, realmId, hasPermission);
    return hasPermission;
}

/**
 * @brief 清理RBAC权限数据
 *
 * 释放所有权限对象的内存，清空权限容器
 */
void AccountMgr::ClearRBAC()
{
    // 删除所有权限对象
    for (std::pair<uint32 const, rbac::RBACPermission*>& permission : _permissions)
        delete permission.second;

    // 清空容器
    _permissions.clear();
    _defaultPermissions.clear();
}

/**
 * @brief 获取指定安全等级的默认权限
 * @param secLevel 安全等级
 * @return 权限ID集合的常引用
 */
rbac::RBACPermissionContainer const& AccountMgr::GetRBACDefaultPermissions(uint8 secLevel)
{
    TC_LOG_TRACE("rbac", "AccountMgr::GetRBACDefaultPermissions: secLevel {} - size: {}", secLevel, uint32(_defaultPermissions[secLevel].size()));
    return _defaultPermissions[secLevel];
}
