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
 * @file LFGMgr.cpp
 * @brief 地下城查找系统（LFG）管理器实现
 *
 * 本文件实现了TrinityCore的地下城查找系统（Looking For Group）的核心管理逻辑。
 * LFG系统允许玩家通过自动匹配系统快速组成地下城或团队副本队伍。
 *
 * 主要功能模块：
 *
 * 1. 队列管理：
 *    - 玩家和队伍排队进入副本
 *    - 支持单排和组排
 *    - 队列状态管理和更新
 *
 * 2. 角色检查：
 *    - 验证队伍角色配置（1坦克+1治疗+3输出）
 *    - 角色检查超时处理
 *    - 自动角色分配和调整
 *
 * 3. 提案系统：
 *    - 匹配成功后创建提案
 *    - 玩家确认加入
 *    - 提案超时和失败处理
 *
 * 4. 投票踢人：
 *    - LFG队伍内踢人投票
 *    - 投票结果处理
 *    - 踢人冷却时间管理
 *
 * 5. 奖励系统：
 *    - 随机副本完成奖励
 *    - 首次完成和重复完成的不同奖励
 *    - 等级对应的奖励配置
 *
 * 6. 传送功能：
 *    - 进入副本传送
 *    - 离开副本传送
 *    - 传送错误处理
 *
 * 7. 数据持久化：
 *    - 队伍状态保存到数据库
 *    - 断线重连后恢复状态
 *    - 副本进度保存
 *
 * 核心算法：
 * - 队列匹配算法（见LFGQueue.cpp）
 * - 角色兼容性检查
 * - 玩家屏蔽检查
 * - 副本条件验证
 *
 * 性能优化：
 * - 使用缓存减少数据库查询
 * - 定时更新队列状态（每15秒）
 * - 批量处理提案和踢人
 *
 * 线程安全：
 * - 单例模式，主线程运行
 * - 所有操作在主循环中执行
 * - 使用消息队列处理跨线程请求
 *
 * 数据流程：
 * 玩家请求 -> 角色检查 -> 加入队列 -> 匹配算法 -> 创建提案 -> 玩家确认 -> 组建队伍 -> 进入副本
 */

#include "LFGMgr.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "DisableMgr.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "LFGGroupData.h"
#include "LFGPlayerData.h"
#include "LFGScripts.h"
#include "LFGQueue.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"
#include "SocialMgr.h"
#include "World.h"
#include "WorldSession.h"

