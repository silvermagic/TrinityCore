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

/* ScriptData
Name: titles_commandscript
%Complete: 100
Comment: All titles related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_titles.cpp
 * @brief 称号管理命令模块
 *
 * 本模块实现了所有与玩家称号管理相关的GM命令,包括:
 * - 添加称号(.titles add)
 * - 移除称号(.titles remove)
 * - 设置当前称号(.titles current)
 * - 设置称号掩码(.titles set mask)
 *
 * 称号是游戏中玩家获得的一种荣誉标识,显示在角色名称前或后。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

/**
 * @class titles_commandscript
 * @brief 称号管理命令脚本类
 *
 * 继承自CommandScript,提供称号管理相关的所有GM命令处理函数。
 */
class titles_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化称号命令脚本,注册脚本名称为"titles_commandscript"
     */
    titles_commandscript() : CommandScript("titles_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回称号命令表结构
     *
     * 构建并返回所有称号相关命令的层次结构,包括:
     * - titles add: 添加称号
     * - titles current: 设置当前称号
     * - titles remove: 移除称号
     * - titles set mask: 设置称号掩码
     */
    ChatCommandTable GetCommands() const override
    {
        // 设置称号子命令表
        static ChatCommandTable titlesSetCommandTable =
        {
            { "mask", HandleTitlesSetMaskCommand, rbac::RBAC_PERM_COMMAND_TITLES_SET_MASK, Console::No },
        };

        // 主称号命令表
        static ChatCommandTable titlesCommandTable =
        {
            { "add",     HandleTitlesAddCommand,     rbac::RBAC_PERM_COMMAND_TITLES_ADD,     Console::No },
            { "current", HandleTitlesCurrentCommand, rbac::RBAC_PERM_COMMAND_TITLES_CURRENT, Console::No },
            { "remove",  HandleTitlesRemoveCommand,  rbac::RBAC_PERM_COMMAND_TITLES_REMOVE,  Console::No },
            { "set",     titlesSetCommandTable },
        };

        // 根命令表
        static ChatCommandTable commandTable =
        {
            { "titles", titlesCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理设置当前称号命令
     * @param handler 聊天处理器指针
     * @param titleId 称号ID
     * @return true 命令执行成功, false 称号ID无效
     *
     * @par 调用时机:
     * 当GM执行.titles current <titleId>命令时调用
     *
     * @par 功能说明:
     * 将指定称号设置为选中玩家的当前称号,并添加该称号到玩家已知称号列表
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TITLES_CURRENT权限
     * - 只能由游戏内玩家执行(不支持控制台)
     */
    static bool HandleTitlesCurrentCommand(ChatHandler* handler, Variant<Hyperlink<title>, uint16> titleId)
    {
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查权限等级
        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 验证称号ID有效性
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
        if (!titleInfo)
        {
            handler->PSendSysMessage(LANG_INVALID_TITLE_ID, titleId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string tNameLink = handler->GetNameLink(target);
        // 根据玩家性别获取称号名称
        std::string titleNameStr = fmt::sprintf(target->GetNativeGender() == GENDER_MALE ? titleInfo->Name[handler->GetSessionDbcLocale()] : titleInfo->Name1[handler->GetSessionDbcLocale()], target->GetName());

        // 添加称号并设置为当前称号
        target->SetTitle(titleInfo);
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE, titleInfo->MaskID);

        handler->PSendSysMessage(LANG_TITLE_CURRENT_RES, titleId, titleNameStr, tNameLink);

        return true;
    }

    /**
     * @brief 处理添加称号命令
     * @param handler 聊天处理器指针
     * @param titleId 称号ID
     * @return true 命令执行成功, false 称号ID无效
     *
     * @par 调用时机:
     * 当GM执行.titles add <titleId>命令时调用
     *
     * @par 功能说明:
     * 将指定称号添加到选中玩家的已知称号列表,但不设置为当前称号
     */
    static bool HandleTitlesAddCommand(ChatHandler* handler, Variant<Hyperlink<title>, uint16> titleId)
    {
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查权限等级
        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 验证称号ID有效性
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
        if (!titleInfo)
        {
            handler->PSendSysMessage(LANG_INVALID_TITLE_ID, titleId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string tNameLink = handler->GetNameLink(target);
        std::string titleNameStr = fmt::sprintf(target->GetNativeGender() == GENDER_MALE ? titleInfo->Name[handler->GetSessionDbcLocale()] : titleInfo->Name1[handler->GetSessionDbcLocale()], target->GetName());

        // 添加称号到已知列表
        target->SetTitle(titleInfo);
        handler->PSendSysMessage(LANG_TITLE_ADD_RES, titleId, titleNameStr, tNameLink);

        return true;
    }

    /**
     * @brief 处理移除称号命令
     * @param handler 聊天处理器指针
     * @param titleId 称号ID
     * @return true 命令执行成功, false 称号ID无效
     *
     * @par 调用时机:
     * 当GM执行.titles remove <titleId>命令时调用
     *
     * @par 功能说明:
     * 从选中玩家的已知称号列表中移除指定称号
     * 如果移除的是当前称号,会重置当前称号
     */
    static bool HandleTitlesRemoveCommand(ChatHandler* handler, Variant<Hyperlink<title>, uint16> titleId)
    {
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查权限等级
        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 验证称号ID有效性
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
        if (!titleInfo)
        {
            handler->PSendSysMessage(LANG_INVALID_TITLE_ID, titleId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 移除称号(第二个参数true表示移除)
        target->SetTitle(titleInfo, true);

        std::string tNameLink = handler->GetNameLink(target);
        std::string titleNameStr = fmt::sprintf(target->GetNativeGender() == GENDER_MALE ? titleInfo->Name[handler->GetSessionDbcLocale()] : titleInfo->Name1[handler->GetSessionDbcLocale()], target->GetName());

        handler->PSendSysMessage(LANG_TITLE_REMOVE_RES, titleId, titleNameStr, tNameLink);

        // 如果移除的是当前称号,重置当前称号
        if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
        {
            target->SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);
            handler->PSendSysMessage(LANG_CURRENT_TITLE_RESET, tNameLink);
        }

        return true;
    }

    /**
     * @brief 处理设置称号掩码命令
     * @param handler 聊天处理器指针
     * @param mask 称号掩码值
     * @return true 命令执行成功
     *
     * @par 调用时机:
     * 当GM执行.titles set mask <mask>命令时调用
     *
     * @par 功能说明:
     * 直接设置玩家的已知称号掩码,可以批量设置多个称号
     * 掩码中的每一位对应一个称号
     * 会自动过滤掉数据库中不存在的称号位
     *
     * @note 这是一个高级命令,需要了解称号掩码的工作原理
     */
    //Edit Player KnownTitles
    static bool HandleTitlesSetMaskCommand(ChatHandler* handler, uint64 mask)
    {
        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查权限等级
        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        uint64 titles2 = mask;

        // 过滤掉数据库中不存在的称号位
        for (uint32 i = 1; i < sCharTitlesStore.GetNumRows(); ++i)
            if (CharTitlesEntry const* tEntry = sCharTitlesStore.LookupEntry(i))
                titles2 &= ~(uint64(1) << tEntry->MaskID);

        mask &= ~titles2;                                     // 移除不存在的称号

        // 设置称号掩码
        target->SetUInt64Value(PLAYER__FIELD_KNOWN_TITLES, mask);
        handler->SendSysMessage(LANG_DONE);

        // 如果当前称号不在新掩码中,重置当前称号
        if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
        {
            target->SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);
            handler->PSendSysMessage(LANG_CURRENT_TITLE_RESET, handler->GetNameLink(target));
        }

        return true;
    }
};

/**
 * @brief 注册称号命令脚本
 *
 * 此函数在服务器启动时被脚本系统调用,用于注册称号命令脚本。
 * 创建titles_commandscript实例并将其添加到命令处理系统中。
 */
void AddSC_titles_commandscript()
{
    new titles_commandscript();
}
