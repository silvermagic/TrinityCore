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
 * @file Group.cpp
 * @brief 队伍系统核心实现文件
 *
 * 本文件实现了 Group 类的所有方法，是队伍系统的核心实现。
 *
 * 主要功能模块：
 *
 * 1. 队伍生命周期管理
 *    - 构造和析构
 *    - 创建和解散
 *    - 从数据库加载和保存
 *
 * 2. 成员管理
 *    - 添加和移除成员
 *    - 邀请管理
 *    - 队长变更
 *    - 子组管理
 *    - 成员标志设置
 *
 * 3. 战利品分配系统
 *    - 自由拾取模式
 *    - 队伍分配模式
 *    - 需求优先贪婪模式
 *    - 主分配者模式
 *    - 掷骰系统实现
 *
 * 4. 副本系统
 *    - 副本绑定管理
 *    - 难度设置
 *    - 副本重置
 *
 * 5. 战场和战场
 *    - 战场队伍创建
 *    - 战场队伍创建
 *    - 战场队列检查
 *
 * 6. 队伍更新和同步
 *    - 状态更新广播
 *    - 成员状态同步
 *    - 目标图标管理
 *
 * 7. 通信系统
 *    - 数据包广播
 *    - 就绪检查
 *    - 队伍消息发送
 *
 * 关键算法：
 * - 掷骰算法：处理需求/贪婪/分解/放弃投票，计算获胜者
 * - 子组平衡：自动分配成员到合适的子组
 * - 战利品分配资格检查：基于职业和装备需求判断是否可以需求
 *
 * 性能优化：
 * - 使用引用计数管理玩家关联
 * - 按需更新队伍状态，避免频繁广播
 * - 使用迭代器模式遍历成员
 *
 * 数据持久化：
 * - 队伍信息存储在 `groups` 表
 * - 成员信息存储在 `group_member` 表
 * - 副本绑定存储在 `group_instance` 表
 *
 * @see Group.h 队伍类定义
 * @see GroupMgr 队伍全局管理器
 * @see Roll 战利品掷骰类
 */

#include "Group.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Common.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "GameObject.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "MapManager.h"
#include "Log.h"
#include "LFGMgr.h"
#include "Random.h"
#include "SpellAuras.h"
#include "UpdateData.h"
#include "UpdateFieldFlags.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

/**
 * @brief Roll 构造函数 - 初始化战利品掷骰对象
 * @param _guid 战利品物品的GUID
 * @param li 战利品项引用，包含物品ID、随机属性、后缀、数量等信息
 *
 * 初始化掷骰所需的基本信息，包括物品标识、属性和投票统计计数器
 */
Roll::Roll(ObjectGuid _guid, LootItem const& li) : itemGUID(_guid), itemid(li.itemid),
itemRandomPropId(li.randomPropertyId), itemRandomSuffix(li.randomSuffix), itemCount(li.count),
totalPlayersRolling(0), totalNeed(0), totalGreed(0), totalPass(0), itemSlot(0),
rollVoteMask(ROLL_ALL_TYPE_NO_DISENCHANT) { }

Roll::~Roll() { }

void Roll::setLoot(Loot* pLoot)
{
    link(pLoot, this);
}

Loot* Roll::getLoot()
{
    return getTarget();
}

/**
 * @brief Group 构造函数 - 初始化队伍对象的默认状态
 *
 * 初始化队伍的所有成员变量为默认值：
 * - 队长GUID和名称为空
 * - 队伍类型为普通小队
 * - 副本难度为普通，团队难度为10人普通
 * - 战场/战场群组指针为空
 * - 分配方式为自由拾取，品质阈值为优秀
 * - 子队伍计数器为空
 * - 目标图标全部清空
 */
Group::Group() : m_leaderGuid(), m_leaderName(""), m_groupType(GROUPTYPE_NORMAL),
m_dungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL), m_raidDifficulty(RAID_DIFFICULTY_10MAN_NORMAL),
m_bgGroup(nullptr), m_bfGroup(nullptr), m_lootMethod(FREE_FOR_ALL), m_lootThreshold(ITEM_QUALITY_UNCOMMON), m_looterGuid(),
m_masterLooterGuid(), m_subGroupsCounts(nullptr), m_guid(), m_counter(0), m_maxEnchantingLevel(0), m_dbStoreId(0), m_isLeaderOffline(false),
 m_scriptRef(this, NoopGroupDeleter())
{
    // 清空所有目标图标（共8个）
    for (uint8 i = 0; i < TARGET_ICONS_COUNT; ++i)
        m_targetIcons[i].Clear();
}

/**
 * @brief Group 析构函数 - 清理队伍资源
 *
 * 主要清理工作：
 * 1. 如果是战场队伍，从战场对象中解除关联
 * 2. 清理所有未完成的掷骰记录
 * 3. 解除与副本存档的绑定关系
 * 4. 释放子队伍计数器数组
 */
Group::~Group()
{
    // 处理战场队伍的清理
    if (m_bgGroup)
    {
        TC_LOG_DEBUG("bg.battleground", "Group::~Group: battleground group being deleted.");
        // 从战场对象中移除本队伍的引用
        if (m_bgGroup->GetBgRaid(ALLIANCE) == this)
            m_bgGroup->SetBgRaid(ALLIANCE, nullptr);
        else if (m_bgGroup->GetBgRaid(HORDE) == this)
            m_bgGroup->SetBgRaid(HORDE, nullptr);
        else
            TC_LOG_ERROR("misc", "Group::~Group: battleground group is not linked to the correct battleground.");
    }

    // 清理所有掷骰记录
    Rolls::iterator itr;
    while (!RollId.empty())
    {
        itr = RollId.begin();
        Roll *r = *itr;
        RollId.erase(itr);
        delete(r);
    }

    // 解除副本存档绑定，可能导致存档对象被卸载
    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
        for (BoundInstancesMap::iterator itr2 = m_boundInstances[i].begin(); itr2 != m_boundInstances[i].end(); ++itr2)
            itr2->second.save->RemoveGroup(this);

    // 清理子队伍计数器
    delete[] m_subGroupsCounts;
}

/**
 * @brief Update - 更新队伍状态
 * @param diff 距离上次更新的时间差（毫秒）
 *
 * 主要职责：
 * - 处理队长离线计时器
 * - 当队长离线时间到期时，自动选择新队长
 *
 * 仅对普通小队和团队有效，战场队伍不触发此逻辑
 */
void Group::Update(uint32 diff)
{
    // 如果队长离线，且队伍类型是普通小队或团队
    if (m_isLeaderOffline && (m_groupType == GROUPTYPE_NORMAL || m_groupType == GROUPTYPE_RAID))
    {
        m_leaderOfflineTimer.Update(diff);
        // 计时器到期后选择新队长
        if (m_leaderOfflineTimer.Passed())
        {
            SelectNewPartyOrRaidLeader();
            m_isLeaderOffline = false;
        }
    }
}

/**
 * @brief SelectNewPartyOrRaidLeader - 选择新的队伍或团队队长
 *
 * 主要流程：
 * 1. 如果是团队，优先选择在线的主力助理（Main Assistant）
 * 2. 如果没有助理或不是团队，选择第一个在线的成员
 * 3. 找到合适的候选人后，调用 ChangeLeader 更换队长
 * 4. 发送队伍更新通知
 */
void Group::SelectNewPartyOrRaidLeader()
{
    Player* newLeader = nullptr;

    // 团队中优先将队长权限移交给主力助理
    if (m_groupType == GROUPTYPE_RAID)
    {
        for (auto memberSlot : m_memberSlots)
        {
            // 查找拥有助理标志的成员
            if ((memberSlot.flags & MEMBER_FLAG_ASSISTANT) == MEMBER_FLAG_ASSISTANT)
                if (Player* player = ObjectAccessor::FindPlayer(memberSlot.guid))
                {
                    newLeader = player;
                    break;
                }
        }
    }

    // 如果没有助理或不是团队，选择第一个在线的成员
    if (!newLeader)
    {
        for (auto memberSlot : m_memberSlots)
            if (Player* player = ObjectAccessor::FindPlayer(memberSlot.guid))
            {
                newLeader = player;
                break;
            }
    }

    // 如果找到了在线的新队长候选人，执行更换队长操作
    if (newLeader)
    {
        ChangeLeader(newLeader->GetGUID());
        SendUpdate();
    }
}

/**
 * @brief Create - 创建新队伍
 * @param leader 队长玩家指针
 * @return 创建成功返回true，失败返回false
 *
 * 主要流程：
 * 1. 生成队伍GUID并设置队长信息
 * 2. 设置队长标志
 * 3. 根据队伍类型初始化相关数据
 * 4. 设置拾取方式和难度
 * 5. 非战场队伍：保存到数据库、转移队长副本绑定、添加队长为成员
 * 6. 战场队伍：直接添加队长为成员
 */
bool Group::Create(Player* leader)
{
    ObjectGuid leaderGuid = leader->GetGUID();
    ObjectGuid::LowType lowguid = sGroupMgr->GenerateGroupId();

    // 设置队伍基本属性
    m_guid = ObjectGuid(HighGuid::Group, lowguid);
    m_leaderGuid = leaderGuid;
    m_leaderName = leader->GetName();

    // 设置队长标志
    leader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    // 战场或战场群组使用特殊类型
    if (isBGGroup() || isBFGroup())
        m_groupType = GROUPTYPE_BGRAID;

    // 团队需要初始化子队伍计数器
    if (m_groupType & GROUPTYPE_RAID)
        _initRaidSubGroupsCounter();

    // 非随机副本队伍使用队伍分配方式
    if (!isLFGGroup())
        m_lootMethod = GROUP_LOOT;

    // 设置拾取阈值和拾取者
    m_lootThreshold = ITEM_QUALITY_UNCOMMON;
    m_looterGuid = leaderGuid;
    m_masterLooterGuid.Clear();

    // 设置默认难度
    m_dungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL;
    m_raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;

    // 非战场队伍需要保存到数据库
    if (!isBGGroup() && !isBFGroup())
    {
        // 使用队长的难度设置
        m_dungeonDifficulty = leader->GetDungeonDifficulty();
        m_raidDifficulty = leader->GetRaidDifficulty();

        // 生成数据库存储ID并注册
        m_dbStoreId = sGroupMgr->GenerateNewGroupDbStoreId();
        sGroupMgr->RegisterGroupDbStoreId(m_dbStoreId, this);

        // 构建并执行数据库插入语句
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GROUP);

        uint8 index = 0;
        stmt->setUInt32(index++, m_dbStoreId);
        stmt->setUInt32(index++, m_leaderGuid.GetCounter());
        stmt->setUInt8(index++, uint8(m_lootMethod));
        stmt->setUInt32(index++, m_looterGuid.GetCounter());
        stmt->setUInt8(index++, uint8(m_lootThreshold));
        // 保存8个目标图标
        stmt->setUInt64(index++, m_targetIcons[0].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[1].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[2].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[3].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[4].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[5].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[6].GetRawValue());
        stmt->setUInt64(index++, m_targetIcons[7].GetRawValue());
        stmt->setUInt8(index++, uint8(m_groupType));
        stmt->setUInt32(index++, uint8(m_dungeonDifficulty));
        stmt->setUInt32(index++, uint8(m_raidDifficulty));
        stmt->setUInt32(index++, m_masterLooterGuid.GetCounter());

        CharacterDatabase.Execute(stmt);

        // 将队长的副本绑定转移到队伍
        Group::ConvertLeaderInstancesToGroup(leader, this, false);

        // 添加队长为队伍成员
        bool addMemberResult = AddMember(leader);
        ASSERT(addMemberResult); // 如果新队伍无法添加队长，说明出现严重错误
    }
    else if (!AddMember(leader))
        return false;

    return true;
}

/**
 * @brief LoadGroupFromDB - 从数据库字段加载队伍数据
 * @param fields 数据库查询结果字段数组
 *
 * 主要流程：
 * 1. 从字段中读取队伍基本信息（ID、队长、拾取设置等）
 * 2. 加载目标图标设置
 * 3. 设置队伍类型和难度
 * 4. 如果是随机副本队伍，加载LFG相关数据
 */