namespace lfg
{

/**
 * @brief LFGDungeonData 默认构造函数
 *
 * 职责：初始化地下城数据结构，所有成员变量设置为默认值
 */
LFGDungeonData::LFGDungeonData() : id(0), name(), map(0), type(0), expansion(0), group(0), minlevel(0),
    maxlevel(0), difficulty(REGULAR_DIFFICULTY), seasonal(false), x(0.0f), y(0.0f), z(0.0f), o(0.0f)
{
}

/**
 * @brief LFGDungeonData 构造函数（从DBC数据初始化）
 *
 * 职责：从DBC数据初始化地下城数据结构
 * @param dbc LFG地下城DBC条目指针
 */
LFGDungeonData::LFGDungeonData(LFGDungeonEntry const* dbc) : id(dbc->ID), name(dbc->Name[0]), map(dbc->MapID),
    type(dbc->TypeID), expansion(uint8(dbc->ExpansionLevel)), group(uint8(dbc->GroupID)),
    minlevel(uint8(dbc->MinLevel)), maxlevel(uint8(dbc->MaxLevel)), difficulty(Difficulty(dbc->Difficulty)),
    seasonal((dbc->Flags & LFG_FLAG_SEASONAL) != 0), x(0.0f), y(0.0f), z(0.0f), o(0.0f)
{
}

/**
 * @brief LFGMgr 构造函数
 *
 * 职责：初始化地下城查找器管理器，设置队列定时器和配置选项
 */
LFGMgr::LFGMgr(): m_QueueTimer(0), m_lfgProposalId(1),
    m_options(sWorld->getIntConfig(CONFIG_LFG_OPTIONSMASK))
{
}

/**
 * @brief LFGMgr 析构函数
 *
 * 职责：清理奖励映射存储，释放动态分配的奖励对象
 */
LFGMgr::~LFGMgr()
{
    for (LfgRewardContainer::iterator itr = RewardMapStore.begin(); itr != RewardMapStore.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 从数据库加载LFG数据
 *
 * 职责：从数据库字段恢复队伍的LFG状态信息
 *
 * @param fields 数据库字段数组
 * @param guid 队伍GUID
 *
 * 主要流程：
 * 1. 验证输入参数有效性
 * 2. 设置队伍领袖
 * 3. 恢复地下城ID和状态
 */
void LFGMgr::_LoadFromDB(Field* fields, ObjectGuid guid)
{
    if (!fields)
        return;

    if (!guid.IsGroup())
        return;

    SetLeader(guid, ObjectGuid(HighGuid::Player, fields[0].GetUInt32()));

    uint32 dungeon = fields[17].GetUInt32();
    uint8 state = fields[18].GetUInt8();

    if (!dungeon || !state)
        return;

    SetDungeon(guid, dungeon);

    switch (state)
    {
        case LFG_STATE_DUNGEON:
        case LFG_STATE_FINISHED_DUNGEON:
            SetState(guid, (LfgState)state);
            break;
        default:
            break;
    }
}

/**
 * @brief 保存LFG数据到数据库
 *
 * 职责：将队伍的LFG状态信息持久化到数据库
 *
 * @param guid 队伍GUID
 * @param db_guid 数据库中的队伍ID
 *
 * 主要流程：
 * 1. 验证是否为队伍GUID
 * 2. 删除旧数据
 * 3. 插入新的地下城和状态数据
 */
void LFGMgr::_SaveToDB(ObjectGuid guid, uint32 db_guid)
{
    if (!guid.IsGroup())
        return;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_LFG_DATA);
    stmt->setUInt32(0, db_guid);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_LFG_DATA);
    stmt->setUInt32(0, db_guid);
    stmt->setUInt32(1, GetDungeon(guid));
    stmt->setUInt32(2, GetState(guid));
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 加载地下城完成奖励
 *
 * 职责：从数据库加载完成地下城获得的奖励配置
 *
 * 主要流程：
 * 1. 清空现有奖励存储
 * 2. 从 lfg_dungeon_rewards 表读取数据
 * 3. 验证地下城、等级、任务的有效性
 * 4. 存储到奖励映射表
 */
/// Load rewards for completing dungeons
void LFGMgr::LoadRewards()
{
    uint32 oldMSTime = getMSTime();

    for (LfgRewardContainer::iterator itr = RewardMapStore.begin(); itr != RewardMapStore.end(); ++itr)
        delete itr->second;
    RewardMapStore.clear();

    // ORDER BY is very important for GetRandomDungeonReward!
    QueryResult result = WorldDatabase.Query("SELECT dungeonId, maxLevel, firstQuestId, otherQuestId FROM lfg_dungeon_rewards ORDER BY dungeonId, maxLevel ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 lfg dungeon rewards. DB table `lfg_dungeon_rewards` is empty!");
        return;
    }

    uint32 count = 0;

    Field* fields = nullptr;
    do
    {
        fields = result->Fetch();
        uint32 dungeonId = fields[0].GetUInt32();
        uint32 maxLevel = fields[1].GetUInt8();
        uint32 firstQuestId = fields[2].GetUInt32();
        uint32 otherQuestId = fields[3].GetUInt32();

        if (!GetLFGDungeonEntry(dungeonId))
        {
            TC_LOG_ERROR("sql.sql", "Dungeon {} specified in table `lfg_dungeon_rewards` does not exist!", dungeonId);
            continue;
        }

        if (!maxLevel || maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        {
            TC_LOG_ERROR("sql.sql", "Level {} specified for dungeon {} in table `lfg_dungeon_rewards` can never be reached!", maxLevel, dungeonId);
            maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
        }

        if (!firstQuestId || !sObjectMgr->GetQuestTemplate(firstQuestId))
        {
            TC_LOG_ERROR("sql.sql", "First quest {} specified for dungeon {} in table `lfg_dungeon_rewards` does not exist!", firstQuestId, dungeonId);
            continue;
        }

        if (otherQuestId && !sObjectMgr->GetQuestTemplate(otherQuestId))
        {
            TC_LOG_ERROR("sql.sql", "Other quest {} specified for dungeon {} in table `lfg_dungeon_rewards` does not exist!", otherQuestId, dungeonId);
            otherQuestId = 0;
        }

        RewardMapStore.insert(LfgRewardContainer::value_type(dungeonId, new LfgReward(maxLevel, firstQuestId, otherQuestId)));
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} lfg dungeon rewards in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取LFG地下城数据
 *
 * 职责：根据ID获取地下城数据结构
 *
 * @param id 地下城ID
 * @return 地下城数据指针，不存在则返回nullptr
 */
LFGDungeonData const* LFGMgr::GetLFGDungeon(uint32 id)
{
    LFGDungeonContainer::const_iterator itr = LfgDungeonStore.find(id);
    if (itr != LfgDungeonStore.end())
        return &(itr->second);

    return nullptr;
}

/**
 * @brief 加载LFG地下城数据
 *
 * 职责：从DBC和数据库加载地下城配置数据，包括传送坐标
 *
 * @param reload 是否重新加载（默认为false）
 *
 * 主要流程：
 * 1. 从DBC文件加载地下城基础信息
 * 2. 从数据库加载传送坐标
 * 3. 对于没有坐标的地下城，从区域触发器获取入口坐标
 * 4. 构建地下城缓存映射
 */
void LFGMgr::LoadLFGDungeons(bool reload /* = false */)
{
    uint32 oldMSTime = getMSTime();

    LfgDungeonStore.clear();

    // Initialize Dungeon map with data from dbcs
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(i);
        if (!dungeon)
            continue;

        switch (dungeon->TypeID)
        {
            case LFG_TYPE_DUNGEON:
            case LFG_TYPE_HEROIC:
            case LFG_TYPE_RAID:
            case LFG_TYPE_RANDOM:
                LfgDungeonStore[dungeon->ID] = LFGDungeonData(dungeon);
                break;
        }
    }

    // Fill teleport locations from DB
    //                                                   0          1           2           3            4
    QueryResult result = WorldDatabase.Query("SELECT dungeonId, position_x, position_y, position_z, orientation FROM lfg_dungeon_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 lfg entrance positions. DB table `lfg_dungeon_template` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 dungeonId = fields[0].GetUInt32();
        LFGDungeonContainer::iterator dungeonItr = LfgDungeonStore.find(dungeonId);
        if (dungeonItr == LfgDungeonStore.end())
        {
            TC_LOG_ERROR("sql.sql", "table `lfg_dungeon_template` contains coordinates for wrong dungeon {}", dungeonId);
            continue;
        }

        LFGDungeonData& data = dungeonItr->second;
        data.x = fields[1].GetFloat();
        data.y = fields[2].GetFloat();
        data.z = fields[3].GetFloat();
        data.o = fields[4].GetFloat();

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} lfg entrance positions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

    // Fill all other teleport coords from areatriggers
    for (LFGDungeonContainer::iterator itr = LfgDungeonStore.begin(); itr != LfgDungeonStore.end(); ++itr)
    {
        LFGDungeonData& dungeon = itr->second;

        // No teleport coords in database, load from areatriggers
        if (dungeon.type != LFG_TYPE_RANDOM && dungeon.x == 0.0f && dungeon.y == 0.0f && dungeon.z == 0.0f)
        {
            AreaTrigger const* at = sObjectMgr->GetMapEntranceTrigger(dungeon.map);
            if (!at)
            {
                TC_LOG_ERROR("sql.sql", "Failed to load dungeon {}, cant find areatrigger for map {}", dungeon.name, dungeon.map);
                continue;
            }

            dungeon.map = at->target_mapId;
            dungeon.x = at->target_X;
            dungeon.y = at->target_Y;
            dungeon.z = at->target_Z;
            dungeon.o = at->target_Orientation;
        }

        if (dungeon.type != LFG_TYPE_RANDOM)
            CachedDungeonMapStore[dungeon.group].insert(dungeon.id);
        CachedDungeonMapStore[0].insert(dungeon.id);
    }

    if (reload)
    {
        CachedDungeonMapStore.clear();
    }
}

/**
 * @brief 获取LFGMgr单例实例
 *
 * 职责：返回LFGMgr的全局单例对象
 *
 * @return LFGMgr单例指针
 */
LFGMgr* LFGMgr::instance()
{
    static LFGMgr instance;
    return &instance;
}

/**
 * @brief 更新地下城查找器（主循环）
 *
 * 职责：定期更新LFG系统状态，处理超时、匹配和队列更新
 *
 * @param diff 距离上次更新的时间差（毫秒）
 *
 * 主要流程：
 * 1. 检查LFG系统是否启用
 * 2. 清理过期的角色检查
 * 3. 清理过期的提案
 * 4. 清理过期的踢人投票
 * 5. 尝试匹配新队伍
 * 6. 更新队列等待时间
 */
void LFGMgr::Update(uint32 diff)
{
    if (!isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER))
        return;

    time_t currTime = GameTime::GetGameTime();

    // Remove obsolete role checks
    for (LfgRoleCheckContainer::iterator it = RoleChecksStore.begin(); it != RoleChecksStore.end();)
    {
        LfgRoleCheckContainer::iterator itRoleCheck = it++;
        LfgRoleCheck& roleCheck = itRoleCheck->second;
        if (currTime < roleCheck.cancelTime)
            continue;
        roleCheck.state = LFG_ROLECHECK_MISSING_ROLE;

        for (LfgRolesMap::const_iterator itRoles = roleCheck.roles.begin(); itRoles != roleCheck.roles.end(); ++itRoles)
        {
            ObjectGuid guid = itRoles->first;
            RestoreState(guid, "Remove Obsolete RoleCheck");
            SendLfgRoleCheckUpdate(guid, roleCheck);
            if (guid == roleCheck.leader)
                SendLfgJoinResult(guid, LfgJoinResultData(LFG_JOIN_FAILED, LFG_ROLECHECK_MISSING_ROLE));
        }

        RestoreState(itRoleCheck->first, "Remove Obsolete RoleCheck");
        RoleChecksStore.erase(itRoleCheck);
    }

    // Remove obsolete proposals
    for (LfgProposalContainer::iterator it = ProposalsStore.begin(); it != ProposalsStore.end();)
    {
        LfgProposalContainer::iterator itRemove = it++;
        if (itRemove->second.cancelTime < currTime)
            RemoveProposal(itRemove, LFG_UPDATETYPE_PROPOSAL_FAILED);
    }

    // Remove obsolete kicks
    for (LfgPlayerBootContainer::iterator it = BootsStore.begin(); it != BootsStore.end();)
    {
        LfgPlayerBootContainer::iterator itBoot = it++;
        LfgPlayerBoot& boot = itBoot->second;
        if (boot.cancelTime < currTime)
        {
            boot.inProgress = false;
            for (LfgAnswerContainer::const_iterator itVotes = boot.votes.begin(); itVotes != boot.votes.end(); ++itVotes)
            {
                ObjectGuid pguid = itVotes->first;
                if (pguid != boot.victim)
                    SendLfgBootProposalUpdate(pguid, boot);
            }
            SetVoteKick(itBoot->first, false);
            BootsStore.erase(itBoot);
        }
    }

    uint32 lastProposalId = m_lfgProposalId;
    // Check if a proposal can be formed with the new groups being added
    for (LfgQueueContainer::iterator it = QueuesStore.begin(); it != QueuesStore.end(); ++it)
        if (uint8 newProposals = it->second.FindGroups())
            TC_LOG_DEBUG("lfg.update", "Found {} new groups in queue {}", newProposals, it->first);

    if (lastProposalId != m_lfgProposalId)
    {
        // FIXME lastProposalId ? lastProposalId +1 ?
        for (LfgProposalContainer::const_iterator itProposal = ProposalsStore.find(m_lfgProposalId); itProposal != ProposalsStore.end(); ++itProposal)
        {
            uint32 proposalId = itProposal->first;
            LfgProposal& proposal = ProposalsStore[proposalId];

            ObjectGuid guid;
            for (LfgProposalPlayerContainer::const_iterator itPlayers = proposal.players.begin(); itPlayers != proposal.players.end(); ++itPlayers)
            {
                guid = itPlayers->first;
                SetState(guid, LFG_STATE_PROPOSAL);
                if (ObjectGuid gguid = GetGroup(guid))
                {
                    SetState(gguid, LFG_STATE_PROPOSAL);
                    SendLfgUpdateParty(guid, LfgUpdateData(LFG_UPDATETYPE_PROPOSAL_BEGIN, GetSelectedDungeons(guid), GetComment(guid)));
                }
                else
                    SendLfgUpdatePlayer(guid, LfgUpdateData(LFG_UPDATETYPE_PROPOSAL_BEGIN, GetSelectedDungeons(guid), GetComment(guid)));
                SendLfgUpdateProposal(guid, proposal);
            }

            if (proposal.state == LFG_PROPOSAL_SUCCESS)
                UpdateProposal(proposalId, guid, true);
        }
    }

    // Update all players status queue info
    if (m_QueueTimer > LFG_QUEUEUPDATE_INTERVAL)
    {
        m_QueueTimer = 0;
        for (LfgQueueContainer::iterator it = QueuesStore.begin(); it != QueuesStore.end(); ++it)
            it->second.UpdateQueueTimers(currTime);
    }
    else
        m_QueueTimer += diff;
}

/**
 * @brief 加入地下城查找器队列
 *
 * 职责：将玩家或队伍加入LFG队列，进行角色检查和匹配条件验证
 *
 * @param player 尝试加入的玩家（或队伍队长）
 * @param roles 玩家选择的角色（坦克/治疗/输出）
 * @param dungeons 目标地下城集合
 * @param comment 玩家备注信息
 *
 * 主要流程：
 * 1. 验证输入参数和角色有效性
 * 2. 检查玩家/队伍成员的限制条件（逃亡者、冷却时间、战场等）
 * 3. 验证地下城类型和兼容性
 * 4. 对于队伍：启动角色检查流程
 * 5. 对于单人：直接加入队列
 * 6. 发送加入结果通知
 */
void LFGMgr::JoinLfg(Player* player, uint8 roles, LfgDungeonSet& dungeons, const std::string& comment)
{
    if (!player || !player->GetSession() || dungeons.empty())
        return;

    // Sanitize input roles
    roles &= PLAYER_ROLE_ANY;
    roles = FilterClassRoles(player, roles);

    // At least 1 role must be selected
    if (!(roles & (PLAYER_ROLE_TANK | PLAYER_ROLE_HEALER | PLAYER_ROLE_DAMAGE)))
        return;

    Group* grp = player->GetGroup();
    ObjectGuid guid = player->GetGUID();
    ObjectGuid gguid = grp ? grp->GetGUID() : guid;
    LfgJoinResultData joinData;
    GuidSet players;
    uint32 rDungeonId = 0;
    bool isContinue = grp && grp->isLFGGroup() && GetState(gguid) != LFG_STATE_FINISHED_DUNGEON;

    // Do not allow to change dungeon in the middle of a current dungeon
    if (isContinue)
    {
        dungeons.clear();
        dungeons.insert(GetDungeon(gguid));
    }

    // Already in queue?
    LfgState state = GetState(gguid);
    if (state == LFG_STATE_QUEUED)
    {
        LFGQueue& queue = GetQueue(gguid);
        queue.RemoveFromQueue(gguid);
    }

    // Check player or group member restrictions
    if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_JOIN_DUNGEON_FINDER))
        joinData.result = LFG_JOIN_NOT_MEET_REQS;
    else if (player->InBattleground() || player->InArena() || player->InBattlegroundQueue())
        joinData.result = LFG_JOIN_USING_BG_SYSTEM;
    else if (player->HasAura(LFG_SPELL_DUNGEON_DESERTER))
        joinData.result = LFG_JOIN_DESERTER;
    else if (!isContinue && player->HasAura(LFG_SPELL_DUNGEON_COOLDOWN))
        joinData.result = LFG_JOIN_RANDOM_COOLDOWN;
    else if (dungeons.empty())
        joinData.result = LFG_JOIN_NOT_MEET_REQS;
    else if (player->HasAura(9454)) // check Freeze debuff
        joinData.result = LFG_JOIN_NOT_MEET_REQS;
    else if (grp)
    {
        if (grp->GetMembersCount() > MAX_GROUP_SIZE)
            joinData.result = LFG_JOIN_TOO_MUCH_MEMBERS;
        else
        {
            uint8 memberCount = 0;
            for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr && joinData.result == LFG_JOIN_OK; itr = itr->next())
            {
                if (Player* plrg = itr->GetSource())
                {
                    if (!plrg->GetSession()->HasPermission(rbac::RBAC_PERM_JOIN_DUNGEON_FINDER))
                        joinData.result = LFG_JOIN_PARTY_NOT_MEET_REQS;
                    if (plrg->HasAura(LFG_SPELL_DUNGEON_DESERTER))
                        joinData.result = LFG_JOIN_PARTY_DESERTER;
                    else if (!isContinue && plrg->HasAura(LFG_SPELL_DUNGEON_COOLDOWN))
                        joinData.result = LFG_JOIN_PARTY_RANDOM_COOLDOWN;
                    else if (plrg->InBattleground() || plrg->InArena() || plrg->InBattlegroundQueue())
                        joinData.result = LFG_JOIN_USING_BG_SYSTEM;
                    else if (plrg->HasAura(9454)) // check Freeze debuff
                        joinData.result = LFG_JOIN_PARTY_NOT_MEET_REQS;
                    ++memberCount;
                    players.insert(plrg->GetGUID());
                }
            }

            if (joinData.result == LFG_JOIN_OK && memberCount != grp->GetMembersCount())
                joinData.result = LFG_JOIN_DISCONNECTED;
        }
    }
    else
        players.insert(player->GetGUID());

    // Check if all dungeons are valid
    bool isRaid = false;
    if (joinData.result == LFG_JOIN_OK)
    {
        bool isDungeon = false;
        for (LfgDungeonSet::const_iterator it = dungeons.begin(); it != dungeons.end() && joinData.result == LFG_JOIN_OK; ++it)
        {
            LfgType type = GetDungeonType(*it);
            switch (type)
            {
                case LFG_TYPE_RANDOM:
                    if (dungeons.size() > 1)               // Only allow 1 random dungeon
                        joinData.result = LFG_JOIN_DUNGEON_INVALID;
                    else
                        rDungeonId = (*dungeons.begin());
                    [[fallthrough]]; // Random can only be dungeon or heroic dungeon
                case LFG_TYPE_HEROIC:
                case LFG_TYPE_DUNGEON:
                    if (isRaid)
                        joinData.result = LFG_JOIN_MIXED_RAID_DUNGEON;
                    isDungeon = true;
                    break;
                case LFG_TYPE_RAID:
                    if (isDungeon)
                        joinData.result = LFG_JOIN_MIXED_RAID_DUNGEON;
                    isRaid = true;
                    break;
                default:
                    joinData.result = LFG_JOIN_DUNGEON_INVALID;
                    break;
            }
        }

        // it could be changed
        if (joinData.result == LFG_JOIN_OK)
        {
            // Expand random dungeons and check restrictions
            if (rDungeonId)
                dungeons = GetDungeonsByRandom(rDungeonId);

            // if we have lockmap then there are no compatible dungeons
            GetCompatibleDungeons(dungeons, players, joinData.lockmap, isContinue);
            if (dungeons.empty())
                joinData.result = grp ? LFG_JOIN_PARTY_NOT_MEET_REQS : LFG_JOIN_NOT_MEET_REQS;
        }
    }

    // Can't join. Send result
    if (joinData.result != LFG_JOIN_OK)
    {
        TC_LOG_DEBUG("lfg.join", "{} joining with {} members. Result: {}, Dungeons: {}",
            guid.ToString(), grp ? grp->GetMembersCount() : 1, joinData.result, ConcatenateDungeons(dungeons));

        if (!dungeons.empty())                             // Only should show lockmap when have no dungeons available
            joinData.lockmap.clear();
        player->GetSession()->SendLfgJoinResult(joinData);
        return;
    }

    SetComment(guid, comment);

    if (isRaid)
    {
        TC_LOG_DEBUG("lfg.join", "{} trying to join raid browser and it's disabled.", guid.ToString());
        return;
    }

    std::string debugNames = "";
    if (grp)                                               // Begin rolecheck
    {
        // Create new rolecheck
        LfgRoleCheck& roleCheck = RoleChecksStore[gguid];
        roleCheck.cancelTime = GameTime::GetGameTime() + LFG_TIME_ROLECHECK;
        roleCheck.state = LFG_ROLECHECK_INITIALITING;
        roleCheck.leader = guid;
        roleCheck.dungeons = dungeons;
        roleCheck.rDungeonId = rDungeonId;

        if (rDungeonId)
        {
            dungeons.clear();
            dungeons.insert(rDungeonId);
        }

        SetState(gguid, LFG_STATE_ROLECHECK);
        // Send update to player
        LfgUpdateData updateData = LfgUpdateData(LFG_UPDATETYPE_JOIN_QUEUE, dungeons, comment);
        for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* plrg = itr->GetSource())
            {
                ObjectGuid pguid = plrg->GetGUID();
                plrg->GetSession()->SendLfgUpdateParty(updateData);
                SetState(pguid, LFG_STATE_ROLECHECK);
                if (!isContinue)
                    SetSelectedDungeons(pguid, dungeons);
                roleCheck.roles[pguid] = 0;
                if (!debugNames.empty())
                    debugNames.append(", ");
                debugNames.append(plrg->GetName());
            }
        }
        // Update leader role
        UpdateRoleCheck(gguid, guid, roles);
    }
    else                                                   // Add player to queue
    {
        LfgRolesMap rolesMap;
        rolesMap[guid] = roles;
        LFGQueue& queue = GetQueue(guid);
        queue.AddQueueData(guid, GameTime::GetGameTime(), dungeons, rolesMap);

        if (!isContinue)
        {
            if (rDungeonId)
            {
                dungeons.clear();
                dungeons.insert(rDungeonId);
            }
            SetSelectedDungeons(guid, dungeons);
        }
        // Send update to player
        player->GetSession()->SendLfgJoinResult(joinData);
        player->GetSession()->SendLfgUpdatePlayer(LfgUpdateData(LFG_UPDATETYPE_JOIN_QUEUE, dungeons, comment));
        SetState(gguid, LFG_STATE_QUEUED);
        SetRoles(guid, roles);
        debugNames.append(player->GetName());
    }

    TC_LOG_DEBUG("lfg.join", "{} joined ({}), Members: {}. Dungeons ({}): {}", guid.ToString(),
        grp ? "group" : "player", debugNames, uint32(dungeons.size()), ConcatenateDungeons(dungeons));
}

