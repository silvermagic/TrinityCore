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
 * @file CombatPackets.cpp
 * @brief 战斗系统网络包实现模块
 *
 * 本模块实现了战斗系统相关的网络消息包的序列化和反序列化功能,
 * 包括近战攻击、停止攻击、武器状态等消息的读写操作。
 *
 * 主要功能:
 * - 客户端数据包的读取和解析
 * - 服务器数据包的序列化和发送
 * - 战斗状态的同步和广播
 *
 * @see CombatPackets.h
 */

#include "CombatPackets.h"
#include "Unit.h"

/**
 * @brief 从网络包中读取攻击目标 GUID
 *
 * 从客户端发送的攻击请求包中提取攻击目标的 GUID。
 * 客户端发送此消息时已经确定了要攻击的目标对象。
 *
 * @note 此方法在服务器接收到 CMSG_ATTACK_SWING 消息时被调用。
 */
void WorldPackets::Combat::AttackSwing::Read()
{
    // 从数据包中读取目标的 GUID
    _worldPacket >> Victim;
}

/**
 * @brief 序列化攻击开始消息包
 * @return 返回序列化后的数据包指针
 *
 * 将攻击者和目标的完整 GUID 写入数据包,用于广播给周围玩家。
 * 客户端收到此消息后会显示攻击动画和战斗状态。
 *
 * 数据包格式:
 * - Attacker GUID (8字节)
 * - Victim GUID (8字节)
 *
 * @note 此方法在服务器需要通知客户端攻击开始时调用,
 *       例如怪物开始攻击玩家或玩家开始攻击怪物。
 */
WorldPacket const* WorldPackets::Combat::AttackStart::Write()
{
    // 写入攻击者的完整 GUID
    _worldPacket << Attacker;
    // 写入目标的完整 GUID
    _worldPacket << Victim;

    return &_worldPacket;
}

/**
 * @brief 从 Unit 对象构造攻击停止消息包
 * @param attacker 攻击者单位指针(不能为空)
 * @param victim 被攻击目标单位指针(可为空)
 *
 * 这是便捷构造函数,自动从 Unit 对象中提取 GUID 和死亡状态。
 * 如果目标存在,会自动检测目标是否已死亡并设置 NowDead 标志。
 *
 * @note 此构造函数推荐在服务器逻辑中使用,可避免手动设置成员变量。
 *       使用 PackedGuid 而非完整 GUID 来优化网络传输大小。
 *
 * @warning attacker 参数必须为有效指针,不能为空。
 */
WorldPackets::Combat::SAttackStop::SAttackStop(Unit const* attacker, Unit const* victim) : ServerPacket(SMSG_ATTACK_STOP, 8 + 8 + 4)
{
    // 获取攻击者的压缩 GUID,用于优化网络传输
    Attacker = attacker->GetPackGUID();

    // 如果目标存在,提取目标信息
    if (victim)
    {
        Victim = victim->GetPackGUID();
        // 检查目标是否已死亡,客户端会根据此标志播放不同的动画
        NowDead = victim->isDead();
    }
}

/**
 * @brief 序列化攻击停止消息包
 * @return 返回序列化后的数据包指针
 *
 * 将攻击者 GUID、目标 GUID 和死亡标志写入数据包,用于广播给周围玩家。
 * 客户端收到此消息后会停止攻击动画,并根据死亡标志播放相应效果。
 *
 * 数据包格式:
 * - Attacker PackedGuid (可变字节,通常 3-8 字节)
 * - Victim PackedGuid (可变字节,通常 3-8 字节)
 * - NowDead 标志 (4字节,uint32)
 *
 * @note NowDead 标志影响客户端的表现:
 *       - true: 目标已死亡,播放击杀动画和音效
 *       - false: 目标仍存活,仅停止攻击
 */
WorldPacket const* WorldPackets::Combat::SAttackStop::Write()
{
    // 写入攻击者的压缩 GUID
    _worldPacket << Attacker;
    // 写入目标的压缩 GUID
    _worldPacket << Victim;
    // 写入死亡标志,转换为 uint32 以保证跨平台兼容性
    _worldPacket << uint32(NowDead);

    return &_worldPacket;
}

/**
 * @brief 序列化取消自动攻击消息包
 * @return 返回序列化后的数据包指针
 *
 * 将单位的压缩 GUID 写入数据包,通知客户端停止自动攻击循环。
 * 主要用于猎人的自动射击等远程自动攻击技能的取消。
 *
 * 数据包格式:
 * - Guid PackedGuid (可变字节,通常 3-8 字节)
 *
 * @note 此消息通常在以下情况发送:
 *       - 玩家移动(射击需要站立)
 *       - 切换目标
 *       - 使用其他技能打断自动攻击
 *       - 被控制效果打断(如昏迷、沉默等)
 */
WorldPacket const* WorldPackets::Combat::CancelAutoRepeat::Write()
{
    // 写入单位的压缩 GUID
    _worldPacket << Guid;

    return &_worldPacket;
}

/**
 * @brief 从网络包中读取武器收起状态
 *
 * 从客户端发送的武器状态切换请求中提取新的收起状态值。
 * 客户端通过按 Z 键或使用快捷键来切换武器的显示状态。
 *
 * 状态值含义:
 * - 0: SHEATH_STATE_UNARMED - 武器收起状态(放在背后或腰间)
 * - 1: SHEATH_STATE_MELEE - 近战武器装备状态(在手中)
 * - 2: SHEATH_STATE_RANGED - 远程武器装备状态
 *
 * @note 此方法在服务器接收到 CMSG_SET_SHEATHED 消息时被调用。
 *       服务器会验证状态的合法性并同步给其他玩家。
 */
void WorldPackets::Combat::SetSheathed::Read()
{
    // 从数据包中读取武器收起状态
    _worldPacket >> CurrentSheathState;
}
