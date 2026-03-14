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
 * @file cs_rbac.cpp
 * @brief 基于角色的访问控制(RBAC)命令脚本模块
 *
 * 本文件实现了所有与RBAC权限系统相关的GM命令，包括：
 * - 授予/撤销/拒绝权限
 * - 查看账户权限列表
 * - 查看系统中所有可用权限
 *
 * RBAC系统用于控制玩家和GM账户对各种命令和功能的访问权限，
 * 提供了细粒度的权限管理机制。
 */

/* ScriptData
Name: rbac_commandscript
%Complete: 100
Comment: All role based access control related commands (including account related)
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Language.h"
#include "Player.h"
#include "Realm.h"
#include "World.h"
#include "WorldSession.h"

/**
 * @struct RBACCommandData
 * @brief RBAC命令数据封装结构体
 *
 * 用于封装RBAC数据对象，管理其生命周期。
 * 当目标账户在线时，使用会话中的RBAC数据；
 * 当目标账户离线时，创建临时的RBAC数据对象并在使用后删除。
 */
struct RBACCommandData
{
    /**
     * @brief 构造函数
     * @param rbac_ RBAC数据指针
     * @param needDelete_ 是否需要在析构时删除RBAC数据对象
     */
    RBACCommandData(rbac::RBACData* rbac_, bool needDelete_) : rbac(rbac_), needDelete(needDelete_) { }

    // 禁止拷贝构造，防止重复删除资源
    RBACCommandData(RBACCommandData const&) = delete;

    /**
     * @brief 析构函数
     *
     * 如果needDelete为true，则删除RBAC数据对象。
     * 这用于清理离线账户的临时RBAC数据。
     */
    ~RBACCommandData()
    {
        if (needDelete)
            delete rbac;
    }

    rbac::RBACData* rbac = nullptr;  ///< RBAC数据指针，存储账户的权限信息
    bool needDelete = false;          ///< 是否需要在析构时删除RBAC数据对象
};

using namespace Trinity::ChatCommands;

/**
 * @class rbac_commandscript
 * @brief RBAC命令脚本类
 *
 * 实现所有RBAC相关的GM命令，包括权限的授予、拒绝、撤销和查看功能。
 * 该类继承自CommandScript，提供命令注册和处理接口。
 */