void Group::LoadGroupFromDB(Field* fields)
{
    m_dbStoreId = fields[16].GetUInt32();
    m_guid = ObjectGuid(HighGuid::Group, sGroupMgr->GenerateGroupId());
    m_leaderGuid = ObjectGuid(HighGuid::Player, fields[0].GetUInt32());

    // 检查队长是否存在，不存在则直接返回
    if (!sCharacterCache->GetCharacterNameByGuid(m_leaderGuid, m_leaderName))
        return;

    // 加载拾取相关设置
    m_lootMethod = LootMethod(fields[1].GetUInt8());
    m_looterGuid = ObjectGuid(HighGuid::Player, fields[2].GetUInt32());
    m_lootThreshold = ItemQualities(fields[3].GetUInt8());

    // 加载8个目标图标
    for (uint8 i = 0; i < TARGET_ICONS_COUNT; ++i)
        m_targetIcons[i].Set(fields[4 + i].GetUInt64());

    // 设置队伍类型，团队需要初始化子队伍计数器
    m_groupType  = GroupType(fields[12].GetUInt8());
    if (m_groupType & GROUPTYPE_RAID)
        _initRaidSubGroupsCounter();

    // 加载副本难度设置，无效值使用默认值
    uint32 diff = fields[13].GetUInt8();
    if (diff >= MAX_DUNGEON_DIFFICULTY)
        m_dungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL;
    else
        m_dungeonDifficulty = Difficulty(diff);

    // 加载团队难度设置，无效值使用默认值
    uint32 r_diff = fields[14].GetUInt8();
    if (r_diff >= MAX_RAID_DIFFICULTY)
       m_raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
    else
       m_raidDifficulty = Difficulty(r_diff);

    // 设置主分配者
    m_masterLooterGuid = ObjectGuid(HighGuid::Player, fields[15].GetUInt32());

    // 如果是随机副本队伍，加载LFG数据
    if (m_groupType & GROUPTYPE_LFG)
        sLFGMgr->_LoadFromDB(fields, GetGUID());
}

/**
 * @brief LoadMemberFromDB - 从数据库加载队伍成员
 * @param guidLow 成员的低GUID
 * @param memberFlags 成员标志（助理、主坦等）
 * @param subgroup 子队伍编号（团队中的小队编号）
 * @param roles 随机副本角色（坦克、治疗、输出）
 *
 * 主要流程：
 * 1. 创建成员槽位并设置基本信息
 * 2. 验证成员角色是否存在，不存在则删除数据库记录
 * 3. 添加到成员列表并更新子队伍计数器
 * 4. 设置LFG成员信息
 */
void Group::LoadMemberFromDB(ObjectGuid::LowType guidLow, uint8 memberFlags, uint8 subgroup, uint8 roles)
{
    MemberSlot member;
    member.guid = ObjectGuid(HighGuid::Player, guidLow);

    // 跳过不存在的成员，并清理数据库记录
    if (!sCharacterCache->GetCharacterNameByGuid(member.guid, member.name))
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
        stmt->setUInt32(0, guidLow);
        CharacterDatabase.Execute(stmt);
        return;
    }

    // 设置成员属性
    member.group = subgroup;
    member.flags = memberFlags;
    member.roles = roles;

    // 添加到成员列表
    m_memberSlots.push_back(member);

    // 更新子队伍计数器
    SubGroupCounterIncrease(subgroup);

    // 设置LFG成员信息
    sLFGMgr->SetupGroupMember(member.guid, GetGUID());
}

/**
 * @brief ConvertToLFG - 将队伍转换为随机副本队伍
 *
 * 主要流程：
 * 1. 设置队伍类型为LFG类型
 * 2. 将拾取方式改为需求优先
 * 3. 更新数据库中的队伍类型
 * 4. 发送队伍更新通知
 */
void Group::ConvertToLFG()
{
    // 设置LFG类型标志
    m_groupType = GroupType(m_groupType | GROUPTYPE_LFG | GROUPTYPE_LFG_RESTRICTED);
    // LFG队伍使用需求优先拾取方式
    m_lootMethod = NEED_BEFORE_GREED;

    // 非战场队伍更新数据库
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_TYPE);
        stmt->setUInt8(0, uint8(m_groupType));
        stmt->setUInt32(1, m_dbStoreId);
        CharacterDatabase.Execute(stmt);
    }

    SendUpdate();
}

/**
 * @brief ConvertToRaid - 将普通小队转换为团队
 *
 * 主要流程：
 * 1. 设置队伍类型为团队
 * 2. 初始化子队伍计数器
 * 3. 更新数据库中的队伍类型
 * 4. 发送队伍更新通知
 * 5. 更新所有成员的任务相关游戏对象状态
 */
void Group::ConvertToRaid()
{
    // 设置团队类型标志
    m_groupType = GroupType(m_groupType | GROUPTYPE_RAID);

    // 初始化子队伍计数器
    _initRaidSubGroupsCounter();

    // 非战场队伍更新数据库
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_TYPE);
        stmt->setUInt8(0, uint8(m_groupType));
        stmt->setUInt32(1, m_dbStoreId);
        CharacterDatabase.Execute(stmt);
    }

    SendUpdate();

    // 更新任务相关的游戏对象状态（某些任务依赖团队成员资格）
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        if (Player* player = ObjectAccessor::FindPlayer(citr->guid))
            player->UpdateVisibleGameobjectsOrSpellClicks();
}

/**
 * @brief AddInvite - 添加邀请玩家加入队伍
 * @param player 被邀请的玩家指针
 * @return 邀请成功返回true，失败返回false
 *
 * 主要流程：
 * 1. 验证玩家有效性和是否已被邀请
 * 2. 检查玩家是否已有队伍（排除战场队伍）
 * 3. 移除玩家之前的邀请
 * 4. 添加到邀请列表并设置邀请标记
 * 5. 触发脚本事件
 */
bool Group::AddInvite(Player* player)
{
    // 验证玩家有效性
    if (!player || player->GetGroupInvite())
        return false;

    // 检查玩家是否已有队伍，战场队伍需要检查原始队伍
    Group* group = player->GetGroup();
    if (group && (group->isBGGroup() || group->isBFGroup()))
        group = player->GetOriginalGroup();
    if (group)
        return false;

    // 移除玩家之前的邀请
    RemoveInvite(player);

    // 添加到邀请列表
    m_invitees.insert(player);
    player->SetGroupInvite(this);

    // 触发脚本事件
    sScriptMgr->OnGroupInviteMember(this, player->GetGUID());

    return true;
}

/**
 * @brief AddLeaderInvite - 邀请玩家并设置其为队长
 * @param player 被邀请并设为队长的玩家指针
 * @return 邀请成功返回true，失败返回false
 *
 * 主要流程：
 * 1. 尝试添加邀请
 * 2. 设置该玩家为队长
 */
bool Group::AddLeaderInvite(Player* player)
{
    if (!AddInvite(player))
        return false;

    // 设置为队长
    m_leaderGuid = player->GetGUID();
    m_leaderName = player->GetName();
    return true;
}

/**
 * @brief RemoveInvite - 移除对玩家的邀请
 * @param player 要移除邀请的玩家指针
 *
 * 从邀请列表中移除玩家并清除其邀请标记
 */
void Group::RemoveInvite(Player* player)
{
    if (player)
    {
        m_invitees.erase(player);
        player->SetGroupInvite(nullptr);
    }
}

/**
 * @brief RemoveAllInvites - 移除所有待处理的邀请
 *
 * 清除所有被邀请玩家的邀请标记，并清空邀请列表
 */
void Group::RemoveAllInvites()
{
    // 清除所有被邀请玩家的邀请标记
    for (InvitesList::iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
        if (*itr)
            (*itr)->SetGroupInvite(nullptr);

    m_invitees.clear();
}

/**
 * @brief GetInvited - 通过GUID获取被邀请的玩家
 * @param guid 玩家GUID
 * @return 找到返回玩家指针，否则返回nullptr
 */
Player* Group::GetInvited(ObjectGuid guid) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetGUID() == guid)
            return (*itr);
    }
    return nullptr;
}

/**
 * @brief GetInvited - 通过名称获取被邀请的玩家
 * @param name 玩家名称
 * @return 找到返回玩家指针，否则返回nullptr
 */
Player* Group::GetInvited(const std::string& name) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetName() == name)
            return (*itr);
    }
    return nullptr;
}

/**
 * @brief AddMember - 添加成员到队伍
 * @param player 要添加的玩家指针
 * @return 添加成功返回true，失败返回false
 *
 * 主要流程：
 * 1. 查找可用的子队伍位置（团队有多个小队）
 * 2. 创建成员槽位并设置基本信息
 * 3. 设置玩家的队伍引用（区分战场队伍和普通队伍）
 * 4. 验证副本有效性
 * 5. 非团队重置目标图标
 * 6. 保存到数据库（非战场队伍）
 * 7. 发送更新通知并触发脚本事件
 * 8. 新成员重置副本、同步难度设置
 * 9. 广播成员数据更新
 * 10. 更新附魔技能等级上限
 */
bool Group::AddMember(Player* player)
{
    // 查找第一个未满的子队伍
    uint8 subGroup = 0;
    if (m_subGroupsCounts)
    {
        bool groupFound = false;
        for (; subGroup < MAX_RAID_SUBGROUPS; ++subGroup)
        {
            if (m_subGroupsCounts[subGroup] < MAX_GROUP_SIZE)
            {
                groupFound = true;
                break;
            }
        }
        // 团队没有空闲位置
        if (!groupFound)
            return false;
    }

    // 创建成员槽位
    MemberSlot member;
    member.guid      = player->GetGUID();
    member.name      = player->GetName();
    member.group     = subGroup;
    member.flags     = 0;
    member.roles     = 0;
    m_memberSlots.push_back(member);

    // 更新子队伍计数器
    SubGroupCounterIncrease(subGroup);

    // 清除邀请标记并设置队伍引用
    player->SetGroupInvite(nullptr);
    if (player->GetGroup())
    {
        // 玩家已在队伍中，添加到战场队伍
        if (isBGGroup() || isBFGroup())
            player->SetBattlegroundOrBattlefieldRaid(this, subGroup);
        else
            // 玩家在战场队伍中，设置原始队伍
            player->SetOriginalGroup(this, subGroup);
    }
    else
        // 玩家不在任何队伍中，直接设置队伍
        player->SetGroup(this, subGroup);

    // 如果玩家被同一队伍邀请回来，取消副本绑定计时器
    player->m_InstanceValid = player->CheckInstanceValidity(false);

    // 非团队重置目标图标
    if (!isRaidGroup())
    {
        for (uint8 i = 0; i < TARGET_ICONS_COUNT; ++i)
            m_targetIcons[i].Clear();
    }

    // 非战场队伍保存到数据库
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GROUP_MEMBER);
        stmt->setUInt32(0, m_dbStoreId);
        stmt->setUInt32(1, member.guid.GetCounter());
        stmt->setUInt8(2, member.flags);
        stmt->setUInt8(3, member.group);
        stmt->setUInt8(4, member.roles);
        CharacterDatabase.Execute(stmt);
    }

    // 发送更新通知并触发脚本事件
    SendUpdate();
    sScriptMgr->OnGroupAddMember(this, player->GetGUID());

    // 非队长新成员的处理
    if (!IsLeader(player->GetGUID()) && !isBGGroup() && !isBFGroup())
    {
        // reset the new member's instances, unless he is currently in one of them
        // including raid/heroic instances that they are not permanently bound to!
        player->ResetInstances(INSTANCE_RESET_GROUP_JOIN, false);
        player->ResetInstances(INSTANCE_RESET_GROUP_JOIN, true);

        if (player->GetLevel() >= LEVELREQUIREMENT_HEROIC)
        {
            if (player->GetDungeonDifficulty() != GetDungeonDifficulty())
            {
                player->SetDungeonDifficulty(GetDungeonDifficulty());
                player->SendDungeonDifficulty(true);
            }
            if (player->GetRaidDifficulty() != GetRaidDifficulty())
            {
                player->SetRaidDifficulty(GetRaidDifficulty());
                player->SendRaidDifficulty(true);
            }
        }
    }
    player->SetGroupUpdateFlag(GROUP_UPDATE_FULL);
    UpdatePlayerOutOfRange(player);

    // quest related GO state dependent from raid membership
    if (isRaidGroup())
        player->UpdateVisibleGameobjectsOrSpellClicks();

    {
        // 重置新成员的副本（不包括当前所在的副本）
        player->ResetInstances(INSTANCE_RESET_GROUP_JOIN, false);
        player->ResetInstances(INSTANCE_RESET_GROUP_JOIN, true);

        // 同步难度设置（满级玩家）
        if (player->GetLevel() >= LEVELREQUIREMENT_HEROIC)
        {
            if (player->GetDungeonDifficulty() != GetDungeonDifficulty())
            {
                player->SetDungeonDifficulty(GetDungeonDifficulty());
                player->SendDungeonDifficulty(true);
            }
            if (player->GetRaidDifficulty() != GetRaidDifficulty())
            {
                player->SetRaidDifficulty(GetRaidDifficulty());
                player->SendRaidDifficulty(true);
            }
        }
    }

    // 设置完整的队伍更新标志
    player->SetGroupUpdateFlag(GROUP_UPDATE_FULL);
    UpdatePlayerOutOfRange(player);

    // 团队成员资格影响任务相关游戏对象状态
    if (isRaidGroup())
        player->UpdateVisibleGameobjectsOrSpellClicks();

    // 广播新成员的队伍数据给其他成员
    {
        player->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);

        UpdateData groupData;
        WorldPacket groupDataPacket;

        // 广播其他成员的数据给新成员
        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (itr->GetSource() == player)
                continue;

            if (Player* existingMember = itr->GetSource())
            {
                // 如果新成员能看到该成员，发送更新
                if (player->HaveAtClient(existingMember))
                {
                    existingMember->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                    existingMember->BuildValuesUpdateBlockForPlayer(&groupData, player);
                    existingMember->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                }

                // 如果该成员能看到新成员，发送新成员数据
                if (existingMember->HaveAtClient(player))
                {
                    UpdateData newData;
                    WorldPacket newDataPacket;
                    player->BuildValuesUpdateBlockForPlayer(&newData, existingMember);
                    if (newData.HasData())
                    {
                        newData.BuildPacket(&newDataPacket);
                        existingMember->SendDirectMessage(&newDataPacket);
                    }
                }
            }
        }

        if (groupData.HasData())
        {
            groupData.BuildPacket(&groupDataPacket);
            player->SendDirectMessage(&groupDataPacket);
        }

        player->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
    }

    // 更新附魔技能等级上限
    if (m_maxEnchantingLevel < player->GetSkillValue(SKILL_ENCHANTING))
        m_maxEnchantingLevel = player->GetSkillValue(SKILL_ENCHANTING);

    return true;
}

