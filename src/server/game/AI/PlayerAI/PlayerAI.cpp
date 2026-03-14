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
 * @file PlayerAI.cpp
 * @brief 玩家AI模块实现文件
 *
 * 本文件实现了PlayerAI及其子类SimpleCharmedPlayerAI，提供AI控制玩家角色的完整功能。
 * 主要内容包括：
 * - 所有职业和专精的法术ID定义
 * - PlayerAI基类的核心功能实现
 * - SimpleCharmedPlayerAI的具体行为逻辑
 *
 * 核心功能：
 * - 法术验证与选择机制
 * - 职业专精识别与智能施法
 * - 远程/近战攻击处理
 * - 变形状态管理
 * - 魅惑状态下的行为控制
 *
 * 设计特点：
 * - 基于权重的法术选择系统，支持智能决策
 * - 完整的职业支持（战士、圣骑士、猎人等10个职业）
 * - 每个职业的三个专精都有定制的施法逻辑
 *
 * 性能考虑：
 * - 专精和治疗者状态在构造时缓存，避免重复计算
 * - 法术验证涉及较多检查，应合理控制调用频率
 */

#include "PlayerAI.h"
#include "CommonHelpers.h"
#include "Creature.h"
#include "Item.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellMgr.h"

/**
 * @enum Spells
 * @brief 法术ID枚举
 *
 * 定义所有职业和专精在AI控制下可能使用的法术ID。
 * 这些法术ID用于AI选择合适的技能进行施放。
 *
 * 组织结构：
 * - 通用法术（自动射击、投掷等）
 * - 按职业分类（战士、圣骑士、猎人等）
 * - 每个职业内按专精分类
 */
enum Spells
{
    /* Generic - 通用法术 */
    SPELL_AUTO_SHOT         =    75,    ///< 自动射击
    SPELL_SHOOT             =  3018,    ///< 射击（弓/枪/弩）
    SPELL_THROW             =  2764,    ///< 投掷
    SPELL_SHOOT_WAND        =  5019,    ///< 魔杖射击

    /* Warrior - Generic - 战士通用法术 */
    SPELL_BATTLE_STANCE     =  2457,    ///< 战斗姿态
    SPELL_BERSERKER_STANCE  =  2458,    ///< 狂暴姿态
    SPELL_DEFENSIVE_STANCE  =    71,    ///< 防御姿态
    SPELL_CHARGE            = 11578,    ///< 冲锋
    SPELL_INTERCEPT         = 20252,    ///< 拦截
    SPELL_ENRAGED_REGEN     = 55694,    ///< 狂怒回复
    SPELL_INTIMIDATING_SHOUT=  5246,    ///< 恐吓吼叫
    SPELL_PUMMEL            =  6552,    ///< 拳击（打断）
    SPELL_SHIELD_BASH       =    72,    ///< 盾击（打断）
    SPELL_BLOODRAGE         =  2687,    ///< 血性狂暴

    /* Warrior - Arms - 战士武器专精 */
    SPELL_SWEEPING_STRIKES  = 12328,    ///< 横扫攻击
    SPELL_MORTAL_STRIKE     = 12294,    ///< 致死打击
    SPELL_BLADESTORM        = 46924,    ///< 剑刃风暴
    SPELL_REND              = 47465,    ///< 撕裂
    SPELL_RETALIATION       = 20230,    ///< 反击风暴
    SPELL_SHATTERING_THROW  = 64382,    ///< 碎裂投掷
    SPELL_THUNDER_CLAP      = 47502,    ///< 雷霆一击

    /* Warrior - Fury - 战士狂暴专精 */
    SPELL_DEATH_WISH        = 12292,    ///< 死亡之愿
    SPELL_BLOODTHIRST       = 23881,    ///< 嗜血
    PASSIVE_TITANS_GRIP       = 46917,  ///< 泰坦之握（被动）
    SPELL_DEMO_SHOUT        = 47437,    ///< 挫志怒吼
    SPELL_EXECUTE           = 47471,    ///< 斩杀
    SPELL_HEROIC_FURY       = 60970,    ///< 英勇之怒
    SPELL_RECKLESSNESS      =  1719,    ///< 鲁莽
    SPELL_PIERCING_HOWL     = 12323,    ///< 刺耳怒吼

    /* Warrior - Protection - 战士防护专精 */
    SPELL_VIGILANCE         = 50720,    ///< 警戒
    SPELL_DEVASTATE         = 20243,    ///< 毁灭打击
    SPELL_SHOCKWAVE         = 46968,    ///< 震荡波
    SPELL_CONCUSSION_BLOW   = 12809,    ///< 震荡猛击
    SPELL_DISARM            =   676,    ///< 缴械
    SPELL_LAST_STAND        = 12975,    ///< 破釜沉舟
    SPELL_SHIELD_BLOCK      =  2565,    ///< 盾牌格挡
    SPELL_SHIELD_SLAM       = 47488,    ///< 盾牌猛击
    SPELL_SHIELD_WALL       =   871,    ///< 盾墙
    SPELL_SPELL_REFLECTION  = 23920,    ///< 法术反射

    /* Paladin - Generic - 圣骑士通用法术 */
    SPELL_AURA_MASTERY          = 31821,    ///< 光环精通
    SPELL_LAY_ON_HANDS          = 48788,    ///< 圣疗术
    SPELL_BLESSING_OF_MIGHT     = 48932,    ///< 力量祝福
    SPELL_AVENGING_WRATH        = 31884,    ///< 复仇之怒
    SPELL_DIVINE_PROTECTION     =   498,    ///< 圣佑术
    SPELL_DIVINE_SHIELD         =   642,    ///< 圣盾术
    SPELL_HAMMER_OF_JUSTICE     = 10308,    ///< 制裁之锤
    SPELL_HAND_OF_FREEDOM       =  1044,    ///< 自由之手
    SPELL_HAND_OF_PROTECTION    = 10278,    ///< 保护之手
    SPELL_HAND_OF_SACRIFICE     =  6940,    ///< 牺牲之手

    /* Paladin - Holy - 圣骑士神圣专精 */
    PASSIVE_ILLUMINATION        = 20215,    ///< 照明（被动）
    SPELL_HOLY_SHOCK            = 20473,    ///< 神圣震击
    SPELL_BEACON_OF_LIGHT       = 53563,    ///< 光明信标
    SPELL_CONSECRATION          = 48819,    ///< 奉献
    SPELL_FLASH_OF_LIGHT        = 48785,    ///< 圣光闪现
    SPELL_HOLY_LIGHT            = 48782,    ///< 圣光术
    SPELL_DIVINE_FAVOR          = 20216,    ///< 神恩术
    SPELL_DIVINE_ILLUMINATION   = 31842,    ///< 神圣启示

    /* Paladin - Protection - 圣骑士防护专精 */
    SPELL_BLESS_OF_SANC         = 20911,    ///< 庇护祝福
    SPELL_HOLY_SHIELD           = 20925,    ///< 神圣之盾
    SPELL_AVENGERS_SHIELD       = 48827,    ///< 复仇者之盾
    SPELL_DIVINE_SACRIFICE      = 64205,    ///< 神圣牺牲
    SPELL_HAMMER_OF_RIGHTEOUS   = 53595,    ///< 正义之锤
    SPELL_RIGHTEOUS_FURY        = 25780,    ///< 正义之怒
    SPELL_SHIELD_OF_RIGHTEOUS   = 61411,    ///< 正义之盾

    /* Paladin - Retribution - 圣骑士惩戒专精 */
    SPELL_SEAL_OF_COMMAND       = 20375,    ///< 命令圣印
    SPELL_CRUSADER_STRIKE       = 35395,    ///< 十字军打击
    SPELL_DIVINE_STORM          = 53385,    ///< 神圣风暴
    SPELL_JUDGEMENT             = 20271,    ///< 审判
    SPELL_HAMMER_OF_WRATH       = 48806,    ///< 愤怒之锤

    /* Hunter - Generic - 猎人通用法术 */
    SPELL_DETERRENCE        = 19263,    ///< 威慑
    SPELL_EXPLOSIVE_TRAP    = 49067,    ///< 爆炸陷阱
    SPELL_FREEZING_ARROW    = 60192,    ///< 冰冻箭
    SPELL_RAPID_FIRE        =  3045,    ///< 急速射击
    SPELL_KILL_SHOT         = 61006,    ///< 杀戮射击
    SPELL_MULTI_SHOT        = 49048,    ///< 多重射击
    SPELL_VIPER_STING       =  3034,    ///< 蝰蛇钉刺

    /* Hunter - Beast Mastery - 猎人野兽控制专精 */
    SPELL_BESTIAL_WRATH     = 19574,    ///< 野性狂怒
    PASSIVE_BEAST_WITHIN    = 34692,    ///< 野兽之心（被动）
    PASSIVE_BEAST_MASTERY   = 53270,    ///< 野兽掌握（被动）

    /* Hunter - Marksmanship - 猎人射击专精 */
    SPELL_AIMED_SHOT        = 19434,    ///< 瞄准射击
    PASSIVE_TRUESHOT_AURA   = 19506,    ///< 强击光环（被动）
    SPELL_CHIMERA_SHOT      = 53209,    ///< 奇美拉射击
    SPELL_ARCANE_SHOT       = 49045,    ///< 奥术射击
    SPELL_STEADY_SHOT       = 49052,    ///< 稳固射击
    SPELL_READINESS         = 23989,    ///< 战备
    SPELL_SILENCING_SHOT    = 34490,    ///< 沉默射击

    /* Hunter - Survival - 猎人生存专精 */
    PASSIVE_LOCK_AND_LOAD   = 56344,    ///< 装填（被动）
    SPELL_WYVERN_STING      = 19386,    ///< 翼龙钉刺
    SPELL_EXPLOSIVE_SHOT    = 53301,    ///< 爆炸射击
    SPELL_BLACK_ARROW       =  3674,    ///< 黑箭

    /* Rogue - Generic - 盗贼通用法术 */
    SPELL_DISMANTLE         = 51722,    ///< 拆卸
    SPELL_EVASION           = 26669,    ///< 闪避
    SPELL_KICK              =  1766,    ///< 脚踢（打断）
    SPELL_VANISH            = 26889,    ///< 消失
    SPELL_BLIND             =  2094,    ///< 致盲
    SPELL_CLOAK_OF_SHADOWS  = 31224,    ///< 暗影斗篷

    /* Rogue - Assassination - 盗贼刺杀专精 */
    SPELL_COLD_BLOOD        = 14177,    ///< 冷血
    SPELL_MUTILATE          =  1329,    ///< 毁伤
    SPELL_HUNGER_FOR_BLOOD  = 51662,    ///< 饥渴嗜血
    SPELL_ENVENOM           = 57993,    ///< 毒伤

