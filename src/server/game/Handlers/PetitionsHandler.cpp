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
 * @file PetitionsHandler.cpp
 * @brief 公会和竞技场请愿处理模块
 *
 * @details 本模块负责处理玩家创建公会和竞技场队伍的所有请愿相关操作,包括:
 * - 购买请愿书（公会宪章和竞技场宪章）
 * - 签署请愿书
 * - 提交请愿书以创建公会或竞技场队伍
 * - 请愿书的查询、重命名、展示等管理操作
 *
 * 请愿书系统是玩家创建公会和竞技场队伍的核心机制:
 * - 公会宪章: 需要收集指定数量的签名后才能创建公会
 * - 竞技场宪章: 分为2v2、3v3、5v5三种,同样需要签名
 *
 * @note 本模块与 GuildMgr、ArenaTeamMgr、PetitionMgr 等管理器密切协作
 */

#include "WorldSession.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "CharacterCache.h"
#include "Common.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PetitionMgr.h"
#include "WorldPacket.h"
#include "World.h"

/**
 * @brief 宪章物品的显示ID
 * 所有类型的宪章在游戏中使用相同的视觉模型
 */
#define CHARTER_DISPLAY_ID 16161

/**
 * @brief 宪章物品ID枚举
 *
 * @details 定义了数据库 item_template 表中不同类型宪章的物品ID
 * 这些物品是玩家创建公会和竞技场队伍的必需道具
 */
enum CharterItemIDs
{
    GUILD_CHARTER                                 = 5863,   ///< 公会宪章 - 用于创建公会
    ARENA_TEAM_CHARTER_2v2                        = 23560,  ///< 2v2竞技场宪章 - 用于创建2v2竞技场队伍
    ARENA_TEAM_CHARTER_3v3                        = 23561,  ///< 3v3竞技场宪章 - 用于创建3v3竞技场队伍
    ARENA_TEAM_CHARTER_5v5                        = 23562   ///< 5v5竞技场宪章 - 用于创建5v5竞技场队伍
};

/**
 * @brief 处理购买请愿书请求
 *
 * @brief 职责：
 * 处理玩家从NPC处购买公会宪章或竞技场宪章的请求。
 * 验证购买条件，扣除金币，并创建请愿书记录。
 *
 * @param recvData 接收的网络数据包，包含NPC GUID、请愿书名称和类型索引
 *
 * @return 无返回值。成功时给予玩家请愿书物品并创建请愿书记录。
 *
 * @brief 主要流程：
 * 1. 解析数据包获取NPC GUID、请愿书名称和类型索引
 * 2. 验证玩家是否可以与该NPC交互（必须是请愿书NPC）
 * 3. 移除玩家的假死状态
 * 4. 根据NPC类型确定请愿书类型：
 *    - 军旗设计师：公会宪章
 *    - 竞技场管理员：2v2/3v3/5v5竞技场宪章
 * 5. 验证购买条件：
 *    - 玩家不在公会/竞技场队伍中
 *    - 名称未被占用且有效
 *    - 玩家有足够的金币
 *    - 玩家有足够的背包空间
 * 6. 扣除金币并给予请愿书物品
 * 7. 初始化请愿书数据（将GUID写入物品字段）
 * 8. 在请愿书管理器中注册该请愿书
 *
 * @note 请愿书物品使用 ITEM_FIELD_ENCHANTMENT 字段存储GUID和签名数量
 * @note 如果玩家已有同类型的请愿书，旧的将被移除
 */
