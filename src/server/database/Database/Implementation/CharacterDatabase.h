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
 * @file CharacterDatabase.h
 * @brief 角色数据库连接模块
 *
 * 本文件定义了角色数据库连接类和相关的预处理 SQL 语句枚举。
 * 角色数据库用于存储玩家角色的所有持久化数据，包括：
 * - 角色基础信息（姓名、等级、位置等）
 * - 物品和装备
 * - 技能和天赋
 * - 任务进度
 * - 公会信息
 * - 竞技场队伍
 * - 邮件系统
 * - 拍卖行数据
 * - 成就和声望
 * - 好友列表
 * - 宠物和坐骑
 */

#ifndef _CHARACTERDATABASE_H
#define _CHARACTERDATABASE_H

#include "MySQLConnection.h"

/**
 * @brief 角色数据库预处理语句枚举
 *
 * 定义了角色数据库所有预处理 SQL 语句的标识符。
 * 这些预处理语句用于高效执行常用的数据库操作。
 *
 * 命名规范：
 * - {DB}_{操作类型}_{数据描述}
 * - 操作类型：SEL(查询)、INS(插入)、UPD(更新)、DEL(删除)、REP(替换)
 * - 当更新多个字段时，参考调用函数名称确定合适的后缀
 */
enum CharacterDatabaseStatements : uint32
{
    /*  Naming standard for defines:
        {DB}_{SEL/INS/UPD/DEL/REP}_{Summary of data changed}
        When updating more than one field, consider looking at the calling function
        name for a suiting suffix.
    */

    // 任务池保存相关
    CHAR_DEL_POOL_QUEST_SAVE,                    ///< 删除任务池保存记录
    CHAR_INS_POOL_QUEST_SAVE,                    ///< 插入任务池保存记录

    // 公会银行物品清理
    CHAR_DEL_NONEXISTENT_GUILD_BANK_ITEM,        ///< 删除不存在的公会银行物品

    // 封禁管理
    CHAR_DEL_EXPIRED_BANS,                       ///< 删除已过期的封禁记录

    // 角色验证相关
    CHAR_SEL_CHECK_NAME,                         ///< 检查角色名是否存在
    CHAR_SEL_CHECK_GUID,                         ///< 检查角色GUID是否存在
    CHAR_SEL_SUM_CHARS,                          ///< 统计角色总数
    CHAR_SEL_CHAR_CREATE_INFO,                   ///< 获取角色创建信息

    // 角色封禁操作
    CHAR_INS_CHARACTER_BAN,                      ///< 插入角色封禁记录
    CHAR_UPD_CHARACTER_BAN,                      ///< 更新角色封禁信息
    CHAR_DEL_CHARACTER_BAN,                      ///< 删除角色封禁记录
    CHAR_SEL_BANINFO,                            ///< 查询封禁信息
    CHAR_SEL_GUID_BY_NAME_FILTER,                ///< 根据名称过滤器查询GUID
    CHAR_SEL_BANINFO_LIST,                       ///< 查询封禁列表
    CHAR_SEL_BANNED_NAME,                        ///< 查询被封禁的角色名

    // 邮件列表
    CHAR_SEL_MAIL_LIST_COUNT,                    ///< 查询邮件列表数量
    CHAR_SEL_MAIL_LIST_INFO,                     ///< 查询邮件列表信息
    CHAR_SEL_MAIL_LIST_ITEMS,                    ///< 查询邮件列表物品

    // 角色枚举（角色选择界面）
    CHAR_SEL_ENUM,                               ///< 查询角色枚举数据（角色选择界面）
    CHAR_SEL_ENUM_DECLINED_NAME,                 ///< 查询角色格变名称（俄语等语言）
    CHAR_SEL_FREE_NAME,                          ///< 查询可用的角色名
    CHAR_SEL_CHAR_ZONE,                          ///< 查询角色所在区域
    CHAR_SEL_CHARACTER_NAME_DATA,                ///< 查询角色名称数据
    CHAR_SEL_CHAR_POSITION_XYZ,                  ///< 查询角色精确位置坐标
    CHAR_SEL_CHAR_POSITION,                      ///< 查询角色位置信息

    // 战场随机任务
    CHAR_DEL_BATTLEGROUND_RANDOM_ALL,            ///< 删除所有战场随机任务记录
    CHAR_DEL_BATTLEGROUND_RANDOM,                ///< 删除指定战场随机任务记录
    CHAR_INS_BATTLEGROUND_RANDOM,                ///< 插入战场随机任务记录

    // 角色数据加载
    CHAR_SEL_CHARACTER,                          ///< 查询角色基础数据
    CHAR_SEL_GROUP_MEMBER,                       ///< 查询小队成员信息
    CHAR_SEL_CHARACTER_INSTANCE,                 ///< 查询角色副本绑定信息
    CHAR_SEL_CHARACTER_AURAS,                    ///< 查询角色光环效果
    CHAR_SEL_CHARACTER_SPELL,                    ///< 查询角色技能列表
    CHAR_SEL_CHARACTER_QUESTSTATUS,              ///< 查询角色任务状态

    // 每日/每周/每月/季节性任务
    CHAR_SEL_CHARACTER_QUESTSTATUS_DAILY,        ///< 查询每日任务状态
    CHAR_SEL_CHARACTER_QUESTSTATUS_WEEKLY,       ///< 查询每周任务状态
    CHAR_SEL_CHARACTER_QUESTSTATUS_MONTHLY,      ///< 查询每月任务状态
    CHAR_SEL_CHARACTER_QUESTSTATUS_SEASONAL,     ///< 查询季节性任务状态
    CHAR_DEL_CHARACTER_QUESTSTATUS_DAILY,        ///< 删除每日任务状态
    CHAR_DEL_CHARACTER_QUESTSTATUS_WEEKLY,       ///< 删除每周任务状态
    CHAR_DEL_CHARACTER_QUESTSTATUS_MONTHLY,      ///< 删除每月任务状态
    CHAR_DEL_CHARACTER_QUESTSTATUS_SEASONAL,     ///< 删除季节性任务状态
    CHAR_INS_CHARACTER_QUESTSTATUS_DAILY,        ///< 插入每日任务状态
    CHAR_INS_CHARACTER_QUESTSTATUS_WEEKLY,       ///< 插入每周任务状态
    CHAR_INS_CHARACTER_QUESTSTATUS_MONTHLY,      ///< 插入每月任务状态
    CHAR_INS_CHARACTER_QUESTSTATUS_SEASONAL,     ///< 插入季节性任务状态
    CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_DAILY,      ///< 重置所有角色的每日任务
    CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_WEEKLY,     ///< 重置所有角色的每周任务
    CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_MONTHLY,    ///< 重置所有角色的每月任务
    CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_SEASONAL_BY_EVENT,  ///< 根据事件重置季节性任务

