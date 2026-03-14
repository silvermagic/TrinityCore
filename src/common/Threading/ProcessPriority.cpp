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
 * @file ProcessPriority.cpp
 * @brief 进程优先级管理模块实现文件
 *
 * 本文件实现了跨平台的进程优先级和处理器亲和性设置功能。
 * 通过平台特定的系统调用，允许服务器进程获得更好的性能表现。
 *
 * 平台支持：
 * - Windows: 使用Windows API设置进程优先级和CPU亲和性
 * - Linux: 使用POSIX调度接口设置进程优先级和CPU亲和性
 * - 其他平台: 不执行任何操作
 */

#include "ProcessPriority.h"
#include "Log.h"

#ifdef _WIN32 // Windows平台
#include <Windows.h>
#elif defined(__linux__) // Linux平台
#include <sched.h>        // CPU亲和性相关函数
#include <sys/resource.h> // 进程优先级相关函数

/**
 * @brief Linux平台高优先级nice值
 *
 * Linux系统中，进程的nice值范围是 -20 到 19：
 * - -20 表示最高优先级
 * - 0 表示默认优先级
 * - 19 表示最低优先级
 *
 * 这里设置为 -15，表示较高的优先级，但不是最高，
 * 以避免过度占用系统资源。
 */
#define PROCESS_HIGH_PRIORITY -15
#endif

/**
 * @brief 设置进程优先级和处理器亲和性
 *
 * 该函数根据配置参数设置进程的CPU亲和性和优先级，以优化服务器性能。
 * 实现了Windows和Linux平台的跨平台支持。
 *
 * @param logChannel 日志通道名称
 * @param affinity 处理器亲和性掩码
 * @param highPriority 是否设置为高优先级
 */
void SetProcessPriority(std::string const& logChannel, uint32 affinity, bool highPriority)
{
    ///- 处理多核处理器的亲和性设置和进程优先级

#ifdef _WIN32 // ==================== Windows平台实现 ====================

    // 获取当前进程句柄
    HANDLE hProcess = GetCurrentProcess();

    // --------------------------
    // 处理CPU亲和性设置
    // --------------------------
    if (affinity > 0)
    {
        ULONG_PTR appAff; // 应用程序可用的处理器掩码
        ULONG_PTR sysAff; // 系统中所有处理器的掩码

        // 获取系统允许的处理器掩码
        if (GetProcessAffinityMask(hProcess, &appAff, &sysAff))
        {
            // 计算实际可用的处理器掩码
            // 通过与操作过滤掉不可访问的处理器
            ULONG_PTR currentAffinity = affinity & appAff;

            // 检查是否有可用的处理器
            if (!currentAffinity)
                // 配置的处理器掩码中没有可访问的处理器
                TC_LOG_ERROR(logChannel, "Processors marked in UseProcessors bitmask (hex) {:x} are not accessible. Accessible processors bitmask (hex): {:x}", affinity, appAff);
            else if (SetProcessAffinityMask(hProcess, currentAffinity))
                // 成功设置处理器亲和性
                TC_LOG_INFO(logChannel, "Using processors (bitmask, hex): {:x}", currentAffinity);
            else
                // 设置处理器亲和性失败
                TC_LOG_ERROR(logChannel, "Can't set used processors (hex): {:x}", currentAffinity);
        }
    }

    // --------------------------
    // 处理进程优先级设置
    // --------------------------
    if (highPriority)
    {
        // 将进程设置为高优先级类别
        // HIGH_PRIORITY_CLASS: 进程将获得比普通进程更高的CPU时间片优先级
        if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
            TC_LOG_INFO(logChannel, "Process priority class set to HIGH");
        else
            TC_LOG_ERROR(logChannel, "Can't set process priority class.");
    }

#elif defined(__linux__) // ==================== Linux平台实现 ====================

    // --------------------------
    // 处理CPU亲和性设置
    // --------------------------
    if (affinity > 0)
    {
        cpu_set_t mask; // CPU集合结构体
        CPU_ZERO(&mask); // 清空CPU集合

        // 根据亲和性掩码设置可用的CPU核心
        // 遍历掩码的每一位，如果该位被设置，则将对应的CPU核心加入集合
        for (unsigned int i = 0; i < sizeof(affinity) * 8; ++i)
            if (affinity & (1 << i))
                CPU_SET(i, &mask); // 将CPU i加入集合

        // 设置进程的CPU亲和性
        // 参数0表示当前进程
        if (sched_setaffinity(0, sizeof(mask), &mask))
            // 设置失败，记录错误信息
            TC_LOG_ERROR(logChannel, "Can't set used processors (hex): {:x}, error: {}", affinity, strerror(errno));
        else
        {
            // 设置成功，重新读取并验证实际生效的CPU亲和性
            CPU_ZERO(&mask);
            sched_getaffinity(0, sizeof(mask), &mask);
            TC_LOG_INFO(logChannel, "Using processors (bitmask, hex): {:x}", *(__cpu_mask*)(&mask));
        }
    }

    // --------------------------
    // 处理进程优先级设置
    // --------------------------
    if (highPriority)
    {
        // 设置进程的nice值
        // PRIO_PROCESS: 表示设置进程优先级
        // 0: 表示当前进程
        // PROCESS_HIGH_PRIORITY: 目标nice值（-15）
        if (setpriority(PRIO_PROCESS, 0, PROCESS_HIGH_PRIORITY))
            // 设置失败，记录错误信息
            TC_LOG_ERROR(logChannel, "Can't set process priority class, error: {}", strerror(errno));
        else
        {
            // 设置成功，记录实际的nice值
            // getpriority返回当前进程的nice值
            TC_LOG_INFO(logChannel, "Process priority class set to {}", getpriority(PRIO_PROCESS, 0));
        }
    }

#else // ==================== 其他平台（不支持） ====================
    // 抑制未使用参数的编译器警告
    (void)logChannel;
    (void)affinity;
    (void)highPriority;
#endif
}
