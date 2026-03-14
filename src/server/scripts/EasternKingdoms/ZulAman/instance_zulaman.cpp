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
 * @file instance_zulaman.cpp
 * @brief 祖阿曼副本实例脚本模块
 *
 * 本模块实现了祖阿曼副本的实例管理逻辑，包括：
 * - Boss状态管理和持久化
 * - 门禁系统控制
 * - 时间挑战模式（限时营救任务）
 * - 战利品宝箱生成
 * - 随机商人和事件状态管理
 *
 * 祖阿曼是一个10人团队副本，特色包括：
 * - 时间挑战模式：限时内击败Boss可获得额外奖励
 * - 每击败一个Boss可解救一个俘虏并获得宝箱
 * - 副本进度保存和恢复
 */

/* ScriptData
SDName: instance_zulaman
SD%Complete: 80
SDComment:
SDCategory: Zul'Aman
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "zulaman.h"

/**
 * @brief 杂项常量枚举
 *
 * 定义实例中使用的各种常量
 */
enum Misc
{
    RAND_VENDOR                    = 2,     ///< 随机商人数量
    WORLDSTATE_SHOW_TIMER          = 3104,  ///< 世界状态：显示计时器
    WORLDSTATE_TIME_TO_SACRIFICE   = 3106   ///< 世界状态：剩余时间
};

/**
 * @brief 俘虏信息结构
 *
 * 定义俘虏NPC和对应宝箱的信息
 * 宝箱在熊/鹰/龙鹰/山猫Boss处生成
 * 战利品取决于已击败的Boss数量，但宝箱ID固定
 * 由于无法直接向游戏对象添加战利品，使用固定的loot_template
 */
struct SHostageInfo
{
    uint32 npc, go;  ///< NPC ID和游戏对象ID（游戏对象ID未使用）
    Position pos;    ///< 生成位置
};

/**
 * @brief 俘虏信息数组
 *
 * 四个Boss对应的俘虏NPC和位置
 */
static SHostageInfo const HostageInfo[] =
{
    { 23790, 186648, { -57.f, 1343.f, 40.77f, 3.2f } }, // bear - 熊神俘虏
    { 23999, 187021, { 400.f, 1414.f, 74.36f, 3.3f } }, // eagle - 鹰神俘虏
    { 24001, 186672, { -35.f, 1134.f, 18.71f, 1.9f } }, // dragonhawk - 龙鹰神俘虏
    { 24024, 186667, { 413.f, 1117.f,  6.32f, 3.1f } }  // lynx - 山猫神俘虏
};

/**
 * @brief 哈里森·琼斯位置
 *
 * NPC在副本入口的初始位置
 */
Position const HarrisonJonesLoc = { 120.687f, 1674.0f, 42.0217f, 1.59044f };

/**
 * @brief 门禁数据
 *
 * 定义各个Boss关联的门禁系统
 */
static DoorData const doorData[] =
{
    { GO_HEXLORD_ENTRANCE,     BOSS_NALORAKK, DOOR_TYPE_PASSAGE },  ///< 妖术领主入口门（纳洛拉克）
    { GO_HEXLORD_ENTRANCE,     BOSS_AKILZON,  DOOR_TYPE_PASSAGE },  ///< 妖术领主入口门（阿基尔松）
    { GO_HEXLORD_ENTRANCE,     BOSS_JANALAI,  DOOR_TYPE_PASSAGE },  ///< 妖术领主入口门（加纳莱）
    { GO_HEXLORD_ENTRANCE,     BOSS_HALAZZI,  DOOR_TYPE_PASSAGE },  ///< 妖术领主入口门（哈拉兹）
    { GO_DOOR_AKILZON,         BOSS_AKILZON,  DOOR_TYPE_ROOM    },  ///< 阿基尔松房间门
    { GO_LYNX_TEMPLE_ENTRANCE, BOSS_HALAZZI,  DOOR_TYPE_ROOM    },  ///< 山猫神殿入口门
    { GO_LYNX_TEMPLE_EXIT,     BOSS_HALAZZI,  DOOR_TYPE_ROOM    },  ///< 山猫神殿出口门
    { GO_HEXLORD_ENTRANCE,     BOSS_HEXLORD,  DOOR_TYPE_ROOM    },  ///< 妖术领主房间门
    { GO_WOODEN_DOOR,          BOSS_HEXLORD,  DOOR_TYPE_PASSAGE },  ///< 木门（妖术领主通道）
    { GO_DOOR_ZULJIN,          BOSS_ZULJIN,   DOOR_TYPE_ROOM    },  ///< 祖尔金房间门
    { 0,                       0,             DOOR_TYPE_ROOM    }   ///< 结束标记
};