    // 角色详细数据
    CHAR_SEL_CHARACTER_REPUTATION,               ///< 查询角色声望数据
    CHAR_SEL_CHARACTER_INVENTORY,                ///< 查询角色物品栏数据
    CHAR_SEL_CHARACTER_ACTIONS,                  ///< 查询角色动作条数据
    CHAR_SEL_CHARACTER_ACTIONS_SPEC,             ///< 查询角色专精动作条数据
    CHAR_SEL_MAIL_COUNT,                         ///< 查询邮件数量
    CHAR_SEL_CHARACTER_SOCIALLIST,               ///< 查询角色社交列表（好友/屏蔽）
    CHAR_SEL_CHARACTER_HOMEBIND,                 ///< 查询角色炉石绑定位置
    CHAR_SEL_CHARACTER_SPELLCOOLDOWNS,           ///< 查询角色技能冷却时间
    CHAR_SEL_CHARACTER_DECLINEDNAMES,            ///< 查询角色格变名称数据
    CHAR_SEL_GUILD_MEMBER,                       ///< 查询公会成员数据
    CHAR_SEL_GUILD_MEMBER_EXTENDED,              ///< 查询公会成员扩展数据
    CHAR_SEL_CHARACTER_ARENAINFO,                ///< 查询角色竞技场信息
    CHAR_SEL_CHARACTER_ACHIEVEMENTS,             ///< 查询角色成就数据
    CHAR_SEL_CHARACTER_CRITERIAPROGRESS,         ///< 查询角色成就条件进度
    CHAR_SEL_CHARACTER_EQUIPMENTSETS,            ///< 查询角色装备套装
    CHAR_SEL_CHARACTER_BGDATA,                   ///< 查询角色战场数据
    CHAR_SEL_CHARACTER_GLYPHS,                   ///< 查询角色雕文数据
    CHAR_SEL_CHARACTER_TALENTS,                  ///< 查询角色天赋数据
    CHAR_SEL_CHARACTER_SKILLS,                   ///< 查询角色技能熟练度
    CHAR_SEL_CHARACTER_RANDOMBG,                 ///< 查询角色随机战场数据
    CHAR_SEL_CHARACTER_BANNED,                   ///< 查询角色是否被封禁
    CHAR_SEL_CHARACTER_QUESTSTATUSREW,           ///< 查询角色已完成任务奖励
    CHAR_SEL_ACCOUNT_INSTANCELOCKTIMES,          ///< 查询账号副本锁定时间
    CHAR_SEL_MAILITEMS,                          ///< 查询邮件物品

    // 拍卖行操作
    CHAR_SEL_AUCTION_ITEMS,                      ///< 查询拍卖行物品
    CHAR_INS_AUCTION,                            ///< 插入拍卖记录
    CHAR_DEL_AUCTION,                            ///< 删除拍卖记录
    CHAR_UPD_AUCTION_BID,                        ///< 更新拍卖竞价
    CHAR_SEL_AUCTIONS,                           ///< 查询拍卖列表
    CHAR_SEL_AUCTION_BIDDERS,                    ///< 查询拍卖竞拍者
    CHAR_INS_AUCTION_BIDDERS,                    ///< 插入拍卖竞拍者记录
    CHAR_DEL_AUCTION_BIDDERS,                    ///< 删除拍卖竞拍者记录

    // 邮件系统操作
    CHAR_INS_MAIL,                               ///< 插入邮件
    CHAR_DEL_MAIL_BY_ID,                         ///< 根据ID删除邮件
    CHAR_INS_MAIL_ITEM,                          ///< 插入邮件物品
    CHAR_DEL_MAIL_ITEM,                          ///< 删除邮件物品
    CHAR_DEL_INVALID_MAIL_ITEM,                  ///< 删除无效的邮件物品
    CHAR_DEL_EMPTY_EXPIRED_MAIL,                 ///< 删除空的过期邮件
    CHAR_SEL_EXPIRED_MAIL,                       ///< 查询过期邮件
    CHAR_SEL_EXPIRED_MAIL_ITEMS,                 ///< 查询过期邮件物品
    CHAR_UPD_MAIL_RETURNED,                      ///< 更新邮件为已退回
    CHAR_UPD_MAIL_ITEM_RECEIVER,                 ///< 更新邮件物品接收者
    CHAR_UPD_ITEM_OWNER,                         ///< 更新物品所有者

    // 物品退还和交易
    CHAR_SEL_ITEM_REFUNDS,                       ///< 查询物品退还信息
    CHAR_SEL_ITEM_BOP_TRADE,                     ///< 查询拾取绑定物品交易信息
    CHAR_DEL_ITEM_BOP_TRADE,                     ///< 删除拾取绑定物品交易记录
    CHAR_INS_ITEM_BOP_TRADE,                     ///< 插入拾取绑定物品交易记录

    // 物品实例管理
    CHAR_REP_INVENTORY_ITEM,                     ///< 替换物品栏物品
    CHAR_REP_ITEM_INSTANCE,                      ///< 替换物品实例
    CHAR_UPD_ITEM_INSTANCE,                      ///< 更新物品实例
    CHAR_UPD_ITEM_INSTANCE_ON_LOAD,              ///< 加载时更新物品实例
    CHAR_DEL_ITEM_INSTANCE,                      ///< 删除物品实例
    CHAR_DEL_ITEM_INSTANCE_BY_OWNER,             ///< 根据所有者删除物品实例

    // 礼物系统
    CHAR_UPD_GIFT_OWNER,                         ///< 更新礼物所有者
    CHAR_DEL_GIFT,                               ///< 删除礼物
    CHAR_SEL_CHARACTER_GIFT_BY_ITEM,             ///< 根据物品查询角色礼物

