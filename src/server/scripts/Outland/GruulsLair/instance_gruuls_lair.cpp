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
 * @file instance_gruuls_lair.cpp
 * @brief 格鲁尔的巢穴副本实例脚本
 *
 * 本模块实现了格鲁尔的巢穴副本的实例管理功能，包括：
 * - 副本中门的状态管理（根据Boss击杀状态自动开关）
 * - 小怪与Boss的关联管理（Maulgar及其四个助手）
 * - Boss的GUID存储与查询
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "gruuls_lair.h"
#include "InstanceScript.h"

/**
 * @brief 门数据配置表
 *
 * 定义副本中各个门与Boss状态的关联关系
 * - DOOR_TYPE_PASSAGE: 通道类型门，Boss死亡后自动打开
 * - DOOR_TYPE_ROOM: 房间类型门，战斗期间关闭
 */
DoorData const doorData[] =
{
    { GO_MAULGAR_DOOR,  DATA_MAULGAR,   DOOR_TYPE_PASSAGE },  // 莫加尔大王门，Boss死亡后开启通道
    { GO_GRUUL_DOOR,    DATA_GRUUL,     DOOR_TYPE_ROOM },     // 格鲁尔门，战斗期间关闭房间
    { 0,                0,              DOOR_TYPE_ROOM }      // 结束标记
};

/**
 * @brief 小怪数据配置表
 *
 * 定义Boss战相关的小怪，用于与小怪死亡检测关联
 * 莫加尔大王战包含5个怪物：Boss本身 + 4个助手
 */
MinionData const minionData[] =
{
    { NPC_MAULGAR,              DATA_MAULGAR },  // 莫加尔大王
    { NPC_KROSH_FIREHAND,       DATA_MAULGAR },  // 克罗斯·火手（法师）
    { NPC_OLM_THE_SUMMONER,     DATA_MAULGAR },  // 奥尔姆·召唤者（术士）
    { NPC_KIGGLER_THE_CRAZED,   DATA_MAULGAR },  // 疯狂的克格尔（萨满）
    { NPC_BLINDEYE_THE_SEER,    DATA_MAULGAR },  // 盲眼先知（牧师）
    { 0, 0 }                                     // 结束标记
};

/**
 * @class instance_gruuls_lair
 * @brief 格鲁尔的巢穴副本脚本类
 *
 * 继承自InstanceMapScript，负责注册和管理整个副本实例
 */
class instance_gruuls_lair : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册格鲁尔的巢穴副本脚本，地图ID为565
         */
        instance_gruuls_lair() : InstanceMapScript(GLScriptName, 565) { }

        /**
         * @struct instance_gruuls_lair_InstanceMapScript
         * @brief 副本实例的核心逻辑实现
         *
         * 继承自InstanceScript，管理副本中的Boss状态、门状态和小怪关联
         */
        struct instance_gruuls_lair_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本实例：
             * - 设置数据头标识
             * - 设置Boss数量
             * - 加载门数据和小怪数据
             */
            instance_gruuls_lair_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadDoorData(doorData);
                LoadMinionData(minionData);
            }

            /**
             * @brief 生物创建回调
             * @param creature 新创建的生物指针
             *
             * 当副本中有生物创建时调用，用于保存关键生物的GUID
             * 目前仅保存莫加尔大王的GUID，供其他脚本查询使用
             *
             * 调用时机：生物在副本中生成时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);

                switch (creature->GetEntry())
                {
                    case NPC_MAULGAR:
                        MaulgarGUID = creature->GetGUID();  // 保存莫加尔大王GUID
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型标识
             * @return 对应的GUID，如果不存在返回空GUID
             *
             * 根据数据类型返回存储的GUID，供其他脚本查询使用
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_MAULGAR:
                        return MaulgarGUID;  // 返回莫加尔大王GUID
                    default:
                        break;
                }
                return ObjectGuid::Empty;
            }

        protected:
            ObjectGuid MaulgarGUID;  ///< 莫加尔大王的GUID，用于查询和关联
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 新创建的实例脚本对象
         *
         * 工厂方法，创建副本实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_gruuls_lair_InstanceMapScript(map);
        }
};

/**
 * @brief 注册副本脚本
 *
 * 在服务器启动时调用，注册格鲁尔的巢穴副本脚本
 * 该函数由脚本系统自动调用
 */
void AddSC_instance_gruuls_lair()
{
    new instance_gruuls_lair();
}
