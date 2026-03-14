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
 * @file NPCHandler.cpp
 * @brief NPC交互处理模块实现文件
 *
 * @职责 处理玩家与各种NPC的交互操作,包括:
 *       - 训练师交互(技能学习、技能列表查询)
 *       - 商人交互(购买、出售、修理装备)
 *       - 管理员交互(公会徽章设计、银行、邮箱)
 *       - 宠物管理员交互(宠物管理、宠物栏操作)
 *       - 旅店老板交互(设置炉石绑定)
 *       - 灵魂医者交互(灵魂复活)
 *       - 任务NPC交互(对话、任务信息)
 *
 * @调用时机 当玩家与NPC交互时,客户端发送相应的操作码,服务器根据NPC类型分发到对应的处理函数
 *
 * @性能注意事项
 *       - 稳定宠物操作涉及数据库事务,需要合理处理
 *       - 装备修理计算涉及大量物品耐久度检查
 *       - NPC对话系统需要缓存机制避免频繁数据库查询
 */

#include "WorldSession.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Common.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "Item.h"
#include "Log.h"
#include "MailPackets.h"
#include "Map.h"
#include "NPCPackets.h"
#include "Opcodes.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "QueryCallback.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @enum StableResultCode
 * @brief 宠物栏操作结果代码
 *
 * @职责 定义宠物栏操作(寄养、取出、购买栏位等)的返回结果代码
 *       客户端根据这些代码显示对应的错误提示或成功消息
 */
enum StableResultCode
{
    STABLE_ERR_MONEY        = 0x01,   ///< 错误:金钱不足 (you don't have enough money)
    STABLE_ERR_STABLE       = 0x06,   ///< 错误:宠物栏操作失败 (通用错误代码,大多数失败情况使用)
    STABLE_SUCCESS_STABLE   = 0x08,   ///< 成功:宠物寄养成功
    STABLE_SUCCESS_UNSTABLE = 0x09,   ///< 成功:宠物取出或交换成功
    STABLE_SUCCESS_BUY_SLOT = 0x0A,   ///< 成功:购买宠物栏位成功
    STABLE_ERR_EXOTIC       = 0x0C    ///< 错误:无法控制奇异宠物 (you are unable to control exotic creatures)
};

/**
 * @brief 处理公会徽章设计师激活操作码
 *
 * @职责 处理玩家与公会徽章设计师NPC的交互
 *       验证NPC有效性并发送徽章设计界面给客户端
 *
 * @param recvData 接收到的网络包数据
 *        - guid: NPC的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 读取NPC的GUID
 *   2. 验证玩家是否可以与该NPC交互(NPC类型检查)
 *   3. 移除玩家的假死状态
 *   4. 发送徽章设计界面给客户端
 */
void WorldSession::HandleTabardVendorActivateOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_TABARDDESIGNER标志)
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_TABARDDESIGNER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTabardVendorActivateOpcode - {} not found or you can not interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 发送徽章设计界面
    SendTabardVendorActivate(guid);
}

/**
 * @brief 发送公会徽章设计师激活消息
 *
 * @职责 向客户端发送公会徽章设计界面打开消息
 *
 * @param guid NPC的GUID
 *
 * @返回值 无
 */
void WorldSession::SendTabardVendorActivate(ObjectGuid guid)
{
    WorldPacket data(MSG_TABARDVENDOR_ACTIVATE, 8);
    data << guid;
    SendPacket(&data);
}

/**
 * @brief 发送邮箱界面
 *
 * @职责 向客户端发送邮箱界面打开消息
 *       用于邮递员NPC或邮箱交互
 *
 * @param guid 邮递员NPC或邮箱的GUID
 *
 * @返回值 无
 */
void WorldSession::SendShowMailBox(ObjectGuid guid)
{
    WorldPackets::Mail::ShowMailbox packet;
    packet.PostmasterGUID = guid;
    SendPacket(packet.Write());
}

