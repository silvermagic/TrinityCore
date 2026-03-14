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
 * @file ByteConverter.h
 * @brief 字节序转换工具模块
 *
 * @details 本模块提供跨平台的字节序转换功能，用于处理不同硬件架构之间的数据兼容性问题。
 *
 * 模块职责：
 * - 提供字节序转换的核心算法
 * - 根据目标平台自动选择正确的转换策略
 * - 支持任意大小的数据类型转换
 *
 * 主要功能：
 * - 字节序反转：将数据从大端序转换为小端序，或从小端序转换为大端序
 * - 自动转换：根据编译时检测的平台字节序自动决定是否需要转换
 * - 指针操作：支持直接通过指针操作内存数据
 *
 * 字节序转换原理：
 * 字节序（Endianness）是指多字节数据在内存中的存储顺序：
 * - 大端序（Big-Endian）：高位字节存储在低地址，低位字节存储在高地址
 *   例如：0x12345678 在内存中存储为 [0x12][0x34][0x56][0x78]
 * - 小端序（Little-Endian）：低位字节存储在低地址，高位字节存储在高地址
 *   例如：0x12345678 在内存中存储为 [0x78][0x56][0x34][0x12]
 *
 * 不同硬件平台可能使用不同的字节序：
 * - x86/x64 架构使用小端序
 * - PowerPC、SPARC 等架构通常使用大端序
 * - ARM 架构通常可配置，但默认使用小端序
 *
 * 网络传输标准使用大端序（网络字节序），因此跨平台数据交换时需要进行转换。
 * TrinityCore 使用小端序作为内部数据格式，在大端序平台上需要进行转换。
 *
 * 转换算法：
 * 本模块采用递归模板实现字节反转，通过模板元编程在编译期展开，
 * 避免了运行时循环开销，提高性能。转换过程将首尾字节交换，
 * 然后递归处理内部字节，直到完成所有字节的交换。
 */

#ifndef TRINITY_BYTECONVERTER_H
#define TRINITY_BYTECONVERTER_H

#include "Define.h"

/**
 * @namespace ByteConverter
 * @brief 字节序转换核心命名空间
 *
 * 提供字节序转换的底层实现，采用模板元编程技术，
 * 在编译期生成高效的字节交换代码。
 */
namespace ByteConverter
{
    /**
     * @brief 字节序反转核心模板函数（递归实现）
     *
     * @details 该函数通过递归模板实现字节反转，将数据的字节顺序完全颠倒。
     * 使用模板参数 T 指定要处理的字节数，编译器会自动展开递归调用，
     * 生成高效的交换代码。
     *
     * 算法原理：
     * 1. 交换首尾两个字节：*val <-> *(val + T - 1)
     * 2. 递归处理内部字节：从 val+1 开始，处理 T-2 个字节
     * 3. 递归终止条件：T <= 1（特化模板处理）
     *
     * 示例（4字节 uint32_t: 0x12345678）：
     * - 第一次调用：交换 byte0(0x12) 和 byte3(0x78) -> 0x78345612
     * - 第二次调用：交换 byte1(0x34) 和 byte2(0x56) -> 0x78563412
     * - 结果：0x78563412（完成反转）
     *
     * @tparam T 要处理的字节数，由编译器根据数据类型大小自动推导
     * @param val 指向数据起始字节的指针
     */
    template<size_t T>
    inline void convert(char *val)
    {
        // 保存第一个字节的值
        char tmp = *val;
        // 将最后一个字节的值复制到第一个字节位置
        *val = *(val + T - 1);
        // 将原第一个字节的值复制到最后一个字节位置
        *(val + T - 1) = tmp;
        // 递归处理内部字节（跳过已处理的两个字节）
        convert<T - 2>(val + 1);
    }

    /**
     * @brief 模板特化：处理0字节的递归终止条件
     *
     * @details 当剩余字节数为0时，表示所有字节已处理完毕，递归终止。
     * 这种情况出现在偶数大小的数据类型（如 uint16_t, uint32_t, uint64_t）
     * 的转换过程中。
     *
     * @param val 指针参数（不使用，仅用于模板匹配）
     */
    template<> inline void convert<0>(char *) { }

    /**
     * @brief 模板特化：处理1字节的递归终止条件
     *
     * @details 当剩余字节数为1时，表示到达了数据的中心字节。
     * 对于奇数大小的数据类型，中心字节不需要交换（位置不变）。
     * 这种情况虽然罕见，但提供了完整性支持。
     *
     * @param val 指针参数（不使用，中心字节无需处理）
     */
    template<> inline void convert<1>(char *) { }           // ignore central byte

    /**
     * @brief 应用字节序转换的便捷函数
     *
     * @details 该函数是对 convert 模板的封装，自动根据类型大小调用正确的转换函数。
     * 用户只需提供数据指针，函数会自动计算类型大小并执行转换。
     *
     * 工作流程：
     * 1. 使用 sizeof(T) 获取类型的字节大小
     * 2. 将类型指针转换为字节指针（char*）
     * 3. 调用 convert<sizeof(T)> 执行实际的字节交换
     *
     * @tparam T 数据类型，可以是任意大小的基本数据类型
     * @param val 指向要转换数据的指针
     *
     * @note 该函数会直接修改原数据，而非返回新值
     *
     * 示例用法：
     * @code
     * uint32_t value = 0x12345678;
     * ByteConverter::apply(&value);  // value 变为 0x78563412
     * @endcode
     */
    template<typename T> inline void apply(T *val)
    {
        convert<sizeof(T)>((char *)(val));
    }
}

