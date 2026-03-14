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
 * @file GitRevision.cpp
 * @brief Git 版本信息获取模块实现
 *
 * 本文件提供了获取 TrinityCore 服务器版本和构建信息的接口实现。
 * 所有版本信息均在构建时由 CMake 从 Git 仓库提取并生成到 revision_data.h 中，
 * 包括 Git 提交哈希、分支名称、构建时间、编译器信息等。
 *
 * 这些信息主要用于：
 * - 服务器启动时显示版本信息
 * - 日志记录中标识服务器版本
 * - 命令行工具（如 --version 参数）输出版本信息
 * - 调试和技术支持时确定服务器构建版本
 */

#include "GitRevision.h"
#include "revision_data.h"

/**
 * @brief 获取 Git 提交哈希值
 * @return Git 提交的 SHA-1 哈希字符串（完整形式或短格式）
 *
 * 该哈希值在 CMake 配置时从 Git 仓库提取，用于唯一标识当前代码版本。
 * 通过此哈希可以精确追踪服务器构建所使用的源代码提交。
 */
char const* GitRevision::GetHash()
{
    return _HASH;
}

/**
 * @brief 获取 Git 提交日期
 * @return Git 提交的时间字符串
 *
 * 返回当前哈希值对应提交的时间戳，用于标识代码的最后修改时间。
 * 时间格式由 Git 提供，通常为 RFC 2822 格式。
 */
char const* GitRevision::GetDate()
{
    return _DATE;
}

/**
 * @brief 获取 Git 分支名称
 * @return Git 分支名称字符串
 *
 * 返回构建时所在的 Git 分支名称。如果处于分离 HEAD 状态（detached HEAD），
 * 可能返回特定的提交哈希或标签名称。此信息有助于识别代码来源。
 */
char const* GitRevision::GetBranch()
{
    return _BRANCH;
}

/**
 * @brief 获取 CMake 配置命令
 * @return 完整的 CMake 配置命令字符串
 *
 * 返回用于配置项目的完整 CMake 命令，包括所有传递的参数和选项。
 * 这对于重现构建环境和调试构建问题非常有用。
 */
char const* GitRevision::GetCMakeCommand()
{
    return _CMAKE_COMMAND;
}

/**
 * @brief 获取 CMake 版本号
 * @return CMake 工具的版本字符串
 *
 * 返回用于构建项目的 CMake 版本。不同版本的 CMake 可能有不同的行为和特性，
 * 此信息有助于诊断构建系统相关的问题。
 */
char const* GitRevision::GetCMakeVersion()
{
    return _CMAKE_VERSION;
}

/**
 * @brief 获取主机操作系统版本
 * @return 构建主机的操作系统信息字符串
 *
 * 返回执行构建的机器的操作系统版本信息，包括操作系统名称和版本号。
 * 这对于理解服务器是在什么环境中构建的非常有用。
 */
char const* GitRevision::GetHostOSVersion()
{
    return _CMAKE_HOST_SYSTEM;
}

/**
 * @brief 获取构建目录路径
 * @return 构建目录的绝对路径字符串
 *
 * 返回 CMake 生成构建文件的目录路径。这通常是包含 Makefile、
 * IDE 项目文件或编译输出的目录。
 */
char const* GitRevision::GetBuildDirectory()
{
    return _BUILD_DIRECTORY;
}

/**
 * @brief 获取源代码目录路径
 * @return 源代码根目录的绝对路径字符串
 *
 * 返回 TrinityCore 源代码的根目录路径，即包含顶级 CMakeLists.txt 文件的目录。
 * 这对于定位配置文件、脚本和其他资源文件非常有用。
 */
char const* GitRevision::GetSourceDirectory()
{
    return _SOURCE_DIRECTORY;
}

/**
 * @brief 获取 MySQL 可执行文件路径
 * @return MySQL 客户端可执行文件的路径字符串
 *
 * 返回用于数据库操作的 MySQL 客户端程序路径。
 * 该路径在 CMake 配置时确定，用于数据库更新工具和导入导出操作。
 */
char const* GitRevision::GetMySQLExecutable()
{
    return _MYSQL_EXECUTABLE;
}