/**
 * @brief 处理训练师列表查询操作码
 *
 * @职责 处理玩家与训练师NPC的交互请求
 *       验证NPC有效性并发送可学习的技能列表给客户端
 *
 * @param packet 接收到的网络包数据
 *        - Unit: NPC的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 验证NPC是否存在且可以交互
 *   2. 调用SendTrainerList发送技能列表
 */
void WorldSession::HandleTrainerListOpcode(WorldPackets::NPC::Hello& packet)
{
    // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_TRAINER标志)
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(packet.Unit, UNIT_NPC_FLAG_TRAINER);
    if (!npc)
    {
        TC_LOG_DEBUG("network", "WorldSession: SendTrainerList - {} not found or you can not interact with him.", packet.Unit.ToString());
        return;
    }

    SendTrainerList(npc);
}

/**
 * @brief 发送训练师技能列表
 *
 * @职责 向客户端发送训练师可教授的技能列表
 *       包含技能名称、费用、等级要求、技能点要求等信息
 *
 * @param npc 训练师NPC指针
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 移除玩家的假死状态
 *   2. 从对象管理器获取训练师数据
 *   3. 验证训练师是否对玩家有效(职业、阵营等条件)
 *   4. 发送技能列表给客户端
 */
void WorldSession::SendTrainerList(Creature* npc)
{
    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 获取训练师数据
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
    if (!trainer)
    {
        TC_LOG_DEBUG("network", "WorldSession: SendTrainerList - trainer spells not found for {}", npc->GetGUID().ToString());
        return;
    }

    // 验证训练师是否对玩家有效(检查职业、阵营等条件)
    if (!trainer->IsTrainerValidForPlayer(_player))
    {
        TC_LOG_DEBUG("network", "WorldSession: SendTrainerList - trainer {} not valid for player {}", npc->GetGUID().ToString(), GetPlayerInfo());
        return;
    }

    // 发送技能列表(包含本地化信息)
    trainer->SendSpells(npc, _player, GetSessionDbLocaleIndex());
}

/**
 * @brief 处理训练师购买技能操作码
 *
 * @职责 处理玩家向训练师购买/学习技能的请求
 *       验证条件并教授技能给玩家
 *
 * @param packet 接收到的网络包数据
 *        - TrainerGUID: 训练师NPC的GUID
 *        - SpellID: 要学习的技能ID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 验证训练师NPC有效性
 *   2. 获取训练师数据
 *   3. 调用TeachSpell教授技能(包含金钱检查、技能点检查、前置技能检查等)
 */
void WorldSession::HandleTrainerBuySpellOpcode(WorldPackets::NPC::TrainerBuySpell& packet)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_TRAINER_BUY_SPELL {}, learn spell id is: {}", packet.TrainerGUID.ToString(), packet.SpellID);

    // 验证NPC是否存在且玩家可以与之交互
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(packet.TrainerGUID, UNIT_NPC_FLAG_TRAINER);
    if (!npc)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTrainerBuySpellOpcode - {} not found or you can not interact with him.", packet.TrainerGUID.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 获取训练师数据
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
    if (!trainer)
        return;

    // 教授技能(包含所有条件检查和金钱扣除)
    trainer->TeachSpell(npc, _player, packet.SpellID);
}

/**
 * @brief 处理NPC对话Hello操作码
 *
 * @职责 处理玩家与NPC对话的开始交互
 *       显示NPC的对话菜单或执行特殊NPC逻辑
 *
 * @param recvData 接收到的网络包数据
 *        - guid: NPC的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 验证NPC是否可以交互
 *   2. 设置阵营可见性
 *   3. 移除会打断对话的光环
 *   4. 如果NPC在移动,暂停其移动
 *   5. 特殊处理:战场灵魂医者直接加入复活队列
 *   6. 触发脚本事件OnGossipHello
 *   7. 如果脚本未处理,显示默认对话菜单
 */
