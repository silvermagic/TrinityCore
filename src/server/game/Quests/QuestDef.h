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

#ifndef TRINITYCORE_QUEST_H
#define TRINITYCORE_QUEST_H

#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "DBCEnums.h"
#include "SharedDefines.h"
#include "UniqueTrackablePtr.h"
#include "WorldPacket.h"
#include <vector>

class Player;

namespace WorldPackets::Quest
{
    struct QuestRewards;
}

// ========== 任务相关常量定义 ==========

/** @brief 玩家任务日志最大容量（最多同时接25个任务） */
#define MAX_QUEST_LOG_SIZE 25

/** @brief 任务目标数量上限（击杀、交互等目标，最多4个） */
#define QUEST_OBJECTIVES_COUNT 4

/** @brief 任务物品目标数量上限（需要收集的物品种类，最多6个） */
#define QUEST_ITEM_OBJECTIVES_COUNT 6

/** @brief 任务源物品ID数量上限（任务物品来源，最多4个） */
#define QUEST_SOURCE_ITEM_IDS_COUNT 4

/** @brief 任务可选奖励数量上限（玩家可选择其中之一，最多6个） */
#define QUEST_REWARD_CHOICES_COUNT 6

/** @brief 任务固定奖励数量上限（必定获得的物品，最多4个） */
#define QUEST_REWARDS_COUNT 4

/** @brief 任务取消链接数量上限 */
#define QUEST_DEPLINK_COUNT 10

/** @brief 任务声望奖励数量上限（最多奖励5个阵营声望） */
#define QUEST_REPUTATIONS_COUNT 5

/** @brief 任务表情数量上限（对话时的表情动画，最多4个） */
#define QUEST_EMOTE_COUNT 4

/** @brief PvP击杀目标槽位索引 */
#define QUEST_PVP_KILL_SLOT 0

/**
 * @brief 任务失败原因枚举
 *
 * 定义了各种任务无法接取或完成的原因，用于向玩家显示错误信息。
 */
// EnumUtils: DESCRIBE THIS
enum QuestFailedReason : uint32
{
    INVALIDREASON_DONT_HAVE_REQ                 = 0,   // 不满足要求（通用错误）
    INVALIDREASON_QUEST_FAILED_LOW_LEVEL        = 1,   // DESCRIPTION You are not high enough level for that quest. - 等级不足
    INVALIDREASON_QUEST_FAILED_WRONG_RACE       = 6,   // DESCRIPTION That quest is not available to your race. - 种族不符
    INVALIDREASON_QUEST_ALREADY_DONE            = 7,   // DESCRIPTION You have completed that quest. - 已完成该任务
    INVALIDREASON_QUEST_ONLY_ONE_TIMED          = 12,  // DESCRIPTION You can only be on one timed quest at a time. - 只能同时进行一个限时任务
    INVALIDREASON_QUEST_ALREADY_ON              = 13,  // DESCRIPTION You are already on that quest. - 已接取该任务
    INVALIDREASON_QUEST_FAILED_EXPANSION        = 16,  // DESCRIPTION This quest requires an expansion enabled account. - 需要扩展包
    INVALIDREASON_QUEST_ALREADY_ON2             = 18,  // DESCRIPTION You are already on that quest. - 已接取该任务（重复）
    INVALIDREASON_QUEST_FAILED_MISSING_ITEMS    = 21,  // DESCRIPTION You don't have the required items with you. Check storage. - 缺少所需物品
    INVALIDREASON_QUEST_FAILED_NOT_ENOUGH_MONEY = 23,  // DESCRIPTION You don't have enough money for that quest. - 金钱不足
    INVALIDREASON_DAILY_QUESTS_REMAINING        = 26,  // DESCRIPTION You have already completed 25 daily quests today. - 今日已完成25个日常任务
    INVALIDREASON_QUEST_FAILED_CAIS             = 27,  // DESCRIPTION You cannot complete quests once you have reached tired time. - 达到疲劳时间
    INVALIDREASON_DAILY_QUEST_COMPLETED_TODAY   = 29   // DESCRIPTION You have completed that daily quest today. - 今日已完成该日常任务
};

/**
 * @brief 任务分享消息类型枚举
 *
 * 定义了任务分享过程中可能出现的各种消息类型。
 */
// EnumUtils: DESCRIBE THIS
enum QuestShareMessages : uint8
{
    QUEST_PARTY_MSG_SHARING_QUEST           = 0,   // 正在分享任务
    QUEST_PARTY_MSG_CANT_TAKE_QUEST         = 1,   // 无法接取任务
    QUEST_PARTY_MSG_ACCEPT_QUEST            = 2,   // 接受任务
    QUEST_PARTY_MSG_DECLINE_QUEST           = 3,   // 拒绝任务
    QUEST_PARTY_MSG_BUSY                    = 4,   // 正忙
    QUEST_PARTY_MSG_LOG_FULL                = 5,   // 任务日志已满
    QUEST_PARTY_MSG_HAVE_QUEST              = 6,   // 已有该任务
    QUEST_PARTY_MSG_FINISH_QUEST            = 7,   // 完成任务
    QUEST_PARTY_MSG_CANT_BE_SHARED_TODAY    = 8,   // 今日无法分享
    QUEST_PARTY_MSG_SHARING_TIMER_EXPIRED   = 9,   // 分享计时器已过期
    QUEST_PARTY_MSG_NOT_IN_PARTY            = 10,  // 不在队伍中
    QUEST_PARTY_MSG_NOT_ELIGIBLE_TODAY      = 11   // 今日不符合条件
};

