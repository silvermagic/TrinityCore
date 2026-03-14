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
 * @file TradeData.cpp
 * @brief 玩家交易数据管理实现
 *
 * 本文件实现了 TradeData 类的所有成员函数，提供玩家交易数据的管理功能。
 * 主要功能包括：
 * - 交易物品的添加、移除和查询
 * - 交易金币的设置和验证
 * - 交易法术的管理
 * - 交易状态的同步更新
 */

#include "TradeData.h"
#include "Item.h"
#include "Player.h"
#include "WorldSession.h"

/**
 * @brief 获取交易对象的交易数据
 * @return 对方玩家的 TradeData 对象指针
 *
 * 实现说明：
 * 直接通过对方玩家的 GetTradeData() 方法获取其交易数据对象。
 */
TradeData* TradeData::GetTraderData() const
{
    return _trader->GetTradeData();
}

/**
 * @brief 获取指定槽位的物品
 * @param slot 交易槽位索引
 * @return 物品指针，若槽位为空则返回 nullptr
 *
 * 实现说明：
 * 首先检查槽位中是否有物品 GUID，若有则通过玩家的 GetItemByGuid 方法查找物品对象。
 */
Item* TradeData::GetItem(TradeSlots slot) const
{
    return !_items[slot].IsEmpty() ? _player->GetItemByGuid(_items[slot]) : nullptr;
}

/**
 * @brief 检查交易中是否包含指定物品
 * @param itemGuid 物品 GUID
 * @return 是否包含该物品
 *
 * 实现说明：
 * 线性遍历所有交易槽位，查找匹配的物品 GUID。
 */
bool TradeData::HasItem(ObjectGuid itemGuid) const
{
    // 遍历所有交易槽位，检查物品 GUID 是否匹配
    for (uint8 i = 0; i < TRADE_SLOT_COUNT; ++i)
        if (_items[i] == itemGuid)
            return true;

    return false;
}

/**
 * @brief 根据物品 GUID 查找其所在的交易槽位
 * @param itemGuid 物品 GUID
 * @return 槽位索引，若未找到返回 TRADE_SLOT_INVALID
 *
 * 实现说明：
 * 线性遍历所有交易槽位，找到第一个匹配的槽位索引。
 */
TradeSlots TradeData::GetTradeSlotForItem(ObjectGuid itemGuid) const
{
    // 遍历所有交易槽位，查找物品所在的槽位
    for (uint8 i = 0; i < TRADE_SLOT_COUNT; ++i)
        if (_items[i] == itemGuid)
            return TradeSlots(i);

    // 未找到物品，返回无效槽位标识
    return TRADE_SLOT_INVALID;
}

/**
 * @brief 获取施法使用的物品
 * @return 施法物品指针，若无则返回 nullptr
 *
 * 实现说明：
 * 检查是否有施法物品 GUID，若有则通过玩家的 GetItemByGuid 方法查找物品对象。
 */
Item* TradeData::GetSpellCastItem() const
{
    return !_spellCastItem.IsEmpty() ? _player->GetItemByGuid(_spellCastItem) : nullptr;
}

/**
 * @brief 设置交易槽位中的物品
 * @param slot 目标槽位索引
 * @param item 要放入的物品指针，可为 nullptr
 * @param update 是否强制更新，默认为 false
 *
 * 实现说明：
 * 1. 提取物品的 GUID
 * 2. 检查是否需要更新（GUID 未变化且非强制更新则直接返回）
 * 3. 更新槽位中的物品 GUID
 * 4. 重置双方的接受状态（任何修改都会重置确认）
 * 5. 发送更新包给对方
 * 6. 如果修改的是非交易槽位（TRADE_SLOT_NONTRADED），清除对方可能施放的法术
 * 7. 清除自己可能施放的法术（可能是材料被移动了）
 *
 * 设计考虑：
 * - 非交易槽位的修改需要清除对方的法术，因为该物品可能已被附魔
 * - 自己的法术也需要清除，因为施法材料可能被移走
 */
void TradeData::SetItem(TradeSlots slot, Item* item, bool update /*= false*/)
{
    // 提取物品 GUID，若 item 为 nullptr 则使用空 GUID
    ObjectGuid itemGuid;
    if (item)
        itemGuid = item->GetGUID();

    // 检查是否需要更新：GUID 未变化且非强制更新时直接返回
    if (_items[slot] == itemGuid && !update)
        return;

    // 更新槽位中的物品 GUID
    _items[slot] = itemGuid;

    // 任何物品变动都会重置双方的接受状态
    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    // 发送更新包给对方
    Update();

    // 特殊处理：非交易槽位物品变更时，需要清除对方可能施放的法术
    // 因为对方可能对这个物品施放了附魔等法术，物品变更后法术应失效
    if (slot == TRADE_SLOT_NONTRADED)
        GetTraderData()->SetSpell(0);

    // 清除自己可能施放的法术，因为施法材料可能被移动或替换
    SetSpell(0);
}

