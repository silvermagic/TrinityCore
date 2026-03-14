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
 * @file RBAC.h
 * @brief 基于角色的访问控制（Role Based Access Control）模块定义
 *
 * 本模块实现了完整的RBAC权限管理系统，包括：
 * - 权限定义和权限关联
 * - 权限的授予、拒绝和撤销
 * - 权限的继承和计算
 *
 * RBAC核心概念：
 * - 权限（Permission）：定义执行特定操作的授权
 * - 角色（Role）：一组权限的集合（权限ID 196-199）
 * - 组（Group）：一组角色的集合
 * - 一个账户可以拥有多个组、角色和权限
 *
 * 权限操作规则：
 * - 账户组只能被授予或撤销
 * - 账户角色和权限可以被授予、拒绝或撤销
 * - 授予（Grant）：分配对象（角色/权限）并允许它
 * - 拒绝（Deny）：分配对象（角色/权限）并拒绝它
 * - 撤销（Revoke）：移除对象（角色/权限），无论它是被授予还是被拒绝
 *
 * 权限计算公式：
 * 全局权限 = 组授予 + 角色授予 + 用户授予 - 角色拒绝 - 用户拒绝
 *
 * 数据库表：
 * - rbac_permissions：权限定义表
 * - rbac_linked_permissions：权限关联表（定义权限继承关系）
 * - rbac_default_permissions：默认权限表（按安全等级分配）
 * - rbac_account_permissions：账户权限表
 *
 * 调用时机：
 * - 服务器启动时加载权限定义
 * - 玩家登录时加载账户权限
 * - 权限变更时重新计算
 *
 * 性能注意事项：
 * - 权限定义在启动时全部加载到内存
 * - 权限检查为内存操作，速度快
 * - 权限变更时需要重新计算全局权限
 */

#ifndef _RBAC_H
#define _RBAC_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <string>
#include <set>
#include <map>

namespace rbac
{

/**
 * @enum RBACPermissions
 * @brief RBAC权限ID枚举
 *
 * 定义了游戏中所有的权限ID，包括：
 * - 核心权限（1-53）：即时登出、跳过队列、加入战场等
 * - 角色权限（196-199）：管理员、游戏管理员、版主、玩家
 * - 命令权限（202+）：各种GM命令的权限控制
 *
 * 权限ID范围：
 * - 1-149：核心权限
 * - 196-199：角色权限（从大到小排列）
 * - 202+：命令权限
 * - 1000+：自定义权限
 */
enum RBACPermissions
{
    // ==================== 核心权限 ====================
    RBAC_PERM_INSTANT_LOGOUT                                 = 1,  ///< 即时登出权限
    RBAC_PERM_SKIP_QUEUE                                     = 2,  ///< 跳过登录队列权限
    RBAC_PERM_JOIN_NORMAL_BG                                 = 3,  ///< 加入普通战场权限
    RBAC_PERM_JOIN_RANDOM_BG                                 = 4,  ///< 加入随机战场权限
    RBAC_PERM_JOIN_ARENAS                                    = 5,  ///< 加入竞技场权限
    RBAC_PERM_JOIN_DUNGEON_FINDER                            = 6,  ///< 加入随机副本权限
    RBAC_PERM_IGNORE_IDLE_CONNECTION                         = 7,  ///< 忽略空闲连接检测权限
    RBAC_PERM_CANNOT_EARN_ACHIEVEMENTS                       = 8,  ///< 无法获得成就（GM标志）
    RBAC_PERM_CANNOT_EARN_REALM_FIRST_ACHIEVEMENTS           = 9,  ///< 无法获得服务器首杀成就
    RBAC_PERM_USE_CHARACTER_TEMPLATES                        = 10, ///< 使用角色模板（非3.3.5a）
    RBAC_PERM_LOG_GM_TRADE                                   = 11, ///< 记录GM交易日志
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_DEMON_HUNTER     = 12, ///< 跳过恶魔猎人创建检查（非3.3.5a）
    RBAC_PERM_SKIP_CHECK_INSTANCE_REQUIRED_BOSSES            = 13, ///< 跳过副本必需BOSS检查
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_TEAMMASK         = 14, ///< 跳过角色创建阵营检查
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_CLASSMASK        = 15, ///< 跳过角色创建职业检查
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RACEMASK         = 16, ///< 跳过角色创建种族检查
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_RESERVEDNAME     = 17, ///< 跳过保留名称检查
    RBAC_PERM_SKIP_CHECK_CHARACTER_CREATION_DEATH_KNIGHT     = 18, ///< 跳过死亡骑士创建检查
    RBAC_PERM_SKIP_CHECK_CHAT_CHANNEL_REQ                    = 19, ///< 跳过聊天频道要求检查
    RBAC_PERM_SKIP_CHECK_DISABLE_MAP                         = 20, ///< 跳过地图禁用检查
    RBAC_PERM_SKIP_CHECK_MORE_TALENTS_THAN_ALLOWED           = 21, ///< 跳过天赋点数超限检查
    RBAC_PERM_SKIP_CHECK_CHAT_SPAM                           = 22, ///< 跳过聊天刷屏检查
    RBAC_PERM_SKIP_CHECK_OVERSPEED_PING                      = 23, ///< 跳过超速Ping检查
    RBAC_PERM_TWO_SIDE_CHARACTER_CREATION                    = 24, ///< 允许跨阵营创建角色
    RBAC_PERM_TWO_SIDE_INTERACTION_CHAT                      = 25, ///< 允许跨阵营聊天
    RBAC_PERM_TWO_SIDE_INTERACTION_CHANNEL                   = 26, ///< 允许跨阵营频道交互
    RBAC_PERM_TWO_SIDE_INTERACTION_MAIL                      = 27, ///< 允许跨阵营邮件
    RBAC_PERM_TWO_SIDE_WHO_LIST                              = 28, ///< 查看敌对阵营玩家列表
    RBAC_PERM_TWO_SIDE_ADD_FRIEND                            = 29, ///< 允许跨阵营添加好友
    RBAC_PERM_COMMANDS_SAVE_WITHOUT_DELAY                    = 30, ///< 命令保存无延迟
    RBAC_PERM_COMMANDS_USE_UNSTUCK_WITH_ARGS                 = 31, ///< 使用带参数的unstuck命令
    RBAC_PERM_COMMANDS_BE_ASSIGNED_TICKET                    = 32, ///< 可被分配工单
    RBAC_PERM_COMMANDS_NOTIFY_COMMAND_NOT_FOUND_ERROR        = 33, ///< 命令未找到时通知
    RBAC_PERM_COMMANDS_APPEAR_IN_GM_LIST                     = 34, ///< 出现在GM列表中
    RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS                         = 35, ///< 查看所有安全等级
    RBAC_PERM_CAN_FILTER_WHISPERS                            = 36, ///< 可过滤密语
    RBAC_PERM_CHAT_USE_STAFF_BADGE                           = 37, ///< 聊天使用员工徽章
    RBAC_PERM_RESURRECT_WITH_FULL_HPS                        = 38, ///< 满血复活
    RBAC_PERM_RESTORE_SAVED_GM_STATE                         = 39, ///< 恢复保存的GM状态
    RBAC_PERM_ALLOW_GM_FRIEND                                = 40, ///< 允许GM好友
    RBAC_PERM_USE_START_GM_LEVEL                             = 41, ///< 使用起始GM等级
    RBAC_PERM_OPCODE_WORLD_TELEPORT                          = 42, ///< 世界传送操作码权限
    RBAC_PERM_OPCODE_WHOIS                                   = 43, ///< Whois操作码权限
    RBAC_PERM_RECEIVE_GLOBAL_GM_TEXTMESSAGE                  = 44, ///< 接收全局GM文本消息
    RBAC_PERM_SILENTLY_JOIN_CHANNEL                          = 45, ///< 静默加入频道
    RBAC_PERM_CHANGE_CHANNEL_NOT_MODERATOR                   = 46, ///< 非管理员也能更改频道
    RBAC_PERM_CHECK_FOR_LOWER_SECURITY                       = 47, ///< 检查更低安全等级
    RBAC_PERM_COMMANDS_PINFO_CHECK_PERSONAL_DATA             = 48, ///< Pinfo命令检查个人数据
    RBAC_PERM_EMAIL_CONFIRM_FOR_PASS_CHANGE                  = 49, ///< 修改密码需要邮箱确认
    RBAC_PERM_MAY_CHECK_OWN_EMAIL                            = 50, ///< 可检查自己的邮箱
    RBAC_PERM_ALLOW_TWO_SIDE_TRADE                           = 51, ///< 允许跨阵营交易
    RBAC_PERM_NO_BATTLEGROUND_DESERTER_DEBUFF                = 52, ///< 无战场逃兵Debuff
    RBAC_PERM_CAN_AFK_ON_BATTLEGROUND                        = 53, ///< 战场中可AFK

    // 核心权限保留空间（至149）
    // 角色（带委托权限的权限）使用199及以下的数字

