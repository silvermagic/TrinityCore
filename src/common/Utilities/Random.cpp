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
 * @file Random.cpp
 * @brief 随机数生成模块实现文件
 *
 * 本文件实现了完整的随机数生成功能，包括：
 * - 整数随机数生成（有符号和无符号）
 * - 浮点数随机数生成
 * - 时间间隔随机数生成
 * - 百分比概率判定
 * - 加权随机选择
 *
 * 实现特点：
 * - 使用 SFMT (SIMD-oriented Fast Mersenne Twister) 算法作为高性能随机数生成器
 * - 线程本地存储确保每个线程有独立的随机数状态，避免竞争
 * - 提供标准 C++ 随机数引擎接口，兼容标准库算法
 *
 * 性能说明：
 * - SFMT 算法比标准 Mersenne Twister 快 2-4 倍
 * - 线程本地存储避免了多线程环境下的锁竞争
 * - 使用标准 C++ 分布类确保均匀性和正确性
 */

#include "Random.h"
#include "Errors.h"
#include "SFMTRand.h"
#include <memory>
#include <random>

/// 线程本地的 SFMT 随机数生成器实例
/// SFMT (SIMD-oriented Fast Mersenne Twister) 是一种高性能的伪随机数生成算法
/// 每个线程都有独立的随机数生成器实例，确保线程安全且避免竞争
static thread_local std::unique_ptr<SFMTRand> sfmtRand;

/// 全局随机引擎实例，用于标准 C++ 随机数分布
/// 该实例实现了 UniformRandomNumberGenerator 概念，可与标准库分布类配合使用
static RandomEngine engine;

/**
 * @brief 获取线程本地的 SFMT 随机数生成器实例
 *
 * 职责：
 *   获取或创建线程本地的 SFMT 随机数生成器实例，确保每个线程都有独立的随机数状态
 *
 * 返回值：
 *   SFMTRand* - 指向当前线程的 SFMT 随机数生成器的指针
 *
 * 主要流程：
 *   1. 检查当前线程是否已创建 SFMT 随机数生成器
 *   2. 如果未创建，则创建一个新的实例
 *   3. 返回生成器的原始指针
 */
static SFMTRand* GetRng()
{
    if (!sfmtRand)
        sfmtRand = std::make_unique<SFMTRand>();

    return sfmtRand.get();
}

/**
 * @brief 生成指定范围内的随机整数（有符号 32 位）
 *
 * 职责：
 *   生成一个在 [min, max] 范围内的均匀分布的随机整数
 *
 * 参数：
 *   min - 范围的最小值（包含）
 *   max - 范围的最大值（包含）
 *
 * 返回值：
 *   int32 - 生成的随机整数，范围在 [min, max] 之间
 *
 * 主要流程：
 *   1. 断言检查 max >= min，确保范围有效
 *   2. 创建均匀整数分布对象
 *   3. 使用全局随机引擎生成随机数并返回
 */
int32 irand(int32 min, int32 max)
{
    ASSERT(max >= min);
    std::uniform_int_distribution<int32> uid(min, max);
    return uid(engine);
}

/**
 * @brief 生成指定范围内的随机整数（无符号 32 位）
 *
 * 职责：
 *   生成一个在 [min, max] 范围内的均匀分布的随机无符号整数
 *
 * 参数：
 *   min - 范围的最小值（包含）
 *   max - 范围的最大值（包含）
 *
 * 返回值：
 *   uint32 - 生成的随机无符号整数，范围在 [min, max] 之间
 *
 * 主要流程：
 *   1. 断言检查 max >= min，确保范围有效
 *   2. 创建均匀整数分布对象
 *   3. 使用全局随机引擎生成随机数并返回
 */
uint32 urand(uint32 min, uint32 max)
{
    ASSERT(max >= min);
    std::uniform_int_distribution<uint32> uid(min, max);
    return uid(engine);
}

/**
 * @brief 生成指定毫秒范围内的随机值
 *
 * 职责：
 *   生成一个在 [min, max] 毫秒范围内的随机值，自动转换为时间单位
 *
 * 参数：
 *   min - 毫秒范围的最小值（包含）
 *   max - 毫秒范围的最大值（包含）
 *
 * 返回值：
 *   uint32 - 生成的随机值，单位由 Milliseconds::period 决定
 *
 * 主要流程：
 *   1. 断言检查最大值不会导致溢出
 *   2. 将毫秒值乘以时间单位转换因子
 *   3. 调用 urand 生成随机值并返回
 */
uint32 urandms(uint32 min, uint32 max)
{
    ASSERT(std::numeric_limits<uint32>::max() / Milliseconds::period::den >= max);
    return urand(min * Milliseconds::period::den, max * Milliseconds::period::den);
}

