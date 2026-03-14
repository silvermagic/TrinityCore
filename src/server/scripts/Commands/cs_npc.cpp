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
Name: npc_commandscript
%Complete: 100
Comment: All npc related commands
Category: commandscripts
EndScriptData */

/**
 * @file cs_npc.cpp
 * @brief NPC管理命令模块
 *
 * 本模块提供了一系列GM命令，用于创建、修改、删除和管理NPC（非玩家角色）。
 * 主要功能包括：
 * - NPC创建与删除：添加新NPC到世界、删除现有NPC
 * - NPC属性设置：等级、阵营、模型、标志、移动类型等
 * - NPC行为控制：跟随、移动、播放表情、说话等
 * - NPC信息查询：显示NPC详细信息、附近NPC列表
 * - 商人管理：添加/删除商品列表中的物品
 * - 生成组管理：批量生成/消除NPC组
 *
 * 这些命令主要用于世界构建、内容测试和服务器管理。
 * 所有命令都需要相应的RBAC权限，且大部分仅限游戏内使用。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "CreatureAI.h"
#include "CreatureGroups.h"
#include "DatabaseEnv.h"
#include "FollowMovementGenerator.h"
#include "GameTime.h"
#include "Language.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementDefines.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "SmartEnum.h"
#include "Transport.h"
#include "World.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @brief 生物生成ID类型定义
 *
 * 支持通过超链接或数值指定生物的生成ID
 */
using CreatureSpawnId = Variant<Hyperlink<creature>, ObjectGuid::LowType>;

/**
 * @brief 生物模板ID类型定义
 *
 * 支持通过超链接或数值指定生物的模板ID
 */
using CreatureEntry = Variant<Hyperlink<creature_entry>, uint32>;

// shared with cs_gobject.cpp, definitions are at the bottom of this file
// 与 cs_gobject.cpp 共享的函数声明，定义在文件底部
bool HandleNpcSpawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("force"), EXACT_SEQUENCE("ignorerespawn")>> const& opts);
bool HandleNpcDespawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("removerespawntime")>> const& opts);

/**
 * @class npc_commandscript
 * @brief NPC命令脚本类
 *
 * 该类继承自CommandScript，提供所有与NPC管理相关的GM命令。
 * 命令分为几个类别：
 * - npc add: 添加NPC相关（创建NPC、添加商人物品、设置移动路径等）
 * - npc set: 设置NPC属性（等级、阵营、模型、标志等）
 * - npc: 其他命令（信息查询、跟随、说话、删除等）
 *
 * 所有命令都需要相应的RBAC权限，且大部分仅限游戏内使用（Console::No）。
 */
