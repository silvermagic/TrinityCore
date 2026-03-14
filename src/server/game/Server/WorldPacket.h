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
 * @file WorldPacket.h
 * @brief 世界数据包模块 - 定义游戏服务器与客户端之间的通信数据包结构
 *
 * 本模块定义了WorldPacket类，这是服务器与客户端通信的基本数据单元。
 * WorldPacket继承自ByteBuffer，扩展了操作码(opcode)功能，用于标识不同类型的网络消息。
 * 主要职责：
 * 1. 封装网络通信数据，包括操作码和数据负载
 * 2. 提供数据包的序列化和反序列化支持
 * 3. 支持数据包的移动语义，优化内存管理
 * 4. 记录特定操作码的接收时间，用于性能分析
 */

#ifndef TRINITYCORE_WORLDPACKET_H
#define TRINITYCORE_WORLDPACKET_H

#include "Common.h"
#include "Opcodes.h"
#include "ByteBuffer.h"
#include "Duration.h"

/**
 * @class WorldPacket
 * @brief 世界数据包类 - 服务器与客户端通信的基本数据单元
 *
 * WorldPacket是游戏服务器网络通信的核心类，继承自ByteBuffer。
 * 每个网络消息都封装为一个WorldPacket对象，包含：
 * - 操作码(opcode)：标识消息类型（如移动、攻击、聊天等）
 * - 数据负载：消息的具体内容
 *
 * 设计特点：
 * 1. 继承ByteBuffer提供数据读写能力
 * 2. 支持移动语义，减少不必要的内存拷贝
 * 3. 提供接收时间戳，用于性能监控和防作弊
 */
class WorldPacket : public ByteBuffer
{
    public:
        /**
         * @brief 默认构造函数 - 创建空数据包容器
         *
         * 创建一个空的数据包对象，通常用于后续初始化。
         * 操作码初始化为NULL_OPCODE，表示未定义。
         *
         * @note 调用时机：需要延迟初始化数据包时使用
         */
        WorldPacket() : ByteBuffer(0), m_opcode(NULL_OPCODE)
        {
        }

        /**
         * @brief 构造函数 - 创建指定操作码的数据包
         * @param opcode 操作码，标识消息类型
         * @param res 预留缓冲区大小，默认200字节
         *
         * 创建指定操作码的数据包，并预分配缓冲区空间。
         * 预分配可以减少后续数据追加时的内存重分配次数。
         *
         * @note 性能注意事项：合理设置res参数可以优化内存分配性能
         * @note 调用时机：发送数据包前创建
         */
        WorldPacket(uint16 opcode, size_t res = 200) : ByteBuffer(res),
            m_opcode(opcode) { }

        /**
         * @brief 移动构造函数 - 转移数据包资源
         * @param packet 源数据包（右值引用）
         *
         * 使用移动语义转移数据包的所有权，避免深拷贝。
         * 转移后，源数据包处于有效但未定义的状态。
         *
         * @note 性能注意事项：优先使用移动语义而非拷贝
         * @note 调用时机：需要转移数据包所有权时
         */
        WorldPacket(WorldPacket&& packet) : ByteBuffer(std::move(packet)), m_opcode(packet.m_opcode)
        {
        }

        /**
         * @brief 移动构造函数（带接收时间） - 转移数据包并设置接收时间
         * @param packet 源数据包（右值引用）
         * @param receivedTime 数据包接收时间点
         *
         * 扩展的移动构造函数，用于特定操作码的性能监控。
         * 接收时间用于计算数据包处理延迟。
         *
         * @note 调用时机：接收客户端数据包并需要性能监控时
         */
        WorldPacket(WorldPacket&& packet, TimePoint receivedTime) : ByteBuffer(std::move(packet)), m_opcode(packet.m_opcode), m_receivedTime(receivedTime)
        {
        }

        /**
         * @brief 拷贝构造函数 - 复制数据包
         * @param right 源数据包
         *
         * 创建数据包的深拷贝，包括操作码和所有数据。
         *
         * @note 性能注意事项：拷贝操作开销较大，优先使用移动语义
         * @note 调用时机：需要独立的数据包副本时
         */
        WorldPacket(WorldPacket const& right) : ByteBuffer(right), m_opcode(right.m_opcode)
        {
        }

        /**
         * @brief 拷贝赋值运算符 - 复制数据包内容
         * @param right 源数据包
         * @return 当前对象引用
         *
         * 执行深拷贝，复制操作码和数据缓冲区。
         * 包含自赋值检查以防止错误。
         *
         * @note 性能注意事项：拷贝操作开销较大，优先使用移动赋值
         */
        WorldPacket& operator=(WorldPacket const& right)
        {
            if (this != &right)
            {
                m_opcode = right.m_opcode;
                ByteBuffer::operator=(right);
            }

            return *this;
        }

        /**
         * @brief 移动赋值运算符 - 转移数据包内容
         * @param right 源数据包（右值引用）
         * @return 当前对象引用
         *
         * 使用移动语义转移数据包内容，避免深拷贝。
         * 包含自赋值检查以防止错误。
         *
         * @note 性能注意事项：优先使用移动赋值而非拷贝赋值
         */
        WorldPacket& operator=(WorldPacket&& right)
        {
            if (this != &right)
            {
                m_opcode = right.m_opcode;
                ByteBuffer::operator=(std::move(right));
            }

            return *this;
        }

        /**
         * @brief 构造函数 - 从消息缓冲区创建数据包
         * @param opcode 操作码
         * @param buffer 消息缓冲区（右值引用）
         *
         * 从现有的消息缓冲区构造数据包，通常用于网络接收场景。
         * 直接转移缓冲区所有权，避免数据拷贝。
         *
         * @note 调用时机：从网络层接收数据并构造数据包时
         */
        WorldPacket(uint16 opcode, MessageBuffer&& buffer) : ByteBuffer(std::move(buffer)), m_opcode(opcode) { }

        /**
         * @brief 初始化数据包 - 重置并设置新的操作码
         * @param opcode 新的操作码
         * @param newres 新的缓冲区大小，默认200字节
         *
         * 清空当前数据包并重新初始化。清除所有数据，预留新的缓冲区空间。
         * 用于重用数据包对象，避免频繁的内存分配。
         *
         * @note 性能注意事项：重用数据包对象比创建新对象更高效
         * @note 调用时机：需要重用数据包对象发送不同类型的消息时
         */
        void Initialize(uint16 opcode, size_t newres = 200)
        {
            clear();
            _storage.reserve(newres);
            m_opcode = opcode;
        }

        /**
         * @brief 获取操作码
         * @return 当前数据包的操作码
         *
         * 返回数据包的操作码，用于标识消息类型。
         *
         * @note 调用时机：处理数据包时需要判断消息类型
         */
        uint16 GetOpcode() const { return m_opcode; }

        /**
         * @brief 设置操作码
         * @param opcode 要设置的操作码
         *
         * 修改数据包的操作码。
         *
         * @note 调用时机：需要修改数据包类型时
         */
        void SetOpcode(uint16 opcode) { m_opcode = opcode; }

        /**
         * @brief 获取接收时间
         * @return 数据包接收的时间点
         *
         * 返回数据包的接收时间戳，用于性能监控和延迟计算。
         * 仅对特定操作码设置此时间戳。
         *
         * @note 调用时机：性能监控和延迟分析时
         */
        TimePoint GetReceivedTime() const { return m_receivedTime; }

    protected:
        uint16 m_opcode;                // 操作码 - 标识数据包类型的16位整数
        TimePoint m_receivedTime;       // 接收时间 - 仅对特定操作码设置，用于性能监控
};

#endif