/**
 * @defgroup EndianConvertFunctions 字节序转换函数组
 * @brief 根据平台字节序自动选择转换策略的函数
 *
 * @details 这些函数根据编译时检测的平台字节序（TRINITY_ENDIAN）自动决定是否执行转换。
 * TrinityCore 内部使用小端序作为标准格式，因此：
 * - 在小端序平台上（x86/x64）：数据已经是正确格式，无需转换
 * - 在大端序平台上：需要转换为大端序进行存储/传输
 *
 * 设计原理：
 * 通过编译时的条件编译（#if TRINITY_ENDIAN），为不同平台生成不同的代码，
 * 避免运行时判断的性能开销。这种设计称为"零开销抽象"。
 * @{
 */

#if TRINITY_ENDIAN == TRINITY_BIGENDIAN
/**
 * @brief 大端序平台：将小端序数据转换为平台格式（大端序）
 *
 * @details 在大端序平台上，接收到的外部数据（小端序）需要转换为大端序才能正确处理。
 *
 * @tparam T 数据类型
 * @param val 要转换的数据引用
 *
 * @note 仅在大端序平台上执行实际转换
 */
template<typename T> inline void EndianConvert(T& val) { ByteConverter::apply<T>(&val); }

/**
 * @brief 大端序平台：将大端序数据保持不变（已经是平台格式）
 *
 * @details 在大端序平台上，如果数据已经是大端序，无需转换。
 *
 * @tparam T 数据类型
 * @param val 数据引用（不执行任何操作）
 */
template<typename T> inline void EndianConvertReverse(T&) { }

/**
 * @brief 大端序平台：通过指针转换小端序数据为大端序
 *
 * @details 与 EndianConvert 功能相同，但通过指针操作，适用于已序列化的数据。
 *
 * @tparam T 数据类型
 * @param val 指向数据的指针
 */
template<typename T> inline void EndianConvertPtr(void* val) { ByteConverter::apply<T>(val); }

/**
 * @brief 大端序平台：通过指针操作大端序数据（无需转换）
 *
 * @details 数据已是正确格式，空实现。
 *
 * @tparam T 数据类型
 * @param val 指向数据的指针（不执行任何操作）
 */
template<typename T> inline void EndianConvertPtrReverse(void*) { }
#else
/**
 * @brief 小端序平台：数据已是平台格式（小端序），无需转换
 *
 * @details 在小端序平台上，TrinityCore 的内部数据格式与平台一致，无需转换。
 *
 * @tparam T 数据类型
 * @param val 数据引用（不执行任何操作）
 */
template<typename T> inline void EndianConvert(T&) { }

/**
 * @brief 小端序平台：将大端序数据转换为平台格式（小端序）
 *
 * @details 在小端序平台上，接收到的大端序数据需要转换为小端序。
 * 这通常用于处理网络数据或跨平台存储的数据。
 *
 * @tparam T 数据类型
 * @param val 要转换的数据引用
 *
 * @note 仅在小端序平台上执行实际转换
 */
template<typename T> inline void EndianConvertReverse(T& val) { ByteConverter::apply<T>(&val); }

/**
 * @brief 小端序平台：通过指针操作小端序数据（无需转换）
 *
 * @details 数据已是正确格式，空实现。
 *
 * @tparam T 数据类型
 * @param val 指向数据的指针（不执行任何操作）
 */
template<typename T> inline void EndianConvertPtr(void*) { }

/**
 * @brief 小端序平台：通过指针转换大端序数据为小端序
 *
 * @details 与 EndianConvertReverse 功能相同，但通过指针操作。
 *
 * @tparam T 数据类型
 * @param val 指向数据的指针
 */
template<typename T> inline void EndianConvertPtrReverse(void* val) { ByteConverter::apply<T>(val); }
#endif

/** @} */ // End of EndianConvertFunctions group

/**
 * @brief 禁止对指针类型使用引用转换函数（编译时链接错误）
 *
 * @details 这些声明故意不提供实现，如果用户尝试将指针类型传给引用版本的转换函数，
 * 将在链接阶段报错，提醒用户应该使用指针版本（EndianConvertPtr）。
 *
 * 这是一种编译时安全机制，防止误用：
 * - 错误用法：EndianConvert<int*>(&ptr)
 * - 正确用法：EndianConvertPtr<int>(ptr)
 *
 * @tparam T 数据类型
 */
template<typename T> void EndianConvert(T*);         // will generate link error
template<typename T> void EndianConvertReverse(T*);  // will generate link error

/**
 * @brief 单字节类型的特化：uint8_t 无需转换
 *
 * @details 单字节数据不存在字节序问题，直接空实现。
 * 这避免了不必要的模板实例化，提高编译效率。
 *
 * @param val uint8_t 数据引用（不执行任何操作）
 */
inline void EndianConvert(uint8&) { }

/**
 * @brief 单字节类型的特化：int8_t 无需转换
 *
 * @details 单字节数据不存在字节序问题，直接空实现。
 *
 * @param val int8_t 数据引用（不执行任何操作）
 */
inline void EndianConvert( int8&) { }

/**
 * @brief 单字节类型的特化：uint8_t 反向转换无需操作
 *
 * @details 单字节数据不分大小端，反向转换也是空操作。
 *
 * @param val uint8_t 数据引用（不执行任何操作）
 */
inline void EndianConvertReverse(uint8&) { }

/**
 * @brief 单字节类型的特化：int8_t 反向转换无需操作
 *
 * @details 单字节数据不分大小端，反向转换也是空操作。
 *
 * @param val int8_t 数据引用（不执行任何操作）
 */
inline void EndianConvertReverse( int8&) { }

#endif
