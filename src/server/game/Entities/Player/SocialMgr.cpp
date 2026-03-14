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
 * @file SocialMgr.cpp
 * @brief 玩家社交系统管理模块实现
 *
 * 本文件实现了玩家社交系统的核心功能，包括：
 * - 社交关系的数据库持久化操作
 * - 好友列表、忽略列表、静音列表的管理
 * - 好友在线状态查询和广播
 * - 社交数据包的构建和发送
 */

#include "SocialMgr.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief PlayerSocial构造函数
 *
 * 初始化玩家社交数据对象，玩家GUID默认为空
 */
PlayerSocial::PlayerSocial(): _playerGUID()
{ }

/**
 * @brief 获取指定类型的社交关系数量
 * @param flag 社交关系标志（好友/忽略/静音）
 * @return 符合指定类型的社交关系数量
 *
 * 遍历社交映射表，统计符合指定标志的社交关系数量。
 * 使用位运算检查关系类型。
 */
uint32 PlayerSocial::GetNumberOfSocialsWithFlag(SocialFlag flag)
{
    uint32 counter = 0;
    // 遍历所有社交关系，统计符合标志的数量
    for (PlayerSocialMap::const_iterator itr = _playerSocialMap.begin(); itr != _playerSocialMap.end(); ++itr)
        if ((itr->second.Flags & flag) != 0)  // 使用位与运算检查是否包含指定标志
            ++counter;

    return counter;
}

/**
 * @brief 添加社交关系
 * @param friendGuid 目标玩家GUID
 * @param flag 社交关系标志
 * @return 添加成功返回true，超过限制返回false
 *
 * 将目标玩家添加到指定的社交列表（好友/忽略/静音）。
 * 处理流程：
 * 1. 检查是否超过客户端限制（好友50人，忽略50人）
 * 2. 如果目标已在社交映射表中，更新标志位
 * 3. 如果目标不在映射表中，创建新条目
 * 4. 同步更新数据库
 */
bool PlayerSocial::AddToSocialList(ObjectGuid const& friendGuid, SocialFlag flag)
{
    // 检查客户端限制：好友列表最多50人，忽略列表最多50人
    if (GetNumberOfSocialsWithFlag(flag) >= (((flag & SOCIAL_FLAG_FRIEND) != 0) ? SOCIALMGR_FRIEND_LIMIT : SOCIALMGR_IGNORE_LIMIT))
        return false;

    // 查找目标玩家是否已在社交映射表中
    PlayerSocialMap::iterator itr = _playerSocialMap.find(friendGuid);
    if (itr != _playerSocialMap.end())
    {
        // 目标已存在，更新标志位（添加新关系）
        itr->second.Flags |= flag;

        // 更新数据库中的社交关系标志
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_SOCIAL_FLAGS);

        stmt->setUInt8(0, itr->second.Flags);
        stmt->setUInt32(1, GetPlayerGUID().GetCounter());
        stmt->setUInt32(2, friendGuid.GetCounter());

        CharacterDatabase.Execute(stmt);
    }
    else
    {
        // 目标不存在，创建新的社交关系条目
        _playerSocialMap[friendGuid].Flags |= flag;

        // 向数据库插入新的社交关系记录
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_SOCIAL);

        stmt->setUInt32(0, GetPlayerGUID().GetCounter());
        stmt->setUInt32(1, friendGuid.GetCounter());
        stmt->setUInt8(2, flag);

        CharacterDatabase.Execute(stmt);
    }

    return true;
}

/**
 * @brief 从社交列表移除
 * @param friendGuid 目标玩家GUID
 * @param flag 要移除的社交关系标志
 *
 * 从指定社交列表中移除目标玩家。
 * 处理流程：
 * 1. 查找目标玩家，不存在则直接返回
 * 2. 使用位运算清除指定标志
 * 3. 如果清除后无任何社交关系，删除整个条目并同步数据库
 * 4. 如果还有其他关系，更新数据库中的标志
 */
