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
 * @file    cs_gobject.cpp
 * @brief   游戏对象(GameObject)管理命令模块
 *
 * @details 本模块实现了所有与游戏对象相关的GM命令，包括：
 *          - 游戏对象的创建、删除、移动、旋转
 *          - 游戏对象信息的查询和显示
 *          - 游戏对象状态和相位的设置
 *          - 游戏对象的激活操作
 *          - 附近游戏对象的搜索
 *
 * @note    这些命令主要用于GM进行世界构建和调试
 *          所有命令都需要相应的RBAC权限才能执行
 *
 * @see     GameObject, GameObjectTemplate, ObjectMgr
 */

/* ScriptData
Name: gobject_commandscript
%Complete: 100
Comment: All gobject related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PoolMgr.h"
#include "RBAC.h"
#include "WorldSession.h"

using namespace Trinity::ChatCommands;

/**
 * @typedef GameObjectSpawnId
 * @brief 游戏对象生成ID类型
 *
 * @details 可以是超链接形式的游戏对象引用或数据库低GUID
 *          用于命令参数中指定游戏对象
 */
using GameObjectSpawnId = Variant<Hyperlink<gameobject>, ObjectGuid::LowType>;

/**
 * @typedef GameObjectEntry
 * @brief 游戏对象模板入口ID类型
 *
 * @details 可以是超链接形式的游戏对象入口或uint32类型的入口ID
 *          用于指定游戏对象的模板类型
 */
using GameObjectEntry = Variant<Hyperlink<gameobject_entry>, uint32>;

// definitions are over in cs_npc.cpp
// 生成组函数声明在 cs_npc.cpp 中定义
bool HandleNpcSpawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("force"), EXACT_SEQUENCE("ignorerespawn")>> const& opts);
bool HandleNpcDespawnGroup(ChatHandler* handler, std::vector<Variant<uint32, EXACT_SEQUENCE("removerespawntime")>> const& opts);

/**
 * @class gobject_commandscript
 * @brief 游戏对象命令脚本类
 *
 * @details 继承自 CommandScript，提供所有游戏对象相关的GM命令处理功能。
 *          该类实现了游戏对象的管理操作，包括创建、删除、移动、状态设置等。
 *
 * @note    所有命令都需要对应的RBAC权限
 *          大部分命令只能在游戏中使用，不支持控制台执行
 */
