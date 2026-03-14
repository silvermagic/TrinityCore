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
 * @file AuctionHouseMgr.cpp
 * @brief 拍卖行管理器实现文件
 *
 * 本文件实现了拍卖行系统的核心功能，包括：
 * - 拍卖行的创建、更新、删除操作
 * - 拍卖物品管理和查询
 * - 竞拍和一口价交易处理
 * - 邮件通知系统
 * - 数据库持久化操作
 *
 * 主要类：
 * - AuctionHouseMgr: 全局拍卖行管理器单例
 * - AuctionHouseObject: 单个拍卖行实例
 * - AuctionEntry: 拍卖条目数据结构
 */

#include "AuctionHouseMgr.h"
#include "AuctionHouseBot.h"
#include "AccountMgr.h"
#include "Bag.h"
#include "Common.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WowTime.h"

/**
 * @enum eAuctionHouse
 * @brief 拍卖行内部常量定义
 */
enum eAuctionHouse
{
    AH_MINIMUM_DEPOSIT = 100  // 最小保证金（铜币），用于无法计算保证金的物品
};

/**
 * @brief 构造函数
 */
AuctionHouseMgr::AuctionHouseMgr() { }

/**
 * @brief 析构函数
 * @note 清理所有拍卖物品对象，释放内存
 */
AuctionHouseMgr::~AuctionHouseMgr()
{
    for (ItemMap::iterator itr = mAitems.begin(); itr != mAitems.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 获取拍卖行管理器单例实例
 * @return 拍卖行管理器单例指针
 */
AuctionHouseMgr* AuctionHouseMgr::instance()
{
    static AuctionHouseMgr instance;
    return &instance;
}

/**
 * @brief 根据阵营模板ID获取对应的拍卖行对象
 * @param factionTemplateId 阵营模板ID
 * @return 对应的拍卖行对象指针
 *
 * @note 决策逻辑：
 *       1. 如果服务器允许跨阵营交易，所有阵营都使用中立拍卖行
 *       2. 否则根据阵营分组：
 *          - 联盟阵营 -> 联盟拍卖行
 *          - 部落阵营 -> 部落拍卖行
 *          - 其他/中立阵营 -> 中立拍卖行
 */
AuctionHouseObject* AuctionHouseMgr::GetAuctionsMap(uint32 factionTemplateId)
{
    // 如果允许跨阵营交易，所有拍卖都使用中立拍卖行
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
        return &mNeutralAuctions;

    // 查找阵营模板配置
    // teams have linked auction houses
    FactionTemplateEntry const* uEntry = sFactionTemplateStore.LookupEntry(factionTemplateId);
    if (!uEntry)
        return &mNeutralAuctions;
    else if (uEntry->FactionGroup & FACTION_MASK_ALLIANCE)
        return &mAllianceAuctions;  // 联盟拍卖行
    else if (uEntry->FactionGroup & FACTION_MASK_HORDE)
        return &mHordeAuctions;     // 部落拍卖行
    else
        return &mNeutralAuctions;   // 中立拍卖行
}

/**
 * @brief 根据拍卖行ID获取拍卖行对象
 * @param auctionHouseId 拍卖行ID
 * @return 对应的拍卖行对象指针
 *
 * @note 如果允许跨阵营交易，所有ID都返回中立拍卖行
 *       否则根据ID返回对应的拍卖行实例
 */
AuctionHouseObject* AuctionHouseMgr::GetAuctionsMapByHouseId(uint8 auctionHouseId)
{
    // 跨阵营交易启用时，统一使用中立拍卖行
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
        return &mNeutralAuctions;

    // 根据拍卖行ID返回对应实例
    switch(auctionHouseId)
    {
        case AUCTIONHOUSE_ALLIANCE : return &mAllianceAuctions;  // 联盟拍卖行
        case AUCTIONHOUSE_HORDE : return &mHordeAuctions;        // 部落拍卖行
        default : return &mNeutralAuctions;                      // 中立拍卖行
    }
}

/**
 * @brief 计算拍卖保证金
 * @param entry 拍卖行配置条目
 * @param time 拍卖持续时间（秒）
 * @param pItem 物品对象指针
 * @param count 物品数量
 * @return 保证金金额（铜币）
 *
 * @note 计算公式：
 *       1. 获取物品卖出价格（MSV）
 *       2. 如果物品无卖出价格，返回最小保证金
 *       3. 计算乘数 = 存款率 / 3
 *       4. 计算时间系数 = 时间 / 12小时
 *       5. 保证金 = MSV × 乘数 × 服务器倍率 × 时间系数 × 数量
 *       6. 处理浮点数精度问题（remainderbase）
 *       7. 确保不低于最小保证金
 */
uint32 AuctionHouseMgr::GetAuctionDeposit(AuctionHouseEntry const* entry, uint32 time, Item* pItem, uint32 count)
{
    // 获取物品的卖出价格（Merchant Sell Value）
    uint32 MSV = pItem->GetTemplate()->SellPrice;

    // 如果物品没有卖出价格，返回最小保证金
    if (MSV <= 0)
        return float(AH_MINIMUM_DEPOSIT) * sWorld->getRate(RATE_AUCTION_DEPOSIT);

    // 计算存款乘数（存款率百分比 / 3）
    float multiplier = CalculatePct(float(entry->DepositRate), 3);

    // 计算时间系数（每12小时为一个单位）
    uint32 timeHr = (((time / 60) / 60) / 12);

    // 计算基础保证金
    uint32 deposit = uint32(MSV * multiplier * sWorld->getRate(RATE_AUCTION_DEPOSIT));

    // 计算浮点数部分（处理精度问题）
    float remainderbase = float(MSV * multiplier * sWorld->getRate(RATE_AUCTION_DEPOSIT)) - deposit;

    // 应用时间系数和数量
    deposit *= timeHr * count;

    // 处理浮点数精度：找到能整除的数量
    int i = count;
    while (i > 0 && (remainderbase * i) != uint32(remainderbase * i))
        i--;

    // 添加浮点数部分
    if (i)
        deposit += remainderbase * i * timeHr;

    // 调试日志
    TC_LOG_DEBUG("auctionHouse", "MSV:        {}", MSV);
    TC_LOG_DEBUG("auctionHouse", "Items:      {}", count);
    TC_LOG_DEBUG("auctionHouse", "Multiplier: {}", multiplier);
    TC_LOG_DEBUG("auctionHouse", "Deposit:    {}", deposit);
    TC_LOG_DEBUG("auctionHouse", "Deposit rm: {}", remainderbase * count);

    // 确保不低于最小保证金
    if (deposit < float(AH_MINIMUM_DEPOSIT) * sWorld->getRate(RATE_AUCTION_DEPOSIT))
        return float(AH_MINIMUM_DEPOSIT) * sWorld->getRate(RATE_AUCTION_DEPOSIT);
    else
        return deposit;
}

/**
 * @brief 发送竞拍成功邮件给竞拍者
 * @param auction 拍卖条目
 * @param trans 数据库事务
 *
 * @note 将物品发送给竞拍成功的玩家，处理流程：
 *       1. 检查物品是否存在
 *       2. 获取竞拍者信息（在线/离线）
 *       3. 如果是GM账号，记录到GM日志
 *       4. 更新物品所有者为竞拍者（防止卖家删号时物品被删除）
 *       5. 发送邮件（包含物品）
 *       6. 更新成就进度
 *       7. 如果竞拍者不存在，删除物品
 *
 * @注意 此函数不释放内存，由调用者负责清理拍卖条目
 */
void AuctionHouseMgr::SendAuctionWonMail(AuctionEntry* auction, CharacterDatabaseTransaction trans)
{
    // 获取拍卖物品
    Item* pItem = GetAItem(auction->itemGUIDLow);
    if (!pItem)
        return;

    uint32 bidderAccId = 0;
    ObjectGuid bidderGuid(HighGuid::Player, auction->bidder);
    Player* bidder = ObjectAccessor::FindConnectedPlayer(bidderGuid);

    // 准备GM日志所需数据
    std::string bidderName;
    bool logGmTrade = (auction->Flags & AUCTION_ENTRY_FLAG_GM_LOG_BUYER) != AUCTION_ENTRY_FLAG_NONE;

    // 获取竞拍者信息
    if (bidder)
    {
        // 竞拍者在线
        bidderAccId = bidder->GetSession()->GetAccountId();
        bidderName = bidder->GetName();
    }
    else
    {
        // 竞拍者离线，从缓存获取账号ID
        bidderAccId = sCharacterCache->GetCharacterAccountIdByGuid(bidderGuid);

        // 如果需要记录GM日志，获取竞拍者名字
        if (logGmTrade && !sCharacterCache->GetCharacterNameByGuid(bidderGuid, bidderName))
            bidderName = sObjectMgr->GetTrinityStringForDBCLocale(LANG_UNKNOWN);
    }

    // 记录GM交易日志
    if (logGmTrade)
    {
        ObjectGuid ownerGuid = ObjectGuid(HighGuid::Player, auction->owner);
        std::string ownerName;
        if (!sCharacterCache->GetCharacterNameByGuid(ownerGuid, ownerName))
            ownerName = sObjectMgr->GetTrinityStringForDBCLocale(LANG_UNKNOWN);

        uint32 ownerAccId = sCharacterCache->GetCharacterAccountIdByGuid(ownerGuid);

        sLog->OutCommand(bidderAccId, "GM {} (Account: {}) won item in auction: {} (Entry: {} Count: {}) and pay money: {}. Original owner {} (Account: {})",
            bidderName, bidderAccId, pItem->GetTemplate()->Name1, pItem->GetEntry(), pItem->GetCount(), auction->bid, ownerName, ownerAccId);
    }

    // 检查竞拍者是否存在且不是机器人
    // receiver exist
    if ((bidder || bidderAccId) && !sAuctionBotConfig->IsBotChar(auction->bidder))
    {
        // 更新物品所有者为竞拍者（防止卖家删除角色时物品被删除）
        // set owner to bidder (to prevent delete item with sender char deleting)
        // owner in `data` will set at mail receive and item extracting
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_OWNER);
        stmt->setUInt32(0, auction->bidder);
        stmt->setUInt32(1, pItem->GetGUID().GetCounter());
        trans->Append(stmt);

        // 如果竞拍者在线，发送通知和更新成就
        if (bidder)
        {
            bidder->GetSession()->SendAuctionBidderNotification(auction->GetHouseId(), auction->Id, bidderGuid, 0, 0, auction->itemEntry);
            // FIXME: for offline player need also
            bidder->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS, 1);
        }

        // 发送邮件：主题=物品信息，正文=卖家和价格，附件=物品
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_WON), AuctionEntry::BuildAuctionWonMailBody(ObjectGuid::Create<HighGuid::Player>(auction->owner), auction->bid, auction->buyout))
            .AddItem(pItem)
            .SendMailTo(trans, MailReceiver(bidder, auction->bidder), auction, MAIL_CHECK_MASK_COPIED);
    }
    else
    {
        // 竞拍者不存在或已被删除，删除物品
        // bidder doesn't exist, delete the item
        sAuctionMgr->RemoveAItem(auction->itemGUIDLow, true, &trans);
    }
}

