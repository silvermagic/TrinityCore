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
 * @file Vehicle.cpp
 * @brief 载具系统实现 - 管理游戏中的载具实体及其乘客系统
 *
 * 本文件实现了 TrinityCore 的载具系统，提供以下核心功能：
 *
 * 1. 载具生命周期管理
 *    - 安装（Install）：初始化载具、设置座位、应用免疫
 *    - 卸载（Uninstall）：移除乘客、清理资源
 *    - 重置（Reset）：重新初始化载具状态
 *
 * 2. 乘客管理
 *    - 添加乘客（AddPassenger）：支持自动座位分配和指定座位
 *    - 移除乘客（RemovePassenger）：清理乘客状态、恢复控制权
 *    - 批量操作：移除所有乘客、重新定位乘客位置
 *
 * 3. 配件系统
 *    - 安装配件（InstallAccessory）：载具自带的 NPC 配件（如炮塔）
 *    - 配件生命周期管理：随从类型配件随载具消失而消失
 *
 * 4. 免疫系统
 *    - 应用载具免疫（ApplyAllImmunities）：击退、治疗、控制效果等
 *    - 特定载具的特殊免疫规则
 *
 * 5. 座位系统
 *    - 座位配置：从 DBC 数据加载座位信息
 *    - 座位权限：可进入/退出标志、控制标志等
 *    - 座位扩展：从数据库加载额外的座位配置
 *
 * 核心设计思想：
 * - 异步加入机制：通过 VehicleJoinEvent 实现延迟乘客加入，避免状态冲突
 * - 事件驱动架构：载具状态变化触发脚本事件
 * - 安全性保证：多重断言检查确保数据一致性
 *
 * 典型使用场景：
 * @code
 * // 创建并安装载具
 * Vehicle* vehicle = new Vehicle(unit, vehicleEntry, creatureEntry);
 * vehicle->Install();
 *
 * // 添加乘客
 * vehicle->AddPassenger(player, -1);  // -1 表示自动分配座位
 *
 * // 移除乘客
 * vehicle->RemovePassenger(player);
 *
 * // 卸载载具
 * vehicle->Uninstall();
 * @endcode
 *
 * @see Vehicle.h 头文件定义
 * @see VehicleEntry 载具 DBC 数据结构
 * @see VehicleSeatEntry 座位 DBC 数据结构
 */

#include "Vehicle.h"
#include "Battleground.h"
#include "Common.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "EventProcessor.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "TemporarySummon.h"
#include "Unit.h"
#include "Util.h"

/**
 * @brief 构造函数 - 初始化载具对象
 *
 * @details 职责：
 *   初始化载具实例，设置座位信息，计算可用座位数量，设置NPC标志。
 *   此构造函数会立即初始化所有座位并设置载具的基础属性。
 *
 * @param unit 载具所属的单位（玩家或生物），是载具的"载体"
 * @param vehInfo 载具配置信息（从Vehicle.dbc加载），包含座位ID列表等
 * @param creatureEntry 生物模板ID，用于查询配件配置
 *
 * @note 性能说明：
 *   - 时间复杂度：O(MAX_VEHICLE_SEATS)，通常是 O(8)，非常高效
 *   - 会查找 DBC 存储和数据库，但查找操作有缓存
 *   - 不会触发脚本事件（脚本事件在 Install() 中触发）
 *
 * @warning 必须在构造后调用 Install() 才能正常使用载具
 *
 * 主要流程：
 *   1. 遍历所有可能的座位位置（最多 MAX_VEHICLE_SEATS 个）
 *   2. 为每个有效座位 ID 查找座位配置信息（VehicleSeat.dbc）
 *   3. 加载座位的扩展配置（从数据库 vehicle_seat_addon 表）
 *   4. 统计可进入/退出的座位数量（UsableSeatNum）
 *   5. 根据是否有可用座位设置相应的 NPC 标志
 *   6. 初始化载具的移动信息标志
 *
 * @code
 * // 典型使用示例
 * VehicleEntry const* vehicleEntry = sVehicleStore.LookupEntry(vehicleId);
 * Vehicle* vehicle = new Vehicle(creature, vehicleEntry, creature->GetEntry());
 * vehicle->Install();
 * @endcode
 */
Vehicle::Vehicle(Unit* unit, VehicleEntry const* vehInfo, uint32 creatureEntry) :
UsableSeatNum(0), _me(unit), _vehicleInfo(vehInfo), _creatureEntry(creatureEntry), _status(STATUS_NONE)
{
    // 遍历所有座位位置，初始化座位映射
    // MAX_VEHICLE_SEATS 通常为 8，表示载具最多有 8 个座位
    for (uint32 i = 0; i < MAX_VEHICLE_SEATS; ++i)
    {
        // 检查该位置是否有有效的座位 ID
        if (uint32 seatId = _vehicleInfo->SeatID[i])
            if (VehicleSeatEntry const* veSeat = sVehicleSeatStore.LookupEntry(seatId))
            {
                // 获取座位的扩展配置（从数据库 vehicle_seat_addon 表加载）
                // 扩展配置包含 DBC 中没有的额外属性，如座椅朝向偏移等
                VehicleSeatAddon const* addon = sObjectMgr->GetVehicleSeatAddon(seatId);
                Seats.insert(std::make_pair(i, VehicleSeat(veSeat, addon)));

                // 统计可进入/退出的座位数量
                // 这些座位可以被玩家通过交互或法术点击进入
                if (veSeat->CanEnterOrExit())
                    ++UsableSeatNum;
            }
    }

    // 根据可用座位数量设置或移除正确的 NPC 标志
    // 这会覆盖数据库中的错误数据，确保客户端正确显示交互提示
    if (UsableSeatNum)
        _me->SetNpcFlag((_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));
    else
        _me->RemoveNpcFlag((_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));

    // 初始化载具的移动信息标志
    // 根据载具类型设置移动限制（如禁止跳跃、禁止横移等）
    InitMovementInfoForBase();
}

/**
 * @brief 析构函数 - 验证载具已正确卸载
 *
 * @details 职责：
 *   确保载具在析构前已调用 Uninstall() 方法，所有座位必须为空。
 *   这是一种防御性编程措施，防止资源泄漏。
 *
 * @warning Uninstall() 必须在此析构函数之前调用！
 *   如果未调用 Uninstall()，断言将失败并导致服务器崩溃。
 *
 * @par 设计原因：
 *   载具的卸载过程涉及复杂的清理工作（移除乘客、触发脚本事件等），
 *   这些工作不适合放在析构函数中，因为析构函数不应该抛出异常或
 *   执行可能失败的操作。因此采用"两阶段析构"模式：
 *   1. Uninstall() - 执行清理工作
 *   2. ~Vehicle() - 验证清理完成
 *
 * @par 断言说明：
 *   - _status == STATUS_UNINSTALLING：确保调用了 Uninstall()
 *   - 所有座位为空：确保所有乘客已正确移除
 */
