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

#include "Guild.h"
#include "AccountMgr.h"
#include "Bag.h"
#include "CalendarMgr.h"
#include "CalendarPackets.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SocialMgr.h"
#include "World.h"
#include "WorldSession.h"
#include <boost/iterator/counting_iterator.hpp>

size_t const MAX_GUILD_BANK_TAB_TEXT_LEN = 500;

uint32 const EMBLEM_PRICE = 10 * GOLD;

// only used in logs
char const* GetGuildEventString(GuildEvents event)
{
    switch (event)
    {
        case GE_PROMOTION:
            return "Member promotion";
        case GE_DEMOTION:
            return "Member demotion";
        case GE_MOTD:
            return "Guild MOTD";
        case GE_JOINED:
            return "Member joined";
        case GE_LEFT:
            return "Member left";
        case GE_REMOVED:
            return "Member removed";
        case GE_LEADER_IS:
            return "Leader is";
        case GE_LEADER_CHANGED:
            return "Leader changed";
        case GE_DISBANDED:
            return "Guild disbanded";
        case GE_TABARDCHANGE:
            return "Tabard change";
        case GE_RANK_UPDATED:
            return "Rank updated";
        case GE_RANK_DELETED:
            return "Rank deleted";
        case GE_SIGNED_ON:
            return "Member signed on";
        case GE_SIGNED_OFF:
            return "Member signed off";
        case GE_GUILDBANKBAGSLOTS_CHANGED:
            return "Bank bag slots changed";
        case GE_BANK_TAB_PURCHASED:
            return "Bank tab purchased";
        case GE_BANK_TAB_UPDATED:
            return "Bank tab updated";
        case GE_BANK_MONEY_SET:
            return "Bank money set";
        case GE_BANK_TAB_AND_MONEY_UPDATED:
            return "Bank and money updated";
        case GE_BANK_TEXT_CHANGED:
            return "Bank tab text changed";
        default:
            break;
    }
    return "<None>";
}

inline uint32 GetGuildBankTabPrice(uint8 tabId)
{
    // these prices are in gold units, not copper
    static uint32 const tabPrices[GUILD_BANK_MAX_TABS] = { 100, 250, 500, 1000, 2500, 5000 };
    ASSERT(tabId < GUILD_BANK_MAX_TABS);

    return tabPrices[tabId];
}

void Guild::SendCommandResult(WorldSession* session, GuildCommandType type, GuildCommandError errCode, std::string_view param)
{
    WorldPackets::Guild::GuildCommandResult resultPacket;
    resultPacket.Command = type;
    resultPacket.Result = errCode;
    resultPacket.Name = param;
    session->SendPacket(resultPacket.Write());

    TC_LOG_DEBUG("guild", "SMSG_GUILD_COMMAND_RESULT [{}]: Type: {}, code: {}, param: {}"
         , session->GetPlayerInfo(), type, errCode, resultPacket.Name);
}

void Guild::SendSaveEmblemResult(WorldSession* session, GuildEmblemError errCode)
{
    WorldPackets::Guild::PlayerSaveGuildEmblem saveResponse;
    saveResponse.Error = int32(errCode);
    session->SendPacket(saveResponse.Write());

    TC_LOG_DEBUG("guild", "MSG_SAVE_GUILD_EMBLEM [{}] Code: {}", session->GetPlayerInfo(), errCode);
}

// LogHolder
template <typename Entry>
Guild::LogHolder<Entry>::LogHolder()
    : m_maxRecords(sWorld->getIntConfig(std::is_same_v<Entry, BankEventLogEntry> ? CONFIG_GUILD_BANK_EVENT_LOG_COUNT : CONFIG_GUILD_EVENT_LOG_COUNT)), m_nextGUID(uint32(GUILD_EVENT_LOG_GUID_UNDEFINED))
{ }

template <typename Entry> template <typename... Ts>
void Guild::LogHolder<Entry>::LoadEvent(Ts&&... args)
{
    Entry const& newEntry = m_log.emplace_front(std::forward<Ts>(args)...);
    if (m_nextGUID == uint32(GUILD_EVENT_LOG_GUID_UNDEFINED))
        m_nextGUID = newEntry.GetGUID();
}

template <typename Entry> template <typename... Ts>
void Guild::LogHolder<Entry>::AddEvent(CharacterDatabaseTransaction trans, Ts&&... args)
{
    // Check max records limit
    if (!CanInsert())
        m_log.pop_front();

    // Add event to list
    Entry const& entry = m_log.emplace_back(std::forward<Ts>(args)...);
    // Save to DB
    entry.SaveToDB(trans);
}

template <typename Entry>
inline uint32 Guild::LogHolder<Entry>::GetNextGUID()
{
    // Next guid was not initialized. It means there are no records for this holder in DB yet.
    // Start from the beginning.
    if (m_nextGUID == uint32(GUILD_EVENT_LOG_GUID_UNDEFINED))
        m_nextGUID = 0;
    else
        m_nextGUID = (m_nextGUID + 1) % m_maxRecords;
    return m_nextGUID;
}

Guild::LogEntry::LogEntry(ObjectGuid::LowType guildId, uint32 guid) : m_guildId(guildId), m_guid(guid), m_timestamp(GameTime::GetGameTime()) { }

// EventLogEntry
void Guild::EventLogEntry::SaveToDB(CharacterDatabaseTransaction trans) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_EVENTLOG);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt32(1, m_guid);
    trans->Append(stmt);

    uint8 index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, uint8(m_eventType));
    stmt->setUInt32(++index, m_playerGuid1);
    stmt->setUInt32(++index, m_playerGuid2);
    stmt->setUInt8 (++index, m_newRank);
    stmt->setUInt64(++index, m_timestamp);
    trans->Append(stmt);
}

void Guild::EventLogEntry::WritePacket(WorldPackets::Guild::GuildEventLogQueryResults& packet) const
{
    ObjectGuid playerGUID = ObjectGuid::Create<HighGuid::Player>(m_playerGuid1);
    ObjectGuid otherGUID = ObjectGuid::Create<HighGuid::Player>(m_playerGuid2);

    WorldPackets::Guild::GuildEventEntry eventEntry;
    eventEntry.PlayerGUID = playerGUID;
    eventEntry.OtherGUID = otherGUID;
    eventEntry.TransactionType = uint8(m_eventType);
    eventEntry.TransactionDate = uint32(GameTime::GetGameTime() - m_timestamp);
    eventEntry.RankID = uint8(m_newRank);
    packet.Entry.push_back(eventEntry);
}

// BankEventLogEntry
void Guild::BankEventLogEntry::SaveToDB(CharacterDatabaseTransaction trans) const
{
    uint8 index = 0;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, m_bankTabId);
    trans->Append(stmt);

    index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, m_bankTabId);
    stmt->setUInt8 (++index, uint8(m_eventType));
    stmt->setUInt32(++index, m_playerGuid);
    stmt->setUInt32(++index, m_itemOrMoney);
    stmt->setUInt16(++index, m_itemStackCount);
    stmt->setUInt8 (++index, m_destTabId);
    stmt->setUInt64(++index, m_timestamp);
    trans->Append(stmt);
}

void Guild::BankEventLogEntry::WritePacket(WorldPackets::Guild::GuildBankLogQueryResults& packet) const
{
    WorldPackets::Guild::GuildBankLogEntry bankLogEntry;
    bankLogEntry.PlayerGUID = ObjectGuid::Create<HighGuid::Player>(m_playerGuid);
    bankLogEntry.TimeOffset = int32(GameTime::GetGameTime() - m_timestamp);
    bankLogEntry.EntryType = int8(m_eventType);

    switch (m_eventType)
    {
        case GUILD_BANK_LOG_DEPOSIT_ITEM:
        case GUILD_BANK_LOG_WITHDRAW_ITEM:
            bankLogEntry.ItemID = int32(m_itemOrMoney);
            bankLogEntry.Count = int32(m_itemStackCount);
            break;
        case GUILD_BANK_LOG_MOVE_ITEM:
        case GUILD_BANK_LOG_MOVE_ITEM2:
            bankLogEntry.ItemID = int32(m_itemOrMoney);
            bankLogEntry.Count = int32(m_itemStackCount);
            bankLogEntry.OtherTab = int8(m_destTabId);
            break;
        default:
            bankLogEntry.Money = uint32(m_itemOrMoney);
            break;
    }

    packet.Entry.push_back(bankLogEntry);
}

// RankInfo
void Guild::RankInfo::LoadFromDB(Field* fields)
{
    m_rankId            = fields[1].GetUInt8();
    m_name              = fields[2].GetString();
    m_rights            = fields[3].GetUInt32();
    m_bankMoneyPerDay   = fields[4].GetUInt32();
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        m_rights |= GR_RIGHT_ALL;
}

void Guild::RankInfo::SaveToDB(CharacterDatabaseTransaction trans) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_RANK);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt8 (1, m_rankId);
    stmt->setString(2, m_name);
    stmt->setUInt32(3, m_rights);
    stmt->setUInt32(4, m_bankMoneyPerDay);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::RankInfo::CreateMissingTabsIfNeeded(uint8 tabs, CharacterDatabaseTransaction trans, bool logOnCreate /* = false */)
{
    for (uint8 i = 0; i < tabs; ++i)
    {
        GuildBankRightsAndSlots& rightsAndSlots = m_bankTabRightsAndSlots[i];
        if (rightsAndSlots.GetTabId() == i)
            continue;

        rightsAndSlots.SetTabId(i);
        if (m_rankId == GR_GUILDMASTER)
            rightsAndSlots.SetGuildMasterValues();

        if (logOnCreate)
            TC_LOG_ERROR("guild", "Guild {} has broken Tab {} for rank {}. Created default tab.", m_guildId, i, m_rankId);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_RIGHT);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8(1, i);
        stmt->setUInt8(2, m_rankId);
        stmt->setUInt8(3, rightsAndSlots.GetRights());
        stmt->setUInt32(4, rightsAndSlots.GetSlots());
        trans->Append(stmt);
    }
}

void Guild::RankInfo::SetName(std::string_view name)
{
    if (m_name == name)
        return;

    m_name = name;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_NAME);
    stmt->setString(0, m_name);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetRights(uint32 rights)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        rights = GR_RIGHT_ALL;

    if (m_rights == rights)
        return;

    m_rights = rights;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_RIGHTS);
    stmt->setUInt32(0, m_rights);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetBankMoneyPerDay(uint32 money)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        money = uint32(GUILD_WITHDRAW_MONEY_UNLIMITED);

    if (m_bankMoneyPerDay == money)
        return;

    m_bankMoneyPerDay = money;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_BANK_MONEY);
    stmt->setUInt32(0, money);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetBankTabSlotsAndRights(GuildBankRightsAndSlots rightsAndSlots, bool saveToDB)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        rightsAndSlots.SetGuildMasterValues();

    GuildBankRightsAndSlots& guildBR = m_bankTabRightsAndSlots[rightsAndSlots.GetTabId()];
    guildBR = rightsAndSlots;

    if (saveToDB)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_RIGHT);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, guildBR.GetTabId());
        stmt->setUInt8 (2, m_rankId);
        stmt->setUInt8 (3, guildBR.GetRights());
        stmt->setUInt32(4, guildBR.GetSlots());
        CharacterDatabase.Execute(stmt);
    }
}

// BankTab
Guild::BankTab::BankTab(ObjectGuid::LowType guildId, uint8 tabId) : m_guildId(guildId), m_tabId(tabId)
{ }

void Guild::BankTab::LoadFromDB(Field* fields)
{
    m_name = fields[2].GetString();
    m_icon = fields[3].GetString();
    m_text = fields[4].GetString();
}

bool Guild::BankTab::LoadItemFromDB(Field* fields)
{
    uint8 slotId = fields[13].GetUInt8();
    ObjectGuid::LowType itemGuid = fields[14].GetUInt32();
    uint32 itemEntry = fields[15].GetUInt32();
    if (slotId >= GUILD_BANK_MAX_SLOTS)
    {
        TC_LOG_ERROR("guild", "Invalid slot for item (GUID: {}, id: {}) in guild bank, skipped.", itemGuid, itemEntry);
        return false;
    }

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
    {
        TC_LOG_ERROR("guild", "Unknown item (GUID: {}, id: {}) in guild bank, skipped.", itemGuid, itemEntry);
        return false;
    }

    Item* pItem = NewItemOrBag(proto);
    if (!pItem->LoadFromDB(itemGuid, ObjectGuid::Empty, fields, itemEntry))
    {
        TC_LOG_ERROR("guild", "Item (GUID {}, id: {}) not found in item_instance, deleting from guild bank!", itemGuid, itemEntry);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_NONEXISTENT_GUILD_BANK_ITEM);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, m_tabId);
        stmt->setUInt8 (2, slotId);
        CharacterDatabase.Execute(stmt);

        delete pItem;
        return false;
    }

    pItem->AddToWorld();
    m_items[slotId] = pItem;
    return true;
}

// Deletes contents of the tab from the world (and from DB if necessary)
void Guild::BankTab::Delete(CharacterDatabaseTransaction trans, bool removeItemsFromDB)
{
    for (uint8 slotId = 0; slotId < GUILD_BANK_MAX_SLOTS; ++slotId)
    {
        if (Item* pItem = m_items[slotId])
        {
            pItem->RemoveFromWorld();
            if (removeItemsFromDB)
                pItem->DeleteFromDB(trans);
            delete pItem;
            pItem = nullptr;
        }
    }
}

void Guild::BankTab::SetInfo(std::string_view name, std::string_view icon)
{
    if ((m_name == name) && (m_icon == icon))
        return;

    m_name = name;
    m_icon = icon;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_TAB_INFO);
    stmt->setString(0, m_name);
    stmt->setString(1, m_icon);
    stmt->setUInt32(2, m_guildId);
    stmt->setUInt8 (3, m_tabId);
    CharacterDatabase.Execute(stmt);
}

void Guild::BankTab::SetText(std::string_view text)
{
    if (m_text == text)
        return;

    m_text = text;
    utf8truncate(m_text, MAX_GUILD_BANK_TAB_TEXT_LEN);          // DB and client size limitation

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_TAB_TEXT);
    stmt->setString(0, m_text);
    stmt->setUInt32(1, m_guildId);
    stmt->setUInt8 (2, m_tabId);
    CharacterDatabase.Execute(stmt);
}

// Sets/removes contents of specified slot.
// If pItem == nullptr contents are removed.
bool Guild::BankTab::SetItem(CharacterDatabaseTransaction trans, uint8 slotId, Item* item)
{
    if (slotId >= GUILD_BANK_MAX_SLOTS)
        return false;

    m_items[slotId] = item;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_ITEM);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt8 (1, m_tabId);
    stmt->setUInt8 (2, slotId);
    trans->Append(stmt);

    if (item)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_ITEM);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, m_tabId);
        stmt->setUInt8 (2, slotId);
        stmt->setUInt32(3, item->GetGUID().GetCounter());
        trans->Append(stmt);

        item->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid::Empty);
        item->SetGuidValue(ITEM_FIELD_OWNER, ObjectGuid::Empty);
        item->FSetState(ITEM_NEW);
        item->SaveToDB(trans);                                 // Not in inventory and can be saved standalone
    }

    return true;
}

