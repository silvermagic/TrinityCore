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
// EventMap 实现文件
// ============================================================================
// 模块职责：
//   实现事件调度系统的核心逻辑，提供事件的调度、执行、取消、延迟等功能
//
// 主要功能：
//   - 事件调度：支持固定时间和随机时间范围的事件调度
//   - 事件执行：按时间顺序执行到期事件，支持阶段过滤
//   - 事件管理：取消、延迟、重新调度事件
//   - 阶段控制：通过阶段掩码控制事件的执行条件
//
// 性能考虑：
//   - 使用 std::multimap 实现时间排序
//   - 使用 extract/insert 节点操作优化延迟事件的性能
//   - 避免在热点路径上的内存分配
//
// 依赖：
//   - Random.h: 提供随机时间生成功能
// ============================================================================

#include "EventMap.h"
#include "Random.h"

/**
 * @brief 重置事件映射表
 *
 * 清空所有已调度的事件，重置时间和阶段到初始状态。
 * 这是一个完全重置操作，会清除所有状态信息。
 *
 * 调用时机：
 *   - AI 脱离战斗时，清除所有战斗相关事件
 *   - 实体重置时（如 BOSS 重置）
 *   - 游戏对象状态切换时
 *   - 任务取消或完成时
 *
 * 注意事项：
 *   - 会删除所有事件，包括延迟事件
 *   - 时间重置为最小值（相当于重新开始计时）
 *   - 阶段掩码重置为 0（无阶段限制）
 *   - 清空 _lastEvent（通过清空 _eventMap 实现）
 *
 * 性能：
 *   时间复杂度 O(n)，需要清除所有事件
 */
void EventMap::Reset()
{
    _eventMap.clear();
    _time = TimePoint::min();
    _phaseMask = 0;
}

/**
 * @brief 设置当前阶段（绝对设置）
 *
 * 设置事件映射表的当前阶段掩码，完全替换现有阶段。
 * 这是一个绝对设置操作，会清除所有其他阶段。
 *
 * @param phase 要设置的阶段，有效值：1-8，0 表示重置阶段（清除所有阶段）
 *
 * 主要逻辑：
 *   - phase == 0: 清除所有阶段限制，_phaseMask 设为 0
 *   - phase >= 1 && phase <= 8: 设置为单一阶段，通过位运算转换为掩码
 *   - phase > 8: 无效值，不做任何操作（由于 PhaseMask 是 uint8，最多 8 位）
 *
 * 调用时机：
 *   - BOSS 进入新的战斗阶段（如从阶段1切换到阶段2）
 *   - 任务状态切换（如从准备阶段切换到执行阶段）
 *   - 需要完全替换阶段而不是添加阶段时
 *
 * 示例：
 *   SetPhase(1);  // _phaseMask = 0x01 (二进制: 00000001)
 *   SetPhase(2);  // _phaseMask = 0x02 (二进制: 00000010)
 *   SetPhase(0);  // _phaseMask = 0x00 (无阶段限制)
 *
 * 注意事项：
 *   - 与 AddPhase 不同，这是替换操作而非添加操作
 *   - 设置后只有匹配该阶段的事件才会被执行
 *   - 事件阶段为 0 时表示所有阶段都可执行，不受此限制
 */
void EventMap::SetPhase(PhaseIndex phase)
{
    if (!phase)
        _phaseMask = 0;
    else if (phase <= sizeof(PhaseMask) * 8)
        _phaseMask = PhaseMask(1u << (phase - 1u));
}

/**
 * @brief 调度一个新事件（固定延迟时间）
 *
 * 在指定时间后调度一个新事件，事件将在当前时间 + time 时触发。
 * 这是最基本的事件调度方法。
 *
 * @param eventId 事件ID，由调用者定义（通常使用枚举），标识具体的事件类型
 * @param time 事件触发前的延迟时间（毫秒）
 * @param group 事件所属组（1-8），0 表示无组，用于批量操作（延迟、取消）
 * @param phase 事件可执行的阶段（1-8），0 表示所有阶段都可执行
 *
 * 主要流程：
 *   1. 参数验证：确保 group 和 phase 不超过最大值（8）
 *   2. 计算触发时间：_time + time
 *   3. 创建 Event 对象（构造函数会自动将索引转换为掩码）
 *   4. 插入到 multimap 中（自动按时间排序）
 *
 * 调用时机：
 *   - AI 初始化时调度初始技能
 *   - 事件处理函数中调度后续事件（链式事件）
 *   - 战斗开始时调度战斗事件
 *   - 周期性任务调度
 *
 * 性能：
 *   时间复杂度 O(log n)，multimap 的插入操作
 *
 * 示例：
 *   events.ScheduleEvent(EVENT_FIREBALL, 5000ms);           // 5秒后触发火球术
 *   events.ScheduleEvent(EVENT_HEAL, 10s, GROUP_HEAL, 2);   // 阶段2中，10秒后触发治疗
 */
