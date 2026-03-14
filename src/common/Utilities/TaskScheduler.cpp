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
 * @file TaskScheduler.cpp
 * @brief 任务调度器实现文件
 *
 * 本模块实现了 TrinityCore 的任务调度系统，提供基于时间的异步任务调度功能。
 *
 * 主要功能：
 * - 定时任务调度：支持延迟执行和周期性重复执行的任务
 * - 异步任务队列：支持立即执行的异步任务
 * - 任务分组管理：支持按组批量取消和管理任务
 * - 验证器机制：支持在任务执行前进行条件验证
 *
 * 核心组件：
 * - TaskScheduler：任务调度器主类，负责任务的管理和调度
 * - TaskQueue：任务队列，使用 std::multiset 实现按时间排序的任务容器
 * - TaskContext：任务上下文，提供任务执行时的操作接口
 *
 * 调度流程：
 * 1. 用户通过 Schedule 等方法添加任务到 TaskQueue
 * 2. Update 方法被周期性调用，检查并执行到期的任务
 * 3. 任务执行时通过 TaskContext 提供操作接口
 * 4. 支持任务的重复执行、重新调度等高级功能
 *
 * 使用场景：
 * - 游戏循环中的定时事件（如技能冷却、Buff 过期等）
 * - 延迟执行的逻辑（如延迟传送、延迟伤害等）
 * - 周期性任务（如定时保存、定期检查等）
 *
 * 线程安全：
 * - 任务调度器本身不是线程安全的
 * - 所有操作应该在同一线程中进行（通常是主线程/世界线程）
 */

#include "TaskScheduler.h"
#include "Errors.h"

/**
 * @brief 清空验证器
 *
 * @职责：重置任务调度器的验证谓词为空验证器，取消所有验证逻辑
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 将内部验证谓词设置为 EmptyValidator（始终返回 true 的空验证器）
 *   2. 返回自身引用以便链式调用
 */
TaskScheduler& TaskScheduler::ClearValidator()
{
    _predicate = EmptyValidator;
    return *this;
}

/**
 * @brief 更新任务调度器（使用当前时间）
 *
 * @职责：执行一轮任务调度更新，处理所有到期的任务
 *
 * @参数：
 *   - callback：成功回调函数，在所有任务处理完成后执行
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 获取当前时间戳
 *   2. 调用 Dispatch 分发并执行所有到期任务
 *   3. 返回自身引用
 */
TaskScheduler& TaskScheduler::Update(success_t const& callback)
{
    _now = clock_t::now();
    Dispatch(callback);
    return *this;
}

/**
 * @brief 更新任务调度器（使用指定的毫秒偏移量）
 *
 * @职责：执行一轮任务调度更新，使用指定的毫秒数作为时间偏移
 *
 * @参数：
 *   - milliseconds：毫秒数偏移量
 *   - callback：成功回调函数，在所有任务处理完成后执行
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 将毫秒数转换为 std::chrono::milliseconds
 *   2. 调用 Update 的重载版本
 */
TaskScheduler& TaskScheduler::Update(size_t const milliseconds, success_t const& callback)
{
    return Update(std::chrono::milliseconds(milliseconds), callback);
}

/**
 * @brief 添加异步任务
 *
 * @职责：将一个可调用对象添加到异步任务队列，将在下一次 Update 时执行
 *
 * @参数：
 *   - callable：可调用对象（通常是 lambda 或函数对象）
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 将可调用对象推入异步任务队列 _asyncHolder
 *   2. 返回自身引用
 */
TaskScheduler& TaskScheduler::Async(std::function<void()> const& callable)
{
    _asyncHolder.push(callable);
    return *this;
}

/**
 * @brief 取消所有任务
 *
 * @职责：清空任务调度器中的所有定时任务和异步任务
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 清空任务容器 _task_holder
 *   2. 重置异步任务队列 _asyncHolder 为新的空队列
 *   3. 返回自身引用
 */
TaskScheduler& TaskScheduler::CancelAll()
{
    /// 清空任务容器
    _task_holder.Clear();
    _asyncHolder = AsyncHolder();
    return *this;
}