/**
 * @brief 任务专业技能枚举
 *
 * 定义了任务可能关联的专业技能类型。
 */
enum QuestTradeSkill
{
    QUEST_TRSKILL_NONE           = 0,   // 无专业技能要求
    QUEST_TRSKILL_ALCHEMY        = 1,   // 炼金术
    QUEST_TRSKILL_BLACKSMITHING  = 2,   // 锻造
    QUEST_TRSKILL_COOKING        = 3,   // 烹饪
    QUEST_TRSKILL_ENCHANTING     = 4,   // 附魔
    QUEST_TRSKILL_ENGINEERING    = 5,   // 工程学
    QUEST_TRSKILL_FIRSTAID       = 6,   // 急救
    QUEST_TRSKILL_HERBALISM      = 7,   // 草药学
    QUEST_TRSKILL_LEATHERWORKING = 8,   // 制皮
    QUEST_TRSKILL_POISONS        = 9,   // 毒药（已废弃）
    QUEST_TRSKILL_TAILORING      = 10,  // 裁缝
    QUEST_TRSKILL_MINING         = 11,  // 采矿
    QUEST_TRSKILL_FISHING        = 12,  // 钓鱼
    QUEST_TRSKILL_SKINNING       = 13,  // 剥皮
    QUEST_TRSKILL_JEWELCRAFTING  = 14   // 珠宝加工
};

/**
 * @brief 任务状态枚举
 *
 * 定义了任务可能处于的各种状态。
 */
enum QuestStatus : uint8
{
    QUEST_STATUS_NONE           = 0,   // 无状态（未接取）
    QUEST_STATUS_COMPLETE       = 1,   // 已完成（可提交）
    //QUEST_STATUS_UNAVAILABLE    = 2, // 已废弃
    QUEST_STATUS_INCOMPLETE     = 3,   // 未完成（进行中）
    //QUEST_STATUS_AVAILABLE      = 4, // 已废弃
    QUEST_STATUS_FAILED         = 5,   // 任务失败
    QUEST_STATUS_REWARDED       = 6,   // 已领取奖励（不存储在数据库中）
    MAX_QUEST_STATUS
};

/**
 * @brief 任务给予者状态枚举
 *
 * 定义了NPC头上的任务图标状态，用于显示任务是否可接、可完成等。
 */
enum QuestGiverStatus
{
    DIALOG_STATUS_NONE                     = 0,   // 无任务
    DIALOG_STATUS_UNAVAILABLE              = 1,   // 任务不可用
    DIALOG_STATUS_LOW_LEVEL_AVAILABLE      = 2,   // 等级不足但可接（灰色感叹号）
    DIALOG_STATUS_LOW_LEVEL_REWARD_REP     = 3,   // 等级不足，可完成且有声望奖励
    DIALOG_STATUS_LOW_LEVEL_AVAILABLE_REP  = 4,   // 等级不足，可接且有声望奖励
    DIALOG_STATUS_INCOMPLETE               = 5,   // 任务未完成（灰色问号）
    DIALOG_STATUS_REWARD_REP               = 6,   // 可完成且有声望奖励
    DIALOG_STATUS_AVAILABLE_REP            = 7,   // 可接且有声望奖励
    DIALOG_STATUS_AVAILABLE                = 8,   // 可接取（黄色感叹号）
    DIALOG_STATUS_REWARD2                  = 9,   // 可完成（无小地图黄点）
    DIALOG_STATUS_REWARD                   = 10,  // 可完成（有小地图黄点，黄色问号）
};

/**
 * @brief 任务标志枚举
 *
 * 定义了任务的各种标志位，用于控制任务的特殊行为。
 * 这些标志既用于服务器端逻辑，也会发送给客户端。
 */
