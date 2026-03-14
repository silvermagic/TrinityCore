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
 * @file Creature.cpp
 * @brief 生物实体类的实现文件
 *
 * 本文件实现了游戏世界中所有生物（NPC、怪物等）的核心功能，包括：
 * - 生物的创建、更新、销毁生命周期管理
 * - 生物属性（生命值、能量、属性）的计算和更新
 * - AI行为和移动控制
 * - 战斗系统（仇恨管理、攻击、死亡）
 * - 战利品系统和商贩系统
 * - 生物编队和召唤系统
 * - 重生和尸体腐烂机制
 *
 * Creature类是游戏中最重要的实体类之一，继承自Unit基类，
 * 几乎所有的游戏交互都与生物相关。
 */

#include "Creature.h"
#include "BattlegroundMgr.h"
#include "CellImpl.h"
#include "Common.h"
#include "Containers.h"
#include "CreatureAI.h"
#include "CreatureAISelector.h"
#include "CreatureGroups.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceScript.h"
#include "Log.h"
#include "LootMgr.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PoolMgr.h"
#include "QueryPackets.h"
#include "QuestDef.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Transport.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"
#include "WorldPacket.h"
#include <G3D/g3dmath.h>

/// 生物移动数据构造函数
/// 职责：初始化生物的移动行为参数
/// 参数：无（使用成员初始化列表）
/// 默认值：
///   - Ground: Run (地面移动方式为奔跑)
///   - Flight: None (无飞行能力)
///   - Swim: true (可以游泳)
///   - Rooted: false (不被定身)
///   - Chase: Run (追逐时奔跑)
///   - Random: Walk (随机移动时行走)
///   - InteractionPauseTimer: 从配置读取玩家交互暂停时间
CreatureMovementData::CreatureMovementData() : Ground(CreatureGroundMovementType::Run), Flight(CreatureFlightMovementType::None), Swim(true), Rooted(false), Chase(CreatureChaseMovementType::Run),
Random(CreatureRandomMovementType::Walk), InteractionPauseTimer(sWorld->getIntConfig(CONFIG_CREATURE_STOP_FOR_PLAYER)) { }

/// 将移动数据转换为字符串表示
/// 职责：生成可读的移动参数描述，用于调试和日志输出
/// 返回值：包含所有移动参数的格式化字符串
std::string CreatureMovementData::ToString() const
{
    char const* const GroundStates[] = { "None", "Run", "Hover" };
    char const* const FlightStates[] = { "None", "DisableGravity", "CanFly" };
    char const* const ChaseStates[]  = { "Run", "CanWalk", "AlwaysWalk" };
    char const* const RandomStates[] = { "Walk", "CanRun", "AlwaysRun" };

    std::ostringstream str;
    str << std::boolalpha
        << "Ground: " << GroundStates[AsUnderlyingType(Ground)]
        << ", Swim: " << Swim
        << ", Flight: " << FlightStates[AsUnderlyingType(Flight)]
        << ", Chase: " << ChaseStates[AsUnderlyingType(Chase)]
        << ", Random: " << RandomStates[AsUnderlyingType(Random)];
    if (Rooted)
        str << ", Rooted";
    str << ", InteractionPauseTimer: " << InteractionPauseTimer;

    return str.str();
}

/// 商贩物品计数构造函数
/// 职责：初始化商贩物品的数量追踪
/// 参数：
///   - _item: 物品ID
///   - _count: 当前数量
VendorItemCount::VendorItemCount(uint32 _item, uint32 _count)
    : itemId(_item), count(_count), lastIncrementTime(GameTime::GetGameTime()) { }

/// 检查商贩物品是否需要金币
/// 职责：判断购买该物品是否需要支付金币
/// 参数：pProto - 物品模板
/// 返回值：需要金币返回true，否则返回false
/// 逻辑：如果物品有扩展消耗且没有"忽略购买价格"标志，则不需要金币
bool VendorItem::IsGoldRequired(ItemTemplate const* pProto) const
{
    return pProto->HasFlag(ITEM_FLAG2_DONT_IGNORE_BUY_PRICE) || !ExtendedCost;
}

/**
 * @brief 从商贩物品列表中移除指定物品
 *
 * @param item_id 要移除的物品ID
 * @return true 成功找到并移除了物品
 * @return false 未找到指定物品
 *
 * @note 此函数使用std::remove_if算法进行高效移除
 */
bool VendorItemData::RemoveItem(uint32 item_id)
{
    auto newEnd = std::remove_if(m_items.begin(), m_items.end(), [=](VendorItem const& vendorItem)
    {
        return vendorItem.item == item_id;
    });

    bool found = (newEnd != m_items.end());
    m_items.erase(newEnd, m_items.end());
    return found;
}

/**
 * @brief 查找商贩物品列表中指定物品和扩展消耗的组合
 *
 * @param item_id 物品ID
 * @param extendedCost 扩展消耗ID（如荣誉点数、徽章等）
 * @return VendorItem const* 找到的商贩物品指针，未找到返回nullptr
 *
 * @note 扩展消耗用于实现使用特殊货币购买物品的功能
 */
VendorItem const* VendorItemData::FindItemCostPair(uint32 item_id, uint32 extendedCost) const
{
    for (VendorItem const& vendorItem : m_items)
        if (vendorItem.item == item_id && vendorItem.ExtendedCost == extendedCost)
            return &vendorItem;
    return nullptr;
}

/// 获取随机有效模型ID
/// 职责：从生物模板的四个可能模型ID中随机选择一个有效的
/// 返回值：随机选择的有效模型ID，如果都没有则返回0
/// 用途：用于生物创建时选择随机显示模型
uint32 CreatureTemplate::GetRandomValidModelId() const
{
    uint8 c = 0;
    uint32 modelIDs[4];

    if (Modelid1) modelIDs[c++] = Modelid1;
    if (Modelid2) modelIDs[c++] = Modelid2;
    if (Modelid3) modelIDs[c++] = Modelid3;
    if (Modelid4) modelIDs[c++] = Modelid4;

    return ((c>0) ? modelIDs[urand(0, c-1)] : 0);
}

/// 获取第一个有效模型ID
/// 职责：按顺序返回第一个有效的模型ID
/// 返回值：第一个有效的模型ID，如果都没有则返回0
/// 用途：当不需要随机模型时使用
uint32 CreatureTemplate::GetFirstValidModelId() const
{
    if (Modelid1) return Modelid1;
    if (Modelid2) return Modelid2;
    if (Modelid3) return Modelid3;
    if (Modelid4) return Modelid4;
    return 0;
}

/// 获取第一个隐形模型ID
/// 职责：查找第一个标记为trigger（触发器/隐形）的模型ID
/// 返回值：第一个隐形模型ID，如果没有则返回默认隐形模型11686
/// 用途：用于创建不可见的触发器生物
uint32 CreatureTemplate::GetFirstInvisibleModel() const
{
    CreatureModelInfo const* modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid1);
    if (modelInfo && modelInfo->is_trigger)
        return Modelid1;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid2);
    if (modelInfo && modelInfo->is_trigger)
        return Modelid2;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid3);
    if (modelInfo && modelInfo->is_trigger)
        return Modelid3;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid4);
    if (modelInfo && modelInfo->is_trigger)
        return Modelid4;

    return 11686;
}

/// 获取第一个可见模型ID
/// 职责：查找第一个非trigger（非隐形）的模型ID
/// 返回值：第一个可见模型ID，如果没有则返回默认可见模型17519
/// 用途：用于需要显示的生物
uint32 CreatureTemplate::GetFirstVisibleModel() const
{
    CreatureModelInfo const* modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid1);
    if (modelInfo && !modelInfo->is_trigger)
        return Modelid1;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid2);
    if (modelInfo && !modelInfo->is_trigger)
        return Modelid2;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid3);
    if (modelInfo && !modelInfo->is_trigger)
        return Modelid3;

    modelInfo = sObjectMgr->GetCreatureModelInfo(Modelid4);
    if (modelInfo && !modelInfo->is_trigger)
        return Modelid4;

    return 17519;
}

/// 初始化查询数据
/// 职责：为所有支持的语言预先生成生物查询响应数据
/// 用途：优化客户端查询生物信息时的性能
void CreatureTemplate::InitializeQueryData()
{
    for (uint8 loc = LOCALE_enUS; loc < TOTAL_LOCALES; ++loc)
        QueryData[loc] = BuildQueryData(static_cast<LocaleConstant>(loc));
}

/// 构建查询数据包
/// 职责：构建响应客户端生物查询的数据包
/// 参数：loc - 语言常量
/// 返回值：包含生物信息的世界数据包
/// 主要内容：
///   - 生物ID
///   - 名称和标题（根据语言本地化）
///   - 光标图标名称
///   - 类型标志、生物类型、家族、阶级
///   - 击杀信用ID数组
///   - 四个模型显示ID
///   - 生命值和能量倍数
///   - 是否为种族领袖
///   - 任务物品列表
///   - 移动信息ID
/// 调用时机：客户端查询生物信息时
WorldPacket CreatureTemplate::BuildQueryData(LocaleConstant loc) const
{
    WorldPackets::Query::QueryCreatureResponse queryTemp;

    std::string locName = Name, locTitle = Title;
    if (CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(Entry))
    {
        ObjectMgr::GetLocaleString(cl->Name, loc, locName);
        ObjectMgr::GetLocaleString(cl->Title, loc, locTitle);
    }

    queryTemp.CreatureID = Entry;
    queryTemp.Allow = true;

    queryTemp.Stats.Name = locName;
    queryTemp.Stats.NameAlt = locTitle;
    queryTemp.Stats.CursorName = IconName;
    queryTemp.Stats.Flags = type_flags;
    queryTemp.Stats.CreatureType = type;
    queryTemp.Stats.CreatureFamily = family;
    queryTemp.Stats.Classification = rank;
    memcpy(queryTemp.Stats.ProxyCreatureID, KillCredit, sizeof(uint32) * MAX_KILL_CREDIT);
    queryTemp.Stats.CreatureDisplayID[0] = Modelid1;
    queryTemp.Stats.CreatureDisplayID[1] = Modelid2;
    queryTemp.Stats.CreatureDisplayID[2] = Modelid3;
    queryTemp.Stats.CreatureDisplayID[3] = Modelid4;
    queryTemp.Stats.HpMulti = ModHealth;
    queryTemp.Stats.EnergyMulti = ModMana;
    queryTemp.Stats.Leader = RacialLeader;

    for (uint32 i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
        queryTemp.Stats.QuestItems[i] = 0;

    if (std::vector<uint32> const* items = sObjectMgr->GetCreatureQuestItemList(Entry))
        for (uint32 i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
            if (i < items->size())
                queryTemp.Stats.QuestItems[i] = (*items)[i];

    queryTemp.Stats.CreatureMovementInfoID = movementId;
    queryTemp.Write();
    queryTemp.ShrinkToFit();
    return queryTemp.Move();
}

/// 援助延迟事件执行
/// 职责：处理延迟的援助请求，让协助者加入战斗
/// 参数：
///   - e_time: 事件执行时间（未使用）
///   - p_time: 上一时间（未使用）
/// 返回值：总是返回true
/// 主要流程：
///   1. 获取受害者对象
///   2. 遍历所有协助者
///   3. 验证协助者存在且可以协助
///   4. 设置协助者不再呼叫援助（防止连锁）
///   5. 让协助者攻击受害者
/// 调用时机：援助延迟事件触发时
bool AssistDelayEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    if (Unit* victim = ObjectAccessor::GetUnit(m_owner, m_victim))
    {
        while (!m_assistants.empty())
        {
            Creature* assistant = ObjectAccessor::GetCreature(m_owner, *m_assistants.begin());
            m_assistants.pop_front();

            if (assistant && assistant->CanAssistTo(&m_owner, victim))
            {
                assistant->SetNoCallAssistance(true);
                assistant->EngageWithTarget(victim);
            }
        }
    }
    return true;
}

/**
 * @brief 获取生物基础属性
 *
 * @param level 生物等级
 * @param unitClass 生物职业（战士、法师等）
 * @return CreatureBaseStats const* 基础属性结构指针
 *
 * @note 基础属性包括基础生命值、基础法力值、基础护甲等，
 *       这些值会根据等级和职业从数据库中获取
 */
CreatureBaseStats const* CreatureBaseStats::GetBaseStats(uint8 level, uint8 unitClass)
{
    return sObjectMgr->GetCreatureBaseStats(level, unitClass);
}

/**
 * @brief 强制消失延迟事件执行
 *
 * @param e_time 事件执行时间（未使用）
 * @param p_time 上一时间（未使用）
 * @return true 总是返回true
 *
 * @brief 职责：执行延迟消失逻辑，让生物消失或取消召唤
 * @details 由于此函数被调用时，对象类型已经在运行时确定，
 *          所以这里不会是临时召唤物类型
 */
bool ForcedDespawnDelayEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    m_owner.DespawnOrUnsummon(0s, m_respawnTimer);    // since we are here, we are not TempSummon as object type cannot change during runtime
    return true;
}

/// 生物类构造函数
/// 职责：初始化生物对象的所有成员变量
/// 参数：isWorldObject - 是否作为世界对象（默认true）
/// 初始化项：
///   - 战利品相关：团队战利品计时器、战利品接收者
///   - 重生相关：重生时间、重生延迟（300秒）、尸体延迟（60秒）
///   - 战斗相关：反应状态（侵略性）、边界检查时间（2.5秒）
///   - 移动相关：默认移动类型（空闲）、游荡距离
///   - AI相关：法术列表初始化、视野距离、战斗距离
///   - 标志相关：生命值恢复标志、声誉获取禁用标志
/// 调用时机：创建新的生物对象时
Creature::Creature(bool isWorldObject): Unit(isWorldObject), MapObject(), m_groupLootTimer(0), lootingGroupLowGUID(0), m_PlayerDamageReq(0), m_lootRecipient(), m_lootRecipientGroup(0), _pickpocketLootRestore(0),
    m_corpseRemoveTime(0), m_respawnTime(0), m_respawnDelay(300), m_corpseDelay(60), m_ignoreCorpseDecayRatio(false), m_wanderDistance(0.0f), m_boundaryCheckTime(2500), m_combatPulseTime(0), m_combatPulseDelay(0), m_reactState(REACT_AGGRESSIVE),
    m_defaultMovementType(IDLE_MOTION_TYPE), m_spawnId(0), m_equipmentId(0), m_originalEquipmentId(0), m_AlreadyCallAssistance(false), m_AlreadySearchedAssistance(false), m_cannotReachTarget(false), m_cannotReachTimer(0),
    m_meleeDamageSchoolMask(SPELL_SCHOOL_MASK_NORMAL), m_originalEntry(0), m_homePosition(), m_transportHomePosition(), m_creatureInfo(nullptr), m_creatureData(nullptr), _waypointPathId(0), _currentWaypointNodeInfo(0, 0),
    m_formation(nullptr), m_triggerJustAppeared(true), m_respawnCompatibilityMode(false), _lastDamagedTime(0),
    _regenerateHealth(true), _regenerateHealthLock(false), _isMissingCanSwimFlagOutOfCombat(false)
{
    m_regenTimer = CREATURE_REGEN_INTERVAL;
    m_valuesCount = UNIT_END;

    for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
        m_spells[i] = 0;

    DisableReputationGain = false;

    m_SightDistance = sWorld->getFloatConfig(CONFIG_SIGHT_MONSTER);
    m_CombatDistance = 0;//MELEE_RANGE;

    ResetLootMode(); // restore default loot mode
    m_isTempWorldObject = false;
}

/// 将生物添加到游戏世界
/// 职责：注册生物到游戏世界的对象存储和查找系统
/// 主要流程：
///   1. 将生物插入地图的对象存储器
///   2. 如果有spawnId，注册到spawnId查找表
///   3. 调用Unit::AddToWorld()进行基础注册
///   4. 搜索并加入编队（如果存在）
///   5. 初始化AI
///   6. 如果是载具，安装载具组件
///   7. 通知区域脚本生物创建
/// 调用时机：生物被创建或生成时
/// 注意：必须确保生物未在世界中
void Creature::AddToWorld()
{
    ///- Register the creature for guid lookup
    if (!IsInWorld())
    {
        GetMap()->GetObjectsStore().Insert<Creature>(GetGUID(), this);
        if (m_spawnId)
            GetMap()->GetCreatureBySpawnIdStore().insert(std::make_pair(m_spawnId, this));

        TC_LOG_DEBUG("entities.unit", "Adding creature {} with DBGUID {} to world in map {}", GetGUID().ToString(), m_spawnId, GetMap()->GetId());

        Unit::AddToWorld();
        SearchFormation();
        AIM_Initialize();
        if (IsVehicle())
            GetVehicleKit()->Install();

        if (GetZoneScript())
            GetZoneScript()->OnCreatureCreate(this);
    }
}

/// 将生物从游戏世界中移除
/// 职责：从游戏世界的对象存储和查找系统中注销生物
/// 主要流程：
///   1. 通知区域脚本生物即将移除
///   2. 如果在编队中，从编队移除
///   3. 调用Unit::RemoveFromWorld()进行基础移除
///   4. 从spawnId查找表中移除
///   5. 从地图对象存储器中移除
/// 调用时机：生物被删除或卸载时
/// 注意：必须确保生物在世界中
void Creature::RemoveFromWorld()
{
    if (IsInWorld())
    {
        if (GetZoneScript())
            GetZoneScript()->OnCreatureRemove(this);

        if (m_formation)
            sFormationMgr->RemoveCreatureFromGroup(m_formation, this);

        Unit::RemoveFromWorld();

        if (m_spawnId)
            Trinity::Containers::MultimapErasePair(GetMap()->GetCreatureBySpawnIdStore(), m_spawnId, this);

        TC_LOG_DEBUG("entities.unit", "Removing creature {} with DBGUID {} to world in map {}", GetGUID().ToString(), m_spawnId, GetMap()->GetId());
        GetMap()->GetObjectsStore().Remove<Creature>(GetGUID());
    }
}

/// 检查生物是否正在返回出生点
/// 职责：判断生物当前是否在执行返回出生点的移动
/// 返回值：正在返回出生点返回true，否则返回false
/// 检查方式：检查移动管理器的当前移动类型是否为HOME_MOTION_TYPE
bool Creature::IsReturningHome() const
{
    if (GetMotionMaster()->GetCurrentMovementGeneratorType() == HOME_MOTION_TYPE)
        return true;

    return false;
}

/// 搜索编队
/// 职责：查找并加入生物所属的编队
/// 主要流程：
///   1. 如果是召唤物，返回（召唤物不加入编队）
///   2. 如果没有spawnId，返回
///   3. 从编队管理器获取编队信息
///   4. 如果找到编队信息，加入编队
/// 调用时机：生物添加到世界时
void Creature::SearchFormation()
{
    if (IsSummon())
        return;

    ObjectGuid::LowType lowguid = GetSpawnId();
    if (!lowguid)
        return;

    if (FormationInfo const* formationInfo = sFormationMgr->GetFormationInfo(lowguid))
        sFormationMgr->AddCreatureToGroup(formationInfo->LeaderSpawnId, this);
}

/**
 * @brief 检查生物是否为编队领袖
 *
 * @return true 该生物是编队领袖
 * @return false 该生物不是编队领袖或没有编队
 *
 * @note 编队领袖负责领导整个编队的移动和行为
 */
bool Creature::IsFormationLeader() const
{
    if (!m_formation)
        return false;

    return m_formation->IsLeader(this);
}

/**
 * @brief 通知编队开始移动
 *
 * @brief 职责：如果是编队领袖，通知编队系统领袖已开始移动
 * @details 编队成员会跟随领袖的移动模式
 *
 * @note 只有编队领袖才能触发此通知
 */
void Creature::SignalFormationMovement()
{
    if (!m_formation)
        return;

    if (!m_formation->IsLeader(this))
        return;

    m_formation->LeaderStartedMoving();
}

/**
 * @brief 检查编队领袖是否允许移动
 *
 * @return true 允许移动
 * @return false 不允许移动或没有编队
 *
 * @note 此函数用于编队移动的同步控制，防止成员和领袖移动不同步
 */
bool Creature::IsFormationLeaderMoveAllowed() const
{
    if (!m_formation)
        return false;

    return m_formation->CanLeaderStartMoving();
}

