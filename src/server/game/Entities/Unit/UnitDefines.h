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
 * @file UnitDefines.h
 * @brief Unit 单位系统核心定义头文件
 *
 * 本文件定义了 Unit 类及其子类（Player、Creature 等）所需的所有基础枚举类型、
 * 常量定义和辅助结构体。这些定义涵盖了单位的姿态状态、视觉标志、PVP 状态、
 * NPC 标志、移动标志、战斗命中信息等核心游戏机制。
 *
 * 核心职责：
 * - 定义单位姿态和动作状态（站立、坐下、死亡等）
 * - 定义单位标志位（不可攻击、免疫、沉默、眩晕等）
 * - 定义 NPC 功能标志（商人、训练师、任务给予者等）
 * - 定义移动标志（行走、飞行、游泳、下落等）
 * - 定义战斗命中信息（暴击、招架、闪避、吸收等）
 * - 定义宠物和守护者的命令与反应状态
 */

#ifndef UnitDefines_h__
#define UnitDefines_h__

#include "Define.h"
#include "EnumFlag.h"
#include <string>

/**
 * @name 基础伤害和攻击时间常量
 * @{
 */

/** @brief 单位基础最小伤害值 */
#define BASE_MINDAMAGE 1.0f

/** @brief 单位基础最大伤害值 */
#define BASE_MAXDAMAGE 2.0f

/** @brief 基础攻击时间间隔（毫秒），即 2 秒 */
#define BASE_ATTACK_TIME 2000

/** @} */

/**
 * @brief 单位装备槽位数量
 *
 * 定义单位可以装备的物品槽位数量，包括主手、副手和远程武器槽位。
 */
#define MAX_EQUIPMENT_ITEMS 3

/**
 * @brief 单位姿态状态类型
 *
 * 定义单位的各种站立、坐姿、睡眠、死亡等姿态状态。
 * 存储在 UNIT_FIELD_BYTES_1 字段的第 0 字节位置。
 *
 * @note 姿态状态会影响单位的动画表现、交互能力和部分游戏机制。
 */
enum UnitStandStateType : uint8
{
    UNIT_STAND_STATE_STAND             = 0,  ///< 站立状态（默认姿态）
    UNIT_STAND_STATE_SIT               = 1,  ///< 坐在地上
    UNIT_STAND_STATE_SIT_CHAIR         = 2,  ///< 坐在椅子上
    UNIT_STAND_STATE_SLEEP             = 3,  ///< 睡眠状态
    UNIT_STAND_STATE_SIT_LOW_CHAIR     = 4,  ///< 坐在低椅子上
    UNIT_STAND_STATE_SIT_MEDIUM_CHAIR  = 5,  ///< 坐在中等高度的椅子上
    UNIT_STAND_STATE_SIT_HIGH_CHAIR    = 6,  ///< 坐在高椅子上
    UNIT_STAND_STATE_DEAD              = 7,  ///< 死亡状态
    UNIT_STAND_STATE_KNEEL             = 8,  ///< 跪下状态
    UNIT_STAND_STATE_SUBMERGED         = 9,  ///< 潜入水中状态

    MAX_UNIT_STAND_STATE                     ///< 姿态状态数量上限
};

/**
 * @brief 单位视觉标志
 *
 * 定义影响单位视觉表现和可追踪性的标志位。
 * 存储在 UNIT_FIELD_BYTES_1 字段的第 2 字节位置。
 */
enum UnitVisFlags : uint8
{
    UNIT_VIS_FLAGS_UNK1         = 0x01,  ///< 未知标志 1
    UNIT_VIS_FLAGS_CREEP        = 0x02,  ///< 蹲伏/潜行移动视觉效果
    UNIT_VIS_FLAGS_UNTRACKABLE  = 0x04,  ///< 无法被追踪（不显示追踪标记）
    UNIT_VIS_FLAGS_UNK4         = 0x08,  ///< 未知标志 4
    UNIT_VIS_FLAGS_UNK5         = 0x10,  ///< 未知标志 5
    UNIT_VIS_FLAGS_ALL          = 0xFF   ///< 所有视觉标志的掩码
};

/**
 * @brief UNIT_FIELD_BYTES_0 字段的字节偏移量
 *
 * UNIT_FIELD_BYTES_0 是一个 4 字节字段，按字节存储单位的基础属性。
 * - 字节 0：种族（Race）
 * - 字节 1：职业（Class）
 * - 字节 2：性别（Gender）
 * - 字节 3：能量类型（Power Type）
 */
enum UnitBytes0Offsets : uint8
{
    UNIT_BYTES_0_OFFSET_RACE        = 0,  ///< 种族偏移位置
    UNIT_BYTES_0_OFFSET_CLASS       = 1,  ///< 职业偏移位置
    UNIT_BYTES_0_OFFSET_GENDER      = 2,  ///< 性别偏移位置
    UNIT_BYTES_0_OFFSET_POWER_TYPE  = 3,  ///< 能量类型偏移位置（法力、怒气、能量等）
};

/**
 * @brief UNIT_FIELD_BYTES_1 字段的字节偏移量
 *
 * UNIT_FIELD_BYTES_1 是一个 4 字节字段，存储单位的姿态和动画相关信息。
 * - 字节 0：姿态状态（Stand State）
 * - 字节 1：宠物天赋点数（Pet Talents）
 * - 字节 2：视觉标志（Visibility Flags）
 * - 字节 3：动画层级（Animation Tier）
 */
