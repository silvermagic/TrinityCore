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
 * @file ChatCommandArgs.h
 * @brief 聊天命令参数解析器模块
 *
 * 本模块定义了如何从字符串中提取各种类型的参数值，是聊天命令系统的核心组件之一。
 * 主要功能包括：
 * - 为不同数据类型（整数、浮点数、字符串、枚举、游戏对象等）提供参数提取器
 * - 支持可选参数（Optional）和可变参数（Variant）
 * - 支持容器类型（vector、array）
 * - 支持游戏内链接（成就、物品、任务、法术等）
 *
 * 每种类型的参数解析器通过 ArgInfo 模板特化实现，必须提供 TryConsume 静态方法。
 */

#ifndef TRINITY_CHATCOMMANDARGS_H
#define TRINITY_CHATCOMMANDARGS_H

#include "ChatCommandHelpers.h"
#include "ChatCommandTags.h"
#include "SmartEnum.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Util.h"
#include <charconv>
#include <map>
#include <string>
#include <string_view>

struct GameTele;

namespace Trinity::Impl::ChatCommands
{

    /************************** ARGUMENT HANDLERS *******************************************\
    |* Define how to extract contents of a certain requested type from a string             *|
    |* 定义如何从字符串中提取特定请求类型的参数                                             *|
    |*                                                                                      *|
    |* Must implement the following:                                                        *|
    |* 必须实现以下接口：                                                                   *|
    |* - TryConsume: T&, ChatHandler const*, std::string_view -> ChatCommandResult          *|
    |*   - on match, returns tail of the provided argument string (as std::string_view)     *|
    |*     匹配成功时，返回参数字符串的剩余部分（作为 std::string_view）                    *|
    |*   - on specific error, returns error message (as std::string&& or char const*)       *|
    |*     特定错误时，返回错误消息（作为 std::string&& 或 char const*）                    *|
    |*   - on generic error, returns std::nullopt (this will print command usage)           *|
    |*     通用错误时，返回 std::nullopt（这将打印命令用法）                                *|
    |*                                                                                      *|
    |*   - if a match is returned, T& should be initialized to the matched value            *|
    |*     如果返回匹配，T& 应该被初始化为匹配的值                                          *|
    |*   - otherwise, the state of T& is indeterminate and caller will not use it           *|
    |*     否则，T& 的状态是不确定的，调用者不会使用它                                      *|
    |*                                                                                      *|
    \****************************************************************************************/

    /**
     * @brief 参数信息提取器基类模板
     *
     * 定义了如何从字符串中提取特定类型参数的通用接口。
     * 每种需要支持的类型都应该提供此模板的特化版本。
     *
     * @tparam T 要提取的参数类型
     * @tparam void SFINAE 控制参数，用于类型约束
     *
     * @note 如果尝试使用未特化的 ArgInfo，会触发静态断言失败
     */
    template <typename T, typename = void>
    struct ArgInfo { static_assert(Trinity::dependant_false_v<T>, "Invalid command parameter type - see ChatCommandArgs.h for possible types"); };

    /**
     * @brief 数值类型（整数和浮点数）的参数提取器特化
     *
     * 为所有整数类型和浮点类型提供统一的参数提取实现。
     * 支持从字符串中解析各种进制和格式的数值。
     *
     * @tparam T 数值类型（必须满足 std::is_integral_v 或 std::is_floating_point_v）
     */
    template <typename T>
    struct ArgInfo<T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
    {
        /**
         * @brief 尝试从字符串中提取数值
         *
         * @param val 输出参数，存储解析结果
         * @param handler 聊天处理器，用于获取本地化字符串
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果：
         *         - 成功时返回剩余字符串
         *         - 特定错误时返回错误消息
         *         - 通用错误时返回 nullopt
         */
        static ChatCommandResult TryConsume(T& val, ChatHandler const* handler, std::string_view args)
        {
            // 从参数字符串中提取第一个标记
            auto [token, tail] = tokenize(args);
            if (token.empty())
                return std::nullopt;

            // 尝试将标记转换为数值
            if (Optional<T> v = StringTo<T>(token, 0))
                val = *v;
            else
                return FormatTrinityString(handler, LANG_CMDPARSER_STRING_VALUE_INVALID, STRING_VIEW_FMT_ARG(token), Trinity::GetTypeName<T>().c_str());

            // 对浮点数额外检查是否为有限值（非无穷大或 NaN）
            if constexpr (std::is_floating_point_v<T>)
            {
                if (!std::isfinite(val))
                    return FormatTrinityString(handler, LANG_CMDPARSER_STRING_VALUE_INVALID, STRING_VIEW_FMT_ARG(token), Trinity::GetTypeName<T>().c_str());
            }

            return tail;
        }
    };

