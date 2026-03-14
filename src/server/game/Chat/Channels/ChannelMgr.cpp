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
 * @file ChannelMgr.cpp
 * @brief 聊天频道管理器实现文件
 *
 * 本文件实现了 ChannelMgr 类的所有功能，包括：
 * - 频道管理器的创建和销毁
 * - 从数据库加载和保存频道数据
 * - 内置频道和自定义频道的创建与查找
 * - 按阵营管理频道
 * - 频道名称的部分匹配查找
 *
 * 关键实现：
 * - 使用静态变量存储阵营频道管理器实例
 * - 自定义频道名称使用宽字符支持多语言
 * - 支持跨阵营频道交互（可配置）
 */

#include "ChannelMgr.h"
#include "Channel.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

/**
 * @brief 析构函数实现
 *
 * 清理所有频道实例，释放内存。
 * 遍历内置频道和自定义频道容器，逐个删除频道对象。
 */
ChannelMgr::~ChannelMgr()
{
    // 清理内置频道
    for (auto itr = _channels.begin(); itr != _channels.end(); ++itr)
        delete itr->second;

    // 清理自定义频道
    for (auto itr = _customChannels.begin(); itr != _customChannels.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 从数据库加载自定义频道
 *
 * 在服务器启动时调用，从 channels 表加载所有自定义频道。
 * 加载的数据包括：
 * - 频道名称和阵营
 * - 公告和所有权设置
 * - 密码和封禁列表
 *
 * 会自动清理过期的频道（根据配置的保存时长）。
 * 对于无效的频道名称或阵营，会从数据库中删除。
 */
/*static*/ void ChannelMgr::LoadFromDB()
{
    // 检查是否启用了自定义频道保存
    if (!sWorld->getBoolConfig(CONFIG_PRESERVE_CUSTOM_CHANNELS))
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 custom chat channels. Custom channel saving is disabled.");
        return;
    }

    uint32 oldMSTime = getMSTime();

    // 清理过期的频道
    if (uint32 days = sWorld->getIntConfig(CONFIG_PRESERVE_CUSTOM_CHANNEL_DURATION))
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_OLD_CHANNELS);
        stmt->setUInt32(0, days * DAY);
        CharacterDatabase.Execute(stmt);
    }

    // 查询所有频道数据
    QueryResult result = CharacterDatabase.Query("SELECT name, team, announce, ownership, password, bannedList FROM channels");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 custom chat channels. DB table `channels` is empty.");
        return;
    }

    std::vector<std::pair<std::string, uint32>> toDelete;  // 需要删除的频道列表
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        std::string dbName = fields[0].GetString();
        uint32 team = fields[1].GetUInt32();
        bool dbAnnounce = fields[2].GetBool();
        bool dbOwnership = fields[3].GetBool();
        std::string dbPass = fields[4].GetString();
        std::string dbBanned = fields[5].GetString();

        // 转换频道名称为宽字符（支持大小写不敏感）
        std::wstring channelName;
        if (!Utf8toWStr(dbName, channelName))
        {
            TC_LOG_ERROR("server.loading", "Failed to load custom chat channel '{}' from database - invalid utf8 sequence? Deleted.", dbName);
            toDelete.push_back({ dbName, team });
            continue;
        }

        // 获取对应阵营的频道管理器
        ChannelMgr* mgr = forTeam(team);
        if (!mgr)
        {
            TC_LOG_ERROR("server.loading", "Failed to load custom chat channel '{}' from database - invalid team {}. Deleted.", dbName, team);
            toDelete.push_back({ dbName, team });
            continue;
        }

        // 创建频道并设置属性
        Channel* channel = new Channel(dbName, team, dbBanned);
        channel->SetAnnounce(dbAnnounce);
        channel->SetOwnership(dbOwnership);
        channel->SetPassword(dbPass);
        mgr->_customChannels.emplace(channelName, channel);

        ++count;
    } while (result->NextRow());

    // 删除无效的频道记录
    for (auto pair : toDelete)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHANNEL);
        stmt->setString(0, pair.first);
        stmt->setUInt32(1, pair.second);
        CharacterDatabase.Execute(stmt);
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} custom chat channels in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取指定阵营的频道管理器
 *
 * 返回对应阵营的频道管理器实例。
 * 使用静态变量存储阵营频道管理器，确保全局唯一。
 *
 * 如果配置了跨阵营频道交互，则所有阵营都使用联盟频道管理器。
 *
 * @param team 阵营（ALLIANCE 或 HORDE）
 * @return 频道管理器指针，如果阵营无效则返回 nullptr
 */
