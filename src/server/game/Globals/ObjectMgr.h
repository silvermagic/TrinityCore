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
 * @file ObjectMgr.h
 * @brief 对象管理器模块 - 游戏世界静态数据的核心管理中心
 *
 * 模块职责：
 * 1. 管理所有游戏静态数据的加载和查询
 *    - 生物模板（CreatureTemplate）
 *    - 游戏对象模板（GameObjectTemplate）
 *    - 物品模板（ItemTemplate）
 *    - 任务模板（Quest）
 *    - 区域触发器、传送点、声望等
 *
 * 2. 管理对象生成系统
 *    - 生物和游戏对象的刷怪数据
 *    - 生成组和刷怪规则
 *    - 链接复活机制
 *
 * 3. 提供 GUID 生成服务
 *    - 全局唯一 ID 生成器
 *    - 支持多种对象类型的 ID 分配
 *
 * 4. 管理本地化文本数据
 *    - 多语言支持
 *    - 广播文本、任务文本、NPC对话等
 *
 * 设计理念：
 * - 单例模式：全局唯一实例，通过 sObjectMgr 宏访问
 * - 数据预加载：服务器启动时从数据库加载所有静态数据到内存
 * - 高效查询：使用哈希表和映射表，提供 O(1) 时间复杂度的查询
 *
 * 性能考虑：
 * - 内存占用大（缓存所有静态数据）
 * - 启动时间长（需要加载大量数据）
 * - 运行时查询高效（内存缓存）
 *
 * 线程安全：
 * - 加载阶段：单线程，启动时完成
 * - 运行阶段：只读操作，线程安全
 */

#ifndef _OBJECTMGR_H
#define _OBJECTMGR_H

#include "Common.h"
#include "ConditionMgr.h"
#include "CreatureData.h"
#include "DatabaseEnvFwd.h"
#include "Errors.h"
#include "GameObjectData.h"
#include "ItemTemplate.h"
#include "IteratorPair.h"
#include "NPCHandler.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Trainer.h"
#include "VehicleDefines.h"
#include "UniqueTrackablePtr.h"
#include <iterator>
#include <map>
#include <unordered_map>

class Item;
class Unit;
class Vehicle;
class Map;
enum GossipOptionIcon : uint8;
struct AccessRequirement;
struct DeclinedName;
struct DungeonEncounterEntry;
struct FactionEntry;
struct PlayerClassInfo;
struct PlayerClassLevelInfo;
struct PlayerInfo;
struct PlayerLevelInfo;
struct SkillRaceClassInfoEntry;
struct WorldSafeLocsEntry;

/**
 * @struct PageText
 * @brief 页面文本数据结构
 *
 * 用于存储游戏中的书本、任务说明等多页文本内容。
 */
struct PageText
{
    std::string Text;       ///< 页面文本内容
    uint32 NextPageID;      ///< 下一页的ID，0表示没有下一页
};

/**
 * @enum SummonerType
 * @brief 召唤者类型枚举
 *
 * 定义可以召唤临时生物的对象类型
 */
enum SummonerType
{
    SUMMONER_TYPE_CREATURE      = 0,    ///< 生物召唤
    SUMMONER_TYPE_GAMEOBJECT    = 1,    ///< 游戏对象召唤
    SUMMONER_TYPE_MAP           = 2     ///< 地图召唤（如副本事件）
};

#pragma pack(push, 1)

/**
 * @struct TempSummonGroupKey
 * @brief 临时召唤组键 - 用于存储临时召唤数据的复合键
 *
 * 该结构体作为 TempSummonDataContainer 的键，通过召唤者ID、类型和组ID
 * 唯一标识一组临时召唤生物。
 *
 * 使用场景：
 * - 查找某个生物/游戏对象召唤的所有临时生物
 * - 管理特定事件召唤的生物组
 */
struct TempSummonGroupKey
{
    /**
     * @brief 构造函数
     * @param summonerEntry 召唤者的模板ID（creature_template.entry 或 gameobject_template.entry）
     * @param summonerType 召唤者类型（生物、游戏对象或地图）
     * @param group 召唤组ID（同一召唤者可以有多个组）
     */
    TempSummonGroupKey(uint32 summonerEntry, SummonerType summonerType, uint8 group)
        : SummonerEntry(summonerEntry), SummonerType(summonerType), SummonGroup(group)
    {
    }

    /// 三向比较运算符，用于 map 排序
    std::strong_ordering operator<=>(TempSummonGroupKey const& right) const = default;

    uint32 SummonerEntry;           ///< 召唤者的模板ID
    ::SummonerType SummonerType;    ///< 召唤者类型，见 SummonerType 枚举
    uint8 SummonGroup;              ///< 召唤组ID
};

/**
 * @struct TempSummonData
 * @brief 临时召唤数据 - 存储临时生物的召唤信息
 *
 * 定义了临时召唤生物的模板、位置、类型和消失时间等信息。
 * 用于脚本和事件中动态召唤生物。
 */
struct TempSummonData
{
    uint32 entry;        ///< 召唤生物的模板ID
    Position pos;        ///< 召唤位置（坐标和朝向）
    TempSummonType type; ///< 召唤类型，见 TempSummonType 枚举（如 TIMED_DESPAWN, CORPSE_DESPAWN 等）
    uint32 time;         ///< 消失时间（毫秒），仅对某些召唤类型有效
};

#pragma pack(pop)

/**
 * @enum ScriptCommands
 * @brief 数据库脚本命令枚举
 *
 * 定义了可以通过数据库脚本系统执行的各种命令。
 * 这些命令用于创建动态的游戏事件、NPC行为、任务流程等。
 *
 * 使用场景：
 * - event_scripts 表：事件触发脚本
 * - spell_scripts 表：法术效果脚本
 * - waypoint_scripts 表：路径点脚本
 *
 * 命令参数说明：
 * - datalong/datalong2：整数参数
 * - dataint：整数参数
 * - x/y/z/o：浮点数参数（位置、朝向）
 */
enum ScriptCommands
{
    SCRIPT_COMMAND_TALK                  = 0,    ///< 说话命令：source/target = Creature, datalong = talk type, datalong2 & 1 = player talk, dataint = string_id
    SCRIPT_COMMAND_EMOTE                 = 1,    ///< 表情命令：source/target = Creature, datalong = emote id, datalong2 = 0: set emote state; > 0: play emote
    SCRIPT_COMMAND_FIELD_SET             = 2,    ///< 设置字段：source/target = Creature, datalong = field id, datalog2 = value
    SCRIPT_COMMAND_MOVE_TO               = 3,    ///< 移动到：source/target = Creature, datalong2 = time to reach, x/y/z = destination
    SCRIPT_COMMAND_FLAG_SET              = 4,    ///< 设置标志：source/target = Creature, datalong = field id, datalog2 = bitmask
    SCRIPT_COMMAND_FLAG_REMOVE           = 5,    ///< 移除标志：source/target = Creature, datalong = field id, datalog2 = bitmask
    SCRIPT_COMMAND_TELEPORT_TO           = 6,    ///< 传送：source/target = Creature/Player, datalong = map_id, datalong2 = 0: Player; 1: Creature, x/y/z = destination, o = orientation
    SCRIPT_COMMAND_QUEST_EXPLORED        = 7,    ///< 探索任务：target/source = Player, target/source = GO/Creature, datalong = quest id, datalong2 = distance or 0
    SCRIPT_COMMAND_KILL_CREDIT           = 8,    ///< 击杀荣誉：target/source = Player, datalong = creature entry, datalong2 = 0: personal credit, 1: group credit
    SCRIPT_COMMAND_RESPAWN_GAMEOBJECT    = 9,    ///< 重生游戏对象：source = WorldObject, datalong = GO guid, datalong2 = despawn delay
    SCRIPT_COMMAND_TEMP_SUMMON_CREATURE  = 10,   ///< 临时召唤生物：source = WorldObject, datalong = creature entry, datalong2 = despawn delay, x/y/z = position, o = orientation
    SCRIPT_COMMAND_OPEN_DOOR             = 11,   ///< 开门：source = Unit, datalong = GO guid, datalong2 = reset delay (min 15)
    SCRIPT_COMMAND_CLOSE_DOOR            = 12,   ///< 关门：source = Unit, datalong = GO guid, datalong2 = reset delay (min 15)
    SCRIPT_COMMAND_ACTIVATE_OBJECT       = 13,   ///< 激活对象：source = Unit, target = GO
    SCRIPT_COMMAND_REMOVE_AURA           = 14,   ///< 移除光环：source (datalong2 != 0) or target (datalong2 == 0) = Unit, datalong = spell id
    SCRIPT_COMMAND_CAST_SPELL            = 15,   ///< 施放法术：source and/or target = Unit, datalong2 = cast direction, dataint & 1 = triggered flag
    SCRIPT_COMMAND_PLAY_SOUND            = 16,   ///< 播放声音：source = WorldObject, datalong = sound id, datalong2 = bitmask flags
    SCRIPT_COMMAND_CREATE_ITEM           = 17,   ///< 创建物品：target/source = Player, datalong = item entry, datalong2 = amount
    SCRIPT_COMMAND_DESPAWN_SELF          = 18,   ///< 自我消失：target/source = Creature, datalong = despawn delay

    SCRIPT_COMMAND_LOAD_PATH             = 20,   ///< 加载路径：source = Unit, datalong = path id, datalong2 = is repeatable
    SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT    = 21,   ///< 调用单位脚本：source = WorldObject, datalong = script id, datalong2 = unit lowguid, dataint = script table
    SCRIPT_COMMAND_KILL                  = 22,   ///< 杀死：source/target = Creature, dataint = remove corpse attribute

    // TrinityCore only - TrinityCore 特有命令
    SCRIPT_COMMAND_ORIENTATION           = 30,   ///< 朝向：source = Unit, datalong > 0 turn source to face target, o = orientation
    SCRIPT_COMMAND_EQUIP                 = 31,   ///< 装备：source = Creature, datalong = equipment id
    SCRIPT_COMMAND_MODEL                 = 32,   ///< 模型：source = Creature, datalong = model id
    SCRIPT_COMMAND_CLOSE_GOSSIP          = 33,   ///< 关闭闲聊：source = Player
    SCRIPT_COMMAND_PLAYMOVIE             = 34,   ///< 播放电影：source = Player, datalong = movie id
    SCRIPT_COMMAND_MOVEMENT              = 35,   ///< 移动：source = Creature, datalong = MovementType, datalong2 = MovementDistance, dataint = pathid
    SCRIPT_COMMAND_PLAY_ANIMKIT          = 36    ///< 播放动画：source = Creature, datalong = AnimKit id (NOT ON 3.3.5A)
};

/**
 * @enum ChatType
 * @brief 聊天类型枚举
 *
 * 定义 NPC 说话时使用的聊天频道类型
 */
enum ChatType
{
    CHAT_TYPE_SAY                = 0,    ///< 普通说话（白色文字）
    CHAT_TYPE_YELL               = 1,    ///< 喊叫（红色文字，大范围）
    CHAT_TYPE_TEXT_EMOTE         = 2,    ///< 文本表情（橙色文字）
    CHAT_TYPE_BOSS_EMOTE         = 3,    ///< Boss 表情（屏幕中央显示）
    CHAT_TYPE_WHISPER            = 4,    ///< 密语（粉色文字）
    CHAT_TYPE_BOSS_WHISPER       = 5,    ///< Boss 密语（屏幕中央显示）
    CHAT_TYPE_ZONE_YELL          = 6,    ///< 区域喊叫
    CHAT_TYPE_END                = 255   ///< 结束标记
};