void PlayerSocial::RemoveFromSocialList(ObjectGuid const& friendGuid, SocialFlag flag)
{
    // 查找目标玩家
    PlayerSocialMap::iterator itr = _playerSocialMap.find(friendGuid);
    if (itr == _playerSocialMap.end())
        return;

    // 使用位运算清除指定标志（取反后位与）
    itr->second.Flags &= ~flag;

    // 检查是否还有其他社交关系
    if (!itr->second.Flags)
    {
        // 无其他关系，从数据库删除整个条目
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_SOCIAL);

        stmt->setUInt32(0, GetPlayerGUID().GetCounter());
        stmt->setUInt32(1, friendGuid.GetCounter());

        CharacterDatabase.Execute(stmt);

        // 从内存映射表中删除
        _playerSocialMap.erase(itr);
    }
    else
    {
        // 还有其他关系，更新数据库中的标志
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_SOCIAL_FLAGS);

        stmt->setUInt8(0, itr->second.Flags);
        stmt->setUInt32(1, GetPlayerGUID());
        stmt->setUInt32(2, friendGuid);

        CharacterDatabase.Execute(stmt);
    }
}

/**
 * @brief 设置好友备注
 * @param friendGuid 好友玩家GUID
 * @param note 备注字符串
 *
 * 为好友设置备注信息，备注长度限制为48字符（数据库和客户端限制）。
 */
void PlayerSocial::SetFriendNote(ObjectGuid const& friendGuid, std::string const& note)
{
    // 查找好友
    PlayerSocialMap::iterator itr = _playerSocialMap.find(friendGuid);
    if (itr == _playerSocialMap.end())                  // 不存在则返回
        return;

    // 设置备注并截断到48字符（数据库和客户端限制）
    itr->second.Note = note;
    utf8truncate(itr->second.Note, 48);                 // 数据库和客户端大小限制

    // 更新数据库中的备注
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_SOCIAL_NOTE);

    stmt->setString(0, itr->second.Note);
    stmt->setUInt32(1, GetPlayerGUID().GetCounter());
    stmt->setUInt32(2, friendGuid.GetCounter());

    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 发送社交列表到客户端
 * @param player 接收列表的玩家对象
 * @param flags 要发送的列表类型标志位掩码
 *
 * 构建并发送社交列表数据包（SMSG_CONTACT_LIST）。
 * 数据包格式：
 * - uint32: 标志位（0x1=好友列表, 0x2=忽略列表, 0x4=静音列表）
 * - uint32: 联系人数量
 * - 对每个联系人：
 *   - uint64: 玩家GUID
 *   - uint32: 关系标志
 *   - string: 备注
 *   - 如果是好友：
 *     - uint8: 在线状态
 *     - 如果在线：
 *       - uint32: 区域ID
 *       - uint32: 等级
 *       - uint32: 职业
 */
