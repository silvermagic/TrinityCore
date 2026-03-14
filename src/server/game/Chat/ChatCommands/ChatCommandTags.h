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
 * @file ChatCommandTags.h
 * @brief 聊天命令标签系统头文件
 *
 * 本文件定义了聊天命令系统中使用的各种标签类型,这些标签用于:
 * - 解析命令参数
 * - 验证输入数据
 * - 提供类型安全的参数提取机制
 *
 * 主要包含:
 * - 容器标签基类(ContainerTag)
 * - 精确序列匹配标签(ExactSequence)
 * - 尾部字符串标签(Tail/WTail)
 * - 引号字符串标签(QuotedString)
 * - 账号标识符标签(AccountIdentifier)
 * - 玩家标识符标签(PlayerIdentifier)
 * - 超链接标签(Hyperlink)
 * - 变体类型标签(Variant)
 */

#ifndef TRINITY_CHATCOMMANDTAGS_H
#define TRINITY_CHATCOMMANDTAGS_H

#include "ChatCommandHelpers.h"
#include "Hyperlinks.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Util.h"
#include <boost/preprocessor/repetition/repeat.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <fmt/ostream.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

class ChatHandler;
class Player;
class WorldSession;

namespace Trinity::Impl::ChatCommands
{
    /**
     * @brief 容器标签基类
     *
     * 所有聊天命令标签类型的基类,提供统一的基础接口。
     * 继承此类的标签类型可以实现自定义的参数解析和验证逻辑。
     */
    struct ContainerTag
    {
        using ChatCommandResult = Trinity::Impl::ChatCommands::ChatCommandResult;
    };

    /**
     * @brief 标签基类模板特化
     *
     * 为继承自 ContainerTag 的类型提供 value_type 提取功能
     */
    template <typename T>
    struct tag_base<T, std::enable_if_t<std::is_base_of_v<ContainerTag, T>>>
    {
        using type = typename T::value_type;
    };

    /**
     * @brief 从字符串字面量中获取指定位置的字符
     *
     * 用于 EXACT_SEQUENCE 宏的实现,从编译期字符串中提取字符
     *
     * @tparam N 字符串长度(编译期确定)
     * @param s 字符串数组
     * @param i 字符索引
     * @return constexpr char 返回指定位置的字符,超出范围返回'\0'
     *
     * @note 限制:字符串长度不能超过25个字符
     */
    template <size_t N>
    inline constexpr char GetChar(char const (&s)[N], size_t i)
    {
        static_assert(N <= 25, "The EXACT_SEQUENCE macro can only be used with up to 25 character long literals. Specify them char-by-char (null terminated) as parameters to ExactSequence<> instead.");
        return i >= N ? '\0' : s[i];
    }

/**
 * @brief Boost预处理宏:从字符串字面量中提取字符
 *
 * 用于在编译期将字符串字面量拆分为字符参数包
 */
#define CHATCOMMANDS_IMPL_SPLIT_LITERAL_EXTRACT_CHAR(z, i, strliteral) \
        BOOST_PP_COMMA_IF(i) Trinity::Impl::ChatCommands::GetChar(strliteral, i)

/**
 * @brief Boost预处理宏:按指定长度拆分字符串字面量
 *
 * @param maxlen 最大长度
 * @param strliteral 字符串字面量
 */
#define CHATCOMMANDS_IMPL_SPLIT_LITERAL_CONSTRAINED(maxlen, strliteral)  \
        BOOST_PP_REPEAT(maxlen, CHATCOMMANDS_IMPL_SPLIT_LITERAL_EXTRACT_CHAR, strliteral)

    // 此宏总是创建25个元素 - "abc" -> 'a', 'b', 'c', '\0', '\0', ... 直到25个
    // 用于将字符串字面量转换为可变参数模板参数
#define CHATCOMMANDS_IMPL_SPLIT_LITERAL(strliteral) CHATCOMMANDS_IMPL_SPLIT_LITERAL_CONSTRAINED(25, strliteral)
}

namespace Trinity::ChatCommands
{
    /************************** CONTAINER TAGS **********************************************\
    |* 容器标签类 - 用于区分不同的参数提取方法                                              *|
    |* 必须继承自 Trinity::Impl::ChatCommands::ContainerTag                                *|
    |* 必须实现以下内容:                                                                    *|
    |* - TryConsume: ChatHandler const*, std::string_view -> ChatCommandResult             *|
    |*   - 匹配成功时,返回参数字符串的剩余部分(作为 std::string_view)                       *|
    |*   - 特定错误时,返回错误消息(作为 std::string&& 或 char const*)                       *|
    |*   - 一般错误时,返回 std::nullopt(这将打印命令用法)                                   *|
    |*                                                                                      *|
    |* - typedef value_type 定义标签包含的类型                                              *|
    |* - 转换操作符到 value_type                                                            *|
    |*                                                                                      *|
    \****************************************************************************************/

