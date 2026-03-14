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
 * @file MovementGenerator.h
 * @brief 移动生成器基类定义模块
 *
 * 本模块定义了移动生成器的基类架构,是所有具体移动生成器(如追击、跟随、逃跑等)的基础框架。
 * 移动生成器负责控制单位的移动行为,包括初始化、更新、停止和重置等生命周期管理。
 *
 * 主要职责:
 * - 定义移动生成器的通用接口和生命周期方法
 * - 管理移动生成器的标志位和状态
 * - 提供工厂模式支持动态创建移动生成器
 * - 实现移动生成器的注册机制
 *
 * 设计模式:
 * - 工厂模式: 通过 MovementGeneratorFactory 和 MovementGeneratorCreator 实现对象的动态创建
 * - 模板方法模式: MovementGeneratorMedium 使用 CRTP 实现类型安全的虚函数调用
 *
 * 相关模块:
 * - MotionMaster: 管理和调度多个移动生成器
 * - 各种具体移动生成器: ChaseMovementGenerator, FollowMovementGenerator 等
 */

#ifndef TRINITY_MOVEMENTGENERATOR_H
#define TRINITY_MOVEMENTGENERATOR_H

#include "Define.h"
#include "FactoryHolder.h"
#include "ObjectRegistry.h"

class Creature;
class Unit;

enum MovementGeneratorType : uint8;

/**
 * @brief 移动生成器标志位枚举
 *
 * 用于标识移动生成器的各种状态,控制移动行为的执行流程
 */
enum MovementGeneratorFlags : uint16
{
    MOVEMENTGENERATOR_FLAG_NONE                   = 0x000,  // 无标志,初始状态
    MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING = 0x001,  // 初始化待处理:生成器已创建但尚未初始化
    MOVEMENTGENERATOR_FLAG_INITIALIZED            = 0x002,  // 已初始化:生成器已完成初始化
    MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING   = 0x004,  // 速度更新待处理:需要更新移动速度
    MOVEMENTGENERATOR_FLAG_INTERRUPTED            = 0x008,  // 已中断:移动被外部事件打断
    MOVEMENTGENERATOR_FLAG_PAUSED                 = 0x010,  // 已暂停:移动暂时停止
    MOVEMENTGENERATOR_FLAG_TIMED_PAUSED           = 0x020,  // 定时暂停:在指定时间后自动恢复
    MOVEMENTGENERATOR_FLAG_DEACTIVATED            = 0x040,  // 已停用:生成器被停用但未删除
    MOVEMENTGENERATOR_FLAG_INFORM_ENABLED         = 0x080,  // 启用通知:允许发送移动完成通知
    MOVEMENTGENERATOR_FLAG_FINALIZED              = 0x100,  // 已完成:生成器已完成生命周期
    MOVEMENTGENERATOR_FLAG_PERSIST_ON_DEATH       = 0x200,  // 死亡保持:单位死亡后保持该移动生成器

    // 瞬态标志:这些标志会在移动结束时自动清除
    MOVEMENTGENERATOR_FLAG_TRANSITORY = MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING | MOVEMENTGENERATOR_FLAG_INTERRUPTED
};

/**
 * @class MovementGenerator
 * @brief 移动生成器基类
 *
 * 所有移动生成器的抽象基类,定义了移动行为的生命周期接口。
 * 负责控制单位的移动逻辑,如追击、跟随、巡逻、逃跑等。
 *
 * 生命周期:
 * 1. 创建 -> 初始化(Initialize)
 * 2. 更新循环(Update)
 * 3. 停用(Deactivate) 或 重置(Reset)
 * 4. 完成(Finalize)
 *
 * 线程安全:
 * - 非线程安全,只能在主线程操作
 * - 通过 MotionMaster 管理生命周期
 */
