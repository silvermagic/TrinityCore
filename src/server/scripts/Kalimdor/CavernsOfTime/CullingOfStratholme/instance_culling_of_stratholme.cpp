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
 * @file instance_culling_of_stratholme.cpp
 * @brief 斯坦索姆的抉择副本 - 实例脚本
 *
 * 本模块实现斯坦索姆抉择副本的核心实例管理逻辑。这是一个线性剧情副本，
 * 玩家通过多个阶段完成整个副本流程，每个阶段都有特定的事件和进度状态。
 *
 * 主要功能：
 * - 实例进度状态管理（瘟疫箱子 -> 乌瑟尔对话 -> 净化开始 -> 天灾波次 -> 城镇大厅 -> 通道战斗 -> 玛尔加尼斯）
 * - 天灾波次生成和管理
 * - 英雄难度限时挑战（无限腐蚀者）
 * - 世界状态同步（UI显示）
 * - 阿萨斯NPC的状态管理
 * - 实例数据保存和加载
 *
 * 副本流程概述：
 * 1. 发现5个瘟疫箱子
 * 2. 乌瑟尔和阿萨斯的对话
 * 3. 开始净化斯坦索姆（10波天灾军团）
 * 4. 进入城镇大厅
 * 5. 通过隐藏通道
 * 6. 击败玛尔加尼斯
 *
 * @note 英雄难度下，在第3波开始时会刷新无限腐蚀者，玩家需在25分钟内击败他
 */

#include "culling_of_stratholme.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "CreatureTextMgr.h"
#include "EventMap.h"
#include "GameObject.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "WorldStatePackets.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/*
 *  斯坦索姆抉择副本Boss列表：
 *  0 - 肉钩（Meathook） - 第5波Boss
 *  1 - 萨尔拉玛·血肉塑造者（Salramm the Fleshcrafter） - 第10波Boss
 *  2 - 时空领主·艾波克（Chrono-Lord Epoch） - 城镇大厅Boss
 *  3 - 玛尔加尼斯（Mal'Ganis） - 最终Boss
 *  4 - 无限腐蚀者（Infinite Corruptor） - 英雄难度限时挑战Boss
 */

/**
 * @brief 实例事件ID枚举
 */
enum COSEvents
{
    EVENT_GUARDIAN_TICK = 1,        // 时间守护者计时器更新（用于无限腐蚀者挑战）
    EVENT_RESPAWN_ARTHAS,           // 重生阿萨斯
    EVENT_CRIER_CALL_TO_GATES,      // 城镇传令官呼叫玩家前往城门
    EVENT_SCOURGE_WAVE,             // 生成天灾波次
    EVENT_CRIER_ANNOUNCE_WAVE       // 传令官宣布波次位置
};

/**
 * @brief 副本相关NPC和游戏对象条目ID枚举
 */
enum COSEntries
{
    NPC_GENERIC_BUNNY        = 28960,  // 通用兔怪（用于法术触发等）
    NPC_CRATE_HELPER         = 27827,  // 箱子辅助NPC
    NPC_CHROMIE              = 26527,  // 克罗米 - 第一个克罗米（副本入口处）
    NPC_INFINITE_CORRUPTOR   = 32273,  // 无限腐蚀者
    NPC_GUARDIAN_OF_TIME     = 32281,  // 时间守护者
    NPC_TIME_RIFT            = 28409,  // 时间裂隙
    NPC_LORDAERON_CRIER      = 27913,  // 洛丹伦传令官
    NPC_DEVOURING_GHOUL      = 28249,  // 吞噬食尸鬼
    NPC_ENRAGED_GHOUL        = 27729,  // 狂怒食尸鬼
    NPC_NECROMANCER          = 28200,  // 死灵法师
    NPC_CRYPT_FIEND          = 27734,  // 地穴恶魔
    NPC_ACOLYTE              = 27731,  // 侍僧
    NPC_CRYPT_STALKER        = 28199,  // 地穴潜行者
    NPC_ABOMINATION          = 27736,  // 憎恶
    NPC_MEATHOOK             = 26529,  // 肉钩（Boss）
    NPC_SALRAMM              = 26530,  // 萨尔拉玛（Boss）

    GO_MALGANIS_GATE_2       = 187723, // 玛尔加尼斯大门
    GO_EXIT_GATE             = 191788, // 出口大门

    SPELL_CRATES_KILL_CREDIT = 58109   // 箱子完成击杀信用
};

/**
 * @brief 台词ID枚举
 */
enum COSYells
{
    CRIER_SAY_CALL_TO_GATES    = 0,  // 传令官呼叫前往城门
    CRIER_SAY_KINGS_SQUARE     = 1,  // 国王广场
    CRIER_SAY_MARKET_ROW       = 2,  // 市集街
    CRIER_SAY_FESTIVAL_LANE    = 3,  // 节日巷道
    CRIER_SAY_ELDERS_SQUARE    = 4,  // 长老广场
    CRIER_SAY_TOWN_HALL        = 5,  // 城镇大厅

    CHROMIE_WHISPER_GUARDIAN_1 = 0,  // 克罗米低语1（25分钟时）
    CHROMIE_WHISPER_GUARDIAN_2 = 1,  // 克罗米低语2（5分钟时）
    CHROMIE_WHISPER_GUARDIAN_3 = 2   // 克罗米低语3（最后1分钟）
};

/**
 * @brief 世界状态ID枚举（用于客户端UI显示）
 */
enum COSWorldStates
{
    WORLDSTATE_SHOW_CRATES        = 3479, // 显示箱子计数器
    WORLDSTATE_CRATES_REVEALED    = 3480, // 已发现的箱子数量
    WORLDSTATE_WAVE_COUNT         = 3504, // 当前波次计数
    WORLDSTATE_WAVE_MARKER_ES     = 3581, // 波次标记：长老广场
    WORLDSTATE_WAVE_MARKER_FL     = 3582, // 波次标记：节日巷道
    WORLDSTATE_WAVE_MARKER_KS     = 3583, // 波次标记：国王广场
    WORLDSTATE_WAVE_MARKER_MR     = 3584, // 波次标记：市集街
    WORLDSTATE_WAVE_MARKER_TH     = 3585, // 波次标记：城镇大厅
    WORLDSTATE_TIME_GUARDIAN      = 3931, // 时间守护者剩余时间（分钟）
    WORLDSTATE_TIME_GUARDIAN_SHOW = 3932  // 是否显示时间守护者计时器
};