void PlayerSocial::SendSocialList(Player* player, uint32 flags)
{
    ASSERT(player);

    uint32 friendsCount = 0;   // 好友计数器
    uint32 ignoredCount = 0;   // 忽略计数器
    uint32 totalCount = 0;     // 总计数器

    // 构建联系人列表数据包，预估大小为 4 + 4 + 数量 * 25
    WorldPacket data(SMSG_CONTACT_LIST, (4 + 4 + _playerSocialMap.size() * 25));
    data << uint32(flags);                                  // 0x1 = 好友列表更新. 0x2 = 忽略列表更新. 0x4 = 静音列表更新.
    size_t countPos = data.wpos();
    data << uint32(0);                                      // 联系人数量占位符，稍后填充

    // 遍历所有社交关系
    for (auto& v : _playerSocialMap)
    {
        uint8 contactFlags = v.second.Flags;
        // 跳过不符合请求标志的联系人
        if (!(contactFlags & flags))
            continue;

        // 检查好友列表客户端限制
        if (contactFlags & SOCIAL_FLAG_FRIEND)
            if (++friendsCount > SOCIALMGR_FRIEND_LIMIT)
                continue;  // 超过限制的跳过

        // 检查忽略列表客户端限制
        if (contactFlags & SOCIAL_FLAG_IGNORED)
            if (++ignoredCount > SOCIALMGR_IGNORE_LIMIT)
                continue;  // 超过限制的跳过

        ++totalCount;
        // 获取好友详细信息（在线状态、等级、职业、区域等）
        SocialMgr::GetFriendInfo(player, v.first, v.second);

        // 写入联系人数据
        data << uint64(v.first);                            // 玩家GUID
        data << uint32(contactFlags);                       // 关系标志 (0x1 = 好友, 0x2 = 忽略, 0x4 = 静音)
        data << v.second.Note;                              // 备注字符串
        if (contactFlags & SOCIAL_FLAG_FRIEND)              // 如果是好友，发送详细信息
        {
            data << uint8(v.second.Status);                 // 在线/离线等状态
            if (v.second.Status)                            // 如果在线，发送额外信息
            {
                data << uint32(v.second.Area);              // 玩家所在区域
                data << uint32(v.second.Level);             // 玩家等级
                data << uint32(v.second.Class);             // 玩家职业
            }
        }
    }

    // 回填实际的联系人数量
    data.put<uint32>(countPos, totalCount);

    // 发送数据包给玩家
    player->SendDirectMessage(&data);
}

/**
 * @brief 内部函数：检查是否存在指定社交关系
 * @param guid 目标玩家GUID
 * @param flags 社交关系标志位掩码
 * @return 存在指定关系返回true，否则返回false
 */
bool PlayerSocial::_HasContact(ObjectGuid const& guid, SocialFlag flags)
{
    PlayerSocialMap::const_iterator itr = _playerSocialMap.find(guid);
    if (itr != _playerSocialMap.end())
        return (itr->second.Flags & flags) != 0;  // 使用位运算检查是否包含指定标志

    return false;
}

/**
 * @brief 检查是否为好友关系
 * @param friendGuid 目标玩家GUID
 * @return 是好友返回true，否则返回false
 */
bool PlayerSocial::HasFriend(ObjectGuid const& friendGuid)
{
    return _HasContact(friendGuid, SOCIAL_FLAG_FRIEND);
}

/**
 * @brief 检查是否在忽略列表中
 * @param ignoreGuid 目标玩家GUID
 * @return 在忽略列表返回true，否则返回false
 */
bool PlayerSocial::HasIgnore(ObjectGuid const& ignoreGuid)
{
    return _HasContact(ignoreGuid, SOCIAL_FLAG_IGNORED);
}

/**
 * @brief 获取SocialMgr单例实例
 * @return SocialMgr单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
SocialMgr* SocialMgr::instance()
{
    static SocialMgr instance;
    return &instance;
}

/**
 * @brief 获取好友详细信息
 * @param player 查询者玩家对象
 * @param friendGUID 目标好友GUID
 * @param friendInfo [out] 输出的好友信息结构体
 *
 * 获取好友的详细信息，包括在线状态、等级、职业、区域等。
 * 会进行权限检查和可见性判断。
 *
 * 处理流程：
 * 1. 初始化好友信息为离线状态
 * 2. 查找目标玩家是否在线
 * 3. 获取好友备注
 * 4. 权限检查：普通玩家不能查看高等级GM
 * 5. 阵营检查：需要权限才能查看敌对阵营
 * 6. 获取在线状态（在线/AFK/DND）
 * 7. 检查招募好友关系
 * 8. 填充详细信息
 */
