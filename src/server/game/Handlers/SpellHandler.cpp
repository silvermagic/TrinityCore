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
 * @file SpellHandler.cpp
 * @brief 法术系统核心处理器模块
 *
 * @模块职责:
 *   处理玩家法术相关的所有网络消息，包括：
 *   - 法术施放、取消、引导
 *   - 物品使用和打开
 *   - 游戏对象交互
 *   - 光环管理（增益、减益）
 *   - 图腾系统
 *   - 镜像图像
 *   - 投射物位置同步
 *
 * @主要功能:
 *   1. 法术施放系统：处理玩家主动施放法术的完整流程
 *   2. 物品使用系统：处理物品的法术触发和容器打开
 *   3. 光环系统：管理玩家身上的增益、减益效果
 *   4. 引导法术：处理需要持续引导的法术
 *   5. 自动射击：管理远程武器的自动射击机制
 *   6. 图腾系统：萨满图腾的创建和销毁
 *   7. 镜像图像：法师镜像等复制单位的外观同步
 *
 * @性能注意事项:
 *   - 法术验证涉及大量条件检查，需要优化查询
 *   - 物品使用涉及数据库异步查询
 *   - 光环更新会触发客户端数据同步，数据量较大
 *   - 镜像图像需要发送完整的装备外观信息
 *
 * @网络协议:
 *   使用标准的WorldPacket进行客户端-服务器通信
 *   支持弹道法术的特殊数据包处理
 */

#include "WorldSession.h"
#include "Common.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "DBCStores.h"
#include "GameClient.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "Totem.h"
#include "TotemPackets.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 处理客户端施法标志
 *
 * @职责:
 *   解析客户端施法数据包中的额外数据，包括弹道高度、速度和移动数据。
 *   某些法术（如投掷类法术）需要额外的弹道信息。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包
 *   castFlags  - 施法标志位，0x02表示包含弹道数据
 *   targets    - 法术目标对象，用于存储解析出的目标信息
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 检查施法标志是否包含弹道数据（0x02位）
 *   2. 如果包含，则读取弹道高度和速度
 *   3. 将高度和速度设置到目标对象中
 *   4. 检查是否包含移动数据，如果有则处理移动指令
 */
void WorldSession::HandleClientCastFlags(WorldPacket& recvPacket, uint8 castFlags, SpellCastTargets& targets)
{
    // 某些施法数据包包含额外数据（用于弹道法术）
    if (castFlags & 0x02)
    {
        // 读取弹道高度和速度
        float elevation, speed;
        recvPacket >> elevation;
        recvPacket >> speed;

        targets.SetElevation(elevation);
        targets.SetSpeed(speed);

        // 检查是否包含移动数据
        uint8 hasMovementData;
        recvPacket >> hasMovementData;
        if (hasMovementData)
        {
            // 读取移动操作码并处理移动数据
            recvPacket.SetOpcode(recvPacket.read<uint32>());
            HandleMovementOpcodes(recvPacket);
        }
    }
}

/**
 * @brief 处理使用物品操作码
 *
 * @职责:
 *   处理玩家使用物品的网络请求，包括验证物品有效性、
 *   检查使用条件、处理绑定逻辑，并触发物品施法。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含物品位置、施法次数、法术ID等信息
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中解析物品位置（背包索引、槽位）、施法次数、法术ID、物品GUID等信息
 *   2. 验证雕文索引是否有效
 *   3. 获取并验证物品对象（存在性、GUID匹配、模板有效性）
 *   4. 检查物品是否需要装备状态才能使用
 *   5. 检查玩家是否可以使用该物品
 *   6. 验证竞技场限制（消耗品、禁止物品）
 *   7. 检查战斗状态限制
 *   8. 处理物品绑定逻辑（使用时绑定）
 *   9. 读取法术目标信息
 *   10. 调用脚本系统或执行物品施法
 */
