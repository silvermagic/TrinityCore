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
 * @file CreatureGroups.h
 * @brief 生物编队系统头文件
 *
 * 本文件定义了生物编队（Formation）系统的核心组件，用于管理生物之间的编队行为，
 * 包括编队成员的跟随、仇恨联动、阵型保持等功能。该系统允许生物以固定的相对位置
 * 跟随领导者移动，并在战斗中实现仇恨共享或互助行为。
 *
 * 主要组件：
 * - FormationMgr：全局编队管理器，负责加载编队数据和创建/销毁编队实例
 * - CreatureGroup：生物编队实例，管理编队成员和领导者关系
 * - FormationInfo：编队成员配置信息，包含跟随距离、角度和AI标志
 *
 * 编队行为由 GroupAIFlags 控制，可实现以下功能：
 * - 成员协助领导者（FLAG_MEMBERS_ASSIST_LEADER）
 * - 领导者协助成员（FLAG_LEADER_ASSISTS_MEMBER）
 * - 闲置时保持阵型（FLAG_IDLE_IN_FORMATION）
 */

#ifndef _FORMATIONS_H
#define _FORMATIONS_H

#include "Define.h"
#include "ObjectGuid.h"
#include <unordered_map>
#include <map>

/**
 * @brief 生物编队AI行为标志枚举
 *
 * 定义生物编队中成员与领导者之间的仇恨联动和移动行为。
 * 这些标志通过数据库表 `creature_formations` 的 `groupAI` 字段配置。
 */
enum GroupAIFlags
{
    FLAG_AGGRO_NONE            = 0,                                                         ///< 无编队行为，生物独立行动
    FLAG_MEMBERS_ASSIST_LEADER = 0x00000001,                                                ///< 成员协助领导者：领导者进入战斗时，所有成员加入战斗
    FLAG_LEADER_ASSISTS_MEMBER = 0x00000002,                                                ///< 领导者协助成员：成员进入战斗时，领导者加入战斗
    FLAG_MEMBERS_ASSIST_MEMBER = (FLAG_MEMBERS_ASSIST_LEADER | FLAG_LEADER_ASSISTS_MEMBER), ///< 全员互助：任何成员受攻击时，所有其他成员（包括领导者）都会协助
    FLAG_IDLE_IN_FORMATION     = 0x00000200,                                                ///< 闲置时保持阵型：成员在非战斗状态下会跟随领导者移动
};

class Creature;
class CreatureGroup;
class Unit;
struct Position;

/**
 * @brief 编队成员配置信息结构体
 *
 * 存储单个生物在编队中的配置数据，包括相对于领导者的位置、
 * 编队AI行为标志以及领导者的路径点信息。
 * 该结构体的数据从数据库表 `creature_formations` 加载。
 */
struct FormationInfo
{
    ObjectGuid::LowType LeaderSpawnId;   ///< 领导者的出生ID（SpawnID）
    float FollowDist;                    ///< 跟随距离：成员与领导者之间的距离
    float FollowAngle;                   ///< 跟随角度：成员相对于领导者的角度（弧度）
    uint32 GroupAI;                      ///< 编队AI标志：控制仇恨联动和移动行为的位掩码（GroupAIFlags）
    uint32 LeaderWaypointIDs[2];         ///< 领导者的路径点ID范围：[起始ID, 结束ID]，用于路径跟随
};

/**
 * @brief 编队管理器（单例模式）
 *
 * 负责全局编队系统的管理，包括：
 * - 从数据库加载编队配置信息
 * - 创建和管理编队实例（CreatureGroup）
 * - 处理生物加入/离开编队的逻辑
 *
 * 该类采用单例模式，通过 sFormationMgr 宏访问全局实例。
 * 编队数据在服务器启动时从 `creature_formations` 数据库表加载。
 */
class TC_GAME_API FormationMgr
{
    private:
        FormationMgr();  ///< 私有构造函数（单例模式）
        ~FormationMgr(); ///< 私有析构函数（单例模式）

        std::unordered_map<uint32 /*spawnID*/, FormationInfo> _creatureGroupMap; ///< 编队成员配置映射：spawnID -> FormationInfo