/**
 * @brief 取消指定组的所有任务
 *
 * @职责：从任务队列中移除所有属于指定组的任务
 *
 * @参数：
 *   - group：任务组 ID
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 使用 RemoveIf 遍历任务队列
 *   2. 移除所有 IsInGroup(group) 返回 true 的任务
 *   3. 返回自身引用
 */
TaskScheduler& TaskScheduler::CancelGroup(group_t const group)
{
    _task_holder.RemoveIf([group](TaskContainer const& task) -> bool
    {
        return task->IsInGroup(group);
    });
    return *this;
}

/**
 * @brief 取消多个组的所有任务
 *
 * @职责：批量取消多个任务组中的所有任务
 *
 * @参数：
 *   - groups：任务组 ID 的向量列表
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 遍历所有组 ID
 *   2. 对每个组调用 CancelGroup 方法
 *   3. 返回自身引用
 */
TaskScheduler& TaskScheduler::CancelGroupsOf(std::vector<group_t> const& groups)
{
    std::for_each(groups.begin(), groups.end(),
        std::bind(&TaskScheduler::CancelGroup, this, std::placeholders::_1));

    return *this;
}

/**
 * @brief 插入任务到调度器
 *
 * @职责：将一个任务容器插入到任务队列中
 *
 * @参数：
 *   - task：要插入的任务容器（使用移动语义）
 *
 * @返回值：返回当前任务调度器的引用，支持链式调用
 *
 * @主要流程：
 *   1. 使用 std::move 将任务推入任务队列
 *   2. 返回自身引用
 */
TaskScheduler& TaskScheduler::InsertTask(TaskContainer task)
{
    _task_holder.Push(std::move(task));
    return *this;
}

/**
 * @brief 分发并执行任务
 *
 * @职责：核心调度函数，处理所有异步任务和到期的定时任务
 *
 * @参数：
 *   - callback：成功回调函数，在所有任务处理完成后执行
 *
 * @主要流程：
 *   1. 检查验证谓词，如果验证失败则立即返回
 *   2. 处理所有异步任务：
 *      - 从 _asyncHolder 队列中依次取出任务执行
 *      - 每次执行后检查验证谓词
 *   3. 处理定时任务：
 *      - 循环检查任务队列，直到队列为空或遇到未到期任务
 *      - 弹出到期任务，创建任务上下文
 *      - 执行任务，并检查验证谓词
 *   4. 所有任务处理完成后，调用最终的回调函数
 */
void TaskScheduler::Dispatch(success_t const& callback)
{
    // 步骤 1: 验证检查
    // 在开始处理任务前，先检查验证谓词
    // 如果验证失败（如对象已销毁或条件不满足），立即中止整个分发过程
    if (!_predicate())
        return;

    // 步骤 2: 处理异步任务队列
    // 异步任务是立即执行的任务，不依赖时间调度
    // 它们被添加到 _asyncHolder 队列中，会在每次 Update 时优先执行
    while (!_asyncHolder.empty())
    {
        // 执行队首的异步任务
        _asyncHolder.front()();
        // 从队列中移除已执行的任务
        _asyncHolder.pop();

        // 每次执行异步任务后都检查验证谓词
        // 这允许任务在执行过程中改变调度器状态（如取消调度）
        if (!_predicate())
            return;
    }

    // 步骤 3: 处理定时任务队列
    // TaskQueue 使用 std::multiset 实现，任务按结束时间自动排序
    // 最早到期的任务位于队列开头
    while (!_task_holder.IsEmpty())
    {
        // 检查队列中最早到期任务的执行时间
        // 如果最早的任务还未到期，后续任务肯定也未到期，直接退出循环
        // 这是优化的关键：利用 multiset 的排序特性提前终止检查
        if (_task_holder.First()->_end > _now)
            break;

        // 从队列中弹出任务，准备执行
        // 使用移动语义避免不必要的拷贝
        TaskContainer task = _task_holder.Pop();

        // 创建任务上下文
        // 上下文包含任务本身和对调度器的弱引用
        // 弱引用确保即使调度器被销毁，任务上下文也能安全检测到
        // 这是 RAII 和智能指针的最佳实践
        TaskContext context(std::move(task), std::weak_ptr<TaskScheduler>(self_reference));

        // 调用任务的回调函数
        // 执行用户定义的任务逻辑
        context.Invoke();

        // 任务执行后再次检查验证谓词
        // 这允许任务内部调用 CancelAll 等方法来中止后续任务执行
        if (!_predicate())
            return;
    }

    // 步骤 4: 完成回调
    // 所有任务处理完成后，调用用户提供的成功回调
    // 这通常用于通知调用者本轮调度已完成
    callback();
}

