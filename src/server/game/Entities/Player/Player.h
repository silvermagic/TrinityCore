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
 * @file Player.h
 * @brief 玩家实体类头文件
 *
 * 本文件定义了 Player 类，这是 TrinityCore 中最重要的类之一，代表游戏中的玩家角色。
 *
 * 主要职责：
 * - 管理玩家的所有数据（属性、装备、技能、任务等）
 * - 处理玩家的行为逻辑（移动、战斗、交互等）
 * - 维护玩家与游戏世界的交互接口
 * - 管理玩家的社交关系（好友、公会、队伍等）
 * - 处理玩家的背包和银行系统
 * - 管理玩家的声望、成就、任务进度
 *
 * 核心功能模块：
 * - 物品系统：装备管理、背包、银行、物品交易
 * - 技能系统：法术、天赋、专业技能
 * - 任务系统：任务进度、奖励、日常任务
 * - 社交系统：好友、公会、队伍、交易
 * - 战斗系统：PVP、PVE、决斗
 * - 传送系统：传送点、飞行路径、召回
 * - 实例系统：副本绑定、难度设置
 *
 * @see Unit - 玩家的基类，提供单位的基础功能
 * @see WorldSession - 管理玩家的网络会话
 * @see Player.cpp - 玩家类的实现文件
 */

#ifndef _PLAYER_H
#define _PLAYER_H

#include "GridObject.h"
#include "Unit.h"
#include "DatabaseEnvFwd.h"
#include "DBCEnums.h"
#include "EquipmentSet.h"
#include "GroupReference.h"
#include "ItemDefines.h"
#include "ItemEnchantmentMgr.h"
#include "MapReference.h"
#include "PetDefines.h"
#include "PlayerTaxi.h"
#include "QuestDef.h"
#include <memory>
#include <queue>
#include <unordered_set>

struct AccessRequirement;
struct AchievementEntry;
struct AreaTableEntry;
struct AreaTriggerEntry;
struct BarberShopStyleEntry;
struct CharacterCustomizeInfo;
struct CharTitlesEntry;
struct ChatChannelsEntry;
struct CreatureTemplate;
struct FactionEntry;
struct ItemSetEffect;
struct ItemTemplate;
struct Loot;
struct Mail;
struct ScalingStatDistributionEntry;
struct ScalingStatValuesEntry;
struct TrainerSpell;
struct VendorItem;

class AchievementMgr;
class Bag;
class Battleground;
class CinematicMgr;
class Channel;
class CharacterCreateInfo;
class Creature;
class DynamicObject;
class GameClient;
class Group;
class Guild;
class Item;
class LootStore;
class OutdoorPvP;
class Pet;
class PetAura;
class PlayerAI;
class PlayerMenu;
class PlayerSocial;
class ReputationMgr;
class SpellCastTargets;
class TradeData;

enum InventoryType : uint8;
enum ItemClass : uint8;
enum LootError : uint8;
enum LootType : uint8;

/**
 * @brief 玩家邮件队列类型定义
 * 存储玩家收到的所有邮件指针
 */
typedef std::deque<Mail*> PlayerMails;

/** @brief 玩家最大技能数量 */
#define PLAYER_MAX_SKILLS           127
/** @brief 玩家最大日常任务数量 */
#define PLAYER_MAX_DAILY_QUESTS     25
/** @brief 玩家已探索区域的大小（用于存储探索进度位图） */
#define PLAYER_EXPLORED_ZONES_SIZE  128

/**
 * @enum SpellModType
 * @brief 法术修改器类型枚举
 *
 * 定义法术修改器的两种类型：固定值修改和百分比修改
 * 注意：SPELLMOD_* 的值实际上对应光环类型
 */
enum SpellModType : uint8
{
    SPELLMOD_FLAT         = SPELL_AURA_ADD_FLAT_MODIFIER,  ///< 固定值修改（例如：+10点伤害）
    SPELLMOD_PCT          = SPELL_AURA_ADD_PCT_MODIFIER    ///< 百分比修改（例如：+10%伤害）
};

/**
 * @enum PlayerUnderwaterState
 * @brief 玩家水下状态枚举
 *
 * 用于跟踪玩家在不同类型液体中的状态
 * 这些值是 Trinity 内部使用的位掩码，不会发送给客户端
 */
enum PlayerUnderwaterState
{
    UNDERWATER_NONE                     = 0x00,  ///< 不在水中
    UNDERWATER_INWATER                  = 0x01,  ///< 在普通水中（受呼吸限制）
    UNDERWATER_INLAVA                   = 0x02,  ///< 在岩浆中（受火焰伤害）
    UNDERWATER_INSLIME                  = 0x04,  ///< 在污泥中（受自然伤害）
    UNDERWATER_INDARKWATER              = 0x08,  ///< 在深水中（受疲劳限制）

    UNDERWATER_EXIST_TIMERS             = 0x10   ///< 存在活跃的计时器
};

/**
 * @enum BuyBankSlotResult
 * @brief 购买银行槽位的结果枚举
 */
enum BuyBankSlotResult
{
    ERR_BANKSLOT_FAILED_TOO_MANY    = 0,  ///< 槽位已达上限
    ERR_BANKSLOT_INSUFFICIENT_FUNDS = 1,  ///< 资金不足
    ERR_BANKSLOT_NOTBANKER          = 2,  ///< 不是银行职员
    ERR_BANKSLOT_OK                 = 3   ///< 购买成功
};

/**
 * @enum PlayerSpellState
 * @brief 玩家法术状态枚举
 *
 * 用于跟踪法术在数据库同步时的状态变化
 */
enum PlayerSpellState : uint8
{
    PLAYERSPELL_UNCHANGED = 0,  ///< 未改变（已保存到数据库）
    PLAYERSPELL_CHANGED   = 1,  ///< 已修改（需要保存）
    PLAYERSPELL_NEW       = 2,  ///< 新学习（需要插入数据库）
    PLAYERSPELL_REMOVED   = 3,  ///< 已移除（需要从数据库删除）
    PLAYERSPELL_TEMPORARY = 4   ///< 临时法术（不保存到数据库）
};

/**
 * @struct PlayerSpell
 * @brief 玩家法术信息结构体
 *
 * 存储单个法术的详细信息和状态
 */
struct PlayerSpell
{
    PlayerSpellState state;     ///< 法术的数据库同步状态
    bool active            : 1; ///< 是否激活（在法术书中显示）
    bool dependent         : 1; ///< 是否为依赖法术（通过其他法术学习、技能提升、任务奖励等获得）
    bool disabled          : 1; ///< 是否禁用（第一阶已通过天赋学习但天赋已被遗忘，保存最高学习阶数）
};

/**
 * @struct PlayerTalent
 * @brief 玩家天赋信息结构体
 *
 * 存储单个天赋的详细信息和所属专精
 */
struct PlayerTalent
{
    PlayerSpellState state;  ///< 天赋的数据库同步状态
    uint8 spec;              ///< 所属专精索引（0=主专精，1=副专精）
};

/**
 * @struct SpellModifier
 * @brief 法术修改器结构体
 *
 * 用于存储影响其他法术的修改效果
 * 通常由光环（Aura）效果产生
 */
struct SpellModifier
{
    /**
     * @brief 构造函数
     * @param _ownerAura 拥有此修改器的光环
     */
    SpellModifier(Aura* _ownerAura) : op(SPELLMOD_DAMAGE), type(SPELLMOD_FLAT), value(0), mask(), spellId(0), ownerAura(_ownerAura) { }

    SpellModOp op;          ///< 修改操作类型（如伤害、施法时间等）
    SpellModType type;      ///< 修改类型（固定值或百分比）

    int32 value;            ///< 修改值（正值增加，负值减少）
    flag96 mask;            ///< 受影响的法术掩码（标识哪些法术受此修改器影响）
    uint32 spellId;         ///< 产生此修改器的法术ID
    Aura* const ownerAura;  ///< 拥有此修改器的光环（用于在光环移除时删除修改器）
};

/** @brief 玩家天赋映射表：天赋ID -> PlayerTalent指针 */
typedef std::unordered_map<uint32, PlayerTalent*> PlayerTalentMap;
/** @brief 玩家法术映射表：法术ID -> PlayerSpell结构 */
typedef std::unordered_map<uint32, PlayerSpell> PlayerSpellMap;
/** @brief 法术修改器容器：存储所有活跃的法术修改器 */
typedef std::unordered_set<SpellModifier*> SpellModContainer;

/** @brief 实例时间映射表：实例ID -> 释放时间 */
typedef std::unordered_map<uint32 /*instanceId*/, time_t/*releaseTime*/> InstanceTimeMap;

/**
 * @enum ActionButtonUpdateState
 * @brief 动作按钮更新状态枚举
 */
enum ActionButtonUpdateState
{
    ACTIONBUTTON_UNCHANGED = 0,  ///< 未改变
    ACTIONBUTTON_CHANGED   = 1,  ///< 已修改
    ACTIONBUTTON_NEW       = 2,  ///< 新增
    ACTIONBUTTON_DELETED   = 3   ///< 已删除
};

/**
 * @enum ActionButtonType
 * @brief 动作按钮类型枚举
 */
enum ActionButtonType
{
    ACTION_BUTTON_SPELL     = 0x00,                     ///< 法术按钮
    ACTION_BUTTON_C         = 0x01,                     ///< 点击类型（具体用途不明）
    ACTION_BUTTON_EQSET     = 0x20,                     ///< 装备方案按钮
    ACTION_BUTTON_MACRO     = 0x40,                     ///< 宏按钮
    ACTION_BUTTON_CMACRO    = ACTION_BUTTON_C | ACTION_BUTTON_MACRO,  ///< 点击宏
    ACTION_BUTTON_ITEM      = 0x80                      ///< 物品按钮
};

/**
 * @enum ReputationSource
 * @brief 声望来源类型枚举
 */
enum ReputationSource
{
    REPUTATION_SOURCE_KILL,              ///< 击杀获得
    REPUTATION_SOURCE_QUEST,             ///< 普通任务奖励
    REPUTATION_SOURCE_DAILY_QUEST,       ///< 日常任务奖励
    REPUTATION_SOURCE_WEEKLY_QUEST,      ///< 周常任务奖励
    REPUTATION_SOURCE_MONTHLY_QUEST,     ///< 月常任务奖励
    REPUTATION_SOURCE_REPEATABLE_QUEST,  ///< 可重复任务奖励
    REPUTATION_SOURCE_SPELL              ///< 法术效果
};

/** @brief 从打包数据中提取动作ID（低24位） */
#define ACTION_BUTTON_ACTION(X) (uint32(X) & 0x00FFFFFF)
/** @brief 从打包数据中提取按钮类型（高8位） */
#define ACTION_BUTTON_TYPE(X)   ((uint32(X) & 0xFF000000) >> 24)
/** @brief 动作ID的最大值 */
#define MAX_ACTION_BUTTON_ACTION_VALUE (0x00FFFFFF+1)

/**
 * @struct ActionButton
 * @brief 动作按钮结构体
 *
 * 表示玩家动作条上的一个按钮
 * 使用打包格式存储动作和类型信息
 */
struct ActionButton
{
    ActionButton() : packedData(0), uState(ACTIONBUTTON_NEW) { }

    uint32 packedData;              ///< 打包数据（低24位=动作ID，高8位=类型）
    ActionButtonUpdateState uState; ///< 数据库同步状态

    /**
     * @brief 获取按钮类型
     * @return 按钮类型枚举值
     */
    ActionButtonType GetType() const { return ActionButtonType(ACTION_BUTTON_TYPE(packedData)); }

    /**
     * @brief 获取动作ID
     * @return 动作ID（法术ID、物品ID等）
     */
    uint32 GetAction() const { return ACTION_BUTTON_ACTION(packedData); }

    /**
     * @brief 设置动作和类型
     * @param action 动作ID
     * @param type 按钮类型
     */
    void SetActionAndType(uint32 action, ActionButtonType type)
    {
        uint32 newData = action | (uint32(type) << 24);
        if (newData != packedData || uState == ACTIONBUTTON_DELETED)
        {
            packedData = newData;
            if (uState != ACTIONBUTTON_NEW)
                uState = ACTIONBUTTON_CHANGED;
        }
    }
};

/** @brief 最大动作按钮数量（3.2.0版本检查通过） */
#define  MAX_ACTION_BUTTONS 144

/** @brief 动作按钮列表：按钮索引 -> ActionButton */
typedef std::map<uint8, ActionButton> ActionButtonList;

/**
 * @struct PvPInfo
 * @brief PVP信息结构体
 *
 * 存储玩家的PVP状态相关信息
 */
struct PvPInfo
{
    PvPInfo() : IsHostile(false), IsInHostileArea(false), IsInNoPvPArea(false), IsInFFAPvPArea(false), EndTimer(0) { }

    bool IsHostile;           ///< 是否处于敌对状态
    bool IsInHostileArea;     ///< 是否在强制PVP区域（如敌方领土）
    bool IsInNoPvPArea;       ///< 是否在禁PVP区域（如圣所或友方主城）
    bool IsInFFAPvPArea;      ///< 是否在FFA PVP区域（如古拉巴什竞技场）
    time_t EndTimer;          ///< PVP标志移除时间（5分钟后移除）
};

/**
 * @enum DuelState
 * @brief 决斗状态枚举
 */
enum DuelState
{
    DUEL_STATE_CHALLENGED,    ///< 已发起挑战
    DUEL_STATE_COUNTDOWN,     ///< 倒计时中
    DUEL_STATE_IN_PROGRESS,   ///< 决斗进行中
    DUEL_STATE_COMPLETED      ///< 决斗已完成
};

/**
 * @struct DuelInfo
 * @brief 决斗信息结构体
 *
 * 存储决斗的详细信息和状态
 */
struct DuelInfo
{
    DuelInfo(Player* opponent, Player* initiator, bool isMounted) : Opponent(opponent), Initiator(initiator), IsMounted(isMounted) {}

    Player* const Opponent;      ///< 决斗对手
    Player* const Initiator;     ///< 决斗发起者
    bool const IsMounted;        ///< 是否在骑乘状态下发起
    DuelState State = DUEL_STATE_CHALLENGED;  ///< 决斗状态
    time_t StartTime = 0;        ///< 决斗开始时间
    time_t OutOfBoundsTime = 0;  ///< 出界时间（用于判定决斗失败）
};

/**
 * @struct Areas
 * @brief 区域坐标信息结构体
 *
 * 用于存储区域的坐标范围信息
 */
struct Areas
{
    uint32 areaID;    ///< 区域ID
    uint32 areaFlag;  ///< 区域标志
    float x1;         ///< X坐标最小值
    float x2;         ///< X坐标最大值
    float y1;         ///< Y坐标最小值
    float y2;         ///< Y坐标最大值
};

/**
 * @enum RuneCooldowns
 * @brief 符文冷却时间枚举
 *
 * 定义死亡骑士符文系统的冷却时间常量
 */
enum RuneCooldowns
{
    RUNE_BASE_COOLDOWN  = 10000,  ///< 基础冷却时间（10秒）
    RUNE_MISS_COOLDOWN  = 1500    ///< 法术未命中时的冷却时间（1.5秒）
};

/**
 * @enum RuneType
 * @brief 符文类型枚举
 *
 * 定义死亡骑士的四种符文类型
 */
enum RuneType : uint8
{
    RUNE_BLOOD      = 0,  ///< 鲜血符文
    RUNE_UNHOLY     = 1,  ///< 邪恶符文
    RUNE_FROST      = 2,  ///< 冰霜符文
    RUNE_DEATH      = 3,  ///< 死亡符文（可转换为任意类型）
    NUM_RUNE_TYPES  = 4   ///< 符文类型总数
};

/**
 * @struct RuneInfo
 * @brief 单个符文的信息结构体
 *
 * 存储单个符文的详细状态信息
 */
struct RuneInfo
{
    uint8 BaseRune;                                  ///< 基础符文类型（原始类型）
    uint8 CurrentRune;                               ///< 当前符文类型（可能被转换）
    uint32 Cooldown;                                 ///< 剩余冷却时间（毫秒）
    std::unordered_set<AuraEffect const*> ConvertAuras;  ///< 影响此符文的光环效果集合
};

/**
 * @struct Runes
 * @brief 玩家符文系统结构体
 *
 * 管理死亡骑士的完整符文系统
 */
struct Runes
{
    RuneInfo runes[MAX_RUNES];  ///< 所有符文的详细信息（共6个符文）
    uint8 runeState;             ///< 可用符文的位掩码（1=可用，0=冷却中）
    RuneType lastUsedRune;       ///< 最后使用的符文类型

    /**
     * @brief 设置符文状态
     * @param index 符文索引（0-5）
     * @param set true表示设置为可用，false表示设置为冷却中
     */
    void SetRuneState(uint8 index, bool set = true)
    {
        if (set)
            runeState |= (1 << index);   // 设置为可用
        else
            runeState &= ~(1 << index);  // 设置为冷却中
    }
};

/**
 * @struct EnchantDuration
 * @brief 附魔持续时间结构体
 *
 * 用于跟踪物品上临时附魔的剩余时间
 */
struct EnchantDuration
{
    EnchantDuration() : item(nullptr), slot(MAX_ENCHANTMENT_SLOT), leftduration(0) { }

    /**
     * @brief 构造函数
     * @param _item 物品指针
     * @param _slot 附魔槽位
     * @param _leftduration 剩余持续时间（秒）
     */
    EnchantDuration(Item* _item, EnchantmentSlot _slot, uint32 _leftduration) : item(_item), slot(_slot),
        leftduration(_leftduration){ ASSERT(item); }

    Item* item;                  ///< 物品指针
    EnchantmentSlot slot;        ///< 附魔槽位
    uint32 leftduration;         ///< 剩余持续时间（秒）
};

/** @brief 附魔持续时间列表 */
typedef std::list<EnchantDuration> EnchantDurationList;
/** @brief 有持续时间的物品列表 */
typedef std::list<Item*> ItemDurationList;

/**
 * @enum PlayerMovementType
 * @brief 玩家移动类型枚举
 */
enum PlayerMovementType
{
    MOVE_ROOT       = 1,  ///< 定身（无法移动）
    MOVE_UNROOT     = 2,  ///< 解除定身
    MOVE_WATER_WALK = 3,  ///< 水面行走
    MOVE_LAND_WALK  = 4   ///< 陆地行走
};

/**
 * @enum DrunkenState
 * @brief 醉酒状态枚举
 *
 * 定义玩家的四种醉酒状态等级
 */
enum DrunkenState
{
    DRUNKEN_SOBER   = 0,  ///< 清醒状态
    DRUNKEN_TIPSY   = 1,  ///< 微醺状态
    DRUNKEN_DRUNK   = 2,  ///< 醉酒状态
    DRUNKEN_SMASHED = 3   ///< 酩酊大醉状态
};

/** @brief 醉酒状态总数 */
#define MAX_DRUNKEN   4

/**
 * @enum PlayerFlags
 * @brief 玩家标志枚举
 *
 * 定义玩家的各种状态标志（32位标志位）
 * 这些标志会影响玩家的显示和行为
 */
