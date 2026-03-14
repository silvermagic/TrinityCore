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
 * @file boss_kaelthas.cpp
 * @brief 风暴要塞-风暴之眼副本Boss凯尔萨斯·逐日者及其顾问团AI实现
 *
 * 本模块实现了凯尔萨斯Boss战斗的完整逻辑，包括:
 * - 凯尔萨斯主Boss AI (5阶段战斗)
 * - 四位顾问AI (塔隆血魔、萨拉雷恩、卡珀尼安、泰隆尼库斯)
 * - 凤凰及凤凰蛋NPC AI
 * - 火焰冲击NPC AI
 * - 重力 lapse、召唤武器等法术脚本
 *
 * 战斗阶段说明:
 * - 第1阶段: 依次激活四位顾问进行战斗
 * - 第2阶段: 召唤并战斗七把传说武器
 * - 第3阶段: 复活四位顾问同时战斗
 * - 第4阶段: 凯尔萨斯亲自参战
 * - 第5阶段: 凯尔萨斯获得完全力量，使用重力失效技能
 *
 * @see https://wowpedia.fandom.com/wiki/Kael%27thas_Sunstrider_(tactics)
 */

/* ScriptData
SDName: Boss_Kaelthas
SD%Complete: 60
SDComment: SQL, weapon scripts, mind control, need correct spells(interruptible/uninterruptible), phoenix spawn location & animation, phoenix behaviour & spawn during gravity lapse
SDCategory: Tempest Keep, The Eye
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "the_eye.h"

/**
 * @enum Yells
 * @brief 所有NPC的台词和表情枚举定义
 *
 * 包含凯尔萨斯及其四位顾问的所有战斗台词ID
 */
enum Yells
{
    // 凯尔萨斯台词
    SAY_INTRO                                  = 0,  ///< 开场白
    SAY_INTRO_CAPERNIAN                        = 1,  ///< 介绍顾问卡珀尼安
    SAY_INTRO_TELONICUS                        = 2,  ///< 介绍顾问泰隆尼库斯
    SAY_INTRO_THALADRED                        = 3,  ///< 介绍顾问塔隆血魔
    SAY_INTRO_SANGUINAR                        = 4,  ///< 介绍顾问萨拉雷恩
    SAY_PHASE2_WEAPON                          = 5,  ///< 第2阶段召唤武器台词
    SAY_PHASE3_ADVANCE                         = 6,  ///< 第3阶段复活顾问台词
    SAY_PHASE4_INTRO2                          = 7,  ///< 第4阶段亲自参战台词
    SAY_PHASE5_NUTS                            = 8,  ///< 第5阶段变身台词
    SAY_SLAY                                   = 9,  ///< 击杀玩家台词
    SAY_MIND_CONTROL                           = 10, ///< 精神控制台词
    SAY_GRAVITY_LAPSE                          = 11, ///< 重力失效台词
    SAY_SUMMON_PHOENIX                         = 12, ///< 召唤凤凰台词
    SAY_DEATH                                  = 13, ///< 死亡台词
    EMOTE_PYROBLAST                            = 14, ///< 炎爆术表情提示

    // 塔隆血魔(暗影行者)台词
    SAY_THALADRED_AGGRO                        = 0,  ///< 激活台词
    SAY_THALADRED_DEATH                        = 1,  ///< 死亡台词
    EMOTE_THALADRED_GAZE                       = 2,  ///< 凝视目标表情

    // 萨拉雷恩领主台词
    SAY_SANGUINAR_AGGRO                        = 0,  ///< 激活台词
    SAY_SANGUINAR_DEATH                        = 1,  ///< 死亡台词

    // 大星术师卡珀尼安台词
    SAY_CAPERNIAN_AGGRO                        = 0,  ///< 激活台词
    SAY_CAPERNIAN_DEATH                        = 1,  ///< 死亡台词

    // 工程大师泰隆尼库斯台词
    SAY_TELONICUS_AGGRO                        = 0,  ///< 激活台词
    SAY_TELONICUS_DEATH                        = 1   ///< 死亡台词
};

/**
 * @enum Spells
 * @brief 所有Boss和相关NPC使用的法术ID枚举
 *
 * 按战斗阶段和NPC分类组织法术ID
 */
enum Spells
{
    // 第2阶段法术 - 召唤武器
    SPELL_SUMMON_WEAPONS                        = 36976, ///< 召唤武器总法术
    SPELL_SUMMON_WEAPONA                        = 36958, ///< 召唤武器A
    SPELL_SUMMON_WEAPONB                        = 36959, ///< 召唤武器B
    SPELL_SUMMON_WEAPONC                        = 36960, ///< 召唤武器C
    SPELL_SUMMON_WEAPOND                        = 36961, ///< 召唤武器D
    SPELL_SUMMON_WEAPONE                        = 36962, ///< 召唤武器E
    SPELL_SUMMON_WEAPONF                        = 36963, ///< 召唤武器F
    SPELL_SUMMON_WEAPONG                        = 36964, ///< 召唤武器G
    SPELL_RESSURECTION                          = 36450, ///< 复活顾问

    // 第4阶段法术 - 凯尔萨斯参战技能
    SPELL_FIREBALL                              = 36805, ///< 火球术 - 主要输出技能
    SPELL_PYROBLAST                             = 36819, ///< 炎爆术 - 高伤害技能
    SPELL_SUMMON_FLAME_STRIKE                   = 36735, ///< 召唤火焰冲击
    SPELL_ARCANE_DISRUPTION                     = 36834, ///< 奥术扰乱
    SPELL_SHOCK_BARRIER                         = 36815, ///< 震荡屏障 - 护盾技能
    SPELL_PHOENIX_ANIMATION                     = 36723, ///< 凤凰动画
    //SPELL_MIND_CONTROL                        = 32830,
    SPELL_MIND_CONTROL                          = 36797, ///< 精神控制 - 控制玩家
    SPELL_BANISH                                = 40370, ///< 放逐 - 对凤凰使用

    // 第5阶段法术 - 凯尔萨斯变身技能
    SPELL_KAEL_GAINING_POWER                    = 36091, ///< 获得力量引导
    SPELL_KAEL_EXPLODES                         = 36373, ///< 爆炸效果
    SPELL_KAEL_EXPLODES2                        = 36375, ///< 爆炸效果2
    SPELL_KAEL_EXPLODES3                        = 36092, ///< 爆炸效果3
    SPELL_KAEL_EXPLODES4                        = 36354, ///< 爆炸效果4
    SPELL_KAEL_STUNNED                          = 36185, ///< 昏迷状态
    SPELL_FULLPOWER                             = 36187, ///< 完全力量buff
    SPELL_NETHER_BEAM                           = 35873, ///< 虚空射线
    SPELL_PURE_NETHER_BEAM                      = 36196, ///< 纯净虚空射线
    SPELL_SUMMON_NETHER_VAPOR                   = 35865, ///< 召唤虚空蒸汽

    // 视觉效果、阶段转换法术
    SPELL_NETHER_BEAM_VISUAL                    = 36089, ///< 虚空射线视觉效果 - 由触发器对凯尔萨斯引导
    SPELL_NETHER_BEAM_VISUAL2                   = 36090, ///< 虚空射线视觉效果2 - 由触发器对凯尔萨斯引导
    SPELL_NETHER_BEAM_VISUAL3                   = 36364, ///< 虚空射线视觉效果3 - 凯尔萨斯自身施放，紫色发光效果

    // 重力失效法术
    SPELL_GRAVITY_LAPSE                         = 35941, ///< 重力失效主法术
    SPELL_GRAVITY_LAPSE_PERIODIC                = 34480, ///< 重力失效周期性效果
    SPELL_GRAVITY_LAPSE_FLIGHT_AURA             = 39432, ///< 重力失效飞行光环 - 玩家自身施放，允许飞行

