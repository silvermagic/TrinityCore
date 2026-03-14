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
 * @file ScriptedFollowerAI.cpp
 * @brief 跟随AI模块实现文件
 *
 * 本文件实现了FollowerAI类,提供跟随任务的核心逻辑:
 * - 跟随玩家移动
 * - 战斗辅助与玩家受攻击响应
 * - 距离检测与任务状态监控
 * - 暂停、恢复和完成跟随功能
 */

#include "ScriptedFollowerAI.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"

/** 最大玩家距离常量,玩家超过此距离将导致跟随失败 */
float constexpr MAX_PLAYER_DISTANCE = 100.0f;

/**
 * @brief 特殊路径点ID枚举
 *
 * 定义用于内部移动逻辑的特殊路径点ID
 */
enum Points
{
    POINT_COMBAT_START = 0xFFFFFF  ///< 战斗起始点
};

/**
 * @brief FollowerAI构造函数
 * @param creature 关联的生物对象
 *
 * 初始化所有成员变量为默认值:
 * - 更新跟随计时器: 2.5秒
 * - 跟随状态: 无
 * - 关联任务: 0(无任务)
 */
FollowerAI::FollowerAI(Creature* creature) : ScriptedAI(creature), _updateFollowTimer(2500), _followState(STATE_FOLLOW_NONE), _questForFollow(0) { }

/**
 * @brief 视线范围内的单位检测
 * @param who 进入视线范围的单位
 *
 * 当单位进入视线范围时的处理逻辑:
 * 1. 如果正在跟随且不需要协助玩家战斗,则直接返回
 * 2. 否则调用基类的视线检测逻辑
 */
void FollowerAI::MoveInLineOfSight(Unit* who)
{
    // 如果正在跟随且不需要协助玩家战斗,则不处理
    if (HasFollowState(STATE_FOLLOW_INPROGRESS) && !ShouldAssistPlayerInCombatAgainst(who))
        return;

    // 调用基类的视线检测逻辑
    ScriptedAI::MoveInLineOfSight(who);
}

/**
 * @brief 生物死亡回调
 * @param killer 击杀者(未使用)
 *
 * 当跟随NPC死亡时的处理逻辑:
 * 1. 检查是否在跟随状态且有领导者和任务
 * 2. 如果玩家在队伍中,则让所有在地图上的队伍成员任务失败
 * 3. 如果玩家不在队伍中,则只让该玩家任务失败
 *
 * @todo 需要更好的检查来处理有完成时间限制的任务
 */
void FollowerAI::JustDied(Unit* /*killer*/)
{
    // 检查是否在跟随状态且有领导者和任务
    if (!HasFollowState(STATE_FOLLOW_INPROGRESS) || !_leaderGUID || !_questForFollow)
        return;

    /// @todo need a better check for quests with time limit.
    // 获取跟随的领导者
    if (Player* player = GetLeaderForFollower())
    {
        // 如果玩家在队伍中,让所有在场的队伍成员任务失败
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
                if (Player* member = groupRef->GetSource())
                    if (member->IsInMap(player))
                        member->FailQuest(_questForFollow);
        }
        else
        {
            // 单人情况下让玩家任务失败
            player->FailQuest(_questForFollow);
        }
    }
}

/**
 * @brief 返回出生点完成回调
 *
 * 当生物返回出生点后的处理逻辑:
 * 1. 如果不在跟随状态,直接返回
 * 2. 获取领导者玩家
 * 3. 如果存在领导者且未暂停,继续跟随
 * 4. 如果领导者不存在,则消失
 */
void FollowerAI::JustReachedHome()
{
    // 检查是否在跟随状态
    if (!HasFollowState(STATE_FOLLOW_INPROGRESS))
        return;

    // 获取领导者
    if (Player* player = GetLeaderForFollower())
    {
        // 如果暂停状态,不继续跟随
        if (HasFollowState(STATE_FOLLOW_PAUSED))
            return;
        // 继续跟随玩家(使用宠物跟随距离和角度)
        me->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }
    else
    {
        // 领导者不存在,消失
        me->DespawnOrUnsummon();
    }
}

/**
 * @brief 主人被攻击回调
 * @param other 攻击者
 *
 * 当跟随的玩家被攻击时的处理逻辑:
 * 1. 检查生物反应模式(非被动)
 * 2. 检查是否应该协助玩家战斗
 * 3. 如果满足条件则与攻击者交战
 */
