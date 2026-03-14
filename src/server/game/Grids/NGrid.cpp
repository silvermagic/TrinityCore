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
 * @file NGrid.cpp
 * @brief 网格系统实现文件
 *
 * 本文件实现了网格系统的核心类和模板实例化:
 * - GridInfo: 网格信息类，管理网格状态和计时器
 * - Grid: 基础网格类，存储游戏对象
 * - NGrid: 扩展网格类，封装NxN个单元格
 *
 * 网格系统架构:
 * - 地图被划分为64x64个网格(NGrid)
 * - 每个网格包含8x8个单元格(Cell/Grid)
 * - 每个单元格存储不同类型的游戏对象
 *
 * 性能特点:
 * - 使用模板特化减少运行时开销
 * - 网格加载/卸载基于玩家位置和活动
 * - 支持延迟卸载以优化内存管理
 *
 * @see NGrid.h
 * @see Grid.h
 * @see GridDefines.h
 */

#include "NGrid.h"
#include "GridDefines.h"
#include "Random.h"

/**
 * @brief GridInfo 默认构造函数
 *
 * 初始化网格信息，创建一个立即可卸载的网格。
 *
 * 初始化内容:
 * - 卸载计时器设为0，表示立即可以卸载
 * - 可见性更新计时器使用随机初始相位，避免所有网格同时更新
 * - 所有卸载锁初始为解锁状态
 *
 * 使用场景:
 * - 创建临时网格
 * - 创建不需要持久化的网格
 *
 * @note vis_Update 使用随机初始化是为了分散网格更新负载，避免性能尖峰
 */
GridInfo::GridInfo() : i_timer(0), vis_Update(0, irand(0, DEFAULT_VISIBILITY_NOTIFY_PERIOD)),
    i_unloadActiveLockCount(0), i_unloadExplicitLock(false), i_unloadReferenceLock(false)
{
}

/**
 * @brief GridInfo 带过期时间的构造函数
 *
 * 创建一个具有指定过期时间的网格信息对象。
 *
 * @param expiry 网格过期时间(秒)，即网格在无活动后多久可以卸载
 * @param unload 是否允许自动卸载，默认为true
 *               - true: 网格可以在过期后自动卸载
 *               - false: 显式锁定网格，防止自动卸载
 *
 * 初始化逻辑:
 * - 卸载计时器设置为 expiry 指定的值
 * - 可见性更新计时器使用随机初始相位
 * - 如果 unload=false，则显式锁设置为true，防止网格被卸载
 *
 * 使用场景:
 * - 创建需要持久化的网格(如重要区域的网格)
 * - 创建实例地图的网格(需要保持加载状态)
 * - 根据配置设置不同网格的过期时间
 *
 * @note 配置中的 GridUnload 和 GridCleanUpDelay 会影响此构造函数的参数
 */
GridInfo::GridInfo(time_t expiry, bool unload /*= true */) : i_timer(expiry), vis_Update(0, irand(0, DEFAULT_VISIBILITY_NOTIFY_PERIOD)),
    i_unloadActiveLockCount(0), i_unloadExplicitLock(!unload), i_unloadReferenceLock(false)
{
}

/**
 * @brief Grid 模板类的显式实例化
 *
 * 显式实例化 Grid 类模板，用于存储玩家和游戏对象。
 *
 * 模板参数说明:
 * - Player: 活动对象类型，玩家会触发网格加载
 * - AllWorldObjectTypes: 世界对象类型列表，包括 Player, Creature, GameObject 等
 * - AllGridObjectTypes: 网格对象类型列表，包括 Creature, GameObject, DynamicObject 等
 *
 * 显式实例化的好处:
 * - 减少编译时间和代码膨胀
 * - 将模板代码放在 .cpp 文件而非头文件
 * - 确保所有需要的模板特化都被正确编译
 */
template class Grid<Player, AllWorldObjectTypes, AllGridObjectTypes>;

/**
 * @brief NGrid 模板类的显式实例化
 *
 * 显式实例化 NGrid 类模板，创建标准的8x8网格。
 *
 * 模板参数说明:
 * - MAX_NUMBER_OF_CELLS: 每个维度的单元格数量(通常为8)
 * - Player: 活动对象类型
 * - AllWorldObjectTypes: 世界对象类型列表
 * - AllGridObjectTypes: 网格对象类型列表
 *
 * 网格结构:
 * - 每个地图被划分为64x64个网格(NGrid)
 * - 每个网格包含8x8=64个单元格(Cell)
 * - 每个单元格存储不同类型的游戏对象
 *
 * @note 这是游戏中使用的标准网格类型
 */
template class NGrid<MAX_NUMBER_OF_CELLS, Player, AllWorldObjectTypes, AllGridObjectTypes>;

/**
 * @brief TypeMapContainer 模板类的显式实例化 - 网格对象类型
 *
 * 显式实例化网格对象类型容器，用于类型安全的对象存储。
 *
 * AllGridObjectTypes 包括:
 * - Creature: 生物(包括NPC和怪物)
 * - GameObject: 游戏对象(如箱子、门等)
 * - DynamicObject: 动态对象(如法术效果区域)
 * - Corpse: 尸体
 * - AreaTrigger: 区域触发器
 * - SceneObject: 场景对象
 * - Conversation: 对话
 *
 * @note TC_GAME_API 宏确保符号在动态链接库中正确导出
 */
template class TC_GAME_API TypeMapContainer<AllGridObjectTypes>;

/**
 * @brief TypeMapContainer 模板类的显式实例化 - 世界对象类型
 *
 * 显式实例化世界对象类型容器，用于类型安全的对象存储。
 *
 * AllWorldObjectTypes 包括:
 * - Player: 玩家(活动对象)
 * - AllGridObjectTypes: 所有网格对象类型
 *
 * 设计模式:
 * - 使用 TypeMapContainer 实现类型安全的异构容器
 * - 每种对象类型存储在独立的容器中
 * - 支持按类型遍历对象
 *
 * @note 世界对象类型包含玩家，玩家是触发网格加载的活动对象
 */
template class TC_GAME_API TypeMapContainer<AllWorldObjectTypes>;
