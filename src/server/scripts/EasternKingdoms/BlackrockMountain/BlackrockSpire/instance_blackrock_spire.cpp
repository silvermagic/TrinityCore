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
 * @file instance_blackrock_spire.cpp
 * @brief 黑石塔副本实例脚本
 *
 * 本模块实现了黑石塔副本(包括上层和下层)的实例管理逻辑。
 * 黑石塔是魔兽世界中一个大型副本,分为上层(LBRS)和下层(UBRS)两部分。
 *
 * 主要功能:
 * - 管理 BOSS 状态和击杀记录
 * - 控制门和机关的开关
 * - 管理龙火大厅的符文机制
 * - 处理区域触发器事件
 * - 协调 BOSS 之间的关联事件
 *
 * 副本结构:
 * - 下层: 包含多个可选 BOSS
 * - 上层: 需要 seal of ascension 才能进入
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"

//uint32 const DragonspireRunes[7] = { GO_HALL_RUNE_1, GO_HALL_RUNE_2, GO_HALL_RUNE_3, GO_HALL_RUNE_4, GO_HALL_RUNE_5, GO_HALL_RUNE_6, GO_HALL_RUNE_7 };

/// 龙火大厅刷出的怪物类型
uint32 const DragonspireMobs[3] = { NPC_BLACKHAND_DREADWEAVER, NPC_BLACKHAND_SUMMONER, NPC_BLACKHAND_VETERAN };

/**
 * @brief 门数据结构数组
 *
 * 定义了副本中各个门与 BOSS 的关联关系。
 * 当 BOSS 被击杀或状态改变时,相应的门会自动打开或关闭。
 */
DoorData const doorData[] =
{
    { GO_DOORS,                  DATA_PYROGAURD_EMBERSEER,     DOOR_TYPE_ROOM },      ///< 炉石守护者灰烬者的房间门
    { GO_EMBERSEER_OUT,          DATA_PYROGAURD_EMBERSEER,     DOOR_TYPE_PASSAGE },   ///< 灰烬者后面的通道门
    { GO_DRAKKISATH_DOOR_1,      DATA_GENERAL_DRAKKISATH,      DOOR_TYPE_PASSAGE },   ///< 达基萨斯将军的门1
    { GO_DRAKKISATH_DOOR_2,      DATA_GENERAL_DRAKKISATH,      DOOR_TYPE_PASSAGE },   ///< 达基萨斯将军的门2
    { GO_PORTCULLIS_ACTIVE,      DATA_WARCHIEF_REND_BLACKHAND, DOOR_TYPE_PASSAGE },   ///< 雷德·黑手的栅栏门
    { GO_PORTCULLIS_TOBOSSROOMS, DATA_WARCHIEF_REND_BLACKHAND, DOOR_TYPE_PASSAGE },   ///< 通往 BOSS 房间的栅栏门
    { 0,                         0,                            DOOR_TYPE_ROOM    }    ///< 数组结束标记
};

/**
 * @brief 实例事件 ID 枚举
 */
enum EventIds
{
    EVENT_DARGONSPIRE_ROOM_STORE           = 1,    ///< 存储龙火大厅怪物事件
    EVENT_DARGONSPIRE_ROOM_CHECK           = 2,    ///< 检查龙火大厅状态事件
    EVENT_UROK_DOOMHOWL_SPAWNS_1           = 3,    ///< 乌洛克生成事件1
    EVENT_UROK_DOOMHOWL_SPAWNS_2           = 4,    ///< 乌洛克生成事件2
    EVENT_UROK_DOOMHOWL_SPAWNS_3           = 5,    ///< 乌洛克生成事件3
    EVENT_UROK_DOOMHOWL_SPAWNS_4           = 6,    ///< 乌洛克生成事件4
    EVENT_UROK_DOOMHOWL_SPAWNS_5           = 7,    ///< 乌洛克生成事件5
    EVENT_UROK_DOOMHOWL_SPAWN_IN           = 8     ///< 乌洛克入场事件
};

/**
 * @brief 黑石塔实例脚本类
 *
 * 继承自 InstanceMapScript,实现了黑石塔副本的实例管理逻辑。
 * 负责管理副本状态、BOSS 数据、游戏对象和区域触发器。
 */
