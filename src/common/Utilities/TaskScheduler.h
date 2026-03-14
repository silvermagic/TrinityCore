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
 * @file TaskScheduler.h
 * @brief 任务调度器模块 - 提供定时任务和异步任务的调度功能
 *
 * 模块职责：
 *   - 提供任务调度功能，支持在指定时间后执行任务
 *   - 支持任务的分组管理，可以通过组ID批量操作任务
 *   - 支持任务的延迟、重调度和取消操作
 *   - 提供异步任务执行机制
 *   - 支持任务的重复执行和计数
 *
 * 主要组件：
 *   - TaskScheduler: 核心调度器类，管理所有任务的调度和执行
 *   - TaskContext: 任务上下文，提供任务执行时的操作接口
 *   - Task: 内部任务对象，存储任务的相关信息
 *   - TaskQueue: 任务队列，按时间顺序组织任务
 *
 * 使用示例：
 *   TaskScheduler scheduler;
 *   scheduler.Schedule(5s, [](TaskContext context) {
 *       // 在5秒后执行的任务
 *       context.Repeat(); // 重复执行
 *   });
 *   scheduler.Update();
 */

#ifndef _TASK_SCHEDULER_H_
#define _TASK_SCHEDULER_H_

#include "Duration.h"
#include "Optional.h"
#include "Random.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <queue>
#include <memory>
#include <utility>
#include <set>

class TaskContext;

/**
 * @class TaskScheduler
 * @brief 任务调度器 - 提供定时任务和异步任务的调度管理功能
 *
 * 职责：
 *   - 调度std::function对象在将来某个时间点执行
 *   - 通过Update方法更新调度器状态并执行到期任务
 *   - 支持任务分组管理，多个任务可以共享同一个组ID
 *   - 提供任务的取消、延迟和重调度操作
 *   - 支持异步任务，在下一个更新周期立即执行
 *   - 支持任务重复执行，并提供重复计数器
 *
 * 主要方法：
 *   - Schedule: 调度一个任务在指定时间后执行
 *   - Async: 调度一个异步任务在下一个更新周期执行
 *   - Cancel: 取消已调度的任务
 *   - Delay: 延迟已调度的任务
 *   - Reschedule: 重新调度任务到新的时间点
 *   - Update: 更新调度器并执行到期任务
 *
 * 任务回调签名：
 *   void(TaskContext) - 回调接收TaskContext对象，可访问调度计划和重复计数器
 *
 * 使用注意事项：
 *   - 不要在任务上下文内部调用TaskScheduler的Schedule方法，应使用TaskContext::Schedule
 *   - 任务组ID是操作特定任务的唯一途径
 *   - 重复计数器可用于实现每次行为不同的重复任务（如对话事件）
 */
class TC_COMMON_API TaskScheduler
{
    friend class TaskContext;

    // 时间定义（使用steady_clock单调时钟）
    typedef std::chrono::steady_clock clock_t;          ///< 时钟类型，使用单调时钟避免系统时间调整的影响
    typedef clock_t::time_point timepoint_t;            ///< 时间点类型
    typedef clock_t::duration duration_t;               ///< 时间间隔类型

    // 任务组类型
    typedef uint32 group_t;                             ///< 任务组ID类型，用于批量操作相关任务
    // 任务重复类型
    typedef uint32 repeated_t;                          ///< 任务重复计数类型，记录任务已重复执行的次数
    // 任务处理函数类型
    typedef std::function<void(TaskContext)> task_handler_t;  ///< 任务回调函数类型，接收TaskContext参数
    // 谓词类型
    typedef std::function<bool()> predicate_t;          ///< 验证谓词类型，用于控制任务是否允许执行
    // 成功回调类型
    typedef std::function<void()> success_t;            ///< 成功回调函数类型，在所有任务处理完成后调用

    /**
     * @class Task
     * @brief 内部任务对象 - 存储单个定时任务的所有相关信息
     *
     * 职责：
     *   - 封装任务的执行时间、持续时间、组ID、重复计数和回调函数
     *   - 提供任务比较操作符，用于在优先队列中排序
     *   - 支持任务组检查
     */
    class Task
    {
        friend class TaskContext;
        friend class TaskScheduler;

