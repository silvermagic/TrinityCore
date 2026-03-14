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
 * @file StatSystem.cpp
 * @brief 单位属性系统实现
 *
 * 本模块负责实现游戏中所有单位（玩家、生物、宠物）的属性计算和管理。
 * 主要功能包括：
 *   - 基础属性（力量、敏捷、耐力、智力、精神）的计算与更新
 *   - 攻击强度、法术强度、护甲、抗性等衍生属性的计算
 *   - 生命值、法力值等资源的最大值计算
 *   - 暴击、闪避、招架、格挡等战斗属性的百分比计算
 *   - 资源回复速率的计算（生命回复、法力回复等）
 *   - 宠物和守护者从主人处获得的属性加成计算
 *
 * 属性计算采用分层结构：
 *   - Unit 基类：提供通用的属性更新框架
 *   - Player 类：实现玩家特有的属性计算公式
 *   - Creature 类：实现生物特有的属性计算
 *   - Guardian 类：实现守护者/宠物特有的属性继承机制
 *
 * 性能注意事项：
 *   - 属性更新可能触发连锁更新，应避免频繁调用
 *   - 使用 UnitMods 系统管理属性修饰符，避免重复计算
 *   - 递减收益公式用于高属性值时的平衡
 */

#include "Unit.h"
#include "Creature.h"
#include "Item.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "World.h"
#include <numeric>

/**
 * @brief 修改 uint32 值的内部辅助函数
 *
 * 职责：
 *   安全地对 uint32 值进行加减操作，处理负数和溢出情况。
 *
 * @param apply     true 表示应用加成，false 表示移除加成
 * @param baseValue 要修改的基础值（引用）
 * @param amount    要增减的数值（引用，可能被修改）
 *
 * @return 实际应用的操作类型（apply 参数可能因负数被反转）
 *
 * 处理逻辑：
 *   1. 如果 amount 为负数，反转 apply 并取 amount 的绝对值
 *   2. 如果 apply 为 true，执行加法
 *   3. 如果 apply 为 false，执行减法，并确保不会下溢
 *
 * 性能说明：
 *   内联函数，频繁调用，需要保持简洁高效。
 */
inline bool _ModifyUInt32(bool apply, uint32& baseValue, int32& amount)
{
    // 如果数值为负，反转操作类型并取绝对值
    // 例如：apply=true, amount=-5 等同于 apply=false, amount=5
    if (amount < 0)
    {
        apply = !apply;
        amount = -amount;
    }
    if (apply)
        baseValue += amount;
    else
    {
        // 确保不会发生 uint32 下溢（下溢是未定义行为）
        if (amount > int32(baseValue))
            amount = baseValue;
        baseValue -= amount;
    }
    return apply;
}

/*#######################################
########                         ########
########    UNIT STAT SYSTEM     ########
########                         ########
#######################################*/

/**
 * @brief 更新所有抗性值
 *
 * 职责：
 *   更新单位的所有魔法抗性（物理、神圣、火焰、自然、冰霜、暗影）。
 *
 * 调用时机：
 *   - 装备改变时
 *   - 光环效果改变时
 *   - 等级提升时
 *
 * 性能说明：
 *   遍历所有魔法学校，复杂度 O(MAX_SPELL_SCHOOL)。
 *   可能触发网络同步，不宜频繁调用。
 */
void Unit::UpdateAllResistances()
{
    for (uint8 i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);
}

/**
 * @brief 更新物理伤害值
 *
 * 职责：
 *   计算并更新单位的主手、副手或远程攻击的物理伤害范围。
 *
 * @param attType 攻击类型（主手、副手、远程）
 *
 * 主要流程：
 *   1. 遍历所有物品原型伤害索引（用于多属性武器）
 *   2. 累加每种伤害类型的最小和最大伤害
 *   3. 根据攻击类型更新对应的字段值
 *
 * 性能说明：
 *   涉及多次 CalculateMinMaxDamage 调用，计算开销较大。
 */
void Unit::UpdateDamagePhysical(WeaponAttackType attType)
{
    float totalMin = 0.f;
    float totalMax = 0.f;

    float tmpMin, tmpMax;
    // 累加所有伤害类型（用于多属性武器，如火焰伤害剑）
    for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
    {
        CalculateMinMaxDamage(attType, false, true, tmpMin, tmpMax, i);
        totalMin += tmpMin;
        totalMax += tmpMax;
    }

    // 根据攻击类型更新对应的字段
    switch (attType)
    {
        case BASE_ATTACK:
        default:
            SetStatFloatValue(UNIT_FIELD_MINDAMAGE, totalMin);
            SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, totalMax);
            break;
        case OFF_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, totalMin);
            SetStatFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, totalMax);
            break;
        case RANGED_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, totalMin);
            SetStatFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, totalMax);
            break;
    }
}

/*#######################################
########                         ########
########   PLAYERS STAT SYSTEM   ########
########                         ########
#######################################*/

/**
 * @brief 更新玩家指定属性
 *
 * 职责：
 *   计算并更新玩家的单一属性值，并触发相关的衍生属性更新。
 *
 * @param stat 要更新的属性类型（力量、敏捷、耐力、智力、精神）
 *
 * @return 更新是否成功
 *
 * 主要流程：
 *   1. 计算属性总值（基础值 * 基础百分比 + 总值）* 总百分比
 *   2. 设置属性值
 *   3. 如果是耐力/智力/力量，同步更新宠物属性
 *   4. 根据属性类型触发相关更新：
 *      - 力量：盾牌格挡值
 *      - 敏捷：护甲、暴击、闪避
 *      - 耐力：最大生命值
 *      - 智力：最大法力值、法术暴击、护甲（特殊光环）
 *   5. 更新攻击强度（近战/远程）
 *   6. 更新法术伤害和治疗加成
 *   7. 更新法力回复
 *   8. 更新从属性获得的战斗等级
 *
 * 调用时机：
 *   - 装备穿脱时
 *   - 属性增益/减益光环应用/移除时
 *   - 等级提升时
 */
bool Player::UpdateStats(Stats stat)
{
    if (stat > STAT_SPIRIT)
        return false;

    // 属性值计算公式：((基础值 * 基础百分比) + 总值) * 总百分比
    float value  = GetTotalStatValue(stat);

    SetStat(stat, int32(value));

    // 耐力、智力、力量会影响宠物属性，需要同步更新
    if (stat == STAT_STAMINA || stat == STAT_INTELLECT || stat == STAT_STRENGTH)
    {
        Pet* pet = GetPet();
        if (pet)
            pet->UpdateStats(stat);
    }

    // 根据属性类型触发相关更新
    switch (stat)
    {
        case STAT_STRENGTH:
            UpdateShieldBlockValue();  // 力量影响盾牌格挡值
            break;
        case STAT_AGILITY:
            UpdateArmor();              // 敏捷提供护甲
            UpdateAllCritPercentages(); // 敏捷提供暴击
            UpdateDodgePercentage();    // 敏捷提供闪避
            break;
        case STAT_STAMINA:
            UpdateMaxHealth();          // 耐力提供生命值
            break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);     // 智力提供法力值
            UpdateAllSpellCritChances();    // 智力提供法术暴击
            UpdateArmor();                  // 某些天赋可使智力提供护甲（SPELL_AURA_MOD_RESISTANCE_OF_INTELLECT_PERCENT）
            break;
        case STAT_SPIRIT:
            break;
        default:
            break;
    }

    // 更新近战攻击强度
    if (stat == STAT_STRENGTH)
    {
        UpdateAttackPowerAndDamage(false);
        // 检查是否有从力量获得远程攻击强度的光环
        if (HasAuraTypeWithMiscvalue(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT, stat))
            UpdateAttackPowerAndDamage(true);
    }
    else if (stat == STAT_AGILITY)
    {
        // 敏捷同时影响近战和远程攻击强度
        UpdateAttackPowerAndDamage(false);
        UpdateAttackPowerAndDamage(true);
    }
    else
    {
        // 检查是否有从其他属性获得攻击强度的光环
        if (HasAuraTypeWithMiscvalue(SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT, stat))
            UpdateAttackPowerAndDamage(false);
        if (HasAuraTypeWithMiscvalue(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT, stat))
            UpdateAttackPowerAndDamage(true);
    }

    UpdateSpellDamageAndHealingBonus();
    UpdatePowerRegen(POWER_MANA);

    // 更新从属性获得的战斗等级（如智力提供法术穿透等级的天赋）
    uint32 mask = 0;
    AuraEffectList const& modRatingFromStat = GetAuraEffectsByType(SPELL_AURA_MOD_RATING_FROM_STAT);
    for (AuraEffectList::const_iterator i = modRatingFromStat.begin(); i != modRatingFromStat.end(); ++i)
        if (Stats((*i)->GetMiscValueB()) == stat)
            mask |= (*i)->GetMiscValue();
    if (mask)
    {
        for (uint32 rating = 0; rating < MAX_COMBAT_RATING; ++rating)
            if (mask & (1 << rating))
                ApplyRatingMod(CombatRating(rating), 0, true);
    }
    return true;
}

/**
 * @brief 应用法术强度加成
 *
 * 职责：
 *   修改玩家的基础法术强度，并同步更新客户端显示字段。
 *
 * @param amount 法术强度变化量
 * @param apply  true 表示应用加成，false 表示移除加成
 *
 * 说明：
 *   法术强度同时影响治疗和各魔法学校的伤害加成。
 *   为性能考虑，直接更新客户端字段而不重新计算全部属性。
 */
void Player::ApplySpellPowerBonus(int32 amount, bool apply)
{
    apply = _ModifyUInt32(apply, m_baseSpellPower, amount);

    // 快速更新客户端显示值，避免完整重新计算
    ApplyModUInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, amount, apply);
    for (int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        ApplyModUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i, amount, apply);
}

