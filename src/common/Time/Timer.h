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
 * @file Timer.h
 * @brief 计时器模块 - 提供时间测量和定时器功能
 *
 * 模块职责：
 *   - 提供应用程序启动时间的获取
 *   - 提供毫秒级时间戳的获取和计算
 *   - 提供多种定时器实现（间隔定时器、时间追踪器、周期定时器）
 *   - 支持时间差的计算和溢出处理
 *
 * 主要组件：
 *   - GetApplicationStartTime: 获取应用程序启动时间
 *   - getMSTime: 获取毫秒级时间戳
 *   - IntervalTimer: 间隔定时器，用于定期触发事件
 *   - TimeTracker: 时间追踪器，用于追踪剩余时间
 *   - PeriodicTimer: 周期定时器，用于周期性触发事件
 */

#ifndef TRINITY_TIMER_H
#define TRINITY_TIMER_H

#include "Define.h"
#include "Duration.h"

/**
 * @brief 获取应用程序启动时间
 * @return 返回应用程序启动时的steady_clock时间点
 *
 * 职责：
 *   返回应用程序启动时的单调时钟时间点，用作时间基准
 *   使用静态变量确保只计算一次，后续调用直接返回缓存值
 *
 * 性能注意事项：
 *   使用静态变量缓存，避免重复计算
 */
inline std::chrono::steady_clock::time_point GetApplicationStartTime()
{
    using namespace std::chrono;

    static const steady_clock::time_point ApplicationStartTime = steady_clock::now();

    return ApplicationStartTime;
}

/**
 * @brief 获取毫秒级时间戳
 * @return 返回从应用程序启动到现在经过的毫秒数
 *
 * 职责：
 *   计算从应用程序启动到现在经过的时间（毫秒）
 *   使用steady_clock确保时间单调递增，不受系统时间调整影响
 *
 * 性能注意事项：
 *   调用steady_clock::now()，开销较小但非零
 *   返回值会在约49.7天后溢出（uint32最大值）
 */
inline uint32 getMSTime()
{
    using namespace std::chrono;

    return uint32(duration_cast<milliseconds>(steady_clock::now() - GetApplicationStartTime()).count());
}

/**
 * @brief 计算两个毫秒时间戳之间的时间差
 * @param oldMSTime 旧的时间戳
 * @param newMSTime 新的时间戳
 * @return 返回时间差（毫秒）
 *
 * 职责：
 *   计算两个时间戳之间的差值，正确处理uint32溢出情况
 *   当时间戳溢出（从最大值回到0）时，仍能正确计算差值
 *
 * 溢出处理：
 *   如果oldMSTime > newMSTime，说明发生了溢出
 *   使用公式：(最大值 - oldMSTime) + newMSTime 计算实际差值
 */
inline uint32 getMSTimeDiff(uint32 oldMSTime, uint32 newMSTime)
{
    // getMSTime() 数据范围有限，这是它在当前tick内溢出的情况
    if (oldMSTime > newMSTime)
        return (0xFFFFFFFF - oldMSTime) + newMSTime;
    else
        return newMSTime - oldMSTime;
}

/**
 * @brief 计算毫秒时间戳与时间点之间的时间差
 * @param oldMSTime 旧的时间戳（毫秒）
 * @param newTime 新的时间点
 * @return 返回时间差（毫秒）
 *
 * 职责：
 *   将时间点转换为毫秒时间戳，然后计算与旧时间戳的差值
 *   结合时间点和时间戳两种时间表示方式
 */
inline uint32 getMSTimeDiff(uint32 oldMSTime, std::chrono::steady_clock::time_point newTime)
{
    using namespace std::chrono;

    uint32 newMSTime = uint32(duration_cast<milliseconds>(newTime - GetApplicationStartTime()).count());
    return getMSTimeDiff(oldMSTime, newMSTime);
}

/**
 * @brief 计算从指定时间戳到现在的时间差
 * @param oldMSTime 旧的时间戳
 * @return 返回从oldMSTime到现在经过的毫秒数
 *
 * 职责：
 *   计算从指定时间戳到当前时间的时间差
 *   常用于计算某个操作已经花费的时间
 */
inline uint32 GetMSTimeDiffToNow(uint32 oldMSTime)
{
    return getMSTimeDiff(oldMSTime, getMSTime());
}

/**
 * @struct IntervalTimer
 * @brief 间隔定时器 - 用于周期性触发事件
 *
 * 职责：
 *   - 管理一个时间间隔，累积时间直到超过间隔值
 *   - 用于实现周期性触发的事件（如定期检查、周期更新等）
 *   - 支持重置和调整间隔时间
 *
 * 使用示例：
 *   IntervalTimer timer;
 *   timer.SetInterval(5000); // 5秒间隔
 *   timer.Update(diff);
 *   if (timer.Passed()) {
 *       // 执行周期性任务
 *       timer.Reset();
 *   }
 */
struct IntervalTimer
{
public:
    /**
     * @brief 默认构造函数
     *
     * 初始化间隔和当前时间为0
     */
    IntervalTimer()
        : _interval(0), _current(0)
    {
    }

    /**
     * @brief 更新定时器
     * @param diff 经过的时间（毫秒）
     *
     * 职责：
     *   累积时间到当前时间计数器
     *   防止负值（可能是时间回退的情况）
     */
    void Update(time_t diff)
    {
        _current += diff;
        if (_current < 0)
            _current = 0;
    }

    /**
     * @brief 检查是否已超过间隔时间
     * @return 如果当前时间大于等于间隔时间返回true，否则返回false
     */
    bool Passed()
    {
        return _current >= _interval;
    }