class instance_blackrock_spire : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册黑石塔实例脚本,地图 ID 为 229。
     */
    instance_blackrock_spire() : InstanceMapScript(BRSScriptName, 229) { }

    /**
     * @brief 黑石塔实例脚本实现结构体
     *
     * 继承自 InstanceScript,实现具体的实例管理逻辑。
     */
    struct instance_blackrock_spireMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 实例地图指针
         *
         * 初始化实例数据:
         * - 设置数据头标识
         * - 设置 BOSS 数量
         * - 加载门数据
         */
        instance_blackrock_spireMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadDoorData(doorData);
        }

        /**
         * @brief 生物创建时的处理
         * @param creature 创建的生物指针
         *
         * 当生物在副本中创建时调用。
         * 保存所有 BOSS 和重要 NPC 的 GUID,以便后续使用。
         *
         * 特殊处理:
         * - 炉石守护者灰烬者: 如果已被击杀,则不生成
         * - 大酋长雷德·黑手: 如果 Gyth 已被击杀,则不生成
         * - 维克多·耐法里奥斯: 如果 Gyth 已被击杀,则不生成
         * - 芬克尔·艾因霍恩: 生成时说出台词
         * - 黑手监工: 添加到监工列表
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_HIGHLORD_OMOKK:
                    HighlordOmokk = creature->GetGUID();
                    break;
                case NPC_SHADOW_HUNTER_VOSHGAJIN:
                    ShadowHunterVoshgajin = creature->GetGUID();
                    break;
                case NPC_WARMASTER_VOONE:
                    WarMasterVoone = creature->GetGUID();
                    break;
                case NPC_MOTHER_SMOLDERWEB:
                    MotherSmolderweb = creature->GetGUID();
                    break;
                case NPC_UROK_DOOMHOWL:
                    UrokDoomhowl = creature->GetGUID();
                    break;
                case NPC_QUARTERMASTER_ZIGRIS:
                    QuartermasterZigris = creature->GetGUID();
                    break;
                case NPC_GIZRUL_THE_SLAVENER:
                    GizrultheSlavener = creature->GetGUID();
                    break;
                case NPC_HALYCON:
                    Halycon = creature->GetGUID();
                    break;
                case NPC_OVERLORD_WYRMTHALAK:
                    OverlordWyrmthalak = creature->GetGUID();
                    break;
                case NPC_PYROGAURD_EMBERSEER:
                    PyroguardEmberseer = creature->GetGUID();
                    // 如果已被击杀,则消失
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        creature->DespawnOrUnsummon(0s, 7_days);
                    break;
                case NPC_WARCHIEF_REND_BLACKHAND:
                    WarchiefRendBlackhand = creature->GetGUID();
                    // 如果 Gyth 已被击杀,则消失
                    if (GetBossState(DATA_GYTH) == DONE)
                        creature->DespawnOrUnsummon(0s, 7_days);
                    break;
                case NPC_GYTH:
                    Gyth = creature->GetGUID();
                    break;
                case NPC_THE_BEAST:
                    TheBeast = creature->GetGUID();
                    break;
                case NPC_GENERAL_DRAKKISATH:
                    GeneralDrakkisath = creature->GetGUID();
                    break;
                case NPC_LORD_VICTOR_NEFARIUS:
                    LordVictorNefarius = creature->GetGUID();
                    // 如果 Gyth 已被击杀,则消失
                    if (GetBossState(DATA_GYTH) == DONE)
                        creature->DespawnOrUnsummon(0s, 7_days);
                    break;
                case NPC_SCARSHIELD_INFILTRATOR:
                    ScarshieldInfiltrator = creature->GetGUID();
                    break;
                case NPC_FINKLE_EINHORN:
                    // 芬克尔·艾因霍恩生成时说出台词
                    creature->AI()->Talk(SAY_FINKLE_GANG);
                    break;
                case NPC_BLACKHAND_INCARCERATOR:
                    // 将黑手监工添加到列表
                    _incarceratorList.push_back(creature->GetGUID());
                    break;
             }
         }

        /**
         * @brief 游戏对象创建时的处理
         * @param go 创建的游戏对象指针
         *
         * 当游戏对象在副本中创建时调用。
         * 保存所有重要游戏对象的 GUID,并根据 BOSS 状态设置其开关状态。
         *
         * 特殊处理:
         * - 幼龙生成器: 自动施放召唤法术
         * - 门和符文: 根据 BOSS 状态自动打开或关闭
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_WHELP_SPAWNER:
                    // 幼龙生成器自动施放召唤法术
                    go->CastSpell(nullptr, SPELL_SUMMON_ROOKERY_WHELP);
                    break;
                case GO_EMBERSEER_IN:
                    go_emberseerin = go->GetGUID();
                    // 如果龙火大厅已完成,则打开门
                    if (GetBossState(DATA_DRAGONSPIRE_ROOM) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_DOORS:
                    go_doors = go->GetGUID();
                    // 如果龙火大厅已完成,则打开门
                    if (GetBossState(DATA_DRAGONSPIRE_ROOM) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_EMBERSEER_OUT:
                    go_emberseerout = go->GetGUID();
                    // 如果灰烬者已被击杀,则打开门
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_HALL_RUNE_1:
                    go_roomrunes[0] = go->GetGUID();
                    // 如果符文已完成,则关闭符文
                    if (GetBossState(DATA_HALL_RUNE_1) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_2:
                    go_roomrunes[1] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_2) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_3:
                    go_roomrunes[2] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_3) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_4:
                    go_roomrunes[3] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_4) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_5:
                    go_roomrunes[4] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_5) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_6:
                    go_roomrunes[5] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_6) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_HALL_RUNE_7:
                    go_roomrunes[6] = go->GetGUID();
                    if (GetBossState(DATA_HALL_RUNE_7) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_1:
                    go_emberseerrunes[0] = go->GetGUID();
                    // 如果灰烬者已被击杀,则关闭符文
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_2:
                    go_emberseerrunes[1] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_3:
                    go_emberseerrunes[2] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_4:
                    go_emberseerrunes[3] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_5:
                    go_emberseerrunes[4] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_6:
                    go_emberseerrunes[5] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_EMBERSEER_RUNE_7:
                    go_emberseerrunes[6] = go->GetGUID();
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == DONE)
                        HandleGameObject(ObjectGuid::Empty, false, go);
                    break;
                case GO_PORTCULLIS_ACTIVE:
                    go_portcullis_active = go->GetGUID();
                    // 如果 Gyth 已被击杀,则打开栅栏
                    if (GetBossState(DATA_GYTH) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_PORTCULLIS_TOBOSSROOMS:
                    go_portcullis_tobossrooms = go->GetGUID();
                    // 如果 Gyth 已被击杀,则打开栅栏
                    if (GetBossState(DATA_GYTH) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 设置 BOSS 状态
         * @param type BOSS 类型 ID
         * @param state 新的 BOSS 状态
         * @return 是否设置成功
         *
         * 当 BOSS 状态改变时调用。
         * 调用基类的实现来保存状态和处理门。
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_HIGHLORD_OMOKK:
                case DATA_SHADOW_HUNTER_VOSHGAJIN:
                case DATA_WARMASTER_VOONE:
                case DATA_MOTHER_SMOLDERWEB:
                case DATA_UROK_DOOMHOWL:
                case DATA_QUARTERMASTER_ZIGRIS:
                case DATA_GIZRUL_THE_SLAVENER:
                case DATA_HALYCON:
                case DATA_OVERLORD_WYRMTHALAK:
                case DATA_PYROGAURD_EMBERSEER:
                case DATA_WARCHIEF_REND_BLACKHAND:
                case DATA_GYTH:
                case DATA_THE_BEAST:
                case DATA_GENERAL_DRAKKISATH:
                case DATA_DRAGONSPIRE_ROOM:
                    break;
                default:
                    break;
            }

             return true;
        }

        /**
         * @brief 处理事件
         * @param obj 触发事件的对象(未使用)
         * @param eventId 事件 ID
         *
         * 处理副本中的特殊事件。
         *
         * 支持的事件:
         * - EVENT_PYROGUARD_EMBERSEER: 激活灰烬者战斗
         * - EVENT_UROK_DOOMHOWL: 乌洛克召唤事件(未实现)
         */
        void ProcessEvent(WorldObject* /*obj*/, uint32 eventId) override
        {
            switch (eventId)
            {
                case EVENT_PYROGUARD_EMBERSEER:
                    // 激活灰烬者战斗
                    if (GetBossState(DATA_PYROGAURD_EMBERSEER) == NOT_STARTED)
                    {
                        if (Creature* Emberseer = instance->GetCreature(PyroguardEmberseer))
                            Emberseer->AI()->SetData(1, 1);
                    }
                    break;
                case EVENT_UROK_DOOMHOWL:
                    // 乌洛克召唤事件(未实现)
                    if (GetBossState(NPC_UROK_DOOMHOWL) == NOT_STARTED)
                    {

                    }
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 设置自定义数据
         * @param type 数据类型
         * @param data 数据值
         *
         * 处理自定义数据设置。
         *
         * 支持的数据类型:
         * - AREATRIGGER: 区域触发器事件
         *   - AREATRIGGER_DRAGONSPIRE_HALL: 龙火大厅触发器
         * - DATA_BLACKHAND_INCARCERATOR: 重生所有黑手监工
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case AREATRIGGER:
                    if (data == AREATRIGGER_DRAGONSPIRE_HALL)
                    {
                        // 如果龙火大厅未完成,则开始检测机制
                        if (GetBossState(DATA_DRAGONSPIRE_ROOM) != DONE)
                            Events.ScheduleEvent(EVENT_DARGONSPIRE_ROOM_STORE, 1s);
                    }
                    break;
                case DATA_BLACKHAND_INCARCERATOR:
                    // 重生所有黑手监工
                    for (GuidList::const_iterator itr = _incarceratorList.begin(); itr != _incarceratorList.end(); ++itr)
                        if (Creature* creature = instance->GetCreature(*itr))
                            creature->Respawn();
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 获取 GUID 数据
         * @param type 数据类型
         * @return 对应的 GUID
         *
         * 根据 ID 返回保存的 BOSS 或游戏对象的 GUID。
         */
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_HIGHLORD_OMOKK:
                    return HighlordOmokk;
                case DATA_SHADOW_HUNTER_VOSHGAJIN:
                    return ShadowHunterVoshgajin;
                case DATA_WARMASTER_VOONE:
                    return WarMasterVoone;
                case DATA_MOTHER_SMOLDERWEB:
                    return MotherSmolderweb;
                case DATA_UROK_DOOMHOWL:
                    return UrokDoomhowl;
                case DATA_QUARTERMASTER_ZIGRIS:
                    return QuartermasterZigris;
                case DATA_GIZRUL_THE_SLAVENER:
                    return GizrultheSlavener;
                case DATA_HALYCON:
                    return Halycon;
                case DATA_OVERLORD_WYRMTHALAK:
                    return OverlordWyrmthalak;
                case DATA_PYROGAURD_EMBERSEER:
                    return PyroguardEmberseer;
                case DATA_WARCHIEF_REND_BLACKHAND:
                    return WarchiefRendBlackhand;
                case DATA_GYTH:
                    return Gyth;
                case DATA_THE_BEAST:
                    return TheBeast;
                case DATA_GENERAL_DRAKKISATH:
                    return GeneralDrakkisath;
                case DATA_SCARSHIELD_INFILTRATOR:
                    return ScarshieldInfiltrator;
                case GO_EMBERSEER_IN:
                    return go_emberseerin;
                case GO_DOORS:
                    return go_doors;
                case GO_EMBERSEER_OUT:
                    return go_emberseerout;
                case GO_HALL_RUNE_1:
                    return go_roomrunes[0];
                case GO_HALL_RUNE_2:
                    return go_roomrunes[1];
                case GO_HALL_RUNE_3:
                    return go_roomrunes[2];
                case GO_HALL_RUNE_4:
                    return go_roomrunes[3];
                case GO_HALL_RUNE_5:
                    return go_roomrunes[4];
                case GO_HALL_RUNE_6:
                    return go_roomrunes[5];
                case GO_HALL_RUNE_7:
                    return go_roomrunes[6];
                case GO_EMBERSEER_RUNE_1:
                    return go_emberseerrunes[0];
                case GO_EMBERSEER_RUNE_2:
                    return go_emberseerrunes[1];
                case GO_EMBERSEER_RUNE_3:
                    return go_emberseerrunes[2];
                case GO_EMBERSEER_RUNE_4:
                    return go_emberseerrunes[3];
                case GO_EMBERSEER_RUNE_5:
                    return go_emberseerrunes[4];
                case GO_EMBERSEER_RUNE_6:
                    return go_emberseerrunes[5];
                case GO_EMBERSEER_RUNE_7:
                    return go_emberseerrunes[6];
                case GO_PORTCULLIS_ACTIVE:
                    return go_portcullis_active;
                case GO_PORTCULLIS_TOBOSSROOMS:
                    return go_portcullis_tobossrooms;
                default:
                    break;
            }
            return ObjectGuid::Empty;
        }

        /**
         * @brief 更新实例逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每个游戏循环周期调用,处理实例中的定时事件。
         *
         * 执行流程:
         * 1. 更新事件计时器
         * 2. 处理到期的事件
         *
         * 支持的事件:
         * - EVENT_DARGONSPIRE_ROOM_STORE: 存储龙火大厅的怪物列表
         * - EVENT_DARGONSPIRE_ROOM_CHECK: 检查龙火大厅的怪物状态
         */
        void Update(uint32 diff) override
        {
            Events.Update(diff);

            while (uint32 eventId = Events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_DARGONSPIRE_ROOM_STORE:
                        // 存储龙火大厅的怪物列表
                        Dragonspireroomstore();
                        // 3秒后检查状态
                        Events.ScheduleEvent(EVENT_DARGONSPIRE_ROOM_CHECK, 3s);
                        break;
                    case EVENT_DARGONSPIRE_ROOM_CHECK:
                        // 检查龙火大厅的状态
                        Dragonspireroomcheck();
                        // 如果未完成,则继续检查
                        if ((GetBossState(DATA_DRAGONSPIRE_ROOM) != DONE))
                            Events.ScheduleEvent(EVENT_DARGONSPIRE_ROOM_CHECK, 3s);
                        break;
                    default:
                         break;
                }
            }
        }

        /**
         * @brief 存储龙火大厅的怪物列表
         *
         * 遍历所有 7 个符文,查找每个符文周围 15 码范围内的怪物。
         * 将怪物的 GUID 保存到列表中,用于后续的状态检查。
         *
         * 性能注意事项:
         * - 使用网格搜索查找怪物,效率较高
         * - 只在触发区域触发器时执行一次
         */
        void Dragonspireroomstore()
        {
            for (uint8 i = 0; i < 7; ++i)
            {
                // 清空怪物列表
                runecreaturelist[i].clear();

                if (GameObject* rune = instance->GetGameObject(go_roomrunes[i]))
                {
                    // 查找 3 种类型的怪物
                    for (uint8 j = 0; j < 3; ++j)
                    {
                        std::list<Creature*> creatureList;
                        GetCreatureListWithEntryInGrid(creatureList, rune, DragonspireMobs[j], 15.0f);
                        for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
                        {
                            if (Creature* creature = *itr)
                                runecreaturelist[i].push_back(creature->GetGUID());
                        }
                    }
                }
            }
        }

        /**
         * @brief 检查龙火大厅的状态
         *
         * 遍历所有 7 个符文,检查每个符文周围的怪物是否已被击杀。
         * 如果某个符文周围的所有怪物都被击杀,则关闭该符文。
         * 当所有 7 个符文都被关闭时,完成龙火大厅事件,打开通往灰烬者的门。
         *
         * 执行逻辑:
         * 1. 检查每个符文是否处于激活状态
         * 2. 检查符文周围是否还有活着的怪物
         * 3. 如果符文激活但怪物已死,则关闭符文并标记为完成
         * 4. 如果所有符文都完成,则打开门
         */
        void Dragonspireroomcheck()
        {
            Creature* mob = nullptr;
            GameObject* rune = nullptr;

            for (uint8 i = 0; i < 7; ++i)
            {
                bool _mobAlive = false;
                rune = instance->GetGameObject(go_roomrunes[i]);
                if (!rune)
                    continue;

                // 检查符文是否激活
                if (rune->GetGoState() == GO_STATE_ACTIVE)
                {
                    // 检查符文周围的怪物是否还活着
                    for (ObjectGuid const& guid : runecreaturelist[i])
                    {
                        mob = instance->GetCreature(guid);
                        if (mob && mob->IsAlive())
                            _mobAlive = true;
                    }
                }

                // 如果符文激活但怪物已死,则关闭符文
                if (!_mobAlive && rune->GetGoState() == GO_STATE_ACTIVE)
                {
                    HandleGameObject(ObjectGuid::Empty, false, rune);

                    // 标记对应的符文状态为完成
                    switch (rune->GetEntry())
                    {
                        case GO_HALL_RUNE_1:
                            SetBossState(DATA_HALL_RUNE_1, DONE);
                            break;
                        case GO_HALL_RUNE_2:
                            SetBossState(DATA_HALL_RUNE_2, DONE);
                            break;
                        case GO_HALL_RUNE_3:
                            SetBossState(DATA_HALL_RUNE_3, DONE);
                            break;
                        case GO_HALL_RUNE_4:
                            SetBossState(DATA_HALL_RUNE_4, DONE);
                            break;
                        case GO_HALL_RUNE_5:
                            SetBossState(DATA_HALL_RUNE_5, DONE);
                            break;
                        case GO_HALL_RUNE_6:
                            SetBossState(DATA_HALL_RUNE_6, DONE);
                            break;
                        case GO_HALL_RUNE_7:
                            SetBossState(DATA_HALL_RUNE_7, DONE);
                            break;
                        default:
                            break;
                    }
                }
            }

            // 如果所有符文都完成,则打开门
            if (GetBossState(DATA_HALL_RUNE_1) == DONE && GetBossState(DATA_HALL_RUNE_2) == DONE && GetBossState(DATA_HALL_RUNE_3) == DONE &&
                GetBossState(DATA_HALL_RUNE_4) == DONE && GetBossState(DATA_HALL_RUNE_5) == DONE && GetBossState(DATA_HALL_RUNE_6) == DONE &&
                GetBossState(DATA_HALL_RUNE_7) == DONE)
            {
                SetBossState(DATA_DRAGONSPIRE_ROOM, DONE);
                // 打开通往灰烬者的门
                if (GameObject* door1 = instance->GetGameObject(go_emberseerin))
                    HandleGameObject(ObjectGuid::Empty, true, door1);
                if (GameObject* door2 = instance->GetGameObject(go_doors))
                    HandleGameObject(ObjectGuid::Empty, true, door2);
            }
        }

        protected:
            EventMap Events;                                ///< 事件管理器
            ObjectGuid HighlordOmokk;                       ///< 大王奥莫克 GUID
            ObjectGuid ShadowHunterVoshgajin;               ///< 暗影猎手沃什加斯 GUID
            ObjectGuid WarMasterVoone;                      ///< 军需官沃恩 GUID
            ObjectGuid MotherSmolderweb;                    ///< 母亲斯莫尔德 WEB GUID
            ObjectGuid UrokDoomhowl;                        ///< 乌洛克 GUID
            ObjectGuid QuartermasterZigris;                 ///< 军需官兹格雷斯 GUID
            ObjectGuid GizrultheSlavener;                   ///< 奴役者基兹卢尔 GUID
            ObjectGuid Halycon;                             ///< 哈雷肯 GUID
            ObjectGuid OverlordWyrmthalak;                  ///< 领主沃尔塔拉斯 GUID
            ObjectGuid PyroguardEmberseer;                  ///< 炉石守护者灰烬者 GUID
            ObjectGuid WarchiefRendBlackhand;               ///< 大酋长雷德·黑手 GUID
            ObjectGuid Gyth;                                ///< 盖斯 GUID
            ObjectGuid LordVictorNefarius;                  ///< 维克多·耐法里奥斯 GUID
            ObjectGuid TheBeast;                            ///< 野兽 GUID
            ObjectGuid GeneralDrakkisath;                   ///< 达基萨斯将军 GUID
            ObjectGuid ScarshieldInfiltrator;               ///< 疤盾渗透者 GUID
            ObjectGuid go_emberseerin;                      ///< 灰烬者入口门 GUID
            ObjectGuid go_doors;                            ///< 门 GUID
            ObjectGuid go_emberseerout;                     ///< 灰烬者出口门 GUID
            ObjectGuid go_blackrockaltar;                   ///< 黑石祭坛 GUID
            ObjectGuid go_roomrunes[7];                     ///< 房间符文数组
            ObjectGuid go_emberseerrunes[7];                ///< 灰烬者符文数组
            GuidVector runecreaturelist[7];                 ///< 符文怪物列表数组
            ObjectGuid go_portcullis_active;                ///< 激活的栅栏门 GUID
            ObjectGuid go_portcullis_tobossrooms;           ///< 通往 BOSS 房间的栅栏门 GUID
            GuidList _incarceratorList;                     ///< 黑手监工 GUID 列表
    };

    /**
     * @brief 获取实例脚本
     * @param map 实例地图指针
     * @return 实例脚本对象指针
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_blackrock_spireMapScript(map);
    }
};

/**
 * @brief 龙火大厅区域触发器脚本
 *
 * 当玩家进入龙火大厅时触发,启动符文检测机制。
 */
