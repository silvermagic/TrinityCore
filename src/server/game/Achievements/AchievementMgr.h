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
 * @file AchievementMgr.h
 * @brief 成就系统管理模块
 *
 * 本模块实现了魔兽世界中的成就系统，包括：
 * - 成就进度跟踪与存储
 * - 成就条件检测与更新
 * - 成就奖励发放（称号、物品、邮件等）
 * - 服务器首杀成就管理
 * - 计时成就处理
 *
 * 成就系统主要包含两个核心类：
 * - AchievementMgr: 玩家级别的成就管理器，管理单个玩家的成就进度
 * - AchievementGlobalMgr: 全局成就管理器，管理成就定义、奖励配置等全局数据
 */

#ifndef __TRINITY_ACHIEVEMENTMGR_H
#define __TRINITY_ACHIEVEMENTMGR_H

#include "DatabaseEnvFwd.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "Duration.h"
#include "ObjectGuid.h"
#include <string>
#include <unordered_map>
#include <vector>

// 前向声明
class Player;
class WorldObject;
class WorldPacket;

// 成就条件条目列表类型定义
typedef std::vector<AchievementCriteriaEntry const*> AchievementCriteriaEntryList;
// 成就条目列表类型定义
typedef std::vector<AchievementEntry const*>         AchievementEntryList;

// 按成就ID索引的条件列表映射
typedef std::unordered_map<uint32, AchievementCriteriaEntryList> AchievementCriteriaListByAchievement;
// 按杂项值索引的条件列表映射（用于快速查找特定类型的条件）
typedef std::unordered_map<uint32, AchievementCriteriaEntryList> AchievementCriteriaListByMiscValue;
// 按条件类型索引的条件列表映射
typedef std::unordered_map<uint32, AchievementCriteriaEntryList> AchievementCriteriaListByCondition;
// 按引用成就ID索引的成就列表映射
typedef std::unordered_map<uint32, AchievementEntryList>         AchievementListByReferencedId;

/**
 * @struct CriteriaProgress
 * @brief 成就条件进度数据结构
 *
 * 用于存储玩家在某个成就条件上的进度信息
 */
struct CriteriaProgress
{
    uint32 counter;     ///< 当前进度计数器，表示已完成的数量
    time_t date;        ///< 最后更新时间（Unix时间戳）
    bool changed;       ///< 是否已修改但尚未保存到数据库
};

/**
 * @enum AchievementCriteriaDataType
 * @brief 成就条件数据类型枚举
 *
 * 定义了成就条件的各种附加数据类型，用于对成就条件进行更细致的限制
 * 例如：要求特定生物、特定地图、特定物品等级等
 */
enum AchievementCriteriaDataType
{                                                           // value1         value2        说明
    ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE                = 0, // 0              0             无附加条件
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_CREATURE          = 1, // creature_id    0             目标必须是特定生物
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE = 2, // class_id       race_id       目标玩家必须是特定职业/种族
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_LESS_HEALTH= 3, // health_percent 0             目标玩家血量低于指定百分比
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_DEAD       = 4, // own_team       0             目标玩家已死亡（非尸体状态），own_team表示是否同阵营
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA              = 5, // spell_id       effect_idx    源玩家必须拥有特定光环效果
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AREA              = 6, // area id        0             源玩家必须在特定区域
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA              = 7, // spell_id       effect_idx    目标必须拥有特定光环效果
    ACHIEVEMENT_CRITERIA_DATA_TYPE_VALUE               = 8, // minvalue                     更新值必须不小于此限制值
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_LEVEL             = 9, // minlevel                     目标的最小等级
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_GENDER            = 10, // gender                       目标的性别（0=男，1=女）
    ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT              = 11, // 脚本化条件检查              使用脚本自定义条件判断
    ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_DIFFICULTY      = 12, // difficulty                   当前地图的难度模式
    ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_PLAYER_COUNT    = 13, // count                        "区域内的玩家数少于指定数量"
    ACHIEVEMENT_CRITERIA_DATA_TYPE_T_TEAM              = 14, // team                         目标的阵营（HORDE=67, ALLIANCE=469）
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_DRUNK             = 15, // drunken_state  0             玩家的醉酒状态
    ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY             = 16, // holiday_id     0             必须在节日活动期间
    ACHIEVEMENT_CRITERIA_DATA_TYPE_BG_LOSS_TEAM_SCORE  = 17, // min_score      max_score     战场胜利时对方队伍分数在范围内
    ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT     = 18, // 0              0             调用副本脚本检查条件
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM     = 19, // item_level     item_quality  装备物品的等级和品质要求
    ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_ID              = 20, // map_id         0             玩家必须在指定地图
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE = 21, // class_id       race_id       源玩家的职业/种族
    ACHIEVEMENT_CRITERIA_DATA_TYPE_NTH_BIRTHDAY        = 22, // N                             在N周年纪念日期间登录
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_KNOWN_TITLE       = 23, // title_id                     玩家已知的称号
    // ACHIEVEMENT_CRITERIA_DATA_TYPE_GAME_EVENT       = 24, // 7.x only                    仅7.x版本使用
    ACHIEVEMENT_CRITERIA_DATA_TYPE_S_ITEM_QUALITY      = 25  // item_quality                 物品品质要求
};

