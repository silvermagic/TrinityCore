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
 * @file BattlegroundDS.h
 * @brief 达拉然下水道竞技场（Dalaran Sewers Arena）头文件
 *
 * 本文件定义了达拉然下水道竞技场的核心数据结构和逻辑。
 * 达拉然下水道竞技场是一个2v2/3v3/5v5的竞技场，特色：
 * - 场地中央有一个周期性出现的瀑布
 * - 瀑布会阻挡视线和移动，并将玩家击退
 * - 玩家从管道进入场地时会被水流推出
 * - 场地内有两个增益buff刷新点
 *
 * 竞技场的目标是击败对方队伍的所有玩家。
 */

#ifndef __BATTLEGROUNDDS_H
#define __BATTLEGROUNDDS_H

#include "Arena.h"
#include "EventMap.h"

/**
 * @brief 达拉然下水道竞技场游戏对象类型枚举
 *
 * 定义了竞技场中所有游戏对象的索引
 */
enum BattlegroundDSObjectTypes
{
    BG_DS_OBJECT_DOOR_1         = 0,  ///< 大门1
    BG_DS_OBJECT_DOOR_2         = 1,  ///< 大门2
    BG_DS_OBJECT_WATER_1        = 2,  ///< 瀑布碰撞体（阻挡移动）
    BG_DS_OBJECT_WATER_2        = 3,  ///< 瀑布视觉效果
    BG_DS_OBJECT_BUFF_1         = 4,  ///< 增益buff 1
    BG_DS_OBJECT_BUFF_2         = 5,  ///< 增益buff 2
    BG_DS_OBJECT_MAX            = 6   ///< 对象总数
};

/**
 * @brief 达拉然下水道竞技场游戏对象ID枚举
 *
 * 定义了各种游戏对象的模板ID
 */
enum BattlegroundDSGameObjects
{
    BG_DS_OBJECT_TYPE_DOOR_1    = 192642,  ///< 大门1模板ID
    BG_DS_OBJECT_TYPE_DOOR_2    = 192643,  ///< 大门2模板ID
    BG_DS_OBJECT_TYPE_WATER_1   = 194395,  ///< 瀑布碰撞体模板ID
    BG_DS_OBJECT_TYPE_WATER_2   = 191877,  ///< 瀑布视觉效果模板ID
    BG_DS_OBJECT_TYPE_BUFF_1    = 184663,  ///< 增益buff 1模板ID
    BG_DS_OBJECT_TYPE_BUFF_2    = 184664   ///< 增益buff 2模板ID
};

/**
 * @brief 达拉然下水道竞技场生物类型枚举
 *
 * 定义了竞技场中所有生物的索引
 */
enum BattlegroundDSCreatureTypes
{
    BG_DS_NPC_WATERFALL_KNOCKBACK = 0,  ///< 瀑布击退生物
    BG_DS_NPC_PIPE_KNOCKBACK_1    = 1,  ///< 管道击退生物1
    BG_DS_NPC_PIPE_KNOCKBACK_2    = 2,  ///< 管道击退生物2
    BG_DS_NPC_MAX                 = 3   ///< 生物总数
};

/**
 * @brief 达拉然下水道竞技场生物ID枚举
 *
 * 定义了各种生物的模板ID
 */
enum BattlegroundDSCreatures
{
    BG_DS_NPC_TYPE_WATER_SPOUT    = 28567  ///< 水柱生物模板ID（用于击退效果）
};

/**
 * @brief 达拉然下水道竞技场法术枚举
 *
 * 定义了竞技场中使用的法术ID
 */
enum BattlegroundDSSpells
{
    BG_DS_SPELL_FLUSH             = 57405,  ///< 管道冲水视觉和目标选择法术
    BG_DS_SPELL_FLUSH_KNOCKBACK   = 61698,  ///< 管道冲水击退效果（触发法术）
    BG_DS_SPELL_WATER_SPOUT       = 58873,  ///< 瀑布击退效果

