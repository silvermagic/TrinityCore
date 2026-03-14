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
 * @file Item.cpp
 * @brief 物品类实现模块
 *
 * 本文件实现了游戏中所有物品的核心功能，是物品系统的主要实现文件。
 *
 * 主要功能模块：
 *   1. 物品生命周期管理
 *      - 创建物品实例（CreateItem, Create）
 *      - 从数据库加载物品（LoadFromDB）
 *      - 保存物品到数据库（SaveToDB）
 *      - 删除物品（DeleteFromDB）
 *
 *   2. 套装效果系统
 *      - 添加套装效果（AddItemsSetItem）
 *      - 移除套装效果（RemoveItemsSetItem）
 *      - 套装物品计数和奖励激活
 *
 *   3. 附魔系统
 *      - 设置/清除附魔（SetEnchantment, ClearEnchantment）
 *      - 随机属性设置（SetItemRandomProperties）
 *      - 宝石镶嵌检查（GemsFitSockets）
 *
 *   4. 耐久度系统
 *      - 计算修理费用（CalculateDurabilityRepairCost）
 *      - 耐久度损耗和修复
 *
 *   5. 退还系统
 *      - 物品退还时间检查
 *      - 退还数据处理
 *
 *   6. 灵魂绑定交易系统
 *      - 拾取绑定物品限时交易
 *      - 交易过期检查
 *
 *   7. 物品更新队列
 *      - 添加/移除更新队列
 *      - 状态管理和数据库同步
 *
 * 数据库交互：
 *   - character_inventory 表：物品位置信息
 *   - item_instance 表：物品实例数据
 *   - item_refund_instance 表：退还数据
 *   - item_soulbound_trade_data 表：灵魂绑定交易数据
 *
 * 性能注意事项：
 *   - 物品更新队列避免频繁数据库操作
 *   - 使用事务批量处理数据库更新
 *   - 套装效果缓存在玩家对象中
 *
 * 设计模式：
 *   - 对象池模式：物品对象由 ObjectMgr 管理
 *   - 观察者模式：物品状态变化通知更新队列
 *   - 状态模式：ItemUpdateState 管理数据库同步策略
 */

#include "Item.h"
#include "Bag.h"
#include "Common.h"
#include "ConditionMgr.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "ItemEnchantmentMgr.h"
#include "Log.h"
#include "LootItemStorage.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "Player.h"
#include "TradeData.h"
#include "UpdateData.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief 为玩家添加套装物品效果
 *
 * 职责：
 *   当玩家装备套装物品时，检查并应用套装效果。
 *   根据玩家当前拥有的套装物品数量，激活对应的套装奖励法术。
 *
 * 参数：
 *   player - 装备套装物品的玩家指针
 *   item   - 被装备的物品指针
 *
 * 主要流程：
 *   1. 获取物品的套装ID
 *   2. 检查玩家是否满足套装的技能要求
 *   3. 查找或创建玩家的套装效果记录
 *   4. 增加套装物品计数
 *   5. 遍历套装奖励，激活达到门槛要求的法术效果
 */
void AddItemsSetItem(Player* player, Item* item)
{
    ItemTemplate const* proto = item->GetTemplate();
    uint32 setid = proto->ItemSet;

    ItemSetEntry const* set = sItemSetStore.LookupEntry(setid);

    if (!set)
    {
        TC_LOG_ERROR("sql.sql", "Item set {} for item (id {}) not found, mods not applied.", setid, proto->ItemId);
        return;
    }

    if (set->RequiredSkill && player->GetSkillValue(set->RequiredSkill) < set->RequiredSkillRank)
        return;

    ItemSetEffect* eff = nullptr;

    for (size_t x = 0; x < player->ItemSetEff.size(); ++x)
    {
        if (player->ItemSetEff[x] && player->ItemSetEff[x]->setid == setid)
        {
            eff = player->ItemSetEff[x];
            break;
        }
    }

    if (!eff)
    {
        eff = new ItemSetEffect();
        eff->setid = setid;

        size_t x = 0;
        for (; x < player->ItemSetEff.size(); ++x)
            if (!player->ItemSetEff[x])
                break;

        if (x < player->ItemSetEff.size())
            player->ItemSetEff[x]=eff;
        else
            player->ItemSetEff.push_back(eff);
    }

    ++eff->item_count;

    for (uint32 x = 0; x < MAX_ITEM_SET_SPELLS; ++x)
    {
        if (!set->SetSpellID[x])
            continue;
        //not enough for  spell
        if (set->SetThreshold[x] > eff->item_count)
            continue;

        uint32 z = 0;
        for (; z < MAX_ITEM_SET_SPELLS; ++z)
            if (eff->spells[z] && eff->spells[z]->Id == set->SetSpellID[x])
                break;

        if (z < MAX_ITEM_SET_SPELLS)
            continue;

        //new spell
        for (uint32 y = 0; y < MAX_ITEM_SET_SPELLS; ++y)
        {
            if (!eff->spells[y])                             // free slot
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(set->SetSpellID[x]);
                if (!spellInfo)
                {
                    TC_LOG_ERROR("entities.player.items", "WORLD: unknown spell id {} in items set {} effects", set->SetSpellID[x], setid);
                    break;
                }

                // spell cast only if fit form requirement, in other case will cast at form change
                player->ApplyEquipSpell(spellInfo, nullptr, true);
                eff->spells[y] = spellInfo;
                break;
            }
        }
    }
}

/**
 * @brief 从玩家移除套装物品效果
 *
 * 职责：
 *   当玩家卸下套装物品时，检查并移除相应的套装效果。
 *   根据剩余的套装物品数量，可能需要移除某些套装奖励法术。
 *
 * 参数：
 *   player - 卸下套装物品的玩家指针
 *   proto  - 被卸下物品的模板指针
 *
 * 主要流程：
 *   1. 获取物品的套装ID
 *   2. 查找玩家对应的套装效果记录
 *   3. 减少套装物品计数
 *   4. 遍历套装奖励，移除不再满足门槛要求的法术效果
 *   5. 如果套装物品数量降为0，清理套装效果记录
 */
void RemoveItemsSetItem(Player*player, ItemTemplate const* proto)
{
    uint32 setid = proto->ItemSet;

    ItemSetEntry const* set = sItemSetStore.LookupEntry(setid);

    if (!set)
    {
        TC_LOG_ERROR("sql.sql", "Item set #{} for item #{} not found, mods not removed.", setid, proto->ItemId);
        return;
    }

    ItemSetEffect* eff = nullptr;
    size_t setindex = 0;
    for (; setindex < player->ItemSetEff.size(); setindex++)
    {
        if (player->ItemSetEff[setindex] && player->ItemSetEff[setindex]->setid == setid)
        {
            eff = player->ItemSetEff[setindex];
            break;
        }
    }

    // can be in case now enough skill requirement for set appling but set has been appliend when skill requirement not enough
    if (!eff)
        return;

    --eff->item_count;

    for (uint32 x = 0; x < MAX_ITEM_SET_SPELLS; x++)
    {
        if (!set->SetSpellID[x])
            continue;

        // enough for spell
        if (set->SetThreshold[x] <= eff->item_count)
            continue;

        for (uint32 z = 0; z < MAX_ITEM_SET_SPELLS; z++)
        {
            if (eff->spells[z] && eff->spells[z]->Id == set->SetSpellID[x])
            {
                // spell can be not active if not fit form requirement
                player->ApplyEquipSpell(eff->spells[z], nullptr, false);
                eff->spells[z]=nullptr;
                break;
            }
        }
    }

    if (!eff->item_count)                                    //all items of a set were removed
    {
        ASSERT(eff == player->ItemSetEff[setindex]);
        delete eff;
        player->ItemSetEff[setindex] = nullptr;
    }
}

/**
 * @brief 检查物品是否可以放入指定的背包
 *
 * 职责：
 *   根据物品类型和背包类型判断物品是否可以放入该背包。
 *   主要用于专业背包（如灵魂碎片袋、草药袋、弹药袋等）的物品过滤。
 *
 * 参数：
 *   pProto    - 待放入物品的模板指针
 *   pBagProto - 背包的模板指针
 *
 * 返回值：
 *   true  - 物品可以放入该背包
 *   false - 物品不能放入该背包
 *
 * 主要流程：
 *   1. 检查物品和背包模板是否有效
 *   2. 根据背包类别（容器/箭袋）进行不同处理
 *   3. 对于专业背包，检查物品的背包族系是否匹配
 */