class TC_GAME_API MovementGenerator
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化移动生成器的基础属性
         */
        MovementGenerator() : Mode(0), Priority(0), Flags(MOVEMENTGENERATOR_FLAG_NONE), BaseUnitState(0) { }

        /**
         * @brief 虚析构函数
         */
        virtual ~MovementGenerator();

        /**
         * @brief 初始化移动生成器
         * @param owner 拥有该生成器的单位
         *
         * 当移动生成器首次成为活动状态时调用,用于设置初始状态。
         * 调用时机:首次添加到 MotionMaster 顶部时
         */
        virtual void Initialize(Unit*) = 0;

        /**
         * @brief 重置移动生成器
         * @param owner 拥有该生成器的单位
         *
         * 当移动生成器重新分配到顶部时调用,用于恢复初始状态。
         * 调用时机:移动生成器已经在队列中,被重新激活时
         */
        virtual void Reset(Unit*) = 0;

        /**
         * @brief 更新移动逻辑
         * @param owner 拥有该生成器的单位
         * @param diff 距离上次更新的时间间隔(毫秒)
         * @return true 表示移动继续,false 表示移动完成
         *
         * 每个游戏循环调用一次,用于更新移动状态。
         * 调用时机:MotionMaster::Update 中
         * 性能注意:频繁调用,需要优化性能
         */
        virtual bool Update(Unit*, uint32 diff) = 0;

        /**
         * @brief 停用移动生成器
         * @param owner 拥有该生成器的单位
         *
         * 当移动生成器被其他生成器替换时调用,用于暂停当前移动。
         * 调用时机:当前顶部生成器被替换时
         */
        virtual void Deactivate(Unit*) = 0;

        /**
         * @brief 完成移动生成器
         * @param owner 拥有该生成器的单位
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         *
         * 当移动生成器被删除时调用,用于清理资源。
         * 调用时机:从 MotionMaster 中移除时
         */
        virtual void Finalize(Unit*, bool, bool) = 0;

        /**
         * @brief 获取移动生成器类型
         * @return 移动生成器类型枚举值
         */
        virtual MovementGeneratorType GetMovementGeneratorType() const = 0;

        /**
         * @brief 单位速度改变通知
         *
         * 当单位的移动速度改变时调用,用于更新移动参数。
         * 调用时机:单位速度属性变化时
         */
        virtual void UnitSpeedChanged() { }

        /**
         * @brief 暂停移动
         * @param timer 暂停时间(毫秒),0表示无限期暂停
         *
         * 暂停当前的移动行为。
         */
        virtual void Pause(uint32/* timer = 0*/) { }

        /**
         * @brief 恢复移动
         * @param overrideTimer 覆盖暂停时间(毫秒),0表示立即恢复
         *
         * 恢复被暂停的移动行为。
         */
        virtual void Resume(uint32/* overrideTimer = 0*/) { }

        /**
         * @brief 获取重置位置
         * @param owner 拥有该生成器的单位
         * @param x [out] X坐标
         * @param y [out] Y坐标
         * @param z [out] Z坐标
         * @return true 表示成功获取重置位置
         *
         * 用于逃避机制,选择一个合适的重置点来重启默认移动。
         */
        virtual bool GetResetPosition(Unit*, float&/* x*/, float&/* y*/, float&/* z*/) { return false; }

        /**
         * @brief 添加标志位
         * @param flag 要添加的标志位
         */
        void AddFlag(uint16 const flag) { Flags |= flag; }

        /**
         * @brief 检查是否具有指定标志位
         * @param flag 要检查的标志位
         * @return true 表示具有该标志位
         */
        bool HasFlag(uint16 const flag) const { return (Flags & flag) != 0; }

        /**
         * @brief 移除标志位
         * @param flag 要移除的标志位
         */
        void RemoveFlag(uint16 const flag) { Flags &= ~flag; }

        /**
         * @brief 获取调试信息
         * @return 调试信息字符串
         */
        virtual std::string GetDebugInfo() const;

        uint8 Mode;            ///< 移动生成器模式(参见 MovementGeneratorMode 枚举)
        uint8 Priority;        ///< 移动生成器优先级(参见 MovementGeneratorPriority 枚举)
        uint16 Flags;          ///< 移动生成器标志位(参见 MovementGeneratorFlags 枚举)
        uint32 BaseUnitState;  ///< 基础单位状态掩码,用于阻止某些行为
};

/**
 * @class MovementGeneratorMedium
 * @brief 移动生成器中间模板类
 *
 * 使用 CRTP (Curiously Recurring Template Pattern) 模式实现类型安全的虚函数调用。
 * 为具体的移动生成器提供类型安全的接口包装。
 *
 * @tparam T 单位类型(Creature 或 Player)
 * @tparam D 派生类类型(具体的移动生成器)
 *
 * 使用示例:
 * class ChaseMovementGenerator : public MovementGeneratorMedium<Unit, ChaseMovementGenerator>
 */