Vehicle::~Vehicle()
{
    // 断言检查：载具必须处于卸载状态
    // 如果此处断言失败，说明忘记调用 Uninstall()
    ASSERT(_status == STATUS_UNINSTALLING);

    // 断言检查：所有座位必须为空
    // 如果有座位不为空，说明有乘客未被正确移除
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        ASSERT(itr->second.IsEmpty());
}

/**
 * @brief 安装载具 - 设置载具状态为已安装
 *
 * @details 职责：
 *   将载具状态设置为 STATUS_INSTALLED，并触发脚本事件。
 *   这是载具初始化流程的最后一步，在构造函数之后调用。
 *
 * @par 调用时机：
 *   在 Vehicle 构造之后、实际使用之前调用。
 *   通常由 Unit::CreateVehicleKit() 调用。
 *
 * @par 触发事件：
 *   - 如果载体是生物单位，触发 sScriptMgr->OnInstall(this)
 *   - 脚本可以在此时执行自定义初始化逻辑
 *
 * @note 此方法本身很简单，主要工作是标记状态和触发脚本。
 *   座位初始化和免疫应用在构造函数和 Reset() 中完成。
 *
 * @see Uninstall() 卸载载具
 * @see Reset() 重置载具状态
 */
void Vehicle::Install()
{
    _status = STATUS_INSTALLED;

    // 如果载具是生物单位，触发脚本管理器的 OnInstall 事件
    // 脚本可以在此时执行载具特定的初始化逻辑
    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnInstall(this);
}

/**
 * @brief 安装所有配件 - 加载载具的所有预定义配件
 *
 * @details 职责：
 *   根据数据库配置安装载具的所有配件（如炮塔、驾驶员等）。
 *   配件是载具自带的 NPC 乘客，通常不可被玩家控制。
 *
 * @param evading 是否在逃避模式下调用（从 CreatureAI::EnterEvadeMode）
 *   - true：仅安装随从类型的配件（用于生物逃避回家时重建配件）
 *   - false：安装所有配件（完整安装）
 *
 * @par 配件数据来源：
 *   配件列表从数据库表 `vehicle_template_accessory` 或 `vehicle_accessory` 加载。
 *   - vehicle_template_accessory：基于生物模板的配件（所有同类生物共享）
 *   - vehicle_accessory：基于具体生物实例的配件（仅特定生物实例）
 *
 * @par 主要流程：
 *   1. 如果不是逃避模式或载具是玩家，先移除所有乘客
 *      - 原因：数据库中可能保存了无效施法者的光环，需要清理
 *   2. 从对象管理器获取配件列表
 *   3. 遍历配件列表，逐个安装配件
 *   4. 在逃避模式下只安装随从（minion）配件
 *      - 原因：非随从配件可能是临时召唤的，逃避时不应该重建
 *
 * @note 性能考虑：
 *   - 配件安装是异步的（通过 InstallAccessory -> AddPassenger -> VehicleJoinEvent）
 *   - 此方法可能触发多个生物召唤，对性能有一定影响
 *
 * @see InstallAccessory() 安装单个配件
 * @see RemoveAllPassengers() 移除所有乘客
 */
void Vehicle::InstallAllAccessories(bool evading)
{
    // 玩家载具或非逃避模式下，先移除所有乘客
    // 原因：数据库中可能保存了无效施法者的光环，需要清理
    // 注释中的英文：We might have aura's saved in the DB with now invalid casters - remove
    if (GetBase()->GetTypeId() == TYPEID_PLAYER || !evading)
        RemoveAllPassengers();

    // 从对象管理器获取载具配件列表
    // 优先查找 vehicle_accessory（实例配置），其次查找 vehicle_template_accessory（模板配置）
    VehicleAccessoryList const* accessories = sObjectMgr->GetVehicleAccessoryList(this);
    if (!accessories)
        return;

    // 遍历配件列表并安装每个配件
    for (VehicleAccessoryList::const_iterator itr = accessories->begin(); itr != accessories->end(); ++itr)
    {
        // 逃避模式下只安装随从配件
        // 随从配件的生命周期与载具绑定，逃避回家时应该重建
        if (!evading || itr->IsMinion)
            InstallAccessory(itr->AccessoryEntry, itr->SeatId, itr->IsMinion, itr->SummonedType, itr->SummonTime);
    }
}

/**
 * @brief 卸载载具 - 移除所有乘客并设置卸载状态
 *
 * @details 职责：
 *   将载具设置为卸载状态，移除所有乘客，此后无法再添加新乘客。
 *   这是载具生命周期的最后一步，在析构之前调用。
 *
 * @par 调用时机：
 *   - 载体单位被删除时（Unit 析构函数）
 *   - 载具法术效果消失时
 *   - 载具被摧毁时
 *   - 手动移除载具时
 *
 * @par 防御机制：
 *   检测递归卸载调用，避免因脚本钩子错误导致的无限递归。
 *   如果检测到递归调用，记录错误日志并提前返回。
 *
 * @warning 一旦调用此方法，载具将不可再添加乘客。
 *   AddPassenger() 会检查状态并拒绝添加。
 *
 * @par 主要流程：
 *   1. 防止递归卸载调用（避免脚本钩子导致的错误）
 *   2. 设置状态为 STATUS_UNINSTALLING
 *   3. 移除所有乘客
 *      - 取消所有待处理的加入事件
 *      - 移除所有 SPELL_AURA_CONTROL_VEHICLE 光环
 *      - 强制剩余乘客退出
 *   4. 如果载具是生物，触发 OnUninstall 脚本事件
 *
 * @see Install() 安装载具
 * @see RemoveAllPassengers() 移除所有乘客
 */
void Vehicle::Uninstall()
{
    // 防止递归卸载调用
    // 检查脚本钩子 OnUninstall/OnRemovePassenger/PassengerBoarded 中的错误
    // 如果已经在卸载状态且不是随从类型，说明存在递归调用
    if (_status == STATUS_UNINSTALLING && !GetBase()->HasUnitTypeMask(UNIT_MASK_MINION))
    {
        TC_LOG_ERROR("entities.vehicle", "Vehicle {} attempts to uninstall, but already has STATUS_UNINSTALLING! "
            "Check Uninstall/PassengerBoarded script hooks for errors.", _me->GetGUID().ToString());
        return;
    }

    // 设置状态为卸载中
    _status = STATUS_UNINSTALLING;
    TC_LOG_DEBUG("entities.vehicle", "Vehicle::Uninstall Entry: {}, {}", _creatureEntry, _me->GetGUID().ToString());

    // 移除所有乘客
    // 这会触发所有乘客的退出流程，包括脚本事件
    RemoveAllPassengers();

    // 如果载具是生物单位，触发脚本管理器的 OnUninstall 事件
    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnUninstall(this);
}