bool ItemCanGoIntoBag(ItemTemplate const* pProto, ItemTemplate const* pBagProto)
{
    if (!pProto || !pBagProto)
        return false;

    switch (pBagProto->Class)
    {
        case ITEM_CLASS_CONTAINER:
            switch (pBagProto->SubClass)
            {
                case ITEM_SUBCLASS_CONTAINER:
                    return true;
                case ITEM_SUBCLASS_SOUL_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_SOUL_SHARDS))
                        return false;
                    return true;
                case ITEM_SUBCLASS_HERB_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_HERBS))
                        return false;
                    return true;
                case ITEM_SUBCLASS_ENCHANTING_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_ENCHANTING_SUPP))
                        return false;
                    return true;
                case ITEM_SUBCLASS_MINING_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_MINING_SUPP))
                        return false;
                    return true;
                case ITEM_SUBCLASS_ENGINEERING_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_ENGINEERING_SUPP))
                        return false;
                    return true;
                case ITEM_SUBCLASS_GEM_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_GEMS))
                        return false;
                    return true;
                case ITEM_SUBCLASS_LEATHERWORKING_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_LEATHERWORKING_SUPP))
                        return false;
                    return true;
                case ITEM_SUBCLASS_INSCRIPTION_CONTAINER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_INSCRIPTION_SUPP))
                        return false;
                    return true;
                default:
                    return false;
            }
        case ITEM_CLASS_QUIVER:
            switch (pBagProto->SubClass)
            {
                case ITEM_SUBCLASS_QUIVER:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_ARROWS))
                        return false;
                    return true;
                case ITEM_SUBCLASS_AMMO_POUCH:
                    if (!(pProto->BagFamily & BAG_FAMILY_MASK_BULLETS))
                        return false;
                    return true;
                default:
                    return false;
            }
    }
    return false;
}

/**
 * @brief Item类构造函数
 *
 * 职责：
 *   初始化物品对象的基本属性和状态。
 *   设置对象类型、更新标志、初始状态等。
 *
 * 主要流程：
 *   1. 设置对象类型为物品
 *   2. 设置更新标志
 *   3. 初始化各种成员变量为默认值
 */
Item::Item()
{
    m_objectType |= TYPEMASK_ITEM;
    m_objectTypeId = TYPEID_ITEM;

    m_updateFlag = UPDATEFLAG_LOWGUID;

    m_valuesCount = ITEM_END;
    m_slot = 0;
    uState = ITEM_NEW;
    uQueuePos = -1;
    m_container = nullptr;
    m_lootGenerated = false;
    mb_in_trade = false;
    m_lastPlayedTimeUpdate = GameTime::GetGameTime();

    m_refundRecipient = 0;
    m_paidMoney = 0;
    m_paidExtendedCost = 0;
}

/**
 * @brief 创建物品实例
 *
 * 职责：
 *   初始化物品对象的核心数据，包括GUID、物品ID、所有者等基本信息。
 *   设置物品的初始属性值如堆叠数量、耐久度、法术充能等。
 *
 * 参数：
 *   guidlow - 物品的低32位GUID
 *   itemId  - 物品模板ID
 *   owner   - 物品所有者玩家指针（可为空）
 *
 * 返回值：
 *   true  - 创建成功
 *   false - 创建失败（物品模板不存在）
 *
 * 主要流程：
 *   1. 调用基类_Create初始化对象基础信息
 *   2. 设置物品入口ID和缩放
 *   3. 设置所有者GUID（如果有）
 *   4. 获取物品模板，设置初始属性：
 *      - 堆叠数量为1
 *      - 最大耐久度和当前耐久度
 *      - 法术充能次数
 *      - 持续时间
 */
bool Item::Create(ObjectGuid::LowType guidlow, uint32 itemId, Player const* owner)
{
    Object::_Create(guidlow, 0, HighGuid::Item);

    SetEntry(itemId);
    SetObjectScale(1.0f);

    if (owner)
    {
        SetGuidValue(ITEM_FIELD_OWNER, owner->GetGUID());
        SetGuidValue(ITEM_FIELD_CONTAINED, owner->GetGUID());
    }

    ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(itemId);
    if (!itemProto)
        return false;

    SetUInt32Value(ITEM_FIELD_STACK_COUNT, 1);
    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, itemProto->MaxDurability);
    SetUInt32Value(ITEM_FIELD_DURABILITY, itemProto->MaxDurability);

    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        SetSpellCharges(i, itemProto->Spells[i].SpellCharges);

    SetUInt32Value(ITEM_FIELD_DURATION, itemProto->Duration);
    SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, 0);
    return true;
}

/**
 * @brief 检查物品是否为非空背包
 *
 * 职责：
 *   判断该物品是否为一个非空的背包容器。
 *
 * 返回值：
 *   true  - 物品是背包且不为空
 *   false - 物品不是背包，或者是空的背包
 */
// Returns true if Item is a bag AND it is not empty.
// Returns false if Item is not a bag OR it is an empty bag.
bool Item::IsNotEmptyBag() const
{
    if (Bag const* bag = ToBag())
        return !bag->IsEmpty();
    return false;
}

/**
 * @brief 更新物品的持续时间
 *
 * 职责：
 *   更新有持续时间的物品的剩余时间。
 *   当时间耗尽时，触发物品过期事件并销毁物品。
 *
 * 参数：
 *   owner - 物品所有者指针
 *   diff  - 经过的毫秒数
 *
 * 主要流程：
 *   1. 检查物品是否有持续时间
 *   2. 如果剩余时间小于等于经过时间，触发物品过期并销毁
 *   3. 否则更新剩余时间并标记物品已改变
 */
void Item::UpdateDuration(Player* owner, uint32 diff)
{
    if (!GetUInt32Value(ITEM_FIELD_DURATION))
        return;

    TC_LOG_DEBUG("entities.player.items", "Item::UpdateDuration Item (Entry: {} Duration {} Diff {})", GetEntry(), GetUInt32Value(ITEM_FIELD_DURATION), diff);

    if (GetUInt32Value(ITEM_FIELD_DURATION) <= diff)
    {
        sScriptMgr->OnItemExpire(owner, GetTemplate());
        owner->DestroyItem(GetBagSlot(), GetSlot(), true);
        return;
    }

    SetUInt32Value(ITEM_FIELD_DURATION, GetUInt32Value(ITEM_FIELD_DURATION) - diff);
    SetState(ITEM_CHANGED, owner);                          // save new time in database
}

/**
 * @brief 将物品保存到数据库
 *
 * 职责：
 *   根据物品的当前状态（新建/修改/删除/未改变）执行相应的数据库操作。
 *   保存物品的所有核心属性到item_instance表。
 *
 * 参数：
 *   trans - 数据库事务对象（可为空，会自动创建事务）
 *
 * 主要流程：
 *   1. 检查是否在事务中，如不在则创建新事务
 *   2. 根据物品状态执行不同操作：
 *      - ITEM_NEW: 使用INSERT语句插入新记录
 *      - ITEM_CHANGED: 使用UPDATE语句更新记录
 *      - ITEM_REMOVED: 删除记录并删除物品对象
 *      - ITEM_UNCHANGED: 不执行任何操作
 *   3. 保存的数据包括：
 *      - 物品ID、所有者、创建者、赠送者
 *      - 数量、持续时间、法术充能
 *      - 标志位、附魔信息、随机属性
 *      - 耐久度、游玩时间、文本内容
 *   4. 如果物品被包装（礼品），更新gift表
 *   5. 对于删除的物品，清理相关的战利品存储
 */
