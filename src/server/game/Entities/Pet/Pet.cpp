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
 * @file Pet.cpp
 * @brief 宠物实体类实现文件
 *
 * 本文件实现了 Pet 类的所有方法,涵盖宠物系统的核心功能。
 *
 * 主要实现模块:
 *
 * 1. 宠物生命周期管理
 *    - 构造与析构:初始化宠物基础属性
 *    - AddToWorld/RemoveFromWorld:宠物加入/离开游戏世界
 *    - Create/CreateBaseAt*:创建宠物实体
 *    - LoadPetFromDB:从数据库加载宠物
 *    - SavePetToDB:保存宠物到数据库
 *    - Remove/DeleteFromDB:移除和删除宠物
 *
 * 2. 宠物属性更新
 *    - Update:主更新循环,处理快乐度衰减、持续时间、焦点恢复等
 *    - GivePetXP/GivePetLevel:经验值和等级管理
 *    - InitStatsForLevel:初始化等级属性
 *    - SynchronizeLevelWithOwner:与主人等级同步
 *
 * 3. 宠物技能系统
 *    - addSpell/learnSpell/removeSpell:技能学习与遗忘
 *    - InitLevelupSpellsForLevel:升级技能初始化
 *    - InitPetCreateSpells:创建时技能初始化
 *    - ToggleAutocast:自动施放切换
 *    - _LoadSpells/_SaveSpells:技能数据持久化
 *
 * 4. 宠物光环系统
 *    - CastPetAuras/CastPetAura:施放宠物光环
 *    - IsPetAura:判断是否为宠物光环
 *    - _LoadAuras/_SaveAuras:光环数据持久化
 *
 * 5. 宠物天赋系统
 *    - resetTalents:重置天赋
 *    - InitTalentForLevel:初始化等级天赋
 *    - GetMaxTalentPointsForLevel:获取等级天赋点数
 *
 * 6. 快乐度系统(猎人宠物专属)
 *    - LoseHappiness:快乐度衰减
 *    - GetHappinessState:获取快乐度状态
 *    - HaveInDiet:检查食物是否在食谱中
 *    - GetCurrentFoodBenefitLevel:获取食物受益等级
 *
 * 性能优化:
 * - 使用异步数据库查询(PetLoadQueryHolder)加载宠物数据
 * - 减少不必要的数据库更新,使用状态标志追踪变化
 * - 快乐度更新采用定时器,避免频繁计算
 *
 * 数据一致性:
 * - 使用事务保证数据完整性
 * - 加载时设置 m_loading 标志防止并发问题
 * - 移除时设置 m_removed 标志防止覆盖数据库
 */

#include "Pet.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PetPackets.h"
#include "Player.h"
#include "QueryHolder.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "TalentPackets.h"
#include "Unit.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ZoneScript.h"

// 宠物经验因子，宠物获得经验为玩家经验的5%
#define PET_XP_FACTOR 0.05f

/**
 * @brief Pet类构造函数
 *
 * 职责：初始化宠物对象的基础属性和状态
 *
 * 参数：
 *   owner - 宠物的主人（玩家对象）
 *   type  - 宠物类型（HUNTER_PET猎人宠物、SUMMON_PET召唤宠物等）
 *
 * 主要流程：
 *   1. 调用父类Guardian构造函数
 *   2. 设置宠物类型掩码
 *   3. 初始化魅力信息（CharmInfo）用于控制宠物
 *   4. 设置默认名称和焦点恢复计时器
 */
Pet::Pet(Player* owner, PetType type) :
    Guardian(nullptr, owner, true), m_usedTalentCount(0), m_removed(false),
    m_happinessTimer(7500), m_petType(type), m_duration(0), m_auraRaidUpdateMask(0), m_loading(false)
{
    ASSERT(GetOwner());

    // 设置宠物单位类型掩码
    m_unitTypeMask |= UNIT_MASK_PET;
    // 如果是猎人宠物，额外设置猎人宠物掩码
    if (type == HUNTER_PET)
        m_unitTypeMask |= UNIT_MASK_HUNTER_PET;

    // 初始化可控制的守护者相关数据
    if (!(m_unitTypeMask & UNIT_MASK_CONTROLABLE_GUARDIAN))
    {
        m_unitTypeMask |= UNIT_MASK_CONTROLABLE_GUARDIAN;
        InitCharmInfo();  // 初始化魅力控制信息
    }

    m_name = "Pet";  // 默认宠物名称
    m_focusRegenTimer = PET_FOCUS_REGEN_INTERVAL;  // 焦点恢复计时器
}

/**
 * @brief Pet类析构函数
 *
 * 职责：清理宠物对象资源
 */
Pet::~Pet() = default;

/**
 * @brief 将宠物添加到游戏世界
 *
 * 职责：注册宠物到世界对象存储，初始化AI，并处理传送后的状态重置
 *
 * 调用时机：当宠物被召唤或传送时
 *
 * 主要流程：
 *   1. 如果宠物不在世界中，将其注册到地图的对象存储
 *   2. 调用父类AddToWorld方法
 *   3. 初始化宠物AI
 *   4. 通知区域脚本/副本脚本宠物创建
 *   5. 如果宠物处于跟随状态，重置所有控制标志防止卡住
 */
void Pet::AddToWorld()
{
    ///- 如果宠物不在世界中，进行注册
    if (!IsInWorld())
    {
        ///- 将宠物注册到地图的对象存储中以便GUID查找
        GetMap()->GetObjectsStore().Insert<Pet>(GetGUID(), this);
        Unit::AddToWorld();  // 调用父类方法
        AIM_Initialize();    // 初始化AI

        // 通知区域脚本或副本脚本
        if (ZoneScript* zoneScript = GetZoneScript() ? GetZoneScript() : GetInstanceScript())
            zoneScript->OnCreatureCreate(this);
    }

    // 防止传送时宠物卡住。宠物被添加到世界时默认为"跟随"状态
    // 所以我们重置标志让AI来处理
    if (GetCharmInfo() && GetCharmInfo()->HasCommandState(COMMAND_FOLLOW))
    {
        GetCharmInfo()->SetIsCommandAttack(false);
        GetCharmInfo()->SetIsCommandFollow(false);
        GetCharmInfo()->SetIsAtStay(false);
        GetCharmInfo()->SetIsFollowing(false);
        GetCharmInfo()->SetIsReturning(false);
    }
}

/**
 * @brief 从游戏世界中移除宠物
 *
 * 职责：从世界对象存储中注销宠物
 *
 * 调用时机：当宠物被解散、死亡或玩家下线时
 *
 * 主要流程：
 *   1. 检查宠物是否在世界中
 *   2. 调用父类RemoveFromWorld
 *   3. 从地图对象存储中移除宠物
 */
void Pet::RemoveFromWorld()
{
    ///- 从访问器中移除宠物
    if (IsInWorld())
    {
        ///- 不调用Creature的函数，普通怪物和图腾存储在不同的位置
        Unit::RemoveFromWorld();
        GetMap()->GetObjectsStore().Remove<Pet>(GetGUID());
    }
}

/**
 * @brief 获取待加载宠物的信息
 *
 * 职责：根据宠物编号、条目ID或当前标志查找宠物存储信息
 *
 * 参数：
 *   stable   - 宠物存储数据结构
 *   petEntry - 宠物条目ID（可选）
 *   petnumber - 宠物编号（可选）
 *   current  - 是否查找当前宠物
 *
 * 返回值：宠物信息指针和保存模式的配对
 *
 * 主要流程：
 *   1. 如果指定了宠物编号，按编号查找
 *   2. 如果指定了current标志，返回当前宠物
 *   3. 如果指定了宠物条目，按条目查找
 *   4. 否则返回任意当前或非稳定宠物
 */
std::pair<PetStable::PetInfo const*, PetSaveMode> Pet::GetLoadPetInfo(PetStable const& stable, uint32 petEntry, uint32 petnumber, bool current)
{
    if (petnumber)
    {
        // 已知宠物编号，按编号查找
        if (stable.CurrentPet && stable.CurrentPet->PetNumber == petnumber)
            return { &stable.CurrentPet.value(), PET_SAVE_AS_CURRENT };

        // 在兽栏槽位中查找
        for (std::size_t stableSlot = 0; stableSlot < stable.StabledPets.size(); ++stableSlot)
            if (stable.StabledPets[stableSlot] && stable.StabledPets[stableSlot]->PetNumber == petnumber)
                return { &stable.StabledPets[stableSlot].value(), PetSaveMode(PET_SAVE_FIRST_STABLE_SLOT + stableSlot) };

        // 在非槽位宠物中查找
        for (PetStable::PetInfo const& pet : stable.UnslottedPets)
            if (pet.PetNumber == petnumber)
                return { &pet, PET_SAVE_NOT_IN_SLOT };
    }
    else if (current)
    {
        // 当前宠物（槽位0）
        if (stable.CurrentPet)
            return { &stable.CurrentPet.value(), PET_SAVE_AS_CURRENT };
    }
    else if (petEntry)
    {
        // 已知宠物条目ID（召唤宠物唯一，猎人宠物不唯一，只在当前或非稳定宠物中查找）
        if (stable.CurrentPet && stable.CurrentPet->CreatureId == petEntry)
            return { &stable.CurrentPet.value(), PET_SAVE_AS_CURRENT };

        for (PetStable::PetInfo const& pet : stable.UnslottedPets)
            if (pet.CreatureId == petEntry)
                return { &pet, PET_SAVE_NOT_IN_SLOT };
    }
    else
    {
        // 任意当前或其他非稳定宠物（用于猎人"召唤宠物"）
        if (stable.CurrentPet)
            return { &stable.CurrentPet.value(), PET_SAVE_AS_CURRENT };

        if (!stable.UnslottedPets.empty())
            return { &stable.UnslottedPets.front(), PET_SAVE_NOT_IN_SLOT };
    }

    return { nullptr, PET_SAVE_AS_DELETED };
}

/**
 * @brief 宠物加载查询持有器
 *
 * 职责：封装宠物加载所需的数据库查询语句
 *
 * 主要流程：
 *   1. 准备查询宠物名称变格（俄语等语言的名称变形）
 *   2. 准备查询宠物光环
 *   3. 准备查询宠物技能
 *   4. 准备查询宠物技能冷却
 */
class PetLoadQueryHolder : public CharacterDatabaseQueryHolder
{
public:
    enum
    {
        DECLINED_NAMES,  // 名称变格数据
        AURAS,           // 光环数据
        SPELLS,          // 技能数据
        COOLDOWNS,       // 冷却数据

        MAX
    };

    PetLoadQueryHolder(ObjectGuid::LowType ownerGuid, uint32 petNumber)
    {
        SetSize(MAX);

        CharacterDatabasePreparedStatement* stmt;

        // 准备查询名称变格
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_DECLINED_NAME);
        stmt->setUInt32(0, ownerGuid);
        stmt->setUInt32(1, petNumber);
        SetPreparedQuery(DECLINED_NAMES, stmt);

        // 准备查询光环
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_AURA);
        stmt->setUInt32(0, petNumber);
        SetPreparedQuery(AURAS, stmt);

        // 准备查询技能
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_SPELL);
        stmt->setUInt32(0, petNumber);
        SetPreparedQuery(SPELLS, stmt);

        // 准备查询冷却
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PET_SPELL_COOLDOWN);
        stmt->setUInt32(0, petNumber);
        SetPreparedQuery(COOLDOWNS, stmt);
    }
};

