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
 * @file cs_character.cpp
 * @brief 角色管理命令模块
 *
 * 本模块提供了角色管理相关的所有GM命令，包括：
 * - 角色定制、改名、种族/阵营转换
 * - 角色等级调整、声望查询
 * - 已删除角色的查询、恢复和彻底删除
 * - 角色数据导入导出
 * - 账号转移等管理功能
 *
 * 主要命令：
 * - .character customize - 角色外观定制
 * - .character rename - 角色改名
 * - .character level - 设置角色等级
 * - .character reputation - 查询角色声望
 * - .character deleted - 管理已删除的角色
 * - .pdump - 角色数据导入导出
 */

/* ScriptData
Name: character_commandscript
%Complete: 100
Comment: All character related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerDump.h"
#include "ReputationMgr.h"
#include "World.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class character_commandscript
 * @brief 角色管理命令脚本类
 *
 * 该类继承自CommandScript，负责注册和处理所有与角色管理相关的GM命令。
 * 提供角色的创建、修改、删除、查询等全方位管理功能。
 */
class character_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化角色命令脚本，注册脚本名称为"character_commandscript"
     */
    character_commandscript() : CommandScript("character_commandscript") { }

    /**
     * @brief 获取命令表
     *
     * 注册所有角色管理相关的命令及其处理函数
     *
     * @return ChatCommandTable 返回命令表，包含所有注册的命令
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable pdumpCommandTable =
        {
            { "copy",          HandlePDumpCopyCommand,               rbac::RBAC_PERM_COMMAND_PDUMP_COPY,                Console::Yes },
            { "load",          HandlePDumpLoadCommand,               rbac::RBAC_PERM_COMMAND_PDUMP_LOAD,                Console::Yes },
            { "write",         HandlePDumpWriteCommand,              rbac::RBAC_PERM_COMMAND_PDUMP_WRITE,               Console::Yes },
        };
        static ChatCommandTable characterDeletedCommandTable =
        {
            { "delete",        HandleCharacterDeletedDeleteCommand,  rbac::RBAC_PERM_COMMAND_CHARACTER_DELETED_DELETE,  Console::Yes },
            { "list",          HandleCharacterDeletedListCommand,    rbac::RBAC_PERM_COMMAND_CHARACTER_DELETED_LIST,    Console::Yes },
            { "restore",       HandleCharacterDeletedRestoreCommand, rbac::RBAC_PERM_COMMAND_CHARACTER_DELETED_RESTORE, Console::Yes },
            { "old",           HandleCharacterDeletedOldCommand,     rbac::RBAC_PERM_COMMAND_CHARACTER_DELETED_OLD,     Console::Yes },
        };

        static ChatCommandTable characterCommandTable =
        {
            { "customize",     HandleCharacterCustomizeCommand,      rbac::RBAC_PERM_COMMAND_CHARACTER_CUSTOMIZE,       Console::Yes },
            { "changefaction", HandleCharacterChangeFactionCommand,  rbac::RBAC_PERM_COMMAND_CHARACTER_CHANGEFACTION,   Console::Yes },
            { "changerace",    HandleCharacterChangeRaceCommand,     rbac::RBAC_PERM_COMMAND_CHARACTER_CHANGERACE,      Console::Yes },
            { "changeaccount", HandleCharacterChangeAccountCommand,  rbac::RBAC_PERM_COMMAND_CHARACTER_CHANGEACCOUNT,   Console::Yes },
            { "deleted",       characterDeletedCommandTable },
            { "erase",         HandleCharacterEraseCommand,          rbac::RBAC_PERM_COMMAND_CHARACTER_ERASE,           Console::Yes },
            { "level",         HandleCharacterLevelCommand,          rbac::RBAC_PERM_COMMAND_CHARACTER_LEVEL,           Console::Yes },
            { "rename",        HandleCharacterRenameCommand,         rbac::RBAC_PERM_COMMAND_CHARACTER_RENAME,          Console::Yes },
            { "reputation",    HandleCharacterReputationCommand,     rbac::RBAC_PERM_COMMAND_CHARACTER_REPUTATION,      Console::Yes },
            { "titles",        HandleCharacterTitlesCommand,         rbac::RBAC_PERM_COMMAND_CHARACTER_TITLES,          Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "character", characterCommandTable },
            { "levelup",       HandleLevelUpCommand,                 rbac::RBAC_PERM_COMMAND_LEVELUP,                   Console::No },
            { "pdump", pdumpCommandTable },
        };
        return commandTable;
    }

    /**
     * @struct DeletedInfo
     * @brief 已删除角色的信息结构体
     *
     * 用于存储已删除角色的基本信息，便于查询、恢复和管理
     */
    struct DeletedInfo
    {
        ObjectGuid  guid;                               ///< 角色的GUID
        std::string name;                               ///< 角色名称
        uint32      accountId;                          ///< 账号ID
        std::string accountName;                        ///< 账号名称
        time_t      deleteDate;                         ///< 角色删除的时间戳
    };

    /// 已删除角色信息列表类型
    typedef std::list<DeletedInfo> DeletedInfoList;

    /**
     * @brief 获取已删除角色的信息列表
     *
     * 从数据库中查询所有匹配搜索条件的已删除角色信息
     *
     * @param foundList 输出参数，用于存储查询到的已删除角色信息列表
     * @param searchString 搜索字符串，可以是角色GUID或角色名称的一部分
     * @return bool 查询成功返回true，出现错误返回false
     *
     * @note 当searchString为空时，返回所有已删除的角色
     *       搜索时优先按GUID查找，如果无法解析为GUID则按名称查找
     */
    static bool GetDeletedCharacterInfoList(DeletedInfoList& foundList, std::string& searchString)
    {
        PreparedQueryResult result;
        CharacterDatabasePreparedStatement* stmt;

        // 根据搜索字符串决定查询方式
        if (!searchString.empty())
        {
            // 尝试将搜索字符串解析为GUID
            if (Optional<uint32> guidValue = Trinity::StringTo<uint64>(searchString))
            {
                // 按GUID查询已删除角色
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_DEL_INFO_BY_GUID);
                stmt->setUInt32(0, *guidValue);
                result = CharacterDatabase.Query(stmt);
            }
            else
            {
                // 按名称查询已删除角色
                if (!normalizePlayerName(searchString))
                    return false;

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_DEL_INFO_BY_NAME);
                stmt->setString(0, searchString);
                result = CharacterDatabase.Query(stmt);
            }
        }
        else
        {
            // 查询所有已删除角色
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_DEL_INFO);
            result = CharacterDatabase.Query(stmt);
        }

        // 处理查询结果，填充已删除角色信息列表
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();

                DeletedInfo info;

                info.guid       = ObjectGuid(HighGuid::Player, fields[0].GetUInt32());
                info.name       = fields[1].GetString();
                info.accountId  = fields[2].GetUInt32();

                // 如果账号不存在，账号名称将为空
                AccountMgr::GetName(info.accountId, info.accountName);
                info.deleteDate = time_t(fields[3].GetUInt32());
                foundList.push_back(info);
            }
            while (result->NextRow());
        }

        return true;
    }

    /**
     * @brief 显示已删除角色的列表辅助函数
     *
     * 以表格形式输出所有已删除角色的信息，根据调用环境自动调整输出格式
     *
     * @param foundList 已删除角色信息列表
     * @param handler 聊天命令处理器
     *
     * @see HandleCharacterDeletedListCommand
     * @see HandleCharacterDeletedRestoreCommand
     * @see HandleCharacterDeletedDeleteCommand
     * @see DeletedInfoList
     *
     * @note 当在控制台调用时，输出格式更详细；
     *       当在游戏内调用时，输出格式更适合聊天框显示
     */
    static void HandleCharacterDeletedListHelper(DeletedInfoList const& foundList, ChatHandler* handler)
    {
        // 控制台输出时添加表格头和分隔线
        if (!handler->GetSession())
        {
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_HEADER);
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
        }

        // 遍历所有已删除角色，逐个输出信息
        for (DeletedInfoList::const_iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
        {
            std::string dateStr = TimeToTimestampStr(itr->deleteDate);

            // 根据调用环境选择不同的输出格式
            if (!handler->GetSession())
                handler->PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CONSOLE,
                    itr->guid.GetCounter(), itr->name.c_str(), itr->accountName.empty() ? "<Not existing>" : itr->accountName.c_str(),
                    itr->accountId, dateStr.c_str());
            else
                handler->PSendSysMessage(LANG_CHARACTER_DELETED_LIST_LINE_CHAT,
                    itr->guid.GetCounter(), itr->name.c_str(), itr->accountName.empty() ? "<Not existing>" : itr->accountName.c_str(),
                    itr->accountId, dateStr.c_str());
        }

        // 控制台输出时添加表格尾部分隔线
        if (!handler->GetSession())
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_BAR);
    }

    /**
     * @brief 恢复已删除角色的辅助函数
     *
     * 将已删除的角色恢复到其原来的账号中，并更新数据库和缓存
     *
     * @param delInfo 要恢复的角色信息
     * @param handler 聊天命令处理器
     *
     * @see HandleCharacterDeletedListHelper
     * @see HandleCharacterDeletedRestoreCommand
     * @see HandleCharacterDeletedDeleteCommand
     * @see DeletedInfoList
     *
     * @note 恢复前会进行以下检查：
     *       1. 账号是否存在
     *       2. 账号角色数量是否已达上限(10个)
     *       3. 是否存在同名角色
     */
    static void HandleCharacterDeletedRestoreHelper(DeletedInfo const& delInfo, ChatHandler* handler)
    {
        // 检查账号是否存在
        if (delInfo.accountName.empty())
        {
            handler->PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_ACCOUNT, delInfo.name.c_str(), delInfo.guid.GetCounter(), delInfo.accountId);
            return;
        }

        // 检查账号角色数量是否已达上限
        uint32 charcount = AccountMgr::GetCharactersCount(delInfo.accountId);
        if (charcount >= 10)
        {
            handler->PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_FULL, delInfo.name.c_str(), delInfo.guid.GetCounter(), delInfo.accountId);
            return;
        }

        // 检查是否存在同名角色
        if (sCharacterCache->GetCharacterGuidByName(delInfo.name))
        {
            handler->PSendSysMessage(LANG_CHARACTER_DELETED_SKIP_NAME, delInfo.name.c_str(), delInfo.guid.GetCounter(), delInfo.accountId);
            return;
        }

        // 在数据库中恢复角色
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_RESTORE_DELETE_INFO);
        stmt->setString(0, delInfo.name);
        stmt->setUInt32(1, delInfo.accountId);
        stmt->setUInt32(2, delInfo.guid.GetCounter());
        CharacterDatabase.Execute(stmt);

        // 将角色信息添加到缓存
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_NAME_DATA);
        stmt->setUInt32(0, delInfo.guid.GetCounter());
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            sCharacterCache->AddCharacterCacheEntry(delInfo.guid, delInfo.accountId, delInfo.name, (*result)[2].GetUInt8(), (*result)[0].GetUInt8(), (*result)[1].GetUInt8(), (*result)[3].GetUInt8());
    }

    /**
     * @brief 处理.character titles命令
     *
     * 显示指定玩家拥有的所有称号列表
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数，未指定时默认为当前目标或自己
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 输出格式："id (idx:idx) - [名称链接 语言] 已知 激活"
     *       只显示玩家已经获得的称号
     */
    static bool HandleCharacterTitlesCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        // 获取目标玩家，默认为当前目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player || !player->IsConnected())
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player const* target = player->GetConnectedPlayer();

        LocaleConstant loc = handler->GetSessionDbcLocale();
        char const* knownStr = handler->GetTrinityString(LANG_KNOWN);

        // 遍历CharTitles.dbc中的所有称号
        for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);

            // 只显示玩家已拥有的称号
            if (titleInfo && target->HasTitle(titleInfo))
            {
                // 根据玩家性别选择对应的称号名称
                char const* name = target->GetNativeGender() == GENDER_MALE ? titleInfo->Name[loc] : titleInfo->Name1[loc];
                if (!*name)
                    name = (target->GetNativeGender() == GENDER_MALE ? titleInfo->Name[sWorld->GetDefaultDbcLocale()] : titleInfo->Name1[sWorld->GetDefaultDbcLocale()]);
                if (!*name)
                    continue;

                // 检查该称号是否为当前激活状态
                char const* activeStr = "";
                if (target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->MaskID)
                    activeStr = handler->GetTrinityString(LANG_ACTIVE);

                std::string titleName = fmt::sprintf(name, player->GetName());

                // 根据调用环境选择不同的输出格式
                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->MaskID, id, titleName.c_str(), localeNames[loc], knownStr, activeStr);
                else
                    handler->PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->MaskID, name, localeNames[loc], knownStr, activeStr);
            }
        }

        return true;
    }

    /**
     * @brief 处理.character rename命令
     *
     * 强制角色改名或设置角色下次登录时改名标记
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @param newNameV 新角色名称，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 如果指定了新名称，则立即强制改名并踢下线；
     *       如果未指定新名称，则设置角色在下次登录时需要改名
     *
     * @warning 改名前会检查：
     *          1. 名称格式是否合法
     *          2. 名称是否已被占用
     *          3. 名称是否为保留名称
     *          4. GM权限是否足够
     */
    static bool HandleCharacterRenameCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, Optional<std::string_view> newNameV)
    {
        // 参数验证：如果提供了新名称但未指定玩家，则命令无效
        if (!player && newNameV)
            return false;

        // 获取目标玩家
        if (!player)
            player = PlayerIdentifier::FromTarget(handler);
        if (!player)
            return false;

        // 权限检查：GM权限必须高于目标玩家
        if (handler->HasLowerSecurity(nullptr, player->GetGUID()))
            return false;

        // 如果指定了新名称，立即执行改名
        if (newNameV)
        {
            std::string newName{ *newNameV };

            // 规范化角色名称（首字母大写，其余小写）
            if (!normalizePlayerName(newName))
            {
                handler->SendSysMessage(LANG_BAD_VALUE);
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 检查名称格式是否合法
            if (ObjectMgr::CheckPlayerName(newName, player->IsConnected() ? player->GetConnectedPlayer()->GetSession()->GetSessionDbcLocale() : sWorld->GetDefaultDbcLocale(), true) != CHAR_NAME_SUCCESS)
            {
                handler->SendSysMessage(LANG_BAD_VALUE);
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 检查是否为保留名称（仅限有相应权限的GM可使用）
            if (WorldSession* session = handler->GetSession())
            {
                if (!session->HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME) && sObjectMgr->IsReservedName(newName))
                {
                    handler->SendSysMessage(LANG_RESERVED_NAME);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }

            // 检查名称是否已被占用
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
            stmt->setString(0, newName);
            PreparedQueryResult result = CharacterDatabase.Query(stmt);
            if (result)
            {
                handler->PSendSysMessage(LANG_RENAME_PLAYER_ALREADY_EXISTS, newName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 从数据库中删除旧的 declined name（俄语等语言的格变名称）
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_DECLINED_NAME);
            stmt->setUInt32(0, player->GetGUID().GetCounter());
            CharacterDatabase.Execute(stmt);

            // 执行改名操作
            if (Player* target = player->GetConnectedPlayer())
            {
                // 玩家在线：直接改名并踢下线
                target->SetName(newName);

                if (WorldSession* session = target->GetSession())
                    session->KickPlayer("HandleCharacterRenameCommand GM Command renaming character");
            }
            else
            {
                // 玩家离线：在数据库中更新名称
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_NAME_BY_GUID);
                stmt->setString(0, newName);
                stmt->setUInt32(1, player->GetGUID().GetCounter());
                CharacterDatabase.Execute(stmt);
            }

            // 更新角色缓存
            sCharacterCache->UpdateCharacterData(*player, newName);

            handler->PSendSysMessage(LANG_RENAME_PLAYER_WITH_NEW_NAME, player->GetName().c_str(), newName.c_str());

            // 记录GM操作日志
            if (WorldSession* session = handler->GetSession())
            {
                if (Player* player = session->GetPlayer())
                    sLog->OutCommand(session->GetAccountId(), "GM {} (Account: {}) forced rename {} to player {} (Account: {})", player->GetName(), session->GetAccountId(), newName, player->GetName(), sCharacterCache->GetCharacterAccountIdByGuid(player->GetGUID()));
            }
            else
                sLog->OutCommand(0, "CONSOLE forced rename '{}' to '{}' ({})", player->GetName(), newName, player->GetGUID().ToString());
        }
        else
        {
            // 未指定新名称：设置角色在下次登录时需要改名
            if (Player* target = player->GetConnectedPlayer())
            {
                // 玩家在线：直接设置登录标记
                handler->PSendSysMessage(LANG_RENAME_PLAYER, handler->GetNameLink(target).c_str());
                target->SetAtLoginFlag(AT_LOGIN_RENAME);
            }
            else
            {
                // 玩家离线：检查权限后在数据库中设置登录标记
                if (handler->HasLowerSecurity(nullptr, player->GetGUID()))
                    return false;

                handler->PSendSysMessage(LANG_RENAME_PLAYER_GUID, handler->playerLink(*player).c_str(), player->GetGUID().GetCounter());

                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
                stmt->setUInt16(0, uint16(AT_LOGIN_RENAME));
                stmt->setUInt32(1, player->GetGUID().GetCounter());
                CharacterDatabase.Execute(stmt);
            }
        }

        return true;
    }

    /**
     * @brief 处理.character customize命令
     *
     * 设置角色下次登录时进入外观定制界面
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 玩家下次登录时可以重新定制角色外观（发型、脸型等）
     */
    static bool HandleCharacterCustomizeCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        // 获取目标玩家
        if (!player)
            player = PlayerIdentifier::FromTarget(handler);
        if (!player)
            return false;

        // 根据玩家在线状态执行相应操作
        if (Player* target = player->GetConnectedPlayer())
        {
            // 玩家在线：直接设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER, handler->GetNameLink(target).c_str());
            target->SetAtLoginFlag(AT_LOGIN_CUSTOMIZE);
        }
        else
        {
            // 玩家离线：在数据库中设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, handler->playerLink(*player).c_str(), player->GetGUID().GetCounter());
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
            stmt->setUInt16(0, static_cast<uint16>(AT_LOGIN_CUSTOMIZE));
            stmt->setUInt32(1, player->GetGUID().GetCounter());
            CharacterDatabase.Execute(stmt);
        }

        return true;
    }

    /**
     * @brief 处理.character changefaction命令
     *
     * 设置角色下次登录时进入阵营转换界面
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 玩家下次登录时可以转换阵营（联盟/部落）
     */
    static bool HandleCharacterChangeFactionCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        // 获取目标玩家
        if (!player)
            player = PlayerIdentifier::FromTarget(handler);
        if (!player)
            return false;

        // 根据玩家在线状态执行相应操作
        if (Player* target = player->GetConnectedPlayer())
        {
            // 玩家在线：直接设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER, handler->GetNameLink(target).c_str());
            target->SetAtLoginFlag(AT_LOGIN_CHANGE_FACTION);
        }
        else
        {
            // 玩家离线：在数据库中设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, handler->playerLink(*player).c_str(), player->GetGUID().GetCounter());
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
            stmt->setUInt16(0, uint16(AT_LOGIN_CHANGE_FACTION));
            stmt->setUInt32(1, player->GetGUID().GetCounter());
            CharacterDatabase.Execute(stmt);
        }

        return true;
    }

    /**
     * @brief 处理.character changerace命令
     *
     * 设置角色下次登录时进入种族转换界面
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 玩家下次登录时可以转换种族（仅限同阵营内）
     */
    static bool HandleCharacterChangeRaceCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        // 获取目标玩家
        if (!player)
            player = PlayerIdentifier::FromTarget(handler);
        if (!player)
            return false;

        // 根据玩家在线状态执行相应操作
        if (Player* target = player->GetConnectedPlayer())
        {
            // 玩家在线：直接设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER, handler->GetNameLink(target).c_str());
            target->SetAtLoginFlag(AT_LOGIN_CHANGE_RACE);
        }
        else
        {
            // 玩家离线：在数据库中设置登录标记
            handler->PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, handler->playerLink(*player).c_str(), player->GetGUID().GetCounter());
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
            stmt->setUInt16(0, uint16(AT_LOGIN_CHANGE_RACE));
            stmt->setUInt32(1, player->GetGUID().GetCounter());
            CharacterDatabase.Execute(stmt);
        }

        return true;
    }

    /**
     * @brief 处理.character changeaccount命令
     *
     * 将角色从一个账号转移到另一个账号
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @param newAccount 新账号标识
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 转移前会检查：
     *       1. 目标账号角色数量是否已达上限
     *       2. 如果角色在线，会先踢下线
     *
     * @warning 此操作会更新数据库和缓存，是一个不可逆操作
     */
    static bool HandleCharacterChangeAccountCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, AccountIdentifier newAccount)
    {
        // 获取目标玩家
        if (!player)
            player = PlayerIdentifier::FromTarget(handler);
        if (!player)
            return false;

        // 获取角色当前账号信息
        CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(player->GetGUID());
        if (!characterInfo)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 oldAccountId = characterInfo->AccountId;

        // 如果新旧账号相同，无需操作
        if (newAccount.GetID() == oldAccountId)
            return true;

        // 检查目标账号角色数量是否已达上限
        if (uint32 charCount = AccountMgr::GetCharactersCount(newAccount.GetID()))
        {
            if (charCount >= sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM))
            {
                handler->PSendSysMessage(LANG_ACCOUNT_CHARACTER_LIST_FULL, newAccount.GetName().c_str(), newAccount.GetID());
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        // 如果角色在线，先踢下线
        if (Player* onlinePlayer = player->GetConnectedPlayer())
            onlinePlayer->GetSession()->KickPlayer("HandleCharacterChangeAccountCommand GM Command transferring character to another account");

        // 在数据库中更新角色所属账号
        CharacterDatabasePreparedStatement* charStmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ACCOUNT_BY_GUID);
        charStmt->setUInt32(0, newAccount.GetID());
        charStmt->setUInt32(1, player->GetGUID().GetCounter());
        CharacterDatabase.DirectExecute(charStmt);

        // 更新两个账号的角色计数
        sWorld->UpdateRealmCharCount(oldAccountId);
        sWorld->UpdateRealmCharCount(newAccount.GetID());

        // 更新角色缓存中的账号ID
        sCharacterCache->UpdateCharacterAccountId(*player, newAccount.GetID());

        handler->PSendSysMessage(LANG_CHANGEACCOUNT_SUCCESS, player->GetName().c_str(), newAccount.GetName().c_str());

        // 记录GM操作日志
        std::string logString = Trinity::StringFormat("changed ownership of player {} ({}) from account {} to account {}", player->GetName(), player->GetGUID().ToString(), oldAccountId, newAccount.GetID());
        if (WorldSession* session = handler->GetSession())
        {
            if (Player* player = session->GetPlayer())
                sLog->OutCommand(session->GetAccountId(), "GM {} (Account: {}) {}", player->GetName(), session->GetAccountId(), logString);
        }
        else
            sLog->OutCommand(0, "{} {}", handler->GetTrinityString(LANG_CONSOLE), logString);
        return true;
    }

    /**
     * @brief 处理.character reputation命令
     *
     * 显示指定玩家的所有声望信息
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 输出格式："声望ID - 声望名称 语言 等级 (数值) [标记]"
     *       标记包括：可见、交战、和平、隐藏、强制隐藏、非激活等
     */
    static bool HandleCharacterReputationCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        // 获取目标玩家，默认为当前目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player || !player->IsConnected())
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player const* target = player->GetConnectedPlayer();
        LocaleConstant loc = handler->GetSessionDbcLocale();

        // 遍历玩家的所有声望
        FactionStateList const& targetFSL = target->GetReputationMgr().GetStateList();
        for (FactionStateList::const_iterator itr = targetFSL.begin(); itr != targetFSL.end(); ++itr)
        {
            FactionState const& faction = itr->second;
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction.ID);

            // 获取声望名称
            char const* factionName = factionEntry ? factionEntry->Name[loc] : "#Not found#";
            ReputationRank rank = target->GetReputationMgr().GetRank(factionEntry);
            std::string rankName = handler->GetTrinityString(ReputationRankStrIndex[rank]);

            std::ostringstream ss;

            // 根据调用环境选择不同的输出格式
            if (handler->GetSession())
                ss << faction.ID << " - |cffffffff|Hfaction:" << faction.ID << "|h[" << factionName << ' ' << localeNames[loc] << "]|h|r";
            else
                ss << faction.ID << " - " << factionName << ' ' << localeNames[loc];

            ss << ' ' << rankName << " (" << target->GetReputationMgr().GetReputation(factionEntry) << ')';

            // 添加声望标记
            if (faction.Flags & FACTION_FLAG_VISIBLE)
                ss << handler->GetTrinityString(LANG_FACTION_VISIBLE);
            if (faction.Flags & FACTION_FLAG_AT_WAR)
                ss << handler->GetTrinityString(LANG_FACTION_ATWAR);
            if (faction.Flags & FACTION_FLAG_PEACE_FORCED)
                ss << handler->GetTrinityString(LANG_FACTION_PEACE_FORCED);
            if (faction.Flags & FACTION_FLAG_HIDDEN)
                ss << handler->GetTrinityString(LANG_FACTION_HIDDEN);
            if (faction.Flags & FACTION_FLAG_INVISIBLE_FORCED)
                ss << handler->GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
            if (faction.Flags & FACTION_FLAG_INACTIVE)
                ss << handler->GetTrinityString(LANG_FACTION_INACTIVE);

            handler->SendSysMessage(ss.str().c_str());
        }

        return true;
    }

    /**
     * @brief 处理.character deleted list命令
     *
     * 显示所有匹配搜索条件的已删除角色列表
     *
     * @param handler 聊天命令处理器
     * @param needleStr 搜索字符串，可选参数，可以是角色GUID或角色名称的一部分
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleCharacterDeletedListHelper
     * @see HandleCharacterDeletedRestoreCommand
     * @see HandleCharacterDeletedDeleteCommand
     * @see DeletedInfoList
     *
     * @note 如果未找到任何匹配的已删除角色，会输出提示信息
     */
    static bool HandleCharacterDeletedListCommand(ChatHandler* handler, Optional<std::string_view> needleStr)
    {
        std::string needle;
        if (needleStr)
            needle.assign(*needleStr);

        // 查询已删除角色信息列表
        DeletedInfoList foundList;
        if (!GetDeletedCharacterInfoList(foundList, needle))
            return false;

        // 如果未找到任何已删除角色，输出警告
        if (foundList.empty())
        {
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 显示已删除角色列表
        HandleCharacterDeletedListHelper(foundList, handler);

        return true;
    }

    /**
     * @brief 处理.character deleted restore命令
     *
     * 恢复所有匹配搜索条件的已删除角色
     *
     * @param handler 聊天命令处理器
     * @param needle 搜索字符串，可以是角色GUID或角色名称的一部分
     * @param newCharName 新角色名称，可选参数，用于恢复时改名
     * @param newAccount 新账号，可选参数，用于恢复时转移到新账号
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see HandleCharacterDeletedRestoreHelper
     * @see HandleCharacterDeletedListCommand
     * @see HandleCharacterDeletedDeleteCommand
     *
     * @note 恢复时会自动调用列表显示命令以展示恢复的角色；
     *       如果指定了新名称但找到多个匹配角色，则会失败
     */
    static bool HandleCharacterDeletedRestoreCommand(ChatHandler* handler, std::string needle, Optional<std::string_view> newCharName, Optional<AccountIdentifier> newAccount)
    {
        // 查询已删除角色信息列表
        DeletedInfoList foundList;
        if (!GetDeletedCharacterInfoList(foundList, needle))
            return false;

        // 如果未找到任何已删除角色，输出警告
        if (foundList.empty())
        {
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 显示要恢复的角色列表
        handler->SendSysMessage(LANG_CHARACTER_DELETED_RESTORE);
        HandleCharacterDeletedListHelper(foundList, handler);

        // 如果未指定新名称，恢复所有找到的角色
        if (!newCharName)
        {
            // 遍历恢复所有角色（跳过账号不存在的角色）
            for (DeletedInfoList::iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
                HandleCharacterDeletedRestoreHelper(*itr, handler);
            return true;
        }

        // 如果指定了新名称，只能恢复单个角色
        if (foundList.size() == 1)
        {
            std::string newName{ *newCharName };
            DeletedInfo delInfo = foundList.front();

            // 更新角色名称
            delInfo.name = newName;

            // 如果提供了新账号，也更新账号信息
            if (newAccount)
            {
                delInfo.accountId = newAccount->GetID();
                delInfo.accountName = newAccount->GetName();
            }

            HandleCharacterDeletedRestoreHelper(delInfo, handler);
            return true;
        }

        // 如果找到多个角色但指定了新名称，报错
        handler->SendSysMessage(LANG_CHARACTER_DELETED_ERR_RENAME);
        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 处理.character deleted delete命令
     *
     * 彻底删除所有匹配搜索条件的已删除角色（无法恢复）
     *
     * @param handler 聊天命令处理器
     * @param needle 搜索字符串，可以是角色GUID或角色名称的一部分
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see Player::GetDeletedCharacterGUIDs
     * @see Player::DeleteFromDB
     * @see HandleCharacterDeletedListCommand
     * @see HandleCharacterDeletedRestoreCommand
     *
     * @warning 此操作不可逆，会永久删除角色数据
     */
    static bool HandleCharacterDeletedDeleteCommand(ChatHandler* handler, std::string needle)
    {
        // 查询已删除角色信息列表
        DeletedInfoList foundList;
        if (!GetDeletedCharacterInfoList(foundList, needle))
            return false;

        // 如果未找到任何已删除角色，输出警告
        if (foundList.empty())
        {
            handler->SendSysMessage(LANG_CHARACTER_DELETED_LIST_EMPTY);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 显示要删除的角色列表
        handler->SendSysMessage(LANG_CHARACTER_DELETED_DELETE);
        HandleCharacterDeletedListHelper(foundList, handler);

        // 彻底删除所有找到的角色（已删除角色的当前账号为0）
        for (DeletedInfoList::const_iterator itr = foundList.begin(); itr != foundList.end(); ++itr)
            Player::DeleteFromDB(itr->guid, 0, false, true);

        return true;
    }

    /**
     * @brief 处理.character deleted old命令
     *
     * 删除所有超过指定天数的已删除角色
     *
     * @param handler 聊天命令处理器
     * @param days 天数，可选参数，未指定时使用配置文件中的设置
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @see Player::DeleteOldCharacters
     * @see Player::DeleteFromDB
     * @see HandleCharacterDeletedDeleteCommand
     * @see HandleCharacterDeletedListCommand
     * @see HandleCharacterDeletedRestoreCommand
     *
     * @note 如果配置中的保留天数为0，则此命令不可用
     */
    static bool HandleCharacterDeletedOldCommand(ChatHandler* /*handler*/, Optional<uint16> days)
    {
        // 获取配置的保留天数
        int32 keepDays = static_cast<int32>(sWorld->getIntConfig(CONFIG_CHARDELETE_KEEP_DAYS));

        // 如果命令指定了天数，使用命令参数
        if (days)
            keepDays = static_cast<int32>(*days);
        else if (keepDays <= 0) // 配置值为0表示禁用，不能使用
            return false;

        // 删除超过指定天数的已删除角色
        Player::DeleteOldCharacters(static_cast<uint32>(keepDays));

        return true;
    }

    /**
     * @brief 处理.character erase命令
     *
     * 彻底删除指定角色（立即删除，无法恢复）
     *
     * @param handler 聊天命令处理器
     * @param player 要删除的目标玩家标识
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @warning 此操作不可逆，会永久删除角色数据；
     *          如果角色在线，会先踢下线再删除
     */
    static bool HandleCharacterEraseCommand(ChatHandler* handler, PlayerIdentifier player)
    {
        uint32 accountId;

        // 获取账号ID，如果角色在线则先踢下线
        if (Player* target = player.GetConnectedPlayer())
        {
            accountId = target->GetSession()->GetAccountId();
            target->GetSession()->KickPlayer("HandleCharacterEraseCommand GM Command deleting character");
        }
        else
            accountId = sCharacterCache->GetCharacterAccountIdByGuid(player);

        // 获取账号名称
        std::string accountName;
        AccountMgr::GetName(accountId, accountName);

        // 从数据库中彻底删除角色
        Player::DeleteFromDB(player, accountId, true, true);
        handler->PSendSysMessage(LANG_CHARACTER_DELETED, player.GetName().c_str(), player.GetGUID().GetCounter(), accountName.c_str(), accountId);

        return true;
    }

    /**
     * @brief 处理.character level命令
     *
     * 设置指定角色的等级
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @param newlevel 目标等级
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 等级范围限制在1到STRONG_MAX_LEVEL之间；
     *       如果角色在线，会重置天赋和经验值；
     *       如果角色离线，只更新数据库，其他数据在下次登录时更新
     */
    static bool HandleCharacterLevelCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, int16 newlevel)
    {
        // 获取目标玩家，默认为当前目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        // 获取当前等级
        uint8 oldlevel = static_cast<uint8>(player->IsConnected() ? player->GetConnectedPlayer()->GetLevel() : sCharacterCache->GetCharacterLevelByGuid(*player));

        // 限制等级范围
        if (newlevel < 1)
            newlevel = 1;

        if (newlevel > static_cast<int16>(STRONG_MAX_LEVEL))
            newlevel = static_cast<int16>(STRONG_MAX_LEVEL);

        // 根据玩家在线状态执行相应操作
        if (Player* target = player->GetConnectedPlayer())
        {
            // 玩家在线：设置等级、初始化天赋、重置经验值
            target->GiveLevel(static_cast<uint8>(newlevel));
            target->InitTalentForLevel();
            target->SetXP(0);

            // 通知目标玩家
            if (handler->needReportToTarget(target))
            {
                if (oldlevel == newlevel)
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_PROGRESS_RESET, handler->GetNameLink().c_str());
                else if (oldlevel < static_cast<uint8>(newlevel))
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_UP, handler->GetNameLink().c_str(), newlevel);
                else                                                // if (oldlevel > newlevel)
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_DOWN, handler->GetNameLink().c_str(), newlevel);
            }
        }
        else
        {
            // 玩家离线：在数据库中更新等级和重置经验值，其他数据在登录时更新
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_LEVEL);
            stmt->setUInt8(0, static_cast<uint8>(newlevel));
            stmt->setUInt32(1, player->GetGUID());
            CharacterDatabase.Execute(stmt);
        }

        // 通知GM
        if (!handler->GetSession() || (handler->GetSession()->GetPlayer() != player->GetConnectedPlayer()))
            handler->PSendSysMessage(LANG_YOU_CHANGE_LVL, handler->playerLink(*player).c_str(), newlevel);

        return true;
    }

    /**
     * @brief 处理.levelup命令
     *
     * 升级或降级指定角色（相对当前等级）
     *
     * @param handler 聊天命令处理器
     * @param player 目标玩家标识，可选参数
     * @param level 等级变化量，正数升级，负数降级
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 与.character level命令不同，此命令是相对变化；
     *       最终等级限制在1到STRONG_MAX_LEVEL之间
     */
    static bool HandleLevelUpCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, int16 level)
    {
        // 获取目标玩家，默认为当前目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        // 计算新等级
        uint8 oldlevel = static_cast<uint8>(player->IsConnected() ? player->GetConnectedPlayer()->GetLevel() : sCharacterCache->GetCharacterLevelByGuid(*player));
        int16 newlevel = static_cast<int16>(oldlevel) + level;

        // 限制等级范围
        if (newlevel < 1)
            newlevel = 1;

        if (newlevel > static_cast<int16>(STRONG_MAX_LEVEL))
            newlevel = static_cast<int16>(STRONG_MAX_LEVEL);

        // 根据玩家在线状态执行相应操作
        if (Player* target = player->GetConnectedPlayer())
        {
            // 玩家在线：设置等级、初始化天赋、重置经验值
            target->GiveLevel(static_cast<uint8>(newlevel));
            target->InitTalentForLevel();
            target->SetXP(0);

            // 通知目标玩家
            if (handler->needReportToTarget(target))
            {
                if (oldlevel == newlevel)
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_PROGRESS_RESET, handler->GetNameLink().c_str());
                else if (oldlevel < static_cast<uint8>(newlevel))
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_UP, handler->GetNameLink().c_str(), newlevel);
                else                                                // if (oldlevel > newlevel)
                    ChatHandler(target->GetSession()).PSendSysMessage(LANG_YOURS_LEVEL_DOWN, handler->GetNameLink().c_str(), newlevel);
            }
        }
        else
        {
            // 玩家离线：在数据库中更新等级和重置经验值
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_LEVEL);
            stmt->setUInt8(0, static_cast<uint8>(newlevel));
            stmt->setUInt32(1, player->GetGUID());
            CharacterDatabase.Execute(stmt);
        }

        // 通知GM
        if (!handler->GetSession() || (handler->GetSession()->GetPlayer() != player->GetConnectedPlayer()))
            handler->PSendSysMessage(LANG_YOU_CHANGE_LVL, handler->playerLink(*player).c_str(), newlevel);

        return true;
    }

    /**
     * @brief 处理.pdump copy命令
     *
     * 复制角色数据到指定账号
     *
     * @param handler 聊天命令处理器
     * @param player 源玩家标识
     * @param account 目标账号标识
     * @param characterName 新角色名称，可选参数
     * @param characterGUID 新角色GUID，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 此命令将角色数据导出到内存，然后导入到目标账号；
     *       如果不指定名称和GUID，将自动生成
     *
     * @warning 复制操作不会检查账号角色数量限制
     */
    static bool HandlePDumpCopyCommand(ChatHandler* handler, PlayerIdentifier player, AccountIdentifier account, Optional<std::string_view> characterName, Optional<ObjectGuid::LowType> characterGUID)
    {
        std::string name;

        // 验证目标角色名称和GUID
        if (!ValidatePDumpTarget(handler, name, characterName, characterGUID))
            return false;

        // 将角色数据导出到字符串
        std::string dump;
        switch (PlayerDumpWriter().WriteDumpToString(dump, player.GetGUID().GetCounter()))
        {
            case DUMP_SUCCESS:
                break;
            case DUMP_CHARACTER_DELETED:
                handler->PSendSysMessage(LANG_COMMAND_EXPORT_DELETED_CHAR);
                handler->SetSentErrorMessage(true);
                return false;
            case DUMP_FILE_OPEN_ERROR: // 此错误代码不应出现
            default:
                handler->PSendSysMessage(LANG_COMMAND_EXPORT_FAILED);
                handler->SetSentErrorMessage(true);
                return false;
        }

        // 从字符串导入角色数据到目标账号
        switch (PlayerDumpReader().LoadDumpFromString(dump, account, name, characterGUID.value_or(0)))
        {
            case DUMP_SUCCESS:
                break;
            case DUMP_TOO_MANY_CHARS:
                handler->PSendSysMessage(LANG_ACCOUNT_CHARACTER_LIST_FULL, account.GetName().c_str(), account.GetID());
                handler->SetSentErrorMessage(true);
                return false;
            case DUMP_FILE_OPEN_ERROR: // 此错误代码不应出现
            case DUMP_FILE_BROKEN: // 此错误代码不应出现
            default:
                handler->PSendSysMessage(LANG_COMMAND_IMPORT_FAILED);
                handler->SetSentErrorMessage(true);
                return false;
        }

        // 导入成功提示
        handler->PSendSysMessage(LANG_COMMAND_IMPORT_SUCCESS);

        return true;
    }

    /**
     * @brief 处理.pdump load命令
     *
     * 从文件加载角色数据到指定账号
     *
     * @param handler 聊天命令处理器
     * @param fileName 角色数据文件路径
     * @param account 目标账号标识
     * @param characterName 新角色名称，可选参数
     * @param characterGUID 新角色GUID，可选参数
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 文件格式通常为SQL脚本；
     *       如果不指定名称和GUID，将使用文件中的值
     */
    static bool HandlePDumpLoadCommand(ChatHandler* handler, std::string fileName, AccountIdentifier account, Optional<std::string_view> characterName, Optional<ObjectGuid::LowType> characterGUID)
    {
        std::string name;

        // 验证目标角色名称和GUID
        if (!ValidatePDumpTarget(handler, name, characterName, characterGUID))
            return false;

        // 从文件加载角色数据
        switch (PlayerDumpReader().LoadDumpFromFile(fileName, account, name, characterGUID.value_or(0)))
        {
            case DUMP_SUCCESS:
                handler->PSendSysMessage(LANG_COMMAND_IMPORT_SUCCESS);
                break;
            case DUMP_FILE_OPEN_ERROR:
                handler->PSendSysMessage(LANG_FILE_OPEN_FAIL, fileName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case DUMP_FILE_BROKEN:
                handler->PSendSysMessage(LANG_DUMP_BROKEN, fileName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case DUMP_TOO_MANY_CHARS:
                handler->PSendSysMessage(LANG_ACCOUNT_CHARACTER_LIST_FULL, account.GetName().c_str(), account.GetID());
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->PSendSysMessage(LANG_COMMAND_IMPORT_FAILED);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }

    /**
     * @brief 验证角色导入目标参数
     *
     * 检查指定的角色名称和GUID是否可用
     *
     * @param handler 聊天命令处理器
     * @param name 输出参数，规范化后的角色名称
     * @param characterName 角色名称，可选参数
     * @param characterGUID 角色GUID，可选参数
     * @return bool 验证通过返回true，失败返回false
     *
     * @note 验证包括：名称格式合法性、GUID是否已被占用
     */
    static bool ValidatePDumpTarget(ChatHandler* handler, std::string& name, Optional<std::string_view> characterName, Optional<ObjectGuid::LowType> characterGUID)
    {
        // 验证角色名称
        if (characterName)
        {
            name.assign(*characterName);

            // 规范化并验证名称格式
            if (!normalizePlayerName(name))
            {
                handler->PSendSysMessage(LANG_INVALID_CHARACTER_NAME);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (ObjectMgr::CheckPlayerName(name, sWorld->GetDefaultDbcLocale(), true) != CHAR_NAME_SUCCESS)
            {
                handler->PSendSysMessage(LANG_INVALID_CHARACTER_NAME);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        // 验证GUID是否已被占用
        if (characterGUID)
        {
            if (sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(HighGuid::Player, *characterGUID)))
            {
                handler->PSendSysMessage(LANG_CHARACTER_GUID_IN_USE, *characterGUID);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 处理.pdump write命令
     *
     * 将角色数据导出到文件
     *
     * @param handler 聊天命令处理器
     * @param fileName 输出文件路径
     * @param player 源玩家标识
     * @return bool 命令执行成功返回true，失败返回false
     *
     * @note 导出文件格式为SQL脚本，可用于.pdump load命令导入
     */
    static bool HandlePDumpWriteCommand(ChatHandler* handler, std::string fileName, PlayerIdentifier player)
    {
        // 将角色数据导出到文件
        switch (PlayerDumpWriter().WriteDumpToFile(fileName, player.GetGUID().GetCounter()))
        {
            case DUMP_SUCCESS:
                handler->PSendSysMessage(LANG_COMMAND_EXPORT_SUCCESS);
                break;
            case DUMP_FILE_OPEN_ERROR:
                handler->PSendSysMessage(LANG_FILE_OPEN_FAIL, fileName.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            case DUMP_CHARACTER_DELETED:
                handler->PSendSysMessage(LANG_COMMAND_EXPORT_DELETED_CHAR);
                handler->SetSentErrorMessage(true);
                return false;
            default:
                handler->PSendSysMessage(LANG_COMMAND_EXPORT_FAILED);
                handler->SetSentErrorMessage(true);
                return false;
        }

        return true;
    }
};

/**
 * @brief 注册角色命令脚本
 *
 * 创建并注册角色命令脚本实例到系统
 */
void AddSC_character_commandscript()
{
    new character_commandscript();
}
