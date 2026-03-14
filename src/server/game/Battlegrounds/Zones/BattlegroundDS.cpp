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
 * @file BattlegroundDS.cpp
 * @brief 达拉然下水道竞技场（Dalaran Sewers Arena）实现文件
 *
 * 本文件实现了达拉然下水道竞技场的核心逻辑，包括：
 * - 竞技场初始化和设置
 * - 大门控制
 * - 瀑布周期控制（警告、激活、关闭、击退）
 * - 管道击退机制
 * - 增益buff刷新
 *
 * 达拉然下水道竞技场是一个2v2/3v3/5v5的竞技场，特色是中央有周期性出现的瀑布，
 * 会阻挡视线和移动，并将玩家击退。
 */

#include "BattlegroundDS.h"
#include "Creature.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

/**
 * @brief 构造函数
 *
 * 初始化竞技场成员变量：
 * - 设置游戏对象和生物容器大小
 * - 初始化管道击退计时器和计数器
 *
 * 调用时机：竞技场创建时
 */
BattlegroundDS::BattlegroundDS()
{
    BgObjects.resize(BG_DS_OBJECT_MAX);  // 调整游戏对象容器大小
    BgCreatures.resize(BG_DS_NPC_MAX);   // 调整生物容器大小

    _pipeKnockBackTimer = 0;    // 管道击退计时器初始化为0
    _pipeKnockBackCount = 0;    // 管道击退计数器初始化为0
}

/**
 * @brief 竞技场更新实现
 * @param diff 时间差（毫秒）
 *
 * 每帧调用，处理：
 * 1. 瀑布周期事件：
 *    - 警告阶段：水流开始出现（5秒）
 *    - 激活阶段：阻挡视线和移动，定期击退（30秒）
 *    - 关闭阶段：移除阻挡和视觉效果
 *    - 循环：30-60秒后再次出现
 * 2. 管道击退计时：
 *    - 竞技场开始后，定期将管道中的玩家推出
 *
 * 调用时机：每帧更新时由Battleground::Update调用
 * 性能注意：此函数每帧调用，需要优化处理
 */
void BattlegroundDS::PostUpdateImpl(uint32 diff)
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
            case BG_DS_EVENT_WATERFALL_WARNING:
                // 瀑布警告阶段：添加水流视觉效果（5秒）
                DoorClose(BG_DS_OBJECT_WATER_2);
                _events.ScheduleEvent(BG_DS_EVENT_WATERFALL_ON, BG_DS_WATERFALL_WARNING_DURATION);
                break;
            case BG_DS_EVENT_WATERFALL_ON:
                // 瀑布激活阶段：激活碰撞，启动击退定时器（30秒）
                DoorClose(BG_DS_OBJECT_WATER_1);  // 激活碰撞阻挡
                _events.ScheduleEvent(BG_DS_EVENT_WATERFALL_OFF, BG_DS_WATERFALL_DURATION);
                _events.ScheduleEvent(BG_DS_EVENT_WATERFALL_KNOCKBACK, BG_DS_WATERFALL_KNOCKBACK_TIMER);
                break;
            case BG_DS_EVENT_WATERFALL_OFF:
                // 瀑布关闭阶段：移除碰撞和视觉效果
                DoorOpen(BG_DS_OBJECT_WATER_1);  // 关闭碰撞阻挡
                DoorOpen(BG_DS_OBJECT_WATER_2);  // 关闭视觉效果
                _events.CancelEvent(BG_DS_EVENT_WATERFALL_KNOCKBACK);  // 取消击退事件
                // 安排下次瀑布出现（30-60秒后）
                _events.ScheduleEvent(BG_DS_EVENT_WATERFALL_WARNING, BG_DS_WATERFALL_TIMER_MIN, BG_DS_WATERFALL_TIMER_MAX);
                break;
            case BG_DS_EVENT_WATERFALL_KNOCKBACK:
                // 瀑布击退：在瀑布激活期间定期击退玩家
                if (Creature* waterSpout = GetBGCreature(BG_DS_NPC_WATERFALL_KNOCKBACK))
                    waterSpout->CastSpell(waterSpout, BG_DS_SPELL_WATER_SPOUT, true);
                // 继续安排下次击退
                _events.ScheduleEvent(eventId, BG_DS_WATERFALL_KNOCKBACK_TIMER);
                break;
            case BG_DS_EVENT_PIPE_KNOCKBACK:
                // 管道击退：将管道中的玩家推出
                for (uint32 i = BG_DS_NPC_PIPE_KNOCKBACK_1; i <= BG_DS_NPC_PIPE_KNOCKBACK_2; ++i)
                    if (Creature* waterSpout = GetBGCreature(i))
                        waterSpout->CastSpell(waterSpout, BG_DS_SPELL_FLUSH, true);
                break;
        }
    }

    // 处理管道击退计时（在竞技场开始后的前几次击退）
    if (_pipeKnockBackCount < BG_DS_PIPE_KNOCKBACK_TOTAL_COUNT)
    {
        if (_pipeKnockBackTimer < diff)
        {
            // 计时结束，执行管道击退
            for (uint32 i = BG_DS_NPC_PIPE_KNOCKBACK_1; i <= BG_DS_NPC_PIPE_KNOCKBACK_2; ++i)
                if (Creature* waterSpout = GetBGCreature(i))
                    waterSpout->CastSpell(waterSpout, BG_DS_SPELL_FLUSH, true);

            ++_pipeKnockBackCount;  // 增加击退计数
            _pipeKnockBackTimer = BG_DS_PIPE_KNOCKBACK_DELAY;  // 重置计时器
        }
        else
            _pipeKnockBackTimer -= diff;  // 减少计时器
    }
}