/**
 * @brief 离开地下城查找器系统
 *
 * 职责：将玩家或队伍从队列、角色检查、提案或踢人投票中移除
 *
 * @param guid 玩家或队伍GUID
 * @param disconnected 是否因断开连接而离开（默认false）
 *
 * 主要流程：
 * 1. 获取当前LFG状态
 * 2. 根据状态处理不同情况：
 *    - 队列中：从队列移除，恢复之前的状态
 *    - 角色检查中：中止角色检查
 *    - 提案中：标记为拒绝，移除提案
 *    - 地下城中：更新状态为NONE
 * 3. 发送更新通知给相关玩家
 */
void LFGMgr::LeaveLfg(ObjectGuid guid, bool disconnected)
{
    ObjectGuid gguid = guid.IsGroup() ? guid : GetGroup(guid);

    TC_LOG_DEBUG("lfg.leave", "{} left ({})", guid.ToString(), guid == gguid ? "group" : "player");

    LfgState state = GetState(guid);
    switch (state)
    {
        case LFG_STATE_QUEUED:
            if (gguid)
            {
                LfgState newState = LFG_STATE_NONE;
                LfgState oldState = GetOldState(gguid);

                // Set the new state to LFG_STATE_DUNGEON/LFG_STATE_FINISHED_DUNGEON if the group is already in a dungeon
                // This is required in case a LFG group vote-kicks a player in a dungeon, queues, then leaves the queue (maybe to queue later again)
                if (Group* group = sGroupMgr->GetGroupByGUID(gguid.GetCounter()))
                    if (group->isLFGGroup() && GetDungeon(gguid) && (oldState == LFG_STATE_DUNGEON || oldState == LFG_STATE_FINISHED_DUNGEON))
                        newState = oldState;

                LFGQueue& queue = GetQueue(gguid);
                queue.RemoveFromQueue(gguid);
                SetState(gguid, newState);
                GuidSet const& players = GetPlayers(gguid);
                for (GuidSet::const_iterator it = players.begin(); it != players.end(); ++it)
                {
                    SetState(*it, newState);
                    SendLfgUpdateParty(*it, LfgUpdateData(LFG_UPDATETYPE_REMOVED_FROM_QUEUE));
                }
            }
            else
            {
                LFGQueue& queue = GetQueue(guid);
                queue.RemoveFromQueue(guid);
                SendLfgUpdatePlayer(guid, LfgUpdateData(LFG_UPDATETYPE_REMOVED_FROM_QUEUE));
                SetState(guid, LFG_STATE_NONE);
            }
            break;
        case LFG_STATE_ROLECHECK:
            if (gguid)
                UpdateRoleCheck(gguid);                    // No player to update role = LFG_ROLECHECK_ABORTED
            break;
        case LFG_STATE_PROPOSAL:
        {
            // Remove from Proposals
            LfgProposalContainer::iterator it = ProposalsStore.begin();
            ObjectGuid pguid = gguid == guid ? GetLeader(gguid) : guid;
            while (it != ProposalsStore.end())
            {
                LfgProposalPlayerContainer::iterator itPlayer = it->second.players.find(pguid);
                if (itPlayer != it->second.players.end())
                {
                    // Mark the player/leader of group who left as didn't accept the proposal
                    itPlayer->second.accept = LFG_ANSWER_DENY;
                    break;
                }
                ++it;
            }

            // Remove from queue - if proposal is found, RemoveProposal will call RemoveFromQueue
            if (it != ProposalsStore.end())
                RemoveProposal(it, LFG_UPDATETYPE_PROPOSAL_DECLINED);
            break;
        }
        case LFG_STATE_NONE:
        case LFG_STATE_RAIDBROWSER:
            break;
        case LFG_STATE_DUNGEON:
        case LFG_STATE_FINISHED_DUNGEON:
            if (guid != gguid && !disconnected) // Player
                SetState(guid, LFG_STATE_NONE);
            break;
    }
}

/**
 * @brief 更新角色检查
 *
 * 职责：更新队伍角色检查状态，验证角色分配是否合理
 *
 * @param gguid 队伍GUID
 * @param guid 玩家GUID（为空表示角色检查失败）
 * @param roles 玩家选择的角色（默认为无）
 *
 * 主要流程：
 * 1. 验证队伍和角色检查数据存在
 * 2. 过滤无效角色（根据职业限制）
 * 3. 更新玩家的角色选择
 * 4. 检查是否所有玩家都已选择角色
 * 5. 验证角色组合是否满足队伍需求（1坦克1治疗3输出）
 * 6. 角色检查完成后将队伍加入队列
 */
void LFGMgr::UpdateRoleCheck(ObjectGuid gguid, ObjectGuid guid /* = ObjectGuid::Empty */, uint8 roles /* = PLAYER_ROLE_NONE */)
{
    if (!gguid)
        return;

    LfgRolesMap check_roles;
    LfgRoleCheckContainer::iterator itRoleCheck = RoleChecksStore.find(gguid);
    if (itRoleCheck == RoleChecksStore.end())
        return;

    // Sanitize input roles
    roles &= PLAYER_ROLE_ANY;

    if (guid)
    {
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            roles = FilterClassRoles(player, roles);
        else
            return;
    }

    LfgRoleCheck& roleCheck = itRoleCheck->second;
    bool sendRoleChosen = roleCheck.state != LFG_ROLECHECK_DEFAULT && guid;

    if (!guid)
        roleCheck.state = LFG_ROLECHECK_ABORTED;
    else if (roles < PLAYER_ROLE_TANK)                            // Player selected no role.
        roleCheck.state = LFG_ROLECHECK_NO_ROLE;
    else
    {
        roleCheck.roles[guid] = roles;

        // Check if all players have selected a role
        LfgRolesMap::const_iterator itRoles = roleCheck.roles.begin();
        while (itRoles != roleCheck.roles.end() && itRoles->second != PLAYER_ROLE_NONE)
            ++itRoles;

        if (itRoles == roleCheck.roles.end())
        {
            // use temporal var to check roles, CheckGroupRoles modifies the roles
            check_roles = roleCheck.roles;
            roleCheck.state = CheckGroupRoles(check_roles) ? LFG_ROLECHECK_FINISHED : LFG_ROLECHECK_WRONG_ROLES;
        }
    }

    LfgDungeonSet dungeons;
    if (roleCheck.rDungeonId)
        dungeons.insert(roleCheck.rDungeonId);
    else
        dungeons = roleCheck.dungeons;

    LfgJoinResultData joinData = LfgJoinResultData(LFG_JOIN_FAILED, roleCheck.state);
    for (LfgRolesMap::const_iterator it = roleCheck.roles.begin(); it != roleCheck.roles.end(); ++it)
    {
        ObjectGuid pguid = it->first;

        if (sendRoleChosen)
            SendLfgRoleChosen(pguid, guid, roles);

        SendLfgRoleCheckUpdate(pguid, roleCheck);
        switch (roleCheck.state)
        {
            case LFG_ROLECHECK_INITIALITING:
                continue;
            case LFG_ROLECHECK_FINISHED:
                SetState(pguid, LFG_STATE_QUEUED);
                SetRoles(pguid, it->second);
                SendLfgUpdateParty(pguid, LfgUpdateData(LFG_UPDATETYPE_ADDED_TO_QUEUE, dungeons, GetComment(pguid)));
                break;
            default:
                if (roleCheck.leader == pguid)
                    SendLfgJoinResult(pguid, joinData);
                SendLfgUpdateParty(pguid, LfgUpdateData(LFG_UPDATETYPE_ROLECHECK_FAILED));
                RestoreState(pguid, "Rolecheck Failed");
                break;
        }
    }

    if (roleCheck.state == LFG_ROLECHECK_FINISHED)
    {
        SetState(gguid, LFG_STATE_QUEUED);
        LFGQueue& queue = GetQueue(gguid);
        queue.AddQueueData(gguid, GameTime::GetGameTime(), roleCheck.dungeons, roleCheck.roles);
        RoleChecksStore.erase(itRoleCheck);
    }
    else if (roleCheck.state != LFG_ROLECHECK_INITIALITING)
    {
        RestoreState(gguid, "Rolecheck Failed");
        RoleChecksStore.erase(itRoleCheck);
    }
}

/**
 * @brief 获取兼容的地下城列表
 *
 * 职责：从给定地下城列表中移除玩家有限制的地下城
 *
 * @param dungeons 地下城集合（输入输出参数）
 * @param players 玩家集合
 * @param lockMap 锁定状态映射（输出参数）
 * @param isContinue 是否继续进行中的副本
 *
 * 主要流程：
 * 1. 遍历所有玩家
 * 2. 获取每个玩家被锁定的地下城列表
 * 3. 从可用地下城集合中移除被锁定的地下城
 * 4. 处理特殊情况：继续进行中的锁定副本
 */
void LFGMgr::GetCompatibleDungeons(LfgDungeonSet& dungeons, GuidSet const& players, LfgLockPartyMap& lockMap, bool isContinue)
{
    lockMap.clear();

    std::map<uint32, uint32> lockedDungeons;

    for (GuidSet::const_iterator it = players.begin(); it != players.end() && !dungeons.empty(); ++it)
    {
        ObjectGuid guid = (*it);
        LfgLockMap const& cachedLockMap = GetLockedDungeons(guid);
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        for (LfgLockMap::const_iterator it2 = cachedLockMap.begin(); it2 != cachedLockMap.end() && !dungeons.empty(); ++it2)
        {
            uint32 dungeonId = (it2->first & 0x00FFFFFF); // Compare dungeon ids
            LfgDungeonSet::iterator itDungeon = dungeons.find(dungeonId);
            if (itDungeon != dungeons.end())
            {
                bool eraseDungeon = true;

                // Don't remove the dungeon if team members are trying to continue a locked instance
                if (it2->second == LFG_LOCKSTATUS_RAID_LOCKED && isContinue)
                {
                    LFGDungeonData const* dungeon = GetLFGDungeon(dungeonId);
                    ASSERT(dungeon);
                    ASSERT(player);
                    if (InstancePlayerBind* playerBind = player->GetBoundInstance(dungeon->map, Difficulty(dungeon->difficulty)))
                    {
                        if (InstanceSave* playerSave = playerBind->save)
                        {
                            uint32 dungeonInstanceId = playerSave->GetInstanceId();
                            auto itLockedDungeon = lockedDungeons.find(dungeonId);
                            if (itLockedDungeon == lockedDungeons.end() || itLockedDungeon->second == dungeonInstanceId)
                                eraseDungeon = false;

                            lockedDungeons[dungeonId] = dungeonInstanceId;
                        }
                    }
                }

                if (eraseDungeon)
                    dungeons.erase(itDungeon);

                lockMap[guid][dungeonId] = it2->second;
            }
        }
    }
    if (!dungeons.empty())
        lockMap.clear();
}

