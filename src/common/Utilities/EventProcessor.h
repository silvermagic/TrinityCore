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
// EventProcessor.h - 事件处理器系统
// ============================================================================
// 模块职责：
//   提供基于时间的事件调度和处理机制。
//   与 EventMap 不同，EventProcessor 支持面向对象的事件类型，
//   允许事件包含复杂的状态和行为。
//
// 核心概念：
//   - BasicEvent: 事件基类，可派生实现自定义事件
//   - EventProcessor: 事件处理器，管理和执行事件
//   - LambdaBasicEvent: 支持Lambda函数作为事件
//
// 使用场景：
//   - 异步操作（延迟执行）
//   - 周期性任务
//   - 网络消息延迟发送
//   - 法术效果的延迟处理
//
// 与 EventMap 的区别：
//   - EventMap: 轻量级，使用枚举ID标识事件
//   - EventProcessor: 重量级，使用对象表示事件，支持复杂逻辑
//
// 性能特性：
//   - 事件插入：O(log n)
//   - 事件执行：O(1) 平均
//   - 支持事件中止和重新调度
// ============================================================================

#ifndef __EVENTPROCESSOR_H
#define __EVENTPROCESSOR_H

#include "Define.h"
#include "Duration.h"
#include "Random.h"
#include <map>
#include <type_traits>

class EventProcessor;

// 注意：所有时间单位都是毫秒

// ============================================================================
// BasicEvent - 事件基类
// ============================================================================
// 类职责：
//   定义事件的基本接口和行为。所有事件都必须继承此类。
//   支持事件执行、中止、可删除性检查等功能。
//
// 使用场景：
//   - 创建自定义事件类
//   - 实现异步操作
//   - 管理需要复杂状态的事件
//
// 生命周期：
//   1. 创建事件对象（new）
//   2. 添加到 EventProcessor（AddEvent）
//   3. 等待执行或中止
//   4. 执行完成后自动删除或手动删除
//
// 性能考虑：
//   - 虚函数调用开销
//   - 事件对象在堆上分配
//   - 适合复杂事件，不适合高频简单事件
// ============================================================================
class TC_COMMON_API BasicEvent
{
        friend class EventProcessor;

        /**
         * @brief 事件中止状态枚举
         *
         * 跟踪事件的中止生命周期：
         *   - STATE_RUNNING: 事件正在运行（正常状态）
         *   - STATE_ABORT_SCHEDULED: 已调度中止，将在下次更新时中止
         *   - STATE_ABORTED: 已中止，事件被取消
         */
        enum class AbortState : uint8
        {
            STATE_RUNNING,           // 正常运行中
            STATE_ABORT_SCHEDULED,   // 已调度中止
            STATE_ABORTED            // 已中止
        };

    public:
        /**
         * @brief 构造函数
         *
         * 初始化事件状态为运行中，时间和执行时间为0。
         */
        BasicEvent()
          : m_abortState(AbortState::STATE_RUNNING), m_addTime(0), m_execTime(0) { }

        /**
         * @brief 虚析构函数
         *
         * 允许派生类在事件删除时执行清理操作。
         * 重写此析构函数以执行事件移除时的特定操作。
         */
        virtual ~BasicEvent() { }

        /**
         * @brief 执行事件
         *
         * @param e_time 事件的执行时间（绝对时间）
         * @param p_time 自上次更新以来的时间间隔（毫秒）
         * @return 如果事件应该被删除返回 true，否则返回 false
         *
         * 当事件触发时调用此方法。
         * 派生类必须重写此方法以实现事件逻辑。
         *
         * 返回值说明：
         *   - true: 事件执行完毕，应该删除（默认）
         *   - false: 事件不应删除，可能需要重新调度
         *
         * 调用时机：
         *   - EventProcessor::Update() 中，当事件到期时
         */
        virtual bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) { return true; }

        /**
         * @brief 检查事件是否可删除
         *
         * @return 如果事件可以安全删除返回 true，否则返回 false
         *
         * 某些事件可能需要在特定条件下才能删除。
         * 如果返回 false，事件会被重新调度到 1ms 后再次检查。
         *
         * 调用时机：
         *   - EventProcessor 决定是否删除事件时
         *   - KillAllEvents 决定是否强制删除时
         */
        virtual bool IsDeletable() const { return true; }

        /**
         * @brief 中止事件
         *
         * @param e_time 中止时的当前时间
         *
         * 当事件被中止时调用此方法。
         * 派生类可以重写此方法以执行清理操作或回滚状态。
         *
         * 调用时机：
         *   - ScheduleAbort() 被调用后的下次更新
         *   - KillAllEvents() 被调用时
         */
        virtual void Abort(uint64 /*e_time*/) { }

        /**
         * @brief 调度事件中止
         *
         * 标记事件将在下次更新时中止。
         * 允许正在执行的事件安全完成当前操作。
         *
         * 调用时机：
         *   - 需要取消尚未执行的事件
         *   - 单位死亡时取消待处理事件
         *
         * 注意事项：
         *   - 只能对正在运行的事件调用
         *   - 会在下次 Update 时调用 Abort()
         */
        void ScheduleAbort();

    private:
        /**
         * @brief 设置事件为已中止状态
         *
         * 内部方法，由 EventProcessor 调用。
         * 将事件标记为已中止状态。
         */
        void SetAborted();

        /**
         * @brief 检查事件是否正在运行
         *
         * @return 如果事件正在运行返回 true
         */
        bool IsRunning() const { return (m_abortState == AbortState::STATE_RUNNING); }

        /**
         * @brief 检查事件是否已调度中止
         *
         * @return 如果事件已调度中止返回 true
         */
        bool IsAbortScheduled() const { return (m_abortState == AbortState::STATE_ABORT_SCHEDULED); }

        /**
         * @brief 检查事件是否已中止
         *
         * @return 如果事件已中止返回 true
         */
        bool IsAborted() const { return (m_abortState == AbortState::STATE_ABORTED); }

        AbortState m_abortState;  // 事件的中止状态

        // 这些成员可用于时间偏移控制
        uint64 m_addTime;   // 事件被添加到队列的时间，由事件处理器填充
        uint64 m_execTime;  // 计划执行的时间，由事件处理器填充
};

