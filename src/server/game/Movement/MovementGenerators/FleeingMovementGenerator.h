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
 * @file    FleeingMovementGenerator.h
 * @brief   逃跑移动生成器模块
 *
 * 本模块实现了单位的逃跑状态移动行为，用于处理单位在恐惧或逃跑效果下的移动逻辑。
 * 主要应用于：
 * - 玩家和生物被施加恐惧效果时的移动控制
 * - 单位逃离特定目标（通常是施法者）
 * - 定时逃跑效果（有限时长的逃跑行为）
 *
 * 逃跑移动的特点：
 * - 单位会远离逃跑目标（通常是造成恐惧的单位）
 * - 移动距离基于与目标的距离计算
 * - 有"安静距离"范围（MIN_QUIET_DISTANCE 到 MAX_QUIET_DISTANCE）
 * - 移动速度为奔跑速度（非行走）
 * - 具有最高的移动优先级
 */

#ifndef TRINITY_FLEEINGMOVEMENTGENERATOR_H
#define TRINITY_FLEEINGMOVEMENTGENERATOR_H

#include "MovementGenerator.h"
#include "ObjectGuid.h"
#include "Timer.h"

class Creature;
class PathGenerator;
struct Position;

/**
 * @class   FleeingMovementGenerator
 * @brief   逃跑移动生成器模板类
 *
 * 继承自 MovementGeneratorMedium，为 Player 和 Creature 提供逃跑状态下的移动控制。
 * 当单位处于逃跑状态时，会远离指定的目标单位，无法被玩家控制。
 *
 * 逃跑距离计算策略：
 * - 如果距离目标太近（< MIN_QUIET_DISTANCE）：继续逃跑远离目标
 * - 如果距离目标太远（> MAX_QUIET_DISTANCE）：可能会回头或侧移
 * - 如果在安静距离范围内：随机方向移动
 *
 * @tparam T 目标单位类型（Player 或 Creature）
 */
template<class T>
class FleeingMovementGenerator : public MovementGeneratorMedium<T, FleeingMovementGenerator<T>>
{
    public:
        /**
         * @brief   构造函数
         * @param   fleeTargetGUID - 逃跑目标的GUID（通常是造成恐惧效果的单位）
         *
         * 初始化逃跑移动生成器的基本属性：
         * - 设置移动模式为默认模式
         * - 设置优先级为最高（HIGHEST）
         * - 设置基础单位状态为逃跑状态
         * - 保存逃跑目标的GUID
         */
        explicit FleeingMovementGenerator(ObjectGuid fleeTargetGUID);

        /**
         * @brief   获取移动生成器类型
         * @return  返回 FLEEING_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief   初始化逃跑移动生成器
         * @param   T* owner - 拥有此移动生成器的单位（玩家或生物）
         *
         * 当移动生成器首次激活时调用，负责：
         * - 设置单位的逃跑标志
         * - 初始化路径生成器
         * - 开始第一次逃跑移动
         */
        void DoInitialize(T*);

        /**
         * @brief   重置逃跑移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被重置时调用，重新初始化生成器状态。
         * 调用时机：当逃跑效果重新应用时
         */
        void DoReset(T*);

        /**
         * @brief   更新逃跑移动逻辑
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 自上次更新以来的时间差（毫秒）
         * @return  bool - 如果返回 false，移动生成器将被移除
         *
         * 每个游戏循环都会调用此函数，负责：
         * - 检查单位是否可以移动
         * - 定时生成新的逃跑目标点
         * - 计算并执行移动路径
         * - 处理速度变化事件
         *
         * 性能注意事项：
         * - 路径计算可能消耗较多CPU资源
         * - 视线检测会增加计算开销
         */
        bool DoUpdate(T*, uint32);

        /**
         * @brief   停用逃跑移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被暂时停用时调用，清除单位的逃跑移动状态。
         */
        void DoDeactivate(T*);

        /**
         * @brief   结束逃跑移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 当移动生成器被移除时调用，负责清理工作：
         * - 移除逃跑标志
         * - 停止移动
         * - 如果是生物且有攻击目标，重新设置目标
         */
        void DoFinalize(T*, bool, bool);

        /**
         * @brief   单位速度变化回调
         *
         * 当单位的移动速度发生变化时调用，设置速度更新待处理标志。
         * 调用时机：单位速度属性改变后
         */
        void UnitSpeedChanged() override { FleeingMovementGenerator<T>::AddFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING); }

    private:
        /**
         * @brief   设置目标位置并开始移动
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 计算逃跑目标点并启动移动：
         * 1. 调用 GetPoint 计算逃跑方向和距离
         * 2. 检查目标点是否在视线内
         * 3. 计算移动路径
         * 4. 启动移动样条
         */
        void SetTargetLocation(T*);

        /**
         * @brief   计算逃跑目标点
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   Position& position - 输出参数，计算得到的目标位置
         *
         * 根据与逃跑目标的距离计算逃跑方向：
         * - 距离 < MIN_QUIET_DISTANCE：继续远离目标
         * - 距离 > MAX_QUIET_DISTANCE：可能回头
         * - 在安静距离内：随机方向移动
         */
        void GetPoint(T*, Position& position);

        std::unique_ptr<PathGenerator> _path;  ///< 路径生成器，用于计算避开障碍物的移动路径
        ObjectGuid _fleeTargetGUID;            ///< 逃跑目标的GUID，单位会远离此目标
        TimeTracker _timer;                    ///< 定时器，控制下一次移动的时间间隔
};

/**
 * @class   TimedFleeingMovementGenerator
 * @brief   定时逃跑移动生成器
 *
 * 继承自 FleeingMovementGenerator<Creature>，为生物提供有限时长的逃跑行为。
 * 主要用于：
 * - 恐惧效果有固定持续时间的情况
 * - 逃跑结束后需要恢复战斗行为的情况
 *
 * 与基础逃跑生成器的区别：
 * - 有固定的持续时间限制
 * - 逃跑结束后会自动通知AI并恢复攻击行为
 */
class TimedFleeingMovementGenerator : public FleeingMovementGenerator<Creature>
{
    public:
        /**
         * @brief   构造函数
         * @param   fleeTargetGUID - 逃跑目标的GUID
         * @param   time - 逃跑持续时间（毫秒）
         */
        explicit TimedFleeingMovementGenerator(ObjectGuid fleeTargetGUID, uint32 time) : FleeingMovementGenerator<Creature>(fleeTargetGUID), _totalFleeTime(time) { }

        /**
         * @brief   更新逃跑逻辑
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 时间增量（毫秒）
         * @return  bool - 如果时间到期返回 false，移除此生成器
         *
         * 更新总逃跑时间，如果时间到期则返回 false 触发结束流程。
         */
        bool Update(Unit*, uint32) override;

        /**
         * @brief   结束逃跑移动
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 逃跑结束时：
         * - 移除逃跑标志
         * - 如果有受害者，恢复攻击行为
         * - 通知AI逃跑结束
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief   获取移动生成器类型
         * @return  返回 TIMED_FLEEING_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

    private:
        TimeTracker _totalFleeTime;  ///< 总逃跑时间计时器
};

#endif