void EventMap::ScheduleEvent(EventId eventId, Milliseconds time, GroupIndex group /*= 0*/, PhaseIndex phase /*= 0*/)
{
    // 参数验证：group 和 phase 不能超过 8（因为使用 uint8 掩码，最多 8 位）
    if (group > sizeof(GroupMask) * 8)
        return;

    if (phase > sizeof(PhaseMask) * 8)
        return;

    // 插入事件到 multimap，键为触发时间点，值为事件信息
    // multimap 会自动按时间排序，确保最早的事件在前面
    _eventMap.insert(EventStore::value_type(_time + time, Event(eventId, group, phase)));
}

/**
 * @brief 调度一个新事件（随机延迟时间范围）
 *
 * 在 minTime 到 maxTime 之间的随机时间后调度一个事件。
 * 用于增加事件触发的随机性，使 AI 行为更自然、不可预测。
 *
 * @param eventId 事件ID，标识具体的事件类型
 * @param minTime 最小延迟时间（毫秒）
 * @param maxTime 最大延迟时间（毫秒）
 * @param group 事件所属组（1-8），0 表示无组
 * @param phase 事件可执行的阶段（1-8），0 表示所有阶段
 *
 * 主要流程：
 *   1. 使用 randtime() 在 [minTime, maxTime] 范围内随机选择一个时间
 *   2. 调用固定时间的 ScheduleEvent 重载版本
 *
 * 调用时机：
 *   - 需要随机延迟的事件（如怪物技能施放）
 *   - 避免固定的技能循环模式，使战斗更有变化
 *   - 模拟真实的反应时间差异
 *
 * 性能：
 *   时间复杂度 O(log n) + 随机数生成开销
 *
 * 示例：
 *   events.ScheduleEvent(EVENT_ATTACK, 3s, 7s);  // 3-7秒内随机时间攻击
 *   events.ScheduleEvent(EVENT_SPELL, 5000ms, 10000ms, GROUP_SPELL, PHASE_COMBAT);
 */
void EventMap::ScheduleEvent(EventId eventId, Milliseconds minTime, Milliseconds maxTime, GroupIndex group /*= 0*/, PhaseIndex phase /*= 0*/)
{
    // randtime() 在 [minTime, maxTime] 范围内均匀随机选择一个时间
    ScheduleEvent(eventId, randtime(minTime, maxTime), group, phase);
}

/**
 * @brief 重新调度一个已存在的事件（固定延迟时间）
 *
 * 取消现有的所有同 ID 事件，并以新的时间和属性重新调度。
 * 这是一个"先删除后添加"的组合操作。
 *
 * @param eventId 要重新调度的事件ID
 * @param time 新的延迟时间（毫秒）
 * @param group 事件所属组（1-8），0 表示无组
 * @param phase 事件可执行的阶段（1-8），0 表示所有阶段
 *
 * 主要流程：
 *   1. 调用 CancelEvent(eventId) 取消所有该 ID 的事件
 *   2. 调用 ScheduleEvent() 以新参数重新调度
 *
 * 调用时机：
 *   - 推迟某个特定事件（如技能被打断后延迟）
 *   - 根据条件重新设置事件时间
 *   - 改变事件的属性（组、阶段）
 *
 * 注意事项：
 *   - 会取消所有同 ID 的事件，不仅仅是第一个
 *   - 如果没有同 ID 事件，则直接添加新事件
 *   - 性能较低：O(n) 查找 + O(log n) 插入
 *
 * 性能：
 *   时间复杂度 O(n) + O(log n)，其中 O(n) 是 CancelEvent 的开销
 *
 * 示例：
 *   events.RescheduleEvent(EVENT_SPELL, 8s);  // 将法术事件重新调度到8秒后
 */
