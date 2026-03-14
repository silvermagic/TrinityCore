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
 * @file LFGMgr.h
 * @brief 地下城查找器管理器头文件
 *
 * 本文件实现了TrinityCore的地下城查找器(Dungeon Finder)系统的核心管理器。
 * 该系统为玩家提供自动匹配功能,支持随机副本、团队副本查找等功能。
 *
 * 主要功能包括:
 * - 队列管理: 玩家和队伍排队等待匹配
 * - 角色检查: 确保队伍有坦克、治疗、输出的合理配置
 * - 提案系统: 匹配成功后的确认机制
 * - 投票踢人: 队伍内踢出玩家的民主投票机制
 * - 奖励系统: 完成随机副本的奖励发放
 * - 传送功能: 进入和离开副本的传送控制
 *
 * @see LFGQueue 队列管理类
 * @see LFGPlayerData 玩家数据类
 * @see LFGGroupData 队伍数据类
 */

#ifndef _LFGMGR_H
#define _LFGMGR_H

#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "LFG.h"
#include "LFGQueue.h"
#include "LFGGroupData.h"
#include "LFGPlayerData.h"
#include "SharedDefines.h"
#include <unordered_map>

class Group;
class Player;
class Quest;
class Map;
struct LFGDungeonEntry;
enum Difficulty : uint8;

namespace lfg
{

/**
 * @brief LFG选项标志位掩码
 *
 * 用于控制地下城查找器功能的启用状态。
 * 这些选项可以通过配置文件进行设置。
 */
enum LfgOptions
{
    LFG_OPTION_ENABLE_DUNGEON_FINDER             = 0x01,  ///< 启用副本查找器（随机副本系统）
    LFG_OPTION_ENABLE_RAID_BROWSER               = 0x02,  ///< 启用团队浏览器（团队副本查找）
};

/**
 * @brief LFG管理器常量枚举
 *
 * 定义地下城查找系统使用的各种常量值，
 * 包括超时时间、法术ID和系统参数。
 */
enum LFGMgrEnum
{
    LFG_TIME_ROLECHECK                           = 45,    ///< 角色检查超时时间（秒） - 玩家选择角色的时限
    LFG_TIME_BOOT                                = 120,   ///< 投票踢人超时时间（秒） - 投票踢人的时限
    LFG_TIME_PROPOSAL                            = 45,    ///< 提案确认超时时间（秒） - 确认副本提案的时限
    LFG_QUEUEUPDATE_INTERVAL                     = 15 * IN_MILLISECONDS,  ///< 队列更新间隔（毫秒） - 队列状态更新频率
    LFG_SPELL_DUNGEON_COOLDOWN                   = 71328, ///< 地下城冷却法术ID - 限制频繁排队
    LFG_SPELL_DUNGEON_DESERTER                   = 71041, ///< 逃亡者减益法术ID - 中途离开副本的惩罚
    LFG_SPELL_LUCK_OF_THE_DRAW                   = 72221, ///< 幸运抽奖增益法术ID - 随机副本奖励增益
    LFG_GROUP_KICK_VOTES_NEEDED                  = 3      ///< 需要的踢人投票数 - 通过踢人提案所需的最小票数
};

/**
 * @brief LFG副本标志位
 *
 * 用于标识副本的特殊属性，如季节性活动等。
 */
enum LfgFlags
{
    LFG_FLAG_UNK1                                = 0x1,   ///< 未知标志1（保留用于未来扩展）
    LFG_FLAG_UNK2                                = 0x2,   ///< 未知标志2（保留用于未来扩展）
    LFG_FLAG_SEASONAL                            = 0x4,   ///< 季节性标志 - 标识节日活动副本
    LFG_FLAG_UNK3                                = 0x8    ///< 未知标志3（保留用于未来扩展）
};

/**
 * @brief 副本类型枚举
 *
 * 定义地下城查找系统中副本的分类类型。
 * 用于区分不同性质的副本内容。
 */
enum LfgType
{
    LFG_TYPE_NONE                                = 0,       ///< 无类型 - 未定义或无效的副本
    LFG_TYPE_DUNGEON                             = 1,       ///< 普通地下城 - 5人小队副本
    LFG_TYPE_RAID                                = 2,       ///< 团队副本 - 10-25人大型副本
    LFG_TYPE_HEROIC                              = 5,       ///< 英雄副本 - 英雄难度地下城
    LFG_TYPE_RANDOM                              = 6        ///< 随机副本 - 随机匹配的副本
};

/**
 * @brief 提案状态枚举
 *
 * 定义副本匹配提案的生命周期状态。
 * 当系统找到合适的队伍组合后，会创建提案供玩家确认。
 */
enum LfgProposalState
{
    LFG_PROPOSAL_INITIATING                      = 0,       ///< 初始化中 - 提案刚创建，等待玩家响应
    LFG_PROPOSAL_FAILED                          = 1,       ///< 提案失败 - 有玩家拒绝或超时
    LFG_PROPOSAL_SUCCESS                         = 2        ///< 提案成功 - 所有玩家都已确认
};

/**
 * @brief 传送错误类型枚举
 *
 * 定义玩家尝试传送进/出副本时可能遇到的错误情况。
 * 这些错误码用于向客户端发送传送失败的反馈。
 *
 * @note 错误码7 = "你现在无法这样做" | 错误码5 = 无客户端反应
 */
enum LfgTeleportError
{
    // 7 = "You can't do that right now" | 5 = No client reaction
    LFG_TELEPORTERROR_OK                         = 0,      ///< 内部使用 - 传送正常执行
    LFG_TELEPORTERROR_PLAYER_DEAD                = 1,      ///< 玩家已死亡 - 死亡状态下无法传送
    LFG_TELEPORTERROR_FALLING                    = 2,      ///< 玩家正在下落 - 下落过程中无法传送
    LFG_TELEPORTERROR_IN_VEHICLE                 = 3,      ///< 玩家在载具中 - 必须先离开载具
    LFG_TELEPORTERROR_FATIGUE                    = 4,      ///< 疲劳区域 - 在疲劳水域中无法传送
    LFG_TELEPORTERROR_INVALID_LOCATION           = 6,      ///< 无效位置 - 当前位置无法传送
    LFG_TELEPORTERROR_CHARMING                   = 8       ///< 被魅惑状态 - 被魅惑控制时无法传送 // FIXME - 可能是7或8（需要正确数据）
};

/**
 * @brief 加入队列结果枚举
 *
 * 定义玩家尝试加入LFG队列时可能返回的各种结果状态。
 * 这些结果码用于向客户端反馈加入失败的具体原因。
 *
 * @note 错误码3 = 无客户端反应 | 错误码18 = "角色检查失败"
 */
enum LfgJoinResult
{
    // 3 = No client reaction | 18 = "Rolecheck failed"
    LFG_JOIN_OK                                  = 0,      ///< 成功加入（无客户端消息） - 队列加入成功
    LFG_JOIN_FAILED                              = 1,      ///< 角色检查失败 - 角色分配不满足要求
    LFG_JOIN_GROUPFULL                           = 2,      ///< 队伍已满 - 当前队伍人数已达上限
    LFG_JOIN_INTERNAL_ERROR                      = 4,      ///< 内部LFG错误 - 服务器内部错误
    LFG_JOIN_NOT_MEET_REQS                       = 5,      ///< 你不满足所选副本的要求 - 等级、装备等条件不足
    LFG_JOIN_PARTY_NOT_MEET_REQS                 = 6,      ///< 一个或多个队友不满足所选副本的要求
    LFG_JOIN_MIXED_RAID_DUNGEON                  = 7,      ///< 选择副本时不能混合地下城、团队副本和随机副本
    LFG_JOIN_MULTI_REALM                         = 8,      ///< 所选副本不支持跨服玩家 - 该副本仅限同服务器
    LFG_JOIN_DISCONNECTED                        = 9,      ///< 一个或多个队友正在等待邀请或已断开连接
    LFG_JOIN_PARTY_INFO_FAILED                   = 10,     ///< 无法获取某些队友的信息 - 数据查询失败
    LFG_JOIN_DUNGEON_INVALID                     = 11,     ///< 一个或多个副本无效 - 副本ID不存在或不可用
    LFG_JOIN_DESERTER                            = 12,     ///< 逃亡者减益效果未消失前无法排队 - 中途退场惩罚
    LFG_JOIN_PARTY_DESERTER                      = 13,     ///< 一个或多个队友有逃亡者减益效果
    LFG_JOIN_RANDOM_COOLDOWN                     = 14,     ///< 随机副本冷却中无法排队 - 需等待冷却结束
    LFG_JOIN_PARTY_RANDOM_COOLDOWN               = 15,     ///< 一个或多个队友在随机副本冷却中
    LFG_JOIN_TOO_MUCH_MEMBERS                    = 16,     ///< 队伍成员超过5人无法进入副本 - 超出队伍人数限制
    LFG_JOIN_USING_BG_SYSTEM                     = 17      ///< 在战场或竞技场中无法使用副本系统 - 已在其他队列中
};

/**
 * @brief 角色检查状态枚举
 *
 * 定义队伍角色检查过程中的各种状态。
 * 在加入LFG队列前，队伍成员需要选择自己的角色（坦克/治疗/输出）。
 */
enum LfgRoleCheckState
{
    LFG_ROLECHECK_DEFAULT                        = 0,      ///< 内部使用 - 未初始化状态
    LFG_ROLECHECK_FINISHED                       = 1,      ///< 角色检查完成 - 所有成员已选择有效角色
    LFG_ROLECHECK_INITIALITING                   = 2,      ///< 角色检查开始 - 等待成员选择角色
    LFG_ROLECHECK_MISSING_ROLE                   = 3,      ///< 有人2分钟内未选择角色 - 超时未响应
    LFG_ROLECHECK_WRONG_ROLES                    = 4,      ///< 角色选择无法组成有效队伍 - 缺少坦克或治疗
    LFG_ROLECHECK_ABORTED                        = 5,      ///< 有人离开了队伍 - 检查过程中断
    LFG_ROLECHECK_NO_ROLE                        = 6       ///< 有人没有选择角色 - 选择为空
};

/**
 * @brief 角色职业位掩码枚举
 *
 * 定义哪些职业可以担任坦克或治疗角色的位掩码。
 * 用于在LFG系统中验证玩家职业与所选角色的兼容性。
 *
 * @note 位掩码基于职业ID（CLASS_*）构建，用于快速查找职业角色能力。
 */
enum LfgRoleClasses {
    /**
     * @brief 可以担任坦克的职业位掩码
     *
     * 包括：战士、圣骑士、死亡骑士、德鲁伊
     */
    TANK = (1 << (CLASS_WARRIOR - 1)) |
           (1 << (CLASS_PALADIN - 1)) |
           (1 << (CLASS_DEATH_KNIGHT - 1)) |
           (1 << (CLASS_DRUID - 1)),