void WorldSession::HandleGossipHelloOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_GOSSIP_HELLO");

    ObjectGuid guid;
    recvData >> guid;

    // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_GOSSIP标志)
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_GOSSIP);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleGossipHelloOpcode - {} not found or you can not interact with him.", guid.ToString());
        return;
    }

    // 如果需要,设置阵营可见性(首次接触该阵营时)
    if (FactionTemplateEntry const* factionTemplateEntry = sFactionTemplateStore.LookupEntry(unit->GetFaction()))
        _player->GetReputationMgr().SetVisible(factionTemplateEntry);

    // 移除会打断对话的光环
    GetPlayer()->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TALK);
    // 移除假死状态 (已注释,似乎不需要)
    //if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
    //    GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 如果NPC在移动,暂停其移动
    if (uint32 pause = unit->GetMovementTemplate().GetInteractionPauseTimer())
        unit->PauseMovement(pause);
    // 设置NPC的初始位置(防止交互后NPC走回原位)
    unit->SetHomePosition(unit->GetPosition());

    // 特殊处理:如果是战场灵魂医者,不需要对话菜单,直接将玩家加入复活队列
    if (unit->IsSpiritGuide())
    {
        Battleground* bg = _player->GetBattleground();
        if (bg)
        {
            bg->AddPlayerToResurrectQueue(unit->GetGUID(), _player->GetGUID());
            sBattlegroundMgr->SendAreaSpiritHealerQueryOpcode(_player, bg, unit->GetGUID());
            return;
        }
    }

    // 清空玩家的对话菜单
    _player->PlayerTalkClass->ClearMenus();

    // 触发脚本的OnGossipHello事件
    // 如果脚本返回true,表示脚本已处理,不再显示默认菜单
    if (!unit->AI()->OnGossipHello(_player))
    {
        // 脚本未处理,准备并发送默认对话菜单
//        _player->TalkedToCreature(unit->GetEntry(), unit->GetGUID());
        _player->PrepareGossipMenu(unit, unit->GetCreatureTemplate()->GossipMenuId, true);
        _player->SendPreparedGossip(unit);
    }
}

/**
 * @brief 处理灵魂医者激活操作码
 *
 * @职责 处理玩家(灵魂状态)与灵魂医者的交互
 *       允许玩家在灵魂状态下复活
 *
 * @param recvData 接收到的网络包数据
 *        - guid: 灵魂医者NPC的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 验证NPC是否为灵魂医者
 *   2. 移除假死状态
 *   3. 执行复活逻辑
 */
void WorldSession::HandleSpiritHealerActivateOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_SPIRIT_HEALER_ACTIVATE");

    ObjectGuid guid;
    recvData >> guid;

    // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_SPIRITHEALER标志)
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_SPIRITHEALER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleSpiritHealerActivateOpcode - {} not found or you can not interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 执行灵魂复活
    SendSpiritResurrect();
}

/**
 * @brief 执行灵魂复活
 *
 * @职责 处理玩家在灵魂医者处复活的实际逻辑
 *       包括复活玩家、扣除耐久度、传送至墓地等
 *
 * @参数 无(使用玩家当前状态)
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 复活玩家(恢复50%生命值)
 *   2. 对所有装备造成25%耐久度损失
 *   3. 生成尸骨(尸体消失)
 *   4. 如果尸体所在墓地与灵魂所在墓地不同,传送至尸体所在墓地
 */
void WorldSession::SendSpiritResurrect()
{
    // 复活玩家,恢复50%生命值,使用SFX效果
    _player->ResurrectPlayer(0.5f, true);

    // 所有装备耐久度损失25%
    _player->DurabilityLossAll(0.25f, true);

    // 获取尸体最近的墓地
    WorldSafeLocsEntry const* corpseGrave = nullptr;
    if (_player->HasCorpse())
    {
        WorldLocation const& corpseLocation = _player->GetCorpseLocation();
        corpseGrave = sObjectMgr->GetClosestGraveyard(corpseLocation.GetPositionX(), corpseLocation.GetPositionY(),
            corpseLocation.GetPositionZ(), corpseLocation.GetMapId(), _player->GetTeam());
    }

    // 生成尸骨,尸体消失
    _player->SpawnCorpseBones();

    // 如果尸体最近的墓地与玩家灵魂最近的墓地不同,传送至尸体所在墓地
    // 这样可以避免玩家复活在远离尸体的地方
    if (corpseGrave)
    {
        WorldSafeLocsEntry const* ghostGrave = sObjectMgr->GetClosestGraveyard(
            _player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), _player->GetMapId(), _player->GetTeam());

        if (corpseGrave != ghostGrave)
            _player->TeleportTo(corpseGrave->Continent, corpseGrave->Loc.X, corpseGrave->Loc.Y, corpseGrave->Loc.Z, _player->GetOrientation());
    }
}