/**
 * @brief 发送销售待处理邮件给卖家
 * @param auction 拍卖条目
 * @param trans 数据库事务
 *
 * @note 通知卖家物品已售出，金币将在延迟时间后到账
 *       包含交易详情：买家信息、价格、手续费、预计到账时间等
 */
void AuctionHouseMgr::SendAuctionSalePendingMail(AuctionEntry* auction, CharacterDatabaseTransaction trans)
{
    ObjectGuid owner_guid(HighGuid::Player, auction->owner);
    Player* owner = ObjectAccessor::FindConnectedPlayer(owner_guid);
    uint32 owner_accId = sCharacterCache->GetCharacterAccountIdByGuid(owner_guid);

    // 检查卖家是否存在且不是机器人
    // owner exist (online or offline)
    if ((owner || owner_accId) && !sAuctionBotConfig->IsBotChar(auction->owner))
    {
        // 计算预计到账时间（当前时间 + 邮件延迟 + 时区偏移）
        WowTime eta = *GameTime::GetUtcWowTime();
        eta += Seconds(sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY));
        if (owner)
            eta += owner->GetSession()->GetTimezoneOffset();

        // 发送发票邮件：包含买家信息、价格、手续费、延迟时间、预计到账时间
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_SALE_PENDING),
            AuctionEntry::BuildAuctionInvoiceMailBody(ObjectGuid::Create<HighGuid::Player>(auction->bidder), auction->bid, auction->buyout, auction->deposit,
                auction->GetAuctionCut(), sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY), eta.GetPackedTime()))
            .SendMailTo(trans, MailReceiver(owner, auction->owner), auction, MAIL_CHECK_MASK_COPIED);
    }
}

/**
 * @brief 发送拍卖成功邮件给卖家
 * @param auction 拍卖条目
 * @param trans 数据库事务
 *
 * @note 处理流程：
 *       1. 计算利润 = 成交价 + 保证金 - 手续费
 *       2. 如果卖家在线，更新成就进度并发送通知
 *       3. 发送邮件（延迟到账，包含利润金币）
 *
 * @注意 此函数不释放内存，由调用者负责清理拍卖条目
 */
void AuctionHouseMgr::SendAuctionSuccessfulMail(AuctionEntry* auction, CharacterDatabaseTransaction trans)
{
    ObjectGuid owner_guid(HighGuid::Player, auction->owner);
    Player* owner = ObjectAccessor::FindConnectedPlayer(owner_guid);
    uint32 owner_accId = sCharacterCache->GetCharacterAccountIdByGuid(owner_guid);

    // 检查卖家是否存在且不是机器人
    // owner exist
    if ((owner || owner_accId) && !sAuctionBotConfig->IsBotChar(auction->owner))
    {
        // 计算实际利润：成交价 + 保证金 - 手续费
        uint32 profit = auction->bid + auction->deposit - auction->GetAuctionCut();

        // 如果卖家在线，更新成就并发送通知
        //FIXME: what do if owner offline
        if (owner)
        {
            owner->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS, profit);
            owner->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD, auction->bid);
            //send auction owner notification, bidder must be current!
            owner->GetSession()->SendAuctionOwnerNotification(auction);
        }

        // 发送邮件：主题=物品信息，正文=买家和价格详情，附件=利润金币（延迟发送）
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_SUCCESSFUL), AuctionEntry::BuildAuctionSoldMailBody(ObjectGuid::Create<HighGuid::Player>(auction->bidder), auction->bid, auction->buyout, auction->deposit, auction->GetAuctionCut()))
            .AddMoney(profit)
            .SendMailTo(trans, MailReceiver(owner, auction->owner), auction, MAIL_CHECK_MASK_COPIED, sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY));
    }
}