/// 移除生物尸体
/// 职责：处理生物尸体消失逻辑，准备重生
/// 参数：
///   - setSpawnTime: 是否设置重生时间
///   - destroyForNearbyPlayers: 是否对附近玩家销毁该对象
/// 主要流程：
///   - 兼容模式(m_respawnCompatibilityMode):
///     1. 设置尸体移除时间为当前时间
///     2. 将死亡状态设为DEAD
///     3. 移除所有光环和战利品
///     4. 通知AI尸体被移除
///     5. 如果需要，销毁给附近玩家
///     6. 设置重生时间
///     7. 将生物传送回重生点
///   - 正常模式:
///     1. 通知AI尸体被移除
///     2. 设置并保存重生时间
///     3. 如果是临时召唤物则取消召唤，否则添加到移除列表
/// 调用时机：尸体腐烂时间到期或强制移除尸体时
void Creature::RemoveCorpse(bool setSpawnTime, bool destroyForNearbyPlayers)
{
    if (getDeathState() != CORPSE)
        return;

    if (m_respawnCompatibilityMode)
    {
        m_corpseRemoveTime = GameTime::GetGameTime();
        setDeathState(DEAD);
        RemoveAllAuras();
        loot.clear();
        uint32 respawnDelay = m_respawnDelay;
        if (CreatureAI* ai = AI())
            ai->CorpseRemoved(respawnDelay);

        if (destroyForNearbyPlayers)
            DestroyForNearbyPlayers();

        // Should get removed later, just keep "compatibility" with scripts
        if (setSpawnTime)
            m_respawnTime = std::max<time_t>(GameTime::GetGameTime() + respawnDelay, m_respawnTime);

        // if corpse was removed during falling, the falling will continue and override relocation to respawn position
        if (IsFalling())
            StopMoving();

        float x, y, z, o;
        GetRespawnPosition(x, y, z, &o);

        // We were spawned on transport, calculate real position
        if (IsSpawnedOnTransport())
        {
            Position& pos = m_movementInfo.transport.pos;
            pos.m_positionX = x;
            pos.m_positionY = y;
            pos.m_positionZ = z;
            pos.SetOrientation(o);

            if (TransportBase* transport = GetDirectTransport())
                transport->CalculatePassengerPosition(x, y, z, &o);
        }

        UpdateAllowedPositionZ(x, y, z);
        SetHomePosition(x, y, z, o);
        GetMap()->CreatureRelocation(this, x, y, z, o);
    }
    else
    {
        if (CreatureAI* ai = AI())
            ai->CorpseRemoved(m_respawnDelay);

        // In case this is called directly and normal respawn timer not set
        // Since this timer will be longer than the already present time it
        // will be ignored if the correct place added a respawn timer
        if (setSpawnTime)
        {
            uint32 respawnDelay = m_respawnDelay;
            m_respawnTime = std::max<time_t>(GameTime::GetGameTime() + respawnDelay, m_respawnTime);

            SaveRespawnTime();
        }

        if (TempSummon* summon = ToTempSummon())
            summon->UnSummon();
        else
            AddObjectToRemoveList();
    }
}

/// 初始化生物条目
/// 职责：设置生物的基础属性和显示信息
/// 参数：
///   - entry: 生物的模板ID
///   - data: 可选的生物数据，用于覆盖默认值
/// 返回值：初始化成功返回true，模板不存在或模型无效返回false
/// 主要流程：
///   1. 获取生物模板
///   2. 根据地图难度选择合适的模板（副本难度调整）
///   3. 设置条目、种族、职业
///   4. 选择并设置显示模型ID
///   5. 加载装备
///   6. 设置名称和速度
///   7. 设置模型大小和碰撞半径
///   8. 加载法术列表
///   9. 设置默认移动类型
/// 调用时机：生物创建或条目更新时
/// 注意：此函数不会修改生物的阵营和属性，这些由UpdateEntry处理
bool Creature::InitEntry(uint32 entry, CreatureData const* data /*= nullptr*/)
{
    CreatureTemplate const* normalInfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!normalInfo)
    {
        TC_LOG_ERROR("sql.sql", "Creature::InitEntry creature entry {} does not exist.", entry);
        return false;
    }

    // get difficulty 1 mode entry, skip for pets
    CreatureTemplate const* cinfo = normalInfo;
    for (uint8 diff = uint8(GetMap()->GetSpawnMode()); diff > 0 && !IsPet();)
    {
        // we already have valid Map pointer for current creature!
        if (normalInfo->DifficultyEntry[diff - 1])
        {
            cinfo = sObjectMgr->GetCreatureTemplate(normalInfo->DifficultyEntry[diff - 1]);
            if (cinfo)
                break;                                      // template found

            // check and reported at startup, so just ignore (restore normalInfo)
            cinfo = normalInfo;
        }

        // for instances heroic to normal, other cases attempt to retrieve previous difficulty
        if (diff >= RAID_DIFFICULTY_10MAN_HEROIC && GetMap()->IsRaid())
            diff -= 2;                                      // to normal raid difficulty cases
        else
            --diff;
    }

    // Initialize loot duplicate count depending on raid difficulty
    if (GetMap()->Is25ManRaid())
        loot.maxDuplicates = 3;

    SetEntry(entry);                                        // normal entry always
    m_creatureInfo = cinfo;                                 // map mode related always

    // equal to player Race field, but creature does not have race
    SetRace(RACE_NONE);

    // known valid are: CLASS_WARRIOR, CLASS_PALADIN, CLASS_ROGUE, CLASS_MAGE
    SetClass(uint8(cinfo->unit_class));

    // Cancel load if no model defined
    if (!(cinfo->GetFirstValidModelId()))
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has no model defined in table `creature_template`, can't load. ", entry);
        return false;
    }

    uint32 displayID = ObjectMgr::ChooseDisplayId(GetCreatureTemplate(), data);
    CreatureModelInfo const* minfo = sObjectMgr->GetCreatureModelRandomGender(&displayID);
    if (!minfo)                                             // Cancel load if no model defined
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid model {} defined in table `creature_template`, can't load.", entry, displayID);
        return false;
    }

    SetDisplayId(displayID);
    SetNativeDisplayId(displayID);

    // Load creature equipment
    if (!data)
        LoadEquipment();  // use default equipment (if available) for summons
    else if (data->equipmentId == 0)
        LoadEquipment(0); // 0 means no equipment for creature table
    else
    {
        m_originalEquipmentId = data->equipmentId;
        LoadEquipment(data->equipmentId);
    }

    SetName(normalInfo->Name);                              // at normal entry always

    SetModCastingSpeed(1.0f);

    SetSpeedRate(MOVE_WALK,   cinfo->speed_walk);
    SetSpeedRate(MOVE_RUN,    cinfo->speed_run);
    SetSpeedRate(MOVE_SWIM,   1.0f); // using 1.0 rate
    SetSpeedRate(MOVE_FLIGHT, 1.0f); // using 1.0 rate

    // Will set UNIT_FIELD_BOUNDINGRADIUS and UNIT_FIELD_COMBATREACH
    SetObjectScale(GetNativeObjectScale());

    SetHoverHeight(cinfo->HoverHeight);

    SetCanDualWield(cinfo->flags_extra & CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK);

    // checked at loading
    m_defaultMovementType = MovementGeneratorType(data ? data->movementType : cinfo->MovementType);
    if (!m_wanderDistance && m_defaultMovementType == RANDOM_MOTION_TYPE)
        m_defaultMovementType = IDLE_MOTION_TYPE;

    for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
        m_spells[i] = GetCreatureTemplate()->spells[i];

    return true;
}

/// 更新生物条目
/// 职责：完整更新生物的所有属性、阵营、标志等
/// 参数：
///   - entry: 新的生物模板ID
///   - data: 可选的生物数据，用于覆盖默认值
///   - updateLevel: 是否更新等级（默认为true）
/// 返回值：更新成功返回true，失败返回false
/// 主要流程：
///   1. 调用InitEntry初始化基础属性
///   2. 设置生命值恢复标志
///   3. 设置武器姿态
///   4. 设置阵营
///   5. 设置NPC标志、单位标志、动态标志
///   6. 如果需要，更新等级和属性
///   7. 设置抗性和攻击时间
///   8. 根据阵营模板设置PVP标志
///   9. 处理载具相关逻辑
///   10. 初始化反应状态
///   11. 加载免疫、移动标志和附加数据
/// 调用时机：生物需要改变类型或重新加载模板时
/// 注意：保留战斗状态标志
bool Creature::UpdateEntry(uint32 entry, CreatureData const* data /*= nullptr*/, bool updateLevel /* = true */)
{
    if (!InitEntry(entry, data))
        return false;

    CreatureTemplate const* cInfo = GetCreatureTemplate();

    _regenerateHealth = cInfo->RegenHealth;

    // creatures always have melee weapon ready if any unless specified otherwise
    if (!GetCreatureAddon())
        SetSheath(SHEATH_STATE_MELEE);

    SetFaction(cInfo->faction);

    uint32 npcFlags, unitFlags, dynamicFlags;
    ObjectMgr::ChooseCreatureFlags(cInfo, &npcFlags, &unitFlags, &dynamicFlags, data);

    if (cInfo->flags_extra & CREATURE_FLAG_EXTRA_WORLDEVENT)
        npcFlags |= sGameEventMgr->GetNPCFlag(this);

    ReplaceAllNpcFlags(NPCFlags(npcFlags));

    // if unit is in combat, keep this flag
    unitFlags &= ~UNIT_FLAG_IN_COMBAT;
    if (IsInCombat())
        unitFlags |= UNIT_FLAG_IN_COMBAT;

    ReplaceAllUnitFlags(UnitFlags(unitFlags));
    ReplaceAllUnitFlags2(UnitFlags2(cInfo->unit_flags2));

    ReplaceAllDynamicFlags(dynamicFlags);

    SetCanDualWield(cInfo->flags_extra & CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK);

    SetAttackTime(BASE_ATTACK,   cInfo->BaseAttackTime);
    SetAttackTime(OFF_ATTACK,    cInfo->BaseAttackTime);
    SetAttackTime(RANGED_ATTACK, cInfo->RangeAttackTime);

    if (updateLevel)
        SelectLevel();

    // Do not update guardian stats here - they are handled in Guardian::InitStatsForLevel()
    if (!IsGuardian())
    {
        uint32 previousHealth = GetHealth();
        UpdateLevelDependantStats();
        if (previousHealth > 0)
            SetHealth(previousHealth);

        SetMeleeDamageSchool(SpellSchools(cInfo->dmgschool));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_HOLY,   BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_HOLY]));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_FIRE,   BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_FIRE]));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_NATURE, BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_NATURE]));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_FROST,  BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_FROST]));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_SHADOW, BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_SHADOW]));
        SetStatFlatModifier(UNIT_MOD_RESISTANCE_ARCANE, BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_ARCANE]));

        SetCanModifyStats(true);
        UpdateAllStats();
    }

    // checked and error show at loading templates
    if (FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(cInfo->faction))
        SetPvP((factionTemplate->Flags & FACTION_TEMPLATE_FLAG_PVP) != 0);

    // updates spell bars for vehicles and set player's faction - should be called here, to overwrite faction that is set from the new template
    if (IsVehicle())
    {
        if (Player* owner = Creature::GetCharmerOrOwnerPlayerOrPlayerItself()) // this check comes in case we don't have a player
        {
            SetFaction(owner->GetFaction()); // vehicles should have same as owner faction
            owner->VehicleSpellInitialize();
        }
    }

    // trigger creature is always uninteractible and can not be attacked
    if (IsTrigger())
        SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

    InitializeReactState();

    if (cInfo->flags_extra & CREATURE_FLAG_EXTRA_NO_TAUNT)
    {
        ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, true);
        ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, true);
    }

    SetIsCombatDisallowed((cInfo->flags_extra & CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT) != 0);

    LoadTemplateRoot();
    InitializeMovementFlags();

    LoadCreaturesAddon();
    LoadTemplateImmunities();

    GetThreatManager().EvaluateSuppressed();

    //We must update last scriptId or it looks like we reloaded a script, breaking some things such as gossip temporarily
    LastUsedScriptID = GetScriptId();

    return true;
}

/// 设置相位掩码
/// 职责：更改生物的相位并更新相关乘客
/// 参数：
///   - newPhaseMask: 新的相位掩码
///   - update: 是否立即更新对象可见性
/// 主要流程：
///   1. 如果相位未改变，直接返回
///   2. 设置新相位
///   3. 更新载具上所有乘客的相位
///   4. 如果需要，更新对象可见性
/// 调用时机：生物需要改变相位时
void Creature::SetPhaseMask(uint32 newPhaseMask, bool update)
{
    if (newPhaseMask == GetPhaseMask())
        return;

    Unit::SetPhaseMask(newPhaseMask, false);

    if (Vehicle* vehicle = GetVehicleKit())
    {
        for (auto seat = vehicle->Seats.begin(); seat != vehicle->Seats.end(); seat++)
            if (Unit* passenger = ObjectAccessor::GetUnit(*this, seat->second.Passenger.Guid))
                passenger->SetPhaseMask(newPhaseMask, update);
    }

    if (update)
        UpdateObjectVisibility();
}

/// 生物更新函数（核心循环）
/// 职责：每帧更新生物的状态，处理死亡、重生、尸体腐烂、生命值恢复等
/// 参数：diff - 距离上次更新的时间差（毫秒）
/// 主要流程：
///   1. 检查并触发JustAppeared事件（刚出现时）
///   2. 更新移动标志
///   3. 根据死亡状态执行不同逻辑：
///      - JUST_RESPAWNED: 错误状态，记录日志
///      - JUST_DIED: 错误状态，记录日志
///      - DEAD: 检查是否需要重生
///        * 检查spawn group是否激活
///        * 检查关联重生时间
///        * 执行重生逻辑
///      - CORPSE: 尸体状态
///        * 调用Unit::Update
///        * 如果在战斗中，更新AI
///        * 处理团队战利品分配计时器
///        * 检查尸体移除时间
///      - ALIVE: 存活状态
///        * 调用Unit::Update
///        * 更新威胁管理器
///        * 处理法术焦点延迟
///        * 检查 evade boundary（边界检查）
///        * 处理战斗脉冲（副本中）
///        * 调用AI更新
///        * 生命值和能量恢复
///        * 无法到达目标的处理
/// 调用时机：每帧游戏循环
/// 性能注意：这是高频调用的函数，需要保持高效
void Creature::Update(uint32 diff)
{
    if (IsAIEnabled() && m_triggerJustAppeared && m_deathState != DEAD)
    {
        if (m_respawnCompatibilityMode && m_vehicleKit)
            m_vehicleKit->Reset();
        m_triggerJustAppeared = false;
        AI()->JustAppeared();
    }

    UpdateMovementFlags();

    switch (m_deathState)
    {
        case JUST_RESPAWNED:
            // Must not be called, see Creature::setDeathState JUST_RESPAWNED -> ALIVE promoting.
            TC_LOG_ERROR("entities.unit", "Creature {} in wrong state: JUST_RESPAWNED (4)", GetGUID().ToString());
            break;
        case JUST_DIED:
            // Must not be called, see Creature::setDeathState JUST_DIED -> CORPSE promoting.
            TC_LOG_ERROR("entities.unit", "Creature {} in wrong state: JUST_DIED (1)", GetGUID().ToString());
            break;
        case DEAD:
        {
            if (!m_respawnCompatibilityMode)
            {
                TC_LOG_ERROR("entities.unit", "Creature {} in wrong state: DEAD (3)", GetGUID().ToString());
                break;
            }
            time_t now = GameTime::GetGameTime();
            if (m_respawnTime <= now)
            {
                // Delay respawn if spawn group is not active
                if (m_creatureData && !GetMap()->IsSpawnGroupActive(m_creatureData->spawnGroupData->groupId))
                {
                    m_respawnTime = now + urand(4,7);
                    break; // Will be rechecked on next Update call after delay expires
                }

                ObjectGuid dbtableHighGuid(HighGuid::Unit, GetEntry(), m_spawnId);
                time_t linkedRespawnTime = GetMap()->GetLinkedRespawnTime(dbtableHighGuid);
                if (!linkedRespawnTime)             // Can respawn
                    Respawn();
                else                                // the master is dead
                {
                    ObjectGuid targetGuid = sObjectMgr->GetLinkedRespawnGuid(dbtableHighGuid);
                    if (targetGuid == dbtableHighGuid) // if linking self, never respawn
                        SetRespawnTime(WEEK);
                    else
                    {
                        // else copy time from master and add a little
                        time_t baseRespawnTime = std::max(linkedRespawnTime, now);
                        time_t const offset = urand(5, MINUTE);

                        // linked guid can be a boss, uses std::numeric_limits<time_t>::max to never respawn in that instance
                        // we shall inherit it instead of adding and causing an overflow
                        if (baseRespawnTime <= std::numeric_limits<time_t>::max() - offset)
                            m_respawnTime = baseRespawnTime + offset;
                        else
                            m_respawnTime = std::numeric_limits<time_t>::max();
                    }
                    SaveRespawnTime(); // also save to DB immediately
                }
            }
            break;
        }
        case CORPSE:
        {
            Unit::Update(diff);
            // deathstate changed on spells update, prevent problems
            if (m_deathState != CORPSE)
                break;

            if (IsEngaged())
                Unit::AIUpdateTick(diff);

            if (m_groupLootTimer && lootingGroupLowGUID)
            {
                if (m_groupLootTimer <= diff)
                {
                    Group* group = sGroupMgr->GetGroupByGUID(lootingGroupLowGUID);
                    if (group)
                        group->EndRoll(&loot, GetMap());
                    m_groupLootTimer = 0;
                    lootingGroupLowGUID = 0;
                }
                else m_groupLootTimer -= diff;
            }
            else if (m_corpseRemoveTime <= GameTime::GetGameTime())
            {
                RemoveCorpse(false);
                TC_LOG_DEBUG("entities.unit", "Removing corpse... {} ", GetEntry());
            }
            break;
        }
        case ALIVE:
        {
            Unit::Update(diff);

            // creature can be dead after Unit::Update call
            // CORPSE/DEAD state will processed at next tick (in other case death timer will be updated unexpectedly)
            if (!IsAlive())
                break;

            GetThreatManager().Update(diff);
            if (_spellFocusInfo.Delay)
            {
                if (_spellFocusInfo.Delay <= diff)
                    ReacquireSpellFocusTarget();
                else
                    _spellFocusInfo.Delay -= diff;
            }

            // periodic check to see if the creature has passed an evade boundary
            if (IsAIEnabled() && !IsInEvadeMode() && IsEngaged())
            {
                if (diff >= m_boundaryCheckTime)
                {
                    AI()->CheckInRoom();
                    m_boundaryCheckTime = 2500;
                } else
                    m_boundaryCheckTime -= diff;
            }

            // if periodic combat pulse is enabled and we are both in combat and in a dungeon, do this now
            if (m_combatPulseDelay > 0 && IsEngaged() && GetMap()->IsDungeon())
            {
                if (diff > m_combatPulseTime)
                    m_combatPulseTime = 0;
                else
                    m_combatPulseTime -= diff;

                if (m_combatPulseTime == 0)
                {
                    Map::PlayerList const& players = GetMap()->GetPlayers();
                    if (!players.isEmpty())
                        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
                        {
                            if (Player* player = it->GetSource())
                            {
                                if (player->IsGameMaster())
                                    continue;

                                if (player->IsAlive() && IsHostileTo(player))
                                    EngageWithTarget(player);
                            }
                        }

                    m_combatPulseTime = m_combatPulseDelay * IN_MILLISECONDS;
                }
            }

            Unit::AIUpdateTick(diff);

            // creature can be dead after UpdateAI call
            // CORPSE/DEAD state will processed at next tick (in other case death timer will be updated unexpectedly)
            if (!IsAlive())
                break;

            if (m_regenTimer > 0)
            {
                if (diff >= m_regenTimer)
                    m_regenTimer = 0;
                else
                    m_regenTimer -= diff;
            }

            if (m_regenTimer == 0)
            {
                if (!IsInEvadeMode())
                {
                    // regenerate health if not in combat or if polymorphed)
                    if (!IsEngaged() || IsPolymorphed())
                        RegenerateHealth();
                    else if (CanNotReachTarget())
                    {
                        // regenerate health if cannot reach the target and the setting is set to do so.
                        // this allows to disable the health regen of raid bosses if pathfinding has issues for whatever reason
                        if (sWorld->getBoolConfig(CONFIG_REGEN_HP_CANNOT_REACH_TARGET_IN_RAID) || !GetMap()->IsRaid())
                        {
                            RegenerateHealth();
                            TC_LOG_DEBUG("entities.unit.chase", "RegenerateHealth() enabled because Creature cannot reach the target. Detail: {}", GetDebugInfo());
                        }
                        else
                            TC_LOG_DEBUG("entities.unit.chase", "RegenerateHealth() disabled even if the Creature cannot reach the target. Detail: {}", GetDebugInfo());
                    }
                }

                if (GetPowerType() == POWER_ENERGY)
                    Regenerate(POWER_ENERGY);
                else
                    Regenerate(POWER_MANA);

                m_regenTimer = CREATURE_REGEN_INTERVAL;
            }

            if (CanNotReachTarget() && !IsInEvadeMode() && !GetMap()->IsRaid())
            {
                m_cannotReachTimer += diff;
                if (m_cannotReachTimer >= CREATURE_NOPATH_EVADE_TIME)
                    if (CreatureAI* ai = AI())
                        ai->EnterEvadeMode(CreatureAI::EVADE_REASON_NO_PATH);
            }
            break;
        }
        default:
            break;
    }
}

