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
 * @file RBAC.cpp
 * @brief 基于角色的访问控制实现
 *
 * 本文件实现了RBAC系统的核心功能：
 * - 权限的授予、拒绝和撤销
 * - 权限的继承和展开
 * - 全局权限的计算
 *
 * 主要处理流程：
 * 1. 从数据库加载账户权限
 * 2. 展开关联权限（处理继承关系）
 * 3. 计算最终的全局权限
 * 4. 提供权限检查接口
 */

#include "RBAC.h"
#include "AccountMgr.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include <sstream>

namespace rbac
{

/**
 * @brief 将权限容器转换为调试字符串
 * @param perms 权限容器
 * @return 权限ID列表字符串（逗号分隔）
 *
 * 用于日志输出和调试
 */
std::string GetDebugPermissionString(RBACPermissionContainer const& perms)
{
    std::string str = "";
    if (!perms.empty())
    {
        std::ostringstream o;
        RBACPermissionContainer::const_iterator itr = perms.begin();
        o << (*itr);
        // 遍历权限容器，生成逗号分隔的字符串
        for (++itr; itr != perms.end(); ++itr)
            o << ", " << uint32(*itr);
        str = o.str();
    }

    return str;
}

/**
 * @brief 授予权限
 * @param permissionId 权限ID
 * @param realmId 领域ID（默认0）
 * @return 操作结果
 *
 * 处理流程：
 * 1. 检查权限ID是否存在
 * 2. 检查权限是否已在拒绝列表中
 * 3. 检查权限是否已授予
 * 4. 添加到授予列表
 * 5. 如果realmId不为0，保存到数据库并重新计算权限
 */
RBACCommandResult RBACData::GrantPermission(uint32 permissionId, int32 realmId /* = 0*/)
{
    // 检查权限ID是否存在
    RBACPermission const* perm = sAccountMgr->GetRBACPermission(permissionId);
    if (!perm)
    {
        TC_LOG_TRACE("rbac", "RBACData::GrantPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission does not exists",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_ID_DOES_NOT_EXISTS;
    }

    // 检查权限是否已在拒绝列表中
    if (HasDeniedPermission(permissionId))
    {
        TC_LOG_TRACE("rbac", "RBACData::GrantPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission in deny list",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_IN_DENIED_LIST;
    }

    // 检查权限是否已授予
    if (HasGrantedPermission(permissionId))
    {
        TC_LOG_TRACE("rbac", "RBACData::GrantPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission already granted",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_CANT_ADD_ALREADY_ADDED;
    }

    // 添加到授予列表
    AddGrantedPermission(permissionId);

    // 如果realmId不为0（非加载时），保存到数据库并重新计算权限
    if (realmId)
    {
        TC_LOG_TRACE("rbac", "RBACData::GrantPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok and DB updated",
                       GetId(), GetName(), permissionId, realmId);
        SavePermission(permissionId, true, realmId);
        CalculateNewPermissions();
    }
    else
        TC_LOG_TRACE("rbac", "RBACData::GrantPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok",
                       GetId(), GetName(), permissionId, realmId);

    return RBAC_OK;
}

/**
 * @brief 拒绝权限
 * @param permissionId 权限ID
 * @param realmId 领域ID（默认0）
 * @return 操作结果
 *
 * 处理流程：
 * 1. 检查权限ID是否存在
 * 2. 检查权限是否已在授予列表中
 * 3. 检查权限是否已拒绝
 * 4. 添加到拒绝列表
 * 5. 如果realmId不为0，保存到数据库并重新计算权限
 */
RBACCommandResult RBACData::DenyPermission(uint32 permissionId, int32 realmId /* = 0*/)
{
    // 检查权限ID是否存在
    RBACPermission const* perm = sAccountMgr->GetRBACPermission(permissionId);
    if (!perm)
    {
        TC_LOG_TRACE("rbac", "RBACData::DenyPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission does not exists",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_ID_DOES_NOT_EXISTS;
    }

    // 检查权限是否已在授予列表中
    if (HasGrantedPermission(permissionId))
    {
        TC_LOG_TRACE("rbac", "RBACData::DenyPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission in grant list",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_IN_GRANTED_LIST;
    }

    // 检查权限是否已拒绝
    if (HasDeniedPermission(permissionId))
    {
        TC_LOG_TRACE("rbac", "RBACData::DenyPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Permission already denied",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_CANT_ADD_ALREADY_ADDED;
    }

    // 添加到拒绝列表
    AddDeniedPermission(permissionId);

    // 如果realmId不为0（非加载时），保存到数据库并重新计算权限
    if (realmId)
    {
        TC_LOG_TRACE("rbac", "RBACData::DenyPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok and DB updated",
                       GetId(), GetName(), permissionId, realmId);
        SavePermission(permissionId, false, realmId);
        CalculateNewPermissions();
    }
    else
        TC_LOG_TRACE("rbac", "RBACData::DenyPermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok",
                       GetId(), GetName(), permissionId, realmId);

    return RBAC_OK;
}

/**
 * @brief 保存权限到数据库
 * @param permission 权限ID
 * @param granted true表示授予，false表示拒绝
 * @param realmId 领域ID
 *
 * 将权限记录插入到rbac_account_permissions表
 */
void RBACData::SavePermission(uint32 permission, bool granted, int32 realmId)
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_RBAC_ACCOUNT_PERMISSION);
    stmt->setUInt32(0, GetId());
    stmt->setUInt32(1, permission);
    stmt->setBool(2, granted);
    stmt->setInt32(3, realmId);
    LoginDatabase.Execute(stmt);
}

/**
 * @brief 撤销权限
 * @param permissionId 权限ID
 * @param realmId 领域ID（默认0）
 * @return 操作结果
 *
 * 处理流程：
 * 1. 检查权限是否在授予或拒绝列表中
 * 2. 从授予和拒绝列表中移除
 * 3. 如果realmId不为0，从数据库删除并重新计算权限
 */
RBACCommandResult RBACData::RevokePermission(uint32 permissionId, int32 realmId /* = 0*/)
{
    // 检查权限是否在授予或拒绝列表中
    if (!HasGrantedPermission(permissionId) && !HasDeniedPermission(permissionId))
    {
        TC_LOG_TRACE("rbac", "RBACData::RevokePermission [Id: {} Name: {}] (Permission {}, RealmId {}). Not granted or revoked",
                       GetId(), GetName(), permissionId, realmId);
        return RBAC_CANT_REVOKE_NOT_IN_LIST;
    }

    // 从授予和拒绝列表中移除
    RemoveGrantedPermission(permissionId);
    RemoveDeniedPermission(permissionId);

    // 如果realmId不为0（非加载时），从数据库删除并重新计算权限
    if (realmId)
    {
        TC_LOG_TRACE("rbac", "RBACData::RevokePermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok and DB updated",
                       GetId(), GetName(), permissionId, realmId);
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_RBAC_ACCOUNT_PERMISSION);
        stmt->setUInt32(0, GetId());
        stmt->setUInt32(1, permissionId);
        stmt->setInt32(2, realmId);
        LoginDatabase.Execute(stmt);

        CalculateNewPermissions();
    }
    else
        TC_LOG_TRACE("rbac", "RBACData::RevokePermission [Id: {} Name: {}] (Permission {}, RealmId {}). Ok",
                       GetId(), GetName(), permissionId, realmId);

    return RBAC_OK;
}

/**
 * @brief 从数据库加载权限（同步）
 *
 * 加载账户的所有权限数据，包括授予和拒绝的权限
 */
void RBACData::LoadFromDB()
{
    ClearData();

    TC_LOG_DEBUG("rbac", "RBACData::LoadFromDB [Id: {} Name: {}]: Loading permissions", GetId(), GetName());
    // 加载影响当前领域的账户权限（授予和拒绝）
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_RBAC_ACCOUNT_PERMISSIONS);
    stmt->setUInt32(0, GetId());
    stmt->setInt32(1, GetRealmId());

    LoadFromDBCallback(LoginDatabase.Query(stmt));
}

/**
 * @brief 从数据库加载权限（异步）
 * @return 查询回调对象
 */
QueryCallback RBACData::LoadFromDBAsync()
{
    ClearData();

    TC_LOG_DEBUG("rbac", "RBACData::LoadFromDB [Id: {} Name: {}]: Loading permissions", GetId(), GetName());
    // 加载影响当前领域的账户权限（授予和拒绝）
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_RBAC_ACCOUNT_PERMISSIONS);
    stmt->setUInt32(0, GetId());
    stmt->setInt32(1, GetRealmId());

    return LoginDatabase.AsyncQuery(stmt);
}

/**
 * @brief 处理数据库加载结果
 * @param result 数据库查询结果
 *
 * 处理流程：
 * 1. 遍历查询结果，根据granted字段决定授予或拒绝
 * 2. 添加基于安全等级的默认权限
 * 3. 计算全局权限
 */
void RBACData::LoadFromDBCallback(PreparedQueryResult result)
{
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            // 根据granted字段判断是授予还是拒绝
            if (fields[1].GetBool())
                GrantPermission(fields[0].GetUInt32());
            else
                DenyPermission(fields[0].GetUInt32());
        } while (result->NextRow());
    }

