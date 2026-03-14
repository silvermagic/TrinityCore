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
 * @file    instance_gnomeregan.cpp
 * @brief   诺莫瑞根副本实例脚本实现
 *
 * @details 该模块实现了诺莫瑞根副本的实例管理功能，主要包括：
 *          - Boss击杀状态管理
 *          - 隧道门的状态控制
 *          - 爆破专家艾米·短路护送事件管理
 *          - 关键生物和GameObject的GUID存储
 *
 *          诺莫瑞根副本包含以下Boss：
 *          - Vicious Fallout（恶性辐射尘）
 *          - Electrocutioner 6000（电刑器6000）
 *          - Crowd Pummler（人群粉碎者）
 *          - Mekgineer Thermaplugg（机电师瑟玛普拉格）
 *
 * @note    诺莫瑞根是一个位于丹莫罗的高级副本，主要面向24-33级的玩家。
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "gnomeregan.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"

/**
 * @class instance_gnomeregan
 * @brief 诺莫瑞根副本实例脚本类
 *
 * @details 该类负责创建和管理诺莫瑞根副本实例。
 *          它继承自InstanceMapScript，提供了创建实例脚本的接口。
 */
class instance_gnomeregan : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数，注册副本脚本
     *
     * @details 参数说明：
     *          - GNOScriptName: 脚本名称
     *          - 90: 诺莫瑞根的地图ID
     */
    instance_gnomeregan() : InstanceMapScript(GNOScriptName, 90) { }

    /**
     * @brief 获取实例脚本对象
     *
     * @param map 副本地图指针
     * @return InstanceScript* 返回新创建的实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_gnomeregan_InstanceMapScript(map);
    }

    /**
     * @struct instance_gnomeregan_InstanceMapScript
     * @brief 诺莫瑞根副本实例脚本实现
     *
     * @details 管理副本内的所有状态，包括：
     *          - Boss击杀状态
     *          - 隧道门的开闭状态
     *          - 爆破专家艾米·短路护送事件状态
     *          - 关键GameObject和Creature的GUID存储
     */
    struct instance_gnomeregan_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数，初始化副本实例
         *
         * @param map 副本地图指针
         *
         * @details 初始化副本脚本，设置数据头、Boss数量。
         */
        instance_gnomeregan_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);        // 设置数据头标识
            SetBossNumber(MAX_ENCOUNTER);  // 设置Boss数量
        }

        ObjectGuid uiCaveInLeftGUID;             ///< 左侧隧道门GUID
        ObjectGuid uiCaveInRightGUID;            ///< 右侧隧道门GUID

        ObjectGuid uiBlastmasterEmiShortfuseGUID; ///< 爆破专家艾米·短路NPC的GUID

        /**
         * @brief 生物创建回调
         *
         * @param creature 新创建的生物
         *
         * @details 当副本中创建生物时触发。
         *          记录关键NPC的GUID，如爆破专家艾米·短路。
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_BLASTMASTER_EMI_SHORTFUSE:
                    uiBlastmasterEmiShortfuseGUID = creature->GetGUID();  // 记录艾米·短路的GUID
                    break;
            }
        }

        /**
         * @brief GameObject创建回调
         *
         * @param go 新创建的GameObject
         *
         * @details 当副本中创建GameObject时触发。
         *          记录关键GameObject的GUID，如隧道门。
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_CAVE_IN_LEFT:
                    uiCaveInLeftGUID = go->GetGUID();   // 记录左侧隧道门GUID
                    break;
                case GO_CAVE_IN_RIGHT:
                    uiCaveInRightGUID = go->GetGUID();  // 记录右侧隧道门GUID
                    break;
            }
        }

        /**
         * @brief 单位死亡回调
         *
         * @param unit 死亡的单位
         *
         * @details 当副本中有单位死亡时触发。
         *          检查死亡单位是否为Boss，如果是则设置Boss状态为完成。
         */
        void OnUnitDeath(Unit* unit) override
        {
            Creature* creature = unit->ToCreature();
            if (creature)
                switch (creature->GetEntry())
                {
                    case NPC_VICIOUS_FALLOUT:
                        SetBossState(DATA_VICIOUS_FALLOUT, DONE);  // 恶性辐射尘击杀
                        break;
                    case NPC_ELECTROCUTIONER:
                        SetBossState(DATA_ELECTROCUTIONER, DONE);  // 电刑器6000击杀
                        break;
                    case NPC_CROWD_PUMMELER:
                        SetBossState(DATA_CROWD_PUMMELER, DONE);   // 人群粉碎者击杀
                        break;
                    case NPC_MEKGINEER:
                        SetBossState(DATA_THERMAPLUGG, DONE);      // 机电师瑟玛普拉格击杀
                        break;
                }
        }

        /**
         * @brief 获取GUID数据回调
         *
         * @param uiType 数据类型标识
         * @return ObjectGuid 返回对应的GUID
         *
         * @details 用于外部脚本获取副本中关键对象的GUID。
         *          包括隧道门和爆破专家艾米·短路的GUID。
         */
        ObjectGuid GetGuidData(uint32 uiType) const override
        {
            switch (uiType)
            {
                case DATA_GO_CAVE_IN_LEFT:              return uiCaveInLeftGUID;            // 返回左侧隧道门GUID
                case DATA_GO_CAVE_IN_RIGHT:             return uiCaveInRightGUID;           // 返回右侧隧道门GUID
                case DATA_NPC_BASTMASTER_EMI_SHORTFUSE: return uiBlastmasterEmiShortfuseGUID; // 返回艾米·短路GUID
            }

            return ObjectGuid::Empty;
        }
    };

};

/**
 * @brief 注册诺莫瑞根副本实例脚本
 *
 * @details 该函数在服务器启动时被调用，用于注册副本实例脚本。
 *          这是TrinityCore脚本系统的标准入口点。
 */
void AddSC_instance_gnomeregan()
{
    new instance_gnomeregan();
}
