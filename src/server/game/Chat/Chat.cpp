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
 * @file Chat.cpp
 * @brief 聊天系统核心实现文件
 *
 * 本文件实现了聊天系统的核心功能,包括:
 * - ChatHandler: 聊天命令处理器的基类实现
 * - CliHandler: 控制台命令处理器实现
 * - AddonChannelCommandHandler: 插件频道命令处理器实现
 *
 * 主要功能:
 * - 命令解析和执行
 * - 聊天消息构建和发送
 * - 权限验证
 * - 目标选择和对象查找
 * - 超链接解析
 * - 错误处理和消息报告
 */

#include "Chat.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "GridNotifiersImpl.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Optional.h"
#include "Player.h"
#include "Realm.h"
#include "StringConvert.h"
#include "World.h"
#include "WorldSession.h"
#include <boost/algorithm/string/replace.hpp>

/**
 * @brief 获取当前玩家对象
 * @return Player* 如果是游戏内会话返回玩家指针,控制台返回nullptr
 */
Player* ChatHandler::GetPlayer() const { return m_session ? m_session->GetPlayer() : nullptr; }

/**
 * @brief 获取本地化字符串
 *
 * 通过字符串条目ID获取当前会话语言对应的本地化字符串
 *
 * @param entry 字符串条目ID
 * @return char const* 本地化字符串
 */
char const* ChatHandler::GetTrinityString(uint32 entry) const
{
    return m_session->GetTrinityString(entry);
}

/**
 * @brief 检查会话是否具有指定权限
 *
 * @param permission 权限ID
 * @return true 如果会话具有该权限
 */
bool ChatHandler::HasPermission(uint32 permission) const
{
    return m_session->HasPermission(permission);
}

/**
 * @brief 获取当前玩家的名称链接
 * @return std::string 格式化的玩家名称链接
 */
std::string ChatHandler::GetNameLink() const
{
    return GetNameLink(m_session->GetPlayer());
}

/**
 * @brief 检查目标权限是否低于当前用户
 *
 * 用于权限验证,确保低权限用户不能对高权限用户执行操作
 *
 * @param target 目标玩家指针
 * @param guid 目标GUID
 * @param strong 是否严格检查(不允许同级)
 * @return true 如果目标权限更低或相等(非严格模式)
 *
 * @note 性能注意事项:需要查询数据库获取账号ID
 */
bool ChatHandler::HasLowerSecurity(Player* target, ObjectGuid guid, bool strong)
{
    WorldSession* target_session = nullptr;
    uint32 target_account = 0;

    // 获取目标会话或账号ID
    if (target)
        target_session = target->GetSession();
    else if (guid)
        target_account = sCharacterCache->GetCharacterAccountIdByGuid(guid);

    // 如果既没有会话也没有账号,报告错误
    if (!target_session && !target_account)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return true;
    }

    return HasLowerSecurityAccount(target_session, target_account, strong);
}

/**
 * @brief 检查账号权限是否低于当前用户
 *
 * 比较账号权限级别,用于权限验证
 *
 * @param target 目标会话
 * @param target_account 目标账号ID
 * @param strong 是否严格检查
 * @return true 如果目标权限更低
 *
 * @note 调用时机:在需要验证操作权限时调用
 * @note 性能注意事项:控制台和RA控制台拥有最高权限,直接返回false
 */
bool ChatHandler::HasLowerSecurityAccount(WorldSession* target, uint32 target_account, bool strong)
{
    uint32 target_sec;

    // 控制台和RA控制台允许所有操作
    if (!m_session)
        return false;

    // 对于非严格检查,如果配置允许同级别操作且具有检查权限,则跳过
    if (m_session->HasPermission(rbac::RBAC_PERM_CHECK_FOR_LOWER_SECURITY) && !strong && !sWorld->getBoolConfig(CONFIG_GM_LOWER_SECURITY))
        return false;

    // 获取目标安全等级
    if (target)
        target_sec = target->GetSecurity();
    else if (target_account)
        target_sec = AccountMgr::GetSecurity(target_account, realm.Id.Realm);
    else
        return true; // 调用者必须为 (target == nullptr && target_account == 0) 报告错误

    // 比较安全等级
    AccountTypes target_ac_sec = AccountTypes(target_sec);
    if (m_session->GetSecurity() < target_ac_sec || (strong && m_session->GetSecurity() <= target_ac_sec))
    {
        SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
        SetSentErrorMessage(true);
        return true;
    }

    return false;
}

