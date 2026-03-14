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
 * @file GuildPackets.cpp
 * @brief 公会系统网络包实现文件
 *
 * 本文件实现了公会系统相关的所有网络包的序列化和反序列化功能。
 * 主要功能模块包括：
 * - 公会查询和创建
 * - 成员管理（邀请、踢出、晋升、降级）
 * - 公会银行操作（存取款、物品管理）
 * - 日志查询
 * - 权限管理
 * - 徽章设置
 *
 * 实现要点：
 * - Read()方法：从网络包读取客户端发送的数据
 * - Write()方法：将服务器数据写入网络包发送给客户端
 * - 使用ByteBuffer的<<和>>运算符进行数据序列化
 */

#include "GuildPackets.h"

/**
 * @brief 读取公会查询请求数据
 *
 * 从客户端网络包中读取要查询的公会ID。
 */
void WorldPackets::Guild::QueryGuildInfo::Read()
{
    _worldPacket >> GuildId;
}

/**
 * @brief 构造函数，初始化为公会查询响应包
 */
WorldPackets::Guild::QueryGuildInfoResponse::QueryGuildInfoResponse()
    : ServerPacket(SMSG_GUILD_QUERY_RESPONSE) { }

/**
 * @brief 序列化公会查询响应数据
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 公会ID
 * 2. 公会名称
 * 3. 所有等级名称（按等级顺序）
 * 4. 徽章样式信息（图案样式、颜色、边框、背景）
 * 5. 等级数量
 */
WorldPacket const* WorldPackets::Guild::QueryGuildInfoResponse::Write()
{
    _worldPacket << GuildId;
    _worldPacket << Info.GuildName;

    // 写入所有等级名称
    for (std::string const& rankName : Info.Ranks)
        _worldPacket << rankName;

    // 写入徽章样式信息
    _worldPacket << uint32(Info.EmblemStyle);
    _worldPacket << uint32(Info.EmblemColor);
    _worldPacket << uint32(Info.BorderStyle);
    _worldPacket << uint32(Info.BorderColor);
    _worldPacket << uint32(Info.BackgroundColor);
    _worldPacket << uint32(Info.RankCount);

    return &_worldPacket;
}

/**
 * @brief 读取公会创建请求数据
 *
 * 从客户端网络包中读取要创建的公会名称。
 */
void WorldPackets::Guild::GuildCreate::Read()
{
    _worldPacket >> GuildName;
}

/**
 * @brief 序列化公会基础信息响应数据
 * @return 返回构建好的网络包指针
 *
 * 写入公会名称、创建日期、成员数量和账户数量。
 */
WorldPacket const* WorldPackets::Guild::GuildInfoResponse::Write()
{
    _worldPacket << GuildName;
    _worldPacket << CreateDate;
    _worldPacket << int32(NumMembers);
    _worldPacket << int32(NumAccounts);

    return &_worldPacket;
}

/**
 * @brief 序列化公会花名册数据
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 成员数量
 * 2. 欢迎文本（MOTD）
 * 3. 公会信息文本
 * 4. 等级数量
 * 5. 等级数据列表（使用序列化运算符）
 * 6. 成员数据列表（使用序列化运算符）
 */
WorldPacket const* WorldPackets::Guild::GuildRoster::Write()
{
    _worldPacket << uint32(MemberData.size());
    _worldPacket << WelcomeText;
    _worldPacket << InfoText;
    _worldPacket << uint32(RankData.size());

    // 写入所有等级数据
    for (GuildRankData const& rank : RankData)
        _worldPacket << rank;

    // 写入所有成员数据
    for (GuildRosterMemberData const& member : MemberData)
        _worldPacket << member;

    return &_worldPacket;
}

/**
 * @brief 读取公会每日消息（MOTD）更新数据
 *
 * 从客户端网络包中读取新的MOTD文本。
 */
void WorldPackets::Guild::GuildUpdateMotdText::Read()
{
    _worldPacket >> MotdText;
}