enum QuestFlags
{
    // 服务器和客户端共用的标志
    QUEST_FLAGS_NONE                    = 0x00000000,  // 无标志
    QUEST_FLAGS_STAY_ALIVE              = 0x00000001,  // 未使用：保持存活
    QUEST_FLAGS_PARTY_ACCEPT            = 0x00000002,  // 未使用：队伍接受（如果玩家在队伍中，所有能接受该任务的玩家都会收到确认框）
    QUEST_FLAGS_EXPLORATION             = 0x00000004,  // 未使用：探索任务
    QUEST_FLAGS_SHARABLE                = 0x00000008,  // 可分享：Player::CanShareQuest()
    QUEST_FLAGS_HAS_CONDITION           = 0x00000010,  // 未使用：有条件
    QUEST_FLAGS_HIDE_REWARD_POI         = 0x00000020,  // 未使用：隐藏奖励POI
    QUEST_FLAGS_RAID                    = 0x00000040,  // 可在团队副本中完成
    QUEST_FLAGS_TBC                     = 0x00000080,  // 未使用：需要TBC扩展包
    QUEST_FLAGS_NO_MONEY_FROM_XP        = 0x00000100,  // 未使用：满级时经验不转换为金钱
    QUEST_FLAGS_HIDDEN_REWARDS          = 0x00000200,  // 隐藏奖励：物品和金钱奖励只在奖励界面显示，不显示在任务日志中
    QUEST_FLAGS_TRACKING                = 0x00000400,  // 追踪任务：任务完成时自动奖励，不会出现在任务日志中
    QUEST_FLAGS_DEPRECATE_REPUTATION    = 0x00000800,  // 未使用：废弃声望
    QUEST_FLAGS_DAILY                   = 0x00001000,  // 日常任务
    QUEST_FLAGS_FLAGS_PVP               = 0x00002000,  // PvP任务：拥有此任务会强制开启PvP标志
    QUEST_FLAGS_UNAVAILABLE             = 0x00004000,  // 不可用：用于非通用可用的任务
    QUEST_FLAGS_WEEKLY                  = 0x00008000,  // 周常任务
    QUEST_FLAGS_AUTOCOMPLETE            = 0x00010000,  // 自动完成
    QUEST_FLAGS_DISPLAY_ITEM_IN_TRACKER = 0x00020000,  // 在任务追踪器中显示可用物品
    QUEST_FLAGS_OBJ_TEXT                = 0x00040000,  // 使用目标文本作为完成文本
    QUEST_FLAGS_AUTO_ACCEPT             = 0x00080000,  // 自动接受：客户端识别此标志为自动接受（3.3.5a版本中未使用）

    // ... 4.x 版本添加了更多标志，直到 0x80000000 - 目前未知
};

/**
 * @brief 任务特殊标志枚举
 *
 * 定义了服务器端专用的任务特殊标志，这些标志不发送给客户端。
 * 可以在数据库的 SpecialFlags 字段中设置。
 */
enum QuestSpecialFlags
{
    QUEST_SPECIAL_FLAGS_NONE                 = 0x000,  // 无特殊标志

    // Trinity自定义标志，在数据库 SpecialFlags 字段中设置，仅用于服务器端

    QUEST_SPECIAL_FLAGS_REPEATABLE           = 0x001,  // 可重复（DB中SpecialFlags=1）
    QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT = 0x002,  // 探索或事件任务（DB中SpecialFlags=2）：需要区域探索、法术施放、脚本触发等
    QUEST_SPECIAL_FLAGS_AUTO_ACCEPT          = 0x004,  // 自动接受（DB中SpecialFlags=4）：任务会自动被接受
    QUEST_SPECIAL_FLAGS_DF_QUEST             = 0x008,  // 副本查找器任务（DB中SpecialFlags=8）：用于副本查找器
    QUEST_SPECIAL_FLAGS_MONTHLY              = 0x010,  // 月常任务（DB中SpecialFlags=16）：每月初重置
    QUEST_SPECIAL_FLAGS_CAST                 = 0x020,  // 施法任务（DB中SpecialFlags=32）：需要施法而非击杀
    // 可在此添加更多自定义标志

    // 数据库允许设置的特殊标志位掩码
    QUEST_SPECIAL_FLAGS_DB_ALLOWED = QUEST_SPECIAL_FLAGS_REPEATABLE | QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT | QUEST_SPECIAL_FLAGS_AUTO_ACCEPT | QUEST_SPECIAL_FLAGS_DF_QUEST | QUEST_SPECIAL_FLAGS_MONTHLY | QUEST_SPECIAL_FLAGS_CAST,

    // 内部计算的标志（不在数据库中设置）

    QUEST_SPECIAL_FLAGS_DELIVER              = 0x080,   // 送货任务（内部计算）
    QUEST_SPECIAL_FLAGS_SPEAKTO              = 0x100,   // 对话任务（内部计算）
    QUEST_SPECIAL_FLAGS_KILL                 = 0x200,   // 击杀任务（内部计算）
    QUEST_SPECIAL_FLAGS_TIMED                = 0x400,   // 限时任务（内部计算）
    QUEST_SPECIAL_FLAGS_PLAYER_KILL          = 0x800,   // 玩家击杀任务（内部计算）
    QUEST_SPECIAL_FLAGS_COMPLETED_AT_START   = 0x1000   // 开始时已完成（内部计算）
};

/**
 * @brief 任务本地化数据结构
 *
 * 存储任务的多语言文本信息，用于支持不同语言环境的客户端。
 */
struct QuestLocale
{
    /** @brief 构造函数，初始化目标文本数组大小 */
    QuestLocale() { ObjectiveText.resize(QUEST_OBJECTIVES_COUNT); }

    std::vector<std::string> Title;              // 任务标题
    std::vector<std::string> Details;            // 任务详情文本
    std::vector<std::string> Objectives;         // 任务目标文本
    std::vector<std::string> OfferRewardText;    // 奖励提示文本
    std::vector<std::string> RequestItemsText;   // 请求物品文本
    std::vector<std::string> AreaDescription;    // 区域描述文本
    std::vector<std::string> CompletedText;      // 完成文本
    std::vector<std::vector<std::string>> ObjectiveText; // 目标文本（每个目标的多语言版本）
};

/**
 * @brief 任务请求物品本地化数据结构
 *
 * 存储任务请求物品时的多语言文本。
 */
struct QuestRequestItemsLocale
{
    std::vector<std::string> CompletionText;  // 完成文本
};

