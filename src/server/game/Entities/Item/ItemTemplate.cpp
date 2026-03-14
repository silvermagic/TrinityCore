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
 * @file ItemTemplate.cpp
 * @brief 物品模板实现模块
 *
 * 本文件实现了 ItemTemplate 结构体中定义的各种辅助方法。
 *
 * 主要功能：
 *   - 计算武器DPS和野性攻击强度加成
 *   - 检查物品是否可以有签名
 *   - 检查物品是否可以在战斗中更换装备状态
 *   - 获取物品等级（含品质修正）
 *   - 获取物品所需技能
 *   - 构建查询数据包用于客户端查询
 *
 * 性能注意事项：
 *   - 查询数据包在服务器启动时预构建并缓存
 *   - 总攻击强度在加载时计算并缓存
 */

#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include "Packets/QueryPackets.h"

/**
 * @brief 检查物品是否可以有签名
 *
 * 判断物品是否可以显示创建者签名（如"由XXX制造"）。
 *
 * 条件：
 *   - 最大堆叠数量为1（不可堆叠）
 *   - 不是消耗品
 *   - 不是任务物品
 *   - 没有"无创建者"标志
 *   - 不是炉石（物品ID 6948是特殊的）
 *
 * @return 是否可以有签名
 *
 * @note 签名用于显示制造者信息，如附魔物品、制造的装备等
 */
bool ItemTemplate::HasSignature() const
{
    return GetMaxStackSize() == 1 &&                           // 不可堆叠
        Class != ITEM_CLASS_CONSUMABLE &&                      // 不是消耗品
        Class != ITEM_CLASS_QUEST &&                           // 不是任务物品
        !HasFlag(ITEM_FLAG_NO_CREATOR) &&                      // 没有"无创建者"标志
        ItemId != 6948; /*Hearthstone*/                        // 不是炉石
}

/**
 * @brief 检查物品是否可以在战斗中更换装备状态
 *
 * 某些装备类型允许在战斗中更换（如圣物、盾牌、武器），
 * 而其他装备类型（如胸甲、腿甲）则不允许。
 *
 * @return 是否可以在战斗中更换装备状态
 *
 * @note 这影响了玩家在战斗中的装备切换行为
 */
bool ItemTemplate::CanChangeEquipStateInCombat() const
{
    // 圣物、盾牌、副手物品可以在战斗中更换
    switch (InventoryType)
    {
        case INVTYPE_RELIC:      // 圣物
        case INVTYPE_SHIELD:     // 盾牌
        case INVTYPE_HOLDABLE:   // 副手物品
            return true;
    }

    // 武器和弹药可以在战斗中更换
    switch (Class)
    {
        case ITEM_CLASS_WEAPON:    // 武器
        case ITEM_CLASS_PROJECTILE: // 弹药
            return true;
    }

    return false;
}

/**
 * @brief 计算武器的DPS（每秒伤害）
 *
 * DPS = (最小伤害 + 最大伤害) / 2 / 攻击速度(秒)
 * 计算所有伤害类型的总和。
 *
 * @return DPS值，如果武器没有攻击速度则返回0
 *
 * @note 公式: DPS = Sum(DamageMin + DamageMax) * 500 / Delay
 *       其中500 = 1000ms / 2，用于将毫秒转换为秒并取平均
 */
float ItemTemplate::getDPS() const
{
    if (!Delay)
        return 0.f;

    // 计算所有伤害类型的总和
    float temp = 0.f;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
        temp += Damage[i].DamageMin + Damage[i].DamageMax;

    // DPS = 总伤害 / 2 / 攻击速度(秒)
    return temp * 500.f / Delay;
}

/**
 * @brief 计算野性形态攻击强度加成
 *
 * 德鲁伊在野性形态（熊/猫）下，武器的DPS会转化为攻击强度。
 * 这是暴雪的设计机制，使德鲁伊在野性形态下能从武器获得收益。
 *
 * @param extraDPS 额外的DPS加成（默认为0）
 * @return 野性攻击强度加成，如果不符合条件则返回0
 *
 * @note 公式: (DPS * 14) - 767
 *       - 14 是DPS到攻击强度的转换因子
 *       - 767 是基础偏移量，确保低DPS武器不提供负加成
 *
 * @note 只对单手武器、双手武器、主手武器、副手武器有效
 */
