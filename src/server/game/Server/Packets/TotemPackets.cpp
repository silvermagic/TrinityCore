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
 * @file TotemPackets.cpp
 * @brief 图腾系统网络数据包实现
 *
 * 本文件实现了图腾(Totem)相关网络数据包的序列化和反序列化功能。
 * 主要职责:
 * - 解析客户端发送的图腾销毁请求
 * - 构建发送给客户端的图腾创建通知
 *
 * 实现细节:
 * - 使用 WorldPacket 的流操作符进行数据读写
 * - 数据包格式符合 3.3.5 客户端协议规范
 */

#include "TotemPackets.h"

/**
 * @brief 读取图腾销毁请求数据包
 *
 * 从客户端数据包中读取图腾槽位索引。客户端通过此数据包告知服务端
 * 玩家想要召回哪个槽位的图腾。
 *
 * 数据格式:
 * - uint8 Slot: 图腾槽位索引
 *
 * 槽位映射关系:
 * - 0: 火图腾槽位
 * - 1: 土图腾槽位
 * - 2: 水图腾槽位
 * - 3: 空气图腾槽位
 *
 * @note 此函数由网络线程调用,处理来自客户端的 CMSG_TOTEM_DESTROYED 消息
 */
void WorldPackets::Totem::TotemDestroyed::Read()
{
    // 从数据包中读取图腾槽位索引(0-3)
    _worldPacket >> Slot;
}

/**
 * @brief 写入图腾创建通知数据包
 *
 * 将图腾的详细信息写入数据包,发送给客户端以通知图腾已被成功召唤。
 * 客户端收到后会显示图腾的UI图标、持续时间等信息。
 *
 * 数据格式(按写入顺序):
 * - uint8 Slot: 图腾槽位索引(1字节)
 * - ObjectGuid Totem: 图腾实体的全局唯一标识符(8字节)
 * - uint32 Duration: 图腾持续时间,单位毫秒(4字节)
 * - uint32 SpellID: 召唤图腾的法术ID(4字节)
 *
 * @return 返回构造完成的 WorldPacket 指针,用于网络发送
 *
 * @note 此函数由图腾召唤逻辑调用,在 Totem::Summon 或相关函数中触发
 * @see Totem::Summon
 * @see Player::SummonTotem
 */
WorldPacket const* WorldPackets::Totem::TotemCreated::Write()
{
    // 写入图腾槽位索引(客户端通过此索引确定UI位置)
    _worldPacket << Slot;

    // 写入图腾实体的GUID(客户端通过此ID与图腾对象交互)
    _worldPacket << Totem;

    // 写入图腾持续时间(客户端显示为倒计时UI)
    _worldPacket << Duration;

    // 写入召唤法术ID(客户端显示法术图标和提示信息)
    _worldPacket << SpellID;

    return &_worldPacket;
}
