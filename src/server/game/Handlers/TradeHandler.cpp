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
 * @file TradeHandler.cpp
 * @brief 玩家交易系统网络消息处理模块
 *
 * 本模块实现了玩家之间交易功能的所有网络消息处理器，包括：
 * - 发起交易请求
 * - 接受/拒绝交易
 * - 设置交易物品和金币
 * - 交易完成确认
 * - 交易取消处理
 *
 * 交易流程：
 * 1. 玩家A向玩家B发起交易请求（CMSG_INITIATE_TRADE）
 * 2. 玩家B收到交易窗口打开通知
 * 3. 双方放置物品和金币
 * 4. 双方确认交易
 * 5. 服务器验证并完成交易
 *
 * 安全考虑：
 * - 防止物品复制（严格验证物品唯一性）
 * - 防止金币溢出（检查金币上限）
 * - 防止跨阵营交易（除非有权限）
 * - GM交易日志记录
 */

#include "WorldSession.h"
#include "AccountMgr.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "SocialMgr.h"
#include "TradeData.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 发送交易状态消息给客户端
 *
 * 向玩家发送交易状态更新消息，包含交易的各种状态信息
 * 如交易开始、交易窗口打开、交易关闭、错误信息等
 *
 * @param info 交易状态信息结构体，包含状态码和相关参数
 *
 * 调用时机：
 * - 交易请求发起时（TRADE_STATUS_BEGIN_TRADE）
 * - 交易窗口打开时（TRADE_STATUS_OPEN_WINDOW）
 * - 交易关闭时（TRADE_STATUS_CLOSE_WINDOW）
 * - 交易错误时（如金币不足、背包满等）
 * - 交易对方不在有效范围内（TRADE_STATUS_WRONG_REALM）
 * - 物品不在拾取列表中（TRADE_STATUS_NOT_ON_TAPLIST）
 *
 * 性能注意事项：
 * - 频繁调用时需注意网络带宽消耗
 * - 消息大小根据状态类型动态变化
 */
void WorldSession::SendTradeStatus(TradeStatusInfo const& info)
{
    // 创建交易状态数据包，预留13字节空间
    WorldPacket data(SMSG_TRADE_STATUS, 13);
    // 写入交易状态码
    data << uint32(info.Status);

    // 根据不同的交易状态填充不同的附加数据
    switch (info.Status)
    {
        case TRADE_STATUS_BEGIN_TRADE:
            // 交易开始，发送交易对方的GUID
            data << uint64(info.TraderGuid);                // CGTradeInfo::m_tradingPlayer
            break;
        case TRADE_STATUS_OPEN_WINDOW:
            // 打开交易窗口，发送交易ID（客户端未使用，默认为0）
            data << uint32(0);                              // CGTradeInfo::m_tradeID
            break;
        case TRADE_STATUS_CLOSE_WINDOW:
            // 交易窗口关闭，发送错误原因和相关信息
            data << uint32(info.Result);                    // InventoryResult - 库存错误码
            data << uint8(info.IsTargetResult);             // bool isTargetError - 是否是对方的错误
                                                            // 用于: EQUIP_ERR_BAG_FULL, EQUIP_ERR_CANT_CARRY_MORE_OF_THIS,
                                                            // EQUIP_ERR_MISSING_REAGENT, EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_COUNT_EXCEEDED
            data << uint32(info.ItemLimitCategoryId);       // ItemLimitCategory.dbc entry - 物品限制类别ID
            break;
        case TRADE_STATUS_WRONG_REALM:
        case TRADE_STATUS_NOT_ON_TAPLIST:
            // 错误的领域或不在拾取列表中，发送交易槽位
            // -1 会清除客户端的 m_tradeMoney
            data << uint8(info.Slot);                       // Trade slot - 交易槽位
            break;
        default:
            // 其他状态不需要附加数据
            break;
    }

    // 发送数据包给客户端
    SendPacket(&data);
}

/**
 * @brief 处理忽略交易请求的消息
 *
 * 当玩家选择忽略某个交易请求时调用此函数
 * 通知交易对方该玩家忽略了交易请求
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 调用时机：
 * - 玩家点击"忽略"按钮拒绝交易请求时
 * - 客户端发送 CMSG_IGNORE_TRADE 消息时
 */
void WorldSession::HandleIgnoreTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 取消交易并通知对方"你忽略了交易"
    _player->TradeCancel(true, TRADE_STATUS_IGNORE_YOU);
}

/**
 * @brief 处理忙碌状态下的交易请求
 *
 * 当玩家处于忙碌状态（如正在战斗）收到交易请求时调用此函数
 * 自动拒绝交易请求
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 调用时机：
 * - 玩家处于忙碌状态收到交易请求时
 * - 客户端发送 CMSG_BUSY_TRADE 消息时
 */
void WorldSession::HandleBusyTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 取消交易并通知对方"目标忙碌"
    _player->TradeCancel(true, TRADE_STATUS_BUSY);
}

/**
 * @brief 发送交易更新消息给客户端
 *
 * 向客户端发送交易窗口中物品和金币的详细信息
 * 可以发送自己的交易信息或对方的交易信息
 *
 * @param trader_data true表示发送对方的交易数据，false表示发送自己的交易数据
 *
 * 调用时机：
 * - 玩家在交易窗口中放置或移除物品时
 * - 玩家修改交易金币数量时
 * - 交易开始时发送初始交易窗口信息
 * - 对方修改交易内容时更新显示
 *
 * 性能注意事项：
 * - 数据包较大，包含所有交易槽位的详细信息
 * - 每次物品变化都会触发，避免频繁调用
 * - 预先计算数据包大小以减少内存分配
 */