typedef std::map<uint32, PageText> PageTextContainer;

/**
 * @struct InstanceTemplate
 * @brief 副本模板数据结构
 *
 * 存储副本地图的基本配置信息
 */
struct InstanceTemplate
{
    uint32 Parent;         ///< 父地图ID（通常是副本所在的世界地图）
    uint32 ScriptId;       ///< 副本脚本ID
    bool AllowMount;       ///< 是否允许骑乘坐骑
};

typedef std::unordered_map<uint16, InstanceTemplate> InstanceTemplateContainer;

/**
 * @struct GameTele
 * @brief 游戏传送点数据结构
 *
 * 存储游戏中可用的传送点信息，GM命令和脚本可以使用这些传送点
 */
struct GameTele
{
    float  position_x;       ///< X 坐标
    float  position_y;       ///< Y 坐标
    float  position_z;       ///< Z 坐标
    float  orientation;      ///< 朝向
    uint32 mapId;            ///< 地图ID
    std::string name;        ///< 传送点名称
    std::wstring wnameLow;   ///< 小写名称（用于搜索）
};

typedef std::unordered_map<uint32, GameTele> GameTeleContainer;

enum ScriptsType
{
    SCRIPTS_FIRST = 1,

    SCRIPTS_SPELL = SCRIPTS_FIRST,
    SCRIPTS_EVENT,
    SCRIPTS_WAYPOINT,

    SCRIPTS_LAST
};

enum eScriptFlags
{
    // Talk Flags
    SF_TALK_USE_PLAYER          = 0x1,

    // Emote flags
    SF_EMOTE_USE_STATE          = 0x1,

    // TeleportTo flags
    SF_TELEPORT_USE_CREATURE    = 0x1,

    // KillCredit flags
    SF_KILLCREDIT_REWARD_GROUP  = 0x1,

    // RemoveAura flags
    SF_REMOVEAURA_REVERSE       = 0x1,

    // CastSpell flags
    SF_CASTSPELL_SOURCE_TO_TARGET = 0,
    SF_CASTSPELL_SOURCE_TO_SOURCE = 1,
    SF_CASTSPELL_TARGET_TO_TARGET = 2,
    SF_CASTSPELL_TARGET_TO_SOURCE = 3,
    SF_CASTSPELL_SEARCH_CREATURE  = 4,
    SF_CASTSPELL_TRIGGERED      = 0x1,

    // PlaySound flags
    SF_PLAYSOUND_TARGET_PLAYER  = 0x1,
    SF_PLAYSOUND_DISTANCE_SOUND = 0x2,

    // Orientation flags
    SF_ORIENTATION_FACE_TARGET  = 0x1
};

struct ScriptInfo
{
    ScriptsType type;
    uint32 id;
    uint32 delay;
    ScriptCommands command;

    union
    {
        struct
        {
            uint32 nData[3];
            float  fData[4];
        } Raw;

        struct                      // SCRIPT_COMMAND_TALK (0)
        {
            uint32 ChatType;        // datalong
            uint32 Flags;           // datalong2
            int32  TextID;          // dataint
        } Talk;

        struct                      // SCRIPT_COMMAND_EMOTE (1)
        {
            uint32 EmoteID;         // datalong
            uint32 Flags;           // datalong2
        } Emote;

        struct                      // SCRIPT_COMMAND_FIELD_SET (2)
        {
            uint32 FieldID;         // datalong
            uint32 FieldValue;      // datalong2
        } FieldSet;

        struct                      // SCRIPT_COMMAND_MOVE_TO (3)
        {
            uint32 Unused1;         // datalong
            uint32 TravelTime;      // datalong2
            int32  Unused2;         // dataint

            float DestX;
            float DestY;
            float DestZ;
        } MoveTo;

        struct                      // SCRIPT_COMMAND_FLAG_SET (4)
                                    // SCRIPT_COMMAND_FLAG_REMOVE (5)
        {
            uint32 FieldID;         // datalong
            uint32 FieldValue;      // datalong2
        } FlagToggle;

        struct                      // SCRIPT_COMMAND_TELEPORT_TO (6)
        {
            uint32 MapID;           // datalong
            uint32 Flags;           // datalong2
            int32  Unused1;         // dataint

            float DestX;
            float DestY;
            float DestZ;
            float Orientation;
        } TeleportTo;

        struct                      // SCRIPT_COMMAND_QUEST_EXPLORED (7)
        {
            uint32 QuestID;         // datalong
            uint32 Distance;        // datalong2
        } QuestExplored;

        struct                      // SCRIPT_COMMAND_KILL_CREDIT (8)
        {
            uint32 CreatureEntry;   // datalong
            uint32 Flags;           // datalong2
        } KillCredit;

        struct                      // SCRIPT_COMMAND_RESPAWN_GAMEOBJECT (9)
        {
            ObjectGuid::LowType GOGuid;          // datalong
            uint32 DespawnDelay;    // datalong2
        } RespawnGameobject;

        struct                      // SCRIPT_COMMAND_TEMP_SUMMON_CREATURE (10)
        {
            uint32 CreatureEntry;   // datalong
            uint32 DespawnDelay;    // datalong2
            int32  Unused1;         // dataint

            float PosX;
            float PosY;
            float PosZ;
            float Orientation;
        } TempSummonCreature;

        struct                      // SCRIPT_COMMAND_CLOSE_DOOR (12)
                                    // SCRIPT_COMMAND_OPEN_DOOR (11)
        {
            ObjectGuid::LowType GOGuid;          // datalong
            uint32 ResetDelay;      // datalong2
        } ToggleDoor;

                                    // SCRIPT_COMMAND_ACTIVATE_OBJECT (13)

        struct                      // SCRIPT_COMMAND_REMOVE_AURA (14)
        {
            uint32 SpellID;         // datalong
            uint32 Flags;           // datalong2
        } RemoveAura;

        struct                      // SCRIPT_COMMAND_CAST_SPELL (15)
        {
            uint32 SpellID;         // datalong
            uint32 Flags;           // datalong2
            int32  CreatureEntry;   // dataint

            float SearchRadius;
        } CastSpell;

        struct                      // SCRIPT_COMMAND_PLAY_SOUND (16)
        {
            uint32 SoundID;         // datalong
            uint32 Flags;           // datalong2
        } PlaySound;

        struct                      // SCRIPT_COMMAND_CREATE_ITEM (17)
        {
            uint32 ItemEntry;       // datalong
            uint32 Amount;          // datalong2
        } CreateItem;

        struct                      // SCRIPT_COMMAND_DESPAWN_SELF (18)
        {
            uint32 DespawnDelay;    // datalong
        } DespawnSelf;

        struct                      // SCRIPT_COMMAND_LOAD_PATH (20)
        {
            uint32 PathID;          // datalong
            uint32 IsRepeatable;    // datalong2
        } LoadPath;

        struct                      // SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT (21)
        {
            uint32 CreatureEntry;   // datalong
            uint32 ScriptID;        // datalong2
            uint32 ScriptType;      // dataint
        } CallScript;

        struct                      // SCRIPT_COMMAND_KILL (22)
        {
            uint32 Unused1;         // datalong
            uint32 Unused2;         // datalong2
            int32  RemoveCorpse;    // dataint
        } Kill;

        struct                      // SCRIPT_COMMAND_ORIENTATION (30)
        {
            uint32 Flags;           // datalong
            uint32 Unused1;         // datalong2
            int32  Unused2;         // dataint

            float Unused3;
            float Unused4;
            float Unused5;
            float Orientation;
        } Orientation;

        struct                      // SCRIPT_COMMAND_EQUIP (31)
        {
            uint32 EquipmentID;     // datalong
        } Equip;

        struct                      // SCRIPT_COMMAND_MODEL (32)
        {
            uint32 ModelID;         // datalong
        } Model;

                                    // SCRIPT_COMMAND_CLOSE_GOSSIP (33)

        struct                      // SCRIPT_COMMAND_PLAYMOVIE (34)
        {
            uint32 MovieID;         // datalong
        } PlayMovie;

        struct                       // SCRIPT_COMMAND_MOVEMENT (35)
        {
            uint32 MovementType;     // datalong
            uint32 MovementDistance; // datalong2
            int32  Path;             // dataint
        } Movement;
    };

    std::string GetDebugInfo() const;
};

typedef std::multimap<uint32, ScriptInfo> ScriptMap;
typedef std::map<uint32, ScriptMap> ScriptMapMap;
typedef std::multimap<uint32 /*spell id*/, std::pair<uint32 /*script id*/, bool /*enabled*/>> SpellScriptsContainer;
typedef std::pair<SpellScriptsContainer::iterator, SpellScriptsContainer::iterator> SpellScriptsBounds;
TC_GAME_API extern ScriptMapMap sSpellScripts;
TC_GAME_API extern ScriptMapMap sEventScripts;
TC_GAME_API extern ScriptMapMap sWaypointScripts;

std::string GetScriptsTableNameByType(ScriptsType type);
ScriptMapMap* GetScriptsMapByType(ScriptsType type);
std::string GetScriptCommandName(ScriptCommands command);

struct TC_GAME_API InstanceSpawnGroupInfo
{
    enum
    {
        FLAG_ACTIVATE_SPAWN = 0x01,
        FLAG_BLOCK_SPAWN    = 0x02,
        FLAG_ALLIANCE_ONLY  = 0x04,
        FLAG_HORDE_ONLY     = 0x08,

        FLAG_ALL = (FLAG_ACTIVATE_SPAWN | FLAG_BLOCK_SPAWN | FLAG_ALLIANCE_ONLY | FLAG_HORDE_ONLY)
    };
    uint8 BossStateId;
    uint8 BossStates;
    uint32 SpawnGroupId;
    uint8 Flags;
};

struct TC_GAME_API SpellClickInfo
{
    uint32 spellId;
    uint8 castFlags;
    SpellClickUserTypes userType;

    // helpers
    bool IsFitToRequirements(Unit const* clicker, Unit const* clickee) const;
};

typedef std::multimap<uint32, SpellClickInfo> SpellClickInfoContainer;

/**
 * @struct AreaTrigger
 * @brief 区域触发器数据结构
 *
 * 存储区域触发器的传送目标信息
 * 当玩家进入特定区域时触发传送
 */
struct AreaTrigger
{
    uint32 target_mapId;            ///< 目标地图ID
    float  target_X;                ///< 目标X坐标
    float  target_Y;                ///< 目标Y坐标
    float  target_Z;                ///< 目标Z坐标
    float  target_Orientation;      ///< 目标朝向
};

struct AccessRequirement
{
    uint8  levelMin;
    uint8  levelMax;
    uint16 item_level;
    uint32 item;
    uint32 item2;
    uint32 quest_A;
    uint32 quest_H;
    uint32 achievement;
    std::string questFailedText;
};

struct BroadcastText
{
    BroadcastText() : Id(0), LanguageID(0), EmoteId1(0), EmoteId2(0), EmoteId3(0),
                      EmoteDelay1(0), EmoteDelay2(0), EmoteDelay3(0), SoundEntriesID(0), EmotesID(0), Flags(0)
    {
        Text.resize(DEFAULT_LOCALE + 1);
        Text1.resize(DEFAULT_LOCALE + 1);
    }

