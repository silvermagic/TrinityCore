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
 * @file ReputationMgr.h
 * @brief 声望管理器头文件
 *
 * 本文件定义了玩家的声望管理系统，负责：
 * - 管理玩家与各个阵营的声望关系
 * - 处理声望的增减、可见性、战争状态等
 * - 维护声望等级统计（尊敬、崇敬、崇拜等）
 * - 处理声望溢出（spillover）机制
 * - 管理强制声望反应
 * - 与数据库交互保存和加载声望数据
 *
 * 声望系统是玩家与游戏世界中各阵营关系的重要组成部分，
 * 影响NPC对玩家的态度、可接取的任务、可购买的商品等。
 */

#ifndef __TRINITY_REPUTATION_MGR_H
#define __TRINITY_REPUTATION_MGR_H

#include "Common.h"
#include "SharedDefines.h"
#include "Language.h"
#include "DBCStructure.h"
#include "QueryResult.h"
#include <map>

/**
 * @brief 声望等级对应的本地化字符串索引数组
 *
 * 该数组将声望等级枚举映射到对应的语言字符串ID，
 * 用于在客户端显示声望等级名称（如仇恨、敌对、友善、崇拜等）
 */
static uint32 ReputationRankStrIndex[MAX_REPUTATION_RANK] =
{
    LANG_REP_HATED,    LANG_REP_HOSTILE, LANG_REP_UNFRIENDLY, LANG_REP_NEUTRAL,
    LANG_REP_FRIENDLY, LANG_REP_HONORED, LANG_REP_REVERED,    LANG_REP_EXALTED
};

/**
 * @brief 阵营标志位枚举
 *
 * 定义了阵营状态的各种标志，用于控制阵营在客户端的显示和行为
 */
enum FactionFlags
{
    FACTION_FLAG_NONE               = 0x00,                 // 无标志
    FACTION_FLAG_VISIBLE            = 0x01,                 // 使阵营在客户端可见（在与该阵营目标交互时设置或可设置）
    FACTION_FLAG_AT_WAR             = 0x02,                 // 启用客户端的"交战"按钮。玩家可控（除对立阵营始终交战外），标志仅在初始创建时设置
    FACTION_FLAG_HIDDEN             = 0x04,                 // 在客户端声望面板中隐藏阵营（玩家可以获得声望，但不发送更新给客户端）
    FACTION_FLAG_INVISIBLE_FORCED   = 0x08,                 // 强制覆盖FACTION_FLAG_VISIBLE并隐藏阵营，用于隐藏对立阵营
    FACTION_FLAG_PEACE_FORCED       = 0x10,                 // 强制覆盖FACTION_FLAG_AT_WAR，用于防止与己方阵营开战
    FACTION_FLAG_INACTIVE           = 0x20,                 // 玩家可控，状态存储在characters.data中（CMSG_SET_FACTION_INACTIVE）
    FACTION_FLAG_RIVAL              = 0x40,                 // 外域两个竞争阵营（奥尔多/占星者）的标志
    FACTION_FLAG_SPECIAL            = 0x80                  // 部落和联盟主城及其诺森德盟友拥有此标志
};

/**
 * @brief 声望列表ID类型
 *
 * 用于标识声望在列表中的位置索引
 */
typedef uint32 RepListID;

/**
 * @brief 阵营状态结构体
 *
 * 存储单个阵营的完整状态信息，包括声望值、标志位和同步状态
 */
struct FactionState
{
    uint32 ID;                  // 阵营ID（对应FactionEntry的ID）
    RepListID ReputationListID; // 声望列表ID（在客户端声望界面中的索引位置）
    int32 Standing;             // 当前声望值（相对于基础声望的偏移量）
    uint8 Flags;                // 阵营标志位（可见性、交战状态等，见FactionFlags枚举）
    bool needSend;              // 是否需要发送给客户端（用于优化网络同步）
    bool needSave;              // 是否需要保存到数据库（用于优化数据库写入）
};

/**
 * @brief 阵营状态列表类型
 *
 * 以声望列表ID为键，存储所有阵营的状态
 */
typedef std::map<RepListID, FactionState> FactionStateList;

