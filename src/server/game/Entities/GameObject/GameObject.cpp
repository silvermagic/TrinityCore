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
 * @file GameObject.cpp
 * @brief 游戏对象(GameObject)核心实现模块
 *
 * 本文件实现了游戏世界中所有静态和动态游戏对象的核心功能，包括：
 * - 游戏对象的生命周期管理（创建、更新、销毁）
 * - 游戏对象状态机（就绪、激活、失活等状态转换）
 * - 游戏对象的交互系统（使用、打开、触发等）
 * - 战利品系统（宝箱、钓鱼洞等）
 * - 陷阱和触发器机制
 * - 门、按钮等交互对象
 * - 运输工具（船只、飞艇等）
 * - 可破坏建筑
 * - 碰撞检测和物理模型管理
 * - 数据持久化（保存到数据库）
 * - AI系统集成
 *
 * 游戏对象类型包括：
 * - GAMEOBJECT_TYPE_DOOR: 门（可开关）
 * - GAMEOBJECT_TYPE_BUTTON: 按钮
 * - GAMEOBJECT_TYPE_QUESTGIVER: 任务给予者
 * - GAMEOBJECT_TYPE_CHEST: 宝箱
 * - GAMEOBJECT_TYPE_TRAP: 陷阱
 * - GAMEOBJECT_TYPE_TRANSPORT: 运输工具
 * - GAMEOBJECT_TYPE_GOOBER: 通用交互对象
 * - GAMEOBJECT_TYPE_FISHINGNODE: 钓鱼节点
 * - GAMEOBJECT_TYPE_FISHINGHOLE: 钓鱼洞
 * - GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING: 可破坏建筑
 * 等多种类型
 *
 * 核心架构：
 * - 继承自WorldObject: 提供世界坐标、相位等基础属性
 * - 继承自MapObject: 提供地图注册和网格管理
 * - 组合AI系统: 处理复杂行为逻辑
 * - 组合碰撞模型: 提供物理碰撞检测
 *
 * @see GameObject.h
 * @see GameObjectTemplate
 * @see GameObjectAI
 */

#include "GameObject.h"
#include "Battleground.h"
#include "CellImpl.h"
#include "Containers.h"
#include "CreatureAISelector.h"
#include "DatabaseEnv.h"
#include "GameObjectAI.h"
#include "GameObjectModel.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "PoolMgr.h"
#include "QueryPackets.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "Transport.h"
#include "UpdateFieldFlags.h"
#include "World.h"
#include <G3D/Box.h>
#include <G3D/CoordinateFrame.h>
#include <G3D/Quat.h>

/**
 * @brief 初始化查询数据
 *
 * 职责：
 *   为所有支持的语言环境初始化游戏对象模板的查询数据
 *
 * 主要流程：
 *   遍历所有语言环境，为每个语言构建查询响应包
 *
 * 调用时机：
 *   游戏对象模板加载时调用
 */
void GameObjectTemplate::InitializeQueryData()
{
    for (uint8 loc = LOCALE_enUS; loc < TOTAL_LOCALES; ++loc)
        QueryData[loc] = BuildQueryData(static_cast<LocaleConstant>(loc));
}

/**
 * @brief 构建查询数据包
 *
 * 职责：
 *   为指定语言环境构建游戏对象模板查询响应包
 *
 * 参数：
 *   @param loc 语言环境常量
 *
 * 返回值：
 *   @return WorldPacket 查询响应包
 *
 * 主要流程：
 *   1. 获取本地化名称和施法条标题
 *   2. 填充查询响应结构
 *   3. 添加任务物品列表
 *   4. 构建并返回数据包
 *
 * 调用时机：
 *   客户端查询游戏对象模板信息时调用
 */
WorldPacket GameObjectTemplate::BuildQueryData(LocaleConstant loc) const
{
    WorldPackets::Query::QueryGameObjectResponse queryTemp;

    std::string locName = name;
    std::string locIconName = IconName;
    std::string locCastBarCaption = castBarCaption;

    if (GameObjectLocale const* gameObjectLocale = sObjectMgr->GetGameObjectLocale(entry))
    {
        ObjectMgr::GetLocaleString(gameObjectLocale->Name, loc, locName);
        ObjectMgr::GetLocaleString(gameObjectLocale->CastBarCaption, loc, locCastBarCaption);
    }

    queryTemp.GameObjectID = entry;
    queryTemp.Allow = true;

    queryTemp.Stats.Type = type;
    queryTemp.Stats.DisplayID = displayId;
    queryTemp.Stats.Name = locName;
    queryTemp.Stats.IconName = locIconName;
    queryTemp.Stats.CastBarCaption = locCastBarCaption;
    queryTemp.Stats.UnkString = unk1;
    memcpy(queryTemp.Stats.Data, raw.data, sizeof(uint32) * MAX_GAMEOBJECT_DATA);
    queryTemp.Stats.Size = size;

    for (uint32 i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; ++i)
        queryTemp.Stats.QuestItems[i] = 0;

    if (std::vector<uint32> const* items = sObjectMgr->GetGameObjectQuestItemList(entry))
        for (uint32 i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; ++i)
            if (i < items->size())
                queryTemp.Stats.QuestItems[i] = (*items)[i];

    queryTemp.Write();
    queryTemp.ShrinkToFit();
    return queryTemp.Move();
}

/**
 * @brief 检查四元数是否为单位四元数
 *
 * 职责：
 *   验证四元数的模是否接近1（单位四元数）
 *
 * 返回值：
 *   @return bool 是单位四元数返回true，否则返回false
 *
 * 数学原理：
 *   单位四元数满足：x² + y² + z² + w² = 1
 *   允许误差范围：1e-5
 */
bool QuaternionData::isUnit() const
{
    return fabs(x * x + y * y + z * z + w * w - 1.0f) < 1e-5f;
}

/**
 * @brief 将四元数转换为欧拉角（ZYX顺序）
 *
 * 职责：
 *   将四元数旋转表示转换为欧拉角表示
 *
 * 参数：
 *   @param Z 输出的Z轴旋转角度（偏航）
 *   @param Y 输出的Y轴旋转角度（俯仰）
 *   @param X 输出的X轴旋转角度（翻滚）
 */
void QuaternionData::toEulerAnglesZYX(float& Z, float& Y, float& X) const
{
    G3D::Matrix3(G3D::Quat(x, y, z, w)).toEulerAnglesZYX(Z, Y, X);
}

/**
 * @brief 从欧拉角创建四元数（ZYX顺序）
 *
 * 职责：
 *   从欧拉角旋转表示创建四元数表示
 *
 * 参数：
 *   @param Z Z轴旋转角度（偏航）
 *   @param Y Y轴旋转角度（俯仰）
 *   @param X X轴旋转角度（翻滚）
 *
 * 返回值：
 *   @return QuaternionData 生成的四元数
 */
QuaternionData QuaternionData::fromEulerAnglesZYX(float Z, float Y, float X)
{
    G3D::Quat quat(G3D::Matrix3::fromEulerAnglesZYX(Z, Y, X));
    return QuaternionData(quat.x, quat.y, quat.z, quat.w);
}

/**
 * @brief GameObject构造函数
 *
 * 职责：
 *   初始化游戏对象的所有成员变量，设置对象类型和基础属性
 *
 * 初始化内容：
 *   - 设置对象类型为TYPEMASK_GAMEOBJECT
 *   - 初始化更新标志（低GUID、静止位置、位置、旋转）
 *   - 初始化重生时间和延迟（默认300秒）
 *   - 设置初始战利品状态为GO_NOT_READY
 *   - 重置战利品模式为默认模式
 *   - 初始化静止位置为原点
 *
 * 调用时机：
 *   创建新的GameObject实例时自动调用
 */
GameObject::GameObject() : WorldObject(false), MapObject(),
    m_model(nullptr), m_goValue(), m_AI(nullptr), m_respawnCompatibilityMode(false)
{
    m_objectType |= TYPEMASK_GAMEOBJECT;
    m_objectTypeId = TYPEID_GAMEOBJECT;

    m_updateFlag = (UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_POSITION | UPDATEFLAG_ROTATION);

    m_valuesCount = GAMEOBJECT_END;
    m_respawnTime = 0;
    m_respawnDelayTime = 300;
    m_despawnDelay = 0;
    m_despawnRespawnTime = 0s;
    m_restockTime = 0;
    m_lootState = GO_NOT_READY;
    m_spawnedByDefault = true;
    m_usetimes = 0;
    m_spellId = 0;
    m_cooldownTime = 0;
    m_prevGoState = GO_STATE_ACTIVE;
    m_goInfo = nullptr;
    m_goTemplateAddon = nullptr;
    m_goData = nullptr;
    m_packedRotation = 0;

    m_spawnId = 0;

    m_lootRecipientGroup = 0;
    m_groupLootTimer = 0;
    lootingGroupLowGUID = 0;
    m_lootGenerationTime = 0;

    ResetLootMode(); // restore default loot mode
    m_stationaryPosition.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
}

/**
 * @brief GameObject析构函数
 *
 * 职责：
 *   清理游戏对象占用的所有资源
 *
 * 清理内容：
 *   - 删除AI实例（如果存在）
 *   - 删除碰撞模型（如果存在）
 *   - 注意：字段数组清理由CleanupsBeforeDelete处理
 *
 * 注意事项：
 *   析构函数中不应调用虚函数，避免派生类已析构时调用错误实现
 */
GameObject::~GameObject()
{
    delete m_AI;
    delete m_model;
    //if (m_uint32Values)                                      // field array can be not exist if GameOBject not loaded
    //    CleanupsBeforeDelete();
}

/**
 * @brief 销毁游戏对象AI
 *
 * 职责：
 *   删除AI实例并清空指针
 *
 * 调用时机：
 *   重新初始化AI前、对象销毁时
 */
void GameObject::AIM_Destroy()
{
    delete m_AI;
    m_AI = nullptr;
}

/**
 * @brief 初始化游戏对象AI
 *
 * 职责：
 *   为游戏对象创建并初始化AI实例
 *
 * 返回值：
 *   @return bool 初始化成功返回true，失败返回false
 *
 * 主要流程：
 *   1. 销毁现有AI实例
 *   2. 通过工厂选择器创建合适的AI
 *   3. 调用AI的InitializeAI方法
 *
 * AI选择逻辑：
 *   根据游戏对象模板中的AIName字段选择对应的AI类
 *   如果未指定，使用默认的NullGameObjectAI
 *
 * 调用时机：
 *   游戏对象创建时、Update中检测到AI缺失时
 */
bool GameObject::AIM_Initialize()
{
    AIM_Destroy();

    m_AI = FactorySelector::SelectGameObjectAI(this);

    if (!m_AI)
        return false;

    m_AI->InitializeAI();
    return true;
}

/**
 * @brief 获取AI名称
 *
 * 职责：
 *   返回游戏对象模板中定义的AI名称字符串
 *
 * 返回值：
 *   @return std::string const& AI名称的常量引用
 *
 * 用途：
 *   用于AI工厂选择合适的AI类型
 */
std::string const& GameObject::GetAIName() const
{
    return sObjectMgr->GetGameObjectTemplate(GetEntry())->AIName;
}

/**
 * @brief 删除前清理
 *
 * 职责：
 *   在游戏对象被删除前执行必要的清理工作
 *
 * 参数：
 *   @param finalCleanup 是否为最终清理
 *
 * 主要流程：
 *   1. 调用父类清理方法
 *   2. 如果字段数组存在，从所有者处移除
 *
 * 调用时机：
 *   对象析构函数中调用，确保资源正确释放
 */
void GameObject::CleanupsBeforeDelete(bool finalCleanup)
{
    WorldObject::CleanupsBeforeDelete(finalCleanup);

    if (m_uint32Values)                                      // field array can be not exist if GameOBject not loaded
        RemoveFromOwner();
}

/**
 * @brief 从所有者处移除游戏对象
 *
 * 职责：
 *   清除游戏对象与所有者的关联关系
 *
 * 主要流程：
 *   1. 获取所有者GUID，如果不存在则直接返回
 *   2. 查找所有者单位对象
 *   3. 如果找到所有者，从其游戏对象列表中移除
 *   4. 如果找不到所有者（可能已离线或传送），记录调试日志并清除GUID
 *
 * 常见场景：
 *   - 法师传送门使用后消失
 *   - 陷阱触发后消失
 *   - 图腾被销毁时
 *
 * 调用时机：
 *   游戏对象删除、消失或清理时调用
 */
void GameObject::RemoveFromOwner()
{
    ObjectGuid ownerGUID = GetOwnerGUID();
    if (!ownerGUID)
        return;

    if (Unit* owner = ObjectAccessor::GetUnit(*this, ownerGUID))
    {
        owner->RemoveGameObject(this, false);
        ASSERT(!GetOwnerGUID());
        return;
    }

    // This happens when a mage portal is despawned after the caster changes map (for example using the portal)
    TC_LOG_DEBUG("misc", "Removed GameObject ({} Entry: {} SpellId: {} LinkedGO: {}) that just lost any reference to the owner ({}) GO list",
        GetGUID().ToString(), GetGOInfo()->entry, m_spellId, GetGOInfo()->GetLinkedGameObjectEntry(), ownerGUID.ToString());
    SetOwnerGUID(ObjectGuid::Empty);
}

/**
 * @brief 将游戏对象添加到世界
 *
 * 职责：
 *   注册游戏对象到世界的各种查找表中，并启用碰撞检测
 *
 * 主要流程：
 *   1. 检查是否已在世界中（避免重复添加）
 *   2. 触发区域脚本的OnGameObjectCreate事件
 *   3. 将对象插入到地图的对象存储和生成ID存储中
 *   4. 处理碰撞模型的添加（运输工具有延迟添加机制）
 *   5. 根据对象状态启用或禁用碰撞
 *   6. 调用父类AddToWorld完成基础注册
 *
 * 调用时机：
 *   当游戏对象被创建并需要进入游戏世界时调用
 *   通常在Create()成功后调用
 */
void GameObject::AddToWorld()
{
    ///- Register the gameobject for guid lookup
    if (!IsInWorld())
    {
        if (m_zoneScript)
            m_zoneScript->OnGameObjectCreate(this);

        GetMap()->GetObjectsStore().Insert<GameObject>(GetGUID(), this);
        if (m_spawnId)
            GetMap()->GetGameObjectBySpawnIdStore().insert(std::make_pair(m_spawnId, this));

        // The state can be changed after GameObject::Create but before GameObject::AddToWorld
        bool toggledState = GetGoType() == GAMEOBJECT_TYPE_CHEST ? getLootState() == GO_READY : (GetGoState() == GO_STATE_READY || IsTransport());
        if (m_model)
        {
            if (Transport* trans = ToTransport())
                trans->SetDelayedAddModelToMap();
            else
                GetMap()->InsertGameObjectModel(*m_model);
        }

        EnableCollision(toggledState);
        WorldObject::AddToWorld();
    }
}

/**
 * @brief 将游戏对象从世界中移除
 *
 * 职责：
 *   从世界的各种查找表中注销游戏对象，清理碰撞模型和关联对象
 *
 * 主要流程：
 *   1. 检查是否在世界中（避免重复移除）
 *   2. 触发区域脚本的OnGameObjectRemove事件
 *   3. 从所有者处移除（清理所有者引用）
 *   4. 从地图中移除碰撞模型
 *   5. 让关联的陷阱消失
 *   6. 调用父类RemoveFromWorld完成基础注销
 *   7. 从生成ID存储和对象存储中移除
 *
 * 调用时机：
 *   当游戏对象需要从游戏世界中移除时调用
 *   通常在对象销毁、消失或卸载时调用
 */
void GameObject::RemoveFromWorld()
{
    ///- Remove the gameobject from the accessor
    if (IsInWorld())
    {
        if (m_zoneScript)
            m_zoneScript->OnGameObjectRemove(this);

        RemoveFromOwner();
        if (m_model)
            if (GetMap()->ContainsGameObjectModel(*m_model))
                GetMap()->RemoveGameObjectModel(*m_model);

        // If linked trap exists, despawn it
        if (GameObject* linkedTrap = GetLinkedTrap())
            linkedTrap->DespawnOrUnsummon();

        WorldObject::RemoveFromWorld();

        if (m_spawnId)
            Trinity::Containers::MultimapErasePair(GetMap()->GetGameObjectBySpawnIdStore(), m_spawnId, this);
        GetMap()->GetObjectsStore().Remove<GameObject>(GetGUID());
    }
}

/**
 * @brief 创建游戏对象
 *
 * 职责：
 *   初始化游戏对象的完整属性，包括位置、旋转、模板信息、AI等
 *
 * 参数：
 *   @param guidlow     对象的低GUID值
 *   @param name_id     游戏对象模板ID（entry）
 *   @param map         所属地图指针
 *   @param phaseMask   相位掩码
 *   @param pos         初始位置
 *   @param rotation    四元数旋转数据
 *   @param animprogress 动画进度
 *   @param go_state    初始游戏对象状态
 *   @param artKit      外观套装ID（默认0）
 *   @param dynamic     是否使用动态生成模式（默认false）
 *   @param spawnid     生成ID（默认0）
 *
 * 返回值：
 *   @return bool 创建成功返回true，失败返回false
 *
 * 主要流程：
 *   1. 验证地图和位置有效性
 *   2. 设置相位掩码和位置数据
 *   3. 获取并验证游戏对象模板
 *   4. 设置旋转和缩放
 *   5. 根据类型初始化特定属性（陷阱、运输工具、可破坏建筑等）
 *   6. 初始化AI和碰撞模型
 *   7. 创建关联的链接陷阱（如果存在）
 *
 * 调用时机：
 *   当需要创建新的游戏对象实例时调用，包括从数据库加载或动态生成
 */
