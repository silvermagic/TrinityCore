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
 * @file LootHandler.cpp
 * @brief 战利品系统网络消息处理器
 *
 * 本模块负责处理玩家与战利品系统交互的所有网络消息，包括：
 * - 从尸体、游戏对象、物品容器中拾取物品
 * - 拾取金币并分配给队伍成员
 * - 战利品窗口的打开和关闭
 * - 团队分配模式下物品分配给指定玩家
 *
 * 支持的战利品来源类型：
 * - 生物尸体（Creature）- 普通击杀掉落
 * - 游戏对象（GameObject）- 箱子、钓鱼等
 * - 物品容器（Item）- 拆解、选矿等
 * - 尸体对象（Corpse）- 战场徽章拾取
 *
 * @note 战利品系统与团队系统紧密关联，支持多种分配方式
 *       （自由拾取、轮流拾取、团队分配、队长分配）
 */

#include "WorldSession.h"
#include "Common.h"
#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "LootItemStorage.h"
#include "LootMgr.h"
#include "Map.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldPacket.h"

/**
 * @brief 处理自动存储战利品物品的请求
 *
 * 当玩家在战利品窗口中点击某个物品时，客户端发送此消息请求将物品存入背包。
 * 该函数会验证玩家是否有权限拾取该物品，并处理不同类型战利品容器的逻辑。
 *
 * @param recvData 接收的网络数据包，包含战利品槽位索引
 *
 * 调用时机：
 * - 玩家在战利品窗口中右键点击或左键点击物品时
 * - 客户端发送 CMSG_AUTOSTORE_LOOT_ITEM 消息
 *
 * 处理流程：
 * 1. 获取玩家当前打开的战利品容器 GUID
 * 2. 根据容器类型（游戏对象/物品/尸体/生物）获取对应的战利品对象
 * 3. 验证玩家与战利品容器的距离和权限
 * 4. 调用 Player::StoreLootItem 执行实际的物品存储
 * 5. 如果物品容器已空且为物品类型，则释放容器
 *
 * 性能注意事项：
 * - 距离检查对于钓鱼浮标和钓鱼洞有特殊处理
 * - 盗贼偷窃时生物可能仍存活
 *
 * @see Player::StoreLootItem()
 * @see Player::GetLootGUID()
 */
void WorldSession::HandleAutostoreLootItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_AUTOSTORE_LOOT_ITEM");
    Player* player = GetPlayer();
    ObjectGuid lguid = player->GetLootGUID();
    Loot* loot = nullptr;
    uint8 lootSlot = 0;

    recvData >> lootSlot;

    if (lguid.IsGameObject())
    {
        GameObject* go = player->GetMap()->GetGameObject(lguid);

        // not check distance for GO in case owned GO (fishing bobber case, for example) or Fishing hole GO
        if (!go || ((go->GetOwnerGUID() != _player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(_player)))
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &go->loot;
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);

        if (!pItem)
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &pItem->loot;
    }
    else if (lguid.IsCorpse())
    {
        Corpse* bones = ObjectAccessor::GetCorpse(*player, lguid);
        if (!bones)
        {
            player->SendLootRelease(lguid);
            return;
        }

        loot = &bones->loot;
    }
    else
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lguid);

        bool lootAllowed = creature && creature->IsAlive() == (player->GetClass() == CLASS_ROGUE && creature->loot.loot_type == LOOT_PICKPOCKETING);
        if (!lootAllowed || !creature->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
        {
            player->SendLootError(lguid, lootAllowed ? LOOT_ERROR_TOO_FAR : LOOT_ERROR_DIDNT_KILL);
            return;
        }

        loot = &creature->loot;
    }

    player->StoreLootItem(lootSlot, loot);

    // 如果玩家拾取了最后一个物品，删除空的容器
    // If player is removing the last LootItem, delete the empty container.
    if (loot->isLooted() && lguid.IsItem())
        player->GetSession()->DoLootRelease(lguid);
}

