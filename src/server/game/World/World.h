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
 * @file World.h
 * @ingroup world
 *
 * @brief 世界管理器头文件
 *
 * 本文件定义了 World 类及其相关的枚举和结构体，这是 TrinityCore 服务器的核心管理器。
 *
 * @模块职责：
 * 1. 管理所有玩家会话（WorldSession）
 * 2. 处理世界更新循环（Update 循环）
 * 3. 管理服务器配置和倍率设置
 * 4. 处理服务器关闭和重启逻辑
 * 5. 管理定时任务（拍卖行更新、尸体清理、游戏事件等）
 * 6. 处理全局消息广播
 * 7. 初始化游戏世界（加载 DBC、数据库数据等）
 *
 * @主要组件：
 * - ServerMessageType: 服务器消息类型枚举
 * - ShutdownMask: 关闭服务器选项标志
 * - ShutdownExitCode: 服务器退出代码
 * - WorldTimers: 世界定时任务类型
 * - WorldBoolConfigs: 布尔配置项枚举
 * - WorldFloatConfigs: 浮点数配置项枚举
 * - WorldIntConfigs: 整数配置项枚举
 * - Rates: 服务器倍率枚举
 * - CliCommandHolder: CLI 命令持有者
 * - CharacterInfo: 角色信息结构体
 * - World: 世界管理器主类
 *
 * @设计模式：
 * - 单例模式：通过 instance() 方法获取唯一实例
 * - 观察者模式：通过脚本系统通知事件
 *
 * @线程安全：
 * - 大部分方法只在主世界更新线程中调用
 * - 部分方法使用互斥锁保护（如 GUID 警告系统）
 * - 异步队列用于跨线程通信（如会话添加、CLI 命令）
 *
 * @使用示例：
 * @code
 * // 获取世界管理器实例
 * World* world = sWorld;
 *
 * // 发送全局消息
 * world->SendGlobalMessage(&packet);
 *
 * // 获取配置值
 * uint32 maxLevel = world->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
 * @endcode
 *
 * @see World.cpp 实现文件
 * @see WorldSession 会话类
 */

/// \addtogroup world The World
/// @{
/// \file

#ifndef __WORLD_H
#define __WORLD_H

#include "Common.h"
#include "AsyncCallbackProcessor.h"
#include "DatabaseEnvFwd.h"
#include "LockedQueue.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "Timer.h"

#include <atomic>
#include <list>
#include <map>
#include <unordered_map>

class Player;
class WorldPacket;
class WorldSession;
class WorldSocket;
struct Realm;

// ============================================================================
// 服务器消息类型枚举
// ============================================================================
// 职责：定义服务器发送给客户端的系统消息类型
// 来源：ServerMessages.dbc
// ============================================================================
enum ServerMessageType
{
    SERVER_MSG_SHUTDOWN_TIME      = 1,  // 服务器关闭倒计时
    SERVER_MSG_RESTART_TIME       = 2,  // 服务器重启倒计时
    SERVER_MSG_STRING             = 3,  // 自定义字符串消息
    SERVER_MSG_SHUTDOWN_CANCELLED = 4,  // 关闭已取消
    SERVER_MSG_RESTART_CANCELLED  = 5   // 重启已取消
};

// ============================================================================
// 关闭服务器掩码枚举
// ============================================================================
// 职责：定义服务器关闭时的选项标志
// ============================================================================
enum ShutdownMask : uint32
{
    SHUTDOWN_MASK_RESTART = 1,  // 重启服务器
    SHUTDOWN_MASK_IDLE    = 2,  // 空闲时关闭
    SHUTDOWN_MASK_FORCE   = 4   // 强制关闭
};

// ============================================================================
// 关闭服务器退出码枚举
// ============================================================================
// 职责：定义服务器关闭时的退出代码
// ============================================================================
enum ShutdownExitCode : uint32
{
    SHUTDOWN_EXIT_CODE = 0,  // 正常关闭
    ERROR_EXIT_CODE    = 1,  // 错误退出
    RESTART_EXIT_CODE  = 2   // 重启退出
};

// ============================================================================
// 世界定时器枚举
// ============================================================================
// 职责：定义世界级别的定时任务类型
// 用途：管理拍卖行、运行时间、尸体清理、游戏事件等定期任务
// ============================================================================
/// Timers for different object refresh rates
enum WorldTimers
{
    WUPDATE_AUCTIONS,            // 拍卖行更新
    WUPDATE_AUCTIONS_PENDING,    // 待处理拍卖更新
    WUPDATE_UPTIME,              // 运行时间更新
    WUPDATE_CORPSES,             // 尸体清理更新
    WUPDATE_EVENTS,              // 游戏事件更新
    WUPDATE_CLEANDB,             // 数据库清理更新
    WUPDATE_AUTOBROADCAST,       // 自动广播更新
    WUPDATE_MAILBOXQUEUE,        // 邮箱队列更新
    WUPDATE_DELETECHARS,         // 删除角色更新
    WUPDATE_AHBOT,               // 拍卖行机器人更新
    WUPDATE_PINGDB,              // 数据库心跳更新
    WUPDATE_CHECK_FILECHANGES,   // 文件变更检查更新
    WUPDATE_WHO_LIST,            // 在线玩家列表更新
    WUPDATE_CHANNEL_SAVE,        // 频道保存更新
    WUPDATE_COUNT                // 定时器总数
};