void Item::SaveToDB(CharacterDatabaseTransaction trans)
{
    bool isInTransaction = bool(trans);
    if (!isInTransaction)
        trans = CharacterDatabase.BeginTransaction();

    ObjectGuid::LowType guid = GetGUID().GetCounter();
    switch (uState)
    {
        case ITEM_NEW:
        case ITEM_CHANGED:
        {
            uint8 index = 0;
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(uState == ITEM_NEW ? CHAR_REP_ITEM_INSTANCE : CHAR_UPD_ITEM_INSTANCE);
            stmt->setUInt32(  index, GetEntry());
            stmt->setUInt32(++index, GetOwnerGUID().GetCounter());
            stmt->setUInt32(++index, GetGuidValue(ITEM_FIELD_CREATOR).GetCounter());
            stmt->setUInt32(++index, GetGuidValue(ITEM_FIELD_GIFTCREATOR).GetCounter());
            stmt->setUInt32(++index, GetCount());
            stmt->setUInt32(++index, GetUInt32Value(ITEM_FIELD_DURATION));

            std::ostringstream ssSpells;
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                ssSpells << GetSpellCharges(i) << ' ';
            stmt->setString(++index, ssSpells.str());

            stmt->setUInt32(++index, GetUInt32Value(ITEM_FIELD_FLAGS));

            std::ostringstream ssEnchants;
            for (uint8 i = 0; i < MAX_ENCHANTMENT_SLOT; ++i)
            {
                ssEnchants << GetEnchantmentId(EnchantmentSlot(i)) << ' ';
                ssEnchants << GetEnchantmentDuration(EnchantmentSlot(i)) << ' ';
                ssEnchants << GetEnchantmentCharges(EnchantmentSlot(i)) << ' ';
            }
            stmt->setString(++index, ssEnchants.str());

            stmt->setInt16 (++index, GetItemRandomPropertyId());
            stmt->setUInt16(++index, GetUInt32Value(ITEM_FIELD_DURABILITY));
            stmt->setUInt32(++index, GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME));
            stmt->setString(++index, m_text);
            stmt->setUInt32(++index, guid);

            trans->Append(stmt);

            if ((uState == ITEM_CHANGED) && IsWrapped())
            {
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GIFT_OWNER);
                stmt->setUInt32(0, GetOwnerGUID().GetCounter());
                stmt->setUInt32(1, guid);
                trans->Append(stmt);
            }
            break;
        }
        case ITEM_REMOVED:
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
            stmt->setUInt32(0, guid);
            trans->Append(stmt);

            if (IsWrapped())
            {
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GIFT);
                stmt->setUInt32(0, guid);
                trans->Append(stmt);
            }

            if (!isInTransaction)
                CharacterDatabase.CommitTransaction(trans);

            // Delete the items if this is a container
            if (!loot.isLooted())
                sLootItemStorage->RemoveStoredLootForContainer(GetGUID().GetCounter());

            delete this;
            return;
        }
        case ITEM_UNCHANGED:
            break;
    }

    SetState(ITEM_UNCHANGED);

    if (!isInTransaction)
        CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 从数据库加载物品数据
 *
 * 职责：
 *   从数据库查询结果中加载物品的所有属性数据。
 *   执行数据验证和修复，确保物品数据的正确性。
 *
 * 参数：
 *   guid       - 物品的低32位GUID
 *   owner_guid - 物品所有者的GUID（可为空，如银行/拍卖行/邮件中的物品）
 *   fields     - 数据库查询结果字段数组
 *   entry      - 物品模板ID
 *
 * 返回值：
 *   true  - 加载成功
 *   false - 加载失败（物品模板无效）
 *
 * 主要流程：
 *   1. 创建物品对象并设置GUID和入口ID
 *   2. 验证物品模板是否存在
 *   3. 加载数据库字段数据：
 *      - 创建者GUID、赠送者GUID
 *      - 物品数量、持续时间
 *      - 法术充能、标志位
 *      - 附魔信息、随机属性
 *      - 耐久度、游玩时间、文本
 *   4. 执行数据修复：
 *      - 修复不匹配的持续时间
 *      - 移除NO_BIND物品的灵魂绑定标志
 *      - 修复超出最大耐久度的情况
 *   5. 如果有修复，立即更新数据库
 */
bool Item::LoadFromDB(ObjectGuid::LowType guid, ObjectGuid owner_guid, Field* fields, uint32 entry)
{
    //                                                    0                1      2         3        4      5             6                 7           8           9    10
    //result = CharacterDatabase.PQuery("SELECT creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text FROM item_instance WHERE guid = '{}'", guid);

    // create item before any checks for store correct guid
    // and allow use "FSetState(ITEM_REMOVED); SaveToDB();" for deleting item from DB
    Object::_Create(guid, 0, HighGuid::Item);

    // Set entry, MUST be before proto check
    SetEntry(entry);
    SetObjectScale(1.0f);

    ItemTemplate const* proto = GetTemplate();
    if (!proto)
    {
        TC_LOG_ERROR("entities.item", "Invalid entry {} for item {}. Refusing to load.", GetEntry(), GetGUID().ToString());
        return false;
    }

    // set owner (not if item is only loaded for gbank/auction/mail
    if (owner_guid)
        SetOwnerGUID(owner_guid);

    bool need_save = false;                                 // need explicit save data at load fixes
    SetGuidValue(ITEM_FIELD_CREATOR, ObjectGuid(HighGuid::Player, fields[0].GetUInt32()));
    SetGuidValue(ITEM_FIELD_GIFTCREATOR, ObjectGuid(HighGuid::Player, fields[1].GetUInt32()));
    SetCount(fields[2].GetUInt32());

    uint32 duration = fields[3].GetUInt32();
    SetUInt32Value(ITEM_FIELD_DURATION, duration);
    // update duration if need, and remove if not need
    if ((proto->Duration == 0) != (duration == 0))
    {
        SetUInt32Value(ITEM_FIELD_DURATION, proto->Duration);
        need_save = true;
    }

    std::vector<std::string_view> tokens = Trinity::Tokenize(fields[4].GetStringView(), ' ', false);
    if (tokens.size() == MAX_ITEM_PROTO_SPELLS)
    {
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (Optional<int32> charges = Trinity::StringTo<int32>(tokens[i]))
                SetSpellCharges(i, *charges);
            else
                TC_LOG_ERROR("entities.item", "Invalid charge info '{}' for item {}, charge data not loaded.", std::string(tokens[i]), GetGUID().ToString());
        }
    }

    SetUInt32Value(ITEM_FIELD_FLAGS, fields[5].GetUInt32());
    // Remove bind flag for items vs NO_BIND set
    if (IsSoulBound() && proto->Bonding == NO_BIND)
    {
        ApplyModFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_SOULBOUND, false);
        need_save = true;
    }

    if (!_LoadIntoDataField(fields[6].GetString(), ITEM_FIELD_ENCHANTMENT_1_1, MAX_ENCHANTMENT_SLOT * MAX_ENCHANTMENT_OFFSET))
        TC_LOG_WARN("entities.item", "Invalid enchantment data '{}' for item {}. Forcing partial load.", fields[6].GetString(), GetGUID().ToString());

    SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, fields[7].GetInt16());
    // recalculate suffix factor
    if (GetItemRandomPropertyId() < 0)
        UpdateItemSuffixFactor();

    uint32 durability = fields[8].GetUInt16();
    SetUInt32Value(ITEM_FIELD_DURABILITY, durability);
    // update max durability (and durability) if need
    SetUInt32Value(ITEM_FIELD_MAXDURABILITY, proto->MaxDurability);

    // do not overwrite durability for wrapped items
    if (durability > proto->MaxDurability && !IsWrapped())
    {
        SetUInt32Value(ITEM_FIELD_DURABILITY, proto->MaxDurability);
        need_save = true;
    }

    SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, fields[9].GetUInt32());
    SetText(fields[10].GetString());

    if (need_save)                                           // normal item changed state set not work at loading
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_INSTANCE_ON_LOAD);
        stmt->setUInt32(0, GetUInt32Value(ITEM_FIELD_DURATION));
        stmt->setUInt32(1, GetUInt32Value(ITEM_FIELD_FLAGS));
        stmt->setUInt32(2, GetUInt32Value(ITEM_FIELD_DURABILITY));
        stmt->setUInt32(3, guid);
        CharacterDatabase.Execute(stmt);
    }

    return true;
}

/**
 * @brief 从数据库删除物品记录（静态方法）
 *
 * 职责：
 *   根据物品GUID从数据库中删除物品实例记录。
 *   这是一个静态方法，不需要物品对象实例。
 *
 * 参数：
 *   trans    - 数据库事务对象
 *   itemGuid - 要删除的物品GUID
 */
/*static*/
void Item::DeleteFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
    stmt->setUInt32(0, itemGuid);
    trans->Append(stmt);
}

/**
 * @brief 从数据库删除物品记录（成员方法）
 *
 * 职责：
 *   从数据库中删除当前物品实例记录。
 *   如果物品是容器且有未拾取的战利品，也会一并清理。
 *
 * 参数：
 *   trans - 数据库事务对象
 */
void Item::DeleteFromDB(CharacterDatabaseTransaction trans)
{
    DeleteFromDB(trans, GetGUID().GetCounter());

    // Delete the items if this is a container
    if (!loot.isLooted())
        sLootItemStorage->RemoveStoredLootForContainer(GetGUID().GetCounter());
}

/**
 * @brief 从背包数据库表中删除物品记录（静态方法）
 *
 * 职责：
 *   从 character_inventory 表中删除指定物品的位置记录。
 *   这是静态方法，不需要物品对象实例。
 *
 * @param trans 数据库事务对象
 * @param itemGuid 要删除的物品GUID
 */
/*static*/
void Item::DeleteFromInventoryDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_INVENTORY_BY_ITEM);
    stmt->setUInt32(0, itemGuid);
    trans->Append(stmt);
}

