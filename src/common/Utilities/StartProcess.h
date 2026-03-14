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
 * @file StartProcess.h
 * @brief 进程启动工具头文件
 *
 * 本模块提供了跨平台的进程启动和管理功能，包括：
 * - 同步启动子进程并等待其完成
 * - 异步启动子进程并获取执行结果
 * - 进程输出重定向到日志系统
 * - 进程终止功能
 * - 可执行文件搜索功能
 *
 * 主要用途：
 * - 启动外部工具和脚本（如编译器、数据库工具等）
 * - 异步执行耗时任务
 * - 进程管理和监控
 *
 * 使用示例：
 * @code
 *   // 同步启动进程
 *   int result = Trinity::StartProcess("/path/to/executable", {"arg1", "arg2"}, "logger");
 *
 *   // 异步启动进程
 *   auto process = Trinity::StartAsyncProcess("/path/to/executable", {"arg1"}, "logger");
 *   int result = process->GetFutureResult().get();
 * @endcode
 */

#ifndef Process_h__
#define Process_h__

#include "Define.h"
#include <future>
#include <memory>
#include <vector>
#include <string>

namespace Trinity
{

/**
 * @brief 同步启动子进程并等待其完成
 *
 * @param executable 可执行文件的路径
 * @param args 传递给子进程的命令行参数列表
 * @param logger 日志记录器的名称
 * @param input_file 输入文件的路径（可选，用于重定向标准输入）
 * @param secure 是否启用安全模式（可选，默认 false）
 * @return int 子进程的退出码
 *
 * 该函数会阻塞当前线程，直到子进程执行完成。
 * 在安全模式下，进程参数不会被记录到日志中。
 *
 * @note 大多数可执行文件期望其名称作为第一个参数
 * @note 子进程的标准输出和标准错误会被重定向到日志系统
 *
 * 性能注意事项：
 * - 该函数会阻塞当前线程直到子进程完成
 * - 对于长时间运行的进程，建议使用 StartAsyncProcess
 */
TC_COMMON_API int StartProcess(std::string const& executable, std::vector<std::string> const& args,
                               std::string const& logger, std::string input_file = "",
                               bool secure = false);

/**
 * @class AsyncProcessResult
 * @brief 异步进程结果抽象类
 *
 * 该类是平台和库无关的异步进程结果表示，
 * 用于管理异步启动的子进程，提供获取进程结果和终止进程的功能。
 *
 * 主要特点：
 * - 通过 future 获取进程执行结果
 * - 支持进程终止操作
 * - 跨平台兼容
 *
 * 使用示例：
 * @code
 *   auto process = StartAsyncProcess(...);
 *   // 执行其他操作
 *   int result = process->GetFutureResult().get();
 *   // 或在需要时终止进程
 *   process->Terminate();
 * @endcode
 */
class AsyncProcessResult
{
public:
    virtual ~AsyncProcessResult() { }

    /**
     * @brief 获取包含进程结果的 future 对象
     * @return 进程执行结果的 future 引用
     *
     * 当进程执行完成后，可以通过 future 获取进程退出码。
     * future 可用于等待进程完成或检查进程状态。
     *
     * @note 返回的 future 在进程完成后会设置退出码
     */
    virtual std::future<int>& GetFutureResult() = 0;

    /**
     * @brief 尝试终止进程
     *
     * 如果子进程仍在运行，则终止它。
     * 该方法不会阻塞，调用后进程会收到终止信号。
     *
     * @note 终止后的进程退出码通常为非正常退出码
     */
    virtual void Terminate() = 0;
};

/**
 * @brief 异步启动子进程
 *
 * @param executable 可执行文件的路径
 * @param args 传递给子进程的命令行参数列表
 * @param logger 日志记录器的名称
 * @param input_file 输入文件的路径（可选，用于重定向标准输入）
 * @param secure 是否启用安全模式（可选，默认 false）
 * @return std::shared_ptr<AsyncProcessResult> 异步进程结果对象的共享指针
 *
 * 该函数在新线程中启动子进程，立即返回而不阻塞当前线程。
 * 返回的 AsyncProcessResult 对象可用于：
 * - 通过 GetFutureResult() 获取进程执行结果
 * - 通过 Terminate() 终止进程
 *
 * 在安全模式下，进程参数不会被记录到日志中。
 *
 * @note 大多数可执行文件期望其名称作为第一个参数
 * @note 子进程的标准输出和标准错误会被重定向到日志系统
 *
 * 性能注意事项：
 * - 该函数立即返回，不会阻塞当前线程
 * - 进程在独立线程中执行，适合并发管理多个进程
 */
TC_COMMON_API std::shared_ptr<AsyncProcessResult>
    StartAsyncProcess(std::string executable, std::vector<std::string> args,
                      std::string logger, std::string input_file = "",
                      bool secure = false);

/**
 * @brief 在系统 PATH 中搜索可执行文件
 *
 * @param filename 要搜索的可执行文件名
 * @return std::string 找到的可执行文件完整路径，如果未找到则返回空字符串
 *
 * 该函数在系统 PATH 环境变量中搜索指定的可执行文件。
 * 如果搜索失败或发生异常，返回空字符串。
 *
 * 使用示例：
 * @code
 *   std::string path = SearchExecutableInPath("git");
 *   if (!path.empty()) {
 *       // 找到了 git 可执行文件
 *       StartProcess(path, {"--version"}, "logger");
 *   }
 * @endcode
 */
TC_COMMON_API std::string SearchExecutableInPath(std::string const& filename);

} // namespace Trinity

#endif // Process_h__