// ============================================================================
// 世界布尔配置枚举
// ============================================================================
// 职责：定义世界服务器的布尔类型配置项
// 用途：控制服务器各项功能的开关状态
// ============================================================================
/// Configuration elements
enum WorldBoolConfigs : uint32
{
    CONFIG_DURABILITY_LOSS_IN_PVP = 0,
    CONFIG_ADDON_CHANNEL,
    CONFIG_CLEAN_CHARACTER_DB,
    CONFIG_GRID_UNLOAD,
    CONFIG_STATS_SAVE_ONLY_ON_LOGOUT,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_CALENDAR,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION,
    CONFIG_ALLOW_TWO_SIDE_TRADE,
    CONFIG_ALL_TAXI_PATHS,
    CONFIG_INSTANT_TAXI,
    CONFIG_INSTANCE_IGNORE_LEVEL,
    CONFIG_INSTANCE_IGNORE_RAID,
    CONFIG_CAST_UNSTUCK,
    CONFIG_ALLOW_GM_GROUP,
    CONFIG_GM_LOWER_SECURITY,
    CONFIG_SKILL_PROSPECTING,
    CONFIG_SKILL_MILLING,
    CONFIG_WEATHER,
    CONFIG_ALWAYS_MAX_SKILL_FOR_LEVEL,
    CONFIG_QUEST_IGNORE_RAID,
    CONFIG_CHAT_PARTY_RAID_WARNINGS,
    CONFIG_DETECT_POS_COLLISION,
    CONFIG_RESTRICTED_LFG_CHANNEL,
    CONFIG_CHAT_FAKE_MESSAGE_PREVENTING,
    CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVP,
    CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVE,
    CONFIG_DEATH_BONES_WORLD,
    CONFIG_DEATH_BONES_BG_OR_ARENA,
    CONFIG_DIE_COMMAND_MODE,
    CONFIG_DECLINED_NAMES_USED,
    CONFIG_BATTLEGROUND_CAST_DESERTER,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY,
    CONFIG_BATTLEGROUND_STORE_STATISTICS_ENABLE,
    CONFIG_BATTLEGROUND_TRACK_DESERTERS,
    CONFIG_BG_XP_FOR_KILL,
    CONFIG_ARENA_AUTO_DISTRIBUTE_POINTS,
    CONFIG_ARENA_QUEUE_ANNOUNCER_ENABLE,
    CONFIG_ARENA_SEASON_IN_PROGRESS,
    CONFIG_ARENA_LOG_EXTENDED_INFO,
    CONFIG_OFFHAND_CHECK_AT_SPELL_UNLEARN,
    CONFIG_VMAP_INDOOR_CHECK,
    CONFIG_START_ALL_SPELLS,
    CONFIG_START_ALL_EXPLORED,
    CONFIG_START_ALL_REP,
    CONFIG_ALWAYS_MAXSKILL,
    CONFIG_PVP_TOKEN_ENABLE,
    CONFIG_NO_RESET_TALENT_COST,
    CONFIG_SHOW_KICK_IN_WORLD,
    CONFIG_SHOW_MUTE_IN_WORLD,
    CONFIG_SHOW_BAN_IN_WORLD,
    CONFIG_AUTOBROADCAST,
    CONFIG_ALLOW_TICKETS,
    CONFIG_DELETE_CHARACTER_TICKET_TRACE,
    CONFIG_DBC_ENFORCE_ITEM_ATTRIBUTES,
    CONFIG_PRESERVE_CUSTOM_CHANNELS,
    CONFIG_PDUMP_NO_PATHS,
    CONFIG_PDUMP_NO_OVERWRITE,
    CONFIG_QUEST_IGNORE_AUTO_ACCEPT,
    CONFIG_QUEST_IGNORE_AUTO_COMPLETE,
    CONFIG_QUEST_ENABLE_QUEST_TRACKER,
    CONFIG_WARDEN_ENABLED,
    CONFIG_ENABLE_MMAPS,
    CONFIG_WINTERGRASP_ENABLE,
    CONFIG_EVENT_ANNOUNCE,
    CONFIG_STATS_LIMITS_ENABLE,
    CONFIG_INSTANCES_RESET_ANNOUNCE,
    CONFIG_IP_BASED_ACTION_LOGGING,
    CONFIG_ALLOW_TRACK_BOTH_RESOURCES,
    CONFIG_CALCULATE_CREATURE_ZONE_AREA_DATA,
    CONFIG_CALCULATE_GAMEOBJECT_ZONE_AREA_DATA,
    CONFIG_RESET_DUEL_COOLDOWNS,
    CONFIG_RESET_DUEL_HEALTH_MANA,
    CONFIG_BASEMAP_LOAD_GRIDS,
    CONFIG_INSTANCEMAP_LOAD_GRIDS,
    CONFIG_HOTSWAP_ENABLED,
    CONFIG_HOTSWAP_RECOMPILER_ENABLED,
    CONFIG_HOTSWAP_EARLY_TERMINATION_ENABLED,
    CONFIG_HOTSWAP_BUILD_FILE_RECREATION_ENABLED,
    CONFIG_HOTSWAP_INSTALL_ENABLED,
    CONFIG_HOTSWAP_PREFIX_CORRECTION_ENABLED,
    CONFIG_PREVENT_RENAME_CUSTOMIZATION,
    CONFIG_CACHE_DATA_QUERIES,
    CONFIG_CHECK_GOBJECT_LOS,
    CONFIG_RESPAWN_DYNAMIC_ESCORTNPC,
    CONFIG_REGEN_HP_CANNOT_REACH_TARGET_IN_RAID,
    CONFIG_ALLOW_LOGGING_IP_ADDRESSES_IN_DATABASE,
    BOOL_CONFIG_VALUE_COUNT
};

// ============================================================================
// 世界浮点数配置枚举
// ============================================================================
// 职责：定义世界服务器的浮点数类型配置项
// 用途：存储距离、范围、倍率等需要精度的数值配置
// ============================================================================
enum WorldFloatConfigs : uint32
{
    CONFIG_GROUP_XP_DISTANCE = 0,
    CONFIG_MAX_RECRUIT_A_FRIEND_DISTANCE,
    CONFIG_SIGHT_MONSTER,
    CONFIG_LISTEN_RANGE_SAY,
    CONFIG_LISTEN_RANGE_TEXTEMOTE,
    CONFIG_LISTEN_RANGE_YELL,
    CONFIG_CREATURE_FAMILY_FLEE_ASSISTANCE_RADIUS,
    CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS,
    CONFIG_THREAT_RADIUS,
    CONFIG_CHANCE_OF_GM_SURVEY,
    CONFIG_STATS_LIMITS_DODGE,
    CONFIG_STATS_LIMITS_PARRY,
    CONFIG_STATS_LIMITS_BLOCK,
    CONFIG_STATS_LIMITS_CRIT,
    CONFIG_ARENA_WIN_RATING_MODIFIER_1,
    CONFIG_ARENA_WIN_RATING_MODIFIER_2,
    CONFIG_ARENA_LOSE_RATING_MODIFIER,
    CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER,
    CONFIG_RESPAWN_DYNAMICRATE_CREATURE,
    CONFIG_RESPAWN_DYNAMICRATE_GAMEOBJECT,
    FLOAT_CONFIG_VALUE_COUNT
};

