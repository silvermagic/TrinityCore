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
 * @file boss_yogg_saron.cpp
 * @brief 尤格-萨隆Boss战斗脚本模块
 *
 * 本模块实现了奥杜尔副本中尤格-萨隆Boss战斗的完整逻辑，包括：
 * - 尤格-萨隆及其关联实体的AI行为
 * - 战斗阶段转换机制
 * - 触手召唤和管理
 * - 幻象房间机制
 * - 理智值系统
 * - 守护者协助机制
 *
 * 战斗概述：
 * - 第一阶段：玩家与萨拉战斗，击杀守护者
 * - 转换阶段：萨拉死亡后，尤格-萨隆出现
 * - 第二阶段：击杀触手，进入幻象房间攻击大脑
 * - 第三阶段：直接攻击尤格-萨隆本体
 *
 * 难度模式：
 * - 玩家可选择不激活守护者来增加难度
 * - 激活的守护者数量越少，奖励越丰厚
 */

#include "ScriptMgr.h"
#include "CreatureTextMgr.h"
#include "GridNotifiers.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ulduar.h"

/**
 * @enum Yells
 * @brief NPC台词和喊话枚举
 *
 * 定义了战斗过程中各个NPC的台词索引，包括：
 * - 萨拉的各种喊话
 * - 尤格-萨隆的战斗台词
 * - 尤格-萨隆之声的低语
 * - 大脑的喊话
 * - 不祥之云的提示
 * - 守护者的台词
 * - 各种幻象中的角色扮演台词
 */
enum Yells
{
    // Sara - 萨拉相关台词
    SAY_SARA_ULDUAR_SCREAM_1                = 0,  // 在整个副本中随机尖叫，正式服未使用
    SAY_SARA_ULDUAR_SCREAM_2                = 1,  // 在整个副本中随机尖叫，正式服未使用
    SAY_SARA_AGGRO                          = 2,  // 开战喊话
    SAY_SARA_FERVOR_HIT                     = 3,  // 萨拉的狂热命中时喊话
    SAY_SARA_BLESSING_HIT                   = 4,  // 萨拉的祝福命中时喊话
    SAY_SARA_KILL                           = 5,  // 击杀玩家时喊话
    SAY_SARA_TRANSFORM_1                    = 6,  // 转换阶段台词1
    SAY_SARA_TRANSFORM_2                    = 7,  // 转换阶段台词2
    SAY_SARA_TRANSFORM_3                    = 8,  // 转换阶段台词3
    SAY_SARA_TRANSFORM_4                    = 9,  // 转换阶段台词4
    SAY_SARA_DEATH_RAY                      = 10, // 死亡射线喊话
    SAY_SARA_PSYCHOSIS_HIT                  = 11, // 精神病命中时喊话

    // Yogg-Saron - 尤格-萨隆相关台词
    SAY_YOGG_SARON_SPAWN                    = 0,  // 出现时喊话
    SAY_YOGG_SARON_MADNESS                  = 1,  // 诱导疯狂喊话
    EMOTE_YOGG_SARON_MADNESS                = 2,  // 诱导疯狂表情
    SAY_YOGG_SARON_PHASE_3                  = 3,  // 第三阶段喊话
    SAY_YOGG_SARON_DEAFENING_ROAR           = 4,  // 震耳咆哮喊话
    EMOTE_YOGG_SARON_DEAFENING_ROAR         = 5,  // 震耳咆哮表情
    SAY_YOGG_SARON_DEATH                    = 6,  // 死亡喊话
    EMOTE_YOGG_SARON_EMPOWERING_SHADOWS     = 7,  // 赋能之影表情
    EMOTE_YOGG_SARON_EXTINGUISH_ALL_LIFE    = 8,  // 熄灭所有生命表情（狂暴）

    // Voice of Yogg-Saron - 尤格-萨隆之声相关台词
    WHISPER_VOICE_PHASE_1_WIPE              = 0,  // 第一阶段团灭时的低语
    WHISPER_VOICE_INSANE                    = 1,  // 玩家疯狂时的低语

    // Brain of Yogg-Saron - 尤格-萨隆大脑相关台词
    EMOTE_BRAIN_ILLUSION_SHATTERED          = 0,  // 幻象破碎表情

    // Ominous Cloud - 不祥之云相关台词
    EMOTE_OMINOUS_CLOUD_PLAYER_TOUCH        = 0,  // 玩家触碰不祥之云的表情

    // Keepers - 守护者相关台词
    SAY_KEEPER_CHOSEN_1                     = 0,  // 守护者被选中台词1
    SAY_KEEPER_CHOSEN_2                     = 1,  // 守护者被选中台词2

    // Yogg-Saron illusions - 尤格-萨隆幻象相关台词
    SAY_STORMWIND_ROLEPLAY_4                = 0,  // 暴风城幻象台词4
    SAY_STORMWIND_ROLEPLAY_7                = 1,  // 暴风城幻象台词7
    SAY_ICECROWN_ROLEPLAY_5                 = 2,  // 冰冠幻象台词5
    SAY_ICECROWN_ROLEPLAY_6                 = 3,  // 冰冠幻象台词6
    SAY_CHAMBER_ROLEPLAY_5                  = 4,  // 龙眠神殿幻象台词5

    // Neltharion - 耐萨里奥（死亡之翼）相关台词
    SAY_CHAMBER_ROLEPLAY_1                  = 0,  // 龙眠神殿幻象台词1
    SAY_CHAMBER_ROLEPLAY_3                  = 1,  // 龙眠神殿幻象台词3

    // Ysera - 伊瑟拉相关台词
    SAY_CHAMBER_ROLEPLAY_2                  = 0,  // 龙眠神殿幻象台词2

    // Malygos - 玛里苟斯相关台词
    SAY_CHAMBER_ROLEPLAY_4                  = 0,  // 龙眠神殿幻象台词4

    // Immolated Champion - 燃烧的勇士（伯瓦尔）相关台词
    SAY_ICECROWN_ROLEPLAY_1                 = 0,  // 冰冠幻象台词1
    SAY_ICECROWN_ROLEPLAY_3                 = 1,  // 冰冠幻象台词3

    // The Lich King - 巫妖王相关台词
    SAY_ICECROWN_ROLEPLAY_2                 = 0,  // 冰冠幻象台词2
    SAY_ICECROWN_ROLEPLAY_4                 = 1,  // 冰冠幻象台词4

    // Garona - 迦罗娜相关台词
    SAY_STORMWIND_ROLEPLAY_1                = 0,  // 暴风城幻象台词1
    SAY_STORMWIND_ROLEPLAY_2                = 1,  // 暴风城幻象台词2
    SAY_STORMWIND_ROLEPLAY_3                = 2,  // 暴风城幻象台词3
    SAY_STORMWIND_ROLEPLAY_6                = 3,  // 暴风城幻象台词6

    // King Llane - 莱恩国王相关台词
    SAY_STORMWIND_ROLEPLAY_5                = 0,  // 暴风城幻象台词5
};

/**
 * @enum Spells
 * @brief 法术ID枚举
 *
 * 定义了尤格-萨隆战斗中使用的所有法术ID，按实体类型分组：
 * - 尤格-萨隆之声的法术
 * - 萨拉的法术
 * - 不祥之云的法术
 * - 尤格-萨隆守护者的法术
 * - 尤格-萨隆本体的法术
 * - 大脑的法术
 * - 各种触手的法术
 * - 不朽守护者的法术
 * - 守护者协助法术
 * - 幻象相关法术
 */
enum Spells
{
    // Voice of Yogg-Saron - 尤格-萨隆之声的法术
    SPELL_SUMMON_GUARDIAN_2                 = 62978, // 召唤尤格-萨隆守护者（第二阶段）
    SPELL_SANITY_PERIODIC                   = 63786, // 理智值周期性光环
    SPELL_SANITY                            = 63050, // 理智值（玩家核心机制）
    SPELL_INSANE_PERIODIC                   = 64554, // 疯狂周期性检查
    SPELL_INSANE                            = 63120, // 疯狂（理智为0时触发）
    //SPELL_CLEAR_INSANE                      = 63122,  // 清除疯狂（何时施放？）
    SPELL_CONSTRICTOR_TENTACLE              = 64132, // 召唤缠绕触手
    SPELL_CRUSHER_TENTACLE_SUMMON           = 64139, // 召唤粉碎触手
    SPELL_CORRUPTOR_TENTACLE_SUMMON         = 64143, // 召唤腐化触手
    SPELL_IMMORTAL_GUARDIAN                 = 64158, // 召唤不朽守护者

    // Sara - 萨拉的法术
    SPELL_SARAS_FERVOR                      = 63138, // 萨拉的狂热
    SPELL_SARAS_FERVOR_TARGET_SELECTOR      = 63747, // 萨拉的狂热目标选择器
    SPELL_SARAS_BLESSING                    = 63134, // 萨拉的祝福
    SPELL_SARAS_BLESSING_TARGET_SELECTOR    = 63745, // 萨拉的祝福目标选择器
    SPELL_SARAS_ANGER                       = 63147, // 萨拉的愤怒
    SPELL_SARAS_ANGER_TARGET_SELECTOR       = 63744, // 萨拉的愤怒目标选择器
    SPELL_FULL_HEAL                         = 43978, // 完全治疗
    SPELL_PHASE_2_TRANSFORM                 = 65157, // 第二阶段转换视觉效果
    SPELL_SHADOWY_BARRIER_SARA              = 64775, // 萨拉的暗影屏障
    SPELL_RIDE_YOGG_SARON_VEHICLE           = 61791, // 骑乘尤格-萨隆载具
    SPELL_PSYCHOSIS                         = 63795, // 精神病（降低理智）
    SPELL_MALADY_OF_THE_MIND                = 63830, // 心灵疾病
    SPELL_BRAIN_LINK                        = 63802, // 大脑连接
    SPELL_BRAIN_LINK_DAMAGE                 = 63803, // 大脑连接伤害（红色光束）
    SPELL_BRAIN_LINK_NO_DAMAGE              = 63804, // 大脑连接无伤害（黄色光束）
    SPELL_DEATH_RAY                         = 63891, // 死亡射线

    // Ominous Cloud - 不祥之云的法术
    SPELL_OMINOUS_CLOUD_VISUAL              = 63084, // 不祥之云视觉效果
    SPELL_SUMMON_GUARDIAN_1                 = 63031, // 召唤尤格-萨隆守护者（第一阶段）

    // Guardian of Yogg-Saron - 尤格-萨隆守护者的法术
    SPELL_DARK_VOLLEY                       = 63038, // 黑暗齐射
    SPELL_SHADOW_NOVA                       = 62714, // 暗影新星（死亡时爆炸）
    SPELL_SHADOW_NOVA_2                     = 65719, // 暗影新星2

    // Yogg-Saron - 尤格-萨隆本体的法术
    SPELL_EXTINGUISH_ALL_LIFE               = 64166, // 熄灭所有生命（狂暴）
    SPELL_SHADOWY_BARRIER_YOGG              = 63894, // 尤格-萨隆的暗影屏障
    SPELL_KNOCK_AWAY                        = 64022, // 击退
    SPELL_PHASE_3_TRANSFORM                 = 63895, // 第三阶段转换
    SPELL_DEAFENING_ROAR                    = 64189, // 震耳咆哮（硬模式技能）
    SPELL_LUNATIC_GAZE                      = 64163, // 疯狂凝视
    SPELL_LUNATIC_GAZE_DAMAGE               = 64164, // 疯狂凝视伤害
    SPELL_SHADOW_BEACON                     = 64465, // 暗影信标（标记不朽守护者）

    // Brain of Yogg-Saron - 尤格-萨隆大脑的法术
    SPELL_MATCH_HEALTH                      = 64066, // 匹配生命值（同步尤格-萨隆生命）
    SPELL_MATCH_HEALTH_2                    = 64069, // 匹配生命值2
    SPELL_INDUCE_MADNESS                    = 64059, // 诱导疯狂
    SPELL_BRAIN_HURT_VISUAL                 = 64361, // 大脑受伤视觉效果
    SPELL_SHATTERED_ILLUSION                = 64173, // 破碎幻象
    SPELL_SHATTERED_ILLUSION_REMOVE         = 65238, // 移除破碎幻象

    // Tentacles - 触手通用法术
    SPELL_ERUPT                             = 64144, // 喷发（召唤视觉效果）
    SPELL_TENTACLE_VOID_ZONE                = 64017,  // 触手虚空区域（腐化和粉碎触手使用）

    // Crusher Tentacle - 粉碎触手的法术
    SPELL_DIMINISH_POWER                    = 64145, // 削弱力量
    SPELL_DIMINSH_POWER                     = 64148, // 削弱力量（引导）
    SPELL_FOCUSED_ANGER                     = 57688, // 专注之怒
    SPELL_CRUSH                             = 64146, // 粉碎
    //SPELL_CRUSH_2                           = 65201,  // 由SPELL_CRUSH触发

    // Constrictor Tentacle - 缠绕触手的法术
    SPELL_TENTACLE_VOID_ZONE_2              = 64384, // 触手虚空区域2
    SPELL_LUNGE                             = 64131, // 猛扑（抓取玩家）

    // Corruptor Tentacle - 腐化触手的法术
    SPELL_APATHY                            = 64156, // 冷漠
    SPELL_BLACK_PLAGUE                      = 64153, // 黑色瘟疫
    SPELL_CURSE_OF_DOOM                     = 64157, // 厄运诅咒
    SPELL_DRAINING_POISON                   = 64152, // 吸取毒素

    // Immortal Guardian - 不朽守护者的法术
    SPELL_EMPOWERING_SHADOWS                = 64468, // 赋能之影
    SPELL_EMPOWERED                         = 64161, // 强化状态
    SPELL_EMPOWERED_BUFF                    = 65294, // 强化增益
    SPELL_WEAKENED                          = 64162, // 虚弱状态
    SPELL_DRAIN_LIFE                        = 64159, // 吸取生命
    SPELL_RECENTLY_SPAWNED                  = 64497, // 最近生成
    SPELL_SIMPLE_TELEPORT                   = 64195, // 简单传送

    // Keepers at Observation Ring - 观察环上的守护者法术
    SPELL_TELEPORT                          = 62940, // 传送

    // Keepers - 守护者通用法术
    SPELL_SIMPLE_TELEPORT_KEEPERS           = 12980, // 守护者简单传送
    SPELL_KEEPER_ACTIVE                     = 62647, // 守护者激活

    // Mimiron - 米米尔隆的协助法术
    SPELL_SPEED_OF_INVENTION                = 62671, // 发明速度（急速增益）
    SPELL_DESTABILIZATION_MATRIX            = 65206, // 失稳矩阵（降低触手伤害）

    // Freya - 弗雷亚的协助法术
    SPELL_RESILIENCE_OF_NATURE              = 62670, // 自然韧性（生命值增益）
    SPELL_SANITY_WELL_SUMMON                = 64170, // 召唤理智之井

    // Sanity Well - 理智之井的法术
    SPELL_SANITY_WELL_VISUAL                = 63288, // 理智之井视觉效果
    SPELL_SANITY_WELL                       = 64169, // 理智之井（恢复理智）

    // Thorim - 托里姆的协助法术
    SPELL_FURY_OF_THE_STORM                 = 62702, // 风暴之怒（伤害增益）
    SPELL_TITANIC_STORM                     = 64171, // 泰坦风暴（秒杀不朽守护者）