/**
 * @brief 发送系统消息
 *
 * 向当前会话发送系统消息,支持转义特殊字符和多行消息
 *
 * @param str 消息内容
 * @param escapeCharacters 是否转义'|'字符(转义为'||')
 *
 * @note 调用时机:当需要向玩家发送系统消息时
 * @note 性能注意事项:多行消息会构建多个数据包
 */
void ChatHandler::SendSysMessage(std::string_view str, bool escapeCharacters)
{
    std::string msg{ str };

    // 如果需要转义且消息中包含'|',则将每个'|'替换为'||'
    if (escapeCharacters && msg.find('|') != std::string::npos)
    {
        std::vector<std::string_view> tokens = Trinity::Tokenize(msg, '|', true);
        std::ostringstream stream;
        for (size_t i = 0; i < tokens.size() - 1; ++i)
            stream << tokens[i] << "||";
        stream << tokens[tokens.size() - 1];

        msg = stream.str();
    }

    WorldPacket data;
    // 按行分割消息并发送
    for (std::string_view line : Trinity::Tokenize(str, '\n', true))
    {
        BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, line);
        m_session->SendPacket(&data);
    }
}

/**
 * @brief 发送全局系统消息
 *
 * 向服务器上所有玩家发送系统消息
 *
 * @param str 消息内容
 *
 * @note 调用时机:当需要向全体玩家广播消息时(如服务器公告)
 */
void ChatHandler::SendGlobalSysMessage(const char *str)
{
    WorldPacket data;
    // 按行分割消息并发送
    for (std::string_view line : Trinity::Tokenize(str, '\n', true))
    {
        BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, line);
        sWorld->SendGlobalMessage(&data);
    }
}

/**
 * @brief 发送全局GM系统消息
 *
 * 向服务器上所有GM发送系统消息
 *
 * @param str 消息内容
 *
 * @note 调用时机:当需要向GM群体发送消息时
 */
void ChatHandler::SendGlobalGMSysMessage(const char *str)
{
    WorldPacket data;
    // 按行分割消息并发送
    for (std::string_view line : Trinity::Tokenize(str, '\n', true))
    {
        BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, line);
        sWorld->SendGlobalGMMessage(&data);
    }
}

/**
 * @brief 发送系统消息(通过字符串条目ID)
 *
 * @param entry 字符串条目ID
 */
void ChatHandler::SendSysMessage(uint32 entry)
{
    SendSysMessage(GetTrinityString(entry));
}

/**
 * @brief 内部命令解析函数
 *
 * 尝试执行命令,如果失败则根据权限发送错误消息
 *
 * @param text 命令文本(不含前缀符号)
 * @return true 如果命令被处理
 *
 * @note 调用时机:由ParseCommands调用,在验证命令格式后
 */
bool ChatHandler::_ParseCommands(std::string_view text)
{
    // 尝试执行命令
    if (Trinity::ChatCommands::TryExecuteCommand(*this, text))
        return true;

    // 对普通玩家隐藏命令不存在的错误
    if (m_session && !m_session->HasPermission(rbac::RBAC_PERM_COMMANDS_NOTIFY_COMMAND_NOT_FOUND_ERROR))
        return false;

    // 向GM发送命令无效的错误消息
    PSendSysMessage(LANG_CMD_INVALID, STRING_VIEW_FMT_ARG(text));
    SetSentErrorMessage(true);
    return true;
}

/**
 * @brief 解析聊天命令
 *
 * 检查命令格式(.command或!command)并调用内部解析函数
 *
 * @param text 原始命令文本
 * @return true 如果命令被处理
 *
 * @note 调用时机:玩家发送聊天消息时,检测到可能的命令
 * @note 命令格式要求:
 *       - 必须以.或!开头
 *       - 不能是单个.或!
 *       - 不能以..或!!开头
 *       - 不能在分隔符后紧接.
 */
bool ChatHandler::ParseCommands(std::string_view text)
{
    ASSERT(!text.empty());

    // 聊天命令必须是.command或!command格式
    if ((text[0] != '!') && (text[0] != '.'))
        return false;

    // 忽略单独的.和!
    if (text.length() < 2)
        return false;

    // 忽略以多个点开头的消息(可能是省略号)
    if (text[1] == text[0])
        return false;

    // 忽略分隔符后的.(如数字后的小数点)
    if (text[1] == Trinity::Impl::ChatCommands::COMMAND_DELIMITER)
        return false;

    // 去掉前缀符号后调用内部解析
    return _ParseCommands(text.substr(1));
}

