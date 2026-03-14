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
 * @file    DBCEnums.h
 * @brief   DBC数据库客户端枚举定义模块
 *
 * @details 本文件定义了TrinityCore中使用的所有DBC(Database Client)相关的枚举类型和结构体。
 *          DBC文件是魔兽世界客户端使用的数据库文件格式，包含游戏配置数据如：
 *          - 成就系统定义
 *          - 区域信息
 *          - 地图难度
 *          - 阵营关系
 *          - 技能系统
 *          - 坐骑和载具系统
 *
 *          这些枚举用于解析和访问DBC文件数据，是游戏核心数据结构的基础。
 *
 * @note    所有枚举值都与客户端DBC文件格式保持一致
 * @see     DBCStructure.h - DBC数据结构定义
 * @see     DBCStores.h - DBC数据存储访问接口
 */

#ifndef DBCENUMS_H
#define DBCENUMS_H

#include "Define.h"
#include <array>

#pragma pack(push, 1)

/**
 * @struct DBCPosition2D
 * @brief DBC文件中的2D坐标结构
 *
 * @details 用于存储DBC文件中的二维坐标数据，常见于：
 *          - 地图上的点位置
 *          - 区域触发器坐标
 *          - POI(兴趣点)位置
 */
struct DBCPosition2D
{
    float X;    ///< X轴坐标（东西方向）
    float Y;    ///< Y轴坐标（南北方向）
};

/**
 * @struct DBCPosition3D
 * @brief DBC文件中的3D坐标结构
 *
 * @details 用于存储DBC文件中的三维坐标数据，常见于：
 *          - 世界中的精确位置
 *          - 传送目标点
 *          - 区域触发器边界
 */
struct DBCPosition3D
{
    float X;    ///< X轴坐标（东西方向）
    float Y;    ///< Y轴坐标（南北方向）
    float Z;    ///< Z轴坐标（高度/垂直方向）
};

#pragma pack(pop)

/**
 * @enum LevelLimit
 * @brief 等级限制枚举
 *
 * @details 定义游戏中玩家、宠物等实体的等级上限。
 *          这些限制需要与客户端DBC文件保持一致。
 */
enum LevelLimit : uint8
{
    /**
     * @brief 客户端期望的默认最大等级
     *
     * @details 用于DBC物品最大等级（"直到最大玩家等级"），
     *          必须符合所使用客户端的最大等级。
     *          对于3.3.5a客户端，此值为80级。
     */
    DEFAULT_MAX_LEVEL = 80,

    /**
     * @brief 客户端支持的玩家/宠物最大等级
     *
     * @details 避免溢出或影响客户端稳定性。
     *          超过此等级可能导致客户端异常。
     *          同时参考GT_MAX_LEVEL定义。
     */
    MAX_LEVEL = 100,

    /**
     * @brief 服务器端硬性等级限制
     *
     * @details 基于代码要求的最大等级上限。
     *          用于防止数据溢出和逻辑错误。
     *          同时参考MAX_LEVEL和GT_MAX_LEVEL定义。
     */
    STRONG_MAX_LEVEL = 255,
};

/**
 * @enum BattlegroundBracketId
 * @brief 战场等级分组ID
 *
 * @details 用于战场的等级范围分组，每个分组代表一个等级段。
 *          战场匹配系统根据玩家等级将其分配到相应的分组中。
 */
enum BattlegroundBracketId
{
    BG_BRACKET_ID_FIRST          = 0,   ///< 第一个等级分组
    BG_BRACKET_ID_LAST           = 15   ///< 最后一个等级分组（共16个分组）
};

/**
 * @def MAX_BATTLEGROUND_BRACKETS
 * @brief 战场分组最大数量
 *
 * @details 必须是PvPDifficulty表中的slot值+1
 */
#define MAX_BATTLEGROUND_BRACKETS  16

/**
 * @enum AreaTeams
 * @brief 区域阵营标识
 *
 * @details 用于标识区域所属的阵营，用于控制玩家在特定区域的PvP状态。
 */
enum AreaTeams
{
    AREATEAM_NONE  = 0,  ///< 无阵营（中立区域）
    AREATEAM_ALLY  = 2,  ///< 联盟阵营
    AREATEAM_HORDE = 4,  ///< 部落阵营
    AREATEAM_ANY   = 6   ///< 任意阵营
};

/**
 * @enum AchievementFaction
 * @brief 成就阵营限制
 *
 * @details 定义成就的阵营要求，某些成就只有特定阵营才能完成。
 */
enum AchievementFaction
{
    ACHIEVEMENT_FACTION_HORDE           = 0,   ///< 部落专属成就
    ACHIEVEMENT_FACTION_ALLIANCE        = 1,   ///< 联盟专属成就
    ACHIEVEMENT_FACTION_ANY             = -1   ///< 双方阵营都可以完成
};

/**
 * @enum AchievementFlags
 * @brief 成就标志位
 *
 * @details 定义成就的各种属性和行为，包括显示方式、统计类型等。
 */
enum AchievementFlags
{
    ACHIEVEMENT_FLAG_COUNTER               = 0x00000001,    ///< 仅计数统计（永不停止和完成）
    ACHIEVEMENT_FLAG_HIDDEN                = 0x00000002,    ///< 不发送给客户端 - 仅服务器内部使用
    ACHIEVEMENT_FLAG_STORE_MAX_VALUE       = 0x00000004,    ///< 仅存储最大值？用于"达到xx级"
    ACHIEVEMENT_FLAG_SUMM                  = 0x00000008,    ///< 对所有条件的数值求和（并计算最大值）
    ACHIEVEMENT_FLAG_MAX_USED              = 0x00000010,    ///< 显示最大条件值（并计算最大值）
    ACHIEVEMENT_FLAG_REQ_COUNT             = 0x00000020,    ///< 使用非零需求计数（并计算最大值）
    ACHIEVEMENT_FLAG_AVERAGE               = 0x00000040,    ///< 显示为平均值（值/天数），依赖其他标志
    ACHIEVEMENT_FLAG_BAR                   = 0x00000080,    ///< 显示为进度条（当前值/最大值），依赖其他标志
    ACHIEVEMENT_FLAG_REALM_FIRST_REACH     = 0x00000100,    ///< 服务器首杀成就（达到条件）
    ACHIEVEMENT_FLAG_REALM_FIRST_KILL      = 0x00000200     ///< 服务器首杀成就（击杀Boss）
};