void EventMap::RescheduleEvent(EventId eventId, Milliseconds time, GroupIndex group, PhaseIndex phase)
{
    // 先取消所有同 ID 的现有事件
    CancelEvent(eventId);
    // 然后以新参数调度事件
    ScheduleEvent(eventId, time, group, phase);
}

/**
 * @brief 重新调度一个已存在的事件（随机延迟时间范围）
 *
 * 取消现有的所有同 ID 事件，并在随机时间后重新调度。
 * 结合了重新调度和随机时间的功能。
 *
 * @param eventId 要重新调度的事件ID
 * @param minTime 最小延迟时间（毫秒）
 * @param maxTime 最大延迟时间（毫秒）
 * @param group 事件所属组（1-8），0 表示无组
 * @param phase 事件可执行的阶段（1-8），0 表示所有阶段
 *
 * 主要流程：
 *   1. 使用 randtime() 在 [minTime, maxTime] 范围内随机选择时间
 *   2. 调用固定时间的 RescheduleEvent 重载版本
 *
 * 调用时机：
 *   - 需要推迟事件并增加随机性
 *   - 重新调度时避免固定模式
 *
 * 性能：
 *   时间复杂度 O(n) + O(log n) + 随机数生成开销
 *
 * 示例：
 *   events.RescheduleEvent(EVENT_ATTACK, 5s, 10s);  // 5-10秒随机时间重新调度攻击
 */
void EventMap::RescheduleEvent(EventId eventId, Milliseconds minTime, Milliseconds maxTime, GroupIndex group /*= 0*/, PhaseIndex phase /*= 0*/)
{
    RescheduleEvent(eventId, randtime(minTime, maxTime), group, phase);
}

/**
 * @brief 重复执行最近的事件（固定延迟时间）
 *
 * 使用与最近执行的事件相同的 ID、组和阶段，重新调度该事件。
 * 这是实现周期性事件的便捷方法，避免了手动传递重复的事件属性。
 *
 * @param time 延迟时间（毫秒）
 *
 * 主要流程：
 *   1. 使用 _lastEvent 中保存的上一次执行事件的信息
 *   2. 以新的触发时间重新插入到事件映射表
 *
 * 调用时机：
 *   - 周期性技能施放（如每 5 秒施放一次火球术）
 *   - 持续性效果更新（如持续伤害、持续治疗）
 *   - 循环事件（如巡逻、巡逻点检查）
 *
 * 使用示例：
 *   void UpdateAI(uint32 diff) {
 *       events.Update(diff);
 *       while (uint32 eventId = events.ExecuteEvent()) {
 *           switch (eventId) {
 *               case EVENT_FIREBALL:
 *                   DoCastVictim(SPELL_FIREBALL);
 *                   events.Repeat(5s);  // 5秒后再次施放火球术
 *                   break;
 *           }
 *       }
 *   }
 *
 * 注意事项：
 *   - 必须在 ExecuteEvent() 之后立即调用，否则 _lastEvent 可能被覆盖
 *   - 会保留原事件的所有属性（ID、组、阶段）
 *   - 如果之前没有执行过事件，_lastEvent 为默认构造的 Event 对象
 *
 * 性能：
 *   时间复杂度 O(log n)
 */
void EventMap::Repeat(Milliseconds time)
{
    // 使用 _lastEvent 保存的事件信息（ID、组掩码、阶段掩码）
    // 以新的触发时间重新插入到事件映射表
    _eventMap.insert(EventStore::value_type(_time + time, _lastEvent));
}

/**
 * @brief 重复执行最近的事件（随机延迟时间范围）
 *
 * 在随机时间后重复最近执行的事件。
 * 结合了重复事件和随机时间，用于实现随机间隔的周期性行为。
 *
 * @param minTime 最小延迟时间（毫秒）
 * @param maxTime 最大延迟时间（毫秒）
 *
 * 主要流程：
 *   1. 使用 randtime() 在 [minTime, maxTime] 范围内随机选择时间
 *   2. 调用固定时间的 Repeat 重载版本
 *
 * 调用时机：
 *   - 需要随机间隔的周期性技能（避免固定模式）
 *   - 使 AI 行为更自然、不可预测
 *
 * 使用示例：
 *   case EVENT_ATTACK:
 *       DoAttack();
 *       events.Repeat(3s, 7s);  // 3-7秒随机间隔再次攻击
 *       break;
 *
 * 性能：
 *   时间复杂度 O(log n) + 随机数生成开销
 */
