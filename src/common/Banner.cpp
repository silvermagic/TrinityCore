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
 * @file Banner.cpp
 * @brief 服务器启动 Banner 显示实现
 *
 * 本文件实现了 TrinityCore 服务器启动时显示的 ASCII 艺术 Banner。
 * Banner 包含项目名称、版本信息以及项目网站链接，
 * 为服务器启动提供视觉化的品牌展示。
 */

#include "Banner.h"
#include "GitRevision.h"
#include "StringFormat.h"

/**
 * @brief 显示服务器启动 Banner
 *
 * 职责：
 *   在服务器启动时输出 ASCII 艺术形式的 Banner，包含项目名称、
 *   版本信息和项目网站链接，提供视觉化的启动标识。
 *
 * 参数：
 *   @param applicationName - 应用程序名称（如 "worldserver"、"authserver" 等），
 *                            用于标识当前启动的服务类型
 *   @param log - 日志输出函数指针，用于输出 Banner 文本，
 *                接受 C 风格字符串作为参数
 *   @param logExtraInfo - 额外信息输出函数指针，可选参数，
 *                         用于在 Banner 后输出额外的启动信息
 *
 * 返回值：
 *   无返回值（void）
 *
 * 主要流程：
 *   1. 输出版本信息行：包含完整的 Git 版本号和应用程序名称
 *   2. 输出操作提示：显示如何停止服务器的快捷键提示
 *   3. 输出 ASCII 艺术 Banner：TrinityCore 的标志性 ASCII 图案
 *   4. 如果提供了额外信息输出函数，则调用该函数输出额外信息
 */
void Trinity::Banner::Show(char const* applicationName, void(*log)(char const* text), void(*logExtraInfo)())
{
    // 输出版本信息行，格式："<完整版本号> (<应用程序名称>)"
    // 例如："TrinityCore rev. 1234abcd 2024-01-01 (worldserver)"
    log(Trinity::StringFormat("{} ({})", GitRevision::GetFullVersion(), applicationName).c_str());

    // 输出操作提示行，告知用户可以通过 Ctrl-C 停止服务器
    log(R"(<Ctrl-C> to stop.)" "\n");

    // 输出 TrinityCore ASCII 艺术 Banner
    // 以下多行构成 "TrinityCore" 的 ASCII 艺术字图案
    log(R"( ______                       __)");
    log(R"(/\__  _\       __          __/\ \__)");
    log(R"(\/_/\ \/ _ __ /\_\    ___ /\_\ \, _\  __  __)");
    log(R"(   \ \ \/\`'__\/\ \ /' _ `\/\ \ \ \/ /\ \/\ \)");
    log(R"(    \ \ \ \ \/ \ \ \/\ \/\ \ \ \ \ \_\ \ \_\ \)");
    log(R"(     \ \_\ \_\  \ \_\ \_\ \_\ \_\ \__\\/`____ \)");
    log(R"(      \/_/\/_/   \/_/\/_/\/_/\/_/\/__/ `/___/> \)");
    log(R"(                                 C O R E  /\___/)");
    log(R"(http://TrinityCore.org                    \/__/)" "\n");

    // 如果提供了额外信息输出函数，则调用该函数
    // 这允许调用者在 Banner 后添加自定义的启动信息
    if (logExtraInfo)
        logExtraInfo();
}