    /**
     * @brief 可以担任治疗的职业位掩码
     *
     * 包括：圣骑士、牧师、萨满、德鲁伊
     */
    HEALER = (1 << (CLASS_PALADIN - 1)) |
             (1 << (CLASS_PRIEST - 1)) |
             (1 << (CLASS_SHAMAN - 1)) |
             (1 << (CLASS_DRUID - 1)),
};

// 前向声明（为了将所有typedef放在一起）
struct LFGDungeonData;    ///< LFG副本数据结构
struct LfgReward;         ///< LFG奖励数据结构
struct LfgQueueInfo;      ///< LFG队列信息结构
struct LfgRoleCheck;      ///< LFG角色检查结构
struct LfgProposal;       ///< LFG提案结构
struct LfgProposalPlayer; ///< LFG提案玩家结构
struct LfgPlayerBoot;     ///< LFG踢人投票结构

/**
 * @brief LFG系统使用的类型定义
 *
 * 这些类型定义用于组织和管理LFG系统的各种数据结构。
 */

typedef std::map<uint8, LFGQueue> LfgQueueContainer;                          ///< 队列容器：按队伍类型索引存储队列对象
typedef std::multimap<uint32, LfgReward const*> LfgRewardContainer;           ///< 奖励容器：按等级索引存储奖励数据
typedef std::pair<LfgRewardContainer::const_iterator, LfgRewardContainer::const_iterator> LfgRewardContainerBounds; ///< 奖励容器范围：用于快速查找指定等级范围的奖励
typedef std::map<uint8, LfgDungeonSet> LfgCachedDungeonContainer;             ///< 缓存的副本容器：按队伍类型索引存储副本集合
typedef std::map<ObjectGuid, LfgAnswer> LfgAnswerContainer;                   ///< 答案容器：存储玩家对提案的投票结果
typedef std::map<ObjectGuid, LfgRoleCheck> LfgRoleCheckContainer;             ///< 角色检查容器：按队伍GUID索引存储角色检查数据
typedef std::map<uint32, LfgProposal> LfgProposalContainer;                   ///< 提案容器：按提案ID索引存储提案数据
typedef std::map<ObjectGuid, LfgProposalPlayer> LfgProposalPlayerContainer;   ///< 提案玩家容器：按玩家GUID索引存储玩家提案数据
typedef std::map<ObjectGuid, LfgPlayerBoot> LfgPlayerBootContainer;           ///< 投票踢人容器：按队伍GUID索引存储踢人投票数据
typedef std::map<ObjectGuid, LfgGroupData> LfgGroupDataContainer;             ///< 队伍数据容器：按队伍GUID索引存储队伍数据
typedef std::map<ObjectGuid, LfgPlayerData> LfgPlayerDataContainer;           ///< 玩家数据容器：按玩家GUID索引存储玩家数据
typedef std::unordered_map<uint32, LFGDungeonData> LFGDungeonContainer;       ///< 副本数据容器：按副本ID索引存储副本数据

/**
 * @brief 加入队列结果数据结构
 *
 * 用于SMSG_LFG_JOIN_RESULT数据包，包含加入队列的结果信息和相关数据。
 */
struct LfgJoinResultData
{
    /**
     * @brief 构造函数
     * @param _result 加入结果（默认为成功）
     * @param _state 角色检查状态（默认为未初始化）
     */
    LfgJoinResultData(LfgJoinResult _result = LFG_JOIN_OK, LfgRoleCheckState _state = LFG_ROLECHECK_DEFAULT):
        result(_result), state(_state) { }

    LfgJoinResult result;          ///< 加入结果 - 操作成功或失败的具体原因
    LfgRoleCheckState state;       ///< 角色检查状态 - 角色检查的当前状态
    LfgLockPartyMap lockmap;       ///< 锁定的副本映射 - 记录哪些副本因条件不满足而不可用
};

/**
 * @brief LFG更新数据结构
 *
 * 用于SMSG_LFG_UPDATE_PARTY和SMSG_LFG_UPDATE_PLAYER数据包，
 * 向客户端发送LFG状态更新的详细信息。
 */
struct LfgUpdateData
{
    /**
     * @brief 默认构造函数
     * @param _type 更新类型（默认为默认类型）
     */
    LfgUpdateData(LfgUpdateType _type = LFG_UPDATETYPE_DEFAULT): updateType(_type), state(LFG_STATE_NONE), comment("") { }

    /**
     * @brief 带副本集合的构造函数
     * @param _type 更新类型
     * @param _dungeons 副本集合
     * @param _comment 备注信息
     */
    LfgUpdateData(LfgUpdateType _type, LfgDungeonSet const& _dungeons, std::string const& _comment):
        updateType(_type), state(LFG_STATE_NONE), dungeons(_dungeons), comment(_comment) { }

    /**
     * @brief 完整构造函数
     * @param _type 更新类型
     * @param _state LFG状态
     * @param _dungeons 副本集合
     * @param _comment 备注信息（可选）
     */
    LfgUpdateData(LfgUpdateType _type, LfgState _state, LfgDungeonSet const& _dungeons, std::string const& _comment = ""):
        updateType(_type), state(_state), dungeons(_dungeons), comment(_comment) { }