/**
 * @brief 构建聊天数据包(使用GUID)
 *
 * 根据不同的聊天类型构建相应的聊天数据包。
 * 数据包格式因聊天类型而异,包含发送者、接收者、消息内容等信息。
 *
 * @param data 输出的数据包
 * @param chatType 聊天类型(如系统消息、私聊、怪物说话等)
 * @param language 语言
 * @param senderGUID 发送者GUID
 * @param receiverGUID 接收者GUID
 * @param message 消息内容
 * @param chatTag 聊天标签(如GM标签)
 * @param senderName 发送者名称
 * @param receiverName 接收者名称
 * @param achievementId 成就ID(用于成就消息)
 * @param gmMessage 是否为GM消息
 * @param channelName 频道名称(用于频道消息)
 * @return size_t 接收者GUID在数据包中的位置,用于后续替换
 *
 * @note 性能注意事项:此函数会分配数据包内存,频繁调用可能影响性能
 * @note 调用时机:发送任何聊天消息时都需要调用此函数构建数据包
 */
size_t ChatHandler::BuildChatPacket(WorldPacket& data, ChatMsg chatType, Language language, ObjectGuid senderGUID, ObjectGuid receiverGUID, std::string_view message, uint8 chatTag,
                                  std::string const& senderName /*= ""*/, std::string const& receiverName /*= ""*/,
                                  uint32 achievementId /*= 0*/, bool gmMessage /*= false*/, std::string const& channelName /*= ""*/)
{
    size_t receiverGUIDPos = 0;
    // 根据是否为GM消息选择不同的消息类型
    data.Initialize(!gmMessage ? SMSG_MESSAGECHAT : SMSG_GM_MESSAGECHAT);
    data << uint8(chatType);
    data << int32(language);
    data << uint64(senderGUID);
    data << uint32(0);  // 某些标志位

    // 根据聊天类型构建不同的数据包结构
    switch (chatType)
    {
        // 怪物相关的聊天类型需要发送者名称
        case CHAT_MSG_MONSTER_SAY:
        case CHAT_MSG_MONSTER_PARTY:
        case CHAT_MSG_MONSTER_YELL:
        case CHAT_MSG_MONSTER_WHISPER:
        case CHAT_MSG_MONSTER_EMOTE:
        case CHAT_MSG_RAID_BOSS_EMOTE:
        case CHAT_MSG_RAID_BOSS_WHISPER:
        case CHAT_MSG_BATTLENET:
            data << uint32(senderName.length() + 1);
            data << senderName;
            receiverGUIDPos = data.wpos();
            data << uint64(receiverGUID);
            // 如果接收者不是玩家或宠物,需要发送接收者名称
            if (receiverGUID && !receiverGUID.IsPlayer() && !receiverGUID.IsPet())
            {
                data << uint32(receiverName.length() + 1);
                data << receiverName;
            }
            break;

        case CHAT_MSG_WHISPER_FOREIGN:
            data << uint32(senderName.length() + 1);
            data << senderName;
            receiverGUIDPos = data.wpos();
            data << uint64(receiverGUID);
            break;

        // 战场系统消息
        case CHAT_MSG_BG_SYSTEM_NEUTRAL:
        case CHAT_MSG_BG_SYSTEM_ALLIANCE:
        case CHAT_MSG_BG_SYSTEM_HORDE:
            receiverGUIDPos = data.wpos();
            data << uint64(receiverGUID);
            if (receiverGUID && !receiverGUID.IsPlayer())
            {
                data << uint32(receiverName.length() + 1);
                data << receiverName;
            }
            break;

        // 成就消息
        case CHAT_MSG_ACHIEVEMENT:
        case CHAT_MSG_GUILD_ACHIEVEMENT:
            receiverGUIDPos = data.wpos();
            data << uint64(receiverGUID);
            break;

        default:
            // GM消息需要发送者名称
            if (gmMessage)
            {
                data << uint32(senderName.length() + 1);
                data << senderName;
            }

            // 频道消息需要频道名称
            if (chatType == CHAT_MSG_CHANNEL)
            {
                ASSERT(channelName.length() > 0);
                data << channelName;
            }

            receiverGUIDPos = data.wpos();
            data << uint64(receiverGUID);
            break;
    }

    // 写入消息内容和聊天标签
    data << uint32(message.length() + 1);
    data << message;
    data << uint8(chatTag);

    // 成就消息需要附加成就ID
    if (chatType == CHAT_MSG_ACHIEVEMENT || chatType == CHAT_MSG_GUILD_ACHIEVEMENT)
        data << uint32(achievementId);

    return receiverGUIDPos;
}