    /* Rogue - Combat - 盗贼战斗专精 */
    SPELL_SINISTER_STRIKE   = 48637,    ///< 邪恶攻击
    SPELL_BLADE_FLURRY      = 13877,    ///< 剑刃乱舞
    SPELL_ADRENALINE_RUSH   = 13750,    ///< 冲动
    SPELL_KILLING_SPREE     = 51690,    ///< 杀戮盛宴
    SPELL_EVISCERATE        = 48668,    ///< 剔骨

    /* Rogue - Sublety - 盗贼敏锐专精 */
    SPELL_HEMORRHAGE        = 16511,    ///< 出血
    SPELL_PREMEDITATION     = 14183,    ///< 预谋
    SPELL_SHADOW_DANCE      = 51713,    ///< 暗影之舞
    SPELL_PREPARATION       = 14185,    ///< 预备
    SPELL_SHADOWSTEP        = 36554,    ///< 暗影步

    /* Priest - Generic - 牧师通用法术 */
    SPELL_FEAR_WARD         =  6346,    ///< 防护恐惧结界
    SPELL_POWER_WORD_FORT   = 48161,    ///< 真言术：耐
    SPELL_DIVINE_SPIRIT     = 48073,    ///< 神圣精神
    SPELL_SHADOW_PROTECTION = 48169,    ///< 暗影防护
    SPELL_DIVINE_HYMN       = 64843,    ///< 神圣赞美诗
    SPELL_HYMN_OF_HOPE      = 64901,    ///< 希望赞美诗
    SPELL_SHADOW_WORD_DEATH = 48158,    ///< 暗言术：死
    SPELL_PSYCHIC_SCREAM    = 10890,    ///< 心灵尖啸

    /* Priest - Discipline - 牧师戒律专精 */
    PASSIVE_SOUL_WARDING      = 63574,  ///< 灵魂卫士（被动）
    SPELL_POWER_INFUSION      = 10060,  ///< 能量灌注
    SPELL_PENANCE             = 47540,  ///< 苦修
    SPELL_PAIN_SUPPRESSION    = 33206,  ///< 疼痛压制
    SPELL_INNER_FOCUS         = 14751,  ///< 心灵集中
    SPELL_POWER_WORD_SHIELD   = 48066,  ///< 真言术：盾

    /* Priest - Holy - 牧师神圣专精 */
    PASSIVE_SPIRIT_REDEMPTION = 20711,  ///< 精神救赎（被动）
    SPELL_DESPERATE_PRAYER    = 19236,  ///< 绝望祷言
    SPELL_GUARDIAN_SPIRIT     = 47788,  ///< 守护圣灵
    SPELL_FLASH_HEAL          = 48071,  ///< 快速治疗
    SPELL_RENEW               = 48068,  ///< 恢复

    /* Priest - Shadow - 牧师暗影专精 */
    SPELL_VAMPIRIC_EMBRACE    = 15286,  ///< 吸血鬼的拥抱
    SPELL_SHADOWFORM          = 15473,  ///< 暗影形态
    SPELL_VAMPIRIC_TOUCH      = 34914,  ///< 吸血鬼之触
    SPELL_MIND_FLAY           = 15407,  ///< 精神鞭笞
    SPELL_MIND_BLAST          = 48127,  ///< 精神冲击
    SPELL_SHADOW_WORD_PAIN    = 48125,  ///< 暗言术：痛
    SPELL_DEVOURING_PLAGUE    = 48300,  ///< 吞噬瘟疫
    SPELL_DISPERSION          = 47585,  ///< 消散

    /* Death Knight - Generic - 死亡骑士通用法术 */
    SPELL_DEATH_GRIP        = 49576,    ///< 死亡之握
    SPELL_STRANGULATE       = 47476,    ///< 绞杀
    SPELL_EMPOWER_RUNE_WEAP = 47568,    ///< 符文武器强化
    SPELL_ICEBORN_FORTITUDE = 48792,    ///< 冰封之韧
    SPELL_ANTI_MAGIC_SHELL  = 48707,    ///< 反魔法护罩
    SPELL_DEATH_COIL_DK     = 49895,    ///< 死亡缠绕
    SPELL_MIND_FREEZE       = 47528,    ///< 心灵冰冻（打断）
    SPELL_ICY_TOUCH         = 49909,    ///< 冰霜之触
    AURA_FROST_FEVER        = 55095,    ///< 冰霜热疫（光环）
    SPELL_PLAGUE_STRIKE     = 49921,    ///< 瘟疫打击
    AURA_BLOOD_PLAGUE       = 55078,    ///< 鲜血瘟疫（光环）
    SPELL_PESTILENCE        = 50842,    ///< 瘟疫同化

    /* Death Knight - Blood - 死亡骑士鲜血专精 */
    SPELL_RUNE_TAP          = 48982,    ///< 符文分流
    SPELL_HYSTERIA          = 49016,    ///< 癫狂
    SPELL_HEART_STRIKE      = 55050,    ///< 心脏打击
    SPELL_DEATH_STRIKE      = 49924,    ///< 灵界打击
    SPELL_BLOOD_STRIKE      = 49930,    ///< 鲜血打击
    SPELL_MARK_OF_BLOOD     = 49005,    ///< 鲜血印记
    SPELL_VAMPIRIC_BLOOD    = 55233,    ///< 鲜血灵气

    /* Death Knight - Frost - 死亡骑士冰霜专精 */
    PASSIVE_ICY_TALONS      = 50887,    ///< 寒冰之爪（被动）
    SPELL_FROST_STRIKE      = 49143,    ///< 冰霜打击
    SPELL_HOWLING_BLAST     = 49184,    ///< 凛风冲击
    SPELL_UNBREAKABLE_ARMOR = 51271,    ///< 坚不可摧护甲
    SPELL_OBLITERATE        = 51425,    ///< 灭寂
    SPELL_DEATHCHILL        = 49796,    ///< 死亡寒颤

    /* Death Knight - Unholy - 死亡骑士邪恶专精 */
    PASSIVE_UNHOLY_BLIGHT   = 49194,    ///< 邪恶虫群（被动）
    PASSIVE_MASTER_OF_GHOUL = 52143,    ///< 食尸鬼主宰（被动）
    SPELL_SCOURGE_STRIKE    = 55090,    ///< 脓疮打击
    SPELL_DEATH_AND_DECAY   = 49938,    ///< 死亡凋零
    SPELL_ANTI_MAGIC_ZONE   = 51052,    ///< 反魔法领域
    SPELL_SUMMON_GARGOYLE   = 49206,    ///< 召唤石像鬼

    /* Shaman - Generic - 萨满通用法术 */
    SPELL_HEROISM           = 32182,    ///< 英勇
    SPELL_BLOODLUST         =  2825,    ///< 嗜血
    SPELL_GROUNDING_TOTEM   =  8177,    ///< 接地图腾

    /* Shaman - Elemental - 萨满元素专精 */
    PASSIVE_ELEMENTAL_FOCUS = 16164,    ///< 元素精准（被动）
    SPELL_TOTEM_OF_WRATH    = 30706,    ///< 愤怒图腾
    SPELL_THUNDERSTORM      = 51490,    ///< 雷暴
    SPELL_LIGHTNING_BOLT    = 49238,    ///< 闪电箭
    SPELL_EARTH_SHOCK       = 49231,    ///< 地震术
    SPELL_FLAME_SHOCK       = 49233,    ///< 烈焰震击
    SPELL_LAVA_BURST        = 60043,    ///< 熔岩爆裂
    SPELL_CHAIN_LIGHTNING   = 49271,    ///< 闪电链
    SPELL_ELEMENTAL_MASTERY = 16166,    ///< 元素掌握

    /* Shaman - Enhancement - 萨满增强专精 */
    PASSIVE_SPIRIT_WEAPONS  = 16268,    ///< 灵魂武器（被动）
    SPELL_LAVA_LASH         = 60103,    ///< 熔岩猛击
    SPELL_FERAL_SPIRIT      = 51533,    ///< 野性狼魂
    AURA_MAELSTROM_WEAPON   = 53817,    ///< 漩涡武器（光环）
    SPELL_STORMSTRIKE       = 17364,    ///< 风暴打击
    SPELL_SHAMANISTIC_RAGE  = 30823,    ///< 萨满之怒

    /* Shaman - Restoration - 萨满恢复专精 */
    SPELL_SHA_NATURE_SWIFT  =   591,    ///< 自然迅捷
    SPELL_MANA_TIDE_TOTEM   =   590,    ///< 法力之潮图腾
    SPELL_EARTH_SHIELD      = 49284,    ///< 大地之盾
    SPELL_RIPTIDE           = 61295,    ///< 激流
    SPELL_HEALING_WAVE      = 49273,    ///< 治疗波
    SPELL_LESSER_HEAL_WAVE  = 49276,    ///< 次级治疗波
    SPELL_TIDAL_FORCE       = 55198,    ///< 潮汐之力

    /* Mage - Generic - 法师通用法术 */
    SPELL_DAMPEN_MAGIC      = 43015,    ///< 抑制魔法
    SPELL_EVOCATION         = 12051,    ///< 唤醒
    SPELL_MANA_SHIELD       = 43020,    ///< 法力护盾
    SPELL_MIRROR_IMAGE      = 55342,    ///< 镜像
    SPELL_SPELLSTEAL        = 30449,    ///< 偷取法术
    SPELL_COUNTERSPELL      =  2139,    ///< 法术反制（打断）
    SPELL_ICE_BLOCK         = 45438,    ///< 寒冰屏障

    /* Mage - Arcane - 法师奥术专精 */
    SPELL_FOCUS_MAGIC       = 54646,    ///< 专注魔法
    SPELL_ARCANE_POWER      = 12042,    ///< 奥术强化
    SPELL_ARCANE_BARRAGE    = 44425,    ///< 奥术弹幕
    SPELL_ARCANE_BLAST      = 42897,    ///< 奥术冲击
    AURA_ARCANE_BLAST       = 36032,    ///< 奥术冲击（光环）
    SPELL_ARCANE_MISSILES   = 42846,    ///< 奥术飞弹
    SPELL_PRESENCE_OF_MIND  = 12043,    ///< 气定神闲

