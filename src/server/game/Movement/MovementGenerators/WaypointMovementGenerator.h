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
 * @file WaypointMovementGenerator.h
 * @brief 路点移动生成器模块
 *
 * 本模块实现了控制生物沿预定义路径点序列移动的生成器。
 * 主要用于：
 * - NPC 的巡逻路线
 * - 生物的 scripted 移动
 * - 任务相关的移动行为
 *
 * 特点：
 * - 支持从数据库加载路径或使用自定义路径
 * - 支持循环或单向路径
 * - 每个路点可以设置延迟、朝向和移动类型
 * - 支持路点事件触发
 * - 支持暂停和恢复功能
 */

#ifndef TRINITY_WAYPOINTMOVEMENTGENERATOR_H
#define TRINITY_WAYPOINTMOVEMENTGENERATOR_H

#include "MovementGenerator.h"
#include "PathMovementBase.h"
#include "Timer.h"

class Creature;
class Unit;
struct WaypointPath;

/**
 * @class WaypointMovementGenerator
 * @brief 路点移动生成器
 *
 * 控制生物沿预定义的路点序列移动。
 * 支持从数据库加载路径或使用动态创建的路径。
 *
 * 主要功能：
 * - 按顺序访问每个路点
 * - 在路点处可以设置延迟和朝向
 * - 触发路点相关的事件和脚本
 * - 通知 AI 路点到达和路径完成
 * - 支持循环路径或单向路径
 *
 * 仅对 Creature 类型有效，是 PathMovementBase 的具体实现。
 */
template<class T>
class WaypointMovementGenerator;

/**
 * @brief 路点移动生成器的 Creature 特化版本
 *
 * 继承自 MovementGeneratorMedium 和 PathMovementBase。
 * 实现了完整的路点移动逻辑。
 */
template<>
class WaypointMovementGenerator<Creature> : public MovementGeneratorMedium<Creature, WaypointMovementGenerator<Creature>>, public PathMovementBase<Creature, WaypointPath const*>
{
    public:
        /**
         * @brief 构造函数 - 从数据库路径 ID 创建
         *
         * @param pathId 路径 ID，对应数据库中的路径
         * @param repeating 是否循环，true 表示到达终点后从头开始
         */
        explicit WaypointMovementGenerator(uint32 pathId = 0, bool repeating = true);

        /**
         * @brief 构造函数 - 使用自定义路径
         *
         * @param path 自定义路径数据
         * @param repeating 是否循环
         */
        explicit WaypointMovementGenerator(WaypointPath& path, bool repeating = true);

        /**
         * @brief 析构函数
         */
        ~WaypointMovementGenerator() { _path = nullptr; }

        /**
         * @brief 获取移动生成器类型
         * @return 返回 WAYPOINT_MOTION_TYPE 枚举值
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief 单位速度变化通知
         *
         * 当单位的移动速度发生变化时调用。
         * 设置速度更新待处理标志，触发移动重新计算。
         */
        void UnitSpeedChanged() override { AddFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING); }

        /**
         * @brief 暂停路点移动
         *
         * @param timer 暂停时间（毫秒），0 表示无限期暂停
         */
        void Pause(uint32 timer = 0) override;

        /**
         * @brief 恢复路点移动
         *
         * @param overrideTimer 如果非 0，重置定时器为指定值
         */
        void Resume(uint32 overrideTimer = 0) override;

        /**
         * @brief 获取重置位置
         *
         * 返回当前路点的位置，用于生物被拉回时。
         *
         * @param owner 拥有此移动生成器的单位
         * @param x 输出参数，X 坐标
         * @param y 输出参数，Y 坐标
         * @param z 输出参数，Z 坐标
         * @return true 表示成功获取位置
         */
        bool GetResetPosition(Unit*, float& x, float& y, float& z) override;

        /**
         * @brief 初始化移动生成器
         *
         * 当移动生成器首次添加到 MotionMaster 时调用。
         * 加载路径数据并准备开始移动。
         *
         * @param owner 拥有此移动生成器的生物
         */
        void DoInitialize(Creature*);

        /**
         * @brief 重置移动生成器
         *
         * 当移动生成器被重新激活时调用。
         *
         * @param owner 拥有此移动生成器的生物
         */
        void DoReset(Creature*);

        /**
         * @brief 更新移动生成器
         *
         * 每个游戏循环周期调用一次。
         * 管理路点之间的移动、延迟和事件触发。
         *
         * @param owner 拥有此移动生成器的生物
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return true 表示移动生成器继续运行
         */
        bool DoUpdate(Creature*, uint32);

        /**
         * @brief 停用移动生成器
         *
         * 当移动生成器被切换到后台时调用。
         *
         * @param owner 拥有此移动生成器的生物
         */
        void DoDeactivate(Creature*);

        /**
         * @brief 结束移动生成器
         *
         * 当移动生成器从 MotionMaster 中移除时调用。
         *
         * @param owner 拥有此移动生成器的生物
         * @param active 是否处于活跃状态
         * @param movementInform 是否触发移动完成回调
         */
        void DoFinalize(Creature*, bool, bool);

        /**
         * @brief 获取调试信息
         * @return 包含路径和移动生成器信息的字符串
         */
        std::string GetDebugInfo() const override;

    private:
        /**
         * @brief 触发移动完成通知
         *
         * 通知生物 AI 当前路点已到达。
         *
         * @param owner 拥有此移动生成器的生物
         */
        void MovementInform(Creature*);

        /**
         * @brief 处理到达路点事件
         *
         * 当生物到达一个路点时调用：
         * - 设置延迟定时器
         * - 触发路点事件
         * - 通知 AI 路点已到达
         *
         * @param owner 拥有此移动生成器的生物
         */
        void OnArrived(Creature*);

        /**
         * @brief 开始移动到下一个路点
         *
         * 计算并启动到下一个路点的移动。
         *
         * @param owner 拥有此移动生成器的生物
         * @param relaunch 是否为重新启动（从中断恢复）
         */
        void StartMove(Creature*, bool relaunch = false);

        /**
         * @brief 计算下一个路点节点
         *
         * 更新当前路点索引到下一个节点。
         *
         * @return true 表示成功计算下一个节点，false 表示路径已完成
         */
        bool ComputeNextNode();

        /**
         * @brief 更新定时器
         *
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return true 表示定时器已过期
         */
        bool UpdateTimer(uint32 diff)
        {
            _nextMoveTime.Update(diff);
            if (_nextMoveTime.Passed())
            {
                _nextMoveTime.Reset(0);
                return true;
            }
            return false;
        }

        TimeTracker _nextMoveTime; ///< 下次移动的定时器（用于路点延迟）
        uint32 _pathId;            ///< 路径 ID（仅当 _loadedFromDB 为 true 时有效）
        bool _repeating;           ///< 是否循环路径
        bool _loadedFromDB;        ///< 是否从数据库加载路径
};

#endif