/**
 * @brief 任务奖励本地化数据结构
 *
 * 存储任务奖励时的多语言文本。
 */
struct QuestOfferRewardLocale
{
    std::vector<std::string> RewardText;  // 奖励文本
};

/**
 * @brief 任务类 - 提供访问任务详细信息的主要接口
 *
 * 这个类提供了便捷的方式来访问预计算（缓存）的任务详情，
 * 包含所有基础任务信息，以及各种工具函数（如计算经验值奖励）。
 *
 * 任务数据来自多个数据库表：
 * - quest_template: 主要任务模板数据
 * - quest_template_addon: 自定义扩展数据
 * - quest_details: 任务详情（表情等）
 * - quest_request_items: 请求物品时的文本
 * - quest_offer_reward: 提供奖励时的文本
 */
class TC_GAME_API Quest
{
    friend class ObjectMgr;
    public:
        /**
         * @brief 构造函数 - 从数据库记录初始化任务数据
         * @param questRecord 数据库字段记录指针
         */
        Quest(Field* questRecord);

        /**
         * @brief 加载任务详情数据（表情动画等）
         * @param fields 数据库字段数组
         */
        void LoadQuestDetails(Field* fields);

        /**
         * @brief 加载任务请求物品时的对话文本
         * @param fields 数据库字段数组
         */
        void LoadQuestRequestItems(Field* fields);

        /**
         * @brief 加载任务奖励时的对话文本
         * @param fields 数据库字段数组
         */
        void LoadQuestOfferReward(Field* fields);

        /**
         * @brief 加载任务模板扩展数据
         * @param fields 数据库字段数组
         */
        void LoadQuestTemplateAddon(Field* fields);

        /**
         * @brief 加载任务奖励邮件的发送者信息
         * @param fields 数据库字段数组
         */
        void LoadQuestMailSender(Field* fields);

        /**
         * @brief 获取任务的经验值奖励
         * @param player 玩家指针（用于根据玩家等级计算）
         * @return 经验值数量
         */
        uint32 GetXPReward(Player const* player) const;

        /**
         * @brief 检查任务是否具有指定标志
         * @param flag 要检查的标志位
         * @return 如果具有该标志返回true
         */
        bool HasFlag(uint32 flag) const { return (_flags & flag) != 0; }

        /**
         * @brief 设置任务标志
         * @param flag 要设置的标志位
         */
        void SetFlag(uint32 flag) { _flags |= flag; }

        /**
         * @brief 检查任务是否具有特殊标志
         * @param flag 要检查的特殊标志位
         * @return 如果具有该特殊标志返回true
         */
        bool HasSpecialFlag(uint32 flag) const { return (_specialFlags & flag) != 0; }

        /**
         * @brief 设置任务特殊标志
         * @param flag 要设置的特殊标志位
         */
        void SetSpecialFlag(uint32 flag) { _specialFlags |= flag; }

        /**
         * @brief 检查任务是否全局启用（由池生成、游戏事件激活等）
         * @param questId 任务ID
         * @return 如果任务已启用返回true
         */
        static bool IsTakingQuestEnabled(uint32 questId);

        // ========== 数据库表数据访问器 ==========

        /** @brief 获取任务ID */
        uint32 GetQuestId() const { return _id; }

        /** @brief 获取任务方法（0=自动接受，1=需要接受） */
        uint32 GetQuestMethod() const { return _method; }

        /** @brief 获取区域或排序ID（正数为区域ID，负数为任务分类） */
        int32  GetZoneOrSort() const { return _zoneOrSort; }

        /** @brief 获取最低等级要求 */
        uint32 GetMinLevel() const { return _minLevel; }

        /** @brief 获取最高等级限制 */
        uint32 GetMaxLevel() const { return _maxLevel; }

        /** @brief 获取任务等级（-1表示根据玩家等级缩放） */
        int32  GetQuestLevel() const { return _level; }

        /** @brief 获取任务类型（0=普通，1=组队，41= PvP等） */
        uint32 GetType() const { return _type; }

        /** @brief 获取职业要求位掩码 */
        uint32 GetRequiredClasses() const { return _requiredClasses; }

        /** @brief 获取允许的种族位掩码 */
        uint32 GetAllowableRaces() const { return _allowableRaces; }

        /** @brief 获取所需技能ID */
        uint32 GetRequiredSkill() const { return _requiredSkillId; }

        /** @brief 获取所需技能点数 */
        uint32 GetRequiredSkillValue() const { return _requiredSkillPoints; }

        /** @brief 获取声望目标阵营1 */
        uint32 GetRepObjectiveFaction() const { return _requiredFactionId1; }

        /** @brief 获取声望目标值1 */
        int32  GetRepObjectiveValue() const { return _requiredFactionValue1; }

        /** @brief 获取声望目标阵营2 */
        uint32 GetRepObjectiveFaction2() const { return _requiredFactionId2; }

        /** @brief 获取声望目标值2 */
        int32  GetRepObjectiveValue2() const { return _requiredFactionValue2; }

        /** @brief 获取最小声望要求阵营 */
        uint32 GetRequiredMinRepFaction() const { return _requiredMinRepFaction; }

        /** @brief 获取最小声望值要求 */
        int32  GetRequiredMinRepValue() const { return _requiredMinRepValue; }

