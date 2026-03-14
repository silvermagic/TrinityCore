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
 * @file TemporarySummon.cpp
 * @brief 临时召唤生物系统实现
 *
 * 本模块负责实现游戏中的临时召唤生物系统，包括：
 *   - TempSummon（临时召唤物）：基础的临时召唤生物类
 *   - Minion（从仆）：跟随特定单位的召唤生物
 *   - Guardian（守护者）：具有战斗能力的从仆
 *   - Puppet（傀儡）：由玩家完全控制的召唤生物
 *
 * 召唤类型说明：
 *   - TEMPSUMMON_MANUAL_DESPAWN：需要手动解除召唤
 *   - TEMPSUMMON_TIMED_DESPAWN：定时消失
 *   - TEMPSUMMON_DEAD_DESPAWN：死亡后消失
 *   - TEMPSUMMON_CORPSE_DESPAWN：尸体状态消失
 *   - TEMPSUMMON_TIMED_OR_DEAD_DESPAWN：定时或死亡状态消失
 *
 * 使用场景：
 *   - 法师的水元素、术士的恶魔
 *   - 死亡骑士的食尸鬼
 *   - 萨满的火元素、幽灵狼
 *   - 德鲁伊的树人
 *   - 各种临时触发器
 */

#include "TemporarySummon.h"
#include "CreatureAI.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"

/**
 * @brief TempSummon 构造函数
 *
 * 职责：
 *   初始化临时召唤生物对象，设置基础属性和召唤者信息。
 *
 * 参数：
 *   properties  - 召唤属性配置，包含召唤类型、阵营、槽位等信息
 *   owner       - 召唤者（WorldObject类型，可以是玩家、生物或游戏对象）
 *   isWorldObject - 是否作为世界对象创建
 *
 * 主要流程：
 *   1. 调用父类 Creature 构造函数
 *   2. 初始化成员变量（属性、类型、计时器等）
 *   3. 如果存在召唤者，记录其 GUID
 *   4. 设置单位类型掩码为召唤类型
 */
TempSummon::TempSummon(SummonPropertiesEntry const* properties, WorldObject* owner, bool isWorldObject) :
Creature(isWorldObject), m_Properties(properties), m_type(TEMPSUMMON_MANUAL_DESPAWN),
m_timer(0), m_lifetime(0), m_canFollowOwner(true), m_visibleBySummonerOnly(false)
{
    if (owner)
        m_summonerGUID = owner->GetGUID();

    m_unitTypeMask |= UNIT_MASK_SUMMON;
}

/**
 * @brief 获取召唤者（WorldObject类型）
 *
 * 职责：
 *   获取召唤此临时召唤生物的世界对象。
 *
 * 返回值：
 *   返回召唤者的 WorldObject 指针，如果不存在则返回 nullptr。
 *
 * 说明：
 *   通过 ObjectAccessor 在当前地图中查找对应 GUID 的世界对象。
 */
WorldObject* TempSummon::GetSummoner() const
{
    return m_summonerGUID ? ObjectAccessor::GetWorldObject(*this, m_summonerGUID) : nullptr;
}

/**
 * @brief 获取召唤者（Unit类型）
 *
 * 职责：
 *   获取召唤此临时召唤生物的单位（玩家或生物）。
 *
 * 返回值：
 *   返回召唤者的 Unit 指针，如果不存在或不是单位类型则返回 nullptr。
 */
Unit* TempSummon::GetSummonerUnit() const
{
    if (WorldObject* summoner = GetSummoner())
        return summoner->ToUnit();
    return nullptr;
}

/**
 * @brief 获取召唤者（Creature类型）
 *
 * 职责：
 *   获取召唤此临时召唤生物的生物对象。
 *
 * 返回值：
 *   返回召唤者的 Creature 指针，如果不存在则返回 nullptr。
 */
Creature* TempSummon::GetSummonerCreatureBase() const
{
    return m_summonerGUID ? ObjectAccessor::GetCreature(*this, m_summonerGUID) : nullptr;
}

/**
 * @brief 获取召唤者（GameObject类型）
 *
 * 职责：
 *   获取召唤此临时召唤生物的游戏对象。
 *
 * 返回值：
 *   返回召唤者的 GameObject 指针，如果不存在或不是游戏对象则返回 nullptr。
 */
GameObject* TempSummon::GetSummonerGameObject() const
{
    if (WorldObject* summoner = GetSummoner())
        return summoner->ToGameObject();
    return nullptr;
}

