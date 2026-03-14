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
 * @file ChatCommand.cpp
 * @brief 聊天命令系统核心实现
 *
 * 本文件实现了聊天命令系统的核心功能，包括：
 * - 命令树的构建和管理
 * - 命令解析和执行
 * - 命令帮助系统
 * - 命令自动补全
 * - 命令使用日志记录
 *
 * 主要执行流程：
 * 1. 服务器启动时，LoadCommandMap 从脚本和数据库加载命令定义
 * 2. 用户输入命令时，TryExecuteCommand 解析并执行
 * 3. 执行成功后，LogCommandUsage 记录命令使用日志（仅 GM 命令）
 *
 * 命令查找算法：
 * - 使用分词将命令字符串拆分为多个标记
 * - 从顶层命令映射开始，逐层匹配命令节点
 * - 支持命令前缀匹配（输入部分命令名）
 * - 处理命令歧义（多个匹配）和错误（无匹配）
 */

#include "ChatCommand.h"

#include "AccountMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

/// 命令映射类型定义（不区分大小写的有序映射）
using ChatSubCommandMap = std::map<std::string_view, Trinity::Impl::ChatCommands::ChatCommandNode, StringCompareLessI_T>;

/**
 * @brief 从构建器加载命令配置
 *
 * 根据构建器的数据类型（调用器或子命令）加载相应的配置。
 *
 * @param builder 命令构建器
 */
void Trinity::Impl::ChatCommands::ChatCommandNode::LoadFromBuilder(ChatCommandBuilder const& builder)
{
    if (std::holds_alternative<ChatCommandBuilder::InvokerEntry>(builder._data))
    {
        // 处理可执行命令
        ASSERT(!_invoker, "Duplicate blank sub-command.");
        TrinityStrings help;
        std::tie(_invoker, help, _permission) = *(std::get<ChatCommandBuilder::InvokerEntry>(builder._data));
        if (help)
            _help.emplace<TrinityStrings>(help);
    }
    else
    {
        // 处理子命令容器
        LoadCommandsIntoMap(this, _subCommands, std::get<ChatCommandBuilder::SubCommandEntry>(builder._data));
    }
}

/**
 * @brief 将命令加载到映射表中
 *
 * 遍历命令构建器列表，将每个命令添加到映射表的相应位置。
 * 支持层级命令结构，通过 COMMAND_DELIMITER 分隔符解析多级命令。
 *
 * @param blank 空白命令节点指针，用于处理空名称命令
 * @param map 目标映射表引用
 * @param commands 命令构建器列表
 *
 * @note 此函数会递归创建中间命令节点以支持多级命令
 */
/*static*/ void Trinity::Impl::ChatCommands::ChatCommandNode::LoadCommandsIntoMap(ChatCommandNode* blank, ChatSubCommandMap& map, Trinity::ChatCommands::ChatCommandTable const& commands)
{
    for (ChatCommandBuilder const& builder : commands)
    {
        if (builder._name.empty())
        {
            // 空名称命令，加载到 blank 节点
            ASSERT(blank, "Empty name command at top level is not permitted.");
            blank->LoadFromBuilder(builder);
        }
        else
        {
            // 解析命令名称的各个层级（如 "account create" -> ["account", "create"]）
            std::vector<std::string_view> const tokens = Trinity::Tokenize(builder._name, COMMAND_DELIMITER, false);
            ASSERT(!tokens.empty(), "Invalid command name '" STRING_VIEW_FMT "'.", STRING_VIEW_FMT_ARG(builder._name));
            ChatSubCommandMap* subMap = &map;
            // 创建或获取中间层级节点
            for (size_t i = 0, n = (tokens.size() - 1); i < n; ++i)
                subMap = &((*subMap)[tokens[i]]._subCommands);
            // 在最终层级加载命令
            ((*subMap)[tokens.back()]).LoadFromBuilder(builder);
        }
    }
}

/// 全局顶层命令映射表，存储所有顶级命令
static ChatSubCommandMap COMMAND_MAP;

