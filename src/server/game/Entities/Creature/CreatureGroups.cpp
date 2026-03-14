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
 * @file CreatureGroups.cpp
 * @brief 生物编队系统实现文件
 *
 * 本文件实现了 TrinityCore 的生物编队（Formation）系统，用于管理一组生物之间的
 * 协同行为，包括：
 * - 编队跟随移动：成员按照预设的距离和角度跟随领队移动
 * - 协助战斗：当领队或成员进入战斗时，其他成员自动协助
 * - 编队数据管理：从数据库加载和维护编队配置信息
 *
 * 核心类：
 * - FormationMgr：单例管理器，负责编队数据的全局管理和数据库加载
 * - CreatureGroup：具体编队实例，管理编队内成员的协同行为
 *
 * 编队行为通过 GroupAIFlags 标志控制，可配置：
 * - 成员协助领队（FLAG_MEMBERS_ASSIST_LEADER）
 * - 领队协助成员（FLAG_LEADER_ASSISTS_MEMBER）
 * - 空闲时保持编队（FLAG_IDLE_IN_FORMATION）
 *
 * @see CreatureGroups.h
 */

#include "CreatureGroups.h"
#include "Containers.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementGenerator.h"
#include "ObjectMgr.h"

/**
 * @brief 编队成员最大允许的同步距离偏差（单位：码）
 *
 * 当成员与领队的距离偏差超过此值时，系统可能触发重新同步逻辑。
 * 用于防止编队成员过度分散。
 */
#define MAX_DESYNC 5.0f

/**
 * @brief FormationMgr 构造函数
 *
 * 初始化编队管理器单例实例。
 * 该类使用单例模式，全局只有一个实例负责管理所有编队数据。
 */
FormationMgr::FormationMgr()
{
}

/**
 * @brief FormationMgr 析构函数
 *
 * 清理编队管理器资源，释放所有编队数据映射表。
 */
FormationMgr::~FormationMgr()
{
}

/**
 * @brief 获取 FormationMgr 单例实例
 *
 * 使用静态局部变量实现线程安全的单例模式（C++11 magic statics）。
 * 服务器运行期间只会创建一个 FormationMgr 实例。
 *
 * @return FormationMgr* 指向全局单例实例的指针
 *
 * @note 线程安全：C++11 保证静态局部变量初始化的线程安全性
 */
FormationMgr* FormationMgr::instance()
{
    static FormationMgr instance;
    return &instance;
}

/**
 * @brief 将生物添加到编队中
 *
 * 根据领队的 spawnId 查找或创建编队，并将指定生物添加为成员。
 * 该函数处理两种情况：
 * 1. 编队已存在：将生物添加到现有编队
 * 2. 编队不存在：创建新编队并添加生物
 *
 * 对于动态生成的生物，会先检查并移除该 spawnId 的旧实例，
 * 避免编队中存在无效的生物指针。
 *
 * @param leaderSpawnId 领队的 spawnId（数据库中的 guid）
 * @param creature 要添加到编队的生物实例
 *
 * @note 此函数在生物生成时调用，确保编队关系正确建立
 * @note 动态生成的生物可能多次重生，需要清理旧实例
 *
 * @see RemoveCreatureFromGroup
 */
void FormationMgr::AddCreatureToGroup(ObjectGuid::LowType leaderSpawnId, Creature* creature)
{
    Map* map = creature->GetMap();

    auto itr = map->CreatureGroupHolder.find(leaderSpawnId);
    if (itr != map->CreatureGroupHolder.end())
    {
        // 将成员添加到已存在的编队
        TC_LOG_DEBUG("entities.unit", "Group found: {}, inserting creature {}, Group InstanceID {}", leaderSpawnId, creature->GetGUID().ToString(), creature->GetInstanceId());

        // 对于动态生成的生物，该生物可能刚刚重生
        // 需要查找该生物之前的实例并从编队中删除，因为旧实例已失效
        auto bounds = Trinity::Containers::MapEqualRange(map->GetCreatureBySpawnIdStore(), creature->GetSpawnId());
        for (auto const& pair : bounds)
        {
            Creature* other = pair.second;
            if (other == creature)
                continue;

            if (itr->second->HasMember(other))
                itr->second->RemoveMember(other);
        }
    }
    else
    {
        // 创建新编队
        TC_LOG_DEBUG("entities.unit", "Group not found: {}. Creating new group.", leaderSpawnId);
        CreatureGroup* group = new CreatureGroup(leaderSpawnId);
        std::tie(itr, std::ignore) = map->CreatureGroupHolder.emplace(leaderSpawnId, group);
    }

    itr->second->AddMember(creature);
}