        timepoint_t _end;               ///< 任务到期时间点，到达此时间时任务将被执行
        duration_t _duration;           ///< 任务持续时间，用于重复执行时计算下次执行时间
        Optional<group_t> _group;       ///< 任务组ID（可选），用于批量操作相关任务
        repeated_t _repeated;           ///< 重复计数器，记录任务已被重复执行的次数
        task_handler_t _task;           ///< 任务回调函数，存储实际要执行的逻辑

    public:
        /**
         * @brief 完整参数构造函数
         * @param end 任务到期时间点
         * @param duration 任务持续时间间隔
         * @param group 任务组ID（可选）
         * @param repeated 任务重复次数
         * @param task 任务回调函数
         */
        Task(timepoint_t const& end, duration_t const& duration, Optional<group_t> const& group,
            repeated_t const repeated, task_handler_t const& task)
                : _end(end), _duration(duration), _group(group), _repeated(repeated), _task(task) { }

        /**
         * @brief 最小参数构造函数（无分组）
         * @param end 任务到期时间点
         * @param duration 任务持续时间间隔
         * @param task 任务回调函数
         */
        Task(timepoint_t const& end, duration_t const& duration, task_handler_t const& task)
            : _end(end), _duration(duration), _group(std::nullopt), _repeated(0), _task(task) { }

        // 禁用拷贝构造
        Task(Task const&) = delete;
        // 禁用移动构造
        Task(Task&&) = delete;
        // 默认拷贝赋值运算符
        Task& operator= (Task const&) = default;
        // 禁用移动赋值运算符
        Task& operator= (Task&& right) = delete;

        /**
         * @brief 三向比较运算符，按到期时间排序任务
         * @param other 要比较的另一个任务
         * @return 返回三向比较结果（less/equivalent/greater）
         */
        std::weak_ordering operator<=> (Task const& other) const
        {
            return std::compare_weak_order_fallback(_end, other._end);
        }

        /**
         * @brief 相等比较运算符，比较任务到期时间
         * @param other 要比较的另一个任务
         * @return 如果到期时间相同返回true，否则返回false
         */
        bool operator== (Task const& other) const
        {
            return _end == other._end;
        }

        /**
         * @brief 检查任务是否属于指定组
         * @param group 要检查的组ID
         * @return 如果任务属于该组返回true，否则返回false
         */
        inline bool IsInGroup(group_t const group) const
        {
            return _group == group;
        }
    };

    typedef std::shared_ptr<Task> TaskContainer;  ///< 任务容器类型，使用shared_ptr管理Task对象

    /**
     * @struct Compare
     * @brief 任务比较器，用于multiset容器中任务排序
     *
     * 职责：
     *   - 提供任务对象的比较逻辑，按到期时间排序
     *   - 确保最早到期的任务位于队列前端
     */
    struct Compare
    {
        /**
         * @brief 比较两个任务容器
         * @param left 左侧任务容器
         * @param right 右侧任务容器
         * @return 如果左侧任务应排在前面返回true
         */
        bool operator() (TaskContainer const& left, TaskContainer const& right) const
        {
            return (*left.get()) < (*right.get());
        }
    };

    /**
     * @class TaskQueue
     * @brief 任务队列 - 管理待执行任务的容器
     *
     * 职责：
     *   - 维护按时间排序的任务容器
     *   - 提供任务的插入、移除和修改操作
     *   - 支持条件过滤的任务移除和修改
     */
    class TC_COMMON_API TaskQueue
    {
        std::multiset<TaskContainer, Compare> container;  ///< 多重集合容器，按到期时间排序存储任务

    public:
        /**
         * @brief 将任务推入队列
         * @param task 要插入的任务容器（右值引用）
         */
        void Push(TaskContainer&& task);

        /**
         * @brief 从队列弹出最早到期的任务
         * @return 返回被弹出的任务容器
         */
        TaskContainer Pop();

        /**
         * @brief 获取队列中最早到期的任务（不移除）
         * @return 返回最早到期任务的常量引用
         */
        TaskContainer const& First() const;

        /**
         * @brief 清空任务队列
         */
        void Clear();