/**
 * @brief 构建聊天数据包(使用世界对象)
 *
 * 自动从世界对象提取信息并构建聊天数据包
 *
 * @param data 输出的数据包
 * @param chatType 聊天类型
 * @param language 语言
 * @param sender 发送者对象
 * @param receiver 接收者对象
 * @param message 消息内容
 * @param achievementId 成就ID
 * @param channelName 频道名称
 * @param locale 语言区域设置
 * @return size_t 接收者GUID在数据包中的位置
 *
 * @note 调用时机:当发送者和接收者是世界对象时使用此函数更方便
 */
size_t ChatHandler::BuildChatPacket(WorldPacket& data, ChatMsg chatType, Language language, WorldObject const* sender, WorldObject const* receiver, std::string_view message,
                                  uint32 achievementId /*= 0*/, std::string const& channelName /*= ""*/, LocaleConstant locale /*= DEFAULT_LOCALE*/)
{
    ObjectGuid senderGUID;
    std::string senderName = "";
    uint8 chatTag = 0;
    bool gmMessage = false;
    ObjectGuid receiverGUID;
    std::string receiverName = "";

    // 从发送者对象提取信息
    if (sender)
    {
        senderGUID = sender->GetGUID();
        senderName = sender->GetNameForLocaleIdx(locale);
        // 如果发送者是玩家,提取聊天标签和GM标志
        if (Player const* playerSender = sender->ToPlayer())
        {
            chatTag = playerSender->GetChatTag();
            gmMessage = playerSender->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM_CHAT);
        }
    }

    // 从接收者对象提取信息
    if (receiver)
    {
        receiverGUID = receiver->GetGUID();
        receiverName = receiver->GetNameForLocaleIdx(locale);
    }

    return BuildChatPacket(data, chatType, language, senderGUID, receiverGUID, message, chatTag, senderName, receiverName, achievementId, gmMessage, channelName);
}

/**
 * @brief 获取选中的玩家
 *
 * 获取当前玩家选中的目标,如果没有选中目标则返回自己
 *
 * @return Player* 选中的玩家或自己
 *
 * @note 调用时机:当需要获取命令操作目标的玩家时
 */
Player* ChatHandler::getSelectedPlayer()
{
    if (!m_session)
        return nullptr;

    ObjectGuid selected = m_session->GetPlayer()->GetTarget();
    if (!selected)
        return m_session->GetPlayer();

    return ObjectAccessor::FindConnectedPlayer(selected);
}

/**
 * @brief 获取选中的单位
 *
 * 获取当前玩家选中的单位,如果没有选中则返回自己
 *
 * @return Unit* 选中的单位或自己
 *
 * @note 调用时机:当需要获取命令操作目标的单位(可以是玩家、NPC、怪物等)时
 */
Unit* ChatHandler::getSelectedUnit()
{
    if (!m_session)
        return nullptr;

    if (Unit* selected = m_session->GetPlayer()->GetSelectedUnit())
        return selected;

    return m_session->GetPlayer();
}

/**
 * @brief 获取选中的对象
 *
 * 获取当前玩家选中的世界对象,如果没有选中则返回附近的GameObject
 *
 * @return WorldObject* 选中的对象
 *
 * @note 调用时机:当需要获取任意选中的世界对象时
 */
WorldObject* ChatHandler::getSelectedObject()
{
    if (!m_session)
        return nullptr;

    ObjectGuid guid = m_session->GetPlayer()->GetTarget();

    if (!guid)
        return GetNearbyGameObject();

    return ObjectAccessor::GetUnit(*m_session->GetPlayer(), guid);
}

/**
 * @brief 获取选中的生物
 *
 * 获取当前玩家选中的生物(包括宠物和载具)
 *
 * @return Creature* 选中的生物
 *
 * @note 调用时机:当需要获取选中的NPC或怪物时
 */
Creature* ChatHandler::getSelectedCreature()
{
    if (!m_session)
        return nullptr;

    return ObjectAccessor::GetCreatureOrPetOrVehicle(*m_session->GetPlayer(), m_session->GetPlayer()->GetTarget());
}

/**
 * @brief 获取选中的玩家或自己
 *
 * 如果选中了玩家则返回选中的玩家,否则返回自己
 * 用于需要玩家参数但允许默认使用自己的命令
 *
 * @return Player* 选中的玩家或自己
 *
 * @note 调用时机:当命令需要一个玩家目标但可以使用自己作为默认值时
 */
Player* ChatHandler::getSelectedPlayerOrSelf()
{
    if (!m_session)
        return nullptr;

    ObjectGuid selected = m_session->GetPlayer()->GetTarget();
    if (!selected)
        return m_session->GetPlayer();

    // 首先尝试获取选中的目标
    Player* targetPlayer = ObjectAccessor::FindConnectedPlayer(selected);
    // 如果目标不是玩家,则返回自己
    if (!targetPlayer)
        targetPlayer = m_session->GetPlayer();

    return targetPlayer;
}