/**
 * @brief 重置载具 - 重新应用免疫并重新安装配件
 *
 * @details 职责：
 *   重置载具状态，重新应用免疫效果，重新安装所有配件（仅对生物有效）。
 *   通常在生物逃避回家或重置时调用。
 *
 * @param evading 是否在逃避模式下调用
 *   - true：从 CreatureAI::EnterEvadeMode 调用，只重建随从配件
 *   - false：完整重置，重建所有配件
 *
 * @par 调用时机：
 *   - 生物逃避回家时（CreatureAI::EnterEvadeMode）
 *   - 生物初始化完成时
 *   - 手动重置载具状态时
 *
 * @par 玩家载具处理：
 *   玩家载具不执行重置操作，此方法会直接返回。
 *   原因：玩家载具的状态由法术和玩家控制，不应自动重置。
 *
 * @par 主要流程：
 *   1. 检查是否为生物单位（玩家载具不处理）
 *   2. 应用所有免疫效果
 *      - 击退免疫
 *      - 机械类型生物的治疗免疫
 *      - 特定载具的特殊免疫
 *   3. 如果载具存活，安装所有配件
 *      - 根据 evading 参数决定安装全部配件或仅安装随从配件
 *   4. 触发 OnReset 脚本事件
 *
 * @note 免疫效果在每次重置时都会重新应用，确保载具状态正确。
 *
 * @see ApplyAllImmunities() 应用免疫效果
 * @see InstallAllAccessories() 安装配件
 */
void Vehicle::Reset(bool evading /*= false*/)
{
    // 只处理生物类型的载具
    // 玩家载具不执行重置操作
    if (GetBase()->GetTypeId() != TYPEID_UNIT)
        return;

    TC_LOG_DEBUG("entities.vehicle", "Vehicle::Reset (Entry: {}, {}, DBGuid: {})", GetCreatureEntry(), _me->GetGUID().ToString(), _me->ToCreature()->GetSpawnId());

    // 应用所有免疫效果
    // 这会设置载具对各种效果的免疫状态
    ApplyAllImmunities();

    // 如果载具存活，重新安装所有配件
    // 根据 evading 参数决定安装全部配件或仅安装随从配件
    if (GetBase()->IsAlive())
        InstallAllAccessories(evading);

    // 触发脚本管理器的 OnReset 事件
    // 脚本可以在此时执行自定义重置逻辑
    sScriptMgr->OnReset(this);
}

/**
 * @brief 应用所有免疫效果 - 设置无法在数据库中配置的免疫
 *
 * @details 职责：
 *   为载具应用特定的免疫效果，这些免疫无法通过数据库配置。
 *   这些免疫是基于游戏设计需求硬编码的，确保载具的行为符合预期。
 *
 * @par 为什么需要硬编码免疫：
 *   - 某些法术的 MECHANIC 为 NONE，无法在数据库中通过机制免疫配置
 *   - 某些免疫是游戏设计的一部分，不应被数据库配置覆盖
 *   - 需要根据载具类型和标志动态决定免疫效果
 *
 * @par 免疫分类：
 *
 * 1. 所有载具的通用免疫：
 *    - 击退效果（SPELL_EFFECT_KNOCK_BACK, SPELL_EFFECT_KNOCK_BACK_DEST）
 *    - 原因：载具通常体积巨大，被击退会造成视觉和逻辑问题
 *
 * 2. 机械类型生物的免疫（非 Boss）：
 *    - 治疗效果：HEAL, HEAL_PCT, PERIODIC_HEAL, DISPEL
 *    - 护盾效果：SCHOOL_IMMUNITY, MOD_UNATTACKABLE, SCHOOL_ABSORB
 *    - 控制效果：BANISH, SHIELD, IMMUNE_SHIELD
 *    - 属性修改：DAMAGE_SHIELD, SPLIT_DAMAGE_PCT, MOD_RESISTANCE, MOD_STAT
 *    - 原因：机械单位不应该被治疗或增强，保持游戏平衡
 *
 * 3. 固定位置载具：
 *    - 设置定身状态（UNIT_STATE_ROOT）
 *    - 原因：如大炮等固定载具不应该移动
 *
 * 4. 特定载具 ID 的特殊免疫：
 *    - 战场载具（远祖海滩、冬拥湖、征服之岛）：定身 + 减速免疫
 *    - 抢夺的载具（摩托车、攻城坦克）：移除伤害增加免疫（攻城槌效果）
 *
 * @warning Boss 级别的载具有自己的数据库免疫，不会被此方法覆盖。
 *
 * @par 性能说明：
 *   - 每次调用会应用多个免疫标志，但免疫检查是哈希查找，性能开销小
 *   - 此方法仅在载具初始化时调用，不会影响运行时性能
 *
 * @see Reset() 重置时会调用此方法
 */
void Vehicle::ApplyAllImmunities()
{
    // 这些免疫无法在数据库中设置，因为某些法术的 MECHANIC 为 NONE

    // 载具应该免疫击退效果
    // 原因：载具通常体积巨大，被击退会造成视觉和逻辑问题
    _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
    _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, true);

    // 机械类型生物和载具（非 Boss，Boss 有自己的数据库免疫）应该免疫治疗
    // 注意：下面的 switch 中有例外情况
    if (_me->ToCreature() && _me->ToCreature()->GetCreatureTemplate()->type == CREATURE_TYPE_MECHANICAL && !_me->ToCreature()->isWorldBoss())
    {
        // 免疫治疗和驱散效果
        // 机械单位不应该被治疗
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_HEAL, true);
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_HEAL_PCT, true);
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_DISPEL, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_PERIODIC_HEAL, true);

        // 免疫护盾和免疫赋予法术
        // 机械单位不应该被护盾保护或变得无敌
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SCHOOL_IMMUNITY, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_UNATTACKABLE, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SCHOOL_ABSORB, true);
        _me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_BANISH, true);
        _me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SHIELD, true);
        _me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_IMMUNE_SHIELD, true);

        // 免疫抗性、伤害分摊、属性修改
        // 机械单位的属性应该是固定的
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_DAMAGE_SHIELD, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SPLIT_DAMAGE_PCT, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_RESISTANCE, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_STAT, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, true);
    }

    // 如果载具有固定位置标志（如大炮），或者是以下硬编码的单位，设置为定身状态
    //  30236 | 银色黎明大炮
    //  39759 | 反坦克炮
    if ((GetVehicleInfo()->Flags & VEHICLE_FLAG_FIXED_POSITION) || GetBase()->GetEntry() == 30236 || GetBase()->GetEntry() == 39759)
        _me->SetControlled(true, UNIT_STATE_ROOT);

    // 以下针对特定载具的不同免疫设置
    switch (GetVehicleInfo()->ID)
    {
        // 防止可移动大炮的 bug
        // 这些战场载具应该被固定在原地
        case 160: // 远祖海滩
        case 244: // 冬拥湖
        case 510: // 征服之岛
        case 452: // 征服之岛
        case 543: // 征服之岛
            _me->SetControlled(true, UNIT_STATE_ROOT);
            // 为什么需要应用这个？我们可以在数据库中简单地添加减速机制的免疫
            _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_DECREASE_SPEED, true);
            break;
        case 335: // 抢来的摩托车
        case 336: // 抢来的攻城坦克
        case 338: // 抢来的碎投车
            // 移除伤害增加免疫，允许攻城槌效果生效
            _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, false);
            break;
        default:
            break;
    }
}