class rbac_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * 初始化命令脚本，设置脚本名称为"rbac_commandscript"
     */
    rbac_commandscript() : CommandScript("rbac_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回RBAC命令的命令表结构
     *
     * 注册以下命令层次结构：
     * - .rbac
     *   - .account
     *     - .list   - 列出账户权限
     *     - .grant  - 授予权限
     *     - .deny   - 拒绝权限
     *     - .revoke - 撤销权限
     *   - .list     - 列出所有可用权限
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rbacAccountCommandTable =
        {
            { "list",   HandleRBACPermListCommand,   rbac::RBAC_PERM_COMMAND_RBAC_ACC_PERM_LIST,   Console::Yes },
            { "grant",  HandleRBACPermGrantCommand,  rbac::RBAC_PERM_COMMAND_RBAC_ACC_PERM_GRANT,  Console::Yes },
            { "deny",   HandleRBACPermDenyCommand,   rbac::RBAC_PERM_COMMAND_RBAC_ACC_PERM_DENY,   Console::Yes },
            { "revoke", HandleRBACPermRevokeCommand, rbac::RBAC_PERM_COMMAND_RBAC_ACC_PERM_REVOKE, Console::Yes },
        };

        static ChatCommandTable rbacCommandTable =
        {
            { "account", rbacAccountCommandTable },
            { "list",    HandleRBACListPermissionsCommand, rbac::RBAC_PERM_COMMAND_RBAC_LIST, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "rbac", rbacCommandTable },
        };

        return commandTable;
    }

    /**
     * @brief 获取RBAC数据对象
     * @param account 账户标识符
     * @return RBACCommandData 封装的RBAC数据对象
     *
     * 此函数根据账户在线状态返回不同的RBAC数据：
     * - 账户在线：使用会话中的RBAC数据，无需删除
     * - 账户离线：从数据库加载RBAC数据，需要在作用域结束时删除
     *
     * @note 离线账户的RBAC数据修改将在下次登录时生效
     */
    static RBACCommandData GetRBACData(AccountIdentifier account)
    {
        // 如果账户当前在线，直接使用会话中的RBAC数据
        if (account.IsConnected())
            return { account.GetConnectedSession()->GetRBACData(), false };

        // 账户离线时，创建临时RBAC数据对象并从数据库加载
        rbac::RBACData* rbac = new rbac::RBACData(account.GetID(), account.GetName(), realm.Id.Realm, AccountMgr::GetSecurity(account.GetID(), realm.Id.Realm));
        rbac->LoadFromDB();

        return { rbac, true };
    }

    /**
     * @brief 处理授予权限命令
     * @param handler 聊天命令处理器
     * @param account 目标账户（可选，未指定则使用当前目标）
     * @param permId 权限ID
     * @param realmId 领域ID（可选，-1表示所有领域）
     * @return 命令执行成功返回true
     *
     * 命令格式: .rbac account grant [account] #permId [realmId]
     *
     * 功能：为指定账户授予特定权限。
     * 授予的权限将添加到账户的授权权限列表中。
     *
     * 可能的结果：
     * - RBAC_OK: 权限授予成功
     * - RBAC_CANT_ADD_ALREADY_ADDED: 权限已在授权列表中
     * - RBAC_IN_DENIED_LIST: 权限在拒绝列表中，需要先移除
     * - RBAC_ID_DOES_NOT_EXISTS: 权限ID不存在
     *
     * @note 需要检查安全等级，不能修改更高安全等级账户的权限
     */
    static bool HandleRBACPermGrantCommand(ChatHandler* handler, Optional<AccountIdentifier> account, uint32 permId, Optional<int32> realmId)
    {
        // 如果未指定账户，尝试从当前目标获取
        if (!account)
            account = AccountIdentifier::FromTarget(handler);
        if (!account)
            return false;

        // 检查安全等级，不能修改更高安全等级账户的权限
        if (handler->HasLowerSecurityAccount(nullptr, account->GetID(), true))
            return false;

        // 如果未指定领域ID，默认为-1（所有领域）
        if (!realmId)
            realmId = -1;

        // 获取RBAC数据
        RBACCommandData data = GetRBACData(*account);

        // 执行权限授予操作
        rbac::RBACCommandResult result = data.rbac->GrantPermission(permId, *realmId);
        rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(permId);

        // 根据操作结果发送相应的系统消息
        switch (result)
        {
            case rbac::RBAC_CANT_ADD_ALREADY_ADDED:
                // 权限已在授权列表中
                handler->PSendSysMessage(LANG_RBAC_PERM_GRANTED_IN_LIST, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_IN_DENIED_LIST:
                // 权限在拒绝列表中，无法直接授予
                handler->PSendSysMessage(LANG_RBAC_PERM_GRANTED_IN_DENIED_LIST, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_OK:
                // 权限授予成功
                handler->PSendSysMessage(LANG_RBAC_PERM_GRANTED, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_ID_DOES_NOT_EXISTS:
                // 权限ID不存在
                handler->PSendSysMessage(LANG_RBAC_WRONG_PARAMETER_ID, permId);
                break;
            default:
                break;
        }

        return true;
    }

    /**
     * @brief 处理拒绝权限命令
     * @param handler 聊天命令处理器
     * @param account 目标账户（可选）
     * @param permId 权限ID
     * @param realmId 领域ID（可选，-1表示所有领域）
     * @return 命令执行成功返回true
     *
     * 命令格式: .rbac account deny [account] #permId [realmId]
     *
     * 功能：为指定账户拒绝特定权限。
     * 拒绝的权限将添加到账户的拒绝权限列表中，
     * 即使权限在其他地方被授予，也会被拒绝。
     *
     * 可能的结果：
     * - RBAC_OK: 权限拒绝成功
     * - RBAC_CANT_ADD_ALREADY_ADDED: 权限已在拒绝列表中
     * - RBAC_IN_GRANTED_LIST: 权限在授权列表中，需要先移除
     * - RBAC_ID_DOES_NOT_EXISTS: 权限ID不存在
     */
    static bool HandleRBACPermDenyCommand(ChatHandler* handler, Optional<AccountIdentifier> account, uint32 permId, Optional<int32> realmId)
    {
        // 如果未指定账户，尝试从当前目标获取
        if (!account)
            account = AccountIdentifier::FromTarget(handler);
        if (!account)
            return false;

        // 检查安全等级
        if (handler->HasLowerSecurityAccount(nullptr, account->GetID(), true))
            return false;

        // 默认领域ID为-1（所有领域）
        if (!realmId)
            realmId = -1;

        RBACCommandData data = GetRBACData(*account);

        // 执行权限拒绝操作
        rbac::RBACCommandResult result = data.rbac->DenyPermission(permId, *realmId);
        rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(permId);

        // 根据操作结果发送相应的系统消息
        switch (result)
        {
            case rbac::RBAC_CANT_ADD_ALREADY_ADDED:
                // 权限已在拒绝列表中
                handler->PSendSysMessage(LANG_RBAC_PERM_DENIED_IN_LIST, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_IN_GRANTED_LIST:
                // 权限在授权列表中，无法直接拒绝
                handler->PSendSysMessage(LANG_RBAC_PERM_DENIED_IN_GRANTED_LIST, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_OK:
                // 权限拒绝成功
                handler->PSendSysMessage(LANG_RBAC_PERM_DENIED, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_ID_DOES_NOT_EXISTS:
                // 权限ID不存在
                handler->PSendSysMessage(LANG_RBAC_WRONG_PARAMETER_ID, permId);
                break;
            default:
                break;
        }

        return true;
    }

    /**
     * @brief 处理撤销权限命令
     * @param handler 聊天命令处理器
     * @param account 目标账户（可选）
     * @param permId 权限ID
     * @param realmId 领域ID（可选，-1表示所有领域）
     * @return 命令执行成功返回true
     *
     * 命令格式: .rbac account revoke [account] #permId [realmId]
     *
     * 功能：从指定账户撤销特定权限。
     * 此命令会从授权列表或拒绝列表中移除该权限。
     *
     * 可能的结果：
     * - RBAC_OK: 权限撤销成功
     * - RBAC_CANT_REVOKE_NOT_IN_LIST: 权限不在任何列表中
     * - RBAC_ID_DOES_NOT_EXISTS: 权限ID不存在
     */
    static bool HandleRBACPermRevokeCommand(ChatHandler* handler, Optional<AccountIdentifier> account, uint32 permId, Optional<int32> realmId)
    {
        // 如果未指定账户，尝试从当前目标获取
        if (!account)
            account = AccountIdentifier::FromTarget(handler);
        if (!account)
            return false;

        // 检查安全等级
        if (handler->HasLowerSecurityAccount(nullptr, account->GetID(), true))
            return false;

        // 默认领域ID为-1（所有领域）
        if (!realmId)
            realmId = -1;

        RBACCommandData data = GetRBACData(*account);

        // 执行权限撤销操作
        rbac::RBACCommandResult result = data.rbac->RevokePermission(permId, *realmId);
        rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(permId);

        // 根据操作结果发送相应的系统消息
        switch (result)
        {
            case rbac::RBAC_CANT_REVOKE_NOT_IN_LIST:
                // 权限不在列表中，无法撤销
                handler->PSendSysMessage(LANG_RBAC_PERM_REVOKED_NOT_IN_LIST, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_OK:
                // 权限撤销成功
                handler->PSendSysMessage(LANG_RBAC_PERM_REVOKED, permId, permission->GetName(),
                                         *realmId, account->GetID(), account->GetName());
                break;
            case rbac::RBAC_ID_DOES_NOT_EXISTS:
                // 权限ID不存在
                handler->PSendSysMessage(LANG_RBAC_WRONG_PARAMETER_ID, permId);
                break;
            default:
                break;
        }

        return true;
    }

    /**
     * @brief 处理列出账户权限命令
     * @param handler 聊天命令处理器
     * @param account 目标账户（可选）
     * @return 命令执行成功返回true
     *
     * 命令格式: .rbac account list [account]
     *
     * 功能：列出指定账户的所有权限信息，包括：
     * 1. 已授予的权限列表（Granted Permissions）
     * 2. 已拒绝的权限列表（Denied Permissions）
     * 3. 根据安全等级获得的默认权限（Default Permissions by Security Level）
     *
     * 这有助于管理员了解账户的完整权限状态。
     */
    static bool HandleRBACPermListCommand(ChatHandler* handler, Optional<AccountIdentifier> account)
    {
        // 如果未指定账户，尝试从当前目标获取
        if (!account)
            account = AccountIdentifier::FromTarget(handler);
        if (!account)
            return false;

        RBACCommandData data = GetRBACData(*account);

        // 显示已授予的权限列表
        handler->PSendSysMessage(LANG_RBAC_LIST_HEADER_GRANTED, data.rbac->GetId(), data.rbac->GetName());
        rbac::RBACPermissionContainer const& granted = data.rbac->GetGrantedPermissions();
        if (granted.empty())
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_EMPTY));
        else
        {
            for (uint32 grantedId : granted)
            {
                rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(grantedId);
                handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, permission->GetId(), permission->GetName());
            }
        }

        // 显示已拒绝的权限列表
        handler->PSendSysMessage(LANG_RBAC_LIST_HEADER_DENIED, data.rbac->GetId(), data.rbac->GetName());
        rbac::RBACPermissionContainer const& denied = data.rbac->GetDeniedPermissions();
        if (denied.empty())
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_EMPTY));
        else
        {
            for (uint32 deniedId : denied)
            {
                rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(deniedId);
                handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, permission->GetId(), permission->GetName());
            }
        }

        // 显示根据安全等级获得的默认权限
        handler->PSendSysMessage(LANG_RBAC_LIST_HEADER_BY_SEC_LEVEL, data.rbac->GetId(), data.rbac->GetName(), data.rbac->GetSecurityLevel());
        rbac::RBACPermissionContainer const& defaultPermissions = sAccountMgr->GetRBACDefaultPermissions(data.rbac->GetSecurityLevel());
        if (defaultPermissions.empty())
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_EMPTY));
        else
        {
            for (uint32 defaultPermission : defaultPermissions)
            {
                rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(defaultPermission);
                handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, permission->GetId(), permission->GetName());
            }
        }

        return true;
    }

    /**
     * @brief 处理列出所有权限命令
     * @param handler 聊天命令处理器
     * @param permId 权限ID（可选，指定则显示该权限的详细信息）
     * @return 命令执行成功返回true
     *
     * 命令格式:
     * - .rbac list          - 列出所有可用权限
     * - .rbac list #permId  - 显示指定权限的详细信息（包括关联权限）
     *
     * 功能：
     * 1. 不指定权限ID时：列出系统中所有可用的权限
     * 2. 指定权限ID时：显示该权限的详细信息，包括其关联的其他权限
     *
     * 关联权限是指当一个权限被授予时，自动一起授予的其他权限。
     * 这允许通过一个权限ID来控制一组相关的权限。
     */
    static bool HandleRBACListPermissionsCommand(ChatHandler* handler, Optional<uint32> permId)
    {
        if (!permId)
        {
            // 列出所有可用权限
            rbac::RBACPermissionsContainer const& permissions = sAccountMgr->GetRBACPermissionList();
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_PERMISSIONS_HEADER));
            for (auto const& [_, permission] : permissions)
            {
                handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, permission->GetId(), permission->GetName());
            }
        }
        else
        {
            // 显示指定权限的详细信息
            rbac::RBACPermission const* permission = sAccountMgr->GetRBACPermission(*permId);
            if (!permission)
            {
                // 权限ID不存在
                handler->PSendSysMessage(LANG_RBAC_WRONG_PARAMETER_ID, *permId);
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 显示权限基本信息
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_PERMISSIONS_HEADER));
            handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, permission->GetId(), permission->GetName());

            // 显示关联权限列表
            handler->PSendSysMessage("%s", handler->GetTrinityString(LANG_RBAC_LIST_PERMS_LINKED_HEADER));
            for (uint32 linkedPerm : permission->GetLinkedPermissions())
                if (rbac::RBACPermission const* rbacPermission = sAccountMgr->GetRBACPermission(linkedPerm))
                    handler->PSendSysMessage(LANG_RBAC_LIST_ELEMENT, rbacPermission->GetId(), rbacPermission->GetName());
        }

        return true;
    }
};

/**
 * @brief 注册RBAC命令脚本
 *
 * 此函数在脚本系统初始化时被调用，
 * 创建rbac_commandscript实例并注册到命令脚本管理器中。
 */
void AddSC_rbac_commandscript()
{
    new rbac_commandscript();
}