    // 25个传送法术，每个团队成员一个
    SPELL_GRAVITY_LAPSE_TELE_FRONT              = 35966, ///< 重力失效传送 - 前方
    SPELL_GRAVITY_LAPSE_TELE_FRONT_RIGHT        = 35967, ///< 重力失效传送 - 右前方
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT         = 35968, ///< 重力失效传送 - 左前方
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT  = 35969, ///< 重力失效传送 - 施法者右后方
    SPELL_GRAVITY_LAPSE_TELE_BACK               = 35970, ///< 重力失效传送 - 后方
    SPELL_GRAVITY_LAPSE_TELE_TO_CASTER          = 35971, ///< 重力失效传送 - 到施法者
    SPELL_GRAVITY_LAPSE_TELE_BACK_LEFT          = 35972, ///< 重力失效传送 - 左后方
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT2        = 35973, ///< 重力失效传送 - 左前方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT        = 35974, ///< 重力失效传送 - 施法者左侧
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT2       = 35975, ///< 重力失效传送 - 施法者左侧2
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT3        = 35976, ///< 重力失效传送 - 左前方3
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_LEFT   = 35977, ///< 重力失效传送 - 施法者左后方
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT       = 35978, ///< 重力失效传送 - 施法者前方
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK        = 35979, ///< 重力失效传送 - 施法者后方
    SPELL_GRAVITY_LAPSE_TELE_FRONT_RIGHT2       = 35980, ///< 重力失效传送 - 右前方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_RIGHT       = 35981, ///< 重力失效传送 - 施法者右侧
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT_RIGHT = 35982, ///< 重力失效传送 - 施法者右前方
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT2      = 35983, ///< 重力失效传送 - 施法者前方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT_LEFT  = 35984, ///< 重力失效传送 - 施法者左前方
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT3       = 35985, ///< 重力失效传送 - 施法者左侧3
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_LEFT2  = 35986, ///< 重力失效传送 - 施法者左后方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK2       = 35987, ///< 重力失效传送 - 施法者后方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT2 = 35988, ///< 重力失效传送 - 施法者右后方2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_RIGHT2      = 35989, ///< 重力失效传送 - 施法者右侧2
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT3 = 35990, ///< 重力失效传送 - 施法者右后方3

    // 通用法术
    SPELL_REMOVE_WEAPONS                        = 39497, ///< 移除所有武器
    SPELL_REMOVE_WEAPONA                        = 39498, ///< 移除武器A
    SPELL_REMOVE_WEAPONB                        = 39499, ///< 移除武器B
    SPELL_REMOVE_WEAPONC                        = 39500, ///< 移除武器C
    SPELL_REMOVE_WEAPOND                        = 39501, ///< 移除武器D
    SPELL_REMOVE_WEAPONE                        = 39502, ///< 移除武器E
    SPELL_REMOVE_WEAPONF                        = 39503, ///< 移除武器F
    SPELL_REMOVE_WEAPONG                        = 39504, ///< 移除武器G

    // 塔隆血魔(暗影行者)法术
    SPELL_PSYCHIC_BLOW                          = 10689, ///< 精神打击
    SPELL_SILENCE                               = 30225, ///< 沉默
    SPELL_REND                                  = 36965, ///< 撕裂
    // 萨拉雷恩领主法术
    SPELL_BELLOWING_ROAR                        = 40636, ///< 咆哮 - 群体恐惧
    // 大星术师卡珀尼安法术
    SPELL_CAPERNIAN_FIREBALL                    = 36971, ///< 卡珀尼安火球术
    SPELL_CONFLAGRATION                         = 37018, ///< 火焰混淆
    SPELL_ARCANE_EXPLOSION                      = 36970, ///< 奥术爆炸
    // 工程大师泰隆尼库斯法术
    SPELL_BOMB                                  = 37036, ///< 炸弹
    SPELL_REMOTE_TOY                            = 37027, ///< 远程玩具
    // 虚空蒸汽法术
    SPELL_NETHER_VAPOR                          = 35859, ///< 虚空蒸汽
    // 凤凰法术
    SPELL_BURN                                  = 36720, ///< 燃烧 - 凤凰持续掉血
    SPELL_EMBER_BLAST                           = 34341, ///< 灰烬爆炸
    SPELL_REBIRTH                               = 41587, ///< 重生 - 凤凰蛋孵化

    // 火焰冲击
    SPELL_FLAME_STRIKE_DUMMY                    = 36730, ///< 火焰冲击虚拟法术
    SPELL_FLAME_STRIKE_DAMAGE                   = 36731  ///< 火焰冲击伤害
};

/**
 * @enum Creatures
 * @brief NPC ID枚举定义
 */
enum Creatures
{
    NPC_PHOENIX                             = 21362, ///< 凤凰NPC ID
    NPC_PHOENIX_EGG                         = 21364  ///< 凤凰蛋NPC ID
};

/**
 * @enum Models
 * @brief 模型ID枚举定义
 */
enum Models
{
    // 凤凰蛋和凤凰模型ID
    MODEL_ID_PHOENIX                        = 19682, ///< 凤凰模型ID
    MODEL_ID_PHOENIX_EGG                    = 20245  ///< 凤凰蛋模型ID
};

/**
 * @enum Actions
 * @brief Boss动作枚举，用于触发特定行为
 */
enum Actions
{
    ACTION_START_ENCOUNTER,      ///< 开始战斗遭遇
    ACTION_REVIVE_ADVISORS,      ///< 复活顾问
    ACTION_PREPARE_ADVISORS,     ///< 准备顾问(重置状态)
    ACTION_ACTIVE_ADVISOR,       ///< 激活顾问参战
    ACTION_SCHEDULE_COMBAT_EVENTS ///< 调度战斗事件
};

/**
 * @enum Advisors
 * @brief 顾问索引和计数枚举
 */
enum Advisors
{
    ADVISOR_THALADRED,           ///< 塔隆血魔(暗影行者)
    ADVISOR_SANGUINAR,           ///< 萨拉雷恩领主
    ADVISOR_CAPERNIAN,           ///< 大星术师卡珀尼安
    ADVISOR_TELONICUS,           ///< 工程大师泰隆尼库斯
    MAX_ADVISORS                 = 4,  ///< 顾问总数

    MAX_DEFEATED_ADVISORS        = 4,  ///< 第1阶段击败的顾问数
    MAX_KILLED_ADVISORS          = 8   ///< 第3阶段彻底击杀的顾问数(第1阶段+第3阶段)
};

/**
 * @enum Events
 * @brief 事件ID枚举，用于事件调度系统
 */
enum Events
{
    EVENT_START_ENCOUNTER = 1,   ///< 开始遭遇事件
    EVENT_ACTIVE_ADVISOR,        ///< 激活顾问事件
    EVENT_SUMMON_WEAPONS,        ///< 召唤武器事件
    EVENT_REVIVE_ADVISORS,       ///< 复活顾问事件
    EVENT_ENGAGE_COMBAT,         ///< 进入战斗事件
    EVENT_FULL_POWER,            ///< 完全力量事件
    EVENT_FIREBALL,              ///< 火球术事件
    EVENT_ARCANE_DISRUPTION,     ///< 奥术扰乱事件
    EVENT_FLAMESTRIKE,           ///< 火焰冲击事件
    EVENT_MIND_CONTROL,          ///< 精神控制事件
    EVENT_SUMMON_PHOENIX,        ///< 召唤凤凰事件
    EVENT_SHOCK_BARRIER,         ///< 震荡屏障事件
    EVENT_PYROBLAST,             ///< 炎爆术事件
    EVENT_PYROBLAST_CAST,        ///< 炎爆术施法事件
    EVENT_GAINING_POWER,         ///< 获得力量事件
    EVENT_END_TRANSITION,        ///< 结束阶段转换事件
    EVENT_GRAVITY_LAPSE,         ///< 重力失效事件
    EVENT_NETHER_BEAM,           ///< 虚空射线事件

    // 移动更新事件
    EVENT_TRANSITION_1,          ///< 阶段转换移动1
    EVENT_TRANSITION_2,          ///< 阶段转换移动2
    EVENT_TRANSITION_3,          ///< 阶段转换移动3
    EVENT_TRANSITION_4,          ///< 阶段转换移动4
    EVENT_TRANSITION_5,          ///< 阶段转换移动5
    EVENT_TRANSITION_6,          ///< 阶段转换移动6

    // 阶段转换事件
    EVENT_SIZE_INCREASE,         ///< 体型增大事件
    EVENT_EXPLODE,               ///< 爆炸事件
    EVENT_RESUME_COMBAT,         ///< 恢复战斗事件

    // 顾问事件
    EVENT_DELAYED_RESSURECTION,  ///< 延迟复活事件

    // 事件分组
    EVENT_GROUP_COMBAT  = 1,     ///< 默认技能组
    EVENT_GROUP_SPECIAL = 2      ///< 特殊技能组(炎爆术、虚空射线、震荡屏障)
};

