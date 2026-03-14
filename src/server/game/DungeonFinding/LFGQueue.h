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
 * @file LFGQueue.h
 * @brief LFG队列系统 - 负责玩家匹配逻辑
 *
 * 本文件实现了LFG系统的核心匹配队列，主要功能包括：
 * - 管理等待匹配的玩家和队伍队列
 * - 检查玩家/队伍之间的兼容性
 * - 自动组成符合要求的队伍（1坦克+1治疗+3输出）
 * - 统计和更新排队等待时间
 * - 缓存兼容性检查结果以提高性能
 *
 * 核心算法：
 * - 使用回溯算法尝试不同的队伍组合
 * - 缓存兼容性结果避免重复计算
 * - 优先处理新加入队列的玩家
 *
 * 性能优化：
 * - 兼容性结果缓存
 * - 分层队列管理（新队列、当前队列）
 * - 最佳兼容组合缓存
 */

#ifndef _LFGQUEUE_H
#define _LFGQUEUE_H

#include "LFG.h"

namespace lfg
{

/**
 * @brief 兼容性检查结果枚举
 *
 * 定义了玩家/队伍组合的兼容性状态
 * 枚举值小于LFG_COMPATIBLES_WITH_LESS_PLAYERS表示不兼容
 */
enum LfgCompatibility
{
    LFG_COMPATIBILITY_PENDING,                              // 等待检查（尚未缓存）
    LFG_INCOMPATIBLES_WRONG_GROUP_SIZE,                     // 队伍大小错误
    LFG_INCOMPATIBLES_TOO_MUCH_PLAYERS,                     // 玩家数量过多
    LFG_INCOMPATIBLES_MULTIPLE_LFG_GROUPS,                  // 多个LFG队伍（不允许合并两个LFG队伍）
    LFG_INCOMPATIBLES_HAS_IGNORES,                          // 存在互相屏蔽的玩家
    LFG_INCOMPATIBLES_NO_ROLES,                             // 角色不兼容（无法组成有效队伍）
    LFG_INCOMPATIBLES_NO_DUNGEONS,                          // 没有共同的副本选择
    LFG_COMPATIBLES_WITH_LESS_PLAYERS,                      // 兼容但人数不足（此值以下的枚举表示不兼容，不要修改顺序）
    LFG_COMPATIBLES_BAD_STATES,                             // 兼容但某些玩家状态异常
    LFG_COMPATIBLES_MATCH                                   // 完美匹配（必须是最后一个）
};

/**
 * @brief 兼容性数据结构
 *
 * 存储玩家组合的兼容性检查结果，包括兼容性状态和角色分配
 */
struct LfgCompatibilityData
{
    LfgCompatibilityData(): compatibility(LFG_COMPATIBILITY_PENDING) { }
    LfgCompatibilityData(LfgCompatibility _compatibility): compatibility(_compatibility) { }
    LfgCompatibilityData(LfgCompatibility _compatibility, LfgRolesMap const& _roles):
        compatibility(_compatibility), roles(_roles) { }

    LfgCompatibility compatibility;    // 兼容性状态
    LfgRolesMap roles;                 // 角色分配映射（玩家GUID -> 角色）
};

/**
 * @brief 队列数据结构
 *
 * 存储玩家或队伍在队列中的所有相关信息
 */
struct LfgQueueData
{
    LfgQueueData();

    LfgQueueData(time_t _joinTime, LfgDungeonSet const& _dungeons, LfgRolesMap const& _roles):
        joinTime(_joinTime), tanks(LFG_TANKS_NEEDED), healers(LFG_HEALERS_NEEDED),
        dps(LFG_DPS_NEEDED), dungeons(_dungeons), roles(_roles)
        { }