    public:
        /**
         * @brief 获取编队管理器单例实例
         * @return 编队管理器的全局唯一实例指针
         * @note 线程安全的单例访问方式，在首次调用时创建实例
         */
        static FormationMgr* instance();

        /**
         * @brief 将生物添加到编队
         * @param leaderSpawnId 领导者的出生ID
         * @param creature 要添加的生物指针
         *
         * 该函数会根据领导者的出生ID查找或创建编队实例，
         * 然后将生物添加为编队成员。如果编队不存在，会自动创建。
         *
         * @note 此函数在生物初始化时调用，性能敏感
         * @see Creature::LoadCreatureFormations()
         */
        void AddCreatureToGroup(ObjectGuid::LowType leaderSpawnId, Creature* creature);

        /**
         * @brief 从编队中移除生物
         * @param group 编队实例指针
         * @param creature 要移除的生物指针
         *
         * 从指定编队中移除生物成员。如果移除后编队为空，
         * 编队实例将被自动销毁。
         *
         * @note 此函数在生物从世界中移除时调用
         * @see Creature::CleanupsBeforeDelete()
         */
        void RemoveCreatureFromGroup(CreatureGroup* group, Creature* creature);

        /**
         * @brief 从数据库加载所有编队配置
         *
         * 在服务器启动时调用，从数据库表 `creature_formations` 加载
         * 所有编队配置信息到内存映射表中。
         *
         * @note 仅在服务器初始化时调用一次
         * @see World::SetInitialWorldSettings()
         */
        void LoadCreatureFormations();

        /**
         * @brief 获取指定生物的编队配置信息
         * @param spawnId 生物的出生ID
         * @return 编队配置信息指针，如果生物不在编队中则返回 nullptr
         *
         * 该函数用于查询生物是否属于某个编队，并获取其在编队中的配置。
         *
         * @note 时间复杂度 O(1)，使用哈希表查找
         */
        FormationInfo* GetFormationInfo(ObjectGuid::LowType spawnId);

        /**
         * @brief 添加编队成员配置到映射表
         * @param spawnId 成员的出生ID
         * @param followAng 跟随角度（弧度）
         * @param followDist 跟随距离
         * @param leaderSpawnId 领导者的出生ID
         * @param groupAI 编队AI标志
         *
         * 该函数用于动态添加编队配置信息，主要用于数据库加载过程。
         *
         * @note 仅在 LoadCreatureFormations() 中调用
         */
        void AddFormationMember(ObjectGuid::LowType spawnId, float followAng, float followDist, ObjectGuid::LowType leaderSpawnId, uint32 groupAI);
};

/**
 * @brief 生物编队实例类
 *
 * 表示一个具体的生物编队，管理编队中的领导者和成员关系。
 * 每个编队有一个领导者（Leader）和多个成员（Member），
 * 成员按照 FormationInfo 中定义的相对位置跟随领导者。
 *
 * 编队的生命周期：
 * 1. 第一个成员加入时，FormationMgr 创建 CreatureGroup 实例
 * 2. 其他成员陆续加入，形成完整编队
 * 3. 成员死亡或从世界移除时，从编队中移除
 * 4. 最后一个成员移除后，编队实例被销毁
 *
 * 编队行为：
 * - 跟随移动：成员根据 FollowDist 和 FollowAngle 保持阵型
 * - 仇恨联动：根据 GroupAIFlags 配置实现成员间仇恨共享
 */
class TC_GAME_API CreatureGroup
{
    private:
        Creature* _leader; ///< 编队领导者指针（必须使用指针而非引用，因为领导者可能为空）
        std::unordered_map<Creature*, FormationInfo*> _members; ///< 编队成员映射：成员指针 -> 编队配置

        ObjectGuid::LowType _leaderSpawnId; ///< 领导者的出生ID，用于查找领导者实体
        bool _formed;   ///< 编队是否已形成：所有成员都已加入且领导者已确定
        bool _engaging; ///< 编队是否正在进入战斗：用于防止递归调用仇恨联动

