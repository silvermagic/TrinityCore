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
 * @file LFGGroupData.cpp
 * @brief 寻组系统队伍数据管理模块实现
 *
 * 本文件实现了 LfgGroupData 类，该类负责管理寻组系统（Looking For Group，LFG）
 * 中队伍的所有核心数据。寻组系统是魔兽世界中用于匹配玩家组队进入副本的核心功能。
 *
 * 主要职责：
 * - 维护队伍在寻组流程中的状态机（排队、匹配、进入副本、完成等状态）
 * - 管理队伍成员列表和队长信息
 * - 存储队伍当前排队或正在进行的副本信息
 * - 处理投票踢人机制的计数和激活状态
 *
 * 设计要点：
 * - 每个参与寻组的队伍（Group）都会有一个 LfgGroupData 实例
 * - 状态机设计使用 m_State（当前状态）和 m_OldState（历史状态）实现状态恢复
 * - 踢人次数有上限限制，防止滥用踢人机制
 *
 * @see LfgGroupData 头文件定义
 * @see LfgState 状态枚举定义
 */

#include "LFG.h"
#include "LFGGroupData.h"

namespace lfg
{

/**
 * @brief 构造函数 - 初始化寻组队伍数据对象
 *
 * 初始化所有成员变量为默认状态，确保对象处于有效且一致的初始状态。
 * 这是创建新队伍时首先调用的方法。
 *
 * 初始化内容：
 * - m_State = LFG_STATE_NONE: 队伍当前未参与寻组系统
 * - m_OldState = LFG_STATE_NONE: 无历史状态
 * - m_Leader = 空GUID: 队伍暂无队长
 * - m_Dungeon = 0: 未排队任何副本
 * - m_KicksLeft = LFG_GROUP_MAX_KICKS (3): 剩余踢人次数设为最大值
 * - m_VoteKickActive = false: 无进行中的踢人投票
 *
 * @note 踢人次数初始值为最大值（3次），这是暴雪官方的设计，
 *       限制队伍在单次副本中踢人的次数，防止滥用机制
 */
LfgGroupData::LfgGroupData(): m_State(LFG_STATE_NONE), m_OldState(LFG_STATE_NONE),
    m_Leader(), m_Dungeon(0), m_KicksLeft(LFG_GROUP_MAX_KICKS), m_VoteKickActive(false)
{ }

/**
 * @brief 析构函数 - 清理寻组队伍数据
 *
 * 负责清理队伍数据对象占用的资源。
 * 由于该类仅使用标准容器和基本数据类型，无需手动释放内存。
 *
 * 自动清理的内容：
 * - m_Players (GuidSet): 标准库容器会自动析构
 * - 所有基础类型成员变量：随对象销毁自动释放
 *
 * @note 该析构函数为空实现，依赖编译器生成的默认析构行为。
 *       如果未来扩展类功能，需要注意资源管理。
 */
LfgGroupData::~LfgGroupData()
{ }

/**
 * @brief 检查当前队伍是否为寻组系统创建的队伍
 *
 * 通过检查历史状态（m_OldState）是否不为 LFG_STATE_NONE 来判断。
 * 这是一种快速识别队伍来源的方式。
 *
 * 判断逻辑：
 * - m_OldState 在队伍进入副本相关状态时会被更新（见 SetState 方法）
 * - 如果 m_OldState != LFG_STATE_NONE，说明队伍曾进入过寻组副本流程
 * - 普通队伍（非LFG创建）的 m_OldState 始终为 LFG_STATE_NONE
 *
 * 使用场景：
 * - 判断队伍是否可以享受随机副本奖励
 * - 确定某些特殊规则是否适用（如踢人限制）
 * - 统计和日志记录
 *
 * @return bool
 *         - true: 队伍通过寻组系统创建或进入过寻组副本
 *         - false: 普通队伍，未通过寻组系统
 *
 * @note 该方法为非 const 方法，但从逻辑上不修改对象状态，
 *       这可能是历史遗留问题，建议未来改为 const 方法
 */
bool LfgGroupData::IsLfgGroup()
{
    return m_OldState != LFG_STATE_NONE;
}

/**
 * @brief 设置队伍的寻组状态 - 核心状态机管理方法
 *
 * 这是寻组系统状态机的核心方法，控制队伍在不同状态间的转换。
 * 根据不同的状态执行不同的附加操作，确保数据一致性。
 *
 * 状态处理逻辑（使用 switch-case 级联处理）：
 *
 * 1. LFG_STATE_NONE 状态（队伍离开寻组系统）：
 *    - 清空副本信息：m_Dungeon = 0
 *    - 重置踢人次数：m_KicksLeft = LFG_GROUP_MAX_KICKS
 *    - 继续执行后续的 m_OldState 和 m_State 更新
 *    - [[fallthrough]] 标记：显式表明这是有意为之的级联执行
 *
 * 2. LFG_STATE_FINISHED_DUNGEON 或 LFG_STATE_DUNGEON 状态：
 *    - 更新历史状态：m_OldState = state
 *    - 这两个状态标识队伍正在或已完成副本，用于 IsLfgGroup() 判断
 *    - [[fallthrough]] 标记：继续执行 m_State 更新
 *
 * 3. 其他状态（如 LFG_STATE_QUEUED, LFG_STATE_PROPOSAL 等）：
 *    - 仅更新当前状态：m_State = state
 *    - 不修改历史状态和副本信息
 *
 * 典型状态转换流程：
 * @code
 * NONE -> QUEUED -> PROPOSAL -> DUNGEON -> FINISHED_DUNGEON -> NONE
 * @endcode
 *
 * 状态机设计要点：
 * - m_State：表示队伍当前在寻组流程中的状态
 * - m_OldState：保存进入副本相关的状态，用于标识队伍来源和状态恢复
 * - 使用 [[fallthrough]] 属性实现状态处理的级联逻辑，确保代码清晰
 *
 * @param state 要设置的新寻组状态，类型为 LfgState 枚举
 *
 * @note 该方法在不同状态下的副作用不同，调用前需明确目的状态
 * @note 重置踢人次数的设计是合理的：队伍离开寻组系统后，下次进入应重置计数
 *
 * @see LfgState 状态枚举定义
 * @see RestoreState() 用于恢复到历史状态
 */
void LfgGroupData::SetState(LfgState state)
{
    switch (state)
    {
        case LFG_STATE_NONE:
            // 离开寻组系统时，清空副本信息并重置踢人次数
            m_Dungeon = 0;
            m_KicksLeft = LFG_GROUP_MAX_KICKS;
            [[fallthrough]];  // 继续执行历史状态更新
        case LFG_STATE_FINISHED_DUNGEON:
        case LFG_STATE_DUNGEON:
            // 进入副本或完成副本时，记录历史状态（用于标识队伍来源）
            m_OldState = state;
            [[fallthrough]];  // 继续执行当前状态更新
        default:
            // 所有状态最终都会更新当前状态
            m_State = state;
    }
}

/**
 * @brief 恢复队伍到之前的历史状态 - 状态回滚机制
 *
 * 将当前状态（m_State）恢复为历史状态（m_OldState）。
 * 这是状态机中的回滚机制，用于处理各种取消或失败场景。
 *
 * 典型应用场景：
 * 1. 匹配提议被拒绝：
 *    - 队伍状态从 PROPOSAL 回滚到 QUEUED 继续排队
 *
 * 2. 玩家在匹配过程中离开：
 *    - 需要回滚到之前的状态重新组织队伍
 *
 * 3. 副本进入失败：
 *    - 从 DUNGEON 状态回滚，允许重新尝试
 *
 * 4. 系统错误或超时：
 *    - 各种异常情况下的状态恢复
 *
 * 实现细节：
 * - 仅修改 m_State，不修改 m_OldState
 * - 这样可以支持多次回滚（如果需要）
 * - 不修改副本信息和踢人次数等数据
 *
 * 示例：
 * @code
 * // 队伍正在副本中 (m_State = LFG_STATE_DUNGEON, m_OldState = LFG_STATE_DUNGEON)
 * // 某些操作需要临时改变状态，完成后恢复
 * SetState(LFG_STATE_FINISHED_DUNGEON);  // 临时状态
 * // ... 执行某些操作 ...
 * RestoreState();  // 恢复到 LFG_STATE_DUNGEON
 * @endcode
 *
 * @note 该方法不验证 m_OldState 的有效性，调用者需确保逻辑正确
 * @note 如果 m_OldState 为 LFG_STATE_NONE，恢复后队伍将变为非 LFG 队伍状态
 */
void LfgGroupData::RestoreState()
{
    m_State = m_OldState;
}

/**
 * @brief 添加玩家到寻组队伍 - 队伍成员管理
 *
 * 将指定玩家的 GUID 添加到队伍成员集合（m_Players）中。
 * 使用 std::set 容器存储，自动保证 GUID 的唯一性。
 *
 * 容器选择原因：
 * - std::set 提供自动去重，避免同一玩家被重复添加
 * - 查找效率为 O(log n)，适合频繁的成员检查
 * - 插入效率为 O(log n)，性能可接受
 *
 * 调用时机：
 * - 玩家通过寻组系统加入队伍时
 * - 队伍成员发生变化时同步更新
 * - 系统初始化队伍成员列表时
 *
 * 数据一致性注意：
 * - 该方法仅更新寻组数据结构，不修改游戏内的实际组队关系
 * - 实际的组队操作由 Group 类处理，这里仅是数据镜像
 * - 需要与 Group 类的成员列表保持同步
 *
 * 性能考虑：
 * - 使用 std::set::insert()，重复插入会被忽略
 * - 不返回插入结果，调用者不需要知道是否已存在
 * - 对于大量成员操作，考虑批量处理
 *
 * @param guid 要添加的玩家 GUID，必须为有效的玩家 GUID
 *
 * @note 该方法不验证 GUID 的有效性，调用者需确保 GUID 合法
 * @note 该方法不检查队伍人数限制，由上层逻辑控制
 *
 * @see RemovePlayer() 移除玩家
 * @see RemoveAllPlayers() 清空所有玩家
 */
void LfgGroupData::AddPlayer(ObjectGuid guid)
{
    m_Players.insert(guid);
}

/**
 * @brief 从寻组队伍中移除玩家 - 队伍成员管理
 *
 * 从队伍成员集合（m_Players）中移除指定玩家的 GUID，
 * 并返回移除后队伍中剩余玩家的数量。
 *
 * 实现细节：
 * 1. 使用 find() 定位玩家 GUID，避免不必要的查找
 * 2. 如果找到，使用迭代器删除，效率为 O(1)（已知位置）
 * 3. 如果未找到，集合保持不变
 * 4. 返回当前集合大小，转换为 uint8 类型
 *
 * 返回值的用途：
 * - 返回剩余人数，方便调用者判断队伍状态
 * - 如果返回 0，说明队伍已解散
 * - 如果返回 1，可能需要重新分配队长
 * - 可用于触发队伍人数变化的后续逻辑
 *
 * 调用时机：
 * - 玩家主动离开队伍时
 * - 玩家被踢出队伍时
 * - 玩家掉线或登出时
 * - 队伍解散前的成员清理
 *
 * 典型使用示例：
 * @code
 * uint8 remainingPlayers = groupData.RemovePlayer(playerGuid);
 * if (remainingPlayers == 0)
 * {
 *     // 队伍已空，执行解散逻辑
 * }
 * else if (remainingPlayers == 1 && removedPlayerWasLeader)
 * {
 *     // 队长离开，需要指定新队长
 * }
 * @endcode
 *
 * 性能考虑：
 * - 使用迭代器删除比按值删除更高效（避免二次查找）
 * - std::set::erase(iterator) 的时间复杂度为 O(1)
 * - 总体时间复杂度为 O(log n)（查找） + O(1)（删除）
 *
 * @param guid 要移除的玩家 GUID
 * @return uint8 移除后队伍中的玩家数量，范围 0-255
 *         - 0: 队伍已清空
 *         - 1-4: 正常的队伍人数（5人小队）
 *         - 最大支持到 255（虽然实际不会超过 5）
 *
 * @note 该方法不处理队长转移逻辑，由上层调用者处理
 * @note 该方法不验证 GUID 是否存在，不存在时静默返回当前人数
 * @note 返回值转换为 uint8，确保与队伍人数上限一致
 *
 * @see AddPlayer() 添加玩家
 * @see RemoveAllPlayers() 清空所有玩家
 */
uint8 LfgGroupData::RemovePlayer(ObjectGuid guid)
{
    // 使用迭代器查找和删除，提高效率
    GuidSet::iterator it = m_Players.find(guid);
    if (it != m_Players.end())
        m_Players.erase(it);
    return uint8(m_Players.size());
}

/**
 * @brief 清空队伍中的所有玩家 - 队伍成员重置
 *
 * 移除队伍成员集合（m_Players）中的所有玩家 GUID。
 * 这是批量清空操作，效率高于逐个移除。
 *
 * 调用时机：
 * - 队伍正式解散时，清空所有成员数据
 * - 寻组系统重置队伍数据时
 * - 副本完成后清理队伍信息
 * - 发生严重错误需要重置状态时
 *
 * 实现细节：
 * - 使用 std::set::clear() 方法，时间复杂度为 O(n)
 * - 清空后集合大小为 0，但容量可能不释放（取决于实现）
 * - 清空后仍然可以继续添加新成员
 *
 * 与逐个移除的区别：
 * - RemovePlayer() 返回剩余人数，适合需要计数的情况
 * - RemoveAllPlayers() 直接清空，适合确定要解散的情况
 * - 性能上，clear() 比循环调用 RemovePlayer() 更高效
 *
 * 数据一致性注意：
 * - 该方法仅清空寻组系统的玩家列表缓存
 * - 不影响游戏内实际的组队关系
 * - 需要与 Group 类的状态同步
 * - 清空后相关的队长信息、副本状态可能需要单独处理
 *
 * 典型使用流程：
 * @code
 * // 队伍解散
 * groupData.SetState(LFG_STATE_NONE);      // 重置状态
 * groupData.RemoveAllPlayers();             // 清空成员
 * groupData.SetLeader(ObjectGuid());        // 清空队长
 * @endcode
 *
 * @note 该方法不自动重置其他数据（如队长、副本、状态），
 *       调用者需要根据场景决定是否重置其他字段
 * @note 清空操作不可逆，调用前需确保逻辑正确
 *
 * @see RemovePlayer() 移除单个玩家
 */
void LfgGroupData::RemoveAllPlayers()
{
    m_Players.clear();
}

/**
 * @brief 设置队伍队长 - 队长权限管理
 *
 * 更新队伍的队长 GUID。队长在寻组系统中拥有特殊权限和职责。
 *
 * 队长的特殊权限：
 * 1. 发起踢人投票：只有队长可以发起踢出队员的投票
 * 2. 决定副本选择：在普通副本模式中，队长选择进入哪个副本
 * 3. 队伍管理：邀请玩家、调整队伍配置等
 * 4. 代表队伍：在寻组系统中代表队伍做出决策
 *
 * 队长变更的典型场景：
 * - 队伍创建时，创建者自动成为队长
 * - 队长离开队伍，需要指定新队长（通常按加入顺序）
 * - 队长主动转让队长权限
 * - 系统自动重新分配（如队长掉线）
 *
 * 在寻组系统中的作用：
 * - 随机副本模式下，队长权限相对弱化，系统自动匹配
 * - 普通副本模式下，队长选择目标副本
 * - 队长信息用于通知、权限验证和日志记录
 *
 * 数据同步注意：
 * - 该方法仅更新寻组数据结构中的队长信息
 * - 需要与 Group 类的队长信息保持同步
 * - 不执行实际的权限转移逻辑，由上层处理
 *
 * 性能考虑：
 * - 简单的赋值操作，O(1) 时间复杂度
 * - 不验证 GUID 的有效性，调用者负责确保正确性
 * - 不检查新队长是否在队伍中，由上层逻辑保证
 *
 * @param guid 新队长的玩家 GUID，如果传空 GUID 则表示清空队长
 *
 * @note 该方法不处理队长变更的后续逻辑（如通知、权限转移），
 *       这些由上层调用者负责
 * @note 传入空 GUID 的场景很少见，通常用于清空数据或错误恢复
 *
 * @see GetLeader() 获取当前队长
 */
void LfgGroupData::SetLeader(ObjectGuid guid)
{
    m_Leader = guid;
}

/**
 * @brief 设置队伍要进入的副本 - 副本信息管理
 *
 * 设置队伍排队或正在进行的副本 ID。
 * 该 ID 存储了副本的完整信息，包括副本 ID 和类型标识。
 *
 * 副本 ID 的结构（32位）：
 * - 高 8 位 (0xFF000000)：存储类型标识和其他元数据
 * - 低 24 位 (0x00FFFFFF)：存储实际的副本 ID（Dungeon Entry ID）
 *
 * 这种设计的优势：
 * 1. 在单个 uint32 中存储完整信息，节省空间
 * 2. 通过位运算快速提取副本 ID
 * 3. 可以附加副本类型信息（如普通、英雄、随机等）
 *
 * 调用时机：
 * - 玩家选择要排队的副本时
 * - 寻组系统成功匹配后设置目标副本
 * - 进入副本时确认副本 ID
 * - 队伍离开寻组系统时设置为 0（在 SetState 中处理）
 *
 * 副本 ID 的来源：
 * - 从 DBC 文件（DungeonEntries.dbc）加载
 * - 通过配置表映射到具体的副本实例
 * - 包含副本的难度、类型等信息
 *
 * 与 GetDungeon 的配合：
 * @code
 * // 设置完整副本 ID
 * SetDungeon(dungeonEntry);
 *
 * // 获取纯副本 ID（低24位）
 * uint32 dungeonId = GetDungeon(true);
 *
 * // 获取完整值（包含元数据）
 * uint32 fullValue = GetDungeon(false);
 * @endcode
 *
 * 数据一致性：
 * - 当状态设置为 LFG_STATE_NONE 时，副本 ID 会自动清零
 * - 需要在合适的时机调用，避免状态不一致
 *
 * @param dungeon 副本 ID，32位无符号整数
 *                - 0 表示清空副本信息
 *                - 非零值包含副本 ID 和类型信息
 *
 * @note 该方法不验证副本 ID 的有效性，调用者需确保 ID 合法
 * @note 副本 ID 的具体格式和含义由数据库和配置定义
 *
 * @see GetDungeon() 获取副本 ID
 * @see SetState() 状态改变时可能清空副本信息
 */
void LfgGroupData::SetDungeon(uint32 dungeon)
{
    m_Dungeon = dungeon;
}

/**
 * @brief 减少剩余踢人次数 - 踢人次数管理
 *
 * 当队伍发起踢人投票后调用此方法，递减剩余可踢人次数。
 * 这是防止踢人机制被滥用的重要限制措施。
 *
 * 踢人机制的设计理念：
 * 1. 防止滥用：限制队伍在单次副本中踢人的次数
 * 2. 保护玩家：避免玩家被恶意频繁踢出
 * 3. 促进合作：强制队伍成员尝试合作解决问题
 * 4. 暴雪官方设计：最大踢人次数为 3 次（LFG_GROUP_MAX_KICKS）
 *
 * 计数管理逻辑：
 * - 初始值：LFG_GROUP_MAX_KICKS (3)，见构造函数
 * - 递减条件：仅在 m_KicksLeft > 0 时递减
 * - 递减时机：踢人投票发起时（非成功踢出时）
 * - 重置时机：队伍离开寻组系统时（SetState(LFG_STATE_NONE)）
 *
 * 典型使用流程：
 * @code
 * // 检查是否可以踢人
 * if (GetKicksLeft() > 0 && !IsVoteKickActive())
 * {
 *     SetVoteKick(true);        // 标记踢人投票激活
 *     DecreaseKicksLeft();      // 递减剩余次数
 *     // ... 发起踢人投票 ...
 * }
 * else
 * {
 *     // 无法踢人，提示原因
 * }
 * @endcode
 *
 * 边界情况处理：
 * - 如果 m_KicksLeft 为 0，方法不做任何操作（保护性编程）
 * - 不会递减到负数，确保数据的合法性
 * - 重置后重新获得完整踢人次数
 *
 * 踢人次数耗尽的后果：
 * - 队伍无法再发起新的踢人投票
 * - 需要等待队伍解散或副本完成后重置
 * - 玩家可能需要通过其他方式处理问题玩家
 *
 * 性能考虑：
 * - O(1) 时间复杂度，简单的条件判断和递减
 * - 使用 if 保护避免下溢（unsigned 类型的潜在问题）
 *
 * @note 踢人次数是在投票发起时递减，而非踢人成功后
 * @note 该方法不检查是否有进行中的踢人投票，由上层逻辑控制
 * @note 踢人次数限制是暴雪官方设计，反映了游戏平衡性考虑
 *
 * @see GetKicksLeft() 获取剩余踢人次数
 * @see SetVoteKick() 设置踢人投票状态
 * @see LFG_GROUP_MAX_KICKS 最大踢人次数常量
 */
void LfgGroupData::DecreaseKicksLeft()
{
    // 仅在次数大于 0 时递减，防止下溢和滥用
    if (m_KicksLeft)
      --m_KicksLeft;
}

/**
 * @brief 获取队伍当前的寻组状态 - 状态查询接口
 *
 * 返回队伍在寻组系统中的当前状态。这是只读访问方法，不会修改对象状态。
 *
 * 可能的返回值（LfgState 枚举）：
 *
 * 1. LFG_STATE_NONE：
 *    - 队伍未参与寻组系统
 *    - 初始状态或已离开寻组系统
 *
 * 2. LFG_STATE_QUEUED：
 *    - 队伍正在排队等待匹配
 *    - 已提交副本申请，等待系统分配
 *
 * 3. LFG_STATE_PROPOSAL：
 *    - 找到匹配，正在等待玩家确认
 *    - 弹出确认对话框，等待所有玩家同意
 *
 * 4. LFG_STATE_DUNGEON：
 *    - 队伍已进入副本
 *    - 正在进行副本挑战
 *
 * 5. LFG_STATE_FINISHED_DUNGEON：
 *    - 已完成副本
 *    - 等待退出或重新排队
 *
 * 6. 其他中间状态：
 *    - LFG_STATE_ROLECHECK：角色检查中
 *    - LFG_STATE_BOOT：踢人投票中
 *    等等
 *
 * 使用场景：
 * - UI 显示队伍当前状态
 * - 决定哪些操作可以执行
 * - 状态机转换的条件判断
 * - 日志记录和调试
 *
 * 状态判断示例：
 * @code
 * LfgState state = groupData.GetState();
 * if (state == LFG_STATE_QUEUED)
 * {
 *     // 队伍正在排队，可以取消排队
 * }
 * else if (state == LFG_STATE_DUNGEON)
 * {
 *     // 正在副本中，可以完成副本
 * }
 * @endcode
 *
 * 性能考虑：
 * - O(1) 时间复杂度，直接返回成员变量
 * - const 方法，保证线程安全性（读操作）
 *
 * @return LfgState 当前寻组状态，类型为 LfgState 枚举
 *
 * @note 该方法不验证状态的合法性，返回的是最后一次 SetState 设置的值
 * @note 状态的具体含义和可用值参见 LfgState 枚举定义
 *
 * @see SetState() 设置状态
 * @see GetOldState() 获取历史状态
 * @see RestoreState() 恢复历史状态
 */
LfgState LfgGroupData::GetState() const
{
    return m_State;
}

/**
 * @brief 获取队伍的历史状态 - 历史状态查询接口
 *
 * 返回队伍的历史状态（m_OldState）。历史状态在寻组系统中扮演重要角色，
 * 用于标识队伍来源和支持状态恢复机制。
 *
 * 历史状态的作用：
 * 1. 标识队伍来源：
 *    - m_OldState != LFG_STATE_NONE 表示队伍通过寻组系统创建
 *    - IsLfgGroup() 方法直接依赖此判断
 *
 * 2. 状态恢复：
 *    - 当需要回滚状态时，从 m_OldState 恢复
 *    - 支持各种取消、失败场景的状态回滚
 *
 * 3. 权限和奖励判断：
 *    - 判断队伍是否有资格获得随机副本奖励
 *    - 确定某些特殊规则是否适用
 *
 * 历史状态的更新时机（见 SetState 方法）：
 * - 当状态转换为 LFG_STATE_DUNGEON 时更新
 * - 当状态转换为 LFG_STATE_FINISHED_DUNGEON 时更新
 * - 其他状态转换不更新历史状态
 *
 * 典型值和含义：
 *
 * 1. LFG_STATE_NONE：
 *    - 初始值
 *    - 队伍从未进入过寻组副本
 *    - IsLfgGroup() 返回 false
 *
 * 2. LFG_STATE_DUNGEON：
 *    - 队伍正在副本中
 *    - IsLfgGroup() 返回 true
 *
 * 3. LFG_STATE_FINISHED_DUNGEON：
 *    - 队伍已完成副本
 *    - IsLfgGroup() 返回 true
 *    - 可以获得副本完成奖励
 *
 * 状态恢复示例：
 * @code
 * // 当前状态可能被临时修改，需要恢复
 * LfgState oldState = groupData.GetOldState();
 * if (oldState != LFG_STATE_NONE)
 * {
 *     groupData.RestoreState();  // 恢复到历史状态
 * }
 * @endcode
 *
 * 性能考虑：
 * - O(1) 时间复杂度，直接返回成员变量
 * - const 方法，保证线程安全性（读操作）
 *
 * @return LfgState 历史状态值，可能的值包括：
 *         - LFG_STATE_NONE：从未进入寻组副本
 *         - LFG_STATE_DUNGEON：正在副本中
 *         - LFG_STATE_FINISHED_DUNGEON：已完成副本
 *
 * @note 历史状态的更新由 SetState 方法自动管理，不应手动修改
 * @note 该方法主要用于内部状态管理和查询，外部使用较少
 *
 * @see GetState() 获取当前状态
 * @see SetState() 设置状态（会更新历史状态）
 * @see RestoreState() 恢复到历史状态
 * @see IsLfgGroup() 判断是否为寻组队伍
 */
LfgState LfgGroupData::GetOldState() const
{
    return m_OldState;
}

/**
 * @brief 获取队伍中所有玩家的 GUID 集合 - 成员列表查询接口
 *
 * 返回包含队伍中所有成员 GUID 的集合的常量引用。
 * 提供对内部成员列表的只读访问。
 *
 * 返回的数据结构：
 * - 类型：GuidSet（std::set<ObjectGuid>）
 * - 特性：自动排序、自动去重、有序存储
 * - 访问方式：常量引用，避免拷贝开销
 *
 * 主要用途：
 * 1. 遍历队伍成员：
 *    @code
 *    GuidSet const& players = groupData.GetPlayers();
 *    for (GuidSet::const_iterator it = players.begin(); it != players.end(); ++it)
 *    {
 *        Player* player = ObjectAccessor::FindPlayer(*it);
 *        if (player)
 *        {
 *            // 处理每个玩家...
 *        }
 *    }
 *    @endcode
 *
 * 2. 检查玩家是否在队伍中：
 *    @code
 *    GuidSet const& players = groupData.GetPlayers();
 *    if (players.find(playerGuid) != players.end())
 *    {
 *        // 玩家在队伍中
 *    }
 *    @endcode
 *
 * 3. 获取队伍人数：
 *    @code
 *    size_t count = groupData.GetPlayers().size();
 *    @endcode
 *
 * 4. 批量操作：
 *    - 向所有成员发送消息
 *    - 批量更新成员状态
 *    - 统计和分析队伍数据
 *
 * 性能考虑：
 * - 返回引用，O(1) 时间复杂度，无拷贝开销
 * - const 引用，保证数据安全，防止外部修改
 * - std::set 的遍历效率为 O(n)，查找效率为 O(log n)
 *
 * 数据一致性：
 * - 返回的集合是内部数据的直接引用，实时反映队伍成员状态
 * - 不进行任何同步或锁操作，调用者需注意线程安全
 * - 在多线程环境下，集合可能在遍历过程中被修改
 *
 * 使用注意：
 * - 不要存储返回的引用供长期使用（可能失效）
 * - 遍历过程中不要修改队伍成员（增删玩家）
 * - 如果需要稳定的快照，应拷贝到本地容器
 *
 * 快照示例：
 * @code
 * // 创建成员列表快照，避免遍历时被修改
 * GuidSet playersSnapshot = groupData.GetPlayers();
 * for (ObjectGuid guid : playersSnapshot)
 * {
 *     // 安全遍历，即使原集合被修改也不影响
 * }
 * @endcode
 *
 * @return const GuidSet& 队伍成员 GUID 集合的常量引用
 *         - 有序集合（按 GUID 值排序）
 *         - 不包含重复元素
 *         - 可能为空集合（队伍已清空）
 *
 * @note 返回的是内部数据的直接引用，不要长期持有
 * @note 在多线程环境下使用时需要注意同步问题
 * @note 该方法主要用于批量操作和成员检查，简单获取人数可用 GetPlayerCount()
 *
 * @see GetPlayerCount() 获取玩家数量（更高效）
 * @see AddPlayer() 添加玩家
 * @see RemovePlayer() 移除玩家
 */
GuidSet const& LfgGroupData::GetPlayers() const
{
    return m_Players;
}

/**
 * @brief 获取队伍中的玩家数量 - 成员计数接口
 *
 * 返回队伍当前的玩家数量。这是一个高效的计数方法，避免获取完整集合。
 *
 * 实现细节：
 * - 直接返回 m_Players.size()，转换为 uint8 类型
 * - std::set::size() 为 O(1) 时间复杂度（C++11 标准）
 * - 转换为 uint8 与队伍人数上限一致
 *
 * 队伍人数限制（魔兽世界 3.3.5 版本）：
 * - 小队：最多 5 人
 * - 团队：最多 40 人（但寻组系统主要针对 5 人小队）
 * - uint8 范围（0-255）足够覆盖所有情况
 *
 * 使用场景：
 * 1. 快速判断队伍是否已满：
 *    @code
 *    if (groupData.GetPlayerCount() >= 5)
 *    {
 *        // 队伍已满，无法添加新成员
 *    }
 *    @endcode
 *
 * 2. 判断队伍是否为空：
 *    @code
 *    if (groupData.GetPlayerCount() == 0)
 *    {
 *        // 队伍已解散
 *    }
 *    @endcode
 *
 * 3. UI 显示和日志记录：
 *    @code
 *    uint8 count = groupData.GetPlayerCount();
 *    SendAreaTriggerMessage("队伍人数: %u/5", count);
 *    @endcode
 *
 * 与 GetPlayers().size() 的对比：
 * - 本方法更简洁、语义更清晰
 * - 性能相同，都是 O(1)
 * - 本方法直接返回 uint8，无需额外的类型转换
 * - 如果只需要人数，优先使用本方法
 *
 * 典型人数判断：
 * - 0 人：队伍已解散或未初始化
 * - 1 人：单人排队（寻组系统支持）
 * - 2-4 人：部分成员队伍
 * - 5 人：满员队伍
 *
 * 性能考虑：
 * - O(1) 时间复杂度，std::set 维护了大小计数器
 * - const 方法，保证线程安全性（读操作）
 * - 比 GetPlayers().size() 略微高效（避免引用传递）
 *
 * @return uint8 队伍中当前的玩家数量，范围 0-255
 *         - 0: 队伍为空（已解散或未初始化）
 *         - 1: 单人队伍
 *         - 2-5: 正常的小队人数
 *         - 通常不超过 5（寻组系统针对小队）
 *
 * @note 该方法返回的类型是 uint8，虽然 std::set::size() 返回 size_t，
 *       但队伍人数不会超过 uint8 范围
 * @note 该方法是 GetPlayers().size() 的便捷封装，性能相同
 *
 * @see GetPlayers() 获取完整成员列表
 * @see AddPlayer() 添加玩家
 * @see RemovePlayer() 移除玩家并返回剩余人数
 */
uint8 LfgGroupData::GetPlayerCount() const
{
    return m_Players.size();
}

/**
 * @brief 获取队伍队长的 GUID - 队长查询接口
 *
 * 返回队伍当前队长的玩家 GUID。队长在寻组系统中拥有特殊权限。
 *
 * 队长的职责和权限：
 * 1. 发起踢人投票：队长可以发起踢出队员的投票
 * 2. 选择副本：在普通模式下选择进入哪个副本
 * 3. 邀请玩家：邀请其他玩家加入队伍
 * 4. 代表队伍：在系统决策中代表队伍
 * 5. 设置副本难度：调整副本的难度级别
 *
 * 返回值含义：
 * - 有效的 GUID：返回队长玩家的 GUID
 * - 空 GUID：队伍没有队长（未初始化或已清空）
 *
 * 使用场景：
 * 1. 权限验证：
 *    @code
 *    ObjectGuid leaderGuid = groupData.GetLeader();
 *    if (playerGuid == leaderGuid)
 *    {
 *        // 当前玩家是队长，拥有特殊权限
 *    }
 *    @endcode
 *
 * 2. 队长转移判断：
 *    @code
 *    ObjectGuid currentLeader = groupData.GetLeader();
 *    if (!currentLeader || !IsPlayerInGroup(currentLeader))
 *    {
 *        // 队长无效或不在队伍中，需要重新指定
 *    }
 *    @endcode
 *
 * 3. 通知队长：
 *    @code
 *    Player* leader = ObjectAccessor::FindPlayer(groupData.GetLeader());
 *    if (leader)
 *    {
 *        SendNotification(leader, "您的队伍已匹配成功！");
 *    }
 *    @endcode
 *
 * 4. 日志记录：
 *    @code
 *    LOG_DEBUG("lfg", "队伍队长: {}", groupData.GetLeader().ToString());
 *    @endcode
 *
 * 与 Group 类的关系：
 * - 该方法返回的是寻组系统缓存的队长信息
 * - 应与 Group 类的队长信息保持同步
 * - 实际的队长权限由 Group 类管理
 *
 * 性能考虑：
 * - O(1) 时间复杂度，直接返回成员变量
 * - const 方法，保证线程安全性（读操作）
 * - 返回值拷贝，ObjectGuid 很小（8-16字节），开销可忽略
 *
 * 队长变更时机：
 * - 队伍创建时：创建者成为队长
 * - 队长离开：自动转给其他成员
 * - 队长主动转让：转给指定玩家
 * - 系统重置：清空队长信息
 *
 * @return ObjectGuid 队长的玩家 GUID
 *         - 有效 GUID：队长存在且有效
 *         - 空 GUID：队伍没有队长（未初始化或已清空）
 *
 * @note 返回的 GUID 可能指向不在线的玩家，使用前需要验证
 * @note 该方法不验证队长是否仍在队伍中，调用者需自行检查
 * @note 在随机副本模式下，队长权限相对弱化
 *
 * @see SetLeader() 设置队长
 * @see IsLfgGroup() 判断是否为寻组队伍
 */
ObjectGuid LfgGroupData::GetLeader() const
{
    return m_Leader;
}

/**
 * @brief 获取副本 ID - 副本信息查询接口
 *
 * 根据参数决定返回纯副本 ID 还是包含元数据的完整值。
 * 副本 ID 在内部使用位运算存储了额外信息。
 *
 * 副本 ID 的存储格式（32位无符号整数）：
 * - 高 8 位 (0xFF000000)：类型标识和元数据
 * - 低 24 位 (0x00FFFFFF)：实际的副本 ID（Dungeon Entry ID）
 *
 * 位运算示例：
 * @code
 * // 假设 m_Dungeon = 0xAB123456
 * // 高8位类型标识: 0xAB000000
 * // 低24位副本ID: 0x00345678（实际值为 0x123456 & 0x00FFFFFF = 0x003456）
 *
 * uint32 dungeonId = GetDungeon(true);   // 返回 0x003456
 * uint32 fullValue = GetDungeon(false);  // 返回 0xAB123456
 * @endcode
 *
 * 参数说明：
 * - asId = true（默认）：返回低 24 位的纯副本 ID
 *   - 用于数据库查询、副本信息获取
 *   - 屏蔽类型标识，只获取副本本身
 *   - 大多数场景使用此选项
 *
 * - asId = false：返回完整的 32 位值
 *   - 包含类型标识和副本 ID
 *   - 用于需要完整信息的特殊场景
 *   - 如调试、日志记录、原始数据访问
 *
 * 使用场景：
 * 1. 查询副本信息（asId = true）：
 *    @code
 *    uint32 dungeonId = groupData.GetDungeon(true);
 *    DungeonEntry const* entry = sDungeonStore.LookupEntry(dungeonId);
 *    @endcode
 *
 * 2. 判断副本类型（需要完整值）：
 *    @code
 *    uint32 fullDungeon = groupData.GetDungeon(false);
 *    uint8 type = (fullDungeon >> 24) & 0xFF;  // 提取类型标识
 *    @endcode
 *
 * 3. 判断是否已排队副本：
 *    @code
 *    if (groupData.GetDungeon(true) != 0)
 *    {
 *        // 队伍已排队或正在副本中
 *    }
 *    @endcode
 *
 * 4. 日志记录：
 *    @code
 *    LOG_INFO("lfg", "队伍 {} 正在副本 {}", groupGuid.ToString().c_str(),
 *             groupData.GetDungeon(true));
 *    @endcode
 *
 * 返回值含义：
 * - 0：队伍未排队任何副本，或已离开寻组系统
 * - 非零：队伍已排队或正在进行的副本 ID
 *
 * 性能考虑：
 * - O(1) 时间复杂度，简单的位运算和条件判断
 * - const 方法，保证线程安全性（读操作）
 * - 位运算开销极小，可忽略不计
 *
 * 副本 ID 的来源：
 * - 从 DBC 文件加载（DungeonEntries.dbc）
 * - 通过数据库配置映射
 * - 包含副本的难度、类型等详细信息
 *
 * @param asId 是否仅返回副本 ID（默认为 true）
 *             - true: 返回低 24 位的副本 ID（纯 ID）
 *             - false: 返回完整的 32 位值（包含类型标识）
 *
 * @return uint32 副本 ID 或完整值
 *         - 0：未设置副本
 *         - 非零：副本 ID（asId=true）或完整值（asId=false）
 *
 * @note 默认参数为 true，表示大多数场景只需要纯副本 ID
 * @note 副本 ID 的具体含义由数据库和配置定义
 * @note 返回 0 可能表示队伍未参与寻组或副本已完成
 *
 * @see SetDungeon() 设置副本 ID
 * @see SetState() 状态改变时可能清空副本信息
 */
uint32 LfgGroupData::GetDungeon(bool asId /* = true */) const
{
    if (asId)
        return (m_Dungeon & 0x00FFFFFF);  // 提取低 24 位，获取纯副本 ID
    else
        return m_Dungeon;                  // 返回完整值，包含类型标识
}

/**
 * @brief 获取剩余踢人次数 - 踢人次数查询接口
 *
 * 返回队伍当前还可以发起的踢人投票次数。这是踢人机制的配额限制。
 *
 * 踢人次数机制：
 * - 初始值：LFG_GROUP_MAX_KICKS (3) 次
 * - 用途：限制队伍在单次副本中踢人的次数
 * - 目的：防止滥用踢人机制，保护玩家权益
 * - 重置：队伍离开寻组系统时重置为最大值
 *
 * 返回值含义：
 * - 3：完整踢人次数（初始状态或刚重置）
 * - 2：已使用 1 次踢人机会
 * - 1：已使用 2 次踢人机会
 * - 0：踢人次数已耗尽，无法再踢人
 *
 * 使用场景：
 * 1. 判断是否可以踢人：
 *    @code
 *    if (groupData.GetKicksLeft() > 0)
 *    {
 *        // 还有踢人次数，可以发起踢人投票
 *        if (!groupData.IsVoteKickActive())
 *        {
 *            // 没有进行中的踢人投票，可以发起新的
 *        }
 *    }
 *    else
 *    {
 *        // 踢人次数已用完，无法踢人
 *        SendNotification(player, "您的队伍已用完踢人次数。");
 *    }
 *    @endcode
 *
 * 2. UI 显示剩余次数：
 *    @code
 *    uint8 kicks = groupData.GetKicksLeft();
 *    SendAreaTriggerMessage("剩余踢人次数: %u/%u", kicks, LFG_GROUP_MAX_KICKS);
 *    @endcode
 *
 * 3. 日志记录：
 *    @code
 *    LOG_DEBUG("lfg", "队伍 {} 剩余踢人次数: {}",
 *              groupGuid.ToString().c_str(), groupData.GetKicksLeft());
 *    @endcode
 *
 * 与其他方法的关系：
 * - DecreaseKicksLeft()：递减踢人次数
 * - SetState(LFG_STATE_NONE)：重置踢人次数
 * - IsVoteKickActive()：检查是否有进行中的踢人投票
 *
 * 踢人次数耗尽的后果：
 * - 无法发起新的踢人投票
 * - 队伍成员需要尝试其他方式解决问题：
 *   - 沟通协商
 *   - 退出队伍重新排队
 *   - 等待副本完成
 * - 暴雪设计此限制的目的是促进玩家合作
 *
 * 典型流程示例：
 * @code
 * // 初始状态：3 次踢人机会
 * GetKicksLeft() == 3;
 *
 * // 第一次踢人
 * DecreaseKicksLeft();
 * GetKicksLeft() == 2;
 *
 * // 第二次踢人
 * DecreaseKicksLeft();
 * GetKicksLeft() == 1;
 *
 * // 第三次踢人
 * DecreaseKicksLeft();
 * GetKicksLeft() == 0;  // 次数用完
 *
 * // 队伍离开寻组系统，重置
 * SetState(LFG_STATE_NONE);
 * GetKicksLeft() == 3;  // 重置为最大值
 * @endcode
 *
 * 性能考虑：
 * - O(1) 时间复杂度，直接返回成员变量
 * - const 方法，保证线程安全性（读操作）
 *
 * @return uint8 剩余踢人次数，范围 0-3
 *         - 3：完整踢人次数（初始状态）
 *         - 0-2：部分使用
 *         - 0：次数已用完，无法踢人
 *
 * @note 踢人次数限制是暴雪官方设计，防止踢人机制被滥用
 * @note 该方法仅返回剩余次数，不检查是否有进行中的踢人投票
 * @note 在实际使用中应结合 IsVoteKickActive() 进行完整判断
 *
 * @see DecreaseKicksLeft() 递减踢人次数
 * @see IsVoteKickActive() 检查踢人投票是否激活
 * @see LFG_GROUP_MAX_KICKS 最大踢人次数常量（3）
 */
uint8 LfgGroupData::GetKicksLeft() const
{
    return m_KicksLeft;
}

/**
 * @brief 设置踢人投票是否激活 - 踢人投票状态管理
 *
 * 用于标记当前是否正在进行踢人投票。踢人投票期间，需要限制某些操作。
 *
 * 踢人投票机制：
 * - 队长发起踢人提案
 * - 其他队员投票同意或拒绝
 * - 达到一定比例同意后踢出玩家
 * - 每次踢人消耗一次踢人配额
 *
 * 激活状态的意义：
 * 1. 防止并发踢人：
 *    - 同一时间只能有一个踢人投票进行
 *    - 避免投票冲突和混乱
 *
 * 2. 限制玩家操作：
 *    - 被踢玩家可能无法离开队伍
 *    - 阻止该玩家执行某些特殊操作
 *
 * 3. 状态同步：
 *    - 通知所有队员踢人投票状态
 *    - 显示投票界面和进度
 *
 * 使用场景：
 * 1. 发起踢人投票：
 *    @code
 *    if (groupData.GetKicksLeft() > 0 && !groupData.IsVoteKickActive())
 *    {
 *        groupData.SetVoteKick(true);        // 标记激活
 *        groupData.DecreaseKicksLeft();      // 消耗踢人次数
 *        // ... 发送踢人提案，开始投票 ...
 *    }
 *    @endcode
 *
 * 2. 投票结束（成功踢出或被拒绝）：
 *    @code
 *    // 投票结束，无论结果如何都取消激活
 *    groupData.SetVoteKick(false);
 *
 *    if (votePassed)
 *    {
 *        // 执行踢人操作
 *    }
 *    else
 *    {
 *        // 投票被拒绝，通知相关玩家
 *    }
 *    @endcode
 *
 * 3. 异常中断：
 *    @code
 *    // 被踢玩家掉线或离开，取消踢人投票
 *    groupData.SetVoteKick(false);
 *    @endcode
 *
 * 状态检查：
 * - 在发起踢人前必须检查 IsVoteKickActive()
 * - 如果返回 true，说明已有踢人投票进行中，不能发起新的
 * - 如果返回 false，可以安全地发起新的踢人投票
 *
 * 典型错误处理：
 * @code
 * if (groupData.IsVoteKickActive())
 * {
 *     // 已有踢人投票进行中，拒绝新的踢人请求
 *     SendNotification(player, "已有踢人投票进行中，请等待完成。");
 *     return;
 * }
 *
 * if (groupData.GetKicksLeft() == 0)
 * {
 *     // 踢人次数用完
 *     SendNotification(player, "您的队伍已用完踢人次数。");
 *     return;
 * }
 *
 * // 检查通过，可以发起踢人投票
 * groupData.SetVoteKick(true);
 * groupData.DecreaseKicksLeft();
 * // ... 继续踢人流程 ...
 * @endcode
 *
 * 数据一致性：
 * - 该状态应与实际的踢人投票流程同步
 * - 投票开始时设为 true，结束时设为 false
 * - 不应出现投票已结束但状态仍为 true 的情况
 *
 * 性能考虑：
 * - O(1) 时间复杂度，简单的布尔赋值
 * - 不触发任何其他操作或通知
 *
 * @param active 踢人投票的激活状态
 *               - true: 踢人投票正在进行中
 *               - false: 没有进行中的踢人投票
 *
 * @note 该方法不验证是否可以激活踢人投票，由调用者负责
 * @note 该方法不自动递减踢人次数，需要显式调用 DecreaseKicksLeft()
 * @note 投票结束后务必设置为 false，否则会阻止后续踢人操作
 *
 * @see IsVoteKickActive() 检查踢人投票是否激活
 * @see DecreaseKicksLeft() 递减踢人次数
 * @see GetKicksLeft() 获取剩余踢人次数
 */
void LfgGroupData::SetVoteKick(bool active)
{
    m_VoteKickActive = active;
}

/**
 * @brief 检查是否有进行中的踢人投票 - 踢人投票状态查询
 *
 * 返回当前是否有踢人投票正在进行。用于防止并发踢人和操作冲突。
 *
 * 踢人投票的生命周期：
 * 1. 发起阶段：
 *    - 队长选择要踢出的玩家
 *    - 系统检查踢人次数和当前状态
 *    - 如果 IsVoteKickActive() 返回 false，允许发起
 *    - 调用 SetVoteKick(true) 标记激活
 *
 * 2. 投票阶段：
 *    - IsVoteKickActive() 返回 true
 *    - 其他队员收到投票请求
 *    - 可以投票同意或拒绝
 *    - 此期间不允许发起新的踢人投票
 *
 * 3. 结束阶段：
 *    - 投票完成（成功或失败）
 *    - 调用 SetVoteKick(false) 取消激活
 *    - 如果成功，执行踢人操作
 *    - 可以发起新的踢人投票（如果有剩余次数）
 *
 * 返回值的含义：
 * - true：踢人投票正在进行中
 *   - 不能发起新的踢人投票
 *   - 被踢玩家可能受到某些限制
 *   - UI 显示投票进度
 *
 * - false：没有进行中的踢人投票
 *   - 可以发起新的踢人投票（如果有次数）
 *   - 正常的队伍状态
 *
 * 使用场景：
 * 1. 发起踢人前的检查：
 *    @code
 *    // 检查是否可以发起踢人投票
 *    if (groupData.IsVoteKickActive())
 *    {
 *        SendNotification(player, "已有踢人投票进行中，请等待完成。");
 *        return false;
 *    }
 *
 *    if (groupData.GetKicksLeft() == 0)
 *    {
 *        SendNotification(player, "您的队伍已用完踢人次数。");
 *        return false;
 *    }
 *
 *    // 检查通过，可以发起踢人投票
 *    return true;
 *    @endcode
 *
 * 2. 判断是否允许某些操作：
 *    @code
 *    if (groupData.IsVoteKickActive())
 *    {
 *        // 踢人投票进行中，限制某些操作
 *        SendNotification(player, "踢人投票进行中，暂时无法执行此操作。");
 *        return;
 *    }
 *    @endcode
 *
 * 3. UI 状态显示：
 *    @code
 *    bool isVoting = groupData.IsVoteKickActive();
 *    if (isVoting)
 *    {
 *        ShowVoteKickUI();  // 显示投票界面
 *    }
 *    else
 *    {
 *        HideVoteKickUI();  // 隐藏投票界面
 *    }
 *    @endcode
 *
 * 4. 日志记录和调试：
 *    @code
 *    LOG_DEBUG("lfg", "队伍 {} 踢人投票状态: {}",
 *              groupGuid.ToString().c_str(),
 *              groupData.IsVoteKickActive() ? "进行中" : "无");
 *    @endcode
 *
 * 并发保护机制：
 * - 通过 m_VoteKickActive 标志位实现简单的锁机制
 * - 防止同一时间多个踢人投票并发执行
 * - 保证投票流程的顺序性和一致性
 *
 * 与其他状态的配合：
 * @code
 * // 完整的踢人条件检查
 * bool CanInitiateKickVote(LfgGroupData& groupData, ObjectGuid kicker)
 * {
 *     // 检查发起者是否为队长
 *     if (groupData.GetLeader() != kicker)
 *         return false;
 *
 *     // 检查是否有踢人次数
 *     if (groupData.GetKicksLeft() == 0)
 *         return false;
 *
 *     // 检查是否有进行中的踢人投票
 *     if (groupData.IsVoteKickActive())
 *         return false;
 *
 *     // 所有条件满足
 *     return true;
 * }
 * @endcode
 *
 * 异常情况处理：
 * 1. 投票中断：
 *    - 被踢玩家掉线：应取消踢人投票
 *    - 队长掉线：应取消踢人投票
 *    - 系统错误：确保状态被清理
 *
 * 2. 状态清理：
 *    - 队伍解散时：踢人投票自动失效
 *    - 副本完成时：清理踢人状态
 *    - 超时：投票长时间未完成应自动取消
 *
 * 性能考虑：
 * - O(1) 时间复杂度，直接返回成员变量
 * - const 方法，保证线程安全性（读操作）
 *
 * @return bool 踢人投票的激活状态
 *         - true: 有踢人投票正在进行
 *         - false: 没有进行中的踢人投票
 *
 * @note 该方法仅返回状态，不进行其他检查（如踢人次数）
 * @note 在发起踢人前应同时检查 IsVoteKickActive() 和 GetKicksLeft()
 * @note 投票结束后务必调用 SetVoteKick(false) 清理状态
 *
 * @see SetVoteKick() 设置踢人投票状态
 * @see GetKicksLeft() 获取剩余踢人次数
 * @see DecreaseKicksLeft() 递减踢人次数
 */
bool LfgGroupData::IsVoteKickActive() const
{
    return m_VoteKickActive;
}

} // namespace lfg
