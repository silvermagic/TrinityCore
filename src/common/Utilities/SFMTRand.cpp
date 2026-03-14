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
 * @file SFMTRand.cpp
 * @brief SFMT（SIMD-oriented Fast Mersenne Twister）随机数生成器实现
 *
 * SFMT 是 Mersenne Twister 随机数生成器的优化版本，具有以下特点：
 * - 使用 SIMD 指令进行加速
 * - 周期更长（2^19937-1）
 * - 速度更快，统计特性更好
 * - 支持多线程安全
 */

#include "SFMTRand.h"
#include <algorithm>
#include <array>
#include <functional>
#include <random>
#include <ctime>

/* ============================================================================
 * 平台兼容性：内存对齐分配函数
 * ============================================================================
 * SFMT 需要 16 字节对齐的内存以提高 SIMD 性能
 * 以下代码为不同平台提供统一的内存对齐分配接口
 */

#if __has_include(<mm_malloc.h>)
// POSIX 系统通常提供 mm_malloc.h
#include <mm_malloc.h>
#elif __has_include(<malloc.h>) && TRINITY_COMPILER == TRINITY_COMPILER_MICROSOFT
// Microsoft Visual Studio 提供 malloc.h
#include <malloc.h>
#else
/**
 * @brief 对齐内存分配函数（跨平台兼容实现）
 *
 * 职责：
 *   分配指定大小和对齐方式的内存块，用于支持需要特殊对齐的数据结构
 *
 * 参数：
 *   __size  - 要分配的内存大小（字节）
 *   __align - 内存对齐字节数（必须是 2 的幂次方）
 *
 * 返回值：
 *   成功：指向对齐内存块的指针
 *   失败：NULL
 *
 * 主要流程：
 *   1. 如果对齐值为 1，直接使用标准 malloc
 *   2. 验证对齐值是否为 2 的幂次方，并确保最小对齐为指针大小
 *   3. 使用 posix_memalign 分配对齐内存
 */
static __inline__ void *__attribute__((__always_inline__, __nodebug__, __malloc__))
_mm_malloc(size_t __size, size_t __align)
{
    // 对齐值为 1 表示无特殊对齐要求，使用标准 malloc
    if (__align == 1)
    {
        return malloc(__size);
    }

    // 确保对齐值至少为指针大小（保证指针存储的正确性）
    // (__align & (__align - 1)) == 0 用于验证 __align 是 2 的幂次方
    if (!(__align & (__align - 1)) && __align < sizeof(void *))
        __align = sizeof(void *);

    void *__mallocedMemory;

    // 使用 posix_memalign 分配对齐内存
    // 返回值非 0 表示失败
    if (posix_memalign(&__mallocedMemory, __align, __size))
        return NULL;

    return __mallocedMemory;
}

/**
 * @brief 释放对齐内存函数（跨平台兼容实现）
 *
 * 职责：
 *   释放由 _mm_malloc 分配的对齐内存
 *
 * 参数：
 *   __p - 要释放的内存指针
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   直接调用标准 free 函数释放内存
 */
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_free(void *__p)
{
    free(__p);
}
#endif

/**
 * @brief SFMTRand 构造函数
 *
 * 职责：
 *   初始化 SFMT 随机数生成器的内部状态，使用高质量的种子值确保随机性
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 检查系统是否提供真随机数设备（熵源 > 0）
 *   2. 如果有真随机源：
 *      - 生成包含 SFMT_N32 个 32 位随机数的种子数组
 *      - 使用种子数组初始化 SFMT 状态
 *   3. 如果没有真随机源（熵源 = 0）：
 *      - 使用当前时间戳作为种子
 *      - 用单一种子初始化 SFMT 状态
 *
 * 说明：
 *   SFMT_N32 是 SFMT 算法定义的常量，表示状态数组中 32 位整数的数量
 *   使用多个种子值可以获得更好的初始随机性分布
 */
SFMTRand::SFMTRand()
{
    // 创建随机设备对象（可能是硬件随机源或操作系统提供的随机源）
    std::random_device dev;

    // 检查随机设备的熵值（熵 > 0 表示有真随机源）
    if (dev.entropy() > 0)
    {
        // 创建包含 SFMT_N32 个 32 位整数的种子数组
        std::array<uint32, SFMT_N32> seed;

        // 使用随机设备填充种子数组的每个元素
        std::generate(seed.begin(), seed.end(), std::ref(dev));

        // 使用种子数组初始化 SFMT 状态（提供更好的随机性）
        sfmt_init_by_array(&_state, seed.data(), seed.size());
    }
    else
    {
        // 没有真随机源时，使用当前时间戳作为种子
        // 这是一个回退方案，随机性较弱但可接受
        sfmt_init_gen_rand(&_state, uint32(time(nullptr)));
    }
}

/**
 * @brief 生成 32 位无符号随机整数
 *
 * 职责：
 *   从 SFMT 随机数生成器中生成一个 32 位无符号随机整数
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   uint32 - 32 位无符号随机整数（范围：0 到 2^32-1）
 *
 * 主要流程：
 *   调用 SFMT 核心函数 sfmt_genrand_uint32 生成随机数
 *
 * 说明：
 *   该函数是线程安全的，每个 SFMTRand 实例维护独立的状态
 *   生成的随机数均匀分布在 0 到 4294967295 范围内
 */