/**
 * @brief 从超链接中提取键值
 *
 * 解析Shift+点击产生的超链接格式: |color|linkType:key|h[name]|h|r
 * 或: |color|linkType:key:something1:...:somethingN|h[name]|h|r
 *
 * @param text 输入文本(会被修改,使用strtok)
 * @param linkType 期望的链接类型(如"Hplayer","Hitem"等)
 * @param something1 输出参数,提取的额外数据
 * @return char* 提取的键值,失败返回nullptr
 *
 * @note 调用时机:当需要从聊天链接中提取数据时(如物品ID、玩家名称等)
 * @note 性能注意事项:会修改输入文本,使用strtok进行分割
 * @warning 此函数使用了strtok,会破坏输入字符串,不能与线程安全版本混用
 */
char* ChatHandler::extractKeyFromLink(char* text, char const* linkType, char** something1)
{
    // 跳过空输入
    if (!text)
        return nullptr;

    // 跳过空白字符
    while (*text == ' '||*text == '\t'||*text == '\b')
        ++text;

    if (!*text)
        return nullptr;

    // 非链接情况,直接作为普通文本处理
    if (text[0] != '|')
        return strtok(text, " ");

    // 解析链接格式: |color|linkType:key|h[name]|h|r
    // 或: |color|linkType:key:something1:...:somethingN|h[name]|h|r

    char* check = strtok(text, "|");                        // 跳过颜色标记
    if (!check)
        return nullptr;                                     // 数据结束

    char* cLinkType = strtok(nullptr, ":");                 // 获取链接类型
    if (!cLinkType)
        return nullptr;                                     // 数据结束

    // 验证链接类型是否匹配
    if (strcmp(cLinkType, linkType) != 0)
    {
        strtok(nullptr, " ");                               // 跳过链接尾部,允许后续继续使用strtok
        SendSysMessage(LANG_WRONG_LINK_TYPE);
        return nullptr;
    }

    char* cKeys = strtok(nullptr, "|");                     // 提取键值和数据
    char* cKeysTail = strtok(nullptr, "");

    char* cKey = strtok(cKeys, ":|");                       // 提取键
    if (something1)
        *something1 = strtok(nullptr, ":|");                // 提取额外数据

    strtok(cKeysTail, "]");                                 // 重启扫描并跳过名称(可能包含空格)
    strtok(nullptr, " ");                                   // 跳过链接尾部
    return cKey;
}

/**
 * @brief 从超链接中提取键值(支持多种链接类型)
 *
 * 支持多种链接类型,返回匹配的类型索引
 *
 * @param text 输入文本
 * @param linkTypes 链接类型数组(以nullptr结尾)
 * @param found_idx 输出参数,找到的链接类型索引
 * @param something1 输出参数,提取的额外数据
 * @return char* 提取的键值
 *
 * @note 调用时机:当命令接受多种类型的链接参数时(如物品或法术)
 */
char* ChatHandler::extractKeyFromLink(char* text, char const* const* linkTypes, int* found_idx, char** something1)
{
    // 跳过空输入
    if (!text)
        return nullptr;

    // 跳过空白字符
    while (*text == ' '||*text == '\t'||*text == '\b')
        ++text;

    if (!*text)
        return nullptr;

    // 非链接情况
    if (text[0] != '|')
        return strtok(text, " ");

    // 支持三种格式:
    // |color|linkType:key|h[name]|h|r
    // |color|linkType:key:something1:...:somethingN|h[name]|h|r
    // |linkType:key|h[name]|h|r (无颜色标记)

    char* tail;

    if (text[1] == 'c')
    {
        char* check = strtok(text, "|");                    // 跳过颜色标记
        if (!check)
            return nullptr;                                 // 数据结束

        tail = strtok(nullptr, "");                         // 获取尾部
    }
    else
        tail = text+1;                                      // 跳过第一个|

    char* cLinkType = strtok(tail, ":");                    // 获取链接类型
    if (!cLinkType)
        return nullptr;                                     // 数据结束

    // 遍历所有支持的链接类型
    for (int i = 0; linkTypes[i]; ++i)
    {
        if (strcmp(cLinkType, linkTypes[i]) == 0)
        {
            char* cKeys = strtok(nullptr, "|");             // 提取键值和数据
            char* cKeysTail = strtok(nullptr, "");

            char* cKey = strtok(cKeys, ":|");               // 提取键
            if (something1)
                *something1 = strtok(nullptr, ":|");        // 提取额外数据

            strtok(cKeysTail, "]");                         // 跳过名称
            strtok(nullptr, " ");                           // 跳过链接尾部
            if (found_idx)
                *found_idx = i;
            return cKey;
        }
    }

    strtok(nullptr, " ");                                   // 跳过链接尾部
    SendSysMessage(LANG_WRONG_LINK_TYPE);
    return nullptr;
}