/**
 * @brief 处理拾取金币的请求
 *
 * 当玩家点击战利品窗口中的金币时，客户端发送此消息请求拾取金币。
 * 如果玩家在队伍中，金币会平均分配给附近符合条件的队友。
 *
 * @param recvData 接收的网络数据包（空数据包）
 *
 * 调用时机：
 * - 玩家在战利品窗口中点击金币图标时
 * - 客户端发送 CMSG_LOOT_MONEY 消息
 *
 * 处理流程：
 * 1. 获取玩家当前打开的战利品容器
 * 2. 根据容器类型获取战利品对象和金币
 * 3. 如果玩家在队伍中且需要分配金币：
 *    - 获取附近符合条件的队友列表
 *    - 计算每个玩家应得的金币数量
 *    - 向每个玩家发送金币并更新成就
 * 4. 如果不需要分配（单人拾取）：
 *    - 直接将金币添加到玩家背包
 * 5. 清空战利品容器中的金币
 * 6. 如果容器已空且为物品类型，释放容器
 *
 * 金币分配规则：
 * - 只有附近的队友才能获得金币分配
 * - 拾取徽章（Corpse）和物品容器不分配金币
 * - 盗贼偷窃（生物存活时）不分配金币
 *
 * 性能注意事项：
 * - 遍历队伍成员时需要检查距离条件
 * - 异步操作会从数据库删除金币拾取记录
 *
 * @see Player::ModifyMoney()
 * @see Group::GetFirstMember()
 */
void WorldSession::HandleLootMoneyOpcode(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_LOOT_MONEY");

    Player* player = GetPlayer();
    ObjectGuid guid = player->GetLootGUID();
    if (!guid)
        return;

    Loot* loot = nullptr;
    bool shareMoney = true;  // 是否需要分配金币给队伍成员

    // 根据战利品容器类型获取对应的战利品对象
    switch (guid.GetHigh())
    {
        case HighGuid::GameObject:
        {
            GameObject* go = GetPlayer()->GetMap()->GetGameObject(guid);

            // 对于玩家拥有的游戏对象（如钓鱼浮标），不检查距离
            // do not check distance for GO if player is the owner of it (ex. fishing bobber)
            if (go && ((go->GetOwnerGUID() == player->GetGUID() || go->IsWithinDistInMap(player))))
                loot = &go->loot;

            break;
        }
        case HighGuid::Corpse:                               // 只在战场移除徽章 / remove insignia ONLY in BG
        {
            Corpse* bones = ObjectAccessor::GetCorpse(*player, guid);

            if (bones && bones->IsWithinDistInMap(player, INTERACTION_DISTANCE))
            {
                loot = &bones->loot;
                shareMoney = false;  // 徽章金币不分配
            }

            break;
        }
        case HighGuid::Item:
        {
            if (Item* item = player->GetItemByGuid(guid))
            {
                loot = &item->loot;
                shareMoney = false;  // 物品容器金币不分配
            }
            break;
        }
        case HighGuid::Unit:
        case HighGuid::Vehicle:
        {
            Creature* creature = player->GetMap()->GetCreature(guid);
            // 盗贼偷窃时生物可能存活
            bool lootAllowed = creature && creature->IsAlive() == (player->GetClass() == CLASS_ROGUE && creature->loot.loot_type == LOOT_PICKPOCKETING);
            if (lootAllowed && creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
            {
                loot = &creature->loot;
                if (creature->IsAlive())
                    shareMoney = false;  // 偷窃金币不分配
            }
            else
                player->SendLootError(guid, lootAllowed ? LOOT_ERROR_TOO_FAR : LOOT_ERROR_DIDNT_KILL);
            break;
        }
        default:
            return;                                         // 不可拾取的类型 / unlootable type
    }

    if (loot)
    {
        loot->NotifyMoneyRemoved();

        // 如果需要分配金币且玩家在队伍中
        // 物品、偷窃和玩家尸体只能单人拾取
        if (shareMoney && player->GetGroup())      //item, pickpocket and players can be looted only single player
        {
            Group* group = player->GetGroup();

            // 收集附近符合条件的队友
            std::vector<Player*> playersNear;
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member)
                    continue;

                if (player->IsAtGroupRewardDistance(member))
                    playersNear.push_back(member);
            }

            // 计算每个玩家应得的金币数量
            uint32 goldPerPlayer = uint32((loot->gold) / (playersNear.size()));

            // 向每个玩家发放金币
            for (std::vector<Player*>::const_iterator i = playersNear.begin(); i != playersNear.end(); ++i)
            {
                (*i)->ModifyMoney(goldPerPlayer);
                (*i)->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);

                // 发送金币拾取通知
                WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
                data << uint32(goldPerPlayer);
                data << uint8(playersNear.size() <= 1); // 控制聊天框显示的文本。0显示"Your share is..."，1显示"You loot..."
                                                        // Controls the text displayed in chat. 0 is "Your share is..." and 1 is "You loot..."
                (*i)->SendDirectMessage(&data);
            }
        }
        else
        {
            // 单人拾取金币
            player->ModifyMoney(loot->gold);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, loot->gold);

            WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
            data << uint32(loot->gold);
            data << uint8(1);   // "You loot..." / "你拾取了..."
            SendPacket(&data);
        }

        loot->gold = 0;

        // 从数据库删除金币拾取记录
        // Delete the money loot record from the DB
        if (loot->containerID > 0)
            sLootItemStorage->RemoveStoredMoneyForContainer(loot->containerID);

        // 如果容器已空，删除容器
        // Delete container if empty
        if (loot->isLooted() && guid.IsItem())
            player->GetSession()->DoLootRelease(guid);
    }
}

