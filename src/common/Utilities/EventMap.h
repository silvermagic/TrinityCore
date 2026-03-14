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

#ifndef TRINITYCORE_EVENT_MAP_H
#define TRINITYCORE_EVENT_MAP_H

#include "Define.h"
#include "Duration.h"
#include <map>

// ============================================================================
// EventMap - 事件调度系统
// ============================================================================
// 模块职责：
//   提供高效的事件调度机制，广泛用于 AI 行为、法术效果、任务逻辑等。
//   支持事件分组、阶段控制、延迟执行等高级功能。
//
// 核心概念：
//   - EventId: 事件标识符，由调用者定义，用于识别事件类型
//   - Group: 事件分组，支持批量操作（延迟、取消）
//   - Phase: 事件阶段，控制事件在特定阶段才可执行
//
// 数据结构：
//   std::multimap<TimePoint, Event> - 按时间自动排序
//
// 性能特性：
//   - 插入事件：O(log n)
//   - 查找最近事件：O(1)
//   - 执行事件：O(1) 平均
//   - 取消事件：O(n) 线性查找
//
// 使用示例：
//   EventMap events;
//   events.ScheduleEvent(EVENT_SPELL_CAST, 5s);
//   events.ScheduleEvent(EVENT_HEAL, 10s, GROUP_HEAL, PHASE_COMBAT);
//   events.Update(diff);
//   uint32 eventId = events.ExecuteEvent();
//
// 典型应用场景：
//   - AI 技能循环：调度怪物的技能施放时间
//   - BOSS 战阶段：控制不同阶段的技能使用
//   - 法术冷却：管理法术的冷却时间
//   - 定时任务：定期执行特定逻辑
// ============================================================================
class TC_COMMON_API EventMap
{
    // ========================================================================
    // 类型定义
    // ========================================================================

    using EventId = uint16;      ///< 事件ID类型，范围 0-65535，由调用者定义枚举值
    using GroupIndex = uint8;    ///< 组索引类型，范围 1-8，0 表示无组
    using GroupMask = uint8;     ///< 组掩码类型，位图形式，用于位运算判断事件所属组
    using PhaseIndex = uint8;    ///< 阶段索引类型，范围 1-8，0 表示所有阶段
    using PhaseMask = uint8;     ///< 阶段掩码类型，位图形式，用于位运算判断事件阶段

    // ========================================================================
    // Event - 事件内部数据结构
    // ========================================================================
    // 存储事件的元数据，包括事件ID、所属组和阶段信息
    // 使用位掩码表示组和阶段，支持高效的多组多阶段判断
    // ========================================================================
    struct Event
    {
        Event() = default;

        /**
         * @brief 构造事件对象
         *
         * @param id 事件ID，标识具体的事件类型
         * @param groupIndex 事件组索引（1-8，0表示无组）
         * @param phaseIndex 事件阶段索引（1-8，0表示所有阶段）
         *
         * 将组索引和阶段索引转换为对应的掩码，便于后续的位运算操作。
         * 例如：groupIndex=3 -> _groupMask=0x04 (第3位为1)
         */
        Event(EventId id, GroupIndex groupIndex, PhaseIndex phaseIndex) :
            _id(id),
            _groupMask(groupIndex ? GroupMask(1u << (groupIndex - 1u)) : 0u),
            _phaseMask(phaseIndex ? PhaseMask(1u << (phaseIndex - 1u)) : 0u)
        {
        }

        EventId _id          = 0u;  ///< 事件标识符，由调用者定义的枚举值
        GroupMask _groupMask = 0u;  ///< 事件组掩码，位图形式，支持多组
        PhaseMask _phaseMask = 0u;  ///< 事件阶段掩码，位图形式，支持多阶段
    };

