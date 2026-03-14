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
 * @file SpellInfo.h
 * @brief 法术信息定义头文件
 *
 * 本文件定义了法术的静态数据结构和查询接口。SpellInfo 是法术系统的数据核心，
 * 存储了所有法术的固有属性，包括：
 *
 * 主要类：
 * - SpellInfo：法术信息主类，存储法术的所有静态属性
 * - SpellEffectInfo：单个法术效果的详细信息
 * - SpellImplicitTargetInfo：法术隐式目标选择信息
 * - SpellDiminishInfo：法术递减收益信息
 *
 * 数据来源：
 * - 主要从 DBC（Database Client）文件加载
 * - Spell.dbc：法术基础属性
 * - SpellRadius.dbc：法术半径
 * - SpellRange.dbc：施法范围
 * - SpellDuration.dbc：持续时间
 * - SpellCastTimes.dbc：施法时间
 *
 * 核心功能：
 * 1. 属性查询（施法时间、范围、消耗等）
 * 2. 效果管理（伤害、治疗、光环等）
 * 3. 目标选择规则
 * 4. 递减收益（DR）计算
 * 5. 法术免疫机制
 * 6. 施法条件检查
 *
 * 数据缓存：
 * - SpellInfo 对象在服务器启动时创建并缓存
 * - 频繁查询的结果会被缓存（如 _spellSpecific, _auraState）
 * - 避免运行时的重复计算
 *
 * @see SpellInfo.cpp 实现文件
 * @see Spell.h 法术执行类
 */

#ifndef _SPELLINFO_H
#define _SPELLINFO_H

#include "SharedDefines.h"
#include "Util.h"
#include "DBCStructure.h"
#include "Object.h"
#include "SpellAuraDefines.h"

class AuraEffect;
class Item;
class Player;
class Spell;
class SpellMgr;
class SpellInfo;
class Unit;
struct Condition;
struct SpellChainNode;
struct SpellTargetPosition;
struct SpellDurationEntry;
struct SpellModifier;
struct SpellRangeEntry;
struct SpellRadiusEntry;
struct SpellEntry;
struct SpellCastTimesEntry;

// 法术目标选择类别枚举
enum SpellTargetSelectionCategories
{
    TARGET_SELECT_CATEGORY_NYI,       // 尚未实现
    TARGET_SELECT_CATEGORY_DEFAULT,   // 默认选择
    TARGET_SELECT_CATEGORY_CHANNEL,   // 引导法术
    TARGET_SELECT_CATEGORY_NEARBY,    // 附近目标
    TARGET_SELECT_CATEGORY_CONE,      // 锥形范围
    TARGET_SELECT_CATEGORY_AREA,      // 区域范围
    TARGET_SELECT_CATEGORY_TRAJ       // 投射物轨迹
};

// 法术目标引用类型枚举
enum SpellTargetReferenceTypes
{
    TARGET_REFERENCE_TYPE_NONE,    // 无引用
    TARGET_REFERENCE_TYPE_CASTER,  // 施法者
    TARGET_REFERENCE_TYPE_TARGET,  // 目标
    TARGET_REFERENCE_TYPE_LAST,    // 最后一个目标
    TARGET_REFERENCE_TYPE_SRC,     // 源位置
    TARGET_REFERENCE_TYPE_DEST     // 目标位置
};

// 法术目标对象类型枚举
enum SpellTargetObjectTypes : uint8
{
    TARGET_OBJECT_TYPE_NONE = 0,         // 无对象类型
    TARGET_OBJECT_TYPE_SRC,              // 源位置
    TARGET_OBJECT_TYPE_DEST,             // 目标位置
    TARGET_OBJECT_TYPE_UNIT,             // 单位（玩家/NPC）
    TARGET_OBJECT_TYPE_UNIT_AND_DEST,    // 单位和目标位置
    TARGET_OBJECT_TYPE_GOBJ,             // 游戏对象
    TARGET_OBJECT_TYPE_GOBJ_ITEM,        // 游戏对象物品
    TARGET_OBJECT_TYPE_ITEM,             // 物品
    TARGET_OBJECT_TYPE_CORPSE,           // 尸体
    // only for effect target type - 仅用于效果目标类型
    TARGET_OBJECT_TYPE_CORPSE_ENEMY,     // 敌方尸体
    TARGET_OBJECT_TYPE_CORPSE_ALLY       // 友方尸体
};

// 法术目标检查类型枚举
enum SpellTargetCheckTypes : uint8
{
    TARGET_CHECK_DEFAULT,      // 默认检查
    TARGET_CHECK_ENTRY,        // 按入口ID检查
    TARGET_CHECK_ENEMY,        // 敌方目标
    TARGET_CHECK_ALLY,         // 友方目标
    TARGET_CHECK_PARTY,        // 队伍成员
    TARGET_CHECK_RAID,         // 团队成员
    TARGET_CHECK_RAID_CLASS,   // 团队职业
    TARGET_CHECK_PASSENGER     // 载具乘客
};