bool GameObject::Create(ObjectGuid::LowType guidlow, uint32 name_id, Map* map, uint32 phaseMask, Position const& pos, QuaternionData const& rotation, uint32 animprogress, GOState go_state, uint32 artKit /*= 0*/, bool dynamic, ObjectGuid::LowType spawnid)
{
    ASSERT(map);
    SetMap(map);

    Relocate(pos);
    m_stationaryPosition.Relocate(pos);
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("misc", "Gameobject (GUID: {} Entry: {}) not created. Suggested coordinates isn't valid (X: {} Y: {})", guidlow, name_id, pos.GetPositionX(), pos.GetPositionY());
        return false;
    }

    // Set if this object can handle dynamic spawns
    if (!dynamic)
        SetRespawnCompatibilityMode();

    SetPhaseMask(phaseMask, false);
    UpdatePositionData();

    SetZoneScript();
    if (m_zoneScript)
    {
        name_id = m_zoneScript->GetGameObjectEntry(guidlow, name_id);
        if (!name_id)
            return false;
    }

    GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(name_id);
    if (!goinfo)
    {
        TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {} Entry: {}) not created: non-existing entry in `gameobject_template`. Map: {} (X: {} Y: {} Z: {})", guidlow, name_id, map->GetId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
        return false;
    }

    if (goinfo->type == GAMEOBJECT_TYPE_MO_TRANSPORT)
    {
        TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {} Entry: {}) not created: gameobject type GAMEOBJECT_TYPE_MO_TRANSPORT cannot be manually created.", guidlow, name_id);
        return false;
    }

    if (goinfo->type == GAMEOBJECT_TYPE_TRANSPORT)
        m_updateFlag = (m_updateFlag | UPDATEFLAG_TRANSPORT) & ~UPDATEFLAG_POSITION;

    Object::_Create(guidlow, goinfo->entry, HighGuid::GameObject);

    m_goInfo = goinfo;
    m_goTemplateAddon = sObjectMgr->GetGameObjectTemplateAddon(name_id);

    if (goinfo->type >= MAX_GAMEOBJECT_TYPE)
    {
        TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {} Entry: {}) not created: non-existing GO type '{}' in `gameobject_template`. It will crash client if created.", guidlow, name_id, goinfo->type);
        return false;
    }

    SetLocalRotation(rotation.x, rotation.y, rotation.z, rotation.w);
    GameObjectAddon const* gameObjectAddon = sObjectMgr->GetGameObjectAddon(GetSpawnId());

    // For most of gameobjects is (0, 0, 0, 1) quaternion, there are only some transports with not standard rotation
    QuaternionData parentRotation;
    if (gameObjectAddon)
        parentRotation = gameObjectAddon->ParentRotation;

    SetParentRotation(parentRotation);

    SetObjectScale(goinfo->size);

    if (GameObjectOverride const* goOverride = GetGameObjectOverride())
    {
        SetFaction(goOverride->Faction);
        ReplaceAllFlags(GameObjectFlags(goOverride->Flags));
    }

    SetEntry(goinfo->entry);

    // set name for logs usage, doesn't affect anything ingame
    SetName(goinfo->name);

    SetDisplayId(goinfo->displayId);

    CreateModel();
    // GAMEOBJECT_BYTES_1, index at 0, 1, 2 and 3
    SetGoType(GameobjectTypes(goinfo->type));
    m_prevGoState = go_state;
    SetGoState(go_state);
    SetGoArtKit(artKit);

    switch (goinfo->type)
    {
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            SetGoAnimProgress(animprogress);
            m_goValue.FishingHole.MaxOpens = urand(GetGOInfo()->fishinghole.minSuccessOpens, GetGOInfo()->fishinghole.maxSuccessOpens);
            break;
        case GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING:
            m_goValue.Building.Health = goinfo->building.intactNumHits + goinfo->building.damagedNumHits;
            m_goValue.Building.MaxHealth = m_goValue.Building.Health;
            SetGoAnimProgress(255);
            break;
        case GAMEOBJECT_TYPE_TRANSPORT:
            SetLevel(goinfo->transport.pause);
            SetGoState(goinfo->transport.startOpen ? GO_STATE_ACTIVE : GO_STATE_READY);
            SetGoAnimProgress(animprogress);
            m_goValue.Transport.PathProgress = 0;
            m_goValue.Transport.AnimationInfo = sTransportMgr->GetTransportAnimInfo(goinfo->entry);
            m_goValue.Transport.CurrentSeg = 0;
            break;
        case GAMEOBJECT_TYPE_FISHINGNODE:
            SetGoAnimProgress(0);
            break;
        case GAMEOBJECT_TYPE_TRAP:
            if (GetGOInfo()->trap.stealthed)
            {
                m_stealth.AddFlag(STEALTH_TRAP);
                m_stealth.AddValue(STEALTH_TRAP, 70);
            }

            if (GetGOInfo()->trap.invisible)
            {
                m_invisibility.AddFlag(INVISIBILITY_TRAP);
                m_invisibility.AddValue(INVISIBILITY_TRAP, 300);
            }

            m_goValue.Trap.TargetSearcherCheckType = TARGET_CHECK_ENEMY;
            if (SpellInfo const* trapSpell = sSpellMgr->GetSpellInfo(goinfo->trap.spellId))
            {
                // positive spells may require enemy targets
                if (trapSpell->IsPositive())
                {
                    bool targetsAlly = false;
                    bool targetsEnemy = false;
                    auto isAllyTarget = [](SpellImplicitTargetInfo const& targetInfo)
                    {
                        return targetInfo.GetObjectType() == TARGET_OBJECT_TYPE_UNIT && targetInfo.GetCheckType() == TARGET_CHECK_ALLY;
                    };
                    auto isEnemyTarget = [](SpellImplicitTargetInfo const& targetInfo)
                    {
                        return targetInfo.GetObjectType() == TARGET_OBJECT_TYPE_UNIT && targetInfo.GetCheckType() == TARGET_CHECK_ENEMY;
                    };
                    for (SpellEffectInfo const& spellEffectInfo : trapSpell->GetEffects())
                    {
                        if (!spellEffectInfo.IsEffect())
                            continue;

                        targetsAlly = targetsAlly || isAllyTarget(spellEffectInfo.TargetA) || isAllyTarget(spellEffectInfo.TargetB);
                        targetsEnemy = targetsEnemy || isEnemyTarget(spellEffectInfo.TargetA) || isEnemyTarget(spellEffectInfo.TargetB);
                    }
                    if (targetsAlly)
                        m_goValue.Trap.TargetSearcherCheckType = targetsEnemy ? TARGET_CHECK_DEFAULT : TARGET_CHECK_ALLY;
                }
            }
            break;
        default:
            SetGoAnimProgress(animprogress);
            break;
    }

    if (gameObjectAddon && gameObjectAddon->InvisibilityValue)
    {
        m_invisibility.AddFlag(gameObjectAddon->invisibilityType);
        m_invisibility.AddValue(gameObjectAddon->invisibilityType, gameObjectAddon->InvisibilityValue);
    }

    LastUsedScriptID = GetGOInfo()->ScriptId;
    AIM_Initialize();

    // Initialize loot duplicate count depending on raid difficulty
    if (map->Is25ManRaid())
        loot.maxDuplicates = 3;

    if (spawnid)
        m_spawnId = spawnid;

    if (uint32 linkedEntry = GetGOInfo()->GetLinkedGameObjectEntry())
    {
        GameObject* linkedGO = new GameObject();
        if (linkedGO->Create(map->GenerateLowGuid<HighGuid::GameObject>(), linkedEntry, map, phaseMask, pos, rotation, 255, GO_STATE_READY))
        {
            SetLinkedTrap(linkedGO);
            map->AddToMap(linkedGO);
        }
        else
            delete linkedGO;
    }

    // Check if GameObject is Large
    if (goinfo->IsLargeGameObject())
        SetVisibilityDistanceOverride(VisibilityDistanceType::Large);

    // Check if GameObject is Infinite
    if (goinfo->IsInfiniteGameObject())
        SetVisibilityDistanceOverride(VisibilityDistanceType::Infinite);

    return true;
}

/**
 * @brief 更新游戏对象状态
 *
 * 职责：
 *   处理游戏对象的周期性更新逻辑，包括AI更新、状态转换、重生计时等
 *
 * 参数：
 *   @param diff 距离上次更新的时间差（毫秒）
 *
 * 主要流程：
 *   1. 更新事件系统
 *   2. 更新AI逻辑
 *   3. 处理消失延迟计时
 *   4. 根据战利品状态(m_lootState)执行不同逻辑：
 *      - GO_NOT_READY: 未就绪状态，处理陷阱激活、运输工具路径等
 *      - GO_READY: 就绪状态，检查重生、陷阱触发、使用次数
 *      - GO_ACTIVATED: 激活状态，处理门/按钮重置、陷阱施法、战利品分配
 *      - GO_JUST_DEACTIVATED: 刚失活状态，处理消失、关联陷阱、战利品清理
 *
 * 状态机说明：
 *   GO_NOT_READY -> GO_READY -> GO_ACTIVATED -> GO_JUST_DEACTIVATED -> GO_NOT_READY
 *
 * 调用时机：
 *   每个游戏循环tick调用一次，由地图更新系统触发
 */