/**
 * @brief 获取顶层命令映射表
 *
 * 如果映射表为空，则先加载命令。这是懒加载模式的实现。
 *
 * @return 顶层命令映射表的常引用
 */
/*static*/ ChatSubCommandMap const& Trinity::Impl::ChatCommands::ChatCommandNode::GetTopLevelMap()
{
    if (COMMAND_MAP.empty())
        LoadCommandMap();
    return COMMAND_MAP;
}

/**
 * @brief 使命令映射表失效
 *
 * 清空全局命令映射表，用于重新加载命令时。
 */
/*static*/ void Trinity::Impl::ChatCommands::ChatCommandNode::InvalidateCommandMap() { COMMAND_MAP.clear(); }

/**
 * @brief 加载命令映射表
 *
 * 从脚本系统和数据库加载所有命令定义，构建完整的命令树。
 * 加载过程：
 * 1. 清空现有映射表
 * 2. 从脚本系统加载命令定义（C++ 代码中定义的命令）
 * 3. 从数据库加载命令帮助文本（command 表）
 * 4. 解析所有命令的完整名称
 *
 * @note 这是一个重量级操作，通常只在服务器启动时执行一次
 */
/*static*/ void Trinity::Impl::ChatCommands::ChatCommandNode::LoadCommandMap()
{
    InvalidateCommandMap();
    // 从脚本系统加载命令定义
    LoadCommandsIntoMap(nullptr, COMMAND_MAP, sScriptMgr->GetChatCommands());

    // 从数据库加载命令帮助文本
    if (PreparedQueryResult result = WorldDatabase.Query(WorldDatabase.GetPreparedStatement(WORLD_SEL_COMMANDS)))
    {
        do
        {
            Field* fields = result->Fetch();
            std::string_view const name = fields[0].GetStringView();
            std::string_view const help = fields[1].GetStringView();

            // 查找对应的命令节点
            ChatCommandNode* cmd = nullptr;
            ChatSubCommandMap* map = &COMMAND_MAP;
            for (std::string_view key : Trinity::Tokenize(name, COMMAND_DELIMITER, false))
            {
                auto it = map->find(key);
                if (it != map->end())
                {
                    cmd = &it->second;
                    map = &cmd->_subCommands;
                }
                else
                {
                    // 命令不存在，记录错误并跳过
                    TC_LOG_ERROR("sql.sql", "Table `command` contains data for non-existant command '{}'. Skipped.", name);
                    cmd = nullptr;
                    break;
                }
            }

            if (!cmd)
                continue;

            // 检查是否已有帮助文本
            if (std::holds_alternative<std::string>(cmd->_help))
                TC_LOG_ERROR("sql.sql", "Table `command` contains duplicate data for command '{}'. Skipped.", name);

            // 设置帮助文本
            if (std::holds_alternative<std::monostate>(cmd->_help))
                cmd->_help.emplace<std::string>(help);
            else
                TC_LOG_ERROR("sql.sql", "Table `command` contains legacy help text for command '{}', which uses `trinity_string`. Skipped.", name);
        } while (result->NextRow());
    }

    // 解析所有命令的完整名称
    for (auto& [name, cmd] : COMMAND_MAP)
        cmd.ResolveNames(std::string(name));
}

/**
 * @brief 解析命令名称
 *
 * 递归设置命令节点及其所有子命令的完整名称。
 * 如果命令有调用器但没有帮助文本，会记录警告。
 *
 * @param name 命令完整名称
 */
void Trinity::Impl::ChatCommands::ChatCommandNode::ResolveNames(std::string name)
{
    // 检查命令是否缺少帮助文本
    if (_invoker && std::holds_alternative<std::monostate>(_help))
        TC_LOG_WARN("sql.sql", "Table `command` is missing help text for command '{}'.", name);

    // 设置当前命令名称
    _name = name;
    // 递归处理所有子命令
    for (auto& [subToken, cmd] : _subCommands)
    {
        std::string subName(name);
        subName.push_back(COMMAND_DELIMITER);
        subName.append(subToken);
        cmd.ResolveNames(subName);
    }
}