void EventMap::Repeat(Milliseconds minTime, Milliseconds maxTime)
{
    Repeat(randtime(minTime, maxTime));
}

/**
 * @brief 执行下一个到期事件
 *
 * 获取并移除下一个到期且符合当前阶段的事件。
 * 这是事件处理的核心方法，通常在 AI 更新循环中每帧调用。
 *
 * @return 到期事件的事件 ID，如果没有到期事件或事件不在当前阶段则返回 0
 *
 * 主要流程：
 *   循环遍历事件映射表（multimap 按时间自动排序，最早事件在前）：
 *     1. 检查事件映射表是否为空
 *     2. 获取第一个事件（最早到期的事件）
 *     3. 检查事件是否已到期（触发时间 <= 当前时间）
 *        - 如果未到期（触发时间 > 当前时间），返回 0（后续事件更晚，无需继续）
 *     4. 检查事件阶段是否匹配当前阶段掩码
 *        - 如果事件有阶段限制（_phaseMask != 0）且当前也有阶段限制（_phaseMask != 0）
 *        - 且事件的阶段与当前阶段不匹配（!(event._phaseMask & _phaseMask)）
 *        - 则删除该事件并继续检查下一个事件
 *     5. 如果事件有效且时间已到且阶段匹配
 *        - 保存事件信息到 _lastEvent（用于 Repeat 方法）
 *        - 从映射表中移除该事件
 *        - 返回事件 ID
 *
 * 阶段匹配逻辑详解：
 *   - 事件 _phaseMask == 0: 表示所有阶段都可执行，总是匹配
 *   - 当前 _phaseMask == 0: 表示无阶段限制，所有事件都可执行
 *   - 两者都非 0: 使用位运算检查是否有交集（event._phaseMask & _phaseMask）
 *
 * 调用时机：
 *   - 在 UpdateAI() 中每帧调用，检查是否有事件需要处理
 *   - 通常在 events.Update(diff) 之后调用
 *
 * 使用示例：
 *   void UpdateAI(uint32 diff) override {
 *       events.Update(diff);
 *
 *       while (uint32 eventId = events.ExecuteEvent()) {
 *           switch (eventId) {
 *               case EVENT_SPELL_1:
 *                   DoCastVictim(SPELL_1);
 *                   events.ScheduleEvent(EVENT_SPELL_2, 5s);
 *                   break;
 *               case EVENT_SPELL_2:
 *                   DoCastVictim(SPELL_2);
 *                   events.Repeat(10s);
 *                   break;
 *           }
 *       }
 *
 *       DoMeleeAttackIfReady();
 *   }
 *
 * 注意事项：
 *   - 调用后事件会从映射表中移除，需要重新调度或重复才能再次触发
 *   - 阶段不匹配的事件会被删除，而不是延迟到匹配阶段
 *   - 返回 0 不一定是没有事件，可能是事件尚未到期
 *   - 可以在一个更新周期内执行多个到期事件（使用 while 循环）
 *
 * 性能：
 *   - 平均时间复杂度：O(1) 当事件到期且阶段匹配时
 *   - 最坏情况：O(n) 当大量事件不在当前阶段时，需要遍历删除
 *   - multimap::begin() 操作是 O(1)
 *   - erase 操作是 O(1) 平均
 */
EventMap::EventId EventMap::ExecuteEvent()
{
    // 循环检查事件，直到找到有效的到期事件或队列为空
    while (!Empty())
    {
        // multimap 按时间排序，begin() 返回最早的事件
        auto itr = _eventMap.begin();

        // 检查事件是否已到期
        // itr->first 是事件的触发时间，_time 是当前时间
        if (itr->first > _time)
            return 0;  // 事件尚未到期，由于事件按时间排序，后续事件更晚，直接返回
        // 检查阶段匹配
        // _phaseMask: 当前 EventMap 的阶段掩码
        // itr->second._phaseMask: 事件的阶段掩码
        // 如果两者都非 0 且没有交集，说明事件不在当前阶段
        else if (_phaseMask && itr->second._phaseMask && !(itr->second._phaseMask & _phaseMask))
            _eventMap.erase(itr);  // 删除不匹配阶段的事件，继续检查下一个
        else
        {
            // 事件有效且时间已到
            auto eventId = itr->second._id;
            _lastEvent = itr->second;  // 保存事件信息，供 Repeat() 使用
            _eventMap.erase(itr);      // 从映射表中移除事件
            return eventId;             // 返回事件 ID 供调用者处理
        }
    }

    return 0;  // 没有待处理事件
}