// ============================================================================
// 世界整数配置枚举
// ============================================================================
// 职责：定义世界服务器的整数类型配置项
// 用途：存储端口、等级、时间间隔、数量等配置
// ============================================================================
enum WorldIntConfigs : uint32
{
    CONFIG_COMPRESSION = 0,
    CONFIG_INTERVAL_SAVE,
    CONFIG_INTERVAL_GRIDCLEAN,
    CONFIG_INTERVAL_MAPUPDATE,
    CONFIG_INTERVAL_CHANGEWEATHER,
    CONFIG_INTERVAL_DISCONNECT_TOLERANCE,
    CONFIG_PORT_WORLD,
    CONFIG_SOCKET_TIMEOUTTIME,
    CONFIG_SESSION_ADD_DELAY,
    CONFIG_GAME_TYPE,
    CONFIG_REALM_ZONE,
    CONFIG_STRICT_PLAYER_NAMES,
    CONFIG_STRICT_CHARTER_NAMES,
    CONFIG_STRICT_PET_NAMES,
    CONFIG_MIN_PLAYER_NAME,
    CONFIG_MIN_CHARTER_NAME,
    CONFIG_MIN_PET_NAME,
    CONFIG_CHARACTER_CREATING_DISABLED,
    CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK,
    CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK,
    CONFIG_CHARACTERS_PER_ACCOUNT,
    CONFIG_CHARACTERS_PER_REALM,
    CONFIG_DEATH_KNIGHTS_PER_REALM,
    CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_DEATH_KNIGHT,
    CONFIG_SKIP_CINEMATICS,
    CONFIG_MAX_PLAYER_LEVEL,
    CONFIG_MIN_DUALSPEC_LEVEL,
    CONFIG_START_PLAYER_LEVEL,
    CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL,
    CONFIG_START_PLAYER_MONEY,
    CONFIG_MAX_HONOR_POINTS,
    CONFIG_START_HONOR_POINTS,
    CONFIG_MAX_ARENA_POINTS,
    CONFIG_START_ARENA_POINTS,
    CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL,
    CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL_DIFFERENCE,
    CONFIG_INSTANCE_RESET_TIME_HOUR,
    CONFIG_INSTANCE_UNLOAD_DELAY,
    CONFIG_DAILY_QUEST_RESET_TIME_HOUR,
    CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY,
    CONFIG_MAX_PRIMARY_TRADE_SKILL,
    CONFIG_MIN_PETITION_SIGNS,
    CONFIG_MIN_QUEST_SCALED_XP_RATIO,
    CONFIG_MIN_CREATURE_SCALED_XP_RATIO,
    CONFIG_MIN_DISCOVERED_SCALED_XP_RATIO,
    CONFIG_GM_LOGIN_STATE,
    CONFIG_GM_VISIBLE_STATE,
    CONFIG_GM_ACCEPT_TICKETS,
    CONFIG_GM_CHAT,
    CONFIG_GM_WHISPERING_TO,
    CONFIG_GM_FREEZE_DURATION,
    CONFIG_GM_LEVEL_IN_GM_LIST,
    CONFIG_GM_LEVEL_IN_WHO_LIST,
    CONFIG_START_GM_LEVEL,
    CONFIG_FORCE_SHUTDOWN_THRESHOLD,
    CONFIG_GROUP_VISIBILITY,
    CONFIG_MAIL_DELIVERY_DELAY,
    CONFIG_CLEAN_OLD_MAIL_TIME,
    CONFIG_UPTIME_UPDATE,
    CONFIG_SKILL_CHANCE_ORANGE,
    CONFIG_SKILL_CHANCE_YELLOW,
    CONFIG_SKILL_CHANCE_GREEN,
    CONFIG_SKILL_CHANCE_GREY,
    CONFIG_SKILL_CHANCE_MINING_STEPS,
    CONFIG_SKILL_CHANCE_SKINNING_STEPS,
    CONFIG_SKILL_GAIN_CRAFTING,
    CONFIG_SKILL_GAIN_DEFENSE,
    CONFIG_SKILL_GAIN_GATHERING,
    CONFIG_SKILL_GAIN_WEAPON,
    CONFIG_MAX_OVERSPEED_PINGS,
    CONFIG_EXPANSION,
    CONFIG_CHATFLOOD_MESSAGE_COUNT,
    CONFIG_CHATFLOOD_MESSAGE_DELAY,
    CONFIG_CHATFLOOD_ADDON_MESSAGE_COUNT,
    CONFIG_CHATFLOOD_ADDON_MESSAGE_DELAY,
    CONFIG_CHATFLOOD_MUTE_TIME,
    CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY,
    CONFIG_CREATURE_FAMILY_FLEE_DELAY,
    CONFIG_WORLD_BOSS_LEVEL_DIFF,
    CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF,
    CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF,
    CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY,
    CONFIG_CHAT_STRICT_LINK_CHECKING_KICK,
    CONFIG_CHAT_CHANNEL_LEVEL_REQ,
    CONFIG_CHAT_WHISPER_LEVEL_REQ,
    CONFIG_CHAT_EMOTE_LEVEL_REQ,
    CONFIG_CHAT_SAY_LEVEL_REQ,
    CONFIG_CHAT_YELL_LEVEL_REQ,
    CONFIG_PARTY_LEVEL_REQ,
    CONFIG_TRADE_LEVEL_REQ,
    CONFIG_TICKET_LEVEL_REQ,
    CONFIG_AUCTION_LEVEL_REQ,
    CONFIG_MAIL_LEVEL_REQ,
    CONFIG_CORPSE_DECAY_NORMAL,
    CONFIG_CORPSE_DECAY_RARE,
    CONFIG_CORPSE_DECAY_ELITE,
    CONFIG_CORPSE_DECAY_RAREELITE,
    CONFIG_CORPSE_DECAY_WORLDBOSS,
    CONFIG_DEATH_SICKNESS_LEVEL,
    CONFIG_INSTANT_LOGOUT,
    CONFIG_DISABLE_BREATHING,
    CONFIG_BATTLEGROUND_INVITATION_TYPE,
    CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER,
    CONFIG_BATTLEGROUND_PREMADE_GROUP_WAIT_FOR_MATCH,
    CONFIG_BATTLEGROUND_REPORT_AFK,
    CONFIG_ARENA_MAX_RATING_DIFFERENCE,
    CONFIG_ARENA_RATING_DISCARD_TIMER,
    CONFIG_ARENA_PREV_OPPONENTS_DISCARD_TIMER,
    CONFIG_ARENA_RATED_UPDATE_TIMER,
    CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS,
    CONFIG_ARENA_SEASON_ID,
    CONFIG_ARENA_START_RATING,
    CONFIG_ARENA_START_PERSONAL_RATING,
    CONFIG_ARENA_START_MATCHMAKER_RATING,
    CONFIG_MAX_WHO,
    CONFIG_HONOR_AFTER_DUEL,
    CONFIG_PVP_TOKEN_MAP_TYPE,
    CONFIG_PVP_TOKEN_ID,
    CONFIG_PVP_TOKEN_COUNT,
    CONFIG_ENABLE_SINFO_LOGIN,
    CONFIG_PLAYER_ALLOW_COMMANDS,
    CONFIG_NUMTHREADS,
    CONFIG_LOGDB_CLEARINTERVAL,
    CONFIG_LOGDB_CLEARTIME,
    CONFIG_CLIENTCACHE_VERSION,
    CONFIG_GUILD_EVENT_LOG_COUNT,
    CONFIG_GUILD_BANK_EVENT_LOG_COUNT,
    CONFIG_MIN_LEVEL_STAT_SAVE,
    CONFIG_RANDOM_BG_RESET_HOUR,
    CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR,
    CONFIG_GUILD_RESET_HOUR,
    CONFIG_CHARDELETE_KEEP_DAYS,
    CONFIG_CHARDELETE_METHOD,
    CONFIG_CHARDELETE_MIN_LEVEL,
    CONFIG_CHARDELETE_DEATH_KNIGHT_MIN_LEVEL,
    CONFIG_AUTOBROADCAST_CENTER,
    CONFIG_AUTOBROADCAST_INTERVAL,
    CONFIG_MAX_RESULTS_LOOKUP_COMMANDS,
    CONFIG_DB_PING_INTERVAL,
    CONFIG_PRESERVE_CUSTOM_CHANNEL_DURATION,
    CONFIG_PRESERVE_CUSTOM_CHANNEL_INTERVAL,
    CONFIG_PERSISTENT_CHARACTER_CLEAN_FLAGS,
    CONFIG_LFG_OPTIONSMASK,
    CONFIG_MAX_INSTANCES_PER_HOUR,
    CONFIG_XP_BOOST_DAYMASK,
    CONFIG_WARDEN_CLIENT_RESPONSE_DELAY,
    CONFIG_WARDEN_CLIENT_CHECK_HOLDOFF,
    CONFIG_WARDEN_CLIENT_FAIL_ACTION,
    CONFIG_WARDEN_CLIENT_BAN_DURATION,
    CONFIG_WARDEN_NUM_INJECT_CHECKS,
    CONFIG_WARDEN_NUM_LUA_CHECKS,
    CONFIG_WARDEN_NUM_CLIENT_MOD_CHECKS,
    CONFIG_WINTERGRASP_PLR_MAX,
    CONFIG_WINTERGRASP_PLR_MIN,
    CONFIG_WINTERGRASP_PLR_MIN_LVL,
    CONFIG_WINTERGRASP_BATTLETIME,
    CONFIG_WINTERGRASP_NOBATTLETIME,
    CONFIG_WINTERGRASP_RESTART_AFTER_CRASH,
    CONFIG_PACKET_SPOOF_POLICY,
    CONFIG_PACKET_SPOOF_BANMODE,
    CONFIG_PACKET_SPOOF_BANDURATION,
    CONFIG_ACC_PASSCHANGESEC,
    CONFIG_BG_REWARD_WINNER_HONOR_FIRST,
    CONFIG_BG_REWARD_WINNER_ARENA_FIRST,
    CONFIG_BG_REWARD_WINNER_HONOR_LAST,
    CONFIG_BG_REWARD_WINNER_ARENA_LAST,
    CONFIG_BG_REWARD_LOSER_HONOR_FIRST,
    CONFIG_BG_REWARD_LOSER_HONOR_LAST,
    CONFIG_BIRTHDAY_TIME,
    CONFIG_CREATURE_PICKPOCKET_REFILL,
    CONFIG_CREATURE_STOP_FOR_PLAYER,
    CONFIG_AHBOT_UPDATE_INTERVAL,
    CONFIG_CHARTER_COST_GUILD,
    CONFIG_CHARTER_COST_ARENA_2v2,
    CONFIG_CHARTER_COST_ARENA_3v3,
    CONFIG_CHARTER_COST_ARENA_5v5,
    CONFIG_NO_GRAY_AGGRO_ABOVE,
    CONFIG_NO_GRAY_AGGRO_BELOW,
    CONFIG_AUCTION_GETALL_DELAY,
    CONFIG_AUCTION_SEARCH_DELAY,
    CONFIG_TALENTS_INSPECTING,
    CONFIG_RESPAWN_MINCHECKINTERVALMS,
    CONFIG_RESPAWN_DYNAMICMODE,
    CONFIG_RESPAWN_GUIDWARNLEVEL,
    CONFIG_RESPAWN_GUIDALERTLEVEL,
    CONFIG_RESPAWN_RESTARTQUIETTIME,
    CONFIG_RESPAWN_DYNAMICMINIMUM_CREATURE,
    CONFIG_RESPAWN_DYNAMICMINIMUM_GAMEOBJECT,
    CONFIG_RESPAWN_GUIDWARNING_FREQUENCY,
    CONFIG_SOCKET_TIMEOUTTIME_ACTIVE,
    CONFIG_PENDING_MOVE_CHANGES_TIMEOUT,
    INT_CONFIG_VALUE_COUNT
};