    // ==================== 角色权限 ====================
    RBAC_ROLE_ADMINISTRATOR                                  = 196, ///< 管理员角色（最高权限）
    RBAC_ROLE_GAMEMASTER                                     = 197, ///< 游戏管理员角色
    RBAC_ROLE_MODERATOR                                      = 198, ///< 版主角色
    RBAC_ROLE_PLAYER                                         = 199, ///< 普通玩家角色

    // 200和201已停用，不要重新使用
    // ==================== 命令权限 ====================

    // 200和201已停用，不要重新使用
    // ==================== 命令权限 ====================

    // RBAC命令权限
    RBAC_PERM_COMMAND_RBAC_ACC_PERM_LIST                     = 202, ///< 列出账户权限命令
    RBAC_PERM_COMMAND_RBAC_ACC_PERM_GRANT                    = 203, ///< 授予账户权限命令
    RBAC_PERM_COMMAND_RBAC_ACC_PERM_DENY                     = 204, ///< 拒绝账户权限命令
    RBAC_PERM_COMMAND_RBAC_ACC_PERM_REVOKE                   = 205, ///< 撤销账户权限命令
    RBAC_PERM_COMMAND_RBAC_LIST                              = 206, ///< 列出RBAC权限命令

    // Battle.net账户命令权限（非3.3.5a版本）
    RBAC_PERM_COMMAND_BNET_ACCOUNT                           = 207, ///< Battle.net账户命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_CREATE                    = 208, ///< 创建Battle.net账户命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_LOCK_COUNTRY              = 209, ///< Battle.net账户国家锁定命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_LOCK_IP                   = 210, ///< Battle.net账户IP锁定命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_PASSWORD                  = 211, ///< Battle.net账户密码命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_SET                       = 212, ///< 设置Battle.net账户命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_SET_PASSWORD              = 213, ///< 设置Battle.net账户密码命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_LINK                      = 214, ///< 关联Battle.net账户命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_UNLINK                    = 215, ///< 解除关联Battle.net账户命令（非3.3.5a）
    RBAC_PERM_COMMAND_BNET_ACCOUNT_CREATE_GAME               = 216, ///< 创建游戏账户命令（非3.3.5a）