/**
 * @brief 从数据库加载宠物
 *
 * 职责：从数据库加载宠物数据并创建宠物实体
 *
 * 参数：
 *   owner     - 宠物的主人（玩家对象）
 *   petEntry  - 宠物条目ID（可选，用于召唤宠物）
 *   petnumber - 宠物编号（可选，用于指定加载的宠物）
 *   current   - 是否加载当前宠物
 *
 * 返回值：加载成功返回true，否则返回false
 *
 * 调用时机：玩家召唤宠物、登录时恢复宠物
 *
 * 主要流程：
 *   1. 获取宠物存储信息
 *   2. 验证宠物是否可被加载
 *   3. 创建宠物对象并设置基础属性
 *   4. 根据宠物类型设置职业、阵营等
 *   5. 加载宠物的位置、等级、经验值
 *   6. 更新宠物存储槽位状态
 *   7. 异步加载光环、技能、冷却数据
 */
bool Pet::LoadPetFromDB(Player* owner, uint32 petEntry, uint32 petnumber, bool current)
{
    m_loading = true;  // 设置加载标志

    PetStable* petStable = ASSERT_NOTNULL(owner->GetPetStable());

    ObjectGuid::LowType ownerid = owner->GetGUID().GetCounter();
    std::pair<PetStable::PetInfo const*, PetSaveMode> info = GetLoadPetInfo(*petStable, petEntry, petnumber, current);
    PetStable::PetInfo const* petInfo = info.first;
    PetSaveMode slot = info.second;
    if (!petInfo)
    {
        m_loading = false;
        return false;
    }

    // 不要尝试重新加载当前宠物
    if (petStable->CurrentPet && owner->GetPet() && petStable->CurrentPet.value().PetNumber == petInfo->PetNumber)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(petInfo->CreatedBySpellId);

    bool isTemporarySummon = spellInfo && spellInfo->GetDuration() > 0;
    if (current && isTemporarySummon)
        return false;

    // 验证猎人宠物是否可被驯服
    if (petInfo->Type == HUNTER_PET)
    {
        CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate(petInfo->CreatureId);
        if (!creatureInfo || !creatureInfo->IsTameable(owner->CanTameExoticPets()))
            return false;
    }

    // 如果需要临时解除召唤，记录宠物编号
    if (current && owner->IsPetNeedBeTemporaryUnsummoned())
    {
        owner->SetTemporaryUnsummonedPetNumber(petInfo->PetNumber);
        return false;
    }

    Map* map = owner->GetMap();
    ObjectGuid::LowType guid = map->GenerateLowGuid<HighGuid::Pet>();

    // 创建宠物对象
    if (!Create(guid, map, owner->GetPhaseMask(), petInfo->CreatureId, petInfo->PetNumber))
        return false;

    // 设置宠物类型和属性
    setPetType(petInfo->Type);
    SetFaction(owner->GetFaction());
    SetCreatedBySpell(petInfo->CreatedBySpellId);

    // 处理小动物类型的宠物
    if (IsCritter())
    {
        float px, py, pz;
        owner->GetClosePoint(px, py, pz, GetCombatReach(), PET_FOLLOW_DIST, GetFollowAngle());
        Relocate(px, py, pz, owner->GetOrientation());

        if (!IsPositionValid())
        {
            TC_LOG_ERROR("entities.pet", "Pet{} not loaded. Suggested coordinates isn't valid (X: {} Y: {})",
                GetGUID().ToString(), GetPositionX(), GetPositionY());
            return false;
        }

        map->AddToMap(ToCreature());
        return true;
    }

    // 设置宠物编号
    m_charmInfo->SetPetNumber(petInfo->PetNumber, IsPermanentPetFor(owner));

    // 设置显示ID和基础属性
    SetDisplayId(petInfo->DisplayId);
    SetNativeDisplayId(petInfo->DisplayId);
    uint8 petlevel = petInfo->Level;
    ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
    SetName(petInfo->Name);

    // 根据宠物类型设置不同的属性
    switch (getPetType())
    {
        case SUMMON_PET:  // 召唤宠物
            petlevel = owner->GetLevel();  // 召唤宠物等级与主人相同
            SetClass(CLASS_MAGE);
            ReplaceAllUnitFlags(UNIT_FLAG_PLAYER_CONTROLLED); // 启用弹出窗口（宠物解散、取消）
            break;
        case HUNTER_PET:  // 猎人宠物
            SetClass(CLASS_WARRIOR);
            SetGender(GENDER_NONE);
            SetSheath(SHEATH_STATE_MELEE);
            ReplaceAllPetFlags(petInfo->WasRenamed ? UNIT_PET_FLAG_CAN_BE_ABANDONED : (UNIT_PET_FLAG_CAN_BE_RENAMED | UNIT_PET_FLAG_CAN_BE_ABANDONED));
            ReplaceAllUnitFlags(UNIT_FLAG_PLAYER_CONTROLLED); // 启用弹出窗口（宠物遗弃、取消）
            SetMaxPower(POWER_HAPPINESS, GetCreatePowerValue(POWER_HAPPINESS));
            SetPower(POWER_HAPPINESS, petInfo->Happiness);  // 设置快乐度
            break;
        default:
            if (!IsPetGhoul())
                TC_LOG_ERROR("entities.pet", "Pet have incorrect type ({}) for pet loading.", getPetType());
            break;
    }

    SetPetNameTimestamp(uint32(GameTime::GetGameTime()));
    SetCreatorGUID(owner->GetGUID());

    // 初始化等级属性
    InitStatsForLevel(petlevel);
    SetPetExperience(petInfo->Experience);

    // 与主人等级同步
    SynchronizeLevelWithOwner();

    // 设置宠物位置（等级决定大小）
    float px, py, pz;
    owner->GetClosePoint(px, py, pz, GetCombatReach(), PET_FOLLOW_DIST, GetFollowAngle());
    Relocate(px, py, pz, owner->GetOrientation());
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.pet", "Pet {} not loaded. Suggested coordinates isn't valid (X: {} Y: {})",
            GetGUID().ToString(), GetPositionX(), GetPositionY());
        return false;
    }

    SetReactState(petInfo->ReactState);
    SetCanModifyStats(true);

    // 设置生命值和法力值
    if (getPetType() == SUMMON_PET && !current)  // 召唤宠物被召唤时满生命值
        SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    else
    {
        uint32 savedhealth = petInfo->Health;
        uint32 savedmana = petInfo->Mana;
        if (!savedhealth && getPetType() == HUNTER_PET)
            setDeathState(JUST_DIED);  // 猎人宠物没有生命值则死亡
        else
        {
            SetHealth(savedhealth > GetMaxHealth() ? GetMaxHealth() : savedhealth);
            SetPower(POWER_MANA, savedmana > GetMaxPower(POWER_MANA) ? GetMaxPower(POWER_MANA) : savedmana);
        }
    }

    // 设置当前宠物槽位
    // 0=当前宠物
    // 1..MAX_PET_STABLES 在兽栏槽位中
    // PET_SAVE_NOT_IN_SLOT(100) = 不在兽栏槽位（召唤中）
    if (slot == PET_SAVE_NOT_IN_SLOT)
    {
        uint32 petInfoNumber = petInfo->PetNumber;
        if (petStable->CurrentPet)
            owner->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);

        auto unslottedPetItr = std::find_if(petStable->UnslottedPets.begin(), petStable->UnslottedPets.end(), [&](PetStable::PetInfo const& unslottedPet)
        {
            return unslottedPet.PetNumber == petInfoNumber;
        });
        ASSERT(!petStable->CurrentPet);
        ASSERT(unslottedPetItr != petStable->UnslottedPets.end());

        petStable->CurrentPet = std::move(*unslottedPetItr);
        petStable->UnslottedPets.erase(unslottedPetItr);

        // 旧的petInfo指针不再有效，刷新它
        petInfo = &petStable->CurrentPet.value();
    }
    else if (PET_SAVE_FIRST_STABLE_SLOT <= slot && slot <= PET_SAVE_LAST_STABLE_SLOT)
    {
        auto stabledPet = std::find_if(petStable->StabledPets.begin(), petStable->StabledPets.end(), [petnumber](Optional<PetStable::PetInfo> const& pet)
        {
            return pet && pet->PetNumber == petnumber;
        });
        ASSERT(stabledPet != petStable->StabledPets.end());

        std::swap(*stabledPet, petStable->CurrentPet);

        // 旧的petInfo指针不再有效，刷新它
        petInfo = &petStable->CurrentPet.value();
    }

    // 发送虚假的召唤法术施放 - 这是正确应用技能冷却所需的
    // 例如：46584 - 没有这个冷却（应该在宠物加载时始终设置），客户端不会设置
    /// @todo 宠物应该通过真实的施法召唤，而不是伪造？
    if (petInfo->CreatedBySpellId)
    {
        WorldPackets::Spells::SpellGo spellGo;
        spellGo.Cast.CasterGUID = owner->GetGUID();
        spellGo.Cast.CasterUnit = owner->GetGUID();
        spellGo.Cast.SpellID = petInfo->CreatedBySpellId;
        spellGo.Cast.CastFlags = CAST_FLAG_UNKNOWN_9;
        spellGo.Cast.CastTime = GameTime::GetGameTimeMS();
        owner->SendMessageToSet(spellGo.Write(), true);
    }

    owner->SetMinion(this, true);

    // 加载动作条（非临时召唤宠物）
    if (!isTemporarySummon)
        m_charmInfo->LoadPetActionBar(petInfo->ActionBar);

    map->AddToMap(ToCreature());

    // 设置最后使用的宠物编号（用于战场）
    if (owner->GetTypeId() == TYPEID_PLAYER && isControlled() && !isTemporarySummoned() && (getPetType() == SUMMON_PET || getPetType() == HUNTER_PET))
        owner->ToPlayer()->SetLastPetNumber(petInfo->PetNumber);

    // 异步加载宠物的光环、技能和冷却数据
    owner->GetSession()->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(std::make_shared<PetLoadQueryHolder>(ownerid, petInfo->PetNumber)))
        .AfterComplete([this, owner, session = owner->GetSession(), isTemporarySummon, current, lastSaveTime = petInfo->LastSaveTime](SQLQueryHolderBase const& holder)
    {
        if (session->GetPlayer() != owner || owner->GetPet() != this)
            return;

        // 通过前面的检查确保'this'仍然有效
        if (m_removed)
            return;

        InitTalentForLevel();  // 在加载技能前设置原始天赋点数

        uint32 timediff = uint32(GameTime::GetGameTime() - lastSaveTime);
        _LoadAuras(holder.GetPreparedResult(PetLoadQueryHolder::AURAS), timediff);

        // 加载动作条，如果数据损坏将用默认技能填充
        if (!isTemporarySummon)
        {
            _LoadSpells(holder.GetPreparedResult(PetLoadQueryHolder::SPELLS));
            InitTalentForLevel();  // 重新初始化以检查天赋数量
            GetSpellHistory()->LoadFromDB<Pet>(holder.GetPreparedResult(PetLoadQueryHolder::COOLDOWNS));
            LearnPetPassives();
            InitLevelupSpellsForLevel();
            if (GetMap()->IsBattleArena())
                RemoveArenaAuras();

            CastPetAuras(current);
        }

        CleanupActionBar();  // 加载后从动作条中移除未知技能

        TC_LOG_DEBUG("entities.pet", "New Pet has {}", GetGUID().ToString());

        owner->PetSpellInitialize();

        if (owner->GetGroup())
            owner->SetGroupUpdateFlag(GROUP_UPDATE_PET);

        owner->SendTalentsInfoData(true);

        if (getPetType() == HUNTER_PET)
        {
            // 加载名称变格（用于俄语等语言）
            if (PreparedQueryResult result = holder.GetPreparedResult(PetLoadQueryHolder::DECLINED_NAMES))
            {
                m_declinedname = std::make_unique<DeclinedName>();
                Field* fields = result->Fetch();
                for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
                    m_declinedname->name[i] = fields[i].GetString();
            }
        }

        // 必须在SetMinion之后（检查所有者GUID）
        LoadTemplateImmunities();
        m_loading = false;
    });

    return true;
}