/**
 * @brief RemoveMember - 从队伍中移除成员
 * @param guid 要移除的成员GUID
 * @param method 移除方式（默认、踢出、离开等）
 * @param kicker 执行踢出操作的玩家GUID（如果是踢出操作）
 * @param reason 移除原因字符串
 * @return 移除后队伍仍然存在返回true，队伍解散返回false
 *
 * 主要流程：
 * 1. 广播队伍更新并触发脚本事件
 * 2. 移除所有来自被移除成员的队伍增益效果
 * 3. LFG队伍的踢出由脚本处理
 * 4. 处理成员移除逻辑（发送数据包、更新数据库等）
 * 5. 如果被移除的是队长，选择新队长
 * 6. 检查是否需要解散队伍
 */
bool Group::RemoveMember(ObjectGuid guid, RemoveMethod const& method /*= GROUP_REMOVEMETHOD_DEFAULT*/, ObjectGuid kicker /*= ObjectGuid::Empty*/, char const* reason /*= nullptr*/)
{
    // 强制更新队伍数据
    BroadcastGroupUpdate();

    // 触发脚本事件
    sScriptMgr->OnGroupRemoveMember(this, guid, method, kicker, reason);

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (player)
    {
        // 移除所有来自被移除成员的队伍增益效果
        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* groupMember = itr->GetSource())
            {
                if (groupMember->GetGUID() == guid)
                    continue;

                groupMember->RemoveAllGroupBuffsFromCaster(guid);
                player->RemoveAllGroupBuffsFromCaster(groupMember->GetGUID());
            }
        }
    }

    // LFG队伍的投票踢出由脚本处理
    if (isLFGGroup() && method == GROUP_REMOVEMETHOD_KICK)
        return !m_memberSlots.empty();

    // 只有多于2名成员时才移除成员并更换队长（战场/LFG允许1人队伍）
    if (GetMembersCount() > ((isBGGroup() || isLFGGroup() || isBFGroup()) ? 1u : 2u))
    {
        if (player)
        {
            bool isOriginalGroup = false;

            // 战场队伍处理
            if (isBGGroup() || isBFGroup())
                player->RemoveFromBattlegroundOrBattlefieldRaid();
            else
            // 普通队伍处理
            {
                if (player->GetOriginalGroup() == this)
                {
                    player->SetOriginalGroup(nullptr);
                    isOriginalGroup = true;
                }
                else
                    player->SetGroup(nullptr);

                // 团队成员资格影响任务相关游戏对象状态
                player->UpdateVisibleGameobjectsOrSpellClicks();
            }

            WorldPacket data;

            // 发送被踢出消息
            if (method == GROUP_REMOVEMETHOD_KICK || method == GROUP_REMOVEMETHOD_KICK_LFG)
            {
                data.Initialize(SMSG_GROUP_UNINVITE, 0);
                player->SendDirectMessage(&data);
            }

            // 如果玩家有原始队伍（移除后），需要通知客户端更新队伍数据
            if (Group* group = player->GetGroup())
                group->SendUpdateToPlayer(player);
            else
            {
                data.Initialize(SMSG_GROUP_LIST, 1 + 1 + 1 + 1 + 8 + 4 + 4 + 8);
                data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
                data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
                player->SendDirectMessage(&data);
            }

            // 玩家离开原始队伍需要更新 Lua_GetReal** 函数报告的值
            if (isOriginalGroup)
            {
                data.Initialize(SMSG_REAL_GROUP_UPDATE, 1 + 4 + 8);
                data << uint8(0x10);
                data << uint32(0);
                data << uint64(0);
                player->SendDirectMessage(&data);
            }

            // 如果玩家在副本中，标记副本无效
            _homebindIfInstance(player);
        }

        // 从数据库中删除成员记录
        if (!isBGGroup() && !isBFGroup())
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
            stmt->setUInt32(0, guid.GetCounter());
            CharacterDatabase.Execute(stmt);
            DelinkMember(guid);
        }

        // 如果离队玩家有附魔技能或离线，重新评估队伍附魔等级
        if (!player || player->GetSkillValue(SKILL_ENCHANTING))
            ResetMaxEnchantingLevel();

        // 从所有掷骰中移除玩家
        for (Rolls::iterator it = RollId.begin(); it != RollId.end(); ++it)
        {
            Roll* roll = *it;
            Roll::PlayerVote::iterator itr2 = roll->playerVote.find(guid);
            if (itr2 == roll->playerVote.end())
                continue;

            // 更新掷骰计数
            if (itr2->second == GREED || itr2->second == DISENCHANT)
                --roll->totalGreed;
            else if (itr2->second == NEED)
                --roll->totalNeed;
            else if (itr2->second == PASS)
                --roll->totalPass;

            if (itr2->second != NOT_VALID)
                --roll->totalPlayersRolling;

            roll->playerVote.erase(itr2);

            // 处理掷骰结果
            CountRollVote(guid, roll->itemGUID, MAX_ROLL_TYPE);
        }

        // 更新子队伍计数器并移除成员槽位
        member_witerator slot = _getMemberWSlot(guid);
        if (slot != m_memberSlots.end())
        {
            SubGroupCounterDecrease(slot->group);
            m_memberSlots.erase(slot);
        }

        // 如果被移除的是队长，选择新队长
        if (m_leaderGuid == guid)
        {
            for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
            {
                if (ObjectAccessor::FindConnectedPlayer(itr->guid))
                {
                    ChangeLeader(itr->guid);
                    break;
                }
            }
        }

        SendUpdate();

        // LFG队伍只剩1人时的特殊处理
        if (isLFGGroup() && GetMembersCount() == 1)
        {
            Player* leader = ObjectAccessor::FindConnectedPlayer(GetLeaderGUID());
            uint32 mapId = sLFGMgr->GetDungeonMapId(GetGUID());
            if (!mapId || !leader || (leader->IsAlive() && leader->GetMapId() != mapId))
            {
                Disband();
                return false;
            }
        }

        // 成员数量不足时解散队伍
        if (m_memberMgr.getSize() < ((isLFGGroup() || isBGGroup()) ? 1u : 2u))
            Disband();

        return true;
    }
    // 移除前成员数量不足3人，直接解散
    else
    {
        Disband();
        return false;
    }
}

/**
 * @brief ChangeLeader - 更换队伍队长
 * @param newLeaderGuid 新队长的GUID
 *
 * 主要流程：
 * 1. 验证新队长是否在队伍中且在线
 * 2. 触发脚本事件
 * 3. 非战场队伍：处理副本绑定转移
 * 4. 更新旧队长和新队长的标志
 * 5. 更新队伍队长信息
 * 6. 广播队长更换消息
 */
void Group::ChangeLeader(ObjectGuid newLeaderGuid)
{
    member_witerator slot = _getMemberWSlot(newLeaderGuid);

    // 验证新队长是否在队伍中
    if (slot == m_memberSlots.end())
        return;

    Player* newLeader = ObjectAccessor::FindConnectedPlayer(slot->guid);

    // 不允许将队长转移给离线玩家
    if (!newLeader)
        return;

    // 触发脚本事件
    sScriptMgr->OnGroupChangeLeader(this, newLeaderGuid, m_leaderGuid);

    // 非战场队伍处理
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 移除队伍的永久副本绑定（已创建地图的实例不解除绑定）
        for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
        {
            for (BoundInstancesMap::iterator itr = m_boundInstances[i].begin(); itr != m_boundInstances[i].end();)
            {
                // 不解除已有地图创建的实例绑定（需要解散队伍才能强制创建新实例）
                if (itr->second.perm && !sMapMgr->FindMap(itr->first, itr->second.save->GetInstanceId()))
                {
                    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_INSTANCE_PERM_BINDING);
                    stmt->setUInt32(0, m_dbStoreId);
                    stmt->setUInt32(1, itr->second.save->GetInstanceId());
                    trans->Append(stmt);

                    itr->second.save->RemoveGroup(this);
                    m_boundInstances[i].erase(itr++);
                }
                else
                    ++itr;
            }
        }

        // 将新队长的永久绑定复制到队伍
        Group::ConvertLeaderInstancesToGroup(newLeader, this, true);

        // 更新数据库中的队长信息
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_LEADER);
        stmt->setUInt32(0, newLeader->GetGUID().GetCounter());
        stmt->setUInt32(1, m_dbStoreId);
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);
    }

    // 移除旧队长的队长标志
    if (Player* oldLeader = ObjectAccessor::FindConnectedPlayer(m_leaderGuid))
        oldLeader->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    // 设置新队长的队长标志
    newLeader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);
    m_leaderGuid = newLeader->GetGUID();
    m_leaderName = newLeader->GetName();

    // 移除助理标志（队长不能同时是助理）
    ToggleGroupMemberFlag(slot, MEMBER_FLAG_ASSISTANT, false);

    // 广播队长更换消息
    WorldPacket data(SMSG_GROUP_SET_LEADER, m_leaderName.size()+1);
    data << slot->name;
    BroadcastPacket(&data, true);
}

/**
 * @brief ConvertLeaderInstancesToGroup - 将玩家的副本绑定转移到队伍
 * @param player 源玩家指针
 * @param group 目标队伍指针
 * @param switchLeader 是否是更换队长操作
 *
 * 主要流程：
 * 1. 遍历所有难度的副本绑定
 * 2. 将有效绑定复制到队伍
 * 3. 更换队长时移除非永久绑定
 * 4. 处理无绑定的非团队副本（允许队伍"接管"实例）
 */
void Group::ConvertLeaderInstancesToGroup(Player* player, Group* group, bool switchLeader)
{
    // 复制所有绑定到队伍（更换队长时假设玩家没有单人绑定）
    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        for (Player::BoundInstancesMap::iterator itr = player->m_boundInstances[i].begin(); itr != player->m_boundInstances[i].end();)
        {
            // 非更换队长或队伍没有该绑定时，复制到队伍
            if (!switchLeader || !group->GetBoundInstance(itr->second.save->GetDifficulty(), itr->first))
                if (itr->second.extendState) // 未过期
                    group->BindToInstance(itr->second.save, itr->second.perm, false);

            // 更换队长时移除非永久绑定
            if (switchLeader && !itr->second.perm)
            {
                player->UnbindInstance(itr, Difficulty(i), false);
            }
            else
                ++itr;
        }
    }

    /* 如果队长在非团队副本地图中且没有人绑定到该地图，队伍可以"接管"该实例
     * 例如：两人队伍因断线解散，玩家在60秒内重连并重组队伍 */
    if (Map* playerMap = player->FindMap())
        if (!switchLeader && playerMap->IsNonRaidDungeon())
            if (InstanceSave* save = sInstanceSaveMgr->GetInstanceSave(playerMap->GetInstanceId()))
                if (save->GetGroupCount() == 0 && save->GetPlayerCount() == 0)
                {
                    TC_LOG_DEBUG("maps", "Group::ConvertLeaderInstancesToGroup: Group for player {} is taking over unbound instance map {} with Id {}", player->GetName(), playerMap->GetId(), playerMap->GetInstanceId());
                    // 如果没有人绑定到该实例，则不是永久绑定
                    group->BindToInstance(save, false, false);
                }
}

/**
 * @brief Disband - 解散队伍
 * @param hideDestroy 是否隐藏解散消息（默认false）
 *
 * 主要流程：
 * 1. 触发脚本事件
 * 2. 遍历所有成员，移除其队伍引用
 * 3. 发送解散消息给所有在线成员
 * 4. 清空成员列表和邀请列表
 * 5. 非战场队伍：从数据库删除队伍记录和成员记录
 * 6. 重置副本绑定
 * 7. 清理LFG数据
 * 8. 从管理器移除并删除队伍对象
 */