/**
 * @brief 处理旅店老板激活操作码
 *
 * @职责 处理玩家与旅店老板的交互
 *       将玩家的炉石绑定点设置到当前位置
 *
 * @param recvData 接收到的网络包数据
 *        - npcGUID: 旅店老板NPC的GUID
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 检查玩家是否在世界中且存活
 *   2. 验证NPC是否为旅店老板
 *   3. 移除假死状态
 *   4. 设置绑定点
 */
void WorldSession::HandleBinderActivateOpcode(WorldPacket& recvData)
{
    ObjectGuid npcGUID;
    recvData >> npcGUID;

    // 玩家必须在世界中且存活
    if (!GetPlayer()->IsInWorld() || !GetPlayer()->IsAlive())
        return;

    // 验证NPC是否存在且玩家可以与之交互(NPC必须有UNIT_NPC_FLAG_INNKEEPER标志)
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_INNKEEPER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleBinderActivateOpcode - {} not found or you can not interact with him.", npcGUID.ToString());
        return;
    }

    // 移除假死状态
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 设置绑定点
    SendBindPoint(unit);
}

/**
 * @brief 发送绑定点设置消息
 *
 * @职责 实际执行炉石绑定点的设置
 *       施放绑定法术并通知客户端
 *
 * @param npc 旅店老板NPC指针
 *
 * @返回值 无
 *
 * @主要流程
 *   1. 检查是否在副本中(副本不允许设置绑定点)
 *   2. 施放绑定法术(3286)
 *   3. 发送成功消息给客户端
 *   4. 关闭对话菜单
 */
