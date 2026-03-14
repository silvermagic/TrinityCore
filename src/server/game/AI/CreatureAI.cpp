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
 * @file CreatureAI.cpp
 * @brief CreatureAI 基类实现文件
 *
 * 本文件实现了 CreatureAI 基类，为所有生物 AI 提供核心功能：
 * - 战斗管理：仇恨、交战状态、脱战机制
 * - 移动控制：视线检测、返回出生点
 * - 召唤管理：召唤各种类型的生物
 * - 边界系统：限制生物活动范围
 * - 事件响应：战斗事件、魅惑状态变化等
 *
 * CreatureAI 是所有生物 AI 的基类，派生类包括：
 * - CombatAI：标准战斗 AI
 * - PassiveAI：被动 AI
 * - GuardAI：守卫 AI
 * - PetAI：宠物 AI
 * - ScriptedAI：脚本化 AI（用于自定义 Boss 和怪物行为）
 *
 * @see CreatureAI.h 头文件
 * @see UnitAI 单位 AI 基类
 * @see Creature 生物类
 */

#include "CreatureAI.h"
#include "AreaBoundary.h"
#include "Creature.h"
#include "CreatureAIImpl.h"
#include "CreatureTextMgr.h"
#include "DBCStructure.h"
#include "Language.h"
#include "Log.h"
#include "Map.h"
#include "MapReference.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellHistory.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "World.h"

/// AI 法术信息数组，存储所有法术的 AI 相关数据
AISpellInfoType* UnitAI::AISpellInfo;

/**
 * @brief 获取指定索引的 AI 法术信息
 *
 * 访问 AI 法术信息数组的辅助函数
 *
 * @param i 法术索引
 * @return AISpellInfoType* 法术信息结构体指针
 *
 * @note 用于快速访问法术的 AI 属性（如施法延迟、目标选择等）
 */
AISpellInfoType* GetAISpellInfo(uint32 i) { return &UnitAI::AISpellInfo[i]; }

// ============================================================================
// 构造函数：CreatureAI::CreatureAI
// 职责：初始化生物 AI 的基础状态
// 参数：
//   creature - 关联的生物对象指针，存储为常量成员 me
// 成员初始化：
//   _boundary      - 区域边界指针，用于限制生物活动范围
//   _negateBoundary - 是否反转边界逻辑
//   _isEngaged     - 是否处于交战状态
//   _moveInLOSLocked - 视线检测锁，防止递归调用导致栈溢出
// 调用时机：生物创建时由 Creature::CreateAI() 调用
// ============================================================================
CreatureAI::CreatureAI(Creature* creature) : UnitAI(creature), me(creature), _boundary(nullptr), _negateBoundary(false), _isEngaged(false), _moveInLOSLocked(false)
{
}

/**
 * @brief 析构函数：清理 CreatureAI 资源
 *
 * 清理 AI 相关资源，包括边界检查和交战状态
 * 派生类可能需要重写此函数以清理额外资源
 */
CreatureAI::~CreatureAI()
{
}

/**
 * @brief 发送生物文本消息
 *
 * 通过生物文本管理器发送预定义的聊天消息，支持普通聊天、喊叫、低语等多种形式
 *
 * @param id 文本消息 ID，对应数据库表 creature_text 中的条目
 * @param whisperTarget 低语目标对象（可选），仅在消息类型为低语时使用
 *
 * @note 通常用于 Boss 战脚本中触发战斗台词或剧情对话
 * @see CreatureTextMgr::SendChat
 */
void CreatureAI::Talk(uint8 id, WorldObject const* whisperTarget /*= nullptr*/)
{
    sCreatureTextMgr->SendChat(me, id, whisperTarget);
}

