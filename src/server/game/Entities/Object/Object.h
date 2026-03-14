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
 * @file Object.h
 * @brief 游戏对象基类定义文件
 *
 * 本文件定义了 TrinityCore 中最核心的两个基类：
 * - Object: 所有游戏对象的基类，提供属性值管理、更新系统和类型识别
 * - WorldObject: 具有位置的世界对象基类，提供位置管理、可见性和交互功能
 *
 * 这两个类构成了游戏对象系统的核心架构，所有玩家、生物、物品、游戏对象等
 * 都继承自这些基类。
 *
 * 核心设计理念：
 * - 值更新系统：通过 UpdateMask 跟踪属性变化，高效同步到客户端
 * - 类型系统：使用 TypeID 和 TypeMask 实现类型识别和安全转型
 * - 网格系统：WorldObject 与地图网格交互，支持空间查询
 * - 可见性系统：支持隐身、潜行、服务器端可见性等多种机制
 */

#ifndef _OBJECT_H
#define _OBJECT_H

#include "Common.h"
#include "Duration.h"
#include "EventProcessor.h"
#include "ModelIgnoreFlags.h"
#include "MovementInfo.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "SharedDefines.h"
#include "SpellDefines.h"
#include "UniqueTrackablePtr.h"
#include "UpdateFields.h"
#include "UpdateMask.h"
#include <list>
#include <set>
#include <unordered_map>

class Corpse;
class Creature;
class CreatureAI;
class DynamicObject;
class GameObject;
class InstanceScript;
class Item;
class Map;
class Player;
class Spell;
class SpellCastTargets;
class SpellEffectInfo;
class SpellInfo;
class TempSummon;
class Transport;
class Unit;
class UpdateData;
class WorldObject;
class WorldPacket;
class ZoneScript;
struct FactionTemplateEntry;
struct PositionFullTerrainStatus;
struct QuaternionData;
enum ZLiquidStatus : uint32;

/**
 * @typedef UpdateDataMapType
 * @brief 更新数据映射类型
 *
 * 将玩家指针映射到其对应的更新数据。用于批量处理多个玩家的对象更新。
 */
typedef std::unordered_map<Player*, UpdateData> UpdateDataMapType;

/**
 * @brief 默认碰撞高度常量
 *
 * DBC 文件中最常见的碰撞高度值（约 2.03 单位）。
 * 用于没有特定碰撞高度定义的对象。
 */
float const DEFAULT_COLLISION_HEIGHT = 2.03128f; // DBC 文件中最常见的值

/**
 * @class Object
 * @brief 游戏对象基类，所有游戏对象的根类
 *
 * Object 类是 TrinityCore 中所有游戏对象的最顶层基类。它为所有派生类提供：
 * - 属性值系统：管理对象的各项属性（生命值、法力值、等级等）
 * - 更新系统：跟踪属性变化并同步到客户端
 * - 类型系统：通过 TypeID 和 TypeMask 实现类型识别
 * - GUID 管理：全局唯一标识符的存储和访问
 *
 * 继承关系：
 * - Object
 *   - WorldObject（具有位置的对象）
 *     - Unit（可战斗单位）
 *       - Player（玩家）
 *       - Creature（生物/宠物）
 *     - GameObject（游戏对象）
 *     - DynamicObject（动态对象）
 *     - Corpse（尸体）
 *   - Item（物品）
 *
 * 核心机制：
 * 1. 值更新系统：使用 m_int32Values/m_uint32Values/m_floatValues 联合体存储属性，
 *    通过 _changesMask 标记变化字段，在更新周期中发送到客户端。
 * 2. 类型安全：提供运行时类型检查（TypeID）和安全的向下转型方法。
 * 3. GUID 系统：每个对象都有全局唯一标识符，由类型、地图ID、实例ID和序号组成。
 *
 * 线程安全：
 * - 对象操作应在地图线程中进行
 * - 值更新通过 AddToObjectUpdate 添加到更新队列
 *
 * @see WorldObject 继承此类并添加位置信息
 * @see UpdateMask 更新掩码实现
 * @see UpdateFields 属性字段定义
 */
class TC_GAME_API Object
{
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类的析构函数正确调用，释放资源。
         * 析构时对象应该已经从世界中移除。
         */
        virtual ~Object();

        /**
         * @brief 检查对象是否在游戏世界中
         * @return 如果对象在世界中返回 true，否则返回 false
         *
         * 性能说明：O(1) 操作，直接返回成员变量。
         *
         * 使用时机：
         * - 在访问需要对象在世界中的功能前检查
         * - 在对象生命周期管理中使用
         */
        bool IsInWorld() const { return m_inWorld; }

        /**
         * @brief 将对象添加到游戏世界中
         *
         * 该函数将对象标记为在世界中，并执行必要的初始化操作。
         * 派生类可以重写此函数以执行额外的添加逻辑。
         *
         * 注意事项：
         * - 调用前应确保对象已正确初始化（GUID、地图等）
         * - WorldObject 会触发网格系统注册
         * - Player 会触发登录流程
         *
         * @see RemoveFromWorld 配对使用
         */
        virtual void AddToWorld();

        /**
         * @brief 将对象从游戏世界中移除
         *
         * 该函数将对象标记为不在世界中，并执行必要的清理操作。
         * 派生类可以重写此函数以执行额外的移除逻辑。
         *
         * 注意事项：
         * - 调用后对象不再被更新系统处理
         * - WorldObject 会从网格系统中注销
         * - 应在对象销毁前调用
         *
         * @see AddToWorld 配对使用
         */
        virtual void RemoveFromWorld();

        /**
         * @brief 静态方法：安全获取对象的 GUID
         * @param o 对象指针（可为 nullptr）
         * @return 对象的 GUID，如果指针为空则返回空 GUID
         *
         * 使用场景：处理可能为空的对象指针时安全获取 GUID。
         */
        static ObjectGuid GetGUID(Object const* o) { return o ? o->GetGUID() : ObjectGuid::Empty; }

        /**
         * @brief 获取对象的全局唯一标识符
         * @return 对象的 GUID
         *
         * 性能说明：O(1) 操作，直接读取属性值。
         */
        ObjectGuid GetGUID() const { return GetGuidValue(OBJECT_FIELD_GUID); }

        /**
         * @brief 获取打包后的 GUID
         * @return 打包格式的 GUID 引用
         *
         * 用途：网络传输优化，减少数据包大小。
         */
        PackedGuid const& GetPackGUID() const { return m_PackGUID; }

        /**
         * @brief 获取对象条目 ID
         * @return 条目 ID（对应 DBC 数据）
         *
         * 用途：查找模板数据、生物/物品定义等。
         */
        uint32 GetEntry() const { return GetUInt32Value(OBJECT_FIELD_ENTRY); }

        /**
         * @brief 设置对象条目 ID
         * @param entry 条目 ID
         *
         * 注意：通常只在对象创建时设置。
         */
        void SetEntry(uint32 entry) { SetUInt32Value(OBJECT_FIELD_ENTRY, entry); }

        /**
         * @brief 获取对象缩放比例
         * @return 缩放比例（1.0 为正常大小）
         */
        float GetObjectScale() const { return GetFloatValue(OBJECT_FIELD_SCALE_X); }

        /**
         * @brief 设置对象缩放比例
         * @param scale 新的缩放比例
         *
         * 派生类可重写以触发额外的更新逻辑（如碰撞体积变化）。
         */
        virtual void SetObjectScale(float scale) { SetFloatValue(OBJECT_FIELD_SCALE_X, scale); }

        /**
         * @brief 获取动态标志
         * @return 动态标志位组合
         *
         * 动态标志用于客户端特殊效果（如尸体 loot 光效）。
         */
        virtual uint32 GetDynamicFlags() const { return 0; }

        /**
         * @brief 检查是否具有指定动态标志
         * @param flag 要检查的标志
         * @return 如果设置了该标志返回 true
         */
        bool HasDynamicFlag(uint32 flag) const { return (GetDynamicFlags() & flag) != 0; }

        /**
         * @brief 添加动态标志
         * @param flag 要添加的标志
         */
        virtual void SetDynamicFlag(uint32 flag) { ReplaceAllDynamicFlags(GetDynamicFlags() | flag); }

        /**
         * @brief 移除动态标志
         * @param flag 要移除的标志
         */
        virtual void RemoveDynamicFlag(uint32 flag) { ReplaceAllDynamicFlags(GetDynamicFlags() & ~flag); }

        /**
         * @brief 替换所有动态标志
         * @param flag 新的标志位组合
         */
        virtual void ReplaceAllDynamicFlags([[maybe_unused]] uint32 flag) { }

        /**
         * @brief 获取对象类型 ID
         * @return 类型 ID（TYPEID_PLAYER、TYPEID_UNIT 等）
         *
         * 用途：运行时类型识别，区分具体对象类型。
         */
        TypeID GetTypeId() const { return m_objectTypeId; }

        /**
         * @brief 检查对象是否属于指定类型
         * @param mask 类型掩码（可组合多个类型）
         * @return 如果匹配任一类型返回 true
         *
         * 示例：
         * @code
         * if (obj->isType(TYPEMASK_UNIT))
         *     // 对象是 Unit、Player 或 Creature
         * @endcode
         */
        bool isType(uint16 mask) const { return (mask & m_objectType) != 0; }

        /**
         * @brief 为玩家构建创建更新块
         * @param data 更新数据缓冲区
         * @param target 目标玩家
         *
         * 当对象首次出现在玩家视野中时调用，发送完整对象数据。
         */
        virtual void BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const;

        /**
         * @brief 向指定玩家发送更新
         * @param player 目标玩家
         */
        void SendUpdateToPlayer(Player* player);

        /**
         * @brief 为玩家构建值更新块
         * @param data 更新数据缓冲区
         * @param target 目标玩家（const）
         *
         * 发送对象属性的变化部分。
         */
        void BuildValuesUpdateBlockForPlayer(UpdateData* data, Player const* target) const;

        /**
         * @brief 构建超出范围更新块
         * @param data 更新数据缓冲区
         *
         * 当对象离开玩家视野时调用，通知客户端销毁对象。
         */
        void BuildOutOfRangeUpdateBlock(UpdateData* data) const;