void WorldSession::SendBindPoint(Creature* npc)
{
    // 防止在副本中设置炉石绑定点
    if (GetPlayer()->GetMap()->Instanceable())
        return;

    // 绑定法术ID: 3286 (Bind)
    uint32 bindspell = 3286;

    // 施放绑定法术
    npc->CastSpell(_player, bindspell, true);

    // 发送法术学习成功消息给客户端
    WorldPacket data(SMSG_TRAINER_BUY_SUCCEEDED, (8+4));
    data << uint64(npc->GetGUID());
    data << uint32(bindspell);
    SendPacket(&data);

    // 关闭对话菜单
    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleRequestStabledPets(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv MSG_LIST_STABLED_PETS");
    ObjectGuid npcGUID;

    recvData >> npcGUID;

    if (!CheckStableMaster(npcGUID))
        return;

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // remove mounts this fix bug where getting pet from stable while mounted deletes pet.
    if (GetPlayer()->IsMounted())
        GetPlayer()->RemoveAurasByType(SPELL_AURA_MOUNTED);

    SendStablePet(npcGUID);
}

void WorldSession::SendStablePet(ObjectGuid guid)
{
    TC_LOG_DEBUG("network", "WORLD: Recv MSG_LIST_STABLED_PETS Send.");

    WorldPacket data(MSG_LIST_STABLED_PETS, 200);           // guess size

    data << uint64(guid);

    size_t wpos = data.wpos();
    data << uint8(0);                                       // place holder for slot show number

    PetStable* petStable = GetPlayer()->GetPetStable();
    if (!petStable)
    {
        data << uint8(0);                                   // stable slots
        SendPacket(&data);
        return;
    }

    data << uint8(petStable->MaxStabledPets);

    uint8 num = 0;                                          // counter for place holder

    if (petStable->CurrentPet)
    {
        PetStable::PetInfo const& pet = *petStable->CurrentPet;
        data << uint32(pet.PetNumber);
        data << uint32(pet.CreatureId);
        data << uint32(pet.Level);
        data << pet.Name;                                   // petname
        data << uint8(1);                                   // flags: 1 active, 2 inactive
        ++num;
    }
    else
    {
        if (PetStable::PetInfo const* pet = petStable->GetUnslottedHunterPet())
        {
            data << uint32(pet->PetNumber);
            data << uint32(pet->CreatureId);
            data << uint32(pet->Level);
            data << pet->Name;                                   // petname
            data << uint8(1);                                   // flags: 1 active, 2 inactive
            ++num;
        }
    }

    for (Optional<PetStable::PetInfo> const& stabledSlot : petStable->StabledPets)
    {
        if (stabledSlot)
        {
            PetStable::PetInfo const& pet = *stabledSlot;
            data << uint32(pet.PetNumber);
            data << uint32(pet.CreatureId);
            data << uint32(pet.Level);
            data << pet.Name;                               // petname
            data << uint8(2);                               // flags: 1 active, 2 inactive
            ++num;
        }
    }

    data.put<uint8>(wpos, num);                             // set real data to placeholder
    SendPacket(&data);
}

void WorldSession::SendPetStableResult(uint8 res)
{
    WorldPacket data(SMSG_STABLE_RESULT, 1);
    data << uint8(res);
    SendPacket(&data);
}

void WorldSession::HandleStablePet(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv CMSG_STABLE_PET");
    ObjectGuid npcGUID;

    recvData >> npcGUID;

    if (!GetPlayer()->IsAlive())
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    if (!CheckStableMaster(npcGUID))
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    PetStable* petStable = GetPlayer()->GetPetStable();
    if (!petStable)
        return;

    Pet* pet = _player->GetPet();

    // can't place in stable dead pet
    if ((pet && (!pet->IsAlive() || pet->getPetType() != HUNTER_PET))
        || (!pet && (petStable->UnslottedPets.size() != 1 || !petStable->UnslottedPets[0].Health || petStable->UnslottedPets[0].Type != HUNTER_PET)))
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    for (uint32 freeSlot = 0; freeSlot < petStable->MaxStabledPets; ++freeSlot)
    {
        if (!petStable->StabledPets[freeSlot])
        {
            if (pet)
            {
                // stable summoned pet
                _player->RemovePet(pet, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + freeSlot));
                std::swap(petStable->StabledPets[freeSlot], petStable->CurrentPet);
                SendPetStableResult(STABLE_SUCCESS_STABLE);
                return;
            }

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
            stmt->setUInt8(0, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + freeSlot));
            stmt->setUInt32(1, _player->GetGUID().GetCounter());
            stmt->setUInt32(2, petStable->UnslottedPets[0].PetNumber);
            CharacterDatabase.Execute(stmt);

            // stable unsummoned pet
            petStable->StabledPets[freeSlot] = std::move(petStable->UnslottedPets.back());
            petStable->UnslottedPets.pop_back();
            SendPetStableResult(STABLE_SUCCESS_STABLE);
            return;
        }
    }

    // not free stable slot
    SendPetStableResult(STABLE_ERR_STABLE);
}