/**
 * @brief 发送拍卖过期邮件给卖家
 * @param auction 拍卖条目
 * @param trans 数据库事务
 *
 * @note 将物品退回给卖家（无人竞拍或拍卖到期）
 *       如果卖家不存在，删除物品
 *
 * @注意 此函数不释放内存，由调用者负责清理拍卖条目
 */
void AuctionHouseMgr::SendAuctionExpiredMail(AuctionEntry* auction, CharacterDatabaseTransaction trans)
{
    //return an item in auction to its owner by mail
    Item* pItem = GetAItem(auction->itemGUIDLow);
    if (!pItem)
        return;

    ObjectGuid owner_guid(HighGuid::Player, auction->owner);
    Player* owner = ObjectAccessor::FindConnectedPlayer(owner_guid);
    uint32 owner_accId = sCharacterCache->GetCharacterAccountIdByGuid(owner_guid);

    // 检查卖家是否存在且不是机器人
    // owner exist
    if ((owner || owner_accId) && !sAuctionBotConfig->IsBotChar(auction->owner))
    {
        // 如果卖家在线，发送通知
        if (owner)
            owner->GetSession()->SendAuctionOwnerNotification(auction);

        // 发送邮件：主题=物品信息，正文=空，附件=物品（立即发送）
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_EXPIRED), "")
            .AddItem(pItem)
            .SendMailTo(trans, MailReceiver(owner, auction->owner), auction, MAIL_CHECK_MASK_COPIED, 0);
    }
    else
    {
        // 卖家不存在，删除物品
        // owner doesn't exist, delete the item
        sAuctionMgr->RemoveAItem(auction->itemGUIDLow, true, &trans);
    }
}

/**
 * @brief 发送被超价邮件给原竞拍者
 * @param auction 拍卖条目
 * @param newPrice 新的出价金额
 * @param newBidder 新的竞拍者
 * @param trans 数据库事务
 *
 * @note 当有人出更高价格时，将原竞拍者的金币退还
 *       同时发送通知告诉原竞拍者被超价
 */
void AuctionHouseMgr::SendAuctionOutbiddedMail(AuctionEntry* auction, uint32 newPrice, Player* newBidder, CharacterDatabaseTransaction trans)
{
    ObjectGuid oldBidder_guid(HighGuid::Player, auction->bidder);
    Player* oldBidder = ObjectAccessor::FindConnectedPlayer(oldBidder_guid);

    uint32 oldBidder_accId = 0;
    if (!oldBidder)
        oldBidder_accId = sCharacterCache->GetCharacterAccountIdByGuid(oldBidder_guid);

    // 检查原竞拍者是否存在且不是机器人
    // old bidder exist
    if ((oldBidder || oldBidder_accId) && !sAuctionBotConfig->IsBotChar(auction->bidder))
    {
        // 如果原竞拍者和新竞拍者都在线，发送超价通知
        if (oldBidder && newBidder)
            oldBidder->GetSession()->SendAuctionBidderNotification(auction->GetHouseId(), auction->Id, newBidder->GetGUID(), newPrice, auction->GetAuctionOutBid(), auction->itemEntry);

        // 发送邮件：主题=物品信息，正文=空，附件=退回的金币
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_OUTBIDDED), "")
            .AddMoney(auction->bid)
            .SendMailTo(trans, MailReceiver(oldBidder, auction->bidder), auction, MAIL_CHECK_MASK_COPIED);
    }
}

/**
 * @brief 发送拍卖取消邮件给竞拍者
 * @param auction 拍卖条目
 * @param trans 数据库事务
 *
 * @note 当卖家取消拍卖时，退还竞拍者的金币
 */
void AuctionHouseMgr::SendAuctionCancelledToBidderMail(AuctionEntry* auction, CharacterDatabaseTransaction trans)
{
    ObjectGuid bidder_guid = ObjectGuid(HighGuid::Player, auction->bidder);
    Player* bidder = ObjectAccessor::FindConnectedPlayer(bidder_guid);

    uint32 bidder_accId = 0;
    if (!bidder)
        bidder_accId = sCharacterCache->GetCharacterAccountIdByGuid(bidder_guid);

    // 检查竞拍者是否存在且不是机器人
    // bidder exist
    if ((bidder || bidder_accId) && !sAuctionBotConfig->IsBotChar(auction->bidder))
        MailDraft(auction->BuildAuctionMailSubject(AUCTION_CANCELLED_TO_BIDDER), "")
            .AddMoney(auction->bid)
            .SendMailTo(trans, MailReceiver(bidder, auction->bidder), auction, MAIL_CHECK_MASK_COPIED);
}

/**
 * @brief 加载拍卖物品
 *
 * @note 从数据库加载所有拍卖中的物品实例
 *       必须在LoadAuctions之前调用
 *
 * @调用时机 服务器启动时
 *
 * @处理流程：
 *       1. 清理现有物品映射（重新加载时）
 *       2. 查询拍卖物品数据（item_instance表）
 *       3. 遍历结果集，创建物品对象
 *       4. 验证物品模板是否存在
 *       5. 加载物品数据到内存
 *       6. 添加到物品映射表
 */
void AuctionHouseMgr::LoadAuctionItems()
{
    uint32 oldMSTime = getMSTime();

    // 如果是重新加载，先清理现有数据
    // need to clear in case we are reloading
    if (!mAitems.empty())
    {
        for (ItemMap::iterator itr = mAitems.begin(); itr != mAitems.end(); ++itr)
            delete itr->second;

        mAitems.clear();
    }

    // 查询拍卖物品数据（字段顺序必须与Item::LoadFromDB匹配）
    // data needs to be at first place for Item::LoadFromDB
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_AUCTION_ITEMS);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 auction items. DB table `auctionhouse` or `item_instance` is empty!");

        return;
    }

    uint32 count = 0;

    // 遍历查询结果，加载每个物品
    do
    {
        Field* fields = result->Fetch();

        // 获取物品GUID和模板ID
        ObjectGuid::LowType item_guid = fields[11].GetUInt32();
        uint32 itemEntry    = fields[12].GetUInt32();

        // 验证物品模板是否存在
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
        if (!proto)
        {
            TC_LOG_ERROR("misc", "AuctionHouseMgr::LoadAuctionItems: Unknown item (GUID: {} item entry: #{}) in auction, skipped.", item_guid, itemEntry);
            continue;
        }

        // 创建物品对象（可能是背包或普通物品）
        Item* item = NewItemOrBag(proto);

        // 从数据库字段加载物品数据
        if (!item->LoadFromDB(item_guid, ObjectGuid::Empty, fields, itemEntry))
        {
            delete item;
            continue;
        }

        // 添加到物品映射表
        AddAItem(item);

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} auction items in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

}

/**
 * @brief 加载拍卖数据
 *
 * @note 从数据库加载所有拍卖条目和竞拍者信息
 *       必须在LoadAuctionItems之后调用（需要验证物品存在）
 *
 * @调用时机 服务器启动时
 *
 * @处理流程：
 *       1. 查询拍卖条目数据（auctionhouse表）
 *       2. 查询竞拍者数据（auctionbidders表）
 *       3. 遍历拍卖数据，创建拍卖条目
 *       4. 验证拍卖行ID和物品是否存在
 *       5. 关联竞拍者信息
 *       6. 添加到对应的拍卖行实例
 */
