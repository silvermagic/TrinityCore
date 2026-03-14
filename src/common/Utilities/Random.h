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
 * @file Random.h
 * @brief 随机数生成模块头文件
 *
 * 本模块提供了完整的随机数生成功能，包括：
 * - 整数随机数生成（有符号和无符号）
 * - 浮点数随机数生成
 * - 时间间隔随机数生成
 * - 百分比概率判定
 * - 加权随机选择
 * - 随机数引擎封装类
 *
 * 该模块使用了 SFMT (SIMD-oriented Fast Mersenne Twister) 算法作为高性能随机数生成器，
 * 并提供了符合 C++ 标准库 UniformRandomNumberGenerator 概念的封装类。
 *
 * 主要用途：
 * - 游戏中的随机事件判定（如暴击、闪避等）
 * - 随机时间延迟
 * - 随机位置生成
 * - 掉落概率计算
 */

#ifndef Random_h__
#define Random_h__

#include "Define.h"
#include "Duration.h"
#include <limits>

/**
 * @brief 生成指定范围内的随机整数（有符号 32 位）
 *
 * @param min 范围的最小值（包含）
 * @param max 范围的最大值（包含）
 * @return int32 生成的随机整数，范围在 [min, max] 之间
 *
 * @note 使用标准 C++ 均匀分布，确保每个值被选中的概率相等
 */
TC_COMMON_API int32 irand(int32 min, int32 max);

/**
 * @brief 生成指定范围内的随机整数（无符号 32 位）
 *
 * @param min 范围的最小值（包含）
 * @param max 范围的最大值（包含）
 * @return uint32 生成的随机无符号整数，范围在 [min, max] 之间
 *
 * @note 使用标准 C++ 均匀分布，确保每个值被选中的概率相等
 */
TC_COMMON_API uint32 urand(uint32 min, uint32 max);

/**
 * @brief 生成指定毫秒范围内的随机值
 *
 * @param min 毫秒范围的最小值（包含）
 * @param max 毫秒范围的最大值（包含）
 * @return uint32 生成的随机值，单位由 Milliseconds::period 决定
 *
 * @note 功能上等同于 urand(min * IN_MILLISECONDS, max * IN_MILLISECONDS)
 *       会自动进行时间单位转换
 */
TC_COMMON_API uint32 urandms(uint32 min, uint32 max);

/**
 * @brief 生成 32 位随机无符号整数
 *
 * @return uint32 生成的随机无符号整数，范围 [0, UINT32_MAX]
 *
 * @note 使用 SFMT 算法生成，性能优于标准库的 rand()
 */
TC_COMMON_API uint32 rand32();

/**
 * @brief 生成指定时间范围内的随机时间间隔
 *
 * @param min 最小时间间隔（毫秒）
 * @param max 最大时间间隔（毫秒）
 * @return Milliseconds 生成的随机时间间隔
 *
 * @note 仅适用于毫秒差值在 uint32 范围内的值
 */
TC_COMMON_API Milliseconds randtime(Milliseconds min, Milliseconds max);

/**
 * @brief 生成指定范围内的随机浮点数
 *
 * @param min 范围的最小值（包含）
 * @param max 范围的最大值（包含）
 * @return float 生成的随机浮点数，范围在 [min, max] 之间
 */
TC_COMMON_API float frand(float min, float max);

/**
 * @brief 生成归一化的随机浮点数
 *
 * @return double 生成的随机双精度浮点数，范围在 [0.0, 1.0) 之间
 *
 * @note 适用于需要归一化随机值的场景，如随机方向向量生成
 */
TC_COMMON_API double rand_norm();

/**
 * @brief 生成随机百分比数值
 *
 * @return double 生成的随机百分比数值，范围在 [0.0, 100.0] 之间
 *
 * @note 主要用于游戏中的概率计算、掉落判定等场景
 */
TC_COMMON_API double rand_chance();

/**
 * @brief 根据权重生成随机索引
 *
 * @param count 权重数组的元素个数
 * @param chances 权重数组指针，每个元素表示对应索引的权重
 * @return uint32 根据权重随机选择的索引值，范围 [0, count-1]
 *
 * @note 索引被选中的概率与其对应的权重成正比
 * @example
 *   double weights[] = {1.0, 2.0, 3.0}; // 总权重 6.0
 *   uint32 index = urandweighted(3, weights);
 *   // 索引 0 被选中概率为 1/6
 *   // 索引 1 被选中概率为 2/6
 *   // 索引 2 被选中概率为 3/6
 */
TC_COMMON_API uint32 urandweighted(size_t count, double const* chances);

/**
 * @brief 浮点数概率判定（百分比）
 *
 * @param chance 成功概率，范围 [0.0, 100.0]
 * @return bool 如果随机掷骰值小于 chance 则返回 true，否则返回 false
 *
 * @note 用于浮点数概率判定，如技能命中率、暴击率等
 */
inline bool roll_chance_f(float chance)
{
    return chance > rand_chance();
}

/**
 * @brief 整数概率判定（百分比）
 *
 * @param chance 成功概率，范围 [0, 99]
 * @return bool 如果随机掷骰值小于 chance 则返回 true，否则返回 false
 *
 * @note 用于整数概率判定，使用 irand(0, 99) 生成随机值
 */
inline bool roll_chance_i(int chance)
{
    return chance > irand(0, 99);
}

/**
 * @class RandomEngine
 * @brief 随机数引擎封装类
 *
 * 该类是对底层随机数生成器的封装，满足 C++ 标准库的 UniformRandomNumberGenerator 概念。
 * 可以用于 C++ 标准库的随机数分布类（如 std::uniform_int_distribution）。
 *
 * 主要特点：
 * - 提供 min() 和 max() 静态方法返回随机数范围
 * - 提供 operator() 生成随机数
 * - 使用 SFMT 算法作为底层实现
 *
 * 使用示例：
 * @code
 *   RandomEngine& engine = RandomEngine::Instance();
 *   std::uniform_int_distribution<int32> dist(1, 100);
 *   int32 random_value = dist(engine);
 * @endcode
 */
class TC_COMMON_API RandomEngine
{
public:
    typedef uint32 result_type;  ///< 随机数结果类型定义

    /**
     * @brief 获取随机数的最小值
     * @return 随机数最小值，始终为 0
     */
    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }

    /**
     * @brief 获取随机数的最大值
     * @return 随机数最大值，UINT32_MAX
     */
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

    /**
     * @brief 生成一个随机数
     * @return 范围在 [min(), max()] 之间的随机无符号整数
     */
    result_type operator()() const { return rand32(); }

    /**
     * @brief 获取全局随机引擎实例
     * @return 全局随机引擎的单例引用
     *
     * @note 返回的全局实例可被多个线程同时使用
     */
    static RandomEngine& Instance();
};

#endif // Random_h__