    /**
     * @brief 精确序列匹配标签
     *
     * 用于匹配精确的字符串序列(不区分大小写)。
     * 通过模板参数包接受字符序列,在编译期构建要匹配的字符串。
     *
     * @tparam chars 要匹配的字符序列,必须以'\0'结尾
     *
     * @example
     * // 匹配 "test" 字符串
     * ExactSequence<'t', 'e', 's', 't', '\0'>
     * // 或使用宏
     * EXACT_SEQUENCE("test")
     */
    template <char... chars>
    struct ExactSequence : Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = void;  // 精确序列不包含值,仅用于匹配

        /**
         * @brief 尝试从参数中消费精确序列
         *
         * @param handler 聊天处理器
         * @param args 待解析的参数字符串
         * @return ChatCommandResult 解析结果
         */
        ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args) const
        {
            if (args.empty())
                return std::nullopt;
            std::string_view start = args.substr(0, _string.length());
            if (StringEqualI(start, _string))
            {
                auto [remainingToken, tail] = Trinity::Impl::ChatCommands::tokenize(args.substr(_string.length()));
                if (remainingToken.empty()) // 如果不为空,说明没有消费完整的token
                    return tail;
                start = args.substr(0, _string.length() + remainingToken.length());
            }
            return Trinity::Impl::ChatCommands::FormatTrinityString(handler, LANG_CMDPARSER_EXACT_SEQ_MISMATCH, STRING_VIEW_FMT_ARG(_string), STRING_VIEW_FMT_ARG(start));
        }

        private:
            // 存储字符序列的静态数组
            static constexpr std::array<char, sizeof...(chars)> _storage = { chars... };
            // 静态断言:序列不能为空且必须以'\0'结尾
            static_assert(!_storage.empty() && (_storage.back() == '\0'), "ExactSequence parameters must be null terminated! Use the EXACT_SEQUENCE macro to make this easier!");
            // 字符串视图,用于实际匹配
            static constexpr std::string_view _string = { _storage.data(), std::string_view::traits_type::length(_storage.data()) };
    };

/**
 * @brief 简化的精确序列定义宏
 *
 * 用于简化 ExactSequence 的使用,自动处理字符拆分
 *
 * @param str 字符串字面量(最长25个字符)
 *
 * @example
 * EXACT_SEQUENCE("test") // 等价于 ExactSequence<'t','e','s','t','\0'>
 */
