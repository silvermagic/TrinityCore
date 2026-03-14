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
 * @file    instance_dire_maul.cpp
 * @brief   厄运之槌（Dire Maul）副本实例脚本
 *
 * 本文件实现了厄运之槌副本的实例管理逻辑，主要功能包括：
 * - 副本内Boss和游戏对象的状态追踪
 * - 水晶激活机制的管理（西区Immol'thar事件）
 * - 力场监狱的动态控制
 * - Boss之间的交互逻辑（Immol'thar与Prince Tortheldrin）
 *
 * 厄运之槌副本结构：
 * - 东区（East）：Pusillin、Lethtendris、Hydrospawn、Zevrim Thornhoof、Alzzin the Wildshaper
 * - 西区（West）：Tendris Warpwood、Magister Kalendris、Tsu'zee、Illyanna Ravenoak、Immol'thar、Prince Tortheldrin
 * - 北区（North）：Guard Mol'dar、Stomper Kreeg、Guard Fengus、Guard Slip'kik、Captain Kromcrush、King Gordok
 *
 * 西区特殊机制：
 * 玩家需要击杀5块水晶周围的守护怪物来激活水晶，
 * 所有水晶激活后，禁锢Immol'thar的力场将消失，允许玩家与Immol'thar战斗。
 * 击败Immol'thar后，Prince Tortheldrin将变为敌对状态，玩家可以与他战斗。
 *
 * @note 本实例脚本的占位符对于随机副本系统是必需的，
 *       它允许系统在最终Boss被击杀后正确给予奖励（战利品袋子），
 *       避免玩家获得逃兵Debuff
 */

#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "diremaul.h"

/**
 * @brief Boss遭遇战编号映射（副本内部使用）
 *
 * 厄运之槌副本共有23个遭遇战，包括Boss和特殊机制事件。
 * 编号0-16为Boss，17-22为西区水晶机制事件。
 */

// 东区Boss（East Wing）
// 0 - Pusillin（普希林）- 恶魔小鬼，追逐事件
// 1 - Lethtendris（莱瑟德里斯）- 精灵术士
// 2 - Hydrospawn（海多斯博恩）- 水元素
// 3 - Zevrim Thornhoof（泽维姆·荆蹄）- 萨特
// 4 - Alzzin the Wildshaper（奥兹恩·变身者）- 东区最终Boss

// 西区Boss（West Wing）
// 5 - Tendris Warpwood（特迪斯·扭木）- 古树
// 6 - Magister Kalendris（卡雷迪斯）- 暗夜精灵法师
// 7 - Tsu'zee（苏斯）- 精灵盗贼
// 8 - Illyanna Ravenoak（伊琳娜·暗木）- 精灵猎人
// 9 - Immol'thar（伊莫塔尔）- 恶魔，被力场囚禁
// 10 - Prince Tortheldrin（托塞德林王子）- 西区最终Boss

// 西区水晶机制事件（特殊机制）
// 17 - CRYSTAL_01（第一块水晶）
// 18 - CRYSTAL_02（第二块水晶）
// 19 - CRYSTAL_03（第三块水晶）
// 20 - CRYSTAL_04（第四块水晶）
// 21 - CRYSTAL_05（第五块水晶）
// 22 - FORCEFIELD（力场监狱）

// 北区Boss（North Wing）
// 11 - Guard Mol'dar（莫达卫士）
// 12 - Stomper Kreeg（克里格）
// 13 - Guard Fengus（芬古斯卫士）
// 14 - Guard Slip'kik（斯里基克卫士）
// 15 - Captain Kromcrush（克鲁什上尉）
// 16 - King Gordok（戈多克大王）- 北区最终Boss

/// 副本中遭遇战的总数量（包括Boss和特殊事件）
uint8 const EncounterCount = 23;

/// 水晶守护怪物类型数组（奥术畸变体和法力残渣）
uint32 const CrystalMobs[2] = { NPC_ARCANE_ABERRATION, NPC_MANA_REMNANT };

/**
 * @enum Events
 * @brief 实例脚本内部事件类型
 *
 * 定义用于管理水晶守护怪物检查的事件类型。
 */
enum Events
{
    EVENT_CRYSTAL_CREATURE_STORE                = 1,  ///< 存储水晶周围怪物事件 - 在副本初始化时存储守护怪物GUID
    EVENT_CRYSTAL_CREATURE_CHECK                = 2   ///< 检查水晶怪物状态事件 - 定期检查守护怪物是否全部死亡
};

/**
 * @class instance_dire_maul
 * @brief 厄运之槌副本实例脚本主类
 *
 * 继承自InstanceMapScript，负责管理厄运之槌副本的整体逻辑。
 * 副本地图ID为429。
 *
 * 主要职责：
 * - 初始化和管理副本状态
 * - 提供实例脚本实例化接口
 */