    // Hodir - 霍迪尔的协助法术
    SPELL_FORTITUDE_OF_FROST                = 62650, // 霜之坚韧（伤害减免）
    SPELL_HODIRS_PROTECTIVE_GAZE            = 64174, // 霍迪尔的保护凝视（免死）
    SPELL_FLASH_FREEZE_VISUAL               = 64176, // 急速冷冻视觉效果

    // Death Orb - 死亡之球的法术
    SPELL_DEATH_RAY_ORIGIN_VISUAL           = 63893, // 死亡射线源头视觉效果

    // Death Ray - 死亡射线的法术
    SPELL_DEATH_RAY_WARNING_VISUAL          = 63882, // 死亡射线警告视觉效果
    SPELL_DEATH_RAY_PERIODIC                = 63883, // 死亡射线周期性伤害
    SPELL_DEATH_RAY_DAMAGE_VISUAL           = 63886, // 死亡射线伤害视觉效果

    // Laughing Skull - 大笑骷髅的法术
    SPELL_LUNATIC_GAZE_SKULL                = 64167, // 骷髅的疯狂凝视

    // Descend Into Madness - 陷入疯狂（传送门）的法术
    SPELL_TELEPORT_PORTAL_VISUAL            = 64416, // 传送门视觉效果
    SPELL_TELEPORT_TO_STORMWIND_ILLUSION    = 63989, // 传送到暴风城幻象
    SPELL_TELEPORT_TO_CHAMBER_ILLUSION      = 63997, // 传送到龙眠神殿幻象
    SPELL_TELEPORT_TO_ICECROWN_ILLUSION     = 63998, // 传送到冰冠幻象

    // Illusions - 幻象相关法术
    SPELL_GRIM_REPRISAL                     = 63305, // 无情报复
    SPELL_GRIM_REPRISAL_DAMAGE              = 64039, // 无情报复伤害

    // Suit of Armor - 盔甲假人的法术
    SPELL_NONDESCRIPT_1                     = 64013, // 无描述效果1

    // Dragon Consorts & Deathsworn Zealot - 龙族配偶和死亡誓执行者的法术
    SPELL_NONDESCRIPT_2                     = 64010, // 无描述效果2

    // Garona - 迦罗娜的法术
    SPELL_ASSASSINATE                       = 64063, // 刺杀

    // King Llane - 莱恩国王的法术
    SPELL_PERMANENT_FEIGN_DEATH             = 29266, // 永久假死

    // The Lich King - 巫妖王的法术
    SPELL_DEATHGRASP                        = 63037, // 死亡之握

    // Turned Champion - 转化的勇士的法术
    SPELL_VERTEX_COLOR_BLACK                = 39662, // 黑色顶点颜色

    // Player self cast spells - 玩家自身施放的法术
    SPELL_MALADY_OF_THE_MIND_JUMP           = 63881, // 心灵疾病跳跃
    SPELL_ILLUSION_ROOM                     = 63988, // 幻象房间标记
    SPELL_HATE_TO_ZERO                      = 63984, // 仇恨清零
    SPELL_TELEPORT_BACK_TO_MAIN_ROOM        = 63992, // 传送回主房间
    SPELL_INSANE_VISUAL                     = 64464, // 疯狂视觉效果
    SPELL_CONSTRICTOR_TENTACLE_SUMMON       = 64133, // 缠绕触手召唤
    SPELL_SQUEEZE                           = 64125, // 挤压
    SPELL_FLASH_FREEZE                      = 64175, // 急速冷冻
    SPELL_LOW_SANITY_SCREEN_EFFECT          = 63752, // 低理智屏幕效果

    SPELL_IN_THE_MAWS_OF_THE_OLD_GOD        = 64184, // 在古神之口中（瓦兰奈尔任务）
};

/**
 * @enum Phases
 * @brief 战斗阶段枚举
 *
 * 尤格-萨隆战斗分为四个阶段：
 * - 第一阶段：与萨拉战斗，击杀守护者
 * - 转换阶段：萨拉死亡，尤格-萨隆出现
 * - 第二阶段：击杀触手，进入幻象房间攻击大脑
 * - 第三阶段：直接攻击尤格-萨隆本体
 */
enum Phases
{
    PHASE_ONE               = 1,  // 第一阶段：萨拉阶段
    PHASE_TRANSFORM         = 2,  // 转换阶段：萨拉死亡到尤格-萨隆出现
    PHASE_TWO               = 3,  // 第二阶段：触手和幻象阶段
    PHASE_THREE             = 4,  // 第三阶段：直接战斗阶段
};

/**
 * @enum Events
 * @brief 事件ID枚举
 *
 * 定义了战斗中所有定时事件的ID，用于事件调度系统。
 * 事件按照所属实体进行分组：
 * - 尤格-萨隆之声事件
 * - 萨拉事件
 * - 触手事件
 * - 尤格-萨隆事件
 * - 守护者事件
 * - 幻象角色扮演事件
 */
enum Events
{
    // Voice of Yogg-Saron - 尤格-萨隆之声的事件
    EVENT_LOCK_DOOR                         = 1,  // 锁门事件
    EVENT_SUMMON_GUARDIAN_OF_YOGG_SARON     = 2,  // 召唤尤格-萨隆守护者
    EVENT_SUMMON_CORRUPTOR_TENTACLE         = 3,  // 召唤腐化触手
    EVENT_SUMMON_CONSTRICTOR_TENTACLE       = 4,  // 召唤缠绕触手
    EVENT_SUMMON_CRUSHER_TENTACLE           = 5,  // 召唤粉碎触手
    EVENT_ILLUSION                          = 6,  // 幻象事件
    EVENT_SUMMON_IMMORTAL_GUARDIAN          = 7,  // 召唤不朽守护者
    EVENT_EXTINGUISH_ALL_LIFE               = 8,  // 熄灭所有生命（狂暴），由之声处理，战斗开始时计时（此时尤格-萨隆还未生成）

    // Sara - 萨拉的事件
    EVENT_SARAS_FERVOR                      = 9,  // 萨拉的狂热
    EVENT_SARAS_BLESSING                    = 10, // 萨拉的祝福
    EVENT_SARAS_ANGER                       = 11, // 萨拉的愤怒
    EVENT_TRANSFORM_1                       = 12, // 转换事件1
    EVENT_TRANSFORM_2                       = 13, // 转换事件2
    EVENT_TRANSFORM_3                       = 14, // 转换事件3
    EVENT_TRANSFORM_4                       = 15, // 转换事件4
    EVENT_PSYCHOSIS                         = 16, // 精神病
    EVENT_MALADY_OF_THE_MIND                = 17, // 心灵疾病
    EVENT_BRAIN_LINK                        = 18, // 大脑连接
    EVENT_DEATH_RAY                         = 19, // 死亡射线

    // Tentacles - 触手的事件
    EVENT_DIMINISH_POWER                    = 20, // 削弱力量
    EVENT_CAST_RANDOM_SPELL                 = 21, // 施放随机法术

    // Yogg-Saron - 尤格-萨隆的事件
    EVENT_YELL_BOW_DOWN                     = 22, // 喊话：跪下
    EVENT_SHADOW_BEACON                     = 23, // 暗影信标
    EVENT_LUNATIC_GAZE                      = 24, // 疯狂凝视
    EVENT_DEAFENING_ROAR                    = 25, // 震耳咆哮（仅在25人模式下，激活0-3个守护者时出现，硬模式技能）

    // Guardian of Yogg-Saron - 尤格-萨隆守护者的事件
    EVENT_DARK_VOLLEY                       = 26, // 黑暗齐射

    // Immortal Guardian - 不朽守护者的事件
    EVENT_DRAIN_LIFE                        = 27, // 吸取生命

    // Keepers - 守护者的事件
    EVENT_DESTABILIZATION_MATRIX            = 28, // 失稳矩阵
    EVENT_HODIRS_PROTECTIVE_GAZE            = 29, // 霍迪尔的保护凝视

    // Chamber Illusion - 龙眠神殿幻象的角色扮演事件
    EVENT_CHAMBER_ROLEPLAY_1                = 30, // 龙眠神殿角色扮演1
    EVENT_CHAMBER_ROLEPLAY_2                = 31, // 龙眠神殿角色扮演2
    EVENT_CHAMBER_ROLEPLAY_3                = 32, // 龙眠神殿角色扮演3
    EVENT_CHAMBER_ROLEPLAY_4                = 33, // 龙眠神殿角色扮演4
    EVENT_CHAMBER_ROLEPLAY_5                = 34, // 龙眠神殿角色扮演5

    // Icecrown Illusion - 冰冠幻象的角色扮演事件
    EVENT_ICECROWN_ROLEPLAY_1               = 35, // 冰冠角色扮演1
    EVENT_ICECROWN_ROLEPLAY_2               = 36, // 冰冠角色扮演2
    EVENT_ICECROWN_ROLEPLAY_3               = 37, // 冰冠角色扮演3
    EVENT_ICECROWN_ROLEPLAY_4               = 38, // 冰冠角色扮演4
    EVENT_ICECROWN_ROLEPLAY_5               = 39, // 冰冠角色扮演5
    EVENT_ICECROWN_ROLEPLAY_6               = 40, // 冰冠角色扮演6

    // Stormwind Illusion - 暴风城幻象的角色扮演事件
    EVENT_STORMWIND_ROLEPLAY_1              = 41, // 暴风城角色扮演1
    EVENT_STORMWIND_ROLEPLAY_2              = 42, // 暴风城角色扮演2
    EVENT_STORMWIND_ROLEPLAY_3              = 43, // 暴风城角色扮演3
    EVENT_STORMWIND_ROLEPLAY_4              = 44, // 暴风城角色扮演4
    EVENT_STORMWIND_ROLEPLAY_5              = 45, // 暴风城角色扮演5
    EVENT_STORMWIND_ROLEPLAY_6              = 46, // 暴风城角色扮演6
    EVENT_STORMWIND_ROLEPLAY_7              = 47, // 暴风城角色扮演7
};

/**
 * @enum EventGroups
 * @brief 事件组枚举
 *
 * 用于将相关事件分组，便于批量延迟或取消。
 */
enum EventGroups
{
    EVENT_GROUP_SUMMON_TENTACLES            = 1,  // 召唤触手事件组
};

/**
 * @enum Actions
 * @brief 动作ID枚举
 *
 * 定义了用于AI之间通信的动作ID。
 * 用于触发特定的状态转换或行为。
 */
enum Actions
{
    ACTION_PHASE_TRANSFORM              = 0,  // 触发转换阶段
    ACTION_PHASE_TWO                    = 1,  // 触发第二阶段
    ACTION_PHASE_THREE                  = 2,  // 触发第三阶段
    ACTION_INDUCE_MADNESS               = 3,  // 诱导疯狂
    ACTION_SANITY_WELLS                 = 4,  // 激活理智之井
    ACTION_FLASH_FREEZE                 = 5,  // 急速冷冻
    ACTION_TENTACLE_KILLED              = 6,  // 触手被击杀
    ACTION_START_ROLEPLAY               = 8,  // 开始角色扮演
    ACTION_TOGGLE_SHATTERED_ILLUSION    = 9,  // 切换破碎幻象状态
};

/**
 * @enum CreatureGroups
 * @brief 生物组枚举
 *
 * 定义了生物召唤组的ID，用于批量召唤。
 */
enum CreatureGroups
{
    CREATURE_GROUP_CLOUDS       = 0,  // 不祥之云组
    CREATURE_GROUP_PORTALS_10   = 1,  // 10人传送门组
    CREATURE_GROUP_PORTALS_25   = 2,  // 25人传送门组
};

/**
 * @brief 尤格-萨隆生成位置
 */
Position const YoggSaronSpawnPos            = {1980.43f, -25.7708f, 324.9724f, 3.141593f};

/**
 * @brief 观察环上守护者的位置
 *
 * 四位守护者在观察环上的初始位置：
 * - [0] 弗雷亚
 * - [1] 霍迪尔
 * - [2] 托里姆
 * - [3] 米米尔隆
 */
Position const ObservationRingKeepersPos[4] =
{
    {1945.682f,  33.34201f, 411.4408f, 5.270895f},  // Freya - 弗雷亚
    {1945.761f, -81.52171f, 411.4407f, 1.029744f},  // Hodir - 霍迪尔
    {2028.822f, -65.73573f, 411.4426f, 2.460914f},  // Thorim - 托里姆
    {2028.766f,  17.42014f, 411.4446f, 3.857178f},  // Mimiron - 米米尔隆
};

/**
 * @brief 战斗区域内守护者的位置
 *
 * 四位守护者被激活后，传送到战斗区域协助玩家的位置：
 * - [0] 弗雷亚
 * - [1] 霍迪尔
 * - [2] 托里姆
 * - [3] 米米尔隆
 */
Position const YSKeepersPos[4] =
{
    {2036.873f,  25.42513f, 338.4984f, 3.909538f},  // Freya - 弗雷亚
    {1939.045f, -90.87457f, 338.5426f, 0.994837f},  // Hodir - 霍迪尔
    {1939.148f,  42.49035f, 338.5427f, 5.235988f},  // Thorim - 托里姆
    {2036.658f, -73.58822f, 338.4985f, 2.460914f},  // Mimiron - 米米尔隆
};

/**
 * @brief 幻象房间中的其他位置
 *
 * 用于幻象角色扮演的移动目标：
 * - [0] 迦罗娜的终点位置
 * - [1] 萨鲁法尔的终点位置
 */
Position const IllusionsMiscPos[2] =
{
    {1928.793f,  65.03109f, 242.3763f, 0.0f}, // Garona end position - 迦罗娜终点位置
    {1912.324f, -155.7967f, 239.9896f, 0.0f}, // Saurfang end position - 萨鲁法尔终点位置
};

/**
 * @enum MiscData
 * @brief 杂项数据枚举
 *
 * 定义了成就、音效和幻象房间数量等杂项数据。
 */
enum MiscData
{
    ACHIEV_TIMED_START_EVENT                = 21001, // 计时成就开始事件ID
    SOUND_LUNATIC_GAZE                      = 15757, // 疯狂凝视音效ID
    MAX_ILLUSION_ROOMS                      = 3      // 最大幻象房间数量
};

/**
 * @brief 幻象传送法术数组
 *
 * 存储三种幻象房间的传送法术ID：
 * - [0] 龙眠神殿幻象
 * - [1] 冰冠幻象
 * - [2] 暴风城幻象
 */
uint32 const IllusionSpells[MAX_ILLUSION_ROOMS]
{
    SPELL_TELEPORT_TO_CHAMBER_ILLUSION,
    SPELL_TELEPORT_TO_ICECROWN_ILLUSION,
    SPELL_TELEPORT_TO_STORMWIND_ILLUSION
};

/**
 * @class StartAttackEvent
 * @brief 开始攻击事件
 *
 * 延迟启动召唤生物的攻击行为。
 * 用于在召唤生物生成后延迟一段时间才开始攻击，
 * 避免召唤瞬间立即攻击玩家。
 *
 * 使用场景：
 * - 触手召唤后的延迟攻击
 * - 守护者召唤后的延迟攻击
 */
class StartAttackEvent : public BasicEvent
{
    public:
        /**
         * @brief 构造函数
         * @param summoner 召唤者生物
         * @param owner 被召唤的生物（需要开始攻击的目标）
         */
        StartAttackEvent(Creature* summoner, Creature* owner)
            : _summonerGuid(summoner->GetGUID()), _owner(owner)
        {
        }

