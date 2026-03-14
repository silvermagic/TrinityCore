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
 * @file DynamicObject.cpp
 * @brief 动态对象实体模块实现文件
 *
 * 本文件实现了 DynamicObject 类的所有方法，包括：
 * - 对象的创建和销毁
 * - 世界加入和移除逻辑
 * - 持续时间和光环管理
 * - 视野系统支持
 * - 与施法者的绑定机制
 *
 * 动态对象是游戏中实现持续性区域效果法术的核心机制，
 * 通过本模块可以创建、更新和销毁这些临时性的游戏对象。
 */

#include "DynamicObject.h"
#include "Common.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "ScriptMgr.h"
#include "Transport.h"
#include "Unit.h"
#include "UpdateData.h"

/**
 * @brief 构造函数，初始化动态对象的基本属性
 *
 * @param isWorldObject 是否为世界对象
 *
 * 初始化流程：
 * 1. 调用父类 WorldObject 构造函数
 * 2. 初始化成员变量为默认值
 * 3. 设置对象类型掩码为 TYPEMASK_DYNAMICOBJECT
 * 4. 设置对象类型 ID 为 TYPEID_DYNAMICOBJECT
 * 5. 配置更新标志（低 GUID、静止位置、位置信息）
 * 6. 设置属性值数量
 */
DynamicObject::DynamicObject(bool isWorldObject) : WorldObject(isWorldObject),
    _aura(nullptr), _removedAura(nullptr), _caster(nullptr), _duration(0), _isViewpoint(false)
{
    // 设置对象类型掩码，添加动态对象类型
    m_objectType |= TYPEMASK_DYNAMICOBJECT;
    // 设置对象类型 ID
    m_objectTypeId = TYPEID_DYNAMICOBJECT;

    // 设置更新标志：
    // - UPDATEFLAG_LOWGUID: 使用低端 GUID
    // - UPDATEFLAG_STATIONARY_POSITION: 静止位置（不会移动）
    // - UPDATEFLAG_POSITION: 包含位置信息
    m_updateFlag = (UPDATEFLAG_LOWGUID | UPDATEFLAG_STATIONARY_POSITION | UPDATEFLAG_POSITION);

    // 设置属性值数组大小
    m_valuesCount = DYNAMICOBJECT_END;
}

/**
 * @brief 析构函数，确保对象被正确清理
 *
 * 在对象销毁时执行安全检查：
 * - 确保没有活跃的光环引用
 * - 确保已与施法者解除绑定
 * - 确保不是任何玩家的视野焦点
 *
 * 删除已移除但尚未清理的光环对象。
 */
DynamicObject::~DynamicObject()
{
    // 确保所有引用都已被正确移除
    ASSERT(!_aura);
    ASSERT(!_caster);
    ASSERT(!_isViewpoint);
    // 删除已移除的光环对象（延迟删除机制）
    delete _removedAura;
}

/**
 * @brief 将动态对象添加到游戏世界
 *
 * 调用时机：当动态对象创建完成并准备进入游戏世界时调用。
 *
 * 执行流程：
 * 1. 检查对象是否已在世界中（防止重复添加）
 * 2. 将对象注册到地图的对象存储器，建立 GUID 查找索引
 * 3. 调用父类 WorldObject::AddToWorld 完成基础注册
 * 4. 绑定到施法者，建立双向引用关系
 *
 * 注意：此方法由法术系统在创建动态对象后调用。
 */
void DynamicObject::AddToWorld()
{
    // 只在对象不在世界中时执行添加操作
    if (!IsInWorld())
    {
        // 将动态对象插入地图的对象存储器，用于 GUID 查找
        GetMap()->GetObjectsStore().Insert<DynamicObject>(GetGUID(), this);
        // 调用父类方法，完成世界对象的标准注册流程
        WorldObject::AddToWorld();
        // 建立与施法者的绑定关系
        BindToCaster();
    }
}