#define EXACT_SEQUENCE(str) Trinity::ChatCommands::ExactSequence<CHATCOMMANDS_IMPL_SPLIT_LITERAL(str)>

    /**
     * @brief 尾部字符串标签
     *
     * 消费剩余的所有参数作为字符串视图。
     * 用于需要获取命令行剩余全部内容的场景。
     */
    struct Tail : std::string_view, Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = std::string_view;

        using std::string_view::operator=;

        /**
         * @brief 尝试消费所有剩余参数
         *
         * @param handler 聊天处理器(未使用)
         * @param args 待解析的参数字符串
         * @return ChatCommandResult 总是返回空字符串视图(表示消费了所有内容)
         */
        ChatCommandResult TryConsume(ChatHandler const*,std::string_view args)
        {
            std::string_view::operator=(args);
            return std::string_view();
        }
    };

    /**
     * @brief 宽字符串尾部标签
     *
     * 消费剩余的所有参数并转换为宽字符串(wstring)。
     * 用于需要处理Unicode字符的场景。
     */
    struct WTail : std::wstring, Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = std::wstring;

        using std::wstring::operator=;

        /**
         * @brief 尝试消费所有剩余参数并转换为宽字符
         *
         * @param handler 聊天处理器
         * @param args 待解析的参数字符串
         * @return ChatCommandResult 转换成功返回空视图,失败返回错误消息
         */
        ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args)
        {
            if (Utf8toWStr(args, *this))
                return std::string_view();
            else
                return Trinity::Impl::ChatCommands::GetTrinityString(handler, LANG_CMDPARSER_INVALID_UTF8);
        }
    };

    /**
     * @brief 引号字符串标签
     *
     * 解析用引号(单引号或双引号)包裹的字符串。
     * 支持转义字符和嵌套引号。
     *
     * @example
     * "hello world" -> 解析为 hello world
     * 'test\'s' -> 解析为 test's
     */
    struct QuotedString : std::string, Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = std::string;

        TC_GAME_API ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args);
    };

    /**
     * @brief 账号标识符标签
     *
     * 用于解析和识别游戏账号。
     * 支持通过账号名称或账号ID来识别账号。
     */
    struct TC_GAME_API AccountIdentifier : Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = uint32;

        /**
         * @brief 默认构造函数
         */
        AccountIdentifier() : _id(), _name(), _session(nullptr) {}

        /**
         * @brief 从会话构造
         * @param session 世界会话对象
         */
        AccountIdentifier(WorldSession& session);

        // 类型转换操作符
        operator uint32() const { return _id; }
        operator std::string const& () const { return _name; }
        operator std::string_view() const { return { _name }; }

        // 访问器方法
        uint32 GetID() const { return _id; }
        std::string const& GetName() const { return _name; }
        bool IsConnected() { return _session != nullptr; }
        WorldSession* GetConnectedSession() { return _session; }

        ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args);

        /**
         * @brief 从目标玩家创建账号标识符
         * @param handler 聊天处理器
         * @return 如果目标玩家存在返回其账号标识符,否则返回空
         */
        static Optional<AccountIdentifier> FromTarget(ChatHandler* handler);

        private:
            uint32 _id;                 ///< 账号ID
            std::string _name;          ///< 账号名称
            WorldSession* _session;     ///< 关联的世界会话(如果账号在线)
    };

    /**
     * @brief 玩家标识符标签
     *
     * 用于解析和识别游戏玩家。
     * 支持通过玩家名称、GUID或超链接来识别玩家。
     */
    struct TC_GAME_API PlayerIdentifier : Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = Player*;

        /**
         * @brief 默认构造函数
         */
        PlayerIdentifier() : _name(), _guid(), _player(nullptr) {}

        /**
         * @brief 从玩家对象构造
         * @param player 玩家对象引用
         */
        PlayerIdentifier(Player& player);

        // 类型转换操作符
        operator ObjectGuid() const { return _guid; }
        operator std::string const&() const { return _name; }
        operator std::string_view() const { return _name; }

        // 访问器方法
        std::string const& GetName() const { return _name; }
        ObjectGuid GetGUID() const { return _guid; }
        bool IsConnected() const { return (_player != nullptr); }
        Player* GetConnectedPlayer() const { return _player; }

        ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args);

        /**
         * @brief 从目标玩家创建玩家标识符
         * @param handler 聊天处理器
         * @return 如果目标玩家存在返回其标识符,否则返回空
         */
        static Optional<PlayerIdentifier> FromTarget(ChatHandler* handler);

        /**
         * @brief 从当前玩家自身创建玩家标识符
         * @param handler 聊天处理器
         * @return 如果玩家存在返回其标识符,否则返回空
         */
        static Optional<PlayerIdentifier> FromSelf(ChatHandler* handler);

        /**
         * @brief 从目标或自身创建玩家标识符
         *
         * 优先尝试从目标获取,如果没有目标则使用自身
         *
         * @param handler 聊天处理器
         * @return 玩家标识符
         */
        static Optional<PlayerIdentifier> FromTargetOrSelf(ChatHandler* handler)
        {
            if (Optional<PlayerIdentifier> fromTarget = FromTarget(handler))
                return fromTarget;
            else
                return FromSelf(handler);
        }

        private:
            std::string _name;      ///< 玩家名称
            ObjectGuid _guid;       ///< 玩家GUID
            Player* _player;        ///< 玩家对象指针(如果在线)
    };

    /**
     * @brief 超链接标签模板
     *
     * 用于解析游戏内超链接(如物品链接、技能链接等)。
     *
     * @tparam linktag 链接标签类型,必须定义 value_type 和 tag() 方法
     */
    template <typename linktag>
    struct Hyperlink : Trinity::Impl::ChatCommands::ContainerTag
    {
        using value_type = typename linktag::value_type;
        using storage_type = std::remove_cvref_t<value_type>;

        // 类型转换和解引用操作符
        operator value_type() const { return val; }
        value_type operator*() const { return val; }
        storage_type const* operator->() const { return &val; }

        /**
         * @brief 尝试从参数中消费超链接
         *
         * 解析超链接格式:|c<颜色>|H<标签>:<数据>|h[<文本>]|h|r
         *
         * @param handler 聊天处理器
         * @param args 待解析的参数字符串
         * @return ChatCommandResult 解析结果
         */
        ChatCommandResult TryConsume(ChatHandler const* handler, std::string_view args)
        {
            // 解析超链接
            Trinity::Hyperlinks::HyperlinkInfo info = Trinity::Hyperlinks::ParseSingleHyperlink(args);
            // 无效的超链接无法消费
            if (!info)
                return std::nullopt;

            // 检查是否是正确的标签类型
            if (info.tag != linktag::tag())
                return std::nullopt;

            // 存储值
            if (!linktag::StoreTo(val, info.data))
                return Trinity::Impl::ChatCommands::GetTrinityString(handler, LANG_CMDPARSER_LINKDATA_INVALID);

            // 最后,跳过任何潜在的分隔符
            auto [token, next] = Trinity::Impl::ChatCommands::tokenize(info.tail);
            if (token.empty()) /* 空token = 第一个字符是分隔符,跳过它 */
                return next;
            else
                return info.tail;
        }

        private:
            storage_type val;  ///< 存储的值
    };

    // 为用户方便引入链接标签命名空间
    using namespace ::Trinity::Hyperlinks::LinkTags;
}

