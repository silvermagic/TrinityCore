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
 * @file MovementGenerator.cpp
 * @brief 移动生成器基类实现模块
 *
 * 本模块实现了移动生成器的基类方法和工厂类。
 * 提供移动生成器的通用功能,包括调试信息输出和对象创建。
 */

#include "MovementGenerator.h"
#include "Creature.h"
#include "IdleMovementGenerator.h"
#include "MovementDefines.h"
#include "PathGenerator.h"
#include "RandomMovementGenerator.h"
#include "UnitAI.h"
#include "WaypointMovementGenerator.h"

/**
 * @brief 移动生成器析构函数
 *
 * 虚析构函数确保派生类能够正确释放资源。
 */
MovementGenerator::~MovementGenerator() { }

/**
 * @brief 获取调试信息
 * @return 包含移动生成器状态信息的字符串
 *
 * 用于日志记录和调试,输出移动生成器的关键属性。
 */
std::string MovementGenerator::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << std::boolalpha
        << "Mode: " << std::to_string(Mode)
        << " Priority: " << std::to_string(Priority)
        << " Flags: " << Flags
        << " BaseUniteState: " << BaseUnitState;
    return sstr.str();
}

/**
 * @brief 空闲移动工厂构造函数
 *
 * 注册空闲移动生成器类型到工厂系统
 */
IdleMovementFactory::IdleMovementFactory() : MovementGeneratorCreator(IDLE_MOTION_TYPE) { }

/**
 * @brief 创建空闲移动生成器
 * @param object 关联的单位(未使用)
 * @return 空闲移动生成器单例
 *
 * 返回单例实例以节省内存,因为空闲行为不需要实例特定的数据。
 */
MovementGenerator* IdleMovementFactory::Create(Unit* /*object*/) const
{
    static IdleMovementGenerator instance;
    return &instance;
}

/**
 * @brief 随机移动工厂构造函数
 *
 * 注册随机移动生成器类型到工厂系统
 */
RandomMovementFactory::RandomMovementFactory() : MovementGeneratorCreator(RANDOM_MOTION_TYPE) { }

/**
 * @brief 创建随机移动生成器
 * @param object 关联的单位(未使用)
 * @return 新创建的随机移动生成器实例
 *
 * 为生物创建随机巡逻行为的移动生成器。
 */
MovementGenerator* RandomMovementFactory::Create(Unit* /*object*/) const
{
    return new RandomMovementGenerator<Creature>();
}

/**
 * @brief 路径点移动工厂构造函数
 *
 * 注册路径点移动生成器类型到工厂系统
 */
WaypointMovementFactory::WaypointMovementFactory() : MovementGeneratorCreator(WAYPOINT_MOTION_TYPE) { }

/**
 * @brief 创建路径点移动生成器
 * @param object 关联的单位(未使用)
 * @return 新创建的路径点移动生成器实例
 *
 * 为生物创建沿预设路径点移动的行为生成器。
 */
MovementGenerator* WaypointMovementFactory::Create(Unit* /*object*/) const
{
    return new WaypointMovementGenerator<Creature>();
}
