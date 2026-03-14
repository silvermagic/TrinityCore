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
 * @file MySQLThreading.cpp
 * @brief MySQL 线程库初始化和管理实现
 *
 * 本文件实现了 MySQL 客户端库的线程安全初始化和清理功能。
 * 这些函数是 TrinityCore 数据库系统的基础组件，确保 MySQL 库
 * 在多线程环境中正确运行。
 *
 * 主要功能包括：
 * - MySQL 库的全局初始化
 * - MySQL 库的资源清理
 * - 获取 MySQL 库版本信息
 *
 * @note MySQL 库必须在任何数据库操作之前初始化，并在程序结束时清理
 * @see MySQLThreading.h
 */

#include "MySQLThreading.h"
#include "MySQLWorkaround.h"

/**
 * @brief 初始化 MySQL 客户端库
 *
 * 调用 MySQL 的 mysql_library_init() 函数来初始化 MySQL 客户端库。
 * 这是使用 MySQL 功能前的必要步骤，必须在任何其他 MySQL 函数调用之前执行。
 *
 * @details
 * - 参数 -1 表示使用默认的命令行参数
 * - 两个 nullptr 参数表示不使用自定义的选项文件或组
 * - 该函数是线程安全的，但只应在程序启动时调用一次
 * - 通常在 TrinityCore 的数据库系统初始化时自动调用
 *
 * @warning 此函数必须在任何 MySQL 操作之前调用
 * @warning 必须与 Library_End() 配对使用，否则会导致资源泄漏
 *
 * @see Library_End()
 * @see mysql_library_init() MySQL 官方文档
 */
void MySQL::Library_Init()
{
    mysql_library_init(-1, nullptr, nullptr);
}

/**
 * @brief 清理并关闭 MySQL 客户端库
 *
 * 调用 MySQL 的 mysql_library_end() 函数来释放 MySQL 客户端库
 * 分配的所有资源。这是程序关闭时的必要清理步骤。
 *
 * @details
 * - 该函数应该在所有 MySQL 操作完成后调用
 * - 释放所有 MySQL 库分配的内部资源
 * - 必须在程序退出前调用，避免资源泄漏
 * - 调用此函数后，任何 MySQL 操作都是未定义行为
 * - 通常在 TrinityCore 的数据库系统关闭时自动调用
 *
 * @warning 必须在所有数据库连接关闭后才能调用此函数
 * @warning 调用此函数后不能再使用任何 MySQL 功能
 * @warning 必须与 Library_Init() 配对使用
 *
 * @see Library_Init()
 * @see mysql_library_end() MySQL 官方文档
 */
void MySQL::Library_End()
{
    mysql_library_end();
}

/**
 * @brief 获取 MySQL 客户端库的版本号
 *
 * 返回当前链接的 MySQL 客户端库的版本标识符。
 * 该版本号在编译时确定，由 MySQL 头文件中的 MYSQL_VERSION_ID 宏定义。
 *
 * @details
 * 版本号格式为：主版本号 * 10000 + 次版本号 * 100 + 修订版本号
 *
 * 示例：
 * - MySQL 5.7.30 的版本号为：5*10000 + 7*100 + 30 = 50730
 * - MySQL 8.0.25 的版本号为：8*10000 + 0*100 + 25 = 80025
 *
 * 该函数主要用于：
 * - 版本兼容性检查
 * - 调试日志记录
 * - 运行时特性检测
 *
 * @return uint32 返回 MySQL 版本标识符
 * @retval 格式为 主版本*10000 + 次版本*100 + 修订版本
 *
 * @note 版本号在编译时确定，反映的是编译时链接的 MySQL 版本
 * @note 此函数不访问数据库，仅返回编译时常量
 *
 * @see MYSQL_VERSION_ID MySQL 官方头文件定义
 */
uint32 MySQL::GetLibraryVersion()
{
    return MYSQL_VERSION_ID;
}
