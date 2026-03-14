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

#ifndef MySQLHacks_h__
#define MySQLHacks_h__

#include "MySQLWorkaround.h"
#include <type_traits>

/**
 * @file MySQLHacks.h
 * @brief MySQL 类型包装和兼容性处理
 *
 * 本文件提供了一系列 MySQL C API 类型的包装结构体,以及跨 MySQL 版本的兼容性处理。
 *
 * 主要功能:
 * - 为 MySQL 原生类型提供类型安全的包装
 * - 解决 MySQL 5.x 和 MySQL 8.x 之间的 API 差异
 * - 提供统一的类型定义,便于代码维护
 *
 * @note 这些包装结构体继承自 MySQL 原生类型,不添加额外的数据成员,
 *       仅用于类型区分和提高代码可读性。
 */

/**
 * @brief MySQL 数据库连接句柄包装结构体
 *
 * 继承自 MySQL 原生的 MYSQL 结构体,用于表示数据库连接句柄。
 * 提供更好的类型安全性和代码可读性。
 *
 * 用途:
 * - 管理与 MySQL 服务器的连接
 * - 作为数据库操作的上下文句柄
 *
 * @note 此结构体不添加任何额外的数据成员或方法,仅作为类型别名使用。
 */
struct MySQLHandle : MYSQL { };

/**
 * @brief MySQL 查询结果集包装结构体
 *
 * 继承自 MySQL 原生的 MYSQL_RES 结构体,用于表示查询结果集。
 * 存储从数据库查询返回的多行数据。
 *
 * 用途:
 * - 存储和遍历 SELECT 查询的结果集
 * - 访问结果集中的字段和行数据
 *
 * @note 必须在不再使用时调用相应的释放函数以避免内存泄漏。
 */
struct MySQLResult : MYSQL_RES { };

/**
 * @brief MySQL 字段信息包装结构体
 *
 * 继承自 MySQL 原生的 MYSQL_FIELD 结构体,用于描述结果集中字段的元数据。
 * 包含字段名称、类型、长度等信息。
 *
 * 用途:
 * - 获取字段名称、类型、大小等元数据信息
 * - 在结果集处理中识别字段属性
 *
 * @note 字段信息在结果集的生命周期内保持有效。
 */
struct MySQLField : MYSQL_FIELD { };

/**
 * @brief MySQL 绑定参数包装结构体
 *
 * 继承自 MySQL 原生的 MYSQL_BIND 结构体,用于预处理语句的参数绑定。
 * 支持输入参数和输出结果的绑定。
 *
 * 用途:
 * - 将 C++ 变量绑定到预处理语句的参数占位符
 * - 绑定变量以接收查询结果
 * - 处理 NULL 值和数据类型转换
 *
 * @note 绑定结构体必须在语句执行期间保持有效。
 */
struct MySQLBind : MYSQL_BIND { };

/**
 * @brief MySQL 预处理语句包装结构体
 *
 * 继承自 MySQL 原生的 MYSQL_STMT 结构体,用于表示预处理语句句柄。
 * 预处理语句可以提高重复执行相同 SQL 语句的性能,并防止 SQL 注入。
 *
 * 用途:
 * - 准备和执行预处理 SQL 语句
 * - 绑定参数和结果
 * - 管理语句的生命周期
 *
 * @note 预处理语句必须在使用完毕后正确关闭以释放资源。
 */
struct MySQLStmt : MYSQL_STMT { };

/**
 * @brief MySQL 布尔类型别名 - 跨版本兼容性处理
 *
 * MySQL 8.0 移除了 my_bool typedef(原本定义为 char),改为直接使用 bool 类型。
 * 为了保持与 MySQL 5.x 和 8.x 的兼容性,使用类型推导自动确定正确的类型。
 *
 * 实现原理:
 * - 通过 decltype 获取 MYSQL_BIND 结构体中 is_null 指针的类型
 * - 使用 std::remove_pointer_t 移除指针修饰符,得到实际的布尔类型
 * - 在 MySQL 5.x 中推导为 char,在 MySQL 8.x 中推导为 bool
 *
 * 用途:
 * - 在绑定参数时指定 NULL 指示器变量的类型
 * - 确保代码在不同 MySQL 版本间兼容
 *
 * @note 这是处理 MySQL API 版本差异的关键技巧,避免了硬编码类型导致的兼容性问题。
 *
 * 示例用法:
 * @code
 * MySQLBind bind;
 * MySQLBool is_null_value = false;
 * bind.is_null = &is_null_value;
 * @endcode
 */
using MySQLBool = std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().is_null)>;

#endif // MySQLHacks_h__
