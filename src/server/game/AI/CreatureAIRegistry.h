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
 * @file CreatureAIRegistry.h
 * @brief AI注册系统头文件
 *
 * 本文件定义了AI注册系统的初始化接口。AI注册系统负责在服务器启动时
 * 注册所有可用的生物AI类型和游戏对象AI类型，以及移动生成器类型。
 * 这些注册的AI和移动生成器可以在运行时通过名称或类型进行查找和创建。
 */

#ifndef TRINITY_CREATUREAIREGISTRY_H
#define TRINITY_CREATUREAIREGISTRY_H

/**
 * @namespace AIRegistry
 * @brief AI注册命名空间
 *
 * 提供AI工厂的注册和初始化功能。该命名空间包含服务器启动时
 * 注册所有标准AI类型的初始化函数。
 */
namespace AIRegistry
{
    /**
     * @brief 初始化AI注册系统
     *
     * 在服务器启动时调用，注册所有标准的生物AI、游戏对象AI和移动生成器类型。
     * 该函数会创建并注册以下类型：
     * - 生物AI：NullCreatureAI, TriggerAI, AggressorAI, ReactorAI, PassiveAI,
     *           CritterAI, GuardAI, PetAI, TotemAI, CombatAI, ArcherAI,
     *           TurretAI, VehicleAI, SmartAI, ScheduledChangeAI
     * - 游戏对象AI：NullGameObjectAI, GameObjectAI, SmartGameObjectAI
     * - 移动生成器：IdleMovement, RandomMovement, WaypointMovement
     *
     * @调用时机 服务器启动时，在World::SetInitialWorldSettings()中调用
     * @性能注意事项 该函数只在服务器启动时调用一次，性能影响可忽略
     * @note 注册的AI工厂采用单例模式，内存由工厂注册表管理
     */
    void Initialize(void);
}
#endif