/**
 * @brief 开始事件：关闭大门
 *
 * 在竞技场开始前的准备阶段调用：
 * 1. 生成所有大门
 *
 * 调用时机：竞技场初始化时，玩家进入准备阶段
 */
void BattlegroundDS::StartingEventCloseDoors()
{
    // 生成所有大门
    for (uint32 i = BG_DS_OBJECT_DOOR_1; i <= BG_DS_OBJECT_DOOR_2; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
}

/**
 * @brief 开始事件：打开大门
 *
 * 竞技场正式开始时调用：
 * 1. 打开所有大门
 * 2. 生成增益buff（60秒后刷新）
 * 3. 启动瀑布定时器（30-60秒后出现）
 * 4. 初始化管道击退计时器
 * 5. 生成瀑布视觉效果
 * 6. 关闭瀑布碰撞阻挡
 * 7. 移除术士恶魔传送门效果（防止玩家在准备阶段设置传送门）
 *
 * 调用时机：竞技场准备时间结束，战斗正式开始
 */
void BattlegroundDS::StartingEventOpenDoors()
{
    // 打开所有大门
    for (uint32 i = BG_DS_OBJECT_DOOR_1; i <= BG_DS_OBJECT_DOOR_2; ++i)
        DoorOpen(i);

    // 生成增益buff（60秒后刷新）
    for (uint32 i = BG_DS_OBJECT_BUFF_1; i <= BG_DS_OBJECT_BUFF_2; ++i)
        SpawnBGObject(i, 60);

    // 启动瀑布定时器（30-60秒后出现）
    _events.ScheduleEvent(BG_DS_EVENT_WATERFALL_WARNING, BG_DS_WATERFALL_TIMER_MIN, BG_DS_WATERFALL_TIMER_MAX);

    // 初始化管道击退计时器和计数器
    _pipeKnockBackCount = 0;
    _pipeKnockBackTimer = BG_DS_PIPE_KNOCKBACK_FIRST_DELAY;

    // 生成瀑布视觉效果
    SpawnBGObject(BG_DS_OBJECT_WATER_2, RESPAWN_IMMEDIATELY);

    // 关闭瀑布碰撞阻挡（确保开始时没有阻挡）
    DoorOpen(BG_DS_OBJECT_WATER_1);
    DoorOpen(BG_DS_OBJECT_WATER_2);

    // 移除术士恶魔传送门效果（防止玩家在准备阶段设置传送门获得不公平优势）
    for (BattlegroundPlayerMap::const_iterator itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
        if (Player* player = _GetPlayer(itr, "BattlegroundDS::StartingEventOpenDoors"))
            player->RemoveAurasDueToSpell(SPELL_WARL_DEMONIC_CIRCLE);
}

/**
 * @brief 处理区域触发器
 * @param player 触发玩家
 * @param trigger 触发器ID
 *
 * 处理玩家进入特定区域触发器的事件：
 * - 5347, 5348: 管道区域触发器
 *   当玩家返回管道时，移除恶魔传送门效果
 *   如果管道击退已完成，重置击退计数器以便再次击退玩家
 *
 * 调用时机：玩家进入区域触发器范围时
 */
void BattlegroundDS::HandleAreaTrigger(Player* player, uint32 trigger)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 5347:
        case 5348:
            // 管道区域：移除恶魔传送门效果
            player->RemoveAurasDueToSpell(SPELL_WARL_DEMONIC_CIRCLE);

            // 如果管道击退已完成，重置击退计数器
            // 这样玩家再次进入管道时会被再次击退
            if (_pipeKnockBackCount >= BG_DS_PIPE_KNOCKBACK_TOTAL_COUNT)
                _pipeKnockBackCount = 0;
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
 * - 显示存活玩家数量UI
 * - 调用父类方法填充基础世界状态
 *
 * 调用时机：玩家进入竞技场时，发送初始化数据
 */
void BattlegroundDS::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 显示存活玩家数量UI
    packet.Worldstates.emplace_back(3610, 1);  // ARENA_WORLD_STATE_ALIVE_PLAYERS_SHOW

    // 调用父类方法填充基础世界状态
    Arena::FillInitialWorldStates(packet);
}

/**
 * @brief 设置竞技场
 * @return 设置成功返回true，否则返回false
 *
 * 生成竞技场中的所有游戏对象和生物：
 * 1. 生成2个大门
 * 2. 生成瀑布对象（碰撞体和视觉效果）
 * 3. 生成2个增益buff
 * 4. 生成3个击退生物（瀑布击退 + 2个管道击退）
 *
 * 调用时机：竞技场创建时
 * 性能注意：此函数只在竞技场创建时调用一次，性能开销较小
 */
bool BattlegroundDS::SetupBattleground()
{
    // 生成大门、瀑布、增益buff和击退生物
    if (!AddObject(BG_DS_OBJECT_DOOR_1, BG_DS_OBJECT_TYPE_DOOR_1, 1350.95f, 817.2f, 20.8096f, 3.15f, 0, 0, 0.99627f, 0.0862864f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_DS_OBJECT_DOOR_2, BG_DS_OBJECT_TYPE_DOOR_2, 1232.65f, 764.913f, 20.0729f, 6.3f, 0, 0, 0.0310211f, -0.999519f, RESPAWN_IMMEDIATELY)
        // 瀑布对象（碰撞体和视觉效果，120秒后刷新）
        || !AddObject(BG_DS_OBJECT_WATER_1, BG_DS_OBJECT_TYPE_WATER_1, 1291.56f, 790.837f, 7.1f, 3.14238f, 0, 0, 0.694215f, -0.719768f, 120)
        || !AddObject(BG_DS_OBJECT_WATER_2, BG_DS_OBJECT_TYPE_WATER_2, 1291.56f, 790.837f, 7.1f, 3.14238f, 0, 0, 0.694215f, -0.719768f, 120)
        // 增益buff（120秒后刷新）
        || !AddObject(BG_DS_OBJECT_BUFF_1, BG_DS_OBJECT_TYPE_BUFF_1, 1291.7f, 813.424f, 7.11472f, 4.64562f, 0, 0, 0.730314f, -0.683111f, 120)
        || !AddObject(BG_DS_OBJECT_BUFF_2, BG_DS_OBJECT_TYPE_BUFF_2, 1291.7f, 768.911f, 7.11472f, 1.55194f, 0, 0, 0.700409f, 0.713742f, 120)
        // 击退生物
        || !AddCreature(BG_DS_NPC_TYPE_WATER_SPOUT, BG_DS_NPC_WATERFALL_KNOCKBACK, 1292.587f, 790.2205f, 7.19796f, 3.054326f, TEAM_NEUTRAL, RESPAWN_IMMEDIATELY)
        || !AddCreature(BG_DS_NPC_TYPE_WATER_SPOUT, BG_DS_NPC_PIPE_KNOCKBACK_1, 1369.977f, 817.2882f, 16.08718f, 3.106686f, TEAM_NEUTRAL, RESPAWN_IMMEDIATELY)
        || !AddCreature(BG_DS_NPC_TYPE_WATER_SPOUT, BG_DS_NPC_PIPE_KNOCKBACK_2, 1212.833f, 765.3871f, 16.09484f, 0.0f, TEAM_NEUTRAL, RESPAWN_IMMEDIATELY))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundDS: Failed to spawn some object!");
        return false;
    }

    return true;
}