/// 成就条件数据类型的最大值
#define MAX_ACHIEVEMENT_CRITERIA_DATA_TYPE               26

/**
 * @struct AchievementCriteriaData
 * @brief 成就条件附加数据结构
 *
 * 用于存储成就条件的附加限制条件数据
 * 采用联合体(union)来节省内存，不同类型的条件使用不同的数据字段
 */
struct AchievementCriteriaData
{
    AchievementCriteriaDataType dataType;   ///< 数据类型
    union
    {
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE              = 0 (无数据)
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_CREATURE        = 1
        /// 目标生物相关数据
        struct
        {
            uint32 id;                       ///< 生物ID
        } creature;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_CLASS_RACE = 2
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_PLAYER_CLASS_RACE = 21
        /// 职业/种族相关数据
        struct
        {
            uint32 class_id;                 ///< 职业ID
            uint32 race_id;                  ///< 种族ID
        } classRace;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_LESS_HEALTH = 3
        /// 血量百分比相关数据
        struct
        {
            uint32 percent;                  ///< 血量百分比阈值
        } health;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_PLAYER_DEAD     = 4
        /// 死亡玩家相关数据
        struct
        {
            uint32 own_team_flag;            ///< 是否同阵营标志
        } player_dead;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AURA            = 5
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_AURA            = 7
        /// 光环效果相关数据
        struct
        {
            uint32 spell_id;                 ///< 法术ID
            uint32 effect_idx;               ///< 效果索引
        } aura;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_AREA            = 6
        /// 区域相关数据
        struct
        {
            uint32 id;                       ///< 区域ID
        } area;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_VALUE             = 8
        /// 数值比较相关数据
        struct
        {
            uint32 value;                    ///< 比较值
            uint32 compType;                 ///< 比较类型
        } value;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_LEVEL           = 9
        /// 等级相关数据
        struct
        {
            uint32 minlevel;                 ///< 最小等级
        } level;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_GENDER          = 10
        /// 性别相关数据
        struct
        {
            uint32 gender;                   ///< 性别（0=男，1=女，2=无）
        } gender;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_SCRIPT            = 11 (无数据)
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_DIFFICULTY    = 12
        /// 地图难度相关数据
        struct
        {
            uint32 difficulty;               ///< 难度等级
        } difficulty;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_PLAYER_COUNT  = 13
        /// 地图玩家数量相关数据
        struct
        {
            uint32 maxcount;                 ///< 最大玩家数量
        } map_players;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_T_TEAM            = 14
        /// 阵营相关数据
        struct
        {
            uint32 team;                     ///< 阵营ID
        } team;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_DRUNK           = 15
        /// 醉酒状态相关数据
        struct
        {
            uint32 state;                    ///< 醉酒状态等级
        } drunk;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_HOLIDAY           = 16
        /// 节日相关数据
        struct
        {
            uint32 id;                       ///< 节日ID
        } holiday;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_BG_LOSS_TEAM_SCORE= 17
        /// 战场失败方分数相关数据
        struct
        {
            uint32 min_score;                ///< 最低分数
            uint32 max_score;                ///< 最高分数
        } bg_loss_team_score;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_INSTANCE_SCRIPT   = 18 (无数据)
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_EQUIPPED_ITEM   = 19
        /// 装备物品相关数据
        struct
        {
            uint32 item_level;               ///< 物品等级
            uint32 item_quality;             ///< 物品品质
        } equipped_item;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_MAP_ID            = 20
        /// 地图ID相关数据
        struct
        {
            uint32 mapId;                    ///< 地图ID
        } map_id;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_NTH_BIRTHDAY      = 22
        /// 周年纪念相关数据
        struct
        {
            uint32 nth_birthday;             ///< 第N个周年纪念日
        } birthday_login;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_KNOWN_TITLE       = 23
        /// 已知称号相关数据
        struct
        {
            uint32 title_id;                 ///< 称号ID
        } known_title;
        // ACHIEVEMENT_CRITERIA_DATA_TYPE_S_ITEM_QUALITY    = 25
        /// 物品品质相关数据
        struct
        {
            uint32 item_quality;             ///< 物品品质等级
        } item;
        // 原始数据访问（用于初始化和通用访问）
        struct
        {
            uint32 value1;                   ///< 原始值1
            uint32 value2;                   ///< 原始值2
        } raw;
    };
    uint32 ScriptId;                         ///< 脚本ID（用于脚本类型条件）

