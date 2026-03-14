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
 * @file LFGPackets.cpp
 * @brief 寻求组队系统（LFG）网络数据包实现
 *
 * 本文件实现了 LFG 系统客户端数据包的反序列化逻辑。
 * 主要负责将接收到的网络字节流解析为结构化的数据对象。
 *
 * 相关模块：
 * - LFGMgr: LFG 系统管理器，处理组队逻辑
 * - LFGHandler: LFG 数据包处理器，响应玩家操作
 */

#include "LFGPackets.h"

/**
 * @brief 读取 LFG 加入请求的数据包内容
 *
 * 该方法实现了 CMSG_LFG_JOIN 消息的反序列化流程。
 * 数据包格式（按读取顺序）：
 * 1. uint32 Roles - 玩家角色定位
 * 2. bool NoPartialClear - 是否拒绝未完成副本
 * 3. bool Achievements - 是否要求成就
 * 4. uint8 SlotCount - 选择的地下城数量
 * 5. uint32 Slots[SlotCount] - 地下城槽位ID数组
 * 6. uint8 NeedsCount - 需求项数量（客户端固定为3）
 * 7. uint8 Needs[3] - 需求类型数组
 * 8. string Comment - 玩家备注信息
 *
 * @note 调用时机：当服务器接收到 CMSG_LFG_JOIN 消息时
 * @note 线程安全：在网络线程中调用，需确保不访问共享资源
 *
 * @performance 反序列化操作复杂度为 O(n)，n 为地下城槽位数量
 * @performance 使用 read_skip 跳过固定值字段，减少不必要的内存操作
 */
void WorldPackets::LFG::LFGJoin::Read()
{
    // 读取玩家选择的角色定位（坦克/治疗/输出）
    _worldPacket >> Roles;

    // 读取副本完成偏好设置
    _worldPacket >> NoPartialClear;
    _worldPacket >> Achievements;

    // 读取选择的地下城槽位列表
    // 客户端发送的槽位数量上限为50
    Slots.resize(_worldPacket.read<uint8>());
    for (uint32& slot : Slots)
        _worldPacket >> slot;

    // 跳过需求项计数字段（客户端硬编码为3，服务器无需读取）
    // 客户端协议设计遗留字段，始终为固定值
    _worldPacket.read_skip<uint8>(); // Needs count, hardcoded to 3 in client

    // 读取需求类型数组（固定3个元素）
    // 用途：标识玩家对不同装备的需求优先级
    for (uint8& needs : Needs)
        _worldPacket >> needs;

    // 读取玩家备注信息（可为空字符串）
    _worldPacket >> Comment;
}