/**
 * @brief 将任务推入任务队列
 *
 * @职责：向任务队列中插入一个新任务
 *
 * @参数：
 *   - task：要插入的任务容器（右值引用，使用移动语义）
 *
 * @主要流程：
 *   1. 使用 std::multiset 的 insert 方法插入任务
 *   2. multiset 会根据任务的结束时间自动排序
 */
void TaskScheduler::TaskQueue::Push(TaskContainer&& task)
{
    // 将任务插入到 multiset 容器中
    // multiset 会根据任务对象的 _end 时间自动排序
    // 这保证了队列开头的任务总是最早到期的
    // 时间复杂度: O(log n)，n 为队列中任务数量
    container.insert(task);
}

/**
 * @brief 从任务队列弹出最早的任务
 *
 * @职责：移除并返回队列中最早到期的任务
 *
 * @返回值：返回被弹出的任务容器
 *
 * @主要流程：
 *   1. 获取 multiset 开头的任务（最早到期）
 *   2. 从容器中删除该任务
 *   3. 返回任务容器
 */
auto TaskScheduler::TaskQueue::Pop() -> TaskContainer
{
    // 获取队列开头的任务（最早到期的任务）
    // 由于 multiset 按 _end 时间排序，begin() 总是指向最早的任务
    TaskContainer result = *container.begin();

    // 从容器中移除该任务
    // 这会使指向该任务的迭代器失效，但不影响其他迭代器
    container.erase(container.begin());

    // 返回任务容器（使用返回值优化 RVO 避免拷贝）
    return result;
}

/**
 * @brief 获取队列中最早的任务
 *
 * @职责：返回任务队列中最早到期的任务，但不移除它
 *
 * @返回值：返回最早到期任务的常量引用
 *
 * @主要流程：
 *   1. 返回 multiset 开头元素的引用（最早到期）
 */
auto TaskScheduler::TaskQueue::First() const -> TaskContainer const&
{
    return *container.begin();
}

/**
 * @brief 清空任务队列
 *
 * @职责：移除任务队列中的所有任务
 *
 * @主要流程：
 *   1. 调用 multiset 的 clear 方法清空容器
 */
void TaskScheduler::TaskQueue::Clear()
{
    container.clear();
}

/**
 * @brief 根据条件移除任务
 *
 * @职责：从任务队列中移除满足过滤条件的所有任务
 *
 * @参数：
 *   - filter：过滤函数，返回 true 表示该任务应被移除
 *
 * @主要流程：
 *   1. 遍历 multiset 容器
 *   2. 对每个任务应用过滤函数
 *   3. 如果过滤函数返回 true，删除该任务
 *   4. 继续处理下一个任务
 */
void TaskScheduler::TaskQueue::RemoveIf(std::function<bool(TaskContainer const&)> const& filter)
{
    // 遍历 multiset 容器，使用迭代器删除模式
    // 注意：不能使用简单的 for 循环，因为删除元素会使迭代器失效
    for (auto itr = container.begin(); itr != container.end();)
        if (filter(*itr))
            // filter 返回 true，删除该任务
            // erase 返回下一个有效迭代器，避免迭代器失效问题
            itr = container.erase(itr);
        else
            // filter 返回 false，保留该任务，继续检查下一个
            ++itr;
}