template<class T, class D>
class MovementGeneratorMedium : public MovementGenerator
{
    public:
        /**
         * @brief 初始化移动生成器(类型安全版本)
         * @param owner 拥有该生成器的单位
         *
         * 将调用转发到派生类的 DoInitialize 方法
         */
        void Initialize(Unit* owner) override
        {
            (static_cast<D*>(this))->DoInitialize(static_cast<T*>(owner));
        }

        /**
         * @brief 重置移动生成器(类型安全版本)
         * @param owner 拥有该生成器的单位
         *
         * 将调用转发到派生类的 DoReset 方法
         */
        void Reset(Unit* owner) override
        {
            (static_cast<D*>(this))->DoReset(static_cast<T*>(owner));
        }

        /**
         * @brief 更新移动逻辑(类型安全版本)
         * @param owner 拥有该生成器的单位
         * @param diff 距离上次更新的时间间隔(毫秒)
         * @return true 表示移动继续,false 表示移动完成
         *
         * 将调用转发到派生类的 DoUpdate 方法
         */
        bool Update(Unit* owner, uint32 diff) override
        {
            return (static_cast<D*>(this))->DoUpdate(static_cast<T*>(owner), diff);
        }

        /**
         * @brief 停用移动生成器(类型安全版本)
         * @param owner 拥有该生成器的单位
         *
         * 将调用转发到派生类的 DoDeactivate 方法
         */
        void Deactivate(Unit* owner) override
        {
            (static_cast<D*>(this))->DoDeactivate(static_cast<T*>(owner));
        }

        /**
         * @brief 完成移动生成器(类型安全版本)
         * @param owner 拥有该生成器的单位
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         *
         * 将调用转发到派生类的 DoFinalize 方法
         */
        void Finalize(Unit* owner, bool active, bool movementInform) override
        {
            (static_cast<D*>(this))->DoFinalize(static_cast<T*>(owner), active, movementInform);
        }
};

/// 移动生成器创建器类型定义(工厂模式基类)
typedef FactoryHolder<MovementGenerator, Unit, MovementGeneratorType> MovementGeneratorCreator;

/**
 * @struct MovementGeneratorFactory
 * @brief 移动生成器工厂模板
 *
 * 用于创建特定类型的移动生成器实例。
 *
 * @tparam Movement 要创建的移动生成器类型
 */
template<class Movement>
struct MovementGeneratorFactory : public MovementGeneratorCreator
{
    /**
     * @brief 构造函数
     * @param movementGeneratorType 移动生成器类型
     */
    MovementGeneratorFactory(MovementGeneratorType movementGeneratorType) : MovementGeneratorCreator(movementGeneratorType) { }

    /**
     * @brief 创建移动生成器实例
     * @param object 关联的单位
     * @return 新创建的移动生成器实例
     */
    MovementGenerator* Create(Unit* /*object*/) const override
    {
        return new Movement();
    }
};

/**
 * @struct IdleMovementFactory
 * @brief 空闲移动生成器工厂
 *
 * 创建空闲状态的移动生成器,返回单例实例以节省内存。
 */
struct IdleMovementFactory : public MovementGeneratorCreator
{
    IdleMovementFactory();

    MovementGenerator* Create(Unit* object) const override;
};

/**
 * @struct RandomMovementFactory
 * @brief 随机移动生成器工厂
 *
 * 创建随机移动行为的生成器,用于生物的随机巡逻。
 */
struct RandomMovementFactory : public MovementGeneratorCreator
{
    RandomMovementFactory();

    MovementGenerator* Create(Unit* object) const override;
};

/**
 * @struct WaypointMovementFactory
 * @brief 路径点移动生成器工厂
 *
 * 创建沿预设路径点移动的生成器,用于 NPC 的固定路线巡逻。
 */
struct WaypointMovementFactory : public MovementGeneratorCreator
{
    WaypointMovementFactory();

    MovementGenerator* Create(Unit* object) const override;
};

/// 移动生成器注册表类型定义
typedef MovementGeneratorCreator::FactoryHolderRegistry MovementGeneratorRegistry;

#define sMovementGeneratorRegistry MovementGeneratorRegistry::instance()

#endif