/**
 * @brief 移除所有乘客 - 清空载具上的所有乘客
 *
 * @details 职责：
 *   移除载具上所有当前乘客和等待加入的乘客。
 *   这是一种强制清理操作，用于载具卸载或重置。
 *
 * @par 调用时机：
 *   - 载具卸载时（Uninstall）
 *   - 载具重置时（InstallAllAccessories，仅非逃避模式）
 *   - 载具被销毁时
 *   - 需要清空载具的特定场景
 *
 * @par 处理范围：
 *   1. 待处理的加入事件（_pendingJoinEvents）
 *      - 取消所有正在等待的 VehicleJoinEvent
 *      - 防止新乘客在清理过程中加入
 *   2. 当前乘客（Seats）
 *      - 移除所有 SPELL_AURA_CONTROL_VEHICLE 光环
 *      - 强制剩余乘客退出
 *
 * @par 异步事件处理：
 *   设置 to_Abort 为 true 将导致 VehicleJoinEvent::Abort 在下次 Unit::UpdateEvents 调用时执行。
 *   这将正确地"重置"乘客的待处理加入过程。
 *
 * @warning 此方法会强制移除所有乘客，可能导致：
 *   - 玩家从载具上掉落
 *   - 配件被移除
 *   - 待处理的加入操作被取消
 *
 * @par 光环处理顺序：
 *   1. 先移除光环（RemoveAurasByType）
 *      - 这会触发光环移除处理器，通常会使乘客自动退出
 *   2. 再强制退出剩余乘客
 *      - 处理光环脚本导致的特殊情况（载具消失但乘客仍存在）
 *
 * @see Uninstall() 卸载载具时调用
 * @see VehicleJoinEvent::Abort() 中止加入事件
 */
void Vehicle::RemoveAllPassengers()
{
    TC_LOG_DEBUG("entities.vehicle", "Vehicle::RemoveAllPassengers. Entry: {}, {}", _creatureEntry, _me->GetGUID().ToString());

    // 设置 to_Abort 为 true 将导致 VehicleJoinEvent::Abort 在下次 Unit::UpdateEvents 调用时执行
    // 这将正确地"重置"乘客的待处理加入过程
    {
        // 在每个待处理加入事件中更新载具指针 - Abort 可能在载具删除后调用
        // 如果载具正在卸载，传入 nullptr；否则传入 this
        Vehicle* eventVehicle = _status != STATUS_UNINSTALLING ? this : nullptr;

        // 遍历所有待处理的加入事件并中止它们
        while (!_pendingJoinEvents.empty())
        {
            VehicleJoinEvent* e = _pendingJoinEvents.front();
            e->ScheduleAbort();        // 标记事件为中止状态
            e->Target = eventVehicle;  // 更新载具指针（可能是 nullptr）
            _pendingJoinEvents.pop_front();
        }
    }

    // 乘客总是在载具上施放 SPELL_AURA_CONTROL_VEHICLE 光环
    // 我们只需移除光环，取消应用处理器将使目标离开载具
    // 我们不需要遍历 Seats
    _me->RemoveAurasByType(SPELL_AURA_CONTROL_VEHICLE);

    // 光环脚本可能导致载具在处理 SPELL_AURA_CONTROL_VEHICLE 移除过程中被消失
    // 在这种情况下，光环效果已经注销，但乘客可能仍然在 Seats 中找到
    // 需要强制这些乘客退出
    for (auto const& [_, seat] : Seats)
        if (Unit* passenger = ObjectAccessor::GetUnit(*_me, seat.Passenger.Guid))
            passenger->_ExitVehicle();  // 强制乘客退出载具
}

/**
 * @brief 检查座位是否为空 - 判断指定座位是否有乘客
 *
 * 职责：
 *   检查载具上指定的座位是否为空
 *
 * 参数：
 *   seatId - 座位ID
 *
 * 返回值：
 *   true - 座位为空
 *   false - 座位已有人或座位不存在
 */
bool Vehicle::HasEmptySeat(int8 seatId) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return false;
    return seat->second.IsEmpty();
}

/**
 * @brief 获取指定座位的乘客 - 获取某个座位上的单位
 *
 * 职责：
 *   返回指定座位上的乘客单位
 *
 * 参数：
 *   seatId - 要查询的座位ID
 *
 * 返回值：
 *   如果找到且在世界中，返回乘客指针；否则返回nullptr
 */
Unit* Vehicle::GetPassenger(int8 seatId) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return nullptr;

    return ObjectAccessor::GetUnit(*GetBase(), seat->second.Passenger.Guid);
}

/**
 * @brief 获取下一个空座位 - 从当前座位开始查找下一个空座位
 *
 * 职责：
 *   根据当前座位查找下一个可用的空座位
 *
 * 参数：
 *   seatId - 当前座位ID
 *   next - true表示向前迭代，false表示向后迭代
 *
 * 返回值：
 *   指向空座位的迭代器，如果没有可用座位则返回Seats.end()
 */
SeatMap::const_iterator Vehicle::GetNextEmptySeat(int8 seatId, bool next) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return seat;

    // 循环查找直到找到空座位
    while (!seat->second.IsEmpty() || HasPendingEventForSeat(seat->first) || (!seat->second.SeatInfo->CanEnterOrExit() && !seat->second.SeatInfo->IsUsableByOverride()))
    {
        if (next)
        {
            if (++seat == Seats.end())
                seat = Seats.begin();
        }
        else
        {
            if (seat == Seats.begin())
                seat = Seats.end();
            --seat;
        }

        // 确保不会无限循环
        if (seat->first == seatId)
            return Seats.end();
    }

    return seat;
}

/**
 * @brief 获取乘客座位的扩展信息 - 获取乘客所坐座位的附加数据
 *
 * 职责：
 *   获取指定乘客所坐座位的扩展配置信息
 *
 * 参数：
 *   passenger - 当前座位使用者的指针
 *
 * 返回值：
 *   座位扩展数据指针，如果乘客不在载具上则返回nullptr
 */
VehicleSeatAddon const* Vehicle::GetSeatAddonForSeatOfPassenger(Unit const* passenger) const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); itr++)
        if (!itr->second.IsEmpty() && itr->second.Passenger.Guid == passenger->GetGUID())
            return itr->second.SeatAddon;

    return nullptr;
}

/**
 * @brief 安装配件 - 在载具上安装NPC配件
 *
 * 职责：
 *   在载具的指定座位上安装一个NPC配件（如炮塔、驾驶员等）
 *
 * 参数：
 *   entry - 配件NPC的模板ID
 *   seatId - 要安装配件的座位ID
 *   minion - 是否为随从，如果是，配件会随载具消失而消失
 *   type - 召唤类型（参见@SummonType枚举）
 *   summonTime - 定时消失召唤的消失时间（毫秒）
 *
 * 主要流程：
 *   1. 检查载具状态，防止在卸载过程中安装配件
 *   2. 召唤配件生物
 *   3. 如果是随从类型，设置UNIT_MASK_ACCESSORY标志
 *   4. 处理法术点击事件，将配件放入座位
 */