void Guild::BankTab::SendText(Guild const* guild, WorldSession* session) const
{
    WorldPackets::Guild::GuildBankTextQueryResult textQuery;
    textQuery.Tab = m_tabId;
    textQuery.Text = m_text;

    if (session)
    {
        TC_LOG_DEBUG("guild", "MSG_QUERY_GUILD_BANK_TEXT [{}]: Tabid: {}, Text: {}"
            , session->GetPlayerInfo(), m_tabId, m_text);
        session->SendPacket(textQuery.Write());
    }
    else
    {
        TC_LOG_DEBUG("guild", "MSG_QUERY_GUILD_BANK_TEXT [Broadcast]: Tabid: {}, Text: {}", m_tabId, m_text);
        guild->BroadcastPacket(textQuery.Write());
    }
}

// Member
Guild::Member::Member(ObjectGuid::LowType guildId, ObjectGuid guid, uint8 rankId) :
    m_guildId(guildId),
    m_guid(guid),
    m_zoneId(0),
    m_level(0),
    m_class(0),
    m_gender(0),
    m_flags(GUILDMEMBER_STATUS_NONE),
    m_logoutTime(GameTime::GetGameTime()),
    m_accountId(0),
    m_rankId(rankId)
{}

void Guild::Member::SetStats(Player* player)
{
    m_name      = player->GetName();
    m_level     = player->GetLevel();
    m_class     = player->GetClass();
    m_gender    = player->GetNativeGender();
    m_zoneId    = player->GetZoneId();
    m_accountId = player->GetSession()->GetAccountId();
}

void Guild::Member::SetStats(std::string_view name, uint8 level, uint8 _class, uint8 gender, uint32 zoneId, uint32 accountId)
{
    m_name      = name;
    m_level     = level;
    m_class     = _class;
    m_gender    = gender;
    m_zoneId    = zoneId;
    m_accountId = accountId;
}

void Guild::Member::SetPublicNote(std::string_view publicNote)
{
    if (m_publicNote == publicNote)
        return;

    m_publicNote = publicNote;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_PNOTE);
    stmt->setString(0, m_publicNote);
    stmt->setUInt32(1, m_guid.GetCounter());
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::SetOfficerNote(std::string_view officerNote)
{
    if (m_officerNote == officerNote)
        return;

    m_officerNote = officerNote;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_OFFNOTE);
    stmt->setString(0, m_officerNote);
    stmt->setUInt32(1, m_guid.GetCounter());
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::ChangeRank(CharacterDatabaseTransaction trans, uint8 newRank)
{
    m_rankId = newRank;

    // Update rank information in player's field, if he is online.
    if (Player* player = FindConnectedPlayer())
        player->SetRank(newRank);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_RANK);
    stmt->setUInt8 (0, newRank);
    stmt->setUInt32(1, m_guid.GetCounter());
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::Member::UpdateLogoutTime()
{
    m_logoutTime = GameTime::GetGameTime();
}

void Guild::Member::SaveToDB(CharacterDatabaseTransaction trans) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_MEMBER);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt32(1, m_guid.GetCounter());
    stmt->setUInt8 (2, m_rankId);
    stmt->setString(3, m_publicNote);
    stmt->setString(4, m_officerNote);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

// Loads member's data from database.
// If member has broken fields (level, class) returns false.
// In this case member has to be removed from guild.
bool Guild::Member::LoadFromDB(Field* fields)
{
    m_publicNote  = fields[3].GetString();
    m_officerNote = fields[4].GetString();

    for (uint8 i = 0; i <= GUILD_BANK_MAX_TABS; ++i)
        m_bankWithdraw[i] = fields[5 + i].GetUInt32();

    SetStats(fields[12].GetString(),
             fields[13].GetUInt8(),                         // characters.level
             fields[14].GetUInt8(),                         // characters.class
             fields[15].GetUInt8(),                         // characters.gender
             fields[16].GetUInt16(),                        // characters.zone
             fields[17].GetUInt32());                       // characters.account
    m_logoutTime = fields[18].GetUInt32();                  // characters.logout_time

    if (!CheckStats())
        return false;

    if (!m_zoneId)
    {
        TC_LOG_DEBUG("guild", "{} has broken zone-data", m_guid.ToString());
        m_zoneId = Player::GetZoneIdFromDB(m_guid);
    }

    ResetFlags();
    return true;
}

// Validate player fields. Returns false if corrupted fields are found.
bool Guild::Member::CheckStats() const
{
    if (m_level < 1)
    {
        TC_LOG_ERROR("guild", "{} has a broken data in field `characters`.`level`, deleting him from guild!", m_guid.ToString());
        return false;
    }

    if (m_class < CLASS_WARRIOR || m_class >= MAX_CLASSES)
    {
        TC_LOG_ERROR("guild", "{} has a broken data in field `characters`.`class`, deleting him from guild!", m_guid.ToString());
        return false;
    }
    return true;
}

Player* Guild::Member::FindPlayer() const
{
    return ObjectAccessor::FindPlayer(m_guid);
}

Player* Guild::Member::FindConnectedPlayer() const
{
    return ObjectAccessor::FindConnectedPlayer(m_guid);
}

// Decreases amount of money/slots left for today.
// If (tabId == GUILD_BANK_MAX_TABS) decrease money amount.
// Otherwise decrease remaining items amount for specified tab.
void Guild::Member::UpdateBankWithdrawValue(CharacterDatabaseTransaction trans, uint8 tabId, uint32 amount)
{
    m_bankWithdraw[tabId] += amount;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_MEMBER_WITHDRAW);
    stmt->setUInt32(0, m_guid.GetCounter());
    for (uint8 i = 0; i <= GUILD_BANK_MAX_TABS;)
    {
        uint32 withdraw = m_bankWithdraw[i++];
        stmt->setUInt32(i, withdraw);
    }

    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::Member::ResetValues()
{
    for (uint8 tabId = 0; tabId <= GUILD_BANK_MAX_TABS; ++tabId)
        m_bankWithdraw[tabId] = 0;
}

// Get amount of money/slots left for today.
// If (tabId == GUILD_BANK_MAX_TABS) return money amount.
// Otherwise return remaining items amount for specified tab.
int32 Guild::Member::GetBankWithdrawValue(uint8 tabId) const
{
    // Guild master has unlimited amount.
    if (IsRank(GR_GUILDMASTER))
        return static_cast<int32>(tabId == GUILD_BANK_MAX_TABS ? GUILD_WITHDRAW_MONEY_UNLIMITED : GUILD_WITHDRAW_SLOT_UNLIMITED);

    return m_bankWithdraw[tabId];
}

// EmblemInfo
void EmblemInfo::ReadPacket(WorldPackets::Guild::SaveGuildEmblem& packet)
{
    m_style = packet.EStyle;
    m_color = packet.EColor;
    m_borderStyle = packet.BStyle;
    m_borderColor = packet.BColor;
    m_backgroundColor = packet.Bg;
}

void EmblemInfo::LoadFromDB(Field* fields)
{
    m_style             = fields[3].GetUInt8();
    m_color             = fields[4].GetUInt8();
    m_borderStyle       = fields[5].GetUInt8();
    m_borderColor       = fields[6].GetUInt8();
    m_backgroundColor   = fields[7].GetUInt8();
}

void EmblemInfo::SaveToDB(ObjectGuid::LowType guildId) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_EMBLEM_INFO);
    stmt->setUInt32(0, m_style);
    stmt->setUInt32(1, m_color);
    stmt->setUInt32(2, m_borderStyle);
    stmt->setUInt32(3, m_borderColor);
    stmt->setUInt32(4, m_backgroundColor);
    stmt->setUInt32(5, guildId);
    CharacterDatabase.Execute(stmt);
}

// MoveItemData
Guild::MoveItemData::MoveItemData(Guild* guild, Player* player, uint8 container, uint8 slotId) : m_pGuild(guild), m_pPlayer(player),
m_container(container), m_slotId(slotId), m_pItem(nullptr), m_pClonedItem(nullptr)
{
}

Guild::MoveItemData::~MoveItemData()
{
}

bool Guild::MoveItemData::CheckItem(uint32& splitedAmount)
{
    ASSERT(m_pItem);
    if (splitedAmount > m_pItem->GetCount())
        return false;
    if (splitedAmount == m_pItem->GetCount())
        splitedAmount = 0;
    return true;
}

bool Guild::MoveItemData::CanStore(Item* pItem, bool swap, bool sendError)
{
    m_vec.clear();
    InventoryResult msg = CanStore(pItem, swap);
    if (sendError && msg != EQUIP_ERR_OK)
        m_pPlayer->SendEquipError(msg, pItem);
    return (msg == EQUIP_ERR_OK);
}

bool Guild::MoveItemData::CloneItem(uint32 count)
{
    ASSERT(m_pItem);
    m_pClonedItem = m_pItem->CloneItem(count);
    if (!m_pClonedItem)
    {
        m_pPlayer->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, m_pItem);
        return false;
    }
    return true;
}

void Guild::MoveItemData::LogAction(MoveItemData* pFrom) const
{
    ASSERT(pFrom->GetItem());

    sScriptMgr->OnGuildItemMove(m_pGuild, m_pPlayer, pFrom->GetItem(),
        pFrom->IsBank(), pFrom->GetContainer(), pFrom->GetSlotId(),
        IsBank(), GetContainer(), GetSlotId());
}

inline void Guild::MoveItemData::CopySlots(SlotIds& ids) const
{
    for (auto itr = m_vec.begin(); itr != m_vec.end(); ++itr)
        ids.insert(uint8(itr->pos));
}

// PlayerMoveItemData
bool Guild::PlayerMoveItemData::InitItem()
{
    m_pItem = m_pPlayer->GetItemByPos(m_container, m_slotId);
    if (m_pItem)
    {
        // Anti-WPE protection. Do not move non-empty bags to bank.
        if (m_pItem->IsNotEmptyBag())
        {
            m_pPlayer->SendEquipError(EQUIP_ERR_CAN_ONLY_DO_WITH_EMPTY_BAGS, m_pItem);
            m_pItem = nullptr;
        }
        // Bound items cannot be put into bank.
        else if (!m_pItem->CanBeTraded())
        {
            m_pPlayer->SendEquipError(EQUIP_ERR_ITEMS_CANT_BE_SWAPPED, m_pItem);
            m_pItem = nullptr;
        }
    }
    return (m_pItem != nullptr);
}

void Guild::PlayerMoveItemData::RemoveItem(CharacterDatabaseTransaction trans, MoveItemData* /*pOther*/, uint32 splitedAmount)
{
    if (splitedAmount)
    {
        m_pItem->SetCount(m_pItem->GetCount() - splitedAmount);
        m_pItem->SetState(ITEM_CHANGED, m_pPlayer);
        m_pPlayer->SaveInventoryAndGoldToDB(trans);
    }
    else
    {
        m_pPlayer->MoveItemFromInventory(m_container, m_slotId, true);
        m_pItem->DeleteFromInventoryDB(trans);
        m_pItem = nullptr;
    }
}

Item* Guild::PlayerMoveItemData::StoreItem(CharacterDatabaseTransaction trans, Item* pItem)
{
    ASSERT(pItem);
    m_pPlayer->MoveItemToInventory(m_vec, pItem, true);
    m_pPlayer->SaveInventoryAndGoldToDB(trans);
    return pItem;
}

void Guild::PlayerMoveItemData::LogBankEvent(CharacterDatabaseTransaction trans, MoveItemData* pFrom, uint32 count) const
{
    ASSERT(pFrom);
    // Bank -> Char
    m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_WITHDRAW_ITEM, pFrom->GetContainer(), m_pPlayer->GetGUID().GetCounter(),
        pFrom->GetItem()->GetEntry(), count);
}

inline InventoryResult Guild::PlayerMoveItemData::CanStore(Item* pItem, bool swap)
{
    return m_pPlayer->CanStoreItem(m_container, m_slotId, m_vec, pItem, swap);
}

// BankMoveItemData
bool Guild::BankMoveItemData::InitItem()
{
    m_pItem = m_pGuild->_GetItem(m_container, m_slotId);
    return (m_pItem != nullptr);
}

bool Guild::BankMoveItemData::HasStoreRights(MoveItemData* pOther) const
{
    ASSERT(pOther);
    // Do not check rights if item is being swapped within the same bank tab
    if (pOther->IsBank() && pOther->GetContainer() == m_container)
        return true;
    return m_pGuild->_MemberHasTabRights(m_pPlayer->GetGUID(), m_container, GUILD_BANK_RIGHT_DEPOSIT_ITEM);
}

bool Guild::BankMoveItemData::HasWithdrawRights(MoveItemData* pOther) const
{
    ASSERT(pOther);
    // Do not check rights if item is being swapped within the same bank tab
    if (pOther->IsBank() && pOther->GetContainer() == m_container)
        return true;

    int32 slots = 0;
    if (Member const* member = m_pGuild->GetMember(m_pPlayer->GetGUID()))
        slots = m_pGuild->_GetMemberRemainingSlots(*member, m_container);

    return slots != 0;
}

void Guild::BankMoveItemData::RemoveItem(CharacterDatabaseTransaction trans, MoveItemData* pOther, uint32 splitedAmount)
{
    ASSERT(m_pItem);
    if (splitedAmount)
    {
        m_pItem->SetCount(m_pItem->GetCount() - splitedAmount);
        m_pItem->FSetState(ITEM_CHANGED);
        m_pItem->SaveToDB(trans);
    }
    else
    {
        m_pGuild->_RemoveItem(trans, m_container, m_slotId);
        m_pItem = nullptr;
    }
    // Decrease amount of player's remaining items (if item is moved to different tab or to player)
    if (!pOther->IsBank() || pOther->GetContainer() != m_container)
        m_pGuild->_UpdateMemberWithdrawSlots(trans, m_pPlayer->GetGUID(), m_container);
}

Item* Guild::BankMoveItemData::StoreItem(CharacterDatabaseTransaction trans, Item* pItem)
{
    if (!pItem)
        return nullptr;

    BankTab* pTab = m_pGuild->GetBankTab(m_container);
    if (!pTab)
        return nullptr;

    Item* pLastItem = pItem;
    for (auto itr = m_vec.begin(); itr != m_vec.end(); )
    {
        ItemPosCount pos(*itr);
        ++itr;

        ASSERT(pItem);

        TC_LOG_DEBUG("guild", "GUILD STORAGE: StoreItem tab = {}, slot = {}, item = {}, count = {}",
            m_container, m_slotId, pItem->GetEntry(), pItem->GetCount());
        pLastItem = _StoreItem(trans, pTab, pItem, pos, itr != m_vec.end());
    }
    return pLastItem;
}

void Guild::BankMoveItemData::LogBankEvent(CharacterDatabaseTransaction trans, MoveItemData* pFrom, uint32 count) const
{
    ASSERT(pFrom->GetItem());
    if (pFrom->IsBank())
        // Bank -> Bank
        m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_MOVE_ITEM, pFrom->GetContainer(), m_pPlayer->GetGUID().GetCounter(),
            pFrom->GetItem()->GetEntry(), count, m_container);
    else
        // Char -> Bank
        m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_DEPOSIT_ITEM, m_container, m_pPlayer->GetGUID().GetCounter(),
            pFrom->GetItem()->GetEntry(), count);
}

