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
 * @file MailPackets.cpp
 * @brief 邮件系统网络包实现模块
 *
 * 本模块实现了邮件系统相关的所有网络包的序列化和反序列化功能。
 * 包括邮件列表查询、发送邮件、收取附件、删除邮件等操作的包处理。
 *
 * 主要功能:
 * - 邮件附件物品信息的序列化(MailAttachedItem)
 * - 邮件列表条目的序列化(MailListEntry)
 * - 客户端请求包的解析(Read函数)
 * - 服务器响应包的构造(Write函数)
 *
 * 性能考虑:
 * - 使用预分配空间减少内存重新分配
 * - 动态计算包大小防止溢出
 * - 字符串使用string_view避免拷贝
 */

#include "MailPackets.h"
#include "GameTime.h"
#include "Item.h"
#include "Mail.h"
#include "Player.h"
#include "World.h"

/**
 * @brief 构造邮件附件物品信息
 * @param item 物品对象指针
 * @param pos 附件在邮件中的位置索引
 *
 * 从物品对象中提取所有必要信息用于网络传输。
 * 包括物品的基本属性、附魔信息、耐久度等。
 */
WorldPackets::Mail::MailAttachedItem::MailAttachedItem(::Item const* item, uint8 pos)
{
    // 设置附件位置索引
    Position = pos;
    // 获取物品GUID的低32位作为附件ID
    AttachID = item->GetGUID().GetCounter();
    // 获取物品模板ID
    ItemID = item->GetEntry();
    // 获取随机属性ID(随机附魔物品)
    RandomPropertiesID = item->GetItemRandomPropertyId();
    // 获取随机属性种子(随机后缀)
    RandomPropertiesSeed = item->GetItemSuffixFactor();
    // 获取物品堆叠数量
    Count = item->GetCount();
    // 获取物品使用次数(消耗品)
    Charges = item->GetSpellCharges();
    // 获取最大耐久度(装备)
    MaxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
    // 获取当前耐久度(装备)
    Durability = item->GetInt32Value(ITEM_FIELD_DURABILITY);
    // 检查物品是否已解锁(不需要钥匙)
    Unlocked = !item->IsLocked();

    // 遍历所有附魔槽位,提取附魔信息
    for (uint8 j = 0; j < MAX_INSPECTED_ENCHANTMENT_SLOT; j++)
    {
        EnchantmentSlot slot = EnchantmentSlot(j);
        // 获取附魔ID
        EnchantmentID[slot] = item->GetEnchantmentId(slot);
        // 获取附魔持续时间(秒)
        EnchantmentDuration[slot] = item->GetEnchantmentDuration(slot);
        // 获取附魔剩余使用次数
        EnchantmentCharges[slot] = item->GetEnchantmentCharges(slot);
    }
}

/**
 * @brief 序列化邮件附件物品信息到字节缓冲区
 * @param data 字节缓冲区引用
 * @param att 要序列化的附件物品信息
 * @return 字节缓冲区引用(支持链式调用)
 *
 * 按照协议格式将附件物品信息写入网络包。
 * 写入顺序必须与客户端解析顺序一致。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Mail::MailAttachedItem const& att)
{
    // 写入附件位置索引
    data << uint8(att.Position);
    // 写入附件ID
    data << int32(att.AttachID);
    // 写入物品模板ID
    data << int32(att.ItemID);

    // 写入所有附魔槽位信息
    for (uint8 i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; i++)
    {
        // 每个槽位写入ID、持续时间、使用次数三个字段
        data << int32(att.EnchantmentID[i]);
        data << int32(att.EnchantmentDuration[i]);
        data << int32(att.EnchantmentCharges[i]);
    }

    // 写入随机属性ID
    data << int32(att.RandomPropertiesID);
    // 写入随机属性种子
    data << int32(att.RandomPropertiesSeed);
    // 写入物品堆叠数量
    data << int32(att.Count);
    // 写入物品使用次数
    data << int32(att.Charges);
    // 写入最大耐久度
    data << uint32(att.MaxDurability);
    // 写入当前耐久度
    data << int32(att.Durability);
    // 写入解锁标志
    data << bool(att.Unlocked);

    return data;
}

/**
 * @brief 构造邮件列表条目
 * @param mail 邮件对象指针
 * @param player 接收邮件的玩家对象
 *
 * 从邮件对象和玩家对象中提取邮件信息,包括发送者、内容、附件等。
 * 根据邮件类型(玩家、NPC、拍卖行等)填充不同的发送者字段。
 */
