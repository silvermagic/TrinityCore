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
 * @file BankPackets.h
 * @brief 银行系统网络包定义文件
 *
 * 本文件定义了银行系统相关的所有客户端和服务器端网络包结构。
 * 主要功能包括:
 *   - 玩家物品存入/取出银行的自动操作包
 *   - 购买额外银行槽位的请求和响应包
 *   - 打开银行界面的通知包
 *
 * 银行系统允许玩家存储物品,玩家可以购买额外的银行槽位来扩展存储空间。
 * 所有包结构都继承自 ClientPacket 或 ServerPacket 基类,实现序列化和反序列化。
 */

#ifndef BankPackets_h__
#define BankPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

namespace WorldPackets
{
    namespace Bank
    {
        /**
         * @class AutoBankItem
         * @brief 自动存入银行物品的客户端包
         *
         * 当玩家使用 Shift+右键点击物品时,客户端发送此包请求自动将该物品存入银行。
         * 服务器会查找银行中合适的空位并移动物品。
         *
         * 继承自 ClientPacket,表示这是一个从客户端发送到服务器的包。
         */
        class AutoBankItem final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据,通过右值引用转移所有权
             *
             * 初始化包类型为 CMSG_AUTOBANK_ITEM (客户端消息:自动银行物品)
             */
            AutoBankItem(WorldPacket&& packet) : ClientPacket(CMSG_AUTOBANK_ITEM, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中读取物品的位置信息(Bag和Slot)。
             * 调用时机:当服务器收到 CMSG_AUTOBANK_ITEM 消息时。
             */
            void Read() override;

            uint8 Bag = 0;  ///< 物品所在的背包编号 (0=主背包,1-4=额外背包)
            uint8 Slot = 0; ///< 物品在背包中的槽位编号 (0-35,取决于背包大小)
        };

        /**
         * @class AutoStoreBankItem
         * @brief 自动从银行取出物品到背包的客户端包
         *
         * 当玩家在银行界面中使用 Shift+右键点击银行中的物品时,
         * 客户端发送此包请求自动将该物品存入玩家背包。
         * 服务器会查找背包中合适的空位并移动物品。
         *
         * 继承自 ClientPacket,表示这是一个从客户端发送到服务器的包。
         */
        class AutoStoreBankItem final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据,通过右值引用转移所有权
             *
             * 初始化包类型为 CMSG_AUTOSTORE_BANK_ITEM (客户端消息:自动存储银行物品)
             */
            AutoStoreBankItem(WorldPacket&& packet) : ClientPacket(CMSG_AUTOSTORE_BANK_ITEM, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中读取物品在银行中的位置信息(Bag和Slot)。
             * 调用时机:当服务器收到 CMSG_AUTOSTORE_BANK_ITEM 消息时。
             */
            void Read() override;

            uint8 Bag = 0;  ///< 物品所在的银行背包编号 (通常是银行页签编号)
            uint8 Slot = 0; ///< 物品在银行背包中的槽位编号
        };

        /**
         * @class BuyBankSlot
         * @brief 购买银行槽位的客户端包
         *
         * 当玩家在银行界面点击购买额外槽位按钮时,客户端发送此包。
         * 玩家可以购买额外的银行槽位来扩展存储空间,每次购买费用递增。
         *
         * 继承自 ClientPacket,表示这是一个从客户端发送到服务器的包。
         */
        class BuyBankSlot final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据,通过右值引用转移所有权
             *
             * 初始化包类型为 CMSG_BUY_BANK_SLOT (客户端消息:购买银行槽位)
             */
            BuyBankSlot(WorldPacket&& packet) : ClientPacket(CMSG_BUY_BANK_SLOT, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中读取银行管理员NPC的GUID。
             * 调用时机:当服务器收到 CMSG_BUY_BANK_SLOT 消息时。
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行管理员NPC的唯一标识符,用于验证玩家与NPC的距离和交互合法性
        };

        /**
         * @class BuyBankSlotResult
         * @brief 购买银行槽位结果的服务器包
         *
         * 服务器处理购买银行槽位请求后,向客户端发送此包通知购买结果。
         * Result字段指示购买是否成功或失败原因(如金币不足、已达上限等)。
         *
         * 继承自 ServerPacket,表示这是一个从服务器发送到客户端的包。
         */
        class BuyBankSlotResult final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化包类型为 SMSG_BUY_BANK_SLOT_RESULT (服务器消息:购买银行槽位结果)
             * 包大小固定为4字节(一个uint32)
             */
            BuyBankSlotResult() : ServerPacket(SMSG_BUY_BANK_SLOT_RESULT, 4) { }

            /**
             * @brief 写入包数据
             * @return 返回写入完成后的世界包常量指针
             *
             * 将购买结果编码到网络包中,准备发送给客户端。
             * 调用时机:当服务器处理完购买银行槽位请求后,向客户端发送结果时。
             */
            WorldPacket const* Write() override;

            uint32 Result = 0; ///< 购买结果码 (0=成功,其他值表示各种失败原因)
        };

        /**
         * @class ShowBank
         * @brief 显示银行界面的服务器包
         *
         * 当玩家与银行管理员NPC交互时,服务器发送此包通知客户端打开银行界面。
         * 客户端收到此包后会显示银行窗口,允许玩家存取物品。
         *
         * 继承自 ServerPacket,表示这是一个从服务器发送到客户端的包。
         */
        class ShowBank final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化包类型为 SMSG_SHOW_BANK (服务器消息:显示银行)
             * 包大小固定为8字节(ObjectGuid的大小)
             */
            ShowBank() : ServerPacket(SMSG_SHOW_BANK, 8) { }

            /**
             * @brief 写入包数据
             * @return 返回写入完成后的世界包常量指针
             *
             * 将银行管理员NPC的GUID写入网络包,准备发送给客户端。
             * 调用时机:当玩家与银行管理员NPC交互成功后,服务器通知客户端打开银行界面时。
             */
            WorldPacket const* Write() override;

            ObjectGuid Banker; ///< 银行管理员NPC的唯一标识符,客户端用于显示正确的银行窗口
        };
    }
}
#endif // BankPackets_h__
