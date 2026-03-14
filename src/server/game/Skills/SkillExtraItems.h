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
 * @file SkillExtraItems.h
 * @brief 技能额外物品和完美物品系统头文件
 *
 * 本模块实现了专业技能中的两个特殊机制：
 *
 * 1. 完美物品创建（Perfect Item Creation）：
 *    - 某些专业专精允许玩家在制造物品时有概率创建出"完美"版本
 *    - 完美物品通常是品质更高或属性更好的版本
 *    - 例如：珠宝加工的完美宝石切割
 *
 * 2. 额外物品创建（Extra Item Creation）：
 *    - 某些专业专精允许玩家在制造物品时获得额外数量
 *    - 可以一次制造出多个物品而不是通常的一个
 *    - 例如：药剂大师制作药剂时的额外产出
 *
 * 这两个机制都依赖于玩家是否拥有相应的专业专精技能。
 */

#ifndef TRINITY_SKILL_EXTRA_ITEMS_H
#define TRINITY_SKILL_EXTRA_ITEMS_H

#include "Common.h"

// predef classes used in functions
class Player;

/**
 * @brief 检查玩家是否可以创建完美物品
 *
 * 检查指定法术是否支持完美物品创建，以及玩家是否满足条件。
 * 如果可以创建，返回相关概率和物品信息。
 *
 * @param player 执行制造的玩家指针
 * @param spellId 制造法术的ID
 * @param[out] perfectCreateChance 输出参数，完美物品的创建概率（百分比）
 * @param[out] perfectItemType 输出参数，完美物品的物品模板ID
 *
 * @return 如果可以创建完美物品返回true，否则返回false
 *
 * @note 调用时机：玩家执行制造动作时，在计算产出前调用
 * @note 条件检查：玩家必须拥有 requiredSpecialization 指定的专精技能
 *
 * @example
 * @code
 * float chance;
 * uint32 perfectItemId;
 * if (CanCreatePerfectItem(player, spellId, chance, perfectItemId))
 * {
 *     // 有概率创建完美物品
 *     if (roll_chance_f(chance))
 *         // 创建完美物品
 * }
 * @endcode
 */
TC_GAME_API bool CanCreatePerfectItem(Player* player, uint32 spellId, float &perfectCreateChance, uint32 &perfectItemType);

/**
 * @brief 加载完美物品创建模板数据
 *
 * 从数据库表 skill_perfect_item_template 加载完美物品配置数据。
 * 该函数在服务器启动时调用。
 *
 * @note 调用时机：服务器启动时的世界初始化阶段
 * @note 支持热重载：可通过GM命令重新加载数据
 * @note 数据验证：会验证法术ID、专精法术ID、物品ID的有效性
 */
TC_GAME_API void LoadSkillPerfectItemTable();

/**
 * @brief 检查玩家是否可以创建额外物品
 *
 * 检查指定法术是否支持额外物品创建，以及玩家是否满足条件。
 * 如果支持，返回相关概率和最大额外数量信息。
 *
 * @param player 执行制造的玩家指针
 * @param spellId 制造法术的ID
 * @param[out] additionalChance 输出参数，每次创建额外物品的概率（百分比）
 * @param[out] additionalMax 输出参数，最大额外物品数量
 *
 * @return 如果可以创建额外物品返回true，否则返回false
 *
 * @note 调用时机：玩家执行制造动作时，在计算产出时调用
 * @note 条件检查：玩家必须拥有 requiredSpecialization 指定的专精技能
 * @note 额外物品机制：每个额外物品独立进行概率判定，直到达到上限或概率失败
 *
 * @example
 * @code
 * float chance;
 * uint8 maxExtra;
 * if (CanCreateExtraItems(player, spellId, chance, maxExtra))
 * {
 *     uint8 extraCount = 0;
 *     for (uint8 i = 0; i < maxExtra; ++i)
 *     {
 *         if (roll_chance_f(chance))
 *             ++extraCount;
 *     }
 *     // extraCount 为额外创建的物品数量
 * }
 * @endcode
 */
TC_GAME_API bool CanCreateExtraItems(Player* player, uint32 spellId, float &additionalChance, uint8 &additionalMax);

/**
 * @brief 加载额外物品创建模板数据
 *
 * 从数据库表 skill_extra_item_template 加载额外物品配置数据。
 * 该函数在服务器启动时调用。
 *
 * @note 调用时机：服务器启动时的世界初始化阶段
 * @note 支持热重载：可通过GM命令重新加载数据
 * @note 数据验证：会验证法术ID、专精法术ID、概率和最大数量的有效性
 */
TC_GAME_API void LoadSkillExtraItemTable();

#endif