    /**
     * @brief 默认构造函数
     * 初始化为无数据类型
     */
    AchievementCriteriaData() : dataType(ACHIEVEMENT_CRITERIA_DATA_TYPE_NONE)
    {
        raw.value1 = 0;
        raw.value2 = 0;
        ScriptId = 0;
    }

    /**
     * @brief 参数构造函数
     * @param _dataType 数据类型
     * @param _value1 值1
     * @param _value2 值2
     * @param _scriptId 脚本ID
     */
    AchievementCriteriaData(uint32 _dataType, uint32 _value1, uint32 _value2, uint32 _scriptId) : dataType(AchievementCriteriaDataType(_dataType))
    {
        raw.value1 = _value1;
        raw.value2 = _value2;
        ScriptId = _scriptId;
    }

    /**
     * @brief 验证条件数据是否有效
     * @param criteria 成就条件条目
     * @return 数据有效返回true，否则返回false
     */
    bool IsValid(AchievementCriteriaEntry const* criteria);

    /**
     * @brief 检查条件是否满足
     * @param criteria_id 条件ID
     * @param source 源玩家
     * @param target 目标对象
     * @param miscValue1 杂项值1
     * @param miscValue2 杂项值2
     * @return 条件满足返回true，否则返回false
     */
    bool Meets(uint32 criteria_id, Player const* source, WorldObject const* target, uint32 miscValue1 = 0, uint32 miscValue2 = 0) const;
};

/**
 * @struct AchievementCriteriaDataSet
 * @brief 成就条件数据集合
 *
 * 存储单个成就条件的所有附加数据要求
 * 一个条件可以有多个数据要求，需要全部满足才能完成条件
 */
struct TC_GAME_API AchievementCriteriaDataSet
{
        AchievementCriteriaDataSet() : criteria_id(0) { }
        typedef std::vector<AchievementCriteriaData> Storage;

        /**
         * @brief 添加条件数据
         * @param data 条件数据
         */
        void Add(AchievementCriteriaData const& data) { storage.push_back(data); }

        /**
         * @brief 检查所有条件是否满足
         * @param source 源玩家
         * @param target 目标对象
         * @param miscValue1 杂项值1
         * @param miscValue2 杂项值2
         * @return 所有条件满足返回true，否则返回false
         */
        bool Meets(Player const* source, WorldObject const* target, uint32 miscValue1 = 0, uint32 miscValue2 = 0) const;

