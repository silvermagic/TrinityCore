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
 * @file BattlegroundRV.cpp
 * @brief 环形竞技场（Ring of Valor）战场实现
 *
 * 本文件实现了环形竞技场的核心游戏逻辑，包括：
 * - 动态地形系统（柱子升降、火墙开关）
 * - 竞技场状态机管理
 * - 游戏对象的创建和更新
 *
 * 设计要点：
 * 1. 状态机驱动：使用_timer和_state实现状态流转
 * 2. 周期性事件：柱子每25秒切换一次状态
 * 3. 视觉与碰撞分离：柱子视觉对象和碰撞对象分开管理
 */

#include "BattlegroundRV.h"
#include "GameObject.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

/**
 * @brief 构造函数
 *
 * 初始化环形竞技场的成员变量：
 * - 分配游戏对象数组空间
 * - 初始化计时器和状态为0
 * - 设置柱子碰撞状态为false
 */
BattlegroundRV::BattlegroundRV()
{
    // 分配游戏对象容器大小
    BgObjects.resize(BG_RV_OBJECT_MAX);

    // 初始化状态变量
    _timer = 0;
    _state = 0;
    _pillarCollision = false;
}

/**
 * @brief 竞技场更新实现
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 核心更新逻辑，实现竞技场的状态机：
 *
 * 状态流转：
 * 1. BG_RV_STATE_OPEN_FENCES（打开火墙）：
 *    - 战斗开始时打开火墙
 *    - 持续5秒后进入下一状态
 *
 * 2. BG_RV_STATE_CLOSE_FIRE（关闭火墙）：
 *    - 关闭火墙视觉效果
 *    - 持续20秒后进入柱子切换状态
 *
 * 3. BG_RV_STATE_SWITCH_PILLARS（切换柱子）：
 *    - 每25秒切换一次柱子状态
 *    - 循环执行
 *
 * 调用时机：每帧调用（STATUS_IN_PROGRESS状态时）
 */
void BattlegroundRV::PostUpdateImpl(uint32 diff)
{
    // 只在战斗进行中更新
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 计时器逻辑：当前状态到期时切换到下一状态
    if (_timer < diff)
    {
        switch (_state)
        {
            case BG_RV_STATE_OPEN_FENCES:
                // 开场阶段：打开火墙（仅游戏开始时执行）
                // 打开火墙门，允许玩家通过
                for (uint8 i = BG_RV_OBJECT_FIRE_1; i <= BG_RV_OBJECT_FIREDOOR_2; ++i)
                    DoorOpen(i);
                // 设置5秒后关闭火墙
                _timer = BG_RV_CLOSE_FIRE_TIMER;
                _state = BG_RV_STATE_CLOSE_FIRE;
                break;
            case BG_RV_STATE_CLOSE_FIRE:
                // 关闭火墙：火墙视觉效果消失
                // 关闭所有火墙相关对象
                for (uint8 i = BG_RV_OBJECT_FIRE_1; i <= BG_RV_OBJECT_FIREDOOR_2; ++i)
                    DoorClose(i);
                // 火墙关闭后20秒开始切换柱子
                _timer = BG_RV_FIRE_TO_PILLAR_TIMER;
                _state = BG_RV_STATE_SWITCH_PILLARS;
                break;
            case BG_RV_STATE_SWITCH_PILLARS:
                // 柱子切换状态：周期性切换柱子升起/降下
                // 切换柱子碰撞状态（升起或降下）
                TogglePillarCollision();
                // 每25秒切换一次
                _timer = BG_RV_PILLAR_SWITCH_TIMER;
                break;
        }
    }
    else
        _timer -= diff;  // 计时器递减
}

/**
 * @brief 开门事件处理
 *
 * 当竞技场战斗开始时执行：
 * 1. 设置增益效果刷新时间（90秒后刷新）
 * 2. 打开升降梯，让玩家进入竞技场
 * 3. 初始化状态机：
 *    - 设置初始状态为打开火墙
 *    - 设置首次触发计时器（约20秒）
 * 4. 初始化柱子系统（首次调用使柱子降下）
 *
 * 调用时机：竞技场准备阶段结束，战斗正式开始时
 */
void BattlegroundRV::StartingEventOpenDoors()
{
    // 增益效果刷新：90秒后刷新第一个增益
    SpawnBGObject(BG_RV_OBJECT_BUFF_1, 90);
    // 增益效果刷新：90秒后刷新第二个增益
    SpawnBGObject(BG_RV_OBJECT_BUFF_2, 90);

    // 升降梯：打开两个升降梯，允许玩家进入
    DoorOpen(BG_RV_OBJECT_ELEVATOR_1);
    DoorOpen(BG_RV_OBJECT_ELEVATOR_2);

    // 初始化状态机：进入打开火墙状态
    _state = BG_RV_STATE_OPEN_FENCES;
    _timer = BG_RV_FIRST_TIMER;  // 约20秒后首次触发

    // 初始化柱子状态
    // 注意：_pillarCollision初始设为true，TogglePillarCollision会将其翻转为false
    // 这样首次调用会使柱子降下（竞技场初始状态）
    _pillarCollision = true;
    TogglePillarCollision();
}