/**
 * @brief 保存宠物到数据库
 *
 * 职责：将宠物的当前状态保存到数据库
 *
 * 参数：
 *   mode - 宠物保存模式（PET_SAVE_AS_CURRENT当前、PET_SAVE_NOT_IN_SLOT非槽位、或删除）
 *
 * 调用时机：宠物被解散、玩家下线、宠物被存放兽栏时
 *
 * 主要流程：
 *   1. 检查宠物是否可以被保存
 *   2. 保存光环、技能、冷却到数据库
 *   3. 根据保存模式处理宠物数据
 *   4. 更新宠物存储信息
 */
void Pet::SavePetToDB(PetSaveMode mode)
{
    if (!GetEntry())
        return;

    // 只保存完全控制的生物
    if (!isControlled())
        return;

    // 不保存非玩家宠物
    if (!GetOwnerGUID().IsPlayer())
        return;

    Player* owner = GetOwner();

    // 如果有其他宠物被临时解除召唤，不将其保存为当前宠物
    if (mode == PET_SAVE_AS_CURRENT && owner->GetTemporaryUnsummonedPetNumber() &&
        owner->GetTemporaryUnsummonedPetNumber() != m_charmInfo->GetPetNumber())
    {
        // 恢复临时解除召唤时宠物会丢失
        if (getPetType() == HUNTER_PET)
            return;

        // 术士的情况
        mode = PET_SAVE_NOT_IN_SLOT;
    }

    uint32 curhealth = GetHealth();
    uint32 curmana = GetPower(POWER_MANA);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    // 在可能移除光环之前保存它们
    _SaveAuras(trans);

    // 兽栏和非槽位保存
    if (mode > PET_SAVE_AS_CURRENT)
        RemoveAllAuras();

    _SaveSpells(trans);
    GetSpellHistory()->SaveToDB<Pet>(trans);
    CharacterDatabase.CommitTransaction(trans);

    // 当前/兽栏/非槽位保存
    if (mode >= PET_SAVE_AS_CURRENT)
    {
        ObjectGuid::LowType ownerLowGUID = GetOwnerGUID().GetCounter();
        trans = CharacterDatabase.BeginTransaction();
        // 移除当前数据

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_PET_BY_ID);
        stmt->setUInt32(0, m_charmInfo->GetPetNumber());
        trans->Append(stmt);

        // 防止在PET_SAVE_AS_CURRENT和PET_SAVE_NOT_IN_SLOT中存在其他猎人宠物
        if (getPetType() == HUNTER_PET && (mode == PET_SAVE_AS_CURRENT || mode > PET_SAVE_LAST_STABLE_SLOT))
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_PET_BY_SLOT);
            stmt->setUInt32(0, ownerLowGUID);
            stmt->setUInt8(1, uint8(PET_SAVE_AS_CURRENT));
            stmt->setUInt8(2, uint8(PET_SAVE_LAST_STABLE_SLOT));
            trans->Append(stmt);
        }

        // 保存宠物
        std::string actionBar = GenerateActionBarData();

        ASSERT(owner->GetPetStable()->CurrentPet && owner->GetPetStable()->CurrentPet->PetNumber == m_charmInfo->GetPetNumber());
        FillPetInfo(&owner->GetPetStable()->CurrentPet.value());

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PET);
        stmt->setUInt32(0, m_charmInfo->GetPetNumber());
        stmt->setUInt32(1, GetEntry());
        stmt->setUInt32(2, ownerLowGUID);
        stmt->setUInt32(3, GetNativeDisplayId());
        stmt->setUInt8(4, GetLevel());
        stmt->setUInt32(5, GetUInt32Value(UNIT_FIELD_PETEXPERIENCE));
        stmt->setUInt8(6, GetReactState());
        stmt->setUInt8(7, mode);
        stmt->setString(8, m_name);
        stmt->setUInt8(9, HasPetFlag(UNIT_PET_FLAG_CAN_BE_RENAMED) ? 0 : 1);
        stmt->setUInt32(10, curhealth);
        stmt->setUInt32(11, curmana);
        stmt->setUInt32(12, GetPower(POWER_HAPPINESS));
        stmt->setString(13, actionBar);
        stmt->setUInt32(14, GameTime::GetGameTime());
        stmt->setUInt32(15, GetUInt32Value(UNIT_CREATED_BY_SPELL));
        stmt->setUInt8(16, getPetType());
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);
    }
    // 删除
    else
    {
        RemoveAllAuras();
        DeleteFromDB(m_charmInfo->GetPetNumber());
    }
}

/**
 * @brief 填充宠物信息结构
 *
 * 职责：将宠物当前状态填充到宠物信息结构中用于保存
 *
 * 参数：
 *   petInfo - 宠物信息结构指针
 *
 * 调用时机：保存宠物到数据库之前
 */
void Pet::FillPetInfo(PetStable::PetInfo* petInfo) const
{
    petInfo->PetNumber = m_charmInfo->GetPetNumber();
    petInfo->CreatureId = GetEntry();
    petInfo->DisplayId = GetNativeDisplayId();
    petInfo->Level = GetLevel();
    petInfo->Experience = GetUInt32Value(UNIT_FIELD_PETEXPERIENCE);
    petInfo->ReactState = GetReactState();
    petInfo->Name = GetName();
    petInfo->WasRenamed = !HasPetFlag(UNIT_PET_FLAG_CAN_BE_RENAMED);
    petInfo->Health = GetHealth();
    petInfo->Mana = GetPower(POWER_MANA);
    petInfo->Happiness = GetPower(POWER_HAPPINESS);
    petInfo->ActionBar = GenerateActionBarData();
    petInfo->LastSaveTime = GameTime::GetGameTime();
    petInfo->CreatedBySpellId = GetUInt32Value(UNIT_CREATED_BY_SPELL);
    petInfo->Type = getPetType();
}

/**
 * @brief 从数据库删除宠物
 *
 * 职责：删除数据库中宠物的所有数据
 *
 * 参数：
 *   guidlow - 宠物的低GUID
 *
 * 调用时机：宠物被永久删除时
 *
 * 主要流程：
 *   1. 删除宠物基本信息
 *   2. 删除名称变格数据
 *   3. 删除光环数据
 *   4. 删除技能数据
 *   5. 删除冷却数据
 */
void Pet::DeleteFromDB(ObjectGuid::LowType guidlow)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 删除宠物基本数据
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_PET_BY_ID);
    stmt->setUInt32(0, guidlow);
    trans->Append(stmt);

    // 删除名称变格
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_PET_DECLINEDNAME);
    stmt->setUInt32(0, guidlow);
    trans->Append(stmt);

    // 删除光环
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_AURAS);
    stmt->setUInt32(0, guidlow);
    trans->Append(stmt);

    // 删除技能
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_SPELLS);
    stmt->setUInt32(0, guidlow);
    trans->Append(stmt);

    // 删除冷却
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_SPELL_COOLDOWNS);
    stmt->setUInt32(0, guidlow);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 设置宠物死亡状态
 *
 * 职责：重写虚函数，处理宠物死亡相关的特殊逻辑
 *
 * 参数：
 *   s - 死亡状态（ALIVE、CORPSE、DEAD等）
 *
 * 调用时机：宠物死亡或复活时
 *
 * 主要流程：
 *   1. 调用父类方法设置死亡状态
 *   2. 如果进入尸体状态，处理猎人宠物的快乐度损失
 *   3. 如果复活，重新施放宠物光环
 */
void Pet::setDeathState(DeathState s)
{
    Creature::setDeathState(s);
    if (getDeathState() == CORPSE)
    {
        if (getPetType() == HUNTER_PET)
        {
            // 宠物尸体不可掠夺和剥皮
            ReplaceAllDynamicFlags(UNIT_DYNFLAG_NONE);
            RemoveUnitFlag(UNIT_FLAG_SKINNABLE);

            // 死亡时在战场/竞技场外损失快乐度
            if (!GetMap()->IsBattlegroundOrArena())
                ModifyPower(POWER_HAPPINESS, -HAPPINESS_LEVEL_SIZE);
        }
    }
    else if (getDeathState() == ALIVE)
    {
        // 复活时施放宠物光环
        CastPetAuras(true);
    }
}

/**
 * @brief 更新宠物状态
 *
 * 职责：处理宠物的周期性更新逻辑
 *
 * 参数：
 *   diff - 距离上次更新的时间间隔（毫秒）
 *
 * 调用时机：每个游戏循环tick
 *
 * 主要流程：
 *   1. 检查宠物是否已被移除或正在加载
 *   2. 根据死亡状态处理不同逻辑
 *   3. 存活状态：检查与主人的距离、持续时间、焦点恢复、快乐度衰减
 *   4. 尸体状态：检查是否应该移除
 *   5. 调用父类Update方法
 */
void Pet::Update(uint32 diff)
{
    // 宠物已移除，等待移除队列，不更新
    if (m_removed)
        return;

    // 宠物正在加载中，不更新
    if (m_loading)
        return;

    switch (m_deathState)
    {
        case CORPSE:
        {
            // 猎人宠物不会因死亡被移除，但其他宠物会
            if (getPetType() != HUNTER_PET || m_corpseRemoveTime <= GameTime::GetGameTime())
            {
                Remove(PET_SAVE_NOT_IN_SLOT);
                return;
            }
            break;
        }
        case ALIVE:
        {
            // 检查与主人的距离，超出可见范围则解除召唤
            Player* owner = GetOwner();
            if ((!IsWithinDistInMap(owner, GetMap()->GetVisibilityRange()) && !isPossessed()) || (isControlled() && !owner->GetPetGUID()))
            {
                Remove(PET_SAVE_NOT_IN_SLOT, true);
                return;
            }

            // 验证宠物与主人的关联
            if (isControlled())
            {
                if (owner->GetPetGUID() != GetGUID())
                {
                    TC_LOG_ERROR("entities.pet", "Pet {} is not pet of owner {}, removed", GetEntry(), GetOwner()->GetName());
                    ASSERT(getPetType() != HUNTER_PET, "Unexpected unlinked pet found for owner %s", owner->GetSession()->GetPlayerInfo().c_str());
                    Remove(PET_SAVE_NOT_IN_SLOT);
                    return;
                }
            }

            // 处理持续时间（临时宠物）
            if (m_duration > 0)
            {
                if (uint32(m_duration) > diff)
                    m_duration -= diff;
                else
                {
                    // 持续时间结束，移除宠物
                    Remove(getPetType() != SUMMON_PET ? PET_SAVE_AS_DELETED : PET_SAVE_NOT_IN_SLOT);
                    return;
                }
            }

            // 焦点恢复（猎人宠物）或能量恢复（死亡骑士食尸鬼）
            if (m_focusRegenTimer)
            {
                if (m_focusRegenTimer > diff)
                    m_focusRegenTimer -= diff;
                else
                {
                    switch (GetPowerType())
                    {
                        case POWER_FOCUS:
                            Regenerate(POWER_FOCUS);
                            m_focusRegenTimer += PET_FOCUS_REGEN_INTERVAL - diff;
                            if (!m_focusRegenTimer) ++m_focusRegenTimer;

                            // 如果大diff（延迟）导致焦点卡住，重置计时器
                            if (m_focusRegenTimer > PET_FOCUS_REGEN_INTERVAL)
                                m_focusRegenTimer = PET_FOCUS_REGEN_INTERVAL;

                            break;

                        // 在Creature::Update中处理
                        //case POWER_ENERGY:
                        //    Regenerate(POWER_ENERGY);
                        //    m_regenTimer += CREATURE_REGEN_INTERVAL - diff;
                        //    if (!m_regenTimer) ++m_regenTimer;
                        //    break;
                        default:
                            m_focusRegenTimer = 0;
                            break;
                    }
                }
            }

            // 猎人宠物专属：快乐度衰减
            if (getPetType() != HUNTER_PET)
                break;

            if (m_happinessTimer <= diff)
            {
                LoseHappiness();  // 损失快乐度
                m_happinessTimer = 7500;  // 7.5秒
            }
            else
                m_happinessTimer -= diff;

            break;
        }
        default:
            break;
    }
    Creature::Update(diff);
}