void WorldSession::HandleUseItemOpcode(WorldPacket& recvPacket)
{
    /// @todo add targets.read() check
    Player* pUser = _player;

    uint8 bagIndex, slot, castFlags;
    uint8 castCount;                                        // 下一次施法计数（用于连击或单一施法）
    ObjectGuid itemGUID;
    uint32 glyphIndex;                                      // 雕文索引
    uint32 spellId;                                         // 施放的法术ID

    recvPacket >> bagIndex >> slot >> castCount >> spellId >> itemGUID >> glyphIndex >> castFlags;

    // 验证雕文索引是否有效
    if (glyphIndex >= MAX_GLYPH_SLOT_INDEX)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    // 获取可使用的物品对象
    Item* pItem = pUser->GetUseableItemByPos(bagIndex, slot);
    if (!pItem)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    // 验证物品GUID是否匹配（防止作弊）
    if (pItem->GetGUID() != itemGUID)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    TC_LOG_DEBUG("network", "WORLD: CMSG_USE_ITEM packet, bagIndex: {}, slot: {}, castCount: {}, spellId: {}, Item: {}, glyphIndex: {}, data length = {}", bagIndex, slot, castCount, spellId, pItem->GetEntry(), glyphIndex, (uint32)recvPacket.size());

    // 获取物品模板（静态数据）
    ItemTemplate const* proto = pItem->GetTemplate();
    if (!proto)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, nullptr);
        return;
    }

    // 某些物品类型只能在装备状态下使用
    if (proto->InventoryType != INVTYPE_NON_EQUIP && !pItem->IsEquipped())
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, nullptr);
        return;
    }

    // 检查玩家是否可以使用该物品
    InventoryResult msg = pUser->CanUseItem(pItem);
    if (msg != EQUIP_ERR_OK)
    {
        pUser->SendEquipError(msg, pItem, nullptr);
        return;
    }

    // 竞技场中只允许使用制造的消耗品、绷带、毒药（数据库中应有2^21标志）
    if (proto->Class == ITEM_CLASS_CONSUMABLE && !proto->HasFlag(ITEM_FLAG_IGNORE_DEFAULT_ARENA_RESTRICTIONS) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, nullptr);
        return;
    }

    // 不允许使用竞技场禁用的物品
    if (proto->HasFlag(ITEM_FLAG_NOT_USEABLE_IN_ARENA) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, nullptr);
        return;
    }

    // 战斗状态检查
    if (pUser->IsInCombat())
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId))
            {
                if (!spellInfo->CanBeUsedInCombat())
                {
                    pUser->SendEquipError(EQUIP_ERR_NOT_IN_COMBAT, pItem, nullptr);
                    return;
                }
            }
        }
    }

    // 检查使用时绑定、拾取时绑定、任务物品绑定逻辑
    // 对于GM使用.additem或.additemset添加的物品，此时还未绑定
    if (pItem->GetTemplate()->Bonding == BIND_WHEN_USE || pItem->GetTemplate()->Bonding == BIND_WHEN_PICKED_UP || pItem->GetTemplate()->Bonding == BIND_QUEST_ITEM)
    {
        if (!pItem->IsSoulBound())
        {
            pItem->SetState(ITEM_CHANGED, pUser);
            pItem->SetBinding(true);
        }
    }

    // 读取法术目标信息
    SpellCastTargets targets;
    targets.Read(recvPacket, pUser);
    HandleClientCastFlags(recvPacket, castFlags, targets);

    // 注意：如果脚本停止施法，必须向客户端发送适当的数据以防止物品卡在灰色状态
    if (!sScriptMgr->OnItemUse(pUser, pItem, targets))
    {
        // 没有脚本或脚本未自行处理请求
        pUser->CastItemUseSpell(pItem, targets, castCount, glyphIndex);
    }
}

/**
 * @brief 处理打开物品操作码
 *
 * @职责:
 *   处理玩家打开物品的网络请求，包括打开包裹、打开有战利品的容器等。
 *   支持打开锁定的物品和包装好的礼物物品。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含物品位置信息
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 忽略被控制状态下的请求
 *   2. 验证玩家是否存活
 *   3. 解析物品位置（背包索引、槽位）
 *   4. 获取并验证物品对象
 *   5. 验证物品是否可以打开（有战利品标志或是包装物品）
 *   6. 检查物品锁定状态
 *   7. 如果是包装物品，查询礼物数据并异步处理
 *   8. 如果是普通容器，直接发送战利品窗口
 */
