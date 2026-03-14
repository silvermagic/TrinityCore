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
 * @file ChatCommand.h
 * @brief 聊天命令系统核心模块
 *
 * 本模块是 TrinityCore 聊天命令系统的核心，提供了完整的命令处理框架，包括：
 * - 命令树的构建和管理
 * - 命令的解析和执行
 * - 命令权限控制
 * - 命令帮助系统
 * - 命令自动补全
 *
 * 系统架构说明：
 * - ChatCommandNode：命令节点，表示命令树中的一个节点
 * - ChatCommandBuilder：命令构建器，用于定义命令的结构
 * - CommandInvoker：命令调用器，封装命令处理函数
 * - CommandPermissions：命令权限，控制命令的访问权限
 *
 * 命令注册流程：
 * 1. 使用 ChatCommandBuilder 定义命令结构
 * 2. 通过脚本系统注册命令
 * 3. LoadCommandMap 构建命令树
 * 4. TryExecuteCommand 执行用户输入的命令
 */

#ifndef TRINITY_CHATCOMMAND_H
#define TRINITY_CHATCOMMAND_H

#include "ChatCommandArgs.h"
#include "ChatCommandTags.h"
#include "Define.h"
#include "Errors.h"
#include "Language.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "RBAC.h"
#include "StringFormat.h"
#include "Util.h"
#include <cstddef>
#include <map>
#include <utility>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

class ChatHandler;

namespace Trinity::ChatCommands
{
    /**
     * @brief 控制台访问权限枚举
     *
     * 指定命令是否可以在服务器控制台中执行。
     */
    enum class Console : bool
    {
        No = false,   ///< 不允许控制台执行
        Yes = true    ///< 允许控制台执行
    };

    struct ChatCommandBuilder;
    /// 命令表类型，用于存储命令构建器列表
    using ChatCommandTable = std::vector<ChatCommandBuilder>;
}

namespace Trinity::Impl::ChatCommands
{
    // forward declaration
    // ConsumeFromOffset contains the bounds check for offset, then hands off to MultiConsumer
    // the call stack is MultiConsumer -> ConsumeFromOffset -> MultiConsumer -> ConsumeFromOffset etc
    // MultiConsumer goes into ArgInfo for parsing on each iteration
    //
    // 前向声明
    // ConsumeFromOffset 包含 offset 的边界检查，然后交给 MultiConsumer
    // 调用栈为 MultiConsumer -> ConsumeFromOffset -> MultiConsumer -> ConsumeFromOffset 等
    // MultiConsumer 在每次迭代时进入 ArgInfo 进行解析

    /**
     * @brief 从指定偏移位置开始消费参数元组
     *
     * 递归处理参数元组中的各个元素，从指定偏移开始逐个解析参数。
     *
     * @tparam Tuple 参数元组类型
     * @tparam offset 起始偏移位置
     * @param tuple 参数元组引用
     * @param handler 聊天处理器
     * @param args 输入参数字符串
     * @return ChatCommandResult 解析结果
     */
    template <typename Tuple, size_t offset>
    ChatCommandResult ConsumeFromOffset(Tuple&, ChatHandler const* handler, std::string_view args);

