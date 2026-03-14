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
 * @file BattlegroundBE.cpp
 * @brief 刀锋山竞技场（Blade's Edge Arena）实现文件
 *
 * 本文件实现了刀锋山竞技场的核心逻辑，包括：
 * - 竞技场初始化和设置
 * - 大门控制
 * - 增益buff刷新
 * - 区域触发器处理
 *
 * 刀锋山竞技场是一个2v2/3v3/5v5的竞技场，特色是中央有一座桥，
 * 两侧是深坑，玩家需要利用地形进行战斗。
 */

#include "BattlegroundBE.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

/**
 * @brief 构造函数
 *
 * 初始化竞技场成员变量，调整游戏对象容器大小
 *
 * 调用时机：竞技场创建时
 */
BattlegroundBE::BattlegroundBE()
{
    BgObjects.resize(BG_BE_OBJECT_MAX);  // 调整游戏对象容器大小
}

/**
 * @brief 竞技场更新实现
 * @param diff 时间差（毫秒）
 *
 * 每帧调用，处理定时事件：
 * - 移除装饰性大门（竞技场开始5秒后）
 *
 * 调用时机：每帧更新时由Battleground::Update调用
 * 性能注意：此函数每帧调用，需要优化处理
 */
void BattlegroundBE::PostUpdateImpl(uint32 diff)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 更新事件计时器
    _events.Update(diff);

    // 执行到期的事件
    while (uint32 eventId = _events.ExecuteEvent())
    {
        switch (eventId)
        {
            case BG_BE_EVENT_REMOVE_DOORS:
                // 移除装饰性大门（竞技场开始5秒后）
                for (uint32 i = BG_BE_OBJECT_DOOR_1; i <= BG_BE_OBJECT_DOOR_2; ++i)
                    DelObject(i);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 开始事件：关闭大门
 *
 * 在竞技场开始前的准备阶段调用：
 * 1. 生成所有大门（包括功能性和装饰性大门）
 * 2. 隐藏增益buff（设为1天后重生）
 *
 * 调用时机：竞技场初始化时，玩家进入准备阶段
 */
void BattlegroundBE::StartingEventCloseDoors()
{
    // 生成所有大门
    for (uint32 i = BG_BE_OBJECT_DOOR_1; i <= BG_BE_OBJECT_DOOR_4; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);

    // 隐藏增益buff
    for (uint32 i = BG_BE_OBJECT_BUFF_1; i <= BG_BE_OBJECT_BUFF_2; ++i)
        SpawnBGObject(i, RESPAWN_ONE_DAY);
}

/**
 * @brief 开始事件：打开大门
 *
 * 竞技场正式开始时调用：
 * 1. 打开功能性大门
 * 2. 启动装饰性大门移除定时器（5秒后移除）
 * 3. 生成增益buff（60秒后刷新）
 *
 * 调用时机：竞技场准备时间结束，战斗正式开始
 */
void BattlegroundBE::StartingEventOpenDoors()
{
    // 打开功能性大门
    for (uint32 i = BG_BE_OBJECT_DOOR_1; i <= BG_BE_OBJECT_DOOR_2; ++i)
        DoorOpen(i);

    // 启动装饰性大门移除定时器
    _events.ScheduleEvent(BG_BE_EVENT_REMOVE_DOORS, BG_BE_REMOVE_DOORS_TIMER);

    // 生成增益buff（60秒后刷新）
    for (uint32 i = BG_BE_OBJECT_BUFF_1; i <= BG_BE_OBJECT_BUFF_2; ++i)
        SpawnBGObject(i, 60);
}

/**
 * @brief 处理区域触发器
 * @param player 触发玩家
 * @param trigger 触发器ID
 *
 * 处理玩家进入特定区域触发器的事件：
 * - 4538, 4539: 增益buff相关触发器（目前未实现特殊处理）
 *
 * 调用时机：玩家进入区域触发器范围时
 */
void BattlegroundBE::HandleAreaTrigger(Player* player, uint32 trigger)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 4538:  // 增益buff触发器？
        case 4539:  // 增益buff触发器？
            break;
        default:
            // 其他触发器交给父类处理
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

/**
 * @brief 填充初始世界状态
 * @param packet 世界状态数据包
 *
 * 初始化客户端的世界状态：
 * - 显示刀锋山竞技场UI
 * - 调用父类方法填充基础世界状态
 *
 * 调用时机：玩家进入竞技场时，发送初始化数据
 */
void BattlegroundBE::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 显示刀锋山竞技场UI
    packet.Worldstates.emplace_back(2547, 1);  // BATTLEGROUND_BLADES_EDGE_ARENA_SHOW

    // 调用父类方法填充基础世界状态
    Arena::FillInitialWorldStates(packet);
}

/**
 * @brief 设置竞技场
 * @return 设置成功返回true，否则返回false
 *
 * 生成竞技场中的所有游戏对象：
 * 1. 生成4个大门（包括功能性大门和装饰性大门）
 * 2. 生成2个增益buff
 *
 * 调用时机：竞技场创建时
 * 性能注意：此函数只在竞技场创建时调用一次，性能开销较小
 */
bool BattlegroundBE::SetupBattleground()
{
    // 生成大门和增益buff
    // 大门（功能性大门和装饰性大门）
    if (!AddObject(BG_BE_OBJECT_DOOR_1, BG_BE_OBJECT_TYPE_DOOR_1, 6287.277f, 282.1877f, 3.810925f, -2.260201f, 0, 0, 0.9044551f, -0.4265689f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_BE_OBJECT_DOOR_2, BG_BE_OBJECT_TYPE_DOOR_2, 6189.546f, 241.7099f, 3.101481f, 0.8813917f, 0, 0, 0.4265689f, 0.9044551f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_BE_OBJECT_DOOR_3, BG_BE_OBJECT_TYPE_DOOR_3, 6299.116f, 296.5494f, 3.308032f, 0.8813917f, 0, 0, 0.4265689f, 0.9044551f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_BE_OBJECT_DOOR_4, BG_BE_OBJECT_TYPE_DOOR_4, 6177.708f, 227.3481f, 3.604374f, -2.260201f, 0, 0, 0.9044551f, -0.4265689f, RESPAWN_IMMEDIATELY)
        // 增益buff（120秒后刷新）
        || !AddObject(BG_BE_OBJECT_BUFF_1, BG_BE_OBJECT_TYPE_BUFF_1, 6249.042f, 275.3239f, 11.22033f, -1.448624f, 0, 0, 0.6626201f, -0.7489557f, 120)
        || !AddObject(BG_BE_OBJECT_BUFF_2, BG_BE_OBJECT_TYPE_BUFF_2, 6228.26f, 249.566f, 11.21812f, -0.06981307f, 0, 0, 0.03489945f, -0.9993908f, 120))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundBE: Failed to spawn some object!");
        return false;
    }

    return true;
}
