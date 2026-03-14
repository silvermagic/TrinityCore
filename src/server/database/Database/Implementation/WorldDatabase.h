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
 * @file WorldDatabase.h
 * @brief 世界数据库连接模块
 *
 * 本文件定义了世界数据库(World Database)的连接类和预编译语句标识符。
 * 世界数据库存储了游戏世界的静态数据，如生物、游戏对象、路径点、
 * 复活点、传送点等核心游戏内容。该模块提供对这些数据的高效访问和修改接口。
 */

#ifndef _WORLDDATABASE_H
#define _WORLDDATABASE_H

#include "MySQLConnection.h"

/**
 * @brief 世界数据库预编译语句标识符枚举
 *
 * 定义了所有世界数据库操作使用的预编译SQL语句ID。
 * 命名规范: {DB}_{操作类型}_{数据变更摘要}
 * - 操作类型: SEL(查询), INS(插入), UPD(更新), DEL(删除), REP(替换)
 * - 数据变更摘要: 简要描述操作的数据内容
 * 当更新多个字段时，可参考调用函数名作为后缀。
 */
enum WorldDatabaseStatements : uint32
{
    /*  Naming standard for defines:
        {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
        When updating more than one field, consider looking at the calling function
        name for a suiting suffix.
    */

    WORLD_DEL_LINKED_RESPAWN,                    ///< 删除关联重生记录
    WORLD_DEL_LINKED_RESPAWN_MASTER,             ///< 删除指定主对象的关联重生记录
    WORLD_REP_LINKED_RESPAWN,                    ///< 替换(插入或更新)关联重生记录
    WORLD_SEL_CREATURE_TEXT,                     ///< 查询生物文本信息
    WORLD_SEL_SMART_SCRIPTS,                     ///< 查询SmartAI脚本
    WORLD_SEL_SMARTAI_WP,                        ///< 查询SmartAI路径点
    WORLD_DEL_GAMEOBJECT,                        ///< 删除游戏对象
    WORLD_DEL_EVENT_GAMEOBJECT,                  ///< 删除事件游戏对象
    WORLD_INS_GRAVEYARD_ZONE,                    ///< 插入墓地区域关联
    WORLD_DEL_GRAVEYARD_ZONE,                    ///< 删除墓地区域关联
    WORLD_INS_GAME_TELE,                         ///< 插入游戏传送点
    WORLD_DEL_GAME_TELE,                         ///< 删除游戏传送点
    WORLD_INS_NPC_VENDOR,                        ///< 插入NPC商人条目
    WORLD_DEL_NPC_VENDOR,                        ///< 删除NPC商人条目
    WORLD_SEL_NPC_VENDOR_REF,                    ///< 查询NPC商人引用数据
    WORLD_UPD_CREATURE_MOVEMENT_TYPE,            ///< 更新生物移动类型
    WORLD_UPD_CREATURE_FACTION,                  ///< 更新生物阵营
    WORLD_UPD_CREATURE_NPCFLAG,                  ///< 更新生物NPC标志
    WORLD_UPD_CREATURE_POSITION,                 ///< 更新生物位置
    WORLD_UPD_CREATURE_WANDER_DISTANCE,          ///< 更新生物游荡距离
    WORLD_UPD_CREATURE_SPAWN_TIME_SECS,          ///< 更新生物生成时间(秒)
    WORLD_INS_CREATURE_FORMATION,                ///< 插入生物编队
    WORLD_INS_WAYPOINT_DATA,                     ///< 插入路径点数据
    WORLD_DEL_WAYPOINT_DATA,                     ///< 删除路径点数据
    WORLD_UPD_WAYPOINT_DATA_POINT,               ///< 更新路径点编号
    WORLD_UPD_WAYPOINT_DATA_POSITION,            ///< 更新路径点位置
    WORLD_UPD_WAYPOINT_DATA_WPGUID,              ///< 更新路径点GUID
    WORLD_UPD_WAYPOINT_DATA_ALL_WPGUID,          ///< 更新所有路径点GUID
    WORLD_SEL_WAYPOINT_DATA_MAX_ID,              ///< 查询路径点最大ID
    WORLD_SEL_WAYPOINT_DATA_BY_ID,               ///< 根据ID查询路径点数据
    WORLD_SEL_WAYPOINT_DATA_POS_BY_ID,           ///< 根据ID查询路径点位置
    WORLD_SEL_WAYPOINT_DATA_POS_FIRST_BY_ID,     ///< 根据ID查询第一个路径点位置
    WORLD_SEL_WAYPOINT_DATA_POS_LAST_BY_ID,      ///< 根据ID查询最后一个路径点位置
    WORLD_SEL_WAYPOINT_DATA_BY_WPGUID,           ///< 根据路径点GUID查询数据
    WORLD_SEL_WAYPOINT_DATA_ALL_BY_WPGUID,       ///< 根据路径点GUID查询所有数据
    WORLD_SEL_WAYPOINT_DATA_MAX_POINT,           ///< 查询最大路径点编号
    WORLD_SEL_WAYPOINT_DATA_BY_POS,              ///< 根据位置查询路径点
    WORLD_SEL_WAYPOINT_DATA_WPGUID_BY_ID,        ///< 根据ID查询路径点GUID
    WORLD_SEL_WAYPOINT_DATA_ACTION,              ///< 查询路径点动作
    WORLD_SEL_WAYPOINT_SCRIPTS_MAX_ID,           ///< 查询路径点脚本最大ID
    WORLD_UPD_CREATURE_ADDON_PATH,               ///< 更新生物附加数据中的路径ID
    WORLD_INS_CREATURE_ADDON,                    ///< 插入生物附加数据
    WORLD_DEL_CREATURE_ADDON,                    ///< 删除生物附加数据
    WORLD_SEL_CREATURE_ADDON_BY_GUID,            ///< 根据GUID查询生物附加数据
    WORLD_INS_WAYPOINT_SCRIPT,                   ///< 插入路径点脚本
    WORLD_DEL_WAYPOINT_SCRIPT,                   ///< 删除路径点脚本
    WORLD_UPD_WAYPOINT_SCRIPT_ID,                ///< 更新路径点脚本ID
    WORLD_UPD_WAYPOINT_SCRIPT_X,                 ///< 更新路径点脚本X坐标
    WORLD_UPD_WAYPOINT_SCRIPT_Y,                 ///< 更新路径点脚本Y坐标
    WORLD_UPD_WAYPOINT_SCRIPT_Z,                 ///< 更新路径点脚本Z坐标
    WORLD_UPD_WAYPOINT_SCRIPT_O,                 ///< 更新路径点脚本朝向
    WORLD_SEL_WAYPOINT_SCRIPT_ID_BY_GUID,        ///< 根据GUID查询路径点脚本ID
    WORLD_DEL_CREATURE,                          ///< 删除生物
    WORLD_SEL_COMMANDS,                          ///< 查询GM命令列表
    WORLD_SEL_CREATURE_TEMPLATE,                 ///< 查询生物模板
    WORLD_SEL_WAYPOINT_SCRIPT_BY_ID,             ///< 根据ID查询路径点脚本
    WORLD_SEL_ITEM_TEMPLATE_BY_NAME,             ///< 根据名称查询物品模板
    WORLD_SEL_CREATURE_BY_ID,                    ///< 根据ID查询生物
    WORLD_SEL_GAMEOBJECT_NEAREST,                ///< 查询最近的游戏对象
    WORLD_SEL_CREATURE_NEAREST,                  ///< 查询最近的生物
    WORLD_SEL_GAMEOBJECT_TARGET,                 ///< 查询目标游戏对象
    WORLD_INS_CREATURE,                          ///< 插入生物
    WORLD_DEL_GAME_EVENT_CREATURE,               ///< 删除游戏事件生物
    WORLD_DEL_GAME_EVENT_MODEL_EQUIP,            ///< 删除游戏事件模型装备
    WORLD_INS_GAMEOBJECT,                        ///< 插入游戏对象
    WORLD_SEL_DISABLES,                          ///< 查询禁用条目
    WORLD_INS_DISABLES,                          ///< 插入禁用条目
    WORLD_DEL_DISABLES,                          ///< 删除禁用条目
    WORLD_UPD_CREATURE_ZONE_AREA_DATA,           ///< 更新生物区域数据
    WORLD_UPD_GAMEOBJECT_ZONE_AREA_DATA,         ///< 更新游戏对象区域数据
    WORLD_DEL_SPAWNGROUP_MEMBER,                 ///< 删除生成组成员
    WORLD_DEL_GAMEOBJECT_ADDON,                  ///< 删除游戏对象附加数据