WorldPackets::Mail::MailListEntry::MailListEntry(::Mail const* mail, ::Player* player)
{
    // 获取邮件唯一ID
    MailID = mail->messageID;
    // 获取发送者类型(玩家、NPC、拍卖行等)
    SenderType = mail->messageType;

    // 根据发送者类型填充发送者信息
    switch (mail->messageType)
    {
        case MAIL_NORMAL:
            // 玩家邮件:使用玩家角色GUID
            SenderCharacter = ObjectGuid::Create<HighGuid::Player>(mail->sender);
            break;
        case MAIL_CREATURE:
        case MAIL_GAMEOBJECT:
        case MAIL_AUCTION:
        case MAIL_CALENDAR:
            // NPC、游戏对象、拍卖行、日历邮件:使用替代发送者ID
            AltSenderID = mail->sender;
            break;
    }

    // 获取货到付款金额
    Cod = mail->COD;
    // 获取信纸类型ID(决定邮件外观)
    StationeryID = mail->stationery;
    // 获取邮件附带的金币
    SentMoney = mail->money;
    // 获取邮件状态标志(已读、已回复等)
    Flags = mail->checked;
    // 计算剩余天数(过期时间 - 当前时间)
    DaysLeft = float(mail->expire_time - GameTime::GetGameTime()) / float(DAY);
    // 获取邮件模板ID(系统邮件)
    MailTemplateID = mail->mailTemplateId;
    // 获取邮件主题(使用string_view避免拷贝)
    Subject = mail->subject;
    // 获取邮件正文(使用string_view避免拷贝)
    Body = mail->body;

    // 遍历邮件附件,构造附件物品信息
    for (uint8 i = 0; i < mail->items.size(); i++)
    {
        // 从玩家临时邮件物品缓存中获取物品对象
        if (::Item* item = player->GetMItem(mail->items[i].item_guid))
        {
            // 构造附件信息并添加到列表
            Attachments.emplace_back(item, i);
        }
    }
}

/**
 * @brief 计算邮件列表条目的序列化大小
 * @return 序列化后的字节数
 *
 * 动态计算包括主题、正文和附件在内的总大小。
 * 用于检测是否超出网络包大小限制,避免包溢出。
 */
std::size_t WorldPackets::Mail::MailListEntry::GetPacketSize() const
{
    // 计算固定字段大小 + 可变字段大小(主题、正文) + 所有附件大小
    return sizeof(uint16) + sizeof(int32) + sizeof(uint8) + (SenderCharacter ? sizeof(uint64) : 0) + (AltSenderID ? sizeof(int32) : 0)
        + sizeof(uint32) + sizeof(int32) + sizeof(int32) + sizeof(uint32) + sizeof(int32) + sizeof(float) + sizeof(int32)
        + Subject.length() + 1 + Body.length() + 1 + sizeof(uint8) + Attachments.size() * MailAttachedItem::GetPacketSize();
}