    /**
     * @brief string_view 类型的参数提取器特化
     *
     * 提取字符串视图参数，不进行任何转换。
     * 这是最基础的字符串提取器，性能最优，无内存拷贝。
     */
    template <>
    struct ArgInfo<std::string_view, void>
    {
        /**
         * @brief 尝试从字符串中提取 string_view
         *
         * @param val 输出参数，存储提取的字符串视图
         * @param handler 聊天处理器（此特化中未使用）
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(std::string_view& val, ChatHandler const*, std::string_view args)
        {
            auto [token, next] = tokenize(args);
            if (token.empty())
                return std::nullopt;
            val = token;
            return next;
        }
    };

    /**
     * @brief std::string 类型的参数提取器特化
     *
     * 提取字符串参数并存储为 std::string。
     * 内部复用 string_view 提取器，然后拷贝到 string 中。
     */
    template <>
    struct ArgInfo<std::string, void>
    {
        /**
         * @brief 尝试从字符串中提取 std::string
         *
         * @param val 输出参数，存储提取的字符串
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(std::string& val, ChatHandler const* handler, std::string_view args)
        {
            std::string_view view;
            // 先提取 string_view
            ChatCommandResult next = ArgInfo<std::string_view>::TryConsume(view, handler, args);
            if (next)
                val.assign(view);  // 拷贝到 string
            return next;
        }
    };

    /**
     * @brief std::wstring 类型的参数提取器特化
     *
     * 提取 UTF-8 字符串参数并转换为宽字符串（std::wstring）。
     * 支持非 ASCII 字符，如中文、日文等。
     */
    template <>
    struct ArgInfo<std::wstring, void>
    {
        /**
         * @brief 尝试从字符串中提取 std::wstring
         *
         * @param val 输出参数，存储转换后的宽字符串
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果，UTF-8 转换失败时返回错误消息
         */
        static ChatCommandResult TryConsume(std::wstring& val, ChatHandler const* handler, std::string_view args)
        {
            std::string_view utf8view;
            // 先提取 UTF-8 string_view
            ChatCommandResult next = ArgInfo<std::string_view>::TryConsume(utf8view, handler, args);

            if (next)
            {
                // 尝试将 UTF-8 转换为宽字符串
                if (Utf8toWStr(utf8view, val))
                    return next;
                else
                    return GetTrinityString(handler, LANG_CMDPARSER_INVALID_UTF8);
            }
            else
                return std::nullopt;
        }
    };

    /**
     * @brief 枚举类型的参数提取器特化
     *
     * 支持通过名称或数值来指定枚举值。
     * 枚举名称支持前缀匹配和大小写不敏感匹配。
     *
     * @tparam T 枚举类型
     */
    template <typename T>
    struct ArgInfo<T, std::enable_if_t<std::is_enum_v<T>>>
    {
        /// 搜索映射类型：字符串 -> 可选枚举值（不区分大小写的有序映射）
        using SearchMap = std::map<std::string_view, Optional<T>, StringCompareLessI_T>;