    uint32 Id;
    uint32 LanguageID;
    std::vector<std::string> Text;
    std::vector<std::string> Text1;
    uint32 EmoteId1;
    uint32 EmoteId2;
    uint32 EmoteId3;
    uint32 EmoteDelay1;
    uint32 EmoteDelay2;
    uint32 EmoteDelay3;
    uint32 SoundEntriesID;
    uint32 EmotesID;
    uint32 Flags;
    // uint32 VerifiedBuild;

    std::string const& GetText(LocaleConstant locale = DEFAULT_LOCALE, uint8 gender = GENDER_MALE, bool forceGender = false) const
    {
        if ((gender == GENDER_FEMALE || gender == GENDER_NONE) && (forceGender || !Text1[DEFAULT_LOCALE].empty()))
        {
            if (Text1.size() > size_t(locale) && !Text1[locale].empty())
                return Text1[locale];
            return Text1[DEFAULT_LOCALE];
        }
        // else if (gender == GENDER_MALE)
        {
            if (Text.size() > size_t(locale) && !Text[locale].empty())
                return Text[locale];
            return Text[DEFAULT_LOCALE];
        }
    }
};

typedef std::unordered_map<uint32, BroadcastText> BroadcastTextContainer;

typedef std::set<ObjectGuid::LowType> CellGuidSet;
struct CellObjectGuids
{
    CellGuidSet creatures;
    CellGuidSet gameobjects;
};
typedef std::unordered_map<uint32/*cell_id*/, CellObjectGuids> CellObjectGuidsMap;
typedef std::unordered_map<uint32/*(mapid, spawnMode) pair*/, CellObjectGuidsMap> MapObjectGuids;

struct TrinityString
{
    std::vector<std::string> Content;
};

struct QuestGreetingLocale
{
    std::vector<std::string> greeting;
};

typedef std::map<ObjectGuid, ObjectGuid> LinkedRespawnContainer;
typedef std::unordered_map<uint32, CreatureTemplate> CreatureTemplateContainer;
typedef std::unordered_map<uint32, CreatureAddon> CreatureTemplateAddonContainer;
typedef std::unordered_map<ObjectGuid::LowType, CreatureData> CreatureDataContainer;
typedef std::unordered_map<ObjectGuid::LowType, CreatureAddon> CreatureAddonContainer;
typedef std::unordered_map<uint16, CreatureBaseStats> CreatureBaseStatsContainer;
typedef std::unordered_map<uint8, EquipmentInfo> EquipmentInfoContainerInternal;
typedef std::unordered_map<uint32, EquipmentInfoContainerInternal> EquipmentInfoContainer;
typedef std::unordered_map<uint32, CreatureModelInfo> CreatureModelContainer;
typedef std::unordered_map<uint32, std::vector<uint32>> CreatureQuestItemMap;
typedef std::unordered_map<uint32, GameObjectTemplate> GameObjectTemplateContainer;
typedef std::unordered_map<uint32, GameObjectTemplateAddon> GameObjectTemplateAddonContainer;
typedef std::unordered_map<ObjectGuid::LowType, GameObjectOverride> GameObjectOverrideContainer;
typedef std::unordered_map<ObjectGuid::LowType, GameObjectData> GameObjectDataContainer;
typedef std::unordered_map<ObjectGuid::LowType, GameObjectAddon> GameObjectAddonContainer;
typedef std::unordered_map<uint32, std::vector<uint32>> GameObjectQuestItemMap;
typedef std::unordered_map<uint32, SpawnGroupTemplateData> SpawnGroupDataContainer;
typedef std::multimap<uint32, SpawnMetadata const*> SpawnGroupLinkContainer;
typedef std::unordered_map<uint16, std::vector<InstanceSpawnGroupInfo>> InstanceSpawnGroupContainer;
typedef std::map<TempSummonGroupKey, std::vector<TempSummonData>> TempSummonDataContainer;
typedef std::unordered_map<uint32, CreatureLocale> CreatureLocaleContainer;
typedef std::unordered_map<uint32, GameObjectLocale> GameObjectLocaleContainer;
typedef std::unordered_map<uint32, ItemTemplate> ItemTemplateContainer;
typedef std::unordered_map<uint32, ItemLocale> ItemLocaleContainer;
typedef std::unordered_map<uint32, ItemSetNameLocale> ItemSetNameLocaleContainer;
typedef std::unordered_map<uint32, QuestLocale> QuestLocaleContainer;
typedef std::unordered_map<uint32, QuestOfferRewardLocale> QuestOfferRewardLocaleContainer;
typedef std::unordered_map<uint32, QuestRequestItemsLocale> QuestRequestItemsLocaleContainer;
typedef std::unordered_map<uint32, NpcTextLocale> NpcTextLocaleContainer;
typedef std::unordered_map<uint32, PageTextLocale> PageTextLocaleContainer;
typedef std::unordered_map<uint32, VehicleSeatAddon> VehicleSeatAddonContainer;

struct GossipMenuItemsLocale
{
    std::vector<std::string> OptionText;
    std::vector<std::string> BoxText;
};

typedef std::unordered_map<std::pair<uint32, uint32>, GossipMenuItemsLocale> GossipMenuItemsLocaleContainer;

struct PointOfInterestLocale
{
    std::vector<std::string> Name;
};

typedef std::unordered_map<uint32, PointOfInterestLocale> PointOfInterestLocaleContainer;
typedef std::unordered_map<uint32, QuestGreetingLocale> QuestGreetingLocaleContainer;

typedef std::unordered_map<uint32, TrinityString> TrinityStringContainer;

typedef std::multimap<uint32, uint32> QuestRelations; // unit/go -> quest

struct QuestRelationResult
{
    public:
        struct Iterator
        {
            public:
                using iterator_category = std::forward_iterator_tag;
                using value_type = QuestRelations::mapped_type;
                using pointer = value_type const*;
                using reference = value_type const&;
                using difference_type = void;

                Iterator(QuestRelations::const_iterator it, QuestRelations::const_iterator end, bool onlyActive)
                    : _it(it), _end(end), _onlyActive(onlyActive)
                {
                    skip();
                }

                bool operator==(Iterator const& other) const { return _it == other._it; }

                Iterator& operator++() { ++_it; skip(); return *this; }
                Iterator operator++(int) { Iterator t = *this; ++*this; return t; }

                value_type operator*() const { return _it->second; }

            private:
                void skip() { if (_onlyActive) _skip(); }
                void _skip();

                QuestRelations::const_iterator _it, _end;
                bool _onlyActive;
        };

        QuestRelationResult() : _onlyActive(false) {}
        QuestRelationResult(std::pair<QuestRelations::const_iterator, QuestRelations::const_iterator> range, bool onlyActive)
            : _begin(range.first), _end(range.second), _onlyActive(onlyActive) {}

        Iterator begin() const { return { _begin, _end, _onlyActive }; }
        Iterator end() const { return { _end, _end, _onlyActive }; }

        bool HasQuest(uint32 questId) const;

    private:
        QuestRelations::const_iterator _begin, _end;
        bool _onlyActive;
};

struct PlayerCreateInfoItem
{
    PlayerCreateInfoItem(uint32 id, uint32 amount) : item_id(id), item_amount(amount) { }

    uint32 item_id = 0;
    uint32 item_amount = 0;
};

typedef std::vector<PlayerCreateInfoItem> PlayerCreateInfoItems;

struct PlayerClassLevelInfo
{
    uint16 basehealth = 0;
    uint16 basemana = 0;
};

struct PlayerClassInfo
{
    //[level-1] 0..MaxPlayerLevel-1
    std::unique_ptr<PlayerClassLevelInfo[]> levelInfo;
};

struct PlayerLevelInfo
{
    uint8 stats[MAX_STATS] = { };
};

typedef std::vector<uint32> PlayerCreateInfoSpells;

struct PlayerCreateInfoAction
{
    PlayerCreateInfoAction(uint8 _button, uint32 _action, uint8 _type) : button(_button), type(_type), action(_action) { }

    uint8 button = 0;
    uint8 type = 0;
    uint32 action = 0;
};

typedef std::vector<PlayerCreateInfoAction> PlayerCreateInfoActions;

struct PlayerCreateInfoSkill
{
    uint16 SkillId;
    uint16 Rank;
};

typedef std::vector<PlayerCreateInfoSkill> PlayerCreateInfoSkills;

// existence checked by displayId != 0
struct PlayerInfo
{
    uint32 mapId = 0;
    uint32 areaId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float orientation = 0.0f;
    uint16 displayId_m = 0;
    uint16 displayId_f = 0;
    PlayerCreateInfoItems item;
    PlayerCreateInfoSpells customSpells;
    PlayerCreateInfoSpells castSpells;
    PlayerCreateInfoActions action;
    PlayerCreateInfoSkills skills;

    //[level-1] 0..MaxPlayerLevel-1
    std::unique_ptr<PlayerLevelInfo[]> levelInfo;
};

typedef std::multimap<int32, uint32> ExclusiveQuestGroups; // exclusiveGroupId -> quest
typedef std::pair<ExclusiveQuestGroups::const_iterator, ExclusiveQuestGroups::const_iterator> ExclusiveQuestGroupsBounds;

struct PetLevelInfo
{
    uint16 stats[MAX_STATS] = {};
    uint16 health = 0;
    uint16 mana = 0;
    uint32 armor = 0;
    uint16 minDamage = 0;
    uint16 maxDamage = 0;
};

struct MailLevelReward
{
    MailLevelReward() : raceMask(0), mailTemplateId(0), senderEntry(0) { }
    MailLevelReward(uint32 _raceMask, uint32 _mailTemplateId, uint32 _senderEntry) : raceMask(_raceMask), mailTemplateId(_mailTemplateId), senderEntry(_senderEntry) { }

    uint32 raceMask;
    uint32 mailTemplateId;
    uint32 senderEntry;
};

typedef std::list<MailLevelReward> MailLevelRewardList;
typedef std::unordered_map<uint8, MailLevelRewardList> MailLevelRewardContainer;

// We assume the rate is in general the same for all three types below, but chose to keep three for scalability and customization
struct RepRewardRate
{
    float questRate;            // We allow rate = 0.0 in database. For this case, it means that
    float questDailyRate;
    float questWeeklyRate;
    float questMonthlyRate;
    float questRepeatableRate;
    float creatureRate;         // no reputation are given at all for this faction/rate type.
    float spellRate;
};

struct ReputationOnKillEntry
{
    uint32 RepFaction1;
    uint32 RepFaction2;
    uint32 ReputationMaxCap1;
    int32 RepValue1;
    uint32 ReputationMaxCap2;
    int32 RepValue2;
    bool IsTeamAward1;
    bool IsTeamAward2;
    bool TeamDependent;
};

struct RepSpilloverTemplate
{
    uint32 faction[MAX_SPILLOVER_FACTIONS];
    float faction_rate[MAX_SPILLOVER_FACTIONS];
    uint32 faction_rank[MAX_SPILLOVER_FACTIONS];
};

struct PointOfInterest
{
    uint32 ID;
    float PositionX;
    float PositionY;
    uint32 Icon;
    uint32 Flags;
    uint32 Importance;
    std::string Name;
};