void Guild::BankMoveItemData::LogAction(MoveItemData* pFrom) const
{
    MoveItemData::LogAction(pFrom);
    if (!pFrom->IsBank() && m_pPlayer->GetSession()->HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE)) /// @todo Move this to scripts
    {
        sLog->OutCommand(m_pPlayer->GetSession()->GetAccountId(),
            "GM {} (Guid: {}) (Account: {}) deposit item: {} (Entry: {} Count: {}) to guild bank named: {} (Guild ID: {})",
            m_pPlayer->GetName(), m_pPlayer->GetGUID().GetCounter(), m_pPlayer->GetSession()->GetAccountId(),
            pFrom->GetItem()->GetTemplate()->Name1, pFrom->GetItem()->GetEntry(), pFrom->GetItem()->GetCount(),
            m_pGuild->GetName(), m_pGuild->GetId());
    }
}

Item* Guild::BankMoveItemData::_StoreItem(CharacterDatabaseTransaction trans, BankTab* pTab, Item* pItem, ItemPosCount& pos, bool clone) const
{
    uint8 slotId = uint8(pos.pos);
    uint32 count = pos.count;
    if (Item* pItemDest = pTab->GetItem(slotId))
    {
        pItemDest->SetCount(pItemDest->GetCount() + count);
        pItemDest->FSetState(ITEM_CHANGED);
        pItemDest->SaveToDB(trans);
        if (!clone)
        {
            pItem->RemoveFromWorld();
            pItem->DeleteFromDB(trans);
            delete pItem;
        }
        return pItemDest;
    }

    if (clone)
        pItem = pItem->CloneItem(count);
    else
        pItem->SetCount(count);

    if (pItem && pTab->SetItem(trans, slotId, pItem))
        return pItem;

    return nullptr;
}

// Tries to reserve space for source item.
// If item in destination slot exists it must be the item of the same entry
// and stack must have enough space to take at least one item.
// Returns false if destination item specified and it cannot be used to reserve space.
bool Guild::BankMoveItemData::_ReserveSpace(uint8 slotId, Item* pItem, Item* pItemDest, uint32& count)
{
    uint32 requiredSpace = pItem->GetMaxStackCount();
    if (pItemDest)
    {
        // Make sure source and destination items match and destination item has space for more stacks.
        if (pItemDest->GetEntry() != pItem->GetEntry() || pItemDest->GetCount() >= pItem->GetMaxStackCount())
            return false;
        requiredSpace -= pItemDest->GetCount();
    }
    // Let's not be greedy, reserve only required space
    requiredSpace = std::min(requiredSpace, count);

    // Reserve space
    ItemPosCount pos(slotId, requiredSpace);
    if (!pos.isContainedIn(m_vec))
    {
        m_vec.push_back(pos);
        count -= requiredSpace;
    }
    return true;
}

void Guild::BankMoveItemData::CanStoreItemInTab(Item* pItem, uint8 skipSlotId, bool merge, uint32& count)
{
    for (uint8 slotId = 0; (slotId < GUILD_BANK_MAX_SLOTS) && (count > 0); ++slotId)
    {
        // Skip slot already processed in CanStore (when destination slot was specified)
        if (slotId == skipSlotId)
            continue;

        Item* pItemDest = m_pGuild->_GetItem(m_container, slotId);
        if (pItemDest == pItem)
            pItemDest = nullptr;

        // If merge skip empty, if not merge skip non-empty
        if ((pItemDest != nullptr) != merge)
            continue;

        _ReserveSpace(slotId, pItem, pItemDest, count);
    }
}

InventoryResult Guild::BankMoveItemData::CanStore(Item* pItem, bool swap)
{
    TC_LOG_DEBUG("guild", "GUILD STORAGE: CanStore() tab = {}, slot = {}, item = {}, count = {}",
        m_container, m_slotId, pItem->GetEntry(), pItem->GetCount());

    uint32 count = pItem->GetCount();
    // Soulbound items cannot be moved
    if (pItem->IsSoulBound())
        return EQUIP_ERR_CANT_DROP_SOULBOUND;

    // Make sure destination bank tab exists
    if (m_container >= m_pGuild->_GetPurchasedTabsSize())
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;

    // Slot explicitely specified. Check it.
    if (m_slotId != NULL_SLOT)
    {
        Item* pItemDest = m_pGuild->_GetItem(m_container, m_slotId);
        // Ignore swapped item (this slot will be empty after move)
        if ((pItemDest == pItem) || swap)
            pItemDest = nullptr;

        if (!_ReserveSpace(m_slotId, pItem, pItemDest, count))
            return EQUIP_ERR_ITEM_CANT_STACK;

        if (count == 0)
            return EQUIP_ERR_OK;
    }

    // Slot was not specified or it has not enough space for all the items in stack
    // Search for stacks to merge with
    if (pItem->GetMaxStackCount() > 1)
    {
        CanStoreItemInTab(pItem, m_slotId, true, count);
        if (count == 0)
            return EQUIP_ERR_OK;
    }

    // Search free slot for item
    CanStoreItemInTab(pItem, m_slotId, false, count);
    if (count == 0)
        return EQUIP_ERR_OK;

    return EQUIP_ERR_BANK_FULL;
}

// Guild
Guild::Guild():
    m_id(0),
    m_leaderGuid(),
    m_createdDate(0),
    m_accountsNumber(0),
    m_bankMoney(0)
{
}

Guild::~Guild()
{
    CharacterDatabaseTransaction temp(nullptr);
    _DeleteBankItems(temp);
}

/**
 * @brief 创建新公会并保存到数据库
 *
 * 职责：
 *   创建一个新的公会，初始化公会数据，设置默认公会等级，将会长添加为成员，
 *   并将所有数据持久化到数据库。
 *
 * @param pLeader 公会会长玩家指针
 * @param name 公会名称
 *
 * @return true 创建成功
 * @return false 创建失败（公会名已存在或会话无效）
 *
 * 主要流程：
 *   1. 检查公会名是否已存在
 *   2. 验证会长的会话有效性
 *   3. 生成公会ID并初始化公会基本属性
 *   4. 将公会信息插入数据库
 *   5. 创建默认公会等级
 *   6. 将会长添加为公会成员
 *   7. 触发公会创建脚本事件
 */
bool Guild::Create(Player* pLeader, std::string_view name)
{
    // 检查是否已存在同名公会
    if (sGuildMgr->GetGuildByName(name))
        return false;

    // 验证会长的会话有效性
    WorldSession* pLeaderSession = pLeader->GetSession();
    if (!pLeaderSession)
        return false;

    // 初始化公会基本属性
    m_id = sGuildMgr->GenerateGuildId();           // 生成唯一公会ID
    m_leaderGuid = pLeader->GetGUID();             // 设置会长GUID
    m_name = name;                                  // 设置公会名称
    m_info = "";                                    // 公会信息（初始为空）
    m_motd = "No message set.";                    // 每日消息（默认消息）
    m_bankMoney = 0;                                // 公会银行金币（初始为0）
    m_createdDate = GameTime::GetGameTime();       // 创建时间

    TC_LOG_DEBUG("guild", "GUILD: creating guild [{}] for leader {} {}",
        m_name, pLeader->GetName(), m_leaderGuid.ToString());

    // 开启数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 删除可能存在的旧公会成员记录（清理脏数据）
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_MEMBERS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 插入新的公会记录到数据库
    uint8 index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD);
    stmt->setUInt32(  index, m_id);
    stmt->setString(++index, m_name);
    stmt->setUInt32(++index, m_leaderGuid.GetCounter());
    stmt->setString(++index, m_info);
    stmt->setString(++index, m_motd);
    stmt->setUInt64(++index, uint32(m_createdDate));
    stmt->setUInt32(++index, m_emblemInfo.GetStyle());
    stmt->setUInt32(++index, m_emblemInfo.GetColor());
    stmt->setUInt32(++index, m_emblemInfo.GetBorderStyle());
    stmt->setUInt32(++index, m_emblemInfo.GetBorderColor());
    stmt->setUInt32(++index, m_emblemInfo.GetBackgroundColor());
    stmt->setUInt64(++index, m_bankMoney);
    trans->Append(stmt);

    // 创建默认公会等级
    _CreateDefaultGuildRanks(trans, pLeaderSession->GetSessionDbLocaleIndex());

    // 将会长添加为公会成员（公会会长等级）
    bool ret = AddMember(trans, m_leaderGuid, GR_GUILDMASTER);

    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);

    // 触发公会创建脚本事件
    if (ret)
        sScriptMgr->OnGuildCreate(this, pLeader, m_name);

    return ret;
}

/**
 * @brief 解散公会并删除所有相关数据
 *
 * 职责：
 *   解散当前公会，移除所有成员，清理公会银行物品，
 *   并从数据库中删除所有公会相关数据。
 *
 * 主要流程：
 *   1. 触发公会解散脚本事件
 *   2. 广播公会解散事件通知所有成员
 *   3. 移除所有公会成员
 *   4. 删除公会基本信息
 *   5. 删除公会等级信息
 *   6. 删除公会银行标签页
 *   7. 删除公会银行物品并释放内存
 *   8. 删除公会银行权限设置
 *   9. 删除公会银行事件日志
 *   10. 删除公会事件日志
 *   11. 从公会管理器中移除公会
 */
void Guild::Disband()
{
    // 在公会数据从数据库删除前调用脚本事件
    sScriptMgr->OnGuildDisband(this);

    // 广播公会解散事件通知所有在线成员
    _BroadcastEvent(GE_DISBANDED, ObjectGuid::Empty);

    // 开启数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 移除所有公会成员
    while (!m_members.empty())
    {
        auto itr = m_members.begin();
        DeleteMember(trans, itr->second.GetGUID(), true);
    }

    // 删除公会基本信息
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会等级信息
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_RANKS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会银行标签页
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_TABS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 释放公会银行标签页内存并删除存储的物品
    _DeleteBankItems(trans, true);

    // 删除公会银行物品记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_ITEMS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会银行权限设置
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会银行事件日志
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_EVENTLOGS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会事件日志
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_EVENTLOGS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);

    // 从公会管理器中移除公会
    sGuildMgr->RemoveGuild(m_id);
}

void Guild::UpdateMemberData(Player* player, uint8 dataid, uint32 value)
{
    if (Member* member = GetMember(player->GetGUID()))
    {
        switch (dataid)
        {
            case GUILD_MEMBER_DATA_ZONEID:
                member->SetZoneID(value);
                break;
            case GUILD_MEMBER_DATA_LEVEL:
                member->SetLevel(value);
                break;
            default:
                TC_LOG_ERROR("guild", "Guild::UpdateMemberData: Called with incorrect DATAID {} (value {})", dataid, value);
                return;
        }
        //HandleRoster();
    }
}

void Guild::OnPlayerStatusChange(Player* player, uint32 flag, bool state)
{
    if (Member* member = GetMember(player->GetGUID()))
    {
        if (state)
            member->AddFlag(flag);
        else member->RemFlag(flag);
    }
}

/**
 * @brief 设置公会名称
 *
 * 职责：
 *   验证并更新公会名称。
 *
 * @param name 新的公会名称
 *
 * @return true 设置成功
 * @return false 设置失败（名称无效或已被使用）
 *
 * 验证条件：
 *   - 不能与当前名称相同
 *   - 不能为空
 *   - 长度不能超过24个字符
 *   - 不能是保留名称
 *   - 必须是有效的公会名称格式
 */
bool Guild::SetName(std::string_view name)
{
    // 验证名称有效性
    if (m_name == name || name.empty() || name.length() > 24 || sObjectMgr->IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        return false;

    // 更新公会名称
    m_name = name;

    // 更新数据库中的公会名称
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_NAME);
    stmt->setString(0, m_name);
    stmt->setUInt32(1, GetId());
    CharacterDatabase.Execute(stmt);

    return true;
}

/**
 * @brief 处理公会名册查询请求
 *
 * 职责：
 *   构建并发送公会名册数据包，包含所有成员和等级信息。
 *
 * @param session 请求玩家的会话
 *
 * 发送的数据包括：
 *   - 公会等级信息（权限、银行权限等）
 *   - 所有成员信息（GUID、等级、区域、等级、职业等）
 *   - 公会消息和公会信息
 */
void Guild::HandleRoster(WorldSession* session)
{
    WorldPackets::Guild::GuildRoster roster;

    // 构建等级数据
    roster.RankData.reserve(m_ranks.size());
    for (RankInfo const& rank : m_ranks)
    {
        WorldPackets::Guild::GuildRankData& rankData = roster.RankData.emplace_back();

        rankData.Flags = rank.GetRights();
        rankData.WithdrawGoldLimit = rank.GetBankMoneyPerDay();
        for (uint8 i = 0; i < GUILD_BANK_MAX_TABS; ++i)
        {
            rankData.TabFlags[i] = rank.GetBankTabRights(i);
            rankData.TabWithdrawItemLimit[i] = rank.GetBankTabSlotsPerDay(i);
        }
    }

    // 检查是否有查看官员备注的权限
    bool sendOfficerNote = _HasRankRight(session->GetPlayer(), GR_RIGHT_VIEWOFFNOTE);

    // 构建成员数据
    roster.MemberData.reserve(m_members.size());
    for (auto const& [guid, member] : m_members)
    {
        WorldPackets::Guild::GuildRosterMemberData& memberData = roster.MemberData.emplace_back();

        memberData.Guid = member.GetGUID();
        memberData.RankID = int32(member.GetRankId());
        memberData.AreaID = int32(member.GetZoneId());
        memberData.LastSave = float(float(GameTime::GetGameTime() - member.GetLogoutTime()) / float(DAY));

        memberData.Status = member.GetFlags();
        memberData.Level = member.GetLevel();
        memberData.ClassID = member.GetClass();
        memberData.Gender = member.GetGender();

        memberData.Name = member.GetName();
        memberData.Note = member.GetPublicNote();
        if (sendOfficerNote)
            memberData.OfficerNote = member.GetOfficerNote();
    }

    // 设置公会消息和信息
    roster.WelcomeText = m_motd;
    roster.InfoText = m_info;

    TC_LOG_DEBUG("guild", "SMSG_GUILD_ROSTER [{}]", session->GetPlayerInfo());
    session->SendPacket(roster.Write());
}

/**
 * @brief 处理公会信息查询请求
 *
 * 职责：
 *   发送公会基本信息，包括公会名称、徽章样式和等级名称。
 *
 * @param session 请求玩家的会话
 */