/**
 * @brief 根据条件修改任务并重新排序
 *
 * @职责：修改满足条件的任务，并重新插入到队列中以保持正确的时间顺序
 *
 * @参数：
 *   - filter：过滤函数，返回 true 表示该任务需要修改并重新排序
 *
 * @主要流程：
 *   1. 创建临时容器缓存需要重新排序的任务
 *   2. 遍历 multiset，对满足条件的任务：
 *      - 从容器中移除
 *      - 添加到缓存
 *   3. 将缓存中的所有任务重新插入到 multiset
 *   4. multiset 会根据修改后的结束时间自动重新排序
 */
void TaskScheduler::TaskQueue::ModifyIf(std::function<bool(TaskContainer const&)> const& filter)
{
    // 创建临时容器缓存需要重新排序的任务
    // 为什么需要缓存？
    // 因为 multiset 中的元素是 const 的，不能直接修改
    // 必须先删除再重新插入，而删除会使迭代器失效
    std::vector<TaskContainer> cache;

    // 遍历任务队列，找出需要修改的任务
    for (auto itr = container.begin(); itr != container.end();)
        if (filter(*itr))
        {
            // 满足条件的任务：从 multiset 中移除并添加到缓存
            // filter 函数可能会修改任务的 _end 时间或其他属性
            // 由于 multiset 按 _end 排序，修改后需要重新插入才能保持正确顺序
            cache.push_back(*itr);
            // erase 返回下一个有效迭代器，避免迭代器失效问题
            itr = container.erase(itr);
        }
        else
            ++itr;

    // 将缓存中的任务重新插入到 multiset
    // multiset 会根据任务修改后的 _end 时间自动排序
    // 这确保了任务队列始终按时间顺序排列
    container.insert(cache.begin(), cache.end());
}

/**
 * @brief 检查任务队列是否为空
 *
 * @职责：判断任务队列中是否还有待处理的任务
 *
 * @返回值：如果队列为空返回 true，否则返回 false
 *
 * @主要流程：
 *   1. 调用 multiset 的 empty 方法检查容器状态
 */
bool TaskScheduler::TaskQueue::IsEmpty() const
{
    return container.empty();
}

/**
 * @brief 向任务调度器分发操作
 *
 * @职责：如果任务调度器仍然存在，则对其应用指定的操作函数
 *
 * @参数：
 *   - apply：要应用到任务调度器的操作函数
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 尝试从弱引用 _owner 锁定任务调度器
 *   2. 如果锁定成功（调度器仍然存在），执行 apply 函数
 *   3. 返回自身引用
 */
TaskContext& TaskContext::Dispatch(std::function<TaskScheduler&(TaskScheduler&)> const& apply)
{
    if (auto const owner = _owner.lock())
        apply(*owner);

    return *this;
}

/**
 * @brief 检查任务上下文是否已过期
 *
 * @职责：判断关联的任务调度器是否已被销毁
 *
 * @返回值：如果任务调度器已被销毁返回 true，否则返回 false
 *
 * @主要流程：
 *   1. 检查弱引用 _owner 是否过期
 */
bool TaskContext::IsExpired() const
{
    return _owner.expired();
}

/**
 * @brief 检查任务是否属于指定组
 *
 * @职责：判断当前任务是否属于指定的任务组
 *
 * @参数：
 *   - group：要检查的任务组 ID
 *
 * @返回值：如果任务属于该组返回 true，否则返回 false
 *
 * @主要流程：
 *   1. 委托给任务对象的 IsInGroup 方法
 */
bool TaskContext::IsInGroup(TaskScheduler::group_t const group) const
{
    return _task->IsInGroup(group);
}

/**
 * @brief 设置任务的组 ID
 *
 * @职责：将当前任务分配到指定的任务组
 *
 * @参数：
 *   - group：要设置的组 ID
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 设置任务对象的 _group 成员
 *   2. 返回自身引用
 */
TaskContext& TaskContext::SetGroup(TaskScheduler::group_t const group)
{
    _task->_group = group;
    return *this;
}