void WorldSession::HandlePetitionBuyOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_PETITION_BUY");

    // 声明变量
    ObjectGuid guidNPC;                                      // NPC的GUID
    uint32 clientIndex;                                     // 客户端索引：公会为1，竞技场为竞技场槽位+1
    std::string name;                                       // 请愿书名称（公会名或竞技场队名）

    // 解析数据包 - 大部分字段是客户端固定值，被跳过
    recvData >> guidNPC;                                   // NPC GUID
    recvData.read_skip<uint32>();                          // 固定值0
    recvData.read_skip<uint64>();                          // 固定值0
    recvData >> name;                                      // 请愿书名称
    recvData.read_skip<std::string>();                     // some string
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint16>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0
    recvData.read_skip<uint32>();                          // 0

    for (int i = 0; i < 10; ++i)
        recvData.read_skip<std::string>();

    recvData >> clientIndex;                               // index
    recvData.read_skip<uint32>();                          // 0

    TC_LOG_DEBUG("network", "Petitioner {} tried sell petition: name {}", guidNPC.ToString(), name);

    // 防作弊：验证玩家是否可以与该NPC交互
    // 检查NPC是否存在、是否具有请愿书NPC标志、玩家是否在交互范围内
    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guidNPC, UNIT_NPC_FLAG_PETITIONER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandlePetitionBuyOpcode - {} not found or you can't interact with him.", guidNPC.ToString());
        return;
    }

    // 移除玩家的假死状态，允许进行交互
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 声明变量存储请愿书信息
    uint32 charterid = 0;                                    // 宪章物品ID
    uint32 cost = 0;                                         // 宪章购买费用
    CharterTypes type = CHARTER_TYPE_NONE;                   // 宪章类型
    // 根据NPC类型确定请愿书类型
    if (creature->IsTabardDesigner())
    {
        // 军旗设计师NPC - 购买公会宪章
        // 如果玩家已经在公会中，则不允许购买
        if (_player->GetGuildId())
            return;

        charterid = GUILD_CHARTER;
        cost = sWorld->getIntConfig(CONFIG_CHARTER_COST_GUILD);
        type = GUILD_CHARTER_TYPE;
    }
    else
    {
        // 竞技场管理员NPC - 购买竞技场宪章
        // 验证玩家是否达到最高等级（竞技场要求满级）
        /// @todo find correct opcode
        if (!_player->IsMaxLevel())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", _player->GetName(), ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
            return;
        }

        // 根据客户端索引确定竞技场类型（索引 = 竞技场槽位 + 1）
        switch (clientIndex)
        {
            case 1:
                charterid = ARENA_TEAM_CHARTER_2v2;
                cost = sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_2v2);
                type = ARENA_TEAM_CHARTER_2v2_TYPE;
                break;
            case 2:
                charterid = ARENA_TEAM_CHARTER_3v3;
                cost = sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_3v3);
                type = ARENA_TEAM_CHARTER_3v3_TYPE;
                break;
            case 3:
                charterid = ARENA_TEAM_CHARTER_5v5;
                cost = sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_5v5);
                type = ARENA_TEAM_CHARTER_5v5_TYPE;
                break;
            default:
                TC_LOG_DEBUG("network", "unknown selection at buy arena petition: {}", clientIndex);
                return;
        }

        // 验证玩家是否已经在对应槽位的竞技场队伍中
        if (_player->GetArenaTeamId(clientIndex - 1))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ALREADY_IN_ARENA_TEAM);
            return;
        }
    }

    // 验证名称是否有效（区分公会和竞技场）
    if (type == GUILD_CHARTER_TYPE)
    {
        // 公会宪章：验证公会名称是否已存在
        if (sGuildMgr->GetGuildByName(name))
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NAME_EXISTS_S, name);
            return;
        }

        // 验证名称是否为保留名称或格式无效
        if (sObjectMgr->IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NAME_INVALID, name);
            return;
        }
    }
    else
    {
        // 竞技场宪章：验证竞技场队伍名称是否已存在
        if (sArenaTeamMgr->GetArenaTeamByName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            return;
        }
        // 验证名称是否为保留名称或格式无效
        if (sObjectMgr->IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_INVALID);
            return;
        }
    }

    // 获取宪章物品模板
    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(charterid);
    if (!pProto)
    {
        _player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, nullptr, charterid, 0);
        return;
    }

    // 验证玩家是否有足够的金币购买宪章
    if (!_player->HasEnoughMoney(cost))
    {
        _player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, charterid, 0);
        return;
    }

    // 验证玩家背包是否有足够的空间存放宪章
    ItemPosCountVec dest;
    InventoryResult msg = _player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, charterid, pProto->BuyCount);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, nullptr, nullptr, charterid);
        return;
    }

    // 扣除金币
    _player->ModifyMoney(-(int32)cost);
    // 将宪章物品添加到玩家背包
    Item* charter = _player->StoreNewItem(dest, charterid, true);
    if (!charter)
        return;

    // 初始化宪章物品数据
    // 使用物品的附魔字段存储请愿书GUID和签名数量
    charter->SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1, charter->GetGUID().GetCounter());
    // ITEM_FIELD_ENCHANTMENT_1_1 存储公会/竞技场队伍ID
    // ITEM_FIELD_ENCHANTMENT_1_1+1 存储当前签名数量（显示在物品上）
    charter->SetState(ITEM_CHANGED, _player);
    _player->SendNewItem(charter, 1, true, false);

    // 清理无效的旧请愿书
    // 如果玩家已有同类型的请愿书，说明数据损坏（前面已检查玩家不在公会/竞技场中）
    CharacterDatabase.EscapeString(name);
    if (Petition const* petition = sPetitionMgr->GetPetitionByOwnerWithType(_player->GetGUID(), type))
    {
        // 从请愿书存储中移除旧请愿书
        sPetitionMgr->RemovePetition(petition->PetitionGuid);
        TC_LOG_DEBUG("network", "Invalid petition {}", petition->PetitionGuid.ToString());
    }

    // 在请愿书管理器中注册新请愿书
    sPetitionMgr->AddPetition(charter->GetGUID(), _player->GetGUID(), name, type, false);
}