/**
 * @enum Phases
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_NONE,              ///< 无阶段
    PHASE_INTRO,             ///< 开场阶段
    PHASE_REVIVED_ADVISORS,  ///< 复活顾问阶段(第3阶段)
    PHASE_COMBAT,            ///< 战斗阶段
    PHASE_TRANSITION         ///< 阶段转换(第4阶段到第5阶段)
};

/**
 * @enum MovementPoints
 * @brief 移动点ID枚举，用于阶段转换时的移动控制
 */
enum MovementPoints
{
    POINT_START_TRANSITION               = 1, ///< 开始阶段转换移动点
    POINT_TRANSITION_CENTER_ASCENDING    = 2, ///< 阶段转换中心上升点
    POINT_TRANSITION_HALFWAY_ASCENDING   = 3, ///< 阶段转换半途上升点
    POINT_TRANSITION_TOP                 = 4, ///< 阶段转换顶点
    POINT_TRANSITION_HALFWAY_DESCENDING  = 5, ///< 阶段转换半途下降点
    POINT_END_TRANSITION                 = 6  ///< 结束阶段转换移动点
};

/**
 * @brief 召唤武器法术数组
 *
 * 包含召唤所有七把传说武器的法术ID
 */
uint32 const SummonWeaponsSpells[] =
{
    SPELL_SUMMON_WEAPONA, SPELL_SUMMON_WEAPONB, SPELL_SUMMON_WEAPONC, SPELL_SUMMON_WEAPOND,
    SPELL_SUMMON_WEAPONE, SPELL_SUMMON_WEAPONF, SPELL_SUMMON_WEAPONG
};

/**
 * @brief 移除武器法术数组
 *
 * 包含移除所有七把传说武器的法术ID
 */
uint32 const RemoveWeaponsSpells[] =
{
    SPELL_REMOVE_WEAPONA, SPELL_REMOVE_WEAPONB, SPELL_REMOVE_WEAPONC, SPELL_REMOVE_WEAPOND,
    SPELL_REMOVE_WEAPONE, SPELL_REMOVE_WEAPONF, SPELL_REMOVE_WEAPONG
};

/**
 * @brief 重力失效传送法术数组
 *
 * 包含25个重力失效传送法术ID，每个团队成员一个
 */
uint32 GravityLapseSpells[] =
{
    SPELL_GRAVITY_LAPSE_TELE_FRONT,
    SPELL_GRAVITY_LAPSE_TELE_FRONT_RIGHT,
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT,
    SPELL_GRAVITY_LAPSE_TELE_BACK,
    SPELL_GRAVITY_LAPSE_TELE_TO_CASTER,
    SPELL_GRAVITY_LAPSE_TELE_BACK_LEFT,
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT2,
    SPELL_GRAVITY_LAPSE_TELE_FRONT_LEFT3,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_LEFT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK,
    SPELL_GRAVITY_LAPSE_TELE_FRONT_RIGHT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_RIGHT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT_RIGHT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_FRONT_LEFT,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_LEFT3,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_LEFT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_RIGHT2,
    SPELL_GRAVITY_LAPSE_TELE_CASTER_BACK_RIGHT3
};

const float CAPERNIAN_DISTANCE          = 20.0f;            ///< 卡珀尼安与目标的保持距离，她会远离目标施法
//const float KAEL_VISIBLE_RANGE          = 50.0f;

Position const afGravityPos = {795.0f, 0.0f, 70.0f};         ///< 重力失效中心位置

/**
 * @brief 阶段转换移动点位置数组
 *
 * 定义凯尔萨斯在阶段转换时的移动路径点
 * 前两个值不是静态的，每次抓包可能略有不同
 */
Position const TransitionPos[6] =
{
    // First two values are not static, they seem to differ on each sniff.
    { 794.0522f, -0.96732f, 48.97848f, 0.0f },     ///< 阶段转换起始点
    { 796.641f, -0.5888171f, 48.72847f, 3.176499f }, ///< 中心上升点
    { 795.007f, -0.471827f, 75.0f, 0.0f },         ///< 半途上升点
    { 795.007f, -0.471827f, 75.0f, 3.133458f },    ///< 顶点
    { 792.419f, -0.504778f, 50.0505f, 0.0f },      ///< 半途下降点
    { 792.419f, -0.504778f, 50.0505f, 3.130386f }  ///< 阶段转换结束点
};

/**
 * @struct boss_kaelthas
 * @brief 凯尔萨斯·逐日者Boss AI实现
 *
 * 继承自BossAI，实现凯尔萨斯的完整战斗逻辑。
 * 凯尔萨斯是一场5阶段的Boss战斗，具有复杂的阶段转换机制。
 *
 * 战斗流程:
 * 1. 第1阶段: 玩家依次与四位顾问战斗，凯尔萨斯旁观
 * 2. 第2阶段: 召唤七把传说武器，玩家需要全部摧毁
 * 3. 第3阶段: 四位顾问复活，同时与玩家战斗
 * 4. 第4阶段: 凯尔萨斯亲自参战，使用火球、炎爆、凤凰等技能
 * 5. 第5阶段: 凯尔萨斯血量低于50%后变身，获得重力失效能力
 */
