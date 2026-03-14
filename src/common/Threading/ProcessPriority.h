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
 * @file ProcessPriority.h
 * @brief 进程优先级管理模块头文件
 *
 * 本模块提供跨平台的进程优先级和处理器亲和性设置功能。
 * 支持Windows和Linux平台，允许服务器进程绑定到特定的CPU核心，
 * 并设置进程优先级以获得更好的性能表现。
 *
 * 主要功能：
 * - 设置进程可使用的CPU核心（处理器亲和性）
 * - 提升进程优先级以获得更多的CPU时间片
 * - 跨平台支持（Windows/Linux）
 *
 * 配置项：
 * - UseProcessors: 指定进程可使用的CPU核心掩码
 * - ProcessPriority: 是否启用高优先级模式
 */

#ifndef _PROCESSPRIO_H
#define _PROCESSPRIO_H

#include "Define.h"
#include <string>

/**
 * @brief 配置项名称：处理器亲和性掩码
 *
 * 该配置项用于指定进程可以使用的CPU核心，值为位掩码格式。
 * 例如：值为 3 (二进制 0011) 表示使用 CPU 0 和 CPU 1
 */
#define CONFIG_PROCESSOR_AFFINITY "UseProcessors"

/**
 * @brief 配置项名称：进程优先级设置
 *
 * 该配置项用于控制是否将进程设置为高优先级运行。
 * 启用后可以提高服务器进程的响应速度和性能。
 */
#define CONFIG_HIGH_PRIORITY "ProcessPriority"

/**
 * @brief 设置进程优先级和处理器亲和性
 *
 * 该函数实现了跨平台的进程优先级和CPU亲和性设置功能。
 * 根据不同的操作系统平台（Windows/Linux），使用相应的系统API
 * 来调整进程的运行参数，以优化服务器性能。
 *
 * @param logChannel 日志通道名称，用于输出设置过程的日志信息
 * @param affinity 处理器亲和性掩码，每一位代表一个CPU核心的可用性
 *                 0 表示使用系统默认设置，不限制CPU核心
 *                 非0值指定了进程可以使用的CPU核心集合
 * @param highPriority 是否设置高优先级
 *                     true: 提升进程优先级以获得更多CPU时间
 *                     false: 使用系统默认优先级
 *
 * @note Windows平台：
 *       - 使用 SetProcessAffinityMask() 设置CPU亲和性
 *       - 使用 SetPriorityClass() 设置进程优先级为 HIGH_PRIORITY_CLASS
 *
 * @note Linux平台：
 *       - 使用 sched_setaffinity() 设置CPU亲和性
 *       - 使用 setpriority() 设置进程nice值为 -15（高优先级）
 *       - nice值范围：-20（最高优先级）到 19（最低优先级）
 *
 * @note 其他平台：
 *       该函数在不支持的平台上不执行任何操作
 *
 * @warning 设置高优先级可能导致系统响应变慢，请谨慎使用
 *
 * 示例用法：
 * @code
 * // 在所有CPU核心上运行，并设置为高优先级
 * SetProcessPriority("server.world", 0, true);
 *
 * // 仅在CPU 0和CPU 1上运行，不改变优先级
 * SetProcessPriority("server.world", 0x3, false);
 * @endcode
 */
void TC_COMMON_API SetProcessPriority(std::string const& logChannel, uint32 affinity, bool highPriority);

#endif