/**
 * @brief 更新法术伤害和治疗加成
 *
 * 职责：
 *   计算并更新玩家的所有魔法学校的伤害加成和治疗加成。
 *
 * 主要流程：
 *   1. 计算总治疗加成
 *   2. 分别计算每个魔法学校的伤害加成（正向和负向）
 *
 * 说明：
 *   这些值仅用于客户端显示，实际伤害计算在 Unit::SpellDamageBonusDone 中进行。
 */
void Player::UpdateSpellDamageAndHealingBonus()
{
    // 魔法伤害修饰符在 Unit::SpellDamageBonusDone 中实现
    // 此信息仅供客户端显示使用
    // 获取所有魔法学校的治疗加成
    SetStatInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL));
    // 获取所有魔法学校的伤害加成
    Unit::AuraEffectList const& modDamageAuras = GetAuraEffectsByType(SPELL_AURA_MOD_DAMAGE_DONE);
    for (uint16 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
    {
        // 计算负向伤害修饰符
        SetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + i, std::accumulate(modDamageAuras.begin(), modDamageAuras.end(), 0, [i](int32 negativeMod, AuraEffect const* aurEff)
        {
            if (aurEff->GetAmount() < 0 && aurEff->GetMiscValue() & (1 << i))
                negativeMod += aurEff->GetAmount();
            return negativeMod;
        }));
        // 正向伤害修饰符 = 总伤害加成 - 负向修饰符
        SetStatInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + i, SpellBaseDamageBonusDone(SpellSchoolMask(1 << i)) - GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + i));
    }
}

/**
 * @brief 更新所有属性
 *
 * 职责：
 *   全面更新玩家的所有属性，通常在重大状态变化时调用。
 *
 * @return 始终返回 true
 *
 * 主要流程：
 *   1. 设置所有基础属性
 *   2. 更新护甲、攻击强度
 *   3. 更新生命值和所有能量值上限
 *   4. 更新所有战斗等级
 *   5. 更新暴击、法术暴击、防御技能相关
 *   6. 更新盾牌格挡值、法术伤害加成
 *   7. 更新所有能量回复
 *   8. 更新熟练度和护甲穿透
 *   9. 更新所有抗性
 *
 * 调用时机：
 *   - 玩家登录时
 *   - 等级提升时
 *   - 职业切换时
 *   - 重置天赋后
 *
 * 性能说明：
 *   这是最重的属性更新函数，触发大量衍生计算，应谨慎调用。
 */
bool Player::UpdateAllStats()
{
    // 设置所有基础属性
    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        float value = GetTotalStatValue(Stats(i));
        SetStat(Stats(i), int32(value));
    }

    // 更新衍生属性
    UpdateArmor();
    UpdateAttackPowerAndDamage(false);
    UpdateAttackPowerAndDamage(true);
    UpdateMaxHealth();

    // 更新所有能量值上限
    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    // 更新所有战斗相关属性
    UpdateAllRatings();
    UpdateAllCritPercentages();
    UpdateAllSpellCritChances();
    UpdateDefenseBonusesMod();
    UpdateShieldBlockValue();
    UpdateSpellDamageAndHealingBonus();

    // 更新能量回复（不同能量类型）
    UpdatePowerRegen(POWER_MANA);
    UpdatePowerRegen(POWER_RAGE);
    UpdatePowerRegen(POWER_ENERGY);
    UpdatePowerRegen(POWER_RUNIC_POWER);

    // 更新熟练度和护甲穿透
    UpdateExpertise(BASE_ATTACK);
    UpdateExpertise(OFF_ATTACK);
    RecalculateRating(CR_ARMOR_PENETRATION);
    UpdateAllResistances();

    return true;
}

/**
 * @brief 应用法术穿透加成
 *
 * 职责：
 *   修改玩家的法术穿透属性，降低目标的抗性。
 *
 * @param amount 法术穿透变化量
 * @param apply  true 表示应用加成，false 表示移除加成
 *
 * 说明：
 *   法术穿透降低目标的抗性，通过修改 PLAYER_FIELD_MOD_TARGET_RESISTANCE 实现。
 */
void Player::ApplySpellPenetrationBonus(int32 amount, bool apply)
{
    ApplyModInt32Value(PLAYER_FIELD_MOD_TARGET_RESISTANCE, -amount, apply);
    m_spellPenetrationItemMod += apply ? amount : -amount;
}

/**
 * @brief 更新指定魔法学校的抗性
 *
 * @param school 魔法学校索引（物理为0，其他为魔法学校）
 *
 * 主要流程：
 *   1. 对于魔法学校（> 0），计算总抗性值并设置
 *   2. 对于物理（= 0），更新护甲
 *   3. 如果有宠物，同步更新宠物的抗性
 */
void Player::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));

        // 宠物继承主人的部分抗性
        Pet* pet = GetPet();
        if (pet)
            pet->UpdateResistances(school);
    }
    else
        UpdateArmor();
}

/**
 * @brief 更新护甲值
 *
 * 职责：
 *   计算玩家的总护甲值，包括装备护甲、敏捷加成、特殊光环等。
 *
 * 护甲计算公式：
 *   ((基础护甲 * 基础百分比) + 敏捷*2 + 总值 + 动态加成) * 总百分比
 *
 * 动态加成：
 *   - 敏捷提供的护甲（每点敏捷2点护甲）
 *   - 特殊光环（如智力转护甲的天赋）
 *
 * 性能说明：
 *   涉及光环遍历，复杂度取决于光环数量。
 */
void Player::UpdateArmor()
{
    UnitMods unitMod = UNIT_MOD_ARMOR;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE);    // 基础护甲（来自装备）
    value *= GetPctModifierValue(unitMod, BASE_PCT);            // 护甲百分比加成
    value += GetStat(STAT_AGILITY) * 2.0f;                      // 敏捷提供护甲（每点敏捷2点护甲）
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE);

    // 添加动态固定值加成（如智力转护甲的光环）
    AuraEffectList const& mResbyIntellect = GetAuraEffectsByType(SPELL_AURA_MOD_RESISTANCE_OF_STAT_PERCENT);
    for (AuraEffectList::const_iterator i = mResbyIntellect.begin(); i != mResbyIntellect.end(); ++i)
    {
        if ((*i)->GetMiscValue() & SPELL_SCHOOL_MASK_NORMAL)
            value += CalculatePct(GetStat(Stats((*i)->GetMiscValueB())), (*i)->GetAmount());
    }

    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));

    // 宠物继承主人的部分护甲
    Pet* pet = GetPet();
    if (pet)
        pet->UpdateArmor();
}

/**
 * @brief 从耐力计算生命值加成
 *
 * @return 从耐力获得的生命值加成
 *
 * 计算公式：
 *   前20点耐力：每点提供1点生命值
 *   超过20点的耐力：每点提供10点生命值
 *
 * 说明：
 *   这是魔兽世界的经典设计，确保低等级玩家有足够的生命值。
 */
float Player::GetHealthBonusFromStamina()
{
    float stamina = GetStat(STAT_STAMINA);
    float baseStam = std::min(20.0f, stamina);  // 前20点耐力
    float moreStam = stamina - baseStam;        // 超过20点的耐力

    return baseStam + (moreStam*10.0f);
}

/**
 * @brief 从智力计算法力值加成
 *
 * @return 从智力获得的法力值加成
 *
 * 计算公式：
 *   前20点智力：每点提供1点法力值
 *   超过20点的智力：每点提供15点法力值
 *
 * 说明：
 *   与耐力的设计类似，确保低等级施法职业有足够的法力值。
 */
float Player::GetManaBonusFromIntellect()
{
    float intellect = GetStat(STAT_INTELLECT);

    float baseInt = std::min(20.0f, intellect);   // 前20点智力
    float moreInt = intellect - baseInt;          // 超过20点的智力

    return baseInt + (moreInt * 15.0f);
}

/**
 * @brief 更新最大生命值
 *
 * 职责：
 *   计算并更新玩家的最大生命值。
 *
 * 计算公式：
 *   ((基础值 + 创建生命值) * 基础百分比 + 总值 + 耐力加成) * 总百分比
 */
void Player::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + GetHealthBonusFromStamina();
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

/**
 * @brief 更新最大能量值
 *
 * @param power 能量类型（法力、怒气、能量、符文能量等）
 *
 * 主要流程：
 *   1. 根据能量类型获取对应的 UnitMod
 *   2. 计算并设置最大值
 *   3. 对于法力值，额外计算智力加成
 */
void Player::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    // 法力值有智力加成
    float bonusPower = (power == POWER_MANA && GetCreatePowerValue(power) > 0) ? GetManaBonusFromIntellect() : 0;

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) +  bonusPower;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(std::lroundf(value)));
}

/**
 * @brief 应用野性攻击强度加成
 *
 * @param amount 攻击强度变化量
 * @param apply  true 表示应用加成，false 表示移除加成
 *
 * 说明：
 *   野性攻击强度是德鲁伊豹形态和熊形态特有的机制，
 *   允许武器攻击强度在这些形态下生效。
 */
void Player::ApplyFeralAPBonus(int32 amount, bool apply)
{
    _ModifyUInt32(apply, m_baseFeralAP, amount);
    UpdateAttackPowerAndDamage();
}

