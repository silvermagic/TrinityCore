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
 * @file SystemPackets.h
 * @brief 系统功能状态数据包定义
 *
 * 本文件定义了与游戏系统功能相关的网络数据包结构,主要用于通知客户端各种系统功能的状态。
 * 包含投诉系统和语音聊天系统的状态信息。
 *
 * 这些数据包在玩家登录时发送,确保客户端了解服务器支持的功能特性。
 */

#ifndef SystemPackets_h__
#define SystemPackets_h__

#include "Packet.h"
#include "SharedDefines.h"

namespace WorldPackets
{
    /**
     * @namespace System
     * @brief 系统功能相关数据包命名空间
     *
     * 包含系统级别功能状态的数据包定义,如投诉功能、语音聊天等。
     */
    namespace System
    {
        /**
         * @class FeatureSystemStatus
         * @brief 特性系统状态数据包
         *
         * 服务器端数据包,用于向客户端发送各种游戏特性功能的状态信息。
         * 主要包括投诉功能和语音聊天功能的启用状态。
         *
         * 继承关系:
         * - 继承自 ServerPacket (服务器发送给客户端的数据包基类)
         *
         * 使用场景:
         * - 玩家登录角色进入游戏时发送(CharacterHandler.cpp:SendFeatureSystemStatus)
         * - 确保客户端了解服务器支持哪些功能特性
         *
         * 数据包结构:
         * - uint8 ComplaintStatus: 投诉功能状态
         * - uint8 VoiceEnabled: 语音功能是否启用
         */
        class FeatureSystemStatus final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化特性系统状态数据包,设置操作码为 SMSG_FEATURE_SYSTEM_STATUS,
             * 预留数据包大小为 2 字节(两个 uint8 字段)。
             */
            FeatureSystemStatus() : ServerPacket(SMSG_FEATURE_SYSTEM_STATUS, 2) { }

            /**
             * @brief 序列化数据包
             *
             * 将特性系统状态数据写入世界数据包,准备发送给客户端。
             *
             * @return 返回已填充数据的 WorldPacket 指针,供网络层发送
             *
             * @note 性能说明:
             * - 简单的内存写入操作,性能开销极小
             * - 仅写入 2 字节数据,网络带宽占用可忽略
             */
            WorldPacket const* Write() override;

            /**
             * @var ComplaintStatus
             * @brief 投诉功能状态
             *
             * 指示客户端投诉功能的启用状态,可能的值参见 ComplaintStatus 枚举:
             * - COMPLAINT_DISABLED (0): 投诉功能禁用
             * - COMPLAINT_ENABLED_WITHOUT_AUTO_IGNORE (1): 启用投诉但不自动忽略被投诉者
             * - COMPLAINT_ENABLED_WITH_AUTO_IGNORE (2): 启用投诉并自动忽略被投诉者
             *
             * 默认值: COMPLAINT_ENABLED_WITH_AUTO_IGNORE
             * 默认配置下启用投诉功能,投诉后会自动将被投诉者加入忽略列表。
             */
            uint8 ComplaintStatus = COMPLAINT_ENABLED_WITH_AUTO_IGNORE;

            /**
             * @var VoiceEnabled
             * @brief 语音聊天功能启用标志
             *
             * 指示服务器是否启用了内置语音聊天功能。
             * - true: 语音聊天功能已启用
             * - false: 语音聊天功能未启用
             *
             * 默认值: false
             * TrinityCore 默认不启用语音聊天功能。
             */
            bool VoiceEnabled = false;
        };
    }
}

#endif // SystemPackets_h__
