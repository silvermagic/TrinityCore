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
 * @file BattlegroundMgr.h
 * @brief 战场管理器模块头文件
 *
 * 本文件定义了战场管理器类，负责管理所有战场实例和队列。
 * 主要职责包括：
 * - 创建、销毁战场实例
 * - 管理战场模板和数据存储
 * - 处理战场队列更新和调度
 * - 管理战场节日和奖励
 * - 处理战场大师NPC映射
 * - 自动分配竞技场点数
 */

#ifndef __BATTLEGROUNDMGR_H
#define __BATTLEGROUNDMGR_H

#include "Common.h"
#include "DBCEnums.h"
#include "Battleground.h"
#include "BattlegroundQueue.h"
#include "UniqueTrackablePtr.h"
#include <unordered_map>

struct BattlemasterListEntry;

typedef std::map<uint32, Trinity::unique_trackable_ptr<Battleground>> BattlegroundContainer;  // 战场容器类型定义
typedef std::set<uint32> BattlegroundClientIdsContainer;                                        // 战场客户端ID容器类型定义

typedef std::unordered_map<uint32, BattlegroundTypeId> BattleMastersMap;                        // 战场大师映射类型定义

/**
 * @brief 战场杂项常量枚举
 *
 * 定义战场系统使用的各种常量值
 */
enum BattlegroundMisc
{
    BATTLEGROUND_ARENA_POINT_DISTRIBUTION_DAY   = 86400,    // 一天的秒数（用于竞技场点数分配）
    BATTLEGROUND_OBJECTIVE_UPDATE_INTERVAL      = 1000      // 战场目标更新间隔（毫秒）
};

/**
 * @brief 战场数据结构体
 *
 * 存储特定战场类型的所有实例数据
 */
struct BattlegroundData
{
    BattlegroundContainer m_Battlegrounds;                               // 战场实例容器（实例ID -> 战场指针）
    BattlegroundClientIdsContainer m_ClientBattlegroundIds[MAX_BATTLEGROUND_BRACKETS]; // 各分段的客户端ID集合
    BGFreeSlotQueueContainer BGFreeSlotQueue;                            // 空闲槽位队列
};

/**
 * @brief 战场模板结构体
 *
 * 定义战场的基本配置模板，从数据库加载
 */
struct BattlegroundTemplate
{
    BattlegroundTypeId Id;                                               // 战场类型ID
    uint16 MinPlayersPerTeam;                                            // 每队最小玩家数
    uint16 MaxPlayersPerTeam;                                            // 每队最大玩家数
    uint8 MinLevel;                                                      // 最低等级
    uint8 MaxLevel;                                                      // 最高等级
    Position StartLocation[PVP_TEAMS_COUNT];                             // 阵营起始位置数组
    float MaxStartDistSq;                                                // 最大起始距离的平方
    uint8 Weight;                                                        // 随机战场选择权重
    uint32 ScriptId;                                                     // 脚本ID
    BattlemasterListEntry const* BattlemasterEntry;                      // 战场大师列表条目指针

    /**
     * @brief 检查是否为竞技场
     * @return 是竞技场返回true
     */
    bool IsArena() const;
};

/**
 * @class BattlegroundMgr
 * @brief 战场管理器类
 *
 * 单例模式的战场管理器，负责管理所有战场和竞技场的生命周期。
 * 主要职责包括：
 * 1. 创建和管理所有战场实例
 * 2. 处理战场队列更新和匹配
 * 3. 管理战场模板和配置数据
 * 4. 处理战场节日和特殊活动
 * 5. 自动分配竞技场点数
 * 6. 管理战场大师NPC映射关系
 */
