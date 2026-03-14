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
 * @file WorldStatePackets.cpp
 * @brief 世界状态数据包实现模块
 *
 * 本文件实现了世界状态数据包的序列化逻辑。
 * 世界状态数据包用于在服务器和客户端之间同步游戏世界的动态状态信息。
 *
 * 主要实现:
 * - InitWorldStates: 初始化世界状态(发送完整状态列表)
 * - UpdateWorldState: 更新单个世界状态变量
 *
 * @see WorldStatePackets.h 头文件定义
 */

#include "WorldStatePackets.h"

/**
 * @brief InitWorldStates 构造函数实现
 *
 * 初始化世界状态数据包,设置操作码并预分配最小包大小。
 * 预分配大小计算:
 * - 4 字节: 地图ID (int32)
 * - 4 字节: 区域ID (int32)
 * - 4 字节: 区域ID (int32)
 * - 2 字节: 世界状态变量数量 (uint16)
 * 总计: 14 字节
 *
 * 注意: 实际包大小会在 Write() 中根据变量数量动态调整。
 */
WorldPackets::WorldState::InitWorldStates::InitWorldStates() : ServerPacket(SMSG_INIT_WORLD_STATES, 4 + 4 + 4 + 2) { }

/**
 * @brief 序列化 InitWorldStates 数据包
 * @return 返回序列化完成的数据包指针
 *
 * 此方法将世界状态初始化数据序列化为二进制格式,以便通过网络发送给客户端。
 *
 * 序列化流程:
 * 1. 预分配足够的缓冲区空间,避免多次内存重分配
 * 2. 写入位置信息(地图ID、区域ID、区域ID)
 * 3. 写入世界状态变量总数
 * 4. 遍历所有世界状态变量,依次写入ID和值
 *
 * 数据包格式:
 * [int32 MapID][int32 ZoneID][int32 AreaID][uint16 Count]
 * [int32 VarID1][int32 Value1]
 * [int32 VarID2][int32 Value2]
 * ...
 *
 * 性能优化:
 * - 使用 reserve() 预分配内存,避免序列化过程中的多次重分配
 * - 每个世界状态变量占用 8 字节 (int32 ID + int32 Value)
 */
WorldPacket const* WorldPackets::WorldState::InitWorldStates::Write()
{
    // 预分配缓冲区空间: 基础字段(14字节) + 每个变量8字节
    // 这样可以避免在循环写入时发生多次内存重分配,提高性能
    _worldPacket.reserve(4 + 4 + 4 + 2 + Worldstates.size() * 8);

    // 写入位置信息,客户端根据这些信息确定哪些世界状态应该显示
    _worldPacket << int32(MapID);   // 当前地图ID
    _worldPacket << int32(ZoneID);  // 当前区域ID
    _worldPacket << int32(AreaID);  // 当前子区域ID

    // 写入世界状态变量数量
    // 使用 uint16 类型,最多支持 65535 个状态变量(实际远不会用到这么多)
    _worldPacket << uint16(Worldstates.size());

    // 遍历并写入所有世界状态变量
    // 每个变量包含: 变量ID(用于标识) + 当前值(用于显示)
    for (WorldStateInfo const& wsi : Worldstates)
    {
        _worldPacket << int32(wsi.VariableID);  // 世界状态变量ID
        _worldPacket << int32(wsi.Value);       // 世界状态变量值
    }

    return &_worldPacket;
}

/**
 * @brief 序列化 UpdateWorldState 数据包
 * @return 返回序列化完成的数据包指针
 *
 * 此方法将单个世界状态更新序列化为二进制格式。
 * 相比 InitWorldStates,此包非常轻量,仅包含:
 * - 变量ID: 用于标识要更新哪个世界状态
 * - 新值: 客户端将此值更新到对应的UI元素
 *
 * 数据包格式:
 * [int32 VariableID][int32 Value]
 *
 * 调用场景:
 * - 战场中资源点被占领
 * - 冬拥湖攻城武器被摧毁
 * - 其他任何世界状态值变化的情况
 *
 * 性能特点:
 * - 固定8字节大小,网络开销极小
 * - 适合高频更新场景
 */
WorldPacket const* WorldPackets::WorldState::UpdateWorldState::Write()
{
    // 直接写入变量ID和新值
    _worldPacket << int32(VariableID);  // 要更新的世界状态变量ID
    _worldPacket << int32(Value);       // 世界状态变量的新值

    return &_worldPacket;
}