uint32 SFMTRand::RandomUInt32()
{
    // 调用 SFMT 核心函数生成 32 位随机数
    return sfmt_genrand_uint32(&_state);
}

/* ============================================================================
 * 自定义内存管理运算符重载
 * ============================================================================
 * SFMTRand 对象必须分配在 16 字节对齐的内存地址上，以支持 SIMD 指令优化
 * 以下运算符重载确保所有 SFMTRand 对象的分配都满足对齐要求
 */

/**
 * @brief new 运算符重载（nothrow 版本）
 *
 * 职责：
 *   分配 16 字节对齐的内存用于创建单个 SFMTRand 对象，失败时不抛出异常
 *
 * 参数：
 *   size    - 要分配的内存大小（字节）
 *   nothrow - nothrow 标志，指示分配失败时返回 nullptr 而非抛出异常
 *
 * 返回值：
 *   成功：指向 16 字节对齐内存的指针
 *   失败：nullptr（不抛出异常）
 *
 * 主要流程：
 *   调用 _mm_malloc 分配 16 字节对齐的内存
 */
void* SFMTRand::operator new(size_t size, std::nothrow_t const&)
{
    return _mm_malloc(size, 16);
}

/**
 * @brief delete 运算符重载（nothrow 版本）
 *
 * 职责：
 *   释放由 new(nothrow) 分配的对齐内存
 *
 * 参数：
 *   ptr     - 要释放的内存指针
 *   nothrow - nothrow 标志（与 new 版本匹配）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   调用 _mm_free 释放对齐内存
 */
void SFMTRand::operator delete(void* ptr, std::nothrow_t const&)
{
    _mm_free(ptr);
}

/**
 * @brief new 运算符重载（标准版本）
 *
 * 职责：
 *   分配 16 字节对齐的内存用于创建单个 SFMTRand 对象
 *
 * 参数：
 *   size - 要分配的内存大小（字节）
 *
 * 返回值：
 *   成功：指向 16 字节对齐内存的指针
 *   失败：抛出 std::bad_alloc 异常
 *
 * 主要流程：
 *   调用 _mm_malloc 分配 16 字节对齐的内存
 */
void* SFMTRand::operator new(size_t size)
{
    return _mm_malloc(size, 16);
}

/**
 * @brief delete 运算符重载（标准版本）
 *
 * 职责：
 *   释放由 new 分配的对齐内存
 *
 * 参数：
 *   ptr - 要释放的内存指针
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   调用 _mm_free 释放对齐内存
 */
void SFMTRand::operator delete(void* ptr)
{
    _mm_free(ptr);
}

/**
 * @brief new[] 运算符重载（nothrow 版本）
 *
 * 职责：
 *   分配 16 字节对齐的内存用于创建 SFMTRand 对象数组，失败时不抛出异常
 *
 * 参数：
 *   size    - 要分配的内存大小（字节，包含所有数组元素）
 *   nothrow - nothrow 标志，指示分配失败时返回 nullptr 而非抛出异常
 *
 * 返回值：
 *   成功：指向 16 字节对齐内存的指针
 *   失败：nullptr（不抛出异常）
 *
 * 主要流程：
 *   调用 _mm_malloc 分配 16 字节对齐的内存
 */
void* SFMTRand::operator new[](size_t size, std::nothrow_t const&)
{
    return _mm_malloc(size, 16);
}

/**
 * @brief delete[] 运算符重载（nothrow 版本）
 *
 * 职责：
 *   释放由 new[](nothrow) 分配的对齐内存数组
 *
 * 参数：
 *   ptr     - 要释放的内存指针
 *   nothrow - nothrow 标志（与 new[] 版本匹配）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   调用 _mm_free 释放对齐内存
 */
void SFMTRand::operator delete[](void* ptr, std::nothrow_t const&)
{
    _mm_free(ptr);
}

/**
 * @brief new[] 运算符重载（标准版本）
 *
 * 职责：
 *   分配 16 字节对齐的内存用于创建 SFMTRand 对象数组
 *
 * 参数：
 *   size - 要分配的内存大小（字节，包含所有数组元素）
 *
 * 返回值：
 *   成功：指向 16 字节对齐内存的指针
 *   失败：抛出 std::bad_alloc 异常
 *
 * 主要流程：
 *   调用 _mm_malloc 分配 16 字节对齐的内存
 */
void* SFMTRand::operator new[](size_t size)
{
    return _mm_malloc(size, 16);
}

/**
 * @brief delete[] 运算符重载（标准版本）
 *
 * 职责：
 *   释放由 new[] 分配的对齐内存数组
 *
 * 参数：
 *   ptr - 要释放的内存指针
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   调用 _mm_free 释放对齐内存
 */
void SFMTRand::operator delete[](void* ptr)
{
    _mm_free(ptr);
}
