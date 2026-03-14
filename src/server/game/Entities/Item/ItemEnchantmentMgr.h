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
 * @file ItemEnchantmentMgr.h
 * @brief 物品附魔管理器头文件
 *
 * 本文件声明了物品随机附魔和随机属性的管理功能。
 *
 * 主要功能：
 *   - 加载随机附魔模板表
 *   - 生成物品的随机属性ID
 *   - 生成物品的随机后缀因子
 *   - 根据权重随机选择附魔效果
 *
 * 模块关系：
 *   - 被 Item.cpp 调用，用于生成物品的随机属性
 *   - 被 ObjectMgr 调用，在服务器启动时加载附魔表
 *   - 与数据库表 item_enchantment_template 交互
 *
 * 设计说明：
 *   - 随机附魔系统允许物品在创建时获得随机的属性加成
 *   - 每个随机属性条目有一个关联的权重（概率）
 *   - 支持两种随机属性类型：RandomProperty（正ID）和 RandomSuffix（负ID）
 */

#ifndef _ITEM_ENCHANTMENT_MGR_H
#define _ITEM_ENCHANTMENT_MGR_H

#include "Common.h"

/**
 * @brief 加载随机附魔模板表
 *
 * 从数据库 item_enchantment_template 表加载随机附魔定义。
 * 该表定义了每个随机属性条目ID对应的可选附魔及其概率。
 *
 * 调用时机：
 *   - 服务器启动时由 ObjectMgr 调用
 *   - 重载物品数据时调用
 *
 * @note 表结构：
 *   - entry: 随机属性条目ID
 *   - ench: 附魔效果ID
 *   - chance: 出现概率（百分比）
 */
TC_GAME_API void LoadRandomEnchantmentsTable();

/**
 * @brief 生成物品的随机属性ID
 *
 * 根据物品模板的 RandomProperty 或 RandomSuffix 字段，
 * 随机选择一个具体的属性ID。
 *
 * @param item_id 物品ID
 * @return 随机属性ID（正数表示 RandomProperty，负数表示 RandomSuffix）
 *         如果物品不支持随机属性则返回0
 *
 * 随机属性类型：
 *   - RandomProperty（正ID）：固定的随机属性组合，如"of the Bear"（熊之）
 *   - RandomSuffix（负ID）：动态计算的属性后缀，如"of Strength"（力量之）
 *
 * @note 物品模板中只能设置 RandomProperty 或 RandomSuffix 之一，不能同时存在
 */
TC_GAME_API int32 GenerateItemRandomPropertyId(uint32 item_id);

/**
 * @brief 根据条目ID获取随机附魔效果
 *
 * 从预加载的附魔表中根据权重随机选择一个附魔效果。
 * 每个附魔效果有一个关联的概率值，概率之和不一定为100%。
 *
 * @param entry 随机属性条目ID（来自 item_enchantment_template 表）
 * @return 随机选择的附魔效果ID，如果条目无效则返回0
 *
 * 算法说明：
 *   1. 如果所有概率之和小于100%，可能不选择任何附魔
 *   2. 如果概率之和大于等于100%，按权重比例选择
 *   3. 使用随机数生成器确保公平性
 */
TC_GAME_API uint32 GetItemEnchantMod(int32 entry);

/**
 * @brief 生成随机后缀因子
 *
 * 计算随机后缀物品的属性因子值。
 * 该因子决定了随机后缀提供的属性加成数值。
 *
 * @param item_id 物品ID
 * @return 随机后缀因子值，如果物品不支持随机后缀则返回0
 *
 * 计算依据：
 *   - 物品等级（决定基础属性值范围）
 *   - 物品品质（绿色/蓝色/紫色使用不同的系数）
 *   - 装备槽位类型（影响属性值大小）
 *
 * @note 随机后缀属性值 = 基础值 * 后缀因子 / 10000
 *       这是暴雪的标准计算公式
 */
TC_GAME_API uint32 GenerateEnchSuffixFactor(uint32 item_id);

#endif