void AuctionHouseMgr::LoadAuctions()
{
    uint32 oldMSTime = getMSTime();

    // 查询拍卖条目数据
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_AUCTIONS);
    PreparedQueryResult resultAuctions = CharacterDatabase.Query(stmt);

    if (!resultAuctions)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 auctions. DB table `auctionhouse` is empty.");

        return;
    }

    // 解析竞拍者列表（一个拍卖可能有多个竞拍者）
    // parse bidder list
    std::unordered_map<uint32, std::unordered_set<ObjectGuid>> biddersByAuction;
    CharacterDatabasePreparedStatement* stmt2 = CharacterDatabase.GetPreparedStatement(CHAR_SEL_AUCTION_BIDDERS);

    uint32 countBidders = 0;
    if (PreparedQueryResult resultBidders = CharacterDatabase.Query(stmt2))
    {
        do
        {
            Field* fields = resultBidders->Fetch();
            // 映射：拍卖ID -> 竞拍者GUID集合
            biddersByAuction[fields[0].GetUInt32()].insert(ObjectGuid::Create<HighGuid::Player>(fields[1].GetUInt32()));
            ++countBidders;
        }
        while (resultBidders->NextRow());
    }

    // 解析拍卖数据并添加到拍卖行
    // parse auctions from db
    uint32 countAuctions = 0;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    do
    {
        Field* fields = resultAuctions->Fetch();

        // 创建拍卖条目对象
        AuctionEntry* aItem = new AuctionEntry();

        // 从数据库字段加载拍卖数据
        if (!aItem->LoadFromDB(fields))
        {
            // 数据无效，删除数据库记录并清理内存
            aItem->DeleteFromDB(trans);
            delete aItem;
            continue;
        }

        // 关联竞拍者列表
        auto it = biddersByAuction.find(aItem->Id);
        if (it != biddersByAuction.end())
            aItem->bidders = std::move(it->second);

        // 添加到对应的拍卖行实例
        GetAuctionsMapByHouseId(aItem->houseId)->AddAuction(aItem);
        ++countAuctions;
    } while (resultAuctions->NextRow());

    CharacterDatabase.CommitTransaction(trans);

    TC_LOG_INFO("server.loading", ">> Loaded {} auctions with {} bidders in {} ms", countAuctions, countBidders, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 添加拍卖物品到管理器
 * @param it 物品对象指针
 *
 * @note 物品必须存在且GUID在映射表中唯一
 *       使用断言确保数据一致性
 */
void AuctionHouseMgr::AddAItem(Item* it)
{
    ASSERT(it);
    ASSERT(mAitems.find(it->GetGUID().GetCounter()) == mAitems.end());
    mAitems[it->GetGUID().GetCounter()] = it;
}

/**
 * @brief 从管理器移除拍卖物品
 * @param id 物品GUID低端部分
 * @param deleteItem 是否从数据库删除物品（默认false）
 * @param trans 数据库事务指针（deleteItem为true时必须提供）
 * @return 移除成功返回true，物品不存在返回false
 *
 * @note 如果deleteItem为true，会标记物品为删除状态并保存到数据库
 */
bool AuctionHouseMgr::RemoveAItem(ObjectGuid::LowType id, bool deleteItem /*= false*/, CharacterDatabaseTransaction* trans /*= nullptr*/)
{
    ItemMap::iterator i = mAitems.find(id);
    if (i == mAitems.end())
        return false;

    // 如果需要删除物品，标记状态并保存
    if (deleteItem)
    {
        ASSERT(trans);
        i->second->FSetState(ITEM_REMOVED);
        i->second->SaveToDB(*trans);
    }

    // 从映射表中移除
    mAitems.erase(i);
    return true;
}

/**
 * @brief 添加待确认拍卖
 * @param player 玩家对象
 * @param aEntry 拍卖条目
 * @return 成功添加返回true，玩家金币不足返回false
 *
 * @note 拍卖在创建时先放入待确认队列
 *       系统会检查玩家是否有足够金币支付所有待确认拍卖的保证金
 *       在下一个更新周期才会扣除金币并正式创建拍卖
 */
bool AuctionHouseMgr::PendingAuctionAdd(Player* player, AuctionEntry* aEntry)
{
    PlayerAuctions* thisAH;
    auto itr = pendingAuctionMap.find(player->GetGUID());

    // 如果玩家已有待确认拍卖
    if (itr != pendingAuctionMap.end())
    {
        thisAH = itr->second.first;

        // 计算已有待确认拍卖的总保证金
        // Get deposit so far
        uint32 totalDeposit = 0;
        for (AuctionEntry const* thisAuction : *thisAH)
            totalDeposit += thisAuction->deposit;

        // 加上新拍卖的保证金
        // Add this deposit
        totalDeposit += aEntry->deposit;

        // 检查玩家是否有足够金币
        if (!player->HasEnoughMoney(totalDeposit))
            return false;
    }
    else
    {
        // 创建新的待确认拍卖列表
        thisAH = new PlayerAuctions;
        pendingAuctionMap[player->GetGUID()] = AuctionPair(thisAH, 0);
    }

    // 添加拍卖到待确认列表
    thisAH->push_back(aEntry);
    return true;
}

/**
 * @brief 获取玩家待确认拍卖数量
 * @param player 玩家对象
 * @return 待确认拍卖数量
 */
uint32 AuctionHouseMgr::PendingAuctionCount(Player const* player) const
{
    auto const itr = pendingAuctionMap.find(player->GetGUID());
    if (itr != pendingAuctionMap.end())
        return itr->second.first->size();

    return 0;
}

/**
 * @brief 处理玩家待确认拍卖
 * @param player 玩家对象
 *
 * @note 处理流程：
 *       1. 计算玩家能支付多少个拍卖的保证金
 *       2. 立即过期无法支付的拍卖（金币不足）
 *       3. 扣除能够支付的拍卖的保证金
 *       4. 清理待确认映射
 *
 * @调用时机 玩家下次更新时或UpdatePendingAuctions调用时
 */
void AuctionHouseMgr::PendingAuctionProcess(Player* player)
{
    auto iterMap = pendingAuctionMap.find(player->GetGUID());
    if (iterMap == pendingAuctionMap.end())
        return;

    PlayerAuctions* thisAH = iterMap->second.first;

    // 计算玩家能够支付的保证金总额
    uint32 totaldeposit = 0;
    auto itrAH = thisAH->begin();
    for (; itrAH != thisAH->end(); ++itrAH)
    {
        AuctionEntry* AH = (*itrAH);
        // 检查玩家是否有足够金币支付下一个拍卖
        if (!player->HasEnoughMoney(totaldeposit + AH->deposit))
            break;

        totaldeposit += AH->deposit;
    }

    // 过期无法支付的拍卖
    // expire auctions we cannot afford
    if (itrAH != thisAH->end())
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        do
        {
            // 立即设置过期时间为当前时间
            AuctionEntry* AH = (*itrAH);
            AH->expire_time = GameTime::GetGameTime();
            AH->DeleteFromDB(trans);
            AH->SaveToDB(trans);
            ++itrAH;
        } while (itrAH != thisAH->end());

        CharacterDatabase.CommitTransaction(trans);
    }

    // 清理待确认映射并扣除保证金
    pendingAuctionMap.erase(player->GetGUID());
    delete thisAH;
    player->ModifyMoney(-int32(totaldeposit));
}

