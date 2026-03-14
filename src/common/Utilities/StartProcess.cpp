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
 * @file StartProcess.cpp
 * @brief 进程启动工具实现文件
 *
 * 本文件提供了进程启动的核心功能实现，包括：
 * - 同步启动子进程并等待其完成
 * - 异步启动子进程并支持进程控制
 * - 进程输出重定向到日志系统
 * - 在系统 PATH 中搜索可执行文件
 *
 * 实现特点：
 * - 使用 Boost.Process 库实现跨平台进程管理
 * - 支持标准输入、输出、错误流的重定向
 * - 子进程输出自动重定向到 TrinityCore 日志系统
 * - 支持安全模式，隐藏敏感参数信息
 *
 * 主要用途：
 * - 启动编译器和其他构建工具
 * - 执行数据库管理脚本
 * - 运行外部工具和实用程序
 * - 异步执行耗时任务
 *
 * 性能说明：
 * - 同步进程会阻塞当前线程直到完成
 * - 异步进程在独立线程中执行，不阻塞主线程
 * - 进程输出通过管道实时传输，避免缓冲区满导致死锁
 */

#include "StartProcess.h"
#include "Errors.h"
#include "Log.h"
#include "Optional.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/process/args.hpp>
#include <boost/process/child.hpp>
#include <boost/process/env.hpp>
#include <boost/process/exe.hpp>
#include <boost/process/io.hpp>
#include <boost/process/pipe.hpp>
#include <boost/process/search_path.hpp>

using namespace boost::process;
using namespace boost::iostreams;

namespace Trinity
{

/**
 * @class TCLogSink
 * @brief 自定义的日志接收器,用于将子进程的输出重定向到日志系统
 *
 * 该类实现了Boost.Iostreams的Sink概念,可以将子进程的标准输出和标准错误
 * 通过回调函数重定向到TrinityCore的日志系统中。
 *
 * @tparam T 回调函数类型,签名为 void(std::string_view)
 */
template<typename T>
class TCLogSink
{
    T callback_;  ///< 日志输出回调函数

public:
    typedef char      char_type;  ///< 字符类型定义
    typedef sink_tag  category;   ///< Boost.Iostreams分类标签

    /**
     * @brief 构造函数
     * @param callback 日志回调函数,接收字符串视图作为参数
     *
     * 要求回调类型具有 void(std::string_view) 签名
     */
    TCLogSink(T callback)
        : callback_(std::move(callback)) { }

