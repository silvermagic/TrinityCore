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
 * @file instance_molten_core.cpp
 * @brief 熔火之心副本实例脚本
 *
 * 本模块实现了熔火之心副本的实例管理逻辑，包括：
 * - BOSS状态管理
 * - 管理者埃克索图斯的召唤机制
 * - 拉格纳罗斯的传送和召唤
 * - 战利品箱生成
 *
 * 熔火之心是40人团队副本，包含以下BOSS：
 * 1. 鲁西弗隆 (Lucifron)
 * 2. 玛格曼达 (Magmadar)
 * 3. 基赫纳斯 (Gehennas)
 * 4. 沙尔登男爵 (Baron Geddon)
 * 5. 迦顿男爵 (Garr)
 * 6. 沙斯拉尔 (Shazzrah)
 * 7. 萨弗隆先驱者 (Sulfuron Harbinger)
 * 8. 焚化者古雷曼格 (Golemagg the Incinerator)
 * 9. 管理者埃克索图斯 (Majordomo Executus)
 * 10. 拉格纳罗斯 (Ragnaros)
 *
 * 特殊机制：
 * - 击杀前8个BOSS后，管理者埃克索图斯会在副本入口处刷新
 * - 击杀管理者埃克索图斯后，拉格纳罗斯房间开启
 */

#include "ScriptMgr.h"
#include "molten_core.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 召唤位置常量数组
 *
 * 定义了管理者埃克索图斯及其随从的召唤位置。
 * 位置0: 管理者埃克索图斯
 * 位置1-4: 烈焰行者治疗者
 * 位置5-8: 烈焰行者精英
 * 位置9: 拉格纳罗斯召唤位置（用于其他逻辑）
 */
Position const SummonPositions[10] =
{
    {737.850f, -1145.35f, -120.288f, 4.71368f},   ///< 管理者埃克索图斯位置
    {744.162f, -1151.63f, -119.726f, 4.58204f},   ///< 烈焰行者治疗者位置1
    {751.247f, -1152.82f, -119.744f, 4.49673f},   ///< 烈焰行者治疗者位置2
    {759.206f, -1155.09f, -120.051f, 4.30104f},   ///< 烈焰行者治疗者位置3
    {755.973f, -1152.33f, -120.029f, 4.25588f},   ///< 烈焰行者治疗者位置4
    {731.712f, -1147.56f, -120.195f, 4.95955f},   ///< 烈焰行者精英位置1
    {726.499f, -1149.80f, -120.156f, 5.24055f},   ///< 烈焰行者精英位置2
    {722.408f, -1152.41f, -120.029f, 5.33087f},   ///< 烈焰行者精英位置3
    {718.994f, -1156.36f, -119.805f, 5.75738f},   ///< 烈焰行者精英位置4
    {838.510f, -829.840f, -232.000f, 2.00000f},   ///< 拉格纳罗斯召唤位置
};

/**
 * @brief 拉格纳罗斯传送位置
 *
 * 玩家传送到拉格纳罗斯房间的位置。
 */
Position const RagnarosTelePos   = {829.159f, -815.773f, -228.972f, 5.30500f};

/**
 * @brief 拉格纳罗斯召唤位置
 *
 * 拉格纳罗斯BOSS的召唤位置。
 */
Position const RagnarosSummonPos = {838.510f, -829.840f, -232.000f, 2.00000f};

/**
 * @brief 熔火之心副本实例脚本类
 *
 * 继承自InstanceMapScript，管理熔火之心副本的整体状态和逻辑。
 * 包括BOSS状态追踪、管理者埃克索图斯的召唤条件检测、
 * 拉格纳罗斯阶段管理等功能。
 */