/**
 * @def MAX_CRITERIA_REQUIREMENTS
 * @brief 成就条件需求的最大数量
 */
#define MAX_CRITERIA_REQUIREMENTS 2

/**
 * @enum AchievementCriteriaCondition
 * @brief 成就条件触发条件类型
 *
 * @details 定义成就进度重置或失败的特殊条件。
 *          这些条件控制成就进度在特定情况下是否保留或重置。
 */
enum AchievementCriteriaCondition
{
    ACHIEVEMENT_CRITERIA_CONDITION_NONE            = 0,   ///< 无特殊条件
    ACHIEVEMENT_CRITERIA_CONDITION_NO_DEATH        = 1,   ///< 死亡时重置进度
    ACHIEVEMENT_CRITERIA_CONDITION_UNK2            = 2,   ///< 连续完成每日任务（连续5天完成每日任务）
    ACHIEVEMENT_CRITERIA_CONDITION_BG_MAP          = 3,   ///< 需要在特定地图上，切换时重置
    ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE         = 4,   ///< 不失败（连胜10场竞技场）
    ACHIEVEMENT_CRITERIA_CONDITION_NO_SPELL_HIT    = 9,   ///< 不被特定法术击中
    ACHIEVEMENT_CRITERIA_CONDITION_NOT_IN_GROUP    = 10,  ///< 不在队伍中
    ACHIEVEMENT_CRITERIA_CONDITION_UNK13           = 13,  ///< 未知条件

    ACHIEVEMENT_CRITERIA_CONDITION_MAX
};

/**
 * @enum AchievementCriteriaFlags
 * @brief 成就条件标志位
 *
 * @details 控制成就条件的显示和行为。
 */
enum AchievementCriteriaFlags
{
    ACHIEVEMENT_CRITERIA_FLAG_SHOW_PROGRESS_BAR = 0x00000001,  ///< 显示为进度条
    ACHIEVEMENT_CRITERIA_FLAG_HIDDEN            = 0x00000002,  ///< 在客户端中隐藏条件
    ACHIEVEMENT_CRITERIA_FLAG_FAIL_ACHIEVEMENT  = 0x00000004,  ///< 失败时成就重置（战场相关）
    ACHIEVEMENT_CRITERIA_FLAG_RESET_ON_START    = 0x00000008,  ///< 开始时重置进度
    ACHIEVEMENT_CRITERIA_FLAG_IS_DATE           = 0x00000010,  ///< 未使用
    ACHIEVEMENT_CRITERIA_FLAG_MONEY_COUNTER     = 0x00000020   ///< 显示为金钱计数器
};

/**
 * @enum AchievementCriteriaTimedTypes
 * @brief 成就计时器触发类型
 *
 * @details 定义成就计时器的启动方式。
 *          某些成就需要在限定时间内完成，计时器由特定事件触发。
 */
enum AchievementCriteriaTimedTypes : uint8
{
    ACHIEVEMENT_TIMED_TYPE_EVENT            = 1,   ///< 由内部事件启动（timerStartEvent中的ID）
    ACHIEVEMENT_TIMED_TYPE_QUEST            = 2,   ///< 接受任务时启动（timerStartEvent中的任务ID）
    ACHIEVEMENT_TIMED_TYPE_SPELL_CASTER     = 5,   ///< 施放法术时启动（施法者）
    ACHIEVEMENT_TIMED_TYPE_SPELL_TARGET     = 6,   ///< 被法术击中时启动（目标）
    ACHIEVEMENT_TIMED_TYPE_CREATURE         = 7,   ///< 击杀生物时启动
    ACHIEVEMENT_TIMED_TYPE_ITEM             = 9,   ///< 使用物品时启动

    ACHIEVEMENT_TIMED_TYPE_MAX
};

/**
 * @enum AchievementCriteriaTypes
 * @brief 成就条件类型枚举
 *
 * @details 定义所有可能的成就条件类型，用于判断玩家完成成就的进度。
 *          每种类型对应不同的游戏行为检测逻辑。
 */