void SocialMgr::GetFriendInfo(Player* player, ObjectGuid const& friendGUID, FriendInfo& friendInfo)
{
    if (!player)
        return;

    // 初始化好友信息为默认值（离线状态）
    friendInfo.Status = FRIEND_STATUS_OFFLINE;
    friendInfo.Area = 0;
    friendInfo.Level = 0;
    friendInfo.Class = 0;

    // 查找目标玩家是否在线
    Player* target = ObjectAccessor::FindPlayer(friendGUID);
    if (!target)
        return;

    // 获取好友备注
    PlayerSocial::PlayerSocialMap::iterator itr = player->GetSocial()->_playerSocialMap.find(friendGUID);
    if (itr != player->GetSocial()->_playerSocialMap.end())
        friendInfo.Note = itr->second.Note;

    // 权限检查：普通玩家只能看到其团队和低等级GM角色
    // MODERATOR, GAME MASTER, ADMINISTRATOR 可以看到所有玩家
    // PLAYER 只能看到其团队和低于配置GM等级的角色

    if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS) &&
        target->GetSession()->GetSecurity() > AccountTypes(sWorld->getIntConfig(CONFIG_GM_LEVEL_IN_WHO_LIST)))
        return;  // 无权限查看高等级GM

    // 阵营检查：玩家只能看到同阵营玩家，除非有跨阵营查看权限
    // player can see member of other team only if CONFIG_ALLOW_TWO_SIDE_WHO_LIST
    if (target->GetTeam() != player->GetTeam() && !player->GetSession()->HasPermission(rbac::RBAC_PERM_TWO_SIDE_WHO_LIST))
        return;  // 无权限查看敌对阵营

    // 检查目标对查询者的可见性
    if (target->IsVisibleGloballyFor(player))
    {
        // 确定在线状态：优先DND（请勿打扰），其次AFK（离开），最后在线
        if (target->isDND())
            friendInfo.Status = FRIEND_STATUS_DND;
        else if (target->isAFK())
            friendInfo.Status = FRIEND_STATUS_AFK;
        else
        {
            friendInfo.Status = FRIEND_STATUS_ONLINE;

            // 检查是否为招募好友关系（RaF）
            // 如果目标是查询者的招募者，或者查询者是目标的招募者，则设置RaF标志
            if (target->GetSession()->GetRecruiterId() == player->GetSession()->GetAccountId() || target->GetSession()->GetAccountId() == player->GetSession()->GetRecruiterId())
                friendInfo.Status = FriendStatus(uint32(friendInfo.Status) | FRIEND_STATUS_RAF);
        }

        // 填充详细信息
        friendInfo.Area = target->GetZoneId();     // 区域ID
        friendInfo.Level = target->GetLevel();     // 等级
        friendInfo.Class = target->GetClass();     // 职业
    }
}

/**
 * @brief 发送好友状态通知
 * @param player 玩家对象
 * @param result 操作结果码
 * @param friendGuid 相关好友的GUID
 * @param broadcast 是否广播给好友列表中的玩家，默认false
 *
 * 构建并发送好友状态变更数据包（SMSG_FRIEND_STATUS）。
 * 数据包格式根据操作结果码而不同。
 */
void SocialMgr::SendFriendStatus(Player* player, FriendsResult result, ObjectGuid const& friendGuid, bool broadcast /*= false*/)
{
    // 获取好友详细信息
    FriendInfo fi;
    GetFriendInfo(player, friendGuid, fi);

    // 构建好友状态数据包
    WorldPacket data(SMSG_FRIEND_STATUS, 9);
    data << uint8(result);       // 操作结果码
    data << friendGuid;          // 好友GUID

    // 根据结果码添加备注信息
    switch (result)
    {
        case FRIEND_ADDED_OFFLINE:
        case FRIEND_ADDED_ONLINE:
            data << fi.Note;     // 添加好友时发送备注
            break;
        default:
            break;
    }

    // 根据结果码添加在线详细信息
    switch (result)
    {
        case FRIEND_ADDED_ONLINE:
        case FRIEND_ONLINE:
            data << uint8(fi.Status);   // 在线状态
            data << uint32(fi.Area);    // 区域ID
            data << uint32(fi.Level);   // 等级
            data << uint32(fi.Class);   // 职业
            break;
        default:
            break;
    }

    // 根据broadcast标志决定发送方式
    if (broadcast)
        BroadcastToFriendListers(player, &data);  // 广播给所有好友
    else
        player->SendDirectMessage(&data);         // 仅发送给指定玩家
}

