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
 * @file ChatCommandHelpers.h
 * @brief 聊天命令解析辅助工具模块
 *
 * 本模块提供了聊天命令系统的基础辅助功能，包括：
 * - 命令字符串的分词处理（tokenize）
 * - 命令解析结果的封装（ChatCommandResult）
 * - 类型特征和模板元编程工具
 * - 错误消息发送和本地化字符串获取接口
 *
 * 这些工具主要供 ChatCommand 和 ChatCommandArgs 模块内部使用，
 * 不建议在其他模块中直接调用。
 */

#ifndef TRINITY_CHATCOMMANDHELPERS_H
#define TRINITY_CHATCOMMANDHELPERS_H

#include "Define.h"
#include "Language.h"
#include "StringFormat.h"
#include <fmt/printf.h>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

class ChatHandler;

namespace Trinity::Impl::ChatCommands
{
    /***************** HELPERS *************************\
    |* These really aren't for outside use...          *|
    |* 这些辅助工具仅供内部使用                        *|
    \***************************************************/

    /**
     * @brief 命令分隔符常量
     *
     * 用于分隔聊天命令中不同参数的分隔符，默认为空格字符。
     * 命令解析器使用此分隔符将输入字符串拆分为多个参数标记。
     */
    static constexpr char COMMAND_DELIMITER = ' ';

    /**
     * @brief 标签基类模板
     *
     * 用于提取类型的标签基类，主要用于模板元编程中获取类型的底层表示。
     *
     * @tparam T 要提取标签的类型
     * @tparam void 默认模板参数，用于特化
     */
    template <typename T, typename = void>
    struct tag_base
    {
        using type = T;  ///< 默认情况下，type 就是 T 本身
    };

    /**
     * @brief 标签基类类型的便捷别名
     *
     * @tparam T 要获取标签类型的参数
     */
    template <typename T>
    using tag_base_t = typename tag_base<T>::type;

    /**
     * @brief 分词结果结构体
     *
     * 封装了将字符串按分隔符分割后的结果，包含：
     * - token: 提取的第一个标记
     * - tail: 剩余的字符串部分
     *
     * 提供了 bool 转换操作符，当 token 非空时返回 true。
     */
    struct TokenizeResult {
        /**
         * @brief bool 转换操作符
         * @return 如果 token 非空则返回 true，否则返回 false
         */
        explicit operator bool() { return !token.empty(); }
        std::string_view token;  ///< 提取的标记
        std::string_view tail;   ///< 剩余字符串
    };

    /**
     * @brief 分词函数，将字符串按分隔符分割
     *
     * 从输入字符串中提取第一个标记和剩余部分。分隔符使用 COMMAND_DELIMITER（空格）。
     *
     * @param args 要分割的输入字符串
     * @return TokenizeResult 包含 token（标记）和 tail（剩余部分）的结构体
     *
     * @par 示例:
     * @code
     * auto [token, tail] = tokenize("command arg1 arg2");
     * // token = "command", tail = "arg1 arg2"
     * @endcode
     *
     * @note 性能说明：此函数使用 string_view 避免内存拷贝，性能开销极低
     */
    inline TokenizeResult tokenize(std::string_view args)
    {
        TokenizeResult result;
        // 查找分隔符位置
        if (size_t delimPos = args.find(COMMAND_DELIMITER); delimPos != std::string_view::npos)
        {
            // 提取分隔符前的部分作为 token
            result.token = args.substr(0, delimPos);
            // 跳过连续的分隔符，找到剩余部分的起始位置
            if (size_t tailPos = args.find_first_not_of(COMMAND_DELIMITER, delimPos); tailPos != std::string_view::npos)
                result.tail = args.substr(tailPos);
        }
        else
        {
            // 没有分隔符，整个字符串作为 token
            result.token = args;
        }

        return result;
    }

    /**
     * @brief 检查所有类型是否都可以赋值给类型 T 的模板
     *
     * 使用折叠表达式检查所有 Ts... 类型是否都可以赋值给 T&。
     * 这用于验证多个参数是否可以赋值给同一个目标类型。
     *
     * @tparam T 目标类型
     * @tparam Ts 要检查的源类型列表
     */
    template <typename T, typename... Ts>
    struct are_all_assignable
    {
        static constexpr bool value = (std::is_assignable_v<T&, Ts> && ...);  ///< 所有 Ts 都能赋值给 T 时为 true
    };

    /**
     * @brief are_all_assignable 的 void 特化版本
     *
     * 当目标类型为 void 时，value 恒为 false。
     */
    template <typename... Ts>
    struct are_all_assignable<void, Ts...>
    {
        static constexpr bool value = false;  ///< void 类型不能被赋值
    };

    /**
     * @brief 获取参数包中第 N 个类型的递归模板
     *
     * 通过递归继承获取类型列表中指定索引位置的类型。
     *
     * @tparam index 要获取的类型索引（从0开始）
     * @tparam T1 当前检查的类型
     * @tparam Ts 剩余的类型列表
     */
    template <std::size_t index, typename T1, typename... Ts>
    struct get_nth : get_nth<index-1, Ts...> { };