/**
 * @brief 强制反应映射类型
 *
 * 用于临时强制指定某个阵营的声望等级（常用于脚本和特殊场景）
 */
typedef std::map<uint32, ReputationRank> ForcedReactions;

class Player;

/**
 * @brief 声望管理器类
 *
 * 负责管理玩家的所有声望相关功能，是玩家声望系统的核心类。
 *
 * 主要职责：
 * - 维护玩家与各阵营的声望关系
 * - 处理声望的增减和等级变化
 * - 管理声望的可见性和交战状态
 * - 处理声望溢出（spillover）到相关阵营
 * - 维护声望等级统计（用于成就系统）
 * - 与客户端同步声望状态
 * - 从数据库加载和保存声望数据
 *
 * 使用时机：
 * - 玩家登录时加载声望数据
 * - 击杀NPC、完成任务时修改声望
 * - 玩家切换交战状态时更新标志
 * - 玩家下线时保存声望数据
 */
class TC_GAME_API ReputationMgr
{
    public:                                                 // 构造函数和全局修改器
        /**
         * @brief 构造函数
         * @param owner 拥有此声望管理器的玩家对象
         *
         * 初始化声望管理器，设置所有计数器为0
         */
        explicit ReputationMgr(Player* owner) : _player(owner),
            _visibleFactionCount(0), _honoredFactionCount(0), _reveredFactionCount(0), _exaltedFactionCount(0), _sendFactionIncreased(false) { }

        /**
         * @brief 析构函数
         */
        ~ReputationMgr() { }

        /**
         * @brief 保存声望数据到数据库
         * @param trans 数据库事务对象
         *
         * 遍历所有需要保存的阵营，将其状态写入character_reputation表
         * 仅保存needSave标志为true的阵营，优化数据库写入性能
         *
         * 调用时机：玩家下线保存、定时保存等场景
         * 性能注意：使用事务批量提交，减少数据库往返次数
         */
        void SaveToDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 从数据库加载声望数据
         * @param result 数据库查询结果集
         *
         * 先调用Initialize()初始化默认声望，然后从数据库加载已保存的声望数据
         * 包括声望值、标志位等信息，并更新相关计数器
         *
         * 调用时机：玩家登录加载角色数据时
         */
        void LoadFromDB(PreparedQueryResult result);

    public:                                                 // 静态成员
        /**
         * @brief 各声望等级所需的点数
         *
         * 静态常量数组，定义了从仇恨到崇拜各等级所需的声望点数
         * 索引对应ReputationRank枚举值
         */
        static const int32 PointsInRank[MAX_REPUTATION_RANK];

        /**
         * @brief 声望上限
         *
         * 静态常量，定义声望的最大值为42999（崇拜上限）
         */
        static const int32 Reputation_Cap;

        /**
         * @brief 声望下限
         *
         * 静态常量，定义声望的最小值为-42000（仇恨下限）
         */
        static const int32 Reputation_Bottom;

        /**
         * @brief 将声望值转换为声望等级
         * @param standing 声望值
         * @return 对应的声望等级
         *
         * 根据声望值计算对应的等级（仇恨、敌对、中立、友善等）
         *
         * 性能注意：使用反向遍历和累减方式计算，避免多次乘法
         */
        static ReputationRank ReputationToRank(int32 standing);
    public:                                                 // 访问器
        /**
         * @brief 获取可见阵营数量
         * @return 可见阵营的数量
         */
        uint8 GetVisibleFactionCount() const { return _visibleFactionCount; }

        /**
         * @brief 获取尊敬等级及以上的阵营数量
         * @return 尊敬及以上阵营的数量
         */
        uint8 GetHonoredFactionCount() const { return _honoredFactionCount; }

        /**
         * @brief 获取崇敬等级及以上的阵营数量
         * @return 崇敬及以上阵营的数量
         */
        uint8 GetReveredFactionCount() const { return _reveredFactionCount; }

        /**
         * @brief 获取崇拜等级的阵营数量
         * @return 崇拜阵营的数量
         */
        uint8 GetExaltedFactionCount() const { return _exaltedFactionCount; }

        /**
         * @brief 获取阵营状态列表
         * @return 所有阵营状态的常量引用
         */
        FactionStateList const& GetStateList() const { return _factions; }