        /**
         * @brief 根据条件移除任务
         * @param filter 过滤函数，返回true表示移除该任务
         */
        void RemoveIf(std::function<bool(TaskContainer const&)> const& filter);

        /**
         * @brief 根据条件修改任务并重新排序
         * @param filter 过滤函数，返回true表示需要修改该任务
         */
        void ModifyIf(std::function<bool(TaskContainer const&)> const& filter);

        /**
         * @brief 检查队列是否为空
         * @return 如果队列为空返回true，否则返回false
         */
        bool IsEmpty() const;
    };

    /// 自引用智能指针，用于跟踪对象是否被销毁
    /// 防止在任务执行过程中调度器被删除导致的问题
    std::shared_ptr<TaskScheduler> self_reference;

    /// 当前时间点，用于判断任务是否到期
    timepoint_t _now;

    /// 任务队列，存储所有待执行的定时任务
    TaskQueue _task_holder;

    typedef std::queue<std::function<void()>> AsyncHolder;  ///< 异步任务队列类型

    /// 异步任务队列，存储将在下一个更新周期执行的任务
    AsyncHolder _asyncHolder;

    /// 验证谓词，用于控制任务是否允许执行
    predicate_t _predicate;

    /**
     * @brief 空验证器，始终返回true
     * @return 始终返回true，表示允许执行
     */
    static bool EmptyValidator()
    {
        return true;
    }

    /**
     * @brief 空回调函数，默认的成功回调
     */
    static void EmptyCallback()
    {
    }

public:
    /**
     * @brief 默认构造函数
     *
     * 初始化任务调度器，设置当前时间和空验证器
     */
    TaskScheduler()
        : self_reference(this, [](TaskScheduler const*) { }), _now(clock_t::now()), _predicate(EmptyValidator) { }

    /**
     * @brief 带验证谓词的构造函数
     * @param predicate 验证谓词，用于控制任务是否允许执行
     */
    template<typename P>
    TaskScheduler(P&& predicate)
        : self_reference(this, [](TaskScheduler const*) { }), _now(clock_t::now()), _predicate(std::forward<P>(predicate)) { }

    // 禁用拷贝构造
    TaskScheduler(TaskScheduler const&) = delete;
    // 禁用移动构造
    TaskScheduler(TaskScheduler&&) = delete;
    // 禁用拷贝赋值运算符
    TaskScheduler& operator= (TaskScheduler const&) = delete;
    // 禁用移动赋值运算符
    TaskScheduler& operator= (TaskScheduler&&) = delete;

    /**
     * @brief 设置验证谓词
     * @tparam P 谓词类型
     * @param predicate 验证谓词函数
     * @return 返回调度器引用，支持链式调用
     *
     * 验证谓词在每次任务执行前被调用，用于控制任务是否允许执行
     */
    template<typename P>
    TaskScheduler& SetValidator(P&& predicate)
    {
        _predicate = std::forward<P>(predicate);
        return *this;
    }

    /**
     * @brief 清除验证谓词
     * @return 返回调度器引用，支持链式调用
     *
     * 重置验证谓词为空验证器，取消所有验证逻辑
     */
    TaskScheduler& ClearValidator();

    /**
     * @brief 更新调度器（使用当前时间）
     * @param callback 成功回调函数，在所有任务处理完成后执行
     * @return 返回调度器引用，支持链式调用
     *
     * 执行一轮任务调度更新，处理所有到期的任务
     */
    TaskScheduler& Update(success_t const& callback = EmptyCallback);

    /**
     * @brief 更新调度器（使用指定的毫秒偏移量）
     * @param milliseconds 毫秒数偏移量
     * @param callback 成功回调函数，在所有任务处理完成后执行
     * @return 返回调度器引用，支持链式调用
     *
     * 使用指定的时间偏移量执行一轮任务调度更新
     */
    TaskScheduler& Update(size_t const milliseconds, success_t const& callback = EmptyCallback);

    /**
     * @brief 更新调度器（使用时间间隔）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param difftime 时间间隔
     * @param callback 成功回调函数，在所有任务处理完成后执行
     * @return 返回调度器引用，支持链式调用
     *
     * 使用指定的时间间隔执行一轮任务调度更新
     */
    template<class _Rep, class _Period>
    TaskScheduler& Update(std::chrono::duration<_Rep, _Period> const& difftime,
        success_t const& callback = EmptyCallback)
    {
        _now += difftime;
        Dispatch(callback);
        return *this;
    }