/**
 * @brief 更新攻击强度和伤害
 *
 * @param ranged true 表示更新远程攻击强度，false 表示更新近战攻击强度
 *
 * 职责：
 *   根据职业、等级、属性计算基础攻击强度，并应用各种加成。
 *
 * 近战攻击强度计算公式（各职业不同）：
 *   - 战士/圣骑士/死亡骑士：等级*3 + 力量*2 - 20
 *   - 盗贼/猎人/萨满：等级*2 + 力量 + 敏捷 - 20
 *   - 法师/牧师/术士：力量 - 10
 *   - 德鲁伊：根据形态不同有多种公式
 *
 * 远程攻击强度计算公式：
 *   - 猎人：等级*2 + 敏捷 - 10
 *   - 盗贼/战士：等级 + 敏捷 - 10
 *   - 其他：敏捷 - 10
 *
 * 德鲁伊特殊处理：
 *   - 猫形态：等级*等级加成 + 力量*2 + 敏捷 - 20 + 武器加成 + 野性AP
 *   - 熊形态：等级*等级加成 + 力量*2 - 20 + 武器加成 + 野性AP
 *   - 枭兽形态：力量*2 - 20 + 野性AP
 *   - 掠夺打击天赋可提供等级和武器加成
 *
 * 性能说明：
 *   涉及光环遍历，计算完成后会更新武器伤害和宠物攻击强度。
 */
void Player::UpdateAttackPowerAndDamage(bool ranged)
{
    float val2 = 0.0f;
    float level = float(GetLevel());

    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    // 根据职业和攻击类型计算基础攻击强度
    if (ranged)
    {
        // 远程攻击强度计算
        switch (GetClass())
        {
            case CLASS_HUNTER:
                val2 = level * 2.0f + GetStat(STAT_AGILITY) - 10.0f;
                break;
            case CLASS_ROGUE:
                val2 = level + GetStat(STAT_AGILITY) - 10.0f;
                break;
            case CLASS_WARRIOR:
                val2 = level + GetStat(STAT_AGILITY) - 10.0f;
                break;
            case CLASS_DRUID:
                // 德鲁伊在豹/熊形态下远程攻击强度为0
                switch (GetShapeshiftForm())
                {
                    case FORM_CAT:
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = 0.0f; break;
                    default:
                        val2 = GetStat(STAT_AGILITY) - 10.0f; break;
                }
                break;
            default: val2 = GetStat(STAT_AGILITY) - 10.0f; break;
        }
    }
    else
    {
        // 近战攻击强度计算
        switch (GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DEATH_KNIGHT:
                // 力量型职业：等级*3 + 力量*2 - 20
                val2 = level * 3.0f + GetStat(STAT_STRENGTH) * 2.0f - 20.0f;
                break;
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                // 敏捷型职业：等级*2 + 力量 + 敏捷 - 20
                val2 = level * 2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f;
                break;
            case CLASS_DRUID:
            {
                // 德鲁伊特殊处理：掠夺打击天赋和野性形态
                float levelBonus = 0.0f;
                float weaponBonus = 0.0f;
                if (IsInFeralForm())
                {
                    // 掠夺打击天赋：等级加成
                    if (AuraEffect const* levelMod = GetAuraEffect(SPELL_AURA_DUMMY, SPELLFAMILY_DRUID, 1563, EFFECT_0))
                        levelBonus = CalculatePct(1.0f, levelMod->GetAmount());

                    // 掠夺打击天赋：武器加成（如果卸下武器，加成为0，使用模板值）
                    if (m_baseFeralAP)
                    {
                        if (Item const* weapon = m_items[EQUIPMENT_SLOT_MAINHAND])
                        {
                            if (AuraEffect const* weaponMod = GetAuraEffect(SPELL_AURA_DUMMY, SPELLFAMILY_DRUID, 1563, EFFECT_1))
                            {
                                ItemTemplate const* itemTemplate = weapon->GetTemplate();
                                int32 bonusAP = itemTemplate->GetTotalAPBonus() + m_baseFeralAP;
                                weaponBonus = CalculatePct(static_cast<float>(bonusAP), weaponMod->GetAmount());
                            }
                        }
                    }
                }

                // 根据形态选择计算公式
                switch (GetShapeshiftForm())
                {
                    case FORM_CAT:
                        val2 = GetLevel() * levelBonus + GetStat(STAT_STRENGTH) * 2.0f + GetStat(STAT_AGILITY) - 20.0f + weaponBonus + m_baseFeralAP;
                        break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = GetLevel() * levelBonus + GetStat(STAT_STRENGTH) * 2.0f - 20.0f + weaponBonus + m_baseFeralAP;
                        break;
                    case FORM_MOONKIN:
                        val2 = GetStat(STAT_STRENGTH) * 2.0f - 20.0f + m_baseFeralAP;
                        break;
                    default:
                        val2 = GetStat(STAT_STRENGTH) * 2.0f - 20.0f;
                        break;
                }
                break;
            }
            case CLASS_MAGE:
            case CLASS_PRIEST:
            case CLASS_WARLOCK:
                // 法师职业：力量 - 10
                val2 = GetStat(STAT_STRENGTH) - 10.0f;
                break;
        }
    }

    // 设置基础攻击强度值
    SetStatFlatModifier(unitMod, BASE_VALUE, val2);

    // 计算最终攻击强度
    float base_attPower  = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetFlatModifierValue(unitMod, TOTAL_VALUE);

    // 添加动态固定值加成
    if (ranged)
    {
        // 远程攻击强度从属性获得的加成（魔杖使用者除外）
        if ((GetClassMask() & CLASSMASK_WAND_USERS) == 0)
        {
            AuraEffectList const& mRAPbyStat = GetAuraEffectsByType(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT);
            for (AuraEffect const* aurEff : mRAPbyStat)
                attPowerMod += CalculatePct(GetStat(Stats(aurEff->GetMiscValue())), aurEff->GetAmount());
        }
    }
    else
    {
        // 近战攻击强度从属性获得的加成
        AuraEffectList const& mAPbyStat = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT);
        for (AuraEffect const* aurEff : mAPbyStat)
            attPowerMod += CalculatePct(GetStat(Stats(aurEff->GetMiscValue())), aurEff->GetAmount());
    }

    // 从护甲获得的攻击强度（如萨满的雷霆风暴天赋，每30秒更新一次）
    attPowerMod += GetTotalAuraModifier(SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR);

    float attPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    // 设置最终攻击强度值
    if (ranged)
    {
        SetRangedAttackPower(int32(base_attPower));
        if (attPowerMod >= 0)
            SetRangedAttackPowerModPos(int32(attPowerMod));
        if (attPowerMod <= 0)
            SetRangedAttackPowerModNeg(int32(attPowerMod));
        SetRangedAttackPowerMultiplier(attPowerMultiplier);
    }
    else
    {
        SetAttackPower(int32(base_attPower));
        if (attPowerMod >= 0)
            SetAttackPowerModPos(int32(attPowerMod));
        if (attPowerMod <= 0)
            SetAttackPowerModNeg(int32(attPowerMod));
        SetAttackPowerMultiplier(attPowerMultiplier);
    }

    // 更新宠物和守护者的攻击强度
    Pet* pet = GetPet();
    Guardian* guardian = GetGuardianPet();

    // 攻击强度变化后自动更新武器伤害
    if (ranged)
    {
        UpdateDamagePhysical(RANGED_ATTACK);
        // 远程攻击强度变化影响猎人宠物
        if (pet && pet->IsHunterPet())
            pet->UpdateAttackPowerAndDamage();
    }
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        // 双持且有副手武器时更新副手伤害
        if (CanDualWield() && haveOffhandWeapon())
            UpdateDamagePhysical(OFF_ATTACK);
        // 萨满和圣骑士的智力转法术强度天赋
        if (GetClass() == CLASS_SHAMAN || GetClass() == CLASS_PALADIN)
            UpdateSpellDamageAndHealingBonus();

        // 近战攻击强度变化影响死亡骑士宠物
        if (pet && (pet->IsPetGhoul() || pet->IsRisenAlly()))
            pet->UpdateAttackPowerAndDamage();

        // 近战攻击强度变化影响萨满的幽灵狼
        if (guardian && guardian->IsSpiritWolf())
            guardian->UpdateAttackPowerAndDamage();
    }
}

/**
 * @brief 更新盾牌格挡值
 *
 * 职责：
 *   更新玩家客户端显示的盾牌格挡值。
 *
 * 说明：
 *   盾牌格挡值主要由力量和盾牌装备属性决定。
 */
void Player::UpdateShieldBlockValue()
{
    SetUInt32Value(PLAYER_SHIELD_BLOCK, GetShieldBlockValue());
}

/**
 * @brief 计算武器伤害范围
 *
 * @param attType      攻击类型（主手、副手、远程）
 * @param normalized   是否使用标准化计算（用于技能伤害）
 * @param addTotalPct  是否添加总百分比修饰符
 * @param minDamage    [out] 最小伤害
 * @param maxDamage    [out] 最大伤害
 * @param damageIndex  伤害索引（用于多属性武器）
 *
 * 主要流程：
 *   1. 如果是特殊伤害索引（非0），只返回武器原型伤害
 *   2. 计算基础伤害值 = 修饰符基础值 + 攻强/14 * 标准化系数
 *   3. 获取武器基础伤害范围
 *   4. 处理特殊情况：
 *      - 德鲁伊形态：使用等级公式代替武器伤害
 *      - 缴械状态：使用基础徒手伤害
 *      - 远程攻击：添加弹药DPS
 *   5. 应用百分比修饰符计算最终伤害
 */