struct GossipMenuItems
{
    uint32              MenuID;
    uint32              OptionID;
    GossipOptionIcon    OptionIcon;
    std::string         OptionText;
    uint32              OptionBroadcastTextID;
    uint32              OptionType;
    uint32              OptionNpcFlag;
    uint32              ActionMenuID;
    uint32              ActionPoiID;
    bool                BoxCoded;
    uint32              BoxMoney;
    std::string         BoxText;
    uint32              BoxBroadcastTextID;
    ConditionContainer  Conditions;
};

struct GossipMenus
{
    uint32              MenuID;
    uint32              TextID;
    ConditionContainer  Conditions;
};

typedef std::multimap<uint32, GossipMenus> GossipMenusContainer;
typedef std::pair<GossipMenusContainer::const_iterator, GossipMenusContainer::const_iterator> GossipMenusMapBounds;
typedef std::pair<GossipMenusContainer::iterator, GossipMenusContainer::iterator> GossipMenusMapBoundsNonConst;
typedef std::multimap<uint32, GossipMenuItems> GossipMenuItemsContainer;
typedef std::pair<GossipMenuItemsContainer::const_iterator, GossipMenuItemsContainer::const_iterator> GossipMenuItemsMapBounds;
typedef std::pair<GossipMenuItemsContainer::iterator, GossipMenuItemsContainer::iterator> GossipMenuItemsMapBoundsNonConst;

struct QuestPOIBlobPoint
{
    int32 X = 0;
    int32 Y = 0;
};

struct QuestPOIBlobData
{
    uint32 BlobIndex = 0;
    int32 ObjectiveIndex = 0;
    uint32 MapID = 0;
    uint32 WorldMapAreaID = 0;
    uint32 Floor = 0;
    uint32 Unk3 = 0;
    uint32 Unk4 = 0;
    std::vector<QuestPOIBlobPoint> QuestPOIBlobPointStats;
};

struct QuestPOIData
{
    uint32 QuestID = 0;
    std::vector<QuestPOIBlobData> QuestPOIBlobDataStats;
};

struct QuestPOIWrapper
{
    QuestPOIData POIData;
    ByteBuffer QueryDataBuffer;

    void InitializeQueryData();
    ByteBuffer BuildQueryData() const;

    QuestPOIWrapper() : QueryDataBuffer(0) { }
};

typedef std::unordered_map<uint32, QuestPOIWrapper> QuestPOIContainer;

struct QuestGreeting
{
    uint16 greetEmoteType;
    uint32 greetEmoteDelay;
    std::string greeting;

    QuestGreeting() : greetEmoteType(0), greetEmoteDelay(0) { }
    QuestGreeting(uint16 _greetEmoteType, uint32 _greetEmoteDelay, std::string _greeting)
        : greetEmoteType(_greetEmoteType), greetEmoteDelay(_greetEmoteDelay), greeting(_greeting) { }
};

typedef std::unordered_map<uint8, std::unordered_map<uint32, QuestGreeting>> QuestGreetingContainer;

struct GraveyardData
{
    uint32 safeLocId;
    uint32 team;
};

typedef std::multimap<uint32, GraveyardData> GraveyardContainer;
typedef std::pair<GraveyardContainer::const_iterator, GraveyardContainer::const_iterator> GraveyardMapBounds;
typedef std::pair<GraveyardContainer::iterator, GraveyardContainer::iterator> GraveyardMapBoundsNonConst;

typedef std::unordered_map<uint32, VendorItemData> CacheVendorItemContainer;

enum SkillRangeType
{
    SKILL_RANGE_LANGUAGE,                                   // 300..300
    SKILL_RANGE_LEVEL,                                      // 1..max skill for level
    SKILL_RANGE_MONO,                                       // 1..1, grey monolite bar
    SKILL_RANGE_RANK,                                       // 1..skill for known rank
    SKILL_RANGE_NONE                                        // 0..0 always
};

SkillRangeType GetSkillRangeType(SkillRaceClassInfoEntry const* rcEntry);

#define MAX_PLAYER_NAME          12                         // max allowed by client name length
#define MAX_INTERNAL_PLAYER_NAME 15                         // max server internal player name length (> MAX_PLAYER_NAME for support declined names)
#define MAX_PET_NAME             12                         // max allowed by client name length
#define MAX_CHARTER_NAME         24                         // max allowed by client name length

TC_GAME_API bool normalizePlayerName(std::string& name);
#define SPAWNGROUP_MAP_UNSET            0xFFFFFFFF

struct LanguageDesc
{
    Language lang_id;
    uint32   spell_id;
    uint32   skill_id;
};

TC_GAME_API extern LanguageDesc lang_description[LANGUAGES_COUNT];
LanguageDesc const* GetLanguageDescByID(uint32 lang);

enum EncounterCreditType : uint8
{
    ENCOUNTER_CREDIT_KILL_CREATURE  = 0,
    ENCOUNTER_CREDIT_CAST_SPELL     = 1
};

struct DungeonEncounter
{
    DungeonEncounter(DungeonEncounterEntry const* _dbcEntry, EncounterCreditType _creditType, uint32 _creditEntry, uint32 _lastEncounterDungeon)
        : dbcEntry(_dbcEntry), creditType(_creditType), creditEntry(_creditEntry), lastEncounterDungeon(_lastEncounterDungeon) { }

    DungeonEncounterEntry const* dbcEntry;
    EncounterCreditType creditType;
    uint32 creditEntry;
    uint32 lastEncounterDungeon;
};

typedef std::vector<std::unique_ptr<DungeonEncounter const>> DungeonEncounterList;
typedef std::unordered_map<uint32, DungeonEncounterList> DungeonEncounterContainer;

typedef std::map<std::pair<SummonSlot /*TotemSlot*/, Races /*RaceId*/>, uint32 /*DisplayId*/> PlayerTotemModelMap;

enum QueryDataGroup
{
    QUERY_DATA_CREATURES        = 0x01,
    QUERY_DATA_GAMEOBJECTS      = 0x02,
    QUERY_DATA_ITEMS            = 0x04,
    QUERY_DATA_QUESTS           = 0x08,
    QUERY_DATA_POIS             = 0x10,

    QUERY_DATA_ALL              = 0xFF
};

class PlayerDumpReader;

/**
 * @brief 对象管理器类
 *
 * ObjectMgr 是 TrinityCore 中核心的对象管理器，负责管理游戏中的各种静态数据，
 * 包括生物模板、游戏对象模板、物品模板、任务模板等。
 *
 * 该类采用单例模式，通过 sObjectMgr 宏或 ObjectMgr::instance() 访问全局实例。
 * 所有游戏数据在服务器启动时从数据库加载并缓存在内存中，提供高效的查询接口。
 *
 * 主要职责：
 * - 加载和管理生物模板（CreatureTemplate）
 * - 加载和管理游戏对象模板（GameObjectTemplate）
 * - 加载和管理物品模板（ItemTemplate）
 * - 加载和管理任务模板（Quest）
 * - 管理 GUID 生成器
 * - 管理刷怪数据和生成组
 * - 管理本地化文本数据
 */
class TC_GAME_API ObjectMgr
{
    friend class PlayerDumpReader;      ///< 玩家数据读取器友元类
    friend class UnitTestDataLoader;    ///< 单元测试数据加载器友元类

    private:
        ObjectMgr();                    ///< 私有构造函数（单例模式）
        ~ObjectMgr();                   ///< 私有析构函数

    public:
        ObjectMgr(ObjectMgr const&) = delete;       ///< 禁用拷贝构造
        ObjectMgr(ObjectMgr&&) = delete;            ///< 禁用移动构造

        ObjectMgr& operator= (ObjectMgr const&) = delete;   ///< 禁用拷贝赋值
        ObjectMgr& operator= (ObjectMgr&&) = delete;        ///< 禁用移动赋值

        /**
         * @brief 获取单例实例
         * @return ObjectMgr 单例指针
         */
        static ObjectMgr* instance();

        typedef std::unordered_map<uint32, Trinity::unique_trackable_ptr<Quest>> QuestContainer;  ///< 任务容器类型

        typedef std::unordered_map<uint32, AreaTrigger> AreaTriggerContainer;  ///< 区域触发器容器类型

        typedef std::map<uint32, uint32> AreaTriggerScriptContainer;  ///< 区域触发器脚本容器类型

        typedef std::unordered_map<uint32, std::unique_ptr<AccessRequirement>> AccessRequirementContainer;  ///< 进入要求容器类型

        typedef std::unordered_map<uint32, RepRewardRate > RepRewardRateContainer;  ///< 声望奖励率容器类型
        typedef std::unordered_map<uint32, ReputationOnKillEntry> RepOnKillContainer;  ///< 击杀声望容器类型
        typedef std::unordered_map<uint32, RepSpilloverTemplate> RepSpilloverTemplateContainer;  ///< 声望溢出模板容器类型

        typedef std::unordered_map<uint32, PointOfInterest> PointOfInterestContainer;  ///< 兴趣点容器类型

        typedef std::vector<std::string> ScriptNameContainer;  ///< 脚本名称容器类型

        typedef std::map<uint32, uint32> CharacterConversionMap;  ///< 角色转换映射类型

        /**
         * @brief 根据模板ID获取游戏对象模板
         * @param entry 游戏对象模板ID
         * @return 游戏对象模板指针，如果不存在返回nullptr
         */
        GameObjectTemplate const* GetGameObjectTemplate(uint32 entry) const;
        /**
         * @brief 获取所有游戏对象模板
         * @return 游戏对象模板容器的常引用
         */
        GameObjectTemplateContainer const& GetGameObjectTemplates() const { return _gameObjectTemplateStore; }
        uint32 LoadReferenceVendor(int32 vendor, int32 item_id, std::set<uint32>* skip_vendors);

        /**
         * @brief 从数据库加载游戏对象模板数据
         */
        void LoadGameObjectTemplate();
        /**
         * @brief 加载游戏对象模板附加数据
         */
        void LoadGameObjectTemplateAddons();
        /**
         * @brief 加载游戏对象覆盖数据
         */
        void LoadGameObjectOverrides();