void FollowerAI::OwnerAttackedBy(Unit* other)
{
    // 检查生物反应模式非被动且应该协助
    if (!me->HasReactState(REACT_PASSIVE) && ShouldAssistPlayerInCombatAgainst(other))
        // 与攻击者交战
        me->EngageWithTarget(other);
}

/**
 * @brief 更新AI主函数
 * @param uiDiff 距离上次更新的时间差(毫秒)
 *
 * 核心更新逻辑分为两大部分:
 * 1. 跟随状态检测:
 *    - 检查是否已完成且无后置事件,如果是则消失
 *    - 检查玩家或队伍成员是否在范围内
 *    - 检查任务是否被放弃
 *    - 如果超出距离或任务被放弃,则消失
 *
 * 2. 调用派生类的更新逻辑
 *
 * @note 每帧调用,需注意性能优化
 */
void FollowerAI::UpdateAI(uint32 uiDiff)
{
    // ==================== 跟随状态检测 ====================
    // 仅在跟随中且未战斗时检测
    if (HasFollowState(STATE_FOLLOW_INPROGRESS) && !me->IsEngaged())
    {
        // 检查更新计时器是否到期
        if (_updateFollowTimer <= uiDiff)
        {
            // 如果已完成且无后置事件,则消失
            if (HasFollowState(STATE_FOLLOW_COMPLETE) && !HasFollowState(STATE_FOLLOW_POSTEVENT))
            {
                TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::UpdateAI: is set completed, despawns. ({})", me->GetGUID().ToString());
                me->DespawnOrUnsummon();
                return;
            }

            // 初始化检测标志
            bool maxRangeExceeded = true;        // 距离超出标志
            bool questAbandoned = (_questForFollow != 0);  // 任务放弃标志

            // 获取领导者
            if (Player* player = GetLeaderForFollower())
            {
                // 如果玩家在队伍中,检查所有队伍成员
                if (Group* group = player->GetGroup())
                {
                    for (GroupReference* groupRef = group->GetFirstMember(); groupRef && (maxRangeExceeded || questAbandoned); groupRef = groupRef->next())
                    {
                        Player* member = groupRef->GetSource();
                        if (!member)
                            continue;
                        // 检查距离
                        if (maxRangeExceeded && me->IsWithinDistInMap(member, MAX_PLAYER_DISTANCE))
                            maxRangeExceeded = false;
                        // 检查任务状态
                        if (questAbandoned)
                        {
                            QuestStatus status = member->GetQuestStatus(_questForFollow);
                            if ((status == QUEST_STATUS_COMPLETE) || (status == QUEST_STATUS_INCOMPLETE))
                                questAbandoned = false;
                        }
                    }
                }
                else
                {
                    // 单人情况
                    // 检查距离
                    if (me->IsWithinDistInMap(player, MAX_PLAYER_DISTANCE))
                        maxRangeExceeded = false;
                    // 检查任务状态
                    if (questAbandoned)
                    {
                        QuestStatus status = player->GetQuestStatus(_questForFollow);
                        if ((status == QUEST_STATUS_COMPLETE) || (status == QUEST_STATUS_INCOMPLETE))
                            questAbandoned = false;
                    }
                }
            }

            // 如果超出距离或任务被放弃,则消失
            if (maxRangeExceeded || questAbandoned)
            {
                TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::UpdateAI: failed because player/group was to far away or not found ({})", me->GetGUID().ToString());
                me->DespawnOrUnsummon();
                return;
            }

            // 重置更新计时器为1秒
            _updateFollowTimer = 1000;
        }
        else
        {
            // 计时器未到期,减少剩余时间
            _updateFollowTimer -= uiDiff;
        }
    }

    // ==================== 调用派生类更新逻辑 ====================
    UpdateFollowerAI(uiDiff);
}

/**
 * @brief 更新跟随AI(派生类可重写)
 * @param uiDiff 距离上次更新的时间差(毫秒)
 *
 * 默认实现为简单的近战攻击逻辑,派生类可重写以添加自定义行为
 */
void FollowerAI::UpdateFollowerAI(uint32 /*uiDiff*/)
{
    // 如果没有有效的攻击目标,则返回
    if (!UpdateVictim())
        return;

    // 如果准备就绪则进行近战攻击
    DoMeleeAttackIfReady();
}