void Player::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex) const
{
    // 特殊伤害索引：仅返回武器原型伤害，不受任何修饰符影响
    if (damageIndex != 0)
    {
        minDamage = 0.0f;
        maxDamage = 0.0f;

        if (!IsInFeralForm() && CanUseAttackType(attType))
        {
            minDamage = GetWeaponDamageRange(attType, MINDAMAGE, damageIndex);
            maxDamage = GetWeaponDamageRange(attType, MAXDAMAGE, damageIndex);
        }
        return;
    }

    UnitMods unitMod;

    switch (attType)
    {
        case BASE_ATTACK:
        default:
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    // 标准化攻击速度系数，最小0.25
    float const attackPowerMod = std::max(GetAPMultiplier(attType, normalized), 0.25f);

    // 计算基础伤害值
    float baseValue  = GetFlatModifierValue(unitMod, BASE_VALUE);
    baseValue += GetTotalAttackPowerValue(attType) / 14.0f * attackPowerMod;

    float basePct    = GetPctModifierValue(unitMod, BASE_PCT);
    float totalValue = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float totalPct   = addTotalPct ? GetPctModifierValue(unitMod, TOTAL_PCT) : 1.0f;

    // 获取武器基础伤害范围
    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    // 德鲁伊形态特殊处理：使用等级公式代替武器伤害
    if (IsInFeralForm())
    {
        uint8 lvl = GetLevel();
        if (lvl > 60)
            lvl = 60;  // 60级以上使用60级公式

        weaponMinDamage = lvl * 0.85f * attackPowerMod;
        weaponMaxDamage = lvl * 1.25f * attackPowerMod;
    }
    else if (!CanUseAttackType(attType)) // 缴械状态：非德鲁伊形态但无法使用武器
    {
        // 远程或副手无法使用时，伤害为0
        if (attType != BASE_ATTACK)
        {
            minDamage = 0.f;
            maxDamage = 0.f;
            return;
        }

        // 主手缴械：使用基础徒手伤害
        weaponMinDamage = BASE_MINDAMAGE;
        weaponMaxDamage = BASE_MAXDAMAGE;
    }
    else if (attType == RANGED_ATTACK) // 远程攻击：添加弹药DPS
    {
        weaponMinDamage += GetAmmoDPS() * attackPowerMod;
        weaponMaxDamage += GetAmmoDPS() * attackPowerMod;
    }

    // 计算最终伤害范围
    minDamage = ((weaponMinDamage + baseValue) * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue) * basePct + totalValue) * totalPct;
}

/**
 * @brief 更新所有防御技能加成
 *
 * 职责：
 *   更新格挡、招架、闪避百分比。
 *
 * 调用时机：
 *   - 防御技能改变时
 *   - 装备改变时
 *   - 相关光环改变时
 */
void Player::UpdateDefenseBonusesMod()
{
    UpdateBlockPercentage();
    UpdateParryPercentage();
    UpdateDodgePercentage();
}

/**
 * @brief 更新格挡百分比
 *
 * 职责：
 *   计算并更新玩家的格挡几率。
 *
 * 计算公式：
 *   基础值(5%) + 防御技能差值*0.04% + 光环加成 + 等级加成
 *
 * 说明：
 *   - 只有装备盾牌才能格挡
 *   - 防御技能差值 = 当前防御技能 - 同等级最大防御技能
 *   - 可配置上限限制
 */
void Player::UpdateBlockPercentage()
{
    float value = 0.0f;
    if (CanBlock())
    {
        // 基础值：5%
        value = 5.0f;
        // 防御技能加成：每点技能差值增加0.04%
        value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        // 光环加成
        value += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
        // 等级加成
        value += GetRatingBonusValue(CR_BLOCK);

        // 应用配置上限
        if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
             value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_BLOCK) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_BLOCK) : value;

        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_BLOCK_PERCENTAGE, value);
}

/**
 * @brief 更新暴击百分比
 *
 * @param attType 攻击类型（主手、副手、远程）
 *
 * 计算公式：
 *   固定加成 + 敏捷百分比加成 + 等级加成 + 武器技能差值*0.04%
 */
void Player::UpdateCritPercentage(WeaponAttackType attType)
{
    BaseModGroup modGroup;
    uint16 index;
    CombatRating cr;

    switch (attType)
    {
        case OFF_ATTACK:
            modGroup = OFFHAND_CRIT_PERCENTAGE;
            index = PLAYER_OFFHAND_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
        case RANGED_ATTACK:
            modGroup = RANGED_CRIT_PERCENTAGE;
            index = PLAYER_RANGED_CRIT_PERCENTAGE;
            cr = CR_CRIT_RANGED;
            break;
        case BASE_ATTACK:
        default:
            modGroup = CRIT_PERCENTAGE;
            index = PLAYER_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
    }

    // 固定加成（光环）+ 敏捷百分比加成 + 等级加成
    float value = GetBaseModValue(modGroup, FLAT_MOD) + GetBaseModValue(modGroup, PCT_MOD) + GetRatingBonusValue(cr);

    // 武器技能差值加成：每点差值增加0.04%暴击
    value += (int32(GetWeaponSkillValue(attType)) - int32(GetMaxSkillValueForLevel())) * 0.04f;

    // 应用配置上限
    if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
         value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_CRIT) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_CRIT) : value;

    value = std::max(0.0f, value);
    SetStatFloatValue(index, value);
}

/**
 * @brief 更新所有暴击百分比
 *
 * 职责：
 *   计算敏捷提供的暴击加成，并更新所有攻击类型的暴击率。
 */
void Player::UpdateAllCritPercentages()
{
    // 从敏捷获取基础暴击百分比
    float value = GetMeleeCritFromAgility();

    // 设置所有攻击类型的敏捷暴击加成
    SetBaseModPctValue(CRIT_PERCENTAGE, value);
    SetBaseModPctValue(OFFHAND_CRIT_PERCENTAGE, value);
    SetBaseModPctValue(RANGED_CRIT_PERCENTAGE, value);

    // 更新各攻击类型的实际暴击率
    UpdateCritPercentage(BASE_ATTACK);
    UpdateCritPercentage(OFF_ATTACK);
    UpdateCritPercentage(RANGED_ATTACK);
}

/**
 * @brief 各职业的递减收益常数k
 *
 * 用于计算递减收益公式。
 * 递减收益用于限制高属性玩家获得过多闪避/招架等防御属性。
 */
float const m_diminishing_k[MAX_CLASSES] =
{
    0.9560f,  // Warrior
    0.9560f,  // Paladin
    0.9880f,  // Hunter
    0.9880f,  // Rogue
    0.9830f,  // Priest
    0.9560f,  // DK
    0.9880f,  // Shaman
    0.9830f,  // Mage
    0.9830f,  // Warlock
    0.0f,     // ??
    0.9720f   // Druid
};

/**
 * @brief 计算递减收益后的属性值
 *
 * @param capArray         各职业的属性上限数组
 * @param playerClass      玩家职业
 * @param nonDiminishValue 不受递减影响的值
 * @param diminishValue    受递减影响的值
 *
 * @return 递减计算后的总属性值
 *
 * 公式说明：
 *   1/x' = 1/c + k/x
 *   即：x' = cx / (x + ck)
 *
 *   其中：
 *   - k  为 m_diminishing_k 对应职业的常数
 *   - c  为 capArray 对应职业的上限
 *   - x  为递减前的值（diminishValue）
 *   - x' 为递减后的值
 *
 * 说明：
 *   递减收益机制确保高属性玩家不能无限堆叠防御属性。
 */
float CalculateDiminishingReturns(float const (&capArray)[MAX_CLASSES], uint8 playerClass, float nonDiminishValue, float diminishValue)
{
    uint32 const classIdx = playerClass - 1;

    float const k = m_diminishing_k[classIdx];
    float const c = capArray[classIdx];

    // 递减公式：x' = cx / (x + ck)
    float result = c * diminishValue / (diminishValue + c * k);
    result += nonDiminishValue;
    return result;
}

/**
 * @brief 各职业的未命中上限
 */
float const miss_cap[MAX_CLASSES] =
{
    16.00f,     // Warrior
    16.00f,     // Paladin
    16.00f,     // Hunter
    16.00f,     // Rogue
    16.00f,     // Priest
    16.00f,     // DK
    16.00f,     // Shaman
    16.00f,     // Mage
    16.00f,     // Warlock
    0.0f,       // ??
    16.00f      // Druid
};

/**
 * @brief 从防御技能获取未命中百分比
 *
 * @return 防御技能提供的未命中几率
 *
 * 说明：
 *   - 基础防御技能不参与递减计算
 *   - 防御等级提供的加成参与递减计算
 */
float Player::GetMissPercentageFromDefense() const
{
    float diminishing = 0.0f, nondiminishing = 0.0f;
    // 防御技能基础值：不参与递减
    nondiminishing += (int32(GetSkillValue(SKILL_DEFENSE)) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    // 防御等级加成：参与递减
    diminishing += (GetRatingBonusValue(CR_DEFENSE_SKILL) * 0.04f);

    // 应用递减公式
    return CalculateDiminishingReturns(miss_cap, GetClass(), nondiminishing, diminishing);
}

/**
 * @brief 各职业的招架上限
 */
float const parry_cap[MAX_CLASSES] =
{
    47.003525f,     // Warrior
    47.003525f,     // Paladin
    145.560408f,    // Hunter
    145.560408f,    // Rogue
    0.0f,           // Priest（无法招架）
    47.003525f,     // DK
    145.560408f,    // Shaman
    0.0f,           // Mage（无法招架）
    0.0f,           // Warlock（无法招架）
    0.0f,           // ??
    0.0f            // Druid（无法招架）
};

/**
 * @brief 更新招架百分比
 *
 * 职责：
 *   计算并更新玩家的招架几率，应用递减收益。
 *
 * 计算组成部分：
 *   - 基础值：5%
 *   - 招架等级加成（受递减影响）
 *   - 防御技能加成：基础技能不受递减，等级加成受递减
 *   - 光环加成（不受递减）
 *
 * 说明：
 *   部分职业（牧师、法师、术士、德鲁伊）无法招架。
 */
void Player::UpdateParryPercentage()
{
    float value = 0.0f;
    uint32 pclass = GetClass() - 1;
    if (CanParry() && parry_cap[pclass] > 0.0f)
    {
        float nondiminishing  = 5.0f;  // 基础招架率
        // 招架等级加成：受递减
        float diminishing = GetRatingBonusValue(CR_PARRY);
        // 防御技能加成
        nondiminishing += (int32(GetSkillValue(SKILL_DEFENSE)) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        diminishing += (GetRatingBonusValue(CR_DEFENSE_SKILL) * 0.04f);
        // 光环加成：不受递减
        nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);

        // 应用递减公式
        value = CalculateDiminishingReturns(parry_cap, GetClass(), nondiminishing, diminishing);

        // 应用配置上限
        if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
             value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_PARRY) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_PARRY) : value;

        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_PARRY_PERCENTAGE, value);
}