void Guild::HandleQuery(WorldSession* session)
{
    WorldPackets::Guild::QueryGuildInfoResponse response;
    response.GuildId = m_id;

    // 设置公会徽章信息
    response.Info.EmblemStyle = m_emblemInfo.GetStyle();
    response.Info.EmblemColor = m_emblemInfo.GetColor();
    response.Info.BorderStyle = m_emblemInfo.GetBorderStyle();
    response.Info.BorderColor = m_emblemInfo.GetBorderColor();
    response.Info.BackgroundColor = m_emblemInfo.GetBackgroundColor();

    // 设置等级名称
    for (uint8 i = 0; i < _GetRanksSize(); ++i)
        response.Info.Ranks[i] = m_ranks[i].GetName();

    response.Info.RankCount = _GetRanksSize();
    response.Info.GuildName = m_name;

    session->SendPacket(response.Write());
    TC_LOG_DEBUG("guild", "SMSG_GUILD_QUERY_RESPONSE [{}]", session->GetPlayerInfo());
}

/**
 * @brief 处理设置每日消息请求
 *
 * 职责：
 *   更新公会的每日消息（MOTD）。
 *
 * @param session 发起请求的玩家会话
 * @param motd 新的每日消息内容
 *
 * 主要流程：
 *   1. 检查是否与当前消息相同
 *   2. 验证玩家是否有设置MOTD的权限
 *   3. 更新MOTD并保存到数据库
 *   4. 触发脚本事件
 *   5. 广播MOTD更新通知
 */
void Guild::HandleSetMOTD(WorldSession* session, std::string_view motd)
{
    // 如果与当前消息相同，直接返回
    if (m_motd == motd)
        return;

    // 玩家必须拥有设置MOTD的权限
    if (!_HasRankRight(session->GetPlayer(), GR_RIGHT_SETMOTD))
    {
        SendCommandResult(session, GUILD_COMMAND_EDIT_MOTD, ERR_GUILD_PERMISSIONS);
    }
    else
    {
        // 更新MOTD
        m_motd = motd;

        // 触发脚本事件
        sScriptMgr->OnGuildMOTDChanged(this, m_motd);

        // 更新数据库
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MOTD);
        stmt->setString(0, m_motd);
        stmt->setUInt32(1, m_id);
        CharacterDatabase.Execute(stmt);

        // 广播MOTD更新通知
        _BroadcastEvent(GE_MOTD, ObjectGuid::Empty, m_motd);
    }
}

/**
 * @brief 处理设置公会信息请求
 *
 * 职责：
 *   更新公会的信息描述。
 *
 * @param session 发起请求的玩家会话
 * @param info 新的公会信息描述
 *
 * 主要流程：
 *   1. 检查是否与当前信息相同
 *   2. 验证玩家是否有修改公会信息的权限
 *   3. 更新信息并保存到数据库
 *   4. 触发脚本事件
 */
void Guild::HandleSetInfo(WorldSession* session, std::string_view info)
{
    // 如果与当前信息相同，直接返回
    if (m_info == info)
        return;

    // 玩家必须拥有修改公会信息的权限
    if (_HasRankRight(session->GetPlayer(), GR_RIGHT_MODIFY_GUILD_INFO))
    {
        // 更新公会信息
        m_info = info;

        // 触发脚本事件
        sScriptMgr->OnGuildInfoChanged(this, m_info);

        // 更新数据库
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_INFO);
        stmt->setString(0, m_info);
        stmt->setUInt32(1, m_id);
        CharacterDatabase.Execute(stmt);
    }
}

void Guild::HandleSetEmblem(WorldSession* session, EmblemInfo const& emblemInfo)
{
    Player* player = session->GetPlayer();
    if (!_IsLeader(player))
        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_NOTGUILDMASTER); // "Only guild leaders can create emblems."
    else if (!player->HasEnoughMoney(EMBLEM_PRICE))
        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_NOTENOUGHMONEY); // "You can't afford to do that."
    else
    {
        player->ModifyMoney(-int32(EMBLEM_PRICE));

        m_emblemInfo = emblemInfo;
        m_emblemInfo.SaveToDB(m_id);

        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_SUCCESS); // "Guild Emblem saved."

        HandleQuery(session);
    }
}

void Guild::HandleSetLeader(WorldSession* session, std::string_view name)
{
    Player* player = session->GetPlayer();
    // Only leader can assign new leader
    if (!_IsLeader(player))
        SendCommandResult(session, GUILD_COMMAND_CHANGE_LEADER, ERR_GUILD_PERMISSIONS);
    // Old leader must be a member of guild
    else if (Member* pOldLeader = GetMember(player->GetGUID()))
    {
        // New leader must be a member of guild
        if (Member* pNewLeader = GetMember(name))
        {
            _SetLeaderGUID(*pNewLeader);

            CharacterDatabaseTransaction trans(nullptr);
            pOldLeader->ChangeRank(trans, GR_OFFICER);
            _BroadcastEvent(GE_LEADER_CHANGED, ObjectGuid::Empty, player->GetName(), pNewLeader->GetName());
        }
    }
}

void Guild::HandleSetBankTabInfo(WorldSession* session, uint8 tabId, std::string_view name, std::string_view icon)
{
    BankTab* tab = GetBankTab(tabId);
    if (!tab)
    {
        TC_LOG_ERROR("guild", "Guild::HandleSetBankTabInfo: Player {} trying to change bank tab info from unexisting tab {}.",
                       session->GetPlayerInfo(), tabId);
        return;
    }

    tab->SetInfo(name, icon);
    _BroadcastEvent(GE_BANK_TAB_UPDATED, ObjectGuid::Empty, std::to_string(tabId), tab->GetName(), tab->GetIcon());
}

void Guild::HandleSetMemberNote(WorldSession* session, std::string_view name, std::string_view note, bool officer)
{
    // Player must have rights to set public/officer note
    if (!_HasRankRight(session->GetPlayer(), officer ? GR_RIGHT_EOFFNOTE : GR_RIGHT_EPNOTE))
        SendCommandResult(session, GUILD_COMMAND_PUBLIC_NOTE, ERR_GUILD_PERMISSIONS);
    else if (Member* member = GetMember(name))
    {
        if (officer)
            member->SetOfficerNote(note);
        else
            member->SetPublicNote(note);

        HandleRoster(session);
    }
}

void Guild::HandleSetRankInfo(WorldSession* session, uint8 rankId, std::string_view name, uint32 rights, uint32 moneyPerDay, std::array<GuildBankRightsAndSlots, GUILD_BANK_MAX_TABS> const& rightsAndSlots)
{
    // Only leader can modify ranks
    if (!_IsLeader(session->GetPlayer()))
        SendCommandResult(session, GUILD_COMMAND_CHANGE_RANK, ERR_GUILD_PERMISSIONS);
    else if (RankInfo* rankInfo = GetRankInfo(rankId))
    {
        rankInfo->SetName(name);
        rankInfo->SetRights(rights);
        _SetRankBankMoneyPerDay(rankId, moneyPerDay);

        for (auto itr = rightsAndSlots.begin(); itr != rightsAndSlots.end(); ++itr)
            _SetRankBankTabRightsAndSlots(rankId, *itr);

        _BroadcastEvent(GE_RANK_UPDATED, ObjectGuid::Empty, std::to_string(rankId), rankInfo->GetName(), std::to_string(m_ranks.size()));

        TC_LOG_DEBUG("guild", "Changed RankName to '{}', rights to 0x{:08X}", rankInfo->GetName(), rights);
    }
}

void Guild::HandleBuyBankTab(WorldSession* session, uint8 tabId)
{
    Player* player = session->GetPlayer();
    if (!player)
        return;

    Member const* member = GetMember(player->GetGUID());
    if (!member)
        return;

    if (_GetPurchasedTabsSize() >= GUILD_BANK_MAX_TABS)
        return;

    if (tabId != _GetPurchasedTabsSize())
        return;

    if (tabId >= GUILD_BANK_MAX_TABS)
        return;

    uint32 tabCost = GetGuildBankTabPrice(tabId) * GOLD;
    if (!player->HasEnoughMoney(tabCost))                   // Should not happen, this is checked by client
        return;

    player->ModifyMoney(-int32(tabCost));

    _CreateNewBankTab();
    _BroadcastEvent(GE_BANK_TAB_PURCHASED, ObjectGuid::Empty);
    SendPermissions(session); /// Hack to force client to update permissions
}

/**
 * @brief 处理邀请玩家加入公会的请求
 *
 * 职责：
 *   验证邀请者和被邀请者的资格，检查各种限制条件，
 *   并向被邀请者发送公会邀请消息。
 *
 * @param session 发起邀请的玩家会话
 * @param name 被邀请玩家的名称
 *
 * 主要流程：
 *   1. 查找被邀请玩家（必须在线）
 *   2. 检查被邀请者是否屏蔽了邀请者
 *   3. 检查阵营限制（是否允许跨阵营邀请）
 *   4. 检查被邀请者是否已在公会中
 *   5. 检查被邀请者是否已被其他公会邀请
 *   6. 检查邀请者是否有邀请权限
 *   7. 设置被邀请者的公会邀请ID
 *   8. 记录邀请事件日志
 *   9. 发送公会邀请包给被邀请者
 */
void Guild::HandleInviteMember(WorldSession* session, std::string_view name)
{
    // 查找被邀请玩家（必须在线）
    Player* pInvitee = ObjectAccessor::FindPlayerByName(name);
    if (!pInvitee)
    {
        SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_GUILD_PLAYER_NOT_FOUND_S, name);
        return;
    }

    Player* player = session->GetPlayer();

    // 不向屏蔽了邀请者的玩家发送邀请
    if (pInvitee->GetSocial()->HasIgnore(player->GetGUID()))
        return;

    // 检查阵营限制（如果配置不允许跨阵营交互）
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && pInvitee->GetTeam() != player->GetTeam())
    {
        SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_GUILD_NOT_ALLIED, name);
        return;
    }

    // 被邀请玩家不能已在其他公会
    if (pInvitee->GetGuildId())
    {
        SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_ALREADY_IN_GUILD_S, name);
        return;
    }

    // 被邀请玩家不能已被其他公会邀请
    if (pInvitee->GetGuildIdInvited())
    {
        SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_ALREADY_INVITED_TO_GUILD_S, name);
        return;
    }

    // 邀请者必须拥有邀请权限
    if (!_HasRankRight(player, GR_RIGHT_INVITE))
    {
        SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_GUILD_PERMISSIONS);
        return;
    }

    // 发送邀请成功结果给邀请者
    SendCommandResult(session, GUILD_COMMAND_INVITE, ERR_GUILD_COMMAND_SUCCESS, name);

    TC_LOG_DEBUG("guild", "Player {} invited {} to join his Guild", player->GetName(), pInvitee->GetName());

    // 设置被邀请者的公会邀请ID
    pInvitee->SetGuildIdInvited(m_id);

    // 记录邀请事件日志
    _LogEvent(GUILD_EVENT_LOG_INVITE_PLAYER, player->GetGUID().GetCounter(), pInvitee->GetGUID().GetCounter());

    // 构建并发送公会邀请包给被邀请者
    WorldPackets::Guild::GuildInvite invite;
    invite.InviterName = player->GetName();
    invite.GuildName = GetName();
    pInvitee->SendDirectMessage(invite.Write());

    TC_LOG_DEBUG("guild", "SMSG_GUILD_INVITE [{}]", pInvitee->GetName());
}

/**
 * @brief 处理玩家接受公会邀请
 *
 * 职责：
 *   验证玩家阵营后，将玩家添加到公会中。
 *
 * @param session 接受邀请的玩家会话
 *
 * 主要流程：
 *   1. 检查阵营限制（是否允许跨阵营加入）
 *   2. 将玩家添加为公会成员
 */
void Guild::HandleAcceptMember(WorldSession* session)
{
    Player* player = session->GetPlayer();

    // 检查阵营限制（如果配置不允许跨阵营交互）
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) &&
        player->GetTeam() != sCharacterCache->GetCharacterTeamByGuid(GetLeaderGUID()))
        return;

    // 将玩家添加到公会（使用默认最低等级）
    CharacterDatabaseTransaction trans(nullptr);
    AddMember(trans, player->GetGUID());
}

/**
 * @brief 处理玩家离开公会
 *
 * 职责：
 *   处理玩家主动退出公会的请求，包括会长退出的特殊情况。
 *
 * @param session 离开公会的玩家会话
 *
 * 主要流程：
 *   1. 移除玩家的公会日历事件和报名
 *   2. 如果是会长离开：
 *      a. 如果还有其他成员，拒绝会长离开
 *      b. 如果是最后一名成员，解散公会
 *   3. 如果是普通成员离开：
 *      a. 从公会中删除成员
 *      b. 记录离开事件日志
 *      c. 广播离开事件通知其他成员
 */
void Guild::HandleLeaveMember(WorldSession* session)
{
    Player* player = session->GetPlayer();

    // 移除玩家的公会日历事件和报名
    sCalendarMgr->RemovePlayerGuildEventsAndSignups(player->GetGUID(), GetId());

    // 处理会长离开的特殊情况
    if (_IsLeader(player))
    {
        if (m_members.size() > 1)
        {
            // 如果还有其他成员，会长不能离开
            SendCommandResult(session, GUILD_COMMAND_QUIT, ERR_GUILD_LEADER_LEAVE);
        }
        else
        {
            // 如果是最后一名成员，会长离开时解散公会
            Disband();
        }
    }
    else
    {
        // 普通成员离开：从公会中删除成员
        CharacterDatabaseTransaction trans(nullptr);
        DeleteMember(trans, player->GetGUID(), false, false);

        // 记录离开公会事件日志
        _LogEvent(GUILD_EVENT_LOG_LEAVE_GUILD, player->GetGUID().GetCounter());

        // 广播成员离开事件通知其他成员
        _BroadcastEvent(GE_LEFT, player->GetGUID(), player->GetName());

        // 发送成功结果给玩家
        SendCommandResult(session, GUILD_COMMAND_QUIT, ERR_GUILD_COMMAND_SUCCESS, m_name);
    }
}

/**
 * @brief 处理踢出公会成员
 *
 * 职责：
 *   处理将公会成员从公会中踢出的请求，验证权限和等级限制。
 *
 * @param session 发起踢人操作的玩家会话
 * @param name 被踢出成员的名称
 *
 * 主要流程：
 *   1. 检查操作者是否有踢人权限
 *   2. 检查被踢成员是否存在
 *   3. 检查被踢成员是否为会长（会长不能被踢）
 *   4. 检查操作者等级是否高于被踢成员
 *   5. 从公会中删除成员
 *   6. 记录踢人事件日志
 *   7. 广播踢人事件通知
 */
void Guild::HandleRemoveMember(WorldSession* session, std::string_view name)
{
    Player* player = session->GetPlayer();

    // 操作者必须拥有踢人权限
    if (!_HasRankRight(player, GR_RIGHT_REMOVE))
    {
        SendCommandResult(session, GUILD_COMMAND_REMOVE, ERR_GUILD_PERMISSIONS);
    }
    else if (Member* member = GetMember(name))
    {
        // 公会会长不能被踢出
        if (member->IsRank(GR_GUILDMASTER))
        {
            SendCommandResult(session, GUILD_COMMAND_REMOVE, ERR_GUILD_LEADER_LEAVE);
        }
        else
        {
            // 不能踢出等级相同或更高的成员
            Member const* memberMe = GetMember(player->GetGUID());
            if (!memberMe || member->IsRankNotLower(memberMe->GetRankId()))
            {
                SendCommandResult(session, GUILD_COMMAND_REMOVE, ERR_GUILD_RANK_TOO_HIGH_S, name);
            }
            else
            {
                ObjectGuid guid = member->GetGUID();

                // 注意：调用DeleteMember后，member指针将失效
                CharacterDatabaseTransaction trans(nullptr);
                DeleteMember(trans, guid, false, true);

                // 记录踢人事件日志
                _LogEvent(GUILD_EVENT_LOG_UNINVITE_PLAYER, player->GetGUID().GetCounter(), guid.GetCounter());

                // 广播成员被踢出事件
                _BroadcastEvent(GE_REMOVED, ObjectGuid::Empty, name, player->GetName());
            }
        }
    }
}