        /**
         * @brief 执行事件
         * @param time 执行时间
         * @param diff 时间差
         * @return 返回true表示事件完成
         *
         * 设置生物为主动攻击状态，并选择召唤者的随机仇恨目标进行攻击。
         */
        bool Execute(uint64 /*time*/, uint32 /*diff*/) override
        {
            // 设置为主动攻击状态
            _owner->SetReactState(REACT_AGGRESSIVE);
            // 获取召唤者
            if (Creature* _summoner = ObjectAccessor::GetCreature(*_owner, _summonerGuid))
                // 选择召唤者的随机目标进行攻击
                if (Unit* target = _summoner->AI()->SelectTarget(SelectTargetMethod::Random, 0, 300.0f))
                    _owner->AI()->AttackStart(target);
            return true;
        }

    private:
        ObjectGuid _summonerGuid;  ///< 召唤者的GUID
        Creature* _owner;          ///< 需要开始攻击的生物
};

/**
 * @class boss_voice_of_yogg_saron
 * @brief 尤格-萨隆之声脚本类
 *
 * 尤格-萨隆之声是战斗的核心控制器，负责：
 * - 协调整个战斗流程
 * - 管理理智值系统
 * - 召唤各种触手和守护者
 * - 控制幻象房间机制
 * - 处理战斗阶段转换
 *
 * 尤格-萨隆之声是隐形的，玩家不直接与它战斗。
 * 它作为战斗的幕后控制者，协调萨拉、尤格-萨隆和大脑的行为。
 */
class boss_voice_of_yogg_saron : public CreatureScript
{
    public:
        boss_voice_of_yogg_saron() : CreatureScript("boss_voice_of_yogg_saron") { }

        /**
         * @struct boss_voice_of_yogg_saronAI
         * @brief 尤格-萨隆之声的AI实现
         *
         * 继承自BossAI，实现了战斗的核心逻辑。
         * 负责管理战斗的所有阶段和机制。
         */
        struct boss_voice_of_yogg_saronAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_voice_of_yogg_saronAI(Creature* creature) : BossAI(creature, DATA_YOGG_SARON)
            {
                Initialize();
                SetCombatMovement(false); // 不移动，固定位置
            }

            /**
             * @brief 初始化成员变量
             *
             * 重置所有计数器和计时器到初始状态。
             */
            void Initialize()
            {
                _guardiansCount = 0;        // 守护者计数
                _guardianTimer = 20s;       // 守护者召唤间隔
                _illusionShattered = false; // 幻象是否破碎
            }

            /**
             * @brief 视线检测
             * @param who 检测到的单位
             *
             * 当玩家进入范围内时触发战斗。
             * 注意：MoveInLineOfSight对于如此大的距离不起作用。
             *
             * @调用时机 每帧检测
             */
            void MoveInLineOfSight(Unit* who) override
            {
                // TODO: MoveInLineOfSight对于如此大的距离不起作用
                if (who->GetTypeId() == TYPEID_PLAYER && !who->ToPlayer()->IsGameMaster() && me->GetDistance2d(who) < 99.0f && !me->IsInCombat())
                    DoZoneInCombat();
            }

            /**
             * @brief 进入脱战模式
             * @param why 脱战原因
             *
             * 当战斗重置时调用，清理所有相关实体的战斗状态。
             * 移除玩家的理智和疯狂光环。
             *
             * @调用时机 战斗重置或团灭时
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                BossAI::EnterEvadeMode(why);

                // 让所有相关实体也脱战
                for (uint8 i = DATA_SARA; i <= DATA_MIMIRON_YS; ++i)
                    if (Creature* creature = ObjectAccessor::GetCreature(*me, instance->GetGuidData(i)))
                        creature->AI()->EnterEvadeMode();

                // 不确定，由萨拉说出（声音），根据wowwiki尤格-萨隆之声低语
                Map::PlayerList const& players = me->GetMap()->GetPlayers();
                for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                    if (Player* player = itr->GetSource())
                    {
                        if (events.IsInPhase(PHASE_ONE))
                            Talk(WHISPER_VOICE_PHASE_1_WIPE, player);

                        // 移除理智和疯狂光环
                        player->RemoveAurasDueToSpell(SPELL_SANITY);
                        player->RemoveAurasDueToSpell(SPELL_INSANE);
                    }
            }

            /**
             * @brief 重置战斗
             *
             * 重置所有战斗状态，召唤不祥之云并设置初始阶段。
             *
             * @调用时机 战斗结束或重置时
             */
            void Reset() override
            {
                _Reset();
                events.SetPhase(PHASE_ONE);

                // 设置成就标志
                instance->SetData(DATA_DRIVE_ME_CRAZY, uint32(true));
                instance->DoStopTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMED_START_EVENT);

                Initialize();