/**
 * @brief 处理展示请愿书签名请求
 *
 * @brief 职责：
 * 处理玩家请求查看请愿书签名的消息。
 * 向玩家发送请愿书的签名列表，包括已签名的玩家信息。
 *
 * @param recvData 接收的网络数据包，包含请愿书GUID
 *
 * @return 无返回值。向玩家发送签名列表数据包。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书GUID
 * 2. 从请愿书管理器获取请愿书数据
 * 3. 验证请愿书是否存在
 * 4. 如果是公会请愿书，验证玩家是否已在公会中
 * 5. 发送签名列表给玩家
 *
 * @note 此函数通常在玩家右键点击请愿书物品或被他人展示请愿书时调用
 */
void WorldSession::HandlePetitionShowSignatures(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_PETITION_SHOW_SIGNATURES");

    ObjectGuid petitionGuid;
    recvData >> petitionGuid;                              // petition guid

    Petition const* petition = sPetitionMgr->GetPetition(petitionGuid);
    if (!petition)
    {
        TC_LOG_DEBUG("entities.player.items", "Petition {} is not found for player {} {}", petitionGuid.ToString(), GetPlayer()->GetGUID().ToString(), GetPlayer()->GetName());
        return;
    }

    // if guild petition and has guild => error, return;
    if (petition->PetitionType == GUILD_CHARTER_TYPE && _player->GetGuildId())
        return;

    TC_LOG_DEBUG("network", "CMSG_PETITION_SHOW_SIGNATURES petition {}", petitionGuid.ToString());

    SendPetitionSigns(petition, _player);
}

/**
 * @brief 发送请愿书签名列表
 *
 * @brief 职责：
 * 构建并发送请愿书签名列表数据包给指定玩家。
 * 包含请愿书GUID、所有者GUID和所有已签名玩家的信息。
 *
 * @param petition 请愿书数据指针，包含签名信息
 * @param sendTo 接收数据包的目标玩家
 *
 * @return 无返回值。向目标玩家发送签名列表数据包。
 *
 * @brief 数据包结构：
 * - 请愿书GUID（8字节）
 * - 所有者GUID（8字节）
 * - 请愿书计数器（4字节）
 * - 签名数量（1字节）
 * - 每个签名：玩家GUID（8字节）+ 未知字段（4字节）
 */
void WorldSession::SendPetitionSigns(Petition const* petition, Player* sendTo)
{
    SignaturesVector const& signatures = petition->Signatures;
    WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, (8 + 8 + 4 + 1 + signatures.size() * 12));
    data << uint64(petition->PetitionGuid);                 // petition guid
    data << uint64(petition->OwnerGuid);                    // owner guid
    data << uint32(petition->PetitionGuid.GetCounter());    // guild guid
    data << uint8(signatures.size());                       // sign's count

    for (Signature const& signature : signatures)
    {
        data << signature.second;                       // Player GUID
        data << uint32(0);                              // there 0 ...
    }

    sendTo->SendDirectMessage(&data);
}

/**
 * @brief 处理查询请愿书请求
 *
 * @brief 职责：
 * 处理玩家查询请愿书详细信息的请求。
 * 返回请愿书的基本信息，包括名称、所需签名数等。
 *
 * @param recvData 接收的网络数据包，包含公会GUID和请愿书GUID
 *
 * @return 无返回值。向客户端发送请愿书查询响应。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取公会GUID和请愿书GUID
 * 2. 调用SendPetitionQueryOpcode发送请愿书信息
 *
 * @note 在TrinityCore中，公会GUID通常与请愿书GUID的低32位相同
 */
void WorldSession::HandleQueryPetition(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_PETITION_QUERY");   // ok

    ObjectGuid::LowType guildguid;
    ObjectGuid petitionguid;
    recvData >> guildguid;                                 // in Trinity always same as GUID_LOPART(petitionguid)
    recvData >> petitionguid;                              // petition guid
    TC_LOG_DEBUG("network", "CMSG_PETITION_QUERY Petition {} Guild GUID {}", petitionguid.ToString(), guildguid);

    SendPetitionQueryOpcode(petitionguid);
}

