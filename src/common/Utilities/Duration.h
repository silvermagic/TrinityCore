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

// ============================================================================
// Duration.h - 时间类型定义库
// ============================================================================
// 模块职责：
//   定义时间相关的类型别名和字面量，简化时间计算和表达。
//   基于 C++11 chrono 库，提供强类型的时间单位。
//
// 使用场景：
//   - 技能冷却时间
//   - 法术持续时间
//   - 任务时间限制
//   - 事件调度时间
//
// 设计理念：
//   - 使用强类型避免单位混淆
//   - 提供简洁的类型别名
//   - 支持字面量表示（如 5s, 100ms）
//
// 示例：
//   Milliseconds duration = 500ms;      // 500 毫秒
//   Seconds cooldown = 10s;             // 10 秒
//   TimePoint endTime = now + 1min;     // 当前时间 + 1 分钟
// ============================================================================

#ifndef _DURATION_H_
#define _DURATION_H_

// HACKS TERRITORY
// 注意：以下代码是为了避免引入 MSVC 的 chrono 实现中的额外依赖
//#if __has_include(<__msvc_chrono.hpp>)
//#include <__msvc_chrono.hpp> // skip all the formatting/istream/locale/mutex bloat
//#else
#include <chrono>
//#endif

/// 毫秒类型简写
/// 用于表示游戏中的大部分时间单位（技能CD、法术持续时间等）
typedef std::chrono::milliseconds Milliseconds;

/// 秒类型简写
/// 用于表示较长的时间间隔（任务时间、Buff持续时间等）
typedef std::chrono::seconds Seconds;

/// 分钟类型简写
/// 用于表示更长的时间间隔（副本重置时间、刷新时间等）
typedef std::chrono::minutes Minutes;

/// 小时类型简写
/// 用于表示很长的时间间隔（日常任务重置等）
typedef std::chrono::hours Hours;

/// 时间点类型简写（单调时钟）
/// 用于计时器和事件调度，不受系统时间调整影响
typedef std::chrono::steady_clock::time_point TimePoint;

/// 系统时间点类型简写（系统时钟）
/// 用于记录真实的日期时间，受系统时间调整影响
typedef std::chrono::system_clock::time_point SystemTimePoint;

/// 使 std::chrono_literals 全局可用
/// 允许使用字面量如 100ms, 5s, 1min, 1h
using namespace std::chrono_literals;

/**
 * @brief 天数字面量操作符
 *
 * @param days 天数
 * @return 对应的小时数
 *
 * 将天数转换为小时，因为 std::chrono 没有提供 days 类型。
 *
 * 示例：
 *   auto time = 2_days;  // 等价于 48h
 *
 * 调用时机：
 *   - 需要以天为单位表达时间间隔时
 *   - 例如：日常任务重置时间为 1_days
 */
constexpr std::chrono::hours operator""_days(unsigned long long days)
{
    return std::chrono::hours(days * 24h);
}

#endif // _DURATION_H_
