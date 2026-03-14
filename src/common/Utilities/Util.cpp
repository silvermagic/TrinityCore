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
 * @file Util.cpp
 * @brief 通用工具函数实现模块
 *
 * 本模块提供 TrinityCore 核心系统所需的通用工具函数，包括：
 * - 字符串处理：UTF-8 编码转换、大小写转换、字符串分割、格式化输出
 * - 时间处理：时间戳转换、时间格式化、时间字符串解析
 * - 编码转换：UTF-8 与宽字符互转、控制台编码转换
 * - 系统工具：进程 ID 管理、操作系统版本检查、IP 地址验证
 * - 数据转换：十六进制与字节数组互转、货币字符串解析
 * - 字符处理：俄语名字变格处理、拉丁字母大小写转换
 *
 * 使用场景：
 * - 日志系统中的 UTF-8 输出和格式化
 * - 网络通信中的数据编码转换
 * - 配置文件解析和命令行参数处理
 * - 游戏客户端的本地化字符串处理
 * - 数据库查询结果的时间格式化
 */

#include "Util.h"
#include "Common.h"
#include "Containers.h"
#include "IpAddress.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include <utf8.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdarg>
#include <ctime>
#include <boost/core/demangle.hpp>

/**
 * @brief 验证操作系统版本是否符合要求
 *
 * 职责：
 *   检查当前操作系统版本是否满足 TrinityCore 运行的最低要求
 *   仅在 Windows 平台上执行版本检查
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 在 Windows 平台上检查系统构建号是否满足最低要求
 *   2. 如果版本过低，则终止程序并输出错误信息
 *   3. 非 Windows 平台不做任何操作
 */
void Trinity::VerifyOsVersion()
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Lambda 函数：检查 Windows 构建号是否大于等于指定版本
    auto isWindowsBuildGreaterOrEqual = [](DWORD build)
    {
        // 初始化 OSVERSIONINFOEX 结构体，设置目标构建号
        OSVERSIONINFOEX osvi = { sizeof(osvi), 0, 0, build, 0, {0}, 0, 0, 0, 0 };
        ULONGLONG conditionMask = 0;
        // 设置条件掩码：构建号大于等于指定值
        VER_SET_CONDITION(conditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

        // 调用 Windows API 验证版本信息
        return VerifyVersionInfo(&osvi, VER_BUILDNUMBER, conditionMask);
    };

    // 如果当前系统版本低于要求的最低版本，终止程序
    if (!isWindowsBuildGreaterOrEqual(TRINITY_REQUIRED_WINDOWS_BUILD))
    {
        // 获取当前系统的实际版本信息
        OSVERSIONINFOEX osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0, 0, 0 };
        GetVersionEx((LPOSVERSIONINFO)&osvi);
        // 输出错误信息并终止程序
        ABORT_MSG("TrinityCore requires Windows 10 19H1 (1903) or Windows Server 2019 (1903) - require build number 10.0.%d but found %d.%d.%d",
            TRINITY_REQUIRED_WINDOWS_BUILD, osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
    }
#endif
}

/**
 * @brief 将字符串按指定分隔符分割成多个标记
 *
 * 职责：
 *   将输入字符串按照指定的分隔符拆分成多个子字符串（标记）
 *   返回所有标记的视图数组，避免不必要的内存拷贝
 *
 * 参数：
 *   str: 要分割的字符串视图
 *   sep: 分隔符字符
 *   keepEmpty: 是否保留空标记
 *
 * 返回值：
 *   包含所有标记的字符串视图向量
 *
 * 主要流程：
 *   1. 遍历字符串，查找所有分隔符位置
 *   2. 提取两个分隔符之间的子字符串作为标记
 *   3. 根据 keepEmpty 参数决定是否跳过空标记
 *   4. 处理最后一个标记（字符串末尾到上一个分隔符之间的内容）
 */
std::vector<std::string_view> Trinity::Tokenize(std::string_view str, char sep, bool keepEmpty)
{
    std::vector<std::string_view> tokens;

    size_t start = 0;
    // 循环查找所有分隔符位置
    for (size_t end = str.find(sep); end != std::string_view::npos; end = str.find(sep, start))
    {
        // 根据 keepEmpty 参数决定是否保留空标记
        // 如果 keepEmpty 为 true，或者标记非空（start < end），则添加标记
        if (keepEmpty || (start < end))
            tokens.push_back(str.substr(start, end - start));
        // 移动到下一个字符继续查找
        start = end+1;
    }

    // 处理最后一个标记（字符串末尾部分）
    if (keepEmpty || (start < str.length()))
        tokens.push_back(str.substr(start));

    return tokens;
}

/**
 * @brief Windows 平台的线程安全本地时间转换函数
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 localtime_r 函数实现
 *   将时间戳转换为本地时间的 tm 结构，线程安全版本
 *
 * 参数：
 *   time: 指向时间戳的指针
 *   result: 指向 tm 结构的指针，用于存储转换结果
 *
 * 返回值：
 *   成功返回 result 指针，失败返回 nullptr
 *
 * 主要流程：
 *   调用 Windows 的 localtime_s 函数进行转换
 */
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__))
struct tm* localtime_r(time_t const* time, struct tm *result)
{
    if (localtime_s(result, time) != 0)
        return nullptr;
    return result;
}

/**
 * @brief Windows 平台的线程安全 UTC 时间转换函数
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 gmtime_r 函数实现
 *   将时间戳转换为 UTC 时间的 tm 结构，线程安全版本
 *
 * 参数：
 *   time: 指向时间戳的指针
 *   result: 指向 tm 结构的指针，用于存储转换结果
 *
 * 返回值：
 *   成功返回 result 指针，失败返回 nullptr
 *
 * 主要流程：
 *   调用 Windows 的 gmtime_s 函数进行转换
 */
struct tm* gmtime_r(time_t const* time, struct tm* result)
{
    if (gmtime_s(result, time) != 0)
        return nullptr;
    return result;
}