void Vehicle::InstallAccessory(uint32 entry, int8 seatId, bool minion, uint8 type, uint32 summonTime)
{
    // 防止在载具卸载过程中添加配件（检查脚本钩子中的错误）
    if (_status == STATUS_UNINSTALLING)
    {
        TC_LOG_ERROR("entities.vehicle", "Vehicle ({}, Entry: {}) attempts to install accessory (Entry: {}) on seat {} with STATUS_UNINSTALLING! "
            "Check Uninstall/PassengerBoarded script hooks for errors.", _me->GetGUID().ToString(),
            GetCreatureEntry(), entry, (int32)seatId);
        return;
    }

    TC_LOG_DEBUG("entities.vehicle", "Vehicle ({}, Entry {}): installing accessory (Entry: {}) on seat: {}",
        _me->GetGUID().ToString(), GetCreatureEntry(), entry, (int32)seatId);

    // 召唤配件生物
    TempSummon* accessory = _me->SummonCreature(entry, *_me, TempSummonType(type), Milliseconds(summonTime));
    ASSERT(accessory);

    // 如果是随从类型，设置配件标志（随载具生命周期）
    if (minion)
        accessory->AddUnitTypeMask(UNIT_MASK_ACCESSORY);

    // 处理法术点击事件，将配件放入指定座位
    _me->HandleSpellClick(accessory, seatId);

    // 如果由于某种原因添加配件失败，将在VehicleJoinEvent::Abort中取消召唤
}

/**
 * @brief 添加乘客 - 尝试将单位添加到载具的指定座位
 *
 * @details 职责：
 *   将乘客添加到载具上，座位选择可以自动分配或指定。
 *   此方法使用异步加入机制，实际的加入操作延迟到下一帧执行。
 *
 * @param unit 要添加的乘客单位（玩家或生物）
 * @param seatId 座位 ID
 *   - -1：自动选择下一个可用座位
 *   - >= 0：指定座位 ID
 *
 * @return true - 成功添加（异步加入事件已安排）
 * @return false - 添加失败（载具卸载中、无可用座位、座位不存在等）
 *
 * @par 异步加入机制：
 *   乘客的加入是异步的，通过 VehicleJoinEvent 实现。原因：
 *   1. 座位选择可能会踢出已有乘客，可能导致载具解散
 *   2. 需要等待载具状态稳定后再执行加入操作
 *   3. 如果载具同时卸载，可以轻松取消加入操作
 *
 * @par 座位选择逻辑：
 *   1. 未指定座位（seatId < 0）：
 *      - 遍历所有座位，查找第一个满足条件的座位：
 *        - 座位为空
 *        - 无待处理事件
 *        - 可以进入/退出或可被覆盖使用
 *   2. 指定座位（seatId >= 0）：
 *      - 检查座位是否存在
 *      - 如果座位已被占用，踢出当前乘客
 *
 * @warning 此方法只是安排加入事件，实际的加入操作在 VehicleJoinEvent::Execute 中执行。
 *   调用此方法后不应立即假设乘客已在载具上。
 *
 * @par 事件队列：
 *   - 加入事件会被添加到 _pendingJoinEvents 队列
 *   - 事件在 Unit::m_Events 中异步执行
 *   - 如果加入失败，事件会被中止（Abort）
 *
 * @see VehicleJoinEvent::Execute() 实际执行加入操作
 * @see RemovePassenger() 移除乘客
 */
bool Vehicle::AddPassenger(Unit* unit, int8 seatId)
{
    // 防止在载具卸载过程中添加乘客（检查脚本钩子中的错误）
    if (_status == STATUS_UNINSTALLING)
    {
        TC_LOG_ERROR("entities.vehicle", "Passenger {}, attempting to board vehicle {} during uninstall! SeatId: {}",
            unit->GetGUID().ToString(), _me->GetGUID().ToString(), (int32)seatId);
        return false;
    }

    TC_LOG_DEBUG("entities.vehicle", "Unit {} scheduling enter vehicle (entry: {}, vehicleId: {}, guid: {} on seat {}",
        unit->GetName(), _me->GetEntry(), _vehicleInfo->ID, _me->GetGUID().ToString(), (int32)seatId);

    // 座位选择代码可能会将其他乘客踢出载具
    // 虽然以下的有效性可能有争议，但有可能当这样的乘客退出时载具会被解散
    // 这就是为什么实际添加乘客到载具是异步安排的原因，这样在载具同时卸载的情况下可以轻松取消

    // 创建异步加入事件
    SeatMap::iterator seat;
    VehicleJoinEvent* e = new VehicleJoinEvent(this, unit);
    // 将事件添加到乘客的事件队列中，立即执行（延迟时间为 0）
    unit->m_Events.AddEvent(e, unit->m_Events.CalculateTime(0s));

    if (seatId < 0) // 没有特定座位要求，自动选择
    {
        // 遍历所有座位，查找第一个可用的座位
        for (seat = Seats.begin(); seat != Seats.end(); ++seat)
        {
            // 座位必须满足以下条件：
            // 1. 座位为空（IsEmpty）
            // 2. 没有待处理的加入事件（HasPendingEventForSeat）
            // 3. 可以进入/退出或可被覆盖使用（CanEnterOrExit 或 IsUsableByOverride）
            if (seat->second.IsEmpty() && !HasPendingEventForSeat(seat->first) && (seat->second.SeatInfo->CanEnterOrExit() || seat->second.SeatInfo->IsUsableByOverride()))
                break;
        }

        if (seat == Seats.end()) // 没有可用座位
        {
            e->ScheduleAbort();  // 中止加入事件
            return false;
        }

        // 设置事件的座位并添加到待处理队列
        e->Seat = seat;
        _pendingJoinEvents.push_back(e);
    }
    else // 指定了特定座位
    {
        // 查找指定座位
        seat = Seats.find(seatId);
        if (seat == Seats.end())
        {
            e->ScheduleAbort();  // 座位不存在，中止事件
            return false;
        }

        // 设置事件的座位并添加到待处理队列
        e->Seat = seat;
        _pendingJoinEvents.push_back(e);

        // 如果座位已被占用，踢出当前乘客
        if (!seat->second.IsEmpty())
        {
            Unit* passenger = ObjectAccessor::GetUnit(*GetBase(), seat->second.Passenger.Guid);
            ASSERT(passenger);
            passenger->ExitVehicle();  // 强制当前乘客退出
        }

        // 确保座位现在是空的
        ASSERT(seat->second.IsEmpty());
    }

    return true;
}

/**
 * @brief 移除乘客 - 将乘客从载具上移除
 *
 * 职责：
 *   从载具上移除指定的乘客，恢复乘客状态并更新载具状态
 *
 * 参数：
 *   unit - 要移除的乘客单位
 *
 * 返回值：
 *   返回载具指针，如果单位不在此载具上则返回nullptr
 *
 * 主要流程：
 *   1. 验证乘客确实在此载具上
 *   2. 更新可用座位计数
 *   3. 恢复乘客的状态标志
 *   4. 清空座位信息
 *   5. 如果是控制座位，解除魅惑关系
 *   6. 处理移动信息和降落伞
 *   7. 触发脚本事件
 */