void WorldSession::HandleUnstablePet(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv CMSG_UNSTABLE_PET.");
    ObjectGuid npcGUID;
    uint32 petnumber;

    recvData >> npcGUID >> petnumber;

    if (!CheckStableMaster(npcGUID))
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    PetStable* petStable = GetPlayer()->GetPetStable();
    if (!petStable)
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    auto stabledPet = std::find_if(petStable->StabledPets.begin(), petStable->StabledPets.end(), [petnumber](Optional<PetStable::PetInfo> const& pet)
    {
        return pet && pet->PetNumber == petnumber;
    });

    if (stabledPet == petStable->StabledPets.end())
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate((*stabledPet)->CreatureId);
    if (!creatureInfo || !creatureInfo->IsTameable(_player->CanTameExoticPets()))
    {
        // if problem in exotic pet
        if (creatureInfo && creatureInfo->IsTameable(true))
            SendPetStableResult(STABLE_ERR_EXOTIC);
        else
            SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    Pet* oldPet = _player->GetPet();
    if (oldPet)
    {
        // try performing a swap, client sends this packet instead of swap when starting from stabled slot
        if (!oldPet->IsAlive() || !oldPet->IsHunterPet())
        {
            SendPetStableResult(STABLE_ERR_STABLE);
            return;
        }

        _player->RemovePet(oldPet, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + std::distance(petStable->StabledPets.begin(), stabledPet)));
    }
    else if (petStable->UnslottedPets.size() == 1)
    {
        if (petStable->CurrentPet || !petStable->UnslottedPets[0].Health || petStable->UnslottedPets[0].Type != HUNTER_PET)
        {
            SendPetStableResult(STABLE_ERR_STABLE);
            return;
        }

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + std::distance(petStable->StabledPets.begin(), stabledPet)));
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petStable->UnslottedPets[0].PetNumber);
        CharacterDatabase.Execute(stmt);

        // move unsummoned pet into CurrentPet slot so that it gets moved into stable slot later
        petStable->CurrentPet = std::move(petStable->UnslottedPets.back());
        petStable->UnslottedPets.pop_back();
    }
    else if (petStable->CurrentPet || !petStable->UnslottedPets.empty())
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    Pet* newPet = new Pet(_player, HUNTER_PET);
    if (!newPet->LoadPetFromDB(_player, 0, petnumber, false))
    {
        delete newPet;

        petStable->UnslottedPets.push_back(std::move(*petStable->CurrentPet));
        petStable->CurrentPet.reset();

        // update current pet slot in db immediately to maintain slot consistency, dismissed pet was already saved
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PET_SAVE_NOT_IN_SLOT);
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petnumber);
        CharacterDatabase.Execute(stmt);

        SendPetStableResult(STABLE_ERR_STABLE);
    }
    else
    {
        // update current pet slot in db immediately to maintain slot consistency, dismissed pet was already saved
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PET_SAVE_AS_CURRENT);
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petnumber);
        CharacterDatabase.Execute(stmt);

        SendPetStableResult(STABLE_SUCCESS_UNSTABLE);
    }
}

void WorldSession::HandleBuyStableSlot(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv CMSG_BUY_STABLE_SLOT.");
    ObjectGuid npcGUID;

    recvData >> npcGUID;

    if (!CheckStableMaster(npcGUID))
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    PetStable& petStable = GetPlayer()->GetOrInitPetStable();
    if (petStable.MaxStabledPets < MAX_PET_STABLES)
    {
        StableSlotPricesEntry const* SlotPrice = sStableSlotPricesStore.LookupEntry(petStable.MaxStabledPets + 1);
        if (_player->HasEnoughMoney(SlotPrice->Cost))
        {
            ++petStable.MaxStabledPets;
            _player->ModifyMoney(-int32(SlotPrice->Cost));
            SendPetStableResult(STABLE_SUCCESS_BUY_SLOT);
        }
        else
            SendPetStableResult(STABLE_ERR_MONEY);
    }
    else
        SendPetStableResult(STABLE_ERR_STABLE);
}

void WorldSession::HandleStableRevivePet(WorldPacket &/* recvData */)
{
    TC_LOG_DEBUG("network", "HandleStableRevivePet: Not implemented");
}

