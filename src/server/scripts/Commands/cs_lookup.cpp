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
 * @file cs_lookup.cpp
 * @brief 查找命令模块
 *
 * 本模块实现了游戏内各种资源的查找功能,包括:
 * - 区域、生物、游戏事件、阵营的查找
 * - 物品、物品套装、游戏对象的查找
 * - 任务、技能、法术的查找
 * - 出租车站点、传送点、称号的查找
 * - 地图信息的查找
 * - 玩家账户相关查找(IP、账号、邮箱)
 *
 * 所有查找命令都支持模糊匹配和多语言搜索,并支持最大结果数限制。
 */

/* ScriptData
Name: lookup_commandscript
%Complete: 100
Comment: All lookup related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

/**
 * @class lookup_commandscript
 * @brief 查找命令脚本类
 *
 * 继承自 CommandScript,负责注册和处理所有查找相关的 GM 命令。
 * 提供对游戏中各类资源的搜索和查询功能,支持按名称、ID 等多种方式查找。
 */
class lookup_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化查找命令脚本,设置脚本名称为 "lookup_commandscript"
     */
    lookup_commandscript() : CommandScript("lookup_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回所有查找命令的注册表
     *
     * 注册所有 lookup 相关的子命令,包括:
     * - lookup player: 玩家查找命令组(ip/account/email)
     * - lookup area: 区域查找
     * - lookup creature: 生物查找
     * - lookup event: 事件查找
     * - lookup faction: 阵营查找
     * - lookup item/item id: 物品查找
     * - lookup item set: 物品套装查找
     * - lookup object: 游戏对象查找
     * - lookup quest/quest id: 任务查找
     * - lookup skill: 技能查找
     * - lookup spell/spell id: 法术查找
     * - lookup taxinode: 出租车站点查找
     * - lookup tele: 传送点查找
     * - lookup title: 称号查找
     * - lookup map/map id: 地图查找
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> lookupPlayerCommandTable =
        {
            { "ip",      rbac::RBAC_PERM_COMMAND_LOOKUP_PLAYER_IP,      true, &HandleLookupPlayerIpCommand,        "" },
            { "account", rbac::RBAC_PERM_COMMAND_LOOKUP_PLAYER_ACCOUNT, true, &HandleLookupPlayerAccountCommand,   "" },
            { "email",   rbac::RBAC_PERM_COMMAND_LOOKUP_PLAYER_EMAIL,   true, &HandleLookupPlayerEmailCommand,     "" },
        };

        static std::vector<ChatCommand> lookupCommandTable =
        {
            { "area",     rbac::RBAC_PERM_COMMAND_LOOKUP_AREA,     true, &HandleLookupAreaCommand,     "" },
            { "creature", rbac::RBAC_PERM_COMMAND_LOOKUP_CREATURE, true, &HandleLookupCreatureCommand, "" },
            { "event",    rbac::RBAC_PERM_COMMAND_LOOKUP_EVENT,    true, &HandleLookupEventCommand,    "" },
            { "faction",  rbac::RBAC_PERM_COMMAND_LOOKUP_FACTION,  true, &HandleLookupFactionCommand,  "" },
            { "item",     rbac::RBAC_PERM_COMMAND_LOOKUP_ITEM,     true, &HandleLookupItemCommand,     "" },
            { "item id",  rbac::RBAC_PERM_COMMAND_LOOKUP_ITEM_ID,  true, &HandleLookupItemIdCommand,   "" },
            { "item set", rbac::RBAC_PERM_COMMAND_LOOKUP_ITEMSET,  true, &HandleLookupItemSetCommand,  "" },
            { "object",   rbac::RBAC_PERM_COMMAND_LOOKUP_OBJECT,   true, &HandleLookupObjectCommand,   "" },
            { "quest",    rbac::RBAC_PERM_COMMAND_LOOKUP_QUEST,    true, &HandleLookupQuestCommand,    "" },
            { "quest id", rbac::RBAC_PERM_COMMAND_LOOKUP_QUEST_ID, true, &HandleLookupQuestIdCommand,  "" },
            { "player",   lookupPlayerCommandTable },
            { "skill",    rbac::RBAC_PERM_COMMAND_LOOKUP_SKILL,    true, &HandleLookupSkillCommand,    "" },
            { "spell",    rbac::RBAC_PERM_COMMAND_LOOKUP_SPELL,    true, &HandleLookupSpellCommand,    "" },
            { "spell id", rbac::RBAC_PERM_COMMAND_LOOKUP_SPELL_ID, true, &HandleLookupSpellIdCommand,  "" },
            { "taxinode", rbac::RBAC_PERM_COMMAND_LOOKUP_TAXINODE, true, &HandleLookupTaxiNodeCommand, "" },
            { "tele",     rbac::RBAC_PERM_COMMAND_LOOKUP_TELE,     true, &HandleLookupTeleCommand,     "" },
            { "title",    rbac::RBAC_PERM_COMMAND_LOOKUP_TITLE,    true, &HandleLookupTitleCommand,    "" },
            { "map",      rbac::RBAC_PERM_COMMAND_LOOKUP_MAP,      true, &HandleLookupMapCommand,      "" },
            { "map id",   rbac::RBAC_PERM_COMMAND_LOOKUP_MAP_ID,   true, &HandleLookupMapIdCommand,    "" },
        };

        static ChatCommandTable commandTable =
        {
            { "lookup", lookupCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理区域查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的区域名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup area 命令时
     * 性能注意事项:
     * - 遍历所有区域条目(AreaTable.dbc)
     * - 支持多语言搜索,在首选语言未找到时搜索其他语言
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [区域名称 语言]"
     */
    static bool HandleLookupAreaCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        // Search in AreaTable.dbc
        for (uint32 i = 0; i < sAreaTableStore.GetNumRows(); ++i)
        {
            AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(i);
            if (areaEntry)
            {
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = areaEntry->AreaName[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = areaEntry->AreaName[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send area in "id - [name]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << areaEntry->ID << " - |cffffffff|Harea:" << areaEntry->ID << "|h[" << name << ' ' << localeNames[locale]<< "]|h|r";
                    else
                        ss << areaEntry->ID << " - " << name << ' ' << localeNames[locale];

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOAREAFOUND);

        return true;
    }

    /**
     * @brief 处理生物查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的生物名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup creature 命令时
     * 性能注意事项:
     * - 遍历所有生物模板数据
     * - 优先搜索本地化名称,若未找到则搜索默认名称
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 游戏内显示为可点击链接,控制台显示纯文本
     */
    static bool HandleLookupCreatureCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        CreatureTemplateContainer const& ctc = sObjectMgr->GetCreatureTemplates();
        for (auto const& creatureTemplatePair : ctc)
        {
            uint32 id = creatureTemplatePair.first;
            uint8 localeIndex = handler->GetSessionDbLocaleIndex();
            if (CreatureLocale const* creatureLocale = sObjectMgr->GetCreatureLocale(id))
            {
                if (creatureLocale->Name.size() > localeIndex && !creatureLocale->Name[localeIndex].empty())
                {
                    std::string const& name = creatureLocale->Name[localeIndex];

                    if (Utf8FitTo(name, wNamePart))
                    {
                        if (maxResults && count++ == maxResults)
                        {
                            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                            return true;
                        }

                        if (handler->GetSession())
                            handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CHAT, id, id, name.c_str());
                        else
                            handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CONSOLE, id, name.c_str());

                        if (!found)
                            found = true;

                        continue;
                    }
                }
            }

            std::string const& name = creatureTemplatePair.second.Name;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CHAT, id, id, name.c_str());
                else
                    handler->PSendSysMessage(LANG_CREATURE_ENTRY_LIST_CONSOLE, id, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOCREATUREFOUND);

        return true;
    }

    /**
     * @brief 处理游戏事件查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的事件描述(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup event 命令时
     * 性能注意事项:
     * - 遍历所有游戏事件数据
     * - 显示事件是否处于激活状态
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [事件描述] [激活状态]"
     */
    static bool HandleLookupEventCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        for (uint32 id = 0; id < events.size(); ++id)
        {
            GameEventData const& eventData = events[id];

            std::string descr = eventData.description;
            if (descr.empty())
                continue;

            if (Utf8FitTo(descr, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                char const* active = activeEvents.find(id) != activeEvents.end() ? handler->GetTrinityString(LANG_ACTIVE) : "";

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT, id, id, eventData.description.c_str(), active);
                else
                    handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE, id, eventData.description.c_str(), active);

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_NOEVENTFOUND);

        return true;
    }

    /**
     * @brief 处理阵营查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的阵营名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup faction 命令时
     * 性能注意事项:
     * - 遍历所有阵营数据(Faction.dbc)
     * - 若有选中玩家,则显示该玩家对各个阵营的声望状态
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [阵营名称] 声望等级 (当前声望值) [状态标记]"
     * 状态标记包括: 可见、交战、强制和平、隐藏、强制不可见、未激活
     */
    static bool HandleLookupFactionCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // Can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower (wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(id);
            if (factionEntry)
            {
                FactionState const* factionState = target ? target->GetReputationMgr().GetState(factionEntry) : nullptr;

                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = factionEntry->Name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = factionEntry->Name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send faction in "id - [faction] rank reputation [visible] [at war] [own team] [unknown] [invisible] [inactive]" format
                    // or              "id - [faction] [no reputation]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << id << " - |cffffffff|Hfaction:" << id << "|h[" << name << ' ' << localeNames[locale] << "]|h|r";
                    else
                        ss << id << " - " << name << ' ' << localeNames[locale];

                    if (factionState) // and then target != NULL also
                    {
                        uint32 index = target->GetReputationMgr().GetReputationRankStrIndex(factionEntry);
                        std::string rankName = handler->GetTrinityString(index);

                        ss << ' ' << rankName << "|h|r (" << target->GetReputationMgr().GetReputation(factionEntry) << ')';

                        if (factionState->Flags & FACTION_FLAG_VISIBLE)
                            ss << handler->GetTrinityString(LANG_FACTION_VISIBLE);
                        if (factionState->Flags & FACTION_FLAG_AT_WAR)
                            ss << handler->GetTrinityString(LANG_FACTION_ATWAR);
                        if (factionState->Flags & FACTION_FLAG_PEACE_FORCED)
                            ss << handler->GetTrinityString(LANG_FACTION_PEACE_FORCED);
                        if (factionState->Flags & FACTION_FLAG_HIDDEN)
                            ss << handler->GetTrinityString(LANG_FACTION_HIDDEN);
                        if (factionState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
                            ss << handler->GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
                        if (factionState->Flags & FACTION_FLAG_INACTIVE)
                            ss << handler->GetTrinityString(LANG_FACTION_INACTIVE);
                    }
                    else
                        ss << handler->GetTrinityString(LANG_FACTION_NOREPUTATION);

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_FACTION_NOTFOUND);
        return true;
    }

    /**
     * @brief 处理物品名称查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的物品名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup item 命令时
     * 性能注意事项:
     * - 遍历所有物品模板数据(item_template 表)
     * - 优先搜索本地化名称,若未找到则搜索默认名称
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 游戏内显示为可点击的物品链接,控制台显示纯文本
     */
    static bool HandleLookupItemCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in `item_template`
        ItemTemplateContainer const& its = sObjectMgr->GetItemTemplateStore();
        for (auto const& itemTemplatePair : its)
        {
            uint8 localeIndex = handler->GetSessionDbLocaleIndex();
            if (ItemLocale const* il = sObjectMgr->GetItemLocale(itemTemplatePair.first))
            {
                if (il->Name.size() > localeIndex && !il->Name[localeIndex].empty())
                {
                    std::string const& name = il->Name[localeIndex];

                    if (Utf8FitTo(name, wNamePart))
                    {
                        if (maxResults && count++ == maxResults)
                        {
                            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                            return true;
                        }

                        if (handler->GetSession())
                            handler->PSendSysMessage(LANG_ITEM_LIST_CHAT, itemTemplatePair.first, itemTemplatePair.first, name.c_str());
                        else
                            handler->PSendSysMessage(LANG_ITEM_LIST_CONSOLE, itemTemplatePair.first, name.c_str());

                        if (!found)
                            found = true;

                        continue;
                    }
                }
            }

            std::string const& name = itemTemplatePair.second.Name1;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_ITEM_LIST_CHAT, itemTemplatePair.first, itemTemplatePair.first, name.c_str());
                else
                    handler->PSendSysMessage(LANG_ITEM_LIST_CONSOLE, itemTemplatePair.first, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOITEMFOUND);

        return true;
    }

    /**
     * @brief 处理物品ID查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的物品ID
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup item id 命令时
     * 性能注意事项:
     * - 直接通过ID查找物品模板,性能高效
     * - 只返回单个物品的查找结果
     *
     * 输出格式: 游戏内显示为可点击的物品链接,控制台显示纯文本
     */
    static bool HandleLookupItemIdCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 id = atoi((char*)args);

        if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(id))
        {
            std::string name = itemTemplate->Name1;

            if (name.empty())
            {
                handler->SendSysMessage(LANG_COMMAND_NOITEMFOUND);
                return true;
            }

            if (handler->GetSession())
                handler->PSendSysMessage(LANG_ITEM_LIST_CHAT, id, id, name.c_str());
            else
                handler->PSendSysMessage(LANG_ITEM_LIST_CONSOLE, id, name.c_str());
        }
        else
            handler->SendSysMessage(LANG_COMMAND_NOITEMFOUND);

        return true;
    }

    /**
     * @brief 处理物品套装查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的套装名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup item set 命令时
     * 性能注意事项:
     * - 遍历所有物品套装数据(ItemSet.dbc)
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [套装名称 语言]"
     */
    static bool HandleLookupItemSetCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in ItemSet.dbc
        for (uint32 id = 0; id < sItemSetStore.GetNumRows(); id++)
        {
            ItemSetEntry const* set = sItemSetStore.LookupEntry(id);
            if (set)
            {
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = set->Name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = set->Name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send item set in "id - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_ITEMSET_LIST_CHAT, id, id, name.c_str(), localeNames[locale]);
                    else
                        handler->PSendSysMessage(LANG_ITEMSET_LIST_CONSOLE, id, name.c_str(), localeNames[locale]);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOITEMSETFOUND);

        return true;
    }

    /**
     * @brief 处理游戏对象查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的游戏对象名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup object 命令时
     * 性能注意事项:
     * - 遍历所有游戏对象模板数据
     * - 优先搜索本地化名称,若未找到则搜索默认名称
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 游戏内显示为可点击的游戏对象链接,控制台显示纯文本
     */
    static bool HandleLookupObjectCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        GameObjectTemplateContainer const& gotc = sObjectMgr->GetGameObjectTemplates();
        for (auto const& gameObjectTemplatePair : gotc)
        {
            uint8 localeIndex = handler->GetSessionDbLocaleIndex();
            if (GameObjectLocale const* objectLocalte = sObjectMgr->GetGameObjectLocale(gameObjectTemplatePair.first))
            {
                if (objectLocalte->Name.size() > localeIndex && !objectLocalte->Name[localeIndex].empty())
                {
                    std::string const& name = objectLocalte->Name[localeIndex];
                    if (Utf8FitTo(name, wNamePart))
                    {
                        if (maxResults && count++ == maxResults)
                        {
                            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                            return true;
                        }

                        if (handler->GetSession())
                            handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CHAT, gameObjectTemplatePair.first, gameObjectTemplatePair.first, name.c_str());
                        else
                            handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CONSOLE, gameObjectTemplatePair.first, name.c_str());

                        if (!found)
                            found = true;

                        continue;
                    }
                }
            }

            std::string const& name = gameObjectTemplatePair.second.name;
            if (name.empty())
                continue;

            if (Utf8FitTo(name, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CHAT, gameObjectTemplatePair.first, gameObjectTemplatePair.first, name.c_str());
                else
                    handler->PSendSysMessage(LANG_GO_ENTRY_LIST_CONSOLE, gameObjectTemplatePair.first, name.c_str());

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOGAMEOBJECTFOUND);

        return true;
    }

    /**
     * @brief 处理任务查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的任务标题(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup quest 命令时
     * 性能注意事项:
     * - 遍历所有任务模板数据
     * - 若有选中玩家,则显示该玩家对各个任务的状态(完成、进行中、已奖励)
     * - 优先搜索本地化标题,若未找到则搜索默认标题
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [任务标题] [任务状态]"
     */
    static bool HandleLookupQuestCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayerOrSelf();

        std::string namePart = args;
        std::wstring wNamePart;

        // converting string that we try to find to lower case
        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        ObjectMgr::QuestContainer const& questTemplates = sObjectMgr->GetQuestTemplates();
        for (auto const& questTemplatePair : questTemplates)
        {
            uint8 localeIndex = handler->GetSessionDbLocaleIndex();
            if (QuestLocale const* questLocale = sObjectMgr->GetQuestLocale(questTemplatePair.first))
            {
                if (questLocale->Title.size() > localeIndex && !questLocale->Title[localeIndex].empty())
                {
                    std::string const& title = questLocale->Title[localeIndex];

                    if (Utf8FitTo(title, wNamePart))
                    {
                        if (maxResults && count++ == maxResults)
                        {
                            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                            return true;
                        }

                        char const* statusStr = "";

                        if (target)
                        {
                            switch (target->GetQuestStatus(questTemplatePair.first))
                            {
                                case QUEST_STATUS_COMPLETE:
                                    statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_COMPLETE);
                                    break;
                                case QUEST_STATUS_INCOMPLETE:
                                    statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_ACTIVE);
                                    break;
                                case QUEST_STATUS_REWARDED:
                                    statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_REWARDED);
                                    break;
                                default:
                                    break;
                            }
                        }

                        if (handler->GetSession())
                            handler->PSendSysMessage(LANG_QUEST_LIST_CHAT, questTemplatePair.first, questTemplatePair.first, questTemplatePair.second->GetQuestLevel(), title.c_str(), statusStr);
                        else
                            handler->PSendSysMessage(LANG_QUEST_LIST_CONSOLE, questTemplatePair.first, title.c_str(), statusStr);

                        if (!found)
                            found = true;

                        continue;
                    }
                }
            }

            std::string const& title = questTemplatePair.second->GetTitle();
            if (title.empty())
                continue;

            if (Utf8FitTo(title, wNamePart))
            {
                if (maxResults && count++ == maxResults)
                {
                    handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                char const* statusStr = "";

                if (target)
                {
                    switch (target->GetQuestStatus(questTemplatePair.first))
                    {
                        case QUEST_STATUS_COMPLETE:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_COMPLETE);
                            break;
                        case QUEST_STATUS_INCOMPLETE:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_ACTIVE);
                            break;
                        case QUEST_STATUS_REWARDED:
                            statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_REWARDED);
                            break;
                        default:
                            break;
                    }
                }

                if (handler->GetSession())
                    handler->PSendSysMessage(LANG_QUEST_LIST_CHAT, questTemplatePair.first, questTemplatePair.first, questTemplatePair.second->GetQuestLevel(), title.c_str(), statusStr);
                else
                    handler->PSendSysMessage(LANG_QUEST_LIST_CONSOLE, questTemplatePair.first, title.c_str(), statusStr);

                if (!found)
                    found = true;
            }
        }

        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOQUESTFOUND);

        return true;
    }

    /**
     * @brief 处理任务ID查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的任务ID
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup quest id 命令时
     * 性能注意事项:
     * - 直接通过ID查找任务模板,性能高效
     * - 若有选中玩家,则显示该玩家对此任务的状态
     * - 只返回单个任务的查找结果
     *
     * 输出格式: "id - [任务标题] [任务状态]"
     */
    static bool HandleLookupQuestIdCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 id = atoi((char*)args);

        // can be NULL at console call
        Player* target = handler->getSelectedPlayerOrSelf();

        if (Quest const* quest = sObjectMgr->GetQuestTemplate(id))
        {
            std::string title = quest->GetTitle();
            if (title.empty())
            {
                handler->SendSysMessage(LANG_COMMAND_NOQUESTFOUND);
                return true;
            }

            char const* statusStr = "";

            if (target)
            {
                switch (target->GetQuestStatus(id))
                {
                    case QUEST_STATUS_COMPLETE:
                        statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_COMPLETE);
                        break;
                    case QUEST_STATUS_INCOMPLETE:
                        statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_ACTIVE);
                        break;
                    case QUEST_STATUS_REWARDED:
                        statusStr = handler->GetTrinityString(LANG_COMMAND_QUEST_REWARDED);
                        break;
                    default:
                        break;
                }
            }

            if (handler->GetSession())
                handler->PSendSysMessage(LANG_QUEST_LIST_CHAT, id, id, quest->GetQuestLevel(), title.c_str(), statusStr);
            else
                handler->PSendSysMessage(LANG_QUEST_LIST_CONSOLE, id, title.c_str(), statusStr);
        }
        else
            handler->SendSysMessage(LANG_COMMAND_NOQUESTFOUND);

        return true;
    }

    /**
     * @brief 处理技能查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的技能名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup skill 命令时
     * 性能注意事项:
     * - 遍历所有技能数据(SkillLine.dbc)
     * - 若有选中玩家,则显示该玩家的技能等级信息
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [技能名称 语言] [已知] [当前值/最大值/永久加成/临时加成]"
     */
    static bool HandleLookupSkillCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL in console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in SkillLine.dbc
        for (uint32 id = 0; id < sSkillLineStore.GetNumRows(); id++)
        {
            SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(id);
            if (skillInfo)
            {
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = skillInfo->DisplayName[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = skillInfo->DisplayName[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    char valStr[50] = "";
                    char const* knownStr = "";
                    if (target && target->HasSkill(id))
                    {
                        knownStr = handler->GetTrinityString(LANG_KNOWN);
                        uint32 curValue = target->GetPureSkillValue(id);
                        uint32 maxValue  = target->GetPureMaxSkillValue(id);
                        uint32 permValue = target->GetSkillPermBonusValue(id);
                        uint32 tempValue = target->GetSkillTempBonusValue(id);

                        char const* valFormat = handler->GetTrinityString(LANG_SKILL_VALUES);
                        snprintf(valStr, 50, valFormat, curValue, maxValue, permValue, tempValue);
                    }

                    // send skill in "id - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_SKILL_LIST_CHAT, id, id, name.c_str(), localeNames[locale], knownStr, valStr);
                    else
                        handler->PSendSysMessage(LANG_SKILL_LIST_CONSOLE, id, name.c_str(), localeNames[locale], knownStr, valStr);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOSKILLFOUND);

        return true;
    }

    /**
     * @brief 处理法术查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的法术名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup spell 命令时
     * 性能注意事项:
     * - 遍历所有法术数据(Spell.dbc),数据量较大
     * - 若有选中玩家,则显示该玩家是否已学习该法术
     * - 检测法术类型:天赋、被动、学习类法术等
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [法术名称 等级N] [天赋] [被动] [学习] [已知] [激活]"
     */
    static bool HandleLookupSpellCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in Spell.dbc
        for (uint32 id = 0; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id);
            if (spellInfo)
            {
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = spellInfo->SpellName[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = spellInfo->SpellName[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    bool known = target && target->HasSpell(id);

                    SpellEffectInfo const& spellEffectInfo = spellInfo->GetEffect(EFFECT_0);
                    bool learn = spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL);

                    SpellInfo const* learnSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);

                    uint32 talentCost = GetTalentSpellCost(id);

                    bool talent = (talentCost > 0);
                    bool passive = spellInfo->IsPassive();
                    bool active = target && target->HasAura(id);

                    // unit32 used to prevent interpreting uint8 as char at output
                    // find rank of learned spell for learning spell, or talent rank
                    uint32 rank = talentCost ? talentCost : learn && learnSpellInfo ? learnSpellInfo->GetRank() : spellInfo->GetRank();

                    // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
                    std::ostringstream ss;
                    if (handler->GetSession())
                        ss << id << " - |cffffffff|Hspell:" << id << "|h[" << name;
                    else
                        ss << id << " - " << name;

                    // include rank in link name
                    if (rank)
                        ss << handler->GetTrinityString(LANG_SPELL_RANK) << rank;

                    if (handler->GetSession())
                        ss << ' ' << localeNames[locale] << "]|h|r";
                    else
                        ss << ' ' << localeNames[locale];

                    if (talent)
                        ss << handler->GetTrinityString(LANG_TALENT);
                    if (passive)
                        ss << handler->GetTrinityString(LANG_PASSIVE);
                    if (learn)
                        ss << handler->GetTrinityString(LANG_LEARN);
                    if (known)
                        ss << handler->GetTrinityString(LANG_KNOWN);
                    if (active)
                        ss << handler->GetTrinityString(LANG_ACTIVE);

                    handler->SendSysMessage(ss.str().c_str());

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);

        return true;
    }

    /**
     * @brief 处理法术ID查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的法术ID
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup spell id 命令时
     * 性能注意事项:
     * - 直接通过ID查找法术信息,性能高效
     * - 若有选中玩家,则显示该玩家的法术状态
     * - 只返回单个法术的查找结果
     *
     * 输出格式: "id - [法术名称 等级N] [天赋] [被动] [学习] [已知] [激活]"
     */
    static bool HandleLookupSpellIdCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL at console call
        Player* target = handler->getSelectedPlayer();

        uint32 id = atoi((char*)args);

        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(id))
        {
            uint8 locale = handler->GetSessionDbcLocale();
            std::string name = spellInfo->SpellName[locale];
            if (name.empty())
            {
                handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);
                return true;
            }

            bool known = target && target->HasSpell(id);

            SpellEffectInfo const& spellEffectInfo = spellInfo->GetEffect(EFFECT_0);
            bool learn = spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL);

            SpellInfo const* learnSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);

            uint32 talentCost = GetTalentSpellCost(id);

            bool talent = (talentCost > 0);
            bool passive = spellInfo->IsPassive();
            bool active = target && target->HasAura(id);

            // unit32 used to prevent interpreting uint8 as char at output
            // find rank of learned spell for learning spell, or talent rank
            uint32 rank = talentCost ? talentCost : learn && learnSpellInfo ? learnSpellInfo->GetRank() : spellInfo->GetRank();

            // send spell in "id - [name, rank N] [talent] [passive] [learn] [known]" format
            std::ostringstream ss;
            if (handler->GetSession())
                ss << id << " - |cffffffff|Hspell:" << id << "|h[" << name;
            else
                ss << id << " - " << name;

            // include rank in link name
            if (rank)
                ss << handler->GetTrinityString(LANG_SPELL_RANK) << rank;

            if (handler->GetSession())
                ss << ' ' << localeNames[locale] << "]|h|r";
            else
                ss << ' ' << localeNames[locale];

            if (talent)
                ss << handler->GetTrinityString(LANG_TALENT);
            if (passive)
                ss << handler->GetTrinityString(LANG_PASSIVE);
            if (learn)
                ss << handler->GetTrinityString(LANG_LEARN);
            if (known)
                ss << handler->GetTrinityString(LANG_KNOWN);
            if (active)
                ss << handler->GetTrinityString(LANG_ACTIVE);

            handler->SendSysMessage(ss.str().c_str());
        }
        else
            handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);

        return true;
    }

    /**
     * @brief 处理出租车站点查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的出租车站点名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup taxinode 命令时
     * 性能注意事项:
     * - 遍历所有出租车站点数据(TaxiNodes.dbc)
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [站点名称 语言] (Map:m X:x Y:y Z:z)"
     */
    static bool HandleLookupTaxiNodeCommand(ChatHandler* handler, const char * args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        bool found = false;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in TaxiNodes.dbc
        for (uint32 id = 0; id < sTaxiNodesStore.GetNumRows(); id++)
        {
            TaxiNodesEntry const* nodeEntry = sTaxiNodesStore.LookupEntry(id);
            if (nodeEntry)
            {
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = nodeEntry->Name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = nodeEntry->Name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && count++ == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    // send taxinode in "id - [name] (Map:m X:x Y:y Z:z)" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_TAXINODE_ENTRY_LIST_CHAT, id, id, name.c_str(), localeNames[locale],
                            nodeEntry->ContinentID, nodeEntry->Pos.X, nodeEntry->Pos.Y, nodeEntry->Pos.Z);
                    else
                        handler->PSendSysMessage(LANG_TAXINODE_ENTRY_LIST_CONSOLE, id, name.c_str(), localeNames[locale],
                            nodeEntry->ContinentID, nodeEntry->Pos.X, nodeEntry->Pos.Y, nodeEntry->Pos.Z);

                    if (!found)
                        found = true;
                }
            }
        }
        if (!found)
            handler->SendSysMessage(LANG_COMMAND_NOTAXINODEFOUND);

        return true;
    }

    /**
     * @brief 处理传送点查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的传送点名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup tele 命令时
     * 性能注意事项:
     * - 遍历所有游戏传送点数据(game_tele 表)
     * - 按名称排序输出结果
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 游戏内显示为可点击的传送点链接,控制台显示纯文本
     */
    // Find tele in game_tele order by name
    static bool HandleLookupTeleCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
        {
            handler->SendSysMessage(LANG_COMMAND_TELE_PARAMETER);
            handler->SetSentErrorMessage(true);
            return false;
        }

        char const* str = strtok((char*)args, " ");
        if (!str)
            return false;

        std::string namePart = str;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        std::ostringstream reply;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);
        bool limitReached = false;

        GameTeleContainer const & teleMap = sObjectMgr->GetGameTeleMap();
        for (GameTeleContainer::const_iterator itr = teleMap.begin(); itr != teleMap.end(); ++itr)
        {
            GameTele const* tele = &itr->second;

            if (tele->wnameLow.find(wNamePart) == std::wstring::npos)
                continue;

            if (maxResults && count++ == maxResults)
            {
                limitReached = true;
                break;
            }

            if (handler->GetSession())
                reply << "  |cffffffff|Htele:" << itr->first << "|h[" << tele->name << "]|h|r\n";
            else
                reply << "  " << itr->first << ' ' << tele->name << "\n";
        }

        if (reply.str().empty())
            handler->SendSysMessage(LANG_COMMAND_TELE_NOLOCATION);
        else
            handler->PSendSysMessage(LANG_COMMAND_TELE_LOCATION, reply.str().c_str());

        if (limitReached)
            handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);

        return true;
    }

    /**
     * @brief 处理称号查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的称号名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup title 命令时
     * 性能注意事项:
     * - 遍历所有称号数据(CharTitles.dbc)
     * - 若有选中玩家,则显示该玩家是否已获得和激活该称号
     * - 支持多语言搜索
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id (idx:idx) - [称号名称 语言] [已知] [激活]"
     * 注意: 称号名称会格式化为包含玩家名字的形式
     */
    static bool HandleLookupTitleCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        // can be NULL in console call
        Player* target = handler->getSelectedPlayer();

        // title name have single string arg for player name
        char const* targetName = target ? target->GetName().c_str() : "NAME";

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        // converting string that we try to find to lower case
        wstrToLower(wNamePart);

        uint32 counter = 0;                                     // Counter for figure out that we found smth.
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        // Search in CharTitles.dbc
        for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
        {
            CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
            if (titleInfo)
            {
                /// @todo: implement female support
                uint8 locale = handler->GetSessionDbcLocale();
                std::string name = titleInfo->Name[locale];
                if (name.empty())
                    continue;

                if (!Utf8FitTo(name, wNamePart))
                {
                    locale = 0;
                    for (; locale < TOTAL_LOCALES; ++locale)
                    {
                        if (locale == handler->GetSessionDbcLocale())
                            continue;

                        name = titleInfo->Name[locale];
                        if (name.empty())
                            continue;

                        if (Utf8FitTo(name, wNamePart))
                            break;
                    }
                }

                if (locale < TOTAL_LOCALES)
                {
                    if (maxResults && counter == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    char const* knownStr = target && target->HasTitle(titleInfo) ? handler->GetTrinityString(LANG_KNOWN) : "";

                    char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->MaskID
                        ? handler->GetTrinityString(LANG_ACTIVE)
                        : "";

                    char titleNameStr[80];
                    snprintf(titleNameStr, 80, name.c_str(), targetName);

                    // send title in "id (idx:idx) - [namedlink locale]" format
                    if (handler->GetSession())
                        handler->PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->MaskID, id, titleNameStr, localeNames[locale], knownStr, activeStr);
                    else
                        handler->PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->MaskID, titleNameStr, localeNames[locale], knownStr, activeStr);

                    ++counter;
                }
            }
        }
        if (counter == 0)  // if counter == 0 then we found nth
            handler->SendSysMessage(LANG_COMMAND_NOTITLEFOUND);

        return true;
    }

    /**
     * @brief 处理地图查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的地图名称(部分匹配)
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup map 命令时
     * 性能注意事项:
     * - 遍历所有地图数据(Map.dbc)
     * - 显示地图类型:大陆、副本、团队副本、战场、竞技场
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: "id - [地图名称] [地图类型]"
     */
    static bool HandleLookupMapCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string namePart = args;
        std::wstring wNamePart;

        if (!Utf8toWStr(namePart, wNamePart))
            return false;

        wstrToLower(wNamePart);

        uint32 counter = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);
        uint8 locale = handler->GetSession() ? handler->GetSession()->GetSessionDbcLocale() : sWorld->GetDefaultDbcLocale();

        // search in Map.dbc
        for (uint32 id = 0; id < sMapStore.GetNumRows(); id++)
        {
            if (MapEntry const* mapInfo = sMapStore.LookupEntry(id))
            {
                std::string name = mapInfo->MapName[locale];
                if (name.empty())
                    continue;

                if (Utf8FitTo(name, wNamePart) && locale < TOTAL_LOCALES)
                {
                    if (maxResults && counter == maxResults)
                    {
                        handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                        return true;
                    }

                    std::ostringstream ss;
                    ss << id << " - [" << name << ']';

                    if (mapInfo->IsContinent())
                        ss << handler->GetTrinityString(LANG_CONTINENT);

                    switch (mapInfo->InstanceType)
                    {
                        case MAP_INSTANCE:
                            ss << handler->GetTrinityString(LANG_INSTANCE);
                            break;
                        case MAP_RAID:
                            ss << handler->GetTrinityString(LANG_RAID);
                            break;
                        case MAP_BATTLEGROUND:
                            ss << handler->GetTrinityString(LANG_BATTLEGROUND);
                            break;
                        case MAP_ARENA:
                            ss << handler->GetTrinityString(LANG_ARENA);
                            break;
                    }

                    handler->SendSysMessage(ss.str().c_str());

                    ++counter;
                }
            }
        }

        if (!counter)
            handler->SendSysMessage(LANG_COMMAND_NOMAPFOUND);

        return true;
    }

    /**
     * @brief 处理地图ID查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,包含要查找的地图ID
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup map id 命令时
     * 性能注意事项:
     * - 直接通过ID查找地图信息,性能高效
     * - 只返回单个地图的查找结果
     *
     * 输出格式: "id - [地图名称] [地图类型]"
     */
    static bool HandleLookupMapIdCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 id = atoi((char*)args);

        if (MapEntry const* mapInfo = sMapStore.LookupEntry(id))
        {
            uint8 locale = handler->GetSession() ? handler->GetSession()->GetSessionDbcLocale() : sWorld->GetDefaultDbcLocale();
            std::string name = mapInfo->MapName[locale];
            if (name.empty())
            {
                handler->SendSysMessage(LANG_COMMAND_NOSPELLFOUND);
                return true;
            }

            std::ostringstream ss;
            ss << id << " - [" << name << ']';

            if (mapInfo->IsContinent())
                ss << handler->GetTrinityString(LANG_CONTINENT);

            switch (mapInfo->InstanceType)
            {
            case MAP_INSTANCE:
                ss << handler->GetTrinityString(LANG_INSTANCE);
                break;
            case MAP_RAID:
                ss << handler->GetTrinityString(LANG_RAID);
                break;
            case MAP_BATTLEGROUND:
                ss << handler->GetTrinityString(LANG_BATTLEGROUND);
                break;
            case MAP_ARENA:
                ss << handler->GetTrinityString(LANG_ARENA);
                break;
            }

            handler->SendSysMessage(ss.str().c_str());
        }
        else
            handler->SendSysMessage(LANG_COMMAND_NOMAPFOUND);

        return true;
    }

    /**
     * @brief 处理玩家IP查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,格式为 "IP [limit]",若不提供则使用选中玩家的IP
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup player ip 命令时
     * 性能注意事项:
     * - 查询登录数据库获取使用该IP的账号
     * - 再查询角色数据库获取每个账号下的角色
     * - 可选的 limit 参数限制每个账号返回的角色数量
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 显示账号信息和该账号下的所有角色(包含在线状态)
     */
    static bool HandleLookupPlayerIpCommand(ChatHandler* handler, char const* args)
    {
        std::string ip;
        int32 limit;
        char* limitStr;

        Player* target = handler->getSelectedPlayer();
        if (!*args)
        {
            // NULL only if used from console
            if (!target || target == handler->GetSession()->GetPlayer())
                return false;

            ip = target->GetSession()->GetRemoteAddress();
            limit = -1;
        }
        else
        {
            ip = strtok((char*)args, " ");
            limitStr = strtok(nullptr, " ");
            limit = limitStr ? atoi(limitStr) : -1;
        }

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_IP);
        stmt->setString(0, ip);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    /**
     * @brief 处理玩家账号查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,格式为 "account [limit]"
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup player account 命令时
     * 性能注意事项:
     * - 查询登录数据库获取账号信息
     * - 再查询角色数据库获取该账号下的角色
     * - limit 参数限制返回的角色数量
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 显示账号信息和该账号下的所有角色(包含在线状态)
     */
    static bool HandleLookupPlayerAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string account = strtok((char*)args, " ");
        char* limitStr = strtok(nullptr, " ");
        int32 limit = limitStr ? atoi(limitStr) : -1;

        if (!Utf8ToUpperOnlyLatin
            (account))
            return false;

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_NAME);
        stmt->setString(0, account);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    /**
     * @brief 处理玩家邮箱查找命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 命令参数,格式为 "email [limit]"
     * @return true 表示命令执行成功,false 表示参数错误
     *
     * 调用时机: 当 GM 使用 .lookup player email 命令时
     * 性能注意事项:
     * - 查询登录数据库获取使用该邮箱的账号
     * - 再查询角色数据库获取每个账号下的角色
     * - limit 参数限制每个账号返回的角色数量
     * - 受 CONFIG_MAX_RESULTS_LOOKUP_COMMANDS 配置限制最大结果数
     *
     * 输出格式: 显示账号信息和该账号下的所有角色(包含在线状态)
     */
    static bool HandleLookupPlayerEmailCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string email = strtok((char*)args, " ");
        char* limitStr = strtok(nullptr, " ");
        int32 limit = limitStr ? atoi(limitStr) : -1;

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_EMAIL);
        stmt->setString(0, email);
        PreparedQueryResult result = LoginDatabase.Query(stmt);

        return LookupPlayerSearchCommand(result, limit, handler);
    }

    /**
     * @brief 玩家搜索命令的通用处理函数
     * @param result 登录数据库查询结果,包含账号ID和账号名
     * @param limit 每个账号最多返回的角色数量,-1 表示无限制
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @return true 表示找到并显示了玩家,false 表示未找到任何玩家
     *
     * 调用时机: 由 HandleLookupPlayerIpCommand/HandleLookupPlayerAccountCommand/HandleLookupPlayerEmailCommand 调用
     * 性能注意事项:
     * - 对每个找到的账号执行一次角色数据库查询
     * - 嵌套循环处理可能影响性能,受最大结果数限制
     *
     * 输出格式:
     * - 先显示账号信息: "Account: accountName (accountId)"
     * - 再显示该账号下的所有角色: "  Character: name [online]"
     */
    static bool LookupPlayerSearchCommand(PreparedQueryResult result, int32 limit, ChatHandler* handler)
    {
        if (!result)
        {
            handler->PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        int32 counter = 0;
        uint32 count = 0;
        uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

        do
        {
            if (maxResults && count++ == maxResults)
            {
                handler->PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                return true;
            }

            Field* fields           = result->Fetch();
            uint32 accountId        = fields[0].GetUInt32();
            std::string accountName = fields[1].GetString();

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_GUID_NAME_BY_ACC);
            stmt->setUInt32(0, accountId);
            PreparedQueryResult result2 = CharacterDatabase.Query(stmt);

            if (result2)
            {
                handler->PSendSysMessage(LANG_LOOKUP_PLAYER_ACCOUNT, accountName.c_str(), accountId);

                do
                {
                    Field* characterFields  = result2->Fetch();
                    ObjectGuid::LowType guid = characterFields[0].GetUInt32();
                    std::string name        = characterFields[1].GetString();
                    uint8 online = characterFields[2].GetUInt8();

                    handler->PSendSysMessage(LANG_LOOKUP_PLAYER_CHARACTER, name.c_str(), guid, online ? handler->GetTrinityString(LANG_ONLINE) : "");
                    ++counter;
                }
                while (result2->NextRow() && (limit == -1 || counter < limit));
            }
        }
        while (result->NextRow());

        if (counter == 0) // empty accounts only
        {
            handler->PSendSysMessage(LANG_NO_PLAYERS_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        return true;
    }
};

/**
 * @brief 注册查找命令脚本
 *
 * 此函数由脚本系统在启动时调用,用于创建并注册 lookup_commandscript 实例。
 * 使查找命令在游戏中可用。
 */
void AddSC_lookup_commandscript()
{
    new lookup_commandscript();
}
