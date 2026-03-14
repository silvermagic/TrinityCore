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
 * @file UpdateData.cpp
 * @brief 对象更新数据管理系统实现
 *
 * 本文件实现了更新数据的收集、打包、压缩和发送功能。
 * 详细说明请参考 UpdateData.h
 */

#include "UpdateData.h"
#include "Errors.h"
#include "Log.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"
#include <zlib.h>

/**
 * @brief 默认构造函数
 *
 * 初始化更新块计数为0。
 */
UpdateData::UpdateData() : m_blockCount(0) { }

/**
 * @brief 批量添加超出范围的GUID
 * @param guids GUID集合
 *
 * 将指定集合中的所有GUID添加到超出范围列表中。
 */
void UpdateData::AddOutOfRangeGUID(GuidSet& guids)
{
    m_outOfRangeGUIDs.insert(guids.begin(), guids.end());
}

/**
 * @brief 添加单个超出范围的GUID
 * @param guid 要添加的GUID
 */
void UpdateData::AddOutOfRangeGUID(ObjectGuid guid)
{
    m_outOfRangeGUIDs.insert(guid);
}

/**
 * @brief 压缩数据
 * @param dst 目标缓冲区指针
 * @param dst_size 输入时为目标缓冲区大小，输出时为压缩后数据大小
 * @param src 源数据指针
 * @param src_size 源数据大小
 *
 * 使用zlib库的deflate算法压缩数据。
 * 压缩级别由世界配置CONFIG_COMPRESSION决定（默认为Z_BEST_SPEED）。
 *
 * 压缩流程：
 * 1. 初始化zlib流结构
 * 2. 设置压缩级别
 * 3. 执行压缩
 * 4. 完成压缩
 * 5. 清理资源
 *
 * 如果压缩失败，会将dst_size设置为0。
 */
void UpdateData::Compress(void* dst, uint32 *dst_size, void* src, int src_size)
{
    z_stream c_stream;

    // 初始化zlib流结构，使用默认内存分配器
    c_stream.zalloc = (alloc_func)nullptr;
    c_stream.zfree = (free_func)nullptr;
    c_stream.opaque = (voidpf)nullptr;

    // 初始化压缩，使用世界配置的压缩级别（默认Z_BEST_SPEED）
    int z_res = deflateInit(&c_stream, sWorld->getIntConfig(CONFIG_COMPRESSION));
    if (z_res != Z_OK)
    {
        TC_LOG_ERROR("misc", "Can't compress update packet (zlib: deflateInit) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    // 设置输入输出缓冲区
    c_stream.next_out = (Bytef*)dst;
    c_stream.avail_out = *dst_size;
    c_stream.next_in = (Bytef*)src;
    c_stream.avail_in = (uInt)src_size;

    // 执行压缩（Z_NO_FLUSH表示还有数据要压缩）
    z_res = deflate(&c_stream, Z_NO_FLUSH);
    if (z_res != Z_OK)
    {
        TC_LOG_ERROR("misc", "Can't compress update packet (zlib: deflate) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    // 检查是否所有输入数据都已处理
    if (c_stream.avail_in != 0)
    {
        TC_LOG_ERROR("misc", "Can't compress update packet (zlib: deflate not greedy)");
        *dst_size = 0;
        return;
    }

    // 完成压缩（Z_FINISH表示这是最后一块数据）
    z_res = deflate(&c_stream, Z_FINISH);
    if (z_res != Z_STREAM_END)
    {
        TC_LOG_ERROR("misc", "Can't compress update packet (zlib: deflate should report Z_STREAM_END instead {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    // 清理压缩资源
    z_res = deflateEnd(&c_stream);
    if (z_res != Z_OK)
    {
        TC_LOG_ERROR("misc", "Can't compress update packet (zlib: deflateEnd) Error code: {} ({})", z_res, zError(z_res));
        *dst_size = 0;
        return;
    }

    // 返回压缩后的数据大小
    *dst_size = c_stream.total_out;
}

/**
 * @brief 构建网络包
 * @param packet 输出参数，构建的网络包
 * @return 如果成功返回true，否则返回false
 *
 * 构建流程：
 * 1. 计算并写入更新块数量
 * 2. 如果有超出范围的GUID，写入OUT_OF_RANGE_OBJECTS块
 * 3. 追加实际的更新数据
 * 4. 如果数据超过100字节，使用zlib压缩
 * 5. 设置相应的操作码
 *
 * 数据包格式：
 * - 未压缩：[SMSG_UPDATE_OBJECT][数据]
 * - 压缩：[SMSG_COMPRESSED_UPDATE_OBJECT][原始大小][压缩数据]
 */
bool UpdateData::BuildPacket(WorldPacket* packet)
{
    ASSERT(packet->empty());                                // shouldn't happen

    // 计算所需缓冲区大小
    ByteBuffer buf(4 + (m_outOfRangeGUIDs.empty() ? 0 : 1 + 4 + 9 * m_outOfRangeGUIDs.size()) + m_data.wpos());

    // 写入更新块数量（如果有超出范围GUID，则+1）
    buf << (uint32) (!m_outOfRangeGUIDs.empty() ? m_blockCount + 1 : m_blockCount);

    // 如果有超出范围的GUID，写入OUT_OF_RANGE_OBJECTS块
    if (!m_outOfRangeGUIDs.empty())
    {
        buf << uint8(UPDATETYPE_OUT_OF_RANGE_OBJECTS);
        buf << uint32(m_outOfRangeGUIDs.size());

        // 写入所有超出范围的GUID（打包格式）
        for (GuidSet::const_iterator i = m_outOfRangeGUIDs.begin(); i != m_outOfRangeGUIDs.end(); ++i)
            buf << i->WriteAsPacked();
    }

    // 追加实际的更新数据
    buf.append(m_data);

    // 获取实际数据大小
    size_t pSize = buf.wpos();                              // use real used data size

    // 如果数据超过100字节，使用压缩
    if (pSize > 100)                                       // compress large packets
    {
        uint32 destsize = compressBound(pSize);
        packet->resize(destsize + sizeof(uint32));

        // 写入原始大小（解压时需要）
        packet->put<uint32>(0, pSize);

        // 执行压缩
        Compress(const_cast<uint8*>(packet->contents()) + sizeof(uint32), &destsize, (void*)buf.contents(), pSize);
        if (destsize == 0)
            return false;

        // 调整包大小为实际压缩后大小
        packet->resize(destsize + sizeof(uint32));
        packet->SetOpcode(SMSG_COMPRESSED_UPDATE_OBJECT);
    }
    else                                                    // send small packets without compression
    {
        // 小包直接发送，不压缩
        packet->append(buf);
        packet->SetOpcode(SMSG_UPDATE_OBJECT);
    }

    return true;
}

/**
 * @brief 清空所有数据
 *
 * 重置数据缓冲区、超出范围GUID列表和更新块计数。
 */
void UpdateData::Clear()
{
    m_data.clear();
    m_outOfRangeGUIDs.clear();
    m_blockCount = 0;
}