/**
 * @brief 记录命令使用日志
 *
 * 为 GM 命令记录详细的执行日志，包括：
 * - 执行者信息（角色名、GUID、账号ID）
 * - 执行位置（坐标、地图、区域）
 * - 目标信息（如果有选中的目标）
 *
 * @param session 游戏会话
 * @param permission 命令所需权限
 * @param cmdStr 命令字符串
 *
 * @note 只记录非玩家权限的命令（GM 命令），玩家命令不记录
 */
static void LogCommandUsage(WorldSession const& session, uint32 permission, std::string_view cmdStr)
{
    // 如果是普通玩家账号，不记录日志
    if (AccountMgr::IsPlayerAccount(session.GetSecurity()))
        return;

    // 如果命令权限属于普通玩家角色，不记录日志
    if (sAccountMgr->GetRBACPermission(rbac::RBAC_ROLE_PLAYER)->GetLinkedPermissions().count(permission))
        return;

    // 获取执行者信息
    Player* player = session.GetPlayer();
    ObjectGuid targetGuid = player->GetTarget();
    uint32 areaId = player->GetAreaId();
    std::string areaName = "Unknown";
    std::string zoneName = "Unknown";

    // 获取区域名称
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId))
    {
        int locale = session.GetSessionDbcLocale();
        areaName = area->AreaName[locale];
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(area->ParentAreaID))
            zoneName = zone->AreaName[locale];
    }

    // 记录命令执行日志
    sLog->OutCommand(session.GetAccountId(), "Command: {} [Player: {} ({}) (Account: {}) X: {} Y: {} Z: {} Map: {} ({}) Area: {} ({}) Zone: {} Selected: {} ({})]",
        cmdStr, player->GetName(), player->GetGUID().ToString(),
        session.GetAccountId(), player->GetPositionX(), player->GetPositionY(),
        player->GetPositionZ(), player->GetMapId(),
        player->FindMap() ? player->FindMap()->GetMapName() : "Unknown",
        areaId, areaName, zoneName,
        (player->GetSelectedUnit()) ? player->GetSelectedUnit()->GetName() : "",
        targetGuid.ToString());
}

/**
 * @brief 发送命令帮助信息
 *
 * 显示命令的帮助文本和可用的子命令列表。
 * 帮助信息包括：
 * - 命令的帮助文本（如果有）
 * - 可见的子命令列表
 *
 * @param handler 聊天处理器
 */
void Trinity::Impl::ChatCommands::ChatCommandNode::SendCommandHelp(ChatHandler& handler) const
{
    bool const hasInvoker = IsInvokerVisible(handler);
    if (hasInvoker)
    {
        // 发送命令的帮助文本
        if (std::holds_alternative<TrinityStrings>(_help))
            handler.SendSysMessage(std::get<TrinityStrings>(_help));
        else if (std::holds_alternative<std::string>(_help))
            handler.SendSysMessage(std::get<std::string>(_help));
        else
        {
            // 没有帮助文本，显示默认消息
            handler.PSendSysMessage(LANG_CMD_HELP_GENERIC, STRING_VIEW_FMT_ARG(_name));
            handler.PSendSysMessage(LANG_CMD_NO_HELP_AVAILABLE, STRING_VIEW_FMT_ARG(_name));
        }
    }

    // 列出可见的子命令
    bool header = false;
    for (auto it = _subCommands.begin(); it != _subCommands.end(); ++it)
    {
        bool const subCommandHasSubCommand = it->second.HasVisibleSubCommands(handler);
        // 跳过不可见的子命令
        if (!subCommandHasSubCommand && !it->second.IsInvokerVisible(handler))
            continue;
        if (!header)
        {
            // 第一次显示子命令前，输出标题
            if (!hasInvoker)
                handler.PSendSysMessage(LANG_CMD_HELP_GENERIC, STRING_VIEW_FMT_ARG(_name));
            handler.SendSysMessage(LANG_SUBCMDS_LIST);
            header = true;
        }
        // 显示子命令名称，有子命令的显示省略号后缀
        handler.PSendSysMessage(subCommandHasSubCommand ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it->second._name));
    }
}