/**
 * @brief 开始跟随
 * @param player 要跟随的玩家
 * @param factionForFollower 跟随者的阵营ID(默认0,不改变)
 * @param quest 关联的任务ID(默认0)
 *
 * 启动跟随模式的核心流程:
 * 1. 如果是护送任务NPC,保存重生时间
 * 2. 检查前置条件(未战斗、未在跟随中)
 * 3. 设置跟随参数(领导者GUID、阵营、任务)
 * 4. 清空移动生成器并暂停移动
 * 5. 移除NPC标志
 * 6. 设置跟随状态为进行中
 * 7. 开始跟随玩家
 */
void FollowerAI::StartFollow(Player* player, uint32 factionForFollower, uint32 quest)
{
    // 如果是护送任务NPC,保存重生时间
    if (CreatureData const* cdata = me->GetCreatureData())
    {
        if (sWorld->getBoolConfig(CONFIG_RESPAWN_DYNAMIC_ESCORTNPC) && (cdata->spawnGroupData->flags & SPAWNGROUP_FLAG_ESCORTQUESTNPC))
            me->SaveRespawnTime(me->GetRespawnDelay());
    }

    // 检查是否在战斗中
    if (me->IsEngaged())
    {
        TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::StartFollow: attempt to StartFollow while in combat. ({})", me->GetGUID().ToString());
        return;
    }

    // 检查是否已经在跟随中
    if (HasFollowState(STATE_FOLLOW_INPROGRESS))
    {
        TC_LOG_ERROR("scripts.ai.followerai", "FollowerAI::StartFollow: attempt to StartFollow while already following. ({})", me->GetGUID().ToString());
        return;
    }

    // 设置跟随参数
    _leaderGUID = player->GetGUID();

    // 如果指定了阵营,则设置阵营
    if (factionForFollower)
        me->SetFaction(factionForFollower);

    // 设置关联任务
    _questForFollow = quest;

    // 清空移动生成器并暂停移动
    me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);
    me->PauseMovement();

    // 移除所有NPC标志,防止玩家交互
    me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);

    // 设置跟随状态为进行中
    AddFollowState(STATE_FOLLOW_INPROGRESS);

    // 开始跟随玩家(使用宠物跟随距离和角度)
    me->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);

    // 记录跟随开始信息
    TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::StartFollow: start follow {} - {} ({})", player->GetName(), _leaderGUID.ToString(), me->GetGUID().ToString());
}

/**
 * @brief 设置跟随暂停状态
 * @param paused true为暂停,false为继续
 *
 * 暂停时添加暂停状态并移除跟随移动生成器,
 * 继续时移除暂停状态并重新开始跟随
 */
void FollowerAI::SetFollowPaused(bool paused)
{
    // 检查是否在跟随中且未完成
    if (!HasFollowState(STATE_FOLLOW_INPROGRESS) || HasFollowState(STATE_FOLLOW_COMPLETE))
        return;

    if (paused)
    {
        // 添加暂停状态
        AddFollowState(STATE_FOLLOW_PAUSED);

        // 如果在跟随状态,移除跟随移动生成器
        if (me->HasUnitState(UNIT_STATE_FOLLOW))
            me->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);
    }
    else
    {
        // 移除暂停状态
        RemoveFollowState(STATE_FOLLOW_PAUSED);

        // 重新开始跟随领导者
        if (Player* leader = GetLeaderForFollower())
            me->GetMotionMaster()->MoveFollow(leader, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }
}

/**
 * @brief 设置跟随完成
 * @param withEndEvent 是否执行后置事件(默认false)
 *
 * 标记跟随完成:
 * 1. 如果在跟随状态,移除跟随移动生成器
 * 2. 如果需要执行后置事件,添加后置事件状态
 * 3. 否则移除后置事件状态
 * 4. 添加完成状态
 */
void FollowerAI::SetFollowComplete(bool withEndEvent)
{
    // 如果在跟随状态,移除跟随移动生成器
    if (me->HasUnitState(UNIT_STATE_FOLLOW))
        me->GetMotionMaster()->Remove(FOLLOW_MOTION_TYPE);

    // 设置后置事件状态
    if (withEndEvent)
        AddFollowState(STATE_FOLLOW_POSTEVENT);
    else
    {
        // 移除后置事件状态
        if (HasFollowState(STATE_FOLLOW_POSTEVENT))
            RemoveFollowState(STATE_FOLLOW_POSTEVENT);
    }

    // 添加完成状态
    AddFollowState(STATE_FOLLOW_COMPLETE);
}