/// 能量恢复
/// 职责：恢复生物的法力值、集中值或能量值
/// 参数：power - 能量类型（POWER_FOCUS, POWER_ENERGY, POWER_MANA等）
/// 主要流程：
///   1. 检查是否有能量恢复标志
///   2. 如果当前值已达最大值，直接返回
///   3. 根据能量类型计算恢复量：
///      - POWER_FOCUS: 猎人宠物集中值，24*速率
///      - POWER_ENERGY: 死亡骑士食尸鬼能量，固定20
///      - POWER_MANA: 法力值，根据精神计算
///   4. 应用光环修正值
///   5. 增加能量值
/// 调用时机：Update中的恢复计时器到期时
/// 注意：战斗中的法力恢复需要检查5秒规则
void Creature::Regenerate(Powers power)
{
    uint32 curValue = GetPower(power);
    uint32 maxValue = GetMaxPower(power);

    if (!HasUnitFlag2(UNIT_FLAG2_REGENERATE_POWER))
        return;

    if (curValue >= maxValue)
        return;

    float addvalue = 0.0f;

    switch (power)
    {
        case POWER_FOCUS:
        {
            // For hunter pets.
            addvalue = 24 * sWorld->getRate(RATE_POWER_FOCUS);
            break;
        }
        case POWER_ENERGY:
        {
            // For deathknight's ghoul.
            addvalue = 20;
            break;
        }
        case POWER_MANA:
        {
            // Combat and any controlled creature
            if (IsInCombat() || GetCharmerOrOwnerGUID())
            {
                if (!IsUnderLastManaUseEffect())
                {
                    float ManaIncreaseRate = sWorld->getRate(RATE_POWER_MANA);
                    float Spirit = GetStat(STAT_SPIRIT);

                    addvalue = uint32((Spirit / 5.0f + 17.0f) * ManaIncreaseRate);
                }
            }
            else
                addvalue = maxValue / 3;

            break;
        }
        default:
            return;
    }

    // Apply modifiers (if any).
    addvalue *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, power);

    addvalue += GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, power) * (IsHunterPet() ? PET_FOCUS_REGEN_INTERVAL : CREATURE_REGEN_INTERVAL) / (5 * IN_MILLISECONDS);

    ModifyPower(power, int32(addvalue));
}

/// 生命值恢复
/// 职责：恢复生物的生命值
/// 主要流程：
///   1. 检查是否可以恢复生命值
///   2. 如果当前生命值已达最大值，直接返回
///   3. 计算恢复量：
///      - 受控生物（宠物等）：根据精神值计算，有法力值的按0.25倍精神，无法力值按0.80倍精神
///      - 非受控生物：最大生命值的1/3
///   4. 应用光环修正值
///   5. 增加生命值
/// 调用时机：Update中的恢复计时器到期时
/// 注意：变形状态下的生物不恢复生命值
void Creature::RegenerateHealth()
{
    if (!CanRegenerateHealth())
        return;

    uint32 curValue = GetHealth();
    uint32 maxValue = GetMaxHealth();

    if (curValue >= maxValue)
        return;

    uint32 addvalue = 0;

    // Not only pet, but any controlled creature (and not polymorphed)
    if (GetCharmerOrOwnerGUID() && !IsPolymorphed())
    {
        float HealthIncreaseRate = sWorld->getRate(RATE_HEALTH);
        float Spirit = GetStat(STAT_SPIRIT);

        if (GetPower(POWER_MANA) > 0)
            addvalue = uint32(Spirit * 0.25 * HealthIncreaseRate);
        else
            addvalue = uint32(Spirit * 0.80 * HealthIncreaseRate);
    }
    else
        addvalue = maxValue/3;

    // Apply modifiers (if any).
    addvalue *= GetTotalAuraMultiplier(SPELL_AURA_MOD_HEALTH_REGEN_PERCENT);

    addvalue += GetTotalAuraModifier(SPELL_AURA_MOD_REGEN) * CREATURE_REGEN_INTERVAL  / (5 * IN_MILLISECONDS);

    ModifyHealth(addvalue);
}

/// 逃跑寻求援助
/// 职责：当生物生命值低时逃跑并寻找帮助
/// 主要流程：
///   1. 检查是否有受害者
///   2. 检查是否有防止逃跑的光环
///   3. 获取援助半径配置
///   4. 在半径内搜索最近的可以协助的生物
///   5. 设置已搜索援助标志
///   6. 如果没找到协助者，进入恐惧状态
///   7. 如果找到，移动到协助者位置
/// 调用时机：生物生命值低于阈值时（由AI调用）
void Creature::DoFleeToGetAssistance()
{
    if (!GetVictim())
        return;

    if (HasAuraType(SPELL_AURA_PREVENTS_FLEEING))
        return;

    float radius = sWorld->getFloatConfig(CONFIG_CREATURE_FAMILY_FLEE_ASSISTANCE_RADIUS);
    if (radius >0)
    {
        Creature* creature = nullptr;
        Trinity::NearestAssistCreatureInCreatureRangeCheck u_check(this, GetVictim(), radius);
        Trinity::CreatureLastSearcher<Trinity::NearestAssistCreatureInCreatureRangeCheck> searcher(this, creature, u_check);
        Cell::VisitGridObjects(this, searcher, radius);

        SetNoSearchAssistance(true);

        if (!creature)
            /// @todo use 31365
            SetControlled(true, UNIT_STATE_FLEEING);
        else
            GetMotionMaster()->MoveSeekAssistance(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
    }
}

/**
 * @brief 销毁生物的AI实例
 *
 * @return true 总是返回true
 *
 * @brief 职责：移除当前AI并刷新AI状态
 * @details 此函数会弹出当前AI实例，然后刷新AI，
 *          通常用于重新创建AI或切换AI行为
 */
bool Creature::AIM_Destroy()
{
    PopAI();
    RefreshAI();
    return true;
}

/**
 * @brief 创建生物的AI实例
 *
 * @param ai 可选的自定义AI实例，为nullptr时由工厂自动选择
 * @return true 创建成功
 *
 * @brief 职责：初始化移动并创建AI实例
 * @details 首先初始化移动管理器，然后设置AI。
 *          如果没有提供自定义AI，则通过工厂选择器根据生物类型选择合适的AI
 */
bool Creature::AIM_Create(CreatureAI* ai /*= nullptr*/)
{
    Motion_Initialize();

    SetAI(ai ? ai : FactorySelector::SelectAI(this));

    return true;
}

/// 初始化AI
/// 职责：创建并初始化生物的AI实例
/// 参数：ai - 可选的自定义AI实例（默认nullptr由工厂创建）
/// 返回值：初始化成功返回true
/// 主要流程：
///   1. 调用AIM_Create创建AI
///   2. 调用AI的InitializeAI方法
///   3. 如果是载具，重置载具组件
/// 调用时机：生物添加到世界时
/// 注意：必须在生物基础属性设置完成后调用
bool Creature::AIM_Initialize(CreatureAI* ai)
{
    if (!AIM_Create(ai))
        return false;

    AI()->InitializeAI();
    if (GetVehicleKit())
        GetVehicleKit()->Reset();
    return true;
}

/// 初始化移动
/// 职责：设置生物的初始移动状态
/// 主要流程：
///   1. 如果在编队中：
///      a. 如果是队长，重置编队
///      b. 如果编队已形成，设置为空闲等待队长命令
///      c. 返回，不初始化移动
///   2. 否则，初始化移动管理器的默认移动
/// 调用时机：AI初始化或重生时
/// 注意：编队成员需要等待队长的移动命令
void Creature::Motion_Initialize()
{
    if (m_formation)
    {
        if (m_formation->GetLeader() == this)
            m_formation->FormationReset(false);
        else if (m_formation->IsFormed())
        {
            GetMotionMaster()->MoveIdle(); // wait the order of leader
            return;
        }
    }

    GetMotionMaster()->Initialize();
}

/// 创建生物（完整创建流程）
/// 职责：初始化生物的所有基础属性并创建对象
/// 参数：
///   - guidlow: 低GUID值
///   - map: 所在地图
///   - phaseMask: 相位掩码
///   - entry: 生物模板ID
///   - pos: 位置信息
///   - data: 可选的生物数据
///   - vehId: 载具ID（可选）
///   - dynamic: 是否使用动态生成模式
/// 返回值：创建成功返回true，失败返回false
/// 主要流程：
///   1. 设置地图和相位
///   2. 设置生成模式（动态/兼容）
///   3. 获取并验证生物模板
///   4. 重定位到指定位置
///   5. 验证位置有效性
///   6. 获取地形数据
///   7. 设置幽灵可见性
///   8. 调用CreateFromProto创建基础对象
///   9. 设置副本Boss的重生延迟
///   10. 根据生物等级设置尸体腐烂时间
///   11. 调整悬停高度
///   12. 设置脚本ID
///   13. 设置幽灵可见性（灵魂医者/向导）
///   14. 设置路径查找标志
///   15. 设置免疫击退
///   16. 初始化威胁管理器
/// 调用时机：生物首次生成时
/// 注意：必须在地图加载阶段或运行时创建生物时调用
bool Creature::Create(ObjectGuid::LowType guidlow, Map* map, uint32 phaseMask, uint32 entry, Position const& pos, CreatureData const* data /*= nullptr*/, uint32 vehId /*= 0*/, bool dynamic)
{
    ASSERT(map);
    SetMap(map);
    SetPhaseMask(phaseMask, false);

    // Set if this creature can handle dynamic spawns
    if (!dynamic)
        SetRespawnCompatibilityMode();

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        TC_LOG_ERROR("sql.sql", "Creature::Create(): creature template (guidlow: {}, entry: {}) does not exist.", guidlow, entry);
        return false;
    }

    //! Relocate before CreateFromProto, to initialize coords and allow
    //! returning correct zone id for selecting OutdoorPvP/Battlefield script
    Relocate(pos);

    // Check if the position is valid before calling CreateFromProto(), otherwise we might add Auras to Creatures at
    // invalid position, triggering a crash about Auras not removed in the destructor
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.unit", "Creature::Create(): given coordinates for creature (guidlow {}, entry {}) are not valid (X: {}, Y: {}, Z: {}, O: {})", guidlow, entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation());
        return false;
    }
    {
        // area/zone id is needed immediately for ZoneScript::GetCreatureEntry hook before it is known which creature template to load (no model/scale available yet)
        PositionFullTerrainStatus data;
        GetMap()->GetFullTerrainStatusForPosition(GetPhaseMask(), GetPositionX(), GetPositionY(), GetPositionZ(), data, MAP_ALL_LIQUIDS, DEFAULT_COLLISION_HEIGHT);
        ProcessPositionDataChanged(data);
    }

    // Allow players to see those units while dead, do it here (mayby altered by addon auras)
    if (cinfo->type_flags & CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS)
        m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GHOST, GHOST_VISIBILITY_ALIVE | GHOST_VISIBILITY_GHOST);

    if (!CreateFromProto(guidlow, entry, data, vehId))
        return false;

    if (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_DUNGEON_BOSS && map->IsDungeon())
        m_respawnDelay = 0; // special value, prevents respawn for dungeon bosses unless overridden

    switch (GetCreatureTemplate()->rank)
    {
        case CREATURE_ELITE_RARE:
            m_corpseDelay = sWorld->getIntConfig(CONFIG_CORPSE_DECAY_RARE);
            break;
        case CREATURE_ELITE_ELITE:
            m_corpseDelay = sWorld->getIntConfig(CONFIG_CORPSE_DECAY_ELITE);
            break;
        case CREATURE_ELITE_RAREELITE:
            m_corpseDelay = sWorld->getIntConfig(CONFIG_CORPSE_DECAY_RAREELITE);
            break;
        case CREATURE_ELITE_WORLDBOSS:
            m_corpseDelay = sWorld->getIntConfig(CONFIG_CORPSE_DECAY_WORLDBOSS);
            break;
        default:
            m_corpseDelay = sWorld->getIntConfig(CONFIG_CORPSE_DECAY_NORMAL);
            break;
    }

    //! Need to be called after LoadCreaturesAddon - MOVEMENTFLAG_HOVER is set there
    m_positionZ += GetHoverOffset();

    LastUsedScriptID = GetScriptId();

    if (IsSpiritHealer() || IsSpiritGuide() || (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_GHOST_VISIBILITY))
    {
        m_serverSideVisibility.SetValue(SERVERSIDE_VISIBILITY_GHOST, GHOST_VISIBILITY_GHOST);
        m_serverSideVisibilityDetect.SetValue(SERVERSIDE_VISIBILITY_GHOST, GHOST_VISIBILITY_GHOST);
    }

    if (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING)
        AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);

    if (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK)
    {
        ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
        ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, true);
    }

    GetThreatManager().Initialize();

    return true;
}

/// 选择攻击目标（受害者）
/// 职责：为生物选择当前应该攻击的目标
/// 返回值：选中的目标单位指针，如果没有有效目标则返回nullptr
/// 主要流程：
///   1. 如果有威胁列表，从威胁管理器获取当前受害者
///   2. 如果没有威胁列表且不是被动反应：
///      a. 获取攻击者作为目标
///      b. 如果是召唤物，检查主人的攻击者
///      c. 检查主人其他受控单位的攻击者
///   3. 如果是被动反应，返回nullptr
///   4. 验证目标是否可接受且可攻击
///   5. 如果有效，设置面向目标并返回
///   6. 如果在载具上，返回nullptr
///   7. 检查永久隐形光环
///   8. 如果没有有效目标，进入规避模式
/// 调用时机：AI需要确定攻击目标时
/// 注意：此函数可能导致生物进入规避状态
Unit* Creature::SelectVictim()
{
    Unit* target = nullptr;

    if (CanHaveThreatList())
        target = GetThreatManager().GetCurrentVictim();
    else if (!HasReactState(REACT_PASSIVE))
    {
        // We're a player pet, probably
        target = getAttackerForHelper();
        if (!target && IsSummon())
        {
            if (Unit* owner = ToTempSummon()->GetOwner())
            {
                if (owner->IsInCombat())
                    target = owner->getAttackerForHelper();
                if (!target)
                {
                    for (ControlList::const_iterator itr = owner->m_Controlled.begin(); itr != owner->m_Controlled.end(); ++itr)
                    {
                        if ((*itr)->IsInCombat())
                        {
                            target = (*itr)->getAttackerForHelper();
                            if (target)
                                break;
                        }
                    }
                }
            }
        }
    }
    else
        return nullptr;

    if (target && _IsTargetAcceptable(target) && CanCreatureAttack(target))
    {
        if (!HasSpellFocus())
            SetInFront(target);
        return target;
    }

    /// @todo a vehicle may eat some mob, so mob should not evade
    if (GetVehicle())
        return nullptr;

    Unit::AuraEffectList const& iAuras = GetAuraEffectsByType(SPELL_AURA_MOD_INVISIBILITY);
    if (!iAuras.empty())
    {
        for (Unit::AuraEffectList::const_iterator itr = iAuras.begin(); itr != iAuras.end(); ++itr)
        {
            if ((*itr)->GetBase()->IsPermanent())
            {
                AI()->EnterEvadeMode(CreatureAI::EVADE_REASON_OTHER);
                break;
            }
        }
        return nullptr;
    }

    // enter in evade mode in other case
    AI()->EnterEvadeMode(CreatureAI::EVADE_REASON_NO_HOSTILES);

    return nullptr;
}

/// 初始化反应状态
/// 职责：根据生物类型设置默认的反应状态
/// 规则：
///   - 图腾、触发器、小动物、灵魂服务者：设为被动(REACT_PASSIVE)
///   - 其他生物：设为侵略性(REACT_AGGRESSIVE)
/// 调用时机：生物创建或重生时
void Creature::InitializeReactState()
{
    if (IsTotem() || IsTrigger() || IsCritter() || IsSpiritService())
        SetReactState(REACT_PASSIVE);
    /*
    else if (IsCivilian())
        SetReactState(REACT_DEFENSIVE);
    */
    else
        SetReactState(REACT_AGGRESSIVE);
}

/**
 * @brief 检查玩家是否可以与战场军官交互
 *
 * @param player 玩家指针
 * @param msg 是否显示错误消息（如果等级不足）
 * @return true 可以交互
 * @return false 不能交互
 *
 * @details 检查玩家等级是否符合战场要求。如果等级不足且msg为true，
 *          会显示相应的错误消息。不同的战场有不同的等级要求和错误消息
 */
bool Creature::isCanInteractWithBattleMaster(Player* player, bool msg) const
{
    if (!IsBattleMaster())
        return false;

    BattlegroundTypeId bgTypeId = sBattlegroundMgr->GetBattleMasterBG(GetEntry());
    if (!msg)
        return player->GetBGAccessByLevel(bgTypeId);

    if (!player->GetBGAccessByLevel(bgTypeId))
    {
        ClearGossipMenuFor(player);
        switch (bgTypeId)
        {
            case BATTLEGROUND_AV:  SendGossipMenuFor(player, 7616, this); break;
            case BATTLEGROUND_WS:  SendGossipMenuFor(player, 7599, this); break;
            case BATTLEGROUND_AB:  SendGossipMenuFor(player, 7642, this); break;
            case BATTLEGROUND_EY:
            case BATTLEGROUND_NA:
            case BATTLEGROUND_BE:
            case BATTLEGROUND_AA:
            case BATTLEGROUND_RL:
            case BATTLEGROUND_SA:
            case BATTLEGROUND_DS:
            case BATTLEGROUND_RV:  SendGossipMenuFor(player, 10024, this); break;
            default: break;
        }
        return false;
    }
    return true;
}

/**
 * @brief 检查是否可以重置天赋
 *
 * @param player 玩家指针
 * @param pet 是否是宠物天赋重置
 * @return true 可以重置天赋
 * @return false 不能重置天赋
 *
 * @details 验证条件：
 *          1. 该NPC必须是一个训练师
 *          2. 玩家等级必须>=10
 *          3. 训练师类型必须匹配（宠物训练师或职业训练师）
 *          4. 该训练师对玩家有效
 */
bool Creature::CanResetTalents(Player* player, bool pet) const
{
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(GetEntry());
    if (!trainer)
        return false;

    return player->GetLevel() >= 10 &&
        (trainer->GetTrainerType() == (pet ? Trainer::Type::Pet : Trainer::Type::Class)) &&
        trainer->IsTrainerValidForPlayer(player);
}

/**
 * @brief 获取战利品接收者玩家
 *
 * @return Player* 战利品接收者玩家指针，没有则返回nullptr
 *
 * @note 返回对生物造成伤害或标签生物的玩家
 */
Player* Creature::GetLootRecipient() const
{
    if (!m_lootRecipient)
        return nullptr;
    return ObjectAccessor::FindConnectedPlayer(m_lootRecipient);
}