/**
 * @brief 处理打开战利品窗口的请求
 *
 * 当玩家右键点击尸体或生物时，客户端发送此消息请求打开战利品窗口。
 * 该函数会验证玩家是否可以打开战利品，并发送战利品列表给客户端。
 *
 * @param recvData 接收的网络数据包，包含目标对象的 GUID
 *
 * 调用时机：
 * - 玩家右键点击可拾取的尸体或生物时
 * - 客户端发送 CMSG_LOOT 消息
 *
 * 处理流程：
 * 1. 从数据包中读取目标对象的 GUID
 * 2. 验证玩家是否存活且目标为生物类型
 * 3. 中断玩家正在施放的法术
 * 4. 移除可能干扰拾取的光环效果
 * 5. 发送战利品列表给客户端
 *
 * 安全检查：
 * - 玩家必须存活才能拾取
 * - 目标必须是生物或载具类型
 *
 * @see Player::SendLoot()
 * @see Player::InterruptNonMeleeSpells()
 */
void WorldSession::HandleLootOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_LOOT");

    ObjectGuid guid;
    recvData >> guid;

    // Check possible cheat
    if (!GetPlayer()->IsAlive() || !guid.IsCreatureOrVehicle())
        return;

    // interrupt cast
    if (GetPlayer()->IsNonMeleeSpellCast(false))
        GetPlayer()->InterruptNonMeleeSpells(false);

    GetPlayer()->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_LOOTING);

    GetPlayer()->SendLoot(guid, LOOT_CORPSE);
}

/**
 * @brief 处理关闭战利品窗口的请求
 *
 * 当玩家关闭战利品窗口时，客户端发送此消息。
 * 该函数会释放战利品容器，并处理相关的清理工作。
 *
 * @param recvData 接收的网络数据包，包含战利品容器的 GUID
 *
 * 调用时机：
 * - 玩家点击关闭战利品窗口时
 * - 玩家离开战利品容器附近时
 * - 客户端发送 CMSG_LOOT_RELEASE 消息
 *
 * 处理流程：
 * 1. 从数据包中读取战利品容器的 GUID
 * 2. 验证是否与玩家当前打开的战利品容器匹配
 * 3. 调用 DoLootRelease 执行实际的释放操作
 *
 * 安全措施：
 * - 使用服务器端存储的 GUID 进行验证，防止作弊者修改 GUID
 *
 * @see DoLootRelease()
 */
void WorldSession::HandleLootReleaseOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_LOOT_RELEASE");

    // 作弊者可能修改 lguid 以阻止正确的战利品释放代码执行并重新拾取
    // 使用内部存储的 GUID 进行验证
    // cheaters can modify lguid to prevent correct apply loot release code and re-loot
    // use internal stored guid
    ObjectGuid guid;
    recvData >> guid;

    if (ObjectGuid lguid = GetPlayer()->GetLootGUID())
        if (lguid == guid)
            DoLootRelease(lguid);
}