/**
 * @brief 将动态对象从游戏世界中移除
 *
 * 调用时机：当动态对象需要从世界中移除时调用（如法术结束、对象销毁、持续时间到期）。
 *
 * 执行流程：
 * 1. 检查对象是否在世界中（防止重复移除）
 * 2. 如果是视野焦点，先移除视野绑定
 * 3. 如果有关联的光环，移除光环
 * 4. 处理光环移除过程中可能触发的对象移除（重入保护）
 * 5. 解除与施法者的绑定
 * 6. 调用父类 RemoveFromWorld 完成基础移除
 * 7. 从地图对象存储器中移除
 *
 * 注意：
 * - 光环移除可能导致对象被提前移除（如触发销毁效果）
 * - 移除顺序很重要，必须先处理视野和光环
 */
void DynamicObject::RemoveFromWorld()
{
    // 只在对象在世界中时执行移除操作
    if (IsInWorld())
    {
        // 如果是视野焦点，先移除视野绑定
        if (_isViewpoint)
            RemoveCasterViewpoint();

        // 如果有光环，移除光环效果
        if (_aura)
            RemoveAura();

        // 注意：光环移除可能触发动态对象的移除
        // 例如：RemoveAura -> Aura::Remove -> 触发效果 -> 再次调用 Remove
        // 需要检查对象是否还在世界中，避免重入问题
        if (!IsInWorld())
            return;

        // 解除与施法者的绑定关系
        UnbindFromCaster();
        // 调用父类方法，完成世界对象的标准移除流程
        WorldObject::RemoveFromWorld();
        // 从地图的对象存储器中移除
        GetMap()->GetObjectsStore().Remove<DynamicObject>(GetGUID());

    }
}

/**
 * @brief 创建动态对象
 *
 * @param guidlow 对象的低端 GUID（用于生成唯一标识符）
 * @param caster 施放法术的单位
 * @param spellId 法术 ID
 * @param pos 创建位置坐标
 * @param radius 影响半径
 * @param type 动态对象类型（区域法术/远视焦点/传送门）
 * @return 创建成功返回 true，创建失败返回 false
 *
 * 调用时机：在法术施放过程中，当需要创建持续性区域效果时调用。
 * 例如：暴风雪、烈焰风暴、奉献等法术施放时。
 *
 * 创建流程：
 * 1. 设置地图和位置信息
 * 2. 验证位置有效性
 * 3. 创建对象的基本属性（GUID、相位掩码）
 * 4. 设置法术相关属性（法术 ID、施法者、半径、类型）
 * 5. 如果是活跃对象，设置为活跃状态
 * 6. 如果施法者在交通工具上，计算相对位置并附加到交通工具
 * 7. 添加到地图
 *
 * 性能注意事项：
 * - 位置验证失败会导致创建失败
 * - 添加到地图失败时需要回滚交通工具附加操作
 * - 交通工具附加必须在添加到地图之前完成
 *
 * 关于 DYNAMICOBJECT_BYTES 的说明：
 * 客户端对 DYNAMICOBJECT_BYTES 低字节的值有特殊处理：
 * - 如果值为 0x01，客户端会覆盖大多数"地面贴片"视觉效果的法术半径
 * - 如果使用其他值，客户端将直接使用 DYNAMICOBJECT_RADIUS 的值
 *   但对于许多法术需要预先补偿（如 radius *= 2）
 * - 暴雪服务器对所有法术都发送 0x01
 */