    public:
        /**
         * @brief 构造函数
         * @param leaderSpawnId 领导者的出生ID
         *
         * 编队必须以领导者的出生ID创建，领导者实体可能在后续才加载。
         * 编队初始化时处于未形成状态（_formed = false）。
         *
         * @note 编队不能为空创建，必须有至少一个成员
         */
        explicit CreatureGroup(ObjectGuid::LowType leaderSpawnId);

        /**
         * @brief 析构函数
         *
         * 清理编队资源，不会主动移除或销毁成员生物。
         */
        ~CreatureGroup();

        /**
         * @brief 获取编队领导者
         * @return 领导者生物指针，如果领导者未加载则返回 nullptr
         */
        Creature* GetLeader() const { return _leader; }

        /**
         * @brief 获取领导者的出生ID
         * @return 领导者的出生ID
         */
        ObjectGuid::LowType GetLeaderSpawnId() const { return _leaderSpawnId; }

        /**
         * @brief 检查编队是否为空
         * @return 如果编队没有任何成员则返回 true
         */
        bool IsEmpty() const { return _members.empty(); }

        /**
         * @brief 检查编队是否已形成
         * @return 如果编队已完全形成（所有成员已加入）则返回 true
         */
        bool IsFormed() const { return _formed; }

        /**
         * @brief 检查指定生物是否为领导者
         * @param creature 要检查的生物
         * @return 如果是领导者则返回 true
         */
        bool IsLeader(Creature const* creature) const { return _leader == creature; }

        /**
         * @brief 检查生物是否为编队成员
         * @param member 要检查的生物指针
         * @return 如果是编队成员则返回 true
         *
         * @note 时间复杂度 O(1)
         */
        bool HasMember(Creature* member) const { return _members.count(member) > 0; }

        /**
         * @brief 添加成员到编队
         * @param member 要添加的生物成员
         *
         * 将生物添加到编队成员列表，并设置其跟随位置。
         * 如果添加的是领导者，会设置 _leader 指针。
         * 当领导者和所有成员都加入后，编队进入已形成状态。
         *
         * @note 此函数由 FormationMgr::AddCreatureToGroup() 调用
         */
        void AddMember(Creature* member);

        /**
         * @brief 从编队中移除成员
         * @param member 要移除的生物成员
         *
         * 从编队成员列表中移除生物。如果移除的是领导者，
         * _leader 指针会被清空。移除后编队可能不再处于已形成状态。
         *
         * @note 此函数由 FormationMgr::RemoveCreatureFromGroup() 调用
         */
        void RemoveMember(Creature* member);

        /**
         * @brief 重置编队状态
         * @param dismiss 是否解散编队（true：解散，false：仅重置状态）
         *
         * 重置编队的战斗和移动状态。如果 dismiss 为 true，
         * 成员会停止跟随领导者并返回初始位置。
         *
         * @note 通常在战斗结束或生物死亡时调用
         */
        void FormationReset(bool dismiss);

        /**
         * @brief 通知编队领导者开始移动
         *
         * 当领导者开始移动时调用，触发所有成员根据
         * FLAG_IDLE_IN_FORMATION 标志开始跟随移动。
         *
         * @note 仅在领导者处于闲置状态且启用了阵型跟随标志时有效
         */
        void LeaderStartedMoving();

        /**
         * @brief 处理成员进入战斗事件
         * @param member 进入战斗的成员
         * @param target 攻击目标
         *
         * 根据编队的 GroupAIFlags 配置处理仇恨联动：
         * - FLAG_MEMBERS_ASSIST_LEADER：领导者进入战斗时，成员加入战斗
         * - FLAG_LEADER_ASSISTS_MEMBER：成员进入战斗时，领导者加入战斗
         * - FLAG_MEMBERS_ASSIST_MEMBER：任何成员受攻击时，所有成员加入战斗
         *
         * @note 使用 _engaging 标志防止递归调用导致死循环
         */
        void MemberEngagingTarget(Creature* member, Unit* target);

        /**
         * @brief 检查领导者是否可以开始移动
         * @return 如果领导者可以移动则返回 true
         *
         * 检查编队状态，确定领导者是否可以自由移动。
         * 通常在领导者尝试移动前调用，以避免打断战斗中的仇恨联动。
         */
        bool CanLeaderStartMoving() const;
};

#define sFormationMgr FormationMgr::instance()

#endif