// ============================================================================
// 函数：CreatureAI::OnCharmed
// 职责：处理生物被魅惑状态变化的响应
// 参数：
//   isNew - true 表示魅惑状态变化是新发生的，false 表示状态已存在
// 逻辑说明：
//   当魅惑结束时（isNew=true 且不再被魅惑），检查是否有上一个魅惑者：
//   - 如果生物不是被动反应状态，则主动攻击上一个魅惑者
//   - 如果没有进入战斗，则进入脱战模式
// 调用时机：由 Unit::SetCharmedBy() 和 Unit::RemoveCharmedBy() 触发
// ============================================================================
// Disable CreatureAI when charmed
void CreatureAI::OnCharmed(bool isNew)
{
    if (isNew && !me->IsCharmed() && me->LastCharmerGUID)
    {
        if (!me->HasReactState(REACT_PASSIVE))
        {
            if (Unit* lastCharmer = ObjectAccessor::GetUnit(*me, me->LastCharmerGUID))
                me->EngageWithTarget(lastCharmer);
        }

        me->LastCharmerGUID.Clear();

        if (!me->IsInCombat())
            EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
    }

    UnitAI::OnCharmed(isNew);
}

// ============================================================================
// 函数：CreatureAI::DoZoneInCombat
// 职责：将副本内所有玩家及其宠物/载具拉入战斗
// 参数：
//   creature - 要拉入战斗的生物，默认为 me（当前 AI 关联的生物）
// 返回值：无
// 适用场景：副本 Boss 战开始时，将所有玩家拉入战斗列表
// 限制：仅在副本中有效，非副本地图会记录错误日志
// 性能注意：遍历地图上所有玩家，避免频繁调用
// ============================================================================
void CreatureAI::DoZoneInCombat(Creature* creature /*= nullptr*/)
{
    if (!creature)
        creature = me;

    Map* map = creature->GetMap();
    if (!map->IsDungeon()) // use IsDungeon instead of Instanceable, in case battlegrounds will be instantiated
    {
        TC_LOG_ERROR("scripts.ai", "CreatureAI::DoZoneInCombat: call for map that isn't an instance ({})", creature->GetGUID().ToString());
        return;
    }

    if (!map->HavePlayers())
        return;

    for (MapReference const& ref : map->GetPlayers())
    {
        if (Player* player = ref.GetSource())
        {
            if (!player->IsAlive() || !CombatManager::CanBeginCombat(creature, player))
                continue;

            creature->EngageWithTarget(player);

            for (Unit* pet : player->m_Controlled)
                creature->EngageWithTarget(pet);

            if (Unit* vehicle = player->GetVehicleBase())
                creature->EngageWithTarget(vehicle);
        }
    }
}

// ============================================================================
// 函数：CreatureAI::MoveInLineOfSight_Safe
// 职责：安全的视线检测包装函数，防止递归调用导致栈溢出
// 参数：
//   who - 进入视线范围的单位
// 设计说明：
//   脚本可能在 MoveInLineOfSight 内部再次调用 MoveInLineOfSight，
//   使用 _moveInLOSLocked 锁机制防止递归调用
// 调用时机：由 Creature::MoveInLineOfSight() 触发
// ============================================================================
// scripts does not take care about MoveInLineOfSight loops
// MoveInLineOfSight can be called inside another MoveInLineOfSight and cause stack overflow
void CreatureAI::MoveInLineOfSight_Safe(Unit* who)
{
    if (_moveInLOSLocked == true)
        return;
    _moveInLOSLocked = true;
    MoveInLineOfSight(who);
    _moveInLOSLocked = false;
}

// ============================================================================
// 函数：CreatureAI::MoveInLineOfSight
// 职责：处理单位进入视线范围的反应，触发仇恨初始化
// 参数：
//   who - 进入视线范围的单位
// 行为说明：
//   1. 已交战的生物不响应
//   2. 攻击性反应状态下，如果可以攻击目标则发起仇恨
// 派生类重写：SmartAI 等派生类会重写此函数添加更复杂的检测逻辑
// 调用时机：单位移动时由 Creature::MoveInLineOfSight() 每帧检测调用
// 性能注意：频繁调用，保持逻辑简洁
// ============================================================================
void CreatureAI::MoveInLineOfSight(Unit* who)
{
    if (me->IsEngaged())
        return;

    if (me->HasReactState(REACT_AGGRESSIVE) && me->CanStartAttack(who, false))
        me->EngageWithTarget(who);
}

