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

#ifndef __UPDATEMASK_H
#define __UPDATEMASK_H

/**
 * @file UpdateMask.h
 * @brief 更新掩码管理模块
 *
 * 本文件定义了用于管理对象更新字段的位掩码系统。在游戏网络同步中,
 * 服务器需要向客户端发送对象状态更新,为了优化网络带宽,系统使用位掩码
 * 标记哪些字段发生了变化。只有被标记的字段才会被序列化到网络包中。
 *
 * 主要包含两个类:
 * - UpdateMask: 内部使用的更新掩码,用于跟踪字段变化
 * - UpdateMaskPacketBuilder: 网络包构建器,用于序列化掩码到客户端格式
 *
 * @see UpdateFields.h 字段定义
 * @see ByteBuffer.h 缓冲区操作
 */

#include "UpdateFields.h"
#include "ByteBuffer.h"
#include "Errors.h"

/**
 * @class UpdateMask
 * @brief 内部更新掩码管理类
 *
 * 用于在服务器内部跟踪对象的哪些字段需要更新。每个字节代表一个字段的更新状态,
 * 使用字节而非位作为单位是为了简化实现和提高访问效率。
 *
 * 典型使用流程:
 * 1. 调用 SetCount() 设置字段总数
 * 2. 使用 SetBit() 标记需要更新的字段
 * 3. 通过 GetBit() 检查字段是否需要更新
 * 4. 处理完成后可调用 Clear() 重置掩码以便重用
 *
 * 注意: 此类主要用于服务器内部状态跟踪,不直接用于网络传输。
 * 网络传输应使用 UpdateMaskPacketBuilder 类。
 *
 * @see UpdateMaskPacketBuilder 用于网络传输的掩码构建器
 */
class UpdateMask
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化一个空的更新掩码对象。此时掩码缓冲区为空,
     * 需要后续调用 SetCount() 分配空间。
     */
    UpdateMask() : _bits(nullptr), _fieldCount(0) { }

    /**
     * @brief 设置指定字段的更新标记
     *
     * 将指定索引位置的字段标记为"需要更新"状态。
     * 此操作为 O(1) 时间复杂度。
     *
     * @param index 字段索引,必须在 [0, _fieldCount) 范围内
     *
     * @warning 调用前必须先调用 SetCount() 初始化掩码大小,
     *          否则会导致数组越界访问
     */
    void SetBit(uint32 index)
    {
        _bits[index] = 1;
    }

    /**
     * @brief 清除指定字段的更新标记
     *
     * 将指定索引位置的字段标记为"无需更新"状态。
     * 此操作为 O(1) 时间复杂度。
     *
     * @param index 字段索引,必须在 [0, _fieldCount) 范围内
     *
     * @warning 调用前必须先调用 SetCount() 初始化掩码大小,
     *          否则会导致数组越界访问
     */
    void UnsetBit(uint32 index)
    {
        _bits[index] = 0;
    }

    /**
     * @brief 检查指定字段是否被标记为需要更新
     *
     * 查询指定字段的更新状态。此操作为 O(1) 时间复杂度。
     *
     * @param index 字段索引,必须在 [0, _fieldCount) 范围内
     * @return true 如果字段被标记为需要更新
     * @return false 如果字段未被标记或掩码未初始化
     *
     * @warning 调用前必须先调用 SetCount() 初始化掩码大小,
     *          否则会导致数组越界访问
     */
    bool GetBit(uint32 index) const
    {
        return _bits[index] != 0;
    }

    /**
     * @brief 设置掩码容量并初始化
     *
     * 分配指定大小的掩码缓冲区,并将所有位初始化为 0。
     * 此方法应在对象创建后首先调用。
     *
     * @param valuesCount 字段总数,决定掩码缓冲区大小
     *
     * @note 该方法会分配新的内存,多次调用会重新分配内存,
     *       旧的掩码数据将被丢弃
     * @note 使用 std::uninitialized_fill_n 进行零初始化,
     *       避免触发构造函数调用,提高性能
     */
    void SetCount(uint32 valuesCount)
    {
        _bits = std::make_unique<uint8[]>(valuesCount);
        std::uninitialized_fill_n(&_bits[0], valuesCount, 0);
        _fieldCount = valuesCount;
    }

    /**
     * @brief 清除所有更新标记
     *
     * 将掩码中所有字段重置为"无需更新"状态。
     * 掩码缓冲区本身不会被释放,可以重新使用。
     *
     * @note 此方法为 O(n) 时间复杂度,n 为字段总数
     * @note 如果掩码未初始化(_bits 为空),此方法不执行任何操作
     */
    void Clear()
    {
        if (_bits)
            std::fill_n(&_bits[0], _fieldCount, 0);
    }

private:
    std::unique_ptr<uint8[]> _bits;   ///< 位掩码缓冲区,每个字节代表一个字段的更新状态
    uint32 _fieldCount;               ///< 字段总数,记录掩码容量
};