namespace Trinity::Impl
{
    /**
     * @brief 类型转换访问器
     *
     * 用于 std::visit 的访问器,将所有变体类型转换为统一的目标类型
     *
     * @tparam T 目标类型
     */
    template <typename T>
    struct CastToVisitor
    {
        template <typename U>
        T operator()(U const& v) const { return v; }
    };
}

namespace Trinity::ChatCommands
{
    /**
     * @brief 变体类型标签
     *
     * 扩展 std::variant,提供额外的便利功能。
     * 用于支持多种可能的参数类型。
     *
     * @tparam T1 第一个类型
     * @tparam Ts 其他类型
     *
     * @example
     * Variant<int, std::string> v; // 可以是int或string
     * v = 42;
     * v = "hello";
     */
    template <typename T1, typename... Ts>
    struct Variant : public std::variant<T1, Ts...>
    {
        using base = std::variant<T1, Ts...>;

        using first_type = Trinity::Impl::ChatCommands::tag_base_t<T1>;
        static constexpr bool have_operators = Trinity::Impl::ChatCommands::are_all_assignable<first_type, Trinity::Impl::ChatCommands::tag_base_t<Ts>...>::value;

        /**
         * @brief 解引用操作符
         *
         * 将变体值转换为第一个类型
         *
         * @tparam C 是否具有操作符(编译期检查)
         * @return first_type 转换后的值
         */
        template <bool C = have_operators>
        std::enable_if_t<C, first_type> operator*() const
        {
            return visit(Trinity::Impl::CastToVisitor<first_type>());
        }

        /**
         * @brief 转换到第一个类型的操作符
         */
        template <bool C = have_operators>
        operator std::enable_if_t<C, first_type>() const
        {
            return operator*();
        }

        /**
         * @brief 逻辑非操作符
         */
        template <bool C = have_operators>
        std::enable_if_t<C, bool> operator!() const { return !**this; }

        /**
         * @brief 赋值操作符
         */
        template <typename T>
        Variant& operator=(T&& arg) { base::operator=(std::forward<T>(arg)); return *this; }

        /**
         * @brief 按索引获取值
         * @tparam index 索引
         * @return 值的引用
         */
        template <size_t index>
        constexpr decltype(auto) get() { return std::get<index>(static_cast<base&>(*this)); }

        template <size_t index>
        constexpr decltype(auto) get() const { return std::get<index>(static_cast<base const&>(*this)); }

        /**
         * @brief 按类型获取值
         * @tparam type 类型
         * @return 值的引用
         */
        template <typename type>
        constexpr decltype(auto) get() { return std::get<type>(static_cast<base&>(*this)); }

        template <typename type>
        constexpr decltype(auto) get() const { return std::get<type>(static_cast<base const&>(*this)); }

        /**
         * @brief 访问器模式
         *
         * 使用访问器函数对象访问变体值
         *
         * @tparam T 访问器类型
         * @param arg 访问器对象
         * @return 访问器返回值
         */
        template <typename T>
        constexpr decltype(auto) visit(T&& arg) { return std::visit(std::forward<T>(arg), static_cast<base&>(*this)); }

        template <typename T>
        constexpr decltype(auto) visit(T&& arg) const { return std::visit(std::forward<T>(arg), static_cast<base const&>(*this)); }

        /**
         * @brief 检查是否持有特定类型
         *
         * @tparam T 要检查的类型
         * @return true 如果持有该类型
         * @return false 如果不持有该类型
         */
        template <typename T>
        constexpr bool holds_alternative() const { return std::holds_alternative<T>(static_cast<base const&>(*this)); }

        /**
         * @brief 流输出操作符
         *
         * 将变体值输出到流
         */
        template <bool C = have_operators>
        friend std::enable_if_t<C, std::ostream&> operator<<(std::ostream& os, Trinity::ChatCommands::Variant<T1, Ts...> const& v)
        {
            return (os << *v);
        }
    };
}

template <typename T1, typename... Ts>
struct fmt::formatter<Trinity::ChatCommands::Variant<T1, Ts...>> : ostream_formatter { };

template <typename T1, typename... Ts>
struct fmt::printf_formatter<Trinity::ChatCommands::Variant<T1, Ts...>> : formatter<T1>
{
    template <typename T, typename OutputIt>
    auto format(T const& value, basic_format_context<OutputIt, char>& ctx) const -> OutputIt
    {
        return formatter<T1>::format(*value, ctx);
    }
};

#endif