/**
 * @brief Windows 平台的时间转换为时间戳函数（UTC）
 *
 * 职责：
 *   为 Windows 平台提供 POSIX 标准的 timegm 函数实现
 *   将 tm 结构（UTC 时间）转换为时间戳
 *
 * 参数：
 *   tm: 指向 tm 结构的指针，包含 UTC 时间信息
 *
 * 返回值：
 *   对应的时间戳
 *
 * 主要流程：
 *   调用 Windows 的 _mkgmtime 函数进行转换
 */
time_t timegm(struct tm* tm)
{
    return _mkgmtime(tm);
}
#endif

/**
 * @brief 将时间戳分解为本地时间结构
 *
 * 职责：
 *   将 Unix 时间戳转换为本地时间的 tm 结构
 *   包含年、月、日、时、分、秒等信息
 *
 * 参数：
 *   time: Unix 时间戳
 *
 * 返回值：
 *   tm 结构，包含分解后的时间信息
 *
 * 主要流程：
 *   调用 localtime_r 函数进行时间分解
 */
tm TimeBreakdown(time_t time)
{
    tm timeLocal;
    // 使用线程安全版本的 localtime 函数将时间戳转换为本地时间结构
    localtime_r(&time, &timeLocal);
    return timeLocal;
}

/**
 * @brief 获取指定日期中特定小时的时间戳
 *
 * 职责：
 *   计算给定时间所在日期中指定小时（本地时间）的时间戳
 *   可选择只返回在给定时间之后的时间戳
 *
 * 参数：
 *   time: 参考时间戳
 *   hour: 目标小时（0-23）
 *   onlyAfterTime: 是否只返回在 time 之后的时间戳
 *
 * 返回值：
 *   计算出的时间戳
 *
 * 主要流程：
 *   1. 将时间戳分解为本地时间
 *   2. 将时分秒归零，得到当天午夜的起始时间戳
 *   3. 加上指定小时数得到目标时间戳
 *   4. 如果 onlyAfterTime 为 true 且计算结果小于等于 time，则加一天
 */
time_t GetLocalHourTimestamp(time_t time, uint8 hour, bool onlyAfterTime)
{
    // 将时间戳分解为本地时间结构
    tm timeLocal = TimeBreakdown(time);
    // 将时、分、秒归零，得到当天午夜的起始时间
    timeLocal.tm_hour = 0;
    timeLocal.tm_min = 0;
    timeLocal.tm_sec = 0;
    // 转换回时间戳，得到当天午夜的时间戳
    time_t midnightLocal = mktime(&timeLocal);
    // 加上指定的小时数，得到目标小时的时间戳
    time_t hourLocal = midnightLocal + hour * HOUR;

    // 如果要求只返回在 time 之后的时间，且计算结果小于等于 time，则加一天
    if (onlyAfterTime && hourLocal <= time)
        hourLocal += DAY;

    return hourLocal;
}

/**
 * @brief 将秒数转换为时间字符串
 *
 * 职责：
 *   将给定的秒数格式化为可读的时间字符串
 *   支持多种格式：纯数字、简短文本、完整文本
 *
 * 参数：
 *   timeInSecs: 时间长度（秒）
 *   timeFormat: 输出格式（Numeric/ShortText/FullText）
 *   hoursOnly: 是否只显示小时，忽略分钟和秒
 *
 * 返回值：
 *   格式化后的时间字符串
 *
 * 主要流程：
 *   1. 计算天数、小时、分钟、秒
 *   2. 根据格式类型生成对应的字符串
 *   3. Numeric 格式：d:h:m:s 形式
 *   4. ShortText 格式：1d 2h 3m 4s 形式
 *   5. FullText 格式：1 Day 2 Hours 3 Minutes 4 Seconds 形式
 */
std::string secsToTimeString(uint64 timeInSecs, TimeFormat timeFormat, bool hoursOnly)
{
    // 计算秒数：总秒数对 60 取余
    uint64 secs    = timeInSecs % MINUTE;
    // 计算分钟数：总秒数对 3600 取余后除以 60
    uint64 minutes = timeInSecs % HOUR / MINUTE;
    // 计算小时数：总秒数对 86400 取余后除以 3600
    uint64 hours   = timeInSecs % DAY  / HOUR;
    // 计算天数：总秒数除以 86400
    uint64 days    = timeInSecs / DAY;

    // 数字格式：d:h:m:s 或 h:m:s 或 m:s 或 0:s
    if (timeFormat == TimeFormat::Numeric)
    {
        if (days)
            return Trinity::StringFormat("{}:{:02}:{:02}:{:02}", days, hours, minutes, secs);
        else if (hours)
            return Trinity::StringFormat("{}:{:02}:{:02}", hours, minutes, secs);
        else if (minutes)
            return Trinity::StringFormat("{}:{:02}", minutes, secs);
        else
            return Trinity::StringFormat("0:{:02}", secs);
    }

    // 文本格式：使用 ostringstream 构建输出字符串
    std::ostringstream ss;
    // 添加天数部分
    if (days)
    {
        ss << days;
        switch (timeFormat)
        {
            case TimeFormat::ShortText:
                ss << "d";
                break;
            case TimeFormat::FullText:
                // 根据单复数选择不同的文本
                if (days == 1)
                    ss << " Day ";
                else
                    ss << " Days ";
                break;
            default:
                return "<Unknown time format>";
        }
    }

    // 添加小时部分
    if (hours || hoursOnly)
    {
        ss << hours;
        switch (timeFormat)
        {
            case TimeFormat::ShortText:
                ss << "h";
                break;
            case TimeFormat::FullText:
                if (hours <= 1)
                    ss << " Hour ";
                else
                    ss << " Hours ";
                break;
            default:
                return "<Unknown time format>";
        }
    }
    // 如果不强制只显示小时，则添加分钟和秒部分
    if (!hoursOnly)
    {
        // 添加分钟部分
        if (minutes)
        {
            ss << minutes;
            switch (timeFormat)
            {
                case TimeFormat::ShortText:
                    ss << "m";
                    break;
                case TimeFormat::FullText:
                    if (minutes == 1)
                        ss << " Minute ";
                    else
                        ss << " Minutes ";
                    break;
                default:
                    return "<Unknown time format>";
            }
        }

        // 添加秒部分（如果秒数非零，或者所有部分都为零）
        if (secs || (!days && !hours && !minutes))
        {
            ss << secs;
            switch (timeFormat)
            {
                case TimeFormat::ShortText:
                    ss << "s";
                    break;
                case TimeFormat::FullText:
                    if (secs <= 1)
                        ss << " Second.";
                    else
                        ss << " Seconds.";
                    break;
                default:
                    return "<Unknown time format>";
            }
        }
    }

    return ss.str();
}

