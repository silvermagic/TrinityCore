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
 * @file cs_account.cpp
 * @brief 账号管理命令模块
 *
 * 本模块实现了所有与账号管理相关的游戏命令，包括：
 * - 账号创建、删除、密码修改
 * - 账号安全设置（双因素认证、IP锁定、国家锁定）
 * - 账号信息查询（在线玩家列表、账号信息显示）
 * - 账号扩展包设置
 * - 账号邮箱管理
 * - 账号权限级别设置
 *
 * 命令层次结构：
 * - .account: 基础账号命令
 *   - .account 2fa: 双因素认证相关
 *   - .account addon: 设置扩展包
 *   - .account create: 创建账号
 *   - .account delete: 删除账号
 *   - .account email: 修改邮箱
 *   - .account lock: 账号锁定
 *   - .account password: 修改密码
 *   - .account set: 设置账号属性
 *
 * 安全机制：
 * - 双因素认证使用 TOTP (Time-based One-Time Password) 算法
 * - 密码和敏感数据使用 AES 加密存储
 * - 权限检查确保只有授权用户才能执行敏感操作
 */

/* ScriptData
Name: account_commandscript
%Complete: 100
Comment: All account related commands
Category: commandscripts
EndScriptData */

#include "AccountMgr.h"
#include "AES.h"
#include "Base32.h"
#include "Chat.h"
#include "CryptoGenerics.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "IpAddress.h"
#include "IPLocation.h"
#include "Language.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SecretMgr.h"
#include "TOTP.h"
#include "World.h"
#include "WorldSession.h"
#include <unordered_map>

using namespace Trinity::ChatCommands;

/**
 * @class account_commandscript
 * @brief 账号命令脚本类
 *
 * 继承自 CommandScript 基类，实现账号管理相关的所有游戏命令。
 * 该类负责注册和处理所有账号相关的命令，包括账号创建、删除、
 * 密码管理、双因素认证、邮箱管理等功能。
 */