    // 账号相关
    CHAR_SEL_ACCOUNT_BY_NAME,                    ///< 根据名称查询账号
    CHAR_UPD_ACCOUNT_BY_GUID,                    ///< 根据GUID更新账号
    CHAR_DEL_ACCOUNT_INSTANCE_LOCK_TIMES,        ///< 删除账号副本锁定时间
    CHAR_INS_ACCOUNT_INSTANCE_LOCK_TIMES,        ///< 插入账号副本锁定时间
    CHAR_SEL_MATCH_MAKER_RATING,                 ///< 查询匹配评分
    CHAR_SEL_CHARACTER_COUNT,                    ///< 查询角色数量
    CHAR_UPD_NAME_BY_GUID,                       ///< 根据GUID更新名称
    CHAR_DEL_DECLINED_NAME,                      ///< 删除格变名称

    // 公会系统
    CHAR_INS_GUILD,                              ///< 插入公会
    CHAR_DEL_GUILD,                              ///< 删除公会
    CHAR_UPD_GUILD_NAME,                         ///< 更新公会名称
    CHAR_INS_GUILD_MEMBER,                       ///< 插入公会成员
    CHAR_DEL_GUILD_MEMBER,                       ///< 删除公会成员
    CHAR_DEL_GUILD_MEMBERS,                      ///< 删除所有公会成员
    CHAR_INS_GUILD_RANK,                         ///< 插入公会等级
    CHAR_DEL_GUILD_RANKS,                        ///< 删除公会所有等级
    CHAR_DEL_GUILD_LOWEST_RANK,                  ///< 删除公会最低等级

    // 公会银行标签页
    CHAR_INS_GUILD_BANK_TAB,                     ///< 插入公会银行标签页
    CHAR_DEL_GUILD_BANK_TAB,                     ///< 删除公会银行标签页
    CHAR_DEL_GUILD_BANK_TABS,                    ///< 删除所有公会银行标签页

    // 公会银行物品
    CHAR_INS_GUILD_BANK_ITEM,                    ///< 插入公会银行物品
    CHAR_DEL_GUILD_BANK_ITEM,                    ///< 删除公会银行物品
    CHAR_DEL_GUILD_BANK_ITEMS,                   ///< 删除所有公会银行物品

    // 公会银行权限
    CHAR_INS_GUILD_BANK_RIGHT,                   ///< 插入公会银行权限
    CHAR_DEL_GUILD_BANK_RIGHTS,                  ///< 删除所有公会银行权限
    CHAR_DEL_GUILD_BANK_RIGHTS_FOR_RANK,         ///< 删除指定等级的公会银行权限

    // 公会银行日志
    CHAR_INS_GUILD_BANK_EVENTLOG,                ///< 插入公会银行事件日志
    CHAR_DEL_GUILD_BANK_EVENTLOG,                ///< 删除公会银行事件日志
    CHAR_DEL_GUILD_BANK_EVENTLOGS,               ///< 删除所有公会银行事件日志

    // 公会事件日志
    CHAR_INS_GUILD_EVENTLOG,                     ///< 插入公会事件日志
    CHAR_DEL_GUILD_EVENTLOG,                     ///< 删除公会事件日志
    CHAR_DEL_GUILD_EVENTLOGS,                    ///< 删除所有公会事件日志

    // 公会成员信息更新
    CHAR_UPD_GUILD_MEMBER_PNOTE,                 ///< 更新公会成员个人备注
    CHAR_UPD_GUILD_MEMBER_OFFNOTE,               ///< 更新公会成员官员备注
    CHAR_UPD_GUILD_MEMBER_RANK,                  ///< 更新公会成员等级

    // 公会信息更新
    CHAR_UPD_GUILD_MOTD,                         ///< 更新公会公告
    CHAR_UPD_GUILD_INFO,                         ///< 更新公会信息
    CHAR_UPD_GUILD_LEADER,                       ///< 更新公会会长
    CHAR_UPD_GUILD_RANK_NAME,                    ///< 更新公会等级名称
    CHAR_UPD_GUILD_RANK_RIGHTS,                  ///< 更新公会等级权限
    CHAR_UPD_GUILD_EMBLEM_INFO,                  ///< 更新公会徽章信息
    CHAR_UPD_GUILD_BANK_TAB_INFO,                ///< 更新公会银行标签页信息
    CHAR_UPD_GUILD_BANK_MONEY,                   ///< 更新公会银行金币
    CHAR_UPD_GUILD_BANK_EVENTLOG_TAB,            ///< 更新公会银行事件日志标签页
    CHAR_UPD_GUILD_RANK_BANK_MONEY,              ///< 更新公会等级银行金币限额
    CHAR_UPD_GUILD_BANK_TAB_TEXT,                ///< 更新公会银行标签页文本

    // 公会成员取款记录
    CHAR_INS_GUILD_MEMBER_WITHDRAW,              ///< 插入公会成员取款记录
    CHAR_DEL_GUILD_MEMBER_WITHDRAW,              ///< 删除公会成员取款记录
    CHAR_SEL_CHAR_DATA_FOR_GUILD,                ///< 查询公会的角色数据

    // 聊天频道
    CHAR_UPD_CHANNEL,                            ///< 更新聊天频道
    CHAR_UPD_CHANNEL_USAGE,                      ///< 更新频道使用情况
    CHAR_UPD_CHANNEL_OWNERSHIP,                  ///< 更新频道所有权
    CHAR_DEL_CHANNEL,                            ///< 删除聊天频道
    CHAR_DEL_OLD_CHANNELS,                       ///< 删除旧聊天频道

    // 装备套装管理
    CHAR_UPD_EQUIP_SET,                          ///< 更新装备套装
    CHAR_INS_EQUIP_SET,                          ///< 插入装备套装
    CHAR_DEL_EQUIP_SET,                          ///< 删除装备套装

    // 光环效果
    CHAR_INS_AURA,                               ///< 插入光环效果

    // 账号数据（缓存数据）
    CHAR_SEL_ACCOUNT_DATA,                       ///< 查询账号数据
    CHAR_REP_ACCOUNT_DATA,                       ///< 替换账号数据
    CHAR_DEL_ACCOUNT_DATA,                       ///< 删除账号数据
    CHAR_SEL_PLAYER_ACCOUNT_DATA,                ///< 查询玩家账号数据
    CHAR_REP_PLAYER_ACCOUNT_DATA,                ///< 替换玩家账号数据
    CHAR_DEL_PLAYER_ACCOUNT_DATA,                ///< 删除玩家账号数据