    /**
     * @brief 多参数消费者模板
     *
     * 负责从参数字符串中依次提取多个参数，存入元组的对应位置。
     * 对于普通类型参数，尝试解析后继续下一个参数。
     *
     * @tparam Tuple 参数元组类型
     * @tparam NextType 当前要解析的参数类型
     * @tparam offset 当前参数在元组中的位置
     */
    template <typename Tuple, typename NextType, size_t offset>
    struct MultiConsumer
    {
        /**
         * @brief 尝试消费参数到元组指定位置
         *
         * @param tuple 参数元组引用
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsumeTo(Tuple& tuple, ChatHandler const* handler, std::string_view args)
        {
            // 尝试解析当前参数
            ChatCommandResult next = ArgInfo<NextType>::TryConsume(std::get<offset>(tuple), handler, args);
            if (next)
                // 成功则继续解析下一个参数
                return ConsumeFromOffset<Tuple, offset + 1>(tuple, handler, *next);
            else
                return next;
        }
    };

    /**
     * @brief 可选参数消费者特化版本
     *
     * 对于 Optional<T> 类型的参数，会尝试两种解析方式：
     * 1. 尝试解析参数 T
     * 2. 如果失败，跳过此参数继续解析后续参数
     *
     * 这样可以支持可选参数的命令。
     *
     * @tparam Tuple 参数元组类型
     * @tparam NestedNextType 可选参数的底层类型
     * @tparam offset 当前参数在元组中的位置
     */
    template <typename Tuple, typename NestedNextType, size_t offset>
    struct MultiConsumer<Tuple, Optional<NestedNextType>, offset>
    {
        /**
         * @brief 尝试消费可选参数
         *
         * 首先尝试解析参数，如果失败则跳过该参数继续解析。
         *
         * @param tuple 参数元组引用
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsumeTo(Tuple& tuple, ChatHandler const* handler, std::string_view args)
        {
            // try with the argument
            // 尝试解析参数
            auto& myArg = std::get<offset>(tuple);
            myArg.emplace();

            ChatCommandResult result1 = ArgInfo<NestedNextType>::TryConsume(myArg.value(), handler, args);
            if (result1)
                if ((result1 = ConsumeFromOffset<Tuple, offset + 1>(tuple, handler, *result1)))
                    return result1;
            // try again omitting the argument
            // 尝试跳过该参数继续解析
            myArg = std::nullopt;
            ChatCommandResult result2 = ConsumeFromOffset<Tuple, offset + 1>(tuple, handler, args);
            if (result2)
                return result2;
            // 如果两种方式都有错误消息，合并显示
            if (result1.HasErrorMessage() && result2.HasErrorMessage())
            {
                return Trinity::StringFormat("{} \"{}\"\n{} \"{}\"",
                    GetTrinityString(handler, LANG_CMDPARSER_EITHER), result2.GetErrorMessage(),
                    GetTrinityString(handler, LANG_CMDPARSER_OR), result1.GetErrorMessage());
            }
            else if (result1.HasErrorMessage())
                return result1;
            else
                return result2;
        }
    };

    /**
     * @brief 从指定偏移位置开始消费参数元组
     *
     * 递归处理参数元组中的各个元素。如果已经处理完所有参数，
     * 检查是否还有剩余字符串，如果有则返回失败。
     *
     * @tparam Tuple 参数元组类型
     * @tparam offset 当前偏移位置
     * @param tuple 参数元组引用
     * @param handler 聊天处理器
     * @param args 输入参数字符串
     * @return ChatCommandResult 解析结果
     */
    template <typename Tuple, size_t offset>
    ChatCommandResult ConsumeFromOffset([[maybe_unused]] Tuple& tuple, [[maybe_unused]] ChatHandler const* handler, std::string_view args)
    {
        if constexpr (offset < std::tuple_size_v<Tuple>)
            // 还有参数要处理，调用 MultiConsumer
            return MultiConsumer<Tuple, std::tuple_element_t<offset, Tuple>, offset>::TryConsumeTo(tuple, handler, args);
        else if (!args.empty()) /* the entire string must be consumed */
            // 所有参数已处理完但还有剩余字符串，失败
            return std::nullopt;
        else
            // 成功处理完所有参数
            return args;
    }

    /**
     * @brief 命令处理函数签名到元组类型的转换器
     *
     * 将命令处理函数的签名转换为参数元组类型。
     * 例如：bool(ChatHandler*, int, std::string) -> std::tuple<ChatHandler*, int, std::string>
     *
     * @tparam T 命令处理函数类型
     */
    template <typename T> struct HandlerToTuple { static_assert(Trinity::dependant_false_v<T>, "Invalid command handler signature"); };

    /// HandlerToTuple 的特化版本，提取函数参数为元组
    template <typename... Ts> struct HandlerToTuple<bool(ChatHandler*, Ts...)> { using type = std::tuple<ChatHandler*, std::remove_cvref_t<Ts>...>; };

    /// 便捷别名，用于获取处理函数的参数元组类型
    template <typename T> using TupleType = typename HandlerToTuple<T>::type;

    /**
     * @brief 命令调用器结构体
     *
     * 封装命令处理函数，提供统一的调用接口。
     * 支持两种类型的处理函数：
     * 1. 类型安全的现代处理函数（使用强类型参数）
     * 2. 传统的 C 风格处理函数（接收 char const* 参数）
     *
     * 内部使用类型擦除技术，通过函数指针存储调用逻辑。
     */
    struct CommandInvoker
    {
        /**
         * @brief 默认构造函数，创建空调用器
         */
        CommandInvoker() : _wrapper(nullptr), _handler(nullptr) {}

