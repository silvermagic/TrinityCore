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
 * @file    instance_naxxramas.cpp
 * @brief   纳克萨玛斯副本实例脚本
 *
 * 本模块实现了纳克萨玛斯副本的实例管理功能，包括：
 * - 副本内所有首领的边界定义
 * - 门和传送门的状态管理
 * - 副本进度的保存和加载
 * - 首领击杀后的对话和事件触发
 * - 成就判定（不朽者/不死者）
 * - 四大区域的传送门激活管理
 */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "naxxramas.h"
#include "TemporarySummon.h"

/**
 * @brief 首领战斗边界数据
 *
 * 定义了每个首领的战斗区域边界，用于防止首领被拖出战斗区域。
 * 包括圆形、矩形、平行四边形和Z轴范围边界。
 */
BossBoundaryData const boundaries =
{
    /* Arachnid Quarter */
    { BOSS_ANUBREKHAN, new CircleBoundary(Position(3273.376709f, -3475.876709f), Position(3195.668213f, -3475.930176f)) },
    { BOSS_FAERLINA, new RectangleBoundary(3315.0f, 3402.0f, -3727.0f, -3590.0f) },
    { BOSS_FAERLINA, new CircleBoundary(Position(3372.68f, -3648.2f), Position(3316.0f, -3704.26f)) },
    { BOSS_MAEXXNA, new CircleBoundary(Position(3502.2587f, -3892.1697f), Position(3418.7422f, -3840.271f)) },

    /* Plague Quarter */
    { BOSS_NOTH, new RectangleBoundary(2618.0f, 2754.0f, -3557.43f, -3450.0f) },
    { BOSS_HEIGAN, new CircleBoundary(Position(2772.57f, -3685.28f), 56.0f) },
    { BOSS_LOATHEB, new CircleBoundary(Position(2909.0f, -3997.41f), 57.0f) },

    /* Military Quarter */
    { BOSS_RAZUVIOUS, new ZRangeBoundary(260.0f, 287.0f) }, // will not chase onto the upper floor
    { BOSS_GOTHIK, new RectangleBoundary(2627.0f, 2764.0f, -3440.0f, -3275.0f) },
    { BOSS_HORSEMEN, new ParallelogramBoundary(Position(2646.0f, -2959.0f), Position(2529.0f, -3075.0f), Position(2506.0f, -2854.0f)) },

    /* Construct Quarter */
    { BOSS_PATCHWERK, new CircleBoundary(Position(3204.0f, -3241.4f), 240.0f) },
    { BOSS_PATCHWERK, new CircleBoundary(Position(3130.8576f, -3210.36f), Position(3085.37f, -3219.85f), true) }, // entrance slime circle blocker
    { BOSS_GROBBULUS, new CircleBoundary(Position(3204.0f, -3241.4f), 240.0f) },
    { BOSS_GROBBULUS, new RectangleBoundary(3295.0f, 3340.0f, -3254.2f, -3230.18f, true) }, // entrance door blocker
    { BOSS_GLUTH, new CircleBoundary(Position(3293.0f, -3142.0f), 80.0) },
    { BOSS_GLUTH, new ParallelogramBoundary(Position(3401.0f, -3149.0f), Position(3261.0f, -3028.0f), Position(3320.0f, -3267.0f)) },
    { BOSS_GLUTH, new ZRangeBoundary(285.0f, 310.0f) },
    { BOSS_THADDIUS, new ParallelogramBoundary(Position(3478.3f, -3070.0f), Position(3370.0f, -2961.5f), Position(3580.0f, -2961.5f)) },

    /* Frostwyrm Lair */
    { BOSS_SAPPHIRON, new CircleBoundary(Position(3517.627f, -5255.5f), 110.0) },
    { BOSS_KELTHUZAD, new CircleBoundary(Position(3716.0f, -5107.0f), 85.0) }
};

/**
 * @brief 门数据配置
 *
 * 定义了副本内各个门与首领状态的关联。
 * 门会根据对应首领的状态自动开启或关闭。
 * DOOR_TYPE_ROOM: 房间门，首领战斗时关闭
 * DOOR_TYPE_PASSAGE: 通道门，首领死亡后开启
 */