                // 召唤不祥之云，并设置它们的移动路径（交替顺时针和逆时针）
                bool clockwise = false;
                std::list<TempSummon*> clouds;
                me->SummonCreatureGroup(CREATURE_GROUP_CLOUDS, &clouds);
                clouds.sort(Trinity::ObjectDistanceOrderPred(me, true));
                for (std::list<TempSummon*>::const_iterator itr = clouds.begin(); itr != clouds.end(); ++itr)
                {
                    (*itr)->AI()->DoAction(int32(clockwise));
                    clockwise = !clockwise;
                }
            }

            /**
             * @brief 进入战斗
             * @param who 目标单位
             *
             * 开始战斗，初始化理智系统，设置萨拉和守护者的战斗状态。
             *
             * @调用时机 战斗开始时
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                // 萨拉进入战斗
                if (Creature* sara = instance->GetCreature(DATA_SARA))
                    sara->SetInCombatWith(me);

                // 所有激活的守护者进入战斗
                for (uint8 i = DATA_FREYA_YS; i <= DATA_MIMIRON_YS; ++i)
                    if (Creature* keeper = ObjectAccessor::GetCreature(*me, instance->GetGuidData(i)))
                        keeper->SetInCombatWith(me);

                // 开始计时成就
                instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMED_START_EVENT);

                // 施放理智周期光环，召唤第一个守护者
                DoCastAOE(SPELL_SUMMON_GUARDIAN_2, { SPELLVALUE_MAX_TARGETS, 1 });
                DoCast(me, SPELL_SANITY_PERIODIC);

                // 调度事件
                events.ScheduleEvent(EVENT_LOCK_DOOR, 15s);
                events.ScheduleEvent(EVENT_SUMMON_GUARDIAN_OF_YOGG_SARON, _guardianTimer, 0, PHASE_ONE);
                events.ScheduleEvent(EVENT_EXTINGUISH_ALL_LIFE, 15min);    // 15分钟狂暴
            }

            /**
             * @brief 死亡处理
             * @param killer 击杀者
             *
             * 不消失尤格-萨隆的尸体，将其从召唤列表中移除。
             *
             * @调用时机 尤格-萨隆之声死亡时（战斗胜利）
             */
            void JustDied(Unit* /*killer*/) override
            {
                // 不消失尤格-萨隆的尸体，将其从召唤列表中移除
                if (Creature* yogg = instance->GetCreature(DATA_YOGG_SARON))
                    summons.Despawn(yogg);

                _JustDied();
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主要的AI更新循环，处理事件调度和战斗逻辑。
             *
             * @调用时机 每帧调用
             * @性能注意事项 包含事件循环和多个条件判断，性能影响中等
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                // 如果没有玩家在战斗中，脱战
                if (!me->GetCombatManager().HasPvECombatWithPlayers())
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);

                events.Update(diff);
                // 当幻象破碎时，延迟触手召唤事件
                if (_illusionShattered)
                    events.DelayEvents(Milliseconds(diff), EVENT_GROUP_SUMMON_TENTACLES);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_LOCK_DOOR:
                            // 锁门，开始疯狂周期检测
                            DoCast(me, SPELL_INSANE_PERIODIC);
                            instance->SetBossState(DATA_YOGG_SARON, IN_PROGRESS);
                            break;
                        case EVENT_EXTINGUISH_ALL_LIFE:
                            // 狂暴：熄灭所有生命
                            if (Creature* yogg = instance->GetCreature(DATA_YOGG_SARON))
                            {
                                yogg->AI()->Talk(EMOTE_YOGG_SARON_EXTINGUISH_ALL_LIFE, me);
                                yogg->CastSpell(nullptr, SPELL_EXTINGUISH_ALL_LIFE, true);
                            }
                            // 短时间后再次施放，玩家可能存活
                            events.ScheduleEvent(EVENT_EXTINGUISH_ALL_LIFE, 10s);
                            break;
                        case EVENT_SUMMON_GUARDIAN_OF_YOGG_SARON:
                            // 第一阶段：召唤守护者
                            DoCastAOE(SPELL_SUMMON_GUARDIAN_2, { SPELLVALUE_MAX_TARGETS, 1 });
                            ++_guardiansCount;
                            // 前6个守护者，每3个减少5秒召唤间隔
                            if (_guardiansCount <= 6 && _guardiansCount % 3 == 0)
                                _guardianTimer -= 5s;
                            events.ScheduleEvent(EVENT_SUMMON_GUARDIAN_OF_YOGG_SARON, _guardianTimer, 0, PHASE_ONE);
                            break;
                        case EVENT_SUMMON_CORRUPTOR_TENTACLE:
                            // 第二阶段：召唤腐化触手
                            DoCastAOE(SPELL_CORRUPTOR_TENTACLE_SUMMON);
                            events.ScheduleEvent(EVENT_SUMMON_CORRUPTOR_TENTACLE, 30s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                            break;
                        case EVENT_SUMMON_CONSTRICTOR_TENTACLE:
                            // 第二阶段：召唤缠绕触手
                            DoCastAOE(SPELL_CONSTRICTOR_TENTACLE, { SPELLVALUE_MAX_TARGETS, 1 });
                            events.ScheduleEvent(EVENT_SUMMON_CONSTRICTOR_TENTACLE, 25s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                            break;
                        case EVENT_SUMMON_CRUSHER_TENTACLE:
                            // 第二阶段：召唤粉碎触手
                            DoCastAOE(SPELL_CRUSHER_TENTACLE_SUMMON);
                            events.ScheduleEvent(EVENT_SUMMON_CRUSHER_TENTACLE, 60s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                            break;
                        case EVENT_ILLUSION:
                        {
                            // 第二阶段：幻象房间
                            if (Creature* yogg = instance->GetCreature(DATA_YOGG_SARON))
                            {
                                yogg->AI()->Talk(EMOTE_YOGG_SARON_MADNESS);
                                yogg->AI()->Talk(SAY_YOGG_SARON_MADNESS);
                            }

                            // 召唤传送门
                            me->SummonCreatureGroup(CREATURE_GROUP_PORTALS_10);
                            if (me->GetMap()->Is25ManRaid())
                                me->SummonCreatureGroup(CREATURE_GROUP_PORTALS_25);

                            // 随机选择幻象类型
                            uint8 illusion = urand(CHAMBER_ILLUSION, STORMWIND_ILLUSION);
                            instance->SetData(DATA_ILLUSION, illusion);

                            // 通知大脑开始诱导疯狂
                            if (Creature* brain = instance->GetCreature(DATA_BRAIN_OF_YOGG_SARON))
                                brain->AI()->DoAction(ACTION_INDUCE_MADNESS);
                            // wowwiki说是80秒，wowhead说是90秒左右
                            events.ScheduleEvent(EVENT_ILLUSION, 80s, 0, PHASE_TWO);
                            break;
                        }
                        case EVENT_SUMMON_IMMORTAL_GUARDIAN:
                            // 第三阶段：召唤不朽守护者
                            DoCastAOE(SPELL_IMMORTAL_GUARDIAN);
                            events.ScheduleEvent(EVENT_SUMMON_IMMORTAL_GUARDIAN, 15s, 0, PHASE_THREE);
                            break;
                        default:
                            break;
                    }
                }
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理阶段转换和其他外部触发的事件。
             *
             * @调用时机 由其他AI（萨拉、大脑等）触发
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_PHASE_TRANSFORM:
                        // 进入转换阶段
                        events.SetPhase(PHASE_TRANSFORM);
                        // 移除所有不祥之云
                        summons.DespawnEntry(NPC_OMINOUS_CLOUD);
                        break;
                    case ACTION_PHASE_TWO:
                        // 进入第二阶段
                        events.SetPhase(PHASE_TWO);
                        // 召唤尤格-萨隆本体
                        me->SummonCreature(NPC_YOGG_SARON, YoggSaronSpawnPos);
                        // 大脑进入战斗
                        if (Creature* brain = instance->GetCreature(DATA_BRAIN_OF_YOGG_SARON))
                            DoZoneInCombat(brain);
                        // 调度触手召唤事件
                        events.ScheduleEvent(EVENT_SUMMON_CORRUPTOR_TENTACLE, 5s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                        events.ScheduleEvent(EVENT_SUMMON_CONSTRICTOR_TENTACLE, 7s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                        events.ScheduleEvent(EVENT_SUMMON_CRUSHER_TENTACLE, 5s, EVENT_GROUP_SUMMON_TENTACLES, PHASE_TWO);
                        events.ScheduleEvent(EVENT_ILLUSION, 1min, 0, PHASE_TWO);
                        break;
                    case ACTION_TOGGLE_SHATTERED_ILLUSION:
                        // 切换幻象破碎状态
                        _illusionShattered = !_illusionShattered;
                        break;
                    case ACTION_PHASE_THREE:
                        // 进入第三阶段
                        events.SetPhase(PHASE_THREE);
                        events.ScheduleEvent(EVENT_SUMMON_IMMORTAL_GUARDIAN, 1s, 0, PHASE_THREE);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 召唤生物处理
             * @param summon 召唤的生物
             *
             * 处理各种召唤生物的初始化，包括设置攻击延迟、视觉效果等。
             *
             * @调用时机 生物被召唤时
             */
            void JustSummoned(Creature* summon) override
            {
                switch (summon->GetEntry())
                {
                    case NPC_GUARDIAN_OF_YOGG_SARON:
                        // 守护者：延迟1秒开始攻击
                        summon->m_Events.AddEvent(new StartAttackEvent(me, summon), summon->m_Events.CalculateTime(1s));
                        break;
                    case NPC_YOGG_SARON:
                        // 尤格-萨隆：播放出现动画
                        summon->HandleEmoteCommand(EMOTE_ONESHOT_EMERGE);
                        break;
                    case NPC_CONSTRICTOR_TENTACLE:
                        // 缠绕触手：立即施放猛扑
                        summon->CastSpell(summon, SPELL_LUNGE, true);
                        break;
                    case NPC_CRUSHER_TENTACLE:
                    case NPC_CORRUPTOR_TENTACLE:
                        // 粉碎和腐化触手：延迟5秒开始攻击
                        summon->SetReactState(REACT_PASSIVE);
                        summon->HandleEmoteCommand(EMOTE_ONESHOT_EMERGE);
                        summon->m_Events.AddEvent(new StartAttackEvent(me, summon), summon->m_Events.CalculateTime(5s));
                        break;
                    case NPC_DESCEND_INTO_MADNESS:
                        // 传送门：显示传送门视觉效果
                        summon->CastSpell(summon, SPELL_TELEPORT_PORTAL_VISUAL, true);
                        break;
                    case NPC_IMMORTAL_GUARDIAN:
                        // 不朽守护者：传送效果
                        summon->CastSpell(summon, SPELL_SIMPLE_TELEPORT, true);
                        break;
                }

                BossAI::JustSummoned(summon);
            }

        private:
            uint8 _guardiansCount;          ///< 已召唤的守护者数量
            Milliseconds _guardianTimer;    ///< 守护者召唤间隔
            bool _illusionShattered;        ///< 幻象是否破碎（用于延迟触手召唤）
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_voice_of_yogg_saronAI>(creature);
        }
};

/**
 * @class boss_sara
 * @brief 萨拉脚本类
 *
 * 萨拉是第一阶段的Boss，实际上是被尤格-萨隆控制的女巨人。
 * 当萨拉被"杀死"后，会触发转换阶段，尤格-萨隆出现。
 *
 * 第一阶段行为：
 * - 对玩家施放各种负面法术（萨拉的狂热、祝福、愤怒）
 * - 召唤尤格-萨隆守护者
 * - 管理大脑连接机制
 *
 * 第二阶段行为：
 * - 变身为尤格-萨隆的一部分
 * - 继续施放精神病、心灵疾病、大脑连接和死亡射线
 */
class boss_sara : public CreatureScript
{
    public:
        boss_sara() : CreatureScript("boss_sara") { }

        /**
         * @struct boss_saraAI
         * @brief 萨拉的AI实现
         *
         * 实现了萨拉在第一和第二阶段的战斗逻辑。
         */
        struct boss_saraAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_saraAI(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

            /**
             * @brief 获取连接玩家的GUID
             * @param guid 玩家GUID
             * @return 与该玩家连接的另一个玩家GUID
             *
             * 用于大脑连接机制，查找与指定玩家配对的另一个玩家。
             */
            ObjectGuid GetLinkedPlayerGUID(ObjectGuid guid) const
            {
                std::map<ObjectGuid, ObjectGuid>::const_iterator itr = _linkData.find(guid);
                if (itr != _linkData.end())
                    return itr->second;

                return ObjectGuid::Empty;
            }

            /**
             * @brief 设置玩家之间的连接
             * @param player1 第一个玩家的GUID
             * @param player2 第二个玩家的GUID
             *
             * 建立两个玩家之间的大脑连接。
             */
            void SetLinkBetween(ObjectGuid player1, ObjectGuid player2)
            {
                _linkData[player1] = player2;
                _linkData[player2] = player1;
            }

            /**
             * @brief 从连接中移除玩家
             * @param player1 要移除的玩家GUID
             *
             * 当光环移除时，为每个目标调用一次。
             */
            // called once for each target on aura remove
            void RemoveLinkFrom(ObjectGuid player1)
            {
                _linkData.erase(player1);
            }

            /**
             * @brief 受到伤害处理
             * @param attacker 攻击者
             * @param damage 伤害值（可修改）
             * @param damageType 伤害类型
             * @param spellInfo 法术信息
             *
             * 当萨拉的生命值降至0时，触发转换阶段。
             * 不允许萨拉真正死亡，而是保持1点生命值并开始转换序列。
             *
             * @调用时机 每次受到伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (damage >= me->GetHealth())
                {
                    // 保持1点生命值
                    damage = me->GetHealth() - 1;

                    if (_events.IsInPhase(PHASE_ONE))
                    {
                        // 通知尤格-萨隆之声开始转换阶段
                        if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                            voice->AI()->DoAction(ACTION_PHASE_TRANSFORM);

                        // 开始转换序列的台词
                        Talk(SAY_SARA_TRANSFORM_1);
                        _events.SetPhase(PHASE_TRANSFORM);
                        _events.ScheduleEvent(EVENT_TRANSFORM_1, 4700ms, 0, PHASE_TRANSFORM);
                        _events.ScheduleEvent(EVENT_TRANSFORM_2, 9500ms, 0, PHASE_TRANSFORM);
                        _events.ScheduleEvent(EVENT_TRANSFORM_3, 14300ms, 0, PHASE_TRANSFORM);
                        _events.ScheduleEvent(EVENT_TRANSFORM_4, 14500ms, 0, PHASE_TRANSFORM);
                    }
                }
            }

            /**
             * @brief 法术命中目标处理
             * @param target 目标对象
             * @param spellInfo 法术信息
             *
             * 当法术命中目标时，有30%几率喊话。
             * 在转换阶段不喊话。
             *
             * @调用时机 法术命中目标时
             */
            void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
            {
                if (!roll_chance_i(30) || _events.IsInPhase(PHASE_TRANSFORM))
                    return;

                switch (spellInfo->Id)
                {
                    case SPELL_SARAS_FERVOR:
                        Talk(SAY_SARA_FERVOR_HIT);
                        break;
                    case SPELL_SARAS_BLESSING:
                        Talk(SAY_SARA_BLESSING_HIT);
                        break;
                    case SPELL_PSYCHOSIS:
                        Talk(SAY_SARA_PSYCHOSIS_HIT);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 击杀单位处理
             * @param victim 被击杀的单位
             *
             * 当萨拉击杀玩家时喊话。
             *
             * @调用时机 击杀玩家时
             */
            void KilledUnit(Unit* victim) override
            {
                if (victim->GetTypeId() == TYPEID_PLAYER && !me->IsInEvadeMode())
                    Talk(SAY_SARA_KILL);
            }

            /**
             * @brief 进入战斗
             * @param who 目标单位
             *
             * 开始第一阶段的战斗，调度各种法术事件。
             *
             * @调用时机 战斗开始时
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                Talk(SAY_SARA_AGGRO);
                _events.ScheduleEvent(EVENT_SARAS_FERVOR, 5s, 0, PHASE_ONE);
                _events.ScheduleEvent(EVENT_SARAS_BLESSING, 10s, 30s, 0, PHASE_ONE);
                _events.ScheduleEvent(EVENT_SARAS_ANGER, 15s, 25s, 0, PHASE_ONE);
            }

            /**
             * @brief 刚进入战斗
             * @param who 目标单位
             *
             * 防止重复进入战斗。
             *
             * @调用时机 进入战斗时
             */
            void JustEnteredCombat(Unit* who) override
            {
                if (IsEngaged())
                    return;

                EngagementStart(who);
            }

            /**
             * @brief 重置
             *
             * 重置萨拉的状态，移除所有光环，设置为被动和友好阵营。
             *
             * @调用时机 战斗重置时
             */
            void Reset() override
            {
                me->RemoveAllAuras();
                me->SetReactState(REACT_PASSIVE);  // 被动反应状态
                me->SetFaction(FACTION_FRIENDLY);  // 友好阵营
                _events.Reset();
                _events.SetPhase(PHASE_ONE);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主要的AI更新循环，处理第一和第二阶段的法术施放。
             * 当破碎幻象激活时暂停行动。
             *
             * @调用时机 每帧调用
             * @性能注意事项 包含事件循环和施法判断，性能影响中等
             */
            void UpdateAI(uint32 diff) override
            {
                if (!me->IsInCombat())
                    return;

                // 破碎幻象期间不行动
                if (me->HasAura(SPELL_SHATTERED_ILLUSION))
                    return;

                _events.Update(diff);

                // 如果正在施法，等待
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SARAS_FERVOR:
                            // 第一阶段：萨拉的狂热
                            DoCastAOE(SPELL_SARAS_FERVOR_TARGET_SELECTOR, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_SARAS_FERVOR, 6s, 0, PHASE_ONE);
                            break;
                        case EVENT_SARAS_ANGER:
                            // 第一阶段：萨拉的愤怒
                            DoCastAOE(SPELL_SARAS_ANGER_TARGET_SELECTOR, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_SARAS_ANGER, 6s, 8s, 0, PHASE_ONE);
                            break;
                        case EVENT_SARAS_BLESSING:
                            // 第一阶段：萨拉的祝福
                            DoCastAOE(SPELL_SARAS_BLESSING_TARGET_SELECTOR, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_SARAS_BLESSING, 6s, 30s, 0, PHASE_ONE);
                            break;
                        case EVENT_TRANSFORM_1:
                            // 转换阶段：台词2
                            Talk(SAY_SARA_TRANSFORM_2);
                            break;
                        case EVENT_TRANSFORM_2:
                            // 转换阶段：台词3
                            Talk(SAY_SARA_TRANSFORM_3);
                            break;
                        case EVENT_TRANSFORM_3:
                            // 转换阶段：台词4，完全治疗，改变阵营，通知开始第二阶段
                            Talk(SAY_SARA_TRANSFORM_4);
                            DoCast(me, SPELL_FULL_HEAL);
                            me->SetFaction(FACTION_MONSTER_2);  // 敌对阵营
                            // 通知尤格-萨隆之声进入第二阶段
                            if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                                voice->AI()->DoAction(ACTION_PHASE_TWO);
                            // 通知米米尔隆进入第二阶段
                            if (Creature* mimiron = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_MIMIRON_YS)))
                                mimiron->AI()->DoAction(ACTION_PHASE_TWO);
                            break;
                        case EVENT_TRANSFORM_4:
                            // 转换阶段：变形，骑乘尤格-萨隆，施放暗影屏障
                            DoCast(me, SPELL_PHASE_2_TRANSFORM);
                            if (Creature* yogg = _instance->GetCreature(DATA_YOGG_SARON))
                                DoCast(yogg, SPELL_RIDE_YOGG_SARON_VEHICLE);
                            DoCast(me, SPELL_SHADOWY_BARRIER_SARA);
                            _events.SetPhase(PHASE_TWO);
                            // 调度第二阶段法术
                            _events.ScheduleEvent(EVENT_DEATH_RAY, 20s, 0, PHASE_TWO);    // 几乎从不在预定时间施放，为什么？
                            _events.ScheduleEvent(EVENT_MALADY_OF_THE_MIND, 18s, 0, PHASE_TWO);
                            _events.ScheduleEvent(EVENT_PSYCHOSIS, 1ms, 0, PHASE_TWO);
                            _events.ScheduleEvent(EVENT_BRAIN_LINK, 23s, 0, PHASE_TWO);
                            break;
                        case EVENT_DEATH_RAY:
                            // 第二阶段：死亡射线
                            DoCast(me, SPELL_DEATH_RAY);
                            _events.ScheduleEvent(EVENT_DEATH_RAY, 21s, 0, PHASE_TWO);
                            break;
                        case EVENT_MALADY_OF_THE_MIND:
                            // 第二阶段：心灵疾病
                            DoCastAOE(SPELL_MALADY_OF_THE_MIND, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_MALADY_OF_THE_MIND, 18s, 25s, 0, PHASE_TWO);
                            break;
                        case EVENT_PSYCHOSIS:
                            // 第二阶段：精神病
                            DoCastAOE(SPELL_PSYCHOSIS, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_PSYCHOSIS, 4s, 0, PHASE_TWO);
                            break;
                        case EVENT_BRAIN_LINK:
                            // 第二阶段：大脑连接
                            DoCastAOE(SPELL_BRAIN_LINK, { SPELLVALUE_MAX_TARGETS, 2 });
                            _events.ScheduleEvent(EVENT_BRAIN_LINK, 23s, 26s, 0, PHASE_TWO);
                            break;
                        default:
                            break;
                    }
                }
            }

            /**
             * @brief 召唤生物处理
             * @param summon 召唤的生物
             *
             * 处理死亡射线相关的召唤物。
             *
             * @调用时机 生物被召唤时
             */
            void JustSummoned(Creature* summon) override
            {
                summon->SetReactState(REACT_PASSIVE);

                switch (summon->GetEntry())
                {
                    case NPC_DEATH_ORB:
                        // 死亡之球：喊话，施放视觉效果，召唤4条死亡射线
                        Talk(SAY_SARA_DEATH_RAY);
                        summon->CastSpell(summon, SPELL_DEATH_RAY_ORIGIN_VISUAL);
                        // 召唤4条死亡射线，随机位置
                        for (uint8 i = 0; i < 4; ++i)
                        {
                            Position pos;
                            float radius = frand(25.0f, 50.0f);
                            float angle = frand(0.0f, 2.0f * float(M_PI));
                            pos.m_positionX = YoggSaronSpawnPos.GetPositionX() + radius * cosf(angle);
                            pos.m_positionY = YoggSaronSpawnPos.GetPositionY() + radius * sinf(angle);
                            pos.m_positionZ = me->GetMap()->GetHeight(me->GetPhaseMask(), pos.GetPositionX(), pos.GetPositionY(), YoggSaronSpawnPos.GetPositionZ() + 5.0f);
                            me->SummonCreature(NPC_DEATH_RAY, pos, TEMPSUMMON_TIMED_DESPAWN, 20s);
                        }
                        break;
                    case NPC_DEATH_RAY:
                        // 死亡射线：施放警告视觉效果
                        summon->CastSpell(summon, SPELL_DEATH_RAY_WARNING_VISUAL);
                        break;
                }

                // 通知尤格-萨隆之声
                if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                    voice->AI()->JustSummoned(summon);
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理阶段转换动作。
             *
             * @调用时机 由其他AI触发
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_PHASE_THREE:    // 萨拉在第三阶段不行动
                        _events.SetPhase(PHASE_THREE);
                        break;
                    default:
                        break;
                }
            }

            private:
                EventMap _events;                               ///< 事件映射表
                InstanceScript* _instance;                      ///< 副本脚本指针
                std::map<ObjectGuid, ObjectGuid> _linkData;     ///< 大脑连接数据映射
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_saraAI>(creature);
        }
};

class boss_yogg_saron : public CreatureScript
{
    public:
        boss_yogg_saron() : CreatureScript("boss_yogg_saron") { }

        struct boss_yogg_saronAI : public PassiveAI
        {
            boss_yogg_saronAI(Creature* creature) : PassiveAI(creature), _instance(creature->GetInstanceScript()) { }

