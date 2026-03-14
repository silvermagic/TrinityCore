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
 * @file ObjectGuid.cpp
 * @brief 游戏对象唯一标识符（GUID）系统实现
 *
 * 本文件实现了GUID的创建、打包、序列化和生成等功能。
 * 详细说明请参考 ObjectGuid.h
 */

#include "ObjectGuid.h"
#include "ByteBuffer.h"
#include "Log.h"
#include "World.h"
#include <sstream>
#include <iomanip>

/// 空GUID的全局常量
ObjectGuid const ObjectGuid::Empty = ObjectGuid();

/**
 * @brief 获取GUID高位类型的名称字符串
 * @param high GUID高位类型
 * @return 类型名称的C字符串
 *
 * 用于调试和日志输出，提供可读的类型名称。
 */
char const* ObjectGuid::GetTypeName(HighGuid high)
{
    switch (high)
    {
        case HighGuid::Item:         return "Item";
        case HighGuid::Player:       return "Player";
        case HighGuid::GameObject:   return "Gameobject";
        case HighGuid::Transport:    return "Transport";
        case HighGuid::Unit:         return "Creature";
        case HighGuid::Pet:          return "Pet";
        case HighGuid::Vehicle:      return "Vehicle";
        case HighGuid::DynamicObject: return "DynObject";
        case HighGuid::Corpse:       return "Corpse";
        case HighGuid::Mo_Transport: return "MoTransport";
        case HighGuid::Instance:     return "InstanceID";
        case HighGuid::Group:        return "Group";
        default:
            return "<unknown>";
    }
}

/**
 * @brief 将GUID转换为可读的字符串表示
 * @return GUID的字符串表示
 *
 * 格式："GUID Full: 0xXXXXXXXXXXXXXXXX Type: XXX Entry: XXX Low: XXX"
 * 用于调试和日志输出。
 */
std::string ObjectGuid::ToString() const
{
    std::ostringstream str;
    str << "GUID Full: 0x" << std::hex << std::setw(16) << std::setfill('0') << _guid << std::dec;
    str << " Type: " << GetTypeName();
    if (HasEntry())
        str << (IsPet() ? " Pet number: " : " Entry: ") << GetEntry() << " ";

    str << " Low: " << GetCounter();
    return str.str();
}

/**
 * @brief 创建全局类型GUID
 * @param type GUID高位类型
 * @param counter 计数器
 * @return 创建的ObjectGuid对象
 */
ObjectGuid ObjectGuid::Global(HighGuid type, LowType counter)
{
    return ObjectGuid(type, counter);
}

/**
 * @brief 创建地图特定类型GUID
 * @param type GUID高位类型
 * @param entry 实体ID
 * @param counter 计数器
 * @return 创建的ObjectGuid对象
 */
ObjectGuid ObjectGuid::MapSpecific(HighGuid type, uint32 entry, LowType counter)
{
    return ObjectGuid(type, entry, counter);
}

/**
 * @brief 设置打包GUID的值
 * @param guid 要打包的ObjectGuid对象
 *
 * 打包算法：
 * 1. 扫描GUID的8个字节（从低到高）
 * 2. 第一个字节是掩码，记录哪些字节非零
 * 3. 后续存储非零字节
 *
 * 这样可以减少网络传输的数据量，特别是对于计数器较小的GUID。
 * 例如：GUID 0x00000000F1300001 只需要4字节存储。
 */
void PackedGuid::Set(ObjectGuid guid)
{
    _packedSize = 1;  // 初始化为1，第一个字节是掩码
    uint64 raw = guid.GetRawValue();

    // 遍历GUID的8个字节
    for (uint8 i = 0; i < 8; ++i)
    {
        uint8 byte = (raw >> (i * 8)) & 0xFF;
        _packedGuid[_packedSize] = byte;

        // 如果字节非零，则在掩码中设置对应位
        if (byte)
        {
            _packedGuid[0] |= uint8(1 << i);
            ++_packedSize;
        }
    }
}

/**
 * @brief ByteBuffer输出操作符重载（ObjectGuid）
 * @param buf 字节缓冲区
 * @param guid 要写入的GUID
 * @return 字节缓冲区引用
 *
 * 将GUID以完整的64位格式写入缓冲区。
 */
