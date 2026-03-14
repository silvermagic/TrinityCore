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
 * @file MessageBuffer.h
 * @brief 网络消息缓冲区管理类
 *
 * 本模块提供了 MessageBuffer 类，用于管理网络消息的读写缓冲区。
 * 这是一个底层的 I/O 缓冲区实现，被网络通信模块广泛使用。
 *
 * 主要特点:
 * - 基于字节流的环形缓冲区设计
 * - 支持读写指针独立管理
 * - 自动扩容机制
 * - 支持移动语义，提高性能
 *
 * 缓冲区结构:
 * ```
 * +------------------------------------------+
 * |  已读区域  |   活跃数据   |   空闲空间    |
 * +------------------------------------------+
 * ^           ^              ^               ^
 * |           |              |               |
 * Base       rpos          wpos            end
 * ```
 *
 * 典型应用场景:
 * - 网络连接的接收缓冲区
 * - 网络连接的发送缓冲区
 * - 数据包的序列化和反序列化
 *
 * @性能说明:
 * - Normalize() 操作涉及内存移动，在数据量大时有性能开销
 * - 建议：在写入新数据前调用 EnsureFreeSpace()
 * - 建议：定期调用 Normalize() 回收已读空间
 */

#ifndef __MESSAGEBUFFER_H_
#define __MESSAGEBUFFER_H_

#include "Define.h"
#include <vector>
#include <cstring>

/**
 * @class MessageBuffer
 * @brief 网络消息缓冲区管理类
 *
 * 管理一个动态大小的字节缓冲区，支持顺序读写操作。
 * 使用读写指针（_rpos 和 _wpos）来跟踪缓冲区中的有效数据。
 *
 * 设计理念:
 * - 写入操作从 _wpos 开始，写入后更新 _wpos
 * - 读取操作从 _rpos 开始，读取后更新 _rpos
 * - 活跃数据位于 [_rpos, _wpos) 区间
 * - 空闲空间位于 [_wpos, buffer_end) 区间
 * - 已读区域位于 [0, _rpos) 区间
 *
 * 内存管理:
 * - 默认初始大小为 4KB
 * - 当空间不足时自动扩容（当前大小的 1.5 倍）
 * - 通过 Normalize() 可以回收已读区域
 */
class MessageBuffer
{
    typedef std::vector<uint8>::size_type size_type;  ///< 大小类型别名

public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个默认大小（4096 字节）的空缓冲区。
     * 读写指针初始化为 0，表示没有数据。
     *
     * 适用场景:
     * - 不确定消息大小时的默认选择
     * - 一般的网络消息处理
     */
    MessageBuffer() : _wpos(0), _rpos(0), _storage()
    {
        _storage.resize(4096);
    }

    /**
     * @brief 指定初始大小的构造函数
     * @param initialSize 缓冲区初始大小（字节）
     *
     * 创建指定大小的缓冲区，适用于已知消息大小的场景。
     *
     * @性能说明 预分配合适的大小可以避免后续扩容开销
     */
    explicit MessageBuffer(std::size_t initialSize) : _wpos(0), _rpos(0), _storage()
    {
        _storage.resize(initialSize);
    }

    /**
     * @brief 拷贝构造函数
     * @param right 要拷贝的源缓冲区
     *
     * 深拷贝另一个缓冲区的所有数据，包括读写指针位置。
     */
    MessageBuffer(MessageBuffer const& right) : _wpos(right._wpos), _rpos(right._rpos), _storage(right._storage)
    {
    }

    /**
     * @brief 移动构造函数
     * @param right 要移动的源缓冲区
     *
     * 移动语义实现，转移缓冲区所有权，避免数据拷贝。
     * 源缓冲区会被清空。
     *
     * @性能说明 O(1)，推荐使用
     */
    MessageBuffer(MessageBuffer&& right) : _wpos(right._wpos), _rpos(right._rpos), _storage(right.Move()) { }

    /**
     * @brief 重置缓冲区
     *
     * 将读写指针重置为 0，清空缓冲区内容。
     * 注意：不会释放内存，只是重置指针。
     *
     * @调用时机 需要重新使用缓冲区时
     * @性能说明 O(1)
     */
    void Reset()
    {
        _wpos = 0;
        _rpos = 0;
    }

    /**
     * @brief 调整缓冲区大小
     * @param bytes 新的缓冲区大小（字节）
     *
     * 强制调整缓冲区大小，可能会截断数据或扩展空间。
     * @警告 如果新大小小于当前活跃数据大小，数据会被截断
     */
    void Resize(size_type bytes)
    {
        _storage.resize(bytes);
    }

    /**
     * @brief 获取缓冲区基地址
     * @return 缓冲区起始地址指针
     *
     * 返回缓冲区的起始地址，常用于底层内存操作。
     */
    uint8* GetBasePointer() { return _storage.data(); }

    /**
     * @brief 获取读取位置指针
     * @return 当前读取位置的指针
     *
     * 返回活跃数据的起始位置，用于读取数据。
     */
    uint8* GetReadPointer() { return GetBasePointer() + _rpos; }

    /**
     * @brief 获取写入位置指针
     * @return 当前写入位置的指针
     *
     * 返回空闲空间的起始位置，用于写入新数据。
     */
    uint8* GetWritePointer() { return GetBasePointer() + _wpos; }

    /**
     * @brief 标记读取完成
     * @param bytes 已读取的字节数
     *
     * 读取数据后调用此方法，更新读取指针位置。
     * 通常与网络读取操作配合使用。
     */
    void ReadCompleted(size_type bytes) { _rpos += bytes; }