enum UnitBytes1Offsets : uint8
{
    UNIT_BYTES_1_OFFSET_STAND_STATE = 0,  ///< 姿态状态偏移位置（站立、坐下等）
    UNIT_BYTES_1_OFFSET_PET_TALENTS = 1,  ///< 宠物天赋点数偏移位置
    UNIT_BYTES_1_OFFSET_VIS_FLAG    = 2,  ///< 视觉标志偏移位置
    UNIT_BYTES_1_OFFSET_ANIM_TIER   = 3   ///< 动画层级偏移位置（地面、飞行等）
};

/**
 * @brief UNIT_FIELD_BYTES_2 字段的字节偏移量
 *
 * UNIT_FIELD_BYTES_2 是一个 4 字节字段，存储单位的武器姿态、PVP 状态和变形形态。
 * - 字节 0：武器收拔状态（Sheath State）
 * - 字节 1：PVP 标志（PvP Flag）
 * - 字节 2：宠物标志（Pet Flags）
 * - 字节 3：变形形态（Shapeshift Form）
 */
enum UnitBytes2Offsets : uint8
{
    UNIT_BYTES_2_OFFSET_SHEATH_STATE    = 0,  ///< 武器收拔状态偏移位置
    UNIT_BYTES_2_OFFSET_PVP_FLAG        = 1,  ///< PVP 标志偏移位置
    UNIT_BYTES_2_OFFSET_PET_FLAGS       = 2,  ///< 宠物标志偏移位置
    UNIT_BYTES_2_OFFSET_SHAPESHIFT_FORM = 3   ///< 变形形态偏移位置
};

/**
 * @brief 动画层级枚举
 *
 * 定义单位的动画播放层级，影响单位在不同环境下的动画表现。
 * 存储在 UNIT_FIELD_BYTES_1 字段的第 3 字节位置（UNIT_BYTES_1_OFFSET_ANIM_TIER）。
 *
 * @note 客户端根据此值选择相应的动画资源，层级切换时会触发过渡动画。
 */
enum class AnimTier : uint8
{
    Ground      = 0,  ///< 地面层级：播放地面动画
    Swim        = 1,  ///< 游泳层级：回退到地面动画，客户端不处理此值，不应出现在抓包数据中，使用会导致层级切换动画异常
    Hover       = 2,  ///< 悬浮层级：播放飞行动画或回退到地面动画，进入可见范围时自动启用客户端悬浮
    Fly         = 3,  ///< 飞行层级：播放飞行动画
    Submerged   = 4,  ///< 潜水层级：水下状态

    Max               ///< 动画层级数量上限
};

/**
 * @brief 武器收拔状态枚举
 *
 * 定义单位武器的收起和拔出状态。
 * 存储在 UNIT_FIELD_BYTES_2 字段的第 0 字节位置（低位字节）。
 *
 * @note 此状态影响单位的武器显示和战斗姿态动画。
 */
enum SheathState : uint8
{
    SHEATH_STATE_UNARMED  = 0,  ///< 收起武器状态：武器未准备，显示收起状态
    SHEATH_STATE_MELEE    = 1,  ///< 近战武器状态：近战武器已准备，显示近战姿态
    SHEATH_STATE_RANGED   = 2,  ///< 远程武器状态：远程武器已准备，显示远程姿态

    MAX_SHEATH_STATE           ///< 武器状态数量上限
};

/**
 * @brief 单位 PVP 状态标志
 *
 * 定义单位的 PVP 相关状态标志，影响玩家间的战斗规则。
 * 存储在 UNIT_FIELD_BYTES_2 字段的第 1 字节位置。
 *
 * @note 这些标志控制 PVP 模式的开启、安全区判定和自由 PVP 状态。
 */
enum UnitPVPStateFlags : uint8
{
    UNIT_BYTE2_FLAG_NONE        = 0x00,  ///< 无标志
    UNIT_BYTE2_FLAG_PVP         = 0x01,  ///< PVP 模式开启，可被敌对阵营玩家攻击
    UNIT_BYTE2_FLAG_UNK1        = 0x02,  ///< 未知标志 1
    UNIT_BYTE2_FLAG_FFA_PVP     = 0x04,  ///< 自由 PVP 模式（FFA），可被任何玩家攻击
    UNIT_BYTE2_FLAG_SANCTUARY   = 0x08,  ///< 圣所状态，无法进行 PVP 战斗
    UNIT_BYTE2_FLAG_UNK4        = 0x10,  ///< 未知标志 4
    UNIT_BYTE2_FLAG_UNK5        = 0x20,  ///< 未知标志 5
    UNIT_BYTE2_FLAG_UNK6        = 0x40,  ///< 未知标志 6
    UNIT_BYTE2_FLAG_UNK7        = 0x80   ///< 未知标志 7
};

DEFINE_ENUM_FLAG(UnitPVPStateFlags);

/**
 * @brief 单位宠物标志
 *
 * 定义宠物单位的特殊标志，控制宠物是否可以重命名或被遗弃。
 * 存储在 UNIT_FIELD_BYTES_2 字段的第 2 字节位置。
 *
 * @note 这些标志通常用于猎人宠物和术士宠物，影响宠物管理功能。
 */
enum UnitPetFlag : uint8
{
    UNIT_PET_FLAG_NONE              = 0x0,   ///< 无标志
    UNIT_PET_FLAG_CAN_BE_RENAMED    = 0x01,  ///< 宠物可以被重命名
    UNIT_PET_FLAG_CAN_BE_ABANDONED  = 0x02   ///< 宠物可以被遗弃
};

DEFINE_ENUM_FLAG(UnitPetFlag);

