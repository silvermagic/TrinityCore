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
 * @file Banner.h
 * @brief 应用程序启动横幅显示模块
 *
 * 本模块负责在服务器启动时显示 TrinityCore 的 ASCII 艺术横幅和相关信息。
 * 主要功能包括：
 * - 显示 TrinityCore 的标志性 ASCII 艺术图案
 * - 显示应用程序名称和版本信息
 * - 显示版权声明和许可证信息
 * - 支持自定义额外信息的显示
 *
 * 该模块被 worldserver 和 authserver 在启动时调用，
 * 为用户提供清晰的视觉反馈和重要的启动信息。
 */

#ifndef TrinityCore_Banner_h__
#define TrinityCore_Banner_h__

#include "Define.h"

namespace Trinity
{
    /**
     * @brief 横幅显示命名空间
     *
     * 包含用于显示应用程序启动横幅的函数和相关工具。
     * 这些函数在服务器启动时被调用，用于展示项目标识和基本信息。
     */
    namespace Banner
    {
        /**
         * @brief 显示应用程序启动横幅
         *
         * 该函数在服务器启动时被调用，用于显示 TrinityCore 的 ASCII 艺术横幅、
         * 应用程序名称、版本信息、版权声明等。支持通过回调函数输出额外信息。
         *
         * 横幅通常包含以下内容：
         * - TrinityCore ASCII 艺术标志
         * - 应用程序名称（如 worldserver 或 authserver）
         * - 版本号和修订版本信息
         * - 版权声明
         * - 许可证信息（GPL v2）
         * - 额外的自定义信息（通过 logExtraInfo 回调）
         *
         * @param applicationName 应用程序名称，通常为 "worldserver" 或 "authserver"
         *                         该名称会显示在横幅中，用于标识当前运行的服务器类型
         *
         * @param log 日志输出回调函数指针，用于输出横幅文本
         *            函数签名：void(char const* text)
         *            该回调会被多次调用，每次输出横幅的一行文本
         *            通常传入日志系统的输出函数，如 Logger::outString 等
         *
         * @param logExtraInfo 额外信息输出回调函数指针，用于在横幅后输出自定义信息
         *                     函数签名：void()
         *                     该回调在主横幅显示完成后被调用
         *                     可用于显示编译信息、配置路径等额外启动信息
         *                     如果不需要额外信息，可以传入 nullptr
         *
         * @note 该函数由 TC_COMMON_API 导出，可在不同模块间调用
         * @note 输出的横幅内容在编译时确定，包含版本和构建信息
         *
         * @example 典型用法示例：
         * @code
         * // 在 worldserver 启动时显示横幅
         * Trinity::Banner::Show(
         *     "worldserver",
         *     [](char const* text) { printf("%s\n", text); },
         *     []() { printf("Configuration: worldserver.conf\n"); }
         * );
         * @endcode
         */
        TC_COMMON_API void Show(char const* applicationName, void(*log)(char const* text), void(*logExtraInfo)());
    }
}

#endif // TrinityCore_Banner_h__