int32 ItemTemplate::getFeralBonus(int32 extraDPS /*= 0*/) const
{
    // 定义允许获得野性加成的武器槽位类型掩码
    constexpr uint32 feralApEnabledInventoryTypeMaks = 1 << INVTYPE_WEAPON | 1 << INVTYPE_2HWEAPON | 1 << INVTYPE_WEAPONMAINHAND | 1 << INVTYPE_WEAPONOFFHAND;

    // 0x02A5F3 - is mask for Melee weapon from ItemSubClassMask.dbc
    // 只有武器类物品且符合槽位类型才能获得野性加成
    if (Class == ITEM_CLASS_WEAPON && (1 << InventoryType) & feralApEnabledInventoryTypeMaks)
    {
        // 计算野性攻击强度加成
        // 公式: (DPS * 14) - 767
        // 其中14是DPS到AP的转换系数，767是基础偏移
        int32 bonus = int32((extraDPS + getDPS()) * 14.0f) - 767;
        if (bonus < 0)
            return 0;
        return bonus;
    }

    return 0;
}

/**
 * @brief 获取包含品质修正的物品等级
 *
 * 根据物品品质对物品等级进行修正。
 * 低品质物品的物品等级会降低，高品质物品则保持原物品等级。
 *
 * @return 修正后的物品等级（不小于0）
 *
 * @note 品质修正规则：
 *       - 灰色/白色/绿色/神器/传家宝：物品等级 - 13
 *       - 蓝色（精良）：物品等级 - 13
 *       - 紫色（史诗）/橙色（传说）：保持原物品等级
 */
float ItemTemplate::GetItemLevelIncludingQuality() const
{
    float itemLevel(ItemLevel);
    switch (Quality)
    {
        case ITEM_QUALITY_POOR:       // 灰色（垃圾）
        case ITEM_QUALITY_NORMAL:     // 白色（普通）
        case ITEM_QUALITY_UNCOMMON:   // 绿色（优秀）
        case ITEM_QUALITY_ARTIFACT:   // 神器
        case ITEM_QUALITY_HEIRLOOM:   // 传家宝
            itemLevel -= 13.f; // leaving this as a separate statement since we do not know the real behavior in this case
            break;
        case ITEM_QUALITY_RARE:       // 蓝色（精良）
            itemLevel -= 13.f;
            break;
        case ITEM_QUALITY_EPIC:       // 紫色（史诗）
        case ITEM_QUALITY_LEGENDARY:  // 橙色（传说）
        default:
            break;
    }

    return std::max<float>(0.f, itemLevel);
}

/**
 * @brief 获取物品所需的技能ID
 *
 * 根据物品类别和子类别返回对应的技能ID。
 * 主要用于武器和护甲类物品。
 *
 * @return 技能ID，如果不需要技能则返回0
 *
 * @note 武器类：返回对应的武器技能（如剑、斧、法杖等）
 *       护甲类：返回对应的护甲技能（如布甲、皮甲、锁甲、板甲）
 */
uint32 ItemTemplate::GetSkill() const
{
    // 武器子类别对应的技能ID映射表
    static uint32 const itemWeaponSkills[MAX_ITEM_SUBCLASS_WEAPON] =
    {
        SKILL_AXES,     SKILL_2H_AXES,  SKILL_BOWS,          SKILL_GUNS,         SKILL_MACES,
        SKILL_2H_MACES, SKILL_POLEARMS, SKILL_SWORDS,        SKILL_2H_SWORDS,    0,
        SKILL_STAVES,   0,              0,                   SKILL_FIST_WEAPONS, 0,
        SKILL_DAGGERS,  SKILL_THROWN,   SKILL_ASSASSINATION, SKILL_CROSSBOWS,    SKILL_WANDS,
        SKILL_FISHING
    };

    // 护甲子类别对应的技能ID映射表
    static uint32 const itemArmorSkills[MAX_ITEM_SUBCLASS_ARMOR] =
    {
        0, SKILL_CLOTH, SKILL_LEATHER, SKILL_MAIL, SKILL_PLATE_MAIL, 0, SKILL_SHIELD, 0, 0, 0, 0
    };

    switch (Class)
    {
        case ITEM_CLASS_WEAPON:
            if (SubClass >= MAX_ITEM_SUBCLASS_WEAPON)
                return 0;
            else
                return itemWeaponSkills[SubClass];

        case ITEM_CLASS_ARMOR:
            if (SubClass >= MAX_ITEM_SUBCLASS_ARMOR)
                return 0;
            else
                return itemArmorSkills[SubClass];

        default:
            return 0;
    }
}