class account_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化账号命令脚本，设置脚本名称为 "account_commandscript"
     */
    account_commandscript() : CommandScript("account_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有账号相关的命令及其子命令，定义命令的权限要求、帮助文本等
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的命令
     *
     * 命令结构：
     * - account set: 设置账号属性的子命令组
     *   - addon: 设置扩展包等级
     *   - sec regmail: 设置注册邮箱
     *   - sec email: 设置安全邮箱
     *   - gmlevel/seclevel: 设置权限等级
     *   - password: 设置密码
     *   - 2fa: 设置双因素认证
     * - account onlinelist: 在线玩家列表（支持过滤）
     * - account: 主命令组
     *   - 2fa setup/remove: 双因素认证设置/移除
     *   - addon: 设置扩展包
     *   - create: 创建账号
     *   - delete: 删除账号
     *   - email: 修改邮箱
     *   - lock country/ip: 国家/IP锁定
     *   - password: 修改密码
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable accountSetCommandTable =
        {
            { "addon",              HandleAccountSetAddonCommand,       LANG_COMMAND_ACC_SET_ADDON_HELP,        rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_ADDON,          Console::Yes },
            { "sec regmail",        HandleAccountSetRegEmailCommand,    LANG_COMMAND_ACC_SET_SEC_REGMAIL_HELP,  rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_SEC_REGMAIL,    Console::Yes },
            { "sec email",          HandleAccountSetEmailCommand,       LANG_COMMAND_ACC_SET_SEC_EMAIL_HELP,    rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_SEC_EMAIL,      Console::Yes },
            { "gmlevel",            HandleAccountSetSecLevelCommand,    LANG_COMMAND_ACC_SET_SECLEVEL_HELP,     rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_SECLEVEL,       Console::Yes },  // temp for a transition period
            { "seclevel",           HandleAccountSetSecLevelCommand,    LANG_COMMAND_ACC_SET_SECLEVEL_HELP,     rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_SECLEVEL,       Console::Yes },
            { "password",           HandleAccountSetPasswordCommand,    LANG_COMMAND_ACC_SET_PASSWORD_HELP,     rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_PASSWORD,       Console::Yes },
            { "2fa",                HandleAccountSet2FACommand,         LANG_COMMAND_ACC_SET_2FA_HELP,          rbac::RBAC_PERM_COMMAND_ACCOUNT_SET_2FA,            Console::Yes },
        };
        static ChatCommandTable accountOnlinelistCommandTable =
        {
            { "",         HandleAccountOnlineListCommand,               LANG_COMMAND_ACC_ONLINELIST_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST,        Console::Yes },
            { "ip",       HandleAccountOnlineListWithIpFilterCommand,   LANG_COMMAND_ACC_ONLINELIST_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST,        Console::Yes },
            { "limit",    HandleAccountOnlineListWithLimitCommand,      LANG_COMMAND_ACC_ONLINELIST_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST,        Console::Yes },
            { "map",      HandleAccountOnlineListWithMapFilterCommand,  LANG_COMMAND_ACC_ONLINELIST_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST,        Console::Yes },
            { "zone",     HandleAccountOnlineListWithZoneFilterCommand, LANG_COMMAND_ACC_ONLINELIST_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST,        Console::Yes },
        };
        static ChatCommandTable accountCommandTable =
        {
            { "2fa setup",          HandleAccount2FASetupCommand,       LANG_COMMAND_ACC_2FA_SETUP_HELP,        rbac::RBAC_PERM_COMMAND_ACCOUNT_2FA_SETUP,          Console::No  },
            { "2fa remove",         HandleAccount2FARemoveCommand,      LANG_COMMAND_ACC_2FA_REMOVE_HELP,       rbac::RBAC_PERM_COMMAND_ACCOUNT_2FA_REMOVE,         Console::No  },
            { "addon",              HandleAccountAddonCommand,          LANG_COMMAND_ACC_ADDON_HELP,            rbac::RBAC_PERM_COMMAND_ACCOUNT_ADDON,              Console::No  },
            { "create",             HandleAccountCreateCommand,         LANG_COMMAND_ACC_CREATE_HELP,           rbac::RBAC_PERM_COMMAND_ACCOUNT_CREATE,             Console::Yes },
            { "delete",             HandleAccountDeleteCommand,         LANG_COMMAND_ACC_DELETE_HELP,           rbac::RBAC_PERM_COMMAND_ACCOUNT_DELETE,             Console::Yes },
            { "email",              HandleAccountEmailCommand,          LANG_COMMAND_ACC_EMAIL_HELP,            rbac::RBAC_PERM_COMMAND_ACCOUNT_EMAIL,              Console::No  },
            { "onlinelist",         accountOnlinelistCommandTable },
            { "lock country",       HandleAccountLockCountryCommand,    LANG_COMMAND_ACC_LOCK_COUNTRY_HELP,     rbac::RBAC_PERM_COMMAND_ACCOUNT_LOCK_COUNTRY,       Console::No  },
            { "lock ip",            HandleAccountLockIpCommand,         LANG_COMMAND_ACC_LOCK_IP_HELP,          rbac::RBAC_PERM_COMMAND_ACCOUNT_LOCK_IP,            Console::No  },
            { "set",                accountSetCommandTable },
            { "password",           HandleAccountPasswordCommand,       LANG_COMMAND_ACC_PASSWORD_HELP,         rbac::RBAC_PERM_COMMAND_ACCOUNT_PASSWORD,           Console::No  },
            { "",                   HandleAccountCommand,               LANG_COMMAND_ACCOUNT_HELP,              rbac::RBAC_PERM_COMMAND_ACCOUNT,                    Console::No  },
        };
        static ChatCommandTable commandTable =
        {
            { "account",            accountCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 设置双因素认证（2FA）
     *
     * 为当前账号设置基于 TOTP（Time-based One-Time Password）的双因素认证。
     * 该命令会生成一个密钥建议，用户需要使用验证器应用扫描并输入验证码来完成设置。
     *
     * @param handler 聊天命令处理器，用于发送消息和获取会话信息
     * @param token 可选参数，用户输入的 TOTP 验证码
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 检查服务器是否配置了 TOTP 主密钥
     * 2. 检查账号是否已启用双因素认证
     * 3. 生成或检索之前生成的密钥建议
     * 4. 如果提供了验证码，验证是否正确
     * 5. 验证通过后，使用 AES 加密密钥并保存到数据库
     *
     * 安全机制：
     * - 密钥使用 AES 加密后存储
     * - 临时建议存储在内存中，避免重复生成
     * - 验证码验证确保用户拥有正确的验证器
     *
     * 调用时机：玩家执行 .account 2fa setup 命令时
     * 性能注意：涉及数据库查询和加密操作，不应频繁调用
     */
    static bool HandleAccount2FASetupCommand(ChatHandler* handler, Optional<uint32> token)
    {
        auto const& masterKey = sSecretMgr->GetSecret(SECRET_TOTP_MASTER_KEY);
        if (!masterKey.IsAvailable())
        {
            handler->SendSysMessage(LANG_2FA_COMMANDS_NOT_SETUP);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 const accountId = handler->GetSession()->GetAccountId();

        { // check if 2FA already enabled
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_TOTP_SECRET);
            stmt->setUInt32(0, accountId);
            PreparedQueryResult result = LoginDatabase.Query(stmt);

            if (!result)
            {
                TC_LOG_ERROR("misc", "Account {} not found in login database when processing .account 2fa setup command.", accountId);
                handler->SendSysMessage(LANG_UNKNOWN_ERROR);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!result->Fetch()->IsNull())
            {
                handler->SendSysMessage(LANG_2FA_ALREADY_SETUP);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        // store random suggested secrets
        static std::unordered_map<uint32, Trinity::Crypto::TOTP::Secret> suggestions;
        auto pair = suggestions.emplace(std::piecewise_construct, std::make_tuple(accountId), std::make_tuple(Trinity::Crypto::TOTP::RECOMMENDED_SECRET_LENGTH)); // std::vector 1-argument size_t constructor invokes resize
        if (pair.second) // no suggestion yet, generate random secret
            Trinity::Crypto::GetRandomBytes(pair.first->second);

        if (!pair.second && token) // suggestion already existed and token specified - validate
        {
            if (Trinity::Crypto::TOTP::ValidateToken(pair.first->second, *token))
            {
                if (masterKey)
                    Trinity::Crypto::AEEncryptWithRandomIV<Trinity::Crypto::AES>(pair.first->second, *masterKey);

                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET);
                stmt->setBinary(0, pair.first->second);
                stmt->setUInt32(1, accountId);
                LoginDatabase.Execute(stmt);
                suggestions.erase(pair.first);
                handler->SendSysMessage(LANG_2FA_SETUP_COMPLETE);
                return true;
            }
            else
                handler->SendSysMessage(LANG_2FA_INVALID_TOKEN);
        }

        // new suggestion, or no token specified, output TOTP parameters
        handler->PSendSysMessage(LANG_2FA_SECRET_SUGGESTION, Trinity::Encoding::Base32::Encode(pair.first->second));
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 移除双因素认证（2FA）
     *
     * 移除当前账号的双因素认证设置。需要提供正确的 TOTP 验证码才能移除。
     *
     * @param handler 聊天命令处理器
     * @param token 可选参数，用户输入的 TOTP 验证码
     *
     * @return bool 移除成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 检查服务器 TOTP 主密钥配置
     * 2. 从数据库获取当前账号的 TOTP 密钥
     * 3. 检查是否已启用双因素认证
     * 4. 如果提供了验证码，解密密钥并验证
     * 5. 验证通过后，清空数据库中的 TOTP 密钥
     *
     * 安全机制：
     * - 需要验证码验证才能移除，防止未授权移除
     * - 密钥解密失败会记录错误日志
     *
     * 调用时机：玩家执行 .account 2fa remove 命令时
     */
    static bool HandleAccount2FARemoveCommand(ChatHandler* handler, Optional<uint32> token)
    {
        auto const& masterKey = sSecretMgr->GetSecret(SECRET_TOTP_MASTER_KEY);
        if (!masterKey.IsAvailable())
        {
            handler->SendSysMessage(LANG_2FA_COMMANDS_NOT_SETUP);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 const accountId = handler->GetSession()->GetAccountId();
        Trinity::Crypto::TOTP::Secret secret;
        { // get current TOTP secret
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_TOTP_SECRET);
            stmt->setUInt32(0, accountId);
            PreparedQueryResult result = LoginDatabase.Query(stmt);

            if (!result)
            {
                TC_LOG_ERROR("misc", "Account {} not found in login database when processing .account 2fa setup command.", accountId);
                handler->SendSysMessage(LANG_UNKNOWN_ERROR);
                handler->SetSentErrorMessage(true);
                return false;
            }

            Field* field = result->Fetch();
            if (field->IsNull())
            { // 2FA not enabled
                handler->SendSysMessage(LANG_2FA_NOT_SETUP);
                handler->SetSentErrorMessage(true);
                return false;
            }

            secret = field->GetBinary();
        }

        if (token)
        {
            if (masterKey)
            {
                bool success = Trinity::Crypto::AEDecrypt<Trinity::Crypto::AES>(secret, *masterKey);
                if (!success)
                {
                    TC_LOG_ERROR("misc", "Account {} has invalid ciphertext in TOTP token.", accountId);
                    handler->SendSysMessage(LANG_UNKNOWN_ERROR);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }

            if (Trinity::Crypto::TOTP::ValidateToken(secret, *token))
            {
                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET);
                stmt->setNull(0);
                stmt->setUInt32(1, accountId);
                LoginDatabase.Execute(stmt);
                handler->SendSysMessage(LANG_2FA_REMOVE_COMPLETE);
                return true;
            }
            else
                handler->SendSysMessage(LANG_2FA_INVALID_TOKEN);
        }

        handler->SendSysMessage(LANG_2FA_REMOVE_NEED_TOKEN);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 设置账号扩展包等级
     *
     * 设置当前账号可以访问的扩展包内容等级。不同扩展包解锁不同的游戏内容。
     *
     * @param handler 聊天命令处理器
     * @param expansion 扩展包等级（0=经典，1=TBC，2=WLK等）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 扩展包等级说明：
     * - 0: 经典旧世（Classic）
     * - 1: 燃烧的远征（The Burning Crusade）
     * - 2: 巫妖王之怒（Wrath of the Lich King）
     * - 3: 大地的裂变（Cataclysm）
     *
     * 限制：
     * - 不能设置超过服务器配置的最大扩展包等级
     *
     * 调用时机：玩家执行 .account addon 命令时
     */
    static bool HandleAccountAddonCommand(ChatHandler* handler, uint8 expansion)
    {
        if (expansion > sWorld->getIntConfig(CONFIG_EXPANSION))
        {
            handler->SendSysMessage(LANG_IMPROPER_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_EXPANSION);

        stmt->setUInt8(0, expansion);
        stmt->setUInt32(1, handler->GetSession()->GetAccountId());

        LoginDatabase.Execute(stmt);

        handler->PSendSysMessage(LANG_ACCOUNT_ADDON, expansion);
        return true;
    }

    /**
     * @brief 创建新账号
     *
     * 创建一个新的游戏账号。此命令通常由管理员或控制台执行。
     *
     * @param handler 聊天命令处理器
     * @param accountName 账号名称
     * @param password 账号密码
     * @param email 可选参数，账号关联的邮箱地址
     *
     * @return bool 创建成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 验证账号名是否包含 '@' 符号（如果包含则提示使用战网命令）
     * 2. 调用 AccountMgr 创建账号
     * 3. 根据返回结果处理不同的错误情况
     *
     * 错误处理：
     * - AOR_OK: 创建成功
     * - AOR_NAME_TOO_LONG: 账号名过长
     * - AOR_PASS_TOO_LONG: 密码过长
     * - AOR_NAME_ALREADY_EXIST: 账号名已存在
     * - AOR_DB_INTERNAL_ERROR: 数据库内部错误
     *
     * 调用时机：管理员执行 .account create 命令时
     * 性能注意：涉及数据库写操作和密码哈希计算
     */
    /// Create an account
    static bool HandleAccountCreateCommand(ChatHandler* handler, std::string const& accountName, std::string const& password, Optional<std::string> const& email)
    {
        if (accountName.find('@') != std::string::npos)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_USE_BNET_COMMANDS);
            handler->SetSentErrorMessage(true);
            return false;
        }

        switch (sAccountMgr->CreateAccount(accountName, password, email.value_or("")))
        {
            case AccountOpResult::AOR_OK:
                handler->PSendSysMessage(LANG_ACCOUNT_CREATED, accountName);
                if (handler->GetSession())
                {
                    TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {}) created Account {} (Email: '{}')",
                        handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                        handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString(),
                        accountName, email.value_or(""));
                }
                break;
            case AccountOpResult::AOR_NAME_TOO_LONG:
                handler->SendSysMessage(LANG_ACCOUNT_NAME_TOO_LONG);
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_PASS_TOO_LONG:
                handler->SendSysMessage(LANG_ACCOUNT_PASS_TOO_LONG);
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_NAME_ALREADY_EXIST:
                handler->SendSysMessage(LANG_ACCOUNT_ALREADY_EXIST);
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_DB_INTERNAL_ERROR:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_CREATED_SQL_ERROR, accountName);
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_CREATED, accountName);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 删除账号
     *
     * 删除指定账号及其在该Realm上的所有角色。这是一个危险操作，不可逆。
     *
     * @param handler 聊天命令处理器
     * @param accountName 要删除的账号名称
     *
     * @return bool 删除成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 将账号名转换为大写格式
     * 2. 验证账号是否存在
     * 3. 权限检查：只能删除安全等级低于自己的账号
     * 4. 调用 AccountMgr 删除账号及其角色数据
     *
     * 安全机制：
     * - 权限等级检查：不能删除比自己权限高或相等的账号
     * - 不能删除自己的账号
     *
     * @todo 此函数需要增强以支持登录/Realm分离架构
     *       （删除角色 -> 删除Realm上的账号角色 -> 删除账号）
     *
     * 调用时机：管理员执行 .account delete 命令时
     * 性能注意：级联删除操作，可能影响多张数据库表
     */
    /// Delete a user account and all associated characters in this realm
    /// @todo This function has to be enhanced to respect the login/realm split (delete char, delete account chars in realm then delete account)
    static bool HandleAccountDeleteCommand(ChatHandler* handler, std::string accountName)
    {
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 accountId = AccountMgr::GetId(accountName);
        if (!accountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        /// Commands not recommended call from chat, but support anyway
        /// can delete only for account with less security
        /// This is also reject self apply in fact
        if (handler->HasLowerSecurityAccount(nullptr, accountId, true))
            return false;

        AccountOpResult result = AccountMgr::DeleteAccount(accountId);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->PSendSysMessage(LANG_ACCOUNT_DELETED, accountName.c_str());
                break;
            case AccountOpResult::AOR_NAME_NOT_EXIST:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_DB_INTERNAL_ERROR:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_DELETED_SQL_ERROR, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_DELETED, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 显示在线玩家列表（基础版本）
     *
     * 显示当前Realm上所有在线玩家的信息，不带任何过滤条件。
     *
     * @param handler 聊天命令处理器
     *
     * @return bool 总是返回 true
     *
     * @details 这是 HandleAccountOnlineListCommandWithParameters 的简化版本，
     *          不传递任何过滤参数，显示所有在线玩家。
     *
     * 调用时机：玩家执行 .account onlinelist 命令时
     */
    /// Display info on users currently in the realm
    static bool HandleAccountOnlineListCommand(ChatHandler* handler)
    {
        return HandleAccountOnlineListCommandWithParameters(handler, {}, {}, {}, {});
    }

    /**
     * @brief 显示在线玩家列表（按IP地址过滤）
     *
     * 显示指定IP地址的在线玩家信息。
     *
     * @param handler 聊天命令处理器
     * @param ipAddress 要过滤的IP地址
     *
     * @return bool 总是返回 true
     *
     * 调用时机：玩家执行 .account onlinelist ip <ip> 命令时
     */
    static bool HandleAccountOnlineListWithIpFilterCommand(ChatHandler* handler, std::string ipAddress)
    {
        return HandleAccountOnlineListCommandWithParameters(handler, ipAddress, {}, {}, {});
    }

    /**
     * @brief 显示在线玩家列表（限制显示数量）
     *
     * 显示指定数量的在线玩家信息。
     *
     * @param handler 聊天命令处理器
     * @param limit 最大显示数量
     *
     * @return bool 总是返回 true
     *
     * 调用时机：玩家执行 .account onlinelist limit <count> 命令时
     */
    static bool HandleAccountOnlineListWithLimitCommand(ChatHandler* handler, uint32 limit)
    {
        return HandleAccountOnlineListCommandWithParameters(handler, {}, limit, {}, {});
    }

    /**
     * @brief 显示在线玩家列表（按地图过滤）
     *
     * 显示在指定地图上的在线玩家信息。
     *
     * @param handler 聊天命令处理器
     * @param mapId 地图ID
     *
     * @return bool 总是返回 true
     *
     * 调用时机：玩家执行 .account onlinelist map <mapId> 命令时
     */
    static bool HandleAccountOnlineListWithMapFilterCommand(ChatHandler* handler, uint32 mapId)
    {
        return HandleAccountOnlineListCommandWithParameters(handler, {}, {}, mapId, {});
    }

    /**
     * @brief 显示在线玩家列表（按区域过滤）
     *
     * 显示在指定区域（Zone）的在线玩家信息。
     *
     * @param handler 聊天命令处理器
     * @param zoneId 区域ID
     *
     * @return bool 总是返回 true
     *
     * 调用时机：玩家执行 .account onlinelist zone <zoneId> 命令时
     */
    static bool HandleAccountOnlineListWithZoneFilterCommand(ChatHandler* handler, uint32 zoneId)
    {
        return HandleAccountOnlineListCommandWithParameters(handler, {}, {}, {}, zoneId);
    }

    /**
     * @brief 显示在线玩家列表（带参数过滤）
     *
     * 显示当前Realm上的在线玩家列表，支持多种过滤条件。
     *
     * @param handler 聊天命令处理器
     * @param ipAddress 可选参数，IP地址过滤
     * @param limit 可选参数，最大显示数量
     * @param mapId 可选参数，地图ID过滤
     * @param zoneId 可选参数，区域ID过滤
     *
     * @return bool 成功返回 true
     *
     * @details 执行流程：
     * 1. 遍历所有在线会话
     * 2. 跳过角色选择界面的会话
     * 3. 应用各种过滤条件（IP、地图、区域）
     * 4. 显示玩家信息：账号名、角色名、IP地址、地图、区域、扩展包、权限等级
     * 5. 达到数量限制后停止遍历
     *
     * 显示信息包括：
     * - 账号名称
     * - 角色名称
     * - IP地址
     * - 地图ID
     * - 区域ID
     * - 扩展包等级
     * - 权限等级
     *
     * 调用时机：被其他在线列表命令调用
     * 性能注意：遍历所有在线会话，大量玩家时可能有性能影响
     */
    static bool HandleAccountOnlineListCommandWithParameters(ChatHandler* handler, Optional<std::string> ipAddress, Optional<uint32> limit, Optional<uint32> mapId, Optional<uint32> zoneId)
    {
        size_t sessionsMatchCount = 0; // 匹配的会话计数器

        // 获取所有在线会话
        SessionMap const& sessionsMap = sWorld->GetAllSessions();
        for (SessionMap::value_type const& sessionPair : sessionsMap)
        {
            WorldSession* session = sessionPair.second;
            Player* player = session->GetPlayer();

            // 忽略角色选择界面的会话（没有加载角色的会话）
            if (!player)
                continue;

            uint32 playerMapId = player->GetMapId();
            uint32 playerZoneId = player->GetZoneId();

            // 应用可选的IP地址过滤
            if (ipAddress && ipAddress != session->GetRemoteAddress())
                continue;

            // 应用可选的地图ID过滤
            if (mapId && mapId != playerMapId)
                continue;

            // 应用可选的区域ID过滤
            if (zoneId && zoneId != playerZoneId)
                continue;

            // 在第一个匹配的会话时显示表头
            if (!sessionsMatchCount)
            {
                ///- 在第一个匹配会话时显示在线账号/角色列表表头
                handler->SendSysMessage(LANG_ACCOUNT_LIST_BAR_HEADER);
                handler->SendSysMessage(LANG_ACCOUNT_LIST_HEADER);
                handler->SendSysMessage(LANG_ACCOUNT_LIST_BAR);
            }

            // 显示该玩家的详细信息
            handler->PSendSysMessage(LANG_ACCOUNT_LIST_LINE,
                session->GetAccountName().c_str(),
                session->GetPlayerName().c_str(),
                session->GetRemoteAddress().c_str(),
                playerMapId,
                playerZoneId,
                session->Expansion(),
                int32(session->GetSecurity()));

            ++sessionsMatchCount; // 增加匹配计数

            // 应用可选的数量限制
            if (limit && sessionsMatchCount >= limit)
                break;
        }

        // 表头在第一个匹配会话时打印。如果未打印，说明没有会话匹配条件
        if (!sessionsMatchCount)
        {
            handler->SendSysMessage(LANG_ACCOUNT_LIST_EMPTY);
            return true;
        }

        handler->SendSysMessage(LANG_ACCOUNT_LIST_BAR);
        return true;
    }

    /**
     * @brief 账号国家锁定设置
     *
     * 启用或禁用账号的国家锁定功能。启用后，账号只能从特定国家登录。
     *
     * @param handler 聊天命令处理器
     * @param state true=启用锁定，false=禁用锁定
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 启用锁定时：
     * 1. 通过 IP 地址获取国家代码
     * 2. 将国家代码保存到数据库
     * 3. 之后登录时会验证IP所在国家
     *
     * 禁用锁定时：
     * 1. 将国家代码设置为 "00"（表示不限制）
     *
     * 前提条件：
     * - 需要配置 IP2Location 数据库
     * - 如果无法获取IP地理位置信息，锁定会失败
     *
     * 调用时机：玩家执行 .account lock country 命令时
     */
    static bool HandleAccountLockCountryCommand(ChatHandler* handler, bool state)
    {
        if (state)
        {
            // 启用国家锁定：获取当前IP的国家代码
            if (IpLocationRecord const* location = sIPLocation->GetLocationRecord(handler->GetSession()->GetRemoteAddress()))
            {
                // 将国家代码保存到数据库
                LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_LOCK_COUNTRY);
                stmt->setString(0, location->CountryCode);
                stmt->setUInt32(1, handler->GetSession()->GetAccountId());
                LoginDatabase.Execute(stmt);
                handler->PSendSysMessage(LANG_COMMAND_ACCLOCKLOCKED);
            }
            else
            {
                // 无法获取IP地理位置信息
                handler->PSendSysMessage("No IP2Location information - account not locked");
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            // 禁用国家锁定：设置国家代码为 "00"（表示不限制）
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_LOCK_COUNTRY);
            stmt->setString(0, "00");
            stmt->setUInt32(1, handler->GetSession()->GetAccountId());
            LoginDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_COMMAND_ACCLOCKUNLOCKED);
        }
        return true;
    }

    /**
     * @brief 账号IP锁定设置
     *
     * 启用或禁用账号的IP锁定功能。启用后，账号只能从当前IP地址登录。
     *
     * @param handler 聊天命令处理器
     * @param state true=启用锁定，false=禁用锁定
     *
     * @return bool 总是返回 true
     *
     * @details 执行流程：
     * 1. 根据状态参数设置数据库中的锁定标志
     * 2. 启用锁定后，登录时会验证IP地址是否匹配
     *
     * 安全机制：
     * - IP锁定可以防止账号被盗后在其他地方登录
     * - 但如果玩家IP变动（如动态IP），需要重新设置
     *
     * 调用时机：玩家执行 .account lock ip 命令时
     */
    static bool HandleAccountLockIpCommand(ChatHandler* handler, bool state)
    {
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_LOCK);

        if (state)
        {
            stmt->setBool(0, true);                                     // 设置为锁定状态
            handler->PSendSysMessage(LANG_COMMAND_ACCLOCKLOCKED);
        }
        else
        {
            stmt->setBool(0, false);                                    // 设置为解锁状态
            handler->PSendSysMessage(LANG_COMMAND_ACCLOCKUNLOCKED);
        }

        stmt->setUInt32(1, handler->GetSession()->GetAccountId());

        LoginDatabase.Execute(stmt);
        return true;
    }

    /**
     * @brief 修改账号邮箱
     *
     * 修改当前账号的邮箱地址，需要验证旧邮箱和密码。
     *
     * @param handler 聊天命令处理器
     * @param oldEmail 当前邮箱地址
     * @param password 账号密码
     * @param email 新邮箱地址
     * @param emailConfirm 新邮箱确认（需要再次输入）
     *
     * @return bool 修改成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 验证提供的旧邮箱是否与数据库中的邮箱匹配
     * 2. 验证密码是否正确
     * 3. 检查新邮箱是否与旧邮箱相同
     * 4. 验证两次输入的新邮箱是否一致
     * 5. 调用 AccountMgr 修改邮箱
     *
     * 安全机制：
     * - 需要验证旧邮箱，防止邮箱被恶意修改
     * - 需要验证密码，确保是账号所有者在操作
     * - 两次邮箱确认，防止输入错误
     * - 所有失败操作都会记录日志并触发脚本事件
     *
     * 调用时机：玩家执行 .account email 命令时
     */
    static bool HandleAccountEmailCommand(ChatHandler* handler, std::string const& oldEmail, std::string const& password, std::string const& email, std::string const& emailConfirm)
    {
        if (!AccountMgr::CheckEmail(handler->GetSession()->GetAccountId(), oldEmail))
        {
            handler->SendSysMessage(LANG_COMMAND_WRONGEMAIL);
            sScriptMgr->OnFailedEmailChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Tried to change email, but the provided email [{}] is not equal to registration email [{}].",
                handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString(),
                email, oldEmail);
            return false;
        }

        if (!AccountMgr::CheckPassword(handler->GetSession()->GetAccountId(), password))
        {
            handler->SendSysMessage(LANG_COMMAND_WRONGOLDPASSWORD);
            sScriptMgr->OnFailedEmailChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Tried to change email, but the provided password is wrong.",
                handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString());
            return false;
        }

        if (email == oldEmail)
        {
            handler->SendSysMessage(LANG_OLD_EMAIL_IS_NEW_EMAIL);
            sScriptMgr->OnFailedEmailChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (email != emailConfirm)
        {
            handler->SendSysMessage(LANG_NEW_EMAILS_NOT_MATCH);
            sScriptMgr->OnFailedEmailChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Tried to change email, but the confirm email does not match.",
                handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString());
            return false;
        }

        AccountOpResult result = AccountMgr::ChangeEmail(handler->GetSession()->GetAccountId(), email);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->SendSysMessage(LANG_COMMAND_EMAIL);
                sScriptMgr->OnEmailChange(handler->GetSession()->GetAccountId());
                TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Changed Email from [{}] to [{}].",
                    handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                    handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString(),
                    oldEmail, email);
                break;
            case AccountOpResult::AOR_EMAIL_TOO_LONG:
                handler->SendSysMessage(LANG_EMAIL_TOO_LONG);
                sScriptMgr->OnFailedEmailChange(handler->GetSession()->GetAccountId());
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->SendSysMessage(LANG_COMMAND_NOTCHANGEEMAIL);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 修改账号密码
     *
     * 修改当前账号的密码，根据服务器配置可能需要邮箱验证。
     *
     * @param handler 聊天命令处理器
     * @param oldPassword 旧密码
     * @param newPassword 新密码
     * @param confirmPassword 新密码确认
     * @param confirmEmail 可选参数，邮箱确认（根据配置可能需要）
     *
     * @return bool 修改成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 检查服务器配置的安全类型：
     *    - PW_NONE (0): 不需要邮箱验证
     *    - PW_EMAIL (1): 需要邮箱验证
     *    - PW_RBAC (2): 根据RBAC权限决定是否需要邮箱验证
     * 2. 验证旧密码是否正确
     * 3. 如果配置要求，验证邮箱是否正确
     * 4. 验证两次输入的新密码是否一致
     * 5. 调用 AccountMgr 修改密码
     *
     * 安全机制：
     * - 必须提供正确的旧密码
     * - 可选的邮箱验证增加安全性
     * - 两次密码确认防止输入错误
     * - 所有失败操作都会记录日志并触发脚本事件
     *
     * 调用时机：玩家执行 .account password 命令时
     */
    static bool HandleAccountPasswordCommand(ChatHandler* handler, std::string const& oldPassword, std::string const& newPassword, std::string const& confirmPassword, Optional<std::string> const& confirmEmail)
    {
        // 首先，检查配置。安全类型是什么？根据它，命令分支执行
        uint32 const pwConfig = sWorld->getIntConfig(CONFIG_ACC_PASSCHANGESEC); // 0 - PW_NONE, 1 - PW_EMAIL, 2 - PW_RBAC

        // 比较旧密码和输入的旧密码 - 未授权者无法通过
        if (!AccountMgr::CheckPassword(handler->GetSession()->GetAccountId(), oldPassword))
        {
            handler->SendSysMessage(LANG_COMMAND_WRONGOLDPASSWORD);
            sScriptMgr->OnFailedPasswordChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Tried to change password, but the provided old password is wrong.",
                handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString());
            return false;
        }

        // 比较旧邮箱和输入的邮箱 - 但是，只有在以下情况下才需要：
        // PW_EMAIL 配置启用，或 PW_RBAC 配置启用且玩家有 RBAC_PERM_EMAIL_CONFIRM_FOR_PASS_CHANGE 权限
        if ((pwConfig == PW_EMAIL || (pwConfig == PW_RBAC && handler->HasPermission(rbac::RBAC_PERM_EMAIL_CONFIRM_FOR_PASS_CHANGE)))
            && !AccountMgr::CheckEmail(handler->GetSession()->GetAccountId(), confirmEmail.value_or("")))
        {
            handler->SendSysMessage(LANG_COMMAND_WRONGEMAIL);
            sScriptMgr->OnFailedPasswordChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} Tried to change password, but the entered email [{}] is wrong.",
                handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString(),
                confirmEmail.value_or(""));
            return false;
        }

        // 确保新密码输入正确
        if (newPassword != confirmPassword)
        {
            handler->SendSysMessage(LANG_NEW_PASSWORDS_NOT_MATCH);
            sScriptMgr->OnFailedPasswordChange(handler->GetSession()->GetAccountId());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 修改密码并显示结果
        AccountOpResult result = AccountMgr::ChangePassword(handler->GetSession()->GetAccountId(), newPassword);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->SendSysMessage(LANG_COMMAND_PASSWORD);
                sScriptMgr->OnPasswordChange(handler->GetSession()->GetAccountId());
                TC_LOG_INFO("entities.player.character", "Account: {} (IP: {}) Character:[{}] {} changed password.",
                    handler->GetSession()->GetAccountId(), handler->GetSession()->GetRemoteAddress(),
                    handler->GetSession()->GetPlayer()->GetName(), handler->GetSession()->GetPlayer()->GetGUID().ToString());
                break;
            case AccountOpResult::AOR_PASS_TOO_LONG:
                handler->SendSysMessage(LANG_PASSWORD_TOO_LONG);
                sScriptMgr->OnFailedPasswordChange(handler->GetSession()->GetAccountId());
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->SendSysMessage(LANG_COMMAND_NOTCHANGEPASSWORD);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 显示账号信息
     *
     * 显示当前账号的基本信息，包括权限等级、安全类型、邮箱等。
     *
     * @param handler 聊天命令处理器
     *
     * @return bool 总是返回 true
     *
     * @details 显示信息包括：
     * 1. GM权限等级
     * 2. 密码修改的安全类型说明：
     *    - Lowest level: 不需要邮箱验证
     *    - Highest level: 需要邮箱验证
     *    - Special level: 根据权限决定是否需要邮箱验证
     * 3. 如果配置为PW_RBAC且有相应权限，显示需要邮箱验证的提示
     * 4. 如果有查看邮箱的权限，显示当前账号邮箱
     *
     * 调用时机：玩家执行 .account 命令时
     */
    static bool HandleAccountCommand(ChatHandler* handler)
    {
        // 显示 GM 权限等级
        AccountTypes securityLevel = handler->GetSession()->GetSecurity();
        handler->PSendSysMessage(LANG_ACCOUNT_LEVEL, uint32(securityLevel));

        // 显示密码修改所需的安全级别
        bool hasRBAC = (handler->HasPermission(rbac::RBAC_PERM_EMAIL_CONFIRM_FOR_PASS_CHANGE) ? true : false);
        uint32 pwConfig = sWorld->getIntConfig(CONFIG_ACC_PASSCHANGESEC); // 0 - PW_NONE, 1 - PW_EMAIL, 2 - PW_RBAC

        handler->PSendSysMessage(LANG_ACCOUNT_SEC_TYPE, (pwConfig == PW_NONE  ? "Lowest level: No Email input required." :
                                                         pwConfig == PW_EMAIL ? "Highest level: Email input required." :
                                                         pwConfig == PW_RBAC  ? "Special level: Your account may require email input depending on settings. That is the case if another line is printed." :
                                                                                "Unknown security level: Config error?"));

        // RBAC 要求显示 - 不对控制台显示
        if (pwConfig == PW_RBAC && handler->GetSession() && hasRBAC)
            handler->PSendSysMessage(LANG_RBAC_EMAIL_REQUIRED);

        // 如果有足够权限，显示邮箱
        if (handler->HasPermission(rbac::RBAC_PERM_MAY_CHECK_OWN_EMAIL))
        {
            std::string emailoutput;
            uint32 accountId = handler->GetSession()->GetAccountId();

            // 从数据库查询邮箱
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_EMAIL_BY_ID);
            stmt->setUInt32(0, accountId);
            PreparedQueryResult result = LoginDatabase.Query(stmt);

            if (result)
            {
                emailoutput = (*result)[0].GetString();
                handler->PSendSysMessage(LANG_COMMAND_EMAIL_OUTPUT, emailoutput.c_str());
            }
        }

        return true;
    }

    /**
     * @brief 设置账号扩展包等级（管理员命令）
     *
     * 为指定账号设置可访问的扩展包等级。如果不指定账号，则对选中的玩家操作。
     *
     * @param handler 聊天命令处理器
     * @param accountName 可选参数，账号名称（如未指定则使用选中玩家）
     * @param expansion 扩展包等级（0=经典，1=TBC，2=WLK等）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 如果指定了账号名，转换为标准格式并验证存在性
     * 2. 如果未指定账号名，使用选中的玩家账号
     * 3. 权限检查：只能修改权限低于自己的账号或自己的账号
     * 4. 验证扩展包等级不超过服务器配置的最大值
     * 5. 更新数据库中的扩展包设置
     *
     * 安全机制：
     * - 权限等级检查：不能修改权限高于或等于自己的账号
     * - 可以修改自己的账号设置
     *
     * 调用时机：管理员执行 .account set addon 命令时
     */
    /// Set/Unset the expansion level for an account
    static bool HandleAccountSetAddonCommand(ChatHandler* handler, Optional<std::string> accountName, uint8 expansion)
    {
        uint32 accountId;
        if (accountName)
        {
            ///- 将账号名转换为大写格式
            if (!Utf8ToUpperOnlyLatin(*accountName))
            {
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName->c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

            accountId = AccountMgr::GetId(*accountName);
            if (!accountId)
            {
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName->c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

        }
        else
        {
            // 如果没有指定账号名，使用选中的玩家
            Player* player = handler->getSelectedPlayer();
            if (!player)
                return false;

            accountId = player->GetSession()->GetAccountId();
            accountName.emplace();
            AccountMgr::GetName(accountId, *accountName);
        }

        // 只允许设置权限低于自己的账号，或者自己的账号
        if (handler->GetSession() && handler->GetSession()->GetAccountId() != accountId &&
            handler->HasLowerSecurityAccount(nullptr, accountId, true))
            return false;

        // 验证扩展包等级不超过服务器配置
        if (expansion > sWorld->getIntConfig(CONFIG_EXPANSION))
            return false;

        // 更新数据库中的扩展包设置
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_EXPANSION);

        stmt->setUInt8(0, expansion);
        stmt->setUInt32(1, accountId);

        LoginDatabase.Execute(stmt);

        handler->PSendSysMessage(LANG_ACCOUNT_SETADDON, accountName->c_str(), accountId, expansion);
        return true;
    }

    /**
     * @brief 设置账号权限等级（管理员命令）
     *
     * 为指定账号设置GM权限等级。可以指定作用域为特定Realm或所有Realm。
     *
     * @param handler 聊天命令处理器
     * @param accountName 可选参数，账号名称（如未指定则使用选中玩家）
     * @param securityLevel 权限等级（0-99，不能设置控制台级别）
     * @param realmId 可选参数，Realm ID（-1表示所有Realm）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 解析账号名或使用选中玩家的账号
     * 2. 验证权限等级不等于或高于控制台级别
     * 3. 确定操作者的权限等级（控制台或普通账号）
     * 4. 权限检查：
     *    - 目标账号的当前权限不能高于或等于操作者
     *    - 要设置的权限等级不能高于或等于操作者
     *    - 不能修改自己的权限等级
     * 5. 特殊检查：如果设置为全局（realmID=-1），验证目标账号在其他Realm没有更高权限
     * 6. 验证Realm ID的有效性
     * 7. 更新权限设置
     *
     * 安全机制：
     * - 严格的权限等级检查，防止越权操作
     * - 不能提升权限到等于或高于自己的等级
     * - 不能修改权限高于或等于自己的账号
     * - 全局设置时检查所有Realm的权限
     *
     * 调用时机：管理员执行 .account set gmlevel/seclevel 命令时
     */
    static bool HandleAccountSetSecLevelCommand(ChatHandler* handler, Optional<std::string> accountName, uint8 securityLevel, Optional<int32> realmId)
    {
        uint32 accountId;
        if (accountName)
        {
            if (!Utf8ToUpperOnlyLatin(*accountName))
            {
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName->c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

            accountId = AccountMgr::GetId(*accountName);
            if (!accountId)
            {
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName->c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            Player* player = handler->getSelectedPlayer();
            if (!player)
                return false;
            accountId = player->GetSession()->GetAccountId();
            accountName.emplace();
            AccountMgr::GetName(accountId, *accountName);
        }

        if (securityLevel >= SEC_CONSOLE)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        int32 realmID = -1;
        if (realmId)
            realmID = *realmId;

        uint32 playerSecurity;
        if (handler->IsConsole())
            playerSecurity = SEC_CONSOLE;
        else
            playerSecurity = AccountMgr::GetSecurity(handler->GetSession()->GetAccountId(), realmID);

        // can set security level only for target with less security and to less security that we have
        // This also restricts setting handler's own security.
        uint32 targetSecurity = AccountMgr::GetSecurity(accountId, realmID);
        if (targetSecurity >= playerSecurity || securityLevel >= playerSecurity)
        {
            handler->SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Check and abort if the target gm has a higher rank on one of the realms and the new realm is -1
        if (realmID == -1 && !AccountMgr::IsConsoleAccount(playerSecurity))
        {
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_ACCESS_SECLEVEL_TEST);

            stmt->setUInt32(0, accountId);
            stmt->setUInt8(1, securityLevel);

            PreparedQueryResult result = LoginDatabase.Query(stmt);

            if (result)
            {
                handler->SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        // Check if provided realmID has a negative value other than -1
        if (realmID < -1)
        {
            handler->SendSysMessage(LANG_INVALID_REALMID);
            handler->SetSentErrorMessage(true);
            return false;
        }

        WorldSession const* session = sWorld->FindSession(accountId);
        sAccountMgr->UpdateAccountAccess(session ? session->GetRBACData() : nullptr, accountId, securityLevel, realmID);

        handler->PSendSysMessage(LANG_YOU_CHANGE_SECURITY, accountName->c_str(), securityLevel);
        return true;
    }

    /**
     * @brief 设置账号密码（管理员命令）
     *
     * 为指定账号设置新密码。这是管理员操作，不需要验证旧密码。
     *
     * @param handler 聊天命令处理器
     * @param accountName 账号名称
     * @param password 新密码
     * @param confirmPassword 新密码确认
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 将账号名转换为标准格式
     * 2. 验证账号是否存在
     * 3. 权限检查：只能为权限低于自己的账号设置密码
     * 4. 验证两次输入的密码是否一致
     * 5. 调用 AccountMgr 修改密码
     *
     * 安全机制：
     * - 权限等级检查：不能修改权限高于或等于自己的账号密码
     * - 不能修改自己的密码（应使用 .account password 命令）
     * - 两次密码确认防止输入错误
     *
     * 调用时机：管理员执行 .account set password 命令时
     */
    /// Set password for account
    static bool HandleAccountSetPasswordCommand(ChatHandler* handler, std::string accountName, std::string const& password, std::string const& confirmPassword)
    {
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 targetAccountId = AccountMgr::GetId(accountName);
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        /// can set password only for target with less security
        /// This also restricts setting handler's own password
        if (handler->HasLowerSecurityAccount(nullptr, targetAccountId, true))
            return false;

        if (password != confirmPassword)
        {
            handler->SendSysMessage(LANG_NEW_PASSWORDS_NOT_MATCH);
            handler->SetSentErrorMessage(true);
            return false;
        }

        AccountOpResult result = AccountMgr::ChangePassword(targetAccountId, password);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->SendSysMessage(LANG_COMMAND_PASSWORD);
                break;
            case AccountOpResult::AOR_NAME_NOT_EXIST:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_PASS_TOO_LONG:
                handler->SendSysMessage(LANG_PASSWORD_TOO_LONG);
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->SendSysMessage(LANG_COMMAND_NOTCHANGEPASSWORD);
                handler->SetSentErrorMessage(true);
                return false;
        }
        return true;
    }

    /**
     * @brief 设置账号双因素认证（管理员命令）
     *
     * 为指定账号设置或移除双因素认证。可以手动设置密钥或关闭功能。
     *
     * @param handler 聊天命令处理器
     * @param accountName 账号名称
     * @param secret TOTP密钥（Base32编码），传入 "off" 则移除双因素认证
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 验证账号名并转换为标准格式
     * 2. 验证账号是否存在
     * 3. 权限检查：只能操作权限低于自己的账号
     * 4. 如果 secret 为 "off"，清空数据库中的TOTP密钥
     * 5. 否则：
     *    - 验证服务器TOTP主密钥配置
     *    - Base32解码密钥
     *    - 验证密钥长度（加密后不超过128字节）
     *    - 使用AES加密密钥
     *    - 保存到数据库
     *
     * 安全机制：
     * - 密钥使用AES加密后存储
     * - 权限等级检查防止越权操作
     * - 密钥长度验证防止溢出
     *
     * 调用时机：管理员执行 .account set 2fa 命令时
     */
    static bool HandleAccountSet2FACommand(ChatHandler* handler, std::string accountName, std::string secret)
    {
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 targetAccountId = AccountMgr::GetId(accountName);
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (handler->HasLowerSecurityAccount(nullptr, targetAccountId, true))
            return false;

        if (secret == "off")
        {
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET);
            stmt->setNull(0);
            stmt->setUInt32(1, targetAccountId);
            LoginDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_2FA_REMOVE_COMPLETE);
            return true;
        }

        auto const& masterKey = sSecretMgr->GetSecret(SECRET_TOTP_MASTER_KEY);
        if (!masterKey.IsAvailable())
        {
            handler->SendSysMessage(LANG_2FA_COMMANDS_NOT_SETUP);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Optional<std::vector<uint8>> decoded = Trinity::Encoding::Base32::Decode(secret);
        if (!decoded)
        {
            handler->SendSysMessage(LANG_2FA_SECRET_INVALID);
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (128 < (decoded->size() + Trinity::Crypto::AES::IV_SIZE_BYTES + Trinity::Crypto::AES::TAG_SIZE_BYTES))
        {
            handler->SendSysMessage(LANG_2FA_SECRET_TOO_LONG);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (masterKey)
            Trinity::Crypto::AEEncryptWithRandomIV<Trinity::Crypto::AES>(*decoded, *masterKey);

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET);
        stmt->setBinary(0, *decoded);
        stmt->setUInt32(1, targetAccountId);
        LoginDatabase.Execute(stmt);
        handler->PSendSysMessage(LANG_2FA_SECRET_SET_COMPLETE, accountName.c_str());
        return true;
    }

    /**
     * @brief 设置账号邮箱（管理员命令）
     *
     * 为指定账号设置普通邮箱地址。这是管理员操作，不需要验证旧邮箱或密码。
     *
     * @param handler 聊天命令处理器
     * @param accountName 账号名称
     * @param email 新邮箱地址
     * @param confirmEmail 新邮箱确认（需要再次输入）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 将账号名转换为标准格式
     * 2. 验证账号是否存在
     * 3. 权限检查：只能为权限低于自己的账号设置邮箱
     * 4. 验证两次输入的邮箱是否一致
     * 5. 调用 AccountMgr 修改邮箱
     * 6. 记录操作日志
     *
     * 安全机制：
     * - 权限等级检查：不能修改权限高于或等于自己的账号邮箱
     * - 不能修改自己的邮箱（应使用 .account email 命令）
     * - 两次邮箱确认防止输入错误
     *
     * 调用时机：管理员执行 .account set sec email 命令时
     */
    /// Set normal email for account
    static bool HandleAccountSetEmailCommand(ChatHandler* handler, std::string accountName, std::string const& email, std::string const& confirmEmail)
    {
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 targetAccountId = AccountMgr::GetId(accountName);
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        /// can set email only for target with less security
        /// This also restricts setting handler's own email.
        if (handler->HasLowerSecurityAccount(nullptr, targetAccountId, true))
            return false;

        if (email != confirmEmail)
        {
            handler->SendSysMessage(LANG_NEW_EMAILS_NOT_MATCH);
            handler->SetSentErrorMessage(true);
            return false;
        }

        AccountOpResult result = AccountMgr::ChangeEmail(targetAccountId, email);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->SendSysMessage(LANG_COMMAND_EMAIL);
                TC_LOG_INFO("entities.player.character", "ChangeEmail: Account {} [Id: {}] had it's email changed to {}.",
                    accountName, targetAccountId, email);
                break;
            case AccountOpResult::AOR_NAME_NOT_EXIST:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_EMAIL_TOO_LONG:
                handler->SendSysMessage(LANG_EMAIL_TOO_LONG);
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->SendSysMessage(LANG_COMMAND_NOTCHANGEEMAIL);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 设置账号注册邮箱（管理员命令）
     *
     * 为指定账号设置注册邮箱地址。注册邮箱通常用于账号找回等重要操作。
     *
     * @param handler 聊天命令处理器
     * @param accountName 账号名称
     * @param email 新注册邮箱地址
     * @param confirmEmail 新邮箱确认（需要再次输入）
     *
     * @return bool 设置成功返回 true，失败返回 false
     *
     * @details 执行流程：
     * 1. 将账号名转换为标准格式
     * 2. 验证账号是否存在
     * 3. 权限检查：只能为权限低于自己的账号设置注册邮箱
     * 4. 验证两次输入的邮箱是否一致
     * 5. 调用 AccountMgr 修改注册邮箱
     * 6. 记录操作日志
     *
     * 安全机制：
     * - 权限等级检查：不能修改权限高于或等于自己的账号注册邮箱
     * - 不能修改自己的注册邮箱（应使用其他命令）
     * - 两次邮箱确认防止输入错误
     * - 所有修改都会记录日志
     *
     * 调用时机：管理员执行 .account set sec regmail 命令时
     */
    /// Change registration email for account
    static bool HandleAccountSetRegEmailCommand(ChatHandler* handler, std::string accountName, std::string const& email, std::string const& confirmEmail)
    {
        if (!Utf8ToUpperOnlyLatin(accountName))
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 targetAccountId = AccountMgr::GetId(accountName);
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        /// can set email only for target with less security
        /// This also restricts setting handler's own email.
        if (handler->HasLowerSecurityAccount(nullptr, targetAccountId, true))
            return false;

        if (email != confirmEmail)
        {
            handler->SendSysMessage(LANG_NEW_EMAILS_NOT_MATCH);
            handler->SetSentErrorMessage(true);
            return false;
        }

        AccountOpResult result = AccountMgr::ChangeRegEmail(targetAccountId, email);
        switch (result)
        {
            case AccountOpResult::AOR_OK:
                handler->SendSysMessage(LANG_COMMAND_EMAIL);
                TC_LOG_INFO("entities.player.character", "ChangeRegEmail: Account {} [Id: {}] had it's Registration Email changed to {}.",
                    accountName, targetAccountId, email);
                break;
            case AccountOpResult::AOR_NAME_NOT_EXIST:
                handler->PSendSysMessage(LANG_ACCOUNT_NOT_EXIST, accountName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case AccountOpResult::AOR_EMAIL_TOO_LONG:
                handler->SendSysMessage(LANG_EMAIL_TOO_LONG);
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->SendSysMessage(LANG_COMMAND_NOTCHANGEEMAIL);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }
};

/**
 * @brief 注册账号命令脚本
 *
 * 此函数在服务器启动时被调用，用于创建并注册账号命令脚本实例。
 * 该函数由脚本管理系统自动调用。
 *
 * 调用时机：服务器初始化时，由脚本加载系统调用
 */
void AddSC_account_commandscript()
{
    new account_commandscript();
}