void GameObject::Update(uint32 diff)
{
    m_Events.Update(diff);

    if (AI())
        AI()->UpdateAI(diff);
    else if (!AIM_Initialize())
        TC_LOG_ERROR("misc", "Could not initialize GameObjectAI");

    if (m_despawnDelay)
    {
        if (m_despawnDelay > diff)
            m_despawnDelay -= diff;
        else
        {
            m_despawnDelay = 0;
            DespawnOrUnsummon(0ms, m_despawnRespawnTime);
        }
    }

    switch (m_lootState)
    {
        case GO_NOT_READY:
        {
            switch (GetGoType())
            {
                case GAMEOBJECT_TYPE_TRAP:
                {
                    // Arming Time for GAMEOBJECT_TYPE_TRAP (6)
                    GameObjectTemplate const* goInfo = GetGOInfo();
                    // Bombs
                    if (goInfo->trap.type == 2)
                        // Hardcoded tooltip value
                        m_cooldownTime = GameTime::GetGameTimeMS() + 10 * IN_MILLISECONDS;
                    else if (Unit* owner = GetOwner())
                        if (owner->IsInCombat())
                            m_cooldownTime = GameTime::GetGameTimeMS() + goInfo->trap.startDelay * IN_MILLISECONDS;

                    SetLootState(GO_READY);
                    break;
                }
                case GAMEOBJECT_TYPE_TRANSPORT:
                {
                    if (!m_goValue.Transport.AnimationInfo)
                        break;

                    if (GetGoState() == GO_STATE_READY)
                    {
                        m_goValue.Transport.PathProgress += diff;
                        /* TODO: Fix movement in unloaded grid - currently GO will just disappear
                        uint32 timer = m_goValue.Transport.PathProgress % m_goValue.Transport.AnimationInfo->TotalTime;
                        TransportAnimationEntry const* node = m_goValue.Transport.AnimationInfo->GetAnimNode(timer);
                        if (node && m_goValue.Transport.CurrentSeg != node->TimeSeg)
                        {
                            m_goValue.Transport.CurrentSeg = node->TimeSeg;

                            G3D::Quat rotation;
                            if (TransportRotationEntry const* rot = m_goValue.Transport.AnimationInfo->GetAnimRotation(timer))
                                rotation = G3D::Quat(rot->X, rot->Y, rot->Z, rot->W);

                            G3D::Vector3 pos = rotation.toRotationMatrix()
                                             * G3D::Matrix3::fromEulerAnglesZYX(GetOrientation(), 0.0f, 0.0f)
                                             * G3D::Vector3(node->X, node->Y, node->Z);

                            pos += G3D::Vector3(GetStationaryX(), GetStationaryY(), GetStationaryZ());

                            G3D::Vector3 src(GetPositionX(), GetPositionY(), GetPositionZ());

                            TC_LOG_DEBUG("misc", "Src: {} Dest: {}", src.toString(), pos.toString());

                            GetMap()->GameObjectRelocation(this, pos.x, pos.y, pos.z, GetOrientation());
                        }
                        */
                    }
                    break;
                }
                case GAMEOBJECT_TYPE_FISHINGNODE:
                {
                    // fishing code (bobber ready)
                    if (GameTime::GetGameTime() > m_respawnTime - FISHING_BOBBER_READY_TIME)
                    {
                        // splash bobber (bobber ready now)
                        Unit* caster = GetOwner();
                        if (caster && caster->GetTypeId() == TYPEID_PLAYER)
                        {
                            SetGoState(GO_STATE_ACTIVE);
                            ReplaceAllFlags(GO_FLAG_NODESPAWN);

                            UpdateData udata;
                            WorldPacket packet;
                            BuildValuesUpdateBlockForPlayer(&udata, caster->ToPlayer());
                            udata.BuildPacket(&packet);
                            caster->ToPlayer()->SendDirectMessage(&packet);

                            SendCustomAnim(GetGoAnimProgress());
                        }

                        m_lootState = GO_READY;                 // can be successfully open with some chance
                    }
                    return;
                }
                case GAMEOBJECT_TYPE_CHEST:
                    if (m_restockTime > GameTime::GetGameTime())
                        return;
                    // If there is no restock timer, or if the restock timer passed, the chest becomes ready to loot
                    m_restockTime = 0;
                    m_lootState = GO_READY;
                    AddToObjectUpdateIfNeeded();
                    break;
                default:
                    m_lootState = GO_READY;                         // for other GOis same switched without delay to GO_READY
                    break;
            }
            [[fallthrough]];
        }
        case GO_READY:
        {
            if (m_respawnCompatibilityMode)
            {
                if (m_respawnTime > 0)                          // timer on
                {
                    time_t now = GameTime::GetGameTime();
                    if (m_respawnTime <= now)            // timer expired
                    {
                        ObjectGuid dbtableHighGuid(HighGuid::GameObject, GetEntry(), m_spawnId);
                        time_t linkedRespawntime = GetMap()->GetLinkedRespawnTime(dbtableHighGuid);
                        if (linkedRespawntime)             // Can't respawn, the master is dead
                        {
                            ObjectGuid targetGuid = sObjectMgr->GetLinkedRespawnGuid(dbtableHighGuid);
                            if (targetGuid == dbtableHighGuid) // if linking self, never respawn
                                SetRespawnTime(WEEK);
                            else
                                m_respawnTime = (now > linkedRespawntime ? now : linkedRespawntime) + urand(5, MINUTE); // else copy time from master and add a little
                            SaveRespawnTime();
                            return;
                        }

                        m_respawnTime = 0;
                        m_SkillupList.clear();
                        m_usetimes = 0;

                        switch (GetGoType())
                        {
                            case GAMEOBJECT_TYPE_FISHINGNODE:   //  can't fish now
                            {
                                Unit* caster = GetOwner();
                                if (caster && caster->GetTypeId() == TYPEID_PLAYER)
                                {
                                    caster->ToPlayer()->RemoveGameObject(this, false);

                                    WorldPacket data(SMSG_FISH_ESCAPED, 0);
                                    caster->ToPlayer()->SendDirectMessage(&data);
                                }
                                // can be delete
                                m_lootState = GO_JUST_DEACTIVATED;
                                return;
                            }
                            case GAMEOBJECT_TYPE_DOOR:
                            case GAMEOBJECT_TYPE_BUTTON:
                                // We need to open doors if they are closed (add there another condition if this code breaks some usage, but it need to be here for battlegrounds)
                                if (GetGoState() != GO_STATE_READY)
                                    ResetDoorOrButton();
                                break;
                            case GAMEOBJECT_TYPE_FISHINGHOLE:
                                // Initialize a new max fish count on respawn
                                m_goValue.FishingHole.MaxOpens = urand(GetGOInfo()->fishinghole.minSuccessOpens, GetGOInfo()->fishinghole.maxSuccessOpens);
                                break;
                            default:
                                break;
                        }

                        // Despawn timer
                        if (!m_spawnedByDefault)
                        {
                            // Can be despawned or destroyed
                            SetLootState(GO_JUST_DEACTIVATED);
                            return;
                        }

                        // Call AI Reset (required for example in SmartAI to clear one time events)
                        if (AI())
                            AI()->Reset();

                        // Respawn timer
                        uint32 poolid = GetSpawnId() ? sPoolMgr->IsPartOfAPool<GameObject>(GetSpawnId()) : 0;
                        if (poolid)
                            sPoolMgr->UpdatePool<GameObject>(poolid, GetSpawnId());
                        else
                            GetMap()->AddToMap(this);
                    }
                }
            }

            // Set respawn timer
            if (!m_respawnCompatibilityMode && m_respawnTime > 0)
                SaveRespawnTime();

            if (isSpawned())
            {
                GameObjectTemplate const* goInfo = GetGOInfo();
                if (goInfo->type == GAMEOBJECT_TYPE_TRAP)
                {
                    if (GameTime::GetGameTimeMS() < m_cooldownTime)
                        break;

                    // Type 2 (bomb) does not need to be triggered by a unit and despawns after casting its spell.
                    if (goInfo->trap.type == 2)
                    {
                        SetLootState(GO_ACTIVATED);
                        break;
                    }

                    // Type 0 despawns after being triggered, type 1 does not.
                    /// @todo This is activation radius. Casting radius must be selected from spell data.
                    float radius;
                    if (!goInfo->trap.diameter)
                    {
                        // Battleground traps: data2 == 0 && data5 == 3
                        if (goInfo->trap.cooldown != 3)
                            break;

                        radius = 3.f;
                    }
                    else
                        radius = goInfo->trap.diameter / 2.f;

                    // Pointer to appropriate target if found any
                    Unit* target = nullptr;

                    /// @todo this hack with search required until GO casting not implemented
                    if (GetOwner())
                    {
                        // summoned traps: Search targets fit to trap spell data
                        if (SpellInfo const* trapSpell = sSpellMgr->GetSpellInfo(goInfo->trap.spellId))
                        {
                            WorldObject* worldObjectTarget = nullptr;
                            Trinity::WorldObjectSpellNearbyTargetCheck checker(radius, this, trapSpell, m_goValue.Trap.TargetSearcherCheckType, nullptr);
                            Trinity::WorldObjectLastSearcher searcher(this, worldObjectTarget, checker, GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER);
                            Cell::VisitAllObjects(this, searcher, radius);
                            target = Object::ToUnit(worldObjectTarget);
                        }
                        else
                        {
                            Trinity::NearestAttackableNoTotemUnitInObjectRangeCheck checker(this, radius);
                            Trinity::UnitLastSearcher<Trinity::NearestAttackableNoTotemUnitInObjectRangeCheck> searcher(this, target, checker);
                            Cell::VisitAllObjects(this, searcher, radius);
                        }
                    }
                    else
                    {
                        // Environmental trap: Any player
                        Player* player = nullptr;
                        Trinity::AnyPlayerInObjectRangeCheck checker(this, radius);
                        Trinity::PlayerSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(this, player, checker);
                        Cell::VisitWorldObjects(this, searcher, radius);
                        target = player;
                    }

                    if (target)
                        SetLootState(GO_ACTIVATED, target);

                }
                else if (uint32 max_charges = goInfo->GetCharges())
                {
                    if (m_usetimes >= max_charges)
                    {
                        m_usetimes = 0;
                        SetLootState(GO_JUST_DEACTIVATED);      // can be despawned or destroyed
                    }
                }
            }

            break;
        }
        case GO_ACTIVATED:
        {
            switch (GetGoType())
            {
                case GAMEOBJECT_TYPE_DOOR:
                case GAMEOBJECT_TYPE_BUTTON:
                    if (m_cooldownTime && GameTime::GetGameTimeMS() >= m_cooldownTime)
                        ResetDoorOrButton();
                    break;
                case GAMEOBJECT_TYPE_GOOBER:
                    if (GameTime::GetGameTimeMS() >= m_cooldownTime)
                    {
                        RemoveFlag(GO_FLAG_IN_USE);
                        SetLootState(GO_JUST_DEACTIVATED);
                    }
                    break;
                case GAMEOBJECT_TYPE_CHEST:
                    if (m_groupLootTimer)
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

                    // Non-consumable chest was partially looted and restock time passed, restock all loot now
                    if (GetGOInfo()->chest.consumable == 0 && GameTime::GetGameTime() >= m_restockTime)
                    {
                        m_restockTime = 0;
                        m_lootState = GO_READY;
                        AddToObjectUpdateIfNeeded();
                    }
                    break;
                case GAMEOBJECT_TYPE_TRAP:
                {
                    GameObjectTemplate const* goInfo = GetGOInfo();
                    if (goInfo->trap.type == 2 && goInfo->trap.spellId)
                    {
                        /// @todo nullptr target won't work for target type 1
                        CastSpell(nullptr, goInfo->trap.spellId);
                        SetLootState(GO_JUST_DEACTIVATED);
                    }
                    else if (Unit* target = ObjectAccessor::GetUnit(*this, m_lootStateUnitGUID))
                    {
                        // Some traps do not have a spell but should be triggered
                        CastSpellExtraArgs args;
                        args.SetOriginalCaster(GetOwnerGUID());
                        if (goInfo->trap.spellId)
                            CastSpell(target, goInfo->trap.spellId, args);

                        // Template value or 4 seconds
                        m_cooldownTime = GameTime::GetGameTimeMS() + (goInfo->trap.cooldown ? goInfo->trap.cooldown : uint32(4)) * IN_MILLISECONDS;

                        if (goInfo->trap.type == 1)
                            SetLootState(GO_JUST_DEACTIVATED);
                        else if (!goInfo->trap.type)
                            SetLootState(GO_READY);

                        // Battleground gameobjects have data2 == 0 && data5 == 3
                        if (!goInfo->trap.diameter && goInfo->trap.cooldown == 3)
                            if (Player* player = target->ToPlayer())
                                if (Battleground* bg = player->GetBattleground())
                                    bg->HandleTriggerBuff(GetGUID());
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case GO_JUST_DEACTIVATED:
        {
            // If nearby linked trap exists, despawn it
            if (GameObject* linkedTrap = GetLinkedTrap())
                linkedTrap->DespawnOrUnsummon();

            //if Gameobject should cast spell, then this, but some GOs (type = 10) should be destroyed
            if (GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                uint32 spellId = GetGOInfo()->goober.spellId;

                if (spellId)
                {
                    for (GuidSet::const_iterator it = m_unique_users.begin(); it != m_unique_users.end(); ++it)
                        // m_unique_users can contain only player GUIDs
                        if (Player* owner = ObjectAccessor::GetPlayer(*this, *it))
                            owner->CastSpell(owner, spellId, false);

                    m_unique_users.clear();
                    m_usetimes = 0;
                }

                // Only goobers with a lock id or a reset time may reset their go state
                if (GetGOInfo()->GetLockId() || GetGOInfo()->GetAutoCloseTime())
                    SetGoState(GO_STATE_READY);

                //any return here in case battleground traps
                if (GameObjectOverride const* goOverride = GetGameObjectOverride())
                    if (goOverride->Flags & GO_FLAG_NODESPAWN)
                        return;
            }

            loot.clear();

            // Do not delete chests or goobers that are not consumed on loot, while still allowing them to despawn when they expire if summoned
            bool isSummonedAndExpired = (GetOwner() || GetSpellId()) && m_respawnTime == 0;
            if ((GetGoType() == GAMEOBJECT_TYPE_CHEST || GetGoType() == GAMEOBJECT_TYPE_GOOBER) && !GetGOInfo()->IsDespawnAtAction() && !isSummonedAndExpired)
            {
                if (GetGoType() == GAMEOBJECT_TYPE_CHEST && GetGOInfo()->chest.chestRestockTime > 0)
                {
                    // Start restock timer when the chest is fully looted
                    m_restockTime = GameTime::GetGameTime() + GetGOInfo()->chest.chestRestockTime;
                    SetLootState(GO_NOT_READY);
                    AddToObjectUpdateIfNeeded();
                }
                else
                    SetLootState(GO_READY);
                UpdateObjectVisibility();
                return;
            }
            else if (GetOwnerGUID() || GetSpellId())
            {
                SetRespawnTime(0);
                Delete();
                return;
            }

            SetLootState(GO_NOT_READY);

            //burning flags in some battlegrounds, if you find better condition, just add it
            if (GetGOInfo()->IsDespawnAtAction() || GetGoAnimProgress() > 0)
            {
                SendObjectDeSpawnAnim(GetGUID());
                //reset flags
                if (GameObjectOverride const* goOverride = GetGameObjectOverride())
                    ReplaceAllFlags(GameObjectFlags(goOverride->Flags));
            }

            if (!m_respawnDelayTime)
                return;

            if (!m_spawnedByDefault)
            {
                m_respawnTime = 0;

                if (m_spawnId)
                    DestroyForNearbyPlayers();
                else
                    Delete();

                return;
            }

            uint32 respawnDelay = m_respawnDelayTime;
            if (uint32 scalingMode = sWorld->getIntConfig(CONFIG_RESPAWN_DYNAMICMODE))
                GetMap()->ApplyDynamicModeRespawnScaling(this, this->m_spawnId, respawnDelay, scalingMode);
            m_respawnTime = GameTime::GetGameTime() + respawnDelay;

            // if option not set then object will be saved at grid unload
            // Otherwise just save respawn time to map object memory
            SaveRespawnTime();

            if (m_respawnCompatibilityMode)
                DestroyForNearbyPlayers();
            else
                AddObjectToRemoveList();

            break;
        }
    }
}

/**
 * @brief 获取游戏对象覆盖数据
 *
 * 职责：
 *   获取游戏对象的覆盖配置数据（覆盖模板默认值）
 *
 * 返回值：
 *   @return GameObjectOverride const* 覆盖数据指针，不存在返回nullptr
 *
 * 优先级：
 *   1. 生成ID特定的覆盖数据
 *   2. 模板附加数据
 *
 * 用途：
 *   允许相同模板的游戏对象有不同的标志、阵营等属性
 */
GameObjectOverride const* GameObject::GetGameObjectOverride() const
{
    if (m_spawnId)
    {
        if (GameObjectOverride const* goOverride = sObjectMgr->GetGameObjectOverride(m_spawnId))
            return goOverride;
    }

    return m_goTemplateAddon;
}

/**
 * @brief 刷新游戏对象
 *
 * 职责：
 *   重新将游戏对象添加到地图中，刷新其状态
 *
 * 主要流程：
 *   1. 检查对象是否处于消失状态（从法术召唤的对象不刷新）
 *   2. 如果对象已生成，重新添加到地图
 *
 * 调用时机：
 *   池系统刷新对象时调用
 */
void GameObject::Refresh()
{
    // Do not refresh despawned GO from spellcast (GO's from spellcast are destroyed after despawn)
    if (m_respawnTime > 0 && m_spawnedByDefault)
        return;

    if (isSpawned())
        GetMap()->AddToMap(this);
}

/**
 * @brief 添加唯一使用记录
 *
 * 职责：
 *   记录玩家使用过该游戏对象，每个玩家只记录一次
 *
 * 参数：
 *   @param player 使用该游戏对象的玩家
 *
 * 主要流程：
 *   1. 增加使用次数计数器
 *   2. 将玩家GUID添加到唯一用户集合
 *
 * 用途：
 *   用于召唤仪式等需要统计多个不同玩家参与的场景
 */
void GameObject::AddUniqueUse(Player* player)
{
    AddUse();
    m_unique_users.insert(player->GetGUID());
}

/**
 * @brief 消失或取消召唤游戏对象
 *
 * 职责：
 *   使游戏对象从世界中消失，可选择延迟消失或设置强制重生时间
 *
 * 参数：
 *   @param delay           消失延迟时间（毫秒），0表示立即消失
 *   @param forceRespawnTime 强制重生时间（秒），用于覆盖默认重生时间
 *
 * 主要流程：
 *   如果有延迟：
 *   1. 设置消失延迟计时器
 *   2. 记录强制重生时间
 *   3. 在Update()中计时结束后执行消失
 *
 *   如果无延迟（立即消失）：
 *   1. 保存重生时间到数据库/地图
 *   2. 调用Delete()清理对象
 *
 * 调用时机：
 *   - 召唤类游戏对象到期消失
 *   - 脚本触发对象消失
 *   - 陷阱触发后消失
 *   - 宝箱被完全掠夺后消失
 */
void GameObject::DespawnOrUnsummon(Milliseconds delay, Seconds forceRespawnTime)
{
    if (delay > 0ms)
    {
        if (!m_despawnDelay || m_despawnDelay > delay.count())
        {
            m_despawnDelay = delay.count();
            m_despawnRespawnTime = forceRespawnTime;
        }
    }
    else
    {
        if (m_goData)
        {
            uint32 const respawnDelay = (forceRespawnTime > 0s) ? forceRespawnTime.count() : m_goData->spawntimesecs;
            SaveRespawnTime(respawnDelay);
        }
        Delete();
    }
}

/**
 * @brief 删除游戏对象
 *
 * 职责：
 *   完全清理游戏对象，包括状态重置、动画播放、池更新等
 *
 * 主要流程：
 *   1. 将战利品状态设为GO_NOT_READY
 *   2. 从所有者处移除（断开所有者关联）
 *   3. 向周围玩家发送消失动画
 *   4. 重置游戏对象状态为GO_STATE_READY
 *   5. 恢复默认标志
 *   6. 如果对象属于池，更新池状态
 *   7. 否则将对象添加到移除列表等待销毁
 *
 * 调用时机：
 *   当游戏对象需要被永久移除时调用
 *   通常由DespawnOrUnsummon()或脚本触发
 *
 * 注意事项：
 *   不会立即释放对象内存，而是加入移除列表
 *   在下一次地图更新时真正删除
 */
void GameObject::Delete()
{
    SetLootState(GO_NOT_READY);
    RemoveFromOwner();

    SendObjectDeSpawnAnim(GetGUID());

    SetGoState(GO_STATE_READY);

    if (GameObjectOverride const* goOverride = GetGameObjectOverride())
        ReplaceAllFlags(GameObjectFlags(goOverride->Flags));

    uint32 poolid = GetSpawnId() ? sPoolMgr->IsPartOfAPool<GameObject>(GetSpawnId()) : 0;
    if (poolid)
        sPoolMgr->UpdatePool<GameObject>(poolid, GetSpawnId());
    else
        AddObjectToRemoveList();
}

/**
 * @brief 获取钓鱼战利品
 *
 * 职责：
 *   根据钓鱼区域的区域/子区域填充战利品表
 *
 * 参数：
 *   @param fishloot   战利品对象指针
 *   @param loot_owner 拾取者玩家
 *
 * 主要流程：
 *   1. 清空现有战利品
 *   2. 获取当前区域和子区域ID
 *   3. 优先使用子区域战利品表
 *   4. 如果子区域无战利品，使用区域战利品表
 *   5. 如果区域也无战利品，使用默认区域1（通用钓鱼池）
 *
 * 战利品优先级：
 *   子区域 > 区域 > 默认区域(1)
 *
 * 调用时机：
 *   玩家钓鱼成功时，生成钓鱼战利品
 */
void GameObject::getFishLoot(Loot* fishloot, Player* loot_owner)
{
    fishloot->clear();

    uint32 zone, subzone;
    uint32 defaultzone = 1;
    GetZoneAndAreaId(zone, subzone);

    // if subzone loot exist use it
    fishloot->FillLoot(subzone, LootTemplates_Fishing, loot_owner, true, true);
    if (fishloot->empty())  //use this becase if zone or subzone has set LOOT_MODE_JUNK_FISH,Even if no normal drop, fishloot->FillLoot return true. it wrong.
    {
        //subzone no result,use zone loot
        fishloot->FillLoot(zone, LootTemplates_Fishing, loot_owner, true, true);
        //use zone 1 as default, somewhere fishing got nothing,becase subzone and zone not set, like Off the coast of Storm Peaks.
        if (fishloot->empty())
            fishloot->FillLoot(defaultzone, LootTemplates_Fishing, loot_owner, true, true);
    }
}

/**
 * @brief 获取钓鱼垃圾战利品
 *
 * 职责：
 *   当钓鱼技能不足时，填充垃圾战利品表
 *
 * 参数：
 *   @param fishloot   战利品对象指针
 *   @param loot_owner 拾取者玩家
 *
 * 主要流程：
 *   与getFishLoot类似，但使用LOOT_MODE_JUNK_FISH模式
 *   获取垃圾物品而非正常钓鱼物品
 *
 * 调用时机：
 *   钓鱼技能检测失败时，玩家获得垃圾物品
 */
void GameObject::getFishLootJunk(Loot* fishloot, Player* loot_owner)
{
    fishloot->clear();

    uint32 zone, subzone;
    uint32 defaultzone = 1;
    GetZoneAndAreaId(zone, subzone);

    // if subzone loot exist use it
    fishloot->FillLoot(subzone, LootTemplates_Fishing, loot_owner, true, true, LOOT_MODE_JUNK_FISH);
    if (fishloot->empty())  //use this becase if zone or subzone has normal mask drop, then fishloot->FillLoot return true.
    {
        //use zone loot
        fishloot->FillLoot(zone, LootTemplates_Fishing, loot_owner, true, true, LOOT_MODE_JUNK_FISH);
        if (fishloot->empty())
            //use zone 1 as default
            fishloot->FillLoot(defaultzone, LootTemplates_Fishing, loot_owner, true, true, LOOT_MODE_JUNK_FISH);
    }
}

/**
 * @brief 保存游戏对象到数据库（无参数版本）
 *
 * 职责：
 *   使用现有的生成数据保存游戏对象状态
 *
 * 主要流程：
 *   1. 从ObjectMgr获取现有的生成数据
 *   2. 使用当前地图ID和已有的生成掩码、相位掩码调用完整保存方法
 *
 * 调用时机：
 *   已经加载的游戏对象需要保存时
 */
void GameObject::SaveToDB()
{
    // this should only be used when the gameobject has already been loaded
    // preferably after adding to map, because mapid may not be valid otherwise
    GameObjectData const* data = sObjectMgr->GetGameObjectData(m_spawnId);
    if (!data)
    {
        TC_LOG_ERROR("misc", "GameObject::SaveToDB failed, cannot get gameobject data!");
        return;
    }

    SaveToDB(GetMapId(), data->spawnMask, data->phaseMask);
}

/**
 * @brief 将游戏对象保存到数据库
 *
 * 职责：
 *   将游戏对象的当前状态保存到数据库，持久化存储
 *
 * 参数：
 *   @param mapid      地图ID
 *   @param spawnMask  生成掩码
 *   @param phaseMask  相位掩码
 *
 * 主要流程：
 *   1. 验证游戏对象模板有效性
 *   2. 如果没有spawnId，生成一个新的
 *   3. 更新内存中的GameObjectData数据：
 *      - 位置、旋转、相位
 *      - 重生时间、动画进度、状态、外观套装
 *   4. 执行数据库事务：
 *      - 先删除旧记录
 *      - 插入新记录
 *
 * 调用时机：
 *   - 游戏对象被动态创建后需要持久化
 *   - GM命令保存游戏对象状态
 *   - 特定脚本需要保存对象状态
 *
 * 注意事项：
 *   通常游戏对象不需要频繁保存，重生时间由SaveRespawnTime()单独处理
 */
void GameObject::SaveToDB(uint32 mapid, uint8 spawnMask, uint32 phaseMask)
{
    GameObjectTemplate const* goI = GetGOInfo();
    if (!goI)
        return;

    if (!m_spawnId)
        m_spawnId = sObjectMgr->GenerateGameObjectSpawnId();

    // update in loaded data (changing data only in this place)
    GameObjectData& data = sObjectMgr->NewOrExistGameObjectData(m_spawnId);

    if (!data.spawnId)
        data.spawnId = m_spawnId;
    ASSERT(data.spawnId == m_spawnId);
    data.id = GetEntry();
    data.mapId = GetMapId();
    data.spawnPoint.Relocate(this);
    data.phaseMask = phaseMask;
    data.rotation = m_localRotation;
    data.spawntimesecs = m_spawnedByDefault ? m_respawnDelayTime : -(int32)m_respawnDelayTime;
    data.animprogress = GetGoAnimProgress();
    data.goState = GetGoState();
    data.spawnMask = spawnMask;
    data.artKit = GetGoArtKit();
    if (!data.spawnGroupData)
        data.spawnGroupData = sObjectMgr->GetDefaultSpawnGroup();

    // Update in DB
    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();

    uint8 index = 0;

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAMEOBJECT);
    stmt->setUInt32(0, m_spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GAMEOBJECT);
    stmt->setUInt32(index++, m_spawnId);
    stmt->setUInt32(index++, GetEntry());
    stmt->setUInt16(index++, uint16(mapid));
    stmt->setUInt8(index++, spawnMask);
    stmt->setUInt32(index++, GetPhaseMask());
    stmt->setFloat(index++, GetPositionX());
    stmt->setFloat(index++, GetPositionY());
    stmt->setFloat(index++, GetPositionZ());
    stmt->setFloat(index++, GetOrientation());
    stmt->setFloat(index++, m_localRotation.x);
    stmt->setFloat(index++, m_localRotation.y);
    stmt->setFloat(index++, m_localRotation.z);
    stmt->setFloat(index++, m_localRotation.w);
    stmt->setInt32(index++, int32(m_respawnDelayTime));
    stmt->setUInt8(index++, GetGoAnimProgress());
    stmt->setUInt8(index++, uint8(GetGoState()));
    trans->Append(stmt);

    WorldDatabase.CommitTransaction(trans);
}

/**
 * @brief 从数据库加载游戏对象
 *
 * 职责：
 *   从数据库数据加载游戏对象的所有属性并初始化
 *
 * 参数：
 *   @param spawnId   生成ID（数据库中的唯一标识）
 *   @param map       所属地图指针
 *   @param addToMap  是否添加到地图中
 *
 * 返回值：
 *   @return bool 加载成功返回true，失败返回false
 *
 * 主要流程：
 *   1. 从ObjectMgr获取GameObjectData数据
 *   2. 验证数据存在性
 *   3. 提取基本属性（entry、phaseMask、animprogress、go_state、artKit）
 *   4. 调用Create()创建对象
 *   5. 设置重生时间和延迟
 *   6. 处理默认生成/非默认生成的逻辑
 *   7. 如果addToMap为true，将对象添加到地图
 *
 * 调用时机：
 *   地图加载时，从数据库加载所有该地图的游戏对象
 *   由Map::LoadGridObjects()触发
 */
bool GameObject::LoadFromDB(ObjectGuid::LowType spawnId, Map* map, bool addToMap, bool)
{
    GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);

    if (!data)
    {
        TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {}) not found in table `gameobject`, can't load. ", spawnId);
        return false;
    }

    uint32 entry = data->id;
    //uint32 map_id = data->mapid;                          // already used before call
    uint32 phaseMask = data->phaseMask;

    uint32 animprogress = data->animprogress;
    GOState go_state = data->goState;
    uint32 artKit = data->artKit;

    m_spawnId = spawnId;
    m_respawnCompatibilityMode = ((data->spawnGroupData->flags & SPAWNGROUP_FLAG_COMPATIBILITY_MODE) != 0);
    if (!Create(map->GenerateLowGuid<HighGuid::GameObject>(), entry, map, phaseMask, data->spawnPoint, data->rotation, animprogress, go_state, artKit, !m_respawnCompatibilityMode))
        return false;

    if (data->spawntimesecs >= 0)
    {
        m_spawnedByDefault = true;

        if (!GetGOInfo()->GetDespawnPossibility() && !GetGOInfo()->IsDespawnAtAction())
        {
            SetFlag(GO_FLAG_NODESPAWN);
            m_respawnDelayTime = 0;
            m_respawnTime = 0;
        }
        else
        {
            m_respawnDelayTime = data->spawntimesecs;
            m_respawnTime = GetMap()->GetGORespawnTime(m_spawnId);

            // ready to respawn
            if (m_respawnTime && m_respawnTime <= GameTime::GetGameTime())
            {
                m_respawnTime = 0;
                GetMap()->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, m_spawnId);
            }
        }
    }
    else
    {
        if (!m_respawnCompatibilityMode)
        {
            TC_LOG_WARN("sql.sql", "GameObject {} (SpawnID {}) is not spawned by default, but tries to use a non-hack spawn system. This will not work. Defaulting to compatibility mode.", entry, spawnId);
            m_respawnCompatibilityMode = true;
        }

        m_spawnedByDefault = false;
        m_respawnDelayTime = -data->spawntimesecs;
        m_respawnTime = 0;
    }

    m_goData = data;

    if (addToMap && !GetMap()->AddToMap(this))
        return false;

    return true;
}