void Group::Disband(bool hideDestroy /* = false */)
{
    // 触发脚本事件
    sScriptMgr->OnGroupDisband(this);

    Player* player;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        player = ObjectAccessor::FindConnectedPlayer(citr->guid);
        if (!player)
            continue;

        bool isOriginalGroup = false;

        // 不能调用 _removeMember 因为会使成员迭代器失效
        // 如果是从战场队伍中移除玩家
        if (isBGGroup() || isBFGroup())
            player->RemoveFromBattlegroundOrBattlefieldRaid();
        else
        {
            // 可以从原始队伍中移除战场中的玩家
            if (player->GetOriginalGroup() == this)
            {
                player->SetOriginalGroup(nullptr);
                isOriginalGroup = true;
            }
            else
                player->SetGroup(nullptr);
        }

        // 团队成员资格影响任务相关游戏对象状态
        if (isRaidGroup())
            player->UpdateVisibleGameobjectsOrSpellClicks();

        if (!player->GetSession())
            continue;

        WorldPacket data;
        // 发送队伍解散消息
        if (!hideDestroy)
        {
            data.Initialize(SMSG_GROUP_DESTROYED, 0);
            player->SendDirectMessage(&data);
        }

        // 已移除玩家队伍引用，其 player->GetGroup() 是原始队伍，发送更新
        if (Group* group = player->GetGroup())
        {
            group->SendUpdate();
        }
        else
        {
            data.Initialize(SMSG_GROUP_LIST, 1+1+1+1+8+4+4+8);
            data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
            data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
            player->SendDirectMessage(&data);
        }

        // 玩家离开原始队伍需要更新 Lua_GetReal** 函数报告的值
        if (isOriginalGroup)
        {
            data.Initialize(SMSG_REAL_GROUP_UPDATE, 1 + 4 + 8);
            data << uint8(0x10);
            data << uint32(0);
            data << uint64(0);
            player->SendDirectMessage(&data);
        }

        // 如果玩家在副本中，标记副本无效
        _homebindIfInstance(player);
    }

    // 清空成员列表
    m_memberSlots.clear();

    // 清空邀请列表
    RemoveAllInvites();

    // 非战场队伍从数据库删除
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 删除队伍记录
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
        stmt->setUInt32(0, m_dbStoreId);
        trans->Append(stmt);

        // 删除所有成员记录
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
        stmt->setUInt32(0, m_dbStoreId);
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);

        // 重置副本绑定
        ResetInstances(INSTANCE_RESET_GROUP_DISBAND, false, nullptr);
        ResetInstances(INSTANCE_RESET_GROUP_DISBAND, true, nullptr);

        // 删除LFG数据
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_LFG_DATA);
        stmt->setUInt32(0, m_dbStoreId);
        CharacterDatabase.Execute(stmt);

        // 释放数据库存储ID
        sGroupMgr->FreeGroupDbStoreId(this);
    }

    // 从管理器移除并删除队伍对象
    sGroupMgr->RemoveGroup(this);
    delete this;
}

/*********************************************************/
/***                   拾取系统                        ***/
/*********************************************************/

/**
 * @brief SendLootStartRoll - 发送掷骰开始消息给所有参与玩家
 * @param countDown 倒计时时间（毫秒）
 * @param mapid 地图ID
 * @param r 掷骰对象引用
 *
 * 构建并发送 SMSG_LOOT_START_ROLL 数据包，包含物品信息和投票类型掩码
 * 只发送给尚未投票的玩家（NOT_EMITED_YET 状态）
 */
void Group::SendLootStartRoll(uint32 countDown, uint32 mapid, Roll const& r)
{
    WorldPacket data(SMSG_LOOT_START_ROLL, (8+4+4+4+4+4+4+1));
    data << uint64(r.itemGUID);                             // 掷骰物品的GUID
    data << uint32(mapid);                                  // 地图ID
    data << uint32(r.itemSlot);                             // 物品槽位
    data << uint32(r.itemid);                               // 物品模板ID
    data << uint32(r.itemRandomSuffix);                     // 随机后缀
    data << uint32(r.itemRandomPropId);                     // 随机属性ID
    data << uint32(r.itemCount);                            // 堆叠数量
    data << uint32(countDown);                              // 选择"需求"或"贪婪"的倒计时时间
    data << uint8(r.rollVoteMask);                          // 投票类型掩码

    // 发送给所有尚未投票的参与玩家
    for (Roll::PlayerVote::const_iterator itr=r.playerVote.begin(); itr != r.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
        if (!p || !p->GetSession())
            continue;

        if (itr->second == NOT_EMITED_YET)
            p->SendDirectMessage(&data);
    }
}

/**
 * @brief SendLootStartRollToPlayer - 发送掷骰开始消息给指定玩家
 * @param countDown 倒计时时间（毫秒）
 * @param mapId 地图ID
 * @param p 目标玩家指针
 * @param canNeed 玩家是否可以选择需求
 * @param r 掷骰对象引用
 *
 * 构建并发送 SMSG_LOOT_START_ROLL 数据包给单个玩家
 * 根据参数决定是否禁用需求选项
 */
void Group::SendLootStartRollToPlayer(uint32 countDown, uint32 mapId, Player* p, bool canNeed, Roll const& r)
{
    if (!p || !p->GetSession())
        return;

    WorldPacket data(SMSG_LOOT_START_ROLL, (8 + 4 + 4 + 4 + 4 + 4 + 4 + 1));
    data << uint64(r.itemGUID);                             // 掷骰物品的GUID
    data << uint32(mapId);                                  // 地图ID
    data << uint32(r.itemSlot);                             // 物品槽位
    data << uint32(r.itemid);                               // 物品模板ID
    data << uint32(r.itemRandomSuffix);                     // 随机后缀
    data << uint32(r.itemRandomPropId);                     // 随机属性ID
    data << uint32(r.itemCount);                            // 堆叠数量
    data << uint32(countDown);                              // 倒计时时间

    // 根据条件禁用需求选项
    uint8 voteMask = r.rollVoteMask;
    if (!canNeed)
        voteMask &= ~ROLL_FLAG_TYPE_NEED;
    data << uint8(voteMask);                                // 投票类型掩码

    p->SendDirectMessage(&data);
}

/**
 * @brief SendLootRoll - 发送玩家掷骰结果消息
 * @param sourceGuid 物品来源GUID
 * @param targetGuid 掷骰玩家GUID
 * @param rollNumber 掷骰点数
 * @param rollType 掷骰类型（需求/贪婪/放弃）
 * @param roll 掷骰对象引用
 * @param autoPass 是否自动放弃
 *
 * 广播玩家对物品的掷骰选择给所有有效参与者
 */
void Group::SendLootRoll(ObjectGuid sourceGuid, ObjectGuid targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll, bool autoPass)
{
    WorldPacket data(SMSG_LOOT_ROLL, (8+4+8+4+4+4+1+1+1));
    data << uint64(sourceGuid);                             // 物品GUID
    data << uint32(roll.itemSlot);                          // 槽位
    data << uint64(targetGuid);                             // 掷骰玩家GUID
    data << uint32(roll.itemid);                            // 物品模板ID
    data << uint32(roll.itemRandomSuffix);                  // 随机后缀
    data << uint32(roll.itemRandomPropId);                  // 随机属性ID
    data << uint8(rollNumber);                              // 掷骰点数（0:需求，>127:放弃）
    data << uint8(rollType);                                // 掷骰类型（0:需求，1:贪婪，2:分解）
    data << uint8(autoPass);                                // 自动放弃标志

    // 发送给所有有效参与者
    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
        if (!p || !p->GetSession())
            continue;

        if (itr->second != NOT_VALID)
            p->SendDirectMessage(&data);
    }
}

/**
 * @brief SendLootRollWon - 发送掷骰获胜消息
 * @param sourceGuid 物品来源GUID
 * @param targetGuid 获胜玩家GUID
 * @param rollNumber 获胜点数
 * @param rollType 获胜类型（需求/贪婪）
 * @param roll 掷骰对象引用
 *
 * 广播掷骰获胜结果给所有有效参与者
 */
void Group::SendLootRollWon(ObjectGuid sourceGuid, ObjectGuid targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ROLL_WON, (8+4+4+4+4+8+1+1));
    data << uint64(sourceGuid);                             // 物品GUID
    data << uint32(roll.itemSlot);                          // 槽位
    data << uint32(roll.itemid);                            // 物品模板ID
    data << uint32(roll.itemRandomSuffix);                  // 随机后缀
    data << uint32(roll.itemRandomPropId);                  // 随机属性ID
    data << uint64(targetGuid);                             // 获胜玩家GUID
    data << uint8(rollNumber);                              // 获胜点数
    data << uint8(rollType);                                // 获胜类型

    // 发送给所有有效参与者
    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
        if (!p || !p->GetSession())
            continue;

        if (itr->second != NOT_VALID)
            p->SendDirectMessage(&data);
    }
}

/**
 * @brief SendLootAllPassed - 发送所有玩家放弃掷骰消息
 * @param roll 掷骰对象引用
 *
 * 当所有参与掷骰的玩家都选择放弃时，广播此消息
 */
void Group::SendLootAllPassed(Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ALL_PASSED, (8+4+4+4+4));
    data << uint64(roll.itemGUID);                          // 物品GUID
    data << uint32(roll.itemSlot);                          // 物品槽位
    data << uint32(roll.itemid);                            // 物品模板ID
    data << uint32(roll.itemRandomPropId);                  // 随机属性ID
    data << uint32(roll.itemRandomSuffix);                  // 随机后缀ID

    // 发送给所有有效参与者
    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(itr->first);
        if (!player || !player->GetSession())
            continue;

        if (itr->second != NOT_VALID)
            player->SendDirectMessage(&data);
    }
}

/**
 * @brief SendLooter - 通知队伍成员指定生物的允许拾取者
 * @param creature 被拾取的生物
 * @param groupLooter 当前拾取者
 *
 * 发送 SMSG_LOOT_LIST 数据包，通知队伍成员谁可以拾取指定生物
 * 主分配模式下，超过阈值的物品只允许主分配者拾取
 */
void Group::SendLooter(Creature* creature, Player* groupLooter)
{
    ASSERT(creature);

    WorldPacket data(SMSG_LOOT_LIST, (8+8));
    data << uint64(creature->GetGUID());

    // 主分配模式下，超过阈值的物品只允许主分配者拾取
    if (GetLootMethod() == MASTER_LOOT && creature->loot.hasOverThresholdItem())
        data << GetMasterLooterGuid().WriteAsPacked();
    else
        data << uint8(0);

    // 设置当前拾取者
    if (groupLooter)
        data << groupLooter->GetPackGUID();
    else
        data << uint8(0);

    BroadcastPacket(&data, false);
}

/**
 * @brief CanRollOnItem - 检查玩家是否可以掷骰获取物品
 * @param item 战利品项
 * @param player 玩家指针
 * @return 可以掷骰返回true，否则返回false
 *
 * 检查条件：
 * 1. 物品模板存在
 * 2. 玩家未达到唯一物品的最大数量限制
 * 3. 物品允许该玩家获取
 */
bool CanRollOnItem(const LootItem& item, Player const* player)
{
    // 玩家已达到唯一物品的最大数量时不能掷骰
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
    if (!proto)
        return false;

    uint32 itemCount = player->GetItemCount(item.itemid, true);
    if (proto->MaxCount > 0 && static_cast<int32>(itemCount) >= proto->MaxCount)
        return false;

    // 检查物品是否允许该玩家获取
    if (!item.AllowedForPlayer(player))
        return false;

    return true;
}

/**
 * @brief GroupLoot - 队伍分配方式处理
 * @param loot 战利品对象指针
 * @param pLootedObject 被拾取的世界对象
 *
 * 主要流程：
 * 1. 遍历所有战利品项
 * 2. 对超过阈值的物品启动掷骰流程
 * 3. 收集附近队伍成员参与掷骰
 * 4. 发送掷骰开始消息
 * 5. 设置拾取计时器
 */