        /**
         * @brief 获取指定阵营的状态
         * @param factionEntry 阵营条目指针
         * @return 阵营状态指针，如果阵营不支持声望则返回nullptr
         */
        FactionState const* GetState(FactionEntry const* factionEntry) const
        {
            return factionEntry->CanHaveReputation() ? GetState(factionEntry->ReputationIndex) : nullptr;
        }

        /**
         * @brief 获取指定声望列表ID的状态
         * @param id 声望列表ID
         * @return 阵营状态指针，如果不存在则返回nullptr
         */
        FactionState const* GetState(RepListID id) const
        {
            FactionStateList::const_iterator repItr = _factions.find (id);
            return repItr != _factions.end() ? &repItr->second : nullptr;
        }

        /**
         * @brief 检查是否与指定阵营处于交战状态（通过阵营ID）
         * @param faction_id 阵营ID
         * @return 是否处于交战状态
         *
         * 根据阵营ID查询对应的阵营条目，然后检查交战标志
         */
        bool IsAtWar(uint32 faction_id) const;

        /**
         * @brief 检查是否与指定阵营处于交战状态（通过阵营条目）
         * @param factionEntry 阵营条目指针
         * @return 是否处于交战状态
         *
         * 直接检查阵营状态中的FACTION_FLAG_AT_WAR标志
         */
        bool IsAtWar(FactionEntry const* factionEntry) const;

        /**
         * @brief 检查声望是否允许用于指定队伍
         * @param team 队伍ID（联盟或部落）
         * @param factionId 阵营ID
         * @return 是否允许获得该阵营的声望
         *
         * 特殊处理：某些任务会给联盟和部落双方的阵营奖励声望，
         * 但DBC数据无法区分阵营专属声望，因此需要硬编码处理
         */
        bool IsReputationAllowedForTeam(TeamId team, uint32 factionId) const;

        /**
         * @brief 获取指定阵营的当前声望值（通过阵营ID）
         * @param faction_id 阵营ID
         * @return 当前声望值，如果阵营不存在则返回0
         */
        int32 GetReputation(uint32 faction_id) const;

        /**
         * @brief 获取指定阵营的当前声望值（通过阵营条目）
         * @param factionEntry 阵营条目指针
         * @return 当前声望值，如果阵营不存在则返回0
         *
         * 返回值 = 基础声望 + 当前声望偏移量
         */
        int32 GetReputation(FactionEntry const* factionEntry) const;

        /**
         * @brief 获取指定阵营的基础声望值
         * @param factionEntry 阵营条目指针
         * @return 基础声望值
         *
         * 根据玩家的种族和职业，从FactionEntry中查找对应的基础声望
         * FactionEntry中最多可以有4组不同的种族/职业组合及其对应的基础声望
         */
        int32 GetBaseReputation(FactionEntry const* factionEntry) const;

        /**
         * @brief 获取指定阵营的声望等级
         * @param factionEntry 阵营条目指针
         * @return 声望等级
         */
        ReputationRank GetRank(FactionEntry const* factionEntry) const;

        /**
         * @brief 获取指定阵营的基础声望等级
         * @param factionEntry 阵营条目指针
         * @return 基础声望等级（不考虑当前声望偏移）
         */
        ReputationRank GetBaseRank(FactionEntry const* factionEntry) const;

        /**
         * @brief 获取声望等级对应的本地化字符串索引
         * @param factionEntry 阵营条目指针
         * @return 语言字符串ID
         */
        uint32 GetReputationRankStrIndex(FactionEntry const* factionEntry) const
        {
            return ReputationRankStrIndex[GetRank(factionEntry)];
        };

        /**
         * @brief 获取强制声望等级（如果存在）
         * @param factionTemplateEntry 阵营模板条目指针
         * @return 强制声望等级指针，如果不存在则返回nullptr
         *
         * 用于获取通过脚本或其他机制临时强制设置的声望等级
         * 常见于特殊场景、任务或脚本控制的对立关系
         */
        ReputationRank const* GetForcedRankIfAny(FactionTemplateEntry const* factionTemplateEntry) const
        {
            ForcedReactions::const_iterator forceItr = _forcedReactions.find(factionTemplateEntry->Faction);
            return forceItr != _forcedReactions.end() ? &forceItr->second : nullptr;
        }