/**
 * @brief 获取附近的游戏对象
 *
 * 在玩家周围搜索最近的游戏对象
 *
 * @return GameObject* 最近的GameObject,如果找不到返回nullptr
 *
 * @note 性能注意事项:使用网格搜索,搜索范围为SIZE_OF_GRIDS
 */
GameObject* ChatHandler::GetNearbyGameObject()
{
    if (!m_session)
        return nullptr;

    Player* pl = m_session->GetPlayer();
    GameObject* obj = nullptr;
    Trinity::NearestGameObjectCheck check(*pl);
    Trinity::GameObjectLastSearcher<Trinity::NearestGameObjectCheck> searcher(pl, obj, check);
    Cell::VisitGridObjects(pl, searcher, SIZE_OF_GRIDS);
    return obj;
}

/**
 * @brief 通过数据库GUID从玩家地图获取游戏对象
 *
 * 根据数据库中的GUID查找当前地图中的游戏对象
 *
 * @param lowguid 数据库GUID
 * @return GameObject* 找到的游戏对象,如果不存在返回nullptr
 *
 * @note 调用时机:当需要根据数据库ID查找特定游戏对象时
 */
GameObject* ChatHandler::GetObjectFromPlayerMapByDbGuid(ObjectGuid::LowType lowguid)
{
    if (!m_session)
        return nullptr;
    auto bounds = m_session->GetPlayer()->GetMap()->GetGameObjectBySpawnIdStore().equal_range(lowguid);
    if (bounds.first != bounds.second)
        return bounds.first->second;
    return nullptr;
}

/**
 * @brief 通过数据库GUID从玩家地图获取生物
 *
 * 根据数据库中的GUID查找当前地图中的生物。
 * 优先返回存活的生物,如果没有存活的则返回死亡的。
 *
 * @param lowguid 数据库GUID
 * @return Creature* 找到的生物,如果不存在返回nullptr
 *
 * @note 性能注意事项:会遍历所有匹配的生物以找到存活的
 */
Creature* ChatHandler::GetCreatureFromPlayerMapByDbGuid(ObjectGuid::LowType lowguid)
{
    if (!m_session)
        return nullptr;
    // 选择第一个存活的生物,如果没有存活的则返回死亡的
    Creature* creature = nullptr;
    auto bounds = m_session->GetPlayer()->GetMap()->GetCreatureBySpawnIdStore().equal_range(lowguid);
    for (auto it = bounds.first; it != bounds.second; ++it)
    {
        creature = it->second;
        if (it->second->IsAlive())
            break;
    }
    return creature;
}

/**
 * @enum GuidLinkType
 * @brief GUID链接类型枚举
 *
 * 定义支持的GUID链接类型,用于从超链接中提取GUID
 */
enum GuidLinkType
{
    GUID_LINK_PLAYER     = 0,   ///< 玩家链接(必须为第一个,用于非链接情况的选择)
    GUID_LINK_CREATURE   = 1,   ///< 生物链接
    GUID_LINK_GAMEOBJECT = 2    ///< 游戏对象链接
};

/// GUID链接类型对应的字符串数组
static char const* const guidKeys[] =
{
    "Hplayer",      ///< 玩家链接前缀
    "Hcreature",    ///< 生物链接前缀
    "Hgameobject",  ///< 游戏对象链接前缀
    nullptr
};

/**
 * @brief 从链接提取低位GUID
 *
 * 从超链接中提取GUID,支持玩家、生物和游戏对象三种类型
 *
 * @param text 输入文本(会被修改)
 * @param guidHigh 输出参数,GUID的高位类型
 * @return ObjectGuid::LowType 提取的低位GUID,失败返回0
 *
 * @note 调用时机:当需要从超链接中提取对象GUID时
 * @note 对于玩家链接,会尝试在线查找和缓存查找
 */