    // 账户管理命令权限
    RBAC_PERM_COMMAND_ACCOUNT                                = 217, ///< 账户管理命令
    RBAC_PERM_COMMAND_ACCOUNT_ADDON                          = 218,
    RBAC_PERM_COMMAND_ACCOUNT_CREATE                         = 219,
    RBAC_PERM_COMMAND_ACCOUNT_DELETE                         = 220,
    RBAC_PERM_COMMAND_ACCOUNT_LOCK                           = 221,
    RBAC_PERM_COMMAND_ACCOUNT_LOCK_COUNTRY                   = 222,
    RBAC_PERM_COMMAND_ACCOUNT_LOCK_IP                        = 223,
    RBAC_PERM_COMMAND_ACCOUNT_ONLINE_LIST                    = 224,
    RBAC_PERM_COMMAND_ACCOUNT_PASSWORD                       = 225,
    RBAC_PERM_COMMAND_ACCOUNT_SET                            = 226,
    RBAC_PERM_COMMAND_ACCOUNT_SET_ADDON                      = 227,
    RBAC_PERM_COMMAND_ACCOUNT_SET_SECLEVEL                   = 228,
    RBAC_PERM_COMMAND_ACCOUNT_SET_PASSWORD                   = 229,
    // 230 previously used, do not reuse
    RBAC_PERM_COMMAND_ACHIEVEMENT_ADD                        = 231,
    // 232 previously used, do not reuse
    RBAC_PERM_COMMAND_ARENA_CAPTAIN                          = 233,
    RBAC_PERM_COMMAND_ARENA_CREATE                           = 234,
    RBAC_PERM_COMMAND_ARENA_DISBAND                          = 235,
    RBAC_PERM_COMMAND_ARENA_INFO                             = 236,
    RBAC_PERM_COMMAND_ARENA_LOOKUP                           = 237,
    RBAC_PERM_COMMAND_ARENA_RENAME                           = 238,
    // 239 previously used, do not reuse
    RBAC_PERM_COMMAND_BAN_ACCOUNT                            = 240,
    RBAC_PERM_COMMAND_BAN_CHARACTER                          = 241,
    RBAC_PERM_COMMAND_BAN_IP                                 = 242,
    RBAC_PERM_COMMAND_BAN_PLAYERACCOUNT                      = 243,
    // 244 previously used, do not reuse
    RBAC_PERM_COMMAND_BANINFO_ACCOUNT                        = 245,
    RBAC_PERM_COMMAND_BANINFO_CHARACTER                      = 246,
    RBAC_PERM_COMMAND_BANINFO_IP                             = 247,
    // 248 previously used, do not reuse
    RBAC_PERM_COMMAND_BANLIST_ACCOUNT                        = 249,
    RBAC_PERM_COMMAND_BANLIST_CHARACTER                      = 250,
    RBAC_PERM_COMMAND_BANLIST_IP                             = 251,
    // 252 previously used, do not reuse
    RBAC_PERM_COMMAND_UNBAN_ACCOUNT                          = 253,
    RBAC_PERM_COMMAND_UNBAN_CHARACTER                        = 254,
    RBAC_PERM_COMMAND_UNBAN_IP                               = 255,
    RBAC_PERM_COMMAND_UNBAN_PLAYERACCOUNT                    = 256,
    // 257 previously used, do not reuse
    RBAC_PERM_COMMAND_BF_START                               = 258,
    RBAC_PERM_COMMAND_BF_STOP                                = 259,
    RBAC_PERM_COMMAND_BF_SWITCH                              = 260,
    RBAC_PERM_COMMAND_BF_TIMER                               = 261,
    RBAC_PERM_COMMAND_BF_ENABLE                              = 262,
    RBAC_PERM_COMMAND_ACCOUNT_EMAIL                          = 263,
    // 264 previously used, do not reuse
    RBAC_PERM_COMMAND_ACCOUNT_SET_SEC_EMAIL                  = 265,
    RBAC_PERM_COMMAND_ACCOUNT_SET_SEC_REGMAIL                = 266,
    RBAC_PERM_COMMAND_CAST                                   = 267,
    RBAC_PERM_COMMAND_CAST_BACK                              = 268,
    RBAC_PERM_COMMAND_CAST_DIST                              = 269,
    RBAC_PERM_COMMAND_CAST_SELF                              = 270,
    RBAC_PERM_COMMAND_CAST_TARGET                            = 271,
    RBAC_PERM_COMMAND_CAST_DEST                              = 272,
    // 273 previously used, do not reuse
    RBAC_PERM_COMMAND_CHARACTER_CUSTOMIZE                    = 274,
    RBAC_PERM_COMMAND_CHARACTER_CHANGEFACTION                = 275,
    RBAC_PERM_COMMAND_CHARACTER_CHANGERACE                   = 276,
    // 277 previously used, do not reuse
    RBAC_PERM_COMMAND_CHARACTER_DELETED_DELETE               = 278,
    RBAC_PERM_COMMAND_CHARACTER_DELETED_LIST                 = 279,
    RBAC_PERM_COMMAND_CHARACTER_DELETED_RESTORE              = 280,
    RBAC_PERM_COMMAND_CHARACTER_DELETED_OLD                  = 281,
    RBAC_PERM_COMMAND_CHARACTER_ERASE                        = 282,
    RBAC_PERM_COMMAND_CHARACTER_LEVEL                        = 283,
    RBAC_PERM_COMMAND_CHARACTER_RENAME                       = 284,
    RBAC_PERM_COMMAND_CHARACTER_REPUTATION                   = 285,
    RBAC_PERM_COMMAND_CHARACTER_TITLES                       = 286,
    RBAC_PERM_COMMAND_LEVELUP                                = 287,
    // 288 previously used, do not reuse
    RBAC_PERM_COMMAND_PDUMP_LOAD                             = 289,
    RBAC_PERM_COMMAND_PDUMP_WRITE                            = 290,
    // 291 previously used, do not reuse
    RBAC_PERM_COMMAND_CHEAT_CASTTIME                         = 292,
    RBAC_PERM_COMMAND_CHEAT_COOLDOWN                         = 293,
    RBAC_PERM_COMMAND_CHEAT_EXPLORE                          = 294,
    RBAC_PERM_COMMAND_CHEAT_GOD                              = 295,
    RBAC_PERM_COMMAND_CHEAT_POWER                            = 296,
    RBAC_PERM_COMMAND_CHEAT_STATUS                           = 297,
    RBAC_PERM_COMMAND_CHEAT_TAXI                             = 298,
    RBAC_PERM_COMMAND_CHEAT_WATERWALK                        = 299,
    RBAC_PERM_COMMAND_DEBUG                                  = 300,
    // 301-342 previously used, do not reuse
    RBAC_PERM_COMMAND_DESERTER_BG_ADD                        = 343,
    RBAC_PERM_COMMAND_DESERTER_BG_REMOVE                     = 344,
    // 345 previously used, do not reuse
    RBAC_PERM_COMMAND_DESERTER_INSTANCE_ADD                  = 346,
    RBAC_PERM_COMMAND_DESERTER_INSTANCE_REMOVE               = 347,
    // 348-349 previously used, do not reuse
    RBAC_PERM_COMMAND_DISABLE_ADD_ACHIEVEMENT_CRITERIA       = 350,
    RBAC_PERM_COMMAND_DISABLE_ADD_BATTLEGROUND               = 351,
    RBAC_PERM_COMMAND_DISABLE_ADD_MAP                        = 352,
    RBAC_PERM_COMMAND_DISABLE_ADD_MMAP                       = 353,
    RBAC_PERM_COMMAND_DISABLE_ADD_OUTDOORPVP                 = 354,
    RBAC_PERM_COMMAND_DISABLE_ADD_QUEST                      = 355,
    RBAC_PERM_COMMAND_DISABLE_ADD_SPELL                      = 356,
    RBAC_PERM_COMMAND_DISABLE_ADD_VMAP                       = 357,
    // 358 previously used, do not reuse
    RBAC_PERM_COMMAND_DISABLE_REMOVE_ACHIEVEMENT_CRITERIA    = 359,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_BATTLEGROUND            = 360,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_MAP                     = 361,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_MMAP                    = 362,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_OUTDOORPVP              = 363,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_QUEST                   = 364,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_SPELL                   = 365,
    RBAC_PERM_COMMAND_DISABLE_REMOVE_VMAP                    = 366,
    RBAC_PERM_COMMAND_EVENT_INFO                             = 367,
    RBAC_PERM_COMMAND_EVENT_ACTIVELIST                       = 368,
    RBAC_PERM_COMMAND_EVENT_START                            = 369,
    RBAC_PERM_COMMAND_EVENT_STOP                             = 370,
    RBAC_PERM_COMMAND_GM                                     = 371,
    RBAC_PERM_COMMAND_GM_CHAT                                = 372,
    RBAC_PERM_COMMAND_GM_FLY                                 = 373,
    RBAC_PERM_COMMAND_GM_INGAME                              = 374,
    RBAC_PERM_COMMAND_GM_LIST                                = 375,
    RBAC_PERM_COMMAND_GM_VISIBLE                             = 376,
    RBAC_PERM_COMMAND_GO                                     = 377,
    RBAC_PERM_COMMAND_ACCOUNT_2FA                            = 378,
    RBAC_PERM_COMMAND_ACCOUNT_2FA_SETUP                      = 379,
    RBAC_PERM_COMMAND_ACCOUNT_2FA_REMOVE                     = 380,
    RBAC_PERM_COMMAND_ACCOUNT_SET_2FA                        = 381,
    // unused 382-386
    // 387 previously used, do not reuse
    RBAC_PERM_COMMAND_GOBJECT_ACTIVATE                       = 388,
    RBAC_PERM_COMMAND_GOBJECT_ADD                            = 389,
    RBAC_PERM_COMMAND_GOBJECT_ADD_TEMP                       = 390,
    RBAC_PERM_COMMAND_GOBJECT_DELETE                         = 391,
    RBAC_PERM_COMMAND_GOBJECT_INFO                           = 392,
    RBAC_PERM_COMMAND_GOBJECT_MOVE                           = 393,
    RBAC_PERM_COMMAND_GOBJECT_NEAR                           = 394,
    // 395 previously used, do not reuse
    RBAC_PERM_COMMAND_GOBJECT_SET_PHASE                      = 396,
    RBAC_PERM_COMMAND_GOBJECT_SET_STATE                      = 397,
    RBAC_PERM_COMMAND_GOBJECT_TARGET                         = 398,
    RBAC_PERM_COMMAND_GOBJECT_TURN                           = 399,
    // 400 previously used, do not reuse
    RBAC_PERM_COMMAND_GUILD                                  = 401,
    RBAC_PERM_COMMAND_GUILD_CREATE                           = 402,
    RBAC_PERM_COMMAND_GUILD_DELETE                           = 403,
    RBAC_PERM_COMMAND_GUILD_INVITE                           = 404,
    RBAC_PERM_COMMAND_GUILD_UNINVITE                         = 405,
    RBAC_PERM_COMMAND_GUILD_RANK                             = 406,
    RBAC_PERM_COMMAND_GUILD_RENAME                           = 407,
    // 408 previously used, do not reuse
    RBAC_PERM_COMMAND_HONOR_ADD                              = 409,
    RBAC_PERM_COMMAND_HONOR_ADD_KILL                         = 410,
    RBAC_PERM_COMMAND_HONOR_UPDATE                           = 411,
    // 412 previously used, do not reuse
    RBAC_PERM_COMMAND_INSTANCE_LISTBINDS                     = 413,
    RBAC_PERM_COMMAND_INSTANCE_UNBIND                        = 414,
    RBAC_PERM_COMMAND_INSTANCE_STATS                         = 415,
    RBAC_PERM_COMMAND_INSTANCE_SAVEDATA                      = 416,
    RBAC_PERM_COMMAND_LEARN                                  = 417,
    // 418 previously used, do not reuse
    RBAC_PERM_COMMAND_LEARN_ALL_MY                           = 419,
    RBAC_PERM_COMMAND_LEARN_ALL_MY_CLASS                     = 420,
    RBAC_PERM_COMMAND_LEARN_MY_PETTALENTS                    = 421,
    RBAC_PERM_COMMAND_LEARN_ALL_MY_SPELLS                    = 422,
    RBAC_PERM_COMMAND_LEARN_ALL_TALENTS                      = 423,
    RBAC_PERM_COMMAND_LEARN_ALL_GM                           = 424,
    RBAC_PERM_COMMAND_LEARN_ALL_CRAFTS                       = 425,
    RBAC_PERM_COMMAND_LEARN_ALL_DEFAULT                      = 426,
    RBAC_PERM_COMMAND_LEARN_ALL_LANG                         = 427,
    RBAC_PERM_COMMAND_LEARN_ALL_RECIPES                      = 428,
    RBAC_PERM_COMMAND_UNLEARN                                = 429,
    // 430 previously used, do not reuse
    RBAC_PERM_COMMAND_LFG_PLAYER                             = 431,
    RBAC_PERM_COMMAND_LFG_GROUP                              = 432,
    RBAC_PERM_COMMAND_LFG_QUEUE                              = 433,
    RBAC_PERM_COMMAND_LFG_CLEAN                              = 434,
    RBAC_PERM_COMMAND_LFG_OPTIONS                            = 435,
    // 436 previously used, do not reuse
    RBAC_PERM_COMMAND_LIST_CREATURE                          = 437,
    RBAC_PERM_COMMAND_LIST_ITEM                              = 438,
    RBAC_PERM_COMMAND_LIST_OBJECT                            = 439,
    RBAC_PERM_COMMAND_LIST_AURAS                             = 440,
    RBAC_PERM_COMMAND_LIST_MAIL                              = 441,
    RBAC_PERM_COMMAND_LOOKUP                                 = 442,
    RBAC_PERM_COMMAND_LOOKUP_AREA                            = 443,
    RBAC_PERM_COMMAND_LOOKUP_CREATURE                        = 444,
    RBAC_PERM_COMMAND_LOOKUP_EVENT                           = 445,
    RBAC_PERM_COMMAND_LOOKUP_FACTION                         = 446,
    RBAC_PERM_COMMAND_LOOKUP_ITEM                            = 447,
    RBAC_PERM_COMMAND_LOOKUP_ITEMSET                         = 448,
    RBAC_PERM_COMMAND_LOOKUP_OBJECT                          = 449,
    RBAC_PERM_COMMAND_LOOKUP_QUEST                           = 450,
    RBAC_PERM_COMMAND_LOOKUP_PLAYER                          = 451,
    RBAC_PERM_COMMAND_LOOKUP_PLAYER_IP                       = 452,
    RBAC_PERM_COMMAND_LOOKUP_PLAYER_ACCOUNT                  = 453,
    RBAC_PERM_COMMAND_LOOKUP_PLAYER_EMAIL                    = 454,
    RBAC_PERM_COMMAND_LOOKUP_SKILL                           = 455,
    RBAC_PERM_COMMAND_LOOKUP_SPELL                           = 456,
    RBAC_PERM_COMMAND_LOOKUP_SPELL_ID                        = 457,
    RBAC_PERM_COMMAND_LOOKUP_TAXINODE                        = 458,
    RBAC_PERM_COMMAND_LOOKUP_TELE                            = 459,
    RBAC_PERM_COMMAND_LOOKUP_TITLE                           = 460,
    RBAC_PERM_COMMAND_LOOKUP_MAP                             = 461,
    RBAC_PERM_COMMAND_ANNOUNCE                               = 462,
    RBAC_PERM_COMMAND_CHANNEL                                = 463,
    RBAC_PERM_COMMAND_CHANNEL_SET                            = 464,
    RBAC_PERM_COMMAND_CHANNEL_SET_OWNERSHIP                  = 465,
    RBAC_PERM_COMMAND_GMANNOUNCE                             = 466,
    RBAC_PERM_COMMAND_GMNAMEANNOUNCE                         = 467,
    RBAC_PERM_COMMAND_GMNOTIFY                               = 468,
    RBAC_PERM_COMMAND_NAMEANNOUNCE                           = 469,
    RBAC_PERM_COMMAND_NOTIFY                                 = 470,
    RBAC_PERM_COMMAND_WHISPERS                               = 471,
    RBAC_PERM_COMMAND_GROUP                                  = 472,
    RBAC_PERM_COMMAND_GROUP_LEADER                           = 473,
    RBAC_PERM_COMMAND_GROUP_DISBAND                          = 474,
    RBAC_PERM_COMMAND_GROUP_REMOVE                           = 475,
    RBAC_PERM_COMMAND_GROUP_JOIN                             = 476,
    RBAC_PERM_COMMAND_GROUP_LIST                             = 477,
    RBAC_PERM_COMMAND_GROUP_SUMMON                           = 478,
    RBAC_PERM_COMMAND_PET                                    = 479,
    RBAC_PERM_COMMAND_PET_CREATE                             = 480,
    RBAC_PERM_COMMAND_PET_LEARN                              = 481,
    RBAC_PERM_COMMAND_PET_UNLEARN                            = 482,
    RBAC_PERM_COMMAND_SEND                                   = 483,
    RBAC_PERM_COMMAND_SEND_ITEMS                             = 484,
    RBAC_PERM_COMMAND_SEND_MAIL                              = 485,
    RBAC_PERM_COMMAND_SEND_MESSAGE                           = 486,
    RBAC_PERM_COMMAND_SEND_MONEY                             = 487,
    RBAC_PERM_COMMAND_ADDITEM                                = 488,
    RBAC_PERM_COMMAND_ADDITEMSET                             = 489,
    RBAC_PERM_COMMAND_APPEAR                                 = 490,
    RBAC_PERM_COMMAND_AURA                                   = 491,
    RBAC_PERM_COMMAND_BANK                                   = 492,
    RBAC_PERM_COMMAND_BINDSIGHT                              = 493,
    RBAC_PERM_COMMAND_COMBATSTOP                             = 494,
    RBAC_PERM_COMMAND_COMETOME                               = 495,
    RBAC_PERM_COMMAND_COMMANDS                               = 496,
    RBAC_PERM_COMMAND_COOLDOWN                               = 497,
    RBAC_PERM_COMMAND_DAMAGE                                 = 498,
    RBAC_PERM_COMMAND_DEV                                    = 499,
    RBAC_PERM_COMMAND_DIE                                    = 500,
    RBAC_PERM_COMMAND_DISMOUNT                               = 501,
    RBAC_PERM_COMMAND_DISTANCE                               = 502,
    RBAC_PERM_COMMAND_FLUSHARENAPOINTS                       = 503,
    RBAC_PERM_COMMAND_FREEZE                                 = 504,
    RBAC_PERM_COMMAND_GPS                                    = 505,
    RBAC_PERM_COMMAND_GUID                                   = 506,
    RBAC_PERM_COMMAND_HELP                                   = 507,
    RBAC_PERM_COMMAND_HIDEAREA                               = 508,
    RBAC_PERM_COMMAND_ITEMMOVE                               = 509,
    RBAC_PERM_COMMAND_KICK                                   = 510,
    RBAC_PERM_COMMAND_LINKGRAVE                              = 511,
    RBAC_PERM_COMMAND_LISTFREEZE                             = 512,
    RBAC_PERM_COMMAND_MAXSKILL                               = 513,
    RBAC_PERM_COMMAND_MOVEGENS                               = 514,
    RBAC_PERM_COMMAND_MUTE                                   = 515,
    RBAC_PERM_COMMAND_NEARGRAVE                              = 516,
    RBAC_PERM_COMMAND_PINFO                                  = 517,
    RBAC_PERM_COMMAND_PLAYALL                                = 518,
    RBAC_PERM_COMMAND_POSSESS                                = 519,
    RBAC_PERM_COMMAND_RECALL                                 = 520,
    RBAC_PERM_COMMAND_REPAIRITEMS                            = 521,
    RBAC_PERM_COMMAND_RESPAWN                                = 522,
    RBAC_PERM_COMMAND_REVIVE                                 = 523,
    RBAC_PERM_COMMAND_SAVEALL                                = 524,
    RBAC_PERM_COMMAND_SAVE                                   = 525,
    RBAC_PERM_COMMAND_SETSKILL                               = 526,
    RBAC_PERM_COMMAND_SHOWAREA                               = 527,
    RBAC_PERM_COMMAND_SUMMON                                 = 528,
    RBAC_PERM_COMMAND_UNAURA                                 = 529,
    RBAC_PERM_COMMAND_UNBINDSIGHT                            = 530,
    RBAC_PERM_COMMAND_UNFREEZE                               = 531,
    RBAC_PERM_COMMAND_UNMUTE                                 = 532,
    RBAC_PERM_COMMAND_UNPOSSESS                              = 533,
    RBAC_PERM_COMMAND_UNSTUCK                                = 534,
    RBAC_PERM_COMMAND_WCHANGE                                = 535,
    RBAC_PERM_COMMAND_MMAP                                   = 536,
    RBAC_PERM_COMMAND_MMAP_LOADEDTILES                       = 537,
    RBAC_PERM_COMMAND_MMAP_LOC                               = 538,
    RBAC_PERM_COMMAND_MMAP_PATH                              = 539,
    RBAC_PERM_COMMAND_MMAP_STATS                             = 540,
    RBAC_PERM_COMMAND_MMAP_TESTAREA                          = 541,
    RBAC_PERM_COMMAND_MORPH                                  = 542,
    RBAC_PERM_COMMAND_DEMORPH                                = 543,
    RBAC_PERM_COMMAND_MODIFY                                 = 544,
    RBAC_PERM_COMMAND_MODIFY_ARENAPOINTS                     = 545,
    RBAC_PERM_COMMAND_MODIFY_BIT                             = 546,
    RBAC_PERM_COMMAND_MODIFY_DRUNK                           = 547,
    RBAC_PERM_COMMAND_MODIFY_ENERGY                          = 548,
    RBAC_PERM_COMMAND_MODIFY_FACTION                         = 549,
    RBAC_PERM_COMMAND_MODIFY_GENDER                          = 550,
    RBAC_PERM_COMMAND_MODIFY_HONOR                           = 551,
    RBAC_PERM_COMMAND_MODIFY_HP                              = 552,
    RBAC_PERM_COMMAND_MODIFY_MANA                            = 553,
    RBAC_PERM_COMMAND_MODIFY_MONEY                           = 554,
    RBAC_PERM_COMMAND_MODIFY_MOUNT                           = 555,
    RBAC_PERM_COMMAND_MODIFY_PHASE                           = 556,
    RBAC_PERM_COMMAND_MODIFY_RAGE                            = 557,
    RBAC_PERM_COMMAND_MODIFY_REPUTATION                      = 558,
    RBAC_PERM_COMMAND_MODIFY_RUNICPOWER                      = 559,
    RBAC_PERM_COMMAND_MODIFY_SCALE                           = 560,
    RBAC_PERM_COMMAND_MODIFY_SPEED                           = 561,
    RBAC_PERM_COMMAND_MODIFY_SPEED_ALL                       = 562,
    RBAC_PERM_COMMAND_MODIFY_SPEED_BACKWALK                  = 563,
    RBAC_PERM_COMMAND_MODIFY_SPEED_FLY                       = 564,
    RBAC_PERM_COMMAND_MODIFY_SPEED_WALK                      = 565,
    RBAC_PERM_COMMAND_MODIFY_SPEED_SWIM                      = 566,
    RBAC_PERM_COMMAND_MODIFY_SPELL                           = 567,
    RBAC_PERM_COMMAND_MODIFY_STANDSTATE                      = 568,
    RBAC_PERM_COMMAND_MODIFY_TALENTPOINTS                    = 569,
    // 570 previously used, do not reuse
    RBAC_PERM_COMMAND_NPC_ADD                                = 571,
    RBAC_PERM_COMMAND_NPC_ADD_FORMATION                      = 572,
    RBAC_PERM_COMMAND_NPC_ADD_ITEM                           = 573,
    RBAC_PERM_COMMAND_NPC_ADD_MOVE                           = 574,
    RBAC_PERM_COMMAND_NPC_ADD_TEMP                           = 575,
    RBAC_PERM_COMMAND_NPC_DELETE                             = 576,
    RBAC_PERM_COMMAND_NPC_DELETE_ITEM                        = 577,
    RBAC_PERM_COMMAND_NPC_FOLLOW                             = 578,
    RBAC_PERM_COMMAND_NPC_FOLLOW_STOP                        = 579,
    RBAC_PERM_COMMAND_NPC_SET                                = 580,
    RBAC_PERM_COMMAND_NPC_SET_ALLOWMOVE                      = 581,
    RBAC_PERM_COMMAND_NPC_SET_ENTRY                          = 582,
    RBAC_PERM_COMMAND_NPC_SET_FACTIONID                      = 583,
    RBAC_PERM_COMMAND_NPC_SET_FLAG                           = 584,
    RBAC_PERM_COMMAND_NPC_SET_LEVEL                          = 585,
    RBAC_PERM_COMMAND_NPC_SET_LINK                           = 586,
    RBAC_PERM_COMMAND_NPC_SET_MODEL                          = 587,
    RBAC_PERM_COMMAND_NPC_SET_MOVETYPE                       = 588,
    RBAC_PERM_COMMAND_NPC_SET_PHASE                          = 589,
    RBAC_PERM_COMMAND_NPC_SET_SPAWNDIST                      = 590,
    RBAC_PERM_COMMAND_NPC_SET_SPAWNTIME                      = 591,
    RBAC_PERM_COMMAND_NPC_SET_DATA                           = 592,
    RBAC_PERM_COMMAND_NPC_INFO                               = 593,
    RBAC_PERM_COMMAND_NPC_NEAR                               = 594,
    RBAC_PERM_COMMAND_NPC_MOVE                               = 595,
    RBAC_PERM_COMMAND_NPC_PLAYEMOTE                          = 596,
    RBAC_PERM_COMMAND_NPC_SAY                                = 597,
    RBAC_PERM_COMMAND_NPC_TEXTEMOTE                          = 598,
    RBAC_PERM_COMMAND_NPC_WHISPER                            = 599,
    RBAC_PERM_COMMAND_NPC_YELL                               = 600,
    RBAC_PERM_COMMAND_NPC_TAME                               = 601,
    RBAC_PERM_COMMAND_QUEST                                  = 602,
    RBAC_PERM_COMMAND_QUEST_ADD                              = 603,
    RBAC_PERM_COMMAND_QUEST_COMPLETE                         = 604,
    RBAC_PERM_COMMAND_QUEST_REMOVE                           = 605,
    RBAC_PERM_COMMAND_QUEST_REWARD                           = 606,
    RBAC_PERM_COMMAND_RELOAD                                 = 607,
    RBAC_PERM_COMMAND_RELOAD_ACCESS_REQUIREMENT              = 608,
    RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_CRITERIA_DATA       = 609,
    RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_REWARD              = 610,
    RBAC_PERM_COMMAND_RELOAD_ALL                             = 611,
    RBAC_PERM_COMMAND_RELOAD_ALL_ACHIEVEMENT                 = 612,
    RBAC_PERM_COMMAND_RELOAD_ALL_AREA                        = 613,
    RBAC_PERM_COMMAND_RELOAD_BROADCAST_TEXT                  = 614,
    RBAC_PERM_COMMAND_RELOAD_ALL_GOSSIP                      = 615,
    RBAC_PERM_COMMAND_RELOAD_ALL_ITEM                        = 616,
    RBAC_PERM_COMMAND_RELOAD_ALL_LOCALES                     = 617,
    RBAC_PERM_COMMAND_RELOAD_ALL_LOOT                        = 618,
    RBAC_PERM_COMMAND_RELOAD_ALL_NPC                         = 619,
    RBAC_PERM_COMMAND_RELOAD_ALL_QUEST                       = 620,
    RBAC_PERM_COMMAND_RELOAD_ALL_SCRIPTS                     = 621,
    RBAC_PERM_COMMAND_RELOAD_ALL_SPELL                       = 622,
    RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_INVOLVEDRELATION    = 623,
    RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_TAVERN              = 624,
    RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_TELEPORT            = 625,
    RBAC_PERM_COMMAND_RELOAD_AUCTIONS                        = 626,
    RBAC_PERM_COMMAND_RELOAD_AUTOBROADCAST                   = 627,
    // 628 previously used, do not reuse
    RBAC_PERM_COMMAND_RELOAD_CONDITIONS                      = 629,
    RBAC_PERM_COMMAND_RELOAD_CONFIG                          = 630,
    RBAC_PERM_COMMAND_RELOAD_BATTLEGROUND_TEMPLATE           = 631,
    RBAC_PERM_COMMAND_MUTEHISTORY                            = 632,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_LINKED_RESPAWN         = 633,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_LOOT_TEMPLATE          = 634,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_ONKILL_REPUTATION      = 635,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_QUESTENDER             = 636,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_QUESTSTARTER           = 637,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_SUMMON_GROUPS          = 638,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_TEMPLATE               = 639,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_TEXT                   = 640,
    RBAC_PERM_COMMAND_RELOAD_DISABLES                        = 641,
    RBAC_PERM_COMMAND_RELOAD_DISENCHANT_LOOT_TEMPLATE        = 642,
    RBAC_PERM_COMMAND_RELOAD_EVENT_SCRIPTS                   = 643,
    RBAC_PERM_COMMAND_RELOAD_FISHING_LOOT_TEMPLATE           = 644,
    RBAC_PERM_COMMAND_RELOAD_GRAVEYARD_ZONE                  = 645,
    RBAC_PERM_COMMAND_RELOAD_GAME_TELE                       = 646,
    RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUESTENDER           = 647,
    RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUEST_LOOT_TEMPLATE  = 648,
    RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_QUESTSTARTER         = 649,
    RBAC_PERM_COMMAND_RELOAD_GM_TICKETS                      = 650,
    RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU                     = 651,
    RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU_OPTION              = 652,
    RBAC_PERM_COMMAND_RELOAD_ITEM_ENCHANTMENT_TEMPLATE       = 653,
    RBAC_PERM_COMMAND_RELOAD_ITEM_LOOT_TEMPLATE              = 654,
    RBAC_PERM_COMMAND_RELOAD_ITEM_SET_NAMES                  = 655,
    RBAC_PERM_COMMAND_RELOAD_LFG_DUNGEON_REWARDS             = 656,
    RBAC_PERM_COMMAND_RELOAD_ACHIEVEMENT_REWARD_LOCALE       = 657,
    RBAC_PERM_COMMAND_RELOAD_CRETURE_TEMPLATE_LOCALE         = 658,
    RBAC_PERM_COMMAND_RELOAD_CRETURE_TEXT_LOCALE             = 659,
    RBAC_PERM_COMMAND_RELOAD_GAMEOBJECT_TEMPLATE_LOCALE      = 660,
    RBAC_PERM_COMMAND_RELOAD_GOSSIP_MENU_OPTION_LOCALE       = 661,
    RBAC_PERM_COMMAND_RELOAD_ITEM_TEMPLATE_LOCALE            = 662,
    RBAC_PERM_COMMAND_RELOAD_ITEM_SET_NAME_LOCALE            = 663,
    RBAC_PERM_COMMAND_RELOAD_NPC_TEXT_LOCALE                 = 664,
    RBAC_PERM_COMMAND_RELOAD_PAGE_TEXT_LOCALE                = 665,
    RBAC_PERM_COMMAND_RELOAD_POINTS_OF_INTEREST_LOCALE       = 666,
    RBAC_PERM_COMMAND_RELOAD_QUEST_TEMPLATE_LOCALE           = 667,
    RBAC_PERM_COMMAND_RELOAD_MAIL_LEVEL_REWARD               = 668,
    RBAC_PERM_COMMAND_RELOAD_MAIL_LOOT_TEMPLATE              = 669,
    RBAC_PERM_COMMAND_RELOAD_MILLING_LOOT_TEMPLATE           = 670,
    RBAC_PERM_COMMAND_RELOAD_NPC_SPELLCLICK_SPELLS           = 671,
    RBAC_PERM_COMMAND_RELOAD_TRAINER                         = 672,
    RBAC_PERM_COMMAND_RELOAD_NPC_VENDOR                      = 673,
    RBAC_PERM_COMMAND_RELOAD_PAGE_TEXT                       = 674,
    RBAC_PERM_COMMAND_RELOAD_PICKPOCKETING_LOOT_TEMPLATE     = 675,
    RBAC_PERM_COMMAND_RELOAD_POINTS_OF_INTEREST              = 676,
    RBAC_PERM_COMMAND_RELOAD_PROSPECTING_LOOT_TEMPLATE       = 677,
    RBAC_PERM_COMMAND_RELOAD_QUEST_POI                       = 678,
    RBAC_PERM_COMMAND_RELOAD_QUEST_TEMPLATE                  = 679,
    RBAC_PERM_COMMAND_RELOAD_RBAC                            = 680,
    RBAC_PERM_COMMAND_RELOAD_REFERENCE_LOOT_TEMPLATE         = 681,
    RBAC_PERM_COMMAND_RELOAD_RESERVED_NAME                   = 682,
    RBAC_PERM_COMMAND_RELOAD_REPUTATION_REWARD_RATE          = 683,
    RBAC_PERM_COMMAND_RELOAD_SPILLOVER_TEMPLATE              = 684,
    RBAC_PERM_COMMAND_RELOAD_SKILL_DISCOVERY_TEMPLATE        = 685,
    RBAC_PERM_COMMAND_RELOAD_SKILL_EXTRA_ITEM_TEMPLATE       = 686,
    RBAC_PERM_COMMAND_RELOAD_SKILL_FISHING_BASE_LEVEL        = 687,
    RBAC_PERM_COMMAND_RELOAD_SKINNING_LOOT_TEMPLATE          = 688,
    RBAC_PERM_COMMAND_RELOAD_SMART_SCRIPTS                   = 689,
    RBAC_PERM_COMMAND_RELOAD_SPELL_REQUIRED                  = 690,
    RBAC_PERM_COMMAND_RELOAD_SPELL_AREA                      = 691,
    RBAC_PERM_COMMAND_RELOAD_SPELL_BONUS_DATA                = 692,
    RBAC_PERM_COMMAND_RELOAD_SPELL_GROUP                     = 693,
    RBAC_PERM_COMMAND_RELOAD_SPELL_LEARN_SPELL               = 694,
    RBAC_PERM_COMMAND_RELOAD_SPELL_LOOT_TEMPLATE             = 695,
    RBAC_PERM_COMMAND_RELOAD_SPELL_LINKED_SPELL              = 696,
    RBAC_PERM_COMMAND_RELOAD_SPELL_PET_AURAS                 = 697,
    RBAC_PERM_COMMAND_CHARACTER_CHANGEACCOUNT                = 698,
    RBAC_PERM_COMMAND_RELOAD_SPELL_PROC                      = 699,
    RBAC_PERM_COMMAND_RELOAD_SPELL_SCRIPTS                   = 700,
    RBAC_PERM_COMMAND_RELOAD_SPELL_TARGET_POSITION           = 701,
    RBAC_PERM_COMMAND_RELOAD_SPELL_THREATS                   = 702,
    RBAC_PERM_COMMAND_RELOAD_SPELL_GROUP_STACK_RULES         = 703,
    RBAC_PERM_COMMAND_RELOAD_TRINITY_STRING                  = 704,
    // 705 previously used, do not reuse
    RBAC_PERM_COMMAND_RELOAD_WAYPOINT_SCRIPTS                = 706,
    RBAC_PERM_COMMAND_RELOAD_WAYPOINT_DATA                   = 707,
    RBAC_PERM_COMMAND_RELOAD_VEHICLE_ACCESORY                = 708,
    RBAC_PERM_COMMAND_RELOAD_VEHICLE_TEMPLATE_ACCESSORY      = 709,
    RBAC_PERM_COMMAND_RESET                                  = 710,
    RBAC_PERM_COMMAND_RESET_ACHIEVEMENTS                     = 711,
    RBAC_PERM_COMMAND_RESET_HONOR                            = 712,
    RBAC_PERM_COMMAND_RESET_LEVEL                            = 713,
    RBAC_PERM_COMMAND_RESET_SPELLS                           = 714,
    RBAC_PERM_COMMAND_RESET_STATS                            = 715,
    RBAC_PERM_COMMAND_RESET_TALENTS                          = 716,
    RBAC_PERM_COMMAND_RESET_ALL                              = 717,
    RBAC_PERM_COMMAND_SERVER                                 = 718,
    RBAC_PERM_COMMAND_SERVER_CORPSES                         = 719,
    RBAC_PERM_COMMAND_SERVER_EXIT                            = 720,
    RBAC_PERM_COMMAND_SERVER_IDLERESTART                     = 721,
    RBAC_PERM_COMMAND_SERVER_IDLERESTART_CANCEL              = 722,
    RBAC_PERM_COMMAND_SERVER_IDLESHUTDOWN                    = 723,
    RBAC_PERM_COMMAND_SERVER_IDLESHUTDOWN_CANCEL             = 724,
    RBAC_PERM_COMMAND_SERVER_INFO                            = 725,
    RBAC_PERM_COMMAND_SERVER_PLIMIT                          = 726,
    RBAC_PERM_COMMAND_SERVER_RESTART                         = 727,
    RBAC_PERM_COMMAND_SERVER_RESTART_CANCEL                  = 728,
    RBAC_PERM_COMMAND_SERVER_SET                             = 729,
    RBAC_PERM_COMMAND_SERVER_SET_CLOSED                      = 730,
    RBAC_PERM_COMMAND_SERVER_SET_DIFFTIME                    = 731, // reserved
    RBAC_PERM_COMMAND_SERVER_SET_LOGLEVEL                    = 732,
    RBAC_PERM_COMMAND_SERVER_SET_MOTD                        = 733,
    RBAC_PERM_COMMAND_SERVER_SHUTDOWN                        = 734,
    RBAC_PERM_COMMAND_SERVER_SHUTDOWN_CANCEL                 = 735,
    RBAC_PERM_COMMAND_SERVER_MOTD                            = 736,
    RBAC_PERM_COMMAND_TELE                                   = 737,
    RBAC_PERM_COMMAND_TELE_ADD                               = 738,
    RBAC_PERM_COMMAND_TELE_DEL                               = 739,
    RBAC_PERM_COMMAND_TELE_NAME                              = 740,
    RBAC_PERM_COMMAND_TELE_GROUP                             = 741,
    RBAC_PERM_COMMAND_TICKET                                 = 742,
    RBAC_PERM_COMMAND_TICKET_ASSIGN                          = 743,
    RBAC_PERM_COMMAND_TICKET_CLOSE                           = 744,
    RBAC_PERM_COMMAND_TICKET_CLOSEDLIST                      = 745,
    RBAC_PERM_COMMAND_TICKET_COMMENT                         = 746,
    RBAC_PERM_COMMAND_TICKET_COMPLETE                        = 747,
    RBAC_PERM_COMMAND_TICKET_DELETE                          = 748,
    RBAC_PERM_COMMAND_TICKET_ESCALATE                        = 749,
    RBAC_PERM_COMMAND_TICKET_ESCALATEDLIST                   = 750,
    RBAC_PERM_COMMAND_TICKET_LIST                            = 751,
    RBAC_PERM_COMMAND_TICKET_ONLINELIST                      = 752,
    RBAC_PERM_COMMAND_TICKET_RESET                           = 753,
    RBAC_PERM_COMMAND_TICKET_RESPONSE                        = 754,
    RBAC_PERM_COMMAND_TICKET_RESPONSE_APPEND                 = 755,
    RBAC_PERM_COMMAND_TICKET_RESPONSE_APPENDLN               = 756,
    RBAC_PERM_COMMAND_TICKET_TOGGLESYSTEM                    = 757,
    RBAC_PERM_COMMAND_TICKET_UNASSIGN                        = 758,
    RBAC_PERM_COMMAND_TICKET_VIEWID                          = 759,
    RBAC_PERM_COMMAND_TICKET_VIEWNAME                        = 760,
    // 761 previously used, do not reuse
    RBAC_PERM_COMMAND_TITLES_ADD                             = 762,
    RBAC_PERM_COMMAND_TITLES_CURRENT                         = 763,
    RBAC_PERM_COMMAND_TITLES_REMOVE                          = 764,
    // 765 previously used, do not reuse
    RBAC_PERM_COMMAND_TITLES_SET_MASK                        = 766,
    RBAC_PERM_COMMAND_WP                                     = 767,
    RBAC_PERM_COMMAND_WP_ADD                                 = 768,
    RBAC_PERM_COMMAND_WP_EVENT                               = 769,
    RBAC_PERM_COMMAND_WP_LOAD                                = 770,
    RBAC_PERM_COMMAND_WP_MODIFY                              = 771,
    RBAC_PERM_COMMAND_WP_UNLOAD                              = 772,
    RBAC_PERM_COMMAND_WP_RELOAD                              = 773,
    RBAC_PERM_COMMAND_WP_SHOW                                = 774,
    RBAC_PERM_COMMAND_MODIFY_CURRENCY                        = 775, // not on 3.3.5a
    RBAC_PERM_COMMAND_DEBUG_PHASE                            = 776, // not on 3.3.5a
    RBAC_PERM_COMMAND_MAILBOX                                = 777,
    // 778 previously used, do not reuse
    RBAC_PERM_COMMAND_AHBOT_ITEMS                            = 779,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_GRAY                       = 780,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_WHITE                      = 781,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_GREEN                      = 782,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_BLUE                       = 783,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_PURPLE                     = 784,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_ORANGE                     = 785,
    RBAC_PERM_COMMAND_AHBOT_ITEMS_YELLOW                     = 786,
    RBAC_PERM_COMMAND_AHBOT_RATIO                            = 787,
    RBAC_PERM_COMMAND_AHBOT_RATIO_ALLIANCE                   = 788,
    RBAC_PERM_COMMAND_AHBOT_RATIO_HORDE                      = 789,
    RBAC_PERM_COMMAND_AHBOT_RATIO_NEUTRAL                    = 790,
    RBAC_PERM_COMMAND_AHBOT_REBUILD                          = 791,
    RBAC_PERM_COMMAND_AHBOT_RELOAD                           = 792,
    RBAC_PERM_COMMAND_AHBOT_STATUS                           = 793,
    RBAC_PERM_COMMAND_GUILD_INFO                             = 794,
    RBAC_PERM_COMMAND_INSTANCE_SET_BOSS_STATE                = 795,
    RBAC_PERM_COMMAND_INSTANCE_GET_BOSS_STATE                = 796,
    RBAC_PERM_COMMAND_PVPSTATS                               = 797,
    RBAC_PERM_COMMAND_MODIFY_XP                              = 798,
    RBAC_PERM_COMMAND_GO_BUG_TICKET                          = 799, // not on 3.3.5a
    RBAC_PERM_COMMAND_GO_COMPLAINT_TICKET                    = 800, // not on 3.3.5a
    RBAC_PERM_COMMAND_GO_SUGGESTION_TICKET                   = 801, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG                             = 802, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT                       = 803, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION                      = 804, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_ASSIGN                      = 805, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_CLOSE                       = 806, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_CLOSEDLIST                  = 807, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_COMMENT                     = 808, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_DELETE                      = 809, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_LIST                        = 810, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_UNASSIGN                    = 811, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_BUG_VIEW                        = 812, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_ASSIGN                = 813, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_CLOSE                 = 814, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_CLOSEDLIST            = 815, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_COMMENT               = 816, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_DELETE                = 817, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_LIST                  = 818, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_UNASSIGN              = 819, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_COMPLAINT_VIEW                  = 820, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_ASSIGN               = 821, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_CLOSE                = 822, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_CLOSEDLIST           = 823, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_COMMENT              = 824, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_DELETE               = 825, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_LIST                 = 826, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_UNASSIGN             = 827, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_SUGGESTION_VIEW                 = 828, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_RESET_ALL                       = 829, // not on 3.3.5a
    RBAC_PERM_COMMAND_BNET_ACCOUNT_LIST_GAME_ACCOUTNS        = 830, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_RESET_BUG                       = 831, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_RESET_COMPLAINT                 = 832, // not on 3.3.5a
    RBAC_PERM_COMMAND_TICKET_RESET_SUGGESTION                = 833, // not on 3.3.5a
    RBAC_PERM_COMMAND_GO_QUEST                               = 834, // not on 3.3.5a
    // 835-836 previously used, do not reuse
    RBAC_PERM_COMMAND_NPC_EVADE                              = 837,
    RBAC_PERM_COMMAND_PET_LEVEL                              = 838,
    RBAC_PERM_COMMAND_SERVER_SHUTDOWN_FORCE                  = 839,
    RBAC_PERM_COMMAND_SERVER_RESTART_FORCE                   = 840,
    RBAC_PERM_COMMAND_NEARGRAVEYARD                          = 841,
    RBAC_PERM_COMMAND_RELOAD_CHARACTER_TEMPLATE              = 842, // not on 3.3.5a
    RBAC_PERM_COMMAND_RELOAD_QUEST_GREETING                  = 843,
    RBAC_PERM_COMMAND_SCENE                                  = 844, // not on 3.3.5a
    RBAC_PERM_COMMAND_SCENE_DEBUG                            = 845, // not on 3.3.5a
    RBAC_PERM_COMMAND_SCENE_PLAY                             = 846, // not on 3.3.5a
    RBAC_PERM_COMMAND_SCENE_PLAY_PACKAGE                     = 847, // not on 3.3.5a
    RBAC_PERM_COMMAND_SCENE_CANCEL                           = 848, // not on 3.3.5a
    RBAC_PERM_COMMAND_LIST_SCENES                            = 849, // not on 3.3.5a
    RBAC_PERM_COMMAND_RELOAD_SCENE_TEMPLATE                  = 850, // not on 3.3.5a
    RBAC_PERM_COMMAND_RELOAD_AREATRIGGER_TEMPLATE            = 851, // not on 3.3.5a
    // 852 previously used, do not reuse
    RBAC_PERM_COMMAND_RELOAD_CONVERSATION_TEMPLATE           = 853, // not on 3.3.5a
    RBAC_PERM_COMMAND_DEBUG_CONVERSATION                     = 854, // not on 3.3.5a
    // 855 previously used, do not reuse
    RBAC_PERM_COMMAND_NPC_SPAWNGROUP                         = 856,
    RBAC_PERM_COMMAND_NPC_DESPAWNGROUP                       = 857,
    RBAC_PERM_COMMAND_GOBJECT_SPAWNGROUP                     = 858,
    RBAC_PERM_COMMAND_GOBJECT_DESPAWNGROUP                   = 859,
    RBAC_PERM_COMMAND_LIST_RESPAWNS                          = 860,
    RBAC_PERM_COMMAND_GROUP_SET                              = 861,
    RBAC_PERM_COMMAND_GROUP_ASSISTANT                        = 862,
    RBAC_PERM_COMMAND_GROUP_MAINTANK                         = 863,
    RBAC_PERM_COMMAND_GROUP_MAINASSIST                       = 864,
    RBAC_PERM_COMMAND_NPC_SHOWLOOT                           = 865,
    RBAC_PERM_COMMAND_LIST_SPAWNPOINTS                       = 866,
    RBAC_PERM_COMMAND_RELOAD_QUEST_GREETING_LOCALE           = 867,
    RBAC_PERM_COMMAND_MODIFY_POWER                           = 868, // reserved
    RBAC_PERM_COMMAND_DEBUG_SEND_PLAYER_CHOICE               = 869, // reserved
    // 870-871 previously used, do not reuse
    RBAC_PERM_COMMAND_SERVER_DEBUG                           = 872,
    RBAC_PERM_COMMAND_RELOAD_CREATURE_MOVEMENT_OVERRIDE      = 873,
    // 874 previously used, do not reuse
    RBAC_PERM_COMMAND_LOOKUP_MAP_ID                          = 875,
    RBAC_PERM_COMMAND_LOOKUP_ITEM_ID                         = 876,
    RBAC_PERM_COMMAND_LOOKUP_QUEST_ID                        = 877,
    // 878-879 previously used, do not reuse
    RBAC_PERM_COMMAND_PDUMP_COPY                             = 880,
    RBAC_PERM_COMMAND_RELOAD_VEHICLE_TEMPLATE                = 881,
    RBAC_PERM_COMMAND_BG_START                               = 884,
    RBAC_PERM_COMMAND_BG_STOP                                = 885,
    //
    // 如果添加新权限，请同时添加到MASTER分支！
    //
    // 自定义权限 1000+
    RBAC_PERM_MAX  ///< 最大权限ID（用于边界检查）
};

/**
 * @enum RBACCommandResult
 * @brief RBAC命令执行结果枚举
 *
 * 定义了权限操作（授予、拒绝、撤销）可能返回的结果状态
 */
enum RBACCommandResult
{
    RBAC_OK,                      ///< 操作成功
    RBAC_CANT_ADD_ALREADY_ADDED,  ///< 无法添加：已存在
    RBAC_CANT_REVOKE_NOT_IN_LIST, ///< 无法撤销：不在列表中
    RBAC_IN_GRANTED_LIST,         ///< 已在授予列表中
    RBAC_IN_DENIED_LIST,          ///< 已在拒绝列表中
    RBAC_ID_DOES_NOT_EXISTS       ///< 权限ID不存在
};

/// 权限容器类型：权限ID的集合
typedef std::set<uint32> RBACPermissionContainer;

/**
 * @class RBACPermission
 * @brief RBAC权限对象类
 *
 * 表示一个权限定义，包含：
 * - 权限ID
 * - 权限名称
 * - 关联权限列表（权限继承关系）
 *
 * 权限继承：
 * - 一个权限可以关联多个其他权限
 * - 当账户获得某权限时，自动获得其关联的所有权限
 * - 例如：管理员角色关联了所有GM命令权限
 */
class TC_GAME_API RBACPermission
{
    public:
        /**
         * @brief 构造函数
         * @param id 权限ID
         * @param name 权限名称
         */
        RBACPermission(uint32 id = 0, std::string const& name = ""):
            _id(id), _name(name), _perms() { }

