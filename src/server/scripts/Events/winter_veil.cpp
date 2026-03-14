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
 * @file winter_veil.cpp
 * @brief 冬幕节事件脚本模块
 *
 * 本模块实现了魔兽世界冬幕节（Winter Veil）节日活动的核心功能，包括：
 * - 槲寄生系统：使用槲寄生获得随机节日物品
 * - 冬意发射器系统：将玩家变成小矮人造型
 * - 驯鹿变形系统：将坐骑变成驯鹿造型
 *
 * 冬幕节每年12月15日至1月2日举办，主要活动包括：
 * - 在各大城市参加冬幕节庆祝活动
 * - 完成节日任务获得礼物
 * - 购买节日限定物品和配方
 * - 与其他玩家在槲寄生下互动
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 槲寄生法术ID枚举
 *
 * 定义了槲寄生使用时可能获得的节日物品法术。
 * 槲寄生是冬幕节的经典互动元素。
 */
enum Mistletoe
{
    SPELL_CREATE_MISTLETOE          = 26206,  ///< 创建槲寄生 - 节日装饰物品
    SPELL_CREATE_HOLLY              = 26207,  ///< 创建冬青 - 节日装饰物品
    SPELL_CREATE_SNOWFLAKES         = 45036   ///< 创建雪花 - 节日装饰物品
};

/**
 * @brief 槲寄生法术脚本 (Spell ID: 26218)
 *
 * 当玩家对其他玩家使用槲寄生时触发。
 * 随机给予目标一个节日装饰物品：槲寄生、冬青或雪花。
 *
 * 传统背景：在现实中，槲寄生下亲吻是圣诞节传统，
 * 游戏中将这一传统转化为友好的物品交换机制。
 */
// 26218 - Mistletoe
class spell_winter_veil_mistletoe : public SpellScript
{
    PrepareSpellScript(spell_winter_veil_mistletoe);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_CREATE_MISTLETOE,
            SPELL_CREATE_HOLLY,
            SPELL_CREATE_SNOWFLAKES
        });
    }

    /**
     * @brief 处理脚本效果
     *
     * 随机选择一种节日物品并施放在目标玩家身上。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* target = GetHitPlayer())
        {
            // 随机选择一种节日物品：冬青、槲寄生或雪花
            uint32 spellId = RAND(SPELL_CREATE_HOLLY, SPELL_CREATE_MISTLETOE, SPELL_CREATE_SNOWFLAKES);
            GetCaster()->CastSpell(target, spellId, true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_winter_veil_mistletoe::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief PX-238冬意发射器变形法术ID枚举
 *
 * 定义了冬意发射器使用的四种不同的小矮人造型法术。
 * 每种法术对应不同的外观变化。
 */
enum PX238WinterWondervolt
{
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_1  = 26157,  ///< 变形效果1
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_2  = 26272,  ///< 变形效果2
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_3  = 26273,  ///< 变形效果3
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_4  = 26274   ///< 变形效果4
};

/**
 * @brief 冬意发射器变形法术ID数组
 *
 * 存储所有变形效果法术ID，用于随机选择变形类型。
 */
std::array<uint32, 4> const WonderboltTransformSpells =
{
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_1,
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_2,
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_3,
    SPELL_PX_238_WINTER_WONDERVOLT_TRANSFORM_4
};

/**
 * @brief PX-238冬意发射器陷阱法术脚本 (Spell ID: 26275)
 *
 * 当玩家经过冬意发射器陷阱时触发。将玩家变成随机的小矮人造型。
 * 如果玩家已经处于某种变形状态，则不会再触发新的变形。
 *
 * 游戏设计：增加节日的趣味性和社交互动，玩家可以在发射器旁
 * 看到其他玩家变成小矮人的滑稽样子。
 */
// 26275 - PX-238 Winter Wondervolt TRAP
class spell_winter_veil_px_238_winter_wondervolt : public SpellScript
{
    PrepareSpellScript(spell_winter_veil_px_238_winter_wondervolt);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(WonderboltTransformSpells);
    }

    /**
     * @brief 处理脚本效果
     *
     * 检查目标是否已变形，如果未变形则随机施放一种变形效果。
     *
     * @param effIndex 法术效果索引
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        if (Unit* target = GetHitUnit())
        {
            // 检查目标是否已经处于任何变形状态
            for (uint32 spell : WonderboltTransformSpells)
                if (target->HasAura(spell))
                    return;  // 已变形，不再重复施加

            // 随机选择一种变形效果并施放
            target->CastSpell(target, Trinity::Containers::SelectRandomContainerElement(WonderboltTransformSpells), true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_winter_veil_px_238_winter_wondervolt::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 驯鹿变形法术ID枚举
 *
 * 定义了将玩家坐骑变成驯鹿的各种法术ID。
 * 根据原坐骑的速度和类型，会使用不同的驯鹿变形法术。
 */