    // 添加基于安全等级的默认权限
    RBACPermissionContainer const& permissions = sAccountMgr->GetRBACDefaultPermissions(_secLevel);
    for (uint32 permission : permissions)
        GrantPermission(permission);

    // 强制计算全局权限
    CalculateNewPermissions();
}

/**
 * @brief 计算新的全局权限
 *
 * 处理流程：
 * 1. 获取授予权限列表
 * 2. 展开授予权限（包含所有关联权限）
 * 3. 获取拒绝权限列表
 * 4. 展开拒绝权限（包含所有关联权限）
 * 5. 从授予权限中移除拒绝权限，得到最终的全局权限
 *
 * 计算公式：全局权限 = 授予权限 - 拒绝权限
 */
void RBACData::CalculateNewPermissions()
{
    TC_LOG_TRACE("rbac", "RBACData::CalculateNewPermissions [Id: {} Name: {}]", GetId(), GetName());

    // 获取授予权限列表
    _globalPerms = GetGrantedPermissions();
    // 展开授予权限（包含所有关联权限）
    ExpandPermissions(_globalPerms);

    // 获取拒绝权限列表
    RBACPermissionContainer revoked = GetDeniedPermissions();
    // 展开拒绝权限（包含所有关联权限）
    ExpandPermissions(revoked);

    // 从授予权限中移除拒绝权限
    RemovePermissions(_globalPerms, revoked);
}

