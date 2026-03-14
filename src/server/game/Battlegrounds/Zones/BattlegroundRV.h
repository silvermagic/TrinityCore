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
 * @file BattlegroundRV.h
 * @brief 环形竞技场（Ring of Valor）战场模块
 *
 * 本模块实现了环形竞技场（达拉然竞技场）的核心逻辑，包括：
 * - 竞技场环境控制（火墙、升降梯、柱子）
 * - 动态地形变化机制
 * - 竞技场特有的游戏对象管理
 *
 * 环形竞技场特点：
 * - 动态柱子系统：柱子会周期性地升起和降下
 * - 火墙机制：开场时有火墙阻挡，随后消失
 * - 升降梯：玩家通过升降梯进入竞技场
 */

#ifndef __BATTLEGROUNDRV_H
#define __BATTLEGROUNDRV_H

#include "Arena.h"

/**
 * @brief 环形竞技场游戏对象类型枚举
 *
 * 定义竞技场中所有游戏对象的索引，用于对象管理和访问
 */
enum BattlegroundRVObjectTypes
{
    BG_RV_OBJECT_BUFF_1,             ///< 第一个增益效果刷新点
    BG_RV_OBJECT_BUFF_2,             ///< 第二个增益效果刷新点
    BG_RV_OBJECT_FIRE_1,             ///< 第一个火墙对象
    BG_RV_OBJECT_FIRE_2,             ///< 第二个火墙对象
    BG_RV_OBJECT_FIREDOOR_1,         ///< 第一个火墙门（视觉效果）
    BG_RV_OBJECT_FIREDOOR_2,         ///< 第二个火墙门（视觉效果）

    BG_RV_OBJECT_PILAR_1,            ///< 柱子1（斧头图标）
    BG_RV_OBJECT_PILAR_3,            ///< 柱子3（闪电图标）
    BG_RV_OBJECT_GEAR_1,             ///< 齿轮1（装饰性对象）
    BG_RV_OBJECT_GEAR_2,             ///< 齿轮2（装饰性对象）

    BG_RV_OBJECT_PILAR_2,            ///< 柱子2（竞技场图标）
    BG_RV_OBJECT_PILAR_4,            ///< 柱子4（象牙图标）
    BG_RV_OBJECT_PULLEY_1,           ///< 滑轮1（装饰性对象）
    BG_RV_OBJECT_PULLEY_2,           ///< 滑轮2（装饰性对象）

    BG_RV_OBJECT_PILAR_COLLISION_1,  ///< 柱子碰撞体1（斧头）
    BG_RV_OBJECT_PILAR_COLLISION_2,  ///< 柱子碰撞体2（竞技场）
    BG_RV_OBJECT_PILAR_COLLISION_3,  ///< 柱子碰撞体3（闪电）
    BG_RV_OBJECT_PILAR_COLLISION_4,  ///< 柱子碰撞体4（象牙）

    BG_RV_OBJECT_ELEVATOR_1,         ///< 升降梯1（北端）
    BG_RV_OBJECT_ELEVATOR_2,         ///< 升降梯2（南端）
    BG_RV_OBJECT_MAX                 ///< 对象总数
};

/**
 * @brief 环形竞技场游戏对象ID枚举
 *
 * 定义游戏中实际的游戏对象模板ID，用于创建对应的游戏对象实例
 */
enum BattlegroundRVGameObjects
{
    BG_RV_OBJECT_TYPE_BUFF_1                     = 184663,  ///< 增益效果对象1
    BG_RV_OBJECT_TYPE_BUFF_2                     = 184664,  ///< 增益效果对象2
    BG_RV_OBJECT_TYPE_FIRE_1                     = 192704,  ///< 火墙对象1
    BG_RV_OBJECT_TYPE_FIRE_2                     = 192705,  ///< 火墙对象2

    BG_RV_OBJECT_TYPE_FIREDOOR_2                 = 192387,  ///< 火墙门对象2
    BG_RV_OBJECT_TYPE_FIREDOOR_1                 = 192388,  ///< 火墙门对象1
    BG_RV_OBJECT_TYPE_PULLEY_1                   = 192389,  ///< 滑轮对象1
    BG_RV_OBJECT_TYPE_PULLEY_2                   = 192390,  ///< 滑轮对象2
    BG_RV_OBJECT_TYPE_GEAR_1                     = 192393,  ///< 齿轮对象1
    BG_RV_OBJECT_TYPE_GEAR_2                     = 192394,  ///< 齿轮对象2
    BG_RV_OBJECT_TYPE_ELEVATOR_1                 = 194582,  ///< 升降梯对象1
    BG_RV_OBJECT_TYPE_ELEVATOR_2                 = 194586,  ///< 升降梯对象2

