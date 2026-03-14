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

#ifndef APPENDERDB_H
#define APPENDERDB_H

#include "Appender.h"

/**
 * @file AppenderDB.h
 * @brief 数据库日志附加器头文件
 *
 * 本文件定义了 AppenderDB 类，用于将日志消息写入数据库。
 * 该附加器继承自 Appender 基类，实现了将日志记录存储到数据库表中的功能。
 */

/**
 * @class AppenderDB
 * @brief 数据库日志附加器类
 *
 * AppenderDB 是一个日志附加器实现，专门用于将日志消息写入数据库。
 * 该类主要用于将服务器运行过程中的日志记录持久化到数据库中，
 * 便于后续的查询、分析和审计。
 *
 * 主要特点：
 * - 支持领域ID设置，用于多领域环境下的日志区分
 * - 支持启用/禁用功能，可在运行时控制日志记录
 * - 自动将日志消息写入对应的数据库表
 *
 * @note 该附加器通常用于记录重要的系统事件、错误信息和安全审计日志
 * @see Appender
 * @see LogMessage
 */
class TC_DATABASE_API AppenderDB: public Appender
{
    public:
        /**
         * @brief 附加器类型常量
         *
         * 定义该附加器的类型为 APPENDER_DB，用于类型识别和类型转换。
         */
        static constexpr AppenderType type = APPENDER_DB;

        /**
         * @brief 构造函数
         *
         * 创建一个数据库日志附加器实例。
         *
         * @param id 附加器的唯一标识符，用于在日志系统中区分不同的附加器
         * @param name 附加器的名称，用于配置和调试时识别
         * @param level 该附加器处理的最低日志级别，低于此级别的日志将被忽略
         * @param flags 附加器标志位，用于控制附加器的行为特性
         * @param args 配置参数列表，可用于传递数据库连接信息等额外参数
         */
        AppenderDB(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args);

        /**
         * @brief 析构函数
         *
         * 销毁数据库附加器实例，释放相关资源。
         */
        ~AppenderDB();

        /**
         * @brief 设置领域ID
         *
         * 设置该附加器关联的领域ID。在多领域服务器环境中，
         * 该ID会被写入日志记录中，用于区分不同领域的日志。
         *
         * @param realmId 领域的唯一标识符
         * @note 重写自 Appender 基类
         */
        void setRealmId(uint32 realmId) override;

        /**
         * @brief 获取附加器类型
         *
         * 返回该附加器的类型标识。
         *
         * @return 返回 APPENDER_DB 类型常量
         * @note 重写自 Appender 基类
         */
        AppenderType getType() const override { return type; }

    private:
        /**
         * @brief 领域ID
         *
         * 存储当前关联的领域标识符，用于在数据库日志记录中标识来源领域。
         */
        uint32 realmId;

        /**
         * @brief 启用状态标志
         *
         * 控制附加器是否处于活动状态。
         * - true: 附加器处于启用状态，日志将被写入数据库
         * - false: 附加器处于禁用状态，日志将被忽略
         */
        bool enabled;

        /**
         * @brief 写入日志消息到数据库
         *
         * 将指定的日志消息写入数据库表中。这是实际执行日志写入操作的核心方法。
         *
         * @param message 要写入的日志消息对象指针，包含日志内容、级别、时间戳等信息
         * @note 重写自 Appender 基类，实现了具体的数据库写入逻辑
         */
        void _write(LogMessage const* message) override;
};

#endif