void WorldSession::SendUpdateTrade(bool trader_data /*= true*/)
{
    // 获取要查看的交易数据
    // trader_data为true时获取对方的数据，false时获取自己的数据
    TradeData* view_trade = trader_data ? _player->GetTradeData()->GetTraderData() : _player->GetTradeData();

    // 创建扩展交易状态数据包，预先计算大小避免多次内存分配
    // 格式: 1字节标识 + 4字节交易ID + 4字节槽位数量 + 4字节槽位数量 + 4字节金币 + 4字节法术
    // + 7个槽位 * (1字节槽位号 + 18个4字节属性)
    WorldPacket data(SMSG_TRADE_STATUS_EXTENDED, 1+4+4+4+4+4+7*(1+4+4+4+4+8+4+4+4+4+8+4+4+4+4+4+4));

    // 写入数据包头部信息
    data << uint8(trader_data);                             // 1表示对方的数据，0表示自己的数据
    data << uint32(0);                                      // CGTradeInfo::m_tradeID - 交易ID（客户端未使用）
    data << uint32(TRADE_SLOT_COUNT);                       // 交易槽位数量
    data << uint32(TRADE_SLOT_COUNT);                       // 交易槽位数量（重复字段）
    data << uint32(view_trade->GetMoney());                 // 交易金币数量
    data << uint32(view_trade->GetSpell());                 // 施加在最低槽位物品上的法术ID

    // 遍历所有交易槽位，发送每个槽位的物品详细信息
    for (uint8 i = 0; i < TRADE_SLOT_COUNT; ++i)
    {
        // 写入槽位编号，如果未指定则表示数据包结束
        data << uint8(i);

        // 如果槽位中有物品，发送物品详细信息
        if (Item* item = view_trade->GetItem(TradeSlots(i)))
        {
            // 发送物品基本属性
            data << uint32(item->GetTemplate()->ItemId);       // 物品模板ID
            data << uint32(item->GetTemplate()->DisplayInfoID);// 物品显示ID
            data << uint32(item->GetCount());               // 堆叠数量

            // 包装物品处理：隐藏属性但显示赠送者名字
            data << uint32(item->IsWrapped() ? 1 : 0);
            data << uint64(item->GetGuidValue(ITEM_FIELD_GIFTCREATOR));

            // 发送永久附魔和宝石信息
            data << uint32(item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT));
            for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT+MAX_GEM_SOCKETS; ++enchant_slot)
                data << uint32(item->GetEnchantmentId(EnchantmentSlot(enchant_slot)));

            // 发送物品创建者信息
            data << uint64(item->GetGuidValue(ITEM_FIELD_CREATOR));
            data << uint32(item->GetSpellCharges());        // 法术充能次数
            data << uint32(item->GetItemSuffixFactor());    // 后缀因子
            data << uint32(item->GetItemRandomPropertyId());// 随机属性ID
            data << uint32(item->GetTemplate()->LockID);       // 锁ID

            // 发送耐久度信息
            data << uint32(item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY)); // 最大耐久度
            data << uint32(item->GetUInt32Value(ITEM_FIELD_DURABILITY));    // 当前耐久度
        }
        else
        {
            // 空槽位，填充18个0（与物品数据大小一致）
            for (uint8 j = 0; j < 18; ++j)
                data << uint32(0);
        }
    }
    // 发送数据包给客户端
    SendPacket(&data);
}

//==============================================================
// transfer the items to the players
/**
 * @brief 执行物品转移操作
 *
 * 在交易确认后，将双方的交易物品转移到对方的背包中
 * 包含完整的错误处理和回滚机制
 *
 * @param myItems[] 我方要交易的物品数组
 * @param hisItems[] 对方要交易的物品数组
 *
 * 处理流程：
 * 1. 检查双方背包是否有空间存放对方物品
 * 2. 如果双方都能存放，执行物品转移
 * 3. 如果任一方无法存放，将物品返还给原主人
 * 4. 记录GM交易日志（如果有权限）
 * 5. 调整绑定物品的创建时间
 *
 * 安全措施：
 * - 转移前验证背包空间
 * - 失败时回滚物品到原主人
 * - GM交易需要记录日志
 * - BOP物品调整创建时间防止滥用
 *
 * 性能注意事项：
 * - 遍历所有交易槽位，避免在循环中分配内存
 * - 优先检查能否存放，减少不必要的操作
 */
