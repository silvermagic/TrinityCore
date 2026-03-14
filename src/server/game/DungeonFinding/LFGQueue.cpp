/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file LFGQueue.cpp
 * @brief LFG队列系统实现 - 核心匹配逻辑
 *
 * 本文件实现了LFG系统的核心匹配算法，主要功能包括：
 * - 队列管理：添加、移除玩家和队伍
 * - 兼容性检查：验证玩家组合是否可以组成有效队伍
 * - 队伍匹配：自动组成符合要求的队伍（1坦克+1治疗+3输出）
 * - 等待时间统计：计算和更新各角色的平均等待时间
 *
 * 核心算法说明：
 * 1. 使用回溯算法尝试不同的玩家组合
 * 2. 缓存兼容性检查结果避免重复计算
 * 3. 优先处理新加入队列的玩家
 * 4. 使用GUID字符串作为缓存键
 *
 * 性能优化策略：
 * - 兼容性结果缓存（CompatibleMapStore）
 * - 分层队列管理（新队列、当前队列）
 * - 最佳兼容组合缓存（bestCompatible）
 */

#include "ObjectDefines.h"
#include "Containers.h"
#include "DBCStructure.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Group.h"
#include "LFGQueue.h"
#include "LFGMgr.h"
#include "Log.h"