/**
 * @brief 检查队伍角色是否合理
 *
 * 职责：验证角色分配是否满足标准队伍配置（1坦克1治疗3输出）
 *
 * @param groles 角色映射表
 * @return 角色是否兼容
 *
 * 主要流程：
 * 1. 遍历所有玩家角色
 * 2. 统计坦克、治疗、输出数量
 * 3. 处理多角色玩家（优先尝试特定角色）
 * 4. 递归检查是否存在有效的角色组合
 */
bool LFGMgr::CheckGroupRoles(LfgRolesMap& groles)
{
    if (groles.empty())
        return false;

    uint8 damage = 0;
    uint8 tank = 0;
    uint8 healer = 0;

    for (LfgRolesMap::iterator it = groles.begin(); it != groles.end(); ++it)
    {
        uint8 role = it->second & ~PLAYER_ROLE_LEADER;
        if (role == PLAYER_ROLE_NONE)
            return false;

        if (role & PLAYER_ROLE_DAMAGE)
        {
            if (role != PLAYER_ROLE_DAMAGE)
            {
                it->second -= PLAYER_ROLE_DAMAGE;
                if (CheckGroupRoles(groles))
                    return true;
                it->second += PLAYER_ROLE_DAMAGE;
            }
            else if (damage == LFG_DPS_NEEDED)
                return false;
            else
                damage++;
        }

        if (role & PLAYER_ROLE_HEALER)
        {
            if (role != PLAYER_ROLE_HEALER)
            {
                it->second -= PLAYER_ROLE_HEALER;
                if (CheckGroupRoles(groles))
                    return true;
                it->second += PLAYER_ROLE_HEALER;
            }
            else if (healer == LFG_HEALERS_NEEDED)
                return false;
            else
                healer++;
        }

        if (role & PLAYER_ROLE_TANK)
        {
            if (role != PLAYER_ROLE_TANK)
            {
                it->second -= PLAYER_ROLE_TANK;
                if (CheckGroupRoles(groles))
                    return true;
                it->second += PLAYER_ROLE_TANK;
            }
            else if (tank == LFG_TANKS_NEEDED)
                return false;
            else
                tank++;
        }
    }
    return (tank + healer + damage) == uint8(groles.size());
}

/**
 * @brief 根据提案创建新队伍
 *
 * 职责：根据提案信息组建队伍，设置角色并传送玩家
 *
 * @param proposal 提案信息
 *
 * 主要流程：
 * 1. 按角色对玩家排序（队长->坦克->治疗->输出）
 * 2. 确定需要传送的玩家
 * 3. 创建或复用队伍
 * 4. 添加玩家到队伍
 * 5. 设置地下城难度和状态
 * 6. 添加随机副本冷却时间
 * 7. 传送玩家到地下城
 */
void LFGMgr::MakeNewGroup(LfgProposal const& proposal)
{
    GuidList players, tankPlayers, healPlayers, dpsPlayers;
    GuidList playersToTeleport;

    for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        ObjectGuid guid = it->first;
        if (guid == proposal.leader)
            players.push_back(guid);
        else
            switch (it->second.role & ~PLAYER_ROLE_LEADER)
            {
                case PLAYER_ROLE_TANK:
                    tankPlayers.push_back(guid);
                    break;
                case PLAYER_ROLE_HEALER:
                    healPlayers.push_back(guid);
                    break;
                case PLAYER_ROLE_DAMAGE:
                    dpsPlayers.push_back(guid);
                    break;
                default:
                    ABORT_MSG("Invalid LFG role %u", it->second.role);
                    break;
            }

        if (proposal.isNew || GetGroup(guid) != proposal.group)
            playersToTeleport.push_back(guid);
    }

    players.splice(players.end(), tankPlayers);
    players.splice(players.end(), healPlayers);
    players.splice(players.end(), dpsPlayers);

    // Set the dungeon difficulty
    LFGDungeonData const* dungeon = GetLFGDungeon(proposal.dungeonId);
    ASSERT(dungeon);

    Group* grp = proposal.group ? sGroupMgr->GetGroupByGUID(proposal.group.GetCounter()) : nullptr;
    for (GuidList::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        ObjectGuid pguid = (*it);
        Player* player = ObjectAccessor::FindConnectedPlayer(pguid);
        if (!player)
            continue;

        Group* group = player->GetGroup();
        if (group && group != grp)
            group->RemoveMember(player->GetGUID());

        if (!grp)
        {
            grp = new Group();
            grp->ConvertToLFG();
            grp->Create(player);
            ObjectGuid gguid = grp->GetGUID();
            SetState(gguid, LFG_STATE_PROPOSAL);
            sGroupMgr->AddGroup(grp);
        }
        else if (group != grp)
            grp->AddMember(player);

        grp->SetLfgRoles(pguid, proposal.players.find(pguid)->second.role);

        // Add the cooldown spell if queued for a random dungeon
        const LfgDungeonSet& dungeons = GetSelectedDungeons(player->GetGUID());
        if (!dungeons.empty())
        {
            uint32 rDungeonId = (*dungeons.begin());
            LFGDungeonEntry const* dungeonEntry = sLFGDungeonStore.LookupEntry(rDungeonId);
            if (dungeonEntry && dungeonEntry->TypeID == LFG_TYPE_RANDOM)
                player->CastSpell(player, LFG_SPELL_DUNGEON_COOLDOWN, false);
        }
    }

    ASSERT(grp);
    grp->SetDungeonDifficulty(Difficulty(dungeon->difficulty));
    ObjectGuid gguid = grp->GetGUID();
    SetDungeon(gguid, dungeon->Entry());
    SetState(gguid, LFG_STATE_DUNGEON);

    _SaveToDB(gguid, grp->GetDbStoreId());

    // Teleport Player
    for (GuidList::const_iterator it = playersToTeleport.begin(); it != playersToTeleport.end(); ++it)
        if (Player* player = ObjectAccessor::FindPlayer(*it))
            TeleportPlayer(player, false);

    // Update group info
    grp->SendUpdate();
}

/**
 * @brief 添加新提案
 *
 * 职责：创建新的组队提案并分配唯一ID
 *
 * @param proposal 提案信息（引用）
 * @return 提案ID
 */
uint32 LFGMgr::AddProposal(LfgProposal& proposal)
{
    proposal.id = ++m_lfgProposalId;
    ProposalsStore[m_lfgProposalId] = proposal;
    return m_lfgProposalId;
}

/**
 * @brief 更新提案状态
 *
 * 职责：更新玩家对提案的响应，判断是否所有玩家都已接受
 *
 * @param proposalId 提案ID
 * @param guid 玩家GUID
 * @param accept 是否接受
 *
 * 主要流程：
 * 1. 验证提案和玩家存在
 * 2. 记录玩家响应
 * 3. 如果拒绝：移除提案
 * 4. 如果所有人都接受：组建队伍
 * 5. 更新等待时间统计
 */
void LFGMgr::UpdateProposal(uint32 proposalId, ObjectGuid guid, bool accept)
{
    // Check if the proposal exists
    LfgProposalContainer::iterator itProposal = ProposalsStore.find(proposalId);
    if (itProposal == ProposalsStore.end())
        return;

    LfgProposal& proposal = itProposal->second;

    // Check if proposal have the current player
    LfgProposalPlayerContainer::iterator itProposalPlayer = proposal.players.find(guid);
    if (itProposalPlayer == proposal.players.end())
        return;

    LfgProposalPlayer& player = itProposalPlayer->second;
    player.accept = LfgAnswer(accept);

    TC_LOG_DEBUG("lfg.proposal.update", "{}, Proposal {}, Selection: {}", guid.ToString(), proposalId, accept);
    if (!accept)
    {
        RemoveProposal(itProposal, LFG_UPDATETYPE_PROPOSAL_DECLINED);
        return;
    }

    // check if all have answered and reorder players (leader first)
    bool allAnswered = true;
    for (LfgProposalPlayerContainer::const_iterator itPlayers = proposal.players.begin(); itPlayers != proposal.players.end(); ++itPlayers)
        if (itPlayers->second.accept != LFG_ANSWER_AGREE)   // No answer (-1) or not accepted (0)
            allAnswered = false;

    if (!allAnswered)
    {
        for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
            SendLfgUpdateProposal(it->first, proposal);

        return;
    }

    bool sendUpdate = proposal.state != LFG_PROPOSAL_SUCCESS;
    proposal.state = LFG_PROPOSAL_SUCCESS;
    time_t joinTime = GameTime::GetGameTime();

    LFGQueue& queue = GetQueue(guid);
    LfgUpdateData updateData = LfgUpdateData(LFG_UPDATETYPE_GROUP_FOUND);
    for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        ObjectGuid pguid = it->first;
        ObjectGuid gguid = it->second.group;
        uint32 dungeonId = (*GetSelectedDungeons(pguid).begin());
        int32 waitTime = -1;
        if (sendUpdate)
           SendLfgUpdateProposal(pguid, proposal);

        if (gguid)
        {
            waitTime = int32((joinTime - queue.GetJoinTime(gguid)) / IN_MILLISECONDS);
            SendLfgUpdateParty(pguid, updateData);
        }
        else
        {
            waitTime = int32((joinTime - queue.GetJoinTime(pguid)) / IN_MILLISECONDS);
            SendLfgUpdatePlayer(pguid, updateData);
        }
        updateData.updateType = LFG_UPDATETYPE_REMOVED_FROM_QUEUE;
        SendLfgUpdatePlayer(pguid, updateData);
        SendLfgUpdateParty(pguid, updateData);

        // Update timers
        uint8 role = GetRoles(pguid);
        role &= ~PLAYER_ROLE_LEADER;
        switch (role)
        {
            case PLAYER_ROLE_DAMAGE:
                queue.UpdateWaitTimeDps(waitTime, dungeonId);
                break;
            case PLAYER_ROLE_HEALER:
                queue.UpdateWaitTimeHealer(waitTime, dungeonId);
                break;
            case PLAYER_ROLE_TANK:
                queue.UpdateWaitTimeTank(waitTime, dungeonId);
                break;
            default:
                queue.UpdateWaitTimeAvg(waitTime, dungeonId);
                break;
        }

        // Store the number of players that were present in group when joining RFD, used for achievement purposes
        if (Player* player = ObjectAccessor::FindConnectedPlayer(pguid))
            if (Group* group = player->GetGroup())
                PlayersStore[pguid].SetNumberOfPartyMembersAtJoin(group->GetMembersCount());

        SetState(pguid, LFG_STATE_DUNGEON);
    }

    // Remove players/groups from Queue
    for (GuidList::const_iterator it = proposal.queues.begin(); it != proposal.queues.end(); ++it)
        queue.RemoveFromQueue(*it);

    MakeNewGroup(proposal);
    ProposalsStore.erase(itProposal);
}

/**
 * @brief 移除提案
 *
 * 职责：移除提案，处理拒绝的玩家/队伍，将其他成员重新加入队列
 *
 * @param itProposal 提案迭代器
 * @param type 移除类型（失败/拒绝）
 *
 * 主要流程：
 * 1. 标记未响应的玩家为拒绝
 * 2. 确定需要移除的玩家/队伍
 * 3. 通知所有玩家结果
 * 4. 从队列中移除拒绝的玩家/队伍
 * 5. 将接受的玩家/队伍重新加入队列
 */