void WorldSession::moveItems(Item* myItems[], Item* hisItems[])
{
    // 获取交易对象
    Player* trader = _player->GetTrader();
    if (!trader)
        return;

    // 遍历所有交易槽位
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
    {
        ItemPosCountVec traderDst;  // 交易对方的目标位置
        ItemPosCountVec playerDst;  // 我方的目标位置

        // 检查双方是否能够存放对方的物品
        // 只有在物品存在且能成功存放时才认为可以交易
        bool traderCanTrade = (myItems[i] == nullptr || trader->CanStoreItem(NULL_BAG, NULL_SLOT, traderDst, myItems[i], false) == EQUIP_ERR_OK);
        bool playerCanTrade = (hisItems[i] == nullptr || _player->CanStoreItem(NULL_BAG, NULL_SLOT, playerDst, hisItems[i], false) == EQUIP_ERR_OK);

        // 如果双方都能存放物品，执行交易
        if (traderCanTrade && playerCanTrade)
        {
            // 交易前已经验证过，现在可以安全地执行转移
            // 双向交易需要先验证再执行，因为转移后无法回滚

            // 处理我方的物品转移给对方
            if (myItems[i])
            {
                // 记录调试日志
                TC_LOG_DEBUG("network", "partner storing: {}", myItems[i]->GetGUID().ToString());

                // 如果是GM交易，记录命令日志
                if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
                {
                    sLog->OutCommand(_player->GetSession()->GetAccountId(), "GM {} (Account: {}) trade: {} (Entry: {} Count: {}) to player: {} (Account: {})",
                        _player->GetName(), _player->GetSession()->GetAccountId(),
                        myItems[i]->GetTemplate()->Name1, myItems[i]->GetEntry(), myItems[i]->GetCount(),
                        trader->GetName(), trader->GetSession()->GetAccountId());
                }

                // 调整BOP（绑定拾取）物品的创建时间
                // 创建时间基于游戏时间，用于防止BOP物品被滥用交易
                if (myItems[i]->IsBOPTradeable())
                    myItems[i]->SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, trader->GetTotalPlayedTime()-(_player->GetTotalPlayedTime()-myItems[i]->GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME)));

                // 将物品移动到对方的背包
                trader->MoveItemToInventory(traderDst, myItems[i], true, true);
            }

            // 处理对方的物品转移给我方
            if (hisItems[i])
            {
                // 记录调试日志
                TC_LOG_DEBUG("network", "player storing: {}", hisItems[i]->GetGUID().ToString());

                // 如果是GM交易，记录命令日志
                if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
                {
                    sLog->OutCommand(trader->GetSession()->GetAccountId(), "GM {} (Account: {}) trade: {} (Entry: {} Count: {}) to player: {} (Account: {})",
                        trader->GetName(), trader->GetSession()->GetAccountId(),
                        hisItems[i]->GetTemplate()->Name1, hisItems[i]->GetEntry(), hisItems[i]->GetCount(),
                        _player->GetName(), _player->GetSession()->GetAccountId());
                }

                // 调整BOP物品的创建时间
                if (hisItems[i]->IsBOPTradeable())
                    hisItems[i]->SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, _player->GetTotalPlayedTime()-(trader->GetTotalPlayedTime()-hisItems[i]->GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME)));

                // 将物品移动到我方的背包
                _player->MoveItemToInventory(playerDst, hisItems[i], true, true);
            }
        }
        else
        {
            // 发生致命错误，无法完成交易
            // 需要将已经从背包移除的物品返还给原主人

            // 返还我方的物品
            if (myItems[i])
            {
                if (!traderCanTrade)
                    TC_LOG_ERROR("network", "trader can't store item: {}", myItems[i]->GetGUID().ToString());

                // 尝试将物品返还到我方背包
                if (_player->CanStoreItem(NULL_BAG, NULL_SLOT, playerDst, myItems[i], false) == EQUIP_ERR_OK)
                    _player->MoveItemToInventory(playerDst, myItems[i], true, true);
                else
                    TC_LOG_ERROR("network", "player can't take item back: {}", myItems[i]->GetGUID().ToString());
            }

            // 返还对方的物品
            if (hisItems[i])
            {
                if (!playerCanTrade)
                    TC_LOG_ERROR("network", "player can't store item: {}", hisItems[i]->GetGUID().ToString());

                // 尝试将物品返还到对方背包
                if (trader->CanStoreItem(NULL_BAG, NULL_SLOT, traderDst, hisItems[i], false) == EQUIP_ERR_OK)
                    trader->MoveItemToInventory(traderDst, hisItems[i], true, true);
                else
                    TC_LOG_ERROR("network", "trader can't take item back: {}", hisItems[i]->GetGUID().ToString());
            }
        }
    }
}

//==============================================================

/**
 * @brief 设置交易为接受模式
 *
 * 在交易确认过程中，设置交易状态并标记所有交易物品
 * 防止物品在交易过程中被其他操作修改
 *
 * @param myTrade 我方的交易数据
 * @param hisTrade 对方的交易数据
 * @param myItems 我方交易物品数组（输出参数）
 * @param hisItems 对方交易物品数组（输出参数）
 *
 * 处理内容：
 * - 设置双方交易数据的接受处理标志
 * - 遍历所有交易槽位，缓存物品指针
 * - 为所有交易物品设置"交易中"标志
 */
static void setAcceptTradeMode(TradeData* myTrade, TradeData* hisTrade, Item* *myItems, Item* *hisItems)
{
    // 设置交易处理标志，防止重复处理
    myTrade->SetInAcceptProcess(true);
    hisTrade->SetInAcceptProcess(true);

    // 遍历所有交易槽位，缓存物品并设置交易标志
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
    {
        // 处理我方物品
        if (Item* item = myTrade->GetItem(TradeSlots(i)))
        {
            // 记录调试日志，包含物品位置信息
            TC_LOG_DEBUG("network", "player trade item {} bag: {} slot: {}", item->GetGUID().ToString(), item->GetBagSlot(), item->GetSlot());
            // 缓存物品指针（可能为nullptr）
            myItems[i] = item;
            // 设置物品的"交易中"标志
            myItems[i]->SetInTrade();
        }

        // 处理对方物品
        if (Item* item = hisTrade->GetItem(TradeSlots(i)))
        {
            // 记录调试日志
            TC_LOG_DEBUG("network", "partner trade item {} bag: {} slot: {}", item->GetGUID().ToString(), item->GetBagSlot(), item->GetSlot());
            // 缓存物品指针
            hisItems[i] = item;
            // 设置物品的"交易中"标志
            hisItems[i]->SetInTrade();
        }
    }
}

/**
 * @brief 清除交易的接受处理标志
 *
 * 交易完成或失败后，清除交易数据的接受处理标志
 * 允许后续交易操作正常进行
 *
 * @param myTrade 我方的交易数据
 * @param hisTrade 对方的交易数据
 */
