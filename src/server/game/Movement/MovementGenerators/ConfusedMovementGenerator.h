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
 * @file    ConfusedMovementGenerator.h
 * @brief   混乱移动生成器模块
 *
 * 本模块实现了单位的混乱状态移动行为，用于处理单位在混乱状态下的随机移动逻辑。
 * 主要应用于：
 * - 玩家和生物被施加混乱效果时的移动控制
 * - 混乱期间单位会随机移动，无法控制自己的行动
 * - 支持路径碰撞检测和视线检测
 *
 * 混乱移动的特点：
 * - 单位会在当前位置附近随机移动
 * - 移动速度为行走速度（非奔跑）
 * - 无法穿过墙壁或障碍物
 * - 具有最高的移动优先级
 */

#ifndef TRINITY_CONFUSEDGENERATOR_H
#define TRINITY_CONFUSEDGENERATOR_H

#include "MovementGenerator.h"
#include "Timer.h"

class PathGenerator;

/**
 * @class   ConfusedMovementGenerator
 * @brief   混乱移动生成器模板类
 *
 * 继承自 MovementGeneratorMedium，为 Player 和 Creature 提供混乱状态下的移动控制。
 * 当单位处于混乱状态时，会随机地在当前位置附近移动，无法被玩家控制。
 *
 * @tparam T 目标单位类型（Player 或 Creature）
 */
template<class T>
class ConfusedMovementGenerator : public MovementGeneratorMedium<T, ConfusedMovementGenerator<T>>
{
    public:
        /**
         * @brief   构造函数
         *
         * 初始化混乱移动生成器的基本属性：
         * - 设置移动模式为默认模式
         * - 设置优先级为最高（HIGHEST）
         * - 设置基础单位状态为混乱状态
         * - 初始化定时器和位置坐标
         */
        explicit ConfusedMovementGenerator();

        /**
         * @brief   获取移动生成器类型
         * @return  返回 CONFUSED_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief   初始化混乱移动生成器
         * @param   T* owner - 拥有此移动生成器的单位（玩家或生物）
         *
         * 当移动生成器首次激活时调用，负责：
         * - 设置单位的混乱标志
         * - 停止当前移动
         * - 记录初始位置
         * - 重置定时器
         */
        void DoInitialize(T*);

        /**
         * @brief   重置混乱移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被重置时调用，重新初始化生成器状态。
         * 调用时机：当混乱效果重新应用时
         */
        void DoReset(T*);

        /**
         * @brief   更新混乱移动逻辑
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 自上次更新以来的时间差（毫秒）
         * @return  bool - 如果返回 false，移动生成器将被移除
         *
         * 每个游戏循环都会调用此函数，负责：
         * - 检查单位是否可以移动
         * - 定时生成新的随机目标点
         * - 计算并执行移动路径
         * - 处理速度变化事件
         *
         * 性能注意事项：
         * - 路径计算可能消耗较多CPU资源
         * - 视线检测会增加计算开销
         */
        bool DoUpdate(T*, uint32);

        /**
         * @brief   停用混乱移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被暂时停用时调用，清除单位的混乱移动状态。
         */
        void DoDeactivate(T*);

        /**
         * @brief   结束混乱移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 当移动生成器被移除时调用，负责清理工作：
         * - 移除混乱标志
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
        void UnitSpeedChanged() override { ConfusedMovementGenerator<T>::AddFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING); }

    private:
        std::unique_ptr<PathGenerator> _path;  ///< 路径生成器，用于计算避开障碍物的移动路径
        TimeTracker _timer;                    ///< 定时器，控制下一次移动的时间间隔
        float _x, _y, _z;                      ///< 参考位置坐标，混乱移动以此点为中心
};

#endif