enum AchievementCriteriaTypes : uint8
{
    ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE                 = 0,    ///< 击杀生物
    ACHIEVEMENT_CRITERIA_TYPE_WIN_BG                        = 1,    ///< 赢得战场
    ACHIEVEMENT_CRITERIA_TYPE_REACH_LEVEL                   = 5,    ///< 达到等级
    ACHIEVEMENT_CRITERIA_TYPE_REACH_SKILL_LEVEL             = 7,    ///< 达到技能等级
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ACHIEVEMENT          = 8,    ///< 完成成就
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT          = 9,    ///< 完成任务数量
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST_DAILY    = 10,   ///< 连续每天完成每日任务
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE       = 11,   ///< 在区域内完成任务
    ACHIEVEMENT_CRITERIA_TYPE_DAMAGE_DONE                   = 13,   ///< 造成伤害
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_DAILY_QUEST          = 14,   ///< 完成每日任务
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_BATTLEGROUND         = 15,   ///< 完成战场
    ACHIEVEMENT_CRITERIA_TYPE_DEATH_AT_MAP                  = 16,   ///< 在地图上死亡
    ACHIEVEMENT_CRITERIA_TYPE_DEATH                         = 17,   ///< 死亡
    ACHIEVEMENT_CRITERIA_TYPE_DEATH_IN_DUNGEON              = 18,   ///< 在地下城中死亡
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_RAID                 = 19,   ///< 完成团队副本
    ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_CREATURE            = 20,   ///< 被生物击杀
    ACHIEVEMENT_CRITERIA_TYPE_KILLED_BY_PLAYER              = 23,   ///< 被玩家击杀
    ACHIEVEMENT_CRITERIA_TYPE_FALL_WITHOUT_DYING            = 24,   ///< 坠落但不死亡
    ACHIEVEMENT_CRITERIA_TYPE_DEATHS_FROM                   = 26,   ///< 死于特定原因
    ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST                = 27,   ///< 完成特定任务
    ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET               = 28,   ///< 成为法术目标
    ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL                    = 29,   ///< 施放法术
    ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE          = 30,   ///< 夺取战场目标
    ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL_AT_AREA        = 31,   ///< 在区域内荣誉击杀
    ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA                     = 32,   ///< 赢得竞技场
    ACHIEVEMENT_CRITERIA_TYPE_PLAY_ARENA                    = 33,   ///< 参与竞技场
    ACHIEVEMENT_CRITERIA_TYPE_LEARN_SPELL                   = 34,   ///< 学习法术
    ACHIEVEMENT_CRITERIA_TYPE_HONORABLE_KILL                = 35,   ///< 荣誉击杀
    ACHIEVEMENT_CRITERIA_TYPE_OWN_ITEM                      = 36,   ///< 拥有物品
    ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA               = 37,   ///< 赢得评级竞技场
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING           = 38,   ///< 最高战队等级
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING       = 39,   ///< 最高个人等级
    ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LEVEL             = 40,   ///< 学习技能等级
    ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM                      = 41,   ///< 使用物品
    ACHIEVEMENT_CRITERIA_TYPE_LOOT_ITEM                     = 42,   ///< 拾取物品
    ACHIEVEMENT_CRITERIA_TYPE_EXPLORE_AREA                  = 43,   ///< 探索区域
    ACHIEVEMENT_CRITERIA_TYPE_OWN_RANK                      = 44,   ///< 拥有军衔
    ACHIEVEMENT_CRITERIA_TYPE_BUY_BANK_SLOT                 = 45,   ///< 购买银行槽位
    ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION               = 46,   ///< 获得声望
    ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION       = 47,   ///< 获得崇拜声望数量
    ACHIEVEMENT_CRITERIA_TYPE_VISIT_BARBER_SHOP             = 48,   ///< 访问理发店
    ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM               = 49,   ///< 装备史诗物品
    ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT             = 50,   ///< 需求掷骰
    ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT            = 51,   ///< 贪婪掷骰
    ACHIEVEMENT_CRITERIA_TYPE_HK_CLASS                      = 52,   ///< 荣誉击杀特定职业
    ACHIEVEMENT_CRITERIA_TYPE_HK_RACE                       = 53,   ///< 荣誉击杀特定种族
    ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE                      = 54,   ///< 做表情动作
    ACHIEVEMENT_CRITERIA_TYPE_HEALING_DONE                  = 55,   ///< 造成治疗
    ACHIEVEMENT_CRITERIA_TYPE_GET_KILLING_BLOWS             = 56,   ///< 获得击杀数
    ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM                    = 57,   ///< 装备物品
    ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_VENDORS            = 59,   ///< 从商贩获得金币
    ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TALENTS        = 60,   ///< 天赋花费金币
    ACHIEVEMENT_CRITERIA_TYPE_NUMBER_OF_TALENT_RESETS       = 61,   ///< 天赋重置次数
    ACHIEVEMENT_CRITERIA_TYPE_MONEY_FROM_QUEST_REWARD       = 62,   ///< 任务奖励金币
    ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_TRAVELLING     = 63,   ///< 旅行花费金币
    ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_AT_BARBER          = 65,   ///< 理发店花费金币
    ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL           = 66,   ///< 邮件花费金币
    ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY                    = 67,   ///< 拾取金币
    ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT                = 68,   ///< 使用游戏对象
    ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2              = 69,   ///< 成为法术目标2
    ACHIEVEMENT_CRITERIA_TYPE_SPECIAL_PVP_KILL              = 70,   ///< 特殊PvP击杀
    ACHIEVEMENT_CRITERIA_TYPE_FISH_IN_GAMEOBJECT            = 72,   ///< 在游戏对象中钓鱼
    ACHIEVEMENT_CRITERIA_TYPE_ON_LOGIN                      = 74,   ///< 登录时触发
    ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILLLINE_SPELLS        = 75,   ///< 学习技能线法术
    ACHIEVEMENT_CRITERIA_TYPE_WIN_DUEL                      = 76,   ///< 赢得决斗
    ACHIEVEMENT_CRITERIA_TYPE_LOSE_DUEL                     = 77,   ///< 输掉决斗
    ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE            = 78,   ///< 击杀特定类型生物
    ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS       = 80,   ///< 拍卖赚取金币
    ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION                = 82,   ///< 创建拍卖
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID           = 83,   ///< 最高拍卖竞价
    ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS                  = 84,   ///< 赢得拍卖
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD          = 85,   ///< 最高拍卖售价
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_GOLD_VALUE_OWNED      = 86,   ///< 拥有最多金币
    ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION       = 87,   ///< 获得崇敬声望数量
    ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION       = 88,   ///< 获得尊敬声望数量
    ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS                = 89,   ///< 已知阵营数量
    ACHIEVEMENT_CRITERIA_TYPE_LOOT_EPIC_ITEM                = 90,   ///< 拾取史诗物品
    ACHIEVEMENT_CRITERIA_TYPE_RECEIVE_EPIC_ITEM             = 91,   ///< 接收史诗物品
    ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED                     = 93,   ///< 需求掷骰
    ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED                    = 94,   ///< 贪婪掷骰
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALTH                = 95,   ///< 最高生命值
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_POWER                 = 96,   ///< 最高能量值
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_STAT                  = 97,   ///< 最高属性值
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_SPELLPOWER            = 98,   ///< 最高法术强度
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_ARMOR                 = 99,   ///< 最高护甲值
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_RATING                = 100,  ///< 最高评分
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_DEALT             = 101,  ///< 最高命中伤害
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HIT_RECEIVED          = 102,  ///< 最高承受伤害
    ACHIEVEMENT_CRITERIA_TYPE_TOTAL_DAMAGE_RECEIVED         = 103,  ///< 总承受伤害
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEAL_CAST             = 104,  ///< 最高治疗量
    ACHIEVEMENT_CRITERIA_TYPE_TOTAL_HEALING_RECEIVED        = 105,  ///< 总接受治疗
    ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_HEALING_RECEIVED      = 106,  ///< 最高接受治疗
    ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED               = 107,  ///< 放弃任务
    ACHIEVEMENT_CRITERIA_TYPE_FLIGHT_PATHS_TAKEN            = 108,  ///< 飞行路线次数
    ACHIEVEMENT_CRITERIA_TYPE_LOOT_TYPE                     = 109,  ///< 拾取类型
    ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2                   = 110,  ///< 施放法术2
    ACHIEVEMENT_CRITERIA_TYPE_LEARN_SKILL_LINE              = 112,  ///< 学习技能线
    ACHIEVEMENT_CRITERIA_TYPE_EARN_HONORABLE_KILL           = 113,  ///< 获得荣誉击杀
    ACHIEVEMENT_CRITERIA_TYPE_ACCEPTED_SUMMONINGS           = 114,  ///< 接受召唤
    ACHIEVEMENT_CRITERIA_TYPE_EARN_ACHIEVEMENT_POINTS       = 115,  ///< 获得成就点数
    ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS = 119,  ///< 使用随机副本与玩家组队
};