    // 教程提示
    CHAR_SEL_TUTORIALS,                          ///< 查询教程提示数据
    CHAR_INS_TUTORIALS,                          ///< 插入教程提示数据
    CHAR_UPD_TUTORIALS,                          ///< 更新教程提示数据
    CHAR_DEL_TUTORIALS,                          ///< 删除教程提示数据

    // 副本保存
    CHAR_INS_INSTANCE_SAVE,                      ///< 插入副本保存记录
    CHAR_UPD_INSTANCE_DATA,                      ///< 更新副本数据

    // 游戏事件保存
    CHAR_DEL_GAME_EVENT_SAVE,                    ///< 删除游戏事件保存记录
    CHAR_INS_GAME_EVENT_SAVE,                    ///< 插入游戏事件保存记录

    // 游戏事件条件保存
    CHAR_DEL_ALL_GAME_EVENT_CONDITION_SAVE,      ///< 删除所有游戏事件条件保存
    CHAR_DEL_GAME_EVENT_CONDITION_SAVE,          ///< 删除游戏事件条件保存
    CHAR_INS_GAME_EVENT_CONDITION_SAVE,          ///< 插入游戏事件条件保存

    // 竞技场队伍
    CHAR_INS_ARENA_TEAM,                         ///< 插入竞技场队伍
    CHAR_INS_ARENA_TEAM_MEMBER,                  ///< 插入竞技场队伍成员
    CHAR_DEL_ARENA_TEAM,                         ///< 删除竞技场队伍
    CHAR_DEL_ARENA_TEAM_MEMBERS,                 ///< 删除竞技场队伍所有成员
    CHAR_UPD_ARENA_TEAM_CAPTAIN,                 ///< 更新竞技场队长
    CHAR_DEL_ARENA_TEAM_MEMBER,                  ///< 删除竞技场队伍成员
    CHAR_UPD_ARENA_TEAM_STATS,                   ///< 更新竞技场队伍统计
    CHAR_UPD_ARENA_TEAM_MEMBER,                  ///< 更新竞技场队伍成员
    CHAR_DEL_CHARACTER_ARENA_STATS,              ///< 删除角色竞技场统计
    CHAR_REP_CHARACTER_ARENA_STATS,              ///< 替换角色竞技场统计
    CHAR_UPD_ARENA_TEAM_NAME,                    ///< 更新竞技场队伍名称

    // 请愿书系统（公会/竞技场创建）
    CHAR_SEL_PETITION,                           ///< 查询请愿书
    CHAR_SEL_PETITION_SIGNATURE,                 ///< 查询请愿书签名
    CHAR_DEL_ALL_PETITION_SIGNATURES,            ///< 删除所有请愿书签名
    CHAR_DEL_PETITION_SIGNATURE,                 ///< 删除请愿书签名
    CHAR_SEL_PETITION_BY_OWNER,                  ///< 根据所有者查询请愿书
    CHAR_SEL_PETITION_TYPE,                      ///< 查询请愿书类型
    CHAR_SEL_PETITION_SIGNATURES,                ///< 查询请愿书签名列表
    CHAR_SEL_PETITION_SIG_BY_ACCOUNT,            ///< 根据账号查询请愿书签名
    CHAR_SEL_PETITION_OWNER_BY_GUID,             ///< 根据GUID查询请愿书所有者
    CHAR_SEL_PETITION_SIG_BY_GUID,               ///< 根据GUID查询请愿书签名
    CHAR_SEL_PETITION_SIG_BY_GUID_TYPE,          ///< 根据GUID和类型查询请愿书签名

    // 战场数据
    CHAR_INS_PLAYER_BGDATA,                      ///< 插入玩家战场数据
    CHAR_DEL_PLAYER_BGDATA,                      ///< 删除玩家战场数据

    // 炉石绑定位置
    CHAR_INS_PLAYER_HOMEBIND,                    ///< 插入玩家炉石绑定位置
    CHAR_UPD_PLAYER_HOMEBIND,                    ///< 更新玩家炉石绑定位置
    CHAR_DEL_PLAYER_HOMEBIND,                    ///< 删除玩家炉石绑定位置

    // 尸体管理
    CHAR_SEL_CORPSES,                            ///< 查询尸体列表
    CHAR_INS_CORPSE,                             ///< 插入尸体记录
    CHAR_DEL_CORPSE,                             ///< 删除尸体记录
    CHAR_DEL_CORPSES_FROM_MAP,                   ///< 删除地图上的所有尸体
    CHAR_SEL_CORPSE_LOCATION,                    ///< 查询尸体位置

    // 复活点管理
    CHAR_SEL_RESPAWNS,                           ///< 查询复活点列表
    CHAR_REP_RESPAWN,                            ///< 替换复活点记录
    CHAR_DEL_RESPAWN,                            ///< 删除复活点记录
    CHAR_DEL_ALL_RESPAWNS,                       ///< 删除所有复活点记录

    // GM工单系统
    CHAR_SEL_GM_TICKETS,                         ///< 查询GM工单列表
    CHAR_REP_GM_TICKET,                          ///< 替换GM工单
    CHAR_DEL_GM_TICKET,                          ///< 删除GM工单
    CHAR_DEL_ALL_GM_TICKETS,                     ///< 删除所有GM工单
    CHAR_DEL_PLAYER_GM_TICKETS,                  ///< 删除玩家的GM工单
    CHAR_UPD_PLAYER_GM_TICKETS_ON_CHAR_DELETION,  ///< 角色删除时更新GM工单

    // GM调查和延迟报告
    CHAR_INS_GM_SURVEY,                          ///< 插入GM调查
    CHAR_INS_GM_SUBSURVEY,                       ///< 插入GM子调查
    CHAR_INS_LAG_REPORT,                         ///< 插入延迟报告

    // 角色创建和更新
    CHAR_INS_CHARACTER,                          ///< 插入角色
    CHAR_UPD_CHARACTER,                          ///< 更新角色

    // 登录标志管理
    CHAR_UPD_ADD_AT_LOGIN_FLAG,                  ///< 添加登录标志
    CHAR_UPD_REM_AT_LOGIN_FLAG,                  ///< 移除登录标志
    CHAR_UPD_ALL_AT_LOGIN_FLAGS,                 ///< 更新所有登录标志

    // 其他
    CHAR_INS_BUG_REPORT,                         ///< 插入Bug报告
    CHAR_UPD_PETITION_NAME,                      ///< 更新请愿书名称
    CHAR_INS_PETITION_SIGNATURE,                 ///< 插入请愿书签名
    CHAR_UPD_ACCOUNT_ONLINE,                     ///< 更新账号在线状态