namespace lfg
{

/**
 * @brief 将GUID列表连接成字符串
 *
 * @param check GUID列表
 * @return 用"|"分隔的GUID字符串
 *
 * @details 该函数将GUID列表转换为字符串，用作兼容性缓存的键。
 *          使用Set去重，确保每个GUID只出现一次。
 *          GUID按升序排列，保证相同组合生成相同的键。
 *
 * @performance 时间复杂度O(n log n)，其中n为GUID数量
 *
 * @example
 *   GuidList guids = {guid1, guid2, guid3};
 *   std::string key = ConcatenateGuids(guids);  // "12345|23456|34567"
 */
std::string ConcatenateGuids(GuidList const& check)
{
    if (check.empty())
        return "";

    // 使用Set去重并自动排序
    GuidSet guids(check.begin(), check.end());

    std::ostringstream o;

    GuidSet::const_iterator it = guids.begin();
    o << it->GetRawValue();
    // 遍历剩余的GUID，每个前面添加"|"分隔符
    for (++it; it != guids.end(); ++it)
        o << '|' << it->GetRawValue();

    return o.str();
}

/**
 * @brief 获取兼容性状态的字符串表示（用于调试和日志）
 *
 * @param compatibles 兼容性枚举值
 * @return 兼容性状态的字符串描述
 */
char const* GetCompatibleString(LfgCompatibility compatibles)
{
    switch (compatibles)
    {
        case LFG_COMPATIBILITY_PENDING:
            return "Pending";
        case LFG_COMPATIBLES_BAD_STATES:
            return "Compatibles (Bad States)";
        case LFG_COMPATIBLES_MATCH:
            return "Match";
        case LFG_COMPATIBLES_WITH_LESS_PLAYERS:
            return "Compatibles (Not enough players)";
        case LFG_INCOMPATIBLES_HAS_IGNORES:
            return "Has ignores";
        case LFG_INCOMPATIBLES_MULTIPLE_LFG_GROUPS:
            return "Multiple Lfg Groups";
        case LFG_INCOMPATIBLES_NO_DUNGEONS:
            return "Incompatible dungeons";
        case LFG_INCOMPATIBLES_NO_ROLES:
            return "Incompatible roles";
        case LFG_INCOMPATIBLES_TOO_MUCH_PLAYERS:
            return "Too many players";
        case LFG_INCOMPATIBLES_WRONG_GROUP_SIZE:
            return "Wrong group size";
        default:
            return "Unknown";
    }
}

/**
 * @brief LfgQueueData默认构造函数
 *
 * @details 初始化队列数据结构，设置默认需要的角色数量
 *          加入时间设为当前游戏时间
 */
LfgQueueData::LfgQueueData() : joinTime(GameTime::GetGameTime()), tanks(LFG_TANKS_NEEDED),
healers(LFG_HEALERS_NEEDED), dps(LFG_DPS_NEEDED)
{ }

/**
 * @brief 获取匹配玩家的详细角色信息字符串
 *
 * @param check 玩家GUID列表
 * @return 格式化的字符串，包含每个GUID的角色信息
 *
 * @details 该函数用于生成包含玩家GUID和角色信息的调试字符串。
 *          输出格式：GUID1 角色1|GUID2 角色2|...
 *          角色信息不包含队长标志，只显示坦克/治疗/输出。
 *
 * @example
 *   输出："12345 坦克|23456 治疗|34567 输出"
 */
std::string LFGQueue::GetDetailedMatchRoles(GuidList const& check) const
{
    if (check.empty())
        return "";

    // 使用Set去重并自动排序
    GuidSet guids(check.begin(), check.end());

    std::ostringstream o;

    GuidSet::const_iterator it = guids.begin();
    o << it->GetRawValue();
    LfgQueueDataContainer::const_iterator itQueue = QueueDataStore.find(*it);
    if (itQueue != QueueDataStore.end())
    {
        // 跳过队长标志，只记录坦克/治疗/输出
        auto role = itQueue->second.roles.find(*it);
        if (role != itQueue->second.roles.end())
            o << ' ' << GetRolesString(itQueue->second.roles.at(*it) & uint8(~PLAYER_ROLE_LEADER));
    }

    // 遍历剩余的GUID
    for (++it; it != guids.end(); ++it)
    {
        o << '|' << it->GetRawValue();
        itQueue = QueueDataStore.find(*it);
        if (itQueue != QueueDataStore.end())
        {
            // 跳过队长标志，只记录坦克/治疗/输出
            auto role = itQueue->second.roles.find(*it);
            if (role != itQueue->second.roles.end())
                o << ' ' << GetRolesString(itQueue->second.roles.at(*it) & uint8(~PLAYER_ROLE_LEADER));
        }
    }

    return o.str();
}

/**
 * @brief 将玩家或队伍添加到队列
 *
 * @param guid 玩家或队伍的GUID
 * @param reAdd 是否为重新加入队列（提案失败后重新排队）
 *
 * @details 该函数将已存在的队列数据添加到队列系统中。
 *          reAdd=true时添加到当前队列前端，提高匹配优先级。
 *          reAdd=false时添加到新队列，等待下一轮匹配。
 *
 * @note 必须先调用AddQueueData创建队列数据
 */
void LFGQueue::AddToQueue(ObjectGuid guid, bool reAdd)
{
    // 检查队列数据是否存在
    LfgQueueDataContainer::iterator itQueue = QueueDataStore.find(guid);
    if (itQueue == QueueDataStore.end())
    {
        TC_LOG_ERROR("lfg.queue.add", "Queue data not found for [{}]", guid.ToString());
        return;
    }

    // 根据reAdd参数决定添加到哪个队列
    if (reAdd)
        AddToFrontCurrentQueue(guid);  // 添加到当前队列前端（高优先级）
    else
        AddToNewQueue(guid);           // 添加到新队列（等待下一轮匹配）
}

/**
 * @brief 从队列中移除玩家或队伍
 *
 * @param guid 要移除的GUID
 *
 * @details 该函数完全清除指定GUID的队列信息：
 *          1. 从新队列和当前队列中移除
 *          2. 从兼容性缓存中移除相关条目
 *          3. 更新其他玩家的bestCompatible缓存
 *          4. 删除队列数据
 *
 * @note 当玩家离开LFG、下线或匹配成功时调用
 */
void LFGQueue::RemoveFromQueue(ObjectGuid guid)
{
    // 从各个队列列表中移除
    RemoveFromNewQueue(guid);
    RemoveFromCurrentQueue(guid);
    RemoveFromCompatibles(guid);

    // 生成GUID字符串用于搜索
    std::ostringstream o;
    o << guid.GetRawValue();
    std::string sguid = o.str();

    // 更新所有包含此GUID的bestCompatible缓存
    LfgQueueDataContainer::iterator itDelete = QueueDataStore.end();
    for (LfgQueueDataContainer::iterator itr = QueueDataStore.begin(); itr != QueueDataStore.end(); ++itr)
        if (itr->first != guid)
        {
            // 如果此队列数据的bestCompatible包含被移除的GUID
            if (std::string::npos != itr->second.bestCompatible.find(sguid))
            {
                itr->second.bestCompatible.clear();
                FindBestCompatibleInQueue(itr);  // 重新查找最佳兼容组合
            }
        }
        else
            itDelete = itr;  // 标记待删除的条目

    // 删除队列数据
    if (itDelete != QueueDataStore.end())
        QueueDataStore.erase(itDelete);
}

/**
 * @brief 添加到新队列（内部函数）
 * @param guid 要添加的GUID
 */
void LFGQueue::AddToNewQueue(ObjectGuid guid)
{
    newToQueueStore.push_back(guid);
}

/**
 * @brief 从新队列移除（内部函数）
 * @param guid 要移除的GUID
 */
void LFGQueue::RemoveFromNewQueue(ObjectGuid guid)
{
    newToQueueStore.remove(guid);
}

/**
 * @brief 添加到当前队列末尾（内部函数）
 * @param guid 要添加的GUID
 */
void LFGQueue::AddToCurrentQueue(ObjectGuid guid)
{
    currentQueueStore.push_back(guid);
}

/**
 * @brief 添加到当前队列前端（内部函数）
 * @param guid 要添加的GUID
 * @details 用于重新加入队列的玩家，提高匹配优先级
 */
void LFGQueue::AddToFrontCurrentQueue(ObjectGuid guid)
{
    currentQueueStore.push_front(guid);
}

/**
 * @brief 从当前队列移除（内部函数）
 * @param guid 要移除的GUID
 */
void LFGQueue::RemoveFromCurrentQueue(ObjectGuid guid)
{
    currentQueueStore.remove(guid);
}

/**
 * @brief 添加队列数据并加入队列
 *
 * @param guid 玩家或队伍的GUID
 * @param joinTime 加入时间戳
 * @param dungeons 选择的副本集合
 * @param rolesMap 角色映射
 *
 * @details 该函数创建新的队列数据并添加到队列系统
 */
void LFGQueue::AddQueueData(ObjectGuid guid, time_t joinTime, LfgDungeonSet const& dungeons, LfgRolesMap const& rolesMap)
{
    QueueDataStore[guid] = LfgQueueData(joinTime, dungeons, rolesMap);
    AddToQueue(guid);
}

/**
 * @brief 移除队列数据（仅移除数据，不从队列列表中移除）
 * @param guid 要移除的GUID
 */
void LFGQueue::RemoveQueueData(ObjectGuid guid)
{
    LfgQueueDataContainer::iterator it = QueueDataStore.find(guid);
    if (it != QueueDataStore.end())
        QueueDataStore.erase(it);
}

/**
 * @brief 更新平均等待时间
 *
 * @param waitTime 本次等待时间（秒）
 * @param dungeonId 副本ID
 *
 * @details 使用增量平均算法更新平均等待时间，避免存储所有历史数据
 *          公式：new_avg = (old_avg * old_count + new_value) / new_count
 */
void LFGQueue::UpdateWaitTimeAvg(int32 waitTime, uint32 dungeonId)
{
    LfgWaitTime &wt = waitTimesAvgStore[dungeonId];
    uint32 old_number = wt.number++;
    wt.time = int32((wt.time * old_number + waitTime) / wt.number);
}

/**
 * @brief 更新坦克等待时间
 * @param waitTime 本次等待时间（秒）
 * @param dungeonId 副本ID
 */
void LFGQueue::UpdateWaitTimeTank(int32 waitTime, uint32 dungeonId)
{
    LfgWaitTime &wt = waitTimesTankStore[dungeonId];
    uint32 old_number = wt.number++;
    wt.time = int32((wt.time * old_number + waitTime) / wt.number);
}

/**
 * @brief 更新治疗等待时间
 * @param waitTime 本次等待时间（秒）
 * @param dungeonId 副本ID
 */
void LFGQueue::UpdateWaitTimeHealer(int32 waitTime, uint32 dungeonId)
{
    LfgWaitTime &wt = waitTimesHealerStore[dungeonId];
    uint32 old_number = wt.number++;
    wt.time = int32((wt.time * old_number + waitTime) / wt.number);
}

/**
 * @brief 更新输出等待时间
 * @param waitTime 本次等待时间（秒）
 * @param dungeonId 副本ID
 */
void LFGQueue::UpdateWaitTimeDps(int32 waitTime, uint32 dungeonId)
{
    LfgWaitTime &wt = waitTimesDpsStore[dungeonId];
    uint32 old_number = wt.number++;
    wt.time = int32((wt.time * old_number + waitTime) / wt.number);
}

/**
 * @brief 从兼容性缓存中移除包含指定GUID的所有条目
 *
 * @param guid 要移除的GUID
 *
 * @details 遍历兼容性缓存，删除所有键中包含该GUID的条目。
 *          这确保了离开队列的玩家不会影响后续的兼容性检查。
 *
 * @performance 时间复杂度O(n*m)，其中n为缓存条目数，m为GUID字符串长度
 */
void LFGQueue::RemoveFromCompatibles(ObjectGuid guid)
{
    std::stringstream out;
    out << guid.GetRawValue();
    std::string strGuid = out.str();

    TC_LOG_DEBUG("lfg.queue.data.compatibles.remove", "Removing {}", guid.ToString());
    // 遍历兼容性缓存，删除包含该GUID的条目
    for (LfgCompatibleContainer::iterator itNext = CompatibleMapStore.begin(); itNext != CompatibleMapStore.end();)
    {
        LfgCompatibleContainer::iterator it = itNext++;
        if (std::string::npos != it->first.find(strGuid))
            CompatibleMapStore.erase(it);
    }
}

/**
 * @brief 设置兼容性状态（不包含角色数据）
 *
 * @param key GUID组合字符串（用"|"分隔）
 * @param compatibles 兼容性状态
 */
void LFGQueue::SetCompatibles(std::string const& key, LfgCompatibility compatibles)
{
    LfgCompatibilityData& data = CompatibleMapStore[key];
    data.compatibility = compatibles;
}

/**
 * @brief 设置完整的兼容性数据（包含角色分配）
 *
 * @param key GUID组合字符串
 * @param data 完整的兼容性数据（状态+角色）
 */
void LFGQueue::SetCompatibilityData(std::string const& key, LfgCompatibilityData const& data)
{
    CompatibleMapStore[key] = data;
}

/**
 * @brief 获取兼容性状态
 *
 * @param key GUID组合字符串
 * @return 兼容性状态，如果未缓存则返回LFG_COMPATIBILITY_PENDING
 */
LfgCompatibility LFGQueue::GetCompatibles(std::string const& key)
{
    LfgCompatibleContainer::iterator itr = CompatibleMapStore.find(key);
    if (itr != CompatibleMapStore.end())
        return itr->second.compatibility;

    return LFG_COMPATIBILITY_PENDING;
}

/**
 * @brief 获取完整的兼容性数据指针
 *
 * @param key GUID组合字符串
 * @return 兼容性数据指针，如果未缓存则返回nullptr
 */
LfgCompatibilityData* LFGQueue::GetCompatibilityData(std::string const& key)
{
    LfgCompatibleContainer::iterator itr = CompatibleMapStore.find(key);
    if (itr != CompatibleMapStore.end())
        return &(itr->second);

    return nullptr;
}

/**
 * @brief 尝试组成新的队伍
 *
 * @return 成功创建的提案数量
 *
 * @details 该函数是LFG匹配的核心，工作流程：
 *          1. 从newToQueueStore取出一个玩家/队伍
 *          2. 尝试与currentQueueStore中的其他玩家/队伍匹配
 *          3. 如果匹配成功，创建提案
 *          4. 如果匹配失败，将玩家/队伍加入currentQueueStore
 *
 * @note 该函数由LFGMgr::Update定期调用（每15秒一次）
 *
 * @performance 最坏情况O(n^2)，但通过兼容性缓存优化
 */
uint8 LFGQueue::FindGroups()
{
    uint8 proposals = 0;
    GuidList firstNew;
    while (!newToQueueStore.empty())
    {
        // 取出新队列的第一个玩家/队伍
        ObjectGuid frontguid = newToQueueStore.front();
        TC_LOG_DEBUG("lfg.queue.match.check.new", "Checking [{}] newToQueue({}), currentQueue({})", frontguid.ToString(),
            uint32(newToQueueStore.size()), uint32(currentQueueStore.size()));

        firstNew.clear();
        firstNew.push_back(frontguid);
        RemoveFromNewQueue(frontguid);

        // 尝试与当前队列中的玩家/队伍匹配
        GuidList temporalList = currentQueueStore;
        LfgCompatibility compatibles = FindNewGroups(firstNew, temporalList);

        if (compatibles == LFG_COMPATIBLES_MATCH)
            ++proposals;
        else
            AddToCurrentQueue(frontguid);  // 未找到匹配，加入当前队列等待下一轮
    }
    return proposals;
}

/**
 * @brief 递归查找匹配的队伍组合
 *
 * @param check 当前检查的GUID列表
 * @param all 剩余可用的GUID列表
 * @return 兼容性状态
 *
 * @details 该函数使用回溯算法尝试不同的队伍组合：
 *          1. 检查当前组合是否已缓存
 *          2. 如果未缓存，执行兼容性检查
 *          3. 如果兼容但人数不足，尝试添加更多玩家
 *          4. 如果匹配成功或已不兼容，返回结果
 *
 * @performance 使用缓存优化，避免重复计算
 */
LfgCompatibility LFGQueue::FindNewGroups(GuidList& check, GuidList& all)
{
    std::string strGuids = ConcatenateGuids(check);
    LfgCompatibility compatibles = GetCompatibles(strGuids);

    TC_LOG_DEBUG("lfg.queue.match.check", "Guids: ({}): {} - all({})", GetDetailedMatchRoles(check), GetCompatibleString(compatibles), GetDetailedMatchRoles(all));

    // 如果未缓存，执行兼容性检查
    if (compatibles == LFG_COMPATIBILITY_PENDING)
        compatibles = CheckCompatibility(check);

    // 特殊处理：兼容但状态异常的情况
    if (compatibles == LFG_COMPATIBLES_BAD_STATES && sLFGMgr->AllQueued(check))
    {
        TC_LOG_DEBUG("lfg.queue.match.check", "Guids: ({}) compatibles (cached) changed from bad states to match", GetDetailedMatchRoles(check));
        SetCompatibles(strGuids, LFG_COMPATIBLES_MATCH);
        return LFG_COMPATIBLES_MATCH;
    }

    // 如果不是"兼容但人数不足"，直接返回结果
    if (compatibles != LFG_COMPATIBLES_WITH_LESS_PLAYERS)
        return compatibles;

    // 尝试与队列中的其他玩家/队伍组合
    while (!all.empty())
    {
        check.push_back(all.front());
        all.pop_front();
        // 递归检查新组合
        LfgCompatibility subcompatibility = FindNewGroups(check, all);
        if (subcompatibility == LFG_COMPATIBLES_MATCH)
            return LFG_COMPATIBLES_MATCH;
        check.pop_back();  // 回溯，尝试下一个组合
    }
    return compatibles;
}

/**
 * @brief 检查GUID列表的兼容性（核心匹配算法）
 *
 * @param check 要检查的GUID列表
 * @return 兼容性状态
 *
 * @details 该函数执行详细的兼容性检查，步骤如下：
 *          1. 检查队伍大小是否合法
 *          2. 递归检查子组合的兼容性（优化策略）
 *          3. 统计玩家数量和LFG队伍数量
 *          4. 检查玩家是否互相屏蔽
 *          5. 检查角色是否可以组成有效队伍
 *          6. 检查是否有共同的副本选择
 *          7. 如果完全匹配，创建提案
 *
 * @complexity 最坏情况O(n^3)，但通过以下优化：
 *             - 兼容性结果缓存
 *             - 递归检查子组合
 *             - 提前终止不兼容的情况
 *
 * @note 该函数会修改队列状态，匹配成功时创建提案
 */
LfgCompatibility LFGQueue::CheckCompatibility(GuidList check)
{
    std::string strGuids = ConcatenateGuids(check);
    LfgProposal proposal;
    LfgDungeonSet proposalDungeons;
    LfgGroupsMap proposalGroups;
    LfgRolesMap proposalRoles;

    // ========== 第一步：检查队伍大小 ==========
    if (check.size() > MAX_GROUP_SIZE || check.empty())
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}): Size wrong - Not compatibles", GetDetailedMatchRoles(check));
        return LFG_INCOMPATIBLES_WRONG_GROUP_SIZE;
    }

    // ========== 第二步：递归检查子组合（优化策略） ==========
    // 如果组合大于2，先检查去掉第一个GUID后的子组合
    if (check.size() > 2)
    {
        ObjectGuid frontGuid = check.front();
        check.pop_front();

        // 检查除第一个之外的所有GUID的组合：(New, A, B, C, D) --> check(A, B, C, D)
        LfgCompatibility child_compatibles = CheckCompatibility(check);
        if (child_compatibles < LFG_COMPATIBLES_WITH_LESS_PLAYERS) // 子组合不兼容
        {
            TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) child {} not compatibles", strGuids, GetDetailedMatchRoles(check));
            SetCompatibles(strGuids, child_compatibles);
            return child_compatibles;
        }
        check.push_front(frontGuid);
    }

    // ========== 第三步：统计玩家数量和LFG队伍数量 ==========
    uint8 numPlayers = 0;
    uint8 numLfgGroups = 0;
    for (GuidList::const_iterator it = check.begin(); it != check.end() && numLfgGroups < 2 && numPlayers <= MAX_GROUP_SIZE; ++it)
    {
        ObjectGuid guid = *it;
        LfgQueueDataContainer::iterator itQueue = QueueDataStore.find(guid);
        if (itQueue == QueueDataStore.end())
        {
            TC_LOG_ERROR("lfg.queue.match.compatibility.check", "Guid: [{}] is not queued but listed as queued!", guid.ToString());
            RemoveFromQueue(guid);
            return LFG_COMPATIBILITY_PENDING;
        }

        // 存储队伍信息（如果是玩家，队伍GUID为空；如果是队伍，保存队伍GUID）
        for (LfgRolesMap::const_iterator it2 = itQueue->second.roles.begin(); it2 != itQueue->second.roles.end(); ++it2)
            proposalGroups[it2->first] = itQueue->first.IsGroup() ? itQueue->first : ObjectGuid::Empty;

        numPlayers += itQueue->second.roles.size();

        if (sLFGMgr->IsLfgGroup(guid))
        {
            if (!numLfgGroups)
                proposal.group = guid;  // 记录第一个LFG队伍
            ++numLfgGroups;
        }
    }

    // ========== 第四步：处理单人情况 ==========
    // 单个玩家或队伍，人数不足5人，总是兼容的
    if (check.size() == 1 && numPlayers != MAX_GROUP_SIZE)
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) single group. Compatibles", GetDetailedMatchRoles(check));
        LfgQueueDataContainer::iterator itQueue = QueueDataStore.find(check.front());

        LfgCompatibilityData data(LFG_COMPATIBLES_WITH_LESS_PLAYERS);
        data.roles = itQueue->second.roles;
        LFGMgr::CheckGroupRoles(data.roles);  // 规范化角色

        UpdateBestCompatibleInQueue(itQueue, strGuids, data.roles);
        SetCompatibilityData(strGuids, data);
        return LFG_COMPATIBLES_WITH_LESS_PLAYERS;
    }

    // ========== 第五步：检查多个LFG队伍 ==========
    if (numLfgGroups > 1)
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) More than one Lfggroup ({})", GetDetailedMatchRoles(check), numLfgGroups);
        SetCompatibles(strGuids, LFG_INCOMPATIBLES_MULTIPLE_LFG_GROUPS);
        return LFG_INCOMPATIBLES_MULTIPLE_LFG_GROUPS;
    }

    // ========== 第六步：检查玩家数量 ==========
    if (numPlayers > MAX_GROUP_SIZE)
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) Too many players ({})", GetDetailedMatchRoles(check), numPlayers);
        SetCompatibles(strGuids, LFG_INCOMPATIBLES_TOO_MUCH_PLAYERS);
        return LFG_INCOMPATIBLES_TOO_MUCH_PLAYERS;
    }

    // ========== 第七步：多组合检查（屏蔽、角色、副本） ==========
    // 单人组已经在加入队列时检查过，这里只检查多人组合
    if (check.size() > 1)
    {
        // 7.1 检查屏蔽列表
        for (GuidList::const_iterator it = check.begin(); it != check.end(); ++it)
        {
            LfgRolesMap const& roles = QueueDataStore[(*it)].roles;
            for (LfgRolesMap::const_iterator itRoles = roles.begin(); itRoles != roles.end(); ++itRoles)
            {
                LfgRolesMap::const_iterator itPlayer;
                for (itPlayer = proposalRoles.begin(); itPlayer != proposalRoles.end(); ++itPlayer)
                {
                    if (itRoles->first == itPlayer->first)
                        TC_LOG_ERROR("lfg.queue.match.compatibility.check", "Guids: ERROR! Player multiple times in queue! [{}]", itRoles->first.ToString());
                    else if (sLFGMgr->HasIgnore(itRoles->first, itPlayer->first))
                        break;  // 发现互相屏蔽，跳过
                }
                // 如果没有被屏蔽，添加到角色列表
                if (itPlayer == proposalRoles.end())
                    proposalRoles[itRoles->first] = itRoles->second;
            }
        }

        // 检查是否有玩家因屏蔽被排除
        if (uint8 playersize = numPlayers - proposalRoles.size())
        {
            TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) not compatible, {} players are ignoring each other", GetDetailedMatchRoles(check), playersize);
            SetCompatibles(strGuids, LFG_INCOMPATIBLES_HAS_IGNORES);
            return LFG_INCOMPATIBLES_HAS_IGNORES;
        }

        // 7.2 检查角色兼容性
        LfgRolesMap debugRoles = proposalRoles;
        if (!LFGMgr::CheckGroupRoles(proposalRoles))
        {
            std::ostringstream o;
            for (LfgRolesMap::const_iterator it = debugRoles.begin(); it != debugRoles.end(); ++it)
                o << ", " << it->first.GetRawValue() << ": " << GetRolesString(it->second);

            TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) Roles not compatible{}", GetDetailedMatchRoles(check), o.str());
            SetCompatibles(strGuids, LFG_INCOMPATIBLES_NO_ROLES);
            return LFG_INCOMPATIBLES_NO_ROLES;
        }

        // 7.3 检查副本兼容性（取交集）
        GuidList::iterator itguid = check.begin();
        proposalDungeons = QueueDataStore[*itguid].dungeons;
        std::ostringstream o;
        o << ", " << itguid->GetRawValue() << ": (" << ConcatenateDungeons(proposalDungeons) << ")";
        for (++itguid; itguid != check.end(); ++itguid)
        {
            LfgDungeonSet temporal;
            LfgDungeonSet& dungeons = QueueDataStore[*itguid].dungeons;
            o << ", " << itguid->GetRawValue() << ": (" << ConcatenateDungeons(dungeons) << ")";
            // 计算副本集合的交集
            std::set_intersection(proposalDungeons.begin(), proposalDungeons.end(), dungeons.begin(), dungeons.end(), std::inserter(temporal, temporal.begin()));
            proposalDungeons = temporal;
        }

        if (proposalDungeons.empty())
        {
            TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) No compatible dungeons{}", GetDetailedMatchRoles(check), o.str());
            SetCompatibles(strGuids, LFG_INCOMPATIBLES_NO_DUNGEONS);
            return LFG_INCOMPATIBLES_NO_DUNGEONS;
        }
    }
    else
    {
        // 单人情况，直接使用其副本和角色
        ObjectGuid gguid = *check.begin();
        LfgQueueData const& queue = QueueDataStore[gguid];
        proposalDungeons = queue.dungeons;
        proposalRoles = queue.roles;
        LFGMgr::CheckGroupRoles(proposalRoles);  // 规范化角色
    }

    // ========== 第八步：检查人数是否足够 ==========
    if (numPlayers != MAX_GROUP_SIZE)
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) Compatibles but not enough players({})", GetDetailedMatchRoles(check), numPlayers);
        LfgCompatibilityData data(LFG_COMPATIBLES_WITH_LESS_PLAYERS);
        data.roles = proposalRoles;

        // 更新所有参与者的最佳兼容组合
        for (GuidList::const_iterator itr = check.begin(); itr != check.end(); ++itr)
            UpdateBestCompatibleInQueue(QueueDataStore.find(*itr), strGuids, data.roles);

        SetCompatibilityData(strGuids, data);
        return LFG_COMPATIBLES_WITH_LESS_PLAYERS;
    }

    // ========== 第九步：创建提案 ==========
    ObjectGuid gguid = *check.begin();
    proposal.queues = check;
    proposal.isNew = numLfgGroups != 1 || sLFGMgr->GetOldState(gguid) != LFG_STATE_DUNGEON;

    // 检查所有玩家是否仍在队列中
    if (!sLFGMgr->AllQueued(check))
    {
        TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) Group MATCH but can't create proposal!", GetDetailedMatchRoles(check));
        SetCompatibles(strGuids, LFG_COMPATIBLES_BAD_STATES);
        return LFG_COMPATIBLES_BAD_STATES;
    }

    // 创建新提案
    proposal.cancelTime = GameTime::GetGameTime() + LFG_TIME_PROPOSAL;
    proposal.state = LFG_PROPOSAL_INITIATING;
    proposal.leader.Clear();
    proposal.dungeonId = Trinity::Containers::SelectRandomContainerElement(proposalDungeons);

    // 选择队长：优先选择标记为队长的玩家
    bool leader = false;
    for (LfgRolesMap::const_iterator itRoles = proposalRoles.begin(); itRoles != proposalRoles.end(); ++itRoles)
    {
        // 分配队长
        if (itRoles->second & PLAYER_ROLE_LEADER)
        {
            if (!leader || !proposal.leader || urand(0, 1))
                proposal.leader = itRoles->first;
            leader = true;
        }
        else if (!leader && (!proposal.leader || urand(0, 1)))
            proposal.leader = itRoles->first;

        // 分配玩家数据和角色
        LfgProposalPlayer &data = proposal.players[itRoles->first];
        data.role = itRoles->second;
        data.group = proposalGroups.find(itRoles->first)->second;
        // 如果是现有LFG队伍的成员，自动接受
        if (!proposal.isNew && data.group && data.group == proposal.group)
            data.accept = LFG_ANSWER_AGREE;
    }

    // 从队列中移除提案成员（但保留队列数据）
    for (GuidList::const_iterator itQueue = proposal.queues.begin(); itQueue != proposal.queues.end(); ++itQueue)
    {
        ObjectGuid guid = (*itQueue);
        RemoveFromNewQueue(guid);
        RemoveFromCurrentQueue(guid);
    }

    // 添加提案到LFGMgr
    sLFGMgr->AddProposal(proposal);

    TC_LOG_DEBUG("lfg.queue.match.compatibility.check", "Guids: ({}) MATCH! Group formed", GetDetailedMatchRoles(check));
    SetCompatibles(strGuids, LFG_COMPATIBLES_MATCH);
    return LFG_COMPATIBLES_MATCH;
}