/**
 * @def ACHIEVEMENT_CRITERIA_TYPE_TOTAL
 * @brief 成就条件类型总数
 */
#define ACHIEVEMENT_CRITERIA_TYPE_TOTAL 124

/**
 * @enum AchievementCategory
 * @brief 成就分类ID
 *
 * @details 定义特殊成就分类的ID。
 */
enum AchievementCategory
{
    CATEGORY_CHILDRENS_WEEK     = 163   ///< 儿童周成就分类
};

/**
 * @enum AreaFlags
 * @brief 区域标志位
 *
 * @details 定义区域的属性和行为标志，包括PvP状态、阵营归属、飞行限制等。
 */
enum AreaFlags
{
    AREA_FLAG_UNK0               = 0x00000001,   ///< 未知标志0
    AREA_FLAG_UNK1               = 0x00000002,   ///< 剃刀沼泽、纳克萨玛斯和阿彻鲁斯（3.3.5a）
    AREA_FLAG_UNK2               = 0x00000004,   ///< 仅用于地图571的区域（开发测试用）
    AREA_FLAG_SLAVE_CAPITAL      = 0x00000008,   ///< 城市及其子区域
    AREA_FLAG_UNK3               = 0x00000010,   ///< 未发现共同含义
    AREA_FLAG_SLAVE_CAPITAL2     = 0x00000020,   ///< 从属主城标志？
    AREA_FLAG_ALLOW_DUELS        = 0x00000040,   ///< 允许在此决斗
    AREA_FLAG_ARENA              = 0x00000080,   ///< 竞技场（副本和世界竞技场）
    AREA_FLAG_CAPITAL            = 0x00000100,   ///< 主城标志
    AREA_FLAG_CITY               = 0x00000200,   ///< 仅用于名为"City"的区域
    AREA_FLAG_OUTLAND            = 0x00000400,   ///< 资料片区域（风暴之眼除外，使用0x4000标志）
    AREA_FLAG_SANCTUARY          = 0x00000800,   ///< 圣域区域（禁用PvP）
    AREA_FLAG_NEED_FLY           = 0x00001000,   ///< 需要飞行才能到达（无尸体复活）
    AREA_FLAG_UNUSED1            = 0x00002000,   ///< 3.3.5a未使用
    AREA_FLAG_OUTLAND2           = 0x00004000,   ///< 资料片区域（鲜血之环竞技场除外，使用0x400标志）
    AREA_FLAG_OUTDOOR_PVP        = 0x00008000,   ///< 室外PvP目标区域
    AREA_FLAG_ARENA_INSTANCE     = 0x00010000,   ///< 仅副本竞技场使用
    AREA_FLAG_UNUSED2            = 0x00020000,   ///< 3.3.5a未使用
    AREA_FLAG_CONTESTED_AREA     = 0x00040000,   ///< 争议区域（PvP服务器上即使位于阵营领地内也算争议区）
    AREA_FLAG_UNK4               = 0x00080000,   ///< 瓦尔加德和阿彻鲁斯
    AREA_FLAG_LOWLEVEL           = 0x00100000,   ///< 低等级起始区域（探索等级≤15）
    AREA_FLAG_TOWN               = 0x00200000,   ///< 带旅馆的小镇
    AREA_FLAG_REST_ZONE_HORDE    = 0x00400000,   ///< 部落休息区域（代替区域触发器）
    AREA_FLAG_REST_ZONE_ALLIANCE = 0x00800000,   ///< 联盟休息区域（代替区域触发器）
    AREA_FLAG_WINTERGRASP        = 0x01000000,   ///< 冬拥湖及其子区域
    AREA_FLAG_INSIDE             = 0x02000000,   ///< 室内区域（用于Map::IsOutdoors判断）
    AREA_FLAG_OUTSIDE            = 0x04000000,   ///< 室外区域（用于Map::IsOutdoors判断）
    AREA_FLAG_WINTERGRASP_2      = 0x08000000,   ///< 可在区域炉石和复活
    AREA_FLAG_NO_FLY_ZONE        = 0x20000000    ///< 禁飞区域
};