Vehicle* Vehicle::RemovePassenger(Unit* unit)
{
    // 检查单位是否在此载具上
    if (unit->GetVehicle() != this)
        return nullptr;

    // 获取乘客的座位迭代器
    SeatMap::iterator seat = GetSeatIteratorForPassenger(unit);
    ASSERT(seat != Seats.end());

    TC_LOG_DEBUG("entities.vehicle", "Unit {} exit vehicle entry {} id {} guid {} seat {}",
        unit->GetName(), _me->GetEntry(), _vehicleInfo->ID, _me->GetGUID().ToString(), (int32)seat->first);

    // 如果座位可以进入/退出，增加可用座位计数
    if (seat->second.SeatInfo->CanEnterOrExit() && ++UsableSeatNum)
        _me->SetNpcFlag((_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));

    // 如果乘客在进入载具前没有UNINTERACTIBLE标志，则移除该标志
    if (seat->second.SeatInfo->Flags & VEHICLE_SEAT_FLAG_PASSENGER_NOT_SELECTABLE && !seat->second.Passenger.IsUninteractible)
        unit->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

    // 重置座位乘客信息
    seat->second.Passenger.Reset();

    // 如果载具是生物且乘客是玩家，并且座位具有控制标志，解除魅惑关系
    if (_me->GetTypeId() == TYPEID_UNIT && unit->GetTypeId() == TYPEID_PLAYER && seat->second.SeatInfo->Flags & VEHICLE_SEAT_FLAG_CAN_CONTROL)
        _me->RemoveCharmedBy(unit);

    // 更新移动信息
    if (_me->IsInWorld())
    {
        if (!_me->GetTransport())
        {
            unit->RemoveUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
            unit->m_movementInfo.transport.Reset();
        }
        else
            unit->m_movementInfo.transport = _me->m_movementInfo.transport;
    }

    // 仅对可飞行载具 - 为飞行中的乘客施放降落伞
    if (unit->IsFlying())
        _me->CastSpell(unit, VEHICLE_SPELL_PARACHUTE, true);

    // 触发AI的PassengerBoarded事件
    if (_me->GetTypeId() == TYPEID_UNIT && _me->ToCreature()->IsAIEnabled())
        _me->ToCreature()->AI()->PassengerBoarded(unit, seat->first, false);

    // 触发脚本管理器的OnRemovePassenger事件
    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnRemovePassenger(this, unit);

    // 清除乘客的载具引用
    unit->SetVehicle(nullptr);
    return this;
}

/**
 * @brief 重新定位乘客 - 更新所有乘客的位置
 *
 * 职责：
 *   根据载具的新位置更新所有乘客的世界坐标
 *   必须在m_base::Relocate之后调用
 *
 * 主要流程：
 *   1. 遍历所有座位
 *   2. 对每个乘客，计算其在载具上的相对位置
 *   3. 将相对位置转换为绝对世界坐标
 *   4. 更新乘客的位置
 */
void Vehicle::RelocatePassengers()
{
    ASSERT(_me->GetMap());

    std::vector<std::pair<Unit*, Position>> seatRelocation;
    seatRelocation.reserve(Seats.size());

    // 不确定绝对位置计算是否正确，它应该取决于载具的俯仰角
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
    {
        if (Unit* passenger = ObjectAccessor::GetUnit(*GetBase(), itr->second.Passenger.Guid))
        {
            ASSERT(passenger->IsInWorld());

            float px, py, pz, po;
            // 获取乘客在载具上的相对位置
            passenger->m_movementInfo.transport.pos.GetPosition(px, py, pz, po);
            // 计算乘客的绝对世界坐标
            CalculatePassengerPosition(px, py, pz, &po);
            seatRelocation.emplace_back(passenger, Position(px, py, pz, po));
        }
    }

    // 批量更新所有乘客的位置
    for (auto const& pair : seatRelocation)
        pair.first->UpdatePosition(pair.second);
}

/**
 * @brief 检查载具是否在使用中 - 判断载具是否有乘客
 *
 * 职责：
 *   检查载具上是否有任何乘客
 *
 * 返回值：
 *   true - 载具上有乘客
 *   false - 载具为空
 */
bool Vehicle::IsVehicleInUse() const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (!itr->second.IsEmpty())
            return true;

    return false;
}

/**
 * @brief 检查载具是否可控 - 判断载具是否有可控制座位
 *
 * 职责：
 *   检查载具是否有可以控制的座位
 *
 * 返回值：
 *   true - 载具具有可控制座位
 *   false - 载具没有可控制座位
 */
bool Vehicle::IsControllableVehicle() const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.SeatInfo->HasFlag(VEHICLE_SEAT_FLAG_CAN_CONTROL))
            return true;

    return false;
}

/**
 * @brief 初始化载具移动信息 - 根据DBC载具标志设置移动标志
 *
 * 职责：
 *   根据载具配置信息中的VehicleFlags设置正确的MovementFlags2
 *
 * 主要流程：
 *   根据载具标志设置相应的移动限制和能力
 */
void Vehicle::InitMovementInfoForBase()
{
    uint32 vehicleFlags = GetVehicleInfo()->Flags;

    // 根据载具标志设置额外的移动标志
    if (vehicleFlags & VEHICLE_FLAG_NO_STRAFE)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_STRAFE);
    if (vehicleFlags & VEHICLE_FLAG_NO_JUMPING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_JUMPING);
    if (vehicleFlags & VEHICLE_FLAG_FULLSPEEDTURNING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_FULL_SPEED_TURNING);
    if (vehicleFlags & VEHICLE_FLAG_ALLOW_PITCHING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING);
    if (vehicleFlags & VEHICLE_FLAG_FULLSPEEDPITCHING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_FULL_SPEED_PITCHING);
}

/**
 * @brief 获取乘客的座位信息 - 获取指定乘客的DBC座位数据
 *
 * 职责：
 *   返回指定乘客所坐座位的信息（VehicleSeat.dbc格式）
 *
 * 参数：
 *   passenger - 要查询的乘客
 *
 * 返回值：
 *   如果乘客在载具上，返回座位DBC记录；否则返回nullptr
 */
VehicleSeatEntry const* Vehicle::GetSeatForPassenger(Unit const* passenger) const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.Passenger.Guid == passenger->GetGUID())
            return itr->second.SeatInfo;

    return nullptr;
}

/**
 * @brief 获取乘客座位迭代器 - 获取指定乘客座位的迭代器
 *
 * 职责：
 *   查找指定乘客在载具上的座位迭代器
 *
 * 参数：
 *   passenger - 要查询的乘客
 *
 * 返回值：
 *   如果找到乘客，返回座位迭代器；否则返回Seats.end()（无效迭代器）
 */
SeatMap::iterator Vehicle::GetSeatIteratorForPassenger(Unit* passenger)
{
    SeatMap::iterator itr;
    for (itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.Passenger.Guid == passenger->GetGUID())
            return itr;

    return Seats.end();
}