/**
 * @brief 序列化公会命令执行结果
 * @return 返回构建好的网络包指针
 *
 * 写入命令类型、相关名称和执行结果码。
 */
WorldPacket const* WorldPackets::Guild::GuildCommandResult::Write()
{
    _worldPacket << int32(Command);
    _worldPacket << Name;
    _worldPacket << int32(Result);

    return &_worldPacket;
}

/**
 * @brief 读取公会邀请请求数据
 *
 * 从客户端网络包中读取被邀请玩家的名称。
 */
void WorldPackets::Guild::GuildInviteByName::Read()
{
    _worldPacket >> Name;
}

/**
 * @brief 序列化公会邀请通知数据
 * @return 返回构建好的网络包指针
 *
 * 写入邀请者名称和公会名称。
 */
WorldPacket const* WorldPackets::Guild::GuildInvite::Write()
{
    _worldPacket << InviterName;
    _worldPacket << GuildName;

    return &_worldPacket;
}

/**
 * @brief 序列化公会成员数据到字节缓冲区
 * @param data 字节缓冲区引用
 * @param rosterMemberData 公会成员数据
 * @return 返回字节缓冲区引用
 *
 * 序列化顺序：
 * 1. 成员GUID
 * 2. 在线状态
 * 3. 角色名称
 * 4. 公会等级ID
 * 5. 角色等级
 * 6. 职业ID
 * 7. 性别
 * 8. 当前区域ID
 * 9. 上次保存时间（仅当离线时）
 * 10. 成员备注
 * 11. 官员备注
 *
 * 注意：离线成员需要额外写入上次保存时间字段。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Guild::GuildRosterMemberData const& rosterMemberData)
{
    data << rosterMemberData.Guid;
    data << uint8(rosterMemberData.Status);
    data << rosterMemberData.Name;
    data << int32(rosterMemberData.RankID);
    data << uint8(rosterMemberData.Level);
    data << uint8(rosterMemberData.ClassID);
    data << uint8(rosterMemberData.Gender);
    data << int32(rosterMemberData.AreaID);

    // 仅当成员离线时（Status=0）才写入上次保存时间
    if (!rosterMemberData.Status)
        data << float(rosterMemberData.LastSave);

    data << rosterMemberData.Note;
    data << rosterMemberData.OfficerNote;

    return data;
}

/**
 * @brief 序列化公会事件数据
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 事件类型
 * 2. 参数数量
 * 3. 参数字符串列表
 * 4. 玩家GUID（仅特定事件类型）
 *
 * 特定事件（加入、离开、上线、下线）需要额外写入玩家GUID，
 * 以便客户端正确显示事件信息。
 */
WorldPacket const* WorldPackets::Guild::GuildEvent::Write()
{
    _worldPacket << uint8(Type);
    _worldPacket << uint8(Params.size());

    // 写入所有参数字符串
    for (std::string_view param : Params)
        _worldPacket << param;

    // 根据事件类型决定是否写入玩家GUID
    switch (Type)
    {
        case GE_JOINED:        // 成员加入
        case GE_LEFT:          // 成员离开
        case GE_SIGNED_ON:     // 成员上线
        case GE_SIGNED_OFF:    // 成员下线
            _worldPacket << Guid;
            break;
        default:
            break;
    }

    return &_worldPacket;
}

/**
 * @brief 序列化公会事件日志查询结果
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 日志条目数量
 * 2. 每个条目的详细数据：
 *    - 事件类型
 *    - 玩家GUID
 *    - 其他玩家GUID（非加入/离开事件）
 *    - 等级ID（仅晋升/降级事件）
 *    - 事件时间戳
 *
 * 注意：预分配缓冲区空间以提高性能。
 */
