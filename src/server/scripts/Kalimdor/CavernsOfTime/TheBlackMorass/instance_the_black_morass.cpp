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
 * @file    instance_the_black_morass.cpp
 * @brief   黑色沼泽副本实例脚本
 * @details 该模块实现了黑色沼泽副本的实例数据管理，包括：
 *          - 时间裂隙事件系统：管理18个波次的裂隙刷新
 *          - 麦迪文护盾系统：追踪护盾百分比
 *          - 传送门系统：控制时间裂隙的刷新位置
 *          - BOSS战触发机制：在第6、12、18个传送门后刷新BOSS
 *          - 任务支持：支持任务9836、10297
 *
 *          副本机制：
 *          - 麦迪文会在副本中央开启时间裂隙
 *          - 玩家需要防守18波裂隙攻击
 *          - 每6波会出现一个BOSS
 *          - 如果麦迪文死亡或护盾耗尽，副本失败
 *
 *          BOSS顺序：
 *          1. 时光领主德贾（Chrono Lord Deja）- 第6个传送门后
 *          2. 坦波鲁斯（Temporus）- 第12个传送门后
 *          3. 埃欧努斯（Aeonus）- 第18个传送门后
 *
 * Name: Instance_The_Black_Morass
 * %Complete: 50
 * Comment: Quest support: 9836, 10297. Currently in progress.
 * Category: Caverns of Time, The Black Morass
 */

#include "ScriptMgr.h"
#include "EventMap.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "the_black_morass.h"

/**
 * @brief 杂项枚举和常量定义
 */
enum Misc
{
    SPELL_RIFT_CHANNEL = 31387,  ///< 裂隙引导法术，裂隙对BOSS施放
    RIFT_BOSS         = 1        ///< 表示随机裂隙BOSS（Rift Lord或Rift Keeper）
};

/**
 * @brief 随机选择裂隙BOSS类型
 * @return 返回NPC_RIFT_KEEPER或NPC_RIFT_LORD
 */
inline uint32 RandRiftBoss() { return ((rand32() % 2) ? NPC_RIFT_KEEPER : NPC_RIFT_LORD); }

/**
 * @brief 传送门刷新位置数组
 * @details 定义了4个可能的传送门刷新位置，每次随机选择一个位置
 *          格式：{X坐标, Y坐标, Z坐标, 朝向}
 */
float PortalLocation[4][4]=
{
    {-2041.06f, 7042.08f, 29.99f, 1.30f},   ///< 传送门位置1
    {-1968.18f, 7042.11f, 21.93f, 2.12f},   ///< 传送门位置2
    {-1885.82f, 7107.36f, 22.32f, 3.07f},   ///< 传送门位置3
    {-1928.11f, 7175.95f, 22.11f, 3.44f}    ///< 传送门位置4
};

/**
 * @struct Wave
 * @brief 波次数据结构
 * @details 定义每个波次的BOSS类型和下一个传送门的时间
 */
struct Wave
{
    uint32 PortalBoss;              ///< 当前传送门的守护BOSS（保护传送门的小BOSS）
    Milliseconds NextPortalTime;    ///< 下一个传送门的时间，为0表示需要先击杀当前BOSS
};

/**
 * @brief 裂隙波次数组
 * @details 定义了整个副本的波次顺序：
 *          - 第1-5波：普通裂隙BOSS
 *          - 第6波：时光领主德贾
 *          - 第7-11波：普通裂隙BOSS
 *          - 第12波：坦波鲁斯
 *          - 第13-17波：普通裂隙BOSS
 *          - 第18波：埃欧努斯
 */
static Wave RiftWaves[]=
{
    { RIFT_BOSS,             0s },  ///< 波次0：裂隙BOSS，等待击杀
    { NPC_CRONO_LORD_DEJA,   0s },  ///< 波次1：时光领主德贾（第6个传送门）
    { RIFT_BOSS,           120s },  ///< 波次2：裂隙BOSS，120秒后刷新下一个
    { NPC_TEMPORUS,        140s },  ///< 波次3：坦波鲁斯（第12个传送门）
    { RIFT_BOSS,           120s },  ///< 波次4：裂隙BOSS，120秒后刷新下一个
    { NPC_AEONUS,            0s }   ///< 波次5：埃欧努斯（第18个传送门）
};

/**
 * @brief 事件ID枚举
 */