void WorldSession::HandleOpenItemOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_OPEN_ITEM packet, data length = {}", (uint32)recvPacket.size());

    Player* player = GetPlayer();

    // 忽略被控制状态下的请求
    if (player->IsCharmed())
        return;

    // 额外检查，客户端会自行输出消息
    if (!player->IsAlive())
    {
        player->SendEquipError(EQUIP_ERR_YOU_ARE_DEAD, nullptr, nullptr);
        return;
    }

    // 读取物品位置
    uint8 bagIndex, slot;
    recvPacket >> bagIndex >> slot;

    TC_LOG_INFO("network", "bagIndex: {}, slot: {}", bagIndex, slot);

    // 获取物品对象
    Item* item = player->GetItemByPos(bagIndex, slot);
    if (!item)
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    // 获取物品模板
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, nullptr);
        return;
    }

    // 验证物品是否有战利品或是否为包装物品
    if (!proto->HasFlag(ITEM_FLAG_HAS_LOOT) && !item->IsWrapped())
    {
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
        TC_LOG_ERROR("entities.player.cheat", "Possible hacking attempt: Player {} {} tried to open item [{}, entry: {}] which is not openable!",
            player->GetName(), player->GetGUID().ToString(), item->GetGUID().ToString(), proto->ItemId);
        return;
    }

    // 处理锁定的物品
    uint32 lockId = proto->LockID;
    if (lockId)
    {
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

        if (!lockInfo)
        {
            player->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, nullptr);
            TC_LOG_ERROR("network", "WORLD::OpenItem: item {} has an unknown lockId: {}!", item->GetGUID().ToString(), lockId);
            return;
        }

        // 检查物品是否已解锁
        if (item->IsLocked())
        {
            player->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, nullptr);
            return;
        }
    }

    // 处理包装物品和普通容器
    if (item->IsWrapped())
    {
        // 异步查询礼物数据
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_GIFT_BY_ITEM);
        stmt->setUInt32(0, item->GetGUID().GetCounter());
        _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
            .WithPreparedCallback(std::bind(&WorldSession::HandleOpenWrappedItemCallback, this, item->GetPos(), item->GetGUID(), std::placeholders::_1)));
    }
    else
        player->SendLoot(item->GetGUID(), LOOT_CORPSE);
}

/**
 * @brief 处理打开包装物品的回调
 *
 * @职责:
 *   异步处理打开包装物品的结果，将礼物物品转换为实际物品。
 *   从character_gifts表中读取礼物数据并更新物品属性。
 *
 * @参数:
 *   pos      - 物品在背包中的位置
 *   itemGuid - 物品的GUID
 *   result   - 数据库查询结果
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 验证玩家和物品是否仍然存在
 *   2. 验证物品GUID和包装状态是否匹配
 *   3. 如果查询结果为空，删除无效的礼物记录
 *   4. 从查询结果中读取物品ID和标志
 *   5. 更新物品属性（清除礼物创建者、设置物品ID、标志、耐久度）
 *   6. 保存玩家背包和金币到数据库
 *   7. 从character_gifts表中删除礼物记录
 */
void WorldSession::HandleOpenWrappedItemCallback(uint16 pos, ObjectGuid itemGuid, PreparedQueryResult result)
{
    if (!GetPlayer())
        return;

    // 获取物品对象
    Item* item = GetPlayer()->GetItemByPos(pos);
    if (!item)
        return;

    // 验证物品是否仍然是同一个物品且仍为包装状态
    if (item->GetGUID() != itemGuid || !item->IsWrapped())
        return;

    // 如果没有查询结果，删除无效的礼物
    if (!result)
    {
        TC_LOG_ERROR("network", "Wrapped item {} does't have record in character_gifts table and will deleted", itemGuid.ToString());
        GetPlayer()->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        return;
    }

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 从查询结果中读取礼物数据
    Field* fields = result->Fetch();
    uint32 entry = fields[0].GetUInt32();
    uint32 flags = fields[1].GetUInt32();

    // 更新物品属性
    item->SetGuidValue(ITEM_FIELD_GIFTCREATOR, ObjectGuid::Empty);
    item->SetEntry(entry);
    item->SetUInt32Value(ITEM_FIELD_FLAGS, flags);
    item->SetUInt32Value(ITEM_FIELD_MAXDURABILITY, item->GetTemplate()->MaxDurability);
    item->SetState(ITEM_CHANGED, GetPlayer());

    // 保存背包和金币
    GetPlayer()->SaveInventoryAndGoldToDB(trans);

    // 删除礼物记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GIFT);
    stmt->setUInt32(0, itemGuid.GetCounter());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 处理使用游戏对象操作码
 *
 * @职责:
 *   处理玩家使用游戏对象的网络请求，如打开宝箱、使用传送门等。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含游戏对象GUID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取游戏对象GUID
 *   2. 检查玩家是否可以与该游戏对象交互
 *   3. 处理被控制状态下的限制（骑乘状态检查）
 *   4. 调用游戏对象的Use方法
 */
void WorldSession::HandleGameObjectUseOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJ_USE Message [{}]", guid.ToString());

    if (GameObject* obj = GetPlayer()->GetGameObjectIfCanInteractWith(guid))
    {
        // 忽略被控制状态下的请求，除非在载具上、已骑乘或游戏对象允许骑乘时使用
        if (GetPlayer()->IsCharmed())
            if (!(GetPlayer()->IsOnVehicle(GetPlayer()->GetCharmed()) || GetPlayer()->IsMounted()) && !obj->GetGOInfo()->IsUsableMounted())
                return;

        obj->Use(GetPlayer());
    }
}