/**
 * @brief 处理成员等级晋升或降级
 *
 * 职责：
 *   处理公会成员等级的晋升或降级操作，验证权限和等级限制。
 *
 * @param session 发起操作的玩家会话
 * @param name 目标成员名称
 * @param demote true为降级，false为晋升
 *
 * 主要流程：
 *   1. 检查操作者是否有晋升/降级权限
 *   2. 检查目标成员是否存在
 *   3. 检查是否对自己操作（不允许）
 *   4. 降级时：
 *      a. 检查目标成员等级是否低于操作者
 *      b. 检查目标成员是否已达最低等级
 *   5. 晋升时：
 *      a. 检查目标成员等级是否可以晋升（不能超过操作者等级-1）
 *   6. 更新成员等级
 *   7. 记录事件日志
 *   8. 广播晋升/降级事件
 */
void Guild::HandleUpdateMemberRank(WorldSession* session, std::string_view name, bool demote)
{
    Player* player = session->GetPlayer();
    GuildCommandType type = demote ? GUILD_COMMAND_DEMOTE : GUILD_COMMAND_PROMOTE;

    // 操作者必须拥有晋升或降级权限
    if (!_HasRankRight(player, demote ? GR_RIGHT_DEMOTE : GR_RIGHT_PROMOTE))
    {
        SendCommandResult(session, type, ERR_GUILD_PERMISSIONS);
    }
    // 目标成员必须存在于公会中
    else if (Member* member = GetMember(name))
    {
        // 玩家不能对自己进行晋升或降级操作
        if (member->IsSamePlayer(player->GetGUID()))
        {
            SendCommandResult(session, type, ERR_GUILD_NAME_INVALID);
            return;
        }

        // 获取操作者的成员信息和等级
        Member const* memberMe = GetMember(player->GetGUID());
        ASSERT(memberMe);
        uint8 rankId = memberMe->GetRankId();

        if (demote)
        {
            // 降级：只能降级等级低于自己的成员
            if (member->IsRankNotLower(rankId))
            {
                SendCommandResult(session, type, ERR_GUILD_RANK_TOO_HIGH_S, name);
                return;
            }

            // 最低等级的成员不能再降级
            if (member->GetRankId() >= _GetLowestRankId())
            {
                SendCommandResult(session, type, ERR_GUILD_RANK_TOO_LOW_S, name);
                return;
            }
        }
        else
        {
            // 晋升：只能晋升到比操作者等级低的等级
            // member->GetRankId() + 1 是当前玩家可以晋升的最高等级
            if (member->IsRankNotLower(rankId + 1))
            {
                SendCommandResult(session, type, ERR_GUILD_RANK_TOO_HIGH_S, name);
                return;
            }
        }

        // 计算新等级（降级+1，晋升-1，因为等级ID越小等级越高）
        uint32 newRankId = member->GetRankId() + (demote ? 1 : -1);

        // 更新成员等级
        CharacterDatabaseTransaction trans(nullptr);
        member->ChangeRank(trans, newRankId);

        // 记录晋升/降级事件日志
        _LogEvent(demote ? GUILD_EVENT_LOG_DEMOTE_PLAYER : GUILD_EVENT_LOG_PROMOTE_PLAYER,
                  player->GetGUID().GetCounter(), member->GetGUID().GetCounter(), newRankId);

        // 广播晋升/降级事件通知
        _BroadcastEvent(demote ? GE_DEMOTION : GE_PROMOTION, ObjectGuid::Empty,
                        player->GetName(), member->GetName(), _GetRankName(newRankId));
    }
}

void Guild::HandleAddNewRank(WorldSession* session, std::string_view name)
{
    uint8 size = _GetRanksSize();
    if (size >= GUILD_RANKS_MAX_COUNT)
        return;

    // Only leader can add new rank
    if (_IsLeader(session->GetPlayer()))
    {
        CharacterDatabaseTransaction trans(nullptr);
        if (_CreateRank(trans, name, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK))
            _BroadcastEvent(GE_RANK_UPDATED, ObjectGuid::Empty, std::to_string(size), name, std::to_string(m_ranks.size()));
    }
}

void Guild::HandleRemoveLowestRank(WorldSession* session)
{
    HandleRemoveRank(session, _GetLowestRankId());
}

void Guild::HandleRemoveRank(WorldSession* session, uint8 rankId)
{
    // Cannot remove rank if total count is minimum allowed by the client or is not leader
    if (_GetRanksSize() <= GUILD_RANKS_MIN_COUNT || rankId >= _GetRanksSize() || !_IsLeader(session->GetPlayer()))
        return;

    // Delete bank rights for rank
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS_FOR_RANK);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8(1, rankId);
    CharacterDatabase.Execute(stmt);
    // Delete rank
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_LOWEST_RANK);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8(1, rankId);
    CharacterDatabase.Execute(stmt);

    // match what the sql statement does
    m_ranks.erase(m_ranks.begin() + rankId, m_ranks.end());

    _BroadcastEvent(GE_RANK_DELETED, ObjectGuid::Empty, std::to_string(m_ranks.size()));
}

