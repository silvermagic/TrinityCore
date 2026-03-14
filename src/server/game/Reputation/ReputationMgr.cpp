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
 * @file ReputationMgr.cpp
 * @brief 声望管理器实现文件
 *
 * 本文件实现了声望管理器的所有功能，包括：
 * - 声望等级计算和转换
 * - 声望的增减和溢出机制
 * - 声望数据的数据库保存和加载
 * - 与客户端的声望状态同步
 * - 声望标志（可见性、交战状态等）的管理
 *
 * 声望系统是玩家与游戏世界互动的重要机制，决定了：
 * - NPC对玩家的态度（友好、中立、敌对）
 * - 可接取的任务和可购买的商品
 * - 可使用的传送点和特殊服务
 * - 某些成就的完成条件
 */

#include "ReputationMgr.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief 各声望等级所需的累计点数
 *
 * 定义了每个声望等级所需的点数，用于ReputationToRank函数计算等级
 * 数组顺序对应ReputationRank枚举：
 * - [0] 仇恨 (Hated): 36000点 (从-42000到-6000)
 * - [1] 敌对 (Hostile): 3000点 (从-6000到-3000)
 * - [2] 不友好 (Unfriendly): 3000点 (从-3000到0)
 * - [3] 中立 (Neutral): 3000点 (从0到3000)
 * - [4] 友善 (Friendly): 6000点 (从3000到9000)
 * - [5] 尊敬 (Honored): 12000点 (从9000到21000)
 * - [6] 崇敬 (Revered): 21000点 (从21000到42000)
 * - [7] 崇拜 (Exalted): 1000点 (从42000到43000，实际上限42999)
 */
const int32 ReputationMgr::PointsInRank[MAX_REPUTATION_RANK] = {36000, 3000, 3000, 3000, 6000, 12000, 21000, 1000};

/**
 * @brief 声望上限
 *
 * 声望的最大值为42999，对应崇拜等级的上限
 */
const int32 ReputationMgr::Reputation_Cap = 42999;

/**
 * @brief 声望下限
 *
 * 声望的最小值为-42000，对应仇恨等级的下限
 */
const int32 ReputationMgr::Reputation_Bottom = -42000;

/**
 * @brief 将声望值转换为声望等级
 * @param standing 声望值
 * @return 对应的声望等级
 *
 * 该函数通过反向遍历PointsInRank数组，计算给定声望值对应的等级
 *
 * 算法说明：
 * 1. 从声望上限+1开始（43000）
 * 2. 依次减去各等级所需的点数
 * 3. 当声望值大于等于当前限制时，返回该等级
 *
 * 性能优势：
 * - 使用累减代替乘法，计算效率更高
 * - 从高等级向低等级遍历，符合大多数玩家声望较高的实际情况
 */
ReputationRank ReputationMgr::ReputationToRank(int32 standing)
{
    int32 limit = Reputation_Cap + 1;  // 从上限+1开始，即43000
    // 从最高等级（崇拜）向最低等级（仇恨）遍历
    for (int i = MAX_REPUTATION_RANK-1; i >= MIN_REPUTATION_RANK; --i)
    {
        limit -= PointsInRank[i];  // 减去当前等级所需的点数
        if (standing >= limit)     // 如果声望值大于等于当前等级的下限
            return ReputationRank(i);
    }
    return MIN_REPUTATION_RANK;    // 如果低于最低等级下限，返回最低等级
}

/**
 * @brief 检查是否与指定阵营处于交战状态（通过阵营ID）
 * @param faction_id 阵营ID
 * @return 是否处于交战状态
 *
 * 该函数首先查找阵营条目，然后调用重载版本检查交战标志
 * 如果阵营不存在，会记录错误日志
 */
bool ReputationMgr::IsAtWar(uint32 faction_id) const
{
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction_id);

    if (!factionEntry)
    {
        // 阵营不存在，记录错误日志并返回false
        TC_LOG_ERROR("misc", "ReputationMgr::IsAtWar: Can't get AtWar flag of {} for unknown faction (faction id) #{}.", _player->GetName(), faction_id);
        return 0;
    }

    return IsAtWar(factionEntry);
}

/**
 * @brief 检查是否与指定阵营处于交战状态（通过阵营条目）
 * @param factionEntry 阵营条目指针
 * @return 是否处于交战状态
 *
 * 检查阵营状态中的FACTION_FLAG_AT_WAR标志位
 */
bool ReputationMgr::IsAtWar(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return false;

    // 获取阵营状态，检查交战标志
    if (FactionState const* factionState = GetState(factionEntry))
        return (factionState->Flags & FACTION_FLAG_AT_WAR) != 0;
    return false;
}

/**
 * @brief 检查声望是否允许用于指定队伍
 * @param team 队伍ID（TEAM_ALLIANCE或TEAM_HORDE）
 * @param factionId 阵营ID
 * @return 是否允许获得该阵营的声望
 *
 * 该函数用于处理某些特殊情况，某些任务会给对立阵营的专属阵营奖励声望，
 * 但DBC数据无法区分阵营专属声望，因此需要硬编码处理
 *
 * 特殊处理：
 * - 部落玩家不能获得联盟先锋（1037）和荣耀堡（946）的声望
 * - 联盟玩家不能获得萨尔玛（947）的声望
 *
 * @hack 这是一种变通方案，理想情况下应该在DBC中标识阵营专属声望
 */