        /**
         * @brief 获取权限名称
         * @return 权限名称的常引用
         */
        std::string const& GetName() const { return _name; }

        /**
         * @brief 获取权限ID
         * @return 权限ID
         */
        uint32 GetId() const { return _id; }

        /**
         * @brief 获取关联权限列表
         * @return 关联权限ID集合的常引用
         *
         * 返回所有与此权限关联的其他权限ID
         */
        RBACPermissionContainer const& GetLinkedPermissions() const { return _perms; }

        /**
         * @brief 添加关联权限
         * @param id 要关联的权限ID
         *
         * 建立权限继承关系，获得此权限时自动获得关联权限
         */
        void AddLinkedPermission(uint32 id) { _perms.insert(id); }

        /**
         * @brief 移除关联权限
         * @param id 要移除的权限ID
         *
         * 移除权限继承关系
         */
        void RemoveLinkedPermission(uint32 id) { _perms.erase(id); }

    private:
        uint32 _id;                         ///< 权限ID
        std::string _name;                  ///< 权限名称
        RBACPermissionContainer _perms;     ///< 关联权限集合
};

/**
 * @class RBACData
 * @brief 账户RBAC数据类
 *
 * 包含账户权限计算所需的所有信息：
 * - 授予的权限列表
 * - 拒绝的权限列表
 * - 计算后的全局权限列表
 * - 关联的账户信息和安全等级
 *
 * 权限计算公式：
 * 全局权限 = 授予权限 - 拒绝权限
 * - 授予权限：通过关联权限和直接分配获得
 * - 拒绝权限：通过关联权限和直接分配获得
 *
 * 使用方式：
 * 1. 创建RBACData对象，设置账户ID、名称、领域ID和安全等级
 * 2. 调用LoadFromDB()加载权限数据
 * 3. 使用HasPermission()检查权限
 *
 * 性能注意事项：
 * - LoadFromDB()会执行数据库查询
 * - 权限检查为内存操作，速度快
 * - 权限变更后需调用CalculateNewPermissions()重新计算
 */
class TC_GAME_API RBACData
{
    public:
        /**
         * @brief 构造函数
         * @param id 账户ID
         * @param name 账户名称
         * @param realmId 领域ID
         * @param secLevel 安全等级（默认255）
         */
        RBACData(uint32 id, std::string const& name, int32 realmId, uint8 secLevel = 255):
            _id(id), _name(name), _realmId(realmId), _secLevel(secLevel),
            _grantedPerms(), _deniedPerms(), _globalPerms() { }