WorldPacket const* WorldPackets::Guild::GuildEventLogQueryResults::Write()
{
    // 预分配缓冲区空间，避免多次重新分配
    _worldPacket.reserve(1 + Entry.size() * sizeof(GuildEventEntry));

    _worldPacket << uint8(Entry.size());

    // 遍历所有事件条目并写入数据
    for (GuildEventEntry const& entry : Entry)
    {
        _worldPacket << uint8(entry.TransactionType);
        _worldPacket << entry.PlayerGUID;

        // 加入和离开事件不需要其他玩家GUID
        if (entry.TransactionType != GUILD_EVENT_LOG_JOIN_GUILD && entry.TransactionType != GUILD_EVENT_LOG_LEAVE_GUILD)
            _worldPacket << entry.OtherGUID;

        // 晋升和降级事件需要写入新等级ID
        if (entry.TransactionType == GUILD_EVENT_LOG_PROMOTE_PLAYER || entry.TransactionType == GUILD_EVENT_LOG_DEMOTE_PLAYER)
            _worldPacket << uint8(entry.RankID);

        _worldPacket << uint32(entry.TransactionDate);
    }

    return &_worldPacket;
}

/**
 * @brief 序列化公会权限查询结果
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 等级ID
 * 2. 总体权限标志
 * 3. 金币提取上限
 * 4. 银行标签页总数
 * 5. 每个标签页的权限设置（权限标志和物品提取上限）
 */
WorldPacket const* WorldPackets::Guild::GuildPermissionsQueryResults::Write()
{
    _worldPacket << uint32(RankID);
    _worldPacket << int32(Flags);
    _worldPacket << int32(WithdrawGoldLimit);
    _worldPacket << int8(NumTabs);

    // 写入每个银行标签页的权限设置
    for (GuildRankTabPermissions const& tab : Tab)
    {
        _worldPacket << int32(tab.Flags);
        _worldPacket << int32(tab.WithdrawItemLimit);
    }

    return &_worldPacket;
}

/**
 * @brief 读取公会等级权限设置数据
 *
 * 从客户端网络包中读取等级权限设置的所有数据。
 * 包括等级ID、权限标志、等级名称、金币限制和每个银行标签页的权限。
 */
void WorldPackets::Guild::GuildSetRankPermissions::Read()
{
    _worldPacket >> RankID;
    _worldPacket >> Flags;
    _worldPacket >> RankName;
    _worldPacket >> WithdrawGoldLimit;

    // 读取每个银行标签页的权限设置
    for (uint8 i = 0; i < GUILD_BANK_MAX_TABS; i++)
    {
        _worldPacket >> TabFlags[i];
        _worldPacket >> TabWithdrawItemLimit[i];
    }
}

/**
 * @brief 序列化公会等级数据到字节缓冲区
 * @param data 字节缓冲区引用
 * @param rankData 公会等级数据
 * @return 返回字节缓冲区引用
 *
 * 序列化顺序：
 * 1. 等级权限标志
 * 2. 金币提取上限
 * 3. 每个银行标签页的权限标志和物品提取上限
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Guild::GuildRankData const& rankData)
{
    data << uint32(rankData.Flags);
    data << uint32(rankData.WithdrawGoldLimit);

    // 写入每个银行标签页的权限设置
    for (uint8 i = 0; i < GUILD_BANK_MAX_TABS; i++)
    {
        data << uint32(rankData.TabFlags[i]);
        data << uint32(rankData.TabWithdrawItemLimit[i]);
    }

    return data;
}

/**
 * @brief 读取添加公会等级请求数据
 *
 * 从客户端网络包中读取新等级的名称。
 */
void WorldPackets::Guild::GuildAddRank::Read()
{
    _worldPacket >> Name;
}

/**
 * @brief 读取更新公会信息文本请求数据
 *
 * 从客户端网络包中读取新的公会信息文本。
 */
void WorldPackets::Guild::GuildUpdateInfoText::Read()
{
    _worldPacket >> InfoText;
}

/**
 * @brief 读取设置成员备注请求数据
 *
 * 从客户端网络包中读取目标成员名称和备注文本。
 */