// 法术目标方向类型枚举
enum SpellTargetDirectionTypes
{
    TARGET_DIR_NONE,        // 无方向
    TARGET_DIR_FRONT,       // 前方
    TARGET_DIR_BACK,        // 后方
    TARGET_DIR_RIGHT,       // 右侧
    TARGET_DIR_LEFT,        // 左侧
    TARGET_DIR_FRONT_RIGHT, // 右前方
    TARGET_DIR_BACK_RIGHT,  // 右后方
    TARGET_DIR_BACK_LEFT,   // 左后方
    TARGET_DIR_FRONT_LEFT,  // 左前方
    TARGET_DIR_RANDOM,      // 随机方向
    TARGET_DIR_ENTRY        // 按入口指定
};

// 法术效果隐式目标类型枚举
enum SpellEffectImplicitTargetTypes
{
    EFFECT_IMPLICIT_TARGET_NONE = 0,     // 无隐式目标
    EFFECT_IMPLICIT_TARGET_EXPLICIT,     // 显式目标
    EFFECT_IMPLICIT_TARGET_CASTER        // 施法者作为目标
};

// 法术分类类型枚举 - 定义法术的特定类型
enum SpellSpecificType
{
    SPELL_SPECIFIC_NORMAL                        = 0,  // 普通法术
    SPELL_SPECIFIC_SEAL                          = 1,  // 圣骑士圣印
    SPELL_SPECIFIC_AURA                          = 3,  // 光环效果
    SPELL_SPECIFIC_STING                         = 4,  // 猎人钉刺
    SPELL_SPECIFIC_CURSE                         = 5,  // 术士诅咒
    SPELL_SPECIFIC_ASPECT                        = 6,  // 猎人守护
    SPELL_SPECIFIC_TRACKER                       = 7,  // 追踪法术
    SPELL_SPECIFIC_WARLOCK_ARMOR                 = 8,  // 术士护甲
    SPELL_SPECIFIC_MAGE_ARMOR                    = 9,  // 法师护甲
    SPELL_SPECIFIC_ELEMENTAL_SHIELD              = 10, // 萨满元素护盾
    SPELL_SPECIFIC_MAGE_POLYMORPH                = 11, // 法师变形术
    SPELL_SPECIFIC_JUDGEMENT                     = 13, // 圣骑士审判
    SPELL_SPECIFIC_WARLOCK_CORRUPTION            = 17, // 术士腐蚀术
    SPELL_SPECIFIC_FOOD                          = 19, // 食物
    SPELL_SPECIFIC_DRINK                         = 20, // 饮料
    SPELL_SPECIFIC_FOOD_AND_DRINK                = 21, // 食物和饮料
    SPELL_SPECIFIC_PRESENCE                      = 22, // 死亡骑士灵气
    SPELL_SPECIFIC_CHARM                         = 23, // 魅惑效果
    SPELL_SPECIFIC_SCROLL                        = 24, // 卷轴
    SPELL_SPECIFIC_MAGE_ARCANE_BRILLANCE         = 25, // 法师奥术光辉
    SPELL_SPECIFIC_WARRIOR_ENRAGE                = 26, // 战士激怒
    SPELL_SPECIFIC_PRIEST_DIVINE_SPIRIT          = 27, // 牧师神圣之灵
    SPELL_SPECIFIC_HAND                          = 28  // 圣骑士之手（自由、保护、拯救等）
};