enum PlayerFlags
{
    PLAYER_FLAGS_GROUP_LEADER      = 0x00000001,  ///< 队伍领袖
    PLAYER_FLAGS_AFK               = 0x00000002,  ///< 暂离状态（AFK）
    PLAYER_FLAGS_DND               = 0x00000004,  ///< 请勿打扰状态（DND）
    PLAYER_FLAGS_GM                = 0x00000008,  ///< GM模式开启
    PLAYER_FLAGS_GHOST             = 0x00000010,  ///< 灵魂状态
    PLAYER_FLAGS_RESTING           = 0x00000020,  ///< 休息状态（在旅馆或城市）
    PLAYER_FLAGS_UNK6              = 0x00000040,  ///< 未知标志6
    PLAYER_FLAGS_UNK7              = 0x00000080,  ///< 未知标志7（3.0.3之前用于FFA PVP状态）
    PLAYER_FLAGS_CONTESTED_PVP     = 0x00000100,  ///< 争夺中的PVP状态（PVP战斗后会被争夺中的守卫攻击）
    PLAYER_FLAGS_IN_PVP            = 0x00000200,  ///< 在PVP中
    PLAYER_FLAGS_HIDE_HELM         = 0x00000400,  ///< 隐藏头盔显示
    PLAYER_FLAGS_HIDE_CLOAK        = 0x00000800,  ///< 隐藏披风显示
    PLAYER_FLAGS_PLAYED_LONG_TIME  = 0x00001000,  ///< 已游戏很长时间
    PLAYER_FLAGS_PLAYED_TOO_LONG   = 0x00002000,  ///< 已游戏过长时间
    PLAYER_FLAGS_IS_OUT_OF_BOUNDS  = 0x00004000,  ///< 超出边界
    PLAYER_FLAGS_DEVELOPER         = 0x00008000,  ///< 开发者标志（<Dev>前缀）
    PLAYER_FLAGS_UNK16             = 0x00010000,  ///< 未知标志16（3.0.3之前用于进入圣所）
    PLAYER_FLAGS_TAXI_BENCHMARK    = 0x00020000,  ///< 飞行坐骑基准测试模式（2.0.1）
    PLAYER_FLAGS_PVP_TIMER         = 0x00040000,  ///< PVP计时器激活（手动关闭PVP后）
    PLAYER_FLAGS_UBER              = 0x00080000,  ///< Uber标志
    PLAYER_FLAGS_UNK20             = 0x00100000,  ///< 未知标志20
    PLAYER_FLAGS_UNK21             = 0x00200000,  ///< 未知标志21
    PLAYER_FLAGS_COMMENTATOR2      = 0x00400000,  ///< 评论员模式2
    PLAYER_ALLOW_ONLY_ABILITY      = 0x00800000,  ///< 仅允许特定技能（用于剑刃风暴和杀戮盛宴）
    PLAYER_FLAGS_UNK24             = 0x01000000,  ///< 未知标志24（禁用所有近战能力，包括自动攻击）
    PLAYER_FLAGS_NO_XP_GAIN        = 0x02000000,  ///< 禁止获得经验值
    PLAYER_FLAGS_UNK26             = 0x04000000,  ///< 未知标志26
    PLAYER_FLAGS_UNK27             = 0x08000000,  ///< 未知标志27
    PLAYER_FLAGS_UNK28             = 0x10000000,  ///< 未知标志28
    PLAYER_FLAGS_UNK29             = 0x20000000,  ///< 未知标志29
    PLAYER_FLAGS_UNK30             = 0x40000000,  ///< 未知标志30
    PLAYER_FLAGS_UNK31             = 0x80000000   ///< 未知标志31
};

/**
 * @enum PlayerBytesOffsets
 * @brief PLAYER_BYTES字段的字节偏移量枚举
 *
 * 定义PLAYER_BYTES字段中各字节的具体含义
 */
enum PlayerBytesOffsets
{
    PLAYER_BYTES_OFFSET_SKIN_ID         = 0,  ///< 皮肤ID
    PLAYER_BYTES_OFFSET_FACE_ID         = 1,  ///< 脸型ID
    PLAYER_BYTES_OFFSET_HAIR_STYLE_ID   = 2,  ///< 发型ID
    PLAYER_BYTES_OFFSET_HAIR_COLOR_ID   = 3   ///< 发色ID
};

/**
 * @enum PlayerBytes2Offsets
 * @brief PLAYER_BYTES_2字段的字节偏移量枚举
 */
enum PlayerBytes2Offsets
{
    PLAYER_BYTES_2_OFFSET_FACIAL_STYLE      = 0,  ///< 面部样式（胡须等）
    PLAYER_BYTES_2_OFFSET_PARTY_TYPE        = 1,  ///< 队伍类型
    PLAYER_BYTES_2_OFFSET_BANK_BAG_SLOTS    = 2,  ///< 银行背包槽数量
    PLAYER_BYTES_2_OFFSET_REST_STATE        = 3   ///< 休息状态
};

/**
 * @enum PlayerBytes3Offsets
 * @brief PLAYER_BYTES_3字段的字节偏移量枚举
 */
enum PlayerBytes3Offsets
{
    PLAYER_BYTES_3_OFFSET_GENDER        = 0,  ///< 性别
    PLAYER_BYTES_3_OFFSET_INEBRIATION   = 1,  ///< 醉酒程度
    PLAYER_BYTES_3_OFFSET_PVP_TITLE     = 2,  ///< PVP称号
    PLAYER_BYTES_3_OFFSET_ARENA_FACTION = 3   ///< 竞技场阵营
};

/**
 * @enum PlayerFieldBytesOffsets
 * @brief PLAYER_FIELD_BYTES字段的字节偏移量枚举
 */
enum PlayerFieldBytesOffsets
{
    PLAYER_FIELD_BYTES_OFFSET_FLAGS                 = 0,  ///< 玩家字段标志
    PLAYER_FIELD_BYTES_OFFSET_RAF_GRANTABLE_LEVEL   = 1,  ///< RAF可赠送等级
    PLAYER_FIELD_BYTES_OFFSET_ACTION_BAR_TOGGLES    = 2,  ///< 动作栏切换状态
    PLAYER_FIELD_BYTES_OFFSET_LIFETIME_MAX_PVP_RANK = 3   ///< 生命周期最大PVP等级
};

/**
 * @enum PlayerFieldBytes2Offsets
 * @brief PLAYER_FIELD_BYTES_2字段的字节偏移量枚举
 */
enum PlayerFieldBytes2Offsets
{
    PLAYER_FIELD_BYTES_2_OFFSET_OVERRIDE_SPELLS_ID                  = 0,  ///< 覆盖法术ID（uint16!）
    PLAYER_FIELD_BYTES_2_OFFSET_IGNORE_POWER_REGEN_PREDICTION_MASK  = 2,  ///< 忽略能量回复预测掩码
    PLAYER_FIELD_BYTES_2_OFFSET_AURA_VISION                         = 3   ///< 光环视野
};

static_assert((PLAYER_FIELD_BYTES_2_OFFSET_OVERRIDE_SPELLS_ID & 1) == 0, "PLAYER_FIELD_BYTES_2_OFFSET_OVERRIDE_SPELLS_ID must be aligned to 2 byte boundary");

/** @brief PLAYER_BYTES_2中覆盖法术的uint16偏移量 */
#define PLAYER_BYTES_2_OVERRIDE_SPELLS_UINT16_OFFSET (PLAYER_FIELD_BYTES_2_OFFSET_OVERRIDE_SPELLS_ID / 2)

/** @brief 已知称号字段数量 */
#define KNOWN_TITLES_SIZE   3
/** @brief 最大称号索引（3个uint64字段，共192个称号） */
#define MAX_TITLE_INDEX     (KNOWN_TITLES_SIZE*64)

/**
 * @enum PlayerFieldByteFlags
 * @brief PLAYER_FIELD_BYTES字段使用的标志枚举
 */
enum PlayerFieldByteFlags
{
    PLAYER_FIELD_BYTE_TRACK_STEALTHED   = 0x00000002,  ///< 追踪潜行单位
    PLAYER_FIELD_BYTE_RELEASE_TIMER     = 0x00000008,  ///< 显示自动释放灵魂计时器
    PLAYER_FIELD_BYTE_NO_RELEASE_WINDOW = 0x00000010   ///< 不显示"释放灵魂"窗口
};

/**
 * @enum PlayerFieldByte2Flags
 * @brief PLAYER_FIELD_BYTES2字段使用的标志枚举
 */
enum PlayerFieldByte2Flags
{
    PLAYER_FIELD_BYTE2_NONE                 = 0x00,  ///< 无标志
    PLAYER_FIELD_BYTE2_STEALTH              = 0x20,  ///< 潜行状态
    PLAYER_FIELD_BYTE2_INVISIBILITY_GLOW    = 0x40   ///< 隐身发光效果
};

/**
 * @enum MirrorTimerType
 * @brief 镜像计时器类型枚举
 *
 * 用于水下呼吸、疲劳、火焰等环境效果的计时
 */
enum MirrorTimerType
{
    FATIGUE_TIMER      = 0,  ///< 疲劳计时器（深水区域）
    BREATH_TIMER       = 1,  ///< 呼吸计时器（水下呼吸）
    FIRE_TIMER         = 2   ///< 火焰计时器（岩浆等）
};

/** @brief 最大计时器数量 */
#define MAX_TIMERS      3
/** @brief 禁用的镜像计时器标志 */
#define DISABLED_MIRROR_TIMER   -1

/**
 * @enum PlayerExtraFlags
 * @brief 玩家额外标志枚举
 *
 * 存储玩家的额外状态标志（GM能力、角色服务等）
 */
enum PlayerExtraFlags
{
    // GM能力标志
    PLAYER_EXTRA_GM_ON                      = 0x0001,  ///< GM模式开启
    PLAYER_EXTRA_ACCEPT_WHISPERS            = 0x0004,  ///< 接受密语
    PLAYER_EXTRA_TAXICHEAT                  = 0x0008,  ///< 飞行作弊（无需解锁飞行点）
    PLAYER_EXTRA_GM_INVISIBLE               = 0x0010,  ///< GM隐身
    PLAYER_EXTRA_GM_CHAT                    = 0x0020,  ///< 聊天中显示GM标志
    PLAYER_EXTRA_HAS_310_FLYER              = 0x0040,  ///< 拥有310%速度飞行坐骑

    // 其他状态
    PLAYER_EXTRA_PVP_DEATH                  = 0x0100,  ///< PVP死亡状态（持续到尸体创建）

    // 角色服务标记
    PLAYER_EXTRA_HAS_RACE_CHANGED           = 0x0200,  ///< 已进行种族变更
    PLAYER_EXTRA_GRANTED_LEVELS_FROM_RAF    = 0x0400,  ///< 从RAF获得等级赠送
    PLAYER_EXTRA_LEVEL_BOOSTED              = 0x0800,  ///< 等级已提升（保留给主分支）
};

/**
 * @enum AtLoginFlags
 * @brief 登录时执行的标志枚举
 *
 * 定义玩家下次登录时需要执行的操作
 */
enum AtLoginFlags
{
    AT_LOGIN_NONE              = 0x000,  ///< 无操作
    AT_LOGIN_RENAME            = 0x001,  ///< 需要重命名
    AT_LOGIN_RESET_SPELLS      = 0x002,  ///< 重置法术
    AT_LOGIN_RESET_TALENTS     = 0x004,  ///< 重置天赋
    AT_LOGIN_CUSTOMIZE         = 0x008,  ///< 自定义外观
    AT_LOGIN_RESET_PET_TALENTS = 0x010,  ///< 重置宠物天赋
    AT_LOGIN_FIRST             = 0x020,  ///< 首次登录
    AT_LOGIN_CHANGE_FACTION    = 0x040,  ///< 变更阵营
    AT_LOGIN_CHANGE_RACE       = 0x080,  ///< 变更种族
    AT_LOGIN_RESURRECT         = 0x100,  ///< 复活
};

/** @brief 任务状态映射表：任务ID -> 任务状态数据 */
typedef std::map<uint32, QuestStatusData> QuestStatusMap;
/** @brief 已奖励任务集合（已完成并领取奖励的任务ID） */
typedef std::set<uint32> RewardedQuestSet;

/**
 * @enum QuestSaveType
 * @brief 任务保存类型枚举
 */
enum QuestSaveType
{
    QUEST_DEFAULT_SAVE_TYPE = 0,       ///< 默认保存类型
    QUEST_DELETE_SAVE_TYPE,            ///< 删除任务
    QUEST_FORCE_DELETE_SAVE_TYPE       ///< 强制删除任务
};

/** @brief 任务状态保存映射表：任务ID -> 保存类型 */
typedef std::map<uint32, QuestSaveType> QuestStatusSaveMap;

/**
 * @enum QuestSlotOffsets
 * @brief 任务槽位偏移量枚举
 *
 * 定义任务在任务日志中的数据偏移
 */
enum QuestSlotOffsets
{
    QUEST_ID_OFFSET     = 0,  ///< 任务ID偏移
    QUEST_STATE_OFFSET  = 1,  ///< 任务状态偏移
    QUEST_COUNTS_OFFSET = 2,  ///< 任务计数偏移（任务目标完成数）
    QUEST_TIME_OFFSET   = 4   ///< 任务时间偏移
};

/** @brief 最大任务偏移量 */
#define MAX_QUEST_OFFSET 5

/**
 * @enum QuestSlotStateMask
 * @brief 任务槽位状态掩码枚举
 */
enum QuestSlotStateMask
{
    QUEST_STATE_NONE     = 0x0000,  ///< 无状态
    QUEST_STATE_COMPLETE = 0x0001,  ///< 任务完成
    QUEST_STATE_FAIL     = 0x0002   ///< 任务失败
};

/**
 * @enum SkillUpdateState
 * @brief 技能更新状态枚举
 */
enum SkillUpdateState
{
    SKILL_UNCHANGED     = 0,  ///< 未改变
    SKILL_CHANGED       = 1,  ///< 已修改
    SKILL_NEW           = 2,  ///< 新技能
    SKILL_DELETED       = 3   ///< 已删除
};

/**
 * @struct SkillStatusData
 * @brief 技能状态数据结构体
 */
struct SkillStatusData
{
    /**
     * @brief 构造函数
     * @param _pos 技能在技能列表中的位置
     * @param _uState 技能的更新状态
     */
    SkillStatusData(uint8 _pos, SkillUpdateState _uState) : pos(_pos), uState(_uState)
    {
    }
    uint8 pos;              ///< 技能位置
    SkillUpdateState uState; ///< 更新状态
};

/** @brief 技能状态映射表：技能ID -> 技能状态数据 */
typedef std::unordered_map<uint32, SkillStatusData> SkillStatusMap;

class Quest;
class Spell;
class Item;
class WorldSession;

/**
 * @enum PlayerSlots
 * @brief 玩家物品槽位枚举
 *
 * 定义玩家物品栏的槽位范围
 */
enum PlayerSlots
{
    PLAYER_SLOT_START           = 0,    ///< 物品槽位起始索引
    PLAYER_SLOT_END             = 150,  ///< 物品槽位结束索引（不包含）
    PLAYER_SLOTS_COUNT          = (PLAYER_SLOT_END - PLAYER_SLOT_START)  ///< 总槽位数
};

/** @brief 主背包槽位索引（用于标识主背包） */
#define INVENTORY_SLOT_BAG_0    255

/**
 * @enum EquipmentSlots
 * @brief 装备槽位枚举
 *
 * 定义玩家可装备的19个槽位
 */
enum EquipmentSlots : uint8
{
    EQUIPMENT_SLOT_START        = 0,   ///< 装备槽起始
    EQUIPMENT_SLOT_HEAD         = 0,   ///< 头部
    EQUIPMENT_SLOT_NECK         = 1,   ///< 项链
    EQUIPMENT_SLOT_SHOULDERS    = 2,   ///< 肩部
    EQUIPMENT_SLOT_BODY         = 3,   ///< 衬衣
    EQUIPMENT_SLOT_CHEST        = 4,   ///< 胸甲
    EQUIPMENT_SLOT_WAIST        = 5,   ///< 腰带
    EQUIPMENT_SLOT_LEGS         = 6,   ///< 腿部
    EQUIPMENT_SLOT_FEET         = 7,   ///< 脚部
    EQUIPMENT_SLOT_WRISTS       = 8,   ///< 手腕
    EQUIPMENT_SLOT_HANDS        = 9,   ///< 手套
    EQUIPMENT_SLOT_FINGER1      = 10,  ///< 戒指1
    EQUIPMENT_SLOT_FINGER2      = 11,  ///< 戒指2
    EQUIPMENT_SLOT_TRINKET1     = 12,  ///< 饰品1
    EQUIPMENT_SLOT_TRINKET2     = 13,  ///< 饰品2
    EQUIPMENT_SLOT_BACK         = 14,  ///< 披风
    EQUIPMENT_SLOT_MAINHAND     = 15,  ///< 主手武器
    EQUIPMENT_SLOT_OFFHAND      = 16,  ///< 副手物品
    EQUIPMENT_SLOT_RANGED       = 17,  ///< 远程武器
    EQUIPMENT_SLOT_TABARD       = 18,  ///< 战袍
    EQUIPMENT_SLOT_END          = 19   ///< 装备槽结束
};

/**
 * @enum InventorySlots
 * @brief 背包槽位枚举（4个背包槽）
 */
enum InventorySlots : uint8
{
    INVENTORY_SLOT_BAG_START    = 19,  ///< 背包槽起始
    INVENTORY_SLOT_BAG_END      = 23   ///< 背包槽结束
};

/**
 * @enum InventoryPackSlots
 * @brief 主背包物品槽位枚举（16个槽位）
 */
enum InventoryPackSlots : uint8
{
    INVENTORY_SLOT_ITEM_START   = 23,  ///< 主背包槽起始
    INVENTORY_SLOT_ITEM_END     = 39   ///< 主背包槽结束
};

/**
 * @enum BankItemSlots
 * @brief 银行物品槽位枚举（28个槽位）
 */
enum BankItemSlots
{
    BANK_SLOT_ITEM_START        = 39,  ///< 银行物品槽起始
    BANK_SLOT_ITEM_END          = 67   ///< 银行物品槽结束
};

/**
 * @enum BankBagSlots
 * @brief 银行背包槽位枚举（7个槽位）
 */
enum BankBagSlots
{
    BANK_SLOT_BAG_START         = 67,  ///< 银行背包槽起始
    BANK_SLOT_BAG_END           = 74   ///< 银行背包槽结束
};

/**
 * @enum BuyBackSlots
 * @brief 回购槽位枚举（12个槽位）
 *
 * 用于存储玩家出售给商人的物品，可回购
 */
enum BuyBackSlots
{
    BUYBACK_SLOT_START          = 74,  ///< 回购槽起始
    BUYBACK_SLOT_END            = 86   ///< 回购槽结束
};

/**
 * @enum KeyRingSlots
 * @brief 钥匙链槽位枚举（32个槽位）
 */
enum KeyRingSlots : uint8
{
    KEYRING_SLOT_START          = 86,   ///< 钥匙链槽起始
    KEYRING_SLOT_END            = 118   ///< 钥匙链槽结束
};

/**
 * @enum CurrencyTokenSlots
 * @brief 货币代币槽位枚举（32个槽位）
 */
enum CurrencyTokenSlots
{
    CURRENCYTOKEN_SLOT_START    = 118,  ///< 代币槽起始
    CURRENCYTOKEN_SLOT_END      = 150   ///< 代币槽结束
};

/**
 * @struct ItemPosCount
 * @brief 物品位置和数量结构体
 *
 * 用于存储物品在背包中的位置和数量信息
 */
struct ItemPosCount
{
    /**
     * @brief 构造函数
     * @param _pos 物品位置
     * @param _count 物品数量
     */
    ItemPosCount(uint16 _pos, uint32 _count) : pos(_pos), count(_count) { }

    /**
     * @brief 检查此位置是否在给定的向量中
     * @param vec 要检查的向量
     * @return 如果包含则返回true
     */
    bool isContainedIn(std::vector<ItemPosCount> const& vec) const;

    uint16 pos;    ///< 物品位置
    uint32 count;  ///< 物品数量
};

/** @brief 物品位置数量向量 */
typedef std::vector<ItemPosCount> ItemPosCountVec;

/**
 * @enum TransferAbortReason
 * @brief 传送中止原因枚举
 *
 * 定义玩家传送失败的各种原因
 */
