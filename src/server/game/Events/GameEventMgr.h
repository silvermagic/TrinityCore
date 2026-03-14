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
 * @file GameEventMgr.h
 * @brief 游戏事件管理器头文件
 *
 * 本模块负责管理游戏中的周期性事件和节日活动，包括：
 * - 事件的启动、停止和状态管理
 * - 事件期间的生物和游戏对象生成/移除
 * - 事件相关的任务、商人和装备模型更新
 * - 节日活动的日程安排
 * - 世界状态更新和条件检测
 *
 * 游戏事件可以是：
 * - 周期性事件（如暗月马戏团、元素入侵等）
 * - 节日活动（如万圣节、圣诞节等）
 * - 世界事件（如安其拉开门等需要玩家参与的事件）
 */

#ifndef TRINITY_GAMEEVENT_MGR_H
#define TRINITY_GAMEEVENT_MGR_H

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "Define.h"
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

/**
 * @brief 游戏事件检查的最大延迟时间
 *
 * 定义游戏事件管理器检查事件状态的最大时间间隔（1天）
 * 用于避免频繁检查长期未开始的事件
 */
#define max_ge_check_delay DAY  // 1 day in seconds

/**
 * @brief 游戏事件状态枚举
 *
 * 定义游戏事件可能处于的各种状态，用于控制事件的生命周期
 */
enum GameEventState
{
    GAMEEVENT_NORMAL           = 0, // 标准游戏事件，根据时间自动启动和停止
    GAMEEVENT_WORLD_INACTIVE   = 1, // 世界事件未激活状态，尚未开始
    GAMEEVENT_WORLD_CONDITIONS = 2, // 世界事件条件匹配阶段，等待条件满足
    GAMEEVENT_WORLD_NEXTPHASE  = 3, // 世界事件条件已满足，等待length定时器启动下一阶段
    GAMEEVENT_WORLD_FINISHED   = 4, // 世界事件已完成，下一事件已启动，本事件需要取消应用
    GAMEEVENT_INTERNAL         = 5  // 内部事件，永不通过update函数处理
};

/**
 * @brief 游戏事件完成条件结构
 *
 * 用于追踪世界事件的完成进度，例如安其拉战争努力需要收集的物资数量
 */
struct GameEventFinishCondition
{
    float reqNum;              // 需要达到的目标数量（使用float因为某些事件使用百分比）
    float done;                // 当前已完成数量
    uint32 max_world_state;    // 最大资源计数的世界状态更新ID
    uint32 done_world_state;   // 已完成资源计数的世界状态更新ID
};

/**
 * @brief 任务到事件条件的映射结构
 *
 * 记录完成任务对事件条件进度的贡献
 */
struct GameEventQuestToEventConditionNum
{
    uint16 event_id;    // 游戏事件ID
    uint32 condition;   // 条件ID
    float num;          // 该任务对条件的贡献值
};

/**
 * @brief 事件条件映射类型
 *
 * 键：条件ID，值：GameEventFinishCondition结构
 */
typedef std::map<uint32 /*condition id*/, GameEventFinishCondition> GameEventConditionMap;

/**
 * @brief 游戏事件数据结构
 *
 * 存储单个游戏事件的所有信息，包括时间安排、状态、条件等
 */
struct GameEventData
{
    /**
     * @brief 默认构造函数
     *
     * 初始化事件数据，默认设置start为1，end为0表示无效事件
     */
    GameEventData() : start(1), end(0), nextstart(0), occurence(0), length(0), holiday_id(HOLIDAY_NONE), holidayStage(0), state(GAMEEVENT_NORMAL),
                      announce(0) { }
    time_t start;           // 事件开始时间（Unix时间戳）
    time_t end;             // 事件结束时间（Unix时间戳）
    time_t nextstart;       // 下一阶段开始时间，此时间后后续事件将本阶段视为已完成
    uint32 occurence;       // 事件周期，从结束到下次开始的间隔时间（秒）
    uint32 length;          // 事件持续时长（分钟），条件全部完成后的事件持续时间
    HolidayIds holiday_id;  // 关联的节日ID
    uint8 holidayStage;     // 节日阶段
    GameEventState state;   // 事件状态，状态变更会保存到game_event表中
    GameEventConditionMap conditions;  // 事件完成条件映射
    std::set<uint16 /*gameevent id*/> prerequisite_events;  // 前置事件集合，必须完成这些事件才能启动本事件
    std::string description;// 事件描述
    uint8 announce;         // 公告设置：0=不公告，1=公告，2=使用配置值