        /**
         * @brief 获取账户名称
         * @return 账户名称的常引用
         */
        std::string const& GetName() const { return _name; }

        /**
         * @brief 获取账户ID
         * @return 账户ID
         */
        uint32 GetId() const { return _id; }

        /**
         * @brief 检查是否拥有指定权限
         * @param permission 权限ID
         * @return 是否拥有该权限
         *
         * 检查账户是否有执行某操作的权限
         *
         * 使用示例：
         * @code
         * bool Player::CanJoinArena(Battleground* bg)
         * {
         *     return bg->isArena() && HasPermission(RBAC_PERM_JOIN_ARENA);
         * }
         * @endcode
         */
        bool HasPermission(uint32 permission) const
        {
            return _globalPerms.find(permission) != _globalPerms.end();
        }

        // 命令系统可用的函数

        /**
         * @brief 获取所有权限（计算后）
         * @return 全局权限集合的常引用
         */
        RBACPermissionContainer const& GetPermissions() const { return _globalPerms; }

        /**
         * @brief 获取授予权限列表
         * @return 授予权限集合的常引用
         */
        RBACPermissionContainer const& GetGrantedPermissions() const { return _grantedPerms; }

        /**
         * @brief 获取拒绝权限列表
         * @return 拒绝权限集合的常引用
         */
        RBACPermissionContainer const& GetDeniedPermissions() const { return _deniedPerms; }