    MAX_WORLDDATABASE_STATEMENTS                 ///< 预编译语句总数(边界标记)
};

/**
 * @brief 世界数据库连接类
 *
 * 继承自 MySQLConnection，专门用于处理与世界数据库的连接。
 * 负责管理世界数据库的预编译语句，提供同步和异步两种连接模式。
 * 世界数据库存储游戏世界的核心静态数据，包括：
 * - 生物(Creature)模板和实例数据
 * - 游戏对象(GameObject)数据
 * - 路径点和移动数据
 * - 墓地和传送点信息
 * - NPC商人和物品数据
 * - SmartAI脚本和配置
 */
class TC_DATABASE_API WorldDatabaseConnection : public MySQLConnection
{
public:
    typedef WorldDatabaseStatements Statements;  ///< 预编译语句类型别名

    /**
     * @brief 构造同步数据库连接
     *
     * 创建一个同步执行的世界数据库连接，适用于需要立即返回结果的场景。
     *
     * @param connInfo MySQL连接信息，包含主机、端口、用户名、密码、数据库名等
     */
    WorldDatabaseConnection(MySQLConnectionInfo& connInfo);

    /**
     * @brief 构造异步数据库连接
     *
     * 创建一个异步执行的世界数据库连接，通过队列处理SQL操作。
     * 适用于高并发场景，可以避免阻塞主线程。
     *
     * @param q SQL操作队列，用于异步处理数据库请求
     * @param connInfo MySQL连接信息，包含主机、端口、用户名、密码、数据库名等
     */
    WorldDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo);

    /**
     * @brief 析构函数
     *
     * 清理数据库连接资源。
     */
    ~WorldDatabaseConnection();

    /**
     * @brief 准备数据库特定的预编译语句
     *
     * 重写父类方法，初始化所有世界数据库操作的预编译SQL语句。
     * 预编译语句可以提高执行效率并防止SQL注入攻击。
     */
    void DoPrepareStatements() override;
};

#endif