enum ReindeerTransformation
{
    SPELL_FLYING_REINDEER_310                   = 44827,  ///< 飞行驯鹿（310%速度）
    SPELL_FLYING_REINDEER_280                   = 44825,  ///< 飞行驯鹿（280%速度）
    SPELL_FLYING_REINDEER_60                    = 44824,  ///< 飞行驯鹿（60%速度）
    SPELL_REINDEER_100                          = 25859,  ///< 地面驯鹿（100%速度）
    SPELL_REINDEER_60                           = 25858,  ///< 地面驯鹿（60%速度）
};

/**
 * @brief 驯鹿变形法术脚本 (Spell ID: 25860)
 *
 * 将玩家的当前坐骑变成冬幕节主题的驯鹿造型。
 * 根据原坐骑的速度和类型，自动选择对应速度的驯鹿：
 * - 飞行坐骑 -> 飞行驯鹿（保持原有速度等级）
 * - 地面坐骑 -> 地面驯鹿（保持原有速度等级）
 *
 * 这是冬幕节的经典功能，让玩家可以骑着驯鹿在艾泽拉斯冒险，
 * 就像圣诞老人一样。
 */
// 25860 - Reindeer Transformation
class spell_winter_veil_reindeer_transformation : public SpellScript
{
    PrepareSpellScript(spell_winter_veil_reindeer_transformation);

    /**
     * @brief 验证法术依赖
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_FLYING_REINDEER_310,
            SPELL_FLYING_REINDEER_280,
            SPELL_FLYING_REINDEER_60,
            SPELL_REINDEER_100,
            SPELL_REINDEER_60
        });
    }

    /**
     * @brief 处理虚拟效果
     *
     * 根据玩家当前坐骑的速度和类型，施放对应的驯鹿变形法术。
     *
     * @param effIndex 法术效果索引
     *
     * 逻辑流程：
     * 1. 检查玩家是否骑乘坐骑
     * 2. 获取坐骑的飞行速度和奔跑速度
     * 3. 移除当前坐骑
     * 4. 根据速度等级施放对应的驯鹿法术
     */
    void HandleDummy(SpellEffIndex /* effIndex */)
    {
        Unit* caster = GetCaster();

        // 检查玩家是否骑乘坐骑
        if (caster->HasAuraType(SPELL_AURA_MOUNTED))
        {
            // 获取当前坐骑的速度
            float flyspeed = caster->GetSpeedRate(MOVE_FLIGHT);
            float speed = caster->GetSpeedRate(MOVE_RUN);

            // 移除当前坐骑光环
            caster->RemoveAurasByType(SPELL_AURA_MOUNTED);
            // 根据坐骑速度和类型选择对应的驯鹿变形法术
            // 共5种不同的法术，取决于坐骑速度和是否能飞行

            if (flyspeed >= 4.1f)
                // 310%飞行速度 - 飞行驯鹿（最快）
                caster->CastSpell(caster, SPELL_FLYING_REINDEER_310, true);
            else if (flyspeed >= 3.8f)
                // 280%飞行速度 - 飞行驯鹿（快速）
                caster->CastSpell(caster, SPELL_FLYING_REINDEER_280, true);
            else if (flyspeed >= 1.6f)
                // 60%飞行速度 - 飞行驯鹿（普通）
                caster->CastSpell(caster, SPELL_FLYING_REINDEER_60, true);
            else if (speed >= 2.0f)
                // 100%地面速度 - 地面驯鹿（快速）
                caster->CastSpell(caster, SPELL_REINDEER_100, true);
            else
                // 60%地面速度 - 地面驯鹿（普通）
                caster->CastSpell(caster, SPELL_REINDEER_60, true);
        }
    }

    /**
     * @brief 注册法术效果回调
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_winter_veil_reindeer_transformation::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册所有冬幕节脚本
 *
 * 此函数在服务器启动时被调用，用于注册本文件中定义的所有法术脚本。
 * 将脚本与对应的法术ID关联起来，使游戏能够正确处理冬幕节相关的法术效果。
 */
void AddSC_event_winter_veil()
{
    // 槲寄生系统
    RegisterSpellScript(spell_winter_veil_mistletoe);

    // 冬意发射器系统
    RegisterSpellScript(spell_winter_veil_px_238_winter_wondervolt);

    // 驯鹿变形系统
    RegisterSpellScript(spell_winter_veil_reindeer_transformation);
}