/**
 * @brief 更新队列定时器，向所有排队玩家发送状态更新
 *
 * @param currTime 当前时间戳
 *
 * @details 该函数定期调用（每15秒），为每个排队的玩家/队伍：
 *          1. 计算已排队时间
 *          2. 根据角色获取预估等待时间
 *          3. 更新最佳兼容组合信息
 *          4. 发送队列状态更新给所有成员
 *
 * @note 该函数由LFGMgr::Update调用
 */
void LFGQueue::UpdateQueueTimers(time_t currTime)
{
    TC_LOG_TRACE("lfg.queue.timers.update", "Updating queue timers...");
    for (LfgQueueDataContainer::iterator itQueue = QueueDataStore.begin(); itQueue != QueueDataStore.end(); ++itQueue)
    {
        LfgQueueData& queueinfo = itQueue->second;
        uint32 dungeonId = (*queueinfo.dungeons.begin());
        uint32 queuedTime = uint32(currTime - queueinfo.joinTime);
        uint8 role = PLAYER_ROLE_NONE;
        int32 waitTime = -1;
        int32 wtTank = waitTimesTankStore[dungeonId].time;
        int32 wtHealer = waitTimesHealerStore[dungeonId].time;
        int32 wtDps = waitTimesDpsStore[dungeonId].time;
        int32 wtAvg = waitTimesAvgStore[dungeonId].time;

        // 计算组合角色（去掉队长标志）
        for (LfgRolesMap::const_iterator itPlayer = queueinfo.roles.begin(); itPlayer != queueinfo.roles.end(); ++itPlayer)
            role |= itPlayer->second;
        role &= ~PLAYER_ROLE_LEADER;

        // 根据角色选择对应的等待时间
        switch (role)
        {
            case PLAYER_ROLE_NONE:                                // 不应该发生 - 防御性编程
                waitTime = -1;
                break;
            case PLAYER_ROLE_TANK:
                waitTime = wtTank;
                break;
            case PLAYER_ROLE_HEALER:
                waitTime = wtHealer;
                break;
            case PLAYER_ROLE_DAMAGE:
                waitTime = wtDps;
                break;
            default:
                waitTime = wtAvg;  // 多角色选择，使用平均时间
                break;
        }

        // 如果没有最佳兼容组合缓存，查找一个
        if (queueinfo.bestCompatible.empty())
            FindBestCompatibleInQueue(itQueue);

        // 发送队列状态给所有成员
        LfgQueueStatusData queueData(dungeonId, waitTime, wtAvg, wtTank, wtHealer, wtDps, queuedTime, queueinfo.tanks, queueinfo.healers, queueinfo.dps);
        for (LfgRolesMap::const_iterator itPlayer = queueinfo.roles.begin(); itPlayer != queueinfo.roles.end(); ++itPlayer)
        {
            ObjectGuid pguid = itPlayer->first;
            LFGMgr::SendLfgQueueStatus(pguid, queueData);
        }
    }
}

