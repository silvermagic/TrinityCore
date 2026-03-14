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
 * @file    item_scripts.cpp
 * @brief   物品脚本集合模块
 *
 * 本模块实现了多个特殊物品的脚本逻辑，包括：
 * - 仅限飞行状态下使用的物品
 * - 时间触发类物品（神秘蛋、恶心的罐子）
 * - 区域限制类物品（彼得罗夫集束炸弹）
 * - 任务相关物品（捕获的青蛙）
 * - 战斗法术触发限制物品
 *
 * 所有物品脚本继承自 ItemScript 基类，通过重写特定回调函数
 * 来实现物品的自定义行为。
 */

/* ScriptData
SDName: Item_Scripts
SD%Complete: 100
SDComment: Items for a range of different items. See content below (in script)
SDCategory: Items
EndScriptData */

/* ContentData
item_flying_machine(i34060, i34061)  Engineering crafted flying machines
item_only_for_flight                Items which should only useable while flying
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "Item.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"

/*#####
# item_only_for_flight
#####*/

/**
 * @brief 仅限飞行状态下使用的物品相关法术和区域枚举
 */
enum OnlyForFlight
{
    SPELL_ARCANE_CHARGES    = 45072  ///< 奥术充能法术ID
};

/**
 * @class item_only_for_flight
 * @brief 仅限飞行状态下使用的物品脚本
 *
 * 该类实现了对特定物品的使用限制，这些物品只能在玩家飞行状态下使用。
 * 针对不同的物品ID，还会应用额外的区域限制条件。
 */
class item_only_for_flight : public ItemScript
{
public:
    /**
     * @brief 构造函数
     * 初始化物品脚本基类，设置脚本名称
     */
    item_only_for_flight() : ItemScript("item_only_for_flight") { }

    /**
     * @brief 物品使用事件回调
     *
     * 当玩家尝试使用物品时触发，检查是否满足使用条件
     *
     * @param player    使用物品的玩家指针
     * @param item      被使用的物品指针
     * @param targets   法术目标信息（未使用）
     *
     * @return true  拦截物品使用（不执行默认行为）
     * @return false 允许物品使用（执行默认行为）
     *
     * @note 调用时机：玩家右键点击物品或通过宏使用物品时
     * @note 性能注意：包含区域查询操作，但效率影响较小
     */
    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        uint32 itemId = item->GetEntry();  // 获取物品模板ID
        bool disabled = false;              // 是否禁用物品的标志

        // 针对特定物品的特殊脚本逻辑
        switch (itemId)
        {
            case 24538:  // 特定物品ID
                // 检查是否在指定区域（区域ID: 3628）
                if (player->GetAreaId() != 3628)
                    disabled = true;
                break;
            case 34489:  // 特定物品ID
                // 检查是否在指定区域（区域ID: 4080）
                if (player->GetZoneId() != 4080)
                    disabled = true;
                break;
            case 34475:  // 特定物品ID
                // 发送"不在地面上"的法术失败消息
                if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_ARCANE_CHARGES))
                    Spell::SendCastResult(player, spellInfo, 1, SPELL_FAILED_NOT_ON_GROUND);
                break;
        }

        // 仅允许在飞行状态下使用，且未被特殊逻辑禁用
        if (player->IsInFlight() && !disabled)
            return false;  // 允许使用

        // 发送错误消息：当前无法执行此操作
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
        return true;  // 拦截使用
    }
};

/*#####
# item_mysterious_egg
#####*/

/**
 * @class item_mysterious_egg
 * @brief 神秘蛋物品脚本
 *
 * 该类实现了神秘蛋的过期逻辑。当神秘蛋过期时，
 * 会自动转换为破碎的蛋（Cracked Egg）物品。
 * 这是游戏中的一种时间演化机制。
 */
class item_mysterious_egg : public ItemScript
{
public:
    /**
     * @brief 构造函数
     * 初始化物品脚本基类，设置脚本名称
     */
    item_mysterious_egg() : ItemScript("item_mysterious_egg") { }

    /**
     * @brief 物品过期事件回调
     *
     * 当物品的过期时间到达时触发，将神秘蛋转换为破碎的蛋
     *
     * @param player      拥有过期物品的玩家指针
     * @param pItemProto  过期物品的模板信息（未使用）
     *
     * @return true  表示脚本已处理该事件
     *
     * @note 调用时机：物品的过期时间到达时由核心自动调用
     * @note 性能注意：包含背包空间检查和物品创建操作
     */
    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;  // 物品存储位置向量
        // 检查玩家是否能存储新物品（破碎的蛋，物品ID: 39883）
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 39883, 1);
        if (msg == EQUIP_ERR_OK)
        {
            // 可以存储，创建新的破碎的蛋并添加到背包
            player->StoreNewItem(dest, 39883, true, GenerateItemRandomPropertyId(39883));
        }

        return true;  // 事件已处理
    }
};