/**
 * @brief 执行战利品容器的释放操作
 *
 * 该函数是战利品释放的核心实现，负责处理不同类型战利品容器的清理工作。
 * 当玩家关闭战利品窗口时，系统会调用此函数进行后续处理。
 *
 * @param lguid 战利品容器的 GUID
 *
 * 处理流程：
 * 1. 清除玩家的战利品 GUID 标记
 * 2. 发送战利品释放确认消息给客户端
 * 3. 移除玩家的拾取状态标志
 * 4. 根据容器类型执行特定的清理操作：
 *    - 游戏对象：处理门、钓鱼洞等特殊类型的逻辑
 *    - 尸体：清除战利品标记
 *    - 物品：处理选矿、分解等消耗性物品
 *    - 生物：处理尸体腐烂、皮肤等后续逻辑
 * 5. 从战利品观察者列表中移除玩家
 *
 * 特殊处理：
 * - 钓鱼洞有使用次数限制
 * - 选矿/分解物品每次消耗 5 个
 * - 轮流拾取模式下释放后需要重置当前拾取者
 *
 * 性能注意事项：
 * - 对于生物尸体，会强制更新动态标志
 * - 移除观察者可以减少不必要的战利品更新
 *
 * @see Player::SendLootRelease()
 * @see Loot::RemoveLooter()
 */