/**
 * @brief 更新所有待确认拍卖
 *
 * @note 处理流程：
 *       1. 遍历所有待确认拍卖映射
 *       2. 检查玩家是否在线
 *       3. 如果在线且拍卖数量未变化，处理待确认拍卖
 *       4. 如果玩家离线，立即过期所有待确认拍卖
 *
 * @调用时机 服务器主循环定期调用
 * @性能注意 可能处理多个玩家的待确认拍卖
 */
void AuctionHouseMgr::UpdatePendingAuctions()
{
    for (auto itr = pendingAuctionMap.begin(); itr != pendingAuctionMap.end();)
    {
        ObjectGuid playerGUID = itr->first;
        if (Player* player = ObjectAccessor::FindConnectedPlayer(playerGUID))
        {
            // 玩家在线
            // Check if there were auctions since last update process if not
            if (PendingAuctionCount(player) == itr->second.second)
            {
                // 拍卖数量未变化，可以处理
                ++itr;
                PendingAuctionProcess(player);
            }
            else
            {
                // 拍卖数量有变化，更新计数，等待下次处理
                ++itr;
                pendingAuctionMap[playerGUID].second = PendingAuctionCount(player);
            }
        }
        else
        {
            // 玩家离线，立即过期所有待确认拍卖
            // Expire any auctions that we couldn't get a deposit for
            TC_LOG_WARN("auctionHouse", "Player {} was offline, unable to retrieve deposit!", playerGUID.ToString());
            PlayerAuctions* thisAH = itr->second.first;
            ++itr;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            for (auto AHitr = thisAH->begin(); AHitr != thisAH->end();)
            {
                AuctionEntry* AH = (*AHitr);
                ++AHitr;
                // 设置过期时间为当前时间
                AH->expire_time = GameTime::GetGameTime();
                AH->DeleteFromDB(trans);
                AH->SaveToDB(trans);
            }
            CharacterDatabase.CommitTransaction(trans);
            pendingAuctionMap.erase(playerGUID);
            delete thisAH;
        }
    }
}

/**
 * @brief 更新所有拍卖行
 *
 * @note 更新三个拍卖行实例（部落、联盟、中立）
 *       每个拍卖行会处理自己的过期拍卖
 *
 * @调用时机 服务器主循环每次世界更新时
 */
void AuctionHouseMgr::Update()
{
    mHordeAuctions.Update();      // 更新部落拍卖行
    mAllianceAuctions.Update();   // 更新联盟拍卖行
    mNeutralAuctions.Update();    // 更新中立拍卖行
}

/**
 * @brief 根据阵营模板ID获取拍卖行配置
 * @param factionTemplateId 阵营模板ID
 * @return 拍卖行配置条目指针
 *
 * @note 决策逻辑：
 *       1. 如果允许跨阵营交易，返回中立拍卖行配置
 *       2. 否则根据阵营分组返回对应拍卖行配置：
 *          - 联盟阵营 -> 联盟拍卖行（暴风城）
 *          - 部落阵营 -> 部落拍卖行（奥格瑞玛）
 *          - 其他 -> 中立拍卖行（地精拍卖行）
 *
 * @FIXME AuctionHouse.dbc中有阵营字段，对应拍卖行种族的玩家阵营
 *        但没有简单的方法将生物阵营转换为特定城市的玩家种族阵营
 */
AuctionHouseEntry const* AuctionHouseMgr::GetAuctionHouseEntry(uint32 factionTemplateId)
{
    uint32 houseid = AUCTIONHOUSE_NEUTRAL; // goblin auction house（默认中立拍卖行）

    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        // FIXME: found way for proper auctionhouse selection by another way
        // AuctionHouse.dbc have faction field with _player_ factions associated with auction house races.
        // but no easy way convert creature faction to player race faction for specific city

        // 查找阵营模板
        FactionTemplateEntry const* u_entry = sFactionTemplateStore.LookupEntry(factionTemplateId);
        if (!u_entry)
            houseid = AUCTIONHOUSE_NEUTRAL; // goblin auction house（中立拍卖行）
        else if (u_entry->FactionGroup & FACTION_MASK_ALLIANCE)
            houseid = AUCTIONHOUSE_ALLIANCE; // human auction house（联盟拍卖行）
        else if (u_entry->FactionGroup & FACTION_MASK_HORDE)
            houseid = AUCTIONHOUSE_HORDE; // orc auction house（部落拍卖行）
        else
            houseid = AUCTIONHOUSE_NEUTRAL; // goblin auction house（中立拍卖行）
    }

    return sAuctionHouseStore.LookupEntry(houseid);
}

/**
 * @brief 根据拍卖行ID获取拍卖行配置
 * @param houseId 拍卖行ID
 * @return 拍卖行配置条目指针
 *
 * @note 如果允许跨阵营交易，所有ID都返回中立拍卖行配置
 */
AuctionHouseEntry const* AuctionHouseMgr::GetAuctionHouseEntryFromHouse(uint8 houseId)
{
    return (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION)) ? sAuctionHouseStore.LookupEntry(AUCTIONHOUSE_NEUTRAL) : sAuctionHouseStore.LookupEntry(houseId);
}

/**
 * @brief 添加拍卖到拍卖行
 * @param auction 拍卖条目指针
 *
 * @note 将拍卖添加到映射表，并触发脚本事件OnAuctionAdd
 */
void AuctionHouseObject::AddAuction(AuctionEntry* auction)
{
    ASSERT(auction);

    // 添加到拍卖映射表
    AuctionsMap[auction->Id] = auction;

    // 触发脚本事件
    sScriptMgr->OnAuctionAdd(this, auction);
}

/**
 * @brief 从拍卖行移除拍卖
 * @param auction 拍卖条目指针
 * @return 如果拍卖在映射中返回true，否则false
 *
 * @note 触发脚本事件OnAuctionRemove，并删除拍卖对象释放内存
 */
bool AuctionHouseObject::RemoveAuction(AuctionEntry* auction)
{
    bool wasInMap = AuctionsMap.erase(auction->Id) ? true : false;

    // 触发脚本事件
    sScriptMgr->OnAuctionRemove(this, auction);

    // we need to delete the entry, it is not referenced any more
    // 删除拍卖对象，释放内存
    delete auction;
    return wasInMap;
}

/**
 * @brief 更新拍卖行状态
 *
 * @note 处理所有即将过期的拍卖（距离过期时间<=60秒）：
 *       1. 清理过期的GetAll限流记录
 *       2. 遍历所有拍卖，检查过期时间
 *       3. 无竞拍者的拍卖：退回物品给卖家
 *       4. 有竞拍者的拍卖：完成交易，发送邮件
 *       5. 从数据库删除拍卖记录
 *       6. 从管理器移除拍卖物品
 *       7. 删除拍卖条目
 *
 * @调用时机 服务器主循环定期调用（每次世界更新）
 * @性能注意 批量数据库操作，一次事务处理所有过期拍卖
 */