/**
 * @brief 将货币字符串转换为货币数值
 *
 * 职责：
 *   解析游戏货币字符串格式（如 "10g 50s 25c"），转换为铜币总数
 *   格式：数字 + 单位（g=金币、s=银币、c=铜币）
 *
 * 参数：
 *   moneyString: 货币字符串，如 "10g 50s 25c"
 *
 * 返回值：
 *   转换成功返回货币总数（铜币），失败返回 nullopt
 *
 * 主要流程：
 *   1. 使用空格分割字符串
 *   2. 解析每个标记的数字和单位
 *   3. 验证每种单位只出现一次
 *   4. 汇总计算总铜币数（1金=100银=10000铜）
 */
Optional<int32> MoneyStringToMoney(std::string const& moneyString)
{
    int32 money = 0;

    // 标记是否已经出现过某种货币单位，防止重复
    bool hadG = false;
    bool hadS = false;
    bool hadC = false;

    // 使用空格分割货币字符串，如 "10g 50s 25c"
    for (std::string_view token : Trinity::Tokenize(moneyString, ' ', false))
    {
        uint32 unit;
        // 根据最后一个字符判断货币单位
        switch (token[token.length() - 1])
        {
            case 'g': // 金币：1g = 100s = 10000c
                if (hadG) return std::nullopt; // 重复的金币单位，格式错误
                hadG = true;
                unit = 100 * 100;
                break;
            case 's': // 银币：1s = 100c
                if (hadS) return std::nullopt; // 重复的银币单位，格式错误
                hadS = true;
                unit = 100;
                break;
            case 'c': // 铜币：基本单位
                if (hadC) return std::nullopt; // 重复的铜币单位，格式错误
                hadC = true;
                unit = 1;
                break;
            default:
                return std::nullopt; // 未知的货币单位
        }

        // 提取数字部分（去掉最后一个单位的字符）
        Optional<uint32> amount = Trinity::StringTo<uint32>(token.substr(0, token.length() - 1));
        if (amount)
            money += (unit * *amount); // 累加转换为铜币总数
        else
            return std::nullopt; // 数字解析失败
    }

    return money;
}

/**
 * @brief 将时间字符串转换为秒数
 *
 * 职责：
 *   解析时间字符串（如 "1d2h3m4s"），转换为总秒数
 *   支持的单位：d(天)、h(小时)、m(分钟)、s(秒)
 *
 * 参数：
 *   timestring: 时间字符串，如 "1d2h3m4s"
 *
 * 返回值：
 *   总秒数，格式错误返回 0
 *
 * 主要流程：
 *   1. 遍历字符串，累积数字
 *   2. 遇到单位字符时，将累积的数字乘以对应倍数
 *   3. 累加到总秒数中
 */
uint32 TimeStringToSecs(std::string const& timestring)
{
    uint32 secs       = 0;  // 总秒数
    uint32 buffer     = 0;  // 数字缓冲区，累积连续的数字字符
    uint32 multiplier = 0;  // 时间单位倍数

    // 遍历时间字符串的每个字符
    for (char itr : timestring)
    {
        if (isdigit(itr))
        {
            // 如果是数字，累积到 buffer 中（支持多位数）
            buffer *= 10;
            buffer += itr - '0';
        }
        else
        {
            // 如果是非数字字符，判断时间单位
            switch (itr)
            {
                case 'd': multiplier = DAY;     break;  // 天
                case 'h': multiplier = HOUR;    break;  // 小时
                case 'm': multiplier = MINUTE;  break;  // 分钟
                case 's': multiplier = 1;       break;  // 秒
                default : return 0;                         // 错误格式，返回 0
            }
            // 计算当前时间单位的秒数并累加
            buffer *= multiplier;
            secs += buffer;
            // 重置缓冲区，准备处理下一个数字
            buffer = 0;
        }
    }

    return secs;
}

/**
 * @brief 将时间戳转换为格式化的时间字符串
 *
 * 职责：
 *   将 Unix 时间戳转换为可读的日期时间字符串
 *   格式：YYYY-MM-DD_HH-MM-SS
 *
 * 参数：
 *   t: Unix 时间戳
 *
 * 返回值：
 *   格式化后的时间字符串
 *
 * 主要流程：
 *   1. 使用 localtime_r 转换为本地时间
 *   2. 格式化为 YYYY-MM-DD_HH-MM-SS 格式
 */
std::string TimeToTimestampStr(time_t t)
{
    tm aTm;
    // 将时间戳转换为本地时间结构
    localtime_r(&t, &aTm);
    // 格式化为 YYYY-MM-DD_HH-MM-SS 形式的时间戳字符串
    // 注意：tm_year 是从 1900 开始的年数，tm_mon 是 0-11 的月份
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    return Trinity::StringFormat("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", aTm.tm_year + 1900, aTm.tm_mon + 1, aTm.tm_mday, aTm.tm_hour, aTm.tm_min, aTm.tm_sec);
}

/**
 * @brief 将时间戳转换为人类可读的字符串
 *
 * 职责：
 *   将 Unix 时间戳转换为本地化的日期时间字符串
 *   使用系统本地化格式显示
 *
 * 参数：
 *   t: Unix 时间戳
 *
 * 返回值：
 *   人类可读的时间字符串
 *
 * 主要流程：
 *   1. 转换为本地时间结构
 *   2. 使用 strftime 格式化为本地化字符串
 */
std::string TimeToHumanReadable(time_t t)
{
    tm time;
    // 转换为本地时间
    localtime_r(&t, &time);
    char buf[30];
    // 使用系统本地化格式格式化时间（%c 表示本地化的日期时间表示）
    strftime(buf, 30, "%c", &time);
    return std::string(buf);
}