/**
 * @brief 发送请愿书查询响应
 *
 * @brief 职责：
 * 构建并发送请愿书详细信息的响应数据包。
 * 包含请愿书ID、所有者、名称、所需签名数等信息。
 *
 * @param petitionguid 请愿书GUID
 *
 * @return 无返回值。向客户端发送请愿书查询响应数据包。
 *
 * @brief 数据包结构：
 * - 请愿书GUID计数器（4字节）
 * - 所有者GUID（8字节）
 * - 请愿书名称（可变长度字符串）
 * - 所需签名数（根据类型不同）
 * - 多个配置字段（用于客户端显示）
 * - 类型标志（0=公会，1=竞技场）
 *
 * @note 仅发送静态数据，动态数据（如签名）通过其他数据包发送
 */
void WorldSession::SendPetitionQueryOpcode(ObjectGuid petitionguid)
{
    Petition const* petition = sPetitionMgr->GetPetition(petitionguid);
    if (!petition)
    {
        TC_LOG_DEBUG("network", "CMSG_PETITION_QUERY failed for petition ({})", petitionguid.ToString());
        return;
    }

    WorldPacket data(SMSG_PETITION_QUERY_RESPONSE, (4+8+petition->PetitionName.size()+1+1+4*12+2+10));
    data << uint32(petitionguid.GetCounter());              // guild/team guid (in Trinity always same as GUID_LOPART(petition guid)
    data << uint64(petition->OwnerGuid);                    // charter owner guid
    data << petition->PetitionName;                         // name (guild/arena team)
    data << uint8(0);                                       // some string

    CharterTypes type = petition->PetitionType;
    if (type == GUILD_CHARTER_TYPE)
    {
        uint32 needed = sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS);
        data << uint32(needed);
        data << uint32(needed);
        data << uint32(0);                                  // bypass client - side limitation, a different value is needed here for each petition
    }
    else
    {
        data << uint32(type-1);
        data << uint32(type-1);
        data << uint32(type);                               // bypass client - side limitation, a different value is needed here for each petition
    }
    data << uint32(0);                                      // 5
    data << uint32(0);                                      // 6
    data << uint32(0);                                      // 7
    data << uint32(0);                                      // 8
    data << uint16(0);                                      // 9 2 bytes field
    data << uint32(0);                                      // 10
    data << uint32(0);                                      // 11
    data << uint32(0);                                      // 13 count of next strings?

    for (int i = 0; i < 10; ++i)
        data << uint8(0);                                   // some string

    data << uint32(0);                                      // 14

    data << uint32(type != GUILD_CHARTER_TYPE);             // 15 0 - guild, 1 - arena team

    SendPacket(&data);
}

/**
 * @brief 处理重命名请愿书请求
 *
 * @brief 职责：
 * 处理玩家重命名请愿书（公会或竞技场队伍名称）的请求。
 * 验证新名称的有效性并更新请愿书记录。
 *
 * @param recvData 接收的网络数据包，包含请愿书GUID和新名称
 *
 * @return 无返回值。成功时更新请愿书名称并向客户端发送确认。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书GUID和新名称
 * 2. 验证玩家是否拥有该请愿书物品
 * 3. 获取请愿书数据
 * 4. 根据类型验证新名称是否有效：
 *    - 公会：检查名称是否已被使用、是否为保留名称、格式是否有效
 *    - 竞技场：检查名称是否已被使用、是否为保留名称、格式是否有效
 * 5. 更新请愿书存储中的名称
 * 6. 发送重命名确认数据包
 *
 * @note 名称更改会立即更新到数据库和请愿书管理器中
 */
void WorldSession::HandlePetitionRenameGuild(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode MSG_PETITION_RENAME");   // ok

    ObjectGuid petitionGuid;
    std::string newName;

    recvData >> petitionGuid;                              // guid
    recvData >> newName;                                   // new name

    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
        return;

    Petition* petition = sPetitionMgr->GetPetition(petitionGuid);
    if (!petition)
    {
        TC_LOG_DEBUG("network", "CMSG_PETITION_QUERY failed for petition {}", petitionGuid.ToString());
        return;
    }

    CharterTypes type = petition->PetitionType;
    if (type == GUILD_CHARTER_TYPE)
    {
        if (sGuildMgr->GetGuildByName(newName))
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NAME_EXISTS_S, newName);
            return;
        }
        if (sObjectMgr->IsReservedName(newName) || !ObjectMgr::IsValidCharterName(newName))
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NAME_INVALID, newName);
            return;
        }
    }
    else
    {
        if (sArenaTeamMgr->GetArenaTeamByName(newName))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, newName, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            return;
        }
        if (sObjectMgr->IsReservedName(newName) || !ObjectMgr::IsValidCharterName(newName))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, newName, "", ERR_ARENA_TEAM_NAME_INVALID);
            return;
        }
    }

    CharacterDatabase.EscapeString(newName);

    // update petition storage
    petition->UpdateName(newName);

    TC_LOG_DEBUG("network", "Petition {} renamed to '{}'", petitionGuid.ToString(), newName);
    WorldPacket data(MSG_PETITION_RENAME, (8+newName.size()+1));
    data << uint64(petitionGuid);
    data << newName;
    SendPacket(&data);
}

