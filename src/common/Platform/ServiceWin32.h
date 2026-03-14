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
 * @file ServiceWin32.h
 * @brief Windows 服务管理模块
 *
 * 本模块提供 TrinityCore 在 Windows 平台上作为系统服务运行的功能。
 * 包含服务的安装、卸载和运行功能，使服务器能够以 Windows 服务的方式
 * 在后台持续运行，无需用户交互。
 *
 * 主要功能：
 * - 将服务器应用程序注册为 Windows 系统服务
 * - 从系统中移除已注册的服务
 * - 启动并运行服务主循环
 *
 * @note 仅在 Windows 平台编译时有效
 */

#ifdef _WIN32
#ifndef _WIN32_SERVICE_
#define _WIN32_SERVICE_

/**
 * @brief 安装 Windows 系统服务
 *
 * 将当前服务器程序注册为 Windows 系统服务。安装后，服务可以通过
 * Windows 服务管理器进行管理，支持开机自启动等特性。
 *
 * 安装过程会：
 * - 在服务控制管理器中注册服务
 * - 配置服务的启动类型和依赖关系
 * - 设置服务的显示名称和描述信息
 *
 * @return bool
 *   - true: 服务安装成功
 *   - false: 服务安装失败（可能权限不足或服务已存在）
 *
 * @note 需要管理员权限才能安装服务
 */
bool WinServiceInstall();

/**
 * @brief 卸载 Windows 系统服务
 *
 * 从 Windows 系统中移除已注册的服务。卸载后，服务将不再出现在
 * 服务管理器中，也无法再作为服务运行。
 *
 * 卸载过程会：
 * - 从服务控制管理器中删除服务注册信息
 * - 清理相关的服务配置
 *
 * @return bool
 *   - true: 服务卸载成功
 *   - false: 服务卸载失败（可能权限不足、服务不存在或服务正在运行）
 *
 * @note 需要管理员权限才能卸载服务
 * @note 建议在卸载前先停止服务
 */
bool WinServiceUninstall();

/**
 * @brief 运行 Windows 服务主循环
 *
 * 启动服务的主循环，使服务器以服务模式运行。此函数会连接到
 * Windows 服务控制管理器，并开始处理服务控制请求（如启动、停止、暂停等）。
 *
 * 运行过程会：
 * - 注册服务控制处理函数
 * - 初始化服务器核心功能
 * - 进入主事件循环，直到服务被停止
 * - 处理服务控制请求并更新服务状态
 *
 * @return bool
 *   - true: 服务运行正常并正常退出
 *   - false: 服务启动或运行过程中发生错误
 *
 * @note 此函数会阻塞直到服务停止
 * @note 通常由服务控制管理器调用，不应直接从命令行调用
 */
bool WinServiceRun();

#endif                                                      // _WIN32_SERVICE_
#endif                                                      // _WIN32
