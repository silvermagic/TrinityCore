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
 * @file BattlegroundRL.cpp
 * @brief 洛丹伦废墟竞技场（Ruins of Lordaeron Arena）战场模块实现文件
 *
 * 本文件实现了洛丹伦废墟竞技场的具体功能，包括：
 * - 竞技场对象的生成和管理
 * - 大门的控制逻辑（关闭、打开、移除）
 * - 增益效果的刷新
 * - 区域触发器的处理
 * - 世界状态的初始化
 *
 * 洛丹伦废墟竞技场是位于提瑞斯法林地的经典竞技场。
 */

#include "BattlegroundRL.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

/**
 * @brief 构造函数
 *
 * 初始化竞技场对象容器，预留足够的空间存储所有游戏对象
 * 包括2扇门和2个增益效果对象
 */
BattlegroundRL::BattlegroundRL()
{
    BgObjects.resize(BG_RL_OBJECT_MAX);
}

/**
 * @brief 战场更新实现
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 核心更新函数，在战场进行中每帧调用：
 * 1. 更新事件调度器，处理定时事件
 * 2. 执行到期的事件（如移除大门）
 *
 * 性能注意事项：
 * - 仅在STATUS_IN_PROGRESS状态下执行
 * - 使用while循环处理所有到期事件，确保不遗漏
 */
void BattlegroundRL::PostUpdateImpl(uint32 diff)
{
    // 仅在战斗进行中更新
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 更新事件调度器
    _events.Update(diff);

    // 处理所有到期的事件
    while (uint32 eventId = _events.ExecuteEvent())
    {
        switch (eventId)
        {
            case BG_RL_EVENT_REMOVE_DOORS:
                // 移除两扇大门
                // 移除而非仅仅打开，可以减少场景中的对象数量，优化性能
                for (uint32 i = BG_RL_OBJECT_DOOR_1; i <= BG_RL_OBJECT_DOOR_2; ++i)
                    DelObject(i);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 开始事件 - 关闭大门
 *
 * 在竞技场准备阶段生成所有2扇门
 * 这些门会阻止玩家在比赛正式开始前离开起始区域
 *
 * 调用时机：战场状态变为STATUS_WAIT_JOIN时
 */
void BattlegroundRL::StartingEventCloseDoors()
{
    // 生成所有2扇门
    for (uint32 i = BG_RL_OBJECT_DOOR_1; i <= BG_RL_OBJECT_DOOR_2; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
}

/**
 * @brief 开始事件 - 打开大门
 *
 * 比赛正式开始时执行：
 * 1. 打开两扇大门
 * 2. 安排5秒后移除大门的事件（优化性能）
 * 3. 刷新两个增益效果对象（60秒后刷新）
 *
 * 调用时机：战场状态变为STATUS_IN_PROGRESS时
 */
void BattlegroundRL::StartingEventOpenDoors()
{
    // 打开两扇大门
    for (uint32 i = BG_RL_OBJECT_DOOR_1; i <= BG_RL_OBJECT_DOOR_2; ++i)
        DoorOpen(i);

    // 安排5秒后移除大门的事件
    // 移除门可以减少场景中的对象数量，提高性能
    _events.ScheduleEvent(BG_RL_EVENT_REMOVE_DOORS, BG_RL_REMOVE_DOORS_TIMER);

    // 生成两个增益效果对象，60秒后刷新
    // 增益效果是竞技场中的重要战略资源
    for (uint32 i = BG_RL_OBJECT_BUFF_1; i <= BG_RL_OBJECT_BUFF_2; ++i)
        SpawnBGObject(i, 60);
}

/**
 * @brief 处理区域触发器
 * @param player 触发区域的玩家指针
 * @param trigger 触发器ID
 *
 * 处理玩家进入特定区域时的触发事件：
 * - 4696、4697：增益效果区域触发器（可能）
 * - 其他：调用基类处理
 *
 * 调用时机：玩家进入特定区域触发器时
 */
void BattlegroundRL::HandleAreaTrigger(Player* player, uint32 trigger)
{
    // 仅在战斗进行中处理区域触发
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 4696:                                          // 增益触发器？
        case 4697:                                          // 增益触发器？
            // 这些触发器可能与增益效果区域相关
            // 目前不执行特殊处理
            break;
        default:
            // 其他触发器由基类处理
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

/**
 * @brief 填充初始世界状态
 * @param packet 世界状态数据包引用
 *
 * 向客户端发送战场初始世界状态数据：
 * - 3002：控制洛丹伦废墟竞技场UI显示的世界状态
 *
 * 调用时机：玩家进入战场时
 */
void BattlegroundRL::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 设置洛丹伦废墟竞技场显示状态（1=显示）
    packet.Worldstates.emplace_back(3002, 1); // BATTELGROUND_RUINS_OF_LORDAERNON_SHOW

    // 调用基类填充通用竞技场世界状态
    Arena::FillInitialWorldStates(packet);
}

/**
 * @brief 设置战场
 * @return 成功返回true，失败返回false
 *
 * 在地图中生成所有必要的游戏对象：
 * - 2扇门
 * - 2个增益效果对象
 *
 * 每个对象包括：
 * - 对象索引
 * - 对象模板ID
 * - 位置坐标（x, y, z）
 * - 朝向（四元数表示）
 * - 重生时间
 *
 * 调用时机：战场初始化时
 *
 * @note 如果任何对象生成失败，会记录错误日志并返回false
 */
bool BattlegroundRL::SetupBattleground()
{
    // 生成大门
    // 两扇门会在比赛开始时打开并稍后移除
    if (!AddObject(BG_RL_OBJECT_DOOR_1, BG_RL_OBJECT_TYPE_DOOR_1, 1293.561f, 1601.938f, 31.60557f, -1.457349f, 0, 0, -0.6658813f, 0.7460576f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RL_OBJECT_DOOR_2, BG_RL_OBJECT_TYPE_DOOR_2, 1278.648f, 1730.557f, 31.60557f, 1.684245f, 0, 0, 0.7460582f, 0.6658807f, RESPAWN_IMMEDIATELY)
    // 生成增益效果对象
    // 这些增益效果会在被拾取后120秒重生
        || !AddObject(BG_RL_OBJECT_BUFF_1, BG_RL_OBJECT_TYPE_BUFF_1, 1328.719971f, 1632.719971f, 36.730400f, -1.448624f, 0, 0, 0.6626201f, -0.7489557f, 120)
        || !AddObject(BG_RL_OBJECT_BUFF_2, BG_RL_OBJECT_TYPE_BUFF_2, 1243.300049f, 1699.170044f, 34.872601f, -0.06981307f, 0, 0, 0.03489945f, -0.9993908f, 120))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundRL: Failed to spawn some object!");
        return false;
    }

    return true;
}