/**
 * @brief 从背包数据库表中删除物品记录
 *
 * 职责：
 *   从 character_inventory 表中删除当前物品的位置记录。
 *
 * @param trans 数据库事务对象
 */
void Item::DeleteFromInventoryDB(CharacterDatabaseTransaction trans)
{
    DeleteFromInventoryDB(trans, GetGUID().GetCounter());
}

/**
 * @brief 获取物品模板
 *
 * 职责：
 *   返回物品对应的模板数据，包含物品的静态属性定义。
 *
 * @return 物品模板指针
 */
ItemTemplate const* Item::GetTemplate() const
{
    return sObjectMgr->GetItemTemplate(GetEntry());
}

/**
 * @brief 获取物品所有者玩家对象
 *
 * 职责：
 *   通过物品的所有者GUID查找并返回对应的玩家对象。
 *
 * 返回值：
 *   玩家指针，如果玩家不在线则返回nullptr
 */
Player* Item::GetOwner()const
{
    return ObjectAccessor::FindPlayer(GetOwnerGUID());
}

/**
 * @brief 获取物品所需技能ID
 *
 * 职责：
 *   获取使用该物品所需的技能ID。
 *   这是物品模板GetSkill()方法的便捷封装。
 *
 * 返回值：
 *   技能ID，如果不需要技能则返回0
 */
// Just a "legacy shortcut" for proto->GetSkill()
uint32 Item::GetSkill()
{
    ItemTemplate const* proto = GetTemplate();
    return proto->GetSkill();
}

/**
 * @brief 获取物品对应的技能ID
 *
 * 职责：
 *   根据物品的类别和子类别返回对应的武器或护甲技能ID。
 *   用于判断玩家是否具备使用该武器或护甲的技能。
 *
 * 返回值：
 *   技能ID，如果物品不是武器或护甲则返回0
 *
 * 主要流程：
 *   根据物品类别（武器/护甲）和子类别映射到对应的技能ID
 */
uint32 Item::GetSpell()
{
    ItemTemplate const* proto = GetTemplate();

    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_AXE:     return  196;
                case ITEM_SUBCLASS_WEAPON_AXE2:    return  197;
                case ITEM_SUBCLASS_WEAPON_BOW:     return  264;
                case ITEM_SUBCLASS_WEAPON_GUN:     return  266;
                case ITEM_SUBCLASS_WEAPON_MACE:    return  198;
                case ITEM_SUBCLASS_WEAPON_MACE2:   return  199;
                case ITEM_SUBCLASS_WEAPON_POLEARM: return  200;
                case ITEM_SUBCLASS_WEAPON_SWORD:   return  201;
                case ITEM_SUBCLASS_WEAPON_SWORD2:  return  202;
                case ITEM_SUBCLASS_WEAPON_STAFF:   return  227;
                case ITEM_SUBCLASS_WEAPON_DAGGER:  return 1180;
                case ITEM_SUBCLASS_WEAPON_THROWN:  return 2567;
                case ITEM_SUBCLASS_WEAPON_SPEAR:   return 3386;
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:return 5011;
                case ITEM_SUBCLASS_WEAPON_WAND:    return 5009;
                default: return 0;
            }
        case ITEM_CLASS_ARMOR:
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_ARMOR_CLOTH:    return 9078;
                case ITEM_SUBCLASS_ARMOR_LEATHER:  return 9077;
                case ITEM_SUBCLASS_ARMOR_MAIL:     return 8737;
                case ITEM_SUBCLASS_ARMOR_PLATE:    return  750;
                case ITEM_SUBCLASS_ARMOR_SHIELD:   return 9116;
                default: return 0;
            }
    }
    return 0;
}

/**
 * @brief 设置物品的随机属性
 *
 * 职责：
 *   为物品设置随机属性（随机前缀或随机后缀）。
 *   根据属性ID的正负区分随机属性和随机后缀。
 *
 * 参数：
 *   randomPropId - 随机属性ID（正数表示随机属性，负数表示随机后缀）
 *
 * 主要流程：
 *   1. 如果ID为0则直接返回
 *   2. 对于正数ID（随机属性）：
 *      - 设置随机属性ID
 *      - 应用对应的附魔效果到属性附魔槽位
 *   3. 对于负数ID（随机后缀）：
 *      - 设置随机后缀ID
 *      - 计算并设置后缀因子
 *      - 应用对应的附魔效果到后缀附魔槽位
 */
void Item::SetItemRandomProperties(int32 randomPropId)
{
    if (!randomPropId)
        return;

    if (randomPropId > 0)
    {
        ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(randomPropId);
        if (item_rand)
        {
            if (GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID) != int32(item_rand->ID))
            {
                SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, item_rand->ID);
                SetState(ITEM_CHANGED, GetOwner());
            }
            for (uint32 i = PROP_ENCHANTMENT_SLOT_2; i < PROP_ENCHANTMENT_SLOT_2 + 3; ++i)
                SetEnchantment(EnchantmentSlot(i), item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_2], 0, 0);
        }
    }
    else
    {
        ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(-randomPropId);
        if (item_rand)
        {
            if (GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID) != -int32(item_rand->ID) ||
                !GetItemSuffixFactor())
            {
                SetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID, -int32(item_rand->ID));
                UpdateItemSuffixFactor();
                SetState(ITEM_CHANGED, GetOwner());
            }

            for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
                SetEnchantment(EnchantmentSlot(i), item_rand->Enchantment[i - PROP_ENCHANTMENT_SLOT_0], 0, 0);
        }
    }
}

/**
 * @brief 更新物品的后缀因子
 *
 * 职责：
 *   根据物品等级、品质和槽位类型重新计算并设置随机后缀的属性因子。
 *   该因子决定了随机后缀提供的属性加成数值。
 *
 * 调用时机：
 *   - 加载带有随机后缀的物品时
 *   - 物品属性需要重新计算时
 */
void Item::UpdateItemSuffixFactor()
{
    uint32 suffixFactor = GenerateEnchSuffixFactor(GetEntry());
    if (GetItemSuffixFactor() == suffixFactor)
        return;
    SetUInt32Value(ITEM_FIELD_PROPERTY_SEED, suffixFactor);
}

/**
 * @brief 设置物品的更新状态
 *
 * 职责：
 *   设置物品在数据库同步中的状态，并管理更新队列。
 *   状态决定了物品在下次保存时如何与数据库同步。
 *
 * 参数：
 *   state     - 新的更新状态（ITEM_NEW/ITEM_CHANGED/ITEM_REMOVED/ITEM_UNCHANGED）
 *   forplayer - 物品所有者玩家指针
 *
 * 主要流程：
 *   1. 如果当前是NEW状态且要设置为REMOVED，直接删除物品对象
 *   2. 如果新状态不是UNCHANGED：
 *      - 新物品保持NEW状态直到保存
 *      - 将物品添加到更新队列
 *   3. 如果新状态是UNCHANGED：
 *      - 从更新队列中移除
 */
void Item::SetState(ItemUpdateState state, Player* forplayer)
{
    if (uState == ITEM_NEW && state == ITEM_REMOVED)
    {
        // pretend the item never existed
        if (forplayer)
        {
            RemoveItemFromUpdateQueueOf(this, forplayer);
            forplayer->DeleteRefundReference(GetGUID());
        }
        delete this;
        return;
    }
    if (state != ITEM_UNCHANGED)
    {
        // new items must stay in new state until saved
        if (uState != ITEM_NEW)
            uState = state;

        if (forplayer)
            AddItemToUpdateQueueOf(this, forplayer);
    }
    else
    {
        // unset in queue
        // the item must be removed from the queue manually
        uQueuePos = -1;
        uState = ITEM_UNCHANGED;
    }
}

/**
 * @brief 将物品添加到玩家的更新队列
 *
 * 职责：
 *   将物品添加到玩家的物品更新队列，以便在下次保存时同步到数据库。
 *
 * 参数：
 *   item   - 要添加的物品指针
 *   player - 玩家指针
 *
 * 主要流程：
 *   1. 检查物品是否已在队列中
 *   2. 验证玩家指针和所有者匹配
 *   3. 检查更新队列是否被阻塞
 *   4. 将物品添加到队列末尾并记录位置
 */
void AddItemToUpdateQueueOf(Item* item, Player* player)
{
    if (item->IsInUpdateQueue())
        return;

    ASSERT(player != nullptr);

    if (player->GetGUID() != item->GetOwnerGUID())
    {
        TC_LOG_DEBUG("entities.player.items", "AddItemToUpdateQueueOf - Owner's guid ({}) and player's guid ({}) don't match!",
            item->GetOwnerGUID().ToString(), player->GetGUID().ToString());
        return;
    }

    if (player->m_itemUpdateQueueBlocked)
        return;

    player->m_itemUpdateQueue.push_back(item);
    item->uQueuePos = player->m_itemUpdateQueue.size() - 1;
}