/**
 * @brief 处理报告游戏对象使用操作码
 *
 * @职责:
 *   处理玩家报告游戏对象使用的网络请求，主要用于成就系统的追踪。
 *   某些游戏对象需要通过报告使用来触发成就进度。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含游戏对象GUID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取游戏对象GUID
 *   2. 忽略被控制状态下的请求
 *   3. 检查玩家是否可以与该游戏对象交互
 *   4. 调用游戏对象AI的OnReportUse方法
 *   5. 更新成就进度
 */
void WorldSession::HandleGameobjectReportUse(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    recvPacket >> guid;

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJ_REPORT_USE Message [{}]", guid.ToString());

    // 忽略被控制状态下的请求
    if (_player->IsCharmed())
        return;

    if (GameObject* go = GetPlayer()->GetGameObjectIfCanInteractWith(guid))
    {
        // 先让AI处理报告使用事件
        if (go->AI()->OnReportUse(_player))
            return;

        // 更新成就进度
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT, go->GetEntry());
    }
}

/**
 * @brief 处理施放法术操作码
 *
 * @职责:
 *   处理玩家施放法术的网络请求，是法术系统的核心入口。
 *   包括验证法术有效性、目标处理、自动射击特殊处理、等级匹配等。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含施法次数、法术ID、施法标志等信息
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取施法次数、法术ID、施法标志
 *   2. 获取法术信息并验证有效性
 *   3. 检查法术是否为被动技能（被动技能不能主动施放）
 *   4. 从数据包中读取目标信息
 *   5. 验证玩家是否已学习该法术（防作弊检查）
 *   6. 处理特殊允许施放的情况（锁定的游戏对象、客户端触发的光环法术）
 *   7. 处理自动射击法术的特殊逻辑（避免重复施放）
 *   8. 根据目标等级自动选择法术等级（增益法术）
 *   9. 创建法术实例并准备施法
 */
void WorldSession::HandleCastSpellOpcode(WorldPacket& recvPacket)
{
    uint32 spellId;
    uint8  castCount, castFlags;
    // 从报文中读取施法次数（避免技能相互触发，变成死循环）、施法ID、施法标志
    recvPacket >> castCount >> spellId >> castFlags;
    TriggerCastFlags triggerFlag = TRIGGERED_NONE;

    TC_LOG_DEBUG("network", "WORLD: got cast spell packet, castCount: {}, spellId: {}, castFlags: {}, data length = {}", castCount, spellId, castFlags, (uint32)recvPacket.size());

    // 获取技能信息（静态数据）
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown spell id {}", spellId);
        recvPacket.rfinish(); // 防止在忽略数据包时产生垃圾日志
        return;
    }

    // 被动技能不能被主动施放
    if (spellInfo->IsPassive())
    {
        recvPacket.rfinish(); // 防止在忽略数据包时产生垃圾日志
        return;
    }

    // 客户端提供的目标
    SpellCastTargets targets;
    // 从报文中读取目标信息
    targets.Read(recvPacket, _player);
    HandleClientCastFlags(recvPacket, castFlags, targets);

    // 检查玩家是否已经学习了该技能（防作弊）
    if (!_player->HasActiveSpell(spellId))
    {
        bool allow = false;

        // 允许施放未知法术的特殊情况：锁定的游戏对象
        if (GameObject* go = targets.GetGOTarget())
            if (go->GetSpellForLock(_player) == spellInfo)
                allow = true;

        // 允许施放由客户端周期触发光环触发的法术
        if (_player->HasAuraTypeWithTriggerSpell(SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT, spellId))
        {
            allow = true;
            triggerFlag = TRIGGERED_FULL_MASK;
        }

        if (!allow)
            return;
    }

    // 客户端在射击轮转期间施放其他法术时会重新发送自动射击施法操作码
    // 跳过它以防止出现"打断"消息
    // 同时检查目标！目标可能已更改，我们需要打断当前法术
    if (spellInfo->IsAutoRepeatRangedSpell())
    {
        // 获取当前自动重复施放的法术
        if (Spell* spell = _player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        {
            // 检查是否是同一个技能，并且目标相同
            if (spell->m_spellInfo == spellInfo && spell->m_targets.GetUnitTargetGUID() == targets.GetUnitTargetGUID())
            {
                recvPacket.rfinish();
                return;
            }
        }
    }

    // 根据目标等级自动选择增益法术等级
    // TODO: 这是否真的必要？客户端似乎已经为"标准"增益发送了正确的等级
    if (spellInfo->IsPositive())
        if (Unit* target = targets.GetUnitTarget())
        {
            SpellInfo const* actualSpellInfo = spellInfo->GetAuraRankForLevel(target->GetLevel());

            // 如果找不到等级，函数返回NULL，但在显式施法情况下，可以施放原始法术并稍后失败并显示适当的错误消息
            if (actualSpellInfo)
                spellInfo = actualSpellInfo;
        }

    // 调试日志输出
    if (std::find(debugSpellIds.begin(), debugSpellIds.end(), spellInfo->Id) != debugSpellIds.end()) {
        TC_LOG_DEBUG("spells", "[XX-{}][{}] WorldSession::HandleCastSpellOpcode - TargetMask: {} TargetGUID: {}", debugSpellSeqId++, spellInfo->Id, targets.GetTargetMask(), targets.GetObjectTargetGUID().ToString());
    }

    // 创建技能实例（动态数据）
    Spell* spell = new Spell(_player, spellInfo, triggerFlag);
    spell->m_fromClient = true;
    spell->m_cast_count = castCount;                       // 设置施法计数
    spell->prepare(targets);
}

