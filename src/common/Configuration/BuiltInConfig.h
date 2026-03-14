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
 * @file BuiltInConfig.h
 * @brief 内置配置访问模块
 *
 * 本模块提供访问构建时内置配置值的辅助函数接口。
 * 这些内置值在编译时确定（通过 GitRevision），但可以在运行时配置文件中覆盖。
 * 主要用于获取构建环境相关的路径和工具配置，例如：
 * - CMake 命令路径
 * - 构建目录路径
 * - 源码目录路径
 * - MySQL 可执行文件路径
 *
 * @note 配置优先级：配置文件 > 内置值
 */

#ifndef BUILT_IN_CONFIG_H
#define BUILT_IN_CONFIG_H

#include "Define.h"
#include <string>

/**
 * @namespace BuiltInConfig
 * @brief 内置配置访问命名空间
 *
 * 提供辅助函数来访问内置值，这些值可以在配置文件中被覆盖。
 * 所有函数都遵循相同的优先级策略：优先使用配置文件中的值，若未配置则使用编译时内置值。
 */
namespace BuiltInConfig
{
    /**
     * @brief 获取 CMake 命令路径
     *
     * 返回 CMake 命令的完整路径。如果配置文件中指定了 "CMakeCommand" 选项，
     * 则返回配置的值；否则返回编译时内置的路径（从 GitRevision 获取）。
     *
     * @return std::string CMake 命令的路径
     *
     * @note 调用时机：
     *       - 服务器启动时需要确定构建工具路径
     *       - 执行数据库更新或构建相关脚本时
     *
     * @note 性能说明：该函数涉及字符串拷贝，但不频繁调用，性能影响可忽略
     */
    TC_COMMON_API std::string GetCMakeCommand();

    /**
     * @brief 获取构建目录路径
     *
     * 返回构建目录的路径。如果配置文件中指定了 "BuildDirectory" 选项，
     * 则返回配置的值；否则返回编译时内置的路径（从 GitRevision 获取）。
     *
     * @return std::string 构建目录的路径
     *
     * @note 调用时机：
     *       - 需要定位构建产物时
     *       - 执行数据库更新脚本时
     *       - 定位编译生成的文件时
     *
     * @note 性能说明：该函数涉及字符串拷贝，但不频繁调用，性能影响可忽略
     */
    TC_COMMON_API std::string GetBuildDirectory();

    /**
     * @brief 获取源码目录路径
     *
     * 返回源码目录的路径。如果配置文件中指定了 "SourceDirectory" 选项，
     * 则返回配置的值；否则返回编译时内置的路径（从 GitRevision 获取）。
     *
     * @return std::string 源码目录的路径
     *
     * @note 调用时机：
     *       - 需要访问源码文件时
     *       - 定位配置文件模板时
     *       - 执行更新脚本时
     *
     * @note 性能说明：该函数涉及字符串拷贝，但不频繁调用，性能影响可忽略
     */
    TC_COMMON_API std::string GetSourceDirectory();

    /**
     * @brief 获取 MySQL 可执行文件路径
     *
     * 返回 MySQL 客户端可执行文件（mysql）的路径。
     * 如果配置文件中指定了 "MySQLExecutable" 选项，则返回配置的值；
     * 否则返回编译时内置的路径（从 GitRevision 获取）。
     *
     * @return std::string MySQL 可执行文件的路径
     *
     * @note 调用时机：
     *       - 执行数据库更新脚本时
     *       - 需要直接连接数据库进行维护操作时
     *
     * @note 性能说明：该函数涉及字符串拷贝，但不频繁调用，性能影响可忽略
     */
    TC_COMMON_API std::string GetMySQLExecutable();

} // namespace BuiltInConfig

#endif // BUILT_IN_CONFIG_H
