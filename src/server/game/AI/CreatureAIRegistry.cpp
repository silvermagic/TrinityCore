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
 * @file CreatureAIRegistry.cpp
 * @brief AI注册系统实现文件
 *
 * 本文件实现了AI注册系统的初始化功能，负责在服务器启动时注册所有
 * 标准的生物AI、游戏对象AI和移动生成器工厂。通过工厂模式，系统可以
 * 根据名称动态创建相应的AI实例。
 */

#include "CreatureAIFactory.h"
#include "GameObjectAIFactory.h"

#include "CombatAI.h"
#include "GuardAI.h"
#include "PassiveAI.h"
#include "PetAI.h"
#include "ReactorAI.h"
#include "ScheduledChangeAI.h"
#include "SmartAI.h"
#include "TotemAI.h"

#include "MovementGenerator.h"

namespace AIRegistry
{
    /**
     * @brief 初始化AI注册系统
     *
     * 该函数创建并注册所有标准AI工厂和移动生成器工厂。每个工厂负责
     * 创建特定类型的AI实例。工厂创建后会自动注册到全局注册表中，
     * 可以通过名称或权限值进行查找。
     *
     * 注册的生物AI类型：
     * - NullCreatureAI: 空AI，不执行任何操作
     * - TriggerAI: 触发器AI，用于触发区域事件
     * - AggressorAI: 攻击型AI，主动攻击敌对目标
     * - ReactorAI: 反应型AI，受到攻击后反击
     * - PassiveAI: 被动AI，不主动攻击
     * - CritterAI: 小动物AI，随机移动并躲避战斗
     * - GuardAI: 卫兵AI，保护特定区域
     * - PetAI: 宠物AI，跟随主人并协助战斗（第二个模板参数false表示不作为默认AI）
     * - TotemAI: 图腾AI，图腾专用AI（第二个模板参数false表示不作为默认AI）
     * - CombatAI: 战斗AI，通用战斗行为
     * - ArcherAI: 弓箭手AI，远程攻击
     * - TurretAI: 炮塔AI，固定位置攻击
     * - VehicleAI: 载具AI，载具专用
     * - SmartAI: 智能AI，脚本化的高级AI
     * - ScheduledChangeAI: 定时切换AI（第二个模板参数false表示不作为默认AI）
     *
     * 注册的游戏对象AI类型：
     * - NullGameObjectAI: 空游戏对象AI
     * - GameObjectAI: 标准游戏对象AI
     * - SmartGameObjectAI: 智能游戏对象AI
     *
     * 注册的移动生成器类型：
     * - IdleMovement: 空闲移动，原地不动
     * - RandomMovement: 随机移动
     * - WaypointMovement: 路径点移动
     */
    void Initialize()
    {
        // 注册生物AI工厂
        // 基础AI类型
        (new CreatureAIFactory<NullCreatureAI>("NullCreatureAI"))->RegisterSelf();      // 空AI，用于占位或特殊场景
        (new CreatureAIFactory<TriggerAI>("TriggerAI"))->RegisterSelf();                // 触发器AI，用于事件触发
        (new CreatureAIFactory<AggressorAI>("AggressorAI"))->RegisterSelf();            // 攻击型AI，主动寻敌攻击
        (new CreatureAIFactory<ReactorAI>("ReactorAI"))->RegisterSelf();                // 反应型AI，被动反击
        (new CreatureAIFactory<PassiveAI>("PassiveAI"))->RegisterSelf();                // 被动AI，不参与战斗
        (new CreatureAIFactory<CritterAI>("CritterAI"))->RegisterSelf();                // 小动物AI，用于非战斗生物
        (new CreatureAIFactory<GuardAI>("GuardAI"))->RegisterSelf();                    // 卫兵AI，区域保护

        // 特殊AI类型（第二个模板参数false表示不作为默认AI）
        (new CreatureAIFactory<PetAI, false>("PetAI"))->RegisterSelf();                 // 宠物AI，由玩家控制
        (new CreatureAIFactory<TotemAI, false>("TotemAI"))->RegisterSelf();             // 图腾AI，图腾专用

        // 战斗相关AI
        (new CreatureAIFactory<CombatAI>("CombatAI"))->RegisterSelf();                  // 通用战斗AI
        (new CreatureAIFactory<ArcherAI>("ArcherAI"))->RegisterSelf();                  // 弓箭手AI，远程攻击
        (new CreatureAIFactory<TurretAI>("TurretAI"))->RegisterSelf();                  // 炮塔AI，固定防御
        (new CreatureAIFactory<VehicleAI>("VehicleAI"))->RegisterSelf();                // 载具AI

        // 高级AI
        (new CreatureAIFactory<SmartAI>("SmartAI"))->RegisterSelf();                    // 智能AI，支持脚本
        (new CreatureAIFactory<ScheduledChangeAI, false>("ScheduledChangeAI"))->RegisterSelf(); // 定时切换AI

        // 注册游戏对象AI工厂
        (new GameObjectAIFactory<NullGameObjectAI>("NullGameObjectAI"))->RegisterSelf();       // 空游戏对象AI
        (new GameObjectAIFactory<GameObjectAI>("GameObjectAI"))->RegisterSelf();               // 标准游戏对象AI
        (new GameObjectAIFactory<SmartGameObjectAI>("SmartGameObjectAI"))->RegisterSelf();     // 智能游戏对象AI

        // 注册移动生成器工厂
        (new IdleMovementFactory())->RegisterSelf();                                     // 空闲移动生成器
        (new RandomMovementFactory())->RegisterSelf();                                   // 随机移动生成器
        (new WaypointMovementFactory())->RegisterSelf();                                 // 路径点移动生成器
    }
}