// ============================================================================
// 服务器倍率枚举
// ============================================================================
// 职责：定义服务器各种倍率配置项
// 用途：控制经验、金币、物品掉落、声望等倍率
// ============================================================================
/// Server rates
enum Rates
{
    RATE_HEALTH = 0,
    RATE_POWER_MANA,
    RATE_POWER_RAGE_INCOME,
    RATE_POWER_RAGE_LOSS,
    RATE_POWER_RUNICPOWER_INCOME,
    RATE_POWER_RUNICPOWER_LOSS,
    RATE_POWER_FOCUS,
    RATE_POWER_ENERGY,
    RATE_SKILL_DISCOVERY,
    RATE_DROP_ITEM_POOR,
    RATE_DROP_ITEM_NORMAL,
    RATE_DROP_ITEM_UNCOMMON,
    RATE_DROP_ITEM_RARE,
    RATE_DROP_ITEM_EPIC,
    RATE_DROP_ITEM_LEGENDARY,
    RATE_DROP_ITEM_ARTIFACT,
    RATE_DROP_ITEM_REFERENCED,
    RATE_DROP_ITEM_REFERENCED_AMOUNT,
    RATE_DROP_MONEY,
    RATE_XP_KILL,
    RATE_XP_BG_KILL,
    RATE_XP_QUEST,
    RATE_XP_EXPLORE,
    RATE_REPAIRCOST,
    RATE_REPUTATION_GAIN,
    RATE_REPUTATION_LOWLEVEL_KILL,
    RATE_REPUTATION_LOWLEVEL_QUEST,
    RATE_REPUTATION_RECRUIT_A_FRIEND_BONUS,
    RATE_CREATURE_NORMAL_HP,
    RATE_CREATURE_ELITE_ELITE_HP,
    RATE_CREATURE_ELITE_RAREELITE_HP,
    RATE_CREATURE_ELITE_WORLDBOSS_HP,
    RATE_CREATURE_ELITE_RARE_HP,
    RATE_CREATURE_NORMAL_DAMAGE,
    RATE_CREATURE_ELITE_ELITE_DAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_DAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE,
    RATE_CREATURE_ELITE_RARE_DAMAGE,
    RATE_CREATURE_NORMAL_SPELLDAMAGE,
    RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RARE_SPELLDAMAGE,
    RATE_CREATURE_AGGRO,
    RATE_REST_INGAME,
    RATE_REST_OFFLINE_IN_TAVERN_OR_CITY,
    RATE_REST_OFFLINE_IN_WILDERNESS,
    RATE_DAMAGE_FALL,
    RATE_AUCTION_TIME,
    RATE_AUCTION_DEPOSIT,
    RATE_AUCTION_CUT,
    RATE_HONOR,
    RATE_ARENA_POINTS,
    RATE_TALENT,
    RATE_CORPSE_DECAY_LOOTED,
    RATE_INSTANCE_RESET_TIME,
    RATE_DURABILITY_LOSS_ON_DEATH,
    RATE_DURABILITY_LOSS_DAMAGE,
    RATE_DURABILITY_LOSS_PARRY,
    RATE_DURABILITY_LOSS_ABSORB,
    RATE_DURABILITY_LOSS_BLOCK,
    RATE_MOVESPEED,
    RATE_XP_BOOST,
    RATE_MONEY_QUEST,
    RATE_MONEY_MAX_LEVEL_QUEST,
    MAX_RATES
};