        /** @brief 获取最大声望要求阵营 */
        uint32 GetRequiredMaxRepFaction() const { return _requiredMaxRepFaction; }

        /** @brief 获取最大声望值限制 */
        int32  GetRequiredMaxRepValue() const { return _requiredMaxRepValue; }

        /** @brief 获取建议玩家数量 */
        uint32 GetSuggestedPlayers() const { return _suggestedPlayers; }

        /** @brief 获取时间限制（秒，0表示无限制） */
        uint32 GetTimeAllowed() const { return _timeAllowed; }

        /** @brief 获取前置任务ID */
        int32  GetPrevQuestId() const { return _prevQuestId; }

        /** @brief 获取下一个任务ID */
        uint32 GetNextQuestId() const { return _nextQuestId; }

        /** @brief 获取互斥组ID（同组任务只能接一个） */
        int32  GetExclusiveGroup() const { return _exclusiveGroup; }

        /** @brief 获取面包屑任务ID（引导玩家到该任务的任务） */
        int32  GetBreadcrumbForQuestId() const { return _breadcrumbForQuestId; }

        /** @brief 获取任务链中下一个任务ID */
        uint32 GetNextQuestInChain() const { return _rewardNextQuest; }

        /** @brief 获取奖励的称号ID */
        uint32 GetCharTitleId() const { return _rewardTitleId; }

        /** @brief 获取需要击杀的玩家数量（PvP任务） */
        uint32 GetPlayersSlain() const { return _requiredPlayerKills; }

        /** @brief 获取奖励的天赋点数 */
        uint32 GetBonusTalents() const { return _rewardTalents; }

        /** @brief 获取奖励的竞技场点数 */
        int32  GetRewArenaPoints() const {return _rewardArenaPoints; }

        /** @brief 获取经验值难度ID */
        uint32 GetXPId() const { return _rewardXPDifficulty; }

        /** @brief 获取起始物品ID */
        uint32 GetSrcItemId() const { return _startItem; }

        /** @brief 获取起始物品数量 */
        uint32 GetSrcItemCount() const { return _startItemCount; }

        /** @brief 获取起始法术ID */
        uint32 GetSrcSpell() const { return _sourceSpellid; }

        /** @brief 获取任务标题 */
        std::string const& GetTitle() const { return _title; }

        /** @brief 获取任务详情文本 */
        std::string const& GetDetails() const { return _details; }

        /** @brief 获取任务目标文本 */
        std::string const& GetObjectives() const { return _objectives; }

        /** @brief 获取奖励提示文本 */
        std::string const& GetOfferRewardText() const { return _offerRewardText; }

        /** @brief 获取请求物品时的文本 */
        std::string const& GetRequestItemsText() const { return _requestItemsText; }

        /** @brief 获取区域描述文本 */
        std::string const& GetAreaDescription() const { return _areaDescription; }

        /** @brief 获取任务完成文本 */
        std::string const& GetCompletedText() const { return _completedText; }

        /**
         * @brief 获取奖励或要求的金钱数量
         * @param player 玩家指针（用于等级缩放，可为nullptr）
         * @return 金钱数量（正数为奖励，负数为要求）
         */
        int32 GetRewOrReqMoney(Player const* player = nullptr) const;

        /** @brief 获取固定荣誉奖励值 */
        uint32 GetRewHonorAddition() const { return _rewardHonor; }

        /** @brief 获取荣誉乘数（用于击杀荣誉计算） */
        float GetRewHonorMultiplier() const { return _rewardKillHonor; }

        /** @brief 获取满级时的金钱奖励（用于客户端经验计算） */
        uint32 GetRewMoneyMaxLevel() const;

        /** @brief 获取奖励法术显示ID */
        uint32 GetRewSpell() const { return _rewardDisplaySpell; }

        /** @brief 获取奖励法术施放ID（实际施放的法术） */
        int32 GetRewSpellCast() const { return _rewardSpell; }

        /** @brief 获取奖励邮件模板ID */
        uint32 GetRewMailTemplateId() const { return _rewardMailTemplateId; }

        /** @brief 获取奖励邮件延迟时间（秒） */
        uint32 GetRewMailDelaySecs() const { return _rewardMailDelay; }

        /** @brief 获取奖励邮件发送者Entry */
        uint32 GetRewMailSenderEntry() const { return _rewardMailSenderEntry; }

        /** @brief 获取POI（兴趣点）地图ID */
        uint32 GetPOIContinent() const { return _poiContinent; }

        /** @brief 获取POI X坐标 */
        float GetPOIx() const { return _poiX; }

        /** @brief 获取POI Y坐标 */
        float GetPOIy() const { return _poiY; }

        /** @brief 获取POI优先级 */
        uint32 GetPointOpt() const { return _poiPriority; }

        /** @brief 获取任务未完成时的表情ID */
        uint32 GetIncompleteEmote() const { return _emoteOnIncomplete; }

        /** @brief 获取任务完成时的表情ID */
        uint32 GetCompleteEmote() const { return _emoteOnComplete; }

        /** @brief 检查任务是否可重复 */
        bool IsRepeatable() const { return _specialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE; }

        /** @brief 检查任务是否自动接受 */
        bool IsAutoAccept() const;

        /** @brief 检查任务是否自动完成 */
        bool IsAutoComplete() const;