    /* Mage - Fire - 法师火焰专精 */
    SPELL_PYROBLAST         = 11366,    ///< 炎爆术
    SPELL_COMBUSTION        = 11129,    ///< 燃烧
    SPELL_LIVING_BOMB       = 44457,    ///< 活体炸弹
    SPELL_FIREBALL          = 42833,    ///< 火球术
    SPELL_FIRE_BLAST        = 42873,    ///< 火焰冲击
    SPELL_DRAGONS_BREATH    = 31661,    ///< 龙息术
    SPELL_BLAST_WAVE        = 11113,    ///< 冲击波

    /* Mage - Frost - 法师冰霜专精 */
    SPELL_ICY_VEINS         = 12472,    ///< 冰冷血脉
    SPELL_ICE_BARRIER       = 11426,    ///< 寒冰护体
    SPELL_DEEP_FREEZE       = 44572,    ///< 深度冻结
    SPELL_FROST_NOVA        = 42917,    ///< 冰霜新星
    SPELL_FROSTBOLT         = 42842,    ///< 寒冰箭
    SPELL_COLD_SNAP         = 11958,    ///< 急速冷却
    SPELL_ICE_LANCE         = 42914,    ///< 冰枪术

    /* Warlock - Generic - 术士通用法术 */
    SPELL_FEAR                 =  6215,    ///< 恐惧
    SPELL_HOWL_OF_TERROR       = 17928,    ///< 恐惧嚎叫
    SPELL_CORRUPTION           = 47813,    ///< 腐蚀术
    SPELL_DEATH_COIL_W         = 47860,    ///< 死亡缠绕（术士版）
    SPELL_SHADOW_BOLT          = 47809,    ///< 暗影箭
    SPELL_INCINERATE           = 47838,    ///< 烧尽
    SPELL_IMMOLATE             = 47811,    ///< 献祭
    SPELL_SEED_OF_CORRUPTION   = 47836,    ///< 腐蚀之种

    /* Warlock - Affliction - 术士痛苦专精 */
    PASSIVE_SIPHON_LIFE        = 63108,    ///< 生命虹吸（被动）
    SPELL_UNSTABLE_AFFLICTION  = 30108,    ///< 痛苦无常
    SPELL_HAUNT                = 48181,    ///< 鬼影缠身
    SPELL_CURSE_OF_AGONY       = 47864,    ///< 痛苦诅咒
    SPELL_DRAIN_SOUL           = 47855,    ///< 吸取灵魂

    /* Warlock - Demonology - 术士恶魔专精 */
    SPELL_SOUL_LINK            = 19028,    ///< 灵魂链接
    SPELL_DEMONIC_EMPOWERMENT  = 47193,    ///< 恶魔增效
    SPELL_METAMORPHOSIS        = 59672,    ///< 恶魔变形
    SPELL_IMMOLATION_AURA      = 50589,    ///< 献祭光环
    SPELL_DEMON_CHARGE         = 54785,    ///< 恶魔冲锋
    AURA_DECIMATION            = 63167,    ///< 斩杀（光环）
    AURA_MOLTEN_CORE           = 71165,    ///< 熔火之心（光环）
    SPELL_SOUL_FIRE            = 47825,    ///< 灵魂之火

    /* Warlock - Destruction - 术士毁灭专精 */
    SPELL_SHADOWBURN           = 17877,    ///< 暗影灼烧
    SPELL_CONFLAGRATE          = 17962,    ///< 燃烧
    SPELL_CHAOS_BOLT           = 50796,    ///< 混乱之箭
    SPELL_SHADOWFURY           = 47847,    ///< 暗影之怒

    /* Druid - Generic - 德鲁伊通用法术 */
    SPELL_BARKSKIN             = 22812,    ///< 树皮术
    SPELL_INNERVATE            = 29166,    ///< 激活

    /* Druid - Balance - 德鲁伊平衡专精 */
    SPELL_INSECT_SWARM        =  5570,    ///< 虫群
    SPELL_MOONKIN_FORM        = 24858,    ///< 枭兽形态
    SPELL_STARFALL            = 48505,    ///< 星落
    SPELL_TYPHOON             = 61384,    ///< 台风
    AURA_ECLIPSE_LUNAR        = 48518,    ///< 月食（光环）
    SPELL_MOONFIRE            = 48463,    ///< 月火术
    SPELL_STARFIRE            = 48465,    ///< 星火术
    SPELL_WRATH               = 48461,    ///< 愤怒

    /* Druid - Feral - 德鲁伊野性专精 */
    SPELL_CAT_FORM            =   768,    ///< 猎豹形态
    SPELL_SURVIVAL_INSTINCTS  = 61336,    ///< 生存本能
    SPELL_MANGLE              = 33917,    ///< 撕碎（熊形态）
    SPELL_BERSERK             = 50334,    ///< 狂暴
    SPELL_MANGLE_CAT          = 48566,    ///< 撕碎（猫形态）
    SPELL_FERAL_CHARGE_CAT    = 49376,    ///< 野性冲锋（猫）
    SPELL_RAKE                = 48574,    ///< 斜掠
    SPELL_RIP                 = 49800,    ///< 撕裂
    SPELL_SAVAGE_ROAR         = 52610,    ///< 野蛮咆哮
    SPELL_TIGER_FURY          = 50213,    ///< 猛虎之怒
    SPELL_CLAW                = 48570,    ///< 爪击
    SPELL_DASH                = 33357,    ///< 急奔
    SPELL_MAIM                = 49802,    ///< 割碎

    /* Druid - Restoration - 德鲁伊恢复专精 */
    SPELL_SWIFTMEND           = 18562,    ///< 迅捷治愈
    SPELL_TREE_OF_LIFE        = 33891,    ///< 生命之树形态
    SPELL_WILD_GROWTH         = 48438,    ///< 野性成长
    SPELL_NATURE_SWIFTNESS    = 17116,    ///< 自然迅捷
    SPELL_TRANQUILITY         = 48447,    ///< 宁静
    SPELL_NOURISH             = 50464,    ///< 滋养
    SPELL_HEALING_TOUCH       = 48378,    ///< 治疗之触
    SPELL_REJUVENATION        = 48441,    ///< 回春术
    SPELL_REGROWTH            = 48443,    ///< 愈合
    SPELL_LIFEBLOOM           = 48451     ///< 生命绽放
};

/**
 * @brief PlayerAI构造函数
 * @param player 被AI控制的玩家对象指针
 *
 * 初始化玩家AI，执行以下操作：
 * - 调用基类UnitAI构造函数
 * - 初始化玩家指针引用
 * - 计算并缓存玩家的专精信息
 * - 计算并缓存治疗者状态
 * - 计算并缓存远程攻击者状态
 *
 * 性能考虑：
 * - 专精和角色状态在构造时一次性计算，避免后续重复计算
 * - 使用Trinity::Helpers::Entity辅助函数进行计算
 */
PlayerAI::PlayerAI(Player* player) : UnitAI(player), me(player),
    _selfSpec(Trinity::Helpers::Entity::GetPlayerSpecialization(player)),
    _isSelfHealer(Trinity::Helpers::Entity::IsPlayerHealer(player)),
    _isSelfRangedAttacker(Trinity::Helpers::Entity::IsPlayerRangedAttacker(player))
{
}

/**
 * @brief 获取魅惑者
 * @return Creature* 魅惑该玩家的生物指针，如果不存在或不是生物则返回nullptr
 *
 * 检查玩家的魅惑者GUID是否为生物，如果是则返回对应的生物对象。
 *
 * 调用时机：
 * - 确定攻击目标时
 * - 需要跟随或保护魅惑者时
 * - 施放需要魅惑者作为目标的法术时
 */
Creature* PlayerAI::GetCharmer() const
{
    // 检查魅惑者GUID是否为生物类型
    if (me->GetCharmerGUID().IsCreature())
        return ObjectAccessor::GetCreature(*me, me->GetCharmerGUID());
    return nullptr;
}

/**
 * @brief 获取玩家专精索引
 * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
 * @return uint8 专精索引，范围0-2（0为最左侧专精，2为最右侧）
 *
 * 根据天赋点分布确定玩家的主要专精。如果传入的玩家是被控玩家自己，
 * 则返回构造时缓存的专精值；否则实时计算指定玩家的专精。
 *
 * 调用时机：
 * - 选择合适的法术时
 * - 确定角色定位时
 *
 * 性能考虑：
 * - 对自身的调用返回缓存值，开销极低
 * - 对其他玩家的调用需要实时计算
 */
uint8 PlayerAI::GetSpec(Player const * who) const
{
    return (!who || who == me) ? _selfSpec : Trinity::Helpers::Entity::GetPlayerSpecialization(who);
}

/**
 * @brief 判断玩家是否为治疗者
 * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
 * @return bool 如果是治疗专精返回true
 *
 * 基于专精判断玩家是否为治疗者角色。对自身使用缓存值，其他玩家实时计算。
 *
 * 调用时机：
 * - 选择法术类型时
 * - 确定战斗角色时
 */
bool PlayerAI::IsHealer(Player const * who) const
{
    return (!who || who == me) ? _isSelfHealer : Trinity::Helpers::Entity::IsPlayerHealer(who);
}

/**
 * @brief 判断玩家是否为远程攻击者
 * @param who 要检查的玩家指针，nullptr表示使用被控玩家自身
 * @return bool 如果是远程攻击者返回true
 *
 * 基于职业和专精判断玩家是否应该进行远程攻击。
 * 对自身使用缓存值，其他玩家实时计算。
 *
 * 调用时机：
 * - 确定攻击方式时
 * - 选择站位时
 */
bool PlayerAI::IsRangedAttacker(Player const * who) const
{
    return (!who || who == me) ? _isSelfRangedAttacker : Trinity::Helpers::Entity::IsPlayerRangedAttacker(who);
}

/**
 * @brief 验证法术是否可以施放（指定单位目标）
 * @param spellId 法术ID
 * @param target 目标单位指针
 * @return TargetedSpell 包含法术和目标的pair，如果无法施放则为空
 *
 * 执行完整的法术施放验证流程：
 * 1. 查找玩家已知的最高等级法术（支持法术升级链）
 * 2. 验证法术信息有效性
 * 3. 检查全局冷却时间
 * 4. 检查法术是否可以自动施放（CanAutoCast）
 *
 * 实现细节：
 * - 优先检查传入的法术ID是否已学会
 * - 如果未学会，遍历法术升级链查找已学会的最高等级
 * - 创建Spell对象进行详细检查
 *
 * 注意：调用者负责清理返回的Spell对象指针
 *
 * 性能考虑：
 * - 涉及法术链查找和信息查询，应避免频繁调用
 * - CanAutoCast检查包含距离、资源、施法条件等多项验证
 */