/**
 * @brief 检查字符串是否为有效的 IP 地址
 *
 * 职责：
 *   验证给定字符串是否为有效的 IPv4 或 IPv6 地址
 *
 * 参数：
 *   ipaddress: IP 地址字符串
 *
 * 返回值：
 *   有效返回 true，无效返回 false
 *
 * 主要流程：
 *   使用 boost 库的地址解析功能进行验证
 */
/// Check if the string is a valid ip address representation
bool IsIPAddress(char const* ipaddress)
{
    // 检查空指针
    if (!ipaddress)
        return false;

    // 使用 boost 库的地址解析功能验证 IP 地址
    boost::system::error_code error;
    Trinity::Net::make_address(ipaddress, error);
    // 如果没有错误，说明是有效的 IP 地址
    return !error;
}

/**
 * @brief 创建 PID 文件
 *
 * 职责：
 *   创建包含当前进程 ID 的文件，用于防止多实例运行
 *
 * 参数：
 *   filename: PID 文件的路径
 *
 * 返回值：
 *   成功返回当前进程 ID，失败返回 0
 *
 * 主要流程：
 *   1. 打开文件进行写入
 *   2. 获取当前进程 ID
 *   3. 将进程 ID 写入文件
 */
/// create PID file
uint32 CreatePIDFile(std::string const& filename)
{
    // 打开文件用于写入（如果文件已存在则覆盖）
    FILE* pid_file = fopen(filename.c_str(), "w");
    if (pid_file == nullptr)
        return 0;

    // 获取当前进程 ID
    uint32 pid = GetPID();

    // 将进程 ID 写入文件
    fprintf(pid_file, "%u", pid);
    fclose(pid_file);

    return pid;
}

/**
 * @brief 获取当前进程 ID
 *
 * 职责：
 *   获取当前进程的进程标识符（PID）
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   当前进程的 ID
 *
 * 主要流程：
 *   根据平台调用不同的系统函数获取进程 ID
 *   Windows 使用 GetCurrentProcessId，Linux 使用 getpid
 */
uint32 GetPID()
{
#ifdef _WIN32
    // Windows 平台：使用 Windows API 获取进程 ID
    DWORD pid = GetCurrentProcessId();
#else
    // Linux/Unix 平台：使用 POSIX 标准函数获取进程 ID
    pid_t pid = getpid();
#endif

    return uint32(pid);
}

/**
 * @brief 计算 UTF-8 字符串的字符长度
 *
 * 职责：
 *   计算 UTF-8 编码字符串中的字符数量（而非字节数）
 *   正确处理多字节字符
 *
 * 参数：
 *   utf8str: UTF-8 编码的字符串
 *
 * 返回值：
 *   字符数量，失败返回 0 并清空字符串
 *
 * 主要流程：
 *   使用 utf8 库的 distance 函数计算字符数
 */
size_t utf8length(std::string& utf8str)
{
    try
    {
        // 使用 utf8 库的 distance 函数计算 UTF-8 字符串中的字符数量
        // 注意：这与 std::string::size() 不同，后者返回字节数
        return utf8::distance(utf8str.c_str(), utf8str.c_str()+utf8str.size());
    }
    catch(std::exception const&)
    {
        // 如果字符串包含无效的 UTF-8 序列，清空字符串并返回 0
        utf8str.clear();
        return 0;
    }
}

/**
 * @brief 截断 UTF-8 字符串到指定字符长度
 *
 * 职责：
 *   将 UTF-8 字符串截断到指定的字符数量（不是字节数）
 *   确保不会在多字节字符中间截断
 *
 * 参数：
 *   utf8str: 要截断的 UTF-8 字符串（会被修改）
 *   len: 目标字符数量
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 计算当前字符串的字符数
 *   2. 如果已经小于等于目标长度，直接返回
 *   3. 转换为宽字符，截断到指定长度
 *   4. 转换回 UTF-8 并更新字符串
 */
void utf8truncate(std::string& utf8str, size_t len)
{
    try
    {
        // 计算当前字符串的字符数量
        size_t wlen = utf8::distance(utf8str.c_str(), utf8str.c_str()+utf8str.size());
        // 如果字符数已经小于等于目标长度，无需截断
        if (wlen <= len)
            return;

        // 转换为宽字符串（UTF-16）以便按字符截断
        std::wstring wstr;
        wstr.resize(wlen);
        utf8::utf8to16(utf8str.c_str(), utf8str.c_str()+utf8str.size(), &wstr[0]);
        // 截断宽字符串到指定字符长度
        wstr.resize(len);
        // 转换回 UTF-8 编码
        char* oend = utf8::utf16to8(wstr.c_str(), wstr.c_str()+wstr.size(), &utf8str[0]);
        // 调整字符串大小，移除未使用的尾部空间
        utf8str.resize(oend-(&utf8str[0]));
    }
    catch(std::exception const&)
    {
        // 转换失败时清空字符串
        utf8str.clear();
    }
}

/**
 * @brief 将 UTF-8 字符串转换为宽字符串（C 风格）
 *
 * 职责：
 *   将 UTF-8 编码的字符串转换为宽字符（wchar_t）编码
 *   使用预分配的缓冲区
 *
 * 参数：
 *   utf8str: UTF-8 源字符串指针
 *   csize: UTF-8 字符串的字节长度
 *   wstr: 宽字符输出缓冲区
 *   wsize: 输入时为缓冲区大小，输出时为实际使用的字符数
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   1. 使用 utf8 库将 UTF-8 转换为 UTF-16
 *   2. 处理转换错误，必要时输出错误信息
 *   3. 更新输出参数 wsize
 */