            void Reset() override
            {
                _events.Reset();
                _events.SetPhase(PHASE_TWO);
                _events.ScheduleEvent(EVENT_YELL_BOW_DOWN, 3s, 0, PHASE_TWO);
                DoCast(me, SPELL_SHADOWY_BARRIER_YOGG);
                DoCast(me, SPELL_KNOCK_AWAY);

                me->ResetLootMode();
                uint32 keepersCount = _instance->GetData(DATA_KEEPERS_COUNT);
                if (keepersCount == 0)
                    me->AddLootMode(LOOT_MODE_HARD_MODE_4);
                if (keepersCount <= 1)
                    me->AddLootMode(LOOT_MODE_HARD_MODE_3);
                if (keepersCount <= 2)
                    me->AddLootMode(LOOT_MODE_HARD_MODE_2);
                if (keepersCount <= 3)
                    me->AddLootMode(LOOT_MODE_HARD_MODE_1);
            }

            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                // Val'anyr
                if (spellInfo->Id == SPELL_IN_THE_MAWS_OF_THE_OLD_GOD)
                    me->AddLootMode(32);
            }

            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_YOGG_SARON_DEATH);

                if (Creature* creature = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                    Unit::Kill(me, creature);

                for (uint8 i = DATA_SARA; i <= DATA_BRAIN_OF_YOGG_SARON; ++i)
                    if (Creature* creature = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(i)))
                        creature->DisappearAndDie();

                for (uint8 i = DATA_FREYA_YS; i <= DATA_MIMIRON_YS; ++i)
                    if (Creature* creature = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(i)))
                        creature->AI()->EnterEvadeMode();

                Map::PlayerList const& players = me->GetMap()->GetPlayers();
                for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                    if (Player* player = itr->GetSource())
                    {
                        player->RemoveAurasDueToSpell(SPELL_SANITY);
                        player->RemoveAurasDueToSpell(SPELL_INSANE);
                    }
            }

            void UpdateAI(uint32 diff) override
            {
                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_YELL_BOW_DOWN:
                            Talk(SAY_YOGG_SARON_SPAWN);
                            break;
                        case EVENT_SHADOW_BEACON:
                            DoCastAOE(SPELL_SHADOW_BEACON);
                            Talk(EMOTE_YOGG_SARON_EMPOWERING_SHADOWS);
                            _events.ScheduleEvent(EVENT_SHADOW_BEACON, 45s, 0, PHASE_THREE);
                            break;
                        case EVENT_LUNATIC_GAZE:
                            DoCast(me, SPELL_LUNATIC_GAZE);
                            sCreatureTextMgr->SendSound(me, SOUND_LUNATIC_GAZE, CHAT_MSG_MONSTER_YELL, 0, TEXT_RANGE_NORMAL, TEAM_OTHER, false);
                            _events.ScheduleEvent(EVENT_LUNATIC_GAZE, 12s, 0, PHASE_THREE);
                            break;
                        case EVENT_DEAFENING_ROAR:
                            DoCastAOE(SPELL_DEAFENING_ROAR);
                            Talk(SAY_YOGG_SARON_DEAFENING_ROAR);
                            Talk(EMOTE_YOGG_SARON_DEAFENING_ROAR);
                            _events.ScheduleEvent(EVENT_DEAFENING_ROAR, 20s, 25s, 0, PHASE_THREE);    // timer guessed
                            break;
                        default:
                            break;
                    }
                }
            }

            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_PHASE_THREE:
                        _events.SetPhase(PHASE_THREE);
                        _events.ScheduleEvent(EVENT_SHADOW_BEACON, 45s, 0, PHASE_THREE);
                        _events.ScheduleEvent(EVENT_LUNATIC_GAZE, 12s, 0, PHASE_THREE);
                        if (me->GetMap()->Is25ManRaid() && _instance->GetData(DATA_KEEPERS_COUNT) < 4)
                            _events.ScheduleEvent(EVENT_DEAFENING_ROAR, 20s, 25s, 0, PHASE_THREE);    // timer guessed
                        Talk(SAY_YOGG_SARON_PHASE_3);
                        DoCast(me, SPELL_PHASE_3_TRANSFORM);
                        me->RemoveAurasDueToSpell(SPELL_SHADOWY_BARRIER_YOGG);
                        me->ResetPlayerDamageReq();
                        break;
                    default:
                        break;
                }
            }

        private:
            EventMap _events;
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_yogg_saronAI>(creature);
        }
};

class boss_brain_of_yogg_saron : public CreatureScript
{
    public:
        boss_brain_of_yogg_saron() : CreatureScript("boss_brain_of_yogg_saron") { }

        struct boss_brain_of_yogg_saronAI : public PassiveAI
        {
            boss_brain_of_yogg_saronAI(Creature* creature) : PassiveAI(creature), _instance(creature->GetInstanceScript()), _summons(creature)
            {
                _tentaclesKilled = 0;
            }

            void Reset() override
            {
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetImmuneToPC(false);
                DoCast(me, SPELL_MATCH_HEALTH);
                _summons.DespawnAll();
            }

            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (me->HealthBelowPctDamaged(30, damage) && !me->HasAura(SPELL_BRAIN_HURT_VISUAL))
                {
                    me->RemoveAllAuras();
                    me->InterruptNonMeleeSpells(true);
                    DoCastAOE(SPELL_SHATTERED_ILLUSION_REMOVE, true);
                    DoCast(me, SPELL_MATCH_HEALTH_2, true); // it doesn't seem to hit Yogg-Saron here
                    DoCast(me, SPELL_BRAIN_HURT_VISUAL, true);
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    me->SetImmuneToPC(true);

                    if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                        voice->AI()->DoAction(ACTION_PHASE_THREE);
                    if (Creature* sara = _instance->GetCreature(DATA_SARA))
                        sara->AI()->DoAction(ACTION_PHASE_THREE);
                    if (Creature* yogg = _instance->GetCreature(DATA_YOGG_SARON))
                        yogg->AI()->DoAction(ACTION_PHASE_THREE);

                    for (uint8 i = DATA_THORIM_YS; i <= DATA_MIMIRON_YS; ++i)
                        if (Creature* keeper = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(i)))
                            keeper->AI()->DoAction(ACTION_PHASE_THREE);
                }
            }

            void UpdateAI(uint32 /*diff*/) override { }

            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_INDUCE_MADNESS:
                    {
                        _tentaclesKilled = 0;

                        me->SummonCreatureGroup(_instance->GetData(DATA_ILLUSION));

                        // make sure doors won't be opened
                        for (uint32 i = GO_BRAIN_ROOM_DOOR_1; i <= GO_BRAIN_ROOM_DOOR_3; ++i)
                            _instance->HandleGameObject(_instance->GetGuidData(i), false);

                        DoCastAOE(SPELL_INDUCE_MADNESS);
                        break;
                    }
                    case ACTION_TENTACLE_KILLED:
                    {
                        uint8 illusion = _instance->GetData(DATA_ILLUSION);
                        if (++_tentaclesKilled >= (illusion == ICECROWN_ILLUSION ? 9 : 8))
                        {
                            sCreatureTextMgr->SendChat(me, EMOTE_BRAIN_ILLUSION_SHATTERED, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_AREA);
                            _summons.DespawnAll();
                            DoCastAOE(SPELL_SHATTERED_ILLUSION, true);
                            _instance->HandleGameObject(_instance->GetGuidData(GO_BRAIN_ROOM_DOOR_1 + illusion), true);

                            if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                                voice->AI()->DoAction(ACTION_TOGGLE_SHATTERED_ILLUSION);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            void JustSummoned(Creature* summon) override
            {
                _summons.Summon(summon);
            }

        private:
            InstanceScript* _instance;
            SummonList _summons;
            uint8 _tentaclesKilled;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_brain_of_yogg_saronAI>(creature);
        }
};

class npc_ominous_cloud : public CreatureScript
{
    public:
        npc_ominous_cloud() : CreatureScript("npc_ominous_cloud") { }

        struct npc_ominous_cloudAI : public PassiveAI
        {
            npc_ominous_cloudAI(Creature* creature) : PassiveAI(creature) { }

            void Reset() override
            {
                DoCast(me, SPELL_OMINOUS_CLOUD_VISUAL);
            }

            void UpdateAI(uint32 /*diff*/) override { }

            void DoAction(int32 action) override
            {
                clockwise = bool(action);
                me->GetMotionMaster()->MoveCirclePath(YoggSaronSpawnPos.GetPositionX(), YoggSaronSpawnPos.GetPositionY(), me->GetPositionZ() + 5.0f, me->GetDistance2d(YoggSaronSpawnPos.GetPositionX(), YoggSaronSpawnPos.GetPositionY()), clockwise, 16);
            }

            bool clockwise = false;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_ominous_cloudAI>(creature);
        }
};

class npc_guardian_of_yogg_saron : public CreatureScript
{
    public:
        npc_guardian_of_yogg_saron() : CreatureScript("npc_guardian_of_yogg_saron") { }

        struct npc_guardian_of_yogg_saronAI : public ScriptedAI
        {
            npc_guardian_of_yogg_saronAI(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

            void JustDied(Unit* /*killer*/) override
            {
                DoCastAOE(SPELL_SHADOW_NOVA);
                DoCastAOE(SPELL_SHADOW_NOVA_2);
            }

            void Reset() override
            {
                _events.ScheduleEvent(EVENT_DARK_VOLLEY, 10s, 15s);
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DARK_VOLLEY:
                            DoCastAOE(SPELL_DARK_VOLLEY);
                            _events.ScheduleEvent(EVENT_DARK_VOLLEY, 10s, 15s);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

            void IsSummonedBy(WorldObject* summoner) override
            {
                if (summoner->GetEntry() != NPC_OMINOUS_CLOUD)
                    return;

                // Guardian can be summoned both by Voice of Yogg-Saron and by Ominous Cloud
                if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                    voice->AI()->JustSummoned(me);
            }

        private:
            EventMap _events;
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_guardian_of_yogg_saronAI>(creature);
        }
};

class npc_corruptor_tentacle : public CreatureScript
{
    public:
        npc_corruptor_tentacle() : CreatureScript("npc_corruptor_tentacle") { }

        struct npc_corruptor_tentacleAI : public ScriptedAI
        {
            npc_corruptor_tentacleAI(Creature* creature) : ScriptedAI(creature)
            {
                SetCombatMovement(false);
            }

            void Reset() override
            {
                DoCast(me, SPELL_TENTACLE_VOID_ZONE);
                DoCastAOE(SPELL_ERUPT);
                _events.ScheduleEvent(EVENT_CAST_RANDOM_SPELL, 1ms);
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                if (me->HasAura(SPELL_SHATTERED_ILLUSION))
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CAST_RANDOM_SPELL:
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                                DoCast(target, RAND(SPELL_BLACK_PLAGUE, SPELL_CURSE_OF_DOOM, SPELL_APATHY, SPELL_DRAINING_POISON));
                            _events.ScheduleEvent(EVENT_CAST_RANDOM_SPELL, 3s);
                            break;
                        default:
                            break;
                    }
                }
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_corruptor_tentacleAI>(creature);
        }
};

class npc_constrictor_tentacle : public CreatureScript
{
    public:
        npc_constrictor_tentacle() : CreatureScript("npc_constrictor_tentacle") { }

        struct npc_constrictor_tentacleAI : public ScriptedAI
        {
            npc_constrictor_tentacleAI(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
            {
                SetCombatMovement(false);
            }

            void Reset() override
            {
                DoCast(me, SPELL_TENTACLE_VOID_ZONE_2);
                DoCastAOE(SPELL_ERUPT);
            }

            void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
            {
                if (!apply)
                    passenger->RemoveAurasDueToSpell(sSpellMgr->GetSpellIdForDifficulty(SPELL_SQUEEZE, passenger));
            }

            void UpdateAI(uint32 /*diff*/) override
            {
                UpdateVictim();
            }

            void IsSummonedBy(WorldObject* /*summoner*/) override
            {
                if (Creature* voice = _instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                    voice->AI()->JustSummoned(me);
            }

        private:
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_constrictor_tentacleAI>(creature);
        }
};

class npc_crusher_tentacle : public CreatureScript
{
    public:
        npc_crusher_tentacle() : CreatureScript("npc_crusher_tentacle") { }

        struct npc_crusher_tentacleAI : public ScriptedAI
        {
            npc_crusher_tentacleAI(Creature* creature) : ScriptedAI(creature)
            {
                SetCombatMovement(false);
            }

            void Reset() override
            {
                DoCast(me, SPELL_CRUSH);
                DoCast(me, SPELL_TENTACLE_VOID_ZONE);
                DoCast(me, SPELL_DIMINSH_POWER);
                DoCast(me, SPELL_FOCUSED_ANGER);
                DoCastAOE(SPELL_ERUPT);

                _events.ScheduleEvent(EVENT_DIMINISH_POWER, 6s, 8s);
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                if (me->HasAura(SPELL_SHATTERED_ILLUSION) || me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // update timers when the Diminish Power is not being channeled so the next one
                // is not cast immediately after interrupt
                _events.Update(diff);

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DIMINISH_POWER:
                            DoCast(SPELL_DIMINISH_POWER);
                            _events.ScheduleEvent(EVENT_DIMINISH_POWER, 20s, 30s);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_crusher_tentacleAI>(creature);
        }
};

class npc_influence_tentacle : public CreatureScript
{
    public:
        npc_influence_tentacle() : CreatureScript("npc_influence_tentacle") { }

        struct npc_influence_tentacleAI : public PassiveAI
        {
            npc_influence_tentacleAI(Creature* creature) : PassiveAI(creature), _instance(creature->GetInstanceScript()) { }

            void Reset() override
            {
                DoCast(me, me->GetEntry() == NPC_SUIT_OF_ARMOR ? SPELL_NONDESCRIPT_1 : SPELL_NONDESCRIPT_2);
            }

            void JustDied(Unit* /*killer*/) override
            {
                if (Creature* brain = _instance->GetCreature(DATA_BRAIN_OF_YOGG_SARON))
                    brain->AI()->DoAction(ACTION_TENTACLE_KILLED);
            }

            void UpdateAI(uint32 /*diff*/) override { }

        private:
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_influence_tentacleAI>(creature);
        }
};

typedef boss_sara::boss_saraAI SaraAI;

class npc_descend_into_madness : public CreatureScript
{
    public:
        npc_descend_into_madness() : CreatureScript("npc_descend_into_madness") { }

        struct npc_descend_into_madnessAI : public PassiveAI
        {
            npc_descend_into_madnessAI(Creature* creature) : PassiveAI(creature), _instance(creature->GetInstanceScript()) { }

            void OnSpellClick(Unit* clicker, bool spellClickHandled) override
            {
                if (!spellClickHandled)
                    return;

                clicker->RemoveAurasDueToSpell(SPELL_BRAIN_LINK);
                uint32 illusion = _instance->GetData(DATA_ILLUSION);
                if (illusion < MAX_ILLUSION_ROOMS)
                    DoCast(clicker, IllusionSpells[illusion], true);
                me->DespawnOrUnsummon();
            }

            void UpdateAI(uint32 /*diff*/) override { }

        private:
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_descend_into_madnessAI>(creature);
        }
};

class npc_immortal_guardian : public CreatureScript
{
    public:
        npc_immortal_guardian() : CreatureScript("npc_immortal_guardian") { }

        struct npc_immortal_guardianAI : public ScriptedAI
        {
            npc_immortal_guardianAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                DoCast(me, SPELL_EMPOWERED);
                DoCast(me, SPELL_RECENTLY_SPAWNED);
                _events.ScheduleEvent(EVENT_DRAIN_LIFE, 3s, 13s);
            }

            void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (me->HealthBelowPctDamaged(1, damage))
                    damage = me->GetHealth() - me->CountPctFromMaxHealth(1);   // or set immune to damage? should be done here or in SPELL_WEAKENED spell script?
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DRAIN_LIFE:
                            DoCast(SPELL_DRAIN_LIFE);
                            _events.ScheduleEvent(EVENT_DRAIN_LIFE, 20s, 30s);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_immortal_guardianAI>(creature);
        }
};

class npc_observation_ring_keeper : public CreatureScript
{
    public:
        npc_observation_ring_keeper() : CreatureScript("npc_observation_ring_keeper") { }

        struct npc_observation_ring_keeperAI : public ScriptedAI
        {
            npc_observation_ring_keeperAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                DoCast(SPELL_SIMPLE_TELEPORT_KEEPERS);  // not visible here
                DoCast(SPELL_KEEPER_ACTIVE);
            }

            bool OnGossipSelect(Player* player, uint32 menuId, uint32 /*gossipListId*/) override
            {
                if (menuId != 10333)
                    return false;

                me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                me->DespawnOrUnsummon(2s);
                DoCast(SPELL_TELEPORT);
                Talk(SAY_KEEPER_CHOSEN_1, player);
                Talk(SAY_KEEPER_CHOSEN_2, player);

                switch (me->GetEntry())
                {
                    case NPC_FREYA_OBSERVATION_RING:
                        me->SummonCreature(NPC_FREYA_YS, YSKeepersPos[0]);
                        break;
                    case NPC_HODIR_OBSERVATION_RING:
                        me->SummonCreature(NPC_HODIR_YS, YSKeepersPos[1]);
                        break;
                    case NPC_THORIM_OBSERVATION_RING:
                        me->SummonCreature(NPC_THORIM_YS, YSKeepersPos[2]);
                        break;
                    case NPC_MIMIRON_OBSERVATION_RING:
                        me->SummonCreature(NPC_MIMIRON_YS, YSKeepersPos[3]);
                        break;
                }
                return false;
            }