void WorldPackets::Guild::GuildSetMemberNote::Read()
{
    _worldPacket >> NoteeName;
    _worldPacket >> Note;
}

/**
 * @brief 读取降级成员请求数据
 *
 * 从客户端网络包中读取要降级的成员名称。
 */
void WorldPackets::Guild::GuildDemoteMember::Read()
{
    _worldPacket >> Demotee;
}

/**
 * @brief 读取晋升成员请求数据
 *
 * 从客户端网络包中读取要晋升的成员名称。
 */
void WorldPackets::Guild::GuildPromoteMember::Read()
{
    _worldPacket >> Promotee;
}

/**
 * @brief 读取踢出成员请求数据
 *
 * 从客户端网络包中读取要踢出的成员名称。
 */
void WorldPackets::Guild::GuildOfficerRemoveMember::Read()
{
    _worldPacket >> Removee;
}

/**
 * @brief 读取激活公会银行请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID和完整更新标志。
 */
void WorldPackets::Guild::GuildBankActivate::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> FullUpdate;
}

/**
 * @brief 读取购买银行标签页请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID和要购买的标签页编号。
 */
void WorldPackets::Guild::GuildBankBuyTab::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> BankTab;
}

/**
 * @brief 读取更新银行标签页请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID、标签页编号、名称和图标。
 */
void WorldPackets::Guild::GuildBankUpdateTab::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> BankTab;
    _worldPacket >> Name;
    _worldPacket >> Icon;
}

/**
 * @brief 读取存入金币请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID和存入金额。
 */
void WorldPackets::Guild::GuildBankDepositMoney::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> Money;
}

/**
 * @brief 读取查询银行标签页请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID、标签页编号和完整更新标志。
 */
void WorldPackets::Guild::GuildBankQueryTab::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> Tab;
    _worldPacket >> FullUpdate;
}

/**
 * @brief 序列化剩余金币提取额度
 * @return 返回构建好的网络包指针
 *
 * 写入玩家今日剩余可提取的金币数量。
 */
WorldPacket const* WorldPackets::Guild::GuildBankRemainingWithdrawMoney::Write()
{
    _worldPacket << RemainingWithdrawMoney;

    return &_worldPacket;
}

/**
 * @brief 读取取出金币请求数据
 *
 * 从客户端网络包中读取银行NPC的GUID和取出金额。
 */
void WorldPackets::Guild::GuildBankWithdrawMoney::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> Money;
}

/**
 * @brief 序列化银行查询结果数据
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 公会银行金币总数
 * 2. 当前标签页编号
 * 3. 剩余提取次数（保存位置用于后续更新）
 * 4. 完整更新标志
 * 5. 标签页信息列表（仅首次打开且标签页0）
 * 6. 物品信息列表
 *
 * 物品信息包括：
 * - 槽位编号、物品ID
 * - 物品标志、随机属性
 * - 数量、附魔、使用次数
 * - 宝石镶嵌信息
 *
 * 注意：空槽位只写入槽位编号和物品ID（0）。
 */