    /**
     * @brief 事件存储容器类型
     *
     * 使用 std::multimap 存储事件，键为事件的触发时间点（TimePoint），
     * 值为事件对象（Event）。
     *
     * multimap 特性：
     *   - 自动按键（时间）排序，最早的事件在前
     *   - 允许同一时间有多个事件（multimap 允许重复键）
     *   - 插入时间复杂度 O(log n)
     *   - 查找最早事件 O(1)
     */
    using EventStore = std::multimap<TimePoint, Event>;

public:
    /**
     * @brief 构造函数
     *
     * 初始化事件映射，设置初始时间为最小值，阶段掩码为0（无阶段）。
     */
    EventMap() : _time(TimePoint::min()), _phaseMask(0) { }

    /**
     * @name Reset
     * @brief Removes all scheduled events and resets time and phase.
     *
     * @brief 重置事件系统
     *
     * 清空所有已调度的事件，重置时间和阶段到初始状态。
     *
     * 调用时机：
     *   - AI 脱离战斗时
     *   - 实体重置时
     *   - 清除所有待处理事件时
     *
     * 注意事项：
     *   - 会删除所有事件，包括延迟事件
     *   - 时间重置为最小值
     *   - 阶段掩码重置为0
     */
    void Reset();

    /**
     * @name Update
     * @brief Updates the timer of the event map.
     * @param time Value in ms to be added to time.
     *
     * @brief 更新事件映射的内部计时器
     *
     * @param time 要添加到当前时间的时间值（毫秒）
     *
     * 推进内部计时器，使未来事件逐渐接近执行时间。
     *
     * 调用时机：
     *   - 每帧更新时（在 UpdateAI 或类似函数中）
     *
     * 性能注意事项：
     *   - 时间复杂度：O(1)
     *   - 只是简单地累加时间，不处理事件
     */
    void Update(uint32 time)
    {
        Update(Milliseconds(time));
    }

    /**
     * @name Update
     * @brief Updates the timer of the event map.
     * @param time Value in ms to be added to time.
     *
     * @brief 更新事件映射的内部计时器（Milliseconds版本）
     *
     * @param time 要添加到当前时间的时间值
     *
     * 推进内部计时器，使未来事件逐渐接近执行时间。
     */
    void Update(Milliseconds time)
    {
        _time += time;
    }

    /**
     * @name GetPhaseMask
     * @return Active phases as mask.
     *
     * @brief 获取当前活动的阶段掩码
     *
     * @return 当前活动的阶段位掩码
     *
     * 返回当前 EventMap 所处的所有阶段的位掩码。
     *
     * 调用时机：
     *   - 检查当前处于哪些阶段
     *   - 调试和日志记录
     *
     * 返回值说明：
     *   - 0: 无阶段
     *   - 非零值: 每个位表示一个阶段是否激活
     */
    PhaseMask GetPhaseMask() const
    {
        return _phaseMask;
    }

    /**
     * @name Empty
     * @return True, if there are no events scheduled.
     *
     * @brief 检查事件映射是否为空
     *
     * @return 如果没有待处理事件返回 true，否则返回 false
     *
     * 检查是否存在已调度的事件。
     *
     * 调用时机：
     *   - 检查是否有待处理事件
     *   - 优化性能，避免不必要的 ExecuteEvent 调用
     */
    bool Empty() const
    {
        return _eventMap.empty();
    }

    /**
     * @name SetPhase
     * @brief Sets the phase of the map (absolute).
     * @param phase Phase which should be set. Values: 1 - 8. 0 resets phase.
     *
     * @brief 设置当前阶段（绝对设置）
     *
     * @param phase 要设置的阶段，有效值：1-8，0 表示重置阶段
     *
     * 完全替换当前阶段掩码，只保留指定的阶段。
     *
     * 调用时机：
     *   - BOSS 进入新的战斗阶段
     *   - 任务状态切换
     *
     * 注意事项：
     *   - 会清除所有其他阶段
     *   - 参数为 0 时清除所有阶段
     */
    void SetPhase(PhaseIndex phase);