/**
 * @brief 生物数据
 *
 * 定义Boss和重要NPC的GUID映射
 */
static ObjectData const creatureData[] =
{
    { NPC_HARRISON_JONES, NPC_HARRISON_JONES },  ///< 哈里森·琼斯NPC
    { NPC_NALORAKK,       BOSS_NALORAKK      },  ///< 纳洛拉克Boss
    { NPC_AKILZON,        BOSS_AKILZON       },  ///< 阿基尔松Boss
    { NPC_JANALAI,        BOSS_JANALAI       },  ///< 加纳莱Boss
    { NPC_HALAZZI,        BOSS_HALAZZI       },  ///< 哈拉兹Boss
    { NPC_HEXLORD,        BOSS_HEXLORD       },  ///< 妖术领主玛拉卡斯Boss
    { NPC_ZULJIN,         BOSS_ZULJIN        },  ///< 祖尔金Boss
    { 0,                  0                  }   ///< 结束标记

};

/**
 * @brief 游戏对象数据
 *
 * 定义重要游戏对象的GUID映射
 */
static ObjectData const gameObjectData[] =
{
    { GO_MASSIVE_GATE,    GO_MASSIVE_GATE    },  ///< 大门
    { GO_HARKORS_SATCHEL, GO_HARKORS_SATCHEL },  ///< 哈克尔的行囊
    { GO_TANZARS_TRUNK,   GO_TANZARS_TRUNK   },  ///< 坦扎尔的箱子
    { GO_ASHLIS_BAG,      GO_ASHLIS_BAG      },  ///< 阿什利的袋子
    { GO_KRAZS_PACKAGE,   GO_KRAZS_PACKAGE   },  ///< 克拉兹的包裹
    { GO_STRANGE_GONG,    GO_STRANGE_GONG    },  ///< 奇怪的锣
    { 0,                  0                  }   ///< 结束标记
};

/**
 * @brief 祖阿曼实例脚本类
 *
 * 继承自InstanceMapScript，实现祖阿曼副本的实例管理
 */