// 法术自定义属性标志枚举 - 用于扩展法术的特定行为
enum SpellCustomAttributes
{
    SPELL_ATTR0_CU_ENCHANT_PROC                  = 0x00000001, // 附魔触发效果
    SPELL_ATTR0_CU_CONE_BACK                     = 0x00000002, // 锥形后方范围
    SPELL_ATTR0_CU_CONE_LINE                     = 0x00000004, // 锥形线性范围
    SPELL_ATTR0_CU_SHARE_DAMAGE                  = 0x00000008, // 分摊伤害
    SPELL_ATTR0_CU_NO_INITIAL_THREAT             = 0x00000010, // 无初始威胁值
    SPELL_ATTR0_CU_AURA_CC                       = 0x00000020, // 光环控制效果（如变形、恐惧等）
    SPELL_ATTR0_CU_DONT_BREAK_STEALTH            = 0x00000040, // 不打破潜行
    SPELL_ATTR0_CU_CAN_CRIT                      = 0x00000080, // 可以暴击
    SPELL_ATTR0_CU_DIRECT_DAMAGE                 = 0x00000100, // 直接伤害
    SPELL_ATTR0_CU_CHARGE                        = 0x00000200, // 冲锋技能
    SPELL_ATTR0_CU_PICKPOCKET                    = 0x00000400, // 偷窃技能
    SPELL_ATTR0_CU_ROLLING_PERIODIC              = 0x00000800, // 滚动周期性效果
    SPELL_ATTR0_CU_NEGATIVE_EFF0                 = 0x00001000, // 效果0为负面效果
    SPELL_ATTR0_CU_NEGATIVE_EFF1                 = 0x00002000, // 效果1为负面效果
    SPELL_ATTR0_CU_NEGATIVE_EFF2                 = 0x00004000, // 效果2为负面效果
    SPELL_ATTR0_CU_IGNORE_ARMOR                  = 0x00008000, // 忽略护甲
    SPELL_ATTR0_CU_REQ_TARGET_FACING_CASTER      = 0x00010000, // 目标需要面向施法者
    SPELL_ATTR0_CU_REQ_CASTER_BEHIND_TARGET      = 0x00020000, // 施法者需要在目标身后
    SPELL_ATTR0_CU_ALLOW_INFLIGHT_TARGET         = 0x00040000, // 允许空中目标
    SPELL_ATTR0_CU_NEEDS_AMMO_DATA               = 0x00080000, // 需要弹药数据
    SPELL_ATTR0_CU_BINARY_SPELL                  = 0x00100000, // 二元法术（全有或全无）
    SPELL_ATTR0_CU_SCHOOLMASK_NORMAL_WITH_MAGIC  = 0x00200000, // 普通攻击附带魔法效果
    SPELL_ATTR0_CU_DEPRECATED_LIQUID_AURA        = 0x00400000, // 已弃用 - 请勿重用
    SPELL_ATTR0_CU_IS_TALENT                     = 0x00800000, // 天赋技能 - 为master分支保留
    SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED          = 0x01000000, // 光环效果无法保存

    SPELL_ATTR0_CU_NEGATIVE                      = SPELL_ATTR0_CU_NEGATIVE_EFF0 | SPELL_ATTR0_CU_NEGATIVE_EFF1 | SPELL_ATTR0_CU_NEGATIVE_EFF2 // 任意效果为负面
};

/**
 * @brief 获取目标标志掩码
 * @param objType 目标对象类型
 * @return 对应的目标标志掩码
 */
uint32 GetTargetFlagMask(SpellTargetObjectTypes objType);

/**
 * @brief 法术隐式目标信息类
 *
 * 用于处理法术的隐式目标选择逻辑，包括目标类型、选择方式、
 * 引用对象等信息的存储和查询。
 */
class TC_GAME_API SpellImplicitTargetInfo
{
private:
    Targets _target;  // 目标类型
public:
    SpellImplicitTargetInfo() : _target(Targets(0)) { }
    SpellImplicitTargetInfo(uint32 target);

    bool IsArea() const;                              // 是否为区域目标
    SpellTargetSelectionCategories GetSelectionCategory() const;  // 获取选择类别
    SpellTargetReferenceTypes GetReferenceType() const;           // 获取引用类型
    SpellTargetObjectTypes GetObjectType() const;                 // 获取对象类型
    SpellTargetCheckTypes GetCheckType() const;                   // 获取检查类型
    SpellTargetDirectionTypes GetDirectionType() const;           // 获取方向类型
    float CalcDirectionAngle() const;                             // 计算方向角度

    Targets GetTarget() const;                                    // 获取目标类型
    uint32 GetExplicitTargetMask(bool& srcSet, bool& dstSet) const; // 获取显式目标掩码

private:
    /**
     * @brief 静态数据结构 - 存储目标类型的固有属性
     */
    struct StaticData
    {
        SpellTargetObjectTypes ObjectType;         // 目标类型返回的对象类型
        SpellTargetReferenceTypes ReferenceType;   // 选择目标时使用的引用对象
        SpellTargetSelectionCategories SelectionCategory;  // 选择类别
        SpellTargetCheckTypes SelectionCheckType;  // 选择条件
        SpellTargetDirectionTypes DirectionType;   // 锥形和目标点的方向
    };
    static std::array<StaticData, TOTAL_SPELL_TARGETS> _data;
};

/**
 * @brief 法术效果信息类
 *
 * 存储和管理单个法术效果的详细信息，包括效果类型、数值、
 * 目标、触发条件等。一个法术可以有多个效果（最多3个）。
 */