    LfgUpdateType updateType;       ///< 更新类型 - 指示更新的具体操作类型
    LfgState state;                 ///< LFG状态 - 当前的LFG系统状态
    LfgDungeonSet dungeons;         ///< 副本集合 - 申请加入的副本列表
    std::string comment;            ///< 备注 - 用于团队查找器(LFR)的说明文本
};

/**
 * @brief LFG队列状态数据结构
 *
 * 用于SMSG_LFG_QUEUE_STATUS数据包，向客户端发送当前队列的等待信息。
 * 显示预估等待时间、队列中各角色数量等统计信息。
 */
struct LfgQueueStatusData
{
    /**
     * @brief 构造函数
     * @param _dungeonId 副本ID（默认为0）
     * @param _waitTime 预估等待时间（默认为-1表示未知）
     * @param _waitTimeAvg 平均等待时间（默认为-1表示未知）
     * @param _waitTimeTank 坦克等待时间（默认为-1表示未知）
     * @param _waitTimeHealer 治疗等待时间（默认为-1表示未知）
     * @param _waitTimeDps 输出等待时间（默认为-1表示未知）
     * @param _queuedTime 已排队时间（默认为0）
     * @param _tanks 需要的坦克数量（默认为0）
     * @param _healers 需要的治疗数量（默认为0）
     * @param _dps 需要的输出数量（默认为0）
     */
    LfgQueueStatusData(uint32 _dungeonId = 0, int32 _waitTime = -1, int32 _waitTimeAvg = -1, int32 _waitTimeTank = -1, int32 _waitTimeHealer = -1,
        int32 _waitTimeDps = -1, uint32 _queuedTime = 0, uint8 _tanks = 0, uint8 _healers = 0, uint8 _dps = 0) :
        dungeonId(_dungeonId), waitTime(_waitTime), waitTimeAvg(_waitTimeAvg), waitTimeTank(_waitTimeTank), waitTimeHealer(_waitTimeHealer),
        waitTimeDps(_waitTimeDps), queuedTime(_queuedTime), tanks(_tanks), healers(_healers), dps(_dps) { }

    uint32 dungeonId;              ///< 副本ID - 当前排队的副本标识
    int32 waitTime;                ///< 预估等待时间（秒） - 根据当前队列情况估算
    int32 waitTimeAvg;             ///< 平均等待时间（秒） - 历史平均等待时长
    int32 waitTimeTank;            ///< 坦克等待时间（秒） - 坦克角色的预估等待时间
    int32 waitTimeHealer;          ///< 治疗等待时间（秒） - 治疗角色的预估等待时间
    int32 waitTimeDps;             ///< 输出等待时间（秒） - 输出角色的预估等待时间
    uint32 queuedTime;             ///< 已排队时间（秒） - 玩家已在队列中的时间
    uint8 tanks;                   ///< 需要的坦克数量 - 完整队伍所需的坦克数
    uint8 healers;                 ///< 需要的治疗数量 - 完整队伍所需的治疗数
    uint8 dps;                     ///< 需要的输出数量 - 完整队伍所需的输出数
};

/**
 * @brief 玩家奖励数据结构
 *
 * 存储玩家完成随机副本后获得的奖励信息，
 * 包括奖励任务和完成状态。
 */
struct LfgPlayerRewardData
{
    /**
     * @brief 构造函数
     * @param random 随机副本入口ID
     * @param current 具体副本入口ID
     * @param _done 是否已完成（是否首次完成）
     * @param _quest 奖励任务指针
     */
    LfgPlayerRewardData(uint32 random, uint32 current, bool _done, Quest const* _quest):
        rdungeonEntry(random), sdungeonEntry(current), done(_done), quest(_quest) { }

    uint32 rdungeonEntry;          ///< 随机副本入口ID - 随机副本的标识
    uint32 sdungeonEntry;          ///< 具体副本入口ID - 实际进入的副本标识
    bool done;                      ///< 是否已完成 - true表示今日已完成，false表示首次完成
    Quest const* quest;             ///< 奖励任务 - 关联的奖励任务对象
};

/**
 * @brief 奖励信息结构
 *
 * 定义随机副本奖励的配置信息，包括等级范围和对应的奖励任务。
 * 根据玩家等级匹配相应的奖励。
 */
struct LfgReward
{
    /**
     * @brief 构造函数
     * @param _maxLevel 最高适用等级（默认为0）
     * @param _firstQuest 首次完成奖励任务ID（默认为0）
     * @param _otherQuest 非首次完成奖励任务ID（默认为0）
     */
    LfgReward(uint32 _maxLevel = 0, uint32 _firstQuest = 0, uint32 _otherQuest = 0):
        maxLevel(_maxLevel), firstQuest(_firstQuest), otherQuest(_otherQuest) { }

    uint32 maxLevel;               ///< 最高等级 - 此奖励适用的最高玩家等级
    uint32 firstQuest;             ///< 首次完成奖励任务ID - 每天首次完成获得的奖励任务
    uint32 otherQuest;             ///< 非首次完成奖励任务ID - 再次完成获得的奖励任务
};

/**
 * @brief 提案玩家数据结构
 *
 * 存储与加入副本提案相关的单个玩家的数据，
 * 包括角色分配、接受状态和原始队伍信息。
 */
struct LfgProposalPlayer
{
    /**
     * @brief 默认构造函数
     *
     * 初始化角色为0，接受状态为待定，队伍GUID为空
     */
    LfgProposalPlayer(): role(0), accept(LFG_ANSWER_PENDING), group() { }

    uint8 role;                                            ///< 提议的角色 - 玩家在副本中的角色（坦克/治疗/输出）
    LfgAnswer accept;                                      ///< 接受状态 - -1:未回答 | 0:不同意 | 1:同意
    ObjectGuid group;                                      ///< 原始队伍GUID - 如果玩家已有队伍则记录，单独排队则为空
};

/**
 * @brief 提案数据结构
 *
 * 存储与加入副本提案相关的完整队伍数据。
 * 当系统找到合适的队伍组合后，会创建提案供所有玩家确认。
 */
struct LfgProposal
{
    /**
     * @brief 构造函数
     * @param dungeon 副本ID（默认为0）
     */
    LfgProposal(uint32 dungeon = 0): id(0), dungeonId(dungeon), state(LFG_PROPOSAL_INITIATING),
        group(), leader(), cancelTime(0), encounters(0), isNew(true)
        { }

    uint32 id;                                             ///< 提案ID - 唯一标识符
    uint32 dungeonId;                                      ///< 要加入的副本ID - 目标副本标识
    LfgProposalState state;                                ///< 提案状态 - 当前提案的生命周期状态
    ObjectGuid group;                                      ///< 提案队伍GUID - 如果是新队伍则为空
    ObjectGuid leader;                                     ///< 队长GUID - 负责提案的玩家
    time_t cancelTime;                                     ///< 取消提案的时间 - 超时时间戳
    uint32 encounters;                                     ///< 副本遭遇战数量 - 已完成的Boss数量
    bool isNew;                                            ///< 是否是新队伍 - true表示新创建的队伍
    GuidList queues;                                       ///< 队列ID列表 - 需要移除或重新添加的队列
    GuidList showorder;                                    ///< 显示顺序 - 更新窗口中的玩家显示顺序
    LfgProposalPlayerContainer players;                    ///< 玩家数据 - 所有参与提案的玩家信息
};

/**
 * @brief 角色检查数据结构
 *
 * 存储想要加入LFG队列的队伍的角色检查信息。
 * 在加入队列前，队伍成员需要选择自己的角色。
 */
struct LfgRoleCheck
{
    time_t cancelTime;                                     ///< 角色检查失败的时间 - 超时时间戳
    LfgRolesMap roles;                                     ///< 玩家选择的角色映射 - GUID到角色的映射
    LfgRoleCheckState state;                               ///< 角色检查状态 - 当前的检查进度状态
    LfgDungeonSet dungeons;                                ///< 队伍申请的副本集合 - 展开后的随机副本列表
    uint32 rDungeonId;                                     ///< 随机副本ID - 原始申请的随机副本
    ObjectGuid leader;                                     ///< 队伍队长 - 发起角色检查的玩家
};

/**
 * @brief 投票踢人数据结构
 *
 * 存储当前进行中的投票踢人提案信息。
 * 允许队伍成员投票决定是否踢出某个玩家。
 */
struct LfgPlayerBoot
{
    time_t cancelTime;                                     ///< 投票剩余时间 - 投票超时的时间戳
    bool inProgress;                                       ///< 投票是否进行中 - true表示正在投票
    LfgAnswerContainer votes;                              ///< 玩家投票结果 - GUID到投票结果的映射（-1:未回答 | 0:不同意 | 1:同意）
    ObjectGuid victim;                                     ///< 被踢玩家的GUID - 被投票踢出的玩家（不能参与投票）
    std::string reason;                                    ///< 踢人原因 - 发起踢人时提供的理由
};

/**
 * @brief LFG副本数据结构
 *
 * 存储地下城查找系统中副本的详细信息，
 * 包括副本标识、地图、等级范围和传送坐标等。
 */
struct LFGDungeonData
{
    /**
     * @brief 默认构造函数
     */
    LFGDungeonData();

    /**
     * @brief 从DBC数据构造
     * @param dbc LFG副本条目数据指针
     */
    LFGDungeonData(LFGDungeonEntry const* dbc);