/**
 * @brief 设置对非交易槽位施放的法术
 * @param spell_id 法术 ID，设为 0 表示清除法术
 * @param castItem 施法使用的物品，默认为 nullptr
 *
 * 实现说明：
 * 1. 提取施法物品的 GUID
 * 2. 检查法术信息是否变化，未变化则直接返回
 * 3. 更新法术 ID 和施法物品 GUID
 * 4. 重置双方的接受状态
 * 5. 发送更新包给物品拥有者（对方）和施法者（自己）
 *
 * 设计考虑：
 * - 法术信息变化时需要通知双方
 * - 先发送给对方（物品拥有者），再发送给自己（施法者）
 */
void TradeData::SetSpell(uint32 spell_id, Item* castItem /*= nullptr*/)
{
    // 提取施法物品的 GUID，若 castItem 为 nullptr 则使用空 GUID
    ObjectGuid itemGuid = castItem ? castItem->GetGUID() : ObjectGuid::Empty;

    // 检查法术信息是否变化，未变化则直接返回
    if (_spell == spell_id && _spellCastItem == itemGuid)
        return;

    // 更新法术 ID 和施法物品 GUID
    _spell = spell_id;
    _spellCastItem = itemGuid;

    // 法术变化会重置双方的接受状态
    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    // 发送更新包给物品拥有者（对方），让其看到法术效果
    Update(true);
    // 发送更新包给施法者（自己），确认法术已设置
    Update(false);
}

/**
 * @brief 设置交易中的金币数量
 * @param money 金币数量（铜币单位）
 *
 * 实现说明：
 * 1. 检查金币数量是否变化，未变化则直接返回
 * 2. 验证玩家是否拥有足够的金币
 * 3. 若金币不足，发送错误消息并返回（不更新金币数量）
 * 4. 更新金币数量
 * 5. 重置双方的接受状态
 * 6. 发送更新包给对方
 *
 * 设计考虑：
 * - 必须先验证金币是否足够，防止设置超出持有量的金币
 * - 金币不足时发送 TRADE_STATUS_CLOSE_WINDOW 消息，携带金币不足错误
 * - 金币变化只通知对方，不需要通知自己
 */
void TradeData::SetMoney(uint32 money)
{
    // 检查金币数量是否变化，未变化则直接返回
    if (_money == money)
        return;

    // 验证玩家是否拥有足够的金币
    if (!_player->HasEnoughMoney(money))
    {
        // 金币不足，发送错误消息给玩家
        TradeStatusInfo info;
        info.Status = TRADE_STATUS_CLOSE_WINDOW;  // 关闭交易窗口
        info.Result = EQUIP_ERR_NOT_ENOUGH_MONEY; // 错误码：金币不足
        _player->GetSession()->SendTradeStatus(info);
        return;
    }

    // 更新金币数量
    _money = money;

    // 金币变化会重置双方的接受状态
    SetAccepted(false);
    GetTraderData()->SetAccepted(false);

    // 发送更新包给对方
    Update(true);
}

/**
 * @brief 发送交易更新数据包
 * @param forTrader 是否发送给对方玩家
 *
 * 实现说明：
 * 根据 forTrader 参数决定发送目标：
 * - true：发送给对方玩家（_trader），参数 true 表示这是对方看到的数据
 * - false：发送给自己（_player），参数 false 表示这是自己看到的数据
 *
 * 调用时机：交易数据发生变化需要同步时，由其他成员函数内部调用
 */
void TradeData::Update(bool forTrader /*= true*/) const
{
    if (forTrader)
        // 发送本方的交易数据给对方玩家，对方参数为 true
        _trader->GetSession()->SendUpdateTrade(true);
    else
        // 发送本方的交易数据给自己，参数为 false
        _player->GetSession()->SendUpdateTrade(false);
}

/**
 * @brief 设置交易接受状态
 * @param state 接受状态
 * @param forTrader 是否发送消息给对方玩家
 *
 * 实现说明：
 * 1. 更新接受状态标志
 * 2. 如果状态变为 false（取消接受），发送 TRADE_STATUS_BACK_TO_TRADE 消息通知相应玩家
 *
 * 设计考虑：
 * - 取消接受时需要通知对方或自己，让其知道交易状态已回退
 * - 接受状态变化时不自动发送更新包，由调用者决定是否需要更新
 */
void TradeData::SetAccepted(bool state, bool forTrader /*= false*/)
{
    // 更新接受状态
    _accepted = state;

    // 如果取消接受，发送消息通知相应玩家
    if (!state)
    {
        TradeStatusInfo info;
        info.Status = TRADE_STATUS_BACK_TO_TRADE; // 交易回退状态
        if (forTrader)
            // 发送给对方玩家
            _trader->GetSession()->SendTradeStatus(info);
        else
            // 发送给自己
            _player->GetSession()->SendTradeStatus(info);
    }
}