class TC_GAME_API SpellEffectInfo
{
    friend class SpellInfo;
    SpellInfo const* _spellInfo;  // 所属法术信息指针
public:
    SpellEffIndex EffectIndex;                  // 效果索引（0-2）
    SpellEffects Effect;                        // 效果类型（如伤害、治疗、召唤等）
    AuraType  ApplyAuraName;                    // 应用光环类型
    uint32    Amplitude;                        // 周期性效果间隔（毫秒）
    int32     DieSides;                         // 骰子面数（随机效果值）
    float     RealPointsPerLevel;               // 每等级增加的基础点数
    int32     BasePoints;                       // 基础点数
    float     PointsPerComboPoint;              // 每连击点增加的点数
    float     ValueMultiplier;                  // 数值乘数
    float     DamageMultiplier;                 // 伤害乘数
    float     BonusMultiplier;                  // 加成乘数
    int32     MiscValue;                        // 杂项值A
    int32     MiscValueB;                       // 杂项值B
    Mechanics Mechanic;                         // 机制类型（如击晕、沉默等）
    SpellImplicitTargetInfo TargetA;            // 目标A信息
    SpellImplicitTargetInfo TargetB;            // 目标B信息
    SpellRadiusEntry const* RadiusEntry;        // 半径数据条目
    uint32    ChainTarget;                      // 连锁目标数量
    uint32    ItemType;                         // 物品类型
    uint32    TriggerSpell;                     // 触发的法术ID
    flag96    SpellClassMask;                   // 法术类别掩码
    std::vector<Condition*>* ImplicitTargetConditions; // 隐式目标条件列表

    SpellEffectInfo();
    explicit SpellEffectInfo(SpellEntry const* spellEntry, SpellInfo const* spellInfo, uint8 effIndex);
    SpellEffectInfo(SpellEffectInfo const&) = delete;
    SpellEffectInfo(SpellEffectInfo&&) noexcept;
    SpellEffectInfo& operator=(SpellEffectInfo const&) = delete;
    SpellEffectInfo& operator=(SpellEffectInfo&&) noexcept;
    ~SpellEffectInfo();

    bool IsEffect() const;                           // 是否有效果
    bool IsEffect(SpellEffects effectName) const;    // 是否为指定效果类型
    bool IsAura() const;                             // 是否为光环效果
    bool IsAura(AuraType aura) const;                // 是否为指定光环类型
    bool IsTargetingArea() const;                    // 是否以区域为目标
    bool IsAreaAuraEffect() const;                   // 是否为区域光环效果
    bool IsUnitOwnedAuraEffect() const;              // 是否为单位拥有的光环效果

    int32 CalcValue(WorldObject const* caster = nullptr, int32 const* basePoints = nullptr) const;  // 计算效果值
    int32 CalcBaseValue(int32 value) const;          // 计算基础值
    float CalcValueMultiplier(WorldObject* caster, Spell* spell = nullptr) const;   // 计算数值乘数
    float CalcDamageMultiplier(WorldObject* caster, Spell* spell = nullptr) const;  // 计算伤害乘数

    bool HasRadius() const;                          // 是否有半径设置
    float CalcRadius(WorldObject* caster = nullptr, Spell* = nullptr) const;        // 计算半径

    uint32 GetProvidedTargetMask() const;            // 获取提供的目标掩码
    uint32 GetMissingTargetMask(bool srcSet = false, bool destSet = false, uint32 mask = 0) const; // 获取缺失的目标掩码

    SpellEffectImplicitTargetTypes GetImplicitTargetType() const;   // 获取隐式目标类型
    SpellTargetObjectTypes GetUsedTargetObjectType() const;         // 获取使用的目标对象类型

    struct ImmunityInfo;
    ImmunityInfo const* GetImmunityInfo() const { return _immunityInfo.get(); }  // 获取免疫信息

private:
    /**
     * @brief 静态数据结构 - 存储效果类型的固有属性
     */
    struct StaticData
    {
        SpellEffectImplicitTargetTypes ImplicitTargetType;  // 隐式目标类型
        SpellTargetObjectTypes UsedTargetObjectType;        // 有效目标对象类型
    };
    static std::array<StaticData, TOTAL_SPELL_EFFECTS> _data;

    std::unique_ptr<ImmunityInfo> _immunityInfo;  // 免疫信息
};

/**
 * @brief 法术递减信息结构体
 *
 * 存储法术的递减收益（Diminishing Returns）相关信息，
 * 用于控制控制效果（如昏迷、恐惧等）持续时间随施放次数递减的机制。
 */
struct TC_GAME_API SpellDiminishInfo
{
    DiminishingGroup DiminishGroup = DIMINISHING_NONE;           // 递减收益分组
    DiminishingReturnsType DiminishReturnType = DRTYPE_NONE;      // 递减收益类型
    DiminishingLevels DiminishMaxLevel = DIMINISHING_LEVEL_IMMUNE; // 递减最大等级
    int32 DiminishDurationLimit = 0;                             // 递减持续时间限制
};