        /**
         * @brief 创建枚举值搜索映射表
         *
         * 遍历枚举的所有值，将名称和常量名添加到映射表中。
         * 如果存在重复名称，会将值设置为 nullopt 以表示歧义。
         *
         * @return SearchMap 构建好的搜索映射表
         */
        static SearchMap MakeSearchMap()
        {
            SearchMap map;
            for (T val : EnumUtils::Iterate<T>())
            {
                EnumText text = EnumUtils::ToString(val);

                std::string_view title(text.Title);
                std::string_view constant(text.Constant);

                // 将常量名添加到映射
                auto [constantIt, constantNew] = map.try_emplace(title, val);
                if (!constantNew)
                    constantIt->second = std::nullopt;  // 重复名称，标记为歧义

                // 如果标题和常量名不同，也添加标题
                if (title != constant)
                {
                    auto [titleIt, titleNew] = map.try_emplace(title, val);
                    if (!titleNew)
                        titleIt->second = std::nullopt;  // 重复名称，标记为歧义
                }
            }
            return map;
        }

        /// 静态搜索映射表，在程序启动时初始化
        static inline SearchMap const _map = MakeSearchMap();

        /**
         * @brief 匹配枚举值
         *
         * 支持前缀匹配和精确匹配。如果输入字符串匹配到多个枚举名称，
         * 则返回 nullptr 表示歧义。
         *
         * @param s 输入字符串
         * @return T const* 匹配到的枚举值指针，未匹配或歧义时返回 nullptr
         */
        static T const* Match(std::string_view s)
        {
            // 使用 lower_bound 找到第一个可能的匹配项
            auto it = _map.lower_bound(s);
            if (it == _map.end() || !StringStartsWithI(it->first, s)) // not a match
                return nullptr;

            if (!StringEqualI(it->first, s)) // we don't have an exact match - check if it is unique
            {
                auto it2 = it;
                ++it2;
                if ((it2 != _map.end()) && StringStartsWithI(it2->first, s)) // not unique
                    return nullptr;
            }

            if (it->second)
                return &*it->second;
            else
                return nullptr;  // 歧义名称
        }

        /**
         * @brief 尝试从字符串中提取枚举值
         *
         * 首先尝试按名称匹配，如果失败则尝试按底层类型解析数值。
         *
         * @param val 输出参数，存储解析到的枚举值
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(T& val, ChatHandler const* handler, std::string_view args)
        {
            std::string_view strVal;
            // 首先尝试字符串匹配
            ChatCommandResult next1 = ArgInfo<std::string_view>::TryConsume(strVal, handler, args);
            if (next1)
            {
                if (T const* match = Match(strVal))
                {
                    val = *match;
                    return next1;
                }
            }

            // Value not found. Try to parse arg as underlying type and cast it to enum type
            // 如果名称匹配失败，尝试将参数解析为底层数值类型
            using U = std::underlying_type_t<T>;
            U uVal = 0;
            if (ChatCommandResult next2 = ArgInfo<U>::TryConsume(uVal, handler, args))
            {
                if (EnumUtils::IsValid<T>(uVal))
                {
                    val = static_cast<T>(uVal);
                    return next2;
                }
            }

            if (next1)
                return FormatTrinityString(handler, LANG_CMDPARSER_STRING_VALUE_INVALID, STRING_VIEW_FMT_ARG(strVal), Trinity::GetTypeName<T>().c_str());
            else
                return next1;
        }
    };

    /**
     * @brief 容器标签类型的参数提取器特化
     *
     * 用于处理自定义容器标签类型（如 Hyperlink 等）。
     * 这些类型通过继承 ContainerTag 来标识自己。
     *
     * @tparam T 容器标签类型（必须继承自 ContainerTag）
     */
    template <typename T>
    struct ArgInfo<T, std::enable_if_t<std::is_base_of_v<ContainerTag, T>>>
    {
        /**
         * @brief 尝试从字符串中提取容器标签值
         *
         * 直接委托给标签类型的 TryConsume 方法。
         *
         * @param tag 输出参数，存储提取的标签值
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(T& tag, ChatHandler const* handler, std::string_view args)
        {
            return tag.TryConsume(handler, args);
        }
    };

    /**
     * @brief std::vector<T> 类型的参数提取器特化
     *
     * 提取非空的元素列表，每个元素使用 T 的 ArgInfo 解析。
     * 要求至少有一个元素才能成功。
     *
     * @tparam T vector 元素类型
     */
    template <typename T>
    struct ArgInfo<std::vector<T>, void>
    {
        /**
         * @brief 尝试从字符串中提取元素向量
         *
         * @param val 输出参数，存储提取的元素向量
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果，至少需要一个元素
         */
        static ChatCommandResult TryConsume(std::vector<T>& val, ChatHandler const* handler, std::string_view args)
        {
            val.clear();
            // 提取第一个元素（必须成功）
            ChatCommandResult next = ArgInfo<T>::TryConsume(val.emplace_back(), handler, args);

            if (!next)
                return next;

            // 继续提取更多元素（可选）
            while (ChatCommandResult next2 = ArgInfo<T>::TryConsume(val.emplace_back(), handler, *next))
                next = std::move(next2);

            // 最后一个 emplace_back 总是失败，需要移除
            val.pop_back();
            return next;
        }
    };