static void clearAcceptTradeMode(TradeData* myTrade, TradeData* hisTrade)
{
    // 清除接受处理标志
    myTrade->SetInAcceptProcess(false);
    hisTrade->SetInAcceptProcess(false);
}

/**
 * @brief 清除物品的交易标志
 *
 * 交易完成或失败后，清除所有交易物品的"交易中"标志
 * 允许物品被其他操作使用
 *
 * @param myItems 我方交易物品数组
 * @param hisItems 对方交易物品数组
 */
static void clearAcceptTradeMode(Item* *myItems, Item* *hisItems)
{
    // 遍历所有交易槽位，清除物品的"交易中"标志
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
    {
        // 清除我方物品标志
        if (myItems[i])
            myItems[i]->SetInTrade(false);
        // 清除对方物品标志
        if (hisItems[i])
            hisItems[i]->SetInTrade(false);
    }
}

/**
 * @brief 处理玩家确认交易的消息
 *
 * 当玩家点击"接受交易"按钮时调用此函数
 * 执行完整的交易验证和执行流程
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 交易验证流程：
 * 1. 检查交易双方距离是否过远
 * 2. 验证双方金币是否足够
 * 3. 验证双方金币是否会溢出
 * 4. 验证所有交易物品是否可交易
 * 5. 检查物品绑定状态
 * 6. 验证交易法术是否可以施放
 * 7. 验证双方背包是否有空间
 * 8. 执行物品和金币转移
 * 9. 保存交易记录到数据库
 *
 * 安全措施：
 * - 严格的距离检查防止远程交易作弊
 * - 金币溢出检查防止金币上限bug
 * - 物品可交易性验证防止非法交易
 * - 绑定检查防止违反绑定规则
 * - 背包空间验证防止物品丢失
 * - 完整的错误处理和回滚机制
 *
 * 调用时机：
 * - 玩家点击交易窗口的"接受"按钮
 * - 客户端发送 CMSG_ACCEPT_TRADE 消息
 *
 * 性能注意事项：
 * - 复杂的验证流程，避免在频繁交易时阻塞
 * - 数据库操作使用事务保证一致性
 */
