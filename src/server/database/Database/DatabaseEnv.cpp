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
 * @file DatabaseEnv.cpp
 * @brief 数据库环境全局对象定义文件
 *
 * 本文件定义了 TrinityCore 服务器使用的三个核心数据库连接池全局对象。
 * 这些数据库连接池是整个服务器数据库操作的基础设施,为所有数据库访问提供统一的接口。
 *
 * 数据库架构说明:
 * - 世界数据库 (WorldDatabase): 存储游戏世界静态数据,如NPC、物品、法术等模板数据
 * - 角色数据库 (CharacterDatabase): 存储玩家角色相关的动态数据,如角色信息、装备、任务进度等
 * - 登录数据库 (LoginDatabase): 存储账号认证相关信息,如账号数据、会话令牌等
 *
 * 使用方式:
 * 这些全局对象在整个服务器运行期间全局可访问,通过 DatabaseWorkerPool 提供的接口
 * 可以执行同步和异步数据库查询操作。所有数据库操作都经过连接池管理,确保高效、
 * 线程安全的数据库访问。
 *
 * @see DatabaseWorkerPool
 * @see WorldDatabaseConnection
 * @see CharacterDatabaseConnection
 * @see LoginDatabaseConnection
 */

#include "DatabaseEnv.h"

/**
 * @brief 世界数据库连接池全局对象
 *
 * 世界数据库存储游戏世界的静态配置数据,这些数据通常是只读的,
 * 在服务器启动时加载并在运行过程中查询使用。
 *
 * 主要存储内容:
 * - 怪物和NPC模板 (creature_template 表)
 * - 物品模板 (item_template 表)
 * - 法术模板 (spell_dbc 等相关表)
 * - 任务模板 (quest_template 表)
 * - 游戏对象模板 (gameobject_template 表)
 * - 掉落表和战利品模板
 * - 区域和地图信息
 * - 技能和天赋数据
 * - 其他游戏规则数据
 *
 * 访问特性:
 * - 主要是读取操作,写入操作极少(通常只在数据库更新时)
 * - 高并发读取场景,需要优化的查询性能
 * - 数据变更通常需要服务器重启或重载配置才能生效
 *
 * 使用示例:
 * @code
 * // 同步查询示例
 * QueryResult result = WorldDatabase.Query("SELECT * FROM creature_template WHERE entry = {}", creatureId);
 *
 * // 异步查询示例
 * WorldDatabase.AsyncQuery("UPDATE creature_template SET name = '{}' WHERE entry = {}", newName, creatureId);
 * @endcode
 */
DatabaseWorkerPool<WorldDatabaseConnection> WorldDatabase;

/**
 * @brief 角色数据库连接池全局对象
 *
 * 角色数据库存储玩家角色的所有动态数据,这些数据频繁读写,
 * 是游戏中变更最频繁的数据库。
 *
 * 主要存储内容:
 * - 角色基础信息 (characters 表): 名称、种族、职业、等级、位置等
 * - 角色装备和物品 (character_inventory, item_instance 表)
 * - 角色技能和法术 (character_spell, character_spell_cooldown 表)
 * - 角色任务进度 (character_queststatus 表)
 * - 角色成就 (character_achievement, character_achievement_progress 表)
 * - 角色声望 (character_reputation 表)
 * - 角色好友列表和社交数据 (character_social 表)
 * - 角色动作条布局 (character_action 表)
 * - 角色公会信息 (guild_member 表)
 * - 角色邮件 (mail, mail_items 表)
 * - 角色拍卖行数据 (auctionhouse 表)
 *
 * 访问特性:
 * - 高频率的读写操作
 * - 需要事务支持以保证数据一致性
 * - 玩家登录/登出时会有大量数据库操作
 * - 需要定期备份以防止数据丢失
 *
 * 使用示例:
 * @code
 * // 使用事务保证数据一致性
 * CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
 * trans->Append("UPDATE characters SET level = {} WHERE guid = {}", newLevel, playerGuid);
 * trans->Append("INSERT INTO character_queststatus (guid, quest, status) VALUES ({}, {}, {})", playerGuid, questId, status);
 * CharacterDatabase.CommitTransaction(trans);
 * @endcode
 */
DatabaseWorkerPool<CharacterDatabaseConnection> CharacterDatabase;

/**
 * @brief 登录数据库连接池全局对象
 *
 * 登录数据库存储账号相关的认证和管理数据,主要用于登录验证和账号管理。
 *
 * 主要存储内容:
 * - 账号信息 (account 表): 用户名、密码哈希、会话密钥、权限等级等
 * - 账号会话数据 (account_access 表): GM权限级别、Realm分配等
 * - 账号封禁记录 (account_banned 表): 封禁时间、封禁原因等
 * - IP封禁记录 (ip_banned 表): 封禁的IP地址范围
 * - Realm列表 (realmlist 表): 可用的游戏服务器列表
 * - 登录票据和令牌 (account_last_played_character 表等)
 * - Uptime统计 (uptime 表): 服务器运行时间记录
 *
 * 访问特性:
 * - 主要在登录阶段访问
 * - 读操作多于写操作
 * - 对安全性要求极高(存储敏感信息如密码哈希)
 * - 需要支持跨Realm的账号数据共享
 *
 * 使用示例:
 * @code
 * // 验证账号登录
 * QueryResult result = LoginDatabase.Query(
 *     "SELECT id, username, sessionkey FROM account WHERE username = '{}'", username);
 *
 * // 更新会话密钥
 * LoginDatabase.DirectExecute(
 *     "UPDATE account SET sessionkey = '{}' WHERE id = {}", sessionKey, accountId);
 * @endcode
 */
DatabaseWorkerPool<LoginDatabaseConnection> LoginDatabase;