/**
 * @brief 单位标志位（UNIT_FIELD_FLAGS）
 *
 * 定义单位的核心状态标志，控制单位的行为、交互和视觉效果。
 * 这些标志存储在单位数据字段 UNIT_FIELD_FLAGS 中，使用位掩码组合。
 *
 * @note 该枚举定义了单位的核心控制标志，影响战斗、移动、交互等多个方面。
 *       某些标志由服务器自动管理，某些由客户端控制。
 *       参考 Unit::IsValidAttackTarget 和 Unit::IsValidAssistTarget 了解免疫机制。
 */
enum UnitFlags : uint32
{
    UNIT_FLAG_SERVER_CONTROLLED     = 0x00000001,  ///< 服务器控制移动：由服务器通过 SPLINE/MONSTER_MOVE 数据包控制移动，通常与眩晕标志一起使用
    UNIT_FLAG_NON_ATTACKABLE        = 0x00000002,  ///< 不可攻击：单位无法被攻击，通常在施放生成法术（SPELL_EFFECT_SPAWN）时使用
    UNIT_FLAG_REMOVE_CLIENT_CONTROL = 0x00000004,  ///< 移除客户端控制：旧版标志，用于禁用玩家控制其他单位时的移动，现由 SMSG_CLIENT_CONTROL 替代
    UNIT_FLAG_PLAYER_CONTROLLED     = 0x00000008,  ///< 玩家控制：单位由玩家控制，对玩家免疫时使用 IMMUNE_TO_PC 而非 IMMUNE_TO_NPC
    UNIT_FLAG_RENAME                = 0x00000010,  ///< 可重命名：单位（宠物）可以被重命名
    UNIT_FLAG_PREPARATION           = 0x00000020,  ///< 准备阶段：施法不消耗法术材料（SPELL_ATTR5_NO_REAGENT_WHILE_PREP）
    UNIT_FLAG_UNK_6                 = 0x00000040,  ///< 未知标志 6
    UNIT_FLAG_NOT_ATTACKABLE_1      = 0x00000080,  ///< 不可攻击标志 1：与 PLAYER_CONTROLLED 组合时表示非 PVP 可攻击状态
    UNIT_FLAG_IMMUNE_TO_PC          = 0x00000100,  ///< 对玩家角色免疫：禁用与玩家角色的战斗和协助
    UNIT_FLAG_IMMUNE_TO_NPC         = 0x00000200,  ///< 对 NPC 免疫：禁用与非玩家角色的战斗和协助
    UNIT_FLAG_LOOTING               = 0x00000400,  ///< 拾取中：播放拾取动画
    UNIT_FLAG_PET_IN_COMBAT         = 0x00000800,  ///< 宠物战斗中：玩家宠物正在追击攻击目标，或其他单位的任何随从在战斗中
    UNIT_FLAG_PVP_ENABLING          = 0x00001000,  ///< PVP 启用：3.0.3 版本后改用 UNIT_BYTES_2_OFFSET_PVP_FLAG
    UNIT_FLAG_SILENCED              = 0x00002000,  ///< 沉默状态：无法施放法术（2.1.1 版本）
    UNIT_FLAG_CANNOT_SWIM           = 0x00004000,  ///< 无法游泳：单位不能进入水中（2.0.8 版本）
    UNIT_FLAG_CAN_SWIM              = 0x00008000,  ///< 可以游泳：在水中显示游泳动画
    UNIT_FLAG_NON_ATTACKABLE_2      = 0x00010000,  ///< 不可攻击标志 2：移除攻击图标，对自己无法协助但可施放自身目标法术（SPELL_AURA_MOD_UNATTACKABLE）
    UNIT_FLAG_PACIFIED              = 0x00020000,  ///< 平静状态：无法进行攻击（3.0.3 版本）
    UNIT_FLAG_STUNNED               = 0x00040000,  ///< 眩晕状态：单位被眩晕，无法移动或行动（3.0.3 版本）
    UNIT_FLAG_IN_COMBAT             = 0x00080000,  ///< 战斗状态：单位正在战斗中
    UNIT_FLAG_ON_TAXI               = 0x00100000,  ///< 乘坐飞行坐骑：客户端禁用不允许在飞行中施放的法术
    UNIT_FLAG_DISARMED              = 0x00200000,  ///< 缴械状态：无法施放近战法术，近战法术提示"需要近战武器"（3.0.3 版本）
    UNIT_FLAG_CONFUSED             = 0x00400000,  ///< 迷惑状态：单位移动不受控制
    UNIT_FLAG_FLEEING               = 0x00800000,  ///< 逃跑状态：单位正在逃跑
    UNIT_FLAG_POSSESSED             = 0x01000000,  ///< 附身状态：单位由玩家直接控制（附身或载具）
    UNIT_FLAG_UNINTERACTIBLE        = 0x02000000,  ///< 不可交互：无法与单位交互
    UNIT_FLAG_SKINNABLE             = 0x04000000,  ///< 可剥皮：单位尸体可以剥皮
    UNIT_FLAG_MOUNT                 = 0x08000000,  ///< 骑乘状态：单位正在骑乘坐骑
    UNIT_FLAG_UNK_28                = 0x10000000,  ///< 未知标志 28
    UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT = 0x20000000,  ///< 阻止聊天文本表情：自动禁用解析聊天文本时触发的表情（如"lol"、以?或!结尾的消息）
    UNIT_FLAG_SHEATHE               = 0x40000000,  ///< 收起武器：武器处于收起状态
    UNIT_FLAG_IMMUNE                = 0x80000000,  ///< 免疫伤害：单位免疫所有伤害

