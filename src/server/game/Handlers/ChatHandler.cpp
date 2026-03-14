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
 * @file ChatHandler.cpp
 * @brief 聊天消息处理模块
 *
 * 本模块负责处理游戏中所有聊天相关的网络消息，包括：
 * - 各类频道聊天（说、喊、密语、队伍、团队、公会等）
 * - 表情动作
 * - 文字表情
 * - AFK/DND状态
 * - 频道消息处理
 *
 * 主要职责：
 * 1. 验证聊天消息的合法性（语言、权限、等级限制等）
 * 2. 处理聊天命令和GM命令
 * 3. 广播聊天消息到相应的频道或目标
 * 4. 处理聊天过滤和恶意消息防护
 */

#include "WorldSession.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "Common.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "ChatPackets.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "Util.h"
#include "Warden.h"
#include "World.h"
#include "WorldPacket.h"
#include <algorithm>

/**
 * @brief 检查字符是否为恶意字符
 *
 * 检查给定字符是否为不可接受的恶意字符（ASCII控制字符等）。
 * 用于过滤聊天消息中的非法字符，防止客户端利用特殊字符进行作弊或攻击。
 *
 * @param c 要检查的字符
 * @return true 如果是恶意字符
 * @return false 如果是合法字符
 *
 * @note 制表符('\t')被特殊处理，不算作恶意字符
 */
inline bool isNasty(uint8 c)
{
    // 制表符是合法的
    if (c == '\t')
        return false;
    // ASCII控制字符块（0x00-0x1F）被视为恶意字符
    if (c <= '\037') // ASCII control block
        return true;
    return false;
}

/**
 * @brief 处理聊天消息操作码
 *
 * 这是聊天系统的核心处理函数，负责处理所有类型的聊天消息。
 * 主要流程：
 * 1. 解析消息类型和语言
 * 2. 验证消息类型是否有效
 * 3. 验证语言权限（防止使用未学会的语言）
 * 4. 处理插件消息的特殊验证
 * 5. 检查禁言状态和发言冷却时间
 * 6. 解析消息内容和目标
 * 7. 验证消息内容合法性
 * 8. 根据消息类型分发到相应的处理逻辑
 *
 * 支持的消息类型：
 * - CHAT_MSG_SAY: 说话（周围玩家可见）
 * - CHAT_MSG_EMOTE: 表情动作
 * - CHAT_MSG_YELL: 喊话（更大范围可见）
 * - CHAT_MSG_WHISPER: 密语（私聊）
 * - CHAT_MSG_PARTY: 队伍频道
 * - CHAT_MSG_GUILD: 公会频道
 * - CHAT_MSG_OFFICER: 公会官员频道
 * - CHAT_MSG_RAID: 团队频道
 * - CHAT_MSG_RAID_WARNING: 团队警告
 * - CHAT_MSG_BATTLEGROUND: 战场频道
 * - CHAT_MSG_CHANNEL: 自定义频道
 * - CHAT_MSG_AFK: 离开状态
 * - CHAT_MSG_DND: 请勿打扰状态
 *
 * @param recvData 接收到的网络数据包，包含消息类型、语言和消息内容
 *
 * @note 此函数包含大量的安全检查，防止作弊和滥用
 */