    /**
     * @brief get_nth 的递归终止特化版本（index = 0）
     *
     * 当 index 为 0 时，返回当前类型 T1。
     */
    template <typename T1, typename... Ts>
    struct get_nth<0, T1, Ts...>
    {
        using type = T1;  ///< 返回类型 T1
    };

    /**
     * @brief get_nth 的便捷别名
     *
     * @tparam index 要获取的类型索引
     * @tparam Ts 类型列表
     */
    template <std::size_t index, typename... Ts>
    using get_nth_t = typename get_nth<index, Ts...>::type;

    /**
     * @brief 聊天命令解析结果类
     *
     * 本质上类似于 std::optional<std::string_view>，但额外支持存储错误消息。
     * 它提供了类似 std::string_view 的 bool 转换和解引用操作符。
     *
     * 内部存储三种状态：
     * - std::monostate: 未指定错误，通常表示到达字符串末尾或解析失败
     * - std::string: 指定的错误消息，如"角色不存在"或"物品链接无效"
     * - std::string_view: 成功，string_view 是剩余的参数字符串
     *
     * @note 此类不可拷贝，但可以移动
     */
    struct ChatCommandResult
    {
        /**
         * @brief 使用 std::nullopt 构造，表示未指定错误
         * @param n 无效标记
         */
        ChatCommandResult(std::nullopt_t) : _storage() {}

        /// 禁止从 std::string 左值构造
        ChatCommandResult(std::string const&) = delete;

        /**
         * @brief 从 std::string 右值构造，存储错误消息
         * @param s 错误消息字符串
         */
        ChatCommandResult(std::string&& s) : _storage(std::in_place_type<std::string>, std::forward<std::string>(s)) {}

        /**
         * @brief 从 C 字符串构造，存储错误消息
         * @param c 错误消息 C 字符串
         */
        ChatCommandResult(char const* c) : _storage(std::in_place_type<std::string>, c) {}

        /**
         * @brief 从 string_view 构造，表示成功
         * @param s 剩余的参数字符串
         */
        ChatCommandResult(std::string_view s) : _storage(std::in_place_type<std::string_view>, s) {}

        /// 禁止拷贝构造
        ChatCommandResult(ChatCommandResult const&) = delete;
        /// 允许移动构造
        ChatCommandResult(ChatCommandResult&&) = default;
        /// 禁止拷贝赋值
        ChatCommandResult& operator=(ChatCommandResult const&) = delete;
        /// 允许移动赋值
        ChatCommandResult& operator=(ChatCommandResult&&) = default;

        /**
         * @brief 解引用操作符，获取剩余参数字符串
         * @return 剩余的参数字符串视图
         * @pre 必须处于成功状态（IsSuccessful() 为 true）
         */
        std::string_view operator*() const { return std::get<std::string_view>(_storage); }

        /**
         * @brief 检查是否解析成功
         * @return 如果存储的是 string_view（成功），返回 true
         */
        bool IsSuccessful() const { return std::holds_alternative<std::string_view>(_storage); }

        /**
         * @brief bool 转换操作符
         * @return 如果解析成功，返回 true
         */
        explicit operator bool() const { return IsSuccessful(); }

        /**
         * @brief 检查是否包含错误消息
         * @return 如果存储的是错误消息字符串，返回 true
         */
        bool HasErrorMessage() const { return std::holds_alternative<std::string>(_storage); }

        /**
         * @brief 获取错误消息
         * @return 错误消息字符串的常引用
         * @pre 必须包含错误消息（HasErrorMessage() 为 true）
         */
        std::string const& GetErrorMessage() const { return std::get<std::string>(_storage); }

        private:
            /// 内部存储：monostate（未指定错误）/ string_view（成功）/ string（错误消息）
            std::variant<std::monostate, std::string_view, std::string> _storage;
    };

    /**
     * @brief 向 ChatHandler 发送错误消息
     *
     * 将错误消息发送给指定的处理器，并标记已发送错误。
     *
     * @param handler 聊天命令处理器指针
     * @param str 错误消息字符串视图
     *
     * @note 此函数会自动调用 SetSentErrorMessage(true)
     */
    TC_GAME_API void SendErrorMessageToHandler(ChatHandler* handler, std::string_view str);

    /**
     * @brief 获取本地化字符串
     *
     * 从 ChatHandler 获取指定 ID 的本地化字符串。
     *
     * @param handler 聊天命令处理器指针
     * @param which 本地化字符串 ID
     * @return 本地化字符串的 C 风格指针
     */
    TC_GAME_API char const* GetTrinityString(ChatHandler const* handler, TrinityStrings which);

    /**
     * @brief 格式化本地化字符串
     *
     * 使用 printf 风格格式化本地化字符串。
     *
     * @tparam Ts 参数类型列表
     * @param handler 聊天命令处理器指针
     * @param which 本地化字符串 ID
     * @param args 格式化参数
     * @return 格式化后的字符串
     *
     * @note 内部使用 fmt::sprintf 进行格式化
     */
    template <typename... Ts>
    std::string FormatTrinityString(ChatHandler const* handler, TrinityStrings which, Ts&&... args)
    {
        return fmt::sprintf(GetTrinityString(handler, which), std::forward<Ts>(args)...);
    }
}

#endif
