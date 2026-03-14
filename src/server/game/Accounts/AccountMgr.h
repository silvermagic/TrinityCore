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
 * @file AccountMgr.h
 * @brief 账户管理器模块
 *
 * 本模块负责游戏账户的核心管理功能，包括：
 * - 账户的创建、删除和修改
 * - 账户认证信息管理（密码、邮箱等）
 * - 账户权限和安全等级管理
 * - 基于角色的访问控制（RBAC）权限加载和查询
 *
 * 调用时机：
 * - 服务器启动时加载RBAC权限数据
 * - 用户注册/注销账户
 * - 管理员修改账户信息
 * - 登录验证时查询账户信息
 *
 * 性能注意事项：
 * - RBAC权限数据在服务器启动时一次性加载到内存
 * - 数据库查询使用预编译语句提高效率
 * - 异步查询接口避免阻塞主线程
 */

#ifndef _ACCMGR_H
#define _ACCMGR_H

#include "RBAC.h"

/**
 * @enum AccountOpResult
 * @brief 账户操作结果枚举
 *
 * 定义所有账户操作可能返回的结果状态
 */
enum class AccountOpResult : uint8
{
    AOR_OK,                  ///< 操作成功
    AOR_NAME_TOO_LONG,       ///< 用户名过长
    AOR_PASS_TOO_LONG,       ///< 密码过长
    AOR_EMAIL_TOO_LONG,      ///< 邮箱过长
    AOR_NAME_ALREADY_EXIST,  ///< 用户名已存在
    AOR_NAME_NOT_EXIST,      ///< 用户名不存在
    AOR_DB_INTERNAL_ERROR    ///< 数据库内部错误
};

/**
 * @enum PasswordChangeSecurity
 * @brief 密码修改安全验证方式
 *
 * 定义修改密码时需要的安全验证级别
 */
enum PasswordChangeSecurity
{
    PW_NONE,    ///< 无需验证
    PW_EMAIL,   ///< 需要邮箱验证
    PW_RBAC     ///< 需要RBAC权限验证
};

/// 最大密码长度限制
#define MAX_PASS_STR 16
/// 最大账户名长度限制
#define MAX_ACCOUNT_STR 16
/// 最大邮箱长度限制
#define MAX_EMAIL_STR 64

namespace rbac
{
/// RBAC权限容器：权限ID -> 权限对象指针的映射
typedef std::map<uint32, rbac::RBACPermission*> RBACPermissionsContainer;
/// 默认权限容器：安全等级 -> 权限ID集合的映射
typedef std::map<uint8, rbac::RBACPermissionContainer> RBACDefaultPermissionsContainer;
}

/**
 * @class AccountMgr
 * @brief 账户管理器类（单例模式）
 *
 * 负责管理所有账户相关的操作，包括：
 * - 账户的创建、删除和修改
 * - 账户认证信息（用户名、密码、邮箱）管理
 * - 账户权限和安全等级管理
 * - RBAC权限系统的加载和查询
 *
 * 使用方式：
 * 通过 sAccountMgr 宏访问单例实例
 *
 * 线程安全性：
 * - 大部分方法是静态方法，直接操作数据库
 * - RBAC相关方法需要考虑线程同步
 */