/**
 * @brief 更新临时召唤生物状态
 *
 * 职责：
 *   每帧更新临时召唤生物，处理不同类型的自动消失逻辑。
 *
 * 参数：
 *   diff - 距离上次更新的时间间隔（毫秒）
 *
 * 主要流程：
 *   1. 调用父类 Creature::Update() 进行基础更新
 *   2. 如果生物已死亡（DEAD状态），立即解除召唤
 *   3. 根据召唤类型 m_type 执行不同的消失逻辑：
 *      - TEMPSUMMON_MANUAL_DESPAWN: 需要手动解除召唤
 *      - TEMPSUMMON_DEAD_DESPAWN: 死亡后解除召唤
 *      - TEMPSUMMON_TIMED_DESPAWN: 定时消失
 *      - TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT: 非战斗状态下定时消失
 *      - TEMPSUMMON_CORPSE_TIMED_DESPAWN: 尸体状态定时消失
 *      - TEMPSUMMON_CORPSE_DESPAWN: 变为尸体后立即消失
 *      - TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN: 定时或尸体状态消失
 *      - TEMPSUMMON_TIMED_OR_DEAD_DESPAWN: 定时或死亡状态消失
 */
void TempSummon::Update(uint32 diff)
{
    Creature::Update(diff);

    // 如果生物已处于DEAD状态，立即解除召唤
    if (m_deathState == DEAD)
    {
        UnSummon();
        return;
    }
    switch (m_type)
    {
        case TEMPSUMMON_MANUAL_DESPAWN:
        case TEMPSUMMON_DEAD_DESPAWN:
            // 这两种类型不需要自动消失逻辑
            // MANUAL_DESPAWN: 需要手动调用UnSummon
            // DEAD_DESPAWN: 在上面的DEAD状态检查中已处理
            break;
        case TEMPSUMMON_TIMED_DESPAWN:
        {
            // 定时消失：计时器到期后解除召唤
            if (m_timer <= diff)
            {
                UnSummon();
                return;
            }

            m_timer -= diff;
            break;
        }
        case TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT:
        {
            // 非战斗状态下定时消失：
            // - 战斗中重置计时器
            // - 非战斗状态下正常倒计时
            if (!IsInCombat())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }

                m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;  // 战斗中重置计时器为初始生命周期

            break;
        }

        case TEMPSUMMON_CORPSE_TIMED_DESPAWN:
        {
            // 尸体状态定时消失：仅在CORPSE状态下开始倒计时
            if (m_deathState == CORPSE)
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }

                m_timer -= diff;
            }
            break;
        }
        case TEMPSUMMON_CORPSE_DESPAWN:
        {
            // 尸体状态立即消失：变为尸体后立即解除召唤
            // 注意：如果m_deathState是DEAD，说明CORPSE状态被跳过了
            if (m_deathState == CORPSE)
            {
                UnSummon();
                return;
            }

            break;
        }
        case TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN:
        {
            // 定时或尸体状态消失：
            // - 变为尸体时立即消失
            // - 非战斗状态下定时消失
            if (m_deathState == CORPSE)
            {
                UnSummon();
                return;
            }

            if (!IsInCombat())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }
                else
                    m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;

            break;
        }
        case TEMPSUMMON_TIMED_OR_DEAD_DESPAWN:
        {
            // 定时或死亡状态消失：
            // - 非战斗且存活状态下定时消失
            // - 战斗中重置计时器
            if (!IsInCombat() && IsAlive())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }
                else
                    m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;
            break;
        }
        default:
            // 未知类型，立即解除召唤并记录错误日志
            UnSummon();
            TC_LOG_ERROR("entities.unit", "Temporary summoned creature (entry: {}) have unknown type {} of ", GetEntry(), m_type);
            break;
    }
}

/**
 * @brief 初始化临时召唤生物属性
 *
 * 职责：
 *   设置临时召唤生物的基础属性，包括存活时间、阵营、等级等。
 *
 * 参数：
 *   duration - 召唤生物的存活时间（毫秒），0表示无限存活直到死亡
 *
 * 主要流程：
 *   1. 断言检查不是宠物类型（宠物有独立的初始化流程）
 *   2. 设置计时器和生命周期
 *   3. 如果类型为 MANUAL_DESPAWN，根据持续时间自动选择合适的消失类型
 *   4. 如果是触发器（Trigger）且有技能，继承召唤者的阵营和等级
 *   5. 处理召唤属性配置：
 *      - 槽位管理：如果已有同槽位召唤物，先解除旧的
 *      - 阵营设置：使用属性配置或继承召唤者阵营
 */