struct boss_kaelthas : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_kaelthas(Creature* creature) : BossAI(creature, DATA_KAELTHAS)
    {
        Initialize();
    }

    /**
     * @brief 初始化所有成员变量
     *
     * 重置战斗状态相关的计数器和标志位
     */
    void Initialize()
    {
        _advisorCounter = 0;      // 顾问计数器
        _pyrosCast = 0;           // 炎爆术施放计数
        _netherbeamsCast = 0;     // 虚空射线施放计数
        _phase = PHASE_NONE;      // 当前阶段
        _scaleStage = 0;          // 体型增大阶段
        _hasFullPower = false;    // 是否已获得完全力量
    }

    /**
     * @brief 重置Boss状态
     *
     * 当Boss脱离战斗或被重置时调用，恢复所有状态到初始值
     * @调用时机 Boss脱离战斗、重置副本时
     */
    void Reset() override
    {
        Initialize();
        DoAction(ACTION_PREPARE_ADVISORS);  // 重置顾问状态
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 移除不可交互标志
        me->SetEmoteState(EMOTE_ONESHOT_NONE);  // 重置表情状态
        me->SetDisableGravity(false);  // 禁用重力
        me->SetTarget(ObjectGuid::Empty);  // 清空目标
        me->SetObjectScale(1.0f);  // 重置体型
        BossAI::Reset();
    }

    /**
     * @brief Boss返回出生点时调用
     *
     * 重置场景中的雕像和窗户等游戏对象
     * @调用时机 Boss返回出生点时
     */
    void JustReachedHome() override
    {
        BossAI::JustReachedHome();
        DoCastSelf(SPELL_REMOVE_WEAPONS);  // 移除所有武器

        // 重建周围环境(雕像和窗户)
        if (GameObject* statue = instance->GetGameObject(DATA_KAEL_STATUE_LEFT))
            statue->ResetDoorOrButton();

        if (GameObject* statue = instance->GetGameObject(DATA_KAEL_STATUE_RIGHT))
            statue->ResetDoorOrButton();

        if (GameObject* window = instance->GetGameObject(DATA_TEMPEST_BRIDGE_WINDOW))
            window->ResetDoorOrButton();
    }

    /**
     * @brief 执行特定动作
     * @param action 动作ID，参见Actions枚举
     *
     * 根据动作类型执行相应的战斗流程控制
     * @调用时机 遭遇战开始、顾问准备、顾问激活等
     */
    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_START_ENCOUNTER:
                // 开始战斗遭遇
                Talk(SAY_INTRO);  // 说开场白
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 设置不可交互(开场动画期间)

                // 缓存四位顾问的GUID
                _advisorGuid[ADVISOR_THALADRED] = instance->GetGuidData(DATA_THALADRED);
                _advisorGuid[ADVISOR_SANGUINAR] = instance->GetGuidData(DATA_SANGUINAR);
                _advisorGuid[ADVISOR_CAPERNIAN] = instance->GetGuidData(DATA_CAPERNIAN);
                _advisorGuid[ADVISOR_TELONICUS] = instance->GetGuidData(DATA_TELONICUS);

                _phase = PHASE_INTRO;  // 进入开场阶段
                instance->SetBossState(DATA_KAELTHAS, IN_PROGRESS);  // 设置Boss状态为进行中
                events.ScheduleEvent(EVENT_START_ENCOUNTER, 23s);  // 23秒后开始激活顾问
                break;
            case ACTION_PREPARE_ADVISORS:
                // 准备顾问，重置他们的状态
                for (uint8 i = 0; i < MAX_ADVISORS; ++i)
                {
                    if (Creature* creature = ObjectAccessor::GetCreature(*me, _advisorGuid[i]))
                    {
                        creature->Respawn(true);  // 重生顾问
                        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 设置不可攻击
                        creature->AI()->EnterEvadeMode();  // 进入逃避模式
                    }
                }
                break;
            case ACTION_ACTIVE_ADVISOR:
                // 激活顾问参战
                // 如果顾问已经被激活过(第3阶段)，则只计数死亡
                if (_phase == PHASE_REVIVED_ADVISORS)
                    ++_advisorCounter;

                switch (_advisorCounter)
                {
                    case ADVISOR_THALADRED:
                        Talk(SAY_INTRO_THALADRED);  // 介绍塔隆血魔
                        events.ScheduleEvent(EVENT_ACTIVE_ADVISOR, 7s);
                        break;
                    case ADVISOR_SANGUINAR:
                        Talk(SAY_INTRO_SANGUINAR);  // 介绍萨拉雷恩
                        events.ScheduleEvent(EVENT_ACTIVE_ADVISOR, 12500ms);
                        break;
                    case ADVISOR_CAPERNIAN:
                        Talk(SAY_INTRO_CAPERNIAN);  // 介绍卡珀尼安
                        events.ScheduleEvent(EVENT_ACTIVE_ADVISOR, 7s);
                        break;
                    case ADVISOR_TELONICUS:
                        Talk(SAY_INTRO_TELONICUS);  // 介绍泰隆尼库斯
                        events.ScheduleEvent(EVENT_ACTIVE_ADVISOR, 8400ms);
                        break;
                    case MAX_DEFEATED_ADVISORS:
                        // 所有顾问被击败 - 第2阶段开始
                        Talk(SAY_PHASE2_WEAPON);
                        events.ScheduleEvent(EVENT_SUMMON_WEAPONS, 3500ms);
                        break;
                    case MAX_KILLED_ADVISORS:
                        // 所有顾问被彻底击杀 - 第3阶段开始
                        events.ScheduleEvent(EVENT_ENGAGE_COMBAT, 5s);
                        break;
                    default:
                        break;
                }
                break;
            case ACTION_SCHEDULE_COMBAT_EVENTS:
                // 调度第4阶段战斗事件
                _phase = PHASE_COMBAT;
                events.SetPhase(PHASE_COMBAT);
                events.ScheduleEvent(EVENT_FIREBALL, 1s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 火球术
                events.ScheduleEvent(EVENT_ARCANE_DISRUPTION, 45s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 奥术扰乱
                events.ScheduleEvent(EVENT_FLAMESTRIKE, 30s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 火焰冲击
                events.ScheduleEvent(EVENT_MIND_CONTROL, 40s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 精神控制
                events.ScheduleEvent(EVENT_SUMMON_PHOENIX, 50s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 召唤凤凰
                break;
            default:
                break;
        }
    }

    /**
     * @brief 视线检测，触发战斗开始
     * @param who 进入视线的单位
     *
     * 当玩家进入Boss 30码范围内时触发战斗
     * @调用时机 玩家进入Boss视野范围时
     */
    void MoveInLineOfSight(Unit* who) override
    {
        if (_phase == PHASE_NONE && me->IsValidAttackTarget(who) && me->IsWithinDistInMap(who, 30.0f))
        {
            DoAction(ACTION_START_ENCOUNTER);  // 开始战斗
            who->SetInCombatWith(me);  // 设置战斗状态
            AddThreat(who, 0.0f);  // 添加威胁值
            me->SetTarget(who->GetGUID());  // 设置目标
        }
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者
     * @param damage 伤害值(可修改)
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 当Boss血量降至50%以下时触发第5阶段转换
     * @调用时机 Boss受到伤害时
     */
    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (_phase == PHASE_NONE)
        {
            DoAction(ACTION_START_ENCOUNTER);  // 如果尚未开始战斗，则开始

            if (attacker)
                me->SetTarget(attacker->GetGUID());
        }

        // 血量低于50%时触发第5阶段转换
        if (!_hasFullPower && me->HealthBelowPctDamaged(50, damage))
        {
            _hasFullPower = true;
            me->AttackStop();  // 停止攻击
            me->InterruptNonMeleeSpells(false);  // 打断非近战法术
            events.CancelEventGroup(EVENT_GROUP_COMBAT);  // 取消战斗事件组
            events.CancelEventGroup(EVENT_GROUP_SPECIAL);  // 取消特殊事件组
            events.SetPhase(PHASE_TRANSITION);  // 设置为转换阶段
            me->SetReactState(REACT_PASSIVE);  // 设置为被动反应
            me->GetMotionMaster()->MovePoint(POINT_START_TRANSITION, TransitionPos[0]);  // 开始移动到转换点
        }
    }

    /**
     * @brief 移动到指定点后回调
     * @param type 移动类型
     * @param point 移动点ID
     *
     * 控制阶段转换时的移动流程
     * @调用时机 移动完成时
     */
    void MovementInform(uint32 type, uint32 point) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        switch (point)
        {
            case POINT_START_TRANSITION:
                events.ScheduleEvent(EVENT_TRANSITION_1, 1s);
                break;
            case POINT_TRANSITION_CENTER_ASCENDING:
                me->SetFacingTo(float(M_PI));  // 面向指定方向
                Talk(SAY_PHASE5_NUTS);  // 说变身台词
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 设置不可交互
                me->SetDisableGravity(true);  // 启用重力失效
                //me->SetHover(true); -- 在抓包中有，但会破坏视觉效果
                events.ScheduleEvent(EVENT_TRANSITION_2, 2s);
                events.ScheduleEvent(EVENT_SIZE_INCREASE, 5s);  // 体型增大
                break;
            case POINT_TRANSITION_HALFWAY_ASCENDING:
                DoCast(me, SPELL_NETHER_BEAM_VISUAL3, true);  // 施放虚空射线视觉效果
                events.ScheduleEvent(EVENT_TRANSITION_3, 1s);
                break;
            case POINT_TRANSITION_TOP:
                events.ScheduleEvent(EVENT_EXPLODE, 10s);  // 爆炸效果
                break;
            case POINT_TRANSITION_HALFWAY_DESCENDING:
                events.ScheduleEvent(EVENT_TRANSITION_5, 2s);
                break;
            case POINT_END_TRANSITION:
                me->SetReactState(REACT_AGGRESSIVE);  // 恢复攻击性反应
                me->InterruptNonMeleeSpells(false);  // 打断非近战法术
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 移除不可交互标志
                me->RemoveAurasDueToSpell(SPELL_FULLPOWER);  // 移除完全力量光环

                if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0))
                    AttackStart(target);  // 攻击最高威胁目标

                DoAction(ACTION_SCHEDULE_COMBAT_EVENTS);  // 重新调度战斗事件
                events.ScheduleEvent(EVENT_GRAVITY_LAPSE, 10s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 重力失效
                break;
            default:
                break;
        }
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     *
     * @调用时机 Boss击杀玩家时
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_SLAY);  // 说击杀台词
    }

    /**
     * @brief 召唤生物时调用
     * @param summoned 被召唤的生物
     *
     * 为召唤的武器分配攻击目标
     * @调用时机 召唤武器或凤凰时
     */
    void JustSummoned(Creature* summoned) override
    {
        // 如果不是凤凰，则是七把武器之一
        if (summoned->GetEntry() != NPC_PHOENIX)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                summoned->AI()->AttackStart(target);  // 武器攻击随机目标

            summons.Summon(summoned);
        }
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者
     *
     * @调用时机 Boss死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);  // 说死亡台词
        _JustDied();
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主AI更新循环，处理所有事件和战斗逻辑
     * @调用时机 每个游戏循环tick
     * @性能注意事项 包含大量事件处理和状态检查
     */
    void UpdateAI(uint32 diff) override
    {
        if (_phase == PHASE_COMBAT)
            if (!UpdateVictim())
                return;

        events.Update(diff);

        // SPELL_KAEL_GAINING_POWER和SPELL_KAEL_STUNNED是引导法术，在转换期间需要被打断
        if (me->HasUnitState(UNIT_STATE_CASTING) && !me->FindCurrentSpellBySpellId(SPELL_KAEL_GAINING_POWER) && !me->FindCurrentSpellBySpellId(SPELL_KAEL_STUNNED))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START_ENCOUNTER:
                    me->SetUnitFlag(UNIT_FLAG_PACIFIED);  // 设置被安抚状态
                    DoAction(ACTION_ACTIVE_ADVISOR);  // 开始激活顾问
                    break;
                case EVENT_ACTIVE_ADVISOR:
                    // 激活当前顾问参战
                    if (Creature* advisor = ObjectAccessor::GetCreature(*me, _advisorGuid[_advisorCounter]))
                    {
                        advisor->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 移除不可攻击标志

                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            advisor->AI()->AttackStart(target);  // 顾问攻击随机目标
                    }
                    ++_advisorCounter;
                    break;
                case EVENT_SUMMON_WEAPONS:
                    DoCastSelf(SPELL_SUMMON_WEAPONS);  // 召唤武器
                    events.ScheduleEvent(EVENT_REVIVE_ADVISORS, 120s);  // 120秒后复活顾问
                    break;
                case EVENT_REVIVE_ADVISORS:
                    _phase = PHASE_REVIVED_ADVISORS;  // 进入第3阶段
                    Talk(SAY_PHASE3_ADVANCE);  // 说复活台词
                    DoCast(me, SPELL_RESSURECTION);  // 施放复活法术
                    break;
                case EVENT_ENGAGE_COMBAT:
                    Talk(SAY_PHASE4_INTRO2);  // 说参战台词

                    // 有时玩家会在第1-3阶段积累威胁值，在释放凯尔萨斯前重置威胁列表
                    ResetThreatList();

                    me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_PACIFIED);  // 移除限制标志

                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        AttackStart(target);  // 攻击随机目标

                    DoAction(ACTION_SCHEDULE_COMBAT_EVENTS);  // 调度战斗事件
                    events.ScheduleEvent(EVENT_PYROBLAST, 60s, EVENT_GROUP_COMBAT, PHASE_COMBAT);  // 炎爆术
                    break;
                case EVENT_FIREBALL:
                    DoCastVictim(SPELL_FIREBALL);  // 对当前目标施放火球术
                    events.ScheduleEvent(EVENT_FIREBALL, 2500ms, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_ARCANE_DISRUPTION:
                    DoCastVictim(SPELL_ARCANE_DISRUPTION, true);  // 施放奥术扰乱
                    events.ScheduleEvent(EVENT_ARCANE_DISRUPTION, 60s, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_FLAMESTRIKE:
                    // 对随机目标施放火焰冲击
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_SUMMON_FLAME_STRIKE);

                    events.ScheduleEvent(EVENT_FLAMESTRIKE, 30s, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_MIND_CONTROL:
                    Talk(SAY_MIND_CONTROL);  // 说精神控制台词
                    DoCastAOE(SPELL_MIND_CONTROL, { SPELLVALUE_MAX_TARGETS, 3 });  // 控制最多3个目标
                    events.ScheduleEvent(EVENT_MIND_CONTROL, 60s, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_SUMMON_PHOENIX:
                    DoCast(me, SPELL_PHOENIX_ANIMATION);  // 施放凤凰动画
                    Talk(SAY_SUMMON_PHOENIX);  // 说召唤凤凰台词
                    events.ScheduleEvent(EVENT_SUMMON_PHOENIX, 45s, 60s, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_END_TRANSITION:
                    me->SetEmoteState(EMOTE_ONESHOT_NONE);  // 重置表情状态
                    DoCast(SPELL_FULLPOWER);  // 施放完全力量
                    events.ScheduleEvent(EVENT_TRANSITION_4, 2s);
                    break;
                case EVENT_PYROBLAST:
                    _pyrosCast = 0;
                    Talk(EMOTE_PYROBLAST);  // 炎爆术表情提示
                    DoCast(me, SPELL_SHOCK_BARRIER);  // 施放震荡屏障
                    events.DelayEvents(10s, EVENT_GROUP_COMBAT);  // 延迟战斗事件
                    events.ScheduleEvent(EVENT_PYROBLAST_CAST, 1s, EVENT_GROUP_SPECIAL, PHASE_COMBAT);
                    break;
                case EVENT_PYROBLAST_CAST:
                    // 连续施放3次炎爆术
                    if (_pyrosCast < 3)
                    {
                        DoCastVictim(SPELL_PYROBLAST);
                        events.ScheduleEvent(EVENT_PYROBLAST_CAST, 3s);
                        _pyrosCast++;
                    }
                    else
                        events.ScheduleEvent(EVENT_PYROBLAST, 60s, EVENT_GROUP_COMBAT, PHASE_COMBAT);
                    break;
                case EVENT_GRAVITY_LAPSE:
                    Talk(SAY_GRAVITY_LAPSE);  // 说重力失效台词
                    DoCastAOE(SPELL_GRAVITY_LAPSE);  // 施放重力失效
                    DoCast(me, SPELL_NETHER_VAPOR);  // 召唤虚空蒸汽
                    events.DelayEvents(24s, EVENT_GROUP_COMBAT);  // 延迟战斗事件
                    events.ScheduleEvent(EVENT_NETHER_BEAM, 3s, EVENT_GROUP_SPECIAL, PHASE_COMBAT);  // 虚空射线
                    events.ScheduleEvent(EVENT_SHOCK_BARRIER, 1s, EVENT_GROUP_SPECIAL, PHASE_COMBAT);  // 震荡屏障
                    events.ScheduleEvent(EVENT_GRAVITY_LAPSE, 30s, EVENT_GROUP_SPECIAL, PHASE_COMBAT);
                    break;
                case EVENT_NETHER_BEAM:
                    // 施放虚空射线，最多8次
                    if (_netherbeamsCast <= 8)
                    {
                        if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                            DoCast(unit, SPELL_NETHER_BEAM);

                        _netherbeamsCast++;
                        events.ScheduleEvent(EVENT_NETHER_BEAM, 3s);
                    }
                    else
                        _netherbeamsCast = 0;
                    break;
                case EVENT_TRANSITION_1:
                    me->GetMotionMaster()->MovePoint(POINT_TRANSITION_CENTER_ASCENDING, TransitionPos[1]);
                    break;
                case EVENT_TRANSITION_2:
                    DoCast(me, SPELL_KAEL_GAINING_POWER);  // 施放获得力量引导
                    me->GetMotionMaster()->Clear();  // 清除移动器
                    me->RemoveUnitMovementFlag(MOVEMENTFLAG_ROOT);  // 移除定身移动标志
                    me->GetMotionMaster()->MovePoint(POINT_TRANSITION_HALFWAY_ASCENDING, TransitionPos[2], false);
                    break;
                case EVENT_TRANSITION_3:
                    me->GetMotionMaster()->MovePoint(POINT_TRANSITION_TOP, TransitionPos[3], false);
                    break;
                case EVENT_TRANSITION_4:
                    me->GetMotionMaster()->MovePoint(POINT_TRANSITION_HALFWAY_DESCENDING, TransitionPos[4], false);
                    break;
                case EVENT_TRANSITION_5:
                    me->GetMotionMaster()->MovePoint(POINT_END_TRANSITION, TransitionPos[5], false);
                    break;
                case EVENT_EXPLODE:
                    me->InterruptNonMeleeSpells(false);  // 打断非近战法术
                    me->RemoveAurasDueToSpell(SPELL_NETHER_BEAM_VISUAL3);  // 移除视觉效果
                    DoCast(me, SPELL_KAEL_EXPLODES3, true);  // 爆炸效果
                    DoCast(me, SPELL_KAEL_STUNNED);  // 核心在飞行时无法正确处理表情
                    me->SetEmoteState(EMOTE_STATE_DROWNED);  // 设置溺水表情

                    // 摧毁周围环境
                    if (GameObject* statue = instance->GetGameObject(DATA_KAEL_STATUE_LEFT))
                        statue->UseDoorOrButton();

                    if (GameObject* statue = instance->GetGameObject(DATA_KAEL_STATUE_RIGHT))
                        statue->UseDoorOrButton();

                    if (GameObject* window = instance->GetGameObject(DATA_TEMPEST_BRIDGE_WINDOW))
                        window->UseDoorOrButton();

                    events.ScheduleEvent(EVENT_END_TRANSITION, 10s);
                    break;
                case EVENT_SIZE_INCREASE:
                    // 逐渐增大体型
                    switch (_scaleStage)
                    {
                        case 0:
                            me->SetObjectScale(1.4f);
                            events.ScheduleEvent(EVENT_SIZE_INCREASE, 5s);
                            break;
                        case 1:
                            me->SetObjectScale(1.8f);
                            events.ScheduleEvent(EVENT_SIZE_INCREASE, 3s);
                            break;
                        case 2:
                            me->SetObjectScale(2.0f);
                            events.ScheduleEvent(EVENT_SIZE_INCREASE, 1s);
                            break;
                        case 3:
                            me->SetObjectScale(2.2f);
                            break;
                        default:
                            break;
                    }
                    ++_scaleStage;
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING) && !me->FindCurrentSpellBySpellId(SPELL_KAEL_GAINING_POWER) && !me->FindCurrentSpellBySpellId(SPELL_KAEL_STUNNED))
                return;
        }

        if (events.IsInPhase(PHASE_COMBAT))
            DoMeleeAttackIfReady();  // 准备近战攻击
    }
