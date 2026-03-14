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
 * @file ServiceWin32.cpp
 * @brief Windows 服务管理模块
 *
 * 本模块实现了 TrinityCore 服务器在 Windows 平台上作为系统服务运行的功能。
 * 主要提供以下核心能力：
 *
 * 1. 服务安装与卸载
 *    - 将服务器程序注册为 Windows 系统服务
 *    - 配置服务的启动类型、故障恢复策略等属性
 *    - 提供服务的干净卸载功能
 *
 * 2. 服务生命周期管理
 *    - 实现服务主入口函数（ServiceMain）
 *    - 处理服务控制请求（启动、停止、暂停、继续等）
 *    - 与 Windows 服务控制管理器（SCM）交互
 *
 * 3. 服务状态报告
 *    - 向 SCM 报告服务的当前状态
 *    - 处理服务控制码并响应系统管理请求
 *
 * Windows 服务机制说明：
 * - Windows 服务是在后台运行的应用程序，无需用户登录即可启动
 * - 服务通过服务控制管理器（SCM）进行管理
 * - 服务具有特定的生命周期：安装 -> 启动 -> 运行 -> 停止 -> 卸载
 * - 服务可以配置为自动启动或手动启动
 * - 服务可以配置故障恢复策略（如自动重启）
 *
 * @note 本文件仅在 Windows 平台（_WIN32）下编译
 * @see https://docs.microsoft.com/en-us/windows/win32/services/services
 */

#ifdef _WIN32

/** @brief 公共头文件，包含基础类型定义和宏 */
#include "Common.h"
/** @brief 日志系统头文件，用于记录错误信息 */
#include "Log.h"
/** @brief C 字符串操作函数 */
#include <cstring>
/** @brief Windows API 核心头文件 */
#include <windows.h>
/** @brief Windows 服务管理 API 头文件 */
#include <winsvc.h>

/**
 * @brief WINADVAPI 宏定义
 *
 * 用于正确声明从 ADVAPI32.DLL 导入的函数。
 * 当未链接 ADVAPI32.LIB 时，使用 DECLSPEC_IMPORT 标记函数从 DLL 导入。
 */
#if !defined(WINADVAPI)
#if !defined(_ADVAPI32_)
#define WINADVAPI DECLSPEC_IMPORT
#else
#define WINADVAPI
#endif
#endif

/**
 * @brief 服务器主函数的外部声明
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码
 */
extern int main(int argc, char ** argv);

/** @brief 服务的长名称（显示名称），在调用方定义 */
extern char serviceLongName[];

/** @brief 服务的短名称（内部标识），在调用方定义 */
extern char serviceName[];

/** @brief 服务的描述信息，在调用方定义 */
extern char serviceDescription[];

/**
 * @brief 服务运行状态标志
 * - 0: 服务停止
 * - 1: 服务运行中
 * - 2: 服务暂停
 */
extern int m_ServiceStatus;

/**
 * @brief 服务状态结构体
 *
 * 包含服务的当前状态信息，用于向 SCM（服务控制管理器）报告服务状态。
 * 包括服务类型、当前状态、接受的控件、错误码等信息。
 */
SERVICE_STATUS serviceStatus;

/**
 * @brief 服务状态句柄
 *
 * 由 RegisterServiceCtrlHandler 返回，用于标识服务实例，
 * 后续调用 SetServiceStatus 时需要传入此句柄。
 */
SERVICE_STATUS_HANDLE serviceStatusHandle = 0;

/**
 * @brief ChangeServiceConfig2A 函数指针类型定义
 *
 * 用于动态获取 ADVAPI32.DLL 中的 ChangeServiceConfig2A 函数地址，
 * 该函数用于修改服务的配置信息（如描述、故障恢复策略等）。
 *
 * @param hService 服务句柄
 * @param dwInfoLevel 信息级别（如 SERVICE_CONFIG_DESCRIPTION）
 * @param lpInfo 指向新配置数据的指针
 * @return 成功返回非零，失败返回零
 */
typedef WINADVAPI BOOL (WINAPI *CSD_T)(SC_HANDLE, DWORD, LPCVOID);