/**
 * @brief 各职业的闪避上限
 */
float const dodge_cap[MAX_CLASSES] =
{
    88.129021f,     // Warrior
    88.129021f,     // Paladin
    145.560408f,    // Hunter
    145.560408f,    // Rogue
    150.375940f,    // Priest
    88.129021f,     // DK
    145.560408f,    // Shaman
    150.375940f,    // Mage
    150.375940f,    // Warlock
    0.0f,           // ??
    116.890707f     // Druid
};

/**
 * @brief 更新闪避百分比
 *
 * 职责：
 *   计算并更新玩家的闪避几率，应用递减收益。
 *
 * 计算组成部分：
 *   - 敏捷提供的闪避（部分受递减，部分不受）
 *   - 防御技能加成
 *   - 光环加成
 *   - 闪避等级加成
 */
void Player::UpdateDodgePercentage()
{
    float diminishing = 0.0f, nondiminishing = 0.0f;
    // 敏捷提供的闪避
    GetDodgeFromAgility(diminishing, nondiminishing);
    // 防御技能加成
    nondiminishing += (int32(GetSkillValue(SKILL_DEFENSE)) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    diminishing += (GetRatingBonusValue(CR_DEFENSE_SKILL) * 0.04f);
    // 光环加成
    nondiminishing += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
    // 闪避等级加成
    diminishing += GetRatingBonusValue(CR_DODGE);

    // 应用递减公式
    float value = CalculateDiminishingReturns(dodge_cap, GetClass(), nondiminishing, diminishing);

    // 应用配置上限
    if (sWorld->getBoolConfig(CONFIG_STATS_LIMITS_ENABLE))
         value = value > sWorld->getFloatConfig(CONFIG_STATS_LIMITS_DODGE) ? sWorld->getFloatConfig(CONFIG_STATS_LIMITS_DODGE) : value;

    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(PLAYER_DODGE_PERCENTAGE, value);
}

/**
 * @brief 更新法术暴击几率
 *
 * @param school 魔法学校索引
 *
 * 主要流程：
 *   1. 物理学校（0）的暴击设为0
 *   2. 其他学校计算：
 *      - 智力提供的暴击
 *      - 光环加成
 *      - 等级加成
 */
void Player::UpdateSpellCritChance(uint32 school)
{
    // 物理学校没有法术暴击
    if (school == SPELL_SCHOOL_NORMAL)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1, 0.0f);
        return;
    }
    // 计算法术暴击
    float crit = 0.0f;
    // 智力提供的暴击
    crit += GetSpellCritFromIntellect();
    // 光环加成：通用法术暴击
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    // 光环加成：百分比暴击
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_CRIT_PCT);
    // 光环加成：按魔法学校的法术暴击
    crit += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, 1<<school);
    // 等级加成
    crit += GetRatingBonusValue(CR_CRIT_SPELL);

    SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school, crit);
}

/**
 * @brief 更新护甲穿透
 *
 * @param amount 护甲穿透等级值
 *
 * 说明：
 *   护甲穿透是无属性装备属性，直接存储到战斗等级字段。
 */
void Player::UpdateArmorPenetration(int32 amount)
{
    SetUInt32Value(PLAYER_FIELD_COMBAT_RATING_1 + AsUnderlyingType(CR_ARMOR_PENETRATION), amount);
}

/**
 * @brief 更新近战命中几率
 *
 * 说明：
 *   从近战命中等级计算命中百分比加成。
 */
void Player::UpdateMeleeHitChances()
{
    m_modMeleeHitChance = GetRatingBonusValue(CR_HIT_MELEE);
}

/**
 * @brief 更新远程命中几率
 *
 * 说明：
 *   从远程命中等级计算命中百分比加成。
 */
void Player::UpdateRangedHitChances()
{
    m_modRangedHitChance = GetRatingBonusValue(CR_HIT_RANGED);
}

/**
 * @brief 更新法术命中几率
 *
 * 说明：
 *   从光环和法术命中等级计算命中百分比加成。
 */
void Player::UpdateSpellHitChances()
{
    m_modSpellHitChance = (float)GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_HIT_CHANCE);
    m_modSpellHitChance += GetRatingBonusValue(CR_HIT_SPELL);
}

/**
 * @brief 更新所有魔法学校的暴击几率
 */
void Player::UpdateAllSpellCritChances()
{
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateSpellCritChance(i);
}

/**
 * @brief 更新熟练度
 *
 * @param attack 攻击类型（远程攻击不使用熟练度）
 *
 * 职责：
 *   计算并更新指定攻击类型的熟练度。
 *   熟练度降低目标的闪避和招架几率。
 *
 * 计算来源：
 *   - 熟练度等级
 *   - 特定武器的熟练度光环
 */
void Player::UpdateExpertise(WeaponAttackType attack)
{
    if (attack == RANGED_ATTACK)
        return;

    int32 expertise = int32(GetRatingBonusValue(CR_EXPERTISE));

    // 获取武器特定的熟练度加成
    Item const* weapon = GetWeaponForAttack(attack, true);
    expertise += GetTotalAuraModifier(SPELL_AURA_MOD_EXPERTISE, [weapon](AuraEffect const* aurEff) -> bool
    {
        return aurEff->GetSpellInfo()->IsItemFitToSpellRequirements(weapon);
    });

    if (expertise < 0)
        expertise = 0;

    switch (attack)
    {
        case BASE_ATTACK:
            SetUInt32Value(PLAYER_EXPERTISE, expertise);
            break;
        case OFF_ATTACK:
            SetUInt32Value(PLAYER_OFFHAND_EXPERTISE, expertise);
            break;
        default:
            break;
    }
}

/**
 * @brief 应用法力回复加成
 *
 * @param amount 加成数值
 * @param apply  true 应用加成，false 移除加成
 */
void Player::ApplyManaRegenBonus(int32 amount, bool apply)
{
    _ModifyUInt32(apply, m_baseManaRegen, amount);
    UpdatePowerRegen(POWER_MANA);
}

/**
 * @brief 应用生命回复加成
 *
 * @param amount 加成数值
 * @param apply  true 应用加成，false 移除加成
 */
void Player::ApplyHealthRegenBonus(int32 amount, bool apply)
{
    _ModifyUInt32(apply, m_baseHealthRegen, amount);
}

/**
 * @brief 各能量类型的基础回复速率和配置
 *
 * 说明：
 *   - first: 基础回复速率（每秒）
 *   - second: 配置文件中的速率倍率参数
 */
static std::pair<float, Optional<Rates>> const powerRegenInfo[MAX_POWERS] =
{
    { 0.f,      RATE_POWER_MANA             }, // POWER_MANA
    { -12.5f,   RATE_POWER_RAGE_LOSS        }, // POWER_RAGE,           -1.25 怒气/秒（衰减）
    { 0.f,      std::nullopt                }, // POWER_FOCUS
    { 10.f,     RATE_POWER_ENERGY           }, // POWER_ENERGY,         +10 能量/秒
    { 0.f,      std::nullopt                }, // POWER_HAPPINESS
    { 0.f,      std::nullopt                }, // POWER_RUNE
    { -12.5f,   RATE_POWER_RUNICPOWER_LOSS  }  // POWER_RUNIC_POWER,    -1.25 符文能量/秒（衰减）
};

/**
 * @brief 更新能量回复速率
 *
 * @param power 能量类型
 *
 * 主要流程：
 *   1. 检查是否有阻止回复的光环
 *   2. 根据能量类型计算回复速率：
 *      - 法力：精神+智力公式，区分战斗/非战斗状态
 *      - 怒气/能量/符文能量：固定基础速率 + 光环加成
 *   3. 应用服务器配置倍率
 *   4. 设置客户端显示字段
 *
 * 说明：
 *   - result_regen: 非战斗/未施法状态下的回复速率
 *   - result_regen_interrupted: 战斗/施法状态下的回复速率
 *   - 法力回复分两部分：精神回复（战斗中受抑制）和固定回复（战斗中不抑制）
 */
