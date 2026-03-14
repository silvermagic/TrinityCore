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
 * @file BattlegroundBE.h
 * @brief 刀锋山竞技场（Blade's Edge Arena）头文件
 *
 * 本文件定义了刀锋山竞技场的核心数据结构和逻辑。
 * 刀锋山竞技场是一个2v2/3v3/5v5的竞技场，特色：
 * - 场地中央有一座桥，两侧是深坑
 * - 桥下有木质立柱，可以利用地形进行战术操作
 * - 场地内有两个增益buff刷新点
 *
 * 竞技场的目标是击败对方队伍的所有玩家。
 */

#ifndef __BATTLEGROUNDBE_H
#define __BATTLEGROUNDBE_H

#include "Arena.h"
#include "EventMap.h"

/**
 * @brief 刀锋山竞技场游戏对象类型枚举
 *
 * 定义了竞技场中所有游戏对象的索引
 */
enum BattlegroundBEObjectTypes
{
    BG_BE_OBJECT_DOOR_1         = 0,  ///< 大门1（联盟方）
    BG_BE_OBJECT_DOOR_2         = 1,  ///< 大门2（部落方）
    BG_BE_OBJECT_DOOR_3         = 2,  ///< 大门3（装饰用）
    BG_BE_OBJECT_DOOR_4         = 3,  ///< 大门4（装饰用）
    BG_BE_OBJECT_BUFF_1         = 4,  ///< 增益buff 1
    BG_BE_OBJECT_BUFF_2         = 5,  ///< 增益buff 2
    BG_BE_OBJECT_MAX            = 6   ///< 对象总数
};

/**
 * @brief 刀锋山竞技场游戏对象ID枚举
 *
 * 定义了各种游戏对象的模板ID
 */
enum BattlegroundBEGameObjects
{
    BG_BE_OBJECT_TYPE_DOOR_1    = 183971,  ///< 大门1模板ID
    BG_BE_OBJECT_TYPE_DOOR_2    = 183973,  ///< 大门2模板ID
    BG_BE_OBJECT_TYPE_DOOR_3    = 183970,  ///< 大门3模板ID
    BG_BE_OBJECT_TYPE_DOOR_4    = 183972,  ///< 大门4模板ID
    BG_BE_OBJECT_TYPE_BUFF_1    = 184663,  ///< 增益buff 1模板ID
    BG_BE_OBJECT_TYPE_BUFF_2    = 184664   ///< 增益buff 2模板ID
};

/// 大门移除定时器（竞技场开始5秒后移除装饰性大门）
inline constexpr Seconds BG_BE_REMOVE_DOORS_TIMER = 5s;

/**
 * @brief 刀锋山竞技场事件枚举
 *
 * 定义了竞技场中的各种事件
 */
enum BattlegroundBEEvents
{
    BG_BE_EVENT_REMOVE_DOORS    = 1  ///< 移除装饰性大门事件
};

/**
 * @brief 刀锋山竞技场类
 *
 * 继承自Arena，实现刀锋山竞技场的核心逻辑
 * 包括竞技场的初始化、大门控制、增益buff刷新等功能
 */
class BattlegroundBE : public Arena
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场成员变量，设置游戏对象容器大小
         */
        BattlegroundBE();

        /* 继承自Battleground类的虚函数 */
        /**
         * @brief 开始事件：关闭大门
         *
         * 在竞技场开始前的准备阶段调用，关闭并生成所有大门，隐藏增益buff
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件：打开大门
         *
         * 竞技场开始时调用，打开大门，生成增益buff，启动大门移除定时器
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 处理区域触发器
         * @param Source 触发玩家
         * @param Trigger 触发器ID
         *
         * 处理玩家进入特定区域触发器的事件
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 设置竞技场
         * @return 设置成功返回true，否则返回false
         *
         * 生成竞技场中的所有游戏对象（大门、增益buff等）
         */
        bool SetupBattleground() override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         *
         * 初始化客户端的世界状态，显示刀锋山竞技场UI
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

    private:
        /**
         * @brief 竞技场更新实现
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理定时事件（如移除装饰性大门）
         */
        void PostUpdateImpl(uint32 diff) override;

        EventMap _events;  ///< 事件映射表，用于管理定时事件
};
#endif
