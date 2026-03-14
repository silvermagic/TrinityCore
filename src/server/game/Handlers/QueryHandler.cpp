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
 * @file QueryHandler.cpp
 * @brief 游戏数据查询处理模块
 *
 * @details 本模块负责处理客户端查询游戏对象静态数据的请求,包括:
 * - 角色名称查询：根据GUID获取角色名称和基本信息
 * - 游戏时间查询：获取服务器时间和每日任务重置时间
 * - 生物模板查询：获取NPC的静态数据（名称、模型、旗帜等）
 * - 游戏对象模板查询：获取GameObject的静态数据
 * - 尸体位置查询：获取玩家尸体的位置信息
 * - NPC文本查询：获取NPC对话文本
 * - 页面文本查询：获取物品描述等多页文本
 * - 任务POI查询：获取任务目标在地图上的兴趣点信息
 *
 * @note 本模块处理的数据大多是静态数据，可以缓存以提高性能
 * @note 客户端会在需要显示某些信息时主动发送查询请求
 */

#include "WorldSession.h"
#include "CharacterCache.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "MapManager.h"
#include "NPCHandler.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryPackets.h"
#include "UpdateMask.h"
#include "World.h"

/**
 * @brief 发送角色名称查询响应
 *
 * @brief 职责：
 * 根据角色GUID查询并返回角色的名称、种族、性别、职业等基本信息。
 * 支持跨服角色查询（战场等场景）。
 *
 * @param guid 要查询的角色GUID
 *
 * @return 无返回值。向客户端发送名称查询响应数据包。
 *
 * @brief 主要流程：
 * 1. 尝试在线查找该角色（可能在线或离线）
 * 2. 从角色缓存获取角色数据
 * 3. 如果找不到角色数据，发送"名称未知"响应
 * 4. 如果找到数据，构建响应数据包：
 *    - GUID（压缩格式）
 *    - 名称已知标志
 *    - 角色名称
 *    - 服务器名称（跨服场景使用）
 *    - 种族、性别、职业
 *    - 名称变格数据（某些语言需要）
 * 5. 发送数据包
 *
 * @note 使用CharacterCache提高查询效率，避免频繁访问数据库
 * @note 性能注意事项：此函数可能被频繁调用，应确保缓存机制有效
 */
void WorldSession::SendNameQueryOpcode(ObjectGuid guid)
{
    // 尝试查找在线玩家
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    // 从角色缓存获取角色数据（包括离线角色）
    CharacterCacheEntry const* nameData = sCharacterCache->GetCharacterCacheByGuid(guid);

    // 构建名称查询响应数据包
    WorldPacket data(SMSG_NAME_QUERY_RESPONSE, (8+1+1+1+1+1+10));
    data << guid.WriteAsPacked();                            // 写入压缩格式的GUID

    if (!nameData)
    {
        // 未找到角色数据，返回"名称未知"响应
        data << uint8(1);                                    // 名称未知标志
        SendPacket(&data);
        return;
    }

    // 找到角色数据，构建完整响应
    data << uint8(0);                                        // 名称已知标志
    data << nameData->Name;                                  // 角色名称
    data << uint8(0);                                        // 服务器名称（跨服交互时设置，如战场）
    data << uint8(nameData->Race);                           // 种族
    data << uint8(nameData->Sex);                            // 性别
    data << uint8(nameData->Class);                          // 职业

    // 处理名称变格（某些语言如俄语需要名称的不同语法形式）
    if (DeclinedName const* names = (player ? player->GetDeclinedNames() : nullptr))
    {
        data << uint8(1);                                    // 名称有变格形式
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data << names->name[i];                          // 写入各种语法形式的名称
    }
    else
        data << uint8(0);                                    // 名称无变格形式

    SendPacket(&data);
}

/**
 * @brief 处理角色名称查询请求
 *
 * @brief 职责：
 * 处理客户端发送的角色名称查询请求。
 * 客户端会在需要显示角色名称时发送此请求。
 *
 * @param recvData 接收的网络数据包，包含要查询的角色GUID
 *
 * @return 无返回值。调用SendNameQueryOpcode发送响应。
 *
 * @brief 调用时机：
 * - 看到其他玩家但名称未知时
 * - 收到其他玩家的消息时
 * - 查看目标信息时
 *
 * @note 默认禁用日志以避免大量控制台输出
 */
void WorldSession::HandleNameQueryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // This is disable by default to prevent lots of console spam
    // TC_LOG_INFO("network", "HandleNameQueryOpcode {}", guid);

    SendNameQueryOpcode(guid);
}