    uint32 id;                     ///< 副本ID - 唯一标识符
    std::string name;              ///< 副本名称 - 显示给玩家的名称
    uint16 map;                    ///< 地图ID - 关联的地图标识
    uint8 type;                    ///< 副本类型 - 地下城/团队副本等
    uint8 expansion;               ///< 资料片 - 所属游戏版本（0=经典，1=TBC，2=WLK）
    uint8 group;                   ///< 组别 - 副本分类组
    uint8 minlevel;                ///< 最低等级 - 进入所需的最低等级
    uint8 maxlevel;                ///< 最高等级 - 进入所需的最高等级
    Difficulty difficulty;         ///< 难度 - 副本难度等级
    bool seasonal;                 ///< 是否为季节性副本 - 节日活动副本标记
    float x, y, z, o;              ///< 传送坐标和朝向 - 进入副本的传送位置

    /**
     * @brief 获取完整的副本入口ID
     * @return 组合后的入口ID（副本ID + 类型左移24位）
     */
    uint32 Entry() const { return id + (type << 24); }
};

/**
 * @brief LFG管理器类 - 地下城查找系统的核心管理器
 *
 * 该类采用单例模式,负责管理整个地下城查找器系统的核心逻辑。
 * 它协调玩家、队伍、队列、提案等多个子系统,实现完整的副本匹配功能。
 *
 * 主要职责:
 * - 队列管理: 维护不同队伍类型的队列,处理排队和匹配逻辑
 * - 角色检查: 确保队伍有合理的角色配置(坦克、治疗、输出)
 * - 提案系统: 匹配成功后创建提案供玩家确认
 * - 投票踢人: 实现队伍内踢出玩家的民主投票机制
 * - 奖励系统: 管理随机副本完成后的奖励发放
 * - 传送功能: 控制玩家进入和离开副本的传送
 *
 * 架构说明:
 * - 单例模式: 通过instance()静态方法获取全局唯一实例
 * - 数据分离: 玩家数据(LfgPlayerData)和队伍数据(LfgGroupData)分开存储
 * - 队列隔离: 不同队伍类型的队列独立管理
 * - 事件驱动: 通过Update()函数定期检查超时和状态更新
 *
 * 线程安全:
 * - 主要运行在世界服务器的主线程中
 * - 所有公共接口应当从主线程调用
 *
 * @note 该类不可复制或移动,只能通过instance()获取实例
 */
class TC_GAME_API LFGMgr
{
    private:
        /**
         * @brief 私有构造函数(单例模式)
         *
         * 初始化所有成员变量为默认值,设置选项标志。
         * 仅通过instance()静态方法创建实例。
         */
        LFGMgr();

        /**
         * @brief 私有析构函数
         *
         * 清理所有动态分配的资源,包括队列、提案、玩家数据等。
         */
        ~LFGMgr();

    public:
        /**
         * @brief 获取LFG管理器单例实例
         * @return LFGMgr全局唯一实例的指针
         *
         * 使用惰性初始化创建单例实例。
         * 该方法是线程安全的(C++11 magic statics)。
         *
         * @code
         * LFGMgr* mgr = LFGMgr::instance();
         * // 或使用宏
         * sLFGMgr->Update(diff);
         * @endcode
         */
        static LFGMgr* instance();

        // ========== 外部使用的函数 ==========

        /**
         * @brief 更新LFG系统状态
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 该函数由世界服务器的UpdateWorld函数定期调用(主循环)。
         * 主要处理:
         * - 队列更新: 每15秒更新一次队列状态(LFG_QUEUEUPDATE_INTERVAL)
         * - 超时检查: 检查角色检查、提案、踢人投票是否超时
         * - 状态清理: 移除已完成的提案和过期的数据
         *
         * 性能说明:
         * - 更新频率: 每个世界更新周期调用一次(约50ms)
         * - 队列更新: 仅在m_QueueTimer达到15秒时执行完整队列更新
         * - 时间复杂度: O(n)其中n为当前活跃的提案/角色检查数量
         *
         * @note 必须在主线程中调用
         */
        void Update(uint32 diff);

        // ========== World.cpp 中使用的函数 ==========

        /**
         * @brief 完成指定队伍的副本
         * @param gguid 队伍GUID(组GUID)
         * @param dungeonId 完成的副本ID
         * @param currMap 当前地图指针,用于验证玩家位置
         *
         * 该函数在队伍完成副本时调用(通常在击杀最终Boss后)。
         * 主要执行:
         * - 状态更新: 将队伍状态从DUNGEON更新为FINISHED_DUNGEON
         * - 奖励检查: 检查玩家是否有资格获得随机副本奖励
         * - 奖励发放: 向符合条件的玩家发放奖励
         * - 数据库保存: 更新数据库中的副本完成记录
         *
         * 调用时机:
         * - Boss击杀时,由脚本或AI调用
         * - 副本完成事件触发时
         *
         * 注意事项:
         * - 所有检查使用内部LFG数据执行,不依赖外部查询
         * - 必须在主线程中调用
         * - currMap用于验证玩家确实在正确的副本中
         *
         * @note 只有通过LFG系统排队的队伍才会触发奖励发放
         */
        void FinishDungeon(ObjectGuid gguid, uint32 dungeonId, Map const* currMap);

        /**
         * @brief 加载随机副本的奖励数据
         *
         * 从数据库表lfg_dungeon_rewards加载奖励配置。
         * 加载的数据包括:
         * - 奖励等级范围(maxLevel)
         * - 首次完成奖励任务ID(firstQuest)
         * - 非首次完成奖励任务ID(otherQuest)
         *
         * 数据存储到RewardMapStore成员变量中,按等级索引。
         *
         * 调用时机:
         * - 服务器启动时,在初始化LFG系统时调用
         * - 重载配置时(reload参数)
         *
         * 性能说明:
         * - 时间复杂度: O(n)其中n为奖励配置数量
         * - 内存占用: 与奖励配置数量成正比
         *
         * @see GetRandomDungeonReward 获取指定等级的奖励
         */
        void LoadRewards();

        /**
         * @brief 从DBC加载副本数据并添加传送坐标
         * @param reload 是否为重新加载(默认为false)
         *
         * 从DBC文件(LFGDungeons.dbc)加载副本基础数据,
         * 并从数据库表lfg_dungeon_template补充传送坐标。
         *
         * 加载的数据包括:
         * - 副本ID、名称、地图ID
         * - 副本类型(地下城/团队副本)
         * - 等级范围(minlevel, maxlevel)
         * - 难度等级
         * - 传送坐标(x, y, z, o)
         * - 季节性标志
         *
         * 调用时机:
         * - 服务器启动时调用一次
         * - 重载配置时(reload=true)
         *
         * 性能说明:
         * - 时间复杂度: O(n)其中n为副本数量
         * - 内存占用: 与副本数量成正比
         *
         * @see GetLFGDungeon 获取指定副本的数据
         */
        void LoadLFGDungeons(bool reload = false);

        // ========== 多个文件使用的函数 ==========

        /**
         * @brief 检查指定GUID是否申请了随机副本
         * @param guid 玩家或队伍GUID
         * @return true表示选择了随机副本,false表示选择了具体副本
         *
         * 该函数用于判断玩家是通过随机副本查找器排队,
         * 还是通过具体副本查找器排队。
         *
         * 用途:
         * - 确定奖励类型(随机副本有额外奖励)
         * - 确定匹配优先级
         * - UI显示不同的状态信息
         *
         * @note 对于队伍GUID,检查队伍选择的副本;对于玩家GUID,检查玩家的副本
         */
        bool selectedRandomLfgDungeon(ObjectGuid guid);

        /**
         * @brief 检查指定GUID是否在指定的地图和难度中
         * @param guid 玩家GUID
         * @param map 地图ID
         * @param difficulty 难度等级
         * @return true表示玩家在该副本中,false表示不在
         *
         * 该函数用于验证玩家是否真正进入了通过LFG排队的副本。
         * 主要用于:
         * - 验证传送目标
         * - 检查奖励资格
         * - 防止滥用LFG系统
         *
         * 实现逻辑:
         * 1. 获取玩家的副本ID和难度
         * 2. 与参数比较是否匹配
         */
        bool inLfgDungeonMap(ObjectGuid guid, uint32 map, Difficulty difficulty);