/**
 * @brief 处理主人战斗交互事件
 *
 * 当生物的主人（所有者）与目标发生战斗交互时被调用
 * 如果生物不是被动状态且可以攻击目标，则主动参战
 *
 * @param target 主人交互的目标单位
 *
 * @note 常见于宠物、召唤物等受控单位，主人攻击时它们也会加入战斗
 */
void CreatureAI::OnOwnerCombatInteraction(Unit* target)
{
    if (!target || !me->IsAlive())
        return;

    if (!me->HasReactState(REACT_PASSIVE) && me->CanStartAttack(target, true))
        me->EngageWithTarget(target);
}

/**
 * @brief 触发警戒反应
 *
 * 当潜行/隐形玩家距离生物过近时触发警戒行为
 * 生物会转向玩家方向并进入分心状态 5 秒
 *
 * @param who 触发警戒的单位（通常是潜行玩家）
 *
 * @note 这是潜行检测机制的一部分，即使玩家未被完全发现，生物也会表现出警觉
 * @note 只对敌对的非平民、非被动、非交战状态的 NPC 有效
 */
void CreatureAI::TriggerAlert(Unit const* who) const
{
    // If there's no target, or target isn't a player do nothing
    if (!who || who->GetTypeId() != TYPEID_PLAYER)
        return;

    // If this unit isn't an NPC, is already distracted, is fighting, is confused, stunned or fleeing, do nothing
    if (me->GetTypeId() != TYPEID_UNIT || me->IsEngaged() || me->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_DISTRACTED))
        return;

    // Only alert for hostiles!
    if (me->IsCivilian() || me->HasReactState(REACT_PASSIVE) || !me->IsHostileTo(who) || !me->_IsTargetAcceptable(who))
        return;

    // Send alert sound (if any) for this creature
    me->SendAIReaction(AI_REACTION_ALERT);

    // Face the unit (stealthed player) and set distracted state for 5 seconds
    me->GetMotionMaster()->MoveDistract(5 * IN_MILLISECONDS, me->GetAbsoluteAngle(who));
}

/**
 * @brief 判断召唤单位是否应该在出生时跟随主人
 *
 * 根据召唤属性确定召唤物是否应该自动跟随其主人
 * 逻辑源自 Spell:EffectSummonType (提交 8499434 之前)
 *
 * @param properties 召唤属性数据（来自 DBC）
 * @return true 召唤物应该跟随主人
 * @return false 召唤物不需要跟随
 *
 * @note 没有召唤属性的召唤物通常是脚本控制的，不跟随任何主人
 * @note 宠物、守护者、仆从等类型默认会跟随主人
 */