DoorData const doorData[] =
{
    { GO_ROOM_ANUBREKHAN,       BOSS_ANUBREKHAN,    DOOR_TYPE_ROOM },
    { GO_PASSAGE_ANUBREKHAN,    BOSS_ANUBREKHAN,    DOOR_TYPE_PASSAGE },
    { GO_PASSAGE_FAERLINA,      BOSS_FAERLINA,      DOOR_TYPE_PASSAGE },
    { GO_ROOM_MAEXXNA,          BOSS_FAERLINA,      DOOR_TYPE_PASSAGE },
    { GO_ROOM_MAEXXNA,          BOSS_MAEXXNA,       DOOR_TYPE_ROOM },
    { GO_ROOM_NOTH,             BOSS_NOTH,          DOOR_TYPE_ROOM },
    { GO_PASSAGE_NOTH,          BOSS_NOTH,          DOOR_TYPE_PASSAGE },
    { GO_ROOM_HEIGAN,           BOSS_NOTH,          DOOR_TYPE_PASSAGE },
    { GO_ROOM_HEIGAN,           BOSS_HEIGAN,        DOOR_TYPE_ROOM },
    { GO_PASSAGE_HEIGAN,        BOSS_HEIGAN,        DOOR_TYPE_PASSAGE },
    { GO_ROOM_LOATHEB,          BOSS_HEIGAN,        DOOR_TYPE_PASSAGE },
    { GO_ROOM_LOATHEB,          BOSS_LOATHEB,       DOOR_TYPE_ROOM },
    { GO_ROOM_GROBBULUS,        BOSS_PATCHWERK,     DOOR_TYPE_PASSAGE },
    { GO_ROOM_GROBBULUS,        BOSS_GROBBULUS,     DOOR_TYPE_ROOM },
    { GO_PASSAGE_GLUTH,         BOSS_GLUTH,         DOOR_TYPE_PASSAGE },
    { GO_ROOM_THADDIUS,         BOSS_GLUTH,         DOOR_TYPE_PASSAGE },
    { GO_ROOM_THADDIUS,         BOSS_THADDIUS,      DOOR_TYPE_ROOM },
    { GO_ROOM_GOTHIK,           BOSS_RAZUVIOUS,     DOOR_TYPE_PASSAGE },
    { GO_ROOM_GOTHIK,           BOSS_GOTHIK,        DOOR_TYPE_ROOM },
    { GO_PASSAGE_GOTHIK,        BOSS_GOTHIK,        DOOR_TYPE_PASSAGE },
    { GO_ROOM_HORSEMEN,         BOSS_GOTHIK,        DOOR_TYPE_PASSAGE },
    { GO_GOTHIK_GATE,           BOSS_GOTHIK,        DOOR_TYPE_ROOM },
    { GO_ROOM_HORSEMEN,         BOSS_HORSEMEN,      DOOR_TYPE_ROOM },
    { GO_PASSAGE_SAPPHIRON,     BOSS_SAPPHIRON,     DOOR_TYPE_PASSAGE },
    { GO_ROOM_KELTHUZAD,        BOSS_KELTHUZAD,     DOOR_TYPE_ROOM },
    { GO_ARAC_EYE_RAMP,         BOSS_MAEXXNA,       DOOR_TYPE_PASSAGE },
    { GO_ARAC_EYE_RAMP_BOSS,    BOSS_MAEXXNA,       DOOR_TYPE_PASSAGE },
    { GO_PLAG_EYE_RAMP,         BOSS_LOATHEB,       DOOR_TYPE_PASSAGE },
    { GO_PLAG_EYE_RAMP_BOSS,    BOSS_LOATHEB,       DOOR_TYPE_PASSAGE },
    { GO_MILI_EYE_RAMP,         BOSS_HORSEMEN,      DOOR_TYPE_PASSAGE },
    { GO_MILI_EYE_RAMP_BOSS,    BOSS_HORSEMEN,      DOOR_TYPE_PASSAGE },
    { GO_CONS_EYE_RAMP,         BOSS_THADDIUS,      DOOR_TYPE_PASSAGE },
    { GO_CONS_EYE_RAMP_BOSS,    BOSS_THADDIUS,      DOOR_TYPE_PASSAGE },
    { 0,                        0,                  DOOR_TYPE_ROOM }
};

/**
 * @brief 游戏对象数据配置
 *
 * 定义了副本内需要追踪的游戏对象GUID与数据ID的映射。
 */