        /**
         * @brief 获取已选择的副本集合
         * @param guid 玩家或队伍GUID
         * @return 副本ID集合的常量引用
         *
         * 返回玩家或队伍申请加入的副本列表。
         * 对于随机副本,返回展开后的具体副本列表。
         *
         * @note 返回的引用在下次操作前有效,不要长期保存
         */
        LfgDungeonSet const& GetSelectedDungeons(ObjectGuid guid);

        /**
         * @brief 获取当前LFG状态
         * @param guid 玩家或队伍GUID
         * @return 当前LFG状态枚举值
         *
         * LFG状态包括:
         * - LFG_STATE_NONE: 未使用LFG
         * - LFG_STATE_ROLECHECK: 角色检查中
         * - LFG_STATE_QUEUED: 在队列中等待
         * - LFG_STATE_PROPOSAL: 提案确认中
         * - LFG_STATE_BOOT: 投票踢人中
         * - LFG_STATE_DUNGEON: 在副本中
         * - LFG_STATE_FINISHED_DUNGEON: 副本已完成
         */
        LfgState GetState(ObjectGuid guid);

        /**
         * @brief 获取当前投票踢人状态
         * @param gguid 队伍GUID
         * @return true表示有踢人投票正在进行,false表示没有
         *
         * 用于在踢人投票期间限制某些操作,
         * 如不能再发起新的踢人提案。
         */
        bool IsVoteKickActive(ObjectGuid gguid);

        /**
         * @brief 获取当前副本ID
         * @param guid 玩家或队伍GUID
         * @param asId true返回副本ID,false返回入口ID(默认为true)
         * @return 副本ID或入口ID,如果不在副本中返回0
         *
         * 副本ID与入口ID的区别:
         * - 副本ID: 游戏内部使用的唯一标识
         * - 入口ID: 客户端显示的标识(包含类型信息)
         *
         * @see GetLFGDungeonEntry 在副本ID和入口ID之间转换
         */
        uint32 GetDungeon(ObjectGuid guid, bool asId = true);

        /**
         * @brief 获取当前副本的地图ID
         * @param guid 玩家或队伍GUID
         * @return 地图ID,如果不在副本中返回0
         *
         * 用于快速获取玩家当前所在副本的地图ID。
         */
        uint32 GetDungeonMapId(ObjectGuid guid);

        /**
         * @brief 获取当前队伍剩余踢人次数
         * @param gguid 队伍GUID
         * @return 剩余踢人次数(0-3)
         *
         * 每个LFG队伍有有限的踢人次数(通常为3次)。
         * 每次成功踢人后次数减1,次数耗尽后不能再踢人。
         *
         * @note 踢人次数在副本重置时恢复
         */
        uint8 GetKicksLeft(ObjectGuid gguid);

        /**
         * @brief 从数据库加载LFG队伍信息
         * @param fields 数据库字段数组
         * @param guid 玩家GUID
         *
         * 从数据库加载玩家的LFG队伍历史信息。
         * 主要用于玩家登录时恢复LFG状态。
         *
         * 加载的数据包括:
         * - 玩家所在队伍
         * - 副本ID
         * - 副本完成状态
         *
         * @note 该函数由Player::LoadFromDB调用
         */
        void _LoadFromDB(Field* fields, ObjectGuid guid);

        /**
         * @brief 从数据库加载队伍数据后初始化玩家数据
         * @param guid 玩家GUID
         * @param gguid 队伍GUID
         *
         * 在玩家加入队伍时调用,用于同步LFG数据。
         * 主要执行:
         * - 设置玩家的队伍GUID
         * - 复制队伍的LFG状态到玩家
         * - 更新队伍的玩家列表
         *
         * @note 该函数在玩家加入已存在的LFG队伍时调用
         */
        void SetupGroupMember(ObjectGuid guid, ObjectGuid gguid);

        /**
         * @brief 根据副本ID返回LFG副本入口ID
         * @param id 副本ID
         * @return LFG副本入口ID,如果找不到返回0
         *
         * 副本ID与入口ID的转换:
         * 入口ID = 副本ID + (类型 << 24)
         *
         * 用于将游戏内部ID转换为客户端显示ID。
         */
        uint32 GetLFGDungeonEntry(uint32 id);

        // ========== cs_lfg 命令使用的函数 ==========

        /**
         * @brief 获取当前玩家角色
         * @param guid 玩家GUID
         * @return 角色位掩码(坦克、治疗、输出的组合)
         *
         * 角色值可以是:
         * - PLAYER_ROLE_TANK: 坦克
         * - PLAYER_ROLE_HEALER: 治疗
         * - PLAYER_ROLE_DAMAGE: 输出
         * - PLAYER_ROLE_LEADER: 队长(可与其他角色组合)
         *
         * @note 玩家可以选择多个角色,如坦克+输出
         */
        uint8 GetRoles(ObjectGuid guid);

        /**
         * @brief 获取当前玩家备注
         * @param gguid 玩家或队伍GUID
         * @return 备注字符串的常量引用
         *
         * 备注主要用于团队查找器(LFR/Looking For Raid),
         * 玩家可以添加说明文字供其他玩家查看。
         *
         * @note 返回的引用在下次操作前有效
         */
        std::string const& GetComment(ObjectGuid gguid);

        /**
         * @brief 获取当前LFG选项
         * @return 选项位掩码
         *
         * 返回当前配置的LFG选项,包括:
         * - LFG_OPTION_ENABLE_DUNGEON_FINDER: 副本查找器开关
         * - LFG_OPTION_ENABLE_RAID_BROWSER: 团队浏览器开关
         *
         * @see isOptionEnabled 检查特定选项是否启用
         */
        uint32 GetOptions();

        /**
         * @brief 设置新的LFG选项
         * @param options 新的选项位掩码
         *
         * 用于动态修改LFG系统配置,通常由GM命令调用。
         * 设置后立即生效,影响新加入队列的玩家。
         *
         * @note 不会影响已经在队列中的玩家
         */
        void SetOptions(uint32 options);

        /**
         * @brief 检查指定的LFG选项是否启用
         * @param option 要检查的选项(单个选项位)
         * @return true表示启用,false表示禁用
         *
         * 示例:
         * @code
         * if (sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER))
         *     // 副本查找器已启用
         * @endcode
         */
        bool isOptionEnabled(uint32 option);

        /**
         * @brief 清空队列 - 仅用于内部测试
         *
         * 清空所有队列、提案和角色检查。
         * 主要用于GM命令测试或服务器维护。
         *
         * @warning 这会中断所有正在排队的玩家,谨慎使用
         */
        void Clean();

        /**
         * @brief 导出队列状态信息 - 仅用于内部测试
         * @param full 是否输出完整信息(默认为false)
         * @return 队列状态的字符串描述
         *
         * 生成队列状态的文本报告,用于GM查看或调试。
         * 包括:
         * - 各队伍类型队列中的玩家数量
         * - 平均等待时间
         * - 当前活跃的提案数量
         *
         * @param full=true时输出更详细的调试信息
         */
        std::string DumpQueueInfo(bool full = false);

        // ========== LFG脚本回调函数 ==========

        /**
         * @brief 获取队伍队长
         * @param guid 玩家或队伍GUID
         * @return 队长的GUID,如果不在队伍中返回空GUID
         *
         * 使用内部LFG数据查找队长,不依赖Group对象。
         * 主要用于LFG系统中需要快速访问队长的场景。
         */
        ObjectGuid GetLeader(ObjectGuid guid);

        /**
         * @brief 设置玩家阵营
         * @param guid 玩家GUID
         * @param team 阵营ID(ALLIANCE或HORDE)
         *
         * 更新玩家在LFG系统中的阵营信息。
         * 阵营用于限制跨阵营组队(除非特别配置)。
         *
         * 调用时机:
         * - 玩家登录时
         * - 玩家阵营变更时(罕见)
         */
        void SetTeam(ObjectGuid guid, uint8 team);

        /**
         * @brief 设置玩家所在队伍
         * @param guid 玩家GUID
         * @param group 队伍GUID(空表示离开队伍)
         *
         * 更新玩家在LFG系统中的队伍归属。
         * 当玩家加入或离开队伍时调用。
         *
         * 注意:
         * - 设置为空GUID时清除队伍信息
         * - 同时更新队伍的玩家列表
         */
        void SetGroup(ObjectGuid guid, ObjectGuid group);

        /**
         * @brief 获取玩家所在队伍
         * @param guid 玩家GUID
         * @return 队伍GUID,如果不在队伍中返回空GUID
         */
        ObjectGuid GetGroup(ObjectGuid guid);

