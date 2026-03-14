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
 * @file ChatCommandTags.cpp
 * @brief 聊天命令标签实现文件
 *
 * 本文件实现了聊天命令系统中使用的各种标签类型,包括:
 * - QuotedString: 引号字符串解析
 * - AccountIdentifier: 账号标识符解析
 * - PlayerIdentifier: 玩家标识符解析
 *
 * 这些标签用于在聊天命令中解析和验证不同类型的参数,提供了类型安全的参数提取机制。
 */

#include "ChatCommandTags.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ChatCommandArgs.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

using namespace Trinity::Impl::ChatCommands;

/**
 * @brief 尝试从参数字符串中消费引号字符串
 *
 * 支持双引号和单引号包裹的字符串,支持转义字符(\)。
 * 如果没有引号,则回退到普通字符串解析。
 *
 * @param handler 聊天处理器,用于获取错误消息
 * @param args 待解析的参数字符串
 * @return ChatCommandResult 解析结果:
 *         - 成功:返回剩余的未解析字符串
 *         - 失败:返回std::nullopt或错误消息
 *
 * @note 示例:
 *       - "hello world" -> 解析出 hello world
 *       - 'test\'s' -> 解析出 test's
 *       - normal -> 按普通字符串解析
 */
ChatCommandResult Trinity::ChatCommands::QuotedString::TryConsume(ChatHandler const* handler, std::string_view args)
{
    // 空字符串无法消费
    if (args.empty())
        return std::nullopt;

    // 如果不是引号开头,则使用普通字符串解析
    if ((args[0] != '"') && (args[0] != '\''))
        return ArgInfo<std::string>::TryConsume(*this, handler, args);

    // 记录引号类型(双引号或单引号)
    char const QUOTE = args[0];

    // 从引号后开始遍历,寻找配对的结束引号
    for (size_t i = 1; i < args.length(); ++i)
    {
        // 找到匹配的结束引号
        if (args[i] == QUOTE)
        {
            // 检查引号后是否有分隔符
            auto [remainingToken, tail] = tokenize(args.substr(i + 1));
            if (remainingToken.empty()) // 如果token为空,说明正确消费了整个引号字符串
                return tail;
            else
                return std::nullopt;
        }

        // 处理转义字符
        if (args[i] == '\\')
        {
            ++i; // 跳过转义字符,处理下一个字符
            if (!(i < args.length()))
                break;
        }

        // 将字符添加到结果字符串中
        std::string::push_back(args[i]);
    }

    // 如果到达这里,说明没有找到闭合引号
    return std::nullopt;
}

/**
 * @brief AccountIdentifier 构造函数,从 WorldSession 初始化
 *
 * @param session 世界会话对象引用
 */
Trinity::ChatCommands::AccountIdentifier::AccountIdentifier(WorldSession& session)
    : _id(session.GetAccountId()), _name(session.GetAccountName()), _session(&session) {}

/**
 * @brief 尝试从参数字符串中消费账号标识符
 *
 * 支持通过账号名称或账号ID来识别账号。
 * 首先尝试按账号名称解析,失败后尝试按账号ID解析。
 *
 * @param handler 聊天处理器,用于获取错误消息
 * @param args 待解析的参数字符串
 * @return ChatCommandResult 解析结果:
 *         - 成功:返回剩余的未解析字符串
 *         - 失败:返回错误消息
 *
 * @note 解析顺序:
 *       1. 先尝试作为账号名称解析
 *       2. 失败后尝试作为账号ID(数字)解析
 */
ChatCommandResult Trinity::ChatCommands::AccountIdentifier::TryConsume(ChatHandler const* handler, std::string_view args)
{
    std::string_view text;
    // 首先提取参数文本
    ChatCommandResult next = ArgInfo<std::string_view>::TryConsume(text, handler, args);
    if (!next)
        return next;

    // 首先尝试按账号名称解析
    _name.assign(text);
    // 将名称转换为大写(仅拉丁字符)
    if (!Utf8ToUpperOnlyLatin(_name))
        return GetTrinityString(handler, LANG_CMDPARSER_INVALID_UTF8);

    // 查询账号ID
    _id = AccountMgr::GetId(_name);
    // 查找对应的会话
    _session = sWorld->FindSession(_id);

    // 如果账号存在,则成功返回
    if (_id)
        return next;

    // 尝试按账号ID解析(数字形式)
    Optional<uint32> id = Trinity::StringTo<uint32>(text, 10);
    if (!id)
        return FormatTrinityString(handler, LANG_CMDPARSER_ACCOUNT_NAME_NO_EXIST, STRING_VIEW_FMT_ARG(_name));

    _id = *id;
    _session = sWorld->FindSession(_id);

    // 根据账号ID获取账号名称
    if (AccountMgr::GetName(_id, _name))
        return next;
    else
        return FormatTrinityString(handler, LANG_CMDPARSER_ACCOUNT_ID_NO_EXIST, _id);
}

/**
 * @brief 从目标玩家创建账号标识符
 *
 * 通过当前玩家选中的目标玩家来获取其账号信息
 *
 * @param handler 聊天处理器
 * @return Optional<AccountIdentifier> 如果目标玩家存在,返回其账号标识符;否则返回空
 *
 * @note 调用时机:当需要从当前选中的目标玩家获取账号信息时
 */