// ============================================================================
// LambdaBasicEvent - Lambda事件包装器
// ============================================================================
// 类职责：
//   将Lambda函数包装为BasicEvent对象，简化简单事件的创建。
//   避免为简单操作创建完整的事件类。
//
// 使用场景：
//   - 简单的一次性延迟操作
//   - 不需要复杂状态的事件
//   - 快速原型开发
//
// 示例：
//   processor.AddEvent([this]() {
//       this->DoSomething();
//   }, 1000ms);
//
// 性能考虑：
//   - Lambda捕获的数据存储在事件对象中
//   - 适合小对象，避免捕获大对象
// ============================================================================
template<typename T>
class LambdaBasicEvent : public BasicEvent
{
public:
    /**
     * @brief 构造函数
     *
     * @param callback 要执行的Lambda函数
     *
     * 接受一个Lambda函数并存储在事件对象中。
     */
    LambdaBasicEvent(T&& callback) : BasicEvent(), _callback(std::move(callback)) { }

    /**
     * @brief 执行事件
     *
     * 调用存储的Lambda函数。
     */
    bool Execute(uint64, uint32) override
    {
        _callback();
        return true;  // 执行后删除事件
    }

private:
    T _callback;  // 存储的Lambda函数
};

/**
 * @brief Lambda事件类型特征
 *
 * 用于SFINAE，确保只有非BasicEvent类型才使用Lambda包装器。
 * 防止将BasicEvent派生类错误地包装为LambdaBasicEvent。
 */
template<typename T>
using is_lambda_event = std::enable_if_t<!std::is_base_of_v<BasicEvent, std::remove_pointer_t<std::remove_cvref_t<T>>>>;

