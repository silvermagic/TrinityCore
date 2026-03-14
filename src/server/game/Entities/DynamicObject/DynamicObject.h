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
 * @file DynamicObject.h
 * @brief 动态对象实体模块头文件
 *
 * 本模块定义了 DynamicObject 类，用于表示游戏中的动态对象实体。
 * 动态对象是一种特殊的游戏对象，主要用于实现以下功能：
 * 1. 地面效果法术（如暴风雪、烈焰风暴等持续性区域伤害）
 * 2. 远视焦点（如猎人的鹰眼术、法师的魔眼术等）
 * 3. 传送门（目前未使用）
 *
 * 动态对象具有以下特性：
 * - 与施法者绑定，生命周期通常与法术持续时间相关
 * - 可以携带光环效果（Aura），在区域内对目标产生影响
 * - 支持视野系统，可作为玩家的观察点
 */

#ifndef TRINITYCORE_DYNAMICOBJECT_H
#define TRINITYCORE_DYNAMICOBJECT_H

#include "Object.h"
#include "GridObject.h"
#include "MapObject.h"

class Unit;
class Aura;
class SpellInfo;

/**
 * @enum DynamicObjectType
 * @brief 动态对象类型枚举
 *
 * 定义动态对象的具体用途类型，不同类型的动态对象在客户端有不同的表现行为。
 */
enum DynamicObjectType
{
    DYNAMIC_OBJECT_PORTAL           = 0x0,      ///< 传送门类型（目前未使用）
    DYNAMIC_OBJECT_AREA_SPELL       = 0x1,      ///< 区域法术类型，用于地面效果法术
    DYNAMIC_OBJECT_FARSIGHT_FOCUS   = 0x2       ///< 远视焦点类型，用于远视类法术
};

/**
 * @class DynamicObject
 * @brief 动态对象类，继承自 WorldObject、GridObject 和 MapObject
 *
 * DynamicObject 表示游戏中的动态对象，用于实现持续性区域效果法术和远视功能。
 * 动态对象会在指定位置创建一个圆形区域，在该区域内对进入的目标施加法术效果。
 *
 * 主要功能：
 * - 创建和管理区域法术效果（如暴风雪、奉献、烈焰风暴等）
 * - 支持远视系统，允许玩家通过该对象观察远处区域
 * - 与施法者绑定，使用施法者的阵营和属性
 * - 支持光环效果，可持续影响区域内的单位
 *
 * 生命周期：
 * - 由法术系统创建，通常在法术施放时生成
 * - 存在时间由法术持续时间或光环持续时间决定
 * - 当持续时间结束或被主动移除时销毁
 *
 * 注意：动态对象是网格系统的一部分，会参与网格更新和对象查询。
 */
class TC_GAME_API DynamicObject : public WorldObject, public GridObject<DynamicObject>, public MapObject
{
    public:
        /**
         * @brief 构造函数
         * @param isWorldObject 是否为世界对象（影响对象的可见性和更新策略）
         *
         * 初始化动态对象的基本属性，设置对象类型和更新标志。
         */
        DynamicObject(bool isWorldObject);

        /**
         * @brief 析构函数
         *
         * 确保对象被正确清理，断言所有引用已被移除。
         * 注意：析构时会删除未处理的光环对象。
         */
        ~DynamicObject();

        /**
         * @brief 将动态对象添加到游戏世界
         *
         * 调用时机：当动态对象创建完成并准备进入游戏世界时调用。
         *
         * 执行流程：
         * 1. 将对象注册到地图的对象存储器
         * 2. 调用父类的 AddToWorld
         * 3. 绑定到施法者
         */
        void AddToWorld() override;

        /**
         * @brief 将动态对象从游戏世界中移除
         *
         * 调用时机：当动态对象需要从世界中移除时调用（如法术结束、对象销毁）。
         *
         * 执行流程：
         * 1. 如果是视野焦点，移除视野绑定
         * 2. 如果有光环，移除光环
         * 3. 解除与施法者的绑定
         * 4. 从地图对象存储器中移除
         *
         * 注意：可能在光环移除过程中触发自身的移除，需要处理重入情况。
         */
        void RemoveFromWorld() override;

        /**
         * @brief 创建动态对象
         * @param guidlow 对象的低端 GUID
         * @param caster 施法者单位
         * @param spellId 法术 ID
         * @param pos 创建位置
         * @param radius 影响半径
         * @param type 动态对象类型
         * @return 创建成功返回 true，否则返回 false
         *
         * 调用时机：在法术施放过程中，需要创建区域效果时调用。
         *
         * 创建流程：
         * 1. 设置地图和位置
         * 2. 验证位置有效性
         * 3. 创建对象 GUID
         * 4. 设置法术相关属性（法术 ID、半径、施法者 GUID）
         * 5. 如果施法者在交通工具上，将对象附加到交通工具
         * 6. 添加到地图
         *
         * 注意：
         * - DYNAMICOBJECT_BYTES 的低字节通常设置为类型值
         * - 客户端会根据此值决定是否覆盖视觉效果半径
         */
        bool CreateDynamicObject(ObjectGuid::LowType guidlow, Unit* caster, uint32 spellId, Position const& pos, float radius, DynamicObjectType type);