/**
 * @brief 处理签署请愿书请求
 *
 * @brief 职责：
 * 处理玩家签署公会或竞技场请愿书的请求。
 * 验证签署条件并将签名添加到请愿书中。
 *
 * @param recvData 接收的网络数据包，包含请愿书GUID和未知参数
 *
 * @return 无返回值。成功时将签名添加到请愿书并通知相关玩家。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书GUID
 * 2. 获取请愿书数据并验证存在性
 * 3. 验证签署者不是请愿书所有者
 * 4. 验证阵营限制（是否允许对立阵营签署）
 * 5. 根据类型验证签署条件：
 *    - 竞技场：验证等级、是否已在队伍中、是否已被邀请
 *    - 公会：验证是否已在公会中、是否已被邀请
 * 6. 验证签名数量未超过上限
 * 7. 验证签署者账号未签署过此请愿书
 * 8. 添加签名到请愿书
 * 9. 发送签署结果给签署者和请愿书所有者
 *
 * @note 一个账号只能签署一次同一请愿书，防止滥用
 */
void WorldSession::HandleSignPetition(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_PETITION_SIGN");    // ok

    ObjectGuid petitionGuid;
    uint8 unk;
    recvData >> petitionGuid;                              // petition guid
    recvData >> unk;

    Petition* petition = sPetitionMgr->GetPetition(petitionGuid);
    if (!petition)
    {
        TC_LOG_ERROR("network", "Petition {} is not found for player {} {}", petitionGuid.ToString(), GetPlayer()->GetGUID().ToString(), GetPlayer()->GetName());
        return;
    }

    ObjectGuid ownerGuid = petition->OwnerGuid;
    CharterTypes type = petition->PetitionType;
    uint8 signs = uint8(petition->Signatures.size());

    ObjectGuid playerGuid = _player->GetGUID();
    if (ownerGuid == playerGuid)
        return;

    // not let enemies sign guild charter
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && GetPlayer()->GetTeam() != sCharacterCache->GetCharacterTeamByGuid(ownerGuid))
    {
        if (type != GUILD_CHARTER_TYPE)
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        else
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (type != GUILD_CHARTER_TYPE)
    {
        if (!_player->IsMaxLevel())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", _player->GetName(), ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
            return;
        }

        uint8 slot = ArenaTeam::GetSlotByType(static_cast<uint32>(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        if (_player->GetArenaTeamId(slot))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_IN_ARENA_TEAM_S);
            return;
        }

        if (_player->GetArenaTeamIdInvited())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_INVITED_TO_ARENA_TEAM_S);
            return;
        }
    }
    else
    {
        if (_player->GetGuildId())
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_INVITE, ERR_ALREADY_IN_GUILD_S, _player->GetName());
            return;
        }
        if (_player->GetGuildIdInvited())
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_INVITE, ERR_ALREADY_INVITED_TO_GUILD_S, _player->GetName());
            return;
        }
    }

    if (++signs > static_cast<uint8>(type))                                        // client signs maximum
        return;

    // Client doesn't allow to sign petition two times by one character, but not check sign by another character from same account
    // not allow sign another player from already sign player account
    bool isSigned = petition->IsPetitionSignedByAccount(GetAccountId());
    if (isSigned)
    {
        WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8+8+4));
        data << uint64(petitionGuid);
        data << uint64(_player->GetGUID());
        data << (uint32)PETITION_SIGN_ALREADY_SIGNED;

        // close at signer side
        SendPacket(&data);

        // update for owner if online
        if (Player* owner = ObjectAccessor::FindConnectedPlayer(ownerGuid))
            owner->SendDirectMessage(&data);
        return;
    }

    // fill petition store
    petition->AddSignature(GetAccountId(), playerGuid, false);

    TC_LOG_DEBUG("network", "PETITION SIGN: {} by player: {} ({} Account: {})", petitionGuid.ToString(), _player->GetName(), playerGuid.ToString(), GetAccountId());

    WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8+8+4));
    data << uint64(petitionGuid);
    data << uint64(_player->GetGUID());
    data << uint32(PETITION_SIGN_OK);
    SendPacket(&data);

    // update for owner if online
    if (Player* owner = ObjectAccessor::FindConnectedPlayer(ownerGuid))
        owner->SendDirectMessage(&data);
}

