/**
 * @file AuctionHouseHandler.cpp
 * @brief 拍卖行系统网络包处理器实现
 *
 * 本文件实现了拍卖行系统的所有网络消息处理功能，包括：
 * - 拍卖行交互初始化（打开拍卖行窗口）
 * - 创建拍卖（上架物品）
 * - 竞价和一口价购买
 * - 取消拍卖
 * - 搜索拍卖物品
 * - 查看竞拍列表和我的拍卖列表
 *
 * 拍卖行系统允许玩家在游戏中买卖物品，支持：
 * - 单物品和多物品堆叠拍卖
 * - 设置起拍价和一口价
 * - 多种搜索筛选条件
 * - 邮件通知系统
 * - 拍卖行手续费和押金机制
 *
 * 主要相关类：
 * - WorldSession: 处理网络消息的会话类
 * - AuctionHouseMgr: 拍卖行管理器
 * - AuctionHouseObject: 拍卖行对象，存储拍卖条目
 * - AuctionEntry: 单个拍卖条目数据结构
 *
 * @copyright This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
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

#include "WorldSession.h"
#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "CharacterCache.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStructure.h"
#include "GameTime.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "UpdateMask.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理玩家与拍卖师NPC交互的网络包
 *
 * 职责：
 *   当玩家点击拍卖师NPC时调用此函数,处理拍卖行窗口的打开请求。
 *   验证玩家是否可以与拍卖师交互,并发送拍卖行欢迎消息。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含拍卖师的GUID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 从网络包中读取拍卖师NPC的GUID
 *   2. 验证玩家是否可以与该NPC交互(距离、NPC标志等)
 *   3. 如果玩家处于假死状态,移除假死光环
 *   4. 发送拍卖行欢迎消息,打开拍卖行窗口
 */
void WorldSession::HandleAuctionHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;                                            //NPC guid
    recvData >> guid;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionHelloOpcode - Unit ({}) not found or you can't interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendAuctionHello(guid, unit);
}

/**
 * @brief 发送拍卖行欢迎消息,打开拍卖行窗口
 *
 * 职责：
 *   向客户端发送拍卖行初始化数据,包括拍卖师GUID和拍卖行ID。
 *   验证玩家等级是否满足使用拍卖行的要求。
 *
 * 参数：
 *   @param guid [in] 拍卖师NPC的GUID
 *   @param unit [in] 拍卖师NPC对象指针
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 检查玩家等级是否满足拍卖行使用要求
 *   2. 根据拍卖师的阵营获取对应的拍卖行配置
 *   3. 构建并发送MSG_AUCTION_HELLO消息包
 *   4. 消息包含拍卖师GUID、拍卖行ID和启用状态标志
 */
void WorldSession::SendAuctionHello(ObjectGuid guid, Creature* unit)
{
    if (GetPlayer()->GetLevel() < sWorld->getIntConfig(CONFIG_AUCTION_LEVEL_REQ))
    {
        SendNotification(GetTrinityString(LANG_AUCTION_REQ), sWorld->getIntConfig(CONFIG_AUCTION_LEVEL_REQ));
        return;
    }

    AuctionHouseEntry const* ahEntry = AuctionHouseMgr::GetAuctionHouseEntry(unit->GetFaction());
    if (!ahEntry)
        return;

    WorldPacket data(MSG_AUCTION_HELLO, 12);
    data << uint64(guid);
    data << uint32(ahEntry->ID);
    data << uint8(1);                                       // 3.3.3: 1 - AH enabled, 0 - AH disabled
    SendPacket(&data);
}

/**
 * @brief 发送拍卖行命令执行结果
 *
 * 职责：
 *   向客户端发送拍卖操作的执行结果,包括出价、创建或删除拍卖的结果。
 *   用于通知客户端拍卖操作是否成功及失败原因。
 *
 * 参数：
 *   @param auctionItemId [in] 拍卖项ID
 *   @param command       [in] 执行的拍卖命令类型(创建、出价、取消等)
 *   @param errorCode     [in] 操作结果错误码
 *   @param bagResult     [in] 背包相关错误码(仅在ERR_AUCTION_INVENTORY错误时发送)
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 构建SMSG_AUCTION_COMMAND_RESULT消息包
 *   2. 写入拍卖项ID、命令类型和错误码
 *   3. 如果是背包错误,额外写入背包错误码
 *   4. 发送消息包给客户端
 */
void WorldSession::SendAuctionCommandResult(uint32 auctionItemId, AuctionAction command, AuctionError errorCode, InventoryResult bagResult)
{
    WorldPacket data(SMSG_AUCTION_COMMAND_RESULT, 16);
    data << int32(auctionItemId);
    data << int32(command);
    data << int32(errorCode);
    if (errorCode == ERR_AUCTION_INVENTORY)
        data << int32(bagResult);
    SendPacket(&data);
}