            void UpdateAI(uint32 /*diff*/) override { }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_observation_ring_keeperAI>(creature);
        }
};

class npc_yogg_saron_keeper : public CreatureScript
{
    public:
        npc_yogg_saron_keeper() : CreatureScript("npc_yogg_saron_keeper") { }

        struct npc_yogg_saron_keeperAI : public ScriptedAI
        {
            npc_yogg_saron_keeperAI(Creature* creature) : ScriptedAI(creature) { }

            void IsSummonedBy(WorldObject* /*summoner*/) override
            {
                DoCast(SPELL_SIMPLE_TELEPORT_KEEPERS);
            }

            void Reset() override
            {
                _events.Reset();
                _events.SetPhase(PHASE_ONE);
                me->SetReactState(REACT_PASSIVE);
                me->RemoveAllAuras();

                DoCast(SPELL_KEEPER_ACTIVE);    // can we skip removing this aura somehow?

                if (me->GetEntry() == NPC_FREYA_YS)
                {
                    std::list<Creature*> wells;
                    GetCreatureListWithEntryInGrid(wells, me, NPC_SANITY_WELL, 200.0f);
                    for (std::list<Creature*>::const_iterator itr = wells.begin(); itr != wells.end(); ++itr)
                    {
                        (*itr)->RemoveAurasDueToSpell(SPELL_SANITY_WELL);
                        (*itr)->RemoveAurasDueToSpell(SPELL_SANITY_WELL_VISUAL);
                    }
                }
            }

            void JustEnteredCombat(Unit* who) override
            {
                if (IsEngaged())
                    return;

                EngagementStart(who);

                switch (me->GetEntry())
                {
                    case NPC_FREYA_YS:
                        DoCast(SPELL_RESILIENCE_OF_NATURE);
                        DoCast(SPELL_SANITY_WELL_SUMMON);
                        break;
                    case NPC_HODIR_YS:
                        DoCast(SPELL_FORTITUDE_OF_FROST);
                        DoCast(SPELL_HODIRS_PROTECTIVE_GAZE);
                        break;
                    case NPC_THORIM_YS:
                        DoCast(SPELL_FURY_OF_THE_STORM);
                        break;
                    case NPC_MIMIRON_YS:
                        DoCast(SPELL_SPEED_OF_INVENTION);
                        break;
                }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!me->IsInCombat())
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DESTABILIZATION_MATRIX:
                            DoCastAOE(SPELL_DESTABILIZATION_MATRIX, { SPELLVALUE_MAX_TARGETS, 1 });
                            _events.ScheduleEvent(EVENT_DESTABILIZATION_MATRIX, 15s, 25s, 0, PHASE_TWO);
                            break;
                        case EVENT_HODIRS_PROTECTIVE_GAZE:
                            DoCast(SPELL_HODIRS_PROTECTIVE_GAZE);
                            break;
                    }
                }
            }

            void DoAction(int32 action) override
            {
                switch (action)
                {
                    // setting the phases is only for Thorim and Mimiron
                    case ACTION_PHASE_TWO:
                        _events.SetPhase(PHASE_TWO);
                        _events.ScheduleEvent(EVENT_DESTABILIZATION_MATRIX, 5s, 15s, 0, PHASE_TWO);
                        break;
                    case ACTION_PHASE_THREE:
                        _events.SetPhase(PHASE_THREE);
                        if (me->GetEntry() == NPC_THORIM_YS)
                            DoCast(SPELL_TITANIC_STORM);
                        break;
                    case ACTION_SANITY_WELLS:
                    {
                        std::list<Creature*> wells;
                        GetCreatureListWithEntryInGrid(wells, me, NPC_SANITY_WELL, 200.0f);
                        for (std::list<Creature*>::const_iterator itr = wells.begin(); itr != wells.end(); ++itr)
                        {
                            (*itr)->CastSpell(*itr, SPELL_SANITY_WELL);
                            (*itr)->CastSpell(*itr, SPELL_SANITY_WELL_VISUAL);
                        }
                        break;
                    }
                    case ACTION_FLASH_FREEZE:
                        DoCast(SPELL_FLASH_FREEZE_VISUAL);
                        _events.ScheduleEvent(EVENT_HODIRS_PROTECTIVE_GAZE, 25s, 30s);
                        break;
                }
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_yogg_saron_keeperAI>(creature);
        }
};

class npc_yogg_saron_illusions : public CreatureScript
{
    public:
        npc_yogg_saron_illusions() : CreatureScript("npc_yogg_saron_illusions") { }

        struct npc_yogg_saron_illusionsAI : public ScriptedAI
        {
            npc_yogg_saron_illusionsAI(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

            void IsSummonedBy(WorldObject* /*summoner*/) override
            {
                switch (_instance->GetData(DATA_ILLUSION))
                {
                    case CHAMBER_ILLUSION:
                        // i think the first Talk should be delayed as in this moment
                        // players are too far away to be able to see it
                        if (Creature* neltharion = me->FindNearestCreature(NPC_NELTHARION, 50.0f))
                            neltharion->AI()->Talk(SAY_CHAMBER_ROLEPLAY_1);

                        _events.ScheduleEvent(EVENT_CHAMBER_ROLEPLAY_1, 16s);
                        _events.ScheduleEvent(EVENT_CHAMBER_ROLEPLAY_2, 22s);
                        _events.ScheduleEvent(EVENT_CHAMBER_ROLEPLAY_3, 28s);
                        _events.ScheduleEvent(EVENT_CHAMBER_ROLEPLAY_4, 36s);
                        break;
                    case ICECROWN_ILLUSION:
                        // same here
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_1, 1s);
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_2, 7500ms);
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_3, 19500ms);
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_4, 25500ms);
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_5, 33s);
                        _events.ScheduleEvent(EVENT_ICECROWN_ROLEPLAY_6, 41300ms);
                        break;
                    case STORMWIND_ILLUSION:
                        _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_4, 33800ms); // "A thousand deaths..."
                        _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_5, 38850ms);
                        _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_7, 58750ms);
                        // TODO: use "or one murder." sound and split the text in DB
                        break;
                }
            }

            void UpdateAI(uint32 diff) override
            {
                _events.Update(diff);

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CHAMBER_ROLEPLAY_1:
                            if (Creature* ysera = me->FindNearestCreature(NPC_YSERA, 50.0f))
                                ysera->AI()->Talk(SAY_CHAMBER_ROLEPLAY_2);
                            break;
                        case EVENT_CHAMBER_ROLEPLAY_2:
                            if (Creature* neltharion = me->FindNearestCreature(NPC_NELTHARION, 50.0f))
                                neltharion->AI()->Talk(SAY_CHAMBER_ROLEPLAY_3);
                            break;
                        case EVENT_CHAMBER_ROLEPLAY_3:
                            if (Creature* malygos = me->FindNearestCreature(NPC_MALYGOS, 50.0f))
                                malygos->AI()->Talk(SAY_CHAMBER_ROLEPLAY_4);
                            break;
                        case EVENT_CHAMBER_ROLEPLAY_4:
                            Talk(SAY_CHAMBER_ROLEPLAY_5);
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_1:
                            if (Creature* bolvar = me->FindNearestCreature(NPC_IMMOLATED_CHAMPION, 50.0f))
                            {
                                bolvar->AI()->Talk(SAY_ICECROWN_ROLEPLAY_1);

                                if (Creature* lichKing = me->FindNearestCreature(NPC_THE_LICH_KING, 50.0f))
                                    lichKing->CastSpell(bolvar, SPELL_DEATHGRASP);
                            }
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_2:
                            if (Creature* lichKing = me->FindNearestCreature(NPC_THE_LICH_KING, 50.0f))
                                lichKing->AI()->Talk(SAY_ICECROWN_ROLEPLAY_2);
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_3:
                            if (Creature* bolvar = me->FindNearestCreature(NPC_IMMOLATED_CHAMPION, 50.0f))
                                bolvar->AI()->Talk(SAY_ICECROWN_ROLEPLAY_3);
                            if (Creature* saurfang = me->FindNearestCreature(NPC_TURNED_CHAMPION, 50.0f))
                                saurfang->AI()->DoAction(ACTION_START_ROLEPLAY);
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_4:
                            if (Creature* lichKing = me->FindNearestCreature(NPC_THE_LICH_KING, 50.0f))
                                lichKing->AI()->Talk(SAY_ICECROWN_ROLEPLAY_4);
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_5:
                            Talk(SAY_ICECROWN_ROLEPLAY_5);
                            break;
                        case EVENT_ICECROWN_ROLEPLAY_6:
                            Talk(SAY_ICECROWN_ROLEPLAY_6);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_4:
                            Talk(SAY_STORMWIND_ROLEPLAY_4);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_5:
                            if (Creature* llane = me->FindNearestCreature(NPC_KING_LLANE, 50.0f))
                                llane->AI()->Talk(SAY_STORMWIND_ROLEPLAY_5);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_7:
                            Talk(SAY_STORMWIND_ROLEPLAY_7);
                            break;
                        default:
                            break;
                    }
                }
            }

        private:
            EventMap _events;
            InstanceScript* _instance;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_yogg_saron_illusionsAI>(creature);
        }
};

class npc_garona : public CreatureScript
{
    public:
        npc_garona() : CreatureScript("npc_garona") { }

        struct npc_garonaAI : public ScriptedAI
        {
            npc_garonaAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                _events.Reset();

                me->SetWalk(true);
                me->GetMotionMaster()->MovePoint(0, IllusionsMiscPos[0]);

                _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_1, 9250ms);
                _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_2, 16700ms);
                _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_3, 24150ms);
                _events.ScheduleEvent(EVENT_STORMWIND_ROLEPLAY_6, 52700ms);
            }

            void UpdateAI(uint32 diff) override
            {
                _events.Update(diff);

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_STORMWIND_ROLEPLAY_1:
                            Talk(SAY_STORMWIND_ROLEPLAY_1);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_2:
                            Talk(SAY_STORMWIND_ROLEPLAY_2);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_3:
                            Talk(SAY_STORMWIND_ROLEPLAY_3);
                            break;
                        case EVENT_STORMWIND_ROLEPLAY_6:
                            Talk(SAY_STORMWIND_ROLEPLAY_6);
                            if (Creature* llane = me->FindNearestCreature(NPC_KING_LLANE, 50.0f))
                            {
                                DoCast(SPELL_ASSASSINATE);
                                llane->CastSpell(llane, SPELL_PERMANENT_FEIGN_DEATH);
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_garonaAI>(creature);
        }
};

class npc_turned_champion : public CreatureScript
{
    public:
        npc_turned_champion() : CreatureScript("npc_turned_champion") { }

        struct npc_turned_championAI : public ScriptedAI
        {
            npc_turned_championAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                DoCast(SPELL_VERTEX_COLOR_BLACK);
            }

            void MovementInform(uint32 type, uint32 pointId) override
            {
                if (type != POINT_MOTION_TYPE || pointId != 0)
                    return;

                me->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
            }

            void DoAction(int32 action) override
            {
                if (action != ACTION_START_ROLEPLAY)
                    return;

                me->SetWalk(true);
                me->GetMotionMaster()->MovePoint(0, IllusionsMiscPos[1]);
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_turned_championAI>(creature);
        }
};

class npc_laughing_skull : public CreatureScript
{
    public:
        npc_laughing_skull() : CreatureScript("npc_laughing_skull") { }

        struct npc_laughing_skullAI : public ScriptedAI
        {
            npc_laughing_skullAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                me->SetReactState(REACT_PASSIVE);
                DoCast(me, SPELL_LUNATIC_GAZE_SKULL);
            }

            // don't evade, otherwise the Lunatic Gaze aura is removed
            void UpdateAI(uint32 /*diff*/) override { }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<npc_laughing_skullAI>(creature);
        }
};

/* 63744 - Sara's Anger
   63745 - Sara's Blessing
   63747 - Sara's Fervor
   65206 - Destabilization Matrix */
class spell_yogg_saron_target_selectors : public SpellScriptLoader    // 63744, 63745, 63747, 65206
{
    public:
        spell_yogg_saron_target_selectors() : SpellScriptLoader("spell_yogg_saron_target_selectors") { }

        class spell_yogg_saron_target_selectors_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_target_selectors_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    GetCaster()->CastSpell(target, uint32(GetEffectValue()));
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_target_selectors_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_target_selectors_SpellScript();
        }
};

class SanityReduction : public SpellScript
{
    public:
        SanityReduction() : SpellScript(), _stacks(0) { }
        SanityReduction(uint8 stacks) : SpellScript(), _stacks(stacks) { }

    void RemoveSanity(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (Aura* sanity = target->GetAura(SPELL_SANITY))
                sanity->ModStackAmount(-int32(_stacks), AURA_REMOVE_BY_ENEMY_SPELL);
    }

    protected:
        uint8 _stacks;
};

class HighSanityTargetSelector
{
    public:
        HighSanityTargetSelector() { }

        bool operator()(WorldObject* object)
        {
            if (Unit* unit = object->ToUnit())
                if (Aura* sanity = unit->GetAura(SPELL_SANITY))
                    return sanity->GetStackAmount() <= 40;
            return true;
        }
};

// 63795, 65301 - Psychosis
class spell_yogg_saron_psychosis : public SpellScriptLoader      // 63795, 65301
{
    public:
        spell_yogg_saron_psychosis() : SpellScriptLoader("spell_yogg_saron_psychosis") { }

        class spell_yogg_saron_psychosis_SpellScript : public SanityReduction
        {
            PrepareSpellScript(spell_yogg_saron_psychosis_SpellScript);

            bool Load() override
            {
                _stacks = GetSpellInfo()->Id == SPELL_PSYCHOSIS ? 9 : 12;
                return true;
            }

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(HighSanityTargetSelector());
                targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_ILLUSION_ROOM));
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_psychosis_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_psychosis_SpellScript::FilterTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_psychosis_SpellScript::RemoveSanity, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_psychosis_SpellScript();
        }
};

// 63830, 63881 - Malady of the Mind
class spell_yogg_saron_malady_of_the_mind : public SpellScriptLoader    // 63830, 63881
{
    public:
        spell_yogg_saron_malady_of_the_mind() : SpellScriptLoader("spell_yogg_saron_malady_of_the_mind") { }

        class spell_yogg_saron_malady_of_the_mind_SpellScript : public SanityReduction
        {
            public:
                spell_yogg_saron_malady_of_the_mind_SpellScript() : SanityReduction(3) { }