/**
 * @brief 损失快乐度
 *
 * 职责：猎人宠物定期损失快乐度
 *
 * 调用时机：每7.5秒由Update调用
 *
 * 主要流程：
 *   1. 检查快乐度是否大于0
 *   2. 计算损失值（战斗中损失更快）
 *   3. 减少快乐度
 */
void Pet::LoseHappiness()
{
    uint32 curValue = GetPower(POWER_HAPPINESS);
    if (curValue <= 0)
        return;
    // 值为每分钟70/35/17/8/4 * 1000 / 8（计时器7.5秒）
    int32 addvalue = 670;
    if (IsInCombat())  // 战斗中快乐度衰减更快
        addvalue = int32(addvalue * 1.5f);
    ModifyPower(POWER_HAPPINESS, -addvalue);
}

/**
 * @brief 获取快乐度状态
 *
 * 职责：根据快乐度值返回快乐状态枚举
 *
 * 返回值：UNHAPPY不快乐、CONTENT一般、HAPPY快乐
 */
HappinessState Pet::GetHappinessState()
{
    if (GetPower(POWER_HAPPINESS) < HAPPINESS_LEVEL_SIZE)
        return UNHAPPY;
    else if (GetPower(POWER_HAPPINESS) >= HAPPINESS_LEVEL_SIZE * 2)
        return HAPPY;
    else
        return CONTENT;
}

/**
 * @brief 移除宠物
 *
 * 职责：通过所有者移除宠物
 *
 * 参数：
 *   mode          - 宠物保存模式
 *   returnreagent - 是否返还施法材料
 */
void Pet::Remove(PetSaveMode mode, bool returnreagent)
{
    GetOwner()->RemovePet(this, mode, returnreagent);
}

/**
 * @brief 给予宠物经验值
 *
 * 职责：为猎人宠物增加经验值并处理升级
 *
 * 参数：
 *   xp - 经验值数量
 *
 * 调用时机：主人获得经验时
 *
 * 主要流程：
 *   1. 检查宠物类型是否为猎人宠物
 *   2. 检查宠物等级是否已达上限
 *   3. 增加经验值并处理升级
 */
void Pet::GivePetXP(uint32 xp)
{
    // 只有猎人宠物可以获得经验
    if (getPetType() != HUNTER_PET)
        return;

    if (xp < 1)
        return;

    if (!IsAlive())
        return;

    // 宠物最高等级不能超过玩家等级
    uint8 maxlevel = std::min((uint8)sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL), GetOwner()->GetLevel());
    uint8 petlevel = GetLevel();

    // 如果宠物等级已达到或超过玩家等级，不给予经验
    if (petlevel >= maxlevel)
       return;

    uint32 nextLvlXP = GetUInt32Value(UNIT_FIELD_PETNEXTLEVELEXP);
    uint32 curXP = GetUInt32Value(UNIT_FIELD_PETEXPERIENCE);
    uint32 newXP = curXP + xp;

    // 检查宠物应该获得多少经验，处理升级
    while (newXP >= nextLvlXP && petlevel < maxlevel)
    {
        // 从下一级所需经验中减去，并给宠物升级
        newXP -= nextLvlXP;
        ++petlevel;

        GivePetLevel(petlevel);

        nextLvlXP = GetUInt32Value(UNIT_FIELD_PETNEXTLEVELEXP);
    }
    // 不受特殊条件影响 - 设置新经验值
    SetPetExperience(petlevel < maxlevel ? newXP : 0);
}

/**
 * @brief 设置宠物等级
 *
 * 职责：设置宠物等级并更新相关属性
 *
 * 参数：
 *   level - 目标等级
 *
 * 调用时机：宠物升级或与主人等级同步时
 *
 * 主要流程：
 *   1. 检查等级是否有效
 *   2. 设置经验值（猎人宠物）
 *   3. 初始化等级属性
 *   4. 学习升级技能
 *   5. 初始化天赋
 */
void Pet::GivePetLevel(uint8 level)
{
    if (!level || level == GetLevel())
        return;

    if (getPetType() == HUNTER_PET)
    {
        SetPetExperience(0);
        SetPetNextLevelExperience(uint32(sObjectMgr->GetXPForLevel(level)*PET_XP_FACTOR));
    }

    InitStatsForLevel(level);      // 初始化属性
    InitLevelupSpellsForLevel();   // 学习升级技能
    InitTalentForLevel();          // 初始化天赋
}

/**
 * @brief 基于生物创建宠物基础数据
 *
 * 职责：从现有生物创建宠物的基础数据
 *
 * 参数：
 *   creature - 源生物对象
 *
 * 返回值：创建成功返回true
 *
 * 调用时机：驯服野兽时
 *
 * 主要流程：
 *   1. 调用CreateBaseAtTamed创建基础数据
 *   2. 复制源生物的位置
 *   3. 设置显示ID和名称
 */
bool Pet::CreateBaseAtCreature(Creature* creature)
{
    ASSERT(creature);

    if (!CreateBaseAtTamed(creature->GetCreatureTemplate(), creature->GetMap(), creature->GetPhaseMask()))
        return false;

    // 复制源生物的位置
    Relocate(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation());

    if (!IsPositionValid())
    {
        TC_LOG_ERROR("entities.pet", "Pet {} not created base at creature. Suggested coordinates isn't valid (X: {} Y: {})",
            GetGUID().ToString(), GetPositionX(), GetPositionY());
        return false;
    }

    CreatureTemplate const* cinfo = GetCreatureTemplate();
    if (!cinfo)
    {
        TC_LOG_ERROR("entities.pet", "CreateBaseAtCreature() failed, creatureInfo is missing!");
        return false;
    }

    // 设置显示ID
    SetDisplayId(creature->GetDisplayId());

    // 设置名称（优先使用生物家族名称）
    if (CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(cinfo->family))
        SetName(cFamily->Name[sWorld->GetDefaultDbcLocale()]);
    else
        SetName(creature->GetNameForLocaleIdx(sObjectMgr->GetDBCLocaleIndex()));

    return true;
}

/**
 * @brief 基于生物模板创建宠物基础数据
 *
 * 职责：从生物模板创建宠物的基础数据
 *
 * 参数：
 *   cinfo - 生物模板
 *   owner - 宠物主人
 *
 * 返回值：创建成功返回true
 *
 * 调用时机：召唤宠物时
 */
bool Pet::CreateBaseAtCreatureInfo(CreatureTemplate const* cinfo, Unit* owner)
{
    if (!CreateBaseAtTamed(cinfo, owner->GetMap(), owner->GetPhaseMask()))
        return false;

    // 设置名称（使用生物家族名称）
    if (CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(cinfo->family))
        SetName(cFamily->Name[sWorld->GetDefaultDbcLocale()]);

    // 定位到主人位置
    Relocate(owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), owner->GetOrientation());

    return true;
}

/**
 * @brief 创建驯服宠物的基础数据
 *
 * 职责：为新驯服的宠物创建基础数据结构
 *
 * 参数：
 *   cinfo    - 生物模板
 *   map      - 所在地图
 *   phaseMask - 相位掩码
 *
 * 返回值：创建成功返回true
 *
 * 调用时机：驯服野兽或创建新宠物时
 *
 * 主要流程：
 *   1. 生成宠物GUID和编号
 *   2. 调用Create创建宠物对象
 *   3. 初始化快乐度、经验值
 *   4. 设置职业和宠物标志
 */
bool Pet::CreateBaseAtTamed(CreatureTemplate const* cinfo, Map* map, uint32 phaseMask)
{
    TC_LOG_DEBUG("entities.pet", "Pet::CreateBaseForTamed");
    ObjectGuid::LowType guid = map->GenerateLowGuid<HighGuid::Pet>();
    uint32 petId = sObjectMgr->GeneratePetNumber();
    if (!Create(guid, map, phaseMask, cinfo->Entry, petId))
        return false;

    // 初始化快乐度（猎人宠物）
    SetMaxPower(POWER_HAPPINESS, GetCreatePowerValue(POWER_HAPPINESS));
    SetPower(POWER_HAPPINESS, 166500);  // 初始快乐度
    SetPetNameTimestamp(0);
    SetPetExperience(0);
    SetPetNextLevelExperience(uint32(sObjectMgr->GetXPForLevel(GetLevel()+1)*PET_XP_FACTOR));
    ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);

    // 野兽类型宠物的设置
    if (cinfo->type == CREATURE_TYPE_BEAST)
    {
        SetClass(CLASS_WARRIOR);
        SetGender(GENDER_NONE);
        SetSheath(SHEATH_STATE_MELEE);
        ReplaceAllPetFlags(UNIT_PET_FLAG_CAN_BE_RENAMED | UNIT_PET_FLAG_CAN_BE_ABANDONED);  // 可重命名和遗弃
    }

    return true;
}

/**
 * @brief 初始化指定等级的宠物属性
 *
 * 职责：根据宠物等级初始化所有基础属性
 *
 * 参数：
 *   petlevel - 宠物等级
 *
 * 返回值：初始化成功返回true
 *
 * 调用时机：宠物创建、升级时
 *
 * 主要流程：
 *   1. 确定宠物类型
 *   2. 设置护甲、攻击速度等基础属性
 *   3. 从数据库加载或计算生命值、法力值
 *   4. 设置能量类型（焦点/能量/法力）
 *   5. 根据宠物类型设置伤害加成
 */