/**
 * @brief 从编队中移除生物
 *
 * 将指定生物从编队中移除。如果移除后编队变为空（没有成员），
 * 则自动销毁编队实例并从地图的编队持有者列表中删除。
 *
 * @param group 目标编队实例
 * @param member 要移除的生物成员
 *
 * @note 当编队为空时会自动销毁编队对象，释放内存
 * @note 此函数在生物被删除或移出地图时调用
 *
 * @see AddCreatureToGroup
 */
void FormationMgr::RemoveCreatureFromGroup(CreatureGroup* group, Creature* member)
{
    TC_LOG_DEBUG("entities.unit", "Deleting member pointer to GUID: {} from group {}", group->GetLeaderSpawnId(), member->GetSpawnId());
    group->RemoveMember(member);

    // 如果编队已空，销毁编队实例
    if (group->IsEmpty())
    {
        Map* map = member->GetMap();

        TC_LOG_DEBUG("entities.unit", "Deleting group with InstanceID {}", member->GetInstanceId());
        auto itr = map->CreatureGroupHolder.find(group->GetLeaderSpawnId());
        ASSERT(itr != map->CreatureGroupHolder.end(), "Not registered group %u in map %u", group->GetLeaderSpawnId(), map->GetId());
        map->CreatureGroupHolder.erase(itr);
        delete group;
    }
}

/**
 * @brief 从数据库加载所有编队配置
 *
 * 从 `creature_formations` 数据库表加载编队配置数据，包括：
 * - 领队和成员的 spawnId
 * - 跟随距离和角度
 * - 编队 AI 行为标志
 * - 领队的路径点 ID
 *
 * 加载过程会进行数据验证：
 * 1. 检查领队和成员的 spawnId 是否有效（对应生物是否存在）
 * 2. 检查领队是否在自身的编队中（领队必须是其所属编队的成员）
 *
 * 无效的编队数据会被跳过或清理，确保数据一致性。
 *
 * @note 此函数在服务器启动时调用，仅加载一次
 * @note 角度值从数据库的角度制转换为弧度制
 *
 * @see FormationInfo
 */