/**
 * @brief 延迟所有事件
 *
 * 将事件映射表中所有事件的触发时间延后指定的延迟时间。
 * 批量操作，影响所有事件。
 *
 * @param delay 延迟时间（毫秒）
 *
 * 主要流程：
 *   1. 检查事件映射表是否为空，空则直接返回
 *   2. 将所有事件从原映射表移动到临时容器（使用 std::move）
 *   3. 遍历临时容器中的每个事件节点
 *   4. 使用 extract() 提取节点（不释放内存，不调用析构函数）
 *   5. 修改节点的键值（触发时间 += delay）
 *   6. 将修改后的节点重新插入原映射表
 *
 * 技术细节：
 *   - 使用 std::move 和 extract 优化性能，避免事件对象的拷贝构造
 *   - extract() 是 C++17 引入的节点操作，可以直接修改键值
 *   - 重新插入时使用 _eventMap.end() 作为提示迭代器，提高插入效率
 *
 * 调用时机：
 *   - 单位被眩晕（Stun）或冻结时，所有事件延迟
 *   - 战斗暂停（如过场动画）
 *   - 时间减速效果
 *
 * 注意事项：
 *   - 会影响所有事件，包括不同组和阶段的事件
 *   - 不改变事件的相对顺序（所有事件都延迟相同时间）
 *   - 对空映射表调用是安全的
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历并重新插入所有事件
 *   但通过节点操作避免了内存分配和对象构造的开销
 */
void EventMap::DelayEvents(Milliseconds delay)
{
    if (Empty())
        return;

    // 将所有事件移动到临时容器，原映射表变空
    EventStore delayed = std::move(_eventMap);

    // 遍历临时容器中的每个事件节点
    for (auto itr = delayed.begin(); itr != delayed.end();)
    {
        // extract() 提取节点，返回节点句柄
        // 节点句柄包含节点的所有权，可以修改键值
        // itr++ 移动到下一个迭代器，当前节点被提取
        EventStore::node_type node = delayed.extract(itr++);

        // 修改节点的键值（触发时间）
        // node.key() 返回键的可修改引用
        node.key() = node.key() + delay;

        // 将节点重新插入原映射表
        // 使用 end() 作为提示，表示插入位置可能在末尾附近
        // std::move(node) 移动节点所有权，避免拷贝
        _eventMap.insert(_eventMap.end(), std::move(node));
    }
}

/**
 * @brief 延迟指定组的所有事件
 *
 * 将指定组的所有事件触发时间延后指定的延迟时间。
 * 只影响特定组的事件，其他组的事件不受影响。
 *
 * @param delay 延迟时间（毫秒）
 * @param group 事件组ID（1-8）
 *
 * 主要流程：
 *   1. 参数验证：
 *      - group 必须在有效范围内（1-8）
 *      - 事件映射表不能为空
 *   2. 遍历事件映射表，查找匹配指定组的事件
 *   3. 对于匹配的事件：
 *      - 创建新的键值对（触发时间 + delay, 事件对象）
 *      - 插入到临时容器 delayed 中
 *      - 从原映射表中删除
 *   4. 将临时容器中的所有事件重新插入原映射表
 *
 * 组匹配逻辑：
 *   - 事件的 _groupMask 是位图形式，每位代表一个组
 *   - 使用位运算检查事件是否属于指定组：
 *     itr->second._groupMask & (1 << (group - 1))
 *   - 例如：group=3，则检查事件 _groupMask 的第 3 位是否为 1
 *
 * 调用时机：
 *   - 特定类型事件需要延迟（如所有治疗技能进入冷却）
 *   - 组技能被打断，整组技能延迟
 *   - 阶段性延迟（如战斗阶段切换，某些技能延迟）
 *
 * 注意事项：
 *   - 只延迟指定组的事件，其他组的事件不受影响
 *   - 组 ID 必须在 1-8 范围内，否则不做任何操作
 *   - 对空映射表调用是安全的
 *   - 与 DelayEvents(Milliseconds) 不同，这个版本需要拷贝事件对象
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历所有事件
 *   由于需要拷贝事件对象，性能略低于延迟所有事件的版本
 */