void Group::GroupLoot(Loot* loot, WorldObject* pLootedObject)
{
    std::vector<LootItem>::iterator i;
    ItemTemplate const* item;
    uint8 itemSlot = 0;

    for (i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        // 跳过每人一份的物品
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            continue;
        }

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            ObjectGuid newitemGUID = ObjectGuid::Create<HighGuid::Item>(sObjectMgr->GetGenerator<HighGuid::Item>().Generate());

            Roll* r = new Roll(newitemGUID, *i);

            //a vector is filled with only near party members
            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member || !member->GetSession())
                    continue;
                if (member->IsAtGroupRewardDistance(pLootedObject))
                {
                    r->totalPlayersRolling++;
                    RollVote vote = member->GetPassOnGroupLoot() ? PASS : NOT_EMITED_YET;
                    if (!CanRollOnItem(*i, member))
                    {
                        vote = PASS;
                        ++r->totalPass;
                    }
                    r->playerVote[member->GetGUID()] = vote;
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                loot->items[itemSlot].is_blocked = true;

                // If there are any "auto pass", broadcast them now
                if (r->totalPass)
                {
                    for (Roll::PlayerVote::const_iterator itr=r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                    {
                        Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
                        if (!p || !p->GetSession())
                            continue;

                        if (itr->second == PASS)
                            SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r, true);
                    }
                }

                if (r->totalPass == r->totalPlayersRolling)
                    delete r;
                else
                {
                    SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

                    RollId.push_back(r);

                    if (Creature* creature = pLootedObject->ToCreature())
                    {
                        creature->m_groupLootTimer = 60000;
                        creature->lootingGroupLowGUID = GetLowGUID();
                    }
                    else if (GameObject* go = pLootedObject->ToGameObject())
                    {
                        go->m_groupLootTimer = 60000;
                        go->lootingGroupLowGUID = GetLowGUID();
                    }
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            //TC_LOG_DEBUG("misc", "Group::GroupLoot: missing item prototype for item with id: {}", i->itemid);
            continue;
        }

        ObjectGuid newitemGUID = ObjectGuid::Create<HighGuid::Item>(sObjectMgr->GetGenerator<HighGuid::Item>().Generate());

        Roll* r = new Roll(newitemGUID, *i);

        //a vector is filled with only near party members
        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member || !member->GetSession())
                continue;

            if (member->IsAtGroupRewardDistance(pLootedObject))
            {
                r->totalPlayersRolling++;
                RollVote vote = NOT_EMITED_YET;
                if (!CanRollOnItem(*i, member))
                {
                    vote = PASS;
                    ++r->totalPass;
                }
                r->playerVote[member->GetGUID()] = vote;
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

            RollId.push_back(r);

            if (Creature* creature = pLootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = pLootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::NeedBeforeGreed(Loot* loot, WorldObject* lootedObject)
{
    ItemTemplate const* item;
    uint8 itemSlot = 0;
    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        ASSERT(item);

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            ObjectGuid newitemGUID = ObjectGuid::Create<HighGuid::Item>(sObjectMgr->GetGenerator<HighGuid::Item>().Generate());

            Roll* r = new Roll(newitemGUID, *i);

            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* playerToRoll = itr->GetSource();
                if (!playerToRoll || !playerToRoll->GetSession())
                    continue;

                if (playerToRoll->IsAtGroupRewardDistance(lootedObject))
                {
                    r->totalPlayersRolling++;
                    RollVote vote = playerToRoll->GetPassOnGroupLoot() ? PASS : NOT_EMITED_YET;
                    if (!CanRollOnItem(*i, playerToRoll))
                    {
                        vote = PASS;
                        r->totalPass++; // Can't broadcast the pass now. need to wait until all rolling players are known
                    }
                    r->playerVote[playerToRoll->GetGUID()] = vote;
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                if (item->HasFlag(ITEM_FLAG2_CAN_ONLY_ROLL_GREED))
                    r->rollVoteMask &= ~ROLL_FLAG_TYPE_NEED;

                loot->items[itemSlot].is_blocked = true;

                //Broadcast Pass and Send Rollstart
                for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                {
                    Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
                    if (!p || !p->GetSession())
                        continue;

                    if (itr->second == PASS)
                        SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                    else
                        SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, p->CanRollForItemInLFG(item, lootedObject) == EQUIP_ERR_OK, *r);
                }

                RollId.push_back(r);

                if (Creature* creature = lootedObject->ToCreature())
                {
                    creature->m_groupLootTimer = 60000;
                    creature->lootingGroupLowGUID = GetLowGUID();
                }
                else if (GameObject* go = lootedObject->ToGameObject())
                {
                    go->m_groupLootTimer = 60000;
                    go->lootingGroupLowGUID = GetLowGUID();
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        ObjectGuid newitemGUID = ObjectGuid::Create<HighGuid::Item>(sObjectMgr->GetGenerator<HighGuid::Item>().Generate());

        Roll* r = new Roll(newitemGUID, *i);

        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* playerToRoll = itr->GetSource();
            if (!playerToRoll || !playerToRoll->GetSession())
                continue;

            if (playerToRoll->IsAtGroupRewardDistance(lootedObject))
            {
                r->totalPlayersRolling++;
                RollVote vote = NOT_EMITED_YET;
                if (!CanRollOnItem(*i, playerToRoll))
                {
                    vote = PASS;
                    ++r->totalPass;
                }
                r->playerVote[playerToRoll->GetGUID()] = vote;
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            //Broadcast Pass and Send Rollstart
            for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
            {
                Player* p = ObjectAccessor::FindConnectedPlayer(itr->first);
                if (!p || !p->GetSession())
                    continue;

                if (itr->second == PASS)
                    SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                else
                    SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, p->CanRollForItemInLFG(item, lootedObject) == EQUIP_ERR_OK, *r);
            }

            RollId.push_back(r);

            if (Creature* creature = lootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = lootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::MasterLoot(Loot* loot, WorldObject* pLootedObject)
{
    TC_LOG_DEBUG("network", "Group::MasterLoot (SMSG_LOOT_MASTER_LIST)");

    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i)
    {
        if (i->freeforall)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i)
    {
        if (!i->follow_loot_rules)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    uint32 real_count = 0;

    WorldPacket data(SMSG_LOOT_MASTER_LIST, 1 + GetMembersCount() * 8);
    data << uint8(GetMembersCount());

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (!looter->IsInWorld())
            continue;

        if (looter->IsAtGroupRewardDistance(pLootedObject))
        {
            data << uint64(looter->GetGUID());
            ++real_count;
        }
    }

    data.put<uint8>(0, real_count);

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (looter->IsAtGroupRewardDistance(pLootedObject))
            looter->SendDirectMessage(&data);
    }
}

bool Group::CountRollVote(ObjectGuid playerGUID, ObjectGuid Guid, uint8 Choice)
{
    Rolls::iterator rollI = GetRoll(Guid);
    if (rollI == RollId.end())
        return false;
    Roll* roll = *rollI;

    Roll::PlayerVote::iterator itr = roll->playerVote.find(playerGUID);
    // this condition means that player joins to the party after roll begins
    if (itr == roll->playerVote.end() || itr->second != NOT_EMITED_YET)
        return false;

    if (roll->getLoot())
        if (roll->getLoot()->items.empty())
            return false;

    switch (Choice)
    {
        case ROLL_PASS:                                     // Player choose pass
            SendLootRoll(ObjectGuid::Empty, playerGUID, 128, ROLL_PASS, *roll);
            ++roll->totalPass;
            itr->second = PASS;
            break;
        case ROLL_NEED:                                     // player choose Need
            SendLootRoll(ObjectGuid::Empty, playerGUID, 0, 0, *roll);
            ++roll->totalNeed;
            itr->second = NEED;
            break;
        case ROLL_GREED:                                    // player choose Greed
            SendLootRoll(ObjectGuid::Empty, playerGUID, 128, ROLL_GREED, *roll);
            ++roll->totalGreed;
            itr->second = GREED;
            break;
        case ROLL_DISENCHANT:                               // player choose Disenchant
            SendLootRoll(ObjectGuid::Empty, playerGUID, 128, ROLL_DISENCHANT, *roll);
            ++roll->totalGreed;
            itr->second = DISENCHANT;
            break;
    }

    if (roll->totalPass + roll->totalNeed + roll->totalGreed >= roll->totalPlayersRolling)
        CountTheRoll(rollI, nullptr);

    return true;
}

//called when roll timer expires
void Group::EndRoll(Loot* pLoot, Map* allowedMap)
{
    for (Rolls::iterator itr = RollId.begin(); itr != RollId.end();)
    {
        if ((*itr)->getLoot() == pLoot) {
            CountTheRoll(itr, allowedMap);           //i don't have to edit player votes, who didn't vote ... he will pass
            itr = RollId.begin();
        }
        else
            ++itr;
    }
}

/**
 * @brief CountTheRoll - 统计掷骰结果并分配物品
 * @param rollI 掷骰对象的迭代器
 * @param allowedMap 允许的地图指针（用于过滤离线或换地图的玩家）
 *
 * 此方法是战利品掷骰系统的核心，负责统计所有投票并决定物品归属。
 *
 * 分配优先级：
 * 1. 需求（Need）- 最高优先级
 * 2. 贪婪（Greed）/ 分解（Disenchant）- 同优先级，随机决定
 * 3. 放弃（Pass）- 无优先级
 *
 * 主要流程：
 * 1. 检查掷骰对象是否有效
 * 2. 统计"需求"投票，随机点数决定获胜者
 * 3. 如果没有需求，统计"贪婪/分解"投票
 * 4. 将物品分配给获胜者
 * 5. 更新成就和日志
 * 6. 清理掷骰记录
 *
 * 特殊处理：
 * - 如果玩家离线或换地图，自动视为放弃
 * - 如果背包已满，物品留在尸体上
 * - 分解选项需要足够的附魔技能
 */
void Group::CountTheRoll(Rolls::iterator rollI, Map* allowedMap)
{
    Roll* roll = *rollI;
    if (!roll->isValid())                                   // is loot already deleted ?
    // 检查战利品是否已被删除
    {
        RollId.erase(rollI);
        delete roll;
        return;
    }

    //end of the roll
    // 处理"需求"投票
    if (roll->totalNeed > 0)
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;           // 最高点数
            ObjectGuid maxguid = ObjectGuid::Empty;  // 获胜者 GUID
            Player* player = nullptr;

            // 遍历所有投票
            for (Roll::PlayerVote::const_iterator itr = roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                if (itr->second != NEED)
                    continue;

                player = ObjectAccessor::FindPlayer(itr->first);
                // 检查玩家是否在线且在同一地图
                if (!player || (allowedMap != nullptr && player->FindMap() != allowedMap))
                {
                    --roll->totalNeed;  // 离线玩家不参与掷骰
                    continue;
                }

                // 生成 1-100 的随机点数
                uint8 randomN = urand(1, 100);
                SendLootRoll(ObjectGuid::Empty, itr->first, randomN, ROLL_NEED, *roll);
                // 记录最高点数
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                }
            }

            // 找到获胜者，分配物品
            if (!maxguid.IsEmpty())
            {
                SendLootRollWon(ObjectGuid::Empty, maxguid, maxresul, ROLL_NEED, *roll);
                player = ObjectAccessor::FindConnectedPlayer(maxguid);

                if (player && player->GetSession())
                {
                    // 更新掷骰成就
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT, roll->itemid, maxresul);

                    ItemPosCountVec dest;
                    LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
                    // 检查背包空间
                    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
                    if (msg == EQUIP_ERR_OK)
                    {
                        // 背包有空间，发放物品
                        item->is_looted = true;
                        roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                        roll->getLoot()->unlootedCount--;
                        player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, item->GetAllowedLooters());
                    }
                    else
                    {
                        // 背包已满，物品留在尸体上
                        item->is_blocked = false;
                        item->rollWinnerGUID = player->GetGUID();
                        player->SendEquipError(msg, nullptr, nullptr, roll->itemid);
                    }
                }
            }
            else
                roll->totalNeed = 0;
        }
    }

    // 如果没有需求，处理贪婪/分解投票
    if (roll->totalNeed == 0 && roll->totalGreed > 0) // if (roll->totalNeed == 0 && ...), not else if, because numbers can be modified above if player is on a different map
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;
            ObjectGuid maxguid = ObjectGuid::Empty;
            Player* player = nullptr;
            RollVote rollvote = NOT_VALID;

            Roll::PlayerVote::iterator itr;
            for (itr = roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                // 贪婪和分解同优先级
                if (itr->second != GREED && itr->second != DISENCHANT)
                    continue;

                player = ObjectAccessor::FindPlayer(itr->first);
                if (!player || (allowedMap != nullptr && player->FindMap() != allowedMap))
                {
                    --roll->totalGreed;
                    continue;
                }

                uint8 randomN = urand(1, 100);
                SendLootRoll(ObjectGuid::Empty, itr->first, randomN, itr->second, *roll);
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                    rollvote = itr->second;
                }
            }

            if (!maxguid.IsEmpty())
            {
                SendLootRollWon(ObjectGuid::Empty, maxguid, maxresul, rollvote, *roll);
                player = ObjectAccessor::FindConnectedPlayer(maxguid);

                if (player && player->GetSession())
                {
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT, roll->itemid, maxresul);

                    LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);

                    if (rollvote == GREED)
                    {
                        ItemPosCountVec dest;
                        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
                        if (msg == EQUIP_ERR_OK)
                        {
                            item->is_looted = true;
                            roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                            roll->getLoot()->unlootedCount--;
                            player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, item->GetAllowedLooters());
                        }
                        else
                        {
                            item->is_blocked = false;
                            item->rollWinnerGUID = player->GetGUID();
                            player->SendEquipError(msg, nullptr, nullptr, roll->itemid);
                        }
                    }
                    else if (rollvote == DISENCHANT)
                    {
                        item->is_looted = true;
                        roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                        roll->getLoot()->unlootedCount--;
                        ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(roll->itemid);
                        ASSERT(pProto);
                        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL, 13262); // Disenchant

                        ItemPosCountVec dest;
                        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
                        if (msg == EQUIP_ERR_OK)
                            player->AutoStoreLoot(pProto->DisenchantID, LootTemplates_Disenchant, true);
                        else // If the player's inventory is full, send the disenchant result in a mail.
                        {
                            Loot loot;
                            loot.FillLoot(pProto->DisenchantID, LootTemplates_Disenchant, player, true);

                            uint32 max_slot = loot.GetMaxSlotInLootFor(player);
                            for (uint32 i = 0; i < max_slot; ++i)
                            {
                                LootItem* lootItem = loot.LootItemInSlot(i, player);
                                player->SendEquipError(msg, nullptr, nullptr, lootItem->itemid);
                                player->SendItemRetrievalMail(lootItem->itemid, lootItem->count);
                            }
                        }
                    }
                }
            }
            else
                roll->totalGreed = 0;
        }
    }

    if (roll->totalNeed == 0 && roll->totalGreed == 0) // if, not else, because numbers can be modified above if player is on a different map
    {
        SendLootAllPassed(*roll);

        // remove is_blocked so that the item is lootable by all players
        LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
        item->is_blocked = false;
    }

    RollId.erase(rollI);
    delete roll;
}