bool ReputationMgr::IsReputationAllowedForTeam(TeamId team, uint32 factionId) const
{
    // @hack 某些任务给联盟专属和部落专属阵营都奖励声望，但DBC数据不允许识别阵营专属声望
    if (team == TEAM_HORDE && (
        factionId == 1037 || // 联盟先锋（Alliance Vanguard）
        factionId == 946))   // 荣耀堡（Honor Hold）
        return false;

    if (team == TEAM_ALLIANCE &&
        factionId == 947)    // 萨尔玛（Thrallmar）
        return false;

    return true;
}

/**
 * @brief 获取指定阵营的当前声望值（通过阵营ID）
 * @param faction_id 阵营ID
 * @return 当前声望值，如果阵营不存在则返回0
 *
 * 根据阵营ID查找阵营条目，然后获取当前声望值
 */
int32 ReputationMgr::GetReputation(uint32 faction_id) const
{
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction_id);

    if (!factionEntry)
    {
        // 阵营不存在，记录错误日志并返回0
        TC_LOG_ERROR("misc", "ReputationMgr::GetReputation: Can't get reputation of {} for unknown faction (faction id) #{}.", _player->GetName(), faction_id);
        return 0;
    }

    return GetReputation(factionEntry);
}

/**
 * @brief 获取指定阵营的基础声望值
 * @param factionEntry 阵营条目指针
 * @return 基础声望值
 *
 * 根据玩家的种族和职业，从FactionEntry中查找对应的基础声望
 *
 * FactionEntry中最多可以有4组不同的种族/职业组合：
 * - 每组包含种族掩码、职业掩码、基础声望值和默认标志
 * - 匹配规则：种族掩码匹配且职业掩码匹配（或职业掩码为0）
 * - 特殊情况：如果种族掩码为0但职业掩码不为0，则只要职业匹配即可
 *
 * @return 如果没有匹配的组合，返回0
 */
int32 ReputationMgr::GetBaseReputation(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = _player->GetRaceMask();    // 获取玩家的种族掩码
    uint32 classMask = _player->GetClassMask();  // 获取玩家的职业掩码

    // 遍历FactionEntry中的4个种族/职业组合
    for (int i=0; i < 4; i++)
    {
        // 检查种族掩码是否匹配
        // 条件1：种族掩码匹配
        // 条件2：种族掩码为0且职业掩码不为0（仅职业限制）
        if ((factionEntry->ReputationRaceMask[i] & raceMask  ||
            (factionEntry->ReputationRaceMask[i] == 0  &&
             factionEntry->ReputationClassMask[i] != 0)) &&
            // 检查职业掩码是否匹配（职业掩码为0表示不限制职业）
            (factionEntry->ReputationClassMask[i] & classMask ||
             factionEntry->ReputationClassMask[i] == 0))

            return factionEntry->ReputationBase[i];  // 返回匹配的基础声望
    }

    // 在faction.dbc中存在一些阵营，它们有RepListId>=0（在角色声望列表中），
    // 但所有ReputationRaceMask[i]都为0，这种情况下返回0
    return 0;
}

/**
 * @brief 获取指定阵营的当前声望值（通过阵营条目）
 * @param factionEntry 阵营条目指针
 * @return 当前声望值，如果阵营不存在则返回0
 *
 * 当前声望 = 基础声望 + 声望偏移量
 * 基础声望由种族和职业决定，声望偏移量是玩家通过游戏行为获得的声望变化
 */
int32 ReputationMgr::GetReputation(FactionEntry const* factionEntry) const
{
    // 没有记录声望的阵营，直接忽略
    if (!factionEntry)
        return 0;

    // 获取阵营状态，返回基础声望+当前声望偏移量
    if (FactionState const* state = GetState(factionEntry))
        return GetBaseReputation(factionEntry) + state->Standing;

    return 0;
}

/**
 * @brief 获取指定阵营的声望等级
 * @param factionEntry 阵营条目指针
 * @return 声望等级
 *
 * 根据当前声望值计算对应的声望等级
 */
ReputationRank ReputationMgr::GetRank(FactionEntry const* factionEntry) const
{
    int32 reputation = GetReputation(factionEntry);
    return ReputationToRank(reputation);
}

/**
 * @brief 获取指定阵营的基础声望等级
 * @param factionEntry 阵营条目指针
 * @return 基础声望等级（不考虑当前声望偏移）
 *
 * 仅根据基础声望计算等级，用于初始化时的等级统计
 */
ReputationRank ReputationMgr::GetBaseRank(FactionEntry const* factionEntry) const
{
    int32 reputation = GetBaseReputation(factionEntry);
    return ReputationToRank(reputation);
}

/**
 * @brief 应用或移除强制声望反应
 * @param faction_id 阵营ID
 * @param rank 强制的声望等级
 * @param apply true为应用强制反应，false为移除
 *
 * 强制声望反应会覆盖正常的声望计算结果，常用于：
 * - 脚本控制的特殊场景（如任务中的临时敌对关系）
 * - 某些特殊物品或法术的效果
 * - 特殊事件的声望调整
 */
void ReputationMgr::ApplyForceReaction(uint32 faction_id, ReputationRank rank, bool apply)
{
    if (apply)
        _forcedReactions[faction_id] = rank;  // 添加或更新强制反应
    else
        _forcedReactions.erase(faction_id);   // 移除强制反应
}

