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
 * @file MySQLThreading.h
 * @brief MySQL库线程管理模块
 *
 * 本模块提供MySQL客户端库的初始化和清理功能。
 * 主要职责：
 * - 初始化MySQL客户端库
 * - 清理MySQL客户端库资源
 * - 获取MySQL库版本信息
 *
 * 使用场景：
 * - 服务器启动时调用Library_Init()初始化MySQL库
 * - 服务器关闭时调用Library_End()清理资源
 * - 运行时查询MySQL库版本用于兼容性检查
 *
 * 线程安全：
 * - Library_Init和Library_End应该只在主线程调用
 * - 这些函数确保MySQL库的多线程支持正确初始化
 */

#ifndef _MYSQLTHREADING_H
#define _MYSQLTHREADING_H

#include "Define.h"

/**
 * @namespace MySQL
 * @brief MySQL库管理命名空间
 *
 * 封装MySQL客户端库的生命周期管理功能
 */
namespace MySQL
{
    /**
     * @brief 初始化MySQL客户端库
     *
     * 在使用任何MySQL功能之前必须调用此函数。
     * 初始化MySQL库的内部状态，启用多线程支持。
     *
     * 调用时机：服务器启动时，在创建任何数据库连接前调用
     * 注意事项：只需调用一次，多次调用是安全的
     */
    TC_DATABASE_API void Library_Init();

    /**
     * @brief 清理MySQL客户端库资源
     *
     * 在不再需要MySQL功能时调用此函数。
     * 释放MySQL库占用的全局资源。
     *
     * 调用时机：服务器关闭时，在关闭所有数据库连接后调用
     * 注意事项：调用后不应再使用任何MySQL功能
     */
    TC_DATABASE_API void Library_End();

    /**
     * @brief 获取MySQL客户端库版本
     * @return MySQL库版本号（格式：主版本*10000 + 次版本*100 + 修订号）
     *
     * 返回编译时链接的MySQL客户端库版本号。
     * 用于版本兼容性检查和日志记录。
     *
     * 示例：MySQL 5.7.20 返回 50720
     */
    TC_DATABASE_API uint32 GetLibraryVersion();
}

#endif
