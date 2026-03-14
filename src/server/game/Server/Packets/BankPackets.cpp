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
 * @file BankPackets.cpp
 * @brief 银行系统网络包实现文件
 *
 * 本文件实现了银行系统相关的所有网络包的序列化和反序列化方法。
 * 主要功能:
 *   - AutoBankItem: 从客户端读取要存入银行的物品位置信息
 *   - AutoStoreBankItem: 从客户端读取要从银行取出的物品位置信息
 *   - BuyBankSlot: 从客户端读取银行管理员NPC的GUID
 *   - BuyBankSlotResult: 向客户端写入购买银行槽位的结果
 *   - ShowBank: 向客户端写入银行管理员的GUID以打开银行界面
 *
 * 所有方法都是轻量级的,仅进行简单的数据读写操作,不涉及复杂的业务逻辑。
 */

#include "BankPackets.h"

/**
 * @brief 读取自动存入银行物品的包数据
 *
 * 从网络包中提取物品的位置信息。客户端发送的数据格式为:
 *   - uint8 Bag:  背包编号
 *   - uint8 Slot: 槽位编号
 *
 * 这两个字段按顺序从网络包中读取,客户端和服务器必须保持一致的读写顺序。
 *
 * @note 性能注意事项:此方法仅进行两次简单的字节读取操作,性能开销极小。
 */
void WorldPackets::Bank::AutoBankItem::Read()
{
    _worldPacket >> Bag;  // 读取背包编号 (0=主背包, 1-4=额外背包)
    _worldPacket >> Slot; // 读取槽位编号
}

/**
 * @brief 读取自动从银行取出物品的包数据
 *
 * 从网络包中提取物品在银行中的位置信息。客户端发送的数据格式为:
 *   - uint8 Bag:  银行背包编号
 *   - uint8 Slot: 槽位编号
 *
 * 这两个字段按顺序从网络包中读取,客户端和服务器必须保持一致的读写顺序。
 *
 * @note 性能注意事项:此方法仅进行两次简单的字节读取操作,性能开销极小。
 */
void WorldPackets::Bank::AutoStoreBankItem::Read()
{
    _worldPacket >> Bag;  // 读取银行背包编号
    _worldPacket >> Slot; // 读取槽位编号
}

/**
 * @brief 读取购买银行槽位的包数据
 *
 * 从网络包中提取银行管理员NPC的GUID。客户端发送的数据格式为:
 *   - ObjectGuid Banker: 银行管理员NPC的唯一标识符
 *
 * ObjectGuid 是一个64位的唯一标识符,用于标识游戏中的对象。
 * 服务器使用此GUID来验证玩家是否靠近该NPC以及是否可以与其交互。
 *
 * @note 性能注意事项:此方法仅进行一次GUID读取操作,性能开销极小。
 */
void WorldPackets::Bank::BuyBankSlot::Read()
{
    _worldPacket >> Banker; // 读取银行管理员NPC的GUID
}

/**
 * @brief 写入购买银行槽位结果的包数据
 *
 * 将购买结果编码到网络包中发送给客户端。服务器发送的数据格式为:
 *   - uint32 Result: 购买结果码
 *
 * 结果码的含义:
 *   - 0: 购买成功
 *   - 其他值: 各种失败原因 (如金币不足、已达槽位上限等)
 *
 * @return 返回写入完成后的世界包常量指针,用于网络发送
 *
 * @note 性能注意事项:此方法仅进行一次uint32写入操作,性能开销极小。
 *       网络发送本身的延迟远大于此方法的执行时间。
 */
WorldPacket const* WorldPackets::Bank::BuyBankSlotResult::Write()
{
    _worldPacket << uint32(Result); // 写入购买结果码

    return &_worldPacket;
}

/**
 * @brief 写入显示银行界面的包数据
 *
 * 将银行管理员NPC的GUID写入网络包发送给客户端。服务器发送的数据格式为:
 *   - ObjectGuid Banker: 银行管理员NPC的唯一标识符
 *
 * 客户端收到此包后会:
 *   1. 打开银行界面窗口
 *   2. 显示银行管理员NPC相关的交互选项
 *   3. 准备好处理后续的银行操作请求
 *
 * @return 返回写入完成后的世界包常量指针,用于网络发送
 *
 * @note 性能注意事项:此方法仅进行一次GUID写入操作,性能开销极小。
 *       网络发送本身的延迟远大于此方法的执行时间。
 */
WorldPacket const* WorldPackets::Bank::ShowBank::Write()
{
    _worldPacket << Banker; // 写入银行管理员NPC的GUID

    return &_worldPacket;
}
