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
 * @file Logger.h
 * @brief 日志记录器头文件
 *
 * 本文件定义了Logger类，负责管理特定类型的日志记录和分发。
 * Logger是日志系统的中间层，连接Log单例和各个Appender。
 *
 * 主要职责：
 *   - 管理特定日志类型的配置（名称、级别）
 *   - 维护关联的Appender列表
 *   - 将日志消息分发给所有关联的Appender
 *   - 提供日志级别过滤功能
 *
 * 层级结构：
 *   Logger支持层级命名，如"entities.player.dump"：
 *   - 如果未找到精确匹配，会向上查找"entities.player"
 *   - 如果还未找到，继续查找"entities"
 *   - 最终回退到"root"日志器
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "Define.h"
#include "LogCommon.h"
#include <unordered_map>
#include <string>

class Appender;
struct LogMessage;

/**
 * @brief 日志记录器类
 *
 * Logger管理特定类型日志的输出配置，包括日志级别和关联的Appender列表。
 * 每条日志消息都会根据其类型找到对应的Logger，然后由Logger分发给所有Appender。
 *
 * 日志类型命名规则：
 *   - root: 根日志器，作为所有未匹配日志的默认处理器
 *   - server: 服务器核心日志
 *   - entities.player: 玩家相关日志
 *   - entities.player.dump: 玩家数据转储日志
 *   - network: 网络相关日志
 *
 * 层级查找机制：
 *   当请求"entities.player.dump"类型的日志时：
 *   1. 首先查找精确匹配的Logger
 *   2. 如果未找到，查找"entities.player"
 *   3. 如果还未找到，查找"entities"
 *   4. 最终回退到"root"
 *
 * 配置示例：
 *   Logger.root=6,Console Server
 *   Logger.server=5,Console Server
 *   Logger.entities.player=3,PlayerFile
 */
class TC_COMMON_API Logger
{
    public:
        /**
         * @brief 构造函数
         *
         * @param name  日志器名称，用于标识日志类型
         * @param level 日志级别，低于此级别的消息将被忽略
         */
        Logger(std::string const& name, LogLevel level);

        /**
         * @brief 添加Appender到日志器
         *
         * @param type     Appender的ID
         * @param appender Appender对象指针（不获取所有权）
         *
         * 同一个Appender可以被多个Logger共享
         */
        void addAppender(uint8 type, Appender* appender);

        /**
         * @brief 从日志器移除Appender
         *
         * @param type 要移除的Appender ID
         */
        void delAppender(uint8 type);

        /**
         * @brief 获取日志器名称
         * @return 日志器名称的常量引用
         */
        std::string const& getName() const;

        /**
         * @brief 获取日志级别
         * @return 当前设置的日志级别
         */
        LogLevel getLogLevel() const;

        /**
         * @brief 设置日志级别
         * @param level 新的日志级别
         */
        void setLogLevel(LogLevel level);

        /**
         * @brief 写入日志消息
         *
         * @param message 日志消息指针
         *
         * 主要流程：
         *   1. 检查日志级别是否满足
         *   2. 检查消息内容是否为空
         *   3. 遍历所有关联的Appender，调用其write方法
         */
        void write(LogMessage* message) const;

    private:
        /// 日志器名称，用于标识日志类型
        std::string name;
        /// 日志级别，低于此级别的消息将被过滤
        LogLevel level;
        /// Appender映射表，ID -> Appender指针
        /// 注意：Logger不拥有Appender的所有权，由Log单例管理
        std::unordered_map<uint8, Appender*> appenders;
};

#endif