/**
 * @enum Difficulty
 * @brief 地图难度枚举
 *
 * @details 定义副本和团队副本的难度等级。
 *          不同的难度对应不同的怪物强度和掉落。
 */
enum Difficulty : uint8
{
    REGULAR_DIFFICULTY           = 0,  ///< 常规难度（用于野外地图）

    DUNGEON_DIFFICULTY_NORMAL    = 0,  ///< 地下城普通难度
    DUNGEON_DIFFICULTY_HEROIC    = 1,  ///< 地下城英雄难度
    DUNGEON_DIFFICULTY_EPIC      = 2,  ///< 地下城史诗难度（未使用）

    RAID_DIFFICULTY_10MAN_NORMAL = 0,  ///< 10人普通模式团队副本
    RAID_DIFFICULTY_25MAN_NORMAL = 1,  ///< 25人普通模式团队副本
    RAID_DIFFICULTY_10MAN_HEROIC = 2,  ///< 10人英雄模式团队副本
    RAID_DIFFICULTY_25MAN_HEROIC = 3   ///< 25人英雄模式团队副本
};

/**
 * @def RAID_DIFFICULTY_MASK_25MAN
 * @brief 25人模式掩码
 *
 * @details 由于25人难度值为1和3，可以用此掩码判断是否为25人模式
 */
#define RAID_DIFFICULTY_MASK_25MAN 1

/**
 * @def MAX_DUNGEON_DIFFICULTY
 * @brief 地下城难度最大数量
 */
#define MAX_DUNGEON_DIFFICULTY     3

/**
 * @def MAX_RAID_DIFFICULTY
 * @brief 团队副本难度最大数量
 */
#define MAX_RAID_DIFFICULTY        4

/**
 * @def MAX_DIFFICULTY
 * @brief 总难度最大数量
 */
#define MAX_DIFFICULTY             4

/**
 * @enum SpawnMask
 * @brief 生物生成掩码
 *
 * @details 定义生物在哪些难度下生成。
 *          使用位掩码可以同时指定多个难度。
 */
enum SpawnMask
{
    SPAWNMASK_CONTINENT         = (1 << REGULAR_DIFFICULTY),  ///< 大陆地图（无难度模式）

    SPAWNMASK_DUNGEON_NORMAL    = (1 << DUNGEON_DIFFICULTY_NORMAL),  ///< 地下城普通
    SPAWNMASK_DUNGEON_HEROIC    = (1 << DUNGEON_DIFFICULTY_HEROIC),  ///< 地下城英雄
    SPAWNMASK_DUNGEON_ALL       = (SPAWNMASK_DUNGEON_NORMAL | SPAWNMASK_DUNGEON_HEROIC),  ///< 所有地下城难度

    SPAWNMASK_RAID_10MAN_NORMAL = (1 << RAID_DIFFICULTY_10MAN_NORMAL),  ///< 10人普通团本
    SPAWNMASK_RAID_25MAN_NORMAL = (1 << RAID_DIFFICULTY_25MAN_NORMAL),  ///< 25人普通团本
    SPAWNMASK_RAID_NORMAL_ALL   = (SPAWNMASK_RAID_10MAN_NORMAL | SPAWNMASK_RAID_25MAN_NORMAL),  ///< 所有普通团本

    SPAWNMASK_RAID_10MAN_HEROIC = (1 << RAID_DIFFICULTY_10MAN_HEROIC),  ///< 10人英雄团本
    SPAWNMASK_RAID_25MAN_HEROIC = (1 << RAID_DIFFICULTY_25MAN_HEROIC),  ///< 25人英雄团本
    SPAWNMASK_RAID_HEROIC_ALL   = (SPAWNMASK_RAID_10MAN_HEROIC | SPAWNMASK_RAID_25MAN_HEROIC),  ///< 所有英雄团本

    SPAWNMASK_RAID_ALL          = (SPAWNMASK_RAID_NORMAL_ALL | SPAWNMASK_RAID_HEROIC_ALL)  ///< 所有团本难度
};

/**
 * @enum FactionTemplateFlags
 * @brief 阵营模板标志位
 *
 * @details 定义阵营的特殊行为标志。
 */
enum FactionTemplateFlags
{
    FACTION_TEMPLATE_FLAG_PVP               = 0x00000800,   ///< 标记为PvP状态
    FACTION_TEMPLATE_FLAG_CONTESTED_GUARD   = 0x00001000,   ///< 争议区域卫兵（攻击参与PvP的玩家）
    FACTION_TEMPLATE_FLAG_HOSTILE_BY_DEFAULT= 0x00002000    ///< 默认敌对
};

/**
 * @enum FactionMasks
 * @brief 阵营掩码
 *
 * @details 用于快速判断单位所属的阵营类型。
 */
enum FactionMasks
{
    FACTION_MASK_PLAYER   = 1,   ///< 任意玩家
    FACTION_MASK_ALLIANCE = 2,   ///< 联盟玩家或生物
    FACTION_MASK_HORDE    = 4,   ///< 部落玩家或生物
    FACTION_MASK_MONSTER  = 8    ///< 怪物阵营的敌对生物
    // 如果未设置任何标志，则为非敌对生物
};

