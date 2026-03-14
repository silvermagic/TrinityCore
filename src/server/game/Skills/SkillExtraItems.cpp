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
 * @file SkillExtraItems.cpp
 * @brief 技能额外物品和完美物品系统实现文件
 *
 * 本文件实现了专业技能中的特殊产出机制：
 *
 * 1. 完美物品系统：
 *    - 允许有专精的玩家制造出完美版本的物品
 *    - 完美物品通常具有更好的属性或更高的品质
 *    - 例如：珠宝加工的完美宝石（Perfect Gem）
 *
 * 2. 额外物品系统：
 *    - 允许有专精的玩家在制造时获得额外数量
 *    - 基于概率多次判定，最多可获得 additionalMaxNum 个额外物品
 *    - 例如：药剂大师制作药剂时的额外产出
 *
 * 数据来源：
 * - 完美物品：skill_perfect_item_template 数据库表
 * - 额外物品：skill_extra_item_template 数据库表
 */

#include "SkillExtraItems.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include <map>

// some type definitions
// no use putting them in the header file, they're only used in this .cpp
// 类型定义仅在本文件使用，无需放入头文件

/**
 * @struct SkillPerfectItemEntry
 * @brief 完美物品创建条目结构体
 *
 * 存储单个制造法术的完美物品配置信息。
 * 当玩家拥有所需专精时，制造物品有概率产出完美版本。
 */
// struct to store information about perfection procs
// one entry per spell
struct SkillPerfectItemEntry
{
    // the spell id of the spell required - it's named "specialization" to conform with SkillExtraItemEntry
    uint32 requiredSpecialization;  // 所需的专业专精法术ID
    // perfection proc chance
    float perfectCreateChance;      // 创建完美物品的概率（百分比）
    // itemid of the resulting perfect item
    uint32 perfectItemType;         // 完美物品的物品模板ID

    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员为默认值
     */
    SkillPerfectItemEntry()
        : requiredSpecialization(0), perfectCreateChance(0.0f), perfectItemType(0) { }

    /**
     * @brief 参数构造函数
     *
     * @param rS 所需专精法术ID
     * @param pCC 完美物品创建概率
     * @param pIT 完美物品类型ID
     */
    SkillPerfectItemEntry(uint32 rS, float pCC, uint32 pIT)
        : requiredSpecialization(rS), perfectCreateChance(pCC), perfectItemType(pIT) { }
};

// map to store perfection info. key = spellId of the creation spell, value is the perfectitementry as specified above
/// 完美物品配置映射表，键为制造法术ID，值为对应的完美物品配置
typedef std::map<uint32, SkillPerfectItemEntry> SkillPerfectItemMap;

/// 全局完美物品数据存储
SkillPerfectItemMap SkillPerfectItemStore;

/**
 * @brief 加载完美物品创建模板数据
 *
 * 从数据库表 skill_perfect_item_template 加载完美物品配置。
 * 该函数执行完整的数据验证，确保所有引用的法术和物品都存在。
 *
 * 验证内容包括：
 * - 制造法术ID是否有效
 * - 所需专精法术ID是否有效
 * - 创建概率是否合法（必须 > 0）
 * - 完美物品ID是否有效
 *
 * @note 调用时机：服务器启动时，世界初始化阶段
 * @note 支持热重载：可通过GM命令重新加载
 */