        /**
         * @brief 从现代类型安全处理函数构造调用器
         *
         * 创建一个包装器，将参数字符串解析为强类型参数后调用处理函数。
         *
         * @tparam TypedHandler 处理函数类型，签名为 bool(ChatHandler*, Args...)
         * @param handler 处理函数引用
         */
        template <typename TypedHandler>
        CommandInvoker(TypedHandler& handler)
        {
            _wrapper = [](void* handler, ChatHandler* chatHandler, std::string_view argsStr)
            {
                using Tuple = TupleType<TypedHandler>;

                Tuple arguments;
                std::get<0>(arguments) = chatHandler;
                // 从偏移 1 开始解析参数（偏移 0 是 ChatHandler*）
                ChatCommandResult result = ConsumeFromOffset<Tuple, 1>(arguments, chatHandler, argsStr);
                if (result)
                    // 解析成功，调用处理函数
                    return std::apply(reinterpret_cast<TypedHandler*>(handler), std::move(arguments));
                else
                {
                    // 解析失败，发送错误消息
                    if (result.HasErrorMessage())
                        SendErrorMessageToHandler(chatHandler, result.GetErrorMessage());
                    return false;
                }
            };
            _handler = reinterpret_cast<void*>(handler);
        }

        /**
         * @brief 从传统 C 风格处理函数构造调用器
         *
         * 为遗留代码提供兼容性支持。
         * 注意：传统处理函数可能使用 strtok 破坏输入字符串，因此需要拷贝。
         *
         * @param handler 传统处理函数指针，签名为 bool(ChatHandler*, char const*)
         */
        CommandInvoker(bool(&handler)(ChatHandler*, char const*))
        {
            _wrapper = [](void* handler, ChatHandler* chatHandler, std::string_view argsStr)
            {
                // make a copy of the argument string
                // legacy handlers can destroy input strings with strtok
                // 拷贝参数字符串，因为传统处理函数可能使用 strtok 破坏输入
                std::string argsStrCopy(argsStr);
                return reinterpret_cast<bool(*)(ChatHandler*, char const*)>(handler)(chatHandler, argsStrCopy.c_str());
            };
            _handler = reinterpret_cast<void*>(handler);
        }

        /**
         * @brief 检查调用器是否有效
         * @return 如果包装器非空则返回 true
         */
        explicit operator bool() const { return (_wrapper != nullptr); }

        /**
         * @brief 调用命令处理函数
         *
         * @param chatHandler 聊天处理器
         * @param args 参数字符串
         * @return bool 命令执行是否成功
         */
        bool operator()(ChatHandler* chatHandler, std::string_view args) const
        {
            ASSERT(_wrapper && _handler);
            return _wrapper(_handler, chatHandler, args);
        }

    private:
        /// 包装函数类型签名
        using wrapper_func = bool(void*, ChatHandler*, std::string_view);
        wrapper_func* _wrapper;  ///< 包装函数指针
        void* _handler;          ///< 处理函数指针（类型擦除后）
    };

    /**
     * @brief 命令权限结构体
     *
     * 定义命令的执行权限要求，包括：
     * - RequiredPermission: 执行此命令所需的 RBAC 权限
     * - AllowConsole: 是否允许从服务器控制台执行
     */
    struct CommandPermissions
    {
        /**
         * @brief 默认构造函数，创建无权限要求的权限配置
         */
        CommandPermissions() : RequiredPermission{}, AllowConsole{} {}

        /**
         * @brief 构造函数
         * @param perm 所需的 RBAC 权限
         * @param console 是否允许控制台执行
         */
        CommandPermissions(rbac::RBACPermissions perm, Trinity::ChatCommands::Console console) : RequiredPermission{ perm }, AllowConsole{ console } {}

        rbac::RBACPermissions RequiredPermission;  ///< 所需的 RBAC 权限 ID
        Trinity::ChatCommands::Console AllowConsole;  ///< 是否允许控制台执行
    };