        /** @brief 获取任务标志位掩码 */
        uint32 GetFlags() const { return _flags; }

        /** @brief 检查是否为日常任务 */
        bool IsDaily() const { return (_flags & QUEST_FLAGS_DAILY) != 0; }

        /** @brief 检查是否为周常任务 */
        bool IsWeekly() const { return (_flags & QUEST_FLAGS_WEEKLY) != 0; }

        /** @brief 检查是否为月常任务 */
        bool IsMonthly() const { return (_specialFlags & QUEST_SPECIAL_FLAGS_MONTHLY) != 0; }

        /** @brief 检查是否为季节性任务 */
        bool IsSeasonal() const { return (_zoneOrSort == -QUEST_SORT_SEASONAL || _zoneOrSort == -QUEST_SORT_SPECIAL || _zoneOrSort == -QUEST_SORT_LUNAR_FESTIVAL || _zoneOrSort == -QUEST_SORT_MIDSUMMER || _zoneOrSort == -QUEST_SORT_BREWFEST || _zoneOrSort == -QUEST_SORT_LOVE_IS_IN_THE_AIR || _zoneOrSort == -QUEST_SORT_NOBLEGARDEN) && !IsRepeatable(); }

        /** @brief 检查是否为日常或周常任务 */
        bool IsDailyOrWeekly() const { return (_flags & (QUEST_FLAGS_DAILY | QUEST_FLAGS_WEEKLY)) != 0; }

        /**
         * @brief 检查是否为团队副本任务
         * @param difficulty 难度设置
         * @return 如果是团队副本任务返回true
         */
        bool IsRaidQuest(Difficulty difficulty) const;

        /**
         * @brief 检查任务是否允许在团队中完成
         * @param difficulty 难度设置
         * @return 如果允许在团队中完成返回true
         */
        bool IsAllowedInRaid(Difficulty difficulty) const;

        /** @brief 检查是否为副本查找器任务 */
        bool IsDFQuest() const { return (_specialFlags & QUEST_SPECIAL_FLAGS_DF_QUEST) != 0; }

        /**
         * @brief 计算荣誉奖励
         * @param level 玩家等级
         * @return 荣誉值
         */
        uint32 CalculateHonorGain(uint8 level) const;

        /**
         * @brief 检查是否可以增加已奖励任务计数器
         * @return 如果可以增加返回true
         */
        bool CanIncreaseRewardedQuestCounters() const;

        // ========== 多值成员变量（数组形式） ==========

        /** @brief 目标文本（最多4个目标） */
        std::string ObjectiveText[QUEST_OBJECTIVES_COUNT];

        /** @brief 需要的物品ID（最多6个物品目标） */
        uint32 RequiredItemId[QUEST_ITEM_OBJECTIVES_COUNT] = { };

        /** @brief 需要的物品数量（对应RequiredItemId） */
        uint32 RequiredItemCount[QUEST_ITEM_OBJECTIVES_COUNT] = { };

        /** @brief 物品掉落来源ID（任务物品来源，最多4个） */
        uint32 ItemDrop[QUEST_SOURCE_ITEM_IDS_COUNT] = { };

        /** @brief 物品掉落数量（对应ItemDrop） */
        uint32 ItemDropQuantity[QUEST_SOURCE_ITEM_IDS_COUNT] = { };

        /** @brief 需要的NPC或GameObject ID（最多4个，>0为生物，<0为游戏对象） */
        int32 RequiredNpcOrGo[QUEST_OBJECTIVES_COUNT] = { };

        /** @brief 需要的NPC或GameObject击杀/交互数量 */
        uint32 RequiredNpcOrGoCount[QUEST_OBJECTIVES_COUNT] = { };

        /** @brief 可选奖励物品ID（最多6个供选择） */
        uint32 RewardChoiceItemId[QUEST_REWARD_CHOICES_COUNT] = { };

        /** @brief 可选奖励物品数量（对应RewardChoiceItemId） */
        uint32 RewardChoiceItemCount[QUEST_REWARD_CHOICES_COUNT] = { };

        /** @brief 固定奖励物品ID（最多4个） */
        uint32 RewardItemId[QUEST_REWARDS_COUNT] = { };

        /** @brief 固定奖励物品数量（对应RewardItemId） */
        uint32 RewardItemIdCount[QUEST_REWARDS_COUNT] = { };

        /** @brief 奖励声望阵营ID（最多5个） */
        uint32 RewardFactionId[QUEST_REPUTATIONS_COUNT] = { };

        /** @brief 奖励声望值ID（对应RewardFactionId） */
        int32 RewardFactionValueId[QUEST_REPUTATIONS_COUNT] = { };

        /** @brief 奖励声望值覆盖（用于自定义声望奖励） */
        int32 RewardFactionValueIdOverride[QUEST_REPUTATIONS_COUNT] = { };

        /** @brief 任务详情对话时的表情ID（最多4个） */
        uint32 DetailsEmote[QUEST_EMOTE_COUNT] = { };

        /** @brief 任务详情对话时的表情延迟（毫秒） */
        uint32 DetailsEmoteDelay[QUEST_EMOTE_COUNT] = { };

        /** @brief 提供奖励时的表情ID（最多4个） */
        uint32 OfferRewardEmote[QUEST_EMOTE_COUNT] = { };