/**
 * @brief 获取可用座位数量 - 统计载具的可用座位数
 *
 * 职责：
 *   统计载具上可用座位的数量（空座位且没有待处理事件）
 *
 * 返回值：
 *   可用座位数量
 */
uint8 Vehicle::GetAvailableSeatCount() const
{
    uint8 ret = 0;
    SeatMap::const_iterator itr;
    for (itr = Seats.begin(); itr != Seats.end(); ++itr)
        // 座位必须为空、无待处理事件、且可以进入/退出或可被覆盖使用
        if (itr->second.IsEmpty() && !HasPendingEventForSeat(itr->first) && (itr->second.SeatInfo->CanEnterOrExit() || itr->second.SeatInfo->IsUsableByOverride()))
            ++ret;

    return ret;
}

/**
 * @brief 移除待处理事件 - 从待处理队列中移除加入事件
 *
 * 职责：
 *   从待处理加入事件存储中移除VehicleJoinEvent对象
 *   此方法仅在事件执行或中止后调用，以防止留下指向已删除事件的指针
 *
 * 参数：
 *   e - 要移除的VehicleJoinEvent指针
 */
void Vehicle::RemovePendingEvent(VehicleJoinEvent* e)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end(); ++itr)
    {
        if (*itr == e)
        {
            _pendingJoinEvents.erase(itr);
            break;
        }
    }
}

/**
 * @brief 移除座位的待处理事件 - 移除指定座位的所有待处理事件
 *
 * 职责：
 *   移除指定座位的所有待处理加入事件
 *   在VehicleJoinEvent::Execute调用时执行
 *
 * 参数：
 *   seatId - 座位ID
 */
void Vehicle::RemovePendingEventsForSeat(int8 seatId)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end();)
    {
        if ((*itr)->Seat->first == seatId)
        {
            (*itr)->ScheduleAbort();
            _pendingJoinEvents.erase(itr++);
        }
        else
            ++itr;
    }
}

/**
 * @brief 移除乘客的待处理事件 - 移除指定乘客的待处理事件
 *
 * 职责：
 *   移除指定乘客的所有待处理加入事件
 *   在载具控制光环移除时执行，但乘客正在加入过程中
 *
 * 参数：
 *   passenger - 要移除事件的乘客
 */
void Vehicle::RemovePendingEventsForPassenger(Unit* passenger)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end();)
    {
        if ((*itr)->Passenger == passenger)
        {
            (*itr)->ScheduleAbort();
            _pendingJoinEvents.erase(itr++);
        }
        else
            ++itr;
    }
}

/**
 * @brief VehicleJoinEvent::Execute - 实际执行乘客登上载具的操作
 *
 * 职责：
 *   将乘客@Passenger实际添加到载具@Target上
 *
 * 参数：
 *   parameter1 - 未使用
 *   parameter2 - 未使用
 *
 * 返回值：
 *   true - 总是返回true，不会失败
 *
 * 主要流程：
 *   1. 验证乘客和载具都在世界中
 *   2. 查找载具控制光环
 *   3. 清理待处理事件
 *   4. 检查乘客存活状态
 *   5. 设置乘客载具关联
 *   6. 更新座位计数和NPC标志
 *   7. 打断乘客的法术和光环
 *   8. 玩家特殊处理（丢弃旗帜、解散宠物等）
 *   9. 设置乘客移动信息和位置
 *   10. 如果是控制座位，建立魅惑关系
 *   11. 发送移动数据包
 *   12. 转移威胁值
 *   13. 触发脚本事件
 */