void WorldSession::HandleAcceptTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 获取我方的交易数据
    TradeData* my_trade = _player->m_trade;
    if (!my_trade)
        return;

    // 获取交易对方
    Player* trader = my_trade->GetTrader();

    // 获取对方的交易数据
    TradeData* his_trade = trader->m_trade;
    if (!his_trade)
        return;

    // 初始化物品数组
    Item* myItems[TRADE_SLOT_TRADED_COUNT]  = { };
    Item* hisItems[TRADE_SLOT_TRADED_COUNT] = { };

    // 设置接受标志（在检查前设置，便于错误处理时正确回滚）
    // 客户端已经设置了接受状态，服务器需要同步
    my_trade->SetAccepted(true);

    TradeStatusInfo info;

    // === 第一阶段：距离验证 ===
    // 检查交易双方是否在有效距离内
    if (!_player->IsWithinDistInMap(trader, TRADE_DISTANCE, false))
    {
        info.Status = TRADE_STATUS_TARGET_TO_FAR;
        SendTradeStatus(info);
        my_trade->SetAccepted(false);
        return;
    }

    // === 第二阶段：金币数量验证 ===
    // 验证我方金币是否足够
    if (!_player->HasEnoughMoney(my_trade->GetMoney()))
    {
        info.Status = TRADE_STATUS_CLOSE_WINDOW;
        info.Result = EQUIP_ERR_NOT_ENOUGH_MONEY;
        SendTradeStatus(info);
        my_trade->SetAccepted(false, true);
        return;
    }

    // 验证对方金币是否足够
    if (!trader->HasEnoughMoney(his_trade->GetMoney()))
    {
        info.Status = TRADE_STATUS_CLOSE_WINDOW;
        info.Result = EQUIP_ERR_NOT_ENOUGH_MONEY;
        trader->GetSession()->SendTradeStatus(info);
        his_trade->SetAccepted(false, true);
        return;
    }

    // === 第三阶段：金币溢出验证 ===
    // 检查我方接收金币后是否会超过上限
    if (_player->GetMoney() >= MAX_MONEY_AMOUNT - his_trade->GetMoney())
    {
        info.Status = TRADE_STATUS_CLOSE_WINDOW;
        info.Result = EQUIP_ERR_TOO_MUCH_GOLD;
        SendTradeStatus(info);
        my_trade->SetAccepted(false, true);
        return;
    }

    // 检查对方接收金币后是否会超过上限
    if (trader->GetMoney() >= MAX_MONEY_AMOUNT - my_trade->GetMoney())
    {
        info.Status = TRADE_STATUS_CLOSE_WINDOW;
        info.Result = EQUIP_ERR_TOO_MUCH_GOLD;
        trader->GetSession()->SendTradeStatus(info);
        his_trade->SetAccepted(false, true);
        return;
    }

    // === 第四阶段：物品可交易性验证 ===
    // 检查所有交易物品是否仍然可交易（防止作弊）
    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
    {
        // 检查我方物品
        if (Item* item = my_trade->GetItem(TradeSlots(i)))
        {
            // 验证物品是否可交易
            if (!item->CanBeTraded(false, true))
            {
                info.Status = TRADE_STATUS_TRADE_CANCELED;
                SendTradeStatus(info);
                return;
            }

            // 验证物品绑定状态（某些物品只能绑定给特定玩家）
            if (item->IsBindedNotWith(trader))
            {
                info.Status = TRADE_STATUS_CLOSE_WINDOW;
                info.Result = EQUIP_ERR_CANNOT_TRADE_THAT;
                SendTradeStatus(info);
                return;
            }
        }

        // 检查对方物品
        if (Item* item = his_trade->GetItem(TradeSlots(i)))
        {
            // 验证物品是否可交易
            if (!item->CanBeTraded(false, true))
            {
                info.Status = TRADE_STATUS_TRADE_CANCELED;
                SendTradeStatus(info);
                return;
            }
            // 对方物品的绑定检查已注释，因为如果物品无效，交易会在后续检查中失败
        }
    }

    // === 第五阶段：双方确认后执行交易 ===
    if (his_trade->IsAccepted())
    {
        // 设置交易接受模式，标记所有交易物品
        setAcceptTradeMode(my_trade, his_trade, myItems, hisItems);

        // 初始化法术相关变量（交易时可对物品施放法术）
        Spell* my_spell = nullptr;
        SpellCastTargets my_targets;

        Spell* his_spell = nullptr;
        SpellCastTargets his_targets;

        // === 第六阶段：法术验证 ===
        // 检查我方的交易法术是否可以施放
        if (uint32 my_spell_id = my_trade->GetSpell())
        {
            SpellInfo const* spellEntry = sSpellMgr->GetSpellInfo(my_spell_id);
            Item* castItem = my_trade->GetSpellCastItem();

            // 验证法术和施法物品的有效性
            if (!spellEntry || !his_trade->GetItem(TRADE_SLOT_NONTRADED) ||
                (my_trade->HasSpellCastItem() && !castItem))
            {
                // 清除交易模式并重置法术
                clearAcceptTradeMode(my_trade, his_trade);
                clearAcceptTradeMode(myItems, hisItems);
                my_trade->SetSpell(0);
                return;
            }

            // 创建法术对象
            my_spell = new Spell(_player, spellEntry, TRIGGERED_FULL_MASK);
            my_spell->m_CastItem = castItem;
            my_targets.SetTradeItemTarget(_player);
            my_spell->m_targets = my_targets;

            // 验证法术是否可以施放
            SpellCastResult res = my_spell->CheckCast(true);
            if (res != SPELL_CAST_OK)
            {
                my_spell->SendCastResult(res);
                clearAcceptTradeMode(my_trade, his_trade);
                clearAcceptTradeMode(myItems, hisItems);
                delete my_spell;
                my_trade->SetSpell(0);
                return;
            }
        }

        // 检查对方的交易法术是否可以施放
        if (uint32 his_spell_id = his_trade->GetSpell())
        {
            SpellInfo const* spellEntry = sSpellMgr->GetSpellInfo(his_spell_id);
            Item* castItem = his_trade->GetSpellCastItem();

            // 验证法术和施法物品的有效性
            if (!spellEntry || !my_trade->GetItem(TRADE_SLOT_NONTRADED) || (his_trade->HasSpellCastItem() && !castItem))
            {
                delete my_spell;
                his_trade->SetSpell(0);
                clearAcceptTradeMode(my_trade, his_trade);
                clearAcceptTradeMode(myItems, hisItems);
                return;
            }

            // 创建法术对象
            his_spell = new Spell(trader, spellEntry, TRIGGERED_FULL_MASK);
            his_spell->m_CastItem = castItem;
            his_targets.SetTradeItemTarget(trader);
            his_spell->m_targets = his_targets;

            // 验证法术是否可以施放
            SpellCastResult res = his_spell->CheckCast(true);
            if (res != SPELL_CAST_OK)
            {
                his_spell->SendCastResult(res);
                clearAcceptTradeMode(my_trade, his_trade);
                clearAcceptTradeMode(myItems, hisItems);
                delete my_spell;
                delete his_spell;
                his_trade->SetSpell(0);
                return;
            }
        }

        // === 第七阶段：通知交易对方已接受 ===
        info.Status = TRADE_STATUS_TRADE_ACCEPT;
        trader->GetSession()->SendTradeStatus(info);

        // === 第八阶段：背包空间验证 ===
        // 验证双方背包是否有足够空间存放对方的物品
        TradeStatusInfo myCanCompleteInfo, hisCanCompleteInfo;
        hisCanCompleteInfo.Result = trader->CanStoreItems(myItems, TRADE_SLOT_TRADED_COUNT, &hisCanCompleteInfo.ItemLimitCategoryId);
        myCanCompleteInfo.Result = _player->CanStoreItems(hisItems, TRADE_SLOT_TRADED_COUNT, &myCanCompleteInfo.ItemLimitCategoryId);

        // 清除物品交易标志（CanStoreItems检查后）
        clearAcceptTradeMode(myItems, hisItems);

        // 如果我方背包空间不足，报告错误
        if (myCanCompleteInfo.Result != EQUIP_ERR_OK)
        {
            clearAcceptTradeMode(my_trade, his_trade);

            myCanCompleteInfo.Status = TRADE_STATUS_CLOSE_WINDOW;
            trader->GetSession()->SendTradeStatus(myCanCompleteInfo);
            myCanCompleteInfo.IsTargetResult = true;
            SendTradeStatus(myCanCompleteInfo);
            my_trade->SetAccepted(false);
            his_trade->SetAccepted(false);
            delete my_spell;
            delete his_spell;
            return;
        }
        // 如果对方背包空间不足，报告错误
        else if (hisCanCompleteInfo.Result != EQUIP_ERR_OK)
        {
            clearAcceptTradeMode(my_trade, his_trade);

            hisCanCompleteInfo.Status = TRADE_STATUS_CLOSE_WINDOW;
            SendTradeStatus(hisCanCompleteInfo);
            hisCanCompleteInfo.IsTargetResult = true;
            trader->GetSession()->SendTradeStatus(hisCanCompleteInfo);
            my_trade->SetAccepted(false);
            his_trade->SetAccepted(false);
            delete my_spell;
            delete his_spell;
            return;
        }

        // === 第九阶段：执行交易 ===

        // 步骤1：从双方背包中移除交易物品
        for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; ++i)
        {
            // 移除我方物品
            if (myItems[i])
            {
                // 设置赠送者GUID（用于包装物品）
                myItems[i]->SetGuidValue(ITEM_FIELD_GIFTCREATOR, _player->GetGUID());
                // 从背包中移除物品
                _player->MoveItemFromInventory(myItems[i]->GetBagSlot(), myItems[i]->GetSlot(), true);
            }
            // 移除对方物品
            if (hisItems[i])
            {
                // 设置赠送者GUID
                hisItems[i]->SetGuidValue(ITEM_FIELD_GIFTCREATOR, trader->GetGUID());
                // 从背包中移除物品
                trader->MoveItemFromInventory(hisItems[i]->GetBagSlot(), hisItems[i]->GetSlot(), true);
            }
        }

        // 步骤2：将物品转移到对方的背包
        moveItems(myItems, hisItems);

        // === 第十阶段：金币交换和日志记录 ===

        // 记录GM金币交易日志
        if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
        {
            // 记录我方给出的金币
            if (my_trade->GetMoney() > 0)
            {
                sLog->OutCommand(_player->GetSession()->GetAccountId(), "GM {} (Account: {}) give money (Amount: {}) to player: {} (Account: {})",
                    _player->GetName(), _player->GetSession()->GetAccountId(),
                    my_trade->GetMoney(),
                    trader->GetName(), trader->GetSession()->GetAccountId());
            }

            // 记录对方给出的金币
            if (his_trade->GetMoney() > 0)
            {
                sLog->OutCommand(trader->GetSession()->GetAccountId(), "GM {} (Account: {}) give money (Amount: {}) to player: {} (Account: {})",
                    trader->GetName(), trader->GetSession()->GetAccountId(),
                    his_trade->GetMoney(),
                    _player->GetName(), _player->GetSession()->GetAccountId());
            }
        }

        // 执行金币交换
        _player->ModifyMoney(-int32(my_trade->GetMoney()));     // 扣除我方给出的金币
        _player->ModifyMoney(his_trade->GetMoney());             // 增加我方收到的金币
        trader->ModifyMoney(-int32(his_trade->GetMoney()));      // 扣除对方给出的金币
        trader->ModifyMoney(my_trade->GetMoney());               // 增加对方收到的金币

        // === 第十一阶段：施放交易法术 ===
        if (my_spell)
            my_spell->prepare(my_targets);

        if (his_spell)
            his_spell->prepare(his_targets);

        // === 第十二阶段：清理交易数据 ===

        // 清除交易模式标志
        clearAcceptTradeMode(my_trade, his_trade);

        // 删除交易数据对象
        delete _player->m_trade;
        _player->m_trade = nullptr;
        delete trader->m_trade;
        trader->m_trade = nullptr;

        // === 第十三阶段：保存到数据库 ===
        // 使用事务保证数据一致性（SaveInventoryAndGoldToDB自身不包含事务保护）
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        _player->SaveInventoryAndGoldToDB(trans);
        trader->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        // 发送交易完成通知
        info.Status = TRADE_STATUS_TRADE_COMPLETE;
        trader->GetSession()->SendTradeStatus(info);
        SendTradeStatus(info);
    }
    else
    {
        // 对方尚未确认，仅通知对方我方已接受
        info.Status = TRADE_STATUS_TRADE_ACCEPT;
        trader->GetSession()->SendTradeStatus(info);
    }
}