    /// @brief 不允许的标志组合：定义服务器不应主动设置的标志位
    UNIT_FLAG_DISALLOWED            = (UNIT_FLAG_SERVER_CONTROLLED | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_REMOVE_CLIENT_CONTROL |
                                       UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_RENAME | UNIT_FLAG_PREPARATION | /* UNIT_FLAG_UNK_6 | */
                                       UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_LOOTING | UNIT_FLAG_PET_IN_COMBAT | UNIT_FLAG_PVP_ENABLING |
                                       UNIT_FLAG_SILENCED | UNIT_FLAG_NON_ATTACKABLE_2 | UNIT_FLAG_PACIFIED | UNIT_FLAG_STUNNED |
                                       UNIT_FLAG_IN_COMBAT | UNIT_FLAG_ON_TAXI | UNIT_FLAG_DISARMED | UNIT_FLAG_CONFUSED | UNIT_FLAG_FLEEING |
                                       UNIT_FLAG_POSSESSED | UNIT_FLAG_SKINNABLE | UNIT_FLAG_MOUNT | UNIT_FLAG_UNK_28 |
                                       UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT | UNIT_FLAG_SHEATHE | UNIT_FLAG_IMMUNE), // SKIP

    /// @brief 允许的标志组合：定义服务器可以安全设置的标志位
    UNIT_FLAG_ALLOWED               = (0xFFFFFFFF & ~UNIT_FLAG_DISALLOWED)  // SKIP
};

DEFINE_ENUM_FLAG(UnitFlags);

/**
 * @brief 单位标志位第二组（UNIT_FIELD_FLAGS_2）
 *
 * 定义单位的扩展状态标志，提供 UnitFlags 之外的额外控制功能。
 * 这些标志存储在单位数据字段 UNIT_FIELD_FLAGS_2 中。
 *
 * @note 该枚举提供了更细粒度的单位控制，包括假死、镜像、武器缴械等功能。
 */
enum UnitFlags2 : uint32
{
    UNIT_FLAG2_FEIGN_DEATH                  = 0x00000001,  ///< 假死状态：单位假装死亡
    UNIT_FLAG2_HIDE_BODY                    = 0x00000002,  ///< 隐藏身体：隐藏单位模型（仅显示玩家装备）
    UNIT_FLAG2_IGNORE_REPUTATION            = 0x00000004,  ///< 忽略声望：忽略声望限制
    UNIT_FLAG2_COMPREHEND_LANG              = 0x00000008,  ///< 理解语言：可以理解所有语言
    UNIT_FLAG2_MIRROR_IMAGE                 = 0x00000010,  ///< 镜像图像：单位是镜像图像（法师法术）
    UNIT_FLAG2_DO_NOT_FADE_IN               = 0x00000020,  ///< 不淡入：单位模型召唤时立即出现（无淡入效果）
    UNIT_FLAG2_FORCE_MOVEMENT               = 0x00000040,  ///< 强制移动：单位被强制移动
    UNIT_FLAG2_DISARM_OFFHAND               = 0x00000080,  ///< 缴械副手：禁用副手武器
    UNIT_FLAG2_DISABLE_PRED_STATS           = 0x00000100,  ///< 禁用预测属性：玩家禁用预测属性（用于团队框架）
    UNIT_FLAG2_UNK_1                        = 0x00000200,  ///< 未知标志 1
    UNIT_FLAG2_DISARM_RANGED                = 0x00000400,  ///< 缴械远程：禁用远程武器（不隐藏远程武器显示）
    UNIT_FLAG2_REGENERATE_POWER             = 0x00000800,  ///< 能量回复：单位能量自动回复
    UNIT_FLAG2_RESTRICT_PARTY_INTERACTION   = 0x00001000,  ///< 限制队伍交互：仅限队伍或团队交互
    UNIT_FLAG2_PREVENT_SPELL_CLICK          = 0x00002000,  ///< 阻止法术点击：禁用法术点击功能
    UNIT_FLAG2_ALLOW_ENEMY_INTERACT         = 0x00004000,  ///< 允许敌对交互：允许与敌对单位交互
    UNIT_FLAG2_CANNOT_TURN                  = 0x00008000,  ///< 无法转向：单位无法转向
    UNIT_FLAG2_UNK2                         = 0x00010000,  ///< 未知标志 2
    UNIT_FLAG2_PLAY_DEATH_ANIM              = 0x00020000,  ///< 播放死亡动画：死亡时播放特殊死亡动画
    UNIT_FLAG2_ALLOW_CHEAT_SPELLS           = 0x00040000,  ///< 允许作弊法术：允许施放作弊法术（SPELL_ATTR7_IS_CHEAT_SPELL）
    UNIT_FLAG2_UNUSED_1                     = 0x00080000,  ///< 未使用标志 1
    UNIT_FLAG2_UNUSED_2                     = 0x00100000,  ///< 未使用标志 2
    UNIT_FLAG2_UNUSED_3                     = 0x00200000,  ///< 未使用标志 3
    UNIT_FLAG2_UNUSED_4                     = 0x00400000,  ///< 未使用标志 4
    UNIT_FLAG2_UNUSED_5                     = 0x00800000,  ///< 未使用标志 5
    UNIT_FLAG2_UNUSED_6                     = 0x01000000,  ///< 未使用标志 6
    UNIT_FLAG2_UNUSED_7                     = 0x02000000,  ///< 未使用标志 7
    UNIT_FLAG2_UNUSED_8                     = 0x04000000,  ///< 未使用标志 8
    UNIT_FLAG2_UNUSED_9                     = 0x08000000,  ///< 未使用标志 9
    UNIT_FLAG2_UNUSED_10                    = 0x10000000,  ///< 未使用标志 10
    UNIT_FLAG2_UNUSED_11                    = 0x20000000,  ///< 未使用标志 11
    UNIT_FLAG2_UNUSED_12                    = 0x40000000,  ///< 未使用标志 12
    UNIT_FLAG2_UNUSED_13                    = 0x80000000,  ///< 未使用标志 13