/**
 * @brief 法术信息类 - 核心法术数据存储和查询接口
 *
 * SpellInfo 是 TrinityCore 中处理法术数据的核心类。它存储了法术的所有静态信息，
 * 包括基本属性、施法时间、范围、消耗、效果等。该类提供了丰富的查询接口来获取
 * 法术的各种属性和行为特征。
 *
 * 主要职责：
 * - 存储法术的基本属性（ID、类别、属性标志等）
 * - 提供施法时间和施法范围查询
 * - 管理法术效果和光环信息
 * - 处理法术目标选择和检查
 * - 支持递减收益（DR）计算
 * - 提供免疫机制查询
 *
 * 用法示例：
 * @code
 * SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
 * uint32 castTime = spellInfo->GetCastTime();
 * float range = spellInfo->GetMaxRange();
 * @endcode
 */
class TC_GAME_API SpellInfo
{
    friend class SpellMgr;

    public:
        // ==================== 基本属性 ====================
        uint32 Id;                                  // 法术ID - 唯一标识符
        SpellCategoryEntry const* CategoryEntry;    // 法术类别条目 - 用于共享冷却等
        uint32 Dispel;                              // 驱散类型（魔法、诅咒、疾病、毒药等）
        uint32 Mechanic;                            // 机制类型（击晕、恐惧、沉默等）
        uint32 Attributes;                          // 基础属性标志
        uint32 AttributesEx;                        // 扩展属性标志1
        uint32 AttributesEx2;                       // 扩展属性标志2
        uint32 AttributesEx3;                       // 扩展属性标志3
        uint32 AttributesEx4;                       // 扩展属性标志4
        uint32 AttributesEx5;                       // 扩展属性标志5
        uint32 AttributesEx6;                       // 扩展属性标志6
        uint32 AttributesEx7;                       // 扩展属性标志7
        uint32 AttributesCu;                        // 自定义属性标志

        // ==================== 姿态和形态 ====================
        uint64 Stances;                             // 需要的姿态/形态（战士姿态、德鲁伊形态等）
        uint64 StancesNot;                          // 禁止的姿态/形态

        // ==================== 目标相关 ====================
        uint32 Targets;                             // 目标类型标志
        uint32 TargetCreatureType;                  // 目标生物类型限制
        uint32 RequiresSpellFocus;                  // 需要的施法焦点（如制造台）
        uint32 FacingCasterFlags;                   // 面向施法者标志

        // ==================== 光环状态要求 ====================
        uint32 CasterAuraState;                     // 施法者需要的光环状态
        uint32 TargetAuraState;                     // 目标需要的光环状态
        uint32 CasterAuraStateNot;                  // 施法者不能有的光环状态
        uint32 TargetAuraStateNot;                  // 目标不能有的光环状态
        uint32 CasterAuraSpell;                     // 施法者需要的法术光环
        uint32 TargetAuraSpell;                     // 目标需要的法术光环
        uint32 ExcludeCasterAuraSpell;              // 排除施法者的法术光环
        uint32 ExcludeTargetAuraSpell;              // 排除目标的法术光环

        // ==================== 施法时间 ====================
        SpellCastTimesEntry const* CastTimeEntry;   // 施法时间条目 - 定义施法所需时间

        // ==================== 冷却和恢复 ====================
        uint32 RecoveryTime;                        // 冷却时间（毫秒）
        uint32 CategoryRecoveryTime;                // 类别冷却时间（毫秒）
        uint32 StartRecoveryCategory;               // 开始恢复的类别
        uint32 StartRecoveryTime;                   // 开始恢复时间

        // ==================== 中断标志 ====================
        uint32 InterruptFlags;                      // 施法中断标志
        uint32 AuraInterruptFlags;                  // 光环中断标志
        uint32 ChannelInterruptFlags;               // 引导中断标志

        // ==================== 触发机制 ====================
        uint32 ProcFlags;                           // 触发标志
        uint32 ProcChance;                          // 触发概率（百分比）
        uint32 ProcCharges;                         // 触发次数

        // ==================== 等级相关 ====================
        uint32 MaxLevel;                            // 最大可学习等级
        uint32 BaseLevel;                           // 基础等级
        uint32 SpellLevel;                          // 法术等级

        // ==================== 持续时间 ====================
        SpellDurationEntry const* DurationEntry;    // 持续时间条目

        // ==================== 能量消耗 ====================
        Powers PowerType;                           // 能量类型（法力、怒气、能量等）
        uint32 ManaCost;                            // 基础法力消耗
        uint32 ManaCostPerlevel;                    // 每等级增加的法力消耗
        uint32 ManaPerSecond;                       // 每秒法力消耗（引导法术）
        uint32 ManaPerSecondPerLevel;               // 每等级每秒法力消耗
        uint32 ManaCostPercentage;                  // 按百分比消耗法力
        uint32 RuneCostID;                          // 符文消耗ID（死亡骑士）

        // ==================== 施法范围 ====================
        SpellRangeEntry const* RangeEntry;          // 施法范围条目 - 定义最小/最大施法距离

        // ==================== 弹道和堆叠 ====================
        float  Speed;                               // 弹道速度
        uint32 StackAmount;                         // 最大堆叠层数