        /**
         * @brief 构建移动更新块
         * @param data 更新数据缓冲区
         * @param flags 移动标志
         */
        void BuildMovementUpdateBlock(UpdateData* data, uint32 flags = 0) const;

        /**
         * @brief 为玩家销毁对象
         * @param target 目标玩家
         * @param onDeath 是否因为死亡而销毁
         *
         * 通知客户端移除对象。死亡时有特殊视觉效果。
         */
        virtual void DestroyForPlayer(Player* target, bool onDeath = false) const;

        /**
         * @brief 获取指定索引处的32位有符号整数值
         * @param index 属性索引
         * @return 指定索引处的值
         */
        int32 GetInt32Value(uint16 index) const;

        /**
         * @brief 获取指定索引处的32位无符号整数值
         * @param index 属性索引
         * @return 指定索引处的值
         */
        uint32 GetUInt32Value(uint16 index) const;

        /**
         * @brief 获取指定索引处的64位无符号整数值
         * @param index 属性索引
         * @return 指定索引处的值
         */
        uint64 GetUInt64Value(uint16 index) const;

        /**
         * @brief 获取指定索引处的浮点值
         * @param index 属性索引
         * @return 指定索引处的值
         */
        float GetFloatValue(uint16 index) const;

        /**
         * @brief 获取指定索引和偏移处的字节值
         * @param index 属性索引
         * @param offset 字节偏移（0-3）
         * @return 指定位置的字节值
         */
        uint8 GetByteValue(uint16 index, uint8 offset) const;

        /**
         * @brief 获取指定索引和偏移处的16位无符号整数值
         * @param index 属性索引
         * @param offset 字偏移（0-1）
         * @return 指定位置的值
         */
        uint16 GetUInt16Value(uint16 index, uint8 offset) const;

        /**
         * @brief 获取指定索引处的GUID值
         * @param index 属性索引
         * @return 指定索引处的GUID
         */
        ObjectGuid GetGuidValue(uint16 index) const;

        /**
         * @brief 设置指定索引处的32位有符号整数值
         * @param index 属性索引
         * @param value 要设置的值
         */
        void SetInt32Value(uint16 index, int32 value);

        /**
         * @brief 设置指定索引处的32位无符号整数值
         *
         * 该函数用于设置对象的属性值，并自动标记该属性为已修改。
         * 这将触发更新系统将新值同步到客户端。
         *
         * @param index 属性索引（对应UpdateFields.h中定义的字段）
         * @param value 要设置的值
         */
        void SetUInt32Value(uint16 index, uint32 value);

        /**
         * @brief 更新指定索引处的32位无符号整数值（不触发更新标记）
         * @param index 属性索引
         * @param value 要设置的值
         */
        void UpdateUInt32Value(uint16 index, uint32 value);
        /**
         * @brief 设置指定索引处的64位无符号整数值
         * @param index 属性索引
         * @param value 要设置的值
         *
         * 用于设置跨越两个32位字段的64位值（如 GUID）。
         */
        void SetUInt64Value(uint16 index, uint64 value);

        /**
         * @brief 设置指定索引处的浮点值
         * @param index 属性索引
         * @param value 要设置的值
         */
        void SetFloatValue(uint16 index, float value);

        /**
         * @brief 设置指定索引和偏移处的字节值
         * @param index 属性索引
         * @param offset 字节偏移（0-3）
         * @param value 要设置的字节值
         *
         * 允许在单个32位字段中存储多个独立字节值。
         */
        void SetByteValue(uint16 index, uint8 offset, uint8 value);

        /**
         * @brief 设置指定索引和偏移处的16位无符号整数值
         * @param index 属性索引
         * @param offset 字偏移（0-1）
         * @param value 要设置的值
         */
        void SetUInt16Value(uint16 index, uint8 offset, uint16 value);

        /**
         * @brief 设置指定索引和偏移处的16位有符号整数值
         * @param index 属性索引
         * @param offset 字偏移（0-1）
         * @param value 要设置的值
         */
        void SetInt16Value(uint16 index, uint8 offset, int16 value) { SetUInt16Value(index, offset, (uint16)value); }

        /**
         * @brief 设置指定索引处的GUID值
         * @param index 属性索引
         * @param value 要设置的GUID
         */
        void SetGuidValue(uint16 index, ObjectGuid value);

        /**
         * @brief 设置属性浮点值（用于统计属性）
         * @param index 属性索引
         * @param value 要设置的值
         *
         * 与 SetFloatValue 类似，但可能有额外的逻辑处理负值。
         */
        void SetStatFloatValue(uint16 index, float value);

        /**
         * @brief 设置属性整数值（用于统计属性）
         * @param index 属性索引
         * @param value 要设置的值
         */
        void SetStatInt32Value(uint16 index, int32 value);

        /**
         * @brief 添加 GUID 到列表字段
         * @param index 列表字段起始索引
         * @param value 要添加的 GUID
         * @return 如果成功添加返回 true
         */
        bool AddGuidValue(uint16 index, ObjectGuid value);

        /**
         * @brief 从列表字段移除 GUID
         * @param index 列表字段起始索引
         * @param value 要移除的 GUID
         * @return 如果成功移除返回 true
         */
        bool RemoveGuidValue(uint16 index, ObjectGuid value);

        /**
         * @brief 应用修改值到无符号整数字段
         * @param index 属性索引
         * @param val 修改值（正数增加，负数减少）
         * @param apply true 为添加，false 为减去
         */
        void ApplyModUInt32Value(uint16 index, int32 val, bool apply);

        /**
         * @brief 应用修改值到有符号整数字段
         * @param index 属性索引
         * @param val 修改值
         * @param apply true 为添加，false 为减去
         */
        void ApplyModInt32Value(uint16 index, int32 val, bool apply);

        /**
         * @brief 应用修改值到正浮点字段
         * @param index 属性索引
         * @param val 修改值
         * @param apply true 为添加，false 为减去
         *
         * 确保值不会变为负数。
         */
        void ApplyModPositiveFloatValue(uint16 index, float val, bool apply);

        /**
         * @brief 应用修改值到有符号浮点字段
         * @param index 属性索引
         * @param val 修改值
         * @param apply true 为添加，false 为减去
         */
        void ApplyModSignedFloatValue(uint16 index, float val, bool apply);

        /**
         * @brief 设置标志位
         * @param index 属性索引
         * @param newFlag 要设置的标志
         */
        void SetFlag(uint16 index, uint32 newFlag);

        /**
         * @brief 移除标志位
         * @param index 属性索引
         * @param oldFlag 要移除的标志
         */
        void RemoveFlag(uint16 index, uint32 oldFlag);

        /**
         * @brief 切换标志位
         * @param index 属性索引
         * @param flag 要切换的标志
         */
        void ToggleFlag(uint16 index, uint32 flag);

        /**
         * @brief 检查是否设置了指定标志
         * @param index 属性索引
         * @param flag 要检查的标志
         * @return 如果设置了该标志返回 true
         */
        bool HasFlag(uint16 index, uint32 flag) const;

        /**
         * @brief 应用或移除标志
         * @param index 属性索引
         * @param flag 要操作的标志
         * @param apply true 为设置，false 为移除
         */
        void ApplyModFlag(uint16 index, uint32 flag, bool apply);

        /**
         * @brief 设置字节标志位
         * @param index 属性索引
         * @param offset 字节偏移
         * @param newFlag 要设置的标志
         */
        void SetByteFlag(uint16 index, uint8 offset, uint8 newFlag);

        /**
         * @brief 移除字节标志位
         * @param index 属性索引
         * @param offset 字节偏移
         * @param newFlag 要移除的标志
         */
        void RemoveByteFlag(uint16 index, uint8 offset, uint8 newFlag);

        /**
         * @brief 切换字节标志位
         * @param index 属性索引
         * @param offset 字节偏移
         * @param flag 要切换的标志
         */
        void ToggleByteFlag(uint16 index, uint8 offset, uint8 flag);

        /**
         * @brief 检查是否设置了指定字节标志
         * @param index 属性索引
         * @param offset 字节偏移
         * @param flag 要检查的标志
         * @return 如果设置了该标志返回 true
         */
        bool HasByteFlag(uint16 index, uint8 offset, uint8 flag) const;

        /**
         * @brief 应用或移除字节标志
         * @param index 属性索引
         * @param offset 字节偏移
         * @param flag 要操作的标志
         * @param apply true 为设置，false 为移除
         */
        void ApplyModByteFlag(uint16 index, uint8 offset, uint8 flag, bool apply);

        /**
         * @brief 设置64位标志
         * @param index 属性索引
         * @param newFlag 要设置的标志
         */
        void SetFlag64(uint16 index, uint64 newFlag);

        /**
         * @brief 移除64位标志
         * @param index 属性索引
         * @param oldFlag 要移除的标志
         */
        void RemoveFlag64(uint16 index, uint64 oldFlag);

        /**
         * @brief 切换64位标志
         * @param index 属性索引
         * @param flag 要切换的标志
         */
        void ToggleFlag64(uint16 index, uint64 flag);

        /**
         * @brief 检查是否设置了指定64位标志
         * @param index 属性索引
         * @param flag 要检查的标志
         * @return 如果设置了该标志返回 true
         */
        bool HasFlag64(uint16 index, uint64 flag) const;

        /**
         * @brief 应用或移除64位标志
         * @param index 属性索引
         * @param flag 要操作的标志
         * @param apply true 为设置，false 为移除
         */
        void ApplyModFlag64(uint16 index, uint64 flag, bool apply);

        /**
         * @brief 清除更新掩码
         * @param remove 是否同时从更新队列移除
         *
         * 在更新周期结束后调用，清除已发送的变化标记。
         */
        void ClearUpdateMask(bool remove);

        /**
         * @brief 获取属性值数组大小
         * @return 属性值数量
         */
        uint16 GetValuesCount() const { return m_valuesCount; }

        /**
         * @brief 检查对象是否提供指定任务
         * @param quest_id 任务 ID
         * @return 如果对象提供该任务返回 true
         *
         * 由任务给予者（NPC、物品等）重写。
         */
        virtual bool hasQuest(uint32 /* quest_id */) const { return false; }

        /**
         * @brief 检查对象是否涉及指定任务
         * @param quest_id 任务 ID
         * @return 如果对象涉及该任务返回 true
         *
         * 由任务目标对象重写。
         */
        virtual bool hasInvolvedQuest(uint32 /* quest_id */) const { return false; }

