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
 * @file BattlegroundEY.h
 * @brief 暴风之眼（Eye of the Storm）战场模块头文件
 *
 * 本文件定义了暴风之眼战场的具体实现，包括：
 * - 战场对象类型枚举（旗帜、基地、增益效果等）
 * - 游戏对象ID枚举
 * - 世界状态枚举
 * - 战场事件和音效枚举
 * - BattlegroundEY类定义
 *
 * 暴风之眼是一个15v15的战场，位于虚空风暴地区。
 * 玩家需要争夺四个基地（塔楼）并抢夺中央旗帜来获得分数。
 * 第一个达到1600分的阵营获胜。
 */

#ifndef __BATTLEGROUNDEY_H
#define __BATTLEGROUNDEY_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Object.h"

/**
 * @brief 暴风之眼杂项常量枚举
 *
 * 定义战场中使用的各种常量值
 */
enum BG_EY_Misc
{
    BG_EY_EVENT_START_BATTLE        = 13180,  ///< 战斗开始事件ID，用于成就"Flurry"
    BG_EY_FLAG_RESPAWN_TIME         = (8*IN_MILLISECONDS),  ///< 旗帜重生时间（8秒）
    BG_EY_FPOINTS_TICK_TIME         = (2*IN_MILLISECONDS)   ///< 基地分数tick间隔（2秒）
};

/**
 * @brief 暴风之眼世界状态枚举
 *
 * 定义用于客户端UI显示的世界状态ID
 * 这些状态用于控制分数显示、基地占领状态、旗帜状态等
 */
enum BG_EY_WorldStates
{
    EY_ALLIANCE_RESOURCES           = 2749,  ///< 联盟资源分数
    EY_HORDE_RESOURCES              = 2750,  ///< 部落资源分数
    EY_ALLIANCE_BASE                = 2752,  ///< 联盟占领基地数量
    EY_HORDE_BASE                   = 2753,  ///< 部落占领基地数量

    // 德莱尼遗迹占领状态
    DRAENEI_RUINS_HORDE_CONTROL     = 2733,  ///< 德莱尼遗迹 - 部落控制
    DRAENEI_RUINS_ALLIANCE_CONTROL  = 2732,  ///< 德莱尼遗迹 - 联盟控制
    DRAENEI_RUINS_UNCONTROL         = 2731,  ///< 德莱尼遗迹 - 未控制

    // 法师塔占领状态
    MAGE_TOWER_ALLIANCE_CONTROL     = 2730,  ///< 法师塔 - 联盟控制
    MAGE_TOWER_HORDE_CONTROL        = 2729,  ///< 法师塔 - 部落控制
    MAGE_TOWER_UNCONTROL            = 2728,  ///< 法师塔 - 未控制

    // 魔能机甲废墟占领状态
    FEL_REAVER_HORDE_CONTROL        = 2727,  ///< 魔能机甲废墟 - 部落控制
    FEL_REAVER_ALLIANCE_CONTROL     = 2726,  ///< 魔能机甲废墟 - 联盟控制
    FEL_REAVER_UNCONTROL            = 2725,  ///< 魔能机甲废墟 - 未控制

    // 血精灵塔占领状态
    BLOOD_ELF_HORDE_CONTROL         = 2724,  ///< 血精灵塔 - 部落控制
    BLOOD_ELF_ALLIANCE_CONTROL      = 2723,  ///< 血精灵塔 - 联盟控制
    BLOOD_ELF_UNCONTROL             = 2722,  ///< 血精灵塔 - 未控制

    // 进度条相关
    PROGRESS_BAR_PERCENT_GREY       = 2720,  ///< 进度条灰色百分比：100=空（全灰），0=蓝/红（无灰色）
    PROGRESS_BAR_STATUS             = 2719,  ///< 进度条状态：50=初始，48...部落占领，33...，0=部落完全控制，100=联盟完全控制
    PROGRESS_BAR_SHOW               = 2718,  ///< 进度条显示：1=初始/联盟控制，0=第二次发送（无消息）

    // 旗帜状态
    NETHERSTORM_FLAG                = 2757,  ///< 虚空风暴旗帜状态
    NETHERSTORM_FLAG_STATE_ALLIANCE = 2769,  ///< 旗帜状态 - 联盟：2=拾起，1=掉落
    NETHERSTORM_FLAG_STATE_HORDE    = 2770   ///< 旗帜状态 - 部落：2=拾起，1=掉落
};

/**
 * @brief 进度条常量枚举
 *
 * 定义基地占领进度条相关的常量值
 */
enum BG_EY_ProgressBarConsts
{
    BG_EY_POINT_MAX_CAPTURERS_COUNT     = 5,   ///< 基地最大占领者数量（影响占领速度）
    BG_EY_POINT_RADIUS                  = 70,  ///< 基地判定半径（码）
    BG_EY_PROGRESS_BAR_DONT_SHOW        = 0,   ///< 不显示进度条
    BG_EY_PROGRESS_BAR_SHOW             = 1,   ///< 显示进度条
    BG_EY_PROGRESS_BAR_PERCENT_GREY     = 40,  ///< 进度条灰色百分比
    BG_EY_PROGRESS_BAR_STATE_MIDDLE     = 50,  ///< 进度条中间状态（中立）
    BG_EY_PROGRESS_BAR_HORDE_CONTROLLED = 0,   ///< 部落完全控制状态
    BG_EY_PROGRESS_BAR_NEUTRAL_LOW      = 30,  ///< 中立状态（偏部落）
    BG_EY_PROGRESS_BAR_NEUTRAL_HIGH     = 70,  ///< 中立状态（偏联盟）
    BG_EY_PROGRESS_BAR_ALI_CONTROLLED   = 100  ///< 联盟完全控制状态
};