enum TransferAbortReason
{
    TRANSFER_ABORT_NONE                     = 0x00,  ///< 无中止
    TRANSFER_ABORT_ERROR                    = 0x01,  ///< 一般错误
    TRANSFER_ABORT_MAX_PLAYERS              = 0x02,  ///< 实例已满
    TRANSFER_ABORT_NOT_FOUND                = 0x03,  ///< 实例未找到
    TRANSFER_ABORT_TOO_MANY_INSTANCES       = 0x04,  ///< 最近进入实例次数过多
    TRANSFER_ABORT_ZONE_IN_COMBAT           = 0x06,  ///< 遭遇战进行中无法进入
    TRANSFER_ABORT_INSUF_EXPAN_LVL          = 0x07,  ///< 需要安装资料片
    TRANSFER_ABORT_DIFFICULTY               = 0x08,  ///< 难度模式不可用
    TRANSFER_ABORT_UNIQUE_MESSAGE           = 0x09,  ///< 特殊消息（如巫妖王之怒场景）
    TRANSFER_ABORT_TOO_MANY_REALM_INSTANCES = 0x0A,  ///< 无法启动更多实例
    TRANSFER_ABORT_NEED_GROUP               = 0x0B,  ///< 需要队伍（3.1）
    TRANSFER_ABORT_NOT_FOUND1               = 0x0C,  ///< 未找到1（3.1）
    TRANSFER_ABORT_NOT_FOUND2               = 0x0D,  ///< 未找到2（3.1）
    TRANSFER_ABORT_NOT_FOUND3               = 0x0E,  ///< 未找到3（3.2）
    TRANSFER_ABORT_REALM_ONLY               = 0x0F,  ///< 所有队员必须来自同一服务器
    TRANSFER_ABORT_MAP_NOT_ALLOWED          = 0x10   ///< 地图当前无法进入
};

/**
 * @enum InstanceResetWarningType
 * @brief 实例重置警告类型枚举
 */
enum InstanceResetWarningType
{
    RAID_INSTANCE_WARNING_HOURS     = 1,  ///< 警告：将在%d小时后重置
    RAID_INSTANCE_WARNING_MIN       = 2,  ///< 警告：将在%d分钟后重置
    RAID_INSTANCE_WARNING_MIN_SOON  = 3,  ///< 警告：即将重置，请退出区域
    RAID_INSTANCE_WELCOME           = 4,  ///< 欢迎进入，将在%s后重置
    RAID_INSTANCE_EXPIRED           = 5   ///< 实例已过期
};

/**
 * @enum ArenaTeamInfoType
 * @brief 竞技场队伍信息偏移量枚举
 *
 * 定义PLAYER_FIELD_ARENA_TEAM_INFO_1_1字段中的偏移量
 */
enum ArenaTeamInfoType
{
    ARENA_TEAM_ID                = 0,  ///< 竞技场队伍ID
    ARENA_TEAM_TYPE              = 1,  ///< 队伍类型（3.2新增）
    ARENA_TEAM_MEMBER            = 2,  ///< 成员类型（0=队长，1=成员）
    ARENA_TEAM_GAMES_WEEK        = 3,  ///< 本周比赛场次
    ARENA_TEAM_GAMES_SEASON      = 4,  ///< 本赛季比赛场次
    ARENA_TEAM_WINS_SEASON       = 5,  ///< 本赛季胜场
    ARENA_TEAM_PERSONAL_RATING   = 6,  ///< 个人等级
    ARENA_TEAM_END               = 7   ///< 字段结束
};

class InstanceSave;

/**
 * @enum RestFlag
 * @brief 休息标志枚举
 *
 * 定义玩家的休息状态标志
 */
enum RestFlag
{
    REST_FLAG_IN_TAVERN         = 0x1,  ///< 在旅馆中
    REST_FLAG_IN_CITY           = 0x2,  ///< 在城市中
    REST_FLAG_IN_FACTION_AREA   = 0x4,  ///< 在阵营区域（与AREA_FLAG_REST_ZONE_*配合使用）
};

/**
 * @enum TeleportToOptions
 * @brief 传送选项枚举
 *
 * 定义传送时的各种行为选项
 */
enum TeleportToOptions
{
    TELE_TO_GM_MODE             = 0x01,  ///< GM模式传送
    TELE_TO_NOT_LEAVE_TRANSPORT = 0x02,  ///< 不离开载具
    TELE_TO_NOT_LEAVE_COMBAT    = 0x04,  ///< 不脱离战斗
    TELE_TO_NOT_UNSUMMON_PET    = 0x08,  ///< 不解散宠物
    TELE_TO_SPELL               = 0x10,  ///< 法术传送
    TELE_TO_TRANSPORT_TELEPORT  = 0x20,  ///< 载具传送
    TELE_REVIVE_AT_TELEPORT     = 0x40   ///< 传送时复活
};

/**
 * @enum EnviromentalDamage
 * @brief 环境伤害类型枚举
 */
enum EnviromentalDamage : uint8
{
    DAMAGE_EXHAUSTED     = 0,  ///< 疲劳伤害
    DAMAGE_DROWNING      = 1,  ///< 溺水伤害
    DAMAGE_FALL          = 2,  ///< 摔落伤害
    DAMAGE_LAVA          = 3,  ///< 岩浆伤害
    DAMAGE_SLIME         = 4,  ///< 污泥伤害
    DAMAGE_FIRE          = 5,  ///< 火焰伤害
    DAMAGE_FALL_TO_VOID  = 6   ///< 掉入虚空（自定义情况，不损失耐久度）
};

/**
 * @enum PlayerChatTag
 * @brief 玩家聊天标签枚举
 */
enum PlayerChatTag
{
    CHAT_TAG_NONE       = 0x00,  ///< 无标签
    CHAT_TAG_AFK        = 0x01,  ///< 暂离标签
    CHAT_TAG_DND        = 0x02,  ///< 请勿打扰标签
    CHAT_TAG_GM         = 0x04,  ///< GM标签
    CHAT_TAG_COM        = 0x08,  ///< 评论员标签
    CHAT_TAG_DEV        = 0x10   ///< 开发者标签
};

/**
 * @enum PlayedTimeIndex
 * @brief 游戏时间索引枚举
 */
enum PlayedTimeIndex
{
    PLAYED_TIME_TOTAL = 0,  ///< 总游戏时间
    PLAYED_TIME_LEVEL = 1   ///< 当前等级游戏时间
};

/** @brief 最大游戏时间索引数 */
#define MAX_PLAYED_TIME_INDEX 2

/**
 * @enum PlayerLoginQueryIndex
 * @brief 玩家登录查询索引枚举
 *
 * 用于玩家加载时的查询列表准备和结果选择
 */
enum PlayerLoginQueryIndex
{
    PLAYER_LOGIN_QUERY_LOAD_FROM                    = 0,   ///< 加载玩家基础数据
    PLAYER_LOGIN_QUERY_LOAD_GROUP                   = 1,   ///< 加载队伍数据
    PLAYER_LOGIN_QUERY_LOAD_BOUND_INSTANCES         = 2,   ///< 加载绑定实例
    PLAYER_LOGIN_QUERY_LOAD_AURAS                   = 3,   ///< 加载光环效果
    PLAYER_LOGIN_QUERY_LOAD_SPELLS                  = 4,   ///< 加载法术
    PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS            = 5,   ///< 加载任务状态
    PLAYER_LOGIN_QUERY_LOAD_DAILY_QUEST_STATUS      = 6,   ///< 加载日常任务状态
    PLAYER_LOGIN_QUERY_LOAD_REPUTATION              = 7,   ///< 加载声望
    PLAYER_LOGIN_QUERY_LOAD_INVENTORY               = 8,   ///< 加载背包物品
    PLAYER_LOGIN_QUERY_LOAD_ACTIONS                 = 9,   ///< 加载动作按钮
    PLAYER_LOGIN_QUERY_LOAD_MAILS                   = 10,  ///< 加载邮件
    PLAYER_LOGIN_QUERY_LOAD_MAIL_ITEMS              = 11,  ///< 加载邮件物品
    PLAYER_LOGIN_QUERY_LOAD_SOCIAL_LIST             = 12,  ///< 加载社交列表
    PLAYER_LOGIN_QUERY_LOAD_HOME_BIND               = 13,  ///< 加载炉石绑定位置
    PLAYER_LOGIN_QUERY_LOAD_SPELL_COOLDOWNS         = 14,  ///< 加载法术冷却
    PLAYER_LOGIN_QUERY_LOAD_DECLINED_NAMES          = 15,  ///< 加载名字变格
    PLAYER_LOGIN_QUERY_LOAD_GUILD                   = 16,  ///< 加载公会信息
    PLAYER_LOGIN_QUERY_LOAD_ARENA_INFO              = 17,  ///< 加载竞技场信息
    PLAYER_LOGIN_QUERY_LOAD_ACHIEVEMENTS            = 18,  ///< 加载成就
    PLAYER_LOGIN_QUERY_LOAD_CRITERIA_PROGRESS       = 19,  ///< 加载成就进度
    PLAYER_LOGIN_QUERY_LOAD_EQUIPMENT_SETS          = 20,  ///< 加载装备方案
    PLAYER_LOGIN_QUERY_LOAD_BG_DATA                 = 21,  ///< 加载战场数据
    PLAYER_LOGIN_QUERY_LOAD_GLYPHS                  = 22,  ///< 加载雕文
    PLAYER_LOGIN_QUERY_LOAD_TALENTS                 = 23,  ///< 加载天赋
    PLAYER_LOGIN_QUERY_LOAD_ACCOUNT_DATA            = 24,  ///< 加载账号数据
    PLAYER_LOGIN_QUERY_LOAD_SKILLS                  = 25,  ///< 加载技能
    PLAYER_LOGIN_QUERY_LOAD_WEEKLY_QUEST_STATUS     = 26,  ///< 加载周常任务状态
    PLAYER_LOGIN_QUERY_LOAD_RANDOM_BG               = 27,  ///< 加载随机战场
    PLAYER_LOGIN_QUERY_LOAD_BANNED                  = 28,  ///< 加载封禁状态
    PLAYER_LOGIN_QUERY_LOAD_QUEST_STATUS_REW        = 29,  ///< 加载任务奖励状态
    PLAYER_LOGIN_QUERY_LOAD_INSTANCE_LOCK_TIMES     = 30,  ///< 加载实例锁定时间
    PLAYER_LOGIN_QUERY_LOAD_SEASONAL_QUEST_STATUS   = 31,  ///< 加载季节性任务状态
    PLAYER_LOGIN_QUERY_LOAD_MONTHLY_QUEST_STATUS    = 32,  ///< 加载月常任务状态
    PLAYER_LOGIN_QUERY_LOAD_CORPSE_LOCATION         = 33,  ///< 加载尸体位置
    PLAYER_LOGIN_QUERY_LOAD_PET_SLOTS               = 34,  ///< 加载宠物槽位
    MAX_PLAYER_LOGIN_QUERY                                ///< 查询总数
};

/**
 * @enum PlayerDelayedOperations
 * @brief 玩家延迟操作枚举
 */
enum PlayerDelayedOperations
{
    DELAYED_SAVE_PLAYER         = 0x01,  ///< 延迟保存玩家
    DELAYED_RESURRECT_PLAYER    = 0x02,  ///< 延迟复活玩家
    DELAYED_SPELL_CAST_DESERTER = 0x04,  ///< 延迟施放逃兵法术
    DELAYED_BG_MOUNT_RESTORE    = 0x08,  ///< 从战场传送后恢复坐骑状态
    DELAYED_BG_TAXI_RESTORE     = 0x10,  ///< 从战场传送后恢复飞行状态
    DELAYED_END                          ///< 延迟操作结束标记
};

// Player summoning auto-decline time (in secs)
#define MAX_PLAYER_SUMMON_DELAY                   (2*MINUTE)
// Maximum money amount : 2^31 - 1
TC_GAME_API extern uint32 const MAX_MONEY_AMOUNT;

enum BindExtensionState
{
    EXTEND_STATE_EXPIRED  =   0,
    EXTEND_STATE_NORMAL   =   1,
    EXTEND_STATE_EXTENDED =   2,
    EXTEND_STATE_KEEP     = 255   // special state: keep current save type
};
struct InstancePlayerBind
{
    InstanceSave* save;
    /* permanent PlayerInstanceBinds are created in Raid/Heroic instances for players
    that aren't already permanently bound when they are inside when a boss is killed
    or when they enter an instance that the group leader is permanently bound to. */
    bool perm;
    /* extend state listing:
    EXPIRED  - doesn't affect anything unless manually re-extended by player
    NORMAL   - standard state
    EXTENDED - won't be promoted to EXPIRED at next reset period, will instead be promoted to NORMAL */
    BindExtensionState extendState;

    InstancePlayerBind() : save(nullptr), perm(false), extendState(EXTEND_STATE_NORMAL) { }
};

enum CharDeleteMethod
{
    CHAR_DELETE_REMOVE = 0,                      // Completely remove from the database
    CHAR_DELETE_UNLINK = 1                       // The character gets unlinked from the account,
                                                 // the name gets freed up and appears as deleted ingame
};

enum CurrencyItems
{
    ITEM_HONOR_POINTS_ID    = 43308,
    ITEM_ARENA_POINTS_ID    = 43307
};

enum ReferAFriendError
{
    ERR_REFER_A_FRIEND_NONE                          = 0x00,
    ERR_REFER_A_FRIEND_NOT_REFERRED_BY               = 0x01,
    ERR_REFER_A_FRIEND_TARGET_TOO_HIGH               = 0x02,
    ERR_REFER_A_FRIEND_INSUFFICIENT_GRANTABLE_LEVELS = 0x03,
    ERR_REFER_A_FRIEND_TOO_FAR                       = 0x04,
    ERR_REFER_A_FRIEND_DIFFERENT_FACTION             = 0x05,
    ERR_REFER_A_FRIEND_NOT_NOW                       = 0x06,
    ERR_REFER_A_FRIEND_GRANT_LEVEL_MAX_I             = 0x07,
    ERR_REFER_A_FRIEND_NO_TARGET                     = 0x08,
    ERR_REFER_A_FRIEND_NOT_IN_GROUP                  = 0x09,
    ERR_REFER_A_FRIEND_SUMMON_LEVEL_MAX_I            = 0x0A,
    ERR_REFER_A_FRIEND_SUMMON_COOLDOWN               = 0x0B,
    ERR_REFER_A_FRIEND_INSUF_EXPAN_LVL               = 0x0C,
    ERR_REFER_A_FRIEND_SUMMON_OFFLINE_S              = 0x0D
};

enum PlayerRestState : uint8
{
    REST_STATE_RESTED                                = 0x01,
    REST_STATE_NOT_RAF_LINKED                        = 0x02,
    REST_STATE_RAF_LINKED                            = 0x06
};

enum PlayerCommandStates
{
    CHEAT_NONE      = 0x00,
    CHEAT_GOD       = 0x01,
    CHEAT_CASTTIME  = 0x02,
    CHEAT_COOLDOWN  = 0x04,
    CHEAT_POWER     = 0x08,
    CHEAT_WATERWALK = 0x10
};

class Player;

/// Holder for Battleground data
struct BGData
{
    BGData() : bgInstanceID(0), bgTypeID(BATTLEGROUND_TYPE_NONE), bgAfkReportedCount(0), bgAfkReportedTimer(0),
        bgTeam(0), mountSpell(0) { ClearTaxiPath(); }

    uint32 bgInstanceID;                    ///< This variable is set to bg->m_InstanceID,
                                            ///  when player is teleported to BG - (it is battleground's GUID)
    BattlegroundTypeId bgTypeID;

    std::set<uint32>   bgAfkReporter;
    uint8              bgAfkReportedCount;
    time_t             bgAfkReportedTimer;

    uint32 bgTeam;                          ///< What side the player will be added to

    uint32 mountSpell;
    uint32 taxiPath[2];

    WorldLocation joinPos;                  ///< From where player entered BG

    void ClearTaxiPath()     { taxiPath[0] = taxiPath[1] = 0; }
    bool HasTaxiPath() const { return taxiPath[0] && taxiPath[1]; }
};

struct TradeStatusInfo
{
    TradeStatusInfo() : Status(TRADE_STATUS_BUSY), TraderGuid(), Result(EQUIP_ERR_OK),
        IsTargetResult(false), ItemLimitCategoryId(0), Slot(0) { }

    TradeStatus Status;
    ObjectGuid TraderGuid;
    InventoryResult Result;
    bool IsTargetResult;
    uint32 ItemLimitCategoryId;
    uint8 Slot;
};

struct ResurrectionData
{
    ObjectGuid GUID;
    WorldLocation Location;
    uint32 Health;
    uint32 Mana;
    uint32 Aura;
};

#define SPELL_DK_RAISE_ALLY 46619

// ============================================================================
// Player 类 - 玩家实体类
// 继承自 Unit 和 GridObject<Player>
// 负责玩家角色的所有数据和行为管理
// ============================================================================
class TC_GAME_API Player : public Unit, public GridObject<Player>
{
    friend class WorldSession;
    friend class CinematicMgr;
    friend void AddItemToUpdateQueueOf(Item* item, Player* player);
    friend void RemoveItemFromUpdateQueueOf(Item* item, Player* player);
    public:
        // 构造函数 - 初始化玩家对象
        explicit Player(WorldSession* session);
        // 析构函数 - 清理玩家资源
        ~Player();

        // 获取玩家AI控制器
        PlayerAI* AI() const { return reinterpret_cast<PlayerAI*>(GetAI()); }

        // 删除前的清理工作
        void CleanupsBeforeDelete(bool finalCleanup = true) override;

        // 将玩家添加到世界
        void AddToWorld() override;
        // 将玩家从世界中移除
        void RemoveFromWorld() override;

        // 设置对象缩放比例
        void SetObjectScale(float scale) override;

        // 传送玩家到指定位置（通过地图ID和坐标）
        bool TeleportTo(uint32 mapid, float x, float y, float z, float orientation, uint32 options = 0);
        // 传送玩家到指定位置（通过WorldLocation）
        bool TeleportTo(WorldLocation const& loc, uint32 options = 0);
        // 传送玩家到战场入口点
        bool TeleportToBGEntryPoint();

        // 检查是否有待处理的召唤请求
        bool HasSummonPending() const;
        // 发送召唤请求给玩家
        void SendSummonRequestFrom(Unit* summoner);
        // 如果可能则执行召唤
        void SummonIfPossible(bool agree);

        bool Create(ObjectGuid::LowType guidlow, CharacterCreateInfo* createInfo);

        void Update(uint32 time) override;

        static bool BuildEnumData(PreparedQueryResult result, WorldPacket* data);

        bool IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const override;

        bool IsFalling() { return GetPositionZ() < m_lastFallZ; }
        bool IsInAreaTriggerRadius(AreaTriggerEntry const* trigger) const;

        void SendInitialPacketsBeforeAddToMap();
        void SendInitialPacketsAfterAddToMap();
        void SendSupercededSpell(uint32 oldSpell, uint32 newSpell) const;
        void SendTransferAborted(uint32 mapid, TransferAbortReason reason, uint8 arg = 0) const;
        void SendInstanceResetWarning(uint32 mapid, Difficulty difficulty, uint32 time, bool welcome) const;

        bool CanInteractWithQuestGiver(Object* questGiver) const;
        Creature* GetNPCIfCanInteractWith(ObjectGuid const& guid, NPCFlags npcFlags) const;
        GameObject* GetGameObjectIfCanInteractWith(ObjectGuid const& guid) const;
        GameObject* GetGameObjectIfCanInteractWith(ObjectGuid const& guid, GameobjectTypes type) const;