bool Utf8toWStr(char const* utf8str, size_t csize, wchar_t* wstr, size_t& wsize)
{
    try
    {
        // 使用受检缓冲区输出迭代器，防止缓冲区溢出
        Trinity::CheckedBufferOutputIterator<wchar_t> out(wstr, wsize);
        // 执行 UTF-8 到 UTF-16 的转换
        out = utf8::utf8to16(utf8str, utf8str+csize, out);
        // 计算实际使用的字符数（总大小减去剩余未使用空间）
        wsize -= out.remaining();
        // 添加字符串结束符
        wstr[wsize] = L'\0';
    }
    catch(std::exception const&)
    {
        // 转换失败时，如果有足够空间则输出错误消息
        // Replace the converted string with an error message if there is enough space
        // Otherwise just return an empty string
        wchar_t const* errorMessage = L"An error occurred converting string from UTF-8 to WStr";
        size_t errorMessageLength = wcslen(errorMessage);
        if (wsize >= errorMessageLength)
        {
            wcscpy(wstr, errorMessage);
            wsize = wcslen(wstr);
        }
        else if (wsize > 0)
        {
            // 空间不足，返回空字符串
            wstr[0] = L'\0';
            wsize = 0;
        }
        else
            wsize = 0;

        return false;
    }

    return true;
}

/**
 * @brief 将 UTF-8 字符串转换为宽字符串（C++ 风格）
 *
 * 职责：
 *   将 UTF-8 编码的字符串视图转换为宽字符串
 *
 * 参数：
 *   utf8str: UTF-8 源字符串视图
 *   wstr: 输出的宽字符串引用
 *
 * 返回值：
 *   成功返回 true，失败返回 false 并清空输出
 *
 * 主要流程：
 *   使用 utf8 库将 UTF-8 转换为 UTF-16 并填充到 wstr
 */
bool Utf8toWStr(std::string_view utf8str, std::wstring& wstr)
{
    wstr.clear();
    try
    {
        // 使用 back_inserter 自动扩展 wstr 的大小
        utf8::utf8to16(utf8str.begin(), utf8str.end(), std::back_inserter(wstr));
    }
    catch(std::exception const&)
    {
        // 转换失败时清空输出
        wstr.clear();
        return false;
    }

    return true;
}

/**
 * @brief 将宽字符串转换为 UTF-8 字符串（C 风格）
 *
 * 职责：
 *   将宽字符（wchar_t）字符串转换为 UTF-8 编码
 *
 * 参数：
 *   wstr: 宽字符源字符串指针
 *   size: 宽字符字符串长度
 *   utf8str: 输出的 UTF-8 字符串引用
 *
 * 返回值：
 *   成功返回 true，失败返回 false 并清空输出
 *
 * 主要流程：
 *   1. 分配足够大的缓冲区（每个宽字符最多 4 字节）
 *   2. 使用 utf8 库转换
 *   3. 调整字符串大小并赋值
 */
bool WStrToUtf8(wchar_t const* wstr, size_t size, std::string& utf8str)
{
    try
    {
        std::string utf8str2;
        // 预分配空间：每个宽字符最多可能占用 4 个字节（UTF-8 编码的最大长度）
        utf8str2.resize(size*4);

        if (size)
        {
            // 执行 UTF-16 到 UTF-8 的转换
            char* oend = utf8::utf16to8(wstr, wstr+size, &utf8str2[0]);
            // 调整字符串大小到实际使用的长度
            utf8str2.resize(oend-(&utf8str2[0]));
        }
        utf8str = utf8str2;
    }
    catch(std::exception const&)
    {
        // 转换失败时清空输出
        utf8str.clear();
        return false;
    }

    return true;
}

/**
 * @brief 将宽字符串视图转换为 UTF-8 字符串
 *
 * 职责：
 *   将宽字符字符串视图转换为 UTF-8 编码
 *
 * 参数：
 *   wstr: 宽字符源字符串视图
 *   utf8str: 输出的 UTF-8 字符串引用
 *
 * 返回值：
 *   成功返回 true，失败返回 false 并清空输出
 *
 * 主要流程：
 *   与 C 风格版本类似，使用 string_view 接口
 */
bool WStrToUtf8(std::wstring_view wstr, std::string& utf8str)
{
    try
    {
        std::string utf8str2;
        // 预分配空间：每个宽字符最多可能占用 4 个字节
        utf8str2.resize(wstr.size()*4);

        if (!wstr.empty())
        {
            // 执行 UTF-16 到 UTF-8 的转换
            char* oend = utf8::utf16to8(wstr.begin(), wstr.end(), &utf8str2[0]);
            // 调整字符串大小到实际使用的长度
            utf8str2.resize(oend-(&utf8str2[0]));
        }
        utf8str = utf8str2;
    }
    catch(std::exception const&)
    {
        // 转换失败时清空输出
        utf8str.clear();
        return false;
    }

    return true;
}

/**
 * @brief 将宽字符串转换为大写
 *
 * 职责：
 *   原地转换宽字符串中所有字符为大写形式
 */
void wstrToUpper(std::wstring& str) { std::transform(std::begin(str), std::end(str), std::begin(str), wcharToUpper); }

/**
 * @brief 将宽字符串转换为小写
 *
 * 职责：
 *   原地转换宽字符串中所有字符为小写形式
 */
void wstrToLower(std::wstring& str) { std::transform(std::begin(str), std::end(str), std::begin(str), wcharToLower); }

/**
 * @brief 将字符串转换为大写
 *
 * 职责：
 *   原地转换字符串中所有字符为大写形式
 */
void strToUpper(std::string& str) { std::transform(std::begin(str), std::end(str), std::begin(str), charToUpper); }

/**
 * @brief 将字符串转换为小写
 *
 * 职责：
 *   原地转换字符串中所有字符为小写形式
 */
void strToLower(std::string& str) { std::transform(std::begin(str), std::end(str), std::begin(str), charToLower); }

/**
 * @brief 获取名字的主要部分（俄语名字变格处理）
 *
 * 职责：
 *   根据指定的变格形式，移除俄语名字的结尾部分
 *   用于俄语名字的语法变化处理
 *
 * 参数：
 *   wname: 宽字符串形式的俄语名字
 *   declension: 变格形式（0-5）
 *
 * 返回值：
 *   处理后的名字主要部分
 *
 * 主要流程：
 *   1. 检查是否为俄语字符
 *   2. 根据变格形式查找对应的结尾
 *   3. 移除匹配的结尾部分
 */