ObjectData const objectData[] =
{
    { GO_NAXX_PORTAL_ARACHNID,  DATA_NAXX_PORTAL_ARACHNID  },
    { GO_NAXX_PORTAL_CONSTRUCT, DATA_NAXX_PORTAL_CONSTRUCT },
    { GO_NAXX_PORTAL_PLAGUE,    DATA_NAXX_PORTAL_PLAGUE    },
    { GO_NAXX_PORTAL_MILITARY,  DATA_NAXX_PORTAL_MILITARY  },
    { GO_KELTHUZAD_THRONE,      DATA_KELTHUZAD_THRONE      },
    { 0,                        0,                         }
};

/**
 * @class instance_naxxramas
 * @brief 纳克萨玛斯副本实例脚本
 *
 * 负责管理纳克萨玛斯副本的整体状态，包括：
 * - 首领状态管理
 * - 游戏对象（门、传送门）状态
 * - 副本事件调度
 * - 成就判定
 * - 副本数据保存和加载
 */
class instance_naxxramas : public InstanceMapScript
{
    public:
        instance_naxxramas() : InstanceMapScript(NaxxramasScriptName, 533) { }

        /**
         * @class instance_naxxramas_InstanceMapScript
         * @brief 纳克萨玛斯实例脚本实现
         *
         * 继承自InstanceScript，实现纳克萨玛斯副本的具体逻辑
         */
        struct instance_naxxramas_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本实例，设置首领数量、边界和门数据
             */
            instance_naxxramas_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadBossBoundaries(boundaries);
                LoadDoorData(doorData);
                LoadObjectData(nullptr, objectData);

                // 初始化成员变量
                hadSapphironBirth       = false;  // 萨菲隆是否已经出现过（出生动画）
                CurrentWingTaunt        = SAY_KELTHUZAD_FIRST_WING_TAUNT;  // 当前区域嘲讽对话ID

