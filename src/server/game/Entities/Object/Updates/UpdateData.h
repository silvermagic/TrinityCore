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
 * @file UpdateData.h
 * @brief 对象更新数据管理系统
 *
 * 本模块实现了游戏对象状态更新的数据收集、打包和发送机制。
 * 服务器通过更新系统将对象状态同步到客户端。
 *
 * 更新类型：
 * - VALUES：仅更新属性值（对象已在客户端存在）
 * - MOVEMENT：更新移动信息
 * - CREATE_OBJECT：创建新对象
 * - CREATE_OBJECT2：创建新对象（带额外初始化）
 * - OUT_OF_RANGE_OBJECTS：通知客户端对象已超出范围
 * - NEAR_OBJECTS：通知客户端附近对象
 *
 * 更新流程：
 * 1. 收集对象变更（属性、位置等）
 * 2. 构建更新数据块
 * 3. 压缩数据（如果超过阈值）
 * 4. 发送网络包
 *
 * 性能优化：
 * - 使用位掩码只传输变化的属性
 * - 大包使用zlib压缩
 * - 批量发送多个更新
 */

#ifndef __UPDATEDATA_H
#define __UPDATEDATA_H

#include "Define.h"
#include "ByteBuffer.h"
#include "ObjectGuid.h"
#include <set>

class WorldPacket;

/**
 * @enum OBJECT_UPDATE_TYPE
 * @brief 对象更新类型枚举
 *
 * 定义服务器向客户端发送的不同更新类型。
 */
enum OBJECT_UPDATE_TYPE
{
    UPDATETYPE_VALUES               = 0,  ///< 仅更新属性值（对象已存在于客户端）
    UPDATETYPE_MOVEMENT             = 1,  ///< 更新移动信息（位置、速度等）
    UPDATETYPE_CREATE_OBJECT        = 2,  ///< 创建新对象（首次进入视野）
    UPDATETYPE_CREATE_OBJECT2       = 3,  ///< 创建新对象（带额外初始化，如新创建的角色）
    UPDATETYPE_OUT_OF_RANGE_OBJECTS = 4,  ///< 对象超出范围通知（客户端应销毁这些对象）
    UPDATETYPE_NEAR_OBJECTS         = 5   ///< 附近对象通知（未使用）
};

/**
 * @enum OBJECT_UPDATE_FLAGS
 * @brief 对象更新标志枚举
 *
 * 定义对象更新时的各种标志，用于控制更新行为和包含的数据。
 */
enum OBJECT_UPDATE_FLAGS
{
    UPDATEFLAG_NONE                 = 0x0000,  ///< 无标志
    UPDATEFLAG_SELF                 = 0x0001,  ///< 更新发送给对象自身
    UPDATEFLAG_TRANSPORT            = 0x0002,  ///< 对象是运输工具
    UPDATEFLAG_HAS_TARGET           = 0x0004,  ///< 单位有目标
    UPDATEFLAG_UNKNOWN              = 0x0008,  ///< 未知标志
    UPDATEFLAG_LOWGUID              = 0x0010,  ///< 使用低位GUID
    UPDATEFLAG_LIVING               = 0x0020,  ///< 活着的单位（需要移动信息）
    UPDATEFLAG_STATIONARY_POSITION  = 0x0040,  ///< 静止位置（游戏对象等）
    UPDATEFLAG_VEHICLE              = 0x0080,  ///< 对象是载具
    UPDATEFLAG_POSITION             = 0x0100,  ///< 包含位置信息
    UPDATEFLAG_ROTATION             = 0x0200   ///< 包含旋转信息
};

/**
 * @class UpdateData
 * @brief 对象更新数据收集和打包类
 *
 * UpdateData 负责收集对象更新信息，并构建网络包发送给客户端。
 * 支持批量更新、超出范围通知和数据压缩。
 *
 * 工作流程：
 * 1. 创建UpdateData实例
 * 2. 调用AddOutOfRangeGUID添加需要销毁的对象GUID
 * 3. 通过GetBuffer()获取ByteBuffer并写入更新数据
 * 4. 调用AddUpdateBlock()标记一个更新块完成
 * 5. 调用BuildPacket构建最终的网络包
 *
 * 使用示例：
 * @code
 * UpdateData updateData;
 * updateData.AddOutOfRangeGUID(guid);
 * ByteBuffer& buffer = updateData.GetBuffer();
 * buffer << updateType << packedGuid;
 * // ... 写入更多数据
 * updateData.AddUpdateBlock();
 * WorldPacket packet;
 * updateData.BuildPacket(&packet);
 * player->SendDirectMessage(&packet);
 * @endcode
 */
class UpdateData
{
    public:
        /**
         * @brief 默认构造函数
         */
        UpdateData();

        /**
         * @brief 移动构造函数
         * @param right 要移动的UpdateData对象
         */
        UpdateData(UpdateData&& right) : m_blockCount(right.m_blockCount),
            m_outOfRangeGUIDs(std::move(right.m_outOfRangeGUIDs)),
            m_data(std::move(right.m_data))
        {
        }

        /**
         * @brief 批量添加超出范围的GUID
         * @param guids GUID集合
         *
         * 将多个GUID标记为超出范围，客户端会销毁这些对象。
         */
        void AddOutOfRangeGUID(GuidSet& guids);

        /**
         * @brief 添加单个超出范围的GUID
         * @param guid 要添加的GUID
         */
        void AddOutOfRangeGUID(ObjectGuid guid);

        /**
         * @brief 增加更新块计数
         *
         * 每完成一个更新块的写入后调用。
         */
        void AddUpdateBlock() { ++m_blockCount; }

        /**
         * @brief 获取数据缓冲区
         * @return ByteBuffer引用，用于写入更新数据
         */
        ByteBuffer& GetBuffer() { return m_data; }

        /**
         * @brief 构建网络包
         * @param packet 输出参数，构建的网络包
         * @return 如果成功返回true，否则返回false
         *
         * 将收集的更新数据构建成网络包。
         * 如果数据超过100字节，会自动压缩。
         */
        bool BuildPacket(WorldPacket* packet);

        /**
         * @brief 检查是否有数据需要发送
         * @return 如果有更新数据或超出范围GUID返回true
         */
        bool HasData() const { return m_blockCount > 0 || !m_outOfRangeGUIDs.empty(); }

        /**
         * @brief 清空所有数据
         *
         * 重置更新块计数、清空缓冲区和超出范围GUID列表。
         */
        void Clear();

        /**
         * @brief 获取超出范围的GUID集合
         * @return GUID集合的常量引用
         */
        GuidSet const& GetOutOfRangeGUIDs() const { return m_outOfRangeGUIDs; }

    protected:
        uint32 m_blockCount;              ///< 更新块数量
        GuidSet m_outOfRangeGUIDs;        ///< 超出范围的GUID集合
        ByteBuffer m_data;                ///< 更新数据缓冲区

        /**
         * @brief 压缩数据
         * @param dst 目标缓冲区
         * @param dst_size 目标缓冲区大小（输入），压缩后大小（输出）
         * @param src 源数据
         * @param src_size 源数据大小
         *
         * 使用zlib压缩数据，用于大包的压缩传输。
         */
        void Compress(void* dst, uint32 *dst_size, void* src, int src_size);

        // 禁止拷贝
        UpdateData(UpdateData const& right) = delete;
        UpdateData& operator=(UpdateData const& right) = delete;
};
#endif