class npc_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化NPC命令脚本，注册命令名称为"npc_commandscript"
     */
    npc_commandscript() : CommandScript("npc_commandscript") { }

    /**
     * @brief 获取所有NPC命令的命令表
     * @return 返回命令表，包含所有NPC相关命令的定义
     *
     * 该函数注册了以下命令类别：
     * - npcAddCommandTable: NPC添加命令
     *   - npc add formation: 添加阵型成员
     *   - npc add item: 添加商人物品
     *   - npc add move: 添加移动路径点
     *   - npc add temp: 添加临时NPC
     *   - npc add: 创建NPC
     *
     * - npcSetCommandTable: NPC设置命令
     *   - npc set allowmove: 允许/禁止移动
     *   - npc set entry: 设置模板ID
     *   - npc set factionid: 设置阵营
     *   - npc set flag: 设置NPC标志
     *   - npc set level: 设置等级
     *   - npc set link: 设置链接
     *   - npc set model: 设置模型
     *   - npc set movetype: 设置移动类型
     *   - npc set phase: 设置阶段
     *   - npc set wanderdistance: 设置游荡距离
     *   - npc set spawntime: 设置生成时间
     *   - npc set data: 设置数据
     *
     * - npcCommandTable: 其他NPC命令
     *   - npc info: 显示NPC信息
     *   - npc near: 显示附近NPC
     *   - npc move: 移动NPC
     *   - npc playemote: 播放表情
     *   - npc say/textemote/whisper/yell: 说话相关
     *   - npc tame: 驯服
     *   - npc spawngroup/despawngroup: 生成组管理
     *   - npc delete: 删除NPC
     *   - npc follow/follow stop: 跟随控制
     *   - npc evade: 重置战斗
     *   - npc showloot: 显示战利品
     */
    ChatCommandTable GetCommands() const override
    {
        // NPC添加命令子表
        static ChatCommandTable npcAddCommandTable =
        {
            { "formation",      HandleNpcAddFormationCommand,      rbac::RBAC_PERM_COMMAND_NPC_ADD_FORMATION,  Console::No },
            { "item",           HandleNpcAddVendorItemCommand,     rbac::RBAC_PERM_COMMAND_NPC_ADD_ITEM,       Console::No },
            { "move",           HandleNpcAddMoveCommand,           rbac::RBAC_PERM_COMMAND_NPC_ADD_MOVE,       Console::No },
            { "temp",           HandleNpcAddTempSpawnCommand,      rbac::RBAC_PERM_COMMAND_NPC_ADD_TEMP,       Console::No },
//          { "weapon",         HandleNpcAddWeaponCommand,         rbac::RBAC_PERM_COMMAND_NPC_ADD_WEAPON,     Console::No },
            { "",               HandleNpcAddCommand,               rbac::RBAC_PERM_COMMAND_NPC_ADD,            Console::No },
        };
        // NPC设置命令子表
        static ChatCommandTable npcSetCommandTable =
        {
            { "allowmove",      HandleNpcSetAllowMovementCommand,  rbac::RBAC_PERM_COMMAND_NPC_SET_ALLOWMOVE,  Console::No },
            { "entry",          HandleNpcSetEntryCommand,          rbac::RBAC_PERM_COMMAND_NPC_SET_ENTRY,      Console::No },
            { "factionid",      HandleNpcSetFactionIdCommand,      rbac::RBAC_PERM_COMMAND_NPC_SET_FACTIONID,  Console::No },
            { "flag",           HandleNpcSetFlagCommand,           rbac::RBAC_PERM_COMMAND_NPC_SET_FLAG,       Console::No },
            { "level",          HandleNpcSetLevelCommand,          rbac::RBAC_PERM_COMMAND_NPC_SET_LEVEL,      Console::No },
            { "link",           HandleNpcSetLinkCommand,           rbac::RBAC_PERM_COMMAND_NPC_SET_LINK,       Console::No },
            { "model",          HandleNpcSetModelCommand,          rbac::RBAC_PERM_COMMAND_NPC_SET_MODEL,      Console::No },
            { "movetype",       HandleNpcSetMoveTypeCommand,       rbac::RBAC_PERM_COMMAND_NPC_SET_MOVETYPE,   Console::No },
            { "phase",          HandleNpcSetPhaseCommand,          rbac::RBAC_PERM_COMMAND_NPC_SET_PHASE,      Console::No },
            { "wanderdistance", HandleNpcSetWanderDistanceCommand, rbac::RBAC_PERM_COMMAND_NPC_SET_SPAWNDIST,  Console::No },
            { "spawntime",      HandleNpcSetSpawnTimeCommand,      rbac::RBAC_PERM_COMMAND_NPC_SET_SPAWNTIME,  Console::No },
            { "data",           HandleNpcSetDataCommand,           rbac::RBAC_PERM_COMMAND_NPC_SET_DATA,       Console::No },
        };
        // NPC主命令表
        static ChatCommandTable npcCommandTable =
        {
            { "add", npcAddCommandTable },
            { "set", npcSetCommandTable },
            { "info",           HandleNpcInfoCommand,              rbac::RBAC_PERM_COMMAND_NPC_INFO,           Console::No },
            { "near",           HandleNpcNearCommand,              rbac::RBAC_PERM_COMMAND_NPC_NEAR,           Console::No },
            { "move",           HandleNpcMoveCommand,              rbac::RBAC_PERM_COMMAND_NPC_MOVE,           Console::No },
            { "playemote",      HandleNpcPlayEmoteCommand,         rbac::RBAC_PERM_COMMAND_NPC_PLAYEMOTE,      Console::No },
            { "say",            HandleNpcSayCommand,               rbac::RBAC_PERM_COMMAND_NPC_SAY,            Console::No },
            { "textemote",      HandleNpcTextEmoteCommand,         rbac::RBAC_PERM_COMMAND_NPC_TEXTEMOTE,      Console::No },
            { "whisper",        HandleNpcWhisperCommand,           rbac::RBAC_PERM_COMMAND_NPC_WHISPER,        Console::No },
            { "yell",           HandleNpcYellCommand,              rbac::RBAC_PERM_COMMAND_NPC_YELL,           Console::No },
            { "tame",           HandleNpcTameCommand,              rbac::RBAC_PERM_COMMAND_NPC_TAME,           Console::No },
            { "spawngroup",     HandleNpcSpawnGroup,               rbac::RBAC_PERM_COMMAND_NPC_SPAWNGROUP,     Console::No },
            { "despawngroup",   HandleNpcDespawnGroup,             rbac::RBAC_PERM_COMMAND_NPC_DESPAWNGROUP,   Console::No },
            { "delete",         HandleNpcDeleteCommand,            rbac::RBAC_PERM_COMMAND_NPC_DELETE,         Console::No },
            { "delete item",    HandleNpcDeleteVendorItemCommand,  rbac::RBAC_PERM_COMMAND_NPC_DELETE_ITEM,    Console::No },
            { "follow",         HandleNpcFollowCommand,            rbac::RBAC_PERM_COMMAND_NPC_FOLLOW,         Console::No },
            { "follow stop",    HandleNpcUnFollowCommand,          rbac::RBAC_PERM_COMMAND_NPC_FOLLOW,         Console::No },
            { "evade",          HandleNpcEvadeCommand,             rbac::RBAC_PERM_COMMAND_NPC_EVADE,          Console::No },
            { "showloot",       HandleNpcShowLootCommand,          rbac::RBAC_PERM_COMMAND_NPC_SHOWLOOT,       Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "npc", npcCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 创建NPC命令处理函数
     * @param handler 聊天处理器指针
     * @param id 生物模板ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc add <模板ID> - 在玩家当前位置创建指定类型的NPC
     *
     * 执行流程：
     * 1. 验证生物模板是否存在
     * 2. 如果玩家在交通工具上，创建NPC乘客并保存到数据库
     * 3. 否则，创建普通NPC：
     *    - 生成唯一GUID
     *    - 创建生物对象
     *    - 保存到数据库
     *    - 重新从数据库加载以确保正确初始化
     *    - 将生物添加到网格系统
     *
     * @note 创建的NPC会持久化到数据库，服务器重启后仍然存在
     *       NPC的位置、阶段掩码等属性基于玩家当前状态
     */
    //add spawn of creature
    static bool HandleNpcAddCommand(ChatHandler* handler, CreatureEntry id)
    {
        if (!sObjectMgr->GetCreatureTemplate(id))
            return false;

        Player* chr = handler->GetSession()->GetPlayer();
        Map* map = chr->GetMap();

        if (Transport* trans = chr->GetTransport())
        {
            ObjectGuid::LowType guid = sObjectMgr->GenerateCreatureSpawnId();
            CreatureData& data = sObjectMgr->NewOrExistCreatureData(guid);
            data.spawnId = guid;
            data.spawnGroupData = sObjectMgr->GetDefaultSpawnGroup();
            data.id = id;
            data.phaseMask = chr->GetPhaseMaskForSpawn();
            data.spawnPoint.Relocate(chr->GetTransOffsetX(), chr->GetTransOffsetY(), chr->GetTransOffsetZ(), chr->GetTransOffsetO());
            if (Creature* creature = trans->CreateNPCPassenger(guid, &data))
            {
                creature->SaveToDB(trans->GetGOInfo()->moTransport.mapID, 1 << map->GetSpawnMode(), chr->GetPhaseMaskForSpawn());
                sObjectMgr->AddCreatureToGrid(guid, &data);
            }
            return true;
        }

        Creature* creature = new Creature();
        if (!creature->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, chr->GetPhaseMaskForSpawn(), id, *chr))
        {
            delete creature;
            return false;
        }

        creature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), chr->GetPhaseMaskForSpawn());

        ObjectGuid::LowType db_guid = creature->GetSpawnId();

        // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells()
        // current "creature" variable is deleted and created fresh new, otherwise old values might trigger asserts or cause undefined behavior
        creature->CleanupsBeforeDelete();
        delete creature;
        creature = new Creature();
        if (!creature->LoadFromDB(db_guid, map, true, true))
        {
            delete creature;
            return false;
        }

        sObjectMgr->AddCreatureToGrid(db_guid, sObjectMgr->GetCreatureData(db_guid));
        return true;
    }

    /**
     * @brief 添加商人物品命令处理函数
     * @param handler 聊天处理器指针
     * @param item 物品模板指针
     * @param mc 最大库存数量（可选）
     * @param it 补充时间（可选，秒）
     * @param ec 扩展花费ID（可选）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc add item <物品ID> [最大数量] [补充时间] [扩展花费]
     *
     * 参数说明：
     * - 物品ID: 要添加到商品列表的物品
     * - 最大数量: 0表示无限供应，否则为限量商品
     * - 补充时间: 限量商品的补充时间（秒）
     * - 扩展花费: 特殊货币花费ID（如荣誉点、竞技场点等）
     *
     * 执行流程：
     * 1. 验证物品是否存在
     * 2. 获取选中的商人NPC
     * 3. 验证商品数据有效性
     * 4. 添加物品到NPC的商品列表
     *
     * @note 选中的NPC必须具有商人的NPC标志
     */
    //add item in vendorlist
    static bool HandleNpcAddVendorItemCommand(ChatHandler* handler, ItemTemplate const* item, Optional<uint32> mc, Optional<uint32> it, Optional<uint32> ec)
    {
        if (!item)
        {
            handler->SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Creature* vendor = handler->getSelectedCreature();
        if (!vendor)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 itemId = item->ItemId;
        uint32 maxcount = mc.value_or(0);
        uint32 incrtime = it.value_or(0);
        uint32 extendedcost = ec.value_or(0);
        uint32 vendor_entry = vendor->GetEntry();

        if (!sObjectMgr->IsVendorItemValid(vendor_entry, itemId, maxcount, incrtime, extendedcost, handler->GetSession()->GetPlayer()))
        {
            handler->SetSentErrorMessage(true);
            return false;
        }

        sObjectMgr->AddVendorItem(vendor_entry, itemId, maxcount, incrtime, extendedcost);

        handler->PSendSysMessage(LANG_ITEM_ADDED_TO_LIST, itemId, item->Name1.c_str(), maxcount, incrtime, extendedcost);
        return true;
    }

    /**
     * @brief 添加NPC移动路径点命令处理函数
     * @param handler 聊天处理器指针
     * @param lowGuid 生物生成ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc add move <生成ID> - 为NPC设置路径点移动类型
     *
     * 该命令将NPC的移动类型设置为WAYPOINT_MOTION_TYPE，
     * 使NPC按照预设的路径点进行移动。
     *
     * 执行流程：
     * 1. 验证生物是否存在
     * 2. 更新数据库中的移动类型为路径点移动
     *
     * @note 需要先设置路径点，NPC才会真正移动
     */
    //add move for creature
    static bool HandleNpcAddMoveCommand(ChatHandler* handler, CreatureSpawnId lowGuid)
    {
        // attempt check creature existence by DB data
        CreatureData const* data = sObjectMgr->GetCreatureData(lowGuid);
        if (!data)
        {
            handler->PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowGuid);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Update movement type
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_MOVEMENT_TYPE);

        stmt->setUInt8(0, uint8(WAYPOINT_MOTION_TYPE));
        stmt->setUInt32(1, lowGuid);

        WorldDatabase.Execute(stmt);

        handler->SendSysMessage(LANG_WAYPOINT_ADDED);

        return true;
    }

    /**
     * @brief 允许/禁止NPC移动命令处理函数
     * @param handler 聊天处理器指针
     * @return 始终返回true
     *
     * .npc set allowmove - 切换全服NPC移动开关
     *
     * 该命令用于全局控制所有NPC的移动行为：
     * - 如果当前允许移动，则禁止
     * - 如果当前禁止移动，则允许
     *
     * @note 这是一个全局设置，影响服务器上所有NPC的移动
     *       主要用于调试或特殊事件期间冻结所有NPC
     */
    static bool HandleNpcSetAllowMovementCommand(ChatHandler* handler)
    {
        if (sWorld->getAllowMovement())
        {
            sWorld->SetAllowMovement(false);
            handler->SendSysMessage(LANG_CREATURE_MOVE_DISABLED);
        }
        else
        {
            sWorld->SetAllowMovement(true);
            handler->SendSysMessage(LANG_CREATURE_MOVE_ENABLED);
        }
        return true;
    }

    /**
     * @brief 设置NPC模板ID命令处理函数
     * @param handler 聊天处理器指针
     * @param newEntryNum 新的模板ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set entry <模板ID> - 将选中的NPC更改为新的模板类型
     *
     * 该命令会更新NPC的所有属性为新模板的默认值，
     * 包括模型、属性、技能等。
     *
     * @note 这是一个临时修改，不会保存到数据库
     *       重启服务器或NPC重生后会恢复原模板
     */
    static bool HandleNpcSetEntryCommand(ChatHandler* handler, CreatureEntry newEntryNum)
    {
        if (!newEntryNum)
            return false;

        Unit* unit = handler->getSelectedUnit();
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }
        Creature* creature = unit->ToCreature();
        if (creature->UpdateEntry(newEntryNum))
            handler->SendSysMessage(LANG_DONE);
        else
            handler->SendSysMessage(LANG_ERROR);
        return true;
    }

    /**
     * @brief 设置NPC等级命令处理函数
     * @param handler 聊天处理器指针
     * @param lvl 新等级（1到最大玩家等级+3）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set level <等级> - 设置选中NPC的等级
     *
     * 执行流程：
     * 1. 验证等级是否在有效范围内
     * 2. 获取选中的NPC（不能是宠物）
     * 3. 设置NPC的等级和生命值
     * 4. 保存到数据库
     *
     * @note 等级会影响NPC的属性，生命值会被重新计算
     */
    //change level of creature or pet
    static bool HandleNpcSetLevelCommand(ChatHandler* handler, uint8 lvl)
    {
        if (lvl < 1 || lvl > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) + 3)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Creature* creature = handler->getSelectedCreature();
        if (!creature || creature->IsPet())
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->SetMaxHealth(100 + 30*lvl);
        creature->SetHealth(100 + 30*lvl);
        creature->SetLevel(lvl);
        creature->SaveToDB();

        return true;
    }

    /**
     * @brief 删除NPC命令处理函数
     * @param handler 聊天处理器指针
     * @param spawnIdArg 生物生成ID（可选）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc delete [生成ID] - 删除选中或指定ID的NPC
     *
     * 执行流程：
     * 1. 如果提供了生成ID，直接使用该ID
     * 2. 否则使用选中的NPC
     * 3. 如果是临时召唤物，执行反召唤
     * 4. 否则从数据库中删除该NPC
     *
     * @note 删除操作会从世界中移除NPC并从数据库中删除记录
     *       此操作不可逆，请谨慎使用
     */
    static bool HandleNpcDeleteCommand(ChatHandler* handler, Optional<CreatureSpawnId> spawnIdArg)
    {
        ObjectGuid::LowType spawnId;
        if (spawnIdArg)
            spawnId = *spawnIdArg;
        else
        {
            Creature* creature = handler->getSelectedCreature();
            if (!creature || creature->IsPet() || creature->IsTotem())
            {
                handler->SendSysMessage(LANG_SELECT_CREATURE);
                handler->SetSentErrorMessage(true);
                return false;
            }
            if (TempSummon* summon = creature->ToTempSummon())
            {
                summon->UnSummon();
                handler->SendSysMessage(LANG_COMMAND_DELCREATMESSAGE);
                return true;
            }
            spawnId = creature->GetSpawnId();
        }

        if (Creature::DeleteFromDB(spawnId))
        {
            handler->SendSysMessage(LANG_COMMAND_DELCREATMESSAGE);
            return true;
        }
        else
        {
            handler->PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, spawnId);
            handler->SetSentErrorMessage(true);
            return false;
        }
    }

    /**
     * @brief 删除商人物品命令处理函数
     * @param handler 聊天处理器指针
     * @param item 要删除的物品模板指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc delete item <物品ID> - 从选中商人的商品列表中删除指定物品
     *
     * 执行流程：
     * 1. 验证选中的NPC是否是商人
     * 2. 验证物品是否存在
     * 3. 从商人的商品列表中移除物品
     *
     * @note 选中的NPC必须具有商人的NPC标志
     */
    //del item from vendor list
    static bool HandleNpcDeleteVendorItemCommand(ChatHandler* handler, ItemTemplate const* item)
    {
        Creature* vendor = handler->getSelectedCreature();
        if (!vendor || !vendor->IsVendor())
        {
            handler->SendSysMessage(LANG_COMMAND_VENDORSELECTION);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!item)
        {
            handler->SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint32 itemId = item->ItemId;
        if (!sObjectMgr->RemoveVendorItem(vendor->GetEntry(), itemId))
        {
            handler->PSendSysMessage(LANG_ITEM_NOT_IN_LIST, itemId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage(LANG_ITEM_DELETED_FROM_LIST, itemId, item->Name1.c_str());
        return true;
    }

    /**
     * @brief 设置NPC阵营命令处理函数
     * @param handler 聊天处理器指针
     * @param factionId 阵营模板ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set factionid <阵营ID> - 设置选中NPC的阵营
     *
     * 执行流程：
     * 1. 验证阵营ID是否有效
     * 2. 获取选中的NPC
     * 3. 更新内存中的阵营
     * 4. 更新模板数据
     * 5. 更新数据库记录
     *
     * @note 阵营决定NPC对玩家和其他NPC的敌对/友好关系
     *       阵营ID定义在 FactionTemplate.dbc 中
     */
    //set faction of creature
    static bool HandleNpcSetFactionIdCommand(ChatHandler* handler, uint32 factionId)
    {
        if (!sFactionTemplateStore.LookupEntry(factionId))
        {
            handler->PSendSysMessage(LANG_WRONG_FACTION, factionId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->SetFaction(factionId);

        // Faction is set in creature_template - not inside creature

        // Update in memory..
        if (CreatureTemplate const* cinfo = creature->GetCreatureTemplate())
            const_cast<CreatureTemplate*>(cinfo)->faction = factionId;

        // ..and DB
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_FACTION);

        stmt->setUInt16(0, uint16(factionId));
        stmt->setUInt32(1, creature->GetEntry());

        WorldDatabase.Execute(stmt);

        return true;
    }

    /**
     * @brief 设置NPC标志命令处理函数
     * @param handler 聊天处理器指针
     * @param npcFlags NPC标志位掩码
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set flag <标志> - 设置选中NPC的NPC标志
     *
     * NPC标志决定NPC的功能类型，例如：
     * - NPC_FLAG_GOSSIP: 可以对话
     * - NPC_FLAG_QUESTGIVER: 任务NPC
     * - NPC_FLAG_VENDOR: 商人
     * - NPC_FLAG_TRAINER: 训练师
     * - NPC_FLAG_FLIGHTMASTER: 飞行管理员
     * 等等
     *
     * 执行流程：
     * 1. 获取选中的NPC
     * 2. 设置新的NPC标志
     * 3. 更新数据库
     *
     * @note 修改后需要重新进入游戏才能看到效果
     */
    //set npcflag of creature
    static bool HandleNpcSetFlagCommand(ChatHandler* handler, NPCFlags npcFlags)
    {
        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->ReplaceAllNpcFlags(npcFlags);

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_NPCFLAG);

        stmt->setUInt32(0, npcFlags);
        stmt->setUInt32(1, creature->GetEntry());

        WorldDatabase.Execute(stmt);

        handler->SendSysMessage(LANG_VALUE_SAVED_REJOIN);

        return true;
    }

    /**
     * @brief 设置NPC数据命令处理函数（用于脚本测试）
     * @param handler 聊天处理器指针
     * @param data_1 数据1
     * @param data_2 数据2
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set data <数据1> <数据2> - 向NPC的AI发送数据
     *
     * 该命令用于脚本调试，可以向NPC的AI传递自定义数据。
     * AI脚本可以通过SetData/GetData接口接收和使用这些数据。
     *
     * 执行流程：
     * 1. 获取选中的NPC
     * 2. 调用NPC的AI的SetData方法
     * 3. 显示NPC的AI类型或脚本名称
     *
     * @note 主要用于开发和调试AI脚本
     */
    //set data of creature for testing scripting
    static bool HandleNpcSetDataCommand(ChatHandler* handler, uint32 data_1, uint32 data_2)
    {
        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->AI()->SetData(data_1, data_2);
        std::string AIorScript = !creature->GetAIName().empty() ? "AI type: " + creature->GetAIName() : (!creature->GetScriptName().empty() ? "Script Name: " + creature->GetScriptName() : "No AI or Script Name Set");
        handler->PSendSysMessage(LANG_NPC_SETDATA, creature->GetGUID().GetCounter(), creature->GetEntry(), creature->GetName().c_str(), data_1, data_2, AIorScript.c_str());
        return true;
    }

    /**
     * @brief 让NPC跟随玩家命令处理函数
     * @param handler 聊天处理器指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc follow - 让选中的NPC跟随自己
     *
     * 执行流程：
     * 1. 获取玩家和选中的NPC
     * 2. 设置NPC的移动模式为跟随玩家
     * 3. 使用宠物的默认跟随距离和角度
     *
     * @note 这是一个临时效果，NPC重生后会恢复原行为
     */
    //npc follow handling
    static bool HandleNpcFollowCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->PSendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // Follow player - Using pet's default dist and angle
        creature->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, creature->GetFollowAngle());

        handler->PSendSysMessage(LANG_CREATURE_FOLLOW_YOU_NOW, creature->GetName().c_str());
        return true;
    }

    /**
     * @brief 显示NPC详细信息命令处理函数
     * @param handler 聊天处理器指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc info - 显示选中NPC的详细信息
     *
     * 显示的信息包括：
     * - 基本信息：名称、生成ID、GUID、模板ID、阵营、NPC标志、模型ID
     * - 生成组信息：组名、组ID、组标志、激活状态
     * - 重生信息：兼容模式、重生延迟
     * - 等级和装备信息
     * - 生命值信息
     * - 移动模板数据
     * - 单位标志和动态标志
     * - 战利品ID（普通战利品、偷窃战利品、剥皮战利品）
     * - 副本ID、阶段掩码、护甲值
     * - 位置坐标
     * - AI信息：AI名称、脚本名称、AI类型、反应状态
     * - 额外标志和机制免疫掩码
     *
     * @note 这是一个非常有用的调试和信息查询命令
     */
    static bool HandleNpcInfoCommand(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();

        if (!target)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        CreatureTemplate const* cInfo = target->GetCreatureTemplate();

        uint32 faction = target->GetFaction();
        uint32 npcflags = target->GetNpcFlags();
        uint32 mechanicImmuneMask = cInfo->MechanicImmuneMask;
        uint32 displayid = target->GetDisplayId();
        uint32 nativeid = target->GetNativeDisplayId();
        uint32 entry = target->GetEntry();

        int64 curRespawnDelay = target->GetRespawnCompatibilityMode() ? target->GetRespawnTimeEx() - GameTime::GetGameTime() : target->GetMap()->GetCreatureRespawnTime(target->GetSpawnId()) - GameTime::GetGameTime();

        if (curRespawnDelay < 0)
            curRespawnDelay = 0;
        std::string curRespawnDelayStr = secsToTimeString(uint64(curRespawnDelay), TimeFormat::ShortText);
        std::string defRespawnDelayStr = secsToTimeString(target->GetRespawnDelay(), TimeFormat::ShortText);

        handler->PSendSysMessage(LANG_NPCINFO_CHAR, target->GetName().c_str(), target->GetSpawnId(), target->GetGUID().GetCounter(), entry, faction, npcflags, displayid, nativeid);
        if (target->GetCreatureData() && target->GetCreatureData()->spawnGroupData->groupId)
        {
            SpawnGroupTemplateData const* const groupData = target->GetCreatureData()->spawnGroupData;
            handler->PSendSysMessage(LANG_SPAWNINFO_GROUP_ID, groupData->name.c_str(), groupData->groupId, groupData->flags, target->GetMap()->IsSpawnGroupActive(groupData->groupId));
        }
        handler->PSendSysMessage(LANG_SPAWNINFO_COMPATIBILITY_MODE, target->GetRespawnCompatibilityMode());
        handler->PSendSysMessage(LANG_NPCINFO_LEVEL, target->GetLevel());
        handler->PSendSysMessage(LANG_NPCINFO_EQUIPMENT, target->GetCurrentEquipmentId(), target->GetOriginalEquipmentId());
        handler->PSendSysMessage(LANG_NPCINFO_HEALTH, target->GetCreateHealth(), target->GetMaxHealth(), target->GetHealth());
        handler->PSendSysMessage(LANG_NPCINFO_MOVEMENT_DATA, target->GetMovementTemplate().ToString().c_str());

        handler->PSendSysMessage(LANG_NPCINFO_UNIT_FIELD_FLAGS, target->GetUnitFlags());
        for (UnitFlags flag : EnumUtils::Iterate<UnitFlags>())
            if (target->HasUnitFlag(flag))
                handler->PSendSysMessage("* %s (0x%X)", EnumUtils::ToTitle(flag), flag);

        handler->PSendSysMessage(LANG_NPCINFO_FLAGS, target->GetUnitFlags2(), target->GetDynamicFlags(), target->GetFaction());
        handler->PSendSysMessage(LANG_COMMAND_RAWPAWNTIMES, defRespawnDelayStr.c_str(), curRespawnDelayStr.c_str());
        handler->PSendSysMessage(LANG_NPCINFO_LOOT,  cInfo->lootid, cInfo->pickpocketLootId, cInfo->SkinLootId);
        handler->PSendSysMessage(LANG_NPCINFO_DUNGEON_ID, target->GetInstanceId());
        handler->PSendSysMessage(LANG_NPCINFO_PHASEMASK, target->GetPhaseMask());
        handler->PSendSysMessage(LANG_NPCINFO_ARMOR, target->GetArmor());
        handler->PSendSysMessage(LANG_NPCINFO_POSITION, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
        handler->PSendSysMessage(LANG_OBJECTINFO_AIINFO, target->GetAIName().c_str(), target->GetScriptName().c_str());
        handler->PSendSysMessage(LANG_NPCINFO_REACTSTATE, DescribeReactState(target->GetReactState()));
        if (CreatureAI const* ai = target->AI())
            handler->PSendSysMessage(LANG_OBJECTINFO_AITYPE, Trinity::GetTypeName(*ai).c_str());
        handler->PSendSysMessage(LANG_NPCINFO_FLAGS_EXTRA, cInfo->flags_extra);
        for (CreatureFlagsExtra flag : EnumUtils::Iterate<CreatureFlagsExtra>())
            if (cInfo->flags_extra & flag)
                handler->PSendSysMessage("* %s (0x%X)", EnumUtils::ToTitle(flag), flag);

        for (NPCFlags flag : EnumUtils::Iterate<NPCFlags>())
            if (npcflags & flag)
                handler->PSendSysMessage("* %s (0x%X)", EnumUtils::ToTitle(flag), flag);

        handler->PSendSysMessage(LANG_NPCINFO_MECHANIC_IMMUNE, mechanicImmuneMask);
        for (Mechanics m : EnumUtils::Iterate<Mechanics>())
            if (m && (mechanicImmuneMask & (1 << (m-1))))
                handler->PSendSysMessage("* %s (0x%X)", EnumUtils::ToTitle(m), m);

        return true;
    }

    /**
     * @brief 查找附近NPC命令处理函数
     * @param handler 聊天处理器指针
     * @param dist 搜索距离（可选，默认10.0）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc near [距离] - 列出玩家附近的NPC
     *
     * 执行流程：
     * 1. 确定搜索距离（默认10码）
     * 2. 从数据库查询指定距离内的所有NPC
     * 3. 显示每个NPC的生成ID、名称和位置
     * 4. 统计并显示找到的NPC总数
     *
     * @note 用于查找附近的NPC，便于管理和调试
     */
    static bool HandleNpcNearCommand(ChatHandler* handler, Optional<float> dist)
    {
        float distance = dist.value_or(10.0f);
        uint32 count = 0;

        Player* player = handler->GetSession()->GetPlayer();

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_CREATURE_NEAREST);
        stmt->setFloat(0, player->GetPositionX());
        stmt->setFloat(1, player->GetPositionY());
        stmt->setFloat(2, player->GetPositionZ());
        stmt->setUInt32(3, player->GetMapId());
        stmt->setFloat(4, player->GetPositionX());
        stmt->setFloat(5, player->GetPositionY());
        stmt->setFloat(6, player->GetPositionZ());
        stmt->setFloat(7, distance * distance);
        PreparedQueryResult result = WorldDatabase.Query(stmt);

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                ObjectGuid::LowType guid = fields[0].GetUInt32();
                uint32 entry = fields[1].GetUInt32();
                float x = fields[2].GetFloat();
                float y = fields[3].GetFloat();
                float z = fields[4].GetFloat();
                uint16 mapId = fields[5].GetUInt16();

                CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(entry);
                if (!creatureTemplate)
                    continue;

                handler->PSendSysMessage(LANG_CREATURE_LIST_CHAT, guid, guid, creatureTemplate->Name.c_str(), x, y, z, mapId, "", "");

                ++count;
            }
            while (result->NextRow());
        }

        handler->PSendSysMessage(LANG_COMMAND_NEAR_NPC_MESSAGE, distance, count);

        return true;
    }

    /**
     * @brief 移动NPC到玩家位置命令处理函数
     * @param handler 聊天处理器指针
     * @param spawnid NPC生成ID（可选）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc move [生成ID] - 将NPC移动到玩家当前位置
     *
     * 执行流程：
     * 1. 如果提供了生成ID，使用该ID；否则使用选中的NPC
     * 2. 验证NPC是否存在且与玩家在同一地图
     * 3. 更新内存中的NPC位置
     * 4. 更新数据库中的NPC位置
     * 5. 如果NPC当前在游戏中，重新生成以应用新位置
     *
     * @note 这是一个持久化操作，会更新数据库
     */
    //move selected creature
    static bool HandleNpcMoveCommand(ChatHandler* handler, Optional<CreatureSpawnId> spawnid)
    {
        Creature* creature = handler->getSelectedCreature();
        Player const* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!spawnid && !creature)
            return false;

        ObjectGuid::LowType lowguid = spawnid ? *spawnid : creature->GetSpawnId();
        // Attempting creature load from DB data
        CreatureData const* data = sObjectMgr->GetCreatureData(lowguid);
        if (!data)
        {
            handler->PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (player->GetMapId() != data->mapId)
        {
            handler->PSendSysMessage(LANG_COMMAND_CREATUREATSAMEMAP, lowguid);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // update position in memory
        sObjectMgr->RemoveCreatureFromGrid(lowguid, data);
        const_cast<CreatureData*>(data)->spawnPoint.Relocate(*player);
        sObjectMgr->AddCreatureToGrid(lowguid, data);

        // update position in DB
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_POSITION);
        stmt->setFloat(0, player->GetPositionX());
        stmt->setFloat(1, player->GetPositionY());
        stmt->setFloat(2, player->GetPositionZ());
        stmt->setFloat(3, player->GetOrientation());
        stmt->setUInt32(4, lowguid);
        WorldDatabase.Execute(stmt);

        // respawn selected creature at the new location
        if (creature)
            creature->DespawnOrUnsummon(0s, 1s);

        handler->PSendSysMessage(LANG_COMMAND_CREATUREMOVED);
        return true;
    }

    /**
     * @brief 播放NPC表情命令处理函数
     * @param handler 聊天处理器指针
     * @param emote 表情ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc playemote <表情ID> - 让选中的NPC播放指定表情
     *
     * 该命令设置NPC的表情状态，使其持续播放指定表情。
     * 表情ID定义在 Emotes.dbc 中。
     *
     * @note 这是一个临时效果，NPC重生后会恢复
     */
    //play npc emote
    static bool HandleNpcPlayEmoteCommand(ChatHandler* handler, Emote emote)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        target->SetEmoteState(emote);

        return true;
    }

    /**
     * @brief 设置NPC模型命令处理函数
     * @param handler 聊天处理器指针
     * @param displayId 显示ID（模型ID）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set model <显示ID> - 设置选中NPC的模型外观
     *
     * 执行流程：
     * 1. 获取选中的NPC（不能是宠物）
     * 2. 验证显示ID是否有效
     * 3. 设置当前显示ID和原生显示ID
     * 4. 保存到数据库
     *
     * @note 显示ID定义在 CreatureDisplayInfo.dbc 中
     *       此修改会持久化到数据库
     */
    //set model of creature
    static bool HandleNpcSetModelCommand(ChatHandler* handler, uint32 displayId)
    {
        Creature* creature = handler->getSelectedCreature();

        if (!creature || creature->IsPet())
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!sCreatureDisplayInfoStore.LookupEntry(displayId))
        {
            handler->PSendSysMessage(LANG_COMMAND_INVALID_PARAM, Trinity::ToString(displayId).c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->SetDisplayId(displayId);
        creature->SetNativeDisplayId(displayId);

        creature->SaveToDB();

        return true;
    }

    /**HandleNpcSetMoveTypeCommand
    * Set the movement type for an NPC.<br/>
    * <br/>
    * Valid movement types are:
    * <ul>
    * <li> stay - NPC wont move </li>
    * <li> random - NPC will move randomly according to the wander_distance </li>
    * <li> way - NPC will move with given waypoints set </li>
    * </ul>
    * additional parameter: NODEL - so no waypoints are deleted, if you
    *                       change the movement type
    */
    static bool HandleNpcSetMoveTypeCommand(ChatHandler* handler, Optional<CreatureSpawnId> lowGuid, Variant<EXACT_SEQUENCE("stay"), EXACT_SEQUENCE("random"), EXACT_SEQUENCE("way")> type, Optional<EXACT_SEQUENCE("nodel")> nodel)
    {
        // 3 arguments:
        // GUID (optional - you can also select the creature)
        // stay|random|way (determines the kind of movement)
        // NODEL (optional - tells the system NOT to delete any waypoints)
        //        this is very handy if you want to do waypoints, that are
        //        later switched on/off according to special events (like escort
        //        quests, etc)

        bool doNotDelete = nodel.has_value();

        ObjectGuid::LowType lowguid = 0;
        Creature* creature = nullptr;

        if (!lowGuid)                                           // case .setmovetype $move_type (with selected creature)
        {
            creature = handler->getSelectedCreature();
            if (!creature || creature->IsPet())
                return false;
            lowguid = creature->GetSpawnId();
        }
        else                                                    // case .setmovetype #creature_guid $move_type (with selected creature)
        {
            lowguid = *lowGuid;

            if (lowguid)
                creature = handler->GetCreatureFromPlayerMapByDbGuid(lowguid);

            // attempt check creature existence by DB data
            if (!creature)
            {
                CreatureData const* data = sObjectMgr->GetCreatureData(lowguid);
                if (!data)
                {
                    handler->PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }
            else
            {
                lowguid = creature->GetSpawnId();
            }
        }

        // now lowguid is low guid really existed creature
        // and creature point (maybe) to this creature or nullptr

        MovementGeneratorType move_type;
        switch (type.index())
        {
            case 0:
                move_type = IDLE_MOTION_TYPE;
                break;
            case 1:
                move_type = RANDOM_MOTION_TYPE;
                break;
            case 2:
                move_type = WAYPOINT_MOTION_TYPE;
                break;
            default:
                return false;
        }

        // update movement type
        //if (doNotDelete == false)
        //    WaypointMgr.DeletePath(lowguid);

        if (creature)
        {
            // update movement type
            if (doNotDelete == false)
                creature->LoadPath(0);

            creature->SetDefaultMovementType(move_type);
            creature->GetMotionMaster()->Initialize();
            if (creature->IsAlive())                            // dead creature will reset movement generator at respawn
            {
                creature->setDeathState(JUST_DIED);
                creature->Respawn();
            }
            creature->SaveToDB();
        }
        if (doNotDelete == false)
        {
            handler->PSendSysMessage(LANG_MOVE_TYPE_SET, EnumUtils::ToTitle(move_type));
        }
        else
        {
            handler->PSendSysMessage(LANG_MOVE_TYPE_SET_NODEL, EnumUtils::ToTitle(move_type));
        }

        return true;
    }

    /**
     * @brief 设置NPC阶段掩码命令处理函数
     * @param handler 聊天处理器指针
     * @param phasemask 阶段掩码值（不能为0）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set phase <阶段掩码> - 设置选中NPC的阶段掩码
     *
     * 阶段掩码控制NPC的可见性和交互性，只有当玩家和NPC的阶段掩码有交集时才能看到NPC。
     *
     * 执行流程：
     * 1. 验证阶段掩码不为0
     * 2. 获取选中的NPC
     * 3. 设置NPC的阶段掩码
     * 4. 如果不是宠物，保存到数据库
     *
     * @note 阶段系统用于任务链中的阶段性内容展示
     */
    //npc phasemask handling
    //change phasemask of creature or pet
    static bool HandleNpcSetPhaseCommand(ChatHandler* handler, uint32 phasemask)
    {
        if (phasemask == 0)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->SetPhaseMask(phasemask, true);

        if (!creature->IsPet())
            creature->SaveToDB();

        return true;
    }

    /**
     * @brief 设置NPC游荡距离命令处理函数
     * @param handler 聊天处理器指针
     * @param option 游荡距离（非负数）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set wanderdistance <距离> - 设置NPC的游荡距离
     *
     * 执行流程：
     * 1. 验证距离参数（必须>=0）
     * 2. 根据距离设置移动类型：
     *    - 距离=0: 站立不动（IDLE_MOTION_TYPE）
     *    - 距离>0: 随机游荡（RANDOM_MOTION_TYPE）
     * 3. 更新NPC的游荡距离和移动类型
     * 4. 如果NPC活着，重新生成以应用新设置
     * 5. 更新数据库
     *
     * @note 游荡距离决定NPC随机移动的范围
     */
    //set spawn dist of creature
    static bool HandleNpcSetWanderDistanceCommand(ChatHandler* handler, float option)
    {
        if (option < 0.0f)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            return false;
        }

        MovementGeneratorType mtype = IDLE_MOTION_TYPE;
        if (option > 0.0f)
            mtype = RANDOM_MOTION_TYPE;

        Creature* creature = handler->getSelectedCreature();
        ObjectGuid::LowType guidLow = 0;

        if (creature)
            guidLow = creature->GetSpawnId();
        else
            return false;

        creature->SetWanderDistance((float)option);
        creature->SetDefaultMovementType(mtype);
        creature->GetMotionMaster()->Initialize();
        if (creature->IsAlive())                                // dead creature will reset movement generator at respawn
        {
            creature->setDeathState(JUST_DIED);
            creature->Respawn();
        }

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_WANDER_DISTANCE);

        stmt->setFloat(0, option);
        stmt->setUInt8(1, uint8(mtype));
        stmt->setUInt32(2, guidLow);

        WorldDatabase.Execute(stmt);

        handler->PSendSysMessage(LANG_COMMAND_WANDER_DISTANCE, option);
        return true;
    }

    /**
     * @brief 设置NPC重生时间命令处理函数
     * @param handler 聊天处理器指针
     * @param spawnTime 重生时间（秒）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc set spawntime <秒数> - 设置NPC被击杀后的重生时间
     *
     * 执行流程：
     * 1. 获取选中的NPC
     * 2. 更新数据库中的重生时间
     * 3. 更新内存中的重生延迟
     *
     * @note 重生时间从NPC死亡开始计算
     */
    //spawn time handling
    static bool HandleNpcSetSpawnTimeCommand(ChatHandler* handler, uint32 spawnTime)
    {
        Creature* creature = handler->getSelectedCreature();
        if (!creature)
            return false;

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_SPAWN_TIME_SECS);
        stmt->setUInt32(0, spawnTime);
        stmt->setUInt32(1, creature->GetSpawnId());
        WorldDatabase.Execute(stmt);

        creature->SetRespawnDelay(spawnTime);
        handler->PSendSysMessage(LANG_COMMAND_SPAWNTIME, spawnTime);

        return true;
    }

    /**
     * @brief 让NPC说话命令处理函数
     * @param handler 聊天处理器指针
     * @param text 说话内容
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc say <文本> - 让选中的NPC说话（附近玩家可见）
     *
     * 该命令让NPC以普通说话的方式发送消息，附近玩家都能看到。
     * 根据文本末尾标点符号自动触发相应表情：
     * - '?' 触发疑问表情
     * - '!' 触发感叹表情
     * - 其他 触发说话表情
     *
     * @note 这是临时效果，仅用于测试或剧情演示
     */
    static bool HandleNpcSayCommand(ChatHandler* handler, Tail text)
    {
        if (text.empty())
            return false;

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->Say(text, LANG_UNIVERSAL);

        // make some emotes
        switch (text.back())
        {
            case '?':   creature->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);      break;
            case '!':   creature->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);   break;
            default:    creature->HandleEmoteCommand(EMOTE_ONESHOT_TALK);          break;
        }

        return true;
    }

    /**
     * @brief 让NPC发送文本表情命令处理函数
     * @param handler 聊天处理器指针
     * @param text 表情文本
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc textemote <文本> - 让选中NPC发送文本表情
     *
     * 该命令让NPC以表情文本的形式发送消息，例如：
     * ".npc textemote 挥手致意" 会显示为 "NPC名 挥手致意"
     *
     * @note 这是临时效果，仅用于测试或剧情演示
     */
    //show text emote by creature in chat
    static bool HandleNpcTextEmoteCommand(ChatHandler* handler, Tail text)
    {
        if (text.empty())
            return false;

        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->TextEmote(text);

        return true;
    }

    /**
     * @brief 停止NPC跟随命令处理函数
     * @param handler 聊天处理器指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc follow stop - 停止选中NPC跟随自己
     *
     * 执行流程：
     * 1. 获取玩家和选中的NPC
     * 2. 查找NPC的跟随移动生成器
     * 3. 验证是否正在跟随该玩家
     * 4. 移除跟随移动生成器
     *
     * @note 只能停止正在跟随自己的NPC
     */
    // npc unfollow handling
    static bool HandleNpcUnFollowCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->PSendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        MovementGenerator* movement = creature->GetMotionMaster()->GetMovementGenerator([player](MovementGenerator const* a) -> bool
        {
            if (a->GetMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            {
                FollowMovementGenerator const* followMovement = dynamic_cast<FollowMovementGenerator const*>(a);
                return followMovement && followMovement->GetTarget() == player;
            }
            return false;
        });

        if (!movement)
        {
            handler->PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU, creature->GetName().c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->GetMotionMaster()->Remove(movement);
        handler->PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU_NOW, creature->GetName().c_str());
        return true;
    }

    /**
     * @brief 让NPC私聊玩家命令处理函数
     * @param handler 聊天处理器指针
     * @param recv 接收者（玩家名称或链接）
     * @param text 私聊内容
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc whisper <玩家名> <文本> - 让选中NPC私聊指定玩家
     *
     * 该命令让NPC向指定玩家发送私聊消息，只有该玩家能看到。
     *
     * @note 这是临时效果，仅用于测试或剧情演示
     */
    // make npc whisper to player
    static bool HandleNpcWhisperCommand(ChatHandler* handler, Variant<Hyperlink<player>, std::string_view> recv, Tail text)
    {
        if (text.empty())
            return false;

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // check online security
        Player* receiver = ObjectAccessor::FindPlayerByName(recv);
        if (handler->HasLowerSecurity(receiver, ObjectGuid::Empty))
            return false;

        creature->Whisper(text, LANG_UNIVERSAL, receiver);
        return true;
    }

    /**
     * @brief 让NPC喊话命令处理函数
     * @param handler 聊天处理器指针
     * @param text 喊话内容
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc yell <文本> - 让选中NPC喊话（大范围可见）
     *
     * 该命令让NPC以喊话方式发送消息，比普通说话范围更大。
     * 同时触发喊话表情动画。
     *
     * @note 这是临时效果，仅用于测试或剧情演示
     */
    static bool HandleNpcYellCommand(ChatHandler* handler, Tail text)
    {
        if (text.empty())
            return false;

        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        creature->Yell(text, LANG_UNIVERSAL);

        // make an emote
        creature->HandleEmoteCommand(EMOTE_ONESHOT_SHOUT);

        return true;
    }

    /**
     * @brief 添加临时NPC命令处理函数
     * @param handler 聊天处理器指针
     * @param lootStr 战利品选项（可选，"loot"或"noloot"）
     * @param id 生物模板ID
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc add temp [loot|noloot] <模板ID> - 在玩家位置生成临时NPC
     *
     * 参数说明：
     * - loot: NPC死亡后留下尸体一段时间（30秒）
     * - noloot: NPC死亡后立即消失
     *
     * 临时NPC不会保存到数据库，适合用于测试或临时事件。
     *
     * @note 临时NPC在消失后不会重生
     */
    // add creature, temp only
    static bool HandleNpcAddTempSpawnCommand(ChatHandler* handler, Optional<std::string_view> lootStr, CreatureEntry id)
    {
        bool loot = false;
        if (lootStr)
        {
            if (StringEqualI(*lootStr, "loot"))
                loot = true;
            else if (StringEqualI(*lootStr, "noloot"))
                loot = false;
            else
                return false;
        }

        Player* chr = handler->GetSession()->GetPlayer();
        if (!sObjectMgr->GetCreatureTemplate(id))
            return false;

        chr->SummonCreature(id, chr->GetPosition(), loot ? TEMPSUMMON_CORPSE_TIMED_DESPAWN : TEMPSUMMON_CORPSE_DESPAWN, 30s);

        return true;
    }

    /**
     * @brief 驯服NPC命令处理函数
     * @param handler 聊天处理器指针
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc tame - 将选中的NPC驯服为自己的宠物
     *
     * 执行流程：
     * 1. 验证目标是否可驯服（必须是生物，不能已经是宠物）
     * 2. 检查玩家是否已有宠物
     * 3. 验证生物模板是否可驯服（考虑猎人天赋）
     * 4. 创建驯服的宠物
     * 5. 设置宠物位置在玩家附近
     * 6. 设置宠物为防御模式
     * 7. 计算合适的等级（玩家等级-5与目标等级的最大值）
     * 8. 添加宠物到世界并显示升级效果
     * 9. 设置玩家拥有宠物
     * 10. 保存宠物到数据库并初始化技能
     *
     * @note 只有可驯服的生物才能被驯服
     *       兽王猎人可以驯服特殊宠物
     */
    //npc tame handling
    static bool HandleNpcTameCommand(ChatHandler* handler)
    {
        Creature* creatureTarget = handler->getSelectedCreature();
        if (!creatureTarget || creatureTarget->IsPet())
        {
            handler->PSendSysMessage (LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage (true);
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        if (player->GetPetGUID())
        {
            handler->SendSysMessage (LANG_YOU_ALREADY_HAVE_PET);
            handler->SetSentErrorMessage (true);
            return false;
        }

        CreatureTemplate const* cInfo = creatureTarget->GetCreatureTemplate();

        if (!cInfo->IsTameable (player->CanTameExoticPets()))
        {
            handler->PSendSysMessage (LANG_CREATURE_NON_TAMEABLE, cInfo->Entry);
            handler->SetSentErrorMessage (true);
            return false;
        }

        // Everything looks OK, create new pet
        Pet* pet = player->CreateTamedPetFrom(creatureTarget);
        if (!pet)
        {
            handler->PSendSysMessage (LANG_CREATURE_NON_TAMEABLE, cInfo->Entry);
            handler->SetSentErrorMessage (true);
            return false;
        }

        // place pet before player
        float x, y, z;
        player->GetClosePoint (x, y, z, creatureTarget->GetCombatReach(), CONTACT_DISTANCE);
        pet->Relocate(x, y, z, float(M_PI) - player->GetOrientation());

        // set pet to defensive mode by default (some classes can't control controlled pets in fact).
        pet->SetReactState(REACT_DEFENSIVE);

        // calculate proper level
        uint8 level = std::max<uint8>(player->GetLevel()-5, creatureTarget->GetLevel());

        // prepare visual effect for levelup
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, level - 1);

        // add to world
        pet->GetMap()->AddToMap(pet->ToCreature());

        // visual effect for levelup
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, level);

        // caster have pet now
        player->SetMinion(pet, true);

        pet->SavePetToDB(PET_SAVE_AS_CURRENT);
        player->PetSpellInitialize();

        return true;
    }

    /**
     * @brief 强制NPC进入逃脱模式命令处理函数
     * @param handler 聊天处理器指针
     * @param why 逃脱原因（可选）
     * @param force 强制标志（可选）
     * @return 命令执行成功返回true，失败返回false
     *
     * .npc evade [原因] [force] - 强制NPC进入逃脱模式
     *
     * 该命令让NPC脱离战斗并返回出生点，用于：
     * - 重置卡住的NPC
     * - 测试逃脱行为
     * - 清除NPC的战斗状态
     *
     * 参数：
     * - 原因: 指定逃脱原因（如EVADE_REASON_NO_HOSTILES等）
     * - force: 强制逃脱，即使NPC已经在逃脱状态
     *
     * @note 不能对宠物使用此命令
     */
    static bool HandleNpcEvadeCommand(ChatHandler* handler, Optional<CreatureAI::EvadeReason> why, Optional<EXACT_SEQUENCE("force")> force)
    {
        Creature* creatureTarget = handler->getSelectedCreature();
        if (!creatureTarget || creatureTarget->IsPet())
        {
            handler->PSendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!creatureTarget->IsAIEnabled())
        {
            handler->PSendSysMessage(LANG_CREATURE_NOT_AI_ENABLED);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (force)
            creatureTarget->ClearUnitState(UNIT_STATE_EVADE);
        creatureTarget->AI()->EnterEvadeMode(why.value_or(CreatureAI::EVADE_REASON_OTHER));

        return true;
    }

    static void _ShowLootEntry(ChatHandler* handler, uint32 itemId, uint8 itemCount, bool alternateString = false)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        ItemLocale const* itemLocale = sObjectMgr->GetItemLocale(itemId);
        char const* name = nullptr;
        if (itemLocale)
            name = itemLocale->Name[handler->GetSessionDbcLocale()].c_str();
        if ((!name || !*name) && itemTemplate)
            name = itemTemplate->Name1.c_str();
        if (!name)
            name = "Unknown item";
        handler->PSendSysMessage(alternateString ? LANG_COMMAND_NPC_SHOWLOOT_ENTRY_2 : LANG_COMMAND_NPC_SHOWLOOT_ENTRY,
            itemCount, ItemQualityColors[itemTemplate ? itemTemplate->Quality : uint32(ITEM_QUALITY_POOR)], itemId, name, itemId);
    }
    static void _IterateNotNormalLootMap(ChatHandler* handler, NotNormalLootItemMap const& map, std::vector<LootItem> const& items)
    {
        for (NotNormalLootItemMap::value_type const& pair : map)
        {
            if (!pair.second)
                continue;
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(pair.first);
            Player const* player = ObjectAccessor::FindConnectedPlayer(guid);
            handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_SUBLABEL, player ? player->GetName() : Trinity::StringFormat("Offline player (GuidLow 0x{:08X})", pair.first), pair.second->size());

            for (auto it = pair.second->cbegin(); it != pair.second->cend(); ++it)
            {
                LootItem const& item = items[it->index];
                if (!(it->is_looted) && !item.is_looted)
                    _ShowLootEntry(handler, item.itemid, item.count, true);
            }
        }
    }
    static bool HandleNpcShowLootCommand(ChatHandler* handler, Optional<EXACT_SEQUENCE("all")> all)
    {
        Creature* creatureTarget = handler->getSelectedCreature();
        if (!creatureTarget || creatureTarget->IsPet())
        {
            handler->PSendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Loot const& loot = creatureTarget->loot;
        if (!creatureTarget->isDead() || loot.empty())
        {
            handler->PSendSysMessage(LANG_COMMAND_NOT_DEAD_OR_NO_LOOT, creatureTarget->GetName());
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_HEADER, creatureTarget->GetName(), creatureTarget->GetEntry());
        handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_MONEY, loot.gold / GOLD, (loot.gold%GOLD) / SILVER, loot.gold%SILVER);

        if (!all)
        {
            handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL, "Standard items", loot.items.size());
            for (LootItem const& item : loot.items)
                if (!item.is_looted)
                    _ShowLootEntry(handler, item.itemid, item.count);

            handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL, "Quest items", loot.quest_items.size());
            for (LootItem const& item : loot.quest_items)
                if (!item.is_looted)
                    _ShowLootEntry(handler, item.itemid, item.count);
        }
        else
        {
            handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL, "Standard items", loot.items.size());
            for (LootItem const& item : loot.items)
                if (!item.is_looted && !item.freeforall && item.conditions.empty())
                    _ShowLootEntry(handler, item.itemid, item.count);

            if (!loot.GetPlayerQuestItems().empty())
            {
                handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL_2, "Per-player quest items");
                _IterateNotNormalLootMap(handler, loot.GetPlayerQuestItems(), loot.quest_items);
            }

            if (!loot.GetPlayerFFAItems().empty())
            {
                handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL_2, "FFA items per allowed player");
                _IterateNotNormalLootMap(handler, loot.GetPlayerFFAItems(), loot.items);
            }

            if (!loot.GetPlayerNonQuestNonFFAConditionalItems().empty())
            {
                handler->PSendSysMessage(LANG_COMMAND_NPC_SHOWLOOT_LABEL_2, "Per-player conditional items");
                _IterateNotNormalLootMap(handler, loot.GetPlayerNonQuestNonFFAConditionalItems(), loot.items);
            }
        }

        return true;
    }

    static bool HandleNpcAddFormationCommand(ChatHandler* handler, ObjectGuid::LowType leaderGUID)
    {
        Creature* creature = handler->getSelectedCreature();

        if (!creature || !creature->GetSpawnId())
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        ObjectGuid::LowType lowguid = creature->GetSpawnId();
        if (creature->GetFormation())
        {
            handler->PSendSysMessage("Selected creature is already member of group %u", creature->GetFormation()->GetLeaderSpawnId());
            return false;
        }

        if (!lowguid)
            return false;

        Player* chr = handler->GetSession()->GetPlayer();

        float  followAngle = (creature->GetAbsoluteAngle(chr) - chr->GetOrientation()) * 180.0f / float(M_PI);
        float  followDist  = std::sqrt(std::pow(chr->GetPositionX() - creature->GetPositionX(), 2.f) + std::pow(chr->GetPositionY() - creature->GetPositionY(), 2.f));
        uint32 groupAI     = 0;
        sFormationMgr->AddFormationMember(lowguid, followAngle, followDist, leaderGUID, groupAI);
        creature->SearchFormation();

        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_CREATURE_FORMATION);
        stmt->setUInt32(0, leaderGUID);
        stmt->setUInt32(1, lowguid);
        stmt->setFloat (2, followAngle);
        stmt->setFloat (3, followDist);
        stmt->setUInt32(4, groupAI);

        WorldDatabase.Execute(stmt);

        handler->PSendSysMessage("Creature %u added to formation with leader %u", lowguid, leaderGUID);

        return true;
    }

    static bool HandleNpcSetLinkCommand(ChatHandler* handler, ObjectGuid::LowType linkguid)
    {
        Creature* creature = handler->getSelectedCreature();

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!creature->GetSpawnId())
        {
            handler->PSendSysMessage("Selected creature %u isn't in creature table", creature->GetGUID().GetCounter());
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (!sObjectMgr->SetCreatureLinkedRespawn(creature->GetSpawnId(), linkguid))
        {
            handler->PSendSysMessage("Selected creature can't link with guid '%u'", linkguid);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage("LinkGUID '%u' added to creature with DBTableGUID: '%u'", linkguid, creature->GetSpawnId());
        return true;
    }

    /// @todo NpcCommands that need to be fixed :
    static bool HandleNpcAddWeaponCommand([[maybe_unused]] ChatHandler* handler, [[maybe_unused]] uint32 SlotID, [[maybe_unused]] ItemTemplate const* tmpItem)
    {
        /*
        if (!tmpItem)
            return;

        uint64 guid = handler->GetSession()->GetPlayer()->GetSelection();
        if (guid == 0)
        {
            handler->SendSysMessage(LANG_NO_SELECTION);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Creature* creature = ObjectAccessor::GetCreature(*handler->GetSession()->GetPlayer(), guid);

        if (!creature)
        {
            handler->SendSysMessage(LANG_SELECT_CREATURE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        switch (SlotID)
        {
            case 1:
                creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY, tmpItem->ItemId);
                break;
            case 2:
                creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY_01, tmpItem->ItemId);
                break;
            case 3:
                creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY_02, tmpItem->ItemId);
                break;
            default:
                handler->PSendSysMessage(LANG_ITEM_SLOT_NOT_EXIST, SlotID);
                handler->SetSentErrorMessage(true);
                return false;
        }

        handler->PSendSysMessage(LANG_ITEM_ADDED_TO_SLOT, tmpItem->ItemID, tmpItem->Name1, SlotID);
        */
        return true;
    }
};