/**
 * @brief 获取跟随的领导者玩家对象
 * @return 玩家指针,如果玩家不存在或不在线返回nullptr
 *
 * 获取逻辑:
 * 1. 通过GUID获取玩家对象
 * 2. 如果玩家存活,直接返回
 * 3. 如果玩家已死亡,但在队伍中有其他存活成员在范围内,自动切换到新领导者
 * 4. 否则返回nullptr
 *
 * @note 此函数会自动切换领导者,这在多人任务中很有用
 */
Player* FollowerAI::GetLeaderForFollower()
{
    // 通过GUID获取玩家对象
    if (Player* player = ObjectAccessor::GetPlayer(*me, _leaderGUID))
    {
        // 如果玩家存活,直接返回
        if (player->IsAlive())
            return player;
        else
        {
            // 玩家已死亡,尝试从队伍中找到新的领导者
            if (Group* group = player->GetGroup())
            {
                // 遍历队伍成员
                for (GroupReference* groupRef = group->GetFirstMember(); groupRef != nullptr; groupRef = groupRef->next())
                {
                    Player* member = groupRef->GetSource();
                    // 检查成员是否在范围内且存活
                    if (member && me->IsWithinDistInMap(member, MAX_PLAYER_DISTANCE) && member->IsAlive())
                    {
                        TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::GetLeaderForFollower: GetLeader changed and returned new leader. ({})", me->GetGUID().ToString());
                        // 更新领导者GUID为新成员
                        _leaderGUID = member->GetGUID();
                        return member;
                    }
                }
            }
        }
    }

    TC_LOG_DEBUG("scripts.ai.followerai", "FollowerAI::GetLeaderForFollower: GetLeader can not find suitable leader. ({})", me->GetGUID().ToString());
    return nullptr;
}

/**
 * @brief 检查是否应该协助玩家战斗
 * @param who 敌对单位
 * @return 如果应该协助返回true,否则返回false
 *
 * 本函数提供对攻击玩家的敌对单位的协助,即使超出正常仇恨范围也会攻击。
 * 这会导致生物攻击任何攻击玩家的敌人(这在官方服务器上也已确认会发生)。
 * 类型标志(type_flag)未确认,但在这里用于进一步研究,是一个很好的候选。
 *
 * 检查条件:
 * 1. 单位有效性和攻击目标
 * 2. 生物类型标志(必须有CAN_ASSIST标志)
 * 3. 敌对单位位置可达性
 * 4. 是否可以攻击敌对单位
 * 5. 脱战状态检查
 * 6. 友好关系检查
 * 7. 距离和视线检查
 */
// This part provides assistance to a player that are attacked by who, even if out of normal aggro range
// It will cause me to attack who that are attacking _any_ player (which has been confirmed may happen also on offi)
// The flag (type_flag) is unconfirmed, but used here for further research and is a good candidate.
bool FollowerAI::ShouldAssistPlayerInCombatAgainst(Unit* who) const
{
    // 检查单位有效性和攻击目标
    if (!who || !who->GetVictim())
        return false;

    // 检查生物类型标志,必须有CAN_ASSIST标志(实验性的未知标志)
    if (!(me->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_CAN_ASSIST))
        return false;

    // 检查敌对单位位置对当前生物是否可达
    if (!who->isInAccessiblePlaceFor(me))
        return false;

    // 检查是否可以攻击敌对单位
    if (!CanAIAttack(who))
        return false;

    // 检查生物是否在脱战状态,脱战状态不能攻击
    if (me->IsInEvadeMode())
        return false;

    // 检查敌对单位是否在脱战状态
    if (who->GetTypeId() == TYPEID_UNIT && who->ToCreature()->IsInEvadeMode())
        return false;

    // 检查是否为友好关系,永远不攻击友好单位
    if (me->IsFriendlyTo(who))
        return false;

    // 检查距离和视线,必须在范围内且有视线
    if (!me->IsWithinDistInMap(who, MAX_PLAYER_DISTANCE) || !me->IsWithinLOSInMap(who))
        return false;

    return true;
}