void EventMap::DelayEvents(Milliseconds delay, GroupIndex group)
{
    // 参数验证：group 必须在 [1, 8] 范围内，事件映射表不能为空
    if (!group || group > sizeof(GroupMask) * 8 || Empty())
        return;

    EventStore delayed;  // 临时容器，存储延迟后的事件

    // 遍历所有事件，查找匹配指定组的事件
    for (auto itr = _eventMap.begin(); itr != _eventMap.end();)
    {
        // 检查事件是否属于指定组
        // GroupMask(1u << (group - 1u)) 创建组的位掩码
        // 例如：group=3 -> 1<<(3-1)=4 -> 二进制: 00000100
        // 如果事件的 _groupMask 的第 3 位为 1，则匹配
        if (itr->second._groupMask & GroupMask(1u << (group - 1u)))
        {
            // 创建新的键值对，触发时间延迟 delay
            // 需要拷贝事件对象（itr->second）
            delayed.insert(EventStore::value_type(itr->first + delay, itr->second));
            // 从原映射表中删除，并移动到下一个迭代器
            _eventMap.erase(itr++);
        }
        else
            ++itr;  // 不匹配，检查下一个事件
    }

    // 将延迟后的事件重新插入原映射表
    _eventMap.insert(delayed.begin(), delayed.end());
}

/**
 * @brief 设置事件的最小延迟
 *
 * 确保指定事件在至少延迟指定时间后才能触发。
 * 如果事件的当前延迟小于给定延迟，则将其延迟增加到给定值。
 * 如果事件的当前延迟已经大于等于给定延迟，则不做任何修改。
 *
 * @param eventId 事件ID，标识要调整的事件
 * @param delay 最小延迟时间（毫秒），相对于当前时间
 *
 * 主要流程：
 *   1. 检查事件映射表是否为空
 *   2. 遍历事件映射表，查找匹配指定 ID 的事件
 *   3. 对于匹配的事件：
 *      - 检查当前触发时间是否早于 (_time + delay)
 *      - 如果早于，则以 (_time + delay) 为新触发时间重新调度
 *      - 如果不早于，则不做任何修改
 *
 * 调用时机：
 *   - 确保事件不会过早触发（如技能冷却）
 *   - 动态调整事件时间，根据某些条件推迟事件
 *   - 应对突发情况，延长事件等待时间
 *
 * 使用示例：
 *   // 假设某个技能被打断，需要至少 3 秒冷却
 *   events.SetMinimalDelay(EVENT_SPELL, 3s);
 *
 * 注意事项：
 *   - 只会增加延迟，不会减少延迟
 *   - 如果有多个同 ID 事件，会检查所有匹配事件
 *   - 对空映射表调用是安全的
 *   - 对不存在的事件调用无效果
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历所有事件查找匹配 ID
 *   如果找到并修改，还需要 O(log n) 的插入操作
 */
void EventMap::SetMinimalDelay(EventId eventId, Milliseconds delay)
{
    if (Empty())
        return;

    // 遍历所有事件，查找匹配指定 ID 的事件
    for (auto itr = _eventMap.begin(); itr != _eventMap.end();)
    {
        if (eventId == itr->second._id)
        {
            // 检查事件的当前触发时间是否早于最小延迟时间
            // itr->first: 事件的当前触发时间
            // _time + delay: 当前时间 + 最小延迟
            if (itr->first < (_time + delay))
            {
                // 事件触发时间太早，需要延迟到最小延迟时间
                // 创建新的键值对，插入到映射表
                _eventMap.insert(EventStore::value_type(_time + delay, itr->second));
                // 删除原事件，并获取下一个有效迭代器
                // 使用 erase 的返回值，避免迭代器失效
                itr = _eventMap.erase(itr);
                continue;  // 继续检查是否还有其他同 ID 事件
            }
            // 事件触发时间已经满足最小延迟，不需要修改
        }
        ++itr;  // 检查下一个事件
    }
}