        /**
         * @brief 授予权限
         * @param permissionId 要授予的权限ID
         * @param realmId 领域ID（0表示从数据库加载时不保存）
         * @return 操作结果
         *
         * 向账户授予权限。如果领域ID为0或权限无法添加，不会保存到数据库
         *
         * 失败条件：
         * - 权限ID不存在
         * - 权限已被授予
         * - 权限已被拒绝
         *
         * 使用示例：
         * @code
         * // 假设已定义并初始化了 "RBACData* rbac"
         * uint32 permissionId = 2;
         * if (rbac->GrantPermission(permissionId) == RBAC_IN_DENIED_LIST)
         *     TC_LOG_DEBUG("entities.player", "Failed to grant permission {}, already denied", permissionId);
         * @endcode
         */
        RBACCommandResult GrantPermission(uint32 permissionId, int32 realmId = 0);

        /**
         * @brief 拒绝权限
         * @param permissionId 要拒绝的权限ID
         * @param realmId 领域ID（0表示从数据库加载时不保存）
         * @return 操作结果
         *
         * 拒绝账户的某权限。如果领域ID为0或权限无法添加，不会保存到数据库
         *
         * 失败条件：
         * - 权限ID不存在
         * - 权限已被授予
         * - 权限已被拒绝
         *
         * 使用示例：
         * @code
         * // 假设已定义并初始化了 "RBACData* rbac"
         * uint32 permissionId = 2;
         * if (rbac->DenyPermission(permissionId) == RBAC_ID_DOES_NOT_EXISTS)
         *     TC_LOG_DEBUG("entities.player", "Permission Id {} does not exists", permissionId);
         * @endcode
         */
        RBACCommandResult DenyPermission(uint32 permissionId, int32 realmId = 0);