/**
 * @brief 广播数据包给好友列表中的玩家
 * @param player 发送源玩家对象
 * @param packet 要广播的数据包
 *
 * 将数据包发送给所有将此玩家加为好友的在线玩家。
 * 会进行权限和阵营检查，确保发送者对接收者可见。
 *
 * 处理流程：
 * 1. 遍历所有玩家的社交数据
 * 2. 检查是否将发送者加为好友
 * 3. 检查接收者是否有权限查看发送者（GM级别）
 * 4. 检查阵营限制
 * 5. 检查发送者对接收者的可见性
 * 6. 发送数据包
 *
 * @note 性能注意事项：遍历所有玩家的社交数据，复杂度O(n*m)
 */
void SocialMgr::BroadcastToFriendListers(Player* player, WorldPacket const* packet)
{
    ASSERT(player);

    // 获取GM可见性配置等级
    AccountTypes gmSecLevel = AccountTypes(sWorld->getIntConfig(CONFIG_GM_LEVEL_IN_WHO_LIST));

    // 遍历所有玩家的社交数据
    for (SocialMap::const_iterator itr = _socialMap.begin(); itr != _socialMap.end(); ++itr)
    {
        // 检查该玩家是否将发送者加为好友
        PlayerSocial::PlayerSocialMap::const_iterator itr2 = itr->second._playerSocialMap.find(player->GetGUID());
        if (itr2 != itr->second._playerSocialMap.end() && (itr2->second.Flags & SOCIAL_FLAG_FRIEND) != 0)
        {
            // 查找目标玩家是否在线
            Player* target = ObjectAccessor::FindPlayer(itr->first);
            if (!target)
                continue;  // 目标不在线，跳过

            WorldSession* session = target->GetSession();

            // 权限检查：普通玩家不能查看高等级GM
            if (!session->HasPermission(rbac::RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS) && player->GetSession()->GetSecurity() > gmSecLevel)
                continue;  // 无权限查看，跳过

            // 阵营检查：需要权限才能查看敌对阵营
            if (target->GetTeam() != player->GetTeam() && !session->HasPermission(rbac::RBAC_PERM_TWO_SIDE_WHO_LIST))
                continue;  // 无权限查看敌对阵营，跳过

            // 检查发送者对接收者的可见性
            if (player->IsVisibleGloballyFor(target))
                session->SendPacket(packet);  // 发送数据包
        }
    }
}

/**
 * @brief 从数据库加载玩家社交数据
 * @param result 数据库查询结果
 * @param guid 玩家GUID
 * @return 加载的PlayerSocial对象指针
 *
 * 从数据库加载玩家的社交关系数据。
 * 数据格式：好友GUID, 标志位, 备注字符串
 *
 * @note 调用时机：玩家登录加载角色数据时
 */
PlayerSocial* SocialMgr::LoadFromDB(PreparedQueryResult result, ObjectGuid const& guid)
{
    // 获取或创建玩家的社交数据对象
    PlayerSocial* social = &_socialMap[guid];
    social->SetPlayerGUID(guid);

    // 如果有查询结果，遍历加载每条社交关系
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            // 创建好友GUID
            ObjectGuid friendGuid = ObjectGuid::Create<HighGuid::Player>(fields[0].GetUInt32());

            // 读取社交关系标志和备注
            uint8 flag = fields[1].GetUInt8();
            social->_playerSocialMap[friendGuid] = FriendInfo(flag, fields[2].GetString());
        }
        while (result->NextRow());
    }

    return social;
}
