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
 * @file BattlegroundNA.h
 * @brief 纳格兰竞技场（Nagrand Arena）战场模块头文件
 *
 * 本文件定义了纳格兰竞技场的具体实现，包括：
 * - 竞技场对象类型枚举（门、增益效果等）
 * - 游戏对象ID枚举
 * - 竞技场事件类型
 * - BattlegroundNA类定义
 *
 * 纳格兰竞技场是一个标准的2v2/3v3/5v5竞技场，位于外域纳格兰地区。
 * 该竞技场是开放式场地，没有障碍物，是一个纯粹的PvP竞技场。
 */

#ifndef __BATTLEGROUNDNA_H
#define __BATTLEGROUNDNA_H

#include "Arena.h"
#include "EventMap.h"

/**
 * @brief 纳格兰竞技场对象类型枚举
 *
 * 定义竞技场中所有游戏对象的索引，用于对象数组的访问和管理
 */
enum BattlegroundNAObjectTypes
{
    BG_NA_OBJECT_DOOR_1         = 0,  ///< 第一扇门（队伍1）
    BG_NA_OBJECT_DOOR_2         = 1,  ///< 第二扇门（队伍2）
    BG_NA_OBJECT_DOOR_3         = 2,  ///< 第三扇门（队伍1装饰门）
    BG_NA_OBJECT_DOOR_4         = 3,  ///< 第四扇门（队伍2装饰门）
    BG_NA_OBJECT_BUFF_1         = 4,  ///< 第一个增益效果对象（位置1）
    BG_NA_OBJECT_BUFF_2         = 5,  ///< 第二个增益效果对象（位置2）
    BG_NA_OBJECT_MAX            = 6   ///< 对象类型总数
};

/**
 * @brief 纳格兰竞技场游戏对象ID枚举
 *
 * 定义竞技场中各个游戏对象的模板ID，这些ID对应数据库中的gameobject_template表
 */
enum BattlegroundNAGameObjects
{
    BG_NA_OBJECT_TYPE_DOOR_1    = 183978,  ///< 第一扇门的游戏对象模板ID
    BG_NA_OBJECT_TYPE_DOOR_2    = 183980,  ///< 第二扇门的游戏对象模板ID
    BG_NA_OBJECT_TYPE_DOOR_3    = 183977,  ///< 第三扇门的游戏对象模板ID（装饰用）
    BG_NA_OBJECT_TYPE_DOOR_4    = 183979,  ///< 第四扇门的游戏对象模板ID（装饰用）
    BG_NA_OBJECT_TYPE_BUFF_1    = 184663,  ///< 第一个增益效果的游戏对象模板ID
    BG_NA_OBJECT_TYPE_BUFF_2    = 184664   ///< 第二个增益效果的游戏对象模板ID
};

/**
 * @brief 门移除定时器
 *
 * 竞技场开始后，门会在5秒后从场景中移除（而不是仅仅是打开）
 * 这是为了优化性能，因为门在比赛进行中不再需要
 */
inline constexpr Seconds BG_NA_REMOVE_DOORS_TIMER    = 5s;

/**
 * @brief 纳格兰竞技场事件类型枚举
 *
 * 定义竞技场中可能发生的各种事件，用于事件调度系统
 */
enum BattlegroundNAEvents
{
    BG_NA_EVENT_REMOVE_DOORS    = 1  ///< 移除竞技场大门事件
};

/**
 * @class BattlegroundNA
 * @brief 纳格兰竞技场类
 *
 * 继承自Arena类，实现纳格兰竞技场的具体逻辑，包括：
 * - 竞技场大门的控制（关闭、打开、移除）
 * - 增益效果的刷新
 * - 区域触发器处理
 * - 世界状态的初始化
 *
 * 纳格兰竞技场特点：
 * - 开放式场地，无视觉障碍
 * - 两个增益刷新点
 * - 对称设计，保证公平性
 */
class BattlegroundNA : public Arena
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场对象容器大小
         */
        BattlegroundNA();

        /**
         * @brief 开始事件 - 关闭大门
         *
         * 在竞技场准备阶段生成并关闭所有大门
         * 这些门会阻止玩家在比赛开始前离开起始区域
         *
         * 调用时机：战场状态变为STATUS_WAIT_JOIN时
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件 - 打开大门
         *
         * 比赛正式开始时打开主要大门，并安排移除大门的事件
         * 同时刷新竞技场中的增益效果对象
         *
         * 调用时机：战场状态变为STATUS_IN_PROGRESS时
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 处理区域触发器
         * @param Source 触发区域的玩家指针
         * @param Trigger 触发器ID
         *
         * 处理玩家进入特定区域时的触发事件
         * 主要用于处理增益效果区域的触发
         *
         * 调用时机：玩家进入特定区域触发器时
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 设置战场
         * @return 成功返回true，失败返回false
         *
         * 在地图中生成所有必要的游戏对象（门、增益效果等）
         * 对象位置坐标由函数参数指定
         *
         * 调用时机：战场初始化时
         */
        bool SetupBattleground() override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包引用
         *
         * 向客户端发送战场初始世界状态数据
         * 包括竞技场显示状态等UI元素
         *
         * 调用时机：玩家进入战场时
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

    private:
        /**
         * @brief 战场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 每帧调用的核心更新函数，处理：
         * - 事件调度器的更新
         * - 定时事件的执行（如移除大门）
         *
         * 调用时机：每帧更新循环中，仅当战场状态为STATUS_IN_PROGRESS时执行
         * 性能注意事项：该函数每帧调用，需保持高效
         */
        void PostUpdateImpl(uint32 diff) override;

        EventMap _events;  ///< 事件调度器，用于管理定时事件（如移除大门）
};
#endif