void WorldSession::HandleMessagechatOpcode(WorldPacket& recvData)
{
    uint32 type;
    uint32 lang;

    recvData >> type;
    recvData >> lang;

    if (type >= MAX_CHAT_MSG_TYPE)
    {
        TC_LOG_ERROR("network", "CHAT: Wrong message type received: {}", type);
        recvData.rfinish();
        return;
    }

    if (lang == LANG_UNIVERSAL && type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
    {
        TC_LOG_ERROR("entities.player.cheat", "CMSG_MESSAGECHAT: Possible hacking-attempt: {} tried to send a message in universal language", GetPlayerInfo());
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        recvData.rfinish();
        return;
    }

    Player* sender = GetPlayer();

    //TC_LOG_DEBUG("CHAT: packet received. type {}, lang {}", type, lang);

    // prevent talking at unknown language (cheating)
    LanguageDesc const* langDesc = GetLanguageDescByID(lang);
    if (!langDesc)
    {
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        recvData.rfinish();
        return;
    }

    if (langDesc->skill_id != 0 && !sender->HasSkill(langDesc->skill_id))
    {
        // also check SPELL_AURA_COMPREHEND_LANGUAGE (client offers option to speak in that language)
        Unit::AuraEffectList const& langAuras = sender->GetAuraEffectsByType(SPELL_AURA_COMPREHEND_LANGUAGE);
        bool foundAura = false;
        for (Unit::AuraEffectList::const_iterator i = langAuras.begin(); i != langAuras.end(); ++i)
        {
            if ((*i)->GetMiscValue() == int32(lang))
            {
                foundAura = true;
                break;
            }
        }
        if (!foundAura)
        {
            SendNotification(LANG_NOT_LEARNED_LANGUAGE);
            recvData.rfinish();
            return;
        }
    }

    if (lang == LANG_ADDON)
    {
        // LANG_ADDON is only valid for the following message types
        switch (type)
        {
            case CHAT_MSG_PARTY:
            case CHAT_MSG_RAID:
            case CHAT_MSG_GUILD:
            case CHAT_MSG_BATTLEGROUND:
            case CHAT_MSG_WHISPER:
                // check if addon messages are disabled
                if (!sWorld->getBoolConfig(CONFIG_ADDON_CHANNEL))
                {
                    recvData.rfinish();
                    return;
                }
                break;
            default:
                TC_LOG_ERROR("network", "Player {}{} sent a chatmessage with an invalid language/message type combination",
                                                     GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());

                recvData.rfinish();
                return;
        }
    }
    else
    {
        // send in universal language if player in .gm on mode (ignore spell effects)
        if (sender->IsGameMaster())
            lang = LANG_UNIVERSAL;
        else
        {
            Unit::AuraEffectList const& ModLangAuras = sender->GetAuraEffectsByType(SPELL_AURA_MOD_LANGUAGE);
            if (!ModLangAuras.empty())
                lang = ModLangAuras.front()->GetMiscValue();
            else if (HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHAT))
                lang = LANG_UNIVERSAL;
            else
            {
                switch (type)
                {
                    case CHAT_MSG_PARTY:
                    case CHAT_MSG_PARTY_LEADER:
                    case CHAT_MSG_RAID:
                    case CHAT_MSG_RAID_LEADER:
                    case CHAT_MSG_RAID_WARNING:
                        // allow two side chat at group channel if two side group allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
                            lang = LANG_UNIVERSAL;
                        break;
                    case CHAT_MSG_GUILD:
                    case CHAT_MSG_OFFICER:
                        // allow two side chat at guild channel if two side guild allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD))
                            lang = LANG_UNIVERSAL;
                        break;
                }
            }
        }
    }

    if (!CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - GameTime::GetGameTime());
        SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        recvData.rfinish(); // Prevent warnings
        return;
    }

    if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
        sender->UpdateSpeakTime(lang == LANG_ADDON ? Player::ChatFloodThrottle::ADDON : Player::ChatFloodThrottle::REGULAR);

    if (sender->HasAura(1852) && type != CHAT_MSG_WHISPER)
    {
        SendNotification(GetTrinityString(LANG_GM_SILENCE), sender->GetName().c_str());
        recvData.rfinish();
        return;
    }

    std::string to, channel, msg;
    switch (type)
    {
        case CHAT_MSG_SAY:
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_YELL:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        case CHAT_MSG_GUILD:
        case CHAT_MSG_OFFICER:
        case CHAT_MSG_RAID:
        case CHAT_MSG_RAID_LEADER:
        case CHAT_MSG_RAID_WARNING:
        case CHAT_MSG_BATTLEGROUND:
        case CHAT_MSG_BATTLEGROUND_LEADER:
        case CHAT_MSG_AFK:
        case CHAT_MSG_DND:
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
        case CHAT_MSG_WHISPER:
            recvData >> to;
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
        case CHAT_MSG_CHANNEL:
            recvData >> channel;
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
    }

    if (msg.size() > 255)
        return;

    // Our Warden module also uses SendAddonMessage as a way to communicate Lua check results to the server, see if this is that
    if ((type == CHAT_MSG_GUILD) && (lang == LANG_ADDON))
    {
        if (_warden && _warden->ProcessLuaCheckResponse(msg))
            return;
    }

    // no chat commands in AFK/DND autoreply, and it can be empty
    if (!(type == CHAT_MSG_AFK || type == CHAT_MSG_DND))
    {
        if (msg.empty())
            return;
        if (lang == LANG_ADDON)
        {
            if (AddonChannelCommandHandler(this).ParseCommands(msg.c_str()))
                return;
        }
        else
        {
            if (ChatHandler(this).ParseCommands(msg.c_str()))
                return;
        }
    }

    // do message validity checks
    if (lang != LANG_ADDON)
    {
        // cut at the first newline or carriage return
        std::string::size_type pos = msg.find_first_of("\n\r");
        if (pos == 0)
            return;
        else if (pos != std::string::npos)
            msg.erase(pos);

        // abort on any sort of nasty character
        for (uint8 c : msg)
            if (isNasty(c))
            {
                TC_LOG_ERROR("network", "Player {} {} sent a message containing invalid character {} - blocked", GetPlayer()->GetName(),
                    GetPlayer()->GetGUID().ToString(), uint8(c));
                return;
            }

        // collapse multiple spaces into one
        if (sWorld->getBoolConfig(CONFIG_CHAT_FAKE_MESSAGE_PREVENTING))
        {
            auto end = std::unique(msg.begin(), msg.end(), [](char c1, char c2) { return (c1 == ' ') && (c2 == ' '); });
            msg.erase(end, msg.end());
        }

        // validate hyperlinks
        if (!ValidateHyperlinksAndMaybeKick(msg))
            return;
    }

    // 根据消息类型进行不同的处理
    switch (type)
    {
        case CHAT_MSG_SAY:  // 说（周围玩家可见）
        {
            // 防止作弊：死亡玩家不能说话
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ));
                return;
            }

            sender->Say(msg, Language(lang));
            break;
        }
        case CHAT_MSG_EMOTE:  // 表情动作（显示在聊天框中的自定义动作）
        {
            // 防止作弊：死亡玩家不能做表情动作
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_EMOTE_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_EMOTE_LEVEL_REQ));
                return;
            }

            sender->TextEmote(msg);
            break;
        }
        case CHAT_MSG_YELL:  // 喊话（比说更大的范围可见）
        {
            // 防止作弊：死亡玩家不能喊话
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_YELL_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_YELL_LEVEL_REQ));
                return;
            }

            sender->Yell(msg, Language(lang));
            break;
        }
        case CHAT_MSG_WHISPER:  // 密语（私聊）
        {
            if (!normalizePlayerName(to))
            {
                SendPlayerNotFoundNotice(to);
                break;
            }

            Player* receiver = ObjectAccessor::FindConnectedPlayerByName(to);
            if (!receiver || (lang != LANG_ADDON && !receiver->isAcceptWhispers() && receiver->GetSession()->HasPermission(rbac::RBAC_PERM_CAN_FILTER_WHISPERS) && !receiver->IsInWhisperWhiteList(sender->GetGUID())))
            {
                SendPlayerNotFoundNotice(to);
                return;
            }

            // Apply checks only if receiver is not already in whitelist and if receiver is not a GM with ".whisper on"
            if (!receiver->IsInWhisperWhiteList(sender->GetGUID()) && !receiver->IsGameMasterAcceptingWhispers())
            {
                if (!sender->IsGameMaster() && sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ))
                {
                    SendNotification(GetTrinityString(LANG_WHISPER_REQ), sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ));
                    return;
                }

                if (GetPlayer()->GetTeam() != receiver->GetTeam() && !HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHAT))
                {
                    SendWrongFactionNotice();
                    return;
                }
            }

            if (GetPlayer()->HasAura(1852) && !receiver->IsGameMaster())
            {
                SendNotification(GetTrinityString(LANG_GM_SILENCE), GetPlayer()->GetName().c_str());
                return;
            }

            // If player is a Gamemaster and doesn't accept whisper, we auto-whitelist every player that the Gamemaster is talking to
            // We also do that if a player is under the required level for whispers.
            if (receiver->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ) ||
                (HasPermission(rbac::RBAC_PERM_CAN_FILTER_WHISPERS) && !sender->isAcceptWhispers() && !sender->IsInWhisperWhiteList(receiver->GetGUID())))
                sender->AddWhisperWhiteList(receiver->GetGUID());

            GetPlayer()->Whisper(msg, Language(lang), receiver);
            break;
        }
        case CHAT_MSG_PARTY:         // 队伍频道
        case CHAT_MSG_PARTY_LEADER:  // 队长在队伍频道的发言
        {
            // if player is in battleground, he cannot say to battleground members by /p
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = sender->GetGroup();
                if (!group || group->isBGGroup())
                    return;
            }

            // control LEADER messages on the server
            // in a scenario where player has both Group and OriginalGroup,
            // client will incorrectly send LEADER type when sending message to OriginalGroup while being a leader in regular group
            type = group->IsLeader(sender->GetGUID()) ? CHAT_MSG_PARTY_LEADER : CHAT_MSG_PARTY;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, ChatMsg(type), Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false, group->GetMemberGroup(GetPlayer()->GetGUID()));
            break;
        }
        case CHAT_MSG_GUILD:  // 公会频道
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

                    guild->BroadcastToGuild(this, false, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
            break;
        }
        case CHAT_MSG_OFFICER:  // 公会官员频道
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

                    guild->BroadcastToGuild(this, true, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
            break;
        }
        case CHAT_MSG_RAID:  // 团队频道
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup())
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_RAID_LEADER:  // 团长在团队频道的发言
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup() || !group->IsLeader(sender->GetGUID()))
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_LEADER, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_RAID_WARNING:
        {
            Group* group = GetPlayer()->GetGroup();
            if (!group || !(group->isRaidGroup() || sWorld->getBoolConfig(CONFIG_CHAT_PARTY_RAID_WARNINGS)) || !(group->IsLeader(GetPlayer()->GetGUID()) || group->IsAssistant(GetPlayer()->GetGUID())) || group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            //in battleground, raid warning is sent only to players in battleground - code is ok
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_WARNING, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_BATTLEGROUND:
        {
            //battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_BATTLEGROUND_LEADER:
        {
            // battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup() || !group->IsLeader(GetPlayer()->GetGUID()))
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND_LEADER, Language(lang), sender, nullptr, msg);;
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_CHANNEL:
        {
            if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHAT_CHANNEL_REQ))
            {
                if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ))
                {
                    SendNotification(GetTrinityString(LANG_CHANNEL_REQ), sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ));
                    return;
                }
            }

            if (Channel* chn = ChannelMgr::GetChannelForPlayerByNamePart(channel, sender))
            {
                sScriptMgr->OnPlayerChat(sender, type, lang, msg, chn);
                chn->Say(sender->GetGUID(), msg, lang);
            }
            break;
        }
        case CHAT_MSG_AFK:
        {
            if (!sender->IsInCombat())
            {
                if (sender->isAFK())                       // Already AFK
                {
                    if (msg.empty())
                        sender->ToggleAFK();               // Remove AFK
                    else
                        sender->autoReplyMsg = msg;        // Update message
                }
                else                                        // New AFK mode
                {
                    sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_AFK_DEFAULT) : msg;

                    if (sender->isDND())
                        sender->ToggleDND();

                    sender->ToggleAFK();
                }

                sScriptMgr->OnPlayerChat(sender, type, lang, msg);
            }
            break;
        }
        case CHAT_MSG_DND:
        {
            if (sender->isDND())                           // Already DND
            {
                if (msg.empty())
                    sender->ToggleDND();                   // Remove DND
                else
                    sender->autoReplyMsg = msg;            // Update message
            }
            else                                            // New DND mode
            {
                sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_DND_DEFAULT) : msg;

                if (sender->isAFK())
                    sender->ToggleAFK();

                sender->ToggleDND();
            }

            sScriptMgr->OnPlayerChat(sender, type, lang, msg);
            break;
        }
        default:
            TC_LOG_ERROR("network", "CHAT: unknown message type {}, lang: {}", type, lang);
            break;
    }
}