PlayerAI::TargetedSpell PlayerAI::VerifySpellCast(uint32 spellId, Unit* target)
{
    // 查找玩家已知的最高等级法术
    uint32 knownRank, nextRank;
    if (me->HasSpell(spellId))
    {
        // 如果玩家已学会该法术ID，假设这是最高等级（优化常见情况）
        knownRank = spellId;
        nextRank = sSpellMgr->GetNextSpellInChain(spellId);
    }
    else
    {
        // 否则从法术链的起始点开始查找
        knownRank = 0;
        nextRank = sSpellMgr->GetFirstSpellInChain(spellId);
    }

    // 遍历法术链，找到玩家已学会的最高等级
    while (nextRank && me->HasSpell(nextRank))
    {
        knownRank = nextRank;
        nextRank = sSpellMgr->GetNextSpellInChain(knownRank);
    }

    // 如果没有找到已学会的等级，返回空
    if (!knownRank)
        return {};

    // 获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(knownRank);
    if (!spellInfo)
        return {};

    // 检查全局冷却
    if (me->GetSpellHistory()->HasGlobalCooldown(spellInfo))
        return {};

    // 创建法术对象并检查是否可以自动施放
    Spell* spell = new Spell(me, spellInfo, TRIGGERED_NONE);
    if (spell->CanAutoCast(target))
        return{ spell, target };

    // 验证失败，清理法术对象
    delete spell;
    return {};
}

/**
 * @brief 验证法术是否可以施放（指定目标类型）
 * @param spellId 法术ID
 * @param target 目标类型枚举
 * @return TargetedSpell 包含法术和目标的pair，如果无法施放则为空
 *
 * 根据目标类型枚举解析实际目标，然后调用验证逻辑：
 * - TARGET_NONE: 无目标法术
 * - TARGET_VICTIM: 当前目标
 * - TARGET_CHARMER: 魅惑者
 * - TARGET_SELF: 自身
 *
 * 注意：调用者负责清理返回的Spell对象指针
 */
PlayerAI::TargetedSpell PlayerAI::VerifySpellCast(uint32 spellId, SpellTarget target)
{
    // 根据目标类型解析实际目标
    Unit* pTarget = nullptr;
    switch (target)
    {
        case TARGET_NONE:
            break;
        case TARGET_VICTIM:
            pTarget = me->GetVictim();
            if (!pTarget)
                return {};
            break;
        case TARGET_CHARMER:
            pTarget = me->GetCharmer();
            if (!pTarget)
                return {};
            break;
        case TARGET_SELF:
            pTarget = me;
            break;
    }

    return VerifySpellCast(spellId, pTarget);
}

/**
 * @brief 从候选列表中选择一个法术
 * @param spells 法术候选列表（引用，会被清空）
 * @return TargetedSpell 被选中的法术-目标对
 *
 * 根据权重随机选择一个法术，删除列表中其他所有法术对象。
 * 选择算法：
 * 1. 计算所有权重的总和
 * 2. 生成0到总权重-1之间的随机数
 * 3. 按权重概率选择法术
 * 4. 清理所有未选中的法术对象
 *
 * 此方法会使传入的vector失效并清空，防止误用。
 *
 * 调用时机：
 * - 决定释放哪个法术时
 * - 完成法术选择后
 *
 * 性能考虑：
 * - 会清理所有未选中的法术对象，确保无内存泄漏
 */
PlayerAI::TargetedSpell PlayerAI::SelectSpellCast(PossibleSpellVector& spells)
{
    // 计算权重总和
    uint32 totalWeights = 0;
    for (PossibleSpell const& wSpell : spells)
        totalWeights += wSpell.second;

    // 生成随机数并选择法术
    TargetedSpell selected;
    uint32 randNum = urand(0, totalWeights - 1);
    for (PossibleSpell const& wSpell : spells)
    {
        if (selected)
        {
            // 已选定法术，删除后续所有未选中的法术对象
            delete wSpell.first.first;
            continue;
        }

        // 根据权重决定是否选择当前法术
        if (randNum < wSpell.second)
            selected = wSpell.first;
        else
        {
            // 未选中，减少随机计数并删除法术对象
            randNum -= wSpell.second;
            delete wSpell.first.first;
        }
    }

    // 清空列表并返回选中的法术
    spells.clear();
    return selected;
}

/**
 * @brief 对目标施放法术
 * @param spell 包含法术和目标的pair
 *
 * 辅助方法，执行选定的法术施放。设置目标并准备施法。
 *
 * 调用时机：
 * - 选定要释放的法术后
 */
void PlayerAI::DoCastAtTarget(TargetedSpell spell)
{
    SpellCastTargets targets;
    targets.SetUnitTarget(spell.second);
    spell.first->prepare(targets);
}

/**
 * @brief 如果准备好则执行远程攻击
 *
 * 检查并执行远程自动攻击（如射击、投掷等）。
 * 执行流程：
 * 1. 检查是否正在施法
 * 2. 检查远程攻击是否就绪
 * 3. 根据装备的远程武器类型确定攻击法术
 * 4. 创建并施放攻击法术
 * 5. 重置攻击计时器
 *
 * 武器类型映射：
 * - 弓/枪/弩 -> 射击（SPELL_SHOOT）
 * - 投掷武器 -> 投掷（SPELL_THROW）
 * - 魔杖 -> 魔杖射击（SPELL_SHOOT_WAND）
 *
 * 调用时机：
 * - UpdateAI中，针对远程职业
 */
void PlayerAI::DoRangedAttackIfReady()
{
    // 正在施法时不执行攻击
    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    // 检查远程攻击是否就绪
    if (!me->isAttackReady(RANGED_ATTACK))
        return;

    // 获取目标
    Unit* victim = me->GetVictim();
    if (!victim)
        return;

    // 根据远程武器类型确定攻击法术
    uint32 rangedAttackSpell = 0;

    Item const* rangedItem = me->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (ItemTemplate const* rangedTemplate = rangedItem ? rangedItem->GetTemplate() : nullptr)
    {
        switch (rangedTemplate->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_BOW:
            case ITEM_SUBCLASS_WEAPON_GUN:
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                rangedAttackSpell = SPELL_SHOOT;
                break;
            case ITEM_SUBCLASS_WEAPON_THROWN:
                rangedAttackSpell = SPELL_THROW;
                break;
            case ITEM_SUBCLASS_WEAPON_WAND:
                rangedAttackSpell = SPELL_SHOOT_WAND;
                break;
        }
    }

    // 没有远程武器或武器类型不支持
    if (!rangedAttackSpell)
        return;

    // 获取法术信息
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(rangedAttackSpell);
    if (!spellInfo)
        return;

    // 创建并检查法术是否可以施放
    Spell* spell = new Spell(me, spellInfo, TRIGGERED_CAST_DIRECTLY);
    if (spell->CheckPetCast(victim) != SPELL_CAST_OK)
    {
        delete spell;
        return;
    }

    // 准备施法
    SpellCastTargets targets;
    targets.SetUnitTarget(victim);
    spell->prepare(targets);

    // 重置远程攻击计时器
    me->resetAttackTimer(RANGED_ATTACK);
}

/**
 * @brief 如果准备好则执行自动攻击
 *
 * 根据玩家类型（远程/近战）选择合适的攻击方式：
 * - 远程攻击者：调用DoRangedAttackIfReady()
 * - 近战攻击者：调用DoMeleeAttackIfReady()
 *
 * 调用时机：
 * - UpdateAI中，作为主要的攻击执行方法
 */
void PlayerAI::DoAutoAttackIfReady()
{
    if (IsRangedAttacker())
        DoRangedAttackIfReady();
    else
        DoMeleeAttackIfReady();
}

/**
 * @brief 取消所有变形状态
 *
 * 取消玩家可以主动取消的所有变形效果。
 * 执行流程：
 * 1. 获取所有变形类型的光环
 * 2. 筛选可取消的变形（非不可取消、正面、非被动）
 * 3. 移除筛选出的变形光环
 *
 * 用于确保玩家处于正常形态以执行某些动作。
 *
 * 调用时机：
 * - 需要玩家回到正常形态时
 * - 切换专精形态时（如平衡德需要切换到枭兽形态）
 */
void PlayerAI::CancelAllShapeshifts()
{
    // 获取所有变形光环
    std::list<AuraEffect*> const& shapeshiftAuras = me->GetAuraEffectsByType(SPELL_AURA_MOD_SHAPESHIFT);
    std::set<Aura*> removableShapeshifts;

    // 筛选可以取消的变形
    for (AuraEffect* auraEff : shapeshiftAuras)
    {
        Aura* aura = auraEff->GetBase();
        if (!aura)
            continue;
        SpellInfo const* auraInfo = aura->GetSpellInfo();
        if (!auraInfo)
            continue;

        // 跳过不可取消的变形
        if (auraInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL))
            continue;

        // 只取消正面的、非被动的变形
        if (!auraInfo->IsPositive() || auraInfo->IsPassive())
            continue;

        removableShapeshifts.insert(aura);
    }

    // 移除所有可取消的变形
    for (Aura* aura : removableShapeshifts)
        me->RemoveOwnedAura(aura, AURA_REMOVE_BY_CANCEL);
}

/**
 * @brief 选择攻击目标
 * @return Unit* 选中的攻击目标指针
 *
 * 基类实现：返回魅惑者的当前目标。
 * 子类可重写此方法提供不同的目标选择逻辑。
 *
 * 调用时机：
 * - 需要确定攻击目标时
 * - 战斗开始时
 */
Unit* PlayerAI::SelectAttackTarget() const
{
    return me->GetCharmer() ? me->GetCharmer()->GetVictim() : nullptr;
}

/**
 * @struct ValidTargetSelectPredicate
 * @brief 有效目标选择谓词结构体
 *
 * 用于目标选择时的有效性判断。封装CanAIAttack检查。
 */
struct ValidTargetSelectPredicate
{
    ValidTargetSelectPredicate(UnitAI const* ai) : _ai(ai) { }
    UnitAI const* const _ai;

    /**
     * @brief 括号运算符，判断目标是否可攻击
     * @param target 目标单位
     * @return bool 如果目标可攻击返回true
     */
    bool operator()(Unit const* target) const
    {
        return _ai->CanAIAttack(target);
    }
};

/**
 * @brief 判断是否可以攻击指定目标
 * @param who 目标单位指针
 * @return bool 如果可以攻击返回true
 *
 * 重写基类方法，提供魅惑状态下的攻击条件检查：
 * 1. 目标必须是有效的攻击目标
 * 2. 目标不能有易碎的控制光环
 * 3. 魅惑者也必须能攻击该目标
 * 4. 调用基类检查
 */