namespace Trinity::Impl::ChatCommands
{
    /**
     * @brief 过滤命令列表迭代器
     *
     * 用于遍历命令映射表中匹配指定前缀且对用户可见的命令。
     * 这是一个辅助类，用于命令查找和自动补全。
     *
     * 功能：
     * - 前缀匹配：只返回以指定 token 开头的命令
     * - 权限过滤：只返回用户有权限查看的命令
     */
    struct FilteredCommandListIterator
    {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化迭代器，定位到第一个匹配的命令。
             *
             * @param map 命令映射表
             * @param handler 聊天处理器
             * @param token 要匹配的命令前缀
             */
            FilteredCommandListIterator(ChatSubCommandMap const& map, ChatHandler const& handler, std::string_view token)
                : _handler{ handler }, _token{ token }, _it{ map.lower_bound(token) }, _end{ map.end() }
            {
                _skip();  // 跳过不匹配或不可见的命令
            }

            /// 解引用操作符
            decltype(auto) operator*() const { return _it.operator*(); }
            /// 成员访问操作符
            decltype(auto) operator->() const { return _it.operator->(); }

            /**
             * @brief 前置递增操作符
             *
             * 移动到下一个匹配的命令。
             *
             * @return 迭代器引用
             */
            FilteredCommandListIterator& operator++()
            {
                ++_it;
                _skip();  // 跳过不匹配或不可见的命令
                return *this;
            }

            /// bool 转换操作符，检查是否还有更多元素
            explicit operator bool() const { return (_it != _end); }
            /// 逻辑非操作符
            bool operator!() const { return !static_cast<bool>(*this); }

        private:
            /**
             * @brief 跳过不匹配或不可见的命令
             *
             * 内部方法，用于在迭代过程中跳过不符合条件的命令。
             * 跳过条件：
             * 1. 名称不以 token 开头
             * 2. 命令对用户不可见
             */
            void _skip()
            {
                // 如果当前命令不匹配前缀，跳到末尾
                if ((_it != _end) && !StringStartsWithI(_it->first, _token))
                    _it = _end;
                // 跳过所有不可见的命令
                while ((_it != _end) && !_it->second.IsVisible(_handler))
                {
                    ++_it;
                    if ((_it != _end) && !StringStartsWithI(_it->first, _token))
                        _it = _end;
                }
            }

            ChatHandler const& _handler;  ///< 聊天处理器引用
            std::string_view const _token;  ///< 搜索前缀
            ChatSubCommandMap::const_iterator _it, _end;  ///< 当前迭代器和结束迭代器

    };
}

/**
 * @brief 尝试执行命令
 *
 * 解析命令字符串，查找匹配的命令节点并执行。
 * 这是命令系统的核心执行入口。
 *
 * 执行流程：
 * 1. 去除命令字符串首尾的分隔符
 * 2. 逐层解析命令标记
 * 3. 处理命令歧义（多个匹配）和错误（无匹配）
 * 4. 找到命令后调用其处理函数
 * 5. 记录命令使用日志（仅 GM 命令）
 *
 * @param handler 聊天处理器
 * @param cmdStr 命令字符串（不包含命令前缀符号）
 * @return bool 是否找到并处理了命令
 */