void Guild::HandleMemberDepositMoney(WorldSession* session, uint32 amount)
{
    Player* player = session->GetPlayer();

    // Call script after validation and before money transfer.
    sScriptMgr->OnGuildMemberDepositMoney(this, player, amount);

    if (m_bankMoney > GUILD_BANK_MONEY_LIMIT - amount)
    {
        SendCommandResult(session, GUILD_COMMAND_MOVE_ITEM, ERR_GUILD_BANK_FULL);
        return;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    _ModifyBankMoney(trans, amount, true);

    player->ModifyMoney(-int32(amount));
    player->SaveGoldToDB(trans);
    _LogBankEvent(trans, GUILD_BANK_LOG_DEPOSIT_MONEY, uint8(0), player->GetGUID().GetCounter(), amount);

    CharacterDatabase.CommitTransaction(trans);

    _BroadcastEvent(GE_BANK_MONEY_SET, ObjectGuid::Empty, Trinity::StringFormat("{:016X}", m_bankMoney));

    if (player->GetSession()->HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
    {
        sLog->OutCommand(player->GetSession()->GetAccountId(),
            "GM {} (Account: {}) deposit money (Amount: {}) to guild bank (Guild ID {})",
            player->GetName(), player->GetSession()->GetAccountId(), amount, m_id);
    }
}

bool Guild::HandleMemberWithdrawMoney(WorldSession* session, uint32 amount, bool repair)
{
    //clamp amount to MAX_MONEY_AMOUNT, Players can't hold more than that anyway
    amount = std::min(amount, MAX_MONEY_AMOUNT);

    if (m_bankMoney < amount)                               // Not enough money in bank
        return false;

    Player* player = session->GetPlayer();

    Member* member = GetMember(player->GetGUID());
    if (!member)
        return false;

   if (uint32(_GetMemberRemainingMoney(*member)) < amount)   // Check if we have enough slot/money today
       return false;

    // Call script after validation and before money transfer.
    sScriptMgr->OnGuildMemberWitdrawMoney(this, player, amount, repair);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    // Add money to player (if required)
    if (!repair)
    {
        if (!player->ModifyMoney(amount))
            return false;

        player->SaveGoldToDB(trans);
    }

    // Update remaining money amount
    member->UpdateBankWithdrawValue(trans, GUILD_BANK_MAX_TABS, amount);
    // Remove money from bank
    _ModifyBankMoney(trans, amount, false);

    // Log guild bank event
    _LogBankEvent(trans, repair ? GUILD_BANK_LOG_REPAIR_MONEY : GUILD_BANK_LOG_WITHDRAW_MONEY, uint8(0), player->GetGUID().GetCounter(), amount);
    CharacterDatabase.CommitTransaction(trans);

    _BroadcastEvent(GE_BANK_MONEY_SET, ObjectGuid::Empty, Trinity::StringFormat("{:016X}", m_bankMoney));
    return true;
}

void Guild::HandleMemberLogout(WorldSession* session)
{
    Player* player = session->GetPlayer();
    if (Member* member = GetMember(player->GetGUID()))
    {
        member->SetStats(player);
        member->UpdateLogoutTime();
        member->ResetFlags();
    }
    _BroadcastEvent(GE_SIGNED_OFF, player->GetGUID(), player->GetName());
}

void Guild::HandleDisband(WorldSession* session)
{
    // Only leader can disband guild
    if (_IsLeader(session->GetPlayer()))
    {
        Disband();
        TC_LOG_DEBUG("guild", "Guild Successfully Disbanded");
    }
}

// Send data to client
void Guild::SendInfo(WorldSession* session) const
{
    WorldPackets::Guild::GuildInfoResponse guildInfo;
    guildInfo.GuildName = m_name;
    guildInfo.CreateDate.SetUtcTimeFromUnixTime(m_createdDate);
    guildInfo.CreateDate += session->GetTimezoneOffset();
    guildInfo.NumMembers = int32(m_members.size());
    guildInfo.NumAccounts = m_accountsNumber;

    session->SendPacket(guildInfo.Write());
    TC_LOG_DEBUG("guild", "SMSG_GUILD_INFO [{}]", session->GetPlayerInfo());
}

void Guild::SendEventLog(WorldSession* session) const
{
    std::list<EventLogEntry> const& eventLog = m_eventLog.GetGuildLog();

    WorldPackets::Guild::GuildEventLogQueryResults packet;
    packet.Entry.reserve(eventLog.size());

    for (EventLogEntry const& entry : eventLog)
        entry.WritePacket(packet);

    session->SendPacket(packet.Write());
    TC_LOG_DEBUG("guild", "MSG_GUILD_EVENT_LOG_QUERY [{}]", session->GetPlayerInfo());
}

void Guild::SendBankLog(WorldSession* session, uint8 tabId) const
{
    // GUILD_BANK_MAX_TABS send by client for money log
    if (tabId < _GetPurchasedTabsSize() || tabId == GUILD_BANK_MAX_TABS)
    {
        std::list<BankEventLogEntry> const& bankEventLog = m_bankEventLog[tabId].GetGuildLog();

        WorldPackets::Guild::GuildBankLogQueryResults packet;
        packet.Tab = tabId;

        packet.Entry.reserve(bankEventLog.size());
        for (BankEventLogEntry const& entry : bankEventLog)
            entry.WritePacket(packet);

        session->SendPacket(packet.Write());

        TC_LOG_DEBUG("guild", "MSG_GUILD_BANK_LOG_QUERY [{}]", session->GetPlayerInfo());
    }
}

void Guild::SendBankTabData(WorldSession* session, uint8 tabId, bool sendAllSlots) const
{
    if (tabId < _GetPurchasedTabsSize())
        _SendBankContent(session, tabId, sendAllSlots);
}

void Guild::SendBankTabsInfo(WorldSession* session, bool sendAllSlots /*= false*/) const
{
    _SendBankList(session, 0, sendAllSlots);
}

void Guild::SendBankTabText(WorldSession* session, uint8 tabId) const
{
    if (BankTab const* tab = GetBankTab(tabId))
        tab->SendText(this, session);
}

void Guild::SendPermissions(WorldSession* session) const
{
    Member const* member = GetMember(session->GetPlayer()->GetGUID());
    if (!member)
        return;

    uint8 rankId = member->GetRankId();

    WorldPackets::Guild::GuildPermissionsQueryResults queryResult;
    queryResult.RankID = rankId;
    queryResult.WithdrawGoldLimit = _GetRankBankMoneyPerDay(rankId);
    queryResult.Flags = _GetRankRights(rankId);
    queryResult.NumTabs = _GetPurchasedTabsSize();

    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
    {
        queryResult.Tab[tabId].Flags = _GetRankBankTabRights(rankId, tabId);
        queryResult.Tab[tabId].WithdrawItemLimit = _GetMemberRemainingSlots(*member, tabId);
    }

    session->SendPacket(queryResult.Write());
    TC_LOG_DEBUG("guild", "MSG_GUILD_PERMISSIONS [{}] Rank: {}", session->GetPlayerInfo(), rankId);
}

void Guild::SendMoneyInfo(WorldSession* session) const
{
    Member const* member = GetMember(session->GetPlayer()->GetGUID());
    if (!member)
        return;

    int32 amount = _GetMemberRemainingMoney(*member);

    WorldPackets::Guild::GuildBankRemainingWithdrawMoney packet;
    packet.RemainingWithdrawMoney = amount;
    session->SendPacket(packet.Write());

    TC_LOG_DEBUG("guild", "MSG_GUILD_BANK_MONEY_WITHDRAWN [{}] Money: {}", session->GetPlayerInfo(), amount);
}

void Guild::SendLoginInfo(WorldSession* session)
{
    WorldPackets::Guild::GuildEvent motd;
    motd.Type = GE_MOTD;
    motd.Params.emplace_back(m_motd);
    session->SendPacket(motd.Write());

    TC_LOG_DEBUG("guild", "SMSG_GUILD_EVENT [{}] MOTD", session->GetPlayerInfo());

    SendBankTabsInfo(session);

    Player* player = session->GetPlayer();

    HandleRoster(session);
    _BroadcastEvent(GE_SIGNED_ON, player->GetGUID(), player->GetName());

    if (Member* member = GetMember(player->GetGUID()))
    {
        member->SetStats(player);
        member->AddFlag(GUILDMEMBER_STATUS_ONLINE);
    }
}

// Loading methods
/**
 * @brief 从数据库加载公会信息
 *
 * 职责：
 *   从数据库查询结果中加载公会的基本信息，包括ID、名称、会长、
 *   公会徽章、消息、创建时间、银行金币和银行标签页数量。
 *
 * @param fields 数据库查询结果字段数组
 *
 * @return true 加载成功
 * @return false 加载失败（本函数始终返回true）
 *
 * 加载的数据包括：
 *   - 公会ID
 *   - 公会名称
 *   - 会长GUID
 *   - 公会徽章信息（样式、颜色、边框等）
 *   - 公会信息描述
 *   - 每日消息
 *   - 创建日期
 *   - 银行金币数量
 *   - 已购买的银行标签页数量
 */
bool Guild::LoadFromDB(Field* fields)
{
    // 加载公会基本属性
    m_id            = fields[0].GetUInt32();                                    // 公会ID
    m_name          = fields[1].GetString();                                    // 公会名称
    m_leaderGuid    = ObjectGuid(HighGuid::Player, fields[2].GetUInt32());     // 会长GUID
    m_emblemInfo.LoadFromDB(fields);                                            // 公会徽章信息
    m_info          = fields[8].GetString();                                    // 公会信息描述
    m_motd          = fields[9].GetString();                                    // 每日消息
    m_createdDate   = time_t(fields[10].GetUInt32());                          // 创建日期
    m_bankMoney     = fields[11].GetUInt64();                                   // 银行金币

    // 加载银行标签页数量
    uint8 purchasedTabs = uint8(fields[12].GetUInt64());
    if (purchasedTabs > GUILD_BANK_MAX_TABS)
        purchasedTabs = GUILD_BANK_MAX_TABS;

    // 初始化银行标签页
    m_bankTabs.clear();
    m_bankTabs.reserve(purchasedTabs);
    for (uint8 i = 0; i < purchasedTabs; ++i)
        m_bankTabs.emplace_back(m_id, i);

    return true;
}

/**
 * @brief 从数据库加载公会等级信息
 *
 * 职责：
 *   从数据库查询结果中加载公会等级信息，并添加到等级列表。
 *
 * @param fields 数据库查询结果字段数组
 */
void Guild::LoadRankFromDB(Field* fields)
{
    RankInfo rankInfo(m_id);
    rankInfo.LoadFromDB(fields);
    m_ranks.push_back(rankInfo);
}

/**
 * @brief 从数据库加载公会成员信息
 *
 * 职责：
 *   从数据库查询结果中加载公会成员信息，包括玩家GUID、等级等数据。
 *
 * @param fields 数据库查询结果字段数组
 *
 * @return true 加载成功
 * @return false 加载失败（成员已存在或数据无效）
 *
 * 主要流程：
 *   1. 从字段中提取玩家GUID
 *   2. 尝试将成员添加到成员列表
 *   3. 加载成员详细数据
 *   4. 如果加载失败，从数据库删除无效成员记录
 *   5. 更新角色缓存中的公会ID
 */
bool Guild::LoadMemberFromDB(Field* fields)
{
    // 提取玩家GUID
    ObjectGuid::LowType lowguid = fields[1].GetUInt32();
    ObjectGuid playerGuid(HighGuid::Player, lowguid);

    // 尝试将成员添加到成员映射表
    auto [memberIt, isNew] = m_members.try_emplace(lowguid, m_id, playerGuid, fields[2].GetUInt8());
    if (!isNew)
    {
        TC_LOG_ERROR("guild", "Tried to add {} to guild '{}'. Member already exists.", playerGuid.ToString(), m_name);
        return false;
    }

    // 加载成员详细数据
    Member& member = memberIt->second;
    if (!member.LoadFromDB(fields))
    {
        // 如果加载失败，从数据库删除无效记录并从成员列表移除
        CharacterDatabaseTransaction trans(nullptr);
        _DeleteMemberFromDB(trans, lowguid);
        m_members.erase(memberIt);
        return false;
    }

    // 更新角色缓存中的公会ID
    sCharacterCache->UpdateCharacterGuildId(playerGuid, GetId());
    return true;
}

/**
 * @brief 从数据库加载公会银行权限信息
 *
 * 职责：
 *   从数据库查询结果中加载公会银行的标签页权限设置。
 *
 * @param fields 数据库查询结果字段数组
 */
void Guild::LoadBankRightFromDB(Field* fields)
{
    // 从字段中提取银行权限信息
    // 字段格式：tabId, rankId, rights, slots
    GuildBankRightsAndSlots rightsAndSlots(fields[1].GetUInt8(), fields[3].GetUInt8(), fields[4].GetUInt32());
    _SetRankBankTabRightsAndSlots(fields[2].GetUInt8(), rightsAndSlots, false);
}

/**
 * @brief 从数据库加载公会事件日志
 *
 * 职责：
 *   从数据库加载公会事件日志记录，如成员加入、离开、晋升等事件。
 *
 * @param fields 数据库查询结果字段数组
 *
 * @return true 加载成功
 * @return false 加载失败（日志已满）
 */
bool Guild::LoadEventLogFromDB(Field* fields)
{
    if (m_eventLog.CanInsert())
    {
        m_eventLog.LoadEvent(
            m_id,                                       // 公会ID
            fields[1].GetUInt32(),                      // 事件GUID
            time_t(fields[6].GetUInt32()),              // 时间戳
            GuildEventLogTypes(fields[2].GetUInt8()),   // 事件类型
            fields[3].GetUInt32(),                      // 玩家GUID 1
            fields[4].GetUInt32(),                      // 玩家GUID 2
            fields[5].GetUInt8());                      // 等级
        return true;
    }
    return false;
}

/**
 * @brief 从数据库加载公会银行事件日志
 *
 * 职责：
 *   从数据库加载公会银行相关的事件日志，如存取款、物品操作等。
 *
 * @param fields 数据库查询结果字段数组
 *
 * @return true 加载成功
 * @return false 加载失败
 */
bool Guild::LoadBankEventLogFromDB(Field* fields)
{
    // 获取标签页ID和是否为金币标签页
    uint8 dbTabId = fields[1].GetUInt8();
    bool isMoneyTab = (dbTabId == GUILD_BANK_MONEY_LOGS_TAB);

    // 检查标签页ID是否有效
    if (dbTabId < _GetPurchasedTabsSize() || isMoneyTab)
    {
        // 金币事件使用特殊标签页ID
        uint8 tabId = isMoneyTab ? uint8(GUILD_BANK_MAX_TABS) : dbTabId;
        LogHolder<BankEventLogEntry>& bankLog = m_bankEventLog[tabId];
        if (bankLog.CanInsert())
        {
            ObjectGuid::LowType guid = fields[2].GetUInt32();
            GuildBankEventLogTypes eventType = GuildBankEventLogTypes(fields[3].GetUInt8());
            if (BankEventLogEntry::IsMoneyEvent(eventType))
            {
                if (!isMoneyTab)
                {
                    TC_LOG_ERROR("guild", "GuildBankEventLog ERROR: MoneyEvent(LogGuid: {}, Guild: {}) does not belong to money tab ({}), ignoring...", guid, m_id, dbTabId);
                    return false;
                }
            }
            else if (isMoneyTab)
            {
                TC_LOG_ERROR("guild", "GuildBankEventLog ERROR: non-money event (LogGuid: {}, Guild: {}) belongs to money tab, ignoring...", guid, m_id);
                return false;
            }
            bankLog.LoadEvent(
                m_id,                                   // guild id
                guid,                                   // guid
                time_t(fields[8].GetUInt32()),          // timestamp
                dbTabId,                                // tab id
                eventType,                              // event type
                fields[4].GetUInt32(),                  // player guid
                fields[5].GetUInt32(),                  // item or money
                fields[6].GetUInt16(),                  // itam stack count
                fields[7].GetUInt8());                  // dest tab id
        }
    }
    return true;
}

void Guild::LoadBankTabFromDB(Field* fields)
{
    uint8 tabId = fields[1].GetUInt8();
    if (tabId >= _GetPurchasedTabsSize())
        TC_LOG_ERROR("guild", "Invalid tab (tabId: {}) in guild bank, skipped.", tabId);
    else
        m_bankTabs[tabId].LoadFromDB(fields);
}

bool Guild::LoadBankItemFromDB(Field* fields)
{
    uint8 tabId = fields[12].GetUInt8();
    if (tabId >= _GetPurchasedTabsSize())
    {
        TC_LOG_ERROR("guild", "Invalid tab for item (GUID: {}, id: #{}) in guild bank, skipped.",
            fields[14].GetUInt32(), fields[15].GetUInt32());
        return false;
    }
    return m_bankTabs[tabId].LoadItemFromDB(fields);
}

// Validates guild data loaded from database. Returns false if guild should be deleted.
bool Guild::Validate()
{
    // Validate ranks data
    // GUILD RANKS represent a sequence starting from 0 = GUILD_MASTER (ALL PRIVILEGES) to max 9 (lowest privileges).
    // The lower rank id is considered higher rank - so promotion does rank-- and demotion does rank++
    // Between ranks in sequence cannot be gaps - so 0, 1, 2, 4 is impossible
    // Min ranks count is 5 and max is 10.
    bool broken_ranks = false;
    uint8 ranks = _GetRanksSize();

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    if (ranks < GUILD_RANKS_MIN_COUNT || ranks > GUILD_RANKS_MAX_COUNT)
    {
        TC_LOG_ERROR("guild", "Guild {} has invalid number of ranks, creating new...", m_id);
        broken_ranks = true;
    }
    else
    {
        for (uint8 rankId = 0; rankId < ranks; ++rankId)
        {
            RankInfo* rankInfo = GetRankInfo(rankId);
            if (rankInfo->GetId() != rankId)
            {
                TC_LOG_ERROR("guild", "Guild {} has broken rank id {}, creating default set of ranks...", m_id, rankId);
                broken_ranks = true;
            }
            else
                rankInfo->CreateMissingTabsIfNeeded(_GetPurchasedTabsSize(), trans, true);
        }
    }

    if (broken_ranks)
    {
        m_ranks.clear();
        _CreateDefaultGuildRanks(trans, DEFAULT_LOCALE);
    }

    // Validate members' data
    for (auto& [guid, member] : m_members)
        if (member.GetRankId() > _GetRanksSize())
            member.ChangeRank(trans, _GetLowestRankId());

    // Repair the structure of the guild.
    // If the guildmaster doesn't exist or isn't member of the guild
    // attempt to promote another member.
    Member* pLeader = GetMember(m_leaderGuid);
    if (!pLeader)
    {
        CharacterDatabaseTransaction dummy(nullptr);
        if (DeleteMember(dummy, m_leaderGuid))
            return false;
    }
    else if (!pLeader->IsRank(GR_GUILDMASTER))
        _SetLeaderGUID(*pLeader);

    // Check config if multiple guildmasters are allowed
    if (!sConfigMgr->GetBoolDefault("Guild.AllowMultipleGuildMaster", false))
        for (auto& [guid, member] : m_members)
            if ((member.GetRankId() == GR_GUILDMASTER) && !member.IsSamePlayer(m_leaderGuid))
                member.ChangeRank(trans, GR_OFFICER);

    if (trans->GetSize() > 0)
        CharacterDatabase.CommitTransaction(trans);
    _UpdateAccountsNumber();
    return true;
}

// Broadcasts
void Guild::BroadcastToGuild(WorldSession* session, bool officerOnly, std::string_view msg, uint32 language) const
{
    if (session && session->GetPlayer() && _HasRankRight(session->GetPlayer(), officerOnly ? GR_RIGHT_OFFCHATSPEAK : GR_RIGHT_GCHATSPEAK))
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, officerOnly ? CHAT_MSG_OFFICER : CHAT_MSG_GUILD, Language(language), session->GetPlayer(), nullptr, msg);
        for (auto const& [guid, member] : m_members)
            if (Player* player = member.FindConnectedPlayer())
                if (player->GetSession() && _HasRankRight(player, officerOnly ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN) &&
                    !player->GetSocial()->HasIgnore(session->GetPlayer()->GetGUID()))
                    player->SendDirectMessage(&data);
    }
}

void Guild::BroadcastPacketToRank(WorldPacket const* packet, uint8 rankId) const
{
    for (auto const& [guid, member] : m_members)
        if (member.IsRank(rankId))
            if (Player* player = member.FindConnectedPlayer())
                player->SendDirectMessage(packet);
}

void Guild::BroadcastPacket(WorldPacket const* packet) const
{
    for (auto const& [guid, member] : m_members)
        if (Player* player = member.FindConnectedPlayer())
            player->SendDirectMessage(packet);
}

void Guild::MassInviteToEvent(WorldSession* session, uint32 minLevel, uint32 maxLevel, uint32 minRank)
{
    WorldPackets::Calendar::CalendarEventInitialInvites packet(true);

    for (auto const& [guid, member] : m_members)
    {
        // not sure if needed, maybe client checks it as well
        if (packet.Invites.size() >= CALENDAR_MAX_INVITES)
        {
            if (Player* player = session->GetPlayer())
                sCalendarMgr->SendCalendarCommandResult(player->GetGUID(), CALENDAR_ERROR_INVITES_EXCEEDED);
            return;
        }

        if (member.GetGUID() == session->GetPlayer()->GetGUID())
            continue;

        uint32 level = sCharacterCache->GetCharacterLevelByGuid(member.GetGUID());
        if (level < minLevel || level > maxLevel)
            continue;

        if (!member.IsRankNotLower(minRank))
            continue;

        packet.Invites.emplace_back(member.GetGUID(), level);
    }

    session->SendPacket(packet.Write());
}

// Members handling
/**
 * @brief 添加新成员到公会
 *
 * 职责：
 *   将指定玩家添加到公会中，设置成员等级，更新玩家公会信息，
 *   记录事件日志并广播通知其他成员。
 *
 * @param trans 数据库事务对象
 * @param guid 要添加的玩家GUID
 * @param rankId 成员等级ID（可选，默认为最低等级）
 *
 * @return true 添加成功
 * @return false 添加失败（玩家已在其他公会或玩家不存在）
 *
 * 主要流程：
 *   1. 检查玩家是否已在公会中
 *   2. 移除玩家的其他公会申请书签名
 *   3. 如果未指定等级，分配最低等级
 *   4. 将成员添加到成员列表
 *   5. 如果玩家在线，设置玩家公会信息并发送登录信息
 *   6. 如果玩家离线，从数据库加载玩家数据
 *   7. 保存成员信息到数据库
 *   8. 更新账号数量统计
 *   9. 记录公会事件日志
 *   10. 广播成员加入事件
 *   11. 触发脚本事件
 */
bool Guild::AddMember(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 rankId)
{
    // 查找玩家（在线或离线）
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);

    // 检查玩家是否已在公会中（在线玩家）
    if (player)
    {
        if (player->GetGuildId() != 0)
            return false;
    }
    // 检查玩家是否已在公会中（离线玩家，通过缓存查询）
    else if (sCharacterCache->GetCharacterGuildIdByGuid(guid) != 0)
        return false;

    // 移除玩家在其他公会申请书上的签名
    // 这可以防止玩家同时加入多个公会，保证公会数据完整性
    Player::RemovePetitionsAndSigns(guid, GUILD_CHARTER_TYPE);

    ObjectGuid::LowType lowguid = guid.GetCounter();

    // 如果未指定等级，分配最低等级
    if (rankId == GUILD_RANK_NONE)
        rankId = _GetLowestRankId();

    // 将成员添加到成员映射表中
    auto [memberIt, isNew] = m_members.try_emplace(lowguid, m_id, guid, rankId);
    if (!isNew)
    {
        TC_LOG_ERROR("guild", "Tried to add {} to guild '{}'. Member already exists.", guid.ToString(), m_name);
        return false;
    }

    Member& member = memberIt->second;
    std::string name;

    // 处理在线玩家
    if (player)
    {
        // 设置玩家的公会相关信息
        player->SetInGuild(m_id);
        player->SetGuildIdInvited(0);
        player->SetRank(rankId);

        // 设置成员统计数据
        member.SetStats(player);

        // 发送公会登录信息给该玩家
        SendLoginInfo(player->GetSession());

        name = player->GetName();
    }
    // 处理离线玩家
    else
    {
        member.ResetFlags();

        bool ok = false;
        // 从数据库加载玩家数据
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_DATA_FOR_GUILD);
        stmt->setUInt32(0, lowguid);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            Field* fields = result->Fetch();
            name = fields[0].GetString();
            // 设置成员统计数据（姓名、等级、等级、性别、区域、账户ID）
            member.SetStats(
                name,
                fields[1].GetUInt8(),
                fields[2].GetUInt8(),
                fields[3].GetUInt8(),
                fields[4].GetUInt16(),
                fields[5].GetUInt32());

            ok = member.CheckStats();
        }
        // 如果数据无效，移除成员并返回失败
        if (!ok)
        {
            m_members.erase(memberIt);
            return false;
        }
        // 更新角色缓存中的公会ID
        sCharacterCache->UpdateCharacterGuildId(guid, GetId());
    }

    // 保存成员信息到数据库
    member.SaveToDB(trans);

    // 更新公会账号数量统计
    _UpdateAccountsNumber();

    // 记录加入公会事件日志
    _LogEvent(GUILD_EVENT_LOG_JOIN_GUILD, lowguid);

    // 广播成员加入事件通知其他成员
    _BroadcastEvent(GE_JOINED, guid, name);

    // 成员成功添加后触发脚本事件
    sScriptMgr->OnGuildAddMember(this, player, rankId);

    return true;
}

