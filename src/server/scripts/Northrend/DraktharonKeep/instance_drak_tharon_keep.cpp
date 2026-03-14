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
 * @file instance_drak_tharon_keep.cpp
 * @brief 达克萨隆要塞副本实例脚本
 *
 * 本文件实现了达克萨隆要塞副本的实例管理逻辑，包括：
 * - 首领状态管理和持久化
 * - 游戏对象和生物的GUID存储
 * - 召唤者位置的初始化和识别
 * - 水晶守卫死亡事件的处理
 *
 * 达克萨隆要塞包含4个首领：
 * 1. 托尔戈斯（Trollgore）
 * 2. 诺沃斯（Novos）
 * 3. 金德雷国王（King Dred）
 * 4. 塔隆-贾（Tharon'ja）
 */

#include "ScriptMgr.h"
#include "drak_tharon_keep.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"

/**
 * @brief 达克萨隆要塞实例脚本类
 *
 * 继承自InstanceMapScript，提供副本的实例级管理功能
 */
class instance_drak_tharon_keep : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         * @note 注册脚本名称和地图ID（600为达克萨隆要塞）
         */
        instance_drak_tharon_keep() : InstanceMapScript(DrakTharonKeepScriptName, 600) { }

        /**
         * @brief 达克萨隆要塞实例脚本实现类
         *
         * 继承自InstanceScript，实现副本的核心管理逻辑
         */
        struct instance_drak_tharon_keep_InstanceScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图对象指针
             *
             * @note 初始化副本的基本配置：
             * - 设置数据头
             * - 设置首领数量
             */
            instance_drak_tharon_keep_InstanceScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
            }

            /**
             * @brief 生物创建回调
             * @param creature 新创建的生物对象
             *
             * @调用时机 当副本中有生物被创建时（包括加载和动态生成）
             * @note 根据生物的入口ID存储对应的GUID，供后续查询使用
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_TROLLGORE:
                        TrollgoreGUID = creature->GetGUID();  ///< 存储托尔戈斯的GUID
                        break;
                    case NPC_NOVOS:
                        NovosGUID = creature->GetGUID();      ///< 存储诺沃斯的GUID
                        break;
                    case NPC_KING_DRED:
                        KingDredGUID = creature->GetGUID();   ///< 存储金德雷国王的GUID
                        break;
                    case NPC_THARON_JA:
                        TharonJaGUID = creature->GetGUID();   ///< 存储塔隆-贾的GUID
                        break;
                    case NPC_WORLD_TRIGGER:
                        InitializeTrollgoreInvaderSummoner(creature);  ///< 初始化托尔戈斯入侵者召唤点
                        break;
                    case NPC_CRYSTAL_CHANNEL_TARGET:
                        InitializeNovosSummoner(creature);    ///< 初始化诺沃斯召唤点
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建回调
             * @param go 新创建的游戏对象
             *
             * @调用时机 当副本中有游戏对象被创建时
             * @note 存储诺沃斯战斗所需的4个水晶的GUID
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_NOVOS_CRYSTAL_1:
                        NovosCrystalGUIDs[0] = go->GetGUID();  ///< 存储水晶1的GUID
                        break;
                    case GO_NOVOS_CRYSTAL_2:
                        NovosCrystalGUIDs[1] = go->GetGUID();  ///< 存储水晶2的GUID
                        break;
                    case GO_NOVOS_CRYSTAL_3:
                        NovosCrystalGUIDs[2] = go->GetGUID();  ///< 存储水晶3的GUID
                        break;
                    case GO_NOVOS_CRYSTAL_4:
                        NovosCrystalGUIDs[3] = go->GetGUID();  ///< 存储水晶4的GUID
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 初始化托尔戈斯入侵者召唤点
             * @param creature 世界触发器生物
             *
             * 根据触发器的Y和Z坐标确定其对应的召唤点位置
             *
             * @调用时机 当NPC_WORLD_TRIGGER生物被创建时
             * @note 通过坐标范围识别不同召唤点，用于托尔戈斯战斗中的入侵者召唤
             */
            void InitializeTrollgoreInvaderSummoner(Creature* creature)
            {
                float y = creature->GetPositionY();
                float z = creature->GetPositionZ();

                // 只处理上层平台的触发器（Z坐标大于50）
                if (z < 50.0f)
                    return;

                // 根据Y坐标范围分配到不同的召唤点
                if (y < -650.0f && y > -660.0f)
                    TrollgoreInvaderSummonerGuids[0] = creature->GetGUID();  ///< 召唤点1
                else if (y < -660.0f && y > -670.0f)
                    TrollgoreInvaderSummonerGuids[1] = creature->GetGUID();  ///< 召唤点2
                else if (y < -675.0f && y > -685.0f)
                    TrollgoreInvaderSummonerGuids[2] = creature->GetGUID();  ///< 召唤点3
            }

            /**
             * @brief 初始化诺沃斯召唤点
             * @param creature 水晶通道目标生物
             *
             * 根据触发器的精确坐标确定其对应的召唤点位置
             *
             * @调用时机 当NPC_CRYSTAL_CHANNEL_TARGET生物被创建时
             * @note 通过精确的坐标范围识别不同的召唤点，用于诺沃斯战斗中的召唤机制
             */
            void InitializeNovosSummoner(Creature* creature)
            {
                float x = creature->GetPositionX();
                float y = creature->GetPositionY();
                float z = creature->GetPositionZ();

                // 召唤点1：左上角平台
                if (x < -374.0f && x > -379.0f && y > -820.0f && y < -815.0f && z < 60.0f && z > 58.0f)
                    NovosSummonerGUIDs[0] = creature->GetGUID();
                // 召唤点2：右上角平台
                else if (x < -379.0f && x > -385.0f && y > -820.0f && y < -815.0f && z < 60.0f && z > 58.0f)
                    NovosSummonerGUIDs[1] = creature->GetGUID();
                // 召唤点3：下方平台
                else if (x < -374.0f && x > -385.0f && y > -827.0f && y < -820.0f && z < 60.0f && z > 58.0f)
                    NovosSummonerGUIDs[2] = creature->GetGUID();
                // 召唤点4：水晶守卫召唤点（不同位置）
                else if (x < -338.0f && x > -344.0f && y > -727.0f && y < 721.0f && z < 30.0f && z > 26.0f)
                    NovosSummonerGUIDs[3] = creature->GetGUID();
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型标识
             * @return 对应的ObjectGuid，未找到则返回空GUID
             *
             * @调用时机 其他脚本需要查询特定对象GUID时
             * @note 支持查询首领、召唤点、水晶等对象的GUID
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_TROLLGORE:
                        return TrollgoreGUID;
                    case DATA_NOVOS:
                        return NovosGUID;
                    case DATA_KING_DRED:
                        return KingDredGUID;
                    case DATA_THARON_JA:
                        return TharonJaGUID;
                    case DATA_TROLLGORE_INVADER_SUMMONER_1:
                    case DATA_TROLLGORE_INVADER_SUMMONER_2:
                    case DATA_TROLLGORE_INVADER_SUMMONER_3:
                        return TrollgoreInvaderSummonerGuids[type - DATA_TROLLGORE_INVADER_SUMMONER_1];
                    case DATA_NOVOS_CRYSTAL_1:
                    case DATA_NOVOS_CRYSTAL_2:
                    case DATA_NOVOS_CRYSTAL_3:
                    case DATA_NOVOS_CRYSTAL_4:
                        return NovosCrystalGUIDs[type - DATA_NOVOS_CRYSTAL_1];
                    case DATA_NOVOS_SUMMONER_1:
                    case DATA_NOVOS_SUMMONER_2:
                    case DATA_NOVOS_SUMMONER_3:
                    case DATA_NOVOS_SUMMONER_4:
                        return NovosSummonerGUIDs[type - DATA_NOVOS_SUMMONER_1];
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 单位死亡回调
             * @param unit 死亡的单位
             *
             * @调用时机 当副本中有任何单位死亡时
             * @note 特殊处理水晶守卫的死亡，通知诺沃斯AI
             */
            void OnUnitDeath(Unit* unit) override
            {
                if (unit->GetEntry() == NPC_CRYSTAL_HANDLER)
                    if (Creature* novos = instance->GetCreature(NovosGUID))
                        novos->AI()->DoAction(ACTION_CRYSTAL_HANDLER_DIED);
            }

        protected:
            // 首领GUID存储
            ObjectGuid TrollgoreGUID;    ///< 托尔戈斯的GUID
            ObjectGuid NovosGUID;        ///< 诺沃斯的GUID
            ObjectGuid KingDredGUID;     ///< 金德雷国王的GUID
            ObjectGuid TharonJaGUID;     ///< 塔隆-贾的GUID

            // 托尔戈斯战斗相关
            ObjectGuid TrollgoreInvaderSummonerGuids[3];  ///< 托尔戈斯入侵者召唤点GUID数组（3个位置）

            // 诺沃斯战斗相关
            ObjectGuid NovosCrystalGUIDs[4];    ///< 诺沃斯水晶GUID数组（4个水晶）
            ObjectGuid NovosSummonerGUIDs[4];   ///< 诺沃斯召唤点GUID数组（4个位置）
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图对象
         * @return 新创建的实例脚本对象
         *
         * @调用时机 副本被创建时由核心调用
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_drak_tharon_keep_InstanceScript(map);
        }
};

/**
 * @brief 注册实例脚本
 *
 * 将达克萨隆要塞实例脚本注册到脚本系统中
 */
void AddSC_instance_drak_tharon_keep()
{
    new instance_drak_tharon_keep();
}