            PrepareSpellScript(spell_yogg_saron_malady_of_the_mind_SpellScript);

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(HighSanityTargetSelector());
                targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_ILLUSION_ROOM));
            }

            void Register() override
            {
                if (m_scriptSpellId == SPELL_MALADY_OF_THE_MIND)
                {
                    OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_malady_of_the_mind_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                    OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_malady_of_the_mind_SpellScript::FilterTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
                    OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_malady_of_the_mind_SpellScript::FilterTargets, EFFECT_2, TARGET_UNIT_SRC_AREA_ENEMY);
                }

                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_malady_of_the_mind_SpellScript::RemoveSanity, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        class spell_yogg_saron_malady_of_the_mind_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_malady_of_the_mind_AuraScript);

            bool Validate(SpellInfo const* /*spell*/) override
            {
                return ValidateSpellInfo({ SPELL_MALADY_OF_THE_MIND_JUMP });
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                switch (GetTargetApplication()->GetRemoveMode())
                {
                    case AURA_REMOVE_BY_ENEMY_SPELL:
                    case AURA_REMOVE_BY_EXPIRE:
                    case AURA_REMOVE_BY_DEATH:
                        break;
                    default:
                        return;
                }

                GetTarget()->CastSpell(GetTarget(), SPELL_MALADY_OF_THE_MIND_JUMP);
            }

            void Register() override
            {
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_malady_of_the_mind_AuraScript::OnRemove, EFFECT_1, SPELL_AURA_MOD_FEAR, AURA_EFFECT_HANDLE_REAL);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_malady_of_the_mind_SpellScript();
        }

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_malady_of_the_mind_AuraScript();
        }
};

// 63802 - Brain Link
class spell_yogg_saron_brain_link : public SpellScriptLoader    // 63802
{
    public:
        spell_yogg_saron_brain_link() : SpellScriptLoader("spell_yogg_saron_brain_link") { }

        class spell_yogg_saron_brain_link_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_brain_link_SpellScript);

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_ILLUSION_ROOM));

                if (targets.size() != 2)
                {
                    targets.clear();
                    return;
                }

                if (SaraAI* ai = CAST_AI(SaraAI, GetCaster()->GetAI()))
                    ai->SetLinkBetween(targets.front()->GetGUID(), targets.back()->GetGUID());
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_brain_link_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
            }
        };

        class spell_yogg_saron_brain_link_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_brain_link_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_BRAIN_LINK_DAMAGE, SPELL_BRAIN_LINK_NO_DAMAGE });
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                Unit* caster = GetCaster();
                if (!caster)
                    return;

                if (SaraAI* ai = CAST_AI(SaraAI, caster->GetAI()))
                {
                    if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
                        ai->RemoveLinkFrom(GetTarget()->GetGUID());
                    else
                    {
                        if (Player* player = ObjectAccessor::GetPlayer(*GetTarget(), ai->GetLinkedPlayerGUID(GetTarget()->GetGUID())))
                        {
                            ai->RemoveLinkFrom(GetTarget()->GetGUID());
                            player->RemoveAurasDueToSpell(SPELL_BRAIN_LINK);
                        }
                    }
                }
            }

            void DummyTick(AuraEffect const* aurEff)
            {
                Unit* caster = GetCaster();
                if (!caster)
                    return;

                SaraAI* ai = CAST_AI(SaraAI, caster->GetAI());
                if (!ai)
                    return;

                Player* linked = ObjectAccessor::GetPlayer(*GetTarget(), ai->GetLinkedPlayerGUID(GetTarget()->GetGUID()));
                if (!linked)
                    return;

                GetTarget()->CastSpell(linked, (GetTarget()->GetDistance(linked) > (float)aurEff->GetAmount()) ? SPELL_BRAIN_LINK_DAMAGE : SPELL_BRAIN_LINK_NO_DAMAGE, true);
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_yogg_saron_brain_link_AuraScript::DummyTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
                OnEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_brain_link_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_brain_link_SpellScript();
        }

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_brain_link_AuraScript();
        }
};

// 63803 - Brain Link (Damage)
class spell_yogg_saron_brain_link_damage : public SpellScriptLoader      // 63803
{
    public:
        spell_yogg_saron_brain_link_damage() : SpellScriptLoader("spell_yogg_saron_brain_link_damage") { }

        class spell_yogg_saron_brain_link_damage_SpellScript : public SanityReduction
        {
            public:
                spell_yogg_saron_brain_link_damage_SpellScript() : SanityReduction(2) { }

            PrepareSpellScript(spell_yogg_saron_brain_link_damage_SpellScript);

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_brain_link_damage_SpellScript::RemoveSanity, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_brain_link_damage_SpellScript();
        }
};

// 63030 - Boil Ominously
class spell_yogg_saron_boil_ominously : public SpellScriptLoader    // 63030
{
    public:
        spell_yogg_saron_boil_ominously() : SpellScriptLoader("spell_yogg_saron_boil_ominously") { }

        class spell_yogg_saron_boil_ominously_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_boil_ominously_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_SUMMON_GUARDIAN_1 });
            }

            void HandleDummy(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    if (!target->HasAura(SPELL_FLASH_FREEZE) && !GetCaster()->HasAura(SPELL_SUMMON_GUARDIAN_1) && !GetCaster()->HasAura(SPELL_SUMMON_GUARDIAN_2))
                    {
                        if (Creature* caster = GetCaster()->ToCreature())
                            caster->AI()->Talk(EMOTE_OMINOUS_CLOUD_PLAYER_TOUCH, target);

                        GetCaster()->CastSpell(GetCaster(), SPELL_SUMMON_GUARDIAN_1, true);
                    }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_boil_ominously_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_boil_ominously_SpellScript();
        }
};

// 64465 - Shadow Beacon
class spell_yogg_saron_shadow_beacon : public SpellScriptLoader     // 64465
{
    public:
        spell_yogg_saron_shadow_beacon() : SpellScriptLoader("spell_yogg_saron_shadow_beacon") { }

        class spell_yogg_saron_shadow_beacon_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_shadow_beacon_AuraScript);

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Creature* target = GetTarget()->ToCreature())
                    target->SetEntry(NPC_MARKED_IMMORTAL_GUARDIAN);
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Creature* target = GetTarget()->ToCreature())
                    target->SetEntry(NPC_IMMORTAL_GUARDIAN);
            }

            void Register() override
            {
                AfterEffectApply += AuraEffectApplyFn(spell_yogg_saron_shadow_beacon_AuraScript::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_shadow_beacon_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_shadow_beacon_AuraScript();
        }
};

// 64466 - Empowering Shadows
class spell_yogg_saron_empowering_shadows_range_check : public SpellScriptLoader    // 64466
{
    public:
        spell_yogg_saron_empowering_shadows_range_check() : SpellScriptLoader("spell_yogg_saron_empowering_shadows_range_check") { }

        class spell_yogg_saron_empowering_shadows_range_check_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_empowering_shadows_range_check_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    target->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_empowering_shadows_range_check_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_empowering_shadows_range_check_SpellScript();
        }
};

// 64467 - Empowering Shadows
class spell_yogg_saron_empowering_shadows_missile : public SpellScriptLoader    // 64467
{
    public:
        spell_yogg_saron_empowering_shadows_missile() : SpellScriptLoader("spell_yogg_saron_empowering_shadows_missile") { }

        class spell_yogg_saron_empowering_shadows_missile_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_empowering_shadows_missile_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_EMPOWERING_SHADOWS });
            }

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    target->CastSpell(nullptr, SPELL_EMPOWERING_SHADOWS, true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_empowering_shadows_missile_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_empowering_shadows_missile_SpellScript();
        }
};

// it works, but is it scripted correctly? why is it aura with 2500ms duration?
// 64132 - Constrictor Tentacle
class spell_yogg_saron_constrictor_tentacle : public SpellScriptLoader     // 64132
{
    public:
        spell_yogg_saron_constrictor_tentacle() : SpellScriptLoader("spell_yogg_saron_constrictor_tentacle") { }

        class spell_yogg_saron_constrictor_tentacle_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_constrictor_tentacle_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_CONSTRICTOR_TENTACLE_SUMMON });
            }

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                GetTarget()->CastSpell(GetTarget(), SPELL_CONSTRICTOR_TENTACLE_SUMMON);
            }

            void Register() override
            {
                AfterEffectApply += AuraEffectApplyFn(spell_yogg_saron_constrictor_tentacle_AuraScript::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_constrictor_tentacle_AuraScript();
        }
};

// 64131 - Lunge
class spell_yogg_saron_lunge : public SpellScriptLoader    // 64131
{
    public:
        spell_yogg_saron_lunge() : SpellScriptLoader("spell_yogg_saron_lunge") { }

        class spell_yogg_saron_lunge_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_lunge_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_SQUEEZE });
            }

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                {
                    target->CastSpell(target, SPELL_SQUEEZE, true);
                    target->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
                }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_lunge_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_lunge_SpellScript();
        }
};

// 64125, 64126 - Squeeze
class spell_yogg_saron_squeeze : public SpellScriptLoader     // 64125, 64126
{
    public:
        spell_yogg_saron_squeeze() : SpellScriptLoader("spell_yogg_saron_squeeze") { }

        class spell_yogg_saron_squeeze_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_squeeze_AuraScript);

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Unit* vehicle = GetTarget()->GetVehicleBase())
                    if (vehicle->IsAlive())
                        vehicle->KillSelf(); // should tentacle die or just release its target?
            }

            void Register() override
            {
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_squeeze_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_squeeze_AuraScript();
        }
};

// 64148 - Diminsh Power
class spell_yogg_saron_diminsh_power : public SpellScriptLoader     // 64148
{
    public:
        spell_yogg_saron_diminsh_power() : SpellScriptLoader("spell_yogg_saron_diminsh_power") { }

        class spell_yogg_saron_diminsh_power_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_diminsh_power_AuraScript);

            void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
            {
                PreventDefaultAction();
                if (Spell* spell = GetTarget()->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                    if (spell->getState() == SPELL_STATE_CASTING)
                        GetTarget()->InterruptSpell(CURRENT_CHANNELED_SPELL);
            }

            void Register() override
            {
                OnEffectProc += AuraEffectProcFn(spell_yogg_saron_diminsh_power_AuraScript::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_diminsh_power_AuraScript();
        }
};

// not sure about SPELL_WEAKENED part, where should it be handled?
// 64161 - Empowered
class spell_yogg_saron_empowered : public SpellScriptLoader     // 64161
{
    public:
        spell_yogg_saron_empowered() : SpellScriptLoader("spell_yogg_saron_empowered") { }

        class spell_yogg_saron_empowered_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_empowered_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_EMPOWERED_BUFF, SPELL_WEAKENED });
            }

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
                args.AddSpellMod(SPELLVALUE_AURA_STACK, 9);
                GetTarget()->CastSpell(GetTarget(), SPELL_EMPOWERED_BUFF, args);
            }

            void OnPeriodic(AuraEffect const* /*aurEff*/)
            {
                Unit* target = GetTarget();
                float stack = std::ceil((target->GetHealthPct() / 10) - 1);
                target->RemoveAurasDueToSpell(SPELL_EMPOWERED_BUFF);

                if (stack)
                {
                    target->RemoveAurasDueToSpell(SPELL_WEAKENED);
                    CastSpellExtraArgs args(TRIGGERED_FULL_MASK);
                    args.AddSpellMod(SPELLVALUE_AURA_STACK, stack);
                    target->CastSpell(target, SPELL_EMPOWERED_BUFF, args);
                }
                else if (!target->HealthAbovePct(1) && !target->HasAura(SPELL_WEAKENED))
                    target->CastSpell(target, SPELL_WEAKENED, true);
            }

            void Register() override
            {
                AfterEffectApply += AuraEffectApplyFn(spell_yogg_saron_empowered_AuraScript::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_yogg_saron_empowered_AuraScript::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_empowered_AuraScript();
        }
};

// 64069 - Match Health
class spell_yogg_saron_match_health : public SpellScriptLoader    // 64069
{
    public:
        spell_yogg_saron_match_health() : SpellScriptLoader("spell_yogg_saron_match_health") { }

        class spell_yogg_saron_match_health_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_match_health_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    target->SetHealth(target->CountPctFromMaxHealth((int32)GetCaster()->GetHealthPct()));
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_match_health_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_match_health_SpellScript();
        }
};

// 65238 - Shattered Illusion
class spell_yogg_saron_shattered_illusion : public SpellScriptLoader    // 65238
{
    public:
        spell_yogg_saron_shattered_illusion() : SpellScriptLoader("spell_yogg_saron_shattered_illusion") { }

        class spell_yogg_saron_shattered_illusion_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_shattered_illusion_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    target->RemoveAurasDueToSpell(uint32(GetEffectValue()));
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_shattered_illusion_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_shattered_illusion_SpellScript();
        }
};

// 63882 - Death Ray Warning Visual
class spell_yogg_saron_death_ray_warning_visual : public SpellScriptLoader     // 63882
{
    public:
        spell_yogg_saron_death_ray_warning_visual() : SpellScriptLoader("spell_yogg_saron_death_ray_warning_visual") { }

        class spell_yogg_saron_death_ray_warning_visual_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_death_ray_warning_visual_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_DEATH_RAY_PERIODIC, SPELL_DEATH_RAY_DAMAGE_VISUAL });
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Unit* caster = GetCaster())
                {
                    caster->CastSpell(caster, SPELL_DEATH_RAY_PERIODIC, true);
                    caster->CastSpell(nullptr, SPELL_DEATH_RAY_DAMAGE_VISUAL, true);
                    // TODO: set better movement
                    caster->GetMotionMaster()->MoveConfused();
                }
            }

            void Register() override
            {
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_death_ray_warning_visual_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_death_ray_warning_visual_AuraScript();
        }
};

// 63993 - Cancel Illusion Room Aura
class spell_yogg_saron_cancel_illusion_room_aura : public SpellScriptLoader    // 63993
{
    public:
        spell_yogg_saron_cancel_illusion_room_aura() : SpellScriptLoader("spell_yogg_saron_cancel_illusion_room_aura") { }

        class spell_yogg_saron_cancel_illusion_room_aura_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_cancel_illusion_room_aura_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_TELEPORT_BACK_TO_MAIN_ROOM });
            }

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                {
                    target->CastSpell(target, SPELL_TELEPORT_BACK_TO_MAIN_ROOM);
                    target->RemoveAurasDueToSpell(uint32(GetEffectValue()));
                }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_cancel_illusion_room_aura_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_cancel_illusion_room_aura_SpellScript();
        }
};

// 64010, 64013 - Nondescript
class spell_yogg_saron_nondescript : public SpellScriptLoader     // 64010, 64013
{
    public:
        spell_yogg_saron_nondescript() : SpellScriptLoader("spell_yogg_saron_nondescript") { }

        class spell_yogg_saron_nondescript_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_nondescript_AuraScript);

            void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
            {
                GetTarget()->CastSpell(GetTarget(), uint32(aurEff->GetAmount()), true);
            }

            void Register() override
            {
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_nondescript_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_MOD_STUN, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_nondescript_AuraScript();
        }
};

// 64012 - Revealed Tentacle
class spell_yogg_saron_revealed_tentacle : public SpellScriptLoader    // 64012
{
    public:
        spell_yogg_saron_revealed_tentacle() : SpellScriptLoader("spell_yogg_saron_revealed_tentacle") { }

        class spell_yogg_saron_revealed_tentacle_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_revealed_tentacle_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_TENTACLE_VOID_ZONE, SPELL_GRIM_REPRISAL });
            }

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Creature* caster = GetCaster()->ToCreature())
                {
                    caster->CastSpell(caster, SPELL_TENTACLE_VOID_ZONE, true);
                    caster->CastSpell(caster, SPELL_GRIM_REPRISAL, true);
                    caster->UpdateEntry(NPC_INFLUENCE_TENTACLE, caster->GetCreatureData());
                }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_revealed_tentacle_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_revealed_tentacle_SpellScript();
        }
};