/**
 * @brief 波次生成位置枚举
 */
enum COSWaveLocations
{
    WAVE_LOC_MIN = CRIER_SAY_KINGS_SQUARE,   // 最小位置ID
    WAVE_LOC_MAX = CRIER_SAY_TOWN_HALL,       // 最大位置ID
    WAVE_MARKER_MIN = WORLDSTATE_WAVE_MARKER_ES, // 最小世界状态标记
    WAVE_MARKER_MAX = WORLDSTATE_WAVE_MARKER_TH  // 最大世界状态标记
};

/**
 * @brief 杂项常量枚举
 */
enum COSMisc
{
    NUM_PLAGUE_CRATES   = 5,   // 瘟疫箱子总数
    NUM_SCOURGE_WAVES   = 10,  // 天灾波次总数
    MAX_SPAWNS_PER_WAVE = 6,   // 每波最大生成数量
    WAVE_MEATHOOK       = 5,   // 肉钩出现的波次
    WAVE_SALRAMM        = 10   // 萨尔拉玛出现的波次
};

/**
 * @brief 门数据配置
 *
 * 定义副本中门的开启/关闭逻辑
 * 格式：{ 游戏对象ID, Boss数据ID, 门类型 }
 */
DoorData const doorData[] =
{
    { GO_MALGANIS_GATE_2, DATA_MAL_GANIS, DOOR_TYPE_ROOM },    // 玛尔加尼斯房间门
    { GO_EXIT_GATE,       DATA_MAL_GANIS, DOOR_TYPE_PASSAGE }, // 出口通道门
    { 0,                  0,              DOOR_TYPE_ROOM } // 结束标记
};

/**
 * @brief 获取稳定状态
 * @param state 当前进度状态
 * @return 对应的稳定状态
 *
 * 当实例需要回退时，将当前状态映射到上一个稳定状态点。
 * 稳定状态是可以安全重置和恢复的检查点。
 *
 * 状态映射规则：
 * - JUST_STARTED -> JUST_STARTED（起点）
 * - CRATES_IN_PROGRESS -> CRATES_IN_PROGRESS（箱子阶段）
 * - CRATES_DONE -> CRATES_DONE（箱子完成）
 * - UTHER_TALK到WAVES_IN_PROGRESS -> PURGE_PENDING（净化准备）
 * - WAVES_DONE到TOWN_HALL -> TOWN_HALL_PENDING（城镇大厅准备）
 * - TOWN_HALL_COMPLETE到GAUNTLET_IN_PROGRESS -> GAUNTLET_PENDING（通道准备）
 * - GAUNTLET_COMPLETE到MALGANIS_IN_PROGRESS -> GAUNTLET_COMPLETE（通道完成）
 * - COMPLETE -> COMPLETE（完成）
 */
COSProgressStates GetStableStateFor(COSProgressStates const state)
{
    switch (state)
    {
        case JUST_STARTED:
        default:
            return JUST_STARTED;
        case CRATES_IN_PROGRESS:
            return CRATES_IN_PROGRESS;
        case CRATES_DONE:
            return CRATES_DONE;
        case UTHER_TALK:
        case PURGE_PENDING:
        case PURGE_STARTING:
        case WAVES_IN_PROGRESS:
            return PURGE_PENDING;
        case WAVES_DONE:
        case TOWN_HALL_PENDING:
        case TOWN_HALL:
            return TOWN_HALL_PENDING;
        case TOWN_HALL_COMPLETE:
        case GAUNTLET_TRANSITION:
        case GAUNTLET_PENDING:
        case GAUNTLET_IN_PROGRESS:
            return GAUNTLET_PENDING;
        case GAUNTLET_COMPLETE:
        case MALGANIS_IN_PROGRESS:
            return GAUNTLET_COMPLETE;
        case COMPLETE:
            return COMPLETE;
    }
}

// 无限腐蚀者、时间守护者和时间裂隙的刷新位置
static Position const CorruptorPos = { 2331.642f, 1273.273f, 132.9524f, 3.717551f };      // 无限腐蚀者位置
static Position const GuardianPos = { 2321.489f, 1268.383f, 132.8507f, 0.418879f };       // 时间守护者位置
static Position const CorruptorRiftPos = { 2443.626f, 1280.450f, 133.0066f, 1.727876f };  // 时间裂隙位置

/**
 * @brief 英雄难度波次配置
 *
 * 定义英雄难度下每波天灾军团的怪物组合。
 * 外层数组索引为波次编号（0-9对应第1-10波），内层数组为该波的怪物条目ID。
 *
 * 波次说明：
 * - 第1波：3只吞噬食尸鬼
 * - 第2波：吞噬食尸鬼、狂怒食尸鬼、死灵法师
 * - 第3波：吞噬食尸鬼、狂怒食尸鬼、死灵法师、地穴恶魔
 * - 第4波：死灵法师、地穴恶魔、4只侍僧
 * - 第5波：肉钩Boss（特殊波）
 * - 第6波：吞噬食尸鬼、死灵法师、地穴恶魔、地穴潜行者
 * - 第7波：吞噬食尸鬼、2只狂怒食尸鬼、憎恶
 * - 第8波：吞噬食尸鬼、狂怒食尸鬼、死灵法师、憎恶
 * - 第9波：吞噬食尸鬼、死灵法师、地穴恶魔、憎恶
 * - 第10波：萨尔拉玛Boss（特殊波）
 */
