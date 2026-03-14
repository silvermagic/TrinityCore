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
 * @file WorldDatabase.cpp
 * @brief 世界数据库连接实现文件
 *
 * 本文件实现了 WorldDatabaseConnection 类，负责与世界数据库建立连接并管理所有预编译语句。
 * 世界数据库存储了游戏世界相关的静态数据和动态数据，包括：
 * - 生物模板和实例数据
 * - 游戏对象数据
 * - 路点系统（移动路径）
 * - 重生点关联
 * - AI脚本（SmartAI）
 * - 商贩数据
 * - 传送点数据
 * - 墓地区域关联
 * - 游戏事件数据
 * - 生成组数据
 */

#include "WorldDatabase.h"
#include "MySQLPreparedStatement.h"

/**
 * @brief 准备所有世界数据库预编译语句
 *
 * 该函数初始化并准备所有与世界数据库交互所需的预编译 SQL 语句。
 * 预编译语句分为同步（CONNECTION_SYNCH）和异步（CONNECTION_ASYNC）两种类型：
 * - 同步语句：需要立即返回结果，用于查询操作
 * - 异步语句：不需要立即返回结果，用于插入、更新、删除操作
 *
 * 如果正在进行重连（m_reconnecting 为 true），则不会重新调整语句数组大小，
 * 而是直接覆盖现有的预编译语句。
 *
 * 准备的语句涵盖以下数据表操作：
 * - linked_respawn：生物/游戏对象的重生点关联
 * - creature_text：生物对话文本
 * - smart_scripts：SmartAI 脚本定义
 * - waypoints：SmartAI 路点
 * - creature：生物实例数据
 * - gameobject：游戏对象实例数据
 * - creature_template：生物模板数据
 * - waypoint_data：路点详细数据
 * - creature_addon：生物附加数据
 * - waypoint_scripts：路点脚本
 * - npc_vendor：NPC商贩售卖物品
 * - game_tele：传送点定义
 * - graveyard_zone：墓地区域关联
 * - creature_formations：生物编队
 * - game_event_*：游戏事件相关表
 * - disables：禁用项配置
 * - spawn_group：生成组成员
 */
void WorldDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_WORLDDATABASE_STATEMENTS);

    // 删除指定生物/游戏对象的重生点关联
    PrepareStatement(WORLD_DEL_LINKED_RESPAWN, "DELETE FROM linked_respawn WHERE guid = ? AND linkType  = ?", CONNECTION_ASYNC);
    // 删除指定主重生点的所有关联
    PrepareStatement(WORLD_DEL_LINKED_RESPAWN_MASTER, "DELETE FROM linked_respawn WHERE linkedGuid = ? AND linkType = ?", CONNECTION_ASYNC);
    // 替换（插入或更新）重生点关联
    PrepareStatement(WORLD_REP_LINKED_RESPAWN, "REPLACE INTO linked_respawn (guid, linkedGuid, linkType) VALUES (?, ?, ?)", CONNECTION_ASYNC);

    // 查询所有生物对话文本（用于NPC对话系统）
    PrepareStatement(WORLD_SEL_CREATURE_TEXT, "SELECT CreatureID, GroupID, ID, Text, Type, Language, Probability, Emote, Duration, Sound, BroadcastTextId, TextRange FROM creature_text", CONNECTION_SYNCH);

    // 查询所有 SmartAI 脚本（按入口GUID、来源类型、ID和链接排序）
    PrepareStatement(WORLD_SEL_SMART_SCRIPTS, "SELECT entryorguid, source_type, id, link, event_type, event_phase_mask, event_chance, event_flags, event_param1, event_param2, event_param3, event_param4, event_param5, action_type, action_param1, action_param2, action_param3, action_param4, action_param5, action_param6, target_type, target_param1, target_param2, target_param3, target_param4, target_x, target_y, target_z, target_o FROM smart_scripts ORDER BY entryorguid, source_type, id, link", CONNECTION_SYNCH);

    // 查询 SmartAI 路点数据（按入口和点ID排序）
    PrepareStatement(WORLD_SEL_SMARTAI_WP, "SELECT entry, pointid, position_x, position_y, position_z, orientation, delay FROM waypoints ORDER BY entry, pointid", CONNECTION_SYNCH);

    // 删除游戏对象实例
    PrepareStatement(WORLD_DEL_GAMEOBJECT, "DELETE FROM gameobject WHERE guid = ?", CONNECTION_ASYNC);
    // 删除游戏事件中的游戏对象关联
    PrepareStatement(WORLD_DEL_EVENT_GAMEOBJECT, "DELETE FROM game_event_gameobject WHERE guid = ?", CONNECTION_ASYNC);

    // 插入墓地区域关联（定义哪些区域的玩家可以复活到指定墓地）
    PrepareStatement(WORLD_INS_GRAVEYARD_ZONE, "INSERT INTO graveyard_zone (ID, GhostZone, Faction) VALUES (?, ?, ?)", CONNECTION_ASYNC);
    // 删除墓地区域关联
    PrepareStatement(WORLD_DEL_GRAVEYARD_ZONE, "DELETE FROM graveyard_zone WHERE ID = ? AND GhostZone = ? AND Faction = ?", CONNECTION_ASYNC);

    // 插入传送点定义（用于GM命令传送）
    PrepareStatement(WORLD_INS_GAME_TELE, "INSERT INTO game_tele (id, position_x, position_y, position_z, orientation, map, name) VALUES (?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    // 删除传送点
    PrepareStatement(WORLD_DEL_GAME_TELE, "DELETE FROM game_tele WHERE name = ?", CONNECTION_ASYNC);

    // 插入NPC商贩售卖物品
    PrepareStatement(WORLD_INS_NPC_VENDOR, "INSERT INTO npc_vendor (entry, item, maxcount, incrtime, extendedcost) VALUES(?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    // 删除NPC商贩售卖物品
    PrepareStatement(WORLD_DEL_NPC_VENDOR, "DELETE FROM npc_vendor WHERE entry = ? AND item = ?", CONNECTION_ASYNC);
    // 查询NPC商贩售卖物品列表（按槽位排序）
    PrepareStatement(WORLD_SEL_NPC_VENDOR_REF, "SELECT item, maxcount, incrtime, ExtendedCost FROM npc_vendor WHERE entry = ? ORDER BY slot ASC", CONNECTION_SYNCH);

    // 更新生物的移动类型
    PrepareStatement(WORLD_UPD_CREATURE_MOVEMENT_TYPE, "UPDATE creature SET MovementType = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新生物模板的阵营
    PrepareStatement(WORLD_UPD_CREATURE_FACTION, "UPDATE creature_template SET faction = ? WHERE entry = ?", CONNECTION_ASYNC);
    // 更新生物模板的NPC标志（如商人、任务给予者等）
    PrepareStatement(WORLD_UPD_CREATURE_NPCFLAG, "UPDATE creature_template SET npcflag = ? WHERE entry = ?", CONNECTION_ASYNC);
    // 更新生物实例的位置坐标
    PrepareStatement(WORLD_UPD_CREATURE_POSITION, "UPDATE creature SET position_x = ?, position_y = ?, position_z = ?, orientation = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新生物的游荡距离和移动类型
    PrepareStatement(WORLD_UPD_CREATURE_WANDER_DISTANCE, "UPDATE creature SET wander_distance = ?, MovementType = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新生物的重生时间（秒）
    PrepareStatement(WORLD_UPD_CREATURE_SPAWN_TIME_SECS, "UPDATE creature SET spawntimesecs = ? WHERE guid = ?", CONNECTION_ASYNC);

    // 插入生物编队定义（首领与成员的关系）
    PrepareStatement(WORLD_INS_CREATURE_FORMATION, "INSERT INTO creature_formations (leaderGUID, memberGUID, dist, angle, groupAI) VALUES (?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    // 插入路点数据
    PrepareStatement(WORLD_INS_WAYPOINT_DATA, "INSERT INTO waypoint_data (id, point, position_x, position_y, position_z, orientation) VALUES (?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);
    // 删除路点数据
    PrepareStatement(WORLD_DEL_WAYPOINT_DATA, "DELETE FROM waypoint_data WHERE id = ? AND point = ?", CONNECTION_ASYNC);
    // 删除路点后，将后续路点的编号前移
    PrepareStatement(WORLD_UPD_WAYPOINT_DATA_POINT, "UPDATE waypoint_data SET point = point - 1 WHERE id = ? AND point > ?", CONNECTION_ASYNC);
    // 更新路点位置
    PrepareStatement(WORLD_UPD_WAYPOINT_DATA_POSITION, "UPDATE waypoint_data SET position_x = ?, position_y = ?, position_z = ?, orientation = ? where id = ? AND point = ?", CONNECTION_ASYNC);
    // 更新路点关联的游戏对象GUID
    PrepareStatement(WORLD_UPD_WAYPOINT_DATA_WPGUID, "UPDATE waypoint_data SET wpguid = ? WHERE id = ? and point = ?", CONNECTION_ASYNC);

    // 查询最大的路点数据ID
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_MAX_ID, "SELECT MAX(id) FROM waypoint_data", CONNECTION_SYNCH);
    // 查询指定路点ID的最大点编号
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_MAX_POINT, "SELECT MAX(point) FROM waypoint_data WHERE id = ?", CONNECTION_SYNCH);
    // 查询指定路点ID的所有路点详情（按点编号排序）
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_BY_ID, "SELECT point, position_x, position_y, position_z, orientation, move_type, delay, action, action_chance FROM waypoint_data WHERE id = ? ORDER BY point", CONNECTION_SYNCH);
    // 查询指定路点ID的所有路点坐标
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_POS_BY_ID, "SELECT point, position_x, position_y, position_z, orientation FROM waypoint_data WHERE id = ?", CONNECTION_SYNCH);
    // 查询指定路点ID的第一个路点坐标
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_POS_FIRST_BY_ID, "SELECT position_x, position_y, position_z, orientation FROM waypoint_data WHERE point = 1 AND id = ?", CONNECTION_SYNCH);
    // 查询指定路点ID的最后一个路点坐标
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_POS_LAST_BY_ID, "SELECT position_x, position_y, position_z, orientation FROM waypoint_data WHERE id = ? ORDER BY point DESC LIMIT 1", CONNECTION_SYNCH);
    // 根据游戏对象GUID查询路点数据
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_BY_WPGUID, "SELECT id, point FROM waypoint_data WHERE wpguid = ?", CONNECTION_SYNCH);
    // 根据游戏对象GUID查询路点的所有详细信息
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_ALL_BY_WPGUID, "SELECT id, point, delay, move_type, action, action_chance FROM waypoint_data WHERE wpguid = ?", CONNECTION_SYNCH);
    // 清除所有路点的游戏对象GUID关联
    PrepareStatement(WORLD_UPD_WAYPOINT_DATA_ALL_WPGUID, "UPDATE waypoint_data SET wpguid = 0", CONNECTION_ASYNC);
    // 根据坐标范围查询路点（用于查找附近的路点）
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_BY_POS, "SELECT id, point FROM waypoint_data WHERE (abs(position_x - ?) <= ?) and (abs(position_y - ?) <= ?) and (abs(position_z - ?) <= ?)", CONNECTION_SYNCH);
    // 查询指定路点ID中所有关联了游戏对象的路点
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_WPGUID_BY_ID, "SELECT wpguid FROM waypoint_data WHERE id = ? and wpguid <> 0", CONNECTION_SYNCH);
    // 查询所有不重复的路点动作
    PrepareStatement(WORLD_SEL_WAYPOINT_DATA_ACTION, "SELECT DISTINCT action FROM waypoint_data", CONNECTION_SYNCH);
    // 查询路点脚本的最大GUID
    PrepareStatement(WORLD_SEL_WAYPOINT_SCRIPTS_MAX_ID, "SELECT MAX(guid) FROM waypoint_scripts", CONNECTION_SYNCH);

    // 插入生物附加数据（主要用于路点路径关联）
    PrepareStatement(WORLD_INS_CREATURE_ADDON, "INSERT INTO creature_addon(guid, path_id) VALUES (?, ?)", CONNECTION_ASYNC);
    // 更新生物附加数据的路点路径ID
    PrepareStatement(WORLD_UPD_CREATURE_ADDON_PATH, "UPDATE creature_addon SET path_id = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 删除生物附加数据
    PrepareStatement(WORLD_DEL_CREATURE_ADDON, "DELETE FROM creature_addon WHERE guid = ?", CONNECTION_ASYNC);
    // 查询指定GUID是否存在生物附加数据
    PrepareStatement(WORLD_SEL_CREATURE_ADDON_BY_GUID, "SELECT guid FROM creature_addon WHERE guid = ?", CONNECTION_SYNCH);

    // 插入路点脚本
    PrepareStatement(WORLD_INS_WAYPOINT_SCRIPT, "INSERT INTO waypoint_scripts (guid) VALUES (?)", CONNECTION_ASYNC);
    // 删除路点脚本
    PrepareStatement(WORLD_DEL_WAYPOINT_SCRIPT, "DELETE FROM waypoint_scripts WHERE guid = ?", CONNECTION_ASYNC);
    // 更新路点脚本的ID
    PrepareStatement(WORLD_UPD_WAYPOINT_SCRIPT_ID, "UPDATE waypoint_scripts SET id = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新路点脚本的X坐标
    PrepareStatement(WORLD_UPD_WAYPOINT_SCRIPT_X, "UPDATE waypoint_scripts SET x = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新路点脚本的Y坐标
    PrepareStatement(WORLD_UPD_WAYPOINT_SCRIPT_Y, "UPDATE waypoint_scripts SET y = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新路点脚本的Z坐标
    PrepareStatement(WORLD_UPD_WAYPOINT_SCRIPT_Z, "UPDATE waypoint_scripts SET z = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新路点脚本的朝向
    PrepareStatement(WORLD_UPD_WAYPOINT_SCRIPT_O, "UPDATE waypoint_scripts SET o = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 根据GUID查询路点脚本的ID
    PrepareStatement(WORLD_SEL_WAYPOINT_SCRIPT_ID_BY_GUID, "SELECT id FROM waypoint_scripts WHERE guid = ?", CONNECTION_SYNCH);

    // 删除生物实例
    PrepareStatement(WORLD_DEL_CREATURE, "DELETE FROM creature WHERE guid = ?", CONNECTION_ASYNC);

    // 查询所有GM命令及其帮助信息
    PrepareStatement(WORLD_SEL_COMMANDS, "SELECT name, help FROM command", CONNECTION_SYNCH);

    // 查询生物模板详细信息（包含移动数据）
    PrepareStatement(WORLD_SEL_CREATURE_TEMPLATE, "SELECT entry, difficulty_entry_1, difficulty_entry_2, difficulty_entry_3, KillCredit1, KillCredit2, modelid1, modelid2, modelid3, modelid4, name, subname, IconName, gossip_menu_id, minlevel, maxlevel, exp, faction, npcflag, speed_walk, speed_run, scale, `rank`, dmgschool, BaseAttackTime, RangeAttackTime, BaseVariance, RangeVariance, unit_class, unit_flags, unit_flags2, dynamicflags, family, type, type_flags, lootid, pickpocketloot, skinloot, PetSpellDataId, VehicleId, mingold, maxgold, AIName, MovementType, ctm.Ground, ctm.Swim, ctm.Flight, ctm.Rooted, ctm.Chase, ctm.Random, ctm.InteractionPauseTimer, HoverHeight, HealthModifier, ManaModifier, ArmorModifier, DamageModifier, ExperienceModifier, RacialLeader, movementId, RegenHealth, mechanic_immune_mask, spell_school_immune_mask, flags_extra, ScriptName FROM creature_template ct LEFT JOIN creature_template_movement ctm ON ct.entry = ctm.CreatureId WHERE entry = ?", CONNECTION_SYNCH);

    // 根据ID查询路点脚本详情
    PrepareStatement(WORLD_SEL_WAYPOINT_SCRIPT_BY_ID, "SELECT guid, delay, command, datalong, datalong2, dataint, x, y, z, o FROM waypoint_scripts WHERE id = ?", CONNECTION_SYNCH);

    // 根据名称查询物品模板ID
    PrepareStatement(WORLD_SEL_ITEM_TEMPLATE_BY_NAME, "SELECT entry FROM item_template WHERE name = ?", CONNECTION_SYNCH);

    // 查询指定生物模板ID的所有实例GUID
    PrepareStatement(WORLD_SEL_CREATURE_BY_ID, "SELECT guid FROM creature WHERE id = ?", CONNECTION_SYNCH);

    // 查询距离指定坐标最近的游戏对象（按距离平方排序）
    PrepareStatement(WORLD_SEL_GAMEOBJECT_NEAREST, "SELECT guid, id, position_x, position_y, position_z, map, (POW(position_x - ?, 2) + POW(position_y - ?, 2) + POW(position_z - ?, 2)) AS order_ FROM gameobject WHERE map = ? AND (POW(position_x - ?, 2) + POW(position_y - ?, 2) + POW(position_z - ?, 2)) <= ? ORDER BY order_", CONNECTION_SYNCH);

    // 查询距离指定坐标最近的生物（按距离平方排序）
    PrepareStatement(WORLD_SEL_CREATURE_NEAREST, "SELECT guid, id, position_x, position_y, position_z, map, (POW(position_x - ?, 2) + POW(position_y - ?, 2) + POW(position_z - ?, 2)) AS order_ FROM creature WHERE map = ? AND (POW(position_x - ?, 2) + POW(position_y - ?, 2) + POW(position_z - ?, 2)) <= ? ORDER BY order_", CONNECTION_SYNCH);

    // 插入新的生物实例
    PrepareStatement(WORLD_INS_CREATURE, "INSERT INTO creature (guid, id , map, spawnMask, phaseMask, modelid, equipment_id, position_x, position_y, position_z, orientation, spawntimesecs, wander_distance, currentwaypoint, curhealth, curmana, MovementType, npcflag, unit_flags, dynamicflags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    // 删除游戏事件中的生物关联
    PrepareStatement(WORLD_DEL_GAME_EVENT_CREATURE, "DELETE FROM game_event_creature WHERE guid = ?", CONNECTION_ASYNC);
    // 删除游戏事件中的模型装备关联
    PrepareStatement(WORLD_DEL_GAME_EVENT_MODEL_EQUIP, "DELETE FROM game_event_model_equip WHERE guid = ?", CONNECTION_ASYNC);

    // 插入新的游戏对象实例
    PrepareStatement(WORLD_INS_GAMEOBJECT, "INSERT INTO gameobject (guid, id, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", CONNECTION_ASYNC);

    // 插入禁用项配置
    PrepareStatement(WORLD_INS_DISABLES, "INSERT INTO disables (entry, sourceType, flags, comment) VALUES (?, ?, ?, ?)", CONNECTION_ASYNC);
    // 查询指定的禁用项
    PrepareStatement(WORLD_SEL_DISABLES, "SELECT entry FROM disables WHERE entry = ? AND sourceType = ?", CONNECTION_SYNCH);
    // 删除禁用项
    PrepareStatement(WORLD_DEL_DISABLES, "DELETE FROM disables WHERE entry = ? AND sourceType = ?", CONNECTION_ASYNC);

    // 更新生物的区域ID和区域ID
    PrepareStatement(WORLD_UPD_CREATURE_ZONE_AREA_DATA, "UPDATE creature SET zoneId = ?, areaId = ? WHERE guid = ?", CONNECTION_ASYNC);
    // 更新游戏对象的区域ID和区域ID
    PrepareStatement(WORLD_UPD_GAMEOBJECT_ZONE_AREA_DATA, "UPDATE gameobject SET zoneId = ?, areaId = ? WHERE guid = ?", CONNECTION_ASYNC);

    // 删除生成组成员
    PrepareStatement(WORLD_DEL_SPAWNGROUP_MEMBER, "DELETE FROM spawn_group WHERE spawnType = ? AND spawnId = ?", CONNECTION_ASYNC);
    // 删除游戏对象附加数据
    PrepareStatement(WORLD_DEL_GAMEOBJECT_ADDON, "DELETE FROM gameobject_addon WHERE guid = ?", CONNECTION_ASYNC);
}

/**
 * @brief 构造函数（无队列模式）
 *
 * 创建一个世界数据库连接实例，不使用生产者-消费者队列。
 * 这种模式的连接通常用于同步操作或独立运行的线程。
 *
 * @param connInfo MySQL连接信息，包含主机、端口、用户名、密码、数据库名等
 */
WorldDatabaseConnection::WorldDatabaseConnection(MySQLConnectionInfo& connInfo) : MySQLConnection(connInfo)
{
}

/**
 * @brief 构造函数（队列模式）
 *
 * 创建一个世界数据库连接实例，使用生产者-消费者队列。
 * 这种模式的连接用于异步数据库操作，可以将SQL操作放入队列中由工作线程执行。
 *
 * @param q SQL操作的生产者-消费者队列指针
 * @param connInfo MySQL连接信息，包含主机、端口、用户名、密码、数据库名等
 */
WorldDatabaseConnection::WorldDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo) : MySQLConnection(q, connInfo)
{
}

/**
 * @brief 析构函数
 *
 * 销毁世界数据库连接实例。
 * 基类 MySQLConnection 会自动关闭数据库连接并释放相关资源。
 */
WorldDatabaseConnection::~WorldDatabaseConnection()
{
}