/**
 * @brief 处理游戏时间查询请求
 *
 * @brief 职责：
 * 处理客户端请求获取当前游戏时间和每日任务重置倒计时的消息。
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值。发送游戏时间响应。
 *
 * @brief 调用时机：
 * - 玩家登录时
 * - 客户端需要同步时间时
 * - 查看每日任务重置时间时
 */
void WorldSession::HandleQueryTimeOpcode(WorldPacket & /*recvData*/)
{
    SendQueryTimeResponse();
}

/**
 * @brief 发送游戏时间查询响应
 *
 * @brief 职责：
 * 构建并发送游戏时间和每日任务重置倒计时的响应数据包。
 *
 * @return 无返回值。向客户端发送时间信息数据包。
 *
 * @brief 数据包结构：
 * - 当前游戏时间（Unix时间戳）
 * - 距离下次每日任务重置的秒数
 *
 * @note 客户端使用此信息显示日常任务重置倒计时
 */
void WorldSession::SendQueryTimeResponse()
{
    WorldPacket data(SMSG_QUERY_TIME_RESPONSE, 4+4);
    data << uint32(GameTime::GetGameTime());
    data << uint32(sWorld->GetNextDailyQuestsResetTime() - GameTime::GetGameTime());
    SendPacket(&data);
}

/**
 * @brief 处理生物模板查询请求
 *
 * @brief 职责：
 * 处理客户端查询NPC生物模板数据的请求。
 * 返回NPC的静态数据，如名称、模型ID、旗帜、类型等。
 *
 * @param query 接收的生物查询数据包，包含生物ID和GUID
 *
 * @return 无返回值。向客户端发送生物模板数据或空响应。
 *
 * @brief 主要流程：
 * 1. 根据生物ID获取生物模板数据
 * 2. 如果找到数据：
 *    - 检查是否启用查询数据缓存
 *    - 如果启用缓存，直接发送预构建的数据包
 *    - 否则动态构建并发送数据包
 * 3. 如果未找到数据，发送空响应
 *
 * @note 仅发送静态数据，动态数据（位置、状态等）通过其他数据包发送
 * @note 性能注意事项：建议启用CONFIG_CACHE_DATA_QUERIES以提高性能
 * @brief 调用时机：客户端看到新NPC时发送查询请求
 */
/// Only _static_ data is sent in this packet !!!
void WorldSession::HandleCreatureQueryOpcode(WorldPackets::Query::QueryCreature& query)
{
    if (CreatureTemplate const* ci = sObjectMgr->GetCreatureTemplate(query.CreatureID))
    {
        TC_LOG_DEBUG("network", "WORLD: CMSG_CREATURE_QUERY '{}' - Entry: {}.", ci->Name, query.CreatureID);
        if (sWorld->getBoolConfig(CONFIG_CACHE_DATA_QUERIES))
            SendPacket(&ci->QueryData[static_cast<uint32>(GetSessionDbLocaleIndex())]);
        else
        {
            WorldPacket response = ci->BuildQueryData(GetSessionDbLocaleIndex());
            SendPacket(&response);
        }
        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_CREATURE_QUERY_RESPONSE");
    }
    else
    {
        TC_LOG_DEBUG("network", "WORLD: CMSG_CREATURE_QUERY - NO CREATURE INFO! ({}, ENTRY: {})",
            query.Guid.ToString(), query.CreatureID);

        WorldPackets::Query::QueryCreatureResponse response;
        response.CreatureID = query.CreatureID;
        SendPacket(response.Write());
        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_CREATURE_QUERY_RESPONSE");
    }
}

/**
 * @brief 处理游戏对象模板查询请求
 *
 * @brief 职责：
 * 处理客户端查询GameObject模板数据的请求。
 * 返回游戏对象的静态数据，如名称、类型、模型ID等。
 *
 * @param query 接收的游戏对象查询数据包，包含游戏对象ID和GUID
 *
 * @return 无返回值。向客户端发送游戏对象模板数据或空响应。
 *
 * @brief 主要流程：
 * 1. 根据游戏对象ID获取模板数据
 * 2. 如果找到数据：
 *    - 检查是否启用查询数据缓存
 *    - 如果启用缓存，直接发送预构建的数据包
 *    - 否则动态构建并发送数据包
 * 3. 如果未找到数据，发送空响应
 *
 * @note 仅发送静态数据，动态数据通过其他数据包发送
 * @note 性能注意事项：建议启用CONFIG_CACHE_DATA_QUERIES以提高性能
 * @brief 调用时机：客户端看到新的GameObject时发送查询请求
 */