private:
    uint8 _advisorCounter;              ///< 顾问计数器，用于跟踪当前激活的顾问
    uint8 _phase;                       ///< 当前战斗阶段
    uint8 _pyrosCast;                   ///< 炎爆术施放计数，每次连续施放3次
    uint8 _scaleStage;                  ///< 体型增大阶段，用于阶段转换时的视觉效果
    uint8 _netherbeamsCast;             ///< 虚空射线施放计数，每次重力失效期间施放8次
    bool _hasFullPower;                 ///< 是否已获得完全力量(第5阶段)
    ObjectGuid _advisorGuid[MAX_ADVISORS]; ///< 四位顾问的GUID数组
};

/**
 * @struct advisorbase_ai
 * @brief 顾问基类AI
 *
 * 四位顾问的共同基类，实现了假死和复活机制。
 * 顾问在第1阶段被击败后不会真正死亡，而是进入假死状态，
 * 在第3阶段被复活后才会真正死亡。
 */
struct advisorbase_ai : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    advisorbase_ai(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _hasRessurrected = false;   // 是否已复活
        _inFakeDeath = false;       // 是否处于假死状态
        DelayRes_Target.Clear();    // 延迟复活目标
    }

    /**
     * @brief 重置AI状态
     *
     * @调用时机 Boss脱离战斗时
     */
    void Reset() override
    {
        Initialize();

        me->SetStandState(UNIT_STAND_STATE_STAND);  // 设置站立状态
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 设置不可攻击
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);  // 移除交互和昏迷标志

        // 重置遭遇战
        if (instance->GetBossState(DATA_KAELTHAS) == IN_PROGRESS)
            if (Creature* Kaelthas = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_KAELTHAS)))
                Kaelthas->AI()->EnterEvadeMode();  // 凯尔萨斯进入逃避模式
    }

    /**
     * @brief 视线检测
     * @param who 进入视线的单位
     *
     * 假死状态或不可攻击时不响应
     */
    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || _inFakeDeath || me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
            return;

        ScriptedAI::MoveInLineOfSight(who);
    }

    /**
     * @brief 开始攻击
     * @param who 攻击目标
     *
     * 假死状态或不可攻击时不响应
     */
    void AttackStart(Unit* who) override
    {
        if (!who || _inFakeDeath || me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
            return;

        ScriptedAI::AttackStart(who);
    }

    /**
     * @brief 被法术命中时调用
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 当被复活法术命中时触发复活逻辑
     * @调用时机 被法术命中时
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_RESSURECTION)
        {
            _hasRessurrected = true;  // 标记已复活
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);  // 移除限制标志
            me->SetStandState(UNIT_STAND_STATE_STAND);  // 设置站立状态
            events.ScheduleEvent(EVENT_DELAYED_RESSURECTION, 2s);  // 2秒后延迟复活
        }
    }

    /**
     * @brief 受到伤害时调用
     * @param killer 攻击者
     * @param damage 伤害值(可修改)
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 第1阶段被击败时进入假死状态而非真正死亡
     * @调用时机 受到伤害时
     */
    void DamageTaken(Unit* killer, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 如果伤害致命、不在假死状态且未复活，则进入假死
        if (damage >= me->GetHealth() && !_inFakeDeath && !_hasRessurrected)
        {
            // 阻止死亡
            damage = 0;
            _inFakeDeath = true;

            me->InterruptNonMeleeSpells(false);  // 打断非近战法术
            me->SetHealth(0);  // 设置血量为0
            me->ClearComboPointHolders();  // 清除连击点持有者
            me->RemoveAllAurasOnDeath();  // 移除死亡时的光环
            me->ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);  // 移除低血量光环状态
            me->ModifyAuraState(AURA_STATE_HEALTHLESS_35_PERCENT, false);
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);  // 设置不可交互和昏迷
            me->SetTarget(ObjectGuid::Empty);  // 清空目标
            me->SetStandState(UNIT_STAND_STATE_DEAD);  // 设置死亡状态
            me->GetMotionMaster()->Clear();  // 清除移动器
            JustDied(killer);  // 调用死亡处理
        }
    }

    /**
     * @brief 死亡处理
     * @param killer 击杀者
     *
     * 通知凯尔萨斯顾问已死亡
     * @调用时机 真正死亡或假死时
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* kael = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_KAELTHAS)))
            kael->AI()->DoAction(ACTION_ACTIVE_ADVISOR);  // 通知凯尔萨斯激活下一个顾问
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理延迟复活事件
     * @调用时机 每个游戏循环tick
     */
    void UpdateAI(uint32 diff) override
    {
        if (_hasRessurrected)
            events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            if (eventId == EVENT_DELAYED_RESSURECTION)
            {
                _inFakeDeath = false;  // 退出假死状态

                // 获取复活目标
                Unit* Target = ObjectAccessor::GetUnit(*me, DelayRes_Target);
                if (!Target)
                    Target = me->GetVictim();

                ResetThreatList();  // 重置威胁列表
                AttackStart(Target);  // 开始攻击目标
                me->GetMotionMaster()->Clear();  // 清除移动器
                me->GetMotionMaster()->MoveChase(Target);  // 追击目标
                AddThreat(Target, 0.0f);  // 添加威胁值
            }
        }
    }
    public:
        EventMap events;              ///< 事件映射表
        InstanceScript* instance;     ///< 副本脚本实例
        bool _hasRessurrected;        ///< 是否已复活(第3阶段)
        bool _inFakeDeath;            ///< 是否处于假死状态(第1阶段)
        ObjectGuid DelayRes_Target;   ///< 延迟复活的目标GUID
};