/**
 * @brief 获取指定GUID的加入时间
 * @param guid 玩家或队伍的GUID
 * @return 加入队列的时间戳
 */
time_t LFGQueue::GetJoinTime(ObjectGuid guid)
{
    return QueueDataStore[guid].joinTime;
}

/**
 * @brief 导出队列状态信息（调试用）
 * @return 队列状态字符串
 */
std::string LFGQueue::DumpQueueInfo() const
{
    uint32 players = 0;
    uint32 groups = 0;
    uint32 playersInGroup = 0;

    // 统计新队列和当前队列中的玩家和队伍
    for (uint8 i = 0; i < 2; ++i)
    {
        GuidList const& queue = i ? newToQueueStore : currentQueueStore;
        for (GuidList::const_iterator it = queue.begin(); it != queue.end(); ++it)
        {
            ObjectGuid guid = *it;
            if (guid.IsGroup())
            {
                groups++;
                playersInGroup += sLFGMgr->GetPlayerCount(guid);
            }
            else
                players++;
        }
    }
    std::ostringstream o;
    o << "Queued Players: " << players << " (in group: " << playersInGroup << ") Groups: " << groups << "\n";
    return o.str();
}

/**
 * @brief 导出兼容性缓存信息（调试用）
 * @param full 是否输出完整信息
 * @return 兼容性缓存字符串
 */