        void ToggleAFK();
        void ToggleDND();
        bool isAFK() const { return HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_AFK); }
        bool isDND() const { return HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_DND); }
        uint8 GetChatTag() const;
        std::string autoReplyMsg;

        uint32 GetBarberShopCost(uint8 newhairstyle, uint8 newhaircolor, uint8 newfacialhair, BarberShopStyleEntry const* newSkin = nullptr) const;

        PlayerSocial* GetSocial() { return m_social; }
        void RemoveSocial();

        PlayerTaxi m_taxi;
        void InitTaxiNodesForLevel() { m_taxi.InitTaxiNodesForLevel(GetRace(), GetClass(), GetLevel()); }
        bool ActivateTaxiPathTo(std::vector<uint32> const& nodes, Creature* npc = nullptr, uint32 spellid = 0);
        bool ActivateTaxiPathTo(uint32 taxi_path_id, uint32 spellid = 0);
        void FinishTaxiFlight();
        void CleanupAfterTaxiFlight();
        void ContinueTaxiFlight() const;
        void SendTaxiNodeStatusMultiple();

        bool IsDeveloper() const { return HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_DEVELOPER); }
        void SetDeveloper(bool on) { ApplyModFlag(PLAYER_FLAGS, PLAYER_FLAGS_DEVELOPER, on); }
        bool isAcceptWhispers() const { return (m_ExtraFlags & PLAYER_EXTRA_ACCEPT_WHISPERS) != 0; }
        void SetAcceptWhispers(bool on) { if (on) m_ExtraFlags |= PLAYER_EXTRA_ACCEPT_WHISPERS; else m_ExtraFlags &= ~PLAYER_EXTRA_ACCEPT_WHISPERS; }
        bool IsGameMaster() const { return (m_ExtraFlags & PLAYER_EXTRA_GM_ON) != 0; }
        bool IsGameMasterAcceptingWhispers() const { return IsGameMaster() && isAcceptWhispers(); }
        bool CanBeGameMaster() const;
        void SetGameMaster(bool on);
        bool isGMChat() const { return (m_ExtraFlags & PLAYER_EXTRA_GM_CHAT) != 0; }
        void SetGMChat(bool on) { if (on) m_ExtraFlags |= PLAYER_EXTRA_GM_CHAT; else m_ExtraFlags &= ~PLAYER_EXTRA_GM_CHAT; }
        bool isTaxiCheater() const { return (m_ExtraFlags & PLAYER_EXTRA_TAXICHEAT) != 0; }
        void SetTaxiCheater(bool on) { if (on) m_ExtraFlags |= PLAYER_EXTRA_TAXICHEAT; else m_ExtraFlags &= ~PLAYER_EXTRA_TAXICHEAT; }
        bool isGMVisible() const { return !(m_ExtraFlags & PLAYER_EXTRA_GM_INVISIBLE); }
        void SetGMVisible(bool on);
        bool Has310Flyer(bool checkAllSpells, uint32 excludeSpellId = 0);
        void SetHas310Flyer(bool on) { if (on) m_ExtraFlags |= PLAYER_EXTRA_HAS_310_FLYER; else m_ExtraFlags &= ~PLAYER_EXTRA_HAS_310_FLYER; }
        void SetPvPDeath(bool on) { if (on) m_ExtraFlags |= PLAYER_EXTRA_PVP_DEATH; else m_ExtraFlags &= ~PLAYER_EXTRA_PVP_DEATH; }
        bool HasRaceChanged() const { return (m_ExtraFlags & PLAYER_EXTRA_HAS_RACE_CHANGED) != 0; }
        void SetHasRaceChanged() { m_ExtraFlags |= PLAYER_EXTRA_HAS_RACE_CHANGED; }
        bool HasBeenGrantedLevelsFromRaF() const { return (m_ExtraFlags & PLAYER_EXTRA_GRANTED_LEVELS_FROM_RAF) != 0; }
        void SetBeenGrantedLevelsFromRaF() { m_ExtraFlags |= PLAYER_EXTRA_GRANTED_LEVELS_FROM_RAF; }
        bool HasLevelBoosted() const { return (m_ExtraFlags & PLAYER_EXTRA_LEVEL_BOOSTED) != 0; }
        void SetHasLevelBoosted() { m_ExtraFlags |= PLAYER_EXTRA_LEVEL_BOOSTED; }

        uint32 GetXP() const { return GetUInt32Value(PLAYER_XP); }
        uint32 GetXPForNextLevel() const { return GetUInt32Value(PLAYER_NEXT_LEVEL_XP); }
        void SetXP(uint32 xp) { SetUInt32Value(PLAYER_XP, xp); }
        void GiveXP(uint32 xp, Unit* victim, float group_rate = 1.0f);
        void GiveLevel(uint8 level);
        bool IsMaxLevel() const;

        void InitStatsForLevel(bool reapplyMods = false);

        // .cheat command related
        bool GetCommandStatus(uint32 command) const { return (_activeCheats & command) != 0; }
        void SetCommandStatusOn(uint32 command) { _activeCheats |= command; }
        void SetCommandStatusOff(uint32 command) { _activeCheats &= ~command; }

        // Played Time Stuff
        time_t m_logintime;
        time_t m_Last_tick;
        uint32 m_Played_time[MAX_PLAYED_TIME_INDEX];
        uint32 GetTotalPlayedTime() const { return m_Played_time[PLAYED_TIME_TOTAL]; }
        uint32 GetLevelPlayedTime() const { return m_Played_time[PLAYED_TIME_LEVEL]; }

        Gender GetNativeGender() const override { return Gender(GetByteValue(PLAYER_BYTES_3, PLAYER_BYTES_3_OFFSET_GENDER)); }
        void SetNativeGender(Gender gender) override { SetByteValue(PLAYER_BYTES_3, PLAYER_BYTES_3_OFFSET_GENDER, gender); }
        uint8 GetSkinId() const { return GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID); }
        void SetSkinId(uint8 skin) { SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_SKIN_ID, skin); }
        uint8 GetFaceId() const { return GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID); }
        void SetFaceId(uint8 face) { SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_FACE_ID, face); }
        uint8 GetHairStyleId() const { return GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID); }
        void SetHairStyleId(uint8 hairStyle) { SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_STYLE_ID, hairStyle); }
        uint8 GetHairColorId() const { return GetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID); }
        void SetHairColorId(uint8 hairColor) { SetByteValue(PLAYER_BYTES, PLAYER_BYTES_OFFSET_HAIR_COLOR_ID, hairColor); }
        uint8 GetFacialStyle() const { return GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE); }
        void SetFacialStyle(uint8 facialStyle) { SetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_FACIAL_STYLE, facialStyle); }

        void setDeathState(DeathState s) override;                   // overwrite Unit::setDeathState

        float GetRestBonus() const { return m_rest_bonus; }
        void SetRestBonus(float rest_bonus_new);

        uint8 GetRestState() const { return GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_REST_STATE); }
        void SetRestState(uint8 restState) { SetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_REST_STATE, restState); }

        bool HasRestFlag(RestFlag restFlag) const { return (_restFlagMask & restFlag) != 0; }
        void SetRestFlag(RestFlag restFlag, uint32 triggerId = 0);
        void RemoveRestFlag(RestFlag restFlag);

        uint32 GetXPRestBonus(uint32 xp);
        uint32 GetInnTriggerId() const { return inn_triggerId; }

        PetStable* GetPetStable() { return m_petStable.get(); }
        PetStable& GetOrInitPetStable();
        PetStable const* GetPetStable() const { return m_petStable.get(); }

        Pet* GetPet() const;
        Pet* SummonPet(uint32 entry, float x, float y, float z, float ang, PetType petType, uint32 despwtime);
        void RemovePet(Pet* pet, PetSaveMode mode, bool returnreagent = false);
        uint32 GetPhaseMaskForSpawn() const;                // used for proper set phase for DB at GM-mode creature/GO spawn

        // pet auras
        std::unordered_set<PetAura const*> m_petAuras;
        void AddPetAura(PetAura const* petSpell);
        void RemovePetAura(PetAura const* petSpell);

        /// Handles said message in regular chat based on declared language and in config pre-defined Range.
        void Say(std::string_view text, Language language, WorldObject const* = nullptr) override;
        void Say(uint32 textId, WorldObject const* target = nullptr) override;
        /// Handles yelled message in regular chat based on declared language and in config pre-defined Range.
        void Yell(std::string_view text, Language language, WorldObject const* = nullptr) override;
        void Yell(uint32 textId, WorldObject const* target = nullptr) override;
        /// Outputs an universal text which is supposed to be an action.
        void TextEmote(std::string_view text, WorldObject const* = nullptr, bool = false) override;
        void TextEmote(uint32 textId, WorldObject const* target = nullptr, bool isBossEmote = false) override;
        /// Handles whispers from Addons and players based on sender, receiver's guid and language.
        void Whisper(std::string_view text, Language language, Player* receiver, bool = false) override;
        void Whisper(uint32 textId, Player* target, bool isBossWhisper = false) override;

        /*********************************************************/
        /***                    STORAGE SYSTEM                 ***/
        /*********************************************************/

        void SetVirtualItemSlot(uint8 i, Item* item);
        void SetSheath(SheathState sheathed) override;             // overwrite Unit version
        uint8 FindEquipSlot(ItemTemplate const* proto, uint32 slot, bool swap) const;
        uint32 GetItemCount(uint32 item, bool inBankAlso = false, Item* skipItem = nullptr) const;
        uint32 GetItemCountWithLimitCategory(uint32 limitCategory, Item* skipItem = nullptr) const;
        Item* GetItemByGuid(ObjectGuid guid) const;
        Item* GetItemByEntry(uint32 entry) const;
        Item* GetItemByPos(uint16 pos) const;
        Item* GetItemByPos(uint8 bag, uint8 slot) const;
        Item* GetUseableItemByPos(uint8 bag, uint8 slot) const;
        Bag*  GetBagByPos(uint8 slot) const;
        uint32 GetFreeInventorySpace() const;
        Item* GetWeaponForAttack(WeaponAttackType attackType, bool useable = false) const;
        Item* GetShield(bool useable = false) const;
        static WeaponAttackType GetAttackBySlot(uint8 slot);        // MAX_ATTACK if not weapon slot
        std::vector<Item*>& GetItemUpdateQueue() { return m_itemUpdateQueue; }
        static bool IsInventoryPos(uint16 pos) { return IsInventoryPos(pos >> 8, pos & 255); }
        static bool IsInventoryPos(uint8 bag, uint8 slot);
        static bool IsEquipmentPos(uint16 pos) { return IsEquipmentPos(pos >> 8, pos & 255); }
        static bool IsEquipmentPos(uint8 bag, uint8 slot);
        static bool IsBagPos(uint16 pos);
        static bool IsBankPos(uint16 pos) { return IsBankPos(pos >> 8, pos & 255); }
        static bool IsBankPos(uint8 bag, uint8 slot);
        bool IsValidPos(uint16 pos, bool explicit_pos) const { return IsValidPos(pos >> 8, pos & 255, explicit_pos); }
        bool IsValidPos(uint8 bag, uint8 slot, bool explicit_pos) const;
        uint8 GetBankBagSlotCount() const { return GetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_BANK_BAG_SLOTS); }
        void SetBankBagSlotCount(uint8 count) { SetByteValue(PLAYER_BYTES_2, PLAYER_BYTES_2_OFFSET_BANK_BAG_SLOTS, count); }
        bool HasItemCount(uint32 item, uint32 count = 1, bool inBankAlso = false) const;
        bool HasItemFitToSpellRequirements(SpellInfo const* spellInfo, Item const* ignoreItem = nullptr) const;
        bool CanNoReagentCast(SpellInfo const* spellInfo) const;
        bool HasItemOrGemWithIdEquipped(uint32 item, uint32 count, uint8 except_slot = NULL_SLOT) const;
        bool HasItemWithLimitCategoryEquipped(uint32 limitCategory, uint32 count, uint8 except_slot = NULL_SLOT) const;
        bool HasGemWithLimitCategoryEquipped(uint32 limitCategory, uint32 count, uint8 except_slot = NULL_SLOT) const;
        InventoryResult CanTakeMoreSimilarItems(Item* pItem, uint32* itemLimitCategory = nullptr) const;
        InventoryResult CanTakeMoreSimilarItems(uint32 entry, uint32 count, uint32* itemLimitCategory = nullptr) const { return CanTakeMoreSimilarItems(entry, count, nullptr, nullptr, itemLimitCategory); }
        InventoryResult CanStoreNewItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, uint32 item, uint32 count, uint32* no_space_count = nullptr) const;
        InventoryResult CanStoreItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, Item* pItem, bool swap = false) const;
        InventoryResult CanStoreItems(Item** items, int count, uint32* itemLimitCategory) const;
        InventoryResult CanEquipNewItem(uint8 slot, uint16& dest, uint32 item, bool swap) const;
        InventoryResult CanEquipItem(uint8 slot, uint16& dest, Item* pItem, bool swap, bool not_loading = true) const;

        InventoryResult CanEquipUniqueItem(Item* pItem, uint8 except_slot = NULL_SLOT, uint32 limit_count = 1) const;
        InventoryResult CanEquipUniqueItem(ItemTemplate const* itemProto, uint8 except_slot = NULL_SLOT, uint32 limit_count = 1) const;
        InventoryResult CanUnequipItems(uint32 item, uint32 count) const;
        InventoryResult CanUnequipItem(uint16 src, bool swap) const;
        InventoryResult CanBankItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, Item* pItem, bool swap, bool not_loading = true) const;
        InventoryResult CanUseItem(Item* pItem, bool not_loading = true) const;
        bool HasItemTotemCategory(uint32 TotemCategory) const;
        InventoryResult CanUseItem(ItemTemplate const* pItem) const;
        InventoryResult CanUseAmmo(uint32 item) const;
        InventoryResult CanRollForItemInLFG(ItemTemplate const* item, WorldObject const* lootedObject) const;
        Item* StoreNewItem(ItemPosCountVec const& pos, uint32 item, bool update, int32 randomPropertyId = 0, GuidSet const& allowedLooters = GuidSet());
        Item* StoreItem(ItemPosCountVec const& pos, Item* pItem, bool update);
        Item* EquipNewItem(uint16 pos, uint32 item, bool update);
        Item* EquipItem(uint16 pos, Item* pItem, bool update);
        void AutoUnequipOffhandIfNeed(bool force = false);
        bool StoreNewItemInBestSlots(uint32 item_id, uint32 item_count);
        void AutoStoreLoot(uint8 bag, uint8 slot, uint32 loot_id, LootStore const& store, bool broadcast = false, bool createdByPlayer = false);
        void AutoStoreLoot(uint32 loot_id, LootStore const& store, bool broadcast = false, bool createdByPlayer = false) { AutoStoreLoot(NULL_BAG, NULL_SLOT, loot_id, store, broadcast, createdByPlayer); }
        void StoreLootItem(uint8 lootSlot, Loot* loot);

        InventoryResult CanTakeMoreSimilarItems(uint32 entry, uint32 count, Item* pItem, uint32* no_space_count = nullptr, uint32* itemLimitCategory = nullptr) const;
        InventoryResult CanStoreItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, uint32 entry, uint32 count, Item* pItem = nullptr, bool swap = false, uint32* no_space_count = nullptr) const;

        void AddRefundReference(ObjectGuid it);
        void DeleteRefundReference(ObjectGuid it);

        void ApplyEquipCooldown(Item* pItem);
        void SetAmmo(uint32 item);
        void RemoveAmmo();
        float GetAmmoDPS() const { return m_ammoDPS; }
        bool CheckAmmoCompatibility(ItemTemplate const* ammo_proto) const;
        void QuickEquipItem(uint16 pos, Item* pItem);
        void VisualizeItem(uint8 slot, Item* pItem);
        void SetVisibleItemSlot(uint8 slot, Item* pItem);
        Item* BankItem(ItemPosCountVec const& dest, Item* pItem, bool update);
        void RemoveItem(uint8 bag, uint8 slot, bool update);
        void MoveItemFromInventory(uint8 bag, uint8 slot, bool update);
                                                            // in trade, auction, guild bank, mail....
        void MoveItemToInventory(ItemPosCountVec const& dest, Item* pItem, bool update, bool in_characterInventoryDB = false);
                                                            // in trade, guild bank, mail....
        void RemoveItemDependentAurasAndCasts(Item* pItem);
        void DestroyItem(uint8 bag, uint8 slot, bool update);
        uint32 DestroyItemCount(uint32 item, uint32 count, bool update, bool unequip_check = false);
        void DestroyItemCount(Item* item, uint32& count, bool update);
        void DestroyConjuredItems(bool update);
        void DestroyZoneLimitedItem(bool update, uint32 new_zone);
        void SplitItem(uint16 src, uint16 dst, uint32 count);
        void SwapItem(uint16 src, uint16 dst);
        void AddItemToBuyBackSlot(Item* pItem);
        Item* GetItemFromBuyBackSlot(uint32 slot);
        void RemoveItemFromBuyBackSlot(uint32 slot, bool del);
        uint32 GetMaxKeyringSize() const { return KEYRING_SLOT_END-KEYRING_SLOT_START; }
        void SendEquipError(InventoryResult msg, Item* pItem, Item* pItem2 = nullptr, uint32 itemid = 0) const;
        void SendBuyError(BuyResult msg, Creature* creature, uint32 item, uint32 param) const;
        void SendSellError(SellResult msg, Creature* creature, ObjectGuid guid, uint32 param) const;
        void AddWeaponProficiency(uint32 newflag) { m_WeaponProficiency |= newflag; }
        void AddArmorProficiency(uint32 newflag) { m_ArmorProficiency |= newflag; }
        uint32 GetWeaponProficiency() const { return m_WeaponProficiency; }
        uint32 GetArmorProficiency() const { return m_ArmorProficiency; }
        bool IsUseEquipedWeapon(bool mainhand) const;
        bool IsTwoHandUsed() const;
        bool IsUsingTwoHandedWeaponInOneHand() const;
        void SendNewItem(Item* item, uint32 count, bool received, bool created, bool broadcast = false, bool sendChatMessage = true);
        bool BuyItemFromVendorSlot(ObjectGuid vendorguid, uint32 vendorslot, uint32 item, uint8 count, uint8 bag, uint8 slot);
        bool _StoreOrEquipNewItem(uint32 vendorslot, uint32 item, uint8 count, uint8 bag, uint8 slot, int32 price, ItemTemplate const* pProto, Creature* pVendor, VendorItem const* crItem, bool bStore);

        float GetReputationPriceDiscount(Creature const* creature) const;
        float GetReputationPriceDiscount(FactionTemplateEntry const* factionTemplate) const;

        Player* GetTrader() const;
        TradeData* GetTradeData() const { return m_trade; }
        void TradeCancel(bool sendback, TradeStatus status = TRADE_STATUS_TRADE_CANCELED);

        CinematicMgr* GetCinematicMgr() const { return _cinematicMgr; }

        void UpdateEnchantTime(uint32 time);
        void UpdateSoulboundTradeItems();
        void AddTradeableItem(Item* item);
        void RemoveTradeableItem(Item* item);
        void UpdateItemDuration(uint32 time, bool realtimeonly = false);
        void AddEnchantmentDurations(Item* item);
        void RemoveEnchantmentDurations(Item* item);
        void RemoveEnchantmentDurationsReferences(Item* item);
        void RemoveArenaEnchantments(EnchantmentSlot slot);
        void AddEnchantmentDuration(Item* item, EnchantmentSlot slot, uint32 duration);
        void ApplyEnchantment(Item* item, EnchantmentSlot slot, bool apply, bool apply_dur = true, bool ignore_condition = false);
        void ApplyEnchantment(Item* item, bool apply);
        void UpdateSkillEnchantments(uint16 skill_id, uint16 curr_value, uint16 new_value);
        void SendEnchantmentDurations();
        void BuildEnchantmentsInfoData(WorldPacket* data);
        void AddItemDurations(Item* item);
        void RemoveItemDurations(Item* item);
        void SendItemDurations();
        void LoadCorpse(PreparedQueryResult result);
        void LoadPet();

        bool AddItem(uint32 itemId, uint32 count);

        /*********************************************************/
        /***                    GOSSIP SYSTEM                  ***/
        /*********************************************************/

        void PrepareGossipMenu(WorldObject* source, uint32 menuId = 0, bool showQuests = false);
        void SendPreparedGossip(WorldObject* source);
        void OnGossipSelect(WorldObject* source, uint32 gossipListId, uint32 menuId);

        uint32 GetGossipTextId(uint32 menuId, WorldObject* source);
        uint32 GetGossipTextId(WorldObject* source);
        static uint32 GetDefaultGossipMenuForSource(WorldObject* source);

        /*********************************************************/
        /***                    QUEST SYSTEM                   ***/
        /*********************************************************/

        int32 GetQuestLevel(Quest const* quest) const { return quest && (quest->GetQuestLevel() > 0) ? quest->GetQuestLevel() : GetLevel(); }

        void PrepareQuestMenu(ObjectGuid guid);
        void SendPreparedQuest(ObjectGuid guid);
        bool IsActiveQuest(uint32 quest_id) const;
        Quest const* GetNextQuest(ObjectGuid guid, Quest const* quest) const;
        bool CanSeeStartQuest(Quest const* quest) const;
        bool CanTakeQuest(Quest const* quest, bool msg) const;
        bool CanAddQuest(Quest const* quest, bool msg) const;
        bool CanCompleteQuest(uint32 quest_id);
        bool CanCompleteRepeatableQuest(Quest const* quest);
        bool CanRewardQuest(Quest const* quest, bool msg);
        bool CanRewardQuest(Quest const* quest, uint32 reward, bool msg);
        void AddQuestAndCheckCompletion(Quest const* quest, Object* questGiver);
        void AddQuest(Quest const* quest, Object* questGiver);
        void AbandonQuest(uint32 quest_id);
        void CompleteQuest(uint32 quest_id);
        void IncompleteQuest(uint32 quest_id);
        void RewardQuest(Quest const* quest, uint32 reward, Object* questGiver, bool announce = true);
        void SetRewardedQuest(uint32 quest_id);
        void FailQuest(uint32 quest_id);
        bool SatisfyQuestSkill(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestLevel(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestLog(bool msg) const;
        bool SatisfyQuestDependentQuests(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestPreviousQuest(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestDependentPreviousQuests(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestBreadcrumbQuest(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestDependentBreadcrumbQuests(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestClass(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestRace(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestReputation(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestStatus(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestConditions(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestTimed(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestExclusiveGroup(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestDay(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestWeek(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestMonth(Quest const* qInfo, bool msg) const;
        bool SatisfyQuestSeasonal(Quest const* qInfo, bool msg) const;
        bool GiveQuestSourceItem(Quest const* quest);
        bool TakeQuestSourceItem(uint32 questId, bool msg);
        bool GetQuestRewardStatus(uint32 quest_id) const;
        QuestStatus GetQuestStatus(uint32 quest_id) const;
        void SetQuestStatus(uint32 questId, QuestStatus status, bool update = true);
        void RemoveActiveQuest(uint32 questId, bool update = true);
        void RemoveRewardedQuest(uint32 questId, bool update = true);
        void SendQuestUpdate(uint32 questId);
        QuestGiverStatus GetQuestDialogStatus(Object* questGiver);

        void SetDailyQuestStatus(uint32 quest_id);
        bool IsDailyQuestDone(uint32 quest_id);
        void SetWeeklyQuestStatus(uint32 quest_id);
        void SetMonthlyQuestStatus(uint32 quest_id);
        void SetSeasonalQuestStatus(uint32 quest_id);
        void ResetDailyQuestStatus();
        void ResetWeeklyQuestStatus();
        void ResetMonthlyQuestStatus();
        void ResetSeasonalQuestStatus(uint16 event_id);

        uint16 FindQuestSlot(uint32 quest_id) const;
        uint32 GetQuestSlotQuestId(uint16 slot) const;
        uint32 GetQuestSlotState(uint16 slot) const;
        uint16 GetQuestSlotCounter(uint16 slot, uint8 counter) const;
        uint32 GetQuestSlotTime(uint16 slot) const;
        void SetQuestSlot(uint16 slot, uint32 quest_id, uint32 timer = 0);
        void SetQuestSlotCounter(uint16 slot, uint8 counter, uint16 count);
        void SetQuestSlotState(uint16 slot, uint32 state);
        void RemoveQuestSlotState(uint16 slot, uint32 state);
        void SetQuestSlotTimer(uint16 slot, uint32 timer);
        void SwapQuestSlot(uint16 slot1, uint16 slot2);

        uint16 GetReqKillOrCastCurrentCount(uint32 quest_id, int32 entry) const;
        void AreaExploredOrEventHappens(uint32 questId);
        void GroupEventHappens(uint32 questId, WorldObject const* pEventObject);
        void ItemAddedQuestCheck(uint32 entry, uint32 count);
        void ItemRemovedQuestCheck(uint32 entry, uint32 count);
        void KilledMonster(CreatureTemplate const* cInfo, ObjectGuid guid);
        void KilledMonsterCredit(uint32 entry, ObjectGuid guid = ObjectGuid::Empty);
        void KilledPlayerCredit(uint16 count = 1);
        void KilledPlayerCreditForQuest(uint16 count, Quest const* quest);
        void KillCreditGO(uint32 entry, ObjectGuid guid = ObjectGuid::Empty);
        void TalkedToCreature(uint32 entry, ObjectGuid guid);
        void MoneyChanged(uint32 value);
        void ReputationChanged(FactionEntry const* factionEntry);
        void ReputationChanged2(FactionEntry const* factionEntry);
        bool HasQuestForItem(uint32 itemId, uint32 excludeQuestId = 0, bool turnIn = false) const;
        bool HasQuestForGO(int32 goId) const;
        void UpdateVisibleGameobjectsOrSpellClicks();
        bool CanShareQuest(uint32 questId) const;

        void SendQuestComplete(uint32 questId) const;
        void SendQuestReward(Quest const* quest, uint32 XP) const;
        void SendQuestFailed(uint32 questId, InventoryResult reason = EQUIP_ERR_OK) const;
        void SendQuestTimerFailed(uint32 questId) const;
        void SendCanTakeQuestResponse(QuestFailedReason msg) const;
        void SendQuestConfirmAccept(Quest const* quest, Player* pReceiver) const;
        void SendPushToPartyResponse(Player const* player, QuestShareMessages msg) const;
        void SendQuestUpdateAddItem(Quest const* quest, uint32 itemIdx, uint16 count) const;
        void SendQuestUpdateAddCreatureOrGo(Quest const* quest, ObjectGuid guid, uint32 creatureOrGOIdx, uint16 oldCount, uint16 addCount);
        void SendQuestUpdateAddPlayer(Quest const* quest, uint16 oldCount, uint16 addCount);
        void SendQuestGiverStatusMultiple();

        uint32 GetSharedQuestID() const { return m_sharedQuestId; }
        ObjectGuid GetPlayerSharingQuest() const { return m_playerSharingQuest; }
        void SetQuestSharingInfo(ObjectGuid guid, uint32 id) { m_playerSharingQuest = guid; m_sharedQuestId = id; }
        void ClearQuestSharingInfo() { m_playerSharingQuest = ObjectGuid::Empty; m_sharedQuestId = 0; }

        uint32 GetInGameTime() const { return m_ingametime; }
        void SetInGameTime(uint32 time) { m_ingametime = time; }

        void AddTimedQuest(uint32 questId) { m_timedquests.insert(questId); }
        void RemoveTimedQuest(uint32 questId) { m_timedquests.erase(questId); }

        bool HasPvPForcingQuest() const;

        /*********************************************************/
        /***                   LOAD SYSTEM                     ***/
        /*********************************************************/

        bool LoadFromDB(ObjectGuid guid, CharacterDatabaseQueryHolder const& holder);
        bool IsLoading() const override;

        void Initialize(ObjectGuid::LowType guid);
        static uint32 GetZoneIdFromDB(ObjectGuid guid);
        static bool   LoadPositionFromDB(uint32& mapid, float& x, float& y, float& z, float& o, bool& in_flight, ObjectGuid guid);

        static bool IsValidGender(uint8 Gender) { return Gender <= GENDER_FEMALE; }
        static bool ValidateAppearance(uint8 race, uint8 class_, uint8 gender, uint8 hairID, uint8 hairColor, uint8 faceID, uint8 facialHair, uint8 skinColor, bool create = false);

        /*********************************************************/
        /***                   SAVE SYSTEM                     ***/
        /*********************************************************/

        void SaveToDB(bool create = false);
        void SaveToDB(CharacterDatabaseTransaction trans, bool create = false);
        void SaveInventoryAndGoldToDB(CharacterDatabaseTransaction trans);                    // fast save function for item/money cheating preventing
        void SaveGoldToDB(CharacterDatabaseTransaction trans) const;

        static void Customize(CharacterCustomizeInfo const* customizeInfo, CharacterDatabaseTransaction trans);
        static void SavePositionInDB(WorldLocation const& loc, uint16 zoneId, ObjectGuid guid, CharacterDatabaseTransaction trans);

        static void DeleteFromDB(ObjectGuid playerguid, uint32 accountId, bool updateRealmChars = true, bool deleteFinally = false);
        static void DeleteOldCharacters();
        static void DeleteOldCharacters(uint32 keepDays);

        bool m_mailsUpdated;

        void SetBindPoint(ObjectGuid guid) const;
        void SendTalentWipeConfirm(ObjectGuid trainerGuid) const;
        void ResetPetTalents();
        void RegenerateAll();
        void Regenerate(Powers power);
        void RegenerateHealth();
        void setRegenTimerCount(uint32 time) {m_regenTimerCount = time;}
        void setWeaponChangeTimer(uint32 time) {m_weaponChangeTimer = time;}

        uint32 GetMoney() const { return GetUInt32Value(PLAYER_FIELD_COINAGE); }
        bool ModifyMoney(int32 amount, bool sendError = true);
        bool HasEnoughMoney(uint32 amount) const { return (GetMoney() >= amount); }
        bool HasEnoughMoney(int32 amount) const { return (amount < 0) || HasEnoughMoney(uint32(amount)); }
        void SetMoney(uint32 value);

        RewardedQuestSet const& getRewardedQuests() const { return m_RewardedQuests; }
        QuestStatusMap& getQuestStatusMap() { return m_QuestStatus; }

        size_t GetRewardedQuestCount() const { return m_RewardedQuests.size(); }
        bool IsQuestRewarded(uint32 quest_id) const;

        Unit* GetSelectedUnit() const;
        Player* GetSelectedPlayer() const;

        void SetTarget(ObjectGuid /*guid*/) override { } /// Used for serverside target changes, does not apply to players
        void SetSelection(ObjectGuid guid) { SetGuidValue(UNIT_FIELD_TARGET, guid); }

        void SendMailResult(uint32 mailId, MailResponseType mailAction, MailResponseResult mailError, uint32 equipError = 0, ObjectGuid::LowType item_guid = 0, uint32 item_count = 0) const;
        void SendNewMail() const;
        void UpdateNextMailTimeAndUnreads();
        void AddNewMailDeliverTime(time_t deliver_time);

        void RemoveMail(uint32 id);

        void AddMail(Mail* mail) { m_mail.push_front(mail);}// for call from WorldSession::SendMailTo
        uint32 GetMailSize() { return m_mail.size();}
        Mail* GetMail(uint32 id);

        PlayerMails const& GetMails() const { return m_mail; }

        void SendItemRetrievalMail(uint32 itemEntry, uint32 count); // Item retrieval mails sent by The Postmaster (34337), used in multiple places.

        /*********************************************************/
        /*** MAILED ITEMS SYSTEM ***/
        /*********************************************************/

        uint8 unReadMails;
        time_t m_nextMailDelivereTime;

        typedef std::unordered_map<uint32, Item*> ItemMap;

        ItemMap mMitems;                                    //template defined in objectmgr.cpp

        Item* GetMItem(uint32 id);
        void AddMItem(Item* it);
        bool RemoveMItem(uint32 id);

        void SendOnCancelExpectedVehicleRideAura() const;
        void PetSpellInitialize();
        void CharmSpellInitialize();
        void PossessSpellInitialize();
        void VehicleSpellInitialize();
        void SendRemoveControlBar() const;
        bool HasSpell(uint32 spell) const override;
        bool HasActiveSpell(uint32 spell) const;            // show in spellbook
        bool IsSpellFitByClassAndRace(uint32 spell_id) const;
        bool HandlePassiveSpellLearn(SpellInfo const* spellInfo);

        void SendProficiency(ItemClass itemClass, uint32 itemSubclassMask) const;
        void SendInitialSpells();
        void SendUnlearnSpells();
        bool AddSpell(uint32 spellId, bool active, bool learning, bool dependent, bool disabled, bool loading = false, uint32 fromSkill = 0);
        void LearnSpell(uint32 spell_id, bool dependent, uint32 fromSkill = 0);
        void RemoveSpell(uint32 spell_id, bool disabled = false, bool learn_low_rank = true);
        void ResetSpells(bool myClassOnly = false);
        void LearnCustomSpells();
        void LearnDefaultSkills();
        void LearnDefaultSkill(uint32 skillId, uint16 rank);
        void LearnQuestRewardedSpells();
        void LearnQuestRewardedSpells(Quest const* quest);
        void AddTemporarySpell(uint32 spellId);
        void RemoveTemporarySpell(uint32 spellId);
        void SetReputation(uint32 factionentry, uint32 value);
        uint32 GetReputation(uint32 factionentry) const;
        std::string const& GetGuildName() const;
        uint32 GetFreeTalentPoints() const { return GetUInt32Value(PLAYER_CHARACTER_POINTS1); }
        void SetFreeTalentPoints(uint32 points);
        bool ResetTalents(bool involuntarily = false);
        uint32 ResetTalentsCost() const;
        void IncreaseResetTalentsCostAndCounters(uint32 lastResetTalentsCost);
        void InitTalentForLevel();
        void BuildPlayerTalentsInfoData(WorldPacket* data);
        void BuildPetTalentsInfoData(WorldPacket* data);
        void SendTalentsInfoData(bool pet);
        void LearnTalent(uint32 talentId, uint32 talentRank);
        void LearnPetTalent(ObjectGuid petGuid, uint32 talentId, uint32 talentRank);
        void SendTameFailure(uint8 result);

        bool AddTalent(uint32 spellId, uint8 spec, bool learning);
        bool HasTalent(uint32 spell_id, uint8 spec) const;

        uint32 CalculateTalentsPoints() const;

        // Dual Spec
        void UpdateSpecCount(uint8 count);
        uint32 GetActiveSpec() const { return m_activeSpec; }
        void SetActiveSpec(uint8 spec){ m_activeSpec = spec; }
        uint8 GetSpecsCount() const { return m_specsCount; }
        void SetSpecsCount(uint8 count) { m_specsCount = count; }
        void ActivateSpec(uint8 spec);
        void LoadActions(PreparedQueryResult result);

        void InitGlyphsForLevel();
        void SetGlyphSlot(uint8 slot, uint32 slottype) { SetUInt32Value(PLAYER_FIELD_GLYPH_SLOTS_1 + slot, slottype); }
        uint32 GetGlyphSlot(uint8 slot) { return GetUInt32Value(PLAYER_FIELD_GLYPH_SLOTS_1 + slot); }
        void SetGlyph(uint8 slot, uint32 glyph);
        uint32 GetGlyph(uint8 slot) { return m_Glyphs[m_activeSpec][slot]; }

        uint32 GetFreePrimaryProfessionPoints() const { return GetUInt32Value(PLAYER_CHARACTER_POINTS2); }
        void SetFreePrimaryProfessions(uint16 profs) { SetUInt32Value(PLAYER_CHARACTER_POINTS2, profs); }
        void InitPrimaryProfessions();

        PlayerSpellMap const& GetSpellMap() const { return m_spells; }
        PlayerSpellMap      & GetSpellMap()       { return m_spells; }

        void AddSpellMod(SpellModifier* mod, bool apply);
        static bool IsAffectedBySpellmod(SpellInfo const* spellInfo, SpellModifier* mod, Spell* spell = nullptr);
        template <class T>
        void ApplySpellMod(uint32 spellId, SpellModOp op, T& basevalue, Spell* spell = nullptr) const;
        static void ApplyModToSpell(SpellModifier* mod, Spell* spell);
        static bool HasSpellModApplied(SpellModifier* mod, Spell* spell);
        void SetSpellModTakingSpell(Spell* spell, bool apply);

        void RemoveArenaSpellCooldowns(bool removeActivePetCooldowns = false);
        uint32 GetLastPotionId() const { return m_lastPotionId; }
        void SetLastPotionId(uint32 item_id) { m_lastPotionId = item_id; }
        void UpdatePotionCooldown(Spell* spell = nullptr);

        void SetResurrectRequestData(WorldObject const* caster, uint32 health, uint32 mana, uint32 appliedAura);

        void ClearResurrectRequestData()
        {
            _resurrectionData.reset();
        }

        bool IsResurrectRequestedBy(ObjectGuid const& guid) const
        {
            if (!IsResurrectRequested())
                return false;

            return !_resurrectionData->GUID.IsEmpty() && _resurrectionData->GUID == guid;
        }

        bool IsResurrectRequested() const { return _resurrectionData.get() != nullptr; }
        void ResurrectUsingRequestData();
        void ResurrectUsingRequestDataImpl();

        uint8 getCinematic() const { return m_cinematic; }
        void setCinematic(uint8 cine) { m_cinematic = cine; }

        uint32 GetMovie() const { return m_movie; }
        void SetMovie(uint32 movie) { m_movie = movie; }

        ActionButton* addActionButton(uint8 button, uint32 action, uint8 type);
        void removeActionButton(uint8 button);
        ActionButton const* GetActionButton(uint8 button);
        void SendInitialActionButtons() const { SendActionButtons(1); }
        void SendActionButtons(uint32 state) const;
        bool IsActionButtonDataValid(uint8 button, uint32 action, uint8 type) const;

        PvPInfo pvpInfo;
        void InitPvP();
        void UpdatePvPState(bool onlyFFA = false);
        void SetPvP(bool state) override;
        void UpdatePvP(bool state, bool override = false);
        void UpdateZone(uint32 newZone, uint32 newArea);
        void UpdateArea(uint32 newArea);
        void SetNeedsZoneUpdate(bool needsUpdate) { m_needsZoneUpdate = needsUpdate; }

        void UpdateZoneDependentAuras(uint32 zone_id);    // zones
        void UpdateAreaDependentAuras(uint32 area_id);    // subzones

        void UpdateAfkReport(time_t currTime);
        void UpdatePvPFlag(time_t currTime);
        void SetContestedPvP(Player* attackedPlayer = nullptr);
        void UpdateContestedPvP(uint32 currTime);
        void SetContestedPvPTimer(uint32 newTime) {m_contestedPvPTimer = newTime;}
        void ResetContestedPvP();

        /// @todo: maybe move UpdateDuelFlag+DuelComplete to independent DuelHandler
        std::unique_ptr<DuelInfo> duel;
        void UpdateDuelFlag(time_t currTime);
        void CheckDuelDistance(time_t currTime);
        void DuelComplete(DuelCompleteType type);
        void SendDuelCountdown(uint32 counter);

        bool IsGroupVisibleFor(Player const* p) const;
        bool IsInSameGroupWith(Player const* p) const;
        bool IsInSameRaidWith(Player const* p) const;
        void UninviteFromGroup();
        static void RemoveFromGroup(Group* group, ObjectGuid guid, RemoveMethod method = GROUP_REMOVEMETHOD_DEFAULT, ObjectGuid kicker = ObjectGuid::Empty, char const* reason = nullptr);
        void RemoveFromGroup(RemoveMethod method = GROUP_REMOVEMETHOD_DEFAULT) { RemoveFromGroup(GetGroup(), GetGUID(), method); }
        void SendUpdateToOutOfRangeGroupMembers();

        void SetInGuild(uint32 guildId);
        void SetRank(uint8 rankId) { SetUInt32Value(PLAYER_GUILDRANK, rankId); }
        uint8 GetRank() const { return uint8(GetUInt32Value(PLAYER_GUILDRANK)); }
        void SetGuildIdInvited(uint32 GuildId) { m_GuildIdInvited = GuildId; }
        uint32 GetGuildId() const { return GetUInt32Value(PLAYER_GUILDID);  }
        Guild* GetGuild();
        int GetGuildIdInvited() const { return m_GuildIdInvited; }
        static void RemovePetitionsAndSigns(ObjectGuid guid, CharterTypes type);

        // Arena Team
        void SetInArenaTeam(uint32 ArenaTeamId, uint8 slot, uint8 type);
        void SetArenaTeamInfoField(uint8 slot, ArenaTeamInfoType type, uint32 value);
        static void LeaveAllArenaTeams(ObjectGuid guid);
        uint32 GetArenaTeamId(uint8 slot) const { return GetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (slot * ARENA_TEAM_END) + ARENA_TEAM_ID); }
        uint32 GetArenaPersonalRating(uint8 slot) const { return GetUInt32Value(PLAYER_FIELD_ARENA_TEAM_INFO_1_1 + (slot * ARENA_TEAM_END) + ARENA_TEAM_PERSONAL_RATING); }
        void SetArenaTeamIdInvited(uint32 ArenaTeamId) { m_ArenaTeamIdInvited = ArenaTeamId; }
        uint32 GetArenaTeamIdInvited() const { return m_ArenaTeamIdInvited; }

        Difficulty GetDifficulty(bool isRaid) const { return isRaid ? m_raidDifficulty : m_dungeonDifficulty; }
        Difficulty GetDungeonDifficulty() const { return m_dungeonDifficulty; }
        Difficulty GetRaidDifficulty() const { return m_raidDifficulty; }
        Difficulty GetStoredRaidDifficulty() const { return m_raidMapDifficulty; } // only for use in difficulty packet after exiting to raid map
        void SetDungeonDifficulty(Difficulty dungeon_difficulty) { m_dungeonDifficulty = dungeon_difficulty; }
        void SetRaidDifficulty(Difficulty raid_difficulty) { m_raidDifficulty = raid_difficulty; }
        void StoreRaidMapDifficulty();

        bool UpdateSkill(uint32 skill_id, uint32 step);
        bool UpdateSkillPro(uint16 SkillId, int32 Chance, uint32 step);

        bool UpdateCraftSkill(uint32 spellid);
        bool UpdateGatherSkill(uint32 SkillId, uint32 SkillValue, uint32 RedLevel, uint32 Multiplicator = 1);
        bool UpdateFishingSkill();

        uint32 GetBaseDefenseSkillValue() const { return GetBaseSkillValue(SKILL_DEFENSE); }
        uint32 GetBaseWeaponSkillValue(WeaponAttackType attType) const;

        float GetHealthBonusFromStamina();
        float GetManaBonusFromIntellect();

        bool UpdateStats(Stats stat) override;
        bool UpdateAllStats() override;
        void ApplySpellPenetrationBonus(int32 amount, bool apply);
        void UpdateResistances(uint32 school) override;
        void UpdateArmor() override;
        void UpdateMaxHealth() override;
        void UpdateMaxPower(Powers power) override;
        void ApplyFeralAPBonus(int32 amount, bool apply);
        void UpdateAttackPowerAndDamage(bool ranged = false) override;
        void UpdateShieldBlockValue();
        void ApplySpellPowerBonus(int32 amount, bool apply);
        void UpdateSpellDamageAndHealingBonus();
        void ApplyRatingMod(CombatRating cr, int32 value, bool apply);
        void UpdateRating(CombatRating cr);
        void UpdateAllRatings();

        void CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex) const override;

        void UpdateDefenseBonusesMod();
        void RecalculateRating(CombatRating cr) { ApplyRatingMod(cr, 0, true);}
        float GetMeleeCritFromAgility() const;
        void GetDodgeFromAgility(float &diminishing, float &nondiminishing) const;
        float GetMissPercentageFromDefense() const;
        float GetSpellCritFromIntellect() const;
        float OCTRegenHPPerSpirit() const;
        float OCTRegenMPPerSpirit() const;
        float GetRatingMultiplier(CombatRating cr) const;
        float GetRatingBonusValue(CombatRating cr) const;
        uint32 GetBaseSpellPowerBonus() const { return m_baseSpellPower; }
        int32 GetSpellPenetrationItemMod() const { return m_spellPenetrationItemMod; }

        bool CanApplyResilience() const override { return true; }

        float GetExpertiseDodgeOrParryReduction(WeaponAttackType attType) const;
        void UpdateBlockPercentage();
        void UpdateCritPercentage(WeaponAttackType attType);
        void UpdateAllCritPercentages();
        void UpdateParryPercentage();
        void UpdateDodgePercentage();
        void UpdateMeleeHitChances();
        void UpdateRangedHitChances();
        void UpdateSpellHitChances();

        void UpdateAllSpellCritChances();
        void UpdateSpellCritChance(uint32 school);
        void UpdateArmorPenetration(int32 amount);
        void UpdateExpertise(WeaponAttackType attType);
        void ApplyManaRegenBonus(int32 amount, bool apply);
        void ApplyHealthRegenBonus(int32 amount, bool apply);
        void UpdatePowerRegen(Powers power);
        void UpdateRuneRegen(RuneType rune);
        float GetPowerRegen(Powers power) const;
        uint32 GetRuneTimer(uint8 index) const { return m_runeGraceCooldown[index]; }
        void SetRuneTimer(uint8 index, uint32 timer) { m_runeGraceCooldown[index] = timer; }
        uint32 GetLastRuneGraceTimer(uint8 index) const { return m_lastRuneGraceTimers[index]; }
        void SetLastRuneGraceTimer(uint8 index, uint32 timer) { m_lastRuneGraceTimers[index] = timer; }

        ObjectGuid GetLootGUID() const { return m_lootGuid; }
        void SetLootGUID(ObjectGuid guid) { m_lootGuid = guid; }

        void RemovedInsignia(Player* looterPlr);

        WorldSession* GetSession() const { return m_session; }
        GameClient* GetGameClient() const;

        void BuildCreateUpdateBlockForPlayer(UpdateData* data, Player* target) const override;
        void DestroyForPlayer(Player* target, bool onDeath = false) const override;
        void SendLogXPGain(uint32 GivenXP, Unit* victim, uint32 BonusXP, bool recruitAFriend = false, float group_rate=1.0f) const;

        // notifiers
        void SendAttackSwingCantAttack() const;
        void SendAttackSwingCancelAttack() const;
        void SendAttackSwingDeadTarget() const;
        void SendAttackSwingNotInRange() const;
        void SendAttackSwingBadFacingAttack() const;
        void SendAutoRepeatCancel(Unit* target);
        void SendExplorationExperience(uint32 Area, uint32 Experience) const;

        void SendDungeonDifficulty(bool IsInGroup) const;
        void SendRaidDifficulty(bool IsInGroup, int32 forcedDifficulty = -1) const;
        void ResetInstances(uint8 method, bool isRaid);
        void SendResetInstanceSuccess(uint32 MapId) const;
        void SendResetInstanceFailed(uint32 reason, uint32 MapId) const;
        void SendResetFailedNotify(uint32 mapid) const;

        bool UpdatePosition(float x, float y, float z, float orientation, bool teleport = false) override;
        bool UpdatePosition(Position const& pos, bool teleport = false) override { return UpdatePosition(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation(), teleport); }
        void ProcessTerrainStatusUpdate(ZLiquidStatus oldLiquidStatus, Optional<LiquidData> const& newLiquidData) override;
        void AtExitCombat() override;

        void SendMessageToSet(WorldPacket const* data, bool self) const override { SendMessageToSetInRange(data, GetVisibilityRange(), self); }
        void SendMessageToSetInRange(WorldPacket const* data, float dist, bool self) const override;
        void SendMessageToSetInRange(WorldPacket const* data, float dist, bool self, bool own_team_only, bool required3dDist = false) const;
        void SendMessageToSet(WorldPacket const* data, Player const* skipped_rcvr) const override;

        Corpse* GetCorpse() const;
        void SpawnCorpseBones(bool triggerSave = true);
        Corpse* CreateCorpse();
        void KillPlayer();
        static void OfflineResurrect(ObjectGuid const& guid, CharacterDatabaseTransaction trans);
        bool HasCorpse() const { return _corpseLocation.GetMapId() != MAPID_INVALID; }
        WorldLocation const& GetCorpseLocation() const { return _corpseLocation; }
        uint32 GetResurrectionSpellId();
        void ResurrectPlayer(float restore_percent, bool applySickness = false);
        void BuildPlayerRepop();
        void RepopAtGraveyard();

        void RemoveGhoul();

        void SendDurabilityLoss();
        void DurabilityLossAll(double percent, bool inventory);
        void DurabilityLoss(Item* item, double percent);
        void DurabilityPointsLossAll(int32 points, bool inventory);
        void DurabilityPointsLoss(Item* item, int32 points);
        void DurabilityPointLossForEquipSlot(EquipmentSlots slot);
        void DurabilityRepairAll(bool takeCost, float discountMod, bool guildBank);
        void DurabilityRepair(uint16 pos, bool takeCost, float discountMod);

        void UpdateMirrorTimers();
        void StopMirrorTimers();
        bool IsMirrorTimerActive(MirrorTimerType type) const;

        void SetMovement(PlayerMovementType pType);

        bool CanJoinConstantChannelInZone(ChatChannelsEntry const* channel, AreaTableEntry const* zone) const;

        void JoinedChannel(Channel* c);
        void LeftChannel(Channel* c);
        void CleanupChannels();
        void UpdateLocalChannels(uint32 newZone);
        void LeaveLFGChannel();

        typedef std::list<Channel*> JoinedChannelsList;
        JoinedChannelsList const& GetJoinedChannels() const { return m_channels; }

        void UpdateDefense();
        void UpdateWeaponSkill(Unit* victim, WeaponAttackType attType);
        void UpdateCombatSkills(Unit* victim, WeaponAttackType attType, bool defense);

        void SetSkill(uint32 id, uint16 step, uint16 newVal, uint16 maxVal);
        uint16 GetMaxSkillValue(uint32 skill) const;        // max + perm. bonus + temp bonus
        uint16 GetPureMaxSkillValue(uint32 skill) const;    // max
        uint16 GetSkillValue(uint32 skill) const;           // skill value + perm. bonus + temp bonus
        uint16 GetBaseSkillValue(uint32 skill) const;       // skill value + perm. bonus
        uint16 GetPureSkillValue(uint32 skill) const;       // skill value
        int16 GetSkillPermBonusValue(uint32 skill) const;
        int16 GetSkillTempBonusValue(uint32 skill) const;
        uint16 GetSkillStep(uint32 skill) const;            // 0...6
        bool HasSkill(uint32 skill) const;
        void LearnSkillRewardedSpells(uint32 skillId, uint32 skillValue);

        WorldLocation& GetTeleportDest() { return m_teleport_dest; }
        uint32 GetTeleportOptions() const { return m_teleport_options; }
        bool IsBeingTeleported() const { return IsBeingTeleportedNear() || IsBeingTeleportedFar(); }
        bool IsBeingTeleportedNear() const { return mSemaphoreTeleport_Near; }
        bool IsBeingTeleportedFar() const { return mSemaphoreTeleport_Far; }
        void SetSemaphoreTeleportNear(bool semphsetting) { mSemaphoreTeleport_Near = semphsetting; }
        void SetSemaphoreTeleportFar(bool semphsetting) { mSemaphoreTeleport_Far = semphsetting; }
        void ProcessDelayedOperations();

        void CheckAreaExploreAndOutdoor(void);

        static uint32 TeamForRace(uint8 race);
        uint32 GetTeam() const { return m_team; }
        TeamId GetTeamId() const { return m_team == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE; }
        void SetFactionForRace(uint8 race);

        void InitDisplayIds();

        bool IsAtGroupRewardDistance(WorldObject const* pRewardSource) const;
        bool IsAtRecruitAFriendDistance(WorldObject const* pOther) const;
        void RewardPlayerAndGroupAtKill(Unit* victim, bool isBattleGround);
        void RewardPlayerAndGroupAtEvent(uint32 creature_id, WorldObject* pRewardSource);
        bool isHonorOrXPTarget(Unit* victim) const;

        bool GetsRecruitAFriendBonus(bool forXP);
        uint8 GetGrantableLevels() const { return m_grantableLevels; }
        void SetGrantableLevels(uint8 val) { m_grantableLevels = val; }

        ReputationMgr&       GetReputationMgr()       { return *m_reputationMgr; }
        ReputationMgr const& GetReputationMgr() const { return *m_reputationMgr; }
        ReputationRank GetReputationRank(uint32 faction_id) const;
        void RewardReputation(Unit* victim, float rate);
        void RewardReputation(Quest const* quest);

        int32 CalculateReputationGain(ReputationSource source, uint32 creatureOrQuestLevel, int32 rep, int32 faction, bool noQuestBonus = false);

        void UpdateSkillsForLevel();
        void UpdateWeaponsSkillsToMaxSkillsForLevel();             // for .levelup
        void ModifySkillBonus(uint32 skillid, int32 val, bool talent);

        /*********************************************************/
        /***                  PVP SYSTEM                       ***/
        /*********************************************************/
        void SetArenaFaction(uint8 arenaFaction) { SetByteValue(PLAYER_BYTES_3, PLAYER_BYTES_3_OFFSET_ARENA_FACTION, arenaFaction); }
        void UpdateHonorFields();
        bool RewardHonor(Unit* victim, uint32 groupsize, int32 honor = -1, bool pvptoken = false);
        uint32 GetHonorPoints() const { return GetUInt32Value(PLAYER_FIELD_HONOR_CURRENCY); }
        uint32 GetArenaPoints() const { return GetUInt32Value(PLAYER_FIELD_ARENA_CURRENCY); }
        void ModifyHonorPoints(int32 value, CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr));      //! If trans is specified, honor save query will be added to trans
        void ModifyArenaPoints(int32 value, CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr));      //! If trans is specified, arena point save query will be added to trans
        uint32 GetMaxPersonalArenaRatingRequirement(uint32 minarenaslot) const;
        void SetHonorPoints(uint32 value);
        void SetArenaPoints(uint32 value);

        // duel health and mana reset methods
        void SaveHealthBeforeDuel() { healthBeforeDuel = GetHealth(); }
        void SaveManaBeforeDuel() { manaBeforeDuel = GetPower(POWER_MANA); }
        void RestoreHealthAfterDuel() { SetHealth(healthBeforeDuel); }
        void RestoreManaAfterDuel() { SetPower(POWER_MANA, manaBeforeDuel); }

        //End of PvP System

        void SetDrunkValue(uint8 newDrunkValue, uint32 itemId = 0);
        uint8 GetDrunkValue() const { return GetByteValue(PLAYER_BYTES_3, PLAYER_BYTES_3_OFFSET_INEBRIATION); }
        int32 GetFakeDrunkValue() const { return GetInt32Value(PLAYER_FAKE_INEBRIATION); }
        void UpdateInvisibilityDrunkDetect();
        static DrunkenState GetDrunkenstateByValue(uint8 value);

        uint32 GetDeathTimer() const { return m_deathTimer; }
        uint32 GetCorpseReclaimDelay(bool pvp) const;
        void UpdateCorpseReclaimDelay();
        int32 CalculateCorpseReclaimDelay(bool load = false) const;
        void SendCorpseReclaimDelay(uint32 delay) const;

        uint32 GetShieldBlockValue() const override;                 // overwrite Unit version (virtual)
        bool CanParry() const { return m_canParry; }
        void SetCanParry(bool value);
        bool CanBlock() const { return m_canBlock; }
        void SetCanBlock(bool value);
        bool CanTitanGrip() const { return m_canTitanGrip; }
        void SetCanTitanGrip(bool value, uint32 penaltySpellId = 0);
        void CheckTitanGripPenalty();
        bool CanTameExoticPets() const { return IsGameMaster() || HasAuraType(SPELL_AURA_ALLOW_TAME_PET_TYPE); }

        void SetRegularAttackTime();

        void HandleBaseModFlatValue(BaseModGroup modGroup, float amount, bool apply);
        void ApplyBaseModPctValue(BaseModGroup modGroup, float pct);

        void SetBaseModFlatValue(BaseModGroup modGroup, float val);
        void SetBaseModPctValue(BaseModGroup modGroup, float val);

        void UpdateDamageDoneMods(WeaponAttackType attackType, int32 skipEnchantSlot = -1) override;
        void UpdateBaseModGroup(BaseModGroup modGroup);

        float GetBaseModValue(BaseModGroup modGroup, BaseModType modType) const;
        float GetTotalBaseModValue(BaseModGroup modGroup) const;

        void _ApplyAllStatBonuses();
        void _RemoveAllStatBonuses();

        void ResetAllPowers();

        SpellSchoolMask GetMeleeDamageSchoolMask(WeaponAttackType attackType = BASE_ATTACK, uint8 damageIndex = 0) const override;

        void CastAllObtainSpells();
        void ApplyItemObtainSpells(Item* item, bool apply);

        void UpdateWeaponDependentCritAuras(WeaponAttackType attackType);
        void UpdateAllWeaponDependentCritAuras();

        void UpdateWeaponDependentAuras(WeaponAttackType attackType);
        void ApplyItemDependentAuras(Item* item, bool apply);

        bool CheckAttackFitToAuraRequirement(WeaponAttackType attackType, AuraEffect const* aurEff) const override;

        void _ApplyItemMods(Item* item, uint8 slot, bool apply, bool updateItemAuras = true);
        void _RemoveAllItemMods();
        void _ApplyAllItemMods();
        void _ApplyAllLevelScaleItemMods(bool apply);
        ScalingStatDistributionEntry const* GetScalingStatDistributionFor(ItemTemplate const& itemTemplate) const;
        ScalingStatValuesEntry const* GetScalingStatValuesFor(ItemTemplate const& itemTemplate) const;
        void _ApplyItemBonuses(ItemTemplate const* proto, uint8 slot, bool apply, bool only_level_scale = false);
        void _ApplyWeaponDamage(uint8 slot, ItemTemplate const* proto, bool apply);
        void _ApplyAmmoBonuses();
        bool EnchantmentFitsRequirements(uint32 enchantmentcondition, int8 slot) const;
        void ToggleMetaGemsActive(uint8 exceptslot, bool apply);
        void CorrectMetaGemEnchants(uint8 slot, bool apply);
        void InitDataForForm(bool reapplyMods = false);

        void ApplyItemEquipSpell(Item* item, bool apply, bool form_change = false);
        void ApplyEquipSpell(SpellInfo const* spellInfo, Item* item, bool apply, bool form_change = false);
        void UpdateEquipSpellsAtFormChange();
        void CastItemCombatSpell(DamageInfo const& damageInfo);
        void CastItemCombatSpell(DamageInfo const& damageInfo, Item* item, ItemTemplate const* proto);
        void CastItemUseSpell(Item* item, SpellCastTargets const& targets, uint8 cast_count, uint32 glyphIndex);

        void SendEquipmentSetList();
        void SetEquipmentSet(EquipmentSetInfo::EquipmentSetData const& eqset);
        void DeleteEquipmentSet(uint64 setGuid);

        void SendInitWorldStates(uint32 zoneId, uint32 areaId);
        void SendUpdateWorldState(uint32 variable, uint32 value) const;
        void SendDirectMessage(WorldPacket const* data) const;
        void SendBGWeekendWorldStates() const;
        void SendBattlefieldWorldStates() const;

        void SendAurasForTarget(Unit* target, bool force = false) const;

        // ========== 公共成员变量 ==========
        // 玩家对话菜单类
        PlayerMenu* PlayerTalkClass;
        // 物品套装效果列表
        std::vector<ItemSetEffect*> ItemSetEff;

        // 发送战利品窗口
        void SendLoot(ObjectGuid guid, LootType loot_type);
        // 发送战利品错误
        void SendLootError(ObjectGuid guid, LootError error) const;
        // 发送战利品释放
        void SendLootRelease(ObjectGuid guid) const;
        // 通知战利品物品被移除
        void SendNotifyLootItemRemoved(uint8 lootSlot) const;
        // 通知战利品金钱被移除
        void SendNotifyLootMoneyRemoved() const;

        /*********************************************************/
        /***               战场系统                            ***/
        /*********************************************************/

        // 是否在战场中
        bool InBattleground()       const                { return m_bgData.bgInstanceID != 0; }
        // 是否在竞技场中
        bool InArena()              const;
        // 获取战场ID
        uint32 GetBattlegroundId()  const                { return m_bgData.bgInstanceID; }
        // 获取战场类型ID
        BattlegroundTypeId GetBattlegroundTypeId() const { return m_bgData.bgTypeID; }
        // 获取战场对象
        Battleground* GetBattleground() const;

        // 是否在战场排队中
        bool InBattlegroundQueue(bool ignoreArena = false) const;
        // 是否是逃兵
        bool IsDeserter() const { return HasAura(26013); }

        // 获取战场排队类型ID
        BattlegroundQueueTypeId GetBattlegroundQueueTypeId(uint32 index) const;
        // 获取战场排队索引
        uint32 GetBattlegroundQueueIndex(BattlegroundQueueTypeId bgQueueTypeId) const;
        // 是否被邀请加入指定类型的战场排队
        bool IsInvitedForBattlegroundQueueType(BattlegroundQueueTypeId bgQueueTypeId) const;
        // 是否在指定类型的战场排队中
        bool InBattlegroundQueueForBattlegroundQueueType(BattlegroundQueueTypeId bgQueueTypeId) const;

        // 设置战场ID
        void SetBattlegroundId(uint32 val, BattlegroundTypeId bgTypeId);
        // 添加战场排队ID
        uint32 AddBattlegroundQueueId(BattlegroundQueueTypeId val);
        // 是否有空闲的战场排队ID
        bool HasFreeBattlegroundQueueId() const;
        // 移除战场排队ID
        void RemoveBattlegroundQueueId(BattlegroundQueueTypeId val);
        // 设置战场排队邀请
        void SetInviteForBattlegroundQueueType(BattlegroundQueueTypeId bgQueueTypeId, uint32 instanceId);
        // 是否被邀请加入指定战场实例
        bool IsInvitedForBattlegroundInstance(uint32 instanceId) const;
        // 获取战场入口点位置
        WorldLocation const& GetBattlegroundEntryPoint() const { return m_bgData.joinPos; }
        // 设置战场入口点
        void SetBattlegroundEntryPoint();

        // 设置战场队伍
        void SetBGTeam(uint32 team);
        // 获取战场队伍
        uint32 GetBGTeam() const;

        // 离开战场
        void LeaveBattleground(bool teleportToEntryPoint = true, bool withoutDeserterDebuff = false);
        // 是否可以加入指定战场
        bool CanJoinToBattleground(Battleground const* bg) const;
        // 是否可以因限制举报AFK
        bool CanReportAfkDueToLimit();
        // 被玩家举报AFK
        void ReportedAfkBy(Player* reporter);
        // 清除AFK举报
        void ClearAfkReports() { m_bgData.bgAfkReporter.clear(); }

        // 根据等级获取战场访问权限
        bool GetBGAccessByLevel(BattlegroundTypeId bgTypeId) const;
        // 是否可以使用战场物体
        bool CanUseBattlegroundObject(GameObject* gameobject) const;
        // 是否完全免疫
        bool isTotalImmune() const;
        // 是否可以占领塔点
        bool CanCaptureTowerPoint() const;

        // 获取是否为随机战场胜利者
        bool GetRandomWinner() const { return m_IsBGRandomWinner; }
        // 设置随机战场胜利者
        void SetRandomWinner(bool isWinner);

        /*********************************************************/
        /***               户外PvP系统                        ***/
        /*********************************************************/

        // 获取户外PvP对象
        OutdoorPvP* GetOutdoorPvP() const;
        // 是否处于户外PvP目标占领的活跃状态
        bool IsOutdoorPvPActive() const;

        /*********************************************************/
        /***              环境系统                            ***/
        /*********************************************************/

        // 是否免疫环境伤害
        bool IsImmuneToEnvironmentalDamage() const;
        // 造成环境伤害
        uint32 EnvironmentalDamage(EnviromentalDamage type, uint32 damage);

        /*********************************************************/
        /***               洪水过滤系统                        ***/
        /*********************************************************/

        // 聊天洪水节流结构
        struct ChatFloodThrottle
        {
            enum Index
            {
                REGULAR = 0,  // 常规聊天
                ADDON = 1,    // 插件消息
                MAX
            };

            time_t Time = 0;   // 时间戳
            uint32 Count = 0;   // 计数
        };

        // 更新说话时间
        void UpdateSpeakTime(ChatFloodThrottle::Index index);

        /*********************************************************/
        /***                 各种系统                         ***/
        /*********************************************************/
        // 如果需要则更新掉落信息
        void UpdateFallInformationIfNeed(MovementInfo const& minfo, uint16 opcode);
        // 观察者对象（用于附身、载具等直接客户端控制）
        WorldObject* m_seer;
        // 设置掉落信息
        void SetFallInformation(uint32 time, float z);
        // 处理掉落
        void HandleFall(MovementInfo const& movementInfo);

        // 是否可以在区域飞行
        bool CanFlyInZone(uint32 mapid, uint32 zone, SpellInfo const* bySpell) const;

        // 设置客户端控制
        void SetClientControl(Unit* target, bool allowMove);

        // 设置观察者
        void SetSeer(WorldObject* target) { m_seer = target; }
        // 设置视点
        void SetViewpoint(WorldObject* target, bool apply);
        // 获取视点
        WorldObject* GetViewpoint() const;
        // 停止施放魅惑
        void StopCastingCharm();
        // 停止施放绑定视野
        void StopCastingBindSight() const;

        // 获取保存计时器
        uint32 GetSaveTimer() const { return m_nextSave; }
        // 设置保存计时器
        void SetSaveTimer(uint32 timer) { m_nextSave = timer; }

        // 保存召回位置
        void SaveRecallPosition() { m_recall_location.WorldRelocate(*this); }
        // 召回（传送回保存的位置）
        void Recall() { TeleportTo(m_recall_location); }

        // 设置家园绑定点
        void SetHomebind(WorldLocation const& loc, uint32 areaId);
        // 发送绑定点更新
        void SendBindPointUpdate();

        // ========== 家园绑定点坐标 ==========
        // 家园绑定地图ID
        uint32 m_homebindMapId;
        // 家园绑定区域ID
        uint16 m_homebindAreaId;
        // 家园绑定X坐标
        float m_homebindX;
        // 家园绑定Y坐标
        float m_homebindY;
        // 家园绑定Z坐标
        float m_homebindZ;

        // 获取起始位置
        WorldLocation GetStartPosition() const;

        // 当前玩家客户端可见的对象集合
        GuidUnorderedSet m_clientGUIDs;

        // 是否有对象在客户端
        bool HaveAtClient(Object const* u) const;

        // 是否永不可见
        bool IsNeverVisible(bool allowServersideObjects) const override;

        // 对指定玩家是否全局可见
        bool IsVisibleGloballyFor(Player const* player) const;

        // 发送初始可见包
        void SendInitialVisiblePackets(Unit* target) const;
        // 更新对象可见性
        void UpdateObjectVisibility(bool forced = true) override;
        // 为玩家更新可见性
        void UpdateVisibilityForPlayer();
        // 更新指定目标的可见性
        void UpdateVisibilityOf(WorldObject* target);
        // 更新触发器可见性
        void UpdateTriggerVisibility();
        // 设置相位掩码（覆盖Unit::SetPhaseMask）
        void SetPhaseMask(uint32 newPhaseMask, bool update) override;

        template<class T>
        void UpdateVisibilityOf(T* target, UpdateData& data, std::set<Unit*>& visibleNow);

        // 是否有登录标志
        bool HasAtLoginFlag(AtLoginFlags f) const { return (m_atLoginFlags & f) != 0; }
        // 设置登录标志
        void SetAtLoginFlag(AtLoginFlags f) { m_atLoginFlags |= f; }
        // 移除登录标志
        void RemoveAtLoginFlag(AtLoginFlags flags, bool persist = false);

        // 是否正在使用查找组功能
        bool isUsingLfg() const;
        // 是否在随机副本中
        bool inRandomLfgDungeon() const;

        typedef std::set<uint32> DFQuestsDoneList;
        // 已完成的地下城查找任务列表
        DFQuestsDoneList m_DFQuests;

        // ========== 临时移除宠物缓存相关 ==========
        // 获取临时解散的宠物编号
        uint32 GetTemporaryUnsummonedPetNumber() const { return m_temporaryUnsummonedPetNumber; }
        // 设置临时解散的宠物编号
        void SetTemporaryUnsummonedPetNumber(uint32 petnumber) { m_temporaryUnsummonedPetNumber = petnumber; }
        // 如果有临时解散的宠物则解散
        void UnsummonPetTemporaryIfAny();
        // 重新召唤临时解散的宠物
        void ResummonPetTemporaryUnSummonedIfAny();
        // 宠物是否需要临时解散
        bool IsPetNeedBeTemporaryUnsummoned() const;

        // 发送过场动画开始
        void SendCinematicStart(uint32 CinematicSequenceId) const;
        // 发送电影开始
        void SendMovieStart(uint32 movieId);

        // 执行随机掷骰
        uint32 DoRandomRoll(uint32 minimum, uint32 maximum);

        /*********************************************************/
        /***                 副本系统                         ***/
        /*********************************************************/

        typedef std::unordered_map< uint32 /*mapId*/, InstancePlayerBind > BoundInstancesMap;

        // 更新家园绑定时间
        void UpdateHomebindTime(uint32 time);

        // 家园绑定计时器
        uint32 m_HomebindTimer;
        // 副本是否有效
        bool m_InstanceValid;
        // 按难度存储的永久绑定和单人绑定
        BoundInstancesMap m_boundInstances[MAX_DIFFICULTY];
        // 获取绑定的副本
        InstancePlayerBind* GetBoundInstance(uint32 mapid, Difficulty difficulty, bool withExpired = false);
        // 获取指定难度的绑定副本映射
        BoundInstancesMap& GetBoundInstances(Difficulty difficulty) { return m_boundInstances[difficulty]; }
        // 获取副本保存
        InstanceSave* GetInstanceSave(uint32 mapid, bool raid);
        // 解除副本绑定
        void UnbindInstance(uint32 mapid, Difficulty difficulty, bool unload = false);
        // 解除副本绑定（通过迭代器）
        void UnbindInstance(BoundInstancesMap::iterator &itr, Difficulty difficulty, bool unload = false);
        // 绑定到副本
        InstancePlayerBind* BindToInstance(InstanceSave* save, bool permanent, BindExtensionState extendState = EXTEND_STATE_NORMAL, bool load = false);
        // 绑定到副本（无参数版本）
        void BindToInstance();
        // 设置待绑定
        void SetPendingBind(uint32 instanceId, uint32 bindTimer);
        // 是否有待绑定
        bool HasPendingBind() const { return _pendingBindId > 0; }
        // 发送团队信息
        void SendRaidInfo();
        // 发送已保存副本信息
        void SendSavedInstances();
        // 是否满足访问需求
        bool Satisfy(AccessRequirement const* ar, uint32 target_map, bool report = false);
        // 检查副本有效性
        bool CheckInstanceValidity(bool /*isLogin*/);
        // 检查副本计数
        bool CheckInstanceCount(uint32 instanceId) const;
        // 添加副本进入时间
        void AddInstanceEnterTime(uint32 instanceId, time_t enterTime);

        // ========== 宠物相关 ==========
        // 获取最后使用的宠物编号（用于战场）
        uint32 GetLastPetNumber() const { return m_lastpetnumber; }
        // 设置最后使用的宠物编号
        void SetLastPetNumber(uint32 petnumber) { m_lastpetnumber = petnumber; }

        /*********************************************************/
        /***                   队伍系统                       ***/
        /*********************************************************/

        // 获取邀请加入的队伍
        Group* GetGroupInvite() const { return m_groupInvite; }
        // 设置邀请加入的队伍
        void SetGroupInvite(Group* group) { m_groupInvite = group; }
        // 获取当前队伍
        Group* GetGroup() { return m_group.getTarget(); }
        // 获取当前队伍（常量版本）
        Group const* GetGroup() const { return const_cast<Group const*>(m_group.getTarget()); }
        // 获取队伍引用
        GroupReference& GetGroupRef() { return m_group; }
        // 设置队伍
        void SetGroup(Group* group, int8 subgroup = -1);
        // 获取子队伍
        uint8 GetSubGroup() const { return m_group.getSubGroup(); }
        // 获取队伍更新标志
        uint32 GetGroupUpdateFlag() const { return m_groupUpdateMask; }
        // 设置队伍更新标志
        void SetGroupUpdateFlag(uint32 flag) { m_groupUpdateMask |= flag; }
        // 获取团队光环更新掩码
        uint64 GetAuraUpdateMaskForRaid() const { return m_auraRaidUpdateMask; }
        // 设置团队光环更新掩码
        void SetAuraUpdateMaskForRaid(uint8 slot) { m_auraRaidUpdateMask |= (uint64(1) << slot); }
        // 获取下一个随机团队成员
        Player* GetNextRandomRaidMember(float radius);
        // 是否可以从队伍中移除成员
        PartyResult CanUninviteFromGroup(ObjectGuid guidMember = ObjectGuid::Empty) const;

        // ========== 战场/战场团队系统 ==========
        // 设置战场或战场队伍
        void SetBattlegroundOrBattlefieldRaid(Group* group, int8 subgroup = -1);
        // 从战场或战场队伍中移除
        void RemoveFromBattlegroundOrBattlefieldRaid();
        // 获取原始队伍
        Group* GetOriginalGroup() const { return m_originalGroup.getTarget(); }
        // 获取原始队伍引用
        GroupReference& GetOriginalGroupRef() { return m_originalGroup; }
        // 获取原始子队伍
        uint8 GetOriginalSubGroup() const { return m_originalGroup.getSubGroup(); }
        // 设置原始队伍
        void SetOriginalGroup(Group* group, int8 subgroup = -1);

        // 设置是否跳过队伍分配
        void SetPassOnGroupLoot(bool bPassOnGroupLoot) { m_bPassOnGroupLoot = bPassOnGroupLoot; }
        // 获取是否跳过队伍分配
        bool GetPassOnGroupLoot() const { return m_bPassOnGroupLoot; }

        // 获取地图引用
        MapReference &GetMapRef() { return m_mapRef; }

        // 设置地图并添加引用
        void SetMap(Map* map) override;
        // 重置地图
        void ResetMap() override;

        // 是否允许掠夺生物
        bool isAllowedToLoot(Creature const* creature) const;

        // 获取名字变格（用于某些语言）
        DeclinedName const* GetDeclinedNames() const { return m_declinedname; }
        // ========== 符文系统（死亡骑士） ==========
        // 获取符文状态
        uint8 GetRunesState() const { return m_runes->runeState; }
        // 获取基础符文类型
        RuneType GetBaseRune(uint8 index) const { return RuneType(m_runes->runes[index].BaseRune); }
        // 获取当前符文类型
        RuneType GetCurrentRune(uint8 index) const { return RuneType(m_runes->runes[index].CurrentRune); }
        // 获取符文冷却时间
        uint32 GetRuneCooldown(uint8 index) const { return m_runes->runes[index].Cooldown; }
        // 获取符文基础冷却时间
        uint32 GetRuneBaseCooldown(uint8 index);
        // 基础符文槽是否在冷却中
        bool IsBaseRuneSlotsOnCooldown(RuneType runeType) const;
        // 获取最后使用的符文类型
        RuneType GetLastUsedRune() const { return m_runes->lastUsedRune; }
        // 设置最后使用的符文类型
        void SetLastUsedRune(RuneType type) { m_runes->lastUsedRune = type; }
        // 设置基础符文
        void SetBaseRune(uint8 index, RuneType baseRune) { m_runes->runes[index].BaseRune = baseRune; }
        // 设置当前符文
        void SetCurrentRune(uint8 index, RuneType currentRune) { m_runes->runes[index].CurrentRune = currentRune; }
        // 设置符文冷却时间
        void SetRuneCooldown(uint8 index, uint32 cooldown, bool casted = false);
        // 设置符文转换光环
        void SetRuneConvertAura(uint8 index, AuraEffect const* aura);
        // 移除符文转换光环
        void RemoveRuneConvertAura(uint8 index, AuraEffect const* aura);
        // 通过光环效果添加符文
        void AddRuneByAuraEffect(uint8 index, RuneType newType, AuraEffect const* aura);
        // 通过光环效果移除符文
        void RemoveRunesByAuraEffect(AuraEffect const* aura);
        // 恢复基础符文
        void RestoreBaseRune(uint8 index);
        // 转换符文
        void ConvertRune(uint8 index, RuneType newType);
        // 重新同步符文
        void ResyncRunes() const;
        // 添加符文能量
        void AddRunePower(uint8 index) const;
        // 初始化符文
        void InitRunes();

        // ========== 成就系统 ==========
        // 发送检查成就响应
        void SendRespondInspectAchievements(Player* player) const;
        // 是否已完成指定成就
        bool HasAchieved(uint32 achievementId) const;
        // 重置成就
        void ResetAchievements();
        // 重置成就条件
        void ResetAchievementCriteria(AchievementCriteriaCondition condition, uint32 value, bool evenIfCriteriaComplete = false);
        // 更新成就条件
        void UpdateAchievementCriteria(AchievementCriteriaTypes type, uint32 miscValue1 = 0, uint32 miscValue2 = 0, WorldObject* ref = nullptr);
        // 开始定时成就
        void StartTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry, uint32 timeLost = 0);
        // 移除定时成就
        void RemoveTimedAchievement(AchievementCriteriaTimedTypes type, uint32 entry);
        // 完成成就
        void CompletedAchievement(AchievementEntry const* entry);

        // ========== 称号系统 ==========
        // 是否拥有指定称号
        bool HasTitle(uint32 bitIndex) const;
        // 是否拥有指定称号
        bool HasTitle(CharTitlesEntry const* title) const;
        // 设置称号
        void SetTitle(CharTitlesEntry const* title, bool lost = false);

        // 是否能看到生物上的法术点击
        bool CanSeeSpellClickOn(Creature const* creature) const;

        // 获取冠军阵营ID
        uint32 GetChampioningFaction() const { return m_ChampioningFaction; }
        // 设置冠军阵营ID
        void SetChampioningFaction(uint32 faction) { m_ChampioningFaction = faction; }
        // 正在修改的法术
        Spell* m_spellModTakingSpell;

        // 获取平均物品等级
        float GetAverageItemLevel() const;
        // 是否调试区域触发器
        bool isDebugAreaTriggers;

        void ClearWhisperWhiteList() { WhisperList.clear(); }
        void AddWhisperWhiteList(ObjectGuid guid) { WhisperList.push_back(guid); }
        bool IsInWhisperWhiteList(ObjectGuid guid);
        void RemoveFromWhisperWhiteList(ObjectGuid guid) { WhisperList.remove(guid); }

        bool SetDisableGravity(bool disable, bool packetOnly /* = false */, bool updateAnimTier = true) override;
        bool SetCanFly(bool apply, bool packetOnly = false) override;
        bool SetWaterWalking(bool apply, bool packetOnly = false) override;
        bool SetFeatherFall(bool apply, bool packetOnly = false) override;
        bool SetHover(bool enable, bool packetOnly = false, bool updateAnimTier = true) override;

        bool CanFly() const override { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_CAN_FLY); }
        bool CanEnterWater() const override { return true; }

        std::string GetMapAreaAndZoneString() const;
        std::string GetCoordsMapAreaAndZoneString() const;

        std::string GetDebugInfo() const override;

    protected:
        // ========================================================================
        // GM（游戏管理员）私聊白名单
        // ========================================================================
        GuidList WhisperList;
        // 再生计时器计数
        uint32 m_regenTimerCount;
        // 进食表情计时器计数
        uint32 m_foodEmoteTimerCount;
        // 能量分数值数组
        float m_powerFraction[MAX_POWERS];
        // 争夺中PvP计时器
        uint32 m_contestedPvPTimer;

        /*********************************************************/
        /***               战场系统                            ***/
        /*********************************************************/

        /*
        战场排队队列数组（玩家所在的战场类型ID队列）
        */
        struct BgBattlegroundQueueID_Rec
        {
            BattlegroundQueueTypeId bgQueueTypeId;  // 战场排队类型ID
            uint32 invitedToInstance;                // 被邀请进入的实例ID
        };

        // 战场排队队列ID数组
        BgBattlegroundQueueID_Rec m_bgBattlegroundQueueID[PLAYER_MAX_BATTLEGROUND_QUEUES];
        // 战场数据
        BGData                    m_bgData;

        // 是否为随机战场胜利者
        bool m_IsBGRandomWinner;

        /*********************************************************/
        /***                   任务系统                        ***/
        /*********************************************************/

        // 同一时间只允许一个定时任务激活
        typedef std::set<uint32> QuestSet;
        typedef std::set<uint32> SeasonalQuestSet;
        typedef std::unordered_map<uint32, SeasonalQuestSet> SeasonalEventQuestMap;
        // 定时任务列表
        QuestSet m_timedquests;
        // 周常任务列表
        QuestSet m_weeklyquests;
        // 月常任务列表
        QuestSet m_monthlyquests;
        // 季节性任务映射
        SeasonalEventQuestMap m_seasonalquests;

        // 正在分享任务的玩家GUID
        ObjectGuid m_playerSharingQuest;
        // 分享的任务ID
        uint32 m_sharedQuestId;
        // 游戏内时间
        uint32 m_ingametime;

        /*********************************************************/
        /***                   加载系统                        ***/
        /*********************************************************/

        // 从数据库加载动作按钮
        void _LoadActions(PreparedQueryResult result);
        // 从数据库加载光环
        void _LoadAuras(PreparedQueryResult result, uint32 timediff);
        // 加载雕文光环
        void _LoadGlyphAuras();
        // 加载绑定的副本
        void _LoadBoundInstances(PreparedQueryResult result);
        // 从数据库加载背包物品
        void _LoadInventory(PreparedQueryResult result, uint32 timeDiff);
        // 从数据库加载邮件
        void _LoadMail(PreparedQueryResult mailsResult, PreparedQueryResult mailItemsResult);
        // 加载邮件中的物品（静态方法）
        static Item* _LoadMailedItem(ObjectGuid const& playerGuid, Player* player, uint32 mailId, Mail* mail, Field* fields);
        // 加载任务状态
        void _LoadQuestStatus(PreparedQueryResult result);
        // 加载已奖励的任务状态
        void _LoadQuestStatusRewarded(PreparedQueryResult result);
        // 加载日常任务状态
        void _LoadDailyQuestStatus(PreparedQueryResult result);
        // 加载周常任务状态
        void _LoadWeeklyQuestStatus(PreparedQueryResult result);
        // 加载月常任务状态
        void _LoadMonthlyQuestStatus(PreparedQueryResult result);
        // 加载季节性任务状态
        void _LoadSeasonalQuestStatus(PreparedQueryResult result);
        // 加载随机战场状态
        void _LoadRandomBGStatus(PreparedQueryResult result);
        // 加载队伍信息
        void _LoadGroup(PreparedQueryResult result);
        // 加载技能
        void _LoadSkills(PreparedQueryResult result);
        // 加载法术
        void _LoadSpells(PreparedQueryResult result);
        // 加载家园绑定点
        bool _LoadHomeBind(PreparedQueryResult result);
        // 加载名字变格（用于某些语言的语法）
        void _LoadDeclinedNames(PreparedQueryResult result);
        // 加载竞技场队伍信息
        void _LoadArenaTeamInfo(PreparedQueryResult result);
        // 加载装备套装
        void _LoadEquipmentSets(PreparedQueryResult result);
        // 加载战场数据
        void _LoadBGData(PreparedQueryResult result);
        // 加载雕文
        void _LoadGlyphs(PreparedQueryResult result);
        // 加载天赋
        void _LoadTalents(PreparedQueryResult result);
        // 加载副本时间限制
        void _LoadInstanceTimeRestrictions(PreparedQueryResult result);
        // 加载宠物兽栏
        void _LoadPetStable(uint8 petStableSlots, PreparedQueryResult result);

        /*********************************************************/
        /***                   保存系统                        ***/
        /*********************************************************/

        // 保存动作按钮到数据库
        void _SaveActions(CharacterDatabaseTransaction trans);
        // 保存光环到数据库
        void _SaveAuras(CharacterDatabaseTransaction trans);
        // 保存背包物品到数据库
        void _SaveInventory(CharacterDatabaseTransaction trans);
        // 保存邮件到数据库
        void _SaveMail(CharacterDatabaseTransaction trans);
        // 保存任务状态到数据库
        void _SaveQuestStatus(CharacterDatabaseTransaction trans);
        // 保存日常任务状态到数据库
        void _SaveDailyQuestStatus(CharacterDatabaseTransaction trans);
        // 保存周常任务状态到数据库
        void _SaveWeeklyQuestStatus(CharacterDatabaseTransaction trans);
        // 保存月常任务状态到数据库
        void _SaveMonthlyQuestStatus(CharacterDatabaseTransaction trans);
        // 保存季节性任务状态到数据库
        void _SaveSeasonalQuestStatus(CharacterDatabaseTransaction trans);
        // 保存技能到数据库
        void _SaveSkills(CharacterDatabaseTransaction trans);
        // 保存法术到数据库
        void _SaveSpells(CharacterDatabaseTransaction trans);
        // 保存装备套装到数据库
        void _SaveEquipmentSets(CharacterDatabaseTransaction trans);
        // 保存战场数据到数据库
        void _SaveBGData(CharacterDatabaseTransaction trans);
        // 保存雕文到数据库
        void _SaveGlyphs(CharacterDatabaseTransaction trans) const;
        // 保存天赋到数据库
        void _SaveTalents(CharacterDatabaseTransaction trans);
        // 保存属性到数据库
        void _SaveStats(CharacterDatabaseTransaction trans) const;
        // 保存副本时间限制到数据库
        void _SaveInstanceTimeRestrictions(CharacterDatabaseTransaction trans);

        /*********************************************************/
        /***              环境系统                             ***/
        /*********************************************************/
        // 处理醒酒
        void HandleSobering();
        // 发送镜像计时器
        void SendMirrorTimer(MirrorTimerType Type, uint32 MaxValue, uint32 CurrentValue, int32 Regen);
        // 停止镜像计时器
        void StopMirrorTimer(MirrorTimerType Type);
        // 处理溺水
        void HandleDrowning(uint32 time_diff);
        // 获取最大计时器值
        int32 getMaxTimer(MirrorTimerType timer) const;

        /*********************************************************/
        /***                  荣誉系统                         ***/
        /*********************************************************/
        // 上次荣誉更新时间
        time_t m_lastHonorUpdateTime;

        // 输出调试值
        void outDebugValues() const;
        // 掠夺目标GUID
        ObjectGuid m_lootGuid;

        // ========== 阵营相关 ==========
        // 玩家阵营（联盟/部落）
        uint32 m_team;
        // 下次自动保存计时器
        uint32 m_nextSave;
        // 聊天洪水控制数据
        std::array<ChatFloodThrottle, ChatFloodThrottle::MAX> m_chatFloodData;
        // 地下城难度
        Difficulty m_dungeonDifficulty;
        // 团队副本难度
        Difficulty m_raidDifficulty;
        // 团队副本地图难度
        Difficulty m_raidMapDifficulty;

        // ========== 登录标志 ==========
        // 登录时需要执行的标志
        uint32 m_atLoginFlags;

        // ========== 物品相关 ==========
        // 背包物品数组（包含装备、背包、银行等所有槽位）
        Item* m_items[PLAYER_SLOTS_COUNT];
        // 当前回购槽位
        uint32 m_currentBuybackSlot;

        // 物品更新队列
        std::vector<Item*> m_itemUpdateQueue;
        // 物品更新队列是否被阻塞
        bool m_itemUpdateQueueBlocked;

        // ========== 额外标志 ==========
        // 额外标志位（GM标志等）
        uint32 m_ExtraFlags;

        // ========== 任务相关 ==========
        // 任务状态映射
        QuestStatusMap m_QuestStatus;
        // 任务状态保存映射
        QuestStatusSaveMap m_QuestStatusSave;

        // 已奖励的任务集合
        RewardedQuestSet m_RewardedQuests;
        // 已奖励任务保存映射
        QuestStatusSaveMap m_RewardedQuestsSave;

        // ========== 技能相关 ==========
        // 技能状态映射
        SkillStatusMap mSkillStatus;

        // ========== 公会/竞技场相关 ==========
        // 被邀请加入的公会ID
        uint32 m_GuildIdInvited;
        // 被邀请加入的竞技场队伍ID
        uint32 m_ArenaTeamIdInvited;

        // ========== 邮件/法术/天赋相关 ==========
        // 玩家邮件列表
        PlayerMails m_mail;
        // 玩家法术映射
        PlayerSpellMap m_spells;
        // 玩家天赋映射数组（每个专精一套天赋）
        PlayerTalentMap* m_talents[MAX_TALENT_SPECS];
        // 最后使用的药水ID（战斗中使用后会阻止下次使用）
        uint32 m_lastPotionId;

        // ========== 专精相关 ==========
        // 当前激活的专精
        uint8 m_activeSpec;
        // 专精数量
        uint8 m_specsCount;

        // ========== 雕文相关 ==========
        // 雕文数组（每个专精一组雕文）
        uint32 m_Glyphs[MAX_TALENT_SPECS][MAX_GLYPH_SLOT_INDEX];

        // ========== 动作按钮相关 ==========
        // 动作按钮列表
        ActionButtonList m_actionButtons;

        // ========== 光环/属性相关 ==========
        // 光环基础固定修正值
        float m_auraBaseFlatMod[BASEMOD_END];
        // 光环基础百分比修正值
        float m_auraBasePctMod[BASEMOD_END];
        // 基础战斗等级值
        int16 m_baseRatingValue[MAX_COMBAT_RATING];
        // 基础法术强度
        uint32 m_baseSpellPower;
        // 基础野性攻击强度
        uint32 m_baseFeralAP;
        // 基础法力回复
        uint32 m_baseManaRegen;
        // 基础生命回复
        uint32 m_baseHealthRegen;
        // 法术穿透物品修正
        int32 m_spellPenetrationItemMod;

        // ========== 法术修正相关 ==========
        // 法术修正容器
        SpellModContainer m_spellMods[MAX_SPELLMOD];

        // ========== 附魔/物品持续时间相关 ==========
        // 附魔持续时间列表
        EnchantDurationList m_enchantDuration;
        // 物品持续时间列表
        ItemDurationList m_itemDuration;
        // 可交易灵魂绑定物品集合
        GuidUnorderedSet m_itemSoulboundTradeable;

        // ========== 复活数据 ==========
        // 复活数据（智能指针）
        std::unique_ptr<ResurrectionData> _resurrectionData;

        // ========== 会话相关 ==========
        // 玩家会话（与客户端的连接）
        WorldSession* m_session;

        // ========== 频道相关 ==========
        // 已加入的频道列表
        JoinedChannelsList m_channels;

        // ========== 过场动画/电影相关 ==========
        // 当前过场动画
        uint8 m_cinematic;

        // 当前电影
        uint32 m_movie;

        // ========== 交易相关 ==========
        // 交易数据
        TradeData* m_trade;

        // ========== 任务变更标志 ==========
        // 日常任务是否变更
        bool   m_DailyQuestChanged;
        // 周常任务是否变更
        bool   m_WeeklyQuestChanged;
        // 月常任务是否变更
        bool   m_MonthlyQuestChanged;
        // 季节性任务是否变更
        bool   m_SeasonalQuestChanged;
        // 上次日常任务时间
        time_t m_lastDailyQuestTime;

        // ========== 计时器相关 ==========
        // 敌对引用检查计时器
        uint32 m_hostileReferenceCheckTimer;
        // 醉酒计时器
        uint32 m_drunkTimer;
        // 武器更换计时器
        uint32 m_weaponChangeTimer;

        // ========== 区域更新相关 ==========
        // 区域更新ID
        uint32 m_zoneUpdateId;
        // 区域更新计时器
        uint32 m_zoneUpdateTimer;
        // 区域更新ID
        uint32 m_areaUpdateId;

        // ========== 死亡相关 ==========
        // 死亡计时器
        uint32 m_deathTimer;
        // 死亡过期时间
        time_t m_deathExpireTime;

        // ========== 武器/护甲熟练度相关 ==========
        // 武器熟练度
        uint32 m_WeaponProficiency;
        // 护甲熟练度
        uint32 m_ArmorProficiency;
        // 是否可以招架
        bool m_canParry;
        // 是否可以格挡
        bool m_canBlock;
        // 是否可以使用泰坦之握
        bool m_canTitanGrip;
        // 泰坦之握惩罚法术ID
        uint32 m_titanGripPenaltySpellId;
        // 挥砍错误消息
        uint8 m_swingErrorMsg;
        // 弹药DPS
        float m_ammoDPS;

        ////////////////////休息系统/////////////////////
        // 休息时间
        time_t _restTime;
        // 旅馆触发器ID（用于休息）
        uint32 inn_triggerId;
        // 休息加成
        float m_rest_bonus;
        // 休息标志掩码
        uint32 _restFlagMask;
        ////////////////////休息系统/////////////////////

        // ========== 天赋重置相关 ==========
        // 重置天赋消耗
        uint32 m_resetTalentsCost;
        // 重置天赋时间
        time_t m_resetTalentsTime;
        // 已使用天赋点数
        uint32 m_usedTalentCount;
        // 任务奖励天赋点数
        uint32 m_questRewardTalentCount;

        // ========== 社交系统 ==========
        // 玩家社交关系
        PlayerSocial* m_social;

        // ========== 组队相关 ==========
        // 当前队伍引用
        GroupReference m_group;
        // 原始队伍引用（战场/竞技场前）
        GroupReference m_originalGroup;
        // 邀请加入的队伍
        Group* m_groupInvite;
        // 队伍更新掩码
        uint32 m_groupUpdateMask;
        // 团队光环更新掩码
        uint64 m_auraRaidUpdateMask;
        // 是否跳过队伍分配
        bool m_bPassOnGroupLoot;

        // ========== 宠物相关 ==========
        // 最后使用的宠物编号（用于战场）
        uint32 m_lastpetnumber;

        // ========== 玩家召唤相关 ==========
        // 召唤过期时间
        time_t m_summon_expire;
        // 召唤位置
        WorldLocation m_summon_location;

        // ========== 召回位置相关 ==========
        // 召回位置（GM命令）
        WorldLocation m_recall_location;

        // ========== 名字变格相关 ==========
        // 名字变格（用于某些语言）
        DeclinedName *m_declinedname;
        // 符文数据（死亡骑士）
        Runes *m_runes;
        // 装备套装容器
        EquipmentSetContainer _equipmentSets;

        // 是否总是能看到指定对象
        bool CanAlwaysSee(WorldObject const* obj) const override;

        // 对指定观察者是否总是可探测
        bool IsAlwaysDetectableFor(WorldObject const* seer) const override;

        // 可授予的等级数（招募好友系统）
        uint8 m_grantableLevels;

        // 钓鱼步骤数
        uint8 m_fishingSteps;

        // 是否需要更新区域
        bool m_needsZoneUpdate;

        // 队伍更新计时器
        TimeTracker m_groupUpdateTimer;

    private:
        // ========================================================================
        // 物品存储相关内部函数
        // ========================================================================
        // 检查是否可以在指定槽位存储物品
        InventoryResult CanStoreItem_InSpecificSlot(uint8 bag, uint8 slot, ItemPosCountVec& dest, ItemTemplate const* pProto, uint32& count, bool swap, Item* pSrcItem) const;
        // 检查是否可以在背包中存储物品
        InventoryResult CanStoreItem_InBag(uint8 bag, ItemPosCountVec& dest, ItemTemplate const* pProto, uint32& count, bool merge, bool non_specialized, Item* pSrcItem, uint8 skip_bag, uint8 skip_slot) const;
        // 检查是否可以在背包槽位中存储物品
        InventoryResult CanStoreItem_InInventorySlots(uint8 slot_begin, uint8 slot_end, ItemPosCountVec& dest, ItemTemplate const* pProto, uint32& count, bool merge, Item* pSrcItem, uint8 skip_bag, uint8 skip_slot) const;
        // 存储物品
        Item* _StoreItem(uint16 pos, Item* pItem, uint32 count, bool clone, bool update);
        // 从数据库加载物品
        Item* _LoadItem(CharacterDatabaseTransaction trans, uint32 zoneId, uint32 timeDiff, Field* fields);

        // 过场动画管理器
        CinematicMgr* _cinematicMgr;

        // 可退款物品集合
        GuidSet m_refundableItems;
        // 发送退款信息
        void SendRefundInfo(Item* item);
        // 退款物品
        void RefundItem(Item* item);

        // 添加已知货币（已知的货币不会被移除，显示为0）
        void AddKnownCurrency(uint32 itemId);

        // 调整任务需求物品数量
        void AdjustQuestReqItemCount(Quest const* quest, QuestStatusData& questStatusData);

        // 是否可以延迟传送
        bool IsCanDelayTeleport() const { return m_bCanDelayTeleport; }
        // 设置是否可以延迟传送
        void SetCanDelayTeleport(bool setting) { m_bCanDelayTeleport = setting; }
        // 是否有延迟传送
        bool IsHasDelayedTeleport() const { return m_bHasDelayedTeleport; }
        // 设置延迟传送标志
        void SetDelayedTeleportFlag(bool setting) { m_bHasDelayedTeleport = setting; }
        // 计划延迟操作
        void ScheduleDelayedOperation(uint32 operation) { if (operation < DELAYED_END) m_DelayedOperations |= operation; }

        // 是否为副本登录GM异常
        bool IsInstanceLoginGameMasterException() const;

        // ========== 地图引用相关 ==========
        // 地图引用
        MapReference m_mapRef;

        // ========== 掉落相关 ==========
        // 最后掉落时间
        uint32 m_lastFallTime;
        // 最后掉落高度Z
        float  m_lastFallZ;

        // ========== 镜像计时器相关 ==========
        // 镜像计时器数组（疲劳、呼吸、火焰）
        int32 m_MirrorTimer[MAX_TIMERS];
        // 镜像计时器标志
        uint8 m_MirrorTimerFlags;
        // 上次镜像计时器标志
        uint8 m_MirrorTimerFlagsLast;

        // ========== 符文计时器相关（死亡骑士） ==========
        // 符文冷却计时器
        uint32 m_runeGraceCooldown[MAX_RUNES];
        // 上次符文冷却计时器
        uint32 m_lastRuneGraceTimers[MAX_RUNES];

        // ========== 传送相关 ==========
        // 当前传送目标位置
        WorldLocation m_teleport_dest;
        // 传送选项
        uint32 m_teleport_options;
        // 近距离传送信号量
        bool mSemaphoreTeleport_Near;
        // 远距离传送信号量
        bool mSemaphoreTeleport_Far;

        // ========== 延迟操作相关 ==========
        // 延迟操作位掩码
        uint32 m_DelayedOperations;
        // 是否可以延迟传送
        bool m_bCanDelayTeleport;
        // 是否有延迟传送
        bool m_bHasDelayedTeleport;

        // ========== 宠物兽栏相关 ==========
        // 宠物兽栏（智能指针）
        std::unique_ptr<PetStable> m_petStable;

        // ========== 临时移除宠物缓存相关 ==========
        // 临时解散的宠物编号
        uint32 m_temporaryUnsummonedPetNumber;
        // 旧宠物法术
        uint32 m_oldpetspell;

        // ========== 成就/声望管理器 ==========
        // 成就管理器
        AchievementMgr* m_achievementMgr;
        // 声望管理器
        ReputationMgr*  m_reputationMgr;

        // ========== 冠军阵营相关 ==========
        // 冠军阵营ID
        uint32 m_ChampioningFaction;

        // ========== 副本时间相关 ==========
        // 副本重置时间映射
        InstanceTimeMap _instanceResetTimes;
        // 待绑定副本ID
        uint32 _pendingBindId;
        // 待绑定计时器
        uint32 _pendingBindTimer;

        // ========== 作弊标志相关 ==========
        // 激活的作弊标志
        uint32 _activeCheats;

        // ========== 决斗相关 ==========
        // 决斗前的生命值
        uint32 healthBeforeDuel;
        // 决斗前的法力值
        uint32 manaBeforeDuel;

        // ========== 尸体位置相关 ==========
        // 尸体位置
        WorldLocation _corpseLocation;
};

TC_GAME_API void AddItemsSetItem(Player* player, Item* item);
TC_GAME_API void RemoveItemsSetItem(Player* player, ItemTemplate const* proto);

#endif