/**
 * @brief 处理拒绝签署请愿书请求
 *
 * @brief 职责：
 * 处理玩家拒绝签署请愿书的请求。
 * 通知请愿书所有者该玩家拒绝了签署请求。
 *
 * @param recvData 接收的网络数据包，包含请愿书GUID
 *
 * @return 无返回值。通知请愿书所有者被拒绝的消息。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书GUID
 * 2. 获取请愿书数据
 * 3. 如果请愿书所有者在线，发送拒绝通知
 *
 * @note 这是玩家展示请愿书后，其他玩家点击"拒绝"按钮时调用
 */
void WorldSession::HandleDeclinePetition(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode MSG_PETITION_DECLINE");  // ok

    ObjectGuid petitionguid;
    recvData >> petitionguid;                              // petition guid
    TC_LOG_DEBUG("network", "Petition {} declined by {}", petitionguid.ToString(), _player->GetGUID().ToString());

    Petition const* petition = sPetitionMgr->GetPetition(petitionguid);
    if (!petition)
        return;

    // petition owner online
    if (Player* owner = ObjectAccessor::FindConnectedPlayer(petition->OwnerGuid))
    {
        WorldPacket data(MSG_PETITION_DECLINE, 8);
        data << uint64(_player->GetGUID());
        owner->SendDirectMessage(&data);
    }
}

/**
 * @brief 处理提供请愿书签署请求
 *
 * @brief 职责：
 * 处理请愿书所有者向其他玩家展示请愿书并请求签名的消息。
 * 验证目标玩家是否可以签署该请愿书。
 *
 * @param recvData 接收的网络数据包，包含请愿书类型、请愿书GUID和目标玩家GUID
 *
 * @return 无返回值。向目标玩家发送签名界面或向请求者发送错误消息。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书类型、请愿书GUID和目标玩家GUID
 * 2. 查找目标玩家
 * 3. 获取请愿书数据
 * 4. 验证阵营限制（是否允许对立阵营签署）
 * 5. 根据类型验证目标玩家是否可以签署：
 *    - 竞技场：验证等级、是否已在队伍中、是否已被邀请
 *    - 公会：验证是否已在公会中、是否已被邀请
 * 6. 向目标玩家发送签名界面
 *
 * @note 这是玩家右键点击其他玩家并选择"请求签名"时调用
 */
void WorldSession::HandleOfferPetitionOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_OFFER_PETITION");   // ok

    ObjectGuid petitionGuid, offererGuid;
    uint32 junk;
    recvData >> junk;                                      // this is not petition type!
    recvData >> petitionGuid;                              // petition guid
    recvData >> offererGuid;                               // player guid

    Player* player = ObjectAccessor::FindConnectedPlayer(offererGuid);
    if (!player)
        return;

    Petition const* petition = sPetitionMgr->GetPetition(petitionGuid);
    if (!petition)
        return;

    CharterTypes type = petition->PetitionType;

    TC_LOG_DEBUG("network", "OFFER PETITION: type {}, {}, to {}", static_cast<uint32>(type), petitionGuid.ToString(), offererGuid.ToString());

    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && GetPlayer()->GetTeam() != player->GetTeam())
    {
        if (type != GUILD_CHARTER_TYPE)
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        else
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (type != GUILD_CHARTER_TYPE)
    {
        if (!player->IsMaxLevel())
        {
            // player is too low level to join an arena team
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "", ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
            return;
        }

        uint8 slot = ArenaTeam::GetSlotByType(static_cast<uint32>(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        if (player->GetArenaTeamId(slot))
        {
            // player is already in an arena team
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "", ERR_ALREADY_IN_ARENA_TEAM_S);
            return;
        }

        if (player->GetArenaTeamIdInvited())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_INVITED_TO_ARENA_TEAM_S);
            return;
        }
    }
    else
    {
        if (player->GetGuildId())
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_INVITE, ERR_ALREADY_IN_GUILD_S, _player->GetName());
            return;
        }

        if (player->GetGuildIdInvited())
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_INVITE, ERR_ALREADY_INVITED_TO_GUILD_S, _player->GetName());
            return;
        }
    }

    SendPetitionSigns(petition, player);
}