bool SimpleCharmedPlayerAI::CanAIAttack(Unit const* who) const
{
    // 检查目标是否为有效攻击目标，且没有易碎的控制光环
    if (!me->IsValidAttackTarget(who) || who->HasBreakableByDamageCrowdControlAura())
        return false;

    // 检查魅惑者是否能攻击该目标
    if (Unit* charmer = me->GetCharmer())
        if (!charmer->IsValidAttackTarget(who))
            return false;

    return UnitAI::CanAIAttack(who);
}

/**
 * @brief 选择攻击目标
 * @return Unit* 选中的攻击目标
 *
 * 重写基类方法，实现魅惑状态下的目标选择逻辑：
 * 1. 获取魅惑者的AI
 * 2. 通过魅惑者AI的目标选择机制选择目标
 * 3. 如果魅惑者没有AI，返回魅惑者的当前目标
 *
 * 调用时机：
 * - 需要选择新的攻击目标时
 * - 当前目标无效时
 */
Unit* SimpleCharmedPlayerAI::SelectAttackTarget() const
{
    if (Unit* charmer = me->GetCharmer())
    {
        if (UnitAI* charmerAI = charmer->GetAI())
            return charmerAI->SelectTarget(SelectTargetMethod::Random, 0, ValidTargetSelectPredicate(this));
        return charmer->GetVictim();
    }
    return nullptr;
}

/**
 * @brief 根据专精选择合适的法术
 * @return TargetedSpell 选中的法术-目标对
 *
 * 根据玩家的职业和专精，选择最合适的法术施放。
 * 这是非常庞大的方法，实现了所有职业的智能施法逻辑。
 *
 * 实现逻辑：
 * 1. 创建法术候选列表
 * 2. 根据职业添加通用法术（打断、位移、生存技能等）
 * 3. 根据专精添加专精特定法术
 * 4. 为每个法术分配权重（权重越高，选中概率越大）
 * 5. 从候选列表中随机选择一个法术
 *
 * 权重设计原则：
 * - 高优先级：打断法术（15-25）、斩杀类法术（15-100）
 * - 中优先级：主要伤害/治疗法术（3-8）
 * - 低优先级：辅助法术（1-3）
 *
 * 特殊逻辑：
 * - 战士：不在近战范围时优先冲锋/拦截
 * - 盗贼：根据连击点数选择积攒/终结技能
 * - 死亡骑士：先施放疾病，再施放打击技能
 * - 德鲁伊：自动切换形态
 *
 * 调用时机：
 * - 施法检查定时器到期时
 * - 需要决定下一个施放的法术时
 *
 * 性能考虑：
 * - 会验证每个候选法术，开销较大
 * - 通过定时器控制调用频率
 */