/**
 * @brief 发送竞价者通知消息
 *
 * 职责：
 *   当拍卖被其他人出价时,向在线的竞价者发送通知。
 *   通知内容包括拍卖位置、拍卖ID、出价者、出价金额等信息。
 *
 * 参数：
 *   @param location  [in] 拍卖行位置ID
 *   @param auctionId [in] 拍卖ID
 *   @param bidder    [in] 出价者GUID
 *   @param bidSum    [in] 出价总金额
 *   @param diff      [in] 出价差额
 *   @param itemEntry [in] 物品模板ID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 构建SMSG_AUCTION_BIDDER_NOTIFICATION消息包
 *   2. 写入拍卖位置、拍卖ID、出价者GUID等信息
 *   3. 发送消息包通知竞价者
 */
void WorldSession::SendAuctionBidderNotification(uint32 location, uint32 auctionId, ObjectGuid bidder, uint32 bidSum, uint32 diff, uint32 itemEntry)
{
    WorldPacket data(SMSG_AUCTION_BIDDER_NOTIFICATION, (8*4));
    data << uint32(location);
    data << uint32(auctionId);
    data << uint64(bidder);
    data << uint32(bidSum);
    data << uint32(diff);
    data << uint32(itemEntry);
    data << uint32(0);
    SendPacket(&data);
}

/**
 * @brief 发送拍卖所有者通知消息
 *
 * 职责：
 *   当拍卖售出时,向拍卖所有者发送通知消息。
 *   在客户端显示"你的拍卖已售出"提示。
 *
 * 参数：
 *   @param auction [in] 拍卖条目指针,包含拍卖的详细信息
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 构建SMSG_AUCTION_OWNER_NOTIFICATION消息包
 *   2. 写入拍卖ID、出价金额、物品模板ID等信息
 *   3. 发送消息包通知拍卖所有者
 */
void WorldSession::SendAuctionOwnerNotification(AuctionEntry* auction)
{
    WorldPacket data(SMSG_AUCTION_OWNER_NOTIFICATION, (8*4));
    data << uint32(auction->Id);
    data << uint32(auction->bid);
    data << uint32(0);                                      //unk
    data << uint64(0);                                      //unk (bidder guid?)
    data << uint32(auction->itemEntry);
    data << uint32(0);                                      //unk
    data << float(0);                                       //unk (time?)
    SendPacket(&data);
}

/**
 * @brief 处理玩家创建拍卖的网络包
 *
 * 职责：
 *   处理玩家在拍卖行创建新拍卖的请求。支持单个或多个物品堆叠拍卖,
 *   验证物品的合法性、扣除押金、创建拍卖条目并保存到数据库。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - auctioneer: 拍卖师GUID
 *                       - itemsCount: 物品数量
 *                       - itemGUIDs[]: 物品GUID数组
 *                       - count[]: 各物品的数量数组
 *                       - bid: 起拍价
 *                       - buyout: 一口价
 *                       - etime: 拍卖时长(分钟)
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取并验证网络包数据的有效性
 *   2. 验证拍卖师NPC是否可交互
 *   3. 验证物品是否存在、可交易、未被拍卖等条件
 *   4. 检查物品堆叠数量和重复GUID
 *   5. 计算并扣除拍卖押金
 *   6. 创建拍卖条目并设置相关属性
 *   7. 处理物品转移:
 *      - 如果物品数量匹配,直接转移到拍卖行
 *      - 如果需要拆分,克隆物品并更新原物品堆叠数
 *   8. 保存拍卖和物品数据到数据库
 *   9. 发送创建成功消息并更新成就进度
 */