void LFGMgr::RemoveProposal(LfgProposalContainer::iterator itProposal, LfgUpdateType type)
{
    LfgProposal& proposal = itProposal->second;
    proposal.state = LFG_PROPOSAL_FAILED;

    TC_LOG_DEBUG("lfg.proposal.remove", "Proposal {}, state FAILED, UpdateType {}", itProposal->first, type);
    // Mark all people that didn't answered as no accept
    if (type == LFG_UPDATETYPE_PROPOSAL_FAILED)
        for (LfgProposalPlayerContainer::iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
            if (it->second.accept == LFG_ANSWER_PENDING)
                it->second.accept = LFG_ANSWER_DENY;

    // Mark players/groups to be removed
    GuidSet toRemove;
    for (LfgProposalPlayerContainer::iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        if (it->second.accept == LFG_ANSWER_AGREE)
            continue;

        ObjectGuid guid = it->second.group ? it->second.group : it->first;
        // Player didn't accept or still pending when no secs left
        if (it->second.accept == LFG_ANSWER_DENY || type == LFG_UPDATETYPE_PROPOSAL_FAILED)
        {
            it->second.accept = LFG_ANSWER_DENY;
            toRemove.insert(guid);
        }
    }

    // Notify players
    for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        ObjectGuid guid = it->first;
        ObjectGuid gguid = it->second.group ? it->second.group : guid;

        SendLfgUpdateProposal(guid, proposal);

        if (toRemove.find(gguid) != toRemove.end())         // Didn't accept or in same group that someone that didn't accept
        {
            LfgUpdateData updateData;
            if (it->second.accept == LFG_ANSWER_DENY)
            {
                updateData.updateType = type;
                TC_LOG_DEBUG("lfg.proposal.remove", "{} didn't accept. Removing from queue and compatible cache", guid.ToString());
            }
            else
            {
                updateData.updateType = LFG_UPDATETYPE_REMOVED_FROM_QUEUE;
                TC_LOG_DEBUG("lfg.proposal.remove", "{} in same group that someone that didn't accept. Removing from queue and compatible cache", guid.ToString());
            }

            RestoreState(guid, "Proposal Fail (didn't accepted or in group with someone that didn't accept");
            if (gguid != guid)
            {
                RestoreState(it->second.group, "Proposal Fail (someone in group didn't accepted)");
                SendLfgUpdateParty(guid, updateData);
            }
            else
                SendLfgUpdatePlayer(guid, updateData);
        }
        else
        {
            TC_LOG_DEBUG("lfg.proposal.remove", "Readding {} to queue.", guid.ToString());
            SetState(guid, LFG_STATE_QUEUED);
            if (gguid != guid)
            {
                SetState(gguid, LFG_STATE_QUEUED);
                SendLfgUpdateParty(guid, LfgUpdateData(LFG_UPDATETYPE_ADDED_TO_QUEUE, GetSelectedDungeons(guid), GetComment(guid)));
            }
            else
                SendLfgUpdatePlayer(guid, LfgUpdateData(LFG_UPDATETYPE_ADDED_TO_QUEUE, GetSelectedDungeons(guid), GetComment(guid)));
        }
    }

    LFGQueue& queue = GetQueue(proposal.players.begin()->first);
    // Remove players/groups from queue
    for (GuidSet::const_iterator it = toRemove.begin(); it != toRemove.end(); ++it)
    {
        ObjectGuid guid = *it;
        queue.RemoveFromQueue(guid);
        proposal.queues.remove(guid);
    }

    // Readd to queue
    for (GuidList::const_iterator it = proposal.queues.begin(); it != proposal.queues.end(); ++it)
    {
        ObjectGuid guid = *it;
        queue.AddToQueue(guid, true);
    }

    ProposalsStore.erase(itProposal);
}

/**
 * @brief 初始化踢人投票
 *
 * 职责：启动对某玩家的踢人投票流程
 *
 * @param gguid 队伍GUID
 * @param kicker 发起踢人的玩家GUID
 * @param victim 被踢的玩家GUID
 * @param reason 踢人原因
 *
 * 主要流程：
 * 1. 设置踢人投票为活动状态
 * 2. 初始化投票数据结构
 * 3. 设置投票超时时间
 * 4. 初始化所有玩家的投票状态为待定
 * 5. 被踢者自动投票反对，发起者自动投票赞成
 * 6. 通知所有玩家踢人投票
 */
void LFGMgr::InitBoot(ObjectGuid gguid, ObjectGuid kicker, ObjectGuid victim, std::string const& reason)
{
    SetVoteKick(gguid, true);

    LfgPlayerBoot& boot = BootsStore[gguid];
    boot.inProgress = true;
    boot.cancelTime = GameTime::GetGameTime() + LFG_TIME_BOOT;
    boot.reason = reason;
    boot.victim = victim;

    GuidSet const& players = GetPlayers(gguid);

    // Set votes
    for (GuidSet::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        ObjectGuid guid = (*itr);
        boot.votes[guid] = LFG_ANSWER_PENDING;
    }

    boot.votes[victim] = LFG_ANSWER_DENY;                  // Victim auto vote NO
    boot.votes[kicker] = LFG_ANSWER_AGREE;                 // Kicker auto vote YES

    // Notify players
    for (GuidSet::const_iterator it = players.begin(); it != players.end(); ++it)
        SendLfgBootProposalUpdate(*it, boot);
}

/**
 * @brief 更新踢人投票
 *
 * 职责：记录玩家对踢人投票的响应，判断投票结果
 *
 * @param guid 玩家GUID
 * @param accept 是否同意踢人
 *
 * 主要流程：
 * 1. 获取队伍和踢人投票数据
 * 2. 验证玩家是否已投票
 * 3. 记录投票结果
 * 4. 统计赞成和反对票数
 * 5. 如果达到所需票数：
 *    - 赞成票足够：踢出玩家
 *    - 反对票过多：投票失败
 * 6. 通知所有玩家结果
 */
void LFGMgr::UpdateBoot(ObjectGuid guid, bool accept)
{
    ObjectGuid gguid = GetGroup(guid);
    if (!gguid)
        return;

    LfgPlayerBootContainer::iterator itBoot = BootsStore.find(gguid);
    if (itBoot == BootsStore.end())
        return;

    LfgPlayerBoot& boot = itBoot->second;

    if (boot.votes[guid] != LFG_ANSWER_PENDING)    // Cheat check: Player can't vote twice
        return;

    boot.votes[guid] = LfgAnswer(accept);

    uint8 agreeNum = 0;
    uint8 denyNum = 0;
    for (LfgAnswerContainer::const_iterator itVotes = boot.votes.begin(); itVotes != boot.votes.end(); ++itVotes)
    {
        switch (itVotes->second)
        {
            case LFG_ANSWER_PENDING:
                break;
            case LFG_ANSWER_AGREE:
                ++agreeNum;
                break;
            case LFG_ANSWER_DENY:
                ++denyNum;
                break;
        }
    }

    // if we don't have enough votes (agree or deny) do nothing
    if (agreeNum < LFG_GROUP_KICK_VOTES_NEEDED && (boot.votes.size() - denyNum) >= LFG_GROUP_KICK_VOTES_NEEDED)
        return;

    // Send update info to all players
    boot.inProgress = false;
    for (LfgAnswerContainer::const_iterator itVotes = boot.votes.begin(); itVotes != boot.votes.end(); ++itVotes)
    {
        ObjectGuid pguid = itVotes->first;
        if (pguid != boot.victim)
            SendLfgBootProposalUpdate(pguid, boot);
    }

    SetVoteKick(gguid, false);
    if (agreeNum == LFG_GROUP_KICK_VOTES_NEEDED)           // Vote passed - Kick player
    {
        if (Group* group = sGroupMgr->GetGroupByGUID(gguid.GetCounter()))
            Player::RemoveFromGroup(group, boot.victim, GROUP_REMOVEMETHOD_KICK_LFG);
        DecreaseKicksLeft(gguid);
    }
    BootsStore.erase(itBoot);
}

/**
 * @brief 传送玩家进出地下城
 *
 * 职责：将玩家传送进入或离开LFG地下城
 *
 * @param player 玩家对象
 * @param out 是否传送出地下城（true为出，false为进）
 * @param fromOpcode 是否由操作码处理器调用（默认false）
 *
 * 主要流程：
 * 1. 获取地下城数据
 * 2. 传送出：将玩家传送回进入点
 * 3. 传送进：
 *    - 检查传送条件（存活、不在坠落、无疲劳等）
 *    - 如果有队友已在副本内，传送到队友位置
 *    - 否则传送到地下城入口
 * 4. 发送传送错误信息（如果有错误）
 */
void LFGMgr::TeleportPlayer(Player* player, bool out, bool fromOpcode /*= false*/)
{
    LFGDungeonData const* dungeon = nullptr;
    Group* group = player->GetGroup();

    if (group && group->isLFGGroup())
        dungeon = GetLFGDungeon(GetDungeon(group->GetGUID()));

    if (!dungeon)
    {
        TC_LOG_DEBUG("lfg.teleport", "Player {} not in group/lfggroup or dungeon not found!",
            player->GetName());
        player->GetSession()->SendLfgTeleportError(uint8(LFG_TELEPORTERROR_INVALID_LOCATION));
        return;
    }

    if (out)
    {
        TC_LOG_DEBUG("lfg.teleport", "Player {} is being teleported out. Current Map {} - Expected Map {}",
            player->GetName(), player->GetMapId(), uint32(dungeon->map));
        if (player->GetMapId() == uint32(dungeon->map))
            player->TeleportToBGEntryPoint();

        return;
    }

    LfgTeleportError error = LFG_TELEPORTERROR_OK;

    if (!player->IsAlive())
        error = LFG_TELEPORTERROR_PLAYER_DEAD;
    else if (player->IsFalling() || player->HasUnitState(UNIT_STATE_JUMPING))
        error = LFG_TELEPORTERROR_FALLING;
    else if (player->IsMirrorTimerActive(FATIGUE_TIMER))
        error = LFG_TELEPORTERROR_FATIGUE;
    else if (player->GetVehicle())
        error = LFG_TELEPORTERROR_IN_VEHICLE;
    else if (player->GetCharmedGUID())
        error = LFG_TELEPORTERROR_CHARMING;
    else if (player->HasAura(9454)) // check Freeze debuff
        error = LFG_TELEPORTERROR_INVALID_LOCATION;
    else if (player->GetMapId() != uint32(dungeon->map))  // Do not teleport players in dungeon to the entrance
    {
        uint32 mapid = dungeon->map;
        float x = dungeon->x;
        float y = dungeon->y;
        float z = dungeon->z;
        float orientation = dungeon->o;

        if (!fromOpcode)
        {
            // Select a player inside to be teleported to
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* plrg = itr->GetSource();
                if (plrg && plrg != player && plrg->GetMapId() == uint32(dungeon->map))
                {
                    mapid = plrg->GetMapId();
                    x = plrg->GetPositionX();
                    y = plrg->GetPositionY();
                    z = plrg->GetPositionZ();
                    orientation = plrg->GetOrientation();
                    break;
                }
            }
        }

        if (!player->GetMap()->IsDungeon())
            player->SetBattlegroundEntryPoint();

        player->FinishTaxiFlight();

        if (!player->TeleportTo(mapid, x, y, z, orientation))
            error = LFG_TELEPORTERROR_INVALID_LOCATION;
    }
    else
        error = LFG_TELEPORTERROR_INVALID_LOCATION;

    if (error != LFG_TELEPORTERROR_OK)
        player->GetSession()->SendLfgTeleportError(uint8(error));

    TC_LOG_DEBUG("lfg.teleport", "Player {} is being teleported in to map {} "
        "(x: {}, y: {}, z: {}) Result: {}", player->GetName(), dungeon->map,
        dungeon->x, dungeon->y, dungeon->z, error);
}

/**
 * @brief 完成地下城并发放奖励
 *
 * 职责：标记地下城完成，为玩家发放随机副本奖励
 *
 * @param gguid 队伍GUID
 * @param dungeonId 地下城ID
 * @param currMap 当前地图
 *
 * 主要流程：
 * 1. 验证地下城ID匹配
 * 2. 检查是否已完成（避免重复奖励）
 * 3. 更新状态为已完成
 * 4. 对于随机/季节性副本：
 *    - 移除冷却时间buff
 *    - 发放首次奖励任务（首杀奖励）
 *    - 发放普通奖励任务
 *    - 更新成就进度
 * 5. 发送奖励通知给玩家
 */