void AddSC_npc_commandscript()
{
    new npc_commandscript();
}

bool HandleNpcSpawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("force"), EXACT_SEQUENCE("ignorerespawn")>> const& opts)
{
    if (opts.empty())
        return false;

    bool ignoreRespawn = false;
    bool force = false;
    uint32 groupId = 0;

    // Decode arguments
    for (auto const& variant : opts)
    {
        switch (variant.index())
        {
            case 0:
                groupId = variant.get<uint32>();
                break;
            case 1:
                force = true;
                break;
            case 2:
                ignoreRespawn = true;
                break;
        }
    }

    Player* player = handler->GetSession()->GetPlayer();

    std::vector <WorldObject*> creatureList;
    if (!player->GetMap()->SpawnGroupSpawn(groupId, ignoreRespawn, force, &creatureList))
    {
        handler->PSendSysMessage(LANG_SPAWNGROUP_BADGROUP, groupId);
        handler->SetSentErrorMessage(true);
        return false;
    }

    handler->PSendSysMessage(LANG_SPAWNGROUP_SPAWNCOUNT, creatureList.size());

    return true;
}

bool HandleNpcDespawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("removerespawntime")>> const& opts)
{
    if (opts.empty())
        return false;

    bool deleteRespawnTimes = false;
    uint32 groupId = 0;

    // Decode arguments
    for (auto const& variant : opts)
    {
        if (variant.holds_alternative<uint32>())
            groupId = variant.get<uint32>();
        else
            deleteRespawnTimes = true;
    }

    Player* player = handler->GetSession()->GetPlayer();

    size_t n = 0;
    if (!player->GetMap()->SpawnGroupDespawn(groupId, deleteRespawnTimes, &n))
    {
        handler->PSendSysMessage(LANG_SPAWNGROUP_BADGROUP, groupId);
        handler->SetSentErrorMessage(true);
        return false;
    }
    handler->PSendSysMessage("Despawned a total of %zu objects.", n);

    return true;
}