/**
 * @brief 处理取消交易确认的消息
 *
 * 当玩家在交易确认后再次点击取消确认按钮时调用此函数
 * 撤销之前的交易确认状态
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 调用时机：
 * - 玩家在交易窗口中取消"接受"状态
 * - 客户端发送 CMSG_UNACCEPT_TRADE 消息
 */
void WorldSession::HandleUnacceptTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 获取我方的交易数据
    TradeData* my_trade = _player->GetTradeData();
    if (!my_trade)
        return;

    // 取消接受状态，并通知对方
    my_trade->SetAccepted(false, true);
}

/**
 * @brief 处理开始交易的消息
 *
 * 当玩家接受交易请求后，正式打开交易窗口时调用此函数
 * 向双方发送交易窗口打开的消息
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 调用时机：
 * - 玩家接受交易请求，交易窗口正式打开
 * - 客户端发送 CMSG_BEGIN_TRADE 消息
 */
void WorldSession::HandleBeginTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 获取我方的交易数据
    TradeData* my_trade = _player->m_trade;
    if (!my_trade)
        return;

    // 向双方发送交易窗口打开的状态消息
    TradeStatusInfo info;
    info.Status = TRADE_STATUS_OPEN_WINDOW;
    my_trade->GetTrader()->GetSession()->SendTradeStatus(info);
    SendTradeStatus(info);
}

/**
 * @brief 发送交易取消消息
 *
 * 向客户端发送交易取消的状态消息
 * 在玩家登出或最近登出时不发送
 *
 * @param status 交易取消的状态码
 *
 * 调用时机：
 * - 交易因各种原因被取消时
 * - 需要通知客户端交易已终止
 *
 * 注意事项：
 * - 玩家登出时不发送，避免网络错误
 */
void WorldSession::SendCancelTrade(TradeStatus status)
{
    // 如果玩家正在登出或最近登出，不发送消息
    if (PlayerRecentlyLoggedOut() || PlayerLogout())
        return;

    // 发送交易取消状态
    TradeStatusInfo info;
    info.Status = status;
    SendTradeStatus(info);
}