std::wstring GetMainPartOfName(std::wstring const& wname, uint32 declension)
{
    // 仅支持俄语西里尔字母的名字变格处理
    // supported only Cyrillic cases
    if (wname.empty() || !isCyrillicCharacter(wname[0]) || declension > 5)
        return wname;

    // 定义俄语名字的各种结尾形式（使用 Unicode 编码）
    // Important: end length must be <= MAX_INTERNAL_PLAYER_NAME-MAX_PLAYER_NAME (3 currently)
    static std::wstring const a_End    = { wchar_t(0x0430)                  }; // а
    static std::wstring const o_End    = { wchar_t(0x043E)                  }; // о
    static std::wstring const ya_End   = { wchar_t(0x044F)                  }; // я
    static std::wstring const ie_End   = { wchar_t(0x0435)                  }; // е
    static std::wstring const i_End    = { wchar_t(0x0438)                  }; // и
    static std::wstring const yeru_End = { wchar_t(0x044B)                  }; // ы
    static std::wstring const u_End    = { wchar_t(0x0443)                  }; // у
    static std::wstring const yu_End   = { wchar_t(0x044E)                  }; // ю
    static std::wstring const oj_End   = { wchar_t(0x043E), wchar_t(0x0439) }; // ой
    static std::wstring const ie_j_End = { wchar_t(0x0435), wchar_t(0x0439) }; // ей
    static std::wstring const io_j_End = { wchar_t(0x0451), wchar_t(0x0439) }; // ёй
    static std::wstring const o_m_End  = { wchar_t(0x043E), wchar_t(0x043C) }; // ом
    static std::wstring const io_m_End = { wchar_t(0x0451), wchar_t(0x043C) }; // ём
    static std::wstring const ie_m_End = { wchar_t(0x0435), wchar_t(0x043C) }; // ем
    static std::wstring const soft_End = { wchar_t(0x044C)                  }; // ь
    static std::wstring const j_End    = { wchar_t(0x0439)                  }; // й

    // 定义每种变格形式需要移除的结尾列表
    static std::array<std::array<std::wstring const*, 7>, 6> const dropEnds = {{
        { &a_End,  &o_End,    &ya_End,   &ie_End,  &soft_End, &j_End,    nullptr },
        { &a_End,  &ya_End,   &yeru_End, &i_End,   nullptr,   nullptr,   nullptr },
        { &ie_End, &u_End,    &yu_End,   &i_End,   nullptr,   nullptr,   nullptr },
        { &u_End,  &yu_End,   &o_End,    &ie_End,  &soft_End, &ya_End,   &a_End  },
        { &oj_End, &io_j_End, &ie_j_End, &o_m_End, &io_m_End, &ie_m_End, &yu_End },
        { &ie_End, &i_End,    nullptr,   nullptr,  nullptr,   nullptr,   nullptr }
    }};

    // 获取当前名字长度
    std::size_t const thisLen = wname.length();
    // 查找对应变格形式的结尾列表
    std::array<std::wstring const*, 7> const& endings = dropEnds[declension];
    // 遍历所有可能的结尾，尝试匹配并移除
    for (auto itr = endings.begin(), end = endings.end(); (itr != end) && *itr; ++itr)
    {
        std::wstring const& ending = **itr;
        std::size_t const endLen = ending.length();
        // 跳过比名字还长的结尾
        if (!(endLen <= thisLen))
            continue;

        // 如果名字以该结尾结尾，则移除结尾并返回名字的主体部分
        if (wname.substr(thisLen-endLen, thisLen) == ending)
            return wname.substr(0, thisLen-endLen);
    }

    // 没有匹配的结尾，返回原名字
    return wname;
}

/**
 * @brief 将 UTF-8 字符串转换为控制台编码
 *
 * 职责：
 *   将 UTF-8 字符串转换为控制台输出所需的编码格式
 *   Windows 平台需要转换，Linux 平台直接使用 UTF-8
 *
 * 参数：
 *   utf8str: UTF-8 源字符串
 *   conStr: 输出的控制台编码字符串
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   Windows: UTF-8 -> 宽字符 -> 控制台编码
 *   Linux: 直接复制
 */
bool utf8ToConsole(std::string_view utf8str, std::string& conStr)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows 平台：UTF-8 -> 宽字符 -> 控制台编码
    std::wstring wstr;
    if (!Utf8toWStr(utf8str, wstr))
        return false;

    // 分配输出缓冲区
    conStr.resize(wstr.size());
    // 使用 Windows API 将宽字符转换为控制台编码（OEM 编码）
    CharToOemBuffW(&wstr[0], &conStr[0], uint32(wstr.size()));
#else
    // Linux/Unix 平台：终端通常使用 UTF-8，直接复制即可
    // not implemented yet
    conStr = utf8str;
#endif

    return true;
}

/**
 * @brief 将控制台编码字符串转换为 UTF-8
 *
 * 职责：
 *   将控制台输入的字符串转换为 UTF-8 编码
 *   Windows 平台需要转换，Linux 平台直接使用
 *
 * 参数：
 *   conStr: 控制台编码的源字符串
 *   utf8str: 输出的 UTF-8 字符串
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   Windows: 控制台编码 -> 宽字符 -> UTF-8
 *   Linux: 直接复制
 */
bool consoleToUtf8(std::string_view conStr, std::string& utf8str)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows 平台：控制台编码 -> 宽字符 -> UTF-8
    std::wstring wstr;
    wstr.resize(conStr.size());
    // 使用 Windows API 将控制台编码（OEM 编码）转换为宽字符
    OemToCharBuffW(&conStr[0], &wstr[0], uint32(conStr.size()));

    // 将宽字符转换为 UTF-8
    return WStrToUtf8(wstr, utf8str);
#else
    // Linux/Unix 平台：终端通常使用 UTF-8，直接复制即可
    // not implemented yet
    utf8str = conStr;
    return true;
#endif
}