void TempSummon::InitStats(uint32 duration)
{
    ASSERT(!IsPet());

    // 设置计时器和生命周期
    m_timer = duration;
    m_lifetime = duration;

    // 如果类型为手动消失，根据持续时间自动选择消失类型
    if (m_type == TEMPSUMMON_MANUAL_DESPAWN)
        m_type = (duration == 0) ? TEMPSUMMON_DEAD_DESPAWN : TEMPSUMMON_TIMED_DESPAWN;

    Unit* owner = GetSummonerUnit();

    // 触发器类型的特殊处理：继承召唤者的阵营和等级
    if (owner && IsTrigger() && m_spells[0])
    {
        SetFaction(owner->GetFaction());
        SetLevel(owner->GetLevel());
        if (owner->GetTypeId() == TYPEID_PLAYER)
            m_ControlledByPlayer = true;
    }

    // 如果没有属性配置，直接返回
    if (!m_Properties)
        return;

    // 处理召唤者相关设置
    if (owner)
    {
        // 槽位管理：确保一个槽位只有一个召唤物
        if (uint32 slot = m_Properties->Slot)
        {
            // 如果该槽位已有其他召唤物，先解除旧的
            if (owner->m_SummonSlot[slot] && owner->m_SummonSlot[slot] != GetGUID())
            {
                Creature* oldSummon = GetMap()->GetCreature(owner->m_SummonSlot[slot]);
                if (oldSummon && oldSummon->IsSummon())
                    oldSummon->ToTempSummon()->UnSummon();
            }
            // 将当前召唤物注册到槽位
            owner->m_SummonSlot[slot] = GetGUID();
        }
    }

    // 设置阵营
    if (m_Properties->Faction)
        SetFaction(m_Properties->Faction);
    else if (IsVehicle() && owner) // 载具继承召唤者阵营
        SetFaction(owner->GetFaction());
}

/**
 * @brief 初始化召唤完成后的处理
 *
 * 职责：
 *   在召唤完成后通知召唤者和被召唤者的AI。
 *
 * 主要流程：
 *   1. 获取召唤者
 *   2. 根据召唤者类型调用相应的AI回调：
 *      - 单位类：调用 JustSummoned()
 *      - 游戏对象类：调用 JustSummoned()
 *   3. 通知被召唤生物的AI它被谁召唤了
 */
void TempSummon::InitSummon()
{
    WorldObject* owner = GetSummoner();
    if (owner)
    {
        // 根据召唤者类型调用相应的AI回调
        if (owner->GetTypeId() == TYPEID_UNIT)
        {
            if (owner->ToCreature()->IsAIEnabled())
                owner->ToCreature()->AI()->JustSummoned(this);
        }
        else if (owner->GetTypeId() == TYPEID_GAMEOBJECT)
        {
            if (owner->ToGameObject()->AI())
                owner->ToGameObject()->AI()->JustSummoned(this);
        }
        // 通知被召唤生物的AI
        if (IsAIEnabled())
            AI()->IsSummonedBy(owner);
    }
}

/**
 * @brief 创建时更新对象可见性
 *
 * 职责：
 *   在召唤生物创建时更新其在世界中的可见性。
 */
void TempSummon::UpdateObjectVisibilityOnCreate()
{
    WorldObject::UpdateObjectVisibility(true);
}

/**
 * @brief 设置临时召唤类型
 *
 * 职责：
 *   设置此临时召唤生物的消失类型。
 *
 * 参数：
 *   type - 召唤类型，决定召唤物如何消失
 */
void TempSummon::SetTempSummonType(TempSummonType type)
{
    m_type = type;
}

/**
 * @brief 解除召唤
 *
 * 职责：
 *   移除临时召唤生物，清理相关资源并通知召唤者。
 *
 * 参数：
 *   msTime - 延迟解除召唤的时间（毫秒），0表示立即解除
 *
 * 主要流程：
 *   1. 如果指定了延迟时间，创建延迟事件并返回
 *   2. 如果是宠物，调用宠物的移除逻辑
 *   3. 通知召唤者AI该召唤物正在消失
 *   4. 将对象添加到移除列表
 */