        /**
         * @brief 设置条件ID
         * @param id 条件ID
         */
        void SetCriteriaId(uint32 id) {criteria_id = id;}
    private:
        uint32 criteria_id;              ///< 条件ID
        Storage storage;                 ///< 条件数据存储容器
};

/// 成就条件数据映射表（按条件ID索引）
typedef std::unordered_map<uint32, AchievementCriteriaDataSet> AchievementCriteriaDataMap;

/**
 * @struct AchievementReward
 * @brief 成就奖励数据结构
 *
 * 定义完成成就后获得的奖励内容
 * 可以包含称号、物品、邮件等
 */
struct AchievementReward
{
    uint32 TitleId[2];                  ///< 称号ID数组 [0]=联盟, [1]=部落
    uint32 ItemId;                      ///< 奖励物品ID
    uint32 SenderCreatureId;            ///< 发送邮件的生物ID
    std::string Subject;                ///< 邮件主题
    std::string Body;                   ///< 邮件正文
    uint32 MailTemplateId;              ///< 邮件模板ID
};

/// 成就奖励映射表（按成就ID索引）
typedef std::unordered_map<uint32, AchievementReward> AchievementRewards;

/**
 * @struct AchievementRewardLocale
 * @brief 成就奖励本地化数据
 *
 * 存储成就奖励邮件的本地化文本
 */
struct AchievementRewardLocale
{
    std::vector<std::string> Subject;   ///< 本地化邮件主题
    std::vector<std::string> Text;      ///< 本地化邮件正文
};

/// 成就奖励本地化映射表
typedef std::unordered_map<uint32, AchievementRewardLocale> AchievementRewardLocales;

/**
 * @struct CompletedAchievementData
 * @brief 已完成成就数据结构
 *
 * 存储已完成成就的时间戳和状态
 */
struct CompletedAchievementData
{
    time_t date;                        ///< 完成时间（Unix时间戳）
    bool changed;                       ///< 是否已修改但尚未保存到数据库
};

/// 条件进度映射表（按条件ID索引）
typedef std::unordered_map<uint32, CriteriaProgress> CriteriaProgressMap;
/// 已完成成就映射表（按成就ID索引）
typedef std::unordered_map<uint32, CompletedAchievementData> CompletedAchievementMap;

/**
 * @enum ProgressType
 * @brief 进度更新类型枚举
 *
 * 定义进度更新的三种方式
 */
enum ProgressType
{
    PROGRESS_SET,                       ///< 设置为指定值（覆盖当前值）
    PROGRESS_ACCUMULATE,                ///< 累加到当前值
    PROGRESS_HIGHEST                    ///< 取当前值和新值中的较大值
};

/**
 * @class AchievementMgr
 * @brief 玩家成就管理器
 *
 * 负责管理单个玩家的成就系统，包括：
 * - 跟踪成就进度
 * - 检测成就完成
 * - 发放成就奖励
 * - 管理计时成就
 * - 保存/加载成就数据
 *
 * 每个玩家实例都有一个对应的AchievementMgr实例
 * 通过Player::GetAchievementMgr()获取
 */
class TC_GAME_API AchievementMgr
{
    public:
        /**
         * @brief 构造函数
         * @param player 关联的玩家对象
         */
        AchievementMgr(Player* player);

        /**
         * @brief 析构函数
         */
        ~AchievementMgr();

        /**
         * @brief 重置所有成就数据
         *
         * 清空所有已完成成就和进度，并重新检查所有条件
         * 用于成就重置功能
         */
        void Reset();

        /**
         * @brief 从数据库删除玩家的成就数据
         * @param lowguid 玩家的GUID
         *
         * 用于删除角色时清理数据
         */
        static void DeleteFromDB(ObjectGuid lowguid);

        /**
         * @brief 从数据库加载成就数据
         * @param achievementResult 已完成成就的查询结果
         * @param criteriaResult 条件进度的查询结果
         *
         * 在玩家登录时调用，加载玩家的成就数据
         */
        void LoadFromDB(PreparedQueryResult achievementResult, PreparedQueryResult criteriaResult);