void AuctionHouseObject::Update()
{
    time_t curTime = GameTime::GetGameTime();
    ///- Handle expired auctions

    // 如果拍卖行为空，无需更新
    // If storage is empty, no need to update. next == NULL in this case.
    if (AuctionsMap.empty())
        return;

    // 清理过期的GetAll限流记录
    // Clear expired throttled players
    for (PlayerGetAllThrottleMap::const_iterator itr = GetAllThrottleMap.begin(); itr != GetAllThrottleMap.end();)
    {
        if (itr->second <= curTime)
            itr = GetAllThrottleMap.erase(itr);
        else
            ++itr;
    }

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 遍历所有拍卖
    for (AuctionEntryMap::iterator it = AuctionsMap.begin(); it != AuctionsMap.end();)
    {
        // from auctionhousehandler.cpp, creates auction pointer & player pointer
        AuctionEntry* auction = it->second;
        // Increment iterator due to AuctionEntry deletion
        // 预先递增迭代器，因为后续会删除当前拍卖
        ++it;

        ///- filter auctions expired on next update
        // 只处理即将过期的拍卖（60秒内）
        if (auction->expire_time > curTime + 60)
            continue;

        ///- Either cancel the auction if there was no bidder
        // 无竞拍者的拍卖：退回物品
        if (auction->bidder == 0 && auction->bid == 0)
        {
            sAuctionMgr->SendAuctionExpiredMail(auction, trans);
            sScriptMgr->OnAuctionExpire(this, auction);
        }
        ///- Or perform the transaction
        // 有竞拍者的拍卖：完成交易
        else
        {
            //we should send an "item sold" message if the seller is online
            //we send the item to the winner
            //we send the money to the seller
            sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
            sAuctionMgr->SendAuctionWonMail(auction, trans);
            sScriptMgr->OnAuctionSuccessful(this, auction);
        }

        ///- In any case clear the auction
        // 从数据库删除拍卖记录
        auction->DeleteFromDB(trans);

        // 从物品管理器移除物品
        sAuctionMgr->RemoveAItem(auction->itemGUIDLow);

        // 从拍卖行移除拍卖条目（会删除对象）
        RemoveAuction(auction);
    }

    // Run DB changes
    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 构建玩家竞拍列表数据包
 * @param data 输出的网络数据包
 * @param player 玩家对象
 * @param count 返回实际发送的拍卖数量
 * @param totalcount 返回匹配的拍卖总数
 *
 * @note 遍历所有拍卖，查找玩家参与竞拍的物品
 *       count：实际写入数据包的拍卖数量
 *       totalcount：玩家参与竞拍的拍卖总数
 */
void AuctionHouseObject::BuildListBidderItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount)
{
    for (AuctionEntryMap::const_iterator itr = AuctionsMap.begin(); itr != AuctionsMap.end(); ++itr)
    {
        AuctionEntry* Aentry = itr->second;
        // 检查玩家是否参与了此拍卖的竞拍
        if (Aentry && Aentry->bidders.find(player->GetGUID()) != Aentry->bidders.end())
        {
            // 构建拍卖信息并写入数据包
            if (itr->second->BuildAuctionInfo(data))
                ++count;

            ++totalcount;
        }
    }
}

/**
 * @brief 构建玩家出售列表数据包
 * @param data 输出的网络数据包
 * @param player 玩家对象
 * @param count 返回实际发送的拍卖数量
 * @param totalcount 返回匹配的拍卖总数
 *
 * @note 遍历所有拍卖，查找玩家出售的物品
 *       count：实际写入数据包的拍卖数量
 *       totalcount：玩家出售的拍卖总数
 */
void AuctionHouseObject::BuildListOwnerItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount)
{
    for (AuctionEntryMap::const_iterator itr = AuctionsMap.begin(); itr != AuctionsMap.end(); ++itr)
    {
        AuctionEntry* Aentry = itr->second;
        // 检查拍卖的卖家是否为该玩家
        if (Aentry && Aentry->owner == player->GetGUID().GetCounter())
        {
            // 构建拍卖信息并写入数据包
            if (Aentry->BuildAuctionInfo(data))
                ++count;

            ++totalcount;
        }
    }
}

/**
 * @brief 构建拍卖物品搜索结果数据包
 * @param data 输出的网络数据包
 * @param player 玩家对象
 * @param wsearchedname 搜索的物品名称（宽字符）
 * @param listfrom 起始位置（分页偏移）
 * @param levelmin 最小等级要求
 * @param levelmax 最大等级要求
 * @param usable 是否只显示可使用的物品
 * @param inventoryType 装备类型（背包位置）
 * @param itemClass 物品大类
 * @param itemSubClass 物品子类
 * @param quality 物品品质
 * @param count 返回实际发送的拍卖数量
 * @param totalcount 返回匹配的拍卖总数
 * @param getall 是否使用GetAll模式（获取所有拍卖）
 *
 * @note GetAll模式：
 *       - 返回所有拍卖（不应用筛选条件）
 *       - 有限流机制：CONFIG_AUCTION_GETALL_DELAY配置的冷却时间
 *       - 最多返回MAX_GETALL_RETURN条记录
 *       - 适合客户端首次加载拍卖行
 *
 * @note 普通搜索模式：
 *       - 支持多种筛选条件：物品类别、等级、品质、名称等
 *       - 支持随机属性后缀搜索（如"of the Monkey"）
 *       - 最多返回50条记录（分页显示）
 *       - 支持本地化搜索
 *
 * @性能注意 GetAll模式可能返回大量数据（MAX_GETALL_RETURN=55000）
 */