/**
 * @brief 加载并计算总攻击强度加成
 *
 * 遍历物品的属性加成和装备法术，计算总攻击强度加成。
 * 这个值在物品加载时计算并缓存，避免运行时重复计算。
 *
 * 计算来源：
 *   1. 物品属性中的攻击强度加成（ITEM_MOD_ATTACK_POWER）
 *   2. 装备触发法术提供的攻击强度光环
 *
 * @note 此方法在物品模板加载后由 ObjectMgr 调用
 */
void ItemTemplate::_LoadTotalAP()
{
    int32 totalAP = 0;

    // 遍历物品属性，累加攻击强度
    for (uint32 i = 0; i < StatsCount; ++i)
        if (ItemStat[i].ItemStatType == ITEM_MOD_ATTACK_POWER)
            totalAP += ItemStat[i].ItemStatValue;

    // some items can have equip spells with +AP
    // 检查装备触发法术是否提供攻击强度光环
    for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        if (Spells[i].SpellId > 0 && Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP)
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(Spells[i].SpellId))
                for (SpellEffectInfo const& effect : spellInfo->GetEffects())
                    if (effect.IsAura(SPELL_AURA_MOD_ATTACK_POWER))
                        totalAP += effect.CalcValue();

    _totalAP = totalAP;
}

/**
 * @brief 初始化所有语言的查询数据
 *
 * 在服务器启动时调用，为所有支持的语言预构建物品查询数据包。
 * 这些数据包会在客户端请求物品信息时直接发送，避免运行时构建。
 *
 * @note 性能优化：预构建数据包减少运行时开销
 */
void ItemTemplate::InitializeQueryData()
{
    for (uint8 loc = LOCALE_enUS; loc < TOTAL_LOCALES; ++loc)
        QueryData[loc] = BuildQueryData(static_cast<LocaleConstant>(loc));
}

/**
 * @brief 构建指定语言的查询数据包
 *
 * 构建包含物品所有属性的数据包，用于响应客户端的物品查询请求。
 * 数据包包含本地化的名称和描述。
 *
 * @param loc 语言常量
 * @return 构建好的查询数据包
 *
 * 调用时机：
 *   - 服务器启动时预构建（InitializeQueryData）
 *   - 客户端查询物品信息时发送
 *
 * @note 数据包格式遵循客户端协议定义
 */