/*#####
# item_disgusting_jar
#####*/

/**
 * @class item_disgusting_jar
 * @brief 恶心的罐子物品脚本
 *
 * 该类实现了恶心的罐子的过期逻辑。当恶心的罐子过期时，
 * 会自动转换为成熟的恶心的罐子（Ripe Disgusting Jar）。
 * 这是一种物品发酵/成熟机制。
 */
class item_disgusting_jar : public ItemScript
{
public:
    /**
     * @brief 构造函数
     * 初始化物品脚本基类，设置脚本名称
     */
    item_disgusting_jar() : ItemScript("item_disgusting_jar") { }

    /**
     * @brief 物品过期事件回调
     *
     * 当物品的过期时间到达时触发，将恶心的罐子转换为成熟的恶心的罐子
     *
     * @param player      拥有过期物品的玩家指针
     * @param pItemProto  过期物品的模板信息（未使用）
     *
     * @return true  表示脚本已处理该事件
     *
     * @note 调用时机：物品的过期时间到达时由核心自动调用
     * @note 性能注意：包含背包空间检查和物品创建操作
     */
    bool OnExpire(Player* player, ItemTemplate const* /*pItemProto*/) override
    {
        ItemPosCountVec dest;  // 物品存储位置向量
        // 检查玩家是否能存储新物品（成熟的恶心的罐子，物品ID: 44718）
        uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 44718, 1);
        if (msg == EQUIP_ERR_OK)
        {
            // 可以存储，创建新的成熟的恶心的罐子并添加到背包
            player->StoreNewItem(dest, 44718, true, GenerateItemRandomPropertyId(44718));
        }

        return true;  // 事件已处理
    }
};

/*#####
# item_petrov_cluster_bombs
#####*/

/**
 * @brief 彼得罗夫集束炸弹相关法术和区域枚举
 */
enum PetrovClusterBombs
{
    SPELL_PETROV_BOMB           = 42406,  ///< 彼得罗夫炸弹法术ID
    AREA_ID_SHATTERED_STRAITS   = 4064,   ///< 破碎海峡区域ID
    ZONE_ID_HOWLING             = 495     ///< 嚎风峡湾区域ID
};

/**
 * @class item_petrov_cluster_bombs
 * @brief 彼得罗夫集束炸弹物品脚本
 *
 * 该类实现了彼得罗夫集束炸弹的使用限制。
 * 该物品只能在嚎风峡湾的破碎海峡区域的特定载具上使用，
 * 这是一种严格的位置和状态限制机制。
 */
class item_petrov_cluster_bombs : public ItemScript
{
public:
    /**
     * @brief 构造函数
     * 初始化物品脚本基类，设置脚本名称
     */
    item_petrov_cluster_bombs() : ItemScript("item_petrov_cluster_bombs") { }

    /**
     * @brief 物品使用事件回调
     *
     * 当玩家尝试使用物品时触发，检查是否在正确的区域和载具上
     *
     * @param player    使用物品的玩家指针
     * @param item      被使用的物品指针
     * @param targets   法术目标信息（未使用）
     *
     * @return true  拦截物品使用（不执行默认行为）
     * @return false 允许物品使用（执行默认行为）
     *
     * @note 调用时机：玩家尝试使用该物品时
     * @note 性能注意：包含区域和载具状态检查，效率影响较小
     */
    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        // 首先检查是否在嚎风峡湾区域
        if (player->GetZoneId() != ZONE_ID_HOWLING)
            return false;  // 不在嚎风峡湾，允许正常使用（或由其他逻辑处理）

        // 检查是否在载具上且在破碎海峡区域
        if (!player->GetTransport() || player->GetAreaId() != AREA_ID_SHATTERED_STRAITS)
        {
            // 不满足条件，发送错误消息
            player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);

            // 发送"不能在这里使用"的法术失败消息
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_PETROV_BOMB))
                Spell::SendCastResult(player, spellInfo, 1, SPELL_FAILED_NOT_HERE);

            return true;  // 拦截使用
        }

        return false;  // 满足所有条件，允许使用
    }
};

/**
 * @brief 捕获的青蛙相关任务和NPC枚举
 */
enum CapturedFrog
{
    QUEST_THE_PERFECT_SPIES      = 25444,  ///< 任务ID：完美的间谍
    NPC_VANIRAS_SENTRY_TOTEM     = 40187   ///< NPC ID：瓦尼拉的哨兵图腾
};

/**
 * @class item_captured_frog
 * @brief 捕获的青蛙物品脚本
 *
 * 该类实现了捕获的青蛙的使用限制。
 * 该物品只能在"完美的间谍"任务进行中使用，
 * 且必须在瓦尼拉的哨兵图腾附近才能生效。
 * 这是一种任务物品的典型使用限制模式。
 */