class at_dragonspire_hall : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_dragonspire_hall() : AreaTriggerScript("at_dragonspire_hall") { }

    /**
     * @brief 区域触发器触发时的处理
     * @param player 触发区域触发器的玩家
     * @param at 区域触发器数据(未使用)
     * @return 是否成功处理
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (player && player->IsAlive())
        {
            if (InstanceScript* instance = player->GetInstanceScript())
            {
                instance->SetData(AREATRIGGER, AREATRIGGER_DRAGONSPIRE_HALL);
                return true;
            }
        }

        return false;
    }
};

/**
 * @brief 黑石竞技场区域触发器脚本
 *
 * 当玩家进入黑石竞技场时触发,启动雷德·黑手事件。
 */
class at_blackrock_stadium : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_blackrock_stadium() : AreaTriggerScript("at_blackrock_stadium") { }

    /**
     * @brief 区域触发器触发时的处理
     * @param player 触发区域触发器的玩家
     * @param at 区域触发器数据(未使用)
     * @return 是否成功处理
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (player && player->IsAlive())
        {
            InstanceScript* instance = player->GetInstanceScript();
            if (!instance)
                return false;

            // 查找附近的雷德·黑手
            if (Creature* rend = player->FindNearestCreature(NPC_WARCHIEF_REND_BLACKHAND, 50.0f))
            {
                rend->AI()->SetData(AREATRIGGER, AREATRIGGER_BLACKROCK_STADIUM);
                return true;
            }
        }

        return false;
    }
};

/**
 * @brief 疤盾渗透者附近区域触发器脚本
 *
 * 当玩家接近疤盾渗透者时触发,根据玩家等级决定是否开始对话。
 * 玩家等级 >= 57 时,渗透者会开始任务对话。
 */