WorldPacket ItemTemplate::BuildQueryData(LocaleConstant loc) const
{
    WorldPackets::Query::QueryItemSingleResponse response;

    // 获取本地化的名称和描述
    std::string locName = Name1;
    std::string locDescription = Description;

    // 如果存在本地化数据，则使用本地化文本
    if (ItemLocale const* il = sObjectMgr->GetItemLocale(ItemId))
    {
        ObjectMgr::GetLocaleString(il->Name, loc, locName);
        ObjectMgr::GetLocaleString(il->Description, loc, locDescription);
    }

    // ==================== 填充基本信息 ====================
    response.ItemID = ItemId;
    response.Allow = true;  // 允许查询

    // ==================== 填充物品属性 ====================
    response.Stats.Class = Class;
    response.Stats.SubClass = SubClass;
    response.Stats.SoundOverrideSubclass = SoundOverrideSubclass;
    response.Stats.Name = locName;
    response.Stats.DisplayInfoID = DisplayInfoID;
    response.Stats.Quality = Quality;
    response.Stats.Flags = Flags;
    response.Stats.Flags2 = Flags2;
    response.Stats.BuyPrice = BuyPrice;
    response.Stats.SellPrice = SellPrice;
    response.Stats.InventoryType = InventoryType;
    response.Stats.AllowableClass = AllowableClass;
    response.Stats.AllowableRace = AllowableRace;
    response.Stats.ItemLevel = ItemLevel;
    response.Stats.RequiredLevel = RequiredLevel;
    response.Stats.RequiredSkill = RequiredSkill;
    response.Stats.RequiredSkillRank = RequiredSkillRank;
    response.Stats.RequiredSpell = RequiredSpell;
    response.Stats.RequiredHonorRank = RequiredHonorRank;
    response.Stats.RequiredCityRank = RequiredCityRank;
    response.Stats.RequiredReputationFaction = RequiredReputationFaction;
    response.Stats.RequiredReputationRank = RequiredReputationRank;
    response.Stats.MaxCount = MaxCount;
    response.Stats.Stackable = Stackable;
    response.Stats.ContainerSlots = ContainerSlots;
    response.Stats.StatsCount = StatsCount;

    // 填充属性列表
    for (uint32 i = 0; i < StatsCount; ++i)
    {
        response.Stats.ItemStat[i].ItemStatType = ItemStat[i].ItemStatType;
        response.Stats.ItemStat[i].ItemStatValue = ItemStat[i].ItemStatValue;
    }

    // ==================== 填充缩放属性 ====================
    response.Stats.ScalingStatDistribution = ScalingStatDistribution;
    response.Stats.ScalingStatValue = ScalingStatValue;

    // ==================== 填充伤害属性 ====================
    for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
    {
        response.Stats.Damage[i].DamageMin = Damage[i].DamageMin;
        response.Stats.Damage[i].DamageMax = Damage[i].DamageMax;
        response.Stats.Damage[i].DamageType = Damage[i].DamageType;
    }

    // ==================== 填充抗性属性 ====================
    response.Stats.Resistance[SPELL_SCHOOL_NORMAL] = Armor;       // 护甲值
    response.Stats.Resistance[SPELL_SCHOOL_HOLY] = HolyRes;       // 神圣抗性
    response.Stats.Resistance[SPELL_SCHOOL_FIRE] = FireRes;       // 火焰抗性
    response.Stats.Resistance[SPELL_SCHOOL_NATURE] = NatureRes;   // 自然抗性
    response.Stats.Resistance[SPELL_SCHOOL_FROST] = FrostRes;     // 冰霜抗性
    response.Stats.Resistance[SPELL_SCHOOL_SHADOW] = ShadowRes;   // 暗影抗性
    response.Stats.Resistance[SPELL_SCHOOL_ARCANE] = ArcaneRes;   // 奥术抗性

    // ==================== 填充武器属性 ====================
    response.Stats.Delay = Delay;
    response.Stats.AmmoType = AmmoType;
    response.Stats.RangedModRange = RangedModRange;

    // ==================== 填充法术属性 ====================
    for (uint8 s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
    {
        response.Stats.Spells[s].SpellId = Spells[s].SpellId;
        response.Stats.Spells[s].SpellTrigger = Spells[s].SpellTrigger;
        response.Stats.Spells[s].SpellCharges = Spells[s].SpellCharges;
        response.Stats.Spells[s].SpellCooldown = Spells[s].SpellCooldown;
        response.Stats.Spells[s].SpellCategory = Spells[s].SpellCategory;
        response.Stats.Spells[s].SpellCategoryCooldown = Spells[s].SpellCategoryCooldown;
    }

    // ==================== 填充其他属性 ====================
    response.Stats.Bonding = Bonding;
    response.Stats.Description = locDescription;
    response.Stats.PageText = PageText;
    response.Stats.LanguageID = LanguageID;
    response.Stats.PageMaterial = PageMaterial;
    response.Stats.StartQuest = StartQuest;
    response.Stats.LockID = LockID;
    response.Stats.Material = Material;
    response.Stats.Sheath = Sheath;
    response.Stats.RandomProperty = RandomProperty;
    response.Stats.RandomSuffix = RandomSuffix;
    response.Stats.Block = Block;
    response.Stats.ItemSet = ItemSet;
    response.Stats.MaxDurability = MaxDurability;
    response.Stats.Area = Area;
    response.Stats.Map = Map;
    response.Stats.BagFamily = BagFamily;
    response.Stats.TotemCategory = TotemCategory;

    // ==================== 填充插槽属性 ====================
    for (uint8 s = 0; s < MAX_ITEM_PROTO_SOCKETS; ++s)
    {
        response.Stats.Socket[s].Color = Socket[s].Color;
        response.Stats.Socket[s].Content = Socket[s].Content;
    }

    response.Stats.SocketBonus = socketBonus;
    response.Stats.GemProperties = GemProperties;
    response.Stats.RequiredDisenchantSkill = RequiredDisenchantSkill;
    response.Stats.ArmorDamageModifier = ArmorDamageModifier;
    response.Stats.Duration = Duration;
    response.Stats.ItemLimitCategory = ItemLimitCategory;
    response.Stats.HolidayId = HolidayId;

    // 写入数据包并压缩
    response.Write();
    response.ShrinkToFit();
    return response.Move();
}
