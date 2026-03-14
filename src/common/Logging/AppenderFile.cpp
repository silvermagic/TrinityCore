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
 * @file AppenderFile.cpp
 * @brief 文件日志追加器实现
 *
 * 本文件实现了AppenderFile类，负责将日志消息持久化到文件系统。
 * 提供文件管理、大小限制、动态文件名等功能。
 */

#include "AppenderFile.h"
#include "Log.h"
#include "LogMessage.h"
#include "StringConvert.h"
#include "Util.h"
#include <algorithm>

/**
 * @brief AppenderFile构造函数
 *
 * 初始化文件日志追加器，解析配置参数并打开日志文件
 *
 * @param id    追加器唯一标识符
 * @param name  追加器名称
 * @param level 追加器的日志级别
 * @param flags 追加器标志位
 * @param args  配置参数数组
 *
 * 参数说明：
 *   args[3] - 文件名（必填），支持%s占位符用于动态文件名
 *   args[4] - 打开模式（可选），默认"a"（追加模式）
 *   args[5] - 最大文件大小（可选），超过后自动创建新文件
 *
 * 主要流程：
 *   1. 验证必要参数（文件名）
 *   2. 解析文件打开模式
 *   3. 处理时间戳标志（在文件名中添加时间戳）
 *   4. 解析最大文件大小
 *   5. 检测是否为动态文件名
 *   6. 打开日志文件（非动态文件名模式）
 */
AppenderFile::AppenderFile(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args) :
    Appender(id, name, level, flags),
    logfile(nullptr),
    _logDir(sLog->GetLogsDir()),
    _maxFileSize(0),
    _fileSize(0)
{
    // 验证必要参数：至少需要4个参数（id, type, level, flags, filename）
    if (args.size() < 4)
        throw InvalidAppenderArgsException(Trinity::StringFormat("Log::CreateAppenderFromConfig: Missing file name for appender {}", name));

    // 获取文件名参数
    _fileName.assign(args[3]);

    // 解析文件打开模式，默认为追加模式"a"
    std::string mode = "a";
    if (4 < args.size())
        mode.assign(args[4]);

    // 如果设置了时间戳标志，在文件名中添加时间戳
    if (flags & APPENDER_FLAGS_USE_TIMESTAMP)
    {
        // 在扩展名前插入时间戳
        size_t dot_pos = _fileName.find_last_of('.');
        if (dot_pos != std::string::npos)
            _fileName.insert(dot_pos, sLog->GetLogsTimestamp());
        else
            _fileName += sLog->GetLogsTimestamp();
    }

    // 解析最大文件大小参数
    if (5 < args.size())
    {
        if (Optional<uint32> size = Trinity::StringTo<uint32>(args[5]))
            _maxFileSize = *size;
        else
            throw InvalidAppenderArgsException(Trinity::StringFormat("Log::CreateAppenderFromConfig: Invalid size '{}' for appender {}", args[5], name));
    }

    // 检测是否为动态文件名（包含%s占位符）
    _dynamicName = std::string::npos != _fileName.find("%s");
    // 检测是否启用备份功能
    _backup = (flags & APPENDER_FLAGS_MAKE_FILE_BACKUP) != 0;

    // 非动态文件名模式：在构造时打开文件
    if (!_dynamicName)
        logfile = OpenFile(_fileName, mode, (mode == "w") && _backup);
}

/**
 * @brief AppenderFile析构函数
 *
 * 关闭日志文件句柄，释放资源
 */
AppenderFile::~AppenderFile()
{
    CloseFile();
}

/**
 * @brief 写入日志消息到文件
 *
 * 实现基类的纯虚函数，将日志消息写入文件系统
 *
 * @param message 日志消息对象指针
 *
 * 主要流程：
 *   1. 检查是否超过最大文件大小
 *   2. 动态文件名模式：
 *      - 根据param1生成实际文件名
 *      - 打开文件、写入、立即关闭
 *   3. 静态文件名模式：
 *      - 超过大小限制时重新创建文件（带备份）
 *      - 写入日志内容并刷新缓冲区
 *   4. 更新文件大小计数器
 *
 * 性能考虑：
 *   - 每次写入后立即flush，确保数据持久化
 *   - 动态文件名模式每次写入都会打开/关闭文件，性能较低
 */
void AppenderFile::_write(LogMessage const* message)
{
    // 检查是否超过最大文件大小
    bool exceedMaxSize = _maxFileSize > 0 && (_fileSize.load() + message->Size()) > _maxFileSize;

    // 动态文件名模式处理
    if (_dynamicName)
    {
        // 使用param1替换%s生成实际文件名
        char namebuf[TRINITY_PATH_MAX];
        snprintf(namebuf, TRINITY_PATH_MAX, _fileName.c_str(), message->param1.c_str());
        // 动态文件名始终使用追加模式，避免删除上次写入的内容
        FILE* file = OpenFile(namebuf, "a", _backup || exceedMaxSize);
        if (!file)
            return;
        // 写入日志内容
        fprintf(file, "%s%s\n", message->prefix.c_str(), message->text.c_str());
        fflush(file);
        _fileSize += uint64(message->Size());
        fclose(file);
        return;
    }
    else if (exceedMaxSize)
    {
        // 静态文件名模式：超过大小限制时重新创建文件（带备份）
        logfile = OpenFile(_fileName, "w", true);
    }

    if (!logfile)
        return;

    // 写入日志内容到文件
    fprintf(logfile, "%s%s\n", message->prefix.c_str(), message->text.c_str());
    // 立即刷新缓冲区，确保数据写入磁盘
    fflush(logfile);
    // 更新文件大小计数器（原子操作，线程安全）
    _fileSize += uint64(message->Size());
}

/**
 * @brief 打开日志文件
 *
 * 打开指定的日志文件，支持备份功能
 *
 * @param filename 文件名（相对于日志目录）
 * @param mode     打开模式（"a"追加，"w"覆盖）
 * @param backup   是否创建备份文件
 * @return 成功返回文件指针，失败返回nullptr
 *
 * 主要流程：
 *   1. 如果需要备份且文件存在：
 *      - 关闭当前文件
 *      - 将现有文件重命名（添加时间戳后缀）
 *   2. 打开新文件
 *   3. 获取当前文件大小（用于追加模式）
 *
 * 备份文件命名格式：
 *   原文件：Server.log
 *   备份：Server.log.2024-03-15_14-30-25
 */
FILE* AppenderFile::OpenFile(std::string const& filename, std::string const& mode, bool backup)
{
    // 构建完整文件路径
    std::string fullName(_logDir + filename);

    // 如果需要备份，将现有文件重命名
    if (backup)
    {
        CloseFile();
        // 构建备份文件名：原文件名.时间戳
        std::string newName(fullName);
        newName.push_back('.');
        newName.append(LogMessage::getTimeStr(time(nullptr)));
        // 将时间戳中的冒号替换为横杠（避免文件名问题）
        std::replace(newName.begin(), newName.end(), ':', '-');
        // 重命名文件（忽略错误，如果无法备份则继续）
        rename(fullName.c_str(), newName.c_str());
    }

    // 打开文件
    if (FILE* ret = fopen(fullName.c_str(), mode.c_str()))
    {
        // 获取当前文件位置作为文件大小（追加模式下为已有内容大小）
        _fileSize = ftell(ret);
        return ret;
    }

    return nullptr;
}

/**
 * @brief 关闭日志文件
 *
 * 关闭当前打开的日志文件句柄，并将句柄置空
 */
void AppenderFile::CloseFile()
{
    if (logfile)
    {
        fclose(logfile);
        logfile = nullptr;
    }
}