bool VehicleJoinEvent::Execute(uint64, uint32)
{
    // 断言检查：乘客和载具必须在世界中
    ASSERT(Passenger->IsInWorld());
    ASSERT(Target && Target->GetBase()->IsInWorld());

    // 查找载具控制光环
    Unit::AuraEffectList const& vehicleAuras = Target->GetBase()->GetAuraEffectsByType(SPELL_AURA_CONTROL_VEHICLE);
    auto itr = std::find_if(vehicleAuras.begin(), vehicleAuras.end(), [this](AuraEffect const* aurEff) -> bool
    {
        return aurEff->GetCasterGUID() == Passenger->GetGUID();
    });
    ASSERT(itr != vehicleAuras.end());

    // 获取光环应用对象
    AuraApplication const* aurApp = (*itr)->GetBase()->GetApplicationOfTarget(Target->GetBase()->GetGUID());
    ASSERT(aurApp && !aurApp->GetRemoveMode());

    // 移除座位的待处理事件
    Target->RemovePendingEventsForSeat(Seat->first);
    Target->RemovePendingEventsForPassenger(Passenger);

    // 乘客可能在等待期间死亡 - 如果是这种情况则中止
    if (!Passenger->IsAlive())
    {
        Abort(0);
        return true;
    }

    // 可能在同一次更新中执行多个载具加入事件
    if (Passenger->GetVehicle())
        Passenger->ExitVehicle();

    // 设置乘客的载具引用
    Passenger->SetVehicle(Target);
    Seat->second.Passenger.Guid = Passenger->GetGUID();
    Seat->second.Passenger.IsUninteractible = Passenger->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

    // 更新可用座位计数
    if (Seat->second.SeatInfo->CanEnterOrExit())
    {
        ASSERT(Target->UsableSeatNum);
        --(Target->UsableSeatNum);
        if (!Target->UsableSeatNum)
        {
            // 移除NPC标志
            if (Target->GetBase()->GetTypeId() == TYPEID_PLAYER)
                Target->GetBase()->RemoveNpcFlag(UNIT_NPC_FLAG_PLAYER_VEHICLE);
            else
                Target->GetBase()->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
        }
    }

    // 打断乘客的非近战法术
    Passenger->InterruptNonMeleeSpells(false);
    // 移除骑乘光环
    Passenger->RemoveAurasByType(SPELL_AURA_MOUNTED);

    // 获取座位信息
    VehicleSeatEntry const* veSeat = Seat->second.SeatInfo;
    VehicleSeatAddon const* veSeatAddon = Seat->second.SeatAddon;

    // 玩家特殊处理
    Player* player = Passenger->ToPlayer();
    if (player)
    {
        // 丢弃旗帜
        if (Battleground* bg = player->GetBattleground())
            bg->EventPlayerDroppedFlag(player);

        // 停止魅惑施法
        player->StopCastingCharm();
        // 停止视野绑定施法
        player->StopCastingBindSight();
        // 发送取消预期载具乘坐光环
        player->SendOnCancelExpectedVehicleRideAura();
        // 如果座位不允许保留宠物，则临时解散宠物
        if (!veSeat->HasFlag(VEHICLE_SEAT_FLAG_B_KEEP_PET))
            player->UnsummonPetTemporaryIfAny();
    }

    // 如果座位标志为不可选择，设置UNINTERACTIBLE标志
    if (veSeat->HasFlag(VEHICLE_SEAT_FLAG_PASSENGER_NOT_SELECTABLE))
        Passenger->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

    // 计算座位位置和朝向
    float o = veSeatAddon ? veSeatAddon->SeatOrientationOffset : 0.f;
    float x = veSeat->AttachmentOffset.X;
    float y = veSeat->AttachmentOffset.Y;
    float z = veSeat->AttachmentOffset.Z;

    // 设置乘客的移动信息
    Passenger->AddUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
    Passenger->m_movementInfo.transport.pos.Relocate(x, y, z, o);
    Passenger->m_movementInfo.transport.time = 0;
    Passenger->m_movementInfo.transport.seat = Seat->first;
    Passenger->m_movementInfo.transport.guid = Target->GetBase()->GetGUID();

    // 如果载具是生物且乘客是玩家，并且座位具有控制标志
    if (Target->GetBase()->GetTypeId() == TYPEID_UNIT && Passenger->GetTypeId() == TYPEID_PLAYER &&
        veSeat->HasFlag(VEHICLE_SEAT_FLAG_CAN_CONTROL))
    {
        // 处理SMSG_CLIENT_CONTROL
        if (!Target->GetBase()->SetCharmedBy(Passenger, CHARM_TYPE_VEHICLE, aurApp))
        {
            // 魅惑失败，可能光环被重定位/脚本/其他原因移除
            Abort(0);
            return true;
        }
    }

    // 发送清除目标数据包 - SMSG_BREAK_TARGET
    Passenger->SendClearTarget();
    // 设置定身状态 - SMSG_FORCE_ROOT，某些情况下发送SMSG_SPLINE_MOVE_ROOT（对生物）
    Passenger->SetControlled(true, UNIT_STATE_ROOT);
    // 也添加MOVEMENTFLAG_ROOT

    // 创建移动样条初始化器
    std::function<void(Movement::MoveSplineInit&)> initializer = [=](Movement::MoveSplineInit& init)
    {
        init.DisableTransportPathTransformations();
        init.MoveTo(x, y, z, false, true);
        init.SetFacing(o);
        init.SetTransportEnter();
    };
    // 启动移动样条
    Passenger->GetMotionMaster()->LaunchMoveSpline(std::move(initializer), EVENT_VEHICLE_BOARD, MOTION_PRIORITY_HIGHEST);

    // 将乘客的威胁值转移到载具
    for (auto const& [guid, threatRef] : Passenger->GetThreatManager().GetThreatenedByMeList())
        threatRef->GetOwner()->GetThreatManager().AddThreat(Target->GetBase(), threatRef->GetThreat(), nullptr, true, true);

    // 触发载具AI的PassengerBoarded事件和脚本事件
    if (Creature* creature = Target->GetBase()->ToCreature())
    {
        if (CreatureAI* ai = creature->AI())
            ai->PassengerBoarded(Passenger, Seat->first, true);

        sScriptMgr->OnAddPassenger(Target, Passenger, Seat->first);

        // 实际上这是一个冗余的钩子。可以只使用OnAddPassenger并在脚本中检查单位类型掩码
        if (Passenger->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
            sScriptMgr->OnInstallAccessory(Target, Passenger->ToCreature());
    }

    return true;
}

/**
 * @brief VehicleJoinEvent::Abort - 中止乘客登车事件
 *
 * 职责：
 *   中止事件，意味着乘客@Passenger将不会登上载具@Target
 *
 * 参数：
 *   parameter1 - 未使用
 *
 * 主要流程：
 *   1. 检查载具是否已卸载
 *   2. 如果载具存在，移除待处理事件和载具控制光环
 *   3. 如果乘客是配件类型，取消召唤
 */
void VehicleJoinEvent::Abort(uint64)
{
    // 检查载具是否已卸载，如果是则所有光环已被移除
    if (Target)
    {
        TC_LOG_DEBUG("entities.vehicle", "Passenger {}, board on vehicle {} SeatId: {} cancelled",
            Passenger->GetGUID().ToString(), Target->GetBase()->GetGUID().ToString(), (int32)Seat->first);

        // 当Abort被直接调用时移除待处理事件
        Target->RemovePendingEvent(this);

        // SPELL_AURA_CONTROL_VEHICLE光环甚至可以在乘客不在载具上时应用
        // 当此代码被触发时，意味着Vehicle::AddPassenger中出了问题，我们应该手动移除光环
        Target->GetBase()->RemoveAurasByType(SPELL_AURA_CONTROL_VEHICLE, Passenger->GetGUID());
    }
    else
        TC_LOG_DEBUG("entities.vehicle", "Passenger {}, board on uninstalled vehicle SeatId: {} cancelled",
            Passenger->GetGUID().ToString(), (int32)Seat->first);

    // 如果乘客在世界中且是配件类型，取消召唤
    if (Passenger->IsInWorld() && Passenger->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
        Passenger->ToCreature()->DespawnOrUnsummon();
}

/**
 * @brief 检查座位是否有待处理事件 - 判断指定座位是否有等待加入的乘客
 *
 * 职责：
 *   检查指定座位是否有待处理的加入事件
 *
 * 参数：
 *   seatId - 座位ID
 *
 * 返回值：
 *   true - 座位有待处理事件
 *   false - 座位没有待处理事件
 */
bool Vehicle::HasPendingEventForSeat(int8 seatId) const
{
    for (PendingJoinEventContainer::const_iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end(); ++itr)
    {
        if ((*itr)->Seat->first == seatId)
            return true;
    }
    return false;
}

/**
 * @brief 获取消失延迟时间 - 获取载具消失前的延迟时间
 *
 * 职责：
 *   返回载具消失前的延迟时间
 *
 * 返回值：
 *   消失延迟时间（毫秒），默认为1毫秒
 */
Milliseconds Vehicle::GetDespawnDelay()
{
    // 从对象管理器获取载具模板
    if (VehicleTemplate const* vehicleTemplate = sObjectMgr->GetVehicleTemplate(this))
        return vehicleTemplate->DespawnDelay;

    return 1ms;
}

/**
 * @brief 获取调试信息 - 生成载具的调试信息字符串
 *
 * 职责：
 *   生成载具座位和待处理事件的调试信息
 *
 * 返回值：
 *   调试信息字符串
 */
std::string Vehicle::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << "Vehicle seats:\n";
    // 遍历所有座位，输出座位状态
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); itr++)
    {
        sstr << "seat " << std::to_string(itr->first) << ": " << (itr->second.IsEmpty() ? "empty" : itr->second.Passenger.Guid.ToString()) << "\n";
    }

    sstr << "Vehicle pending events:";

    // 输出待处理事件
    if (_pendingJoinEvents.empty())
    {
        sstr << " none";
    }
    else
    {
        sstr << "\n";
        for (PendingJoinEventContainer::const_iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end(); ++itr)
        {
            sstr << "seat " << std::to_string((*itr)->Seat->first) << ": " << (*itr)->Passenger->GetGUID().ToString() << "\n";
        }
    }

    return sstr.str();
}

/**
 * @brief 获取弱指针 - 获取载具对象的弱指针
 *
 * 职责：
 *   返回载具的弱指针，用于安全引用
 *
 * 返回值：
 *   载具的弱指针
 */
Trinity::unique_weak_ptr<Vehicle> Vehicle::GetWeakPtr() const
{
    return _me->GetVehicleKitWeakPtr();
}