/**
 * @brief 从玩家的更新队列中移除物品
 *
 * 职责：
 *   将物品从玩家的物品更新队列中移除，通常在物品保存完成或删除时调用。
 *
 * 参数：
 *   item   - 要移除的物品指针
 *   player - 玩家指针
 *
 * 主要流程：
 *   1. 检查物品是否在队列中
 *   2. 验证玩家指针和所有者匹配
 *   3. 检查更新队列是否被阻塞
 *   4. 将队列中对应位置设为nullptr，重置物品的队列位置
 */
void RemoveItemFromUpdateQueueOf(Item* item, Player* player)
{
    if (!item->IsInUpdateQueue())
        return;

    ASSERT(player != nullptr);

    if (player->GetGUID() != item->GetOwnerGUID())
    {
        TC_LOG_DEBUG("entities.player.items", "RemoveItemFromUpdateQueueOf - Owner's guid ({}) and player's guid ({}) don't match!",
            item->GetOwnerGUID().ToString(), player->GetGUID().ToString());
        return;
    }

    if (player->m_itemUpdateQueueBlocked)
        return;

    player->m_itemUpdateQueue[item->uQueuePos] = nullptr;
    item->uQueuePos = -1;
}

/**
 * @brief 获取物品所在的背包槽位
 *
 * 职责：
 *   返回物品所在容器的背包槽位。
 *   如果物品不在任何背包中，返回INVENTORY_SLOT_BAG_0（表示主背包）。
 *
 * @return 背包槽位索引（0-3为背包槽位，INVENTORY_SLOT_BAG_0为主背包）
 */
uint8 Item::GetBagSlot() const
{
    return m_container ? m_container->GetSlot() : uint8(INVENTORY_SLOT_BAG_0);
}

/**
 * @brief 检查物品是否被装备
 *
 * 职责：
 *   判断物品是否位于装备槽位中。
 *
 * 返回值：
 *   true  - 物品在装备槽位中
 *   false - 物品不在装备槽位中（在背包或其他位置）
 */
bool Item::IsEquipped() const
{
    return !IsInBag() && m_slot < EQUIPMENT_SLOT_END;
}

/**
 * @brief 检查物品是否可以被交易
 *
 * 职责：
 *   判断物品是否可以通过邮件、交易等方式转让给其他玩家。
 *   检查多种限制条件如灵魂绑定、装备状态、战利品状态等。
 *
 * 参数：
 *   mail  - 是否通过邮件交易
 *   trade - 是否通过面对面交易
 *
 * 返回值：
 *   true  - 物品可以交易
 *   false - 物品不可交易
 *
 * 主要流程：
 *   1. 检查是否已生成战利品（已生成则不可交易）
 *   2. 检查灵魂绑定状态（绑定的物品大多数情况不可交易）
 *   3. 检查背包是否为空（非空背包不可交易）
 *   4. 检查所有者是否能卸下该物品
 *   5. 检查是否因附魔而绑定
 */
bool Item::CanBeTraded(bool mail, bool trade) const
{
    if (m_lootGenerated)
        return false;

    if ((!mail || !IsBoundAccountWide()) && (IsSoulBound() && (!IsBOPTradeable() || !trade)))
        return false;

    if (IsBag() && (Player::IsBagPos(GetPos()) || !ToBag()->IsEmpty()))
        return false;

    if (Player* owner = GetOwner())
    {
        if (owner->CanUnequipItem(GetPos(), false) != EQUIP_ERR_OK)
            return false;
        if (owner->GetLootGUID() == GetGUID())
            return false;
    }

    if (IsBoundByEnchant())
        return false;

    return true;
}

/**
 * @brief 计算物品的耐久度修复费用
 *
 * 职责：
 *   根据物品当前耐久度损耗、物品等级、品质等因素计算修复费用。
 *
 * 参数：
 *   discount - 折扣比例（0.0-1.0）
 *
 * 返回值：
 *   修复所需的铜币数量
 *
 * 主要流程：
 *   1. 获取物品的最大耐久度和当前耐久度
 *   2. 计算损失的耐久度
 *   3. 根据物品等级获取耐久度成本数据
 *   4. 根据物品品质获取品质乘数
 *   5. 根据物品类别（武器/护甲）获取耐久度乘数
 *   6. 计算最终费用：损失耐久度 * 乘数 * 品质因子 * 折扣 * 服务器倍率
 */
uint32 Item::CalculateDurabilityRepairCost(float discount) const
{
    uint32 maxDurability = GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
    if (!maxDurability)
        return 0;

    uint32 curDurability = GetUInt32Value(ITEM_FIELD_DURABILITY);
    ASSERT(maxDurability >= curDurability);

    uint32 lostDurability = maxDurability - curDurability;
    if (!lostDurability)
        return 0;

    ItemTemplate const* itemTemplate = GetTemplate();

    DurabilityCostsEntry const* durabilityCost = sDurabilityCostsStore.LookupEntry(itemTemplate->ItemLevel);
    if (!durabilityCost)
        return 0;

    uint32 durabilityQualityEntryId = (itemTemplate->Quality + 1) * 2;
    DurabilityQualityEntry const* durabilityQualityEntry = sDurabilityQualityStore.LookupEntry(durabilityQualityEntryId);
    if (!durabilityQualityEntry)
        return 0;

    uint32 dmultiplier;
    switch (itemTemplate->Class)
    {
        case ITEM_CLASS_WEAPON:
            dmultiplier = durabilityCost->WeaponSubClassCost[itemTemplate->SubClass];
            break;
        case ITEM_CLASS_ARMOR:
            dmultiplier = durabilityCost->ArmorSubClassCost[itemTemplate->SubClass];
            break;
        default:
            dmultiplier = 0;
            break;
    }

    uint32 cost = static_cast<uint32>(std::round(lostDurability * dmultiplier * double(durabilityQualityEntry->Data)));
    cost = uint32(cost * discount * sWorld->getRate(RATE_REPAIRCOST));

    if (cost == 0) // Fix for ITEM_QUALITY_ARTIFACT
        cost = 1;

    return cost;
}

/**
 * @brief 检查玩家是否拥有附魔所需的技能
 *
 * 职责：
 *   检查玩家是否满足物品上所有附魔的技能要求。
 *   用于判断玩家是否能正常使用该物品的附魔效果。
 *
 * 参数：
 *   player - 待检查的玩家指针
 *
 * 返回值：
 *   true  - 玩家满足所有附魔的技能要求
 *   false - 玩家不满足某个附魔的技能要求
 */
bool Item::HasEnchantRequiredSkill(Player const* player) const
{
    // Check all enchants for required skill
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->RequiredSkillID && player->GetSkillValue(enchantEntry->RequiredSkillID) < enchantEntry->RequiredSkillRank)
                    return false;

    return true;
}

/**
 * @brief 获取物品附魔所需的最低等级
 *
 * 职责：
 *   检查物品上所有附魔，返回所需等级中最高的一个。
 *
 * @return 所有附魔中要求等级最高的值，如果没有等级要求则返回0
 */
uint32 Item::GetEnchantRequiredLevel() const
{
    uint32 level = 0;

    // Check all enchants for required level
    // 检查所有附魔的等级要求
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->MinLevel > level)
                    level = enchantEntry->MinLevel;

    return level;
}

/**
 * @brief 检查物品是否因附魔而绑定
 *
 * 职责：
 *   检查物品上是否有任何附魔会使其灵魂绑定。
 *   某些附魔在应用时会使物品绑定。
 *
 * 返回值：
 *   true  - 物品上有至少一个会使物品绑定的附魔
 *   false - 物品上没有会使物品绑定的附魔
 */
bool Item::IsBoundByEnchant() const
{
    // Check all enchants for soulbound
    for (uint32 enchant_slot = PERM_ENCHANTMENT_SLOT; enchant_slot < MAX_ENCHANTMENT_SLOT; ++enchant_slot)
        if (uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot)))
            if (SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id))
                if (enchantEntry->Flags & ENCHANTMENT_CAN_SOULBOUND)
                    return true;
    return false;
}

/**
 * @brief 检查物品是否可以与指定模板的物品部分合并
 *
 * 职责：
 *   判断当前物品是否可以与另一物品堆叠合并。
 *   用于堆叠物品的合并操作检查。
 *
 * 参数：
 *   proto - 待合并物品的模板指针
 *
 * 返回值：
 *   EQUIP_ERR_OK - 可以合并
 *   其他错误码 - 不能合并的原因
 *
 * 主要流程：
 *   1. 检查是否正在生成战利品
 *   2. 检查物品类型是否相同
 *   3. 检查当前堆叠数是否未达到上限
 */