class instance_dire_maul : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称和地图ID。地图ID 429对应厄运之槌副本。
     */
    instance_dire_maul() : InstanceMapScript("instance_dire_maul", 429) { }

    /**
     * @class instance_dire_maul_InstanceMapScript
     * @brief 厄运之槌副本实例脚本实现类
     *
     * 继承自InstanceScript，实现副本的具体管理逻辑。
     *
     * 主要功能：
     * - 管理Immol'thar和Prince Tortheldrin的交互
     * - 控制水晶激活机制
     * - 监控力场监狱状态
     * - 定期检查水晶守护怪物的存活状态
     */
    struct instance_dire_maul_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化副本脚本，设置数据头和Boss数量。
         */
        instance_dire_maul_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);      // 设置数据头标识符，用于序列化
            SetBossNumber(EncounterCount); // 设置Boss遭遇战数量
        }

        /**
         * @brief 生物创建时的回调函数
         * @param creature 新创建的生物指针
         *
         * 当副本中的生物被创建时调用，用于记录关键Boss的GUID并设置初始状态。
         *
         * 处理逻辑：
         * - Immol'thar：记录GUID，如果力场未破坏则设为不可攻击状态
         *   （防止玩家通过宠物将其拉出力场范围）
         * - Prince Tortheldrin：记录GUID，用于Immol'thar死亡后的交互
         *
         * @note 关于Immol'thar的不可攻击状态：
         *       这是为了防止玩家在力场存在时用宠物将Boss拉出力场。
         *       虽然这不是完美的解决方案（理想情况下应该使用移动地图支持门），
         *       但在当前移动地图不支持门的情况下这是必要的临时措施。
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_IMMOLTHAR:
                    _immoGUID = creature->GetGUID();
                    // 将Immol'thar设为不可攻击状态，防止玩家用宠物将其拉出力场
                    // 注意：这不是完美的解决方案，理想情况下mmaps应该支持门
                    if (GetBossState(DATA_FORCEFIELD) != DONE)
                        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    break;
                case NPC_TORTHELDRIN:
                    _tortheldrinGUID = creature->GetGUID();
                break;
                default:
                    break;
            }
        }

        /**
         * @brief 游戏对象创建时的回调函数
         * @param go 新创建的游戏对象指针
         *
         * 当副本中的游戏对象被创建时调用，用于记录水晶和力场的GUID，
         * 并启动水晶守护怪物的监控机制。
         *
         * 处理逻辑：
         * - 5块水晶：分别记录GUID到数组中
         * - 力场监狱：记录GUID，如果力场未破坏则启动怪物存储事件
         *
         * @note 力场监狱的检查机制：
         *       当力场存在时，会启动定时器来存储并监控周围守护怪物，
         *       一旦所有守护怪物被击杀，相应的水晶将被激活。
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_CRYSTAL_01:
                    _crystalGUIDs[0] = go->GetGUID();
                    break;
                case GO_CRYSTAL_02:
                    _crystalGUIDs[1] = go->GetGUID();
                    break;
                case GO_CRYSTAL_03:
                    _crystalGUIDs[2] = go->GetGUID();
                    break;
                case GO_CRYSTAL_04:
                    _crystalGUIDs[3] = go->GetGUID();
                    break;
                case GO_CRYSTAL_05:
                    _crystalGUIDs[4] = go->GetGUID();
                    break;
                case GO_FORCEFIELD:
                    _forcefieldGUID = go->GetGUID();
                    // 如果力场未破坏，启动水晶守护怪物的监控
                    if (GetBossState(DATA_FORCEFIELD) != DONE)
                        _events.ScheduleEvent(EVENT_CRYSTAL_CREATURE_STORE, 1s);
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 获取指定类型对象的GUID
         * @param type 对象类型（NPC ID或游戏对象ID）
         * @return ObjectGuid 返回对应对象的GUID，如果不存在则返回空GUID
         *
         * 提供副本内关键对象的GUID查询接口，供其他脚本使用。
         *
         * 支持的对象类型：
         * - 5块水晶（GO_CRYSTAL_01~05）
         * - 力场监狱（GO_FORCEFIELD）
         * - Immol'thar（NPC_IMMOLTHAR）
         * - Prince Tortheldrin（NPC_TORTHELDRIN）
         *
         * @note 此函数通常在其他脚本（如Boss AI）中被调用，
         *       用于获取特定游戏对象或生物的引用。
         */
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case GO_CRYSTAL_01:
                    return _crystalGUIDs[0];
                case GO_CRYSTAL_02:
                    return _crystalGUIDs[1];
                case GO_CRYSTAL_03:
                    return _crystalGUIDs[2];
                case GO_CRYSTAL_04:
                    return _crystalGUIDs[3];
                case GO_CRYSTAL_05:
                    return _crystalGUIDs[4];
                case GO_FORCEFIELD:
                    return _forcefieldGUID;
                case NPC_IMMOLTHAR:
                    return _immoGUID;
                case NPC_TORTHELDRIN:
                    return _tortheldrinGUID;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        /**
         * @brief 实例脚本更新函数
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 每个游戏Tick调用一次，用于处理定时事件。
         *
         * 事件处理流程：
         * - EVENT_CRYSTAL_CREATURE_STORE：存储水晶周围守护怪物，延迟3秒后开始检查
         * - EVENT_CRYSTAL_CREATURE_CHECK：检查守护怪物状态，如果力场未破坏则每秒检查一次
         *
         * @note 性能考虑：
         *       检查间隔为1秒，避免过于频繁的检查影响性能。
         *       一旦力场被破坏，检查事件将不再被调度。
         */
        void Update(uint32 diff) override
        {
            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CRYSTAL_CREATURE_STORE:
                        CrystalCreatureStore();
                        _events.ScheduleEvent(EVENT_CRYSTAL_CREATURE_CHECK, 3s);
                        break;
                    case EVENT_CRYSTAL_CREATURE_CHECK:
                        CrystalCreatureCheck();
                        // 如果力场未破坏，继续调度检查事件
                        if ((GetBossState(DATA_FORCEFIELD) != DONE))
                            _events.ScheduleEvent(EVENT_CRYSTAL_CREATURE_CHECK, 1s);
                        break;
                    default:
                         break;
                }
            }
        }

        /**
         * @brief 存储水晶周围守护怪物的GUID
         *
         * 遍历所有5块水晶，搜索每块水晶周围30码范围内的守护怪物，
         * 并将它们的GUID存储到数组中，用于后续的存活状态检查。
         *
         * 存储逻辑：
         * - 遍历5块水晶
         * - 对每块水晶搜索两种守护怪物（奥术畸变体和法力残渣）
         * - 将找到的怪物GUID存入对应的数组位置
         *
         * @note 每块水晶最多存储4个守护怪物GUID
         *       搜索半径为30码，这是基于水晶周围怪物的实际分布范围
         *
         * @see CrystalCreatureCheck() 用于检查存储的怪物是否全部死亡
         */
        void CrystalCreatureStore()
        {
            for (uint8 i = 0; i < 5; ++i) // 遍历所有5块水晶
            {
                uint8 creatureCount = 0;

                if (GameObject* crystal = instance->GetGameObject(_crystalGUIDs[i]))
                {
                    // 对每种守护怪物类型进行搜索
                    for (uint8 j = 0; j < 2; ++j)
                    {
                        std::list<Creature*> creatureList;
                        // 在水晶周围30码范围内搜索守护怪物
                        GetCreatureListWithEntryInGrid(creatureList, crystal, CrystalMobs[j], 30.0f);
                        for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
                        {
                            _crystalCreatureGUIDs[i][creatureCount] = (*itr)->GetGUID();
                            ++creatureCount;
                        }
                    }
                }
            }
        }

        /**
         * @brief 检查水晶守护怪物的存活状态
         *
         * 定期检查每块水晶周围的守护怪物，如果某块水晶的所有守护怪物都已死亡，
         * 则激活该水晶。当所有5块水晶都被激活后，解除对Immol'thar的力场囚禁。
         *
         * 检查流程：
         * 1. 遍历所有水晶
         * 2. 检查水晶状态是否为GO_STATE_READY（未激活状态）
         * 3. 检查该水晶周围存储的所有守护怪物是否死亡
         * 4. 如果全部死亡，激活该水晶并设置相应的遭遇战状态为DONE
         * 5. 检查是否所有水晶都已激活，如果是，解除力场并移除Immol'thar的不可攻击标记
         *
         * 激活效果：
         * - 单个水晶：设置游戏对象状态为GO_STATE_ACTIVE（视觉上激活）
         * - 所有水晶：移除力场监狱，使Immol'thar可被攻击
         *
         * @note 此函数由Update()中的定时事件每秒调用一次，
         *       直到力场被破坏为止。这种轮询方式确保即使怪物在
         *       脚本外被击杀，也能正确触发水晶激活。
         *
         * @see CrystalCreatureStore() 怪物GUID存储函数
         */
        void CrystalCreatureCheck()
        {
            for (uint8 i = 0; i < _crystalGUIDs.size(); ++i)
            {
                bool _mobAlive = false;
                GameObject* go = instance->GetGameObject(_crystalGUIDs[i]);
                if (!go)
                    continue;

                // 只检查处于GO_STATE_READY状态的水晶（未激活）
                if (go->GetGoState() == GO_STATE_READY)
                {
                    // 检查该水晶周围所有存储的守护怪物
                    for (uint8 j = 0; j < _crystalCreatureGUIDs[i].size(); ++j)
                    {
                        Creature* mob = instance->GetCreature(_crystalCreatureGUIDs[i][j]);
                        if (mob && mob->IsAlive())
                        {
                            _mobAlive = true;
                            break;
                        }
                    }
                }

                // 如果所有守护怪物都死亡且水晶未激活，则激活该水晶
                if (!_mobAlive && go->GetGoState() == GO_STATE_READY)
                {
                    HandleGameObject(ObjectGuid::Empty, false, go);

                    // 根据水晶ID设置相应的遭遇战状态
                    switch (go->GetEntry())
                    {
                        case GO_CRYSTAL_01:
                            SetBossState(DATA_CRYSTAL_01, DONE);
                            go->SetGoState(GO_STATE_ACTIVE);
                            break;
                        case GO_CRYSTAL_02:
                            SetBossState(DATA_CRYSTAL_02, DONE);
                            go->SetGoState(GO_STATE_ACTIVE);
                            break;
                        case GO_CRYSTAL_03:
                            SetBossState(DATA_CRYSTAL_03, DONE);
                            go->SetGoState(GO_STATE_ACTIVE);
                            break;
                        case GO_CRYSTAL_04:
                            SetBossState(DATA_CRYSTAL_04, DONE);
                            go->SetGoState(GO_STATE_ACTIVE);
                            break;
                        case GO_CRYSTAL_05:
                            SetBossState(DATA_CRYSTAL_05, DONE);
                            go->SetGoState(GO_STATE_ACTIVE);
                            break;
                        default:
                            break;
                    }
                }
            }

            // 检查是否所有水晶都已激活
            if (GetBossState(DATA_CRYSTAL_01) == DONE && GetBossState(DATA_CRYSTAL_02) == DONE && GetBossState(DATA_CRYSTAL_03) == DONE &&
                GetBossState(DATA_CRYSTAL_04) == DONE && GetBossState(DATA_CRYSTAL_05) == DONE)
            {
                // 设置力场遭遇战为完成状态
                SetBossState(DATA_FORCEFIELD, DONE);
                // 激活力场使其消失
                if (GameObject* ffield = instance->GetGameObject(_forcefieldGUID))
                    ffield->SetGoState(GO_STATE_ACTIVE);
                // 移除Immol'thar的不可攻击标记，允许玩家攻击
                if (Creature* immo = instance->GetCreature(_immoGUID))
                    immo->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            }
        }

        /**
         * @brief 单位死亡时的回调函数
         * @param unit 死亡的单位指针
         *
         * 当副本中的任何单位死亡时调用，用于处理Boss之间的交互逻辑。
         *
         * 处理逻辑：
         * - 当Immol'thar死亡时，将Prince Tortheldrin的阵营设为敌对，
         *   允许玩家与他进行战斗。
         *
         * @note 这是厄运之槌西区的特殊机制：
         *       Prince Tortheldrin原本是中立NPC，
         *       只有在Immol'thar被击杀后才会变为敌对状态。
         *       这符合魔兽世界的背景故事：Tortheldrin守护着囚禁Immol'thar的监狱。
         */
        void OnUnitDeath(Unit* unit) override
        {
            if (unit->GetGUID() == _immoGUID)
            {
                // Immol'thar死亡后，Prince Tortheldrin变为敌对
                if (Creature* tortheldrin = instance->GetCreature(_tortheldrinGUID))
                    tortheldrin->SetFaction(FACTION_ENEMY);
            }
        }