PlayerAI::TargetedSpell SimpleCharmedPlayerAI::SelectAppropriateCastForSpec()
{
    PossibleSpellVector spells;

    // 根据职业选择法术
    switch (me->GetClass())
    {
        case CLASS_WARRIOR: // 战士
            // 不在近战范围时优先使用冲锋/拦截
            if (!me->IsWithinMeleeRange(me->GetVictim()))
            {
                VerifyAndPushSpellCast(spells, SPELL_CHARGE, TARGET_VICTIM, 15);
                VerifyAndPushSpellCast(spells, SPELL_INTERCEPT, TARGET_VICTIM, 10);
            }
            // 通用法术
            VerifyAndPushSpellCast(spells, SPELL_ENRAGED_REGEN, TARGET_NONE, 3);
            VerifyAndPushSpellCast(spells, SPELL_INTIMIDATING_SHOUT, TARGET_VICTIM, 4);
            // 目标正在施法时优先打断
            if (me->GetVictim() && me->GetVictim()->HasUnitState(UNIT_STATE_CASTING))
            {
                VerifyAndPushSpellCast(spells, SPELL_PUMMEL, TARGET_VICTIM, 15);
                VerifyAndPushSpellCast(spells, SPELL_SHIELD_BASH, TARGET_VICTIM, 15);
            }
            VerifyAndPushSpellCast(spells, SPELL_BLOODRAGE, TARGET_NONE, 5);

            // 根据专精选择法术
            switch (GetSpec())
            {
                case SPEC_WARRIOR_PROTECTION:
                    VerifyAndPushSpellCast(spells, SPELL_SHOCKWAVE, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_CONCUSSION_BLOW, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_DISARM, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_LAST_STAND, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_SHIELD_BLOCK, TARGET_NONE, 1);
                    VerifyAndPushSpellCast(spells, SPELL_SHIELD_SLAM, TARGET_VICTIM, 4);
                    VerifyAndPushSpellCast(spells, SPELL_SHIELD_WALL, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_SPELL_REFLECTION, TARGET_NONE, 3);
                    VerifyAndPushSpellCast(spells, SPELL_DEVASTATE, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_REND, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_THUNDER_CLAP, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_DEMO_SHOUT, TARGET_VICTIM, 1);
                    break;
                case SPEC_WARRIOR_ARMS:
                    VerifyAndPushSpellCast(spells, SPELL_SWEEPING_STRIKES, TARGET_NONE, 2);
                    VerifyAndPushSpellCast(spells, SPELL_MORTAL_STRIKE, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_BLADESTORM, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_REND, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_RETALIATION, TARGET_NONE, 3);
                    VerifyAndPushSpellCast(spells, SPELL_SHATTERING_THROW, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_SWEEPING_STRIKES, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_THUNDER_CLAP, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_EXECUTE, TARGET_VICTIM, 15);
                    break;
                case SPEC_WARRIOR_FURY:
                    VerifyAndPushSpellCast(spells, SPELL_DEATH_WISH, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_BLOODTHIRST, TARGET_VICTIM, 4);
                    VerifyAndPushSpellCast(spells, SPELL_DEMO_SHOUT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_EXECUTE, TARGET_VICTIM, 15);
                    VerifyAndPushSpellCast(spells, SPELL_HEROIC_FURY, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_RECKLESSNESS, TARGET_NONE, 8);
                    VerifyAndPushSpellCast(spells, SPELL_PIERCING_HOWL, TARGET_VICTIM, 2);
                    break;
            }
            break;
        case CLASS_PALADIN:
            VerifyAndPushSpellCast(spells, SPELL_AURA_MASTERY, TARGET_NONE, 3);
            VerifyAndPushSpellCast(spells, SPELL_LAY_ON_HANDS, TARGET_CHARMER, 8);
            VerifyAndPushSpellCast(spells, SPELL_BLESSING_OF_MIGHT, TARGET_CHARMER, 8);
            VerifyAndPushSpellCast(spells, SPELL_AVENGING_WRATH, TARGET_NONE, 5);
            VerifyAndPushSpellCast(spells, SPELL_DIVINE_PROTECTION, TARGET_NONE, 4);
            VerifyAndPushSpellCast(spells, SPELL_DIVINE_SHIELD, TARGET_NONE, 2);
            VerifyAndPushSpellCast(spells, SPELL_HAMMER_OF_JUSTICE, TARGET_VICTIM, 6);
            VerifyAndPushSpellCast(spells, SPELL_HAND_OF_FREEDOM, TARGET_SELF, 3);
            VerifyAndPushSpellCast(spells, SPELL_HAND_OF_PROTECTION, TARGET_SELF, 1);
            if (Creature* creatureCharmer = GetCharmer())
            {
                if (creatureCharmer->IsDungeonBoss() || creatureCharmer->isWorldBoss())
                    VerifyAndPushSpellCast(spells, SPELL_HAND_OF_SACRIFICE, creatureCharmer, 10);
                else
                    VerifyAndPushSpellCast(spells, SPELL_HAND_OF_PROTECTION, creatureCharmer, 3);
            }

            switch (GetSpec())
            {
                case SPEC_PALADIN_PROTECTION:
                    VerifyAndPushSpellCast(spells, SPELL_HAMMER_OF_RIGHTEOUS, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_DIVINE_SACRIFICE, TARGET_NONE, 2);
                    VerifyAndPushSpellCast(spells, SPELL_SHIELD_OF_RIGHTEOUS, TARGET_VICTIM, 4);
                    VerifyAndPushSpellCast(spells, SPELL_JUDGEMENT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_CONSECRATION, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_HOLY_SHIELD, TARGET_NONE, 1);
                    break;
                case SPEC_PALADIN_HOLY:
                    VerifyAndPushSpellCast(spells, SPELL_HOLY_SHOCK, TARGET_CHARMER, 3);
                    VerifyAndPushSpellCast(spells, SPELL_HOLY_SHOCK, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_FLASH_OF_LIGHT, TARGET_CHARMER, 4);
                    VerifyAndPushSpellCast(spells, SPELL_HOLY_LIGHT, TARGET_CHARMER, 3);
                    VerifyAndPushSpellCast(spells, SPELL_DIVINE_FAVOR, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_DIVINE_ILLUMINATION, TARGET_NONE, 3);
                    break;
                case SPEC_PALADIN_RETRIBUTION:
                    VerifyAndPushSpellCast(spells, SPELL_CRUSADER_STRIKE, TARGET_VICTIM, 4);
                    VerifyAndPushSpellCast(spells, SPELL_DIVINE_STORM, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_JUDGEMENT, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_HAMMER_OF_WRATH, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_RIGHTEOUS_FURY, TARGET_NONE, 2);
                    break;
            }
            break;
        case CLASS_HUNTER:
            VerifyAndPushSpellCast(spells, SPELL_DETERRENCE, TARGET_NONE, 3);
            VerifyAndPushSpellCast(spells, SPELL_EXPLOSIVE_TRAP, TARGET_NONE, 1);
            VerifyAndPushSpellCast(spells, SPELL_FREEZING_ARROW, TARGET_VICTIM, 2);
            VerifyAndPushSpellCast(spells, SPELL_RAPID_FIRE, TARGET_NONE, 10);
            VerifyAndPushSpellCast(spells, SPELL_KILL_SHOT, TARGET_VICTIM, 10);
            if (me->GetVictim() && me->GetVictim()->GetPowerType() == POWER_MANA && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_VIPER_STING, me->GetGUID()))
                VerifyAndPushSpellCast(spells, SPELL_VIPER_STING, TARGET_VICTIM, 5);

            switch (GetSpec())
            {
                case SPEC_HUNTER_BEAST_MASTERY:
                    VerifyAndPushSpellCast(spells, SPELL_AIMED_SHOT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_ARCANE_SHOT, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_STEADY_SHOT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_MULTI_SHOT, TARGET_VICTIM, 2);
                    break;
                case SPEC_HUNTER_MARKSMANSHIP:
                    VerifyAndPushSpellCast(spells, SPELL_AIMED_SHOT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_CHIMERA_SHOT, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_ARCANE_SHOT, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_STEADY_SHOT, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_READINESS, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_SILENCING_SHOT, TARGET_VICTIM, 5);
                    break;
                case SPEC_HUNTER_SURVIVAL:
                    VerifyAndPushSpellCast(spells, SPELL_EXPLOSIVE_SHOT, TARGET_VICTIM, 8);
                    VerifyAndPushSpellCast(spells, SPELL_BLACK_ARROW, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_MULTI_SHOT, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_STEADY_SHOT, TARGET_VICTIM, 1);
                    break;
            }
            break;
        case CLASS_ROGUE:
        {
            VerifyAndPushSpellCast(spells, SPELL_DISMANTLE, TARGET_VICTIM, 8);
            VerifyAndPushSpellCast(spells, SPELL_EVASION, TARGET_NONE, 8);
            VerifyAndPushSpellCast(spells, SPELL_VANISH, TARGET_NONE, 4);
            VerifyAndPushSpellCast(spells, SPELL_BLIND, TARGET_VICTIM, 2);
            VerifyAndPushSpellCast(spells, SPELL_CLOAK_OF_SHADOWS, TARGET_NONE, 2);

            uint32 builder = 0, finisher = 0;
            switch (GetSpec())
            {
                case SPEC_ROGUE_ASSASSINATION:
                    builder = SPELL_MUTILATE, finisher = SPELL_ENVENOM;
                    VerifyAndPushSpellCast(spells, SPELL_COLD_BLOOD, TARGET_NONE, 20);
                    break;
                case SPEC_ROGUE_COMBAT:
                    builder = SPELL_SINISTER_STRIKE, finisher = SPELL_EVISCERATE;
                    VerifyAndPushSpellCast(spells, SPELL_ADRENALINE_RUSH, TARGET_NONE, 6);
                    VerifyAndPushSpellCast(spells, SPELL_BLADE_FLURRY, TARGET_NONE, 5);
                    VerifyAndPushSpellCast(spells, SPELL_KILLING_SPREE, TARGET_NONE, 25);
                    break;
                case SPEC_ROGUE_SUBLETY:
                    builder = SPELL_HEMORRHAGE, finisher = SPELL_EVISCERATE;
                    VerifyAndPushSpellCast(spells, SPELL_PREPARATION, TARGET_NONE, 10);
                    if (!me->IsWithinMeleeRange(me->GetVictim()))
                        VerifyAndPushSpellCast(spells, SPELL_SHADOWSTEP, TARGET_VICTIM, 25);
                    VerifyAndPushSpellCast(spells, SPELL_SHADOW_DANCE, TARGET_NONE, 10);
                    break;
            }

            if (Unit* victim = me->GetVictim())
            {
                if (victim->HasUnitState(UNIT_STATE_CASTING))
                    VerifyAndPushSpellCast(spells, SPELL_KICK, TARGET_VICTIM, 25);

                uint8 const cp = me->GetComboPoints(victim);
                if (cp >= 4)
                    VerifyAndPushSpellCast(spells, finisher, TARGET_VICTIM, 10);
                if (cp <= 4)
                    VerifyAndPushSpellCast(spells, builder, TARGET_VICTIM, 5);
            }
            break;
        }
        case CLASS_PRIEST:
            VerifyAndPushSpellCast(spells, SPELL_FEAR_WARD, TARGET_SELF, 2);
            VerifyAndPushSpellCast(spells, SPELL_POWER_WORD_FORT, TARGET_CHARMER, 1);
            VerifyAndPushSpellCast(spells, SPELL_DIVINE_SPIRIT, TARGET_CHARMER, 1);
            VerifyAndPushSpellCast(spells, SPELL_SHADOW_PROTECTION, TARGET_CHARMER, 2);
            VerifyAndPushSpellCast(spells, SPELL_DIVINE_HYMN, TARGET_NONE, 5);
            VerifyAndPushSpellCast(spells, SPELL_HYMN_OF_HOPE, TARGET_NONE, 5);
            VerifyAndPushSpellCast(spells, SPELL_SHADOW_WORD_DEATH, TARGET_VICTIM, 1);
            VerifyAndPushSpellCast(spells, SPELL_PSYCHIC_SCREAM, TARGET_VICTIM, 3);
            switch (GetSpec())
            {
                case SPEC_PRIEST_DISCIPLINE:
                    VerifyAndPushSpellCast(spells, SPELL_POWER_WORD_SHIELD, TARGET_CHARMER, 3);
                    VerifyAndPushSpellCast(spells, SPELL_INNER_FOCUS, TARGET_NONE, 3);
                    VerifyAndPushSpellCast(spells, SPELL_PAIN_SUPPRESSION, TARGET_CHARMER, 15);
                    VerifyAndPushSpellCast(spells, SPELL_POWER_INFUSION, TARGET_CHARMER, 10);
                    VerifyAndPushSpellCast(spells, SPELL_PENANCE, TARGET_CHARMER, 3);
                    VerifyAndPushSpellCast(spells, SPELL_FLASH_HEAL, TARGET_CHARMER, 1);
                    break;
                case SPEC_PRIEST_HOLY:
                    VerifyAndPushSpellCast(spells, SPELL_DESPERATE_PRAYER, TARGET_NONE, 3);
                    VerifyAndPushSpellCast(spells, SPELL_GUARDIAN_SPIRIT, TARGET_CHARMER, 5);
                    VerifyAndPushSpellCast(spells, SPELL_FLASH_HEAL, TARGET_CHARMER, 1);
                    VerifyAndPushSpellCast(spells, SPELL_RENEW, TARGET_CHARMER, 3);
                    break;
                case SPEC_PRIEST_SHADOW:
                    if (!me->HasAura(SPELL_SHADOWFORM))
                    {
                        VerifyAndPushSpellCast(spells, SPELL_SHADOWFORM, TARGET_NONE, 100);
                        break;
                    }
                    if (Unit* victim = me->GetVictim())
                    {
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_VAMPIRIC_TOUCH, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_VAMPIRIC_TOUCH, TARGET_VICTIM, 4);
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_SHADOW_WORD_PAIN, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_SHADOW_WORD_PAIN, TARGET_VICTIM, 3);
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_DEVOURING_PLAGUE, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_DEVOURING_PLAGUE, TARGET_VICTIM, 4);
                    }
                    VerifyAndPushSpellCast(spells, SPELL_MIND_BLAST, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_MIND_FLAY, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_DISPERSION, TARGET_NONE, 10);
                    break;
            }
            break;
        case CLASS_DEATH_KNIGHT:
        {
            if (!me->IsWithinMeleeRange(me->GetVictim()))
                VerifyAndPushSpellCast(spells, SPELL_DEATH_GRIP, TARGET_VICTIM, 25);
            VerifyAndPushSpellCast(spells, SPELL_STRANGULATE, TARGET_VICTIM, 15);
            VerifyAndPushSpellCast(spells, SPELL_EMPOWER_RUNE_WEAP, TARGET_NONE, 5);
            VerifyAndPushSpellCast(spells, SPELL_ICEBORN_FORTITUDE, TARGET_NONE, 15);
            VerifyAndPushSpellCast(spells, SPELL_ANTI_MAGIC_SHELL, TARGET_NONE, 10);

            bool hasFF = false, hasBP = false;
            if (Unit* victim = me->GetVictim())
            {
                if (victim->HasUnitState(UNIT_STATE_CASTING))
                    VerifyAndPushSpellCast(spells, SPELL_MIND_FREEZE, TARGET_VICTIM, 25);

                hasFF = !!victim->GetAuraApplicationOfRankedSpell(AURA_FROST_FEVER, me->GetGUID()), hasBP = !!victim->GetAuraApplicationOfRankedSpell(AURA_BLOOD_PLAGUE, me->GetGUID());
                if (hasFF && hasBP)
                    VerifyAndPushSpellCast(spells, SPELL_PESTILENCE, TARGET_VICTIM, 3);
                if (!hasFF)
                    VerifyAndPushSpellCast(spells, SPELL_ICY_TOUCH, TARGET_VICTIM, 4);
                if (!hasBP)
                    VerifyAndPushSpellCast(spells, SPELL_PLAGUE_STRIKE, TARGET_VICTIM, 4);
            }
            switch (GetSpec())
            {
                case SPEC_DEATH_KNIGHT_BLOOD:
                    VerifyAndPushSpellCast(spells, SPELL_RUNE_TAP, TARGET_NONE, 2);
                    VerifyAndPushSpellCast(spells, SPELL_HYSTERIA, TARGET_SELF, 5);
                    if (Creature* creatureCharmer = GetCharmer())
                        if (!creatureCharmer->IsDungeonBoss() && !creatureCharmer->isWorldBoss())
                            VerifyAndPushSpellCast(spells, SPELL_HYSTERIA, creatureCharmer, 15);
                    VerifyAndPushSpellCast(spells, SPELL_HEART_STRIKE, TARGET_VICTIM, 2);
                    if (hasFF && hasBP)
                        VerifyAndPushSpellCast(spells, SPELL_DEATH_STRIKE, TARGET_VICTIM, 8);
                    VerifyAndPushSpellCast(spells, SPELL_DEATH_COIL_DK, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_MARK_OF_BLOOD, TARGET_VICTIM, 20);
                    VerifyAndPushSpellCast(spells, SPELL_VAMPIRIC_BLOOD, TARGET_NONE, 10);
                    break;
                case SPEC_DEATH_KNIGHT_FROST:
                    if (hasFF && hasBP)
                        VerifyAndPushSpellCast(spells, SPELL_OBLITERATE, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_HOWLING_BLAST, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_UNBREAKABLE_ARMOR, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_DEATHCHILL, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_FROST_STRIKE, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_BLOOD_STRIKE, TARGET_VICTIM, 1);
                    break;
                case SPEC_DEATH_KNIGHT_UNHOLY:
                    if (hasFF && hasBP)
                        VerifyAndPushSpellCast(spells, SPELL_SCOURGE_STRIKE, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_DEATH_AND_DECAY, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_ANTI_MAGIC_ZONE, TARGET_NONE, 8);
                    VerifyAndPushSpellCast(spells, SPELL_SUMMON_GARGOYLE, TARGET_VICTIM, 7);
                    VerifyAndPushSpellCast(spells, SPELL_BLOOD_STRIKE, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_DEATH_COIL_DK, TARGET_VICTIM, 3);
                    break;
            }
            break;
        }
        case CLASS_SHAMAN:
            VerifyAndPushSpellCast(spells, SPELL_HEROISM, TARGET_NONE, 25);
            VerifyAndPushSpellCast(spells, SPELL_BLOODLUST, TARGET_NONE, 25);
            VerifyAndPushSpellCast(spells, SPELL_GROUNDING_TOTEM, TARGET_NONE, 2);
            switch (GetSpec())
            {
                case SPEC_SHAMAN_RESTORATION:
                    if (Unit* charmer = me->GetCharmer())
                        if (!charmer->GetAuraApplicationOfRankedSpell(SPELL_EARTH_SHIELD, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_EARTH_SHIELD, charmer, 2);
                    if (me->HasAura(SPELL_SHA_NATURE_SWIFT))
                        VerifyAndPushSpellCast(spells, SPELL_HEALING_WAVE, TARGET_CHARMER, 20);
                    else
                        VerifyAndPushSpellCast(spells, SPELL_LESSER_HEAL_WAVE, TARGET_CHARMER, 1);
                    VerifyAndPushSpellCast(spells, SPELL_TIDAL_FORCE, TARGET_NONE, 4);
                    VerifyAndPushSpellCast(spells, SPELL_SHA_NATURE_SWIFT, TARGET_NONE, 4);
                    VerifyAndPushSpellCast(spells, SPELL_MANA_TIDE_TOTEM, TARGET_NONE, 3);
                    break;
                case SPEC_SHAMAN_ELEMENTAL:
                    if (Unit* victim = me->GetVictim())
                    {
                        if (victim->GetAuraOfRankedSpell(SPELL_FLAME_SHOCK, GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_LAVA_BURST, TARGET_VICTIM, 5);
                        else
                            VerifyAndPushSpellCast(spells, SPELL_FLAME_SHOCK, TARGET_VICTIM, 3);
                    }
                    VerifyAndPushSpellCast(spells, SPELL_CHAIN_LIGHTNING, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_LIGHTNING_BOLT, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_ELEMENTAL_MASTERY, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_THUNDERSTORM, TARGET_NONE, 3);
                    break;
                case SPEC_SHAMAN_ENHANCEMENT:
                    if (Aura const* maelstrom = me->GetAura(AURA_MAELSTROM_WEAPON))
                        if (maelstrom->GetStackAmount() == 5)
                            VerifyAndPushSpellCast(spells, SPELL_LIGHTNING_BOLT, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_STORMSTRIKE, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_EARTH_SHOCK, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_LAVA_LASH, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_SHAMANISTIC_RAGE, TARGET_NONE, 10);
                    break;
            }
            break;
        case CLASS_MAGE:
            if (me->GetVictim() && me->GetVictim()->HasUnitState(UNIT_STATE_CASTING))
                VerifyAndPushSpellCast(spells, SPELL_COUNTERSPELL, TARGET_VICTIM, 25);
            VerifyAndPushSpellCast(spells, SPELL_DAMPEN_MAGIC, TARGET_CHARMER, 2);
            VerifyAndPushSpellCast(spells, SPELL_EVOCATION, TARGET_NONE, 3);
            VerifyAndPushSpellCast(spells, SPELL_MANA_SHIELD, TARGET_NONE, 1);
            VerifyAndPushSpellCast(spells, SPELL_MIRROR_IMAGE, TARGET_NONE, 3);
            VerifyAndPushSpellCast(spells, SPELL_SPELLSTEAL, TARGET_VICTIM, 2);
            VerifyAndPushSpellCast(spells, SPELL_ICE_BLOCK, TARGET_NONE, 1);
            VerifyAndPushSpellCast(spells, SPELL_ICY_VEINS, TARGET_NONE, 3);
            switch (GetSpec())
            {
                case SPEC_MAGE_ARCANE:
                    if (Aura* abAura = me->GetAura(AURA_ARCANE_BLAST))
                        if (abAura->GetStackAmount() >= 3)
                            VerifyAndPushSpellCast(spells, SPELL_ARCANE_MISSILES, TARGET_VICTIM, 7);
                    VerifyAndPushSpellCast(spells, SPELL_ARCANE_BLAST, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_ARCANE_BARRAGE, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_ARCANE_POWER, TARGET_NONE, 8);
                    VerifyAndPushSpellCast(spells, SPELL_PRESENCE_OF_MIND, TARGET_NONE, 7);
                    break;
                case SPEC_MAGE_FIRE:
                    if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_LIVING_BOMB))
                        VerifyAndPushSpellCast(spells, SPELL_LIVING_BOMB, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_COMBUSTION, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_FIREBALL, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_FIRE_BLAST, TARGET_VICTIM, 1);
                    VerifyAndPushSpellCast(spells, SPELL_DRAGONS_BREATH, TARGET_VICTIM, 2);
                    VerifyAndPushSpellCast(spells, SPELL_BLAST_WAVE, TARGET_VICTIM, 1);
                    break;
                case SPEC_MAGE_FROST:
                    VerifyAndPushSpellCast(spells, SPELL_DEEP_FREEZE, TARGET_VICTIM, 10);
                    VerifyAndPushSpellCast(spells, SPELL_FROST_NOVA, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_FROSTBOLT, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_COLD_SNAP, TARGET_VICTIM, 5);
                    if (me->GetVictim() && me->GetVictim()->HasAuraState(AURA_STATE_FROZEN, nullptr, me))
                        VerifyAndPushSpellCast(spells, SPELL_ICE_LANCE, TARGET_VICTIM, 5);
                    break;
            }
            break;
        case CLASS_WARLOCK:
            VerifyAndPushSpellCast(spells, SPELL_DEATH_COIL_W, TARGET_VICTIM, 2);
            VerifyAndPushSpellCast(spells, SPELL_FEAR, TARGET_VICTIM, 2);
            VerifyAndPushSpellCast(spells, SPELL_SEED_OF_CORRUPTION, TARGET_VICTIM, 4);
            VerifyAndPushSpellCast(spells, SPELL_HOWL_OF_TERROR, TARGET_NONE, 2);
            if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_CORRUPTION, me->GetGUID()))
                VerifyAndPushSpellCast(spells, SPELL_CORRUPTION, TARGET_VICTIM, 10);
            switch (GetSpec())
            {
                case SPEC_WARLOCK_AFFLICTION:
                    if (Unit* victim = me->GetVictim())
                    {
                        VerifyAndPushSpellCast(spells, SPELL_SHADOW_BOLT, TARGET_VICTIM, 7);
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_UNSTABLE_AFFLICTION, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_UNSTABLE_AFFLICTION, TARGET_VICTIM, 8);
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_HAUNT, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_HAUNT, TARGET_VICTIM, 8);
                        if (!victim->GetAuraApplicationOfRankedSpell(SPELL_CURSE_OF_AGONY, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_CURSE_OF_AGONY, TARGET_VICTIM, 4);
                        if (victim->HealthBelowPct(25))
                            VerifyAndPushSpellCast(spells, SPELL_DRAIN_SOUL, TARGET_VICTIM, 100);
                    }
                    break;
                case SPEC_WARLOCK_DEMONOLOGY:
                    VerifyAndPushSpellCast(spells, SPELL_METAMORPHOSIS, TARGET_NONE, 15);
                    VerifyAndPushSpellCast(spells, SPELL_SHADOW_BOLT, TARGET_VICTIM, 7);
                    if (me->HasAura(AURA_DECIMATION))
                        VerifyAndPushSpellCast(spells, SPELL_SOUL_FIRE, TARGET_VICTIM, 100);
                    if (me->HasAura(SPELL_METAMORPHOSIS))
                    {
                        VerifyAndPushSpellCast(spells, SPELL_IMMOLATION_AURA, TARGET_NONE, 30);
                        if (!me->IsWithinMeleeRange(me->GetVictim()))
                            VerifyAndPushSpellCast(spells, SPELL_DEMON_CHARGE, TARGET_VICTIM, 20);
                    }
                    if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_IMMOLATE, me->GetGUID()))
                        VerifyAndPushSpellCast(spells, SPELL_IMMOLATE, TARGET_VICTIM, 5);
                    if (me->HasAura(AURA_MOLTEN_CORE))
                        VerifyAndPushSpellCast(spells, SPELL_INCINERATE, TARGET_VICTIM, 10);
                    break;
                case SPEC_WARLOCK_DESTRUCTION:
                    if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_IMMOLATE, me->GetGUID()))
                        VerifyAndPushSpellCast(spells, SPELL_IMMOLATE, TARGET_VICTIM, 8);
                    if (me->GetVictim() && me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_IMMOLATE, me->GetGUID()))
                        VerifyAndPushSpellCast(spells, SPELL_CONFLAGRATE, TARGET_VICTIM, 8);
                    VerifyAndPushSpellCast(spells, SPELL_SHADOWFURY, TARGET_VICTIM, 5);
                    VerifyAndPushSpellCast(spells, SPELL_CHAOS_BOLT, TARGET_VICTIM, 10);
                    VerifyAndPushSpellCast(spells, SPELL_SHADOWBURN, TARGET_VICTIM, 3);
                    VerifyAndPushSpellCast(spells, SPELL_INCINERATE, TARGET_VICTIM, 7);
                    break;
            }
            break;
        case CLASS_DRUID:
            VerifyAndPushSpellCast(spells, SPELL_INNERVATE, TARGET_CHARMER, 5);
            VerifyAndPushSpellCast(spells, SPELL_BARKSKIN, TARGET_NONE, 5);
            switch (GetSpec())
            {
                case SPEC_DRUID_RESTORATION:
                    if (!me->HasAura(SPELL_TREE_OF_LIFE))
                    {
                        CancelAllShapeshifts();
                        VerifyAndPushSpellCast(spells, SPELL_TREE_OF_LIFE, TARGET_NONE, 100);
                        break;
                    }
                    VerifyAndPushSpellCast(spells, SPELL_TRANQUILITY, TARGET_NONE, 10);
                    VerifyAndPushSpellCast(spells, SPELL_NATURE_SWIFTNESS, TARGET_NONE, 7);
                    if (Creature* creatureCharmer = GetCharmer())
                    {
                        VerifyAndPushSpellCast(spells, SPELL_NOURISH, creatureCharmer, 5);
                        VerifyAndPushSpellCast(spells, SPELL_WILD_GROWTH, creatureCharmer, 5);
                        if (!creatureCharmer->GetAuraApplicationOfRankedSpell(SPELL_REJUVENATION, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_REJUVENATION, creatureCharmer, 8);
                        if (!creatureCharmer->GetAuraApplicationOfRankedSpell(SPELL_REGROWTH, me->GetGUID()))
                            VerifyAndPushSpellCast(spells, SPELL_REGROWTH, creatureCharmer, 8);
                        uint8 lifebloomStacks = 0;
                        if (Aura const* lifebloom = creatureCharmer->GetAura(SPELL_LIFEBLOOM, me->GetGUID()))
                            lifebloomStacks = lifebloom->GetStackAmount();
                        if (lifebloomStacks < 3)
                            VerifyAndPushSpellCast(spells, SPELL_LIFEBLOOM, creatureCharmer, 5);
                        if (creatureCharmer->GetAuraApplicationOfRankedSpell(SPELL_REJUVENATION) ||
                            creatureCharmer->GetAuraApplicationOfRankedSpell(SPELL_REGROWTH))
                            VerifyAndPushSpellCast(spells, SPELL_SWIFTMEND, creatureCharmer, 10);
                        if (me->HasAura(SPELL_NATURE_SWIFTNESS))
                            VerifyAndPushSpellCast(spells, SPELL_HEALING_TOUCH, creatureCharmer, 100);
                    }
                    break;
                case SPEC_DRUID_BALANCE:
                {
                    if (!me->HasAura(SPELL_MOONKIN_FORM))
                    {
                        CancelAllShapeshifts();
                        VerifyAndPushSpellCast(spells, SPELL_MOONKIN_FORM, TARGET_NONE, 100);
                        break;
                    }
                    uint32 const mainAttackSpell = me->HasAura(AURA_ECLIPSE_LUNAR) ? SPELL_STARFIRE : SPELL_WRATH;
                    VerifyAndPushSpellCast(spells, SPELL_STARFALL, TARGET_NONE, 20);
                    VerifyAndPushSpellCast(spells, mainAttackSpell, TARGET_VICTIM, 10);
                    if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_INSECT_SWARM, me->GetGUID()))
                        VerifyAndPushSpellCast(spells, SPELL_INSECT_SWARM, TARGET_VICTIM, 7);
                    if (me->GetVictim() && !me->GetVictim()->GetAuraApplicationOfRankedSpell(SPELL_MOONFIRE, me->GetGUID()))
                        VerifyAndPushSpellCast(spells, SPELL_MOONFIRE, TARGET_VICTIM, 5);
                    if (me->GetVictim() && me->GetVictim()->HasUnitState(UNIT_STATE_CASTING))
                        VerifyAndPushSpellCast(spells, SPELL_TYPHOON, TARGET_NONE, 15);
                    break;
                }
                case SPEC_DRUID_FERAL:
                    if (!me->HasAura(SPELL_CAT_FORM))
                    {
                        CancelAllShapeshifts();
                        VerifyAndPushSpellCast(spells, SPELL_CAT_FORM, TARGET_NONE, 100);
                        break;
                    }
                    VerifyAndPushSpellCast(spells, SPELL_BERSERK, TARGET_NONE, 20);
                    VerifyAndPushSpellCast(spells, SPELL_SURVIVAL_INSTINCTS, TARGET_NONE, 15);
                    VerifyAndPushSpellCast(spells, SPELL_TIGER_FURY, TARGET_NONE, 15);
                    VerifyAndPushSpellCast(spells, SPELL_DASH, TARGET_NONE, 5);
                    if (Unit* victim = me->GetVictim())
                    {
                        uint8 const cp = me->GetComboPoints(victim);
                        if (victim->HasUnitState(UNIT_STATE_CASTING) && cp >= 1)
                            VerifyAndPushSpellCast(spells, SPELL_MAIM, TARGET_VICTIM, 25);
                        if (!me->IsWithinMeleeRange(victim))
                            VerifyAndPushSpellCast(spells, SPELL_FERAL_CHARGE_CAT, TARGET_VICTIM, 25);
                        if (cp >= 4)
                            VerifyAndPushSpellCast(spells, SPELL_RIP, TARGET_VICTIM, 50);
                        if (cp <= 4)
                        {
                            VerifyAndPushSpellCast(spells, SPELL_MANGLE_CAT, TARGET_VICTIM, 10);
                            VerifyAndPushSpellCast(spells, SPELL_CLAW, TARGET_VICTIM, 5);
                            if (!victim->GetAuraApplicationOfRankedSpell(SPELL_RAKE, me->GetGUID()))
                                VerifyAndPushSpellCast(spells, SPELL_RAKE, TARGET_VICTIM, 8);
                            if (!me->HasAura(SPELL_SAVAGE_ROAR))
                                VerifyAndPushSpellCast(spells, SPELL_SAVAGE_ROAR, TARGET_NONE, 15);
                        }
                    }
                    break;
            }
            break;
    }

    return SelectSpellCast(spells);
}

