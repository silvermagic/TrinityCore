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
 * @file Mail.cpp
 * @brief 邮件系统核心实现文件
 *
 * 本文件实现了 TrinityCore 邮件系统的核心功能。
 * 提供了邮件发送、接收、退回和物品管理等功能的具体实现。
 *
 * 主要实现内容：
 * - MailSender: 根据不同类型的对象创建邮件发送者信息
 * - MailReceiver: 邮件接收者信息管理
 * - MailDraft: 邮件草稿的构建和发送逻辑
 * - 邮件模板物品生成
 * - 邮件退回和删除处理
 *
 * 依赖模块：
 * - AuctionHouseMgr: 拍卖行管理
 * - CalendarMgr: 日历事件管理
 * - CharacterCache: 角色缓存
 * - ObjectAccessor: 对象访问器
 * - LootMgr: 战利品生成系统
 */

#include "Mail.h"
#include "AuctionHouseMgr.h"
#include "BattlegroundMgr.h"
#include "CalendarMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"

/**
 * @brief MailSender 构造函数 - 从通用对象推断发送者信息
 *
 * 根据对象的类型（Unit、GameObject、Player）自动设置邮件类型和发送者ID。
 * 这是一个多态处理函数，能够识别不同类型的游戏对象。
 *
 * @param sender 发送者对象指针，支持 Unit、GameObject 或 Player 类型
 * @param stationery 信纸样式，默认为默认样式
 *
 * 类型映射规则：
 * - TYPEID_UNIT (NPC/生物) -> MAIL_CREATURE，使用Entry ID作为发送者ID
 * - TYPEID_GAMEOBJECT (游戏对象) -> MAIL_GAMEOBJECT，使用Entry ID作为发送者ID
 * - TYPEID_PLAYER (玩家) -> MAIL_NORMAL，使用玩家GUID低32位作为发送者ID
 * - 其他类型 -> MAIL_NORMAL，发送者ID设为0，记录错误日志
 *
 * 调用时机：
 * - NPC发送邮件给玩家时
 * - 游戏对象发送邮件时
 * - 通过通用接口发送邮件时
 *
 * 注意事项：
 * - 对于不支持的类型会记录错误日志，但不会中断执行
 * - 发送者ID为0时会显示为不存在的玩家
 */
MailSender::MailSender(Object* sender, MailStationery stationery) : m_stationery(stationery)
{
    // 根据对象类型确定邮件类型和发送者标识
    switch (sender->GetTypeId())
    {
        case TYPEID_UNIT:
            // NPC/生物发送的邮件，客户端会查询该生物信息
            m_messageType = MAIL_CREATURE;
            m_senderId = sender->GetEntry();  // 使用生物的Entry ID
            break;
        case TYPEID_GAMEOBJECT:
            // 游戏对象发送的邮件，客户端会查询该对象信息
            m_messageType = MAIL_GAMEOBJECT;
            m_senderId = sender->GetEntry();  // 使用对象的Entry ID
            break;
        /*case TYPEID_ITEM:
            m_messageType = MAIL_ITEM;
            m_senderId = sender->GetEntry();
            break;*/
        case TYPEID_PLAYER:
            // 玩家发送的普通邮件
            m_messageType = MAIL_NORMAL;
            m_senderId = sender->GetGUID().GetCounter();  // 使用玩家的GUID
            break;
        default:
            // 不支持的类型，设置为普通邮件但发送者ID为0
            m_messageType = MAIL_NORMAL;
            m_senderId = 0;                                 // will show mail from non-existing player
            TC_LOG_ERROR("misc", "MailSender::MailSender - Mail message contains unexpected sender typeid ({}).", sender->GetTypeId());
            break;
    }
}

/**
 * @brief MailSender 构造函数 - 从日历事件创建发送者
 *
 * 创建日历事件相关的邮件发送者信息。
 * 日历邮件主要用于事件邀请、提醒和取消通知。
 *
 * @param sender 日历事件指针
 *
 * 设置内容：
 * - 邮件类型：MAIL_CALENDAR
 * - 发送者ID：日历事件ID
 * - 信纸样式：MAIL_STATIONERY_DEFAULT（可能需要根据具体需求调整）
 *
 * 调用时机：
 * - 发送日历事件邀请时
 * - 发送事件提醒时
 * - 事件被取消时通知参与者
 */
MailSender::MailSender(CalendarEvent* sender)
    : m_messageType(MAIL_CALENDAR), m_senderId(sender->GetEventId()), m_stationery(MAIL_STATIONERY_DEFAULT) // what stationery we should use here?
{
}

