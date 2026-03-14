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
 * @file SkillDiscovery.cpp
 * @brief 技能发现系统实现文件
 *
 * 本文件实现了魔兽世界中的技能发现机制，包括：
 * - 从数据库加载技能发现模板
 * - 处理显式发现法术（如诺格弗格药剂）
 * - 处理制造时的随机配方发现
 * - 技能等级和概率计算
 *
 * 技能发现的两种模式：
 * 1. 基于技能线的发现：玩家在制作物品时有概率发现新配方
 * 2. 基于法术的发现：使用特定法术时触发发现
 *
 * 数据存储方式：
 * - 正数键（>0）：代表触发发现的法术ID
 * - 负数键（<0）：代表技能线ID的负值（用于技能线级别的发现）
 */

#include "SkillDiscovery.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Util.h"
#include "World.h"
#include <map>

/**
 * @struct SkillDiscoveryEntry
 * @brief 技能发现条目结构体
 *
 * 存储单个可发现法术的配置信息，包括发现的法术ID、
 * 所需技能等级和发现概率。
 */
struct SkillDiscoveryEntry
{
    uint32  spellId;                                        // 可被发现的法术ID
    uint32  reqSkillValue;                                  // 触发发现所需的最低技能等级
    float   chance;                                         // 发现概率（百分比）

    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员为默认值（0）
     */
    SkillDiscoveryEntry()
        : spellId(0), reqSkillValue(0), chance(0) { }

    /**
     * @brief 参数构造函数
     *
     * @param _spellId 可发现的法术ID
     * @param req_skill_val 所需技能等级
     * @param _chance 发现概率
     */
    SkillDiscoveryEntry(uint32 _spellId, uint32 req_skill_val, float _chance)
        : spellId(_spellId), reqSkillValue(req_skill_val), chance(_chance) { }
};

/// 技能发现列表类型，一个触发源可能对应多个可发现条目
typedef std::list<SkillDiscoveryEntry> SkillDiscoveryList;

/// 技能发现映射表，键为触发源（法术ID或技能ID的负值），值为发现列表
typedef std::unordered_map<int32, SkillDiscoveryList> SkillDiscoveryMap;

/// 全局技能发现数据存储，服务器启动时从数据库加载
static SkillDiscoveryMap SkillDiscoveryStore;

/**
 * @brief 加载技能发现模板数据
 *
 * 从数据库表 skill_discovery_template 加载技能发现配置。
 * 该函数会进行详细的数据验证，包括：
 * - 验证法术是否存在
 * - 验证触发条件是否合法
 * - 验证发现概率是否有效
 *
 * 支持两种发现模式：
 * 1. 法术触发发现（reqSpell > 0）：使用特定法术时触发
 * 2. 技能线触发发现（reqSpell = 0）：使用该技能线下的法术时触发
 *
 * @note 调用时机：服务器启动时，在世界初始化阶段调用
 * @note 支持重载：可以通过GM命令重新加载数据
 */