/**
 * @brief 处理提交请愿书请求
 *
 * @brief 职责：
 * 处理玩家提交请愿书以创建公会或竞技场队伍的请求。
 * 验证所有条件并最终创建公会或竞技场队伍。
 *
 * @param recvData 接收的网络数据包，包含请愿书GUID和竞技场徽章数据
 *
 * @return 无返回值。成功时创建公会或竞技场队伍并通知玩家。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取请愿书GUID
 * 2. 验证玩家拥有该请愿书物品
 * 3. 获取请愿书数据
 * 4. 验证提交者是请愿书所有者
 * 5. 根据类型验证创建条件：
 *    - 公会：验证玩家未在公会中、名称未被占用
 *    - 竞技场：验证玩家未在该槽位、名称未被占用
 * 6. 验证签名数量是否满足要求
 * 7. 删除请愿书物品
 * 8. 创建公会或竞技场队伍：
 *    - 公会：创建公会，添加所有签名的玩家为成员
 *    - 竞技场：读取徽章数据，创建队伍，添加所有签名的玩家为成员
 * 9. 从请愿书管理器中移除请愿书
 * 10. 发送成功消息
 *
 * @note 这是请愿书流程的最后一步，成功后请愿书被消耗
 */
void WorldSession::HandleTurnInPetitionOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_TURN_IN_PETITION");

    // Get petition guid from packet
    WorldPacket data;
    ObjectGuid petitionGuid;

    recvData >> petitionGuid;

    // Check if player really has the required petition charter
    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
        return;

    TC_LOG_DEBUG("network", "Petition {} turned in by {}", petitionGuid.ToString(), _player->GetGUID().ToString());

    Petition const* petition = sPetitionMgr->GetPetition(petitionGuid);
    if (!petition)
    {
        TC_LOG_ERROR("entities.player.cheat", "Player {} {} tried to turn in petition ({}) that is not present in the database", _player->GetName(), _player->GetGUID().ToString(), petitionGuid.ToString());
        return;
    }

    CharterTypes type = petition->PetitionType;
    std::string const name = petition->PetitionName; // we need a copy, it will be removed on guild/arena remove

    // Only the petition owner can turn in the petition
    if (_player->GetGUID() != petition->OwnerGuid)
        return;

    // Petition type (guild/arena) specific checks
    if (type == GUILD_CHARTER_TYPE)
    {
        // Check if player is already in a guild
        if (_player->GetGuildId())
        {
            data.Initialize(SMSG_TURN_IN_PETITION_RESULTS, 4);
            data << (uint32)PETITION_TURN_ALREADY_IN_GUILD;
            _player->SendDirectMessage(&data);
            return;
        }

        // Check if guild name is already taken
        if (sGuildMgr->GetGuildByName(name))
        {
            Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_NAME_EXISTS_S, name);
            return;
        }
    }
    else
    {
        // Check for valid arena bracket (2v2, 3v3, 5v5)
        uint8 slot = ArenaTeam::GetSlotByType(static_cast<uint32>(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        // Check if player is already in an arena team
        if (_player->GetArenaTeamId(slot))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ALREADY_IN_ARENA_TEAM);
            return;
        }

        // Check if arena team name is already taken
        if (sArenaTeamMgr->GetArenaTeamByName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            return;
        }
    }

    SignaturesVector const signatures = petition->Signatures; // we need a copy, it will be removed on guild/arena remove
    uint32 requiredSignatures = static_cast<uint32>(type) - 1;
    if (type == GUILD_CHARTER_TYPE)
        requiredSignatures = sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS);

    // Notify player if signatures are missing
    if (signatures.size() < requiredSignatures)
    {
        data.Initialize(SMSG_TURN_IN_PETITION_RESULTS, 4);
        data << (uint32)PETITION_TURN_NEED_MORE_SIGNATURES;
        SendPacket(&data);
        return;
    }

    // Proceed with guild/arena team creation

    // Delete charter item
    _player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    if (type == GUILD_CHARTER_TYPE)
    {
        // Create guild
        Guild* guild = new Guild;

        if (!guild->Create(_player, name))
        {
            delete guild;
            return;
        }

        // Register guild and add guild master
        sGuildMgr->AddGuild(guild);

        Guild::SendCommandResult(this, GUILD_COMMAND_CREATE, ERR_GUILD_COMMAND_SUCCESS, name);

        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            // Add members from signatures
            for (Signature const& signature : signatures)
                guild->AddMember(trans, signature.second);

            CharacterDatabase.CommitTransaction(trans);
        }
    }
    else
    {
        // Receive the rest of the packet in arena team creation case
        uint32 background, icon, iconcolor, border, bordercolor;
        recvData >> background >> icon >> iconcolor >> border >> bordercolor;

        // Create arena team
        ArenaTeam* arenaTeam = new ArenaTeam();

        if (!arenaTeam->Create(_player->GetGUID(), type, name, background, icon, iconcolor, border, bordercolor))
        {
            delete arenaTeam;
            return;
        }

        // Register arena team
        sArenaTeamMgr->AddArenaTeam(arenaTeam);
        TC_LOG_DEBUG("network", "PetitonsHandler: Arena team (guid: {}) added to ObjectMgr", arenaTeam->GetId());

        // Add members
        for (Signature const& signature : signatures)
        {
            TC_LOG_DEBUG("network", "PetitionsHandler: Adding arena team (guid: {}) member {}", arenaTeam->GetId(), signature.second.ToString());
            arenaTeam->AddMember(signature.second);
        }
    }

    sPetitionMgr->RemovePetition(petitionGuid);

    // created
    TC_LOG_DEBUG("network", "Player {} ({}) turning in petition {}", _player->GetName(), _player->GetGUID().ToString(), petitionGuid.ToString());

    data.Initialize(SMSG_TURN_IN_PETITION_RESULTS, 4);
    data << (uint32)PETITION_TURN_OK;
    SendPacket(&data);
}