/**
 * @brief MailSender 构造函数 - 从拍卖条目创建发送者
 *
 * 创建拍卖行相关的邮件发送者信息。
 * 拍卖邮件用于通知拍卖结果、过期提醒等。
 *
 * @param sender 拍卖条目指针
 *
 * 设置内容：
 * - 邮件类型：MAIL_AUCTION
 * - 发送者ID：拍卖行ID（用于识别是哪个拍卖行的邮件）
 * - 信纸样式：MAIL_STATIONERY_AUCTION（拍卖行专用样式）
 *
 * 调用时机：
 * - 拍卖成功时通知卖家和买家
 * - 拍卖过期时退还物品给卖家
 * - 拍卖被取消时
 */
MailSender::MailSender(AuctionEntry* sender)
    : m_messageType(MAIL_AUCTION), m_senderId(sender->GetHouseId()), m_stationery(MAIL_STATIONERY_AUCTION) { }

/**
 * @brief MailSender 构造函数 - 从玩家对象创建发送者
 *
 * 创建玩家发送的邮件信息，根据玩家身份自动选择信纸样式。
 * GM玩家使用特殊的信纸样式以区分身份。
 *
 * @param sender 玩家指针
 *
 * 设置内容：
 * - 邮件类型：MAIL_NORMAL
 * - 信纸样式：GM使用MAIL_STATIONERY_GM，普通玩家使用MAIL_STATIONERY_DEFAULT
 * - 发送者ID：玩家GUID低32位
 *
 * 调用时机：
 * - 玩家发送邮件给其他玩家
 * - GM发送系统通知邮件
 */
MailSender::MailSender(Player* sender)
{
    m_messageType = MAIL_NORMAL;
    // 根据玩家是否为GM选择不同的信纸样式
    m_stationery = sender->IsGameMaster() ? MAIL_STATIONERY_GM : MAIL_STATIONERY_DEFAULT;
    m_senderId = sender->GetGUID().GetCounter();
}

/**
 * @brief MailSender 构造函数 - 从生物Entry ID创建发送者
 *
 * 创建NPC发送的邮件信息，使用生物的Entry ID作为标识。
 * 这是一种便捷的构造方式，无需实际的NPC对象。
 *
 * @param senderEntry 生物的Entry ID
 *
 * 设置内容：
 * - 邮件类型：MAIL_CREATURE
 * - 发送者ID：生物Entry ID
 * - 信纸样式：MAIL_STATIONERY_DEFAULT
 *
 * 调用时机：
 * - 系统需要以NPC名义发送邮件但NPC对象不可用时
 * - 任务NPC发送奖励邮件
 * - 特殊NPC发送通知邮件
 */
MailSender::MailSender(uint32 senderEntry)
{
    m_messageType = MAIL_CREATURE;
    m_senderId = senderEntry;
    m_stationery = MAIL_STATIONERY_DEFAULT;
}

/**
 * @brief MailReceiver 构造函数 - 使用玩家对象创建接收者
 *
 * 从玩家对象自动获取GUID创建邮件接收者。
 *
 * @param receiver 玩家对象指针
 *
 * 实现说明：
 * - 同时设置玩家对象指针和GUID
 * - GUID从玩家对象中自动提取
 *
 * 调用时机：
 * - 向在线玩家发送邮件时
 */
MailReceiver::MailReceiver(Player* receiver) : m_receiver(receiver), m_receiver_lowguid(receiver->GetGUID().GetCounter()) { }

/**
 * @brief MailReceiver 构造函数 - 同时提供玩家对象和GUID
 *
 * 显式指定玩家对象和GUID，会验证二者的一致性。
 *
 * @param receiver 玩家对象指针（可以为nullptr表示离线玩家）
 * @param receiver_lowguid 接收者的玩家GUID低32位
 *
 * 实现说明：
 * - 使用断言验证玩家对象存在时，其GUID必须与传入的GUID一致
 * - 允许 receiver 为 nullptr（表示离线玩家）
 *
 * 调用时机：
 * - 发送邮件给可能离线的玩家
 * - 需要明确指定GUID的场景
 */
MailReceiver::MailReceiver(Player* receiver, ObjectGuid::LowType receiver_lowguid) : m_receiver(receiver), m_receiver_lowguid(receiver_lowguid)
{
    // 断言验证：如果玩家对象存在，其GUID必须与传入的GUID一致
    ASSERT(!receiver || receiver->GetGUID().GetCounter() == receiver_lowguid);
}