/**
 * @brief 获取阵营的默认状态标志
 * @param factionEntry 阵营条目指针
 * @return 默认状态标志位
 *
 * 根据玩家的种族和职业，从FactionEntry中查找对应的默认标志
 * 这些标志在初始化声望时使用，包括：
 * - FACTION_FLAG_VISIBLE: 是否默认可见
 * - FACTION_FLAG_AT_WAR: 是否默认交战
 * - 其他特殊标志
 *
 * 匹配逻辑与GetBaseReputation相同
 */
uint32 ReputationMgr::GetDefaultStateFlags(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = _player->GetRaceMask();    // 获取玩家的种族掩码
    uint32 classMask = _player->GetClassMask();  // 获取玩家的职业掩码

    // 遍历FactionEntry中的4个种族/职业组合
    for (int i=0; i < 4; i++)
    {
        // 检查种族掩码是否匹配
        if ((factionEntry->ReputationRaceMask[i] & raceMask  ||
            (factionEntry->ReputationRaceMask[i] == 0  &&
             factionEntry->ReputationClassMask[i] != 0)) &&
            // 检查职业掩码是否匹配
            (factionEntry->ReputationClassMask[i] & classMask ||
             factionEntry->ReputationClassMask[i] == 0))

            return factionEntry->ReputationFlags[i];  // 返回匹配的默认标志
    }
    return 0;
}

/**
 * @brief 发送强制声望反应给客户端
 *
 * 发送SMSG_SET_FORCED_REACTIONS包，将所有强制声望反应同步到客户端
 * 强制反应会覆盖正常的声望计算，影响NPC对玩家的态度
 *
 * 包结构：
 * - uint32: 强制反应的数量
 * - 对每个强制反应：
 *   - uint32: 阵营ID（对应Faction.dbc）
 *   - uint32: 声望等级
 *
 * 调用时机：有强制反应变更时，或玩家登录时
 */
void ReputationMgr::SendForceReactions()
{
    WorldPacket data;
    // 初始化包：4字节（数量） + 每个反应8字节（阵营ID 4字节 + 等级 4字节）
    data.Initialize(SMSG_SET_FORCED_REACTIONS, 4+_forcedReactions.size()*(4+4));
    data << uint32(_forcedReactions.size());  // 写入强制反应数量

    // 遍历所有强制反应，写入阵营ID和声望等级
    for (ForcedReactions::const_iterator itr = _forcedReactions.begin(); itr != _forcedReactions.end(); ++itr)
    {
        data << uint32(itr->first);   // 阵营ID (Faction.dbc)
        data << uint32(itr->second);  // 声望等级
    }
    _player->SendDirectMessage(&data);  // 发送给客户端
}

/**
 * @brief 发送阵营状态给客户端
 * @param faction 要发送的阵营状态（为nullptr时发送所有needSend标记的阵营）
 *
 * 发送SMSG_SET_FACTION_STANDING包，通知客户端声望更新
 *
 * 包结构：
 * - float: 声望倍率（目前固定为0，可能用于未来扩展）
 * - uint8: 是否播放声望提升特效
 * - uint32: 阵营更新数量
 * - 对每个更新的阵营：
 *   - uint32: 声望列表ID
 *   - uint32: 当前声望值
 *
 * 发送逻辑：
 * 1. 如果指定了faction参数，优先发送该阵营
 * 2. 同时发送所有needSend标志为true的阵营
 * 3. 重置_sendFactionIncreased标志
 *
 * 调用时机：声望发生变化时，在SetReputation中调用
 */
void ReputationMgr::SendState(FactionState const* faction)
{
    uint32 count = faction ? 1 : 0;  // 如果指定了阵营，初始计数为1

    WorldPacket data(SMSG_SET_FACTION_STANDING, 17);
    data << float(0);  // 声望倍率（目前未使用，固定为0）
    data << uint8(_sendFactionIncreased);  // 是否播放声望提升特效
    _sendFactionIncreased = false;         // 重置标志

    size_t p_count = data.wpos();  // 记录数量字段的位置，稍后回填
    data << uint32(count);         // 先写入初始计数

    // 如果指定了阵营，写入该阵营的数据
    if (faction)
    {
        data << uint32(faction->ReputationListID);
        data << uint32(faction->Standing);
    }

    // 遍历所有阵营，发送needSend为true的阵营
    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        if (itr->second.needSend)
        {
            itr->second.needSend = false;  // 重置needSend标志
            // 如果指定了faction，避免重复发送
            if (!faction || itr->second.ReputationListID != faction->ReputationListID)
            {
                data << uint32(itr->second.ReputationListID);
                data << uint32(itr->second.Standing);
                ++count;  // 增加计数
            }
        }
    }

    // 回填实际的数量值
    data.put<uint32>(p_count, count);
    _player->SendDirectMessage(&data);
}

/**
 * @brief 发送初始声望列表给客户端
 *
 * 发送SMSG_INITIALIZE_FACTIONS包，在玩家登录时初始化客户端的声望列表
 *
 * 包结构（固定128个阵营槽位）：
 * - uint32: 总数量（固定为128）
 * - 对每个槽位（0-127）：
 *   - uint8: 阵营标志位
 *   - uint32: 当前声望值
 *
 * 客户端期望按声望列表ID的顺序接收数据，如果某些ID没有对应的阵营，
 * 则发送0填充。
 *
 * 调用时机：玩家登录完成，进入世界前
 * 性能注意：固定大小包，约644字节
 */
