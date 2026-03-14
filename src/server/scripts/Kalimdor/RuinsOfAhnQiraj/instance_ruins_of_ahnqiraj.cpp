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
 * @file instance_ruins_of_ahnqiraj.cpp
 * @brief 安其拉废墟副本实例脚本
 *
 * 本模块实现了安其拉废墟（AQ20）副本的实例管理：
 * - 管理副本中所有BOSS的GUID
 * - 保存和加载副本进度
 * - 提供BOSS状态查询接口
 * - 管理瘫痪玩家的GUID（用于阿亚米斯BOSS战）
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "ruins_of_ahnqiraj.h"

/**
 * @class instance_ruins_of_ahnqiraj
 * @brief 安其拉废墟副本脚本类
 *
 * 负责注册和管理安其拉废墟副本的实例脚本
 */
class instance_ruins_of_ahnqiraj : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册副本脚本，地图ID为509（安其拉废墟）
         */
        instance_ruins_of_ahnqiraj() : InstanceMapScript(AQ20ScriptName, 509) { }

        /**
         * @class instance_ruins_of_ahnqiraj_InstanceMapScript
         * @brief 安其拉废墟副本实例脚本实现类
         *
         * 实现了副本的管理逻辑：
         * - 存储所有BOSS的GUID
         * - 管理BOSS的击杀状态
         * - 提供GUID查询接口
         */
        struct instance_ruins_of_ahnqiraj_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 地图实例指针
             *
             * 初始化副本脚本，设置数据头和BOSS数量
             */
            instance_ruins_of_ahnqiraj_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);         // 设置保存数据的标识头
                SetBossNumber(NUM_ENCOUNTER);   // 设置BOSS数量
            }

            /**
             * @brief 生物创建回调
             * @param creature 创建的生物
             *
             * 当生物在副本中创建时，根据生物类型保存其GUID
             * @调用时机 生物创建或加载时
             * @性能注意事项 每个生物创建时都会调用，需保持高效
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_KURINAXX:
                        _kurinaxxGUID = creature->GetGUID();  ///< 库林纳克斯GUID
                        break;
                    case NPC_RAJAXX:
                        _rajaxxGUID = creature->GetGUID();    ///< 拉贾克斯GUID
                        break;
                    case NPC_MOAM:
                        _moamGUID = creature->GetGUID();      ///< 莫阿姆GUID
                        break;
                    case NPC_BURU:
                        _buruGUID = creature->GetGUID();      ///< 布鲁GUID
                        break;
                    case NPC_AYAMISS:
                        _ayamissGUID = creature->GetGUID();   ///< 阿亚米斯GUID
                        break;
                    case NPC_OSSIRIAN:
                        _ossirianGUID = creature->GetGUID();  ///< 奥库塔尔GUID
                        break;
                }
            }

            /**
             * @brief 设置BOSS状态
             * @param bossId BOSS ID
             * @param state BOSS状态（进行中、完成等）
             * @return 是否设置成功
             *
             * 当BOSS状态改变时调用，用于保存副本进度
             * @调用时机 BOSS进入战斗、击杀、重置时
             */
            bool SetBossState(uint32 bossId, EncounterState state) override
            {
                // 调用父类方法保存状态
                if (!InstanceScript::SetBossState(bossId, state))
                    return false;

                return true;
            }

            /**
             * @brief 设置GUID数据
             * @param type 数据类型
             * @param data GUID数据
             *
             * 用于存储特定数据，当前仅存储瘫痪玩家的GUID
             * @调用时机 阿亚米斯BOSS施放瘫痪技能时
             */
            void SetGuidData(uint32 type, ObjectGuid data) override
            {
                if (type == DATA_PARALYZED)
                    _paralyzedGUID = data;  ///< 瘫痪玩家的GUID
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型
             * @return 对应的GUID
             *
             * 根据类型返回对应的BOSS GUID或瘫痪玩家GUID
             * @调用时机 其他脚本查询BOSS或瘫痪玩家时
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_KURINNAXX:
                        return _kurinaxxGUID;      ///< 库林纳克斯GUID
                    case DATA_RAJAXX:
                        return _rajaxxGUID;        ///< 拉贾克斯GUID
                    case DATA_MOAM:
                        return _moamGUID;          ///< 莫阿姆GUID
                    case DATA_BURU:
                        return _buruGUID;          ///< 布鲁GUID
                    case DATA_AYAMISS:
                        return _ayamissGUID;       ///< 阿亚米斯GUID
                    case DATA_OSSIRIAN:
                        return _ossirianGUID;      ///< 奥库塔尔GUID
                    case DATA_PARALYZED:
                        return _paralyzedGUID;     ///< 瘫痪玩家的GUID
                }

                return ObjectGuid::Empty;
            }

        private:
            ObjectGuid _kurinaxxGUID;     ///< 库林纳克斯BOSS的GUID
            ObjectGuid _rajaxxGUID;       ///< 拉贾克斯BOSS的GUID
            ObjectGuid _moamGUID;         ///< 莫阿姆BOSS的GUID
            ObjectGuid _buruGUID;         ///< 布鲁BOSS的GUID
            ObjectGuid _ayamissGUID;      ///< 阿亚米斯BOSS的GUID
            ObjectGuid _ossirianGUID;     ///< 奥库塔尔BOSS的GUID
            ObjectGuid _paralyzedGUID;    ///< 被瘫痪玩家的GUID（用于阿亚米斯BOSS战）
        };

        /**
         * @brief 获取实例脚本
         * @param map 地图实例指针
         * @return 实例脚本指针
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_ruins_of_ahnqiraj_InstanceMapScript(map);
        }
};

/**
 * @brief 注册脚本
 *
 * 将安其拉废墟副本脚本注册到脚本系统
 */
void AddSC_instance_ruins_of_ahnqiraj()
{
    new instance_ruins_of_ahnqiraj();
}