/**
 * @brief 从数据库删除游戏对象（静态方法）
 *
 * 职责：
 *   从数据库和所有地图中永久删除指定游戏对象
 *
 * 参数：
 *   @param spawnId 要删除的游戏对象生成ID
 *
 * 返回值：
 *   @return bool 删除成功返回true，对象不存在返回false
 *
 * 主要流程：
 *   1. 验证游戏对象数据存在
 *   2. 遍历所有该地图ID的地图实例：
 *      - 让所有激活的对象进入移除列表
 *      - 移除重生时间记录
 *   3. 从内存中删除对象数据
 *   4. 从数据库中删除关联的所有数据：
 *      - gameobject表
 *      - spawn_group_member表
 *      - gameobject_addon表
 *      - game_event_gameobject表
 *      - linked_respawn表（GO到GO、GO到生物的主从关系）
 *
 * 调用时机：
 *   - GM命令删除游戏对象
 *   - 脚本需要永久移除游戏对象
 *
 * 注意事项：
 *   这是一个破坏性操作，不可恢复
 *   会影响所有地图实例中的该游戏对象
 */
/*static*/ bool GameObject::DeleteFromDB(ObjectGuid::LowType spawnId)
{
    GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);
    if (!data)
        return false;

    CharacterDatabaseTransaction charTrans = CharacterDatabase.BeginTransaction();

    sMapMgr->DoForAllMapsWithMapId(data->mapId,
        [spawnId, charTrans](Map* map) -> void
        {
            // despawn all active objects, and remove their respawns
            std::vector<GameObject*> toUnload;
            for (auto const& pair : Trinity::Containers::MapEqualRange(map->GetGameObjectBySpawnIdStore(), spawnId))
                toUnload.push_back(pair.second);
            for (GameObject* obj : toUnload)
                map->AddObjectToRemoveList(obj);
            map->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, spawnId, charTrans);
        }
    );

    // delete data from memory
    sObjectMgr->DeleteGameObjectData(spawnId);

    CharacterDatabase.CommitTransaction(charTrans);

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();

    // ... and the database
    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAMEOBJECT);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_SPAWNGROUP_MEMBER);
    stmt->setUInt8(0, uint8(SPAWN_TYPE_GAMEOBJECT));
    stmt->setUInt32(1, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_EVENT_GAMEOBJECT);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_GO_TO_GO);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_GO_TO_CREATURE);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN_MASTER);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_GO_TO_GO);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN_MASTER);
    stmt->setUInt32(0, spawnId);
    stmt->setUInt32(1, LINKED_RESPAWN_CREATURE_TO_GO);
    trans->Append(stmt);

    stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAMEOBJECT_ADDON);
    stmt->setUInt32(0, spawnId);
    trans->Append(stmt);

    WorldDatabase.CommitTransaction(trans);

    return true;
}

/*********************************************************/
/***                    QUEST SYSTEM                   ***/
/*********************************************************/

/**
 * @brief 检查游戏对象是否提供指定任务
 *
 * 职责：
 *   判断该游戏对象是否可以提供指定任务（任务接取）
 *
 * 参数：
 *   @param quest_id 任务ID
 *
 * 返回值：
 *   @return bool 提供任务返回true，否则返回false
 */
bool GameObject::hasQuest(uint32 quest_id) const
{
    return sObjectMgr->GetGOQuestRelations(GetEntry()).HasQuest(quest_id);
}

/**
 * @brief 检查游戏对象是否涉及指定任务
 *
 * 职责：
 *   判断该游戏对象是否作为指定任务的目标（任务完成）
 *
 * 参数：
 *   @param quest_id 任务ID
 *
 * 返回值：
 *   @return bool 涉及任务返回true，否则返回false
 */
bool GameObject::hasInvolvedQuest(uint32 quest_id) const
{
    return sObjectMgr->GetGOQuestInvolvedRelations(GetEntry()).HasQuest(quest_id);
}

/**
 * @brief 检查是否为运输工具
 *
 * 职责：
 *   判断游戏对象是否为运输工具类型（船、飞艇等）
 *
 * 返回值：
 *   @return bool 是运输工具返回true，否则返回false
 *
 * 运输工具类型：
 *   - GAMEOBJECT_TYPE_TRANSPORT: 普通运输工具
 *   - GAMEOBJECT_TYPE_MO_TRANSPORT: 动态运输工具
 */
bool GameObject::IsTransport() const
{
    // If something is marked as a transport, don't transmit an out of range packet for it.
    GameObjectTemplate const* gInfo = GetGOInfo();
    if (!gInfo)
        return false;

    return gInfo->type == GAMEOBJECT_TYPE_TRANSPORT || gInfo->type == GAMEOBJECT_TYPE_MO_TRANSPORT;
}

/**
 * @brief 检查是否为动态运输工具
 *
 * 职责：
 *   判断游戏对象是否为不间断运行的运输工具
 *
 * 返回值：
 *   @return bool 是动态运输工具返回true，否则返回false
 *
 * 判断条件：
 *   - MO_TRANSPORT类型
 *   - TRANSPORT类型且无暂停时间
 */
// is Dynamic transport = non-stop Transport
bool GameObject::IsDynTransport() const
{
    // If something is marked as a transport, don't transmit an out of range packet for it.
    GameObjectTemplate const* gInfo = GetGOInfo();
    if (!gInfo)
        return false;

    return gInfo->type == GAMEOBJECT_TYPE_MO_TRANSPORT || (gInfo->type == GAMEOBJECT_TYPE_TRANSPORT && !gInfo->transport.pause);
}

/**
 * @brief 检查是否为可破坏建筑
 *
 * 职责：
 *   判断游戏对象是否为可破坏建筑（城墙、大门等）
 *
 * 返回值：
 *   @return bool 是可破坏建筑返回true，否则返回false
 */
bool GameObject::IsDestructibleBuilding() const
{
    GameObjectTemplate const* gInfo = GetGOInfo();
    if (!gInfo)
        return false;

    return gInfo->type == GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING;
}

/**
 * @brief 保存重生时间
 *
 * 职责：
 *   将游戏对象的重生时间保存到数据库或地图内存中
 *
 * 参数：
 *   @param forceDelay 强制重生延迟时间（秒），0表示使用当前重生时间
 *
 * 主要流程：
 *   1. 检查是否有生成数据且对象默认生成
 *   2. 根据重生兼容模式选择保存方式：
 *      - 兼容模式：直接保存到数据库
 *      - 非兼容模式：保存到地图的内存中，由地图管理持久化
 *   3. 记录重生时间和对象信息
 *
 * 调用时机：
 *   - 游戏对象被掠夺完成或激活后消失
 *   - 对象进入失活状态
 *   - 需要强制设置重生时间时
 *
 * 注意事项：
 *   频繁调用会写入数据库，应避免不必要的保存
 */
void GameObject::SaveRespawnTime(uint32 forceDelay)
{
    if (m_goData && (forceDelay || m_respawnTime > GameTime::GetGameTime()) && m_spawnedByDefault)
    {
        if (m_respawnCompatibilityMode)
        {
            RespawnInfo ri;
            ri.type = SPAWN_TYPE_GAMEOBJECT;
            ri.spawnId = m_spawnId;
            ri.respawnTime = m_respawnTime;
            GetMap()->SaveRespawnInfoDB(ri);
            return;
        }

        uint32 thisRespawnTime = forceDelay ? GameTime::GetGameTime() + forceDelay : m_respawnTime;
        GetMap()->SaveRespawnTime(SPAWN_TYPE_GAMEOBJECT, m_spawnId, GetEntry(), thisRespawnTime, Trinity::ComputeGridCoord(GetPositionX(), GetPositionY()).GetId());
    }
}

/**
 * @brief 检查对象是否永远不可见
 *
 * 职责：
 *   判断游戏对象是否在任何情况下都不应显示给客户端
 *
 * 参数：
 *   @param allowServersideObjects 是否允许服务器端对象
 *
 * 返回值：
 *   @return bool 永远不可见返回true，否则返回false
 *
 * 不可见条件：
 *   - 继承自WorldObject的不可见条件
 *   - 服务器端专用对象且不允许显示
 */
bool GameObject::IsNeverVisible(bool allowServersideObjects) const
{
    if (WorldObject::IsNeverVisible(allowServersideObjects))
        return true;

    if (GetGOInfo()->GetServerOnly() && !allowServersideObjects)
        return true;

    return false;
}

/**
 * @brief 检查对象是否对观察者始终可见
 *
 * 职责：
 *   判断游戏对象是否应该始终对指定观察者可见
 *
 * 参数：
 *   @param seer 观察者对象
 *
 * 返回值：
 *   @return bool 始终可见返回true，否则返回false
 *
 * 始终可见条件：
 *   - 继承自WorldObject的条件
 *   - 是运输工具或可破坏建筑
 *   - 是观察者的所有者对象
 *   - 所有者的友方单位
 */
bool GameObject::IsAlwaysVisibleFor(WorldObject const* seer) const
{
    if (WorldObject::IsAlwaysVisibleFor(seer))
        return true;

    if (IsTransport() || IsDestructibleBuilding())
        return true;

    if (!seer)
        return false;

    // Always seen by owner and friendly units
    if (ObjectGuid guid = GetOwnerGUID())
    {
        if (seer->GetGUID() == guid)
            return true;

        Unit* owner = GetOwner();
        if (Unit const* unitSeer = seer->ToUnit())
            if (owner && owner->IsFriendlyTo(unitSeer))
                return true;
    }

    return false;
}

/**
 * @brief 检查对象是否因消失而不可见
 *
 * 职责：
 *   判断游戏对象是否因处于消失状态而不可见
 *
 * 返回值：
 *   @return bool 因消失不可见返回true，否则返回false
 */
bool GameObject::IsInvisibleDueToDespawn() const
{
    if (WorldObject::IsInvisibleDueToDespawn())
        return true;

    // Despawned
    if (!isSpawned())
        return true;

    return false;
}

/**
 * @brief 获取对目标的有效等级
 *
 * 职责：
 *   获取游戏对象相对于指定目标的等级
 *
 * 参数：
 *   @param target 目标对象
 *
 * 返回值：
 *   @return uint8 对象等级
 *
 * 等级规则：
 *   - 有所有者：使用所有者等级
 *   - 陷阱类型：使用模板等级或目标等级
 *   - 其他：默认等级1
 */
uint8 GameObject::GetLevelForTarget(WorldObject const* target) const
{
    if (Unit* owner = GetOwner())
        return owner->GetLevelForTarget(target);

    if (GetGoType() == GAMEOBJECT_TYPE_TRAP)
    {
        if (GetGOInfo()->trap.level != 0)
            return GetGOInfo()->trap.level;
        if (const Unit* targetUnit = target->ToUnit())
            return targetUnit->GetLevel();
    }

    return 1;
}

/**
 * @brief 获取扩展重生时间
 *
 * 职责：
 *   获取游戏对象的重生时间，如果已过期则返回当前时间
 *
 * 返回值：
 *   @return time_t 重生时间戳
 *
 * 用途：
 *   确保返回的时间总是>=当前时间
 */
time_t GameObject::GetRespawnTimeEx() const
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
 * 职责：
 *   设置游戏对象的重生时间和延迟
 *
 * 参数：
 *   @param respawn 重生延迟时间（秒），<=0表示立即重生或不重生
 *
 * 主要流程：
 *   1. 计算重生时间点 = 当前时间 + 延迟
 *   2. 保存延迟值
 *   3. 如果是非默认生成对象且有重生时间，更新对象可见性
 *
 * 调用时机：
 *   - 游戏对象被掠夺后设置重生时间
 *   - 脚本强制设置重生时间
 *   - 重置游戏对象状态时
 */
void GameObject::SetRespawnTime(int32 respawn)
{
    m_respawnTime = respawn > 0 ? GameTime::GetGameTime() + respawn : 0;
    m_respawnDelayTime = respawn > 0 ? respawn : 0;
    if (respawn && !m_spawnedByDefault)
        UpdateObjectVisibility(true);
}

/**
 * @brief 立即重生游戏对象
 *
 * 职责：
 *   强制游戏对象立即重生
 *
 * 主要流程：
 *   1. 检查是否为默认生成对象且有重生时间
 *   2. 将重生时间设为当前时间
 *   3. 通知地图重生该对象
 *
 * 调用时机：
 *   - GM命令强制重生对象
 *   - 特定脚本需要立即重生对象
 *   - 任务完成触发对象重生
 */
void GameObject::Respawn()
{
    if (m_spawnedByDefault && m_respawnTime > 0)
    {
        m_respawnTime = GameTime::GetGameTime();
        GetMap()->Respawn(SPAWN_TYPE_GAMEOBJECT, m_spawnId);
    }
}

/**
 * @brief 检查游戏对象是否对玩家任务激活
 *
 * 职责：
 *   判断游戏对象是否应该对玩家显示任务激活效果（高亮、感叹号等）
 *
 * 参数：
 *   @param target 目标玩家
 *
 * 返回值：
 *   @return bool 激活返回true，否则返回false
 *
 * 激活条件（根据游戏对象类型）：
 *   - QUESTGIVER: 任务对话状态 > DIALOG_STATUS_UNAVAILABLE
 *   - CHEST: 玩家有相关任务未完成或战利品中有任务物品
 *   - GENERIC: 玩家有相关任务未完成
 *   - GOOBER: 玩家有相关任务未完成
 *
 * 调用时机：
 *   客户端查询游戏对象动态标志时调用
 *   用于确定是否显示任务激活光效
 */
bool GameObject::ActivateToQuest(Player const* target) const
{
    if (target->HasQuestForGO(GetEntry()))
        return true;

    if (!sObjectMgr->IsGameObjectForQuests(GetEntry()))
        return false;

    switch (GetGoType())
    {
        case GAMEOBJECT_TYPE_QUESTGIVER:
        {
            GameObject* go = const_cast<GameObject*>(this);
            QuestGiverStatus questStatus = const_cast<Player*>(target)->GetQuestDialogStatus(go);
            if (questStatus > DIALOG_STATUS_UNAVAILABLE)
                return true;
            break;
        }
        case GAMEOBJECT_TYPE_CHEST:
        {
            // Chests become inactive while not ready to be looted
            if (getLootState() == GO_NOT_READY)
                return false;

            // scan GO chest with loot including quest items
            if (target->GetQuestStatus(GetGOInfo()->chest.questId) == QUEST_STATUS_INCOMPLETE || LootTemplates_Gameobject.HaveQuestLootForPlayer(GetGOInfo()->GetLootId(), target))
            {
                if (Battleground const* bg = target->GetBattleground())
                    return bg->CanActivateGO(GetEntry(), target->GetTeam());
                return true;
            }
            break;
        }
        case GAMEOBJECT_TYPE_GENERIC:
        {
            if (GetGOInfo()->_generic.questID == -1 || target->GetQuestStatus(GetGOInfo()->_generic.questID) == QUEST_STATUS_INCOMPLETE)
                return true;
            break;
        }
        case GAMEOBJECT_TYPE_GOOBER:
        {
            if (GetGOInfo()->goober.questId == -1 || target->GetQuestStatus(GetGOInfo()->goober.questId) == QUEST_STATUS_INCOMPLETE)
                return true;
            break;
        }
        default:
            break;
    }

    return false;
}

/**
 * @brief 触发关联的游戏对象（陷阱）
 *
 * 职责：
 *   让关联的陷阱游戏对象对目标施放法术
 *
 * 参数：
 *   @param trapEntry 陷阱模板ID
 *   @param target    目标单位
 *
 * 主要流程：
 *   1. 验证陷阱模板和类型
 *   2. 获取陷阱法术信息
 *   3. 让关联陷阱对目标施放法术
 *
 * 调用时机：
 *   Goober类型对象被使用时，触发关联陷阱
 */
void GameObject::TriggeringLinkedGameObject(uint32 trapEntry, Unit* target)
{
    GameObjectTemplate const* trapInfo = sObjectMgr->GetGameObjectTemplate(trapEntry);
    if (!trapInfo || trapInfo->type != GAMEOBJECT_TYPE_TRAP)
        return;

    SpellInfo const* trapSpell = sSpellMgr->GetSpellInfo(trapInfo->trap.spellId);
    if (!trapSpell)                                          // checked at load already
        return;

    if (GameObject* trapGO = GetLinkedTrap())
        trapGO->CastSpell(target, trapSpell->Id);
}

/**
 * @brief 查找周围的钓鱼洞
 *
 * 职责：
 *   在指定范围内搜索最近的钓鱼洞游戏对象
 *
 * 参数：
 *   @param range 搜索半径
 *
 * 返回值：
 *   @return GameObject* 找到的钓鱼洞对象，未找到返回nullptr
 *
 * 调用时机：
 *   玩家钓鱼时检查是否有钓鱼洞
 */