void TempSummon::UnSummon(uint32 msTime)
{
    // 如果指定了延迟时间，创建延迟事件
    if (msTime)
    {
        ForcedUnsummonDelayEvent* pEvent = new ForcedUnsummonDelayEvent(*this);

        m_Events.AddEvent(pEvent, m_Events.CalculateTime(Milliseconds(msTime)));
        return;
    }

    // 宠物有特殊的移除流程
    //ASSERT(!IsPet());
    if (IsPet())
    {
        ToPet()->Remove(PET_SAVE_NOT_IN_SLOT);
        ASSERT(!IsInWorld());
        return;
    }

    // 通知召唤者AI该召唤物正在消失
    if (WorldObject * owner = GetSummoner())
    {
        if (owner->GetTypeId() == TYPEID_UNIT && owner->ToCreature()->IsAIEnabled())
            owner->ToCreature()->AI()->SummonedCreatureDespawn(this);
        else if (owner->GetTypeId() == TYPEID_GAMEOBJECT && owner->ToGameObject()->AI())
            owner->ToGameObject()->AI()->SummonedCreatureDespawn(this);
    }

    // 将对象添加到移除列表，等待被移出世界
    AddObjectToRemoveList();
}

/**
 * @brief 执行强制解除召唤延迟事件
 *
 * 职责：
 *   当延迟时间到达时，执行解除召唤操作。
 *
 * 参数：
 *   e_time - 事件执行时间
 *   p_time - 处理时间
 *
 * 返回值：
 *   始终返回 true，表示事件执行完成
 */
bool ForcedUnsummonDelayEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    m_owner.UnSummon();
    return true;
}

/**
 * @brief 从世界中移除
 *
 * 职责：
 *   清理召唤槽位并调用父类的移除方法。
 *
 * 主要流程：
 *   1. 检查是否在世界中
 *   2. 如果有属性配置且有槽位，清理召唤者的对应槽位
 *   3. 调用父类 Creature::RemoveFromWorld()
 */
void TempSummon::RemoveFromWorld()
{
    if (!IsInWorld())
        return;

    // 清理召唤者的召唤槽位
    if (m_Properties)
        if (uint32 slot = m_Properties->Slot)
            if (Unit* owner = GetSummonerUnit())
                if (owner->m_SummonSlot[slot] == GetGUID())
                    owner->m_SummonSlot[slot].Clear();

    //if (GetOwnerGUID())
    //    TC_LOG_ERROR("entities.unit", "Unit {} has owner guid when removed from world", GetEntry());

    Creature::RemoveFromWorld();
}

/**
 * @brief 获取调试信息
 *
 * 职责：
 *   生成包含临时召唤生物详细信息的调试字符串。
 *
 * 返回值：
 *   包含调试信息的字符串，包括父类信息、召唤类型、召唤者GUID和计时器值
 */
std::string TempSummon::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Creature::GetDebugInfo() << "\n"
        << std::boolalpha
        << "TempSummonType: " << std::to_string(GetSummonType()) << " Summoner: " << GetSummonerGUID().ToString()
        << "Timer: " << GetTimer();
    return sstr.str();
}

/*============================================================================
 * Minion 类实现 - 从仆类，是TempSummon的子类
 *============================================================================*/

/**
 * @brief Minion 构造函数
 *
 * 职责：
 *   初始化从仆对象，从仆是跟随特定单位的召唤生物。
 *
 * 参数：
 *   properties    - 召唤属性配置
 *   owner         - 拥有者（必须是 Unit 类型）
 *   isWorldObject - 是否作为世界对象创建
 *
 * 主要流程：
 *   1. 调用父类 TempSummon 构造函数
 *   2. 设置单位类型掩码为从仆类型
 *   3. 设置默认跟随角度
 */
Minion::Minion(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject)
    : TempSummon(properties, owner, isWorldObject), m_owner(owner)
{
    ASSERT(m_owner);
    m_unitTypeMask |= UNIT_MASK_MINION;
    m_followAngle = PET_FOLLOW_ANGLE;
}

/**
 * @brief 初始化从仆属性
 *
 * 职责：
 *   设置从仆的基础属性，包括反应状态、创建者GUID、阵营等。
 *
 * 参数：
 *   duration - 存活时间（毫秒）
 *
 * 主要流程：
 *   1. 调用父类 TempSummon::InitStats()
 *   2. 设置反应状态为被动
 *   3. 设置创建者GUID和阵营
 *   4. 注册到拥有者的从仆列表
 */
void Minion::InitStats(uint32 duration)
{
    TempSummon::InitStats(duration);

    // 从仆默认为被动反应状态
    SetReactState(REACT_PASSIVE);

    // 设置创建者GUID和阵营
    SetCreatorGUID(GetOwner()->GetGUID());
    SetFaction(GetOwner()->GetFaction());

    // 将从仆注册到拥有者
    GetOwner()->SetMinion(this, true);
}

