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
 * @file SFMTRand.h
 * @brief SFMT 随机数生成器封装类
 *
 * 本文件提供了 SFMT (SIMD-oriented Fast Mersenne Twister) 随机数生成器的 C++ 封装。
 * SFMT 是一种高速、高质量的伪随机数生成算法，是 Mersenne Twister 算法的改进版本，
 * 专为现代处理器的 SIMD 指令集优化。
 *
 * 主要特性：
 * - 周期长：可达 2^216091 - 1
 * - 高速：利用 SIMD 指令并行计算
 * - 高质量：通过 TestU01 测试套件
 * - 内存对齐要求：需要 16 字节对齐以支持 SSE2 指令
 */

#ifndef SFMTRand_h__
#define SFMTRand_h__

#include "Define.h"
#include <SFMT.h>
#include <new>

/**
 * @class SFMTRand
 * @brief SFMT 随机数生成器的 C++ 封装类
 *
 * 该类封装了 SFMT 随机数生成算法，提供简单易用的随机数生成接口。
 *
 * @details
 * SFMT 算法特点：
 * - 基于 Mersenne Twister 算法的改进版本
 * - 利用 SIMD 指令（如 SSE2, Altivec）实现并行计算
 * - 状态空间大（256 个 128 位整数），周期极长
 * - 生成速度快，比标准 Mersenne Twister 快 2-4 倍
 * - 统计学特性优异，通过严格的随机性测试
 *
 * 性能特性：
 * - 适用于需要大量随机数的高性能场景
 * - 线程不安全，每个线程应使用独立实例
 * - 初始化开销较大，建议长期使用而非频繁创建销毁
 *
 * 使用示例：
 * @code
 * SFMTRand rng;
 * uint32 randomValue = rng.RandomUInt32();
 * @endcode
 */
class SFMTRand {
public:
    /**
     * @brief 构造函数，使用随机种子初始化生成器状态
     *
     * 使用系统提供的随机源初始化内部状态，确保每次运行产生不同的随机序列。
     */
    SFMTRand();

    /**
     * @brief 生成 32 位随机无符号整数
     *
     * @return uint32 返回一个 32 位随机无符号整数，范围为 [0, UINT32_MAX]
     *
     * @details
     * 该函数从 SFMT 算法生成器中提取下一个随机数。
     * 生成的随机数在 32 位整数范围内均匀分布。
     */
    uint32 RandomUInt32();

    /**
     * @brief 不抛出异常的 new 操作符（单个对象）
     *
     * @param size 要分配的内存大小（字节）
     * @return void* 返回分配的内存指针，失败时返回 nullptr
     *
     * @details
     * SFMT 算法要求内部状态必须按特定字节对齐（通常 16 字节）以支持 SIMD 指令。
     * 此自定义操作符确保分配的内存满足对齐要求。
     */
    void* operator new(size_t size, std::nothrow_t const&);

    /**
     * @brief 不抛出异常的 delete 操作符（单个对象）
     *
     * @param ptr 要释放的内存指针
     *
     * @details
     * 与自定义 new 操作符配对使用，释放对齐分配的内存。
     */
    void operator delete(void* ptr, std::nothrow_t const&);

    /**
     * @brief 标准 new 操作符（单个对象）
     *
     * @param size 要分配的内存大小（字节）
     * @return void* 返回分配的内存指针
     * @throws std::bad_alloc 内存分配失败时抛出异常
     *
     * @details
     * SFMT 算法要求内部状态必须按特定字节对齐（通常 16 字节）以支持 SIMD 指令。
     * 此自定义操作符确保分配的内存满足对齐要求。
     */
    void* operator new(size_t size);

    /**
     * @brief 标准 delete 操作符（单个对象）
     *
     * @param ptr 要释放的内存指针
     *
     * @details
     * 与自定义 new 操作符配对使用，释放对齐分配的内存。
     */
    void operator delete(void* ptr);

    /**
     * @brief 不抛出异常的 new[] 操作符（数组）
     *
     * @param size 要分配的内存大小（字节）
     * @return void* 返回分配的内存指针，失败时返回 nullptr
     *
     * @details
     * 为 SFMTRand 数组分配对齐内存。
     * 数组中每个对象都需要满足内存对齐要求。
     */
    void* operator new[](size_t size, std::nothrow_t const&);

    /**
     * @brief 不抛出异常的 delete[] 操作符（数组）
     *
     * @param ptr 要释放的内存指针
     *
     * @details
     * 与自定义 new[] 操作符配对使用，释放数组内存。
     */
    void operator delete[](void* ptr, std::nothrow_t const&);

    /**
     * @brief 标准 new[] 操作符（数组）
     *
     * @param size 要分配的内存大小（字节）
     * @return void* 返回分配的内存指针
     * @throws std::bad_alloc 内存分配失败时抛出异常
     *
     * @details
     * 为 SFMTRand 数组分配对齐内存。
     * 数组中每个对象都需要满足内存对齐要求。
     */
    void* operator new[](size_t size);

    /**
     * @brief 标准 delete[] 操作符（数组）
     *
     * @param ptr 要释放的内存指针
     *
     * @details
     * 与自定义 new[] 操作符配对使用，释放数组内存。
     */
    void operator delete[](void* ptr);

private:
    sfmt_t _state;  ///< SFMT 算法的内部状态结构体，包含 256 个 128 位整数的状态数组
};

#endif // SFMTRand_h__
