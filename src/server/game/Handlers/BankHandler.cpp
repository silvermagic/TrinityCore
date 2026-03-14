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
 * @file BankHandler.cpp
 * @brief 银行处理模块
 *
 * 本模块处理所有与银行相关的网络消息,包括:
 * - 打开银行窗口
 * - 存取物品
 * - 购买银行背包槽位
 *
 * 银行是玩家的个人存储空间,可以存放物品和金币
 * 玩家可以通过银行NPC或GM命令访问银行
 */

#include "BankPackets.h"
#include "Item.h"
#include "DBCStores.h"
#include "Log.h"
#include "NPCPackets.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief 检查玩家是否可以使用银行
 * @param bankerGUID 银行NPC的GUID(可选参数,默认为0)
 * @return true表示可以使用银行,false表示不能使用
 *
 * 验证玩家是否有权限访问银行:
 * - 如果bankerGUID为空,使用当前记录的银行NPC
 * - 如果是使用GM命令访问银行,允许访问
 * - 如果是通过NPC访问,验证NPC是否是银行NPC且可交互
 */
bool WorldSession::CanUseBank(ObjectGuid bankerGUID) const
{
    // bankerGUID参数是可选的,默认为0
    if (!bankerGUID)
        bankerGUID = m_currentBankerGUID;

    // 检查是否是使用GM命令访问银行(玩家自己的GUID且与当前银行GUID相同)
    bool isUsingBankCommand = (bankerGUID == GetPlayer()->GetGUID() && bankerGUID == m_currentBankerGUID);

    if (!isUsingBankCommand)
    {
        // 通过NPC访问,验证NPC是否是银行NPC且可交互
        Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(bankerGUID, UNIT_NPC_FLAG_BANKER);
        if (!creature)
            return false;
    }

    return true;
}

/**
 * @brief 处理银行NPC激活消息
 * @param packet 接收到的数据包,包含银行NPC的GUID
 *
 * 当玩家点击银行NPC时调用,打开银行窗口
 * 执行以下操作:
 * - 验证NPC是否是银行NPC且可交互
 * - 移除假死状态
 * - 发送显示银行窗口的消息
 */
void WorldSession::HandleBankerActivateOpcode(WorldPackets::NPC::Hello& packet)
{
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(packet.Unit, UNIT_NPC_FLAG_BANKER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleBankerActivateOpcode - {} not found or you can not interact with him.", packet.Unit.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    SendShowBank(packet.Unit);
}

/**
 * @brief 处理自动存入银行物品的消息
 * @param packet 接收到的数据包,包含物品所在背包和槽位
 *
 * 当玩家将物品从背包存入银行时调用
 * 执行以下操作:
 * - 验证玩家是否可以使用银行
 * - 检查物品是否存在
 * - 验证物品是否可以存入银行
 * - 将物品从背包移除并存入银行
 * - 检查任务物品状态
 *
 * 性能注意: 可能涉及物品堆叠拆分操作
 */
void WorldSession::HandleAutoBankItemOpcode(WorldPackets::Bank::AutoBankItem& packet)
{
    TC_LOG_DEBUG("network", "STORAGE: receive bag = {}, slot = {}", packet.Bag, packet.Slot);

    if (!CanUseBank())
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAutoBankItemOpcode - Unit ({}) not found or you can't interact with him.", m_currentBankerGUID.ToString());
        return;
    }

    // 获取要存入的物品
    Item* item = _player->GetItemByPos(packet.Bag, packet.Slot);
    if (!item)
        return;

    // 检查物品是否可以存入银行
    ItemPosCountVec dest;
    InventoryResult msg = _player->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, item, nullptr);
        return;
    }

    // 如果目标位置就是当前位置,无需移动
    if (dest.size() == 1 && dest[0].pos == item->GetPos())
    {
        _player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);
        return;
    }

    // 从背包移除物品并存入银行
    _player->RemoveItem(packet.Bag, packet.Slot, true);
    _player->ItemRemovedQuestCheck(item->GetEntry(), item->GetCount());
    _player->BankItem(dest, item, true);
}

/**
 * @brief 处理自动从银行取出物品的消息
 * @param packet 接收到的数据包,包含物品所在背包和槽位
 *
 * 当玩家将物品从银行取出到背包,或在银行内移动物品时调用
 * 支持两种操作:
 * - 从银行移动物品到背包
 * - 从背包移动物品到银行(与AutoBankItem功能类似)
 *
 * 执行以下操作:
 * - 验证玩家是否可以使用银行
 * - 根据物品位置判断操作方向(银行->背包 或 背包->银行)
 * - 验证目标位置是否可以存放物品
 * - 执行物品转移
 * - 更新任务物品状态
 */