void ReputationMgr::SendInitialReputations()
{
    uint8 count = 128;  // 客户端固定128个声望槽位
    WorldPacket data(SMSG_INITIALIZE_FACTIONS, 4 + count * 5);  // 4字节计数 + 128*5字节数据
    data << uint32(count);

    RepListID a = 0;  // 当前槽位索引

    // 遍历所有阵营状态（按声望列表ID排序）
    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        // 填充空缺的槽位（用0填充）
        for (; a != itr->first; ++a)
        {
            data << uint8(0);   // 标志位：0（不存在）
            data << uint32(0);  // 声望值：0
        }

        // 填充实际数据
        data << uint8(itr->second.Flags);    // 阵营标志位
        data << uint32(itr->second.Standing); // 当前声望值

        itr->second.needSend = false;  // 标记为已发送，避免后续重复发送

        ++a;  // 移动到下一个槽位
    }

    // 填充剩余的空槽位
    for (; a != count; ++a)
    {
        data << uint8(0);
        data << uint32(0);
    }

    _player->SendDirectMessage(&data);
}

/**
 * @brief 发送阵营可见性更新给客户端
 * @param faction 阵营状态指针
 *
 * 发送SMSG_SET_FACTION_VISIBLE包，使阵营在客户端声望列表中显示
 *
 * 包结构：
 * - uint32: 声望列表ID
 *
 * 注意：如果玩家正在加载中（PlayerLoading），则跳过发送
 * 因为登录时会通过SendInitialReputations统一发送
 */
void ReputationMgr::SendVisible(FactionState const* faction) const
{
    // 玩家正在加载中，跳过发送（登录时会统一发送）
    if (_player->GetSession()->PlayerLoading())
        return;

    // 发送阵营可见包，使阵营在客户端声望列表中显示
    WorldPacket data(SMSG_SET_FACTION_VISIBLE, 4);
    data << faction->ReputationListID;  // 写入声望列表ID
    _player->SendDirectMessage(&data);
}

/**
 * @brief 初始化声望管理器
 *
 * 清空所有声望数据，遍历FactionStore初始化所有阵营的默认状态
 * 这是声望系统的初始化过程，为玩家创建初始的声望列表
 *
 * 初始化步骤：
 * 1. 清空阵营列表和计数器
 * 2. 遍历FactionStore中的所有阵营
 * 3. 为每个支持声望的阵营（ReputationIndex >= 0）创建默认状态
 * 4. 设置默认标志（可见性、交战状态等）
 * 5. 更新等级计数器（用于成就系统）
 *
 * 调用时机：LoadFromDB之前，或在需要重置声望时
 */
void ReputationMgr::Initialize()
{
    // 清空所有数据
    _factions.clear();
    _visibleFactionCount = 0;
    _honoredFactionCount = 0;
    _reveredFactionCount = 0;
    _exaltedFactionCount = 0;
    _sendFactionIncreased = false;

    // 遍历FactionStore中的所有阵营
    for (unsigned int i = 1; i < sFactionStore.GetNumRows(); i++)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(i);

        // 只处理支持声望的阵营（ReputationIndex >= 0）
        if (factionEntry && (factionEntry->ReputationIndex >= 0))
        {
            FactionState newFaction;
            newFaction.ID = factionEntry->ID;                          // 阵营ID
            newFaction.ReputationListID = factionEntry->ReputationIndex; // 声望列表ID
            newFaction.Standing = 0;                                   // 初始声望偏移为0
            newFaction.Flags = GetDefaultStateFlags(factionEntry);    // 获取默认标志
            newFaction.needSend = true;                                // 标记为需要发送
            newFaction.needSave = true;                                // 标记为需要保存

            // 如果默认标志包含可见标志，增加可见阵营计数
            if (newFaction.Flags & FACTION_FLAG_VISIBLE)
                ++_visibleFactionCount;

            // 更新等级计数器（从默认的HOSTILE等级到基础声望等级）
            UpdateRankCounters(REP_HOSTILE, GetBaseRank(factionEntry));

            // 将阵营状态添加到列表中
            _factions[newFaction.ReputationListID] = newFaction;
        }
    }
}

/**
 * @brief 设置声望的核心实现函数
 * @param factionEntry 阵营条目指针
 * @param standing 声望值或增量
 * @param incremental true为增量模式，false为绝对值模式
 * @param spillOverOnly 是否仅处理声望溢出
 * @return 是否成功修改
 *
 * 这是声望系统的核心函数，负责处理声望变化和声望溢出机制
 *
 * 声望溢出（Spillover）机制说明：
 * 当玩家提高某个阵营的声望时，可能会同时提高其关联阵营的声望
 * 例如：提高"铁炉堡"的声望可能会同时提高其他联盟主城的声望
 *
 * 溢出规则来源：
 * 1. 数据库的RepSpilloverTemplate表（优先级高）
 * 2. DBC中的ParentFaction信息（默认规则）
 *
 * 处理流程：
 * 1. 触发脚本事件OnPlayerReputationChange
 * 2. 处理声望溢出到关联阵营
 * 3. 设置主阵营的声望（如果spillOverOnly为false）
 * 4. 发送声望更新给客户端
 *
 * spillOverOnly参数的用途：
 * 当击杀高等级生物时，如果奖励倍率很高，可能导致声望溢出超过了主阵营获得的声望
 * 这种情况下，系统会只处理溢出部分，避免低等级玩家通过高等级生物获得过多声望
 */