void LFGMgr::FinishDungeon(ObjectGuid gguid, const uint32 dungeonId, Map const* currMap)
{
    uint32 gDungeonId = GetDungeon(gguid);
    if (gDungeonId != dungeonId)
    {
        TC_LOG_DEBUG("lfg.dungeon.finish", "Group {} finished dungeon {} but queued for {}", gguid.ToString(), dungeonId, gDungeonId);
        return;
    }

    if (GetState(gguid) == LFG_STATE_FINISHED_DUNGEON) // Shouldn't happen. Do not reward multiple times
    {
        TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {} already rewarded", gguid.ToString());
        return;
    }

    SetState(gguid, LFG_STATE_FINISHED_DUNGEON);

    GuidSet const& players = GetPlayers(gguid);
    for (GuidSet::const_iterator it = players.begin(); it != players.end(); ++it)
    {
        ObjectGuid guid = (*it);
        if (GetState(guid) == LFG_STATE_FINISHED_DUNGEON)
        {
            TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} already rewarded", gguid.ToString(), guid.ToString());
            continue;
        }

        uint32 rDungeonId = 0;
        LfgDungeonSet const& dungeons = GetSelectedDungeons(guid);
        if (!dungeons.empty())
            rDungeonId = (*dungeons.begin());

        SetState(guid, LFG_STATE_FINISHED_DUNGEON);

        // Give rewards only if its a random dungeon
        LFGDungeonData const* dungeon = GetLFGDungeon(rDungeonId);

        if (!dungeon || (dungeon->type != LFG_TYPE_RANDOM && !dungeon->seasonal))
        {
            TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} dungeon {} is not random or seasonal", gguid.ToString(), guid.ToString(), rDungeonId);
            continue;
        }

        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
        {
            TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} not found in world", gguid.ToString(), guid.ToString());
            continue;
        }

        if (player->FindMap() != currMap)
        {
            TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} is in a different map", gguid.ToString(), guid.ToString());
            continue;
        }

        player->RemoveAurasDueToSpell(LFG_SPELL_DUNGEON_COOLDOWN);

        LFGDungeonData const* dungeonDone = GetLFGDungeon(dungeonId);
        uint32 mapId = dungeonDone ? uint32(dungeonDone->map) : 0;

        if (player->GetMapId() != mapId)
        {
            TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} is in map {} and should be in {} to get reward", gguid.ToString(), guid.ToString(), player->GetMapId(), mapId);
            continue;
        }

        // Update achievements
        if (dungeon->difficulty == DUNGEON_DIFFICULTY_HEROIC)
        {
            uint8 lfdRandomPlayers = 0;
            if (uint8 numParty = PlayersStore[guid].GetNumberOfPartyMembersAtJoin())
                lfdRandomPlayers = 5 - numParty;
            else
                lfdRandomPlayers = 4;
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS, lfdRandomPlayers);
        }

        LfgReward const* reward = GetRandomDungeonReward(rDungeonId, player->GetLevel());
        if (!reward)
            continue;

        bool done = false;
        Quest const* quest = sObjectMgr->GetQuestTemplate(reward->firstQuest);
        if (!quest)
            continue;

        // if we can take the quest, means that we haven't done this kind of "run", IE: First Heroic Random of Day.
        if (player->CanRewardQuest(quest, false))
            player->RewardQuest(quest, 0, nullptr, false);
        else
        {
            done = true;
            quest = sObjectMgr->GetQuestTemplate(reward->otherQuest);
            if (!quest)
                continue;
            // we give reward without informing client (retail does this)
            player->RewardQuest(quest, 0, nullptr, false);
        }

        // Give rewards
        TC_LOG_DEBUG("lfg.dungeon.finish", "Group: {}, Player: {} done dungeon {}, {} previously done.", gguid.ToString(), guid.ToString(), GetDungeon(gguid), done ? " " : " not");
        LfgPlayerRewardData data = LfgPlayerRewardData(dungeon->Entry(), GetDungeon(gguid, false), done, quest);
        player->GetSession()->SendLfgPlayerReward(data);
    }
}

// --------------------------------------------------------------------------//
// 辅助函数
// --------------------------------------------------------------------------//

/**
 * @brief 获取指定随机地下城可完成的地下城列表
 *
 * 职责：根据随机地下城ID获取对应的地下城列表
 *
 * @param randomdungeon 随机地下城ID（0表示返回所有地下城）
 * @return 可完成的地下城集合
 */
LfgDungeonSet const& LFGMgr::GetDungeonsByRandom(uint32 randomdungeon)
{
    LFGDungeonData const* dungeon = GetLFGDungeon(randomdungeon);
    uint32 group = dungeon ? dungeon->group : 0;
    return CachedDungeonMapStore[group];
}

/**
 * @brief 获取指定等级下随机地下城的奖励
 *
 * 职责：根据地下城ID和玩家等级获取对应的奖励配置
 *
 * @param dungeon 地下城ID
 * @param level 玩家等级
 * @return 奖励信息指针
 */
LfgReward const* LFGMgr::GetRandomDungeonReward(uint32 dungeon, uint8 level)
{
    LfgReward const* rew = nullptr;
    LfgRewardContainerBounds bounds = RewardMapStore.equal_range(dungeon & 0x00FFFFFF);
    for (LfgRewardContainer::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
    {
        rew = itr->second;
        // ordered properly at loading
        if (itr->second->maxLevel >= level)
            break;
    }

    return rew;
}

/**
 * @brief 获取地下城类型
 *
 * 职责：根据地下城ID返回其类型（普通/英雄/团队/随机等）
 *
 * @param dungeonId 地下城ID
 * @return 地下城类型
 */
LfgType LFGMgr::GetDungeonType(uint32 dungeonId)
{
    LFGDungeonData const* dungeon = GetLFGDungeon(dungeonId);
    if (!dungeon)
        return LFG_TYPE_NONE;

    return LfgType(dungeon->type);
}

/**
 * @brief 获取LFG状态
 *
 * 职责：获取玩家或队伍当前的LFG状态
 *
 * @param guid 玩家或队伍GUID
 * @return LFG状态
 */
LfgState LFGMgr::GetState(ObjectGuid guid)
{
    LfgState state;
    if (guid.IsGroup())
    {
        state = GroupsStore[guid].GetState();
        TC_LOG_TRACE("lfg.data.group.state.get", "Group: {}, State: {}", guid.ToString(), GetStateString(state));
    }
    else
    {
        state = PlayersStore[guid].GetState();
        TC_LOG_TRACE("lfg.data.player.state.get", "Player: {}, State: {}", guid.ToString(), GetStateString(state));
    }

    return state;
}

/**
 * @brief 获取旧的LFG状态
 *
 * 职责：获取玩家或队伍之前的LFG状态（用于状态恢复）
 *
 * @param guid 玩家或队伍GUID
 * @return 旧的LFG状态
 */
LfgState LFGMgr::GetOldState(ObjectGuid guid)
{
    LfgState state;
    if (guid.IsGroup())
    {
        state = GroupsStore[guid].GetOldState();
        TC_LOG_TRACE("lfg.data.group.oldstate.get", "Group: {}, Old state: {}", guid.ToString(), state);
    }
    else
    {
        state = PlayersStore[guid].GetOldState();
        TC_LOG_TRACE("lfg.data.player.oldstate.get", "Player: {}, Old state: {}", guid.ToString(), state);
    }

    return state;
}

/**
 * @brief 检查踢人投票是否激活
 *
 * 职责：检查队伍当前是否有正在进行的踢人投票
 *
 * @param gguid 队伍GUID
 * @return 踢人投票是否激活
 */
bool LFGMgr::IsVoteKickActive(ObjectGuid gguid)
{
    ASSERT(gguid.IsGroup());

    bool active = GroupsStore[gguid].IsVoteKickActive();
    TC_LOG_TRACE("lfg.data.group.votekick.get", "Group: {}, Active: {}", gguid.ToString(), active);

    return active;
}

/**
 * @brief 获取地下城ID
 *
 * 职责：获取队伍当前进行或排队的地下城ID
 *
 * @param guid 队伍GUID
 * @param asId 是否返回ID而非入口ID（默认true）
 * @return 地下城ID
 */
uint32 LFGMgr::GetDungeon(ObjectGuid guid, bool asId /*= true */)
{
    uint32 dungeon = GroupsStore[guid].GetDungeon(asId);
    TC_LOG_TRACE("lfg.data.group.dungeon.get", "Group: {}, asId: {}, Dungeon: {}", guid.ToString(), asId, dungeon);
    return dungeon;
}

/**
 * @brief 获取地下城地图ID
 *
 * 职责：根据地下城ID获取对应的地图ID
 *
 * @param guid 队伍GUID
 * @return 地图ID
 */
uint32 LFGMgr::GetDungeonMapId(ObjectGuid guid)
{
    uint32 dungeonId = GroupsStore[guid].GetDungeon(true);
    uint32 mapId = 0;
    if (dungeonId)
        if (LFGDungeonData const* dungeon = GetLFGDungeon(dungeonId))
            mapId = dungeon->map;

    TC_LOG_TRACE("lfg.data.group.dungeon.map", "Group: {}, MapId: {} (DungeonId: {})", guid.ToString(), mapId, dungeonId);

    return mapId;
}

/**
 * @brief 获取玩家角色
 *
 * 职责：获取玩家在LFG中选择的角色（坦克/治疗/输出）
 *
 * @param guid 玩家GUID
 * @return 角色标志位
 */
uint8 LFGMgr::GetRoles(ObjectGuid guid)
{
    uint8 roles = PlayersStore[guid].GetRoles();
    TC_LOG_TRACE("lfg.data.player.role.get", "Player: {}, Role: {}", guid.ToString(), roles);
    return roles;
}

/**
 * @brief 获取玩家备注
 *
 * 职责：获取玩家在LFG中设置的备注信息
 *
 * @param guid 玩家GUID
 * @return 备注字符串引用
 */
const std::string& LFGMgr::GetComment(ObjectGuid guid)
{
    TC_LOG_TRACE("lfg.data.player.comment.get", "Player: {}, Comment: {}", guid.ToString(), PlayersStore[guid].GetComment());
    return PlayersStore[guid].GetComment();
}

/**
 * @brief 获取玩家选择的地下城列表
 *
 * 职责：获取玩家在LFG中选择的地下城集合
 *
 * @param guid 玩家GUID
 * @return 地下城集合引用
 */
LfgDungeonSet const& LFGMgr::GetSelectedDungeons(ObjectGuid guid)
{
    TC_LOG_TRACE("lfg.data.player.dungeons.selected.get", "Player: {}, Selected Dungeons: {}", guid.ToString(), ConcatenateDungeons(PlayersStore[guid].GetSelectedDungeons()));
    return PlayersStore[guid].GetSelectedDungeons();
}

/**
 * @brief 获取玩家被锁定的地下城列表
 *
 * 职责：获取玩家无法进入的地下城及其原因
 *
 * @param guid 玩家GUID
 * @return 锁定状态映射（地下城ID -> 锁定原因）
 *
 * 主要流程：
 * 1. 检查权限、等级、资料片限制
 * 2. 检查地图是否被禁用
 * 3. 检查副本锁定状态
 * 4. 检查季节性活动
 * 5. 检查装备等级、成就、任务要求等
 */
LfgLockMap const LFGMgr::GetLockedDungeons(ObjectGuid guid)
{
    TC_LOG_TRACE("lfg.data.player.dungeons.locked.get", "Player: {}, LockedDungeons.", guid.ToString());
    LfgLockMap lock;
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player)
    {
        TC_LOG_WARN("lfg.data.player.dungeons.locked.get", "Player: {} not ingame while retrieving his LockedDungeons.", guid.ToString());
        return lock;
    }

    uint8 level = player->GetLevel();
    uint8 expansion = player->GetSession()->Expansion();
    LfgDungeonSet const& dungeons = GetDungeonsByRandom(0);
    bool denyJoin = !player->GetSession()->HasPermission(rbac::RBAC_PERM_JOIN_DUNGEON_FINDER);

    for (LfgDungeonSet::const_iterator it = dungeons.begin(); it != dungeons.end(); ++it)
    {
        LFGDungeonData const* dungeon = GetLFGDungeon(*it);
        if (!dungeon) // should never happen - We provide a list from sLFGDungeonStore
            continue;

        uint32 lockData = 0;
        if (denyJoin)
            lockData = LFG_LOCKSTATUS_RAID_LOCKED;
        else if (dungeon->expansion > expansion)
            lockData = LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        else if (DisableMgr::IsDisabledFor(DISABLE_TYPE_MAP, dungeon->map, player))
            lockData = LFG_LOCKSTATUS_RAID_LOCKED;
        else if (DisableMgr::IsDisabledFor(DISABLE_TYPE_LFG_MAP, dungeon->map, player))
            lockData = LFG_LOCKSTATUS_RAID_LOCKED;
        else if (dungeon->difficulty > DUNGEON_DIFFICULTY_NORMAL && player->GetBoundInstance(dungeon->map, Difficulty(dungeon->difficulty)))
            lockData = LFG_LOCKSTATUS_RAID_LOCKED;
        else if (dungeon->minlevel > level)
            lockData = LFG_LOCKSTATUS_TOO_LOW_LEVEL;
        else if (dungeon->maxlevel < level)
            lockData = LFG_LOCKSTATUS_TOO_HIGH_LEVEL;
        else if (dungeon->seasonal && !IsSeasonActive(dungeon->id))
            lockData = LFG_LOCKSTATUS_NOT_IN_SEASON;
        else if (AccessRequirement const* ar = sObjectMgr->GetAccessRequirement(dungeon->map, Difficulty(dungeon->difficulty)))
        {
            if (ar->item_level && player->GetAverageItemLevel() < ar->item_level)
                lockData = LFG_LOCKSTATUS_TOO_LOW_GEAR_SCORE;
            else if (ar->achievement && !player->HasAchieved(ar->achievement))
                lockData = LFG_LOCKSTATUS_MISSING_ACHIEVEMENT;
            else if (player->GetTeam() == ALLIANCE && ar->quest_A && !player->GetQuestRewardStatus(ar->quest_A))
                lockData = LFG_LOCKSTATUS_QUEST_NOT_COMPLETED;
            else if (player->GetTeam() == HORDE && ar->quest_H && !player->GetQuestRewardStatus(ar->quest_H))
                lockData = LFG_LOCKSTATUS_QUEST_NOT_COMPLETED;
            else
                if (ar->item)
                {
                    if (!player->HasItemCount(ar->item) && (!ar->item2 || !player->HasItemCount(ar->item2)))
                        lockData = LFG_LOCKSTATUS_MISSING_ITEM;
                }
                else if (ar->item2 && !player->HasItemCount(ar->item2))
                    lockData = LFG_LOCKSTATUS_MISSING_ITEM;
        }

        /* @todo VoA closed if WG is not under team control (LFG_LOCKSTATUS_RAID_LOCKED)
        lockData = LFG_LOCKSTATUS_TOO_HIGH_GEAR_SCORE;
        lockData = LFG_LOCKSTATUS_ATTUNEMENT_TOO_LOW_LEVEL;
        lockData = LFG_LOCKSTATUS_ATTUNEMENT_TOO_HIGH_LEVEL;
        */

        if (lockData)
            lock[dungeon->Entry()] = lockData;
    }

    return lock;
}