    /**
     * @brief 调度异步任务
     * @param callable 要异步执行的可调用对象
     * @return 返回调度器引用，支持链式调用
     *
     * 调度一个在下一个更新周期立即执行的任务
     * 注意：可以安全地在回调函数内修改TaskScheduler
     */
    TaskScheduler& Async(std::function<void()> const& callable);

    /**
     * @brief 调度定时任务（固定时间间隔）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param time 任务执行的时间间隔
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     *
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::Schedule
     */
    template<class _Rep, class _Period>
    TaskScheduler& Schedule(std::chrono::duration<_Rep, _Period> const& time,
        task_handler_t const& task)
    {
        return ScheduleAt(_now, time, task);
    }

    /**
     * @brief 调度定时任务（固定时间间隔，带分组）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param time 任务执行的时间间隔
     * @param group 任务组ID
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     *
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::Schedule
     */
    template<class _Rep, class _Period>
    TaskScheduler& Schedule(std::chrono::duration<_Rep, _Period> const& time,
        group_t const group, task_handler_t const& task)
    {
        return ScheduleAt(_now, time, group, task);
    }

    /**
     * @brief 调度定时任务（随机时间间隔）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择一个时间间隔调度任务
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::Schedule
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& Schedule(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max, task_handler_t const& task)
    {
        return Schedule(randtime(min, max), task);
    }

    /**
     * @brief 调度定时任务（随机时间间隔，带分组）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @param group 任务组ID
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择一个时间间隔调度任务
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::Schedule
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& Schedule(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max, group_t const group,
        task_handler_t const& task)
    {
        return Schedule(randtime(min, max), group, task);
    }

    /**
     * @brief 取消所有任务
     * @return 返回调度器引用，支持链式调用
     *
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::CancelAll
     */
    TaskScheduler& CancelAll();

    /**
     * @brief 取消指定组的所有任务
     * @param group 任务组ID
     * @return 返回调度器引用，支持链式调用
     *
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::CancelGroup
     */
    TaskScheduler& CancelGroup(group_t const group);

    /**
     * @brief 批量取消多个组的任务
     * @param groups 任务组ID向量
     * @return 返回调度器引用，支持链式调用
     *
     * 提示：可以使用初始化列表，如 "{1, 2, 3, 4}"
     */
    TaskScheduler& CancelGroupsOf(std::vector<group_t> const& groups);

    /**
     * @brief 延迟所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param duration 要延迟的时间长度
     * @return 返回调度器引用，支持链式调用
     *
     * 将所有任务的执行时间推迟指定的时长
     */
    template<class _Rep, class _Period>
    TaskScheduler& DelayAll(std::chrono::duration<_Rep, _Period> const& duration)
    {
        _task_holder.ModifyIf([&duration](TaskContainer const& task) -> bool
        {
            task->_end += duration;
            return true;
        });
        return *this;
    }

    /**
     * @brief 延迟所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小延迟时长
     * @param max 最大延迟时长
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择延迟时长，推迟所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& DelayAll(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return DelayAll(randtime(min, max));
    }

    /**
     * @brief 延迟指定组的所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param group 任务组ID
     * @param duration 要延迟的时间长度
     * @return 返回调度器引用，支持链式调用
     *
     * 将指定组中所有任务的执行时间推迟指定的时长
     */
    template<class _Rep, class _Period>
    TaskScheduler& DelayGroup(group_t const group, std::chrono::duration<_Rep, _Period> const& duration)
    {
        _task_holder.ModifyIf([&duration, group](TaskContainer const& task) -> bool
        {
            if (task->IsInGroup(group))
            {
                task->_end += duration;
                return true;
            }
            else
                return false;
        });
        return *this;
    }