        /**
         * @brief 根据模板ID获取生物模板
         * @param entry 生物模板ID（creature_template表中的entry字段）
         * @return 生物模板指针，如果不存在返回nullptr
         */
        CreatureTemplate const* GetCreatureTemplate(uint32 entry) const;
        /**
         * @brief 获取所有生物模板
         * @return 生物模板容器的常引用
         */
        CreatureTemplateContainer const& GetCreatureTemplates() const { return _creatureTemplateStore; }
        /**
         * @brief 根据模型ID获取生物模型信息
         * @param modelId 模型ID
         * @return 生物模型信息指针
         */
        CreatureModelInfo const* GetCreatureModelInfo(uint32 modelId) const;
        /**
         * @brief 获取生物模型信息并随机选择性别
         * @param displayID 显示ID（输入输出参数）
         * @return 生物模型信息指针
         */
        CreatureModelInfo const* GetCreatureModelRandomGender(uint32* displayID) const;
        /**
         * @brief 选择生物的显示ID
         * @param cinfo 生物模板指针
         * @param data 生物数据指针（可选）
         * @return 选中的显示ID
         */
        static uint32 ChooseDisplayId(CreatureTemplate const* cinfo, CreatureData const* data = nullptr);
        /**
         * @brief 选择生物标志
         * @param cinfo 生物模板指针
         * @param npcflag NPC标志输出参数
         * @param unit_flags 单位标志输出参数
         * @param dynamicflags 动态标志输出参数
         * @param data 生物数据指针（可选）
         */
        static void ChooseCreatureFlags(CreatureTemplate const* cinfo, uint32* npcflag, uint32* unit_flags, uint32* dynamicflags, CreatureData const* data = nullptr);
        /**
         * @brief 获取装备信息
         * @param entry 生物模板ID
         * @param id 装备ID输出参数
         * @return 装备信息指针
         */
        EquipmentInfo const* GetEquipmentInfo(uint32 entry, int8& id) const;
        /**
         * @brief 根据GUID获取生物附加数据
         * @param lowguid 生物的低GUID
         * @return 生物附加数据指针
         */
        CreatureAddon const* GetCreatureAddon(ObjectGuid::LowType lowguid) const;
        /**
         * @brief 根据GUID获取游戏对象附加数据
         * @param lowguid 游戏对象的低GUID
         * @return 游戏对象附加数据指针
         */
        GameObjectAddon const* GetGameObjectAddon(ObjectGuid::LowType lowguid) const;
        /**
         * @brief 根据模板ID获取游戏对象模板附加数据
         * @param entry 游戏对象模板ID
         * @return 游戏对象模板附加数据指针
         */
        GameObjectTemplateAddon const* GetGameObjectTemplateAddon(uint32 entry) const;
        /**
         * @brief 根据刷新ID获取游戏对象覆盖数据
         * @param spawnId 刷新ID
         * @return 游戏对象覆盖数据指针
         */
        GameObjectOverride const* GetGameObjectOverride(ObjectGuid::LowType spawnId) const;
        /**
         * @brief 根据模板ID获取生物模板附加数据
         * @param entry 生物模板ID
         * @return 生物模板附加数据指针
         */
        CreatureAddon const* GetCreatureTemplateAddon(uint32 entry) const;
        /**
         * @brief 根据刷新ID获取生物移动覆盖数据
         * @param spawnId 刷新ID
         * @return 生物移动数据指针
         */
        CreatureMovementData const* GetCreatureMovementOverride(ObjectGuid::LowType spawnId) const;
        /**
         * @brief 根据模板ID获取物品模板
         * @param entry 物品模板ID
         * @return 物品模板指针，如果不存在返回nullptr
         */
        ItemTemplate const* GetItemTemplate(uint32 entry) const;
        /**
         * @brief 获取所有物品模板
         * @return 物品模板容器的常引用
         */
        ItemTemplateContainer const& GetItemTemplateStore() const { return _itemTemplateStore; }

        uint32 GetModelForTotem(SummonSlot totemSlot, Races race) const;

        ItemSetNameEntry const* GetItemSetNameEntry(uint32 itemId) const
        {
            ItemSetNameContainer::const_iterator itr = _itemSetNameStore.find(itemId);
            if (itr != _itemSetNameStore.end())
                return &itr->second;
            return nullptr;
        }

        InstanceTemplateContainer const& GetInstanceTemplates() const { return _instanceTemplateStore; }
        InstanceTemplate const* GetInstanceTemplate(uint32 mapId) const;

        PetLevelInfo const* GetPetLevelInfo(uint32 creature_id, uint8 level) const;

        PlayerClassInfo const* GetPlayerClassInfo(uint32 class_) const { return class_ < MAX_CLASSES ? _playerClassInfo[class_].get() : nullptr; }

        void GetPlayerClassLevelInfo(uint32 class_, uint8 level, PlayerClassLevelInfo* info) const;

        PlayerInfo const* GetPlayerInfo(uint32 race, uint32 class_) const;

        void GetPlayerLevelInfo(uint32 race, uint32 class_, uint8 level, PlayerLevelInfo* info) const;

        std::vector<uint32> const* GetGameObjectQuestItemList(uint32 id) const
        {
            GameObjectQuestItemMap::const_iterator itr = _gameObjectQuestItemStore.find(id);
            if (itr != _gameObjectQuestItemStore.end())
                return &itr->second;
            return nullptr;
        }
        GameObjectQuestItemMap const* GetGameObjectQuestItemMap() const { return &_gameObjectQuestItemStore; }

        std::vector<uint32> const* GetCreatureQuestItemList(uint32 id) const
        {
            CreatureQuestItemMap::const_iterator itr = _creatureQuestItemStore.find(id);
            if (itr != _creatureQuestItemStore.end())
                return &itr->second;
            return nullptr;
        }
        CreatureQuestItemMap const* GetCreatureQuestItemMap() const { return &_creatureQuestItemStore; }

        /**
         * @brief 获取最近的出租车节点
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param mapid 地图ID
         * @param team 阵营
         * @return 最近的出租车节点ID
         */
        uint32 GetNearestTaxiNode(float x, float y, float z, uint32 mapid, uint32 team);
        /**
         * @brief 获取出租车路径
         * @param source 起点节点ID
         * @param destination 终点节点ID
         * @param path 路径ID输出参数
         * @param cost 费用输出参数
         */
        void GetTaxiPath(uint32 source, uint32 destination, uint32 &path, uint32 &cost);
        /**
         * @brief 获取出租车坐骑显示ID
         * @param id 节点ID
         * @param team 阵营
         * @param allowed_alt_team 是否允许对立阵营
         * @return 坐骑显示ID
         */
        uint32 GetTaxiMountDisplayId(uint32 id, uint32 team, bool allowed_alt_team = false);

        /**
         * @brief 根据任务ID获取任务模板
         * @param quest_id 任务ID
         * @return 任务模板指针，如果不存在返回nullptr
         */
        Quest const* GetQuestTemplate(uint32 quest_id) const;

        /**
         * @brief 获取所有任务模板
         * @return 任务模板容器的常引用
         */
        QuestContainer const& GetQuestTemplates() const { return _questTemplates; }

        uint32 GetQuestForAreaTrigger(uint32 Trigger_ID) const
        {
            QuestAreaTriggerContainer::const_iterator itr = _questAreaTriggerStore.find(Trigger_ID);
            if (itr != _questAreaTriggerStore.end())
                return itr->second;
            return 0;
        }

        bool IsTavernAreaTrigger(uint32 Trigger_ID) const
        {
            return _tavernAreaTriggerStore.find(Trigger_ID) != _tavernAreaTriggerStore.end();
        }

        bool IsGameObjectForQuests(uint32 entry) const
        {
            return _gameObjectForQuestStore.find(entry) != _gameObjectForQuestStore.end();
        }

        GossipText const* GetGossipText(uint32 Text_ID) const;
        QuestGreeting const* GetQuestGreeting(ObjectGuid guid) const;

        WorldSafeLocsEntry const* GetDefaultGraveyard(uint32 team) const;
        WorldSafeLocsEntry const* GetClosestGraveyard(float x, float y, float z, uint32 MapId, uint32 team) const;
        bool AddGraveyardLink(uint32 id, uint32 zoneId, uint32 team, bool persist = true);
        void RemoveGraveyardLink(uint32 id, uint32 zoneId, uint32 team, bool persist = false);
        void LoadGraveyardZones();
        GraveyardData const* FindGraveyardData(uint32 id, uint32 zone) const;

        AreaTrigger const* GetAreaTrigger(uint32 trigger) const;
        AccessRequirement const* GetAccessRequirement(uint32 mapid, Difficulty difficulty) const;
        AreaTrigger const* GetGoBackTrigger(uint32 Map) const;
        AreaTrigger const* GetMapEntranceTrigger(uint32 Map) const;

        uint32 GetAreaTriggerScriptId(uint32 trigger_id) const;
        SpellScriptsBounds GetSpellScriptsBounds(uint32 spellId);

        RepRewardRate const* GetRepRewardRate(uint32 factionId) const
        {
            RepRewardRateContainer::const_iterator itr = _repRewardRateStore.find(factionId);
            if (itr != _repRewardRateStore.end())
                return &itr->second;

            return nullptr;
        }

        ReputationOnKillEntry const* GetReputationOnKilEntry(uint32 id) const
        {
            RepOnKillContainer::const_iterator itr = _repOnKillStore.find(id);
            if (itr != _repOnKillStore.end())
                return &itr->second;
            return nullptr;
        }

        int32 GetBaseReputationOf(FactionEntry const* factionEntry, uint8 race, uint8 playerClass) const;

        RepSpilloverTemplate const* GetRepSpilloverTemplate(uint32 factionId) const
        {
            RepSpilloverTemplateContainer::const_iterator itr = _repSpilloverTemplateStore.find(factionId);
            if (itr != _repSpilloverTemplateStore.end())
                return &itr->second;

            return nullptr;
        }

        PointOfInterest const* GetPointOfInterest(uint32 id) const
        {
            PointOfInterestContainer::const_iterator itr = _pointsOfInterestStore.find(id);
            if (itr != _pointsOfInterestStore.end())
                return &itr->second;
            return nullptr;
        }

        QuestPOIWrapper const* GetQuestPOIWrapper(uint32 questId) const;

        VehicleTemplate const* GetVehicleTemplate(Vehicle* veh) const;
        VehicleAccessoryList const* GetVehicleAccessoryList(Vehicle* veh) const;

        DungeonEncounterList const* GetDungeonEncounterList(uint32 mapId, Difficulty difficulty) const;

        /**
         * @brief 从数据库加载所有任务数据
         *
         * 该函数从 quest_template 表加载任务模板数据，构建任务对象并存储到 _questTemplates 容器中。
         * 同时也会加载任务的关联数据，包括任务目标、奖励等。
         */
        void LoadQuests();
        /**
         * @brief 加载任务发起者和结束者关系
         *
         * 加载哪些NPC或游戏对象可以发放任务或接受任务完成。
         */
        void LoadQuestStartersAndEnders();
        /**
         * @brief 加载游戏对象任务发起者
         */
        void LoadGameobjectQuestStarters();
        /**
         * @brief 加载游戏对象任务结束者
         */
        void LoadGameobjectQuestEnders();
        /**
         * @brief 加载生物任务发起者
         */
        void LoadCreatureQuestStarters();
        /**
         * @brief 加载生物任务结束者
         */
        void LoadCreatureQuestEnders();

        QuestRelations* GetGOQuestRelationMapHACK() { return &_goQuestRelations; }
        QuestRelationResult GetGOQuestRelations(uint32 entry) const { return GetQuestRelationsFrom(_goQuestRelations, entry, true); }
        QuestRelationResult GetGOQuestInvolvedRelations(uint32 entry) const { return GetQuestRelationsFrom(_goQuestInvolvedRelations, entry, false); }
        QuestRelations* GetCreatureQuestRelationMapHACK() { return &_creatureQuestRelations; }
        QuestRelationResult GetCreatureQuestRelations(uint32 entry) const { return GetQuestRelationsFrom(_creatureQuestRelations, entry, true); }
        QuestRelationResult GetCreatureQuestInvolvedRelations(uint32 entry) const { return GetQuestRelationsFrom(_creatureQuestInvolvedRelations, entry, false); }

        ExclusiveQuestGroupsBounds GetExclusiveQuestGroupBounds(int32 exclusiveGroupId) const
        {
            return _exclusiveQuestGroups.equal_range(exclusiveGroupId);
        }

        bool LoadTrinityStrings();

        void LoadEventScripts();
        void LoadSpellScripts();
        void LoadWaypointScripts();