class at_nearby_scarshield_infiltrator : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_nearby_scarshield_infiltrator() : AreaTriggerScript("at_nearby_scarshield_infiltrator") { }

    /**
     * @brief 区域触发器触发时的处理
     * @param player 触发区域触发器的玩家
     * @param at 区域触发器数据(未使用)
     * @return 是否成功处理
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (player->IsAlive())
        {
            if (InstanceScript* instance = player->GetInstanceScript())
            {
                if (Creature* infiltrator = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_SCARSHIELD_INFILTRATOR)))
                {
                    // 如果玩家等级 >= 57,则开始任务对话
                    if (player->GetLevel() >= 57)
                        infiltrator->AI()->SetData(1, 1);
                    // 否则说出台词
                    else if (infiltrator->GetEntry() == NPC_SCARSHIELD_INFILTRATOR)
                        infiltrator->AI()->Talk(0, player);

                    return true;
                }
            }
        }

        return false;
    }
};

/**
 * @brief 注册实例脚本
 *
 * 将黑石塔实例脚本和区域触发器注册到脚本系统中。
 */
void AddSC_instance_blackrock_spire()
{
    new instance_blackrock_spire();
    new at_dragonspire_hall();
    new at_blackrock_stadium();
    new at_nearby_scarshield_infiltrator();
}