void Player::UpdatePowerRegen(Powers power)
{
    if (power == POWER_HEALTH || power >= MAX_POWERS)
        return;

    float result_regen              = 0.f; // 非战斗 / 非施法状态下的回复速率
    float result_regen_interrupted  = 0.f; // 战斗 / 施法状态下的回复速率
    float modifier                  = 1.f; // 配置倍率或其他修饰符

    // 检查是否有阻止回复的光环
    if (HasAuraTypeWithValue(SPELL_AURA_PREVENT_REGENERATE_POWER, power))
    {
        SetFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + AsUnderlyingType(power), power == POWER_ENERGY ? -10.f : 0.f);
        SetFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER + AsUnderlyingType(power), power == POWER_ENERGY ? -10.f : 0.f);
        return;
    }

    switch (power)
    {
        case POWER_MANA:
        {
            float Intellect = GetStat(STAT_INTELLECT);
            // 法力回复公式：sqrt(智力) * 精神回复系数
            float power_regen = std::sqrt(Intellect) * OCTRegenMPPerSpirit();
            // 百分比加成光环
            power_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);

            // 固定法力回复（来自光环和装备，以MP5为单位）
            float power_regen_mp5 = (GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA) + m_baseManaRegen) / 5.0f;

            // 从属性获得法力回复的光环（如精神转法力回复）
            AuraEffectList const& regenAura = GetAuraEffectsByType(SPELL_AURA_MOD_MANA_REGEN_FROM_STAT);
            for (AuraEffectList::const_iterator i = regenAura.begin(); i != regenAura.end(); ++i)
                power_regen_mp5 += GetStat(Stats((*i)->GetMiscValue())) * (*i)->GetAmount() / 500.0f;

            // 施法状态下的精神回复百分比（某些天赋允许施法时回复部分法力）
            int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
            if (modManaRegenInterrupt > 100)
                modManaRegenInterrupt = 100;

            result_regen                = power_regen_mp5 + power_regen;
            result_regen_interrupted    = power_regen_mp5 + CalculatePct(power_regen, modManaRegenInterrupt);

            // 低等级玩家有回复加成（确保新角色有足够法力）
            if (GetLevel() < 15)
                modifier *= 2.066f - (GetLevel() * 0.066f);
            break;
        }
        case POWER_RAGE:
        case POWER_ENERGY:
        case POWER_RUNIC_POWER:
        {
            // 使用基础回复速率
            result_regen                = powerRegenInfo[AsUnderlyingType(power)].first;
            result_regen_interrupted    = 0.f;

            // 应用百分比加成
            result_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, AsUnderlyingType(power));
            // 应用固定值加成
            result_regen_interrupted += static_cast<float>(GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, AsUnderlyingType(power))) / 5.f;

            // 屠戮天赋需要战斗状态，所以符文能量的中断回复不合并
            if (power != POWER_RUNIC_POWER)
                result_regen += result_regen_interrupted;
            break;
        }
        default:
            break;
    }

    // 应用服务器配置倍率
    if (powerRegenInfo[AsUnderlyingType(power)].second.has_value())
        modifier *= sWorld->getRate(powerRegenInfo[AsUnderlyingType(power)].second.value());

    result_regen                *= modifier;
    result_regen_interrupted    *= modifier;

    // 单位字段存储相对于基础能量回复的偏移量
    // 非法力类型需要减去基础值（因为基础值已由客户端处理）
    if (power != POWER_MANA)
        result_regen -= powerRegenInfo[AsUnderlyingType(power)].first;

    // 能量回复在战斗/非战斗状态下相同
    if (power == POWER_ENERGY)
        result_regen_interrupted = result_regen;

    SetFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + AsUnderlyingType(power), result_regen);
    SetFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER + AsUnderlyingType(power), result_regen_interrupted);
}

/**
 * @brief 获取当前能量回复速率
 *
 * @param power 能量类型
 *
 * @return 当前实际的能量回复速率
 *
 * 说明：
 *   根据当前状态（战斗/施法等）返回对应的回复速率。
 */
float Player::GetPowerRegen(Powers power) const
{
    if (power == POWER_HEALTH || power >= MAX_POWERS)
        return 0.f;

    // 判断是否处于"中断"状态
    bool interrupted =  HasAuraType(SPELL_AURA_INTERRUPT_REGEN) ||
                        (power == POWER_MANA && IsUnderLastManaUseEffect()) ||
                        (power != POWER_MANA && IsInCombat());

    // 获取对应状态的回复速率
    float regen = GetFloatValue((interrupted ? UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER : UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER) + AsUnderlyingType(power));
    // 非法力类型需要加上基础值
    if (power != POWER_MANA)
        regen += (power == POWER_ENERGY || !interrupted) ? powerRegenInfo[AsUnderlyingType(power)].first : 0.f;

    return regen;
}

/**
 * @brief 更新符文回复速率
 *
 * @param rune 符文类型
 *
 * 说明：
 *   死亡骑士的符文回复基于冷却时间计算。
 *   回复速率 = 1000 / 冷却时间（每秒恢复的符文百分比）
 */
void Player::UpdateRuneRegen(RuneType rune)
{
    if (rune >= NUM_RUNE_TYPES)
        return;

    uint32 cooldown = 0;

    // 查找该类型符文的基础冷却时间
    for (uint32 i = 0; i < MAX_RUNES; ++i)
        if (GetBaseRune(i) == rune)
        {
            cooldown = GetRuneBaseCooldown(i);
            break;
        }

    if (cooldown <= 0)
        return;

    // 回复速率 = 1000ms / 冷却时间
    float regen = float(1 * IN_MILLISECONDS) / float(cooldown);
    SetFloatValue(PLAYER_RUNE_REGEN_1 + uint8(rune), regen);
}

/**
 * @brief 应用所有属性加成
 *
 * 职责：
 *   禁止属性修改检测，应用所有光环和装备的属性修饰符，然后更新所有属性。
 *
 * 调用时机：
 *   玩家登录时或完全重新计算属性时。
 */
void Player::_ApplyAllStatBonuses()
{
    SetCanModifyStats(false);

    _ApplyAllAuraStatMods();
    _ApplyAllItemMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/**
 * @brief 移除所有属性加成
 *
 * 职责：
 *   禁止属性修改检测，移除所有装备和光环的属性修饰符，然后更新所有属性。
 */
void Player::_RemoveAllStatBonuses()
{
    SetCanModifyStats(false);

    _RemoveAllItemMods();
    _RemoveAllAuraStatMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/*#######################################
########                         ########
########    MOBS STAT SYSTEM     ########
########                         ########
#######################################*/

/**
 * @brief 更新生物属性（空实现）
 *
 * 说明：
 *   普通生物的属性由数据库模板定义，不需要动态计算。
 *   宠物和守护者有独立的属性更新实现。
 */
bool Creature::UpdateStats(Stats /*stat*/)
{
    return true;
}

/**
 * @brief 更新生物的所有属性
 *
 * 主要流程：
 *   1. 更新最大生命值
 *   2. 更新攻击强度
 *   3. 更新所有能量值上限
 *   4. 更新所有抗性
 */
bool Creature::UpdateAllStats()
{
    UpdateMaxHealth();
    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);

    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllResistances();

    return true;
}

/**
 * @brief 更新生物的抗性
 *
 * @param school 魔法学校索引
 *
 * 说明：
 *   魔法学校使用光环修饰符计算，物理学校调用护甲更新。
 */
void Creature::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

/**
 * @brief 更新生物的护甲
 *
 * 说明：
 *   生物护甲完全由光环修饰符决定。
 */
void Creature::UpdateArmor()
{
    float value = GetTotalAuraModValue(UNIT_MOD_ARMOR);
    SetArmor(int32(value));
}

/**
 * @brief 更新生物的最大生命值
 *
 * 说明：
 *   生物生命值完全由光环修饰符决定。
 */
void Creature::UpdateMaxHealth()
{
    float value = GetTotalAuraModValue(UNIT_MOD_HEALTH);
    SetMaxHealth(uint32(value));
}

/**
 * @brief 更新生物的最大能量值
 *
 * @param power 能量类型
 *
 * 说明：
 *   使用标准的属性公式：(基础值 * 基础百分比 + 总值) * 总百分比
 */
void Creature::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE);
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(std::lroundf(value)));
}

/**
 * @brief 更新生物的攻击强度和伤害
 *
 * @param ranged true 远程，false 近战
 *
 * 说明：
 *   更新攻击强度后自动更新武器伤害。
 */
void Creature::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    float baseAttackPower       = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attackPowerMod        = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float attackPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    if (ranged)
    {
        SetRangedAttackPower(int32(baseAttackPower));
        if (attackPowerMod >= 0)
            SetRangedAttackPowerModPos(int32(attackPowerMod));
        if (attackPowerMod <= 0)
            SetRangedAttackPowerModNeg(int32(attackPowerMod));
        SetRangedAttackPowerMultiplier(attackPowerMultiplier);
    }
    else
    {
        SetAttackPower(int32(baseAttackPower));
        if (attackPowerMod >= 0)
            SetAttackPowerModPos(int32(attackPowerMod));
        if (attackPowerMod <= 0)
            SetAttackPowerModNeg(int32(attackPowerMod));
        SetAttackPowerMultiplier(attackPowerMultiplier);
    }

    // 攻击强度变化后自动更新武器伤害
    if (ranged)
        UpdateDamagePhysical(RANGED_ATTACK);
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        UpdateDamagePhysical(OFF_ATTACK);
    }
}

/**
 * @brief 计算生物的武器伤害范围
 *
 * @param attType      攻击类型
 * @param normalized   是否标准化
 * @param addTotalPct  是否添加总百分比
 * @param minDamage    [out] 最小伤害
 * @param maxDamage    [out] 最大伤害
 * @param damageIndex  伤害索引（生物只有主伤害，索引必须为0）
 *
 * 计算公式：
 *   ((武器伤害 + 基础值) * 伤害倍率 * 基础百分比 + 总值) * 总百分比
 *
 * 说明：
 *   - 生物使用伤害方差系数调整伤害范围
 *   - ModDamage 是生物模板的伤害倍率
 */