// ============================================================================
// 计费计划标志枚举
// ============================================================================
// 职责：定义用于 SMSG_AUTH_RESPONSE 数据包的计费标志
// 用途：标识玩家的会话类型（免费试用、订阅等）
// ============================================================================
/// Can be used in SMSG_AUTH_RESPONSE packet
enum BillingPlanFlags
{
    SESSION_NONE            = 0x00,
    SESSION_UNUSED          = 0x01,
    SESSION_RECURRING_BILL  = 0x02,
    SESSION_FREE_TRIAL      = 0x04,
    SESSION_IGR             = 0x08,
    SESSION_USAGE           = 0x10,
    SESSION_TIME_MIXTURE    = 0x20,
    SESSION_RESTRICTED      = 0x40,
    SESSION_ENABLE_CAIS     = 0x80
};

// ============================================================================
// 领域区域枚举
// ============================================================================
// 职责：定义服务器的区域设置类型
// 用途：根据区域设置字符命名规则和语言支持
// ============================================================================
enum RealmZone
{
    REALM_ZONE_UNKNOWN       = 0,                           // any language
    REALM_ZONE_DEVELOPMENT   = 1,                           // any language
    REALM_ZONE_UNITED_STATES = 2,                           // extended-Latin
    REALM_ZONE_OCEANIC       = 3,                           // extended-Latin
    REALM_ZONE_LATIN_AMERICA = 4,                           // extended-Latin
    REALM_ZONE_TOURNAMENT_5  = 5,                           // basic-Latin at create, any at login
    REALM_ZONE_KOREA         = 6,                           // East-Asian
    REALM_ZONE_TOURNAMENT_7  = 7,                           // basic-Latin at create, any at login
    REALM_ZONE_ENGLISH       = 8,                           // extended-Latin
    REALM_ZONE_GERMAN        = 9,                           // extended-Latin
    REALM_ZONE_FRENCH        = 10,                          // extended-Latin
    REALM_ZONE_SPANISH       = 11,                          // extended-Latin
    REALM_ZONE_RUSSIAN       = 12,                          // Cyrillic
    REALM_ZONE_TOURNAMENT_13 = 13,                          // basic-Latin at create, any at login
    REALM_ZONE_TAIWAN        = 14,                          // East-Asian
    REALM_ZONE_TOURNAMENT_15 = 15,                          // basic-Latin at create, any at login
    REALM_ZONE_CHINA         = 16,                          // East-Asian
    REALM_ZONE_CN1           = 17,                          // basic-Latin at create, any at login
    REALM_ZONE_CN2           = 18,                          // basic-Latin at create, any at login
    REALM_ZONE_CN3           = 19,                          // basic-Latin at create, any at login
    REALM_ZONE_CN4           = 20,                          // basic-Latin at create, any at login
    REALM_ZONE_CN5           = 21,                          // basic-Latin at create, any at login
    REALM_ZONE_CN6           = 22,                          // basic-Latin at create, any at login
    REALM_ZONE_CN7           = 23,                          // basic-Latin at create, any at login
    REALM_ZONE_CN8           = 24,                          // basic-Latin at create, any at login
    REALM_ZONE_TOURNAMENT_25 = 25,                          // basic-Latin at create, any at login
    REALM_ZONE_TEST_SERVER   = 26,                          // any language
    REALM_ZONE_TOURNAMENT_27 = 27,                          // basic-Latin at create, any at login
    REALM_ZONE_QA_SERVER     = 28,                          // any language
    REALM_ZONE_CN9           = 29,                          // basic-Latin at create, any at login
    REALM_ZONE_TEST_SERVER_2 = 30,                          // any language
    REALM_ZONE_CN10          = 31,                          // basic-Latin at create, any at login
    REALM_ZONE_CTC           = 32,
    REALM_ZONE_CNC           = 33,
    REALM_ZONE_CN1_4         = 34,                          // basic-Latin at create, any at login
    REALM_ZONE_CN2_6_9       = 35,                          // basic-Latin at create, any at login
    REALM_ZONE_CN3_7         = 36,                          // basic-Latin at create, any at login
    REALM_ZONE_CN5_8         = 37                           // basic-Latin at create, any at login
};

// ============================================================================
// CLI命令持有者结构体
// ============================================================================
// 职责：存储用于延迟执行的CLI命令
// 用途：在多线程环境中安全地传递控制台命令
// ============================================================================
/// Storage class for commands issued for delayed execution
struct TC_GAME_API CliCommandHolder
{
    using Print = void(*)(void*, std::string_view);             // 打印函数类型
    using CommandFinished = void(*)(void*, bool success);       // 命令完成回调类型

    void* m_callbackArg;            // 回调参数
    char* m_command;                // 命令字符串
    Print m_print;                  // 打印回调函数
    CommandFinished m_commandFinished;  // 命令完成回调

    CliCommandHolder(void* callbackArg, char const* command, Print zprint, CommandFinished commandFinished);
    ~CliCommandHolder();

private:
    CliCommandHolder(CliCommandHolder const& right) = delete;
    CliCommandHolder& operator=(CliCommandHolder const& right) = delete;
};

// ============================================================================
// 会话映射类型定义
// ============================================================================
typedef std::unordered_map<uint32, WorldSession*> SessionMap;

// ============================================================================
// 角色信息结构体
// ============================================================================
// 职责：存储角色的基本信息
// 用途：用于快速查询角色信息而无需加载完整角色数据
// ============================================================================
struct CharacterInfo
{
    std::string Name;                   // 角色名称
    uint32 AccountId;                   // 账号ID
    uint8 Class;                        // 职业
    uint8 Race;                         // 种族
    uint8 Sex;                          // 性别
    uint8 Level;                        // 等级
    ObjectGuid::LowType GuildId;        // 公会ID
    uint32 ArenaTeamId[3];              // 竞技场队伍ID（2v2, 3v3, 5v5）
};

// ============================================================================
// 世界管理器类
// ============================================================================
// 职责：管理游戏世界的全局状态和核心功能
// 功能：
//   - 管理所有玩家会话
//   - 处理世界更新循环
//   - 管理服务器配置和倍率
//   - 处理服务器关闭和重启
//   - 管理定时任务（拍卖行、尸体清理等）
//   - 处理全局消息广播
// ============================================================================
/// The World
class TC_GAME_API World
{
    public:
        // ====================================================================
        // 单例模式
        // ====================================================================
        /// 获取世界实例（单例模式）
        static World* instance();

        static std::atomic<uint32> m_worldLoopCounter;  // 世界循环计数器