        // ==================== 施法材料 ====================
        std::array<uint32, 2> Totem;                // 需要的图腾
        std::array<int32, MAX_SPELL_REAGENTS>  Reagent;     // 需要的材料
        std::array<uint32, MAX_SPELL_REAGENTS> ReagentCount; // 材料数量

        // ==================== 装备要求 ====================
        int32  EquippedItemClass;                   // 需要装备的物品类别
        int32  EquippedItemSubClassMask;            // 需要装备的物品子类别掩码
        int32  EquippedItemInventoryTypeMask;       // 需要装备的物品栏位类型掩码

        // ==================== 图腾类别 ====================
        std::array<uint32, 2> TotemCategory;        // 需要的图腾类别

        // ==================== 视觉效果 ====================
        std::array<uint32, 2> SpellVisual;          // 法术视觉效果ID
        uint32 SpellIconID;                         // 法术图标ID
        uint32 ActiveIconID;                        // 激活状态图标ID
        uint32 Priority;                            // 优先级

        // ==================== 名称和等级 ====================
        std::array<char const*, 16> SpellName;      // 法术名称（多语言）
        std::array<char const*, 16> Rank;           // 法术等级名称（如"等级1"）

        // ==================== 目标限制 ====================
        uint32 MaxTargetLevel;                      // 最大目标等级限制
        uint32 MaxAffectedTargets;                  // 最大影响目标数量

        // ==================== 法术家族 ====================
        uint32 SpellFamilyName;                     // 法术家族名称（用于法术修改）
        flag96 SpellFamilyFlags;                    // 法术家族标志

        // ==================== 伤害类型 ====================
        uint32 DmgClass;                            // 伤害类别（魔法、物理等）
        uint32 PreventionType;                      // 阻止类型

        // ==================== 区域和效果 ====================
        int32  AreaGroupId;                         // 区域组ID
        uint32 SchoolMask;                          // 法术学派掩码

        // ==================== 效果和目标 ====================
        std::array<SpellEffectInfo, MAX_SPELL_EFFECTS> _effects; // 法术效果数组
        uint32 ExplicitTargetMask;                  // 显式目标掩码
        SpellChainNode const* ChainEntry;           // 法术链节点（升级关系）

        SpellInfo(SpellEntry const* spellEntry);
        ~SpellInfo();

        // ==================== 类别和效果查询 ====================
        uint32 GetCategory() const;                         // 获取法术类别ID
        bool HasEffect(SpellEffects effect) const;          // 是否拥有指定类型的效果
        bool HasAura(AuraType aura) const;                  // 是否拥有指定类型的光环
        bool HasAreaAuraEffect() const;                     // 是否拥有区域光环效果
        bool HasOnlyDamageEffects() const;                  // 是否仅拥有伤害效果

        // ==================== 属性检查 ====================
        inline bool HasAttribute(SpellAttr0 attribute) const { return !!(Attributes & attribute); }
        inline bool HasAttribute(SpellAttr1 attribute) const { return !!(AttributesEx & attribute); }
        inline bool HasAttribute(SpellAttr2 attribute) const { return !!(AttributesEx2 & attribute); }
        inline bool HasAttribute(SpellAttr3 attribute) const { return !!(AttributesEx3 & attribute); }
        inline bool HasAttribute(SpellAttr4 attribute) const { return !!(AttributesEx4 & attribute); }
        inline bool HasAttribute(SpellAttr5 attribute) const { return !!(AttributesEx5 & attribute); }
        inline bool HasAttribute(SpellAttr6 attribute) const { return !!(AttributesEx6 & attribute); }
        inline bool HasAttribute(SpellAttr7 attribute) const { return !!(AttributesEx7 & attribute); }
        inline bool HasAttribute(SpellCustomAttributes customAttribute) const { return !!(AttributesCu & customAttribute); }

        // ==================== 专业和技能类型检查 ====================
        bool IsExplicitDiscovery() const;                   // 是否为显式发现（如制造业发现配方）
        bool IsLootCrafting() const;                        // 是否为制造类战利品
        bool IsProfessionOrRiding() const;                  // 是否为专业技能或骑术
        bool IsProfession() const;                          // 是否为专业技能
        bool IsPrimaryProfession() const;                   // 是否为主要专业技能
        bool IsPrimaryProfessionFirstRank() const;          // 是否为主要专业技能的第一等级
        bool IsAbilityLearnedWithProfession() const;        // 是否随专业学习的能力
        bool IsAbilityOfSkillType(uint32 skillType) const;  // 是否属于指定技能类型

        // ==================== 目标和区域检查 ====================
        bool IsAffectingArea() const;                       // 是否影响区域
        bool IsTargetingArea() const;                       // 是否以区域为目标
        bool NeedsExplicitUnitTarget() const;               // 是否需要显式单位目标
        bool NeedsToBeTriggeredByCaster(SpellInfo const* triggeringSpell) const; // 是否需要被施法者触发
        bool IsSelfCast() const;                            // 是否为自身施法