/**
 * @brief 获取剩余踢人次数
 *
 * 职责：获取队伍剩余可踢人次数
 *
 * @param guid 队伍GUID
 * @return 剩余踢人次数
 */
uint8 LFGMgr::GetKicksLeft(ObjectGuid guid)
{
    uint8 kicks = GroupsStore[guid].GetKicksLeft();
    TC_LOG_TRACE("lfg.data.group.kickleft.get", "Group: {}, Kicks left: {}", guid.ToString(), kicks);
    return kicks;
}

/**
 * @brief 恢复之前的LFG状态
 *
 * 职责：将玩家或队伍恢复到之前的LFG状态
 *
 * @param guid 玩家或队伍GUID
 * @param debugMsg 调试信息
 */
void LFGMgr::RestoreState(ObjectGuid guid, char const* debugMsg)
{
    if (guid.IsGroup())
    {
        LfgGroupData& data = GroupsStore[guid];
        TC_LOG_TRACE("lfg.data.group.state.restore", "Group: {} ({}), State: {}, Old state: {}",
            guid.ToString(), debugMsg, GetStateString(data.GetState()),
            GetStateString(data.GetOldState()));

        data.RestoreState();
    }
    else
    {
        LfgPlayerData& data = PlayersStore[guid];
        TC_LOG_TRACE("lfg.data.player.state.restore", "Player: {} ({}), State: {}, Old state: {}",
            guid.ToString(), debugMsg, GetStateString(data.GetState()),
            GetStateString(data.GetOldState()));

        data.RestoreState();
    }
}

/**
 * @brief 设置LFG状态
 *
 * 职责：设置玩家或队伍的LFG状态
 *
 * @param guid 玩家或队伍GUID
 * @param state 新状态
 */
void LFGMgr::SetState(ObjectGuid guid, LfgState state)
{
    if (guid.IsGroup())
    {
        LfgGroupData& data = GroupsStore[guid];
        TC_LOG_TRACE("lfg.data.group.state.set", "Group: {}, New state: {}, Previous: {}, Old state: {}",
            guid.ToString(), GetStateString(state), GetStateString(data.GetState()),
            GetStateString(data.GetOldState()));

        data.SetState(state);
    }
    else
    {
        LfgPlayerData& data = PlayersStore[guid];
        TC_LOG_TRACE("lfg.data.player.state.set", "Player: {}, New state: {}, Previous: {}, OldState: {}",
            guid.ToString(), GetStateString(state), GetStateString(data.GetState()),
            GetStateString(data.GetOldState()));

        data.SetState(state);
    }
}

/**
 * @brief 设置踢人投票状态
 *
 * 职责：设置队伍踢人投票的激活状态
 *
 * @param gguid 队伍GUID
 * @param active 是否激活
 */
void LFGMgr::SetVoteKick(ObjectGuid gguid, bool active)
{
    ASSERT(gguid.IsGroup());

    LfgGroupData& data = GroupsStore[gguid];
    TC_LOG_TRACE("lfg.data.group.votekick.set", "Group: {}, New state: {}, Previous: {}",
        gguid.ToString(), active, data.IsVoteKickActive());

    data.SetVoteKick(active);
}

/**
 * @brief 设置地下城
 *
 * 职责：设置队伍当前进行或排队的地下城
 *
 * @param guid 队伍GUID
 * @param dungeon 地下城ID
 */
void LFGMgr::SetDungeon(ObjectGuid guid, uint32 dungeon)
{
    TC_LOG_TRACE("lfg.data.group.dungeon.set", "Group: {}, Dungeon: {}", guid.ToString(), dungeon);
    GroupsStore[guid].SetDungeon(dungeon);
}

/**
 * @brief 设置玩家角色
 *
 * 职责：设置玩家在LFG中的角色
 *
 * @param guid 玩家GUID
 * @param roles 角色标志位
 */
void LFGMgr::SetRoles(ObjectGuid guid, uint8 roles)
{
    TC_LOG_TRACE("lfg.data.player.role.set", "Player: {}, Roles: {}", guid.ToString(), roles);
    PlayersStore[guid].SetRoles(roles);
}

/**
 * @brief 设置玩家备注
 *
 * 职责：设置玩家在LFG中的备注信息
 *
 * @param guid 玩家GUID
 * @param comment 备注内容
 */
void LFGMgr::SetComment(ObjectGuid guid, std::string const& comment)
{
    TC_LOG_TRACE("lfg.data.player.comment.set", "Player: {}, Comment: {}", guid.ToString(), comment);
    PlayersStore[guid].SetComment(comment);
}

/**
 * @brief 设置选择的地下城
 *
 * 职责：设置玩家在LFG中选择的地下城列表
 *
 * @param guid 玩家GUID
 * @param dungeons 地下城集合
 */
void LFGMgr::SetSelectedDungeons(ObjectGuid guid, LfgDungeonSet const& dungeons)
{
    TC_LOG_TRACE("lfg.data.player.dungeon.selected.set", "Player: {}, Dungeons: {}", guid.ToString(), ConcatenateDungeons(dungeons));
    PlayersStore[guid].SetSelectedDungeons(dungeons);
}

/**
 * @brief 减少剩余踢人次数
 *
 * 职责：减少队伍剩余可踢人次数
 *
 * @param guid 队伍GUID
 */
void LFGMgr::DecreaseKicksLeft(ObjectGuid guid)
{
    GroupsStore[guid].DecreaseKicksLeft();
    TC_LOG_TRACE("lfg.data.group.kicksleft.decrease", "Group: {}, Kicks: {}", guid.ToString(), GroupsStore[guid].GetKicksLeft());
}

/**
 * @brief 移除玩家数据
 *
 * 职责：从玩家数据存储中移除指定玩家
 *
 * @param guid 玩家GUID
 */
void LFGMgr::RemovePlayerData(ObjectGuid guid)
{
    TC_LOG_TRACE("lfg.data.player.remove", "Player: {}", guid.ToString());
    LfgPlayerDataContainer::iterator it = PlayersStore.find(guid);
    if (it != PlayersStore.end())
        PlayersStore.erase(it);
}

/**
 * @brief 移除队伍数据
 *
 * 职责：从队伍数据存储中移除指定队伍及相关玩家状态
 *
 * @param guid 队伍GUID
 */
void LFGMgr::RemoveGroupData(ObjectGuid guid)
{
    TC_LOG_TRACE("lfg.data.group.remove", "Group: {}", guid.ToString());
    LfgGroupDataContainer::iterator it = GroupsStore.find(guid);
    if (it == GroupsStore.end())
        return;

    LfgState state = GetState(guid);
    // If group is being formed after proposal success do nothing more
    GuidSet const& players = it->second.GetPlayers();
    for (ObjectGuid playerGUID : players)
    {
        SetGroup(playerGUID, ObjectGuid::Empty);
        if (state != LFG_STATE_PROPOSAL)
        {
            SetState(playerGUID, LFG_STATE_NONE);
            SendLfgUpdateParty(playerGUID, LfgUpdateData(LFG_UPDATETYPE_REMOVED_FROM_QUEUE));
        }
    }
    GroupsStore.erase(it);
}

/**
 * @brief 获取玩家阵营
 *
 * 职责：获取玩家的阵营（联盟/部落）
 *
 * @param guid 玩家GUID
 * @return 阵营ID
 */
uint8 LFGMgr::GetTeam(ObjectGuid guid)
{
    uint8 team = PlayersStore[guid].GetTeam();
    TC_LOG_TRACE("lfg.data.player.team.get", "Player: {}, Team: {}", guid.ToString(), team);
    return team;
}

/**
 * @brief 过滤职业角色
 *
 * 职责：根据玩家职业过滤可选角色（如战士不能选择治疗）
 *
 * @param player 玩家对象
 * @param roles 原始角色标志位
 * @return 过滤后的角色标志位
 */
uint8 LFGMgr::FilterClassRoles(Player* player, uint8 roles)
{
    roles &= PLAYER_ROLE_ANY;
    if (!(LfgRoleClasses::TANK & player->GetClassMask()))
        roles &= ~PLAYER_ROLE_TANK;
    if (!(LfgRoleClasses::HEALER & player->GetClassMask()))
        roles &= ~PLAYER_ROLE_HEALER;
    return roles;
}

/**
 * @brief 从队伍移除玩家
 *
 * 职责：从队伍数据中移除指定玩家
 *
 * @param gguid 队伍GUID
 * @param guid 玩家GUID
 * @return 剩余玩家数量
 */
uint8 LFGMgr::RemovePlayerFromGroup(ObjectGuid gguid, ObjectGuid guid)
{
    return GroupsStore[gguid].RemovePlayer(guid);
}

/**
 * @brief 添加玩家到队伍
 *
 * 职责：将玩家添加到队伍数据中
 *
 * @param gguid 队伍GUID
 * @param guid 玩家GUID
 */
void LFGMgr::AddPlayerToGroup(ObjectGuid gguid, ObjectGuid guid)
{
    GroupsStore[gguid].AddPlayer(guid);
}

/**
 * @brief 设置队伍领袖
 *
 * 职责：设置队伍的领袖玩家
 *
 * @param gguid 队伍GUID
 * @param leader 领袖玩家GUID
 */
void LFGMgr::SetLeader(ObjectGuid gguid, ObjectGuid leader)
{
    GroupsStore[gguid].SetLeader(leader);
}

/**
 * @brief 设置玩家阵营
 *
 * 职责：设置玩家的阵营，考虑跨阵营组队设置
 *
 * @param guid 玩家GUID
 * @param team 阵营ID
 */
void LFGMgr::SetTeam(ObjectGuid guid, uint8 team)
{
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
        team = 0;

    PlayersStore[guid].SetTeam(team);
}

/**
 * @brief 获取玩家所在队伍
 *
 * 职责：获取玩家所在的队伍GUID
 *
 * @param guid 玩家GUID
 * @return 队伍GUID（无队伍则为空）
 */
ObjectGuid LFGMgr::GetGroup(ObjectGuid guid)
{
    return PlayersStore[guid].GetGroup();
}

/**
 * @brief 设置玩家所在队伍
 *
 * 职责：设置玩家所属的队伍
 *
 * @param guid 玩家GUID
 * @param group 队伍GUID
 */
void LFGMgr::SetGroup(ObjectGuid guid, ObjectGuid group)
{
    PlayersStore[guid].SetGroup(group);
}

/**
 * @brief 获取队伍中的所有玩家
 *
 * 职责：获取队伍中所有玩家的GUID集合
 *
 * @param guid 队伍GUID
 * @return 玩家GUID集合引用
 */
