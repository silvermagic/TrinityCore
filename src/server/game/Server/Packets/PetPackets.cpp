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
 * @file PetPackets.cpp
 * @brief 宠物系统网络包实现模块
 *
 * 本模块实现了宠物系统相关网络数据包的序列化和反序列化功能。
 * 主要功能包括:
 * - 客户端包的读取(Read)操作:从网络字节流中解析数据
 * - 服务器包的写入(Write)操作:将数据序列化为网络字节流
 *
 * 所有包的读写操作都使用 TrinityCore 的 WorldPacket 类提供的流操作符,
 * 确保数据的正确序列化和网络字节序转换。
 */

#include "PetPackets.h"

/**
 * @brief 读取解散小动物包数据
 *
 * 从网络包中读取小动物的 GUID。
 * 使用 >> 运算符自动处理字节序转换。
 */
void WorldPackets::Pet::DismissCritter::Read()
{
    // 从网络包流中读取小动物的 GUID(8 字节)
    // >> 运算符会自动处理网络字节序到主机字节序的转换
    _worldPacket >> CritterGUID;
}

/**
 * @brief 读取放弃宠物包数据
 *
 * 从网络包中读取宠物的 GUID。
 * 使用 >> 运算符自动处理字节序转换。
 */
void WorldPackets::Pet::PetAbandon::Read()
{
    // 从网络包流中读取宠物的 GUID(8 字节)
    // 注意: 放弃操作不可逆,服务器端需要验证宠物归属权
    _worldPacket >> PetGUID;
}

/**
 * @brief 读取宠物停止攻击包数据
 *
 * 从网络包中读取宠物的 GUID。
 * 使用 >> 运算符自动处理字节序转换。
 */
void WorldPackets::Pet::PetStopAttack::Read()
{
    // 从网络包流中读取宠物的 GUID(8 字节)
    // 服务器收到后会命令宠物停止攻击并返回跟随状态
    _worldPacket >> PetGUID;
}

/**
 * @brief 读取宠物法术自动施放设置包数据
 *
 * 从网络包中依次读取宠物 GUID、法术 ID 和自动施放启用状态。
 * 数据按顺序读取,客户端必须按相同顺序写入。
 */
void WorldPackets::Pet::PetSpellAutocast::Read()
{
    // 按顺序从网络包流中读取数据
    // 1. 读取宠物 GUID(8 字节),用于识别是哪只宠物
    _worldPacket >> PetGUID;

    // 2. 读取法术 ID(4 字节),用于识别要设置哪个法术
    _worldPacket >> SpellID;

    // 3. 读取自动施放状态(1 字节布尔值),true 表示开启自动施放
    _worldPacket >> AutocastEnabled;
}

/**
 * @brief 写入宠物学习法术通知包数据
 * @return 返回写入完成的网络包指针,供网络层发送
 *
 * 将法术 ID 写入网络包,通知客户端宠物学会了新法术。
 * 使用 << 运算符自动处理主机字节序到网络字节序的转换。
 */
WorldPacket const* WorldPackets::Pet::PetLearnedSpell::Write()
{
    // 将法术 ID 写入网络包流(4 字节)
    // << 运算符会自动处理主机字节序到网络字节序的转换
    // 显式转换为 uint32 确保数据大小明确
    _worldPacket << uint32(SpellID);

    // 返回网络包指针,网络层会将其发送给客户端
    return &_worldPacket;
}

/**
 * @brief 写入宠物遗忘法术通知包数据
 * @return 返回写入完成的网络包指针,供网络层发送
 *
 * 将法术 ID 写入网络包,通知客户端宠物遗忘了某个法术。
 * 使用 << 运算符自动处理主机字节序到网络字节序的转换。
 */
WorldPacket const* WorldPackets::Pet::PetUnlearnedSpell::Write()
{
    // 将法术 ID 写入网络包流(4 字节)
    // << 运算符会自动处理主机字节序到网络字节序的转换
    // 显式转换为 uint32 确保数据大小明确
    _worldPacket << uint32(SpellID);

    // 返回网络包指针,网络层会将其发送给客户端
    return &_worldPacket;
}