/**
 * @brief 处理取消施法操作码
 *
 * @职责:
 *   处理玩家主动取消正在施放的法术的网络请求。
 *
 * @参数:
 *   cancelCast - 取消施法数据包，包含要取消的法术ID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 忽略被控制状态下的请求
 *   2. 检查玩家是否正在施放非近战法术
 *   3. 打断指定的非近战法术
 */
void WorldSession::HandleCancelCastOpcode(WorldPackets::Spells::CancelCast& cancelCast)
{
    // 忽略被控制状态下的请求
    if (_player->IsCharmed())
        return;

    // 如果玩家正在施放非近战法术，则打断
    if (_player->IsNonMeleeSpellCast(false))
        _player->InterruptNonMeleeSpells(false, cancelCast.SpellID, false);
}

/**
 * @brief 处理取消光环操作码
 *
 * @职责:
 *   处理玩家主动取消身上的光环效果的网络请求。
 *   包括取消增益法术、引导法术等。
 *
 * @参数:
 *   cancelAura - 取消光环数据包，包含要取消的法术ID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 获取法术信息并验证有效性
 *   2. 检查法术是否允许被取消（某些法术不可取消）
 *   3. 如果是引导法术，打断正在引导的法术
 *   4. 对于非引导法术，验证是否为正面效果且非被动
 *   5. 移除指定的光环
 *   6. 处理资源追踪类法术的特殊逻辑（取消一个时同时取消另一个）
 */
void WorldSession::HandleCancelAuraOpcode(WorldPackets::Spells::CancelAura& cancelAura)
{
    // 获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cancelAura.SpellID);
    if (!spellInfo)
        return;

    // 不允许移除具有SPELL_ATTR0_CANT_CANCEL属性的法术
    if (spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL))
        return;

    // 引导法术的情况（当前正在施放）
    if (spellInfo->IsChanneled())
    {
        if (Spell* curSpell = _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (curSpell->m_spellInfo->Id == cancelAura.SpellID)
                _player->InterruptSpell(CURRENT_CHANNELED_SPELL);
        return;
    }

    // 非引导法术的情况：
    // 不允许移除非正面法术
    // 不允许取消被动光环（某些被动光环是可见的）
    if (!spellInfo->IsPositive() || spellInfo->IsPassive())
        return;

    // 移除光环（可能当有多个时只移除一个？）
    _player->RemoveOwnedAura(cancelAura.SpellID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);

    // 如果移除的法术是资源追踪类，检查玩家是否同时追踪两个（草药/矿物）并移除另一个
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TRACK_BOTH_RESOURCES) && spellInfo->HasAura(SPELL_AURA_TRACK_RESOURCES))
    {
        Unit::AuraEffectList const& auraEffects = _player->GetAuraEffectsByType(SPELL_AURA_TRACK_RESOURCES);
        if (!auraEffects.empty())
        {
            // 构建要取消的法术ID列表。尝试在迭代AuraEffectList时取消光环会导致第二次遍历时出现"不兼容迭代器"错误
            std::list<uint32> spellIDs;

            for (Unit::AuraEffectList::const_iterator auraEffect = auraEffects.begin(); auraEffect != auraEffects.end(); ++auraEffect)
                spellIDs.push_back((*auraEffect)->GetId());

            // 移除所有与资源追踪相关的光环（3.3.5a中只有草药和矿物）
            for (std::list<uint32>::iterator it = spellIDs.begin(); it != spellIDs.end(); ++it)
                _player->RemoveOwnedAura(*it, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
        }
    }
}

