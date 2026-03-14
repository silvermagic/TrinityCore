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
 * @file databasePCH.h
 * @brief 数据库模块预编译头文件
 *
 * 本文件定义了数据库模块的预编译头（Precompiled Header），包含了数据库子系统中
 * 最常用的头文件。使用预编译头可以显著减少编译时间，因为这些头文件的内容只需要
 * 编译一次，后续编译可以直接重用预编译的结果。
 *
 * 预编译头适用于以下场景：
 * - 项目中大量文件包含相同的头文件集合
 * - 这些头文件的内容变化不频繁
 * - 需要优化大型项目的编译速度
 *
 * 包含的头文件分类：
 * - 基础定义：Define.h, Errors.h
 * - 日志系统：Log.h
 * - 数据库核心：Field.h, MySQLConnection.h, PreparedStatement.h
 * - 查询相关：QueryResult.h, SQLOperation.h, Transaction.h
 * - 第三方库：MySQL客户端库（mysql.h）
 * - 标准库：string, vector
 *
 * @note 在 Windows 平台上需要包含 winsock2.h，以解决 MySQL 头文件在 5.7 版本之前
 *       未正确包含 winsock 定义的问题
 *
 * @see CMakeLists.txt 中的 USE_COREPCH 选项控制预编译头的启用
 */

// 基础类型定义和宏
#include "Define.h"          ///< 基础类型定义、平台相关宏和编译器属性宏

// 错误处理
#include "Errors.h"          ///< 断言宏和错误处理函数，用于运行时检查和异常报告

// 数据库字段
#include "Field.h"           ///< 数据库字段类，用于表示查询结果中的单个字段数据

// 日志系统
#include "Log.h"             ///< 日志记录系统，提供不同级别的日志输出功能

// MySQL 数据库连接
#include "MySQLConnection.h"  ///< MySQL 数据库连接类，封装了 MySQL 连接的生命周期管理

// 预处理语句
#include "PreparedStatement.h"  ///< 预处理语句类，支持参数化查询以防止 SQL 注入并提升性能

// 查询结果
#include "QueryResult.h"     ///< 查询结果集类，用于存储和遍历数据库查询返回的数据

// SQL 操作
#include "SQLOperation.h"    ///< SQL 操作基类，定义了异步数据库操作的接口

// 事务处理
#include "Transaction.h"     ///< 事务类，支持多个 SQL 语句的原子性执行

#ifdef _WIN32
// Windows 平台特定：修复 MySQL 头文件在 5.7 版本之前未正确包含 winsock 定义的问题
// MySQL 的 SOCKET 类型定义需要 winsock2.h 支持
#include <winsock2.h>        ///< Windows Socket API，提供 SOCKET 类型定义
#endif

// MySQL 客户端库
#include <mysql.h>           ///< MySQL C API，提供底层数据库访问接口

// C++ 标准库
#include <string>            ///< std::string 字符串类，广泛用于数据库文本字段
#include <vector>            ///< std::vector 动态数组容器，用于存储查询结果集