protected:
        EventMap _events;                                          ///< 事件管理器，用于调度定时事件
        std::array<ObjectGuid, 5> _crystalGUIDs;                   ///< 5块水晶的GUID数组
        std::array<std::array<ObjectGuid, 4>, 5> _crystalCreatureGUIDs; ///< 每块水晶周围的守护怪物GUID（5块水晶，每块最多4个怪物）
        ObjectGuid _forcefieldGUID;                                ///< 力场监狱的GUID
        ObjectGuid _immoGUID;                                      ///< Immol'thar的GUID
        ObjectGuid _tortheldrinGUID;                               ///< Prince Tortheldrin的GUID
    };

    /**
     * @brief 创建实例脚本实例
     * @param map 副本地图指针
     * @return InstanceScript* 返回新创建的实例脚本实例
     *
     * 工厂函数，当副本被创建时由核心调用，用于生成实例脚本对象。
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_dire_maul_InstanceMapScript(map);
    }
};

/**
 * @brief 注册厄运之槌副本实例脚本
 *
 * 此函数在服务器启动时被脚本加载器调用，用于注册厄运之槌副本脚本。
 * 该函数在 kalimdor_script_loader.cpp 中通过 AddSC_instance_dire_maul() 被引用。
 */
void AddSC_instance_dire_maul()
{
    new instance_dire_maul();
}