GameObject* GameObject::LookupFishingHoleAround(float range)
{
    GameObject* ok = nullptr;
    Trinity::NearestGameObjectFishingHole u_check(*this, range);
    Trinity::GameObjectSearcher<Trinity::NearestGameObjectFishingHole> checker(this, ok, u_check);
    Cell::VisitGridObjects(this, checker, range);
    return ok;
}

/**
 * @brief 重置门或按钮到默认状态
 *
 * 职责：
 *   将门或按钮恢复到之前的开关状态
 *
 * 主要流程：
 *   1. 检查当前状态，如果是GO_READY或GO_JUST_DEACTIVATED则直接返回
 *   2. 移除"使用中"标志
 *   3. 恢复之前的游戏对象状态
 *   4. 设置战利品状态为GO_JUST_DEACTIVATED
 *   5. 清除冷却时间
 *
 * 调用时机：
 *   - 门/按钮的自动关闭计时到期
 *   - 脚本强制重置门/按钮
 *   - 战场中重置门的状态
 */
void GameObject::ResetDoorOrButton()
{
    if (m_lootState == GO_READY || m_lootState == GO_JUST_DEACTIVATED)
        return;

    RemoveFlag(GO_FLAG_IN_USE);
    SetGoState(m_prevGoState);

    SetLootState(GO_JUST_DEACTIVATED);
    m_cooldownTime = 0;
}

/**
 * @brief 使用门或按钮
 *
 * 职责：
 *   激活门或按钮，并设置自动恢复计时
 *
 * 参数：
 *   @param time_to_restore 自动恢复时间（毫秒），0表示使用模板默认值
 *   @param alternative     是否使用替代状态（销毁状态而非激活状态）
 *   @param user            使用者单位
 *
 * 主要流程：
 *   1. 检查当前状态是否为GO_READY（只能激活就绪状态的对象）
 *   2. 如果未指定时间，使用模板中的自动关闭时间
 *   3. 切换门/按钮状态（打开或销毁）
 *   4. 设置战利品状态为GO_ACTIVATED
 *   5. 设置冷却时间，到期后自动恢复
 *
 * 调用时机：
 *   - 玩家右键点击门或按钮
 *   - 脚本触发门或按钮
 *   - 副本中特定事件触发门开关
 */
void GameObject::UseDoorOrButton(uint32 time_to_restore, bool alternative /* = false */, Unit* user /*=nullptr*/)
{
    if (m_lootState != GO_READY)
        return;

    if (!time_to_restore)
        time_to_restore = GetGOInfo()->GetAutoCloseTime();

    SwitchDoorOrButton(true, alternative);
    SetLootState(GO_ACTIVATED, user);

    m_cooldownTime = time_to_restore ? (GameTime::GetGameTimeMS() + time_to_restore) : 0;
}

/**
 * @brief 激活游戏对象（执行指定动作）
 *
 * 职责：
 *   根据法术效果激活游戏对象执行特定动作
 *
 * 参数：
 *   @param action       要执行的动作类型
 *   @param spellCaster  法术施法者
 *   @param spellId      触发该动作的法术ID
 *   @param effectIndex  法术效果索引
 *
 * 支持的动作类型：
 *   - None: 无动作（记录错误日志）
 *   - AnimateCustom0-3: 播放自定义动画
 *   - Disturb: 干扰（等同于Use）
 *   - Unlock/Lock: 解锁/锁定
 *   - Open/Close: 打开/关闭
 *   - OpenAndUnlock: 打开并解锁
 *   - Destroy/Rebuild: 销毁/重建
 *   - Despawn: 消失
 *   - MakeInert/MakeActive: 使其不可交互/可交互
 *   - CloseAndLock: 关闭并锁定
 *   - UseArtKit0-3: 使用外观套装
 *   - SetTapList: 设置触摸列表（未实现）
 *
 * 调用时机：
 *   当法术效果为SPELL_EFFECT_ACTIVATE_OBJECT时触发
 *   由法术系统调用
 */
void GameObject::ActivateObject(GameObjectActions action, WorldObject* spellCaster, uint32 spellId, int32 effectIndex)
{
    Unit* unitCaster = spellCaster ? spellCaster->ToUnit() : nullptr;

    switch (action)
    {
        case GameObjectActions::None:
            TC_LOG_FATAL("spell", "Spell {} has action type NONE in effect {}", spellId, effectIndex);
            break;
        case GameObjectActions::AnimateCustom0:
        case GameObjectActions::AnimateCustom1:
        case GameObjectActions::AnimateCustom2:
        case GameObjectActions::AnimateCustom3:
            SendCustomAnim(uint32(action) - uint32(GameObjectActions::AnimateCustom0));
            break;
        case GameObjectActions::Disturb: // What's the difference with Open?
            if (unitCaster)
                Use(unitCaster);
            break;
        case GameObjectActions::Unlock:
            RemoveFlag(GO_FLAG_LOCKED);
            break;
        case GameObjectActions::Lock:
            SetFlag(GO_FLAG_LOCKED);
            break;
        case GameObjectActions::Open:
            if (unitCaster)
                Use(unitCaster);
            break;
        case GameObjectActions::OpenAndUnlock:
            if (unitCaster)
            {
                UseDoorOrButton(0, false, unitCaster);
                RemoveFlag(GO_FLAG_LOCKED);
            }
            break;
        case GameObjectActions::Close:
            ResetDoorOrButton();
            break;
        case GameObjectActions::ToggleOpen:
            // No use cases, implementation unknown
            break;
        case GameObjectActions::Destroy:
            if (unitCaster)
                UseDoorOrButton(0, true, unitCaster);
            break;
        case GameObjectActions::Rebuild:
            ResetDoorOrButton();
            break;
        case GameObjectActions::Creation:
            // No use cases, implementation unknown
            break;
        case GameObjectActions::Despawn:
            DespawnOrUnsummon();
            break;
        case GameObjectActions::MakeInert:
            SetFlag(GO_FLAG_NOT_SELECTABLE);
            break;
        case GameObjectActions::MakeActive:
            RemoveFlag(GO_FLAG_NOT_SELECTABLE);
            break;
        case GameObjectActions::CloseAndLock:
            ResetDoorOrButton();
            SetFlag(GO_FLAG_LOCKED);
            break;
        case GameObjectActions::UseArtKit0:
        case GameObjectActions::UseArtKit1:
        case GameObjectActions::UseArtKit2:
        case GameObjectActions::UseArtKit3:
        {
            GameObjectTemplateAddon const* templateAddon = GetTemplateAddon();

            uint32 artKitIndex = uint32(action) - uint32(GameObjectActions::UseArtKit0);

            uint32 artKitValue = 0;
            if (templateAddon != nullptr)
                artKitValue = templateAddon->artKits[artKitIndex];

            if (artKitValue == 0)
                TC_LOG_ERROR("sql.sql", "GameObject {} hit by spell {} needs `artkit{}` in `gameobject_template_addon`", GetEntry(), spellId, artKitIndex);
            else
                SetGoArtKit(artKitValue);

            break;
        }
        case GameObjectActions::SetTapList:
            // No use cases, implementation unknown
            break;
        default:
            TC_LOG_ERROR("spell", "Spell {} has unhandled action {} in effect {}", spellId, int32(action), effectIndex);
            break;
    }
}

/**
 * @brief 设置游戏对象外观套装
 *
 * 职责：
 *   改变游戏对象的视觉外观套装ID
 *
 * 参数：
 *   @param kit 外观套装ID
 *
 * 主要流程：
 *   1. 设置GAMEOBJECT_BYTES_1字段的外观套装字节
 *   2. 更新内存中的GameObjectData数据
 *
 * 调用时机：
 *   - 游戏对象创建时从数据库加载
 *   - 法术效果改变外观时
 *   - 脚本强制改变外观时
 *
 * 外观套装说明：
 *   不同的外观套装可以让同一个游戏对象模板显示不同的模型或纹理
 *   常用于任务进度显示、状态变化等
 */
void GameObject::SetGoArtKit(uint8 kit)
{
    SetByteValue(GAMEOBJECT_BYTES_1, 2, kit);
    GameObjectData* data = const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(m_spawnId));
    if (data)
        data->artKit = kit;
}

/**
 * @brief 设置游戏对象外观套装（静态方法，支持离线对象）
 *
 * 职责：
 *   为在线或离线的游戏对象设置外观套装ID
 *
 * 参数：
 *   @param artkit  外观套装ID
 *   @param go      在线游戏对象指针（可为nullptr）
 *   @param lowguid 离线对象的生成ID（当go为nullptr时使用）
 *
 * 主要流程：
 *   1. 如果提供了在线对象go，直接调用其SetGoArtKit方法
 *   2. 如果只提供了lowguid，从ObjectMgr获取数据并更新
 *   3. 更新内存中的GameObjectData数据
 *
 * 用途：
 *   允许为未加载的游戏对象设置外观套装
 *   用于事件脚本、任务进度等场景
 */
void GameObject::SetGoArtKit(uint8 artkit, GameObject* go, ObjectGuid::LowType lowguid)
{
    GameObjectData const* data = nullptr;
    if (go)
    {
        go->SetGoArtKit(artkit);
        data = go->GetGameObjectData();
    }
    else if (lowguid)
        data = sObjectMgr->GetGameObjectData(lowguid);

    if (data)
        const_cast<GameObjectData*>(data)->artKit = artkit;
}

/**
 * @brief 切换门或按钮的开关状态
 *
 * 职责：
 *   切换门或按钮的激活状态，并设置使用中标志
 *
 * 参数：
 *   @param activate   true=激活（打开），false=取消激活（关闭）
 *   @param alternative 是否使用替代状态（销毁状态）
 *
 * 状态切换逻辑：
 *   - 激活时：添加GO_FLAG_IN_USE标志
 *   - 取消激活时：移除GO_FLAG_IN_USE标志
 *   - 关闭状态 -> 激活/销毁状态
 *   - 激活/销毁状态 -> 关闭状态
 *
 * 调用时机：
 *   由UseDoorOrButton()或ResetDoorOrButton()内部调用
 *   不应直接外部调用
 */
void GameObject::SwitchDoorOrButton(bool activate, bool alternative /* = false */)
{
    if (activate)
        SetFlag(GO_FLAG_IN_USE);
    else
        RemoveFlag(GO_FLAG_IN_USE);

    if (GetGoState() == GO_STATE_READY)                      //if closed -> open
        SetGoState(alternative ? GO_STATE_DESTROYED : GO_STATE_ACTIVE);
    else                                                    //if open -> close
        SetGoState(GO_STATE_READY);
}

/**
 * @brief 使用游戏对象（玩家交互入口）
 *
 * 职责：
 *   处理玩家与游戏对象的交互逻辑，根据对象类型执行不同操作
 *
 * 参数：
 *   @param user 使用该游戏对象的单位（通常是玩家）
 *
 * 主要流程：
 *   1. 检查冷却时间和免疫状态
 *   2. 清理坐骑状态（如果对象不能在坐骑上使用）
 *   3. 触发AI的OnGossipHello事件
 *   4. 根据游戏对象类型执行特定逻辑：
 *      - DOOR/BUTTON: 打开/关闭门或按钮
 *      - QUESTGIVER: 显示任务对话菜单
 *      - TRAP: 触发陷阱施法
 *      - CHAIR: 让玩家坐在椅子上
 *      - GOOBER: 显示页面文本、对话、触发事件、任务进度
 *      - CAMERA: 播放过场动画
 *      - FISHINGNODE: 处理钓鱼逻辑
 *      - SUMMONING_RITUAL: 处理召唤仪式
 *      - SPELLCASTER: 施放法术
 *      - MEETINGSTONE: 集合石组队功能
 *      - FLAGSTAND/FLAGDROP: 战场旗帜交互
 *      - FISHINGHOLE: 钓鱼洞
 *      - BARBER_CHAIR: 理发椅
 *   5. 施放关联的法术（如果有）
 *
 * 调用时机：
 *   当玩家右键点击游戏对象或脚本触发使用时调用
 */