bool DynamicObject::CreateDynamicObject(ObjectGuid::LowType guidlow, Unit* caster, uint32 spellId, Position const& pos, float radius, DynamicObjectType type)
{
    // 设置动态对象所在的地图（与施法者相同）
    SetMap(caster->GetMap());
    // 定位到指定位置
    Relocate(pos);

    // 验证位置有效性
    if (!IsPositionValid())
    {
        TC_LOG_ERROR("misc", "DynamicObject (spell {}) not created. Suggested coordinates isn't valid (X: {} Y: {})", spellId, GetPositionX(), GetPositionY());
        return false;
    }

    // 创建对象的基本属性
    // 使用施法者的相位掩码，确保在正确的相位中可见
    WorldObject::_Create(guidlow, HighGuid::DynamicObject, caster->GetPhaseMask());

    // 设置法术 ID 作为条目 ID
    SetEntry(spellId);
    // 设置对象缩放为 1（标准大小）
    SetObjectScale(1);
    // 设置施法者 GUID
    SetGuidValue(DYNAMICOBJECT_CASTER, caster->GetGUID());

    // 设置动态对象类型
    // 客户端根据此值决定是否覆盖视觉效果半径
    // 详细说明见函数文档
    SetByteValue(DYNAMICOBJECT_BYTES, 0, type);
    // 设置法术 ID
    SetUInt32Value(DYNAMICOBJECT_SPELLID, spellId);
    // 设置影响半径
    SetFloatValue(DYNAMICOBJECT_RADIUS, radius);
    // 设置施法时间（用于客户端显示）
    SetUInt32Value(DYNAMICOBJECT_CASTTIME, GameTime::GetGameTimeMS());

    // 如果对象存储在世界对象网格容器中，设置为活跃对象
    // 这必须在添加到地图之前设置，以便正确放入世界容器
    if (IsStoredInWorldObjectGridContainer())
        setActive(true);    //必须在添加到地图之前设置，以便放入世界容器

    // 处理施法者在交通工具上的情况
    Transport* transport = caster->GetTransport();
    if (transport)
    {
        // 计算相对于交通工具的偏移位置
        float x, y, z, o;
        pos.GetPosition(x, y, z, o);
        transport->CalculatePassengerOffset(x, y, z, &o);
        m_movementInfo.transport.pos.Relocate(x, y, z, o);

        // 在添加到地图之前附加到交通工具
        // 这对客户端正确显示动态对象至关重要
        transport->AddPassenger(this);
    }

    // 将动态对象添加到地图
    if (!GetMap()->AddToMap(this))
    {
        // 如果添加失败，需要从交通工具中移除
        // 返回 false 会导致对象被删除
        if (transport)
            transport->RemovePassenger(this);
        return false;
    }

    return true;
}

/**
 * @brief 更新动态对象状态
 *
 * @param p_time 自上次更新以来经过的时间（毫秒）
 *
 * 调用时机：每次地图更新时由地图系统自动调用。
 * 这是游戏主循环的一部分，用于处理动态对象的持续性效果。
 *
 * 更新流程：
 * 1. 验证施法者存在且在同一地图（安全检查）
 * 2. 如果有关联的光环，更新光环效果
 * 3. 如果没有光环，更新自身的持续时间
 * 4. 检查是否过期，过期则移除对象
 * 5. 如果未过期，调用脚本系统的更新回调
 *
 * 光环更新注意事项：
 * - 光环可能在 UpdateOwner 过程中被移除（如被驱散）
 * - 光环移除会将 _aura 置为 null
 * - 需要在调用后重新检查 _aura 的状态
 *
 * 性能注意事项：
 * - 此方法每个更新周期都会调用，应保持高效
 * - 脚本回调可能会增加额外开销
 */
void DynamicObject::Update(uint32 p_time)
{
    // 施法者必须存在且在同一地图中
    // 这是核心假设，违反此假设表示严重的逻辑错误
    ASSERT(_caster);
    ASSERT(_caster->GetMap() == GetMap());

    bool expired = false;

    // 如果有关联的光环，更新光环
    if (_aura)
    {
        // 只在光环未被移除时更新
        if (!_aura->IsRemoved())
            _aura->UpdateOwner(p_time, this);

        // 注意：_aura 可能在 UpdateOwner 调用过程中被设置为 null
        // 例如：光环被驱散或触发移除效果
        // 需要重新检查 _aura 的状态
        if (_aura && (_aura->IsRemoved() || _aura->IsExpired()))
            expired = true;
    }
    else
    {
        // 没有光环时，更新自身的持续时间
        if (GetDuration() > int32(p_time))
            _duration -= p_time;  // 减少剩余时间
        else
            expired = true;  // 时间耗尽，标记为过期
    }

    // 如果已过期，移除动态对象
    if (expired)
        Remove();
    else
        // 调用脚本系统的更新回调，允许脚本处理自定义逻辑
        sScriptMgr->OnDynamicObjectUpdate(this, p_time);
}