    /**
     * @name AddPhase
     * @brief Activates the given phase (absolute).
     * @param phase Phase which should be activated. Values: 1 - 8
     *
     * @brief 激活指定阶段（相对添加）
     *
     * @param phase 要激活的阶段，有效值：1-8
     *
     * 在当前阶段掩码基础上添加新阶段。
     *
     * 调用时机：
     *   - 添加新的战斗状态
     *   - 同时保持多个阶段激活
     *
     * 注意事项：
     *   - 不会清除现有阶段
     *   - 可以同时激活多个阶段
     */
    void AddPhase(PhaseIndex phase)
    {
        if (phase && phase <= sizeof(PhaseMask) * 8)
            _phaseMask |= PhaseMask(1u << (phase - 1u));
    }

    /**
     * @name RemovePhase
     * @brief Deactivates the given phase (absolute).
     * @param phase Phase which should be deactivated. Values: 1 - 8.
     *
     * @brief 停用指定阶段
     *
     * @param phase 要停用的阶段，有效值：1-8
     *
     * 从当前阶段掩码中移除指定阶段。
     *
     * 调用时机：
     *   - 退出特定战斗状态
     *   - 移除特定事件条件
     *
     * 注意事项：
     *   - 不会影响其他阶段
     *   - 对未激活的阶段调用是安全的
     */
    void RemovePhase(PhaseIndex phase)
    {
        if (phase && phase <= sizeof(PhaseMask) * 8)
            _phaseMask &= PhaseMask(~(1u << (phase - 1u)));
    }

    /**
     * @name ScheduleEvent
     * @brief Creates new event entry in map.
     * @param eventId The id of the new event.
     * @param time The time until the event occurs as std::chrono type.
     * @param group The group which the event is associated to. Has to be between 1 and 8. 0 means it has no group.
     * @param phase The phase in which the event can occur. Has to be between 1 and 8. 0 means it can occur in all phases.
     *
     * @brief 调度一个新事件
     *
     * @param eventId 事件ID，由调用者定义（通常使用枚举）
     * @param time 事件触发前的延迟时间
     * @param group 事件所属组（1-8），0表示无组
     * @param phase 事件可执行的阶段（1-8），0表示所有阶段
     *
     * 在指定时间后调度一个事件。事件将在当前时间 + time 时触发。
     *
     * 调用时机：
     *   - AI 初始化时调度初始技能
     *   - 事件处理时调度后续事件
     *   - 战斗开始时调度战斗事件
     *
     * 性能注意事项：
     *   - 时间复杂度：O(log n)
     *   - 使用 multimap 自动按时间排序
     */
    void ScheduleEvent(EventId eventId, Milliseconds time, GroupIndex group = 0u, PhaseIndex phase = 0u);

    /**
     * @name ScheduleEvent
     * @brief Creates new event entry in map.
     * @param eventId The id of the new event.
     * @param minTime The minimum time until the event occurs as std::chrono type.
     * @param maxTime The maximum time until the event occurs as std::chrono type.
     * @param group The group which the event is associated to. Has to be between 1 and 8. 0 means it has no group.
     * @param phase The phase in which the event can occur. Has to be between 1 and 8. 0 means it can occur in all phases.
     *
     * @brief 调度一个新事件（随机时间）
     *
     * @param eventId 事件ID
     * @param minTime 最小延迟时间
     * @param maxTime 最大延迟时间
     * @param group 事件所属组（1-8），0表示无组
     * @param phase 事件可执行的阶段（1-8），0表示所有阶段
     *
     * 在 minTime 到 maxTime 之间的随机时间后调度一个事件。
     * 用于增加事件触发的随机性，使AI行为更自然。
     *
     * 调用时机：
     *   - 需要随机延迟的事件
     *   - 避免固定的技能循环模式
     */
    void ScheduleEvent(EventId eventId, Milliseconds minTime, Milliseconds maxTime, GroupIndex group = 0u, PhaseIndex phase = 0u);