static std::array<std::array<uint32, MAX_SPAWNS_PER_WAVE>, NUM_SCOURGE_WAVES> const HeroicWaves =
{
    {
        { { NPC_DEVOURING_GHOUL, NPC_DEVOURING_GHOUL, NPC_DEVOURING_GHOUL                                      } }, // wave 1
        { { NPC_DEVOURING_GHOUL, NPC_ENRAGED_GHOUL,   NPC_NECROMANCER                                          } }, // wave 2
        { { NPC_DEVOURING_GHOUL, NPC_ENRAGED_GHOUL,   NPC_NECROMANCER,   NPC_CRYPT_FIEND                       } }, // wave 3
        { { NPC_NECROMANCER,     NPC_CRYPT_FIEND,     NPC_ACOLYTE,       NPC_ACOLYTE, NPC_ACOLYTE, NPC_ACOLYTE } }, // wave 4
        { { 0                                                                                                  } }, // wave 5, meathook (special)
        { { NPC_DEVOURING_GHOUL, NPC_NECROMANCER,     NPC_CRYPT_FIEND,   NPC_CRYPT_STALKER                     } }, // wave 6
        { { NPC_DEVOURING_GHOUL, NPC_ENRAGED_GHOUL,   NPC_ENRAGED_GHOUL, NPC_ABOMINATION                       } }, // wave 7
        { { NPC_DEVOURING_GHOUL, NPC_ENRAGED_GHOUL,   NPC_NECROMANCER,   NPC_ABOMINATION                       } }, // wave 8
        { { NPC_DEVOURING_GHOUL, NPC_NECROMANCER,     NPC_CRYPT_FIEND,   NPC_ABOMINATION                       } }, // wave 9
        { { 0                                                                                                  } } // wave 10, salramm (special)
    }
};

/**
 * @brief 波次生成位置结构体
 *
 * 定义单个波次生成点的信息
 */
struct WaveLocation
{
    COSWorldStates const WorldState;                         // 对应的世界状态标记
    std::array<Position, MAX_SPAWNS_PER_WAVE> SpawnPoints;   // 生成点坐标数组
};

/**
 * @brief 所有波次生成位置配置
 *
 * 定义5个不同的波次生成区域，每个区域有6个生成点。
 * 区域包括：
 * 0 - 国王广场（King's Square）
 * 1 - 市集街（Market Row）
 * 2 - 节日巷道（Festival Lane）
 * 3 - 长老广场（Elders' Square）
 * 4 - 城镇大厅（Town Hall）
 */
static const std::array<WaveLocation, WAVE_LOC_MAX - WAVE_LOC_MIN + 1> WaveLocations =
{
    {
        { // King's Square
            WORLDSTATE_WAVE_MARKER_KS,
            {
                {
                    { 2131.474f, 1352.615f, 131.372f, 6.10960f },
                    { 2131.463f, 1357.127f, 131.587f, 5.95173f },
                    { 2129.795f, 1345.093f, 131.194f, 0.17905f },
                    { 2136.235f, 1347.894f, 131.628f, 0.20262f },
                    { 2138.219f, 1356.240f, 132.169f, 5.95173f },
                    { 2140.584f, 1351.624f, 132.142f, 6.08525f }
                }
            }
        },
        { // Market Row
            WORLDSTATE_WAVE_MARKER_MR,
            {
                {
                    { 2226.364f, 1331.808f, 127.0193f, 3.298672f },
                    { 2229.934f, 1329.146f, 127.057f,  3.24605f },
                    { 2225.028f, 1327.269f, 127.791f,  3.03792f },
                    { 2223.844f, 1335.282f, 127.749f,  3.47774f },
                    { 2222.192f, 1330.859f, 127.526f,  3.18793f },
                    { 2225.865f, 1331.029f, 127.007f,  3.18793f }
                }
            }
        },
        { // Festival Lane
            WORLDSTATE_WAVE_MARKER_FL,
            {
                {
                    { 2183.596f, 1238.823f, 136.551f, 2.16377f },
                    { 2181.420f, 1237.357f, 136.565f, 2.16377f },
                    { 2178.692f, 1237.446f, 136.694f, 1.99098f },
                    { 2184.980f, 1242.458f, 136.772f, 2.59181f },
                    { 2176.873f, 1240.463f, 136.420f, 2.10094f },
                    { 2181.523f, 1244.298f, 136.338f, 2.38997f }
                }
            }
        },
        { // Elders' Square
            WORLDSTATE_WAVE_MARKER_ES,
            {
                {
                    { 2267.003f, 1168.055f, 137.821f, 2.79050f },
                    { 2264.392f, 1162.145f, 137.910f, 2.39937f },
                    { 2262.785f, 1166.648f, 138.053f, 2.71353f },
                    { 2265.214f, 1170.771f, 137.972f, 2.80385f },
                    { 2259.745f, 1159.360f, 138.198f, 2.34047f },
                    { 2264.222f, 1171.708f, 138.047f, 2.82742f }
                }
            }
        },
        { // Town Hall
            WORLDSTATE_WAVE_MARKER_TH,
            {
                {
                    { 2351.656f, 1218.682f, 130.062f, 4.63383f },
                    { 2354.921f, 1218.425f, 130.280f, 4.63383f },
                    { 2347.516f, 1216.976f, 130.491f, 5.02496f },
                    { 2356.508f, 1216.656f, 130.445f, 4.29061f },
                    { 2346.674f, 1216.739f, 130.576f, 5.32341f },
                    { 2351.728f, 1214.561f, 130.255f, 4.61891f }
                }
            }
        }
    }
};

/**
 * @brief 斯坦索姆抉择实例脚本类
 *
 * 继承自InstanceMapScript，负责管理整个副本的实例数据和逻辑。
 * 这是副本的核心管理类，控制所有进度、事件和状态转换。
 */