        void LoadSpellScriptNames();
        void ValidateSpellScripts();

        void LoadBroadcastTexts();
        void LoadBroadcastTextLocales();
        void LoadCreatureClassLevelStats();
        void LoadCreatureLocales();
        /**
         * @brief 从数据库加载生物模板数据
         *
         * 该函数从 creature_template 表加载所有生物模板数据。
         * 生物模板定义了生物的基础属性，包括名称、等级、生命值、伤害、
         * AI名称、脚本名称等信息。这是服务器启动时的关键加载函数之一。
         */
        void LoadCreatureTemplates();
        /**
         * @brief 加载生物模板附加数据
         *
         * 从 creature_template_addon 表加载生物模板的附加数据，
         * 如光环、复活时间等。
         */
        void LoadCreatureTemplateAddons();
        /**
         * @brief 加载单个生物模板
         * @param fields 数据库字段数组
         */
        void LoadCreatureTemplate(Field* fields);
        /**
         * @brief 加载生物模板抗性数据
         */
        void LoadCreatureTemplateResistances();
        /**
         * @brief 加载生物模板技能数据
         */
        void LoadCreatureTemplateSpells();
        /**
         * @brief 检查生物模板数据的有效性
         * @param cInfo 生物模板指针
         */
        void CheckCreatureTemplate(CreatureTemplate const* cInfo);
        /**
         * @brief 检查生物移动数据的有效性
         * @param table 表名
         * @param id 记录ID
         * @param creatureMovement 生物移动数据引用
         */
        void CheckCreatureMovement(char const* table, uint64 id, CreatureMovementData& creatureMovement);
        /**
         * @brief 加载游戏对象任务物品映射
         */
        void LoadGameObjectQuestItems();
        /**
         * @brief 加载生物任务物品映射
         */
        void LoadCreatureQuestItems();
        /**
         * @brief 加载临时召唤数据
         */
        void LoadTempSummons();
        /**
         * @brief 加载生物刷怪数据
         *
         * 从 creature 表加载所有生物的刷新数据，包括位置、朝向、
         * 刷新时间等信息。
         */
        void LoadCreatures();
        /**
         * @brief 加载链接复活数据
         */
        void LoadLinkedRespawn();
        /**
         * @brief 设置生物链接复活
         * @param guid 生物GUID
         * @param linkedGuid 链接生物GUID
         * @return 设置是否成功
         */
        bool SetCreatureLinkedRespawn(ObjectGuid::LowType guid, ObjectGuid::LowType linkedGuid);
        /**
         * @brief 加载生物附加数据
         */
        void LoadCreatureAddons();
        /**
         * @brief 加载游戏对象附加数据
         */
        void LoadGameObjectAddons();
        /**
         * @brief 加载生物模型信息
         */
        void LoadCreatureModelInfo();
        /**
         * @brief 加载玩家图腾模型
         */
        void LoadPlayerTotemModels();
        /**
         * @brief 加载装备模板
         */
        void LoadEquipmentTemplates();
        /**
         * @brief 加载生物移动覆盖数据
         */
        void LoadCreatureMovementOverrides();
        /**
         * @brief 加载游戏对象本地化数据
         */
        void LoadGameObjectLocales();
        /**
         * @brief 加载游戏对象刷怪数据
         *
         * 从 gameobject 表加载所有游戏对象的刷新数据。
         */
        void LoadGameObjects();
        /**
         * @brief 加载生成组模板
         */
        void LoadSpawnGroupTemplates();
        /**
         * @brief 加载生成组
         */
        void LoadSpawnGroups();
        /**
         * @brief 加载副本生成组
         */
        void LoadInstanceSpawnGroups();
        /**
         * @brief 从数据库加载物品模板数据
         *
         * 从 item_template 表加载所有物品模板数据。
         * 物品模板定义了物品的基础属性，包括名称、描述、属性、
         * 伤害、护甲、需求等级等信息。
         */
        void LoadItemTemplates();
        /**
         * @brief 加载物品本地化数据
         */
        void LoadItemLocales();
        /**
         * @brief 加载套装名称数据
         */
        void LoadItemSetNames();
        /**
         * @brief 加载套装名称本地化数据
         */
        void LoadItemSetNameLocales();
        /**
         * @brief 加载任务本地化数据
         */
        void LoadQuestLocales();
        /**
         * @brief 加载NPC文本本地化数据
         */
        void LoadNpcTextLocales();
        /**
         * @brief 加载任务奖励本地化数据
         */
        void LoadQuestOfferRewardLocale();
        /**
         * @brief 加载任务请求数据本地化数据
         */
        void LoadQuestRequestItemsLocale();
        /**
         * @brief 加载页面文本本地化数据
         */
        void LoadPageTextLocales();
        /**
         * @brief 加载闲聊菜单项本地化数据
         */
        void LoadGossipMenuItemsLocales();
        /**
         * @brief 加载兴趣点本地化数据
         */
        void LoadPointOfInterestLocales();
        /**
         * @brief 加载任务问候语本地化数据
         */
        void LoadQuestGreetingLocales();
        /**
         * @brief 加载副本模板
         */
        void LoadInstanceTemplate();
        /**
         * @brief 加载副本遭遇战数据
         */
        void LoadInstanceEncounters();
        /**
         * @brief 加载邮件等级奖励数据
         */
        void LoadMailLevelRewards();
        /**
         * @brief 加载载具模板附件
         */
        void LoadVehicleTemplateAccessories();
        /**
         * @brief 加载载具模板
         */
        void LoadVehicleTemplate();
        /**
         * @brief 加载载具附件
         */
        void LoadVehicleAccessories();
        /**
         * @brief 加载载具座位附加数据
         */
        void LoadVehicleSeatAddon();

        void LoadGossipText();

        void LoadAreaTriggerTeleports();
        void LoadAccessRequirements();
        void LoadQuestAreaTriggers();
        void LoadQuestGreetings();
        void LoadAreaTriggerScripts();
        void LoadTavernAreaTriggers();
        void LoadGameObjectForQuests();

        void LoadPageTexts();
        PageText const* GetPageText(uint32 pageEntry);

        void LoadPlayerInfo();
        void LoadPetLevelInfo();
        void LoadExplorationBaseXP();
        void LoadPetNames();
        void LoadPetNumber();
        void LoadFishingBaseSkillLevel();

        void LoadReputationRewardRate();
        void LoadReputationOnKill();
        void LoadReputationSpilloverTemplate();

        void LoadPointsOfInterest();
        void LoadQuestPOI();

        void LoadNPCSpellClickSpells();

        void LoadGameTele();

        void LoadGossipMenu();
        void LoadGossipMenuItems();

        void LoadVendors();
        void LoadTrainers();
        void LoadCreatureDefaultTrainers();

        void InitializeQueriesData(QueryDataGroup mask);

        std::string GeneratePetName(uint32 entry);
        uint32 GetBaseXP(uint8 level);
        uint32 GetXPForLevel(uint8 level) const;

        int32 GetFishingBaseSkillLevel(uint32 entry) const
        {
            FishingBaseSkillContainer::const_iterator itr = _fishingBaseForAreaStore.find(entry);
            return itr != _fishingBaseForAreaStore.end() ? itr->second : 0;
        }

        void ReturnOrDeleteOldMails(bool serverUp);

        CreatureBaseStats const* GetCreatureBaseStats(uint8 level, uint8 unitClass);

        void SetHighestGuids();

        template<HighGuid type>
        ObjectGuidGenerator& GetGenerator()
        {
            static_assert(ObjectGuidTraits<type>::Global, "Only global guid can be generated in ObjectMgr context");
            return GetGuidSequenceGenerator(type);
        }

        uint32 GenerateAuctionID();
        uint64 GenerateEquipmentSetGuid();
        uint32 GenerateMailID();
        uint32 GeneratePetNumber();
        ObjectGuid::LowType GenerateCreatureSpawnId();
        ObjectGuid::LowType GenerateGameObjectSpawnId();

        SpawnGroupTemplateData const* GetSpawnGroupData(uint32 groupId) const { auto it = _spawnGroupDataStore.find(groupId); return it != _spawnGroupDataStore.end() ? &it->second : nullptr; }
        SpawnGroupTemplateData const* GetSpawnGroupData(SpawnObjectType type, ObjectGuid::LowType spawnId) const { SpawnMetadata const* data = GetSpawnMetadata(type, spawnId); return data ? data->spawnGroupData : nullptr; }
        SpawnGroupTemplateData const* GetDefaultSpawnGroup() const { return &_spawnGroupDataStore.at(0); }
        SpawnGroupTemplateData const* GetLegacySpawnGroup() const { return &_spawnGroupDataStore.at(1); }
        Trinity::IteratorPair<SpawnGroupLinkContainer::const_iterator> GetSpawnMetadataForGroup(uint32 groupId) const { return Trinity::Containers::MapEqualRange(_spawnGroupMapStore, groupId); }
        std::vector<InstanceSpawnGroupInfo> const* GetSpawnGroupsForInstance(uint32 instanceId) const { auto it = _instanceSpawnGroupStore.find(instanceId); return it != _instanceSpawnGroupStore.end() ? &it->second : nullptr; }

        MailLevelReward const* GetMailLevelReward(uint32 level, uint32 raceMask) const
        {
            MailLevelRewardContainer::const_iterator map_itr = _mailLevelRewardStore.find(level);
            if (map_itr == _mailLevelRewardStore.end())
                return nullptr;

            for (MailLevelRewardList::const_iterator set_itr = map_itr->second.begin(); set_itr != map_itr->second.end(); ++set_itr)
                if (set_itr->raceMask & raceMask)
                    return &*set_itr;

            return nullptr;
        }

        CellObjectGuids const* GetCellObjectGuids(uint16 mapid, uint8 spawnMode, uint32 cell_id);

        CellObjectGuidsMap const* GetMapObjectGuids(uint16 mapid, uint8 spawnMode);

        /**
         * Gets temp summon data for all creatures of specified group.
         *
         * @param summonerId   Summoner's entry.
         * @param summonerType Summoner's type, see SummonerType for available types.
         * @param group        Id of required group.
         *
         * @return null if group was not found, otherwise reference to the creature group data
         */
        std::vector<TempSummonData> const* GetSummonGroup(uint32 summonerId, SummonerType summonerType, uint8 group) const
        {
            TempSummonDataContainer::const_iterator itr = _tempSummonDataStore.find(TempSummonGroupKey(summonerId, summonerType, group));
            if (itr != _tempSummonDataStore.end())
                return &itr->second;

            return nullptr;
        }

        BroadcastText const* GetBroadcastText(uint32 id) const
        {
            BroadcastTextContainer::const_iterator itr = _broadcastTextStore.find(id);
            if (itr != _broadcastTextStore.end())
                return &itr->second;
            return nullptr;
        }

        SpawnMetadata const* GetSpawnMetadata(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            if (SpawnData::TypeHasData(type))
                return GetSpawnData(type, spawnId);
            else
                return nullptr;
        }