/**
 * @brief 音效枚举
 *
 * 定义战场中各种事件的音效ID
 */
enum BG_EY_Sounds
{
    // 注意：音效ID看起来很奇怪，但确认是正确的
    BG_EY_SOUND_FLAG_PICKED_UP_ALLIANCE = 8212,  ///< 联盟拾起旗帜音效
    BG_EY_SOUND_FLAG_CAPTURED_HORDE     = 8213,  ///< 部落夺取旗帜音效
    BG_EY_SOUND_FLAG_PICKED_UP_HORDE    = 8174,  ///< 部落拾起旗帜音效
    BG_EY_SOUND_FLAG_CAPTURED_ALLIANCE  = 8173,  ///< 联盟夺取旗帜音效
    BG_EY_SOUND_FLAG_RESET              = 8192   ///< 旗帜重置音效
};

/**
 * @brief 法术枚举
 *
 * 定义战场中使用的法术ID
 */
enum BG_EY_Spells
{
    BG_EY_NETHERSTORM_FLAG_SPELL        = 34976,  ///< 虚空风暴旗帜法术（携带旗帜）
    BG_EY_PLAYER_DROPPED_FLAG_SPELL     = 34991   ///< 玩家掉落旗帜法术
};

/**
 * @brief 游戏对象模板ID枚举
 *
 * 定义战场中各种游戏对象的模板ID，对应数据库gameobject_template表
 */
enum EYBattlegroundObjectEntry
{
    BG_OBJECT_A_DOOR_EY_ENTRY           = 184719,  ///< 联盟大门
    BG_OBJECT_H_DOOR_EY_ENTRY           = 184720,  ///< 部落大门
    BG_OBJECT_FLAG1_EY_ENTRY            = 184493,  ///< 虚空风暴旗帜（通用）
    BG_OBJECT_FLAG2_EY_ENTRY            = 184141,  ///< 虚空风暴旗帜（旗座）
    BG_OBJECT_FLAG3_EY_ENTRY            = 184142,  ///< 虚空风暴旗帜（掉落旗帜）
    BG_OBJECT_A_BANNER_EY_ENTRY         = 184381,  ///< 视觉旗帜（联盟）
    BG_OBJECT_H_BANNER_EY_ENTRY         = 184380,  ///< 视觉旗帜（部落）
    BG_OBJECT_N_BANNER_EY_ENTRY         = 184382,  ///< 视觉旗帜（中立）
    BG_OBJECT_BE_TOWER_CAP_EY_ENTRY     = 184080,  ///< 血精灵塔占领点
    BG_OBJECT_FR_TOWER_CAP_EY_ENTRY     = 184081,  ///< 魔能机甲废墟占领点
    BG_OBJECT_HU_TOWER_CAP_EY_ENTRY     = 184082,  ///< 人类塔占领点（法师塔）
    BG_OBJECT_DR_TOWER_CAP_EY_ENTRY     = 184083   ///< 德莱尼遗迹占领点
};

/**
 * @brief 区域触发器枚举
 *
 * 定义基地和增益效果的区域触发器ID
 */
enum EYBattlegroundPointsTrigger
{
    TR_BLOOD_ELF_POINT        = 4476,  ///< 血精灵塔基地触发器
    TR_FEL_REAVER_POINT       = 4514,  ///< 魔能机甲废墟基地触发器
    TR_MAGE_TOWER_POINT       = 4516,  ///< 法师塔基地触发器
    TR_DRAENEI_RUINS_POINT    = 4518,  ///< 德莱尼遗迹基地触发器
    TR_BLOOD_ELF_BUFF         = 4568,  ///< 血精灵塔增益触发器
    TR_FEL_REAVER_BUFF        = 4569,  ///< 魔能机甲废墟增益触发器
    TR_MAGE_TOWER_BUFF        = 4570,  ///< 法师塔增益触发器
    TR_DRAENEI_RUINS_BUFF     = 4571   ///< 德莱尼遗迹增益触发器
};

/**
 * @brief 墓地枚举
 *
 * 定义战场中各个墓地的ID
 */
enum EYBattlegroundGaveyards
{
    EY_GRAVEYARD_MAIN_ALLIANCE     = 1103,  ///< 联盟主墓地
    EY_GRAVEYARD_MAIN_HORDE        = 1104,  ///< 部落主墓地
    EY_GRAVEYARD_FEL_REAVER       = 1105,  ///< 魔能机甲废墟墓地
    EY_GRAVEYARD_BLOOD_ELF         = 1106,  ///< 血精灵塔墓地
    EY_GRAVEYARD_DRAENEI_RUINS     = 1107,  ///< 德莱尼遗迹墓地
    EY_GRAVEYARD_MAGE_TOWER        = 1108   ///< 法师塔墓地
};

/**
 * @brief 基地枚举
 *
 * 定义战场中四个基地的索引
 */