/**
 * @brief 安装 Windows 服务
 *
 * 将 TrinityCore 服务器程序注册为 Windows 系统服务。
 * 该函数执行以下操作：
 * 1. 打开服务控制管理器（SCM）
 * 2. 获取当前可执行文件的完整路径
 * 3. 创建服务，配置服务属性（自动启动、交互式进程等）
 * 4. 设置服务的描述信息
 * 5. 配置服务的故障恢复策略（服务崩溃后 10 秒自动重启）
 *
 * 服务配置详情：
 * - 服务类型：独占进程 + 交互式进程（允许与桌面交互）
 * - 启动类型：自动启动（系统启动时自动运行）
 * - 错误控制：忽略错误（启动失败不显示错误弹窗）
 * - 运行账户：LocalSystem（本地系统账户，最高权限）
 * - 故障恢复：失败后延迟 10 秒自动重启
 *
 * @return 安装成功返回 true，失败返回 false
 *
 * @note 需要管理员权限才能安装服务
 * @note 安装后会自动添加 "--service run" 命令行参数
 *
 * @see WinServiceUninstall() 卸载服务
 */
bool WinServiceInstall()
{
    // 打开服务控制管理器，请求创建服务的权限
    SC_HANDLE serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);

    if (serviceControlManager)
    {
        // 获取当前可执行文件的完整路径
        char path[_MAX_PATH + 10];
        if (GetModuleFileName( 0, path, sizeof(path)/sizeof(path[0]) ) > 0)
        {
            SC_HANDLE service;
            // 添加服务运行参数，标识以服务模式启动
            std::strcat(path, " --service run");

            // 创建服务
            service = CreateService(serviceControlManager,
                serviceName,                                // 服务内部名称
                serviceLongName,                            // 服务显示名称
                SERVICE_ALL_ACCESS,                         // 请求所有访问权限
                                                            // 服务类型：独占进程且支持交互
                SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
                SERVICE_AUTO_START,                         // 启动类型：自动启动
                SERVICE_ERROR_IGNORE,                       // 错误控制：忽略启动错误
                path,                                       // 服务可执行文件路径
                0,                                          // 不属于任何加载顺序组
                0,                                          // 不需要标签标识符
                0,                                          // 不依赖其他服务
                0,                                          // 使用 LocalSystem 账户
                0);                                         // 无密码

            if (service)
            {
                // 获取 ADVAPI32.DLL 模块句柄，用于调用扩展服务配置函数
                HMODULE advapi32 = GetModuleHandle("ADVAPI32.DLL");
                if (!advapi32)
                {
                    CloseServiceHandle(service);
                    CloseServiceHandle(serviceControlManager);
                    return false;
                }

                // 动态获取 ChangeServiceConfig2A 函数地址
                CSD_T ChangeService_Config2 = (CSD_T) GetProcAddress(advapi32, "ChangeServiceConfig2A");
                if (!ChangeService_Config2)
                {
                    CloseServiceHandle(service);
                    CloseServiceHandle(serviceControlManager);
                    return false;
                }

                // 设置服务的描述信息
                SERVICE_DESCRIPTION sdBuf;
                sdBuf.lpDescription = serviceDescription;
                ChangeService_Config2(
                    service,                                // 服务句柄
                    SERVICE_CONFIG_DESCRIPTION,             // 配置类型：修改描述
                    &sdBuf);                                // 新的描述信息

                // 配置服务的故障恢复策略
                SC_ACTION _action[1];
                _action[0].Type = SC_ACTION_RESTART;        // 失败时重启服务
                _action[0].Delay = 10000;                   // 延迟 10 秒后重启

                SERVICE_FAILURE_ACTIONS sfa;
                ZeroMemory(&sfa, sizeof(SERVICE_FAILURE_ACTIONS));
                sfa.lpsaActions = _action;                  // 失败动作数组
                sfa.cActions = 1;                           // 动作数量
                sfa.dwResetPeriod = INFINITE;               // 失败计数器永不清零

                // 应用故障恢复配置
                ChangeService_Config2(
                    service,                                // 服务句柄
                    SERVICE_CONFIG_FAILURE_ACTIONS,         // 配置类型：故障恢复策略
                    &sfa);                                  // 新的故障恢复配置

                // 关闭服务句柄，释放资源
                CloseServiceHandle(service);

            }
        }
        // 关闭服务控制管理器句柄
        CloseServiceHandle(serviceControlManager);
    }

    printf("Service installed\n");
    return true;
}