/**
 * @brief 处理表情动作操作码
 *
 * 处理客户端发送的表情动作请求（如挥手等）。
 * 仅支持客户端硬编码的少数表情动作（EMOTE_ONESHOT_NONE和EMOTE_ONESHOT_WAVE）。
 *
 * @param packet 接收到的网络数据包，包含表情动作ID
 *
 * 主要流程：
 * 1. 验证表情动作是否合法（仅允许特定的硬编码表情）
 * 2. 检查玩家是否存活且未处于死亡状态
 * 3. 触发脚本事件
 * 4. 执行表情动作
 *
 * @note 其他表情动作通过HandleTextEmoteOpcode处理
 */
void WorldSession::HandleEmoteOpcode(WorldPackets::Chat::EmoteClient& packet)
{
    Emote emoteId = static_cast<Emote>(packet.EmoteID);

    // restrict to the only emotes hardcoded in client
    if (emoteId != EMOTE_ONESHOT_NONE && emoteId != EMOTE_ONESHOT_WAVE)
        return;

    if (!_player->IsAlive() || _player->HasUnitState(UNIT_STATE_DIED))
        return;

    sScriptMgr->OnPlayerEmote(_player, emoteId);
    _player->HandleEmoteCommand(emoteId);
}

/**
 * @namespace Trinity
 * @brief TrinityCore命名空间，包含表情聊天构建器等辅助类
 */