/// Only _static_ data is sent in this packet !!!
void WorldSession::HandleGameObjectQueryOpcode(WorldPackets::Query::QueryGameObject& query)
{
    if (GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(query.GameObjectID))
    {
        if (sWorld->getBoolConfig(CONFIG_CACHE_DATA_QUERIES))
            SendPacket(&info->QueryData[static_cast<uint32>(GetSessionDbLocaleIndex())]);
        else
        {
            WorldPacket response = info->BuildQueryData(GetSessionDbLocaleIndex());
            SendPacket(&response);
        }
        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_GAMEOBJECT_QUERY_RESPONSE");
    }
    else
    {
        TC_LOG_DEBUG("network", "WORLD: CMSG_GAMEOBJECT_QUERY - Missing gameobject info for ({}, ENTRY: {})",
            query.Guid.ToString(), query.GameObjectID);

        WorldPackets::Query::QueryGameObjectResponse response;
        response.GameObjectID = query.GameObjectID;
        SendPacket(response.Write());
        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_GAMEOBJECT_QUERY_RESPONSE");
    }
}

/**
 * @brief 处理尸体位置查询请求
 *
 * @brief 职责：
 * 处理客户端查询玩家尸体位置的请求。
 * 返回尸体的地图、坐标等信息，用于玩家找回尸体。
 *
 * @param recvData 接收的网络数据包（未使用）
 *
 * @return 无返回值。向客户端发送尸体位置信息或"尸体未找到"响应。
 *
 * @brief 主要流程：
 * 1. 检查玩家是否有尸体
 * 2. 如果没有尸体，发送"尸体未找到"响应
 * 3. 获取尸体位置信息
 * 4. 如果尸体在不同地图，查找副本入口位置
 * 5. 构建并发送尸体位置数据包
 *
 * @brief 数据包结构：
 * - 尸体找到标志（1字节）
 * - 地图ID（实际显示地图，可能是副本入口）
 * - X、Y、Z坐标
 * - 尸体所在地图ID
 * - 未知字段
 *
 * @note 对于副本尸体，显示副本入口位置而不是尸体实际位置
 */
void WorldSession::HandleCorpseQueryOpcode(WorldPacket & /*recvData*/)
{
    // 检查玩家是否有尸体
    if (!_player->HasCorpse())
    {
        // 没有尸体，发送"尸体未找到"响应
        WorldPacket data(MSG_CORPSE_QUERY, 1);
        data << uint8(0);                                    // 尸体未找到标志
        SendPacket(&data);
        return;
    }

    // 获取尸体位置信息
    WorldLocation corpseLocation = _player->GetCorpseLocation();
    uint32 corpseMapID = corpseLocation.GetMapId();          // 尸体所在地图
    uint32 mapID = corpseLocation.GetMapId();                // 显示用的地图ID
    float x = corpseLocation.GetPositionX();
    float y = corpseLocation.GetPositionY();
    float z = corpseLocation.GetPositionZ();

    // 如果尸体在不同地图，查找副本入口位置
    if (mapID != _player->GetMapId())
    {
        // 查找地图入口，用于正确显示入口位置
        if (MapEntry const* corpseMapEntry = sMapStore.LookupEntry(mapID))
        {
            // 如果尸体在副本中且副本有入口地图
            if (corpseMapEntry->IsDungeon() && corpseMapEntry->CorpseMapID >= 0)
            {
                // 如果能找到入口地图
                if (Map const* entranceMap = sMapMgr->CreateBaseMap(corpseMapEntry->CorpseMapID))
                {
                    // 使用入口地图ID和入口坐标
                    mapID = corpseMapEntry->CorpseMapID;
                    x = corpseMapEntry->Corpse.X;
                    y = corpseMapEntry->Corpse.Y;
                    z = entranceMap->GetHeight(GetPlayer()->GetPhaseMask(), x, y, MAX_HEIGHT);
                }
            }
        }
    }

    // 构建并发送尸体位置数据包
    WorldPacket data(MSG_CORPSE_QUERY, 1+(6*4));
    data << uint8(1);                                        // 尸体找到标志
    data << int32(mapID);                                    // 显示地图ID（可能是入口地图）
    data << float(x);
    data << float(y);
    data << float(z);
    data << int32(corpseMapID);                              // 尸体实际所在地图
    data << uint32(0);                                       // 未知字段
    SendPacket(&data);
}