enum EYBattlegroundPoints
{
    FEL_REAVER     = 0,  ///< 魔能机甲废墟
    BLOOD_ELF       = 1,  ///< 血精灵塔
    DRAENEI_RUINS   = 2,  ///< 德莱尼遗迹
    MAGE_TOWER      = 3,  ///< 法师塔

    EY_PLAYERS_OUT_OF_POINTS  = 4,  ///< 玩家不在任何基地中
    EY_POINTS_MAX             = 4   ///< 基地总数
};

/**
 * @brief 生物类型枚举
 *
 * 定义战场中各种生物（灵魂医者、触发器）的索引
 */
enum EYBattlegroundCreaturesTypes
{
    EY_SPIRIT_FEL_REAVER      = 0,  ///< 魔能机甲废墟灵魂医者
    EY_SPIRIT_BLOOD_ELF        = 1,  ///< 血精灵塔灵魂医者
    EY_SPIRIT_DRAENEI_RUINS    = 2,  ///< 德莱尼遗迹灵魂医者
    EY_SPIRIT_MAGE_TOWER       = 3,  ///< 法师塔灵魂医者
    EY_SPIRIT_MAIN_ALLIANCE    = 4,  ///< 联盟主基地灵魂医者
    EY_SPIRIT_MAIN_HORDE       = 5,  ///< 部落主基地灵魂医者

    EY_TRIGGER_FEL_REAVER      = 6,  ///< 魔能机甲废墟触发器
    EY_TRIGGER_BLOOD_ELF        = 7,  ///< 血精灵塔触发器
    EY_TRIGGER_DRAENEI_RUINS    = 8,  ///< 德莱尼遗迹触发器
    EY_TRIGGER_MAGE_TOWER       = 9,  ///< 法师塔触发器

    BG_EY_CREATURES_MAX        = 10   ///< 生物总数
};

/**
 * @brief 游戏对象类型枚举
 *
 * 定义战场中所有游戏对象的索引，用于对象数组的访问和管理
 * 包括大门、旗帜、基地旗帜（联盟/部落/中立）、占领点和增益效果
 */
enum EYBattlegroundObjectTypes
{
    BG_EY_OBJECT_DOOR_A                         = 0,   ///< 联盟大门
    BG_EY_OBJECT_DOOR_H                         = 1,   ///< 部落大门

    // 魔能机甲废墟联盟旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER    = 2,
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_LEFT      = 3,
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_RIGHT     = 4,

    // 血精灵塔联盟旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER      = 5,
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_LEFT        = 6,
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_RIGHT       = 7,

    // 德莱尼遗迹联盟旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER  = 8,
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_LEFT    = 9,
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_RIGHT   = 10,

    // 法师塔联盟旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER     = 11,
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_LEFT       = 12,
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_RIGHT      = 13,

    // 魔能机甲废墟部落旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER    = 14,
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_LEFT      = 15,
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_RIGHT     = 16,

    // 血精灵塔部落旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER      = 17,
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_LEFT        = 18,
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_RIGHT       = 19,

    // 德莱尼遗迹部落旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER  = 20,
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_LEFT    = 21,
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_RIGHT   = 22,

    // 法师塔部落旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER     = 23,
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_LEFT       = 24,
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_RIGHT      = 25,

    // 魔能机甲废墟中立旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER    = 26,
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_LEFT      = 27,
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_RIGHT     = 28,

    // 血精灵塔中立旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER      = 29,
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_LEFT        = 30,
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_RIGHT       = 31,

    // 德莱尼遗迹中立旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER  = 32,
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_LEFT    = 33,
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_RIGHT   = 34,

    // 法师塔中立旗帜（中心、左侧、右侧）
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER     = 35,
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_LEFT       = 36,
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_RIGHT      = 37,

    // 基地占领点
    BG_EY_OBJECT_TOWER_CAP_FEL_REAVER          = 38,  ///< 魔能机甲废墟占领点
    BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF            = 39,  ///< 血精灵塔占领点
    BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS        = 40,  ///< 德莱尼遗迹占领点
    BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER           = 41,  ///< 法师塔占领点

    // 旗帜
    BG_EY_OBJECT_FLAG_NETHERSTORM               = 42,  ///< 虚空风暴中央旗帜
    BG_EY_OBJECT_FLAG_FEL_REAVER               = 43,  ///< 魔能机甲废墟旗帜点
    BG_EY_OBJECT_FLAG_BLOOD_ELF                 = 44,  ///< 血精灵塔旗帜点
    BG_EY_OBJECT_FLAG_DRAENEI_RUINS             = 45,  ///< 德莱尼遗迹旗帜点
    BG_EY_OBJECT_FLAG_MAGE_TOWER                = 46,  ///< 法师塔旗帜点