WorldPacket const* WorldPackets::Guild::GuildBankQueryResults::Write()
{
    _worldPacket << uint64(Money);
    _worldPacket << uint8(Tab);

    // 保存剩余提取次数字段的位置，用于后续动态更新
    _withdrawalsRemainingPos = _worldPacket.wpos();
    _worldPacket << int32(WithdrawalsRemaining);
    _worldPacket << uint8(FullUpdate);

    // 仅在首次打开且标签页为0时写入所有标签页信息
    if (!Tab && FullUpdate)
    {
        _worldPacket << uint8(TabInfo.size());
        for (GuildBankTabInfo const& tab : TabInfo)
        {
            _worldPacket << tab.Name;
            _worldPacket << tab.Icon;
        }
    }

    // 写入物品信息列表
    _worldPacket << uint8(ItemInfo.size());
    for (GuildBankItemInfo const& item : ItemInfo)
    {
        _worldPacket << uint8(item.Slot);
        _worldPacket << uint32(item.ItemID);

        // 仅当物品ID不为0时写入详细信息
        if (item.ItemID)
        {
            _worldPacket << int32(item.Flags);
            _worldPacket << int32(item.RandomPropertiesID);

            // 仅当有随机属性时写入随机属性种子
            if (item.RandomPropertiesID)
                _worldPacket << int32(item.RandomPropertiesSeed);

            _worldPacket << int32(item.Count);
            _worldPacket << int32(item.EnchantmentID);
            _worldPacket << uint8(item.Charges);
            _worldPacket << uint8(item.SocketEnchant.size());

            // 写入宝石镶嵌信息
            for (GuildBankSocketEnchant const& socketEnchant : item.SocketEnchant)
            {
                _worldPacket << uint8(socketEnchant.SocketIndex);
                _worldPacket << int32(socketEnchant.SocketEnchantID);
            }
        }
    }

    return &_worldPacket;
}

/**
 * @brief 设置剩余提取次数
 * @param withdrawalsRemaining 剩余提取次数
 *
 * 此方法用于动态更新已写入网络包中的剩余提取次数字段。
 * 通过保存的字段位置直接修改包中的数据，而不需要重新构建整个包。
 *
 * 性能优势：避免重新序列化整个包，提高效率。
 */
void WorldPackets::Guild::GuildBankQueryResults::SetWithdrawalsRemaining(int32 withdrawalsRemaining)
{
    WithdrawalsRemaining = withdrawalsRemaining;
    // 直接在保存的位置更新数据
    _worldPacket.put<int32>(_withdrawalsRemainingPos, withdrawalsRemaining);
}

/**
 * @brief 读取银行物品交换请求数据
 *
 * 根据操作类型读取不同的数据结构：
 *
 * 情况1：BankOnly=true（银行内物品移动）
 *   - 目标标签页、槽位、物品ID
 *   - 源标签页、槽位、物品ID
 *   - 自动存储标志、物品数量
 *
 * 情况2：BankOnly=false（玩家与银行交换）
 *   - 银行标签页、槽位、物品ID
 *   - 自动存储标志
 *   - 如AutoStore=true：物品数量、目标槽位、堆叠数量
 *   - 如AutoStore=false：容器槽位、容器物品槽位、目标槽位、堆叠数量
 *
 * 注意：数据结构复杂，需要根据标志位判断读取顺序。
 */
void WorldPackets::Guild::GuildBankSwapItems::Read()
{
    _worldPacket >> Banker;
    _worldPacket >> BankOnly;

    if (BankOnly)
    {
        // 银行内物品移动：读取目标和源位置信息
        // 目标位置
        _worldPacket >> BankTab;
        _worldPacket >> BankSlot;
        _worldPacket >> ItemID;

        // 源位置
        _worldPacket >> BankTab1;
        _worldPacket >> BankSlot1;
        _worldPacket >> ItemID1;

        _worldPacket >> AutoStore;
        _worldPacket >> BankItemCount;
    }
    else
    {
        // 玩家与银行之间的物品交换
        _worldPacket >> BankTab;
        _worldPacket >> BankSlot;
        _worldPacket >> ItemID;

        _worldPacket >> AutoStore;
        if (AutoStore)
        {
            // 自动存储模式：从银行自动存入玩家背包
            _worldPacket >> BankItemCount;
            _worldPacket >> ToSlot;
            _worldPacket >> StackCount;
        }
        else
        {
            // 手动模式：指定玩家背包中的具体位置
            _worldPacket >> ContainerSlot;
            _worldPacket >> ContainerItemSlot;
            _worldPacket >> ToSlot;
            _worldPacket >> StackCount;
        }
    }
}

/**
 * @brief 读取银行日志查询请求数据
 *
 * 从客户端网络包中读取要查询的标签页编号。
 * Tab=255表示查询金币日志。
 */
void WorldPackets::Guild::GuildBankLogQuery::Read()
{
    _worldPacket >> Tab;
}