/*static*/ ChannelMgr* ChannelMgr::forTeam(uint32 team)
{
    // 静态实例，全局唯一
    static ChannelMgr allianceChannelMgr(ALLIANCE);
    static ChannelMgr hordeChannelMgr(HORDE);

    // 如果配置了跨阵营频道交互，返回联盟频道管理器
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
        return &allianceChannelMgr;        // 跨阵营交互

    // 根据阵营返回对应的管理器
    if (team == ALLIANCE)
        return &allianceChannelMgr;

    if (team == HORDE)
        return &hordeChannelMgr;

    return nullptr;
}

/**
 * @brief 根据名称部分匹配查找玩家所在的频道
 *
 * 在玩家已加入的频道列表中查找名称匹配的频道。
 * 名称匹配不区分大小写。
 *
 * @param namePart 频道名称部分（不区分大小写）
 * @param playerSearcher 查找玩家（用于获取已加入的频道列表）
 * @return 匹配的频道指针，如果没有匹配则返回 nullptr
 */
Channel* ChannelMgr::GetChannelForPlayerByNamePart(std::string const& namePart, Player* playerSearcher)
{
    // 转换为宽字符并转小写
    std::wstring channelNamePart;
    if (!Utf8toWStr(namePart, channelNamePart))
        return nullptr;

    wstrToLower(channelNamePart);

    // 遍历玩家已加入的频道
    for (Channel* channel : playerSearcher->GetJoinedChannels())
    {
        std::string chanName = channel->GetName(playerSearcher->GetSession()->GetSessionDbcLocale());

        std::wstring channelNameW;
        if (!Utf8toWStr(chanName, channelNameW))
            continue;

        wstrToLower(channelNameW);

        // 检查是否匹配（前缀匹配）
        if (!channelNameW.compare(0, channelNamePart.size(), channelNamePart))
            return channel;
    }

    return nullptr;
}

/**
 * @brief 保存所有自定义频道到数据库
 *
 * 遍历所有自定义频道，调用 UpdateChannelInDB 方法保存数据。
 * 定期调用以确保持久化频道设置和活动状态。
 */
void ChannelMgr::SaveToDB()
{
    for (auto pair : _customChannels)
        pair.second->UpdateChannelInDB();
}

/**
 * @brief 获取或创建系统频道
 *
 * 根据频道 ID 和区域信息获取系统频道。
 * 如果频道不存在则自动创建。
 *
 * 对于全局频道和仅城市频道，区域 ID 会被设置为 0。
 *
 * @param channelId 频道 ID（来自 ChatChannels.dbc）
 * @param zoneEntry 区域信息，用于区域相关频道，默认为 nullptr
 * @return 系统频道指针
 */
Channel* ChannelMgr::GetSystemChannel(uint32 channelId, AreaTableEntry const* zoneEntry)
{
    ChatChannelsEntry const* channelEntry = sChatChannelsStore.AssertEntry(channelId);
    uint32 zoneId = zoneEntry ? zoneEntry->ID : 0;

    // 全局频道和仅城市频道不使用区域 ID
    if (channelEntry->Flags & (CHANNEL_DBC_FLAG_GLOBAL | CHANNEL_DBC_FLAG_CITY_ONLY))
        zoneId = 0;

    // 构建查找键
    std::pair<uint32, uint32> key = std::make_pair(channelId, zoneId);

    // 查找现有频道
    auto itr = _channels.find(key);
    if (itr != _channels.end())
        return itr->second;

    // 创建新频道
    Channel* newChannel = new Channel(channelId, _team, zoneEntry);
    _channels[key] = newChannel;
    return newChannel;
}

/**
 * @brief 创建自定义频道
 *
 * 创建一个新的自定义频道。
 * 如果频道已存在，则返回 nullptr。
 *
 * @param name 频道名称
 * @return 新创建的频道指针，如果频道已存在则返回 nullptr
 */
Channel* ChannelMgr::CreateCustomChannel(std::string const& name)
{
    // 转换为宽字符并转小写（用于大小写不敏感查找）
    std::wstring channelName;
    if (!Utf8toWStr(name, channelName))
        return nullptr;

    wstrToLower(channelName);

    // 检查频道是否已存在
    Channel*& c = _customChannels[channelName];
    if (c)
        return nullptr;

    // 创建新频道并标记为需要保存
    Channel* newChannel = new Channel(name, _team);
    newChannel->SetDirty();

    c = newChannel;
    return newChannel;
}

/**
 * @brief 获取自定义频道
 *
 * 根据名称查找自定义频道，不区分大小写。
 *
 * @param name 频道名称（不区分大小写）
 * @return 频道指针，如果不存在则返回 nullptr
 */