                playerDied              = false;  // 是否有玩家死亡（用于不朽者/不死者成就）
            }

            /**
             * @brief 生物创建时的回调
             * @param creature 新创建的生物
             *
             * 当副本内的生物创建时被调用，用于记录首领和重要NPC的GUID
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_ANUBREKHAN:
                        AnubRekhanGUID = creature->GetGUID();
                        break;
                    case NPC_FAERLINA:
                        FaerlinaGUID = creature->GetGUID();
                        break;
                    case NPC_RAZUVIOUS:
                        RazuviousGUID = creature->GetGUID();
                        break;
                    case NPC_GOTHIK:
                        GothikGUID = creature->GetGUID();
                        break;
                    case NPC_THANE:
                        ThaneGUID = creature->GetGUID();
                        break;
                    case NPC_LADY:
                        LadyGUID = creature->GetGUID();
                        break;
                    case NPC_BARON:
                        BaronGUID = creature->GetGUID();
                        break;
                    case NPC_SIR:
                        SirGUID = creature->GetGUID();
                        break;
                    case NPC_GLUTH:
                        GluthGUID = creature->GetGUID();
                        break;
                    case NPC_HEIGAN:
                        HeiganGUID = creature->GetGUID();
                        break;
                    case NPC_THADDIUS:
                        ThaddiusGUID = creature->GetGUID();
                        break;
                    case NPC_FEUGEN:
                        FeugenGUID = creature->GetGUID();
                        break;
                    case NPC_STALAGG:
                        StalaggGUID = creature->GetGUID();
                        break;
                    case NPC_SAPPHIRON:
                        SapphironGUID = creature->GetGUID();
                        break;
                    case NPC_KEL_THUZAD:
                        KelthuzadGUID = creature->GetGUID();
                        break;
                    case NPC_LICH_KING:
                        LichKingGUID = creature->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建时的回调
             * @param go 新创建的游戏对象
             *
             * 当副本内的游戏对象创建时被调用，用于记录门、传送门等对象的GUID
             * 并根据副本进度设置对象的初始状态
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_GOTHIK_GATE:
                        GothikGateGUID = go->GetGUID();
                        break;
                    case GO_HORSEMEN_CHEST:
                    case GO_HORSEMEN_CHEST_HERO:
                        HorsemenChestGUID = go->GetGUID();
                        break;
                    case GO_KELTHUZAD_PORTAL01:
                        PortalsGUID[0] = go->GetGUID();
                        break;
                    case GO_KELTHUZAD_PORTAL02:
                        PortalsGUID[1] = go->GetGUID();
                        break;
                    case GO_KELTHUZAD_PORTAL03:
                        PortalsGUID[2] = go->GetGUID();
                        break;
                    case GO_KELTHUZAD_PORTAL04:
                        PortalsGUID[3] = go->GetGUID();
                        break;
                    case GO_KELTHUZAD_TRIGGER:
                        KelthuzadTriggerGUID = go->GetGUID();
                        break;
                    case GO_ROOM_KELTHUZAD:
                        KelthuzadDoorGUID = go->GetGUID();
                        break;
                    case GO_NAXX_PORTAL_ARACHNID:
                        if (GetBossState(BOSS_MAEXXNA) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_NAXX_PORTAL_CONSTRUCT:
                        if (GetBossState(BOSS_THADDIUS) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_NAXX_PORTAL_PLAGUE:
                        if (GetBossState(BOSS_LOATHEB) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_NAXX_PORTAL_MILITARY:
                        if (GetBossState(BOSS_HORSEMEN) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_KELTHUZAD_THRONE:
                        if (GetBossState(BOSS_KELTHUZAD) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_BIRTH:
                        if (hadSapphironBirth || GetBossState(BOSS_SAPPHIRON) == DONE)
                        {
                            hadSapphironBirth = true;
                            go->Delete();
                        }
                        break;
                    default:
                        break;
                }

                InstanceScript::OnGameObjectCreate(go);
            }

            /**
             * @brief 单位死亡时的回调
             * @param unit 死亡的单位
             *
             * 处理玩家死亡事件用于成就判定，以及特殊NPC（比格尔斯沃斯猫）死亡时的对话
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (!playerDied && unit->IsPlayer() && IsEncounterInProgress())
                {
                    playerDied = true;
                    SaveToDB();
                }

                if (Creature* creature = unit->ToCreature())
                    if (creature->GetEntry() == NPC_BIGGLESWORTH)
                    {
                        // Loads Kel'Thuzad's grid. We need this as he must be active in order for his texts to work.
                        instance->LoadGrid(3749.67f, -5114.06f);
                        if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                            kelthuzad->AI()->Talk(SAY_KELTHUZAD_CAT_DIED);
                    }
            }

            /**
             * @brief 设置实例数据
             * @param id 数据ID
             * @param value 数据值
             *
             * 用于设置副本内的自定义数据，如高希克门的状态、萨菲隆出生动画状态
             */
            void SetData(uint32 id, uint32 value) override
            {
                switch (id)
                {
                    case DATA_GOTHIK_GATE:
                        if (GameObject* gate = instance->GetGameObject(GothikGateGUID))
                            gate->SetGoState(GOState(value));
                        break;
                    case DATA_HAD_SAPPHIRON_BIRTH:
                        hadSapphironBirth = (value == 1u);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取实例数据
             * @param id 数据ID
             * @return 对应的数据值
             *
             * 用于获取副本内的自定义数据
             */
            uint32 GetData(uint32 id) const override
            {
                switch (id)
                {
                    case DATA_HAD_SAPPHIRON_BIRTH:
                        return hadSapphironBirth ? 1u : 0u;
                    default:
                        break;
                }

                return 0;
            }

            /**
             * @brief 获取GUID数据
             * @param id 数据ID
             * @return 对应的GUID
             *
             * 根据数据ID返回对应生物的GUID，用于各首领脚本之间的通信
             */
            ObjectGuid GetGuidData(uint32 id) const override
            {
                switch (id)
                {
                    case DATA_ANUBREKHAN:
                        return AnubRekhanGUID;
                    case DATA_FAERLINA:
                        return FaerlinaGUID;
                    case DATA_RAZUVIOUS:
                        return RazuviousGUID;
                    case DATA_GOTHIK:
                        return GothikGUID;
                    case DATA_THANE:
                        return ThaneGUID;
                    case DATA_LADY:
                        return LadyGUID;
                    case DATA_BARON:
                        return BaronGUID;
                    case DATA_SIR:
                        return SirGUID;
                    case DATA_HEIGAN:
                        return HeiganGUID;
                    case DATA_GLUTH:
                        return GluthGUID;
                    case DATA_FEUGEN:
                        return FeugenGUID;
                    case DATA_STALAGG:
                        return StalaggGUID;
                    case DATA_THADDIUS:
                        return ThaddiusGUID;
                    case DATA_SAPPHIRON:
                        return SapphironGUID;
                    case DATA_KELTHUZAD:
                        return KelthuzadGUID;
                    case DATA_KELTHUZAD_PORTAL01:
                        return PortalsGUID[0];
                    case DATA_KELTHUZAD_PORTAL02:
                        return PortalsGUID[1];
                    case DATA_KELTHUZAD_PORTAL03:
                        return PortalsGUID[2];
                    case DATA_KELTHUZAD_PORTAL04:
                        return PortalsGUID[3];
                    case DATA_KELTHUZAD_TRIGGER:
                        return KelthuzadTriggerGUID;
                    case DATA_LICH_KING:
                        return LichKingGUID;
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 设置首领状态
             * @param id 首领ID
             * @param state 首领状态
             * @return 是否成功设置
             *
             * 当首领状态改变时触发相关事件：
             * - 激活对应区域的传送门
             * - 触发克尔苏加德的风趣嘲讽
             * - 开启通向下一区域的道路
             * - 四骑士战利品箱子刷新
             * - 萨菲隆死亡后的对话链
             */
            bool SetBossState(uint32 id, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(id, state))
                    return false;

                switch (id)
                {
                    case BOSS_MAEXXNA:
                        if (state == DONE)
                        {
                            if (GameObject* teleporter = GetGameObject(DATA_NAXX_PORTAL_ARACHNID))
                                teleporter->RemoveFlag(GO_FLAG_NOT_SELECTABLE);

                            events.ScheduleEvent(EVENT_KELTHUZAD_WING_TAUNT, 6s);
                        }
                        break;
                    case BOSS_LOATHEB:
                        if (state == DONE)
                        {
                            if (GameObject* teleporter = GetGameObject(DATA_NAXX_PORTAL_PLAGUE))
                                teleporter->RemoveFlag(GO_FLAG_NOT_SELECTABLE);

                            events.ScheduleEvent(EVENT_KELTHUZAD_WING_TAUNT, 6s);
                        }
                        break;
                    case BOSS_THADDIUS:
                        if (state == DONE)
                        {
                            if (GameObject* teleporter = GetGameObject(DATA_NAXX_PORTAL_CONSTRUCT))
                                teleporter->RemoveFlag(GO_FLAG_NOT_SELECTABLE);

                            events.ScheduleEvent(EVENT_KELTHUZAD_WING_TAUNT, 6s);
                        }
                        break;
                    case BOSS_GOTHIK:
                        if (state == DONE)
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_KORTHAZZ, 10s);
                        break;
                    case BOSS_HORSEMEN:
                        if (state == DONE)
                        {
                            if (GameObject* horsemenChest = instance->GetGameObject(HorsemenChestGUID))
                            {
                                horsemenChest->SetRespawnTime(horsemenChest->GetRespawnDelay());
                                horsemenChest->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                            }

                            if (GameObject* teleporter = GetGameObject(DATA_NAXX_PORTAL_MILITARY))
                                teleporter->RemoveFlag(GO_FLAG_NOT_SELECTABLE);

                            events.ScheduleEvent(EVENT_KELTHUZAD_WING_TAUNT, 6s);
                        }
                        break;
                    case BOSS_SAPPHIRON:
                        if (state == DONE)
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD, 6s);
                        HandleGameObject(KelthuzadDoorGUID, false);
                        break;
                    case BOSS_KELTHUZAD:
                        if (state == DONE)
                            if (GameObject* throne = GetGameObject(DATA_KELTHUZAD_THRONE))
                                throne->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 更新实例状态
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 定期更新副本实例，处理事件调度器中的事件
             * 包括对话链的执行和克尔苏加德的风趣嘲讽
             */
            void Update(uint32 diff) override
            {
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DIALOGUE_GOTHIK_KORTHAZZ:
                            if (Creature* korthazz = instance->GetCreature(ThaneGUID))
                                korthazz->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_ZELIEK, 5s);
                            break;
                        case EVENT_DIALOGUE_GOTHIK_ZELIEK:
                            if (Creature* zeliek = instance->GetCreature(SirGUID))
                                zeliek->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_BLAUMEUX, 6s);
                            break;
                        case EVENT_DIALOGUE_GOTHIK_BLAUMEUX:
                            if (Creature* blaumeux = instance->GetCreature(LadyGUID))
                                blaumeux->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_RIVENDARE, 6s);
                            break;
                        case EVENT_DIALOGUE_GOTHIK_RIVENDARE:
                            if (Creature* rivendare = instance->GetCreature(BaronGUID))
                                rivendare->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_BLAUMEUX2, Seconds(6));
                            break;
                        case EVENT_DIALOGUE_GOTHIK_BLAUMEUX2:
                            if (Creature* blaumeux = instance->GetCreature(LadyGUID))
                                blaumeux->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN2);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_ZELIEK2, Seconds(6));
                            break;
                        case EVENT_DIALOGUE_GOTHIK_ZELIEK2:
                            if (Creature* zeliek = instance->GetCreature(SirGUID))
                                zeliek->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN2);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_KORTHAZZ2, Seconds(6));
                            break;
                        case EVENT_DIALOGUE_GOTHIK_KORTHAZZ2:
                            if (Creature* korthazz = instance->GetCreature(ThaneGUID))
                                korthazz->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN2);
                            events.ScheduleEvent(EVENT_DIALOGUE_GOTHIK_RIVENDARE2, Seconds(6));
                            break;
                        case EVENT_DIALOGUE_GOTHIK_RIVENDARE2:
                            if (Creature* rivendare = instance->GetCreature(BaronGUID))
                                rivendare->AI()->Talk(SAY_DIALOGUE_GOTHIK_HORSEMAN2);
                            break;
                        case EVENT_KELTHUZAD_WING_TAUNT:
                            // Loads Kel'Thuzad's grid. We need this as he must be active in order for his texts to work.
                            instance->LoadGrid(3749.67f, -5114.06f);
                            if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                                kelthuzad->AI()->Talk(CurrentWingTaunt);
                            ++CurrentWingTaunt;
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD:
                            if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                                kelthuzad->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_KELTHUZAD);
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_LICHKING, 6s);
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_LICHKING:
                            if (Creature* lichKing = instance->GetCreature(LichKingGUID))
                                lichKing->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_LICH_KING);
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD2, Seconds(16));
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD2:
                            if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                                kelthuzad->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_KELTHUZAD2);
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_LICHKING2, Seconds(9));
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_LICHKING2:
                            if (Creature* lichKing = instance->GetCreature(LichKingGUID))
                                lichKing->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_LICH_KING2);
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD3, Seconds(12));
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD3:
                            if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                                kelthuzad->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_KELTHUZAD3);
                            events.ScheduleEvent(EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD4, Seconds(6));
                            break;
                        case EVENT_DIALOGUE_SAPPHIRON_KELTHUZAD4:
                            if (Creature* kelthuzad = instance->GetCreature(KelthuzadGUID))
                                kelthuzad->AI()->Talk(SAY_DIALOGUE_SAPPHIRON_KELTHUZAD4);
                            HandleGameObject(KelthuzadDoorGUID, true);
                            break;
                        default:
                            break;
                    }
                }
            }

            /**
             * @brief 检查所有首领是否被击杀
             * @return 如果所有首领都被击杀（最多1个进行中的遭遇）返回true
             *
             * 此函数在CheckAchievementCriteriaMeet中被调用，用于判断是否满足不朽者/不死者成就条件。
             * 由于CheckAchievementCriteriaMeet在SetBossState(bossId, DONE)之前被调用，
             * 因此需要排除最后一个首领，如果最多只有1个遭遇正在进行，则所有首领都已完成。
             */
            bool AreAllEncountersDone()
            {
                uint32 numBossAlive = 0;
                for (uint32 i = 0; i < EncounterCount; ++i)
                    if (GetBossState(i) != DONE)
                        numBossAlive++;

                if (numBossAlive > 1)
                    return false;
                return true;
            }

            /**
             * @brief 检查成就条件是否满足
             * @param criteria_id 成就条件ID
             * @param source 触发成就的玩家
             * @param target 目标单位（可选）
             * @param miscvalue1 额外参数（可选）
             * @return 是否满足成就条件
             *
             * 检查以下成就：
             * - 他们都倒下了：15秒内击杀四骑士
             * - 不朽者（25人）：在一个CD内不死亡任何人击杀所有首领
             * - 不死者（10人）：在一个CD内不死亡任何人击杀所有首领
             */
            bool CheckAchievementCriteriaMeet(uint32 criteria_id, Player const* /*source*/, Unit const* /*target = nullptr*/, uint32 /*miscvalue1 = 0*/) override
            {
                switch (criteria_id)
                {
                    // And They Would All Go Down Together (kill 4HM within 15sec of each other)
                    case 7600: // 25-man
                    case 7601: // 10-man
                        if (criteria_id + instance->GetSpawnMode() == 7601)
                            return false;
                        if (Creature* baron = instance->GetCreature(BaronGUID)) // it doesn't matter which one we use, really
                            return (baron->AI()->GetData(DATA_HORSEMEN_CHECK_ACHIEVEMENT_CREDIT) == 1u);
                        return false;
                    // Difficulty checks are done on DB.
                    // Criteria for achievement 2186: The Immortal (25-man)
                    case 13233: // The Four Horsemen
                    case 13234: // Maexxna
                    case 13235: // Thaddius
                    case 13236: // Loatheb
                    case 7616:  // Kel'Thuzad
                    // Criteria for achievement 2187: The Undying (10-man)
                    case 13237: // The Four Horsemen
                    case 13238: // Maexxna
                    case 13239: // Loatheb
                    case 13240: // Thaddius
                    case 7617:  // Kel'Thuzad
                        if (AreAllEncountersDone() && !playerDied)
                            return true;
                        return false;
                }

                return false;
            }

            /**
             * @brief 写入额外的保存数据
             * @param data 输出字符串流
             *
             * 将playerDied标志保存到副本数据中
             */
            void WriteSaveDataMore(std::ostringstream& data) override
            {
                data << uint32(playerDied ? 1 : 0);
            }

            /**
             * @brief 读取额外的保存数据
             * @param data 输入字符串流
             *
             * 从副本数据中读取playerDied标志
             */
            void ReadSaveDataMore(std::istringstream& data) override
            {
                uint32 tmpState;
                data >> tmpState;
                playerDied = tmpState != 0;
            }

        protected:
            /* ========== 蜘蛛区 ========== */
            ObjectGuid AnubRekhanGUID;    // 阿努布雷坎GUID
            ObjectGuid FaerlinaGUID;      // 大寡妇费琳娜GUID

            /* ========== 瘟疫区 ========== */
            ObjectGuid HeiganGUID;        // 肮脏的希尔盖GUID

            /* ========== 军事区 ========== */
            ObjectGuid RazuviousGUID;     // 教官拉苏维奥斯GUID
            ObjectGuid GothikGUID;        // 高希克GUID
            ObjectGuid GothikGateGUID;    // 高希克之门GUID
            ObjectGuid ThaneGUID;         // 瑟里耶克爵士GUID（四骑士-库尔塔兹领主）
            ObjectGuid LadyGUID;          // 布劳缪克斯女士GUID（四骑士）
            ObjectGuid BaronGUID;         // 男爵GUID（四骑士-瑞文戴尔男爵）
            ObjectGuid SirGUID;           // 泽尔尼克斯爵士GUID（四骑士）
            ObjectGuid HorsemenChestGUID; // 四骑士宝箱GUID

            /* ========== 构造区 ========== */
            ObjectGuid GluthGUID;         // 格拉斯GUID
            ObjectGuid ThaddiusGUID;      // 塔迪乌斯GUID
            ObjectGuid FeugenGUID;        // 费尔根GUID（塔迪乌斯小怪）
            ObjectGuid StalaggGUID;       // 斯塔拉格GUID（塔迪乌斯小怪）

            /* ========== 冰霜巨龙巢穴 ========== */
            ObjectGuid SapphironGUID;         // 萨菲隆GUID
            ObjectGuid KelthuzadGUID;         // 克尔苏加德GUID
            ObjectGuid KelthuzadTriggerGUID;  // 克尔苏加德触发器GUID
            ObjectGuid PortalsGUID[4];        // 克尔苏加德房间四个传送门GUID
            ObjectGuid KelthuzadDoorGUID;     // 克尔苏加德房间门GUID
            ObjectGuid LichKingGUID;          // 巫妖王GUID（克尔苏加德房间装饰性NPC）

            bool hadSapphironBirth;           // 萨菲隆是否已经完成出生动画
            uint8 CurrentWingTaunt;           // 当前克尔苏加德区域嘲讽对话ID

            /* ========== 不朽者/不死者成就 ========== */
            bool playerDied;                  // 是否有玩家在首领战斗中死亡

            EventMap events;                  // 事件调度器
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_naxxramas_InstanceMapScript(map);
        }
};

void AddSC_instance_naxxramas()
{
    new instance_naxxramas();
}