/**
 * @brief 删除公会成员
 *
 * 职责：
 *   从公会中移除指定成员，处理会长转移逻辑，更新玩家公会信息，
 *   并从数据库中删除成员记录。
 *
 * @param trans 数据库事务对象
 * @param guid 要删除的玩家GUID
 * @param isDisbanding 是否正在解散公会（默认false）
 * @param isKicked 是否是被踢出公会（默认false）
 *
 * @return true 公会被解散（删除会长后无其他成员或公会变为空）
 * @return false 成功删除成员，公会继续存在
 *
 * 主要流程：
 *   1. 如果删除的是会长且不是解散操作，则需要转移会长
 *      a. 找到等级最高（等级ID最小）的成员作为新会长
 *      b. 如果没有其他成员，解散公会
 *      c. 设置新会长并广播会长变更事件
 *   2. 触发成员移除脚本事件
 *   3. 从成员列表中移除成员
 *   4. 更新玩家的公会信息（在线玩家直接更新，离线玩家更新缓存）
 *   5. 从数据库删除成员记录
 *   6. 更新账号数量统计
 *   7. 如果公会变空，解散公会
 */
bool Guild::DeleteMember(CharacterDatabaseTransaction trans, ObjectGuid guid, bool isDisbanding, bool isKicked)
{
    ObjectGuid::LowType lowguid = guid.GetCounter();
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);

    // 处理会长被删除的特殊情况
    // 会长可能在以下情况被删除：
    // 1. 加载公会时会长GUID在角色表中不存在
    // 2. 通过GM命令将会长从公会中移除
    if (m_leaderGuid == guid && !isDisbanding)
    {
        Member* oldLeader = nullptr;
        Member* newLeader = nullptr;

        // 遍历所有成员，寻找旧会长和新会长
        for (auto& [guid, member] : m_members)
        {
            if (guid == lowguid)
                oldLeader = &member;
            // 选择等级最高（等级ID最小）的成员作为新会长
            else if (!newLeader || newLeader->GetRankId() > member.GetRankId())
                newLeader = &member;
        }

        // 如果没有其他成员，解散公会
        if (!newLeader)
        {
            Disband();
            return true;
        }

        // 设置新会长
        _SetLeaderGUID(*newLeader);

        // 如果新会长在线，更新其等级为公会会长
        if (Player* newLeaderPlayer = newLeader->FindPlayer())
            newLeaderPlayer->SetRank(GR_GUILDMASTER);

        // 如果旧会长存在（加载时会长已删除的情况可能不存在），广播会长变更事件
        if (oldLeader)
        {
            _BroadcastEvent(GE_LEADER_CHANGED, ObjectGuid::Empty, oldLeader->GetName(), newLeader->GetName());
            _BroadcastEvent(GE_LEFT, guid, oldLeader->GetName());
        }
    }

    // 在成员实际从公会移除前触发脚本事件
    sScriptMgr->OnGuildRemoveMember(this, player, isDisbanding, isKicked);

    // 从成员列表中移除成员
    m_members.erase(lowguid);

    // 更新玩家的公会信息
    // 在线玩家：直接更新玩家对象的公会ID和等级
    // 离线玩家：更新角色缓存中的公会ID
    if (player)
    {
        player->SetInGuild(0);
        player->SetRank(0);
    }
    else
        sCharacterCache->UpdateCharacterGuildId(guid, 0);

    // 从数据库删除成员记录
    _DeleteMemberFromDB(trans, lowguid);

    // 如果不是解散操作，更新账号数量统计
    if (!isDisbanding)
        _UpdateAccountsNumber();

    // 如果公会变为空，解散公会
    if (m_members.empty())
    {
        Disband();
        return true;
    }

    return false;
}

bool Guild::ChangeMemberRank(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 newRank)
{
    if (newRank <= _GetLowestRankId())                    // Validate rank (allow only existing ranks)
    {
        if (Member* member = GetMember(guid))
        {
            member->ChangeRank(trans, newRank);
            return true;
        }
    }

    return false;
}

uint64 Guild::GetMemberAvailableMoneyForRepairItems(ObjectGuid guid) const
{
    Member const* member = GetMember(guid);
    if (!member)
        return 0;

    return std::min(m_bankMoney, static_cast<uint64>(_GetMemberRemainingMoney(*member)));
}

// Bank (items move)
void Guild::SwapItems(Player* player, uint8 tabId, uint8 slotId, uint8 destTabId, uint8 destSlotId, uint32 splitedAmount)
{
    if (tabId >= _GetPurchasedTabsSize() || slotId >= GUILD_BANK_MAX_SLOTS ||
        destTabId >= _GetPurchasedTabsSize() || destSlotId >= GUILD_BANK_MAX_SLOTS)
        return;

    if (tabId == destTabId && slotId == destSlotId)
        return;

    BankMoveItemData from(this, player, tabId, slotId);
    BankMoveItemData to(this, player, destTabId, destSlotId);
    _MoveItems(&from, &to, splitedAmount);
}

void Guild::SwapItemsWithInventory(Player* player, bool toChar, uint8 tabId, uint8 slotId, uint8 playerBag, uint8 playerSlotId, uint32 splitedAmount)
{
    if ((slotId >= GUILD_BANK_MAX_SLOTS && slotId != NULL_SLOT) || tabId >= _GetPurchasedTabsSize())
        return;

    BankMoveItemData bankData(this, player, tabId, slotId);
    PlayerMoveItemData charData(this, player, playerBag, playerSlotId);
    if (toChar)
        _MoveItems(&bankData, &charData, splitedAmount);
    else
        _MoveItems(&charData, &bankData, splitedAmount);
}

// Bank tabs
void Guild::SetBankTabText(uint8 tabId, std::string_view text)
{
    if (BankTab* pTab = GetBankTab(tabId))
    {
        pTab->SetText(text);
        pTab->SendText(this, nullptr);
    }
}

bool Guild::_HasRankRight(Player* player, uint32 right) const
{
    if (player)
        if (Member const* member = GetMember(player->GetGUID()))
            return (_GetRankRights(member->GetRankId()) & right) != GR_RIGHT_EMPTY;
    return false;
}

void Guild::_DeleteMemberFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType lowguid)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_MEMBER);
    stmt->setUInt32(0, lowguid);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

// Private methods
void Guild::_CreateNewBankTab()
{
    uint8 tabId = _GetPurchasedTabsSize();                      // Next free id
    m_bankTabs.emplace_back(m_id, tabId);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_TAB);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8 (1, tabId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_TAB);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8 (1, tabId);
    trans->Append(stmt);

    ++tabId;
    for (auto itr = m_ranks.begin(); itr != m_ranks.end(); ++itr)
        (*itr).CreateMissingTabsIfNeeded(tabId, trans, false);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 创建默认公会等级
 *
 * 职责：
 *   创建公会默认的等级结构，包括会长、官员、老兵、成员和新手等级，
 *   并设置相应的权限。
 *
 * @param trans 数据库事务对象
 * @param loc 语言区域设置，用于本地化等级名称
 *
 * 默认等级结构：
 *   1. 公会会长 (Guild Master) - 所有权限
 *   2. 官员 (Officer) - 所有权限
 *   3. 老兵 (Veteran) - 公会频道听和说权限
 *   4. 成员 (Member) - 公会频道听和说权限
 *   5. 新手 (Initiate) - 公会频道听和说权限
 */