void LoadSkillDiscoveryTable()
{
    uint32 oldMSTime = getMSTime();

    SkillDiscoveryStore.clear();                            // 清空现有数据，支持重载功能

    //                                                0        1         2              3
    // 查询技能发现模板表：法术ID、触发法术/技能、所需技能等级、发现概率
    QueryResult result = WorldDatabase.Query("SELECT spellId, reqSpell, reqSkillValue, chance FROM skill_discovery_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 skill discovery definitions. DB table `skill_discovery_template` is empty.");
        return;
    }

    uint32 count = 0;

    // 用于收集无效条目的错误信息
    std::ostringstream ssNonDiscoverableEntries;
    // 记录已报告过的触发法术，避免重复日志
    std::set<uint32> reportedReqSpells;

    do
    {
        Field* fields = result->Fetch();

        uint32 spellId         = fields[0].GetUInt32();   // 可被发现的法术ID
        int32  reqSkillOrSpell = fields[1].GetInt32();    // 触发条件：正数为法术ID，0为技能线，负数非法
        uint32 reqSkillValue   = fields[2].GetUInt16();   // 所需最低技能等级
        float  chance          = fields[3].GetFloat();    // 发现概率

        // 验证发现概率必须大于0，否则无法被发现
        if (chance <= 0)                                    // chance
        {
            ssNonDiscoverableEntries << "spellId = " << spellId << " reqSkillOrSpell = " << reqSkillOrSpell
                << " reqSkillValue = " << reqSkillValue << " chance = " << chance << "(chance problem)\n";
            continue;
        }

        // 情况1：基于法术的发现（reqSpell > 0）
        if (reqSkillOrSpell > 0)                            // spell case
        {
            uint32 absReqSkillOrSpell = uint32(reqSkillOrSpell);
            SpellInfo const* reqSpellInfo = sSpellMgr->GetSpellInfo(absReqSkillOrSpell);

            // 验证触发法术是否存在
            if (!reqSpellInfo)
            {
                if (reportedReqSpells.find(absReqSkillOrSpell) == reportedReqSpells.end())
                {
                    TC_LOG_ERROR("sql.sql", "Spell (ID: {}) has a non-existing spell (ID: {}) in `reqSpell` field in the `skill_discovery_template` table.", spellId, reqSkillOrSpell);
                    reportedReqSpells.insert(absReqSkillOrSpell);
                }
                continue;
            }

            // 验证触发法术必须是发现机制法术或显式发现法术
            // mechanic discovery
            if (reqSpellInfo->Mechanic != MECHANIC_DISCOVERY &&
                // explicit discovery ability
                !reqSpellInfo->IsExplicitDiscovery())
            {
                if (reportedReqSpells.find(absReqSkillOrSpell) == reportedReqSpells.end())
                {
                    TC_LOG_ERROR("sql.sql", "Spell (ID: {}) does not have any MECHANIC_DISCOVERY (28) value in the Mechanic field in spell.dbc"
                        " nor 100% chance random discovery ability, but is listed for spellId {} (and maybe more) in the `skill_discovery_template` table.",
                        absReqSkillOrSpell, spellId);
                    reportedReqSpells.insert(absReqSkillOrSpell);
                }
                continue;
            }

            // 将发现条目添加到存储中，使用触发法术ID作为键
            SkillDiscoveryStore[reqSkillOrSpell].push_back(SkillDiscoveryEntry(spellId, reqSkillValue, chance));
        }
        // 情况2：基于技能线的发现（reqSpell = 0）
        else if (reqSkillOrSpell == 0)                      // skill case
        {
            // 获取法术所属的技能线信息
            SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);

            // 验证法术是否在技能线数据中
            if (bounds.first == bounds.second)
            {
                TC_LOG_ERROR("sql.sql", "Spell (ID: {}) is not listed in `SkillLineAbility.dbc`, but listed with `reqSpell`= 0 in the `skill_discovery_template` table.", spellId);
                continue;
            }

            // 将发现条目添加到所有相关的技能线中
            // 一个法术可能属于多个技能线（如联盟/部落不同版本）
            for (SkillLineAbilityMap::const_iterator _spell_idx = bounds.first; _spell_idx != bounds.second; ++_spell_idx)
                SkillDiscoveryStore[-int32(_spell_idx->second->SkillLine)].push_back(SkillDiscoveryEntry(spellId, reqSkillValue, chance));
        }
        // 情况3：负数值是非法的
        else
        {
            TC_LOG_ERROR("sql.sql", "Spell (ID: {}) has a negative value in `reqSpell` field in the `skill_discovery_template` table.", spellId);
            continue;
        }

        ++count;
    }
    while (result->NextRow());

    // 输出所有无效条目的汇总信息
    if (!ssNonDiscoverableEntries.str().empty())
        TC_LOG_ERROR("sql.sql", "Some items can't be successfully discovered, their chance field value is < 0.000001 in the `skill_discovery_template` DB table. List:\n{}", ssNonDiscoverableEntries.str());

    // 检查所有显式发现法术是否有对应的发现数据
    // 显式发现法术必须在 skill_discovery_template 中有数据才能正常工作
    // report about empty data for explicit discovery spells
    for (uint32 spell_id = 1; spell_id < sSpellMgr->GetSpellInfoStoreSize(); ++spell_id)
    {
        SpellInfo const* spellEntry = sSpellMgr->GetSpellInfo(spell_id);
        if (!spellEntry)
            continue;

        // skip not explicit discovery spells
        if (!spellEntry->IsExplicitDiscovery())
            continue;

        // 如果显式发现法术没有配置数据，记录错误
        if (SkillDiscoveryStore.find(int32(spell_id)) == SkillDiscoveryStore.end())
            TC_LOG_ERROR("sql.sql", "Spell (ID: {}) has got 100% chance random discovery ability, but does not have data in the `skill_discovery_template` table.", spell_id);
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} skill discovery definitions in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取显式发现法术的结果
 *
 * 处理显式发现类法术，这类法术必定成功返回一个可发现的法术（如果存在）。
 * 显式发现法术的例子：诺格弗格药剂、各种发现类技能。
 *
 * 算法流程：
 * 1. 检查法术是否有发现配置
 * 2. 获取玩家技能等级
 * 3. 计算所有可发现法术的总概率（排除已学会的和技能等级不足的）
 * 4. 进行加权随机选择
 *
 * @param spellId 显式发现法术的ID
 * @param player 使用法术的玩家指针
 *
 * @return 发现的法术ID，如果没有可发现的返回0
 *
 * @note 调用时机：玩家施放显式发现法术时
 * @note 显式发现的特点：只要还有未发现的法术，必定返回一个有效结果
 */