    // 小队系统
    CHAR_INS_GROUP,                              ///< 插入小队
    CHAR_INS_GROUP_MEMBER,                       ///< 插入小队成员
    CHAR_DEL_GROUP_MEMBER,                       ///< 删除小队成员
    CHAR_DEL_GROUP_INSTANCE_PERM_BINDING,        ///< 删除小队副本永久绑定
    CHAR_UPD_GROUP_LEADER,                       ///< 更新小队队长
    CHAR_UPD_GROUP_TYPE,                         ///< 更新小队类型
    CHAR_UPD_GROUP_MEMBER_SUBGROUP,              ///< 更新小队成员子组
    CHAR_UPD_GROUP_MEMBER_FLAG,                  ///< 更新小队成员标志
    CHAR_UPD_GROUP_DIFFICULTY,                   ///< 更新小队难度
    CHAR_UPD_GROUP_RAID_DIFFICULTY,              ///< 更新小队团队难度

    // 无效数据清理
    CHAR_DEL_INVALID_SPELL_SPELLS,               ///< 删除无效的技能法术
    CHAR_DEL_INVALID_SPELL_TALENTS,              ///< 删除无效的天赋法术
    CHAR_UPD_DELETE_INFO,                        ///< 更新删除信息
    CHAR_UPD_RESTORE_DELETE_INFO,                ///< 恢复删除信息
    CHAR_UPD_ZONE,                               ///< 更新区域
    CHAR_UPD_LEVEL,                              ///< 更新等级
    CHAR_DEL_INVALID_ACHIEV_PROGRESS_CRITERIA,   ///< 删除无效的成就进度条件
    CHAR_DEL_INVALID_ACHIEVMENT,                 ///< 删除无效的成就

    // 插件管理
    CHAR_INS_ADDON,                              ///< 插入插件信息

    // 宠物技能清理
    CHAR_DEL_INVALID_PET_SPELL,                  ///< 删除无效的宠物技能

    // 小队副本实例
    CHAR_DEL_GROUP_INSTANCE_BY_INSTANCE,         ///< 根据实例删除小队副本记录
    CHAR_DEL_GROUP_INSTANCE_BY_GUID,             ///< 根据GUID删除小队副本记录
    CHAR_REP_GROUP_INSTANCE,                     ///< 替换小队副本记录

    // 副本重置时间
    CHAR_UPD_INSTANCE_RESETTIME,                 ///< 更新副本重置时间
    CHAR_INS_GLOBAL_INSTANCE_RESETTIME,          ///< 插入全局副本重置时间
    CHAR_DEL_GLOBAL_INSTANCE_RESETTIME,          ///< 删除全局副本重置时间
    CHAR_UPD_GLOBAL_INSTANCE_RESETTIME,          ///< 更新全局副本重置时间

    // 角色在线状态
    CHAR_UPD_CHAR_ONLINE,                        ///< 更新角色在线状态
    CHAR_UPD_CHAR_NAME_AT_LOGIN,                 ///< 登录时更新角色名称

    // 世界状态
    CHAR_UPD_WORLDSTATE,                         ///< 更新世界状态
    CHAR_INS_WORLDSTATE,                         ///< 插入世界状态

    // 角色副本实例
    CHAR_DEL_CHAR_INSTANCE_BY_INSTANCE_GUID,     ///< 根据实例GUID删除角色副本记录
    CHAR_UPD_CHAR_INSTANCE,                      ///< 更新角色副本记录
    CHAR_INS_CHAR_INSTANCE,                      ///< 插入角色副本记录

    // 外观修改
    CHAR_UPD_GENDER_AND_APPEARANCE,              ///< 更新性别和外观

    // 技能管理
    CHAR_DEL_CHARACTER_SKILL,                    ///< 删除角色技能

    // 社交系统
    CHAR_UPD_CHARACTER_SOCIAL_FLAGS,             ///< 更新角色社交标志
    CHAR_INS_CHARACTER_SOCIAL,                   ///< 插入角色社交关系
    CHAR_DEL_CHARACTER_SOCIAL,                   ///< 删除角色社交关系
    CHAR_UPD_CHARACTER_SOCIAL_NOTE,              ///< 更新角色社交备注

    // 角色位置更新
    CHAR_UPD_CHARACTER_POSITION,                 ///< 更新角色位置
    CHAR_UPD_CHARACTER_POSITION_BY_MAPID,        ///< 根据地图ID更新角色位置

    // 随机组队工具数据
    CHAR_INS_LFG_DATA,                           ///< 插入LFG数据
    CHAR_DEL_LFG_DATA,                           ///< 删除LFG数据

    // 角色查询
    CHAR_SEL_CHARACTER_AURA_FROZEN,              ///< 查询角色冰冻光环
    CHAR_SEL_CHARACTER_ONLINE,                   ///< 查询在线角色

    // 角色删除信息
    CHAR_SEL_CHAR_DEL_INFO_BY_GUID,              ///< 根据GUID查询角色删除信息
    CHAR_SEL_CHAR_DEL_INFO_BY_NAME,              ///< 根据名称查询角色删除信息
    CHAR_SEL_CHAR_DEL_INFO,                      ///< 查询角色删除信息

    // 角色信息查询
    CHAR_SEL_CHARS_BY_ACCOUNT_ID,                ///< 根据账号ID查询角色列表
    CHAR_SEL_CHAR_PINFO,                         ///< 查询角色详细信息
    CHAR_SEL_PINFO_XP,                           ///< 查询经验值信息
    CHAR_SEL_PINFO_MAILS,                        ///< 查询邮件信息
    CHAR_SEL_PINFO_BANS,                         ///< 查询封禁信息
    CHAR_SEL_CHAR_HOMEBIND,                      ///< 查询角色炉石绑定位置
    CHAR_SEL_CHAR_GUID_NAME_BY_ACC,              ///< 根据账号查询角色GUID和名称
    CHAR_SEL_CHARACTER_AT_LOGIN,                 ///< 查询登录时的角色数据
    CHAR_SEL_CHAR_CLASS_LVL_AT_LOGIN,            ///< 查询登录时的职业和等级
    CHAR_SEL_CHAR_CUSTOMIZE_INFO,                ///< 查询角色自定义信息
    CHAR_SEL_CHAR_RACE_OR_FACTION_CHANGE_INFOS,  ///< 查询种族或阵营变更信息
    CHAR_SEL_INSTANCE,                           ///< 查询实例信息
    CHAR_SEL_PERM_BIND_BY_INSTANCE,              ///< 查询实例的永久绑定
    CHAR_SEL_CHAR_COD_ITEM_MAIL,                 ///< 查询货到付款邮件物品
    CHAR_SEL_CHAR_SOCIAL,                        ///< 查询角色社交关系
    CHAR_SEL_CHAR_OLD_CHARS,                     ///< 查询旧角色列表
    CHAR_SEL_MAIL,                               ///< 查询邮件