ObjectGuid::LowType ChatHandler::extractLowGuidFromLink(char* text, HighGuid& guidHigh)
{
    int type = 0;

    // 支持的链接格式:
    // |color|Hcreature:creature_guid|h[name]|h|r
    // |color|Hgameobject:go_guid|h[name]|h|r
    // |color|Hplayer:name|h[name]|h|r
    char* idS = extractKeyFromLink(text, guidKeys, &type);
    if (!idS)
        return 0;

    switch (type)
    {
        case GUID_LINK_PLAYER:
        {
            guidHigh = HighGuid::Player;
            std::string name = idS;
            // 规范化玩家名称
            if (!normalizePlayerName(name))
                return 0;

            // 尝试在线查找玩家
            if (Player* player = ObjectAccessor::FindPlayerByName(name))
                return player->GetGUID().GetCounter();

            // 从缓存查找玩家GUID
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name);
            return guid.GetCounter();

        }
        case GUID_LINK_CREATURE:
        {
            guidHigh = HighGuid::Unit;
            ObjectGuid::LowType lowguid = Trinity::StringTo<ObjectGuid::LowType>(idS).value_or(0);
            return lowguid;
        }
        case GUID_LINK_GAMEOBJECT:
        {
            guidHigh = HighGuid::GameObject;
            ObjectGuid::LowType lowguid = Trinity::StringTo<ObjectGuid::LowType>(idS).value_or(0);
            return lowguid;
        }
    }

    // 未知类型
    return 0;
}

std::string ChatHandler::extractPlayerNameFromLink(char* text)
{
    // |color|Hplayer:name|h[name]|h|r
    char* name_str = extractKeyFromLink(text, "Hplayer");
    if (!name_str)
        return "";

    std::string name = name_str;
    if (!normalizePlayerName(name))
        return "";

    return name;
}

bool ChatHandler::extractPlayerTarget(char* args, Player** player, ObjectGuid* player_guid /*=nullptr*/, std::string* player_name /*= nullptr*/)
{
    if (args && *args)
    {
        std::string name = extractPlayerNameFromLink(args);
        if (name.empty())
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        Player* pl = ObjectAccessor::FindPlayerByName(name);

        // if allowed player pointer
        if (player)
            *player = pl;

        // if need guid value from DB (in name case for check player existence)
        ObjectGuid guid = !pl && (player_guid || player_name) ? sCharacterCache->GetCharacterGuidByName(name) : ObjectGuid::Empty;

        // if allowed player guid (if no then only online players allowed)
        if (player_guid)
            *player_guid = pl ? pl->GetGUID() : guid;

        if (player_name)
            *player_name = pl || guid ? name : "";
    }
    else
    {
        // populate strtok buffer to prevent crashes
        static char dummy[1] = "";
        strtok(dummy, "");

        Player* pl = getSelectedPlayerOrSelf();
        // if allowed player pointer
        if (player)
            *player = pl;
        // if allowed player guid (if no then only online players allowed)
        if (player_guid)
            *player_guid = pl ? pl->GetGUID() : ObjectGuid::Empty;

        if (player_name)
            *player_name = pl ? pl->GetName() : "";
    }

    // some from req. data must be provided (note: name is empty if player does not exist)
    if ((!player || !*player) && (!player_guid || !*player_guid) && (!player_name || player_name->empty()))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

char* ChatHandler::extractQuotedArg(char* args)
{
    if (!args || !*args)
        return nullptr;

    if (*args == '"')
        return strtok(args+1, "\"");
    else
    {
        // skip spaces
        while (*args == ' ')
        {
            args += 1;
            continue;
        }

        // return nullptr if we reached the end of the string
        if (!*args)
            return nullptr;

        // since we skipped all spaces, we expect another token now
        if (*args == '"')
        {
            // return an empty string if there are 2 "" in a row.
            // strtok doesn't handle this case
            if (*(args + 1) == '"')
            {
                strtok(args, " ");
                static char arg[1];
                arg[0] = '\0';
                return arg;
            }
            else
                return strtok(args + 1, "\"");
        }
        else
            return nullptr;
    }
}

bool ChatHandler::needReportToTarget(Player* chr) const
{
    Player* pl = m_session->GetPlayer();
    return pl != chr && pl->IsVisibleGloballyFor(chr);
}

LocaleConstant ChatHandler::GetSessionDbcLocale() const
{
    return m_session->GetSessionDbcLocale();
}

int ChatHandler::GetSessionDbLocaleIndex() const
{
    return m_session->GetSessionDbLocaleIndex();
}

std::string ChatHandler::GetNameLink(Player* chr) const
{
    return playerLink(chr->GetName());
}

char const* CliHandler::GetTrinityString(uint32 entry) const
{
    return sObjectMgr->GetTrinityStringForDBCLocale(entry);
}

void CliHandler::SendSysMessage(std::string_view str, bool /*escapeCharacters*/)
{
    m_print(m_callbackArg, str);
    m_print(m_callbackArg, "\r\n");
}

bool CliHandler::ParseCommands(std::string_view str)
{
    if (str.empty())
        return false;
    // Console allows using commands both with and without leading indicator
    if (str[0] == '.' || str[0] == '!')
        str = str.substr(1);
    return _ParseCommands(str);
}

std::string CliHandler::GetNameLink() const
{
    return GetTrinityString(LANG_CONSOLE_COMMAND);
}

bool CliHandler::needReportToTarget(Player* /*chr*/) const
{
    return true;
}

bool ChatHandler::GetPlayerGroupAndGUIDByName(char const* cname, Player*& player, Group*& group, ObjectGuid& guid, bool offline)
{
    player = nullptr;
    guid.Clear();

    if (cname)
    {
        std::string name = cname;
        if (!name.empty())
        {
            if (!normalizePlayerName(name))
            {
                SendSysMessage(LANG_PLAYER_NOT_FOUND);
                SetSentErrorMessage(true);
                return false;
            }

            player = ObjectAccessor::FindPlayerByName(name);
            if (offline)
                guid = sCharacterCache->GetCharacterGuidByName(name);
        }
    }

    if (player)
    {
        group = player->GetGroup();
        if (!guid || !offline)
            guid = player->GetGUID();
    }
    else
    {
        if (getSelectedPlayer())
            player = getSelectedPlayer();
        else
            player = m_session->GetPlayer();

        if (!guid || !offline)
            guid  = player->GetGUID();
        group = player->GetGroup();
    }

    return true;
}

LocaleConstant CliHandler::GetSessionDbcLocale() const
{
    return sWorld->GetDefaultDbcLocale();
}

int CliHandler::GetSessionDbLocaleIndex() const
{
    return sObjectMgr->GetDBCLocaleIndex();
}

bool AddonChannelCommandHandler::ParseCommands(std::string_view str)
{
    if (str.length() < 17)
        return false;
    if (!StringStartsWith(str, "TrinityCore\t"))
        return false;
    char opcode = str[12];
    echo = &str[13];

    switch (opcode)
    {
        case 'p': // p Ping
            SendAck();
            return true;
        case 'h': // h Issue human-readable command
        case 'i': // i Issue command
        {
            if (!str[17])
                return false;
            humanReadable = (opcode == 'h');
            std::string_view cmd = str.substr(17);
            if (_ParseCommands(cmd)) // actual command starts at str[17]
            {
                if (!hadAck)
                    SendAck();
                if (HasSentErrorMessage())
                    SendFailed();
                else
                    SendOK();
            }
            else
            {
                PSendSysMessage(LANG_CMD_INVALID, STRING_VIEW_FMT_ARG(cmd));
                SendFailed();
            }
            return true;
        }
        default:
            return false;
    }
}

void AddonChannelCommandHandler::Send(std::string const& msg)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, GetSession()->GetPlayer(), GetSession()->GetPlayer(), msg);
    GetSession()->SendPacket(&data);
}