    /**
     * @brief 标记写入完成
     * @param bytes 已写入的字节数
     *
     * 写入数据后调用此方法，更新写入指针位置。
     * 通常与网络发送操作配合使用。
     */
    void WriteCompleted(size_type bytes) { _wpos += bytes; }

    /**
     * @brief 获取活跃数据大小
     * @return 当前缓冲区中有效数据的字节数
     *
     * 活跃数据是指已写入但尚未读取的数据。
     */
    size_type GetActiveSize() const { return _wpos - _rpos; }

    /**
     * @brief 获取剩余空间大小
     * @return 缓冲区中剩余可用空间的字节数
     *
     * 剩余空间是指写入指针之后可用于写入的空间。
     */
    size_type GetRemainingSpace() const { return _storage.size() - _wpos; }

    /**
     * @brief 获取缓冲区总大小
     * @return 缓冲区的总容量（字节）
     *
     * 返回缓冲区的总大小，包括已读区域、活跃数据和空闲空间。
     */
    size_type GetBufferSize() const { return _storage.size(); }

    /**
     * @brief 规范化缓冲区，回收已读空间
     *
     * 将活跃数据移动到缓冲区起始位置，回收已读区域。
     * 这样可以增加剩余空间，避免频繁扩容。
     *
     * 操作流程:
     * 1. 检查是否有已读区域（_rpos > 0）
     * 2. 如果有活跃数据，将其移动到缓冲区起始位置
     * 3. 更新读写指针
     *
     * @调用时机:
     * - 缓冲区空间不足，但已读区域较大时
     * - 定期清理缓冲区时
     *
     * @性能说明 O(n)，n 为活跃数据大小
     *
     * @example
     * // 缓冲区状态: [已读XXXX|活跃数据YYYY|空闲空间ZZ]
     * // Normalize() 后: [活跃数据YYYY|空闲空间ZZZZZZ]
     */
    void Normalize()
    {
        // 只有存在已读区域时才需要处理
        if (_rpos)
        {
            // 如果有活跃数据，需要移动
            if (_rpos != _wpos)
                memmove(GetBasePointer(), GetReadPointer(), GetActiveSize());
            // 更新指针位置
            _wpos -= _rpos;
            _rpos = 0;
        }
    }

    /**
     * @brief 确保有空闲空间可用
     *
     * 如果缓冲区已满，自动扩容。扩容策略为当前大小的 1.5 倍。
     *
     * @前提条件 应该先调用 Normalize() 回收已读空间
     *
     * @调用时机 准备写入新数据前，确保有足够空间
     * @性能说明 扩容时 O(n)，不扩容时 O(1)
     *
     * @example
     * buffer.Normalize();    // 先回收已读空间
     * buffer.EnsureFreeSpace(); // 确保有空间（必要时扩容）
     * // 现在可以安全写入数据
     */
    void EnsureFreeSpace()
    {
        // 如果缓冲区已满，扩容到当前大小的 1.5 倍
        if (GetRemainingSpace() == 0)
            _storage.resize(_storage.size() * 3 / 2);
    }

    /**
     * @brief 写入数据到缓冲区
     * @param data 要写入的数据指针
     * @param size 要写入的字节数
     *
     * 将指定数据拷贝到缓冲区的写入位置，并更新写入指针。
     * 调用者应确保有足够的空闲空间。
     *
     * @调用时机 需要向缓冲区添加数据时
     * @性能说明 O(n)，n 为 size
     *
     * @warning 不会检查空间是否足够，调用者需自行确保
     */
    void Write(void const* data, std::size_t size)
    {
        if (size)
        {
            memcpy(GetWritePointer(), data, size);
            WriteCompleted(size);
        }
    }

    /**
     * @brief 移动语义：转移缓冲区所有权
     * @return 包含缓冲区数据的 vector 右值引用
     *
     * 将内部存储的数据以右值引用形式返回，同时重置缓冲区。
     * 用于将数据转移出去，避免拷贝。
     *
     * @调用时机 需要将缓冲区数据转移给其他容器时
     * @性能说明 O(1)
     *
     * @warning 调用后缓冲区会被重置
     */
    std::vector<uint8>&& Move()
    {
        _wpos = 0;
        _rpos = 0;
        return std::move(_storage);
    }

    /**
     * @brief 拷贝赋值操作符
     * @param right 要拷贝的源缓冲区
     * @return 缓冲区自身的引用
     *
     * 深拷贝另一个缓冲区的所有数据。
     */
    MessageBuffer& operator=(MessageBuffer const& right)
    {
        if (this != &right)
        {
            _wpos = right._wpos;
            _rpos = right._rpos;
            _storage = right._storage;
        }

        return *this;
    }

    /**
     * @brief 移动赋值操作符
     * @param right 要移动的源缓冲区
     * @return 缓冲区自身的引用
     *
     * 移动语义实现，转移缓冲区所有权，避免数据拷贝。
     */
    MessageBuffer& operator=(MessageBuffer&& right)
    {
        if (this != &right)
        {
            _wpos = right._wpos;
            _rpos = right._rpos;
            _storage = right.Move();
        }

        return *this;
    }

private:
    size_type _wpos;              ///< 写入位置指针，指向下一个可写入位置
    size_type _rpos;              ///< 读取位置指针，指向下一个可读取位置
    std::vector<uint8> _storage;  ///< 底层数据存储容器
};

#endif /* __MESSAGEBUFFER_H_ */