    /// @brief 不允许的标志组合：定义服务器不应主动设置的 UnitFlags2 标志位
    UNIT_FLAG2_DISALLOWED                   = (UNIT_FLAG2_FEIGN_DEATH | UNIT_FLAG2_IGNORE_REPUTATION | UNIT_FLAG2_COMPREHEND_LANG |
                                               UNIT_FLAG2_MIRROR_IMAGE | UNIT_FLAG2_FORCE_MOVEMENT | UNIT_FLAG2_DISARM_OFFHAND |
                                               UNIT_FLAG2_DISABLE_PRED_STATS | UNIT_FLAG2_UNK_1 | UNIT_FLAG2_DISARM_RANGED |
                                            /* UNIT_FLAG2_REGENERATE_POWER | */ UNIT_FLAG2_RESTRICT_PARTY_INTERACTION |
                                               UNIT_FLAG2_PREVENT_SPELL_CLICK | UNIT_FLAG2_ALLOW_ENEMY_INTERACT | /* UNIT_FLAG2_UNK2 | */
                                            /* UNIT_FLAG2_PLAY_DEATH_ANIM | */ UNIT_FLAG2_ALLOW_CHEAT_SPELLS | UNIT_FLAG2_UNUSED_1 |
                                               UNIT_FLAG2_UNUSED_2 | UNIT_FLAG2_UNUSED_3 | UNIT_FLAG2_UNUSED_4 | UNIT_FLAG2_UNUSED_5 |
                                               UNIT_FLAG2_UNUSED_6 | UNIT_FLAG2_UNUSED_7 | UNIT_FLAG2_UNUSED_8 | UNIT_FLAG2_UNUSED_9 |
                                               UNIT_FLAG2_UNUSED_10 | UNIT_FLAG2_UNUSED_11 | UNIT_FLAG2_UNUSED_12 | UNIT_FLAG2_UNUSED_13), // SKIP

    /// @brief 允许的标志组合：定义服务器可以安全设置的 UnitFlags2 标志位
    UNIT_FLAG2_ALLOWED                      = (0xFFFFFFFF & ~UNIT_FLAG2_DISALLOWED) // SKIP
};

DEFINE_ENUM_FLAG(UnitFlags2);

/**
 * @brief NPC（非玩家角色）功能标志
 *
 * 定义 NPC 可提供的交互功能和服务类型。
 * 这些标志控制玩家与 NPC 交互时可用的菜单和功能。
 *
 * @note NPC 可以同时拥有多个标志，以提供多种服务（如商人兼训练师）。
 *       客户端根据这些标志显示相应的交互选项和图标。
 */
enum NPCFlags : uint32
{
    UNIT_NPC_FLAG_NONE                  = 0x00000000,  ///< 无 NPC 功能
    UNIT_NPC_FLAG_GOSSIP                = 0x00000001,  ///< 对话菜单：NPC 拥有对话菜单，可进行对话交互
    UNIT_NPC_FLAG_QUESTGIVER            = 0x00000002,  ///< 任务给予者：NPC 可以提供任务
    UNIT_NPC_FLAG_UNK1                  = 0x00000004,  ///< 未知标志 1
    UNIT_NPC_FLAG_UNK2                  = 0x00000008,  ///< 未知标志 2
    UNIT_NPC_FLAG_TRAINER               = 0x00000010,  ///< 训练师：NPC 可以训练技能
    UNIT_NPC_FLAG_TRAINER_CLASS         = 0x00000020,  ///< 职业训练师：NPC 是职业训练师
    UNIT_NPC_FLAG_TRAINER_PROFESSION    = 0x00000040,  ///< 专业训练师：NPC 是专业技能训练师
    UNIT_NPC_FLAG_VENDOR                = 0x00000080,  ///< 商人（通用）：NPC 是通用商人
    UNIT_NPC_FLAG_VENDOR_AMMO           = 0x00000100,  ///< 弹药商人：NPC 出售弹药，通常也是杂货商人
    UNIT_NPC_FLAG_VENDOR_FOOD           = 0x00000200,  ///< 食物商人：NPC 出售食物和饮料
    UNIT_NPC_FLAG_VENDOR_POISON         = 0x00000400,  ///< 毒药商人：NPC 出售毒药（推测）
    UNIT_NPC_FLAG_VENDOR_REAGENT        = 0x00000800,  ///< 施法材料商人：NPC 出售施法材料
    UNIT_NPC_FLAG_REPAIR                = 0x00001000,  ///< 修理商：NPC 可以修理装备
    UNIT_NPC_FLAG_FLIGHTMASTER          = 0x00002000,  ///< 飞行管理员：NPC 提供飞行路径服务
    UNIT_NPC_FLAG_SPIRITHEALER          = 0x00004000,  ///< 灵魂医者：NPC 是灵魂医者，可复活玩家（推测）
    UNIT_NPC_FLAG_SPIRITGUIDE           = 0x00008000,  ///< 灵魂向导：NPC 是灵魂向导（推测）
    UNIT_NPC_FLAG_INNKEEPER             = 0x00010000,  ///< 旅店老板：NPC 是旅店老板，可设置炉石绑定
    UNIT_NPC_FLAG_BANKER                = 0x00020000,  ///< 银行职员：NPC 提供银行服务
    UNIT_NPC_FLAG_PETITIONER            = 0x00040000,  ///< 公会/竞技场登记员：NPC 处理公会和竞技场队伍申请（0xC0000 = 公会，0x40000 = 竞技场）
    UNIT_NPC_FLAG_TABARDDESIGNER        = 0x00080000,  ///< 公会战袍设计师：NPC 提供公会战袍设计服务
    UNIT_NPC_FLAG_BATTLEMASTER          = 0x00100000,  ///< 战场军官：NPC 提供战场排队服务
    UNIT_NPC_FLAG_AUCTIONEER            = 0x00200000,  ///< 拍卖师：NPC 提供拍卖行服务
    UNIT_NPC_FLAG_STABLEMASTER          = 0x00400000,  ///< 兽栏管理员：NPC 提供宠物存放服务
    UNIT_NPC_FLAG_GUILD_BANKER          = 0x00800000,  ///< 公会银行职员：NPC 提供公会银行服务（客户端发送 997 操作码）
    UNIT_NPC_FLAG_SPELLCLICK            = 0x01000000,  ///< 法术点击：NPC 启用法术点击功能（客户端发送 1015 操作码）
    UNIT_NPC_FLAG_PLAYER_VEHICLE        = 0x02000000,  ///< 玩家载具：拥有载具数据的玩家坐骑应设置此标志
    UNIT_NPC_FLAG_MAILBOX               = 0x04000000   ///< 邮箱：NPC 充当邮箱功能
};