/**
 * @brief 移除动态对象
 *
 * 调用时机：
 * - 持续时间到期
 * - 法术被驱散
 * - 施法者死亡或离开地图
 * - 光环被移除
 *
 * 执行流程：
 * 1. 发送消失动画（视觉效果）
 * 2. 从游戏世界移除
 * 3. 添加到待删除列表，延迟删除
 *
 * 注意：对象不会立即删除，而是加入删除队列，在下一帧清理。
 */
void DynamicObject::Remove()
{
    if (IsInWorld())
    {
        // 发送消失动画，让客户端看到淡出效果
        SendObjectDeSpawnAnim(GetGUID());
        // 从世界中移除对象
        RemoveFromWorld();
        // 添加到待删除列表，由地图系统在适当时机删除
        AddObjectToRemoveList();
    }
}

/**
 * @brief 获取剩余持续时间
 *
 * @return 剩余持续时间（毫秒）
 *
 * 如果动态对象关联了光环，返回光环的持续时间；
 * 否则返回对象自身的持续时间。
 *
 * 调用时机：在更新循环中检查对象是否过期时调用。
 */
int32 DynamicObject::GetDuration() const
{
    if (!_aura)
        return _duration;  // 返回自身持续时间
    else
        return _aura->GetDuration();  // 返回光环的持续时间
}

/**
 * @brief 设置持续时间
 *
 * @param newDuration 新的持续时间（毫秒）
 *
 * 如果动态对象关联了光环，设置光环的持续时间；
 * 否则设置对象自身的持续时间。
 *
 * 调用时机：
 * - 法术效果改变持续时间时（如强化效果）
 * - 延迟效果处理时
 */
void DynamicObject::SetDuration(int32 newDuration)
{
    if (!_aura)
        _duration = newDuration;  // 设置自身持续时间
    else
        _aura->SetDuration(newDuration);  // 设置光环持续时间
}

/**
 * @brief 延迟动态对象的效果
 *
 * @param delaytime 要减少的时间（毫秒）
 *
 * 将持续时间减少指定值。通常用于处理延迟效果，
 * 例如当目标离开区域后再进入时，减少剩余持续时间。
 */
void DynamicObject::Delay(int32 delaytime)
{
    SetDuration(GetDuration() - delaytime);
}

/**
 * @brief 设置关联的光环
 *
 * @param aura 要关联的光环对象
 *
 * 调用时机：当需要将光环效果绑定到动态对象时调用。
 * 例如：奉献、暴风雪等持续性区域法术会创建光环。
 *
 * 注意事项：
 * - 一个动态对象只能有一个光环
 * - 调用前必须确保当前没有光环
 * - 光环的生命周期通常与动态对象相同
 */
void DynamicObject::SetAura(Aura* aura)
{
    // 断言：必须没有现有光环，且新光环有效
    ASSERT(!_aura && aura);
    _aura = aura;
}

/**
 * @brief 移除关联的光环
 *
 * 调用时机：
 * - 动态对象从世界移除时
 * - 光环被驱散或取消时
 * - 法术效果结束时
 *
 * 执行流程：
 * 1. 将当前光环转移到 _removedAura（延迟删除机制）
 * 2. 清空当前光环引用
 * 3. 如果光环尚未被移除，调用光环的移除方法
 *
 * 延迟删除机制说明：
 * - _removedAura 保存已移除的光环，在析构时删除
 * - 这避免了在处理光环时出现悬空指针
 * - 确保光环对象在所有引用清除后才被销毁
 */
void DynamicObject::RemoveAura()
{
    // 断言：必须有光环，且没有待删除的光环
    ASSERT(_aura && !_removedAura);
    // 转移到 _removedAura，延迟删除
    _removedAura = _aura;
    _aura = nullptr;
    // 如果光环尚未被移除，执行移除操作
    if (!_removedAura->IsRemoved())
        _removedAura->_Remove(AURA_REMOVE_BY_DEFAULT);
}