/**
 * @brief 处理宠物取消光环操作码
 *
 * @职责:
 *   处理玩家取消宠物身上光环效果的网络请求。
 *
 * @参数:
 *   packet - 宠物取消光环数据包，包含宠物GUID和法术ID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 获取法术信息并验证有效性
 *   2. 获取宠物对象（生物、宠物或载具）
 *   3. 验证宠物是否属于该玩家
 *   4. 检查宠物是否存活
 *   5. 移除宠物身上的指定光环
 */
void WorldSession::HandlePetCancelAuraOpcode(WorldPackets::Spells::PetCancelAura& packet)
{
    // 获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(packet.SpellID);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown PET spell id {}", packet.SpellID);
        return;
    }

    // 获取宠物对象
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.PetGUID);

    if (!pet)
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: Attempt to cancel an aura for non-existant {} by player '{}'", packet.PetGUID.ToString(), GetPlayer()->GetName());
        return;
    }

    // 验证宠物是否属于该玩家
    if (pet != GetPlayer()->GetGuardianPet() && pet != GetPlayer()->GetCharmed())
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: {} is not a pet of player '{}'", packet.PetGUID.ToString(), GetPlayer()->GetName());
        return;
    }

    // 检查宠物是否存活
    if (!pet->IsAlive())
    {
        pet->SendPetActionFeedback(FEEDBACK_PET_DEAD);
        return;
    }

    // 移除光环
    pet->RemoveOwnedAura(packet.SpellID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

/**
 * @brief 处理取消成长光环操作码
 *
 * @职责:
 *   处理玩家取消身上所有成长（体型变化）效果的网络请求。
 *
 * @参数:
 *   cancelGrowthAura - 取消成长光环数据包（未使用）
 *
 * @返回值: 无
 *
 * @主要流程:
 *   移除所有符合条件的体型变化光环（可取消、正面效果、非被动）
 */
void WorldSession::HandleCancelGrowthAuraOpcode(WorldPackets::Spells::CancelGrowthAura& /*cancelGrowthAura*/)
{
    _player->RemoveAurasByType(SPELL_AURA_MOD_SCALE, [](AuraApplication const* aurApp)
    {
        SpellInfo const* spellInfo = aurApp->GetBase()->GetSpellInfo();
        return !spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL) && spellInfo->IsPositive() && !spellInfo->IsPassive();
    });
}

/**
 * @brief 处理取消坐骑光环操作码
 *
 * @职责:
 *   处理玩家取消坐骑状态的网络请求。
 *
 * @参数:
 *   cancelMountAura - 取消坐骑光环数据包（未使用）
 *
 * @返回值: 无
 *
 * @主要流程:
 *   移除所有符合条件的坐骑光环（可取消、正面效果、非被动）
 */
void WorldSession::HandleCancelMountAuraOpcode(WorldPackets::Spells::CancelMountAura& /*cancelMountAura*/)
{
    _player->RemoveAurasByType(SPELL_AURA_MOUNTED, [](AuraApplication const* aurApp)
    {
        SpellInfo const* spellInfo = aurApp->GetBase()->GetSpellInfo();
        return !spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL) && spellInfo->IsPositive() && !spellInfo->IsPassive();
    });
}

/**
 * @brief 处理取消自动重复施法操作码
 *
 * @职责:
 *   处理玩家取消自动射击状态的网络请求。
 *
 * @参数:
 *   cancelAutoRepeatSpell - 取消自动重复施法数据包（未使用）
 *
 * @返回值: 无
 *
 * @主要流程:
 *   打断当前自动重复施法
 */
void WorldSession::HandleCancelAutoRepeatSpellOpcode(WorldPackets::Spells::CancelAutoRepeatSpell& /*cancelAutoRepeatSpell*/)
{
    // 可能更好的方法是发送SMSG_CANCEL_AUTO_REPEAT？
    // 取消并准备删除
    _player->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
}

/**
 * @brief 处理取消引导法术操作码
 *
 * @职责:
 *   处理玩家取消正在引导的法术的网络请求。
 *
 * @参数:
 *   cancelChanneling - 取消引导数据包，包含引导法术ID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 获取当前活动移动单位
 *   2. 获取法术信息并验证有效性
 *   3. 检查法术是否允许被取消
 *   4. 验证当前正在引导的法术是否匹配
 *   5. 打断引导法术
 */
