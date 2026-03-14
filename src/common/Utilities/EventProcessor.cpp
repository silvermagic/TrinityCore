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
 * @file EventProcessor.cpp
 *
 * @brief 事件处理器实现文件
 *
 * 本文件实现了事件处理系统的核心功能，包括：
 * - BasicEvent: 事件基类，定义事件的基本属性和状态管理
 * - EventProcessor: 事件处理器，管理事件队列的调度和执行
 *
 * 事件处理系统是 TrinityCore 的核心调度机制，用于处理各种延迟任务，
 * 如法术效果、AI 行为、定时器等。事件按照时间顺序执行，支持事件的
 * 添加、修改、中止和删除操作。
 *
 * 主要特性：
 * - 基于时间的优先级队列（使用 multimap 实现）
 * - 支持事件的中止和强制终止
 * - 支持不可删除事件的特殊处理
 * - 线程安全的事件执行（需在单线程中使用）
 *
 * @see EventProcessor.h
 */

#include "EventProcessor.h"
#include "Errors.h"

/**
 * @brief 调度事件中止
 *
 * 职责：
 *   标记正在运行的事件需要被中止。这允许事件在下次更新时被安全地终止。
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 断言检查事件是否正在运行（防止重复调度中止）
 *   2. 将中止状态设置为"已调度中止"
 *
 * 注意：
 *   此函数只能对正在运行的事件调用，否则会触发断言失败
 */
void BasicEvent::ScheduleAbort()
{
    ASSERT(IsRunning()
           && "Tried to scheduled the abortion of an event twice!");
    m_abortState = AbortState::STATE_ABORT_SCHEDULED;
}

/**
 * @brief 设置事件为已中止状态
 *
 * 职责：
 *   将事件标记为已中止状态。这是事件中止流程的最后一步。
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 断言检查事件是否已经被中止（防止重复中止）
 *   2. 将中止状态设置为"已中止"
 *
 * 注意：
 *   此函数只能在事件未被中止时调用，否则会触发断言失败
 */
void BasicEvent::SetAborted()
{
    ASSERT(!IsAborted()
           && "Tried to abort an already aborted event!");
    m_abortState = AbortState::STATE_ABORTED;
}

/**
 * @brief 事件处理器析构函数
 *
 * 职责：
 *   清理所有待处理的事件，释放资源。
 *
 * 参数：
 *   无
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   调用 KillAllEvents(true) 强制终止并删除所有事件
 *
 * 注意：
 *   参数 true 表示强制删除所有事件，包括不可删除的事件
 */
EventProcessor::~EventProcessor()
{
    KillAllEvents(true);
}

/**
 * @brief 更新事件处理器
 *
 * 职责：
 *   处理所有到期的事件。这是事件处理器的核心方法，负责执行、中止或重新调度事件。
 *
 * 参数：
 *   p_time - 自上次更新以来经过的时间（毫秒）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 更新当前时间
 *   2. 遍历事件队列中所有到期的事件
 *   3. 对每个到期的事件：
 *      a. 从队列中移除事件
 *      b. 如果事件正在运行：执行事件，如果执行返回 true 则删除事件
 *      c. 如果事件已调度中止：调用 Abort()，标记为已中止
 *      d. 如果事件可删除：删除事件
 *      e. 如果事件不可删除：重新调度到下一个更新周期检查
 *
 * 注意：
 *   - 事件按照时间顺序执行（multimap 自动排序）
 *   - 正在运行的事件会立即执行
 *   - 不可删除的事件会在 1ms 后重新检查
 */
void EventProcessor::Update(uint32 p_time)
{
    // 更新当前时间
    m_time += p_time;

    // 主事件循环
    std::multimap<uint64, BasicEvent*>::iterator i;
    while (((i = m_events.begin()) != m_events.end()) && i->first <= m_time)
    {
        // 从队列中获取并移除事件
        BasicEvent* event = i->second;
        m_events.erase(i);

        if (event->IsRunning())
        {
            // 执行正在运行的事件
            if (event->Execute(m_time, p_time))
            {
                // 如果事件未重新添加，则完全销毁事件
                delete event;
            }
            continue;
        }

        if (event->IsAbortScheduled())
        {
            // 中止已调度中止的事件
            event->Abort(m_time);
            // 标记事件为已中止
            event->SetAborted();
        }

        if (event->IsDeletable())
        {
            // 删除可删除的事件
            delete event;
            continue;
        }

        // 重新调度不可删除的事件到下一个更新周期检查
        AddEvent(event, CalculateTime(1ms), false);
    }
}