    // 冻结光环
    CHAR_DEL_CHAR_AURA_FROZEN,                   ///< 删除角色冰冻光环

    // 物品统计查询
    CHAR_SEL_CHAR_INVENTORY_COUNT_ITEM,          ///< 统计角色背包中的物品数量
    CHAR_SEL_MAIL_COUNT_ITEM,                    ///< 统计邮件中的物品数量
    CHAR_SEL_AUCTIONHOUSE_COUNT_ITEM,            ///< 统计拍卖行中的物品数量
    CHAR_SEL_GUILD_BANK_COUNT_ITEM,              ///< 统计公会银行中的物品数量
    CHAR_SEL_CHAR_INVENTORY_ITEM_BY_ENTRY,       ///< 根据物品ID查询角色背包物品
    CHAR_SEL_MAIL_ITEMS_BY_ENTRY,                ///< 根据物品ID查询邮件物品
    CHAR_SEL_AUCTIONHOUSE_ITEM_BY_ENTRY,         ///< 根据物品ID查询拍卖行物品
    CHAR_SEL_GUILD_BANK_ITEM_BY_ENTRY,           ///< 根据物品ID查询公会银行物品

    // 成就系统
    CHAR_DEL_CHAR_ACHIEVEMENT,                   ///< 删除角色成就
    CHAR_DEL_CHAR_ACHIEVEMENT_PROGRESS,          ///< 删除角色成就进度
    CHAR_INS_CHAR_ACHIEVEMENT,                   ///< 插入角色成就
    CHAR_DEL_CHAR_ACHIEVEMENT_PROGRESS_BY_CRITERIA,  ///< 根据条件删除角色成就进度
    CHAR_INS_CHAR_ACHIEVEMENT_PROGRESS,          ///< 插入角色成就进度

    // 声望系统
    CHAR_DEL_CHAR_REPUTATION_BY_FACTION,         ///< 根据阵营删除角色声望
    CHAR_INS_CHAR_REPUTATION_BY_FACTION,         ///< 根据阵营插入角色声望

    // 竞技场点数
    CHAR_UPD_ADD_CHAR_ARENA_POINTS,              ///< 增加角色竞技场点数

    // 物品退款
    CHAR_DEL_ITEM_REFUND_INSTANCE,               ///< 删除物品退款实例
    CHAR_INS_ITEM_REFUND_INSTANCE,               ///< 插入物品退款实例

    // 小队删除
    CHAR_DEL_GROUP,                              ///< 删除小队
    CHAR_DEL_GROUP_MEMBER_ALL,                   ///< 删除所有小队成员

    // 角色礼物
    CHAR_INS_CHAR_GIFT,                          ///< 插入角色礼物

    // 副本实例清理
    CHAR_DEL_INSTANCE_BY_INSTANCE,               ///< 根据实例ID删除实例
    CHAR_DEL_CHAR_INSTANCE_BY_INSTANCE,          ///< 根据实例ID删除角色副本记录
    CHAR_DEL_EXPIRED_CHAR_INSTANCE_BY_MAP_DIFF,  ///< 删除过期的角色副本记录（按地图和难度）
    CHAR_DEL_GROUP_INSTANCE_BY_MAP_DIFF,         ///< 删除过期的组队副本记录（按地图和难度）
    CHAR_DEL_EXPIRED_INSTANCE_BY_MAP_DIFF,       ///< 删除过期的实例（按地图和难度）
    CHAR_UPD_EXPIRE_CHAR_INSTANCE_BY_MAP_DIFF,   ///< 更新过期的角色副本记录（按地图和难度）

    // 邮件物品清理
    CHAR_DEL_MAIL_ITEM_BY_ID,                    ///< 根据ID删除邮件物品

    // 请愿书创建和删除
    CHAR_INS_PETITION,                           ///< 插入请愿书
    CHAR_DEL_PETITION_BY_GUID,                   ///< 根据GUID删除请愿书
    CHAR_DEL_PETITION_SIGNATURE_BY_GUID,         ///< 根据GUID删除请愿书签名

    // 格变名称
    CHAR_DEL_CHAR_DECLINED_NAME,                 ///< 删除角色格变名称
    CHAR_INS_CHAR_DECLINED_NAME,                 ///< 插入角色格变名称

    // 种族变更
    CHAR_UPD_CHAR_RACE,                          ///< 更新角色种族

    // 语言技能
    CHAR_DEL_CHAR_SKILL_LANGUAGES,               ///< 删除角色语言技能
    CHAR_INS_CHAR_SKILL_LANGUAGE,                ///< 插入角色语言技能

    // 飞行路线
    CHAR_UPD_CHAR_TAXI_PATH,                     ///< 更新角色飞行路径
    CHAR_UPD_CHAR_TAXIMASK,                      ///< 更新角色飞行点掩码

    // 任务状态删除
    CHAR_DEL_CHAR_QUESTSTATUS,                   ///< 删除角色任务状态

    // 社交关系删除
    CHAR_DEL_CHAR_SOCIAL_BY_GUID,                ///< 根据GUID删除角色社交关系
    CHAR_DEL_CHAR_SOCIAL_BY_FRIEND,              ///< 根据好友删除角色社交关系

    // 成就删除
    CHAR_DEL_CHAR_ACHIEVEMENT_BY_ACHIEVEMENT,    ///< 根据成就ID删除角色成就
    CHAR_UPD_CHAR_ACHIEVEMENT,                   ///< 更新角色成就