    public:                                                 // 修改器
        /**
         * @brief 设置指定阵营的声望值（绝对值）
         * @param factionEntry 阵营条目指针
         * @param standing 新的声望值
         * @return 是否成功修改
         *
         * 设置声望为指定的绝对值，会触发声望溢出机制
         */
        bool SetReputation(FactionEntry const* factionEntry, int32 standing)
        {
            return SetReputation(factionEntry, standing, false, false);
        }

        /**
         * @brief 修改指定阵营的声望值（增量值）
         * @param factionEntry 阵营条目指针
         * @param standing 声望增量（可为负数）
         * @param spillOverOnly 是否仅处理声望溢出（不修改主阵营声望）
         * @return 是否成功修改
         *
         * 增量方式修改声望，会应用服务器倍率，并触发声望溢出机制
         * spillOverOnly参数用于处理高等级奖励率时，仅溢出到关联阵营
         */
        bool ModifyReputation(FactionEntry const* factionEntry, int32 standing, bool spillOverOnly = false)
        {
            return SetReputation(factionEntry, standing, true, spillOverOnly);
        }

        /**
         * @brief 设置阵营可见（通过阵营模板条目）
         * @param factionTemplateEntry 阵营模板条目指针
         *
         * 将阵营设置为可见状态，但不会显示对立阵营的声望
         */
        void SetVisible(FactionTemplateEntry const* factionTemplateEntry);

        /**
         * @brief 设置阵营可见（通过阵营条目）
         * @param factionEntry 阵营条目指针
         *
         * 将阵营设置为可见状态
         */
        void SetVisible(FactionEntry const* factionEntry);

        /**
         * @brief 设置阵营的交战状态
         * @param repListID 声望列表ID
         * @param on true为开启交战，false为关闭交战
         *
         * 设置玩家是否与指定阵营处于交战状态
         */
        void SetAtWar(RepListID repListID, bool on);

        /**
         * @brief 设置阵营的未激活状态
         * @param repListID 声望列表ID
         * @param on true为设为未激活，false为激活
         *
         * 将阵营标记为未激活状态，客户端会折叠显示这些阵营
         */
        void SetInactive(RepListID repListID, bool on);

        /**
         * @brief 应用或移除强制声望反应
         * @param faction_id 阵营ID
         * @param rank 强制的声望等级
         * @param apply true为应用强制反应，false为移除
         *
         * 临时强制指定某个阵营的声望等级，不受正常声望规则限制
         * 常用于脚本控制特殊场景中的声望关系
         */
        void ApplyForceReaction(uint32 faction_id, ReputationRank rank, bool apply);

        /**
         * @brief 设置单个阵营的声望值
         * @param factionEntry 阵营条目指针
         * @param standing 声望值或增量
         * @param incremental true为增量模式，false为绝对值模式
         * @return 是否成功修改
         *
         * 直接设置单个阵营的声望，不处理声望溢出
         * 公开接口，主要用于GM命令直接修改声望
         */
        bool SetOneFactionReputation(FactionEntry const* factionEntry, int32 standing, bool incremental);

    public:                                                 // 发送器
        /**
         * @brief 发送初始声望列表给客户端
         *
         * 在玩家登录时发送完整的声望列表初始化包（SMSG_INITIALIZE_FACTIONS）
         * 包含所有阵营的标志位和声望值
         *
         * 调用时机：玩家登录完成，进入世界前
         */
        void SendInitialReputations();

        /**
         * @brief 发送强制声望反应给客户端
         *
         * 发送所有强制声望反应到客户端（SMSG_SET_FORCED_REACTIONS）
         * 强制反应会覆盖正常的声望计算结果
         *
         * 调用时机：有强制反应变更时
         */
        void SendForceReactions();

        /**
         * @brief 发送阵营状态给客户端
         * @param faction 要发送的阵营状态（为nullptr时仅发送needSend标记的阵营）
         *
         * 发送声望更新包给客户端（SMSG_SET_FACTION_STANDING）
         * 如果faction为nullptr，则发送所有needSend为true的阵营
         *
         * 调用时机：声望发生变化时
         */
        void SendState(FactionState const* faction);