    time_t joinTime;                   // 加入队列的时间戳（用于计算等待时间）
    uint8 tanks;                       // 仍需要的坦克数量
    uint8 healers;                     // 仍需要的治疗数量
    uint8 dps;                         // 仍需要的输出数量
    LfgDungeonSet dungeons;            // 选择的副本集合
    LfgRolesMap roles;                 // 玩家角色映射
    std::string bestCompatible;        // 最佳兼容组合的GUID字符串（用于显示排队进度）
};

/**
 * @brief 等待时间统计结构
 *
 * 用于统计特定副本的平均等待时间
 */
struct LfgWaitTime
{
    LfgWaitTime(): time(-1), number(0) { }
    int32 time;                        // 平均等待时间（秒），-1表示无数据
    uint32 number;                     // 参与统计的玩家数量
};

// 类型定义
typedef std::map<uint32, LfgWaitTime> LfgWaitTimesContainer;              // 等待时间统计容器（副本ID -> 等待时间）
typedef std::map<std::string, LfgCompatibilityData> LfgCompatibleContainer;  // 兼容性缓存容器（GUID组合字符串 -> 兼容性数据）
typedef std::map<ObjectGuid, LfgQueueData> LfgQueueDataContainer;            // 队列数据容器（GUID -> 队列数据）

/**
 * @brief LFG队列类 - 负责玩家匹配和队伍组成
 *
 * 该类实现了LFG系统的核心匹配逻辑，主要职责包括：
 * - 管理排队中的玩家和队伍
 * - 检查玩家/队伍之间的兼容性
 * - 自动组成符合要求的队伍
 * - 统计和更新等待时间
 * - 向玩家发送队列状态更新
 *
 * 工作流程：
 * 1. 玩家/队伍加入队列（AddToQueue）
 * 2. 定期更新队列状态（UpdateQueueTimers）
 * 3. 尝试匹配队伍（FindGroups）
 * 4. 匹配成功后创建提案（LFGMgr处理）
 *
 * 性能考虑：
 * - 兼容性结果会被缓存以避免重复计算
 * - 使用分层队列（新队列、当前队列）优化匹配效率
 * - 定期清理无效的队列数据
 */
class TC_GAME_API LFGQueue
{
    public:

        // ========== 队列管理函数 ==========

        /**
         * @brief 获取匹配玩家的详细角色信息
         * @param check 玩家GUID列表
         * @return 格式化的字符串，包含每个玩家的角色信息
         */
        std::string GetDetailedMatchRoles(GuidList const& check) const;

        /**
         * @brief 将玩家或队伍添加到队列
         * @param guid 玩家或队伍的GUID
         * @param reAdd 是否为重新加入（true则添加到队列前端）
         */
        void AddToQueue(ObjectGuid guid, bool reAdd = false);

        /**
         * @brief 从队列中移除玩家或队伍
         * @param guid 要移除的GUID
         */
        void RemoveFromQueue(ObjectGuid guid);

        /**
         * @brief 添加队列数据
         * @param guid 玩家或队伍的GUID
         * @param joinTime 加入时间
         * @param dungeons 选择的副本集合
         * @param rolesMap 角色映射
         */
        void AddQueueData(ObjectGuid guid, time_t joinTime, LfgDungeonSet const& dungeons, LfgRolesMap const& rolesMap);

        /**
         * @brief 移除队列数据
         * @param guid 要移除的GUID
         */
        void RemoveQueueData(ObjectGuid guid);

        // ========== 等待时间更新函数 ==========

        /**
         * @brief 更新平均等待时间
         * @param waitTime 本次等待时间（秒）
         * @param dungeonId 副本ID
         */
        void UpdateWaitTimeAvg(int32 waitTime, uint32 dungeonId);

        /**
         * @brief 更新坦克等待时间
         * @param waitTime 本次等待时间（秒）
         * @param dungeonId 副本ID
         */
        void UpdateWaitTimeTank(int32 waitTime, uint32 dungeonId);

        /**
         * @brief 更新治疗等待时间
         * @param waitTime 本次等待时间（秒）
         * @param dungeonId 副本ID
         */
        void UpdateWaitTimeHealer(int32 waitTime, uint32 dungeonId);