class instance_culling_of_stratholme : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化实例脚本，地图ID为595（斯坦索姆抉择）
         */
        instance_culling_of_stratholme() : InstanceMapScript("instance_culling_of_stratholme", 595) { }

        /**
         * @brief 斯坦索姆抉择实例脚本实现类
         *
         * 继承自InstanceScript，实现副本的核心管理逻辑。
         * 主要职责：
         * - 管理副本进度状态
         * - 处理天灾波次生成
         * - 管理无限腐蚀者限时挑战
         * - 同步世界状态到客户端
         * - 管理NPC和游戏对象
         * - 保存和加载实例数据
         */
        struct instance_culling_of_stratholme_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * 初始化实例脚本，设置所有成员变量的初始值
             */
            instance_culling_of_stratholme_InstanceMapScript(InstanceMap* map) : InstanceScript(map), _currentState(JUST_STARTED), _infiniteGuardianTimeout(0), _waveCount(0), _currentSpawnLoc(0)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadDoorData(doorData);

                _currentWorldStates[WORLDSTATE_SHOW_CRATES] = _currentWorldStates[WORLDSTATE_CRATES_REVEALED] = _currentWorldStates[WORLDSTATE_WAVE_COUNT] = _currentWorldStates[WORLDSTATE_TIME_GUARDIAN_SHOW] = _currentWorldStates[WORLDSTATE_TIME_GUARDIAN] = 0;
                _sentWorldStates = _currentWorldStates;
                _plagueCrates.reserve(NUM_PLAGUE_CRATES);
            }

            /**
             * @brief 填充初始世界状态
             * @param packet 世界状态初始化数据包
             *
             * 调用时机：新玩家进入副本时
             *
             * 执行操作：将当前世界状态打包发送给客户端
             */
            void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override
            {
                for (WorldStateMap::const_iterator itr = _sentWorldStates.begin(); itr != _sentWorldStates.end(); ++itr)
                    packet.Worldstates.emplace_back(itr->first, itr->second);
            }

            /**
             * @brief 写入保存数据
             * @param data 输出字符串流
             *
             * 调用时机：实例需要保存时
             *
             * 保存内容：
             * - 当前进度状态
             * - 无限守护者超时时间戳
             */
            void WriteSaveDataMore(std::ostringstream& data) override
            {
                data << _currentState << ' ' << _infiniteGuardianTimeout;
            }

            /**
             * @brief 读取保存数据
             * @param data 输入字符串流
             *
             * 调用时机：加载已保存的实例时
             *
             * 加载逻辑：
             * 1. 读取保存的状态和超时时间戳
             * 2. 回退到上一个稳定状态（防止卡死）
             * 3. 如果有守护者计时，启动计时器
             * 4. 记录调试日志
             *
             * @note 回退到稳定状态是为了确保副本可以正常继续，避免中间状态导致卡死
             */
            void ReadSaveDataMore(std::istringstream& data) override
            {
                // 从保存数据读取当前实例进度，然后回退到上一个稳定状态
                uint32 state = JUST_STARTED;
                time_t infiniteGuardianTime = 0;
                data >> state;
                data >> infiniteGuardianTime; // UNIX时间戳

                COSProgressStates loadState = GetStableStateFor(COSProgressStates(state));
                SetInstanceProgress(loadState, true);

                if (infiniteGuardianTime)
                {
                    _infiniteGuardianTimeout = infiniteGuardianTime;
                    events.ScheduleEvent(EVENT_GUARDIAN_TICK, 0s);
                }

                time_t timediff = (infiniteGuardianTime - GameTime::GetGameTime());
                if (!infiniteGuardianTime)
                    timediff = -1;

                TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::ReadSaveDataMore: 加载状态 {} 和守护者超时，剩余 {} 分钟 {} 秒", (uint32)loadState, timediff / MINUTE, timediff % MINUTE);
            }

            /**
             * @brief 设置数据
             * @param type 数据类型ID
             * @param data 数据值
             *
             * 调用时机：外部脚本通过instance->SetData()设置实例数据时
             *
             * 处理的数据类型：
             * - DATA_GM_OVERRIDE: GM强制覆盖进度状态
             * - DATA_ARTHAS_DIED: 阿萨斯死亡，实例失败，回退到上一个稳定状态
             * - DATA_CRATES_START: 开始箱子阶段
             * - DATA_CRATE_REVEALED: 发现箱子，更新计数
             * - DATA_UTHER_FINISHED: 乌瑟尔对话完成
             * - DATA_SKIP_TO_PURGE: 跳过前置直接开始净化
             * - DATA_START_WAVES: 开始天灾波次
             * - DATA_REACH_TOWN_HALL: 到达城镇大厅
             * - DATA_TOWN_HALL_DONE: 城镇大厅事件完成
             * - DATA_GAUNTLET_REACHED: 到达通道
             * - DATA_GAUNTLET_DONE: 通道战斗完成
             * - DATA_MALGANIS_DONE: 玛尔加尼斯战斗完成
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_GM_OVERRIDE:
                        SetInstanceProgress(COSProgressStates(data), true);
                        break;
                    case DATA_ARTHAS_DIED:
                        // 重生所有内容，然后回退到上一个稳定状态
                        _arthasGUID = ObjectGuid::Empty;
                        SetInstanceProgress(GetStableStateFor(_currentState), true);
                        break;
                    case DATA_CRATES_START:
                        if (_currentState == JUST_STARTED)
                            SetInstanceProgress(CRATES_IN_PROGRESS, false);
                        break;
                    case DATA_CRATE_REVEALED:
                        if (uint32 missingCrates = MissingPlagueCrates())
                            SetWorldState(WORLDSTATE_CRATES_REVEALED, NUM_PLAGUE_CRATES - missingCrates);
                        else
                            SetInstanceProgress(CRATES_DONE, false);
                        break;
                    case DATA_UTHER_FINISHED:
                        if (_currentState == UTHER_TALK)
                            SetInstanceProgress(PURGE_PENDING, false);
                        break;
                    case DATA_SKIP_TO_PURGE:
                        if (_currentState <= CRATES_DONE)
                            SetInstanceProgress(PURGE_PENDING, false);
                        break;
                    case DATA_START_WAVES:
                        if (_currentState == PURGE_STARTING)
                            SetInstanceProgress(WAVES_IN_PROGRESS, false);
                        break;
                    case DATA_REACH_TOWN_HALL:
                        if (_currentState == WAVES_DONE)
                            SetInstanceProgress(TOWN_HALL_PENDING, false);
                        break;
                    case DATA_TOWN_HALL_DONE:
                        if (_currentState == TOWN_HALL)
                            SetInstanceProgress(TOWN_HALL_COMPLETE, false);
                        break;
                    case DATA_GAUNTLET_REACHED:
                        if (_currentState == GAUNTLET_TRANSITION)
                            SetInstanceProgress(GAUNTLET_PENDING, false);
                        break;
                    case DATA_GAUNTLET_DONE:
                        if (_currentState == GAUNTLET_IN_PROGRESS)
                            SetInstanceProgress(GAUNTLET_COMPLETE, false);
                        break;
                    case DATA_MALGANIS_DONE:
                        if (_currentState == MALGANIS_IN_PROGRESS)
                            SetInstanceProgress(COMPLETE, false);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 单位死亡回调
             * @param unit 死亡的单位
             *
             * 调用时机：副本内任何单位死亡时
             *
             * 主要逻辑：
             * 1. 检查是否在波次进行中
             * 2. 如果是天灾波次怪物，从存活列表中移除
             * 3. 如果所有波次怪物都被击杀，进入下一波或完成波次阶段
             *
             * @note 这是波次进度推进的核心逻辑
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (_currentState != WAVES_IN_PROGRESS || _waveSpawns.empty())
                    return;

                // 如果这是波次怪物...
                auto it = _waveSpawns.find(unit->GetGUID());
                if (it == _waveSpawns.end())
                    return;

                // ...从列表中移除，然后检查是否还有存活的怪物...
                _waveSpawns.erase(it);
                if (!_waveSpawns.empty())
                    return;

                // ...如果没有了，波次完成，推进进度

                // 清除现有的世界标记
                for (uint32 marker = WAVE_MARKER_MIN; marker <= WAVE_MARKER_MAX; ++marker)
                    SetWorldState(COSWorldStates(marker), 0, false);
                PropagateWorldStateUpdate();

                // 如果适用，调度下一波
                if (_waveCount < NUM_SCOURGE_WAVES)
                    events.ScheduleEvent(EVENT_SCOURGE_WAVE, (_waveCount == WAVE_MEATHOOK) ? 20s : 1s);
                else
                    SetInstanceProgress(WAVES_DONE, false);
            }

            /**
             * @brief 设置GUID数据
             * @param type 数据类型ID
             * @param guid GUID值
             *
             * 调用时机：外部脚本通过instance->SetGuidData()设置GUID数据时
             *
             * 处理的数据类型：
             * - DATA_GM_RECALL: GM召回所有玩家到阿萨斯位置
             * - DATA_UTHER_START: 开始乌瑟尔对话
             * - DATA_START_PURGE: 开始净化事件（阿萨斯RP2）
             * - DATA_START_TOWN_HALL: 开始城镇大厅事件（阿萨斯RP3）
             * - DATA_TO_GAUNTLET: 前往通道（阿萨斯RP4-1）
             * - DATA_START_GAUNTLET: 开始通道战斗（阿萨斯RP4-2）
             * - DATA_START_MALGANIS: 开始玛尔加尼斯战斗（阿萨斯RP5）
             */
            void SetGuidData(uint32 type, ObjectGuid guid) override
            {
                switch (type)
                {
                    case DATA_GM_RECALL:
                    {
                        Creature* arthas = instance->GetCreature(_arthasGUID);
                        Position const& target = arthas ? arthas->GetPosition() : GetArthasSnapbackFor(_currentState);

                        for (auto itr = instance->GetPlayers().begin(); itr != instance->GetPlayers().end(); ++itr)
                        {
                            if (Player* player = itr->GetSource())
                                if (player->GetGUID() == guid || !player->IsGameMaster())
                                {
                                    player->CombatStop(true);
                                    const float offsetDist = 10;
                                    float myAngle = rand_norm() * 2.0 * M_PI;
                                    Position myTarget(target.GetPositionX() + std::sin(myAngle) * offsetDist, target.GetPositionY() + std::sin(myAngle) * offsetDist, target.GetPositionZ(), myAngle + M_PI);
                                    player->NearTeleportTo(myTarget);
                                }
                        }
                        break;
                    }
                    case DATA_UTHER_START:
                        if (_currentState == CRATES_DONE)
                            SetInstanceProgress(UTHER_TALK, false);
                        break;
                    case DATA_START_PURGE:
                        InitiateArthasEvent(PURGE_PENDING, PURGE_STARTING, ACTION_START_RP_EVENT2, guid);
                        break;
                    case DATA_START_TOWN_HALL:
                        InitiateArthasEvent(TOWN_HALL_PENDING, TOWN_HALL, ACTION_START_RP_EVENT3, guid);
                        break;
                    case DATA_TO_GAUNTLET:
                        InitiateArthasEvent(TOWN_HALL_COMPLETE, GAUNTLET_TRANSITION, ACTION_START_RP_EVENT4_1, guid);
                        break;
                    case DATA_START_GAUNTLET:
                        InitiateArthasEvent(GAUNTLET_PENDING, GAUNTLET_IN_PROGRESS, ACTION_START_RP_EVENT4_2, guid);
                        break;
                    case DATA_START_MALGANIS:
                        InitiateArthasEvent(GAUNTLET_COMPLETE, MALGANIS_IN_PROGRESS, ACTION_START_RP_EVENT5, guid);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取数据
             * @param type 数据类型ID
             * @return 对应的数据值
             *
             * 调用时机：外部脚本查询实例数据时
             *
             * 支持的查询：
             * - DATA_INSTANCE_PROGRESS: 返回当前进度状态
             */
            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_INSTANCE_PROGRESS)
                    return _currentState;
                return 0;
            }

            /**
             * @brief 设置Boss状态
             * @param type Boss数据ID
             * @param state 遭遇战状态
             * @return 是否成功设置
             *
             * 调用时机：Boss状态变化时
             *
             * 特殊处理：
             * - 无限腐蚀者完成时：取消守护者计时器，隐藏UI计时器
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (type == DATA_INFINITE_CORRUPTOR && state == DONE)
                {
                    events.CancelEvent(EVENT_GUARDIAN_TICK);
                    SetWorldState(WORLDSTATE_TIME_GUARDIAN_SHOW, 0, false);
                    SetWorldState(WORLDSTATE_TIME_GUARDIAN, 0);
                }

                if (!InstanceScript::SetBossState(type, state))
                    return false;

                return true;
            }

            /**
             * @brief 更新实例
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 调用时机：每个游戏tick（通常约50ms）
             *
             * 主要逻辑：
             * 1. 更新事件调度器
             * 2. 处理到期的事件：
             *    - EVENT_GUARDIAN_TICK: 时间守护者计时器更新
             *    - EVENT_RESPAWN_ARTHAS: 重生阿萨斯
             *    - EVENT_CRIER_CALL_TO_GATES: 传令官呼叫前往城门
             *    - EVENT_SCOURGE_WAVE: 生成天灾波次
             *    - EVENT_CRIER_ANNOUNCE_WAVE: 传令官宣布波次位置
             *
             * @性能注意事项：每帧都会调用，避免耗时操作
             */
            void Update(uint32 diff) override
            {
                events.Update(diff);
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_GUARDIAN_TICK: // 在计时器的:00秒tick，以及在剩余4:30时用于克罗米低语
                        {                         // 我们将低语作为守护者tick处理，因为不想重复实时代码
                            if (instance->GetSpawnMode() != DUNGEON_DIFFICULTY_HEROIC)
                                return;

                            time_t secondsToGuardianDeath = _infiniteGuardianTimeout - GameTime::GetGameTime();
                            if (secondsToGuardianDeath <= 0)
                            {
                                // 时间到，守护者死亡，任务失败
                                _infiniteGuardianTimeout = 0;
                                SetWorldState(WORLDSTATE_TIME_GUARDIAN_SHOW, 0, false);
                                SetWorldState(WORLDSTATE_TIME_GUARDIAN, 0);

                                if (Creature* corruptor = instance->GetCreature(_corruptorGUID))
                                {
                                    corruptor->AI()->DoAction(-ACTION_CORRUPTOR_LEAVE);
                                    if (Creature* guardian = instance->GetCreature(_guardianGUID))
                                        Unit::Kill(corruptor, guardian); // @todo 是否有特定法术？
                                }
                                SetBossState(DATA_INFINITE_CORRUPTOR, FAIL);
                            }
                            else
                            {
                                time_t minutes = (secondsToGuardianDeath - 1) / MINUTE;
                                time_t seconds = ((secondsToGuardianDeath - 1) % MINUTE) + 1;

                                // 克罗米低语 - 我们只在:00和:30时tick，但为了应对慢tick率留一些余地
                                if (minutes == 24 && seconds >= 45)
                                    if (Creature* chromie = instance->GetCreature(_chromieGUID))
                                        chromie->AI()->Talk(CHROMIE_WHISPER_GUARDIAN_1);
                                if (minutes == 4 && seconds < 45)
                                    if (Creature* chromie = instance->GetCreature(_chromieGUID))
                                        chromie->AI()->Talk(CHROMIE_WHISPER_GUARDIAN_2);
                                if (minutes == 0)
                                    if (Creature* chromie = instance->GetCreature(_chromieGUID))
                                        chromie->AI()->Talk(CHROMIE_WHISPER_GUARDIAN_3);

                                // 更新计时器状态
                                SetWorldState(WORLDSTATE_TIME_GUARDIAN_SHOW, 1, false);
                                SetWorldState(WORLDSTATE_TIME_GUARDIAN, minutes + 1);
                                if (minutes == 4 && seconds > 30)
                                    events.Repeat(Seconds(seconds - 30));
                                else
                                    events.Repeat(Seconds(seconds));
                            }
                            break;
                        }
                        case EVENT_RESPAWN_ARTHAS:
                            TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::Update: 为实例生成新的阿萨斯...");
                            instance->SummonCreature(NPC_ARTHAS, GetArthasSnapbackFor(_currentState));
                            events.CancelEvent(EVENT_RESPAWN_ARTHAS); // 确保不会调度两个
                            break;
                        case EVENT_CRIER_CALL_TO_GATES:
                            if (_currentState == CRATES_DONE)
                                if (Creature* crier = instance->GetCreature(_crierGUID))
                                    crier->AI()->Talk(CRIER_SAY_CALL_TO_GATES);
                            break;
                        case EVENT_SCOURGE_WAVE:
                        {
                            if (_currentState != WAVES_IN_PROGRESS)
                                break;

                            ++_waveCount;
                            SetWorldState(WORLDSTATE_WAVE_COUNT, _waveCount);

                            // 随机选择生成位置，但不允许重复
                            uint8 spawnLoc = urand(WAVE_LOC_MIN, WAVE_LOC_MAX);
                            while (spawnLoc == _currentSpawnLoc)
                                spawnLoc = urand(WAVE_LOC_MIN, WAVE_LOC_MAX);
                            WaveLocation const& spawnLocation = WaveLocations[spawnLoc - WAVE_LOC_MIN];

                            switch (_waveCount)
                            {
                                case WAVE_MEATHOOK:
                                    // 第5波：生成肉钩Boss
                                    if (Creature* spawn = instance->SummonCreature(NPC_MEATHOOK, spawnLocation.SpawnPoints[0]))
                                        _waveSpawns.insert(spawn->GetGUID());
                                    break;
                                case WAVE_SALRAMM:
                                    // 第10波：生成萨尔拉玛Boss
                                    if (Creature* spawn = instance->SummonCreature(NPC_SALRAMM, spawnLocation.SpawnPoints[0]))
                                        _waveSpawns.insert(spawn->GetGUID());
                                    break;
                                default:
                                    // 普通波次：根据难度生成怪物
                                    if (instance->GetSpawnMode() == DUNGEON_DIFFICULTY_HEROIC)
                                    {
                                        // 英雄难度：按预设配置生成
                                        for (uint32 i = 0; i < MAX_SPAWNS_PER_WAVE; ++i)
                                            if (uint32 entry = HeroicWaves[_waveCount - 1][i])
                                                if (Creature* spawn = instance->SummonCreature(entry, spawnLocation.SpawnPoints[i]))
                                                    _waveSpawns.insert(spawn->GetGUID());
                                    }
                                    else
                                    {
                                        // 普通难度：只生成2只吞噬食尸鬼
                                        for (uint32 i = 0; i <= 1; ++i)
                                            if (Creature* spawn = instance->SummonCreature(NPC_DEVOURING_GHOUL, spawnLocation.SpawnPoints[i]))
                                                _waveSpawns.insert(spawn->GetGUID());
                                    }
                                    break;
                            }

                            // 设置世界状态标记
                            for (uint32 marker = WAVE_MARKER_MIN; marker <= WAVE_MARKER_MAX; ++marker)
                                SetWorldState(COSWorldStates(marker), 0, false);
                            SetWorldState(spawnLocation.WorldState, 1);

                            events.RescheduleEvent(EVENT_CRIER_ANNOUNCE_WAVE, 2s);
                            _currentSpawnLoc = spawnLoc;
                            break;
                        }
                        case EVENT_CRIER_ANNOUNCE_WAVE:
                            if (_currentState == WAVES_IN_PROGRESS)
                                if (Creature* crier = instance->GetCreature(_crierGUID))
                                    crier->AI()->Talk(_currentSpawnLoc);
                            break;
                        default:
                            break;
                    }
                }
            }

            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_CHROMIE:
                        _chromieGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    case NPC_INFINITE_CORRUPTOR:
                        _corruptorGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    case NPC_GUARDIAN_OF_TIME:
                        _guardianGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    case NPC_GENERIC_BUNNY:
                        _genericBunnyGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    case NPC_CRATE_HELPER:
                        _plagueCrates.push_back(creature->GetGUID());
                        break;
                    case NPC_ARTHAS:
                        TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::OnCreatureCreate: Arthas spawned at {}", creature->GetPosition().ToString());
                        _arthasGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    case NPC_LORDAERON_CRIER:
                        _crierGUID = creature->GetGUID();
                        creature->setActive(true);
                        break;
                    default:
                        break;
                }
            }

            void OnGameObjectCreate(GameObject* object) override
            {
                switch (object->GetEntry())
                {
                    case GO_HIDDEN_PASSAGE:
                        _passageGUID = object->GetGUID();
                        object->setActive(true);
                        object->SetGoState(_currentState <= GAUNTLET_TRANSITION ? GO_STATE_READY : GO_STATE_ACTIVE);
                        break;
                    default:
                        break;
                }
            }

            void InitiateArthasEvent(COSProgressStates fromState, COSProgressStates toState, COSInstanceActions startAction, ObjectGuid starterGUID)
            {
                if (_currentState != fromState)
                    return;
                SetInstanceProgress(toState, false);
                if (Creature* arthas = instance->GetCreature(_arthasGUID))
                    arthas->AI()->SetGUID(starterGUID, -startAction);
            }

            void SetInstanceProgress(COSProgressStates state, bool force)
            {
                TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::SetInstanceProgress: Instance progress is now 0x{:X}", (uint32)state);
                _currentState = state;

                /* Spawn group management */
                SetSpawnGroupState(SPAWNGRP_CHROMIE_MID, (state >= CRATES_DONE), force);
                SetSpawnGroupState(SPAWNGRP_CRATE_HELPERS, (state == CRATES_IN_PROGRESS || state == CRATES_DONE), true);
                SetSpawnGroupState(SPAWNGRP_GAUNTLET_TRASH, (state == WAVES_IN_PROGRESS), force);
                SetSpawnGroupState(SPAWNGRP_UNDEAD_TRASH, (state >= WAVES_IN_PROGRESS && state < GAUNTLET_COMPLETE), force);
                SetSpawnGroupState(SPAWNGRP_RESIDENTS, (state < WAVES_IN_PROGRESS), true);

                /* Arthas management */
                if (state > CRATES_DONE)
                {   // there might be an Arthas instance in the dungeon somewhere
                    // notify him of the change so he can adjust
                    Creature* arthas = instance->GetCreature(_arthasGUID);
                    if (arthas)
                    {
                        if (force)
                        {
                            arthas->DespawnOrUnsummon();
                            arthas = nullptr;
                        }
                        else
                            arthas->AI()->DoAction(-ACTION_PROGRESS_UPDATE);
                    }

                    if (!arthas) // if there is currently no arthas, then we need to spawn one
                        events.ScheduleEvent(EVENT_RESPAWN_ARTHAS, 1s);
                }
                else if (Creature* arthas = instance->GetCreature(_arthasGUID)) // there shouldn't be any Arthas around
                    arthas->DespawnOrUnsummon();

                /* World state management */
                // Plague crates
                if (state == CRATES_IN_PROGRESS)
                {
                    SetWorldState(WORLDSTATE_SHOW_CRATES, 1, false);
                    SetWorldState(WORLDSTATE_CRATES_REVEALED, 0, false);
                }
                else if (state == CRATES_DONE)
                {
                    SetWorldState(WORLDSTATE_SHOW_CRATES, 1, false);
                    SetWorldState(WORLDSTATE_CRATES_REVEALED, NUM_PLAGUE_CRATES, false);
                }
                else
                {
                    SetWorldState(WORLDSTATE_SHOW_CRATES, 0, false);
                    SetWorldState(WORLDSTATE_CRATES_REVEALED, state == JUST_STARTED ? 0 : NUM_PLAGUE_CRATES, false);
                }
                // Scourge wave counter
                if (state == WAVES_DONE)
                    SetWorldState(WORLDSTATE_WAVE_COUNT, NUM_SCOURGE_WAVES, false);
                else
                    SetWorldState(WORLDSTATE_WAVE_COUNT, 0, false);

                PropagateWorldStateUpdate();

                // Hidden Passage status handling
                if (GameObject* passage = instance->GetGameObject(_passageGUID))
                    passage->SetGoState(state <= GAUNTLET_TRANSITION ? GO_STATE_READY : GO_STATE_ACTIVE);

                switch (state)
                {
                    case CRATES_DONE:
                        if (Creature* bunny = instance->GetCreature(_genericBunnyGUID))
                            bunny->CastSpell(nullptr, SPELL_CRATES_KILL_CREDIT, TRIGGERED_FULL_MASK);
                        events.ScheduleEvent(EVENT_CRIER_CALL_TO_GATES, 5s);
                        break;
                    case WAVES_IN_PROGRESS:
                        _waveCount = 0;
                        _currentSpawnLoc = 0;
                        _waveSpawns.clear();
                        events.ScheduleEvent(EVENT_SCOURGE_WAVE, 1s);
                        SpawnInfiniteCorruptor();
                        break;
                    default:
                        break;
                }

                if (force)
                {
                    // Forced transitions are regressions (event failures) or GM overrides; respawn all dead creatures, and despawn any temporary summons
                    events.Reset();
                    instance->DeleteRespawnTimes();

                    // Reset respawn time on all permanent spawns, despawn all temporary spawns
                    // @todo dynspawn, this won't work
                    std::vector<Creature*> toDespawn;
                    std::unordered_map<ObjectGuid, Creature*> const& objects = instance->GetObjectsStore().GetElements()._elements._element;
                    for (std::unordered_map<ObjectGuid, Creature*>::const_iterator itr = objects.cbegin(); itr != objects.cend(); ++itr)
                    {
                        if (itr->second && (itr->second->isDead() || !itr->second->GetSpawnId() || itr->second->GetOriginalEntry() != itr->second->GetEntry()))
                        {
                            if (itr->second->getDeathState() == DEAD) // despawned, not corpse
                                itr->second->SetRespawnTime(1);
                            else
                                toDespawn.push_back(itr->second);
                        }
                    }

                    for (Creature* creature : toDespawn)
                    {
                        if (creature->GetSpawnId())
                            creature->SetRespawnTime(1);
                        creature->DespawnOrUnsummon(0s, 1s);
                    }

                    SpawnInfiniteCorruptor();
                    events.RescheduleEvent(EVENT_RESPAWN_ARTHAS, 1s);
                }

                SaveToDB();
            }

        private:
            typedef std::unordered_map<uint32, uint32> WorldStateMap; // 世界状态映射类型

            /**
             * @brief 计算缺失的瘟疫箱子数量
             * @return 未发现的箱子数量
             *
             * 遍历所有箱子辅助NPC，检查其状态，返回未发现的数量
             */
            uint32 MissingPlagueCrates() const
            {
                uint32 returnValue = 0;
                for (ObjectGuid const& crateHelperGUID : _plagueCrates)
                    if (Creature* crateHelper = instance->GetCreature(crateHelperGUID))
                        if (crateHelper->IsAlive() && !crateHelper->AI()->GetData(DATA_CRATE_REVEALED))
                            ++returnValue;
                return returnValue;
            }

            /**
             * @brief 生成无限腐蚀者及相关NPC
             *
             * 生成条件：
             * - 未设置守护者超时（尚未生成）
             * - 英雄难度
             * - 无限腐蚀者Boss未完成且未失败
             *
             * 生成内容：
             * - 时间裂隙
             * - 时间守护者
             * - 无限腐蚀者
             * - 设置25分钟超时
             * - 启动守护者计时器
             */
            void SpawnInfiniteCorruptor()
            {
                if (!_infiniteGuardianTimeout && instance->GetSpawnMode() == DUNGEON_DIFFICULTY_HEROIC && (GetBossState(DATA_INFINITE_CORRUPTOR) != DONE && GetBossState(DATA_INFINITE_CORRUPTOR) != FAIL))
                {
                    instance->SummonCreature(NPC_TIME_RIFT, CorruptorRiftPos);
                    instance->SummonCreature(NPC_GUARDIAN_OF_TIME, GuardianPos);
                    instance->SummonCreature(NPC_INFINITE_CORRUPTOR, CorruptorPos);
                    _infiniteGuardianTimeout = GameTime::GetGameTime() + 25 * MINUTE;
                    events.ScheduleEvent(EVENT_GUARDIAN_TICK, 6s);
                }
            }

            /**
             * @brief 设置世界状态
             * @param state 世界状态ID
             * @param value 状态值
             * @param immediate 是否立即更新到客户端
             *
             * 更新内部状态，如果immediate为true则立即同步到客户端
             */
            void SetWorldState(COSWorldStates state, uint32 value, bool immediate = true)
            {
                TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::SetWorldState: {} {}", uint32(state), value);
                _currentWorldStates[state] = value;
                if (immediate)
                    PropagateWorldStateUpdate();
            }

            /**
             * @brief 传播世界状态更新
             *
             * 比较当前状态和已发送状态，只发送有变化的状态到客户端
             */
            void PropagateWorldStateUpdate()
            {
                TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::PropagateWorldStateUpdate: 传播世界状态");
                for (WorldStateMap::const_iterator it = _currentWorldStates.begin(); it != _currentWorldStates.end(); ++it)
                {
                    uint32& sent = _sentWorldStates[it->first];
                    if (sent != it->second)
                    {
                        TC_LOG_DEBUG("scripts.cos", "instance_culling_of_stratholme::PropagateWorldStateUpdate: 发送世界状态 {} ({})", it->first, it->second);
                        DoUpdateWorldState(it->first, it->second);
                        sent = it->second;
                    }
                }
            }

            /**
             * @brief 设置生成组状态
             * @param group 生成组ID
             * @param state 是否激活
             * @param force 是否强制
             *
             * 根据参数管理生成组：
             * - state=true: 生成组内生物
             * - state=false + force=true: 强制消失组内生物
             * - state=false + force=false: 将组设为非激活状态
             */
            void SetSpawnGroupState(COSInstanceEntries group, bool state, bool force)
            {
                if (state)
                    instance->SpawnGroupSpawn(group, true);
                else if (force)
                    instance->SpawnGroupDespawn(group, true);
                else
                    instance->SetSpawnGroupInactive(group);
            }

            // ==================== 成员变量 ====================

            EventMap events;                       // 事件调度器
            COSProgressStates _currentState;        // 当前进度状态
            WorldStateMap _sentWorldStates;         // 已发送的世界状态（用于增量更新）
            WorldStateMap _currentWorldStates;      // 当前世界状态
            time_t _infiniteGuardianTimeout;        // 无限守护者超时时间戳（UNIX时间）

            // 通用NPC GUID
            ObjectGuid _chromieGUID;                // 克罗米GUID
            ObjectGuid _corruptorGUID;              // 无限腐蚀者GUID
            ObjectGuid _guardianGUID;               // 时间守护者GUID
            ObjectGuid _genericBunnyGUID;           // 通用兔怪GUID（用于法术触发）
            std::vector<ObjectGuid> _plagueCrates;  // 瘟疫箱子辅助NPC GUID列表

            ObjectGuid _arthasGUID;                 // 阿萨斯GUID
            ObjectGuid _crierGUID;                  // 洛丹伦传令官GUID

            // 天灾波次
            uint32 _waveCount;                      // 当前波次计数（1-10）
            uint8 _currentSpawnLoc;                 // 当前生成位置ID
            std::unordered_set<ObjectGuid> _waveSpawns; // 当前波次存活的怪物GUID集合

            // 通道
            ObjectGuid _passageGUID;                // 隐藏通道游戏对象GUID
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 创建的实例脚本指针
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_culling_of_stratholme_InstanceMapScript(map);
        }
};

/**
 * @brief 注册斯坦索姆抉择实例脚本
 *
 * 创建并注册斯坦索姆抉择实例脚本实例
 */
void AddSC_instance_culling_of_stratholme()
{
    new instance_culling_of_stratholme();
}