    /**
     * @brief 聊天命令节点类
     *
     * 表示命令树中的一个节点。每个节点可以：
     * - 有一个可执行的命令处理器（_invoker）
     * - 有若干子命令（_subCommands）
     * - 或者两者都有（既是命令又是子命令的容器）
     *
     * 命令树结构示例：
     * .account (节点，有子命令)
     *   .create (子节点，有处理器)
     *   .delete (子节点，有处理器)
     *   .onlinelist (子节点，有处理器)
     * .lookup (节点，有处理器和子命令)
     *   .item (子节点，有处理器)
     *   .spell (子节点，有处理器)
     *
     * 命令节点的生命周期：
     * 1. 通过 ChatCommandBuilder 构建
     * 2. LoadCommandMap 时构建命令树
     * 3. TryExecuteCommand 时被查找和执行
     */
    class ChatCommandNode
    {
        friend struct FilteredCommandListIterator;
        using ChatCommandBuilder = Trinity::ChatCommands::ChatCommandBuilder;

        public:
            /**
             * @brief 加载命令映射表
             *
             * 从脚本系统和数据库加载所有命令，构建命令树。
             * 在服务器启动时调用一次。
             *
             * @note 这是一个重量级操作，会清空并重建整个命令树
             */
            static void LoadCommandMap();

            /**
             * @brief 使命令映射表失效
             *
             * 清空命令映射表，用于重新加载命令时。
             */
            static void InvalidateCommandMap();

            /**
             * @brief 尝试执行命令
             *
             * 解析命令字符串，查找匹配的命令节点并执行。
             * 支持命令前缀匹配和子命令递归查找。
             *
             * @param handler 聊天处理器
             * @param cmd 命令字符串（不包含命令前缀符号）
             * @return bool 是否找到并处理了命令
             *
             * @note 执行失败时会自动发送错误消息或帮助信息
             */
            static bool TryExecuteCommand(ChatHandler& handler, std::string_view cmd);

            /**
             * @brief 发送命令帮助信息
             *
             * 显示指定命令的帮助信息，包括命令用法和可用的子命令列表。
             *
             * @param handler 聊天处理器
             * @param cmd 命令字符串
             */
            static void SendCommandHelpFor(ChatHandler& handler, std::string_view cmd);

            /**
             * @brief 获取命令自动补全建议
             *
             * 根据已输入的命令字符串，返回可能的补全建议列表。
             * 用于客户端的 Tab 补全功能。
             *
             * @param handler 聊天处理器
             * @param cmd 部分命令字符串
             * @return std::vector<std::string> 可能的补全建议列表
             */
            static std::vector<std::string> GetAutoCompletionsFor(ChatHandler const& handler, std::string_view cmd);

            /**
             * @brief 默认构造函数
             */
            ChatCommandNode() : _name{}, _invoker {}, _permission{}, _help{}, _subCommands{} {}

        private:
            /**
             * @brief 获取顶层命令映射表
             * @return 顶层命令映射表的常引用
             */
            static std::map<std::string_view, ChatCommandNode, StringCompareLessI_T> const& GetTopLevelMap();

            /**
             * @brief 将命令加载到映射表中
             *
             * @param blank 空白命令节点指针（用于处理空名称命令）
             * @param map 目标映射表
             * @param commands 命令构建器列表
             */
            static void LoadCommandsIntoMap(ChatCommandNode* blank, std::map<std::string_view, ChatCommandNode, StringCompareLessI_T>& map, Trinity::ChatCommands::ChatCommandTable const& commands);

            /**
             * @brief 从构建器加载命令配置
             * @param builder 命令构建器
             */
            void LoadFromBuilder(ChatCommandBuilder const& builder);

            /// 移动构造函数
            ChatCommandNode(ChatCommandNode&& other) = default;

            /**
             * @brief 解析命令名称
             *
             * 递归设置命令节点及其所有子命令的完整名称。
             *
             * @param name 命令完整名称
             */
            void ResolveNames(std::string name);

            /**
             * @brief 发送命令帮助信息
             * @param handler 聊天处理器
             */
            void SendCommandHelp(ChatHandler& handler) const;

            /**
             * @brief 检查命令是否对用户可见
             *
             * 命令可见的条件：有可见的调用器或可见的子命令。
             *
             * @param who 聊天处理器
             * @return bool 是否可见
             */
            bool IsVisible(ChatHandler const& who) const { return (IsInvokerVisible(who) || HasVisibleSubCommands(who)); }

            /**
             * @brief 检查调用器是否对用户可见
             *
             * 检查条件：
             * 1. 有调用器
             * 2. 控制台/游戏内权限正确
             * 3. 用户有所需权限
             *
             * @param who 聊天处理器
             * @return bool 调用器是否可见
             */
            bool IsInvokerVisible(ChatHandler const& who) const;

