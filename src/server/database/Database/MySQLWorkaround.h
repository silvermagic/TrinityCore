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
 * @file MySQLWorkaround.h
 * @brief MySQL头文件兼容性修正
 *
 * 本文件用于解决MySQL客户端库头文件的兼容性问题。
 * 主要功能：
 * - 在Windows平台修复mysql.h未正确包含winsock头文件的问题
 * - 统一包含MySQL头文件的入口
 *
 * 问题说明：
 * 某些版本的MySQL头文件（5.7之前）在Windows平台未正确包含winsock2.h，
 * 导致SOCKET类型未定义。此文件通过在包含mysql.h之前先包含winsock2.h来解决。
 *
 * 使用方式：
 * 需要使用MySQL头文件的代码应该包含此文件，而不是直接包含<mysql.h>
 *
 * 注意事项：
 * - 此问题在MySQL 5.7及以后版本已修复
 * - 必须在其他头文件之前包含winsock2.h以避免冲突
 */

#ifdef _WIN32
// Windows平台的修复：mysql.h未正确包含winsock头文件来定义SOCKET类型
// 此问题在MySQL 5.7中已修复
#include <winsock2.h>
#endif

// 包含MySQL客户端库主头文件
#include <mysql.h>