/**
 * @brief 取消指定ID的所有事件
 *
 * 从事件映射表中移除所有具有指定ID的事件。
 * 这是批量取消操作的常用方法。
 *
 * @param eventId 要取消的事件ID
 *
 * 主要流程：
 *   1. 检查事件映射表是否为空
 *   2. 遍历事件映射表，查找所有匹配指定 ID 的事件
 *   3. 删除所有匹配的事件
 *
 * 调用时机：
 *   - 取消特定技能的后续事件（如法术被打断）
 *   - 任务取消时清理相关事件
 *   - 状态改变时移除不再需要的事件
 *   - AI 重置时清除特定事件
 *
 * 使用示例：
 *   // 法术被打断，取消后续法术事件
 *   events.CancelEvent(EVENT_SPELL_CHAIN);
 *
 *   // 进入新阶段，取消旧阶段事件
 *   events.CancelEvent(EVENT_PHASE_1_SPELL);
 *
 * 注意事项：
 *   - 会删除所有同 ID 的事件，不仅仅是第一个
 *   - 如果不存在该 ID 的事件，不做任何操作
 *   - 对空映射表调用是安全的
 *   - 删除后事件将不再触发，除非重新调度
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历所有事件查找匹配 ID
 */
void EventMap::CancelEvent(EventId eventId)
{
    if (Empty())
        return;

    // 遍历所有事件，删除匹配的事件
    for (auto itr = _eventMap.begin(); itr != _eventMap.end();)
    {
        if (eventId == itr->second._id)
            // 删除事件，并移动到下一个迭代器
            // erase(itr++) 返回删除位置之后的迭代器
            _eventMap.erase(itr++);
        else
            ++itr;  // 不匹配，检查下一个事件
    }
}

/**
 * @brief 取消指定组的所有事件
 *
 * 从事件映射表中移除所有属于指定组的事件。
 * 这是按组批量取消事件的方法。
 *
 * @param group 要取消的事件组ID（1-8）
 *
 * 主要流程：
 *   1. 参数验证：
 *      - group 必须在有效范围内（1-8）
 *      - 事件映射表不能为空
 *   2. 遍历事件映射表，查找匹配指定组的事件
 *   3. 删除所有匹配组的事件
 *
 * 组匹配逻辑：
 *   - 使用位运算检查事件是否属于指定组
 *   - itr->second._groupMask & (1 << (group - 1))
 *   - 例如：group=2，检查事件的 _groupMask 第 2 位是否为 1
 *
 * 调用时机：
 *   - 取消特定类型的所有事件（如所有治疗技能）
 *   - 阶段切换时清理旧阶段事件
 *   - 特定技能树被禁用时
 *   - 整组技能进入冷却或被打断
 *
 * 使用示例：
 *   // 进入战斗阶段2，取消阶段1的所有技能
 *   events.CancelEventGroup(GROUP_PHASE_1_SPELLS);
 *   events.SetPhase(2);
 *
 *   // 所有召唤技能被打断
 *   events.CancelEventGroup(GROUP_SUMMON);
 *
 * 注意事项：
 *   - 只删除指定组的事件，其他组的事件不受影响
 *   - 组 ID 必须在 1-8 范围内，否则不做任何操作
 *   - 对空映射表调用是安全的
 *   - 删除后事件将不再触发，除非重新调度
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历所有事件查找匹配组
 */
void EventMap::CancelEventGroup(GroupIndex group)
{
    // 参数验证：group 必须在 [1, 8] 范围内，事件映射表不能为空
    if (!group || group > sizeof(GroupMask) * 8 || Empty())
        return;

    // 遍历所有事件，删除匹配组的事件
    for (auto itr = _eventMap.begin(); itr != _eventMap.end();)
    {
        // 检查事件是否属于指定组
        // GroupMask(1u << (group - 1u)) 创建组的位掩码
        // 如果事件的 _groupMask 与组掩码有交集，则匹配
        if (itr->second._groupMask & GroupMask(1u << (group - 1u)))
            // 删除事件，并移动到下一个迭代器
            _eventMap.erase(itr++);
        else
            ++itr;  // 不匹配，检查下一个事件
    }
}