        // ====================================================================
        // 会话管理
        // ====================================================================
        /// 根据ID查找会话
        WorldSession* FindSession(uint32 id) const;
        /// 添加会话
        void AddSession(WorldSession* s);
        /// 发送自动广播
        void SendAutoBroadcast();
        /// 移除会话
        bool RemoveSession(uint32 id);
        /// 获取当前活跃会话数量并更新计数器
        void UpdateMaxSessionCounters();
        /// 获取所有会话映射
        SessionMap const& GetAllSessions() const { return m_sessions; }
        /// 获取活跃和排队中的会话总数
        uint32 GetActiveAndQueuedSessionCount() const { return m_sessions.size(); }
        /// 获取活跃会话数量
        uint32 GetActiveSessionCount() const { return m_sessions.size() - m_QueuedPlayer.size(); }
        /// 获取排队中的会话数量
        uint32 GetQueuedSessionCount() const { return m_QueuedPlayer.size(); }
        /// 获取自上次重启以来服务器最大并行会话数
        uint32 GetMaxQueuedSessionCount() const { return m_maxQueuedSessionCount; }
        uint32 GetMaxActiveSessionCount() const { return m_maxActiveSessionCount; }
        /// 获取玩家数量
        inline uint32 GetPlayerCount() const { return m_PlayerCount; }
        inline uint32 GetMaxPlayerCount() const { return m_MaxPlayerCount; }
        /// 增加/减少玩家数量
        inline void IncreasePlayerCount()
        {
            m_PlayerCount++;
            m_MaxPlayerCount = std::max(m_MaxPlayerCount, m_PlayerCount);
        }
        inline void DecreasePlayerCount() { m_PlayerCount--; }

        /// 在指定区域查找玩家
        Player* FindPlayerInZone(uint32 zone);

        // ====================================================================
        // 服务器状态控制
        // ====================================================================
        /// 服务器是否已关闭（拒绝客户端连接）
        bool IsClosed() const;
        /// 设置服务器关闭状态
        void SetClosed(bool val);

        // ====================================================================
        // 安全等级限制
        // ====================================================================
        /// 获取玩家安全等级限制
        AccountTypes GetPlayerSecurityLimit() const { return m_allowedSecurityLevel; }
        /// 设置玩家安全等级限制
        void SetPlayerSecurityLimit(AccountTypes sec);
        /// 从数据库加载允许的安全等级
        void LoadDBAllowedSecurityLevel();

        // ====================================================================
        // 玩家数量限制
        // ====================================================================
        /// 设置玩家数量限制
        void SetPlayerAmountLimit(uint32 limit) { m_playerLimit = limit; }
        uint32 GetPlayerAmountLimit() const { return m_playerLimit; }

        // ====================================================================
        // 玩家队列管理
        // ====================================================================
        typedef std::list<WorldSession*> Queue;
        /// 添加玩家到队列
        void AddQueuedPlayer(WorldSession*);
        /// 从队列移除玩家
        bool RemoveQueuedPlayer(WorldSession* session);
        /// 获取玩家在队列中的位置
        int32 GetQueuePos(WorldSession*);
        /// 检查是否最近断开连接
        bool HasRecentlyDisconnected(WorldSession*);

        // ====================================================================
        // 移动控制
        // ====================================================================
        /// 是否允许移动
        bool getAllowMovement() const { return m_allowMovement; }
        /// 设置是否允许移动
        void SetAllowMovement(bool allow) { m_allowMovement = allow; }

        // ====================================================================
        // 新角色设置
        // ====================================================================
        /// 设置新角色欢迎字符串（首次登录显示）
        void SetNewCharString(std::string const& str) { m_newCharString = str; }
        /// 获取新角色欢迎字符串
        std::string const& GetNewCharString() const { return m_newCharString; }

        // ====================================================================
        // 语言和本地化
        // ====================================================================
        /// 获取默认DBC语言
        LocaleConstant GetDefaultDbcLocale() const { return m_defaultDbcLocale; }

        /// 获取数据文件存储路径（dbc、maps等）
        std::string const& GetDataPath() const { return m_dataPath; }

        // ====================================================================
        // 任务重置时间
        // ====================================================================
        /// 获取下次日常任务重置时间
        time_t GetNextDailyQuestsResetTime() const { return m_NextDailyQuestReset; }
        /// 获取下次周常任务重置时间
        time_t GetNextWeeklyQuestsResetTime() const { return m_NextWeeklyQuestReset; }
        /// 获取下次随机战场重置时间
        time_t GetNextRandomBGResetTime() const { return m_NextRandomBGReset; }