/**
 * @brief 获取战利品接收者团队
 *
 * @return Group* 战利品接收者团队指针，没有则返回nullptr
 *
 * @note 返回有权拾取该生物战利品的团队
 */
Group* Creature::GetLootRecipientGroup() const
{
    if (!m_lootRecipientGroup)
        return nullptr;
    return sGroupMgr->GetGroupByGUID(m_lootRecipientGroup);
}

/// 设置战利品接收者
/// 职责：设置哪个玩家或团队有权拾取该生物的战利品
/// 参数：
///   - unit: 接收者单位
///   - withGroup: 是否包含团队（默认true）
/// 主要流程：
///   1. 如果unit为空，清除接收者和团队，移除拾取标志
///   2. 如果单位不是玩家且不是载具，返回
///   3. 获取玩家（可能是主人）
///   4. 设置接收者GUID
///   5. 如果withGroup，设置团队GUID
///   6. 设置被标签动态标志
/// 调用时机：生物受到伤害或被击杀时
/// 注意：被标签的生物显示为灰色给其他玩家
void Creature::SetLootRecipient(Unit* unit, bool withGroup)
{
    // set the player whose group should receive the right
    // to loot the creature after it dies
    // should be set to nullptr after the loot disappears

    if (!unit)
    {
        m_lootRecipient.Clear();
        m_lootRecipientGroup = 0;
        RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE|UNIT_DYNFLAG_TAPPED);
        return;
    }

    if (unit->GetTypeId() != TYPEID_PLAYER && !unit->IsVehicle())
        return;

    Player* player = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)                                             // normal creature, no player involved
        return;

    m_lootRecipient = player->GetGUID();
    if (withGroup)
    {
        if (Group* group = player->GetGroup())
            m_lootRecipientGroup = group->GetLowGUID();
    }
    else
        m_lootRecipientGroup = ObjectGuid::Empty;

    SetDynamicFlag(UNIT_DYNFLAG_TAPPED);
}

// return true if this creature is tapped by the player or by a member of his group.
/// 检查生物是否被玩家标签
/// 职责：判断生物是否被指定玩家或其团队标签
/// 参数：player - 玩家指针
/// 返回值：已被该玩家或其团队标签返回true，否则返回false
/// 逻辑：
///   1. 如果玩家GUID匹配战利品接收者，返回true
///   2. 如果玩家有团队且团队匹配战利品接收者团队，返回true
/// 用途：用于判断玩家是否有权攻击或拾取该生物
bool Creature::isTappedBy(Player const* player) const
{
    if (player->GetGUID() == m_lootRecipient)
        return true;

    Group const* playerGroup = player->GetGroup();
    if (!playerGroup || playerGroup != GetLootRecipientGroup()) // if we dont have a group we arent the recipient
        return false;                                           // if creature doesnt have group bound it means it was solo killed by someone else

    return true;
}

/// 保存生物到数据库（使用现有数据）
/// 职责：将生物当前状态保存到数据库
/// 主要流程：
///   1. 从对象管理器获取生物数据
///   2. 验证数据存在
///   3. 计算地图ID（考虑运输工具）
///   4. 调用完整SaveToDB函数
/// 调用时机：需要持久化生物状态时
/// 注意：仅适用于已加载的生物
void Creature::SaveToDB()
{
    // this should only be used when the creature has already been loaded
    // preferably after adding to map, because mapid may not be valid otherwise
    CreatureData const* data = sObjectMgr->GetCreatureData(m_spawnId);
    if (!data)
    {
        TC_LOG_ERROR("entities.unit", "Creature::SaveToDB failed, cannot get creature data!");
        return;
    }

    uint32 mapId = GetTransport() ? GetTransport()->GetGOInfo()->moTransport.mapID : GetMapId();
    SaveToDB(mapId, data->spawnMask, GetPhaseMask());
}

/// 保存生物到数据库（指定参数）
/// 职责：将生物数据完整保存到数据库
/// 参数：
///   - mapid: 地图ID
///   - spawnMask: 生成掩码
///   - phaseMask: 相位掩码
/// 主要流程：
///   1. 如果没有spawnId，生成一个新的
///   2. 获取或创建生物数据结构
///   3. 收集当前属性（显示ID、NPC标志、单位标志、动态标志）
///   4. 与模板比较，仅保存差异值
///   5. 设置生物数据字段
///   6. 根据是否在运输工具上设置位置
///   7. 执行数据库事务：先删除旧记录，再插入新记录
/// 调用时机：需要保存生物状态或创建新生物记录时
void Creature::SaveToDB(uint32 mapid, uint8 spawnMask, uint32 phaseMask)
{
    // update in loaded data
    if (!m_spawnId)
        m_spawnId = sObjectMgr->GenerateCreatureSpawnId();

    CreatureData& data = sObjectMgr->NewOrExistCreatureData(m_spawnId);

    uint32 displayId = GetNativeDisplayId();
    uint32 npcflag = GetNpcFlags();
    uint32 unit_flags = GetUnitFlags();
    uint32 dynamicflags = GetDynamicFlags();

    // check if it's a custom model and if not, use 0 for displayId
    CreatureTemplate const* cinfo = GetCreatureTemplate();
    if (cinfo)
    {
        if (displayId == cinfo->Modelid1 || displayId == cinfo->Modelid2 ||
            displayId == cinfo->Modelid3 || displayId == cinfo->Modelid4)
            displayId = 0;

        if (npcflag == cinfo->npcflag)
            npcflag = 0;

        if (unit_flags == cinfo->unit_flags)
            unit_flags = 0;

        if (dynamicflags == cinfo->dynamicflags)
            dynamicflags = 0;
    }

    if (!data.spawnId)
        data.spawnId = m_spawnId;
    ASSERT(data.spawnId == m_spawnId);
    data.id = GetEntry();
    data.phaseMask = phaseMask;
    data.displayid = displayId;
    data.equipmentId = GetCurrentEquipmentId();
    if (!GetTransport())
    {
        data.mapId = GetMapId();
        data.spawnPoint.Relocate(this);
    }
    else
    {
        data.mapId = mapid;
        data.spawnPoint.Relocate(GetTransOffsetX(), GetTransOffsetY(), GetTransOffsetZ(), GetTransOffsetO());
    }
    data.spawntimesecs = m_respawnDelay;
    // prevent add data integrity problems
    data.wander_distance = GetDefaultMovementType() == IDLE_MOTION_TYPE ? 0.0f : m_wanderDistance;
    data.currentwaypoint = 0;
    data.curhealth = GetHealth();
    data.curmana = GetPower(POWER_MANA);
    // prevent add data integrity problems
    data.movementType = !m_wanderDistance && GetDefaultMovementType() == RANDOM_MOTION_TYPE
        ? IDLE_MOTION_TYPE : GetDefaultMovementType();
    data.spawnMask = spawnMask;
    data.npcflag = npcflag;
    data.unit_flags = unit_flags;
    data.dynamicflags = dynamicflags;
    if (!data.spawnGroupData)
        data.spawnGroupData = sObjectMgr->GetDefaultSpawnGroup();

    // update in DB
    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_CREATURE);
    stmt->setUInt32(0, m_spawnId);

    trans->Append(stmt);

    uint8 index = 0;

    stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_CREATURE);
    stmt->setUInt32(index++, m_spawnId);
    stmt->setUInt32(index++, GetEntry());
    stmt->setUInt16(index++, uint16(mapid));
    stmt->setUInt8(index++, spawnMask);
    stmt->setUInt32(index++, GetPhaseMask());
    stmt->setUInt32(index++, displayId);
    stmt->setInt32(index++, int32(GetCurrentEquipmentId()));
    stmt->setFloat(index++, GetPositionX());
    stmt->setFloat(index++, GetPositionY());
    stmt->setFloat(index++, GetPositionZ());
    stmt->setFloat(index++, GetOrientation());
    stmt->setUInt32(index++, m_respawnDelay);
    stmt->setFloat(index++, m_wanderDistance);
    stmt->setUInt32(index++, 0);
    stmt->setUInt32(index++, GetHealth());
    stmt->setUInt32(index++, GetPower(POWER_MANA));
    stmt->setUInt8(index++, uint8(GetDefaultMovementType()));
    stmt->setUInt32(index++, npcflag);
    stmt->setUInt32(index++, unit_flags);
    stmt->setUInt32(index++, dynamicflags);
    trans->Append(stmt);

    WorldDatabase.CommitTransaction(trans);
}

/// 选择生物等级
/// 职责：根据模板的等级范围随机确定生物的等级
/// 主要流程：
///   1. 从模板获取最小和最大等级
///   2. 如果最小等于最大，使用该等级
///   3. 否则在范围内随机选择一个等级
/// 调用时机：生物创建时
void Creature::SelectLevel()
{
    CreatureTemplate const* cInfo = GetCreatureTemplate();

    // level
    uint8 minlevel = std::min(cInfo->maxlevel, cInfo->minlevel);
    uint8 maxlevel = std::max(cInfo->maxlevel, cInfo->minlevel);
    uint8 level = minlevel == maxlevel ? minlevel : urand(minlevel, maxlevel);
    SetLevel(level);
}

/// 更新等级相关属性
/// 职责：根据等级和生物类型计算并设置生命值、法力值、伤害、护甲等属性
/// 主要流程：
///   1. 获取生物基础属性
///   2. 根据等级获取基础属性值
///   3. 计算生命值（基础值 * 阶级修正系数）
///   4. 设置生命值上限和当前值
///   5. 计算法力值
///   6. 根据职业设置法力值上限
///   7. 计算基础伤害
///   8. 设置武器伤害（主手、副手、远程）
///   9. 设置攻击强度
///   10. 计算护甲值
/// 调用时机：等级变化或生物创建时
/// 注意：不适用于守护者（Guardian），它们有独立的属性初始化逻辑
void Creature::UpdateLevelDependantStats()
{
    CreatureTemplate const* cInfo = GetCreatureTemplate();
    uint32 rank = IsPet() ? 0 : cInfo->rank;
    CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(GetLevel(), cInfo->unit_class);

    // health
    float healthmod = _GetHealthMod(rank);

    uint32 basehp = stats->GenerateHealth(cInfo);
    uint32 health = uint32(basehp * healthmod);

    SetCreateHealth(health);
    SetMaxHealth(health);
    SetHealth(health);
    ResetPlayerDamageReq();

    // mana
    uint32 mana = stats->GenerateMana(cInfo);
    SetCreateMana(mana);

    switch (GetClass())
    {
        case UNIT_CLASS_PALADIN:
        case UNIT_CLASS_MAGE:
            SetMaxPower(POWER_MANA, mana);
            SetFullPower(POWER_MANA);
            break;
        default: // We don't set max power here, 0 makes power bar hidden
            break;
    }

    SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, (float)health);

    // damage
    float basedamage = stats->GenerateBaseDamage(cInfo);

    float weaponBaseMinDamage = basedamage;
    float weaponBaseMaxDamage = basedamage * 1.5f;

    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, weaponBaseMinDamage);
    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, weaponBaseMaxDamage);

    SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, weaponBaseMinDamage);
    SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, weaponBaseMaxDamage);

    SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, weaponBaseMinDamage);
    SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, weaponBaseMaxDamage);

    SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, stats->AttackPower);
    SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, stats->RangedAttackPower);

    float armor = (float)stats->GenerateArmor(cInfo); /// @todo Why is this treated as uint32 when it's a float?
    SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, armor);
}

/**
 * @brief 根据生物阶级获取生命值修正系数
 *
 * @param Rank 生物阶级（普通、精英、稀有、世界Boss等）
 * @return float 生命值修正系数
 *
 * @note 不同阶级的生物有不同的生命值倍率，
 *       这些倍率可以通过服务器配置调整
 */
float Creature::_GetHealthMod(int32 Rank)
{
    switch (Rank)                                           // define rates for each elite rank
    {
        case CREATURE_ELITE_NORMAL:
            return sWorld->getRate(RATE_CREATURE_NORMAL_HP);
        case CREATURE_ELITE_ELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
        case CREATURE_ELITE_RAREELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_HP);
        case CREATURE_ELITE_WORLDBOSS:
            return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_HP);
        case CREATURE_ELITE_RARE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RARE_HP);
        default:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
    }
}

/**
 * @brief 降低玩家伤害需求值
 *
 * @param unDamage 降低的伤害值
 *
 * @brief 职责：用于追踪玩家对生物造成的伤害
 * @details 某些生物需要玩家造成一定伤害才能获得战利品/经验，
 *          此函数用于递减这个需求值
 */
void Creature::LowerPlayerDamageReq(uint32 unDamage)
{
    if (m_PlayerDamageReq)
        m_PlayerDamageReq > unDamage ? m_PlayerDamageReq -= unDamage : m_PlayerDamageReq = 0;
}

/**
 * @brief 根据生物阶级获取伤害修正系数
 *
 * @param Rank 生物阶级
 * @return float 伤害修正系数
 *
 * @note 不同阶级的生物有不同的伤害倍率，
 *       世界Boss的倍率最高
 */
float Creature::_GetDamageMod(int32 Rank)
{
    switch (Rank)                                           // define rates for each elite rank
    {
        case CREATURE_ELITE_NORMAL:
            return sWorld->getRate(RATE_CREATURE_NORMAL_DAMAGE);
        case CREATURE_ELITE_ELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_DAMAGE);
        case CREATURE_ELITE_RAREELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_DAMAGE);
        case CREATURE_ELITE_WORLDBOSS:
            return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE);
        case CREATURE_ELITE_RARE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RARE_DAMAGE);
        default:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_DAMAGE);
    }
}

/**
 * @brief 根据生物阶级获取法术伤害修正系数
 *
 * @param Rank 生物阶级
 * @return float 法术伤害修正系数
 *
 * @note 不同阶级的生物有不同的法术伤害倍率，
 *       这影响施法类生物的伤害输出
 */
float Creature::GetSpellDamageMod(int32 Rank) const
{
    switch (Rank)                                           // define rates for each elite rank
    {
        case CREATURE_ELITE_NORMAL:
            return sWorld->getRate(RATE_CREATURE_NORMAL_SPELLDAMAGE);
        case CREATURE_ELITE_ELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE);
        case CREATURE_ELITE_RAREELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE);
        case CREATURE_ELITE_WORLDBOSS:
            return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE);
        case CREATURE_ELITE_RARE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RARE_SPELLDAMAGE);
        default:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE);
    }
}

/// 从模板创建生物
/// 职责：根据模板数据创建生物的基础结构
/// 参数：
///   - guidlow: 低GUID值
///   - entry: 生物模板ID
///   - data: 可选的生物数据
///   - vehId: 载具ID（可选）
/// 返回值：创建成功返回true，模板不存在返回false
/// 主要流程：
///   1. 设置区域脚本
///   2. 如果有区域脚本，可能修改生物条目
///   3. 获取并验证生物模板
///   4. 保存原始条目
///   5. 创建对象（区分载具和普通单位）
///   6. 调用UpdateEntry设置属性
///   7. 如果是载具，创建载具组件
/// 调用时机：Create函数内部调用
bool Creature::CreateFromProto(ObjectGuid::LowType guidlow, uint32 entry, CreatureData const* data /*= nullptr*/, uint32 vehId /*= 0*/)
{
    SetZoneScript();
    if (GetZoneScript() && data)
    {
        entry = GetZoneScript()->GetCreatureEntry(guidlow, data);
        if (!entry)
            return false;
    }

    CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(entry);
    if (!cinfo)
    {
        TC_LOG_ERROR("sql.sql", "Creature::CreateFromProto(): creature template (guidlow: {}, entry: {}) does not exist.", guidlow, entry);
        return false;
    }

    SetOriginalEntry(entry);

    Object::_Create(guidlow, entry, (vehId || cinfo->VehicleId) ? HighGuid::Vehicle : HighGuid::Unit);

    if (!UpdateEntry(entry, data))
        return false;

    if (!vehId)
    {
        if (GetCreatureTemplate()->VehicleId)
        {
            vehId = GetCreatureTemplate()->VehicleId;
            entry = GetCreatureTemplate()->Entry;
        }
        else
            vehId = cinfo->VehicleId;
    }

    if (vehId)
        if (CreateVehicleKit(vehId, entry))
            UpdateDisplayPower();

    return true;
}

/// 从数据库加载生物
/// 职责：从数据库数据加载并初始化生物
/// 参数：
///   - spawnId: 生成ID（数据库主键）
///   - map: 目标地图
///   - addToMap: 是否添加到地图
///   - allowDuplicate: 是否允许重复生成
/// 返回值：加载成功返回true，失败返回false
/// 主要流程：
///   1. 检查是否已存在该spawnId的生物
///   2. 如果存在存活实例，跳过创建
///   3. 如果存在死亡实例，标记为移除
///   4. 从对象管理器获取生物数据
///   5. 设置spawnId和相关数据
///   6. 创建生物对象
///   7. 设置出生位置
///   8. 设置死亡状态为存活
///   9. 获取重生时间
///   10. 检查spawn group是否激活
///   11. 如果有重生时间，处理重生逻辑
///   12. 设置出生生命值
///   13. 设置默认移动类型
///   14. 如果需要，添加到地图
/// 调用时机：地图加载生物时
/// 注意：这是从数据库加载持久化生物的主要入口
bool Creature::LoadFromDB(ObjectGuid::LowType spawnId, Map* map, bool addToMap, bool allowDuplicate)
{
    if (!allowDuplicate)
    {
        // If an alive instance of this spawnId is already found, skip creation
        // If only dead instance(s) exist, despawn them and spawn a new (maybe also dead) version
        const auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
        std::vector <Creature*> despawnList;

        if (creatureBounds.first != creatureBounds.second)
        {
            for (auto itr = creatureBounds.first; itr != creatureBounds.second; ++itr)
            {
                if (itr->second->IsAlive())
                {
                    TC_LOG_DEBUG("maps", "Would have spawned {} but {} already exists", spawnId, creatureBounds.first->second->GetGUID().ToString());
                    return false;
                }
                else
                {
                    despawnList.push_back(itr->second);
                    TC_LOG_DEBUG("maps", "Despawned dead instance of spawn {} ({})", spawnId, itr->second->GetGUID().ToString());
                }
            }

            for (Creature* despawnCreature : despawnList)
            {
                despawnCreature->AddObjectToRemoveList();
            }
        }
    }

    CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);

    if (!data)
    {
        TC_LOG_ERROR("sql.sql", "Creature (SpawnID {}) not found in table `creature`, can't load. ", spawnId);
        return false;
    }

    m_spawnId = spawnId;

    m_respawnCompatibilityMode = ((data->spawnGroupData->flags & SPAWNGROUP_FLAG_COMPATIBILITY_MODE) != 0);
    m_creatureData = data;
    m_wanderDistance = data->wander_distance;
    m_respawnDelay = data->spawntimesecs;

    if (!Create(map->GenerateLowGuid<HighGuid::Unit>(), map, data->phaseMask, data->id, data->spawnPoint, data, 0U , !m_respawnCompatibilityMode))
        return false;

    //We should set first home position, because then AI calls home movement
    SetHomePosition(*this);

    m_deathState = ALIVE;

    m_respawnTime = GetMap()->GetCreatureRespawnTime(m_spawnId);

    if (!m_respawnTime && !map->IsSpawnGroupActive(data->spawnGroupData->groupId))
    {
        if (!m_respawnCompatibilityMode)
        {
            // @todo pools need fixing! this is just a temporary thing, but they violate dynspawn principles
            if (!sPoolMgr->IsPartOfAPool<Creature>(spawnId))
            {
                TC_LOG_ERROR("entities.unit", "Creature (SpawnID {}) trying to load in inactive spawn group '{}':\n{}", spawnId, data->spawnGroupData->name, GetDebugInfo());
                return false;
            }
        }

        m_respawnTime = GameTime::GetGameTime() + urand(4, 7);
    }

    if (m_respawnTime)
    {
        if (!m_respawnCompatibilityMode)
        {
            // @todo same as above
            if (!sPoolMgr->IsPartOfAPool<Creature>(spawnId))
            {
                TC_LOG_ERROR("entities.unit", "Creature (SpawnID {}) trying to load despite a respawn timer in progress:\n{}", spawnId, GetDebugInfo());
                return false;
            }
        }
        else
        {
            // compatibility mode creatures will be respawned in ::Update()
            m_deathState = DEAD;
        }

        if (CanFly())
        {
            float tz = map->GetHeight(GetPhaseMask(), data->spawnPoint, true, MAX_FALL_DISTANCE);
            if (data->spawnPoint.GetPositionZ() - tz > 0.1f && Trinity::IsValidMapCoord(tz))
                Relocate(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY(), tz);
        }
    }

    SetSpawnHealth();

    // checked at creature_template loading
    m_defaultMovementType = MovementGeneratorType(data->movementType);

    if (addToMap && !GetMap()->AddToMap(this))
        return false;
    return true;
}