        /**
         * @brief 保存成就数据到数据库
         * @param trans 数据库事务
         *
         * 保存所有已修改的成就数据到数据库
         * 在玩家登出或定期保存时调用
         */
        void SaveToDB(CharacterDatabaseTransaction trans);

        /**
         * @brief 重置特定条件的成就进度
         * @param condition 条件类型
         * @param value 条件值
         * @param evenIfCriteriaComplete 是否重置已完成的条件
         *
         * 用于特定事件发生时重置相关成就进度
         * 例如：更改阵营时重置阵营相关成就
         */
        void ResetAchievementCriteria(AchievementCriteriaCondition condition, uint32 value, bool evenIfCriteriaComplete);

        /**
         * @brief 更新成就条件进度
         * @param type 条件类型
         * @param miscValue1 杂项值1（根据条件类型有不同含义）
         * @param miscValue2 杂项值2（根据条件类型有不同含义）
         * @param ref 相关的世界对象引用
         *
         * 这是成就系统的核心函数，在玩家执行相关动作时被调用
         * 例如：击杀生物、完成任务、获得物品等
         *
         * @note 此函数会被频繁调用，性能敏感
         */
        void UpdateAchievementCriteria(AchievementCriteriaTypes type, uint32 miscValue1 = 0, uint32 miscValue2 = 0, WorldObject* ref = nullptr);

        /**
         * @brief 完成指定成就
         * @param entry 成就定义条目
         *
         * 处理成就完成逻辑：发送通知、发放奖励、触发关联成就等
         */
        void CompletedAchievement(AchievementEntry const* entry);

        /**
         * @brief 检查所有成就条件
         *
         * 在玩家登录时调用，检查所有可能已完成的成就
         * 用于处理成就系统禁用期间可能错过的成就
         */
        void CheckAllAchievementCriteria();

        /**
         * @brief 发送所有成就数据给客户端
         *
         * 发送玩家的所有成就和进度数据到客户端
         * 在登录时和查看成就界面时调用
         */
        void SendAllAchievementData() const;

        /**
         * @brief 发送成就数据给查看者
         * @param player 查看成就的玩家
         *
         * 用于"视察玩家"功能，发送成就数据给其他玩家查看
         */
        void SendRespondInspectAchievements(Player* player) const;

        /**
         * @brief 检查玩家是否已完成指定成就
         * @param achievementId 成就ID
         * @return 已完成返回true，否则返回false
         */
        bool HasAchieved(uint32 achievementId) const;

        /**
         * @brief 获取关联的玩家对象
         * @return 玩家对象指针
         */
        Player* GetPlayer() const { return m_player; }

        /**
         * @brief 更新计时成就
         * @param timeDiff 经过的时间（毫秒）
         *
         * 每帧调用，更新所有正在计时的成就
         * 超时的成就会被重置
         */
        void UpdateTimedAchievements(uint32 timeDiff);