void WorldSession::HandleAuctionSellItem(WorldPacket& recvData)
{
    ObjectGuid auctioneer;
    uint32 itemsCount, etime, bid, buyout;
    recvData >> auctioneer;
    recvData >> itemsCount;

    ObjectGuid itemGUIDs[MAX_AUCTION_ITEMS]; // 160 slot = 4x 36 slot bag + backpack 16 slot
    uint32 count[MAX_AUCTION_ITEMS];
    memset(count, 0, sizeof(count));

    if (itemsCount > MAX_AUCTION_ITEMS)
    {
        SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
        recvData.rfinish();
        return;
    }

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        recvData >> itemGUIDs[i];
        recvData >> count[i];

        if (!itemGUIDs[i] || !count[i] || count[i] > 1000)
        {
            recvData.rfinish();
            return;
        }
    }

    recvData >> bid;
    recvData >> buyout;
    recvData >> etime;

    if (!bid || !etime)
        return;

    if (bid > MAX_MONEY_AMOUNT || buyout > MAX_MONEY_AMOUNT)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionSellItem - Player {} {} attempted to sell item with higher price than max gold amount.", _player->GetName(), _player->GetGUID().ToString());
        SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
        return;
    }

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneer, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionSellItem - Unit ({}) not found or you can't interact with him.", auctioneer.ToString());
        return;
    }

    AuctionHouseEntry const* auctionHouseEntry = AuctionHouseMgr::GetAuctionHouseEntry(creature->GetFaction());
    if (!auctionHouseEntry)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionSellItem - Unit ({}) has wrong faction.", auctioneer.ToString());
        return;
    }

    etime *= MINUTE;

    switch (etime)
    {
        case 1*MIN_AUCTION_TIME:
        case 2*MIN_AUCTION_TIME:
        case 4*MIN_AUCTION_TIME:
            break;
        default:
            return;
    }

    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    Item* items[MAX_AUCTION_ITEMS];

    uint32 finalCount = 0;
    uint32 itemEntry = 0;

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        Item* item = _player->GetItemByGuid(itemGUIDs[i]);

        if (!item)
        {
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_ITEM_NOT_FOUND);
            return;
        }

        if (itemEntry == 0)
            itemEntry = item->GetTemplate()->ItemId;

        if (sAuctionMgr->GetAItem(item->GetGUID().GetCounter()) || !item->CanBeTraded() || item->IsNotEmptyBag() ||
            item->GetTemplate()->HasFlag(ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION) ||
            item->GetCount() < count[i] || itemEntry != item->GetTemplate()->ItemId)
        {
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            return;
        }

        items[i] = item;
        finalCount += count[i];
    }

    if (!finalCount)
    {
        SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
        return;
    }

    // 检查是否存在重复的GUID,如果存在则可能是作弊行为
    for (uint32 i = 0; i < itemsCount - 1; ++i)
    {
        for (uint32 j = i + 1; j < itemsCount; ++j)
        {
            if (itemGUIDs[i] == itemGUIDs[j])
            {
                SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
                return;
            }
        }
    }

    for (uint32 i = 0; i < itemsCount; ++i)
    {
        Item* item = items[i];

        if (item->GetMaxStackCount() < finalCount)
        {
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            return;
        }
    }

    Item* item = items[0];

    uint32 auctionTime = uint32(etime * sWorld->getRate(RATE_AUCTION_TIME));
    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    uint32 deposit = sAuctionMgr->GetAuctionDeposit(auctionHouseEntry, etime, item, finalCount);
    if (!_player->HasEnoughMoney(deposit))
    {
        SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_NOT_ENOUGHT_MONEY);
        return;
    }

    AuctionEntry* AH = new AuctionEntry();

    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
        AH->houseId = AUCTIONHOUSE_NEUTRAL;
    else
    {
        CreatureData const* auctioneerData = sObjectMgr->GetCreatureData(creature->GetSpawnId());
        if (!auctioneerData)
        {
            TC_LOG_ERROR("misc", "Data for auctioneer not found ({})", auctioneer.ToString());
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            delete AH;
            return;
        }

        CreatureTemplate const* auctioneerInfo = sObjectMgr->GetCreatureTemplate(auctioneerData->id);
        if (!auctioneerInfo)
        {
            TC_LOG_ERROR("misc", "Non existing auctioneer ({})", auctioneer.ToString());
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            delete AH;
            return;
        }

        AuctionHouseEntry const* AHEntry = sAuctionMgr->GetAuctionHouseEntry(auctioneerInfo->faction);
        AH->houseId = AHEntry->ID;
    }

    // 要求的堆叠数量与当前物品堆叠数量匹配,直接将物品转移到拍卖行
    if (itemsCount == 1 && item->GetCount() == count[0])
    {
        if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
        {
            sLog->OutCommand(GetAccountId(), "GM {} (Account: {}) create auction: {} (Entry: {} Count: {})",
                GetPlayerName(), GetAccountId(), item->GetTemplate()->Name1, item->GetEntry(), item->GetCount());
        }

        AH->Id = sObjectMgr->GenerateAuctionID();
        AH->itemGUIDLow = item->GetGUID().GetCounter();
        AH->itemEntry = item->GetEntry();
        AH->itemCount = item->GetCount();
        AH->owner = _player->GetGUID().GetCounter();
        AH->startbid = bid;
        AH->bidder = 0;
        AH->bid = 0;
        AH->buyout = buyout;
        AH->expire_time = GameTime::GetGameTime() + auctionTime;
        AH->deposit = deposit;
        AH->etime = etime;
        AH->auctionHouseEntry = auctionHouseEntry;
        AH->Flags = AUCTION_ENTRY_FLAG_NONE;

        TC_LOG_INFO("network", "CMSG_AUCTION_SELL_ITEM: Player {} {} is selling item {} entry {} {} with count {} with initial bid {} with buyout {} and with time {} (in sec) in auctionhouse {}",
            _player->GetName(), _player->GetGUID().ToString(), item->GetTemplate()->Name1, item->GetEntry(), item->GetGUID().ToString(), item->GetCount(), bid, buyout, auctionTime, AH->GetHouseId());

        // 添加到待处理拍卖列表,如果资金不足则失败
        if (!sAuctionMgr->PendingAuctionAdd(_player, AH))
        {
            SendAuctionCommandResult(AH->Id, AUCTION_SELL_ITEM, ERR_AUCTION_NOT_ENOUGHT_MONEY);
            return;
        }

        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(AH);
        _player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        item->DeleteFromInventoryDB(trans);
        item->SaveToDB(trans);

        AH->SaveToDB(trans);
        _player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        SendAuctionCommandResult(AH->Id, AUCTION_SELL_ITEM, ERR_AUCTION_OK);

        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION, 1);
    }
    else // 要求的堆叠数量与当前物品堆叠数量不匹配,克隆物品并设置正确的堆叠数量
    {
        Item* newItem = item->CloneItem(finalCount, _player);
        if (!newItem)
        {
            TC_LOG_ERROR("network", "CMSG_AUCTION_SELL_ITEM: Could not create clone of item {}", item->GetEntry());
            SendAuctionCommandResult(0, AUCTION_SELL_ITEM, ERR_AUCTION_DATABASE_ERROR);
            delete AH;
            return;
        }

        if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
        {
            sLog->OutCommand(GetAccountId(), "GM {} (Account: {}) create auction: {} (Entry: {} Count: {})",
                GetPlayerName(), GetAccountId(), newItem->GetTemplate()->Name1, newItem->GetEntry(), newItem->GetCount());
        }

        AH->Id = sObjectMgr->GenerateAuctionID();
        AH->itemGUIDLow = newItem->GetGUID().GetCounter();
        AH->itemEntry = newItem->GetEntry();
        AH->itemCount = newItem->GetCount();
        AH->owner = _player->GetGUID().GetCounter();
        AH->startbid = bid;
        AH->bidder = 0;
        AH->bid = 0;
        AH->buyout = buyout;
        AH->expire_time = GameTime::GetGameTime() + auctionTime;
        AH->deposit = deposit;
        AH->etime = etime;
        AH->auctionHouseEntry = auctionHouseEntry;
        AH->Flags = AUCTION_ENTRY_FLAG_NONE;

        TC_LOG_INFO("network", "CMSG_AUCTION_SELL_ITEM: Player {} {} is selling item {} entry {} {} with count {} with initial bid {} with buyout {} and with time {} (in sec) in auctionhouse {}",
            _player->GetName(), _player->GetGUID().ToString(), newItem->GetTemplate()->Name1, newItem->GetEntry(), newItem->GetGUID().ToString(), newItem->GetCount(), bid, buyout, auctionTime, AH->GetHouseId());

        // 添加到待处理拍卖列表,如果资金不足则失败
        if (!sAuctionMgr->PendingAuctionAdd(_player, AH))
        {
            SendAuctionCommandResult(AH->Id, AUCTION_SELL_ITEM, ERR_AUCTION_NOT_ENOUGHT_MONEY);
            return;
        }

        sAuctionMgr->AddAItem(newItem);
        auctionHouse->AddAuction(AH);
        for (uint32 j = 0; j < itemsCount; ++j)
        {
            Item* item2 = items[j];

            // 物品堆叠数量等于所需数量,准备删除物品 - 克隆的物品将用于拍卖
            if (item2->GetCount() == count[j])
            {
                _player->MoveItemFromInventory(item2->GetBagSlot(), item2->GetSlot(), true);

                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                item2->DeleteFromInventoryDB(trans);
                item2->DeleteFromDB(trans);
                CharacterDatabase.CommitTransaction(trans);
                delete item2;
            }
            else // 物品堆叠数量大于所需数量,更新物品堆叠数量并保存到数据库 - 克隆的物品将用于拍卖
            {
                item2->SetCount(item2->GetCount() - count[j]);
                item2->SetState(ITEM_CHANGED, _player);
                _player->ItemRemovedQuestCheck(item2->GetEntry(), count[j]);
                item2->SendUpdateToPlayer(_player);

                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                item2->SaveToDB(trans);
                CharacterDatabase.CommitTransaction(trans);
            }
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        newItem->SaveToDB(trans);
        AH->SaveToDB(trans);
        _player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        SendAuctionCommandResult(AH->Id, AUCTION_SELL_ITEM, ERR_AUCTION_OK);

        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION, 1);
    }
}