bool ReputationMgr::SetReputation(FactionEntry const* factionEntry, int32 standing, bool incremental, bool spillOverOnly)
{
    // 触发脚本事件，允许脚本自定义声望变化
    sScriptMgr->OnPlayerReputationChange(_player, factionEntry->ID, standing, incremental);

    bool res = false;

    // === 第一步：处理声望溢出 ===

    // 如果数据库中定义了溢出模板，使用数据库规则（优先级高）
    if (RepSpilloverTemplate const* repTemplate = sObjectMgr->GetRepSpilloverTemplate(factionEntry->ID))
    {
        // 遍历溢出模板中的所有关联阵营
        for (uint32 i = 0; i < MAX_SPILLOVER_FACTIONS; ++i)
        {
            if (repTemplate->faction[i])
            {
                // 只有当玩家在该阵营的声望等级低于模板中指定的等级时，才溢出
                // 这防止了高级别玩家通过溢出获得过多声望
                if (_player->GetReputationRank(repTemplate->faction[i]) <= ReputationRank(repTemplate->faction_rank[i]))
                {
                    // 计算溢出声望 = 原始声望 * 溢出倍率
                    int32 spilloverRep = int32(standing * repTemplate->faction_rate[i]);
                    SetOneFactionReputation(sFactionStore.AssertEntry(repTemplate->faction[i]), spilloverRep, incremental);
                }
            }
        }
    }
    else
    {
        // 使用DBC中的ParentFaction信息处理溢出
        float spillOverRepOut = float(standing);

        // 检查是否有子阵营需要接收溢出声望
        SimpleFactionsList const* flist = GetFactionTeamList(factionEntry->ID);

        // 如果没有子阵营，检查是否有相同父阵营的阵营（兄弟阵营）
        if (!flist && factionEntry->ParentFactionID && factionEntry->ParentFactionMod[1] != 0.0f)
        {
            // 应用父阵营倍率
            spillOverRepOut *= factionEntry->ParentFactionMod[1];

            if (FactionEntry const* parent = sFactionStore.LookupEntry(factionEntry->ParentFactionID))
            {
                FactionStateList::iterator parentState = _factions.find(parent->ReputationIndex);

                // 某些团队阵营有自己的声望值，在这种情况下不溢出到其他子阵营
                // 例如：联盟先锋、部落远征军等
                if (parentState != _factions.end() && (parentState->second.Flags & FACTION_FLAG_SPECIAL))
                {
                    SetOneFactionReputation(parent, int32(spillOverRepOut), incremental);
                }
                else
                {
                    // 溢出到"兄弟"阵营（同父阵营的其他子阵营）
                    flist = GetFactionTeamList(factionEntry->ParentFactionID);
                }
            }
        }

        // 如果找到了阵营列表，进行声望溢出
        if (flist)
        {
            // 溢出到关联阵营
            for (SimpleFactionsList::const_iterator itr = flist->begin(); itr != flist->end(); ++itr)
            {
                if (FactionEntry const* factionEntryCalc = sFactionStore.LookupEntry(*itr))
                {
                    // 跳过自己
                    if (factionEntryCalc == factionEntry)
                        continue;

                    // 如果该阵营的声望等级已经超过了上限，跳过溢出
                    if (GetRank(factionEntryCalc) > ReputationRank(factionEntryCalc->ParentFactionCap[0]))
                        continue;

                    // 计算溢出声望 = 基础溢出值 * 子阵营倍率
                    int32 spilloverRep = int32(spillOverRepOut * factionEntryCalc->ParentFactionMod[0]);
                    if (spilloverRep != 0 || !incremental)
                        res = SetOneFactionReputation(factionEntryCalc, spilloverRep, incremental);
                }
            }
        }
    }

    // === 第二步：设置主阵营声望 ===

    // 溢出处理完成，更新阵营本身的声望
    FactionStateList::iterator faction = _factions.find(factionEntry->ReputationIndex);
    if (faction != _factions.end())
    {
        // 如果只更新溢出，不更新主声望（等级超过生物奖励率时使用）
        if (!spillOverOnly)
            res = SetOneFactionReputation(factionEntry, standing, incremental);

        // 只有这个阵营会报告给客户端，即使它没有自己的可见声望
        SendState(&faction->second);
    }

    return res;
}

/**
 * @brief 设置单个阵营的声望值
 * @param factionEntry 阵营条目指针
 * @param standing 声望值或增量
 * @param incremental true为增量模式，false为绝对值模式
 * @return 是否成功修改
 *
 * 直接设置单个阵营的声望，不处理声望溢出
 * 这是SetReputation的底层实现函数
 *
 * 处理流程：
 * 1. 应用服务器声望倍率（仅在增量模式）
 * 2. 限制声望值在有效范围内
 * 3. 更新声望值和标志
 * 4. 设置阵营可见
 * 5. 更新交战状态（如果变为敌对）
 * 6. 更新等级计数器
 * 7. 触发成就更新
 *
 * 性能注意：
 * - 使用floor和+0.5f实现四舍五入，避免精度损失
 * - 成就更新可能触发多个成就检查，但这是必要的
 */