/**
 * @brief 获取距离指定事件的剩余时间
 *
 * 计算当前时间到指定事件触发时间的差值。
 * 用于查询事件的剩余等待时间。
 *
 * @param eventId 事件ID
 * @return 距离事件触发的剩余时间（毫秒）
 *         如果事件未调度返回 Milliseconds::max()
 *         如果事件已过期返回负值或零
 *
 * 主要流程：
 *   1. 使用 C++17 结构化绑定遍历事件映射表
 *   2. 查找第一个匹配指定 ID 的事件
 *   3. 计算触发时间与当前时间的差值
 *   4. 如果未找到，返回 Milliseconds::max()
 *
 * 调用时机：
 *   - 检查特定事件的剩余时间
 *   - 显示冷却时间（UI 显示）
 *   - 条件判断（如事件即将触发时执行特殊逻辑）
 *   - 日志和调试
 *
 * 使用示例：
 *   Milliseconds timeLeft = events.GetTimeUntilEvent(EVENT_ULTIMATE);
 *   if (timeLeft < 5s && timeLeft > 0s) {
 *       // 终极技能即将就绪，播放特效
 *       PlayReadyEffect();
 *   }
 *
 * 返回值说明：
 *   - Milliseconds::max(): 事件未调度或不存在
 *   - 正值: 事件的剩余时间（事件在未来触发）
 *   - 零: 事件刚好到期
 *   - 负值: 事件已过期（应该已触发但未执行）
 *
 * 注意事项：
 *   - 如果有多个同 ID 事件，返回第一个找到的事件的剩余时间
 *   - 不区分事件所属组或阶段
 *   - 返回 Milliseconds::max() 表示事件不存在，可用于判断事件是否已调度
 *   - const 方法，不修改事件映射表
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历查找指定事件
 */
Milliseconds EventMap::GetTimeUntilEvent(EventId eventId) const
{
    // 使用 C++17 结构化绑定遍历事件映射表
    // const auto& [time, event]: time 是触发时间，event 是事件对象
    for (auto const& [time, event] : _eventMap)
    {
        if (eventId == event._id)
        {
            // 计算触发时间与当前时间的差值
            // time - _time 可能是正值（未来）、零（当前）或负值（已过期）
            // 使用 duration_cast 转换为毫秒精度
            return std::chrono::duration_cast<Milliseconds>(time - _time);
        }
    }

    // 事件未找到，返回最大时间值
    // Milliseconds::max() 是一个特殊值，表示事件不存在
    return Milliseconds::max();
}

/**
 * @brief 检查指定事件是否已调度
 *
 * 检查事件映射表中是否存在指定ID的事件。
 * 这是一个便捷方法，用于快速判断事件是否在队列中。
 *
 * @param eventId 事件ID
 * @return 如果事件已调度返回 true，否则返回 false
 *
 * 主要流程：
 *   1. 调用 GetTimeUntilEvent() 获取事件的剩余时间
 *   2. 如果返回值不等于 Milliseconds::max()，说明事件存在
 *
 * 调用时机：
 *   - 避免重复调度同一事件
 *   - 检查事件是否在队列中（如技能是否在冷却中）
 *   - 条件判断（如只在事件未调度时调度）
 *   - 调试和日志
 *
 * 使用示例：
 *   if (!events.HasEventScheduled(EVENT_ULTIMATE)) {
 *       // 终极技能未在队列中，可以调度
 *       events.ScheduleEvent(EVENT_ULTIMATE, 30s);
 *   }
 *
 *   if (events.HasEventScheduled(EVENT_HEAL)) {
 *       // 治疗技能正在冷却
 *       return;
 *   }
 *
 * 注意事项：
 *   - 如果有多个同 ID 事件，只要有一个就返回 true
 *   - 不区分事件所属组或阶段
 *   - 不关心事件是否已到期，只要在映射表中就返回 true
 *   - const 方法，不修改事件映射表
 *   - 基于 GetTimeUntilEvent() 实现，性能相同
 *
 * 性能：
 *   时间复杂度 O(n)，需要遍历查找指定事件
 */
bool EventMap::HasEventScheduled(EventId eventId) const
{
    // 利用 GetTimeUntilEvent() 的返回值判断事件是否存在
    // Milliseconds::max() 表示事件未找到
    return GetTimeUntilEvent(eventId) != Milliseconds::max();
}
