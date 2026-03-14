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
 * @file instance_blackrock_depths.cpp
 * @brief 黑石深渊副本实例脚本实现
 *
 * 本文件实现了黑石深渊副本的核心实例管理逻辑，包括：
 * - BOSS和游戏对象的GUID管理
 * - 副本进度和遭遇状态追踪
 * - 七贤之墓事件的完整流程控制
 * - 茉艾拉·铜须形态变换逻辑
 * - 数据保存和加载功能
 *
 * 黑石深渊是一个复杂的副本，包含多个关键事件：
 * 1. 律法之环竞技场事件
 * 2. 金库事件
 * 3. 酒吧事件
 * 4. 七贤之墓事件
 * 5. 大厅火盆事件
 * 6. 马格姆斯和最终BOSS事件
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MapReference.h"
#include "Player.h"

#define TIMER_TOMBOFTHESEVEN    15000   ///< 七贤之墓事件BOSS出现的间隔时间（毫秒）
#define MAX_ENCOUNTER           6       ///< 副本中需要追踪的最大遭遇战数量
constexpr uint8 TOMB_OF_SEVEN_BOSS_NUM = 7;  ///< 七贤之墓中的BOSS数量

/**
 * @brief 生物ID枚举
 * 定义副本中所有关键NPC的生物ID
 */
enum Creatures
{
    NPC_EMPEROR              = 9019,   // 索瑞森大帝 - 最终BOSS
    NPC_PHALANX              = 9502,   // 法兰克斯 - 酒吧BOSS
    NPC_ANGERREL             = 9035,   // 愤怒者 - 七贤之墓BOSS
    NPC_DOPEREL              = 9040,   // 多佩尔 - 七贤之墓BOSS
    NPC_HATEREL              = 9034,   // 仇恨者 - 七贤之墓BOSS
    NPC_VILEREL              = 9036,   // 卑鄙者 - 七贤之墓BOSS
    NPC_SEETHREL             = 9038,   // 暴怒者 - 七贤之墓BOSS
    NPC_GLOOMREL             = 9037,   // 忧郁者 - 七贤之墓BOSS
    NPC_DOOMREL              = 9039,   // 厄运者 - 七贤之墓BOSS
    NPC_MAGMUS               = 9938,   // 马格姆斯 - 守门BOSS
    NPC_MOIRA                = 8929,   // 茉艾拉·铜须公主 - 最终BOSS之一
    NPC_PRIESTESS_THAURISSAN = 10076,  // 瑟瑞斯萨女祭司 - 茉艾拉的另一形态
    NPC_COREN                = 23872,  // 科伦·黑酿 - 美酒节事件BOSS
};

/**
 * @brief 游戏对象ID枚举
 * 定义副本中所有关键游戏对象的ID
 */
enum GameObjects
{
    GO_ARENA1               = 161525,  // 竞技场门1（小怪门）
    GO_ARENA2               = 161522,  // 竞技场门2（BOSS门）
    GO_ARENA3               = 161524,  // 竞技场门3（出口门）
    GO_ARENA4               = 161523,  // 竞技场门4（入口门）
    GO_SHADOW_LOCK          = 161460,  // 暗影锁
    GO_SHADOW_MECHANISM     = 161461,  // 暗影机关
    GO_SHADOW_GIANT_DOOR    = 157923,  // 暗影巨门
    GO_SHADOW_DUMMY         = 161516,  // 暗影假人
    GO_BAR_KEG_SHOT         = 170607,  // 酒吧酒桶
    GO_BAR_KEG_TRAP         = 171941,  // 酒吧酒桶陷阱
    GO_BAR_DOOR             = 170571,  // 酒吧门
    GO_TOMB_ENTER           = 170576,  // 七贤之墓入口门
    GO_TOMB_EXIT            = 170577,  // 七贤之墓出口门
    GO_LYCEUM               = 170558,  // 大厅门
    GO_SF_N                 = 174745,  // 暗影熔炉火盆（北）
    GO_SF_S                 = 174744,  // 暗影熔炉火盆（南）
    GO_GOLEM_ROOM_N         = 170573,  // 马格姆斯房间门（北）
    GO_GOLEM_ROOM_S         = 170574,  // 马格姆斯房间门（南）
    GO_THRONE_ROOM          = 170575,  // 王座房间门
    GO_SPECTRAL_CHALICE     = 164869,  // 幽灵圣杯
    GO_CHEST_SEVEN          = 169243   // 七贤宝箱
};