GuidSet const& LFGMgr::GetPlayers(ObjectGuid guid)
{
    return GroupsStore[guid].GetPlayers();
}

/**
 * @brief 获取队伍玩家数量
 *
 * 职责：获取队伍中的玩家数量
 *
 * @param guid 队伍GUID
 * @return 玩家数量
 */
uint8 LFGMgr::GetPlayerCount(ObjectGuid guid)
{
    return GroupsStore[guid].GetPlayerCount();
}

/**
 * @brief 获取队伍领袖
 *
 * 职责：获取队伍领袖的GUID
 *
 * @param guid 队伍GUID
 * @return 领袖玩家GUID
 */
ObjectGuid LFGMgr::GetLeader(ObjectGuid guid)
{
    return GroupsStore[guid].GetLeader();
}

/**
 * @brief 检查两个玩家是否互相屏蔽
 *
 * 职责：检查两个玩家之间是否存在屏蔽关系
 *
 * @param guid1 第一个玩家GUID
 * @param guid2 第二个玩家GUID
 * @return 是否存在屏蔽关系
 */
bool LFGMgr::HasIgnore(ObjectGuid guid1, ObjectGuid guid2)
{
    Player* plr1 = ObjectAccessor::FindConnectedPlayer(guid1);
    Player* plr2 = ObjectAccessor::FindConnectedPlayer(guid2);
    return plr1 && plr2 && (plr1->GetSocial()->HasIgnore(guid2) || plr2->GetSocial()->HasIgnore(guid1));
}

/**
 * @brief 发送角色选择通知
 *
 * 职责：向玩家发送角色选择通知
 *
 * @param guid 接收通知的玩家GUID
 * @param pguid 选择角色的玩家GUID
 * @param roles 选择的角色
 */
void LFGMgr::SendLfgRoleChosen(ObjectGuid guid, ObjectGuid pguid, uint8 roles)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgRoleChosen(pguid, roles);
}

/**
 * @brief 发送角色检查更新
 *
 * 职责：向玩家发送角色检查状态更新
 *
 * @param guid 玩家GUID
 * @param roleCheck 角色检查数据
 */
void LFGMgr::SendLfgRoleCheckUpdate(ObjectGuid guid, LfgRoleCheck const& roleCheck)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgRoleCheckUpdate(roleCheck);
}

/**
 * @brief 发送玩家LFG更新
 *
 * 职责：向玩家发送LFG状态更新（单人队列）
 *
 * @param guid 玩家GUID
 * @param data 更新数据
 */
void LFGMgr::SendLfgUpdatePlayer(ObjectGuid guid, LfgUpdateData const& data)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgUpdatePlayer(data);
}

/**
 * @brief 发送队伍LFG更新
 *
 * 职责：向玩家发送队伍LFG状态更新
 *
 * @param guid 玩家GUID
 * @param data 更新数据
 */
void LFGMgr::SendLfgUpdateParty(ObjectGuid guid, LfgUpdateData const& data)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgUpdateParty(data);
}

/**
 * @brief 发送加入结果
 *
 * 职责：向玩家发送加入LFG的结果
 *
 * @param guid 玩家GUID
 * @param data 加入结果数据
 */
void LFGMgr::SendLfgJoinResult(ObjectGuid guid, LfgJoinResultData const& data)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgJoinResult(data);
}

/**
 * @brief 发送踢人投票更新
 *
 * 职责：向玩家发送踢人投票状态更新
 *
 * @param guid 玩家GUID
 * @param boot 踢人投票数据
 */
void LFGMgr::SendLfgBootProposalUpdate(ObjectGuid guid, LfgPlayerBoot const& boot)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgBootProposalUpdate(boot);
}

/**
 * @brief 发送提案更新
 *
 * 职责：向玩家发送组队提案状态更新
 *
 * @param guid 玩家GUID
 * @param proposal 提案数据
 */
void LFGMgr::SendLfgUpdateProposal(ObjectGuid guid, LfgProposal const& proposal)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgUpdateProposal(proposal);
}

/**
 * @brief 发送队列状态
 *
 * 职责：向玩家发送队列等待时间状态
 *
 * @param guid 玩家GUID
 * @param data 队列状态数据
 */
void LFGMgr::SendLfgQueueStatus(ObjectGuid guid, LfgQueueStatusData const& data)
{
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        player->GetSession()->SendLfgQueueStatus(data);
}

/**
 * @brief 检查是否为LFG队伍
 *
 * 职责：判断指定GUID是否为LFG队伍
 *
 * @param guid 队伍GUID
 * @return 是否为LFG队伍
 */
bool LFGMgr::IsLfgGroup(ObjectGuid guid)
{
    return guid && guid.IsGroup() && GroupsStore[guid].IsLfgGroup();
}

/**
 * @brief 获取LFG队列
 *
 * 职责：根据玩家或队伍获取对应的LFG队列（按阵营区分）
 *
 * @param guid 玩家或队伍GUID
 * @return LFG队列引用
 */
LFGQueue& LFGMgr::GetQueue(ObjectGuid guid)
{
    uint8 queueId = 0;
    if (guid.IsGroup())
    {
        GuidSet const& players = GetPlayers(guid);
        ObjectGuid pguid = players.empty() ? ObjectGuid::Empty : (*players.begin());
        if (pguid)
            queueId = GetTeam(pguid);
    }
    else
        queueId = GetTeam(guid);
    return QueuesStore[queueId];
}

/**
 * @brief 检查所有GUID是否都在队列中
 *
 * 职责：验证所有指定的玩家/队伍是否都处于排队状态
 *
 * @param check GUID列表
 * @return 是否全部在队列中
 */
bool LFGMgr::AllQueued(GuidList const& check)
{
    if (check.empty())
        return false;

    for (GuidList::const_iterator it = check.begin(); it != check.end(); ++it)
    {
        LfgState state = GetState(*it);
        if (state != LFG_STATE_QUEUED)
        {
            if (state != LFG_STATE_PROPOSAL)
                TC_LOG_DEBUG("lfg.allqueued", "Unexpected state found while trying to form new group. Guid: {}, State: {}", (*it).ToString(), GetStateString(state));

            return false;
        }
    }
    return true;
}

/**
 * @brief 清理所有队列（仅用于调试）
 *
 * 职责：清空所有队列存储
 */
// Only for debugging purposes
void LFGMgr::Clean()
{
    QueuesStore.clear();
}

/**
 * @brief 检查选项是否启用
 *
 * 职责：检查指定的LFG选项是否启用
 *
 * @param option 选项标志位
 * @return 是否启用
 */
bool LFGMgr::isOptionEnabled(uint32 option)
{
    return (m_options & option) != 0;
}

/**
 * @brief 获取LFG选项配置
 *
 * 职责：获取当前的LFG选项配置
 *
 * @return 选项配置值
 */
uint32 LFGMgr::GetOptions()
{
    return m_options;
}

/**
 * @brief 设置LFG选项配置
 *
 * 职责：设置LFG选项配置
 *
 * @param options 选项配置值
 */
void LFGMgr::SetOptions(uint32 options)
{
    m_options = options;
}

/**
 * @brief 获取LFG状态
 *
 * 职责：获取玩家的LFG状态数据
 *
 * @param guid 玩家GUID
 * @return LFG更新数据
 */
LfgUpdateData LFGMgr::GetLfgStatus(ObjectGuid guid)
{
    LfgPlayerData& playerData = PlayersStore[guid];
    return LfgUpdateData(LFG_UPDATETYPE_UPDATE_STATUS, playerData.GetState(), playerData.GetSelectedDungeons());
}

/**
 * @brief 检查季节性活动是否激活
 *
 * 职责：检查指定地下城的季节性活动是否正在进行
 *
 * @param dungeonId 地下城ID
 * @return 季节性活动是否激活
 */
bool LFGMgr::IsSeasonActive(uint32 dungeonId)
{
    switch (dungeonId)
    {
        case 285: // The Headless Horseman
            return IsHolidayActive(HOLIDAY_HALLOWS_END);
        case 286: // The Frost Lord Ahune
            return IsHolidayActive(HOLIDAY_FIRE_FESTIVAL);
        case 287: // Coren Direbrew
            return IsHolidayActive(HOLIDAY_BREWFEST);
        case 288: // The Crown Chemical Co.
            return IsHolidayActive(HOLIDAY_LOVE_IS_IN_THE_AIR);
    }
    return false;
}

/**
 * @brief 导出队列信息
 *
 * 职责：导出当前队列状态信息（用于调试/GM命令）
 *
 * @param full 是否导出完整信息
 * @return 队列信息字符串
 */
std::string LFGMgr::DumpQueueInfo(bool full)
{
    uint32 size = uint32(QueuesStore.size());
    std::ostringstream o;

    o << "Number of Queues: " << size << "\n";
    for (LfgQueueContainer::const_iterator itr = QueuesStore.begin(); itr != QueuesStore.end(); ++itr)
    {
        std::string const& queued = itr->second.DumpQueueInfo();
        std::string const& compatibles = itr->second.DumpCompatibleInfo(full);
        o << queued << compatibles;
    }

    return o.str();
}

/**
 * @brief 设置队伍成员
 *
 * 职责：为新加入的队伍成员设置LFG数据
 *
 * @param guid 玩家GUID
 * @param gguid 队伍GUID
 */
void LFGMgr::SetupGroupMember(ObjectGuid guid, ObjectGuid gguid)
{
    LfgDungeonSet dungeons;
    dungeons.insert(GetDungeon(gguid));
    SetSelectedDungeons(guid, dungeons);
    SetState(guid, GetState(gguid));
    SetGroup(guid, gguid);
    AddPlayerToGroup(gguid, guid);
}

/**
 * @brief 检查是否选择了随机地下城
 *
 * 职责：判断玩家是否选择了随机或季节性地下城
 *
 * @param guid 玩家GUID
 * @return 是否选择了随机地下城
 */
bool LFGMgr::selectedRandomLfgDungeon(ObjectGuid guid)
{
    if (GetState(guid) != LFG_STATE_NONE)
    {
        LfgDungeonSet const& dungeons = GetSelectedDungeons(guid);
        if (!dungeons.empty())
        {
             LFGDungeonData const* dungeon = GetLFGDungeon(*dungeons.begin());
             if (dungeon && (dungeon->type == LFG_TYPE_RANDOM || dungeon->seasonal))
                 return true;
        }
    }

    return false;
}

/**
 * @brief 检查是否在LFG地下城地图中
 *
 * 职责：验证玩家是否在指定的LFG地下城地图中
 *
 * @param guid 玩家或队伍GUID
 * @param map 地图ID
 * @param difficulty 难度
 * @return 是否在LFG地下城地图中
 */
bool LFGMgr::inLfgDungeonMap(ObjectGuid guid, uint32 map, Difficulty difficulty)
{
    if (!guid.IsGroup())
        guid = GetGroup(guid);

    if (uint32 dungeonId = GetDungeon(guid, true))
        if (LFGDungeonData const* dungeon = GetLFGDungeon(dungeonId))
            if (uint32(dungeon->map) == map && dungeon->difficulty == difficulty)
                return true;

    return false;
}

/**
 * @brief 获取LFG地下城入口ID
 *
 * 职责：根据地下城ID获取其入口ID
 *
 * @param id 地下城ID
 * @return 入口ID（不存在返回0）
 */
uint32 LFGMgr::GetLFGDungeonEntry(uint32 id)
{
    if (id)
        if (LFGDungeonData const* dungeon = GetLFGDungeon(id))
            return dungeon->Entry();

    return 0;
}

/**
 * @brief 获取随机和季节性地下城列表
 *
 * 职责：根据玩家等级和资料片版本获取可用的随机和季节性地下城
 *
 * @param level 玩家等级
 * @param expansion 资料片版本
 * @return 可用的随机和季节性地下城集合
 */
LfgDungeonSet LFGMgr::GetRandomAndSeasonalDungeons(uint8 level, uint8 expansion)
{
    LfgDungeonSet randomDungeons;
    for (lfg::LFGDungeonContainer::const_iterator itr = LfgDungeonStore.begin(); itr != LfgDungeonStore.end(); ++itr)
    {
        lfg::LFGDungeonData const& dungeon = itr->second;
        if ((dungeon.type == lfg::LFG_TYPE_RANDOM || (dungeon.seasonal && sLFGMgr->IsSeasonActive(dungeon.id)))
            && dungeon.expansion <= expansion && dungeon.minlevel <= level && level <= dungeon.maxlevel)
            randomDungeons.insert(dungeon.Entry());
    }
    return randomDungeons;
}

} // namespace lfg