            /**
             * @brief 检查是否有可见的子命令
             * @param who 聊天处理器
             * @return bool 是否有可见子命令
             */
            bool HasVisibleSubCommands(ChatHandler const& who) const;

            std::string _name;  ///< 命令完整名称（包括父命令前缀）
            CommandInvoker _invoker;  ///< 命令调用器
            CommandPermissions _permission;  ///< 命令权限
            /// 帮助文本，可以是：空（monostate）、本地化字符串ID（TrinityStrings）、直接字符串（string）
            std::variant<std::monostate, TrinityStrings, std::string> _help;
            std::map<std::string_view, ChatCommandNode, StringCompareLessI_T> _subCommands;  ///< 子命令映射表
    };
}

namespace Trinity::ChatCommands
{
    /**
     * @brief 聊天命令构建器结构体
     *
     * 用于定义和构建聊天命令的配置信息。
     * 支持两种类型的命令定义：
     * 1. 可执行命令（带有处理函数）
     * 2. 子命令容器（仅包含子命令）
     *
     * 使用示例：
     * @code
     * // 定义可执行命令
     * ChatCommandBuilder("account", &HandleAccountCommand, LANG_ACCOUNT_HELP, RBAC_PERM_ACCOUNT, Console::Yes);
     *
     * // 定义子命令容器
     * ChatCommandBuilder("account", {
     *     ChatCommandBuilder("create", &HandleAccountCreateCommand, RBAC_PERM_ACCOUNT_CREATE, Console::Yes),
     *     ChatCommandBuilder("delete", &HandleAccountDeleteCommand, RBAC_PERM_ACCOUNT_DELETE, Console::Yes)
     * });
     * @endcode
     */
    struct ChatCommandBuilder
    {
        friend class Trinity::Impl::ChatCommands::ChatCommandNode;

        /**
         * @brief 调用器条目结构体
         *
         * 存储可执行命令的相关信息，包括调用器、帮助文本和权限。
         */
        struct InvokerEntry
        {
            /**
             * @brief 构造函数
             * @tparam T 处理函数类型
             * @param handler 处理函数
             * @param help 帮助文本 ID
             * @param permission 所需权限
             * @param allowConsole 是否允许控制台执行
             */
            template <typename T>
            InvokerEntry(T& handler, TrinityStrings help, rbac::RBACPermissions permission, Trinity::ChatCommands::Console allowConsole)
                : _invoker{ handler }, _help{ help }, _permissions{ permission, allowConsole }
            {}
            InvokerEntry(InvokerEntry const&) = default;
            InvokerEntry(InvokerEntry&&) = default;

            Trinity::Impl::ChatCommands::CommandInvoker _invoker;  ///< 命令调用器
            TrinityStrings _help;  ///< 帮助文本 ID
            Trinity::Impl::ChatCommands::CommandPermissions _permissions;  ///< 权限配置

            /**
             * @brief 解包操作符，返回元组形式的数据
             * @return 包含调用器、帮助文本和权限的元组
             */
            auto operator*() const { return std::tie(_invoker, _help, _permissions); }
        };

        /// 子命令条目类型，是对命令构建器向量的引用包装
        using SubCommandEntry = std::reference_wrapper<std::vector<ChatCommandBuilder> const>;

        /// 移动构造函数
        ChatCommandBuilder(ChatCommandBuilder&&) = default;
        /// 拷贝构造函数
        ChatCommandBuilder(ChatCommandBuilder const&) = default;

        /**
         * @brief 构造可执行命令构建器（带帮助文本）
         *
         * @tparam TypedHandler 处理函数类型
         * @param name 命令名称
         * @param handler 处理函数
         * @param help 帮助文本 ID
         * @param permission 所需权限
         * @param allowConsole 是否允许控制台执行
         */
        template <typename TypedHandler>
        ChatCommandBuilder(char const* name, TypedHandler& handler, TrinityStrings help, rbac::RBACPermissions permission, Trinity::ChatCommands::Console allowConsole)
            : _name{ ASSERT_NOTNULL(name) }, _data{ std::in_place_type<InvokerEntry>, handler, help, permission, allowConsole }
        {}