        /// 获取玩家可达到的最大技能等级
        uint16 GetConfigMaxSkillValue() const
        {
            uint16 lvl = uint16(getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            return lvl > 60 ? 300 + ((lvl - 60) * 75) / 10 : lvl * 5;
        }

        // ====================================================================
        // 世界初始化
        // ====================================================================
        /// 设置初始世界设置
        void SetInitialWorldSettings();
        /// 加载配置设置
        void LoadConfigSettings(bool reload = false);

        // ====================================================================
        // 全局消息发送
        // ====================================================================
        /// 发送世界文本消息（支持可变参数）
        void SendWorldText(uint32 string_id, ...);
        /// 发送全局文本消息
        void SendGlobalText(char const* text, WorldSession* self);
        /// 发送GM文本消息
        void SendGMText(uint32 string_id, ...);
        /// 发送服务器消息
        void SendServerMessage(ServerMessageType messageID, std::string stringParam = "", Player* player = nullptr);
        /// 发送全局数据包
        void SendGlobalMessage(WorldPacket const* packet, WorldSession* self = nullptr, uint32 team = 0);
        /// 发送全局GM消息
        void SendGlobalGMMessage(WorldPacket const* packet, WorldSession* self = nullptr, uint32 team = 0);

        // ====================================================================
        // 服务器关闭和重启
        // ====================================================================
        /// 是否正在关闭服务器
        bool IsShuttingDown() const { return m_ShutdownTimer > 0; }
        /// 获取关闭倒计时剩余时间
        uint32 GetShutDownTimeLeft() const { return m_ShutdownTimer; }
        /// 启动服务器关闭流程
        void ShutdownServ(uint32 time, uint32 options, uint8 exitcode, const std::string& reason = std::string());
        /// 取消服务器关闭
        uint32 ShutdownCancel();
        /// 发送关闭消息
        void ShutdownMsg(bool show = false, Player* player = nullptr, const std::string& reason = std::string());
        /// 获取退出代码
        static uint8 GetExitCode() { return m_ExitCode; }
        /// 立即停止服务器
        static void StopNow(uint8 exitcode) { m_stopEvent = true; m_ExitCode = exitcode; }
        /// 服务器是否已停止
        static bool IsStopped() { return m_stopEvent; }

        // ====================================================================
        // 世界更新
        // ====================================================================
        /// 更新世界状态（主循环）
        void Update(uint32 diff);

        /// 更新所有会话
        void UpdateSessions(uint32 diff);

        // ====================================================================
        // 倍率配置
        // ====================================================================
        /// 设置服务器倍率
        void setRate(Rates rate, float value) { rate_values[rate]=value; }
        /// 获取服务器倍率
        float getRate(Rates rate) const { return rate_values[rate]; }

        // ====================================================================
        // 布尔配置
        // ====================================================================
        /// 设置布尔配置项
        void setBoolConfig(WorldBoolConfigs index, bool value)
        {
            if (index < BOOL_CONFIG_VALUE_COUNT)
                m_bool_configs[index] = value;
        }
        /// 获取布尔配置项
        bool getBoolConfig(WorldBoolConfigs index) const
        {
            return index < BOOL_CONFIG_VALUE_COUNT ? m_bool_configs[index] : 0;
        }

        // ====================================================================
        // 浮点数配置
        // ====================================================================
        /// 设置浮点数配置项
        void setFloatConfig(WorldFloatConfigs index, float value)
        {
            if (index < FLOAT_CONFIG_VALUE_COUNT)
                m_float_configs[index] = value;
        }
        /// 获取浮点数配置项
        float getFloatConfig(WorldFloatConfigs index) const
        {
            return index < FLOAT_CONFIG_VALUE_COUNT ? m_float_configs[index] : 0;
        }

        // ====================================================================
        // 整数配置
        // ====================================================================
        /// 设置整数配置项
        void setIntConfig(WorldIntConfigs index, uint32 value)
        {
            if (index < INT_CONFIG_VALUE_COUNT)
                m_int_configs[index] = value;
        }
        /// 获取整数配置项
        uint32 getIntConfig(WorldIntConfigs index) const
        {
            return index < INT_CONFIG_VALUE_COUNT ? m_int_configs[index] : 0;
        }

        // ====================================================================
        // 世界状态
        // ====================================================================
        /// 设置世界状态值
        void setWorldState(uint32 index, uint64 value);
        /// 获取世界状态值
        uint64 getWorldState(uint32 index) const;
        /// 加载世界状态
        void LoadWorldStates();

        // ====================================================================
        // PvP服务器判断
        // ====================================================================
        /// 是否为PvP服务器
        bool IsPvPRealm() const;
        /// 是否为自由PvP服务器
        bool IsFFAPvPRealm() const;

        // ====================================================================
        // 踢出和封禁管理
        // ====================================================================
        /// 踢出所有玩家
        void KickAll();
        /// 踢出指定安全等级以下的玩家
        void KickAllLess(AccountTypes sec);
        /// 封禁账号
        BanReturn BanAccount(BanMode mode, std::string const& nameOrIP, std::string const& duration, std::string const& reason, std::string const& author);
        BanReturn BanAccount(BanMode mode, std::string const& nameOrIP, uint32 duration_secs, std::string const& reason, std::string const& author);
        /// 解除账号封禁
        bool RemoveBanAccount(BanMode mode, std::string const& nameOrIP);
        /// 封禁角色
        BanReturn BanCharacter(std::string const& name, std::string const& duration, std::string const& reason, std::string const& author);
        /// 解除角色封禁
        bool RemoveBanCharacter(std::string const& name);

        // ====================================================================
        // 可见性距离配置（用于最大速度访问）
        // ====================================================================
        /// 获取大陆上的最大可见距离
        static float GetMaxVisibleDistanceOnContinents()    { return m_MaxVisibleDistanceOnContinents; }
        /// 获取副本中的最大可见距离
        static float GetMaxVisibleDistanceInInstances()     { return m_MaxVisibleDistanceInInstances;  }
        /// 获取战场中的最大可见距离
        static float GetMaxVisibleDistanceInBG()            { return m_MaxVisibleDistanceInBG;         }
        /// 获取竞技场中的最大可见距离
        static float GetMaxVisibleDistanceInArenas()        { return m_MaxVisibleDistanceInArenas;     }

        // ====================================================================
        // 可见性通知周期配置
        // ====================================================================
        /// 获取大陆上的可见性通知周期
        static int32 GetVisibilityNotifyPeriodOnContinents(){ return m_visibility_notify_periodOnContinents; }
        /// 获取副本中的可见性通知周期
        static int32 GetVisibilityNotifyPeriodInInstances() { return m_visibility_notify_periodInInstances;  }
        /// 获取战场中的可见性通知周期
        static int32 GetVisibilityNotifyPeriodInBG()        { return m_visibility_notify_periodInBG;         }
        /// 获取竞技场中的可见性通知周期
        static int32 GetVisibilityNotifyPeriodInArenas()    { return m_visibility_notify_periodInArenas;     }

        // ====================================================================
        // CLI命令处理
        // ====================================================================
        /// 处理CLI命令
        void ProcessCliCommands();
        /// 将CLI命令加入队列
        void QueueCliCommand(CliCommandHolder* commandHolder) { cliCmdQueue.add(commandHolder); }

        /// 强制游戏事件更新
        void ForceGameEventUpdate();

        /// 更新领域角色计数
        void UpdateRealmCharCount(uint32 accid);

        /// 获取可用的DBC语言
        LocaleConstant GetAvailableDbcLocale(LocaleConstant locale) const { if (m_availableDbcLocaleMask & (1 << locale)) return locale; else return m_defaultDbcLocale; }

        // ====================================================================
        // 数据库版本管理
        // ====================================================================
        /// 加载数据库版本
        void LoadDBVersion();
        /// 获取数据库版本
        char const* GetDBVersion() const { return m_DBVersion.c_str(); }

        /// 加载自动广播消息
        void LoadAutobroadcasts();

        /// 更新区域依赖光环
        void UpdateAreaDependentAuras();

        // ====================================================================
        // 清理标志
        // ====================================================================
        /// 获取清理标志
        uint32 GetCleaningFlags() const { return m_CleaningFlags; }
        /// 设置清理标志
        void SetCleaningFlags(uint32 flags) { m_CleaningFlags = flags; }
        /// 重置事件季节性任务
        void ResetEventSeasonalQuests(uint16 event_id);

        /// 重新加载RBAC权限
        void ReloadRBAC();

        // ====================================================================
        // GUID警告和清理
        // ====================================================================
        /// 移除旧尸体
        void RemoveOldCorpses();
        /// 触发GUID警告
        void TriggerGuidWarning();
        /// 触发GUID警报
        void TriggerGuidAlert();
        /// 是否有GUID警告
        bool IsGuidWarning() { return _guidWarn; }
        /// 是否有GUID警报
        bool IsGuidAlert() { return _guidAlert; }

    protected:
        // ====================================================================
        // 游戏时间更新
        // ====================================================================
        /// 更新游戏时间
        void _UpdateGameTime();

        /// 更新领域角色计数回调函数
        void _UpdateRealmCharCount(PreparedQueryResult resultCharCount);

        // ====================================================================
        // 任务重置时间管理
        // ====================================================================
        /// 初始化任务重置时间
        void InitQuestResetTimes();
        /// 检查任务重置时间
        void CheckQuestResetTimes();
        /// 重置日常任务
        void ResetDailyQuests();
        /// 重置周常任务
        void ResetWeeklyQuests();
        /// 重置月常任务
        void ResetMonthlyQuests();

        // ====================================================================
        // 重置时间初始化
        // ====================================================================
        /// 初始化随机战场重置时间
        void InitRandomBGResetTime();
        /// 初始化日历旧事件删除时间
        void InitCalendarOldEventsDeletionTime();
        /// 初始化公会重置时间
        void InitGuildResetTime();
        /// 重置随机战场
        void ResetRandomBG();
        /// 删除旧日历事件
        void CalendarDeleteOldEvents();
        /// 重置公会上限
        void ResetGuildCap();
    private:
        // ====================================================================
        // 构造和析构
        // ====================================================================
        World();
        ~World();

        // ====================================================================
        // 服务器停止相关静态变量
        // ====================================================================
        static std::atomic<bool> m_stopEvent;      // 停止事件标志
        static uint8 m_ExitCode;                   // 退出代码
        uint32 m_ShutdownTimer;                    // 关闭倒计时（秒）
        uint32 m_ShutdownMask;                     // 关闭掩码

        uint32 m_CleaningFlags;                    // 清理标志

        bool m_isClosed;                           // 服务器是否关闭

        // ====================================================================
        // 定时器和时间管理
        // ====================================================================
        IntervalTimer m_timers[WUPDATE_COUNT];     // 定时器数组
        time_t mail_timer;                         // 邮件定时器
        time_t mail_timer_expires;                 // 邮件定时器过期时间

        // ====================================================================
        // 会话管理
        // ====================================================================
        SessionMap m_sessions;                     // 会话映射
        typedef std::unordered_map<uint32, time_t> DisconnectMap;
        DisconnectMap m_disconnects;               // 断开连接映射
        uint32 m_maxActiveSessionCount;            // 最大活跃会话数
        uint32 m_maxQueuedSessionCount;            // 最大排队会话数
        uint32 m_PlayerCount;                      // 当前玩家数量
        uint32 m_MaxPlayerCount;                   // 最大玩家数量

        std::string m_newCharString;               // 新角色欢迎字符串

        // ====================================================================
        // 配置和倍率
        // ====================================================================
        float rate_values[MAX_RATES];              // 倍率值数组
        uint32 m_int_configs[INT_CONFIG_VALUE_COUNT];      // 整数配置数组
        bool m_bool_configs[BOOL_CONFIG_VALUE_COUNT];      // 布尔配置数组
        float m_float_configs[FLOAT_CONFIG_VALUE_COUNT];   // 浮点数配置数组
        typedef std::map<uint32, uint64> WorldStatesMap;
        WorldStatesMap m_worldstates;              // 世界状态映射
        uint32 m_playerLimit;                      // 玩家数量限制
        AccountTypes m_allowedSecurityLevel;       // 允许的安全等级
        LocaleConstant m_defaultDbcLocale;         // 默认DBC语言（从配置加载）
        uint32 m_availableDbcLocaleMask;           // 可用的DBC语言掩码（根据已加载的DBC）
        void DetectDBCLang();                      // 检测DBC语言
        bool m_allowMovement;                      // 是否允许移动
        std::string m_dataPath;                    // 数据文件路径（dbc、maps等）

        // ====================================================================
        // 可见性距离配置（用于最大速度访问）
        // ====================================================================
        static float m_MaxVisibleDistanceOnContinents;    // 大陆上的最大可见距离
        static float m_MaxVisibleDistanceInInstances;     // 副本中的最大可见距离
        static float m_MaxVisibleDistanceInBG;            // 战场中的最大可见距离
        static float m_MaxVisibleDistanceInArenas;        // 竞技场中的最大可见距离

        static int32 m_visibility_notify_periodOnContinents;  // 大陆上的可见性通知周期
        static int32 m_visibility_notify_periodInInstances;   // 副本中的可见性通知周期
        static int32 m_visibility_notify_periodInBG;          // 战场中的可见性通知周期
        static int32 m_visibility_notify_periodInArenas;      // 竞技场中的可见性通知周期

        // ====================================================================
        // CLI命令队列（线程安全）
        // ====================================================================
        LockedQueue<CliCommandHolder*> cliCmdQueue;       // CLI命令队列

        // ====================================================================
        // 重置时间
        // ====================================================================
        time_t m_NextDailyQuestReset;                     // 下次日常任务重置时间
        time_t m_NextWeeklyQuestReset;                    // 下次周常任务重置时间
        time_t m_NextMonthlyQuestReset;                   // 下次月常任务重置时间
        time_t m_NextRandomBGReset;                       // 下次随机战场重置时间
        time_t m_NextCalendarOldEventsDeletionTime;       // 下次日历旧事件删除时间
        time_t m_NextGuildReset;                          // 下次公会重置时间

        // ====================================================================
        // 玩家队列
        // ====================================================================
        Queue m_QueuedPlayer;                             // 排队玩家队列

        // ====================================================================
        // 异步会话添加
        // ====================================================================
        void AddSession_(WorldSession* s);                // 添加会话（内部实现）
        LockedQueue<WorldSession*> addSessQueue;          // 异步添加会话队列

        // ====================================================================
        // 数据库版本
        // ====================================================================
        std::string m_DBVersion;                          // 数据库版本字符串

        // ====================================================================
        // 自动广播
        // ====================================================================
        typedef std::map<uint8, std::string> AutobroadcastsMap;
        AutobroadcastsMap m_Autobroadcasts;               // 自动广播消息映射
        typedef std::map<uint8, uint8> AutobroadcastsWeightMap;
        AutobroadcastsWeightMap m_AutobroadcastsWeights;  // 自动广播权重映射

        void ProcessQueryCallbacks();                     // 处理查询回调

        // ====================================================================
        // GUID警告和警报
        // ====================================================================
        void SendGuidWarning();                           // 发送GUID警告
        void DoGuidWarningRestart();                      // 执行GUID警告重启
        void DoGuidAlertRestart();                        // 执行GUID警报重启
        QueryCallbackProcessor _queryProcessor;           // 查询回调处理器

        std::string _guidWarningMsg;                      // GUID警告消息
        std::string _alertRestartReason;                  // 警报重启原因

        std::mutex _guidAlertLock;                        // GUID警报锁

        bool _guidWarn;                                   // GUID警告标志
        bool _guidAlert;                                  // GUID警报标志
        uint32 _warnDiff;                                 // 警告间隔
        time_t _warnShutdownTime;                         // 警告关闭时间

    friend class debug_commandscript;
};

// ============================================================================
// 全局变量和宏定义
// ============================================================================
TC_GAME_API extern Realm realm;           // 领域全局变量

#define sWorld World::instance()          // 世界管理器单例访问宏

#endif
/// @}
