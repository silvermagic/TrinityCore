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

/* ScriptData
SDName: Instance_Blackfathom_Deeps
SD%Complete: 50
SDComment:
SDCategory: Blackfathom Deeps
EndScriptData */

/**
 * @file instance_blackfathom_deeps.cpp
 * @brief 黑暗深渊副本实例脚本
 *
 * 管理黑暗深渊副本的整体状态和机制，包括：
 * - Boss战斗状态跟踪（Kelris、Gelihast、Aku'mai）
 * - 火焰点燃事件管理（共4个火焰）
 * - 小怪召唤机制（每点燃一个火焰触发不同波次）
 * - 游戏对象状态管理（门、祭坛、神殿等）
 * - 稀有怪Lorgus Jett的随机刷新位置
 *
 * 副本流程：
 * 1. 击败Twilight Lord Kelris
 * 2. 点燃4个火焰（每点燃一个触发小怪波次）
 * 3. 击杀所有召唤的小怪（共18只）开启大门
 * 4. 击败Gelihast
 * 5. 击败Aku'mai（最终Boss）
 *
 * 特殊机制：
 * - 火焰点燃顺序不影响小怪波次，每个火焰对应固定的小怪配置
 * - 击败Boss后会解锁对应的游戏对象供玩家交互
 */

#include "ScriptMgr.h"
#include "blackfathom_deeps.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Random.h"

/**
 * @brief Lorgus Jett（稀有怪）的可能刷新位置
 *
 * Lorgus Jett是副本中的稀有精英怪，每次副本重置时会在以下4个位置中随机选择一个刷新。
 * 玩家需要探索副本才能找到他。
 */
Position const LorgusPosition[4] =
{
    { -458.500610f, -38.343079f, -33.474445f, 0.0f },  ///< 位置1：副本入口附近
    { -469.423615f, -88.400513f, -39.265102f, 0.0f },  ///< 位置2：第一个大厅
    { -622.354980f, -10.350100f, -22.777000f, 0.0f },  ///< 位置3：走廊区域
    { -759.640564f,  16.658913f, -29.159529f, 0.0f }   ///< 位置4：Boss房间附近
};

/**
 * @brief 火焰事件小怪召唤位置
 *
 * 定义了点燃火焰后小怪的召唤位置。
 * 前10个位置用于火焰事件小怪，分为左右两侧：
 * - 位置0-4：左侧区域
 * - 位置5-9：右侧区域
 * - 位置10：Morridune的生成位置（击败Aku'mai后）
 */
Position const SpawnsLocation[] =
{
    { -768.949f, -174.413f, -25.87f, 3.09f },  ///< 左侧位置1
    { -768.888f, -164.238f, -25.87f, 3.09f },  ///< 左侧位置2
    { -768.951f, -153.911f, -25.88f, 3.09f },  ///< 左侧位置3
    { -774.400f, -169.405f, -25.86f, 3.11f },  ///< 左侧位置4
    { -774.491f, -159.371f, -25.86f, 3.21f },  ///< 左侧位置5
    { -867.782f, -174.352f, -25.87f, 6.27f },  ///< 右侧位置1
    { -867.875f, -164.089f, -25.87f, 6.27f },  ///< 右侧位置2
    { -867.859f, -153.927f, -25.88f, 6.27f },  ///< 右侧位置3
    { -861.823f, -159.018f, -25.87f, 0.00f },  ///< 右侧位置4
    { -861.524f, -169.387f, -25.87f, 0.18f },  ///< 右侧位置5
    { -859.827f, -468.425f, -33.88f, 5.63f }   ///< Morridune生成位置
};

/**
 * @class instance_blackfathom_deeps
 * @brief 黑暗深渊副本实例脚本主类
 *
 * 继承自InstanceMapScript，管理整个副本的生命周期和状态。
 * 负责创建和管理副本实例脚本对象。
 */