/**
 * @brief 序列化邮件列表条目到字节缓冲区
 * @param data 字节缓冲区引用
 * @param entry 要序列化的邮件列表条目
 * @return 字节缓冲区引用(支持链式调用)
 *
 * 按照协议格式将邮件信息写入网络包。
 * 写入顺序必须与客户端解析顺序一致。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Mail::MailListEntry const& entry)
{
    // 首先写入整个条目的大小(客户端需要预先知道大小来分配内存)
    data << uint16(entry.GetPacketSize());
    // 写入邮件ID
    data << int32(entry.MailID);
    // 写入发送者类型
    data << uint8(entry.SenderType);

    // 根据发送者类型写入发送者信息
    if (entry.SenderCharacter)
    {
        // 玩家邮件:写入玩家GUID
        data << *entry.SenderCharacter;
    }
    else if (entry.AltSenderID)
    {
        // NPC/拍卖行等邮件:写入替代发送者ID
        data << int32(*entry.AltSenderID);
    }

    // 写入货到付款金额
    data << uint32(entry.Cod);
    // 写入包裹ID(未使用)
    data << int32(entry.PackageID);
    // 写入信纸类型ID
    data << int32(entry.StationeryID);
    // 写入附带的金币
    data << uint32(entry.SentMoney);
    // 写入邮件状态标志
    data << int32(entry.Flags);
    // 写入剩余天数
    data << float(entry.DaysLeft);
    // 写入邮件模板ID
    data << int32(entry.MailTemplateID);
    // 写入邮件主题(以null结尾的字符串)
    data << entry.Subject;
    // 写入邮件正文(以null结尾的字符串)
    data << entry.Body;
    // 写入附件数量
    data << uint8(entry.Attachments.size());

    // 写入所有附件物品信息
    for (WorldPackets::Mail::MailAttachedItem const& att : entry.Attachments)
        data << att;

    return data;
}

/**
 * @brief 读取邮件列表请求包
 *
 * 从网络包中解析邮箱对象的GUID。
 * 玩家打开邮箱时发送此请求。
 */
void WorldPackets::Mail::MailGetList::Read()
{
    // 读取邮箱对象的GUID(GameObject的GUID)
    _worldPacket >> Mailbox;
}

/**
 * @brief 构造邮件列表结果包
 *
 * 初始化服务器包并预分配空间。
 * 预先写入占位数据(邮件总数和列表大小为0),后续会更新。
 */
WorldPackets::Mail::MailListResult::MailListResult() : ServerPacket(SMSG_MAIL_LIST_RESULT, 8)
{
    // 预先写入邮件总数占位符(后续在Write中更新)
    _worldPacket << int32(0); // TotalNumRecords
    // 预先写入邮件列表大小占位符(后续在Write中更新)
    _worldPacket << uint8(0); // Mails.size()
}

/**
 * @brief 序列化邮件列表结果包
 * @return 指向序列化后的WorldPacket指针
 *
 * 更新邮件总数和列表大小字段,完成包的序列化。
 * 邮件内容已在AddMail中写入,此处仅更新计数字段。
 */
WorldPacket const* WorldPackets::Mail::MailListResult::Write()
{
    // 更新邮件总数(偏移量0)
    _worldPacket.put<int32>(0, TotalNumRecords);
    // 更新邮件列表大小(偏移量4)
    _worldPacket.put<uint8>(4, Mails.size());

    return &_worldPacket;
}

/**
 * @brief 添加邮件到列表结果中
 * @param mail 邮件对象指针
 * @param player 接收邮件的玩家对象
 *
 * 将一封邮件添加到返回列表中,同时检查包大小和邮件数量限制。
 * 如果超出限制会停止添加,但TotalNumRecords仍会计数所有邮件。
 *
 * 限制条件:
 * - 单包最多50封邮件(客户端限制)
 * - 包大小不能超过INT16_MAX(约32KB)
 */
void WorldPackets::Mail::MailListResult::AddMail(::Mail const* mail, Player* player)
{
    // 增加邮件总计数(即使不添加到包中也要计数)
    ++TotalNumRecords;

    // 检查是否已达到邮件数量限制或包大小限制
    if (Mails.size() >= 50 || _maxPacketSizeReached)
        return;

    // 构造邮件列表条目
    MailListEntry packetEntry(mail, player);

    // 检查添加此邮件后是否会超出包大小限制
    if (_worldPacket.size() + packetEntry.GetPacketSize() >= std::size_t(std::numeric_limits<int16>::max()))
    {
        // 标记已达到包大小限制,后续邮件不再添加
        _maxPacketSizeReached = true;
        return;
    }

    // 将邮件条目序列化到网络包中
    _worldPacket << Mails.emplace_back(std::move(packetEntry));
}