DEFINE_ENUM_FLAG(NPCFlags);

/**
 * @brief 移动标志枚举
 *
 * 定义单位的移动状态和移动方式标志，控制单位在游戏世界中的移动行为。
 * 这些标志用于描述单位的移动方向、移动方式和特殊移动状态。
 *
 * @note 移动标志组合使用位掩码，可以同时设置多个标志来描述复杂的移动状态。
 *       某些标志互斥（如 MOVEMENTFLAG_ROOT 不能与 MOVEMENTFLAG_MASK_MOVING 同时设置）。
 */
enum MovementFlags : uint32
{
    MOVEMENTFLAG_NONE                  = 0x00000000,  ///< 无移动标志
    MOVEMENTFLAG_FORWARD               = 0x00000001,  ///< 向前移动
    MOVEMENTFLAG_BACKWARD              = 0x00000002,  ///< 向后移动
    MOVEMENTFLAG_STRAFE_LEFT           = 0x00000004,  ///< 向左平移
    MOVEMENTFLAG_STRAFE_RIGHT          = 0x00000008,  ///< 向右平移
    MOVEMENTFLAG_LEFT                  = 0x00000010,  ///< 向左转向
    MOVEMENTFLAG_RIGHT                 = 0x00000020,  ///< 向右转向
    MOVEMENTFLAG_PITCH_UP              = 0x00000040,  ///< 向上俯仰
    MOVEMENTFLAG_PITCH_DOWN            = 0x00000080,  ///< 向下俯仰
    MOVEMENTFLAG_WALKING               = 0x00000100,  ///< 行走模式：单位正在行走（非跑步）
    MOVEMENTFLAG_ONTRANSPORT           = 0x00000200,  ///< 在载具上：单位位于载具上，也用于某些生物飞行
    MOVEMENTFLAG_DISABLE_GRAVITY       = 0x00000400,  ///< 禁用重力：单位不受重力影响（原 MOVEMENTFLAG_LEVITATING），用于无法行走的情况
    MOVEMENTFLAG_ROOT                  = 0x00000800,  ///< 定身：单位被定身，无法移动（不得与 MOVEMENTFLAG_MASK_MOVING 同时设置）
    MOVEMENTFLAG_FALLING               = 0x00001000,  ///< 下落中：单位正在下落，此类型下落会造成伤害
    MOVEMENTFLAG_FALLING_FAR           = 0x00002000,  ///< 远距离下落：单位正在从高处下落
    MOVEMENTFLAG_PENDING_STOP          = 0x00004000,  ///< 待停止：准备停止移动
    MOVEMENTFLAG_PENDING_STRAFE_STOP   = 0x00008000,  ///< 待停止平移：准备停止平移
    MOVEMENTFLAG_PENDING_FORWARD       = 0x00010000,  ///< 待向前移动：准备向前移动
    MOVEMENTFLAG_PENDING_BACKWARD      = 0x00020000,  ///< 待向后移动：准备向后移动
    MOVEMENTFLAG_PENDING_STRAFE_LEFT   = 0x00040000,  ///< 待向左平移：准备向左平移
    MOVEMENTFLAG_PENDING_STRAFE_RIGHT  = 0x00080000,  ///< 待向右平移：准备向右平移
    MOVEMENTFLAG_PENDING_ROOT          = 0x00100000,  ///< 待定身：准备进入定身状态
    MOVEMENTFLAG_SWIMMING              = 0x00200000,  ///< 游泳中：单位正在游泳，也可与飞行标志一起出现
    MOVEMENTFLAG_ASCENDING             = 0x00400000,  ///< 上升中：单位正在上升（飞行或游泳时按空格键）
    MOVEMENTFLAG_DESCENDING            = 0x00800000,  ///< 下降中：单位正在下降
    MOVEMENTFLAG_CAN_FLY               = 0x01000000,  ///< 可以飞行：单位可以飞行并且可以行走
    MOVEMENTFLAG_FLYING                = 0x02000000,  ///< 正在飞行：单位实际正在飞行（仅用于玩家，生物使用 disable_gravity）
    MOVEMENTFLAG_SPLINE_ELEVATION      = 0x04000000,  ///< 样条高度：用于飞行路径
    MOVEMENTFLAG_SPLINE_ENABLED        = 0x08000000,  ///< 样条启用：用于飞行路径
    MOVEMENTFLAG_WATERWALKING          = 0x10000000,  ///< 水上行走：单位可以在水面上行走
    MOVEMENTFLAG_FALLING_SLOW          = 0x20000000,  ///< 缓慢下落：盗贼安全降落被动技能激活
    MOVEMENTFLAG_HOVER                 = 0x40000000,  ///< 悬浮：单位悬浮，无法跳跃