    /**
     * @brief 检查事件是否有效
     * @return 如果事件有效返回true
     */
    bool isValid() const { return length > 0 || state > GAMEEVENT_NORMAL; }
};

/**
 * @brief 模型和装备数据结构
 *
 * 存储生物在事件期间的模型ID和装备ID变更信息
 */
struct ModelEquip
{
    uint32 modelid;          // 新模型ID
    uint32 modelid_prev;     // 原始模型ID
    uint8 equipment_id;      // 新装备ID
    uint8 equipement_id_prev;// 原始装备ID
};

/**
 * @brief NPC商人条目结构
 *
 * 存储事件期间NPC出售物品的信息
 */
struct NPCVendorEntry
{
    uint32 entry;           // 生物模板ID
    uint32 item;            // 物品ID
    int32  maxcount;        // 最大库存数量，0表示无限
    uint32 incrtime;        // 库存恢复时间（秒），当maxcount != 0时有效
    uint32 ExtendedCost;    // 扩展花费ID
};

class Player;
class Creature;
class Quest;

/**
 * @class GameEventMgr
 * @brief 游戏事件管理器
 *
 * 单例类，负责管理游戏中的所有周期性事件和节日活动。
 * 主要功能包括：
 * - 从数据库加载事件数据并初始化
 * - 根据时间自动启动和停止事件
 * - 管理事件相关的生物、游戏对象、任务、商人等
 * - 处理世界事件的条件检测和进度追踪
 * - 发送世界状态更新给玩家
 * - 运行事件相关的SmartAI脚本
 *
 * 使用方式：
 * - 通过sGameEventMgr宏访问单例实例
 * - 世界服务器启动时调用Initialize()和LoadFromDB()
 * - 世界更新循环中定期调用Update()
 */