/**
 * @brief 处理NPC文本查询请求
 *
 * @brief 职责：
 * 处理客户端查询NPC对话文本的请求。
 * 返回NPC的对话文本内容，支持多语言和文本广播。
 *
 * @param recvData 接收的网络数据包，包含文本ID和NPC GUID
 *
 * @return 无返回值。向客户端发送NPC文本数据包。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取文本ID和NPC GUID
 * 2. 根据文本ID获取NPC文本数据
 * 3. 如果找不到文本，使用默认文本
 * 4. 构建响应数据包，包含：
 *    - 文本ID
 *    - 多个文本选项（概率、文本内容、语言、表情动画）
 * 5. 处理本地化和广播文本
 * 6. 发送数据包
 *
 * @note 每个NPC文本最多有MAX_GOSSIP_TEXT_OPTIONS个选项
 * @note 支持广播文本系统，优先使用广播文本
 * @brief 调用时机：玩家与NPC交互打开对话界面时
 */
void WorldSession::HandleNpcTextQueryOpcode(WorldPacket& recvData)
{
    uint32 textID;
    uint64 guid;

    recvData >> textID;
    TC_LOG_DEBUG("network", "WORLD: CMSG_NPC_TEXT_QUERY TextId: {}", textID);

    recvData >> guid;

    // 获取NPC文本数据
    GossipText const* gossip = sObjectMgr->GetGossipText(textID);

    // 构建NPC文本更新数据包
    WorldPacket data(SMSG_NPC_TEXT_UPDATE, 100);             // 估计大小
    data << textID;

    if (!gossip)
    {
        // 没有找到文本数据，使用默认文本
        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            data << float(0);                                 // 概率
            data << "Greetings $N";                           // 文本0
            data << "Greetings $N";                           // 文本1
            data << uint32(0);                                // 语言
            data << uint32(0);                                // 表情延迟/ID（8个字段）
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
        }
    }
    else
    {
        // 找到文本数据，构建完整响应
        std::string text0[MAX_GOSSIP_TEXT_OPTIONS], text1[MAX_GOSSIP_TEXT_OPTIONS];
        LocaleConstant locale = GetSessionDbLocaleIndex();

        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            // 优先使用广播文本系统
            BroadcastText const* bct = sObjectMgr->GetBroadcastText(gossip->Options[i].BroadcastTextID);
            if (bct)
            {
                // 使用广播文本（支持性别区分）
                text0[i] = bct->GetText(locale, GENDER_MALE, true);
                text1[i] = bct->GetText(locale, GENDER_FEMALE, true);
            }
            else
            {
                // 使用普通文本
                text0[i] = gossip->Options[i].Text_0;
                text1[i] = gossip->Options[i].Text_1;
            }

            // 如果不是默认语言且没有广播文本，尝试获取本地化文本
            if (locale != DEFAULT_LOCALE && !bct)
            {
                if (NpcTextLocale const* npcTextLocale = sObjectMgr->GetNpcTextLocale(textID))
                {
                    ObjectMgr::GetLocaleString(npcTextLocale->Text_0[i], locale, text0[i]);
                    ObjectMgr::GetLocaleString(npcTextLocale->Text_1[i], locale, text1[i]);
                }
            }

            // 写入文本选项数据
            data << gossip->Options[i].Probability;          // 显示概率

            // 写入文本内容（如果某个文本为空，使用另一个）
            if (text0[i].empty())
                data << text1[i];
            else
                data << text0[i];

            if (text1[i].empty())
                data << text0[i];
            else
                data << text1[i];

            data << gossip->Options[i].Language;             // 语言ID

            // 写入表情动画数据
            for (uint8 j = 0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                data << gossip->Options[i].Emotes[j]._Delay; // 表情延迟
                data << gossip->Options[i].Emotes[j]._Emote; // 表情ID
            }
        }
    }

    SendPacket(&data);
}

/**
 * @brief 处理页面文本查询请求
 *
 * @brief 职责：
 * 处理客户端查询页面文本的请求。
 * 页面文本用于物品描述、书籍等多页文本内容。
 *
 * @param recvData 接收的网络数据包，包含页面ID和GUID
 *
 * @return 无返回值。向客户端发送页面文本内容。
 *
 * @brief 主要流程：
 * 1. 从数据包中读取页面ID和GUID
 * 2. 查找页面文本数据
 * 3. 如果找到文本：
 *    - 获取本地化文本
 *    - 发送文本内容和下一页ID
 * 4. 如果未找到，发送错误文本
 * 5. 如果有下一页，继续发送下一页内容（循环处理）
 *
 * @note 支持多页文本，通过NextPageID链接
 * @note 仅发送静态数据
 */