/**
 * @brief SetTargetIcon - 设置目标图标
 * @param id 图标ID（0-7）
 * @param whoGuid 设置图标的玩家GUID
 * @param targetGuid 目标对象GUID
 *
 * 主要流程：
 * 1. 验证图标ID有效性
 * 2. 如果目标已存在其他图标上，先清除旧图标
 * 3. 更新图标状态并广播给所有成员
 */
void Group::SetTargetIcon(uint8 id, ObjectGuid whoGuid, ObjectGuid targetGuid)
{
    if (id >= TARGET_ICONS_COUNT)
        return;

    // 清除其他图标上的相同目标
    if (targetGuid)
        for (int i=0; i<TARGET_ICONS_COUNT; ++i)
            if (m_targetIcons[i] == targetGuid)
                SetTargetIcon(i, ObjectGuid::Empty, ObjectGuid::Empty);

    m_targetIcons[id] = targetGuid;

    // 广播目标图标更新
    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1+8+1+8));
    data << uint8(0);                                       // 设置目标模式
    data << uint64(whoGuid);
    data << uint8(id);
    data << uint64(targetGuid);
    BroadcastPacket(&data, true);
}

/**
 * @brief SendTargetIconList - 发送目标图标列表给指定会话
 * @param session 目标会话指针
 *
 * 发送当前所有目标图标设置给指定玩家
 */
void Group::SendTargetIconList(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1+TARGET_ICONS_COUNT*9));
    data << uint8(1);                                       // 列表模式

    for (uint8 i = 0; i < TARGET_ICONS_COUNT; ++i)
    {
        if (m_targetIcons[i].IsEmpty())
            continue;

        data << uint8(i);
        data << uint64(m_targetIcons[i]);
    }

    session->SendPacket(&data);
}

/**
 * @brief SendUpdate - 发送队伍更新给所有在线成员
 *
 * 遍历所有成员槽位，向在线玩家发送队伍更新数据包
 */
void Group::SendUpdate()
{
    for (member_witerator witr = m_memberSlots.begin(); witr != m_memberSlots.end(); ++witr)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(witr->guid);
        if (!player)
            continue;

        SendUpdateToPlayer(player, &(*witr));
    }
}

/**
 * @brief SendUpdateToPlayer - 发送队伍更新给指定玩家
 * @param player 目标玩家指针
 * @param slot 成员槽位指针（可选）
 *
 * 主要流程：
 * 1. 验证玩家队伍引用
 * 2. 构建队伍列表数据包
 * 3. 包含队伍类型、成员信息、拾取设置、难度等
 * 4. 发送给指定玩家
 */
void Group::SendUpdateToPlayer(Player const* player, MemberSlot const* slot /*= nullptr*/)
{
    // 验证玩家队伍引用
    if (player->GetGroup() != this)
    {
        if (player->GetOriginalGroup() == this)
            SendOriginalGroupUpdateToPlayer(player);

        return;
    }

    // 如果没有提供成员槽位，则查找
    if (!slot)
    {
        member_citerator citr = _getMemberCSlot(player->GetGUID());

        if (citr == m_memberSlots.end())
            return;

        slot = &(*citr);
    }

    // 构建队伍列表数据包
    WorldPacket data(SMSG_GROUP_LIST, (1+1+1+1+1+4+8+4+4+(GetMembersCount()-1)*(13+8+1+1+1+1)+8+1+8+1+1+1+1));
    data << uint8(m_groupType);                         // 队伍类型标志
    data << uint8(slot->group);                         // 子队伍编号
    data << uint8(slot->flags);                         // 成员标志
    data << uint8(slot->roles);                         // LFG角色

    // LFG队伍附加信息
    if (isLFGGroup())
    {
        data << uint8(sLFGMgr->GetState(m_guid) == lfg::LFG_STATE_FINISHED_DUNGEON ? 2 : 0); // 副本完成状态
        data << uint32(sLFGMgr->GetDungeon(m_guid));    // 副本ID
    }

    data << uint64(m_guid);
    data << uint32(m_counter++);                        // 计数器，每次发送递增
    data << uint32(GetMembersCount()-1);                // 其他成员数量

    // 发送其他成员信息
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        if (slot->guid == citr->guid)
            continue;

        Player* member = ObjectAccessor::FindConnectedPlayer(citr->guid);

        // 计算在线状态
        uint8 onlineState = (member && !member->GetSession()->PlayerLogout()) ? MEMBER_STATUS_ONLINE : MEMBER_STATUS_OFFLINE;
        onlineState = onlineState | ((isBGGroup() || isBFGroup()) ? MEMBER_STATUS_PVP : 0);

        data << citr->name;
        data << uint64(citr->guid);                     // GUID
        data << uint8(onlineState);                     // 在线状态
        data << uint8(citr->group);                     // 子队伍编号
        data << uint8(citr->flags);                     // 成员标志
        data << uint8(citr->roles);                     // LFG角色
    }

    data << uint64(m_leaderGuid);                       // 队长GUID

    // 如果有其他成员，发送拾取和难度设置
    if (GetMembersCount() - 1)
    {
        data << uint8(m_lootMethod);                    // 拾取方式

        if (m_lootMethod == MASTER_LOOT)
            data << uint64(m_masterLooterGuid);         // master looter guid
        else
            data << uint64(0);

        data << uint8(m_lootThreshold);                 // loot threshold
        data << uint8(m_dungeonDifficulty);             // Dungeon Difficulty
        data << uint8(m_raidDifficulty);                // Raid Difficulty
        data << uint8(m_raidDifficulty >= RAID_DIFFICULTY_10MAN_HEROIC);    // 3.3 Dynamic Raid Difficulty - 0 normal/1 heroic
    }

    player->SendDirectMessage(&data);
}

void Group::SendOriginalGroupUpdateToPlayer(Player const* player) const
{
    WorldPacket data(SMSG_REAL_GROUP_UPDATE, 1 + 4 + 8);
    data << uint8(m_groupType);
    data << uint32(GetMembersCount() - 1);
    data << uint64(m_leaderGuid);
    player->SendDirectMessage(&data);
}

/**
 * @brief UpdatePlayerOutOfRange - 更新超出范围的玩家数据
 * @param player 目标玩家指针
 *
 * 向不在同一地图或超出视野范围的队伍成员发送玩家状态更新
 */
void Group::UpdatePlayerOutOfRange(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    WorldPacket data;
    player->GetSession()->BuildPartyMemberStatsChangedPacket(player, &data);

    Player* member;
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        member = itr->GetSource();
        // 发送给不在同一地图或超出视野范围的成员
        if (member && member != player && (!member->IsInMap(player) || !member->IsWithinDist(player, member->GetSightRange(), false)))
            member->SendDirectMessage(&data);
    }
}

/**
 * @brief BroadcastPacket - 广播数据包给队伍成员
 * @param packet 数据包指针
 * @param ignorePlayersInBGRaid 是否忽略战场队伍中的玩家
 * @param group 指定子队伍编号（-1为所有）
 * @param ignoredPlayer 忽略的玩家GUID
 *
 * 向符合条件的队伍成员广播数据包
 */
void Group::BroadcastPacket(WorldPacket const* packet, bool ignorePlayersInBGRaid, int group /*= -1*/, ObjectGuid ignoredPlayer /*= ObjectGuid::Empty*/)
{
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        // 过滤无效玩家和忽略的玩家
        if (!player || (!ignoredPlayer.IsEmpty() && player->GetGUID() == ignoredPlayer) || (ignorePlayersInBGRaid && player->GetGroup() != this))
            continue;

        // 检查子队伍和会话有效性
        if (player->GetSession() && (group == -1 || itr->getSubGroup() == group))
            player->SendDirectMessage(packet);
    }
}

/**
 * @brief BroadcastReadyCheck - 广播就绪检查数据包
 * @param packet 数据包指针
 *
 * 只发送给队长和助理
 */
void Group::BroadcastReadyCheck(WorldPacket const* packet)
{
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (player && player->GetSession())
            // 只发送给队长和助理
            if (IsLeader(player->GetGUID()) || IsAssistant(player->GetGUID()))
                player->SendDirectMessage(packet);
    }
}

/**
 * @brief OfflineReadyCheck - 检查离线玩家的就绪状态
 *
 * 为所有离线或无会话的成员发送未就绪状态
 */
void Group::OfflineReadyCheck()
{
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(citr->guid);
        // 离线或无会话的成员标记为未就绪
        if (!player || !player->GetSession())
        {
            WorldPacket data(MSG_RAID_READY_CHECK_CONFIRM, 9);
            data << uint64(citr->guid);
            data << uint8(0);  // 未就绪
            BroadcastReadyCheck(&data);
        }
    }
}

/**
 * @brief _setMembersGroup - 设置成员的子队伍编号
 * @param guid 成员GUID
 * @param group 子队伍编号
 * @return 设置成功返回true，失败返回false
 *
 * 内部函数，用于设置成员所属的子队伍并更新计数器
 */
bool Group::_setMembersGroup(ObjectGuid guid, uint8 group)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return false;

    slot->group = group;

    SubGroupCounterIncrease(group);

    // 非战场队伍更新数据库
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_MEMBER_SUBGROUP);
        stmt->setUInt8(0, group);
        stmt->setUInt32(1, guid.GetCounter());

        CharacterDatabase.Execute(stmt);
    }

    return true;
}

bool Group::SameSubGroup(Player const* member1, Player const* member2) const
{
    if (!member1 || !member2)
        return false;

    if (member1->GetGroup() != this || member2->GetGroup() != this)
        return false;
    else
        return member1->GetSubGroup() == member2->GetSubGroup();
}

// Allows setting sub groups both for online or offline members
void Group::ChangeMembersGroup(ObjectGuid guid, uint8 group)
{
    // Only raid groups have sub groups
    if (!isRaidGroup())
        return;

    // Check if player is really in the raid
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    // Abort if the player is already in the target sub group
    uint8 prevSubGroup = GetMemberGroup(guid);
    if (prevSubGroup == group)
        return;

    // Update the player slot with the new sub group setting
    slot->group = group;

    // Increase the counter of the new sub group..
    SubGroupCounterIncrease(group);

    // ..and decrease the counter of the previous one
    SubGroupCounterDecrease(prevSubGroup);

    // Preserve new sub group in database for non-raid groups
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_MEMBER_SUBGROUP);

        stmt->setUInt8(0, group);
        stmt->setUInt32(1, guid.GetCounter());

        CharacterDatabase.Execute(stmt);
    }

    // In case the moved player is online, update the player object with the new sub group references
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
    {
        if (player->GetGroup() == this)
            player->GetGroupRef().setSubGroup(group);
        else
        {
            // If player is in BG raid, it is possible that he is also in normal raid - and that normal raid is stored in m_originalGroup reference
            prevSubGroup = player->GetOriginalSubGroup();
            player->GetOriginalGroupRef().setSubGroup(group);
        }
    }

    // Broadcast the changes to the group
    SendUpdate();
}