class TC_GAME_API BattlegroundMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         *
         * 初始化战场管理器的成员变量
         */
        BattlegroundMgr();

        /**
         * @brief 私有析构函数
         *
         * 清理所有战场实例
         */
        ~BattlegroundMgr();

    public:
        BattlegroundMgr(BattlegroundMgr const& right) = delete;             // 禁止拷贝构造
        BattlegroundMgr(BattlegroundMgr&& right) = delete;                  // 禁止移动构造
        BattlegroundMgr& operator=(BattlegroundMgr const& right) = delete;  // 禁止拷贝赋值
        BattlegroundMgr& operator=(BattlegroundMgr&& right) = delete;       // 禁止移动赋值

        /**
         * @brief 获取单例实例
         * @return 战场管理器实例指针
         *
         * 使用静态局部变量实现线程安全的单例模式
         */
        static BattlegroundMgr* instance();

        /**
         * @brief 更新战场管理器
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 主更新循环，定期调用以更新所有战场实例、队列和计时器
         */
        void Update(uint32 diff);

        /* Packet Building */
        /**
         * @brief 构建玩家加入战场数据包
         * @param data 数据包指针
         * @param player 加入的玩家指针
         */
        void BuildPlayerJoinedBattlegroundPacket(WorldPacket* data, Player* player);

        /**
         * @brief 构建玩家离开战场数据包
         * @param data 数据包指针
         * @param guid 离开的玩家GUID
         */
        void BuildPlayerLeftBattlegroundPacket(WorldPacket* data, ObjectGuid guid);

        /**
         * @brief 构建战场列表数据包
         * @param data 数据包指针
         * @param guid 战场大师GUID
         * @param player 玩家指针
         * @param bgTypeId 战场类型ID
         * @param fromWhere 加入来源
         */
        void BuildBattlegroundListPacket(WorldPacket* data, ObjectGuid guid, Player* player, BattlegroundTypeId bgTypeId, uint8 fromWhere);

        /**
         * @brief 构建队伍加入战场结果数据包
         * @param data 数据包指针
         * @param result 加入结果
         */
        void BuildGroupJoinedBattlegroundPacket(WorldPacket* data, GroupJoinBattlegroundResult result);

        /**
         * @brief 构建战场状态数据包
         * @param data 数据包指针
         * @param bg 战场指针
         * @param queueSlot 队列槽位
         * @param statusId 状态ID
         * @param time1 时间1（含义根据状态变化）
         * @param time2 时间2（含义根据状态变化）
         * @param arenaType 竞技场类型
         * @param arenaFaction 竞技场阵营
         */
        void BuildBattlegroundStatusPacket(WorldPacket* data, Battleground* bg, uint8 queueSlot, uint8 statusId, uint32 time1, uint32 time2, uint8 arenaType, uint32 arenaFaction);

        /**
         * @brief 发送区域灵魂医者查询操作码
         * @param player 玩家指针
         * @param bg 战场指针
         * @param guid 灵魂医者GUID
         */
        void SendAreaSpiritHealerQueryOpcode(Player* player, Battleground* bg, ObjectGuid guid);

        /* Battlegrounds */
        /**
         * @brief 通过客户端实例ID获取战场
         * @param instanceId 客户端实例ID
         * @param bgTypeId 战场类型ID
         * @return 战场指针，未找到返回nullptr
         *
         * 客户端发送的实例ID与内部实例ID可能不同，需要通过此方法转换
         */
        Battleground* GetBattlegroundThroughClientInstance(uint32 instanceId, BattlegroundTypeId bgTypeId);

        /**
         * @brief 通过实例ID获取战场
         * @param InstanceID 实例ID
         * @param bgTypeId 战场类型ID（BATTLEGROUND_TYPE_NONE表示搜索所有类型）
         * @return 战场指针，未找到返回nullptr
         */
        Battleground* GetBattleground(uint32 InstanceID, BattlegroundTypeId bgTypeId);

        /**
         * @brief 获取战场模板
         * @param bgTypeId 战场类型ID
         * @return 战场模板指针，未找到返回nullptr
         *
         * 返回用于创建战场实例的模板战场
         */
        Battleground* GetBattlegroundTemplate(BattlegroundTypeId bgTypeId);

        /**
         * @brief 创建新战场
         * @param bgTypeId 战场类型ID
         * @param bracketEntry PvP难度条目
         * @param arenaType 竞技场类型（2v2/3v3/5v5）
         * @param isRated 是否为评级比赛
         * @return 新创建的战场指针，失败返回nullptr
         */
        Battleground* CreateNewBattleground(BattlegroundTypeId bgTypeId, PvPDifficultyEntry const* bracketEntry, uint8 arenaType, bool isRated);

        /**
         * @brief 添加战场到管理器
         * @param bg 战场指针
         */
        void AddBattleground(Battleground* bg);

        /**
         * @brief 将战场添加到空闲槽位队列
         * @param bgTypeId 战场类型ID
         * @param bg 战场指针
         */
        void AddToBGFreeSlotQueue(BattlegroundTypeId bgTypeId, Battleground* bg);

        /**
         * @brief 从空闲槽位队列移除战场
         * @param bgTypeId 战场类型ID
         * @param instanceId 战场实例ID
         */
        void RemoveFromBGFreeSlotQueue(BattlegroundTypeId bgTypeId, uint32 instanceId);

        /**
         * @brief 获取空闲槽位队列存储
         * @param bgTypeId 战场类型ID
         * @return 空闲槽位队列引用
         */
        BGFreeSlotQueueContainer& GetBGFreeSlotQueueStore(BattlegroundTypeId bgTypeId);

        /**
         * @brief 加载战场模板
         *
         * 从数据库加载所有战场模板配置
         */
        void LoadBattlegroundTemplates();

        /**
         * @brief 删除所有战场
         *
         * 清理所有战场实例，用于服务器关闭或重载
         */
        void DeleteAllBattlegrounds();

        /**
         * @brief 将玩家传送到战场
         * @param player 玩家指针
         * @param InstanceID 战场实例ID
         * @param bgTypeId 战场类型ID
         */
        void SendToBattleground(Player* player, uint32 InstanceID, BattlegroundTypeId bgTypeId);

        /* Battleground queues */
        /**
         * @brief 获取战场队列
         * @param bgQueueTypeId 战场队列类型ID
         * @return 战场队列引用
         */
        BattlegroundQueue& GetBattlegroundQueue(BattlegroundQueueTypeId bgQueueTypeId) { return m_BattlegroundQueues[bgQueueTypeId]; }

        /**
         * @brief 调度队列更新
         * @param arenaMatchmakerRating 竞技场匹配等级
         * @param arenaType 竞技场类型
         * @param bgQueueTypeId 战场队列类型ID
         * @param bgTypeId 战场类型ID
         * @param bracket_id 战场分段ID
         *
         * 将队列更新请求添加到调度列表，避免频繁更新
         */
        void ScheduleQueueUpdate(uint32 arenaMatchmakerRating, uint8 arenaType, BattlegroundQueueTypeId bgQueueTypeId, BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id);

        /**
         * @brief 获取过早结束时间
         * @return 过早结束计时器值（毫秒）
         *
         * 当一方玩家数量不足时，战场过早结束的等待时间
         */
        uint32 GetPrematureFinishTime() const;

        /**
         * @brief 切换竞技场测试模式
         *
         * 开启/关闭竞技场测试模式，用于GM测试
         */
        void ToggleArenaTesting();

        /**
         * @brief 切换战场测试模式
         *
         * 开启/关闭战场测试模式，用于GM测试
         */
        void ToggleTesting();

        /**
         * @brief 重置所有战场节日状态
         */
        void ResetHolidays();

        /**
         * @brief 设置战场节日激活状态
         * @param battlegroundId 战场ID
         *
         * 激活指定战场的节日周末奖励
         */
        void SetHolidayActive(uint32 battlegroundId);

        bool isArenaTesting() const { return m_ArenaTesting; }              // 是否处于竞技场测试模式
        bool isTesting() const { return m_Testing; }                        // 是否处于战场测试模式

        /**
         * @brief 将战场类型ID转换为队列类型ID
         * @param bgTypeId 战场类型ID
         * @param arenaType 竞技场类型
         * @return 战场队列类型ID
         */
        static BattlegroundQueueTypeId BGQueueTypeId(BattlegroundTypeId bgTypeId, uint8 arenaType);

        /**
         * @brief 将队列类型ID转换为战场类型ID
         * @param bgQueueTypeId 战场队列类型ID
         * @return 战场类型ID
         */
        static BattlegroundTypeId BGTemplateId(BattlegroundQueueTypeId bgQueueTypeId);

        /**
         * @brief 获取队列类型对应的竞技场类型
         * @param bgQueueTypeId 战场队列类型ID
         * @return 竞技场类型（2v2/3v3/5v5），如果不是竞技场返回0
         */
        static uint8 BGArenaType(BattlegroundQueueTypeId bgQueueTypeId);

        /**
         * @brief 将战场类型ID转换为周末节日ID
         * @param bgTypeId 战场类型ID
         * @return 节日ID
         */
        static HolidayIds BGTypeToWeekendHolidayId(BattlegroundTypeId bgTypeId);

        /**
         * @brief 将周末节日ID转换为战场类型ID
         * @param holiday 节日ID
         * @return 战场类型ID
         */
        static BattlegroundTypeId WeekendHolidayIdToBGType(HolidayIds holiday);

        /**
         * @brief 检查是否为战场周末
         * @param bgTypeId 战场类型ID
         * @return 是战场周末返回true
         */
        static bool IsBGWeekend(BattlegroundTypeId bgTypeId);

        /**
         * @brief 获取最大评级差距
         * @return 最大评级差距值
         *
         * 用于竞技场匹配，限制匹配双方的评级差距
         */
        uint32 GetMaxRatingDifference() const;

        /**
         * @brief 获取评级丢弃计时器
         * @return 评级丢弃计时器值（毫秒）
         *
         * 等待时间超过此值后，放宽评级差距限制
         */
        uint32 GetRatingDiscardTimer()  const;

        /**
         * @brief 初始化自动竞技场点数分配
         *
         * 设置自动分配竞技场点数的计时器
         */
        void InitAutomaticArenaPointDistribution();

        /**
         * @brief 加载战场大师条目
         *
         * 从数据库加载战场大师NPC与战场类型的映射关系
         */
        void LoadBattleMastersEntry();

        /**
         * @brief 检查战场大师配置
         *
         * 验证所有标记为战场大师的NPC都有对应的战场类型配置
         */
        void CheckBattleMasters();

        /**
         * @brief 获取战场大师对应的战场类型
         * @param entry NPC条目ID
         * @return 战场类型ID，未找到返回BATTLEGROUND_TYPE_NONE
         */
        BattlegroundTypeId GetBattleMasterBG(uint32 entry) const
        {
            BattleMastersMap::const_iterator itr = mBattleMastersMap.find(entry);
            if (itr != mBattleMastersMap.end())
                return itr->second;
            return BATTLEGROUND_TYPE_NONE;
        }

    private:
        /**
         * @brief 创建战场实例
         * @param bgTemplate 战场模板指针
         * @return 成功返回true
         */
        bool CreateBattleground(BattlegroundTemplate const* bgTemplate);

        /**
         * @brief 创建客户端可见的实例ID
         * @param bgTypeId 战场类型ID
         * @param bracket_id 战场分段ID
         * @return 客户端实例ID
         *
         * 为战场创建用于客户端显示的唯一ID
         */
        uint32 CreateClientVisibleInstanceId(BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id);

        /**
         * @brief 检查是否为竞技场类型
         * @param bgTypeId 战场类型ID
         * @return 是竞技场返回true
         */
        static bool IsArenaType(BattlegroundTypeId bgTypeId);

        /**
         * @brief 获取随机战场
         * @param id 战场类型ID
         * @return 随机选择的战场类型ID
         *
         * 根据权重随机选择一个战场类型
         */
        BattlegroundTypeId GetRandomBG(BattlegroundTypeId id);

        typedef std::map<BattlegroundTypeId, BattlegroundData> BattlegroundDataContainer;
        BattlegroundDataContainer bgDataStore;                             // 战场数据存储容器

        BattlegroundQueue m_BattlegroundQueues[MAX_BATTLEGROUND_QUEUE_TYPES]; // 战场队列数组

        std::vector<uint64> m_QueueUpdateScheduler;                         // 队列更新调度器
        uint32 m_NextRatedArenaUpdate;                                      // 下次评级竞技场更新时间
        time_t m_NextAutoDistributionTime;                                  // 下次自动分配时间
        uint32 m_AutoDistributionTimeChecker;                              // 自动分配时间检查器
        uint32 m_UpdateTimer;                                               // 更新计时器
        bool   m_ArenaTesting;                                              // 竞技场测试模式标志
        bool   m_Testing;                                                   // 战场测试模式标志
        BattleMastersMap mBattleMastersMap;                                // 战场大师映射表

        /**
         * @brief 根据战场类型ID获取战场模板
         * @param id 战场类型ID
         * @return 战场模板指针，未找到返回nullptr
         */
        BattlegroundTemplate const* GetBattlegroundTemplateByTypeId(BattlegroundTypeId id)
        {
            BattlegroundTemplateMap::const_iterator itr = _battlegroundTemplates.find(id);
            if (itr != _battlegroundTemplates.end())
                return &itr->second;
            return nullptr;
        }

        /**
         * @brief 根据地图ID获取战场模板
         * @param mapId 地图ID
         * @return 战场模板指针，未找到返回nullptr
         */
        BattlegroundTemplate const* GetBattlegroundTemplateByMapId(uint32 mapId)
        {
            BattlegroundMapTemplateContainer::const_iterator itr = _battlegroundMapTemplates.find(mapId);
            if (itr != _battlegroundMapTemplates.end())
                return itr->second;
            return nullptr;
        }

        typedef std::map<BattlegroundTypeId, uint8 /*weight*/> BattlegroundSelectionWeightMap;

        typedef std::map<BattlegroundTypeId, BattlegroundTemplate> BattlegroundTemplateMap;
        typedef std::map<uint32 /*mapId*/, BattlegroundTemplate*> BattlegroundMapTemplateContainer;
        BattlegroundTemplateMap _battlegroundTemplates;                     // 战场模板映射表
        BattlegroundMapTemplateContainer _battlegroundMapTemplates;         // 地图ID到战场模板的映射
};

#define sBattlegroundMgr BattlegroundMgr::instance()

#endif // __BATTLEGROUNDMGR_H