        // ==================== 法术状态检查 ====================
        bool IsPassive() const;                             // 是否为被动法术
        bool IsAutocastable() const;                        // 是否可自动施放
        bool IsStackableWithRanks() const;                  // 是否可与不同等级叠加
        bool IsPassiveStackableWithRanks() const;           // 被动法术是否可与不同等级叠加
        bool IsMultiSlotAura() const;                       // 是否为多槽位光环
        bool IsStackableOnOneSlotWithDifferentCasters() const; // 是否可以在同一槽位由不同施法者叠加
        bool IsCooldownStartedOnEvent() const;              // 冷却是否由事件启动
        bool IsDeathPersistent() const;                     // 死亡后是否持续
        bool IsRequiringDeadTarget() const;                 // 是否需要死亡目标
        bool IsAllowingDeadTarget() const;                  // 是否允许死亡目标
        bool IsGroupBuff() const;                           // 是否为团队增益
        bool CanBeUsedInCombat() const;                     // 是否可在战斗中使用
        bool IsPositive() const;                            // 是否为正面效果
        bool IsPositiveEffect(uint8 effIndex) const;        // 指定效果是否为正面
        bool IsChanneled() const;                           // 是否为引导法术
        bool IsMoveAllowedChannel() const;                  // 引导时是否允许移动
        bool NeedsComboPoints() const;                      // 是否需要连击点
        bool IsNextMeleeSwingSpell() const;                 // 是否为下次近战攻击触发
        bool IsBreakingStealth() const;                     // 是否打破潜行
        bool IsRangedWeaponSpell() const;                   // 是否为远程武器法术
        bool IsAutoRepeatRangedSpell() const;               // 是否为自动射击
        bool HasInitialAggro() const;                       // 是否产生初始仇恨

        // ==================== 武器和物品检查 ====================
        WeaponAttackType GetAttackType() const;             // 获取攻击类型（主手、副手、远程）
        bool IsItemFitToSpellRequirements(Item const* item) const; // 物品是否符合法术要求

        // ==================== 法术家族和修改 ====================
        bool IsAffected(uint32 familyName, flag96 const& familyFlags) const; // 是否受法术家族影响
        bool IsAffectedBySpellMods() const;                 // 是否受法术修改影响
        bool IsAffectedBySpellMod(SpellModifier const* mod) const; // 是否受特定法术修改影响

        // ==================== 免疫和驱散 ====================
        bool CanPierceImmuneAura(SpellInfo const* auraSpellInfo) const; // 是否能穿透免疫光环
        bool CanDispelAura(SpellInfo const* auraSpellInfo) const; // 是否能驱散光环

        // ==================== 光环互斥 ====================
        bool IsSingleTarget() const;                        // 是否为单体目标法术
        bool IsAuraExclusiveBySpecificWith(SpellInfo const* spellInfo) const; // 是否与指定法术光环互斥
        bool IsAuraExclusiveBySpecificPerCasterWith(SpellInfo const* spellInfo) const; // 是否按施法者与指定法术光环互斥

        // ==================== 施法条件检查 ====================
        SpellCastResult CheckShapeshift(uint32 form) const; // 检查变形形态限制
        SpellCastResult CheckLocation(uint32 map_id, uint32 zone_id, uint32 area_id, Player const* player = nullptr, bool strict = true) const; // 检查施法位置
        SpellCastResult CheckTarget(WorldObject const* caster, WorldObject const* target, bool implicit = true) const; // 检查目标有效性
        SpellCastResult CheckExplicitTarget(WorldObject const* caster, WorldObject const* target, Item const* itemTarget = nullptr) const; // 检查显式目标
        SpellCastResult CheckVehicle(Unit const* caster) const; // 检查载具限制
        bool CheckTargetCreatureType(Unit const* target) const; // 检查目标生物类型

        // ==================== 机制和学派 ====================
        SpellSchoolMask GetSchoolMask() const;              // 获取法术学派掩码
        uint32 GetAllEffectsMechanicMask() const;           // 获取所有效果的机制掩码
        uint32 GetEffectMechanicMask(SpellEffIndex effIndex) const; // 获取指定效果的机制掩码
        uint32 GetSpellMechanicMaskByEffectMask(uint32 effectMask) const; // 根据效果掩码获取机制掩码
        Mechanics GetEffectMechanic(SpellEffIndex effIndex) const; // 获取指定效果的机制
        uint32 GetDispelMask() const;                       // 获取驱散掩码
        static uint32 GetDispelMask(DispelType type);       // 根据驱散类型获取驱散掩码
        uint32 GetExplicitTargetMask() const;               // 获取显式目标掩码

        // ==================== 光环状态和特定类型 ====================
        AuraStateType GetAuraState() const;                 // 获取光环状态
        SpellSpecificType GetSpellSpecific() const;         // 获取法术特定类型

