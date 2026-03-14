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

/* ScriptData
Name: tele_commandscript
%Complete: 100
Comment: All tele related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_tele.cpp
 * @brief 传送点管理命令模块
 *
 * 本模块实现了所有与传送点(Teleport)相关的GM命令,包括:
 * - 传送点添加(.tele add)
 * - 传送点删除(.tele del)
 * - 传送到指定位置(.tele)
 * - 传送玩家到指定位置(.tele name)
 * - 传送小队到指定位置(.tele group)
 * - 传送到NPC位置(.tele name npc)
 *
 * 传送点数据存储在game_tele数据库表中,可以被GM用于快速移动。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Group.h"
#include "Language.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @class tele_commandscript
 * @brief 传送点管理命令脚本类
 *
 * 继承自CommandScript,提供传送点管理和传送相关的所有GM命令处理函数。
 * 包括传送点的创建、删除、查询以及玩家传送功能。
 */
class tele_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化传送命令脚本,注册脚本名称为"tele_commandscript"
     */
    tele_commandscript() : CommandScript("tele_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回传送命令表结构
     *
     * 构建并返回所有传送相关命令的层次结构,包括:
     * - tele add: 添加新传送点
     * - tele del: 删除传送点
     * - tele name: 传送指定玩家
     *   - tele name npc: 传送玩家到NPC位置
     *     - id: 通过NPC ID传送
     *     - guid: 通过NPC生成ID传送
     *     - name: 通过NPC名称传送
     * - tele group: 传送小队
     * - tele: 传送到指定传送点
     *
     * @note 此函数在服务器启动时调用一次,构建的命令表会被缓存
     */
    ChatCommandTable GetCommands() const override
    {
        // 传送玩家到NPC的子命令表
        static ChatCommandTable teleNameNpcCommandTable =
        {
            { "id",     HandleTeleNameNpcIdCommand,         rbac::RBAC_PERM_COMMAND_TELE_NAME,  Console::Yes },
            { "guid",   HandleTeleNameNpcSpawnIdCommand,    rbac::RBAC_PERM_COMMAND_TELE_NAME,  Console::Yes },
            { "name",   HandleTeleNameNpcNameCommand,       rbac::RBAC_PERM_COMMAND_TELE_NAME,  Console::Yes },
        };

        // 传送指定玩家的命令表
        static ChatCommandTable teleNameCommandTable =
        {
            { "npc",    teleNameNpcCommandTable },
            { "",       HandleTeleNameCommand,  rbac::RBAC_PERM_COMMAND_TELE_NAME,  Console::Yes },
        };

        // 主传送命令表
        static ChatCommandTable teleCommandTable =
        {
            { "add",    HandleTeleAddCommand,   rbac::RBAC_PERM_COMMAND_TELE_ADD,   Console::No },
            { "del",    HandleTeleDelCommand,   rbac::RBAC_PERM_COMMAND_TELE_DEL,   Console::Yes },
            { "name",   teleNameCommandTable },
            { "group",  HandleTeleGroupCommand, rbac::RBAC_PERM_COMMAND_TELE_GROUP, Console::No },
            { "",       HandleTeleCommand,      rbac::RBAC_PERM_COMMAND_TELE,       Console::No },
        };

        // 根命令表
        static ChatCommandTable commandTable =
        {
            { "tele", teleCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理添加传送点命令
     * @param handler 聊天处理器指针
     * @param name 传送点名称
     * @return true 命令执行成功, false 传送点已存在或添加失败
     *
     * @par 调用时机:
     * 当GM执行.tele add <name>命令时调用
     *
     * @par 功能说明:
     * 在玩家当前位置创建一个新的传送点,并将数据保存到数据库中。
     * 传送点记录玩家的坐标、朝向和地图ID。
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_ADD权限
     * - 只能由游戏内玩家执行(不支持控制台)
     *
     * @par 使用示例:
     * .tele add Stormwind
     * 在当前位置创建名为"Stormwind"的传送点
     */
    static bool HandleTeleAddCommand(ChatHandler* handler, std::string const& name)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        // 检查是否已存在同名传送点
        if (sObjectMgr->GetGameTeleExactName(name))
        {
            handler->SendSysMessage(LANG_COMMAND_TP_ALREADYEXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 创建新的传送点对象并设置属性
        GameTele tele;
        tele.position_x  = player->GetPositionX();      // X坐标
        tele.position_y  = player->GetPositionY();      // Y坐标
        tele.position_z  = player->GetPositionZ();      // Z坐标
        tele.orientation = player->GetOrientation();    // 朝向
        tele.mapId       = player->GetMapId();          // 地图ID
        tele.name        = name;                         // 传送点名称

        // 添加传送点到管理器并保存到数据库
        if (sObjectMgr->AddGameTele(tele))
        {
            handler->SendSysMessage(LANG_COMMAND_TP_ADDED);
        }
        else
        {
            handler->SendSysMessage(LANG_COMMAND_TP_ADDEDERR);
            handler->SetSentErrorMessage(true);
            return false;
        }

        return true;
    }

    /**
     * @brief 处理删除传送点命令
     * @param handler 聊天处理器指针
     * @param tele 要删除的传送点对象指针
     * @return true 命令执行成功, false 传送点不存在
     *
     * @par 调用时机:
     * 当GM执行.tele del <name>命令时调用
     *
     * @par 功能说明:
     * 从数据库中删除指定的传送点
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_DEL权限
     * - 支持控制台执行
     */
    static bool HandleTeleDelCommand(ChatHandler* handler, GameTele const* tele)
    {
        if (!tele)
        {
            handler->SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }
        std::string name = tele->name;
        sObjectMgr->DeleteGameTele(name);
        handler->SendSysMessage(LANG_COMMAND_TP_DELETED);
        return true;
    }

    /**
     * @brief 执行玩家传送的核心逻辑
     * @param handler 聊天处理器指针
     * @param player 目标玩家标识符
     * @param mapId 目标地图ID
     * @param pos 目标位置
     * @param locationName 位置名称(用于日志和消息)
     * @return true 传送成功, false 坐标无效或权限不足
     *
     * @par 功能说明:
     * 执行玩家传送的核心逻辑,支持在线和离线玩家:
     * - 在线玩家:直接传送并保存召回位置
     * - 离线玩家:更新数据库中的位置记录
     *
     * @par 安全检查:
     * - 验证坐标有效性
     * - 检查权限等级
     * - 检查是否正在传送
     * - 处理飞行中的玩家
     *
     * @par 性能注意事项:
     * - 对于离线玩家会执行数据库更新操作
     * - 传送操作会中断正在进行的动作
     */
    static bool DoNameTeleport(ChatHandler* handler, PlayerIdentifier player, uint32 mapId, Position const& pos, std::string const& locationName)
    {
        // 验证坐标有效性,确保不是交通工具地图
        if (!MapManager::IsValidMapCoord(mapId, pos) || sObjectMgr->IsTransportMap(mapId))
        {
            handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, pos.GetPositionX(), pos.GetPositionY(), mapId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 处理在线玩家
        if (Player* target = player.GetConnectedPlayer())
        {
            // 检查权限等级
            // check online security
            if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
                return false;

            std::string chrNameLink = handler->playerLink(target->GetName());

            // 检查玩家是否正在传送中
            if (target->IsBeingTeleported() == true)
            {
                handler->PSendSysMessage(LANG_IS_TELEPORTED, chrNameLink.c_str());
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 发送传送消息
            handler->PSendSysMessage(LANG_TELEPORTING_TO, chrNameLink.c_str(), "", locationName.c_str());
            if (handler->needReportToTarget(target))
                ChatHandler(target->GetSession()).PSendSysMessage(LANG_TELEPORTED_TO_BY, handler->GetNameLink().c_str());

            // 如果玩家在飞行中,结束飞行;否则保存召回位置
            // stop flight if need
            if (target->IsInFlight())
                target->FinishTaxiFlight();
            else
                target->SaveRecallPosition(); // save only in non-flight case

            // 执行传送
            target->TeleportTo({ mapId, pos });
        }
        else
        {
            // 处理离线玩家 - 更新数据库中的位置
            // check offline security
            if (handler->HasLowerSecurity(nullptr, player.GetGUID()))
                return false;

            std::string nameLink = handler->playerLink(player.GetName());

            handler->PSendSysMessage(LANG_TELEPORTING_TO, nameLink.c_str(), handler->GetTrinityString(LANG_OFFLINE), locationName.c_str());

            // 保存位置到数据库
            Player::SavePositionInDB({ mapId, pos }, sMapMgr->GetZoneId(PHASEMASK_NORMAL, { mapId, pos }), player.GetGUID(), nullptr);
        }

        return true;
    }

    /**
     * @brief 处理传送玩家到指定传送点命令
     * @param handler 聊天处理器指针
     * @param player 目标玩家标识符(可选,默认为当前目标或自己)
     * @param where 目标位置,可以是传送点对象或"$home"(回家)
     * @return true 传送成功, false 参数无效
     *
     * @par 调用时机:
     * 当GM执行.tele name <player> <location>命令时调用
     *
     * @par 功能说明:
     * 传送指定玩家到指定位置:
     * - 如果位置是"$home",传送玩家到绑定炉石位置
     * - 否则传送到指定的传送点
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_NAME权限
     * - 支持控制台执行
     */
    // teleport player to given game_tele.entry
    static bool HandleTeleNameCommand(ChatHandler* handler, Optional<PlayerIdentifier> player, Variant<GameTele const*, EXACT_SEQUENCE("$home")> where)
    {
        // 如果没有指定玩家,使用当前目标或自己
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        // 检查是否是传送到炉石绑定点
        if (where.index() == 1)    // References target's homebind
        {
            if (Player* target = player->GetConnectedPlayer())
                // 在线玩家直接传送
                target->TeleportTo(target->m_homebindMapId, target->m_homebindX, target->m_homebindY, target->m_homebindZ, target->GetOrientation());
            else
            {
                // 离线玩家从数据库查询炉石绑定位置
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_HOMEBIND);
                stmt->setUInt32(0, player->GetGUID().GetCounter());
                PreparedQueryResult resultDB = CharacterDatabase.Query(stmt);

                if (resultDB)
                {
                    Field* fieldsDB = resultDB->Fetch();
                    WorldLocation loc(fieldsDB[0].GetUInt16(), fieldsDB[2].GetFloat(), fieldsDB[3].GetFloat(), fieldsDB[4].GetFloat(), 0.0f);
                    uint32 zoneId = fieldsDB[1].GetUInt16();

                    // 更新数据库中的位置
                    Player::SavePositionInDB(loc, zoneId, player->GetGUID(), nullptr);
                }
            }

            return true;
        }

        // 传送到指定传送点
        // id, or string, or [name] Shift-click form |color|Htele:id|h[name]|h|r
        GameTele const* tele = where.get<GameTele const*>();
        return DoNameTeleport(handler, *player, tele->mapId, { tele->position_x, tele->position_y, tele->position_z, tele->orientation }, tele->name);
    }

    /**
     * @brief 处理传送小队命令
     * @param handler 聊天处理器指针
     * @param tele 目标传送点对象指针
     * @return true 传送成功, false 传送点无效或小队不存在
     *
     * @par 调用时机:
     * 当GM执行.tele group <location>命令时调用
     *
     * @par 功能说明:
     * 传送选中玩家所在小队的所有成员到指定位置
     * 只有小队在线成员会被传送
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_GROUP权限
     * - 只能由游戏内玩家执行(不支持控制台)
     */
    //Teleport group to given game_tele.entry
    static bool HandleTeleGroupCommand(ChatHandler* handler, GameTele const* tele)
    {
        if (!tele)
        {
            handler->SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* target = handler->getSelectedPlayer();
        if (!target)
        {
            handler->SendSysMessage(LANG_NO_CHAR_SELECTED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查权限等级
        // check online security
        if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
            return false;

        // 检查目标地图是否有效
        MapEntry const* map = sMapStore.LookupEntry(tele->mapId);
        if (!map || map->IsBattlegroundOrArena())
        {
            handler->SendSysMessage(LANG_CANNOT_TELE_TO_BG);
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string nameLink = handler->GetNameLink(target);

        // 获取小队
        Group* grp = target->GetGroup();
        if (!grp)
        {
            handler->PSendSysMessage(LANG_NOT_IN_GROUP, nameLink.c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 遍历小队成员并传送
        for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* player = itr->GetSource();

            if (!player || !player->GetSession())
                continue;

            // 检查权限等级
            // check online security
            if (handler->HasLowerSecurity(player, ObjectGuid::Empty))
                return false;

            std::string plNameLink = handler->GetNameLink(player);

            // 跳过正在传送的玩家
            if (player->IsBeingTeleported())
            {
                handler->PSendSysMessage(LANG_IS_TELEPORTED, plNameLink.c_str());
                continue;
            }

            // 发送传送消息
            handler->PSendSysMessage(LANG_TELEPORTING_TO, plNameLink.c_str(), "", tele->name.c_str());
            if (handler->needReportToTarget(player))
                ChatHandler(player->GetSession()).PSendSysMessage(LANG_TELEPORTED_TO_BY, nameLink.c_str());

            // 处理飞行中的玩家
            // stop flight if need
            if (player->IsInFlight())
                player->FinishTaxiFlight();
            else
                player->SaveRecallPosition(); // save only in non-flight case

            // 执行传送
            player->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
        }

        return true;
    }

    /**
     * @brief 处理传送到指定传送点命令
     * @param handler 聊天处理器指针
     * @param tele 目标传送点对象指针
     * @return true 传送成功, false 传送点无效或在战斗中
     *
     * @par 调用时机:
     * 当GM执行.tele <location>命令时调用
     *
     * @par 功能说明:
     * 将执行命令的玩家传送到指定的传送点
     *
     * @par 限制条件:
     * - 如果没有RBAC_PERM_COMMAND_TELE_NAME权限,在战斗中无法传送
     * - 不能传送到战场或竞技场(除非已经在该地图且是GM)
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE权限
     * - 只能由游戏内玩家执行(不支持控制台)
     */
    static bool HandleTeleCommand(ChatHandler* handler, GameTele const* tele)
    {
        if (!tele)
        {
            handler->SendSysMessage(LANG_COMMAND_TELE_NOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        // 检查战斗状态 - 普通玩家在战斗中不能传送
        if (player->IsInCombat() && !handler->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_TELE_NAME))
        {
            handler->SendSysMessage(LANG_YOU_IN_COMBAT);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查地图有效性
        MapEntry const* map = sMapStore.LookupEntry(tele->mapId);
        if (!map || (map->IsBattlegroundOrArena() && (player->GetMapId() != tele->mapId || !player->IsGameMaster())))
        {
            handler->SendSysMessage(LANG_CANNOT_TELE_TO_BG);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 处理飞行中的玩家
        // stop flight if need
        if (player->IsInFlight())
            player->FinishTaxiFlight();
        else
            player->SaveRecallPosition(); // save only in non-flight case

        // 执行传送
        player->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
        return true;
    }

    /**
     * @brief 处理传送到NPC位置命令(通过NPC ID)
     * @param handler 聊天处理器指针
     * @param player 目标玩家标识符
     * @param creatureId NPC的ID(生物模板ID)
     * @return true 传送成功, false NPC不存在或多个NPC
     *
     * @par 调用时机:
     * 当GM执行.tele name npc id <player> <creatureId>命令时调用
     *
     * @par 功能说明:
     * 传送指定玩家到指定ID的NPC的生成位置
     * 如果有多个相同ID的NPC,会提示并传送到第一个
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_NAME权限
     * - 支持控制台执行
     */
    static bool HandleTeleNameNpcIdCommand(ChatHandler* handler, PlayerIdentifier player, Variant<Hyperlink<creature_entry>, uint32> creatureId)
    {
        CreatureData const* spawnpoint = nullptr;
        // 遍历所有生物数据查找指定ID的生物
        for (auto const& pair : sObjectMgr->GetAllCreatureData())
        {
            if (pair.second.id != *creatureId)
                continue;

            if (!spawnpoint)
                spawnpoint = &pair.second;
            else
            {
                // 找到多个相同ID的生物
                handler->SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);
                break;
            }
        }

        if (!spawnpoint)
        {
            handler->SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取生物模板
        CreatureTemplate const* creatureTemplate = ASSERT_NOTNULL(sObjectMgr->GetCreatureTemplate(*creatureId));

        // 执行传送
        return DoNameTeleport(handler, player, spawnpoint->mapId, spawnpoint->spawnPoint, creatureTemplate->Name);
    }

    /**
     * @brief 处理传送到NPC位置命令(通过生成ID)
     * @param handler 聊天处理器指针
     * @param player 目标玩家标识符
     * @param spawnId NPC的生成ID(数据库中的GUID)
     * @return true 传送成功, false NPC不存在
     *
     * @par 调用时机:
     * 当GM执行.tele name npc guid <player> <spawnId>命令时调用
     *
     * @par 功能说明:
     * 传送指定玩家到指定生成ID的NPC位置
     * 这是精确的NPC定位,不会混淆
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_NAME权限
     * - 支持控制台执行
     */
    static bool HandleTeleNameNpcSpawnIdCommand(ChatHandler* handler, PlayerIdentifier player, Variant<Hyperlink<creature>, ObjectGuid::LowType> spawnId)
    {
        // 通过生成ID获取生物数据
        CreatureData const* spawnpoint = sObjectMgr->GetCreatureData(spawnId);
        if (!spawnpoint)
        {
            handler->SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 获取生物模板
        CreatureTemplate const* creatureTemplate = ASSERT_NOTNULL(sObjectMgr->GetCreatureTemplate(spawnpoint->id));

        // 执行传送
        return DoNameTeleport(handler, player, spawnpoint->mapId, spawnpoint->spawnPoint, creatureTemplate->Name);
    }

    /**
     * @brief 处理传送到NPC位置命令(通过NPC名称)
     * @param handler 聊天处理器指针
     * @param player 目标玩家标识符
     * @param name NPC的名称
     * @return true 传送成功, false NPC不存在
     *
     * @par 调用时机:
     * 当GM执行.tele name npc name <player> <npcName>命令时调用
     *
     * @par 功能说明:
     * 传送指定玩家到指定名称的NPC位置
     * 使用模糊匹配,会传送到第一个匹配的NPC
     *
     * @par 权限要求:
     * - 需要RBAC_PERM_COMMAND_TELE_NAME权限
     * - 支持控制台执行
     */
    static bool HandleTeleNameNpcNameCommand(ChatHandler* handler, PlayerIdentifier player, Tail name)
    {
        // 转义名称字符串以防止SQL注入
        std::string normalizedName(name);
        WorldDatabase.EscapeString(normalizedName);

        // 查询数据库获取NPC位置信息
        QueryResult result = WorldDatabase.PQuery("SELECT c.position_x, c.position_y, c.position_z, c.orientation, c.map, ct.name FROM creature c INNER JOIN creature_template ct ON c.id = ct.entry WHERE ct.name LIKE '{}'", normalizedName);
        if (!result)
        {
            handler->SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果有多个匹配,提示用户
        if (result->GetRowCount() > 1)
            handler->SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);

        // 执行传送
        Field* fields = result->Fetch();
        return DoNameTeleport(handler, player, fields[4].GetUInt16(), { fields[0].GetFloat(), fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat() }, fields[5].GetString());
    }
};

/**
 * @brief 注册传送命令脚本
 *
 * 此函数在服务器启动时被脚本系统调用,用于注册传送命令脚本。
 * 创建tele_commandscript实例并将其添加到命令处理系统中。
 */
void AddSC_tele_commandscript()
{
    new tele_commandscript();
}