/*static*/ bool Trinity::Impl::ChatCommands::ChatCommandNode::TryExecuteCommand(ChatHandler& handler, std::string_view cmdStr)
{
    ChatCommandNode const* cmd = nullptr;
    ChatSubCommandMap const* map = &GetTopLevelMap();

    // 去除首尾的分隔符
    while (!cmdStr.empty() && (cmdStr.front() == COMMAND_DELIMITER))
        cmdStr.remove_prefix(1);
    while (!cmdStr.empty() && (cmdStr.back() == COMMAND_DELIMITER))
        cmdStr.remove_suffix(1);
    std::string_view oldTail = cmdStr;

    // 逐层解析命令标记
    while (!oldTail.empty())
    {
        /* oldTail = token DELIMITER newTail */
        auto [token, newTail] = tokenize(oldTail);
        ASSERT(!token.empty());

        // 查找匹配的命令
        FilteredCommandListIterator it1(*map, handler, token);
        if (!it1)
            break; /* no matching subcommands found */

        if (!StringEqualI(it1->first, token))
        {
            /* ok, so it1 points at a partially matching subcommand - let's see if there are others */
            /* it1 指向部分匹配的子命令 - 检查是否有其他匹配 */
            auto it2 = it1;
            ++it2;

            if (it2)
            {
                /* there are multiple matching subcommands - print possibilities and return */
                /* 有多个匹配的子命令 - 显示可能的命令列表并返回 */
                if (cmd)
                    handler.PSendSysMessage(LANG_SUBCMD_AMBIGUOUS, STRING_VIEW_FMT_ARG(cmd->_name), COMMAND_DELIMITER, STRING_VIEW_FMT_ARG(token));
                else
                    handler.PSendSysMessage(LANG_CMD_AMBIGUOUS, STRING_VIEW_FMT_ARG(token));

                handler.PSendSysMessage(it1->second.HasVisibleSubCommands(handler) ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it1->first));
                do
                {
                    handler.PSendSysMessage(it2->second.HasVisibleSubCommands(handler) ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it2->first));
                } while (++it2);

                return true;
            }
        }

        /* now we matched exactly one subcommand, and it1 points to it; go down the rabbit hole */
        /* 现在匹配到唯一子命令，继续深入解析 */
        cmd = &it1->second;
        map = &cmd->_subCommands;

        oldTail = newTail;
    }

    if (cmd)
    {
        /* if we matched a command at some point, invoke it */
        /* 如果找到了命令，尝试调用它 */
        handler.SetSentErrorMessage(false);
        if (cmd->IsInvokerVisible(handler) && cmd->_invoker(&handler, oldTail))
        {
            /* invocation succeeded, log this */
            /* 调用成功，记录日志 */
            if (!handler.IsConsole())
                LogCommandUsage(*handler.GetSession(), cmd->_permission.RequiredPermission, cmdStr);
        }
        else if (!handler.HasSentErrorMessage())
        {
            /* invocation failed, we should show usage */
            /* 调用失败，显示帮助信息 */
            cmd->SendCommandHelp(handler);
            handler.SetSentErrorMessage(true);
        }
        return true;
    }

    return false;
}

/**
 * @brief 发送指定命令的帮助信息
 *
 * 根据命令字符串查找命令节点并显示其帮助信息。
 * 与 TryExecuteCommand 类似，但只显示帮助而不执行命令。
 *
 * @param handler 聊天处理器
 * @param cmdStr 命令字符串
 */