/**
 * @enum MapTypes
 * @brief 地图类型枚举
 *
 * @details 定义地图的类型，对应Lua_IsInInstance函数返回值。
 */
enum MapTypes
{
    MAP_COMMON          = 0,   ///< 普通地图（野外）
    MAP_INSTANCE        = 1,   ///< 小队副本
    MAP_RAID            = 2,   ///< 团队副本
    MAP_BATTLEGROUND    = 3,   ///< 战场
    MAP_ARENA           = 4    ///< 竞技场
};

/**
 * @enum MapFlags
 * @brief 地图标志位
 *
 * @details 定义地图的特殊属性。
 */
enum MapFlags
{
    MAP_FLAG_DYNAMIC_DIFFICULTY = 0x100  ///< 动态难度标志
};

/**
 * @enum AbilityLearnType
 * @brief 技能学习类型
 *
 * @details 定义技能法术的学习方式。
 */
enum AbilityLearnType
{
    SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE  = 1,  ///< 法术状态根据技能值更新
    SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN  = 2   ///< 法术随整个技能一起学习/移除
};

/**
 * @enum ItemEnchantmentType
 * @brief 物品附魔类型
 *
 * @details 定义物品附魔的不同效果类型。
 */
enum ItemEnchantmentType
{
    ITEM_ENCHANTMENT_TYPE_NONE             = 0,  ///< 无附魔
    ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL     = 1,  ///< 战斗法术（攻击时触发）
    ITEM_ENCHANTMENT_TYPE_DAMAGE           = 2,  ///< 伤害加成
    ITEM_ENCHANTMENT_TYPE_EQUIP_SPELL      = 3,  ///< 装备时触发法术
    ITEM_ENCHANTMENT_TYPE_RESISTANCE       = 4,  ///< 抗性加成
    ITEM_ENCHANTMENT_TYPE_STAT             = 5,  ///< 属性加成
    ITEM_ENCHANTMENT_TYPE_TOTEM            = 6,  ///< 图腾
    ITEM_ENCHANTMENT_TYPE_USE_SPELL        = 7,  ///< 使用时触发法术
    ITEM_ENCHANTMENT_TYPE_PRISMATIC_SOCKET = 8   ///< 棱彩插槽
};

/**
 * @enum ItemLimitCategoryMode
 * @brief 物品限制类别模式
 *
 * @details 定义物品数量限制的应用方式。
 */
enum ItemLimitCategoryMode
{
    ITEM_LIMIT_CATEGORY_MODE_HAVE       = 0,  ///< 限制背包/银行中的拥有数量
    ITEM_LIMIT_CATEGORY_MODE_EQUIP      = 1   ///< 限制装备数量（包括已镶嵌的宝石）
};

/**
 * @enum SkillRaceClassInfoFlags
 * @brief 技能种族职业信息标志位
 *
 * @details 定义技能对不同种族和职业的可见性和行为。
 */
enum SkillRaceClassInfoFlags
{
    SKILL_FLAG_NO_SKILLUP_MESSAGE       = 0x2,    ///< 不显示技能提升消息
    SKILL_FLAG_ALWAYS_MAX_VALUE         = 0x10,   ///< 总是最大值
    SKILL_FLAG_UNLEARNABLE              = 0x20,   ///< 技能可以遗忘
    SKILL_FLAG_INCLUDE_IN_SORT          = 0x80,   ///< 客户端排序法术书时额外比较技能ID
    SKILL_FLAG_NOT_TRAINABLE            = 0x100,  ///< 不可训练
    SKILL_FLAG_MONO_VALUE               = 0x400   ///< 单一值（客户端显示，实际值可不同）
};

/**
 * @enum SpellCategoryFlags
 * @brief 法术类别标志位
 *
 * @details 定义法术类别的特殊行为。
 */
enum SpellCategoryFlags
{
    SPELL_CATEGORY_FLAG_COOLDOWN_SCALES_WITH_WEAPON_SPEED   = 0x01,  ///< 冷却时间随武器速度缩放（未使用）
    SPELL_CATEGORY_FLAG_COOLDOWN_STARTS_ON_EVENT            = 0x04   ///< 冷却在事件开始时触发
};

/**
 * @def MAX_SPELL_EFFECTS
 * @brief 法术效果最大数量
 */
#define MAX_SPELL_EFFECTS 3

/**
 * @def MAX_EFFECT_MASK
 * @brief 效果掩码最大值
 */
#define MAX_EFFECT_MASK 7

/**
 * @def MAX_SPELL_REAGENTS
 * @brief 法术材料最大数量
 */
#define MAX_SPELL_REAGENTS 8

/**
 * @enum EnchantmentSlotMask
 * @brief 附魔槽位掩码
 *
 * @details 定义附魔槽位的特殊属性。
 */
enum EnchantmentSlotMask
{
    ENCHANTMENT_CAN_SOULBOUND = 0x01,  ///< 可灵魂绑定
    ENCHANTMENT_UNK1 = 0x02,           ///< 未知1
    ENCHANTMENT_UNK2 = 0x04,           ///< 未知2
    ENCHANTMENT_UNK3 = 0x08            ///< 未知3
};

/**
 * @enum SummonPropGroup
 * @brief 召唤属性分组
 *
 * @details 来自SummonProperties.dbc第1列，定义召唤生物的类型分组。
 */
enum SummonPropGroup
{
    SUMMON_PROP_GROUP_UNKNOWN1       = 0,   ///< 未知分组1（3.0.3版本有1160个法术）
    SUMMON_PROP_GROUP_UNKNOWN2       = 1,   ///< 未知分组2（3.0.3版本有861个法术）
    SUMMON_PROP_GROUP_PETS           = 2,   ///< 宠物分组（3.0.3版本有52个法术，主要是宠物）
    SUMMON_PROP_GROUP_CONTROLLABLE   = 3,   ///< 可控制分组（3.0.3版本有13个法术，主要是可控单位）
    SUMMON_PROP_GROUP_UNKNOWN3       = 4    ///< 未知分组3（3.0.3版本有86个法术，主要是坐骑/载具）
};

