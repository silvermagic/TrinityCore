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
 * @file LFG.h
 * @brief 地下城查找系统（LFG - Looking For Group）核心定义
 *
 * 本文件定义了LFG系统的基础数据结构、枚举类型和工具函数。
 * LFG系统允许玩家通过自动匹配系统寻找队友，组成地下城或团队副本队伍。
 *
 * 主要功能：
 * - 定义玩家角色类型（坦克、治疗、输出）
 * - 定义LFG系统的各种状态和更新类型
 * - 提供辅助函数用于字符串格式化
 *
 * 相关模块：
 * - LFGMgr: LFG系统管理器
 * - LFGQueue: 队列匹配逻辑
 * - LFGScripts: 与游戏核心的交互脚本
 */

#ifndef _LFG_H
#define _LFG_H

#include "Define.h"
#include "ObjectGuid.h"
#include <map>
#include <set>
#include <string>

namespace lfg
{

/**
 * @brief LFG系统常量定义
 *
 * 定义了LFG队伍中各角色所需的数量
 */
enum LFGEnum
{
    LFG_TANKS_NEEDED                             = 1,      // 需要的坦克数量
    LFG_HEALERS_NEEDED                           = 1,      // 需要的治疗数量
    LFG_DPS_NEEDED                               = 3       // 需要的输出数量
};

/**
 * @brief 玩家角色类型定义
 *
 * 定义了玩家在LFG系统中可以选择的角色类型，使用位掩码表示
 * 玩家可以同时选择多个角色（例如同时选择坦克和队长）
 */
enum LfgRoles
{
    PLAYER_ROLE_NONE                             = 0x00,   // 无角色
    PLAYER_ROLE_LEADER                           = 0x01,   // 队长
    PLAYER_ROLE_TANK                             = 0x02,   // 坦克
    PLAYER_ROLE_HEALER                           = 0x04,   // 治疗
    PLAYER_ROLE_DAMAGE                           = 0x08,   // 输出（DPS）
    PLAYER_ROLE_ANY                              = PLAYER_ROLE_LEADER | PLAYER_ROLE_TANK | PLAYER_ROLE_HEALER | PLAYER_ROLE_DAMAGE  // 任意角色
};

/**
 * @brief LFG更新类型
 *
 * 定义了LFG系统向客户端发送的各种更新消息类型
 * 这些类型决定了客户端如何显示LFG状态变化
 */
enum LfgUpdateType
{
    LFG_UPDATETYPE_DEFAULT                       = 0,      // 内部使用 - 默认状态
    LFG_UPDATETYPE_LEADER_UNK1                   = 1,      // 队长离开队伍时使用（FIXME: 需要进一步确认）
    LFG_UPDATETYPE_ROLECHECK_ABORTED             = 4,      // 角色检查中止
    LFG_UPDATETYPE_JOIN_QUEUE                    = 5,      // 加入队列
    LFG_UPDATETYPE_ROLECHECK_FAILED              = 6,      // 角色检查失败
    LFG_UPDATETYPE_REMOVED_FROM_QUEUE            = 7,      // 从队列中移除
    LFG_UPDATETYPE_PROPOSAL_FAILED               = 8,      // 提案失败
    LFG_UPDATETYPE_PROPOSAL_DECLINED             = 9,      // 提案被拒绝
    LFG_UPDATETYPE_GROUP_FOUND                   = 10,     // 队伍已找到
    LFG_UPDATETYPE_ADDED_TO_QUEUE                = 12,     // 已添加到队列
    LFG_UPDATETYPE_PROPOSAL_BEGIN                = 13,     // 提案开始
    LFG_UPDATETYPE_UPDATE_STATUS                 = 14,     // 更新状态
    LFG_UPDATETYPE_GROUP_MEMBER_OFFLINE          = 15,     // 队员离线
    LFG_UPDATETYPE_GROUP_DISBAND_UNK16           = 16,     // 队伍解散时使用（FIXME: 需要进一步确认）
};

/**
 * @brief LFG系统状态
 *
 * 定义了玩家或队伍在LFG系统中可能处于的各种状态
 * 状态转换流程：NONE -> ROLECHECK -> QUEUED -> PROPOSAL -> DUNGEON -> FINISHED_DUNGEON
 */
enum LfgState
{
    LFG_STATE_NONE,                                        // 未使用LFG/LFR
    LFG_STATE_ROLECHECK,                                   // 角色检查进行中
    LFG_STATE_QUEUED,                                      // 已排队等待
    LFG_STATE_PROPOSAL,                                    // 提案进行中（匹配成功，等待确认）
    //LFG_STATE_BOOT,                                      // 投票踢人进行中（未实现）
    LFG_STATE_DUNGEON = 5,                                 // 在LFG队伍中，正在进行副本
    LFG_STATE_FINISHED_DUNGEON,                            // 在LFG队伍中，副本已完成
    LFG_STATE_RAIDBROWSER                                  // 使用团队查找器（LFR）
};

/**
 * @brief 副本锁定状态类型
 *
 * 定义了玩家无法进入特定副本的各种原因
 * 这些状态会显示给玩家，告知他们为什么无法排队
 */
enum LfgLockStatusType
{
    LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION        = 1,      // 资料片不足
    LFG_LOCKSTATUS_TOO_LOW_LEVEL                 = 2,      // 等级过低
    LFG_LOCKSTATUS_TOO_HIGH_LEVEL                = 3,      // 等级过高
    LFG_LOCKSTATUS_TOO_LOW_GEAR_SCORE            = 4,      // 装备等级过低
    LFG_LOCKSTATUS_TOO_HIGH_GEAR_SCORE           = 5,      // 装备等级过高
    LFG_LOCKSTATUS_RAID_LOCKED                   = 6,      // 团队副本已锁定
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_LOW_LEVEL      = 1001,   // 钥匙等级过低
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_HIGH_LEVEL     = 1002,   // 钥匙等级过高
    LFG_LOCKSTATUS_QUEST_NOT_COMPLETED           = 1022,   // 任务未完成
    LFG_LOCKSTATUS_MISSING_ITEM                  = 1025,   // 缺少物品
    LFG_LOCKSTATUS_NOT_IN_SEASON                 = 1031,   // 不在季节性活动期间
    LFG_LOCKSTATUS_MISSING_ACHIEVEMENT           = 1034    // 缺少成就
};

/**
 * @brief 答案状态
 *
 * 定义了玩家对提案或投票的响应状态
 * 也用于检查队伍兼容性
 */
enum LfgAnswer
{
    LFG_ANSWER_PENDING                           = -1,     // 等待回答
    LFG_ANSWER_DENY                              = 0,      // 拒绝
    LFG_ANSWER_AGREE                             = 1       // 同意
};

// 类型定义
typedef std::set<uint32> LfgDungeonSet;                           // 副本ID集合
typedef std::map<uint32, uint32> LfgLockMap;                      // 副本锁定映射（副本ID -> 锁定原因）
typedef std::map<ObjectGuid, LfgLockMap> LfgLockPartyMap;         // 队伍副本锁定映射（玩家GUID -> 锁定映射）
typedef std::map<ObjectGuid, uint8> LfgRolesMap;                  // 角色映射（玩家GUID -> 角色位掩码）
typedef std::map<ObjectGuid, ObjectGuid> LfgGroupsMap;            // 队伍映射（玩家GUID -> 队伍GUID）

/**
 * @brief 将副本ID集合连接成字符串
 *
 * @param dungeons 副本ID集合
 * @return 格式化的字符串，副本ID之间用逗号分隔
 *
 * 用途：用于日志输出和调试
 */
TC_GAME_API std::string ConcatenateDungeons(LfgDungeonSet const& dungeons);

/**
 * @brief 获取角色的字符串表示
 *
 * @param roles 角色位掩码
 * @return 角色名称字符串，多个角色用逗号分隔
 *
 * 用途：将角色位掩码转换为可读的字符串（如"坦克, 治疗"）
 */
TC_GAME_API std::string GetRolesString(uint8 roles);

/**
 * @brief 获取LFG状态的字符串表示
 *
 * @param state LFG状态枚举
 * @return 状态名称字符串
 *
 * 用途：将状态枚举转换为可读的字符串用于显示
 */
TC_GAME_API std::string GetStateString(LfgState state);

} // namespace lfg

#endif