class item_captured_frog : public ItemScript
{
public:
    /**
     * @brief 构造函数
     * 初始化物品脚本基类，设置脚本名称
     */
    item_captured_frog() : ItemScript("item_captured_frog") { }

    /**
     * @brief 物品使用事件回调
     *
     * 当玩家尝试使用物品时触发，检查是否满足任务和位置条件
     *
     * @param player    使用物品的玩家指针
     * @param item      被使用的物品指针
     * @param targets   法术目标信息（未使用）
     *
     * @return true  拦截物品使用（不执行默认行为）
     * @return false 允许物品使用（执行默认行为）
     *
     * @note 调用时机：玩家尝试使用该物品时
     * @note 性能注意：包含任务状态检查和NPC搜索操作
     */
    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        // 检查任务状态是否为进行中
        if (player->GetQuestStatus(QUEST_THE_PERFECT_SPIES) == QUEST_STATUS_INCOMPLETE)
        {
            // 检查附近是否存在瓦尼拉的哨兵图腾（范围：10码）
            if (player->FindNearestCreature(NPC_VANIRAS_SENTRY_TOTEM, 10.0f))
                return false;  // 满足条件，允许使用
            else
                // 不在图腾附近，发送超出范围错误
                player->SendEquipError(EQUIP_ERR_OUT_OF_RANGE, item, nullptr);
        }
        else
            // 任务状态不正确，发送当前无法执行错误
            player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);

        return true;  // 拦截使用
    }
};

/**
 * @class item_generic_limit_chance_above_60
 * @brief 通用物品触发限制脚本（针对60级以上目标）
 *
 * 该类实现了对物品战斗法术触发的等级限制机制。
 * 目前主要用于物品 19169（夜幕，Nightfall）。
 * 当目标等级超过60级时，物品的触发几率会大幅降低，
 * 这是一种平衡机制，防止低级物品在高级内容中过于强大。
 *
 * @note 机制说明：
 *   - 目标等级 ≤ 60：正常触发几率
 *   - 目标等级 > 60：触发几率按公式 (等级-60) * 9.93 计算
 *   - 70级目标约有 99.3% 的失败几率，即实际触发率约 0.1%
 */
class item_generic_limit_chance_above_60 : public ItemScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化物品脚本基类，设置脚本名称
         */
        item_generic_limit_chance_above_60() : ItemScript("item_generic_limit_chance_above_60") { }

        /**
         * @brief 物品战斗法术施放事件回调
         *
         * 当物品的战斗法术即将触发时调用，决定是否允许触发
         *
         * @param player    拥有物品的玩家指针（未使用）
         * @param victim    法术目标单位指针
         * @param spellInfo 即将施放的法术信息（未使用）
         * @param item      触发法术的物品指针（未使用）
         *
         * @return true  允许法术触发
         * @return false 阻止法术触发
         *
         * @note 调用时机：物品的战斗法术即将触发时（基于PPM或触发几率）
         * @note 性能注意：仅包含简单的数学计算，性能影响极小
         * @note 算法说明：基础PPM几率已经在核心中判定，此函数仅进行额外成功率判定
         */
        bool OnCastItemCombatSpell(Player* /*player*/, Unit* victim, SpellInfo const* /*spellInfo*/, Item* /*item*/) override
        {
            // 当目标等级超过60级时，大幅降低触发几率（公式未知，由暴雪设计）
            if (victim->GetLevel() > 60)
            {
                // 在70级时提供约0.1%的触发几率
                float const lvlPenaltyFactor = 9.93f;  // 等级惩罚系数
                float const failureChance = (victim->GetLevel() - 60) * lvlPenaltyFactor;

                // 基础PPM几率已经判定，这里仅判定最终成功率
                // roll_chance_f 返回 true 表示失败（失败几率 = failureChance%）
                // 取反表示：返回 true 表示成功（不失败）
                return !roll_chance_f(failureChance);
            }

            return true;  // 目标等级 ≤ 60，允许触发
        }
};

/**
 * @brief 注册所有物品脚本
 *
 * 该函数由脚本系统在服务器启动时调用，用于注册本文件中定义的所有物品脚本。
 * 每个脚本通过 new 操作符创建实例，脚本系统会自动管理其生命周期。
 *
 * @note 调用时机：服务器启动过程中的脚本加载阶段
 * @note 内存管理：创建的脚本对象由脚本系统自动管理，无需手动释放
 */
void AddSC_item_scripts()
{
    new item_only_for_flight();               // 仅限飞行状态使用的物品脚本
    new item_mysterious_egg();                // 神秘蛋过期转换脚本
    new item_disgusting_jar();                // 恶心的罐子过期转换脚本
    new item_petrov_cluster_bombs();          // 彼得罗夫集束炸弹脚本
    new item_captured_frog();                 // 捕获的青蛙任务物品脚本
    new item_generic_limit_chance_above_60(); // 60级以上目标触发限制脚本
}