void WorldSession::DoLootRelease(ObjectGuid lguid)
{
    Player  *player = GetPlayer();
    Loot    *loot;

    // 清除玩家的战利品 GUID 和拾取标志
    player->SetLootGUID(ObjectGuid::Empty);
    player->SendLootRelease(lguid);

    player->RemoveUnitFlag(UNIT_FLAG_LOOTING);

    if (!player->IsInWorld())
        return;

    // 根据战利品容器类型执行不同的处理逻辑
    if (lguid.IsGameObject())
    {
        GameObject* go = GetPlayer()->GetMap()->GetGameObject(lguid);

        // 对于玩家拥有的游戏对象（如钓鱼浮标）或钓鱼洞，不检查距离
        // not check distance for GO in case owned GO (fishing bobber case, for example) or Fishing hole GO
        if (!go || ((go->GetOwnerGUID() != _player->GetGUID() && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE) && !go->IsWithinDistInMap(_player)))
            return;

        loot = &go->loot;

        // 处理门类型的游戏对象
        if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR)
        {
            // 锁定的门通过法术效果 OpenLock 打开，防止将其标记为已拾取
            // locked doors are opened with spelleffect openlock, prevent remove its as looted
            go->UseDoorOrButton();
        }
        else if (loot->isLooted() || go->GetGoType() == GAMEOBJECT_TYPE_FISHINGNODE)
        {
            // 钓鱼洞有使用次数限制
            if (go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
            {                                               // The fishing hole used once more
                go->AddUse();                               // 如果达到最大使用次数，将在下一个 tick 中消失
                                                            // if the max usage is reached, will be despawned in next tick
                if (go->GetUseCount() >= go->GetGOValue()->FishingHole.MaxOpens)
                    go->SetLootState(GO_JUST_DEACTIVATED);
                else
                    go->SetLootState(GO_READY);
            }
            else
                go->SetLootState(GO_JUST_DEACTIVATED);

            loot->clear();
        }
        else
        {
            // 未完全拾取的对象保持激活状态
            // not fully looted object
            go->SetLootState(GO_ACTIVATED, player);

            // 如果轮流拾取玩家释放，重置当前拾取者
            // if the round robin player release, reset it.
            if (player->GetGUID() == loot->roundRobinPlayer)
                loot->roundRobinPlayer.Clear();
        }
    }
    else if (lguid.IsCorpse())        // 只在战场移除徽章 / ONLY remove insignia at BG
    {
        Corpse* corpse = ObjectAccessor::GetCorpse(*player, lguid);
        if (!corpse || !corpse->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
            return;

        loot = &corpse->loot;

        if (loot->isLooted())
        {
            loot->clear();
            corpse->RemoveFlag(CORPSE_FIELD_DYNAMIC_FLAGS, CORPSE_DYNFLAG_LOOTABLE);
        }
    }
    else if (lguid.IsItem())
    {
        Item* pItem = player->GetItemByGuid(lguid);
        if (!pItem)
            return;

        ItemTemplate const* proto = pItem->GetTemplate();

        // 对于选矿和研磨，每次只销毁堆叠中的 5 个物品
        // destroy only 5 items from stack in case prospecting and milling
        if (proto->HasFlag(ITEM_FLAG_IS_PROSPECTABLE) || proto->HasFlag(ITEM_FLAG_IS_MILLABLE))
        {
            pItem->m_lootGenerated = false;
            pItem->loot.clear();

            uint32 count = pItem->GetCount();

            // 法术代码中检查 >=5，但也适用于作弊情况下的其他堆叠移除
            // >=5 checked in spell code, but will work for cheating cases also with removing from another stacks.
            if (count > 5)
                count = 5;

            player->DestroyItemCount(pItem, count, true);
        }
        else
        {
            // 只有在没有战利品或金币时（未拾取的战利品会保存到数据库）
            // 或者如果它不是可打开的物品时，才删除物品
            // Only delete item if no loot or money (unlooted loot is saved to db) or if it isn't an openable item
            if (pItem->loot.isLooted() || !proto->HasFlag(ITEM_FLAG_HAS_LOOT))
                player->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
        }
        return;                                             // 物品只能单人拾取 / item can be looted only single player
    }
    else
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lguid);

        // 盗贼偷窃时生物可能存活
        bool lootAllowed = creature && creature->IsAlive() == (player->GetClass() == CLASS_ROGUE && creature->loot.loot_type == LOOT_PICKPOCKETING);
        if (!lootAllowed || !creature->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
            return;

        loot = &creature->loot;
        if (loot->isLooted())
        {
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

            // 跳过偷窃战利品以提高速度，剥皮计时器减少实际上是无效操作
            // skip pickpocketing loot for speed, skinning timer reduction is no-op in fact
            if (!creature->IsAlive())
                creature->AllLootRemovedFromCorpse();

            loot->clear();
        }
        else
        {
            // 如果轮流拾取玩家释放，重置当前拾取者
            // if the round robin player release, reset it.
            if (player->GetGUID() == loot->roundRobinPlayer)
            {
                loot->roundRobinPlayer.Clear();

                if (Group* group = player->GetGroup())
                    group->SendLooter(creature, nullptr);
            }
            // 强制更新动态标志以更新拾取者和可拾取信息
            // force dynflag update to update looter and lootable info
            creature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
        }
    }

    // 玩家不再查看战利品列表，不需要接收战利品列表更新
    //Player is not looking at loot list, he doesn't need to see updates on the loot list
    loot->RemoveLooter(player->GetGUID());
}

/**
 * @brief 处理队长分配物品给指定玩家的请求
 *
 * 在团队分配模式下，队长可以将战利品物品分配给指定的团队成员。
 * 该函数会验证队长权限和目标玩家的资格，然后执行物品转移。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - lootguid: 战利品容器的 GUID
 *                 - slotid: 战利品槽位索引
 *                 - target_playerguid: 目标玩家的 GUID
 *
 * 调用时机：
 * - 队长在战利品窗口中右键点击物品并选择"分配给..."时
 * - 客户端发送 CMSG_LOOT_MASTER_GIVE 消息
 *
 * 处理流程：
 * 1. 验证玩家是否为团队队长且分配模式为队长分配
 * 2. 验证目标玩家是否在同一团队和地图中
 * 3. 获取战利品容器和物品信息
 * 4. 检查目标玩家是否有足够的背包空间
 * 5. 检查目标玩家是否满足物品使用条件
 * 6. 将物品转移到目标玩家的背包
 * 7. 更新成就和战利品状态
 *
 * 安全检查：
 * - 队长必须具有分配权限
 * - 目标玩家必须在同一团队
 * - 槽位索引必须在有效范围内
 * - 目标玩家背包必须有空间
 *
 * 性能注意事项：
 * - 需要获取物品的允许拾取者列表用于交易
 * - 物品转移后需要通知所有客户端
 *
 * @see Player::StoreNewItem()
 * @see LootItem::AllowedForPlayer()
 */