    SPELL_WARL_DEMONIC_CIRCLE     = 48018   ///< 术士恶魔传送门（需要移除）
};

/**
 * @brief 达拉然下水道竞技场数据枚举
 *
 * 定义了竞技场的时间相关数据
 * 注意：这些值可能不是完全正确的，需要进一步确认
 */
enum BattlegroundDSData
{
    BG_DS_PIPE_KNOCKBACK_FIRST_DELAY    = 5000,  ///< 管道首次击退延迟（毫秒）
    BG_DS_PIPE_KNOCKBACK_DELAY          = 3000,  ///< 管道后续击退延迟（毫秒）
    BG_DS_PIPE_KNOCKBACK_TOTAL_COUNT    = 2,     ///< 管道击退总次数
};

/// 瀑布定时器最小值（30秒）
inline constexpr Seconds BG_DS_WATERFALL_TIMER_MIN = 30s;
/// 瀑布定时器最大值（60秒）
inline constexpr Seconds BG_DS_WATERFALL_TIMER_MAX = 60s;
/// 瀑布警告持续时间（5秒，水流开始但没有阻挡）
inline constexpr Seconds BG_DS_WATERFALL_WARNING_DURATION = 5s;
/// 瀑布持续时间（30秒）
inline constexpr Seconds BG_DS_WATERFALL_DURATION = 30s;
/// 瀑布击退定时器（1.5秒）
inline constexpr Milliseconds BG_DS_WATERFALL_KNOCKBACK_TIMER = 1500ms;

/**
 * @brief 达拉然下水道竞技场事件枚举
 *
 * 定义了竞技场中的各种事件
 */
enum BattlegroundDSEvents
{
    BG_DS_EVENT_WATERFALL_WARNING       = 1,  ///< 瀑布警告（水流开始，但没有阻挡）
    BG_DS_EVENT_WATERFALL_ON            = 2,  ///< 瀑布激活（阻挡视线和移动）
    BG_DS_EVENT_WATERFALL_OFF           = 3,  ///< 瀑布关闭
    BG_DS_EVENT_WATERFALL_KNOCKBACK     = 4,  ///< 瀑布击退

    BG_DS_EVENT_PIPE_KNOCKBACK          = 5   ///< 管道击退
};

/**
 * @brief 达拉然下水道竞技场类
 *
 * 继承自Arena，实现达拉然下水道竞技场的核心逻辑
 * 包括竞技场的初始化、大门控制、瀑布周期、管道击退等功能
 */
class BattlegroundDS : public Arena
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化竞技场成员变量，设置游戏对象和生物容器大小
         */
        BattlegroundDS();

        /* 继承自Battleground类的虚函数 */
        /**
         * @brief 开始事件：关闭大门
         *
         * 在竞技场开始前的准备阶段调用，关闭并生成所有大门
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件：打开大门
         *
         * 竞技场开始时调用，打开大门，生成增益buff，启动瀑布定时器和管道击退
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 处理区域触发器
         * @param Source 触发玩家
         * @param Trigger 触发器ID
         *
         * 处理玩家进入特定区域触发器的事件（如管道区域）
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 设置竞技场
         * @return 设置成功返回true，否则返回false
         *
         * 生成竞技场中的所有游戏对象和生物（大门、瀑布、增益buff、击退生物等）
         */
        bool SetupBattleground() override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         *
         * 初始化客户端的世界状态，显示达拉然下水道竞技场UI
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

    private:
        /**
         * @brief 竞技场更新实现
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理：
         * - 瀑布周期事件（警告、激活、关闭、击退）
         * - 管道击退计时
         */
        void PostUpdateImpl(uint32 diff) override;

        EventMap _events;          ///< 事件映射表，用于管理定时事件

        uint32 _pipeKnockBackTimer;  ///< 管道击退计时器（毫秒）
        uint8 _pipeKnockBackCount;   ///< 管道击退次数计数器
};

#endif