class TC_GAME_API GameEventMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        GameEventMgr();
        /**
         * @brief 私有析构函数
         */
        ~GameEventMgr() { }

    public:
        /**
         * @brief 获取单例实例
         * @return GameEventMgr单例指针
         */
        static GameEventMgr* instance();

        /**
         * @brief 活动事件ID集合类型
         */
        typedef std::set<uint16> ActiveEvents;
        /**
         * @brief 事件数据向量类型
         */
        typedef std::vector<GameEventData> GameEventDataMap;

        /**
         * @brief 获取当前活动事件列表
         * @return 活动事件ID集合的常引用
         * @note 线程安全，只读操作
         */
        ActiveEvents const& GetActiveEventList() const { return m_ActiveEvents; }

        /**
         * @brief 获取所有事件数据映射
         * @return 事件数据向量的常引用
         */
        GameEventDataMap const& GetEventMap() const { return mGameEvent; }

        /**
         * @brief 检查单个游戏事件是否应该激活
         * @param entry 事件ID
         * @return 如果事件应该激活返回true
         */
        bool CheckOneGameEvent(uint16 entry) const;

        /**
         * @brief 计算下次检查事件的时间
         * @param entry 事件ID
         * @return 距离下次检查的秒数
         */
        uint32 NextCheck(uint16 entry) const;

        /**
         * @brief 从数据库加载所有事件数据
         *
         * 加载事件定义、生物、游戏对象、任务、商人、模型装备等数据
         * 调用时机：世界服务器启动时
         */
        void LoadFromDB();

        /**
         * @brief 加载节日日期数据
         *
         * 从DBC文件加载节日相关的时间信息
         */
        void LoadHolidayDates();

        /**
         * @brief 更新所有游戏事件
         * @return 距离下次需要更新的时间（秒）
         *
         * 检查所有事件的状态，启动应该启动的事件，停止应该停止的事件
         * 调用时机：世界服务器每次更新循环中
         * 性能注意：会遍历所有事件，但不频繁调用
         */
        uint32 Update();

        /**
         * @brief 检查事件是否处于活动状态
         * @param event_id 事件ID
         * @return 如果事件活动返回true
         */
        bool IsActiveEvent(uint16 event_id) { return (m_ActiveEvents.find(event_id) != m_ActiveEvents.end()); }

        /**
         * @brief 启动系统事件
         * @return 下次更新时间
         *
         * 初始化并启动所有应该活动的事件
         */
        uint32 StartSystem();

        /**
         * @brief 初始化游戏事件管理器
         *
         * 重置所有事件状态，清空活动事件列表
         */
        void Initialize();

        /**
         * @brief 启动竞技场赛季事件
         *
         * 根据数据库配置启动当前竞技场赛季对应的事件
         */
        void StartArenaSeason();

        /**
         * @brief 启动内部事件
         * @param event_id 事件ID
         *
         * 启动GAMEEVENT_INTERNAL类型的事件，这类事件不会通过Update自动处理
         */
        void StartInternalEvent(uint16 event_id);

        /**
         * @brief 启动指定事件
         * @param event_id 事件ID
         * @param overwrite 是否覆盖时间检查，默认false
         * @return 如果成功启动返回true
         *
         * 手动启动事件，会生成事件相关的生物、游戏对象，更新任务等
         * 当overwrite=true时，忽略事件的时间限制强制启动
         */
        bool StartEvent(uint16 event_id, bool overwrite = false);

        /**
         * @brief 停止指定事件
         * @param event_id 事件ID
         * @param overwrite 是否覆盖时间检查，默认false
         *
         * 手动停止事件，会移除事件相关的生物、游戏对象，恢复原始状态
         * 当overwrite=true时，忽略事件的时间限制强制停止
         */
        void StopEvent(uint16 event_id, bool overwrite = false);

        /**
         * @brief 处理任务完成
         * @param quest_id 任务ID
         *
         * 当玩家完成与世界事件相关的任务时调用，更新事件条件进度
         * 调用时机：玩家完成任务时
         */
        void HandleQuestComplete(uint32 quest_id);

        /**
         * @brief 获取生物的NPC标志
         * @param cr 生物对象指针
         * @return NPC标志位掩码
         *
         * 根据活动事件计算生物应该显示的NPC标志
         */
        uint32 GetNPCFlag(Creature* cr);

    private:
        /**
         * @brief 发送世界状态更新给玩家
         * @param player 玩家对象指针
         * @param event_id 事件ID
         *
         * 将事件相关的世界状态变量发送给指定玩家
         */
        void SendWorldStateUpdate(Player* player, uint16 event_id);

        /**
         * @brief 添加活动事件
         * @param event_id 事件ID
         */
        void AddActiveEvent(uint16 event_id) { m_ActiveEvents.insert(event_id); }

        /**
         * @brief 移除活动事件
         * @param event_id 事件ID
         */
        void RemoveActiveEvent(uint16 event_id) { m_ActiveEvents.erase(event_id); }

        /**
         * @brief 应用新事件
         * @param event_id 事件ID
         *
         * 执行事件启动时的所有操作：生成生物、游戏对象，更新任务、商人等
         */
        void ApplyNewEvent(uint16 event_id);

        /**
         * @brief 取消应用事件
         * @param event_id 事件ID
         *
         * 执行事件停止时的所有清理操作：移除生物、游戏对象，恢复原始状态
         */
        void UnApplyEvent(uint16 event_id);

        /**
         * @brief 生成事件生物和游戏对象
         * @param event_id 事件ID
         *
         * 生成事件关联的所有生物和游戏对象
         */
        void GameEventSpawn(int16 event_id);

        /**
         * @brief 移除事件生物和游戏对象
         * @param event_id 事件ID
         *
         * 移除事件关联的所有生物和游戏对象
         */
        void GameEventUnspawn(int16 event_id);

        /**
         * @brief 更改装备或模型
         * @param event_id 事件ID
         * @param activate true表示激活事件，false表示停止事件
         *
         * 切换事件关联生物的模型和装备
         */
        void ChangeEquipOrModel(int16 event_id, bool activate);

        /**
         * @brief 更新事件任务
         * @param event_id 事件ID
         * @param activate true表示激活任务，false表示停用任务
         *
         * 激活或停用事件关联的任务
         */
        void UpdateEventQuests(uint16 event_id, bool activate);

        /**
         * @brief 更新世界状态
         * @param event_id 事件ID
         * @param Activate true表示激活，false表示停用
         *
         * 更新事件相关的世界状态变量
         */
        void UpdateWorldStates(uint16 event_id, bool Activate);

        /**
         * @brief 更新事件NPC标志
         * @param event_id 事件ID
         *
         * 更新事件关联NPC的交互标志
         */
        void UpdateEventNPCFlags(uint16 event_id);

        /**
         * @brief 更新事件NPC商人
         * @param event_id 事件ID
         * @param activate true表示激活，false表示停用
         *
         * 添加或移除事件期间NPC出售的物品
         */
        void UpdateEventNPCVendor(uint16 event_id, bool activate);

        /**
         * @brief 更新战场设置
         *
         * 根据活动的节日事件更新战场假日设置
         */
        void UpdateBattlegroundSettings();

        /**
         * @brief 运行SmartAI脚本
         * @param event_id 事件ID
         * @param activate true表示事件开始，false表示事件结束
         *
         * 触发SMART_EVENT_GAME_EVENT_START或SMART_EVENT_GAME_EVENT_END事件
         */
        void RunSmartAIScripts(uint16 event_id, bool activate);

        /**
         * @brief 检查单个游戏事件的条件
         * @param event_id 事件ID
         * @return 如果所有条件都满足返回true
         */
        bool CheckOneGameEventConditions(uint16 event_id);

        /**
         * @brief 保存世界事件状态到数据库
         * @param event_id 事件ID
         *
         * 将世界事件的状态和进度保存到game_event表
         */
        void SaveWorldEventStateToDB(uint16 event_id);

        /**
         * @brief 检查是否有其他活动事件也包含该生物任务
         * @param quest_id 任务ID
         * @param event_id 要排除的事件ID
         * @return 如果有其他活动事件包含此任务返回true
         */
        bool hasCreatureQuestActiveEventExcept(uint32 quest_id, uint16 event_id);

        /**
         * @brief 检查是否有其他活动事件也包含该游戏对象任务
         * @param quest_id 任务ID
         * @param event_id 要排除的事件ID
         * @return 如果有其他活动事件包含此任务返回true
         */
        bool hasGameObjectQuestActiveEventExcept(uint32 quest_id, uint16 event_id);

        /**
         * @brief 检查是否有其他活动事件也包含该生物
         * @param creature_guid 生物GUID低32位
         * @param event_id 要排除的事件ID
         * @return 如果有其他活动事件包含此生物返回true
         */
        bool hasCreatureActiveEventExcept(ObjectGuid::LowType creature_guid, uint16 event_id);

        /**
         * @brief 检查是否有其他活动事件也包含该游戏对象
         * @param go_guid 游戏对象GUID低32位
         * @param event_id 要排除的事件ID
         * @return 如果有其他活动事件包含此游戏对象返回true
         */
        bool hasGameObjectActiveEventExcept(ObjectGuid::LowType go_guid, uint16 event_id);

        /**
         * @brief 设置节日事件时间
         * @param event 事件数据引用
         *
         * 根据节日DBC数据计算并设置事件的开始和结束时间
         */
        void SetHolidayEventTime(GameEventData& event);

        // 类型定义
        typedef std::list<ObjectGuid::LowType> GuidList;                    // GUID列表类型
        typedef std::list<uint32> IdList;                                   // ID列表类型
        typedef std::vector<GuidList> GameEventGuidMap;                     // 事件到GUID列表的映射
        typedef std::vector<IdList> GameEventIdMap;                         // 事件到ID列表的映射
        typedef std::pair<ObjectGuid::LowType, ModelEquip> ModelEquipPair;  // GUID到模型装备的对
        typedef std::list<ModelEquipPair> ModelEquipList;                   // 模型装备列表
        typedef std::vector<ModelEquipList> GameEventModelEquipMap;         // 事件到模型装备列表的映射
        typedef std::pair<uint32, uint32> QuestRelation;                    // 任务关系对
        typedef std::list<QuestRelation> QuestRelList;                      // 任务关系列表
        typedef std::vector<QuestRelList> GameEventQuestMap;                // 事件到任务关系列表的映射
        typedef std::list<NPCVendorEntry> NPCVendorList;                    // NPC商人条目列表
        typedef std::vector<NPCVendorList> GameEventNPCVendorMap;           // 事件到NPC商人列表的映射
        typedef std::map<uint32 /*quest id*/, GameEventQuestToEventConditionNum> QuestIdToEventConditionMap; // 任务到事件条件的映射
        typedef std::pair<ObjectGuid::LowType /*guid*/, uint32 /*npcflag*/> GuidNPCFlagPair; // GUID到NPC标志的对
        typedef std::list<GuidNPCFlagPair> NPCFlagList;                     // NPC标志列表
        typedef std::vector<NPCFlagList> GameEventNPCFlagMap;               // 事件到NPC标志列表的映射
        typedef std::vector<uint32> GameEventBattlegroundMap;               // 事件到战场ID的映射

        // 成员变量
        GameEventQuestMap mGameEventCreatureQuests;                         // 事件关联的生物任务映射
        GameEventQuestMap mGameEventGameObjectQuests;                       // 事件关联的游戏对象任务映射
        GameEventNPCVendorMap mGameEventVendors;                            // 事件关联的NPC商人映射
        GameEventModelEquipMap mGameEventModelEquip;                        // 事件关联的模型装备映射
        //GameEventGuidMap  mGameEventCreatureGuids;                        // 事件关联的生物GUID映射（已废弃）
        //GameEventGuidMap  mGameEventGameobjectGuids;                      // 事件关联的游戏对象GUID映射（已废弃）
        GameEventIdMap    mGameEventPoolIds;                                // 事件关联的池ID映射
        GameEventDataMap  mGameEvent;                                       // 所有事件数据数组，索引为事件ID
        GameEventBattlegroundMap mGameEventBattlegroundHolidays;            // 事件关联的战场假日映射
        QuestIdToEventConditionMap mQuestToEventConditions;                 // 任务ID到事件条件的映射
        GameEventNPCFlagMap mGameEventNPCFlags;                             // 事件关联的NPC标志映射
        ActiveEvents m_ActiveEvents;                                        // 当前活动的事件ID集合
        bool isSystemInit;                                                  // 系统是否已初始化标志

    public:
        GameEventGuidMap  mGameEventCreatureGuids;      // 事件关联的生物GUID列表数组（索引为事件ID）
        GameEventGuidMap  mGameEventGameobjectGuids;    // 事件关联的游戏对象GUID列表数组（索引为事件ID）
        std::vector<uint32> modifiedHolidays;           // 已修改的节日ID列表
};

/**
 * @brief 获取游戏事件管理器单例
 */
#define sGameEventMgr GameEventMgr::instance()

/**
 * @brief 检查节日是否活动
 * @param id 节日ID
 * @return 如果节日活动返回true
 */
TC_GAME_API bool IsHolidayActive(HolidayIds id);

/**
 * @brief 检查事件是否活动
 * @param eventId 事件ID
 * @return 如果事件活动返回true
 */
TC_GAME_API bool IsEventActive(uint16 eventId);

#endif