/**
 * @brief 生成指定范围内的随机浮点数
 *
 * 职责：
 *   生成一个在 [min, max] 范围内的均匀分布的随机浮点数
 *
 * 参数：
 *   min - 范围的最小值（包含）
 *   max - 范围的最大值（包含）
 *
 * 返回值：
 *   float - 生成的随机浮点数，范围在 [min, max] 之间
 *
 * 主要流程：
 *   1. 断言检查 max >= min，确保范围有效
 *   2. 创建均匀实数分布对象
 *   3. 使用全局随机引擎生成随机数并返回
 */
float frand(float min, float max)
{
    ASSERT(max >= min);
    std::uniform_real_distribution<float> urd(min, max);
    return urd(engine);
}

/**
 * @brief 生成指定时间范围内的随机时间间隔
 *
 * 职责：
 *   生成一个在 [min, max] 时间范围内的随机毫秒时间间隔
 *
 * 参数：
 *   min - 最小时间间隔（毫秒）
 *   max - 最大时间间隔（毫秒）
 *
 * 返回值：
 *   Milliseconds - 生成的随机时间间隔
 *
 * 主要流程：
 *   1. 计算最大值和最小值的差值
 *   2. 断言检查差值非负且不超过 uint32 范围
 *   3. 在 [0, diff] 范围内生成随机值
 *   4. 将随机值加到最小时间上并返回
 */
Milliseconds randtime(Milliseconds min, Milliseconds max)
{
    long long diff = max.count() - min.count();
    ASSERT(diff >= 0);
    ASSERT(diff <= (uint32)-1);
    return min + Milliseconds(urand(0, diff));
}

/**
 * @brief 生成 32 位随机无符号整数
 *
 * 职责：
 *   使用 SFMT 算法生成一个 32 位随机无符号整数
 *
 * 返回值：
 *   uint32 - 生成的随机无符号整数，范围 [0, UINT32_MAX]
 *
 * 主要流程：
 *   1. 获取线程本地的 SFMT 随机数生成器
 *   2. 调用其 RandomUInt32 方法生成随机数
 *   3. 返回生成的随机数
 */
uint32 rand32()
{
    return GetRng()->RandomUInt32();
}

/**
 * @brief 生成归一化的随机浮点数
 *
 * 职责：
 *   生成一个在 [0.0, 1.0) 范围内的均匀分布的随机双精度浮点数
 *
 * 返回值：
 *   double - 生成的随机双精度浮点数，范围在 [0.0, 1.0) 之间
 *
 * 主要流程：
 *   1. 创建默认参数的均匀实数分布对象（默认范围 [0.0, 1.0)）
 *   2. 使用全局随机引擎生成随机数并返回
 */
double rand_norm()
{
    std::uniform_real_distribution<double> urd;
    return urd(engine);
}

/**
 * @brief 生成随机百分比数值
 *
 * 职责：
 *   生成一个在 [0.0, 100.0] 范围内的均匀分布的随机双精度浮点数
 *   通常用于游戏中的概率计算、掉落判定等场景
 *
 * 返回值：
 *   double - 生成的随机百分比数值，范围在 [0.0, 100.0] 之间
 *
 * 主要流程：
 *   1. 创建指定范围为 [0.0, 100.0] 的均匀实数分布对象
 *   2. 使用全局随机引擎生成随机数并返回
 */
double rand_chance()
{
    std::uniform_real_distribution<double> urd(0.0, 100.0);
    return urd(engine);
}

/**
 * @brief 根据权重生成随机索引
 *
 * 职责：
 *   根据提供的权重数组，使用离散分布生成一个随机索引
 *   索引被选中的概率与其对应的权重成正比
 *
 * 参数：
 *   count - 权重数组的元素个数
 *   chances - 权重数组指针，每个元素表示对应索引的权重
 *
 * 返回值：
 *   uint32 - 根据权重随机选择的索引值，范围 [0, count-1]
 *
 * 主要流程：
 *   1. 使用权重数组创建离散分布对象
 *   2. 使用全局随机引擎生成随机索引
 *   3. 返回生成的索引值
 *
 * 示例：
 *   double weights[] = {1.0, 2.0, 3.0}; // 总权重 6.0
 *   uint32 index = urandweighted(3, weights);
 *   // 索引 0 被选中概率为 1/6
 *   // 索引 1 被选中概率为 2/6
 *   // 索引 2 被选中概率为 3/6
 */
uint32 urandweighted(size_t count, double const* chances)
{
    std::discrete_distribution<uint32> dd(chances, chances + count);
    return dd(engine);
}

/**
 * @brief 获取全局随机引擎实例的引用
 *
 * 职责：
 *   提供对全局随机引擎实例的访问接口，允许外部代码使用该引擎
 *
 * 返回值：
 *   RandomEngine& - 全局随机引擎实例的引用
 *
 * 主要流程：
 *   1. 返回静态全局随机引擎实例 engine 的引用
 */
RandomEngine& RandomEngine::Instance()
{
    return engine;
}