    private:                                                // 内部辅助函数
        /**
         * @brief 初始化声望管理器
         *
         * 清空所有声望数据，遍历FactionStore初始化所有阵营的默认状态
         * 设置默认的可见性、交战状态等标志，并初始化计数器
         *
         * 调用时机：LoadFromDB之前，或在需要重置声望时
         */
        void Initialize();

        /**
         * @brief 获取阵营的默认状态标志
         * @param factionEntry 阵营条目指针
         * @return 默认状态标志位
         *
         * 根据玩家的种族和职业，从FactionEntry中查找对应的默认标志
         * 包括初始可见性、交战状态等
         */
        uint32 GetDefaultStateFlags(FactionEntry const* factionEntry) const;

        /**
         * @brief 设置声望的核心实现函数
         * @param factionEntry 阵营条目指针
         * @param standing 声望值或增量
         * @param incremental true为增量模式，false为绝对值模式
         * @param spillOverOnly 是否仅处理声望溢出
         * @return 是否成功修改
         *
         * 核心声望设置函数，处理：
         * 1. 声望溢出（spillover）到关联阵营
         * 2. 调用SetOneFactionReputation设置主阵营声望
         * 3. 发送更新给客户端
         *
         * 声望溢出机制：
         * - 某些阵营提高声望会同时提高其关联阵营的声望
         * - 可以通过数据库的RepSpilloverTemplate自定义溢出规则
         * - 如果没有数据库定义，则使用DBC中的ParentFaction信息
         */
        bool SetReputation(FactionEntry const* factionEntry, int32 standing, bool incremental, bool spillOverOnly);

        /**
         * @brief 设置阵营可见（通过阵营状态）
         * @param faction 阵营状态指针
         *
         * 内部实现，设置阵营为可见状态
         * 会检查是否可以设置可见（避免强制隐藏的阵营被显示）
         */
        void SetVisible(FactionState* faction);

        /**
         * @brief 设置阵营交战状态（通过阵营状态）
         * @param faction 阵营状态指针
         * @param atWar true为开启交战，false为关闭交战
         *
         * 内部实现，设置阵营的交战状态
         * 会检查是否允许切换交战状态（避免强制和平阵营被设为交战）
         */
        void SetAtWar(FactionState* faction, bool atWar) const;

        /**
         * @brief 设置阵营未激活状态（通过阵营状态）
         * @param faction 阵营状态指针
         * @param inactive true为设为未激活，false为激活
         *
         * 内部实现，设置阵营的未激活状态
         * 会检查阵营是否可见，未激活状态需要阵营先可见
         */
        void SetInactive(FactionState* faction, bool inactive) const;

        /**
         * @brief 发送阵营可见性更新给客户端
         * @param faction 阵营状态指针
         *
         * 发送SMSG_SET_FACTION_VISIBLE包，使阵营在客户端声望列表中显示
         * 如果玩家正在加载中，则跳过发送
         */
        void SendVisible(FactionState const* faction) const;

        /**
         * @brief 更新声望等级计数器
         * @param old_rank 旧的声望等级
         * @param new_rank 新的声望等级
         *
         * 当声望等级变化时，更新_honoredFactionCount、_reveredFactionCount、
         * _exaltedFactionCount等计数器，用于成就系统判断
         *
         * 性能注意：仅在等级变化时调用，避免频繁更新
         */
        void UpdateRankCounters(ReputationRank old_rank, ReputationRank new_rank);

    private:
        // 成员变量
        Player* _player;                    // 拥有此声望管理器的玩家对象
        FactionStateList _factions;         // 所有阵营的状态列表，以声望列表ID为键
        ForcedReactions _forcedReactions;   // 强制声望反应映射
        uint8 _visibleFactionCount;         // 可见阵营数量（用于成就统计）
        uint8 _honoredFactionCount;         // 尊敬及以上阵营数量（用于成就统计）
        uint8 _reveredFactionCount;         // 崇敬及以上阵营数量（用于成就统计）
        uint8 _exaltedFactionCount;         // 崇拜阵营数量（用于成就统计）
        bool _sendFactionIncreased;         // 是否在下一次发送声望更新时播放声望提升特效
};

#endif