/**
 * @brief 清除任务的组 ID
 *
 * @职责：将当前任务从任何任务组中移除
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 将任务对象的 _group 成员设置为 std::nullopt（空值）
 *   2. 返回自身引用
 */
TaskContext& TaskContext::ClearGroup()
{
    _task->_group = std::nullopt;
    return *this;
}

/**
 * @brief 获取任务重复计数器
 *
 * @职责：获取当前任务已经被重复执行的次数
 *
 * @返回值：返回重复执行的次数（repeated_t 类型）
 *
 * @主要流程：
 *   1. 返回任务对象的 _repeated 成员值
 */
TaskScheduler::repeated_t TaskContext::GetRepeatCounter() const
{
    return _task->_repeated;
}

/**
 * @brief 添加异步任务
 *
 * @职责：向关联的任务调度器添加一个异步任务
 *
 * @参数：
 *   - callable：要异步执行的可调用对象
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 通过 Dispatch 方法将异步任务添加到任务调度器
 *   2. 使用 std::bind 绑定 TaskScheduler::Async 方法
 */
TaskContext& TaskContext::Async(std::function<void()> const& callable)
{
    return Dispatch(std::bind(&TaskScheduler::Async, std::placeholders::_1, callable));
}

/**
 * @brief 取消所有任务
 *
 * @职责：通过任务上下文取消关联任务调度器中的所有任务
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 通过 Dispatch 方法调用 TaskScheduler::CancelAll
 *   2. 使用 std::mem_fn 创建成员函数指针
 */
TaskContext& TaskContext::CancelAll()
{
    return Dispatch(std::mem_fn(&TaskScheduler::CancelAll));
}

/**
 * @brief 取消指定组的任务
 *
 * @职责：通过任务上下文取消关联任务调度器中指定组的所有任务
 *
 * @参数：
 *   - group：要取消的任务组 ID
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 通过 Dispatch 方法调用 TaskScheduler::CancelGroup
 *   2. 使用 std::bind 绑定参数并传递给调度器
 */
TaskContext& TaskContext::CancelGroup(TaskScheduler::group_t const group)
{
    return Dispatch(std::bind(&TaskScheduler::CancelGroup, std::placeholders::_1, group));
}

/**
 * @brief 取消多个组的任务
 *
 * @职责：通过任务上下文批量取消关联任务调度器中多个组的所有任务
 *
 * @参数：
 *   - groups：要取消的任务组 ID 向量
 *
 * @返回值：返回当前任务上下文的引用，支持链式调用
 *
 * @主要流程：
 *   1. 通过 Dispatch 方法调用 TaskScheduler::CancelGroupsOf
 *   2. 使用 std::bind 和 std::cref 绑定参数并传递给调度器
 */
TaskContext& TaskContext::CancelGroupsOf(std::vector<TaskScheduler::group_t> const& groups)
{
    return Dispatch(std::bind(&TaskScheduler::CancelGroupsOf, std::placeholders::_1, std::cref(groups)));
}

/**
 * @brief 断言任务上下文未被消费
 *
 * @职责：检查任务上下文是否已经被消费，如果已消费则触发断言失败
 *
 * @主要流程：
 *   1. 检查 _consumed 标志是否为 true
 *   2. 如果已消费，触发断言错误，提示"错误的任务逻辑，任务上下文已经被消费"
 *
 * @注意事项：
 *   - 如果遇到此断言，请检查是否重复使用了 TaskContext
 *   - 每个 TaskContext 只应该被消费一次
 */
void TaskContext::AssertOnConsumed() const
{
    // 此处适配 TC 以防止静态分析工具警告
    // 如果遇到此断言，请检查是否重复使用了 TaskContext 超过 1 次！
    ASSERT(!(*_consumed) && "Bad task logic, task context was consumed already!");
}

/**
 * @brief 调用执行任务
 *
 * @职责：执行任务的实际回调函数
 *
 * @主要流程：
 *   1. 调用任务对象的 _task 回调函数，传递当前上下文作为参数
 *   2. 这会执行用户定义的任务逻辑
 */
void TaskContext::Invoke()
{
    _task->_task(*this);
}