void AuctionHouseObject::BuildListAuctionItems(WorldPacket& data, Player* player,
    std::wstring const& wsearchedname, uint32 listfrom, uint8 levelmin, uint8 levelmax, uint8 usable,
    uint32 inventoryType, uint32 itemClass, uint32 itemSubClass, uint32 quality,
    uint32& count, uint32& totalcount, bool getall)
{
    // 获取玩家的本地化设置
    LocaleConstant localeConstant = player->GetSession()->GetSessionDbLocaleIndex();
    int locdbc_idx = player->GetSession()->GetSessionDbcLocale();

    time_t curTime = GameTime::GetGameTime();

    // 检查GetAll限流状态
    auto itr = GetAllThrottleMap.find(player->GetGUID());
    time_t throttleTime = itr != GetAllThrottleMap.end() ? itr->second : curTime;

    // ========== GetAll模式：返回所有拍卖 ==========
    if (getall && throttleTime <= curTime)
    {
        for (AuctionEntryMap::const_iterator it = AuctionsMap.begin(); it != AuctionsMap.end(); ++it)
        {
            AuctionEntry* Aentry = it->second;
            // Skip expired auctions
            // 跳过已过期的拍卖
            if (Aentry->expire_time < curTime)
                continue;

            Item* item = sAuctionMgr->GetAItem(Aentry->itemGUIDLow);
            if (!item)
                continue;

            ++count;
            ++totalcount;
            Aentry->BuildAuctionInfo(data, item);

            // 达到最大返回数量，停止
            if (count >= MAX_GETALL_RETURN)
                break;
        }
        // 设置限流时间（当前时间 + 延迟配置）
        GetAllThrottleMap[player->GetGUID()] = curTime + sWorld->getIntConfig(CONFIG_AUCTION_GETALL_DELAY);
        return;
    }

    // ========== 普通搜索模式：应用筛选条件 ==========
    for (AuctionEntryMap::const_iterator it = AuctionsMap.begin(); it != AuctionsMap.end(); ++it)
    {
        AuctionEntry* Aentry = it->second;
        // Skip expired auctions
        // 跳过已过期的拍卖
        if (Aentry->expire_time < curTime)
            continue;

        Item* item = sAuctionMgr->GetAItem(Aentry->itemGUIDLow);
        if (!item)
            continue;

        ItemTemplate const* proto = item->GetTemplate();

        // 筛选条件：物品大类
        if (itemClass != 0xffffffff && proto->Class != itemClass)
            continue;

        // 筛选条件：物品子类
        if (itemSubClass != 0xffffffff && proto->SubClass != itemSubClass)
            continue;

        // 筛选条件：装备类型（背包位置）
        if (inventoryType != 0xffffffff && proto->InventoryType != inventoryType)
        {
            // Cloth items can have INVTYPE_CHEST or INVTYPE_ROBE
            // 布甲胸甲可以是INVTYPE_CHEST或INVTYPE_ROBE类型
            if (!(inventoryType == INVTYPE_CHEST && proto->InventoryType == INVTYPE_ROBE))
                continue;
        }

        // 筛选条件：物品品质
        if (quality != 0xffffffff && proto->Quality != quality)
            continue;

        // 筛选条件：等级要求范围
        if (levelmin != 0x00 && (proto->RequiredLevel < levelmin || (levelmax != 0x00 && proto->RequiredLevel > levelmax)))
            continue;

        // 筛选条件：是否可使用
        if (usable != 0x00 && player->CanUseItem(item) != EQUIP_ERR_OK)
            continue;

        // Allow search by suffix (ie: of the Monkey) or partial name (ie: Monkey)
        // No need to do any of this if no search term was entered
        // 名称搜索（支持随机属性后缀搜索）
        if (!wsearchedname.empty())
        {
            std::string name = proto->Name1;
            if (name.empty())
                continue;

            // local name
            // 获取本地化名称
            if (localeConstant != LOCALE_enUS)
                if (ItemLocale const* il = sObjectMgr->GetItemLocale(proto->ItemId))
                    ObjectMgr::GetLocaleString(il->Name, localeConstant, name);

            // DO NOT use GetItemEnchantMod(proto->RandomProperty) as it may return a result
            //  that matches the search but it may not equal item->GetItemRandomPropertyId()
            //  used in BuildAuctionInfo() which then causes wrong items to be listed
            // 获取物品的随机属性ID
            int32 propRefID = item->GetItemRandomPropertyId();

            if (propRefID)
            {
                // Append the suffix to the name (ie: of the Monkey) if one exists
                // These are found in ItemRandomSuffix.dbc and ItemRandomProperties.dbc
                //  even though the DBC names seem misleading
                // 添加随机属性后缀到物品名称

                std::array<char const*, 16> const* suffix = nullptr;

                if (propRefID < 0)
                {
                    // 随机后缀（ItemRandomSuffix.dbc）
                    ItemRandomSuffixEntry const* itemRandSuffix = sItemRandomSuffixStore.LookupEntry(-propRefID);
                    if (itemRandSuffix)
                        suffix = &itemRandSuffix->Name;
                }
                else
                {
                    // 随机属性（ItemRandomProperties.dbc）
                    ItemRandomPropertiesEntry const* itemRandProp = sItemRandomPropertiesStore.LookupEntry(propRefID);
                    if (itemRandProp)
                        suffix = &itemRandProp->Name;
                }

                // dbc local name
                // 添加本地化的后缀名称
                if (suffix)
                {
                    // Append the suffix (ie: of the Monkey) to the name using localization
                    // or default enUS if localization is invalid
                    name += ' ';
                    name += (*suffix)[locdbc_idx >= 0 ? locdbc_idx : LOCALE_enUS];
                }
            }

            // Perform the search (with or without suffix)
            // 执行名称匹配搜索
            if (!Utf8FitTo(name, wsearchedname))
                continue;
        }

        // Add the item if no search term or if entered search term was found
        // 添加到结果列表（分页处理：最多返回50条）
        if (count < 50 && totalcount >= listfrom)
        {
            ++count;
            Aentry->BuildAuctionInfo(data, item);
        }
        ++totalcount;
    }
}

/**
 * @brief 构建拍卖信息数据包
 * @param data 输出的网络数据包
 * @param sourceItem 可选的物品指针，如果为空则从管理器获取
 * @return 构建成功返回true，物品不存在返回false
 *
 * @note 将拍卖的完整信息打包到网络数据包中，发送给客户端
 *       数据包格式：
 *       - 拍卖ID
 *       - 物品模板ID
 *       - 附魔信息（MAX_INSPECTED_ENCHANTMENT_SLOT个槽位）
 *       - 随机属性ID
 *       - 后缀因子
 *       - 物品数量
 *       - 法术充能数
 *       - 物品标志
 *       - 卖家GUID
 *       - 起拍价
 *       - 最小出价增量
 *       - 一口价
 *       - 剩余时间（毫秒）
 *       - 当前竞拍者GUID
 *       - 当前出价
 */
bool AuctionEntry::BuildAuctionInfo(WorldPacket& data, Item* sourceItem) const
{
    //this function inserts to WorldPacket auction's data
    // 获取物品对象
    Item* item = (sourceItem) ? sourceItem : sAuctionMgr->GetAItem(itemGUIDLow);
    if (!item)
    {
        TC_LOG_ERROR("misc", "AuctionEntry::BuildAuctionInfo: Auction {} has a non-existent item: {}", Id, itemGUIDLow);
        return false;
    }

    // 写入拍卖ID
    data << uint32(Id);
    // 写入物品模板ID
    data << uint32(item->GetEntry());

    // 写入附魔信息（每个槽位：ID、持续时间、充能数）
    for (uint8 i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
    {
        data << uint32(item->GetEnchantmentId(EnchantmentSlot(i)));
        data << uint32(item->GetEnchantmentDuration(EnchantmentSlot(i)));
        data << uint32(item->GetEnchantmentCharges(EnchantmentSlot(i)));
    }

    data << int32(item->GetItemRandomPropertyId());                 // Random item property id（随机属性ID）
    data << uint32(item->GetItemSuffixFactor());                    // SuffixFactor（后缀因子）
    data << uint32(item->GetCount());                               // item->count（物品数量）
    data << uint32(item->GetSpellCharges());                        // item->charge FFFFFFF（法术充能）
    data << uint32(item->GetUInt32Value(ITEM_FIELD_FLAGS));         // item flags（物品标志）
    data << uint64(owner);                                          // Auction->owner（卖家GUID）
    data << uint32(startbid);                                       // Auction->startbid (not sure if useful)（起拍价）
    data << uint32(bid ? GetAuctionOutBid() : 0);                   // Minimal outbid（最小出价增量）
    data << uint32(buyout);                                         // Auction->buyout（一口价）
    data << uint32((expire_time - GameTime::GetGameTime()) * IN_MILLISECONDS);   // time left（剩余时间，毫秒）
    data << uint64(bidder);                                         // auction->bidder current（当前竞拍者GUID）
    data << uint32(bid);                                            // current bid（当前出价）
    return true;
}

/**
 * @brief 计算拍卖行手续费
 * @return 手续费金额（金币）
 *
 * @note 计算公式：成交价 × 手续费比例 × 服务器倍率
 *       手续费比例由拍卖行配置决定（ConsignmentRate）
 *       结果不能为负数
 */
uint32 AuctionEntry::GetAuctionCut() const
{
    int32 cut = int32(CalculatePct(bid, auctionHouseEntry->ConsignmentRate) * sWorld->getRate(RATE_AUCTION_CUT));
    return std::max(cut, 0);
}

/**
 * @brief 计算最小出价增量
 * @return 最小出价增量金额（金币）
 *
 * @note 计算公式：当前出价的5%
 *       最小值为1铜币（确保至少有增量）
 *       用于确保新的出价必须比当前价格高出一定金额
 */
/// the sum of outbid is (1% from current bid)*5, if bid is very small, it is 1c
uint32 AuctionEntry::GetAuctionOutBid() const
{
    uint32 outbid = CalculatePct(bid, 5);
    return outbid ? outbid : 1;
}

/**
 * @brief 从数据库删除拍卖记录
 * @param trans 数据库事务
 *
 * @note 同时删除拍卖表（auctionhouse）和竞拍者表（auctionbidders）中的记录
 */
void AuctionEntry::DeleteFromDB(CharacterDatabaseTransaction trans) const
{
    CharacterDatabasePreparedStatement* stmt;

    // 删除拍卖条目
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_AUCTION);
    stmt->setUInt32(0, Id);
    trans->Append(stmt);

    // 删除该拍卖的所有竞拍者记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_AUCTION_BIDDERS);
    stmt->setUInt32(0, Id);
    trans->Append(stmt);
}