// Retrieve the next Round-Roubin player for the group
//
// No update done if loot method is FFA.
//
// If the RR player is not yet set for the group, the first group member becomes the round-robin player.
// If the RR player is set, the next player in group becomes the round-robin player.
//
// If ifneed is true,
//      the current RR player is checked to be near the looted object.
//      if yes, no update done.
//      if not, he loses his turn.
void Group::UpdateLooterGuid(WorldObject* pLootedObject, bool ifneed)
{
    // round robin style looting applies for all low
    // quality items in each loot method except free for all
    if (GetLootMethod() == FREE_FOR_ALL)
        return;

    ObjectGuid oldLooterGUID = GetLooterGuid();
    member_citerator guid_itr = _getMemberCSlot(oldLooterGUID);
    if (guid_itr != m_memberSlots.end())
    {
        if (ifneed)
        {
            // not update if only update if need and ok
            Player* looter = ObjectAccessor::FindPlayer(guid_itr->guid);
            if (looter && looter->IsAtGroupRewardDistance(pLootedObject))
                return;
        }
        ++guid_itr;
    }

    // search next after current
    Player* pNewLooter = nullptr;
    for (member_citerator itr = guid_itr; itr != m_memberSlots.end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
            if (player->IsAtGroupRewardDistance(pLootedObject))
            {
                pNewLooter = player;
                break;
            }
    }

    if (!pNewLooter)
    {
        // search from start
        for (member_citerator itr = m_memberSlots.begin(); itr != guid_itr; ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
                if (player->IsAtGroupRewardDistance(pLootedObject))
                {
                    pNewLooter = player;
                    break;
                }
        }
    }

    if (pNewLooter)
    {
        if (oldLooterGUID != pNewLooter->GetGUID())
        {
            SetLooterGuid(pNewLooter->GetGUID());
            SendUpdate();
        }
    }
    else
    {
        SetLooterGuid(ObjectGuid::Empty);
        SendUpdate();
    }
}

/**
 * @brief CanJoinBattlegroundQueue - 检查队伍是否可以加入战场队列
 * @param bgOrTemplate 战场模板
 * @param bgQueueTypeId 战场队列类型ID
 * @param MinPlayerCount 最少玩家数量
 * @param MaxPlayerCount 最大玩家数量（未使用）
 * @param isRated 是否是积分赛
 * @param arenaSlot 竞技场槽位
 * @return 返回战场加入结果码
 *
 * 主要检查项：
 * 1. 队伍类型（LFG队伍不能加入）
 * 2. 队伍人数限制
 * 3. 成员阵营一致性
 * 4. 成员等级区间一致性
 * 5. 成员状态（离线、逃兵、队列限制等）
 */
GroupJoinBattlegroundResult Group::CanJoinBattlegroundQueue(Battleground const* bgOrTemplate, BattlegroundQueueTypeId bgQueueTypeId, uint32 MinPlayerCount, uint32 /*MaxPlayerCount*/, bool isRated, uint32 arenaSlot)
{
    // LFG队伍不能加入战场
    if (isLFGGroup())
        return ERR_LFG_CANT_USE_BATTLEGROUND;

    BattlemasterListEntry const* bgEntry = sBattlemasterListStore.LookupEntry(bgOrTemplate->GetTypeID());
    if (!bgEntry)
        return ERR_GROUP_JOIN_BATTLEGROUND_FAIL;            // 不应该发生

    // 检查人数限制
    uint32 memberscount = GetMembersCount();

    if (memberscount > bgEntry->MaxGroupSize)
        return ERR_BATTLEGROUND_NONE;                        // 客户端处理 ERR_GROUP_JOIN_BATTLEGROUND_TOO_MANY

    // 获取参考玩家用于比较（竞技场队伍ID、基于等级的队列ID等）
    Player* reference = ASSERT_NOTNULL(GetFirstMember())->GetSource();
    if (!reference)
        return ERR_BATTLEGROUND_JOIN_FAILED;

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgOrTemplate->GetMapId(), reference->GetLevel());
    if (!bracketEntry)
        return ERR_BATTLEGROUND_JOIN_FAILED;

    uint32 arenaTeamId = reference->GetArenaTeamId(arenaSlot);
    uint32 team = reference->GetTeam();

    BattlegroundQueueTypeId bgQueueTypeIdRandom = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RB, 0);

    // 检查每个成员是否可以加入
    memberscount = 0;
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next(), ++memberscount)
    {
        Player* member = itr->GetSource();
        // 离线成员不能加入
        if (!member)
            return ERR_BATTLEGROUND_JOIN_FAILED;
        // RBAC权限检查
        if (!member->CanJoinToBattleground(bgOrTemplate))
            return ERR_BATTLEGROUND_JOIN_TIMED_OUT;
        // 不允许跨阵营组队
        if (member->GetTeam() != team)
            return ERR_BATTLEGROUND_JOIN_TIMED_OUT;
        // 等级区间不一致
        PvPDifficultyEntry const* memberBracketEntry = GetBattlegroundBracketByLevel(bracketEntry->MapID, member->GetLevel());
        if (memberBracketEntry != bracketEntry)
            return ERR_BATTLEGROUND_JOIN_RANGE_INDEX;
        // 积分赛队伍ID不匹配
        if (isRated && member->GetArenaTeamId(arenaSlot) != arenaTeamId)
            return ERR_BATTLEGROUND_JOIN_FAILED;
        // 已在战场队列中
        if (member->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
            return ERR_BATTLEGROUND_JOIN_FAILED;
        // 已在随机战场队列中
        if (bgOrTemplate->GetTypeID() != BATTLEGROUND_AA && member->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeIdRandom))
            return ERR_IN_RANDOM_BG;
        // 随机战场时已在其他队列
        if (bgOrTemplate->GetTypeID() == BATTLEGROUND_RB && member->InBattlegroundQueue(true))
            return ERR_IN_NON_RANDOM_BG;
        // 逃兵debuff（非竞技场）
        if (bgOrTemplate->GetTypeID() != BATTLEGROUND_AA && member->IsDeserter())
            return ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS;
        // 战场队列已满
        if (!member->HasFreeBattlegroundQueueId())
            return ERR_BATTLEGROUND_TOO_MANY_QUEUES;
        // 使用副本系统
        if (member->isUsingLfg())
            return ERR_LFG_CANT_USE_BATTLEGROUND;
        // 冻结debuff
        if (member->HasAura(9454))
            return ERR_BATTLEGROUND_JOIN_FAILED;
    }

    // 竞技场检查人数
    if (bgOrTemplate->isArena() && memberscount != MinPlayerCount)
        return ERR_ARENA_TEAM_PARTY_SIZE;

    return GroupJoinBattlegroundResult(bgOrTemplate->GetTypeID());
}

//===================================================
//============== 掷骰系统 ===========================
//===================================================

/**
 * @brief targetObjectBuildLink - 构建目标对象链接
 *
 * 掷骰对象链接到战利品对象的回调函数
 */
void Roll::targetObjectBuildLink()
{
    // 从 link() 调用
    getTarget()->addLootValidatorRef(this);
}

/**
 * @brief SetDungeonDifficulty - 设置副本难度
 * @param difficulty 难度值
 *
 * 更新副本难度并保存到数据库
 */
void Group::SetDungeonDifficulty(Difficulty difficulty)
{
    m_dungeonDifficulty = difficulty;
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_DIFFICULTY);

        stmt->setUInt8(0, uint8(m_dungeonDifficulty));
        stmt->setUInt32(1, m_dbStoreId);

        CharacterDatabase.Execute(stmt);
    }

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (!player->GetSession())
            continue;

        player->SetDungeonDifficulty(difficulty);
        player->SendDungeonDifficulty(true);
    }
}

/**
 * @brief SetRaidDifficulty - 设置团队难度
 * @param difficulty 难度值
 *
 * 更新团队难度并保存到数据库，通知所有在线成员
 */
void Group::SetRaidDifficulty(Difficulty difficulty)
{
    m_raidDifficulty = difficulty;
    if (!isBGGroup() && !isBFGroup())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_RAID_DIFFICULTY);
        stmt->setUInt8(0, uint8(m_raidDifficulty));
        stmt->setUInt32(1, m_dbStoreId);
        CharacterDatabase.Execute(stmt);
    }

    // 通知所有在线成员
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (!player->GetSession())
            continue;

        player->SetRaidDifficulty(difficulty);
        player->SendRaidDifficulty(true);
    }
}

/**
 * @brief InCombatToInstance - 检查队伍是否在指定副本中战斗
 * @param instanceId 副本实例ID
 * @return 在战斗中返回true，否则返回false
 *
 * 检查队伍成员是否在指定副本中与特定怪物战斗
 */
bool Group::InCombatToInstance(uint32 instanceId)
{
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        // 检查玩家是否在该副本中且有攻击者
        if (player && player->GetInstanceId() == instanceId && !player->getAttackers().empty() && (player->GetMap()->IsRaidOrHeroicDungeon()))
            for (std::set<Unit*>::const_iterator i = player->getAttackers().begin(); i != player->getAttackers().end(); ++i)
                // 检查攻击者是否是需要副本绑定的怪物
                if ((*i) && (*i)->GetTypeId() == TYPEID_UNIT && (*i)->ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND)
                    return true;
    }
    return false;
}

void Group::ResetInstances(uint8 method, bool isRaid, Player* SendMsgTo)
{
    if (isBGGroup() || isBFGroup())
        return;

    // method can be INSTANCE_RESET_ALL, INSTANCE_RESET_CHANGE_DIFFICULTY, INSTANCE_RESET_GROUP_DISBAND

    // we assume that when the difficulty changes, all instances that can be reset will be
    Difficulty diff = GetDifficulty(isRaid);

    for (BoundInstancesMap::iterator itr = m_boundInstances[diff].begin(); itr != m_boundInstances[diff].end();)
    {
        InstanceSave* instanceSave = itr->second.save;
        MapEntry const* entry = sMapStore.LookupEntry(itr->first);
        if (!entry || entry->IsRaid() != isRaid || (!instanceSave->CanReset() && method != INSTANCE_RESET_GROUP_DISBAND))
        {
            ++itr;
            continue;
        }

        if (method == INSTANCE_RESET_ALL)
        {
            // the "reset all instances" method can only reset normal maps
            if (entry->InstanceType == MAP_RAID || diff == DUNGEON_DIFFICULTY_HEROIC)
            {
                ++itr;
                continue;
            }
        }

        bool isEmpty = true;
        // if the map is loaded, reset it
        Map* map = sMapMgr->FindMap(instanceSave->GetMapId(), instanceSave->GetInstanceId());
        if (map && map->IsDungeon() && !(method == INSTANCE_RESET_GROUP_DISBAND && !instanceSave->CanReset()))
        {
            if (instanceSave->CanReset())
                isEmpty = ((InstanceMap*)map)->Reset(method);
            else
                isEmpty = !map->HavePlayers();
        }

        if (SendMsgTo)
        {
            if (!isEmpty)
                SendMsgTo->SendResetInstanceFailed(0, instanceSave->GetMapId());
            else if (sWorld->getBoolConfig(CONFIG_INSTANCES_RESET_ANNOUNCE))
            {
                if (Group* group = SendMsgTo->GetGroup())
                {
                    for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
                        if (Player* player = groupRef->GetSource())
                            player->SendResetInstanceSuccess(instanceSave->GetMapId());
                }

                else
                    SendMsgTo->SendResetInstanceSuccess(instanceSave->GetMapId());
            }
            else
                SendMsgTo->SendResetInstanceSuccess(instanceSave->GetMapId());
        }

        if (isEmpty || method == INSTANCE_RESET_GROUP_DISBAND || method == INSTANCE_RESET_CHANGE_DIFFICULTY)
        {
            // do not reset the instance, just unbind if others are permanently bound to it
            if (isEmpty && instanceSave->CanReset())
            {
                if (map && map->IsDungeon() && SendMsgTo)
                {
                    AreaTrigger const * const instanceEntrance = sObjectMgr->GetGoBackTrigger(map->GetId());

                    if (!instanceEntrance)
                        TC_LOG_DEBUG("root", "Instance entrance not found for maps {}", map->GetId());
                    else
                    {
                        WorldSafeLocsEntry const * graveyardLocation = sObjectMgr->GetClosestGraveyard(instanceEntrance->target_X, instanceEntrance->target_Y, instanceEntrance->target_Z, instanceEntrance->target_mapId, SendMsgTo->GetTeam());
                        uint32 const zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, graveyardLocation->Continent, graveyardLocation->Loc.X, graveyardLocation->Loc.Y, graveyardLocation->Loc.Z);

                        for (MemberSlot const& member : GetMemberSlots())
                        {
                            if (!ObjectAccessor::FindConnectedPlayer(member.guid))
                            {
                                CharacterDatabasePreparedStatement*stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_POSITION_BY_MAPID);

                                stmt->setFloat(0, graveyardLocation->Loc.X);
                                stmt->setFloat(1, graveyardLocation->Loc.Y);
                                stmt->setFloat(2, graveyardLocation->Loc.Z);
                                stmt->setFloat(3, instanceEntrance->target_Orientation);
                                stmt->setUInt32(4, graveyardLocation->Continent);
                                stmt->setUInt32(5, zoneId);
                                stmt->setUInt32(6, member.guid.GetCounter());
                                stmt->setUInt32(7, map->GetId());

                                CharacterDatabase.Execute(stmt);
                            }
                        }
                    }
                }

                instanceSave->DeleteFromDB();
            }
            else
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_INSTANCE_BY_INSTANCE);

                stmt->setUInt32(0, instanceSave->GetInstanceId());

                CharacterDatabase.Execute(stmt);
            }

            // i don't know for sure if hash_map iterators
            m_boundInstances[diff].erase(itr);
            itr = m_boundInstances[diff].begin();
            // this unloads the instance save unless online players are bound to it
            // (eg. permanent binds or GM solo binds)
            instanceSave->RemoveGroup(this);
        }
        else
            ++itr;
    }
}