/**
 * @brief 处理玩家对拍卖出价或一口价购买的网络包
 *
 * 职责：
 *   处理玩家对拍卖物品的出价请求,包括普通出价和一口价购买。
 *   验证出价的有效性,处理资金转移,发送通知邮件,更新数据库。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - auctioneer: 拍卖师GUID
 *                       - auctionId: 拍卖ID
 *                       - price: 出价金额
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取并验证网络包数据
 *   2. 验证拍卖师NPC是否可交互
 *   3. 获取拍卖条目并验证:
 *      - 玩家不能竞拍自己的拍卖
 *      - 玩家不能竞拍同一账号下其他角色的拍卖
 *      - 出价必须高于当前出价和起拍价
 *      - 玩家必须有足够的金币
 *   4. 处理出价逻辑:
 *      a) 普通出价(price < buyout):
 *         - 退还之前竞价者的金币
 *         - 扣除玩家金币
 *         - 更新拍卖条目
 *         - 发送出价成功消息
 *      b) 一口价购买(price >= buyout):
 *         - 扣除玩家金币
 *         - 发送通知邮件给卖方和买方
 *         - 从拍卖行移除物品
 *         - 发送购买成功消息
 *   5. 保存数据到数据库
 */
void WorldSession::HandleAuctionPlaceBid(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_PLACE_BID");

    ObjectGuid auctioneer;
    uint32 auctionId;
    uint32 price;
    recvData >> auctioneer;
    recvData >> auctionId >> price;

    if (!auctionId || !price)
        return;                                             // 检查作弊者

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneer, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionPlaceBid - {} not found or you can't interact with him.", auctioneer.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    AuctionEntry* auction = auctionHouse->GetAuction(auctionId);
    Player* player = GetPlayer();

    if (!auction || auction->owner == player->GetGUID().GetCounter())
    {
        // 你不能竞拍自己的拍卖
        SendAuctionCommandResult(0, AUCTION_PLACE_BID, ERR_AUCTION_BID_OWN);
        return;
    }

    // 不可能同时在线拥有另一个角色(使用此方法加速检查,当所有者在线时)
    ObjectGuid ownerGuid(HighGuid::Player, auction->owner);
    Player* auction_owner = ObjectAccessor::FindPlayer(ownerGuid);
    if (!auction_owner && sCharacterCache->GetCharacterAccountIdByGuid(ownerGuid) == player->GetSession()->GetAccountId())
    {
        // 你不能竞拍自己另一个角色的拍卖
        SendAuctionCommandResult(0, AUCTION_PLACE_BID, ERR_AUCTION_BID_OWN);
        return;
    }

    // 作弊检查
    if (price <= auction->bid || price < auction->startbid)
        return;

    // 如果不是一口价,价格太低无法成为下一个出价
    if ((price < auction->buyout || auction->buyout == 0) &&
        price < auction->bid + auction->GetAuctionOutBid())
    {
        // 拍卖已经有更高的出价,客户端会测试此情况!
        return;
    }

    if (!player->HasEnoughMoney(price))
    {
        // 你没有足够的金币!,客户端会测试此情况!
        //SendAuctionCommandResult(auction->auctionId, AUCTION_PLACE_BID, ???);
        return;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    if (price < auction->buyout || auction->buyout == 0)
    {
        // 普通出价处理
        if (auction->bidder > 0)
        {
            if (auction->bidder == player->GetGUID().GetCounter())
                player->ModifyMoney(-int32(price - auction->bid));
            else
            {
                // 发送邮件给上一个竞价者并退还金币
                sAuctionMgr->SendAuctionOutbiddedMail(auction, price, GetPlayer(), trans);
                player->ModifyMoney(-int32(price));
            }
        }
        else
            player->ModifyMoney(-int32(price));

        auction->bidder = player->GetGUID().GetCounter();
        auction->bid = price;
        if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
            auction->Flags = AuctionEntryFlag(auction->Flags | AUCTION_ENTRY_FLAG_GM_LOG_BUYER);
        else
            auction->Flags = AuctionEntryFlag(auction->Flags & ~AUCTION_ENTRY_FLAG_GM_LOG_BUYER);

        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID, price);

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_AUCTION_BID);
        stmt->setUInt32(0, auction->bidder);
        stmt->setUInt32(1, auction->bid);
        stmt->setUInt8(2, auction->Flags);
        stmt->setUInt32(3, auction->Id);
        trans->Append(stmt);

        if (auction->bidders.find(player->GetGUID()) == auction->bidders.end())
        {
            // 保存新的竞价者到列表,并保存记录到数据库
            auction->bidders.insert(player->GetGUID());
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_AUCTION_BIDDERS);
            stmt->setUInt32(0, auction->Id);
            stmt->setUInt32(1, auction->bidder);
            trans->Append(stmt);
        }

        SendAuctionCommandResult(auction->Id, AUCTION_PLACE_BID, ERR_AUCTION_OK);
    }
    else
    {
        // 一口价购买处理
        if (player->GetGUID().GetCounter() == auction->bidder)
            player->ModifyMoney(-int32(auction->buyout - auction->bid));
        else
        {
            player->ModifyMoney(-int32(auction->buyout));
            if (auction->bidder)                          // 对已有出价的拍卖进行一口价购买
                sAuctionMgr->SendAuctionOutbiddedMail(auction, auction->buyout, GetPlayer(), trans);
        }
        auction->bidder = player->GetGUID().GetCounter();
        auction->bid = auction->buyout;
        if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
            auction->Flags = AuctionEntryFlag(auction->Flags | AUCTION_ENTRY_FLAG_GM_LOG_BUYER);
        else
            auction->Flags = AuctionEntryFlag(auction->Flags & ~AUCTION_ENTRY_FLAG_GM_LOG_BUYER);

        GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID, auction->buyout);

        // 邮件必须在事务控制下,以防止数据丢失
        sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
        sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
        sAuctionMgr->SendAuctionWonMail(auction, trans);

        SendAuctionCommandResult(auction->Id, AUCTION_PLACE_BID, ERR_AUCTION_OK);

        auction->DeleteFromDB(trans);

        sAuctionMgr->RemoveAItem(auction->itemGUIDLow);
        auctionHouse->RemoveAuction(auction);
    }
    player->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 处理拍卖所有者取消拍卖的网络包
 *
 * 职责：
 *   处理拍卖所有者取消拍卖的请求。如果已有竞价者,需要退还金币;
 *   物品将通过邮件退还给所有者。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - auctioneer: 拍卖师GUID
 *                       - auctionId: 要取消的拍卖ID
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取拍卖师GUID和拍卖ID
 *   2. 验证拍卖师NPC是否可交互
 *   3. 获取拍卖条目并验证所有权
 *   4. 如果有竞价者:
 *      - 计算并扣除拍卖行手续费
 *      - 发送邮件退还金币给竞价者
 *   5. 通过邮件将物品退还给拍卖所有者
 *   6. 从拍卖行移除拍卖条目
 *   7. 更新数据库并发送取消成功消息
 */
