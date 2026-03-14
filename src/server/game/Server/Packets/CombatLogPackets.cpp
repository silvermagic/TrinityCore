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
 * @file CombatLogPackets.cpp
 * @brief 战斗日志数据包实现文件
 *
 * 实现战斗日志相关数据包的序列化功能,将战斗数据转换为网络字节流,
 * 用于服务器向客户端发送战斗日志信息。
 *
 * @module CombatLogPackets
 * @author TrinityCore Team
 */

#include "CombatLogPackets.h"

/**
 * @brief 序列化环境伤害日志数据包
 *
 * 将环境伤害的详细信息按照客户端协议格式序列化为网络字节流。
 * 数据包格式遵循 World of Warcraft 3.3.5 版本的协议规范。
 *
 * @return WorldPacket const* 指向序列化完成的数据包指针
 *
 * @note 序列化数据结构:
 *       - Victim: ObjectGuid (8字节) - 受害者的 GUID
 *       - Type: uint8 (1字节) - 环境伤害类型枚举值
 *       - Amount: uint32 (4字节) - 实际伤害数值
 *       - Resisted: uint32 (4字节) - 抵抗的伤害值
 *       - Absorbed: uint32 (4字节) - 吸收的伤害值
 *
 * @warning 数据包的字节序为小端序(Little-Endian),与网络协议标准一致
 *
 * @performance 此函数在玩家受到环境伤害时调用,频率较高但操作简单,
 *              主要是内存拷贝操作,性能影响可忽略
 *
 * @see Player::EnvironmentalDamage() 主要调用点
 * @see EnviromentalDamage 环境伤害类型定义
 */
WorldPacket const* WorldPackets::CombatLog::EnvironmentalDamageLog::Write()
{
    // 写入受害者 GUID,用于客户端识别受到伤害的单位
    _worldPacket << Victim;

    // 写入环境伤害类型(转换为 uint8),客户端据此播放不同的视觉效果
    _worldPacket << uint8(Type);

    // 写入实际伤害数值,客户端显示红色伤害数字
    _worldPacket << uint32(Amount);

    // 写入抵抗的伤害值,可能用于客户端显示抗性效果
    _worldPacket << uint32(Resisted);

    // 写入吸收的伤害值,客户端显示吸收效果(如护盾吸收)
    _worldPacket << uint32(Absorbed);

    // 返回序列化完成的数据包指针,准备发送
    return &_worldPacket;
}
