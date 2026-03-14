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
 * @file TotemPackets.h
 * @brief 图腾系统网络数据包定义
 *
 * 本文件定义了图腾(Totem)相关的客户端和服务端网络数据包结构。
 * 图腾是萨满职业的特殊召唤物,包括火图腾、水图腾、空气图腾和土图腾四种类型。
 *
 * 主要功能:
 * - 定义图腾创建通知数据包(TotemCreated)
 * - 定义图腾销毁请求数据包(TotemDestroyed)
 *
 * 数据流向:
 * - 客户端 -> 服务端: TotemDestroyed (玩家主动召回图腾)
 * - 服务端 -> 客户端: TotemCreated (图腾被成功召唤)
 */

#ifndef TotemPackets_h__
#define TotemPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

namespace WorldPackets
{
    /**
     * @namespace Totem
     * @brief 图腾相关网络数据包命名空间
     *
     * 包含图腾创建、销毁等操作的网络消息定义
     */
    namespace Totem
    {
        /**
         * @class TotemDestroyed
         * @brief 图腾销毁请求包(客户端 -> 服务端)
         *
         * 继承自 ClientPacket,用于处理玩家主动召回图腾的请求。
         * 当玩家右键点击图腾buff图标选择取消时,客户端发送此数据包。
         *
         * 调用时机:
         * - 玩家主动召回图腾
         * - 召回特定槽位的图腾
         *
         * 性能注意事项:
         * - 数据包体积小(仅1字节),网络开销可忽略
         * - 处理逻辑简单,不影响服务器性能
         */
        class TotemDestroyed final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界数据包对象(右值引用)
             *
             * 初始化客户端数据包,设置操作码为 CMSG_TOTEM_DESTROYED
             */
            TotemDestroyed(WorldPacket&& packet) : ClientPacket(CMSG_TOTEM_DESTROYED, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络数据包中读取图腾槽位信息
             * 数据格式: uint8 Slot (图腾槽位索引)
             */
            void Read() override;

            uint8 Slot = 0;  ///< 图腾槽位索引(0-3: 分别对应火、水、空气、土图腾)
        };

        /**
         * @class TotemCreated
         * @brief 图腾创建通知包(服务端 -> 客户端)
         *
         * 继承自 ServerPacket,用于通知客户端图腾已被成功召唤。
         * 服务端在图腾实体创建完成后发送此数据包,客户端收到后会显示图腾UI和持续时间。
         *
         * 调用时机:
         * - 萨满施放图腾法术成功后
         * - 图腾实体在游戏中生成后立即发送
         *
         * 性能注意事项:
         * - 数据包大小固定(17字节: 1+8+4+4)
         * - 每个图腾召唤时发送一次,频率不高
         */
        class TotemCreated final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化服务端数据包,设置操作码为 SMSG_TOTEM_CREATED
             * 并预分配数据包缓冲区大小(1字节槽位 + 8字节GUID + 4字节持续时间 + 4字节法术ID)
             */
            TotemCreated() : ServerPacket(SMSG_TOTEM_CREATED, 1 + 8 + 4 + 4) { }

            /**
             * @brief 写入数据包内容
             * @return 返回构造完成的世界数据包指针
             *
             * 将图腾信息写入网络数据包,发送给客户端
             * 数据格式: uint8 Slot + ObjectGuid Totem + uint32 Duration + uint32 SpellID
             */
            WorldPacket const* Write() override;

            uint8 Slot = 0;           ///< 图腾槽位索引(0-3: 分别对应火、水、空气、土图腾)
            ObjectGuid Totem;         ///< 图腾实体的全局唯一标识符
            uint32 Duration = 0;      ///< 图腾持续时间(毫秒),0表示永久存在
            uint32 SpellID = 0;       ///< 召唤图腾的法术ID

        };
    }
}

#endif // TotemPackets_h__