    // 增益效果（每个基地3种：速度、恢复、狂暴）
    BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER          = 47,  ///< 魔能机甲废墟速度增益
    BG_EY_OBJECT_REGENBUFF_FEL_REAVER          = 48,  ///< 魔能机甲废墟恢复增益
    BG_EY_OBJECT_BERSERKBUFF_FEL_REAVER        = 49,  ///< 魔能机甲废墟狂暴增益
    BG_EY_OBJECT_SPEEDBUFF_BLOOD_ELF            = 50,  ///< 血精灵塔速度增益
    BG_EY_OBJECT_REGENBUFF_BLOOD_ELF            = 51,  ///< 血精灵塔恢复增益
    BG_EY_OBJECT_BERSERKBUFF_BLOOD_ELF          = 52,  ///< 血精灵塔狂暴增益
    BG_EY_OBJECT_SPEEDBUFF_DRAENEI_RUINS        = 53,  ///< 德莱尼遗迹速度增益
    BG_EY_OBJECT_REGENBUFF_DRAENEI_RUINS        = 54,  ///< 德莱尼遗迹恢复增益
    BG_EY_OBJECT_BERSERKBUFF_DRAENEI_RUINS      = 55,  ///< 德莱尼遗迹狂暴增益
    BG_EY_OBJECT_SPEEDBUFF_MAGE_TOWER           = 56,  ///< 法师塔速度增益
    BG_EY_OBJECT_REGENBUFF_MAGE_TOWER           = 57,  ///< 法师塔恢复增益
    BG_EY_OBJECT_BERSERKBUFF_MAGE_TOWER         = 58,  ///< 法师塔狂暴增益
    BG_EY_OBJECT_MAX                            = 59   ///< 对象类型总数
};

/// 非节日荣誉tick数
#define BG_EY_NotEYWeekendHonorTicks    260
/// 节日荣誉tick数
#define BG_EY_EYWeekendHonorTicks       160

/**
 * @brief 分数常量枚举
 *
 * 定义战场分数相关常量
 */
enum BG_EY_Score
{
    BG_EY_WARNING_NEAR_VICTORY_SCORE    = 1400,  ///< 警告分数（接近胜利）
    BG_EY_MAX_TEAM_SCORE                = 1600   ///< 最大团队分数（胜利条件）
};

/**
 * @brief 旗帜状态枚举
 *
 * 定义旗帜的各种状态
 */
enum BG_EY_FlagState
{
    BG_EY_FLAG_STATE_ON_BASE      = 0,  ///< 旗帜在基地（中央）
    BG_EY_FLAG_STATE_WAIT_RESPAWN = 1,  ///< 旗帜等待重生
    BG_EY_FLAG_STATE_ON_PLAYER    = 2,  ///< 旗帜被玩家携带
    BG_EY_FLAG_STATE_ON_GROUND    = 3   ///< 旗帜掉落在地上
};

/**
 * @brief 基地状态枚举
 *
 * 定义基地的占领状态
 */
enum EYBattlegroundPointState
{
    EY_POINT_NO_OWNER           = 0,  ///< 无所有者
    EY_POINT_STATE_UNCONTROLLED = 0,  ///< 未控制状态
    EY_POINT_UNDER_CONTROL      = 3   ///< 被控制状态
};

/**
 * @brief 目标类型枚举
 *
 * 定义战场目标类型
 */
enum BG_EY_Objectives
{
    EY_OBJECTIVE_CAPTURE_FLAG   = 183  ///< 夺旗目标
};

/**
 * @brief 广播文本枚举
 *
 * 定义战场中各种事件的广播文本ID
 */
enum BG_EY_BroadcastTexts
{
    // 魔能机甲废墟占领消息
    BG_EY_TEXT_ALLIANCE_TAKEN_FEL_REAVER_RUINS  = 17828,  ///< 联盟占领魔能机甲废墟
    BG_EY_TEXT_HORDE_TAKEN_FEL_REAVER_RUINS     = 17829,  ///< 部落占领魔能机甲废墟
    BG_EY_TEXT_ALLIANCE_LOST_FEL_REAVER_RUINS   = 17835,  ///< 联盟失去魔能机甲废墟
    BG_EY_TEXT_HORDE_LOST_FEL_REAVER_RUINS      = 17836,  ///< 部落失去魔能机甲废墟

    // 血精灵塔占领消息
    BG_EY_TEXT_ALLIANCE_TAKEN_BLOOD_ELF_TOWER   = 17819,  ///< 联盟占领血精灵塔
    BG_EY_TEXT_HORDE_TAKEN_BLOOD_ELF_TOWER      = 17823,  ///< 部落占领血精灵塔
    BG_EY_TEXT_ALLIANCE_LOST_BLOOD_ELF_TOWER    = 17831,  ///< 联盟失去血精灵塔
    BG_EY_TEXT_HORDE_LOST_BLOOD_ELF_TOWER       = 17832,  ///< 部落失去血精灵塔

    // 德莱尼遗迹占领消息
    BG_EY_TEXT_ALLIANCE_TAKEN_DRAENEI_RUINS     = 17827,  ///< 联盟占领德莱尼遗迹
    BG_EY_TEXT_HORDE_TAKEN_DRAENEI_RUINS        = 17826,  ///< 部落占领德莱尼遗迹
    BG_EY_TEXT_ALLIANCE_LOST_DRAENEI_RUINS      = 17833,  ///< 联盟失去德莱尼遗迹
    BG_EY_TEXT_HORDE_LOST_DRAENEI_RUINS         = 17834,  ///< 部落失去德莱尼遗迹

