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
 * @file    instance_karazhan.cpp
 * @brief   卡拉赞副本实例脚本
 * @details 实现卡拉赞副本的实例管理功能,包括:
 *          - Boss状态管理(12个Boss战斗状态)
 *          - 游戏对象管理(门、箱子、瓮等)
 *          - 歌剧事件管理(绿野仙踪/小红帽/罗密欧与朱丽叶)
 *          - 可选Boss生成机制(击杀小怪后随机刷新)
 *          - 实例数据存储和查询
 */

/* ScriptData
SDName: Instance_Karazhan
SD%Complete: 70
SDComment: Instance Script for Karazhan to help in various encounters. @todo GameObject visibility for Opera event.
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "karazhan.h"
#include "Map.h"

/**
 * @brief Boss编号索引说明
 * @details 卡拉赞副本的Boss编号和是否可选:
 *          0  - Attumen + Midnight (可选)
 *          1  - Moroes (莫罗斯)
 *          2  - Maiden of Virtue (可选)
 *          3  - Hyakiss the Lurker / Rokad the Ravager / Shadikith the Glider (可选Boss)
 *          4  - Opera Event (歌剧事件)
 *          5  - Curator (馆长)
 *          6  - Shade of Aran (可选)
 *          7  - Terestian Illhoof (可选)
 *          8  - Netherspite (可选)
 *          9  - Chess Event (象棋事件)
 *          10 - Prince Malchezzar (麦克扎尔王子)
 *          11 - Nightbane (夜之魇)
 */

/**
 * @brief 可选Boss生成位置
 * @details 定义了三个可选Boss的生成坐标:
 *          - Hyakiss the Lurker (潜伏者海雅克丝)
 *          - Shadikith the Glider (滑翔者沙迪基斯)
 *          - Rokad the Ravager (掠夺者罗卡德)
 */
const Position OptionalSpawn[] =
{
    { -10960.981445f, -1940.138428f, 46.178097f, 4.12f  }, ///< Hyakiss the Lurker 生成位置
    { -10945.769531f, -2040.153320f, 49.474438f, 0.077f }, ///< Shadikith the Glider 生成位置
    { -10899.903320f, -2085.573730f, 49.474449f, 1.38f  }  ///< Rokad the Ravager 生成位置
};

/**
 * @class instance_karazhan
 * @brief 卡拉赞副本实例脚本类
 * @details 注册和管理卡拉赞副本实例的AI和状态
 */
