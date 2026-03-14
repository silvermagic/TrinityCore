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
 * @file    HomeMovementGenerator.h
 * @brief   回家移动生成器模块
 *
 * 本模块实现了单位的回家移动行为，用于处理生物返回出生点的移动逻辑。
 * 主要应用于：
 * - 生物脱离战斗后返回出生位置（脱战归位）
 * - 生物重置后返回出生位置
 * - 生物死亡复活后返回出生位置
 *
 * 回家移动的特点：
 * - 生物以奔跑速度返回出生点
 * - 到达后会恢复初始朝向
 * - 到达后会恢复初始生命值和附加组件
 * - 优先级为普通，可被其他移动行为中断
 * - 主要用于 Creature，Player 的模板实现为空
 */

#ifndef TRINITY_HOMEMOVEMENTGENERATOR_H
#define TRINITY_HOMEMOVEMENTGENERATOR_H

#include "MovementGenerator.h"

/**
 * @class   HomeMovementGenerator
 * @brief   回家移动生成器模板类
 *
 * 继承自 MovementGeneratorMedium，为单位提供返回出生点的移动控制。
 * 当生物脱离战斗或需要重置时，会使用此生成器返回出生位置。
 *
 * 主要流程：
 * 1. 初始化时记录目标位置（出生点）
 * 2. 启动移动样条前往出生点
 * 3. 到达后执行重置操作（恢复生命、朝向、附加组件等）
 * 4. 通知AI已到达出生点
 *
 * @tparam T 目标单位类型（主要是 Creature，Player 的模板实现为空）
 */
template <class T>
class HomeMovementGenerator : public MovementGeneratorMedium< T, HomeMovementGenerator<T> >
{
    public:
        /**
         * @brief   构造函数
         *
         * 初始化回家移动生成器的基本属性：
         * - 设置移动模式为默认模式
         * - 设置优先级为普通（NORMAL）
         * - 设置基础单位状态为漫游状态
         */
        explicit HomeMovementGenerator();

        /**
         * @brief   获取移动生成器类型
         * @return  返回 HOME_MOTION_TYPE
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief   初始化回家移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器首次激活时调用，负责：
         * - 设置生成器标志
         * - 设置允许搜索援助标志
         * - 设置目标位置并开始移动
         */
        void DoInitialize(T*);

        /**
         * @brief   重置回家移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被重置时调用，重新初始化生成器状态。
         */
        void DoReset(T*);

        /**
         * @brief   更新回家移动逻辑
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   uint32 diff - 自上次更新以来的时间差（毫秒）
         * @return  bool - 如果返回 false，表示移动已完成或被中断
         *
         * 每个游戏循环都会调用此函数，负责：
         * - 检查移动是否完成或被中断
         * - 设置通知启用标志以便在结束时通知AI
         */
        bool DoUpdate(T*, uint32);

        /**
         * @brief   停用回家移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 当移动生成器被暂时停用时调用，清除单位的漫游移动状态。
         */
        void DoDeactivate(T*);

        /**
         * @brief   结束回家移动生成器
         * @param   T* owner - 拥有此移动生成器的单位
         * @param   bool active - 是否处于激活状态
         * @param   bool movementInform - 是否需要发送移动通知
         *
         * 当移动生成器被移除时调用，负责清理工作：
         * - 清除漫游移动状态和逃跑状态
         * - 如果到达出生点，恢复生物的初始状态
         * - 通知AI已到达出生点
         */
        void DoFinalize(T*, bool, bool);

    private:
        /**
         * @brief   设置目标位置并开始移动
         * @param   T* owner - 拥有此移动生成器的单位
         *
         * 获取出生位置并启动移动样条。
         */
        void SetTargetLocation(T*);
};

#endif
