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
 * @file DatabaseEnv.h
 * @brief 数据库环境头文件
 *
 * 本文件是 TrinityCore 数据库访问层的核心头文件，提供统一的数据库访问接口。
 * 它定义了三个主要的数据库连接池全局访问器：
 * - WorldDatabase: 世界数据库，存储游戏静态数据（NPC、物品、副本等模板数据）
 * - CharacterDatabase: 角色数据库，存储玩家角色数据（装备、任务进度、技能等）
 * - LoginDatabase: 登录数据库，存储账号认证和服务器列表数据
 *
 * 设计模式：
 * 使用单例模式通过全局变量提供数据库访问，简化各模块对数据库的访问。
 * 使用 DatabaseWorkerPool 实现异步数据库操作，提高服务器性能。
 *
 * 使用示例：
 * @code
 * // 同步查询
 * QueryResult result = WorldDatabase.Query("SELECT * FROM creature_template WHERE entry = 1");
 *
 * // 异步执行
 * CharacterDatabase.Execute("UPDATE characters SET money = {} WHERE guid = {}", money, guid);
 *
 * // 预编译语句
 * auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MONEY);
 * stmt->SetData(0, money);
 * stmt->SetData(1, guid);
 * CharacterDatabase.Execute(stmt);
 * @endcode
 */

#ifndef DATABASEENV_H
#define DATABASEENV_H

#include "Define.h"
#include "DatabaseWorkerPool.h"

// 引入各数据库连接实现类
#include "Implementation/LoginDatabase.h"
#include "Implementation/CharacterDatabase.h"
#include "Implementation/WorldDatabase.h"

// 引入数据库操作相关辅助类
#include "Field.h"              // 字段类，用于读取查询结果中的单个字段值
#include "PreparedStatement.h"  // 预编译语句类，用于参数化查询，防止SQL注入
#include "QueryCallback.h"      // 查询回调类，用于异步查询的结果处理
#include "QueryResult.h"        // 查询结果类，封装查询返回的数据集
#include "Transaction.h"        // 事务类，用于保证多个数据库操作的原子性

/**
 * @brief 世界数据库全局访问器
 *
 * 世界数据库存储游戏的静态配置数据，包括：
 * - 生物模板（creature_template）
 * - 物品模板（item_template）
 * - 游戏对象模板（gameobject_template）
 * - 任务模板（quest_template）
 * - 地图和区域信息
 * - 技能和法术数据
 * - NPC文本和对话
 * - 掉落表和战利品模板
 *
 * 这些数据通常是只读的，由数据库管理工具或脚本导入，
 * 服务器运行时主要进行读取操作。
 *
 * 性能注意事项：
 * - 使用预编译语句避免重复解析SQL
 * - 异步操作应使用 CONNECTION_ASYNC 标记的语句
 * - 批量读取数据时考虑使用多结果集查询
 */
TC_DATABASE_API extern DatabaseWorkerPool<WorldDatabaseConnection> WorldDatabase;

/**
 * @brief 角色数据库全局访问器
 *
 * 角色数据库存储玩家动态数据，包括：
 * - 角色基本信息（characters表）
 * - 角色物品和装备（character_inventory, item_instance）
 * - 任务进度（character_queststatus）
 * - 技能和天赋（character_spell, character_talent）
 * - 成就和进度（character_achievement, character_achievement_progress）
 * - 声望（character_reputation）
 * - 公会信息（guild, guild_member）
 * - 邮件和拍卖（mail, auctionhouse）
 * - 好友列表和社交（character_social）
 * - PVP数据（character_arena_stats）
 *
 * 这些数据频繁读写，需要特别关注：
 * - 使用事务保证数据一致性
 * - 异步写入提高性能
 * - 定期备份防止数据丢失
 *
 * 性能注意事项：
 * - 角色保存时使用事务批量更新
 * - 避免在主线程执行耗时查询
 * - 合理使用异步操作防止阻塞游戏循环
 */
TC_DATABASE_API extern DatabaseWorkerPool<CharacterDatabaseConnection> CharacterDatabase;

/**
 * @brief 登录数据库全局访问器
 *
 * 登录数据库存储账号认证相关数据，包括：
 * - 账号信息（account表：用户名、密码、邮箱、最后登录等）
 * - 账号权限（account_access：GM等级、可用命令）
 * - 服务器列表（realmlist：服务器地址、端口、状态）
 * - 账号封禁（account_banned, ip_banned）
 * - 账号静默（account_muted）
 * - RBAC权限（rbac_account_permissions）
 * - 安全验证（totp_secret, secret_digest）
 * - 登录日志（logs_ip_actions）
 *
 * 安全注意事项：
 * - 密码使用SRP6协议存储，不明文保存
 * - 支持TOTP双因素认证
 * - 记录登录IP用于安全审计
 * - 支持账号锁定和IP封禁
 *
 * 跨服特性：
 * 登录数据库通常被多个世界服务器共享，
 * 实现跨服角色查询和统一账号管理。
 */
TC_DATABASE_API extern DatabaseWorkerPool<LoginDatabaseConnection> LoginDatabase;

#endif