        // ==================== 施法范围 ====================
        float GetMinRange(bool positive = false) const;     // 获取最小施法范围
        float GetMaxRange(bool positive = false, WorldObject* caster = nullptr, Spell* spell = nullptr) const; // 获取最大施法范围

        // ==================== 持续时间 ====================
        int32 GetDuration() const;                          // 获取法术持续时间
        int32 GetMaxDuration() const;                       // 获取最大持续时间

        uint32 GetMaxTicks() const;                         // 获取最大跳数（周期性效果）

        // ==================== 施法时间和冷却 ====================
        uint32 CalcCastTime(Spell* spell = nullptr) const;  // 计算施法时间
        uint32 GetRecoveryTime() const;                     // 获取恢复时间（冷却时间）

        // ==================== 能量消耗 ====================
        int32 CalcPowerCost(WorldObject const* caster, SpellSchoolMask schoolMask, Spell* spell = nullptr) const; // 计算法术能量消耗

        // ==================== 法术等级 ====================
        bool IsRanked() const;                              // 是否有等级
        uint8 GetRank() const;                              // 获取等级编号
        SpellInfo const* GetFirstRankSpell() const;         // 获取第一级法术
        SpellInfo const* GetLastRankSpell() const;          // 获取最后一级法术
        SpellInfo const* GetNextRankSpell() const;          // 获取下一级法术
        SpellInfo const* GetPrevRankSpell() const;          // 获取上一级法术
        SpellInfo const* GetAuraRankForLevel(uint8 level) const; // 根据等级获取对应光环等级
        bool IsRankOf(SpellInfo const* spellInfo) const;    // 是否为同一法术的不同等级
        bool IsDifferentRankOf(SpellInfo const* spellInfo) const; // 是否为不同等级
        bool IsHighRankOf(SpellInfo const* spellInfo) const; // 是否为高级别版本

        // ==================== 效果访问 ====================
        std::array<SpellEffectInfo, MAX_SPELL_EFFECTS> const& GetEffects() const { return _effects; }
        SpellEffectInfo const& GetEffect(SpellEffIndex index) const { ASSERT(index < _effects.size()); return _effects[index]; }

        // ==================== 递减收益（DR）====================
        // 递减收益系统用于控制控制效果（如昏迷、恐惧等）的持续时间随施放次数递减
        DiminishingGroup GetDiminishingReturnsGroupForSpell(bool triggered) const; // 获取递减收益分组
        DiminishingReturnsType GetDiminishingReturnsGroupType(bool triggered) const; // 获取递减收益类型
        DiminishingLevels GetDiminishingReturnsMaxLevel(bool triggered) const; // 获取递减最大等级
        int32 GetDiminishingReturnsLimitDuration(bool triggered) const; // 获取递减持续时间限制

        // ==================== 法术免疫 ====================
        void ApplyAllSpellImmunitiesTo(Unit* target, SpellEffectInfo const& spellEffectInfo, bool apply) const; // 应用所有法术免疫
        bool CanSpellProvideImmunityAgainstAura(SpellInfo const* auraSpellInfo) const; // 法术是否能提供对抗光环的免疫
        bool SpellCancelsAuraEffect(AuraEffect const* aurEff) const; // 法术是否取消光环效果

        uint32 GetAllowedMechanicMask() const;              // 获取允许的机制掩码
        uint32 GetMechanicImmunityMask(Unit* caster) const; // 获取机制免疫掩码

    private:
        // ==================== 加载辅助函数 ====================
        void _InitializeExplicitTargetMask();               // 初始化显式目标掩码
        void _InitializeSpellPositivity();                  // 初始化法术正负性
        void _LoadSpellSpecific();                          // 加载法术特定类型
        void _LoadAuraState();                              // 加载光环状态
        void _LoadSpellDiminishInfo();                      // 加载递减收益信息
        void _LoadImmunityInfo();                           // 加载免疫信息

        std::array<SpellEffectInfo, MAX_SPELL_EFFECTS>& _GetEffects() { return _effects; }
        SpellEffectInfo& _GetEffect(SpellEffIndex index) { ASSERT(index < _effects.size()); return _effects[index]; }

        // ==================== 卸载辅助函数 ====================
        void _UnloadImplicitTargetConditionLists();         // 卸载隐式目标条件列表

        // ==================== 私有成员变量 ====================
        SpellSpecificType _spellSpecific;                   // 法术特定类型缓存
        AuraStateType _auraState;                           // 光环状态缓存

        SpellDiminishInfo _diminishInfoNonTriggered;        // 非触发法术递减信息
        SpellDiminishInfo _diminishInfoTriggered;           // 触发法术递减信息

        uint32 _allowedMechanicMask;                        // 允许的机制掩码缓存
};

#endif // _SPELLINFO_H