/**
 * @brief MailDraft::AddItem - 添加附件物品
 *
 * 将物品添加到邮件的附件列表中。
 *
 * @param item 要附加的物品指针
 * @return 返回当前对象的引用，支持链式调用
 *
 * 实现说明：
 * - 使用物品GUID的低32位作为键存储
 * - 如果相同GUID的物品已存在，会被覆盖
 * - 物品存储在map中便于快速查找和避免重复
 *
 * 调用时机：
 * - 发送带附件的邮件时
 * - 退回邮件时添加原有附件
 *
 * 性能说明：
 * - O(log n) 的插入复杂度，n为当前附件数量
 * - 通常邮件附件数量有限（最多12个），性能影响可忽略
 */
MailDraft& MailDraft::AddItem(Item* item)
{
    m_items[item->GetGUID().GetCounter()] = item; return *this;
}

/**
 * @brief MailDraft::prepareItems - 准备邮件模板中的物品
 *
 * 根据邮件模板ID自动生成附件物品。
 * 使用战利品系统从模板定义中生成物品。
 *
 * @param receiver 接收者玩家对象，用于战利品生成时的条件判断
 * @param trans 数据库事务对象，用于保存生成的物品
 *
 * 处理流程：
 * 1. 检查是否有模板ID且需要生成物品
 * 2. 特殊处理模板123（"好消息和坏消息"任务奖励100金币）
 * 3. 使用战利品系统填充邮件战利品
 * 4. 遍历战利品槽位生成物品
 * 5. 保存物品到数据库（防止发送失败时丢失）
 * 6. 添加物品到邮件附件列表
 *
 * 调用时机：
 * - SendMailTo 函数内部，接收者在线时调用
 * - 确保物品在发送前已保存到数据库
 *
 * 注意事项：
 * - 一个模板可以生成多个物品，但最多不超过 MAX_MAIL_ITEMS (12个)
 * - 物品生成使用战利品系统，支持条件判断和随机性
 * - 生成后立即保存到数据库，发送失败时会删除
 */
void MailDraft::prepareItems(Player* receiver, CharacterDatabaseTransaction trans)
{
    // 如果没有模板ID或不需要生成物品，直接返回
    if (!m_mailTemplateId || !m_mailTemplateItemsNeed)
        return;

    // 标记已处理，避免重复生成
    m_mailTemplateItemsNeed = false;

    // 特殊处理：任务"好消息和坏消息"的邮件包含100金币
    // The mail sent after turning in the quest The Good News and The Bad News contains 100g
    if (m_mailTemplateId == 123)
        m_money = 1000000;  // 100金币 = 10000银币 = 1000000铜币

    Loot mailLoot;

    // 使用战利品系统根据模板ID填充邮件战利品
    // can be empty
    mailLoot.FillLoot(m_mailTemplateId, LootTemplates_Mail, receiver, true, true);

    // 获取接收者可以获得的战利品槽位数
    uint32 max_slot = mailLoot.GetMaxSlotInLootFor(receiver);

    // 遍历战利品槽位，生成物品直到达到上限或没有更多物品
    for (uint32 i = 0; m_items.size() < MAX_MAIL_ITEMS && i < max_slot; ++i)
    {
        // 尝试获取该槽位的战利品
        if (LootItem* lootitem = mailLoot.LootItemInSlot(i, receiver))
        {
            // 创建物品实例
            if (Item* item = Item::CreateItem(lootitem->itemid, lootitem->count, receiver))
            {
                // 保存物品到数据库，防止邮件加载时丢失
                // 如果发送失败，物品会被删除
                item->SaveToDB(trans);                           // save for prevent lost at next mail load, if send fail then item will deleted
                AddItem(item);
            }
        }
    }
}

/**
 * @brief MailDraft::deleteIncludedItems - 删除邮件中包含的所有物品
 *
 * 清理邮件草稿中的所有附件物品，可选择是否同时从数据库删除。
 *
 * @param trans 数据库事务对象
 * @param inDB 是否同时从数据库删除物品记录，默认为false
 *
 * 处理流程：
 * 1. 遍历所有附件物品
 * 2. 如果 inDB 为 true，添加数据库删除语句到事务
 * 3. 释放物品对象的内存
 * 4. 清空物品映射表
 *
 * 调用时机：
 * - 邮件发送失败时（inDB=true，删除数据库记录）
 * - 退回邮件但发件人不存在时（inDB=true）
 * - 接收者离线时（inDB=false，物品已在数据库中，仅需释放内存）
 *
 * 注意事项：
 * - 此操作会释放物品对象，调用后不应再访问这些物品
 * - 数据库删除操作会被添加到事务中，确保数据一致性
 */
