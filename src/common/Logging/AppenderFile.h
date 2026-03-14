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
 * @file AppenderFile.h
 * @brief 文件日志追加器头文件
 *
 * 本文件定义了AppenderFile类，负责将日志消息输出到文件系统。
 * 支持动态文件名、文件大小限制、备份等高级特性。
 *
 * 主要特性：
 *   - 支持静态和动态文件名
 *   - 支持文件大小限制和自动轮转
 *   - 支持文件备份功能
 *   - 支持在文件名中添加时间戳
 *   - 线程安全的文件大小计数
 */

#ifndef APPENDERFILE_H
#define APPENDERFILE_H

#include "Appender.h"
#include <atomic>

/**
 * @brief 文件日志追加器
 *
 * 继承自Appender基类，实现日志消息输出到文件的功能。
 * 支持多种文件管理策略，适用于各种日志场景。
 *
 * 文件名支持：
 *   - 静态文件名：所有日志写入同一个文件
 *   - 动态文件名：使用%s占位符，根据日志参数生成不同文件名
 *   - 时间戳文件名：在文件名中自动添加启动时间戳
 *
 * 文件管理：
 *   - 文件大小限制：超过限制时自动创建新文件
 *   - 自动备份：写模式("w")下可选创建旧文件备份
 *   - 追加模式：默认追加写入，支持自定义模式
 *
 * 性能考虑：
 *   - 使用原子计数器跟踪文件大小
 *   - 每次写入后立即flush确保数据持久化
 */
class TC_COMMON_API AppenderFile : public Appender
{
    public:
        /// 追加器类型标识，用于工厂模式创建实例
        static constexpr AppenderType type = APPENDER_FILE;

        /**
         * @brief 构造函数
         *
         * @param id    追加器唯一标识符
         * @param name  追加器名称
         * @param level 追加器的日志级别
         * @param flags 追加器标志位
         * @param args  配置参数数组
         *              - args[3]: 文件名（必填）
         *              - args[4]: 打开模式，默认"a"（追加）
         *              - args[5]: 最大文件大小（字节），超过后创建新文件
         *
         * 配置示例：
         *   - 文件名："Server.log"
         *   - 动态文件名："Player_%s.log"（%s会被param1替换）
         *   - 模式："a"(追加)或"w"(覆盖)
         *   - 大小限制：10485760(10MB)
         *
         * 标志位支持：
         *   - APPENDER_FLAGS_USE_TIMESTAMP: 在文件名中添加时间戳
         *   - APPENDER_FLAGS_MAKE_FILE_BACKUP: 写模式下创建备份
         */
        AppenderFile(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args);

        /**
         * @brief 析构函数
         *
         * 关闭打开的日志文件
         */
        ~AppenderFile();

        /**
         * @brief 打开日志文件
         *
         * @param name   文件名（相对于日志目录）
         * @param mode   打开模式（"a"追加，"w"覆盖）
         * @param backup 是否创建备份文件
         * @return 成功返回文件指针，失败返回nullptr
         *
         * 主要流程：
         *   1. 如果需要备份，先将现有文件重命名（添加时间戳）
         *   2. 打开新文件并获取文件大小
         */
        FILE* OpenFile(std::string const& name, std::string const& mode, bool backup);

        /**
         * @brief 获取追加器类型
         * @return APPENDER_FILE
         */
        AppenderType getType() const override { return type; }

    private:
        /**
         * @brief 关闭日志文件
         *
         * 关闭当前打开的日志文件句柄
         */
        void CloseFile();

        /**
         * @brief 写入日志消息到文件
         *
         * @param message 日志消息对象指针
         *
         * 主要流程：
         *   1. 检查是否超过最大文件大小
         *   2. 动态文件名模式：每次写入打开/关闭文件
         *   3. 静态文件名模式：超过大小限制时重新打开文件
         *   4. 写入日志内容并flush
         *   5. 更新文件大小计数器
         */
        void _write(LogMessage const* message) override;

        /// 日志文件句柄
        FILE* logfile;
        /// 日志文件名（可能包含%s占位符）
        std::string _fileName;
        /// 日志文件存储目录
        std::string _logDir;
        /// 是否使用动态文件名（文件名包含%s占位符）
        bool _dynamicName;
        /// 是否启用文件备份
        bool _backup;
        /// 最大文件大小限制（字节），0表示无限制
        uint64 _maxFileSize;
        /// 当前文件大小（原子变量，线程安全）
        std::atomic<uint64> _fileSize;
};

#endif