/**
 * @struct boss_thaladred_the_darkener
 * @brief 塔隆血魔(暗影行者)AI
 *
 * 凯尔萨斯四位顾问之一，战士型Boss。
 * 特点是会随机凝视目标并追击，对目标造成大量伤害。
 * 技能：精神打击、沉默、撕裂
 */
struct boss_thaladred_the_darkener : public advisorbase_ai
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_thaladred_the_darkener(Creature* creature) : advisorbase_ai(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Gaze_Timer = 100;          // 凝视计时器
        Silence_Timer = 20000;     // 沉默计时器
        Rend_Timer = 4000;         // 撕裂计时器
        PsychicBlow_Timer = 10000; // 精神打击计时器
    }

    uint32 Gaze_Timer;          ///< 凝视计时器
    uint32 Silence_Timer;       ///< 沉默计时器
    uint32 Rend_Timer;          ///< 撕裂计时器
    uint32 PsychicBlow_Timer;   ///< 精神打击计时器

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();

        advisorbase_ai::Reset();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_THALADRED_AGGRO);  // 说激活台词
        AddThreat(who, 5000000.0f);  // 添加大量初始威胁值
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* killer) override
    {
        if (_hasRessurrected)
            Talk(SAY_THALADRED_DEATH);  // 第3阶段死亡时说台词

        advisorbase_ai::JustDied(killer);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     */
    void UpdateAI(uint32 diff) override
    {
        advisorbase_ai::UpdateAI(diff);

        if (!UpdateVictim() || _inFakeDeath)
            return;

        // 凝视计时器 - 随机切换目标
        if (Gaze_Timer <= diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                ResetThreatList();  // 重置威胁列表
                AddThreat(target, 5000000.0f);  // 给新目标添加大量威胁值
                Talk(EMOTE_THALADRED_GAZE, target);  // 发送凝视表情提示
                Gaze_Timer = 8500;
            }
        }
        else
            Gaze_Timer -= diff;

        // 沉默计时器
        if (Silence_Timer <= diff)
        {
            DoCastVictim(SPELL_SILENCE);  // 对当前目标施放沉默
            Silence_Timer = 20000;
        }
        else
            Silence_Timer -= diff;

        // 撕裂计时器
        if (Rend_Timer <= diff)
        {
            DoCastVictim(SPELL_REND);  // 对当前目标施放撕裂
            Rend_Timer = 4000;
        }
        else
            Rend_Timer -= diff;

        // 精神打击计时器
        if (PsychicBlow_Timer <= diff)
        {
            DoCastVictim(SPELL_PSYCHIC_BLOW);  // 对当前目标施放精神打击
            PsychicBlow_Timer = 20000 + rand32() % 5000;
        }
        else
            PsychicBlow_Timer -= diff;

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct boss_lord_sanguinar
 * @brief 萨拉雷恩领主AI
 *
 * 凯尔萨斯四位顾问之一，骑士型Boss。
 * 主要技能是群体恐惧咆哮。
 * 技能：咆哮恐惧
 */
struct boss_lord_sanguinar : public advisorbase_ai
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_lord_sanguinar(Creature* creature) : advisorbase_ai(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Fear_Timer = 20000;  // 恐惧计时器
    }

    uint32 Fear_Timer;  ///< 恐惧计时器

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
        advisorbase_ai::Reset();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_SANGUINAR_AGGRO);  // 说激活台词
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* killer) override
    {
        if (_hasRessurrected)
            Talk(SAY_SANGUINAR_DEATH);  // 第3阶段死亡时说台词

        advisorbase_ai::JustDied(killer);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     */
    void UpdateAI(uint32 diff) override
    {
        advisorbase_ai::UpdateAI(diff);

        if (!UpdateVictim() || _inFakeDeath)
            return;

        // 恐惧计时器 - 约30秒一次群体恐惧
        if (Fear_Timer <= diff)
        {
            DoCastVictim(SPELL_BELLOWING_ROAR);  // 施放咆哮恐惧
            Fear_Timer = 25000 + rand32() % 10000;  // 约30秒后再次施放
        }
        else
            Fear_Timer -= diff;

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct boss_grand_astromancer_capernian
 * @brief 大星术师卡珀尼安AI
 *
 * 凯尔萨斯四位顾问之一，法师型Boss。
 * 特点是保持距离施法，不进行近战攻击。
 * 技能：火球术、火焰混淆、奥术爆炸
 *
 * @note 不进行近战攻击，需要保持与目标的距离
 */
struct boss_grand_astromancer_capernian : public advisorbase_ai
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_grand_astromancer_capernian(Creature* creature) : advisorbase_ai(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Fireball_Timer = 2000;        // 火球术计时器
        Conflagration_Timer = 20000;  // 火焰混淆计时器
        ArcaneExplosion_Timer = 5000; // 奥术爆炸计时器
        Yell_Timer = 2000;            // 喊话计时器
        Yell = false;                 // 是否已喊话
    }

    uint32 Fireball_Timer;        ///< 火球术计时器
    uint32 Conflagration_Timer;   ///< 火焰混淆计时器
    uint32 ArcaneExplosion_Timer; ///< 奥术爆炸计时器
    uint32 Yell_Timer;            ///< 喊话计时器
    bool Yell;                    ///< 是否已喊话

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();

        advisorbase_ai::Reset();
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* killer) override
    {
        if (_hasRessurrected)
            Talk(SAY_CAPERNIAN_DEATH);  // 第3阶段死亡时说台词

        advisorbase_ai::JustDied(killer);
    }

    /**
     * @brief 开始攻击
     * @param who 攻击目标
     *
     * 保持与目标的距离(20码)
     */
    void AttackStart(Unit* who) override
    {
        if (!who || me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
            return;

        if (me->Attack(who, true))
        {
            AddThreat(who, 0.0f);
            me->SetInCombatWith(who);
            who->SetInCombatWith(me);

            me->GetMotionMaster()->MoveChase(who, CAPERNIAN_DISTANCE);  // 保持距离追击
        }
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_CAPERNIAN_AGGRO);  // 说激活台词
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * @note 不进行近战攻击
     */
    void UpdateAI(uint32 diff) override
    {
        advisorbase_ai::UpdateAI(diff);

        if (!UpdateVictim() || _inFakeDeath)
            return;

        // 火球术计时器
        if (Fireball_Timer <= diff)
        {
            DoCastVictim(SPELL_CAPERNIAN_FIREBALL);  // 对当前目标施放火球术
            Fireball_Timer = 4000;
        }
        else
            Fireball_Timer -= diff;

        // 火焰混淆计时器
        if (Conflagration_Timer <= diff)
        {
            Unit* target = SelectTarget(SelectTargetMethod::Random, 0);

            // 优先对30码内的随机目标施放，否则对当前目标施放
            if (target && me->IsWithinDistInMap(target, 30))
                DoCast(target, SPELL_CONFLAGRATION);
            else
                DoCastVictim(SPELL_CONFLAGRATION);

            Conflagration_Timer = 10000 + rand32() % 5000;
        }
        else
            Conflagration_Timer -= diff;

        // 奥术爆炸计时器 - 当有近战范围目标时施放
        if (ArcaneExplosion_Timer <= diff)
        {
            bool InMeleeRange = false;
            Unit* target = nullptr;
            for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
            {
                Unit* unit = ref->GetVictim();
                if (unit->IsWithinMeleeRange(me))
                {
                    InMeleeRange = true;
                    target = unit;
                    break;
                }
            }

            if (InMeleeRange)
                DoCast(target, SPELL_ARCANE_EXPLOSION);  // 对近战目标施放奥术爆炸

            ArcaneExplosion_Timer = 4000 + rand32() % 2000;
        }
        else
            ArcaneExplosion_Timer -= diff;

        // 不进行任何近战伤害
    }
};

/**
 * @struct boss_master_engineer_telonicus
 * @brief 工程大师泰隆尼库斯AI
 *
 * 凯尔萨斯四位顾问之一，工程师型Boss。
 * 使用炸弹和远程玩具进行攻击。
 * 技能：炸弹、远程玩具
 */
struct boss_master_engineer_telonicus : public advisorbase_ai
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_master_engineer_telonicus(Creature* creature) : advisorbase_ai(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Bomb_Timer = 10000;       // 炸弹计时器
        RemoteToy_Timer = 5000;   // 远程玩具计时器
    }

    uint32 Bomb_Timer;        ///< 炸弹计时器
    uint32 RemoteToy_Timer;   ///< 远程玩具计时器

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();

        advisorbase_ai::Reset();
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* killer) override
    {
        if (_hasRessurrected)
            Talk(SAY_TELONICUS_DEATH);  // 第3阶段死亡时说台词

        advisorbase_ai::JustDied(killer);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_TELONICUS_AGGRO);  // 说激活台词
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     */
    void UpdateAI(uint32 diff) override
    {
        advisorbase_ai::UpdateAI(diff);

        if (!UpdateVictim() || _inFakeDeath)
            return;

        // 炸弹计时器
        if (Bomb_Timer <= diff)
        {
            DoCastVictim(SPELL_BOMB);  // 对当前目标施放炸弹
            Bomb_Timer = 25000;
        }
        else
            Bomb_Timer -= diff;

        // 远程玩具计时器
        if (RemoteToy_Timer <= diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_REMOTE_TOY);  // 对随机目标施放远程玩具

            RemoteToy_Timer = 10000 + rand32() % 5000;
        }
        else
            RemoteToy_Timer -= diff;

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_kael_flamestrike
 * @brief 凯尔萨斯火焰冲击NPC AI
 *
 * 由凯尔萨斯召唤的火焰冲击区域NPC。
 * 出现后施放火焰冲击虚拟法术，15秒后消失。
 */