/*static*/ void Trinity::Impl::ChatCommands::ChatCommandNode::SendCommandHelpFor(ChatHandler& handler, std::string_view cmdStr)
{
    ChatCommandNode const* cmd = nullptr;
    ChatSubCommandMap const* map = &GetTopLevelMap();

    // 解析命令标记
    for (std::string_view token : Trinity::Tokenize(cmdStr, COMMAND_DELIMITER, false))
    {
        FilteredCommandListIterator it1(*map, handler, token);
        if (!it1)
        {
            /* no matching subcommands found */
            /* 没有找到匹配的子命令 */
            if (cmd)
            {
                cmd->SendCommandHelp(handler);
                handler.PSendSysMessage(LANG_SUBCMD_INVALID, STRING_VIEW_FMT_ARG(cmd->_name), COMMAND_DELIMITER, STRING_VIEW_FMT_ARG(token));
            }
            else
                handler.PSendSysMessage(LANG_CMD_INVALID, STRING_VIEW_FMT_ARG(token));
            return;
        }

        if (!StringEqualI(it1->first, token))
        {
            /* ok, so it1 points at a partially matching subcommand - let's see if there are others */
            /* it1 指向部分匹配的子命令 - 检查是否有其他匹配 */
            auto it2 = it1;
            ++it2;

            if (it2)
            {
                /* there are multiple matching subcommands - print possibilities and return */
                /* 有多个匹配的子命令 - 显示可能的命令列表并返回 */
                if (cmd)
                    handler.PSendSysMessage(LANG_SUBCMD_AMBIGUOUS, STRING_VIEW_FMT_ARG(cmd->_name), COMMAND_DELIMITER, STRING_VIEW_FMT_ARG(token));
                else
                    handler.PSendSysMessage(LANG_CMD_AMBIGUOUS, STRING_VIEW_FMT_ARG(token));

                handler.PSendSysMessage(it1->second.HasVisibleSubCommands(handler) ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it1->first));
                do
                {
                    handler.PSendSysMessage(it2->second.HasVisibleSubCommands(handler) ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it2->first));
                } while (++it2);

                return;
            }
        }

        cmd = &it1->second;
        map = &cmd->_subCommands;
    }

    if (cmd)
        cmd->SendCommandHelp(handler);
    else if (cmdStr.empty())
    {
        // 没有指定命令，列出所有可用的顶层命令
        FilteredCommandListIterator it(*map, handler, "");
        if (!it)
            return;
        handler.SendSysMessage(LANG_AVAILABLE_CMDS);
        do
        {
            handler.PSendSysMessage(it->second.HasVisibleSubCommands(handler) ? LANG_SUBCMDS_LIST_ENTRY_ELLIPSIS : LANG_SUBCMDS_LIST_ENTRY, STRING_VIEW_FMT_ARG(it->second._name));
        } while (++it);
    }
    else
        handler.PSendSysMessage(LANG_CMD_INVALID, STRING_VIEW_FMT_ARG(cmdStr));
}

/**
 * @brief 获取命令自动补全建议
 *
 * 根据已输入的命令字符串，返回可能的补全建议列表。
 * 用于客户端的 Tab 补全功能。
 *
 * 补全逻辑：
 * 1. 解析已输入的命令标记
 * 2. 查找匹配的命令节点
 * 3. 如果有多个匹配，返回所有可能的补全
 * 4. 如果有剩余文本，保持其不变
 * 5. 如果已到达命令末尾，返回所有可见的子命令
 *
 * @param handler 聊天处理器
 * @param cmdStr 部分命令字符串
 * @return std::vector<std::string> 可能的补全建议列表
 */
/*static*/ std::vector<std::string> Trinity::Impl::ChatCommands::ChatCommandNode::GetAutoCompletionsFor(ChatHandler const& handler, std::string_view cmdStr)
{
    std::string path;
    ChatCommandNode const* cmd = nullptr;
    ChatSubCommandMap const* map = &GetTopLevelMap();

    // 去除首尾的分隔符
    while (!cmdStr.empty() && (cmdStr.front() == COMMAND_DELIMITER))
        cmdStr.remove_prefix(1);
    while (!cmdStr.empty() && (cmdStr.back() == COMMAND_DELIMITER))
        cmdStr.remove_suffix(1);
    std::string_view oldTail = cmdStr;

    // 逐层解析命令标记
    while (!oldTail.empty())
    {
        /* oldTail = token DELIMITER newTail */
        auto [token, newTail] = tokenize(oldTail);
        ASSERT(!token.empty());
        FilteredCommandListIterator it1(*map, handler, token);
        if (!it1)
            break; /* no matching subcommands found */

        if (!StringEqualI(it1->first, token))
        {
            /* ok, so it1 points at a partially matching subcommand - let's see if there are others */
            /* it1 指向部分匹配的子命令 - 检查是否有其他匹配 */
            auto it2 = it1;
            ++it2;

            if (it2)
            {
                /* there are multiple matching subcommands - terminate here and show possibilities */
                /* 有多个匹配的子命令 - 返回所有可能的补全 */
                std::vector<std::string> vec;
                // 生成补全字符串的 lambda
                auto possibility = ([prefix = std::string_view(path), suffix = std::string_view(newTail)](std::string_view match)
                {
                    if (prefix.empty())
                    {
                        return Trinity::StringFormat("{}{}{}", match, COMMAND_DELIMITER, suffix);
                    }
                    else
                    {
                        return Trinity::StringFormat("{}{}{}{}{}", prefix, COMMAND_DELIMITER, match, COMMAND_DELIMITER, suffix);
                    }
                });

                vec.emplace_back(possibility(it1->first));

                do vec.emplace_back(possibility(it2->first));
                while (++it2);

                return vec;
            }
        }

        /* now we matched exactly one subcommand, and it1 points to it; go down the rabbit hole */
        /* 匹配到唯一子命令，继续深入解析 */
        if (path.empty())
            path.assign(it1->first);
        else
            path = Trinity::StringFormat("{}{}{}", path, COMMAND_DELIMITER, it1->first);

        cmd = &it1->second;
        map = &cmd->_subCommands;

        oldTail = newTail;
    }

    if (!oldTail.empty())
    {
        /* there is some trailing text, leave it as is */
        /* 有剩余文本，保持不变 */
        if (cmd)
        {
            /* if we matched a command at some point, auto-complete it */
            /* 如果找到了命令，返回带剩余文本的补全 */
            return {
                Trinity::StringFormat("{}{}{}", path, COMMAND_DELIMITER, oldTail)
            };
        }
        else
            return {};
    }
    else
    {
        /* offer all subcommands */
        /* 提供所有可见的子命令作为补全建议 */
        auto possibility = ([prefix = std::string_view(path)](std::string_view match)
        {
            if (prefix.empty())
                return std::string(match);
            else
            {
                return Trinity::StringFormat("{}{}{}", prefix, COMMAND_DELIMITER, match);
            }
        });

        std::vector<std::string> vec;
        for (FilteredCommandListIterator it(*map, handler, ""); it; ++it)
            vec.emplace_back(possibility(it->first));
        return vec;
    }
}