        /**
         * @brief 设置对象是否为新创建
         * @param enable true 为新对象
         *
         * 新对象在首次更新时会发送完整数据。
         */
        void SetIsNewObject(bool enable) { m_isNewObject = enable; }

        /**
         * @brief 构建更新数据
         * @param updateDataMap 更新数据映射
         *
         * 派生类重写以添加自己的更新逻辑。
         */
        virtual void BuildUpdate(UpdateDataMapType&) { }

        /**
         * @brief 为玩家构建字段更新
         * @param player 目标玩家
         * @param data 更新数据映射
         */
        void BuildFieldsUpdate(Player*, UpdateDataMapType &) const;

        /**
         * @brief 设置字段通知标志
         * @param flag 要设置的标志
         */
        void SetFieldNotifyFlag(uint16 flag) { _fieldNotifyFlags |= flag; }

        /**
         * @brief 移除字段通知标志
         * @param flag 要移除的标志
         */
        void RemoveFieldNotifyFlag(uint16 flag) { _fieldNotifyFlags &= uint16(~flag); }

        /**
         * @brief 强制更新指定索引的值
         * @param index 属性索引
         *
         * 用于特殊情况下需要立即发送更新的场景。
         */
        void ForceValuesUpdateAtIndex(uint32);

        // ============ 类型检查和转换方法 ============

        /**
         * @brief 检查对象是否为 WorldObject 类型
         * @return 如果是 WorldObject 类型返回 true
         */
        inline bool IsWorldObject() const { return isType(TYPEMASK_WORLDOBJECT); }