void MailDraft::deleteIncludedItems(CharacterDatabaseTransaction trans, bool inDB /*= false*/ )
{
    // 遍历所有附件物品
    for (MailItemMap::iterator mailItemIter = m_items.begin(); mailItemIter != m_items.end(); ++mailItemIter)
    {
        Item* item = mailItemIter->second;

        // 如果需要从数据库删除
        if (inDB)
        {
            // 准备删除物品实例的SQL语句
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
            stmt->setUInt32(0, item->GetGUID().GetCounter());
            trans->Append(stmt);
        }

        // 释放物品对象的内存
        delete item;
    }

    // 清空物品映射表
    m_items.clear();
}

/**
 * @brief MailDraft::SendReturnToSender - 将邮件退回给发件人
 *
 * 当邮件无法送达或被收件人退回时，将邮件返回给原始发件人。
 * 如果发件人不存在，则删除邮件和所有附件。
 *
 * @param sender_acc 原始发件人的账号ID
 * @param sender_guid 原始发件人的玩家GUID低32位
 * @param receiver_guid 当前邮件所有者（原收件人）的玩家GUID低32位
 * @param trans 数据库事务对象
 *
 * 处理流程：
 * 1. 查找原始发件人是否在线
 * 2. 如果发件人离线，尝试获取其账号ID
 * 3. 如果发件人不存在（账号ID为0），删除邮件和所有附件
 * 4. 如果发件人存在：
 *    a. 检查是否需要物品送达延迟（发件人和收件人不同账号时需要）
 *    b. 更新所有附件物品的所有者为发件人
 *    c. 计算送达延迟时间
 *    d. 发送退回邮件给发件人
 *
 * 调用时机：
 * - 收件人退回邮件时
 * - COD邮件被拒绝时
 * - 邮件过期系统自动退回时
 *
 * 安全机制：
 * - MAIL_CHECK_MASK_RETURNED 标记防止邮件被多次退回造成死循环
 * - 不同账号间的物品转移有延迟，防止快速转移物品
 *
 * 性能说明：
 * - 涉及数据库更新操作，使用事务保证一致性
 * - 查找在线玩家使用 ObjectAccessor，效率较高
 */
void MailDraft::SendReturnToSender(uint32 sender_acc, ObjectGuid::LowType sender_guid, ObjectGuid::LowType receiver_guid, CharacterDatabaseTransaction trans)
{
    // 构建原始发件人的完整GUID
    ObjectGuid receiverGuid(HighGuid::Player, receiver_guid);

    // 尝试查找在线的原始发件人
    Player* receiver = ObjectAccessor::FindConnectedPlayer(receiverGuid);

    // 如果发件人离线，尝试获取其账号ID
    uint32 rc_account = 0;
    if (!receiver)
        rc_account = sCharacterCache->GetCharacterAccountIdByGuid(receiverGuid);

    // 如果发件人不存在（离线且无账号信息），删除邮件和附件
    if (!receiver && !rc_account)                            // sender not exist
    {
        deleteIncludedItems(trans, true);  // 从数据库删除物品
        return;
    }

    // 准备邮件并处理物品
    // prepare mail and send in other case
    bool needItemDelay = false;

    if (!m_items.empty())
    {
        // 如果物品发送给不同账号的角色，应用物品送达延迟
        // 这是为了防止快速转移物品的滥用行为
        // if item send to character at another account, then apply item delivery delay
        needItemDelay = sender_acc != rc_account;

        // 更新物品所有者为原始发件人，防止发件人删除角色时物品丢失
        // set owner to new receiver (to prevent delete item with sender char deleting)
        for (MailItemMap::iterator mailItemIter = m_items.begin(); mailItemIter != m_items.end(); ++mailItemIter)
        {
            Item* item = mailItemIter->second;
            // 物品不在背包中，需要独立保存
            item->SaveToDB(trans);                      // item not in inventory and can be save standalone

            // 更新数据库中物品的所有者为原始发件人
            // owner in data will set at mail receive and item extracting
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_OWNER);
            stmt->setUInt32(0, receiver_guid);
            stmt->setUInt32(1, item->GetGUID().GetCounter());
            trans->Append(stmt);
        }
    }

    // 如果有物品且跨账号，应用服务器配置的送达延迟（默认1小时）
    // If theres is an item, there is a one hour delivery delay.
    uint32 deliver_delay = needItemDelay ? sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY) : 0;

    // 发送退回邮件，标记为已退回防止再次退回
    // will delete item or place to receiver mail list
    SendMailTo(trans, MailReceiver(receiver, receiver_guid), MailSender(MAIL_NORMAL, sender_guid), MAIL_CHECK_MASK_RETURNED, deliver_delay);
}