/**
 * @brief 处理取消交易的消息
 *
 * 当玩家主动取消交易时调用此函数
 * 终止正在进行的交易流程
 *
 * @param recvPacket 接收到的网络数据包（未使用）
 *
 * 调用时机：
 * - 玩家点击交易窗口的关闭按钮
 * - 玩家按下ESC键关闭交易窗口
 * - 登出完成后也会发送此消息
 * - 客户端发送 CMSG_CANCEL_TRADE 消息
 *
 * 注意事项：
 * - 状态为 STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT 时也能接收此消息
 * - 需要检查 _player 是否存在
 */
void WorldSession::HandleCancelTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // 登出完成后也会发送此消息
    // 需要检查玩家对象是否存在（因为STATUS_LOGGEDIN_OR_RECENTLY_LOGGOUT状态）
    if (_player)
        _player->TradeCancel(true);
}

/**
 * @brief 处理发起交易请求的消息
 *
 * 当玩家向另一个玩家发起交易请求时调用此函数
 * 执行完整的交易前置条件检查
 *
 * @param recvPacket 接收到的网络数据包，包含目标玩家的GUID
 *
 * 验证流程：
 * 1. 检查发起者是否已有进行中的交易
 * 2. 检查发起者是否存活
 * 3. 检查发起者是否处于眩晕状态
 * 4. 检查发起者是否正在登出
 * 5. 检查发起者是否在飞行中
 * 6. 检查发起者等级是否满足要求
 * 7. 检查目标玩家是否存在
 * 8. 检查目标玩家是否已有交易
 * 9. 检查目标玩家是否存活
 * 10. 检查目标玩家是否在飞行中
 * 11. 检查目标玩家是否处于眩晕状态
 * 12. 检查目标玩家是否正在登出
 * 13. 检查阵营是否允许交易
 * 14. 检查双方距离是否合适
 *
 * 调用时机：
 * - 玩家右键点击另一个玩家并选择"交易"
 * - 客户端发送 CMSG_INITIATE_TRADE 消息
 *
 * 性能注意事项：
 * - 多项条件检查，避免无效的交易请求
 * - 使用 FindPlayer 查找目标玩家
 */
void WorldSession::HandleInitiateTradeOpcode(WorldPacket& recvPacket)
{
    // 读取目标玩家的GUID
    ObjectGuid ID;
    recvPacket >> ID;

    // 如果发起者已有交易，直接返回
    if (GetPlayer()->m_trade)
        return;

    TradeStatusInfo info;

    // === 发起者状态检查 ===

    // 检查发起者是否存活
    if (!GetPlayer()->IsAlive())
    {
        info.Status = TRADE_STATUS_YOU_DEAD;
        SendTradeStatus(info);
        return;
    }

    // 检查发起者是否处于眩晕状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_STUNNED))
    {
        info.Status = TRADE_STATUS_YOU_STUNNED;
        SendTradeStatus(info);
        return;
    }

    // 检查发起者是否正在登出
    if (isLogingOut())
    {
        info.Status = TRADE_STATUS_YOU_LOGOUT;
        SendTradeStatus(info);
        return;
    }

    // 检查发起者是否在飞行中
    if (GetPlayer()->IsInFlight())
    {
        info.Status = TRADE_STATUS_TARGET_TO_FAR;
        SendTradeStatus(info);
        return;
    }

    // 检查发起者等级是否满足最低交易等级要求
    if (GetPlayer()->GetLevel() < sWorld->getIntConfig(CONFIG_TRADE_LEVEL_REQ))
    {
        SendNotification(GetTrinityString(LANG_TRADE_REQ), sWorld->getIntConfig(CONFIG_TRADE_LEVEL_REQ));
        info.Status = TRADE_STATUS_CLOSE_WINDOW;
        SendTradeStatus(info);
        return;
    }

    // === 目标玩家检查 ===

    // 查找目标玩家
    Player* pOther = ObjectAccessor::FindPlayer(ID);

    // 检查目标玩家是否存在
    if (!pOther)
    {
        info.Status = TRADE_STATUS_NO_TARGET;
        SendTradeStatus(info);
        return;
    }

    // 检查目标是否是自己或是否已有交易
    if (pOther == GetPlayer() || pOther->m_trade)
    {
        info.Status = TRADE_STATUS_BUSY;
        SendTradeStatus(info);
        return;
    }

    // 检查目标玩家是否存活
    if (!pOther->IsAlive())
    {
        info.Status = TRADE_STATUS_TARGET_DEAD;
        SendTradeStatus(info);
        return;
    }

    // 检查目标玩家是否在飞行中
    if (pOther->IsInFlight())
    {
        info.Status = TRADE_STATUS_TARGET_TO_FAR;
        SendTradeStatus(info);
        return;
    }

    // 检查目标玩家是否处于眩晕状态
    if (pOther->HasUnitState(UNIT_STATE_STUNNED))
    {
        info.Status = TRADE_STATUS_TARGET_STUNNED;
        SendTradeStatus(info);
        return;
    }

    // 检查目标玩家是否正在登出
    if (pOther->GetSession()->isLogingOut())
    {
        info.Status = TRADE_STATUS_TARGET_LOGOUT;
        SendTradeStatus(info);
        return;
    }

    // 检查阵营限制（不同阵营默认不能交易，除非有权限）
    if (pOther->GetTeam() != _player->GetTeam() &&
        (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_TRADE) &&
         !GetPlayer()->GetSession()->HasPermission(rbac::RBAC_PERM_ALLOW_TWO_SIDE_TRADE)))
    {
        info.Status = TRADE_STATUS_WRONG_FACTION;
        SendTradeStatus(info);
        return;
    }

    // 检查双方距离是否在交易范围内
    if (!pOther->IsWithinDistInMap(_player, TRADE_DISTANCE, false))
    {
        info.Status = TRADE_STATUS_TARGET_TO_FAR;
        SendTradeStatus(info);
        return;
    }

    // === 所有问题检查通过，开始交易 ===

    // 为双方创建交易数据对象
    _player->m_trade = new TradeData(_player, pOther);
    pOther->m_trade = new TradeData(pOther, _player);

    // 向目标玩家发送交易开始通知
    info.Status = TRADE_STATUS_BEGIN_TRADE;
    info.TraderGuid = _player->GetGUID();
    pOther->GetSession()->SendTradeStatus(info);
}