/**
 * @class UpdateMaskPacketBuilder
 * @brief 网络包更新掩码构建器
 *
 * 专门用于构建发送给客户端的更新掩码数据包。客户端使用 uint32 类型读取掩码,
 * 因此此类将掩码组织为 32 位块的形式,每个块包含 32 个字段的更新状态。
 *
 * 与 UpdateMask 的区别:
 * - UpdateMask: 使用字节为单位,便于服务器内部快速访问
 * - UpdateMaskPacketBuilder: 使用位为单位,紧凑存储以节省网络带宽
 *
 * 数据包格式:
 * - 第一个字节: 块数量 (blockCount)
 * - 后续数据: 若干个 uint32 块,每个块表示 32 个字段的更新状态
 *
 * 典型使用流程:
 * 1. 构造时指定最大字段数
 * 2. 调用 SetBit() 标记需要更新的字段
 * 3. 调用 AppendToPacket() 将掩码序列化到网络包
 *
 * @see UpdateMask 内部掩码管理类
 * @see ByteBuffer 网络缓冲区
 */
class UpdateMaskPacketBuilder
{
public:
    /// 客户端读取掩码的类型,每个块包含 32 个位
    using ClientUpdateMaskType = uint32;

    /**
     * @brief 掩码块大小枚举
     */
    enum UpdateMaskCount
    {
        CLIENT_UPDATE_MASK_BITS = sizeof(ClientUpdateMaskType) * 8,  ///< 每个块包含的位数 (32 位)
    };

    /**
     * @brief 构造函数
     *
     * 根据字段总数分配掩码缓冲区。缓冲区大小按 32 位对齐,
     * 即分配 ceil(fieldCount / 32) 个 uint32 块。
     *
     * @param valuesCount 字段总数,决定掩码缓冲区大小
     *
     * @note 所有掩码位初始为 0 (未标记状态)
     */
    explicit UpdateMaskPacketBuilder(uint32 valuesCount) : _lastSetBit(0)
    {
        std::size_t blockCount = CalculateBlockCount(valuesCount);
        _mask = std::make_unique<ClientUpdateMaskType[]>(blockCount);
        std::uninitialized_fill_n(&_mask[0], blockCount, 0);
    }

    /**
     * @brief 设置指定字段的更新标记
     *
     * 将指定字段在掩码中标记为"需要更新"。此操作会:
     * 1. 计算字段所在的块索引和位偏移
     * 2. 使用位运算设置对应位
     * 3. 更新最后设置的字段索引
     *
     * @param bit 字段索引,从 0 开始
     *
     * @note 此操作为 O(1) 时间复杂度
     * @note 多次设置同一位是幂等操作
     */
    void SetBit(uint32 bit)
    {
        _mask[GetBlockIndex(bit)] |= GetBlockFlag(bit);
        _lastSetBit = bit;
    }

    /**
     * @brief 将掩码追加到网络包
     *
     * 序列化更新掩码到指定的字节缓冲区。数据格式为:
     * - 1 字节: 块数量 (仅包含实际需要的块,非全部块)
     * - N 个 uint32: 掩码数据
     *
     * 优化: 只发送 _lastSetBit 之前的块,减少网络传输量。
     * 例如,如果最大字段数为 100,但只设置了第 5 个字段,
     * 则只发送第一个块(包含前 32 个字段的状态)。
     *
     * @param data 目标字节缓冲区指针
     *
     * @warning data 必须为有效指针,否则会导致崩溃
     */
    void AppendToPacket(ByteBuffer* data)
    {
        uint8 blockCount = CalculateBlockCount(_lastSetBit + 1);
        *data << uint8(blockCount);
        if (blockCount)
            data->append(&_mask[0], blockCount);
    }

private:
    /**
     * @brief 计算指定字段数所需的块数量
     *
     * 将字段数向上取整到最近的 32 的倍数,然后除以 32 得到块数。
     *
     * @param fieldCount 字段总数
     * @return 所需的 uint32 块数量
     *
     * @note 这是一个编译时常量表达式,可在编译期求值
     * @note 公式: ceil(fieldCount / 32)
     */
    static constexpr uint8 CalculateBlockCount(uint32 fieldCount)
    {
        return (fieldCount + CLIENT_UPDATE_MASK_BITS - 1) / CLIENT_UPDATE_MASK_BITS;
    }

    /**
     * @brief 计算字段所在的块索引
     *
     * 字段索引 / 32 得到块索引。
     * 例如: 字段 0-31 在块 0,字段 32-63 在块 1,以此类推。
     *
     * @param bit 字段索引
     * @return 字段所在的块索引
     *
     * @note 这是一个编译时常量表达式
     */
    static constexpr std::size_t GetBlockIndex(uint32 bit)
    {
        return bit / 32;
    }

    /**
     * @brief 计算字段在块内的位标志
     *
     * 使用字段索引 % 32 得到块内偏移,然后生成对应的位标志。
     * 例如: 字段 0 的标志为 0x00000001,字段 31 的标志为 0x80000000。
     *
     * @param bit 字段索引
     * @return 该字段对应的位标志 (单一位为 1 的 uint32)
     *
     * @note 这是一个编译时常量表达式
     */
    static constexpr uint32 GetBlockFlag(uint32 bit)
    {
        return 1u << (bit % 32);
    }

    std::unique_ptr<ClientUpdateMaskType[]> _mask;  ///< 掩码缓冲区,按 32 位块组织
    uint32 _lastSetBit;                             ///< 最后设置的字段索引,用于优化网络传输
};

#endif