struct npc_kael_flamestrike : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_kael_flamestrike(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 初始化AI
     *
     * 设置为被动反应状态
     */
    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
    }

    /**
     * @brief 出现时调用
     *
     * 施放火焰冲击虚拟法术并设置消失时间
     * @调用时机 NPC生成时
     */
    void JustAppeared() override
    {
        DoCastSelf(SPELL_FLAME_STRIKE_DUMMY);  // 施放火焰冲击虚拟法术
        me->DespawnOrUnsummon(15s);  // 15秒后消失
    }
};

/**
 * @struct npc_phoenix_tk
 * @brief 凤凰NPC AI
 *
 * 由凯尔萨斯召唤的凤凰NPC。
 * 凤凰会持续燃烧自己的生命值，死亡后生成凤凰蛋。
 * 凤凰蛋孵化后会再次生成凤凰。
 */
struct npc_phoenix_tk : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_phoenix_tk(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Cycle_Timer = 2000;  // 燃烧周期计时器
    }

    uint32 Cycle_Timer;  ///< 燃烧周期计时器(每2秒燃烧一次生命)

    /**
     * @brief 重置AI状态
     *
     * 施放燃烧光环
     */
    void Reset() override
    {
        Initialize();
        DoCast(me, SPELL_BURN, true);  // 施放燃烧光环
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     *
     * 召唤凤凰蛋
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 这个法术还在使用吗？
        //DoCast(me, SPELL_EMBER_BLAST, true);
        // 召唤凤凰蛋，16秒后消失
        me->SummonCreature(NPC_PHOENIX_EGG, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 16s);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 凤凰每2秒燃烧自己的生命值
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (Cycle_Timer <= diff)
        {
            // 燃烧法术应该能做这个，但它不能，所以暂时这样做
            uint32 dmg = urand(4500, 5500);  // 随机4500-5500伤害
            if (me->GetHealth() > dmg)
                me->ModifyHealth(-int32(dmg));  // 扣除生命值
            Cycle_Timer = 2000;
        }
        else
            Cycle_Timer -= diff;

        DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_phoenix_egg_tk
 * @brief 凤凰蛋NPC AI
 *
 * 由死亡凤凰生成的凤凰蛋NPC。
 * 15秒后孵化生成新的凤凰。
 */
struct npc_phoenix_egg_tk : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_phoenix_egg_tk(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        Rebirth_Timer = 15000;  // 孵化计时器(15秒)
    }

    uint32 Rebirth_Timer;  ///< 孵化计时器

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 视线检测(忽略)
     * @param who 进入视线的单位
     */
    //ignore any
    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 开始攻击
     * @param who 攻击目标
     *
     * 进入战斗但不移动
     */
    void AttackStart(Unit* who) override
    {
        if (me->Attack(who, false))
        {
            me->SetInCombatWith(who);
            who->SetInCombatWith(me);

            DoStartNoMovement(who);  // 不移动
        }
    }

    /**
     * @brief 召唤生物时调用
     * @param summoned 被召唤的生物
     *
     * 给新凤凰添加威胁值并施放重生法术
     */
    void JustSummoned(Creature* summoned) override
    {
        AddThreat(me->GetVictim(), 0.0f, summoned);  // 继承威胁值
        summoned->CastSpell(summoned, SPELL_REBIRTH, false);  // 施放重生法术
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 15秒后孵化生成新凤凰
     */
    void UpdateAI(uint32 diff) override
    {
        if (!Rebirth_Timer)
            return;

        if (Rebirth_Timer <= diff)
        {
            // 召唤新凤凰
            me->SummonCreature(NPC_PHOENIX, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_CORPSE_DESPAWN, 5s);
            Rebirth_Timer = 0;  // 停止计时器
        }
        else
            Rebirth_Timer -= diff;
    }
};

/**
 * @class spell_kael_gravity_lapse
 * @brief 重力失效法术脚本
 *
 * 法术ID: 35941
 * 为每个目标分配不同的传送法术，使玩家分布在房间四周。
 * 同时为玩家添加周期性效果和飞行光环。
 */
// 35941 - Gravity Lapse
class spell_kael_gravity_lapse : public SpellScript
{
    PrepareSpellScript(spell_kael_gravity_lapse);

public:
    /**
     * @brief 构造函数
     */
    spell_kael_gravity_lapse()
    {
        _targetCount = 0;
    }

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return 验证结果
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(GravityLapseSpells);
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 为目标施放对应的传送法术和飞行光环
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        // 为目标施放对应的传送法术(按顺序分配)
        GetCaster()->CastSpell(GetHitUnit(), GravityLapseSpells[_targetCount], true);
        // 为目标添加周期性效果
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_GRAVITY_LAPSE_PERIODIC, true);
        // 为目标添加飞行光环
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_GRAVITY_LAPSE_FLIGHT_AURA, true);
        _targetCount++;
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_kael_gravity_lapse::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }

private:
    uint8 _targetCount;  ///< 目标计数器，用于分配不同的传送法术
};

/**
 * @class spell_kaelthas_flame_strike
 * @brief 火焰冲击光环脚本
 *
 * 法术ID: 36730
 * 当虚拟光环移除时施放实际的火焰冲击伤害法术。
 */
// 36730 - Flame Strike
class spell_kaelthas_flame_strike : public AuraScript
{
    PrepareAuraScript(spell_kaelthas_flame_strike);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证结果
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLAME_STRIKE_DAMAGE });
    }

    /**
     * @brief 光环移除后回调
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 施放火焰冲击伤害法术
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->CastSpell(target, SPELL_FLAME_STRIKE_DAMAGE);  // 施放伤害法术
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_kaelthas_flame_strike::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_kaelthas_summon_weapons
 * @brief 召唤武器法术脚本
 *
 * 法术ID: 36976
 * 施放所有七把传说武器的召唤法术。
 */
// 36976 - Summon Weapons
class spell_kaelthas_summon_weapons : public SpellScript
{
    PrepareSpellScript(spell_kaelthas_summon_weapons);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证结果
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(SummonWeaponsSpells);
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 为施法者施放所有七把武器的召唤法术
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        for (uint32 spells : SummonWeaponsSpells)
            caster->CastSpell(caster, spells, true);  // 施放每把武器的召唤法术
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_kaelthas_summon_weapons::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_kaelthas_remove_weapons
 * @brief 移除附魔武器法术脚本
 *
 * 法术ID: 39497
 * 从玩家身上移除所有附魔武器效果。
 */
// 39497 - Remove Enchanted Weapons
class spell_kaelthas_remove_weapons : public SpellScript
{
    PrepareSpellScript(spell_kaelthas_remove_weapons);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证结果
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(RemoveWeaponsSpells);
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 对命中的玩家施放所有武器移除法术
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = GetHitPlayer())
            for (uint32 spells : RemoveWeaponsSpells)
                // 施放移除法术，但不忽略能量和材料消耗
                player->CastSpell(player, spells, TriggerCastFlags(TRIGGERED_FULL_MASK & ~TRIGGERED_IGNORE_POWER_AND_REAGENT_COST));
    }

    /**
     * @brief 注册效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_kaelthas_remove_weapons::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册凯尔萨斯Boss及相关NPC和法术脚本
 *
 * 此函数由脚本系统在服务器启动时调用，注册所有相关的AI和法术脚本
 */
void AddSC_boss_kaelthas()
{
    RegisterTheEyeCreatureAI(boss_kaelthas);                      // 注册凯尔萨斯AI
    RegisterTheEyeCreatureAI(boss_thaladred_the_darkener);        // 注册塔隆血魔AI
    RegisterTheEyeCreatureAI(boss_lord_sanguinar);                // 注册萨拉雷恩AI
    RegisterTheEyeCreatureAI(boss_grand_astromancer_capernian);   // 注册卡珀尼安AI
    RegisterTheEyeCreatureAI(boss_master_engineer_telonicus);     // 注册泰隆尼库斯AI
    RegisterTheEyeCreatureAI(npc_kael_flamestrike);               // 注册火焰冲击AI
    RegisterTheEyeCreatureAI(npc_phoenix_tk);                     // 注册凤凰AI
    RegisterTheEyeCreatureAI(npc_phoenix_egg_tk);                 // 注册凤凰蛋AI
    RegisterSpellScript(spell_kael_gravity_lapse);                // 注册重力失效法术脚本
    RegisterSpellScript(spell_kaelthas_flame_strike);             // 注册火焰冲击法术脚本
    RegisterSpellScript(spell_kaelthas_summon_weapons);           // 注册召唤武器法术脚本
    RegisterSpellScript(spell_kaelthas_remove_weapons);           // 注册移除武器法术脚本
}