        /** @brief 提供奖励时的表情延迟（毫秒） */
        uint32 OfferRewardEmoteDelay[QUEST_EMOTE_COUNT] = { };

        // ========== 数量统计访问器 ==========

        /** @brief 获取需要物品的数量 */
        uint32 GetReqItemsCount() const { return _reqItemsCount; }

        /** @brief 获取需要击杀/交互的生物或游戏对象数量 */
        uint32 GetReqCreatureOrGOcount() const { return _reqCreatureOrGOcount; }

        /** @brief 获取可选奖励物品数量 */
        uint32 GetRewChoiceItemsCount() const { return _rewChoiceItemsCount; }

        /** @brief 获取固定奖励物品数量 */
        uint32 GetRewItemsCount() const { return _rewItemsCount; }

        /**
         * @brief 设置任务关联的游戏事件ID
         * @param eventId 游戏事件ID
         */
        void SetEventIdForQuest(uint16 eventId) { _eventIdForQuest = eventId; }

        /** @brief 获取任务关联的游戏事件ID */
        uint16 GetEventIdForQuest() const { return _eventIdForQuest; }

        /**
         * @brief 向任务标题添加等级前缀
         * @param title 任务标题（会被修改）
         * @param level 任务等级
         */
        static void AddQuestLevelToTitle(std::string& title, int32 level);

        /** @brief 初始化查询数据（为所有语言环境构建数据包） */
        void InitializeQueryData();

        /**
         * @brief 构建任务查询数据包
         * @param loc 语言环境
         * @return 世界数据包
         */
        WorldPacket BuildQueryData(LocaleConstant loc) const;

        /**
         * @brief 构建任务奖励数据
         * @param rewards 奖励结构体引用（输出）
         * @param player 玩家指针
         * @param sendHiddenRewards 是否发送隐藏的奖励
         */
        void BuildQuestRewards(WorldPackets::Quest::QuestRewards& rewards, Player* player, bool sendHiddenRewards = false) const;

        /** @brief 获取任务的弱引用指针 */
        Trinity::unique_weak_ptr<Quest> GetWeakPtr() const { return _weakRef; }

        /** @brief 依赖的前置任务ID列表 */
        std::vector<uint32> DependentPreviousQuests;

        /** @brief 依赖的面包屑任务ID列表 */
        std::vector<uint32> DependentBreadcrumbQuests;

        /** @brief 各语言环境的查询数据包（缓存） */
        WorldPacket QueryData[TOTAL_LOCALES];

        // ========== 缓存的计算数据 ==========
    private:
        /** @brief 需要的物品种类数量（缓存） */
        uint32 _reqItemsCount = 0;

        /** @brief 需要击杀/交互的生物或游戏对象种类数量（缓存） */
        uint32 _reqCreatureOrGOcount = 0;

        /** @brief 可选奖励物品种类数量（缓存） */
        uint32 _rewChoiceItemsCount = 0;

        /** @brief 固定奖励物品种类数量（缓存） */
        uint32 _rewItemsCount = 0;

        /** @brief 关联的游戏事件ID（用于季节性任务等） */
        uint16 _eventIdForQuest = 0;

        // ========== quest_template 表数据 ==========

        /** @brief 任务ID（唯一标识） */
        uint32 _id = 0;

        /** @brief 任务方法（0=自动接受，1=需要与NPC对话接受） */
        uint32 _method = 0;

        /** @brief 区域或排序ID（正数=区域ID，负数=任务分类ID） */
        int32 _zoneOrSort = 0;

        /** @brief 最低等级要求 */
        uint32 _minLevel = 0;

        /** @brief 任务等级（-1表示根据玩家等级缩放） */
        int32 _level = 0;

        /** @brief 任务类型（0=普通，1=组队，41=PvP，62=团队副本，81=地下城等） */
        uint32 _type = 0;

        /** @brief 允许的种族位掩码 */
        uint32 _allowableRaces = 0;

        /** @brief 所需声望阵营1 */
        uint32 _requiredFactionId1 = 0;

        /** @brief 所需声望值1 */
        int32 _requiredFactionValue1 = 0;

        /** @brief 所需声望阵营2 */
        uint32 _requiredFactionId2 = 0;

        /** @brief 所需声望值2 */
        int32 _requiredFactionValue2 = 0;

        /** @brief 建议的玩家数量 */
        uint32 _suggestedPlayers = 0;

        /** @brief 时间限制（秒，0表示无时间限制） */
        uint32 _timeAllowed = 0;

        /** @brief 任务标志位掩码 */
        uint32 _flags = 0;

        /** @brief 奖励的称号ID */
        uint32 _rewardTitleId = 0;

        /** @brief 需要击杀的玩家数量（PvP任务） */
        uint32 _requiredPlayerKills = 0;

        /** @brief 奖励的天赋点数 */
        uint32 _rewardTalents = 0;

        /** @brief 奖励的竞技场点数 */
        int32 _rewardArenaPoints = 0;

        /** @brief 任务链中下一个任务的ID */
        uint32 _rewardNextQuest = 0;

        /** @brief 经验值难度ID（用于查找经验值奖励） */
        uint32 _rewardXPDifficulty = 0;

