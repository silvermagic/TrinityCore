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
 * @file GitRevision.h
 * @brief Git 版本信息查询模块
 *
 * 本文件提供了获取 TrinityCore 项目 Git 版本信息的接口。
 * 这些信息包括 Git 提交哈希、分支、编译配置、构建环境等元数据，
 * 用于版本追踪、调试和服务器信息展示。
 *
 * 所有版本信息在编译时通过 CMake 脚本从 Git 仓库和构建环境中提取，
 * 并以只读方式存储在生成的源文件中。
 */

#ifndef __GITREVISION_H__
#define __GITREVISION_H__

#include "Define.h"

/**
 * @namespace GitRevision
 * @brief Git 版本信息命名空间
 *
 * 提供统一的版本信息查询接口，用于获取编译时嵌入的 Git 仓库状态、
 * 构建环境配置和版本元数据。
 */
namespace GitRevision
{
    /**
     * @brief 获取 Git 提交哈希值
     * @return 返回当前 HEAD 提交的完整 SHA-1 哈希字符串
     */
    TC_COMMON_API char const* GetHash();

    /**
     * @brief 获取 Git 提交日期
     * @return 返回最后一次提交的日期时间字符串
     */
    TC_COMMON_API char const* GetDate();

    /**
     * @brief 获取当前 Git 分支名称
     * @return 返回当前检出的分支名称字符串
     */
    TC_COMMON_API char const* GetBranch();

    /**
     * @brief 获取 CMake 配置命令
     * @return 返回用于配置项目的完整 CMake 命令行字符串
     */
    TC_COMMON_API char const* GetCMakeCommand();

    /**
     * @brief 获取 CMake 版本号
     * @return 返回用于构建的 CMake 版本字符串
     */
    TC_COMMON_API char const* GetCMakeVersion();

    /**
     * @brief 获取主机操作系统版本信息
     * @return 返回编译主机操作系统的详细信息字符串
     */
    TC_COMMON_API char const* GetHostOSVersion();

    /**
     * @brief 获取构建目录路径
     * @return 返回项目构建输出目录的绝对路径字符串
     */
    TC_COMMON_API char const* GetBuildDirectory();

    /**
     * @brief 获取源代码目录路径
     * @return 返回源代码根目录的绝对路径字符串
     */
    TC_COMMON_API char const* GetSourceDirectory();

    /**
     * @brief 获取 MySQL 可执行文件路径
     * @return 返回 MySQL 客户端可执行文件的路径字符串
     */
    TC_COMMON_API char const* GetMySQLExecutable();

    /**
     * @brief 获取完整数据库版本标识
     * @return 返回数据库的完整版本标识字符串
     */
    TC_COMMON_API char const* GetFullDatabase();

    /**
     * @brief 获取完整版本字符串
     * @return 返回包含项目名称、版本号和 Git 信息的完整版本字符串
     */
    TC_COMMON_API char const* GetFullVersion();

    /**
     * @brief 获取公司/组织名称
     * @return 返回项目的公司或组织名称字符串
     */
    TC_COMMON_API char const* GetCompanyNameStr();

    /**
     * @brief 获取版权声明字符串
     * @return 返回项目的版权法律声明字符串
     */
    TC_COMMON_API char const* GetLegalCopyrightStr();

    /**
     * @brief 获取文件版本字符串
     * @return 返回用于 Windows 资源文件的版本字符串
     */
    TC_COMMON_API char const* GetFileVersionStr();

    /**
     * @brief 获取产品版本字符串
     * @return 返回用于 Windows 资源文件的产品版本字符串
     */
    TC_COMMON_API char const* GetProductVersionStr();
}

#endif