/**
 * @enum SummonPropFlags
 * @brief 召唤属性标志位
 *
 * @details 来自SummonProperties.dbc第5列，定义召唤生物的特殊属性。
 */
enum SummonPropFlags
{
    SUMMON_PROP_FLAG_NONE            = 0x00000000,  ///< 无标志（3.0.3版本有1342个法术）
    SUMMON_PROP_FLAG_UNK1            = 0x00000001,  ///< 未知1（3.0.3版本有75个法术，敌对相关）
    SUMMON_PROP_FLAG_UNK2            = 0x00000002,  ///< 未知2（3.0.3版本有616个法术，友方相关）
    SUMMON_PROP_FLAG_UNK3            = 0x00000004,  ///< 未知3（3.0.3版本有22个法术）
    SUMMON_PROP_FLAG_UNK4            = 0x00000008,  ///< 未知4（3.0.3版本有49个法术，部分坐骑）
    SUMMON_PROP_FLAG_PERSONAL_SPAWN  = 0x00000010,  ///< 个人生成（仅召唤者可见）
    SUMMON_PROP_FLAG_UNK6            = 0x00000020,  ///< 未知6（3.3.5版本未使用）
    SUMMON_PROP_FLAG_UNK7            = 0x00000040,  ///< 未知7（3.0.3版本有12个法术）
    SUMMON_PROP_FLAG_UNK8            = 0x00000080,  ///< 未知8（3.0.3版本有4个法术）
    SUMMON_PROP_FLAG_UNK9            = 0x00000100,  ///< 未知9（3.0.3版本有51个法术，多为任务相关）
    SUMMON_PROP_FLAG_UNK10           = 0x00000200,  ///< 未知10（3.0.3版本有51个法术，防御相关）
    SUMMON_PROP_FLAG_UNK11           = 0x00000400,  ///< 未知11（3个法术，需要附近有某物？）
    SUMMON_PROP_FLAG_UNK12           = 0x00000800,  ///< 未知12（3.0.3版本有30个法术）
    SUMMON_PROP_FLAG_UNK13           = 0x00001000,  ///< 未知13（圣光井、杰维斯、诺莫瑞根报警机器人、载具建造）
    SUMMON_PROP_FLAG_UNK14           = 0x00002000,  ///< 未知14（向导，玩家跟随）
    SUMMON_PROP_FLAG_UNK15           = 0x00004000,  ///< 未知15（自然之力、暗影魔、野性之魂、水元素）
    SUMMON_PROP_FLAG_UNK16           = 0x00008000   ///< 未知16（光/暗子弹、吞噬、相位相关？）
};

/**
 * @def MAX_TALENT_RANK
 * @brief 天赋最大等级
 */
#define MAX_TALENT_RANK 5

/**
 * @def MAX_PET_TALENT_RANK
 * @brief 宠物天赋最大等级
 *
 * @details 用于计算，期望值≤MAX_TALENT_RANK
 */
#define MAX_PET_TALENT_RANK 3

/**
 * @def MAX_TALENT_TABS
 * @brief 天赋页签最大数量
 */
#define MAX_TALENT_TABS 3

/**
 * @brief 出租车路径点掩码大小
 */
static constexpr size_t TaxiMaskSize = 14;

/**
 * @typedef TaxiMask
 * @brief 出租车路径点掩码类型
 *
 * @details 用于记录玩家已解锁的飞行点。
 */
typedef std::array<uint32, TaxiMaskSize> TaxiMask;

/**
 * @enum TotemCategoryType
 * @brief 图腾类别类型
 *
 * @details 定义不同类型的图腾工具，用于萨满图腾和职业技能工具。
 */
enum TotemCategoryType
{
    TOTEM_CATEGORY_TYPE_KNIFE           = 1,   ///< 匕首/小刀
    TOTEM_CATEGORY_TYPE_TOTEM           = 2,   ///< 图腾
    TOTEM_CATEGORY_TYPE_ROD             = 3,   ///< 法杖
    TOTEM_CATEGORY_TYPE_PICK            = 21,  ///< 矿工锄
    TOTEM_CATEGORY_TYPE_STONE           = 22,  ///< 石头
    TOTEM_CATEGORY_TYPE_HAMMER          = 23,  ///< 铁匠锤
    TOTEM_CATEGORY_TYPE_SPANNER         = 24   ///< 扳手（工程学）
};

/**
 * @enum VehicleSeatFlags
 * @brief 载具座位标志位
 *
 * @details 定义载具座位的各种属性和行为，控制玩家在载具中的交互。
 */