/**
 * @brief 序列化银行日志查询结果
 * @return 返回构建好的网络包指针
 *
 * 写入顺序：
 * 1. 标签页编号
 * 2. 日志条目数量
 * 3. 每个条目的详细数据：
 *    - 日志类型
 *    - 玩家GUID
 *    - 根据类型写入：
 *      * 物品存取：物品ID和数量
 *      * 物品移动：物品ID、数量、目标标签页
 *      * 金币操作：金币数量
 *    - 时间偏移
 */
WorldPacket const* WorldPackets::Guild::GuildBankLogQueryResults::Write()
{
    _worldPacket << uint8(Tab);
    _worldPacket << uint8(Entry.size());

    // 遍历所有日志条目
    for (GuildBankLogEntry const& logEntry : Entry)
    {
        _worldPacket << int8(logEntry.EntryType);
        _worldPacket << logEntry.PlayerGUID;

        // 根据日志类型写入不同的数据
        switch (logEntry.EntryType)
        {
            case GUILD_BANK_LOG_DEPOSIT_ITEM:    // 存入物品
            case GUILD_BANK_LOG_WITHDRAW_ITEM:   // 取出物品
                _worldPacket << uint32(logEntry.ItemID);
                _worldPacket << uint32(logEntry.Count);
                break;
            case GUILD_BANK_LOG_MOVE_ITEM:       // 移动物品
            case GUILD_BANK_LOG_MOVE_ITEM2:      // 移动物品2
                _worldPacket << uint32(logEntry.ItemID);
                _worldPacket << uint32(logEntry.Count);
                _worldPacket << uint8(logEntry.OtherTab);
                break;
            default: // 金币操作
                _worldPacket << uint32(logEntry.Money);
                break;
        }

        _worldPacket << uint32(logEntry.TimeOffset);
    }

    return &_worldPacket;
}

/**
 * @brief 读取银行标签页文本查询请求数据
 *
 * 从客户端网络包中读取要查询的标签页编号。
 */
void WorldPackets::Guild::GuildBankTextQuery::Read()
{
    _worldPacket >> Tab;
}

/**
 * @brief 序列化银行标签页文本查询结果
 * @return 返回构建好的网络包指针
 *
 * 写入标签页编号和备注文本内容。
 */
WorldPacket const* WorldPackets::Guild::GuildBankTextQueryResult::Write()
{
    _worldPacket << uint8(Tab);
    _worldPacket << Text;

    return &_worldPacket;
}

/**
 * @brief 读取设置银行标签页文本请求数据
 *
 * 从客户端网络包中读取标签页编号和新的备注文本。
 */
void WorldPackets::Guild::GuildBankSetTabText::Read()
{
    _worldPacket >> Tab;
    _worldPacket >> TabText;
}

/**
 * @brief 读取转让会长请求数据
 *
 * 从客户端网络包中读取新会长的角色名称。
 */
void WorldPackets::Guild::GuildSetGuildMaster::Read()
{
    _worldPacket >> NewMasterName;
}

/**
 * @brief 读取保存公会徽章请求数据
 *
 * 从客户端网络包中读取徽章设计NPC的GUID和徽章样式信息。
 * 包括图案样式、颜色、边框样式和颜色、背景颜色。
 */
void WorldPackets::Guild::SaveGuildEmblem::Read()
{
    _worldPacket >> Vendor;
    _worldPacket >> EStyle;
    _worldPacket >> EColor;
    _worldPacket >> BStyle;
    _worldPacket >> BColor;
    _worldPacket >> Bg;
}

/**
 * @brief 序列化保存公会徽章结果
 * @return 返回构建好的网络包指针
 *
 * 写入保存操作的结果码（0=成功，其他=错误）。
 */
WorldPacket const* WorldPackets::Guild::PlayerSaveGuildEmblem::Write()
{
    _worldPacket << int32(Error);

    return &_worldPacket;
}
