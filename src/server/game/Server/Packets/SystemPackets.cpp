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
 * @file SystemPackets.cpp
 * @brief 系统功能状态数据包实现
 *
 * 本文件实现了系统功能状态数据包的序列化方法,
 * 主要负责将特性系统状态信息转换为网络数据包格式。
 */

#include "SystemPackets.h"

/**
 * @brief 序列化特性系统状态数据包
 *
 * 将投诉功能状态和语音功能启用标志写入世界数据包。
 * 数据包格式:
 * - uint8 ComplaintStatus: 投诉功能状态码
 * - uint8 VoiceEnabled: 语音功能启用标志(0=禁用, 1=启用)
 *
 * @return 返回填充完成的 WorldPacket 常量指针
 *
 * @note 调用时机:
 * - 由 WorldSession::SendFeatureSystemStatus() 调用
 * - 在玩家登录进入游戏世界时发送
 *
 * @note 性能注意事项:
 * - 仅执行两次字节写入操作,性能开销极小
 * - 数据包大小固定为 2 字节,网络传输开销可忽略
 * - 不涉及内存分配或复杂计算
 *
 * @see WorldSession::SendFeatureSystemStatus
 */
WorldPacket const* WorldPackets::System::FeatureSystemStatus::Write()
{
    // 写入投诉功能状态码
    // 告知客户端投诉功能是否启用以及是否自动忽略被投诉者
    _worldPacket << uint8(ComplaintStatus);

    // 写入语音聊天功能启用标志
    // 告知客户端服务器是否支持内置语音聊天
    _worldPacket << uint8(VoiceEnabled);

    // 返回已填充的数据包,供网络层发送
    return &_worldPacket;
}