/**
 * @brief 卸载 Windows 服务
 *
 * 从系统中移除 TrinityCore 服务注册信息。
 * 该函数执行以下操作：
 * 1. 连接到服务控制管理器（SCM）
 * 2. 打开指定的服务
 * 3. 检查服务当前状态，确保服务已停止
 * 4. 删除服务注册信息
 *
 * @return 卸载成功返回 true，失败返回 false
 *
 * @note 需要管理员权限才能卸载服务
 * @note 只能卸载已停止的服务，如果服务正在运行需要先停止
 *
 * @see WinServiceInstall() 安装服务
 */
bool WinServiceUninstall()
{
    // 打开服务控制管理器，请求连接权限
    SC_HANDLE serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);

    if (serviceControlManager)
    {
        // 打开指定的服务，请求查询状态和删除权限
        SC_HANDLE service = OpenService(serviceControlManager,
            serviceName, SERVICE_QUERY_STATUS | DELETE);
        if (service)
        {
            SERVICE_STATUS serviceStatus2;
            // 查询服务当前状态
            if (QueryServiceStatus(service, &serviceStatus2))
            {
                // 只有服务已停止时才允许删除
                if (serviceStatus2.dwCurrentState == SERVICE_STOPPED)
                    DeleteService(service);
            }
            CloseServiceHandle(service);
        }

        CloseServiceHandle(serviceControlManager);
    }

    printf("Service uninstalled\n");
    return true;
}

/**
 * @brief 服务控制处理器
 *
 * 由服务控制管理器（SCM）调用的回调函数，用于处理服务的控制请求。
 * 当系统管理员或其他进程发送控制码给服务时，SCM 会调用此函数。
 *
 * 支持的控制码：
 * - SERVICE_CONTROL_INTERROGATE: 查询服务状态（SCM 定期发送）
 * - SERVICE_CONTROL_STOP: 停止服务请求
 * - SERVICE_CONTROL_SHUTDOWN: 系统关机通知
 * - SERVICE_CONTROL_PAUSE: 暂停服务请求
 * - SERVICE_CONTROL_CONTINUE: 继续服务请求
 * - 128-255: 用户自定义控制码
 *
 * @param controlCode 服务控制码，指示请求的操作类型
 *
 * @note 此函数必须快速返回，不能执行耗时操作
 * @note 停止服务时通过设置 m_ServiceStatus = 0 通知主循环退出
 */
void WINAPI ServiceControlHandler(DWORD controlCode)
{
    switch (controlCode)
    {
        // 查询服务状态，SCM 定期发送此控制码检查服务是否响应
        case SERVICE_CONTROL_INTERROGATE:
            break;

        // 系统关机或停止服务请求
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_STOP:
            // 报告服务正在停止中
            serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(serviceStatusHandle, &serviceStatus);

            // 设置停止标志，通知 ServiceMain 中的主循环退出
            m_ServiceStatus = 0;
            return;

        // 暂停服务请求
        case SERVICE_CONTROL_PAUSE:
            m_ServiceStatus = 2;  // 设置暂停状态标志
            serviceStatus.dwCurrentState = SERVICE_PAUSED;
            SetServiceStatus(serviceStatusHandle, &serviceStatus);
            break;

        // 继续服务请求（从暂停状态恢复）
        case SERVICE_CONTROL_CONTINUE:
            serviceStatus.dwCurrentState = SERVICE_RUNNING;
            SetServiceStatus(serviceStatusHandle, &serviceStatus);
            m_ServiceStatus = 1;  // 设置运行状态标志
            break;

        default:
            // 用户自定义控制码（128-255）
            if ( controlCode >= 128 && controlCode <= 255 )
                // 用户定义的控制码，暂不处理
                break;
            else
                // 无法识别的控制码，忽略
                break;
    }

    // 更新服务状态到 SCM
    SetServiceStatus(serviceStatusHandle, &serviceStatus);
}

/**
 * @brief 服务主入口函数
 *
 * 这是服务的实际入口点，由服务控制管理器（SCM）在服务启动时调用。
 * 该函数负责：
 * 1. 初始化服务状态结构体
 * 2. 注册服务控制处理器
 * 3. 设置工作目录为可执行文件所在目录
 * 4. 报告服务运行状态
 * 5. 调用服务器的 main() 函数进入主循环
 * 6. 服务停止后清理并报告停止状态
 *
 * 服务生命周期：
 * 1. SERVICE_START_PENDING: 服务正在启动
 * 2. SERVICE_RUNNING: 服务正在运行（调用 main() 函数）
 * 3. SERVICE_STOP_PENDING: 服务正在停止（main() 函数返回）
 * 4. SERVICE_STOPPED: 服务已停止
 *
 * @param argc 命令行参数个数（通常由 SCM 传入）
 * @param argv 命令行参数数组
 *
 * @note 此函数由 SCM 线程调用，不应阻塞
 * @note main() 函数在同一个线程中运行，当 m_ServiceStatus 变为 0 时应退出
 *
 * @see ServiceControlHandler() 处理服务控制请求
 */