    // 法师塔占领消息
    BG_EY_TEXT_ALLIANCE_TAKEN_MAGE_TOWER        = 17824,  ///< 联盟占领法师塔
    BG_EY_TEXT_HORDE_TAKEN_MAGE_TOWER           = 17825,  ///< 部落占领法师塔
    BG_EY_TEXT_ALLIANCE_LOST_MAGE_TOWER         = 17837,  ///< 联盟失去法师塔
    BG_EY_TEXT_HORDE_LOST_MAGE_TOWER            = 17838,  ///< 部落失去法师塔

    // 旗帜相关消息
    BG_EY_TEXT_TAKEN_FLAG                       = 18359,  ///< 拾起旗帜
    BG_EY_TEXT_FLAG_DROPPED                     = 18361,  ///< 旗帜掉落
    BG_EY_TEXT_FLAG_RESET                       = 18364,  ///< 旗帜重置
    BG_EY_TEXT_ALLIANCE_CAPTURED_FLAG           = 18375,  ///< 联盟夺旗成功
    BG_EY_TEXT_HORDE_CAPTURED_FLAG              = 18384,  ///< 部落夺旗成功
};

/**
 * @struct BattlegroundEYPointIconsStruct
 * @brief 基地图标世界状态结构体
 *
 * 存储基地在不同占领状态下的世界状态ID
 */
struct BattlegroundEYPointIconsStruct
{
    /**
     * @brief 构造函数
     * @param _WorldStateControlIndex 控制状态世界状态ID
     * @param _WorldStateAllianceControlledIndex 联盟控制状态世界状态ID
     * @param _WorldStateHordeControlledIndex 部落控制状态世界状态ID
     */
    BattlegroundEYPointIconsStruct(uint32 _WorldStateControlIndex, uint32 _WorldStateAllianceControlledIndex, uint32 _WorldStateHordeControlledIndex)
        : WorldStateControlIndex(_WorldStateControlIndex), WorldStateAllianceControlledIndex(_WorldStateAllianceControlledIndex), WorldStateHordeControlledIndex(_WorldStateHordeControlledIndex) { }

    uint32 WorldStateControlIndex;          ///< 未控制状态世界状态ID
    uint32 WorldStateAllianceControlledIndex;  ///< 联盟控制状态世界状态ID
    uint32 WorldStateHordeControlledIndex;     ///< 部落控制状态世界状态ID
};

/// 四个基地的触发器中心位置坐标
Position const BG_EY_TriggerPositions[EY_POINTS_MAX] =
{
    {2044.28f, 1729.68f, 1189.96f, 0.017453f},  ///< 魔能机甲废墟中心
    {2048.83f, 1393.65f, 1194.49f, 0.20944f},   ///< 血精灵塔中心
    {2286.56f, 1402.36f, 1197.11f, 3.72381f},   ///< 德莱尼遗迹中心
    {2284.48f, 1731.23f, 1189.99f, 2.89725f}    ///< 法师塔中心
};

/**
 * @struct BattlegroundEYLosingPointStruct
 * @brief 失去基地结构体
 *
 * 存储基地失去控制时需要的数据（对象类型和消息ID）
 */
struct BattlegroundEYLosingPointStruct
{
    /**
     * @brief 构造函数
     * @param _SpawnNeutralObjectType 生成的中立旗帜对象类型
     * @param _DespawnObjectTypeAlliance 移除的联盟旗帜对象类型
     * @param _MessageIdAlliance 联盟失去基地的消息ID
     * @param _DespawnObjectTypeHorde 移除的部落旗帜对象类型
     * @param _MessageIdHorde 部落失去基地的消息ID
     */
    BattlegroundEYLosingPointStruct(uint32 _SpawnNeutralObjectType, uint32 _DespawnObjectTypeAlliance, uint32 _MessageIdAlliance, uint32 _DespawnObjectTypeHorde, uint32 _MessageIdHorde)
        : SpawnNeutralObjectType(_SpawnNeutralObjectType),
        DespawnObjectTypeAlliance(_DespawnObjectTypeAlliance), MessageIdAlliance(_MessageIdAlliance),
        DespawnObjectTypeHorde(_DespawnObjectTypeHorde), MessageIdHorde(_MessageIdHorde)
    { }

    uint32 SpawnNeutralObjectType;      ///< 生成的中立旗帜对象类型
    uint32 DespawnObjectTypeAlliance;   ///< 移除的联盟旗帜对象类型
    uint32 MessageIdAlliance;           ///< 联盟失去基地的消息ID
    uint32 DespawnObjectTypeHorde;      ///< 移除的部落旗帜对象类型
    uint32 MessageIdHorde;              ///< 部落失去基地的消息ID
};

/**
 * @struct BattlegroundEYCapturingPointStruct
 * @brief 占领基地结构体
 *
 * 存储基地被占领时需要的数据（对象类型、消息ID和墓地ID）
 */
struct BattlegroundEYCapturingPointStruct
{
    /**
     * @brief 构造函数
     * @param _DespawnNeutralObjectType 移除的中立旗帜对象类型
     * @param _SpawnObjectTypeAlliance 生成的联盟旗帜对象类型
     * @param _MessageIdAlliance 联盟占领基地的消息ID
     * @param _SpawnObjectTypeHorde 生成的部落旗帜对象类型
     * @param _MessageIdHorde 部落占领基地的消息ID
     * @param _GraveyardId 墓地ID
     */
    BattlegroundEYCapturingPointStruct(uint32 _DespawnNeutralObjectType, uint32 _SpawnObjectTypeAlliance, uint32 _MessageIdAlliance, uint32 _SpawnObjectTypeHorde, uint32 _MessageIdHorde, uint32 _GraveyardId)
        : DespawnNeutralObjectType(_DespawnNeutralObjectType),
        SpawnObjectTypeAlliance(_SpawnObjectTypeAlliance), MessageIdAlliance(_MessageIdAlliance),
        SpawnObjectTypeHorde(_SpawnObjectTypeHorde), MessageIdHorde(_MessageIdHorde),
        GraveyardId(_GraveyardId)
    { }