        /** @brief 起始物品ID（接任务时自动获得的物品） */
        uint32 _startItem = 0;

        /** @brief 任务标题 */
        std::string _title;

        /** @brief 任务详情文本（NPC讲述的故事） */
        std::string _details;

        /** @brief 任务目标文本（显示在任务日志中的目标） */
        std::string _objectives;

        /** @brief 奖励提示文本（完成任务时NPC说的话） */
        std::string _offerRewardText;

        /** @brief 请求物品文本（未完成时与NPC对话的文本） */
        std::string _requestItemsText;

        /** @brief 区域描述文本（显示在任务追踪器中） */
        std::string _areaDescription;

        /** @brief 完成文本（任务完成时显示的文本） */
        std::string _completedText;

        /** @brief 固定荣誉奖励值 */
        uint32 _rewardHonor = 0;

        /** @brief 击杀荣誉乘数 */
        float _rewardKillHonor = 0.f;

        /** @brief 奖励/要求的金钱（正数=奖励，负数=要求支付） */
        int32 _rewardMoney = 0;

        /** @brief 奖励的额外金钱 */
        uint32 _rewardBonusMoney = 0;

        /** @brief 奖励法术显示ID（客户端显示的法术） */
        uint32 _rewardDisplaySpell = 0;

        /** @brief 奖励法术施放ID（实际施放的法术） */
        int32 _rewardSpell = 0;

        /** @brief POI所在地图ID */
        uint32 _poiContinent = 0;

        /** @brief POI X坐标 */
        float _poiX = 0.f;

        /** @brief POI Y坐标 */
        float _poiY = 0.f;

        /** @brief POI优先级 */
        uint32 _poiPriority = 0;

        /** @brief 任务未完成时的表情ID */
        uint32 _emoteOnIncomplete = 0;

        /** @brief 任务完成时的表情ID */
        uint32 _emoteOnComplete = 0;

        // ========== quest_template_addon 表数据（自定义扩展） ==========

        /** @brief 最高等级限制 */
        uint32 _maxLevel = 0;

        /** @brief 职业要求位掩码 */
        uint32 _requiredClasses = 0;

        /** @brief 起始法术ID（接受任务时施放的法术） */
        uint32 _sourceSpellid = 0;

        /** @brief 前置任务ID（必须完成才能接此任务） */
        int32 _prevQuestId = 0;

        /** @brief 下一个任务ID（完成此任务后可接的任务） */
        uint32 _nextQuestId = 0;

        /** @brief 互斥组ID（同组任务只能接一个） */
        int32 _exclusiveGroup = 0;

        /** @brief 面包屑任务ID（引导玩家到该任务的任务） */
        int32 _breadcrumbForQuestId = 0;

        /** @brief 奖励邮件模板ID */
        uint32 _rewardMailTemplateId = 0;

        /** @brief 奖励邮件延迟时间（秒） */
        uint32 _rewardMailDelay = 0;

        /** @brief 所需技能ID */
        uint32 _requiredSkillId = 0;

        /** @brief 所需技能点数 */
        uint32 _requiredSkillPoints = 0;

        /** @brief 最小声望阵营要求 */
        uint32 _requiredMinRepFaction = 0;

        /** @brief 最小声望值要求 */
        int32 _requiredMinRepValue = 0;

        /** @brief 最大声望阵营限制 */
        uint32 _requiredMaxRepFaction = 0;

        /** @brief 最大声望值限制 */
        int32 _requiredMaxRepValue = 0;

        /** @brief 起始物品数量 */
        uint32 _startItemCount = 0;

        /** @brief 奖励邮件发送者Entry */
        uint32 _rewardMailSenderEntry = 0;

        /** @brief 特殊标志位掩码（服务器端自定义标志，非客户端数据） */
        uint32 _specialFlags = 0;

        // ========== 辅助函数 ==========

        /**
         * @brief 将经验值四舍五入到标准值
         * @param xp 原始经验值
         * @return 四舍五入后的标准经验值
         */
        static uint32 RoundXPValue(uint32 xp);

        /** @brief 任务的弱引用指针（用于安全引用管理） */
        Trinity::unique_weak_ptr<Quest> _weakRef;
};

/**
 * @brief 任务状态数据结构
 *
 * 存储玩家特定任务的状态信息，包括任务进度、计时器等。
 * 每个玩家对每个任务都有一个对应的 QuestStatusData 实例。
 */
struct QuestStatusData
{
    /** @brief 任务状态（未接、进行中、已完成、已失败等） */
    QuestStatus Status = QUEST_STATUS_NONE;

    /** @brief 任务计时器剩余时间（毫秒，0表示无计时器） */
    uint32 Timer = 0;

    /** @brief 物品收集进度（对应QUEST_ITEM_OBJECTIVES_COUNT个物品目标） */
    uint16 ItemCount[QUEST_ITEM_OBJECTIVES_COUNT] = { };

    /** @brief 生物/游戏对象击杀或交互进度（对应QUEST_OBJECTIVES_COUNT个目标） */
    uint16 CreatureOrGOCount[QUEST_OBJECTIVES_COUNT] = { };

    /** @brief 玩家击杀数量（PvP任务） */
    uint16 PlayerCount = 0;

    /** @brief 是否已探索（探索任务用） */
    bool Explored = false;
};
#endif