class instance_karazhan : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * @details 注册卡拉赞副本(地图ID: 532)
     */
    instance_karazhan() : InstanceMapScript(KZScriptName, 532) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 实例脚本指针
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_karazhan_InstanceMapScript(map);
    }

    /**
     * @struct instance_karazhan_InstanceMapScript
     * @brief 卡拉赞实例脚本实现结构体
     * @details 管理副本中的Boss状态、游戏对象GUID、事件进度等
     */
    struct instance_karazhan_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         * @details 初始化副本状态,设置Boss数量,随机选择歌剧事件
         */
        instance_karazhan_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);

            // 1 - OZ(绿野仙踪), 2 - HOOD(小红帽), 3 - RAJ(罗密欧与朱丽叶), 这个值一旦生成就不会改变
            OperaEvent = urand(EVENT_OZ, EVENT_RAJ);
            OzDeathCount = 0;
            OptionalBossCount = 0;
        }

        /**
         * @brief 生物创建时调用
         * @param creature 新创建的生物对象
         * @details 保存关键Boss的GUID,用于后续查询和交互
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_KILREK:
                    KilrekGUID = creature->GetGUID(); ///< 保存克雷克(伊利霍夫的小鬼)的GUID
                    break;
                case NPC_TERESTIAN_ILLHOOF:
                    TerestianGUID = creature->GetGUID(); ///< 保存特雷斯坦·伊利霍夫的GUID
                    break;
                case NPC_MOROES:
                    MoroesGUID = creature->GetGUID(); ///< 保存莫罗斯的GUID
                    break;
                case NPC_NIGHTBANE:
                    NightbaneGUID = creature->GetGUID(); ///< 保存夜之魇的GUID
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 单位死亡时调用
         * @param unit 死亡的单位
         * @details 处理小怪死亡计数,用于触发可选Boss生成;
         *          以及可选Boss死亡时的状态更新
         */
        void OnUnitDeath(Unit* unit) override
        {
            Creature* creature = unit->ToCreature();
            if (!creature)
                return;

            switch (creature->GetEntry())
            {
                case NPC_COLDMIST_WIDOW:      ///< 寒雾寡妇
                case NPC_COLDMIST_STALKER:    ///< 寒雾潜伏者
                case NPC_SHADOWBAT:           ///< 阴影蝙蝠
                case NPC_VAMPIRIC_SHADOWBAT:  ///< 吸血阴影蝙蝠
                case NPC_GREATER_SHADOWBAT:   ///< 巨型阴影蝙蝠
                case NPC_PHASE_HOUND:         ///< 相位猎犬
                case NPC_DREADBEAST:          ///< 恐惧野兽
                case NPC_SHADOWBEAST:         ///< 阴影野兽
                    // 如果可选Boss尚未触发
                    if (GetBossState(DATA_OPTIONAL_BOSS) == TO_BE_DECIDED)
                    {
                        ++OptionalBossCount;
                        // 击杀足够数量的小怪后,随机生成一个可选Boss
                        if (OptionalBossCount == OPTIONAL_BOSS_REQUIRED_DEATH_COUNT)
                        {
                            switch (urand(NPC_HYAKISS_THE_LURKER, NPC_ROKAD_THE_RAVAGER))
                            {
                                case NPC_HYAKISS_THE_LURKER:
                                    instance->SummonCreature(NPC_HYAKISS_THE_LURKER, OptionalSpawn[0]);
                                    break;
                                case NPC_SHADIKITH_THE_GLIDER:
                                    instance->SummonCreature(NPC_SHADIKITH_THE_GLIDER, OptionalSpawn[1]);
                                    break;
                                case NPC_ROKAD_THE_RAVAGER:
                                    instance->SummonCreature(NPC_ROKAD_THE_RAVAGER, OptionalSpawn[2]);
                                    break;
                            }
                        }
                    }
                    break;
                case NPC_HYAKISS_THE_LURKER:   ///< 潜伏者海雅克丝
                case NPC_SHADIKITH_THE_GLIDER: ///< 滑翔者沙迪基斯
                case NPC_ROKAD_THE_RAVAGER:    ///< 掠夺者罗卡德
                    // 可选Boss被击杀,标记为完成
                    SetBossState(DATA_OPTIONAL_BOSS, DONE);
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 设置实例数据
         * @param type 数据类型
         * @param data 数据值
         * @details 主要用于歌剧事件中的死亡计数
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_OPERA_OZ_DEATHCOUNT:
                    if (data == SPECIAL)
                        ++OzDeathCount;
                    else if (data == IN_PROGRESS)
                        OzDeathCount = 0;
                    break;
            }
        }

        /**
         * @brief 设置Boss状态
         * @param type Boss数据类型
         * @param state 遭遇战状态
         * @return 状态设置是否成功
         * @details 处理Boss状态变更时的副作用:
         *          - 歌剧事件完成时打开舞台门和侧门
         *          - 象棋事件完成时刷新尘封的箱子
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_OPERA_PERFORMANCE:
                    if (state == DONE)
                    {
                        HandleGameObject(StageDoorLeftGUID, true);
                        HandleGameObject(StageDoorRightGUID, true);
                        if (GameObject* sideEntrance = instance->GetGameObject(SideEntranceDoor))
                            sideEntrance->RemoveFlag(GO_FLAG_LOCKED);
                        UpdateEncounterStateForKilledCreature(16812, nullptr);
                    }
                    break;
                case DATA_CHESS:
                    if (state == DONE)
                        DoRespawnGameObject(DustCoveredChest, 24h);
                    break;
                default:
                    break;
            }

            return true;
        }

        /**
         * @brief 设置GUID数据
         * @param type 数据类型
         * @param data GUID数据
         * @details 用于保存麦迪文影像的GUID
         */
         void SetGuidData(uint32 type, ObjectGuid data) override
         {
             if (type == DATA_IMAGE_OF_MEDIVH)
                 ImageGUID = data;
         }

        /**
         * @brief 游戏对象创建时调用
         * @param go 新创建的游戏对象
         * @details 保存各种门、箱子、瓮等游戏对象的GUID,
         *          并根据Boss状态设置初始状态
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_STAGE_CURTAIN:
                    CurtainGUID = go->GetGUID();  ///< 舞台幕布
                    break;
                case GO_STAGE_DOOR_LEFT:
                    StageDoorLeftGUID = go->GetGUID();  ///< 左侧舞台门
                    if (GetBossState(DATA_OPERA_PERFORMANCE) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                case GO_STAGE_DOOR_RIGHT:
                    StageDoorRightGUID = go->GetGUID();  ///< 右侧舞台门
                    if (GetBossState(DATA_OPERA_PERFORMANCE) == DONE)
                        go->SetGoState(GO_STATE_ACTIVE);
                    break;
                case GO_PRIVATE_LIBRARY_DOOR:
                    LibraryDoor = go->GetGUID();  ///< 私人图书馆门(埃兰之影处)
                    break;
                case GO_MASSIVE_DOOR:
                    MassiveDoor = go->GetGUID();  ///< 巨型门(虚空幽龙处)
                    break;
                case GO_GAMESMAN_HALL_DOOR:
                    GamesmansDoor = go->GetGUID();  ///< 游戏厅门(象棋前)
                    break;
                case GO_GAMESMAN_HALL_EXIT_DOOR:
                    GamesmansExitDoor = go->GetGUID();  ///< 游戏厅出口门(象棋后)
                    break;
                case GO_NETHERSPACE_DOOR:
                    NetherspaceDoor = go->GetGUID();  ///< 虚空之门(玛尔加尼斯处)
                    break;
                case GO_MASTERS_TERRACE_DOOR:
                    MastersTerraceDoor[0] = go->GetGUID();  ///< 主人露台门1
                    break;
                case GO_MASTERS_TERRACE_DOOR2:
                    MastersTerraceDoor[1] = go->GetGUID();  ///< 主人露台门2
                    break;
                case GO_SIDE_ENTRANCE_DOOR:
                    SideEntranceDoor = go->GetGUID();  ///< 侧门入口
                    if (GetBossState(DATA_OPERA_PERFORMANCE) == DONE)
                        go->SetFlag(GO_FLAG_LOCKED);
                    else
                        go->RemoveFlag(GO_FLAG_LOCKED);
                    break;
                case GO_DUST_COVERED_CHEST:
                    DustCoveredChest = go->GetGUID();  ///< 尘封的箱子(象棋奖励)
                    break;
                case GO_BLACKENED_URN:
                    BlackenedUrnGUID = go->GetGUID();  ///< 熏黑的瓮(召唤夜之魇)
                    break;
            }

            // 根据歌剧事件类型设置游戏对象可见性(待实现)
            switch (OperaEvent)
            {
                /// @todo 根据表演类型设置对象可见性
                case EVENT_OZ:      ///< 绿野仙踪
                    break;

                case EVENT_HOOD:    ///< 小红帽
                    break;

                case EVENT_RAJ:     ///< 罗密欧与朱丽叶
                    break;
            }
        }

        /**
         * @brief 获取实例数据
         * @param type 数据类型
         * @return 对应的数据值
         * @details 主要用于获取歌剧事件类型和死亡计数
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_OPERA_PERFORMANCE:
                    return OperaEvent;       ///< 返回歌剧事件类型
                case DATA_OPERA_OZ_DEATHCOUNT:
                    return OzDeathCount;     ///< 返回绿野仙踪死亡计数
            }

            return 0;
        }

        /**
         * @brief 获取GUID数据
         * @param type 数据类型
         * @return 对应的GUID
         * @details 根据类型返回相应的生物或游戏对象的GUID
         */
        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_KILREK:
                    return KilrekGUID;           ///< 克雷克(伊利霍夫的小鬼)
                case DATA_TERESTIAN:
                    return TerestianGUID;        ///< 特雷斯坦·伊利霍夫
                case DATA_MOROES:
                    return MoroesGUID;           ///< 莫罗斯
                case DATA_NIGHTBANE:
                    return NightbaneGUID;        ///< 夜之魇
                case DATA_GO_STAGEDOORLEFT:
                    return StageDoorLeftGUID;    ///< 左侧舞台门
                case DATA_GO_STAGEDOORRIGHT:
                    return StageDoorRightGUID;   ///< 右侧舞台门
                case DATA_GO_CURTAINS:
                    return CurtainGUID;          ///< 舞台幕布
                case DATA_GO_LIBRARY_DOOR:
                    return LibraryDoor;          ///< 私人图书馆门
                case DATA_GO_MASSIVE_DOOR:
                    return MassiveDoor;          ///< 巨型门
                case DATA_GO_SIDE_ENTRANCE_DOOR:
                    return SideEntranceDoor;     ///< 侧门入口
                case DATA_GO_GAME_DOOR:
                    return GamesmansDoor;        ///< 游戏厅门
                case DATA_GO_GAME_EXIT_DOOR:
                    return GamesmansExitDoor;    ///< 游戏厅出口门
                case DATA_GO_NETHER_DOOR:
                    return NetherspaceDoor;      ///< 虚空之门
                case DATA_MASTERS_TERRACE_DOOR_1:
                    return MastersTerraceDoor[0]; ///< 主人露台门1
                case DATA_MASTERS_TERRACE_DOOR_2:
                    return MastersTerraceDoor[1]; ///< 主人露台门2
                case DATA_IMAGE_OF_MEDIVH:
                    return ImageGUID;            ///< 麦迪文影像
                case DATA_GO_BLACKENED_URN:
                    return BlackenedUrnGUID;     ///< 熏黑的瓮
            }

            return ObjectGuid::Empty;
        }

    private:
        uint32 OperaEvent;          ///< 歌剧事件类型(绿野仙踪/小红帽/罗密欧与朱丽叶)
        uint32 OzDeathCount;        ///< 绿野仙踪事件中的死亡计数
        uint32 OptionalBossCount;   ///< 可选Boss触发所需的小怪击杀计数

        ObjectGuid CurtainGUID;           ///< 舞台幕布GUID
        ObjectGuid StageDoorLeftGUID;     ///< 左侧舞台门GUID
        ObjectGuid StageDoorRightGUID;    ///< 右侧舞台门GUID

        ObjectGuid KilrekGUID;            ///< 克雷克(伊利霍夫的小鬼)GUID
        ObjectGuid TerestianGUID;         ///< 特雷斯坦·伊利霍夫GUID
        ObjectGuid MoroesGUID;            ///< 莫罗斯GUID
        ObjectGuid NightbaneGUID;         ///< 夜之魇GUID

        ObjectGuid LibraryDoor;           ///< 私人图书馆门(埃兰之影处)
        ObjectGuid MassiveDoor;           ///< 巨型门(虚空幽龙处)
        ObjectGuid SideEntranceDoor;      ///< 侧门入口
        ObjectGuid GamesmansDoor;         ///< 游戏厅门(象棋前)
        ObjectGuid GamesmansExitDoor;     ///< 游戏厅出口门(象棋后)
        ObjectGuid NetherspaceDoor;       ///< 虚空之门(玛尔加尼斯处)
        ObjectGuid MastersTerraceDoor[2]; ///< 主人露台门(2个)
        ObjectGuid ImageGUID;             ///< 麦迪文影像GUID
        ObjectGuid DustCoveredChest;      ///< 尘封的箱子GUID(象棋奖励)
        ObjectGuid BlackenedUrnGUID;      ///< 熏黑的瓮GUID(召唤夜之魇)
    };
};

/**
 * @brief 注册卡拉赞副本实例脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建卡拉赞副本实例脚本，注册到脚本系统
 */
void AddSC_instance_karazhan()
{
    new instance_karazhan();
}