    /// @brief 移动中的标志掩码：包含所有表示单位正在移动的标志
    MOVEMENTFLAG_MASK_MOVING =
        MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD | MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT |
        MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING |
        MOVEMENTFLAG_SPLINE_ELEVATION,

    /// @brief 转向的标志掩码：包含所有表示单位正在转向的标志
    MOVEMENTFLAG_MASK_TURNING =
        MOVEMENTFLAG_LEFT | MOVEMENTFLAG_RIGHT | MOVEMENTFLAG_PITCH_UP | MOVEMENTFLAG_PITCH_DOWN,

    /// @brief 飞行移动的标志掩码：包含飞行相关的移动标志
    MOVEMENTFLAG_MASK_MOVING_FLY =
        MOVEMENTFLAG_FLYING | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING,

    /// @brief 仅玩家的标志掩码：玩家专属的移动标志（待扩展）
    MOVEMENTFLAG_MASK_PLAYER_ONLY =
        MOVEMENTFLAG_FLYING,

    /// @brief 玩家状态操作码标志掩码：具有状态改变操作码的移动标志
    MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE = MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_ROOT |
        MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_WATERWALKING | MOVEMENTFLAG_FALLING_SLOW | MOVEMENTFLAG_HOVER
};

/**
 * @brief 移动标志第二组（MovementFlags2）
 *
 * 定义扩展的移动控制标志，提供 MovementFlags 之外的额外移动控制功能。
 * 这些标志控制更细粒度的移动行为，如禁止平移、禁止跳跃、插值移动等。
 *
 * @note 这些标志主要用于特殊移动模式和载具控制。
 */
enum MovementFlags2 : uint32
{
    MOVEMENTFLAG2_NONE                                  = 0x00000000,  ///< 无标志
    MOVEMENTFLAG2_NO_STRAFE                             = 0x00000001,  ///< 禁止平移：单位无法进行左右平移
    MOVEMENTFLAG2_NO_JUMPING                            = 0x00000002,  ///< 禁止跳跃：单位无法跳跃
    MOVEMENTFLAG2_UNK3                                  = 0x00000004,  ///< 未知标志 3：覆盖各种客户端检查
    MOVEMENTFLAG2_FULL_SPEED_TURNING                    = 0x00000008,  ///< 全速转向：单位可以全速转向
    MOVEMENTFLAG2_FULL_SPEED_PITCHING                   = 0x00000010,  ///< 全速俯仰：单位可以全速俯仰
    MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING                 = 0x00000020,  ///< 始终允许俯仰：单位始终可以俯仰
    MOVEMENTFLAG2_UNK7                                  = 0x00000040,  ///< 未知标志 7
    MOVEMENTFLAG2_UNK8                                  = 0x00000080,  ///< 未知标志 8
    MOVEMENTFLAG2_UNK9                                  = 0x00000100,  ///< 未知标志 9
    MOVEMENTFLAG2_UNK10                                 = 0x00000200,  ///< 未知标志 10
    MOVEMENTFLAG2_INTERPOLATED_MOVEMENT                 = 0x00000400,  ///< 插值移动：使用插值进行移动
    MOVEMENTFLAG2_INTERPOLATED_TURNING                  = 0x00000800,  ///< 插值转向：使用插值进行转向
    MOVEMENTFLAG2_INTERPOLATED_PITCHING                 = 0x00001000,  ///< 插值俯仰：使用插值进行俯仰
    MOVEMENTFLAG2_UNK14                                 = 0x00002000,  ///< 未知标志 14
    MOVEMENTFLAG2_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY   = 0x00004000,  ///< 游泳飞行过渡：可以在游泳和飞行之间平滑过渡
    MOVEMENTFLAG2_UNK16                                 = 0x00008000   ///< 未知标志 16
};

/**
 * @brief 单位移动类型枚举
 *
 * 定义单位在不同情况下的移动速度类型。
 * 每种移动类型对应一个基础速度值，可被光环、技能等修改。
 */
enum UnitMoveType
{
    MOVE_WALK           = 0,  ///< 行走速度
    MOVE_RUN            = 1,  ///< 跑步速度（默认移动速度）
    MOVE_RUN_BACK       = 2,  ///< 后退速度
    MOVE_SWIM           = 3,  ///< 游泳速度
    MOVE_SWIM_BACK      = 4,  ///< 后退游泳速度
    MOVE_TURN_RATE      = 5,  ///< 转向速度
    MOVE_FLIGHT         = 6,  ///< 飞行速度
    MOVE_FLIGHT_BACK    = 7,  ///< 后退飞行速度
    MOVE_PITCH_RATE     = 8   ///< 俯仰速度
};

/// @brief 移动类型数量上限
#define MAX_MOVE_TYPE     9

/**
 * @brief 命中信息标志枚举
 *
 * 定义战斗中攻击命中的各种状态和结果标志。
 * 这些标志用于描述攻击的类型、结果和视觉效果。
 *
 * @note 命中信息组合使用位掩码，可以同时表示多种状态（如暴击 + 部分吸收）。
 *       客户端根据这些标志播放相应的动画和音效。
 */