    uint32 DespawnNeutralObjectType;  ///< 移除的中立旗帜对象类型
    uint32 SpawnObjectTypeAlliance;   ///< 生成的联盟旗帜对象类型
    uint32 MessageIdAlliance;         ///< 联盟占领基地的消息ID
    uint32 SpawnObjectTypeHorde;      ///< 生成的部落旗帜对象类型
    uint32 MessageIdHorde;            ///< 部落占领基地的消息ID
    uint32 GraveyardId;               ///< 墓地ID
};

/// 基地tick分数（根据占领基地数量不同）
const uint8  BG_EY_TickPoints[EY_POINTS_MAX] = {1, 2, 5, 10};
/// 旗帜送回分数（根据占领基地数量不同）
const uint32 BG_EY_FlagPoints[EY_POINTS_MAX] = {75, 85, 100, 500};

/// 常量数组：基地图标世界状态
const BattlegroundEYPointIconsStruct m_PointsIconStruct[EY_POINTS_MAX] =
{
    BattlegroundEYPointIconsStruct(FEL_REAVER_UNCONTROL, FEL_REAVER_ALLIANCE_CONTROL, FEL_REAVER_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(BLOOD_ELF_UNCONTROL, BLOOD_ELF_ALLIANCE_CONTROL, BLOOD_ELF_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(DRAENEI_RUINS_UNCONTROL, DRAENEI_RUINS_ALLIANCE_CONTROL, DRAENEI_RUINS_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(MAGE_TOWER_UNCONTROL, MAGE_TOWER_ALLIANCE_CONTROL, MAGE_TOWER_HORDE_CONTROL)
};

/// 常量数组：失去基地类型
const BattlegroundEYLosingPointStruct m_LosingPointTypes[EY_POINTS_MAX] =
{
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_ALLIANCE_LOST_FEL_REAVER_RUINS, BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_HORDE_LOST_FEL_REAVER_RUINS),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_ALLIANCE_LOST_BLOOD_ELF_TOWER, BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_HORDE_LOST_BLOOD_ELF_TOWER),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_ALLIANCE_LOST_DRAENEI_RUINS, BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_HORDE_LOST_DRAENEI_RUINS),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_ALLIANCE_LOST_MAGE_TOWER, BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_HORDE_LOST_MAGE_TOWER)
};

/// 常量数组：占领基地类型
const BattlegroundEYCapturingPointStruct m_CapturingPointTypes[EY_POINTS_MAX] =
{
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_FEL_REAVER_RUINS, BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_HORDE_TAKEN_FEL_REAVER_RUINS, EY_GRAVEYARD_FEL_REAVER),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_BLOOD_ELF_TOWER, BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_HORDE_TAKEN_BLOOD_ELF_TOWER, EY_GRAVEYARD_BLOOD_ELF),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_DRAENEI_RUINS, BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_HORDE_TAKEN_DRAENEI_RUINS, EY_GRAVEYARD_DRAENEI_RUINS),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_MAGE_TOWER, BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_HORDE_TAKEN_MAGE_TOWER, EY_GRAVEYARD_MAGE_TOWER)
};

/**
 * @struct BattlegroundEYScore
 * @brief 暴风之眼玩家分数结构体
 *
 * 继承自BattlegroundScore，添加暴风之眼特定的分数统计（夺旗次数）
 */
struct BattlegroundEYScore final : public BattlegroundScore
{
    friend class BattlegroundEY;

    protected:
        /**
         * @brief 构造函数
         * @param playerGuid 玩家GUID
         */
        BattlegroundEYScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), FlagCaptures(0) { }

        /**
         * @brief 更新分数
         * @param type 分数类型
         * @param value 分数值
         *
         * 根据分数类型更新对应的统计数据
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_FLAG_CAPTURES:   ///< 夺旗次数
                    FlagCaptures += value;
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         * @param data 数据包引用
         *
         * 将玩家目标数据写入数据包
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        /**
         * @brief 获取属性1（夺旗次数）
         * @return 夺旗次数
         */
        uint32 GetAttr1() const final override { return FlagCaptures; }

        uint32 FlagCaptures;  ///< 夺旗次数
};

/**
 * @class BattlegroundEY
 * @brief 暴风之眼战场类
 *
 * 继承自Battleground类，实现暴风之眼战场的具体逻辑，包括：
 * - 四个基地（塔楼）的占领控制
 * - 虚空风暴旗帜的抢夺和送回
 * - 分数计算和胜利判定
 * - 玩家复活点管理
 *
 * 暴风之眼特点：
 * - 15v15战场，位于虚空风暴
 * - 四个基地：魔能机甲废墟、血精灵塔、德莱尼遗迹、法师塔
 * - 中央有虚空风暴旗帜，玩家需要抢夺并送回己方占领的基地
 * - 占领基地越多，每tick得分越多，旗帜送回得分也越高
 * - 第一个达到1600分的阵营获胜
 */