InventoryResult Item::CanBeMergedPartlyWith(ItemTemplate const* proto) const
{
    // not allow merge looting currently items
    if (m_lootGenerated)
        return EQUIP_ERR_ALREADY_LOOTED;

    // check item type
    if (GetEntry() != proto->ItemId)
        return EQUIP_ERR_ITEM_CANT_STACK;

    // check free space (full stacks can't be target of merge
    if (GetCount() >= proto->GetMaxStackSize())
        return EQUIP_ERR_ITEM_CANT_STACK;

    return EQUIP_ERR_OK;
}

/**
 * @brief 检查物品是否符合法术要求
 *
 * 职责：
 *   判断物品是否符合特定法术的施放条件。
 *   主要用于附魔、磨刀石、武器油等法术的目标物品检查。
 *
 * 参数：
 *   spellInfo - 法术信息指针
 *
 * 返回值：
 *   true  - 物品符合法术要求
 *   false - 物品不符合法术要求
 *
 * 主要流程：
 *   1. 检查物品类别是否符合法术要求
 *   2. 对于附魔法术，特殊处理羊皮纸（Vellum）
 *   3. 检查物品子类别是否符合掩码要求
 *   4. 检查物品装备类型（库存类型）是否符合要求
 */
bool Item::IsFitToSpellRequirements(SpellInfo const* spellInfo) const
{
    ItemTemplate const* proto = GetTemplate();

    bool const isEnchantSpell = spellInfo->HasEffect(SPELL_EFFECT_ENCHANT_ITEM) || spellInfo->HasEffect(SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY) || spellInfo->HasEffect(SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC);
    if (spellInfo->EquippedItemClass != -1)                 // -1 == any item class
    {
        // Special case - accept vellum for armor/weapon requirements
        if (isEnchantSpell && ((spellInfo->EquippedItemClass == ITEM_CLASS_ARMOR && proto->IsArmorVellum())
            || (spellInfo->EquippedItemClass == ITEM_CLASS_WEAPON && proto->IsWeaponVellum())))
            return true;

        if (spellInfo->EquippedItemClass != int32(proto->Class))
            return false;                                   //  wrong item class

        if (spellInfo->EquippedItemSubClassMask != 0)        // 0 == any subclass
        {
            if ((spellInfo->EquippedItemSubClassMask & (1 << proto->SubClass)) == 0)
                return false;                               // subclass not present in mask
        }
    }

    if (isEnchantSpell && spellInfo->EquippedItemInventoryTypeMask != 0)       // 0 == any inventory type
    {
        // Special case - accept weapon type for main and offhand requirements
        if (proto->InventoryType == INVTYPE_WEAPON &&
            (spellInfo->EquippedItemInventoryTypeMask & (1 << INVTYPE_WEAPONMAINHAND) ||
             spellInfo->EquippedItemInventoryTypeMask & (1 << INVTYPE_WEAPONOFFHAND)))
            return true;
        else if ((spellInfo->EquippedItemInventoryTypeMask & (1 << proto->InventoryType)) == 0)
            return false;                                   // inventory type not present in mask
    }

    return true;
}

/**
 * @brief 设置物品附魔
 *
 * 职责：
 *   在指定附魔槽位设置附魔效果。
 *   同时发送附魔日志给玩家。
 *
 * 参数：
 *   slot     - 附魔槽位类型
 *   id       - 附魔ID
 *   duration - 附魔持续时间（秒）
 *   charges  - 附魔充能次数
 *   caster   - 施法者GUID（用于日志记录）
 *
 * 主要流程：
 *   1. 检查附魔数据是否有变化（避免不必要的数据库操作）
 *   2. 如果是可检查的附魔槽位：
 *      - 发送旧附魔移除日志
 *      - 发送新附魔添加日志
 *   3. 设置附魔ID、持续时间、充能次数
 *   4. 标记物品状态为已改变
 */
void Item::SetEnchantment(EnchantmentSlot slot, uint32 id, uint32 duration, uint32 charges, ObjectGuid caster /*= ObjectGuid::Empty*/)
{
    // Better lost small time at check in comparison lost time at item save to DB.
    if ((GetEnchantmentId(slot) == id) && (GetEnchantmentDuration(slot) == duration) && (GetEnchantmentCharges(slot) == charges))
        return;

    Player* owner = GetOwner();
    if (slot < MAX_INSPECTED_ENCHANTMENT_SLOT)
    {
        if (uint32 oldEnchant = GetEnchantmentId(slot))
            owner->GetSession()->SendEnchantmentLog(GetOwnerGUID(), ObjectGuid::Empty, GetEntry(), oldEnchant);

        if (id)
            owner->GetSession()->SendEnchantmentLog(GetOwnerGUID(), caster, GetEntry(), id);
    }

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_ID_OFFSET, id);
    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET, duration);
    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET, charges);
    SetState(ITEM_CHANGED, owner);
}

/**
 * @brief 设置附魔持续时间
 *
 * 职责：
 *   更新指定槽位附魔的持续时间。
 *   通常用于临时附魔的时间更新。
 *
 * 参数：
 *   slot     - 附魔槽位类型
 *   duration - 新的持续时间（秒）
 *   owner    - 物品所有者指针
 */
void Item::SetEnchantmentDuration(EnchantmentSlot slot, uint32 duration, Player* owner)
{
    if (GetEnchantmentDuration(slot) == duration)
        return;

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_DURATION_OFFSET, duration);
    SetState(ITEM_CHANGED, owner);
    // Cannot use GetOwner() here, has to be passed as an argument to avoid freeze due to hashtable locking
}

/**
 * @brief 设置附魔充能次数
 *
 * 职责：
 *   更新指定槽位附魔的充能次数。
 *   用于可消耗的附魔效果（如武器磨刀石）。
 *
 * @param slot 附魔槽位类型
 * @param charges 新的充能次数
 */
void Item::SetEnchantmentCharges(EnchantmentSlot slot, uint32 charges)
{
    if (GetEnchantmentCharges(slot) == charges)
        return;

    SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + ENCHANTMENT_CHARGES_OFFSET, charges);
    SetState(ITEM_CHANGED, GetOwner());
}

/**
 * @brief 清除指定槽位的附魔
 *
 * 职责：
 *   移除指定槽位的附魔效果。
 *   清零附魔ID、持续时间和充能次数。
 *
 * 参数：
 *   slot - 要清除的附魔槽位
 */
void Item::ClearEnchantment(EnchantmentSlot slot)
{
    if (!GetEnchantmentId(slot))
        return;

    for (uint8 x = 0; x < MAX_ITEM_ENCHANTMENT_EFFECTS; ++x)
        SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1 + slot*MAX_ENCHANTMENT_OFFSET + x, 0);
    SetState(ITEM_CHANGED, GetOwner());
}

/**
 * @brief 检查宝石是否符合插槽颜色要求
 *
 * 职责：
 *   检查物品上镶嵌的所有宝石是否满足对应插槽的颜色要求。
 *   用于判断是否激活插槽奖励。
 *
 * 返回值：
 *   true  - 所有宝石都符合插槽颜色要求
 *   false - 至少有一个宝石不符合要求
 *
 * 主要流程：
 *   1. 遍历所有宝石插槽
 *   2. 获取插槽颜色要求
 *   3. 获取镶嵌宝石的颜色属性
 *   4. 检查宝石颜色是否匹配插槽颜色
 */
bool Item::GemsFitSockets() const
{
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT+MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint8 SocketColor = GetTemplate()->Socket[enchant_slot-SOCK_ENCHANTMENT_SLOT].Color;

        if (!SocketColor) // no socket slot
            continue;

        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id) // no gems on this socket
            return false;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry) // invalid gem id on this socket
            return false;

        uint8 GemColor = 0;

        uint32 gemid = enchantEntry->SrcItemID;
        if (gemid)
        {
            ItemTemplate const* gemProto = sObjectMgr->GetItemTemplate(gemid);
            if (gemProto)
            {
                GemPropertiesEntry const* gemProperty = sGemPropertiesStore.LookupEntry(gemProto->GemProperties);
                if (gemProperty)
                    GemColor = gemProperty->Type;
            }
        }

        if (!(GemColor & SocketColor)) // bad gem color on this socket
            return false;
    }
    return true;
}

/**
 * @brief 获取指定ID宝石的数量
 *
 * 职责：
 *   统计物品上镶嵌的指定ID宝石的数量。
 *   用于检查宝石唯一性限制。
 *
 * @param GemID 宝石物品ID
 * @return 镶嵌的该宝石数量（0-3）
 */
