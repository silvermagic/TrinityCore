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
 * @file ScheduledChangeAI.cpp
 * @brief 计划变更AI模块实现文件
 *
 * 本文件实现了ScheduledChangeAI类，这是一个特殊的AI类，用于处理
 * 需要切换AI类型的生物。该类提供一个最小化的构造函数实现，所有其他
 * 方法在头文件中已内联定义为空实现。
 *
 * 使用场景：
 * - 当生物需要动态切换AI时，作为临时占位符
 * - 防止AI切换过程中的竞态条件
 * - 确保在AI变更期间生物处于稳定状态
 */

#include "ScheduledChangeAI.h"

/**
 * @brief ScheduledChangeAI构造函数
 * @param creature 关联的生物对象指针
 *
 * 构造函数仅调用基类CreatureAI的构造函数，不执行额外的初始化操作。
 * 这确保了最小化的初始化开销，适合作为临时AI使用。
 *
 * 实现细节：
 * - 调用基类构造函数初始化生物关联
 * - 不分配额外资源
 * - 不执行任何游戏逻辑初始化
 *
 * 性能考虑：
 * - 极低的开销，适合频繁创建和销毁
 * - 不涉及内存分配或资源管理
 */
ScheduledChangeAI::ScheduledChangeAI(Creature* creature): CreatureAI(creature)
{ }