    /**
     * @brief 延迟指定组的所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param group 任务组ID
     * @param min 最小延迟时长
     * @param max 最大延迟时长
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择延迟时长，推迟指定组的所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& DelayGroup(group_t const group,
        std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return DelayGroup(group, randtime(min, max));
    }

    /**
     * @brief 重新调度所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param duration 从现在开始的新时间间隔
     * @return 返回调度器引用，支持链式调用
     *
     * 将所有任务的执行时间重新设置为当前时间加上指定时长
     */
    template<class _Rep, class _Period>
    TaskScheduler& RescheduleAll(std::chrono::duration<_Rep, _Period> const& duration)
    {
        auto const end = _now + duration;
        _task_holder.ModifyIf([end](TaskContainer const& task) -> bool
        {
            task->_end = end;
            return true;
        });
        return *this;
    }

    /**
     * @brief 重新调度所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔，重新调度所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& RescheduleAll(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return RescheduleAll(randtime(min, max));
    }

    /**
     * @brief 重新调度指定组的所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param group 任务组ID
     * @param duration 从现在开始的新时间间隔
     * @return 返回调度器引用，支持链式调用
     *
     * 将指定组中所有任务的执行时间重新设置为当前时间加上指定时长
     */
    template<class _Rep, class _Period>
    TaskScheduler& RescheduleGroup(group_t const group, std::chrono::duration<_Rep, _Period> const& duration)
    {
        auto const end = _now + duration;
       _task_holder.ModifyIf([end, group](TaskContainer const& task) -> bool
        {
            if (task->IsInGroup(group))
            {
                task->_end = end;
                return true;
            }
            else
                return false;
        });
        return *this;
    }

    /**
     * @brief 重新调度指定组的所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param group 任务组ID
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @return 返回调度器引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔，重新调度指定组的所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskScheduler& RescheduleGroup(group_t const group,
        std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return RescheduleGroup(group, randtime(min, max));
    }

private:
    /**
     * @brief 插入任务到调度器
     * @param task 要插入的任务容器
     * @return 返回调度器引用，支持链式调用
     */
    TaskScheduler& InsertTask(TaskContainer task);

    /**
     * @brief 在指定时间点调度任务（无分组）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param end 起始时间点
     * @param time 任务执行的时间间隔
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     */
    template<class _Rep, class _Period>
    TaskScheduler& ScheduleAt(timepoint_t const& end,
        std::chrono::duration<_Rep, _Period> const& time, task_handler_t const& task)
    {
        return InsertTask(TaskContainer(new Task(end + time, time, task)));
    }

    /**
     * @brief 在指定时间点调度任务（带分组）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param end 起始时间点
     * @param time 任务执行的时间间隔
     * @param group 任务组ID
     * @param task 任务回调函数
     * @return 返回调度器引用，支持链式调用
     *
     * 警告：不要在任务上下文内部调用此方法！应使用TaskContext::Schedule
     */
    template<class _Rep, class _Period>
    TaskScheduler& ScheduleAt(timepoint_t const& end,
        std::chrono::duration<_Rep, _Period> const& time,
        group_t const group, task_handler_t const& task)
    {
        static repeated_t const DEFAULT_REPEATED = 0;
        return InsertTask(TaskContainer(new Task(end + time, time, group, DEFAULT_REPEATED, task)));
    }

    /**
     * @brief 分发并执行到期任务
     * @param callback 成功回调函数
     *
     * 核心调度函数，处理所有异步任务和到期的定时任务
     */
    void Dispatch(success_t const& callback);
};

/**
 * @class TaskContext
 * @brief 任务上下文 - 提供任务执行时的操作接口
 *
 * 职责：
 *   - 封装任务执行时的上下文信息
 *   - 提供任务重复执行的接口
 *   - 提供任务分组管理的接口
 *   - 提供调度新任务和操作现有任务的接口
 *   - 防止任务上下文被重复消费
 *
 * 使用注意事项：
 *   - TaskContext对象在任务回调函数中作为参数传递
 *   - 可以通过TaskContext重复执行当前任务
 *   - 可以通过TaskContext调度新任务
 *   - 一个TaskContext只能被消费一次（通过Repeat等方法）
 */
class TC_COMMON_API TaskContext
{
    friend class TaskScheduler;

    /// 关联的任务对象
    TaskScheduler::TaskContainer _task;

    /// 任务调度器的弱引用
    std::weak_ptr<TaskScheduler> _owner;