        SpawnData const* GetSpawnData(SpawnObjectType type, ObjectGuid::LowType spawnId) const
        {
            if (!SpawnData::TypeHasData(type))
                return nullptr;
            switch (type)
            {
                case SPAWN_TYPE_CREATURE:
                    return GetCreatureData(spawnId);
                case SPAWN_TYPE_GAMEOBJECT:
                    return GetGameObjectData(spawnId);
                default:
                    ABORT_MSG("Invalid spawn object type %u", uint32(type));
                    return nullptr;
            }
        }
        void OnDeleteSpawnData(SpawnData const* data);
        CreatureDataContainer const& GetAllCreatureData() const { return _creatureDataStore; }
        CreatureData const* GetCreatureData(ObjectGuid::LowType spawnId) const
        {
            CreatureDataContainer::const_iterator itr = _creatureDataStore.find(spawnId);
            if (itr == _creatureDataStore.end()) return nullptr;
            return &itr->second;
        }
        CreatureData& NewOrExistCreatureData(ObjectGuid::LowType spawnId) { return _creatureDataStore[spawnId]; }
        void DeleteCreatureData(ObjectGuid::LowType spawnId);
        ObjectGuid GetLinkedRespawnGuid(ObjectGuid spawnId) const
        {
            LinkedRespawnContainer::const_iterator itr = _linkedRespawnStore.find(spawnId);
            if (itr == _linkedRespawnStore.end()) return ObjectGuid::Empty;
            return itr->second;
        }
        CreatureLocale const* GetCreatureLocale(uint32 entry) const
        {
            CreatureLocaleContainer::const_iterator itr = _creatureLocaleStore.find(entry);
            if (itr == _creatureLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        GameObjectDataContainer const& GetAllGameObjectData() const { return _gameObjectDataStore; }
        GameObjectData const* GetGameObjectData(ObjectGuid::LowType spawnId) const
        {
            GameObjectDataContainer::const_iterator itr = _gameObjectDataStore.find(spawnId);
            if (itr == _gameObjectDataStore.end()) return nullptr;
            return &itr->second;
        }
        GameObjectData& NewOrExistGameObjectData(ObjectGuid::LowType spawnId) { return _gameObjectDataStore[spawnId]; }
        void DeleteGameObjectData(ObjectGuid::LowType spawnId);
        GameObjectLocale const* GetGameObjectLocale(uint32 entry) const
        {
            GameObjectLocaleContainer::const_iterator itr = _gameObjectLocaleStore.find(entry);
            if (itr == _gameObjectLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        ItemLocale const* GetItemLocale(uint32 entry) const
        {
            ItemLocaleContainer::const_iterator itr = _itemLocaleStore.find(entry);
            if (itr == _itemLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        ItemSetNameLocale const* GetItemSetNameLocale(uint32 entry) const
        {
            ItemSetNameLocaleContainer::const_iterator itr = _itemSetNameLocaleStore.find(entry);
            if (itr == _itemSetNameLocaleStore.end())return nullptr;
            return &itr->second;
        }
        QuestLocale const* GetQuestLocale(uint32 entry) const
        {
            QuestLocaleContainer::const_iterator itr = _questLocaleStore.find(entry);
            if (itr == _questLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        QuestOfferRewardLocale const* GetQuestOfferRewardLocale(uint32 entry) const
        {
            auto itr = _questOfferRewardLocaleStore.find(entry);
            if (itr == _questOfferRewardLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        QuestRequestItemsLocale const* GetQuestRequestItemsLocale(uint32 entry) const
        {
            auto itr = _questRequestItemsLocaleStore.find(entry);
            if (itr == _questRequestItemsLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        NpcTextLocale const* GetNpcTextLocale(uint32 entry) const
        {
            NpcTextLocaleContainer::const_iterator itr = _npcTextLocaleStore.find(entry);
            if (itr == _npcTextLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        PageTextLocale const* GetPageTextLocale(uint32 entry) const
        {
            PageTextLocaleContainer::const_iterator itr = _pageTextLocaleStore.find(entry);
            if (itr == _pageTextLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        GossipMenuItemsLocale const* GetGossipMenuItemsLocale(uint32 menuId, uint32 optionId) const
        {
            auto itr = _gossipMenuItemsLocaleStore.find(std::make_pair(menuId, optionId));
            if (itr == _gossipMenuItemsLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        PointOfInterestLocale const* GetPointOfInterestLocale(uint32 id) const
        {
            PointOfInterestLocaleContainer::const_iterator itr = _pointOfInterestLocaleStore.find(id);
            if (itr == _pointOfInterestLocaleStore.end()) return nullptr;
            return &itr->second;
        }
        QuestGreetingLocale const* GetQuestGreetingLocale(uint32 id) const
        {
            QuestGreetingLocaleContainer::const_iterator itr = _questGreetingLocaleStore.find(id);
            if (itr == _questGreetingLocaleStore.end()) return nullptr;
            return &itr->second;
        }

        TrinityString const* GetTrinityString(uint32 entry) const
        {
            TrinityStringContainer::const_iterator itr = _trinityStringStore.find(entry);
            if (itr == _trinityStringStore.end())
                return nullptr;
            return &itr->second;
        }
        char const* GetTrinityString(uint32 entry, LocaleConstant locale) const;
        char const* GetTrinityStringForDBCLocale(uint32 entry) const { return GetTrinityString(entry, DBCLocaleIndex); }
        LocaleConstant GetDBCLocaleIndex() const { return DBCLocaleIndex; }
        void SetDBCLocaleIndex(LocaleConstant locale) { DBCLocaleIndex = locale; }

        // grid objects
        void AddCreatureToGrid(ObjectGuid::LowType guid, CreatureData const* data);
        void RemoveCreatureFromGrid(ObjectGuid::LowType guid, CreatureData const* data);
        void AddGameobjectToGrid(ObjectGuid::LowType guid, GameObjectData const* data);
        void RemoveGameobjectFromGrid(ObjectGuid::LowType guid, GameObjectData const* data);
        ObjectGuid::LowType AddGameObjectData(uint32 entry, uint32 map, Position const& pos, QuaternionData const& rot, uint32 spawntimedelay = 0);
        ObjectGuid::LowType AddCreatureData(uint32 entry, uint32 map, Position const& pos, uint32 spawntimedelay = 0);

        // reserved names
        void LoadReservedPlayersNames();
        bool IsReservedName(std::string_view name) const;

        // name with valid structure and symbols
        static ResponseCodes CheckPlayerName(std::string_view name, LocaleConstant locale, bool create = false);
        static PetNameInvalidReason CheckPetName(std::string_view name, LocaleConstant locale);
        static bool IsValidCharterName(std::string_view name);

        static bool CheckDeclinedNames(const std::wstring& w_ownname, DeclinedName const& names);

        GameTele const* GetGameTele(uint32 id) const
        {
            GameTeleContainer::const_iterator itr = _gameTeleStore.find(id);
            if (itr == _gameTeleStore.end()) return nullptr;
            return &itr->second;
        }
        GameTele const* GetGameTele(std::string_view name) const;
        GameTele const* GetGameTeleExactName(std::string_view name) const;
        GameTeleContainer const& GetGameTeleMap() const { return _gameTeleStore; }
        bool AddGameTele(GameTele& data);
        bool DeleteGameTele(std::string_view name);

        Trainer::Trainer const* GetTrainer(uint32 creatureId) const;
        std::vector<Trainer::Trainer const*> const& GetClassTrainers(uint8 classId) const { return _classTrainers.at(classId); }

        VendorItemData const* GetNpcVendorItemList(uint32 entry) const
        {
            CacheVendorItemContainer::const_iterator iter = _cacheVendorItemStore.find(entry);
            if (iter == _cacheVendorItemStore.end())
                return nullptr;

            return &iter->second;
        }
        void AddVendorItem(uint32 entry, uint32 item, int32 maxcount, uint32 incrtime, uint32 extendedCost, bool persist = true); // for event
        bool RemoveVendorItem(uint32 entry, uint32 item, bool persist = true); // for event
        bool IsVendorItemValid(uint32 vendor_entry, uint32 item, int32 maxcount, uint32 ptime, uint32 ExtendedCost, Player* player = nullptr, std::set<uint32>* skip_vendors = nullptr, uint32 ORnpcflag = 0) const;

        void LoadScriptNames();
        ScriptNameContainer const& GetAllScriptNames() const;
        std::string const& GetScriptName(uint32 id) const;
        uint32 GetScriptId(std::string const& name);

        Trinity::IteratorPair<SpellClickInfoContainer::const_iterator> GetSpellClickInfoMapBounds(uint32 creature_id) const
        {
            return Trinity::Containers::MapEqualRange(_spellClickInfoStore, creature_id);
        }

        GossipMenusMapBounds GetGossipMenusMapBounds(uint32 uiMenuId) const
        {
            return _gossipMenusStore.equal_range(uiMenuId);
        }

        GossipMenusMapBoundsNonConst GetGossipMenusMapBoundsNonConst(uint32 uiMenuId)
        {
            return _gossipMenusStore.equal_range(uiMenuId);
        }

        GossipMenuItemsMapBounds GetGossipMenuItemsMapBounds(uint32 uiMenuId) const
        {
            return _gossipMenuItemsStore.equal_range(uiMenuId);
        }
        GossipMenuItemsMapBoundsNonConst GetGossipMenuItemsMapBoundsNonConst(uint32 uiMenuId)
        {
            return _gossipMenuItemsStore.equal_range(uiMenuId);
        }

        // for wintergrasp only
        GraveyardContainer GraveyardStore;

        static void AddLocaleString(std::string&& value, LocaleConstant localeConstant, std::vector<std::string>& data);
        static std::string_view GetLocaleString(std::vector<std::string> const& data, size_t locale)
        {
            if (locale < data.size())
                return data[locale];
            else
                return {};
        }
        static void GetLocaleString(std::vector<std::string> const& data, LocaleConstant localeConstant, std::string& value)
        {
            if (std::string_view str = GetLocaleString(data, static_cast<size_t>(localeConstant)); !str.empty())
                value.assign(str);
        }

        CharacterConversionMap FactionChangeAchievements;
        CharacterConversionMap FactionChangeItems;
        CharacterConversionMap FactionChangeQuests;
        CharacterConversionMap FactionChangeReputation;
        CharacterConversionMap FactionChangeSpells;
        CharacterConversionMap FactionChangeTitles;

        void LoadFactionChangeAchievements();
        void LoadFactionChangeItems();
        void LoadFactionChangeQuests();
        void LoadFactionChangeReputations();
        void LoadFactionChangeSpells();
        void LoadFactionChangeTitles();

        bool IsTransportMap(uint32 mapId) const { return _transportMaps.count(mapId) != 0; }

        VehicleSeatAddon const* GetVehicleSeatAddon(uint32 seatId) const
        {
            VehicleSeatAddonContainer::const_iterator itr = _vehicleSeatAddonStore.find(seatId);
            if (itr == _vehicleSeatAddonStore.end())
                return nullptr;

            return &itr->second;
        }

    private:
        // ========== ID生成器相关成员变量 ==========
        uint32 _auctionId;                      ///< 下一个拍卖ID
        uint64 _equipmentSetGuid;               ///< 下一个装备集GUID
        std::atomic<uint32> _mailId;            ///< 下一个邮件ID（原子操作）
        std::atomic<uint32> _hiPetNumber;       ///< 下一个宠物编号（原子操作）

        ObjectGuid::LowType _creatureSpawnId;   ///< 下一个生物刷新ID
        ObjectGuid::LowType _gameObjectSpawnId; ///< 下一个游戏对象刷新ID

        /**
         * @brief 获取指定类型的GUID序列生成器
         * @param high GUID高位类型
         * @return GUID生成器引用
         */
        ObjectGuidGenerator& GetGuidSequenceGenerator(HighGuid high);

        std::map<HighGuid, std::unique_ptr<ObjectGuidGenerator>> _guidGenerators;  ///< GUID生成器映射

        // ========== 核心模板数据容器 ==========
        QuestContainer _questTemplates;         ///< 任务模板容器（任务ID -> 任务对象）

        typedef std::unordered_map<uint32, GossipText> GossipTextContainer;
        typedef std::map<uint32, uint32> QuestAreaTriggerContainer;
        typedef std::set<uint32> TavernAreaTriggerContainer;
        typedef std::set<uint32> GameObjectForQuestContainer;

        QuestAreaTriggerContainer _questAreaTriggerStore;       ///< 任务区域触发器映射
        TavernAreaTriggerContainer _tavernAreaTriggerStore;     ///< 旅馆区域触发器集合
        GameObjectForQuestContainer _gameObjectForQuestStore;   ///< 任务相关游戏对象集合
        GossipTextContainer _gossipTextStore;                   ///< 闲聊文本容器
        QuestGreetingContainer _questGreetingStore;             ///< 任务问候语容器
        AreaTriggerContainer _areaTriggerStore;                 ///< 区域触发器容器
        AreaTriggerScriptContainer _areaTriggerScriptStore;     ///< 区域触发器脚本映射
        AccessRequirementContainer _accessRequirementStore;     ///< 进入要求数据容器
        DungeonEncounterContainer _dungeonEncounterStore;       ///< 副本遭遇战容器

        // ========== 声望系统相关容器 ==========
        RepRewardRateContainer _repRewardRateStore;             ///< 声望奖励率容器
        RepOnKillContainer _repOnKillStore;                     ///< 击杀声望容器
        RepSpilloverTemplateContainer _repSpilloverTemplateStore;  ///< 声望溢出模板容器

        // ========== 闲聊和兴趣点相关容器 ==========
        GossipMenusContainer _gossipMenusStore;                 ///< 闲聊菜单容器
        GossipMenuItemsContainer _gossipMenuItemsStore;         ///< 闲聊菜单项容器
        PointOfInterestContainer _pointsOfInterestStore;        ///< 兴趣点容器

        QuestPOIContainer _questPOIStore;                       ///< 任务POI容器

        // ========== 任务关系容器 ==========
        QuestRelations _goQuestRelations;                       ///< 游戏对象任务关系（游戏对象 -> 任务）
        QuestRelations _goQuestInvolvedRelations;               ///< 游戏对象任务涉及关系
        QuestRelations _creatureQuestRelations;                 ///< 生物任务关系（生物 -> 任务）
        QuestRelations _creatureQuestInvolvedRelations;         ///< 生物任务涉及关系

        ExclusiveQuestGroups _exclusiveQuestGroups;             ///< 互斥任务组

        // ========== 玩家名称相关 ==========
        typedef std::set<std::wstring> ReservedNamesContainer;
        ReservedNamesContainer _reservedNamesStore;             ///< 保留名称集合

        GameTeleContainer _gameTeleStore;                       ///< 游戏传送点容器

        ScriptNameContainer _scriptNamesStore;                  ///< 脚本名称容器

        SpellClickInfoContainer _spellClickInfoStore;           ///< 法术点击信息容器

        SpellScriptsContainer _spellScriptsStore;               ///< 法术脚本容器

        // ========== 载具相关容器 ==========
        std::unordered_map<uint32, VehicleTemplate> _vehicleTemplateStore;      ///< 载具模板容器
        VehicleAccessoryContainer _vehicleTemplateAccessoryStore;               ///< 载具模板附件容器
        VehicleAccessoryContainer _vehicleAccessoryStore;                       ///< 载具附件容器

        LocaleConstant DBCLocaleIndex;                          ///< DBC本地化索引

        PageTextContainer _pageTextStore;                       ///< 页面文本容器
        InstanceTemplateContainer _instanceTemplateStore;       ///< 副本模板容器

    private:
        void LoadScripts(ScriptsType type);
        void LoadQuestRelationsHelper(QuestRelations& map, std::string const& table);
        QuestRelationResult GetQuestRelationsFrom(QuestRelations const& map, uint32 key, bool onlyActive) const { return { map.equal_range(key), onlyActive }; }
        void PlayerCreateInfoAddItemHelper(uint32 race_, uint32 class_, uint32 itemId, int32 count);

        MailLevelRewardContainer _mailLevelRewardStore;        ///< 邮件等级奖励容器

        // ========== 生物基础数据容器 ==========
        CreatureBaseStatsContainer _creatureBaseStatsStore;    ///< 生物基础属性容器（等级+职业 -> 基础属性）

        typedef std::unordered_map<uint32 /*creatureId*/, std::unique_ptr<PetLevelInfo[] /*level*/>> PetLevelInfoContainer;
        PetLevelInfoContainer _petInfoStore;                   ///< 宠物等级信息容器

        std::unique_ptr<PlayerClassInfo> _playerClassInfo[MAX_CLASSES];  ///< 玩家职业信息数组

        void BuildPlayerLevelInfo(uint8 race, uint8 class_, uint8 level, PlayerLevelInfo* plinfo) const;

        std::unique_ptr<PlayerInfo> _playerInfo[MAX_RACES][MAX_CLASSES];  ///< 玩家信息数组（种族 x 职业）

        typedef std::vector<uint32> PlayerXPperLevel;           ///< 玩家每级经验值类型
        PlayerXPperLevel _playerXPperLevel;                     ///< 玩家每级经验值表

        typedef std::map<uint32, uint32> BaseXPContainer;       ///< 基础经验值容器类型
        BaseXPContainer _baseXPTable;                           ///< 基础经验值表（区域等级 -> 基础经验）

        typedef std::map<uint32, int32> FishingBaseSkillContainer;  ///< 钓鱼基础技能容器类型
        FishingBaseSkillContainer _fishingBaseForAreaStore;     ///< 各区域钓鱼基础技能等级

        typedef std::map<uint32, std::vector<std::string>> HalfNameContainer;
        HalfNameContainer _petHalfName0;                        ///< 宠物名称前半部分
        HalfNameContainer _petHalfName1;                        ///< 宠物名称后半部分

        typedef std::unordered_map<uint32, ItemSetNameEntry> ItemSetNameContainer;
        ItemSetNameContainer _itemSetNameStore;                 ///< 套装名称容器

        // ========== 刷怪数据容器 ==========
        MapObjectGuids _mapObjectGuidsStore;                    ///< 地图对象GUID容器
        CreatureDataContainer _creatureDataStore;               ///< 生物数据容器（刷新ID -> 生物数据）
        CreatureTemplateContainer _creatureTemplateStore;       ///< 生物模板容器（模板ID -> 生物模板）
        CreatureModelContainer _creatureModelStore;             ///< 生物模型容器
        CreatureAddonContainer _creatureAddonStore;             ///< 生物附加数据容器
        CreatureTemplateAddonContainer _creatureTemplateAddonStore;  ///< 生物模板附加数据容器
        std::unordered_map<ObjectGuid::LowType, CreatureMovementData> _creatureMovementOverrides;  ///< 生物移动覆盖数据
        GameObjectAddonContainer _gameObjectAddonStore;         ///< 游戏对象附加数据容器
        GameObjectQuestItemMap _gameObjectQuestItemStore;       ///< 游戏对象任务物品映射
        CreatureQuestItemMap _creatureQuestItemStore;           ///< 生物任务物品映射
        EquipmentInfoContainer _equipmentInfoStore;             ///< 装备信息容器
        LinkedRespawnContainer _linkedRespawnStore;             ///< 链接复活容器
        CreatureLocaleContainer _creatureLocaleStore;           ///< 生物本地化容器
        GameObjectDataContainer _gameObjectDataStore;           ///< 游戏对象数据容器
        GameObjectLocaleContainer _gameObjectLocaleStore;       ///< 游戏对象本地化容器
        GameObjectTemplateContainer _gameObjectTemplateStore;   ///< 游戏对象模板容器（模板ID -> 游戏对象模板）
        GameObjectTemplateAddonContainer _gameObjectTemplateAddonStore;  ///< 游戏对象模板附加数据容器
        GameObjectOverrideContainer _gameObjectOverrideStore;   ///< 游戏对象覆盖数据容器
        SpawnGroupDataContainer _spawnGroupDataStore;           ///< 生成组数据容器
        SpawnGroupLinkContainer _spawnGroupMapStore;            ///< 生成组链接容器
        InstanceSpawnGroupContainer _instanceSpawnGroupStore;   ///< 副本生成组容器
        /// 存储临时召唤数据，按召唤者ID、召唤者类型和组ID分组
        TempSummonDataContainer _tempSummonDataStore;

        // ========== 本地化数据容器 ==========
        BroadcastTextContainer _broadcastTextStore;             ///< 广播文本容器
        ItemTemplateContainer _itemTemplateStore;               ///< 物品模板容器（模板ID -> 物品模板）
        ItemLocaleContainer _itemLocaleStore;                   ///< 物品本地化容器
        ItemSetNameLocaleContainer _itemSetNameLocaleStore;     ///< 套装名称本地化容器
        QuestLocaleContainer _questLocaleStore;                 ///< 任务本地化容器
        QuestOfferRewardLocaleContainer _questOfferRewardLocaleStore;  ///< 任务奖励本地化容器
        QuestRequestItemsLocaleContainer _questRequestItemsLocaleStore;  ///< 任务请求数据本地化容器

        NpcTextLocaleContainer _npcTextLocaleStore;             ///< NPC文本本地化容器
        PageTextLocaleContainer _pageTextLocaleStore;           ///< 页面文本本地化容器
        GossipMenuItemsLocaleContainer _gossipMenuItemsLocaleStore;  ///< 闲聊菜单项本地化容器
        PointOfInterestLocaleContainer _pointOfInterestLocaleStore;  ///< 兴趣点本地化容器
        QuestGreetingLocaleContainer _questGreetingLocaleStore;  ///< 任务问候语本地化容器

        TrinityStringContainer _trinityStringStore;             ///< Trinity字符串容器

        // ========== 商人和训练师相关容器 ==========
        CacheVendorItemContainer _cacheVendorItemStore;         ///< 商人物品缓存容器
        std::unordered_map<uint32, Trainer::Trainer> _trainers;  ///< 训练师容器
        std::unordered_map<uint8, std::vector<Trainer::Trainer const*>> _classTrainers;  ///< 职业训练师容器
        std::unordered_map<uint32, Trainer::Trainer const*> _creatureDefaultTrainers;  ///< 生物默认训练师映射

        // ========== 难度相关容器 ==========
        std::set<uint32> _difficultyEntries[MAX_DIFFICULTY - 1];  ///< 已加载的难度1值生物条目
        std::set<uint32> _hasDifficultyEntries[MAX_DIFFICULTY - 1];  ///< 已加载具有难度1值的生物

        std::set<uint32> _transportMaps;                        ///< 运输工具地图ID集合

        PlayerTotemModelMap _playerTotemModel;                  ///< 玩家图腾模型映射
        VehicleSeatAddonContainer _vehicleSeatAddonStore;       ///< 载具座位附加数据容器
};

#define sObjectMgr ObjectMgr::instance()

#endif