/// @todo 将属性修正代码移到宠物被动光环
bool Guardian::InitStatsForLevel(uint8 petlevel)
{
    CreatureTemplate const* cinfo = GetCreatureTemplate();
    ASSERT(cinfo);

    SetLevel(petlevel);

    // 确定宠物类型
    PetType petType = MAX_PET_TYPE;
    if (IsPet() && GetOwner()->GetTypeId() == TYPEID_PLAYER)
    {
        if (GetOwner()->GetClass() == CLASS_WARLOCK
            || GetOwner()->GetClass() == CLASS_SHAMAN        // 火元素
            || GetOwner()->GetClass() == CLASS_DEATH_KNIGHT) // 复活的食尸鬼
        {
            petType = SUMMON_PET;
        }
        else if (GetOwner()->GetClass() == CLASS_HUNTER)
        {
            petType = HUNTER_PET;
            m_unitTypeMask |= UNIT_MASK_HUNTER_PET;
        }
        else
        {
            TC_LOG_ERROR("entities.pet", "Unknown type pet {} is summoned by player class {}",
                           GetEntry(), GetOwner()->GetClass());
        }
    }

    // 猎人宠物使用通用ID 1
    uint32 creature_ID = (petType == HUNTER_PET) ? 1 : cinfo->Entry;

    // 设置近战伤害类型
    SetMeleeDamageSchool(SpellSchools(cinfo->dmgschool));

    // 设置基础护甲值
    SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, float(petlevel * 50));

    // 设置攻击时间
    SetAttackTime(BASE_ATTACK, BASE_ATTACK_TIME);
    SetAttackTime(OFF_ATTACK, BASE_ATTACK_TIME);
    SetAttackTime(RANGED_ATTACK, BASE_ATTACK_TIME);

    SetModCastingSpeed(1.0f);

    // 设置缩放比例
    SetObjectScale(GetNativeObjectScale());

    // 设置抗性（猎人宠物不从creature_template继承抗性，他们有单独的光环）
    if (!IsHunterPet())
        for (uint8 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
            SetStatFlatModifier(UnitMods(UNIT_MOD_RESISTANCE_START + i), BASE_VALUE, float(cinfo->resistance[i]));

    // 从数据库加载生命值、法力值、护甲和属性
    PetLevelInfo const* pInfo = sObjectMgr->GetPetLevelInfo(creature_ID, petlevel);
    if (pInfo)  // 数据库中存在数据
    {
        SetCreateHealth(pInfo->health);
        SetCreateMana(pInfo->mana);

        if (pInfo->armor > 0)
            SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, float(pInfo->armor));

        for (uint8 stat = 0; stat < MAX_STATS; ++stat)
            SetCreateStat(Stats(stat), float(pInfo->stats[stat]));
    }
    else  // 数据库中不存在，使用默认假数据
    {
        // 移除包含在数据库值中的精英加成
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(petlevel, cinfo->unit_class);
        float healthmod = _GetHealthMod(cinfo->rank);
        uint32 basehp = stats->GenerateHealth(cinfo);
        uint32 health = uint32(basehp * healthmod);
        uint32 mana = stats->GenerateMana(cinfo);

        SetCreateHealth(health);
        SetCreateMana(mana);
        // 默认属性值
        SetCreateStat(STAT_STRENGTH, 22);
        SetCreateStat(STAT_AGILITY, 22);
        SetCreateStat(STAT_STAMINA, 25);
        SetCreateStat(STAT_INTELLECT, 28);
        SetCreateStat(STAT_SPIRIT, 27);
    }

    // 设置能量类型
    if (petType == HUNTER_PET) // 猎人宠物使用焦点
        SetPowerType(POWER_FOCUS);
    else if (IsPetGhoul() || IsRisenAlly()) // 死亡骑士宠物使用能量
    {
        SetPowerType(POWER_ENERGY);
        SetFullPower(POWER_ENERGY);
    }
    else
        SetPowerType(POWER_MANA);

    // 设置伤害
    SetBonusDamage(0);
    switch (petType)
    {
        case SUMMON_PET:
        {
            // 伤害加成使用火焰或暗影伤害中较高的那个
            int32 fire = GetOwner()->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_FIRE));
            int32 shadow = GetOwner()->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_SHADOW));
            int32 val = (fire > shadow) ? fire : shadow;
            if (val < 0)
                val = 0;

            SetBonusDamage(val * 0.15f);

            if (pInfo)
            {
                SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(pInfo->minDamage));
                SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(pInfo->maxDamage));
            }
            else
            {
                SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel - (petlevel / 4)));
                SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel + (petlevel / 4)));
            }
            break;
        }
        case HUNTER_PET:
        {
            SetUInt32Value(UNIT_FIELD_PETNEXTLEVELEXP, uint32(sObjectMgr->GetXPForLevel(petlevel)*PET_XP_FACTOR));
            // 这些公式可能不正确，但设计上接近应有的值
            // 这使DPS为宠物等级的0.5
            SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel - (petlevel / 4)));
            // 伤害范围为宠物等级/2
            SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel + (petlevel / 4)));
            // 伤害随后因力量和宠物缩放修正攻击强度而增加
            break;
        }
        default:
        {
            // 特殊宠物的定制化设置
            switch (GetEntry())
            {
                case 510: // 法师水元素
                {
                    SetBonusDamage(int32(GetOwner()->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_FROST) * 0.33f));
                    break;
                }
                case 1964: // 自然之力（树人）
                {
                    if (!pInfo)
                        SetCreateHealth(30 + 30*petlevel);
                    float bonusDmg = GetOwner()->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_NATURE) * 0.15f;
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel * 2.5f - (petlevel / 2) + bonusDmg));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel * 2.5f + (petlevel / 2) + bonusDmg));
                    break;
                }
                case 15352: // 大地元素
                {
                    if (!pInfo)
                        SetCreateHealth(100 + 120*petlevel);
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel - (petlevel / 4)));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel + (petlevel / 4)));
                    break;
                }
                case 15438: // 火元素
                {
                    if (!pInfo)
                    {
                        SetCreateHealth(40*petlevel);
                        SetCreateMana(28 + 10*petlevel);
                    }
                    SetBonusDamage(int32(GetOwner()->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_FIRE) * 0.5f));
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel * 4 - petlevel));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel * 4 + petlevel));
                    break;
                }
                case 19668: // 暗影魔
                {
                    if (!pInfo)
                    {
                        SetCreateMana(28 + 10*petlevel);
                        SetCreateHealth(28 + 30*petlevel);
                    }
                    int32 bonus_dmg = int32(GetOwner()->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_SHADOW)* 0.3f);
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float((petlevel * 4 - petlevel) + bonus_dmg));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float((petlevel * 4 + petlevel) + bonus_dmg));
                    break;
                }
                case 19833: // 毒蛇陷阱 - 毒蛇
                {
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float((petlevel / 2) - 25));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float((petlevel / 2) - 18));
                    break;
                }
                case 19921: // 毒蛇陷阱 - 毒蛇
                {
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel / 2 - 10));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel / 2));
                    break;
                }
                case 29264: // 野性狼魂
                {
                    if (!pInfo)
                        SetCreateHealth(30*petlevel);

                    // 狼的攻击速度为1.5秒
                    SetAttackTime(BASE_ATTACK, cinfo->BaseAttackTime);

                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float((petlevel * 4 - petlevel)));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float((petlevel * 4 + petlevel)));

                    // 加成护甲（玩家护甲的35%）
                    SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, float(GetOwner()->GetArmor()) * 0.35f);
                    // 加成耐力（玩家耐力的30%）
                    SetStatFlatModifier(UNIT_MOD_STAT_STAMINA, BASE_VALUE, float(GetOwner()->GetStat(STAT_STAMINA)) * 0.3f);
                    // 灵魂狩猎：被动，灵魂狼的攻击为自己和主人治疗造成伤害的150%
                    if (!HasAura(58877))
                        AddAura(58877, this);
                    break;
                }
                case 31216: // 镜像
                {
                    SetBonusDamage(int32(GetOwner()->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_FROST) * 0.33f));
                    SetDisplayId(GetOwner()->GetDisplayId());  // 使用主人的外观
                    if (!pInfo)
                    {
                        SetCreateMana(28 + 30*petlevel);
                        SetCreateHealth(28 + 10*petlevel);
                    }
                    break;
                }
                case 27829: // 石像鬼
                {
                    if (!pInfo)
                    {
                        SetCreateMana(28 + 10*petlevel);
                        SetCreateHealth(28 + 30*petlevel);
                    }
                    SetBonusDamage(int32(GetOwner()->GetTotalAttackPowerValue(BASE_ATTACK) * 0.5f));
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel - (petlevel / 4)));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel + (petlevel / 4)));
                    break;
                }
                case 28017: // 血虫
                {
                    SetCreateHealth(4 * petlevel);
                    SetBonusDamage(int32(GetOwner()->GetTotalAttackPowerValue(BASE_ATTACK) * 0.006f));
                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(petlevel - 30 - (petlevel / 4)));
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(petlevel - 30 + (petlevel / 4)));
                    break;
                }
                default:
                {
                    // 默认伤害计算
                    CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(petlevel, cinfo->unit_class);
                    float basedamage = stats->GenerateBaseDamage(cinfo);

                    float weaponBaseMinDamage = basedamage;
                    float weaponBaseMaxDamage = basedamage * 1.5f;

                    SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, weaponBaseMinDamage);
                    SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, weaponBaseMaxDamage);
                    break;
                }
            }
            break;
        }
    }

    UpdateAllStats();

    SetFullHealth();
    SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    return true;
}

/**
 * @brief 检查物品是否在宠物食谱中
 *
 * 职责：判断指定物品是否可以被宠物食用
 *
 * 参数：
 *   item - 物品模板
 *
 * 返回值：在食谱中返回true
 */
bool Pet::HaveInDiet(ItemTemplate const* item) const
{
    if (!item->FoodType)
        return false;

    CreatureTemplate const* cInfo = GetCreatureTemplate();
    if (!cInfo)
        return false;

    CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(cInfo->family);
    if (!cFamily)
        return false;

    uint32 diet = cFamily->PetFoodMask;
    uint32 FoodMask = 1 << (item->FoodType-1);
    return (diet & FoodMask) != 0;
}

/**
 * @brief 获取当前食物的受益等级
 *
 * 职责：计算宠物从指定等级食物中获得的快乐度收益
 *
 * 参数：
 *   itemlevel - 食物等级
 *
 * 返回值：快乐度收益值
 *
 * 说明：食物等级与宠物等级差距越小，收益越高
 */
uint32 Pet::GetCurrentFoodBenefitLevel(uint32 itemlevel) const
{
    // 食物等级差-5或更高
    if (GetLevel() <= itemlevel + 5)  // 可以用55级食物喂养60级宠物获得完整效果
        return 35000;
    // -10..-6
    else if (GetLevel() <= itemlevel + 10)
        return 17000;
    // -14..-11
    else if (GetLevel() <= itemlevel + 14)  // 55级食物在70级变绿，说得通
        return 8000;
    // -15或更少
    else
        return 0;  // 食物等级太低
}

/**
 * @brief 从数据库加载宠物技能
 *
 * 职责：从数据库查询结果加载宠物的技能列表
 *
 * 参数：
 *   result - 数据库查询结果
 *
 * 调用时机：宠物加载时
 */
void Pet::_LoadSpells(PreparedQueryResult result)
{
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            addSpell(fields[0].GetUInt32(), ActiveStates(fields[1].GetUInt8()), PETSPELL_UNCHANGED);
        }
        while (result->NextRow());
    }
}

/**
 * @brief 保存宠物技能到数据库
 *
 * 职责：将宠物技能状态变化保存到数据库
 *
 * 参数：
 *   trans - 数据库事务
 *
 * 主要流程：
 *   1. 遍历所有技能
 *   2. 根据技能状态执行插入、更新或删除操作
 *   3. 不保存家族被动技能
 */