void Creature::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex /*= 0*/) const
{
    // 生物只有一种伤害类型
    if (damageIndex != 0)
    {
        minDamage = 0.f;
        maxDamage = 0.f;
        return;
    }

    // 根据攻击类型获取伤害方差和UnitMod
    float variance = 1.0f;
    UnitMods unitMod;
    switch (attType)
    {
        case BASE_ATTACK:
        default:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            variance = GetCreatureTemplate()->RangeVariance;
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    // 副手攻击检查
    if (attType == OFF_ATTACK && !haveOffhandWeapon())
    {
        minDamage = 0.0f;
        maxDamage = 0.0f;
        return;
    }

    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    // 缴械状态
    if (!CanUseAttackType(attType))
    {
        weaponMinDamage = 0.0f;
        weaponMaxDamage = 0.0f;
    }

    float attackPower      = GetTotalAttackPowerValue(attType);
    float attackSpeedMulti = GetAPMultiplier(attType, normalized);
    float baseValue        = GetFlatModifierValue(unitMod, BASE_VALUE) + (attackPower / 14.0f) * variance;
    float basePct          = GetPctModifierValue(unitMod, BASE_PCT) * attackSpeedMulti;
    float totalValue       = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float totalPct         = addTotalPct ? GetPctModifierValue(unitMod, TOTAL_PCT) : 1.0f;
    float dmgMultiplier    = GetCreatureTemplate()->ModDamage; // = ModDamage * _GetDamageMod(rank);

    minDamage = ((weaponMinDamage + baseValue) * dmgMultiplier * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue) * dmgMultiplier * basePct + totalValue) * totalPct;
}

/*#######################################
########                         ########
########    PETS STAT SYSTEM     ########
########                         ########
#######################################*/

/**
 * @brief 宠物和守护者实体ID定义
 *
 * 这些是常见宠物的实体ID，用于特殊属性计算。
 */
#define ENTRY_IMP               416     // 小鬼（术士）
#define ENTRY_VOIDWALKER        1860    // 虚空行者（术士）
#define ENTRY_SUCCUBUS          1863    // 魅魔（术士）
#define ENTRY_FELHUNTER         417     // 地狱猎犬（术士）
#define ENTRY_FELGUARD          17252   // 恶魔守卫（术士）
#define ENTRY_WATER_ELEMENTAL   510     // 水元素（法师）
#define ENTRY_TREANT            1964    // 树人（德鲁伊）
#define ENTRY_FIRE_ELEMENTAL    15438   // 火元素（萨满）
#define ENTRY_GHOUL             26125   // 食尸鬼（死亡骑士）
#define ENTRY_BLOODWORM         28017   // 血虫（死亡骑士）

/**
 * @brief 更新守护者属性
 *
 * @param stat 要更新的属性
 *
 * @return 更新是否成功
 *
 * 职责：
 *   计算守护者的属性值，包括从主人处继承的属性加成。
 *
 * 守护者属性继承规则：
 *   1. 食尸鬼/复活的盟友（DK）：
 *      - 耐力：主人耐力的30%（受天赋和雕文影响）
 *      - 力量：主力量的70%（受天赋和雕文影响）
 *
 *   2. 猎人宠物：
 *      - 耐力：主人耐力的45%（受野性狩猎天赋影响）
 *      - 攻击强度：主人远程攻击强度的22%（受野性狩猎和动物伙伴天赋影响）
 *
 *   3. 术士宠物：
 *      - 耐力：主人耐力的75%
 *      - 智力：主智力的30%
 *
 *   4. 法师水元素：
 *      - 智力：主智力的30%
 */
bool Guardian::UpdateStats(Stats stat)
{
    if (stat >= MAX_STATS)
        return false;

    // 属性值计算公式：((基础值 * 基础百分比) + 总值) * 总百分比
    float value  = GetTotalStatValue(stat);
    float ownersBonus = 0.0f;

    Unit* owner = GetOwner();
    // 死亡骑士食尸鬼/复活的盟友：耐力和力量从主人继承
    float mod = 0.75f;
    if ((IsPetGhoul() || IsRisenAlly()) && (stat == STAT_STAMINA || stat == STAT_STRENGTH))
    {
        if (stat == STAT_STAMINA)
            mod = 0.3f; // 默认：主人耐力的30%
        else
            mod = 0.7f; // 默认：主力量的70%

        // 检查"贪食亡者"天赋（效果不是光环）
        AuraEffect const* aurEff = owner->GetAuraEffect(SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, SPELLFAMILY_DEATHKNIGHT, 3010, 0);
        if (aurEff)
        {
            SpellInfo const* spellInfo = aurEff->GetSpellInfo();
            AddPct(mod, spellInfo->GetEffect(EFFECT_1).CalcValue()); // 贪食亡者修改继承比例
        }
        // 食尸鬼雕文
        aurEff = owner->GetAuraEffect(58686, 0);
        if (aurEff)
            mod += CalculatePct(1.0f, aurEff->GetAmount()); // 雕文增加固定比例
        ownersBonus = float(owner->GetStat(stat)) * mod;
        value += ownersBonus;
    }
    else if (stat == STAT_STAMINA)
    {
        // 术士宠物获得主人耐力的75%
        if (owner->GetClass() == CLASS_WARLOCK && IsPet())
        {
            ownersBonus = CalculatePct(owner->GetStat(STAT_STAMINA), 75);
            value += ownersBonus;
        }
        else
        {
            // 其他宠物获得主人耐力的45%（受天赋影响）
            mod = 0.45f;
            if (IsPet())
            {
                // 野性狩猎天赋
                PetSpellMap::const_iterator itr = (ToPet()->m_spells.find(62758)); // Wild Hunt rank 1
                if (itr == ToPet()->m_spells.end())
                    itr = ToPet()->m_spells.find(62762);                            // Wild Hunt rank 2

                if (itr != ToPet()->m_spells.end())
                {
                    SpellInfo const* spellInfo = sSpellMgr->AssertSpellInfo(itr->first);
                    AddPct(mod, spellInfo->GetEffect(EFFECT_0).CalcValue());
                }
            }
            ownersBonus = float(owner->GetStat(stat)) * mod;
            value += ownersBonus;
        }
    }
    // 术士和法师的宠物获得主人智力的30%
    else if (stat == STAT_INTELLECT)
    {
        if (owner->GetClass() == CLASS_WARLOCK || owner->GetClass() == CLASS_MAGE)
        {
            ownersBonus = CalculatePct(owner->GetStat(stat), 30);
            value += ownersBonus;
        }
    }

    SetStat(stat, int32(value));
    m_statFromOwner[stat] = ownersBonus;
    UpdateStatBuffMod(stat);

    // 根据属性类型更新衍生属性
    switch (stat)
    {
        case STAT_STRENGTH:         UpdateAttackPowerAndDamage();        break;
        case STAT_AGILITY:          UpdateArmor();                       break;
        case STAT_STAMINA:          UpdateMaxHealth();                   break;
        case STAT_INTELLECT:        UpdateMaxPower(POWER_MANA);          break;
        case STAT_SPIRIT:
        default:
            break;
    }

    return true;
}

/**
 * @brief 更新守护者的所有属性
 *
 * 主要流程：
 *   1. 更新最大生命值
 *   2. 更新所有基础属性
 *   3. 更新所有能量值上限
 *   4. 更新所有抗性
 */
bool Guardian::UpdateAllStats()
{
    UpdateMaxHealth();

    for (uint8 i = STAT_STRENGTH; i < MAX_STATS; ++i)
        UpdateStats(Stats(i));

    for (uint8 i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllResistances();

    return true;
}

/**
 * @brief 更新守护者的抗性
 *
 * @param school 魔法学校索引
 *
 * 说明：
 *   猎人和术士宠物获得主人40%的抗性。
 */
void Guardian::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));

        // 猎人和术士宠物获得主人抗性的40%
        if (IsPet())
            value += float(CalculatePct(m_owner->GetResistance(SpellSchools(school)), 40));

        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

/**
 * @brief 更新守护者的护甲
 *
 * 说明：
 *   - 敏捷提供护甲（每点敏捷2点护甲）
 *   - 猎人和术士宠物获得主人护甲的35%
 */
void Guardian::UpdateArmor()
{
    float value = 0.0f;
    float bonus_armor = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    // 猎人和术士宠物获得主人护甲的35%
    if (IsPet())
        bonus_armor = float(CalculatePct(m_owner->GetArmor(), 35));

    value  = GetFlatModifierValue(unitMod, BASE_VALUE);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetStat(STAT_AGILITY) * 2.0f;  // 敏捷提供护甲
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + bonus_armor;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));
}

/**
 * @brief 更新守护者的最大生命值
 *
 * 说明：
 *   不同类型的宠物有不同的耐力系数：
 *   - 小鬼：8.4
 *   - 虚空行者：11.0（坦克型宠物）
 *   - 魅魔：9.1
 *   - 地狱猎犬：9.5
 *   - 恶魔守卫：11.0
 *   - 血虫：1.0
 *   - 其他：10.0
 */
void Guardian::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;
    float stamina = GetStat(STAT_STAMINA) - GetCreateStat(STAT_STAMINA);

    // 不同宠物类型的耐力系数
    float multiplicator;
    switch (GetEntry())
    {
        case ENTRY_IMP:         multiplicator = 8.4f;   break;
        case ENTRY_VOIDWALKER:  multiplicator = 11.0f;  break;  // 坦克型宠物
        case ENTRY_SUCCUBUS:    multiplicator = 9.1f;   break;
        case ENTRY_FELHUNTER:   multiplicator = 9.5f;   break;
        case ENTRY_FELGUARD:    multiplicator = 11.0f;  break;
        case ENTRY_BLOODWORM:   multiplicator = 1.0f;   break;
        default:                multiplicator = 10.0f;  break;
    }

    float value = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + stamina * multiplicator;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

