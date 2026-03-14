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
 * @file RandomMovementGenerator.h
 * @brief 随机移动生成器模块
 *
 * 本模块实现了控制生物在指定范围内随机漫游的移动生成器。
 * 主要用于：
 * - 生物的默认漫游行为
 * - 创建生动的世界环境
 * - 模拟生物的自然移动模式
 *
 * 特点：
 * - 在参考点周围的指定距离内随机移动
 * - 使用寻路系统避免障碍物
 * - 模拟真实生物的移动模式（移动-暂停-移动）
 * - 支持暂停和恢复功能
 */

#ifndef TRINITY_RANDOMMOTIONGENERATOR_H
#define TRINITY_RANDOMMOTIONGENERATOR_H

#include "MovementGenerator.h"
#include "Position.h"
#include "Timer.h"

class PathGenerator;

/**
 * @class RandomMovementGenerator
 * @brief 随机移动生成器模板类
 *
 * 控制生物在参考点周围的指定距离内随机移动。
 * 实现了模拟真实生物行为的移动-暂停模式：
 * - 生物会移动 2-10 次后暂停
 * - 暂停时间随机为 4-10 秒
 * - 移动目标在漫游距离内随机选择
 *
 * 该生成器仅对 Creature 类型有效，对 Player 类型的实现为空。
 *
 * @tparam T 单位类型（Player 或 Creature）
 */
template<class T>
class RandomMovementGenerator : public MovementGeneratorMedium<T, RandomMovementGenerator<T>>
{
    public:
        /**
         * @brief 构造函数
         *
         * @param distance 漫游距离，生物将在参考点周围此距离内随机移动
         *                 如果为 0，将使用生物自身的漫游距离设置
         */
        explicit RandomMovementGenerator(float distance = 0.0f);

        /**
         * @brief 获取移动生成器类型
         * @return 返回 RANDOM_MOTION_TYPE 枚举值
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief 暂停随机移动
         *
         * @param timer 暂停时间（毫秒），如果为 0 则无限期暂停
         *
         * 支持两种暂停模式：
         * - 定时暂停：在指定时间后自动恢复
         * - 无限期暂停：需要手动调用 Resume 恢复
         */
        void Pause(uint32 timer = 0) override;

        /**
         * @brief 恢复随机移动
         *
         * @param overrideTimer 如果非 0，重置定时器为指定值
         *
         * 清除暂停标志，恢复随机移动。
         */
        void Resume(uint32 overrideTimer = 0) override;

        /**
         * @brief 初始化移动生成器
         *
         * 当移动生成器首次添加到 MotionMaster 时调用。
         * 设置参考位置并初始化漫游参数。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoInitialize(T*);

        /**
         * @brief 重置移动生成器
         *
         * 当移动生成器被重新激活时调用。
         * 清除标志并重新初始化。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoReset(T*);

        /**
         * @brief 更新移动生成器
         *
         * 每个游戏循环周期调用一次。
         * 检查移动状态、处理暂停、触发新的随机移动。
         *
         * @param owner 拥有此移动生成器的单位
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return true 表示移动生成器继续运行
         */
        bool DoUpdate(T*, uint32);

        /**
         * @brief 停用移动生成器
         *
         * 当移动生成器被切换到后台时调用。
         * 设置停用标志并清除移动状态。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoDeactivate(T*);

        /**
         * @brief 结束移动生成器
         *
         * 当移动生成器从 MotionMaster 中移除时调用。
         * 清理状态并停止移动。
         *
         * @param owner 拥有此移动生成器的单位
         * @param active 是否处于活跃状态
         * @param movementInform 是否触发移动完成回调
         */
        void DoFinalize(T*, bool, bool);

        /**
         * @brief 单位速度变化通知
         *
         * 当单位的移动速度发生变化时调用。
         * 设置速度更新待处理标志。
         */
        void UnitSpeedChanged() override { RandomMovementGenerator<T>::AddFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING); }

    private:
        /**
         * @brief 设置随机目标位置并开始移动
         *
         * 核心方法，执行以下操作：
         * 1. 在漫游距离内随机选择一个目标点
         * 2. 检查目标点是否在视线范围内
         * 3. 使用寻路系统计算路径
         * 4. 启动 MoveSpline 移动
         * 5. 更新漫游步数和暂停定时器
         *
         * @param owner 拥有此移动生成器的单位
         */
        void SetRandomLocation(T*);

        std::unique_ptr<PathGenerator> _path; ///< 寻路路径生成器
        TimeTracker _timer;                   ///< 移动/暂停定时器
        Position _reference;                  ///< 参考位置（初始位置），漫游以此为圆心
        float _wanderDistance;                ///< 漫游距离，生物在此范围内随机移动
        uint8 _wanderSteps;                   ///< 剩余漫游步数，达到 0 时触发暂停
};

#endif