/// 法师职业的追逐距离（28码）
static const float CASTER_CHASE_DISTANCE = 28.0f;

/**
 * @brief 更新AI状态
 * @param diff 距离上次更新的时间差（毫秒）
 *
 * 每个世界更新周期调用，执行AI逻辑的核心方法。
 *
 * 主要逻辑流程：
 * 1. 检查魅惑者是否存在
 * 2. 如果魅惑者处于躲避模式且魅惑光环永久，则自毁
 * 3. 如果魅惑者正在战斗：
 *    a. 选择或验证攻击目标
 *    b. 更新移动和面向
 *    c. 检查并施放法术
 *    d. 执行自动攻击
 * 4. 如果魅惑者不在战斗，跟随魅惑者
 *
 * 特殊处理：
 * - 远程职业保持最大施法距离
 * - 自动面向目标
 * - 施法检查定时器控制（500ms间隔）
 *
 * 调用时机：
 * - 每个世界更新周期（通常50ms）
 */
void SimpleCharmedPlayerAI::UpdateAI(uint32 diff)
{
    // 获取魅惑者
    Creature* charmer = GetCharmer();
    if (!charmer)
        return;

    // 如果魅惑者处于躲避模式且魅惑光环为永久，则自毁
    // 这是防止永久魅惑玩家的情况
    if (charmer->IsInEvadeMode())
    {
        for (AuraEffect* aura : me->GetAuraEffectsByType(SPELL_AURA_MOD_CHARM))
        {
            if (aura->GetCasterGUID() == charmer->GetGUID() && aura->GetBase()->IsPermanent())
            {
                me->KillSelf();
                return;
            }
        }
    }

    // 如果魅惑者正在战斗
    if (charmer->IsEngaged())
    {
        // 获取当前目标
        Unit* target = me->GetVictim();
        if (!target || !CanAIAttack(target))
        {
            // 当前目标无效，选择新目标
            target = SelectAttackTarget();
            if (!target || !CanAIAttack(target))
            {
                // 无法找到有效目标，跟随魅惑者
                if (!_isFollowing)
                {
                    _isFollowing = true;
                    me->AttackStop();
                    me->CastStop();

                    // 清除追逐移动
                    if (me->HasUnitState(UNIT_STATE_CHASE))
                        me->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

                    // 跟随魅惑者
                    me->GetMotionMaster()->MoveFollow(charmer, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                }
                return;
            }
            _isFollowing = false;

            // 根据攻击类型开始攻击
            if (IsRangedAttacker())
            {
                // 远程攻击者：如果不在视线内则追踪，否则保持距离
                _chaseCloser = !me->IsWithinLOSInMap(target);
                if (_chaseCloser)
                    AttackStart(target);
                else
                    AttackStartCaster(target, CASTER_CHASE_DISTANCE);
            }
            else
                // 近战攻击者：直接开始攻击
                AttackStart(target);
            _forceFacing = true;
        }

        // 处理面向：如果停止移动且可以转向，则面向目标
        if (me->IsStopped() && !me->HasUnitState(UNIT_STATE_CANNOT_TURN))
        {
            float targetAngle = me->GetAbsoluteAngle(target);
            // 如果强制面向或角度差超过0.4弧度，则转向目标
            if (_forceFacing || fabs(me->GetOrientation() - targetAngle) > 0.4f)
            {
                me->SetFacingTo(targetAngle);
                _forceFacing = false;
            }
        }

        // 施法检查定时器
        if (_castCheckTimer <= diff)
        {
            // 如果正在施法，将定时器设为0（下次更新时立即检查）
            if (me->HasUnitState(UNIT_STATE_CASTING))
                _castCheckTimer = 0;
            else
            {
                // 远程攻击者：检查视线并调整追逐状态
                if (IsRangedAttacker())
                {
                    bool inLOS = me->IsWithinLOSInMap(target);
                    if (_chaseCloser != !inLOS)
                    {
                        _chaseCloser = !inLOS;
                        if (_chaseCloser)
                            AttackStart(target);
                        else
                            AttackStartCaster(target, CASTER_CHASE_DISTANCE);
                    }
                }

                // 选择并施放合适的法术
                if (TargetedSpell shouldCast = SelectAppropriateCastForSpec())
                    DoCastAtTarget(shouldCast);

                // 重置检查定时器为500ms
                _castCheckTimer = 500;
            }
        }
        else
            _castCheckTimer -= diff;

        // 执行自动攻击
        DoAutoAttackIfReady();
    }
    else if (!_isFollowing)
    {
        // 魅惑者不在战斗中，开始跟随
        _isFollowing = true;
        me->AttackStop();
        me->CastStop();

        // 清除追逐移动
        if (me->HasUnitState(UNIT_STATE_CHASE))
            me->GetMotionMaster()->Remove(CHASE_MOTION_TYPE);

        // 跟随魅惑者
        me->GetMotionMaster()->MoveFollow(charmer, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }
}

/**
 * @brief 魅惑状态变更处理
 * @param isNew 是否为新的魅惑状态
 *
 * 当玩家的魅惑状态改变时调用。
 *
 * 处理逻辑：
 * - 被魅惑时：
 *   1. 停止施法
 *   2. 停止攻击
 *   3. 如果没有其他移动，强制同步位置
 *
 * - 解除魅惑时：
 *   1. 停止施法
 *   2. 停止攻击
 *   3. 清除所有移动
 *
 * 最后调用基类的OnCharmed方法
 */
void SimpleCharmedPlayerAI::OnCharmed(bool isNew)
{
    if (me->IsCharmed())
    {
        // 被魅惑时的处理
        me->CastStop();
        me->AttackStop();

        // 如果没有其他移动，强制同步位置（确保客户端位置一致）
        if (me->GetMotionMaster()->Size() <= 1)
            me->GetMotionMaster()->MovePoint(0, me->GetPosition(), false);
    }
    else
    {
        // 解除魅惑时的处理
        me->CastStop();
        me->AttackStop();

        // 清除所有移动
        me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);
    }

    // 调用基类处理
    PlayerAI::OnCharmed(isNew);
}