    /// 任务消费标志，防止任务上下文被重复消费
    std::shared_ptr<bool> _consumed;

    /**
     * @brief 向任务调度器分发操作
     * @param apply 要应用的操作函数
     * @return 返回当前上下文引用，支持链式调用
     *
     * 如果任务调度器仍然存在，则对其应用指定的操作函数
     */
    TaskContext& Dispatch(std::function<TaskScheduler&(TaskScheduler&)> const& apply);

public:
    /**
     * @brief 默认构造函数
     *
     * 构造一个空的、已消费的任务上下文
     */
    TaskContext()
        : _task(), _owner(), _consumed(std::make_shared<bool>(true)) { }

    /**
     * @brief 从任务和所有者构造
     * @param task 任务容器（右值引用）
     * @param owner 任务调度器的弱引用（右值引用）
     */
    explicit TaskContext(TaskScheduler::TaskContainer&& task, std::weak_ptr<TaskScheduler>&& owner)
        : _task(task), _owner(owner), _consumed(std::make_shared<bool>(false)) { }

    /**
     * @brief 拷贝构造函数
     * @param right 要拷贝的任务上下文
     */
    TaskContext(TaskContext const& right)
        : _task(right._task), _owner(right._owner), _consumed(right._consumed) { }

    /**
     * @brief 移动构造函数
     * @param right 要移动的任务上下文
     */
    TaskContext(TaskContext&& right)
        : _task(std::move(right._task)), _owner(std::move(right._owner)), _consumed(std::move(right._consumed)) { }