/**
 * @brief 设置生物是否可以双持武器
 *
 * @param value true可以双持，false不能双持
 *
 * @details 调用Unit的基类方法，然后更新副手伤害
 */
void Creature::SetCanDualWield(bool value)
{
    Unit::SetCanDualWield(value);
    UpdateDamagePhysical(OFF_ATTACK);
}

/// 加载装备
/// 职责：设置生物的虚拟装备显示
/// 参数：
///   - id: 装备ID（0表示无装备）
///   - force: 是否强制设置（默认true）
/// 主要流程：
///   1. 如果id为0且强制，清除所有装备
///   2. 否则从对象管理器获取装备信息
///   3. 设置所有虚拟物品槽位
/// 调用时机：生物创建或装备更新时
void Creature::LoadEquipment(int8 id, bool force /*= true*/)
{
    if (id == 0)
    {
        if (force)
        {
            for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
                SetVirtualItem(i, 0);
            m_equipmentId = 0;
        }

        return;
    }

    EquipmentInfo const* einfo = sObjectMgr->GetEquipmentInfo(GetEntry(), id);
    if (!einfo)
        return;

    m_equipmentId = id;
    for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
        SetVirtualItem(i, einfo->ItemEntry[i]);
}

/// 设置出生生命值
/// 职责：将生物的生命值和法力值设置为出生时的初始值
/// 主要流程：
///   1. 如果生命值恢复被锁定，直接返回
///   2. 如果有生物数据且不恢复生命值：
///      a. 使用数据库中的生命值
///      b. 应用阶级修正系数
///      c. 设置法力值
///   3. 否则使用最大生命值和法力值
///   4. 根据死亡状态设置当前生命值
/// 调用时机：生物创建或重生时
void Creature::SetSpawnHealth()
{
    if (_regenerateHealthLock)
        return;

    uint32 curhealth;
    if (m_creatureData && !_regenerateHealth)
    {
        curhealth = m_creatureData->curhealth;
        if (curhealth)
        {
            curhealth = uint32(curhealth*_GetHealthMod(GetCreatureTemplate()->rank));
            if (curhealth < 1)
                curhealth = 1;
        }
        SetPower(POWER_MANA, m_creatureData->curmana);
    }
    else
    {
        curhealth = GetMaxHealth();
        SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    }

    SetHealth((m_deathState == ALIVE || m_deathState == JUST_RESPAWNED) ? curhealth : 0);
}

/// 加载模板定身状态
/// 职责：根据移动模板设置生物的定身状态
/// 用途：某些生物天生就是定身的（如炮台、陷阱）
void Creature::LoadTemplateRoot()
{
    if (GetMovementTemplate().IsRooted())
        SetControlled(true, UNIT_STATE_ROOT);
}

/// 检查是否有任务
/// 职责：判断该生物是否提供指定任务
/// 参数：quest_id - 任务ID
/// 返回值：提供该任务返回true，否则返回false
bool Creature::hasQuest(uint32 quest_id) const
{
    return sObjectMgr->GetCreatureQuestRelations(GetEntry()).HasQuest(quest_id);
}

/// 检查是否涉及任务
/// 职责：判断该生物是否与指定任务相关（如任务目标）
/// 参数：quest_id - 任务ID
/// 返回值：与该任务相关返回true，否则返回false
bool Creature::hasInvolvedQuest(uint32 quest_id) const
{
    return sObjectMgr->GetCreatureQuestInvolvedRelations(GetEntry()).HasQuest(quest_id);
}

/// 从数据库删除生物（静态函数）
/// 职责：完全删除生物及其所有相关数据
/// 参数：spawnId - 生成ID
/// 返回值：删除成功返回true，生物不存在返回false
/// 主要流程：
///   1. 获取生物数据
///   2. 在所有地图中卸载该生物
///   3. 从内存中删除生物数据
///   4. 从数据库删除所有相关记录：
///      - creature表
///      - spawn_group_member表
///      - creature_addon表
///      - game_event_creature表
///      - game_event_model_equip表
///      - linked_respawn表
/// 调用时机：管理员删除生物或脚本清理时
/*static*/ bool Creature::DeleteFromDB(ObjectGuid::LowType spawnId)
{
    CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
    if (!data)
        return false;

    CharacterDatabaseTransaction charTrans = CharacterDatabase.BeginTransaction();

    sMapMgr->DoForAllMapsWithMapId(data->mapId,
        [spawnId, charTrans](Map* map) -> void
        {
            // despawn all active creatures, and remove their respawns
            std::vector<Creature*> toUnload;
            for (auto const& pair : Trinity::Containers::MapEqualRange(map->GetCreatureBySpawnIdStore(), spawnId))
                toUnload.push_back(pair.second);
            for (Creature* creature : toUnload)
                map->AddObjectToRemoveList(creature);
            map->RemoveRespawnTime(SPAWN_TYPE_CREATURE, spawnId, charTrans);
        }
    );

    // delete data from memory ...
    sObjectMgr->DeleteCreatureData(spawnId);

    CharacterDatabase.CommitTransaction(charTrans);

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();

    // ... and the database
    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_CREATURE);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_SPAWNGROUP_MEMBER);
    stmt->setUInt8(0, uint8(SPAWN_TYPE_CREATURE));
    stmt->setUInt32(1, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_CREATURE_ADDON);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAME_EVENT_CREATURE);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAME_EVENT_MODEL_EQUIP);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_CREATURE_TO_CREATURE);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_CREATURE_TO_GO);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN_MASTER);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_CREATURE_TO_CREATURE);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN_MASTER);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_GO_TO_CREATURE);
    trans->Append(stmt);

    WorldDatabase.CommitTransaction(trans);

    return true;
}

/**
 * @brief 检查生物是否因消失而隐形
 *
 * @return true 生物因消失状态而隐形
 * @return false 生物可见
 *
 * @details 如果生物已死亡、尸体已腐烂，则认为生物因消失而隐形
 */
bool Creature::IsInvisibleDueToDespawn() const
{
    if (Unit::IsInvisibleDueToDespawn())
        return true;

    if (IsAlive() || isDying() || m_corpseRemoveTime > GameTime::GetGameTime())
        return false;

    return true;
}

/**
 * @brief 检查生物是否总能看到某个对象
 *
 * @param obj 要检查的对象
 * @return true 总能看到
 * @return false 正常视野检查
 *
 * @details 某些AI可能有特殊视野规则，此函数委托给AI检查
 */
bool Creature::CanAlwaysSee(WorldObject const* obj) const
{
    if (IsAIEnabled() && AI()->CanSeeAlways(obj))
        return true;

    return false;
}

/// 检查是否可以开始攻击
/// 职责：判断生物是否可以攻击指定目标
/// 参数：
///   - who: 潜在的攻击目标
///   - force: 是否强制攻击检查
/// 返回值：可以攻击返回true，否则返回false
/// 检查条件：
///   1. 不能是平民
///   2. 如果免疫NPC，目标必须有玩家控制标志
///   3. 如果免疫PC，目标不能有玩家控制标志
///   4. 不能攻击非战斗宠物
///   5. 如果不能飞行，高度差不能超过攻击范围
///   6. 目标必须是可接受的
///   7. 必须在攻击距离内
///   8. 必须通过CanCreatureAttack检查
///   9. 不能有灰色生物仇恨（低等级）
///   10. 必须在视线内
/// 调用时机：AI决定是否主动攻击目标时
bool Creature::CanStartAttack(Unit const* who, bool force) const
{
    if (IsCivilian())
        return false;

    // This set of checks is should be done only for creatures
    if ((IsImmuneToNPC() && !who->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED))
        || (IsImmuneToPC() && who->HasUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED)))
        return false;

    // Do not attack non-combat pets
    if (who->GetTypeId() == TYPEID_UNIT && who->GetCreatureType() == CREATURE_TYPE_NON_COMBAT_PET)
        return false;

    if (!CanFly() && (GetDistanceZ(who) > CREATURE_Z_ATTACK_RANGE + m_CombatDistance))
        //|| who->IsControlledByPlayer() && who->IsFlying()))
        // we cannot check flying for other creatures, too much map/vmap calculation
        /// @todo should switch to range attack
        return false;

    if (!force)
    {
        if (!_IsTargetAcceptable(who))
            return false;

        if (IsNeutralToAll() || !IsWithinDistInMap(who, GetAttackDistance(who) + m_CombatDistance))
            return false;
    }

    if (!CanCreatureAttack(who, force))
        return false;

    // No aggro from gray creatures
    if (CheckNoGrayAggroConfig(who->GetLevelForTarget(this), GetLevelForTarget(who)))
        return false;

    return IsWithinLOSInMap(who);
}

/**
 * @brief 检查是否应禁用灰色生物的仇恨
 *
 * @param playerLevel 玩家等级
 * @param creatureLevel 生物等级
 * @return true 应该禁用仇恨（不攻击）
 * @return false 正常仇恨逻辑
 *
 * @details 当生物等级远低于玩家等级时（灰色经验），
 *          根据服务器配置决定是否禁用仇恨。
 *          这允许高等级玩家轻松通过低等级区域
 */
bool Creature::CheckNoGrayAggroConfig(uint32 playerLevel, uint32 creatureLevel) const
{
    if (Trinity::XP::GetColorCode(playerLevel, creatureLevel) != XP_GRAY)
        return false;

    uint32 notAbove = sWorld->getIntConfig(CONFIG_NO_GRAY_AGGRO_ABOVE);
    uint32 notBelow = sWorld->getIntConfig(CONFIG_NO_GRAY_AGGRO_BELOW);
    if (notAbove == 0 && notBelow == 0)
        return false;

    if (playerLevel <= notBelow || (playerLevel >= notAbove && notAbove > 0))
        return true;
    return false;
}

/// 获取攻击距离（仇恨半径）
/// 职责：计算生物对目标的仇恨检测距离
/// 参数：player - 目标单位
/// 返回值：仇恨半径（码）
/// 计算公式：
///   - 基础距离：20码 - 碰撞半径
///   - 根据等级差调整：每级 +-1码
///   - 应用检测范围光环修正
///   - 对于超过资料片最高等级的生物，使用最高等级计算
///   - 限制在5-45码之间
/// 调用时机：判断目标是否进入仇恨范围时
float Creature::GetAttackDistance(Unit const* player) const
{
    float aggroRate = sWorld->getRate(RATE_CREATURE_AGGRO);
    if (aggroRate == 0)
        return 0.0f;

    // WoW Wiki: the minimum radius seems to be 5 yards, while the maximum range is 45 yards
    float maxRadius = (45.0f * sWorld->getRate(RATE_CREATURE_AGGRO));
    float minRadius = (5.0f * sWorld->getRate(RATE_CREATURE_AGGRO));

    uint8 expansionMaxLevel = uint8(GetMaxLevelForExpansion(GetCreatureTemplate()->expansion));
    int32 levelDifference = GetLevel() - player->GetLevel();

    // The aggro radius for creatures with equal level as the player is 20 yards.
    // The combatreach should not get taken into account for the distance so we drop it from the range (see Supremus as expample)
    float baseAggroDistance = 20.0f - GetCombatReach();

    // + - 1 yard for each level difference between player and creature
    float aggroRadius = baseAggroDistance + float(levelDifference);

    // detect range auras
    if (float(GetLevel() + 5) <= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
    {
        aggroRadius += GetTotalAuraModifier(SPELL_AURA_MOD_DETECT_RANGE);
        aggroRadius += player->GetTotalAuraModifier(SPELL_AURA_MOD_DETECTED_RANGE);
    }

    // The aggro range of creatures with higher levels than the total player level for the expansion should get the maxlevel treatment
    // This makes sure that creatures such as bosses wont have a bigger aggro range than the rest of the npc's
    // The following code is used for blizzlike behaivior such as skippable bosses
    if (GetLevel() > expansionMaxLevel)
        aggroRadius = baseAggroDistance + float(expansionMaxLevel - player->GetLevel());

    // Make sure that we wont go over the total range limits
    if (aggroRadius > maxRadius)
        aggroRadius = maxRadius;
    else if (aggroRadius < minRadius)
        aggroRadius = minRadius;

    return (aggroRadius * aggroRate);
}

/// 设置死亡状态
/// 职责：处理生物的死亡状态转换
/// 参数：s - 新的死亡状态
/// 主要流程：
///   - JUST_DIED（刚死亡）：
///     1. 设置尸体移除时间
///     2. 应用动态重生时间缩放
///     3. 设置重生时间
///     4. 保存重生时间到数据库
///     5. 释放法术焦点
///     6. 清除目标
///     7. 移除NPC标志
///     8. 取消坐骑
///     9. 设置为非活跃状态
///     10. 重置协助搜索标志
///     11. 如果是编队领袖，重置编队
///     12. 如果在空中，开始下落
///     13. 转换到尸体状态
///   - JUST_RESPAWNED（刚重生）：
///     1. 设置满生命值或出生生命值
///     2. 清除战利品接收者
///     3. 重置玩家伤害需求
///     4. 清除无法到达目标标志
///     5. 更新移动标志
///     6. 清除可擦除的单位状态
///     7. 恢复NPC标志、单位标志、动态标志
///     8. 恢复近战伤害类型
///     9. 恢复相位
///     10. 初始化移动
///     11. 转换到存活状态
///     12. 加载附加数据
/// 调用时机：生物死亡或重生时
void Creature::setDeathState(DeathState s)
{
    Unit::setDeathState(s);

    if (s == JUST_DIED)
    {
        m_corpseRemoveTime = GameTime::GetGameTime() + m_corpseDelay;

        uint32 respawnDelay = m_respawnDelay;
        if (uint32 scalingMode = sWorld->getIntConfig(CONFIG_RESPAWN_DYNAMICMODE))
            GetMap()->ApplyDynamicModeRespawnScaling(this, m_spawnId, respawnDelay, scalingMode);

        // @todo remove the boss respawn time hack in a dynspawn follow-up once we have creature groups in instances
        if (m_respawnCompatibilityMode)
        {
            if (IsDungeonBoss() && !m_respawnDelay)
                m_respawnTime = std::numeric_limits<time_t>::max(); // never respawn in this instance
            else
                m_respawnTime = GameTime::GetGameTime() + respawnDelay + m_corpseDelay;
        }
        else
        {
            if (IsDungeonBoss() && !m_respawnDelay)
                m_respawnTime = std::numeric_limits<time_t>::max(); // never respawn in this instance
            else
                m_respawnTime = GameTime::GetGameTime() + respawnDelay;
        }

        SaveRespawnTime();

        ReleaseSpellFocus(nullptr, false); // remove spellcast focus
        DoNotReacquireSpellFocusTarget();  // cancel delayed re-target
        SetTarget(ObjectGuid::Empty);      // drop target - dead mobs shouldn't ever target things

        ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);

        SetMountDisplayId(0); // if creature is mounted on a virtual mount, remove it at death

        setActive(false);

        SetNoSearchAssistance(false);

        //Dismiss group if is leader
        if (m_formation && m_formation->GetLeader() == this)
            m_formation->FormationReset(true);

        bool needsFalling = (IsFlying() || IsHovering()) && !IsUnderWater();
        SetHover(false, false);
        SetDisableGravity(false, false);

        if (needsFalling)
            GetMotionMaster()->MoveFall();

        Unit::setDeathState(CORPSE);
    }
    else if (s == JUST_RESPAWNED)
    {
        if (IsPet())
            SetFullHealth();
        else
            SetSpawnHealth();

        SetLootRecipient(nullptr);
        ResetPlayerDamageReq();

        SetCannotReachTarget(false);
        UpdateMovementFlags();

        ClearUnitState(UNIT_STATE_ALL_ERASABLE);

        if (!IsPet())
        {
            CreatureData const* creatureData = GetCreatureData();
            CreatureTemplate const* cinfo = GetCreatureTemplate();

            uint32 npcflag, unit_flags, dynamicflags;
            ObjectMgr::ChooseCreatureFlags(cinfo, &npcflag, &unit_flags, &dynamicflags, creatureData);

            ReplaceAllNpcFlags(NPCFlags(npcflag));
            ReplaceAllUnitFlags(UnitFlags(unit_flags));
            ReplaceAllDynamicFlags(dynamicflags);

            SetMeleeDamageSchool(SpellSchools(cinfo->dmgschool));

            if (creatureData && GetPhaseMask() != creatureData->phaseMask)
                SetPhaseMask(creatureData->phaseMask, false);
        }

        Motion_Initialize();
        Unit::setDeathState(ALIVE);

        if (!IsPet())
            LoadCreaturesAddon();
    }
}

/// 生物重生
/// 职责：处理生物的完整重生流程
/// 参数：force - 是否强制重生（即使还活着也杀死重生）
/// 主要流程：
///   - 强制重生模式：
///     1. 如果存活，设置为刚死亡
///     2. 如果不是尸体状态，设置为尸体
///   - 兼容模式(m_respawnCompatibilityMode)：
///     1. 销毁给附近玩家
///     2. 移除尸体
///     3. 如果是死亡状态：
///        * 清除重生时间
///        * 清除偷窃填充计时器
///        * 清除战利品
///        * 如果条目改变，恢复原始条目
///        * 重新选择等级
///        * 设置为刚重生状态
///        * 随机性别模型
///        * 初始化默认移动
///        * 重置反应状态
///        * 重置AI
///        * 触发JustAppeared
///        * 如果在池中，更新池
///     4. 更新对象可见性
///   - 正常模式：
///     * 调用地图的Respawn函数
/// 调用时机：重生时间到期或强制重生时
void Creature::Respawn(bool force)
{
    if (force)
    {
        if (IsAlive())
            setDeathState(JUST_DIED);
        else if (getDeathState() != CORPSE)
            setDeathState(CORPSE);
    }

    if (m_respawnCompatibilityMode)
    {
        DestroyForNearbyPlayers();
        RemoveCorpse(false, false);

        if (getDeathState() == DEAD)
        {
            TC_LOG_DEBUG("entities.unit", "Respawning creature {} ({})", GetName(), GetGUID().ToString());
            m_respawnTime = 0;
            ResetPickPocketRefillTimer();
            loot.clear();

            if (m_originalEntry != GetEntry())
                UpdateEntry(m_originalEntry);

            SelectLevel();

            setDeathState(JUST_RESPAWNED);

            uint32 displayID = GetNativeDisplayId();
            if (sObjectMgr->GetCreatureModelRandomGender(&displayID))
            {
                SetDisplayId(displayID);
                SetNativeDisplayId(displayID);
            }

            GetMotionMaster()->InitializeDefault();

            // Re-initialize reactstate that could be altered by movementgenerators
            InitializeReactState();

            if (UnitAI* ai = AI()) // reset the AI to be sure no dirty or uninitialized values will be used till next tick
                ai->Reset();

            m_triggerJustAppeared = true;

            uint32 poolid = GetSpawnId() ? sPoolMgr->IsPartOfAPool<Creature>(GetSpawnId()) : 0;
            if (poolid)
                sPoolMgr->UpdatePool<Creature>(poolid, GetSpawnId());
        }
        UpdateObjectVisibility();
    }
    else
    {
        if (m_spawnId)
            GetMap()->Respawn(SPAWN_TYPE_CREATURE, m_spawnId);
    }

    TC_LOG_DEBUG("entities.unit", "Respawning creature {} ({})",
        GetName(), GetGUID().ToString());

}