        /**
         * @brief 撤销权限
         * @param permissionId 要撤销的权限ID
         * @param realmId 领域ID（0表示从数据库加载时不保存）
         * @return 操作结果
         *
         * 从账户移除权限。如果领域ID为0或权限无法移除，不会保存到数据库
         * 删除操作总是会影响指定领域和"所有领域(-1)"
         *
         * 失败条件：
         * - 权限不在授予或拒绝列表中
         *
         * 使用示例：
         * @code
         * // 假设已定义并初始化了 "RBACData* rbac"
         * uint32 permissionId = 2;
         * if (rbac->RevokePermission(permissionId) == RBAC_OK)
         *     TC_LOG_DEBUG("entities.player", "Permission {} successfully removed", permissionId);
         * @endcode
         */
        RBACCommandResult RevokePermission(uint32 permissionId, int32 realmId = 0);

        /**
         * @brief 从数据库加载权限（同步）
         *
         * 加载账户的所有权限数据，包括：
         * - 授予的权限
         * - 拒绝的权限
         * - 默认权限（基于安全等级）
         *
         * 性能注意事项：执行同步数据库查询
         */
        void LoadFromDB();

        /**
         * @brief 从数据库加载权限（异步）
         * @return 查询回调对象
         *
         * 异步加载权限数据，适合在主线程调用避免阻塞
         */
        QueryCallback LoadFromDBAsync();