enum VehicleSeatFlags
{
    VEHICLE_SEAT_FLAG_HAS_LOWER_ANIM_FOR_ENTER                         = 0x00000001,  ///< 进入时有下身动画
    VEHICLE_SEAT_FLAG_HAS_LOWER_ANIM_FOR_RIDE                          = 0x00000002,  ///< 乘坐时有下身动画
    VEHICLE_SEAT_FLAG_UNK3                                             = 0x00000004,  ///< 未知3
    VEHICLE_SEAT_FLAG_SHOULD_USE_VEH_SEAT_EXIT_ANIM_ON_VOLUNTARY_EXIT  = 0x00000008,  ///< 自愿退出时使用座位退出动画
    VEHICLE_SEAT_FLAG_UNK5                                             = 0x00000010,  ///< 未知5
    VEHICLE_SEAT_FLAG_UNK6                                             = 0x00000020,  ///< 未知6
    VEHICLE_SEAT_FLAG_UNK7                                             = 0x00000040,  ///< 未知7
    VEHICLE_SEAT_FLAG_UNK8                                             = 0x00000080,  ///< 未知8
    VEHICLE_SEAT_FLAG_UNK9                                             = 0x00000100,  ///< 未知9
    VEHICLE_SEAT_FLAG_HIDE_PASSENGER                                   = 0x00000200,  ///< 隐藏乘客
    VEHICLE_SEAT_FLAG_ALLOW_TURNING                                    = 0x00000400,  ///< 允许转向（用于CGCamera__SyncFreeLookFacing）
    VEHICLE_SEAT_FLAG_CAN_CONTROL                                      = 0x00000800,  ///< 可以控制载具（Lua_UnitInVehicleControlSeat）
    VEHICLE_SEAT_FLAG_CAN_CAST_MOUNT_SPELL                             = 0x00001000,  ///< 可以从座位施放坐骑法术（可能仅4.x版本）
    VEHICLE_SEAT_FLAG_UNCONTROLLED                                     = 0x00002000,  ///< 不可控制（可覆盖VEHICLE_SEAT_FLAG_CAN_ENTER_OR_EXIT）
    VEHICLE_SEAT_FLAG_CAN_ATTACK                                       = 0x00004000,  ///< 可以攻击、施法和使用物品
    VEHICLE_SEAT_FLAG_SHOULD_USE_VEH_SEAT_EXIT_ANIM_ON_FORCED_EXIT     = 0x00008000,  ///< 强制退出时使用座位退出动画
    VEHICLE_SEAT_FLAG_UNK17                                            = 0x00010000,  ///< 未知17
    VEHICLE_SEAT_FLAG_UNK18                                            = 0x00020000,  ///< 未知18：进入载具时保持特定永久光环
    VEHICLE_SEAT_FLAG_HAS_VEH_EXIT_ANIM_VOLUNTARY_EXIT                 = 0x00040000,  ///< 自愿退出时有载具退出动画
    VEHICLE_SEAT_FLAG_HAS_VEH_EXIT_ANIM_FORCED_EXIT                    = 0x00080000,  ///< 强制退出时有载具退出动画
    VEHICLE_SEAT_FLAG_PASSENGER_NOT_SELECTABLE                         = 0x00100000,  ///< 乘客不可选中
    VEHICLE_SEAT_FLAG_UNK22                                            = 0x00200000,  ///< 未知22
    VEHICLE_SEAT_FLAG_REC_HAS_VEHICLE_ENTER_ANIM                       = 0x00400000,  ///< 有载具进入动画
    VEHICLE_SEAT_FLAG_IS_USING_VEHICLE_CONTROLS                        = 0x00800000,  ///< 使用载具控制（Lua_IsUsingVehicleControls）
    VEHICLE_SEAT_FLAG_ENABLE_VEHICLE_ZOOM                              = 0x01000000,  ///< 启用载具缩放
    VEHICLE_SEAT_FLAG_CAN_ENTER_OR_EXIT                                = 0x02000000,  ///< 可以自由进出（Lua_CanExitVehicle）
    VEHICLE_SEAT_FLAG_CAN_SWITCH                                       = 0x04000000,  ///< 可以切换座位（Lua_CanSwitchVehicleSeats）
    VEHICLE_SEAT_FLAG_HAS_START_WARITING_FOR_VEH_TRANSITION_ANIM_ENTER = 0x08000000,  ///< 有载具过渡动画进入开始等待
    VEHICLE_SEAT_FLAG_HAS_START_WARITING_FOR_VEH_TRANSITION_ANIM_EXIT  = 0x10000000,  ///< 有载具过渡动画退出开始等待
    VEHICLE_SEAT_FLAG_CAN_CAST                                         = 0x20000000,  ///< 可以施法（Lua_UnitHasVehicleUI）
    VEHICLE_SEAT_FLAG_UNK2                                             = 0x40000000,  ///< 未知2（在CastSpell2中与0x800一起检查）
    VEHICLE_SEAT_FLAG_ALLOWS_INTERACTION                               = 0x80000000   ///< 允许交互
};

/**
 * @enum VehicleSeatFlagsB
 * @brief 载具座位标志位B
 *
 * @details 额外的载具座位属性标志。
 */
enum VehicleSeatFlagsB
{
    VEHICLE_SEAT_FLAG_B_NONE                     = 0x00000000,   ///< 无标志
    VEHICLE_SEAT_FLAG_B_USABLE_FORCED            = 0x00000002,   ///< 强制可用
    VEHICLE_SEAT_FLAG_B_TARGETS_IN_RAIDUI        = 0x00000008,   ///< 团队界面中显示目标（Lua_UnitTargetsVehicleInRaidUI）
    VEHICLE_SEAT_FLAG_B_EJECTABLE                = 0x00000020,   ///< 可弹出
    VEHICLE_SEAT_FLAG_B_USABLE_FORCED_2          = 0x00000040,   ///< 强制可用2
    VEHICLE_SEAT_FLAG_B_USABLE_FORCED_3          = 0x00000100,   ///< 强制可用3
    VEHICLE_SEAT_FLAG_B_KEEP_PET                 = 0x00020000,   ///< 保留宠物
    VEHICLE_SEAT_FLAG_B_USABLE_FORCED_4          = 0x02000000,   ///< 强制可用4
    VEHICLE_SEAT_FLAG_B_CAN_SWITCH               = 0x04000000,   ///< 可以切换
    VEHICLE_SEAT_FLAG_B_VEHICLE_PLAYERFRAME_UI   = 0x80000000    ///< 载具玩家框架UI（Lua_UnitHasVehiclePlayerFrameUI）
};

#endif