uint8 Item::GetGemCountWithID(uint32 GemID) const
{
    uint8 count = 0;
    // 遍历所有宝石插槽
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT+MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id)
            continue;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry)
            continue;

        // 检查宝石ID是否匹配
        if (GemID == enchantEntry->SrcItemID)
            ++count;
    }
    return count;
}

/**
 * @brief 获取指定限制类别宝石的数量
 *
 * 职责：
 *   统计物品上镶嵌的属于指定限制类别的宝石数量。
 *   用于检查某些唯一性宝石的装备限制。
 *
 * @param limitCategory 物品限制类别ID
 * @return 属于该限制类别的宝石数量（0-3）
 *
 * @note 例如：某些高级宝石限制每个玩家只能装备一定数量
 */
uint8 Item::GetGemCountWithLimitCategory(uint32 limitCategory) const
{
    uint8 count = 0;
    // 遍历所有宝石插槽
    for (uint32 enchant_slot = SOCK_ENCHANTMENT_SLOT; enchant_slot < SOCK_ENCHANTMENT_SLOT+MAX_GEM_SOCKETS; ++enchant_slot)
    {
        uint32 enchant_id = GetEnchantmentId(EnchantmentSlot(enchant_slot));
        if (!enchant_id)
            continue;

        SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!enchantEntry)
            continue;

        // 获取宝石模板，检查限制类别
        ItemTemplate const* gemProto = sObjectMgr->GetItemTemplate(enchantEntry->SrcItemID);
        if (!gemProto)
            continue;

        if (gemProto->ItemLimitCategory == limitCategory)
            ++count;
    }
    return count;
}

/**
 * @brief 检查物品是否限制于其他地图或区域
 *
 * 职责：
 *   判断物品是否有地图或区域限制，且当前不在该限制区域内。
 *
 * @param cur_mapId 当前地图ID
 * @param cur_zoneId 当前区域ID
 * @return true 如果物品限制于其他地图或区域
 */
bool Item::IsLimitedToAnotherMapOrZone(uint32 cur_mapId, uint32 cur_zoneId) const
{
    ItemTemplate const* proto = GetTemplate();
    return proto && ((proto->Map && proto->Map != cur_mapId) || (proto->Area && proto->Area != cur_zoneId));
}

/**
 * @brief 发送插槽更新消息
 *
 * 职责：
 *   向客户端发送宝石插槽镶嵌结果的更新消息。
 *   用于通知客户端宝石镶嵌完成后的状态变化。
 */
void Item::SendUpdateSockets()
{
    WorldPacket data(SMSG_SOCKET_GEMS_RESULT, 8+4+4+4+4);
    data << uint64(GetGUID());
    // 包含所有插槽和插槽奖励的附魔ID
    for (uint32 i = SOCK_ENCHANTMENT_SLOT; i <= BONUS_ENCHANTMENT_SLOT; ++i)
        data << uint32(GetEnchantmentId(EnchantmentSlot(i)));

    GetOwner()->SendDirectMessage(&data);
}

/**
 * @brief 发送物品时间更新消息
 *
 * 职责：
 *   向客户端发送物品剩余持续时间的更新消息。
 *   虽然客户端已有物品数据，但需要此消息来正确显示剩余时间。
 *
 * 参数：
 *   owner - 物品所有者指针
 */
// Though the client has the information in the item's data field,
// we have to send SMSG_ITEM_TIME_UPDATE to display the remaining
// time.
void Item::SendTimeUpdate(Player* owner)
{
    uint32 duration = GetUInt32Value(ITEM_FIELD_DURATION);
    if (!duration)
        return;

    WorldPacket data(SMSG_ITEM_TIME_UPDATE, (8+4));
    data << uint64(GetGUID());
    data << uint32(duration);
    owner->SendDirectMessage(&data);
}

/**
 * @brief 创建物品实例（静态工厂方法）
 *
 * 职责：
 *   根据物品ID和数量创建新的物品实例。
 *   这是创建物品的主要入口点。
 *
 * 参数：
 *   itemEntry - 物品模板ID
 *   count     - 物品数量
 *   player    - 接收物品的玩家指针（可选）
 *
 * 返回值：
 *   创建的物品指针，失败返回nullptr
 *
 * 主要流程：
 *   1. 检查数量是否有效
 *   2. 获取物品模板
 *   3. 如果数量超过最大堆叠数，限制为最大堆叠数
 *   4. 创建物品对象（可能是背包类型）
 *   5. 调用Create()方法初始化物品
 */
Item* Item::CreateItem(uint32 itemEntry, uint32 count, Player const* player /*= nullptr*/)
{
    if (count < 1)
        return nullptr;                                        //don't create item at zero count

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (proto)
    {
        if (count > proto->GetMaxStackSize())
            count = proto->GetMaxStackSize();

        ASSERT_NODEBUGINFO(count != 0 && "pProto->Stackable == 0 but checked at loading already");

        Item* item = NewItemOrBag(proto);
        if (item->Create(sObjectMgr->GetGenerator<HighGuid::Item>().Generate(), itemEntry, player))
        {
            item->SetCount(count);
            return item;
        }
        else
            delete item;
    }
    else
        ABORT();
    return nullptr;
}

/**
 * @brief 克隆物品
 *
 * 职责：
 *   创建当前物品的副本，复制基本属性但不包括所有状态。
 *   常用于拆分堆叠物品或复制物品。
 *
 * 参数：
 *   count  - 新物品的数量
 *   player - 接收新物品的玩家指针（可选）
 *
 * 返回值：
 *   新物品指针，失败返回nullptr
 *
 * 主要流程：
 *   1. 调用CreateItem创建基础物品
 *   2. 复制创建者、赠送者GUID
 *   3. 复制标志位（移除可退款和可交易标志）
 *   4. 复制持续时间
 *   5. 如果有玩家参数，复制随机属性
 */
Item* Item::CloneItem(uint32 count, Player const* player /*= nullptr*/) const
{
    Item* newItem = CreateItem(GetEntry(), count, player);
    if (!newItem)
        return nullptr;

    newItem->SetGuidValue(ITEM_FIELD_CREATOR, GetGuidValue(ITEM_FIELD_CREATOR));
    newItem->SetGuidValue(ITEM_FIELD_GIFTCREATOR, GetGuidValue(ITEM_FIELD_GIFTCREATOR));
    newItem->SetUInt32Value(ITEM_FIELD_FLAGS,        GetUInt32Value(ITEM_FIELD_FLAGS) & ~(ITEM_FIELD_FLAG_REFUNDABLE | ITEM_FIELD_FLAG_BOP_TRADEABLE));
    newItem->SetUInt32Value(ITEM_FIELD_DURATION,     GetUInt32Value(ITEM_FIELD_DURATION));
    // player CAN be NULL in which case we must not update random properties because that accesses player's item update queue
    if (player)
        newItem->SetItemRandomProperties(GetItemRandomPropertyId());
    return newItem;
}

/**
 * @brief 检查物品是否与指定玩家绑定
 *
 * 职责：
 *   判断物品是否被绑定且不能由指定玩家使用。
 *   用于交易、拍卖等场景的权限检查。
 *
 * 参数：
 *   player - 待检查的玩家指针
 *
 * 返回值：
 *   true  - 物品已绑定且不能由该玩家使用
 *   false - 物品未绑定，或该玩家是物品所有者，或物品可交易
 *
 * 主要流程：
 *   1. 未绑定物品直接返回false
 *   2. 如果玩家是所有者返回false
 *   3. 如果物品可交易且玩家在允许列表中返回false
 *   4. 战网绑定物品返回false
 */
bool Item::IsBindedNotWith(Player const* player) const
{
    // not binded item
    if (!IsSoulBound())
        return false;

    // own item
    if (GetOwnerGUID() == player->GetGUID())
        return false;

    if (IsBOPTradeable())
        if (allowedGUIDs.find(player->GetGUID()) != allowedGUIDs.end())
            return false;

    // BOA item case
    if (IsBoundAccountWide())
        return false;

    return true;
}

/**
 * @brief 构建物品更新数据
 *
 * 职责：
 *   为物品构建更新数据包，发送给客户端同步物品状态。
 *
 * 参数：
 *   data_map - 更新数据映射表，按玩家分组存储更新数据
 *
 * 主要流程：
 *   1. 获取物品所有者
 *   2. 构建字段更新数据
 *   3. 清除更新掩码
 */
void Item::BuildUpdate(UpdateDataMapType& data_map)
{
    if (Player* owner = GetOwner())
        BuildFieldsUpdate(owner, data_map);
    ClearUpdateMask(false);
}