/**
 * @brief 更新守护者的最大能量值
 *
 * @param power 能量类型
 *
 * 说明：
 *   不同类型的宠物有不同的智力系数：
 *   - 小鬼：4.95
 *   - 其他术士宠物：11.5
 *   - 其他：15.0
 */
void Guardian::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + AsUnderlyingType(power));

    // 智力对法力值的影响
    float addValue = (power == POWER_MANA) ? GetStat(STAT_INTELLECT) - GetCreateStat(STAT_INTELLECT) : 0.0f;
    float multiplicator = 15.0f;

    // 不同宠物类型的智力系数
    switch (GetEntry())
    {
        case ENTRY_IMP:         multiplicator = 4.95f;  break;
        case ENTRY_VOIDWALKER:
        case ENTRY_SUCCUBUS:
        case ENTRY_FELHUNTER:
        case ENTRY_FELGUARD:    multiplicator = 11.5f;  break;
        default:                multiplicator = 15.0f;  break;
    }

    float value  = GetFlatModifierValue(unitMod, BASE_VALUE) + GetCreatePowerValue(power);
    value *= GetPctModifierValue(unitMod, BASE_PCT);
    value += GetFlatModifierValue(unitMod, TOTAL_VALUE) + addValue * multiplicator;
    value *= GetPctModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

/**
 * @brief 更新守护者的攻击强度和伤害
 *
 * @param ranged 是否远程（守护者不使用远程攻击）
 *
 * 职责：
 *   计算守护者的基础攻击强度，以及从主人处继承的攻击强度。
 *
 * 主人属性继承规则：
 *   1. 猎人宠物：主人远程攻击强度的22%（受天赋和雕文影响）
 *   2. DK食尸鬼：主人近战攻击强度的22%
 *   3. 萨满幽灵狼：主人近战攻击强度的31%（受雕文影响为61%）
 *   4. 术士宠物：主人火焰或暗影伤害的57%
 *   5. 法师水元素：主人冰霜伤害的40%
 */
void Guardian::UpdateAttackPowerAndDamage(bool ranged)
{
    if (ranged)
        return;

    float val = 0.0f;
    float bonusAP = 0.0f;
    UnitMods unitMod = UNIT_MOD_ATTACK_POWER;

    // 小鬼的攻击强度计算不同
    if (GetEntry() == ENTRY_IMP)
        val = GetStat(STAT_STRENGTH) - 10.0f;
    else
        val = 2 * GetStat(STAT_STRENGTH) - 20.0f;

    Unit* owner = GetOwner();
    if (owner && owner->GetTypeId() == TYPEID_PLAYER)
    {
        if (IsHunterPet())  // 猎人宠物：从主人的远程攻击强度获益
        {
            float mod = 1.0f;  // 猎人贡献修正系数
            if (IsPet())
            {
                // 野性狩猎天赋
                PetSpellMap::const_iterator itr = ToPet()->m_spells.find(62758);    // Wild Hunt rank 1
                if (itr == ToPet()->m_spells.end())
                    itr = ToPet()->m_spells.find(62762);                            // Wild Hunt rank 2

                if (itr != ToPet()->m_spells.end())
                {
                    SpellInfo const* sProto = sSpellMgr->AssertSpellInfo(itr->first);
                    mod += CalculatePct(1.0f, sProto->GetEffect(EFFECT_1).CalcValue());
                }
            }

            bonusAP = owner->GetTotalAttackPowerValue(RANGED_ATTACK) * 0.22f * mod;
            // 动物伙伴天赋
            if (AuraEffect* aurEff = owner->GetAuraEffectOfRankedSpell(34453, EFFECT_1, owner->GetGUID()))
            {
                AddPct(bonusAP, aurEff->GetAmount());
                AddPct(val, aurEff->GetAmount());
            }
            SetBonusDamage(int32(owner->GetTotalAttackPowerValue(RANGED_ATTACK) * 0.1287f * mod));
        }
        else if (IsPetGhoul() || IsRisenAlly())  // DK食尸鬼：从主人的近战攻击强度获益
        {
            bonusAP = owner->GetTotalAttackPowerValue(BASE_ATTACK) * 0.22f;
            SetBonusDamage(int32(owner->GetTotalAttackPowerValue(BASE_ATTACK) * 0.1287f));
        }
        else if (IsSpiritWolf())  // 萨满幽灵狼
        {
            float dmg_multiplier = 0.31f;
            if (m_owner->GetAuraEffect(63271, 0)) // 幽灵狼雕文
                dmg_multiplier = 0.61f;
            bonusAP = owner->GetTotalAttackPowerValue(BASE_ATTACK) * dmg_multiplier;
            SetBonusDamage(int32(owner->GetTotalAttackPowerValue(BASE_ATTACK) * dmg_multiplier));
        }
        // 术士恶魔：从主人的火焰或暗影伤害获益
        else if (IsPet())
        {
            int32 fire  = owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_FIRE)) - owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_FIRE));
            int32 shadow = owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_SHADOW)) - owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_SHADOW));
            // 使用火焰或暗影伤害中较高者
            int32 maximum  = (fire > shadow) ? fire : shadow;
            if (maximum < 0)
                maximum = 0;
            SetBonusDamage(int32(maximum * 0.15f));
            bonusAP = maximum * 0.57f;
        }
        // 法师水元素：从主人的冰霜伤害获益
        else if (GetEntry() == ENTRY_WATER_ELEMENTAL)
        {
            int32 frost = owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_FROST)) - owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_FROST));
            if (frost < 0)
                frost = 0;
            SetBonusDamage(int32(frost * 0.4f));
        }
    }

    SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, val + bonusAP);

    // 计算最终攻击强度
    // UNIT_MOD_ATTACK_POWER 的 BASE_VALUE 存储数据库中的近战攻击强度字段
    float base_attPower  = GetFlatModifierValue(unitMod, BASE_VALUE) * GetPctModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetPctModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetAttackPower(int32(base_attPower));
    SetAttackPowerModPos(int32(attPowerMod));
    SetAttackPowerMultiplier(attPowerMultiplier);

    // 攻击强度变化后自动更新武器伤害
    UpdateDamagePhysical(BASE_ATTACK);
}

/**
 * @brief 更新守护者的物理伤害
 *
 * @param attType 攻击类型（守护者只使用主手攻击）
 *
 * 职责：
 *   计算守护者的近战伤害范围，包括从主人法术伤害获得的加成。
 *
 * 特殊加成规则：
 *   - 德鲁伊树人：主人自然伤害的9%
 *   - 萨满火元素：主人火焰伤害的40%
 *
 * 猎人宠物快乐状态影响：
 *   - 快乐：125%伤害
 *   - 满意：100%伤害
 *   - 不快乐：75%伤害
 */
void Guardian::UpdateDamagePhysical(WeaponAttackType attType)
{
    if (attType > BASE_ATTACK)
        return;

    float bonusDamage = 0.0f;
    if (m_owner->GetTypeId() == TYPEID_PLAYER)
    {
        // 德鲁伊树人：从主人的自然伤害获益
        if (GetEntry() == ENTRY_TREANT)
        {
            int32 spellDmg = m_owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_NATURE)) - m_owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_NATURE));
            if (spellDmg > 0)
                bonusDamage = spellDmg * 0.09f;
        }
        // 萨满火元素：从主人的火焰伤害获益
        else if (GetEntry() == ENTRY_FIRE_ELEMENTAL)
        {
            int32 spellDmg = m_owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + AsUnderlyingType(SPELL_SCHOOL_FIRE)) - m_owner->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + AsUnderlyingType(SPELL_SCHOOL_FIRE));
            if (spellDmg > 0)
                bonusDamage = spellDmg * 0.4f;
        }
    }

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    // 基于攻击速度的伤害计算
    float att_speed = float(GetAttackTime(BASE_ATTACK))/1000.0f;

    float base_value  = GetFlatModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType) / 14.0f * att_speed + bonusDamage;
    float base_pct    = GetPctModifierValue(unitMod, BASE_PCT);
    float total_value = GetFlatModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetPctModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    // 猎人宠物根据快乐状态调整伤害
    if (IsHunterPet())
    {
        switch (ToPet()->GetHappinessState())
        {
            case HAPPY:
                // 快乐状态：125%伤害
                mindamage = mindamage * 1.25f;
                maxdamage = maxdamage * 1.25f;
                break;
            case CONTENT:
                // 满意状态：100%伤害
                break;
            case UNHAPPY:
                // 不快乐状态：75%伤害
                mindamage = mindamage * 0.75f;
                maxdamage = maxdamage * 0.75f;
                break;
        }
    }

    // 特殊光环处理（某些光环需要减少伤害）
    /// @todo: 移除这个硬编码处理
    Unit::AuraEffectList const& mDummy = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACKSPEED);
    for (Unit::AuraEffectList::const_iterator itr = mDummy.begin(); itr != mDummy.end(); ++itr)
    {
        switch ((*itr)->GetSpellInfo()->Id)
        {
            case 61682:
            case 61683:
                AddPct(mindamage, -(*itr)->GetAmount());
                AddPct(maxdamage, -(*itr)->GetAmount());
                break;
            default:
                break;
        }
    }

    SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
    SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
}

/**
 * @brief 设置守护者的法术伤害加成
 *
 * @param damage 法术伤害加成值
 *
 * 说明：
 *   这个值存储在守护者内部，并同步到玩家的宠物法术强度字段（用于客户端显示）。
 */
void Guardian::SetBonusDamage(int32 damage)
{
    m_bonusSpellDamage = damage;
    if (GetOwner()->GetTypeId() == TYPEID_PLAYER)
        GetOwner()->SetUInt32Value(PLAYER_PET_SPELL_POWER, damage);
}