/**
 * @brief 处理显示请愿书列表请求
 *
 * @brief 职责：
 * 处理玩家请求查看NPC出售的请愿书列表。
 * 向客户端发送该NPC可出售的所有请愿书信息。
 *
 * @param recvData 接收的网络数据包，包含NPC GUID
 *
 * @return 无返回值。向客户端发送请愿书列表。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取NPC GUID
 * 2. 调用SendPetitionShowList发送列表
 *
 * @note 通常在玩家与请愿书NPC交互时触发
 */
void WorldSession::HandlePetitionShowListOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received CMSG_PETITION_SHOWLIST");

    ObjectGuid guid;
    recvData >> guid;

    SendPetitionShowList(guid);
}

/**
 * @brief 发送请愿书出售列表
 *
 * @brief 职责：
 * 构建并发送NPC可出售的请愿书列表数据包。
 * 根据NPC类型发送不同的请愿书列表：
 * - 军旗设计师：仅公会宪章
 * - 竞技场管理员：2v2、3v3、5v5竞技场宪章
 *
 * @param guid NPC的GUID
 *
 * @return 无返回值。向客户端发送请愿书列表数据包。
 *
 * @brief 数据包结构（公会NPC）：
 * - NPC GUID
 * - 数量（1）
 * - 索引（1）
 * - 宪章物品ID
 * - 显示ID
 * - 费用
 * - 未知字段
 * - 所需签名数
 *
 * @brief 数据包结构（竞技场NPC）：
 * - NPC GUID
 * - 数量（3）
 * - 三个竞技场宪章的详细信息（2v2、3v3、5v5）
 *
 * @note 军旗设计师NPC同时具有TABARDDESIGNER和PETITIONER标志
 */
void WorldSession::SendPetitionShowList(ObjectGuid guid)
{
    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_PETITIONER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandlePetitionShowListOpcode - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    WorldPacket data(SMSG_PETITION_SHOWLIST, 8+1+4*6);
    data << guid;                                           // npc guid

    if (creature->IsTabardDesigner())
    {
        data << uint8(1);                                   // count
        data << uint32(1);                                  // index
        data << uint32(GUILD_CHARTER);                      // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(sWorld->getIntConfig(CONFIG_CHARTER_COST_GUILD)); // charter cost
        data << uint32(0);                                  // unknown
        data << uint32(sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS)); // required signs
    }
    else
    {
        data << uint8(3);                                   // count
        // 2v2
        data << uint32(1);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_2v2);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_2v2)); // charter cost
        data << uint32(2);                                  // unknown
        data << uint32(2);                                  // required signs?
        // 3v3
        data << uint32(2);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_3v3);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_3v3)); // charter cost
        data << uint32(3);                                  // unknown
        data << uint32(3);                                  // required signs?
        // 5v5
        data << uint32(3);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_5v5);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(sWorld->getIntConfig(CONFIG_CHARTER_COST_ARENA_5v5)); // charter cost
        data << uint32(5);                                  // unknown
        data << uint32(5);                                  // required signs?
    }

    SendPacket(&data);
    TC_LOG_DEBUG("network", "Sent SMSG_PETITION_SHOWLIST");
}