/// 强制消失
/// 职责：强制生物消失并可选设置重生计时器
/// 参数：
///   - timeMSToDespawn: 延迟消失时间（毫秒），0表示立即消失
///   - forceRespawnTimer: 强制重生时间（秒），0表示使用默认值
/// 主要流程：
///   1. 如果有延迟时间，添加延迟事件
///   2. 兼容模式：
///      a. 销毁给附近玩家
///      b. 如果存活，设置为死亡
///      c. 移除尸体
///      d. 恢复尸体和重生延迟
///   3. 正常模式：
///      a. 如果有强制重生时间，保存
///      b. 否则使用默认重生时间
///      c. 添加到移除列表
/// 调用时机：脚本或系统需要强制移除生物时
void Creature::ForcedDespawn(uint32 timeMSToDespawn, Seconds forceRespawnTimer)
{
    if (timeMSToDespawn)
    {
        m_Events.AddEvent(new ForcedDespawnDelayEvent(*this, forceRespawnTimer), m_Events.CalculateTime(Milliseconds(timeMSToDespawn)));
        return;
    }

    if (m_respawnCompatibilityMode)
    {
        uint32 corpseDelay = GetCorpseDelay();
        uint32 respawnDelay = GetRespawnDelay();

        // do it before killing creature
        DestroyForNearbyPlayers();

        bool overrideRespawnTime = false;
        if (IsAlive())
        {
            if (forceRespawnTimer > Seconds::zero())
            {
                SetCorpseDelay(0);
                SetRespawnDelay(forceRespawnTimer.count());
                overrideRespawnTime = true;
            }

            setDeathState(JUST_DIED);
        }

        // Skip corpse decay time
        RemoveCorpse(!overrideRespawnTime, false);

        SetCorpseDelay(corpseDelay);
        SetRespawnDelay(respawnDelay);
    }
    else
    {
        if (forceRespawnTimer > Seconds::zero())
            SaveRespawnTime(forceRespawnTimer.count());
        else
        {
            uint32 respawnDelay = m_respawnDelay;
            if (uint32 scalingMode = sWorld->getIntConfig(CONFIG_RESPAWN_DYNAMICMODE))
                GetMap()->ApplyDynamicModeRespawnScaling(this, m_spawnId, respawnDelay, scalingMode);
            m_respawnTime = GameTime::GetGameTime() + respawnDelay;
            SaveRespawnTime();
        }

        AddObjectToRemoveList();
    }
}

/// 消失或取消召唤
/// 职责：统一处理生物的消失逻辑（区分临时召唤和普通生物）
/// 参数：
///   - timeToDespawn: 延迟消失时间（毫秒）
///   - forceRespawnTimer: 强制重生时间（秒）
/// 主要流程：
///   1. 如果是临时召唤物，调用UnSummon
///   2. 否则，调用ForcedDespawn
/// 调用时机：需要移除生物时
void Creature::DespawnOrUnsummon(Milliseconds timeToDespawn /*= 0s*/, Seconds forceRespawnTimer /*= 0s*/)
{
    if (TempSummon* summon = ToTempSummon())
        summon->UnSummon(timeToDespawn.count());
    else
        ForcedDespawn(timeToDespawn.count(), forceRespawnTimer);
}

/**
 * @brief 加载模板免疫
 *
 * @brief 职责：从生物模板加载免疫数据并应用到生物
 * @details 主要处理两种免疫：
 *          1. 机制免疫（MechanicImmuneMask）- 如免疫昏迷、恐惧等
 *          2. 法术学派免疫（SpellSchoolImmuneMask）- 如免疫火焰、冰霜等
 *
 * @note 使用占位符法术ID（uint32最大值）来标识模板免疫，
 *       猎人宠物不继承模板免疫
 */
void Creature::LoadTemplateImmunities()
{
    // uint32 max used for "spell id", the immunity system will not perform SpellInfo checks against invalid spells
    // used so we know which immunities were loaded from template
    static uint32 const placeholderSpellId = std::numeric_limits<uint32>::max();

    // unapply template immunities (in case we're updating entry)
    for (uint32 i = MECHANIC_NONE + 1; i < MAX_MECHANIC; ++i)
        ApplySpellImmune(placeholderSpellId, IMMUNITY_MECHANIC, i, false);

    for (uint32 i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        ApplySpellImmune(placeholderSpellId, IMMUNITY_SCHOOL, 1 << i, false);

    // don't inherit immunities for hunter pets
    if (GetOwnerGUID().IsPlayer() && IsHunterPet())
        return;

    if (uint32 mask = GetCreatureTemplate()->MechanicImmuneMask)
    {
        for (uint32 i = MECHANIC_NONE + 1; i < MAX_MECHANIC; ++i)
        {
            if (mask & (1 << (i - 1)))
                ApplySpellImmune(placeholderSpellId, IMMUNITY_MECHANIC, i, true);
        }
    }

    if (uint32 mask = GetCreatureTemplate()->SpellSchoolImmuneMask)
    {
        for (uint8 i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        {
            if (mask & (1 << i))
                ApplySpellImmune(placeholderSpellId, IMMUNITY_SCHOOL, 1 << i, true);
        }
    }
}

/// 检查是否免疫法术
/// 职责：判断生物是否对指定法术完全免疫
/// 参数：
///   - spellInfo: 法术信息
///   - caster: 施法者
///   - requireImmunityPurgesEffectAttribute: 是否需要免疫清除效果属性
/// 返回值：免疫该法术返回true，否则返回false
/// 主要流程：
///   1. 检查所有效果是否都免疫
///   2. 如果所有效果都免疫，返回true
///   3. 否则调用Unit基类的检查
bool Creature::IsImmunedToSpell(SpellInfo const* spellInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute /*= false*/) const
{
    if (!spellInfo)
        return false;

    bool immunedToAllEffects = true;
    for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
    {
        if (spellEffectInfo.IsEffect() && !IsImmunedToSpellEffect(spellInfo, spellEffectInfo, caster, requireImmunityPurgesEffectAttribute))
        {
            immunedToAllEffects = false;
            break;
        }
    }

    if (immunedToAllEffects)
        return true;

    return Unit::IsImmunedToSpell(spellInfo, caster, requireImmunityPurgesEffectAttribute);
}

/**
 * @brief 检查是否免疫法术效果
 *
 * @param spellInfo 法术信息
 * @param spellEffectInfo 法术效果信息
 * @param caster 施法者
 * @param requireImmunityPurgesEffectAttribute 是否需要免疫清除效果属性
 * @return true 免疫该效果
 * @return false 不免疫该效果
 *
 * @details 特殊规则：机械生物免疫治疗效果
 */
bool Creature::IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster,
    bool requireImmunityPurgesEffectAttribute /*= false*/) const
{
    if (GetCreatureTemplate()->type == CREATURE_TYPE_MECHANICAL && spellEffectInfo.IsEffect(SPELL_EFFECT_HEAL))
        return true;

    return Unit::IsImmunedToSpellEffect(spellInfo, spellEffectInfo, caster, requireImmunityPurgesEffectAttribute);
}

/// 检查是否为精英生物
/// 职责：判断生物是否为精英（不包括稀有）
/// 返回值：是精英返回true，否则返回false
/// 注意：宠物总是返回false
bool Creature::isElite() const
{
    if (IsPet())
        return false;

    uint32 rank = GetCreatureTemplate()->rank;
    return rank != CREATURE_ELITE_NORMAL && rank != CREATURE_ELITE_RARE;
}

/// 检查是否为世界Boss
/// 职责：判断生物是否为世界Boss
/// 返回值：是世界Boss返回true，否则返回false
/// 判断依据：检查CREATURE_TYPE_FLAG_BOSS_MOB标志
/// 注意：宠物总是返回false
bool Creature::isWorldBoss() const
{
    if (IsPet())
        return false;

    return (GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_BOSS_MOB) != 0;
}

/// 选择最近的敌对目标
/// 职责：在指定距离内搜索最近的敌对单位
/// 参数：
///   - dist: 搜索距离，0表示最大可见距离
///   - playerOnly: 是否只搜索玩家（默认false）
/// 返回值：最近的敌对单位指针，未找到返回nullptr
/// 调用时机：需要快速找到附近敌人时
Unit* Creature::SelectNearestTarget(float dist, bool playerOnly /* = false */) const
{
    if (dist == 0.0f)
        dist = MAX_VISIBILITY_DISTANCE;

    Unit* target = nullptr;
    Trinity::NearestHostileUnitCheck u_check(this, dist, playerOnly);
    Trinity::UnitLastSearcher<Trinity::NearestHostileUnitCheck> searcher(this, target, u_check);
    Cell::VisitAllObjects(this, searcher, dist);
    return target;
}

/**
 * @brief 在攻击距离内选择最近的敌对目标
 *
 * @param dist 搜索距离，超过ATTACK_DISTANCE时被忽略
 * @return Unit* 最近的敌对单位指针，未找到返回nullptr
 *
 * @details 与SelectNearestTarget不同，此函数在攻击距离内搜索，
 *          忽略威胁列表，只考虑距离
 */
Unit* Creature::SelectNearestTargetInAttackDistance(float dist) const
{
    if (dist > MAX_VISIBILITY_DISTANCE)
    {
        TC_LOG_ERROR("entities.unit", "Creature {} SelectNearestTargetInAttackDistance called with dist > MAX_VISIBILITY_DISTANCE. Distance set to ATTACK_DISTANCE.", GetGUID().ToString());
        dist = ATTACK_DISTANCE;
    }

    Unit* target = nullptr;
    Trinity::NearestHostileUnitInAttackDistanceCheck u_check(this, dist);
    Trinity::UnitLastSearcher<Trinity::NearestHostileUnitInAttackDistanceCheck> searcher(this, target, u_check);
    Cell::VisitAllObjects(this, searcher, std::max(dist, ATTACK_DISTANCE));
    return target;
}

/// 发送AI反应消息
/// 职责：向周围玩家发送AI反应提示
/// 参数：reactionType - AI反应类型
/// 用途：通知客户端AI的特定行为（如进入战斗）
void Creature::SendAIReaction(AiReaction reactionType)
{
    WorldPacket data(SMSG_AI_REACTION, 12);

    data << uint64(GetGUID());
    data << uint32(reactionType);

    ((WorldObject*)this)->SendMessageToSet(&data, true);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_AI_REACTION, type {}.", reactionType);
}

/// 呼叫援助
/// 职责：当生物受到攻击时呼叫附近同阵营生物来协助
/// 主要流程：
///   1. 检查是否已经呼叫过援助
///   2. 检查是否有受害者、不是宠物、不受魅惑
///   3. 设置已呼叫援助标志
///   4. 获取援助半径配置
///   5. 在范围内搜索可以协助的生物
///   6. 创建援助延迟事件
///   7. 将所有协助者加入事件列表
///   8. 根据配置延迟添加事件
/// 调用时机：生物受到攻击时（由AI调用）
/// 注意：使用延迟事件确保不是立即响应
void Creature::CallAssistance()
{
    if (!m_AlreadyCallAssistance && GetVictim() && !IsPet() && !IsCharmed())
    {
        SetNoCallAssistance(true);

        float radius = sWorld->getFloatConfig(CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS);

        if (radius > 0)
        {
            std::list<Creature*> assistList;
            Trinity::AnyAssistCreatureInRangeCheck u_check(this, GetVictim(), radius);
            Trinity::CreatureListSearcher<Trinity::AnyAssistCreatureInRangeCheck> searcher(this, assistList, u_check);
            Cell::VisitGridObjects(this, searcher, radius);

            if (!assistList.empty())
            {
                AssistDelayEvent* e = new AssistDelayEvent(EnsureVictim()->GetGUID(), *this);
                while (!assistList.empty())
                {
                    // Pushing guids because in delay can happen some creature gets despawned => invalid pointer
                    e->AddAssistant((*assistList.begin())->GetGUID());
                    assistList.pop_front();
                }
                m_Events.AddEvent(e, m_Events.CalculateTime(Milliseconds(sWorld->getIntConfig(CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY))));
            }
        }
    }
}

/// 呼救
/// 职责：在指定半径内呼叫同阵营生物帮助战斗
/// 参数：radius - 呼救半径
/// 主要流程：
///   1. 验证半径有效且生物在战斗中、存活、非宠物、非魅惑
///   2. 获取当前威胁目标
///   3. 在半径内搜索可以协助的生物
/// 调用时机：生物生命值低或需要帮助时
/// 注意：与CallAssistance不同，这是寻求帮助而不是响应帮助
void Creature::CallForHelp(float radius)
{
    if (radius <= 0.0f || !IsEngaged() || !IsAlive() || IsPet() || IsCharmed())
        return;

    Unit* target = GetThreatManager().GetCurrentVictim();
    if (!target)
        target = GetThreatManager().GetAnyTarget();
    if (!target)
        target = GetCombatManager().GetAnyTarget();

    if (!target)
    {
        TC_LOG_ERROR("entities.unit", "Creature {} ({}) trying to call for help without being in combat.", GetEntry(), GetName());
        return;
    }

    Trinity::CallOfHelpCreatureInRangeDo u_do(this, target, radius);
    Trinity::CreatureWorker<Trinity::CallOfHelpCreatureInRangeDo> worker(this, u_do);
    Cell::VisitGridObjects(this, worker, radius);
}

/// 检查是否可以协助攻击
/// 职责：判断当前生物是否可以协助另一个单位攻击敌人
/// 参数：
///   - u: 请求协助的单位
///   - enemy: 敌人单位
///   - checkfaction: 是否检查阵营（默认true）
/// 返回值：可以协助返回true，否则返回false
/// 检查条件：
///   1. 必须是侵略性反应状态
///   2. 必须存活
///   3. 不能在规避模式
///   4. 敌人不能在规避模式
///   5. 不能是平民
///   6. 不能是不可攻击或不可交互的
///   7. 不能免疫NPC
///   8. 不能已在战斗中
///   9. 必须是自由生物（无主人）
///   10. 必须是相同阵营或友好
///   11. 必须对敌人敌对
/// 调用时机：响应援助请求时
bool Creature::CanAssistTo(Unit const* u, Unit const* enemy, bool checkfaction /*= true*/) const
{
    // is it true?
    if (!HasReactState(REACT_AGGRESSIVE))
        return false;

    // we don't need help from zombies :)
    if (!IsAlive())
        return false;

    // we cannot assist in evade mode
    if (IsInEvadeMode())
        return false;

    // or if enemy is in evade mode
    if (enemy->GetTypeId() == TYPEID_UNIT && enemy->ToCreature()->IsInEvadeMode())
        return false;

    // we don't need help from non-combatant ;)
    if (IsCivilian())
        return false;

    if (HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE) || IsImmuneToNPC())
        return false;

    // skip fighting creature
    if (IsEngaged())
        return false;

    // only free creature
    if (GetCharmerOrOwnerGUID())
        return false;

    // only from same creature faction
    if (checkfaction)
    {
        if (GetFaction() != u->GetFaction())
            return false;
    }
    else
    {
        if (!IsFriendlyTo(u))
            return false;
    }

    // skip non hostile to caster enemy creatures
    if (!IsHostileTo(enemy))
        return false;

    return true;
}

/// 检查目标是否可接受（内部函数）
/// 职责：验证攻击目标是否符合攻击条件
/// 参数：target - 待验证的目标
/// 返回值：目标可接受返回true，否则返回false
/// 检查条件：
///   1. 不能是友方单位
///   2. 必须可被攻击
///   3. 不能在同一个载具上
///   4. 如果目标已死亡：
///      - 必须能忽略假死且目标是假死状态
///   5. 如果已被该目标攻击或对该目标敌对，可接受
/// 调用时机：选择攻击目标前验证
bool Creature::_IsTargetAcceptable(Unit const* target) const
{
    ASSERT(target);

    // if the target cannot be attacked, the target is not acceptable
    if (IsFriendlyTo(target)
        || !target->isTargetableForAttack(false)
        || (m_vehicle && (IsOnVehicle(target) || m_vehicle->GetBase()->IsOnVehicle(target))))
        return false;

    if (target->HasUnitState(UNIT_STATE_DIED))
    {
        // some creatures can detect fake death
        if (CanIgnoreFeignDeath() && target->HasUnitFlag2(UNIT_FLAG2_FEIGN_DEATH))
            return true;
        else
            return false;
    }

    // if I'm already fighting target, or I'm hostile towards the target, the target is acceptable
    if (IsEngagedBy(target) || IsHostileTo(target))
        return true;

    // if the target's victim is not friendly, or the target is friendly, the target is not acceptable
    return false;
}

/// 保存重生时间
/// 职责：将生物的重生时间保存到数据库和地图
/// 参数：forceDelay - 强制延迟时间（秒），0表示使用当前重生时间
/// 主要流程：
///   1. 如果是召唤物或没有spawnId，返回
///   2. 如果生物数据没有数据库数据，返回
///   3. 兼容模式：创建重生信息并保存到数据库
///   4. 正常模式：保存到地图的重生系统
/// 调用时机：生物死亡或强制消失时
void Creature::SaveRespawnTime(uint32 forceDelay)
{
    if (IsSummon() || !m_spawnId || (m_creatureData && !m_creatureData->dbData))
        return;

    if (m_respawnCompatibilityMode)
    {
        RespawnInfo ri;
        ri.type = SPAWN_TYPE_CREATURE;
        ri.spawnId = m_spawnId;
        ri.respawnTime = m_respawnTime;
        GetMap()->SaveRespawnInfoDB(ri);
        return;
    }

    time_t thisRespawnTime = forceDelay ? GameTime::GetGameTime() + forceDelay : m_respawnTime;
    GetMap()->SaveRespawnTime(SPAWN_TYPE_CREATURE, m_spawnId, GetEntry(), thisRespawnTime, Trinity::ComputeGridCoord(GetHomePosition().GetPositionX(), GetHomePosition().GetPositionY()).GetId());
}

// this should not be called by petAI or
/// 检查生物是否可以攻击目标
/// 职责：验证生物是否可以攻击指定受害者
/// 参数：
///   - victim: 潜在的受害者
///   - force: 是否强制攻击（未使用）
/// 返回值：可以攻击返回true，否则返回false
/// 检查条件：
///   1. 必须在同一地图
///   2. 必须是有效的攻击目标
///   3. 受害者必须在可到达的位置
///   4. AI必须允许攻击
///   5. 不能在规避模式
///   6. 受害者如果是生物，也不能在规避模式
///   7. 如果不是玩家宠物：
///      - 在副本中总是可以
///      - 最近受过伤害或有嘲讽光环可以攻击
///   8. 必须在距离限制内（考虑主人的位置或出生点位置）
/// 调用时机：确定攻击目标时
bool Creature::CanCreatureAttack(Unit const* victim, bool /*force*/) const
{
    if (!victim->IsInMap(this))
        return false;

    if (!IsValidAttackTarget(victim))
        return false;

    if (!victim->isInAccessiblePlaceFor(this))
        return false;

    if (CreatureAI* ai = AI())
        if (!ai->CanAIAttack(victim))
            return false;

    // we cannot attack in evade mode
    if (IsInEvadeMode())
        return false;

    // or if enemy is in evade mode
    if (victim->GetTypeId() == TYPEID_UNIT && victim->ToCreature()->IsInEvadeMode())
        return false;

    if (!GetCharmerOrOwnerGUID().IsPlayer())
    {
        if (GetMap()->IsDungeon())
            return true;

        // don't check distance to home position if recently damaged, this should include taunt auras
        if (!isWorldBoss() && (GetLastDamagedTime() > GameTime::GetGameTime() || HasAuraType(SPELL_AURA_MOD_TAUNT)))
            return true;
    }

    // Map visibility range, but no more than 2*cell size
    float dist = std::min<float>(GetMap()->GetVisibilityRange(), SIZE_OF_GRID_CELL*2);

    if (Unit* unit = GetCharmerOrOwner())
        return victim->IsWithinDist(unit, dist);
    else
    {
        // include sizes for huge npcs
        dist += GetCombatReach() + victim->GetCombatReach();

        // to prevent creatures in air ignore attacks because distance is already too high...
        if (GetMovementTemplate().IsFlightAllowed())
            return victim->IsInDist2d(&m_homePosition, dist);
        else
            return victim->IsInDist(&m_homePosition, dist);
    }
}

/**
 * @brief 获取生物附加数据
 *
 * @return CreatureAddon const* 附加数据指针，没有返回nullptr
 *
 * @details 优先获取特定spawnId的附加数据，否则获取模板附加数据
 */