        /**
         * @brief 启动计时成就
         * @param type 计时类型
         * @param entry 关联条目ID
         * @param timeLost 已损失的时间（毫秒）
         *
         * 启动一个限时完成的成就
         * 例如：限时副本成就、限时任务成就等
         */
        void StartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry, uint32 timeLost = 0);

        /**
         * @brief 移除计时成就
         * @param type 计时类型
         * @param entry 关联条目ID
         *
         * 用于取消限时成就的计时
         * 例如：任务失败时取消相关计时成就
         */
        void RemoveTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry);

    private:
        /**
         * @brief 发送成就完成通知
         * @param achievement 成就定义条目
         *
         * 向周围玩家和公会成员广播成就完成消息
         */
        void SendAchievementEarned(AchievementEntry const* achievement) const;

        /**
         * @brief 发送条件进度更新
         * @param entry 条件定义条目
         * @param progress 进度数据
         * @param timeElapsed 已经过时间（用于计时成就）
         * @param timedCompleted 计时成就是否完成
         */
        void SendCriteriaUpdate(AchievementCriteriaEntry const* entry, CriteriaProgress const* progress, uint32 timeElapsed, bool timedCompleted) const;

        /**
         * @brief 获取条件进度
         * @param entry 条件定义条目
         * @return 进度数据指针，不存在返回nullptr
         */
        CriteriaProgress* GetCriteriaProgress(AchievementCriteriaEntry const* entry);

        /**
         * @brief 设置条件进度
         * @param entry 条件定义条目
         * @param changeValue 变更值
         * @param ptype 进度更新类型
         */
        void SetCriteriaProgress(AchievementCriteriaEntry const* entry, uint32 changeValue, ProgressType ptype = PROGRESS_SET);

        /**
         * @brief 移除条件进度
         * @param entry 条件定义条目
         */
        void RemoveCriteriaProgress(AchievementCriteriaEntry const* entry);

        /**
         * @brief 处理条件完成
         * @param achievement 成就定义条目
         *
         * 当某个条件完成时检查并完成成就
         */
        void CompletedCriteriaFor(AchievementEntry const* achievement);

        /**
         * @brief 检查条件是否完成
         * @param achievementCriteria 条件定义条目
         * @param achievement 成就定义条目
         * @return 条件完成返回true
         */
        bool IsCompletedCriteria(AchievementCriteriaEntry const* achievementCriteria, AchievementEntry const* achievement);

        /**
         * @brief 检查成就是否完成
         * @param entry 成就定义条目
         * @return 成就完成返回true
         */
        bool IsCompletedAchievement(AchievementEntry const* entry);

        /**
         * @brief 检查是否可以更新条件进度
         * @param criteria 条件定义条目
         * @param achievement 成就定义条目
         * @param miscValue1 杂项值1
         * @param miscValue2 杂项值2
         * @param ref 相关对象引用
         * @return 可以更新返回true
         */
        bool CanUpdateCriteria(AchievementCriteriaEntry const* criteria, AchievementEntry const* achievement, uint32 miscValue1, uint32 miscValue2, WorldObject const* ref);

        /**
         * @brief 构建成就数据包
         * @param receiver 接收者
         * @param data 数据包指针
         */
        void BuildAllDataPacket(Player const* receiver, WorldPacket* data) const;

        /**
         * @brief 检查条件条件是否满足
         * @param criteria 条件定义条目
         * @return 条件满足返回true
         */
        bool ConditionsSatisfied(AchievementCriteriaEntry const* criteria) const;

        /**
         * @brief 检查条件要求是否满足
         * @param criteria 条件定义条目
         * @param achievement 成就定义条目
         * @param miscValue1 杂项值1
         * @param miscValue2 杂项值2
         * @param ref 相关对象引用
         * @return 要求满足返回true
         */
        bool RequirementsSatisfied(AchievementCriteriaEntry const* criteria, AchievementEntry const* achievement, uint32 miscValue1, uint32 miscValue2, WorldObject const* ref) const;

        Player* m_player;                               ///< 关联的玩家对象
        CriteriaProgressMap m_criteriaProgress;         ///< 条件进度映射表
        CompletedAchievementMap m_completedAchievements; ///< 已完成成就映射表
        typedef std::map<uint32, uint32> TimedAchievementMap;
        TimedAchievementMap m_timedAchievements;        ///< 计时成就映射表（条件ID -> 剩余时间毫秒）
};

/**
 * @class AchievementGlobalMgr
 * @brief 全局成就管理器（单例模式）
 *
 * 负责管理成就系统的全局数据，包括：
 * - 成就和条件定义的索引
 * - 成就奖励配置
 * - 服务器首杀成就状态
 * - 数据加载和缓存
 *
 * 该类是单例模式，通过sAchievementMgr宏访问
 * 所有数据在服务器启动时从DBC和数据库加载
 */
class TC_GAME_API AchievementGlobalMgr
{
        AchievementGlobalMgr() { }
        ~AchievementGlobalMgr() { }

    public:
        /**
         * @brief 获取条件类型的字符串表示
         * @param type 条件类型枚举
         * @return 条件类型名称字符串
         */
        static char const* GetCriteriaTypeString(AchievementCriteriaTypes type);