/**
 * @brief 获取完整数据库文件名
 * @return 完整数据库压缩包的文件名
 *
 * 返回 TrinityCore 完整数据库分发包的文件名，通常用于指示应该下载
 * 哪个数据库文件来初始化服务器数据库。
 */
char const* GitRevision::GetFullDatabase()
{
    return _FULL_DATABASE;
}

/**
 * @brief 平台标识字符串定义
 *
 * 根据编译时检测的目标平台定义相应的平台名称字符串。
 * 支持的平台包括：
 * - Windows 32位和64位
 * - macOS (Apple)
 * - Intel 平台
 * - Unix/Linux 平台
 */
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
#  ifdef _WIN64
#    define TRINITY_PLATFORM_STR "Win64"
#  else
#    define TRINITY_PLATFORM_STR "Win32"
#  endif
#elif TRINITY_PLATFORM == TRINITY_PLATFORM_APPLE
#  define TRINITY_PLATFORM_STR "MacOSX"
#elif TRINITY_PLATFORM == TRINITY_PLATFORM_INTEL
#  define TRINITY_PLATFORM_STR "Intel"
#else // TRINITY_PLATFORM_UNIX
#  define TRINITY_PLATFORM_STR "Unix"
#endif

/**
 * @brief 链接类型标识字符串定义
 *
 * 根据编译配置确定程序使用静态链接还是动态链接。
 * - Static: 静态链接，所有库代码编译到可执行文件中
 * - Dynamic: 动态链接，运行时加载共享库
 */
#ifndef TRINITY_API_USE_DYNAMIC_LINKING
#  define TRINITY_LINKAGE_TYPE_STR "Static"
#else
#  define TRINITY_LINKAGE_TYPE_STR "Dynamic"
#endif

/**
 * @brief 获取完整版本信息字符串
 * @return 格式化的完整版本信息字符串
 *
 * 返回包含以下信息的完整版本字符串：
 * - 项目名称 (TrinityCore)
 * - 产品版本号 (VER_PRODUCTVERSION_STR)
 * - 目标平台 (TRINITY_PLATFORM_STR)
 * - 构建指令 (_BUILD_DIRECTIVE，如 Release、Debug 等)
 * - 链接类型 (Static 或 Dynamic)
 *
 * 典型输出格式："TrinityCore rev. 3.3.5 (Unix, RelWithDebInfo, Static)"
 * 此字符串常用于服务器启动横幅和版本查询命令。
 */
char const* GitRevision::GetFullVersion()
{
  return "TrinityCore rev. " VER_PRODUCTVERSION_STR
    " (" TRINITY_PLATFORM_STR ", " _BUILD_DIRECTIVE ", " TRINITY_LINKAGE_TYPE_STR ")";
}

/**
 * @brief 获取公司名称字符串
 * @return 项目维护者或公司的名称
 *
 * 返回版本资源中定义的公司/组织名称，用于可执行文件的版本信息属性。
 * 通常设置为 "TrinityCore Project" 或类似的标识。
 */
char const* GitRevision::GetCompanyNameStr()
{
    return VER_COMPANYNAME_STR;
}

/**
 * @brief 获取版权声明字符串
 * @return 版权和法律声明文本
 *
 * 返回版本资源中的版权声明，通常包含版权所有者信息、
 * 许可证类型（如 GPL）和其他法律声明。
 */
char const* GitRevision::GetLegalCopyrightStr()
{
    return VER_LEGALCOPYRIGHT_STR;
}

/**
 * @brief 获取文件版本字符串
 * @return 可执行文件的版本号字符串
 *
 * 返回可执行文件的版本号，格式通常为 "major.minor.patch.build"。
 * 此版本号用于 Windows 可执行文件的版本资源，也可用于版本比较。
 */
char const* GitRevision::GetFileVersionStr()
{
    return VER_FILEVERSION_STR;
}

/**
 * @brief 获取产品版本字符串
 * @return 产品版本号字符串
 *
 * 返回产品的版本号，表示 TrinityCore 的发行版本。
 * 此版本号通常与文件版本相同或略有不同，具体取决于发布策略。
 * 在服务器版本显示和版本查询中广泛使用。
 */
char const* GitRevision::GetProductVersionStr()
{
    return VER_PRODUCTVERSION_STR;
}