CreatureAddon const* Creature::GetCreatureAddon() const
{
    if (m_spawnId)
    {
        if (CreatureAddon const* addon = sObjectMgr->GetCreatureAddon(m_spawnId))
            return addon;
    }

    // dependent from difficulty mode entry
    return sObjectMgr->GetCreatureTemplateAddon(GetCreatureTemplate()->Entry);
}

//creature_addon table
/// 加载生物附加数据
/// 职责：从数据库加载生物的附加属性（装备、光环、移动路径等）
/// 返回值：成功加载返回true，无附加数据返回false
/// 主要流程：
///   1. 获取生物附加数据（优先spawnId特定，否则使用模板）
///   2. 如果有坐骑模型，装备坐骑
///   3. 设置站立状态、动画层、视觉标志
///   4. 如果可悬停，添加悬停移动标志
///   5. 设置武器姿态和PVP标志
///   6. 设置表情状态
///   7. 设置可见性距离覆盖
///   8. 设置路径ID（巡逻路径）
///   9. 加载所有附加光环
/// 调用时机：生物创建或重生时
/// 注意：这些数据来自creature_addon表
bool Creature::LoadCreaturesAddon()
{
    CreatureAddon const* creatureAddon = GetCreatureAddon();
    if (!creatureAddon)
        return false;

    if (creatureAddon->mount != 0)
        Mount(creatureAddon->mount);

    // UNIT_FIELD_BYTES_1 values
    SetStandState(UnitStandStateType(creatureAddon->standState));
    SetAnimTier(AnimTier(creatureAddon->animTier));
    ReplaceAllVisFlags(UnitVisFlags(creatureAddon->visFlags));

    //! Suspected correlation between UNIT_FIELD_BYTES_1, offset 3, value 0x2:
    //! If no inhabittype_fly (if no MovementFlag_DisableGravity or MovementFlag_CanFly flag found in sniffs)
    //! Check using InhabitType as movement flags are assigned dynamically
    //! basing on whether the creature is in air or not
    //! Set MovementFlag_Hover. Otherwise do nothing.
    if (CanHover())
        AddUnitMovementFlag(MOVEMENTFLAG_HOVER);

    // UNIT_FIELD_BYTES_2 values
    SetSheath(SheathState(creatureAddon->sheathState));
    ReplaceAllPvpFlags(UnitPVPStateFlags(creatureAddon->pvpFlags));

    // These fields must only be handled by core internals and must not be modified via scripts/DB data
    ReplaceAllPetFlags(UNIT_PET_FLAG_NONE);
    SetShapeshiftForm(FORM_NONE);

    if (creatureAddon->emote != 0)
        SetEmoteState(Emote(creatureAddon->emote));

    // Check if visibility distance different
    if (creatureAddon->visibilityDistanceType != VisibilityDistanceType::Normal)
        SetVisibilityDistanceOverride(creatureAddon->visibilityDistanceType);

    // Load Path
    if (creatureAddon->path_id != 0)
        _waypointPathId = creatureAddon->path_id;

    if (!creatureAddon->auras.empty())
    {
        for (std::vector<uint32>::const_iterator itr = creatureAddon->auras.begin(); itr != creatureAddon->auras.end(); ++itr)
        {
            SpellInfo const* AdditionalSpellInfo = sSpellMgr->GetSpellInfo(*itr);
            if (!AdditionalSpellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Creature {} has wrong spell {} defined in `auras` field.", GetGUID().ToString(), *itr);
                continue;
            }

            // skip already applied aura
            if (HasAura(*itr))
                continue;

            AddAura(*itr, this);
            TC_LOG_DEBUG("entities.unit", "Spell: {} added to creature {}", *itr, GetGUID().ToString());
        }
    }

    return true;
}

/**
 * @brief 发送区域受攻击消息
 *
 * @param attacker 攻击者玩家
 *
 * @brief 职责：向对立阵营的玩家发送区域受攻击警告
 * @details 当玩家攻击特定生物时，会通知对立阵营的玩家
 *          该区域正在受到攻击（用于PvP区域）
 */
void Creature::SendZoneUnderAttackMessage(Player* attacker)
{
    uint32 enemy_team = attacker->GetTeam();

    WorldPacket data(SMSG_ZONE_UNDER_ATTACK, 4);
    data << (uint32)GetAreaId();
    sWorld->SendGlobalMessage(&data, nullptr, (enemy_team == ALLIANCE ? HORDE : ALLIANCE));
}

/**
 * @brief 获取盾牌格挡值
 *
 * @return uint32 格挡值
 *
 * @details 计算公式：等级/2 + 力量/20
 */
uint32 Creature::GetShieldBlockValue() const                  //dunno mob block value
{
    return (GetLevel()/2 + uint32(GetStat(STAT_STRENGTH)/20));
}

/**
 * @brief 检查生物是否拥有指定法术
 *
 * @param spellID 法术ID
 * @return true 拥有该法术
 * @return false 没有该法术
 *
 * @note 检查生物的默认法术列表（m_spells数组）
 */
bool Creature::HasSpell(uint32 spellID) const
{
    return std::find(std::begin(m_spells), std::end(m_spells), spellID) != std::end(m_spells);
}

/**
 * @brief 获取扩展后的重生时间
 *
 * @return time_t 重生时间戳（如果已过期则返回当前时间）
 */
time_t Creature::GetRespawnTimeEx() const
{
    time_t now = GameTime::GetGameTime();
    if (m_respawnTime > now)
        return m_respawnTime;
    else
        return now;
}

/**
 * @brief 设置重生时间
 *
 * @param respawn 重生延迟（秒），0表示立即重生
 */
void Creature::SetRespawnTime(uint32 respawn)
{
    m_respawnTime = respawn ? GameTime::GetGameTime() + respawn : 0;
}

/**
 * @brief 获取重生位置
 *
 * @param x [out] X坐标
 * @param y [out] Y坐标
 * @param z [out] Z坐标
 * @param ori [out] 可选：朝向
 * @param dist [out] 可选：游荡距离
 *
 * @details 如果有生物数据，从数据库spawnPoint获取，
 *          否则使用出生位置
 */
void Creature::GetRespawnPosition(float &x, float &y, float &z, float* ori, float* dist) const
{
    if (m_creatureData)
    {
        if (ori)
            m_creatureData->spawnPoint.GetPosition(x, y, z, *ori);
        else
            m_creatureData->spawnPoint.GetPosition(x, y, z);

        if (dist)
            *dist = m_creatureData->wander_distance;
    }
    else
    {
        Position const& homePos = GetHomePosition();
        if (ori)
            homePos.GetPosition(x, y, z, *ori);
        else
            homePos.GetPosition(x, y, z);
        if (dist)
            *dist = 0;
    }
}

/**
 * @brief 初始化移动标志
 *
 * @details 目前等同于UpdateMovementFlags
 */
void Creature::InitializeMovementFlags()
{
    // It does the same, for now
    UpdateMovementFlags();
}

/// 更新移动标志
/// 职责：根据当前位置和环境更新生物的移动标志
/// 主要流程：
///   1. 如果被玩家控制（魅惑/载具），不更新
///   2. 如果有NO_MOVE_FLAGS_UPDATE标志，不更新
///   3. 获取地面高度
///   4. 判断是否在空中（考虑悬停高度）
///   5. 如果允许飞行且在空中：
///      a. 根据飞行类型设置CanFly或DisableGravity
///      b. 如果没有悬停光环，取消悬停
///   6. 否则：
///      a. 取消飞行和重力禁用
///      b. 如果存活且可悬停，设置悬停
///   7. 如果不在空中，移除下落标志
///   8. 如果在水中且可游泳，设置游泳标志
/// 调用时机：每次Update调用时
void Creature::UpdateMovementFlags()
{
    // Do not update movement flags if creature is controlled by a player (charm/vehicle)
    if (IsMovedByClient())
        return;

    // Creatures with CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE should control MovementFlags in your own scripts
    if (GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE)
        return;

    // Set the movement flags if the creature is in that mode. (Only fly if actually in air, only swim if in water, etc)
    float ground = GetFloorZ();

    bool canHover = CanHover();
    bool isInAir = (G3D::fuzzyGt(GetPositionZ(), ground + (canHover ? GetFloatValue(UNIT_FIELD_HOVERHEIGHT) : 0.0f) + GROUND_HEIGHT_TOLERANCE) || G3D::fuzzyLt(GetPositionZ(), ground - GROUND_HEIGHT_TOLERANCE)); // Can be underground too, prevent the falling

    if (GetMovementTemplate().IsFlightAllowed() && isInAir && !IsFalling())
    {
        if (GetMovementTemplate().Flight == CreatureFlightMovementType::CanFly)
            SetCanFly(true);
        else
            SetDisableGravity(true);

        if (!HasAuraType(SPELL_AURA_HOVER))
            SetHover(false);
    }
    else
    {
        SetCanFly(false);
        SetDisableGravity(false);
        if (IsAlive() && (CanHover() || HasAuraType(SPELL_AURA_HOVER)))
            SetHover(true);
    }

    if (!isInAir)
        RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING);

    SetSwim(CanSwim() && IsInWater());
}

/**
 * @brief 获取移动模板
 *
 * @return CreatureMovementData const& 移动数据引用
 *
 * @details 优先返回特定spawnId的移动覆盖，否则返回模板移动数据
 */
CreatureMovementData const& Creature::GetMovementTemplate() const
{
    if (CreatureMovementData const* movementOverride = sObjectMgr->GetCreatureMovementOverride(m_spawnId))
        return *movementOverride;

    return GetCreatureTemplate()->Movement;
}

/**
 * @brief 检查生物是否可以游泳
 *
 * @return true 可以游泳
 * @return false 不能游泳
 *
 * @note 宠物总是可以游泳
 */
bool Creature::CanSwim() const
{
    if (Unit::CanSwim())
        return true;

    if (IsPet())
        return true;

    return false;
}

/**
 * @brief 检查生物是否可以进入水中
 *
 * @return true 可以进入水中
 * @return false 不能进入水中
 */
bool Creature::CanEnterWater() const
{
    if (CanSwim())
        return true;

    return GetMovementTemplate().IsSwimAllowed();
}

/**
 * @brief 刷新可游泳标志
 *
 * @param recheck 是否重新检查（默认false）
 *
 * @details 如果生物可以进入水中但缺少游泳标志，会自动添加
 */
void Creature::RefreshCanSwimFlag(bool recheck)
{
    if (!_isMissingCanSwimFlagOutOfCombat || recheck)
        _isMissingCanSwimFlagOutOfCombat = !HasUnitFlag(UNIT_FLAG_CAN_SWIM);

    // Check if the creature has UNIT_FLAG_CAN_SWIM and add it if it's missing
    // Creatures must be able to chase a target in water if they can enter water
    if (_isMissingCanSwimFlagOutOfCombat && CanEnterWater())
        SetUnitFlag(UNIT_FLAG_CAN_SWIM);
}

/**
 * @brief 当所有战利品从尸体移除时调用
 *
 * @details 如果生物可剥皮，设置可剥皮标志；
 *          根据服务器配置调整尸体腐烂时间
 */
void Creature::AllLootRemovedFromCorpse()
{
    if (loot.loot_type != LOOT_SKINNING && !IsPet() && GetCreatureTemplate()->SkinLootId && hasLootRecipient())
        if (LootTemplates_Skinning.HaveLootFor(GetCreatureTemplate()->SkinLootId))
            SetUnitFlag(UNIT_FLAG_SKINNABLE);

    time_t now = GameTime::GetGameTime();
    // Do not reset corpse remove time if corpse is already removed
    if (m_corpseRemoveTime <= now)
        return;

    // Scripts can choose to ignore RATE_CORPSE_DECAY_LOOTED by calling SetCorpseDelay(timer, true)
    float decayRate = m_ignoreCorpseDecayRatio ? 1.f : sWorld->getRate(RATE_CORPSE_DECAY_LOOTED);

    // corpse skinnable, but without skinning flag, and then skinned, corpse will despawn next update
    if (loot.loot_type == LOOT_SKINNING)
        m_corpseRemoveTime = now;
    else
        m_corpseRemoveTime = now + uint32(m_corpseDelay * decayRate);

    m_respawnTime = std::max<time_t>(m_corpseRemoveTime + m_respawnDelay, m_respawnTime);
}

/**
 * @brief 获取对目标的等级
 *
 * @param target 目标对象
 * @return uint8 对目标的等级
 *
 * @details 世界Boss的等级会根据目标等级动态调整
 */
uint8 Creature::GetLevelForTarget(WorldObject const* target) const
{
    if (!isWorldBoss() || !target->ToUnit())
        return Unit::GetLevelForTarget(target);

    uint16 level = target->ToUnit()->GetLevel() + sWorld->getIntConfig(CONFIG_WORLD_BOSS_LEVEL_DIFF);
    if (level < 1)
        return 1;
    if (level > 255)
        return 255;
    return uint8(level);
}

/**
 * @brief 获取AI名称
 *
 * @return std::string const& AI名称引用
 */
std::string const& Creature::GetAIName() const
{
    return sObjectMgr->GetCreatureTemplate(GetEntry())->AIName;
}

/**
 * @brief 获取脚本名称
 *
 * @return std::string 脚本名称
 */
std::string Creature::GetScriptName() const
{
    return sObjectMgr->GetScriptName(GetScriptId());
}

/**
 * @brief 获取脚本ID
 *
 * @return uint32 脚本ID
 *
 * @details 优先使用creature_data中的scriptId，否则使用模板的ScriptID
 */
uint32 Creature::GetScriptId() const
{
    if (CreatureData const* creatureData = GetCreatureData())
        if (uint32 scriptId = creatureData->scriptId)
            return scriptId;

    return ASSERT_NOTNULL(sObjectMgr->GetCreatureTemplate(GetEntry()))->ScriptID;
}

/**
 * @brief 获取商贩物品列表
 *
 * @return VendorItemData const* 商贩物品数据指针，没有返回nullptr
 */
VendorItemData const* Creature::GetVendorItems() const
{
    return sObjectMgr->GetNpcVendorItemList(GetEntry());
}

/**
 * @brief 获取商贩物品当前数量
 *
 * @param vItem 商贩物品信息
 * @return uint32 当前可用数量
 *
 * @details 处理有限数量物品，如果超时会自动补充
 */
uint32 Creature::GetVendorItemCurrentCount(VendorItem const* vItem)
{
    if (!vItem->maxcount)
        return vItem->maxcount;

    VendorItemCounts::iterator itr = m_vendorItemCounts.begin();
    for (; itr != m_vendorItemCounts.end(); ++itr)
        if (itr->itemId == vItem->item)
            break;

    if (itr == m_vendorItemCounts.end())
        return vItem->maxcount;

    VendorItemCount* vCount = &*itr;

    time_t ptime = GameTime::GetGameTime();

    if (time_t(vCount->lastIncrementTime + vItem->incrtime) <= ptime)
        if (ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(vItem->item))
        {
            uint32 diff = uint32((ptime - vCount->lastIncrementTime)/vItem->incrtime);
            if ((vCount->count + diff * pProto->BuyCount) >= vItem->maxcount)
            {
                m_vendorItemCounts.erase(itr);
                return vItem->maxcount;
            }

            vCount->count += diff * pProto->BuyCount;
            vCount->lastIncrementTime = ptime;
        }

    return vCount->count;
}

/**
 * @brief 更新商贩物品当前数量
 *
 * @param vItem 商贩物品信息
 * @param used_count 已使用数量
 * @return uint32 更新后的数量
 *
 * @details 当玩家购买物品后减少数量，并开始补充计时
 */
uint32 Creature::UpdateVendorItemCurrentCount(VendorItem const* vItem, uint32 used_count)
{
    if (!vItem->maxcount)
        return 0;

    VendorItemCounts::iterator itr = m_vendorItemCounts.begin();
    for (; itr != m_vendorItemCounts.end(); ++itr)
        if (itr->itemId == vItem->item)
            break;

    if (itr == m_vendorItemCounts.end())
    {
        uint32 new_count = vItem->maxcount > used_count ? vItem->maxcount-used_count : 0;
        m_vendorItemCounts.push_back(VendorItemCount(vItem->item, new_count));
        return new_count;
    }

    VendorItemCount* vCount = &*itr;

    time_t ptime = GameTime::GetGameTime();

    if (time_t(vCount->lastIncrementTime + vItem->incrtime) <= ptime)
        if (ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(vItem->item))
        {
            uint32 diff = uint32((ptime - vCount->lastIncrementTime)/vItem->incrtime);
            if ((vCount->count + diff * pProto->BuyCount) < vItem->maxcount)
                vCount->count += diff * pProto->BuyCount;
            else
                vCount->count = vItem->maxcount;
        }

    vCount->count = vCount->count > used_count ? vCount->count-used_count : 0;
    vCount->lastIncrementTime = ptime;
    return vCount->count;
}

/**
 * @brief 获取本地化的名称
 *
 * @param loc_idx 语言索引
 * @return std::string const& 本地化名称引用
 *
 * @details 如果有对应语言的本地化名称则返回，否则返回默认名称
 */
std::string const & Creature::GetNameForLocaleIdx(LocaleConstant loc_idx) const
{
    if (loc_idx != DEFAULT_LOCALE)
    {
        uint8 uloc_idx = uint8(loc_idx);
        CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(GetEntry());
        if (cl)
        {
            if (cl->Name.size() > uloc_idx && !cl->Name[uloc_idx].empty())
                return cl->Name[uloc_idx];
        }
    }

    return GetName();
}

/**
 * @brief 获取宠物指定位置的自动施放法术
 *
 * @param pos 位置索引
 * @return uint32 法术ID，无效位置或未启用返回0
 */
uint32 Creature::GetPetAutoSpellOnPos(uint8 pos) const
{
    if (pos >= MAX_SPELL_CHARM || !m_charmInfo || m_charmInfo->GetCharmSpell(pos)->GetType() != ACT_ENABLED)
        return 0;
    else
        return m_charmInfo->GetCharmSpell(pos)->GetAction();
}

/**
 * @brief 获取宠物追逐距离
 *
 * @return float 追逐距离（码）
 *
 * @details 根据宠物自动施放法术的最大射程计算
 */
float Creature::GetPetChaseDistance() const
{
    float range = 0.f;

    for (uint8 i = 0; i < GetPetAutoSpellSize(); ++i)
    {
        uint32 spellID = GetPetAutoSpellOnPos(i);
        if (!spellID)
            continue;

        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID))
        {
            if (spellInfo->GetRecoveryTime() == 0 && spellInfo->RangeEntry->ID != 1 /*Self*/ && spellInfo->RangeEntry->ID != 2 /*Combat Range*/ && spellInfo->GetMaxRange() > range)
                range = spellInfo->GetMaxRange();
        }
    }

    return range;
}

/**
 * @brief 设置是否无法到达目标
 *
 * @param cannotReach true表示无法到达目标
 *
 * @details 当生物无法到达攻击目标时设置此标志，
 *          可能会触发生命值恢复或进入规避模式
 */
void Creature::SetCannotReachTarget(bool cannotReach)
{
    if (cannotReach == m_cannotReachTarget)
        return;
    m_cannotReachTarget = cannotReach;
    m_cannotReachTimer = 0;

    if (cannotReach)
        TC_LOG_DEBUG("entities.unit.chase", "Creature::SetCannotReachTarget() called with true. Details: {}", GetDebugInfo());
}

/**
 * @brief 设置行走模式
 *
 * @param enable true为行走，false为奔跑
 * @return true 设置成功
 * @return false 设置失败（状态未改变）
 *
 * @details 向客户端发送移动模式变更数据包
 */
bool Creature::SetWalk(bool enable)
{
    if (!Unit::SetWalk(enable))
        return false;

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_WALK_MODE : SMSG_SPLINE_MOVE_SET_RUN_MODE, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, false);
    return true;
}

/**
 * @brief 设置禁用重力
 *
 * @param disable true禁用重力（飞行），false启用重力
 * @param packetOnly 是否仅发送数据包（默认false）
 * @param updateAnimTier 是否更新动画层级（默认true）
 * @return true 设置成功
 * @return false 设置失败
 *
 * @details 禁用重力允许生物在空中停留，会自动更新动画层级
 */