void FormationMgr::LoadCreatureFormations()
{
    uint32 oldMSTime = getMSTime();

    // 查询编队数据
    QueryResult result = WorldDatabase.Query("SELECT leaderGUID, memberGUID, dist, angle, groupAI, point_1, point_2 FROM creature_formations ORDER BY leaderGUID");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">>  Loaded 0 creatures in formations. DB table `creature_formations` is empty!");
        return;
    }

    uint32 count = 0;
    std::unordered_set<ObjectGuid::LowType> leaderSpawnIds;
    do
    {
        Field* fields = result->Fetch();

        // 加载编队成员数据
        FormationInfo member;
        member.LeaderSpawnId              = fields[0].GetUInt32();
        ObjectGuid::LowType memberSpawnId = fields[1].GetUInt32();
        member.FollowDist                 = 0.f;
        member.FollowAngle                = 0.f;

        // 如果是领队本身，跳过距离和角度的加载（领队不需要跟随参数）
        if (member.LeaderSpawnId != memberSpawnId)
        {
            member.FollowDist             = fields[2].GetFloat();
            member.FollowAngle            = fields[3].GetFloat() * float(M_PI) / 180.0f; // 角度转弧度
        }

        member.GroupAI                    = fields[4].GetUInt32();
        for (uint8 i = 0; i < 2; ++i)
            member.LeaderWaypointIDs[i]   = fields[5 + i].GetUInt16();

        // 数据正确性检查
        {
            if (!sObjectMgr->GetCreatureData(member.LeaderSpawnId))
            {
                TC_LOG_ERROR("sql.sql", "creature_formations table leader guid {} incorrect (not exist)", member.LeaderSpawnId);
                continue;
            }

            if (!sObjectMgr->GetCreatureData(memberSpawnId))
            {
                TC_LOG_ERROR("sql.sql", "creature_formations table member guid {} incorrect (not exist)", memberSpawnId);
                continue;
            }

            leaderSpawnIds.insert(member.LeaderSpawnId);
        }

        _creatureGroupMap.emplace(memberSpawnId, std::move(member));
        ++count;
    } while (result->NextRow());

    // 检查领队是否在自身的编队中
    // 如果领队不在编队中，则该编队无效，需要清理所有相关成员
    for (ObjectGuid::LowType leaderSpawnId : leaderSpawnIds)
    {
        if (!_creatureGroupMap.count(leaderSpawnId))
        {
            TC_LOG_ERROR("sql.sql", "creature_formation contains leader spawn {} which is not included on its formation, removing", leaderSpawnId);
            for (auto itr = _creatureGroupMap.begin(); itr != _creatureGroupMap.end();)
            {
                if (itr->second.LeaderSpawnId == leaderSpawnId)
                {
                    itr = _creatureGroupMap.erase(itr);
                    continue;
                }

                ++itr;
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} creatures in formations in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取指定生物的编队配置信息
 *
 * 根据生物的 spawnId 查询其所属编队的配置信息，
 * 包括领队 ID、跟随距离、跟随角度、AI 标志和路径点 ID。
 *
 * @param spawnId 生物的 spawnId（数据库中的 guid）
 * @return FormationInfo* 编队配置信息指针，如果生物不属于任何编队则返回 nullptr
 *
 * @note 返回的指针由 FormationMgr 管理，调用者不应删除
 * @note 此函数用于运行时查询生物的编队属性
 */
FormationInfo* FormationMgr::GetFormationInfo(ObjectGuid::LowType spawnId)
{
    return Trinity::Containers::MapGetValuePtr(_creatureGroupMap, spawnId);
}

/**
 * @brief 动态添加编队成员
 *
 * 在运行时动态创建编队成员关系，用于脚本或特殊场景中创建临时编队。
 * 新添加的成员将使用指定的跟随参数和 AI 行为标志。
 *
 * @param spawnId 成员的 spawnId
 * @param followAng 跟随角度（弧度制）
 * @param followDist 跟随距离
 * @param leaderSpawnId 领队的 spawnId
 * @param groupAI 编队 AI 行为标志（GroupAIFlags 枚举值）
 *
 * @note 此函数不会将生物实例添加到编队中，仅添加配置数据
 * @note 路径点 ID 默认初始化为 0
 *
 * @see AddCreatureToGroup
 */
void FormationMgr::AddFormationMember(ObjectGuid::LowType spawnId, float followAng, float followDist, ObjectGuid::LowType leaderSpawnId, uint32 groupAI)
{
    FormationInfo member;
    member.LeaderSpawnId = leaderSpawnId;
    member.FollowDist    = followDist;
    member.FollowAngle   = followAng;
    member.GroupAI       = groupAI;
    for (uint8 i = 0; i < 2; ++i)
        member.LeaderWaypointIDs[i] = 0;

    _creatureGroupMap.emplace(spawnId, std::move(member));
}

/**
 * @brief CreatureGroup 构造函数
 *
 * 创建一个新的编队实例。编队必须指定领队的 spawnId。
 * 编队初始状态：无领队指针、未形成编队、未在战斗协助中。
 *
 * @param leaderSpawnId 领队的 spawnId（数据库中的 guid）
 *
 * @note 编队不能为空创建，必须指定领队 ID
 * @note 领队指针 _leader 在 AddMember 时设置
 */
CreatureGroup::CreatureGroup(ObjectGuid::LowType leaderSpawnId) : _leader(nullptr), _members(), _leaderSpawnId(leaderSpawnId), _formed(false), _engaging(false)
{
}

/**
 * @brief CreatureGroup 析构函数
 *
 * 清理编队资源。注意成员的 FormationInfo 指针由 FormationMgr 管理，
 * 此处不需要释放。
 */
CreatureGroup::~CreatureGroup()
{
}

/**
 * @brief 添加成员到编队
 *
 * 将生物添加到编队中。如果生物的 spawnId 与领队 ID 匹配，
 * 则将其设置为编队的领队。
 *
 * 添加过程：
 * 1. 检查生物是否为领队，如果是则设置 _leader 指针
 * 2. 从 FormationMgr 获取该生物的编队配置信息
 * 3. 将生物和配置信息加入成员映射表
 * 4. 设置生物的编队引用
 *
 * @param member 要添加的生物实例
 *
 * @pre 生物必须在 FormationMgr 中有对应的 FormationInfo 配置
 * @note 每个编队有且仅有一个领队
 * @note 领队也是编队的成员之一
 *
 * @see RemoveMember
 */
void CreatureGroup::AddMember(Creature* member)
{
    TC_LOG_DEBUG("entities.unit", "CreatureGroup::AddMember: Adding unit {}.", member->GetGUID().ToString());

    // 检查是否为领队
    if (member->GetSpawnId() == _leaderSpawnId)
    {
        TC_LOG_DEBUG("entities.unit", "Unit {} is formation leader. Adding group.", member->GetGUID().ToString());
        _leader = member;
    }

    // 此时编队配置必须已注册
    FormationInfo* formationInfo = ASSERT_NOTNULL(sFormationMgr->GetFormationInfo(member->GetSpawnId()));
    _members.emplace(member, formationInfo);
    member->SetFormation(this);
}

/**
 * @brief 从编队中移除成员
 *
 * 将生物从编队中移除。如果移除的是领队，则清空领队指针。
 * 同时清除生物的编队引用。
 *
 * @param member 要移除的生物实例
 *
 * @note 移除领队不会销毁编队，领队指针置空等待重新设置
 * @note 移除操作不会销毁编队，即使编队为空也由 FormationMgr 负责
 *
 * @see AddMember
 */
void CreatureGroup::RemoveMember(Creature* member)
{
    if (_leader == member)
        _leader = nullptr;

    _members.erase(member);
    member->SetFormation(nullptr);
}

/**
 * @brief 处理成员进入战斗时的协助行为
 *
 * 当编队中的某个成员进入战斗时，根据编队的 AI 标志触发其他成员的协助行为。
 * 支持两种协助模式：
 * 1. 成员协助领队：当领队进入战斗时，其他成员协助攻击
 * 2. 领队协助成员：当成员进入战斗时，领队协助攻击
 *
 * 该函数使用递归保护标志 _engaging 防止无限递归调用。
 *
 * @param member 进入战斗的成员
 * @param target 战斗目标
 *
 * @note 仅在 GroupAI 标志不为 0 时生效
 * @note 使用 _engaging 标志防止递归调用导致的栈溢出
 * @note 只有存活的成员才会参与协助
 * @note 成员必须能够合法攻击目标才会加入战斗
 *
 * @see GroupAIFlags
 */
void CreatureGroup::MemberEngagingTarget(Creature* member, Unit* target)
{
    // 防止递归调用
    if (_engaging)
        return;

    uint8 groupAI = ASSERT_NOTNULL(sFormationMgr->GetFormationInfo(member->GetSpawnId()))->GroupAI;
    if (!groupAI)
        return;

    // 根据触发者身份和 AI 标志判断是否触发协助
    if (member == _leader)
    {
        // 领队进入战斗，检查成员是否协助领队
        if (!(groupAI & FLAG_MEMBERS_ASSIST_LEADER))
            return;
    }
    else if (!(groupAI & FLAG_LEADER_ASSISTS_MEMBER))
        return; // 成员进入战斗，检查领队是否协助成员

    _engaging = true;

    // 遍历所有成员，触发协助行为
    for (auto const& pair : _members)
    {
        Creature* other = pair.first;
        if (other == member)
            continue;

        if (!other->IsAlive())
            continue;

        // 根据标志和角色决定是否协助
        if (((other != _leader && (groupAI & FLAG_MEMBERS_ASSIST_LEADER)) || (other == _leader && (groupAI & FLAG_LEADER_ASSISTS_MEMBER))) && other->IsValidAttackTarget(target))
            other->EngageWithTarget(target);
    }

    _engaging = false;
}

/**
 * @brief 重置编队状态
 *
 * 重置编队中所有非领队成员的移动状态，使其进入空闲状态。
 * 用于编队解散或重置时清理成员的移动行为。
 *
 * @param dismiss 是否解散编队（当前未使用，保留参数）
 *
 * @note 当前实现将所有成员设置为空闲状态（MoveIdle）
 * @note 领队不受影响，仅重置成员状态
 * @note 该函数可能在编队解散、重置或特定事件触发时调用
 *
 * @todo 参数 dismiss 当前未使用，可能用于控制是否完全解散编队
 */
void CreatureGroup::FormationReset(bool /*dismiss*/)
{
    for (auto const& pair : _members)
    {
        if (pair.first != _leader && pair.first->IsAlive())
        {
            pair.first->GetMotionMaster()->MoveIdle();
            // TC_LOG_DEBUG("entities.unit", "CreatureGroup::FormationReset: Set {} movement for member {}", dismiss ? "default" : "idle", pair.first->GetGUID().ToString());
        }
    }

    // _formed = !dismiss;
}

/**
 * @brief 处理领队开始移动事件
 *
 * 当领队开始移动时，通知所有符合条件的成员开始跟随领队移动。
 * 成员将按照预设的距离和角度跟随领队，保持编队阵型。
 *
 * 成员跟随的条件：
 * 1. 不是领队本身
 * 2. 成员存活
 * 3. 成员未处于战斗状态
 * 4. 成员启用了空闲编队标志（FLAG_IDLE_IN_FORMATION）
 * 5. 成员未处于跟随编队状态（避免重复启动）
 *
 * @note 角度值需要加上 π 进行反转（历史遗留设计）
 * @note 成员通过 MoveFormation 生成器实现跟随行为
 * @note 该函数在领队开始巡逻或移动时调用
 *
 * @see MoveFormation
 * @see FLAG_IDLE_IN_FORMATION
 */
void CreatureGroup::LeaderStartedMoving()
{
    if (!_leader)
        return;

    for (auto const& pair : _members)
    {
        Creature* member = pair.first;
        if (member == _leader || !member->IsAlive() || member->IsEngaged() || !(pair.second->GroupAI & FLAG_IDLE_IN_FORMATION))
            continue;

        float angle = pair.second->FollowAngle + float(M_PI); // 角度反转（历史原因）
        float dist = pair.second->FollowDist;

        if (!member->HasUnitState(UNIT_STATE_FOLLOW_FORMATION))
            member->GetMotionMaster()->MoveFormation(_leader, dist, angle, pair.second->LeaderWaypointIDs[0], pair.second->LeaderWaypointIDs[1]);
    }
}

/**
 * @brief 检查领队是否可以开始移动
 *
 * 检查编队中所有非领队成员的状态，判断领队是否可以安全开始移动。
 * 只有当所有成员都处于可跟随状态时，领队才能开始移动。
 *
 * 成员不可跟随的条件：
 * 1. 成员正在战斗中（IsEngaged）
 * 2. 成员正在返回出生点（IsReturningHome）
 *
 * @return true 领队可以开始移动，所有成员都可跟随
 * @return false 领队不能移动，存在成员处于不可跟随状态
 *
 * @note 此函数用于编队协同移动的决策，确保编队完整性
 * @note 领队自身的状态不影响返回值，仅检查成员状态
 * @note 只有存活的成员会被检查
 *
 * @see LeaderStartedMoving
 */
bool CreatureGroup::CanLeaderStartMoving() const
{
    for (std::unordered_map<Creature*, FormationInfo*>::value_type const& pair : _members)
    {
        if (pair.first != _leader && pair.first->IsAlive())
        {
            if (pair.first->IsEngaged() || pair.first->IsReturningHome())
                return false;
        }
    }

    return true;
}
