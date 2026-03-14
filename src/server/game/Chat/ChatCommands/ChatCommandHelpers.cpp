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
 * @file ChatCommandHelpers.cpp
 * @brief 聊天命令解析辅助工具实现
 *
 * 本文件实现了 ChatCommandHelpers.h 中声明的辅助函数，包括：
 * - 向命令处理器发送错误消息
 * - 获取本地化字符串
 */

#include "ChatCommandHelpers.h"
#include "Chat.h"
#include "ObjectMgr.h"

/**
 * @brief 向 ChatHandler 发送错误消息
 *
 * 将错误消息发送给指定的处理器，并标记已发送错误消息。
 * 这样可以防止在命令执行失败时重复显示帮助信息。
 *
 * @param handler 聊天命令处理器指针，用于发送消息
 * @param str 错误消息字符串视图
 */
void Trinity::Impl::ChatCommands::SendErrorMessageToHandler(ChatHandler* handler, std::string_view str)
{
    // 发送系统消息给处理器
    handler->SendSysMessage(str);
    // 标记已发送错误消息，防止后续重复显示帮助信息
    handler->SetSentErrorMessage(true);
}

/**
 * @brief 获取本地化字符串
 *
 * 通过 ChatHandler 获取指定 ID 的本地化字符串。
 * 返回的字符串会根据处理器的语言设置进行本地化。
 *
 * @param handler 聊天命令处理器指针
 * @param which 本地化字符串 ID（TrinityStrings 枚举值）
 * @return 本地化字符串的 C 风格指针
 */
char const* Trinity::Impl::ChatCommands::GetTrinityString(ChatHandler const* handler, TrinityStrings which)
{
    return handler->GetTrinityString(which);
}