// ============================================================================
// EventProcessor - 事件处理器
// ============================================================================
// 类职责：
//   管理和执行定时事件队列。提供事件的添加、删除、中止等功能。
//   作为事件的拥有者，负责事件对象的生命周期管理。
//
// 核心功能：
//   - 事件调度：AddEvent系列方法
//   - 事件执行：Update方法
//   - 事件管理：KillAllEvents、ModifyEventTime
//
// 数据结构：
//   std::multimap<uint64, BasicEvent*> - 按执行时间排序的事件队列
//
// 使用场景：
//   - 游戏实体的异步操作
//   - 网络消息的延迟发送
//   - 定时器和周期性任务
//
// 线程安全：
//   - 非线程安全，需要在单线程中使用
//   - 通常每个实体拥有自己的EventProcessor
//
// 性能特性：
//   - 事件插入：O(log n)
//   - 事件执行：O(1) 平均
//   - 事件查找：O(n) 用于修改和删除特定事件
//
// 生命周期管理：
//   - 事件对象由EventProcessor拥有
//   - 执行后自动删除（除非Execute返回false）
//   - 析构时删除所有待处理事件
// ============================================================================
class TC_COMMON_API EventProcessor
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化事件处理器，设置时间为0。
         */
        EventProcessor() : m_time(0) { }

        /**
         * @brief 析构函数
         *
         * 清理所有待处理的事件。
         * 强制删除所有事件，包括不可删除的事件。
         */
        ~EventProcessor();

        /**
         * @brief 更新事件处理器
         *
         * @param p_time 自上次更新以来经过的时间（毫秒）
         *
         * 处理所有到期的事件。这是事件处理器的核心方法。
         *
         * 调用时机：
         *   - 每帧更新时（在UpdateAI或类似函数中）
         *
         * 主要流程：
         *   1. 更新当前时间
         *   2. 遍历所有到期事件
         *   3. 执行或中止事件
         *   4. 删除已完成的事件
         *
         * 性能注意事项：
         *   - 时间复杂度取决于执行的事件数量
         *   - 事件按时间顺序执行
         */
        void Update(uint32 p_time);

        /**
         * @brief 终止并删除所有事件
         *
         * @param force 是否强制删除所有事件
         *              true: 删除所有事件，包括不可删除的事件
         *              false: 仅删除可删除的事件
         *
         * 清理事件处理器中的所有事件。
         *
         * 调用时机：
         *   - 实体销毁时
         *   - 战斗结束清理
         *   - 重置事件处理器
         */
        void KillAllEvents(bool force);

        /**
         * @brief 添加事件到处理器
         *
         * @param event 事件对象指针（所有权转移给EventProcessor）
         * @param e_time 事件的执行时间（绝对时间）
         * @param set_addtime 是否设置事件的添加时间
         *                    true: 设置事件添加时间为当前时间（默认）
         *                    false: 不修改添加时间
         *
         * 将事件添加到事件队列，在指定时间执行。
         *
         * 调用时机：
         *   - 需要在特定时间执行操作
         *   - 已经计算好绝对执行时间
         *
         * 注意事项：
         *   - 事件的所有权转移给EventProcessor
         *   - 不要手动删除已添加的事件
         */
        void AddEvent(BasicEvent* event, Milliseconds e_time, bool set_addtime = true);

        /**
         * @brief 添加Lambda事件
         *
         * @tparam T Lambda类型
         * @param event Lambda函数
         * @param e_time 执行时间
         * @param set_addtime 是否设置添加时间
         *
         * 便捷方法，将Lambda函数包装为事件并添加。
         *
         * 示例：
         *   processor.AddEvent([this]() { DoSomething(); }, 1s);
         */
        template<typename T>
        is_lambda_event<T> AddEvent(T&& event, Milliseconds e_time, bool set_addtime = true) { AddEvent(new LambdaBasicEvent<T>(std::move(event)), e_time, set_addtime); }

        /**
         * @brief 在指定偏移时间后添加事件
         *
         * @param event 事件对象
         * @param offset 相对于当前时间的偏移量
         *
         * 在当前时间 + offset 后执行事件。
         *
         * 调用时机：
         *   - 延迟执行操作
         *   - 相对时间调度
         */
        void AddEventAtOffset(BasicEvent* event, Milliseconds offset) { AddEvent(event, CalculateTime(offset)); }

        /**
         * @brief 在随机偏移时间后添加事件
         *
         * @param event 事件对象
         * @param offset 最小偏移时间
         * @param offset2 最大偏移时间
         *
         * 在随机时间后执行事件（offset 到 offset2 之间）。
         *
         * 调用时机：
         *   - 需要随机延迟的操作
         *   - 避免固定模式
         */
        void AddEventAtOffset(BasicEvent* event, Milliseconds offset, Milliseconds offset2) { AddEvent(event, CalculateTime(randtime(offset, offset2))); }

        /**
         * @brief 在指定偏移时间后添加Lambda事件
         */
        template<typename T>
        is_lambda_event<T> AddEventAtOffset(T&& event, Milliseconds offset) { AddEventAtOffset(new LambdaBasicEvent<T>(std::move(event)), offset); }

        /**
         * @brief 在随机偏移时间后添加Lambda事件
         */
        template<typename T>
        is_lambda_event<T> AddEventAtOffset(T&& event, Milliseconds offset, Milliseconds offset2) { AddEventAtOffset(new LambdaBasicEvent<T>(std::move(event)), offset, offset2); }

        /**
         * @brief 修改事件的执行时间
         *
         * @param event 要修改的事件指针
         * @param newTime 新的执行时间
         *
         * 更改已添加事件的执行时间。
         *
         * 调用时机：
         *   - 推迟或提前事件执行
         *   - 动态调整事件时间
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)
         *   - 需要线性查找事件
         */
        void ModifyEventTime(BasicEvent* event, Milliseconds newTime);

        /**
         * @brief 计算绝对时间
         *
         * @param t_offset 相对于当前时间的偏移量
         * @return 绝对执行时间
         *
         * 将相对时间转换为绝对时间。
         *
         * 示例：
         *   CalculateTime(5s) 返回当前时间 + 5秒
         */
        Milliseconds CalculateTime(Milliseconds t_offset) const { return Milliseconds(m_time) + t_offset; }

    protected:
        uint64 m_time;  // 当前时间（毫秒）
        std::multimap<uint64, BasicEvent*> m_events;  // 事件队列，按键（时间）排序
};

#endif
