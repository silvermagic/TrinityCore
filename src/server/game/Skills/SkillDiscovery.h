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
 * @file SkillDiscovery.h
 * @brief 技能发现系统头文件
 *
 * 本模块负责管理游戏中的技能发现机制，包括：
 * - 技能配方/配方的随机发现（如锻造、炼金术等专业技能的新配方发现）
 * - 显式发现法术的处理（玩家主动使用发现类法术）
 * - 技能等级相关的发现概率计算
 *
 * 技能发现是魔兽世界中专业技能的一个重要机制，允许玩家在制作物品时
 * 有一定概率发现新的配方或技能。
 */

#ifndef TRINITY_SKILLDISCOVERY_H
#define TRINITY_SKILLDISCOVERY_H

#include "Common.h"

class Player;

/**
 * @brief 加载技能发现模板数据
 *
 * 从数据库表 skill_discovery_template 加载技能发现配置数据。
 * 该函数在服务器启动时调用，用于初始化技能发现系统。
 *
 * @note 调用时机：服务器启动时的世界初始化阶段
 * @note 性能注意：加载过程会进行大量数据验证，确保数据完整性
 * @note 支持热重载，可在运行时重新加载数据
 */
TC_GAME_API void LoadSkillDiscoveryTable();

/**
 * @brief 获取技能发现结果
 *
 * 根据技能ID和法术ID计算是否触发技能发现，返回发现的法术ID。
 * 此函数用于常规的技能发现机制（如制作时的随机发现）。
 *
 * @param skillId 技能ID（如锻造、炼金术等专业技能ID），为0时只检查法术发现
 * @param spellId 当前使用的法术ID，作为发现的触发条件
 * @param player 触发发现的玩家对象指针
 *
 * @return 返回发现的法术ID，如果没有发现则返回0
 *
 * @note 调用时机：玩家使用制造类法术时调用
 * @note 性能注意：会进行随机数生成和多重条件检查，但开销很小
 * @note 发现概率受服务器配置 RATE_SKILL_DISCOVERY 影响
 */
TC_GAME_API uint32 GetSkillDiscoverySpell(uint32 skillId, uint32 spellId, Player* player);

/**
 * @brief 检查玩家是否已发现所有可能的法术
 *
 * 检查指定法术的所有可发现配方是否都已被玩家学会。
 *
 * @param spellId 触发发现的法术ID
 * @param player 要检查的玩家对象指针
 *
 * @return 如果已发现所有可能的法术返回true，否则返回false
 *
 * @note 调用时机：判断是否还能继续发现新配方时使用
 * @note 如果法术没有发现配置，返回true
 */
TC_GAME_API bool HasDiscoveredAllSpells(uint32 spellId, Player* player);

/**
 * @brief 检查玩家是否已发现任意法术
 *
 * 检查玩家是否已经通过指定法术发现了至少一个新配方。
 *
 * @param spellId 触发发现的法术ID
 * @param player 要检查的玩家对象指针
 *
 * @return 如果已发现至少一个法术返回true，否则返回false
 *
 * @note 调用时机：用于成就或任务条件检查
 */
TC_GAME_API bool HasDiscoveredAnySpell(uint32 spellId, Player* player);

/**
 * @brief 获取显式发现法术的结果
 *
 * 处理显式发现类法术（如诺格弗格药剂等），根据玩家技能等级
 * 和已学会的法术情况，返回本次发现的法术ID。
 * 显式发现法术必定成功返回一个法术（如果还有未学会的）。
 *
 * @param spellId 显式发现法术的ID
 * @param player 使用发现法术的玩家对象指针
 *
 * @return 返回发现的法术ID，如果没有可发现的法术返回0
 *
 * @note 调用时机：玩家施放显式发现类法术时调用
 * @note 显式发现法术的特征：SpellInfo 中 IsExplicitDiscovery() 返回true
 * @note 与 GetSkillDiscoverySpell 不同，此函数保证返回有效结果（如果存在）
 */
TC_GAME_API uint32 GetExplicitDiscoverySpell(uint32 spellId, Player* player);

#endif
