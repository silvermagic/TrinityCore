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
 * @file cs_go.cpp
 * @brief 传送命令模块
 *
 * 本模块实现了GM命令系统中用于传送玩家到各种位置的命令。
 * GM可以快速传送到指定的坐标、生物、游戏对象、墓地、出租车节点等位置。
 * 这些命令对于测试、调试和管理工作非常有用。
 *
 * 主要功能:
 * - 传送到指定坐标 (xyz)
 * - 传送到区域坐标 (zonexy)
 * - 传送到生物位置 (creature)
 * - 传送到游戏对象位置 (gameobject)
 * - 传送到墓地位置 (graveyard)
 * - 传送到网格位置 (grid)
 * - 传送到出租车节点 (taxinode)
 * - 传送到区域触发器 (areatrigger)
 * - 传送到工单位置 (ticket)
 * - 相对位置偏移传送 (offset)
 * - 传送到副本入口 (instance)
 * - 传送到Boss位置 (boss)
 *
 * 使用场景:
 * - 快速移动到测试地点
 * - 处理玩家工单
 * - 验证生物和对象的生成点
 * - 检查副本和Boss配置
 */
/* ScriptData
Name: go_commandscript
%Complete: 100
Comment: All go related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "Language.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "TicketMgr.h"
#include "Transport.h"
#include "Util.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class go_commandscript
 * @brief 传送命令脚本类
 *
 * 继承自CommandScript基类，提供玩家传送功能的GM命令实现。
 * 该类负责注册和处理所有与传送相关的命令。
 * 支持多种传送目标类型，包括坐标、生物、对象、墓地等。
 */