    // 阵营变更相关
    CHAR_UPD_CHAR_INVENTORY_FACTION_CHANGE,      ///< 阵营变更时更新角色物品栏
    CHAR_DEL_CHAR_SPELL_BY_SPELL,                ///< 根据法术ID删除角色法术
    CHAR_UPD_CHAR_SPELL_FACTION_CHANGE,          ///< 阵营变更时更新角色法术
    CHAR_SEL_CHAR_REP_BY_FACTION,                ///< 根据阵营查询角色声望
    CHAR_DEL_CHAR_REP_BY_FACTION,                ///< 根据阵营删除角色声望
    CHAR_UPD_CHAR_REP_FACTION_CHANGE,            ///< 阵营变更时更新角色声望
    CHAR_UPD_CHAR_TITLES_FACTION_CHANGE,         ///< 阵营变更时更新角色称号
    CHAR_RES_CHAR_TITLES_FACTION_CHANGE,         ///< 阵营变更时重置角色称号

    // 法术冷却时间
    CHAR_DEL_CHAR_SPELL_COOLDOWNS,               ///< 删除角色法术冷却时间
    CHAR_INS_CHAR_SPELL_COOLDOWN,                ///< 插入角色法术冷却时间

    // 角色删除相关（批量删除）
    CHAR_DEL_CHARACTER,                          ///< 删除角色
    CHAR_DEL_CHAR_ACTION,                        ///< 删除角色动作条
    CHAR_DEL_CHAR_AURA,                          ///< 删除角色光环
    CHAR_DEL_CHAR_GIFT,                          ///< 删除角色礼物
    CHAR_DEL_CHAR_INSTANCE,                      ///< 删除角色副本记录
    CHAR_DEL_CHAR_INVENTORY,                     ///< 删除角色物品栏
    CHAR_DEL_CHAR_QUESTSTATUS_REWARDED,          ///< 删除角色已奖励任务状态
    CHAR_DEL_CHAR_REPUTATION,                    ///< 删除角色声望
    CHAR_DEL_CHAR_SPELL,                         ///< 删除角色法术
    CHAR_DEL_MAIL,                               ///< 删除邮件
    CHAR_DEL_MAIL_ITEMS,                         ///< 删除邮件物品
    CHAR_DEL_CHAR_ACHIEVEMENTS,                  ///< 删除角色成就
    CHAR_DEL_CHAR_EQUIPMENTSETS,                 ///< 删除角色装备套装
    CHAR_DEL_GUILD_EVENTLOG_BY_PLAYER,           ///< 根据玩家删除公会事件日志
    CHAR_DEL_GUILD_BANK_EVENTLOG_BY_PLAYER,      ///< 根据玩家删除公会银行事件日志
    CHAR_DEL_CHAR_GLYPHS,                        ///< 删除角色雕文
    CHAR_DEL_CHAR_TALENT,                        ///< 删除角色天赋
    CHAR_DEL_CHAR_SKILLS,                        ///< 删除角色技能

    // 角色货币更新
    CHAR_UPD_CHAR_HONOR_POINTS,                  ///< 更新角色荣誉点数
    CHAR_UPD_CHAR_ARENA_POINTS,                  ///< 更新角色竞技场点数
    CHAR_UPD_CHAR_MONEY,                         ///< 更新角色金币

    // 角色动作条
    CHAR_INS_CHAR_ACTION,                        ///< 插入角色动作条
    CHAR_UPD_CHAR_ACTION,                        ///< 更新角色动作条
    CHAR_DEL_CHAR_ACTION_BY_BUTTON_SPEC,         ///< 根据按钮和专精删除角色动作条

    // 角色物品栏操作
    CHAR_DEL_CHAR_INVENTORY_BY_ITEM,             ///< 根据物品删除角色物品栏记录
    CHAR_DEL_CHAR_INVENTORY_BY_BAG_SLOT,         ///< 根据背包和槽位删除角色物品栏记录

    // 邮件更新
    CHAR_UPD_MAIL,                               ///< 更新邮件

    // 任务状态
    CHAR_REP_CHAR_QUESTSTATUS,                   ///< 替换角色任务状态
    CHAR_DEL_CHAR_QUESTSTATUS_BY_QUEST,          ///< 根据任务ID删除角色任务状态
    CHAR_INS_CHAR_QUESTSTATUS_REWARDED,          ///< 插入角色已奖励任务状态
    CHAR_DEL_CHAR_QUESTSTATUS_REWARDED_BY_QUEST,  ///< 根据任务ID删除角色已奖励任务状态
    CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_FACTION_CHANGE,  ///< 阵营变更时更新角色已奖励任务状态
    CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_ACTIVE,   ///< 更新角色已奖励任务状态为激活
    CHAR_UPD_CHAR_QUESTSTATUS_REWARDED_ACTIVE_BY_QUEST,  ///< 根据任务更新角色已奖励任务状态为激活

    // 角色技能熟练度
    CHAR_DEL_CHAR_SKILL_BY_SKILL,                ///< 根据技能ID删除角色技能
    CHAR_INS_CHAR_SKILLS,                        ///< 插入角色技能
    CHAR_UPD_CHAR_SKILLS,                        ///< 更新角色技能

    // 角色法术
    CHAR_INS_CHAR_SPELL,                         ///< 插入角色法术

    // 角色属性
    CHAR_DEL_CHAR_STATS,                         ///< 删除角色属性
    CHAR_INS_CHAR_STATS,                         ///< 插入角色属性

    // 请愿书删除（按所有者）
    CHAR_DEL_PETITION_BY_OWNER,                  ///< 根据所有者删除请愿书
    CHAR_DEL_PETITION_SIGNATURE_BY_OWNER,        ///< 根据所有者删除请愿书签名
    CHAR_DEL_PETITION_BY_OWNER_AND_TYPE,         ///< 根据所有者和类型删除请愿书
    CHAR_DEL_PETITION_SIGNATURE_BY_OWNER_AND_TYPE,  ///< 根据所有者和类型删除请愿书签名

    // 角色雕文
    CHAR_INS_CHAR_GLYPHS,                        ///< 插入角色雕文

    // 角色天赋
    CHAR_DEL_CHAR_TALENT_BY_SPELL_SPEC,          ///< 根据法术和专精删除角色天赋
    CHAR_INS_CHAR_TALENT,                        ///< 插入角色天赋

    // 角色动作条（专精相关）
    CHAR_DEL_CHAR_ACTION_EXCEPT_SPEC,            ///< 删除除指定专精外的角色动作条

    // 钓鱼步骤
    CHAR_INS_CHAR_FISHINGSTEPS,                  ///< 插入角色钓鱼步骤
    CHAR_DEL_CHAR_FISHINGSTEPS,                  ///< 删除角色钓鱼步骤