enum HitInfo
{
    HITINFO_NORMALSWING         = 0x00000000,  ///< 普通攻击：正常近战攻击
    HITINFO_UNK1                = 0x00000001,  ///< 未知标志 1：需要正确的数据包结构
    HITINFO_AFFECTS_VICTIM      = 0x00000002,  ///< 影响目标：攻击对目标产生效果
    HITINFO_OFFHAND             = 0x00000004,  ///< 副手攻击：攻击来自副手武器
    HITINFO_UNK2                = 0x00000008,  ///< 未知标志 2
    HITINFO_MISS                = 0x00000010,  ///< 未命中：攻击未命中目标
    HITINFO_FULL_ABSORB         = 0x00000020,  ///< 完全吸收：伤害被完全吸收
    HITINFO_PARTIAL_ABSORB      = 0x00000040,  ///< 部分吸收：伤害被部分吸收
    HITINFO_FULL_RESIST         = 0x00000080,  ///< 完全抵抗：伤害被完全抵抗
    HITINFO_PARTIAL_RESIST      = 0x00000100,  ///< 部分抵抗：伤害被部分抵抗
    HITINFO_CRITICALHIT         = 0x00000200,  ///< 暴击：攻击造成暴击伤害
    HITINFO_UNK10               = 0x00000400,  ///< 未知标志 10
    HITINFO_UNK11               = 0x00000800,  ///< 未知标志 11
    HITINFO_UNK12               = 0x00001000,  ///< 未知标志 12
    HITINFO_BLOCK               = 0x00002000,  ///< 格挡：攻击被格挡，减少伤害
    HITINFO_UNK14               = 0x00004000,  ///< 未知标志 14：仅当近战法术 ID 存在时设置，无伤害时不显示世界文本
    HITINFO_UNK15               = 0x00008000,  ///< 未知标志 15：玩家目标相关，与血液喷溅视觉效果有关
    HITINFO_GLANCING            = 0x00010000,  ///< 擦伤：攻击造成擦伤伤害（伤害降低）
    HITINFO_CRUSHING            = 0x00020000,  ///< 碾压：攻击造成碾压伤害（伤害增加）
    HITINFO_NO_ANIMATION        = 0x00040000,  ///< 无动画：不播放命中动画
    HITINFO_UNK19               = 0x00080000,  ///< 未知标志 19
    HITINFO_UNK20               = 0x00100000,  ///< 未知标志 20
    HITINFO_SWINGNOHITSOUND     = 0x00200000,  ///< 无命中音效：不播放命中音效（未使用？）
    HITINFO_UNK22               = 0x00400000,  ///< 未知标志 22
    HITINFO_RAGE_GAIN           = 0x00800000,  ///< 获得怒气：战士从攻击中获得怒气
    HITINFO_FAKE_DAMAGE         = 0x01000000   ///< 虚假伤害：即使未造成伤害也启用伤害动画，仅在无伤害时设置
};

/// @brief 名字变格数量上限（斯拉夫语系的格变化）
#define MAX_DECLINED_NAME_CASES 5

/**
 * @brief 名字变格结构体
 *
 * 存储名字在不同语法格中的变体形式。
 * 主要用于斯拉夫语系（俄语等）的名字格变化，包含主格、属格、与格、宾格、工具格等。
 *
 * @note 在英语等其他语言中，此结构体通常不使用或所有格位使用相同字符串。
 */
struct DeclinedName
{
    std::string name[MAX_DECLINED_NAME_CASES];  ///< 名字变格数组，索引对应不同的语法格
};

/**
 * @brief 宠物/守护者激活状态枚举
 *
 * 定义宠物或守护者技能的激活状态和控制模式。
 * 控制宠物技能的自动施放和手动施放行为。
 *
 * @note 这些状态控制宠物技能栏中技能的激活方式。
 */
enum ActiveStates : uint8
{
    ACT_PASSIVE  = 0x01,  ///< 被动技能：技能为被动效果，不显示在技能栏
    ACT_DISABLED = 0x81,  ///< 禁用状态：技能可施放但被禁用（0x80 表示可施放）
    ACT_ENABLED  = 0xC1,  ///< 启用状态：技能可施放且自动施放已启用（0x40 自动施放 + 0x80 可施放）
    ACT_COMMAND  = 0x07,  ///< 命令模式：技能为命令技能（0x01 | 0x02 | 0x04）
    ACT_REACTION = 0x06,  ///< 反应模式：技能为反应技能（0x02 | 0x04）
    ACT_DECIDE   = 0x00   ///< 自定义模式：自定义决策逻辑
};

/**
 * @brief 宠物/守护者反应状态枚举
 *
 * 定义宠物或守护者对敌对目标的自动反应行为。
 * 控制宠物如何自动响应周围的敌对单位。
 *
 * @note 反应状态影响宠物的自动攻击行为和仇恨管理。
 */
enum ReactStates : uint8
{
    REACT_PASSIVE    = 0,  ///< 被动模式：不会主动攻击任何目标
    REACT_DEFENSIVE  = 1,  ///< 防御模式：仅攻击攻击主人或自己的敌人
    REACT_AGGRESSIVE = 2   ///< 攻击模式：主动攻击视野内的敌对目标
};

/**
 * @brief 获取反应状态的描述字符串
 *
 * @param state 反应状态枚举值
 * @return const char* 状态的描述字符串
 *
 * @note 该函数用于日志记录和调试输出，返回人类可读的状态名称。
 */
inline char const* DescribeReactState(ReactStates state)
{
    switch (state)
    {
        case REACT_PASSIVE:     return "PASSIVE";
        case REACT_DEFENSIVE:   return "DEFENSIVE";
        case REACT_AGGRESSIVE:  return "AGGRESSIVE";
        default:                return "<Invalid react state>";
    }
}

enum CommandStates : uint8
{
    COMMAND_STAY    = 0,
    COMMAND_FOLLOW  = 1,
    COMMAND_ATTACK  = 2,
    COMMAND_ABANDON = 3
};

#endif // UnitDefines_h__