bool ReputationMgr::SetOneFactionReputation(FactionEntry const* factionEntry, int32 standing, bool incremental)
{
    FactionStateList::iterator itr = _factions.find(factionEntry->ReputationIndex);
    if (itr != _factions.end())
    {
        int32 BaseRep = GetBaseReputation(factionEntry);

        if (incremental)
        {
            // 增量模式：应用服务器声望倍率
            // 使用floor和+0.5f实现四舍五入，避免int32 *= float的精度损失
            standing = int32(floor((float)standing * sWorld->getRate(RATE_REPUTATION_GAIN) + 0.5f));
            // 计算新的总声望值 = 当前声望偏移 + 基础声望 + 增量
            standing += itr->second.Standing + BaseRep;
        }

        // 限制声望值在有效范围内
        if (standing > Reputation_Cap)
            standing = Reputation_Cap;
        else if (standing < Reputation_Bottom)
            standing = Reputation_Bottom;

        // 计算声望等级变化
        ReputationRank old_rank = ReputationToRank(itr->second.Standing + BaseRep);
        ReputationRank new_rank = ReputationToRank(standing);

        // 更新声望偏移值（总声望 - 基础声望）
        itr->second.Standing = standing - BaseRep;
        itr->second.needSend = true;  // 标记为需要发送给客户端
        itr->second.needSave = true;  // 标记为需要保存到数据库

        // 设置阵营为可见
        SetVisible(&itr->second);

        // 如果声望等级降到敌对或更低，自动开启交战状态
        if (new_rank <= REP_HOSTILE)
            SetAtWar(&itr->second, true);

        // 如果声望等级提升，标记需要播放声望提升特效
        if (new_rank > old_rank)
            _sendFactionIncreased = true;

        // 更新等级计数器（用于成就系统）
        UpdateRankCounters(old_rank, new_rank);

        // 触发玩家声望变化事件
        _player->ReputationChanged(factionEntry);

        // 更新相关成就
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS,          factionEntry->ID);
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION,         factionEntry->ID);
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION, factionEntry->ID);
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION, factionEntry->ID);
        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION, factionEntry->ID);

        return true;
    }
    return false;
}

/**
 * @brief 设置阵营可见（通过阵营模板条目）
 * @param factionTemplateEntry 阵营模板条目指针
 *
 * 通过阵营模板条目设置阵营可见，但不会显示对立阵营的声望
 * 这用于在与NPC交互时自动显示其阵营的声望
 */
void ReputationMgr::SetVisible(FactionTemplateEntry const*factionTemplateEntry)
{
    if (!factionTemplateEntry->Faction)
        return;

    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionTemplateEntry->Faction))
        // 永远不显示对立阵营的声望
        // 检查种族掩码[1]（通常对应对立阵营）和基础声望是否为最低值
        if (!(factionEntry->ReputationRaceMask[1] & _player->GetRaceMask() && factionEntry->ReputationBase[1] == Reputation_Bottom))
            SetVisible(factionEntry);
}

/**
 * @brief 设置阵营可见（通过阵营条目）
 * @param factionEntry 阵营条目指针
 *
 * 根据阵营条目设置阵营可见，查找对应的阵营状态并调用内部实现
 */
void ReputationMgr::SetVisible(FactionEntry const* factionEntry)
{
    // 检查阵营是否支持声望
    if (factionEntry->ReputationIndex < 0)
        return;

    FactionStateList::iterator itr = _factions.find(factionEntry->ReputationIndex);
    if (itr == _factions.end())
        return;

    SetVisible(&itr->second);
}

/**
 * @brief 设置阵营可见（内部实现）
 * @param faction 阵营状态指针
 *
 * 将阵营设置为可见状态，使其在客户端声望列表中显示
 *
 * 可见性规则：
 * 1. 强制隐藏的阵营（FACTION_FLAG_INVISIBLE_FORCED）不能设置为可见
 * 2. 隐藏的阵营（FACTION_FLAG_HIDDEN）不能设置为可见
 * 3. 例外：拥有FACTION_FLAG_SPECIAL标志的阵营可以强制显示
 * 4. 已经可见的阵营不会重复处理
 *
 * 设置可见后会：
 * - 添加FACTION_FLAG_VISIBLE标志
 * - 标记为需要发送和保存
 * - 增加可见阵营计数
 * - 发送可见性更新包给客户端
 */
void ReputationMgr::SetVisible(FactionState* faction)
{
    // 强制隐藏或隐藏的阵营不能设置为可见，除非有特殊标志
    // FACTION_FLAG_SPECIAL用于主城和一些特殊阵营，允许强制显示
    if (faction->Flags & (FACTION_FLAG_INVISIBLE_FORCED|FACTION_FLAG_HIDDEN) && !(faction->Flags & FACTION_FLAG_SPECIAL))
        return;

    // 已经可见，跳过
    if (faction->Flags & FACTION_FLAG_VISIBLE)
        return;

    // 设置可见标志
    faction->Flags |= FACTION_FLAG_VISIBLE;
    faction->needSend = true;  // 标记为需要发送给客户端
    faction->needSave = true;  // 标记为需要保存到数据库

    ++_visibleFactionCount;    // 增加可见阵营计数

    SendVisible(faction);      // 发送可见性更新给客户端
}

/**
 * @brief 设置阵营的交战状态（通过声望列表ID）
 * @param repListID 声望列表ID
 * @param on true为开启交战，false为关闭交战
 *
 * 根据声望列表ID设置阵营的交战状态
 * 强制隐藏的阵营不能改变交战状态
 */