        /**
         * @brief 获取条件类型的字符串表示（重载）
         * @param type 条件类型数值
         * @return 条件类型名称字符串
         */
        static char const* GetCriteriaTypeString(uint32 type);

        /**
         * @brief 获取单例实例
         * @return AchievementGlobalMgr单例指针
         */
        static AchievementGlobalMgr* instance();

        /**
         * @brief 根据类型获取成就条件列表
         * @param type 条件类型
         * @param miscValue 杂项值（用于优化查找）
         * @return 条件列表引用
         *
         * 如果提供了miscValue，会优先查找特定值的条件列表
         * 这可以大大减少需要遍历的条件数量，提高性能
         */
        AchievementCriteriaEntryList const& GetAchievementCriteriaByType(AchievementCriteriaTypes type, uint32 miscValue) const;

        /**
         * @brief 根据计时类型获取成就条件列表
         * @param type 计时类型
         * @return 条件列表引用
         */
        AchievementCriteriaEntryList const& GetTimedAchievementCriteriaByType(AchievementCriteriaTimedTypes type) const
        {
            return m_AchievementCriteriasByTimedType[type];
        }

        /**
         * @brief 根据条件类型和值获取成就条件列表
         * @param condition 条件类型
         * @param val 条件值
         * @return 条件列表指针，不存在返回nullptr
         */
        AchievementCriteriaEntryList const* GetAchievementCriteriaByCondition(AchievementCriteriaCondition condition, uint32 val)
        {
            AchievementCriteriaListByCondition::const_iterator itr = m_AchievementCriteriasByCondition[condition].find(val);
            return itr != m_AchievementCriteriasByCondition[condition].end() ? &itr->second : nullptr;
        }

        /**
         * @brief 根据成就ID获取其所有条件
         * @param id 成就ID
         * @return 条件列表指针，不存在返回nullptr
         */
        AchievementCriteriaEntryList const* GetAchievementCriteriaByAchievement(uint32 id) const
        {
            AchievementCriteriaListByAchievement::const_iterator itr = m_AchievementCriteriaListByAchievement.find(id);
            return itr != m_AchievementCriteriaListByAchievement.end() ? &itr->second : nullptr;
        }

        /**
         * @brief 根据引用成就ID获取成就列表
         * @param id 被引用的成就ID
         * @return 成就列表指针，不存在返回nullptr
         *
         * 某些成就会引用其他成就作为条件
         * 此函数用于快速查找引用了指定成就的所有成就
         */
        AchievementEntryList const* GetAchievementByReferencedId(uint32 id) const
        {
            AchievementListByReferencedId::const_iterator itr = m_AchievementListByReferencedId.find(id);
            return itr != m_AchievementListByReferencedId.end() ? &itr->second : nullptr;
        }

        /**
         * @brief 获取成就奖励配置
         * @param achievement 成就定义条目
         * @return 奖励配置指针，不存在返回nullptr
         */
        AchievementReward const* GetAchievementReward(AchievementEntry const* achievement) const
        {
            AchievementRewards::const_iterator iter = m_achievementRewards.find(achievement->ID);
            return iter != m_achievementRewards.end() ? &iter->second : nullptr;
        }

        /**
         * @brief 获取成就奖励本地化配置
         * @param achievement 成就定义条目
         * @return 本地化配置指针，不存在返回nullptr
         */
        AchievementRewardLocale const* GetAchievementRewardLocale(AchievementEntry const* achievement) const
        {
            AchievementRewardLocales::const_iterator iter = m_achievementRewardLocales.find(achievement->ID);
            return iter != m_achievementRewardLocales.end() ? &iter->second : nullptr;
        }

        /**
         * @brief 获取条件数据集
         * @param achievementCriteria 条件定义条目
         * @return 数据集指针，不存在返回nullptr
         */
        AchievementCriteriaDataSet const* GetCriteriaDataSet(AchievementCriteriaEntry const* achievementCriteria) const
        {
            AchievementCriteriaDataMap::const_iterator iter = m_criteriaDataMap.find(achievementCriteria->ID);
            return iter != m_criteriaDataMap.end() ? &iter->second : nullptr;
        }

