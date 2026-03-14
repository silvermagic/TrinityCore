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
 * @file BuiltInConfig.cpp
 * @brief 内置配置访问模块实现
 *
 * 本文件实现了内置配置访问功能，提供从配置文件或编译时内置值获取配置的能力。
 * 实现策略：优先使用配置文件中的值，若未配置则回退到编译时的内置值（通过 GitRevision 获取）。
 */

#include "BuiltInConfig.h"
#include "Config.h"
#include "GitRevision.h"

/**
 * @brief 从配置文件或默认函数获取字符串值
 *
 * 这是一个内部辅助模板函数，实现了"配置优先，内置值兜底"的通用逻辑。
 * 先从配置管理器中查找指定键的值，若为空则调用提供的 getter 函数获取默认值。
 *
 * @tparam Fn 获取默认值的函数类型（通常是无参函数返回 std::string）
 * @param key 配置文件中的键名
 * @param getter 获取默认值的函数对象（当配置值为空时调用）
 * @return std::string 配置值或默认值
 *
 * @note 内部函数，仅供本文件内的其他函数调用
 * @note 性能说明：涉及字符串拷贝，但配置读取通常只在初始化阶段进行，性能影响可忽略
 */
template<typename Fn>
static std::string GetStringWithDefaultValueFromFunction(
    std::string const& key, Fn getter)
{
    // 首先尝试从配置文件中读取指定键的值
    std::string const value = sConfigMgr->GetStringDefault(key, "");
    // 如果配置值非空，返回配置值；否则调用 getter 函数获取内置的默认值
    return value.empty() ? getter() : value;
}

std::string BuiltInConfig::GetCMakeCommand()
{
    // 获取 CMake 命令路径，配置键为 "CMakeCommand"
    // 若配置文件未指定，则使用 GitRevision 中记录的编译时路径
    return GetStringWithDefaultValueFromFunction(
        "CMakeCommand", GitRevision::GetCMakeCommand);
}

std::string BuiltInConfig::GetBuildDirectory()
{
    // 获取构建目录路径，配置键为 "BuildDirectory"
    // 若配置文件未指定，则使用 GitRevision 中记录的编译时路径
    return GetStringWithDefaultValueFromFunction(
        "BuildDirectory", GitRevision::GetBuildDirectory);
}

std::string BuiltInConfig::GetSourceDirectory()
{
    // 获取源码目录路径，配置键为 "SourceDirectory"
    // 若配置文件未指定，则使用 GitRevision 中记录的编译时路径
    return GetStringWithDefaultValueFromFunction(
        "SourceDirectory", GitRevision::GetSourceDirectory);
}

std::string BuiltInConfig::GetMySQLExecutable()
{
    // 获取 MySQL 可执行文件路径，配置键为 "MySQLExecutable"
    // 若配置文件未指定，则使用 GitRevision 中记录的编译时路径
    return GetStringWithDefaultValueFromFunction(
        "MySQLExecutable", GitRevision::GetMySQLExecutable);
}