    /**
     * @name RescheduleEvent
     * @brief Cancels the given event and reschedules it.
     * @param eventId The id of the event.
     * @param time The time until the event occurs as std::chrono type.
     * @param group The group which the event is associated to. Has to be between 1 and 8. 0 means it has no group.
     * @param phase The phase in which the event can occur. Has to be between 1 and 8. 0 means it can occur in all phases.
     *
     * @brief 重新调度一个已存在的事件
     *
     * @param eventId 要重新调度的事件ID
     * @param time 新的延迟时间
     * @param group 事件所属组（1-8），0表示无组
     * @param phase 事件可执行的阶段（1-8），0表示所有阶段
     *
     * 取消现有的同ID事件，并以新的时间重新调度。
     * 如果存在多个同ID事件，只取消第一个找到的。
     *
     * 调用时机：
     *   - 推迟某个特定事件
     *   - 根据条件重新设置事件时间
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n) 查找 + O(log n) 插入
     */
    void RescheduleEvent(EventId eventId, Milliseconds time, GroupIndex group = 0u, PhaseIndex phase = 0u);

    /**
     * @name RescheduleEvent
     * @brief Cancels the given event and reschedules it.
     * @param eventId The id of the event.
     * @param minTime The minimum time until the event occurs as std::chrono type.
     * @param maxTime The maximum time until the event occurs as std::chrono type.
     * @param group The group which the event is associated to. Has to be between 1 and 8. 0 means it has no group.
     * @param phase The phase in which the event can occur. Has to be between 1 and 8. 0 means it can occur in all phases.
     *
     * @brief 重新调度一个已存在的事件（随机时间）
     *
     * @param eventId 要重新调度的事件ID
     * @param minTime 最小延迟时间
     * @param maxTime 最大延迟时间
     * @param group 事件所属组（1-8），0表示无组
     * @param phase 事件可执行的阶段（1-8），0表示所有阶段
     *
     * 取消现有的同ID事件，并在随机时间后重新调度。
     */
    void RescheduleEvent(EventId eventId, Milliseconds minTime, Milliseconds maxTime, GroupIndex group = 0u, PhaseIndex phase = 0u);

    /**
     * @name RepeatEvent
     * @brief Repeats the most recently executed event.
     * @param time Time until the event occurs as std::chrono type.
     *
     * @brief 重复执行最近的事件
     *
     * @param time 延迟时间
     *
     * 使用与最近执行的事件相同的ID、组和阶段，重新调度该事件。
     * 这是实现周期性事件的便捷方法。
     *
     * 调用时机：
     *   - 周期性技能施放
     *   - 持续性效果更新
     *
     * 注意事项：
     *   - 必须在 ExecuteEvent() 之后立即调用
     *   - 会保留原事件的所有属性（组、阶段）
     */
    void Repeat(Milliseconds time);

    /**
     * @name RepeatEvent
     * @brief Repeats the most recently executed event.
     * @param minTime The minimum time until the event occurs as std::chrono type.
     * @param maxTime The maximum time until the event occurs as std::chrono type.
     *
     * @brief 重复执行最近的事件（随机时间）
     *
     * @param minTime 最小延迟时间
     * @param maxTime 最大延迟时间
     *
     * 在随机时间后重复最近执行的事件。
     * 用于实现随机间隔的周期性行为。
     */
    void Repeat(Milliseconds minTime, Milliseconds maxTime);

    /**
     * @name ExecuteEvent
     * @brief Returns the next event to execute and removes it from map.
     * @return Id of the event to execute.
     *
     * @brief 执行下一个到期事件
     *
     * @return 到期事件的事件ID，如果没有到期事件或事件不在当前阶段则返回0
     *
     * 获取并移除下一个到期且符合当前阶段的事件。
     * 这是事件处理的核心方法，通常在AI更新循环中调用。
     *
     * 调用时机：
     *   - 在 UpdateAI() 中每帧调用
     *   - 检查是否有事件需要处理
     *
     * 主要流程：
     *   1. 检查事件队列是否为空
     *   2. 查找最早到期的事件
     *   3. 检查事件是否已到期（时间 <= 当前时间）
     *   4. 检查事件阶段是否匹配当前阶段
     *   5. 如果匹配则移除事件并返回其ID
     *   6. 如果不匹配阶段则跳过该事件
     *
     * 性能注意事项：
     *   - 平均时间复杂度：O(1)
     *   - 最坏情况：O(n) 当大量事件不在当前阶段时
     *
     * 返回值说明：
     *   - 0: 没有到期事件或事件不在当前阶段
     *   - 非零: 到期事件的事件ID
     */
    EventId ExecuteEvent();