    /**
     * @brief 写入数据到日志接收器
     * @param str 要写入的字符数组指针
     * @param size 字符数组的大小
     * @return 已处理的字符数
     *
     * 该方法会:
     * 1. 查找行结束符(\r或\n)
     * 2. 截取到第一个行结束符的内容
     * 3. 通过回调函数输出日志
     * 4. 返回已处理的字符数(包括行结束符)
     */
    std::streamsize write(char const* str, std::streamsize size)
    {
        std::string_view consoleStr(str, size);
        size_t lineEnd = consoleStr.find_first_of("\r\n");
        std::streamsize processedCharacters = size;
        if (lineEnd != std::string_view::npos)
        {
            consoleStr = consoleStr.substr(0, lineEnd);
            processedCharacters = lineEnd + 1;
        }

        if (!consoleStr.empty())
            callback_(consoleStr);

        return processedCharacters;
    }
};

/**
 * @brief 创建日志接收器的工厂函数
 * @tparam T 回调函数类型
 * @param callback 日志回调函数
 * @return TCLogSink对象实例
 *
 * 该函数用于简化TCLogSink对象的创建过程,自动推导模板参数类型。
 */
template<typename T>
auto MakeTCLogSink(T&& callback)
    -> TCLogSink<typename std::decay<T>::type>
{
    return { std::forward<T>(callback) };
}

/**
 * @brief 创建并启动子进程的内部实现函数
 *
 * @tparam T 等待函数类型,签名为 int(child&)
 * @param waiter 进程等待函数,负责等待子进程完成并返回退出码
 * @param executable 可执行文件的路径
 * @param argsVector 传递给子进程的命令行参数列表
 * @param logger 日志记录器的名称
 * @param input 输入文件的路径(可选,用于重定向标准输入)
 * @param secure 是否启用安全模式(安全模式下不输出进程参数信息)
 * @return 子进程的退出码
 *
 * 主要流程:
 * 1. 创建输出流和错误流管道
 * 2. 记录进程启动信息(非安全模式)
 * 3. 准备输入文件(如果指定)
 * 4. 启动子进程,配置标准输入、输出和错误流的重定向
 * 5. 创建日志接收器,将子进程输出重定向到日志系统
 * 6. 调用等待函数等待进程完成
 * 7. 记录进程完成信息(非安全模式)
 * 8. 返回进程退出码
 */
template<typename T>
static int CreateChildProcess(T waiter, std::string const& executable,
                              std::vector<std::string> const& argsVector,
                              std::string const& logger, std::string const& input,
                              bool secure)
{
#if TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
#pragma warning(push)
#pragma warning(disable:4297)
/*
  Silence warning with boost 1.83

    boost/process/pipe.hpp(132,5): warning C4297: 'boost::process::basic_pipebuf<char,std::char_traits<char>>::~basic_pipebuf': function assumed not to throw an exception but does
    boost/process/pipe.hpp(132,5): message : destructor or deallocator has a (possibly implicit) non-throwing exception specification
    boost/process/pipe.hpp(124,6): message : while compiling class template member function 'boost::process::basic_pipebuf<char,std::char_traits<char>>::~basic_pipebuf(void)'
    boost/process/pipe.hpp(304,42): message : see reference to class template instantiation 'boost::process::basic_pipebuf<char,std::char_traits<char>>' being compiled
*/
#endif
    // 创建子进程的标准输出和标准错误流管道
    ipstream outStream;
    ipstream errStream;
#if TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
#pragma warning(pop)
#endif

    // 非安全模式下记录进程启动信息
    if (!secure)
    {
        TC_LOG_TRACE(logger, "Starting process \"{}\" with arguments: \"{}\".",
                executable, boost::algorithm::join(argsVector, " "));
    }

    // 准备输入文件(仅以读权限打开,因为boost process默认以读写方式打开)
    std::shared_ptr<FILE> inputFile(!input.empty() ? fopen(input.c_str(), "rb") : nullptr, [](FILE* ptr)
    {
        if (ptr != nullptr)
            fclose(ptr);
    });

    // 启动子进程
    child c = [&]()
    {
        if (inputFile)
        {
            // 绑定标准输入文件
            return child{
                exe = boost::filesystem::absolute(executable).string(),
                args = argsVector,
                env = environment(boost::this_process::environment()),
                std_in = inputFile.get(),
                std_out = outStream,
                std_err = errStream
            };
        }
        else
        {
            // 不绑定标准输入(关闭标准输入)
            return child{
                exe = boost::filesystem::absolute(executable).string(),
                args = argsVector,
                env = environment(boost::this_process::environment()),
                std_in = boost::process::close,
                std_out = outStream,
                std_err = errStream
            };
        }
    }();

    // 创建标准输出日志接收器,重定向到INFO级别日志
    auto outInfo = MakeTCLogSink([&](std::string_view msg)
    {
        TC_LOG_INFO(logger, "{}", msg);
    });

    // 创建标准错误日志接收器,重定向到ERROR级别日志
    auto outError = MakeTCLogSink([&](std::string_view msg)
    {
        TC_LOG_ERROR(logger, "{}", msg);
    });

    // 将子进程的输出流和错误流复制到日志接收器
    copy(outStream, outInfo);
    copy(errStream, outError);

    // 在当前作用域中调用等待函数,防止在离开作用域时流过早关闭
    int const result = waiter(c);

    // 非安全模式下记录进程完成信息
    if (!secure)
    {
        TC_LOG_TRACE(logger, ">> Process \"{}\" finished with return value {}.",
                executable, result);
    }

    return result;
}

/**
 * @brief 同步启动子进程并等待其完成
 *
 * @param executable 可执行文件的路径
 * @param args 传递给子进程的命令行参数列表
 * @param logger 日志记录器的名称
 * @param input_file 输入文件的路径(可选,用于重定向标准输入)
 * @param secure 是否启用安全模式(安全模式下不输出进程参数信息)
 * @return 子进程的退出码
 *
 * 该函数会阻塞当前线程,直到子进程执行完成。
 * 主要流程:
 * 1. 创建等待子进程完成的lambda函数
 * 2. 调用CreateChildProcess启动子进程
 * 3. 等待子进程完成并返回退出码
 */
int StartProcess(std::string const& executable, std::vector<std::string> const& args,
                 std::string const& logger, std::string input_file, bool secure)
{
    return CreateChildProcess([](child& c) -> int
    {
        try
        {
            c.wait();
            return c.exit_code();
        }
        catch (...)
        {
            return EXIT_FAILURE;
        }
    }, executable, args, logger, input_file, secure);
}

/**
 * @class AsyncProcessResultImplementation
 * @brief 异步进程结果实现类
 *
 * 该类继承自AsyncProcessResult,用于管理异步启动的子进程。
 * 提供了获取进程执行结果和终止进程的功能。
 */
class AsyncProcessResultImplementation
    : public AsyncProcessResult
{
    std::string const executable;        ///< 可执行文件路径
    std::vector<std::string> const args; ///< 命令行参数列表
    std::string const logger;            ///< 日志记录器名称
    std::string const input_file;        ///< 输入文件路径
    bool const is_secure;                ///< 是否为安全模式

    std::atomic<bool> was_terminated;    ///< 进程是否已被终止的标志

    // 解决boost < 1.57版本缺少移动支持的变通方案
    Optional<std::shared_ptr<std::future<int>>> result;  ///< 异步执行结果
    Optional<std::reference_wrapper<child>> my_child;    ///< 子进程引用

public:
    /**
     * @brief 构造函数
     * @param executable_ 可执行文件路径
     * @param args_ 命令行参数列表
     * @param logger_ 日志记录器名称
     * @param input_file_ 输入文件路径
     * @param secure 是否启用安全模式
     */
    explicit AsyncProcessResultImplementation(std::string executable_, std::vector<std::string> args_,
                                     std::string logger_, std::string input_file_,
                                     bool secure)
        : executable(std::move(executable_)), args(std::move(args_)),
          logger(std::move(logger_)), input_file(input_file_),
          is_secure(secure), was_terminated(false) { }

    // 禁用拷贝和移动操作
    AsyncProcessResultImplementation(AsyncProcessResultImplementation const&) = delete;
    AsyncProcessResultImplementation& operator= (AsyncProcessResultImplementation const&) = delete;
    AsyncProcessResultImplementation(AsyncProcessResultImplementation&&) = delete;
    AsyncProcessResultImplementation& operator= (AsyncProcessResultImplementation&&) = delete;

    /**
     * @brief 启动子进程
     * @return 进程退出码
     *
     * 该函数会:
     * 1. 检查进程是否已启动
     * 2. 调用CreateChildProcess启动子进程
     * 3. 保存子进程引用以便后续终止操作
     * 4. 等待进程完成并返回结果
     */
    int StartProcess()
    {
        ASSERT(!my_child, "Process started already!");

        return CreateChildProcess([&](child& c) -> int
        {
            int result;
            my_child = std::reference_wrapper<child>(c);

            try
            {
                c.wait();
                result = c.exit_code();
            }
            catch (...)
            {
                result = EXIT_FAILURE;
            }

            my_child.reset();
            return was_terminated ? EXIT_FAILURE : result;

        }, executable, args, logger, input_file, is_secure);
    }

    /**
     * @brief 设置异步执行结果的future对象
     * @param result_ future对象,包含进程执行结果
     */
    void SetFuture(std::future<int> result_)
    {
        result = std::make_shared<std::future<int>>(std::move(result_));
    }

    /**
     * @brief 获取包含进程结果的future对象
     * @return 进程执行结果的future引用
     *
     * 当进程执行完成后,可以通过future获取进程退出码
     */
    /// Returns the future which contains the result of the process
    /// as soon it is finished.
    std::future<int>& GetFutureResult() override
    {
        ASSERT(*result, "The process wasn't started!");
        return **result;
    }

    /**
     * @brief 尝试终止进程
     *
     * 如果子进程仍在运行,则终止它并设置终止标志
     */
    /// Tries to terminate the process
    void Terminate() override
    {
        if (my_child)
        {
            was_terminated = true;
            try
            {
                my_child->get().terminate();
            }
            catch(...)
            {
                // 忽略异常
            }
        }
    }
};

/**
 * @brief 异步启动子进程
 *
 * @param executable 可执行文件的路径
 * @param args 传递给子进程的命令行参数列表
 * @param logger 日志记录器的名称
 * @param input_file 输入文件的路径(可选,用于重定向标准输入)
 * @param secure 是否启用安全模式(安全模式下不输出进程参数信息)
 * @return 异步进程结果对象的共享指针
 *
 * 该函数会在新线程中启动子进程,立即返回而不阻塞当前线程。
 * 返回的AsyncProcessResult对象可用于:
 * - 通过GetFutureResult()获取进程执行结果
 * - 通过Terminate()终止进程
 *
 * 主要流程:
 * 1. 创建AsyncProcessResultImplementation对象
 * 2. 在异步任务中启动进程
 * 3. 返回进程句柄
 */
std::shared_ptr<AsyncProcessResult>
    StartAsyncProcess(std::string executable, std::vector<std::string> args,
                      std::string logger, std::string input_file, bool secure)
{
    auto handle = std::make_shared<AsyncProcessResultImplementation>(
        std::move(executable), std::move(args), std::move(logger), std::move(input_file), secure);

    handle->SetFuture(std::async(std::launch::async, [handle] { return handle->StartProcess(); }));
    return handle;
}

/**
 * @brief 在系统PATH中搜索可执行文件
 *
 * @param filename 要搜索的可执行文件名
 * @return 找到的可执行文件完整路径,如果未找到则返回空字符串
 *
 * 该函数使用boost::process::search_path在系统PATH环境变量中搜索指定的可执行文件。
 * 如果搜索失败或发生异常,返回空字符串。
 */
std::string SearchExecutableInPath(std::string const& filename)
{
    try
    {
        return search_path(filename).string();
    }
    catch (...)
    {
        return "";
    }
}

} // namespace Trinity