uint32 GetExplicitDiscoverySpell(uint32 spellId, Player* player)
{
    // explicit discovery spell chances (always success if case exist)
    // in this case we have both skill and spell
    // 查找该法术的发现配置
    SkillDiscoveryMap::const_iterator tab = SkillDiscoveryStore.find(int32(spellId));
    if (tab == SkillDiscoveryStore.end())
        return 0;

    // 获取玩家的相关技能等级（用于技能等级要求的检查）
    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    uint32 skillvalue = bounds.first != bounds.second ? player->GetSkillValue(bounds.first->second->SkillLine) : uint32(0);

    // 计算所有可发现法术的总概率
    // 只计算：1) 技能等级满足要求 2) 玩家尚未学会的法术
    float full_chance = 0;
    for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        if (item_iter->reqSkillValue <= skillvalue)
            if (!player->HasSpell(item_iter->spellId))
                full_chance += item_iter->chance;

    // 计算概率比率，用于归一化随机范围
    float rate = full_chance / 100.0f;
    float roll = (float)rand_chance() * rate;                      // roll now in range 0..full_chance
                                                                  // 生成0到full_chance范围内的随机数

    // 按概率权重选择要返回的法术
    // 类似于轮盘赌算法：累积概率，直到随机数落在某个区间
    for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
    {
        // 跳过技能等级不满足的法术
        if (item_iter->reqSkillValue > skillvalue)
            continue;

        // 跳过已经学会的法术
        if (player->HasSpell(item_iter->spellId))
            continue;

        // 检查随机数是否落在当前法术的概率区间内
        if (item_iter->chance > roll)
            return item_iter->spellId;

        // 减去当前法术的概率，继续检查下一个
        roll -= item_iter->chance;
    }

    return 0;
}

/**
 * @brief 检查玩家是否已发现所有可能的法术
 *
 * 遍历指定法术的所有可发现条目，检查玩家是否已学会全部。
 *
 * @param spellId 触发发现的法术ID
 * @param player 要检查的玩家指针
 *
 * @return 如果已发现所有法术返回true，如果还有未发现的返回false
 *         如果法术没有发现配置，也返回true
 *
 * @note 调用时机：用于判断是否还能继续发现新配方
 */
bool HasDiscoveredAllSpells(uint32 spellId, Player* player)
{
    // 查找发现配置，不存在则认为已全部发现
    SkillDiscoveryMap::const_iterator tab = SkillDiscoveryStore.find(int32(spellId));
    if (tab == SkillDiscoveryStore.end())
        return true;

    // 检查是否所有可发现的法术都已学会
    for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        if (!player->HasSpell(item_iter->spellId))
            return false;  // 发现一个未学会的法术

    return true;
}

