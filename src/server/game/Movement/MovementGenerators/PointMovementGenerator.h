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
 * @file PointMovementGenerator.h
 * @brief 点位移动生成器模块
 *
 * 本模块实现了控制单位移动到指定目标点的移动生成器。
 * 主要用于：
 * - NPC 移动到指定坐标点
 * - 玩家移动到特定位置
 * - 协助移动（如求援行为）
 *
 * 核心类：
 * - PointMovementGenerator: 通用点位移动生成器模板类
 * - AssistanceMovementGenerator: 求援移动生成器（专用于生物求援行为）
 */

#ifndef TRINITY_POINTMOVEMENTGENERATOR_H
#define TRINITY_POINTMOVEMENTGENERATOR_H

#include "MovementGenerator.h"
#include "Optional.h"

class Creature;

/**
 * @class PointMovementGenerator
 * @brief 点位移动生成器模板类
 *
 * 控制单位移动到指定的世界坐标点。
 * 支持路径生成、自定义速度和最终朝向设置。
 * 适用于 Player 和 Creature 两种单位类型。
 *
 * @tparam T 单位类型（Player 或 Creature）
 */
template<class T>
class PointMovementGenerator : public MovementGeneratorMedium<T, PointMovementGenerator<T>>
{
    public:
        /**
         * @brief 构造函数
         *
         * @param id 移动标识符，用于回调时识别不同的移动任务
         * @param x 目标点 X 坐标
         * @param y 目标点 Y 坐标
         * @param z 目标点 Z 坐标
         * @param generatePath 是否生成寻路路径（true 表示使用导航网格寻路）
         * @param speed 移动速度，0.0f 表示使用单位默认速度
         * @param finalOrient 可选参数，到达目标点后的最终朝向角度
         */
        explicit PointMovementGenerator(uint32 id, float x, float y, float z, bool generatePath, float speed = 0.0f, Optional<float> finalOrient = {});

        /**
         * @brief 获取移动生成器类型
         * @return 返回 POINT_MOTION_TYPE 枚举值
         */
        MovementGeneratorType GetMovementGeneratorType() const override;

        /**
         * @brief 初始化移动生成器
         *
         * 当移动生成器首次添加到 MotionMaster 时调用。
         * 设置移动标志，启动 MoveSpline 移动。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoInitialize(T*);

        /**
         * @brief 重置移动生成器
         *
         * 当移动生成器被重新激活时调用。
         * 清除过渡标志并重新初始化移动。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoReset(T*);

        /**
         * @brief 更新移动生成器
         *
         * 每个游戏循环周期调用一次。
         * 检查移动状态、处理中断、重新启动被中断的移动。
         *
         * @param owner 拥有此移动生成器的单位
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return true 表示移动生成器继续运行，false 表示移动已完成
         */
        bool DoUpdate(T*, uint32);

        /**
         * @brief 停用移动生成器
         *
         * 当移动生成器被切换到后台（非活跃状态）时调用。
         * 设置停用标志并清除单位移动状态。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void DoDeactivate(T*);

        /**
         * @brief 结束移动生成器
         *
         * 当移动生成器从 MotionMaster 中移除时调用。
         * 清理状态并触发移动完成通知。
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
         * 设置速度更新待处理标志，触发移动重新计算。
         */
        void UnitSpeedChanged() override { PointMovementGenerator<T>::AddFlag(MOVEMENTGENERATOR_FLAG_SPEED_UPDATE_PENDING); }

        /**
         * @brief 获取移动标识符
         * @return 移动任务的唯一标识符
         */
        uint32 GetId() const { return _movementId; }

    private:
        /**
         * @brief 触发移动完成通知
         *
         * 通知单位 AI 移动任务已完成。
         * Creature 版本会调用 AI 的 MovementInform 方法。
         *
         * @param owner 拥有此移动生成器的单位
         */
        void MovementInform(T*);

        uint32 _movementId;          ///< 移动任务标识符，用于回调识别
        float _x, _y, _z;            ///< 目标点坐标
        float _speed;                ///< 移动速度（0 表示使用单位默认速度）
        bool _generatePath;          ///< 是否使用导航网格生成路径
        Optional<float> _finalOrient; ///< 到达目标点后的最终朝向角度（可选）
};

/**
 * @class AssistanceMovementGenerator
 * @brief 求援移动生成器
 *
 * 专用于生物求援行为的移动生成器。
 * 当生物受到攻击并需要请求同伴协助时使用。
 * 继承自 PointMovementGenerator，在移动完成后会触发求援呼叫。
 *
 * 行为流程：
 * 1. 生物移动到求援点
 * 2. 到达后呼叫附近的同伴协助
 * 3. 进入短暂的分心状态等待同伴响应
 */
class AssistanceMovementGenerator : public PointMovementGenerator<Creature>
{
    public:
        /**
         * @brief 构造函数
         *
         * @param id 移动标识符
         * @param x 求援点 X 坐标
         * @param y 求援点 Y 坐标
         * @param z 求援点 Z 坐标
         *
         * 注意：强制启用路径生成（generatePath = true）
         */
        explicit AssistanceMovementGenerator(uint32 id, float x, float y, float z) : PointMovementGenerator<Creature>(id, x, y, z, true) { }

        /**
         * @brief 结束移动生成器
         *
         * 重写父类方法，在移动完成后执行求援逻辑：
         * - 启用求援功能
         * - 呼叫附近同伴
         * - 启动求援分心定时器
         *
         * @param owner 拥有此移动生成器的单位
         * @param active 是否处于活跃状态
         * @param movementInform 是否触发移动完成回调
         */
        void Finalize(Unit*, bool, bool) override;

        /**
         * @brief 获取移动生成器类型
         * @return 返回 ASSISTANCE_MOTION_TYPE 枚举值
         */
        MovementGeneratorType GetMovementGeneratorType() const override;
};

#endif