        /**
         * @brief 构造可执行命令构建器（无帮助文本）
         *
         * @tparam TypedHandler 处理函数类型
         * @param name 命令名称
         * @param handler 处理函数
         * @param permission 所需权限
         * @param allowConsole 是否允许控制台执行
         */
        template <typename TypedHandler>
        ChatCommandBuilder(char const* name, TypedHandler& handler, rbac::RBACPermissions permission, Trinity::ChatCommands::Console allowConsole)
            : ChatCommandBuilder(name, handler, TrinityStrings(), permission, allowConsole)
        {}

        /**
         * @brief 构造子命令容器构建器
         *
         * @param name 命令名称
         * @param subCommands 子命令列表
         */
        ChatCommandBuilder(char const* name, std::vector<ChatCommandBuilder> const& subCommands)
            : _name{ ASSERT_NOTNULL(name) }, _data{ std::in_place_type<SubCommandEntry>, subCommands }
        {}

        /**
         * @brief 已废弃：传统 C 风格处理函数的构造函数
         * @deprecated 请转换为类型安全的参数处理器
         */
        [[deprecated("char const* parameters to command handlers are deprecated; convert this to a typed argument handler instead")]]
        ChatCommandBuilder(char const* name, bool(&handler)(ChatHandler*, char const*), rbac::RBACPermissions permission, Trinity::ChatCommands::Console allowConsole)
            : ChatCommandBuilder(name, handler, TrinityStrings(), permission, allowConsole)
        {}

        /**
         * @brief 已废弃：旧式命令格式构造函数
         * @deprecated 请使用新格式 { name, handler (非指针!), permission, Console::(Yes/No) }
         */
        template <typename TypedHandler>
        [[deprecated("you are using the old-style command format; convert this to the new format ({ name, handler (not a pointer!), permission, Console::(Yes/No) })")]]
        ChatCommandBuilder(char const* name, rbac::RBACPermissions permission, bool console, TypedHandler* handler, char const*)
            : ChatCommandBuilder(name, *handler, TrinityStrings(), permission, static_cast<Trinity::ChatCommands::Console>(console))
        {}

        /**
         * @brief 已废弃：旧式子命令格式构造函数
         * @deprecated 请使用新格式 { name, subCommands }
         */
        [[deprecated("you are using the old-style command format; convert this to the new format ({ name, subCommands })")]]
        ChatCommandBuilder(char const* name, rbac::RBACPermissions, bool, std::nullptr_t, char const*, std::vector <ChatCommandBuilder> const& sub)
            : ChatCommandBuilder(name, sub)
        {}

    private:
        std::string_view _name;  ///< 命令名称
        std::variant<InvokerEntry, SubCommandEntry> _data;  ///< 命令数据（调用器或子命令）
    };

    /**
     * @brief 加载命令映射表
     *
     * 公共接口，委托给 ChatCommandNode::LoadCommandMap。
     */
    TC_GAME_API void LoadCommandMap();

    /**
     * @brief 使命令映射表失效
     *
     * 公共接口，委托给 ChatCommandNode::InvalidateCommandMap。
     */
    TC_GAME_API void InvalidateCommandMap();

    /**
     * @brief 尝试执行命令
     *
     * 公共接口，委托给 ChatCommandNode::TryExecuteCommand。
     *
     * @param handler 聊天处理器
     * @param cmd 命令字符串
     * @return bool 是否成功处理了命令
     */
    TC_GAME_API bool TryExecuteCommand(ChatHandler& handler, std::string_view cmd);

    /**
     * @brief 发送命令帮助信息
     *
     * 公共接口，委托给 ChatCommandNode::SendCommandHelpFor。
     *
     * @param handler 聊天处理器
     * @param cmd 命令字符串
     */
    TC_GAME_API void SendCommandHelpFor(ChatHandler& handler, std::string_view cmd);

    /**
     * @brief 获取命令自动补全建议
     *
     * 公共接口，委托给 ChatCommandNode::GetAutoCompletionsFor。
     *
     * @param handler 聊天处理器
     * @param cmd 部分命令字符串
     * @return std::vector<std::string> 补全建议列表
     */
    TC_GAME_API std::vector<std::string> GetAutoCompletionsFor(ChatHandler const& handler, std::string_view cmd);
}

// backwards compatibility with old patches
// 向后兼容旧补丁的类型别名
using ChatCommand [[deprecated("std::vector<ChatCommand> should be ChatCommandTable! (using namespace Trinity::ChatCommands)")]] = Trinity::ChatCommands::ChatCommandBuilder;

#endif