    /**
     * @name DelayEvents
     * @brief Delays all events.
     * @param delay Amount of delay as std::chrono type.
     *
     * @brief 延迟所有事件
     *
     * @param delay 延迟时间
     *
     * 将所有事件推迟指定的时间。
     *
     * 调用时机：
     *   - 单位被眩晕或冻结时
     *   - 战斗暂停
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     *   - 需要重新插入所有事件到 multimap
     */
    void DelayEvents(Milliseconds delay);

    /**
     * @name DelayEvents
     * @brief Delay all events of the same group.
     * @param delay Amount of delay as std::chrono type.
     * @param group Group of the events.
     *
     * @brief 延迟指定组的所有事件
     *
     * @param delay 延迟时间
     * @param group 事件组ID
     *
     * 将指定组的所有事件推迟指定的时间。
     *
     * 调用时机：
     *   - 特定类型事件需要延迟
     *   - 部分技能进入冷却
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     *   - 需要遍历所有事件查找匹配的组
     */
    void DelayEvents(Milliseconds delay, GroupIndex group);

    /**
     * @name SetMinimalDelay
     * @brief Increase event delay if smaller than given delay.
     * @param eventId The id of the event.
     * @param delay Minimum delay for given event.
     *
     * @brief 设置事件的最小延迟
     *
     * @param eventId 事件ID
     * @param delay 最小延迟时间
     *
     * 如果指定事件的当前延迟小于给定延迟，则将其延迟增加到给定值。
     * 不会减少延迟时间。
     *
     * 调用时机：
     *   - 确保事件不会过早触发
     *   - 动态调整事件时间
     *
     * 注意事项：
     *   - 只影响找到的第一个匹配事件
     *   - 只会增加延迟，不会减少
     */
    void SetMinimalDelay(EventId eventId, Milliseconds delay);

    /**
     * @name CancelEvent
     * @brief Cancels all events of the specified id.
     * @param eventId Event id to cancel.
     *
     * @brief 取消指定ID的所有事件
     *
     * @param eventId 要取消的事件ID
     *
     * 从事件映射中移除所有具有指定ID的事件。
     *
     * 调用时机：
     *   - 取消特定技能的后续事件
     *   - 任务取消时清理相关事件
     *   - 状态改变时移除不再需要的事件
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     *   - 需要遍历所有事件查找匹配的ID
     */
    void CancelEvent(EventId eventId);

    /**
     * @name CancelEventGroup
     * @brief Cancel events belonging to specified group.
     * @param group Group to cancel.
     *
     * @brief 取消指定组的所有事件
     *
     * @param group 要取消的事件组ID
     *
     * 从事件映射中移除所有属于指定组的事件。
     *
     * 调用时机：
     *   - 取消特定类型的所有事件
     *   - 阶段切换时清理旧阶段事件
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     */
    void CancelEventGroup(GroupIndex group);

    /**
     * @name IsInPhase
     * @brief Returns whether event map is in specified phase or not.
     * @param phase Wanted phase.
     * @return True, if phase of event map contains specified phase.
     *
     * @brief 检查是否处于指定阶段
     *
     * @param phase 要检查的阶段
     * @return 如果当前处于该阶段返回 true，否则返回 false
     *
     * 检查当前阶段掩码是否包含指定阶段。
     *
     * 调用时机：
     *   - 条件判断
     *   - 调试日志
     *
     * 注意事项：
     *   - phase=0 返回 true（表示所有阶段）
     *   - phase > 8 返回 false（超出范围）
     */
    bool IsInPhase(PhaseIndex phase) const
    {
        return phase <= sizeof(PhaseIndex) * 8 && (!phase || _phaseMask & PhaseMask(1u << (phase - 1u)));
    }