        /**
         * @brief 设置队伍队长
         * @param gguid 队伍GUID
         * @param leader 新队长的玩家GUID
         *
         * 更新LFG系统中的队长信息。
         * 当队伍转让队长时调用。
         */
        void SetLeader(ObjectGuid gguid, ObjectGuid leader);

        /**
         * @brief 移除保存的队伍数据
         * @param guid 玩家GUID
         *
         * 当玩家彻底离开LFG系统时调用。
         * 清除玩家在队伍数据中的所有记录。
         *
         * @note 不影响实际Group对象,仅清理LFG数据
         */
        void RemoveGroupData(ObjectGuid guid);

        /**
         * @brief 从队伍中移除玩家
         * @param gguid 队伍GUID
         * @param guid 要移除的玩家GUID
         * @return 移除后队伍中剩余的玩家数量
         *
         * 从LFG队伍数据中移除指定玩家。
         * 如果队伍为空,会自动清理队伍数据。
         */
        uint8 RemovePlayerFromGroup(ObjectGuid gguid, ObjectGuid guid);

        /**
         * @brief 将玩家添加到队伍
         * @param gguid 队伍GUID
         * @param guid 要添加的玩家GUID
         *
         * 向LFG队伍数据中添加新玩家。
         * 自动初始化玩家的LFG数据。
         */
        void AddPlayerToGroup(ObjectGuid gguid, ObjectGuid guid);

        // ========== LFGHandler 数据包处理函数 ==========

        /**
         * @brief 获取锁定的副本映射
         * @param guid 玩家GUID
         * @return 锁定的副本映射(副本ID -> 锁定原因)
         *
         * 返回玩家因条件不满足而无法进入的副本列表。
         * 锁定原因包括:
         * - 等级不足或过高
         * - 缺少关键物品或任务
         * - 有逃亡者减益
         * - 冷却时间未到
         *
         * 用于客户端显示灰色副本并提示原因。
         */
        LfgLockMap const GetLockedDungeons(ObjectGuid guid);

        /**
         * @brief 返回当前LFG状态
         * @param guid 玩家GUID
         * @return LFG更新数据结构
         *
         * 获取玩家完整的LFG状态信息,用于客户端同步。
         * 包括:
         * - 当前状态(队列中、副本中等)
         * - 选择的副本集合
         * - 备注(用于LFR)
         */
        LfgUpdateData GetLfgStatus(ObjectGuid guid);

        /**
         * @brief 检查季节性副本是否激活
         * @param dungeonId 副本ID
         * @return true表示激活,false表示未激活
         *
         * 季节性副本仅在特定节日或活动期间开放。
         * 如:
         * - 仲夏火焰节副本
         * - 万圣节副本
         * - 春节副本
         *
         * 激活状态由游戏事件系统控制。
         */
        bool IsSeasonActive(uint32 dungeonId);

        /**
         * @brief 获取指定副本和玩家等级对应的随机副本奖励
         * @param dungeon 随机副本ID
         * @param level 玩家等级
         * @return 奖励数据指针,如果找不到返回nullptr
         *
         * 根据玩家等级查找对应的奖励配置。
         * 奖励数据包含:
         * - 首次完成奖励任务ID
         * - 非首次完成奖励任务ID
         *
         * @see LoadRewards 加载奖励数据
         */
        LfgReward const* GetRandomDungeonReward(uint32 dungeon, uint8 level);

        /**
         * @brief 返回指定等级和资料片的所有随机和季节性副本
         * @param level 玩家等级
         * @param expansion 资料片(0=经典,1=TBC,2=WLK)
         * @return 符合条件的副本集合
         *
         * 获取玩家可以看到并选择的随机副本列表。
         * 用于填充客户端的副本选择界面。
         */
        LfgDungeonSet GetRandomAndSeasonalDungeons(uint8 level, uint8 expansion);

        /**
         * @brief 传送玩家到/从选择的副本
         * @param player 玩家对象指针
         * @param out true表示传送出副本,false表示传送进副本
         * @param fromOpcode 是否来自客户端数据包(默认为false)
         *
         * 执行玩家传送逻辑:
         * - 进副本: 传送到副本入口
         * - 出副本: 传送到原始位置(离开副本前记录的位置)
         *
         * 传送前检查:
         * - 玩家是否死亡
         * - 是否在战斗中
         * - 是否在下落
         * - 是否在载具中
         * - 是否被魅惑
         *
         * 如果检查失败,向客户端发送错误消息。
         *
         * @note 传送是异步的,可能需要多个数据包完成
         */
        void TeleportPlayer(Player* player, bool out, bool fromOpcode = false);

        /**
         * @brief 初始化新的踢人提案
         * @param gguid 队伍GUID
         * @param kguid 发起踢人的玩家GUID(踢人者)
         * @param vguid 被踢玩家的GUID(受害者)
         * @param reason 踢人原因
         *
         * 创建一个新的投票踢人提案。
         * 主要步骤:
         * 1. 验证踢人条件(次数限制、冷却等)
         * 2. 创建踢人数据结构
         * 3. 向所有队员发送投票界面
         * 4. 启动超时计时器
         *
         * 限制条件:
         * - 队伍必须有剩余踢人次数
         * - 被踢玩家不能是队长
         * - 不能在另一个踢人投票进行中
         * - 发起者必须在副本中
         *
         * @see UpdateBoot 更新投票结果
         */
        void InitBoot(ObjectGuid gguid, ObjectGuid kguid, ObjectGuid vguid, std::string const& reason);

        /**
         * @brief 更新踢人提案,记录玩家的投票
         * @param guid 投票玩家的GUID
         * @param accept true表示同意踢人,false表示反对
         *
         * 处理玩家的投票并检查是否达到通过条件。
         * 投票结果:
         * - 通过: 需要3票同意(LFG_GROUP_KICK_VOTES_NEEDED)
         * - 否决: 任何一票反对
         * - 超时: 未在规定时间内完成投票
         *
         * 通过后执行:
         * 1. 将被踢玩家移出副本
         * 2. 减少队伍踢人次数
         * 3. 清理踢人数据
         */
        void UpdateBoot(ObjectGuid guid, bool accept);

        /**
         * @brief 更新加入副本的提案,记录玩家的选择
         * @param proposalId 提案ID
         * @param guid 玩家GUID
         * @param accept true表示接受提案,false表示拒绝
         *
         * 处理玩家对副本提案的确认。
         * 提案生命周期:
         * 1. 系统找到合适组合后创建提案
         * 2. 向所有玩家发送提案确认界面
         * 3. 玩家选择接受或拒绝
         * 4. 所有玩家接受后创建队伍并传送
         *
         * 提案失败情况:
         * - 任一玩家拒绝
         * - 超时未响应(LFG_TIME_PROPOSAL)
         * - 玩家断线
         *
         * 失败后处理:
         * - 拒绝者被移出队列
         * - 接受者重新加入队列
         */
        void UpdateProposal(uint32 proposalId, ObjectGuid guid, bool accept);

        /**
         * @brief 更新角色检查,记录玩家的角色选择
         * @param gguid 队伍GUID
         * @param guid 玩家GUID(默认为空,用于超时检查)
         * @param roles 选择的角色位掩码(默认为无)
         *
         * 处理玩家在角色检查阶段的角色选择。
         * 角色检查流程:
         * 1. 队伍申请加入LFG队列
         * 2. 向所有队员发送角色选择界面
         * 3. 玩家选择角色(坦克/治疗/输出)
         * 4. 检查角色组合是否合理
         * 5. 通过后正式加入队列
         *
         * 检查条件:
         * - 所有玩家都已选择角色
         * - 角色组合可以完成副本(至少1坦克1治疗)
         * - 在超时时间内完成(LFG_TIME_ROLECHECK)
         *
         * 失败原因:
         * - 超时
         * - 角色组合不合理
         * - 玩家离队
         */
        void UpdateRoleCheck(ObjectGuid gguid, ObjectGuid guid = ObjectGuid::Empty, uint8 roles = PLAYER_ROLE_NONE);

        /**
         * @brief 设置玩家LFG角色
         * @param guid 玩家GUID
         * @param roles 角色位掩码
         *
         * 更新玩家在LFG系统中的角色偏好。
         * 可以设置多个角色(如坦克+输出)。
         */
        void SetRoles(ObjectGuid guid, uint8 roles);