class go_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化传送命令脚本，设置脚本名称为"go_commandscript"
     */
    go_commandscript() : CommandScript("go_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回命令表的常量引用
     *
     * 注册所有传送相关的命令，包括：
     * - go creature：传送到生物位置
     * - go gameobject：传送到游戏对象位置
     * - go graveyard：传送到墓地位置
     * - go grid：传送到网格位置
     * - go taxinode：传送到出租车节点位置
     * - go areatrigger：传送到区域触发器位置
     * - go zonexy：传送到区域坐标位置
     * - go xyz：传送到精确坐标位置
     * - go ticket：传送到工单位置
     * - go offset：相对位置偏移传送
     * - go instance：传送到副本入口
     * - go boss：传送到Boss位置
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable goCommandTable =
        {
            { "creature",           HandleGoCreatureSpawnIdCommand,         rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "creature id",        HandleGoCreatureCIdCommand,             rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "gameobject",         HandleGoGameObjectSpawnIdCommand,       rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "gameobject id",      HandleGoGameObjectGOIdCommand,          rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "graveyard",          HandleGoGraveyardCommand,               rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "grid",               HandleGoGridCommand,                    rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "taxinode",           HandleGoTaxinodeCommand,                rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "areatrigger",        HandleGoAreaTriggerCommand,             rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "zonexy",             HandleGoZoneXYCommand,                  rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "xyz",                HandleGoXYZCommand,                     rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "ticket",             HandleGoTicketCommand,                  rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "offset",             HandleGoOffsetCommand,                  rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "instance",           HandleGoInstanceCommand,                rbac::RBAC_PERM_COMMAND_GO,             Console::No },
            { "boss",               HandleGoBossCommand,                    rbac::RBAC_PERM_COMMAND_GO,             Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "go", goCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 执行传送的核心函数
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param pos 目标位置坐标
     * @param mapId 目标地图ID，默认为无效地图（使用当前地图）
     * @return 成功返回true，失败返回false
     *
     * 这是所有传送命令的底层实现，负责：
     * 1. 验证目标坐标的有效性
     * 2. 如果玩家在飞行中，停止飞行
     * 3. 保存当前位置以便回传
     * 4. 执行实际传送操作
     *
     * 调用时机：被各个具体的传送命令调用
     * 性能注意：会触发地图加载和玩家数据更新
     */
    static bool DoTeleport(ChatHandler* handler, Position pos, uint32 mapId = MAPID_INVALID)
    {
        Player* player = handler->GetSession()->GetPlayer();

        // 如果未指定地图ID，使用当前地图
        if (mapId == MAPID_INVALID)
            mapId = player->GetMapId();

        // 验证地图坐标是否有效，排除传送地图
        if (!MapManager::IsValidMapCoord(mapId, pos) || sObjectMgr->IsTransportMap(mapId))
        {
            handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, pos.GetPositionX(), pos.GetPositionY(), mapId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // stop flight if need
        // 如果玩家在飞行路线上，强制结束飞行
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case
            // 保存当前位置用于回传，仅在非飞行状态保存

        // 执行传送
        player->TeleportTo({ mapId, pos });
        return true;
    }

    /**
     * @brief 传送到生物生成点（通过生成ID）
     * @param handler 聊天处理器
     * @param spawnId 生物的生成ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 根据生物的生成ID（spawnId）传送到该生物的生成位置。
     * 生成ID是数据库中记录的唯一标识。
     *
     * 调用时机：当GM执行 .go creature <spawnId> 命令时
     */
    static bool HandleGoCreatureSpawnIdCommand(ChatHandler* handler, Variant<Hyperlink<creature>, ObjectGuid::LowType> spawnId)
    {
        // 通过生成ID查询生物数据
        CreatureData const* spawnpoint = sObjectMgr->GetCreatureData(spawnId);
        if (!spawnpoint)
        {
            // 找不到该生成ID的生物
            handler->SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 传送到生物的生成位置
        return DoTeleport(handler, spawnpoint->spawnPoint, spawnpoint->mapId);
    }

    /**
     * @brief 传送到生物生成点（通过生物模板ID）
     * @param handler 聊天处理器
     * @param cId 生物的模板ID（creature entry），支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 根据生物的模板ID传送到该类型生物的生成位置。
     * 如果有多个相同模板的生物，会提示有多个生成点并传送到第一个。
     *
     * 调用时机：当GM执行 .go creature id <entry> 命令时
     */
    static bool HandleGoCreatureCIdCommand(ChatHandler* handler, Variant<Hyperlink<creature_entry>, uint32> cId)
    {
        CreatureData const* spawnpoint = nullptr;
        // 遍历所有生物数据，查找匹配模板ID的生成点
        for (auto const& pair : sObjectMgr->GetAllCreatureData())
        {
            if (pair.second.id != *cId)
                continue;

            if (!spawnpoint)
                spawnpoint = &pair.second;  // 记录第一个匹配的生成点
            else
            {
                // 发现多个匹配，提示用户
                handler->SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);
                break;
            }
        }

        if (!spawnpoint)
        {
            // 没有找到匹配的生物
            handler->SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 传送到找到的生成位置
        return DoTeleport(handler, spawnpoint->spawnPoint, spawnpoint->mapId);
    }

    /**
     * @brief 传送到游戏对象生成点（通过生成ID）
     * @param handler 聊天处理器
     * @param spawnId 游戏对象的生成ID
     * @return 成功返回true，失败返回false
     *
     * 根据游戏对象的生成ID传送到该对象的生成位置。
     *
     * 调用时机：当GM执行 .go gameobject <spawnId> 命令时
     */
    static bool HandleGoGameObjectSpawnIdCommand(ChatHandler* handler, uint32 spawnId)
    {
        // 通过生成ID查询游戏对象数据
        GameObjectData const* spawnpoint = sObjectMgr->GetGameObjectData(spawnId);
        if (!spawnpoint)
        {
            // 找不到该生成ID的游戏对象
            handler->SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 传送到游戏对象的生成位置
        return DoTeleport(handler, spawnpoint->spawnPoint, spawnpoint->mapId);
    }

    /**
     * @brief 传送到游戏对象生成点（通过对象模板ID）
     * @param handler 聊天处理器
     * @param goId 游戏对象的模板ID（gameobject entry）
     * @return 成功返回true，失败返回false
     *
     * 根据游戏对象的模板ID传送到该类型对象的生成位置。
     * 如果有多个相同模板的对象，会提示有多个生成点。
     *
     * 调用时机：当GM执行 .go gameobject id <entry> 命令时
     */
    static bool HandleGoGameObjectGOIdCommand(ChatHandler* handler, uint32 goId)
    {
        GameObjectData const* spawnpoint = nullptr;
        // 遍历所有游戏对象数据，查找匹配模板ID的生成点
        for (auto const& pair : sObjectMgr->GetAllGameObjectData())
        {
            if (pair.second.id != goId)
                continue;

            if (!spawnpoint)
                spawnpoint = &pair.second;  // 记录第一个匹配的生成点
            else
            {
                // 发现多个匹配，提示用户
                handler->SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);
                break;
            }
        }

        if (!spawnpoint)
        {
            // 没有找到匹配的游戏对象
            handler->SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 传送到找到的生成位置
        return DoTeleport(handler, spawnpoint->spawnPoint, spawnpoint->mapId);
    }

    /**
     * @brief 传送到墓地位置
     * @param handler 聊天处理器
     * @param gyId 墓地ID
     * @return 成功返回true，失败返回false
     *
     * 传送到指定ID的墓地位置。墓地在玩家死亡复活时使用。
     *
     * 调用时机：当GM执行 .go graveyard <id> 命令时
     */
    static bool HandleGoGraveyardCommand(ChatHandler* handler, uint32 gyId)
    {
        // 查询墓地数据
        WorldSafeLocsEntry const* gy = sWorldSafeLocsStore.LookupEntry(gyId);
        if (!gy)
        {
            // 找不到该墓地
            handler->PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST, gyId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证墓地坐标是否有效
        if (!MapManager::IsValidMapCoord(gy->Continent, gy->Loc.X, gy->Loc.Y, gy->Loc.Z))
        {
            handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, gy->Loc.X, gy->Loc.Y, gy->Continent);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        // stop flight if need
        // 如果玩家在飞行路线上，强制结束飞行
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case
            // 保存当前位置用于回传

        // 传送到墓地位置
        player->TeleportTo(gy->Continent, gy->Loc.X, gy->Loc.Y, gy->Loc.Z, player->GetOrientation());
        return true;
    }

    /**
     * @brief 传送到网格位置
     * @param handler 聊天处理器
     * @param gridX 网格X坐标
     * @param gridY 网格Y坐标
     * @param oMapId 可选的地图ID，不提供则使用当前地图
     * @return 成功返回true，失败返回false
     *
     * 传送到指定网格的中心位置。网格是地图的一种划分方式。
     * 坐标系统使用网格ID，会自动转换为世界坐标。
     *
     * 调用时机：当GM执行 .go grid <x> <y> 命令时
     */
    //teleport to grid
    static bool HandleGoGridCommand(ChatHandler* handler, float gridX, float gridY, Optional<uint32> oMapId)
    {
        Player* player = handler->GetSession()->GetPlayer();
        uint32 mapId = oMapId.value_or(player->GetMapId());

        // center of grid
        // 将网格坐标转换为世界坐标（网格中心）
        float x = (gridX - CENTER_GRID_ID + 0.5f) * SIZE_OF_GRIDS;
        float y = (gridY - CENTER_GRID_ID + 0.5f) * SIZE_OF_GRIDS;

        // 验证坐标有效性
        if (!MapManager::IsValidMapCoord(mapId, x, y))
        {
            handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, x, y, mapId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // stop flight if need
        // 如果玩家在飞行路线上，强制结束飞行
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case
            // 保存当前位置用于回传

        // 获取地图信息并计算高度
        Map const* map = sMapMgr->CreateBaseMap(mapId);
        // Z坐标取地形高度或水面高度的较大值
        float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));

        // 传送到计算出的位置
        player->TeleportTo(mapId, x, y, z, player->GetOrientation());
        return true;
    }

    /**
     * @brief 传送到出租车节点位置
     * @param handler 聊天处理器
     * @param nodeId 出租车节点ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 传送到指定的出租车节点（飞行点）位置。
     *
     * 调用时机：当GM执行 .go taxinode <id> 命令时
     */
    static bool HandleGoTaxinodeCommand(ChatHandler* handler, Variant<Hyperlink<taxinode>, uint32> nodeId)
    {
        // 查询出租车节点数据
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(nodeId);
        if (!node)
        {
            // 找不到该出租车节点
            handler->PSendSysMessage(LANG_COMMAND_GOTAXINODENOTFOUND, nodeId);
            handler->SetSentErrorMessage(true);
            return false;
        }
        // 传送到出租车节点位置
        return DoTeleport(handler, { node->Pos.X, node->Pos.Y, node->Pos.Z }, node->ContinentID);
    }

    /**
     * @brief 传送到区域触发器位置
     * @param handler 聊天处理器
     * @param areaTriggerId 区域触发器ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 传送到指定区域触发器的位置。区域触发器用于检测玩家进入特定区域，
     * 通常用于传送门、任务区域等。
     *
     * 调用时机：当GM执行 .go areatrigger <id> 命令时
     */
    static bool HandleGoAreaTriggerCommand(ChatHandler* handler, Variant<Hyperlink<areatrigger>, uint32> areaTriggerId)
    {
        // 查询区域触发器数据
        AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(areaTriggerId);
        if (!at)
        {
            // 找不到该区域触发器
            handler->PSendSysMessage(LANG_COMMAND_GOAREATRNOTFOUND, areaTriggerId);
            handler->SetSentErrorMessage(true);
            return false;
        }
        // 传送到区域触发器位置
        return DoTeleport(handler, { at->Pos.X, at->Pos.Y, at->Pos.Z }, at->ContinentID);
    }

    /**
     * @brief 传送到区域坐标位置
     * @param handler 聊天处理器
     * @param x 区域内的X坐标百分比（0-100）
     * @param y 区域内的Y坐标百分比（0-100）
     * @param areaIdArg 可选的区域ID，不提供则使用当前区域
     * @return 成功返回true，失败返回false
     *
     * 传送到指定区域内的相对坐标位置。坐标使用百分比表示（0-100）。
     * 系统会自动将区域坐标转换为世界坐标。
     *
     * 调用时机：当GM执行 .go zonexy <x> <y> 命令时
     */
    //teleport at coordinates
    static bool HandleGoZoneXYCommand(ChatHandler* handler, float x, float y, Optional<Variant<Hyperlink<area>, uint32>> areaIdArg)
    {
        Player* player = handler->GetSession()->GetPlayer();

        // 获取区域ID，如果没有指定则使用当前所在区域
        uint32 areaId = areaIdArg ? *areaIdArg : player->GetZoneId();

        AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(areaId);

        // 验证坐标范围和区域有效性
        if (x < 0 || x > 100 || y < 0 || y > 100 || !areaEntry)
        {
            handler->PSendSysMessage(LANG_INVALID_ZONE_COORD, x, y, areaId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // update to parent zone if exist (client map show only zones without parents)
        // 如果有父区域，使用父区域（客户端地图只显示没有父区域的区域）
        AreaTableEntry const* zoneEntry = areaEntry->ParentAreaID ? sAreaTableStore.LookupEntry(areaEntry->ParentAreaID) : areaEntry;
        ASSERT(zoneEntry);

        Map const* map = sMapMgr->CreateBaseMap(zoneEntry->ContinentID);

        // 检查地图是否为副本地图
        if (map->Instanceable())
        {
            handler->PSendSysMessage(LANG_INVALID_ZONE_MAP, areaEntry->ID, areaEntry->AreaName[handler->GetSessionDbcLocale()], map->GetId(), map->GetMapName());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 将区域坐标转换为地图坐标
        Zone2MapCoordinates(x, y, zoneEntry->ID);

        // 验证转换后的坐标有效性
        if (!MapManager::IsValidMapCoord(zoneEntry->ContinentID, x, y))
        {
            handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, x, y, zoneEntry->ContinentID);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // stop flight if need
        // 如果玩家在飞行路线上，强制结束飞行
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case
            // 保存当前位置用于回传

        // 计算高度（地形高度或水面高度）
        float z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));

        // 传送到计算出的位置
        player->TeleportTo(zoneEntry->ContinentID, x, y, z, player->GetOrientation());
        return true;
    }

    /**
     * @brief 传送到精确坐标位置
     * @param handler 聊天处理器
     * @param x 世界坐标X
     * @param y 世界坐标Y
     * @param z 可选的世界坐标Z（高度），不提供则自动计算
     * @param id 可选的地图ID，不提供则使用当前地图
     * @param o 可选的朝向角度，不提供则使用0
     * @return 成功返回true，失败返回false
     *
     * 传送到指定的精确世界坐标位置。这是最灵活的传送命令。
     * 如果不提供Z坐标，系统会根据地形自动计算合适的高度。
     *
     * 调用时机：当GM执行 .go xyz <x> <y> [z] [map] [o] 命令时
     */
    //teleport at coordinates, including Z and orientation
    static bool HandleGoXYZCommand(ChatHandler* handler, float x, float y, Optional<float> z, Optional<uint32> id, Optional<float> o)
    {
        Player* player = handler->GetSession()->GetPlayer();
        uint32 mapId = id.value_or(player->GetMapId());

        if (z)
        {
            // 如果提供了Z坐标，验证坐标有效性
            if (!MapManager::IsValidMapCoord(mapId, x, y, *z))
            {
                handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, x, y, mapId);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            // 如果没有提供Z坐标，验证XY坐标并自动计算高度
            if (!MapManager::IsValidMapCoord(mapId, x, y))
            {
                handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, x, y, mapId);
                handler->SetSentErrorMessage(true);
                return false;
            }
            Map const* map = sMapMgr->CreateBaseMap(mapId);
            // Z坐标取地形高度或水面高度的较大值
            z = std::max(map->GetHeight(x, y, MAX_HEIGHT), map->GetWaterLevel(x, y));
        }

        // 执行传送，使用提供的朝向或默认值0
        return DoTeleport(handler, { x, y, *z, o.value_or(0.0f) }, mapId);
    }

    /**
     * @brief 传送到工单位置
     * @param handler 聊天处理器
     * @param ticketId 工单ID
     * @return 成功返回true，失败返回false
     *
     * 传送到指定工单创建时玩家的位置。
     * 用于GM处理玩家提交的问题工单。
     *
     * 调用时机：当GM执行 .go ticket <id> 命令时
     */
    static bool HandleGoTicketCommand(ChatHandler* handler, uint32 ticketId)
    {
        // 查询工单数据
        GmTicket* ticket = sTicketMgr->GetTicket(ticketId);
        if (!ticket)
        {
            // 找不到该工单
            handler->SendSysMessage(LANG_COMMAND_TICKETNOTEXIST);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();

        // stop flight if need
        // 如果玩家在飞行路线上，强制结束飞行
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case
            // 保存当前位置用于回传

        // 传送到工单位置
        ticket->TeleportTo(player);
        return true;
    }

    /**
     * @brief 相对位置偏移传送
     * @param handler 聊天处理器
     * @param dX X方向偏移量
     * @param dY 可选的Y方向偏移量，不提供则使用0
     * @param dZ 可选的Z方向偏移量，不提供则使用0
     * @param dO 可选的朝向偏移量，不提供则使用0
     * @return 成功返回true，失败返回false
     *
     * 从当前位置按照指定偏移量进行相对传送。
     * 适用于小范围位置调整。
     *
     * 调用时机：当GM执行 .go offset <dX> [dY] [dZ] [dO] 命令时
     */
    static bool HandleGoOffsetCommand(ChatHandler* handler, float dX, Optional<float> dY, Optional<float> dZ, Optional<float> dO)
    {
        // 获取当前位置
        Position loc = handler->GetSession()->GetPlayer()->GetPosition();
        // 应用偏移量
        loc.RelocateOffset({ dX, dY.value_or(0.0f), dZ.value_or(0.0f), dO.value_or(0.0f) });

        return DoTeleport(handler, loc);
    }

    /**
     * @brief 传送到副本入口
     * @param handler 聊天处理器
     * @param labels 搜索标签列表，用于匹配副本脚本名称
     * @return 成功返回true，失败返回false
     *
     * 根据提供的标签搜索匹配的副本，并传送到副本入口或开始位置。
     * 标签会与副本的脚本名称进行匹配。
     * 如果有多个匹配结果，会显示所有匹配项供用户选择。
     *
     * 调用时机：当GM执行 .go instance <label1> [label2] ... 命令时
     * 性能注意：需要遍历所有副本模板进行匹配
     */
    static bool HandleGoInstanceCommand(ChatHandler* handler, std::vector<std::string_view> labels)
    {
        if (labels.empty())
            return false;

        // #matched labels -> (mapid, scriptname)
        // 使用多重映射存储匹配结果，键为匹配标签数量，值为(地图ID, 地图名称, 脚本名称)
        std::multimap<uint32, std::tuple<uint16, char const*, char const*>> matches;
        for (auto const& pair : sObjectMgr->GetInstanceTemplates())
        {
            uint32 count = 0;
            std::string const& scriptName = sObjectMgr->GetScriptName(pair.second.ScriptId);
            char const* mapName = ASSERT_NOTNULL(sMapStore.LookupEntry(pair.first))->MapName[handler->GetSessionDbcLocale()];
            // 统计匹配的标签数量
            for (std::string_view label : labels)
                if (StringContainsStringI(scriptName, label))
                    ++count;

            // 如果有匹配，添加到结果集中
            if (count)
                matches.emplace(count, decltype(matches)::mapped_type(pair.first, mapName, scriptName.c_str()));
        }
        if (matches.empty())
        {
            // 没有找到匹配的副本
            handler->SendSysMessage(LANG_COMMAND_NO_INSTANCES_MATCH);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查是否有多个相同的最佳匹配
        auto it = matches.crbegin(), end = matches.crend();
        uint32 const maxCount = it->first;
        if ((++it) != end && it->first == maxCount)
        {
            // 有多个相同的最佳匹配，显示所有匹配项
            handler->SendSysMessage(LANG_COMMAND_MULTIPLE_INSTANCES_MATCH);
            --it;
            do
                handler->PSendSysMessage(LANG_COMMAND_MULTIPLE_INSTANCES_ENTRY, std::get<1>(it->second), std::get<0>(it->second), std::get<2>(it->second));
            while (((++it) != end) && (it->first == maxCount));
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取最佳匹配的副本信息
        it = matches.crbegin();
        uint32 const mapId = std::get<0>(it->second);
        char const* const mapName = std::get<1>(it->second);

        Player* player = handler->GetSession()->GetPlayer();
        // 停止飞行并保存回传位置
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition();

        // try going to entrance
        // 尝试传送到副本出口（进入副本的位置）
        if (AreaTrigger const* exit = sObjectMgr->GetGoBackTrigger(mapId))
        {
            if (player->TeleportTo(exit->target_mapId, exit->target_X, exit->target_Y, exit->target_Z, exit->target_Orientation + M_PI))
            {
                // 成功传送到副本入口
                handler->PSendSysMessage(LANG_COMMAND_WENT_TO_INSTANCE_GATE, mapName, mapId);
                return true;
            }
            else
            {
                // 传送到副本入口失败
                uint32 const parentMapId = exit->target_mapId;
                char const* const parentMapName = ASSERT_NOTNULL(sMapStore.LookupEntry(parentMapId))->MapName[handler->GetSessionDbcLocale()];
                handler->PSendSysMessage(LANG_COMMAND_GO_INSTANCE_GATE_FAILED, mapName, mapId, parentMapName, parentMapId);
            }
        }
        else
            // 副本没有出口触发器
            handler->PSendSysMessage(LANG_COMMAND_INSTANCE_NO_EXIT, mapName, mapId);

        // try going to start
        // 尝试传送到副本开始位置
        if (AreaTrigger const* entrance = sObjectMgr->GetMapEntranceTrigger(mapId))
        {
            if (player->TeleportTo(entrance->target_mapId, entrance->target_X, entrance->target_Y, entrance->target_Z, entrance->target_Orientation))
            {
                // 成功传送到副本开始位置
                handler->PSendSysMessage(LANG_COMMAND_WENT_TO_INSTANCE_START, mapName, mapId);
                return true;
            }
            else
                // 传送到副本开始位置失败
                handler->PSendSysMessage(LANG_COMMAND_GO_INSTANCE_START_FAILED, mapName, mapId);
        }
        else
            // 副本没有入口触发器
            handler->PSendSysMessage(LANG_COMMAND_INSTANCE_NO_ENTRANCE, mapName, mapId);

        handler->SetSentErrorMessage(true);
        return false;
    }

    /**
     * @brief 传送到Boss位置
     * @param handler 聊天处理器
     * @param needles 搜索关键词列表，用于匹配Boss名称或脚本名称
     * @return 成功返回true，失败返回false
     *
     * 根据提供的关键词搜索匹配的Boss，并传送到Boss的生成位置。
     * 关键词会与Boss的名称和脚本名称进行匹配。
     * 只搜索标记为副本Boss的生物。
     * 如果有多个匹配结果或Boss有多个生成点，会显示详细信息。
     *
     * 调用时机：当GM执行 .go boss <keyword1> [keyword2] ... 命令时
     * 性能注意：需要遍历所有生物模板和生成点数据
     */
    static bool HandleGoBossCommand(ChatHandler* handler, std::vector<std::string_view> needles)
    {
        if (needles.empty())
            return false;

        // 匹配结果：匹配数量 -> Boss模板指针
        std::multimap<uint32, CreatureTemplate const*> matches;
        // Boss生成点查找表：生物ID -> 生成点列表
        std::unordered_map<uint32, std::vector<CreatureData const*>> spawnLookup;

        // find all boss flagged mobs that match our needles
        // 查找所有标记为副本Boss且匹配关键词的生物
        for (auto const& pair : sObjectMgr->GetCreatureTemplates())
        {
            CreatureTemplate const& data = pair.second;
            // 只搜索标记为副本Boss的生物
            if (!(data.flags_extra & CREATURE_FLAG_EXTRA_DUNGEON_BOSS))
                continue;

            uint32 count = 0;
            std::string const& scriptName = sObjectMgr->GetScriptName(data.ScriptID);
            // 统计匹配的关键词数量（搜索脚本名称和生物名称）
            for (std::string_view label : needles)
                if (StringContainsStringI(scriptName, label) || StringContainsStringI(data.Name, label))
                    ++count;

            if (count)
            {
                matches.emplace(count, &data);
                (void)spawnLookup[data.Entry]; // inserts default-constructed vector
                // 预先生成空的生成点列表
            }
        }

        if (!matches.empty())
        {
            // find the spawn points of any matches
            // 查找所有匹配Boss的生成点
            for (auto const& pair : sObjectMgr->GetAllCreatureData())
            {
                CreatureData const& data = pair.second;
                auto it = spawnLookup.find(data.id);
                if (it != spawnLookup.end())
                    it->second.push_back(&data);  // 添加生成点到列表
            }

            // remove any matches without spawns
            // 移除没有生成点的Boss
            Trinity::Containers::EraseIf(matches, [&spawnLookup](decltype(matches)::value_type const& pair) { return spawnLookup[pair.second->Entry].empty(); });
        }

        // check if we even have any matches left
        // 检查是否还有匹配结果
        if (matches.empty())
        {
            handler->SendSysMessage(LANG_COMMAND_NO_BOSSES_MATCH);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // see if we have multiple equal matches left
        // 检查是否有多个相同的最佳匹配
        auto it = matches.crbegin(), end = matches.crend();
        uint32 const maxCount = it->first;
        if ((++it) != end && it->first == maxCount)
        {
            // 有多个相同的最佳匹配，显示所有匹配项
            handler->SendSysMessage(LANG_COMMAND_MULTIPLE_BOSSES_MATCH);
            --it;
            do
                handler->PSendSysMessage(LANG_COMMAND_MULTIPLE_BOSSES_ENTRY, it->second->Entry, it->second->Name.c_str(), sObjectMgr->GetScriptName(it->second->ScriptID).c_str());
            while (((++it) != end) && (it->first == maxCount));
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取最佳匹配的Boss信息
        CreatureTemplate const* const boss = matches.crbegin()->second;
        std::vector<CreatureData const*> const& spawns = spawnLookup[boss->Entry];
        ASSERT(!spawns.empty());

        // 检查Boss是否有多个生成点
        if (spawns.size() > 1)
        {
            // Boss有多个生成点，显示所有生成点信息
            handler->PSendSysMessage(LANG_COMMAND_BOSS_MULTIPLE_SPAWNS, boss->Name.c_str(), boss->Entry);
            for (CreatureData const* spawn : spawns)
            {
                uint32 const mapId = spawn->mapId;
                MapEntry const* const map = ASSERT_NOTNULL(sMapStore.LookupEntry(mapId));
                handler->PSendSysMessage(LANG_COMMAND_BOSS_MULTIPLE_SPAWN_ETY, spawn->spawnId, mapId, map->MapName[handler->GetSessionDbcLocale()], spawn->spawnPoint.ToString().c_str());
            }
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 准备传送
        Player* player = handler->GetSession()->GetPlayer();
        // 停止飞行并保存回传位置
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition();

        // 传送到Boss位置
        CreatureData const* const spawn = spawns.front();
        uint32 const mapId = spawn->mapId;
        if (!player->TeleportTo({ mapId, spawn->spawnPoint }))
        {
            // 传送失败
            char const* const mapName = ASSERT_NOTNULL(sMapStore.LookupEntry(mapId))->MapName[handler->GetSessionDbcLocale()];
            handler->PSendSysMessage(LANG_COMMAND_GO_BOSS_FAILED, spawn->spawnId, boss->Name.c_str(), boss->Entry, mapName);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 传送成功，显示成功信息
        handler->PSendSysMessage(LANG_COMMAND_WENT_TO_BOSS, boss->Name.c_str(), boss->Entry, spawn->spawnId);
        return true;
    }
};

/**
 * @brief 注册传送命令脚本
 *
 * 此函数用于将传送命令脚本注册到服务器中，
 * 在服务器启动时被调用以初始化命令系统
 */
void AddSC_go_commandscript()
{
    new go_commandscript();
}
