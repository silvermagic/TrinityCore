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
 * @file    IdleMovementGenerator.h
 * @brief   空闲移动生成器模块
 *
 * 本模块实现了多种空闲状态相关的移动生成器，包括：
 * 1. IdleMovementGenerator - 空闲移动生成器
 *    - 用于表示单位没有活动的移动行为
 *    - 作为移动生成器栈的默认底层生成器
 *    - 主要作用是停止单位的移动
 *
 * 2. RotateMovementGenerator - 旋转移动生成器
 *    - 用于控制单位进行原地旋转
 *    - 支持向左或向右旋转
 *    - 常用于脚本控制单位转向
 *
 * 3. DistractMovementGenerator - 分心移动生成器
 *    - 用于控制单位转向特定方向
 *    - 持续一段时间后自动结束
 *    - 常用于单位被吸引注意力的情况
 *
 * 4. AssistanceDistractMovementGenerator - 援助分心移动生成器
 *    - 继承自 DistractMovementGenerator
 *    - 结束后会将生物的反应状态设置为攻击性
 *    - 用于寻求援助的生物
 */

#ifndef TRINITY_IDLEMOVEMENTGENERATOR_H
#define TRINITY_IDLEMOVEMENTGENERATOR_H

#include "MovementGenerator.h"
#include "Timer.h"

enum RotateDirection : uint8;

/**
 * @class   IdleMovementGenerator
 * @brief   空闲移动生成器
 *
 * 继承自 MovementGenerator，表示单位处于空闲状态。
 * 主要功能：
 * - 停止单位的移动
 * - 作为移动生成器栈的默认底层生成器
 * - 不进行任何实际的移动行为
 *
 * 使用场景：
 * - 当所有其他移动生成器都被移除时，单位回到空闲状态
 * - 作为 MotionMaster 的默认生成器
 */
class IdleMovementGenerator : public MovementGenerator
{
    public:
        /**
         * @brief   构造函数
         *
         * 初始化空闲移动生成器的基本属性：
         * - Mode: 默认移动模式
         * - Priority: 普通优先级
         * - Flags: 已初始化状态
         * - BaseUnitState: 0（无特殊状态）
         */
        explicit IdleMovementGenerator();

        /**
         * @brief   初始化空闲移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 停止单位的移动。
         */
        void Initialize(Unit*) override;

        /**
         * @brief   重置空闲移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 停止单位的移动。
         */
        void Reset(Unit*) override;

        /**
         * @brief   更新空闲移动逻辑
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 时间增量（毫秒）
         * @return  始终返回 true，表示继续运行
         *
         * 空闲状态下不需要做任何更新。
         */
        bool Update(Unit*, uint32) override { return true; }

        /**
         * @brief   停用空闲移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 空闲状态下停用不需要特殊处理。
         */
        void Deactivate(Unit*) override;

        /**
         * @brief   结束空闲移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 设置已结束标志。
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief   获取移动生成器类型
         * @return  返回 IDLE_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;
};

/**
 * @class   RotateMovementGenerator
 * @brief   旋转移动生成器
 *
 * 继承自 MovementGenerator，控制单位进行原地旋转。
 * 主要功能：
 * - 控制单位向左或向右旋转指定时间
 * - 使用移动样条系统实现平滑旋转
 * - 支持在载具上的旋转
 *
 * 使用场景：
 * - 脚本控制单位转向特定方向
 * - 特殊技能效果（如旋风斩）
 */
class RotateMovementGenerator : public MovementGenerator
{
    public:
        /**
         * @brief   构造函数
         * @param   id - 移动ID，用于标识不同的旋转移动
         * @param   time - 旋转持续时间（毫秒）
         * @param   direction - 旋转方向（左或右）
         *
         * 初始化旋转移动生成器的基本属性：
         * - Mode: 默认移动模式
         * - Priority: 普通优先级
         * - Flags: 初始化待处理状态
         * - BaseUnitState: 旋转状态
         */
        explicit RotateMovementGenerator(uint32 id, uint32 time, RotateDirection direction);

        /**
         * @brief   初始化旋转移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 停止单位的移动，准备开始旋转。
         */
        void Initialize(Unit*) override;

        /**
         * @brief   重置旋转移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 重新初始化旋转移动。
         */
        void Reset(Unit*) override;