ByteBuffer& operator<<(ByteBuffer& buf, ObjectGuid const& guid)
{
    buf << uint64(guid.GetRawValue());
    return buf;
}

/**
 * @brief ByteBuffer输入操作符重载（ObjectGuid）
 * @param buf 字节缓冲区
 * @param guid 要读取到的GUID引用
 * @return 字节缓冲区引用
 *
 * 从缓冲区读取完整的64位GUID。
 */
ByteBuffer& operator>>(ByteBuffer& buf, ObjectGuid& guid)
{
    guid.Set(buf.read<uint64>());
    return buf;
}

/**
 * @brief ByteBuffer输出操作符重载（PackedGuid）
 * @param buf 字节缓冲区
 * @param guid 要写入的打包GUID
 * @return 字节缓冲区引用
 *
 * 将打包GUID写入缓冲区，数据量通常小于8字节。
 */
ByteBuffer& operator<<(ByteBuffer& buf, PackedGuid const& guid)
{
    buf.append(guid._packedGuid.data(), guid._packedSize);
    return buf;
}

/**
 * @brief ByteBuffer输出操作符重载（PackedGuidWriter）
 * @param buf 字节缓冲区
 * @param guid 打包GUID写入辅助对象
 * @return 字节缓冲区引用
 *
 * 使用ByteBuffer的appendPackGUID方法写入打包GUID。
 */
ByteBuffer& operator<<(ByteBuffer& buf, PackedGuidWriter const& guid)
{
    buf.appendPackGUID(guid.Guid.GetRawValue());
    return buf;
}

/**
 * @brief ByteBuffer输入操作符重载（PackedGuidReader）
 * @param buf 字节缓冲区
 * @param guid 打包GUID读取辅助对象
 * @return 字节缓冲区引用
 *
 * 使用ByteBuffer的readPackGUID方法读取打包GUID。
 */
ByteBuffer& operator>>(ByteBuffer& buf, PackedGuidReader const& guid)
{
    buf.readPackGUID(reinterpret_cast<uint64&>(guid.Guid));
    return buf;
}

/**
 * @brief 生成新的GUID计数器值
 * @return 新的计数器值
 *
 * 生成流程：
 * 1. 检查计数器是否即将溢出
 * 2. 对于某些类型（Unit, Vehicle, GameObject, Transport），检查是否需要触发警告
 * 3. 返回当前计数器并递增
 *
 * 注意：此函数不是线程安全的，调用者需要确保同步。
 */
ObjectGuid::LowType ObjectGuidGenerator::Generate()
{
    // 检查计数器是否即将溢出
    if (_nextGuid >= ObjectGuid::GetMaxCounter(_high) - 1)
        HandleCounterOverflow();

    // 对于生物、载具、游戏对象和运输工具，检查是否需要触发警告
    if (_high == HighGuid::Unit || _high == HighGuid::Vehicle || _high == HighGuid::GameObject || _high == HighGuid::Transport)
        CheckGuidTrigger();

    return _nextGuid++;
}

/**
 * @brief 处理计数器溢出
 *
 * 当GUID计数器达到最大值时调用。
 * 记录致命错误日志并关闭服务器，因为GUID溢出会导致游戏逻辑错误。
 */
void ObjectGuidGenerator::HandleCounterOverflow()
{
    TC_LOG_ERROR("misc", "{} guid overflow!! Can't continue, shutting down server. ", ObjectGuid::GetTypeName(_high));
    World::StopNow(ERROR_EXIT_CODE);
}

/**
 * @brief 检查GUID使用情况并触发警告
 *
 * 当GUID使用量达到预设阈值时，触发系统警告。
 * 这有助于管理员及时发现GUID消耗过快的问题。
 */
void ObjectGuidGenerator::CheckGuidTrigger()
{
    // 如果未处于GUID警告状态，且计数器超过警告阈值，触发警告
    if (!sWorld->IsGuidAlert() && _nextGuid > sWorld->getIntConfig(CONFIG_RESPAWN_GUIDALERTLEVEL))
        sWorld->TriggerGuidAlert();
    // 如果未处于GUID警报状态，且计数器超过警报阈值，触发警报
    else if (!sWorld->IsGuidWarning() && _nextGuid > sWorld->getIntConfig(CONFIG_RESPAWN_GUIDWARNLEVEL))
        sWorld->TriggerGuidWarning();
}