Channel* ChannelMgr::GetCustomChannel(std::string const& name) const
{
    // 转换为宽字符并转小写
    std::wstring channelName;
    if (!Utf8toWStr(name, channelName))
        return nullptr;

    wstrToLower(channelName);

    // 查找频道
    auto itr = _customChannels.find(channelName);
    if (itr != _customChannels.end())
        return itr->second;

    return nullptr;
}

/**
 * @brief 获取频道（通用接口）
 *
 * 根据频道类型（内置或自定义）查找频道。
 * 如果频道不存在且 pkt 为 true，则发送错误消息给玩家。
 *
 * @param channelId 频道 ID，0 表示自定义频道
 * @param name 频道名称（仅对自定义频道使用）
 * @param player 请求玩家（用于发送错误消息）
 * @param pkt 是否发送错误消息，默认为 true
 * @param zoneEntry 区域信息（仅对内置频道使用），默认为 nullptr
 * @return 频道指针，如果不存在则返回 nullptr
 */
Channel* ChannelMgr::GetChannel(uint32 channelId, std::string const& name, Player* player, bool pkt /*= true*/, AreaTableEntry const* zoneEntry /*= nullptr*/) const
{
    Channel* ret = nullptr;
    bool send = false;

    if (channelId) // 内置频道
    {
        ChatChannelsEntry const* channelEntry = sChatChannelsStore.AssertEntry(channelId);
        uint32 zoneId = zoneEntry ? zoneEntry->ID : 0;

        // 全局频道和仅城市频道不使用区域 ID
        if (channelEntry->Flags & (CHANNEL_DBC_FLAG_GLOBAL | CHANNEL_DBC_FLAG_CITY_ONLY))
            zoneId = 0;

        // 构建查找键并查找频道
        std::pair<uint32, uint32> key = std::make_pair(channelId, zoneId);

        auto itr = _channels.find(key);
        if (itr != _channels.end())
            ret = itr->second;
        else
            send = true;  // 频道不存在，标记需要发送错误消息
    }
    else // 自定义频道
    {
        // 转换为宽字符并转小写
        std::wstring channelName;
        if (!Utf8toWStr(name, channelName))
            return nullptr;

        wstrToLower(channelName);

        // 查找频道
        auto itr = _customChannels.find(channelName);
        if (itr != _customChannels.end())
            ret = itr->second;
        else
            send = true;  // 频道不存在，标记需要发送错误消息
    }

    // 发送错误消息
    if (send && pkt)
    {
        std::string channelName = name;
        Channel::GetChannelName(channelName, channelId, player->GetSession()->GetSessionDbcLocale(), zoneEntry);

        WorldPacket data;
        ChannelMgr::MakeNotOnPacket(&data, channelName);
        player->SendDirectMessage(&data);
    }

    return ret;
}

/**
 * @brief 处理频道离开后的清理
 *
 * 检查内置频道是否还有成员，如果没有则销毁频道。
 * 自定义频道不会被销毁（会持久化到数据库）。
 *
 * @param channelId 频道 ID
 * @param zoneEntry 区域信息
 */
void ChannelMgr::LeftChannel(uint32 channelId, AreaTableEntry const* zoneEntry)
{
    ChatChannelsEntry const* channelEntry = sChatChannelsStore.AssertEntry(channelId);
    uint32 zoneId = zoneEntry ? zoneEntry->ID : 0;

    // 全局频道和仅城市频道不使用区域 ID
    if (channelEntry->Flags & (CHANNEL_DBC_FLAG_GLOBAL | CHANNEL_DBC_FLAG_CITY_ONLY))
        zoneId = 0;

    // 构建查找键
    std::pair<uint32, uint32> key = std::make_pair(channelId, zoneId);

    // 查找频道
    auto itr = _channels.find(key);
    if (itr == _channels.end())
        return;

    // 如果频道中没有玩家，销毁频道
    Channel* channel = itr->second;
    if (!channel->GetNumPlayers())
    {
        _channels.erase(itr);
        delete channel;
    }
}

/**
 * @brief 构建"不在频道中"数据包
 *
 * 构建一个通知数据包，告诉玩家他们不在指定频道中。
 *
 * @param data 输出数据包
 * @param name 频道名称
 */
void ChannelMgr::MakeNotOnPacket(WorldPacket* data, std::string const& name)
{
    data->Initialize(SMSG_CHANNEL_NOTIFY, 1 + name.size());
    (*data) << uint8(CHAT_NOT_MEMBER_NOTICE) << name;
}