class gobject_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * @param name 脚本名称，固定为 "gobject_commandscript"
     */
    gobject_commandscript() : CommandScript("gobject_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回游戏对象命令表，包含所有子命令及其处理函数
     *
     * @details 定义了以下命令：
     *          - activate: 激活游戏对象
     *          - delete: 删除游戏对象
     *          - info: 显示游戏对象信息
     *          - move: 移动游戏对象
     *          - near: 查找附近游戏对象
     *          - target: 目标选择游戏对象
     *          - turn: 旋转游戏对象
     *          - add: 添加游戏对象
     *          - add temp: 添加临时游戏对象
     *          - set phase: 设置相位
     *          - set state: 设置状态
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable gobjectCommandTable =
        {
            { "activate",       HandleGameObjectActivateCommand,  rbac::RBAC_PERM_COMMAND_GOBJECT_ACTIVATE,       Console::No },
            { "delete",         HandleGameObjectDeleteCommand,    rbac::RBAC_PERM_COMMAND_GOBJECT_DELETE,         Console::No },
            { "info",           HandleGameObjectInfoCommand,      rbac::RBAC_PERM_COMMAND_GOBJECT_INFO,           Console::No },
            { "move",           HandleGameObjectMoveCommand,      rbac::RBAC_PERM_COMMAND_GOBJECT_MOVE,           Console::No },
            { "near",           HandleGameObjectNearCommand,      rbac::RBAC_PERM_COMMAND_GOBJECT_NEAR,           Console::No },
            { "target",         HandleGameObjectTargetCommand,    rbac::RBAC_PERM_COMMAND_GOBJECT_TARGET,         Console::No },
            { "turn",           HandleGameObjectTurnCommand,      rbac::RBAC_PERM_COMMAND_GOBJECT_TURN,           Console::No },
            { "spawngroup",     HandleNpcSpawnGroup,              rbac::RBAC_PERM_COMMAND_GOBJECT_SPAWNGROUP,     Console::No },
            { "despawngroup",   HandleNpcDespawnGroup,            rbac::RBAC_PERM_COMMAND_GOBJECT_DESPAWNGROUP,   Console::No },
            { "add temp",       HandleGameObjectAddTempCommand,   rbac::RBAC_PERM_COMMAND_GOBJECT_ADD_TEMP,       Console::No },
            { "add",            HandleGameObjectAddCommand,       rbac::RBAC_PERM_COMMAND_GOBJECT_ADD,            Console::No },
            { "set phase",      HandleGameObjectSetPhaseCommand,  rbac::RBAC_PERM_COMMAND_GOBJECT_SET_PHASE,      Console::No },
            { "set state",      HandleGameObjectSetStateCommand,  rbac::RBAC_PERM_COMMAND_GOBJECT_SET_STATE,      Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "gobject", gobjectCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 激活游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param guidLow 游戏对象的生成ID
     * @return 成功返回 true，失败返回 false
     *
     * @details 激活指定的游戏对象（如门、按钮等）。
     *          - 设置对象为就绪状态
     *          - 触发开门/按钮动画
     *          - 自动关闭时间根据模板设置决定（10秒或0）
     *
     * @note 调用时机：GM执行 .gobject activate 命令时
     *       性能注意事项：仅对本地对象进行操作，影响较小
     */
    static bool HandleGameObjectActivateCommand(ChatHandler* handler, GameObjectSpawnId guidLow)
    {
        // 从玩家所在地图获取指定ID的游戏对象
        GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(guidLow);
        if (!object)
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, guidLow);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 根据模板设置获取自动关闭时间，有设置则10秒，否则0（不自动关闭）
        uint32_t const autoCloseTime = object->GetGOInfo()->GetAutoCloseTime() ? 10000u : 0u;

        // 激活对象：设置为就绪状态并使用门/按钮功能
        object->SetLootState(GO_READY);
        object->UseDoorOrButton(autoCloseTime, false, handler->GetSession()->GetPlayer());

        handler->PSendSysMessage("Object activated!");

        return true;
    }

    /**
     * @brief 添加游戏对象命令处理（永久保存）
     * @param handler 聊天命令处理器
     * @param objectId 游戏对象模板入口ID
     * @param spawnTimeSecs 可选的重生时间（秒）
     * @return 成功返回 true，失败返回 false
     *
     * @details 在玩家当前位置创建一个永久的游戏对象，并保存到数据库。
     *          流程：
     *          1. 验证游戏对象模板是否存在
     *          2. 验证显示ID是否有效
     *          3. 创建游戏对象实例
     *          4. 保存到数据库
     *          5. 从数据库重新加载以确保数据一致性
     *          6. 将对象添加到网格中
     *
     * @note 调用时机：GM执行 .gobject add 命令时
     *       性能注意事项：涉及数据库操作，创建后会重新加载对象
     */
    //spawn go
    static bool HandleGameObjectAddCommand(ChatHandler* handler, GameObjectEntry objectId, Optional<int32> spawnTimeSecs)
    {
        if (!objectId)
            return false;

        // 获取游戏对象模板信息
        GameObjectTemplate const* objectInfo = sObjectMgr->GetGameObjectTemplate(objectId);
        if (!objectInfo)
        {
            handler->PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST, objectId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 验证显示ID是否有效
        if (objectInfo->displayId && !sGameObjectDisplayInfoStore.LookupEntry(objectInfo->displayId))
        {
            // report to DB errors log as in loading case
            // 记录到数据库错误日志，与加载时相同
            TC_LOG_ERROR("sql.sql", "Gameobject (Entry {} GoType: {}) have invalid displayId ({}), not spawned.", *objectId, objectInfo->type, objectInfo->displayId);
            handler->PSendSysMessage(LANG_GAMEOBJECT_HAVE_INVALID_DATA, objectId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        Map* map = player->GetMap();

        // 创建新的游戏对象实例
        GameObject* object = new GameObject();
        ObjectGuid::LowType guidLow = map->GenerateLowGuid<HighGuid::GameObject>();

        // 根据玩家朝向计算旋转四元数
        QuaternionData rot = QuaternionData::fromEulerAnglesZYX(player->GetOrientation(), 0.f, 0.f);
        if (!object->Create(guidLow, objectInfo->entry, map, player->GetPhaseMaskForSpawn(), *player, rot, 255, GO_STATE_READY))
        {
            delete object;
            return false;
        }

        // 设置重生时间（如果指定）
        if (spawnTimeSecs)
            object->SetRespawnTime(*spawnTimeSecs);

        // fill the gameobject data and save to the db
        // 填充游戏对象数据并保存到数据库
        object->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), player->GetPhaseMaskForSpawn());
        guidLow = object->GetSpawnId();

        // delete the old object and do a clean load from DB with a fresh new GameObject instance.
        // this is required to avoid weird behavior and memory leaks
        // 删除旧对象并从数据库重新加载，使用全新的游戏对象实例
        // 这是避免奇怪行为和内存泄漏的必要步骤
        delete object;

        object = new GameObject();
        // this will generate a new guid if the object is in an instance
        // 如果对象在副本中，这将生成新的GUID
        if (!object->LoadFromDB(guidLow, map, true))
        {
            delete object;
            return false;
        }

        /// @todo is it really necessary to add both the real and DB table guid here ?
        // 将游戏对象添加到网格加载列表中
        sObjectMgr->AddGameobjectToGrid(guidLow, sObjectMgr->GetGameObjectData(guidLow));

        handler->PSendSysMessage(LANG_GAMEOBJECT_ADD, objectId, objectInfo->name.c_str(), guidLow, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        return true;
    }

    /**
     * @brief 添加临时游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param objectId 游戏对象模板入口ID
     * @param spawntime 可选的存在时间（秒），默认300秒
     * @return 成功返回 true，失败返回 false
     *
     * @details 在玩家当前位置召唤一个临时游戏对象。
     *          - 不会保存到数据库
     *          - 超过指定时间后自动消失
     *          - 主要用于测试或临时装饰
     *
     * @note 调用时机：GM执行 .gobject add temp 命令时
     *       性能注意事项：不涉及数据库操作，性能影响小
     */
    // add go, temp only
    static bool HandleGameObjectAddTempCommand(ChatHandler* handler, GameObjectEntry objectId, Optional<uint64> spawntime)
    {
        Player* player = handler->GetSession()->GetPlayer();
        // 默认存在时间为300秒（5分钟）
        Seconds spawntm(spawntime.value_or(300));

        // 根据玩家朝向计算旋转四元数
        QuaternionData rotation = QuaternionData::fromEulerAnglesZYX(player->GetOrientation(), 0.f, 0.f);

        // 验证游戏对象模板是否存在
        if (!sObjectMgr->GetGameObjectTemplate(objectId))
        {
            handler->PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST, objectId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 召唤临时游戏对象
        player->SummonGameObject(objectId, *player, rotation, spawntm);

        return true;
    }

    /**
     * @brief 查找并选中游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param objectId 可选参数，可以是游戏对象入口ID或名称
     * @return 成功返回 true，失败返回 false
     *
     * @details 根据条件查找并显示附近的游戏对象信息。
     *          支持三种查找模式：
     *          1. 指定入口ID：查找该类型最近的对象
     *          2. 指定名称：模糊匹配名称查找最近的对象
     *          3. 无参数：查找当前活跃事件相关的最近10个对象
     *
     *          显示信息包括：
     *          - GUID、入口ID、名称、坐标、地图ID、相位
     *          - 重生时间信息（如果对象已加载）
     *
     * @note 调用时机：GM执行 .gobject target 命令时
     *       性能注意事项：执行数据库查询，查询结果按距离排序
     */
    static bool HandleGameObjectTargetCommand(ChatHandler* handler, Optional<Variant<GameObjectEntry, std::string_view>> objectId)
    {
        Player* player = handler->GetSession()->GetPlayer();
        QueryResult result;
        GameEventMgr::ActiveEvents const& activeEventsList = sGameEventMgr->GetActiveEventList();

        if (objectId)
        {
            // 根据入口ID或名称进行查询
            if (objectId->holds_alternative<GameObjectEntry>())
            {
                // 按入口ID查询，计算距离并按距离升序排序，取最近的1个
                result = WorldDatabase.PQuery("SELECT guid, id, position_x, position_y, position_z, orientation, map, phaseMask, (POW(position_x - '{}', 2) + POW(position_y - '{}', 2) + POW(position_z - '{}', 2)) AS order_ FROM gameobject WHERE map = '{}' AND id = '{}' ORDER BY order_ ASC LIMIT 1",
                    player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), static_cast<uint32>(objectId->get<GameObjectEntry>()));
            }
            else
            {
                // 按名称模糊匹配查询
                std::string name = std::string(objectId->get<std::string_view>());
                WorldDatabase.EscapeString(name);
                result = WorldDatabase.PQuery(
                    "SELECT guid, id, position_x, position_y, position_z, orientation, map, phaseMask, (POW(position_x - {}, 2) + POW(position_y - {}, 2) + POW(position_z - {}, 2)) AS order_ "
                    "FROM gameobject LEFT JOIN gameobject_template ON gameobject_template.entry = gameobject.id WHERE map = {} AND name LIKE '%{}%' ORDER BY order_ ASC LIMIT 1",
                    player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), name);
            }
        }
        else
        {
            // 无参数时，查找当前活跃事件相关的游戏对象
            std::ostringstream eventFilter;
            eventFilter << " AND (eventEntry IS NULL ";
            bool initString = true;

            // 构建活跃事件的过滤条件
            for (GameEventMgr::ActiveEvents::const_iterator itr = activeEventsList.begin(); itr != activeEventsList.end(); ++itr)
            {
                if (initString)
                {
                    eventFilter  <<  "OR eventEntry IN (" << *itr;
                    initString = false;
                }
                else
                    eventFilter << ',' << *itr;
            }

            if (!initString)
                eventFilter << "))";
            else
                eventFilter << ')';

            // 查询最近的10个游戏对象
            result = WorldDatabase.PQuery("SELECT gameobject.guid, id, position_x, position_y, position_z, orientation, map, phaseMask, "
                "(POW(position_x - {}, 2) + POW(position_y - {}, 2) + POW(position_z - {}, 2)) AS order_ FROM gameobject "
                "LEFT OUTER JOIN game_event_gameobject on gameobject.guid = game_event_gameobject.guid WHERE map = '{}' {} ORDER BY order_ ASC LIMIT 10",
                handler->GetSession()->GetPlayer()->GetPositionX(), handler->GetSession()->GetPlayer()->GetPositionY(), handler->GetSession()->GetPlayer()->GetPositionZ(),
                handler->GetSession()->GetPlayer()->GetMapId(), eventFilter.str());
        }

        if (!result)
        {
            handler->SendSysMessage(LANG_COMMAND_TARGETOBJNOTFOUND);
            return true;
        }

        bool found = false;
        float x, y, z, o;
        ObjectGuid::LowType guidLow;
        uint32 id, phase;
        uint16 mapId;
        uint32 poolId;

        // 遍历查询结果，查找有效的（非池或已生成的）游戏对象
        do
        {
            Field* fields = result->Fetch();
            guidLow = fields[0].GetUInt32();
            id =      fields[1].GetUInt32();
            x =       fields[2].GetFloat();
            y =       fields[3].GetFloat();
            z =       fields[4].GetFloat();
            o =       fields[5].GetFloat();
            mapId =   fields[6].GetUInt16();
            phase =   fields[7].GetUInt32();
            // 检查对象是否在池中
            poolId =  sPoolMgr->IsPartOfAPool<GameObject>(guidLow);
            // 只接受不在池中或已生成的对象
            if (!poolId || sPoolMgr->IsSpawnedObject<GameObject>(guidLow))
                found = true;
        } while (result->NextRow() && !found);

        if (!found)
        {
            handler->PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST, id);
            return false;
        }

        GameObjectTemplate const* objectInfo = sObjectMgr->GetGameObjectTemplate(id);

        if (!objectInfo)
        {
            handler->PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST, id);
            return false;
        }

        // 尝试获取已加载的游戏对象实例
        GameObject* target = handler->GetObjectFromPlayerMapByDbGuid(guidLow);

        // 显示游戏对象详细信息
        handler->PSendSysMessage(LANG_GAMEOBJECT_DETAIL, guidLow, objectInfo->name.c_str(), guidLow, id, x, y, z, mapId, o, phase);

        // 如果对象已加载，显示重生时间信息
        if (target)
        {
            int32 curRespawnDelay = int32(target->GetRespawnTimeEx() - GameTime::GetGameTime());
            if (curRespawnDelay < 0)
                curRespawnDelay = 0;

            std::string curRespawnDelayStr = secsToTimeString(curRespawnDelay, TimeFormat::ShortText);
            std::string defRespawnDelayStr = secsToTimeString(target->GetRespawnDelay(), TimeFormat::ShortText);

            handler->PSendSysMessage(LANG_COMMAND_RAWPAWNTIMES, defRespawnDelayStr.c_str(), curRespawnDelayStr.c_str());
        }
        return true;
    }

    /**
     * @brief 删除游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param spawnId 游戏对象的生成ID
     * @return 成功返回 true，失败返回 false
     *
     * @details 删除指定的游戏对象。
     *          - 如果对象有所有者，需要先从所有者移除
     *          - 从数据库中删除对象记录
     *          - 所有者必须是玩家，否则拒绝删除
     *
     * @note 调用时机：GM执行 .gobject delete 命令时
     *       性能注意事项：涉及数据库删除操作
     */
    //delete object by selection or guid
    static bool HandleGameObjectDeleteCommand(ChatHandler* handler, GameObjectSpawnId spawnId)
    {
        // 尝试获取已加载的游戏对象
        if (GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(spawnId))
        {
            Player const* const player = handler->GetSession()->GetPlayer();
            // 检查对象是否有所有者
            if (ObjectGuid ownerGuid = object->GetOwnerGUID())
            {
                Unit* owner = ObjectAccessor::GetUnit(*player, ownerGuid);
                // 所有者必须是玩家
                if (!owner || !ownerGuid.IsPlayer())
                {
                    handler->PSendSysMessage(LANG_COMMAND_DELOBJREFERCREATURE, ownerGuid.GetCounter(), spawnId);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                // 从所有者移除对象
                owner->RemoveGameObject(object, false);
            }
        }

        // 从数据库删除游戏对象
        if (GameObject::DeleteFromDB(spawnId))
        {
            handler->PSendSysMessage(LANG_COMMAND_DELOBJMESSAGE, spawnId);
            return true;
        }
        else
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, spawnId);
            handler->SetSentErrorMessage(true);
            return false;
        }
    }

    /**
     * @brief 旋转游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param guidLow 游戏对象的生成ID
     * @param oz 可选的偏航角（Z轴旋转），默认使用玩家朝向
     * @param oy 可选的俯仰角（Y轴旋转），默认0
     * @param ox 可选的翻滚角（X轴旋转），默认0
     * @return 成功返回 true，失败返回 false
     *
     * @details 旋转指定的游戏对象到指定角度。
     *          - 更新对象的位置和旋转角度
     *          - 保存到数据库
     *          - 重新生成对象以更新客户端缓存
     *
     * @note 调用时机：GM执行 .gobject turn 命令时
     *       性能注意事项：涉及数据库保存操作，需要重新生成对象
     *       技术说明：3.3.5a客户端会缓存最近删除的对象，需要完全重新生成以避免客户端使用旧位置
     */
    //turn selected object
    static bool HandleGameObjectTurnCommand(ChatHandler* handler, GameObjectSpawnId guidLow, Optional<float> oz, Optional<float> oy, Optional<float> ox)
    {
        if (!guidLow)
            return false;

        GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(guidLow);
        if (!object)
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, guidLow);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 如果未指定偏航角，使用玩家当前朝向
        if (!oz)
            oz = handler->GetSession()->GetPlayer()->GetOrientation();

        Map* map = object->GetMap();
        // 更新对象位置和旋转角度
        object->Relocate(object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(), *oz);
        object->SetLocalRotationAngles(*oz, oy.value_or(0.0f), ox.value_or(0.0f));
        object->SaveToDB();

        // Generate a completely new spawn with new guid
        // 3.3.5a client caches recently deleted objects and brings them back to life
        // when CreateObject block for this guid is received again
        // however it entirely skips parsing that block and only uses already known location
        // 生成一个完全新的spawn
        // 3.3.5a客户端会缓存最近删除的对象，当再次收到该guid的CreateObject数据块时会复活它们
        // 但是客户端会完全跳过解析该数据块，只使用已知的旧位置
        object->Delete();

        // 重新加载对象
        object = new GameObject();
        if (!object->LoadFromDB(guidLow, map, true))
        {
            delete object;
            return false;
        }

        handler->PSendSysMessage(LANG_COMMAND_TURNOBJMESSAGE, object->GetSpawnId(), object->GetGOInfo()->name.c_str(), object->GetSpawnId());
        return true;
    }

    /**
     * @brief 移动游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param guidLow 游戏对象的生成ID
     * @param xyz 可选的目标坐标[x, y, z]，默认移动到玩家当前位置
     * @return 成功返回 true，失败返回 false
     *
     * @details 移动指定的游戏对象到新位置。
     *          - 支持指定坐标或移动到玩家当前位置
     *          - 验证坐标有效性
     *          - 更新网格中的对象注册信息
     *          - 保存到数据库并重新生成对象
     *
     * @note 调用时机：GM执行 .gobject move 命令时
     *       性能注意事项：涉及数据库保存和网格更新操作
     */
    //move selected object
    static bool HandleGameObjectMoveCommand(ChatHandler* handler, GameObjectSpawnId guidLow, Optional<std::array<float,3>> xyz)
    {
        if (!guidLow)
            return false;

        GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(guidLow);
        if (!object)
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, guidLow);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Position pos;
        if (xyz)
        {
            // 使用指定坐标
            pos = { (*xyz)[0], (*xyz)[1], (*xyz)[2] };
            // 验证坐标是否有效
            if (!MapManager::IsValidMapCoord(object->GetMapId(), pos))
            {
                handler->PSendSysMessage(LANG_INVALID_TARGET_COORD, pos.GetPositionX(), pos.GetPositionY(), object->GetMapId());
                handler->SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            // 使用玩家当前位置
            pos = handler->GetSession()->GetPlayer()->GetPosition();
        }

        Map* map = object->GetMap();

        // 保留原有朝向，更新位置
        pos.SetOrientation(object->GetOrientation());
        object->Relocate(pos);

        // update which cell has this gameobject registered for loading
        // 更新网格中注册的游戏对象加载信息
        sObjectMgr->RemoveGameobjectFromGrid(guidLow, object->GetGameObjectData());
        object->SaveToDB();
        sObjectMgr->AddGameobjectToGrid(guidLow, object->GetGameObjectData());

        // Generate a completely new spawn with new guid
        // 3.3.5a client caches recently deleted objects and brings them back to life
        // when CreateObject block for this guid is received again
        // however it entirely skips parsing that block and only uses already known location
        // 生成一个完全新的spawn（原因同turn命令）
        object->Delete();

        object = new GameObject();
        if (!object->LoadFromDB(guidLow, map, true))
        {
            delete object;
            return false;
        }

        handler->PSendSysMessage(LANG_COMMAND_MOVEOBJMESSAGE, object->GetSpawnId(), object->GetGOInfo()->name.c_str(), object->GetSpawnId());
        return true;
    }

    /**
     * @brief 设置游戏对象相位命令处理
     * @param handler 聊天命令处理器
     * @param guidLow 游戏对象的生成ID
     * @param phaseMask 相位掩码值
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置指定游戏对象的相位掩码。
     *          - 相位掩码决定对象在哪些相位可见
     *          - 0值无效，将被拒绝
     *          - 更新后保存到数据库
     *
     * @note 调用时机：GM执行 .gobject set phase 命令时
     *       性能注意事项：涉及数据库保存操作
     */
    //set phasemask for selected object
    static bool HandleGameObjectSetPhaseCommand(ChatHandler* handler, GameObjectSpawnId guidLow, uint32 phaseMask)
    {
        if (!guidLow)
            return false;

        GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(guidLow);
        if (!object)
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, guidLow);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 相位掩码不能为0
        if (!phaseMask)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 设置相位掩码并保存
        object->SetPhaseMask(phaseMask, true);
        object->SaveToDB();
        return true;
    }

    /**
     * @brief 查找附近游戏对象命令处理
     * @param handler 聊天命令处理器
     * @param dist 可选的搜索距离，默认10.0
     * @return 总是返回 true
     *
     * @details 列出玩家周围指定距离内的所有游戏对象。
     *          - 查询数据库获取附近对象信息
     *          - 显示GUID、入口ID、名称、坐标等信息
     *          - 统计并显示找到的对象数量
     *
     * @note 调用时机：GM执行 .gobject near 命令时
     *       性能注意事项：执行数据库查询，距离越大查询越耗时
     */
    static bool HandleGameObjectNearCommand(ChatHandler* handler, Optional<float> dist)
    {
        float distance = dist.value_or(10.0f);
        uint32 count = 0;

        Player* player = handler->GetSession()->GetPlayer();

        // 准备查询语句：查找附近的游戏对象
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_GAMEOBJECT_NEAREST);
        stmt->setFloat(0, player->GetPositionX());
        stmt->setFloat(1, player->GetPositionY());
        stmt->setFloat(2, player->GetPositionZ());
        stmt->setUInt32(3, player->GetMapId());
        stmt->setFloat(4, player->GetPositionX());
        stmt->setFloat(5, player->GetPositionY());
        stmt->setFloat(6, player->GetPositionZ());
        // 使用距离的平方进行比较（避免开方运算）
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

                GameObjectTemplate const* gameObjectInfo = sObjectMgr->GetGameObjectTemplate(entry);

                if (!gameObjectInfo)
                    continue;

                handler->PSendSysMessage(LANG_GO_LIST_CHAT, guid, entry, guid, gameObjectInfo->name.c_str(), x, y, z, mapId, "", "");

                ++count;
            } while (result->NextRow());
        }

        handler->PSendSysMessage(LANG_COMMAND_NEAROBJMESSAGE, distance, count);
        return true;
    }

    /**
     * @brief 显示游戏对象详细信息命令处理
     * @param handler 聊天命令处理器
     * @param isGuid 可选参数，如果为"guid"表示使用生成ID查询
     * @param data 游戏对象数据，可以是入口ID、生成ID或超链接
     * @return 成功返回 true，失败返回 false
     *
     * @details 显示游戏对象的详细信息，包括：
     *          - 基础信息：GUID、入口ID、名称、类型、显示ID
     *          - 空间信息：坐标、旋转角度
     *          - 额外信息：阵营、标志、AI信息
     *          - 特殊信息：战利品ID（箱子和钓鱼洞）
     *          - 生成组信息（如果属于生成组）
     *
     * @note 调用时机：GM执行 .gobject info 命令时
     *       性能注意事项：仅查询操作，影响较小
     */
    //show info of gameobject
    static bool HandleGameObjectInfoCommand(ChatHandler* handler, Optional<EXACT_SEQUENCE("guid")> isGuid, Variant<Hyperlink<gameobject_entry>, Hyperlink<gameobject>, uint32> data)
    {
        uint32 entry = 0;
        uint32 type = 0;
        uint32 displayId = 0;
        std::string name;
        uint32 lootId = 0;

        GameObject* thisGO = nullptr;
        GameObjectData const* spawnData = nullptr;

        // 判断是按生成ID还是按入口ID查询
        ObjectGuid::LowType spawnId = 0;
        if (isGuid || data.holds_alternative<Hyperlink<gameobject>>())
        {
            // 按生成ID查询
            spawnId = *data;
            spawnData = sObjectMgr->GetGameObjectData(spawnId);
            if (!spawnData)
            {
                handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, spawnId);
                handler->SetSentErrorMessage(true);
                return false;
            }
            entry = spawnData->id;
            thisGO = handler->GetObjectFromPlayerMapByDbGuid(spawnId);
        }
        else
        {
            // 按入口ID查询
            entry = *data;
        }

        // 获取游戏对象模板
        GameObjectTemplate const* gameObjectInfo = sObjectMgr->GetGameObjectTemplate(entry);
        if (!gameObjectInfo)
        {
            handler->PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST, entry);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 提取基本信息
        type = gameObjectInfo->type;
        displayId = gameObjectInfo->displayId;
        name = gameObjectInfo->name;
        // 对于箱子和钓鱼洞类型，提取战利品ID
        if (type == GAMEOBJECT_TYPE_CHEST)
            lootId = gameObjectInfo->chest.lootId;
        else if (type == GAMEOBJECT_TYPE_FISHINGHOLE)
            lootId = gameObjectInfo->fishinghole.lootId;

        // If we have a real object, send some info about it
        // 如果有实际的游戏对象实例，发送额外信息
        if (thisGO)
        {
            handler->PSendSysMessage(LANG_SPAWNINFO_GUIDINFO, thisGO->GetGUID().ToString().c_str());
            handler->PSendSysMessage(LANG_SPAWNINFO_COMPATIBILITY_MODE, thisGO->GetRespawnCompatibilityMode());

            // 如果对象属于生成组，显示生成组信息
            if (thisGO->GetGameObjectData() && thisGO->GetGameObjectData()->spawnGroupData->groupId)
            {
                SpawnGroupTemplateData const* groupData = thisGO->GetGameObjectData()->spawnGroupData;
                handler->PSendSysMessage(LANG_SPAWNINFO_GROUP_ID, groupData->name.c_str(), groupData->groupId, groupData->flags, thisGO->GetMap()->IsSpawnGroupActive(groupData->groupId));
            }

            // 显示覆盖数据（优先使用特定覆盖，其次使用模板附加数据）
            GameObjectOverride const* goOverride = sObjectMgr->GetGameObjectOverride(spawnId);
            if (!goOverride)
                goOverride = sObjectMgr->GetGameObjectTemplateAddon(entry);
            if (goOverride)
                handler->PSendSysMessage(LANG_GOINFO_ADDON, goOverride->Faction, goOverride->Flags);
        }

        // 如果有生成数据，显示位置和旋转信息
        if (spawnData)
        {
            float yaw, pitch, roll;
            spawnData->rotation.toEulerAnglesZYX(yaw, pitch, roll);
            handler->PSendSysMessage(LANG_SPAWNINFO_SPAWNID_LOCATION, spawnData->spawnId, spawnData->spawnPoint.GetPositionX(), spawnData->spawnPoint.GetPositionY(), spawnData->spawnPoint.GetPositionZ());
            handler->PSendSysMessage(LANG_SPAWNINFO_ROTATION, yaw, pitch, roll);
        }

        // 显示模板基本信息
        handler->PSendSysMessage(LANG_GOINFO_ENTRY, entry);
        handler->PSendSysMessage(LANG_GOINFO_TYPE, type);
        handler->PSendSysMessage(LANG_GOINFO_LOOTID, lootId);
        handler->PSendSysMessage(LANG_GOINFO_DISPLAYID, displayId);
        handler->PSendSysMessage(LANG_GOINFO_NAME, name.c_str());
        handler->PSendSysMessage(LANG_GOINFO_SIZE, gameObjectInfo->size);
        handler->PSendSysMessage(LANG_OBJECTINFO_AIINFO, gameObjectInfo->AIName.c_str(), sObjectMgr->GetScriptName(gameObjectInfo->ScriptId).c_str());
        // 如果对象有AI，显示AI类型
        if (GameObjectAI const* ai = thisGO ? thisGO->AI() : nullptr)
            handler->PSendSysMessage(LANG_OBJECTINFO_AITYPE, Trinity::GetTypeName(*ai).c_str());

        // 显示模型边界框信息
        if (GameObjectDisplayInfoEntry const* modelInfo = sGameObjectDisplayInfoStore.LookupEntry(displayId))
            handler->PSendSysMessage(LANG_GOINFO_MODEL, modelInfo->GeoBoxMax.X, modelInfo->GeoBoxMax.Y, modelInfo->GeoBoxMax.Z, modelInfo->GeoBoxMin.X, modelInfo->GeoBoxMin.Y, modelInfo->GeoBoxMin.Z);

        return true;
    }

    /**
     * @brief 设置游戏对象状态命令处理
     * @param handler 聊天命令处理器
     * @param guidLow 游戏对象的生成ID
     * @param objectType 对象类型/状态类型（0-5或特殊负值）
     * @param objectState 可选的状态值
     * @return 成功返回 true，失败返回 false
     *
     * @details 设置游戏对象的各种状态属性：
     *          - objectType值：
     *            0: GOState（游戏对象状态，如开启/关闭）
     *            1: GoType（游戏对象类型）
     *            2: GoArtKit（外观套装）
     *            3: GoAnimProgress（动画进度）
     *            4: 自定义动画
     *            5: DestructibleState（可破坏状态）
     *            -1: 发送消失动画
     *            -2: 无效操作
     *
     * @note 调用时机：GM执行 .gobject set state 命令时
     *       性能注意事项：仅修改内存状态，不保存到数据库
     */
    static bool HandleGameObjectSetStateCommand(ChatHandler* handler, GameObjectSpawnId guidLow, int32 objectType, Optional<uint32> objectState)
    {
        if (!guidLow)
            return false;

        GameObject* object = handler->GetObjectFromPlayerMapByDbGuid(guidLow);
        if (!object)
        {
            handler->PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, guidLow);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 处理特殊负值类型
        if (objectType < 0)
        {
            if (objectType == -1)
                object->SendObjectDeSpawnAnim(object->GetGUID());
            else if (objectType == -2)
                return false;
            return true;
        }

        if (!objectState)
            return false;

        // 根据类型设置不同的状态
        switch (objectType)
        {
            case 0:
                // 设置游戏对象状态（如门的开/关）
                object->SetGoState(GOState(*objectState));
                break;
            case 1:
                // 设置游戏对象类型
                object->SetGoType(GameobjectTypes(*objectState));
                break;
            case 2:
                // 设置外观套装
                object->SetGoArtKit(*objectState);
                break;
            case 3:
                // 设置动画进度
                object->SetGoAnimProgress(*objectState);
                break;
            case 4:
                // 发送自定义动画
                object->SendCustomAnim(*objectState);
                break;
            case 5:
                // 设置可破坏状态，需要验证范围
                if (*objectState > GO_DESTRUCTIBLE_REBUILDING)
                    return false;

                object->SetDestructibleState(GameObjectDestructibleState(*objectState));
                break;
            default:
                break;
        }
        handler->PSendSysMessage("Set gobject type %d state %u", objectType, *objectState);
        return true;
    }
};

/**
 * @brief 注册游戏对象命令脚本
 *
 * @details 创建并注册游戏对象命令脚本实例到脚本系统中。
 *          该函数在服务器启动时被调用，用于初始化所有游戏对象相关命令。
 */
void AddSC_gobject_commandscript()
{
    new gobject_commandscript();
}