        /**
         * @brief 更新动态对象
         * @param p_time 自上次更新以来经过的时间（毫秒）
         *
         * 调用时机：每次地图更新时由地图系统调用。
         *
         * 更新流程：
         * 1. 验证施法者存在且在同一地图
         * 2. 如果有光环，更新光环；否则更新持续时间
         * 3. 检查是否过期，过期则移除对象
         * 4. 调用脚本系统的更新回调
         *
         * 注意：光环可能在更新过程中被移除，需要处理这种情况。
         */
        void Update(uint32 p_time) override;

        /**
         * @brief 移除动态对象
         *
         * 调用时机：当动态对象需要被销毁时调用（如持续时间结束、法术被驱散）。
         *
         * 执行流程：
         * 1. 发送消失动画
         * 2. 从世界移除
         * 3. 添加到待删除列表
         */
        void Remove();

        /**
         * @brief 设置持续时间
         * @param newDuration 新的持续时间（毫秒）
         *
         * 如果对象有光环，则设置光环的持续时间；否则设置对象自身的持续时间。
         */
        void SetDuration(int32 newDuration);

        /**
         * @brief 获取持续时间
         * @return 剩余持续时间（毫秒）
         *
         * 如果对象有光环，返回光环的持续时间；否则返回对象自身的持续时间。
         */
        int32 GetDuration() const;

        /**
         * @brief 延迟动态对象的持续时间
         * @param delaytime 要减少的时间（毫秒）
         *
         * 将持续时间减少指定值，用于处理延迟效果。
         */
        void Delay(int32 delaytime);

        /**
         * @brief 设置光环
         * @param aura 要关联的光环对象
         *
         * 调用时机：当需要将光环效果绑定到动态对象时调用。
         *
         * 注意：一个动态对象只能有一个光环，调用时必须确保当前没有光环。
         */
        void SetAura(Aura* aura);

        /**
         * @brief 移除光环
         *
         * 调用时机：当需要解除光环与动态对象的绑定时调用。
         *
         * 执行流程：
         * 1. 将光环转移到 _removedAura
         * 2. 如果光环未被移除，调用光环的移除方法
         *
         * 注意：光环对象不会立即删除，而是延迟处理以避免悬空指针。
         */
        void RemoveAura();

        /**
         * @brief 设置施法者视野焦点
         *
         * 调用时机：当施法者是玩家且需要通过此动态对象观察时调用（如远视法术）。
         *
         * 将玩家的视角切换到此动态对象的位置。
         */
        void SetCasterViewpoint();

        /**
         * @brief 移除施法者视野焦点
         *
         * 调用时机：当远视效果结束时调用。
         *
         * 恢复玩家的正常视角。
         */
        void RemoveCasterViewpoint();

        /**
         * @brief 获取施法者
         * @return 施放此动态对象的单位指针
         */
        Unit* GetCaster() const { return _caster; }

        /**
         * @brief 获取阵营
         * @return 施法者的阵营 ID
         *
         * 动态对象使用施法者的阵营，用于阵营相关的判断（如敌我识别）。
         */
        uint32 GetFaction() const override;

        /**
         * @brief 绑定到施法者
         *
         * 调用时机：当动态对象添加到世界时调用。
         *
         * 建立施法者与动态对象的双向引用关系。
         */
        void BindToCaster();

        /**
         * @brief 从施法者解绑
         *
         * 调用时机：当动态对象从世界移除时调用。
         *
         * 断开施法者与动态对象的引用关系。
         */
        void UnbindFromCaster();

        /**
         * @brief 获取法术 ID
         * @return 关联的法术 ID
         */
        uint32 GetSpellId() const { return GetUInt32Value(DYNAMICOBJECT_SPELLID); }

        /**
         * @brief 获取法术信息
         * @return 法术信息结构体指针
         */
        SpellInfo const* GetSpellInfo() const;

        /**
         * @brief 获取施法者 GUID
         * @return 施法者的全局唯一标识符
         */
        ObjectGuid GetCasterGUID() const { return GetGuidValue(DYNAMICOBJECT_CASTER); }

        /**
         * @brief 获取所有者 GUID
         * @return 施法者的 GUID（所有者就是施法者）
         */
        ObjectGuid GetOwnerGUID() const override { return GetCasterGUID(); }

        /**
         * @brief 获取影响半径
         * @return 动态对象的影响半径
         */
        float GetRadius() const { return GetFloatValue(DYNAMICOBJECT_RADIUS); }

    protected:
        Aura* _aura;            ///< 关联的光环对象（如果此动态对象承载光环效果）
        Aura* _removedAura;     ///< 已移除但尚未删除的光环对象（用于延迟删除）
        Unit* _caster;          ///< 施放此动态对象的单位
        int32 _duration;        ///< 持续时间（毫秒），仅用于非光环类型的动态对象
        bool _isViewpoint;      ///< 是否作为视野焦点使用（用于远视类法术）
};
#endif