/**
 * @brief 从世界中移除从仆
 *
 * 职责：
 *   从拥有者的从仆列表中移除并调用父类移除方法。
 */
void Minion::RemoveFromWorld()
{
    if (!IsInWorld())
        return;

    // 从拥有者的从仆列表中移除
    GetOwner()->SetMinion(this, false);
    TempSummon::RemoveFromWorld();
}

/**
 * @brief 设置死亡状态
 *
 * 职责：
 *   处理从仆死亡时的特殊情况，特别是守护者宠物死亡后的处理。
 *
 * 参数：
 *   s - 新的死亡状态
 *
 * 主要流程：
 *   1. 调用父类 Creature::setDeathState()
 *   2. 如果是守护者宠物刚刚死亡：
 *      - 检查是否有同类型的存活宠物
 *      - 如果有，将其设为当前宠物并初始化宠物界面
 */
void Minion::setDeathState(DeathState s)
{
    Creature::setDeathState(s);
    if (s != JUST_DIED || !IsGuardianPet())
        return;

    // 处理守护者宠物死亡后的切换逻辑
    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER || owner->GetMinionGUID() != GetGUID())
        return;

    // 查找同类型的存活宠物进行切换
    for (Unit* controlled : owner->m_Controlled)
    {
        if (controlled->GetEntry() == GetEntry() && controlled->IsAlive())
        {
            owner->SetMinionGUID(controlled->GetGUID());
            owner->SetPetGUID(controlled->GetGUID());
            owner->ToPlayer()->CharmSpellInitialize();
            break;
        }
    }
}

/**
 * @brief 检查是否为守护者宠物
 *
 * 职责：
 *   判断此从仆是否为守护者类型的宠物。
 *
 * 返回值：
 *   如果是宠物或控制类型为宠物（SUMMON_CATEGORY_PET），返回 true
 */
bool Minion::IsGuardianPet() const
{
    return IsPet() || (m_Properties && m_Properties->Control == SUMMON_CATEGORY_PET);
}

/**
 * @brief 获取调试信息
 *
 * 职责：
 *   生成包含从仆详细信息的调试字符串。
 *
 * 返回值：
 *   包含调试信息的字符串，附加拥有者GUID信息
 */
std::string Minion::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << TempSummon::GetDebugInfo() << "\n"
        << std::boolalpha
        << "Owner: " << (GetOwner() ? GetOwner()->GetGUID().ToString() : "");
    return sstr.str();
}

/*============================================================================
 * Guardian 类实现 - 守护者类，是Minion的子类
 *============================================================================*/

/**
 * @brief Guardian 构造函数
 *
 * 职责：
 *   初始化守护者对象，守护者是具有战斗能力的从仆。
 *
 * 参数：
 *   properties    - 召唤属性配置
 *   owner         - 拥有者
 *   isWorldObject - 是否作为世界对象创建
 *
 * 主要流程：
 *   1. 调用父类 Minion 构造函数
 *   2. 初始化法术伤害加成
 *   3. 初始化从拥有者获得的属性数组
 *   4. 设置单位类型掩码为守护者类型
 *   5. 如果是宠物类型，设置为可控制守护者并初始化魅惑信息
 */
Guardian::Guardian(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject) : Minion(properties, owner, isWorldObject)
, m_bonusSpellDamage(0)
{
    // 初始化从拥有者获得的属性数组为0
    memset(m_statFromOwner, 0, sizeof(float)*MAX_STATS);
    m_unitTypeMask |= UNIT_MASK_GUARDIAN;

    // 如果是宠物类型，设置为可控制守护者
    if (properties && (properties->Title == SUMMON_TYPE_PET || properties->Control == SUMMON_CATEGORY_PET))
    {
        m_unitTypeMask |= UNIT_MASK_CONTROLABLE_GUARDIAN;
        InitCharmInfo();
    }
}

/**
 * @brief 初始化守护者属性
 *
 * 职责：
 *   设置守护者的基础属性，包括等级、法术等。
 *
 * 参数：
 *   duration - 存活时间（毫秒）
 *
 * 主要流程：
 *   1. 调用父类 Minion::InitStats()
 *   2. 根据拥有者等级初始化属性
 *   3. 如果拥有者是玩家且是可控制守护者，初始化魅惑法术
 *   4. 设置反应状态为攻击性
 */