namespace Trinity
{
    /**
     * @class EmoteChatBuilder
     * @brief 表情聊天数据包构建器
     *
     * 用于构建表情动作相关的聊天数据包。
     * 该类遵循访问者模式，通过operator()方法构建数据包。
     */
    class EmoteChatBuilder
    {
        public:
            EmoteChatBuilder(Player const& player, uint32 text_emote, uint32 emote_num, Unit const* target)
                : i_player(player), i_text_emote(text_emote), i_emote_num(emote_num), i_target(target) { }

            void operator()(WorldPacket& data, LocaleConstant loc_idx)
            {
                std::string const name(i_target ? i_target->GetNameForLocaleIdx(loc_idx) : "");
                uint32 namlen = name.size();

                data.Initialize(SMSG_TEXT_EMOTE, 20 + namlen);
                data << i_player.GetGUID();
                data << uint32(i_text_emote);
                data << uint32(i_emote_num);
                data << uint32(namlen);
                if (namlen > 1)
                    data << name;
                else
                    data << uint8(0x00);
            }

        private:
            Player const& i_player;      ///< 执行表情动作的玩家
            uint32        i_text_emote;  ///< 文字表情ID（如"挥手"、"跳舞"等）
            uint32        i_emote_num;   ///< 表情动作序号
            Unit const*   i_target;      ///< 表情动作的目标单位（可为nullptr）
    };
}                                                           // namespace Trinity