void WorldSession::HandleStableSwapPet(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv CMSG_STABLE_SWAP_PET.");
    ObjectGuid npcGUID;
    uint32 petId;

    recvData >> npcGUID >> petId;

    if (!CheckStableMaster(npcGUID))
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    PetStable* petStable = GetPlayer()->GetPetStable();
    if (!petStable)
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // Find swapped pet slot in stable
    auto stabledPet = std::find_if(petStable->StabledPets.begin(), petStable->StabledPets.end(), [petId](Optional<PetStable::PetInfo> const& pet)
    {
        return pet && pet->PetNumber == petId;
    });

    if (stabledPet == petStable->StabledPets.end())
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate((*stabledPet)->CreatureId);
    if (!creatureInfo || !creatureInfo->IsTameable(_player->CanTameExoticPets()))
    {
        // if problem in exotic pet
        if (creatureInfo && creatureInfo->IsTameable(true))
            SendPetStableResult(STABLE_ERR_EXOTIC);
        else
            SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    Pet* oldPet = _player->GetPet();
    if (oldPet)
    {
        if (!oldPet->IsAlive() || !oldPet->IsHunterPet())
        {
            SendPetStableResult(STABLE_ERR_STABLE);
            return;
        }

        _player->RemovePet(oldPet, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + std::distance(petStable->StabledPets.begin(), stabledPet)));
    }
    else if (petStable->UnslottedPets.size() == 1)
    {
        if (petStable->CurrentPet || !petStable->UnslottedPets[0].Health || petStable->UnslottedPets[0].Type != HUNTER_PET)
        {
            SendPetStableResult(STABLE_ERR_STABLE);
            return;
        }

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + std::distance(petStable->StabledPets.begin(), stabledPet)));
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petStable->UnslottedPets[0].PetNumber);
        CharacterDatabase.Execute(stmt);

        // move unsummoned pet into CurrentPet slot so that it gets moved into stable slot later
        petStable->CurrentPet = std::move(petStable->UnslottedPets.back());
        petStable->UnslottedPets.pop_back();
    }
    else if (petStable->CurrentPet || !petStable->UnslottedPets.empty())
    {
        SendPetStableResult(STABLE_ERR_STABLE);
        return;
    }

    // summon unstabled pet
    Pet* newPet = new Pet(_player, HUNTER_PET);
    if (!newPet->LoadPetFromDB(_player, 0, petId, false))
    {
        delete newPet;
        SendPetStableResult(STABLE_ERR_STABLE);

        petStable->UnslottedPets.push_back(std::move(*petStable->CurrentPet));
        petStable->CurrentPet.reset();

        // update current pet slot in db immediately to maintain slot consistency, dismissed pet was already saved
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PET_SAVE_NOT_IN_SLOT);
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petId);
        CharacterDatabase.Execute(stmt);
    }
    else
    {
        // update current pet slot in db immediately to maintain slot consistency, dismissed pet was already saved
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_SLOT_BY_ID);
        stmt->setUInt8(0, PET_SAVE_AS_CURRENT);
        stmt->setUInt32(1, _player->GetGUID().GetCounter());
        stmt->setUInt32(2, petId);
        CharacterDatabase.Execute(stmt);

        SendPetStableResult(STABLE_SUCCESS_UNSTABLE);
    }
}

void WorldSession::HandleRepairItemOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_REPAIR_ITEM");

    ObjectGuid npcGUID, itemGUID;
    uint8 guildBank;                                        // new in 2.3.2, bool that means from guild bank money

    recvData >> npcGUID >> itemGUID >> guildBank;

    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(npcGUID, UNIT_NPC_FLAG_REPAIR);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleRepairItemOpcode - {} not found or you can not interact with him.", npcGUID.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // reputation discount
    float discountMod = _player->GetReputationPriceDiscount(unit);

    if (itemGUID)
    {
        TC_LOG_DEBUG("network", "ITEM: Repair {}, at {}", itemGUID.ToString(), npcGUID.ToString());

        Item* item = _player->GetItemByGuid(itemGUID);
        if (item)
            _player->DurabilityRepair(item->GetPos(), true, discountMod);
    }
    else
    {
        TC_LOG_DEBUG("network", "ITEM: Repair all items at {}", npcGUID.ToString());
        _player->DurabilityRepairAll(true, discountMod, guildBank != 0);
    }
}