        /**
         * @brief   更新旋转移动逻辑
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 时间增量（毫秒）
         * @return  是否继续运行（时间到期返回 false）
         *
         * 更新流程：
         * 1. 计算新的朝向角度
         * 2. 使用移动样条设置朝向
         * 3. 更新剩余时间
         * 4. 如果时间到期，返回 false 结束旋转
         */
        bool Update(Unit*, uint32) override;

        /**
         * @brief   停用旋转移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 设置已停用标志。
         */
        void Deactivate(Unit*) override;

        /**
         * @brief   结束旋转移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 设置已结束标志，如果是生物则通知AI旋转完成。
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief   获取移动生成器类型
         * @return  返回 ROTATE_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

    private:
        uint32 _id;                ///< 移动ID，用于标识不同的旋转移动
        uint32 _duration;          ///< 剩余旋转时间（毫秒）
        uint32 _maxDuration;       ///< 最大旋转时间（毫秒）
        RotateDirection _direction; ///< 旋转方向（左或右）
};

/**
 * @class   DistractMovementGenerator
 * @brief   分心移动生成器
 *
 * 继承自 MovementGenerator，控制单位转向特定方向并保持一段时间。
 * 主要功能：
 * - 让单位转向指定的朝向
 * - 持续一段时间后自动结束
 * - 如果单位不是站立状态，会让单位站起来
 *
 * 使用场景：
 * - 单位被声音或其他因素吸引注意力
 * - 听到可疑声音后转向查看
 */
class DistractMovementGenerator : public MovementGenerator
{
    public:
        /**
         * @brief   构造函数
         * @param   timer - 分心持续时间（毫秒）
         * @param   orientation - 目标朝向（弧度）
         *
         * 初始化分心移动生成器的基本属性：
         * - Mode: 默认移动模式
         * - Priority: 最高优先级（会覆盖其他移动）
         * - Flags: 初始化待处理状态
         * - BaseUnitState: 分心状态
         */
        explicit DistractMovementGenerator(uint32 timer, float orientation);

        /**
         * @brief   初始化分心移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 初始化流程：
         * 1. 如果单位不是站立状态，设置为站立状态
         * 2. 使用移动样条设置朝向
         */
        void Initialize(Unit*) override;

        /**
         * @brief   重置分心移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 重新初始化分心移动。
         */
        void Reset(Unit*) override;

        /**
         * @brief   更新分心移动逻辑
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 时间增量（毫秒）
         * @return  是否继续运行（时间到期返回 false）
         *
         * 更新剩余时间，如果时间到期则返回 false 结束分心。
         */
        bool Update(Unit*, uint32) override;

        /**
         * @brief   停用分心移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         *
         * 设置已停用标志。
         */
        void Deactivate(Unit*) override;

        /**
         * @brief   结束分心移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 设置已结束标志，如果是生物且通知已启用，则转向出生朝向。
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief   获取移动生成器类型
         * @return  返回 DISTRACT_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

    private:
        uint32 _timer;        ///< 分心剩余时间（毫秒）
        float _orientation;   ///< 目标朝向（弧度）
};

/**
 * @class   AssistanceDistractMovementGenerator
 * @brief   援助分心移动生成器
 *
 * 继承自 DistractMovementGenerator，用于寻求援助的生物。
 * 主要功能：
 * - 与 DistractMovementGenerator 相同的转向行为
 * - 结束后将生物的反应状态设置为攻击性
 *
 * 使用场景：
 * - 生物被攻击后寻求援助
 * - 援助生物到达后进入攻击性状态
 */
class AssistanceDistractMovementGenerator : public DistractMovementGenerator
{
    public:
        /**
         * @brief   构造函数
         * @param   timer - 分心持续时间（毫秒）
         * @param   orientation - 目标朝向（弧度）
         *
         * 调用父类构造函数，但将优先级设置为普通。
         */
        explicit AssistanceDistractMovementGenerator(uint32 timer, float orientation);

        /**
         * @brief   结束援助分心移动生成器
         * @param   Unit* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 设置已结束标志，如果是生物且通知已启用，则设置为攻击性状态。
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief   获取移动生成器类型
         * @return  返回 ASSISTANCE_DISTRACT_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;
};

#endif