    /**
     * @brief 重置定时器
     *
     * 职责：
     *   如果当前时间已超过间隔，则保留超出部分
     *   这样可以避免时间漂移，保持精确的周期性
     */
    void Reset()
    {
        if (_current >= _interval)
            _current %= _interval;
    }

    /**
     * @brief 设置当前时间
     * @param current 新的当前时间值
     */
    void SetCurrent(time_t current)
    {
        _current = current;
    }

    /**
     * @brief 设置间隔时间
     * @param interval 新的间隔时间
     */
    void SetInterval(time_t interval)
    {
        _interval = interval;
    }

    /**
     * @brief 获取间隔时间
     * @return 返回间隔时间
     */
    time_t GetInterval() const
    {
        return _interval;
    }

    /**
     * @brief 获取当前时间
     * @return 返回当前累积时间
     */
    time_t GetCurrent() const
    {
        return _current;
    }

private:
    time_t _interval;   ///< 间隔时间，触发周期
    time_t _current;    ///< 当前累积时间
};

/**
 * @struct TimeTracker
 * @brief 时间追踪器 - 追踪剩余时间直到过期
 *
 * 职责：
 *   - 追踪一个倒计时时间，随时间递减
 *   - 用于实现超时检测、持续时间追踪等场景
 *   - 支持重置倒计时时间
 *
 * 使用示例：
 *   TimeTracker tracker(5000ms); // 5秒超时
 *   tracker.Update(diff);
 *   if (tracker.Passed()) {
 *       // 超时处理
 *   }
 */
struct TimeTracker
{
public:
    /**
     * @brief 构造函数（毫秒数）
     * @param expiry 过期时间（毫秒），默认为0
     */
    TimeTracker(int32 expiry = 0) : _expiryTime(expiry) { }

    /**
     * @brief 构造函数（持续时间类型）
     * @param expiry 过期时间
     */
    TimeTracker(Milliseconds expiry) : _expiryTime(expiry) { }

    /**
     * @brief 更新追踪器（毫秒数）
     * @param diff 经过的时间（毫秒）
     *
     * 减少剩余时间
     */
    void Update(int32 diff)
    {
        Update(Milliseconds(diff));
    }

    /**
     * @brief 更新追踪器（持续时间类型）
     * @param diff 经过的时间
     *
     * 减少剩余时间
     */
    void Update(Milliseconds diff)
    {
        _expiryTime -= diff;
    }

    /**
     * @brief 检查是否已过期
     * @return 如果剩余时间小于等于0返回true，否则返回false
     */
    bool Passed() const
    {
        return _expiryTime <= 0s;
    }

    /**
     * @brief 重置过期时间（毫秒数）
     * @param expiry 新的过期时间（毫秒）
     */
    void Reset(int32 expiry)
    {
        Reset(Milliseconds(expiry));
    }

    /**
     * @brief 重置过期时间（持续时间类型）
     * @param expiry 新的过期时间
     */
    void Reset(Milliseconds expiry)
    {
        _expiryTime = expiry;
    }

    /**
     * @brief 获取剩余过期时间
     * @return 返回剩余时间
     */
    Milliseconds GetExpiry() const
    {
        return _expiryTime;
    }

private:
    Milliseconds _expiryTime;   ///< 剩余过期时间
};

/**
 * @struct PeriodicTimer
 * @brief 周期定时器 - 周期性触发事件，支持立即触发
 *
 * 职责：
 *   - 管理一个周期时间，周期性触发事件
 *   - Update方法返回是否触发的布尔值
 *   - 支持设置初始延迟时间
 *   - 提供分离的追踪器接口（T前缀方法）
 *
 * 使用示例：
 *   PeriodicTimer timer(5000, 2000); // 5秒周期，2秒后首次触发
 *   if (timer.Update(diff)) {
 *       // 周期性任务触发
 *   }
 */
struct PeriodicTimer
{
public:
    /**
     * @brief 构造函数
     * @param period 周期时间（毫秒）
     * @param start_time 初始延迟时间（毫秒）
     */
    PeriodicTimer(int32 period, int32 start_time)
        : i_period(period), i_expireTime(start_time)
    {
    }

    /**
     * @brief 更新定时器并检查是否触发
     * @param diff 经过的时间（毫秒）
     * @return 如果触发返回true，否则返回false
     *
     * 职责：
     *   减少剩余时间，如果到期则重置并返回true
     *   重置时选择较大的值（周期时间或diff），防止过快触发
     */
    bool Update(const uint32 diff)
    {
        if ((i_expireTime -= diff) > 0)
            return false;

        i_expireTime += i_period > int32(diff) ? i_period : diff;
        return true;
    }

    /**
     * @brief 设置周期和初始延迟
     * @param period 周期时间（毫秒）
     * @param start_time 初始延迟时间（毫秒）
     */
    void SetPeriodic(int32 period, int32 start_time)
    {
        i_expireTime = start_time;
        i_period = period;
    }

    // 追踪器接口方法（Tracker interface）

    /**
     * @brief 更新追踪器（仅减少时间，不自动重置）
     * @param diff 经过的时间（毫秒）
     */
    void TUpdate(int32 diff) { i_expireTime -= diff; }

    /**
     * @brief 检查追踪器是否到期
     * @return 如果剩余时间小于等于0返回true，否则返回false
     */
    bool TPassed() const { return i_expireTime <= 0; }

    /**
     * @brief 重置追踪器
     * @param diff 经过的时间（毫秒）
     * @param period 周期时间（毫秒）
     *
     * 重置时选择较大的值（周期时间或diff）
     */
    void TReset(int32 diff, int32 period)  { i_expireTime += period > diff ? period : diff; }

private:
    int32 i_period;         ///< 周期时间
    int32 i_expireTime;     ///< 剩余时间（到期时间）
};

#endif