    /**
     * @brief std::array<T, N> 类型的参数提取器特化
     *
     * 提取固定数量的元素，所有 N 个元素都必须成功解析。
     *
     * @tparam T array 元素类型
     * @tparam N array 大小
     */
    template <typename T, size_t N>
    struct ArgInfo<std::array<T, N>, void>
    {
        /**
         * @brief 尝试从字符串中提取固定大小数组
         *
         * @param val 输出参数，存储提取的元素数组
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果，所有元素都必须成功解析
         */
        static ChatCommandResult TryConsume(std::array<T, N>& val, ChatHandler const* handler, std::string_view args)
        {
            ChatCommandResult next = args;
            // 逐个提取元素，任何一个失败则停止
            for (T& t : val)
                if (!(next = ArgInfo<T>::TryConsume(t, handler, *next)))
                    break;
            return next;
        }
    };

    /**
     * @brief Variant<Ts...> 类型的参数提取器特化
     *
     * 支持多种可能的参数类型，按顺序尝试解析，第一个成功的结果会被使用。
     * 当所有类型都解析失败时，会合并显示所有错误消息。
     *
     * @tparam Ts variant 可能的类型列表
     */
    template <typename... Ts>
    struct ArgInfo<Trinity::ChatCommands::Variant<Ts...>>
    {
        using V = std::variant<Ts...>;  ///< 底层 variant 类型
        static constexpr size_t N = std::variant_size_v<V>;  ///< variant 类型数量