        /**
         * @brief 静态转换：安全地将 Object 转换为 WorldObject
         * @param o 对象指针（可为 nullptr）
         * @return WorldObject 指针，转换失败返回 nullptr
         */
        static WorldObject* ToWorldObject(Object* o) { return o ? o->ToWorldObject() : nullptr; }
        static WorldObject const* ToWorldObject(Object const* o) { return o ? o->ToWorldObject() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 WorldObject
         * @return WorldObject 指针，如果不是该类型返回 nullptr
         */
        WorldObject* ToWorldObject() { if (IsWorldObject()) return reinterpret_cast<WorldObject*>(this); else return nullptr; }
        WorldObject const* ToWorldObject() const { if (IsWorldObject()) return reinterpret_cast<WorldObject const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 Item 类型
         * @return 如果是 Item 类型返回 true
         */
        inline bool IsItem() const { return isType(TYPEMASK_ITEM); }

        /**
         * @brief 静态转换：安全地将 Object 转换为 Item
         * @param o 对象指针（可为 nullptr）
         * @return Item 指针，转换失败返回 nullptr
         */
        static Item* ToItem(Object* o) { return o ? o->ToItem() : nullptr; }
        static Item const* ToItem(Object const* o) { return o ? o->ToItem() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 Item
         * @return Item 指针，如果不是该类型返回 nullptr
         */
        Item* ToItem() { if (IsItem()) return reinterpret_cast<Item*>(this); else return nullptr; }
        Item const* ToItem() const { if (IsItem()) return reinterpret_cast<Item const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 Player 类型
         * @return 如果是 Player 类型返回 true
         */
        inline bool IsPlayer() const { return GetTypeId() == TYPEID_PLAYER; }

        /**
         * @brief 静态转换：安全地将 Object 转换为 Player
         * @param o 对象指针（可为 nullptr）
         * @return Player 指针，转换失败返回 nullptr
         */
        static Player* ToPlayer(Object* o) { return o ? o->ToPlayer() : nullptr; }
        static Player const* ToPlayer(Object const* o) { return o ? o->ToPlayer() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 Player
         * @return Player 指针，如果不是该类型返回 nullptr
         */
        Player* ToPlayer() { if (IsPlayer()) return reinterpret_cast<Player*>(this); else return nullptr; }
        Player const* ToPlayer() const { if (IsPlayer()) return reinterpret_cast<Player const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 Creature 类型
         * @return 如果是 Creature 类型返回 true
         */
        inline bool IsCreature() const { return GetTypeId() == TYPEID_UNIT; }

        /**
         * @brief 静态转换：安全地将 Object 转换为 Creature
         * @param o 对象指针（可为 nullptr）
         * @return Creature 指针，转换失败返回 nullptr
         */
        static Creature* ToCreature(Object* o) { return o ? o->ToCreature() : nullptr; }
        static Creature const* ToCreature(Object const* o) { return o ? o->ToCreature() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 Creature
         * @return Creature 指针，如果不是该类型返回 nullptr
         */
        Creature* ToCreature() { if (IsCreature()) return reinterpret_cast<Creature*>(this); else return nullptr; }
        Creature const* ToCreature() const { if (IsCreature()) return reinterpret_cast<Creature const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 Unit 类型（Player 或 Creature）
         * @return 如果是 Unit 类型返回 true
         */
        inline bool IsUnit() const { return isType(TYPEMASK_UNIT); }

        /**
         * @brief 静态转换：安全地将 Object 转换为 Unit
         * @param o 对象指针（可为 nullptr）
         * @return Unit 指针，转换失败返回 nullptr
         */
        static Unit* ToUnit(Object* o) { return o ? o->ToUnit() : nullptr; }
        static Unit const* ToUnit(Object const* o) { return o ? o->ToUnit() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 Unit
         * @return Unit 指针，如果不是该类型返回 nullptr
         */
        Unit* ToUnit() { if (IsUnit()) return reinterpret_cast<Unit*>(this); else return nullptr; }
        Unit const* ToUnit() const { if (IsUnit()) return reinterpret_cast<Unit const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 GameObject 类型
         * @return 如果是 GameObject 类型返回 true
         */
        inline bool IsGameObject() const { return GetTypeId() == TYPEID_GAMEOBJECT; }

        /**
         * @brief 静态转换：安全地将 Object 转换为 GameObject
         * @param o 对象指针（可为 nullptr）
         * @return GameObject 指针，转换失败返回 nullptr
         */
        static GameObject* ToGameObject(Object* o) { return o ? o->ToGameObject() : nullptr; }
        static GameObject const* ToGameObject(Object const* o) { return o ? o->ToGameObject() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 GameObject
         * @return GameObject 指针，如果不是该类型返回 nullptr
         */
        GameObject* ToGameObject() { if (IsGameObject()) return reinterpret_cast<GameObject*>(this); else return nullptr; }
        GameObject const* ToGameObject() const { if (IsGameObject()) return reinterpret_cast<GameObject const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 Corpse 类型
         * @return 如果是 Corpse 类型返回 true
         */
        inline bool IsCorpse() const { return GetTypeId() == TYPEID_CORPSE; }

        /**
         * @brief 静态转换：安全地将 Object 转换为 Corpse
         * @param o 对象指针（可为 nullptr）
         * @return Corpse 指针，转换失败返回 nullptr
         */
        static Corpse* ToCorpse(Object* o) { return o ? o->ToCorpse() : nullptr; }
        static Corpse const* ToCorpse(Object const* o) { return o ? o->ToCorpse() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 Corpse
         * @return Corpse 指针，如果不是该类型返回 nullptr
         */
        Corpse* ToCorpse() { if (IsCorpse()) return reinterpret_cast<Corpse*>(this); else return nullptr; }
        Corpse const* ToCorpse() const { if (IsCorpse()) return reinterpret_cast<Corpse const*>(this); else return nullptr; }

        /**
         * @brief 检查对象是否为 DynamicObject 类型
         * @return 如果是 DynamicObject 类型返回 true
         */
        inline bool IsDynObject() const { return GetTypeId() == TYPEID_DYNAMICOBJECT; }

        /**
         * @brief 静态转换：安全地将 Object 转换为 DynamicObject
         * @param o 对象指针（可为 nullptr）
         * @return DynamicObject 指针，转换失败返回 nullptr
         */
        static DynamicObject* ToDynObject(Object* o) { return o ? o->ToDynObject() : nullptr; }
        static DynamicObject const* ToDynObject(Object const* o) { return o ? o->ToDynObject() : nullptr; }

        /**
         * @brief 实例转换：将当前对象转换为 DynamicObject
         * @return DynamicObject 指针，如果不是该类型返回 nullptr
         */
        DynamicObject* ToDynObject() { if (IsDynObject()) return reinterpret_cast<DynamicObject*>(this); else return nullptr; }
        DynamicObject const* ToDynObject() const { if (IsDynObject()) return reinterpret_cast<DynamicObject const*>(this); else return nullptr; }

        /**
         * @brief 获取调试信息字符串
         * @return 调试信息
         */
        virtual std::string GetDebugInfo() const;

        /**
         * @brief 获取对象的弱引用指针
         * @return 弱引用指针
         *
         * 用于脚本系统安全引用对象，避免悬空指针。
         */
        Trinity::unique_weak_ptr<Object> GetWeakPtr() const { return m_scriptRef; }

    protected:
        /**
         * @brief 受保护的构造函数
         *
         * Object 是抽象基类，不能直接实例化。
         * 派生类必须调用此构造函数进行初始化。
         */
        Object();

        /**
         * @brief 初始化属性值数组
         *
         * 分配属性值数组并初始化更新掩码。
         * 由 _Create 调用。
         */
        void _InitValues();

        /**
         * @brief 创建对象核心数据
         * @param guidlow GUID 低位部分（对象序号）
         * @param entry 条目 ID
         * @param guidhigh GUID 高位类型
         *
         * 设置对象的基本属性：GUID、条目 ID、缩放等。
         */
        void _Create(ObjectGuid::LowType guidlow, uint32 entry, HighGuid guidhigh);

        /**
         * @brief 连接多个字段值为字符串
         * @param startIndex 起始索引
         * @param size 字段数量
         * @return 连接后的字符串
         */
        std::string _ConcatFields(uint16 startIndex, uint16 size) const;

        /**
         * @brief 从字符串加载数据到字段
         * @param data 数据字符串
         * @param startOffset 起始偏移
         * @param count 字段数量
         * @return 如果成功返回 true
         *
         * 用于从数据库加载数据字段（如字节数组）。
         */
        [[nodiscard]] bool _LoadIntoDataField(std::string const& data, uint32 startOffset, uint32 count);

        /**
         * @brief 获取更新字段数据
         * @param target 目标玩家
         * @param flags 输出标志数组
         * @return 更新标志掩码
         */
        uint32 GetUpdateFieldData(Player const* target, uint32*& flags) const;

        /**
         * @brief 构建移动更新数据
         * @param data 字节缓冲区
         * @param flags 移动标志
         */
        void BuildMovementUpdate(ByteBuffer* data, uint16 flags) const;

        /**
         * @brief 构建值更新数据
         * @param updatetype 更新类型
         * @param data 字节缓冲区
         * @param target 目标玩家
         */
        virtual void BuildValuesUpdate(uint8 updatetype, ByteBuffer* data, Player const* target) const;

        uint16 m_objectType;           ///< 对象类型掩码，用于标识对象的类型组合（如TYPEMASK_UNIT、TYPEMASK_PLAYER等）

        TypeID m_objectTypeId;         ///< 对象类型ID，标识具体对象类型（如TYPEID_PLAYER、TYPEID_UNIT等）
        uint16 m_updateFlag;           ///< 更新标志，用于控制对象更新行为

        /**
         * @brief 属性值联合体
         *
         * 使用联合体存储属性值，允许以不同类型访问同一块内存。
         * 这允许根据字段类型使用整数或浮点数解释。
         */
        union
        {
            int32  *m_int32Values;     ///< 整型属性值数组（有符号）
            uint32 *m_uint32Values;    ///< 整型属性值数组（无符号）
            float  *m_floatValues;     ///< 浮点型属性值数组
        };

        UpdateMask _changesMask;       ///< 更新掩码，记录哪些属性值发生了变化

        uint16 m_valuesCount;          ///< 属性值数组的元素数量

        uint16 _fieldNotifyFlags;      ///< 字段通知标志，用于控制字段更新的通知行为

        /**
         * @brief 将对象添加到更新系统
         * @return 如果成功添加返回 true
         *
         * 纯虚函数，由派生类实现。将对象注册到地图的更新队列中。
         */
        virtual bool AddToObjectUpdate() = 0;

        /**
         * @brief 从更新系统移除对象
         *
         * 纯虚函数，由派生类实现。从地图的更新队列中注销对象。
         */
        virtual void RemoveFromObjectUpdate() = 0;

        /**
         * @brief 如果需要，将对象添加到更新队列
         *
         * 当对象属性发生变化时调用，确保更新系统处理该对象。
         */
        void AddToObjectUpdateIfNeeded();

        bool m_objectUpdated;          ///< 对象是否已添加到更新队列

    private:
        bool m_inWorld;                ///< 对象是否在游戏世界中
        bool m_isNewObject;            ///< 是否为新创建的对象

        PackedGuid m_PackGUID;         ///< 打包后的GUID，用于网络传输优化

        /**
         * @brief 空操作对象删除器
         *
         * 用于 unique_trackable_ptr，不实际删除对象。
         * 对象生命周期由地图系统管理。
         */
        struct NoopObjectDeleter { void operator()(Object*) const { /*noop - not managed*/ } };

        Trinity::unique_trackable_ptr<Object> m_scriptRef;  ///< 脚本系统的跟踪指针

        /**
         * @brief 打印索引错误信息
         * @param index 错误的索引
         * @param set 是否是设置操作
         * @return 总是返回 false
         *
         * 用于断言失败时输出详细的错误信息。
         */
        bool PrintIndexError(uint32 index, bool set) const;

        // 禁止拷贝和移动
        Object(Object const& right) = delete;
        Object(Object&& right) = delete;
        Object& operator=(Object const& right) = delete;
        Object& operator=(Object&& right) = delete;
};

/**
 * @brief 带标志的值数组模板类
 * @tparam T_VALUES 值类型
 * @tparam T_FLAGS 标志类型
 * @tparam FLAG_TYPE 标志枚举类型
 * @tparam ARRAY_SIZE 数组大小
 *
 * 用于管理一组相关的值和对应的标志。
 * 主要用于隐身、潜行等可见性系统。
 */
template <class T_VALUES, class T_FLAGS, class FLAG_TYPE, size_t ARRAY_SIZE>
class FlaggedValuesArray32
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化所有值为0，标志为0。
         */
        FlaggedValuesArray32()
        {
            for (uint32 i = 0; i < ARRAY_SIZE; ++i)
                m_values[i] = T_VALUES(0);
            m_flags = 0;
        }

        /**
         * @brief 获取标志位
         * @return 当前标志位组合
         */
        T_FLAGS GetFlags() const { return m_flags; }

        /**
         * @brief 检查是否设置了指定标志
         * @param flag 要检查的标志
         * @return 如果设置了该标志返回true
         */
        bool HasFlag(FLAG_TYPE flag) const { return m_flags & (1 << flag); }

        /**
         * @brief 添加标志
         * @param flag 要添加的标志
         */
        void AddFlag(FLAG_TYPE flag) { m_flags |= (1 << flag); }

        /**
         * @brief 删除标志
         * @param flag 要删除的标志
         */
        void DelFlag(FLAG_TYPE flag) { m_flags &= ~(1 << flag); }

        /**
         * @brief 获取指定标志对应的值
         * @param flag 标志索引
         * @return 对应的值
         */
        T_VALUES GetValue(FLAG_TYPE flag) const { return m_values[flag]; }

        /**
         * @brief 设置指定标志对应的值
         * @param flag 标志索引
         * @param value 要设置的值
         */
        void SetValue(FLAG_TYPE flag, T_VALUES value) { m_values[flag] = value; }

        /**
         * @brief 增加指定标志对应的值
         * @param flag 标志索引
         * @param value 要增加的值
         */
        void AddValue(FLAG_TYPE flag, T_VALUES value) { m_values[flag] += value; }

    private:
        T_VALUES m_values[ARRAY_SIZE];  ///< 值数组
        T_FLAGS m_flags;                 ///< 标志位组合
};

/**
 * @class WorldObject
 * @brief 世界对象类，表示游戏世界中具有位置的对象
 *
 * WorldObject 继承自 Object 和 WorldLocation，为游戏世界中所有具有位置的对象
 * 提供基础功能。包括玩家、生物、游戏对象、动态对象、尸体等。
 *
 * 主要功能：
 * - 位置管理和距离计算
 * - 相位系统支持
 * - 区域和地带信息管理
 * - 可见性和侦测系统
 * - 召唤和生成对象
 * - 法术施放辅助
 * - 阵营和声望关系
 * - 事件处理器
 *
 * 设计模式：
 * - 使用虚函数实现多态
 * - 提供类型安全的向下转型方法
 * - 使用事件处理器实现延迟执行
 */
class TC_GAME_API WorldObject : public Object, public WorldLocation
{
    protected:
        /**
         * @brief 受保护的构造函数
         * @param isWorldObject 是否存储在世界对象网格容器中
         *
         * isWorldObject 参数决定对象在网格系统中的存储方式：
         * - true：存储在世界对象列表，始终更新（适用于玩家、活动对象等）
         * - false：存储在网格对象列表，仅在玩家附近更新（适用于普通生物等）
         *
         * 这影响性能和内存使用，是优化系统的重要参数。
         */
        explicit WorldObject(bool isWorldObject);
    public:
        /**
         * @brief 虚析构函数
         *
         * 清理资源，调用 CleanupsBeforeDelete。
         */
        virtual ~WorldObject();

        /**
         * @brief 更新对象状态
         * @param time_diff 距离上次更新的时间差（毫秒）
         *
         * 派生类可重写此函数实现定期更新逻辑。
         * 每个地图更新周期（通常每秒多次）调用一次。
         */
        virtual void Update(uint32 /*time_diff*/) { }

        /**
         * @brief 创建 WorldObject 核心
         * @param guidlow GUID 低位部分
         * @param guidhigh GUID 高位类型
         * @param phaseMask 相位掩码
         *
         * 初始化 WorldObject 的基本属性，包括相位和位置。
         */
        void _Create(ObjectGuid::LowType guidlow, HighGuid guidhigh, uint32 phaseMask);

        void AddToWorld() override;
        void RemoveFromWorld() override;

        // ============ 位置计算方法 ============

        /**
         * @brief 获取附近的2D坐标点
         * @param searcher 搜索者对象
         * @param x 输出X坐标
         * @param y 输出Y坐标
         * @param distance 距离
         * @param absAngle 绝对角度
         */
        void GetNearPoint2D(WorldObject const* searcher, float& x, float& y, float distance, float absAngle) const;

        /**
         * @brief 获取附近的3D坐标点
         * @param searcher 搜索者对象
         * @param x 输出X坐标
         * @param y 输出Y坐标
         * @param z 输出Z坐标
         * @param distance2d 2D距离
         * @param absAngle 绝对角度
         */
        void GetNearPoint(WorldObject const* searcher, float& x, float& y, float& z, float distance2d, float absAngle) const;

        /**
         * @brief 获取近距离接触点
         * @param x 输出X坐标
         * @param y 输出Y坐标
         * @param z 输出Z坐标
         * @param size 对象大小
         * @param distance2d 2D距离
         * @param relAngle 相对角度
         */
        void GetClosePoint(float& x, float& y, float& z, float size, float distance2d = 0, float relAngle = 0) const;

        /**
         * @brief 移动位置到指定距离和角度
         * @param pos 输入位置，也用于输出
         * @param dist 移动距离
         * @param angle 移动角度
         */
        void MovePosition(Position &pos, float dist, float angle);

        /**
         * @brief 获取附近位置
         * @param dist 距离
         * @param angle 角度
         * @return 新位置
         */
        Position GetNearPosition(float dist, float angle);

        /**
         * @brief 移动位置到首次碰撞处
         * @param pos 输入位置，也用于输出
         * @param dist 移动距离
         * @param angle 移动角度
         */
        void MovePositionToFirstCollision(Position &pos, float dist, float angle);

        /**
         * @brief 获取首次碰撞位置
         * @param dist 距离
         * @param angle 角度
         * @return 碰撞位置
         */
        Position GetFirstCollisionPosition(float dist, float angle);

        /**
         * @brief 获取随机附近位置
         * @param radius 搜索半径
         * @return 随机位置
         */
        Position GetRandomNearPosition(float radius);

        /**
         * @brief 获取与另一对象的接触点
         * @param obj 目标对象
         * @param x 输出X坐标
         * @param y 输出Y坐标
         * @param z 输出Z坐标
         * @param distance2d 2D距离
         */
        void GetContactPoint(WorldObject const* obj, float& x, float& y, float& z, float distance2d = CONTACT_DISTANCE) const;

        /**
         * @brief 获取战斗触及距离
         * @return 战斗触及半径
         *
         * 由 Unit 重写，其他对象返回 0。
         */
        virtual float GetCombatReach() const { return 0.0f; }

        /**
         * @brief 更新地面高度
         * @param x X坐标
         * @param y Y坐标
         * @param z 输入输出Z坐标
         */
        void UpdateGroundPositionZ(float x, float y, float &z) const;

        /**
         * @brief 更新允许的高度
         * @param x X坐标
         * @param y Y坐标
         * @param z 输入输出Z坐标
         * @param groundZ 输出地面高度
         */
        void UpdateAllowedPositionZ(float x, float y, float &z, float* groundZ = nullptr) const;

        /**
         * @brief 获取随机点
         * @param srcPos 源位置
         * @param distance 最大距离
         * @param rand_x 输出X坐标
         * @param rand_y 输出Y坐标
         * @param rand_z 输出Z坐标
         */
        void GetRandomPoint(Position const& srcPos, float distance, float& rand_x, float& rand_y, float& rand_z) const;

        /**
         * @brief 获取随机点
         * @param srcPos 源位置
         * @param distance 最大距离
         * @return 随机位置
         */
        Position GetRandomPoint(Position const& srcPos, float distance) const;

        // ============ 实例和相位系统 ============

        /**
         * @brief 获取实例ID
         * @return 实例ID
         */
        uint32 GetInstanceId() const { return m_InstanceId; }

        /**
         * @brief 设置相位掩码
         * @param newPhaseMask 新的相位掩码
         * @param update 是否立即更新可见性
         */
        virtual void SetPhaseMask(uint32 newPhaseMask, bool update);

        /**
         * @brief 获取相位掩码
         * @return 相位掩码
         */
        uint32 GetPhaseMask() const { return m_phaseMask; }

        /**
         * @brief 检查是否在同一相位
         * @param phasemask 要检查的相位掩码
         * @return 如果有共同相位返回 true
         */
        bool InSamePhase(uint32 phasemask) const { return (GetPhaseMask() & phasemask) != 0; }

        /**
         * @brief 检查是否与另一对象在同一相位
         * @param obj 另一对象
         * @return 如果在同一相位返回 true
         */
        bool InSamePhase(WorldObject const* obj) const { return obj && InSamePhase(obj->GetPhaseMask()); }

        /**
         * @brief 静态方法：检查两个对象是否在同一相位
         */
        static bool InSamePhase(WorldObject const* a, WorldObject const* b) { return a && a->InSamePhase(b); }

        // ============ 区域和地带信息 ============

        /**
         * @brief 获取区域ID
         * @return 区域ID（如 "艾尔文森林"）
         */
        uint32 GetZoneId() const { return m_zoneId; }

        /**
         * @brief 获取地区ID
         * @return 地区ID（如 "闪金镇"）
         */
        uint32 GetAreaId() const { return m_areaId; }

        /**
         * @brief 同时获取区域和地区ID
         */
        void GetZoneAndAreaId(uint32& zoneid, uint32& areaid) const { zoneid = m_zoneId, areaid = m_areaId; }

        /**
         * @brief 检查是否在世界PvP区域
         * @return 如果在PvP区域返回 true
         */
        bool IsInWorldPvpZone() const;

        /**
         * @brief 检查是否在户外
         * @return 如果在户外返回 true
         */
        bool IsOutdoors() const { return m_outdoors; }

        /**
         * @brief 获取液体状态
         * @return 液体状态标志
         */
        ZLiquidStatus GetLiquidStatus() const { return m_liquidStatus; }

        /**
         * @brief 获取副本脚本
         * @return 副本脚本指针，如果不是副本返回 nullptr
         */
        InstanceScript* GetInstanceScript() const;

        // ============ 名称管理 ============

        /**
         * @brief 获取对象名称
         * @return 名称字符串
         */
        std::string const& GetName() const { return m_name; }

        /**
         * @brief 设置对象名称
         * @param newname 新名称
         */
        void SetName(std::string newname) { m_name = std::move(newname); }

        /**
         * @brief 获取指定语言环境下的名称
         * @param locale 语言常量
         * @return 名称字符串
         */
        virtual std::string const& GetNameForLocaleIdx(LocaleConstant /*locale*/) const { return m_name; }

        // ============ 距离计算方法 ============

        /**
         * @brief 计算到另一对象的距离（3D）
         * @param obj 目标对象
         * @return 距离
         */
        float GetDistance(WorldObject const* obj) const;

        /**
         * @brief 计算到位置的距离（3D）
         * @param pos 目标位置
         * @return 距离
         */
        float GetDistance(Position const& pos) const;

        /**
         * @brief 计算到坐标的距离（3D）
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @return 距离
         */
        float GetDistance(float x, float y, float z) const;

        /**
         * @brief 计算到另一对象的2D距离
         * @param obj 目标对象
         * @return 2D距离
         */
        float GetDistance2d(WorldObject const* obj) const;

        /**
         * @brief 计算到坐标的2D距离
         * @param x X坐标
         * @param y Y坐标
         * @return 2D距离
         */
        float GetDistance2d(float x, float y) const;

        /**
         * @brief 计算到另一对象的Z轴距离
         * @param obj 目标对象
         * @return Z轴距离
         */
        float GetDistanceZ(WorldObject const* obj) const;

        // ============ 距离判断方法 ============

        /**
         * @brief 检查是否是同一对象或在同一地图
         */
        bool IsSelfOrInSameMap(WorldObject const* obj) const;

        /**
         * @brief 检查是否在同一地图
         */
        bool IsInMap(WorldObject const* obj) const;

        /**
         * @brief 检查是否在指定距离内（3D）
         */
        bool IsWithinDist3d(float x, float y, float z, float dist) const;

        /**
         * @brief 检查位置是否在指定距离内（3D）
         */
        bool IsWithinDist3d(Position const* pos, float dist) const;

        /**
         * @brief 检查是否在指定距离内（2D）
         */
        bool IsWithinDist2d(float x, float y, float dist) const;

        /**
         * @brief 检查位置是否在指定距离内（2D）
         */
        bool IsWithinDist2d(Position const* pos, float dist) const;

        /**
         * @brief 检查对象是否在指定距离内
         * @param obj 目标对象
         * @param dist2compare 比较距离
         * @param is3D 是否使用3D距离
         * @return 如果在距离内返回 true
         *
         * 注意：仅用于确定在同一地图的对象
         */
        bool IsWithinDist(WorldObject const* obj, float dist2compare, bool is3D = true) const;

        /**
         * @brief 检查对象是否在地图中的指定距离内
         * @param obj 目标对象
         * @param dist2compare 比较距离
         * @param is3D 是否使用3D距离
         * @param incOwnRadius 是否包含自身半径
         * @param incTargetRadius 是否包含目标半径
         * @return 如果在距离内返回 true
         */
        bool IsWithinDistInMap(WorldObject const* obj, float dist2compare, bool is3D = true, bool incOwnRadius = true, bool incTargetRadius = true) const;

        /**
         * @brief 检查坐标是否在视线范围内
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param checks 视线检查类型
         * @param ignoreFlags 忽略的模型标志
         * @return 如果在视线内返回 true
         */
        bool IsWithinLOS(float x, float y, float z, LineOfSightChecks checks = LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags ignoreFlags = VMAP::ModelIgnoreFlags::Nothing) const;

        /**
         * @brief 检查对象是否在视线范围内
         * @param obj 目标对象
         * @param checks 视线检查类型
         * @param ignoreFlags 忽略的模型标志
         * @return 如果在视线内返回 true
         */
        bool IsWithinLOSInMap(WorldObject const* obj, LineOfSightChecks checks = LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags ignoreFlags = VMAP::ModelIgnoreFlags::Nothing) const;

        /**
         * @brief 获取命中球面点
         * @param dest 目标位置
         * @return 命中点位置
         */
        Position GetHitSpherePointFor(Position const& dest) const;

        /**
         * @brief 获取命中球面点（输出参数版本）
         */
        void GetHitSpherePointFor(Position const& dest, float& x, float& y, float& z) const;

        /**
         * @brief 比较两个对象到本对象的距离
         * @param obj1 第一个对象
         * @param obj2 第二个对象
         * @param is3D 是否使用3D距离
         * @return 如果 obj1 比 obj2 近返回 true
         */
        bool GetDistanceOrder(WorldObject const* obj1, WorldObject const* obj2, bool is3D = true) const;

        /**
         * @brief 检查对象是否在指定范围内
         * @param obj 目标对象
         * @param minRange 最小距离
         * @param maxRange 最大距离
         * @param is3D 是否使用3D距离
         * @return 如果在范围内返回 true
         */
        bool IsInRange(WorldObject const* obj, float minRange, float maxRange, bool is3D = true) const;

        /**
         * @brief 检查坐标是否在指定范围内（2D）
         */
        bool IsInRange2d(float x, float y, float minRange, float maxRange) const;

        /**
         * @brief 检查坐标是否在指定范围内（3D）
         */
        bool IsInRange3d(float x, float y, float z, float minRange, float maxRange) const;

        /**
         * @brief 检查目标是否在前方
         * @param target 目标对象
         * @param arc 前方扇形角度（弧度）
         * @return 如果在前方返回 true
         */
        bool isInFront(WorldObject const* target, float arc = float(M_PI)) const;

        /**
         * @brief 检查目标是否在后方
         * @param target 目标对象
         * @param arc 后方扇形角度（弧度）
         * @return 如果在后方返回 true
         */
        bool isInBack(WorldObject const* target, float arc = float(M_PI)) const;

        /**
         * @brief 检查是否在两点之间
         * @param pos1 第一点
         * @param pos2 第二点
         * @param size 额外大小
         * @return 如果在两点之间返回 true
         */
        bool IsInBetween(Position const& pos1, Position const& pos2, float size = 0) const;

        /**
         * @brief 检查是否在两个对象之间
         */
        bool IsInBetween(WorldObject const* obj1, WorldObject const* obj2, float size = 0) const { return obj1 && obj2 && IsInBetween(obj1->GetPosition(), obj2->GetPosition(), size); }

        // ============ 消息和通信 ============

        /**
         * @brief 删除前的清理操作
         * @param finalCleanup 是否是最终清理
         *
         * 在析构函数或批量删除生物前调用，移除对已删除单位的交叉引用。
         */
        virtual void CleanupsBeforeDelete(bool finalCleanup = true);

        /**
         * @brief 向附近玩家发送数据包
         * @param data 数据包
         * @param self 是否发送给自己
         */
        virtual void SendMessageToSet(WorldPacket const* data, bool self) const;

        /**
         * @brief 向指定范围内的玩家发送数据包
         * @param data 数据包
         * @param dist 距离
         * @param self 是否发送给自己
         */
        virtual void SendMessageToSetInRange(WorldPacket const* data, float dist, bool self) const;

        /**
         * @brief 向附近玩家发送数据包（跳过指定接收者）
         * @param data 数据包
         * @param skipped_rcvr 要跳过的接收者
         */
        virtual void SendMessageToSet(WorldPacket const* data, Player const* skipped_rcvr) const;

        // ============ 等级和属性 ============

        /**
         * @brief 获取对目标的有效等级
         * @param target 目标对象
         * @return 等级
         *
         * 用于处理等级缩放等内容。
         */
        virtual uint8 GetLevelForTarget(WorldObject const* /*target*/) const { return 1; }

        // ============ 音效和音乐 ============

        /**
         * @brief 播放距离音效
         * @param soundId 音效ID
         * @param target 目标玩家（nullptr 表示附近所有玩家）
         */
        void PlayDistanceSound(uint32 soundId, Player* target = nullptr);

        /**
         * @brief 播放直接音效
         * @param soundId 音效ID
         * @param target 目标玩家
         */
        void PlayDirectSound(uint32 soundId, Player* target = nullptr);

        /**
         * @brief 播放音乐
         * @param musicId 音乐ID
         * @param target 目标玩家
         */
        void PlayDirectMusic(uint32 musicId, Player* target = nullptr);

        /**
         * @brief 发送对象消失动画
         * @param guid 对象GUID
         */
        void SendObjectDeSpawnAnim(ObjectGuid guid);

        /**
         * @brief 将对象添加到移除列表
         *
         * 标记对象待删除，在下一个更新周期移除。
         */
        void AddObjectToRemoveList();

        // ============ 可见性和侦测系统 ============

        /**
         * @brief 获取网格激活范围
         * @return 激活范围
         */
        float GetGridActivationRange() const;

        /**
         * @brief 获取可见性范围
         * @return 可见性范围
         */
        float GetVisibilityRange() const;

        /**
         * @brief 获取视野范围
         * @param target 目标对象
         * @return 视野范围
         */
        float GetSightRange(WorldObject const* target = nullptr) const;

        /**
         * @brief 检查是否能看到或侦测到对象
         * @param obj 目标对象
         * @param implicitDetect 是否隐式侦测
         * @param distanceCheck 是否进行距离检查
         * @param checkAlert 是否检查警觉状态
         * @return 如果能看到或侦测到返回 true
         */
        bool CanSeeOrDetect(WorldObject const* obj, bool implicitDetect = false, bool distanceCheck = false, bool checkAlert = false) const;

        // ============ 可见性数组（潜行、隐身等） ============

        FlaggedValuesArray32<int32, uint32, StealthType, TOTAL_STEALTH_TYPES> m_stealth;           ///< 潜行值数组
        FlaggedValuesArray32<int32, uint32, StealthType, TOTAL_STEALTH_TYPES> m_stealthDetect;     ///< 潜行侦测值数组

        FlaggedValuesArray32<int32, uint32, InvisibilityType, TOTAL_INVISIBILITY_TYPES> m_invisibility;           ///< 隐身值数组
        FlaggedValuesArray32<int32, uint32, InvisibilityType, TOTAL_INVISIBILITY_TYPES> m_invisibilityDetect;     ///< 隐身侦测值数组

        FlaggedValuesArray32<int32, uint32, ServerSideVisibilityType, TOTAL_SERVERSIDE_VISIBILITY_TYPES> m_serverSideVisibility;           ///< 服务器端可见性数组
        FlaggedValuesArray32<int32, uint32, ServerSideVisibilityType, TOTAL_SERVERSIDE_VISIBILITY_TYPES> m_serverSideVisibilityDetect;     ///< 服务器端可见性侦测数组

        // ============ 地图管理 ============

        /**
         * @brief 设置对象所在的地图
         * @param map 地图指针
         *
         * 将对象注册到指定地图的网格系统中。
         */
        virtual void SetMap(Map* map);

        /**
         * @brief 重置地图引用
         *
         * 从当前地图注销对象。
         */
        virtual void ResetMap();

        /**
         * @brief 获取对象所在的地图
         * @return 地图指针
         *
         * 断言地图指针有效。对象应在世界中。
         */
        Map* GetMap() const { ASSERT(m_currMap); return m_currMap; }

        /**
         * @brief 查找对象所在的地图
         * @return 地图指针（可能为 nullptr）
         *
         * 用于检查对象不在世界中时的 GetMap() 调用。
         */
        Map* FindMap() const { return m_currMap; }

        /**
         * @brief 设置区域脚本
         *
         * 根据当前位置设置对应的区域脚本。
         */
        void SetZoneScript();

        /**
         * @brief 清除区域脚本
         */
        void ClearZoneScript();

        /**
         * @brief 获取区域脚本
         * @return 区域脚本指针
         */
        ZoneScript* GetZoneScript() const { return m_zoneScript; }

        // ============ 召唤系统 ============

        /**
         * @brief 召唤生物
         * @param entry 生物条目ID
         * @param pos 位置
         * @param despawnType 消失类型
         * @param despawnTime 消失时间
         * @param vehId 载具ID
         * @param spellId 召唤法术ID
         * @param visibleBySummonerOnly 是否仅对召唤者可见
         * @return 召唤的生物指针
         */
        TempSummon* SummonCreature(uint32 entry, Position const& pos, TempSummonType despawnType = TEMPSUMMON_MANUAL_DESPAWN, Milliseconds despawnTime = 0s, uint32 vehId = 0, uint32 spellId = 0, bool visibleBySummonerOnly = false);

        /**
         * @brief 召唤生物（坐标参数版本）
         */
        TempSummon* SummonCreature(uint32 entry, float x, float y, float z, float o = 0, TempSummonType despawnType = TEMPSUMMON_MANUAL_DESPAWN, Milliseconds despawnTime = 0s, bool visibleBySummonerOnly = false);

        /**
         * @brief 召唤游戏对象
         * @param entry 游戏对象条目ID
         * @param pos 位置
         * @param rot 旋转四元数
         * @param respawnTime 重生时间
         * @param summonType 召唤类型
         * @return 召唤的游戏对象指针
         */
        GameObject* SummonGameObject(uint32 entry, Position const& pos, QuaternionData const& rot, Seconds respawnTime, GOSummonType summonType = GO_SUMMON_TIMED_OR_CORPSE_DESPAWN);

        /**
         * @brief 召唤游戏对象（坐标参数版本）
         */
        GameObject* SummonGameObject(uint32 entry, float x, float y, float z, float ang, QuaternionData const& rot, Seconds respawnTime, GOSummonType summonType = GO_SUMMON_TIMED_OR_CORPSE_DESPAWN);

        /**
         * @brief 召唤触发器生物
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param ang 角度
         * @param despawnTime 消失时间
         * @param GetAI AI工厂函数
         * @return 召唤的生物指针
         *
         * 触发器是特殊的不可见生物，用于特效、陷阱等。
         */
        Creature*   SummonTrigger(float x, float y, float z, float ang, Milliseconds despawnTime, CreatureAI* (*GetAI)(Creature*) = nullptr);

        /**
         * @brief 召唤生物组
         * @param group 生物组ID
         * @param list 输出列表
         */
        void SummonCreatureGroup(uint8 group, std::list<TempSummon*>* list = nullptr);

        // ============ 搜索和查找 ============

        /**
         * @brief 查找最近的生物
         * @param entry 生物条目ID
         * @param range 搜索范围
         * @param alive 是否要求存活
         * @return 最近的生物指针
         */
        Creature*   FindNearestCreature(uint32 entry, float range, bool alive = true) const;

        /**
         * @brief 查找最近的游戏对象
         * @param entry 游戏对象条目ID
         * @param range 搜索范围
         * @param spawnedOnly 是否仅搜索已生成的
         * @return 最近的游戏对象指针
         */
        GameObject* FindNearestGameObject(uint32 entry, float range, bool spawnedOnly = true) const;

        /**
         * @brief 查找最近的未生成游戏对象
         */
        GameObject* FindNearestUnspawnedGameObject(uint32 entry, float range) const;

        /**
         * @brief 查找指定类型的最近游戏对象
         */
        GameObject* FindNearestGameObjectOfType(GameobjectTypes type, float range) const;

        /**
         * @brief 选择最近的玩家
         * @param distance 搜索距离
         * @return 最近的玩家指针
         */
        Player* SelectNearestPlayer(float distance) const;

        // ============ 所有者和控制者系统 ============

        /**
         * @brief 获取所有者GUID
         * @return 所有者GUID
         *
         * 纯虚函数，由派生类实现。
         */
        virtual ObjectGuid GetOwnerGUID() const = 0;

        /**
         * @brief 获取控制者或所有者GUID
         * @return 控制者GUID（如果有）或所有者GUID
         */
        virtual ObjectGuid GetCharmerOrOwnerGUID() const { return GetOwnerGUID(); }

        /**
         * @brief 获取控制者、所有者或自身GUID
         * @return 对应的GUID
         */
        ObjectGuid GetCharmerOrOwnerOrOwnGUID() const;

        /**
         * @brief 获取所有者
         * @return 所有者Unit指针
         */
        Unit* GetOwner() const;

        /**
         * @brief 获取控制者或所有者
         * @return 控制者或所有者Unit指针
         */
        Unit* GetCharmerOrOwner() const;

        /**
         * @brief 获取控制者、所有者或自身
         * @return 对应的Unit指针
         */
        Unit* GetCharmerOrOwnerOrSelf() const;

        /**
         * @brief 获取影响的玩家
         * @return 影响的玩家指针
         *
         * 用于法术修改、控制等情况。
         */
        Player* GetCharmerOrOwnerPlayerOrPlayerItself() const;

        /**
         * @brief 获取受影响的玩家
         * @return 受影响的玩家指针
         */
        Player* GetAffectingPlayer() const;

        /**
         * @brief 获取法术修改所有者
         * @return 拥有法术修改的玩家指针
         */
        Player* GetSpellModOwner() const;

        /**
         * @brief 计算法术伤害
         * @param spellEffectInfo 法术效果信息
         * @param basePoints 基础点数
         * @return 计算后的伤害值
         */
        int32 CalculateSpellDamage(SpellEffectInfo const& spellEffectInfo, int32 const* basePoints = nullptr) const;

        // ============ 法术范围检查 ============

        /**
         * @brief 获取对目标的最大法术距离
         * @param target 目标
         * @param spellInfo 法术信息
         * @return 最大距离
         */
        float GetSpellMaxRangeForTarget(Unit const* target, SpellInfo const* spellInfo) const;

        /**
         * @brief 获取对目标的最小法术距离
         * @param target 目标
         * @param spellInfo 法术信息
         * @return 最小距离
         */
        float GetSpellMinRangeForTarget(Unit const* target, SpellInfo const* spellInfo) const;

        /**
         * @brief 应用效果修改器
         * @param spellInfo 法术信息
         * @param effIndex 效果索引
         * @param value 基础值
         * @return 修改后的值
         */
        float ApplyEffectModifiers(SpellInfo const* spellInfo, uint8 effIndex, float value) const;

        /**
         * @brief 计算法术持续时间
         * @param spellInfo 法术信息
         * @return 持续时间（毫秒）
         */
        int32 CalcSpellDuration(SpellInfo const* spellInfo) const;

        /**
         * @brief 修改法术持续时间
         * @param spellInfo 法术信息
         * @param target 目标
         * @param duration 基础持续时间
         * @param positive 是否是正面效果
         * @param effectMask 效果掩码
         * @return 修改后的持续时间
         */
        int32 ModSpellDuration(SpellInfo const* spellInfo, WorldObject const* target, int32 duration, bool positive, uint32 effectMask) const;

        /**
         * @brief 修改法术施法时间
         * @param spellInfo 法术信息
         * @param castTime 输入输出施法时间
         * @param spell 法术实例
         */
        void ModSpellCastTime(SpellInfo const* spellInfo, int32& castTime, Spell* spell = nullptr) const;

        /**
         * @brief 修改法术持续时间
         * @param spellInfo 法术信息
         * @param durationTime 输入输出持续时间
         * @param spell 法术实例
         */
        void ModSpellDurationTime(SpellInfo const* spellInfo, int32& durationTime, Spell* spell = nullptr) const;

        // ============ 法术命中和抵抗 ============

        /**
         * @brief 计算近战法术未命中几率
         * @param victim 目标
         * @param attType 攻击类型
         * @param skillDiff 技能差值
         * @param spellId 法术ID
         * @return 未命中几率百分比
         */
        virtual float MeleeSpellMissChance(Unit const* victim, WeaponAttackType attType, int32 skillDiff, uint32 spellId) const;

        /**
         * @brief 计算近战法术命中结果
         * @param victim 目标
         * @param spellInfo 法术信息
         * @return 命中结果
         */
        virtual SpellMissInfo MeleeSpellHitResult(Unit* victim, SpellInfo const* spellInfo) const;

        /**
         * @brief 计算魔法法术命中结果
         * @param victim 目标
         * @param spellInfo 法术信息
         * @return 命中结果
         */
        SpellMissInfo MagicSpellHitResult(Unit* victim, SpellInfo const* spellInfo) const;

        /**
         * @brief 计算法术命中结果
         * @param victim 目标
         * @param spellInfo 法术信息
         * @param canReflect 是否可以反射
         * @return 命中结果
         */
        SpellMissInfo SpellHitResult(Unit* victim, SpellInfo const* spellInfo, bool canReflect = false) const;

        /**
         * @brief 发送法术未命中消息
         * @param target 目标
         * @param spellID 法术ID
         * @param missInfo 未命中信息
         */
        void SendSpellMiss(Unit* target, uint32 spellID, SpellMissInfo missInfo);

        // ============ 阵营和关系系统 ============

        /**
         * @brief 获取阵营ID
         * @return 阵营ID
         *
         * 纯虚函数，由派生类实现。
         */
        virtual uint32 GetFaction() const = 0;

        /**
         * @brief 设置阵营
         * @param faction 新阵营ID
         */
        virtual void SetFaction(uint32 /*faction*/) { }

        /**
         * @brief 获取阵营模板条目
         * @return 阵营模板条目指针
         */
        FactionTemplateEntry const* GetFactionTemplateEntry() const;

        /**
         * @brief 获取对目标的关系等级
         * @param target 目标对象
         * @return 关系等级
         */
        ReputationRank GetReactionTo(WorldObject const* target) const;

        /**
         * @brief 静态方法：计算阵营对目标的关系
         */
        static ReputationRank GetFactionReactionTo(FactionTemplateEntry const* factionTemplateEntry, WorldObject const* target);

        /**
         * @brief 检查是否敌对
         */
        bool IsHostileTo(WorldObject const* target) const;

        /**
         * @brief 检查是否敌对玩家
         */
        bool IsHostileToPlayers() const;

        /**
         * @brief 检查是否友好
         */
        bool IsFriendlyTo(WorldObject const* target) const;

        /**
         * @brief 检查是否对所有人都中立
         */
        bool IsNeutralToAll() const;

        // ============ 法术施放 ============

        /**
         * @brief 施放法术
         * @param targets 目标参数
         * @param spellId 法术ID
         * @param args 额外参数
         * @return 施放结果
         *
         * args 参数可以是多种类型，参见 CastSpellExtraArgs 构造函数。
         */
        SpellCastResult CastSpell(CastSpellTargetArg const& targets, uint32 spellId, CastSpellExtraArgs const& args = { });

        /**
         * @brief 检查是否是有效的攻击目标
         * @param target 目标
         * @param bySpell 法术信息
         * @return 如果是有效目标返回 true
         */
        bool IsValidAttackTarget(WorldObject const* target, SpellInfo const* bySpell = nullptr) const;

        /**
         * @brief 检查是否是有效的协助目标
         * @param target 目标
         * @param bySpell 法术信息
         * @return 如果是有效目标返回 true
         */
        bool IsValidAssistTarget(WorldObject const* target, SpellInfo const* bySpell = nullptr) const;

        /**
         * @brief 获取魔法命中重定向目标
         * @param victim 原始目标
         * @param spellInfo 法术信息
         * @return 重定向后的目标
         *
         * 用于处理如误导等效果。
         */
        Unit* GetMagicHitRedirectTarget(Unit* victim, SpellInfo const* spellInfo);

        // ============ 网格搜索模板方法 ============

        /**
         * @brief 在网格中搜索指定条目的游戏对象
         * @tparam Container 容器类型
         * @param gameObjectContainer 输出容器
         * @param entry 游戏对象条目ID
         * @param maxSearchRange 最大搜索范围
         */
        template <typename Container>
        void GetGameObjectListWithEntryInGrid(Container& gameObjectContainer, uint32 entry, float maxSearchRange = 250.0f) const;

        /**
         * @brief 在网格中搜索指定条目的生物
         * @tparam Container 容器类型
         * @param creatureContainer 输出容器
         * @param entry 生物条目ID
         * @param maxSearchRange 最大搜索范围
         */
        template <typename Container>
        void GetCreatureListWithEntryInGrid(Container& creatureContainer, uint32 entry, float maxSearchRange = 250.0f) const;

        /**
         * @brief 在网格中搜索玩家
         * @tparam Container 容器类型
         * @param playerContainer 输出容器
         * @param maxSearchRange 最大搜索范围
         * @param alive 是否仅搜索存活玩家
         */
        template <typename Container>
        void GetPlayerListInGrid(Container& playerContainer, float maxSearchRange, bool alive = true) const;

        // ============ 可见性更新 ============

        /**
         * @brief 为附近玩家销毁对象
         */
        void DestroyForNearbyPlayers();

        /**
         * @brief 更新对象可见性
         * @param forced 是否强制更新
         */
        virtual void UpdateObjectVisibility(bool forced = true);

        /**
         * @brief 创建时更新对象可见性
         */
        virtual void UpdateObjectVisibilityOnCreate() { UpdateObjectVisibility(true); }

        /**
         * @brief 更新位置数据
         */
        void UpdatePositionData();

        void BuildUpdate(UpdateDataMapType&) override;
        bool AddToObjectUpdate() override;
        void RemoveFromObjectUpdate() override;

        // ============ 重定位和可见性通知系统 ============

        /**
         * @brief 添加通知标志
         * @param f 标志
         */
        void AddToNotify(uint16 f) { m_notifyflags |= f;}

        /**
         * @brief 检查是否需要通知
         * @param f 标志
         * @return 如果需要通知返回 true
         */
        bool isNeedNotify(uint16 f) const { return (m_notifyflags & f) != 0; }

        /**
         * @brief 获取通知标志
         * @return 通知标志
         */
        uint16 GetNotifyFlags() const { return m_notifyflags; }

        /**
         * @brief 重置所有通知标志
         */
        void ResetAllNotifies() { m_notifyflags = 0; }

        // ============ 活动对象系统 ============

        /**
         * @brief 检查是否是活动对象
         * @return 如果是活动对象返回 true
         *
         * 活动对象即使没有玩家附近也会更新。
         */
        bool isActiveObject() const { return m_isActive; }

        /**
         * @brief 设置活动状态
         * @param isActiveObject 是否活动
         */
        void setActive(bool isActiveObject);

        /**
         * @brief 检查是否远距离可见
         * @return 如果是返回 true
         */
        bool IsFarVisible() const { return m_isFarVisible; }

        /**
         * @brief 设置远距离可见
         * @param on 是否启用
         */
        void SetFarVisible(bool on);

        /**
         * @brief 检查可见性是否被覆盖
         * @return 如果是返回 true
         */
        bool IsVisibilityOverridden() const { return m_visibilityDistanceOverride.has_value(); }

        /**
         * @brief 设置可见性距离覆盖
         * @param type 距离类型
         */
        void SetVisibilityDistanceOverride(VisibilityDistanceType type);

        /**
         * @brief 设置是否存储在世界对象网格容器中
         * @param apply 是否应用
         */
        void SetIsStoredInWorldObjectGridContainer(bool apply);

        /**
         * @brief 检查是否始终存储在世界对象网格容器中
         * @return 如果是返回 true
         */
        bool IsAlwaysStoredInWorldObjectGridContainer() const { return m_isStoredInWorldObjectGridContainer; }

        /**
         * @brief 检查是否存储在世界对象网格容器中
         * @return 如果是返回 true
         */
        bool IsStoredInWorldObjectGridContainer() const;

        uint32  LastUsedScriptID;  ///< 最后使用的脚本ID

        // ============ 运输工具系统 ============

        /**
         * @brief 获取所在的运输工具
         * @return 运输工具指针
         */
        Transport* GetTransport() const { return m_transport; }

        /**
         * @brief 获取运输工具上的X偏移
         */
        float GetTransOffsetX() const { return m_movementInfo.transport.pos.GetPositionX(); }

        /**
         * @brief 获取运输工具上的Y偏移
         */
        float GetTransOffsetY() const { return m_movementInfo.transport.pos.GetPositionY(); }

        /**
         * @brief 获取运输工具上的Z偏移
         */
        float GetTransOffsetZ() const { return m_movementInfo.transport.pos.GetPositionZ(); }

        /**
         * @brief 获取运输工具上的角度偏移
         */
        float GetTransOffsetO() const { return m_movementInfo.transport.pos.GetOrientation(); }

        /**
         * @brief 获取运输工具上的位置偏移
         * @return 位置常引用
         */
        Position const& GetTransOffset() const { return m_movementInfo.transport.pos; }

        /**
         * @brief 获取运输工具上的时间
         */
        uint32 GetTransTime()   const { return m_movementInfo.transport.time; }

        /**
         * @brief 获取运输工具上的座位ID
         */
        int8 GetTransSeat()     const { return m_movementInfo.transport.seat; }

        /**
         * @brief 获取运输工具的GUID
         * @return 运输工具GUID
         */
        virtual ObjectGuid GetTransGUID() const;

        /**
         * @brief 设置运输工具
         * @param t 运输工具指针
         */
        void SetTransport(Transport* t) { m_transport = t; }

        MovementInfo m_movementInfo;  ///< 移动信息结构体

        // ============ 固定位置对象 ============

        /**
         * @brief 获取固定X坐标
         * @return 固定X坐标（用于静态对象）
         */
        virtual float GetStationaryX() const { return GetPositionX(); }

        /**
         * @brief 获取固定Y坐标
         */
        virtual float GetStationaryY() const { return GetPositionY(); }

        /**
         * @brief 获取固定Z坐标
         */
        virtual float GetStationaryZ() const { return GetPositionZ(); }

        /**
         * @brief 获取固定角度
         */
        virtual float GetStationaryO() const { return GetOrientation(); }

        /**
         * @brief 获取地面高度
         * @return 地面Z坐标
         */
        float GetFloorZ() const;

        /**
         * @brief 获取碰撞高度
         * @return 碰撞高度
         */
        virtual float GetCollisionHeight() const { return 0.0f; }

        /**
         * @brief 获取地图水面或地面高度
         * @param x X坐标
         * @param y Y坐标
         * @param z 参考Z坐标
         * @param ground 输出地面高度
         * @return 水面或地面高度
         */
        float GetMapWaterOrGroundLevel(float x, float y, float z, float* ground = nullptr) const;

        /**
         * @brief 获取地图高度
         * @param x X坐标
         * @param y Y坐标
         * @param z 参考Z坐标
         * @param vmap 是否使用可视地图
         * @param distanceToSearch 搜索距离
         * @return 地面高度
         */
        float GetMapHeight(float x, float y, float z, bool vmap = true, float distanceToSearch = 50.0f) const;

        /**
         * @brief 获取调试信息
         * @return 调试信息字符串
         */
        std::string GetDebugInfo() const override;

        // ============ 事件处理器 ============

        EventProcessor m_Events;  ///< 事件处理器，用于延迟执行

    protected:
        std::string m_name;                                     ///< 对象名称
        bool m_isActive;                                        ///< 是否是活动对象
        bool m_isFarVisible;                                    ///< 是否远距离可见
        Optional<float> m_visibilityDistanceOverride;           ///< 可见性距离覆盖值
        bool const m_isStoredInWorldObjectGridContainer;        ///< 是否存储在世界对象网格容器中
        ZoneScript* m_zoneScript;                               ///< 区域脚本指针

        // 运输工具相关
        Transport* m_transport;  ///< 所在的运输工具

        /**
         * @brief 处理位置数据变化
         * @param data 位置地形状态数据
         */
        virtual void ProcessPositionDataChanged(PositionFullTerrainStatus const& data);

        uint32 m_zoneId;          ///< 区域ID
        uint32 m_areaId;          ///< 地区ID
        float m_staticFloorZ;     ///< 静态地面高度
        bool m_outdoors;          ///< 是否在户外
        ZLiquidStatus m_liquidStatus;  ///< 液体状态

        /**
         * @brief 设置位置地图ID
         * @param _mapId 地图ID
         *
         * 仅在 LoadFromDB()/Create() 中使用！
         */
        void SetLocationMapId(uint32 _mapId) { m_mapId = _mapId; }

        /**
         * @brief 设置位置实例ID
         * @param _instanceId 实例ID
         *
         * 仅在 LoadFromDB()/Create() 中使用！
         */
        void SetLocationInstanceId(uint32 _instanceId) { m_InstanceId = _instanceId; }

        // ============ 可见性虚函数（内部使用） ============

        /**
         * @brief 检查是否永远不可见
         * @param allowServersideObjects 是否允许服务器端对象
         * @return 如果永远不可见返回 true
         */
        virtual bool IsNeverVisible([[maybe_unused]] bool allowServersideObjects) const { return !IsInWorld(); }

        /**
         * @brief 检查对观察者是否始终可见
         * @param seer 观察者
         * @return 如果始终可见返回 true
         */
        virtual bool IsAlwaysVisibleFor(WorldObject const* /*seer*/) const { return false; }

        /**
         * @brief 检查是否因消失而不可见
         * @return 如果因消失而不可见返回 true
         */
        virtual bool IsInvisibleDueToDespawn() const { return false; }

        /**
         * @brief 检查对观察者是否始终可侦测
         * @param seer 观察者
         * @return 如果始终可侦测返回 true
         *
         * 与 IsAlwaysVisibleFor 的区别：
         * 1. 在距离检查之后调用
         * 2. 使用所有者或控制者作为观察者
         */
        virtual bool IsAlwaysDetectableFor(WorldObject const* /*seer*/) const { return false; }

    private:
        Map* m_currMap;          ///< 当前对象所在的地图

        uint32 m_InstanceId;     ///< 地图副本实例ID
        uint32 m_phaseMask;      ///< 区域相位状态

        uint16 m_notifyflags;    ///< 通知标志

        /**
         * @brief 内部距离检查
         * @param obj 目标对象
         * @param dist2compare 比较距离
         * @param is3D 是否3D距离
         * @param incOwnRadius 是否包含自身半径
         * @param incTargetRadius 是否包含目标半径
         * @return 如果在距离内返回 true
         */
        virtual bool _IsWithinDist(WorldObject const* obj, float dist2compare, bool is3D, bool incOwnRadius = true, bool incTargetRadius = true) const;

        /**
         * @brief 检查是否永远看不到对象
         * @param obj 目标对象
         * @return 如果永远看不到返回 true
         */
        bool CanNeverSee(WorldObject const* obj) const;

        /**
         * @brief 检查是否始终能看到对象
         * @param obj 目标对象
         * @return 如果始终能看到返回 true
         */
        virtual bool CanAlwaysSee(WorldObject const* /*obj*/) const { return false; }

        /**
         * @brief 检查是否能侦测到对象
         * @param obj 目标对象
         * @param ignoreStealth 是否忽略潜行
         * @param checkAlert 是否检查警觉
         * @return 如果能侦测到返回 true
         */
        bool CanDetect(WorldObject const* obj, bool ignoreStealth, bool checkAlert = false) const;

        /**
         * @brief 检查是否能侦测到对象的隐身
         * @param obj 目标对象
         * @return 如果能侦测到返回 true
         */
        bool CanDetectInvisibilityOf(WorldObject const* obj) const;

        /**
         * @brief 检查是否能侦测到对象的潜行
         * @param obj 目标对象
         * @param checkAlert 是否检查警觉
         * @return 如果能侦测到返回 true
         */
        bool CanDetectStealthOf(WorldObject const* obj, bool checkAlert = false) const;
};

namespace Trinity
{
    /**
     * @class ObjectDistanceOrderPred
     * @brief 基于距离的 WorldObject 排序谓词
     *
     * 二元谓词，用于根据到参考 WorldObject 的距离对 WorldObject 进行排序。
     * 常用于 std::sort 等算法。
     *
     * 使用示例：
     * @code
     * std::vector<WorldObject*> objects;
     * // ... 填充 objects
     * std::sort(objects.begin(), objects.end(), Trinity::ObjectDistanceOrderPred(player));
     * @endcode
     */
    class ObjectDistanceOrderPred
    {
        public:
            /**
             * @brief 构造函数
             * @param refObj 参考对象（用于计算距离）
             * @param ascending 是否升序排序（true: 近到远，false: 远到近）
             */
            ObjectDistanceOrderPred(WorldObject const* refObj, bool ascending = true) : _refObj(refObj), _ascending(ascending) { }

            /**
             * @brief 比较运算符
             * @param left 左侧对象
             * @param right 右侧对象
             * @return 根据排序方向返回比较结果
             */
            bool operator()(WorldObject const* left, WorldObject const* right) const
            {
                return _refObj->GetDistanceOrder(left, right) == _ascending;
            }

        private:
            WorldObject const* _refObj;  ///< 参考对象
            bool _ascending;             ///< 是否升序排序
    };
}

#endif