    // 日历事件
    CHAR_REP_CALENDAR_EVENT,                     ///< 替换日历事件
    CHAR_DEL_CALENDAR_EVENT,                     ///< 删除日历事件
    CHAR_REP_CALENDAR_INVITE,                    ///< 替换日历邀请
    CHAR_DEL_CALENDAR_INVITE,                    ///< 删除日历邀请

    // 宠物系统
    CHAR_SEL_PET_AURA,                           ///< 查询宠物光环
    CHAR_SEL_PET_SPELL,                          ///< 查询宠物法术
    CHAR_SEL_PET_SPELL_COOLDOWN,                 ///< 查询宠物法术冷却
    CHAR_SEL_PET_DECLINED_NAME,                  ///< 查询宠物格变名称
    CHAR_DEL_PET_AURAS,                          ///< 删除宠物光环
    CHAR_DEL_PET_SPELL_COOLDOWNS,                ///< 删除宠物法术冷却
    CHAR_INS_PET_SPELL_COOLDOWN,                 ///< 插入宠物法术冷却
    CHAR_DEL_PET_SPELL_BY_SPELL,                 ///< 根据法术ID删除宠物法术
    CHAR_INS_PET_SPELL,                          ///< 插入宠物法术
    CHAR_INS_PET_AURA,                           ///< 插入宠物光环

    // 宠物管理
    CHAR_DEL_PET_SPELLS,                         ///< 删除宠物所有法术
    CHAR_DEL_CHAR_PET_BY_OWNER,                  ///< 根据所有者删除角色宠物
    CHAR_DEL_CHAR_PET_DECLINEDNAME_BY_OWNER,     ///< 根据所有者删除角色宠物格变名称
    CHAR_SEL_CHAR_PET_IDS,                       ///< 查询角色宠物ID列表
    CHAR_SEL_CHAR_PETS,                          ///< 查询角色宠物列表
    CHAR_DEL_CHAR_PET_DECLINEDNAME,              ///< 删除角色宠物格变名称
    CHAR_INS_CHAR_PET_DECLINEDNAME,              ///< 插入角色宠物格变名称
    CHAR_UPD_CHAR_PET_NAME,                      ///< 更新角色宠物名称
    CHAR_UPD_CHAR_PET_SLOT_BY_ID,                ///< 根据ID更新角色宠物槽位
    CHAR_DEL_CHAR_PET_BY_ID,                     ///< 根据ID删除角色宠物
    CHAR_DEL_CHAR_PET_BY_SLOT,                   ///< 根据槽位删除角色宠物
    CHAR_INS_PET,                                ///< 插入宠物

    // 物品容器
    CHAR_SEL_ITEMCONTAINER_ITEMS,                ///< 查询物品容器物品
    CHAR_DEL_ITEMCONTAINER_ITEMS,                ///< 删除物品容器物品
    CHAR_DEL_ITEMCONTAINER_ITEM,                 ///< 删除物品容器单个物品
    CHAR_INS_ITEMCONTAINER_ITEMS,                ///< 插入物品容器物品
    CHAR_SEL_ITEMCONTAINER_MONEY,                ///< 查询物品容器金币
    CHAR_DEL_ITEMCONTAINER_MONEY,                ///< 删除物品容器金币
    CHAR_INS_ITEMCONTAINER_MONEY,                ///< 插入物品容器金币

    // PVP统计
    CHAR_SEL_PVPSTATS_MAXID,                     ///< 查询PVP统计最大ID
    CHAR_INS_PVPSTATS_BATTLEGROUND,              ///< 插入PVP战场统计
    CHAR_INS_PVPSTATS_PLAYER,                    ///< 插入PVP玩家统计
    CHAR_SEL_PVPSTATS_FACTIONS_OVERALL,          ///< 查询阵营总体PVP统计

    // 任务追踪
    CHAR_INS_QUEST_TRACK,                        ///< 插入任务追踪记录
    CHAR_UPD_QUEST_TRACK_GM_COMPLETE,            ///< 更新任务追踪GM完成
    CHAR_UPD_QUEST_TRACK_COMPLETE_TIME,          ///< 更新任务追踪完成时间
    CHAR_UPD_QUEST_TRACK_ABANDON_TIME,           ///< 更新任务追踪放弃时间

    // 逃兵追踪
    CHAR_INS_DESERTER_TRACK,                     ///< 插入逃兵追踪记录

    MAX_CHARACTERDATABASE_STATEMENTS             ///< 预处理语句总数
};

/**
 * @brief 角色数据库连接类
 *
 * 继承自 MySQLConnection，专门用于处理角色数据库连接。
 * 角色数据库存储所有与玩家角色相关的持久化数据，包括：
 * - 角色基础属性和信息
 * - 物品和装备数据
 * - 技能、天赋、雕文
 * - 任务进度
 * - 声望和成就
 * - 公会和竞技场队伍
 * - 邮件和拍卖行
 * - 好友列表和社交关系
 * - 宠物和坐骑
 * - 等等
 */
class TC_DATABASE_API CharacterDatabaseConnection : public MySQLConnection
{
public:
    /**
     * @brief 预处理语句类型别名
     *
     * 方便在代码中使用 CharacterDatabaseStatements 枚举
     */
    typedef CharacterDatabaseStatements Statements;

    /**
     * @brief 构造同步数据库连接
     *
     * 用于同步执行数据库操作的场景
     *
     * @param connInfo MySQL连接信息配置
     */
    CharacterDatabaseConnection(MySQLConnectionInfo& connInfo);

    /**
     * @brief 构造异步数据库连接
     *
     * 用于异步执行数据库操作的场景，通过队列处理
     *
     * @param q 生产者-消费者队列，用于异步操作调度
     * @param connInfo MySQL连接信息配置
     */
    CharacterDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* q, MySQLConnectionInfo& connInfo);

    /**
     * @brief 析构函数
     *
     * 清理数据库连接资源
     */
    ~CharacterDatabaseConnection();

    /**
     * @brief 准备数据库特定的预处理语句
     *
     * 初始化所有角色数据库的预处理 SQL 语句。
     * 这些预处理语句用于提高数据库操作效率，
     * 避免重复解析 SQL 语句，并防止 SQL 注入攻击。
     *
     * 该方法在连接创建时自动调用。
     */
    void DoPrepareStatements() override;
};

#endif