        /**
         * @brief 在指定索引处尝试解析 variant
         *
         * 递归尝试解析 variant 的每个类型，直到找到成功的类型或遍历完所有类型。
         *
         * @tparam I 当前尝试的类型索引
         * @param val 输出参数，存储解析结果
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        template <size_t I>
        static ChatCommandResult TryAtIndex([[maybe_unused]] Trinity::ChatCommands::Variant<Ts...>& val, [[maybe_unused]] ChatHandler const* handler, [[maybe_unused]] std::string_view args)
        {
            if constexpr (I < N)
            {
                // 尝试解析为第 I 种类型
                ChatCommandResult thisResult = ArgInfo<std::variant_alternative_t<I, V>>::TryConsume(val.template emplace<I>(), handler, args);
                if (thisResult)
                    return thisResult;
                else
                {
                    // 当前类型解析失败，尝试下一种类型
                    ChatCommandResult nestedResult = TryAtIndex<I + 1>(val, handler, args);
                    if (nestedResult || !thisResult.HasErrorMessage())
                        return nestedResult;
                    if (!nestedResult.HasErrorMessage())
                        return thisResult;
                    // 合并两个错误消息，使用"或"连接
                    if (StringStartsWith(nestedResult.GetErrorMessage(), "\""))
                        return Trinity::StringFormat("\"{}\"\n{} {}", thisResult.GetErrorMessage(), GetTrinityString(handler, LANG_CMDPARSER_OR), nestedResult.GetErrorMessage());
                    else
                        return Trinity::StringFormat("\"{}\"\n{} \"{}\"", thisResult.GetErrorMessage(), GetTrinityString(handler, LANG_CMDPARSER_OR), nestedResult.GetErrorMessage());
                }
            }
            else
                return std::nullopt;  // 所有类型都尝试过了
        }

        /**
         * @brief 尝试从字符串中提取 variant 值
         *
         * @param val 输出参数，存储解析结果
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(Trinity::ChatCommands::Variant<Ts...>& val, ChatHandler const* handler, std::string_view args)
        {
            ChatCommandResult result = TryAtIndex<0>(val, handler, args);
            // 如果错误消息包含换行符（表示多个错误），添加"要么"前缀
            if (result.HasErrorMessage() && (result.GetErrorMessage().find('\n') != std::string::npos))
                return Trinity::StringFormat("{} {}", GetTrinityString(handler, LANG_CMDPARSER_EITHER), result.GetErrorMessage());
            return result;
        }
    };

    /**
     * @brief AchievementEntry const* 类型的参数提取器特化
     *
     * 支持通过数值 ID 或游戏内成就链接来指定成就。
     *
     * @note 实现在 ChatCommandArgs.cpp 中
     */
    template <>
    struct TC_GAME_API ArgInfo<AchievementEntry const*>
    {
        /**
         * @brief 尝试从字符串中提取成就条目指针
         * @param data 输出参数，存储提取的成就条目指针
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(AchievementEntry const*&, ChatHandler const*, std::string_view);
    };

    /**
     * @brief GameTele const* 类型的参数提取器特化
     *
     * 支持通过传送点名称或游戏内链接来指定传送点。
     *
     * @note 实现在 ChatCommandArgs.cpp 中
     */
    template <>
    struct TC_GAME_API ArgInfo<GameTele const*>
    {
        /**
         * @brief 尝试从字符串中提取传送点指针
         * @param data 输出参数，存储提取的传送点指针
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(GameTele const*&, ChatHandler const*, std::string_view);
    };

    /**
     * @brief ItemTemplate const* 类型的参数提取器特化
     *
     * 支持通过物品 ID 或游戏内物品链接来指定物品模板。
     *
     * @note 实现在 ChatCommandArgs.cpp 中
     */
    template <>
    struct TC_GAME_API ArgInfo<ItemTemplate const*>
    {
        /**
         * @brief 尝试从字符串中提取物品模板指针
         * @param data 输出参数，存储提取的物品模板指针
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(ItemTemplate const*&, ChatHandler const*, std::string_view);
    };

    /**
     * @brief Quest const* 类型的参数提取器特化
     *
     * 支持通过任务 ID 或游戏内任务链接来指定任务。
     *
     * @note 实现在 ChatCommandArgs.cpp 中
     */
    template <>
    struct TC_GAME_API ArgInfo<Quest const*>
    {
        /**
         * @brief 尝试从字符串中提取任务指针
         * @param data 输出参数，存储提取的任务指针
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(Quest const*&, ChatHandler const*, std::string_view);
    };

    /**
     * @brief SpellInfo const* 类型的参数提取器特化
     *
     * 支持通过法术 ID 或多种游戏内链接（法术、天赋、附魔、雕文、商业技能）来指定法术。
     *
     * @note 实现在 ChatCommandArgs.cpp 中
     */
    template <>
    struct TC_GAME_API ArgInfo<SpellInfo const*>
    {
        /**
         * @brief 尝试从字符串中提取法术信息指针
         * @param data 输出参数，存储提取的法术信息指针
         * @param handler 聊天处理器
         * @param args 输入参数字符串
         * @return ChatCommandResult 解析结果
         */
        static ChatCommandResult TryConsume(SpellInfo const*&, ChatHandler const*, std::string_view);
    };

}

#endif