void GameObject::Use(Unit* user)
{
    // by default spell caster is user
    Unit* spellCaster = user;
    uint32 spellId = 0;
    bool triggered = false;

    if (Player* playerUser = user->ToPlayer())
    {
        if (m_goInfo->CannotBeUsedUnderImmunity() && playerUser->HasUnitFlag(UNIT_FLAG_IMMUNE))
            return;

        if (!m_goInfo->IsUsableMounted())
            playerUser->RemoveAurasByType(SPELL_AURA_MOUNTED);

        playerUser->PlayerTalkClass->ClearMenus();
        if (AI()->OnGossipHello(playerUser))
            return;
    }

    // If cooldown data present in template
    if (uint32 cooldown = GetGOInfo()->GetCooldown())
    {
        if (GameTime::GetGameTimeMS() < m_cooldownTime)
            return;

        m_cooldownTime = GameTime::GetGameTimeMS() + cooldown * IN_MILLISECONDS;
    }

    switch (GetGoType())
    {
        case GAMEOBJECT_TYPE_DOOR:                          //0
        case GAMEOBJECT_TYPE_BUTTON:                        //1
            //doors/buttons never really despawn, only reset to default state/flags
            UseDoorOrButton(0, false, user);
            return;
        case GAMEOBJECT_TYPE_QUESTGIVER:                    //2
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            player->PrepareGossipMenu(this, GetGOInfo()->questgiver.gossipID, true);
            player->SendPreparedGossip(this);
            return;
        }
        case GAMEOBJECT_TYPE_TRAP:                          //6
        {
            GameObjectTemplate const* goInfo = GetGOInfo();
            if (goInfo->trap.spellId)
                CastSpell(user, goInfo->trap.spellId);

            m_cooldownTime = GameTime::GetGameTimeMS() + (goInfo->trap.cooldown ? goInfo->trap.cooldown : uint32(4)) * IN_MILLISECONDS;   // template or 4 seconds

            if (goInfo->trap.type == 1)         // Deactivate after trigger
                SetLootState(GO_JUST_DEACTIVATED);

            return;
        }
        //Sitting: Wooden bench, chairs enzz
        case GAMEOBJECT_TYPE_CHAIR:                         //7
        {
            GameObjectTemplate const* info = GetGOInfo();
            if (!info)
                return;

            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            if (ChairListSlots.empty())        // this is called once at first chair use to make list of available slots
            {
                if (info->chair.slots > 0)     // sometimes chairs in DB have error in fields and we dont know number of slots
                    for (uint32 i = 0; i < info->chair.slots; ++i)
                        ChairListSlots[i].Clear(); // Last user of current slot set to 0 (none sit here yet)
                else
                    ChairListSlots[0].Clear();     // error in DB, make one default slot
            }

            Player* player = user->ToPlayer();

            // a chair may have n slots. we have to calculate their positions and teleport the player to the nearest one

            float lowestDist = DEFAULT_VISIBILITY_DISTANCE;

            uint32 nearest_slot = 0;
            float x_lowest = GetPositionX();
            float y_lowest = GetPositionY();

            // the object orientation + 1/2 pi
            // every slot will be on that straight line
            float orthogonalOrientation = GetOrientation() + float(M_PI) * 0.5f;
            // find nearest slot
            bool found_free_slot = false;
            for (ChairSlotAndUser::iterator itr = ChairListSlots.begin(); itr != ChairListSlots.end(); ++itr)
            {
                // the distance between this slot and the center of the go - imagine a 1D space
                float relativeDistance = (info->size*itr->first)-(info->size*(info->chair.slots-1)/2.0f);

                float x_i = GetPositionX() + relativeDistance * std::cos(orthogonalOrientation);
                float y_i = GetPositionY() + relativeDistance * std::sin(orthogonalOrientation);

                if (itr->second)
                {
                    if (Player* ChairUser = ObjectAccessor::GetPlayer(*this, itr->second))
                    {
                        if (ChairUser->IsSitState() && ChairUser->GetStandState() != UNIT_STAND_STATE_SIT && ChairUser->GetExactDist2d(x_i, y_i) < 0.1f)
                            continue;        // This seat is already occupied by ChairUser. NOTE: Not sure if the ChairUser->GetStandState() != UNIT_STAND_STATE_SIT check is required.
                        else
                            itr->second.Clear(); // This seat is unoccupied.
                    }
                    else
                        itr->second.Clear();     // The seat may of had an occupant, but they're offline.
                }

                found_free_slot = true;

                // calculate the distance between the player and this slot
                float thisDistance = player->GetDistance2d(x_i, y_i);

                if (thisDistance <= lowestDist)
                {
                    nearest_slot = itr->first;
                    lowestDist = thisDistance;
                    x_lowest = x_i;
                    y_lowest = y_i;
                }
            }

            if (found_free_slot)
            {
                ChairSlotAndUser::iterator itr = ChairListSlots.find(nearest_slot);
                if (itr != ChairListSlots.end())
                {
                    itr->second = player->GetGUID(); //this slot in now used by player
                    player->TeleportTo(GetMapId(), x_lowest, y_lowest, GetPositionZ(), GetOrientation(), TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET);
                    player->SetStandState(UnitStandStateType(UNIT_STAND_STATE_SIT_LOW_CHAIR + info->chair.height));
                    return;
                }
            }

            return;
        }
        //big gun, its a spell/aura
        case GAMEOBJECT_TYPE_GOOBER:                        //10
        {
            GameObjectTemplate const* info = GetGOInfo();

            if (Player* player = user->ToPlayer())
            {
                if (info->goober.pageId)                    // show page...
                {
                    WorldPacket data(SMSG_GAMEOBJECT_PAGETEXT, 8);
                    data << GetGUID();
                    player->SendDirectMessage(&data);
                }
                else if (info->goober.gossipID)
                {
                    player->PrepareGossipMenu(this, info->goober.gossipID);
                    player->SendPreparedGossip(this);
                }

                if (info->goober.eventId)
                {
                    TC_LOG_DEBUG("maps.script", "Goober ScriptStart id {} for GO entry {} (GUID {}).", info->goober.eventId, GetEntry(), GetSpawnId());
                    GetMap()->ScriptsStart(sEventScripts, info->goober.eventId, player, this);
                    EventInform(info->goober.eventId, user);
                }

                // possible quest objective for active quests
                if (info->goober.questId && sObjectMgr->GetQuestTemplate(info->goober.questId))
                {
                    //Quest require to be active for GO using
                    if (player->GetQuestStatus(info->goober.questId) != QUEST_STATUS_INCOMPLETE)
                        break;
                }

                if (Group* group = player->GetGroup())
                {
                    for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                        if (Player* member = itr->GetSource())
                            if (member->IsAtGroupRewardDistance(this))
                                member->KillCreditGO(info->entry, GetGUID());
                }
                else
                    player->KillCreditGO(info->entry, GetGUID());
            }

            if (uint32 trapEntry = info->goober.linkedTrapId)
                TriggeringLinkedGameObject(trapEntry, user);

            SetFlag(GO_FLAG_IN_USE);
            SetLootState(GO_ACTIVATED, user);

            // this appear to be ok, however others exist in addition to this that should have custom (ex: 190510, 188692, 187389)
            if (info->goober.customAnim)
                SendCustomAnim(GetGoAnimProgress());
            else
                SetGoState(GO_STATE_ACTIVE);

            m_cooldownTime = GameTime::GetGameTimeMS() + info->GetAutoCloseTime();

            // cast this spell later if provided
            spellId = info->goober.spellId;
            spellCaster = nullptr;

            break;
        }
        case GAMEOBJECT_TYPE_CAMERA:                        //13
        {
            GameObjectTemplate const* info = GetGOInfo();
            if (!info)
                return;

            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            if (info->camera.cinematicId)
                player->SendCinematicStart(info->camera.cinematicId);

            if (info->camera.eventID)
            {
                GetMap()->ScriptsStart(sEventScripts, info->camera.eventID, player, this);
                EventInform(info->camera.eventID, user);
            }

            return;
        }
        //fishing bobber
        case GAMEOBJECT_TYPE_FISHINGNODE:                   //17
        {
            Player* player = user->ToPlayer();
            if (!player)
                return;

            if (player->GetGUID() != GetOwnerGUID())
                return;

            switch (getLootState())
            {
                case GO_READY:                              // ready for loot
                {
                    uint32 zone, subzone;
                    GetZoneAndAreaId(zone, subzone);

                    int32 zone_skill = sObjectMgr->GetFishingBaseSkillLevel(subzone);
                    if (!zone_skill)
                        zone_skill = sObjectMgr->GetFishingBaseSkillLevel(zone);

                    //provide error, no fishable zone or area should be 0
                    if (!zone_skill)
                        TC_LOG_ERROR("sql.sql", "Fishable areaId {} are not properly defined in `skill_fishing_base_level`.", subzone);

                    int32 skill = player->GetSkillValue(SKILL_FISHING);

                    int32 chance;
                    if (skill < zone_skill)
                    {
                        chance = int32(pow((double)skill/zone_skill, 2) * 100);
                        if (chance < 1)
                            chance = 1;
                    }
                    else
                        chance = 100;

                    int32 roll = irand(1, 100);

                    TC_LOG_DEBUG("misc", "Fishing check (skill: {} zone min skill: {} chance {} roll: {}", skill, zone_skill, chance, roll);

                    player->UpdateFishingSkill();

                    /// @todo find reasonable value for fishing hole search
                    GameObject* fishingPool = LookupFishingHoleAround(20.0f + CONTACT_DISTANCE);

                    // If fishing skill is high enough, or if fishing on a pool, send correct loot.
                    // Fishing pools have no skill requirement as of patch 3.3.0 (undocumented change).
                    if (chance >= roll || fishingPool)
                    {
                        /// @todo I do not understand this hack. Need some explanation.
                        // prevent removing GO at spell cancel
                        RemoveFromOwner();
                        SetOwnerGUID(player->GetGUID());
                        SetSpellId(0); // prevent removing unintended auras at Unit::RemoveGameObject

                        if (fishingPool)
                        {
                            fishingPool->Use(player);
                            SetLootState(GO_JUST_DEACTIVATED);
                        }
                        else
                            player->SendLoot(GetGUID(), LOOT_FISHING);
                    }
                    else // If fishing skill is too low, send junk loot.
                        player->SendLoot(GetGUID(), LOOT_FISHING_JUNK);
                    break;
                }
                case GO_JUST_DEACTIVATED:                   // nothing to do, will be deleted at next update
                    break;
                default:
                {
                    SetLootState(GO_JUST_DEACTIVATED);

                    WorldPacket data(SMSG_FISH_NOT_HOOKED, 0);
                    player->SendDirectMessage(&data);
                    break;
                }
            }

            player->FinishSpell(CURRENT_CHANNELED_SPELL);
            return;
        }

        case GAMEOBJECT_TYPE_SUMMONING_RITUAL:              //18
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            Unit* owner = GetOwner();

            GameObjectTemplate const* info = GetGOInfo();

            Player* m_ritualOwner = nullptr;
            if (m_ritualOwnerGUID)
                m_ritualOwner = ObjectAccessor::FindPlayer(m_ritualOwnerGUID);

            // ritual owner is set for GO's without owner (not summoned)
            if (!m_ritualOwner && !owner)
            {
                m_ritualOwnerGUID = player->GetGUID();
                m_ritualOwner = player;
            }

            if (owner)
            {
                if (owner->GetTypeId() != TYPEID_PLAYER)
                    return;

                // accept only use by player from same group as owner, excluding owner itself (unique use already added in spell effect)
                if (player == owner->ToPlayer() || (info->summoningRitual.castersGrouped && !player->IsInSameRaidWith(owner->ToPlayer())))
                    return;

                // expect owner to already be channeling, so if not...
                if (!owner->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                    return;

                // in case summoning ritual caster is GO creator
                spellCaster = owner;
            }
            else
            {
                if (player != m_ritualOwner && (info->summoningRitual.castersGrouped && !player->IsInSameRaidWith(m_ritualOwner)))
                    return;

                spellCaster = player;
            }

            AddUniqueUse(player);

            if (info->summoningRitual.animSpell)
            {
                player->CastSpell(player, info->summoningRitual.animSpell, true);

                // for this case, summoningRitual.spellId is always triggered
                triggered = true;
            }

            // full amount unique participants including original summoner
            if (GetUniqueUseCount() == info->summoningRitual.reqParticipants)
            {
                if (m_ritualOwner)
                    spellCaster = m_ritualOwner;

                spellId = info->summoningRitual.spellId;

                if (spellId == 62330)                       // GO store nonexistent spell, replace by expected
                {
                    // spell have reagent and mana cost but it not expected use its
                    // it triggered spell in fact cast at currently channeled GO
                    spellId = 61993;
                    triggered = true;
                }

                // Cast casterTargetSpell at a random GO user
                // on the current DB there is only one gameobject that uses this (Ritual of Doom)
                // and its required target number is 1 (outter for loop will run once)
                if (info->summoningRitual.casterTargetSpell && info->summoningRitual.casterTargetSpell != 1) // No idea why this field is a bool in some cases
                    for (uint32 i = 0; i < info->summoningRitual.casterTargetSpellTargets; i++)
                        // m_unique_users can contain only player GUIDs
                        if (Player* target = ObjectAccessor::GetPlayer(*this, Trinity::Containers::SelectRandomContainerElement(m_unique_users)))
                            spellCaster->CastSpell(target, info->summoningRitual.casterTargetSpell, true);

                // finish owners spell
                if (owner)
                    owner->FinishSpell(CURRENT_CHANNELED_SPELL);

                // can be deleted now, if
                if (!info->summoningRitual.ritualPersistent)
                    SetLootState(GO_JUST_DEACTIVATED);
                else
                {
                    // reset ritual for this GO
                    m_ritualOwnerGUID.Clear();
                    m_unique_users.clear();
                    m_usetimes = 0;
                }
            }
            else
                return;

            // go to end function to spell casting
            break;
        }
        case GAMEOBJECT_TYPE_SPELLCASTER:                   //22
        {
            GameObjectTemplate const* info = GetGOInfo();
            if (!info)
                return;

            if (info->spellcaster.partyOnly)
            {
                Unit* caster = GetOwner();
                if (!caster || caster->GetTypeId() != TYPEID_PLAYER)
                    return;

                if (user->GetTypeId() != TYPEID_PLAYER || !user->ToPlayer()->IsInSameRaidWith(caster->ToPlayer()))
                    return;
            }

            user->RemoveAurasByType(SPELL_AURA_MOUNTED);
            spellId = info->spellcaster.spellId;

            AddUse();
            break;
        }
        case GAMEOBJECT_TYPE_MEETINGSTONE:                  //23
        {
            GameObjectTemplate const* info = GetGOInfo();

            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            Player* targetPlayer = ObjectAccessor::FindPlayer(player->GetTarget());

            // accept only use by player from same raid as caster, except caster itself
            if (!targetPlayer || targetPlayer == player || !targetPlayer->IsInSameRaidWith(player))
                return;

            //required lvl checks!
            uint8 level = player->GetLevel();
            if (level < info->meetingstone.minLevel)
                return;
            level = targetPlayer->GetLevel();
            if (level < info->meetingstone.minLevel)
                return;

            if (info->entry == 194097)
                spellId = 61994;                            // Ritual of Summoning
            else
                spellId = 59782;                            // Summoning Stone Effect

            break;
        }

        case GAMEOBJECT_TYPE_FLAGSTAND:                     // 24
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            if (player->CanUseBattlegroundObject(this))
            {
                // in battleground check
                Battleground* bg = player->GetBattleground();
                if (!bg)
                    return;

                if (player->GetVehicle())
                    return;

                player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
                player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
                // BG flag click
                // AB:
                // 15001
                // 15002
                // 15003
                // 15004
                // 15005
                bg->EventPlayerClickedOnFlag(player, this);
                return;                                     //we don;t need to delete flag ... it is despawned!
            }
            break;
        }

        case GAMEOBJECT_TYPE_FISHINGHOLE:                   // 25
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            player->SendLoot(GetGUID(), LOOT_FISHINGHOLE);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT, GetGOInfo()->entry);
            return;
        }

        case GAMEOBJECT_TYPE_FLAGDROP:                      // 26
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            if (player->CanUseBattlegroundObject(this))
            {
                // in battleground check
                Battleground* bg = player->GetBattleground();
                if (!bg)
                    return;

                if (player->GetVehicle())
                    return;

                player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
                player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
                // BG flag dropped
                // WS:
                // 179785 - Silverwing Flag
                // 179786 - Warsong Flag
                // EotS:
                // 184142 - Netherstorm Flag
                GameObjectTemplate const* info = GetGOInfo();
                if (info)
                {
                    switch (info->entry)
                    {
                        case 179785:                        // Silverwing Flag
                        case 179786:                        // Warsong Flag
                            if (bg->GetTypeID(true) == BATTLEGROUND_WS)
                                bg->EventPlayerClickedOnFlag(player, this);
                            break;
                        case 184142:                        // Netherstorm Flag
                            if (bg->GetTypeID(true) == BATTLEGROUND_EY)
                                bg->EventPlayerClickedOnFlag(player, this);
                            break;
                    }
                }
                //this cause to call return, all flags must be deleted here!!
                spellId = 0;
                Delete();
            }
            break;
        }
        case GAMEOBJECT_TYPE_BARBER_CHAIR:                  //32
        {
            GameObjectTemplate const* info = GetGOInfo();
            if (!info)
                return;

            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = user->ToPlayer();

            // fallback, will always work
            player->TeleportTo(GetMapId(), GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation(), TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET);

            WorldPacket data(SMSG_ENABLE_BARBER_SHOP, 0);
            player->SendDirectMessage(&data);

            player->SetStandState(UnitStandStateType(UNIT_STAND_STATE_SIT_LOW_CHAIR + info->barberChair.chairheight));
            return;
        }
        default:
            if (GetGoType() >= MAX_GAMEOBJECT_TYPE)
                TC_LOG_ERROR("misc", "GameObject::Use(): unit ({}, name: {}) tries to use object ({}, name: {}) of unknown type ({})",
                    user->GetGUID().ToString(), user->GetName(), GetGUID().ToString(), GetGOInfo()->name, GetGoType());
            break;
    }

    if (!spellId)
        return;

    if (!sSpellMgr->GetSpellInfo(spellId))
    {
        if (user->GetTypeId() != TYPEID_PLAYER || !sOutdoorPvPMgr->HandleCustomSpell(user->ToPlayer(), spellId, this))
            TC_LOG_ERROR("misc", "WORLD: unknown spell id {} at use action for gameobject (Entry: {} GoType: {})", spellId, GetEntry(), GetGoType());
        else
            TC_LOG_DEBUG("outdoorpvp", "WORLD: {} non-dbc spell was handled by OutdoorPvP", spellId);
        return;
    }

    if (Player* player = user->ToPlayer())
        sOutdoorPvPMgr->HandleCustomSpell(player, spellId, this);

    if (spellCaster)
        spellCaster->CastSpell(user, spellId, triggered);
    else
        CastSpell(user, spellId);
}

/**
 * @brief 发送自定义动画
 *
 * 职责：
 *   向周围玩家发送游戏对象的自定义动画包
 *
 * 参数：
 *   @param anim 动画ID
 *
 * 主要流程：
 *   1. 构建SMSG_GAMEOBJECT_CUSTOM_ANIM包
 *   2. 包含对象GUID和动画ID
 *   3. 发送给所有可见的玩家
 *
 * 调用时机：
 *   - 游戏对象激活时播放动画
 *   - 法术触发动画效果时
 *   - 脚本触发特定动画时
 */
void GameObject::SendCustomAnim(uint32 anim)
{
    WorldPacket data(SMSG_GAMEOBJECT_CUSTOM_ANIM, 8+4);
    data << GetGUID();
    data << uint32(anim);
    SendMessageToSet(&data, true);
}

/**
 * @brief 检查点是否在范围内
 *
 * 职责：
 *   判断指定坐标点是否在游戏对象的碰撞范围内
 *
 * 参数：
 *   @param x, y, z 目标坐标
 *   @param radius  额外半径
 *
 * 返回值：
 *   @return bool 在范围内返回true，否则返回false
 *
 * 主要流程：
 *   1. 获取显示信息
 *   2. 如果无显示信息，使用简单的距离检查
 *   3. 否则使用包围盒检查，考虑对象的旋转
 */
bool GameObject::IsInRange(float x, float y, float z, float radius) const
{
    GameObjectDisplayInfoEntry const* info = sGameObjectDisplayInfoStore.LookupEntry(m_goInfo->displayId);
    if (!info)
        return IsWithinDist3d(x, y, z, radius);

    float sinA = std::sin(GetOrientation());
    float cosA = std::cos(GetOrientation());
    float dx = x - GetPositionX();
    float dy = y - GetPositionY();
    float dz = z - GetPositionZ();
    float dist = std::sqrt(dx*dx + dy*dy);
    //! Check if the distance between the 2 objects is 0, can happen if both objects are on the same position.
    //! The code below this check wont crash if dist is 0 because 0/0 in float operations is valid, and returns infinite
    if (G3D::fuzzyEq(dist, 0.0f))
        return true;

    float sinB = dx / dist;
    float cosB = dy / dist;
    dx = dist * (cosA * cosB + sinA * sinB);
    dy = dist * (cosA * sinB - sinA * cosB);
    return dx < info->GeoBoxMax.X + radius && dx > info->GeoBoxMin.X - radius
        && dy < info->GeoBoxMax.Y + radius && dy > info->GeoBoxMin.Y - radius
        && dz < info->GeoBoxMax.Z + radius && dz > info->GeoBoxMin.Z - radius;
}

/**
 * @brief 触发事件通知
 *
 * 职责：
 *   通知AI、区域脚本和战场地图该游戏对象的事件被触发
 *
 * 参数：
 *   @param eventId  事件ID
 *   @param invoker  触发者（可选）
 *
 * 主要流程：
 *   1. 验证事件ID有效性
 *   2. 通知AI事件触发
 *   3. 通知区域脚本处理事件
 *   4. 如果是战场地图，通知战场处理事件
 *
 * 调用时机：
 *   - Goober类型游戏对象被使用时
 *   - 可破坏建筑状态改变时
 *   - 脚本触发特定事件时
 */
void GameObject::EventInform(uint32 eventId, WorldObject* invoker /*= nullptr*/)
{
    if (!eventId)
        return;

    if (AI())
        AI()->EventInform(eventId);

    if (GetZoneScript())
        GetZoneScript()->ProcessEvent(this, eventId);

    if (BattlegroundMap* bgMap = GetMap()->ToBattlegroundMap())
        if (bgMap->GetBG())
            bgMap->GetBG()->ProcessEvent(this, eventId, invoker);
}

/**
 * @brief 获取脚本ID
 *
 * 职责：
 *   获取游戏对象关联的脚本ID
 *
 * 返回值：
 *   @return uint32 脚本ID
 *
 * 优先级：
 *   1. 生成数据中的脚本ID
 *   2. 模板中的脚本ID
 */
uint32 GameObject::GetScriptId() const
{
    if (GameObjectData const* gameObjectData = GetGameObjectData())
        if (uint32 scriptId = gameObjectData->scriptId)
            return scriptId;

    return GetGOInfo()->ScriptId;
}

/**
 * @brief 获取本地化名称
 *
 * 职责：
 *   获取游戏对象在指定语言环境下的本地化名称
 *
 * 参数：
 *   @param loc_idx 语言环境索引
 *
 * 返回值：
 *   @return std::string const& 本地化名称的常量引用
 *
 * 主要流程：
 *   1. 如果不是默认语言，尝试获取本地化名称
 *   2. 找不到本地化名称则返回默认名称
 */
// overwrite WorldObject function for proper name localization
std::string const & GameObject::GetNameForLocaleIdx(LocaleConstant loc_idx) const
{
    if (loc_idx != DEFAULT_LOCALE)
    {
        uint8 uloc_idx = uint8(loc_idx);
        if (GameObjectLocale const* cl = sObjectMgr->GetGameObjectLocale(GetEntry()))
            if (cl->Name.size() > uloc_idx && !cl->Name[uloc_idx].empty())
                return cl->Name[uloc_idx];
    }

    return GetName();
}

/**
 * @brief 更新打包旋转数据
 *
 * 职责：
 *   将四元数旋转数据打包为64位整数格式，用于网络传输
 *
 * 打包算法：
 *   使用位运算将四元数的x、y、z分量压缩存储
 *   w分量通过符号位推断
 */
void GameObject::UpdatePackedRotation()
{
    static const int32 PACK_YZ = 1 << 20;
    static const int32 PACK_X = PACK_YZ << 1;

    static const int32 PACK_YZ_MASK = (PACK_YZ << 1) - 1;
    static const int32 PACK_X_MASK = (PACK_X << 1) - 1;

    int8 w_sign = (m_localRotation.w >= 0.f ? 1 : -1);
    int64 x = int32(m_localRotation.x * PACK_X)  * w_sign & PACK_X_MASK;
    int64 y = int32(m_localRotation.y * PACK_YZ) * w_sign & PACK_YZ_MASK;
    int64 z = int32(m_localRotation.z * PACK_YZ) * w_sign & PACK_YZ_MASK;
    m_packedRotation = z | (y << 21) | (x << 42);
}

/**
 * @brief 设置本地旋转
 *
 * 职责：
 *   设置游戏对象的本地旋转四元数
 *
 * 参数：
 *   @param qx, qy, qz, qw 四元数的四个分量
 *
 * 主要流程：
 *   1. 创建四元数并归一化
 *   2. 存储归一化后的值
 *   3. 更新打包旋转数据
 */