// 63305 - Grim Reprisal
class spell_yogg_saron_grim_reprisal : public SpellScriptLoader     // 63305
{
    public:
        spell_yogg_saron_grim_reprisal() : SpellScriptLoader("spell_yogg_saron_grim_reprisal") { }

        class spell_yogg_saron_grim_reprisal_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_grim_reprisal_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_GRIM_REPRISAL_DAMAGE });
            }

            void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
            {
                PreventDefaultAction();
                DamageInfo* damageInfo = eventInfo.GetDamageInfo();
                if (!damageInfo || !damageInfo->GetDamage())
                    return;

                CastSpellExtraArgs args(aurEff);
                args.AddSpellBP0(CalculatePct(damageInfo->GetDamage(), 60));
                GetTarget()->CastSpell(damageInfo->GetAttacker(), SPELL_GRIM_REPRISAL_DAMAGE, args);
            }

            void Register() override
            {
                OnEffectProc += AuraEffectProcFn(spell_yogg_saron_grim_reprisal_AuraScript::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_grim_reprisal_AuraScript();
        }
};

// 64059 - Induce Madness
class spell_yogg_saron_induce_madness : public SpellScriptLoader    // 64059
{
    public:
        spell_yogg_saron_induce_madness() : SpellScriptLoader("spell_yogg_saron_induce_madness") { }

        class spell_yogg_saron_induce_madness_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_induce_madness_SpellScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_TELEPORT_BACK_TO_MAIN_ROOM, SPELL_SHATTERED_ILLUSION_REMOVE });
            }

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                {
                    target->CastSpell(target, SPELL_TELEPORT_BACK_TO_MAIN_ROOM);
                    target->RemoveAurasDueToSpell(SPELL_SANITY, ObjectGuid::Empty, 0, AURA_REMOVE_BY_ENEMY_SPELL);
                    target->RemoveAurasDueToSpell(uint32(GetEffectValue()));
                }
            }

            void ClearShatteredIllusion()
            {
                GetCaster()->CastSpell(nullptr, SPELL_SHATTERED_ILLUSION_REMOVE);

                if (InstanceScript* instance = GetCaster()->GetInstanceScript())
                    if (Creature* voice = instance->GetCreature(DATA_VOICE_OF_YOGG_SARON))
                        voice->AI()->DoAction(ACTION_TOGGLE_SHATTERED_ILLUSION);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_induce_madness_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
                AfterCast += SpellCastFn(spell_yogg_saron_induce_madness_SpellScript::ClearShatteredIllusion);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_induce_madness_SpellScript();
        }
};

// 63050 - Sanity
class spell_yogg_saron_sanity : public SpellScriptLoader     // 63050
{
    public:
        spell_yogg_saron_sanity() : SpellScriptLoader("spell_yogg_saron_sanity") { }

        class spell_yogg_saron_sanity_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_sanity_SpellScript);

            // don't target players outside of room or handle it in SPELL_INSANE_PERIODIC?

            void ModSanityStacks()
            {
                GetSpell()->SetSpellValue(SPELLVALUE_AURA_STACK, 100);
            }

            void Register() override
            {
                BeforeCast += SpellCastFn(spell_yogg_saron_sanity_SpellScript::ModSanityStacks);
            }
        };

        class spell_yogg_saron_sanity_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_sanity_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_LOW_SANITY_SCREEN_EFFECT, SPELL_INSANE });
            }

            void DummyTick(AuraEffect const* /*aurEff*/)
            {
                if (GetTarget()->HasAura(SPELL_SANITY_WELL))
                    ModStackAmount(20);

                if (GetStackAmount() <= 40 && !GetTarget()->HasAura(SPELL_LOW_SANITY_SCREEN_EFFECT))
                    GetTarget()->CastSpell(GetTarget(), SPELL_LOW_SANITY_SCREEN_EFFECT, true);
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_ENEMY_SPELL)
                    return;

                if (InstanceScript* instance = GetTarget()->GetInstanceScript())
                    instance->SetData(DATA_DRIVE_ME_CRAZY, uint32(false));

                GetTarget()->RemoveAurasDueToSpell(SPELL_BRAIN_LINK);

                if (Unit* caster = GetCaster())
                    caster->CastSpell(GetTarget(), SPELL_INSANE, true);
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_yogg_saron_sanity_AuraScript::DummyTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_sanity_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_sanity_SpellScript();
        }

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_sanity_AuraScript();
        }
};

// 63120 - Insane
class spell_yogg_saron_insane : public SpellScriptLoader     // 63120
{
    public:
        spell_yogg_saron_insane() : SpellScriptLoader("spell_yogg_saron_insane") { }

        class spell_yogg_saron_insane_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_insane_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_INSANE_VISUAL });
            }

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Unit* caster = GetCaster())
                    if (Creature* yogg = caster->ToCreature())
                        yogg->AI()->Talk(WHISPER_VOICE_INSANE, GetTarget());

                GetTarget()->CastSpell(GetTarget(), SPELL_INSANE_VISUAL, true);
            }

            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (GetTarget()->IsAlive())
                    GetTarget()->KillSelf();
            }

            void Register() override
            {
                AfterEffectApply += AuraEffectApplyFn(spell_yogg_saron_insane_AuraScript::OnApply, EFFECT_0, SPELL_AURA_AOE_CHARM, AURA_EFFECT_HANDLE_REAL);
                AfterEffectRemove += AuraEffectRemoveFn(spell_yogg_saron_insane_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_AOE_CHARM, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_insane_AuraScript();
        }
};

// 64555 - Insane Periodic
class spell_yogg_saron_insane_periodic : public SpellScriptLoader    // 64555
{
    public:
        spell_yogg_saron_insane_periodic() : SpellScriptLoader("spell_yogg_saron_insane_periodic") { }

        class spell_yogg_saron_insane_periodic_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_insane_periodic_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    GetCaster()->CastSpell(target, uint32(GetEffectValue()), true);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_insane_periodic_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_insane_periodic_SpellScript();
        }
};

class LunaticGazeTargetSelector
{
    public:
        LunaticGazeTargetSelector(Unit* caster) : _caster(caster) { }

        bool operator()(WorldObject* object)
        {
            return !object->HasInArc(static_cast<float>(M_PI), _caster);
        }

    private:
        Unit* _caster;
};

// 64164, 64168 - Lunatic Gaze
class spell_yogg_saron_lunatic_gaze : public SpellScriptLoader      // 64164, 64168
{
    public:
        spell_yogg_saron_lunatic_gaze() : SpellScriptLoader("spell_yogg_saron_lunatic_gaze") { }

        class spell_yogg_saron_lunatic_gaze_SpellScript : public SanityReduction
        {
            PrepareSpellScript(spell_yogg_saron_lunatic_gaze_SpellScript);

            bool Load() override
            {
                _stacks = GetSpellInfo()->Id == SPELL_LUNATIC_GAZE_DAMAGE ? 4 : 2;
                return true;
            }

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(LunaticGazeTargetSelector(GetCaster()));
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_lunatic_gaze_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_yogg_saron_lunatic_gaze_SpellScript::FilterTargets, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_lunatic_gaze_SpellScript::RemoveSanity, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_lunatic_gaze_SpellScript();
        }
};

/* 62650 - Fortitude of Frost
   62670 - Resilience of Nature
   62671 - Speed of Invention
   62702 - Fury of the Storm */
class spell_yogg_saron_keeper_aura : public SpellScriptLoader     // 62650, 62670, 62671, 62702
{
    public:
        spell_yogg_saron_keeper_aura() : SpellScriptLoader("spell_yogg_saron_keeper_aura") { }

        class spell_yogg_saron_keeper_aura_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_keeper_aura_AuraScript);

            bool CanApply(Unit* target)
            {
                if (target->GetTypeId() != TYPEID_PLAYER && target != GetCaster())
                    return false;
                return true;
            }

            void Register() override
            {
                DoCheckAreaTarget += AuraCheckAreaTargetFn(spell_yogg_saron_keeper_aura_AuraScript::CanApply);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_keeper_aura_AuraScript();
        }
};

// 64184 - In the Maws of the Old God
class spell_yogg_saron_in_the_maws_of_the_old_god : public SpellScriptLoader    // 64184
{
    public:
        spell_yogg_saron_in_the_maws_of_the_old_god() : SpellScriptLoader("spell_yogg_saron_in_the_maws_of_the_old_god") { }

        class spell_yogg_saron_in_the_maws_of_the_old_god_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_in_the_maws_of_the_old_god_SpellScript);

            SpellCastResult CheckRequirement()
            {
                if (InstanceScript* instance = GetCaster()->GetInstanceScript())
                {
                    if (Creature* yogg = instance->GetCreature(DATA_YOGG_SARON))
                    {
                        if (yogg->FindCurrentSpellBySpellId(SPELL_DEAFENING_ROAR))
                        {
                            if (GetCaster()->GetDistance(yogg) > 20.0f)
                                return SPELL_FAILED_OUT_OF_RANGE;
                            else
                                return SPELL_CAST_OK;
                        }
                    }
                }

                return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;
            }

            void Register() override
            {
                OnCheckCast += SpellCheckCastFn(spell_yogg_saron_in_the_maws_of_the_old_god_SpellScript::CheckRequirement);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_in_the_maws_of_the_old_god_SpellScript();
        }
};

// 64172 - Titanic Storm
class spell_yogg_saron_titanic_storm : public SpellScriptLoader    // 64172
{
    public:
        spell_yogg_saron_titanic_storm() : SpellScriptLoader("spell_yogg_saron_titanic_storm") { }

        class spell_yogg_saron_titanic_storm_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_yogg_saron_titanic_storm_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    Unit::Kill(GetCaster(), target);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_yogg_saron_titanic_storm_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_yogg_saron_titanic_storm_SpellScript();
        }
};

// 64174 - Hodir's Protective Gaze
class spell_yogg_saron_hodirs_protective_gaze : public SpellScriptLoader     // 64174
{
    public:
        spell_yogg_saron_hodirs_protective_gaze() : SpellScriptLoader("spell_yogg_saron_hodirs_protective_gaze") { }

        class spell_yogg_saron_hodirs_protective_gaze_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_yogg_saron_hodirs_protective_gaze_AuraScript);

            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_FLASH_FREEZE });
            }

            bool CanApply(Unit* target)
            {
                if (target->GetTypeId() != TYPEID_PLAYER && target != GetCaster())
                    return false;
                return true;
            }

            void OnAbsorb(AuraEffect* /*aurEff*/, DamageInfo& dmgInfo, uint32& absorbAmount)
            {
                if (dmgInfo.GetDamage() >= GetTarget()->GetHealth())
                {
                    absorbAmount = dmgInfo.GetDamage();
                    // or absorbAmount = dmgInfo.GetDamage() - GetTarget()->GetHealth() + 1
                    GetTarget()->CastSpell(GetTarget(), SPELL_FLASH_FREEZE, true);
                }
                else
                    PreventDefaultAction();
            }

            void Register() override
            {
                DoCheckAreaTarget += AuraCheckAreaTargetFn(spell_yogg_saron_hodirs_protective_gaze_AuraScript::CanApply);
                OnEffectAbsorb += AuraEffectAbsorbFn(spell_yogg_saron_hodirs_protective_gaze_AuraScript::OnAbsorb, EFFECT_0);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_yogg_saron_hodirs_protective_gaze_AuraScript();
        }
};

/**
 * @brief 注册尤格-萨隆相关脚本
 *
 * 此函数是脚本系统的入口点，负责注册本文件中所有AI和法术脚本。
 * 包括：
 * - Boss AI：尤格-萨隆之声、萨拉、尤格-萨隆、大脑
 * - NPC AI：各种触手、守护者、幻象NPC等
 * - 法术脚本：理智系统、大脑连接、各种技能效果等
 *
 * @调用时机 服务器启动时，由脚本加载系统调用
 */
void AddSC_boss_yogg_saron()
{
    // Boss AI
    new boss_voice_of_yogg_saron();     // 尤格-萨隆之声（核心控制器）
    new boss_sara();                    // 萨拉（第一阶段Boss）
    new boss_yogg_saron();              // 尤格-萨隆本体
    new boss_brain_of_yogg_saron();     // 尤格-萨隆大脑

    // NPC AI
    new npc_ominous_cloud();            // 不祥之云
    new npc_guardian_of_yogg_saron();   // 尤格-萨隆守护者
    new npc_corruptor_tentacle();       // 腐化触手
    new npc_constrictor_tentacle();     // 缠绕触手
    new npc_crusher_tentacle();         // 粉碎触手
    new npc_influence_tentacle();       // 影响触手（幻象房间中的目标）
    new npc_descend_into_madness();     // 陷入疯狂（传送门）
    new npc_immortal_guardian();        // 不朽守护者
    new npc_observation_ring_keeper();  // 观察环上的守护者
    new npc_yogg_saron_keeper();        // 战斗区域内的守护者
    new npc_yogg_saron_illusions();     // 尤格-萨隆幻象
    new npc_garona();                   // 迦罗娜（暴风城幻象）
    new npc_turned_champion();          // 转化的勇士（冰冠幻象）
    new npc_laughing_skull();           // 大笑骷髅

    // 法术脚本
    new spell_yogg_saron_target_selectors();            // 目标选择器
    new spell_yogg_saron_psychosis();                   // 精神病
    new spell_yogg_saron_malady_of_the_mind();          // 心灵疾病
    new spell_yogg_saron_brain_link();                  // 大脑连接
    new spell_yogg_saron_brain_link_damage();           // 大脑连接伤害
    new spell_yogg_saron_boil_ominously();              // 不祥沸腾
    new spell_yogg_saron_shadow_beacon();               // 暗影信标
    new spell_yogg_saron_empowering_shadows_range_check();  // 赋能之影范围检查
    new spell_yogg_saron_empowering_shadows_missile();  // 赋能之影飞弹
    new spell_yogg_saron_constrictor_tentacle();        // 缠绕触手召唤
    new spell_yogg_saron_lunge();                       // 猛扑
    new spell_yogg_saron_squeeze();                     // 挤压
    new spell_yogg_saron_diminsh_power();               // 削弱力量
    new spell_yogg_saron_empowered();                   // 强化
    new spell_yogg_saron_match_health();                // 匹配生命值
    new spell_yogg_saron_shattered_illusion();          // 破碎幻象
    new spell_yogg_saron_death_ray_warning_visual();    // 死亡射线警告视觉效果
    new spell_yogg_saron_cancel_illusion_room_aura();   // 取消幻象房间光环
    new spell_yogg_saron_nondescript();                 // 无描述效果
    new spell_yogg_saron_revealed_tentacle();           // 揭示触手
    new spell_yogg_saron_grim_reprisal();               // 无情报复
    new spell_yogg_saron_induce_madness();              // 诱导疯狂
    new spell_yogg_saron_sanity();                      // 理智值
    new spell_yogg_saron_insane();                      // 疯狂
    new spell_yogg_saron_insane_periodic();             // 疯狂周期性
    new spell_yogg_saron_lunatic_gaze();                // 疯狂凝视
    new spell_yogg_saron_keeper_aura();                 // 守护者光环
    new spell_yogg_saron_in_the_maws_of_the_old_god();  // 在古神之口中
    new spell_yogg_saron_titanic_storm();               // 泰坦风暴
    new spell_yogg_saron_hodirs_protective_gaze();      // 霍迪尔的保护凝视
}