/**
 * @brief 检查 UTF-8 字符串是否包含指定的宽字符串（不区分大小写）
 *
 * 职责：
 *   在 UTF-8 字符串中搜索指定的宽字符串
 *   搜索不区分大小写
 *
 * 参数：
 *   str: UTF-8 源字符串
 *   search: 要搜索的宽字符串
 *
 * 返回值：
 *   找到返回 true，未找到返回 false
 *
 * 主要流程：
 *   1. 将 UTF-8 转换为宽字符串
 *   2. 转换为小写
 *   3. 执行子串搜索
 */
bool Utf8FitTo(std::string_view str, std::wstring_view search)
{
    std::wstring temp;

    // 将 UTF-8 字符串转换为宽字符串
    if (!Utf8toWStr(str, temp))
        return false;

    // 转换为小写以进行不区分大小写的搜索
    // converting to lower case
    wstrToLower(temp);

    // 查找子串
    if (temp.find(search) == std::wstring::npos)
        return false;

    return true;
}

/**
 * @brief UTF-8 格式化打印函数
 *
 * 职责：
 *   向文件流输出 UTF-8 格式化的字符串
 *   支持可变参数列表
 *   自动处理 Windows 控制台编码转换
 *
 * 参数：
 *   out: 输出文件流
 *   str: 格式化字符串
 *   ...: 可变参数
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   包装 vutf8printf 函数，处理可变参数
 */
void utf8printf(FILE* out, const char *str, ...)
{
    va_list ap;
    va_start(ap, str);
    vutf8printf(out, str, &ap);
    va_end(ap);
}

/**
 * @brief UTF-8 格式化打印函数（va_list 版本）
 *
 * 职责：
 *   向文件流输出 UTF-8 格式化的字符串
 *   使用 va_list 参数，供其他函数调用
 *   自动处理 Windows 控制台编码转换
 *
 * 参数：
 *   out: 输出文件流
 *   str: 格式化字符串
 *   ap: 可变参数列表指针
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   Windows:
 *     1. 使用 vsnprintf 格式化到缓冲区
 *     2. 转换为宽字符
 *     3. 转换为控制台编码输出
 *   Linux:
 *     直接使用 vfprintf 输出
 */
void vutf8printf(FILE* out, const char *str, va_list* ap)
{
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    // Windows 平台：需要从 UTF-8 转换为控制台编码
    char temp_buf[32 * 1024];
    wchar_t wtemp_buf[32 * 1024];

    // 使用 vsnprintf 格式化到缓冲区
    size_t temp_len = vsnprintf(temp_buf, 32 * 1024, str, *ap);
    //vsnprintf returns -1 if the buffer is too small
    if (temp_len == size_t(-1))
        temp_len = 32*1024-1;

    // 将 UTF-8 字符串转换为宽字符
    size_t wtemp_len = 32*1024-1;
    Utf8toWStr(temp_buf, temp_len, wtemp_buf, wtemp_len);

    // 将宽字符转换为控制台编码（OEM 编码）并输出
    CharToOemBuffW(&wtemp_buf[0], &temp_buf[0], uint32(wtemp_len + 1));
    fprintf(out, "%s", temp_buf);
#else
    // Linux/Unix 平台：终端通常使用 UTF-8，直接输出即可
    vfprintf(out, str, *ap);
#endif
}

/**
 * @brief 将 UTF-8 字符串中的拉丁字母转换为大写
 *
 * 职责：
 *   仅将 UTF-8 字符串中的拉丁字母转换为大写
 *   其他字符保持不变
 *
 * 参数：
 *   utf8String: 要转换的 UTF-8 字符串（会被修改）
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   1. 转换为宽字符串
 *   2. 仅对拉丁字符执行大写转换
 *   3. 转换回 UTF-8
 */
bool Utf8ToUpperOnlyLatin(std::string& utf8String)
{
    std::wstring wstr;
    // 将 UTF-8 转换为宽字符串
    if (!Utf8toWStr(utf8String, wstr))
        return false;

    // 仅对拉丁字符执行大写转换（非拉丁字符保持不变）
    std::transform(wstr.begin(), wstr.end(), wstr.begin(), wcharToUpperOnlyLatin);

    // 转换回 UTF-8
    return WStrToUtf8(wstr, utf8String);
}

#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
/**
 * @brief 从 Windows 控制台读取输入
 *
 * 职责：
 *   从 Windows 控制台读取用户输入，自动转换为 UTF-8
 *   仅在 Windows 平台可用
 *
 * 参数：
 *   str: 输出的 UTF-8 字符串
 *   size: 读取的最大字符数，默认 256
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   1. 使用 ReadConsoleW 读取宽字符输入
 *   2. 转换为 UTF-8 字符串
 */
bool ReadWinConsole(std::string& str, size_t size /*= 256*/)
{
    // 分配宽字符缓冲区
    wchar_t* commandbuf = new wchar_t[size + 1];
    // 获取标准输入句柄
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    DWORD read = 0;

    // 使用 Windows API 读取宽字符输入
    if (!ReadConsoleW(hConsole, commandbuf, size, &read, nullptr) || read == 0)
    {
        delete[] commandbuf;
        return false;
    }

    // 添加字符串结束符
    commandbuf[read] = 0;

    // 将宽字符转换为 UTF-8
    bool ok = WStrToUtf8(commandbuf, wcslen(commandbuf), str);
    delete[] commandbuf;
    return ok;
}

/**
 * @brief 向 Windows 控制台写入输出
 *
 * 职责：
 *   将 UTF-8 字符串转换为宽字符并写入 Windows 控制台
 *   仅在 Windows 平台可用
 *
 * 参数：
 *   str: 要输出的 UTF-8 字符串视图
 *   error: 是否输出到标准错误流（默认 false，输出到标准输出）
 *
 * 返回值：
 *   成功返回 true，失败返回 false
 *
 * 主要流程：
 *   1. 转换为宽字符
 *   2. 使用 WriteConsoleW 写入控制台
 */