/**
 * @brief 设置施法者的视野焦点到此动态对象
 *
 * 调用时机：当施放远视类法术（如鹰眼术、魔眼术）时调用。
 *
 * 执行流程：
 * 1. 检查施法者是否为玩家（只有玩家有视野系统）
 * 2. 将玩家的视角切换到此动态对象的位置
 * 3. 标记此对象为视野焦点
 *
 * 效果：玩家可以从此动态对象的位置观察周围环境，
 * 实现远程侦查功能。
 */
void DynamicObject::SetCasterViewpoint()
{
    // 只有玩家施法者才有视野系统
    if (Player* caster = _caster->ToPlayer())
    {
        // 设置玩家的视角到此动态对象
        caster->SetViewpoint(this, true);
        _isViewpoint = true;
    }
}

/**
 * @brief 移除施法者的视野焦点
 *
 * 调用时机：当远视效果结束时调用（如法术持续时间结束、被驱散、玩家取消）。
 *
 * 执行流程：
 * 1. 检查施法者是否为玩家
 * 2. 恢复玩家的正常视角
 * 3. 清除视野焦点标记
 *
 * 效果：玩家的视角恢复到自己的角色位置。
 */
void DynamicObject::RemoveCasterViewpoint()
{
    if (Player* caster = _caster->ToPlayer())
    {
        // 恢复玩家的正常视角
        caster->SetViewpoint(this, false);
        _isViewpoint = false;
    }
}

/**
 * @brief 获取阵营
 *
 * @return 施法者的阵营 ID
 *
 * 动态对象本身没有阵营，使用施法者的阵营。
 * 这用于阵营相关的判断，如：
 * - 敌我识别
 * - PVP 规则
 * - 法术目标选择
 */
uint32 DynamicObject::GetFaction() const
{
    // 施法者必须存在
    ASSERT(_caster);
    // 返回施法者的阵营
    return _caster->GetFaction();
}

/**
 * @brief 绑定到施法者
 *
 * 调用时机：动态对象添加到世界时调用。
 *
 * 执行流程：
 * 1. 通过 GUID 查找施法者单位
 * 2. 验证施法者存在且在同一地图
 * 3. 向施法者注册此动态对象
 *
 * 这建立了施法者与动态对象的双向引用关系，
 * 允许施法者管理其创建的动态对象。
 */
void DynamicObject::BindToCaster()
{
    // 断言：当前没有绑定到施法者
    ASSERT(!_caster);
    // 通过 GUID 获取施法者单位
    _caster = ObjectAccessor::GetUnit(*this, GetCasterGUID());
    // 断言：施法者必须存在
    ASSERT(_caster);
    // 断言：施法者必须在同一地图
    ASSERT(_caster->GetMap() == GetMap());
    // 向施法者注册此动态对象
    _caster->_RegisterDynObject(this);
}

/**
 * @brief 从施法者解绑
 *
 * 调用时机：动态对象从世界移除时调用。
 *
 * 执行流程：
 * 1. 向施法者注销此动态对象
 * 2. 清空施法者引用
 *
 * 这断开了施法者与动态对象的引用关系，
 * 防止悬空指针。
 */
void DynamicObject::UnbindFromCaster()
{
    // 断言：必须已绑定到施法者
    ASSERT(_caster);
    // 从施法者注销此动态对象
    _caster->_UnregisterDynObject(this);
    // 清空引用
    _caster = nullptr;
}

/**
 * @brief 获取法术信息
 *
 * @return 法术信息结构体指针，如果法术不存在则返回 null
 *
 * 通过法术 ID 从法术管理器获取法术的详细信息，
 * 包括法术效果、施法时间、冷却时间等。
 */
SpellInfo const* DynamicObject::GetSpellInfo() const
{
    return sSpellMgr->GetSpellInfo(GetSpellId());
}