/**
 * @brief MailDraft::SendMailTo - 发送邮件到指定接收者
 *
 * 完成邮件的发送流程，将邮件数据持久化到数据库，
 * 并在接收者在线时更新其内存中的邮件列表。
 *
 * @param trans 数据库事务对象，确保数据一致性
 * @param receiver 邮件接收者封装对象
 * @param sender 邮件发送者封装对象
 * @param checked 邮件检查标记，默认为MAIL_CHECK_MASK_NONE
 * @param deliver_delay 邮件送达延迟时间（秒），默认为0表示立即送达
 *
 * 处理流程：
 * 1. 获取接收者和发送者的玩家对象（可能为空表示离线）
 * 2. 如果接收者在线，准备邮件模板中的物品
 * 3. 生成唯一的邮件ID
 * 4. 计算送达时间和过期时间
 * 5. 将邮件基本信息写入数据库 mail 表
 * 6. 将附件物品信息写入数据库 mail_items 表
 * 7. 如果接收者在线：
 *    a. 更新新邮件送达时间
 *    b. 创建内存中的 Mail 对象并填充数据
 *    c. 添加到接收者的邮件列表
 *    d. 将物品添加到接收者的临时物品映射中
 * 8. 如果接收者离线且有附件，清理内存中的物品对象
 *
 * 过期时间计算规则：
 * - 拍卖行邮件（无物品无金币）：CONFIG_MAIL_DELIVERY_DELAY（默认1小时）
 * - 战场管理员邮件（奖励印记）：1天
 * - COD邮件：3天
 * - 普通邮件：30天（GM发送的为90天）
 *
 * 调用时机：
 * - 玩家发送新邮件
 * - 系统发送通知邮件
 * - 退回邮件
 * - 拍卖行通知
 * - 日历事件通知
 *
 * 数据库操作：
 * - CHAR_INS_MAIL: 插入邮件基本信息
 * - CHAR_INS_MAIL_ITEM: 插入邮件附件物品关联
 *
 * 性能说明：
 * - 所有数据库操作在同一事务中，确保原子性
 * - 在线接收者会立即收到新邮件通知
 * - 离线接收者下次登录时从数据库加载邮件
 */
