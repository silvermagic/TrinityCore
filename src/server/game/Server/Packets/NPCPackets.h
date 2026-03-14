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
 * @file NPCPackets.h
 * @brief NPC 交互数据包定义模块
 *
 * 本文件定义了与 NPC 交互相关的网络数据包结构,包括:
 * - 与各种 NPC 的打招呼交互(银行家、战场军官、绑定者、商人、训练师等)
 * - 训练师功能(查看可学法术列表、购买法术)
 * - 训练师购买结果的反馈(成功或失败)
 *
 * 这些数据包用于客户端与服务器之间的 NPC 交互通信。
 */

#ifndef NPCPackets_h__
#define NPCPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"
#include <array>

namespace WorldPackets
{
    namespace NPC
    {
        /**
         * @class Hello
         * @brief NPC 打招呼客户端数据包
         *
         * 用于多种 NPC 类型的交互请求,包括:
         * - CMSG_BANKER_ACTIVATE: 激活银行家 NPC
         * - CMSG_BATTLEMASTER_HELLO: 与战场军官交互
         * - CMSG_BINDER_ACTIVATE: 激活绑定者(旅店老板)
         * - CMSG_GOSSIP_HELLO: NPC 对话交互
         * - CMSG_LIST_INVENTORY: 查看商人商品列表
         * - CMSG_TRAINER_LIST: 查看训练师技能列表
         *
         * 继承自 ClientPacket,表示这是一个客户端发送到服务器的数据包。
         */
        class Hello final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界数据包的右值引用
             */
            Hello(WorldPacket&& packet) : ClientPacket(std::move(packet)) { }

            /**
             * @brief 从数据包中读取数据
             *
             * 从网络数据包中读取 NPC 的 GUID 标识符。
             */
            void Read() override;

            ObjectGuid Unit;  ///< 目标 NPC 的全局唯一标识符
        };

        /**
         * @struct TrainerListSpell
         * @brief 训练师技能列表中的单个技能信息
         *
         * 用于描述训练师可以教授的每个技能的详细信息,
         * 包括技能ID、价格、等级要求、技能要求等。
         */
        struct TrainerListSpell
        {
            int32 SpellID          = 0;                  ///< 法术/技能ID
            uint8 Usable           = 0;                  ///< 是否可学习状态(0=不可用, 1=可学习, 2=已学会)
            int32 MoneyCost        = 0;                  ///< 学习该技能需要的金币数量(铜币单位)
            std::array<int32, 2> PointCost = { };        ///< 天赋点消耗(与 Lua 中的 PLAYER_CHARACTER_POINTS 比较)
            uint8 ReqLevel         = 0;                  ///< 学习该技能需要的最低角色等级
            int32 ReqSkillLine     = 0;                  ///< 学习该技能需要的专业技能ID
            int32 ReqSkillRank     = 0;                  ///< 学习该技能需要的专业技能等级
            std::array<int32, 3> ReqAbility = { };       ///< 学习该技能需要的前置技能ID列表(最多3个)
        };

        /**
         * @class TrainerList
         * @brief 训练师技能列表服务器数据包
         *
         * 用于向客户端发送训练师可教授的技能列表。
         * 当玩家与训练师交互时,服务器会发送此数据包。
         *
         * 继承自 ServerPacket,表示这是一个服务器发送到客户端的数据包。
         */
        class TrainerList final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化为 SMSG_TRAINER_LIST 类型的服务器数据包
             */
            TrainerList() : ServerPacket(SMSG_TRAINER_LIST) { }

            /**
             * @brief 将数据写入数据包
             * @return 返回写入完成后的数据包常量指针
             *
             * 将训练师的 GUID、类型、技能列表和问候语写入数据包,
             * 供网络层发送给客户端。
             */
            WorldPacket const* Write() override;

            ObjectGuid TrainerGUID;                      ///< 训练师的 GUID
            int32 TrainerType = 0;                       ///< 训练师类型(职业训练师、专业技能训练师等)
            std::vector<TrainerListSpell> Spells;        ///< 可学习的技能列表
            std::string Greeting;                        ///< 训练师的问候语
        };

        /**
         * @class TrainerBuySpell
         * @brief 购买训练师技能客户端数据包
         *
         * 当玩家在训练师界面点击学习某个技能时,客户端发送此数据包到服务器,
         * 请求从指定训练师处购买指定技能。
         *
         * 继承自 ClientPacket,表示这是一个客户端发送到服务器的数据包。
         */
        class TrainerBuySpell final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界数据包的右值引用
             *
             * 初始化为 CMSG_TRAINER_BUY_SPELL 类型的客户端数据包
             */
            TrainerBuySpell(WorldPacket&& packet) : ClientPacket(CMSG_TRAINER_BUY_SPELL, std::move(packet)) { }

            /**
             * @brief 从数据包中读取数据
             *
             * 从网络数据包中读取训练师的 GUID 和要购买的技能ID。
             */
            void Read() override;

            ObjectGuid TrainerGUID;  ///< 训练师的 GUID
            int32 SpellID = 0;       ///< 要购买的技能ID
        };

        /**
         * @class TrainerBuyFailed
         * @brief 训练师技能购买失败服务器数据包
         *
         * 当玩家尝试从训练师购买技能失败时,服务器发送此数据包通知客户端。
         * 包含失败原因,如金币不足、等级不够、缺少前置技能等。
         *
         * 继承自 ServerPacket,表示这是一个服务器发送到客户端的数据包。
         */
        class TrainerBuyFailed final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化为 SMSG_TRAINER_BUY_FAILED 类型的服务器数据包
             * 数据包大小为 8(GUID) + 4(技能ID) + 4(失败原因) = 16 字节
             */
            TrainerBuyFailed() : ServerPacket(SMSG_TRAINER_BUY_FAILED, 8 + 4 + 4) { }

            /**
             * @brief 将数据写入数据包
             * @return 返回写入完成后的数据包常量指针
             *
             * 将训练师 GUID、技能ID 和失败原因写入数据包,
             * 供网络层发送给客户端。
             */
            WorldPacket const* Write() override;

            ObjectGuid TrainerGUID;                  ///< 训练师的 GUID
            int32 SpellID             = 0;           ///< 购买失败的技能ID
            int32 TrainerFailedReason = 0;           ///< 失败原因代码
        };

        /**
         * @class TrainerBuySucceeded
         * @brief 训练师技能购买成功服务器数据包
         *
         * 当玩家从训练师成功购买技能后,服务器发送此数据包通知客户端,
         * 确认技能购买成功,客户端可以更新 UI 显示。
         *
         * 继承自 ServerPacket,表示这是一个服务器发送到客户端的数据包。
         */
        class TrainerBuySucceeded final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化为 SMSG_TRAINER_BUY_SUCCEEDED 类型的服务器数据包
             * 数据包大小为 8(GUID) + 4(技能ID) = 12 字节
             */
            TrainerBuySucceeded() : ServerPacket(SMSG_TRAINER_BUY_SUCCEEDED, 8 + 4) { }

            /**
             * @brief 将数据写入数据包
             * @return 返回写入完成后的数据包常量指针
             *
             * 将训练师 GUID 和技能ID 写入数据包,
             * 供网络层发送给客户端。
             */
            WorldPacket const* Write() override;

            ObjectGuid TrainerGUID;  ///< 训练师的 GUID
            int32 SpellID = 0;       ///< 成功购买的技能ID
        };
    }
}

#endif // NPCPackets_h__