/**
 * @brief 检查调用器是否对用户可见
 *
 * 检查条件：
 * 1. 有调用器（命令可执行）
 * 2. 对于控制台用户，命令必须允许控制台执行
 * 3. 用户必须有所需的权限
 *
 * @param who 聊天处理器
 * @return bool 调用器是否可见
 */
bool Trinity::Impl::ChatCommands::ChatCommandNode::IsInvokerVisible(ChatHandler const& who) const
{
    // 没有调用器，不可见
    if (!_invoker)
        return false;
    // 控制台用户检查是否允许控制台执行
    if (who.IsConsole() && (_permission.AllowConsole == Trinity::ChatCommands::Console::No))
        return false;
    // 检查权限
    return who.HasPermission(_permission.RequiredPermission);
}

/**
 * @brief 检查是否有可见的子命令
 *
 * 遍历所有子命令，检查是否有对用户可见的。
 *
 * @param who 聊天处理器
 * @return bool 是否有可见子命令
 */
bool Trinity::Impl::ChatCommands::ChatCommandNode::HasVisibleSubCommands(ChatHandler const& who) const
{
    for (auto it = _subCommands.begin(); it != _subCommands.end(); ++it)
        if (it->second.IsVisible(who))
            return true;
    return false;
}

/// 公共接口实现，委托给 ChatCommandNode
void Trinity::ChatCommands::LoadCommandMap() { Trinity::Impl::ChatCommands::ChatCommandNode::LoadCommandMap(); }
void Trinity::ChatCommands::InvalidateCommandMap() { Trinity::Impl::ChatCommands::ChatCommandNode::InvalidateCommandMap(); }
bool Trinity::ChatCommands::TryExecuteCommand(ChatHandler& handler, std::string_view cmd) { return Trinity::Impl::ChatCommands::ChatCommandNode::TryExecuteCommand(handler, cmd); }
void Trinity::ChatCommands::SendCommandHelpFor(ChatHandler& handler, std::string_view cmd) { Trinity::Impl::ChatCommands::ChatCommandNode::SendCommandHelpFor(handler, cmd); }
std::vector<std::string> Trinity::ChatCommands::GetAutoCompletionsFor(ChatHandler const& handler, std::string_view cmd) { return Trinity::Impl::ChatCommands::ChatCommandNode::GetAutoCompletionsFor(handler, cmd); }