class instance_blackfathom_deeps : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册黑暗深渊副本脚本，地图ID为48。
     */
    instance_blackfathom_deeps() : InstanceMapScript(BFDScriptName, 48) { }

    /**
     * @brief 获取副本实例脚本
     * @param map 副本地图指针
     * @return 新创建的副本实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_blackfathom_deeps_InstanceMapScript(map);
    }

    /**
     * @struct instance_blackfathom_deeps_InstanceMapScript
     * @brief 黑暗深渊副本实例脚本实现
     *
     * 继承自InstanceScript，实现副本的核心逻辑：
     * - 管理Boss战斗状态
     * - 处理火焰点燃事件
     * - 控制游戏对象状态
     * - 追踪副本进度
     */
    struct instance_blackfathom_deeps_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化副本脚本：
         * - 设置数据头标识符
         * - 设置Boss数量
         * - 初始化计数器
         */
        instance_blackfathom_deeps_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);

            countFires = 0;
            deathTimes = 0;
        }

        /**
         * @brief 生物创建时调用
         * @param creature 创建的生物指针
         *
         * 处理副本中生物的初始化：
         * - Twilight Lord Kelris：保存GUID供后续查询
         * - Lorgus Jett：随机设置刷新位置（4个位置中随机选择）
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_TWILIGHT_LORD_KELRIS:
                    twilightLordKelrisGUID = creature->GetGUID();
                    break;
                case NPC_LORGUS_JETT:
                    creature->SetHomePosition(LorgusPosition[urand(0, 3)]);
                    break;
            }
        }

        /**
         * @brief 游戏对象创建时调用
         * @param go 创建的游戏对象指针
         *
         * 处理副本中游戏对象的初始化和状态设置：
         * - 火焰对象：保存GUID供小怪召唤使用
         * - Gelihast神殿：Boss未击杀时设为不可选择
         * - 深渊祭坛：Aku'mai未击杀时设为不可选择
         * - Aku'mai大门：根据Boss状态决定是否开启
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_FIRE_OF_AKU_MAI_1:
                    shrine1GUID = go->GetGUID();
                    break;
                case GO_FIRE_OF_AKU_MAI_2:
                    shrine2GUID = go->GetGUID();
                    break;
                case GO_FIRE_OF_AKU_MAI_3:
                    shrine3GUID = go->GetGUID();
                    break;
                case GO_FIRE_OF_AKU_MAI_4:
                    shrine4GUID = go->GetGUID();
                    break;
                case GO_SHRINE_OF_GELIHAST:
                    shrineOfGelihastGUID = go->GetGUID();
                    // 如果Gelihast未被击杀，神殿不可交互
                    if (GetBossState(DATA_GELIHAST) != DONE)
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                case GO_ALTAR_OF_THE_DEEPS:
                    altarOfTheDeepsGUID = go->GetGUID();
                    // 如果Aku'mai未被击杀，祭坛不可交互
                    if (GetBossState(DATA_AKU_MAI) != DONE)
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                case GO_AKU_MAI_DOOR:
                    // 如果Aku'mai已被击杀，大门保持开启状态
                    if (GetBossState(DATA_AKU_MAI) == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    mainDoorGUID = go->GetGUID();
                    break;
            }
        }

        /**
         * @brief 设置副本数据
         * @param type 数据类型
         * @param data 数据值
         *
         * 处理副本中的自定义数据更新：
         * - DATA_FIRE：火焰点燃计数，每个火焰点燃时触发对应波次的小怪
         * - DATA_EVENT：小怪击杀计数，达到18时开启大门
         *
         * 火焰波次详解：
         * - 第1个火焰：召唤4只Aku'mai Snapjaw（鳄龟）
         * - 第2个火焰：召唤10只Murkshallow Softshell（软壳龟）
         * - 第3个火焰：召唤2只Aku'mai Servant（仆从）
         * - 第4个火焰：召唤4只Barbed Crustacean（刺甲蟹）
         * 总计：4+10+2+4=20只小怪（但实际计数器检测18只）
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_FIRE:
                    countFires = data;
                    switch (countFires)
                    {
                        case 1:
                            // 第1个火焰：召唤4只鳄龟
                            if (GameObject* go = instance->GetGameObject(shrine1GUID))
                            {
                                go->SummonCreature(NPC_AKU_MAI_SNAPJAW, SpawnsLocation[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_AKU_MAI_SNAPJAW, SpawnsLocation[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_AKU_MAI_SNAPJAW, SpawnsLocation[5], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_AKU_MAI_SNAPJAW, SpawnsLocation[7], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                            }
                            break;
                        case 2:
                            // 第2个火焰：召唤10只软壳龟（分散在左右两侧）
                            if (GameObject* go = instance->GetGameObject(shrine1GUID))
                            {
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[4], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[5], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[6], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[7], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[8], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_MURKSHALLOW_SOFTSHELL, SpawnsLocation[9], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                            }
                            break;
                        case 3:
                            // 第3个火焰：召唤2只仆从（法系怪）
                            if (GameObject* go = instance->GetGameObject(shrine1GUID))
                            {
                                go->SummonCreature(NPC_AKU_MAI_SERVANT, SpawnsLocation[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_AKU_MAI_SERVANT, SpawnsLocation[6], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                            }
                            break;
                        case 4:
                            // 第4个火焰：召唤4只刺甲蟹
                            if (GameObject* go = instance->GetGameObject(shrine1GUID))
                            {
                                go->SummonCreature(NPC_BARBED_CRUSTACEAN, SpawnsLocation[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_BARBED_CRUSTACEAN, SpawnsLocation[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_BARBED_CRUSTACEAN, SpawnsLocation[5], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                                go->SummonCreature(NPC_BARBED_CRUSTACEAN, SpawnsLocation[7], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                            }
                            break;
                    }
                    break;
                case DATA_EVENT:
                    deathTimes = data;
                    // 击杀18只小怪后开启通往最终Boss的大门
                    if (deathTimes == 18)
                        HandleGameObject(mainDoorGUID, true);
                    break;
            }
        }

        /**
         * @brief 设置Boss战斗状态
         * @param type Boss类型ID
         * @param state 战斗状态
         * @return true表示状态设置成功
         *
         * 当Boss战斗状态改变时调用，处理相应的副本逻辑：
         * - Gelihast死亡：解锁Gelihast神殿，允许玩家获取增益
         * - Aku'mai死亡：解锁深渊祭坛，召唤Morridune NPC
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_GELIHAST:
                    if (state == DONE)
                        if (GameObject* go = instance->GetGameObject(shrineOfGelihastGUID))
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);  // 允许玩家使用神殿
                    break;
                case DATA_AKU_MAI:
                    if (state == DONE)
                        if (GameObject* go = instance->GetGameObject(altarOfTheDeepsGUID))
                        {
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);  // 允许玩家使用祭坛
                            // 召唤Morridune，他将护送玩家离开副本
                            go->SummonCreature(NPC_MORRIDUNE, SpawnsLocation[10], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min);
                        }
                    break;
                default:
                    break;
            }

            return true;
        }

        /**
         * @brief 获取副本数据
         * @param type 数据类型
         * @return 对应的数据值
         *
         * 返回副本的自定义数据：
         * - DATA_FIRE：已点燃的火焰数量
         * - DATA_EVENT：已击杀的小怪数量
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_FIRE:
                    return countFires;
                case DATA_EVENT:
                    return deathTimes;
            }

            return 0;
        }

        /**
         * @brief 获取游戏对象GUID
         * @param data 对象类型标识
         * @return 对应的游戏对象GUID
         *
         * 返回副本中重要对象的GUID，供其他脚本使用：
         * - Twilight Lord Kelris：Boss GUID
         * - 神殿1-4：火焰对象GUID
         * - Gelihast神殿：祭坛GUID
         * - 主门：大门GUID
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            switch (data)
            {
                case DATA_TWILIGHT_LORD_KELRIS:
                    return twilightLordKelrisGUID;
                case DATA_SHRINE1:
                    return shrine1GUID;
                case DATA_SHRINE2:
                    return shrine2GUID;
                case DATA_SHRINE3:
                    return shrine3GUID;
                case DATA_SHRINE4:
                    return shrine4GUID;
                case DATA_SHRINE_OF_GELIHAST:
                    return shrineOfGelihastGUID;
                case DATA_MAINDOOR:
                    return mainDoorGUID;
            }

            return ObjectGuid::Empty;
        }

    private:
        ObjectGuid twilightLordKelrisGUID;     ///< Twilight Lord Kelris的GUID
        ObjectGuid shrine1GUID;                 ///< 第1个火焰的GUID
        ObjectGuid shrine2GUID;                 ///< 第2个火焰的GUID
        ObjectGuid shrine3GUID;                 ///< 第3个火焰的GUID
        ObjectGuid shrine4GUID;                 ///< 第4个火焰的GUID
        ObjectGuid shrineOfGelihastGUID;       ///< Gelihast神殿的GUID
        ObjectGuid altarOfTheDeepsGUID;        ///< 深渊祭坛的GUID
        ObjectGuid mainDoorGUID;               ///< 通往最终Boss区域的大门GUID
        uint8 countFires;                       ///< 已点燃的火焰计数
        uint8 deathTimes;                       ///< 已击杀的小怪计数
    };
};

/**
 * @brief 注册黑暗深渊副本实例脚本
 *
 * 创建并注册副本实例脚本对象，使其在副本加载时生效。
 */
void AddSC_instance_blackfathom_deeps()
{
    new instance_blackfathom_deeps();
}