void WorldSession::HandleAuctionRemoveItem(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_REMOVE_ITEM");

    ObjectGuid auctioneer;
    uint32 auctionId;
    recvData >> auctioneer;
    recvData >> auctionId;
    //TC_LOG_DEBUG("Cancel AUCTION AuctionID: {}", auctionId);

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(auctioneer, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionRemoveItem - {} not found or you can't interact with him.", auctioneer.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    AuctionEntry* auction = auctionHouse->GetAuction(auctionId);
    Player* player = GetPlayer();

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    if (auction && auction->owner == player->GetGUID().GetCounter())
    {
        Item* pItem = sAuctionMgr->GetAItem(auction->itemGUIDLow);
        if (pItem)
        {
            if (auction->bidder > 0)                        // 如果有竞价者,必须退还他支付的金币
            {
                uint32 auctionCut = auction->GetAuctionCut();
                if (!player->HasEnoughMoney(auctionCut))          // 玩家没有足够的金币,可能需要消息提示
                    return;
                // 需要发送竞价者取消通知,但不清楚具体部分..
                sAuctionMgr->SendAuctionCancelledToBidderMail(auction, trans);
                player->ModifyMoney(-int32(auctionCut));
            }

            // 物品将被删除或添加到接收邮件列表
            MailDraft(auction->BuildAuctionMailSubject(AUCTION_CANCELED), "")
                .AddItem(pItem)
                .SendMailTo(trans, player, auction, MAIL_CHECK_MASK_COPIED);
        }
        else
        {
            TC_LOG_ERROR("network", "Auction id: {} has non-existed item (item guid : {})!!!", auction->Id, auction->itemGUIDLow);
            SendAuctionCommandResult(0, AUCTION_CANCEL, ERR_AUCTION_DATABASE_ERROR);
            return;
        }
    }
    else
    {
        SendAuctionCommandResult(0, AUCTION_CANCEL, ERR_AUCTION_DATABASE_ERROR);
        // 此代码不应该发生...可能应该添加断言
        TC_LOG_ERROR("entities.player.cheat", "CHEATER : {} tried to cancel auction (id: {}) of another player, or auction is NULL", player->GetGUID().ToString(), auctionId);
        return;
    }

    // 通知玩家拍卖已移除
    SendAuctionCommandResult(auction->Id, AUCTION_CANCEL, ERR_AUCTION_OK);

    // 现在移除拍卖

    player->SaveInventoryAndGoldToDB(trans);
    auction->DeleteFromDB(trans);
    CharacterDatabase.CommitTransaction(trans);

    sAuctionMgr->RemoveAItem(auction->itemGUIDLow);
    auctionHouse->RemoveAuction(auction);
}

/**
 * @brief 处理列出玩家竞价物品的网络包
 *
 * 职责：
 *   处理玩家查看自己参与竞价的拍卖列表请求。
 *   返回玩家当前竞价的所有拍卖信息。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - guid: 拍卖师GUID
 *                       - listfrom: 列表起始位置(未实际使用)
 *                       - outbiddedCount: 被超价的拍卖数量
 *                       - outbiddedAuctionId[]: 被超价的拍卖ID数组
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取拍卖师GUID和列表参数
 *   2. 验证拍卖师NPC是否可交互
 *   3. 构建竞价列表结果消息包:
 *      - 首先添加所有被超价的拍卖信息
 *      - 然后添加玩家当前竞价的拍卖信息
 *   4. 发送拍卖列表给客户端
 */
void WorldSession::HandleAuctionListBidderItems(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_LIST_BIDDER_ITEMS");

    ObjectGuid guid;                                        //NPC guid
    uint32 listfrom;                                        //page of auctions
    uint32 outbiddedCount;                                  //count of outbidded auctions

    recvData >> guid;
    recvData >> listfrom;                                  // not used in fact (this list not have page control in client)
    recvData >> outbiddedCount;
    if (recvData.size() != (16 + outbiddedCount * 4))
    {
        TC_LOG_ERROR("network", "Client sent bad opcode!!! with count: {} and size : {} (must be: {})", outbiddedCount, (unsigned long)recvData.size(), (16 + outbiddedCount * 4));
        outbiddedCount = 0;
    }

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionListBidderItems - {} not found or you can't interact with him.", guid.ToString());
        recvData.rfinish();
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    WorldPacket data(SMSG_AUCTION_BIDDER_LIST_RESULT, (4+4+4));
    Player* player = GetPlayer();
    data << (uint32) 0;                                     //add 0 as count
    uint32 count = 0;
    uint32 totalcount = 0;
    while (outbiddedCount > 0)                             //add all data, which client requires
    {
        --outbiddedCount;
        uint32 outbiddedAuctionId;
        recvData >> outbiddedAuctionId;
        AuctionEntry* auction = auctionHouse->GetAuction(outbiddedAuctionId);
        if (auction && auction->BuildAuctionInfo(data))
        {
            ++totalcount;
            ++count;
        }
    }

    auctionHouse->BuildListBidderItems(data, player, count, totalcount);
    data.put<uint32>(0, count);                           // add count to placeholder
    data << totalcount;
    data << (uint32)sWorld->getIntConfig(CONFIG_AUCTION_SEARCH_DELAY);
    SendPacket(&data);
}

/**
 * @brief 处理列出玩家拥有的拍卖的网络包
 *
 * 职责：
 *   处理玩家查看自己创建的拍卖列表请求。
 *   返回玩家当前拥有的所有拍卖信息。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - guid: 拍卖师GUID
 *                       - listfrom: 列表起始位置(未实际使用)
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取拍卖师GUID和列表参数
 *   2. 验证拍卖师NPC是否可交互
 *   3. 构建所有者拍卖列表结果消息包
 *   4. 发送拍卖列表给客户端
 */
void WorldSession::HandleAuctionListOwnerItems(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_LIST_OWNER_ITEMS");

    uint32 listfrom;
    ObjectGuid guid;

    recvData >> guid;
    recvData >> listfrom;                                  // not used in fact (this list not have page control in client)

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionListOwnerItems - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    WorldPacket data(SMSG_AUCTION_OWNER_LIST_RESULT, (4+4+4));
    data << (uint32) 0;                                     // amount place holder

    uint32 count = 0;
    uint32 totalcount = 0;

    auctionHouse->BuildListOwnerItems(data, _player, count, totalcount);
    data.put<uint32>(0, count);
    data << (uint32) totalcount;
    data << (uint32) sWorld->getIntConfig(CONFIG_AUCTION_SEARCH_DELAY);
    SendPacket(&data);
}

/**
 * @brief 处理拍卖行搜索的网络包
 *
 * 职责：
 *   处理玩家在拍卖行搜索物品的请求。支持多种筛选条件,
 *   包括物品名称、等级范围、物品类型、品质等。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据,包含:
 *                       - guid: 拍卖师GUID
 *                       - listfrom: 列表起始位置,用于分页(每页50个元素)
 *                       - searchedname: 搜索的物品名称
 *                       - levelmin/levelmax: 物品等级范围
 *                       - auctionSlotID: 物品槽位类型
 *                       - auctionMainCategory: 物品主类别
 *                       - auctionSubCategory: 物品子类别
 *                       - quality: 物品品质
 *                       - usable: 是否只显示可用物品
 *                       - getAll: 是否获取所有物品
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 读取拍卖师GUID和各种搜索条件
 *   2. 验证拍卖师NPC是否可交互
 *   3. 将搜索名称转换为小写宽字符串
 *   4. 根据搜索条件构建拍卖物品列表
 *   5. 发送搜索结果给客户端
 */
void WorldSession::HandleAuctionListItems(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_LIST_ITEMS");

    std::string searchedname;
    uint8 levelmin, levelmax, usable, getAll;
    uint32 listfrom, auctionSlotID, auctionMainCategory, auctionSubCategory, quality;
    ObjectGuid guid;

    recvData >> guid;
    recvData >> listfrom;                                  // start, used for page control listing by 50 elements
    recvData >> searchedname;

    recvData >> levelmin >> levelmax;
    recvData >> auctionSlotID >> auctionMainCategory >> auctionSubCategory;
    recvData >> quality >> usable;

    recvData >> getAll;

    // 此数据块看起来使用了某种字节打包或类似方式...
    uint8 unkCnt;
    recvData >> unkCnt;
    for (uint8 i = 0; i < unkCnt; i++)
    {
        recvData.read_skip<uint8>();
        recvData.read_skip<uint8>();
    }

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAuctionListItems - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(creature->GetFaction());

    TC_LOG_DEBUG("auctionHouse", "Auctionhouse search ({}) list from: {}, searchedname: {}, levelmin: {}, levelmax: {}, auctionSlotID: {}, auctionMainCategory: {}, auctionSubCategory: {}, quality: {}, usable: {}",
        guid.ToString(), listfrom, searchedname, levelmin, levelmax, auctionSlotID, auctionMainCategory, auctionSubCategory, quality, usable);

    WorldPacket data(SMSG_AUCTION_LIST_RESULT, (4+4+4));
    uint32 count = 0;
    uint32 totalcount = 0;
    data << (uint32) 0;

    // 将搜索字符串转换为小写
    std::wstring wsearchedname;
    if (!Utf8toWStr(searchedname, wsearchedname))
        return;

    wstrToLower(wsearchedname);

    auctionHouse->BuildListAuctionItems(data, _player,
        wsearchedname, listfrom, levelmin, levelmax, usable,
        auctionSlotID, auctionMainCategory, auctionSubCategory, quality,
        count, totalcount, (getAll != 0 && sWorld->getIntConfig(CONFIG_AUCTION_GETALL_DELAY) != 0));

    data.put<uint32>(0, count);
    data << (uint32) totalcount;
    data << (uint32) sWorld->getIntConfig(CONFIG_AUCTION_SEARCH_DELAY);
    SendPacket(&data);
}

/**
 * @brief 处理列出待处理拍卖销售的网络包
 *
 * 职责：
 *   处理玩家查看待处理拍卖销售列表的请求。
 *   目前此功能似乎未完全实现,仅返回空列表。
 *
 * 参数：
 *   @param recvData [in] 接收到的网络包数据
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 跳过接收数据中的GUID
 *   2. 构建并发送待处理销售列表消息包
 *   3. 当前实现返回计数为0的空列表
 */
void WorldSession::HandleAuctionListPendingSales(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_AUCTION_LIST_PENDING_SALES");

    recvData.read_skip<uint64>();

    uint32 count = 0;

    WorldPacket data(SMSG_AUCTION_LIST_PENDING_SALES, 4);
    data << uint32(count);                                  // count
    /*for (uint32 i = 0; i < count; ++i)
    {
        data << "";                                         // string
        data << "";                                         // string
        data << uint32(0);
        data << uint32(0);
        data << float(0);
    }*/
    SendPacket(&data);
}