/**
 * @brief GetBoundInstance - 获取玩家当前地图的副本绑定
 * @param player 玩家指针
 * @return 副本绑定指针，无绑定返回nullptr
 */
InstanceGroupBind* Group::GetBoundInstance(Player* player)
{
    uint32 mapid = player->GetMapId();
    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    return GetBoundInstance(mapEntry);
}

/**
 * @brief GetBoundInstance - 获取指定地图的副本绑定
 * @param aMap 地图指针
 * @return 副本绑定指针，无绑定返回nullptr
 */
InstanceGroupBind* Group::GetBoundInstance(Map* aMap)
{
    // 当前生成编号与地图难度相同
    Difficulty difficulty = GetDifficulty(aMap->IsRaid());
    return GetBoundInstance(difficulty, aMap->GetId());
}

/**
 * @brief GetBoundInstance - 获取指定地图条目的副本绑定
 * @param mapEntry 地图条目
 * @return 副本绑定指针，无绑定返回nullptr
 */
InstanceGroupBind* Group::GetBoundInstance(MapEntry const* mapEntry)
{
    if (!mapEntry || !mapEntry->IsDungeon())
        return nullptr;

    Difficulty difficulty = GetDifficulty(mapEntry->IsRaid());
    return GetBoundInstance(difficulty, mapEntry->ID);
}

/**
 * @brief GetBoundInstance - 获取指定难度和地图ID的副本绑定
 * @param difficulty 难度
 * @param mapId 地图ID
 * @return 副本绑定指针，无绑定返回nullptr
 */
InstanceGroupBind* Group::GetBoundInstance(Difficulty difficulty, uint32 mapId)
{
    // 某些副本只有一个难度
    GetDownscaledMapDifficultyData(mapId, difficulty);

    BoundInstancesMap::iterator itr = m_boundInstances[difficulty].find(mapId);
    if (itr != m_boundInstances[difficulty].end())
        return &itr->second;
    else
        return nullptr;
}

/**
 * @brief BindToInstance - 将队伍绑定到副本
 * @param save 副本存档指针
 * @param permanent 是否永久绑定
 * @param load 是否是加载操作（不写数据库）
 * @return 副本绑定指针
 *
 * 主要流程：
 * 1. 验证参数有效性
 * 2. 非加载操作时更新数据库
 * 3. 更新存档引用计数
 * 4. 设置绑定信息
 */
InstanceGroupBind* Group::BindToInstance(InstanceSave* save, bool permanent, bool load)
{
    if (!save || isBGGroup() || isBFGroup())
        return nullptr;

    InstanceGroupBind& bind = m_boundInstances[save->GetDifficulty()][save->GetMapId()];
    // 非加载操作且绑定发生变化时更新数据库
    if (!load && (!bind.save || permanent != bind.perm || save != bind.save))
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_GROUP_INSTANCE);
        stmt->setUInt32(0, m_dbStoreId);
        stmt->setUInt32(1, save->GetInstanceId());
        stmt->setBool(2, permanent);
        CharacterDatabase.Execute(stmt);
    }

    // 更新存档引用
    if (bind.save != save)
    {
        if (bind.save)
            bind.save->RemoveGroup(this);
        save->AddGroup(this);
    }

    bind.save = save;
    bind.perm = permanent;
    if (!load)
        TC_LOG_DEBUG("maps", "Group::BindToInstance: {}, storage id: {} is now bound to map {}, instance {}, difficulty {}",
            GetGUID().ToString(), m_dbStoreId, save->GetMapId(), save->GetInstanceId(), static_cast<uint32>(save->GetDifficulty()));

    return &bind;
}

/**
 * @brief UnbindInstance - 解除队伍的副本绑定
 * @param mapid 地图ID
 * @param difficulty 难度
 * @param unload 是否是卸载操作（不写数据库）
 *
 * 解除指定地图和难度的副本绑定
 */
void Group::UnbindInstance(uint32 mapid, uint8 difficulty, bool unload)
{
    BoundInstancesMap::iterator itr = m_boundInstances[difficulty].find(mapid);
    if (itr != m_boundInstances[difficulty].end())
    {
        // 非卸载操作时删除数据库记录
        if (!unload)
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_INSTANCE_BY_GUID);
            stmt->setUInt32(0, m_dbStoreId);
            stmt->setUInt32(1, itr->second.save->GetInstanceId());
            CharacterDatabase.Execute(stmt);
        }

        itr->second.save->RemoveGroup(this);                // save can become invalid
        m_boundInstances[difficulty].erase(itr);
    }
}

void Group::_homebindIfInstance(Player* player)
{
    if (player && !player->IsGameMaster() && sMapStore.LookupEntry(player->GetMapId())->IsDungeon())
        player->m_InstanceValid = false;
}

void Group::BroadcastGroupUpdate(void)
{
    // FG: HACK: force flags update on group leave - for values update hack
    // -- not very efficient but safe
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* pp = ObjectAccessor::FindPlayer(citr->guid);
        if (pp)
        {
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_BYTES_2);
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
            TC_LOG_DEBUG("misc", "-- Forced group value update for '{}'", pp->GetName());
        }
    }
}

void Group::ResetMaxEnchantingLevel()
{
    m_maxEnchantingLevel = 0;
    Player* member = nullptr;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        member = ObjectAccessor::FindPlayer(citr->guid);
        if (member && m_maxEnchantingLevel < member->GetSkillValue(SKILL_ENCHANTING))
            m_maxEnchantingLevel = member->GetSkillValue(SKILL_ENCHANTING);
    }
}

void Group::SetLootMethod(LootMethod method)
{
    m_lootMethod = method;
}

void Group::SetLooterGuid(ObjectGuid guid)
{
    m_looterGuid = guid;
}

void Group::SetMasterLooterGuid(ObjectGuid guid)
{
    m_masterLooterGuid = guid;
}

void Group::SetLootThreshold(ItemQualities threshold)
{
    m_lootThreshold = threshold;
}

void Group::SetLfgRoles(ObjectGuid guid, uint8 roles)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    slot->roles = roles;
    SendUpdate();
}

bool Group::IsFull() const
{
    return isRaidGroup() ? (m_memberSlots.size() >= MAX_RAID_SIZE) : (m_memberSlots.size() >= MAX_GROUP_SIZE);
}

bool Group::isLFGGroup() const
{
    return (m_groupType & GROUPTYPE_LFG) != 0;
}

bool Group::isRaidGroup() const
{
    return (m_groupType & GROUPTYPE_RAID) != 0;
}

bool Group::isBGGroup() const
{
    return m_bgGroup != nullptr;
}

bool Group::isBFGroup() const
{
    return m_bfGroup != nullptr;
}

bool Group::IsCreated() const
{
    return GetMembersCount() > 0;
}

ObjectGuid Group::GetLeaderGUID() const
{
    return m_leaderGuid;
}

ObjectGuid Group::GetGUID() const
{
    return m_guid;
}

ObjectGuid::LowType Group::GetLowGUID() const
{
    return m_guid.GetCounter();
}

char const* Group::GetLeaderName() const
{
    return m_leaderName.c_str();
}

LootMethod Group::GetLootMethod() const
{
    return m_lootMethod;
}

ObjectGuid Group::GetLooterGuid() const
{
    if (GetLootMethod() == FREE_FOR_ALL)
        return ObjectGuid::Empty;
    return m_looterGuid;
}

ObjectGuid Group::GetMasterLooterGuid() const
{
    return m_masterLooterGuid;
}

ItemQualities Group::GetLootThreshold() const
{
    return m_lootThreshold;
}

bool Group::IsMember(ObjectGuid guid) const
{
    return _getMemberCSlot(guid) != m_memberSlots.end();
}

bool Group::IsLeader(ObjectGuid guid) const
{
    return (GetLeaderGUID() == guid);
}

ObjectGuid Group::GetMemberGUID(const std::string& name)
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->name == name)
            return itr->guid;
    return ObjectGuid::Empty;
}

uint8 Group::GetMemberFlags(ObjectGuid guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
        return 0u;
    return mslot->flags;
}

bool Group::SameSubGroup(ObjectGuid guid1, ObjectGuid guid2) const
{
    member_citerator mslot2 = _getMemberCSlot(guid2);
    if (mslot2 == m_memberSlots.end())
       return false;
    return SameSubGroup(guid1, &*mslot2);
}

bool Group::SameSubGroup(ObjectGuid guid1, MemberSlot const* slot2) const
{
    member_citerator mslot1 = _getMemberCSlot(guid1);
    if (mslot1 == m_memberSlots.end() || !slot2)
        return false;
    return (mslot1->group == slot2->group);
}

bool Group::HasFreeSlotSubGroup(uint8 subgroup) const
{
    return (m_subGroupsCounts && m_subGroupsCounts[subgroup] < MAX_GROUP_SIZE);
}

uint8 Group::GetMemberGroup(ObjectGuid guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
       return (MAX_RAID_SUBGROUPS+1);
    return mslot->group;
}

void Group::SetBattlegroundGroup(Battleground* bg)
{
    m_bgGroup = bg;
}

void Group::SetBattlefieldGroup(Battlefield *bg)
{
    m_bfGroup = bg;
}

void Group::SetGroupMemberFlag(ObjectGuid guid, bool apply, GroupMemberFlags flag)
{
    // Assistants, main assistants and main tanks are only available in raid groups
    if (!isRaidGroup())
       return;

    // Check if player is really in the raid
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    // Do flag specific actions, e.g ensure uniqueness
    switch (flag)
    {
        case MEMBER_FLAG_MAINASSIST:
            RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINASSIST);         // Remove main assist flag from current if any.
            break;
        case MEMBER_FLAG_MAINTANK:
            RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINTANK);           // Remove main tank flag from current if any.
            break;
        case MEMBER_FLAG_ASSISTANT:
            break;
        default:
            return;                                                      // This should never happen
    }

    // Switch the actual flag
    ToggleGroupMemberFlag(slot, flag, apply);

    // Preserve the new setting in the db
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_MEMBER_FLAG);

    stmt->setUInt8(0, slot->flags);
    stmt->setUInt32(1, guid.GetCounter());

    CharacterDatabase.Execute(stmt);

    // Broadcast the changes to the group
    SendUpdate();
}

Difficulty Group::GetDifficulty(bool isRaid) const
{
    return isRaid ? m_raidDifficulty : m_dungeonDifficulty;
}

Difficulty Group::GetDungeonDifficulty() const
{
    return m_dungeonDifficulty;
}

Difficulty Group::GetRaidDifficulty() const
{
    return m_raidDifficulty;
}

bool Group::isRollLootActive() const
{
    return !RollId.empty();
}

Group::Rolls::iterator Group::GetRoll(ObjectGuid Guid)
{
    Rolls::iterator iter;
    for (iter = RollId.begin(); iter != RollId.end(); ++iter)
        if ((*iter)->itemGUID == Guid && (*iter)->isValid())
            return iter;
    return RollId.end();
}

void Group::LinkMember(GroupReference* pRef)
{
    m_memberMgr.insertFirst(pRef);
}

void Group::DelinkMember(ObjectGuid guid)
{
    GroupReference* ref = m_memberMgr.getFirst();
    while (ref)
    {
        GroupReference* nextRef = ref->next();
        if (ref->GetSource()->GetGUID() == guid)
        {
            ref->unlink();
            break;
        }
        ref = nextRef;
    }
}

Group::BoundInstancesMap& Group::GetBoundInstances(Difficulty difficulty)
{
    return m_boundInstances[difficulty];
}

void Group::_initRaidSubGroupsCounter()
{
    // Sub group counters initialization
    if (!m_subGroupsCounts)
        m_subGroupsCounts = new uint8[MAX_RAID_SUBGROUPS];

    memset((void*)m_subGroupsCounts, 0, (MAX_RAID_SUBGROUPS)*sizeof(uint8));

    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        ++m_subGroupsCounts[itr->group];
}

Group::member_citerator Group::_getMemberCSlot(ObjectGuid Guid) const
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

Group::member_witerator Group::_getMemberWSlot(ObjectGuid Guid)
{
    for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

void Group::SubGroupCounterIncrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        ++m_subGroupsCounts[subgroup];
}

void Group::SubGroupCounterDecrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        --m_subGroupsCounts[subgroup];
}

void Group::RemoveUniqueGroupMemberFlag(GroupMemberFlags flag)
{
    for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->flags & flag)
            itr->flags &= ~flag;
}

void Group::ToggleGroupMemberFlag(member_witerator slot, uint8 flag, bool apply)
{
    if (apply)
        slot->flags |= flag;
    else
        slot->flags &= ~flag;
}

void Group::StartLeaderOfflineTimer()
{
    m_isLeaderOffline = true;
    m_leaderOfflineTimer.Reset(2 * MINUTE * IN_MILLISECONDS);
}

void Group::StopLeaderOfflineTimer()
{
    m_isLeaderOffline = false;
}