        /**
         * @brief 处理异步加载结果
         * @param result 数据库查询结果
         *
         * 处理异步查询返回的权限数据
         */
        void LoadFromDBCallback(PreparedQueryResult result);

        /**
         * @brief 设置安全等级
         * @param id 新的安全等级
         *
         * 更新安全等级并重新加载权限
         */
        void SetSecurityLevel(uint8 id)
        {
            _secLevel = id;
            LoadFromDB();
        }

        /**
         * @brief 获取安全等级
         * @return 当前安全等级
         */
        uint8 GetSecurityLevel() const { return _secLevel; }

    private:
        /**
         * @brief 保存权限到数据库
         * @param permission 权限ID
         * @param granted true表示授予，false表示拒绝
         * @param realmId 领域ID
         */
        void SavePermission(uint32 permission, bool granted, int32 realmId);

        /**
         * @brief 清理权限数据
         *
         * 清空授予、拒绝和全局权限列表，用于重新加载
         */
        void ClearData();

        /**
         * @brief 计算新的全局权限
         *
         * 在权限变更后重新计算全局权限
         * 计算公式：授予权限 - 拒绝权限
         * - 授予权限：通过关联权限和直接分配获得
         * - 拒绝权限：通过关联权限和直接分配获得
         */
        void CalculateNewPermissions();

        /**
         * @brief 获取领域ID
         * @return 领域ID
         */
        int32 GetRealmId() const { return _realmId; }

        // ==================== 辅助私有函数 ====================
        // 定义这些函数以便在内部结构变化时保持代码一致性

        /**
         * @brief 检查权限是否已授予
         * @param permissionId 权限ID
         * @return 是否已授予
         */
        bool HasGrantedPermission(uint32 permissionId) const
        {
            return _grantedPerms.find(permissionId) != _grantedPerms.end();
        }

        /**
         * @brief 检查权限是否已拒绝
         * @param permissionId 权限ID
         * @return 是否已拒绝
         */
        bool HasDeniedPermission(uint32 permissionId) const
        {
            return _deniedPerms.find(permissionId) != _deniedPerms.end();
        }

        /**
         * @brief 添加授予权限
         * @param permissionId 权限ID
         */
        void AddGrantedPermission(uint32 permissionId)
        {
            _grantedPerms.insert(permissionId);
        }

        /**
         * @brief 移除授予权限
         * @param permissionId 权限ID
         */
        void RemoveGrantedPermission(uint32 permissionId)
        {
            _grantedPerms.erase(permissionId);
        }

        /**
         * @brief 添加拒绝权限
         * @param permissionId 权限ID
         */
        void AddDeniedPermission(uint32 permissionId)
        {
            _deniedPerms.insert(permissionId);
        }

        /**
         * @brief 移除拒绝权限
         * @param permissionId 权限ID
         */
        void RemoveDeniedPermission(uint32 permissionId)
        {
            _deniedPerms.erase(permissionId);
        }

        /**
         * @brief 将权限列表添加到另一个列表
         * @param permsFrom 源权限列表
         * @param permsTo 目标权限列表
         */
        void AddPermissions(RBACPermissionContainer const& permsFrom, RBACPermissionContainer& permsTo);

        /**
         * @brief 从权限列表中移除另一个列表的权限
         * @param permsFrom 源权限列表（会被修改）
         * @param permsToRemove 要移除的权限列表
         */
        void RemovePermissions(RBACPermissionContainer& permsFrom, RBACPermissionContainer const& permsToRemove);

        /**
         * @brief 展开权限列表（包含所有关联权限）
         * @param permissions 权限列表（输入输出参数）
         *
         * 给定一个权限列表，获取所有继承的权限
         * 例如：如果权限A关联了权限B和C，展开后将包含A、B、C
         */
        void ExpandPermissions(RBACPermissionContainer& permissions);

        uint32 _id;                         ///< 账户ID
        std::string _name;                  ///< 账户名称
        int32 _realmId;                     ///< 领域ID
        uint8 _secLevel;                    ///< 安全等级
        RBACPermissionContainer _grantedPerms;  ///< 授予权限集合
        RBACPermissionContainer _deniedPerms;   ///< 拒绝权限集合
        RBACPermissionContainer _globalPerms;   ///< 计算后的全局权限集合
};

}

#endif