void WorldSession::HandleAutoStoreBankItemOpcode(WorldPackets::Bank::AutoStoreBankItem& packet)
{
    TC_LOG_DEBUG("network", "STORAGE: receive bag = {}, slot = {}", packet.Bag, packet.Slot);

    if (!CanUseBank())
    {
        TC_LOG_DEBUG("network", "WORLD: HandleAutoStoreBankItemOpcode - Unit ({}) not found or you can't interact with him.", m_currentBankerGUID.ToString());
        return;
    }

    Item* item = _player->GetItemByPos(packet.Bag, packet.Slot);
    if (!item)
        return;

    if (_player->IsBankPos(packet.Bag, packet.Slot))  // 从银行移动到背包
    {
        ItemPosCountVec dest;
        InventoryResult msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, item, nullptr);
            return;
        }

        _player->RemoveItem(packet.Bag, packet.Slot, true);
        if (Item const* storedItem = _player->StoreItem(dest, item, true))
            _player->ItemAddedQuestCheck(storedItem->GetEntry(), storedItem->GetCount());
    }
    else  // 从背包移动到银行
    {
        ItemPosCountVec dest;
        InventoryResult msg = _player->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            _player->SendEquipError(msg, item, nullptr);
            return;
        }

        _player->RemoveItem(packet.Bag, packet.Slot, true);
        _player->BankItem(dest, item, true);
    }
}

/**
 * @brief 处理购买银行背包槽位的消息
 * @param buyBankSlot 接收到的数据包,包含银行NPC的GUID
 *
 * 当玩家购买新的银行背包槽位时调用
 * 执行以下操作:
 * - 验证玩家是否可以使用银行
 * - 获取当前槽位数量并计算下一个槽位
 * - 从DBC查找槽位价格
 * - 验证玩家是否有足够的金币
 * - 扣除金币并增加槽位
 * - 发送购买结果给客户端
 * - 更新成就进度
 *
 * 银行槽位价格递增,越往后越贵
 */
void WorldSession::HandleBuyBankSlotOpcode(WorldPackets::Bank::BuyBankSlot& buyBankSlot)
{
    WorldPackets::Bank::BuyBankSlotResult packet;
    if (!CanUseBank(buyBankSlot.Banker))
    {
        packet.Result = ERR_BANKSLOT_NOTBANKER;
        SendPacket(packet.Write());
        TC_LOG_DEBUG("network", "WORLD: HandleBuyBankSlotOpcode - {} not found or you can't interact with him.", buyBankSlot.Banker.ToString());
        return;
    }

    uint32 slot = _player->GetBankBagSlotCount();

    // 计算下一个槽位编号
    ++slot;

    TC_LOG_INFO("network", "PLAYER: Buy bank bag slot, slot number = {}", slot);

    // 从DBC获取槽位价格
    BankBagSlotPricesEntry const* slotEntry = sBankBagSlotPricesStore.LookupEntry(slot);

    if (!slotEntry)
    {
        packet.Result = ERR_BANKSLOT_FAILED_TOO_MANY;
        SendPacket(packet.Write());
        return;
    }

    uint32 price = slotEntry->Cost;

    // 检查玩家是否有足够的金币
    if (!_player->HasEnoughMoney(price))
    {
        packet.Result = ERR_BANKSLOT_INSUFFICIENT_FUNDS;
        SendPacket(packet.Write());
        return;
    }

    // 扣除金币并增加槽位
    _player->SetBankBagSlotCount(slot);
    _player->ModifyMoney(-int32(price));

    packet.Result = ERR_BANKSLOT_OK;
    SendPacket(packet.Write());

    // 更新成就进度
    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT);
}

/**
 * @brief 发送显示银行窗口的消息给客户端
 * @param guid 银行NPC的GUID
 *
 * 调用时机: 当玩家与银行NPC交互时
 *
 * 功能:
 * - 记录当前银行NPC的GUID
 * - 发送显示银行窗口的消息给客户端
 * - 客户端收到消息后会打开银行界面
 */
void WorldSession::SendShowBank(ObjectGuid guid)
{
    m_currentBankerGUID = guid;
    WorldPackets::Bank::ShowBank packet;
    packet.Banker = guid;
    SendPacket(packet.Write());
}