void WorldSession::HandleCancelChanneling(WorldPackets::Spells::CancelChannelling& cancelChanneling)
{
    // 获取当前活动移动单位
    Unit* mover = GetGameClient()->GetActivelyMovedUnit();

    // 忽略被控制状态下的请求（玩家情况）
    if (!mover)
        return;

    // 获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cancelChanneling.ChannelSpell);
    if (!spellInfo)
        return;

    // 不允许移除具有SPELL_ATTR0_CANT_CANCEL属性的法术
    if (spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL))
        return;

    // 获取当前引导法术并验证
    Spell* spell = mover->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!spell || spell->GetSpellInfo()->Id != spellInfo->Id)
        return;

    // 打断引导法术
    mover->InterruptSpell(CURRENT_CHANNELED_SPELL);
}

/**
 * @brief 处理图腾销毁操作码
 *
 * @职责:
 *   处理玩家主动销毁图腾的网络请求。
 *
 * @参数:
 *   totemDestroyed - 图腾销毁数据包，包含图腾槽位信息
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 忽略被控制状态下的请求
 *   2. 计算图腾槽位ID
 *   3. 验证槽位有效性
 *   4. 获取图腾对象
 *   5. 召回图腾
 */
void WorldSession::HandleTotemDestroyed(WorldPackets::Totem::TotemDestroyed& totemDestroyed)
{
    // 忽略被控制状态下的请求
    if (_player->IsCharmed())
        return;

    // 计算图腾槽位ID
    uint8 slotId = totemDestroyed.Slot;
    slotId += SUMMON_SLOT_TOTEM_FIRE;

    // 验证槽位有效性
    if (slotId >= MAX_TOTEM_SLOT)
        return;

    // 检查该槽位是否有图腾
    if (!_player->m_SummonSlot[slotId])
        return;

    // 获取图腾对象并召回
    Creature* totem = ObjectAccessor::GetCreature(*_player, _player->m_SummonSlot[slotId]);
    if (totem && totem->IsTotem())
        totem->ToTotem()->UnSummon();
}

/**
 * @brief 处理自我复活操作码
 *
 * @职责:
 *   处理玩家使用自我复活能力（如术士的灵魂石、萨满的复生）的网络请求。
 *
 * @参数:
 *   recvData - 接收到的网络数据包（空数据包）
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 获取玩家存储的自我复活法术ID
 *   2. 检查是否有阻止复活的光环
 *   3. 施放自我复活法术
 *   4. 清除自我复活法术ID
 */
void WorldSession::HandleSelfResOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_SELF_RES");                  // 空操作码

    // 获取自我复活法术
    if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(_player->GetUInt32Value(PLAYER_SELF_RES_SPELL)))
    {
        // 检查是否有阻止复活的光环
        if (_player->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION) && !spell->HasAttribute(SPELL_ATTR7_BYPASS_NO_RESURRECT_AURA))
            return; // 静默返回，客户端应自行显示错误并不发送此操作码

        // 施放复活法术
        _player->CastSpell(_player, spell->Id);
        _player->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);
    }
}

/**
 * @brief 处理法术点击操作码
 *
 * @职责:
 *   处理玩家点击NPC触发法术的网络请求。
 *   某些NPC（如载具、特殊交互对象）支持通过点击触发法术。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含NPC的GUID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取NPC的GUID
 *   2. 获取生物对象（NPC、宠物或载具）
 *   3. 验证生物是否存在且在世界中
 *   4. 处理法术点击事件
 */
void WorldSession::HandleSpellClick(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // 获取生物对象，可能不在世界中，会导致崩溃
    Creature* unit = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!unit)
        return;

    /// @todo Unit::SetCharmedBy: 28782不在世界中但0试图控制它！-> 崩溃
    if (!unit->IsInWorld())
        return;

    // 处理法术点击事件
    unit->HandleSpellClick(_player);
}

/**
 * @brief 处理镜像图像数据请求操作码
 *
 * @职责:
 *   处理客户端请求镜像图像数据的网络请求。
 *   镜像图像是法师的镜像技能等复制出的单位，需要获取原始施法者的外观数据。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含镜像图像的GUID
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取镜像图像的GUID
 *   2. 获取镜像图像单位
 *   3. 检查单位是否有克隆施法者光环
 *   4. 获取原始施法者（克隆来源）
 *   5. 构建镜像图像数据包，包括显示ID、种族、性别、职业等
 *   6. 如果原始施法者是玩家，发送玩家外观和装备数据
 *   7. 如果原始施法者是生物，发送默认数据
 *   8. 发送数据包给客户端
 */