/**
 * @brief 读取创建文本物品请求包
 *
 * 从网络包中解析邮箱GUID和邮件ID。
 * 玩家右键点击邮件正文时发送此请求。
 */
void WorldPackets::Mail::MailCreateTextItem::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取目标邮件ID
    _worldPacket >> MailID;
}

/**
 * @brief 读取发送邮件请求包
 *
 * 从网络包中解析所有邮件发送信息,包括收件人、内容、附件等。
 * 解析顺序必须与客户端发送顺序一致。
 */
void WorldPackets::Mail::SendMail::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Info.Mailbox;
    // 读取收件人角色名称
    _worldPacket >> Info.Target;
    // 读取邮件主题(最多255字符)
    _worldPacket >> Info.Subject;
    // 读取邮件正文(最多7999字符)
    _worldPacket >> Info.Body;
    // 读取信纸类型ID
    _worldPacket >> Info.StationeryID;
    // 读取包裹ID(未使用)
    _worldPacket >> Info.PackageID;

    // 读取附件数量并调整数组大小
    Info.Attachments.resize(_worldPacket.read<uint8>());

    // 读取所有附件信息
    for (auto& att : Info.Attachments)
    {
        // 读取附件槽位位置
        _worldPacket >> att.AttachPosition;
        // 读取物品GUID(玩家背包中的物品)
        _worldPacket >> att.ItemGUID;
    }

    // 读取发送的金币数量
    _worldPacket >> Info.SendMoney;
    // 读取货到付款金额
    _worldPacket >> Info.Cod;

    // 跳过未使用的8字节字段(客户端会发送)
    _worldPacket.read_skip<uint64>();
    // 跳过未使用的1字节字段(客户端会发送)
    _worldPacket.read_skip<uint8>();
}

/**
 * @brief 读取退回邮件请求包
 *
 * 从网络包中解析邮箱GUID、邮件ID和发送者GUID。
 * 玩家选择退回邮件时发送此请求。
 */
void WorldPackets::Mail::MailReturnToSender::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取要退回的邮件ID
    _worldPacket >> MailID;
    // 读取原发送者GUID
    _worldPacket >> SenderGUID;
}

/**
 * @brief 序列化邮件命令结果包
 * @return 指向序列化后的WorldPacket指针
 *
 * 根据错误码和操作类型选择性写入额外字段。
 * 不同的错误情况需要发送不同的数据给客户端。
 */
WorldPacket const* WorldPackets::Mail::MailCommandResult::Write()
{
    // 写入邮件ID
    _worldPacket << uint32(MailID);
    // 写入操作类型
    _worldPacket << uint32(Command);
    // 写入错误码
    _worldPacket << uint32(ErrorCode);

    // 如果是装备错误,需要写入背包错误码
    if (ErrorCode == MAIL_ERR_EQUIP_ERROR)
        _worldPacket << uint32(BagResult);

    // 如果是收取附件操作
    if (Command == MAIL_ITEM_TAKEN)
    {
        // 如果操作成功或物品已过期,需要写入附件ID和背包中数量
        if (ErrorCode == MAIL_OK || ErrorCode == MAIL_ERR_ITEM_HAS_EXPIRED)
        {
            _worldPacket << uint32(AttachID);
            _worldPacket << uint32(QtyInInventory);
        }
    }

    return &_worldPacket;
}

/**
 * @brief 读取标记邮件已读请求包
 *
 * 从网络包中解析邮箱GUID和邮件ID。
 * 玩家打开邮件阅读后发送此请求。
 */
void WorldPackets::Mail::MailMarkAsRead::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取要标记的邮件ID
    _worldPacket >> MailID;
}

/**
 * @brief 读取删除邮件请求包
 *
 * 从网络包中解析邮箱GUID、邮件ID和删除原因。
 * 玩家删除邮件时发送此请求。
 */
void WorldPackets::Mail::MailDelete::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取要删除的邮件ID
    _worldPacket >> MailID;
    // 读取删除原因(用于日志和统计)
    _worldPacket >> DeleteReason;
}