/**
 * @brief 将物品添加到对象更新列表
 *
 * 职责：
 *   将物品添加到地图的对象更新列表，用于定期同步更新。
 *
 * @return true 如果成功添加，false 如果物品没有所有者
 */
bool Item::AddToObjectUpdate()
{
    if (Player* owner = GetOwner())
    {
        owner->GetMap()->AddUpdateObject(this);
        return true;
    }

    return false;
}

/**
 * @brief 从对象更新列表移除物品
 *
 * 职责：
 *   将物品从地图的对象更新列表中移除。
 */
void Item::RemoveFromObjectUpdate()
{
    if (Player* owner = GetOwner())
        owner->GetMap()->RemoveUpdateObject(this);
}

/**
 * @brief 保存物品退款数据到数据库
 *
 * 职责：
 *   将物品的可退款相关数据保存到数据库。
 *   包括退款接收者、支付的金额和扩展消耗数据。
 *
 * 主要流程：
 *   1. 开启数据库事务
 *   2. 删除旧的退款记录（如果存在）
 *   3. 插入新的退款记录
 *   4. 提交事务
 */
void Item::SaveRefundDataToDB()
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_REFUND_INSTANCE);
    stmt->setUInt32(0, GetGUID().GetCounter());
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ITEM_REFUND_INSTANCE);
    stmt->setUInt32(0, GetGUID().GetCounter());
    stmt->setUInt32(1, GetRefundRecipient());
    stmt->setUInt32(2, GetPaidMoney());
    stmt->setUInt16(3, uint16(GetPaidExtendedCost()));
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 从数据库删除退款数据
 *
 * 职责：
 *   从 item_refund_instance 表中删除物品的退款记录。
 *
 * @param trans 数据库事务指针（如果为空则不执行删除）
 */
void Item::DeleteRefundDataFromDB(CharacterDatabaseTransaction* trans)
{
    if (trans)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_REFUND_INSTANCE);
        stmt->setUInt32(0, GetGUID().GetCounter());
        (*trans)->Append(stmt);

    }
}

/**
 * @brief 设置物品为不可退款
 *
 * 职责：
 *   移除物品的可退款标志和相关数据。
 *   当物品超过退款时限或不再符合退款条件时调用。
 *
 * 参数：
 *   owner       - 物品所有者指针
 *   changestate - 是否标记物品状态为已改变
 *   trans       - 数据库事务指针（可选）
 *
 * 主要流程：
 *   1. 检查物品是否可退款
 *   2. 移除可退款标志
 *   3. 清空退款相关数据（接收者、支付金额、扩展消耗）
 *   4. 从数据库删除退款数据
 *   5. 从所有者的退款引用列表中移除
 */
void Item::SetNotRefundable(Player* owner, bool changestate /*=true*/, CharacterDatabaseTransaction* trans /*=nullptr*/)
{
    if (!IsRefundable())
        return;

    RemoveFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE);
    // Following is not applicable in the trading procedure
    if (changestate)
        SetState(ITEM_CHANGED, owner);

    SetRefundRecipient(0);
    SetPaidMoney(0);
    SetPaidExtendedCost(0);
    DeleteRefundDataFromDB(trans);

    owner->DeleteRefundReference(GetGUID());
}

/**
 * @brief 更新物品的游玩时间
 *
 * 职责：
 *   更新物品的累计游玩时间，用于退款时限计算。
 *   如果超过2小时退款时限，将物品设置为不可退款。
 *
 * 参数：
 *   owner - 物品所有者指针
 *
 * 主要流程：
 *   1. 获取当前游玩时间
 *   2. 计算自上次更新以来经过的时间
 *   3. 如果未超过2小时，更新游玩时间
 *   4. 如果超过2小时，设置物品为不可退款
 */
void Item::UpdatePlayedTime(Player* owner)
{
    /*  Here we update our played time
        We simply add a number to the current played time,
        based on the time elapsed since the last update hereof.
    */
    // Get current played time
    uint32 current_playtime = GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME);
    // Calculate time elapsed since last played time update
    time_t curtime = GameTime::GetGameTime();
    uint32 elapsed = uint32(curtime - m_lastPlayedTimeUpdate);
    uint32 new_playtime = current_playtime + elapsed;
    // Check if the refund timer has expired yet
    if (new_playtime <= 2*HOUR)
    {
        // No? Proceed.
        // Update the data field
        SetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME, new_playtime);
        // Flag as changed to get saved to DB
        SetState(ITEM_CHANGED, owner);
        // Speaks for itself
        m_lastPlayedTimeUpdate = curtime;
        return;
    }
    // Yes
    SetNotRefundable(owner);
}

/**
 * @brief 获取物品的累计游玩时间
 *
 * 职责：
 *   计算物品从创建/获取以来的累计游玩时间。
 *   用于退款时限判断。
 *
 * @return 累计游玩时间（秒）
 */
uint32 Item::GetPlayedTime()
{
    time_t curtime = GameTime::GetGameTime();
    uint32 elapsed = uint32(curtime - m_lastPlayedTimeUpdate);
    return GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME) + elapsed;
}

/**
 * @brief 检查退款是否已过期
 *
 * 职责：
 *   判断物品是否已超过2小时的退款时限。
 *
 * @return true 如果已超过2小时
 */
bool Item::IsRefundExpired()
{
    return (GetPlayedTime() > 2*HOUR);
}

/**
 * @brief 设置物品为可交易的灵魂绑定物品
 *
 * 职责：
 *   将物品标记为可交易状态，并记录允许交易的玩家列表。
 *   用于某些绑定物品的限时交易功能（如团队副本拾取）。
 *
 * 参数：
 *   allowedLooters - 允许交易的玩家GUID集合
 */
void Item::SetSoulboundTradeable(GuidSet const& allowedLooters)
{
    SetFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE);
    allowedGUIDs = allowedLooters;
}

/**
 * @brief 清除物品的灵魂绑定可交易状态
 *
 * 职责：
 *   移除物品的可交易标志和允许交易的玩家列表。
 *   清理数据库中的相关记录。
 *
 * 参数：
 *   currentOwner - 当前物品所有者指针
 */
void Item::ClearSoulboundTradeable(Player* currentOwner)
{
    RemoveFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE);
    if (allowedGUIDs.empty())
        return;

    allowedGUIDs.clear();
    SetState(ITEM_CHANGED, currentOwner);
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_BOP_TRADE);
    stmt->setUInt32(0, GetGUID().GetCounter());
    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 检查灵魂绑定交易是否已过期
 *
 * 职责：
 *   检查物品的可交易时限是否已过期（2小时）。
 *   如果过期，清除可交易状态。
 *
 * @return true 如果已过期并清理了交易状态
 *
 * @note 此方法从所有者的更新循环中调用，GetOwner()必须有效
 */
bool Item::CheckSoulboundTradeExpire()
{
    // called from owner's update - GetOwner() MUST be valid
    // 检查是否已超过2小时的交易时限
    if (GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME) + 2*HOUR < GetOwner()->GetTotalPlayedTime())
    {
        ClearSoulboundTradeable(GetOwner());
        return true; // remove from tradeable list
    }

    return false;
}

/**
 * @brief 设置物品堆叠数量
 *
 * 职责：
 *   设置物品的堆叠数量，并更新交易数据（如果物品正在交易中）。
 *
 * 参数：
 *   value - 新的堆叠数量
 *
 * 主要流程：
 *   1. 设置堆叠数量值
 *   2. 如果物品所有者正在进行交易，更新交易窗口中的物品信息
 */
void Item::SetCount(uint32 value)
{
    SetUInt32Value(ITEM_FIELD_STACK_COUNT, value);

    if (Player* player = GetOwner())
    {
        if (TradeData* tradeData = player->GetTradeData())
        {
            TradeSlots slot = tradeData->GetTradeSlotForItem(GetGUID());

            if (slot != TRADE_SLOT_INVALID)
                tradeData->SetItem(slot, this, true);
        }
    }
}

/**
 * @brief 获取物品的调试信息
 *
 * 职责：
 *   生成包含物品关键信息的调试字符串，用于日志记录和问题排查。
 *
 * @return 调试信息字符串，包含GUID、所有者、数量、位置、装备状态等
 */
std::string Item::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Object::GetDebugInfo() << "\n"
        << std::boolalpha
        << "Owner: " << GetOwnerGUID().ToString() << " Count: " << GetCount()
        << " BagSlot: " << std::to_string(GetBagSlot()) << " Slot: " << std::to_string(GetSlot()) << " Equipped: " << IsEquipped();
    return sstr.str();
}