/**
 * @brief 检查玩家是否已发现任意法术
 *
 * 检查玩家是否已通过指定法术发现至少一个新配方。
 *
 * @param spellId 触发发现的法术ID
 * @param player 要检查的玩家指针
 *
 * @return 如果已发现至少一个法术返回true，否则返回false
 *         如果法术没有发现配置，返回false
 *
 * @note 调用时机：用于成就判定或任务条件检查
 */
bool HasDiscoveredAnySpell(uint32 spellId, Player* player)
{
    // 查找发现配置，不存在则返回false
    SkillDiscoveryMap::const_iterator tab = SkillDiscoveryStore.find(int32(spellId));
    if (tab == SkillDiscoveryStore.end())
        return false;

    // 检查是否至少有一个可发现的法术已被学会
    for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        if (player->HasSpell(item_iter->spellId))
            return true;  // 发现一个已学会的法术

    return false;
}

/**
 * @brief 获取技能发现结果
 *
 * 处理常规的技能发现机制（如制作物品时的随机发现）。
 * 与显式发现不同，此函数基于概率触发，不是必定成功。
 *
 * 查找顺序：
 * 1. 首先检查基于法术的发现配置
 * 2. 如果没有，再检查基于技能线的发现配置
 *
 * 触发条件：
 * - 随机概率命中（受服务器倍率 RATE_SKILL_DISCOVERY 影响）
 * - 技能等级满足要求
 * - 玩家尚未学会该法术
 *
 * @param skillId 技能ID（专业技能ID），为0时只检查法术发现
 * @param spellId 当前使用的法术ID
 * @param player 执行动作的玩家指针
 *
 * @return 发现的法术ID，如果没有发现返回0
 *
 * @note 调用时机：玩家执行制造类动作时调用
 * @note 发现概率 = 基础概率 * 服务器倍率
 * @note 性能注意：需要进行多次条件检查和随机数生成，但开销很小
 */
uint32 GetSkillDiscoverySpell(uint32 skillId, uint32 spellId, Player* player)
{
    // 获取玩家的技能等级，skillId为0时技能等级为0
    uint32 skillvalue = skillId ? player->GetSkillValue(skillId) : uint32(0);

    // check spell case
    // 步骤1：检查基于法术的发现配置
    SkillDiscoveryMap::const_iterator tab = SkillDiscoveryStore.find(int32(spellId));

    if (tab != SkillDiscoveryStore.end())
    {
        // 遍历所有可发现的条目，每个条目独立进行概率判定
        for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        {
            // 三个条件同时满足才触发发现：
            // 1. 概率判定成功（概率 * 服务器倍率）
            // 2. 技能等级满足要求
            // 3. 玩家尚未学会该法术
            if (roll_chance_f(item_iter->chance * sWorld->getRate(RATE_SKILL_DISCOVERY)) &&
                item_iter->reqSkillValue <= skillvalue &&
                !player->HasSpell(item_iter->spellId))
                return item_iter->spellId;
        }

        return 0;
    }

    // 如果没有技能ID，直接返回（技能线检查需要有效的skillId）
    if (!skillId)
        return 0;

    // check skill line case
    // 步骤2：检查基于技能线的发现配置
    // 技能线配置使用负数键存储，所以要取负值查找
    tab = SkillDiscoveryStore.find(-(int32)skillId);
    if (tab != SkillDiscoveryStore.end())
    {
        // 同样遍历所有可发现条目进行概率判定
        for (SkillDiscoveryList::const_iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        {
            // 三个条件同时满足才触发发现
            if (roll_chance_f(item_iter->chance * sWorld->getRate(RATE_SKILL_DISCOVERY)) &&
                item_iter->reqSkillValue <= skillvalue &&
                !player->HasSpell(item_iter->spellId))
                return item_iter->spellId;
        }

        return 0;
    }

    return 0;
}