    /**
     * @brief 拷贝赋值运算符
     * @param right 要拷贝的任务上下文
     * @return 返回当前上下文引用
     */
    TaskContext& operator= (TaskContext const& right)
    {
        _task = right._task;
        _owner = right._owner;
        _consumed = right._consumed;
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     * @param right 要移动的任务上下文
     * @return 返回当前上下文引用
     */
    TaskContext& operator= (TaskContext&& right)
    {
        _task = std::move(right._task);
        _owner = std::move(right._owner);
        _consumed = std::move(right._consumed);
        return *this;
    }

    /**
     * @brief 检查任务上下文是否已过期
     * @return 如果任务调度器已被销毁返回true，否则返回false
     */
    bool IsExpired() const;

    /**
     * @brief 检查任务是否属于指定组
     * @param group 要检查的组ID
     * @return 如果任务属于该组返回true，否则返回false
     */
    bool IsInGroup(TaskScheduler::group_t const group) const;

    /**
     * @brief 设置任务的组ID
     * @param group 要设置的组ID
     * @return 返回当前上下文引用，支持链式调用
     */
    TaskContext& SetGroup(TaskScheduler::group_t const group);

    /**
     * @brief 清除任务的组ID
     * @return 返回当前上下文引用，支持链式调用
     */
    TaskContext& ClearGroup();

    /**
     * @brief 获取任务重复计数器
     * @return 返回任务已被重复执行的次数
     */
    TaskScheduler::repeated_t GetRepeatCounter() const;

    /**
     * @brief 重复执行任务并设置新的持续时间
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param duration 新的任务持续时间
     * @return 返回当前上下文引用，支持链式调用
     *
     * 注意：此操作会消费任务上下文，同一个上下文不能再次调用Repeat
     */
    template<class _Rep, class _Period>
    TaskContext& Repeat(std::chrono::duration<_Rep, _Period> const& duration)
    {
        AssertOnConsumed();

        // 设置新的持续时间，更新任务结束时间，增加重复计数器
        _task->_duration = duration;
        _task->_end += duration;
        _task->_repeated += 1;
        (*_consumed) = true;
        return Dispatch(std::bind(&TaskScheduler::InsertTask, std::placeholders::_1, _task));
    }

    /**
     * @brief 使用相同的持续时间重复执行任务
     * @return 返回当前上下文引用，支持链式调用
     *
     * 注意：此操作会消费任务上下文，同一个上下文不能再次调用Repeat
     */
    TaskContext& Repeat()
    {
        return Repeat(_task->_duration);
    }

    /**
     * @brief 使用随机持续时间重复执行任务
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小持续时间
     * @param max 最大持续时间
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择持续时间重复执行任务
     * 注意：此操作会消费任务上下文，同一个上下文不能再次调用Repeat
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& Repeat(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return Repeat(randtime(min, max));
    }

    /**
     * @brief 从上下文中调度异步任务
     * @param callable 要异步执行的可调用对象
     * @return 返回当前上下文引用，支持链式调用
     *
     * 调度一个在下一个更新周期执行的任务
     * 可以安全地在回调函数内修改TaskScheduler
     */
    TaskContext& Async(std::function<void()> const& callable);

    /**
     * @brief 从上下文中调度定时任务（固定时间间隔）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param time 任务执行的时间间隔
     * @param task 任务回调函数
     * @return 返回当前上下文引用，支持链式调用
     *
     * 注意：新任务可能会立即执行！
     * 如果需要确保任务在下一个更新周期执行，请使用 TaskScheduler::Async
     */
    template<class _Rep, class _Period>
    TaskContext& Schedule(std::chrono::duration<_Rep, _Period> const& time,
        TaskScheduler::task_handler_t const& task)
    {
        auto const end = _task->_end;
        return Dispatch([end, time, task](TaskScheduler& scheduler) -> TaskScheduler&
        {
            return scheduler.ScheduleAt<_Rep, _Period>(end, time, task);
        });
    }

    /**
     * @brief 从上下文中调度定时任务（固定时间间隔，带分组）
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param time 任务执行的时间间隔
     * @param group 任务组ID
     * @param task 任务回调函数
     * @return 返回当前上下文引用，支持链式调用
     *
     * 注意：新任务可能会立即执行！
     * 如果需要确保任务在下一个更新周期执行，请使用 TaskScheduler::Async
     */
    template<class _Rep, class _Period>
    TaskContext& Schedule(std::chrono::duration<_Rep, _Period> const& time,
        TaskScheduler::group_t const group, TaskScheduler::task_handler_t const& task)
    {
        auto const end = _task->_end;
        return Dispatch([end, time, group, task](TaskScheduler& scheduler) -> TaskScheduler&
        {
            return scheduler.ScheduleAt<_Rep, _Period>(end, time, group, task);
        });
    }

    /**
     * @brief 从上下文中调度定时任务（随机时间间隔）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @param task 任务回调函数
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔调度任务
     * 注意：新任务可能会立即执行！
     * 如果需要确保任务在下一个更新周期执行，请使用 TaskScheduler::Async
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& Schedule(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max, TaskScheduler::task_handler_t const& task)
    {
        return Schedule(randtime(min, max), task);
    }

    /**
     * @brief 从上下文中调度定时任务（随机时间间隔，带分组）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @param group 任务组ID
     * @param task 任务回调函数
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔调度任务
     * 注意：新任务可能会立即执行！
     * 如果需要确保任务在下一个更新周期执行，请使用 TaskScheduler::Async
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& Schedule(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max, TaskScheduler::group_t const group,
        TaskScheduler::task_handler_t const& task)
    {
        return Schedule(randtime(min, max), group, task);
    }

    /**
     * @brief 从上下文中取消所有任务
     * @return 返回当前上下文引用，支持链式调用
     *
     * 取消关联任务调度器中的所有任务
     */
    TaskContext& CancelAll();

    /**
     * @brief 从上下文中取消指定组的任务
     * @param group 要取消的任务组ID
     * @return 返回当前上下文引用，支持链式调用
     *
     * 取消关联任务调度器中指定组的所有任务
     */
    TaskContext& CancelGroup(TaskScheduler::group_t const group);

    /**
     * @brief 从上下文中批量取消多个组的任务
     * @param groups 要取消的任务组ID向量
     * @return 返回当前上下文引用，支持链式调用
     *
     * 取消关联任务调度器中多个组的所有任务
     * 提示：可以使用初始化列表，如 "{1, 2, 3, 4}"
     */
    TaskContext& CancelGroupsOf(std::vector<TaskScheduler::group_t> const& groups);

    /**
     * @brief 从上下文中延迟所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param duration 要延迟的时间长度
     * @return 返回当前上下文引用，支持链式调用
     *
     * 将关联任务调度器中所有任务的执行时间推迟指定的时长
     */
    template<class _Rep, class _Period>
    TaskContext& DelayAll(std::chrono::duration<_Rep, _Period> const& duration)
    {
        return Dispatch(std::bind(&TaskScheduler::DelayAll<_Rep, _Period>, std::placeholders::_1, duration));
    }

    /**
     * @brief 从上下文中延迟所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小延迟时长
     * @param max 最大延迟时长
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择延迟时长，推迟所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& DelayAll(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return DelayAll(randtime(min, max));
    }

    /**
     * @brief 从上下文中延迟指定组的任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param group 任务组ID
     * @param duration 要延迟的时间长度
     * @return 返回当前上下文引用，支持链式调用
     *
     * 将关联任务调度器中指定组所有任务的执行时间推迟指定的时长
     */
    template<class _Rep, class _Period>
    TaskContext& DelayGroup(TaskScheduler::group_t const group, std::chrono::duration<_Rep, _Period> const& duration)
    {
        return Dispatch(std::bind(&TaskScheduler::DelayGroup<_Rep, _Period>, std::placeholders::_1, group, duration));
    }

    /**
     * @brief 从上下文中延迟指定组的任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param group 任务组ID
     * @param min 最小延迟时长
     * @param max 最大延迟时长
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择延迟时长，推迟指定组的所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& DelayGroup(TaskScheduler::group_t const group,
        std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return DelayGroup(group, randtime(min, max));
    }

    /**
     * @brief 从上下文中重新调度所有任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param duration 从现在开始的新时间间隔
     * @return 返回当前上下文引用，支持链式调用
     *
     * 将关联任务调度器中所有任务的执行时间重新设置为当前时间加上指定时长
     */
    template<class _Rep, class _Period>
    TaskContext& RescheduleAll(std::chrono::duration<_Rep, _Period> const& duration)
    {
        return Dispatch(std::bind(&TaskScheduler::RescheduleAll, std::placeholders::_1, duration));
    }

    /**
     * @brief 从上下文中重新调度所有任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔，重新调度所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& RescheduleAll(std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return RescheduleAll(randtime(min, max));
    }

    /**
     * @brief 从上下文中重新调度指定组的任务
     * @tparam _Rep 时间间隔的数值类型
     * @tparam _Period 时间间隔的周期类型
     * @param group 任务组ID
     * @param duration 从现在开始的新时间间隔
     * @return 返回当前上下文引用，支持链式调用
     *
     * 将关联任务调度器中指定组所有任务的执行时间重新设置为当前时间加上指定时长
     */
    template<class _Rep, class _Period>
    TaskContext& RescheduleGroup(TaskScheduler::group_t const group, std::chrono::duration<_Rep, _Period> const& duration)
    {
        return Dispatch(std::bind(&TaskScheduler::RescheduleGroup<_Rep, _Period>, std::placeholders::_1, group, duration));
    }

    /**
     * @brief 从上下文中重新调度指定组的任务（随机时长）
     * @tparam _RepLeft 最小时间间隔的数值类型
     * @tparam _PeriodLeft 最小时间间隔的周期类型
     * @tparam _RepRight 最大时间间隔的数值类型
     * @tparam _PeriodRight 最大时间间隔的周期类型
     * @param group 任务组ID
     * @param min 最小时间间隔
     * @param max 最大时间间隔
     * @return 返回当前上下文引用，支持链式调用
     *
     * 在[min, max]范围内随机选择时间间隔，重新调度指定组的所有任务
     */
    template<class _RepLeft, class _PeriodLeft, class _RepRight, class _PeriodRight>
    TaskContext& RescheduleGroup(TaskScheduler::group_t const group,
        std::chrono::duration<_RepLeft, _PeriodLeft> const& min,
        std::chrono::duration<_RepRight, _PeriodRight> const& max)
    {
        return RescheduleGroup(group, randtime(min, max));
    }

private:
    /**
     * @brief 断言任务上下文未被消费
     *
     * 检查任务上下文是否已经被消费，如果已消费则触发断言失败
     */
    void AssertOnConsumed() const;

    /**
     * @brief 调用执行任务
     *
     * 执行任务的实际回调函数
     */
    void Invoke();
};

#endif /// _TASK_SCHEDULER_H_