        /**
         * @brief 设置玩家LFR备注
         * @param guid 玩家GUID
         * @param comment 备注文本
         *
         * 设置团队查找器的备注说明。
         * 其他玩家可以在浏览团队时看到此备注。
         */
        void SetComment(ObjectGuid guid, std::string const& comment);

        /**
         * @brief 使用选择的角色、副本和备注加入LFG
         * @param player 玩家对象指针
         * @param roles 角色位掩码
         * @param dungeons 要加入的副本集合
         * @param comment 备注(用于LFR)
         *
         * 玩家或队伍加入LFG队列的主入口函数。
         * 执行步骤:
         * 1. 验证玩家状态(等级、装备、减益等)
         * 2. 检查副本要求
         * 3. 如果是队伍,启动角色检查
         * 4. 如果是单人,直接加入队列
         *
         * 验证包括:
         * - 等级范围
         * - 装备等级(英雄副本)
         * - 逃亡者减益
         * - 随机副本冷却
         * - 是否已在队列中
         *
         * @see LeaveLfg 离开LFG队列
         */
        void JoinLfg(Player* player, uint8 roles, LfgDungeonSet& dungeons, std::string const& comment);

        /**
         * @brief 离开LFG
         * @param guid 玩家或队伍GUID
         * @param disconnected 是否因断线离开(默认为false)
         *
         * 将玩家或队伍从LFG系统中移除。
         * 清理步骤:
         * 1. 从队列中移除
         * 2. 取消进行中的提案
         * 3. 清理角色检查
         * 4. 重置LFG状态
         *
         * @note 断线玩家的处理方式不同,会保留部分数据用于重连
         */
        void LeaveLfg(ObjectGuid guid, bool disconnected = false);

        // ========== LfgQueue 队列管理函数 ==========

        /**
         * @brief 获取上一次LFG状态
         * @param guid 玩家或队伍GUID
         * @return 上一次的LFG状态
         *
         * 用于跟踪副本完成前的状态。
         * 主要用于:
         * - 副本完成后恢复到之前的状态
         * - 判断玩家是否可以从新排队
         */
        LfgState GetOldState(ObjectGuid guid);

        /**
         * @brief 检查指定队伍GUID是否为LFG队伍
         * @param guid 队伍GUID
         * @return true表示LFG队伍,false表示普通队伍
         *
         * LFG队伍由系统创建,具有特殊属性:
         * - 自动分配角色
         * - 有传送功能
         * - 有投票踢人限制
         * - 有随机副本奖励
         */
        bool IsLfgGroup(ObjectGuid guid);

        /**
         * @brief 获取指定队伍的玩家数量
         * @param guid 队伍GUID
         * @return 队伍中的玩家数量(0-5)
         */
        uint8 GetPlayerCount(ObjectGuid guid);

        /**
         * @brief 添加新提案,返回提案ID
         * @param proposal 提案数据结构(会被修改以设置ID)
         * @return 新提案的唯一ID
         *
         * 将新创建的提案添加到提案容器中。
         * 自动分配唯一ID并设置时间戳。
         *
         * @note 提案ID用于后续更新和查询
         */
        uint32 AddProposal(LfgProposal& proposal);

        /**
         * @brief 检查所有玩家是否都在队列中
         * @param check 要检查的玩家GUID列表
         * @return true表示全部在队列中,false表示有玩家不在
         *
         * 用于提案创建前验证所有玩家的队列状态。
         */
        bool AllQueued(GuidList const& check);

        /**
         * @brief 检查给定角色是否匹配,并修改角色映射为新角色
         * @param groles 角色映射(玩家GUID -> 角色),会被修改
         * @return true表示角色组合合理,false表示不合理
         *
         * 验证并调整队伍角色分配:
         * - 检查是否有足够的坦克(至少1个)
         * - 检查是否有足够的治疗(至少1个)
         * - 自动调整多角色玩家的角色分配
         *
         * 示例:
         * - 玩家A选择了坦克+输出,队伍缺坦克,则分配为坦克
         * - 玩家B选择了治疗+输出,队伍缺治疗,则分配为治疗
         *
         * @note 该函数是静态方法,不依赖实例数据
         */
        static bool CheckGroupRoles(LfgRolesMap &groles);

        /**
         * @brief 检查指定玩家是否互相屏蔽
         * @param guid1 第一个玩家的GUID
         * @param guid2 第二个玩家的GUID
         * @return true表示互相屏蔽,false表示可以互相看到
         *
         * 用于提案匹配时过滤屏蔽列表中的玩家。
         * 如果A屏蔽了B,则不会匹配到同一队伍。
         *
         * @note 该函数是静态方法,检查双方的屏蔽列表
         */
        static bool HasIgnore(ObjectGuid guid1, ObjectGuid guid2);

        /**
         * @brief 向玩家发送队列状态
         * @param guid 玩家GUID
         * @param data 队列状态数据
         *
         * 向客户端发送SMSG_LFG_QUEUE_STATUS数据包。
         * 包含:
         * - 预估等待时间
         * - 平均等待时间
         * - 各角色等待时间
         * - 队列中各角色数量
         *
         * @note 该函数是静态方法,直接发送网络数据包
         */
        static void SendLfgQueueStatus(ObjectGuid guid, LfgQueueStatusData const& data);

    private:
        // ========== 私有成员函数 ==========

        /**
         * @brief 获取玩家阵营
         * @param guid 玩家GUID
         * @return 阵营ID(ALLIANCE、HORDE或NEUTRAL)
         *
         * 从玩家数据中获取阵营,不依赖Player对象。
         */
        uint8 GetTeam(ObjectGuid guid);

        /**
         * @brief 根据职业过滤角色选择
         * @param player 玩家对象指针
         * @param roles 玩家选择的角色位掩码
         * @return 过滤后的角色位掩码
         *
         * 根据玩家职业移除不可用的角色选项。
         * 例如:
         * - 法师只能选择输出
         * - 战士可以选择坦克或输出
         * - 圣骑士可以选择坦克、治疗或输出
         *
         * @see LfgRoleClasses 职业角色能力定义
         */
        uint8 FilterClassRoles(Player* player, uint8 roles);

        /**
         * @brief 恢复之前的状态
         * @param guid 玩家或队伍GUID
         * @param debugMsg 调试消息(用于日志记录)
         *
         * 将LFG状态恢复到之前保存的状态。
         * 主要用于:
         * - 提案失败后恢复队列状态
         * - 副本完成后恢复
         */
        void RestoreState(ObjectGuid guid, char const* debugMsg);

        /**
         * @brief 清除状态
         * @param guid 玩家或队伍GUID
         * @param debugMsg 调试消息(用于日志记录)
         *
         * 清除所有LFG相关状态,重置为初始状态。
         */
        void ClearState(ObjectGuid guid, char const* debugMsg);

        /**
         * @brief 设置副本ID
         * @param guid 玩家或队伍GUID
         * @param dungeon 副本ID
         *
         * 更新当前进入的副本ID。
         */
        void SetDungeon(ObjectGuid guid, uint32 dungeon);

        /**
         * @brief 设置选择的副本集合
         * @param guid 玩家或队伍GUID
         * @param dungeons 副本集合
         *
         * 保存玩家选择申请的副本列表。
         */
        void SetSelectedDungeons(ObjectGuid guid, LfgDungeonSet const& dungeons);

        /**
         * @brief 减少剩余踢人次数
         * @param guid 队伍GUID
         *
         * 成功踢人后调用,将踢人次数减1。
         */
        void DecreaseKicksLeft(ObjectGuid guid);

        /**
         * @brief 设置LFG状态
         * @param guid 玩家或队伍GUID
         * @param state 新的LFG状态
         *
         * 更新LFG状态并触发相应的状态转换逻辑。
         */
        void SetState(ObjectGuid guid, LfgState state);

        /**
         * @brief 设置投票踢人状态
         * @param gguid 队伍GUID
         * @param active true表示踢人投票激活,false表示结束
         *
         * 更新队伍的踢人投票状态。
         */
        void SetVoteKick(ObjectGuid gguid, bool active);

        /**
         * @brief 移除玩家数据
         * @param guid 玩家GUID
         *
         * 完全移除玩家的LFG数据。
         * 通常在玩家离线或离开LFG系统时调用。
         */
        void RemovePlayerData(ObjectGuid guid);