void ReputationMgr::SetAtWar(RepListID repListID, bool on)
{
    FactionStateList::iterator itr = _factions.find(repListID);
    if (itr == _factions.end())
        return;

    // 强制隐藏或隐藏的阵营不能改变交战状态
    if (itr->second.Flags & (FACTION_FLAG_INVISIBLE_FORCED|FACTION_FLAG_HIDDEN))
        return;

    SetAtWar(&itr->second, on);
}

/**
 * @brief 设置阵营的交战状态（内部实现）
 * @param faction 阵营状态指针
 * @param atWar true为开启交战，false为关闭交战
 *
 * 设置玩家是否与指定阵营处于交战状态
 *
 * 交战规则：
 * 1. 不能对自己阵营宣战（除非是竞争阵营，如奥尔多vs占星者）
 * 2. 强制和平的阵营（FACTION_FLAG_PEACE_FORCED）不能开启交战
 * 3. 竞争阵营（FACTION_FLAG_RIVAL）允许在声望高于仇恨时仍然交战
 * 4. 已经处于目标状态的阵营不会重复处理
 *
 * 性能注意：该函数为const，表示不修改对象逻辑状态，仅修改阵营状态数据
 */
void ReputationMgr::SetAtWar(FactionState* faction, bool atWar) const
{
    // 不允许向自己的阵营宣战，但允许竞争阵营（如奥尔多vs占星者）
    // 条件：开启交战 且 强制和平 且 不是竞争阵营 且 声望高于仇恨
    if (atWar && (faction->Flags & FACTION_FLAG_PEACE_FORCED) && !(faction->Flags & FACTION_FLAG_RIVAL) && ReputationToRank(faction->Standing) > REP_HATED)
        return;

    // 已经处于目标状态，跳过
    if (((faction->Flags & FACTION_FLAG_AT_WAR) != 0) == atWar)
        return;

    // 设置或清除交战标志
    if (atWar)
        faction->Flags |= FACTION_FLAG_AT_WAR;
    else
        faction->Flags &= ~FACTION_FLAG_AT_WAR;

    faction->needSend = true;  // 标记为需要发送给客户端
    faction->needSave = true;  // 标记为需要保存到数据库
}

/**
 * @brief 设置阵营的未激活状态（通过声望列表ID）
 * @param repListID 声望列表ID
 * @param on true为设为未激活，false为激活
 *
 * 将阵营标记为未激活状态，客户端会折叠显示这些阵营
 */
void ReputationMgr::SetInactive(RepListID repListID, bool on)
{
    FactionStateList::iterator itr = _factions.find(repListID);
    if (itr == _factions.end())
        return;

    SetInactive(&itr->second, on);
}

/**
 * @brief 设置阵营的未激活状态（内部实现）
 * @param faction 阵营状态指针
 * @param inactive true为设为未激活，false为激活
 *
 * 设置阵营的未激活状态
 *
 * 未激活规则：
 * 1. 强制隐藏或隐藏的阵营不能设置为未激活
 * 2. 未激活状态要求阵营必须先可见
 * 3. 已经处于目标状态的阵营不会重复处理
 *
 * 未激活状态影响：
 * - 客户端会在声望列表中折叠显示这些阵营
 * - 通常用于玩家不关心的阵营，减少列表混乱
 */
void ReputationMgr::SetInactive(FactionState* faction, bool inactive) const
{
    // 强制隐藏或隐藏的阵营不能设为未激活，且必须先可见
    if (inactive && ((faction->Flags & (FACTION_FLAG_INVISIBLE_FORCED|FACTION_FLAG_HIDDEN)) || !(faction->Flags & FACTION_FLAG_VISIBLE)))
        return;

    // 已经处于目标状态，跳过
    if (((faction->Flags & FACTION_FLAG_INACTIVE) != 0) == inactive)
        return;

    // 设置或清除未激活标志
    if (inactive)
        faction->Flags |= FACTION_FLAG_INACTIVE;
    else
        faction->Flags &= ~FACTION_FLAG_INACTIVE;

    faction->needSend = true;  // 标记为需要发送给客户端
    faction->needSave = true;  // 标记为需要保存到数据库
}

/**
 * @brief 从数据库加载声望数据
 * @param result 数据库查询结果集
 *
 * 先调用Initialize()初始化默认声望，然后从数据库加载已保存的声望数据
 *
 * 加载流程：
 * 1. 调用Initialize()创建所有阵营的默认状态
 * 2. 遍历数据库结果集，更新已保存的阵营状态
 * 3. 更新等级计数器
 * 4. 应用数据库中的标志（可见性、交战状态、未激活状态）
 * 5. 重置needSend和needSave标志（如果标志未改变）
 *
 * 数据库表结构（character_reputation）：
 * - faction: 阵营ID
 * - standing: 当前声望偏移值
 * - flags: 阵营标志位
 *
 * 调用时机：玩家登录加载角色数据时
 * 性能注意：仅在登录时调用，不影响游戏运行时性能
 */