std::string LFGQueue::DumpCompatibleInfo(bool full /* = false */) const
{
    std::ostringstream o;
    o << "Compatible Map size: " << CompatibleMapStore.size() << "\n";
    if (full)
        for (LfgCompatibleContainer::const_iterator itr = CompatibleMapStore.begin(); itr != CompatibleMapStore.end(); ++itr)
        {
            o << "(" << itr->first << "): " << GetCompatibleString(itr->second.compatibility);
            if (!itr->second.roles.empty())
            {
                o << " (";
                bool first = true;
                for (auto const& role : itr->second.roles)
                {
                    if (!first)
                        o << "|";
                    o << role.first.GetRawValue() << " " << GetRolesString(role.second & uint8(~PLAYER_ROLE_LEADER));
                    first = false;
                }
                o << ")";
            }
            o << "\n";
        }

    return o.str();
}

/**
 * @brief 为队列条目查找最佳兼容组合
 *
 * @param itrQueue 队列数据迭代器
 *
 * @details 在兼容性缓存中查找包含该队列条目的所有"兼容但人数不足"的组合，
 *          选择人数最多的组合作为最佳兼容组合
 */
void LFGQueue::FindBestCompatibleInQueue(LfgQueueDataContainer::iterator itrQueue)
{
    TC_LOG_DEBUG("lfg.queue.compatibles.find", "{}", itrQueue->first.ToString());
    std::ostringstream o;
    o << itrQueue->first.GetRawValue();
    std::string sguid = o.str();

    // 遍历兼容性缓存，查找包含该GUID的最佳组合
    for (LfgCompatibleContainer::const_iterator itr = CompatibleMapStore.begin(); itr != CompatibleMapStore.end(); ++itr)
        if (itr->second.compatibility == LFG_COMPATIBLES_WITH_LESS_PLAYERS &&
            std::string::npos != itr->first.find(sguid))
        {
            UpdateBestCompatibleInQueue(itrQueue, itr->first, itr->second.roles);
        }
}