void GameObject::SetLocalRotation(float qx, float qy, float qz, float qw)
{
    G3D::Quat rotation(qx, qy, qz, qw);
    rotation.unitize();
    m_localRotation.x = rotation.x;
    m_localRotation.y = rotation.y;
    m_localRotation.z = rotation.z;
    m_localRotation.w = rotation.w;
    UpdatePackedRotation();
}

/**
 * @brief 设置父级旋转
 *
 * 职责：
 *   设置游戏对象的父级旋转（用于运输工具上的对象）
 *
 * 参数：
 *   @param rotation 四元数旋转数据
 *
 * 主要流程：
 *   将旋转数据存储到GAMEOBJECT_PARENTROTATION字段
 */
void GameObject::SetParentRotation(QuaternionData const& rotation)
{
    SetFloatValue(GAMEOBJECT_PARENTROTATION + 0, rotation.x);
    SetFloatValue(GAMEOBJECT_PARENTROTATION + 1, rotation.y);
    SetFloatValue(GAMEOBJECT_PARENTROTATION + 2, rotation.z);
    SetFloatValue(GAMEOBJECT_PARENTROTATION + 3, rotation.w);
}

/**
 * @brief 设置本地旋转角度
 *
 * 职责：
 *   从欧拉角设置游戏对象的本地旋转
 *
 * 参数：
 *   @param z_rot Z轴旋转（偏航）
 *   @param y_rot Y轴旋转（俯仰）
 *   @param x_rot X轴旋转（翻滚）
 *
 * 主要流程：
 *   将欧拉角转换为四元数后设置
 */
void GameObject::SetLocalRotationAngles(float z_rot, float y_rot, float x_rot)
{
    G3D::Quat quat(G3D::Matrix3::fromEulerAnglesZYX(z_rot, y_rot, x_rot));
    SetLocalRotation(quat.x, quat.y, quat.z, quat.w);
}

/**
 * @brief 获取世界旋转
 *
 * 职责：
 *   获取游戏对象在世界坐标系中的旋转
 *
 * 返回值：
 *   @return QuaternionData 世界旋转四元数
 *
 * 主要流程：
 *   如果在运输工具上，将本地旋转与运输工具的世界旋转组合
 *   否则直接返回本地旋转
 */
QuaternionData GameObject::GetWorldRotation() const
{
    QuaternionData localRotation = GetLocalRotation();
    if (Transport* transport = GetTransport())
    {
        QuaternionData worldRotation = transport->GetWorldRotation();

        G3D::Quat worldRotationQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w);
        G3D::Quat localRotationQuat(localRotation.x, localRotation.y, localRotation.z, localRotation.w);

        G3D::Quat resultRotation = localRotationQuat * worldRotationQuat;

        return QuaternionData(resultRotation.x, resultRotation.y, resultRotation.z, resultRotation.w);
    }
    return localRotation;
}

/**
 * @brief 修改可破坏建筑的生命值
 *
 * 职责：
 *   改变可破坏建筑的当前生命值，并触发相应的状态变化
 *
 * 参数：
 *   @param change           生命值变化量（负数为伤害，正数为治疗）
 *   @param attackerOrHealer 造成伤害或治疗的攻击者/治疗者
 *   @param spellId          造成伤害或治疗的法术ID
 *
 * 主要流程：
 *   1. 验证对象有最大生命值且变化量不为0
 *   2. 防止重复摧毁（生命值已为0时不再扣减）
 *   3. 计算并限制新生命值在[0, MaxHealth]范围内
 *   4. 更新动画进度条（255 * 生命值百分比）
 *   5. 发送伤害/治疗数据包给客户端
 *   6. 根据生命值百分比判断是否需要改变破坏状态：
 *      - 0%: 摧毁状态
 *      - <=damagedNumHits: 受损状态
 *      - 100%: 完好状态
 *
 * 调用时机：
 *   当可破坏建筑（城墙、大门等）受到伤害或被修复时调用
 *   由法术伤害系统或攻城武器系统触发
 */
void GameObject::ModifyHealth(int32 change, WorldObject* attackerOrHealer /*= nullptr*/, uint32 spellId /*= 0*/)
{
    if (!m_goValue.Building.MaxHealth || !change)
        return;

    // prevent double destructions of the same object
    if (change < 0 && !m_goValue.Building.Health)
        return;

    if (int32(m_goValue.Building.Health) + change <= 0)
        m_goValue.Building.Health = 0;
    else if (int32(m_goValue.Building.Health) + change >= int32(m_goValue.Building.MaxHealth))
        m_goValue.Building.Health = m_goValue.Building.MaxHealth;
    else
        m_goValue.Building.Health += change;

    // Set the health bar, value = 255 * healthPct;
    SetGoAnimProgress(m_goValue.Building.Health * 255 / m_goValue.Building.MaxHealth);

    // dealing damage, send packet
    if (Player* player = attackerOrHealer ? attackerOrHealer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr)
    {
        WorldPacket data(SMSG_DESTRUCTIBLE_BUILDING_DAMAGE, 8 + 8 + 8 + 4 + 4);
        data << GetPackGUID();
        data << attackerOrHealer->GetPackGUID();
        data << player->GetPackGUID();
        data << uint32(-change);                    // change  < 0 triggers SPELL_BUILDING_HEAL combat log event
                                                    // change >= 0 triggers SPELL_BUILDING_DAMAGE event
        data << uint32(spellId);
        player->SendDirectMessage(&data);
    }

    GameObjectDestructibleState newState = GetDestructibleState();

    if (!m_goValue.Building.Health)
        newState = GO_DESTRUCTIBLE_DESTROYED;
    else if (m_goValue.Building.Health <= GetGOInfo()->building.damagedNumHits)
        newState = GO_DESTRUCTIBLE_DAMAGED;
    else if (m_goValue.Building.Health == m_goValue.Building.MaxHealth)
        newState = GO_DESTRUCTIBLE_INTACT;

    if (newState == GetDestructibleState())
        return;

    SetDestructibleState(newState, attackerOrHealer, false);
}

/**
 * @brief 设置可破坏建筑的状态
 *
 * 职责：
 *   改变可破坏建筑（城墙、大门等）的状态，并更新模型和碰撞
 *
 * 参数：
 *   @param state             目标破坏状态
 *   @param attackerOrHealer  造成伤害或治疗的攻击者/治疗者
 *   @param setHealth         是否同时设置生命值
 *
 * 状态说明：
 *   - GO_DESTRUCTIBLE_INTACT: 完好状态
 *   - GO_DESTRUCTIBLE_DAMAGED: 受损状态
 *   - GO_DESTRUCTIBLE_DESTROYED: 摧毁状态
 *   - GO_DESTRUCTIBLE_REBUILDING: 重建中状态
 *
 * 主要流程：
 *   1. 验证对象类型必须是可破坏建筑
 *   2. 根据状态执行不同操作：
 *      - INTACT: 移除受损/摧毁标志，恢复完整模型，启用碰撞
 *      - DAMAGED: 触发受损事件，设置受损标志，切换到受损模型
 *      - DESTROYED: 触发摧毁事件，设置摧毁标志，切换到摧毁模型，禁用碰撞
 *      - REBUILDING: 触发重建事件，移除标志，切换到重建模型，启用碰撞
 *   3. 如果指定setHealth，同步更新生命值
 *
 * 调用时机：
 *   当可破坏建筑受到伤害、被摧毁或开始重建时调用
 *   通常在ModifyHealth()中根据生命值百分比自动触发
 */
void GameObject::SetDestructibleState(GameObjectDestructibleState state, WorldObject* attackerOrHealer /*= nullptr*/, bool setHealth /*= false*/)
{
    // the user calling this must know he is already operating on destructible gameobject
    ASSERT(GetGoType() == GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING);

    switch (state)
    {
        case GO_DESTRUCTIBLE_INTACT:
            RemoveFlag(GO_FLAG_DAMAGED | GO_FLAG_DESTROYED);
            SetDisplayId(m_goInfo->displayId);
            if (setHealth)
            {
                m_goValue.Building.Health = m_goValue.Building.MaxHealth;
                SetGoAnimProgress(255);
            }
            EnableCollision(true);
            break;
        case GO_DESTRUCTIBLE_DAMAGED:
        {
            EventInform(m_goInfo->building.damagedEvent, attackerOrHealer);
            AI()->Damaged(attackerOrHealer, m_goInfo->building.damagedEvent);

            RemoveFlag(GO_FLAG_DESTROYED);
            SetFlag(GO_FLAG_DAMAGED);

            uint32 modelId = m_goInfo->displayId;
            if (DestructibleModelDataEntry const* modelData = sDestructibleModelDataStore.LookupEntry(m_goInfo->building.destructibleData))
                if (modelData->State1Wmo)
                    modelId = modelData->State1Wmo;
            SetDisplayId(modelId);

            if (setHealth)
            {
                m_goValue.Building.Health = m_goInfo->building.damagedNumHits;
                uint32 maxHealth = m_goValue.Building.MaxHealth;
                // in this case current health is 0 anyway so just prevent crashing here
                if (!maxHealth)
                    maxHealth = 1;
                SetGoAnimProgress(m_goValue.Building.Health * 255 / maxHealth);
            }
            break;
        }
        case GO_DESTRUCTIBLE_DESTROYED:
        {
            EventInform(m_goInfo->building.destroyedEvent, attackerOrHealer);
            AI()->Destroyed(attackerOrHealer, m_goInfo->building.destroyedEvent);

            if (Player* player = attackerOrHealer ? attackerOrHealer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr)
                if (Battleground* bg = player->GetBattleground())
                    bg->DestroyGate(player, this);

            RemoveFlag(GO_FLAG_DAMAGED);
            SetFlag(GO_FLAG_DESTROYED);

            uint32 modelId = m_goInfo->displayId;
            if (DestructibleModelDataEntry const* modelData = sDestructibleModelDataStore.LookupEntry(m_goInfo->building.destructibleData))
                if (modelData->State2Wmo)
                    modelId = modelData->State2Wmo;
            SetDisplayId(modelId);

            if (setHealth)
            {
                m_goValue.Building.Health = 0;
                SetGoAnimProgress(0);
            }
            EnableCollision(false);
            break;
        }
        case GO_DESTRUCTIBLE_REBUILDING:
        {
            EventInform(m_goInfo->building.rebuildingEvent, attackerOrHealer);
            RemoveFlag(GO_FLAG_DAMAGED | GO_FLAG_DESTROYED);

            uint32 modelId = m_goInfo->displayId;
            if (DestructibleModelDataEntry const* modelData = sDestructibleModelDataStore.LookupEntry(m_goInfo->building.destructibleData))
                if (modelData->State3Wmo)
                    modelId = modelData->State3Wmo;
            SetDisplayId(modelId);

            // restores to full health
            if (setHealth)
            {
                m_goValue.Building.Health = m_goValue.Building.MaxHealth;
                SetGoAnimProgress(255);
            }
            EnableCollision(true);
            break;
        }
    }
}

/**
 * @brief 设置战利品状态
 *
 * 职责：
 *   改变游戏对象的战利品状态，用于控制对象的生命周期和交互行为
 *
 * 参数：
 *   @param state 目标战利品状态
 *   @param unit  触发状态改变的单位（可选）
 *
 * 状态说明：
 *   - GO_NOT_READY: 对象未就绪（正在初始化或等待）
 *   - GO_READY: 对象就绪，可以被激活
 *   - GO_ACTIVATED: 对象已激活，正在使用中
 *   - GO_JUST_DEACTIVATED: 对象刚失活，准备消失或重置
 *
 * 主要流程：
 *   1. 设置新的战利品状态
 *   2. 记录触发单位（用于陷阱等需要目标的场景）
 *   3. 通知AI战利品状态改变
 *   4. 对于宝箱类型，启动补货计时器
 *   5. 根据状态启用或禁用碰撞
 *
 * 调用时机：
 *   当游戏对象需要改变其可用状态时调用
 *   例如：宝箱被打开、陷阱被触发、门被激活等
 */
void GameObject::SetLootState(LootState state, Unit* unit)
{
    m_lootState = state;
    if (unit)
        m_lootStateUnitGUID = unit->GetGUID();
    else
        m_lootStateUnitGUID.Clear();

    AI()->OnLootStateChanged(state, unit);

    // Start restock timer if the chest is partially looted or not looted at all
    if (GetGoType() == GAMEOBJECT_TYPE_CHEST && state == GO_ACTIVATED && GetGOInfo()->chest.chestRestockTime > 0 && m_restockTime == 0)
        m_restockTime = GameTime::GetGameTime() + GetGOInfo()->chest.chestRestockTime;

    if (GetGoType() == GAMEOBJECT_TYPE_DOOR) // only set collision for doors on SetGoState
        return;

    if (m_model)
    {
        bool collision = false;
        // Use the current go state
        if ((GetGoState() != GO_STATE_READY && (state == GO_ACTIVATED || state == GO_JUST_DEACTIVATED)) || state == GO_READY)
            collision = !collision;

        EnableCollision(collision);
    }
}

/**
 * @brief 设置战利品生成时间
 *
 * 职责：
 *   记录战利品生成的时间戳
 *
 * 用途：
 *   用于防止战利品重复生成和调试
 */
void GameObject::SetLootGenerationTime()
{
    m_lootGenerationTime = GameTime::GetGameTime();
}

/**
 * @brief 设置游戏对象状态
 *
 * 职责：
 *   改变游戏对象的激活状态，并更新碰撞检测
 *
 * 参数：
 *   @param state 目标状态（GO_STATE_READY=关闭, GO_STATE_ACTIVE=激活, GO_STATE_DESTROYED=销毁）
 *
 * 主要流程：
 *   1. 设置GAMEOBJECT_BYTES_1字段的状态字节
 *   2. 通知AI状态改变事件
 *   3. 根据状态启用或禁用碰撞：
 *      - GO_STATE_READY: 启用碰撞（关闭状态，有障碍）
 *      - 其他状态: 禁用碰撞（打开状态，可穿过）
 *
 * 注意事项：
 *   - 运输工具不处理碰撞
 *   - 仅在世界中时才更新碰撞模型
 *
 * 调用时机：
 *   门、按钮等对象需要切换开/关状态时调用
 */
void GameObject::SetGoState(GOState state)
{
    SetByteValue(GAMEOBJECT_BYTES_1, 0, state);
    if (AI())
        AI()->OnStateChanged(state);
    if (m_model && !IsTransport())
    {
        if (!IsInWorld())
            return;

        // startOpen determines whether we are going to add or remove the LoS on activation
        bool collision = false;
        if (state == GO_STATE_READY)
            collision = !collision;

        EnableCollision(collision);
    }
}

/**
 * @brief 获取运输工具的运行周期
 *
 * 职责：
 *   获取运输工具完成一次完整路径所需的时间
 *
 * 返回值：
 *   @return uint32 周期时间（毫秒），无动画信息返回0
 *
 * 注意：
 *   仅适用于TRANSPORT类型的游戏对象
 */
uint32 GameObject::GetTransportPeriod() const
{
    ASSERT(GetGOInfo()->type == GAMEOBJECT_TYPE_TRANSPORT);
    if (m_goValue.Transport.AnimationInfo)
        return m_goValue.Transport.AnimationInfo->TotalTime;

    return 0;
}

/**
 * @brief 设置显示ID
 *
 * 职责：
 *   改变游戏对象的模型显示ID
 *
 * 参数：
 *   @param displayid 新的显示ID
 *
 * 主要流程：
 *   1. 设置GAMEOBJECT_DISPLAYID字段
 *   2. 更新碰撞模型
 *
 * 调用时机：
 *   可破坏建筑状态改变时、脚本强制改变外观时
 */
void GameObject::SetDisplayId(uint32 displayid)
{
    SetUInt32Value(GAMEOBJECT_DISPLAYID, displayid);
    UpdateModel();
}

/**
 * @brief 设置相位掩码
 *
 * 职责：
 *   改变游戏对象的相位可见性
 *
 * 参数：
 *   @param newPhaseMask 新的相位掩码
 *   @param update       是否立即更新
 *
 * 主要流程：
 *   1. 调用父类设置方法
 *   2. 如果碰撞模型已启用，重新启用碰撞（更新相位）
 */
void GameObject::SetPhaseMask(uint32 newPhaseMask, bool update)
{
    WorldObject::SetPhaseMask(newPhaseMask, update);
    if (m_model && m_model->isEnabled())
        EnableCollision(true);
}

/**
 * @brief 启用或禁用碰撞检测
 *
 * 职责：
 *   控制游戏对象的物理碰撞是否生效
 *
 * 参数：
 *   @param enable true=启用碰撞，false=禁用碰撞
 *
 * 主要流程：
 *   1. 检查碰撞模型是否存在
 *   2. 调用模型的enable方法：
 *      - 启用时：传入相位掩码，使碰撞对相应相位的玩家生效
 *      - 禁用时：传入0，完全禁用碰撞
 *
 * 调用时机：
 *   - 门/按钮状态改变时（打开禁用碰撞，关闭启用碰撞）
 *   - 游戏对象添加到世界时
 *   - 可破坏建筑被摧毁时（禁用碰撞）
 *   - 可破坏建筑重建时（启用碰撞）
 *
 * 注意事项：
 *   碰撞模型使用V地图数据，启用/禁用是即时生效的
 */
void GameObject::EnableCollision(bool enable)
{
    if (!m_model)
        return;

    /*if (enable && !GetMap()->ContainsGameObjectModel(*m_model))
        GetMap()->InsertGameObjectModel(*m_model);*/

    m_model->enable(enable ? GetPhaseMask() : 0);
}

/**
 * @brief 更新碰撞模型
 *
 * 职责：
 *   重新加载并更新游戏对象的碰撞模型
 *
 * 主要流程：
 *   1. 检查是否在世界中
 *   2. 移除旧模型
 *   3. 删除旧模型内存
 *   4. 创建新模型
 *   5. 将新模型添加到地图
 *
 * 调用时机：
 *   显示ID改变时、需要重新加载模型时
 */
void GameObject::UpdateModel()
{
    if (!IsInWorld())
        return;
    if (m_model)
        if (GetMap()->ContainsGameObjectModel(*m_model))
            GetMap()->RemoveGameObjectModel(*m_model);
    delete m_model;
    CreateModel();
    if (m_model)
        GetMap()->InsertGameObjectModel(*m_model);
}

/**
 * @brief 获取拾取接收者
 *
 * 职责：
 *   获取有权拾取该游戏对象战利品的玩家
 *
 * 返回值：
 *   @return Player* 拾取者玩家指针，不存在返回nullptr
 */
Player* GameObject::GetLootRecipient() const
{
    if (!m_lootRecipient)
        return nullptr;
    return ObjectAccessor::FindConnectedPlayer(m_lootRecipient);
}

/**
 * @brief 获取拾取接收者队伍
 *
 * 职责：
 *   获取有权拾取该游戏对象战利品的队伍
 *
 * 返回值：
 *   @return Group* 队伍指针，不存在返回nullptr
 */