enum EventIds
{
    EVENT_NEXT_PORTAL = 1  ///< 下一个传送门事件
};

/**
 * @class   instance_the_black_morass
 * @brief   黑色沼泽副本实例脚本
 * @details 管理黑色沼泽副本的整体状态，包括裂隙事件、麦迪文护盾、BOSS刷新等
 */
class instance_the_black_morass : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * @details 注册黑色沼泽副本脚本，地图ID为269
     */
    instance_the_black_morass() : InstanceMapScript(TBMScriptName, 269) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 返回黑色沼泽实例脚本实例
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_the_black_morass_InstanceMapScript(map);
    }

    /**
     * @struct  instance_the_black_morass_InstanceMapScript
     * @brief   黑色沼泽实例脚本实现
     * @details 继承自InstanceScript，实现黑色沼泽副本的所有实例管理功能
     */
    struct instance_the_black_morass_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         * @details 初始化副本脚本，设置数据头和清理状态
         */
        instance_the_black_morass_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            Clear();
        }

        uint32 m_auiEncounter[EncounterCount];  ///< 遭遇状态数组

        uint32 mRiftPortalCount;   ///< 已刷新的传送门数量（总共18个）
        uint32 mShieldPercent;     ///< 麦迪文护盾百分比（初始100%）
        uint8  mRiftWaveCount;     ///< 裂隙波次计数
        uint8  mRiftWaveId;        ///< 当前波次ID（用于确定BOSS类型）

        ObjectGuid _medivhGUID;    ///< 麦迪文的GUID
        uint8  _currentRiftId;     ///< 当前传送门位置ID（0-3）

        /**
         * @brief 清理所有状态
         * @details 重置所有状态变量为初始值
         */
        void Clear()
        {
            memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

            mRiftPortalCount    = 0;    // 传送门计数重置
            mShieldPercent      = 100;  // 护盾初始为100%
            mRiftWaveCount      = 0;    // 波次计数重置
            mRiftWaveId         = 0;    // 波次ID重置

            _currentRiftId      = 0;    // 当前传送门位置重置
        }

        /**
         * @brief 初始化世界状态
         * @param Enable 是否启用（默认为true）
         * @details 更新客户端的世界状态显示
         */
        void InitWorldState(bool Enable = true)
        {
            DoUpdateWorldState(WORLD_STATE_BM, Enable ? 1 : 0);         // 副本事件是否启用
            DoUpdateWorldState(WORLD_STATE_BM_SHIELD, 100);             // 护盾百分比
            DoUpdateWorldState(WORLD_STATE_BM_RIFT, 0);                 // 传送门数量
        }

        /**
         * @brief 检查是否有战斗正在进行
         * @return 如果麦迪文事件正在进行返回true，否则返回false
         */
        bool IsEncounterInProgress() const override
        {
            if (GetData(TYPE_MEDIVH) == IN_PROGRESS)
                return true;

            return false;
        }

        /**
         * @brief 玩家进入副本回调
         * @param player 进入副本的玩家
         * @details 如果事件未开始，关闭世界状态显示
         */
        void OnPlayerEnter(Player* player) override
        {
            if (GetData(TYPE_MEDIVH) == IN_PROGRESS)
                return;

            // 如果事件未开始，发送关闭状态
            player->SendUpdateWorldState(WORLD_STATE_BM, 0);
        }

        /**
         * @brief 生物创建回调
         * @param creature 生物指针
         * @details 记录麦迪文的GUID
         */
        void OnCreatureCreate(Creature* creature) override
        {
            if (creature->GetEntry() == NPC_MEDIVH)
                _medivhGUID = creature->GetGUID();
        }

        /**
         * @brief 检查事件是否可以继续
         * @return 如果可以继续返回true，否则返回false
         * @details 检查副本中是否有玩家
         */
        bool CanProgressEvent()
        {
            // 如果副本中没有玩家，则不能继续
            if (instance->GetPlayers().isEmpty())
                return false;

            return true;
        }

        /**
         * @brief 获取裂隙波次ID
         * @return 当前波次ID
         * @details 根据传送门数量确定波次ID：
         *          - 第6个传送门：返回波次1（时光领主德贾）
         *          - 第12个传送门：返回波次3（坦波鲁斯）
         *          - 第18个传送门：返回波次5（埃欧努斯）
         */
        uint8 GetRiftWaveId()
        {
            switch (mRiftPortalCount)
            {
            case 6:
                mRiftWaveId = 2;  // 设置下一个波次ID
                return 1;         // 返回时光领主德贾的波次
            case 12:
                mRiftWaveId = 4;  // 设置下一个波次ID
                return 3;         // 返回坦波鲁斯的波次
            case 18:
                return 5;         // 返回埃欧努斯的波次
            default:
                return mRiftWaveId;
            }
        }

        /**
         * @brief 设置数据
         * @param type 数据类型
         * @param data 数据值
         * @details 设置副本中的各种数据，包括麦迪文状态、裂隙状态等
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
            case TYPE_MEDIVH:
                // 麦迪文事件状态
                if (data == SPECIAL && m_auiEncounter[0] == IN_PROGRESS)
                {
                    // 护盾受损，减少护盾百分比
                    --mShieldPercent;

                    // 更新客户端护盾显示
                    DoUpdateWorldState(WORLD_STATE_BM_SHIELD, mShieldPercent);

                    // 如果护盾耗尽
                    if (!mShieldPercent)
                    {
                        if (Creature* medivh = instance->GetCreature(_medivhGUID))
                        {
                            if (medivh->IsAlive())
                            {
                                // 麦迪文死亡，事件失败
                                medivh->KillSelf();
                                m_auiEncounter[0] = FAIL;
                                m_auiEncounter[1] = NOT_STARTED;
                            }
                        }
                    }
                }
                else
                {
                    if (data == IN_PROGRESS)
                    {
                        // 事件开始
                        TC_LOG_DEBUG("scripts", "Instance The Black Morass: Starting event.");
                        InitWorldState();
                        m_auiEncounter[1] = IN_PROGRESS;
                        // 15秒后刷新第一个传送门
                        ScheduleEventNextPortal(15s);
                    }

                    if (data == DONE)
                    {
                        // 事件完成（所有BOSS击杀）
                        TC_LOG_DEBUG("scripts", "Instance The Black Morass: Event completed.");
                        Map::PlayerList const& players = instance->GetPlayers();

                        if (!players.isEmpty())
                        {
                            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                            {
                                if (Player* player = itr->GetSource())
                                {
                                    // 更新任务进度
                                    if (player->GetQuestStatus(QUEST_OPENING_PORTAL) == QUEST_STATUS_INCOMPLETE)
                                        player->AreaExploredOrEventHappens(QUEST_OPENING_PORTAL);

                                    if (player->GetQuestStatus(QUEST_MASTER_TOUCH) == QUEST_STATUS_INCOMPLETE)
                                        player->AreaExploredOrEventHappens(QUEST_MASTER_TOUCH);
                                }
                            }
                        }
                    }

                    m_auiEncounter[0] = data;
                }
                break;

            case TYPE_RIFT:
                // 裂隙事件状态
                if (data == SPECIAL)
                {
                    // 裂隙BOSS被击杀，准备下一个传送门
                    if (mRiftPortalCount < 7)
                        ScheduleEventNextPortal(5s);
                }
                else
                    m_auiEncounter[1] = data;
                break;
            }
        }

        /**
         * @brief 获取数据
         * @param type 数据类型
         * @return 对应的数据值
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
            case TYPE_MEDIVH:    return m_auiEncounter[0];  // 麦迪文事件状态
            case TYPE_RIFT:      return m_auiEncounter[1];  // 裂隙事件状态
            case DATA_PORTAL_COUNT: return mRiftPortalCount; // 传送门数量
            case DATA_SHIELD:    return mShieldPercent;     // 护盾百分比
            }
            return 0;
        }

        /**
         * @brief 获取GUID数据
         * @param data 数据类型
         * @return 对应的GUID
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            if (data == DATA_MEDIVH)
                return _medivhGUID;

            return ObjectGuid::Empty;
        }

        /**
         * @brief 召唤传送门BOSS
         * @param me 传送门NPC（召唤者）
         * @return 返回召唤的BOSS指针，失败返回nullptr
         * @details 根据当前波次决定召唤的BOSS类型：
         *          - 普通波次：随机裂隙守护者或裂隙领主
         *          - BOSS波次：时光领主德贾、坦波鲁斯或埃欧努斯
         */
        Creature* SummonedPortalBoss(Creature* me)
        {
            // 获取当前波次的BOSS类型
            uint32 entry = RiftWaves[GetRiftWaveId()].PortalBoss;

            // 如果是随机裂隙BOSS，则随机选择类型
            if (entry == RIFT_BOSS)
                entry = RandRiftBoss();

            TC_LOG_DEBUG("scripts", "Instance The Black Morass: Summoning rift boss entry {}.", entry);

            // 在传送门附近随机位置召唤BOSS
            Position pos = me->GetRandomNearPosition(10.0f);

            // 标准化Z坐标，确保BOSS在地面上
            pos.m_positionZ = std::max(me->GetMap()->GetHeight(pos.m_positionX, pos.m_positionY, MAX_HEIGHT), me->GetMap()->GetWaterLevel(pos.m_positionX, pos.m_positionY));

            // 召唤BOSS，如果死亡则10分钟后消失
            if (Creature* summon = me->SummonCreature(entry, pos, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 10min))
                return summon;

            TC_LOG_DEBUG("scripts", "Instance The Black Morass: What just happened there? No boss, no loot, no fun...");
            return nullptr;
        }

        /**
         * @brief 刷新传送门
         * @details 在随机位置刷新时间裂隙和对应的BOSS
         */
        void DoSpawnPortal()
        {
            if (Creature* medivh = instance->GetCreature(_medivhGUID))
            {
                // 随机选择传送门位置（避免与上一个位置相同）
                uint8 tmp = urand(0, 2);

                if (tmp >= _currentRiftId)
                    ++tmp;

                TC_LOG_DEBUG("scripts", "Instance The Black Morass: Creating Time Rift at locationId {} (old locationId was {}).", tmp, _currentRiftId);

                _currentRiftId = tmp;

                // 召唤时间裂隙NPC
                Creature* temp = medivh->SummonCreature(NPC_TIME_RIFT,
                    PortalLocation[tmp][0], PortalLocation[tmp][1], PortalLocation[tmp][2], PortalLocation[tmp][3],
                    TEMPSUMMON_CORPSE_DESPAWN);

                if (temp)
                {
                    // 召唤传送门BOSS
                    if (Creature* boss = SummonedPortalBoss(temp))
                    {
                        // 如果是埃欧努斯，直接对麦迪文建立仇恨
                        if (boss->GetEntry() == NPC_AEONUS)
                            boss->GetThreatManager().AddThreat(medivh, 0.0f);
                        else
                        {
                            // 普通裂隙BOSS对传送门建立仇恨，并让传送门引导裂隙法术
                            boss->GetThreatManager().AddThreat(temp, 0.0f);
                            temp->CastSpell(boss, SPELL_RIFT_CHANNEL, false);
                        }
                    }
                }
            }
        }

        /**
         * @brief 更新实例状态
         * @param diff 距离上次更新的时间差（毫秒）
         * @details 处理传送门刷新逻辑
         */
        void Update(uint32 diff) override
        {
            // 如果裂隙事件未开始，直接返回
            if (m_auiEncounter[1] != IN_PROGRESS)
                return;

            // 检查是否可以继续（是否有玩家）
            if (!CanProgressEvent())
            {
                Clear();
                return;
            }

            // 更新事件调度器
            Events.Update(diff);

            // 检查是否需要刷新下一个传送门
            if (Events.ExecuteEvent() == EVENT_NEXT_PORTAL)
            {
                ++mRiftPortalCount;
                // 更新客户端传送门数量显示
                DoUpdateWorldState(WORLD_STATE_BM_RIFT, mRiftPortalCount);
                // 刷新传送门
                DoSpawnPortal();
                // 调度下一个传送门
                ScheduleEventNextPortal(RiftWaves[GetRiftWaveId()].NextPortalTime);
            }
        }

        /**
         * @brief 调度下一个传送门事件
         * @param nextPortalTime 下一个传送门的时间间隔
         */
        void ScheduleEventNextPortal(Milliseconds nextPortalTime)
        {
            if (nextPortalTime > 0s)
                Events.RescheduleEvent(EVENT_NEXT_PORTAL, nextPortalTime);
        }

        protected:
            EventMap Events;  ///< 事件调度器
    };

};

/**
 * @brief 添加黑色沼泽实例脚本到系统
 */
void AddSC_instance_the_black_morass()
{
    new instance_the_black_morass();
}