class TC_GAME_API AccountMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        AccountMgr();

        /**
         * @brief 析构函数，清理RBAC权限数据
         */
        ~AccountMgr();

    public:
        /**
         * @brief 获取单例实例
         * @return AccountMgr单例指针
         *
         * 调用时机：需要访问账户管理器时调用
         */
        static AccountMgr* instance();

        /**
         * @brief 创建新账户
         * @param username 用户名
         * @param password 密码
         * @param email 邮箱（可选）
         * @return 操作结果
         *
         * 调用时机：用户注册新账户时
         *
         * 处理流程：
         * 1. 验证用户名和密码长度
         * 2. 检查用户名是否已存在
         * 3. 使用SRP6算法生成认证数据
         * 4. 写入数据库
         *
         * 性能注意事项：直接执行数据库操作，可能阻塞
         */
        AccountOpResult CreateAccount(std::string username, std::string password, std::string email = "");

        /**
         * @brief 删除账户
         * @param accountId 账户ID
         * @return 操作结果
         *
         * 调用时机：管理员删除账户时
         *
         * 处理流程：
         * 1. 检查账户是否存在
         * 2. 获取该账户的所有角色
         * 3. 踢出在线玩家
         * 4. 删除角色数据
         * 5. 删除账户相关数据（教程、账户数据、封禁记录等）
         * 6. 删除登录数据库中的账户记录
         *
         * 性能注意事项：涉及大量数据库删除操作，使用事务
         */
        static AccountOpResult DeleteAccount(uint32 accountId);

        /**
         * @brief 修改用户名和密码
         * @param accountId 账户ID
         * @param newUsername 新用户名
         * @param newPassword 新密码
         * @return 操作结果
         *
         * 调用时机：管理员修改账户信息时
         */
        static AccountOpResult ChangeUsername(uint32 accountId, std::string newUsername, std::string newPassword);

        /**
         * @brief 修改密码
         * @param accountId 账户ID
         * @param newPassword 新密码
         * @return 操作结果
         *
         * 调用时机：用户或管理员修改密码时
         *
         * 处理流程：
         * 1. 验证账户存在
         * 2. 验证密码长度
         * 3. 使用SRP6算法重新生成认证数据
         * 4. 更新数据库
         *
         * 性能注意事项：触发脚本回调
         */
        static AccountOpResult ChangePassword(uint32 accountId, std::string newPassword);

        /**
         * @brief 修改邮箱
         * @param accountId 账户ID
         * @param newEmail 新邮箱
         * @return 操作结果
         *
         * 调用时机：用户修改邮箱时
         */
        static AccountOpResult ChangeEmail(uint32 accountId, std::string newEmail);

        /**
         * @brief 修改注册邮箱
         * @param accountId 账户ID
         * @param newEmail 新邮箱
         * @return 操作结果
         *
         * 调用时机：管理员修改注册邮箱时
         */
        static AccountOpResult ChangeRegEmail(uint32 accountId, std::string newEmail);

        /**
         * @brief 验证密码
         * @param accountId 账户ID
         * @param password 待验证的密码
         * @return 密码是否正确
         *
         * 调用时机：需要验证用户身份时（如修改密码前）
         *
         * 性能注意事项：使用SRP6算法验证，需要数据库查询
         */
        static bool CheckPassword(uint32 accountId, std::string password);

        /**
         * @brief 验证邮箱
         * @param accountId 账户ID
         * @param newEmail 待验证的邮箱
         * @return 邮箱是否匹配
         *
         * 调用时机：需要验证用户邮箱时
         */
        static bool CheckEmail(uint32 accountId, std::string newEmail);

        /**
         * @brief 根据用户名获取账户ID
         * @param username 用户名
         * @return 账户ID，不存在返回0
         *
         * 调用时机：需要根据用户名查找账户时
         *
         * 性能注意事项：执行数据库查询
         */
        static uint32 GetId(std::string_view username);

        /**
         * @brief 获取账户安全等级
         * @param accountId 账户ID
         * @param realmId 领域ID，-1表示所有领域
         * @return 安全等级（GM等级）
         *
         * 调用时机：需要检查账户权限等级时
         *
         * 性能注意事项：同步数据库查询
         */
        static uint32 GetSecurity(uint32 accountId, int32 realmId);

        /**
         * @brief 异步获取账户安全等级
         * @param accountId 账户ID
         * @param realmId 领域ID，-1表示所有领域
         * @param callback 回调函数，接收安全等级参数
         * @return 查询回调对象
         *
         * 调用时机：需要异步查询安全等级时（避免阻塞）
         *
         * 性能注意事项：异步查询，适合在主线程调用
         */
        [[nodiscard]] static QueryCallback GetSecurityAsync(uint32 accountId, int32 realmId, std::function<void(uint32)> callback);

        /**
         * @brief 根据账户ID获取用户名
         * @param accountId 账户ID
         * @param name [out] 输出用户名
         * @return 是否成功获取
         *
         * 调用时机：需要根据ID查询用户名时
         */
        static bool GetName(uint32 accountId, std::string& name);

        /**
         * @brief 获取账户邮箱
         * @param accountId 账户ID
         * @param email [out] 输出邮箱
         * @return 是否成功获取
         *
         * 调用时机：需要获取账户邮箱时
         */
        static bool GetEmail(uint32 accountId, std::string& email);

        /**
         * @brief 获取账户下的角色数量
         * @param accountId 账户ID
         * @return 角色数量
         *
         * 调用时机：需要统计账户角色时
         */
        static uint32 GetCharactersCount(uint32 accountId);

        /**
         * @brief 检查账户是否被封禁
         * @param name 用户名
         * @return 是否被封禁
         *
         * 调用时机：登录验证时检查封禁状态
         */
        static bool IsBannedAccount(std::string const& name);

        /**
         * @brief 检查是否为普通玩家账户
         * @param gmlevel GM等级
         * @return 是否为普通玩家
         *
         * 调用时机：需要判断账户类型时
         */
        static bool IsPlayerAccount(uint32 gmlevel);

        /**
         * @brief 检查是否为管理员账户
         * @param gmlevel GM等级
         * @return 是否为管理员
         *
         * 调用时机：需要判断是否有管理权限时
         */
        static bool IsAdminAccount(uint32 gmlevel);

        /**
         * @brief 检查是否为控制台账户
         * @param gmlevel GM等级
         * @return 是否为控制台账户
         *
         * 调用时机：需要判断是否为控制台操作时
         */
        static bool IsConsoleAccount(uint32 gmlevel);

        /**
         * @brief 检查账户是否拥有指定权限
         * @param accountId 账户ID
         * @param permission 权限ID
         * @param realmId 领域ID
         * @return 是否拥有权限
         *
         * 调用时机：需要验证账户权限时
         *
         * 性能注意事项：创建临时RBACData对象并加载权限，开销较大
         */
        static bool HasPermission(uint32 accountId, uint32 permission, uint32 realmId);

        /**
         * @brief 更新账户访问权限
         * @param rbac RBAC数据对象
         * @param accountId 账户ID
         * @param securityLevel 安全等级
         * @param realmId 领域ID，-1表示所有领域
         *
         * 调用时机：管理员修改账户安全等级时
         *
         * 处理流程：
         * 1. 更新内存中的RBAC数据
         * 2. 删除旧的权限记录
         * 3. 添加新的权限记录
         */
        void UpdateAccountAccess(rbac::RBACData* rbac, uint32 accountId, uint8 securityLevel, int32 realmId);

        /**
         * @brief 加载RBAC权限数据
         *
         * 调用时机：服务器启动时
         *
         * 处理流程：
         * 1. 清理旧的权限数据
         * 2. 从数据库加载权限定义
         * 3. 加载权限关联关系
         * 4. 加载默认权限
         *
         * 性能注意事项：一次性加载所有权限到内存，服务器启动时执行
         */
        void LoadRBAC();

        /**
         * @brief 获取RBAC权限对象
         * @param permissionId 权限ID
         * @return 权限对象指针，不存在返回nullptr
         *
         * 调用时机：需要获取权限详细信息时
         *
         * 性能注意事项：内存查找，速度较快
         */
        rbac::RBACPermission const* GetRBACPermission(uint32 permission) const;

        /**
         * @brief 获取所有RBAC权限列表
         * @return 权限容器常引用
         *
         * 调用时机：需要遍历所有权限时
         */
        rbac::RBACPermissionsContainer const& GetRBACPermissionList() const { return _permissions; }

        /**
         * @brief 获取指定安全等级的默认权限
         * @param secLevel 安全等级
         * @return 权限ID集合
         *
         * 调用时机：创建RBACData时获取默认权限
         */
        rbac::RBACPermissionContainer const& GetRBACDefaultPermissions(uint8 secLevel);

    private:
        /**
         * @brief 清理RBAC权限数据
         *
         * 调用时机：重新加载权限前或析构时
         *
         * 性能注意事项：释放所有权限对象内存
         */
        void ClearRBAC();

        /// 所有RBAC权限定义：权限ID -> 权限对象
        rbac::RBACPermissionsContainer _permissions;
        /// 默认权限：安全等级 -> 权限ID集合
        rbac::RBACDefaultPermissionsContainer _defaultPermissions;
};

#define sAccountMgr AccountMgr::instance()
#endif