/**
 * @brief 更新队列条目的最佳兼容组合
 *
 * @param itrQueue 队列数据迭代器
 * @param key GUID组合字符串
 * @param roles 角色映射
 *
 * @details 如果新组合的人数多于之前的最佳组合，则更新。
 *          同时更新所需的角色数量（坦克、治疗、输出）
 */
void LFGQueue::UpdateBestCompatibleInQueue(LfgQueueDataContainer::iterator itrQueue, std::string const& key, LfgRolesMap const& roles)
{
    LfgQueueData& queueData = itrQueue->second;

    // 计算已存储组合的人数
    uint8 storedSize = queueData.bestCompatible.empty() ? 0 :
        std::count(queueData.bestCompatible.begin(), queueData.bestCompatible.end(), '|') + 1;

    // 计算新组合的人数
    uint8 size = std::count(key.begin(), key.end(), '|') + 1;

    // 如果新组合人数不多于已存储的，不更新
    if (size <= storedSize)
        return;

    TC_LOG_DEBUG("lfg.queue.compatibles.update", "Changed ({}) to ({}) as best compatible group for {}",
        queueData.bestCompatible, key, itrQueue->first.ToString());

    // 更新最佳兼容组合
    queueData.bestCompatible = key;
    queueData.tanks = LFG_TANKS_NEEDED;
    queueData.healers = LFG_HEALERS_NEEDED;
    queueData.dps = LFG_DPS_NEEDED;

    // 计算仍需的角色数量
    for (LfgRolesMap::const_iterator it = roles.begin(); it != roles.end(); ++it)
    {
        uint8 role = it->second;
        if (role & PLAYER_ROLE_TANK)
            --queueData.tanks;
        else if (role & PLAYER_ROLE_HEALER)
            --queueData.healers;
        else
            --queueData.dps;
    }
}

} // namespace lfg