        /**
         * @brief 更新输出等待时间
         * @param waitTime 本次等待时间（秒）
         * @param dungeonId 副本ID
         */
        void UpdateWaitTimeDps(int32 waitTime, uint32 dungeonId);

        // ========== 队列定时器更新 ==========

        /**
         * @brief 更新队列定时器，向所有排队玩家发送状态更新
         * @param currTime 当前时间戳
         */
        void UpdateQueueTimers(time_t currTime);

        /**
         * @brief 获取指定GUID的加入时间
         * @param guid 玩家或队伍的GUID
         * @return 加入队列的时间戳
         */
        time_t GetJoinTime(ObjectGuid guid);

        // ========== 队伍匹配函数 ==========

        /**
         * @brief 尝试组成新的队伍
         * @return 成功创建的提案数量
         *
         * @details 遍历新队列中的所有玩家/队伍，尝试与当前队列中的其他玩家/队伍匹配
         */
        uint8 FindGroups();

        // ========== 调试函数 ==========

        /**
         * @brief 导出队列状态信息（用于调试）
         * @return 队列状态字符串
         */
        std::string DumpQueueInfo() const;

        /**
         * @brief 导出兼容性缓存信息（用于调试）
         * @param full 是否输出完整信息
         * @return 兼容性缓存字符串
         */
        std::string DumpCompatibleInfo(bool full = false) const;

    private:
        // ========== 内部辅助函数 ==========

        void SetQueueUpdateData(std::string const& strGuids, LfgRolesMap const& proposalRoles);

        void AddToNewQueue(ObjectGuid guid);
        void AddToCurrentQueue(ObjectGuid guid);
        void AddToFrontCurrentQueue(ObjectGuid guid);
        void RemoveFromNewQueue(ObjectGuid guid);
        void RemoveFromCurrentQueue(ObjectGuid guid);

        void SetCompatibles(std::string const& key, LfgCompatibility compatibles);
        LfgCompatibility GetCompatibles(std::string const& key);
        void RemoveFromCompatibles(ObjectGuid guid);

        void SetCompatibilityData(std::string const& key, LfgCompatibilityData const& compatibles);
        LfgCompatibilityData* GetCompatibilityData(std::string const& key);
        void FindBestCompatibleInQueue(LfgQueueDataContainer::iterator itrQueue);
        void UpdateBestCompatibleInQueue(LfgQueueDataContainer::iterator itrQueue, std::string const& key, LfgRolesMap const& roles);

        /**
         * @brief 递归查找匹配的队伍组合
         * @param check 当前检查的GUID列表
         * @param all 剩余可用的GUID列表
         * @return 兼容性状态
         */
        LfgCompatibility FindNewGroups(GuidList& check, GuidList& all);

        /**
         * @brief 检查GUID列表的兼容性
         * @param check 要检查的GUID列表
         * @return 兼容性状态
         */
        LfgCompatibility CheckCompatibility(GuidList check);

        // ========== 成员变量 ==========

        // 队列数据
        LfgQueueDataContainer QueueDataStore;              // 队列数据存储（GUID -> 队列数据）
        LfgCompatibleContainer CompatibleMapStore;         // 兼容性缓存（GUID组合字符串 -> 兼容性数据）

        // 等待时间统计
        LfgWaitTimesContainer waitTimesAvgStore;           // 平均等待时间（多角色）
        LfgWaitTimesContainer waitTimesTankStore;          // 坦克等待时间
        LfgWaitTimesContainer waitTimesHealerStore;        // 治疗等待时间
        LfgWaitTimesContainer waitTimesDpsStore;           // 输出等待时间

        // 队列列表
        GuidList currentQueueStore;                        // 当前队列（有序列表，用于匹配）
        GuidList newToQueueStore;                          // 新加入队列（等待处理）
};

} // namespace lfg

#endif