// loads the perfection proc info from DB
void LoadSkillPerfectItemTable()
{
    uint32 oldMSTime = getMSTime();

    SkillPerfectItemStore.clear(); // reload capability
                                  // 清空数据，支持重载

    //                                                  0               1                      2                  3
    // 查询完美物品模板表：制造法术ID、所需专精、创建概率、完美物品ID
    QueryResult result = WorldDatabase.Query("SELECT spellId, requiredSpecialization, perfectCreateChance, perfectItemType FROM skill_perfect_item_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell perfection definitions. DB table `skill_perfect_item_template` is empty.");
        return;
    }

    uint32 count = 0;

    do /* fetch data and run sanity checks */
       /* 获取数据并执行完整性检查 */
    {
        Field* fields = result->Fetch();

        uint32 spellId = fields[0].GetUInt32();  // 制造法术ID

        // 验证制造法术是否存在
        if (!sSpellMgr->GetSpellInfo(spellId))
        {
            TC_LOG_ERROR("sql.sql", "Skill perfection data for spell {} has a non-existing spell id in the `skill_perfect_item_template`!", spellId);
            continue;
        }

        uint32 requiredSpecialization = fields[1].GetUInt32();  // 所需专精法术ID
        // 验证专精法术是否存在
        if (!sSpellMgr->GetSpellInfo(requiredSpecialization))
        {
            TC_LOG_ERROR("sql.sql", "Skill perfection data for spell {} has a non-existing required specialization spell id {} in the `skill_perfect_item_template`!", spellId, requiredSpecialization);
            continue;
        }

        float perfectCreateChance = fields[2].GetFloat();  // 完美物品创建概率
        // 验证概率必须大于0
        if (perfectCreateChance <= 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Skill perfection data for spell {} has impossibly low proc chance in the `skill_perfect_item_template`!", spellId);
            continue;
        }

        uint32 perfectItemType = fields[3].GetUInt32();  // 完美物品ID
        // 验证完美物品是否存在
        if (!sObjectMgr->GetItemTemplate(perfectItemType))
        {
            TC_LOG_ERROR("sql.sql", "Skill perfection data for spell {} references a non-existing perfect item id {} in the `skill_perfect_item_template`!", spellId, perfectItemType);
            continue;
        }

        // 所有验证通过，存入数据
        SkillPerfectItemEntry& skillPerfectItemEntry = SkillPerfectItemStore[spellId];

        skillPerfectItemEntry.requiredSpecialization = requiredSpecialization;
        skillPerfectItemEntry.perfectCreateChance = perfectCreateChance;
        skillPerfectItemEntry.perfectItemType = perfectItemType;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell perfection definitions in {} ms.", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @struct SkillExtraItemEntry
 * @brief 额外物品创建条目结构体
 *
 * 存储单个制造法术的额外物品配置信息。
 * 当玩家拥有所需专精时，制造物品有概率获得额外数量。
 */
// struct to store information about extra item creation
// one entry for every spell that is able to create an extra item
struct SkillExtraItemEntry
{
    // the spell id of the specialization required to create extra items
    uint32 requiredSpecialization;  // 所需的专业专精法术ID
    // the chance to create one additional item
    float additionalCreateChance;   // 每个额外物品的创建概率（百分比）
    // maximum number of extra items created per crafting
    uint8 additionalMaxNum;         // 单次制造可获得的最大额外物品数量

    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员为默认值
     */
    SkillExtraItemEntry()
        : requiredSpecialization(0), additionalCreateChance(0.0f), additionalMaxNum(0) { }

    /**
     * @brief 参数构造函数
     *
     * @param rS 所需专精法术ID
     * @param aCC 额外物品创建概率
     * @param aMN 最大额外物品数量
     */
    SkillExtraItemEntry(uint32 rS, float aCC, uint8 aMN)
        : requiredSpecialization(rS), additionalCreateChance(aCC), additionalMaxNum(aMN) { }
};

// map to store the extra item creation info, the key is the spellId of the creation spell, the mapped value is the assigned SkillExtraItemEntry
/// 额外物品配置映射表，键为制造法术ID，值为对应的额外物品配置
typedef std::map<uint32, SkillExtraItemEntry> SkillExtraItemMap;

/// 全局额外物品数据存储
SkillExtraItemMap SkillExtraItemStore;

/**
 * @brief 加载额外物品创建模板数据
 *
 * 从数据库表 skill_extra_item_template 加载额外物品配置。
 * 该函数执行完整的数据验证，确保所有引用的法术都存在，
 * 且概率和最大数量配置合理。
 *
 * 验证内容包括：
 * - 制造法术ID是否有效
 * - 所需专精法术ID是否有效
 * - 创建概率是否合法（必须 > 0）
 * - 最大额外数量是否合法（必须 > 0）
 *
 * @note 调用时机：服务器启动时，世界初始化阶段
 * @note 支持热重载：可通过GM命令重新加载
 */
// loads the extra item creation info from DB
void LoadSkillExtraItemTable()
{
    uint32 oldMSTime = getMSTime();

    SkillExtraItemStore.clear();                            // need for reload
                                                          // 清空数据，支持重载

    //                                                  0               1                       2                    3
    // 查询额外物品模板表：制造法术ID、所需专精、创建概率、最大额外数量
    QueryResult result = WorldDatabase.Query("SELECT spellId, requiredSpecialization, additionalCreateChance, additionalMaxNum FROM skill_extra_item_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell specialization definitions. DB table `skill_extra_item_template` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 spellId = fields[0].GetUInt32();  // 制造法术ID

        // 验证制造法术是否存在
        if (!sSpellMgr->GetSpellInfo(spellId))
        {
            TC_LOG_ERROR("sql.sql", "Skill specialization {} has a non-existing spell id in the `skill_extra_item_template`!", spellId);
            continue;
        }

        uint32 requiredSpecialization = fields[1].GetUInt32();  // 所需专精法术ID
        // 验证专精法术是否存在
        if (!sSpellMgr->GetSpellInfo(requiredSpecialization))
        {
            TC_LOG_ERROR("sql.sql", "Skill specialization {} has a non-existing required specialization spell id {} in the `skill_extra_item_template`!", spellId, requiredSpecialization);
            continue;
        }

        float additionalCreateChance = fields[2].GetFloat();  // 额外物品创建概率
        // 验证概率必须大于0
        if (additionalCreateChance <= 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Skill specialization {} has too low additional create chance in the `skill_extra_item_template`!", spellId);
            continue;
        }

        uint8 additionalMaxNum = fields[3].GetUInt8();  // 最大额外物品数量
        // 验证最大数量必须大于0
        if (!additionalMaxNum)
        {
            TC_LOG_ERROR("sql.sql", "Skill specialization {} has 0 max number of extra items in the `skill_extra_item_template`!", spellId);
            continue;
        }

        // 所有验证通过，存入数据
        SkillExtraItemEntry& skillExtraItemEntry = SkillExtraItemStore[spellId];

        skillExtraItemEntry.requiredSpecialization = requiredSpecialization;
        skillExtraItemEntry.additionalCreateChance = additionalCreateChance;
        skillExtraItemEntry.additionalMaxNum       = additionalMaxNum;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell specialization definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 检查玩家是否可以创建完美物品
 *
 * 检查指定的制造法术是否支持完美物品创建，并且玩家是否拥有
 * 必需的专业专精。如果满足条件，返回完美物品的相关信息。
 *
 * 工作流程：
 * 1. 在配置数据中查找该法术的完美物品配置
 * 2. 检查玩家是否拥有所需的专业专精
 * 3. 如果满足条件，返回概率和完美物品ID
 *
 * @param player 执行制造的玩家指针
 * @param spellId 制造法术的ID
 * @param[out] perfectCreateChance 输出参数，完美物品创建概率（百分比）
 * @param[out] perfectItemType 输出参数，完美物品的物品模板ID
 *
 * @return 如果可以创建完美物品返回true，否则返回false
 *
 * @note 调用时机：玩家执行制造动作时，在计算产出前调用
 * @note 典型应用：珠宝加工的完美宝石切割
 *
 * @example 使用示例：
 * @code
 * float chance;
 * uint32 perfectItemId;
 * if (CanCreatePerfectItem(player, spellId, chance, perfectItemId))
 * {
 *     if (roll_chance_f(chance))
 *     {
 *         // 创建完美物品而非普通物品
 *         player->AddItem(perfectItemId, 1);
 *     }
 * }
 * @endcode
 */
bool CanCreatePerfectItem(Player* player, uint32 spellId, float &perfectCreateChance, uint32 &perfectItemType)
{
    // 查找该法术的完美物品配置
    SkillPerfectItemMap::const_iterator ret = SkillPerfectItemStore.find(spellId);
    // no entry in DB means no perfection proc possible
    // 数据库中没有配置，说明不支持完美物品
    if (ret == SkillPerfectItemStore.end())
        return false;

    SkillPerfectItemEntry const* thisEntry = &ret->second;

    // if you don't have the spell needed, then no procs for you
    // 检查玩家是否拥有必需的专业专精
    if (!player->HasSpell(thisEntry->requiredSpecialization))
        return false;

    // set values as appropriate
    // 设置输出参数：完美物品概率和类型
    perfectCreateChance = thisEntry->perfectCreateChance;
    perfectItemType = thisEntry->perfectItemType;

    // and tell the caller to start rolling the dice
    // 返回true，告诉调用者可以进行完美物品的概率判定
    return true;
}

/**
 * @brief 检查玩家是否可以创建额外物品
 *
 * 检查指定的制造法术是否支持额外物品创建，并且玩家是否拥有
 * 必需的专业专精。如果满足条件，返回额外物品的相关配置。
 *
 * 工作流程：
 * 1. 在配置数据中查找该法术的额外物品配置
 * 2. 检查玩家是否拥有所需的专业专精
 * 3. 如果满足条件，返回概率和最大额外数量
 *
 * 额外物品机制说明：
 * - 每个额外物品独立进行概率判定
 * - 最多可获得 additionalMaxNum 个额外物品
 * - 例如：如果概率是20%，最大数量是4，则最多尝试4次，
 *        每次有20%概率获得一个额外物品
 *
 * @param player 执行制造的玩家指针
 * @param spellId 制造法术的ID
 * @param[out] additionalChance 输出参数，每个额外物品的创建概率（百分比）
 * @param[out] additionalMax 输出参数，最大额外物品数量
 *
 * @return 如果可以创建额外物品返回true，否则返回false
 *
 * @note 调用时机：玩家执行制造动作时，在计算产出时调用
 * @note 典型应用：药剂大师制作药剂时的额外产出
 *
 * @example 使用示例：
 * @code
 * float chance;
 * uint8 maxExtra;
 * if (CanCreateExtraItems(player, spellId, chance, maxExtra))
 * {
 *     uint8 extraCount = 0;
 *     // 对每个可能的额外物品进行独立概率判定
 *     for (uint8 i = 0; i < maxExtra; ++i)
 *     {
 *         if (roll_chance_f(chance))
 *             ++extraCount;
 *     }
 *     // extraCount 是本次制造获得的额外物品数量
 *     if (extraCount > 0)
 *         player->AddItem(itemId, extraCount);
 * }
 * @endcode
 */
bool CanCreateExtraItems(Player* player, uint32 spellId, float &additionalChance, uint8 &additionalMax)
{
    // get the info for the specified spell
    // 查找该法术的额外物品配置
    SkillExtraItemMap::const_iterator ret = SkillExtraItemStore.find(spellId);
    if (ret == SkillExtraItemStore.end())
        return false;

    SkillExtraItemEntry const* specEntry = &ret->second;

    // the player doesn't have the required specialization, return false
    // 检查玩家是否拥有必需的专业专精
    if (!player->HasSpell(specEntry->requiredSpecialization))
        return false;

    // set the arguments to the appropriate values
    // 设置输出参数：额外物品概率和最大数量
    additionalChance = specEntry->additionalCreateChance;
    additionalMax = specEntry->additionalMaxNum;

    // enable extra item creation
    // 返回true，告诉调用者可以启用额外物品机制
    return true;
}