/**
 * @brief 终止并删除所有事件
 *
 * 职责：
 *   清理事件处理器中的所有事件，可选择强制删除不可删除的事件。
 *
 * 参数：
 *   force - 是否强制删除所有事件
 *           true: 强制删除所有事件，包括不可删除的事件
 *           false: 仅删除可删除的事件，保留不可删除的事件
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 遍历所有事件
 *   2. 对每个未中止的事件：标记为已中止并调用 Abort()
 *   3. 根据强制标志处理事件删除：
 *      - force=true: 删除所有事件，最后清空容器
 *      - force=false: 仅删除可删除的事件，从容器中移除
 *   4. 如果强制模式，最后清空整个事件容器
 *
 * 注意：
 *   - 强制模式用于析构函数中，确保所有资源被释放
 *   - 非强制模式保留不可删除的事件，可能是因为事件需要特殊处理
 */
void EventProcessor::KillAllEvents(bool force)
{
    for (auto itr = m_events.begin(); itr != m_events.end();)
    {
        // 中止尚未中止的事件
        if (!itr->second->IsAborted())
        {
            itr->second->SetAborted();
            itr->second->Abort(m_time);
        }

        // 当不强制终止时，跳过不可删除的事件
        if (!force && !itr->second->IsDeletable())
        {
            ++itr;
            continue;
        }

        delete itr->second;

        if (force)
            ++itr; // 强制模式下清理整个容器
        else
            itr = m_events.erase(itr); // 非强制模式下从容器中移除
    }

    if (force)
        m_events.clear();
}

/**
 * @brief 添加事件到处理器
 *
 * 职责：
 *   将一个事件添加到事件处理器的队列中，并设置其执行时间。
 *
 * 参数：
 *   event      - 要添加的事件指针
 *   e_time     - 事件的执行时间（绝对时间，相对于 EventProcessor 的时间轴）
 *   set_addtime - 是否设置事件的添加时间
 *                 true: 设置事件添加时间为当前时间（用于新事件）
 *                 false: 不修改添加时间（用于重新调度的事件）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 如果需要，设置事件的添加时间为当前时间
 *   2. 设置事件的执行时间
 *   3. 将事件插入到事件队列中（使用执行时间作为键）
 *
 * 注意：
 *   - 事件按执行时间排序存储在 multimap 中
 *   - 同一执行时间可以有多个事件
 *   - 事件的所有权转移给 EventProcessor，由其负责删除
 */
void EventProcessor::AddEvent(BasicEvent* event, Milliseconds e_time, bool set_addtime)
{
    if (set_addtime)
        event->m_addTime = m_time;
    event->m_execTime = e_time.count();
    m_events.insert(std::pair<uint64, BasicEvent*>(e_time.count(), event));
}

/**
 * @brief 修改事件的执行时间
 *
 * 职责：
 *   更改已添加事件的执行时间，通过移除并重新插入实现时间调整。
 *
 * 参数：
 *   event   - 要修改的事件指针
 *   newTime - 新的执行时间（绝对时间，相对于 EventProcessor 的时间轴）
 *
 * 返回值：
 *   无
 *
 * 主要流程：
 *   1. 遍历事件队列查找指定的事件
 *   2. 找到后更新事件的执行时间
 *   3. 从队列中移除事件
 *   4. 以新的执行时间重新插入事件到队列
 *
 * 注意：
 *   - 此操作的时间复杂度为 O(n)，因为需要线性查找事件
 *   - 如果事件不在队列中，函数不会执行任何操作
 *   - 重新插入会自动按照新的执行时间排序
 */
void EventProcessor::ModifyEventTime(BasicEvent* event, Milliseconds newTime)
{
    for (auto itr = m_events.begin(); itr != m_events.end(); ++itr)
    {
        if (itr->second != event)
            continue;

        event->m_execTime = newTime.count();
        m_events.erase(itr);
        m_events.insert(std::pair<uint64, BasicEvent*>(newTime.count(), event));
        break;
    }
}