    BG_RV_OBJECT_TYPE_PILAR_COLLISION_1          = 194580,  ///< 柱子碰撞体1 - 斧头（Axe）
    BG_RV_OBJECT_TYPE_PILAR_COLLISION_2          = 194579,  ///< 柱子碰撞体2 - 竞技场（Arena）
    BG_RV_OBJECT_TYPE_PILAR_COLLISION_3          = 194581,  ///< 柱子碰撞体3 - 闪电（Lightning）
    BG_RV_OBJECT_TYPE_PILAR_COLLISION_4          = 194578,  ///< 柱子碰撞体4 - 象牙（Ivory）

    BG_RV_OBJECT_TYPE_PILAR_1                    = 194583,  ///< 柱子视觉对象1 - 斧头（Axe）
    BG_RV_OBJECT_TYPE_PILAR_2                    = 194584,  ///< 柱子视觉对象2 - 竞技场（Arena）
    BG_RV_OBJECT_TYPE_PILAR_3                    = 194585,  ///< 柱子视觉对象3 - 闪电（Lightning）
    BG_RV_OBJECT_TYPE_PILAR_4                    = 194587   ///< 柱子视觉对象4 - 象牙（Ivory）
};

/**
 * @brief 环形竞技场状态和数据枚举
 *
 * 定义竞技场的状态流转和定时器常量
 */
enum BattlegroundRVData
{
    BG_RV_STATE_OPEN_FENCES,       ///< 状态：打开火墙（开场阶段）
    BG_RV_STATE_SWITCH_PILLARS,    ///< 状态：切换柱子状态（柱子升降）
    BG_RV_STATE_CLOSE_FIRE,        ///< 状态：关闭火墙（开场后）

    BG_RV_PILLAR_SWITCH_TIMER                    = 25000,  ///< 柱子切换间隔：25秒
    BG_RV_FIRE_TO_PILLAR_TIMER                   = 20000,  ///< 火墙关闭到柱子切换的时间：20秒
    BG_RV_CLOSE_FIRE_TIMER                       =  5000,  ///< 火墙关闭持续时间：5秒
    BG_RV_FIRST_TIMER                            = 20133,  ///< 首次触发时间：约20秒（开场到火墙）

    BG_RV_WORLD_STATE                            = 0xe1a   ///< 世界状态ID
};

/**
 * @class BattlegroundRV
 * @brief 环形竞技场（Ring of Valor）战场类
 *
 * 继承自Arena类，实现了达拉然环形竞技场的核心逻辑：
 * - 管理动态地形元素（柱子、火墙、升降梯）
 * - 处理竞技场特有的状态流转
 * - 协调视觉和碰撞对象的同步更新
 *
 * 主要机制：
 * 1. 开场阶段：升降梯启动，火墙阻挡，准备就绪
 * 2. 战斗阶段：火墙消失，柱子周期性升降
 * 3. 柱子系统：每25秒切换一次柱子状态
 */
class BattlegroundRV : public Arena
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场对象，设置初始状态和计时器
         */
        BattlegroundRV();

        /**
         * @brief 开门事件处理
         *
         * 当竞技场开始时调用：
         * - 启动增益效果刷新
         * - 打开升降梯
         * - 初始化火墙和柱子系统
         *
         * 调用时机：竞技场准备阶段结束，战斗开始时
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 填充初始世界状态数据
         * @param packet 世界状态数据包
         *
         * 向客户端发送竞技场的初始世界状态信息
         *
         * 调用时机：玩家进入竞技场时
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 处理区域触发事件
         * @param Source 触发区域的玩家
         * @param Trigger 区域触发器ID
         *
         * 处理玩家进入特定区域时的事件，主要用于火墙和升降梯区域
         *
         * 调用时机：玩家进入区域触发器范围时
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 设置竞技场
         * @return 设置成功返回true，否则返回false
         *
         * 创建竞技场中的所有游戏对象：
         * - 升降梯
         * - 火墙和火墙门
         * - 柱子及其碰撞体
         * - 增益效果点
         * - 装饰性对象（齿轮、滑轮）
         *
         * 调用时机：竞技场初始化时
         * 性能注意：涉及大量对象创建，应避免频繁调用
         */
        bool SetupBattleground() override;

    private:
        /**
         * @brief 竞技场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 核心更新逻辑，处理：
         * - 状态机流转
         * - 计时器管理
         * - 火墙和柱子的时序控制
         *
         * 调用时机：每次战场更新循环（通常每帧）
         * 性能注意：高频调用，需保持轻量
         */
        void PostUpdateImpl(uint32 diff) override;

        /**
         * @brief 切换柱子碰撞状态
         *
         * 切换柱子的升起/降下状态：
         * - 更新视觉对象（柱子外观）
         * - 更新碰撞对象（玩家阻挡）
         * - 同步状态给所有玩家
         *
         * 调用时机：每25秒自动调用一次
         */
        void TogglePillarCollision();

        uint32 _timer;           ///< 当前状态计时器（毫秒）
        uint32 _state;           ///< 当前状态（BG_RV_STATE_*）
        bool   _pillarCollision; ///< 柱子碰撞状态标记（true=升起，false=降下）
};
#endif
