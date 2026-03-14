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
 * @file CreatureAISelector.h
 * @brief AI选择器头文件
 *
 * 本文件定义了AI和移动生成器的选择接口。AI选择器负责根据生物或游戏对象的
 * 配置信息（如AI名称、脚本名称）和权限值，选择并创建最合适的AI实例。
 * 选择过程采用多级优先级机制，确保生物能够获得最适合其行为的AI。
 */

#ifndef TRINITY_CREATUREAISELECTOR_H
#define TRINITY_CREATUREAISELECTOR_H

class CreatureAI;
class Creature;
class MovementGenerator;
class Unit;
class GameObjectAI;
class GameObject;

/**
 * @namespace FactorySelector
 * @brief 工厂选择器命名空间
 *
 * 提供AI工厂和移动生成器工厂的选择功能。该命名空间包含的函数根据
 *不同的选择策略（数据库配置、脚本、权限值）为生物和游戏对象选择
 *最合适的AI或移动生成器。
 */
namespace FactorySelector
{
    /**
     * @brief 为生物选择合适的AI
     *
     * 根据多重优先级规则为生物选择AI：
     * 1. 如果是宠物，强制使用PetAI
     * 2. 如果有脚本名称，尝试从脚本系统获取AI
     * 3. 如果有AI名称配置，从注册表获取对应AI工厂
     * 4. 否则根据权限值（Permit）选择最合适的AI
     *
     * @param creature 需要AI的生物实例
     * @return 返回选中的AI实例，调用者负责管理内存
     *
     * @调用时机 生物创建或重置时调用
     * @性能注意事项 涉及字符串查找和动态转换，但频率不高
     * @note 宠物AI优先级最高，确保宠物行为正确
     * @warning 返回的AI实例由生物对象管理，不要手动删除
     */
    TC_GAME_API CreatureAI* SelectAI(Creature* creature);

    /**
     * @brief 为单位选择移动生成器
     *
     * 根据单位的默认移动类型选择对应的移动生成器。
     * 对于生物，会检查是否有控制者（如玩家）来决定移动类型。
     *
     * @param unit 需要移动生成器的单位实例
     * @return 返回选中的移动生成器实例，调用者负责管理内存
     *
     * @调用时机 单位初始化移动系统时调用
     * @性能注意事项 简单的类型查找，性能开销极小
     * @warning 返回的移动生成器实例由单位对象管理，不要手动删除
     */
    TC_GAME_API MovementGenerator* SelectMovementGenerator(Unit* unit);

    /**
     * @brief 为游戏对象选择合适的AI
     *
     * 根据以下优先级为游戏对象选择AI：
     * 1. 如果有脚本名称，优先从脚本系统获取AI
     * 2. 否则根据AI名称配置或权限值选择AI
     *
     * @param go 需要AI的游戏对象实例
     * @return 返回选中的游戏对象AI实例，调用者负责管理内存
     *
     * @调用时机 游戏对象创建或初始化时调用
     * @性能注意事项 涉及字符串查找和动态转换，但频率不高
     * @warning 返回的AI实例由游戏对象管理，不要手动删除
     */
    TC_GAME_API GameObjectAI* SelectGameObjectAI(GameObject* go);
}

#endif