/**
 * @brief 处理文字表情操作码
 *
 * 处理客户端发送的文字表情请求（如/挥手、/跳舞、/坐下等）。
 * 文字表情与简单表情动作不同，会显示在聊天框中并影响角色状态。
 *
 * @param recvData 接收到的网络数据包，包含表情ID、表情序号和目标GUID
 *
 * 主要流程：
 * 1. 验证玩家是否存活和是否有发言权限
 * 2. 解析表情ID、表情序号和目标GUID
 * 3. 触发脚本事件
 * 4. 根据表情类型执行相应动作：
 *    - EMOTE_STATE_SLEEP/SIT/KNEEL: 改变角色姿态状态
 *    - 其他表情: 执行一次性表情动作
 * 5. 向周围玩家广播表情消息
 * 6. 更新成就进度
 * 7. 通知NPC接收表情事件（用于任务交互）
 *
 * @note 某些表情（如坐下、睡觉）会改变角色的持续状态
 */
void WorldSession::HandleTextEmoteOpcode(WorldPacket& recvData)
{
    if (!GetPlayer()->IsAlive())
        return;

    if (!CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - GameTime::GetGameTime());
        SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        return;
    }

    uint32 text_emote, emoteNum;
    ObjectGuid guid;

    recvData >> text_emote;
    recvData >> emoteNum;
    recvData >> guid;

    sScriptMgr->OnPlayerTextEmote(GetPlayer(), text_emote, emoteNum, guid);

    EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(text_emote);
    if (!em)
        return;

    Emote emote = static_cast<Emote>(em->EmoteID);

    switch (emote)
    {
        case EMOTE_STATE_SLEEP:
        case EMOTE_STATE_SIT:
        case EMOTE_STATE_KNEEL:
        case EMOTE_ONESHOT_NONE:
            break;
        default:
            // Only allow text-emotes for "dead" entities (feign death included)
            if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
                break;
            GetPlayer()->HandleEmoteCommand(emote);
            break;
    }

    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);

    CellCoord p = Trinity::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    Cell cell(p);
    cell.SetNoCreate();

    Trinity::EmoteChatBuilder emote_builder(*GetPlayer(), text_emote, emoteNum, unit);
    Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > emote_do(emote_builder);
    Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > > emote_worker(GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE), emote_do);
    TypeContainerVisitor<Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder> >, WorldTypeMapContainer> message(emote_worker);
    cell.Visit(p, message, *GetPlayer()->GetMap(), *GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE));

    GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE, text_emote, 0, unit);

    //Send scripted event call
    if (unit && unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->AI())
        ((Creature*)unit)->AI()->ReceiveEmote(GetPlayer(), text_emote);
}