        /**
         * @brief 检查服务器首杀成就是否已完成
         * @param achievement 成就定义条目
         * @return 已完成返回true
         */
        bool IsRealmCompleted(AchievementEntry const* achievement) const;

        /**
         * @brief 设置服务器首杀成就已完成
         * @param achievement 成就定义条目
         */
        void SetRealmCompleted(AchievementEntry const* achievement);

        /**
         * @brief 加载成就条件列表
         *
         * 从DBC加载成就条件定义，并建立各种索引
         * 在服务器启动时调用
         */
        void LoadAchievementCriteriaList();

        /**
         * @brief 加载成就条件附加数据
         *
         * 从数据库加载条件的附加数据要求
         * 在服务器启动时调用
         */
        void LoadAchievementCriteriaData();

        /**
         * @brief 加载成就引用列表
         *
         * 建立成就之间的引用关系索引
         * 在服务器启动时调用
         */
        void LoadAchievementReferenceList();

        /**
         * @brief 加载已完成的成就
         *
         * 从数据库加载服务器上已完成的成就
         * 用于服务器首杀成就的检测
         */
        void LoadCompletedAchievements();

        /**
         * @brief 加载成就奖励配置
         *
         * 从数据库加载成就奖励（物品、称号等）
         * 在服务器启动时调用
         */
        void LoadRewards();

        /**
         * @brief 加载成就奖励本地化文本
         *
         * 从数据库加载多语言的奖励邮件文本
         * 在服务器启动时调用
         */
        void LoadRewardLocales();

        /**
         * @brief 根据ID获取成就定义
         * @param achievementId 成就ID
         * @return 成就定义指针，不存在返回nullptr
         */
        AchievementEntry const* GetAchievement(uint32 achievementId) const;

        /**
         * @brief 根据ID获取条件定义
         * @param achievementId 条件ID
         * @return 条件定义指针，不存在返回nullptr
         */
        AchievementCriteriaEntry const* GetAchievementCriteria(uint32 achievementId) const;
    private:
        AchievementCriteriaDataMap m_criteriaDataMap;   ///< 条件附加数据映射表

        // 按类型存储成就条件，加速查找
        AchievementCriteriaEntryList m_AchievementCriteriasByType[ACHIEVEMENT_CRITERIA_TYPE_TOTAL];

        /// 空条件列表（用于返回不存在的查找结果）
        static AchievementCriteriaEntryList const EmptyCriteriaList;

        // 按杂项值分割存储条件，进一步优化查找性能
        AchievementCriteriaListByMiscValue m_AchievementCriteriasByMiscValue[ACHIEVEMENT_CRITERIA_TYPE_TOTAL];

        /// 按计时类型存储的条件列表
        AchievementCriteriaEntryList m_AchievementCriteriasByTimedType[ACHIEVEMENT_TIMED_TYPE_MAX];

        /// 按条件类型存储的条件列表
        AchievementCriteriaListByCondition m_AchievementCriteriasByCondition[ACHIEVEMENT_CRITERIA_CONDITION_MAX];

        // 按成就ID存储的条件列表，加速查找
        AchievementCriteriaListByAchievement m_AchievementCriteriaListByAchievement;

        // 按引用成就ID存储的成就列表，加速查找
        AchievementListByReferencedId m_AchievementListByReferencedId;

        // 存储服务器首杀成就
        // SystemTimePoint::min() 表示尚未完成的服务器首杀占位符
        // SystemTimePoint::max() 表示在服务器启动前已完成的服务器首杀
        std::unordered_map<uint32 /*achievementId*/, SystemTimePoint /*completionTime*/> _allCompletedAchievements;

        AchievementRewards m_achievementRewards;        ///< 成就奖励配置表
        AchievementRewardLocales m_achievementRewardLocales; ///< 成就奖励本地化表

        friend class UnitTestDataLoader;
};

/// 全局成就管理器单例访问宏
#define sAchievementMgr AchievementGlobalMgr::instance()

#endif