class instance_zulaman : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册祖阿曼实例脚本，地图ID为568
         */
        instance_zulaman() : InstanceMapScript(ZulamanScriptName, 568) { }

        /**
         * @brief 祖阿曼实例脚本实现类
         *
         * 继承自InstanceScript，实现副本的核心管理逻辑
         */
        struct instance_zulaman_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * 初始化副本的所有状态和数据
             */
            instance_zulaman_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);                    // 设置数据头
                SetBossNumber(MAX_ENCOUNTER);              // 设置Boss数量
                LoadDoorData(doorData);                    // 加载门禁数据
                LoadObjectData(creatureData, gameObjectData); // 加载对象数据

                QuestTimer = 0;      // 任务计时器
                QuestMinute = 0;     // 剩余分钟数
                ChestLooted = 0;     // 已开启的宝箱数量

                for (uint8 i = 0; i < RAND_VENDOR; ++i)
                    RandVendor[i] = NOT_STARTED;  // 初始化随机商人为未开始状态

                GongEvent = NOT_STARTED;  // 锣事件状态
            }

            uint32 QuestTimer;      ///< 任务计时器（毫秒）
            uint32 QuestMinute;     ///< 剩余分钟数
            uint32 ChestLooted;     ///< 已开启的宝箱数量

            EncounterState RandVendor[RAND_VENDOR];  ///< 随机商人状态数组
            EncounterState GongEvent;                 ///< 锣事件状态

            /**
             * @brief 玩家进入副本时的处理
             * @param player 进入副本的玩家（未使用）
             *
             * 当玩家进入副本时，召唤哈里森·琼斯NPC
             */
            void OnPlayerEnter(Player* /*player*/) override
            {
                if (!GetGuidData(NPC_HARRISON_JONES))
                    instance->SummonCreature(NPC_HARRISON_JONES, HarrisonJonesLoc);
            }

            /**
             * @brief 游戏对象创建时的处理
             * @param go 创建的游戏对象
             *
             * 处理特定游戏对象的初始化状态
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_MASSIVE_GATE:
                        // 如果锣事件已完成，打开大门
                        if (GongEvent == DONE)
                            go->SetGoState(GO_STATE_ACTIVE);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 召唤俘虏NPC
             * @param num 俘虏编号（0-3）
             *
             * 在对应Boss被击败后召唤俘虏NPC
             * 俘虏NPC提供对话和任务交互
             *
             * @note 仅在时间挑战模式进行中才会召唤
             */
            void SummonHostage(uint8 num)
            {
                if (!QuestMinute)  // 时间挑战模式未激活时不召唤
                    return;

                Map::PlayerList const& playerList = instance->GetPlayers();
                if (playerList.isEmpty())
                    return;

                if (Player* player = playerList.getFirst()->GetSource())
                {
                    // 召唤俘虏NPC
                    if (Unit* hostage = player->SummonCreature(HostageInfo[num].npc, HostageInfo[num].pos, TEMPSUMMON_DEAD_DESPAWN))
                    {
                        hostage->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 设为不可攻击
                        hostage->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);       // 启用对话交互
                    }
                }
            }

            /**
             * @brief 保存额外的实例数据
             * @param oss 输出字符串流
             *
             * 将锣事件状态、宝箱开启数量、剩余时间保存到数据库
             */
            void WriteSaveDataMore(std::ostringstream& oss) override
            {
                oss << "S " << uint32(GongEvent) << ' '
                    << uint32(ChestLooted) << ' '
                    << uint32(QuestMinute) << ' ';
            }

            /**
             * @brief 读取额外的实例数据
             * @param iss 输入字符串流
             *
             * 从数据库加载实例的额外数据
             *
             * @note 如果数据损坏，记录错误日志
             */
            void ReadSaveDataMore(std::istringstream& iss) override
            {
                char dataHead; // S
                uint32 data1, data2, data3;
                iss >> dataHead >> data1 >> data2 >> data3;

                if (dataHead == 'S')
                {
                    GongEvent = EncounterState(data1);   // 恢复锣事件状态
                    ChestLooted = data2;                 // 恢复宝箱开启数量
                    QuestMinute = data3;                 // 恢复剩余时间
                }
                else
                {
                    TC_LOG_ERROR("scripts", "Zul'aman: corrupted save data.");
                    return;
                }

                // 如果锣事件正在进行中，重置为未开始
                if (GongEvent == IN_PROGRESS)
                    GongEvent = NOT_STARTED;
            }

            /**
             * @brief 设置实例数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 处理锣事件、宝箱开启、随机商人状态等
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_GONGEVENT:
                        // 锣事件状态设置
                        GongEvent = EncounterState(data);
                        if (GongEvent == IN_PROGRESS)
                            SaveToDB();          // 开始时保存
                        else if (GongEvent == DONE)
                            QuestMinute = 21;    // 完成后启动时间挑战（21分钟）
                        break;
                    case DATA_CHESTLOOTED:
                        // 宝箱被开启
                        ++ChestLooted;
                        SaveToDB();              // 保存宝箱开启数量
                        break;
                    case TYPE_RAND_VENDOR_1:
                    case TYPE_RAND_VENDOR_2:
                        // 随机商人状态更新
                        RandVendor[type - TYPE_RAND_VENDOR_1] = EncounterState(data);
                        break;
                }
            }

            /**
             * @brief 设置Boss状态
             * @param id Boss ID
             * @param state Boss状态
             * @return 如果成功设置返回true，否则返回false
             *
             * 处理Boss被击败后的时间挑战奖励和俘虏召唤
             *
             * 时间挑战奖励机制：
             * - 纳洛拉克（熊神）：击败后增加15分钟
             * - 阿基尔松（鹰神）：击败后增加10分钟
             * - 加纳莱和哈拉兹：不增加时间
             * - 每个Boss被击败后召唤对应俘虏
             */
            bool SetBossState(uint32 id, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(id, state))
                    return false;

                switch (id)
                {
                    case BOSS_NALORAKK:
                        // 纳洛拉克被击败
                        if (state == DONE)
                        {
                            if (QuestMinute)
                            {
                                QuestMinute += 15;  // 增加15分钟
                                DoUpdateWorldState(WORLDSTATE_TIME_TO_SACRIFICE, QuestMinute);
                            }
                            SummonHostage(0);  // 召唤熊神俘虏
                        }
                        break;
                    case BOSS_AKILZON:
                        // 阿基尔松被击败
                        if (state == DONE)
                        {
                            if (QuestMinute)
                            {
                                QuestMinute += 10;  // 增加10分钟
                                DoUpdateWorldState(WORLDSTATE_TIME_TO_SACRIFICE, QuestMinute);
                            }
                            SummonHostage(1);  // 召唤鹰神俘虏
                        }
                        break;
                    case BOSS_JANALAI:
                        // 加纳莱被击败
                        if (state == DONE)
                            SummonHostage(2);  // 召唤龙鹰神俘虏
                        break;
                    case BOSS_HALAZZI:
                        // 哈拉兹被击败
                        if (state == DONE)
                            SummonHostage(3);  // 召唤山猫神俘虏
                        break;
                }

                // Boss被击败后的通用处理
                if (state == DONE)
                {
                    // 哈拉兹被击败后，时间挑战模式结束
                    if (QuestMinute && id == BOSS_HALAZZI)
                    {
                        QuestMinute = 0;  // 停止计时
                        DoUpdateWorldState(WORLDSTATE_SHOW_TIMER, 0);
                    }
                    SaveToDB();  // 保存副本进度
                }

                return true;
            }

            /**
             * @brief 获取实例数据
             * @param type 数据类型
             * @return 对应的数据值
             *
             * 查询锣事件状态、宝箱开启数量、随机商人状态等
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_GONGEVENT:
                        return uint32(GongEvent);        // 返回锣事件状态
                    case DATA_CHESTLOOTED:
                        return ChestLooted;              // 返回宝箱开启数量
                    case TYPE_RAND_VENDOR_1:
                    case TYPE_RAND_VENDOR_2:
                        return RandVendor[type - TYPE_RAND_VENDOR_1];  // 返回随机商人状态
                }

                return 0;
            }

            /**
             * @brief 更新实例状态
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 处理时间挑战模式的倒计时逻辑
             * 每分钟更新一次剩余时间并保存到数据库
             */
            void Update(uint32 diff) override
            {
                if (QuestMinute)
                {
                    if (QuestTimer <= diff)
                    {
                        QuestMinute--;         // 减少剩余时间
                        SaveToDB();            // 保存进度
                        QuestTimer += 1 * MINUTE * IN_MILLISECONDS;  // 重置计时器为1分钟

                        if (QuestMinute)
                        {
                            // 还有剩余时间，更新世界状态显示
                            DoUpdateWorldState(WORLDSTATE_SHOW_TIMER, 1);
                            DoUpdateWorldState(WORLDSTATE_TIME_TO_SACRIFICE, QuestMinute);
                        }
                        else
                        {
                            // 时间耗尽，隐藏计时器
                            DoUpdateWorldState(WORLDSTATE_SHOW_TIMER, 0);
                        }
                    }
                    QuestTimer -= diff;
                }
            }
        };

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_zulaman_InstanceMapScript(map);
        }
};

void AddSC_instance_zulaman()
{
    new instance_zulaman();
}