class BattlegroundEY : public Battleground
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化战场成员变量
         */
        BattlegroundEY();

        /**
         * @brief 析构函数
         */
        ~BattlegroundEY();

        /**
         * @brief 添加玩家到战场
         * @param player 玩家指针
         *
         * 重写基类方法，为玩家创建暴风之眼特有的分数记录
         *
         * 调用时机：玩家进入战场时
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 开始事件 - 关闭大门
         *
         * 在战场准备阶段生成并关闭阵营大门
         *
         * 调用时机：战场状态变为STATUS_WAIT_JOIN时
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件 - 打开大门
         *
         * 比赛正式开始时打开阵营大门，初始化基地和旗帜
         *
         * 调用时机：战场状态变为STATUS_IN_PROGRESS时
         */
        void StartingEventOpenDoors() override;

        /* BG Flags - 旗帜相关函数 */

        /**
         * @brief 获取旗帜携带者GUID
         * @param team 队伍ID（未使用）
         * @return 旗帜携带者GUID
         */
        ObjectGuid GetFlagPickerGUID(int32 /*team*/ = -1) const override { return m_FlagKeeper; }

        /**
         * @brief 设置旗帜携带者
         * @param guid 玩家GUID
         */
        void SetFlagPicker(ObjectGuid guid) { m_FlagKeeper = guid; }

        /**
         * @brief 检查旗帜是否被拾起
         * @return 旗帜被拾起返回true
         */
        bool IsFlagPickedup() const         { return !m_FlagKeeper.IsEmpty(); }

        /**
         * @brief 获取旗帜状态
         * @return 旗帜状态（基地、等待重生、玩家携带、掉落）
         */
        uint8 GetFlagState() const          { return m_FlagState; }

        /**
         * @brief 重生旗帜
         * @param send_message 是否发送消息
         *
         * 在中央重生旗帜
         */
        void RespawnFlag(bool send_message);

        /**
         * @brief 旗帜掉落后重生
         *
         * 旗帜掉落在地上后，经过一定时间自动重生
         */
        void RespawnFlagAfterDrop();

        /**
         * @brief 移除玩家
         * @param player 玩家指针
         * @param guid 玩家GUID
         * @param team 队伍ID
         *
         * 处理玩家离开战场时的逻辑，如旗帜掉落等
         *
         * 调用时机：玩家离开战场时
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发器
         * @param Source 玩家指针
         * @param Trigger 触发器ID
         *
         * 处理玩家进入基地区域的触发事件
         *
         * 调用时机：玩家进入特定区域触发器时
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 处理玩家击杀
         * @param player 被击杀的玩家
         * @param killer 击杀者
         *
         * 处理玩家击杀事件，如旗帜掉落等
         *
         * 调用时机：玩家被击杀时
         */
        void HandleKillPlayer(Player* player, Player* killer) override;

        /**
         * @brief 获取最近的墓地
         * @param player 玩家指针
         * @return 墓地信息
         *
         * 根据玩家位置和基地占领状态，返回最近的墓地
         *
         * 调用时机：玩家死亡选择墓地时
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /**
         * @brief 设置战场
         * @return 成功返回true，失败返回false
         *
         * 在地图中生成所有必要的游戏对象和生物
         *
         * 调用时机：战场初始化时
         */
        bool SetupBattleground() override;

        /**
         * @brief 重置战场
         *
         * 重置所有战场状态到初始状态
         *
         * 调用时机：战场结束或重置时
         */
        void Reset() override;

        /**
         * @brief 更新团队分数
         * @param Team 团队ID
         *
         * 向客户端发送更新的团队分数
         */
        void UpdateTeamScore(uint32 Team);

        /**
         * @brief 结束战场
         * @param winner 获胜团队ID
         *
         * 处理战场结束逻辑
         *
         * 调用时机：有团队达到胜利分数时
         */
        void EndBattleground(uint32 winner) override;

        /**
         * @brief 更新玩家分数
         * @param player 玩家指针
         * @param type 分数类型
         * @param value 分数值
         * @param doAddHonor 是否增加荣誉
         * @return 成功返回true
         *
         * 更新玩家的特定类型分数
         *
         * 调用时机：玩家完成特定行为时（如夺旗）
         */
        bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true) override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包引用
         *
         * 向客户端发送战场初始世界状态数据
         *
         * 调用时机：玩家进入战场时
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 设置掉落旗帜GUID
         * @param guid 旗帜GUID
         * @param TeamID 团队ID（未使用）
         */
        void SetDroppedFlagGUID(ObjectGuid guid, int32 /*TeamID*/ = -1) override  { m_DroppedFlagGUID = guid; }

        /**
         * @brief 获取掉落旗帜GUID
         * @return 掉落旗帜GUID
         */
        ObjectGuid GetDroppedFlagGUID() const { return m_DroppedFlagGUID; }

        /* Battleground Events - 战场事件 */

        /**
         * @brief 玩家点击旗帜事件
         * @param Source 玩家指针
         * @param target_obj 旗帜游戏对象
         *
         * 处理玩家点击旗帜的事件（拾起或送回）
         *
         * 调用时机：玩家点击旗帜时
         */
        void EventPlayerClickedOnFlag(Player* Source, GameObject* target_obj) override;

        /**
         * @brief 玩家掉落旗帜事件
         * @param Source 玩家指针
         *
         * 处理玩家掉落旗帜的事件
         *
         * 调用时机：旗帜携带者死亡或主动掉落时
         */
        void EventPlayerDroppedFlag(Player* Source) override;

        /* achievement req. - 成就需求 */

        /**
         * @brief 检查是否所有节点被同一阵营控制
         * @param team 团队ID
         * @return 是返回true
         *
         * 用于成就检测
         */
        bool IsAllNodesControlledByTeam(uint32 team) const override;

        /**
         * @brief 获取提前结束获胜者
         * @return 获胜团队ID
         *
         * 当战场提前结束时（如一方队伍人数过少），判断获胜者
         */
        uint32 GetPrematureWinner() override;

    private:
        /**
         * @brief 战场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 核心更新函数，每帧调用：
         * - 更新基地占领状态
         * - 更新分数tick
         * - 检查玩家进入/离开基地
         *
         * 调用时机：每帧更新循环中
         * 性能注意事项：该函数每帧调用，需保持高效
         */
        void PostUpdateImpl(uint32 diff) override;

        /**
         * @brief 玩家夺旗成功事件
         * @param Source 玩家指针
         * @param BgObjectType 基地对象类型
         *
         * 处理玩家成功将旗帜送回基地的事件
         */
        void EventPlayerCapturedFlag(Player* Source, uint32 BgObjectType);

        /**
         * @brief 团队占领基地事件
         * @param Source 玩家指针
         * @param Point 基地ID
         *
         * 处理基地被占领的事件
         */
        void EventTeamCapturedPoint(Player* Source, uint32 Point);

        /**
         * @brief 团队失去基地事件
         * @param Source 玩家指针
         * @param Point 基地ID
         *
         * 处理基地失去控制的事件
         */
        void EventTeamLostPoint(Player* Source, uint32 Point);

        /**
         * @brief 更新基地占领数量
         * @param Team 团队ID
         *
         * 统计团队占领的基地数量并更新世界状态
         */
        void UpdatePointsCount(uint32 Team);

        /**
         * @brief 更新基地图标
         * @param Team 团队ID
         * @param Point 基地ID
         *
         * 更新基地旗帜图标显示
         */
        void UpdatePointsIcons(uint32 Team, uint32 Point);

        /* Point status updating procedures - 基地状态更新过程 */

        /**
         * @brief 检查玩家离开基地
         *
         * 检测玩家是否离开了基地区域
         */
        void CheckSomeoneLeftPoint();

        /**
         * @brief 检查玩家加入基地
         *
         * 检测玩家是否进入了基地区域
         */
        void CheckSomeoneJoinedPoint();

        /**
         * @brief 更新基地状态
         *
         * 更新所有基地的占领进度
         */
        void UpdatePointStatuses();

        /* Scorekeeping - 分数记录 */

        /**
         * @brief 增加团队分数
         * @param Team 团队ID
         * @param Points 分数值
         *
         * 为团队增加分数并检查胜利条件
         */
        void AddPoints(uint32 Team, uint32 Points);

        /**
         * @brief 减少团队分数
         * @param TeamID 团队ID
         * @param Points 分数值（默认1）
         */
        void RemovePoint(uint32 TeamID, uint32 Points = 1) { m_TeamScores[GetTeamIndexByTeamId(TeamID)] -= Points; }

        /**
         * @brief 设置团队分数
         * @param TeamID 团队ID
         * @param Points 分数值（默认0）
         */
        void SetTeamPoint(uint32 TeamID, uint32 Points = 0) { m_TeamScores[GetTeamIndexByTeamId(TeamID)] = Points; }

        uint32 m_HonorScoreTics[2];         ///< 荣誉分数tick计数器[联盟, 部落]
        uint32 m_TeamPointsCount[2];        ///< 团队占领基地数量[联盟, 部落]

        uint32 m_Points_Trigger[EY_POINTS_MAX];  ///< 基地触发器ID

        ObjectGuid m_FlagKeeper;            ///< 旗帜携带者GUID
        ObjectGuid m_DroppedFlagGUID;       ///< 掉落旗帜GUID
        uint32 m_FlagCapturedBgObjectType;  ///< 旗帜被夺取时应移除的对象类型
        uint8 m_FlagState;                  ///< 旗帜状态
        int32 m_FlagsTimer;                 ///< 旗帜计时器
        int32 m_TowerCapCheckTimer;         ///< 塔楼占领检查计时器

        uint32 m_PointOwnedByTeam[EY_POINTS_MAX];    ///< 基地所有者团队ID
        uint8 m_PointState[EY_POINTS_MAX];           ///< 基地状态
        int32 m_PointBarStatus[EY_POINTS_MAX];       ///< 基地进度条状态
        GuidVector m_PlayersNearPoint[EY_POINTS_MAX + 1];  ///< 基地附近的玩家列表
        uint8 m_CurrentPointPlayersCount[2*EY_POINTS_MAX]; ///< 当前基地玩家数量[联盟偏移, 部落偏移]

        int32 m_PointAddingTimer;           ///< 基地加分计时器
        uint32 m_HonorTics;                 ///< 荣誉tick计数
};
#endif