/**
 * @brief 处理区域触发事件
 * @param player 触发区域的玩家
 * @param trigger 区域触发器ID
 *
 * 处理玩家进入特定区域触发器时的事件：
 * - 5224, 5226, 5473, 5474：火墙相关区域（已在3.2.0版本移除）
 * - 其他：调用基类处理
 *
 * 调用时机：玩家进入区域触发器范围时
 * 注意：火墙区域触发器已废弃，此处仅作保留
 */
void BattlegroundRV::HandleAreaTrigger(Player* player, uint32 trigger)
{
    // 只在战斗进行中处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 5224:  // 火墙区域触发器
        case 5226:
        // 注意：火墙在3.2.0版本已被移除
        case 5473:
        case 5474:
            break;  // 不做任何处理
        default:
            // 其他区域触发器由基类处理
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

/**
 * @brief 填充初始世界状态数据
 * @param packet 世界状态数据包
 *
 * 向客户端发送竞技场的初始世界状态信息：
 * - 发送竞技场特定的世界状态ID
 * - 调用基类发送竞技场通用状态
 *
 * 调用时机：玩家进入竞技场时，初始化客户端UI
 */
void BattlegroundRV::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 发送环形竞技场的特定世界状态
    packet.Worldstates.emplace_back(BG_RV_WORLD_STATE, 1);

    // 调用基类发送竞技场通用世界状态
    Arena::FillInitialWorldStates(packet);
}

/**
 * @brief 设置竞技场
 * @return 设置成功返回true，否则返回false
 *
 * 创建竞技场中的所有游戏对象，包括：
 * 1. 升降梯（2个）：玩家入场通道
 * 2. 增益效果点（2个）：提供战斗增益
 * 3. 火墙系统（4个对象）：火墙及火墙门
 * 4. 齿轮（2个）：装饰性对象
 * 5. 滑轮（2个）：装饰性对象
 * 6. 柱子视觉对象（4个）：不同图标的柱子
 * 7. 柱子碰撞体（4个）：实际的碰撞阻挡
 *
 * 所有对象立即刷新（RESPAWN_IMMEDIATELY）
 *
 * 调用时机：竞技场初始化时
 * 性能注意：涉及20个游戏对象的创建，建议在非热点路径调用
 */