void WorldSession::HandleMirrorImageDataRequest(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GET_MIRRORIMAGE_DATA");
    ObjectGuid guid;
    recvData >> guid;

    // 获取客户端需要数据的单位
    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);
    if (!unit)
        return;

    // 检查单位是否有克隆施法者光环
    if (!unit->HasAuraType(SPELL_AURA_CLONE_CASTER))
        return;

    // 获取单位的创建者（SPELL_AURA_CLONE_CASTER不叠加）
    Unit* creator = unit->GetAuraEffectsByType(SPELL_AURA_CLONE_CASTER).front()->GetCaster();
    if (!creator)
        return;

    // 构建镜像图像数据包
    WorldPacket data(SMSG_MIRRORIMAGE_DATA, 68);
    data << uint64(guid);
    data << uint32(creator->GetDisplayId());
    data << uint8(creator->GetRace());
    data << uint8(creator->GetGender());
    data << uint8(creator->GetClass());

    // 如果创建者是玩家，发送玩家外观和装备数据
    if (Player* player = creator->ToPlayer())
    {
        data << uint8(player->GetSkinId());
        data << uint8(player->GetFaceId());
        data << uint8(player->GetHairStyleId());
        data << uint8(player->GetHairColorId());
        data << uint8(player->GetFacialStyle());
        data << uint32(player->GetGuildId());

        // 可见装备槽位列表
        static EquipmentSlots const itemSlots[] =
        {
            EQUIPMENT_SLOT_HEAD,
            EQUIPMENT_SLOT_SHOULDERS,
            EQUIPMENT_SLOT_BODY,
            EQUIPMENT_SLOT_CHEST,
            EQUIPMENT_SLOT_WAIST,
            EQUIPMENT_SLOT_LEGS,
            EQUIPMENT_SLOT_FEET,
            EQUIPMENT_SLOT_WRISTS,
            EQUIPMENT_SLOT_HANDS,
            EQUIPMENT_SLOT_BACK,
            EQUIPMENT_SLOT_TABARD,
            EQUIPMENT_SLOT_END
        };

        // 显示可见槽位中的装备
        for (EquipmentSlots const* itr = &itemSlots[0]; *itr != EQUIPMENT_SLOT_END; ++itr)
        {
            // 如果玩家隐藏头盔或披风，发送0
            if (*itr == EQUIPMENT_SLOT_HEAD && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
                data << uint32(0);
            else if (*itr == EQUIPMENT_SLOT_BACK && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
                data << uint32(0);
            else if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, *itr))
                data << uint32(item->GetTemplate()->DisplayInfoID);
            else
                data << uint32(0);
        }
    }
    else
    {
        // 生物跳过玩家数据
        data << uint8(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
    }

    SendPacket(&data);
}

/**
 * @brief 处理更新投射物位置操作码
 *
 * @职责:
 *   处理客户端更新投射物（如飞刀、箭矢等）命中位置的网络请求。
 *   某些法术需要根据客户端计算的实际命中位置更新服务端的目标位置。
 *
 * @参数:
 *   recvPacket - 接收到的网络数据包，包含施法者GUID、法术ID、施法计数和命中位置
 *
 * @返回值: 无
 *
 * @主要流程:
 *   1. 从数据包中读取施法者GUID、法术ID、施法计数和命中位置坐标
 *   2. 获取施法者单位
 *   3. 查找当前正在施放的法术
 *   4. 更新法术目标位置
 *   5. 重新计算飞行时间
 *   6. 向周围广播投射物位置更新消息
 */
void WorldSession::HandleUpdateProjectilePosition(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_UPDATE_PROJECTILE_POSITION");

    ObjectGuid casterGuid;
    uint32 spellId;
    uint8 castCount;
    float x, y, z;    // 导弹命中位置

    // 读取数据包内容
    recvPacket >> casterGuid;
    recvPacket >> spellId;
    recvPacket >> castCount;
    recvPacket >> x;
    recvPacket >> y;
    recvPacket >> z;

    // 获取施法者
    Unit* caster = ObjectAccessor::GetUnit(*_player, casterGuid);
    if (!caster)
        return;

    // 查找当前施放的法术
    Spell* spell = caster->FindCurrentSpellBySpellId(spellId);
    if (!spell || !spell->m_targets.HasDst())
        return;

    // 更新目标位置
    Position pos = *spell->m_targets.GetDstPos();
    pos.Relocate(x, y, z);
    spell->m_targets.ModDst(pos);

    // 目标已更改，重新计算飞行时间
    spell->RecalculateDelayMomentForDst();

    // 构建并发送投射物位置更新消息
    WorldPacket data(SMSG_SET_PROJECTILE_POSITION, 21);
    data << uint64(casterGuid);
    data << uint8(castCount);
    data << float(x);
    data << float(y);
    data << float(z);
    caster->SendMessageToSet(&data, true);
}
