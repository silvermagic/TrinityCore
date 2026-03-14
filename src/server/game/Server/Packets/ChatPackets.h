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
 * @file ChatPackets.h
 * @brief 聊天系统网络包定义模块
 *
 * 本模块定义了聊天系统相关的网络数据包结构，主要包括：
 * - 表情动作包（Emote）：用于玩家角色表情动作的同步
 * - 聊天服务器消息包（ChatServerMessage）：用于服务器向客户端发送系统消息
 *
 * 这些数据包类继承自 WorldPackets::ServerPacket 和 WorldPackets::ClientPacket 基类，
 * 实现了客户端与服务器之间的聊天相关通信协议。
 *
 * 主要功能：
 * 1. 表情动作的客户端请求和服务器广播
 * 2. 服务器系统消息的推送
 *
 * 调用关系：
 * - 客户端发送 CMSG_EMOTE -> EmoteClient 包 -> WorldSession::HandleEmoteOpcode 处理
 * - 服务器发送 SMSG_EMOTE -> Emote 包 -> 广播给周围玩家
 * - 服务器发送 SMSG_CHAT_SERVER_MESSAGE -> ChatServerMessage 包 -> 显示系统消息
 */

#ifndef ChatPackets_h__
#define ChatPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

namespace WorldPackets
{
    /**
     * @brief 聊天系统包命名空间
     *
     * 包含所有与聊天、表情、频道通信相关的网络包类定义。
     */
    namespace Chat
    {
        /**
         * @brief 表情动作服务器包
         *
         * 继承自 ServerPacket，用于服务器向客户端发送表情动作通知。
         * 当玩家执行表情动作时，服务器通过此包广播给周围玩家，
         * 使其他玩家能看到该玩家的表情动作表现。
         *
         * 包结构：
         * - EmoteID (uint32): 表情动作ID，定义表情的具体类型
         * - Guid (ObjectGuid): 执行表情动作的单位GUID
         *
         * 使用场景：
         * - 玩家使用 /emote 命令
         * - NPC 或玩家执行预设表情
         * - 脚本触发表情动作
         *
         * 调用时机：
         * - WorldSession::HandleEmoteOpcode 处理客户端请求后广播
         * - 脚本调用 Unit::HandleEmoteCommand 时发送
         *
         * 性能注意事项：
         * - 初始包大小为 12 字节 (4字节 EmoteID + 8字节 Guid)
         * - 广播时会发送给视野内所有玩家，需注意玩家密度
         */
        class Emote final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包，设置操作码为 SMSG_EMOTE，预分配缓冲区大小为 12 字节。
             */
            Emote() : ServerPacket(SMSG_EMOTE, 4 + 8) { }

            /**
             * @brief 序列化表情动作数据到网络包
             *
             * @return 返回序列化完成的 WorldPacket 指针
             *
             * 将 EmoteID 和 Guid 写入数据包缓冲区，准备发送给客户端。
             */
            WorldPacket const* Write() override;

            uint32 EmoteID = 0;      ///< 表情动作ID，参考 Emotes.dbc 定义
            ObjectGuid Guid;         ///< 执行表情动作的单位（玩家或NPC）的GUID
        };

        /**
         * @brief 表情动作客户端包
         *
         * 继承自 ClientPacket，用于客户端向服务器发送表情动作请求。
         * 当玩家客户端触发表情动作时（如使用表情命令或快捷键），
         * 会发送此包到服务器，服务器验证后广播给周围玩家。
         *
         * 包结构：
         * - EmoteID (uint32): 请求的表情动作ID
         *
         * 使用场景：
         * - 玩家通过聊天框输入表情命令（如 /wave, /dance）
         * - 玩家点击表情动作按钮
         * - UI 脚本触发表情
         *
         * 调用时机：
         * - 客户端触发表情动作时发送
         * - 由 WorldSession::HandleEmoteOpcode 接收和处理
         *
         * 处理流程：
         * 1. 客户端发送 CMSG_EMOTE 包
         * 2. 服务器验证玩家状态（是否被沉默、是否死亡等）
         * 3. 服务器创建 Emote 包广播给周围玩家
         */
        class EmoteClient final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * @param packet 接收到的网络包数据（右值引用）
             *
             * 初始化客户端包，设置操作码为 CMSG_EMOTE，并移动包数据。
             */
            EmoteClient(WorldPacket&& packet) : ClientPacket(CMSG_EMOTE, std::move(packet)) { }

            /**
             * @brief 从网络包反序列化表情动作数据
             *
             * 从接收到的网络包中读取 EmoteID 数据。
             * 读取完成后，数据即可通过成员变量访问。
             */
            void Read() override;

            uint32 EmoteID = 0;      ///< 客户端请求的表情动作ID
        };

        /**
         * @brief 聊天服务器消息包
         *
         * 继承自 ServerPacket，用于服务器向客户端发送系统消息。
         * 此包通常用于显示服务器特定的消息，如服务器重启通知、
         * GM 公告、系统提示等需要国际化的消息。
         *
         * 包结构：
         * - MessageID (int32): 消息ID，对应数据库或配置中的消息模板
         * - StringParam (string): 消息参数，用于消息模板的动态替换
         *
         * 消息机制：
         * - MessageID 对应预定义的消息模板（通常在数据库或客户端定义）
         * - StringParam 提供动态参数，用于填充模板中的占位符
         * - 客户端根据 MessageID 查找本地化文本并替换参数显示
         *
         * 使用场景：
         * - 服务器维护公告
         * - GM 公告和通知
         * - 系统提示信息
         * - 需要国际化的服务器消息
         *
         * 调用时机：
         * - 服务器启动/关闭通知
         * - GM 执行公告命令
         * - 系统事件触发的消息
         *
         * 性能注意事项：
         * - 初始包大小为 24 字节（4字节 MessageID + 预估20字节字符串）
         * - StringParam 长度可变，需注意长字符串对网络带宽的影响
         * - 避免频繁发送大量玩家范围的此消息包
         */
        class ChatServerMessage final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包，设置操作码为 SMSG_CHAT_SERVER_MESSAGE，
             * 预分配缓冲区大小为 24 字节。
             */
            ChatServerMessage() : ServerPacket(SMSG_CHAT_SERVER_MESSAGE, 4 + 20) { }

            /**
             * @brief 序列化服务器消息数据到网络包
             *
             * @return 返回序列化完成的 WorldPacket 指针
             *
             * 将 MessageID 和 StringParam 写入数据包缓冲区，准备发送给客户端。
             */
            WorldPacket const* Write() override;

            int32 MessageID = 0;           ///< 消息模板ID，用于客户端查找本地化文本
            std::string StringParam;       ///< 消息参数，用于动态填充消息模板
        };
    }
}

#endif // ChatPackets_h__