void Guardian::InitStats(uint32 duration)
{
    Minion::InitStats(duration);

    // 根据拥有者等级初始化属性
    InitStatsForLevel(GetOwner()->GetLevel());

    // 如果是玩家拥有的可控制守护者，初始化魅惑法术
    if (GetOwner()->GetTypeId() == TYPEID_PLAYER && HasUnitTypeMask(UNIT_MASK_CONTROLABLE_GUARDIAN))
        m_charmInfo->InitCharmCreateSpells();

    // 守护者默认为攻击性反应状态
    SetReactState(REACT_AGGRESSIVE);
}

/**
 * @brief 初始化召唤完成后的处理
 *
 * 职责：
 *   在守护者召唤完成后初始化玩家客户端的宠物界面。
 *
 * 主要流程：
 *   1. 调用父类 TempSummon::InitSummon()
 *   2. 如果拥有者是玩家且此守护者是其当前宠物，初始化宠物界面
 */
void Guardian::InitSummon()
{
    TempSummon::InitSummon();

    // 如果是玩家的主宠物，初始化宠物界面
    if (GetOwner()->GetTypeId() == TYPEID_PLAYER
            && GetOwner()->GetMinionGUID() == GetGUID()
            && !GetOwner()->GetCharmedGUID())
    {
        GetOwner()->ToPlayer()->CharmSpellInitialize();
    }
}

/**
 * @brief 获取调试信息
 *
 * 职责：
 *   生成包含守护者详细信息的调试字符串。
 *
 * 返回值：
 *   包含调试信息的字符串
 */
std::string Guardian::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Minion::GetDebugInfo();
    return sstr.str();
}

/*============================================================================
 * Puppet 类实现 - 傀儡类，是Minion的子类
 *============================================================================*/

/**
 * @brief Puppet 构造函数
 *
 * 职责：
 *   初始化傀儡对象，傀儡是由玩家完全控制的召唤生物。
 *
 * 参数：
 *   properties - 召唤属性配置
 *   owner      - 拥有者（必须是玩家）
 *
 * 主要流程：
 *   1. 调用父类 Minion 构造函数
 *   2. 断言检查拥有者必须是玩家
 *   3. 设置单位类型掩码为傀儡类型
 */
Puppet::Puppet(SummonPropertiesEntry const* properties, Unit* owner)
    : Minion(properties, owner, false) //maybe true?
{
    // 傀儡的拥有者必须是玩家
    ASSERT(m_owner->GetTypeId() == TYPEID_PLAYER);
    m_unitTypeMask |= UNIT_MASK_PUPPET;
}

/**
 * @brief 初始化傀儡属性
 *
 * 职责：
 *   设置傀儡的基础属性。
 *
 * 参数：
 *   duration - 存活时间（毫秒）
 *
 * 主要流程：
 *   1. 调用父类 Minion::InitStats()
 *   2. 设置等级与拥有者相同
 *   3. 设置反应状态为被动
 */
void Puppet::InitStats(uint32 duration)
{
    Minion::InitStats(duration);
    // 傀儡等级与拥有者相同
    SetLevel(GetOwner()->GetLevel());
    // 傀儡为被动反应状态
    SetReactState(REACT_PASSIVE);
}

/**
 * @brief 初始化召唤完成后的处理
 *
 * 职责：
 *   在傀儡召唤完成后建立控制关系。
 *
 * 主要流程：
 *   1. 调用父类 Minion::InitSummon()
 *   2. 建立附身控制关系（CHARM_TYPE_POSSESS）
 */
void Puppet::InitSummon()
{
    Minion::InitSummon();
    // 建立附身控制关系
    if (!SetCharmedBy(GetOwner(), CHARM_TYPE_POSSESS))
        ABORT();
}

/**
 * @brief 更新傀儡状态
 *
 * 职责：
 *   每帧更新傀儡，检查存活状态。
 *
 * 参数：
 *   time - 距离上次更新的时间间隔（毫秒）
 *
 * 主要流程：
 *   1. 调用父类 Minion::Update()
 *   2. 如果傀儡在世界中但已死亡，解除召唤
 */
void Puppet::Update(uint32 time)
{
    Minion::Update(time);
    // 检查傀儡是否已死亡，如果是则解除召唤
    // 注意：这可能是因为施法者停止引导导致的
    if (IsInWorld())
    {
        if (!IsAlive())
        {
            UnSummon();
            /// @todo 为什么远距离 .die 命令不会移除傀儡
        }
    }
}