/**
 * @brief 读取收取邮件附件请求包
 *
 * 从网络包中解析邮箱GUID、邮件ID和附件ID。
 * 玩家点击收取附件物品时发送此请求。
 */
void WorldPackets::Mail::MailTakeItem::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取邮件ID
    _worldPacket >> MailID;
    // 读取附件ID(物品GUID)
    _worldPacket >> AttachID;
}

/**
 * @brief 读取收取邮件金币请求包
 *
 * 从网络包中解析邮箱GUID和邮件ID。
 * 玩家点击收取金币时发送此请求。
 */
void WorldPackets::Mail::MailTakeMoney::Read()
{
    // 读取邮箱对象GUID
    _worldPacket >> Mailbox;
    // 读取邮件ID
    _worldPacket >> MailID;
}

/**
 * @brief 构造即将到达邮件信息条目
 * @param mail 邮件对象指针
 *
 * 从邮件对象中提取发送者信息和到达时间。
 * 用于通知客户端即将到达的新邮件。
 */
WorldPackets::Mail::MailQueryNextTimeResult::MailNextTimeEntry::MailNextTimeEntry(::Mail const* mail)
{
    // 根据邮件类型填充发送者信息
    switch (mail->messageType)
    {
        case MAIL_NORMAL:
            // 玩家邮件:构造玩家GUID
            SenderGuid = ObjectGuid::Create<HighGuid::Player>(mail->sender);
            break;
        case MAIL_AUCTION:
        case MAIL_CREATURE:
        case MAIL_GAMEOBJECT:
        case MAIL_CALENDAR:
            // 拍卖行、NPC、游戏对象、日历邮件:使用替代发送者ID
            AltSenderID = mail->sender;
            break;
    }

    // 计算剩余到达时间(交付时间 - 当前时间)
    TimeLeft = mail->deliver_time - time(nullptr);
    // 设置替代发送者类型
    AltSenderType = mail->messageType;
    // 设置信纸类型ID
    StationeryID = mail->stationery;
}

/**
 * @brief 序列化查询下一封邮件时间结果包
 * @return 指向序列化后的WorldPacket指针
 *
 * 将下一封邮件到达时间和即将到达的邮件列表写入网络包。
 * 客户端使用此信息显示新邮件倒计时通知。
 */
WorldPacket const* WorldPackets::Mail::MailQueryNextTimeResult::Write()
{
    // 写入下一封邮件的到达时间(秒)
    _worldPacket << float(NextMailTime);
    // 写入即将到达的邮件数量
    _worldPacket << int32(Next.size());

    // 写入每封即将到达邮件的信息
    for (auto const& entry : Next)
    {
        // 写入发送者GUID(玩家邮件)
        _worldPacket << entry.SenderGuid;
        // 写入替代发送者ID(NPC、拍卖行等)
        _worldPacket << int32(entry.AltSenderID);
        // 写入替代发送者类型
        _worldPacket << int32(entry.AltSenderType);
        // 写入信纸类型ID
        _worldPacket << int32(entry.StationeryID);
        // 写入剩余到达时间
        _worldPacket << float(entry.TimeLeft);
    }

    return &_worldPacket;
}

/**
 * @brief 序列化收到新邮件通知包
 * @return 指向序列化后的WorldPacket指针
 *
 * 将显示通知的延迟时间写入网络包。
 * 服务器主动推送此包通知客户端收到新邮件。
 */
WorldPacket const* WorldPackets::Mail::NotifyReceivedMail::Write()
{
    // 写入显示通知的延迟时间(秒)
    // 延迟用于拍卖行邮件等场景,避免立即通知
    _worldPacket << float(Delay);

    return &_worldPacket;
}

/**
 * @brief 序列化显示邮箱界面包
 * @return 指向序列化后的WorldPacket指针
 *
 * 将邮递员或邮箱对象的GUID写入网络包。
 * 服务器发送此包请求客户端打开邮箱界面。
 */
WorldPacket const* WorldPackets::Mail::ShowMailbox::Write()
{
    // 写入邮递员或邮箱对象的GUID
    _worldPacket << PostmasterGUID;

    return &_worldPacket;
}