/**
 * @brief 处理设置交易金币的消息
 *
 * 当玩家在交易窗口中修改金币数量时调用此函数
 * 更新交易数据中的金币数额
 *
 * @param recvPacket 接收到的网络数据包，包含金币数量
 *
 * 调用时机：
 * - 玩家在交易窗口的金币输入框中输入金币数量
 * - 客户端发送 CMSG_SET_TRADE_GOLD 消息
 *
 * 安全考虑：
 * - 实际金币验证在交易确认时进行
 * - 此处仅更新交易数据，不扣除金币
 */
void WorldSession::HandleSetTradeGoldOpcode(WorldPacket& recvPacket)
{
    // 读取金币数量
    uint32 gold;
    recvPacket >> gold;

    // 获取交易数据
    TradeData* my_trade = _player->GetTradeData();
    if (!my_trade)
        return;

    // 更新交易金币数量
    my_trade->SetMoney(gold);
}

/**
 * @brief 处理设置交易物品的消息
 *
 * 当玩家将物品放入交易窗口时调用此函数
 * 执行完整的物品验证并更新交易数据
 *
 * @param recvPacket 接收到的网络数据包，包含交易槽位、背包和槽位信息
 *
 * 验证流程：
 * 1. 检查交易槽位是否有效
 * 2. 检查物品是否存在
 * 3. 检查物品是否可交易
 * 4. 检查物品是否已在交易中（防止重复放置）
 * 5. 检查物品绑定状态
 *
 * 调用时机：
 * - 玩家将物品从背包拖放到交易窗口
 * - 客户端发送 CMSG_SET_TRADE_ITEM 消息
 *
 * 安全措施：
 * - 验证槽位编号防止越界
 * - 验证物品可交易性防止非法交易
 * - 验证物品唯一性防止复制
 * - 验证绑定状态防止违规交易
 */
void WorldSession::HandleSetTradeItemOpcode(WorldPacket& recvPacket)
{
    // 读取交易槽位、背包和物品槽位
    uint8 tradeSlot;
    uint8 bag;
    uint8 slot;

    recvPacket >> tradeSlot;
    recvPacket >> bag;
    recvPacket >> slot;

    // 获取交易数据
    TradeData* my_trade = _player->GetTradeData();
    if (!my_trade)
        return;

    TradeStatusInfo info;

    // 检查交易槽位是否有效
    if (tradeSlot >= TRADE_SLOT_COUNT)
    {
        info.Status = TRADE_STATUS_TRADE_CANCELED;
        SendTradeStatus(info);
        return;
    }

    // 获取物品并验证（防止作弊，正常客户端操作不会失败）
    Item* item = _player->GetItemByPos(bag, slot);

    // 验证物品是否存在且可交易
    // TRADE_SLOT_NONTRADED 是不可交易槽位（用于放置接受法术的物品）
    if (!item || (tradeSlot != TRADE_SLOT_NONTRADED && !item->CanBeTraded(false, true)))
    {
        info.Status = TRADE_STATUS_TRADE_CANCELED;
        SendTradeStatus(info);
        return;
    }

    // 获取物品GUID
    ObjectGuid iGUID = item->GetGUID();

    // 防止通过作弊或客户端bug将同一物品放入多个交易槽位
    if (my_trade->HasItem(iGUID))
    {
        // 作弊尝试，取消交易
        info.Status = TRADE_STATUS_TRADE_CANCELED;
        SendTradeStatus(info);
        return;
    }

    // 检查物品绑定状态（某些物品只能绑定给特定玩家）
    if (tradeSlot != TRADE_SLOT_NONTRADED && item->IsBindedNotWith(my_trade->GetTrader()))
    {
        info.Status = TRADE_STATUS_NOT_ON_TAPLIST;
        info.Slot = tradeSlot;
        SendTradeStatus(info);
        return;
    }

    // 所有检查通过，将物品放入交易槽位
    my_trade->SetItem(TradeSlots(tradeSlot), item);
}

/**
 * @brief 处理清除交易物品的消息
 *
 * 当玩家从交易窗口移除物品时调用此函数
 * 将指定槽位的物品清空
 *
 * @param recvPacket 接收到的网络数据包，包含交易槽位编号
 *
 * 调用时机：
 * - 玩家将物品从交易窗口拖回背包
 * - 玩家右键点击交易窗口中的物品
 * - 客户端发送 CMSG_CLEAR_TRADE_ITEM 消息
 */
void WorldSession::HandleClearTradeItemOpcode(WorldPacket& recvPacket)
{
    // 读取交易槽位
    uint8 tradeSlot;
    recvPacket >> tradeSlot;

    // 获取交易数据
    TradeData* my_trade = _player->m_trade;
    if (!my_trade)
        return;

    // 检查槽位是否有效
    if (tradeSlot >= TRADE_SLOT_COUNT)
        return;

    // 清除该槽位的物品
    my_trade->SetItem(TradeSlots(tradeSlot), nullptr);
}