bool WriteWinConsole(std::string_view str, bool error /*= false*/)
{
    std::wstring wstr;
    // 将 UTF-8 转换为宽字符
    if (!Utf8toWStr(str, wstr))
        return false;

    // 根据参数选择输出到标准输出或标准错误流
    HANDLE hConsole = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD write = 0;

    // 使用 Windows API 写入宽字符到控制台
    return WriteConsoleW(hConsole, wstr.c_str(), wstr.size(), &write, nullptr);
}
#endif

/**
 * @brief 移除字符串中的换行符
 *
 * 职责：
 *   移除字符串中第一个换行符（\r 或 \n）及其后的所有内容
 *
 * 参数：
 *   str: 要处理的字符串（会被修改）
 *
 * 返回值：
 *   如果找到并移除了换行符，返回换行符的位置；否则返回 nullopt
 *
 * 主要流程：
 *   1. 查找第一个 \r 或 \n 字符
 *   2. 从该位置截断字符串
 *   3. 返回换行符位置
 */
TC_COMMON_API Optional<std::size_t> RemoveCRLF(std::string & str)
{
    // 查找第一个换行符（回车或换行）
    std::size_t nextLineIndex = str.find_first_of("\r\n");
    if (nextLineIndex == std::string::npos)
        return std::nullopt;

    // 从换行符位置截断字符串
    str.erase(nextLineIndex);
    // 返回换行符的位置
    return nextLineIndex;
}

/**
 * @brief 将字节数组转换为十六进制字符串
 *
 * 职责：
 *   将字节数组转换为十六进制字符串表示
 *   可选择是否反转字节顺序
 *
 * 参数：
 *   bytes: 字节数组指针
 *   arrayLen: 数组长度
 *   reverse: 是否反转字节顺序（默认 false）
 *
 * 返回值：
 *   十六进制字符串（大写，每字节两个字符）
 *
 * 主要流程：
 *   1. 根据是否反转设置遍历方向
 *   2. 遍历字节数组
 *   3. 将每个字节格式化为两位十六进制
 */
std::string Trinity::Impl::ByteArrayToHexStr(uint8 const* bytes, size_t arrayLen, bool reverse /* = false */)
{
    // 设置遍历方向：正向或反向
    int32 init = 0;
    int32 end = arrayLen;
    int8 op = 1;

    if (reverse)
    {
        // 反向遍历：从最后一个字节开始
        init = arrayLen - 1;
        end = -1;
        op = -1;
    }

    std::string result;
    // 预分配空间：每个字节对应两个十六进制字符
    result.reserve(arrayLen * 2);
    auto inserter = std::back_inserter(result);
    // 遍历字节数组，将每个字节格式化为两位十六进制
    for (int32 i = init; i != end; i += op)
        Trinity::StringFormatTo(inserter, "{:02X}", bytes[i]);

    return result;
}

/**
 * @brief 将十六进制字符串转换为字节数组
 *
 * 职责：
 *   将十六进制字符串解析为字节数组
 *   可选择是否反转字节顺序
 *
 * 参数：
 *   str: 十六进制字符串（长度必须是 2 * outlen）
 *   out: 输出字节数组指针
 *   outlen: 输出数组长度
 *   reverse: 是否反转字节顺序（默认 false）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 验证字符串长度正确
 *   2. 根据是否反转设置遍历方向
 *   3. 每两个字符解析为一个字节
 */
void Trinity::Impl::HexStrToByteArray(std::string_view str, uint8* out, size_t outlen, bool reverse /*= false*/)
{
    // 验证输入字符串长度：每个字节需要两个十六进制字符
    ASSERT(str.size() == (2 * outlen));

    // 设置遍历方向：正向或反向
    int32 init = 0;
    int32 end = int32(str.length());
    int8 op = 1;

    if (reverse)
    {
        // 反向遍历：从最后两个字符开始
        init = int32(str.length() - 2);
        end = -2;
        op = -1;
    }

    uint32 j = 0;
    // 每两个字符解析为一个字节
    for (int32 i = init; i != end; i += 2 * op)
        out[j++] = Trinity::StringTo<uint8>(str.substr(i, 2), 16).value_or(0);
}

/**
 * @brief 不区分大小写的字符串相等比较
 *
 * 职责：
 *   比较两个字符串是否相等，忽略大小写差异
 *
 * 参数：
 *   a: 第一个字符串
 *   b: 第二个字符串
 *
 * 返回值：
 *   相等返回 true，不等返回 false
 *
 * 主要流程：
 *   使用 std::equal 算法，对每对字符调用 tolower 比较
 */
bool StringEqualI(std::string_view a, std::string_view b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
}

/**
 * @brief 不区分大小写的字符串包含检查
 *
 * 职责：
 *   检查 haystack 中是否包含 needle，忽略大小写差异
 *
 * 参数：
 *   haystack: 被搜索的字符串
 *   needle: 要搜索的子串
 *
 * 返回值：
 *   包含返回 true，不包含返回 false
 *
 * 主要流程：
 *   使用 std::search 算法，对每对字符调用 tolower 比较
 */
bool StringContainsStringI(std::string_view haystack, std::string_view needle)
{
    return haystack.end() !=
        std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
}

/**
 * @brief 不区分大小写的字符串小于比较
 *
 * 职责：
 *   比较两个字符串的字典序大小，忽略大小写差异
 *
 * 参数：
 *   a: 第一个字符串
 *   b: 第二个字符串
 *
 * 返回值：
 *   a 小于 b 返回 true，否则返回 false
 *
 * 主要流程：
 *   使用 std::lexicographical_compare 算法，对每对字符调用 tolower 比较
 */
bool StringCompareLessI(std::string_view a, std::string_view b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char c1, char c2) { return std::tolower(c1) < std::tolower(c2); });
}

/**
 * @brief 获取类型的可读名称
 *
 * 职责：
 *   将类型信息对象转换为人类可读的类型名称
 *   解码编译器生成的名称修饰（name mangling）
 *
 * 参数：
 *   info: 类型信息对象（type_info）
 *
 * 返回值：
 *   可读的类型名称字符串
 *
 * 主要流程：
 *   使用 boost::core::demangle 解码类型名称
 */
std::string Trinity::Impl::GetTypeName(std::type_info const& info)
{
    return boost::core::demangle(info.name());
}