bool BattlegroundRV::SetupBattleground()
{
    // 升降梯：创建两个升降梯，分别位于竞技场南北两端
    if (!AddObject(BG_RV_OBJECT_ELEVATOR_1, BG_RV_OBJECT_TYPE_ELEVATOR_1, 763.536377f, -294.535767f, 0.505383f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_ELEVATOR_2, BG_RV_OBJECT_TYPE_ELEVATOR_2, 763.506348f, -273.873352f, 0.505383f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
    // 增益效果：创建两个增益刷新点
        || !AddObject(BG_RV_OBJECT_BUFF_1, BG_RV_OBJECT_TYPE_BUFF_1, 735.551819f, -284.794678f, 28.276682f, 0.034906f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_BUFF_2, BG_RV_OBJECT_TYPE_BUFF_2, 791.224487f, -284.794464f, 28.276682f, 2.600535f, 0, 0, 0, RESPAWN_IMMEDIATELY)
    // 火墙系统：创建火墙和火墙门对象
        || !AddObject(BG_RV_OBJECT_FIRE_1, BG_RV_OBJECT_TYPE_FIRE_1, 743.543457f, -283.799469f, 28.286655f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_FIRE_2, BG_RV_OBJECT_TYPE_FIRE_2, 782.971802f, -283.799469f, 28.286655f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_FIREDOOR_1, BG_RV_OBJECT_TYPE_FIREDOOR_1, 743.711060f, -284.099609f, 27.542587f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_FIREDOOR_2, BG_RV_OBJECT_TYPE_FIREDOOR_2, 783.221252f, -284.133362f, 27.535686f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
    // 齿轮：装饰性对象，位于竞技场中央
        || !AddObject(BG_RV_OBJECT_GEAR_1, BG_RV_OBJECT_TYPE_GEAR_1, 763.664551f, -261.872986f, 26.686588f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_GEAR_2, BG_RV_OBJECT_TYPE_GEAR_2, 763.578979f, -306.146149f, 26.665222f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
    // 滑轮：装饰性对象，位于竞技场两侧
        || !AddObject(BG_RV_OBJECT_PULLEY_1, BG_RV_OBJECT_TYPE_PULLEY_1, 700.722290f, -283.990662f, 39.517582f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PULLEY_2, BG_RV_OBJECT_TYPE_PULLEY_2, 826.303833f, -283.996429f, 39.517582f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
    // 柱子视觉对象：创建4个柱子，各有不同图标（斧头、竞技场、闪电、象牙）
        || !AddObject(BG_RV_OBJECT_PILAR_1, BG_RV_OBJECT_TYPE_PILAR_1, 763.632385f, -306.162384f, 25.909504f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_2, BG_RV_OBJECT_TYPE_PILAR_2, 723.644287f, -284.493256f, 24.648525f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_3, BG_RV_OBJECT_TYPE_PILAR_3, 763.611145f, -261.856750f, 25.909504f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_4, BG_RV_OBJECT_TYPE_PILAR_4, 802.211609f, -284.493256f, 24.648525f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)

    // 柱子碰撞体：创建4个柱子的碰撞阻挡区域
        || !AddObject(BG_RV_OBJECT_PILAR_COLLISION_1, BG_RV_OBJECT_TYPE_PILAR_COLLISION_1, 763.632385f, -306.162384f, 30.639660f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_COLLISION_2, BG_RV_OBJECT_TYPE_PILAR_COLLISION_2, 723.644287f, -284.493256f, 32.382710f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_COLLISION_3, BG_RV_OBJECT_TYPE_PILAR_COLLISION_3, 763.611145f, -261.856750f, 30.639660f, 0.000000f, 0, 0, 0, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_RV_OBJECT_PILAR_COLLISION_4, BG_RV_OBJECT_TYPE_PILAR_COLLISION_4, 802.211609f, -284.493256f, 32.382710f, 3.141593f, 0, 0, 0, RESPAWN_IMMEDIATELY))
    {
        // 如果任何对象创建失败，记录错误日志
        TC_LOG_ERROR("sql.sql", "BatteGroundRV: Failed to spawn some object!");
        return false;
    }
    return true;
}

/**
 * @brief 切换柱子碰撞状态
 *
 * 切换竞技场柱子的升起/降下状态：
 * 1. 更新柱子视觉对象（PILAR_1, PILAR_3, GEAR_1, GEAR_2）
 *    - _pillarCollision为true时打开，false时关闭
 * 2. 更新柱子视觉对象（PILAR_2, PILAR_4, PULLEY_1, PULLEY_2）
 *    - 状态与上述相反，实现视觉互补效果
 * 3. 更新柱子碰撞体的状态
 *    - 设置碰撞体的GOState，控制是否阻挡玩家
 * 4. 向所有玩家同步更新
 *    - 发送游戏对象状态更新包给所有在场玩家
 *
 * 状态标记：
 * - _pillarCollision = true：柱子升起状态
 * - _pillarCollision = false：柱子降下状态
 *
 * 调用时机：每25秒自动调用一次（在BG_RV_STATE_SWITCH_PILLARS状态）
 * 性能注意：需要遍历所有玩家发送更新，玩家数量多时需关注性能
 */
void BattlegroundRV::TogglePillarCollision()
{
    // 第一组柱子：根据当前状态决定开启/关闭
    // 包括：柱子1、柱子3、齿轮1、齿轮2
    for (uint8 i = BG_RV_OBJECT_PILAR_1; i <= BG_RV_OBJECT_GEAR_2; ++i)
        _pillarCollision ? DoorOpen(i) : DoorClose(i);

    // 第二组柱子：状态与第一组相反，实现视觉互补
    // 包括：柱子2、柱子4、滑轮1、滑轮2
    for (uint8 i = BG_RV_OBJECT_PILAR_2; i <= BG_RV_OBJECT_PULLEY_2; ++i)
        _pillarCollision ? DoorClose(i) : DoorOpen(i);

    // 更新所有柱子相关对象的状态并向玩家同步
    for (uint8 i = BG_RV_OBJECT_PILAR_1; i <= BG_RV_OBJECT_PILAR_COLLISION_4; ++i)
    {
        if (GameObject* go = GetBGObject(i))
        {
            // 对于碰撞体对象，设置其游戏对象状态
            if (i >= BG_RV_OBJECT_PILAR_COLLISION_1)
            {
                // 根据游戏对象配置和当前状态决定GOState
                // 如果门默认开启(startOpen!=0)且当前_pillarCollision为true，则设为ACTIVE
                // 否则设为READY（关闭状态）
                GOState state = ((go->GetGOInfo()->door.startOpen != 0) == _pillarCollision) ? GO_STATE_ACTIVE : GO_STATE_READY;
                go->SetGoState(state);
            }

            // 向所有玩家发送更新包
            for (BattlegroundPlayerMap::const_iterator itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
                if (Player* player = ObjectAccessor::FindPlayer(itr->first))
                    go->SendUpdateToPlayer(player);
        }
    }

    // 翻转柱子碰撞状态标记，下次调用时切换到相反状态
    _pillarCollision = !_pillarCollision;
}