/// Only _static_ data is sent in this packet !!!
void WorldSession::HandleQueryPageText(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_PAGE_TEXT_QUERY");

    uint32 pageID;
    recvData >> pageID;
    recvData.read_skip<uint64>();                          // guid

    while (pageID)
    {
        PageText const* pageText = sObjectMgr->GetPageText(pageID);
                                                            // guess size
        WorldPacket data(SMSG_PAGE_TEXT_QUERY_RESPONSE, 50);
        data << pageID;

        if (!pageText)
        {
            data << "Item page missing.";
            data << uint32(0);
            pageID = 0;
        }
        else
        {
            std::string Text = pageText->Text;

            LocaleConstant localeConstant = GetSessionDbLocaleIndex();
            if (localeConstant != LOCALE_enUS)
                if (PageTextLocale const* pageTextLocale = sObjectMgr->GetPageTextLocale(pageID))
                    ObjectMgr::GetLocaleString(pageTextLocale->Text, localeConstant, Text);

            data << Text;
            data << uint32(pageText->NextPageID);
            pageID = pageText->NextPageID;
        }
        SendPacket(&data);

        TC_LOG_DEBUG("network", "WORLD: Sent SMSG_PAGE_TEXT_QUERY_RESPONSE");
    }
}

/**
 * @brief 处理尸体地图位置查询请求
 *
 * @brief 职责：
 * 处理客户端查询尸体在地图上的位置的请求。
 * 返回尸体的精确坐标信息。
 *
 * @param recvData 接收的网络数据包，包含未知参数
 *
 * @return 无返回值。向客户端发送尸体地图位置信息。
 *
 * @note 当前实现返回全0坐标，可能需要进一步实现
 */
void WorldSession::HandleCorpseMapPositionQuery(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recv CMSG_CORPSE_MAP_POSITION_QUERY");

    uint32 unk;
    recvData >> unk;

    WorldPacket data(SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE, 4+4+4+4);
    data << float(0);
    data << float(0);
    data << float(0);
    data << float(0);
    SendPacket(&data);
}

/**
 * @brief 处理任务POI（兴趣点）查询请求
 *
 * @brief 职责：
 * 处理客户端批量查询任务兴趣点的请求。
 * 返回任务目标在地图上的标记点信息。
 *
 * @param query 接收的任务POI查询数据包，包含任务数量和任务ID列表
 *
 * @return 无返回值。向客户端发送任务POI数据。
 *
 * @brief 主要流程：
 * 1. 验证查询的任务数量是否有效
 * 2. 使用集合去重，避免发送重复任务的POI
 * 3. 遍历任务ID列表：
 *    - 验证玩家是否拥有该任务
 *    - 获取任务的POI数据
 *    - 如果启用缓存，直接使用缓存数据
 *    - 否则动态构建POI数据
 * 4. 发送所有POI数据
 *
 * @brief 数据包结构：
 * - 任务数量
 * - 每个任务的POI数据：
 *   - 任务ID
 *   - POI点数量
 *   - 每个POI的坐标、地图ID等信息
 *
 * @note 性能注意事项：建议启用CONFIG_CACHE_DATA_QUERIES以提高性能
 * @brief 调用时机：玩家查看任务日志或地图时
 */
void WorldSession::HandleQuestPOIQuery(WorldPackets::Query::QuestPOIQuery& query)
{
    if (query.MissingQuestCount > MAX_QUEST_LOG_SIZE)
        return;

    // Read quest ids and add the in a unordered_set so we don't send POIs for the same quest multiple times
    std::unordered_set<uint32> questIds;
    for (uint32 i = 0; i < query.MissingQuestCount; ++i)
        questIds.insert(query.MissingQuestPOIs[i]); // quest id

    WorldPacket data(SMSG_QUEST_POI_QUERY_RESPONSE, 4 + (4 + 4 + 40) * questIds.size());
    data << uint32(questIds.size()); // count

    for (uint32 questId : questIds)
    {
        uint16 const questSlot = _player->FindQuestSlot(questId);
        if (questSlot != MAX_QUEST_LOG_SIZE && _player->GetQuestSlotQuestId(questSlot) == questId)
        {
            if (QuestPOIWrapper const* poiWrapper = sObjectMgr->GetQuestPOIWrapper(questId))
            {
                if (sWorld->getBoolConfig(CONFIG_CACHE_DATA_QUERIES))
                    data.append(poiWrapper->QueryDataBuffer);
                else
                {
                    ByteBuffer POIByteBuffer = poiWrapper->BuildQueryData();
                    data.append(POIByteBuffer);
                }
            }
            else
            {
                data << uint32(questId); // quest ID
                data << uint32(0); // POI count
            }
        }
        else
        {
            data << uint32(questId); // quest ID
            data << uint32(0); // POI count
        }
    }

    SendPacket(&data);
}