Group* GameObject::GetLootRecipientGroup() const
{
    if (!m_lootRecipientGroup)
        return nullptr;
    return sGroupMgr->GetGroupByGUID(m_lootRecipientGroup);
}

/**
 * @brief 设置战利品接收者
 *
 * 职责：
 *   设置有权拾取该游戏对象战利品的玩家和队伍
 *
 * 参数：
 *   @param unit  有权拾取的单位（玩家或载具）
 *   @param group 所属队伍（可选，如果不提供则从玩家获取）
 *
 * 主要流程：
 *   1. 如果unit为空，清除拾取者但保留队伍
 *   2. 验证unit必须是玩家或载具
 *   3. 获取玩家的真实对象（处理魅惑/所有者关系）
 *   4. 设置拾取者GUID
 *   5. 设置队伍GUID（优先使用传入参数，否则从玩家获取）
 *
 * 调用时机：
 *   - 玩家激活宝箱时
 *   - 钓鱼成功时
 *   - 某些特殊的拾取规则需要时
 *
 * 战利品权限规则：
 *   - 单人：只有拾取者本人可以拾取
 *   - 组队：队伍中所有成员可以拾取（根据分配规则）
 */
void GameObject::SetLootRecipient(Unit* unit, Group* group)
{
    // set the player whose group should receive the right
    // to loot the creature after it dies
    // should be set to nullptr after the loot disappears

    if (!unit)
    {
        m_lootRecipient.Clear();
        m_lootRecipientGroup = group ? group->GetLowGUID() : 0;
        return;
    }

    if (unit->GetTypeId() != TYPEID_PLAYER && !unit->IsVehicle())
        return;

    Player* player = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)                                             // normal creature, no player involved
        return;

    m_lootRecipient = player->GetGUID();

    // either get the group from the passed parameter or from unit's one
    if (group)
        m_lootRecipientGroup = group->GetLowGUID();
    else if (Group* unitGroup = player->GetGroup())
        m_lootRecipientGroup = unitGroup->GetLowGUID();
}

/**
 * @brief 检查玩家是否被允许拾取
 *
 * 职责：
 *   判断指定玩家是否有权拾取该游戏对象的战利品
 *
 * 参数：
 *   @param player 目标玩家
 *
 * 返回值：
 *   @return bool 允许拾取返回true，否则返回false
 *
 * 拾取权限规则：
 *   - 无拾取者和队伍：所有人可拾取
 *   - 是拾取者本人：可拾取
 *   - 是拾取者队伍成员：可拾取
 *   - 其他情况：不可拾取
 */
bool GameObject::IsLootAllowedFor(Player const* player) const
{
    if (!m_lootRecipient && !m_lootRecipientGroup)
        return true;

    if (player->GetGUID() == m_lootRecipient)
        return true;

    Group const* playerGroup = player->GetGroup();
    if (!playerGroup || playerGroup != GetLootRecipientGroup()) // if we dont have a group we arent the recipient
        return false;                                           // if go doesnt have group bound it means it was solo killed by someone else

    return true;
}

/**
 * @brief 获取关联的陷阱对象
 *
 * 职责：
 *   获取与该游戏对象关联的陷阱游戏对象
 *
 * 返回值：
 *   @return GameObject* 关联的陷阱指针，不存在返回nullptr
 */
GameObject* GameObject::GetLinkedTrap()
{
    return ObjectAccessor::GetGameObject(*this, m_linkedTrap);
}

/**
 * @brief 构建字段值更新包
 *
 * 职责：
 *   为指定玩家构建游戏对象的字段值更新数据
 *
 * 参数：
 *   @param updateType 更新类型
 *   @param data       输出的字节缓冲区
 *   @param target     目标玩家
 *
 * 主要流程：
 *   1. 验证目标玩家存在
 *   2. 确定可见性标志（公开或所有者）
 *   3. 遍历所有字段，构建更新掩码
 *   4. 特殊处理：
 *      - GAMEOBJECT_DYNAMIC: 动态标志，根据任务状态设置高亮
 *      - GAMEOBJECT_FLAGS: 对象标志，根据拾取权限设置锁定标志
 *   5. 将字段数据写入缓冲区
 *
 * 特殊字段处理：
 *   - 动态标志：任务激活、运输工具进度等
 *   - 对象标志：锁定、不可选择、使用中等
 *
 * 调用时机：
 *   当需要向客户端发送游戏对象状态更新时调用
 *   由UpdateData系统调用
 */
void GameObject::BuildValuesUpdate(uint8 updateType, ByteBuffer* data, Player const* target) const
{
    if (!target)
        return;

    bool forcedFlags = GetGoType() == GAMEOBJECT_TYPE_CHEST && GetGOInfo()->chest.groupLootRules && HasLootRecipient();
    bool targetIsGM = target->IsGameMaster();

    ByteBuffer fieldBuffer;

    UpdateMaskPacketBuilder updateMask(m_valuesCount);

    uint32* flags = GameObjectUpdateFieldFlags;
    uint32 visibleFlag = UF_FLAG_PUBLIC;
    if (GetOwnerGUID() == target->GetGUID())
        visibleFlag |= UF_FLAG_OWNER;

    for (uint16 index = 0; index < m_valuesCount; ++index)
    {
        if (_fieldNotifyFlags & flags[index] ||
            ((updateType == UPDATETYPE_VALUES ? _changesMask.GetBit(index) : m_uint32Values[index]) && (flags[index] & visibleFlag)) ||
            (index == GAMEOBJECT_FLAGS && forcedFlags))
        {
            updateMask.SetBit(index);

            if (index == GAMEOBJECT_DYNAMIC)
            {
                uint16 dynFlags = 0;
                int16 pathProgress = -1;
                switch (GetGoType())
                {
                    case GAMEOBJECT_TYPE_QUESTGIVER:
                        if (ActivateToQuest(target))
                            dynFlags |= GO_DYNFLAG_LO_ACTIVATE;
                        break;
                    case GAMEOBJECT_TYPE_CHEST:
                    case GAMEOBJECT_TYPE_GOOBER:
                        if (ActivateToQuest(target))
                            dynFlags |= GO_DYNFLAG_LO_ACTIVATE | GO_DYNFLAG_LO_SPARKLE;
                        else if (targetIsGM)
                            dynFlags |= GO_DYNFLAG_LO_ACTIVATE;
                        break;
                    case GAMEOBJECT_TYPE_GENERIC:
                        if (ActivateToQuest(target))
                            dynFlags |= GO_DYNFLAG_LO_SPARKLE;
                        break;
                    case GAMEOBJECT_TYPE_TRANSPORT:
                    case GAMEOBJECT_TYPE_MO_TRANSPORT:
                    {
                        if (uint32 transportPeriod = GetTransportPeriod())
                        {
                            float timer = float(m_goValue.Transport.PathProgress % transportPeriod);
                            pathProgress = int16(timer / float(transportPeriod) * 65535.0f);
                        }
                        break;
                    }
                    default:
                        break;
                }

                fieldBuffer << uint16(dynFlags);
                fieldBuffer << int16(pathProgress);
            }
            else if (index == GAMEOBJECT_FLAGS)
            {
                uint32 goFlags = m_uint32Values[GAMEOBJECT_FLAGS];
                if (GetGoType() == GAMEOBJECT_TYPE_CHEST)
                    if (GetGOInfo()->chest.groupLootRules && !IsLootAllowedFor(target))
                        goFlags |= GO_FLAG_LOCKED | GO_FLAG_NOT_SELECTABLE;

                fieldBuffer << goFlags;
            }
            else
                fieldBuffer << m_uint32Values[index];                // other cases
        }
    }

    updateMask.AppendToPacket(data);
    data->append(fieldBuffer);
}

/**
 * @brief 获取重生位置
 *
 * 职责：
 *   获取游戏对象的重生点位置
 *
 * 参数：
 *   @param x, y, z 输出的坐标
 *   @param ori     输出的朝向（可选）
 *
 * 主要流程：
 *   如果有生成数据，返回生成点位置
 *   否则返回当前位置
 */
void GameObject::GetRespawnPosition(float &x, float &y, float &z, float* ori /* = nullptr*/) const
{
    if (m_goData)
    {
        if (ori)
            m_goData->spawnPoint.GetPosition(x, y, z, *ori);
        else
            m_goData->spawnPoint.GetPosition(x, y, z);
    }
    else
    {
        if (ori)
            GetPosition(x, y, z, *ori);
        else
            GetPosition(x, y, z);
    }
}

/**
 * @brief 获取交互距离
 *
 * 职责：
 *   返回玩家与游戏对象交互所需的最大距离
 *
 * 返回值：
 *   @return float 交互距离（码）
 *
 * 距离规则（根据游戏对象类型）：
 *   - AREADAMAGE: 0.0（不可交互）
 *   - QUESTGIVER/TEXT/FLAGSTAND/FLAGDROP/MINI_GAME: 5.55
 *   - BINDER: 10.0
 *   - CHAIR/BARBER_CHAIR: 3.0
 *   - FISHINGNODE: 100.0（钓鱼浮标允许远距离）
 *   - FISHINGHOLE: 20.0 + CONTACT_DISTANCE
 *   - CAMERA/MAP_OBJECT/DUNGEON_DIFFICULTY/DESTRUCTIBLE_BUILDING/DOOR: 5.0
 *   - GUILD_BANK/MAILBOX: 10.0（非标准值，为了可靠性增加）
 *   - 其他: INTERACTION_DISTANCE (默认值)
 *
 * 调用时机：
 *   检查玩家是否可以与游戏对象交互时调用
 */
float GameObject::GetInteractionDistance() const
{
    switch (GetGoType())
    {
        case GAMEOBJECT_TYPE_AREADAMAGE:
            return 0.0f;
        case GAMEOBJECT_TYPE_QUESTGIVER:
        case GAMEOBJECT_TYPE_TEXT:
        case GAMEOBJECT_TYPE_FLAGSTAND:
        case GAMEOBJECT_TYPE_FLAGDROP:
        case GAMEOBJECT_TYPE_MINI_GAME:
            return 5.5555553f;
        case GAMEOBJECT_TYPE_BINDER:
            return 10.0f;
        case GAMEOBJECT_TYPE_CHAIR:
        case GAMEOBJECT_TYPE_BARBER_CHAIR:
            return 3.0f;
        case GAMEOBJECT_TYPE_FISHINGNODE:
            return 100.0f;
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            return 20.0f + CONTACT_DISTANCE; // max spell range
        case GAMEOBJECT_TYPE_CAMERA:
        case GAMEOBJECT_TYPE_MAP_OBJECT:
        case GAMEOBJECT_TYPE_DUNGEON_DIFFICULTY:
        case GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING:
        case GAMEOBJECT_TYPE_DOOR:
            return 5.0f;
        // Following values are not blizzlike
        case GAMEOBJECT_TYPE_GUILD_BANK:
        case GAMEOBJECT_TYPE_MAILBOX:
            // Successful mailbox interaction is rather critical to the client, failing it will start a minute-long cooldown until the next mail query may be executed.
            // And since movement info update is not sent with mailbox interaction query, server may find the player outside of interaction range. Thus we increase it.
            return 10.0f; // 5.0f is blizzlike
        default:
            return INTERACTION_DISTANCE;
    }
}

/**
 * @brief 更新模型位置
 *
 * 职责：
 *   当游戏对象位置改变时更新碰撞模型位置
 *
 * 主要流程：
 *   1. 检查模型存在性
 *   2. 从地图移除模型
 *   3. 更新模型位置
 *   4. 重新添加到地图
 *
 * 调用时机：
 *   游戏对象被传送或移动后
 */
void GameObject::UpdateModelPosition()
{
    if (!m_model)
        return;

    if (GetMap()->ContainsGameObjectModel(*m_model))
    {
        GetMap()->RemoveGameObjectModel(*m_model);
        m_model->UpdatePosition();
        GetMap()->InsertGameObjectModel(*m_model);
    }
}

class GameObjectModelOwnerImpl : public GameObjectModelOwnerBase
{
public:
    explicit GameObjectModelOwnerImpl(GameObject* owner) : _owner(owner) { }

    bool IsSpawned() const override { return _owner->isSpawned(); }
    uint32 GetDisplayId() const override { return _owner->GetDisplayId(); }
    uint32 GetPhaseMask() const override { return _owner->GetPhaseMask(); }
    G3D::Vector3 GetPosition() const override { return G3D::Vector3(_owner->GetPositionX(), _owner->GetPositionY(), _owner->GetPositionZ()); }
    float GetOrientation() const override { return _owner->GetOrientation(); }
    float GetScale() const override { return _owner->GetObjectScale(); }
    void DebugVisualizeCorner(G3D::Vector3 const& corner) const override { const_cast<GameObject*>(_owner)->SummonCreature(1, corner.x, corner.y, corner.z, 0, TEMPSUMMON_MANUAL_DESPAWN); }

private:
    GameObject* _owner;
};

/**
 * @brief 创建碰撞模型
 *
 * 职责：
 *   从V地图数据创建游戏对象的碰撞模型
 *
 * 主要流程：
 *   使用模型所有者实现类创建GameObjectModel实例
 *
 * 调用时机：
 *   游戏对象创建时、模型更新时
 */
void GameObject::CreateModel()
{
    m_model = GameObjectModel::Create(std::make_unique<GameObjectModelOwnerImpl>(this), sWorld->GetDataPath());
}

/**
 * @brief 获取调试信息
 *
 * 职责：
 *   生成游戏对象的调试信息字符串
 *
 * 返回值：
 *   @return std::string 调试信息字符串
 *
 * 包含内容：
 *   WorldObject信息、SpawnId、GoState、ScriptId、AIName
 */
std::string GameObject::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << WorldObject::GetDebugInfo() << "\n"
        << "SpawnId: " << GetSpawnId() << " GoState: " << std::to_string(GetGoState()) << " ScriptId: " << GetScriptId() << " AIName: " << GetAIName();
    return sstr.str();
}

/**
 * @brief 检查玩家是否在交互距离内
 *
 * 职责：
 *   判断玩家是否可以与游戏对象交互
 *
 * 参数：
 *   @param player 目标玩家
 *   @param spell  用于开锁的法术信息（可选）
 *
 * 返回值：
 *   @return bool 在交互距离内返回true，否则返回false
 *
 * 主要流程：
 *   1. 如果有法术，使用法术最大距离
 *   2. 对于SPELL_FOCUS类型，使用距离平方比较
 *   3. 其他情况使用标准交互距离检查
 */
bool GameObject::IsAtInteractDistance(Player const* player, SpellInfo const* spell) const
{
    if (spell || (spell = GetSpellForLock(player)))
    {
        float maxRange = spell->GetMaxRange(spell->IsPositive());

        if (GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS)
            return maxRange * maxRange >= GetExactDistSq(player);

        if (sGameObjectDisplayInfoStore.LookupEntry(GetGOInfo()->displayId))
            return IsAtInteractDistance(*player, maxRange);
    }

    return IsAtInteractDistance(*player, GetInteractionDistance());
}

/**
 * @brief 检查位置是否在交互距离内
 *
 * 职责：
 *   判断指定位置是否在游戏对象的交互范围内
 *
 * 参数：
 *   @param pos    目标位置
 *   @param radius 交互半径
 *
 * 返回值：
 *   @return bool 在交互距离内返回true，否则返回false
 *
 * 主要流程：
 *   1. 如果有显示信息，使用旋转后的包围盒检查
 *   2. 否则使用简单的距离检查
 */
bool GameObject::IsAtInteractDistance(Position const& pos, float radius) const
{
    if (GameObjectDisplayInfoEntry const* displayInfo = sGameObjectDisplayInfoStore.LookupEntry(GetGOInfo()->displayId))
    {
        float scale = GetObjectScale();

        float minX = displayInfo->GeoBoxMin.X * scale - radius;
        float minY = displayInfo->GeoBoxMin.Y * scale - radius;
        float minZ = displayInfo->GeoBoxMin.Z * scale - radius;
        float maxX = displayInfo->GeoBoxMax.X * scale + radius;
        float maxY = displayInfo->GeoBoxMax.Y * scale + radius;
        float maxZ = displayInfo->GeoBoxMax.Z * scale + radius;

        QuaternionData worldRotation = GetWorldRotation();
        G3D::Quat worldRotationQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w);

        return G3D::CoordinateFrame { { worldRotationQuat }, { GetPositionX(), GetPositionY(), GetPositionZ() } }
                .toWorldSpace(G3D::Box { { minX, minY, minZ }, { maxX, maxY, maxZ } })
                .contains({ pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() });
    }

    return GetExactDist(&pos) <= radius;
}

/**
 * @brief 检查玩家是否在地图交互距离内
 *
 * 职责：
 *   综合检查玩家是否可以与游戏对象交互
 *
 * 参数：
 *   @param player 目标玩家
 *
 * 返回值：
 *   @return bool 可以交互返回true，否则返回false
 *
 * 检查条件：
 *   - 在同一地图
 *   - 相位相同
 *   - 在交互距离内
 */
bool GameObject::IsWithinDistInMap(Player const* player) const
{
    return IsInMap(player) && InSamePhase(player) && IsAtInteractDistance(player);
}

/**
 * @brief 获取用于开锁的法术
 *
 * 职责：
 *   查找玩家可以用来打开游戏对象锁的法术
 *
 * 参数：
 *   @param player 目标玩家
 *
 * 返回值：
 *   @return SpellInfo const* 可用的开锁法术，无则返回nullptr
 *
 * 主要流程：
 *   1. 获取对象的锁ID
 *   2. 遍历锁的所有开锁方式
 *   3. 检查是否有匹配的法术（直接法术或技能法术）
 *
 * 开锁类型：
 *   - LOCK_KEY_SPELL: 需要特定法术
 *   - LOCK_KEY_SKILL: 需要特定技能和法术（如开锁技能）
 */
SpellInfo const* GameObject::GetSpellForLock(Player const* player) const
{
    if (!player)
        return nullptr;

    uint32 lockId = GetGOInfo()->GetLockId();
    if (!lockId)
        return nullptr;

    LockEntry const* lock = sLockStore.LookupEntry(lockId);
    if (!lock)
        return nullptr;

    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (!lock->Type[i])
            continue;

        if (lock->Type[i] == LOCK_KEY_SPELL)
            if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(lock->Index[i]))
                return spell;

        if (lock->Type[i] != LOCK_KEY_SKILL)
            break;

        for (auto&& playerSpell : player->GetSpellMap())
            if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(playerSpell.first))
                for (SpellEffectInfo const& spellEffectInfo : spell->GetEffects())
                    if (spellEffectInfo.IsEffect(SPELL_EFFECT_OPEN_LOCK) && ((uint32) spellEffectInfo.MiscValue) == lock->Index[i])
                        if (spellEffectInfo.CalcValue(player) >= int32(lock->Skill[i]))
                            return spell;
    }

    return nullptr;
}