        /**
         * @brief 获取兼容的副本集合
         * @param dungeons 输出参数,兼容的副本集合
         * @param players 玩家集合
         * @param lockMap 输出参数,锁定原因映射
         * @param isContinue 是否为继续排队(非首次加入)
         *
         * 计算所有玩家都满足条件的副本列表。
         * 用于队伍加入队列前的验证。
         */
        void GetCompatibleDungeons(LfgDungeonSet& dungeons, GuidSet const& players, LfgLockPartyMap& lockMap, bool isContinue);

        /**
         * @brief 保存到数据库
         * @param guid 玩家GUID
         * @param db_guid 数据库ID
         *
         * 将玩家的LFG数据保存到数据库。
         * 用于持久化LFG状态,支持玩家重连。
         */
        void _SaveToDB(ObjectGuid guid, uint32 db_guid);

        /**
         * @brief 获取LFG副本数据
         * @param id 副本ID
         * @return 副本数据指针,如果找不到返回nullptr
         */
        LFGDungeonData const* GetLFGDungeon(uint32 id);

        // ========== 提案相关函数 ==========

        /**
         * @brief 移除提案
         * @param itProposal 提案容器的迭代器
         * @param type 移除类型(用于通知客户端)
         *
         * 从提案容器中移除提案并通知所有相关玩家。
         * 根据type发送不同的更新消息:
         * - LFG_UPDATETYPE_PROPOSAL_FAILED: 提案失败
         * - LFG_UPDATETYPE_PROPOSAL_DECLINED: 玩家拒绝
         */
        void RemoveProposal(LfgProposalContainer::iterator itProposal, LfgUpdateType type);

        /**
         * @brief 创建新队伍
         * @param proposal 提案数据
         *
         * 根据提案创建新的游戏队伍。
         * 主要步骤:
         * 1. 创建Group对象
         * 2. 将所有玩家添加到队伍
         * 3. 设置队长
         * 4. 分配角色
         * 5. 发送提案接受通知
         * 6. 启动传送流程
         *
         * @note 如果提案中已有队伍,则复用该队伍
         */
        void MakeNewGroup(LfgProposal const& proposal);

        // ========== 通用函数 ==========

        /**
         * @brief 获取指定GUID对应的队列
         * @param guid 玩家或队伍GUID
         * @return 队列对象的引用
         *
         * 根据GUID的队伍类型返回对应的队列。
         * 不同队伍类型的队列是独立的。
         *
         * @note 返回的引用始终有效,队列自动创建
         */
        LFGQueue &GetQueue(ObjectGuid guid);

        /**
         * @brief 根据随机副本ID获取对应的副本集合
         * @param randomdungeon 随机副本ID
         * @return 具体副本集合的常量引用
         *
         * 将随机副本展开为具体可匹配的副本列表。
         * 例如:随机诺森德副本包含所有符合条件的诺森德副本。
         */
        LfgDungeonSet const& GetDungeonsByRandom(uint32 randomdungeon);

        /**
         * @brief 获取副本类型
         * @param dungeon 副本ID
         * @return 副本类型枚举值
         *
         * 返回副本的分类类型:
         * - LFG_TYPE_DUNGEON: 普通地下城
         * - LFG_TYPE_RAID: 团队副本
         * - LFG_TYPE_HEROIC: 英雄副本
         * - LFG_TYPE_RANDOM: 随机副本
         */
        LfgType GetDungeonType(uint32 dungeon);

        // ========== 发送数据包函数 ==========

        /**
         * @brief 发送踢人提案更新
         * @param guid 接收消息的玩家GUID
         * @param boot 踢人提案数据
         *
         * 向玩家发送SMSG_LFG_BOOT_PLAYER数据包。
         * 显示踢人投票界面,包含:
         * - 发起者、被踢者信息
         * - 踢人原因
         * - 投票进度
         * - 剩余时间
         */
        void SendLfgBootProposalUpdate(ObjectGuid guid, LfgPlayerBoot const& boot);

        /**
         * @brief 发送加入结果
         * @param guid 接收消息的玩家GUID
         * @param data 加入结果数据
         *
         * 向玩家发送SMSG_LFG_JOIN_RESULT数据包。
         * 通知加入队列的结果:
         * - 成功: 显示队列界面
         * - 失败: 显示失败原因
         */
        void SendLfgJoinResult(ObjectGuid guid, LfgJoinResultData const& data);

        /**
         * @brief 发送角色选择
         * @param guid 接收消息的玩家GUID
         * @param pguid 选择角色的玩家GUID
         * @param roles 选择的角色
         *
         * 向队伍广播SMSG_LFG_ROLE_CHOSEN数据包。
         * 通知队伍成员某玩家已选择角色。
         */
        void SendLfgRoleChosen(ObjectGuid guid, ObjectGuid pguid, uint8 roles);

        /**
         * @brief 发送角色检查更新
         * @param guid 接收消息的玩家GUID
         * @param roleCheck 角色检查数据
         *
         * 向玩家发送SMSG_LFG_ROLE_CHECK_UPDATE数据包。
         * 显示角色检查界面,包含:
         * - 各队员的角色选择
         * - 检查状态
         * - 剩余时间
         */
        void SendLfgRoleCheckUpdate(ObjectGuid guid, LfgRoleCheck const& roleCheck);

        /**
         * @brief 发送队伍LFG更新
         * @param guid 接收消息的玩家GUID
         * @param data 更新数据
         *
         * 向队伍发送SMSG_LFG_UPDATE_PARTY数据包。
         * 通知队伍LFG状态变化。
         */
        void SendLfgUpdateParty(ObjectGuid guid, LfgUpdateData const& data);

        /**
         * @brief 发送玩家LFG更新
         * @param guid 接收消息的玩家GUID
         * @param data 更新数据
         *
         * 向玩家发送SMSG_LFG_UPDATE_PLAYER数据包。
         * 通知单人LFG状态变化。
         */
        void SendLfgUpdatePlayer(ObjectGuid guid, LfgUpdateData const& data);

        /**
         * @brief 发送提案更新
         * @param guid 接收消息的玩家GUID
         * @param proposal 提案数据
         *
         * 向玩家发送SMSG_LFG_PROPOSAL_UPDATE数据包。
         * 显示提案确认界面,包含:
         * - 副本信息
         * - 各玩家接受状态
         * - 剩余时间
         */
        void SendLfgUpdateProposal(ObjectGuid guid, LfgProposal const& proposal);

        /**
         * @brief 获取队伍中的玩家集合
         * @param guid 队伍GUID
         * @return 玩家GUID集合的常量引用
         */
        GuidSet const& GetPlayers(ObjectGuid guid);

        // ========== 成员变量 ==========

        // 通用变量
        uint32 m_QueueTimer;                               ///< 队列更新计时器(毫秒) - 用于控制队列更新频率,达到LFG_QUEUEUPDATE_INTERVAL时触发更新
        uint32 m_lfgProposalId;                            ///< 提案ID计数器 - 用于生成唯一的提案ID,每次创建新提案时递增
        uint32 m_options;                                  ///< LFG选项标志位 - 存储当前启用的LFG功能(副本查找器、团队浏览器等)

        LfgQueueContainer QueuesStore;                     ///< 队列存储容器 - 按队伍类型索引存储队列对象,不同队伍类型的队列独立管理
        LfgCachedDungeonContainer CachedDungeonMapStore;   ///< 缓存的副本映射容器 - 按队伍类型索引存储所有可用副本,避免重复计算

        // 奖励系统
        LfgRewardContainer RewardMapStore;                 ///< 随机副本奖励容器 - 按等级索引存储奖励数据,支持快速查找指定等级的奖励
        LFGDungeonContainer LfgDungeonStore;               ///< LFG副本数据容器 - 按副本ID索引存储副本详细信息(名称、地图、坐标等)

        // 角色检查、提案、投票踢人
        LfgRoleCheckContainer RoleChecksStore;             ///< 角色检查容器 - 存储所有正在进行中的角色检查,按队伍GUID索引
        LfgProposalContainer ProposalsStore;               ///< 提案容器 - 存储所有正在等待确认的提案,按提案ID索引
        LfgPlayerBootContainer BootsStore;                 ///< 投票踢人容器 - 存储所有正在进行中的踢人投票,按队伍GUID索引

        // 玩家和队伍数据
        LfgPlayerDataContainer PlayersStore;               ///< 玩家数据容器 - 存储所有使用过LFG的玩家数据,按玩家GUID索引
        LfgGroupDataContainer GroupsStore;                 ///< 队伍数据容器 - 存储所有LFG队伍的数据,按队伍GUID索引
};

} // namespace lfg

#define sLFGMgr lfg::LFGMgr::instance()
#endif
