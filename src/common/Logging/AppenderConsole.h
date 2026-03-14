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
 * @file AppenderConsole.h
 * @brief 控制台日志追加器头文件
 *
 * 本文件定义了AppenderConsole类，负责将日志消息输出到控制台（标准输出或标准错误）。
 * 支持彩色输出功能，可以为不同级别的日志设置不同的显示颜色。
 *
 * 平台兼容性：
 *   - Windows平台：使用控制台API实现彩色输出
 *   - Unix/Linux平台：使用ANSI转义序列实现彩色输出
 */

#ifndef APPENDERCONSOLE_H
#define APPENDERCONSOLE_H

#include "Appender.h"

/**
 * @brief 控制台颜色类型枚举
 *
 * 定义了控制台输出支持的所有颜色，包括标准色和高亮色
 * 在Windows平台映射为控制台API颜色属性，在Unix/Linux平台映射为ANSI颜色码
 */
enum ColorTypes
{
    BLACK,      ///< 黑色
    RED,        ///< 红色
    GREEN,      ///< 绿色
    BROWN,      ///< 棕色
    BLUE,       ///< 蓝色
    MAGENTA,    ///< 洋红色（紫红色）
    CYAN,       ///< 青色（蓝绿色）
    GREY,       ///< 灰色
    YELLOW,     ///< 黄色（高亮）
    LRED,       ///< 亮红色
    LGREEN,     ///< 亮绿色
    LBLUE,      ///< 亮蓝色
    LMAGENTA,   ///< 亮洋红色
    LCYAN,      ///< 亮青色
    WHITE,      ///< 白色
    NUM_COLOR_TYPES ///< 颜色类型总数（枚举计数，不作为实际颜色使用）
};

/**
 * @brief 控制台日志追加器
 *
 * 继承自Appender基类，实现日志消息输出到控制台的功能。
 * 支持彩色输出，可以为不同日志级别设置不同的显示颜色。
 *
 * 主要特性：
 *   - 支持标准输出(stdout)和标准错误(stderr)输出
 *   - ERROR和FATAL级别自动输出到stderr
 *   - 其他级别输出到stdout
 *   - 支持彩色输出，提高日志可读性
 *   - 跨平台支持（Windows和Unix/Linux）
 *
 * 输出目标选择：
 *   - FATAL、ERROR级别 -> stderr（标准错误流）
 *   - 其他级别 -> stdout（标准输出流）
 */
class TC_COMMON_API AppenderConsole : public Appender
{
    public:
        /// 追加器类型标识，用于工厂模式创建实例
        static constexpr AppenderType type = APPENDER_CONSOLE;

        /**
         * @brief 构造函数
         *
         * @param _id    追加器唯一标识符
         * @param name   追加器名称
         * @param level  追加器的日志级别
         * @param flags  追加器标志位
         * @param args   配置参数数组，args[3]为颜色配置字符串
         *
         * 颜色配置格式：空格分隔的6个颜色值，分别对应FATAL、ERROR、WARN、INFO、DEBUG、TRACE级别
         * 例如："1 1 2 3 4 5"
         */
        AppenderConsole(uint8 _id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args);

        /**
         * @brief 初始化颜色配置
         *
         * @param name     追加器名称（用于错误消息）
         * @param init_str 颜色配置字符串
         *
         * 解析颜色配置字符串，为每个日志级别设置对应的显示颜色
         */
        void InitColors(std::string const& name, std::string_view init_str);

        /**
         * @brief 获取追加器类型
         * @return APPENDER_CONSOLE
         */
        AppenderType getType() const override { return type; }

    private:
        /**
         * @brief 设置控制台输出颜色
         *
         * @param stdout_stream true使用stdout，false使用stderr
         * @param color         要设置的颜色类型
         *
         * 平台实现：
         *   - Windows：使用SetConsoleTextAttribute API
         *   - Unix/Linux：输出ANSI转义序列
         */
        void SetColor(bool stdout_stream, ColorTypes color);

        /**
         * @brief 重置控制台颜色
         *
         * @param stdout_stream true使用stdout，false使用stderr
         *
         * 将控制台颜色恢复为默认值
         */
        void ResetColor(bool stdout_stream);

        /**
         * @brief 打印字符串到控制台
         *
         * @param str   要打印的字符串
         * @param error true输出到stderr，false输出到stdout
         */
        void Print(std::string const& str, bool error);

        /**
         * @brief 写入日志消息到控制台（核心输出函数）
         *
         * @param message 日志消息对象指针
         *
         * 实现基类的纯虚函数，根据日志级别选择输出流和颜色
         */
        void _write(LogMessage const* message) override;

        /// 是否启用彩色输出
        bool _colored;
        /// 各日志级别的颜色配置数组，索引对应日志级别（0=FATAL, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=TRACE）
        ColorTypes _colors[NUM_ENABLED_LOG_LEVELS];
};

#endif