void Pet::_SaveSpells(CharacterDatabaseTransaction trans)
{
    for (PetSpellMap::iterator itr = m_spells.begin(), next = m_spells.begin(); itr != m_spells.end(); itr = next)
    {
        ++next;

        // 不保存家族被动技能到数据库
        if (itr->second.type == PETSPELL_FAMILY)
            continue;

        CharacterDatabasePreparedStatement* stmt;

        switch (itr->second.state)
        {
            case PETSPELL_REMOVED:
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_SPELL_BY_SPELL);
                stmt->setUInt32(0, m_charmInfo->GetPetNumber());
                stmt->setUInt32(1, itr->first);
                trans->Append(stmt);

                m_spells.erase(itr);
                continue;
            case PETSPELL_CHANGED:
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_SPELL_BY_SPELL);
                stmt->setUInt32(0, m_charmInfo->GetPetNumber());
                stmt->setUInt32(1, itr->first);
                trans->Append(stmt);

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PET_SPELL);
                stmt->setUInt32(0, m_charmInfo->GetPetNumber());
                stmt->setUInt32(1, itr->first);
                stmt->setUInt8(2, itr->second.active);
                trans->Append(stmt);

                break;
            case PETSPELL_NEW:
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PET_SPELL);
                stmt->setUInt32(0, m_charmInfo->GetPetNumber());
                stmt->setUInt32(1, itr->first);
                stmt->setUInt8(2, itr->second.active);
                trans->Append(stmt);
                break;
            case PETSPELL_UNCHANGED:
                continue;
        }
        itr->second.state = PETSPELL_UNCHANGED;
    }
}

/**
 * @brief 从数据库加载宠物光环
 *
 * 职责：从数据库查询结果加载宠物的光环状态
 *
 * 参数：
 *   result   - 数据库查询结果
 *   timediff - 距离上次保存的时间差
 *
 * 调用时机：宠物加载时
 *
 * 主要流程：
 *   1. 遍历查询结果
 *   2. 解析光环数据
 *   3. 处理离线时间对负面效果的影响
 *   4. 创建并应用光环
 */
void Pet::_LoadAuras(PreparedQueryResult result, uint32 timediff)
{
    TC_LOG_DEBUG("entities.pet", "Loading auras for pet {}", GetGUID().ToString());

    if (result)
    {
        do
        {
            int32 damage[3];
            int32 baseDamage[3];
            Field* fields = result->Fetch();
            ObjectGuid caster_guid(fields[0].GetUInt64());
            // 存储空GUID - 宠物是法术的施法者 - 参见Pet::_SaveAuras
            if (!caster_guid)
                caster_guid = GetGUID();
            uint32 spellid = fields[1].GetUInt32();
            uint8 effmask = fields[2].GetUInt8();
            uint8 recalculatemask = fields[3].GetUInt8();
            uint8 stackcount = fields[4].GetUInt8();
            damage[0] = fields[5].GetInt32();
            damage[1] = fields[6].GetInt32();
            damage[2] = fields[7].GetInt32();
            baseDamage[0] = fields[8].GetInt32();
            baseDamage[1] = fields[9].GetInt32();
            baseDamage[2] = fields[10].GetInt32();
            int32 maxduration = fields[11].GetInt32();
            int32 remaintime = fields[12].GetInt32();
            uint8 remaincharges = fields[13].GetUInt8();
            float critChance = fields[14].GetFloat();
            bool applyResilience = fields[15].GetBool();

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid);
            if (!spellInfo)
            {
                TC_LOG_ERROR("entities.pet", "Unknown aura (spellid {}), ignore.", spellid);
                continue;
            }

            // 负面效果应在下线后继续倒计时
            if (remaintime != -1 && (!spellInfo->IsPositive() || spellInfo->HasAttribute(SPELL_ATTR4_FADES_WHILE_LOGGED_OUT)))
            {
                if (remaintime/IN_MILLISECONDS <= int32(timediff))
                    continue;

                remaintime -= timediff*IN_MILLISECONDS;
            }

            // 防止remaincharges值错误
            if (spellInfo->ProcCharges)
            {
                if (remaincharges <= 0 || remaincharges > spellInfo->ProcCharges)
                    remaincharges = spellInfo->ProcCharges;
            }
            else
                remaincharges = 0;

            AuraCreateInfo createInfo(spellInfo, effmask, this);
            createInfo
                .SetCasterGUID(caster_guid)
                .SetBaseAmount(baseDamage);

            if (Aura* aura = Aura::TryCreate(createInfo))
            {
                if (!aura->CanBeSaved())
                {
                    aura->Remove();
                    continue;
                }
                aura->SetLoadedState(maxduration, remaintime, remaincharges, stackcount, recalculatemask, critChance, applyResilience, &damage[0]);
                aura->ApplyForTargets();
                TC_LOG_DEBUG("entities.pet", "Added aura spellid {}, effectmask {}", spellInfo->Id, effmask);
            }
        }
        while (result->NextRow());
    }
}

/**
 * @brief 保存宠物光环到数据库
 *
 * 职责：将宠物的光环状态保存到数据库
 *
 * 参数：
 *   trans - 数据库事务
 *
 * 主要流程：
 *   1. 删除现有光环记录
 *   2. 遍历所有光环
 *   3. 保存可保存的光环数据
 */
void Pet::_SaveAuras(CharacterDatabaseTransaction trans)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_AURAS);
    stmt->setUInt32(0, m_charmInfo->GetPetNumber());
    trans->Append(stmt);

    for (AuraMap::const_iterator itr = m_ownedAuras.begin(); itr != m_ownedAuras.end(); ++itr)
    {
        // 检查光环是否需要保存
        if (!itr->second->CanBeSaved() || IsPetAura(itr->second))
            continue;

        Aura* aura = itr->second;

        int32 damage[MAX_SPELL_EFFECTS];
        int32 baseDamage[MAX_SPELL_EFFECTS];
        uint8 effMask = 0;
        uint8 recalculateMask = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (aura->GetEffect(i))
            {
                baseDamage[i] = aura->GetEffect(i)->GetBaseAmount();
                damage[i] = aura->GetEffect(i)->GetAmount();
                effMask |= (1<<i);
                if (aura->GetEffect(i)->CanBeRecalculated())
                    recalculateMask |= (1<<i);
            }
            else
            {
                baseDamage[i] = 0;
                damage[i] = 0;
            }
        }

        // don't save guid of caster in case we are caster of the spell - guid for pet is generated every pet load, so it won't match saved guid anyways
        ObjectGuid casterGUID = (itr->second->GetCasterGUID() == GetGUID()) ? ObjectGuid::Empty : itr->second->GetCasterGUID();

        uint8 index = 0;

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PET_AURA);
        stmt->setUInt32(index++, m_charmInfo->GetPetNumber());
        stmt->setUInt64(index++, casterGUID.GetRawValue());
        stmt->setUInt32(index++, itr->second->GetId());
        stmt->setUInt8(index++, effMask);
        stmt->setUInt8(index++, recalculateMask);
        stmt->setUInt8(index++, itr->second->GetStackAmount());
        stmt->setInt32(index++, damage[0]);
        stmt->setInt32(index++, damage[1]);
        stmt->setInt32(index++, damage[2]);
        stmt->setInt32(index++, baseDamage[0]);
        stmt->setInt32(index++, baseDamage[1]);
        stmt->setInt32(index++, baseDamage[2]);
        stmt->setInt32(index++, itr->second->GetMaxDuration());
        stmt->setInt32(index++, itr->second->GetDuration());
        stmt->setUInt8(index++, itr->second->GetCharges());
        stmt->setFloat(index++, itr->second->GetCritChance());
        stmt->setBool (index++, itr->second->CanApplyResilience());

        trans->Append(stmt);
    }
}

bool Pet::addSpell(uint32 spellId, ActiveStates active /*= ACT_DECIDE*/, PetSpellState state /*= PETSPELL_NEW*/, PetSpellType type /*= PETSPELL_NORMAL*/)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        // do pet spell book cleanup
        if (state == PETSPELL_UNCHANGED)                    // spell load case
        {
            TC_LOG_ERROR("entities.pet", "Pet::addSpell: Non-existed in SpellStore spell #{} request, deleting for all pets in `pet_spell`.", spellId);

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_INVALID_PET_SPELL);

            stmt->setUInt32(0, spellId);

            CharacterDatabase.Execute(stmt);
        }
        else
            TC_LOG_ERROR("entities.pet", "Pet::addSpell: Non-existed in SpellStore spell #{} request.", spellId);

        return false;
    }

    PetSpellMap::iterator itr = m_spells.find(spellId);
    if (itr != m_spells.end())
    {
        if (itr->second.state == PETSPELL_REMOVED)
            state = PETSPELL_CHANGED;
        else
        {
            if (state == PETSPELL_UNCHANGED && itr->second.state != PETSPELL_UNCHANGED)
            {
                // can be in case spell loading but learned at some previous spell loading
                itr->second.state = PETSPELL_UNCHANGED;

                if (active == ACT_ENABLED)
                    ToggleAutocast(spellInfo, true);
                else if (active == ACT_DISABLED)
                    ToggleAutocast(spellInfo, false);
            }

            return false;
        }
    }

    PetSpell newspell;
    newspell.state = state;
    newspell.type = type;

    if (active == ACT_DECIDE)                               // active was not used before, so we save it's autocast/passive state here
    {
        if (spellInfo->IsAutocastable())
            newspell.active = ACT_DISABLED;
        else
            newspell.active = ACT_PASSIVE;
    }
    else
        newspell.active = active;

    // talent: unlearn all other talent ranks (high and low)
    if (TalentSpellPos const* talentPos = GetTalentSpellPos(spellId))
    {
        if (TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentPos->talent_id))
        {
            for (uint32 rankSpellId : talentInfo->SpellRank)
            {
                // skip learning spell and no rank spell case
                if (!rankSpellId || rankSpellId == spellId)
                    continue;

                // skip unknown ranks
                if (!HasSpell(rankSpellId))
                    continue;
                removeSpell(rankSpellId, false, false);
            }
        }
    }
    else if (spellInfo->IsRanked())
    {
        for (PetSpellMap::const_iterator itr2 = m_spells.begin(); itr2 != m_spells.end(); ++itr2)
        {
            if (itr2->second.state == PETSPELL_REMOVED)
                continue;

            SpellInfo const* oldRankSpellInfo = sSpellMgr->GetSpellInfo(itr2->first);

            if (!oldRankSpellInfo)
                continue;

            if (spellInfo->IsDifferentRankOf(oldRankSpellInfo))
            {
                // replace by new high rank
                if (spellInfo->IsHighRankOf(oldRankSpellInfo))
                {
                    newspell.active = itr2->second.active;

                    if (newspell.active == ACT_ENABLED)
                        ToggleAutocast(oldRankSpellInfo, false);

                    unlearnSpell(itr2->first, false, false);
                    break;
                }
                // ignore new lesser rank
                else
                    return false;
            }
        }
    }

    m_spells[spellId] = newspell;

    if (spellInfo->IsPassive() && (!spellInfo->CasterAuraState || HasAuraState(AuraStateType(spellInfo->CasterAuraState))))
        CastSpell(this, spellId, true);
    else
        m_charmInfo->AddSpellToActionBar(spellInfo);

    if (newspell.active == ACT_ENABLED)
        ToggleAutocast(spellInfo, true);

    uint32 talentCost = GetTalentSpellCost(spellId);
    if (talentCost)
    {
        int32 free_points = GetMaxTalentPointsForLevel(GetLevel());
        m_usedTalentCount += talentCost;
        // update free talent points
        free_points -= m_usedTalentCount;
        SetFreeTalentPoints(free_points > 0 ? free_points : 0);
    }
    return true;
}

/**
 * @brief 学习技能
 *
 * 职责：让宠物学习一个新技能
 *
 * 参数：
 *   spell_id - 技能ID
 *
 * 返回值：学习成功返回true
 *
 * 调用时机：宠物升级、学习新技能时
 */