void Guild::_CreateDefaultGuildRanks(CharacterDatabaseTransaction trans, LocaleConstant loc)
{
    ASSERT(trans);

    // 删除公会现有等级记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_RANKS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 删除公会银行权限记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // 创建默认等级（按等级ID从小到大顺序创建）
    _CreateRank(trans, sObjectMgr->GetTrinityString(LANG_GUILD_MASTER,   loc), GR_RIGHT_ALL);                                          // 公会会长 - 所有权限
    _CreateRank(trans, sObjectMgr->GetTrinityString(LANG_GUILD_OFFICER,  loc), GR_RIGHT_ALL);                                          // 官员 - 所有权限
    _CreateRank(trans, sObjectMgr->GetTrinityString(LANG_GUILD_VETERAN,  loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);           // 老兵 - 公会频道权限
    _CreateRank(trans, sObjectMgr->GetTrinityString(LANG_GUILD_MEMBER,   loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);           // 成员 - 公会频道权限
    _CreateRank(trans, sObjectMgr->GetTrinityString(LANG_GUILD_INITIATE, loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);           // 新手 - 公会频道权限
}

/**
 * @brief 创建新的公会等级
 *
 * 职责：
 *   创建一个新的公会等级，设置等级名称和权限，并保存到数据库。
 *
 * @param trans 数据库事务对象
 * @param name 等级名称
 * @param rights 等级权限标志位
 *
 * @return true 创建成功
 * @return false 创建失败（已达最大等级数量限制）
 *
 * 注意：
 *   - 等级ID按顺序分配（0, 1, 2, ...），0 表示公会会长
 *   - 新等级会自动创建银行标签页权限记录
 */
bool Guild::_CreateRank(CharacterDatabaseTransaction trans, std::string_view name, uint32 rights)
{
    uint8 newRankId = _GetRanksSize();
    if (newRankId >= GUILD_RANKS_MAX_COUNT)
        return false;

    // 创建等级信息对象
    // 等级ID按顺序递增：0=会长, 1=官员, 2=老兵, ...
    RankInfo info(m_id, newRankId, name, rights, 0);
    m_ranks.push_back(info);

    // 处理事务
    bool const isInTransaction = bool(trans);
    if (!isInTransaction)
        trans = CharacterDatabase.BeginTransaction();

    // 为新等级创建银行标签页权限
    info.CreateMissingTabsIfNeeded(_GetPurchasedTabsSize(), trans);

    // 保存等级信息到数据库
    info.SaveToDB(trans);

    if (!isInTransaction)
        CharacterDatabase.CommitTransaction(trans);

    return true;
}

/**
 * @brief 更新公会账号数量统计
 *
 * 职责：
 *   统计公会中不同账号的数量（玩家可能有多个角色在同一公会）。
 *
 * 说明：
 *   一个账号可能有多个角色在同一公会中，但账号数只计一次。
 *   使用集合确保每个账号ID唯一。
 */
void Guild::_UpdateAccountsNumber()
{
    // 使用集合确保账号ID唯一
    std::unordered_set<uint32> accountsIdSet;
    for (auto const& [guid, member] : m_members)
        accountsIdSet.insert(member.GetAccountId());

    m_accountsNumber = accountsIdSet.size();
}

/**
 * @brief 检查玩家是否为公会会长
 *
 * 职责：
 *   判断指定玩家是否为公会会长。
 *
 * @param player 要检查的玩家指针
 *
 * @return true 是公会会长
 * @return false 不是公会会长
 *
 * 说明：
 *   同时检查会长GUID和玩家等级（以支持多个公会会长的特性）
 */
bool Guild::_IsLeader(Player* player) const
{
    // 检查玩家GUID是否与会长的GUID匹配
    if (player->GetGUID() == m_leaderGuid)
        return true;

    // 检查玩家等级是否为公会会长等级
    if (Member const* member = GetMember(player->GetGUID()))
        return member->IsRank(GR_GUILDMASTER);

    return false;
}

/**
 * @brief 删除公会银行物品
 *
 * 职责：
 *   删除所有公会银行标签页中的物品，并可选是否从数据库删除。
 *
 * @param trans 数据库事务对象
 * @param removeItemsFromDB 是否从数据库删除物品记录
 */
void Guild::_DeleteBankItems(CharacterDatabaseTransaction trans, bool removeItemsFromDB)
{
    // 删除每个银行标签页中的物品
    for (uint8 tabId = 0; tabId < _GetPurchasedTabsSize(); ++tabId)
        m_bankTabs[tabId].Delete(trans, removeItemsFromDB);

    // 清空银行标签页列表
    m_bankTabs.clear();
}

/**
 * @brief 修改公会银行金币
 *
 * 职责：
 *   增加或减少公会银行的金币数量。
 *
 * @param trans 数据库事务对象
 * @param amount 金币数量
 * @param add true为增加，false为减少
 *
 * @return true 操作成功
 * @return false 操作失败（金币不足）
 */
bool Guild::_ModifyBankMoney(CharacterDatabaseTransaction trans, uint64 amount, bool add)
{
    if (add)
    {
        // 增加金币
        m_bankMoney += amount;
    }
    else
    {
        // 减少金币前检查余额是否充足
        if (m_bankMoney < amount)
            return false;
        m_bankMoney -= amount;
    }

    // 更新数据库中的公会银行金币
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_MONEY);
    stmt->setUInt64(0, m_bankMoney);
    stmt->setUInt32(1, m_id);
    trans->Append(stmt);

    return true;
}

/**
 * @brief 设置公会会长GUID
 *
 * 职责：
 *   更新公会会长为指定成员，并将其等级设置为公会会长。
 *
 * @param pLeader 新会长的成员引用
 */
void Guild::_SetLeaderGUID(Member& pLeader)
{
    // 开启数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 更新会长GUID
    m_leaderGuid = pLeader.GetGUID();

    // 将新会长等级设置为公会会长
    pLeader.ChangeRank(trans, GR_GUILDMASTER);

    // 更新数据库中的会长信息
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_LEADER);
    stmt->setUInt32(0, m_leaderGuid.GetCounter());
    stmt->setUInt32(1, m_id);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

void Guild::_SetRankBankMoneyPerDay(uint8 rankId, uint32 moneyPerDay)
{
    if (RankInfo* rankInfo = GetRankInfo(rankId))
        rankInfo->SetBankMoneyPerDay(moneyPerDay);
}

void Guild::_SetRankBankTabRightsAndSlots(uint8 rankId, GuildBankRightsAndSlots rightsAndSlots, bool saveToDB)
{
    if (rightsAndSlots.GetTabId() >= _GetPurchasedTabsSize())
        return;

    if (RankInfo* rankInfo = GetRankInfo(rankId))
        rankInfo->SetBankTabSlotsAndRights(rightsAndSlots, saveToDB);
}

inline std::string Guild::_GetRankName(uint8 rankId) const
{
    if (RankInfo const* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetName();
    return "<unknown>";
}

inline uint32 Guild::_GetRankRights(uint8 rankId) const
{
    if (RankInfo const* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetRights();
    return 0;
}

inline int32 Guild::_GetRankBankMoneyPerDay(uint8 rankId) const
{
    if (RankInfo const* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetBankMoneyPerDay();
    return 0;
}

inline int32 Guild::_GetRankBankTabSlotsPerDay(uint8 rankId, uint8 tabId) const
{
    if (tabId < _GetPurchasedTabsSize())
        if (RankInfo const* rankInfo = GetRankInfo(rankId))
            return rankInfo->GetBankTabSlotsPerDay(tabId);
    return 0;
}

inline int8 Guild::_GetRankBankTabRights(uint8 rankId, uint8 tabId) const
{
    if (RankInfo const* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetBankTabRights(tabId);
    return 0;
}

inline int32 Guild::_GetMemberRemainingSlots(Member const& member, uint8 tabId) const
{
    uint8 rankId = member.GetRankId();
    if (rankId == GR_GUILDMASTER)
        return static_cast<int32>(GUILD_WITHDRAW_SLOT_UNLIMITED);
    if ((_GetRankBankTabRights(rankId, tabId) & GUILD_BANK_RIGHT_VIEW_TAB) != 0)
    {
        int32 remaining = _GetRankBankTabSlotsPerDay(rankId, tabId) - member.GetBankWithdrawValue(tabId);
        if (remaining > 0)
            return remaining;
    }
    return 0;
}

inline int32 Guild::_GetMemberRemainingMoney(Member const& member) const
{
    uint8 rankId = member.GetRankId();
    if (rankId == GR_GUILDMASTER)
        return static_cast<int32>(GUILD_WITHDRAW_MONEY_UNLIMITED);

    if ((_GetRankRights(rankId) & (GR_RIGHT_WITHDRAW_REPAIR | GR_RIGHT_WITHDRAW_GOLD)) != 0)
    {
        int32 remaining = _GetRankBankMoneyPerDay(rankId) - member.GetBankWithdrawValue(GUILD_BANK_MAX_TABS);
        if (remaining > 0)
            return remaining;
    }
    return 0;
}

inline void Guild::_UpdateMemberWithdrawSlots(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 tabId)
{
    if (Member* member = GetMember(guid))
    {
        uint8 rankId = member->GetRankId();
        if (rankId != GR_GUILDMASTER
            && member->GetBankWithdrawValue(tabId) < _GetRankBankTabSlotsPerDay(rankId, tabId))
            member->UpdateBankWithdrawValue(trans, tabId, 1);
    }
}

inline bool Guild::_MemberHasTabRights(ObjectGuid guid, uint8 tabId, uint32 rights) const
{
    if (Member const* member = GetMember(guid))
    {
        // Leader always has full rights
        if (member->IsRank(GR_GUILDMASTER) || m_leaderGuid == guid)
            return true;
        return (_GetRankBankTabRights(member->GetRankId(), tabId) & rights) == rights;
    }
    return false;
}

// Add new event log record
inline void Guild::_LogEvent(GuildEventLogTypes eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    m_eventLog.AddEvent(trans, m_id, m_eventLog.GetNextGUID(), eventType, playerGuid1, playerGuid2, newRank);
    CharacterDatabase.CommitTransaction(trans);

    sScriptMgr->OnGuildEvent(this, uint8(eventType), playerGuid1, playerGuid2, newRank);
}

// Add new bank event log record
void Guild::_LogBankEvent(CharacterDatabaseTransaction trans, GuildBankEventLogTypes eventType, uint8 tabId, ObjectGuid::LowType lowguid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId)
{
    if (tabId > GUILD_BANK_MAX_TABS)
        return;

    // not logging moves within the same tab
    if (eventType == GUILD_BANK_LOG_MOVE_ITEM && tabId == destTabId)
        return;

    uint8 dbTabId = tabId;
    if (BankEventLogEntry::IsMoneyEvent(eventType))
    {
        tabId = GUILD_BANK_MAX_TABS;
        dbTabId = GUILD_BANK_MONEY_LOGS_TAB;
    }
    LogHolder<BankEventLogEntry>& pLog = m_bankEventLog[tabId];
    pLog.AddEvent(trans, m_id, pLog.GetNextGUID(), eventType, dbTabId, lowguid, itemOrMoney, itemStackCount, destTabId);

    sScriptMgr->OnGuildBankEvent(this, uint8(eventType), tabId, lowguid, itemOrMoney, itemStackCount, destTabId);
}

inline Item* Guild::_GetItem(uint8 tabId, uint8 slotId) const
{
    if (BankTab const* tab = GetBankTab(tabId))
        return tab->GetItem(slotId);
    return nullptr;
}

inline void Guild::_RemoveItem(CharacterDatabaseTransaction trans, uint8 tabId, uint8 slotId)
{
    if (BankTab* pTab = GetBankTab(tabId))
        pTab->SetItem(trans, slotId, nullptr);
}

void Guild::_MoveItems(MoveItemData* pSrc, MoveItemData* pDest, uint32 splitedAmount)
{
    // 1. Initialize source item
    if (!pSrc->InitItem())
        return; // No source item

    // 2. Check source item
    if (!pSrc->CheckItem(splitedAmount))
        return; // Source item or splited amount is invalid

    // 3. Check destination rights
    if (!pDest->HasStoreRights(pSrc))
        return; // Player has no rights to store item in destination

    // 4. Check source withdraw rights
    if (!pSrc->HasWithdrawRights(pDest))
        return; // Player has no rights to withdraw items from source

    // 5. Check split
    if (splitedAmount)
    {
        // 5.1. Clone source item
        if (!pSrc->CloneItem(splitedAmount))
            return; // Item could not be cloned

        // 5.2. Move splited item to destination
        _DoItemsMove(pSrc, pDest, true, splitedAmount);
    }
    else // 6. No split
    {
        // 6.1. Try to merge items in destination (pDest->GetItem() == nullptr)
        if (!_DoItemsMove(pSrc, pDest, false)) // Item could not be merged
        {
            // 6.2. Try to swap items
            // 6.2.1. Initialize destination item
            if (!pDest->InitItem())
                return;

            // 6.2.2. Check rights to store item in source (opposite direction)
            if (!pSrc->HasStoreRights(pDest))
                return; // Player has no rights to store item in source (opposite direction)

            if (!pDest->HasWithdrawRights(pSrc))
                return; // Player has no rights to withdraw item from destination (opposite direction)

            // 6.2.3. Swap items (pDest->GetItem() != nullptr)
            _DoItemsMove(pSrc, pDest, true);
        }
    }
    // 7. Send changes
    _SendBankContentUpdate(pSrc, pDest);
}

bool Guild::_DoItemsMove(MoveItemData* pSrc, MoveItemData* pDest, bool sendError, uint32 splitedAmount)
{
    Item* pDestItem = pDest->GetItem();
    bool swap = (pDestItem != nullptr);

    Item* pSrcItem = pSrc->GetItem(splitedAmount != 0);
    // 1. Can store source item in destination
    if (!pDest->CanStore(pSrcItem, swap, sendError))
        return false;

    // 2. Can store destination item in source
    if (swap)
        if (!pSrc->CanStore(pDestItem, true, true))
            return false;

    // GM LOG (@todo move to scripts)
    pDest->LogAction(pSrc);
    if (swap)
        pSrc->LogAction(pDest);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    // 3. Log bank events
    pDest->LogBankEvent(trans, pSrc, pSrcItem->GetCount());
    if (swap)
        pSrc->LogBankEvent(trans, pDest, pDestItem->GetCount());

    // 4. Remove item from source
    pSrc->RemoveItem(trans, pDest, splitedAmount);

    // 5. Remove item from destination
    if (swap)
        pDest->RemoveItem(trans, pSrc);

    // 6. Store item in destination
    pDest->StoreItem(trans, pSrcItem);

    // 7. Store item in source
    if (swap)
        pSrc->StoreItem(trans, pDestItem);

    CharacterDatabase.CommitTransaction(trans);
    return true;
}

void Guild::_SendBankContent(WorldSession* session, uint8 tabId, bool sendAllSlots) const
{
    ObjectGuid guid = session->GetPlayer()->GetGUID();
    if (!_MemberHasTabRights(guid, tabId, GUILD_BANK_RIGHT_VIEW_TAB))
        return;

    _SendBankList(session, tabId, sendAllSlots);
}

void Guild::_SendBankMoneyUpdate(WorldSession* session) const
{
    _SendBankList(session);
}

void Guild::_SendBankContentUpdate(MoveItemData* pSrc, MoveItemData* pDest) const
{
    ASSERT(pSrc->IsBank() || pDest->IsBank());

    uint8 tabId = 0;
    SlotIds slots;
    if (pSrc->IsBank()) // B ->
    {
        tabId = pSrc->GetContainer();
        slots.insert(pSrc->GetSlotId());
        if (pDest->IsBank()) // B -> B
        {
            // Same tab - add destination slots to collection
            if (pDest->GetContainer() == pSrc->GetContainer())
                pDest->CopySlots(slots);
            else // Different tabs - send second message
            {
                SlotIds destSlots;
                pDest->CopySlots(destSlots);
                _SendBankContentUpdate(pDest->GetContainer(), destSlots);
            }
        }
    }
    else if (pDest->IsBank()) // C -> B
    {
        tabId = pDest->GetContainer();
        pDest->CopySlots(slots);
    }

    _SendBankContentUpdate(tabId, slots);
}

void Guild::_SendBankContentUpdate(uint8 tabId, SlotIds slots) const
{
    _SendBankList(nullptr, tabId, false, &slots);
}

void Guild::_BroadcastEvent(GuildEvents guildEvent, ObjectGuid guid,
    Optional<std::string_view> param1 /*= {}*/, Optional<std::string_view> param2 /*= {}*/, Optional<std::string_view> param3 /*= {}*/) const
{
    WorldPackets::Guild::GuildEvent event;
    event.Type = guildEvent;
    if (param1)
        event.Params.push_back(*param1);

    if (param2)
    {
        event.Params.resize(2);
        event.Params[1] = *param2;
    }

    if (param3)
    {
        event.Params.resize(3);
        event.Params[2] = *param3;
    }

    event.Guid = guid;
    BroadcastPacket(event.Write());

    TC_LOG_DEBUG("guild", "SMSG_GUILD_EVENT [Broadcast] Event: {} ({})", GetGuildEventString(guildEvent), guildEvent);
}

void Guild::_SendBankList(WorldSession* session /* = nullptr*/, uint8 tabId /*= 0*/, bool sendAllSlots /*= false*/, SlotIds *slots /*= nullptr*/) const
{
    WorldPackets::Guild::GuildBankQueryResults packet;

    packet.Money = m_bankMoney;
    packet.Tab = int32(tabId);
    packet.FullUpdate = sendAllSlots;

    if (sendAllSlots && !tabId)
    {
        packet.TabInfo.reserve(_GetPurchasedTabsSize());
        for (uint8 i = 0; i < _GetPurchasedTabsSize(); ++i)
        {
            WorldPackets::Guild::GuildBankTabInfo tabInfo;
            tabInfo.Name = m_bankTabs[i].GetName();
            tabInfo.Icon = m_bankTabs[i].GetIcon();
            packet.TabInfo.push_back(tabInfo);
        }
    }

    if (BankTab const* tab = GetBankTab(tabId))
    {
        auto fillItems = [&](auto begin, auto end, bool skipEmpty)
        {
            for (auto itr = begin; itr != end; ++itr)
            {
                if (Item* tabItem = tab->GetItem(*itr))
                {
                    WorldPackets::Guild::GuildBankItemInfo itemInfo;

                    itemInfo.Slot = *itr;
                    itemInfo.ItemID = tabItem->GetEntry();
                    itemInfo.RandomPropertiesID = tabItem->GetItemRandomPropertyId();
                    itemInfo.RandomPropertiesSeed = tabItem->GetItemSuffixFactor();
                    itemInfo.Count = int32(tabItem->GetCount());
                    itemInfo.Charges = int32(abs(tabItem->GetSpellCharges()));
                    itemInfo.EnchantmentID = int32(tabItem->GetEnchantmentId(PERM_ENCHANTMENT_SLOT));
                    itemInfo.Flags = tabItem->GetInt32Value(ITEM_FIELD_FLAGS);

                    for (uint32 socketSlot = 0; socketSlot < MAX_GEM_SOCKETS; ++socketSlot)
                    {
                        if (uint32 enchId = tabItem->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + socketSlot)))
                        {
                            WorldPackets::Guild::GuildBankSocketEnchant gem;
                            gem.SocketIndex = socketSlot;
                            gem.SocketEnchantID = int32(enchId);
                            itemInfo.SocketEnchant.push_back(gem);
                        }
                    }

                    packet.ItemInfo.push_back(itemInfo);
                }
                else if (!skipEmpty)
                {
                    WorldPackets::Guild::GuildBankItemInfo itemInfo;

                    itemInfo.Slot = *itr;
                    itemInfo.ItemID = 0;

                    packet.ItemInfo.push_back(itemInfo);
                }
            }

        };

        if (sendAllSlots)
            fillItems(boost::make_counting_iterator(uint8(0)), boost::make_counting_iterator(uint8(GUILD_BANK_MAX_SLOTS)), true);
        else if (slots && !slots->empty())
            fillItems(slots->begin(), slots->end(), false);
    }

    if (session)
    {
        if (Member const* member = GetMember(session->GetPlayer()->GetGUID()))
            packet.WithdrawalsRemaining = _GetMemberRemainingSlots(*member, tabId);

        session->SendPacket(packet.Write());
        TC_LOG_DEBUG("guild", "SMSG_GUILD_BANK_LIST [{}]: TabId: {}, FullSlots: {}, slots: {}",
                       session->GetPlayerInfo(), tabId, sendAllSlots, packet.WithdrawalsRemaining);
    }
    else /// @todo - Probably this is just sent to session + those that have sent CMSG_GUILD_BANKER_ACTIVATE
    {
        packet.Write();
        for (auto const& [guid, member] : m_members)
        {
            if (!_MemberHasTabRights(member.GetGUID(), tabId, GUILD_BANK_RIGHT_VIEW_TAB))
                continue;
            Player* player = member.FindPlayer();
            if (!player)
                continue;

            packet.SetWithdrawalsRemaining(_GetMemberRemainingSlots(member, tabId));
            player->SendDirectMessage(packet.GetRawPacket());
            TC_LOG_DEBUG("guild", "SMSG_GUILD_BANK_LIST [{}]: TabId: {}, FullSlots: {}, slots: {}"
                , player->GetName(), tabId, sendAllSlots, packet.WithdrawalsRemaining);
        }
    }
}

void Guild::ResetTimes()
{
    for (auto& [guid, member] : m_members)
        member.ResetValues();

    _BroadcastEvent(GE_BANK_TAB_AND_MONEY_UPDATED, ObjectGuid::Empty);
}