/**
 * @brief 将拍卖保存到数据库
 * @param trans 数据库事务
 *
 * @note 执行INSERT操作，新增拍卖记录到auctionhouse表
 *       不保存竞拍者列表（竞拍者通过单独的表管理）
 */
void AuctionEntry::SaveToDB(CharacterDatabaseTransaction trans) const
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_AUCTION);
    stmt->setUInt32(0, Id);                  // 拍卖ID
    stmt->setUInt8(1, houseId);              // 拍卖行ID
    stmt->setUInt32(2, itemGUIDLow);         // 物品GUID低端
    stmt->setUInt32(3, owner);               // 卖家GUID低端
    stmt->setUInt32(4, buyout);              // 一口价
    stmt->setUInt32(5, uint32(expire_time)); // 过期时间
    stmt->setUInt32(6, bidder);              // 当前竞拍者GUID低端
    stmt->setUInt32(7, bid);                 // 当前出价
    stmt->setUInt32(8, startbid);            // 起拍价
    stmt->setUInt32(9, deposit);             // 保证金
    stmt->setUInt8(10, Flags);               // 拍卖标志
    trans->Append(stmt);
}

/**
 * @brief 从数据库字段加载拍卖数据
 * @param fields 数据库查询结果字段数组
 * @return 加载成功返回true，数据无效返回false
 *
 * @note 加载流程：
 *       1. 从字段数组读取所有拍卖属性
 *       2. 验证拍卖行ID是否有效
 *       3. 验证物品是否存在
 *
 * @注意 必须在LoadAuctionItems之后调用，因为需要检查物品是否存在
 */
bool AuctionEntry::LoadFromDB(Field* fields)
{
    // 从数据库字段读取拍卖数据
    Id = fields[0].GetUInt32();          // 拍卖ID
    houseId = fields[1].GetUInt8();      // 拍卖行ID
    itemGUIDLow = fields[2].GetUInt32(); // 物品GUID低端
    itemEntry = fields[3].GetUInt32();   // 物品模板ID
    itemCount = fields[4].GetUInt32();   // 物品数量
    owner = fields[5].GetUInt32();       // 卖家GUID低端
    buyout = fields[6].GetUInt32();      // 一口价
    expire_time = fields[7].GetUInt32(); // 过期时间
    bidder = fields[8].GetUInt32();      // 当前竞拍者GUID低端
    bid = fields[9].GetUInt32();         // 当前出价
    startbid = fields[10].GetUInt32();   // 起拍价
    deposit = fields[11].GetUInt32();    // 保证金
    Flags = AuctionEntryFlag(fields[12].GetUInt8()); // 拍卖标志

    // 验证拍卖行配置是否存在
    auctionHouseEntry = AuctionHouseMgr::GetAuctionHouseEntryFromHouse(houseId);
    if (!auctionHouseEntry)
    {
        TC_LOG_ERROR("misc", "Auction {} has invalid house id {}", Id, houseId);
        return false;
    }

    // check if sold item exists for guid
    // and itemEntry in fact (GetAItem will fail if problematic in result check in AuctionHouseMgr::LoadAuctionItems)
    // 验证物品是否存在
    if (!sAuctionMgr->GetAItem(itemGUIDLow))
    {
        TC_LOG_ERROR("misc", "Auction {} has not a existing item : {}", Id, itemGUIDLow);
        return false;
    }

    return true;
}
/**
 * @brief 构建拍卖邮件主题
 * @param response 邮件响应类型（竞拍成功、过期、取消等）
 * @return 格式化的邮件主题字符串
 *
 * @note 格式：物品ID:随机属性ID:响应类型:拍卖ID:物品数量
 *       例如："12345:0:1:67890:1" 表示物品ID 12345，无随机属性，竞拍成功，拍卖ID 67890，数量1
 */
std::string AuctionEntry::BuildAuctionMailSubject(MailAuctionAnswers response) const
{
    Item* item = sAuctionMgr->GetAItem(itemGUIDLow);
    return Trinity::StringFormat("{}:{}:{}:{}:{}", itemEntry, item ? item->GetItemRandomPropertyId() : 0, response, Id, itemCount);
}

/**
 * @brief 构建竞拍成功邮件正文
 * @param guid 卖家GUID
 * @param bid 成交价格
 * @param buyout 一口价（可能为0）
 * @return 格式化的邮件正文
 *
 * @note 格式：GUID(十六进制):出价:一口价
 *       例如："0x1:1000:0" 表示卖家GUID为1，出价1000金币，无一口价
 */
std::string AuctionEntry::BuildAuctionWonMailBody(ObjectGuid guid, uint32 bid, uint32 buyout)
{
    return Trinity::StringFormat("{:X}:{}:{}", guid.GetRawValue(), bid, buyout);
}

/**
 * @brief 构建拍卖成功邮件正文
 * @param guid 买家GUID
 * @param bid 成交价格
 * @param buyout 一口价
 * @param deposit 保证金
 * @param consignment 手续费
 * @return 格式化的邮件正文
 *
 * @note 格式：GUID(十六进制):出价:一口价:保证金:手续费
 *       例如："0x1:1000:0:50:100" 表示买家GUID为1，出价1000，无一口价，保证金50，手续费100
 */
std::string AuctionEntry::BuildAuctionSoldMailBody(ObjectGuid guid, uint32 bid, uint32 buyout, uint32 deposit, uint32 consignment)
{
    return Trinity::StringFormat("{:X}:{}:{}:{}:{}", guid.GetRawValue(), bid, buyout, deposit, consignment);
}

/**
 * @brief 构建拍卖发票邮件正文
 * @param guid 买家GUID
 * @param bid 成交价格
 * @param buyout 一口价
 * @param deposit 保证金
 * @param consignment 手续费
 * @param moneyDelay 金币到账延迟时间（秒）
 * @param eta 预计到账时间（打包的WowTime）
 * @return 格式化的邮件正文
 *
 * @note 格式：GUID(十六进制):出价:一口价:保证金:手续费:延迟时间:预计到账时间
 *       例如："0x1:1000:0:50:100:3600:1234567890"
 *       用于销售待处理邮件，告知卖家金币将延迟到账
 */
std::string AuctionEntry::BuildAuctionInvoiceMailBody(ObjectGuid guid, uint32 bid, uint32 buyout, uint32 deposit, uint32 consignment, uint32 moneyDelay, uint32 eta)
{
    return Trinity::StringFormat("{:X}:{}:{}:{}:{}:{}:{}", guid.GetRawValue(), bid, buyout, deposit, consignment, moneyDelay, eta);
}