bool Pet::learnSpell(uint32 spell_id)
{
    // 防止技能书中出现重复条目
    if (!addSpell(spell_id))
        return false;

    if (!m_loading)
    {
        WorldPackets::Pet::PetLearnedSpell packet;
        packet.SpellID = spell_id;
        GetOwner()->SendDirectMessage(packet.Write());
        GetOwner()->PetSpellInitialize();
    }
    return true;
}

/**
 * @brief 初始化当前等级的升级技能
 *
 * 职责：根据宠物等级学习或遗忘升级技能
 *
 * 调用时机：宠物创建、升级、降级时
 *
 * 主要流程：
 *   1. 获取宠物家族的升级技能列表
 *   2. 反向遍历，如果等级不足则遗忘，否则学习
 *   3. 处理默认技能
 */
void Pet::InitLevelupSpellsForLevel()
{
    uint8 level = GetLevel();

    // 获取宠物家族的升级技能列表
    if (PetLevelupSpellSet const* levelupSpells = GetCreatureTemplate()->family ? sSpellMgr->GetPetLevelupSpellList(GetCreatureTemplate()->family) : nullptr)
    {
        // PetLevelupSpellSet按等级排序，按逆序处理
        for (PetLevelupSpellSet::const_reverse_iterator itr = levelupSpells->rbegin(); itr != levelupSpells->rend(); ++itr)
        {
            // 如果降级则首先调用
            if (itr->first > level)
                unlearnSpell(itr->second, true);  // 如果有前一等级则学习
            // 如果升级则调用
            else
                learnSpell(itr->second);  // 如果有前一等级则遗忘
        }
    }

    int32 petSpellsId = GetCreatureTemplate()->PetSpellDataId ? -(int32)GetCreatureTemplate()->PetSpellDataId : GetEntry();

    // 默认技能（如果宠物等级低于正常游戏中的第一个可能等级，则可能不被学习）
    if (PetDefaultSpellsEntry const* defSpells = sSpellMgr->GetPetDefaultSpellsEntry(petSpellsId))
    {
        for (uint32 spellId : defSpells->spellid)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
                continue;

            // 如果降级则首先调用
            if (spellInfo->SpellLevel > level)
                unlearnSpell(spellInfo->Id, true);
            // 如果升级则调用
            else
                learnSpell(spellInfo->Id);
        }
    }
}

/**
 * @brief 遗忘技能
 *
 * 职责：让宠物遗忘一个技能
 *
 * 参数：
 *   spell_id   - 技能ID
 *   learn_prev - 是否学习前一等级
 *   clear_ab   - 是否从动作条清除
 *
 * 返回值：遗忘成功返回true
 */
bool Pet::unlearnSpell(uint32 spell_id, bool learn_prev, bool clear_ab)
{
    if (removeSpell(spell_id, learn_prev, clear_ab))
    {
        if (!m_loading)
        {
            WorldPackets::Pet::PetUnlearnedSpell packet;
            packet.SpellID = spell_id;
            GetOwner()->SendDirectMessage(packet.Write());
        }
        return true;
    }
    return false;
}

/**
 * @brief 移除技能
 *
 * 职责：从宠物技能列表中移除一个技能
 *
 * 参数：
 *   spell_id   - 技能ID
 *   learn_prev - 是否学习前一等级
 *   clear_ab   - 是否从动作条清除
 *
 * 返回值：移除成功返回true
 *
 * 主要流程：
 *   1. 查找技能并标记为已移除
 *   2. 移除相关光环
 *   3. 更新天赋点数
 *   4. 可选学习前一等级技能
 */
bool Pet::removeSpell(uint32 spell_id, bool learn_prev, bool clear_ab)
{
    PetSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr == m_spells.end())
        return false;

    if (itr->second.state == PETSPELL_REMOVED)
        return false;

    if (itr->second.state == PETSPELL_NEW)
        m_spells.erase(itr);
    else
        itr->second.state = PETSPELL_REMOVED;

    RemoveAurasDueToSpell(spell_id);

    // 更新天赋点数
    uint32 talentCost = GetTalentSpellCost(spell_id);
    if (talentCost > 0)
    {
        if (m_usedTalentCount > talentCost)
            m_usedTalentCount -= talentCost;
        else
            m_usedTalentCount = 0;
        // 更新自由天赋点数
        int32 free_points = GetMaxTalentPointsForLevel(GetLevel()) - m_usedTalentCount;
        SetFreeTalentPoints(free_points > 0 ? free_points : 0);
    }

    // 如果需要，学习前一等级
    if (learn_prev)
    {
        if (uint32 prev_id = sSpellMgr->GetPrevSpellInChain (spell_id))
            learnSpell(prev_id);
        else
            learn_prev = false;
    }

    // 如果移除最后一级或非分级技能，则更新服务器和客户端的动作条
    if (clear_ab && !learn_prev && m_charmInfo->RemoveSpellFromActionBar(spell_id))
    {
        if (!m_loading)
            GetOwner()->PetSpellInitialize();  // 需要更新最后移除等级的动作条
    }

    return true;
}

/**
 * @brief 清理动作条
 *
 * 职责：移除动作条中宠物不再拥有的技能
 *
 * 调用时机：加载宠物技能后
 */
void Pet::CleanupActionBar()
{
    for (uint8 i = 0; i < MAX_UNIT_ACTION_BAR_INDEX; ++i)
        if (UnitActionBarEntry const* ab = m_charmInfo->GetActionBarEntry(i))
            if (ab->GetAction() && ab->IsActionBarForSpell())
            {
                if (!HasSpell(ab->GetAction()))
                    m_charmInfo->SetActionBar(i, 0, ACT_PASSIVE);
                else if (ab->GetType() == ACT_ENABLED)
                {
                    if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(ab->GetAction()))
                        ToggleAutocast(spellInfo, true);
                }
            }
}

/**
 * @brief 初始化宠物创建时的技能
 *
 * 职责：为新创建的宠物初始化技能列表和动作条
 *
 * 调用时机：宠物首次创建时
 *
 * 主要流程：
 *   1. 初始化宠物动作条
 *   2. 清空技能列表
 *   3. 学习被动技能
 *   4. 学习升级技能
 *   5. 施放宠物光环
 */
void Pet::InitPetCreateSpells()
{
    m_charmInfo->InitPetActionBar();
    m_spells.clear();

    LearnPetPassives();
    InitLevelupSpellsForLevel();

    CastPetAuras(false);
}

bool Pet::resetTalents(bool involuntarily /*= false*/)
{
    Player* player = GetOwner();

    // not need after this call
    if (player->HasAtLoginFlag(AT_LOGIN_RESET_PET_TALENTS))
        player->RemoveAtLoginFlag(AT_LOGIN_RESET_PET_TALENTS, true);

    CreatureTemplate const* ci = GetCreatureTemplate();
    if (!ci)
        return false;
    // Check pet talent type
    CreatureFamilyEntry const* pet_family = sCreatureFamilyStore.LookupEntry(ci->family);
    if (!pet_family || pet_family->PetTalentType < 0)
        return false;

    uint8 level = GetLevel();
    uint32 talentPointsForLevel = GetMaxTalentPointsForLevel(level);

    if (m_usedTalentCount == 0)
    {
        SetFreeTalentPoints(talentPointsForLevel);
        return false;
    }

    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(i);

        if (!talentInfo)
            continue;

        TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TabID);

        if (!talentTabInfo)
            continue;

        // unlearn only talents for pets family talent type
        if (!((1 << pet_family->PetTalentType) & talentTabInfo->PetTalentMask))
            continue;

        for (uint32 talentSpellId : talentInfo->SpellRank)
        {
            for (PetSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end();)
            {
                if (itr->second.state == PETSPELL_REMOVED)
                {
                    ++itr;
                    continue;
                }
                // remove learned spells (all ranks)
                uint32 itrFirstId = sSpellMgr->GetFirstSpellInChain(itr->first);

                // unlearn if first rank is talent or learned by talent
                if (itrFirstId == talentSpellId || sSpellMgr->IsSpellLearnToSpell(talentSpellId, itrFirstId))
                {
                    unlearnSpell(itr->first, false);
                    itr = m_spells.begin();
                    continue;
                }
                else
                    ++itr;
            }
        }
    }

    SetFreeTalentPoints(talentPointsForLevel);

    if (!m_loading)
        player->PetSpellInitialize();

    if (involuntarily)
        player->SendDirectMessage(WorldPackets::Talents::InvoluntarilyReset(true).Write());

    return true;
}

void Pet::resetTalentsForAllPetsOf(Player* owner, Pet* onlinePet /*= nullptr*/, bool involuntarily /*= false*/)
{
    // not need after this call
    if (owner->HasAtLoginFlag(AT_LOGIN_RESET_PET_TALENTS))
        owner->RemoveAtLoginFlag(AT_LOGIN_RESET_PET_TALENTS, true);

    // reset for online
    if (onlinePet)
        onlinePet->resetTalents(involuntarily);

    PetStable* petStable = owner->GetPetStable();
    if (!petStable)
        return;

    std::unordered_set<uint32> petIds;
    if (petStable->CurrentPet)
        petIds.insert(petStable->CurrentPet->PetNumber);

    for (Optional<PetStable::PetInfo> const& stabledPet : petStable->StabledPets)
        if (stabledPet)
            petIds.insert(stabledPet->PetNumber);

    for (PetStable::PetInfo const& unslottedPet : petStable->UnslottedPets)
        petIds.insert(unslottedPet.PetNumber);

    // now need only reset for offline pets (all pets except online case)
    if (onlinePet)
        petIds.erase(onlinePet->GetCharmInfo()->GetPetNumber());

    // no offline pets
    if (petIds.empty())
        return;

    if (!onlinePet)
        owner->SendDirectMessage(WorldPackets::Talents::InvoluntarilyReset(true).Write());

    bool need_comma = false;
    std::ostringstream ss;
    ss << "DELETE FROM pet_spell WHERE guid IN (";

    for (uint32 id : petIds)
    {
        if (need_comma)
            ss << ',';

        ss << id;

        need_comma = true;
    }

    ss << ") AND spell IN (";

    need_comma = false;
    for (uint32 spell : sPetTalentSpells)
    {
        if (need_comma)
            ss << ',';

        ss << spell;

        need_comma = true;
    }

    ss << ')';

    CharacterDatabase.Execute(ss.str().c_str());
}

/**
 * @brief 初始化当前等级的天赋
 *
 * 职责：根据宠物等级设置可用天赋点数
 *
 * 调用时机：宠物创建、升级、学习/遗忘天赋时
 */
void Pet::InitTalentForLevel()
{
    uint8 level = GetLevel();
    uint32 talentPointsForLevel = GetMaxTalentPointsForLevel(level);
    // 如果低等级（降级）或天赋点数错误（猎人可以遗忘TP增加天赋），重置天赋
    if (talentPointsForLevel == 0 || m_usedTalentCount > talentPointsForLevel)
        resetTalents();  // 移除所有天赋点

    SetFreeTalentPoints(talentPointsForLevel - m_usedTalentCount);

    if (!m_loading)
        GetOwner()->SendTalentsInfoData(true);
}

/**
 * @brief 获取指定等级的最大天赋点数
 *
 * 职责：计算宠物在指定等级可用的天赋点数
 *
 * 参数：
 *   level - 宠物等级
 *
 * 返回值：该等级可用的天赋点数
 *
 * 说明：宠物从20级开始获得天赋点，每4级获得1点
 */