Optional<Trinity::ChatCommands::AccountIdentifier> Trinity::ChatCommands::AccountIdentifier::FromTarget(ChatHandler* handler)
{
    // 获取当前玩家
    if (Player* player = handler->GetPlayer())
        // 获取选中的目标玩家
        if (Player* target = player->GetSelectedPlayer())
            // 获取目标的会话并创建账号标识符
            if (WorldSession* session = target->GetSession())
                return { *session };
    return std::nullopt;
}

/**
 * @brief 尝试从参数字符串中消费玩家标识符
 *
 * 支持多种格式识别玩家:
 * 1. 玩家链接超链接(|Hplayer:xxx|h)
 * 2. 玩家GUID(数字)
 * 3. 玩家名称(文本)
 *
 * @param handler 聊天处理器,用于获取错误消息
 * @param args 待解析的参数字符串
 * @return ChatCommandResult 解析结果:
 *         - 成功:返回剩余的未解析字符串
 *         - 失败:返回错误消息
 *
 * @note 解析优先级:
 *       1. 先尝试作为GUID解析
 *       2. 再尝试作为超链接或名称解析
 */
ChatCommandResult Trinity::ChatCommands::PlayerIdentifier::TryConsume(ChatHandler const* handler, std::string_view args)
{
    // 使用变体类型支持多种输入格式
    Variant<Hyperlink<player>, ObjectGuid::LowType, std::string_view> val;
    ChatCommandResult next = ArgInfo<decltype(val)>::TryConsume(val, handler, args);
    if (!next)
        return next;

    // 如果是GUID类型(数字)
    if (val.holds_alternative<ObjectGuid::LowType>())
    {
        // 从GUID创建玩家对象GUID
        _guid = ObjectGuid::Create<HighGuid::Player>(val.get<ObjectGuid::LowType>());
        // 尝试在线查找玩家
        if ((_player = ObjectAccessor::FindPlayerByLowGUID(_guid.GetCounter())))
            _name = _player->GetName();
        // 如果玩家不在线,从缓存中获取名称
        else if (!sCharacterCache->GetCharacterNameByGuid(_guid, _name))
            return FormatTrinityString(handler, LANG_CMDPARSER_CHAR_GUID_NO_EXIST, _guid.ToString().c_str());
        return next;
    }
    else
    {
        // 处理超链接或名称格式
        if (val.holds_alternative<Hyperlink<player>>())
            // 从玩家超链接中提取名称
            _name.assign(static_cast<std::string_view>(val.get<Hyperlink<player>>()));
        else
            // 直接使用文本作为名称
            _name.assign(val.get<std::string_view>());

        // 规范化玩家名称(首字母大写)
        if (!normalizePlayerName(_name))
            return FormatTrinityString(handler, LANG_CMDPARSER_CHAR_NAME_INVALID, STRING_VIEW_FMT_ARG(_name));

        // 尝试在线查找玩家
        if ((_player = ObjectAccessor::FindPlayerByName(_name)))
            _guid = _player->GetGUID();
        // 如果玩家不在线,从缓存中获取GUID
        else if (!(_guid = sCharacterCache->GetCharacterGuidByName(_name)))
            return FormatTrinityString(handler, LANG_CMDPARSER_CHAR_NAME_NO_EXIST, STRING_VIEW_FMT_ARG(_name));
        return next;
    }
}

/**
 * @brief PlayerIdentifier 构造函数,从 Player 对象初始化
 *
 * @param player 玩家对象引用
 */
Trinity::ChatCommands::PlayerIdentifier::PlayerIdentifier(Player& player)
    : _name(player.GetName()), _guid(player.GetGUID()), _player(&player) {}

/**
 * @brief 从目标玩家创建玩家标识符
 *
 * 通过当前玩家选中的目标玩家来获取其信息
 *
 * @param handler 聊天处理器
 * @return Optional<PlayerIdentifier> 如果目标玩家存在,返回其标识符;否则返回空
 *
 * @note 调用时机:当需要从当前选中的目标玩家获取玩家信息时
 */
/*static*/ Optional<Trinity::ChatCommands::PlayerIdentifier> Trinity::ChatCommands::PlayerIdentifier::FromTarget(ChatHandler* handler)
{
    if (Player* player = handler->GetPlayer())
        if (Player* target = player->GetSelectedPlayer())
            return { *target };
    return std::nullopt;

}

/**
 * @brief 从当前玩家自身创建玩家标识符
 *
 * 获取执行命令的玩家自身的信息
 *
 * @param handler 聊天处理器
 * @return Optional<PlayerIdentifier> 如果玩家存在,返回其标识符;否则返回空
 *
 * @note 调用时机:当需要获取当前玩家自身的信息时
 */
/*static*/ Optional<Trinity::ChatCommands::PlayerIdentifier> Trinity::ChatCommands::PlayerIdentifier::FromSelf(ChatHandler* handler)
{
    if (Player* player = handler->GetPlayer())
        return { *player };
    return std::nullopt;
}