// adapted from logic in Spell:EffectSummonType before commit 8499434
static bool ShouldFollowOnSpawn(SummonPropertiesEntry const* properties)
{
    // Summons without SummonProperties are generally scripted summons that don't belong to any owner
    if (!properties)
        return false;

    switch (properties->Control)
    {
        case SUMMON_CATEGORY_PET:
            return true;
        case SUMMON_CATEGORY_WILD:
        case SUMMON_CATEGORY_ALLY:
        case SUMMON_CATEGORY_UNK:
            if (properties->Flags & 512)
                return true;
            switch (properties->Title)
            {
                case SUMMON_TYPE_PET:
                case SUMMON_TYPE_GUARDIAN:
                case SUMMON_TYPE_GUARDIAN2:
                case SUMMON_TYPE_MINION:
                case SUMMON_TYPE_MINIPET:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

/**
 * @brief 生物出现后的回调函数
 *
 * 在生物生成或重新出现时调用（例如从尸体复活、召唤等）
 * 对于召唤物，如果符合条件则自动跟随主人
 *
 * @note 仅对未交战状态的召唤物生效
 * @note 不在载具上的召唤物会清除移动状态并跟随主人
 */
void CreatureAI::JustAppeared()
{
    if (!IsEngaged())
    {
        if (TempSummon* summon = me->ToTempSummon())
        {
            // Only apply this to specific types of summons
            if (!summon->GetVehicle() && ShouldFollowOnSpawn(summon->m_Properties) && summon->CanFollowOwner())
            {
                if (Unit* owner = summon->GetCharmerOrOwner())
                {
                    summon->GetMotionMaster()->Clear();
                    summon->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, summon->GetFollowAngle());
                }
            }
        }
    }
}

/**
 * @brief 进入战斗时的回调函数
 *
 * 当生物首次进入战斗时调用，标记交战状态开始
 * 仅对没有仇恨列表的生物启动交战追踪
 *
 * @param who 使生物进入战斗的目标单位
 *
 * @note 拥有仇恨列表的生物通过仇恨系统管理交战状态
 * @see EngagementStart
 */
void CreatureAI::JustEnteredCombat(Unit* who)
{
    if (!IsEngaged() && !me->CanHaveThreatList())
        EngagementStart(who);
}

// ============================================================================
// 函数：CreatureAI::EnterEvadeMode
// 职责：使生物进入脱战模式，重置状态并返回出生点
// 参数：
//   why - 脱战原因枚举值（EVADE_REASON_NO_HOSTILES、BOUNDARY、NO_PATH 等）
// 行为说明：
//   1. 清除所有战斗相关状态（光环、连击点、战斗状态）
//   2. 重置技能冷却
//   3. 如果有主人则跟随主人，否则返回出生点
//   4. 调用 Reset() 重置 AI 状态
// 调用时机：
//   - 仇恨列表为空
//   - 超出战斗边界
//   - 无法到达目标超过 5 秒
// 性能注意：涉及状态重置，不应频繁调用
// ============================================================================
void CreatureAI::EnterEvadeMode(EvadeReason why)
{
    if (!_EnterEvadeMode(why))
        return;

    TC_LOG_DEBUG("scripts.ai", "CreatureAI::EnterEvadeMode: entering evade mode (why: {}) ({})", why, me->GetGUID().ToString());

    if (!me->GetVehicle()) // otherwise me will be in evade mode forever
    {
        if (Unit* owner = me->GetCharmerOrOwner())
        {
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, me->GetFollowAngle());
        }
        else
        {
            // Required to prevent attacking creatures that are evading and cause them to reenter combat
            // Does not apply to MoveFollow
            me->AddUnitState(UNIT_STATE_EVADE);
            me->GetMotionMaster()->MoveTargetedHome();
        }
    }

    Reset();
}

// ============================================================================
// 函数：CreatureAI::UpdateVictim
// 职责：更新当前攻击目标，处理目标切换逻辑
// 参数：无
// 返回值：
//   true  - 有有效目标，继续战斗
//   false - 无有效目标，应进入脱战
// 行为说明：
//   1. 检查是否处于交战状态
//   2. 检查生物是否存活
//   3. 非被动状态下选择新目标并开始攻击
//   4. 被动状态下停止攻击
// 调用时机：通常在 UpdateAI() 中每帧调用
// 性能注意：每帧调用，保持简洁
// ============================================================================
bool CreatureAI::UpdateVictim()
{
    if (!IsEngaged())
        return false;

    if (!me->IsAlive())
    {
        EngagementOver();
        return false;
    }

    if (!me->HasReactState(REACT_PASSIVE))
    {
        if (Unit* victim = me->SelectVictim())
            if (victim != me->GetVictim())
                AttackStart(victim);

        return me->GetVictim() != nullptr;
    }
    else if (!me->IsInCombat())
    {
        EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
        return false;
    }
    else if (me->GetVictim())
        me->AttackStop();

    return true;
}

/**
 * @brief 启动交战状态
 *
 * 标记生物正式进入交战状态，触发相关的交战事件
 * 如果已经处于交战状态则记录错误日志
 *
 * @param who 使生物进入交战的目标单位
 *
 * @note 调用生物的 AtEngage() 方法处理交战开始逻辑
 * @note 不应重复调用，否则会记录错误
 */
void CreatureAI::EngagementStart(Unit* who)
{
    if (_isEngaged)
    {
        TC_LOG_ERROR("scripts.ai", "CreatureAI::EngagementStart called even though creature is already engaged. Creature debug info:\n{}", me->GetDebugInfo());
        return;
    }
    _isEngaged = true;

    me->AtEngage(who);
}

/**
 * @brief 结束交战状态
 *
 * 标记生物退出交战状态，触发相关的脱战事件
 * 如果未处于交战状态则记录调试日志
 *
 * @note 调用生物的 AtDisengage() 方法处理脱战逻辑
 * @note 通常在脱战模式、死亡或重置时调用
 */
void CreatureAI::EngagementOver()
{
    if (!_isEngaged)
    {
        TC_LOG_DEBUG("scripts.ai", "CreatureAI::EngagementOver called even though creature is not currently engaged. Creature debug info:\n{}", me->GetDebugInfo());
        return;
    }
    _isEngaged = false;

    me->AtDisengage();
}

/**
 * @brief 执行脱战模式的核心逻辑（内部实现）
 *
 * 执行脱战模式的状态清理和重置操作：
 * - 移除所有战斗光环
 * - 清除连击点
 * - 停止战斗
 * - 重置生物附加数据
 * - 清除战利品接收者
 * - 重置技能冷却
 * - 结束交战状态
 *
 * @param why 脱战原因（暂未使用，保留用于扩展）
 * @return true 成功进入脱战模式
 * @return false 已经在脱战模式或已死亡
 *
 * @note 这是 EnterEvadeMode() 的内部实现，不应直接调用
 * @note 在脱战模式或死亡状态下直接返回 false
 */
bool CreatureAI::_EnterEvadeMode(EvadeReason /*why*/)
{
    if (me->IsInEvadeMode())
        return false;

    if (!me->IsAlive())
    {
        EngagementOver();
        return false;
    }

    me->RemoveAurasOnEvade();
    me->ClearComboPointHolders(); // Remove all combo points targeting this unit
    me->CombatStop(true);
    me->LoadCreaturesAddon();
    me->SetLootRecipient(nullptr);
    me->ResetPlayerDamageReq();
    me->SetLastDamagedTime(0);
    me->SetCannotReachTarget(false);
    me->DoNotReacquireSpellFocusTarget();
    me->SetTarget(ObjectGuid::Empty);
    me->GetSpellHistory()->ResetAllCooldowns();
    EngagementOver();

    return true;
}

/// 可视化边界使用的生物 ID
static const uint32 BOUNDARY_VISUALIZE_CREATURE = 15425;

/// 可视化生物的缩放比例
static const float BOUNDARY_VISUALIZE_CREATURE_SCALE = 0.25f;

/// 可视化步进大小（码）
static const int8 BOUNDARY_VISUALIZE_STEP_SIZE = 1;

/// 可视化安全限制（防止无限循环）
static const int32 BOUNDARY_VISUALIZE_FAILSAFE_LIMIT = 750;

/// 可视化生成高度（相对于地面）
static const float BOUNDARY_VISUALIZE_SPAWN_HEIGHT = 5.0f;

/**
 * @brief 可视化生物活动边界
 *
 * 通过召唤可视化生物来显示生物的活动边界范围
 * 使用洪水填充算法遍历边界内所有可到达位置
 *
 * @param duration 可视化生物的存活时间（秒）
 * @param owner 召唤可视化生物的所有者
 * @param fill 是否填充整个边界区域（true）或仅显示边界线（false）
 * @return int32 返回值：
 *              - -1：owner 为空
 *              - LANG_CREATURE_MOVEMENT_NOT_BOUNDED：生物没有边界限制
 *              - LANG_CREATURE_NO_INTERIOR_POINT_FOUND：无法找到内部点
 *              - LANG_CREATURE_MOVEMENT_MAYBE_UNBOUNDED：边界可能无限
 *              - 0：成功
 *
 * @note 用于调试和 GM 工具，可视化生物边界限制
 * @note 使用宽度优先搜索（BFS）算法填充边界区域
 */
int32 CreatureAI::VisualizeBoundary(Seconds duration, Unit* owner, bool fill) const
{
    typedef std::pair<int32, int32> coordinate;

    if (!owner)
        return -1;

    if (!_boundary || _boundary->empty())
        return LANG_CREATURE_MOVEMENT_NOT_BOUNDED;

    std::queue<coordinate> Q;
    std::unordered_set<coordinate> alreadyChecked;
    std::unordered_set<coordinate> outOfBounds;

    Position startPosition = owner->GetPosition();
    if (!IsInBoundary(&startPosition)) // fall back to creature position
    {
        startPosition = me->GetPosition();
        if (!IsInBoundary(&startPosition)) // fall back to creature home position
        {
            startPosition = me->GetHomePosition();
            if (!IsInBoundary(&startPosition))
                return LANG_CREATURE_NO_INTERIOR_POINT_FOUND;
        }
    }
    float spawnZ = startPosition.GetPositionZ() + BOUNDARY_VISUALIZE_SPAWN_HEIGHT;

    bool boundsWarning = false;
    Q.push({ 0,0 });
    while (!Q.empty())
    {
        coordinate front = Q.front();
        bool hasOutOfBoundsNeighbor = false;
        for (coordinate const& off : std::list<coordinate>{ {1, 0}, {0, 1}, {-1, 0}, {0, -1} })
        {
            coordinate next(front.first + off.first, front.second + off.second);
            if (next.first > BOUNDARY_VISUALIZE_FAILSAFE_LIMIT || next.first < -BOUNDARY_VISUALIZE_FAILSAFE_LIMIT || next.second > BOUNDARY_VISUALIZE_FAILSAFE_LIMIT || next.second < -BOUNDARY_VISUALIZE_FAILSAFE_LIMIT)
            {
                boundsWarning = true;
                continue;
            }
            if (alreadyChecked.find(next) == alreadyChecked.end()) // never check a coordinate twice
            {
                Position nextPos(startPosition.GetPositionX() + next.first*BOUNDARY_VISUALIZE_STEP_SIZE, startPosition.GetPositionY() + next.second*BOUNDARY_VISUALIZE_STEP_SIZE, startPosition.GetPositionZ());
                if (IsInBoundary(&nextPos))
                    Q.push(next);
                else
                {
                    outOfBounds.insert(next);
                    hasOutOfBoundsNeighbor = true;
                }
                alreadyChecked.insert(next);
            }
            else if (outOfBounds.find(next) != outOfBounds.end())
                hasOutOfBoundsNeighbor = true;
        }
        if (fill || hasOutOfBoundsNeighbor)
        {
            if (TempSummon* point = owner->SummonCreature(BOUNDARY_VISUALIZE_CREATURE, Position(startPosition.GetPositionX() + front.first * BOUNDARY_VISUALIZE_STEP_SIZE, startPosition.GetPositionY() + front.second * BOUNDARY_VISUALIZE_STEP_SIZE, spawnZ), TEMPSUMMON_TIMED_DESPAWN, duration))
            {
                point->SetObjectScale(BOUNDARY_VISUALIZE_CREATURE_SCALE);
                point->SetUnitFlag(UNIT_FLAG_STUNNED);
                point->SetImmuneToAll(true);
                if (!hasOutOfBoundsNeighbor)
                    point->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            }
        }

        Q.pop();
    }
    return boundsWarning ? LANG_CREATURE_MOVEMENT_MAYBE_UNBOUNDED : 0;
}

/**
 * @brief 检查位置是否在边界范围内
 *
 * 判断指定位置是否在生物的活动边界内
 * 如果没有设置边界，则所有位置都被视为在边界内
 *
 * @param who 要检查的位置（可选，默认为生物当前位置）
 * @return true 位置在边界内（或没有边界限制）
 * @return false 位置在边界外
 *
 * @note 使用 _negateBoundary 标志可以反转边界判断逻辑
 * @see IsInBounds
 */
bool CreatureAI::IsInBoundary(Position const* who) const
{
    if (!_boundary)
        return true;

    if (!who)
        who = me;

    return CreatureAI::IsInBounds(*_boundary, who) != _negateBoundary;
}

/**
 * @brief 静态方法：检查位置是否在边界集合内
 *
 * 遍历所有边界区域，检查位置是否在所有边界范围内
 * 只有通过所有边界检查才返回 true
 *
 * @param boundary 边界集合（可能包含多个边界区域）
 * @param pos 要检查的位置
 * @return true 位置在所有边界范围内
 * @return false 位置在任意一个边界外
 *
 * @note 这是一个静态方法，可以独立使用
 * @note 多个边界采用 AND 逻辑（必须同时满足所有边界条件）
 */
bool CreatureAI::IsInBounds(CreatureBoundary const& boundary, Position const* pos)
{
    for (AreaBoundary const* areaBoundary : boundary)
        if (!areaBoundary->IsWithinBoundary(pos))
            return false;

    return true;
}

/**
 * @brief 检查生物是否在房间（边界）内
 *
 * 检查生物当前位置是否在允许的活动边界内
 * 如果超出边界则进入脱战模式
 *
 * @return true 生物在边界内
 * @return false 生物超出边界（已触发脱战）
 *
 * @note 通常在 UpdateAI() 中调用以确保生物不会离开指定区域
 * @note Boss 战中常用此函数防止生物被拉出战斗区域
 */
bool CreatureAI::CheckInRoom()
{
    if (IsInBoundary())
        return true;
    else
    {
        EnterEvadeMode(EVADE_REASON_BOUNDARY);
        return false;
    }
}

/**
 * @brief 设置生物的活动边界
 *
 * 配置生物的活动范围限制，超出边界将触发脱战
 * 设置后立即执行边界检查
 *
 * @param boundary 边界集合指针（包含多个边界区域的集合）
 * @param negateBoundaries 是否反转边界逻辑（默认 false）
 *                          - false：在边界内为有效
 *                          - true：在边界外为有效
 *
 * @note 边界通常在 AI 初始化时设置
 * @note Boss 战中常用边界限制防止被拉出战斗区域
 * @see AreaBoundary
 */
void CreatureAI::SetBoundary(CreatureBoundary const* boundary, bool negateBoundaries /*= false*/)
{
    _boundary = boundary;
    _negateBoundary = negateBoundaries;
    me->DoImmediateBoundaryCheck();
}

/**
 * @brief 在指定位置召唤生物
 *
 * 在指定坐标召唤指定类型的生物
 *
 * @param entry 要召唤的生物模板 ID（creature_template 表）
 * @param pos 召唤位置
 * @param despawnTime 消失时间（毫秒）
 * @param summonType 召唤类型（TEMPSUMMON_TIMED_DESPAWN 等）
 * @return Creature* 召唤成功的生物指针，失败返回 nullptr
 *
 * @note 这是对 Creature::SummonCreature 的封装
 * @see TempSummonType
 */
Creature* CreatureAI::DoSummon(uint32 entry, Position const& pos, Milliseconds despawnTime, TempSummonType summonType)
{
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}

/**
 * @brief 在对象附近随机位置召唤生物
 *
 * 在指定对象周围的随机位置召唤生物
 *
 * @param entry 要召唤的生物模板 ID
 * @param obj 参照对象（召唤位置以此为基准）
 * @param radius 随机半径（码）
 * @param despawnTime 消失时间（毫秒）
 * @param summonType 召唤类型
 * @return Creature* 召唤成功的生物指针，失败返回 nullptr
 *
 * @note 召唤位置在对象周围 radius 范围内随机生成
 */
Creature* CreatureAI::DoSummon(uint32 entry, WorldObject* obj, float radius, Milliseconds despawnTime, TempSummonType summonType)
{
    Position pos = obj->GetRandomNearPosition(radius);
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}

/**
 * @brief 在对象附近随机位置召唤飞行生物
 *
 * 在指定对象周围的随机位置召唤飞行生物
 * 与 DoSummon 的区别是会在对象 Z 轴基础上增加飞行高度
 *
 * @param entry 要召唤的生物模板 ID
 * @param obj 参照对象
 * @param flightZ 飞行高度（相对于对象 Z 轴的偏移）
 * @param radius 随机半径（码）
 * @param despawnTime 消失时间（毫秒）
 * @param summonType 召唤类型
 * @return Creature* 召唤成功的生物指针，失败返回 nullptr
 *
 * @note 适用于召唤飞行单位（如龙类、鸟类等）
 * @note Z 坐标 = 对象 Z + flightZ
 */
Creature* CreatureAI::DoSummonFlyer(uint32 entry, WorldObject* obj, float flightZ, float radius, Milliseconds despawnTime, TempSummonType summonType)
{
    Position pos = obj->GetRandomNearPosition(radius);
    pos.m_positionZ += flightZ;
    return me->SummonCreature(entry, pos, summonType, despawnTime);
}