uint8 Pet::GetMaxTalentPointsForLevel(uint8 level) const
{
    uint8 points = (level >= 20) ? ((level - 16) / 4) : 0;
    // 主人的SPELL_AURA_MOD_PET_TALENT_POINTS光环修正
    points += GetOwner()->GetTotalAuraModifier(SPELL_AURA_MOD_PET_TALENT_POINTS);
    return points;
}

/**
 * @brief 切换自动施法状态
 *
 * 职责：开启或关闭技能的自动施法
 *
 * 参数：
 *   spellInfo - 技能信息
 *   apply     - true开启，false关闭
 */
void Pet::ToggleAutocast(SpellInfo const* spellInfo, bool apply)
{
    ASSERT(spellInfo);

    if (!spellInfo->IsAutocastable())
        return;

    PetSpellMap::iterator itr = m_spells.find(spellInfo->Id);
    if (itr == m_spells.end())
        return;

    auto autospellItr = std::find(m_autospells.begin(), m_autospells.end(), spellInfo->Id);

    if (apply)
    {
        if (autospellItr == m_autospells.end())
        {
            m_autospells.push_back(spellInfo->Id);

            if (itr->second.active != ACT_ENABLED)
            {
                itr->second.active = ACT_ENABLED;
                if (itr->second.state != PETSPELL_NEW)
                    itr->second.state = PETSPELL_CHANGED;
            }
        }
    }
    else
    {
        if (autospellItr != m_autospells.end())
        {
            m_autospells.erase(autospellItr);

            if (itr->second.active != ACT_DISABLED)
            {
                itr->second.active = ACT_DISABLED;
                if (itr->second.state != PETSPELL_NEW)
                    itr->second.state = PETSPELL_CHANGED;
            }
        }
    }
}

/**
 * @brief 检查是否为永久宠物
 *
 * 职责：判断宠物是否为玩家的永久宠物
 *
 * 参数：
 *   owner - 宠物主人
 *
 * 返回值：是永久宠物返回true
 *
 * 说明：
 *   - 猎人宠物始终是永久宠物
 *   - 术士的恶魔是永久宠物
 *   - 死亡骑士的不死生物是永久宠物
 */
bool Pet::IsPermanentPetFor(Player* owner) const
{
    switch (getPetType())
    {
        case SUMMON_PET:
            switch (owner->GetClass())
            {
                case CLASS_WARLOCK:
                    return GetCreatureTemplate()->type == CREATURE_TYPE_DEMON;
                case CLASS_DEATH_KNIGHT:
                    return GetCreatureTemplate()->type == CREATURE_TYPE_UNDEAD;
                default:
                    return false;
            }
        case HUNTER_PET:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 创建宠物对象
 *
 * 职责：初始化宠物对象的基础数据
 *
 * 参数：
 *   guidlow   - 低GUID
 *   map       - 所在地图
 *   phaseMask - 相位掩码
 *   Entry     - 生物条目ID
 *   petId     - 宠物编号
 *
 * 返回值：创建成功返回true
 *
 * 主要流程：
 *   1. 设置地图和相位
 *   2. 创建对象GUID
 *   3. 初始化生物条目
 *   4. 设置单位标志
 *   5. 初始化威胁管理器
 */
bool Pet::Create(ObjectGuid::LowType guidlow, Map* map, uint32 phaseMask, uint32 Entry, uint32 petId)
{
    ASSERT(map);
    SetMap(map);

    SetPhaseMask(phaseMask, false);
    Object::_Create(guidlow, petId, HighGuid::Pet);

    m_originalEntry = Entry;

    if (!InitEntry(Entry))
        return false;

    // 强制玩家宠物的能量恢复标志，就像玩家自己一样
    SetUnitFlag2(UNIT_FLAG2_REGENERATE_POWER);
    SetSheath(SHEATH_STATE_MELEE);

    GetThreatManager().Initialize();

    return true;
}

/**
 * @brief 检查宠物是否拥有技能
 *
 * 职责：检查宠物技能列表中是否包含指定技能
 *
 * 参数：
 *   spell - 技能ID
 *
 * 返回值：拥有技能返回true
 */
bool Pet::HasSpell(uint32 spell) const
{
    PetSpellMap::const_iterator itr = m_spells.find(spell);
    return itr != m_spells.end() && itr->second.state != PETSPELL_REMOVED;
}

/**
 * @brief 学习宠物家族被动技能
 *
 * 职责：学习宠物家族技能线中的所有被动技能
 *
 * 调用时机：宠物创建时
 *
 * 说明：包括通用猎人宠物技能270、被动技能01-10、凶猛灵感等
 */
void Pet::LearnPetPassives()
{
    CreatureTemplate const* cInfo = GetCreatureTemplate();
    if (!cInfo)
        return;

    CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(cInfo->family);
    if (!cFamily)
        return;

    PetFamilySpellsStore::const_iterator petStore = sPetFamilySpellsStore.find(cFamily->ID);
    if (petStore != sPetFamilySpellsStore.end())
    {
        // 通用猎人宠物技能270
        // 被动技能01~10、被动技能00（20782，未使用）、凶猛灵感（34457）
        // 缩放01~03（34902~34904，来自主人的加成，未使用）
        for (uint32 spellId : petStore->second)
            addSpell(spellId, ACT_DECIDE, PETSPELL_NEW, PETSPELL_FAMILY);
    }
}

/**
 * @brief 施放宠物光环
 *
 * 职责：为永久宠物施放主人身上的宠物光环
 *
 * 参数：
 *   current - 是否为当前宠物
 *
 * 调用时机：宠物加载、召唤时
 */
void Pet::CastPetAuras(bool current)
{
    Player* owner = GetOwner();

    if (!IsPermanentPetFor(owner))
        return;

    for (auto itr = owner->m_petAuras.begin(); itr != owner->m_petAuras.end();)
    {
        PetAura const* pa = *itr;
        ++itr;

        if (!current && pa->IsRemovedOnChangePet())
            owner->RemovePetAura(pa);
        else
            CastPetAura(pa);
    }
}

/**
 * @brief 施放宠物光环
 *
 * 职责：为宠物施放指定的光环
 *
 * 参数：
 *   aura - 宠物光环数据
 */
void Pet::CastPetAura(PetAura const* aura)
{
    uint32 auraId = aura->GetAura(GetEntry());
    if (!auraId)
        return;

    CastSpellExtraArgs args;
    args.TriggerFlags = TRIGGERED_FULL_MASK;

    // 恶魔知识：基于耐力和智力计算加成
    if (auraId == 35696)
        args.AddSpellMod(SPELLVALUE_BASE_POINT0, CalculatePct(aura->GetDamage(), GetStat(STAT_STAMINA) + GetStat(STAT_INTELLECT)));

    CastSpell(this, auraId, args);
}

/**
 * @brief 检查光环是否为宠物光环
 *
 * 职责：判断指定光环是否为主人拥有的宠物光环
 *
 * 参数：
 *   aura - 光环对象
 *
 * 返回值：是宠物光环返回true
 */
bool Pet::IsPetAura(Aura const* aura)
{
    Player* owner = GetOwner();

    // 如果主人有该宠物光环，返回true
    for (PetAura const* petAura : owner->m_petAuras)
        if (petAura->GetAura(GetEntry()) == aura->GetId())
            return true;

    return false;
}

/**
 * @brief 学习高等级技能及其所有后续等级
 *
 * 职责：递归学习技能链中的所有等级
 *
 * 参数：
 *   spellid - 起始技能ID
 *
 * 调用时机：需要学习完整技能链时
 */
void Pet::learnSpellHighRank(uint32 spellid)
{
    learnSpell(spellid);

    if (uint32 next = sSpellMgr->GetNextSpellInChain(spellid))
        learnSpellHighRank(next);
}

/**
 * @brief 与主人等级同步
 *
 * 职责：根据宠物类型同步宠物等级与主人等级
 *
 * 调用时机：宠物加载时
 *
 * 说明：
 *   - 召唤宠物：始终与主人等级相同
 *   - 猎人宠物：不超过主人等级，如果落后超过5级则提升到主人等级-5
 */
void Pet::SynchronizeLevelWithOwner()
{
    Player* owner = GetOwner();

    switch (getPetType())
    {
        // 始终与主人等级相同
        case SUMMON_PET:
            GivePetLevel(owner->GetLevel());
            break;
        // 不能超过主人等级
        case HUNTER_PET:
            if (GetLevel() > owner->GetLevel())
                GivePetLevel(owner->GetLevel());
            else if (GetLevel() + 5 < owner->GetLevel())
                GivePetLevel(owner->GetLevel() - 5);
            break;
        default:
            break;
    }
}

/**
 * @brief 获取宠物主人
 *
 * 职责：返回宠物的主人（玩家对象）
 *
 * 返回值：玩家指针
 */
Player* Pet::GetOwner() const
{
    return Minion::GetOwner()->ToPlayer();
}

/**
 * @brief 获取原生对象缩放比例
 *
 * 职责：计算宠物的原生缩放比例
 *
 * 返回值：缩放比例
 *
 * 说明：猎人宠物根据等级和生物家族计算缩放
 */
float Pet::GetNativeObjectScale() const
{
    CreatureFamilyEntry const* creatureFamily = sCreatureFamilyStore.LookupEntry(GetCreatureTemplate()->family);
    if (creatureFamily && creatureFamily->MinScale > 0.0f && getPetType() == HUNTER_PET)
    {
        float scale;
        if (GetLevel() >= creatureFamily->MaxScaleLevel)
            scale = creatureFamily->MaxScale;
        else if (GetLevel() <= creatureFamily->MinScaleLevel)
            scale = creatureFamily->MinScale;
        else
            scale = creatureFamily->MinScale + float(GetLevel() - creatureFamily->MinScaleLevel) / creatureFamily->MaxScaleLevel * (creatureFamily->MaxScale - creatureFamily->MinScale);

        return scale;
    }

    return Guardian::GetNativeObjectScale();
}

/**
 * @brief 设置显示ID
 *
 * 职责：设置宠物的模型显示ID并通知队伍更新
 *
 * 参数：
 *   modelId - 模型ID
 */
void Pet::SetDisplayId(uint32 modelId)
{
    Guardian::SetDisplayId(modelId);

    if (!isControlled())
        return;

    // 如果主人在队伍中，设置队伍更新标志
    if (GetOwner()->GetGroup())
        GetOwner()->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_MODEL_ID);
}

/**
 * @brief 生成动作条数据字符串
 *
 * 职责：将宠物动作条序列化为字符串用于数据库保存
 *
 * 返回值：动作条数据字符串
 */
std::string Pet::GenerateActionBarData() const
{
    std::ostringstream oss;

    for (uint32 i = ACTION_BAR_INDEX_START; i < ACTION_BAR_INDEX_END; ++i)
    {
        oss << uint32(m_charmInfo->GetActionBarEntry(i)->GetType()) << ' '
            << uint32(m_charmInfo->GetActionBarEntry(i)->GetAction()) << ' ';
    }

    return oss.str();
}

/**
 * @brief 获取调试信息
 *
 * 职责：返回宠物的调试信息字符串
 *
 * 返回值：调试信息字符串
 */
std::string Pet::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Guardian::GetDebugInfo() << "\n"
        << std::boolalpha
        << "PetType: " << std::to_string(getPetType()) << " "
        << "PetNumber: " << m_charmInfo->GetPetNumber();
    return sstr.str();
}