/**
 * @brief 将权限列表添加到另一个列表
 * @param permsFrom 源权限列表
 * @param permsTo 目标权限列表
 */
void RBACData::AddPermissions(RBACPermissionContainer const& permsFrom, RBACPermissionContainer& permsTo)
{
    for (uint32 permission : permsFrom)
        permsTo.insert(permission);
}

/**
 * @brief 从权限列表中移除另一个列表的权限
 * @param permsFrom 源权限列表（会被修改）
 * @param permsToRemove 要移除的权限列表
 */
void RBACData::RemovePermissions(RBACPermissionContainer& permsFrom, RBACPermissionContainer const& permsToRemove)
{
    for (uint32 permission: permsToRemove)
        permsFrom.erase(permission);
}

/**
 * @brief 展开权限列表（包含所有关联权限）
 * @param permissions 权限列表（输入输出参数）
 *
 * 算法：
 * 1. 将原始权限列表复制到待检查列表
 * 2. 清空原始列表
 * 3. 循环处理待检查列表：
 *    a. 取出一个权限ID
 *    b. 获取权限对象
 *    c. 将权限ID添加到最终列表
 *    d. 将所有关联权限添加到待检查列表（如果尚未在最终列表中）
 * 4. 最终列表包含所有原始权限及其关联权限
 *
 * 例如：如果权限A关联了权限B和C，展开后将包含A、B、C
 */
void RBACData::ExpandPermissions(RBACPermissionContainer& permissions)
{
    RBACPermissionContainer toCheck = permissions;
    permissions.clear();

    while (!toCheck.empty())
    {
        // 从待检查列表中取出第一个权限
        uint32 permissionId = *toCheck.begin();
        toCheck.erase(toCheck.begin());

        // 获取权限对象
        RBACPermission const* permission = sAccountMgr->GetRBACPermission(permissionId);
        if (!permission)
            continue;

        // 将权限ID添加到最终列表（展开后的列表）
        permissions.insert(permissionId);

        // 将所有关联权限（尚未展开的）添加到待检查列表
        RBACPermissionContainer const& linkedPerms = permission->GetLinkedPermissions();
        for (uint32 linkedPerm : linkedPerms)
            if (permissions.find(linkedPerm) == permissions.end())
                toCheck.insert(linkedPerm);
    }

    TC_LOG_DEBUG("rbac", "RBACData::ExpandPermissions: Expanded: {}", GetDebugPermissionString(permissions));
}

/**
 * @brief 清理权限数据
 *
 * 清空授予、拒绝和全局权限列表
 */
void RBACData::ClearData()
{
    _grantedPerms.clear();
    _deniedPerms.clear();
    _globalPerms.clear();
}

} // namespace rbac