void WorldSession::HandleLootMasterGiveOpcode(WorldPacket& recvData)
{
    uint8 slotid;
    ObjectGuid lootguid, target_playerguid;

    recvData >> lootguid >> slotid >> target_playerguid;

    if (!_player->GetGroup() || _player->GetGroup()->GetMasterLooterGuid() != _player->GetGUID() || _player->GetGroup()->GetLootMethod() != MASTER_LOOT)
    {
        _player->SendLootError(lootguid, LOOT_ERROR_DIDNT_KILL);
        return;
    }

    // player on other map
    Player* target = ObjectAccessor::GetPlayer(*_player, target_playerguid);
    if (!target)
    {
        _player->SendLootError(lootguid, LOOT_ERROR_PLAYER_NOT_FOUND);
        return;
    }

    TC_LOG_DEBUG("network", "WorldSession::HandleLootMasterGiveOpcode (CMSG_LOOT_MASTER_GIVE, 0x02A3) Target = [{}].", target->GetName());

    if (_player->GetLootGUID() != lootguid)
    {
        _player->SendLootError(lootguid, LOOT_ERROR_DIDNT_KILL);
        return;
    }

    if (!_player->IsInRaidWith(target) || !_player->IsInMap(target))
    {
        _player->SendLootError(lootguid, LOOT_ERROR_MASTER_OTHER);
        TC_LOG_INFO("entities.player.cheat", "MasterLootItem: Player {} tried to give an item to ineligible player {} !", GetPlayer()->GetName(), target->GetName());
        return;
    }

    Loot* loot = nullptr;

    if (GetPlayer()->GetLootGUID().IsCreatureOrVehicle())
    {
        Creature* creature = GetPlayer()->GetMap()->GetCreature(lootguid);
        if (!creature)
            return;

        loot = &creature->loot;
    }
    else if (GetPlayer()->GetLootGUID().IsGameObject())
    {
        GameObject* pGO = GetPlayer()->GetMap()->GetGameObject(lootguid);
        if (!pGO)
            return;

        loot = &pGO->loot;
    }

    if (!loot)
        return;

    if (slotid >= loot->items.size() + loot->quest_items.size())
    {
        TC_LOG_DEBUG("loot", "MasterLootItem: Player {} might be using a hack! (slot {}, size {})",
            GetPlayer()->GetName(), slotid, (unsigned long)loot->items.size());
        return;
    }

    LootItem& item = slotid >= loot->items.size() ? loot->quest_items[slotid - loot->items.size()] : loot->items[slotid];

    ItemPosCountVec dest;
    InventoryResult msg = target->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item.itemid, item.count);
    if (!item.AllowedForPlayer(target, true))
        msg = EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM;
    if (msg != EQUIP_ERR_OK)
    {
        if (msg == EQUIP_ERR_CANT_CARRY_MORE_OF_THIS)
            _player->SendLootError(lootguid, LOOT_ERROR_MASTER_UNIQUE_ITEM);
        else if (msg == EQUIP_ERR_INVENTORY_FULL)
            _player->SendLootError(lootguid, LOOT_ERROR_MASTER_INV_FULL);
        else
            _player->SendLootError(lootguid, LOOT_ERROR_MASTER_OTHER);
        return;
    }

    // list of players allowed to receive this item in trade
    GuidSet looters = item.GetAllowedLooters();

    // now move item from loot to target inventory
    Item* newitem = target->StoreNewItem(dest, item.itemid, true, item.randomPropertyId, looters);
    target->SendNewItem(newitem, uint32(item.count), false, false, true);
    target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM, item.itemid, item.count);
    target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE, loot->loot_type, item.count);
    target->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM, item.itemid, item.count);

    // mark as looted
    item.count = 0;
    item.is_looted = true;

    loot->NotifyItemRemoved(slotid);
    --loot->unlootedCount;
}