void MailDraft::SendMailTo(CharacterDatabaseTransaction trans, MailReceiver const& receiver, MailSender const& sender, MailCheckMask checked, uint32 deliver_delay)
{
    // 获取接收者玩家对象（可能为NULL表示离线）
    Player* pReceiver = receiver.GetPlayer();               // can be NULL

    // 查找在线的发送者
    Player* pSender = ObjectAccessor::FindPlayerByLowGUID(sender.GetSenderId());

    // 如果接收者在线，准备邮件模板中的物品
    if (pReceiver)
        prepareItems(pReceiver, trans);                            // generate mail template items

    // 生成全局唯一的邮件ID
    uint32 mailId = sObjectMgr->GenerateMailID();

    // 计算实际送达时间
    time_t deliver_time = GameTime::GetGameTime() + deliver_delay;

    // 计算邮件过期延迟时间
    //expire time if COD 3 days, if no COD 30 days, if auction sale pending 1 hour
    uint32 expire_delay;

    // 拍卖行邮件且无物品和金币，使用送达延迟作为过期时间（通常是销售待处理时间）
    // auction mail without any items and money
    if (sender.GetMailMessageType() == MAIL_AUCTION && m_items.empty() && !m_money)
        expire_delay = sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY);
    // 战场管理员的邮件（奖励印记）仅保存1天
    // mail from battlemaster (rewardmarks) should last only one day
    else if (sender.GetMailMessageType() == MAIL_CREATURE && sBattlegroundMgr->GetBattleMasterBG(sender.GetSenderId()) != BATTLEGROUND_TYPE_NONE)
        expire_delay = DAY;
     // 默认情况：COD邮件3天，普通邮件30天（GM发送的90天）
     // default case: expire time if COD 3 days, if no COD 30 days (or 90 days if sender is a game master)
    else
    {
        if (m_COD)
            expire_delay = 3 * DAY;  // COD邮件有效期3天
        else
            expire_delay = pSender && pSender->IsGameMaster() ? 90 * DAY : 30 * DAY;  // GM邮件90天，普通邮件30天
    }

    // 计算最终过期时间
    time_t expire_time = deliver_time + expire_delay;

    // 将邮件基本信息插入数据库
    // Add to DB
    uint8 index = 0;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL);
    stmt->setUInt32(  index, mailId);                    // 邮件ID
    stmt->setUInt8 (++index, uint8(sender.GetMailMessageType()));  // 消息类型
    stmt->setInt8  (++index, int8(sender.GetStationery()));        // 信纸样式
    stmt->setUInt16(++index, GetMailTemplateId());       // 邮件模板ID
    stmt->setUInt32(++index, sender.GetSenderId());      // 发送者ID
    stmt->setUInt32(++index, receiver.GetPlayerGUIDLow()); // 接收者ID
    stmt->setString(++index, GetSubject());              // 邮件主题
    stmt->setString(++index, GetBody());                 // 邮件正文
    stmt->setBool  (++index, !m_items.empty());          // 是否有附件
    stmt->setUInt64(++index, uint64(expire_time));       // 过期时间
    stmt->setUInt64(++index, uint64(deliver_time));      // 送达时间
    stmt->setUInt32(++index, m_money);                   // 附带金币
    stmt->setUInt32(++index, m_COD);                     // COD金额
    stmt->setUInt8 (++index, uint8(checked));            // 检查标记
    trans->Append(stmt);

    // 将所有附件物品插入 mail_items 表
    for (MailItemMap::const_iterator mailItemIter = m_items.begin(); mailItemIter != m_items.end(); ++mailItemIter)
    {
        Item* pItem = mailItemIter->second;
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_MAIL_ITEM);
        stmt->setUInt32(0, mailId);                      // 邮件ID
        stmt->setUInt32(1, pItem->GetGUID().GetCounter()); // 物品GUID
        stmt->setUInt32(2, receiver.GetPlayerGUIDLow());   // 接收者ID
        trans->Append(stmt);
    }

    // 对于在线接收者，更新其内存中的邮件列表和通知
    // For online receiver update in game mail status and data
    if (pReceiver)
    {
        // 记录新邮件的送达时间，用于客户端显示新邮件通知
        pReceiver->AddNewMailDeliverTime(deliver_time);

        // 创建内存中的邮件对象
        Mail* m = new Mail;
        m->messageID = mailId;
        m->mailTemplateId = GetMailTemplateId();
        m->subject = GetSubject();
        m->body = GetBody();
        m->money = GetMoney();
        m->COD = GetCOD();

        // 复制附件物品信息
        for (MailItemMap::const_iterator mailItemIter = m_items.begin(); mailItemIter != m_items.end(); ++mailItemIter)
        {
            Item* item = mailItemIter->second;
            m->AddItem(item->GetGUID().GetCounter(), item->GetEntry());
        }

        // 设置邮件的其他属性
        m->messageType = sender.GetMailMessageType();
        m->stationery = sender.GetStationery();
        m->sender = sender.GetSenderId();
        m->receiver = receiver.GetPlayerGUIDLow();
        m->expire_time = expire_time;
        m->deliver_time = deliver_time;
        m->checked = checked;
        m->state = MAIL_STATE_UNCHANGED;  // 新邮件，无需同步数据库

        // 将邮件添加到接收者的邮件列表开头
        pReceiver->AddMail(m);                           // to insert new mail to beginning of maillist

        // 如果有附件物品，将物品添加到接收者的临时物品映射中
        // 这样接收者可以直接访问这些物品对象
        if (!m_items.empty())
        {
            for (MailItemMap::iterator mailItemIter = m_items.begin(); mailItemIter != m_items.end(); ++mailItemIter)
                pReceiver->AddMItem(mailItemIter->second);
        }
    }
    else if (!m_items.empty())
    {
        // 接收者离线且有附件物品，清理内存中的物品对象
        // 物品已在数据库中保存，接收者登录时会重新加载
        CharacterDatabaseTransaction temp = CharacterDatabaseTransaction(nullptr);
        deleteIncludedItems(temp);  // 仅释放内存，不删除数据库记录
    }
}