class instance_molten_core : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化副本脚本，设置脚本名称和地图ID。
         * 地图ID 409 对应熔火之心副本。
         */
        instance_molten_core() : InstanceMapScript(MCScriptName, 409) { }

        /**
         * @brief 熔火之心实例脚本结构体
         *
         * 继承自InstanceScript，实现具体的副本实例管理逻辑。
         * 负责追踪BOSS状态、管理召唤条件、处理副本数据持久化等。
         */
        struct instance_molten_core_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化实例脚本，设置副本数据头和BOSS数量。
             * 初始化成员变量为默认值。
             */
            instance_molten_core_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                // 设置数据存储头标识
                SetHeaders(DataHeader);
                // 设置BOSS总数
                SetBossNumber(MAX_ENCOUNTER);
                // 初始化管理者埃克索图斯调度标志为false
                _executusSchedule = false;
                // 初始化拉格纳罗斯小怪死亡计数为0
                _ragnarosAddDeaths = 0;
            }

            /**
             * @brief 玩家进入副本时调用
             * @param player 进入副本的玩家指针
             *
             * 当玩家进入副本时检查是否需要召唤管理者埃克索图斯。
             * 调用时机: 每当玩家进入副本时
             */
            void OnPlayerEnter(Player* /*player*/) override
            {
                // 如果已调度召唤管理者埃克索图斯，执行召唤
                if (_executusSchedule)
                    SummonMajordomoExecutus();
            }

            /**
             * @brief 生物创建时调用
             * @param creature 创建的生物指针
             *
             * 当副本中的生物被创建时，根据生物类型保存其GUID。
             * 这些GUID用于后续的查询和操作。
             *
             * 调用时机: 当副本中的生物被创建时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_GOLEMAGG_THE_INCINERATOR:
                        // 保存焚化者古雷曼格的GUID
                        _golemaggTheIncineratorGUID = creature->GetGUID();
                        break;
                    case NPC_MAJORDOMO_EXECUTUS:
                        // 保存管理者埃克索图斯的GUID
                        _majordomoExecutusGUID = creature->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建时调用
             * @param go 创建的游戏对象指针
             *
             * 当副本中的游戏对象被创建时，根据对象类型保存其GUID。
             * 主要用于保存战利品箱的GUID。
             *
             * 调用时机: 当副本中的游戏对象被创建时
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_CACHE_OF_THE_FIRELORD:
                        // 保存炎魔之王的宝藏GUID
                        _cacheOfTheFirelordGUID = go->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 设置副本数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 用于设置副本的自定义数据。
             * 主要用于追踪拉格纳罗斯阶段的小怪死亡数量。
             *
             * 调用时机: 当需要更新副本数据时
             */
            void SetData(uint32 type, uint32 data) override
            {
                if (type == DATA_RAGNAROS_ADDS)
                {
                    if (data == 1)
                        // 增加小怪死亡计数
                        ++_ragnarosAddDeaths;
                    else if (data == 0)
                        // 重置小怪死亡计数
                        _ragnarosAddDeaths = 0;
                }
            }

            /**
             * @brief 获取副本数据
             * @param type 数据类型
             * @return 数据值
             *
             * 获取副本的自定义数据。
             * 主要用于查询拉格纳罗斯阶段的小怪死亡数量。
             *
             * 调用时机: 当需要查询副本数据时
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_RAGNAROS_ADDS:
                        // 返回拉格纳罗斯小怪死亡数量
                        return _ragnarosAddDeaths;
                }

                return 0;
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型
             * @return 对应生物的GUID
             *
             * 根据类型返回保存的生物GUID。
             * 主要用于其他脚本查询特定BOSS的GUID。
             *
             * 调用时机: 当其他脚本需要查询GUID时
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case BOSS_GOLEMAGG_THE_INCINERATOR:
                        // 返回焚化者古雷曼格的GUID
                        return _golemaggTheIncineratorGUID;
                    case BOSS_MAJORDOMO_EXECUTUS:
                        // 返回管理者埃克索图斯的GUID
                        return _majordomoExecutusGUID;
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 设置BOSS状态
             * @param bossId BOSS ID
             * @param state 遭遇战状态
             * @return 是否成功设置
             *
             * 当BOSS状态改变时调用，用于处理相关逻辑：
             * - 检查是否满足召唤管理者埃克索图斯的条件
             * - 击杀管理者埃克索图斯后生成战利品箱
             *
             * 调用时机: 当BOSS状态改变时（战斗开始、击杀等）
             */
            bool SetBossState(uint32 bossId, EncounterState state) override
            {
                // 调用父类方法进行基础状态设置
                if (!InstanceScript::SetBossState(bossId, state))
                    return false;

                // 如果BOSS被击杀且是管理者埃克索图斯之前的BOSS
                // 检查是否满足召唤管理者埃克索图斯的条件
                if (state == DONE && bossId < BOSS_MAJORDOMO_EXECUTUS)
                    if (CheckMajordomoExecutus())
                        SummonMajordomoExecutus();

                // 如果管理者埃克索图斯被击杀，生成战利品箱
                if (bossId == BOSS_MAJORDOMO_EXECUTUS && state == DONE)
                    DoRespawnGameObject(_cacheOfTheFirelordGUID, 7_days);

                return true;
            }

            /**
             * @brief 召唤管理者埃克索图斯
             *
             * 根据副本进度召唤管理者埃克索图斯：
             * - 如果管理者埃克索图斯尚未被击杀，在副本入口召唤
             * - 如果已被击杀，在拉格纳罗斯房间召唤并触发拉格纳罗斯战斗
             *
             * 调用时机:
             * - 玩家进入副本时（如果已调度）
             * - 击杀前置BOSS后满足条件时
             */
            void SummonMajordomoExecutus()
            {
                // 清除调度标志
                _executusSchedule = false;

                // 如果管理者埃克索图斯已存在，不重复召唤
                if (_majordomoExecutusGUID)
                    return;

                // 如果管理者埃克索图斯尚未被击杀，在副本入口召唤
                if (GetBossState(BOSS_MAJORDOMO_EXECUTUS) != DONE)
                {
                    // 召唤管理者埃克索图斯
                    instance->SummonCreature(NPC_MAJORDOMO_EXECUTUS, SummonPositions[0]);
                    // 召唤4个烈焰行者治疗者
                    instance->SummonCreature(NPC_FLAMEWAKER_HEALER, SummonPositions[1]);
                    instance->SummonCreature(NPC_FLAMEWAKER_HEALER, SummonPositions[2]);
                    instance->SummonCreature(NPC_FLAMEWAKER_HEALER, SummonPositions[3]);
                    instance->SummonCreature(NPC_FLAMEWAKER_HEALER, SummonPositions[4]);
                    // 召唤4个烈焰行者精英
                    instance->SummonCreature(NPC_FLAMEWAKER_ELITE, SummonPositions[5]);
                    instance->SummonCreature(NPC_FLAMEWAKER_ELITE, SummonPositions[6]);
                    instance->SummonCreature(NPC_FLAMEWAKER_ELITE, SummonPositions[7]);
                    instance->SummonCreature(NPC_FLAMEWAKER_ELITE, SummonPositions[8]);
                }
                // 如果管理者埃克索图斯已被击杀，在拉格纳罗斯房间召唤
                else if (TempSummon* summon = instance->SummonCreature(NPC_MAJORDOMO_EXECUTUS, RagnarosTelePos))
                {
                    // 触发拉格纳罗斯战斗的替代动作
                    summon->AI()->DoAction(ACTION_START_RAGNAROS_ALT);
                }
            }

            /**
             * @brief 检查是否满足召唤管理者埃克索图斯的条件
             * @return 是否满足条件
             *
             * 检查所有前置BOSS是否已被击杀。
             * 前置BOSS包括从鲁西弗隆到焚化者古雷曼格的8个BOSS。
             *
             * 调用时机: 当BOSS状态改变时
             * 性能注意事项: 循环次数固定为BOSS数量，性能开销可忽略
             */
            bool CheckMajordomoExecutus() const
            {
                // 如果拉格纳罗斯已被击杀，不再召唤管理者埃克索图斯
                if (GetBossState(BOSS_RAGNAROS) == DONE)
                    return false;

                // 检查管理者埃克索图斯之前的所有BOSS是否都已被击杀
                for (uint8 i = 0; i < BOSS_MAJORDOMO_EXECUTUS; ++i)
                    if (GetBossState(i) != DONE)
                        return false;

                return true;
            }

            /**
             * @brief 读取保存数据
             * @param data 数据流
             *
             * 从存档数据中恢复副本状态。
             * 如果满足召唤条件，设置调度标志以便在玩家进入时召唤。
             *
             * 调用时机: 当副本从存档加载时
             */
            void ReadSaveDataMore(std::istringstream& /*data*/) override
            {
                // 如果满足召唤管理者埃克索图斯的条件，设置调度标志
                if (CheckMajordomoExecutus())
                    _executusSchedule = true;
            }

        private:
            ObjectGuid _golemaggTheIncineratorGUID;   ///< 焚化者古雷曼格的GUID
            ObjectGuid _majordomoExecutusGUID;        ///< 管理者埃克索图斯的GUID
            ObjectGuid _cacheOfTheFirelordGUID;       ///< 炎魔之王的宝藏GUID
            bool  _executusSchedule;                  ///< 是否需要调度召唤管理者埃克索图斯
            uint8 _ragnarosAddDeaths;                 ///< 拉格纳罗斯小怪死亡计数
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 实例脚本指针
         *
         * 创建并返回熔火之心副本的实例脚本对象。
         * 调用时机: 当副本地图需要创建实例脚本时
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_molten_core_InstanceMapScript(map);
        }
};

/**
 * @brief 注册熔火之心副本实例脚本
 *
 * 此函数用于将熔火之心副本实例脚本注册到脚本系统中。
 * 在服务器启动时由脚本加载器调用。
 */
void AddSC_instance_molten_core()
{
    new instance_molten_core();
}