void AddonChannelCommandHandler::SendAck() // a Command acknowledged, no body
{
    ASSERT(echo);
    char ack[18] = "TrinityCore\ta";
    memcpy(ack+13, echo, 4);
    ack[17] = '\0';
    Send(ack);
    hadAck = true;
}

void AddonChannelCommandHandler::SendOK() // o Command OK, no body
{
    ASSERT(echo);
    char ok[18] = "TrinityCore\to";
    memcpy(ok+13, echo, 4);
    ok[17] = '\0';
    Send(ok);
}

void AddonChannelCommandHandler::SendFailed() // f Command failed, no body
{
    ASSERT(echo);
    char fail[18] = "TrinityCore\tf";
    memcpy(fail + 13, echo, 4);
    fail[17] = '\0';
    Send(fail);
}

// m Command message, message in body
void AddonChannelCommandHandler::SendSysMessage(std::string_view str, bool escapeCharacters)
{
    ASSERT(echo);
    if (!hadAck)
        SendAck();

    std::string msg = "TrinityCore\tm";
    msg.append(echo, 4);
    std::string body(str);
    if (escapeCharacters)
        boost::replace_all(body, "|", "||");
    size_t pos, lastpos;
    for (lastpos = 0, pos = body.find('\n', lastpos); pos != std::string::npos; lastpos = pos + 1, pos = body.find('\n', lastpos))
    {
        std::string line(msg);
        line.append(body, lastpos, pos - lastpos);
        Send(line);
    }
    msg.append(body, lastpos, pos - lastpos);
    Send(msg);
}
