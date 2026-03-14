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
 * @file TalentPackets.h
 * @brief 天赋系统网络数据包定义
 *
 * 本文件定义了天赋重置相关的网络数据包结构,包括:
 * - 天赋重置确认请求和响应
 * - 强制天赋重置通知
 *
 * 这些数据包用于玩家与服务器之间的天赋重置交互,
 * 支持玩家天赋和宠物天赋两种场景。
 */

#ifndef TalentPackets_h__
#define TalentPackets_h__

#include "ObjectGuid.h"
#include "Packet.h"

namespace WorldPackets::Talents
{
/**
 * @class RespecWipeConfirm
 * @brief 天赋重置确认数据包(服务器发送)
 *
 * 继承自 ServerPacket,用于服务器向客户端发送天赋重置确认信息。
 * 当玩家请求重置天赋时,服务器会发送此数据包,包含重置费用和NPC信息。
 * 客户端收到后会显示确认对话框,等待玩家确认是否支付费用进行重置。
 */
class RespecWipeConfirm final : public ServerPacket
{
public:
    /**
     * @brief 构造函数
     * @param respecMaster 负责天赋重置的NPC的GUID
     * @param cost 重置天赋所需的费用(铜币)
     *
     * 初始化服务器数据包,设置数据包大小为 8 + 4 字节(ObjectGuid + uint32)
     */
    explicit RespecWipeConfirm(ObjectGuid respecMaster, uint32 cost)
        : ServerPacket(MSG_TALENT_WIPE_CONFIRM, 8 + 4), RespecMaster(respecMaster), Cost(cost) { }

    /**
     * @brief 序列化数据包
     * @return 返回序列化后的数据包指针
     *
     * 将 RespecMaster 和 Cost 写入数据包缓冲区
     */
    WorldPacket const* Write() override;

    ObjectGuid RespecMaster;  ///< 负责天赋重置的NPC的GUID(如职业训练师)
    uint32 Cost = 0;          ///< 重置天赋的费用(以铜币为单位)
};

/**
 * @class ConfirmRespecWipe
 * @brief 确认天赋重置数据包(客户端发送)
 *
 * 继承自 ClientPacket,用于客户端向服务器确认天赋重置操作。
 * 当玩家在确认对话框中点击确认后,客户端发送此数据包,
 * 服务器收到后执行实际的天赋重置操作。
 */
class ConfirmRespecWipe final : public ClientPacket
{
public:
    /**
     * @brief 构造函数
     * @param packet 从网络接收的原始数据包
     *
     * 初始化客户端数据包,用于解析天赋重置确认信息
     */
    explicit ConfirmRespecWipe(WorldPacket&& packet) : ClientPacket(MSG_TALENT_WIPE_CONFIRM, std::move(packet)) { }

    /**
     * @brief 反序列化数据包
     *
     * 从数据包缓冲区读取 RespecMaster 信息
     */
    void Read() override;

    ObjectGuid RespecMaster;  ///< 玩家确认要与之交互的天赋重置NPC的GUID
};

/**
 * @class InvoluntarilyReset
 * @brief 强制天赋重置通知数据包(服务器发送)
 *
 * 继承自 ServerPacket,用于服务器通知客户端天赋被强制重置。
 * 当玩家的天赋因为某些特殊原因被服务器强制清除时发送,
 * 例如:版本更新导致天赋树变更、游戏机制调整等。
 * 支持玩家天赋和宠物天赋两种场景。
 */
class InvoluntarilyReset final : public ServerPacket
{
public:
    /**
     * @brief 构造函数
     * @param isPetTalents 是否为宠物天赋重置
     *                     - true: 宠物天赋被重置
     *                     - false: 玩家天赋被重置
     *
     * 初始化服务器数据包,设置数据包大小为 1 字节(uint8)
     */
    explicit InvoluntarilyReset(bool isPetTalents) : ServerPacket(SMSG_TALENTS_INVOLUNTARILY_RESET, 1), IsPetTalents(isPetTalents ? 1 : 0) { }

    /**
     * @brief 序列化数据包
     * @return 返回序列化后的数据包指针
     *
     * 将 IsPetTalents 标志写入数据包缓冲区
     */
    WorldPacket const* Write() override;

    uint8 IsPetTalents = 0;  ///< 标识是否为宠物天赋重置(0=玩家天赋, 1=宠物天赋)
};
}

#endif // TalentPackets_h__