/**
 * @brief 处理聊天忽略操作码
 *
 * 处理玩家忽略某人密语的请求。
 * 当玩家将某人加入忽略列表后，被忽略者发来的密语会触发此消息，
 * 服务器会向被忽略者发送"对方已忽略你的消息"提示。
 *
 * @param recvData 接收到的网络数据包，包含被忽略玩家的GUID和未知标志
 *
 * 主要流程：
 * 1. 解析被忽略玩家的GUID
 * 2. 查找被忽略玩家是否在线
 * 3. 向被忽略者发送"已被忽略"的提示消息
 */
void WorldSession::HandleChatIgnoredOpcode(WorldPacket& recvData)
{
    ObjectGuid iguid;
    uint8 unk;
    //TC_LOG_DEBUG("network", "WORLD: Received CMSG_CHAT_IGNORED");

    recvData >> iguid;
    recvData >> unk;                                       // probably related to spam reporting

    Player* player = ObjectAccessor::FindConnectedPlayer(iguid);
    if (!player || !player->GetSession())
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_IGNORED, LANG_UNIVERSAL, _player, _player, GetPlayer()->GetName());
    player->SendDirectMessage(&data);
}

/**
 * @brief 处理拒绝频道邀请操作码
 *
 * 处理玩家拒绝频道邀请的请求。
 * 目前仅记录日志，未实现具体功能。
 *
 * @param recvPacket 接收到的网络数据包
 */
void WorldSession::HandleChannelDeclineInvite(WorldPacket &recvPacket)
{
    TC_LOG_DEBUG("network", "Opcode {}", recvPacket.GetOpcode());
}

/**
 * @brief 发送玩家未找到通知
 *
 * 向客户端发送玩家未找到的系统消息。
 * 当密语的对象不存在或不在线时使用。
 *
 * @param name 未找到的玩家名称
 */
void WorldSession::SendPlayerNotFoundNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_NOT_FOUND, name.size()+1);
    data << name;
    SendPacket(&data);
}

/**
 * @brief 发送玩家名称歧义通知
 *
 * 向客户端发送玩家名称歧义的系统消息。
 * 当输入的玩家名称匹配多个在线玩家时使用（部分匹配）。
 *
 * @param name 产生歧义的玩家名称
 */
void WorldSession::SendPlayerAmbiguousNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_AMBIGUOUS, name.size()+1);
    data << name;
    SendPacket(&data);
}

/**
 * @brief 发送阵营错误通知
 *
 * 向客户端发送阵营错误的系统消息。
 * 当玩家尝试与对立阵营玩家进行某些交互（如密语）被禁止时使用。
 */
void WorldSession::SendWrongFactionNotice()
{
    WorldPacket data(SMSG_CHAT_WRONG_FACTION, 0);
    SendPacket(&data);
}

/**
 * @brief 发送聊天受限通知
 *
 * 向客户端发送聊天受限的系统消息。
 * 当玩家的聊天行为受到某些限制时使用（如新手等级限制、试用期限制等）。
 *
 * @param restriction 限制类型枚举值
 */
void WorldSession::SendChatRestrictedNotice(ChatRestrictionType restriction)
{
    WorldPacket data(SMSG_CHAT_RESTRICTED, 1);
    data << uint8(restriction);
    SendPacket(&data);
}