    /**
     * @name GetTimeUntilEvent
     * @brief Returns time as std::chrono type until next event.
     * @param eventId The id of the event.
     * @return Time of next event. If event is not scheduled returns Milliseconds::max()
     * @return Time of next event.
     *
     * @brief 获取距离指定事件的剩余时间
     *
     * @param eventId 事件ID
     * @return 距离事件触发的剩余时间，如果事件未调度返回 Milliseconds::max()
     *
     * 计算当前时间到指定事件触发时间的差值。
     *
     * 调用时机：
     *   - 检查特定事件的剩余时间
     *   - 显示冷却时间
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     *   - 需要遍历查找指定事件
     *
     * 返回值说明：
     *   - Milliseconds::max(): 事件未调度
     *   - 负值: 事件已过期
     *   - 正值: 事件的剩余时间
     */
    Milliseconds GetTimeUntilEvent(EventId eventId) const;

    /**
     * @name HasEventScheduled
     * @brief Returns whether an event is scheduled
     * @param eventId The id of the event.
     * @return True if event is scheduled
     *
     * @brief 检查指定事件是否已调度
     *
     * @param eventId 事件ID
     * @return 如果事件已调度返回 true，否则返回 false
     *
     * 检查事件映射中是否存在指定ID的事件。
     *
     * 调用时机：
     *   - 避免重复调度同一事件
     *   - 检查事件是否在队列中
     *
     * 性能注意事项：
     *   - 时间复杂度：O(n)
     *   - 需要遍历查找指定事件
     */
    bool HasEventScheduled(EventId eventId) const;

private:
    /**
    * @name _time
    * @brief Internal timer.
    *
    * This does not represent the real date/time value.
    * It's more like a stopwatch: It can run, it can be stopped,
    * it can be resetted and so on. Events occur when this timer
    * has reached their time value. Its value is changed in the
    * Update method.
    *
    * 内部计时器
    *
    * 这是一个相对时间，不是真实的日期/时间值。
    * 更像是一个秒表：可以运行、停止、重置等。
    * 当此计时器达到事件的时间值时，事件触发。
    * 其值在 Update 方法中更新。
    */
    TimePoint _time;

    /**
    * @name _phaseMask
    * @brief Phase mask of the event map.
    *
    * Contains the phases the event map is in. Multiple
    * phases from 1 to 8 can be set with SetPhase or
    * AddPhase. RemovePhase deactives a phase.
    *
    * 事件映射的阶段掩码
    *
    * 包含事件映射当前所处的阶段。
    * 可以通过 SetPhase 或 AddPhase 设置多个阶段（1-8）。
    * RemovePhase 用于停用某个阶段。
    *
    * 每个位代表一个阶段：
    *   - 位 0: 阶段 1
    *   - 位 1: 阶段 2
    *   - ...
    *   - 位 7: 阶段 8
    */
    PhaseMask _phaseMask;

    /**
    * @name _eventMap
    * @brief Internal event storage map. Contains the scheduled events.
    *
    * See typedef at the beginning of the class for more
    * details.
    *
    * 内部事件存储映射。包含所有已调度的事件。
    *
    * 使用 std::multimap 存储，键为事件的触发时间，值为事件对象。
    * multimap 自动按时间排序，确保最早的事件在前面。
    * 允许同一时间有多个事件（multimap 特性）。
    *
    * 详细说明见类开头的 typedef。
    */
    EventStore _eventMap;

    /**
    * @name _lastEvent
    * @brief Stores information on the most recently executed event
    *
    * 存储最近执行的事件信息
    *
    * 用于 Repeat() 方法，保留最近执行事件的所有信息（ID、组、阶段）。
    * 使得可以轻松地重新调度具有相同属性的事件。
    */
    Event _lastEvent;
};

#endif // TRINITYCORE_EVENT_MAP_H