/**
 * @brief 任务ID枚举
 * 定义与茉艾拉相关的任务ID
 */
enum Quests
{
    QUEST_THE_PRINCESS_SURPRISE = 4363, // 联盟任务：公主的惊喜
    QUEST_THE_PRINCESS_SAVED    = 4004  // 部落任务：拯救公主
};

/**
 * @brief 黑石深渊副本实例脚本类
 *
 * 继承自InstanceMapScript，为黑石深渊副本提供实例级别的管理和状态追踪。
 * 该类负责管理副本中的所有BOSS、游戏对象和事件状态，并处理数据持久化。
 */
class instance_blackrock_depths : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * 初始化副本脚本，注册脚本名称和地图ID（230为黑石深渊地图ID）
     */
    instance_blackrock_depths() : InstanceMapScript(BRDScriptName, 230) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 返回黑石深渊实例脚本对象
     *
     * 工厂方法，为指定的副本地图创建实例脚本对象。
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_blackrock_depths_InstanceMapScript(map);
    }

    /**
     * @brief 黑石深渊实例脚本实现类
     *
     * 继承自InstanceScript，实现黑石深渊副本的核心管理逻辑。
     * 包括BOSS和游戏对象的GUID存储、遭遇战状态追踪、七贤之墓事件管理等。
     */
    struct instance_blackrock_depths_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         * 初始化所有成员变量并设置数据头
         */
        instance_blackrock_depths_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            memset(&encounter, 0, sizeof(encounter));  // 初始化遭遇战状态数组

            BarAleCount = 0;          // 酒吧啤酒计数器
            GhostKillCount = 0;       // 七贤之墓击杀计数器
            TombTimer = TIMER_TOMBOFTHESEVEN;  // 七贤之墓事件计时器
            TombEventCounter = 0;     // 七贤之墓事件计数器
        }

        uint32 encounter[MAX_ENCOUNTER];    ///< 遭遇战状态数组，存储6个事件的状态
        std::string str_data;               ///< 序列化的副本数据字符串

        ObjectGuid EmperorGUID;             ///< 索瑞森大帝GUID
        ObjectGuid PhalanxGUID;             ///< 法兰克斯GUID
        ObjectGuid MagmusGUID;              ///< 马格姆斯GUID
        ObjectGuid MoiraGUID;               ///< 茉艾拉·铜须GUID
        ObjectGuid CorenGUID;               ///< 科伦·黑酿GUID

        ObjectGuid GoArena1GUID;            ///< 竞技场门1 GUID
        ObjectGuid GoArena2GUID;            ///< 竞技场门2 GUID
        ObjectGuid GoArena3GUID;            ///< 竞技场门3 GUID
        ObjectGuid GoArena4GUID;            ///< 竞技场门4 GUID
        ObjectGuid GoShadowLockGUID;        ///< 暗影锁GUID
        ObjectGuid GoShadowMechGUID;        ///< 暗影机关GUID
        ObjectGuid GoShadowGiantGUID;       ///< 暗影巨门GUID
        ObjectGuid GoShadowDummyGUID;       ///< 暗影假人GUID
        ObjectGuid GoBarKegGUID;            ///< 酒吧酒桶GUID
        ObjectGuid GoBarKegTrapGUID;        ///< 酒吧酒桶陷阱GUID
        ObjectGuid GoBarDoorGUID;           ///< 酒吧门GUID
        ObjectGuid GoTombEnterGUID;         ///< 七贤之墓入口门GUID
        ObjectGuid GoTombExitGUID;          ///< 七贤之墓出口门GUID
        ObjectGuid GoLyceumGUID;            ///< 大厅门GUID
        ObjectGuid GoSFSGUID;               ///< 南火盆GUID
        ObjectGuid GoSFNGUID;               ///< 北火盆GUID
        ObjectGuid GoGolemNGUID;            ///< 北机关门GUID
        ObjectGuid GoGolemSGUID;            ///< 南机关门GUID
        ObjectGuid GoThroneGUID;            ///< 王座门GUID
        ObjectGuid GoChestGUID;             ///< 七贤宝箱GUID
        ObjectGuid GoSpectralChaliceGUID;   ///< 幽灵圣杯GUID

        uint32 BarAleCount;                 ///< 酒吧事件中已提交的啤酒数量
        uint32 GhostKillCount;              ///< 七贤之墓中已击杀的BOSS数量
        ObjectGuid TombBossGUIDs[TOMB_OF_SEVEN_BOSS_NUM];  ///< 七贤之墓BOSS GUID数组
        ObjectGuid TombEventStarterGUID;    ///< 七贤之墓事件触发者GUID
        uint32 TombTimer;                   ///< 七贤之墓事件计时器
        uint32 TombEventCounter;            ///< 七贤之墓事件当前处理的BOSS索引

        /**
         * @brief 更新茉艾拉的形态
         * @param moira 茉艾拉生物对象指针
         *
         * 检查副本中所有玩家的任务状态，如果所有玩家都在进行对应阵营的任务，
         * 则将茉艾拉转换为瑟瑞斯萨女祭司形态。
         * 联盟玩家需要激活"公主的惊喜"任务，部落玩家需要激活"拯救公主"任务。
         *
         * 调用时机：茉艾拉生物创建时
         */
        void UpdateMoira(Creature* moira)
        {
            InstanceMap::PlayerList const& players = instance->GetPlayers();

            // 遍历副本中的所有玩家
            for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
                if (Player * player = i->GetSource())
                    // 如果联盟玩家未激活"公主的惊喜"任务或部落玩家未激活"拯救公主"任务，则不转换形态
                    if ((player->GetTeamId() == TEAM_ALLIANCE && !player->IsActiveQuest(QUEST_THE_PRINCESS_SURPRISE))
                        || (player->GetTeamId() == TEAM_HORDE && !player->IsActiveQuest(QUEST_THE_PRINCESS_SAVED)))
                        return;

            // 所有玩家都在进行对应任务，转换茉艾拉为瑟瑞斯萨女祭司形态
            moira->UpdateEntry(NPC_PRIESTESS_THAURISSAN);
        }

        /**
         * @brief 生物创建事件处理
         * @param creature 新创建的生物对象
         *
         * 当副本中的生物创建时调用，记录关键生物的GUID以供后续使用。
         * 对于七贤之墓BOSS，按固定顺序存储在数组中。
         * 对于马格姆斯，如果已死亡则开启通往最终BOSS的门。
         *
         * 调用时机：副本中的生物创建时由核心引擎调用
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_EMPEROR: EmperorGUID = creature->GetGUID(); break;
                case NPC_PHALANX: PhalanxGUID = creature->GetGUID(); break;
                case NPC_MOIRA:
                    MoiraGUID = creature->GetGUID();
                    UpdateMoira(creature);  // 根据任务状态更新茉艾拉形态
                    break;
                case NPC_COREN: CorenGUID = creature->GetGUID(); break;
                // 七贤之墓BOSS按固定顺序存储
                case NPC_DOOMREL: TombBossGUIDs[0] = creature->GetGUID(); break;   // 厄运者
                case NPC_DOPEREL: TombBossGUIDs[1] = creature->GetGUID(); break;   // 多佩尔
                case NPC_HATEREL: TombBossGUIDs[2] = creature->GetGUID(); break;   // 仇恨者
                case NPC_VILEREL: TombBossGUIDs[3] = creature->GetGUID(); break;   // 卑鄙者
                case NPC_SEETHREL: TombBossGUIDs[4] = creature->GetGUID(); break;  // 暴怒者
                case NPC_GLOOMREL: TombBossGUIDs[5] = creature->GetGUID(); break;  // 忧郁者
                case NPC_ANGERREL: TombBossGUIDs[6] = creature->GetGUID(); break;  // 愤怒者
                case NPC_MAGMUS:
                    MagmusGUID = creature->GetGUID();
                    // 如果马格姆斯已死亡，开启通往最终BOSS的门
                    if (!creature->IsAlive())
                        HandleGameObject(GetGuidData(DATA_THRONE_DOOR), true);
                    break;
            }
        }

        /**
         * @brief 游戏对象创建事件处理
         * @param go 新创建的游戏对象
         *
         * 当副本中的游戏对象创建时调用，记录关键游戏对象的GUID。
         * 对于七贤之墓出口门，根据已击杀BOSS数量决定开启或关闭状态。
         *
         * 调用时机：副本中的游戏对象创建时由核心引擎调用
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_ARENA1: GoArena1GUID = go->GetGUID(); break;
                case GO_ARENA2: GoArena2GUID = go->GetGUID(); break;
                case GO_ARENA3: GoArena3GUID = go->GetGUID(); break;
                case GO_ARENA4: GoArena4GUID = go->GetGUID(); break;
                case GO_SHADOW_LOCK: GoShadowLockGUID = go->GetGUID(); break;
                case GO_SHADOW_MECHANISM: GoShadowMechGUID = go->GetGUID(); break;
                case GO_SHADOW_GIANT_DOOR: GoShadowGiantGUID = go->GetGUID(); break;
                case GO_SHADOW_DUMMY: GoShadowDummyGUID = go->GetGUID(); break;
                case GO_BAR_KEG_SHOT: GoBarKegGUID = go->GetGUID(); break;
                case GO_BAR_KEG_TRAP: GoBarKegTrapGUID = go->GetGUID(); break;
                case GO_BAR_DOOR: GoBarDoorGUID = go->GetGUID(); break;
                case GO_TOMB_ENTER: GoTombEnterGUID = go->GetGUID(); break;
                case GO_TOMB_EXIT:
                    GoTombExitGUID = go->GetGUID();
                    // 如果已击杀所有七贤之墓BOSS，开启出口门；否则关闭
                    if (GhostKillCount >= TOMB_OF_SEVEN_BOSS_NUM)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    else
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_LYCEUM: GoLyceumGUID = go->GetGUID(); break;
                case GO_SF_S: GoSFSGUID = go->GetGUID(); break;
                case GO_SF_N: GoSFNGUID = go->GetGUID(); break;
                case GO_GOLEM_ROOM_N: GoGolemNGUID = go->GetGUID(); break;
                case GO_GOLEM_ROOM_S: GoGolemSGUID = go->GetGUID(); break;
                case GO_THRONE_ROOM: GoThroneGUID = go->GetGUID(); break;
                case GO_CHEST_SEVEN: GoChestGUID = go->GetGUID(); break;
                case GO_SPECTRAL_CHALICE: GoSpectralChaliceGUID = go->GetGUID(); break;
            }
        }

        /**
         * @brief 设置GUID数据
         * @param type 数据类型标识
         * @param data GUID数据
         *
         * 用于存储和更新副本中的GUID数据，主要用于七贤之墓事件的触发和重置。
         *
         * 调用时机：其他脚本需要存储或更新副本GUID数据时
         */
        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            TC_LOG_DEBUG("scripts", "Instance Blackrock Depths: SetGuidData update (Type: {} Data {})", type, data.ToString());

            switch (type)
            {
            case DATA_EVENSTARTER:
                TombEventStarterGUID = data;
                if (!TombEventStarterGUID)
                    TombOfSevenReset();  // 如果GUID为空，重置七贤之墓事件
                else
                    TombOfSevenStart();  // 否则启动七贤之墓事件
                break;
            }
        }

        /**
         * @brief 设置副本数据
         * @param type 数据类型标识
         * @param data 数据值
         *
         * 用于更新副本中各种事件的状态，包括遭遇战进度和计数器。
         * 当事件完成或七贤之墓BOSS全部击杀时，保存副本数据。
         *
         * 调用时机：副本中的事件状态发生变化时
         */
        void SetData(uint32 type, uint32 data) override
        {
            TC_LOG_DEBUG("scripts", "Instance Blackrock Depths: SetData update (Type: {} Data {})", type, data);

            switch (type)
            {
                case TYPE_RING_OF_LAW:
                    encounter[0] = data;  // 律法之环事件状态
                    break;
                case TYPE_VAULT:
                    encounter[1] = data;  // 金库事件状态
                    break;
                case TYPE_BAR:
                    if (data == SPECIAL)
                        ++BarAleCount;    // 酒吧事件中提交啤酒，增加计数
                    else
                        encounter[2] = data;  // 酒吧事件状态
                    break;
                case TYPE_TOMB_OF_SEVEN:
                    encounter[3] = data;  // 七贤之墓事件状态
                    break;
                case TYPE_LYCEUM:
                    encounter[4] = data;  // 大厅事件状态
                    break;
                case TYPE_IRON_HALL:
                    encounter[5] = data;  // 铁厅事件状态
                    break;
                case DATA_GHOSTKILL:
                    GhostKillCount += data;  // 增加七贤之墓BOSS击杀计数
                    break;
            }

            // 当事件完成或七贤之墓BOSS全部击杀时，保存副本数据
            if (data == DONE || GhostKillCount >= TOMB_OF_SEVEN_BOSS_NUM)
            {
                OUT_SAVE_INST_DATA;

                // 将所有遭遇战状态和击杀计数序列化为字符串
                std::ostringstream saveStream;
                saveStream << encounter[0] << ' ' << encounter[1] << ' ' << encounter[2] << ' '
                    << encounter[3] << ' ' << encounter[4] << ' ' << encounter[5] << ' ' << GhostKillCount;

                str_data = saveStream.str();

                SaveToDB();
                OUT_SAVE_INST_DATA_COMPLETE;
            }
        }

        /**
         * @brief 获取副本数据
         * @param type 数据类型标识
         * @return 返回对应的数据值
         *
         * 用于查询副本中各种事件的状态和计数器值。
         * 对于酒吧事件，如果正在进行且啤酒数量达到3，返回SPECIAL状态。
         *
         * 调用时机：其他脚本需要查询副本数据时
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case TYPE_RING_OF_LAW:
                    return encounter[0];
                case TYPE_VAULT:
                    return encounter[1];
                case TYPE_BAR:
                    // 如果酒吧事件正在进行且啤酒数量达到3，返回SPECIAL状态
                    if (encounter[2] == IN_PROGRESS && BarAleCount == 3)
                        return SPECIAL;
                    else
                        return encounter[2];
                case TYPE_TOMB_OF_SEVEN:
                    return encounter[3];
                case TYPE_LYCEUM:
                    return encounter[4];
                case TYPE_IRON_HALL:
                    return encounter[5];
                case DATA_GHOSTKILL:
                    return GhostKillCount;
            }
            return 0;
        }

        /**
         * @brief 获取GUID数据
         * @param data 数据类型标识
         * @return 返回对应的GUID
         *
         * 用于查询副本中存储的各种生物和游戏对象GUID。
         *
         * 调用时机：其他脚本需要获取副本中的GUID数据时
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            switch (data)
            {
                case DATA_EMPEROR:
                    return EmperorGUID;
                case DATA_PHALANX:
                    return PhalanxGUID;
                case DATA_MOIRA:
                    return MoiraGUID;
                case DATA_COREN:
                    return CorenGUID;
                case DATA_ARENA1:
                    return GoArena1GUID;
                case DATA_ARENA2:
                    return GoArena2GUID;
                case DATA_ARENA3:
                    return GoArena3GUID;
                case DATA_ARENA4:
                    return GoArena4GUID;
                case DATA_GO_BAR_KEG:
                    return GoBarKegGUID;
                case DATA_GO_BAR_KEG_TRAP:
                    return GoBarKegTrapGUID;
                case DATA_GO_BAR_DOOR:
                    return GoBarDoorGUID;
                case DATA_EVENSTARTER:
                    return TombEventStarterGUID;
                case DATA_SF_BRAZIER_N:
                    return GoSFNGUID;
                case DATA_SF_BRAZIER_S:
                    return GoSFSGUID;
                case DATA_THRONE_DOOR:
                    return GoThroneGUID;
                case DATA_GOLEM_DOOR_N:
                    return GoGolemNGUID;
                case DATA_GOLEM_DOOR_S:
                    return GoGolemSGUID;
                case DATA_GO_CHALICE:
                    return GoSpectralChaliceGUID;
            }
            return ObjectGuid::Empty;
        }

        /**
         * @brief 获取保存数据
         * @return 返回序列化的副本数据字符串
         *
         * 用于副本数据持久化，返回包含所有遭遇战状态的字符串。
         */
        std::string GetSaveData() override
        {
            return str_data;
        }

        /**
         * @brief 加载副本数据
         * @param in 序列化的副本数据字符串
         *
         * 从保存的字符串中恢复副本状态，包括所有遭遇战状态和七贤之墓击杀计数。
         * 如果数据不完整或状态为进行中，则重置为未开始状态。
         *
         * 调用时机：副本加载时从数据库读取保存的数据
         */
        void Load(char const* in) override
        {
            if (!in)
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }

            OUT_LOAD_INST_DATA(in);

            // 从字符串中解析遭遇战状态和击杀计数
            std::istringstream loadStream(in);
            loadStream >> encounter[0] >> encounter[1] >> encounter[2] >> encounter[3]
            >> encounter[4] >> encounter[5] >> GhostKillCount;

            // 将所有进行中的遭遇战重置为未开始（避免卡住）
            for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                if (encounter[i] == IN_PROGRESS)
                    encounter[i] = NOT_STARTED;

            // 如果七贤之墓击杀计数不完整，重置事件
            if (GhostKillCount > 0 && GhostKillCount < TOMB_OF_SEVEN_BOSS_NUM)
                GhostKillCount = 0;

            // 限制击杀计数最大值为BOSS总数
            if (GhostKillCount >= TOMB_OF_SEVEN_BOSS_NUM)
                GhostKillCount = TOMB_OF_SEVEN_BOSS_NUM;

            OUT_LOAD_INST_DATA_COMPLETE;
        }

        /**
         * @brief 七贤之墓事件推进
         *
         * 激活下一个BOSS，使其变为敌对并开始攻击玩家。
         * 按照固定顺序逐个激活七贤之墓中的BOSS。
         *
         * 调用时机：定时器触发，每15秒激活一个BOSS
         * 性能注意事项：仅激活一个BOSS，性能开销较小
         */
        void TombOfSevenEvent()
        {
            // 如果尚未击杀所有BOSS且当前索引的BOSS存在
            if (GhostKillCount < TOMB_OF_SEVEN_BOSS_NUM && TombBossGUIDs[TombEventCounter])
            {
                if (Creature* boss = instance->GetCreature(TombBossGUIDs[TombEventCounter]))
                {
                    // 将BOSS设置为黑铁矮人阵营（敌对）
                    boss->SetFaction(FACTION_DARK_IRON_DWARVES);
                    // 取消对玩家的免疫
                    boss->SetImmuneToPC(false);
                    // 选择最近的目标并开始攻击
                    if (Unit* target = boss->SelectNearestTarget(500))
                        boss->AI()->AttackStart(target);
                }
            }
        }

        /**
         * @brief 重置七贤之墓事件
         *
         * 关闭出口门，开启入口门，重置所有BOSS状态，清空事件数据。
         * 用于事件失败或重置副本时恢复初始状态。
         *
         * 调用时机：七贤之墓事件触发者为空GUID时
         */
        void TombOfSevenReset()
        {
            HandleGameObject(GoTombExitGUID, false);   // 关闭出口门
            HandleGameObject(GoTombEnterGUID, true);   // 开启入口门

            // 重置所有BOSS状态
            for (uint8 i = 0; i < TOMB_OF_SEVEN_BOSS_NUM; ++i)
            {
                if (Creature* boss = instance->GetCreature(TombBossGUIDs[i]))
                {
                    if (!boss->IsAlive())
                        boss->Respawn();               // 复活已死亡的BOSS
                    else
                        boss->SetFaction(FACTION_FRIENDLY);  // 将存活的BOSS设置为友善阵营
                }
            }

            // 重置事件相关计数器和GUID
            GhostKillCount = 0;
            TombEventStarterGUID.Clear();
            TombEventCounter = 0;
            TombTimer = TIMER_TOMBOFTHESEVEN;
            SetData(TYPE_TOMB_OF_SEVEN, NOT_STARTED);
        }

        /**
         * @brief 启动七贤之墓事件
         *
         * 关闭入口门和出口门，将事件状态设置为进行中。
         * 玩家需要依次击败7个BOSS才能完成事件。
         *
         * 调用时机：玩家与幽灵圣杯交互触发事件时
         */
        void TombOfSevenStart()
        {
            HandleGameObject(GoTombExitGUID, false);   // 关闭出口门（防止逃跑）
            HandleGameObject(GoTombEnterGUID, false);  // 关闭入口门（防止增援）
            SetData(TYPE_TOMB_OF_SEVEN, IN_PROGRESS);
        }

        /**
         * @brief 结束七贤之墓事件
         *
         * 刷新七贤宝箱，开启所有门，清空事件触发者GUID，标记事件完成。
         * 玩家可以开启宝箱获得奖励。
         *
         * 调用时机：所有7个BOSS被击杀时
         */
        void TombOfSevenEnd()
        {
            // 刷新七贤宝箱（24小时后消失）
            DoRespawnGameObject(GoChestGUID, 24h);
            HandleGameObject(GoTombExitGUID, true);    // 开启出口门
            HandleGameObject(GoTombEnterGUID, true);   // 开启入口门
            TombEventStarterGUID.Clear();              // 清空事件触发者
            SetData(TYPE_TOMB_OF_SEVEN, DONE);
        }

        /**
         * @brief 更新实例逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理七贤之墓事件的定时器逻辑：
         * - 每15秒激活下一个BOSS
         * - 检查已击杀的BOSS数量
         * - 当所有BOSS被击杀时结束事件
         *
         * 调用时机：每个游戏Tick（约每50毫秒）
         * 性能注意事项：频繁调用，已优化为仅在事件进行中才处理
         */
        void Update(uint32 diff) override
        {
            // 如果事件触发者存在且未击杀所有BOSS
            if (TombEventStarterGUID && GhostKillCount < TOMB_OF_SEVEN_BOSS_NUM)
            {
                if (TombTimer <= diff)
                {
                    TombTimer = TIMER_TOMBOFTHESEVEN;  // 重置计时器

                    // 如果当前BOSS索引未超过总数，激活下一个BOSS
                    if (TombEventCounter < TOMB_OF_SEVEN_BOSS_NUM)
                    {
                        TombOfSevenEvent();
                        ++TombEventCounter;
                    }

                    // Check Killed bosses
                    // 检查已击杀的BOSS，更新击杀计数
                    for (uint8 i = 0; i < TOMB_OF_SEVEN_BOSS_NUM; ++i)
                    {
                        if (Creature* boss = instance->GetCreature(TombBossGUIDs[i]))
                        {
                            if (!boss->IsAlive())
                            {
                                GhostKillCount = i+1;  // 更新击杀计数为索引+1
                             }
                        }
                    }
                } else TombTimer -= diff;
            }

            // 如果已击杀所有BOSS且事件触发者存在，结束事件
            if (GhostKillCount >= TOMB_OF_SEVEN_BOSS_NUM && TombEventStarterGUID)
                TombOfSevenEnd();
        }
    };
};

/**
 * @brief 注册黑石深渊实例脚本
 *
 * 将黑石深渊副本实例脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有实例脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_instance_blackrock_depths()
{
    new instance_blackrock_depths();
}