bool Creature::SetDisableGravity(bool disable, bool packetOnly /*=false*/, bool updateAnimTier /*= true*/)
{
    //! It's possible only a packet is sent but moveflags are not updated
    //! Need more research on this
    if (!packetOnly && !Unit::SetDisableGravity(disable, packetOnly, updateAnimTier))
        return false;

    if (updateAnimTier && IsAlive() && !HasUnitState(UNIT_STATE_ROOT) && !GetMovementTemplate().IsRooted())
    {
        if (IsGravityDisabled())
            SetAnimTier(AnimTier::Fly);
        else if (IsHovering())
            SetAnimTier(AnimTier::Hover);
        else
            SetAnimTier(AnimTier::Ground);
    }

    if (!movespline->Initialized())
        return true;

    WorldPacket data(disable ? SMSG_SPLINE_MOVE_GRAVITY_DISABLE : SMSG_SPLINE_MOVE_GRAVITY_ENABLE, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, false);
    return true;
}

/**
 * @brief 设置游泳模式
 *
 * @param enable true开始游泳，false停止游泳
 * @return true 设置成功
 * @return false 设置失败
 */
bool Creature::SetSwim(bool enable)
{
    if (!Unit::SetSwim(enable))
        return false;

    if (!movespline->Initialized())
        return true;

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_START_SWIM : SMSG_SPLINE_MOVE_STOP_SWIM);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
    return true;
}

/**
 * @brief 设置是否可以飞行
 *
 * @param enable true可以飞行，false不能飞行
 * @param packetOnly 未使用的参数
 * @return true 设置成功
 * @return false 设置失败
 *
 * @details 与SetDisableGravity不同，CanFly是真正的飞行能力
 */
bool Creature::SetCanFly(bool enable, bool /*packetOnly = false */)
{
    if (!Unit::SetCanFly(enable))
        return false;

    if (!movespline->Initialized())
        return true;

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_FLYING : SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, false);
    return true;
}

/**
 * @brief 设置水上行走
 *
 * @param enable true水上行走，false正常落水
 * @param packetOnly 是否仅发送数据包（默认false）
 * @return true 设置成功
 * @return false 设置失败
 */
bool Creature::SetWaterWalking(bool enable, bool packetOnly /* = false */)
{
    if (!packetOnly && !Unit::SetWaterWalking(enable))
        return false;

    if (!movespline->Initialized())
        return true;

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_WATER_WALK : SMSG_SPLINE_MOVE_LAND_WALK);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
    return true;
}

/**
 * @brief 设置缓落
 *
 * @param enable true缓落，false正常下落
 * @param packetOnly 是否仅发送数据包（默认false）
 * @return true 设置成功
 * @return false 设置失败
 *
 * @details 缓落允许生物缓慢下降，避免摔落伤害
 */
bool Creature::SetFeatherFall(bool enable, bool packetOnly /* = false */)
{
    if (!packetOnly && !Unit::SetFeatherFall(enable))
        return false;

    if (!movespline->Initialized())
        return true;

    WorldPacket data(enable ? SMSG_SPLINE_MOVE_FEATHER_FALL : SMSG_SPLINE_MOVE_NORMAL_FALL);
    data << GetPackGUID();
    SendMessageToSet(&data, true);
    return true;
}

/**
 * @brief 设置悬停
 *
 * @param enable true悬停，false取消悬停
 * @param packetOnly 是否仅发送数据包（默认false）
 * @param updateAnimTier 是否更新动画层级（默认true）
 * @return true 设置成功
 * @return false 设置失败
 *
 * @details 悬停的生物在空中静止，会更新动画层级
 */
bool Creature::SetHover(bool enable, bool packetOnly /*= false*/, bool updateAnimTier /*= true*/)
{
    if (!packetOnly && !Unit::SetHover(enable, packetOnly, updateAnimTier))
        return false;

    if (updateAnimTier && IsAlive() && !HasUnitState(UNIT_STATE_ROOT) && !GetMovementTemplate().IsRooted())
    {
        if (IsGravityDisabled())
            SetAnimTier(AnimTier::Fly);
        else if (IsHovering())
            SetAnimTier(AnimTier::Hover);
        else
            SetAnimTier(AnimTier::Ground);
    }

    if (!movespline->Initialized())
        return true;

    //! Not always a packet is sent
    WorldPacket data(enable ? SMSG_SPLINE_MOVE_SET_HOVER : SMSG_SPLINE_MOVE_UNSET_HOVER, 9);
    data << GetPackGUID();
    SendMessageToSet(&data, false);
    return true;
}

/**
 * @brief 获取仇恨范围
 *
 * @param target 目标单位
 * @return float 仇恨范围（码）
 *
 * @details 用于宠物选择目标，基础仇恨半径为20码，
 *          根据等级差调整（每级1码），考虑检测范围光环
 */
float Creature::GetAggroRange(Unit const* target) const
{
    // Determines the aggro range for creatures (usually pets), used mainly for aggressive pet target selection.
    // Based on data from wowwiki due to lack of 3.3.5a data

    if (target && IsPet())
    {
        uint32 targetLevel = 0;

        if (target->GetTypeId() == TYPEID_PLAYER)
            targetLevel = target->GetLevelForTarget(this);
        else if (target->GetTypeId() == TYPEID_UNIT)
            targetLevel = target->ToCreature()->GetLevelForTarget(this);

        uint32 myLevel = GetLevelForTarget(target);
        int32 levelDiff = int32(targetLevel) - int32(myLevel);

        // The maximum Aggro Radius is capped at 45 yards (25 level difference)
        if (levelDiff < -25)
            levelDiff = -25;

        // The base aggro radius for mob of same level
        float aggroRadius = 20;

        // Aggro Radius varies with level difference at a rate of roughly 1 yard/level
        aggroRadius -= (float)levelDiff;

        // detect range auras
        aggroRadius += GetTotalAuraModifier(SPELL_AURA_MOD_DETECT_RANGE);

        // detected range auras
        aggroRadius += target->GetTotalAuraModifier(SPELL_AURA_MOD_DETECTED_RANGE);

        // Just in case, we don't want pets running all over the map
        if (aggroRadius > MAX_AGGRO_RADIUS)
            aggroRadius = MAX_AGGRO_RADIUS;

        // Minimum Aggro Radius for a mob seems to be combat range (5 yards)
        //  hunter pets seem to ignore minimum aggro radius so we'll default it a little higher
        if (aggroRadius < 10)
            aggroRadius = 10;

        return (aggroRadius);
    }

    // Default
    return 0.0f;
}

/**
 * @brief 在仇恨范围内选择最近的敌对单位
 *
 * @param useLOS 是否使用视线检查（默认true）
 * @param ignoreCivilians 是否忽略平民（默认false）
 * @return Unit* 最近的敌对单位，未找到返回nullptr
 *
 * @details 主要用于设置为侵略性的宠物，
 *          不会返回中立或友好目标
 */
Unit* Creature::SelectNearestHostileUnitInAggroRange(bool useLOS, bool ignoreCivilians) const
{
    // Selects nearest hostile target within creature's aggro range. Used primarily by
    //  pets set to aggressive. Will not return neutral or friendly targets.

    Unit* target = nullptr;

    Trinity::NearestHostileUnitInAggroRangeCheck u_check(this, useLOS, ignoreCivilians);
    Trinity::UnitSearcher<Trinity::NearestHostileUnitInAggroRangeCheck> searcher(this, target, u_check);

    Cell::VisitGridObjects(this, searcher, MAX_AGGRO_RADIUS);

    return target;
}

float Creature::GetNativeObjectScale() const
{
    return GetCreatureTemplate()->scale;
}

void Creature::SetObjectScale(float scale)
{
    Unit::SetObjectScale(scale);

    if (CreatureModelInfo const* minfo = sObjectMgr->GetCreatureModelInfo(GetDisplayId()))
    {
        SetBoundingRadius((IsPet() ? 1.0f : minfo->bounding_radius) * scale);
        SetCombatReach((IsPet() ? DEFAULT_PLAYER_COMBAT_REACH : minfo->combat_reach) * scale);
    }
}

void Creature::SetDisplayId(uint32 modelId)
{
    Unit::SetDisplayId(modelId);

    if (CreatureModelInfo const* minfo = sObjectMgr->GetCreatureModelInfo(modelId))
    {
        SetBoundingRadius((IsPet() ? 1.0f : minfo->bounding_radius) * GetObjectScale());
        SetCombatReach((IsPet() ? DEFAULT_PLAYER_COMBAT_REACH : minfo->combat_reach) * GetObjectScale());
    }
}

void Creature::SetTarget(ObjectGuid guid)
{
    if (HasSpellFocus())
        _spellFocusInfo.Target = guid;
    else
        SetGuidValue(UNIT_FIELD_TARGET, guid);
}

/// 设置法术焦点
/// 职责：在施法期间锁定目标面向，确保施法方向正确
/// 参数：
///   - focusSpell: 施放的法术
///   - target: 焦点目标
/// 主要流程：
///   1. 验证参数和状态（已有焦点、死亡、假死、昏迷）
///   2. 检查法术是否禁用焦点
///   3. 不对载具法术使用焦点
///   4. 瞬发非引导法术不需要焦点更新
///   5. 保存施法前的目标和朝向
///   6. 设置法术焦点
///   7. 根据法术属性设置目标面向
///   8. 如果不允许施法期间转身，设置聚焦状态
/// 调用时机：开始施法时
void Creature::SetSpellFocus(Spell const* focusSpell, WorldObject const* target)
{
    // Pointer validation and checking for a already existing focus
    if (_spellFocusInfo.Spell || !focusSpell)
        return;

    // Prevent dead / feign death creatures from setting a focus target
    if (!IsAlive() || HasUnitFlag2(UNIT_FLAG2_FEIGN_DEATH) || HasAuraType(SPELL_AURA_FEIGN_DEATH))
        return;

    // Don't allow stunned creatures to set a focus target
    if (HasUnitFlag(UNIT_FLAG_STUNNED))
        return;

    // some spells shouldn't track targets
    if (focusSpell->IsFocusDisabled())
        return;

    SpellInfo const* spellInfo = focusSpell->GetSpellInfo();

    // don't use spell focus for vehicle spells
    if (spellInfo->HasAura(SPELL_AURA_CONTROL_VEHICLE))
        return;

    // instant non-channeled casts and non-target spells don't need facing updates
    if (!target && (!focusSpell->GetCastTime() && !spellInfo->IsChanneled()))
        return;

    // store pre-cast values for target and orientation (used to later restore)
    if (!_spellFocusInfo.Delay)
    { // only overwrite these fields if we aren't transitioning from one spell focus to another
        _spellFocusInfo.Target = GetGuidValue(UNIT_FIELD_TARGET);
        _spellFocusInfo.Orientation = GetOrientation();
    }
    else // don't automatically reacquire target for the previous spellcast
        _spellFocusInfo.Delay = 0;

    _spellFocusInfo.Spell = focusSpell;

    bool const noTurnDuringCast = spellInfo->HasAttribute(SPELL_ATTR5_DONT_TURN_DURING_CAST);
    bool const turnDisabled = HasUnitFlag2(UNIT_FLAG2_CANNOT_TURN);
    // set target, then force send update packet to players if it changed to provide appropriate facing
    ObjectGuid newTarget = (target && !noTurnDuringCast && !turnDisabled) ? target->GetGUID() : ObjectGuid::Empty;
    if (GetGuidValue(UNIT_FIELD_TARGET) != newTarget)
        SetGuidValue(UNIT_FIELD_TARGET, newTarget);

    // If we are not allowed to turn during cast but have a focus target, face the target
    if (!turnDisabled && noTurnDuringCast && target)
        SetFacingToObject(target, false);

    if (noTurnDuringCast)
        AddUnitState(UNIT_STATE_FOCUSING);
}

bool Creature::HasSpellFocus(Spell const* focusSpell) const
{
    if (isDead()) // dead creatures cannot focus
    {
        if (_spellFocusInfo.Spell || _spellFocusInfo.Delay)
        {
            TC_LOG_WARN("entities.unit", "Creature '{}' (entry {}) has spell focus (spell id {}, delay {}ms) despite being dead.",
                        GetName(), GetEntry(), _spellFocusInfo.Spell ? _spellFocusInfo.Spell->GetSpellInfo()->Id : 0, _spellFocusInfo.Delay);
        }
        return false;
    }

    if (focusSpell)
        return (focusSpell == _spellFocusInfo.Spell);
    else
        return (_spellFocusInfo.Spell || _spellFocusInfo.Delay);
}

/// 释放法术焦点
/// 职责：施法结束后释放焦点目标，恢复原有朝向
/// 参数：
///   - focusSpell: 要释放的法术焦点
///   - withDelay: 是否延迟恢复（默认用于视觉效果）
/// 主要流程：
///   1. 验证是否有法术焦点
///   2. 如果指定的法术不是当前焦点，返回
///   3. 清除聚焦状态
///   4. 如果是宠物，立即恢复目标
///   5. 否则设置延迟恢复（防止视觉bug）
///   6. 清除法术焦点
/// 调用时机：施法结束或被打断时
void Creature::ReleaseSpellFocus(Spell const* focusSpell, bool withDelay)
{
    if (!_spellFocusInfo.Spell)
        return;

    // focused to something else
    if (focusSpell && focusSpell != _spellFocusInfo.Spell)
        return;

    if (_spellFocusInfo.Spell->GetSpellInfo()->HasAttribute(SPELL_ATTR5_DONT_TURN_DURING_CAST))
        ClearUnitState(UNIT_STATE_FOCUSING);

    if (IsPet()) // player pets do not use delay system
    {
        if (!HasUnitFlag2(UNIT_FLAG2_CANNOT_TURN))
            ReacquireSpellFocusTarget();
    }
    else // don't allow re-target right away to prevent visual bugs
        _spellFocusInfo.Delay = withDelay ? 1000 : 1;

    _spellFocusInfo.Spell = nullptr;
}

void Creature::ReacquireSpellFocusTarget()
{
    if (!HasSpellFocus())
    {
        TC_LOG_ERROR("entities.unit", "Creature::ReacquireSpellFocusTarget() being called with HasSpellFocus() returning false. {}", GetDebugInfo());
        return;
    }

    SetGuidValue(UNIT_FIELD_TARGET, _spellFocusInfo.Target);

    if (!HasUnitFlag2(UNIT_FLAG2_CANNOT_TURN))
    {
        if (_spellFocusInfo.Target)
        {
            if (WorldObject const* objTarget = ObjectAccessor::GetWorldObject(*this, _spellFocusInfo.Target))
                SetFacingToObject(objTarget, false);
        }
        else
            SetFacingTo(_spellFocusInfo.Orientation, false);
    }
    _spellFocusInfo.Delay = 0;
}

void Creature::DoNotReacquireSpellFocusTarget()
{
    _spellFocusInfo.Delay = 0;
    _spellFocusInfo.Spell = nullptr;
}

bool Creature::IsMovementPreventedByCasting() const
{
    if (!Unit::IsMovementPreventedByCasting() && !HasSpellFocus())
        return false;

    return true;
}

void Creature::StartPickPocketRefillTimer()
{
    _pickpocketLootRestore = GameTime::GetGameTime() + sWorld->getIntConfig(CONFIG_CREATURE_PICKPOCKET_REFILL);
}

bool Creature::CanGeneratePickPocketLoot() const
{
    return _pickpocketLootRestore <= GameTime::GetGameTime();
}

void Creature::SetTextRepeatId(uint8 textGroup, uint8 id)
{
    CreatureTextRepeatIds& repeats = m_textRepeat[textGroup];
    if (std::find(repeats.begin(), repeats.end(), id) == repeats.end())
        repeats.push_back(id);
    else
        TC_LOG_ERROR("sql.sql", "CreatureTextMgr: TextGroup {} for Creature({}) {}, id {} already added", uint32(textGroup), GetName(), GetGUID().ToString(), uint32(id));
}

CreatureTextRepeatIds Creature::GetTextRepeatGroup(uint8 textGroup)
{
    CreatureTextRepeatIds ids;

    CreatureTextRepeatGroup::const_iterator groupItr = m_textRepeat.find(textGroup);
    if (groupItr != m_textRepeat.end())
        ids = groupItr->second;

    return ids;
}

void Creature::ClearTextRepeatGroup(uint8 textGroup)
{
    CreatureTextRepeatGroup::iterator groupItr = m_textRepeat.find(textGroup);
    if (groupItr != m_textRepeat.end())
        groupItr->second.clear();
}

bool Creature::CanGiveExperience() const
{
    return !IsCritter()
        && !IsPet()
        && !IsTotem()
        && !(GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_NO_XP);
}

bool Creature::IsEngaged() const
{
    if (CreatureAI const* ai = AI())
        return ai->IsEngaged();
    return false;
}

/// 进入战斗时调用
/// 职责：处理生物进入战斗时的初始化逻辑
/// 参数：target - 战斗目标
/// 主要流程：
///   1. 调用Unit基类的AtEngage
///   2. 如果不允许骑乘战斗，下马
///   3. 刷新游泳标志
///   4. 如果是宠物或守护者，更新速度
///   5. 如果在巡逻或护送中，更新出生点位置
///   6. 如果是载具，更新所有乘客的出生点
///   7. 通知AI进入战斗
///   8. 通知编队成员
/// 调用时机：生物首次进入战斗时
void Creature::AtEngage(Unit* target)
{
    Unit::AtEngage(target);

    if (!(GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_ALLOW_MOUNTED_COMBAT))
        Dismount();

    RefreshCanSwimFlag();

    if (IsPet() || IsGuardian()) // update pets' speed for catchup OOC speed
    {
        UpdateSpeed(MOVE_RUN);
        UpdateSpeed(MOVE_SWIM);
        UpdateSpeed(MOVE_FLIGHT);
    }

    MovementGeneratorType const movetype = GetMotionMaster()->GetCurrentMovementGeneratorType();
    if (movetype == WAYPOINT_MOTION_TYPE || movetype == POINT_MOTION_TYPE || (IsAIEnabled() && AI()->IsEscorted()))
    {
        SetHomePosition(GetPosition());

        // if its a vehicle, set the home positon of every creature passenger at engage
        // so that they are in combat range if hostile
        if (Vehicle* vehicle = GetVehicleKit())
        {
            for (auto seat = vehicle->Seats.begin(); seat != vehicle->Seats.end(); ++seat)
                if (Unit* passenger = ObjectAccessor::GetUnit(*this, seat->second.Passenger.Guid))
                    if (Creature* creature = passenger->ToCreature())
                        creature->SetHomePosition(GetPosition());
        }
    }

    if (CreatureAI* ai = AI())
        ai->JustEngagedWith(target);
    if (CreatureGroup* formation = GetFormation())
        formation->MemberEngagingTarget(this, target);
}

/// 脱离战斗时调用
/// 职责：处理生物脱离战斗时的清理逻辑
/// 主要流程：
///   1. 调用Unit基类的AtDisengage
///   2. 清除攻击玩家状态
///   3. 如果存活且被标签，恢复动态标志
///   4. 如果是宠物或守护者，更新速度
/// 调用时机：生物完全脱离战斗时
void Creature::AtDisengage()
{
    Unit::AtDisengage();

    ClearUnitState(UNIT_STATE_ATTACK_PLAYER);
    if (IsAlive() && HasDynamicFlag(UNIT_DYNFLAG_TAPPED))
        ReplaceAllDynamicFlags(GetCreatureTemplate()->dynamicflags);

    if (IsPet() || IsGuardian()) // update pets' speed for catchup OOC speed
    {
        UpdateSpeed(MOVE_RUN);
        UpdateSpeed(MOVE_SWIM);
        UpdateSpeed(MOVE_FLIGHT);
    }
}

bool Creature::IsEscorted() const
{
    if (CreatureAI const* ai = AI())
        return ai->IsEscorted();
    return false;
}

std::string Creature::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Unit::GetDebugInfo() << "\n"
        << "AIName: " << GetAIName() << " ScriptName: " << GetScriptName()
        << " WaypointPath: " << GetWaypointPath() << " SpawnId: " << GetSpawnId();
    return sstr.str();
}

void Creature::ExitVehicle(Position const* /*exitPosition*/)
{
    bool const isInVehicle = GetVehicle();
    Unit::ExitVehicle();

    // if alive creature exits a vehicle, set it's home position to the
    // exited position so it won't run away (home) and evade if it's hostile
    if (isInVehicle && IsAlive())
        SetHomePosition(GetPosition());
}