void WINAPI ServiceMain(DWORD argc, char *argv[])
{
    // 初始化服务状态结构体
    serviceStatus.dwServiceType = SERVICE_WIN32;                    // 服务类型：Win32 服务
    serviceStatus.dwCurrentState = SERVICE_START_PENDING;           // 当前状态：正在启动
    serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;  // 接受的控制
    serviceStatus.dwWin32ExitCode = NO_ERROR;                       // Win32 错误码
    serviceStatus.dwServiceSpecificExitCode = NO_ERROR;             // 服务特定错误码
    serviceStatus.dwCheckPoint = 0;                                 // 检查点（用于长时间启动操作）
    serviceStatus.dwWaitHint = 0;                                   // 等待提示（预计完成时间）

    // 注册服务控制处理器，关联服务名和控制处理函数
    serviceStatusHandle = RegisterServiceCtrlHandler(serviceName, ServiceControlHandler);

    if ( serviceStatusHandle )
    {
        char path[_MAX_PATH + 1];
        unsigned int i, last_slash = 0;

        // 获取当前可执行文件的完整路径
        GetModuleFileName(0, path, sizeof(path)/sizeof(path[0]));

        // 查找最后一个反斜杠，提取目录路径
        size_t pathLen = std::strlen(path);
        for (i = 0; i < pathLen; i++)
        {
            if (path[i] == '\\') last_slash = i;
        }

        // 截断文件名，只保留目录路径
        path[last_slash] = 0;

        // 报告服务正在启动
        serviceStatus.dwCurrentState = SERVICE_START_PENDING;
        SetServiceStatus(serviceStatusHandle, &serviceStatus);

        // 设置当前工作目录为可执行文件所在目录
        // 这样服务器可以找到相对路径的配置文件和资源文件
        SetCurrentDirectory(path);

        // 报告服务正在运行，并接受停止和关机控制
        serviceStatus.dwControlsAccepted |= (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        serviceStatus.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus( serviceStatusHandle, &serviceStatus );

        ////////////////////////
        // 服务主循环 //
        ////////////////////////

        // 设置运行状态标志
        m_ServiceStatus = 1;
        // 覆盖参数，防止服务启动参数影响服务器运行
        argc = 1;
        // 调用服务器的主函数，进入主循环
        // 当 m_ServiceStatus 变为 0 时，main() 应该退出
        main(argc, argv);

        // main() 返回，表示服务正在停止
        serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(serviceStatusHandle, &serviceStatus);

        // 在此处可以添加清理代码

        // 报告服务已停止
        serviceStatus.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(serviceStatusHandle, &serviceStatus);
    }
}

/**
 * @brief 启动服务运行
 *
 * 启动 Windows 服务的主入口函数。
 * 该函数连接到服务控制管理器（SCM），并将当前进程注册为服务进程。
 *
 * 工作流程：
 * 1. 构建服务表，关联服务名和 ServiceMain 函数
 * 2. 调用 StartServiceCtrlDispatcher 连接到 SCM
 * 3. SCM 会在新线程中调用 ServiceMain 函数
 * 4. 此函数会阻塞直到服务停止
 *
 * @return 成功启动返回 true，失败返回 false
 *
 * @note 此函数必须在服务进程的主线程中调用
 * @note 如果进程不是由 SCM 启动的，此函数会失败
 * @note 调用此函数后，进程将成为服务进程，由 SCM 管理
 *
 * @see ServiceMain() 服务主入口函数
 */
bool WinServiceRun()
{
    // 构建服务表，定义服务名和入口函数的映射关系
    SERVICE_TABLE_ENTRY serviceTable[] =
    {
        { serviceName, ServiceMain },   // 服务名 -> 主函数映射
        { 0, 0 }                        // 表结束标记
    };

    // 连接到服务控制分发器，启动服务
    // 此函数会阻塞直到服务停止
    if (!StartServiceCtrlDispatcher(serviceTable))
    {
        // 启动失败，记录错误日志
        TC_LOG_ERROR("server.worldserver", "StartService Failed. Error [{}]", uint32(::GetLastError()));
        return false;
    }
    return true;
}
#endif