void ReputationMgr::LoadFromDB(PreparedQueryResult result)
{
    // 首先初始化默认声望，确保所有阵营都有默认状态
    Initialize();

    // 查询结果：SELECT faction, standing, flags FROM character_reputation WHERE guid = ?

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            // 从数据库读取阵营ID
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(fields[0].GetUInt16());

            // 只处理支持声望的阵营
            if (factionEntry && (factionEntry->ReputationIndex >= 0))
            {
                // 获取阵营状态引用
                FactionState* faction = &_factions[factionEntry->ReputationIndex];

                // 更新声望偏移值
                faction->Standing = fields[1].GetInt32();

                // 更新等级计数器
                int32 BaseRep = GetBaseReputation(factionEntry);
                ReputationRank old_rank = ReputationToRank(BaseRep);
                ReputationRank new_rank = ReputationToRank(BaseRep + faction->Standing);
                UpdateRankCounters(old_rank, new_rank);

                // 从数据库读取标志位
                uint32 dbFactionFlags = fields[2].GetUInt16();

                // 应用可见性标志
                // SetVisible内部会检查是否强制隐藏
                if (dbFactionFlags & FACTION_FLAG_VISIBLE)
                    SetVisible(faction);

                // 应用未激活标志
                // SetInactive内部会检查可见性要求
                if (dbFactionFlags & FACTION_FLAG_INACTIVE)
                    SetInactive(faction, true);

                // 应用交战状态
                if (dbFactionFlags & FACTION_FLAG_AT_WAR)
                {
                    // 数据库中标记为交战
                    // SetAtWar内部会检查FACTION_FLAG_PEACE_FORCED
                    SetAtWar(faction, true);
                }
                else
                {
                    // 数据库中未标记为交战
                    // 允许移除交战状态（如果阵营可见且不是强制隐藏）
                    if (faction->Flags & FACTION_FLAG_VISIBLE)
                        SetAtWar(faction, false);
                }

                // 如果声望等级为敌对或更低，强制开启交战状态
                if (GetRank(factionEntry) <= REP_HOSTILE)
                    SetAtWar(faction, true);

                // 重置变更标志（如果标志与数据库相同）
                // 这避免了在登录后立即保存未改变的数据
                if (faction->Flags == dbFactionFlags)
                {
                    faction->needSend = false;
                    faction->needSave = false;
                }
            }
        }
        while (result->NextRow());
    }
}

/**
 * @brief 保存声望数据到数据库
 * @param trans 数据库事务对象
 *
 * 遍历所有阵营，将needSave标志为true的阵营状态保存到character_reputation表
 *
 * 保存策略：
 * - 使用DELETE + INSERT的方式更新数据
 * - 只保存needSave=true的阵营，避免不必要的数据库写入
 * - 使用事务批量提交，减少数据库往返次数
 *
 * 数据库操作：
 * 1. 删除旧的声望记录（CHAR_DEL_CHAR_REPUTATION_BY_FACTION）
 * 2. 插入新的声望记录（CHAR_INS_CHAR_REPUTATION_BY_FACTION）
 *
 * 调用时机：
 * - 玩家下线保存
 * - 定时保存（通常每15分钟）
 * - 声望重大变化时
 *
 * 性能注意：
 * - 使用事务批量提交，显著提高性能
 * - needSave标志机制避免频繁保存未变化的数据
 */
void ReputationMgr::SaveToDB(CharacterDatabaseTransaction trans)
{
    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        // 只保存标记为需要保存的阵营
        if (itr->second.needSave)
        {
            // 第一步：删除旧的声望记录
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_REPUTATION_BY_FACTION);
            stmt->setUInt32(0, _player->GetGUID().GetCounter());  // 玩家GUID
            stmt->setUInt16(1, uint16(itr->second.ID));           // 阵营ID
            trans->Append(stmt);

            // 第二步：插入新的声望记录
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_REPUTATION_BY_FACTION);
            stmt->setUInt32(0, _player->GetGUID().GetCounter());  // 玩家GUID
            stmt->setUInt16(1, uint16(itr->second.ID));           // 阵营ID
            stmt->setInt32(2, itr->second.Standing);              // 声望偏移值
            stmt->setUInt16(3, uint16(itr->second.Flags));        // 阵营标志位
            trans->Append(stmt);

            // 重置needSave标志，避免重复保存
            itr->second.needSave = false;
        }
    }
}

/**
 * @brief 更新声望等级计数器
 * @param old_rank 旧的声望等级
 * @param new_rank 新的声望等级
 *
 * 当声望等级变化时，更新相关计数器
 * 这些计数器用于成就系统，追踪玩家的声望成就进度
 *
 * 计数器说明：
 * - _honoredFactionCount: 尊敬及以上阵营数量
 * - _reveredFactionCount: 崇敬及以上阵营数量
 * - _exaltedFactionCount: 崇拜阵营数量
 *
 * 算法逻辑：
 * 1. 从旧等级的计数器中减去
 * 2. 向新等级的计数器中加上
 *
 * 相关成就：
 * - "崇高" - 获得10个崇拜声望
 * - "受人尊敬" - 获得15个崇敬声望
 * - "声望显赫" - 获得30个尊敬声望
 *
 * 性能注意：
 * - 仅在等级变化时调用
 * - 使用简单的条件判断，性能影响极小
 */
void ReputationMgr::UpdateRankCounters(ReputationRank old_rank, ReputationRank new_rank)
{
    // 从旧等级的计数器中减去
    if (old_rank >= REP_EXALTED)
        --_exaltedFactionCount;
    if (old_rank >= REP_REVERED)
        --_reveredFactionCount;
    if (old_rank >= REP_HONORED)
        --_honoredFactionCount;

    // 向新等级的计数器中加上
    if (new_rank >= REP_EXALTED)
        ++_exaltedFactionCount;
    if (new_rank >= REP_REVERED)
        ++_reveredFactionCount;
    if (new_rank >= REP_HONORED)
        ++_honoredFactionCount;
}
