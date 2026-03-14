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
 * @file LFGPackets.h
 * @brief 寻求组队系统（LFG）网络数据包定义
 *
 * 本模块定义了地下城查找器（Looking For Group）系统的客户端到服务器数据包结构。
 * 主要功能包括：
 * - 玩家加入 LFG 队列的请求处理
 * - 玩家离开 LFG 队列的请求处理
 * - LFG 相关参数和偏好设置的数据传输
 *
 * LFG 系统允许玩家跨服组队完成地下城副本，是 WoW 3.3 版本的重要功能。
 */

#ifndef LFGPackets_h__
#define LFGPackets_h__

#include "Packet.h"
#include "PacketUtilities.h"

namespace WorldPackets::LFG
{
    /**
     * @class LFGJoin
     * @brief 玩家加入地下城查找器队列的数据包
     *
     * 当玩家点击"寻求组队"按钮时，客户端发送此数据包到服务器。
     * 包含玩家的角色定位、选择的地下城、偏好设置等信息。
     *
     * 继承自 ClientPacket，表示这是客户端发起的请求。
     */
    class LFGJoin final : public ClientPacket
    {
    public:
        /**
         * @brief 构造函数
         * @param packet 原始网络数据包
         *
         * 使用移动语义接收数据包，避免数据拷贝，提升性能。
         */
        LFGJoin(WorldPacket&& packet) : ClientPacket(CMSG_LFG_JOIN, std::move(packet)) { }

        /**
         * @brief 读取数据包内容
         *
         * 从网络数据包中反序列化所有字段。
         * 服务器在网络线程中调用此方法解析客户端请求。
         *
         * @note 此方法在接收到 CMSG_LFG_JOIN 消息时自动调用
         * @warning 必须在数据包有效时调用，否则可能读取越界
         */
        void Read() override;

        uint32 Roles = 0;                   ///< 角色位掩码：坦克(0x01)、治疗(0x02)、输出(0x04)等
        Array<uint32, 50> Slots;            ///< 选择的地下城槽位ID列表，最多50个
        std::string Comment;                ///< 玩家备注信息，显示给其他队友
        bool NoPartialClear = false;        ///< 是否拒绝未完成的副本（true=只要全新副本）
        bool Achievements = false;          ///< 是否要求队友完成特定成就
        std::array<uint8, 3> Needs = { };   ///< 需求类型数组（客户端硬编码为3个元素）
    };

    /**
     * @class LFGLeave
     * @brief 玩家离开地下城查找器队列的数据包
     *
     * 当玩家取消寻求组队或关闭 LFG 窗口时，客户端发送此数据包。
     * 该数据包不包含额外数据，仅通过消息类型标识意图。
     *
     * 继承自 ClientPacket，表示这是客户端发起的请求。
     */
    class LFGLeave final : public ClientPacket
    {
    public:
        /**
         * @brief 构造函数
         * @param packet 原始网络数据包
         *
         * 使用移动语义接收数据包，避免数据拷贝。
         */
        LFGLeave(WorldPacket&& packet) : ClientPacket(CMSG_LFG_LEAVE, std::move(packet)) { }

        /**
         * @brief 读取数据包内容
         *
         * 空实现，因为 LFGLeave 数据包不携带额外数据。
         * 服务器仅需识别消息类型即可处理离开请求。
         *
         * @note 重写基类方法，但不执行任何操作
         */
        void Read() override { };
    };
}

#endif // LFGPackets_h__
