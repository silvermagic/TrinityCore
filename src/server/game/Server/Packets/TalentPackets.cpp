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
 * @file TalentPackets.cpp
 * @brief 天赋系统网络数据包实现
 *
 * 本文件实现了天赋重置相关数据包的序列化和反序列化功能,
 * 负责在服务器和客户端之间传输天赋重置信息。
 *
 * 主要功能:
 * - 天赋重置确认信息的数据包序列化(服务器->客户端)
 * - 天赋重置确认请求的数据包反序列化(客户端->服务器)
 * - 强制天赋重置通知的数据包序列化(服务器->客户端)
 */

#include "TalentPackets.h"

namespace WorldPackets::Talents
{
/**
 * @brief 序列化天赋重置确认数据包
 *
 * 将天赋重置确认信息序列化为网络数据包,发送给客户端。
 * 数据包格式: [ObjectGuid RespecMaster][uint32 Cost]
 *
 * @return 返回序列化完成的数据包指针
 *
 * @note 数据包大小固定为 8 + 4 = 12 字节
 * @note 此函数在服务器准备发送天赋重置确认时调用
 */
WorldPacket const* RespecWipeConfirm::Write()
{
    // 写入负责天赋重置的NPC的GUID
    _worldPacket << RespecMaster;

    // 写入重置费用(铜币)
    _worldPacket << uint32(Cost);

    return &_worldPacket;
}

/**
 * @brief 反序列化天赋重置确认请求
 *
 * 从客户端发送的数据包中解析天赋重置确认信息。
 * 数据包格式: [ObjectGuid RespecMaster]
 *
 * @note 此函数在服务器接收到玩家的天赋重置确认时调用
 * @note 客户端仅返回NPC的GUID,服务器需要验证该NPC的有效性
 */
void ConfirmRespecWipe::Read()
{
    // 读取玩家确认要与之交互的天赋重置NPC的GUID
    _worldPacket >> RespecMaster;
}

/**
 * @brief 序列化强制天赋重置通知
 *
 * 将强制天赋重置通知序列化为网络数据包,发送给客户端。
 * 数据包格式: [uint8 IsPetTalents]
 *
 * @return 返回序列化完成的数据包指针
 *
 * @note 数据包大小固定为 1 字节
 * @note 此函数在服务器需要强制重置玩家或宠物天赋时调用
 * @note 常见触发场景:版本更新、天赋树重构、游戏机制调整
 */
WorldPacket const* InvoluntarilyReset::Write()
{
    // 写入天赋类型标志(0=玩家天赋, 1=宠物天赋)
    _worldPacket << uint8(IsPetTalents);

    return &_worldPacket;
}
}
