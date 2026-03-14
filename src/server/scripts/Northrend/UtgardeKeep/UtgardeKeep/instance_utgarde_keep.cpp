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
 * @file instance_utgarde_keep.cpp
 * @brief 诺森德副本"乌特加德城堡"实例脚本
 *
 * 模块职责：
 * 1. 管理副本内所有BOSS的状态和GUID
 * 2. 管理副本内的门和游戏对象状态
 * 3. 处理熔炉事件（Forge Event）的状态和保存
 * 4. 管理Skarvald和Dalronn小BOSS的随从关系
 * 5. 保存和加载副本进度数据
 *
 * 副本包含的BOSS：
 * - 凯雷塞斯王子（Prince Keleseth）
 * - Skarvald和Dalronn（双子BOSS）
 * - 掠夺者英格瓦（Ingvar the Plunderer，最终BOSS）
 *
 * 特殊机制：
 * - 3个熔炉事件（可交互的游戏对象，完成会保存进度）
 * - 最终BOSS前的门控制
 *
 * @author TrinityCore Team
 * @date 2026
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "utgarde_keep.h"

/**
 * @brief 门数据配置数组
 *
 * 定义副本中的门与BOSS状态的关联关系
 */
DoorData const doorData[] =
{
    { GO_GIANT_PORTCULLIS_1,    DATA_INGVAR,    DOOR_TYPE_PASSAGE },  // 英格瓦前的门1
    { GO_GIANT_PORTCULLIS_2,    DATA_INGVAR,    DOOR_TYPE_PASSAGE },  // 英格瓦前的门2
    { 0,                        0,              DOOR_TYPE_ROOM }       // 结束标记
};

/**
 * @brief 随从数据配置数组
 *
 * 定义小BOSS的随从关系（Skarvald和Dalronn互相是随从）
 */
MinionData const minionData[] =
{
    { NPC_SKARVALD,     DATA_SKARVALD_DALRONN },  // Skarvald是Dalronn的随从
    { NPC_DALRONN,      DATA_SKARVALD_DALRONN },  // Dalronn是Skarvald的随从
    { 0,                0 }                       // 结束标记
};

/**
 * @class instance_utgarde_keep
 * @brief 乌特加德城堡实例脚本类
 *
 * 继承自：InstanceMapScript（实例地图脚本基类）
 *
 * 职责：
 * - 管理乌特加德城堡副本的整体逻辑
 * - 提供实例脚本的具体实现
 */
class instance_utgarde_keep : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * @param UKScriptName 脚本名称
         * @param 574 乌特加德城堡的地图ID
         */
        instance_utgarde_keep() : InstanceMapScript(UKScriptName, 574) { }

        /**
         * @struct instance_utgarde_keep_InstanceMapScript
         * @brief 乌特加德城堡实例脚本的具体实现
         *
         * 继承自：InstanceScript
         *
         * 职责：
         * - 跟踪副本内所有BOSS和小怪的GUID
         * - 管理熔炉事件的状态
         * - 处理游戏对象的创建和状态更新
         * - 保存和加载副本进度
         */
        struct instance_utgarde_keep_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             */
            instance_utgarde_keep_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                // 设置数据头标识
                SetHeaders(DataHeader);
                // 设置BOSS数量
                SetBossNumber(EncounterCount);
                // 加载门数据配置
                LoadDoorData(doorData);
                // 加载随从数据配置
                LoadMinionData(minionData);
            }

            /**
             * @brief 生物创建时的处理函数
             * @param creature 被创建的生物对象
             *
             * 调用时机：
             * - 副本中的任何生物被创建时（生成或召唤）
             *
             * 功能：
             * - 记录BOSS的GUID
             * - 处理随从BOSS的从属关系
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_PRINCE_KELESETH:
                        // 记录凯雷塞斯王子的GUID
                        PrinceKelesethGUID = creature->GetGUID();
                        break;
                    case NPC_SKARVALD:
                        // 记录Skarvald的GUID
                        SkarvaldGUID = creature->GetGUID();
                        // 添加为随从（用于双子BOSS机制）
                        AddMinion(creature, true);
                        break;
                    case NPC_DALRONN:
                        // 记录Dalronn的GUID
                        DalronnGUID = creature->GetGUID();
                        // 添加为随从（用于双子BOSS机制）
                        AddMinion(creature, true);
                        break;
                    case NPC_INGVAR:
                        // 记录掠夺者英格瓦的GUID
                        IngvarGUID = creature->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 生物移除时的处理函数
             * @param creature 被移除的生物对象
             *
             * 调用时机：
             * - 副本中的任何生物被移除时（消失或死亡后清理）
             *
             * 功能：
             * - 移除随从BOSS的从属关系
             */
            void OnCreatureRemove(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_SKARVALD:
                    case NPC_DALRONN:
                        // 移除随从状态
                        AddMinion(creature, false);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建时的处理函数
             * @param go 被创建的游戏对象
             *
             * 调用时机：
             * - 副本中的任何游戏对象被创建时
             *
             * 功能：
             * - 记录熔炉相关对象的GUID（风箱、炉火、铁砧）
             * - 根据熔炉事件状态激活或停用对象
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_BELLOW_1:
                        // 记录第1个熔炉的风箱GUID
                        Forges[0].BellowGUID = go->GetGUID();
                        // 如果事件已开始，则激活风箱
                        HandleGameObject(ObjectGuid::Empty, Forges[0].Event != NOT_STARTED, go);
                        break;
                    case GO_BELLOW_2:
                        // 记录第2个熔炉的风箱GUID
                        Forges[1].BellowGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[1].Event != NOT_STARTED, go);
                        break;
                    case GO_BELLOW_3:
                        // 记录第3个熔炉的风箱GUID
                        Forges[2].BellowGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[2].Event != NOT_STARTED, go);
                        break;
                    case GO_FORGEFIRE_1:
                        // 记录第1个熔炉的炉火GUID
                        Forges[0].FireGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[0].Event != NOT_STARTED, go);
                        break;
                    case GO_FORGEFIRE_2:
                        // 记录第2个熔炉的炉火GUID
                        Forges[1].FireGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[1].Event != NOT_STARTED, go);
                        break;
                    case GO_FORGEFIRE_3:
                        // 记录第3个熔炉的炉火GUID
                        Forges[2].FireGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[2].Event != NOT_STARTED, go);
                        break;
                    case GO_GLOWING_ANVIL_1:
                        // 记录第1个熔炉的铁砧GUID
                        Forges[0].AnvilGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[0].Event != NOT_STARTED, go);
                        break;
                    case GO_GLOWING_ANVIL_2:
                        // 记录第2个熔炉的铁砧GUID
                        Forges[1].AnvilGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[1].Event != NOT_STARTED, go);
                        break;
                    case GO_GLOWING_ANVIL_3:
                        // 记录第3个熔炉的铁砧GUID
                        Forges[2].AnvilGUID = go->GetGUID();
                        HandleGameObject(ObjectGuid::Empty, Forges[2].Event != NOT_STARTED, go);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取指定类型数据的GUID
             * @param type 数据类型（BOSS或对象的标识）
             * @return 对应的GUID
             *
             * 调用时机：
             * - 其他脚本需要获取BOSS或对象的GUID时
             *
             * 功能：
             * - 根据类型返回对应的BOSS GUID
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_PRINCE_KELESETH:
                        return PrinceKelesethGUID;
                    case DATA_SKARVALD:
                        return SkarvaldGUID;
                    case DATA_DALRONN:
                        return DalronnGUID;
                    case DATA_INGVAR:
                        return IngvarGUID;
                    default:
                        break;
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 设置指定类型的数据
             * @param type 数据类型
             * @param data 数据值
             *
             * 调用时机：
             * - 熔炉事件状态改变时
             *
             * 功能：
             * - 更新熔炉事件的状态
             * - 控制熔炉相关对象的激活状态
             * - 保存进度到数据库
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_FORGE_1:
                    case DATA_FORGE_2:
                    case DATA_FORGE_3:
                    {
                        // 计算熔炉索引（0-2）
                        uint8 i = type - DATA_FORGE_1;
                        // 控制熔炉相关对象的激活状态
                        HandleGameObject(Forges[i].AnvilGUID, data != NOT_STARTED);
                        HandleGameObject(Forges[i].BellowGUID, data != NOT_STARTED);
                        HandleGameObject(Forges[i].FireGUID, data != NOT_STARTED);
                        // 更新事件状态
                        Forges[i].Event = data;

                        // 如果事件完成，保存到数据库
                        if (data == DONE)
                            SaveToDB();
                        break;
                    }
                    default:
                        break;
                }
            }

            /**
             * @brief 写入额外的保存数据
             * @param data 输出字符串流
             *
             * 调用时机：
             * - 保存副本进度时
             *
             * 功能：
             * - 将3个熔炉事件的状态写入保存数据
             */
            void WriteSaveDataMore(std::ostringstream& data) override
            {
                for (uint8 i = 0; i < 3; ++i)
                    data << Forges[i].Event << ' ';
            }

            /**
             * @brief 读取额外的保存数据
             * @param data 输入字符串流
             *
             * 调用时机：
             * - 加载副本进度时
             *
             * 功能：
             * - 从保存数据中读取3个熔炉事件的状态
             */
            void ReadSaveDataMore(std::istringstream& data) override
            {
                for (uint8 i = 0; i < 3; ++i)
                    data >> Forges[i].Event;
            }

        protected:
            ForgeInfo Forges[3];                // 3个熔炉的信息结构体数组

            ObjectGuid PrinceKelesethGUID;       // 凯雷塞斯王子的GUID
            ObjectGuid SkarvaldGUID;             // Skarvald的GUID
            ObjectGuid DalronnGUID;              // Dalronn的GUID
            ObjectGuid IngvarGUID;               // 掠夺者英格瓦的GUID
        };

        /**
         * @brief 获取实例脚本对象
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
           return new instance_utgarde_keep_InstanceMapScript(map);
        }
};

/**
 * @brief 注册乌特加德城堡实例脚本
 *
 * 调用时机：
 * - 服务器启动时，脚本加载系统会调用此函数
 *
 * 功能：
 * - 创建并注册实例脚本对象
 */
void AddSC_instance_utgarde_keep()
{
    new instance_utgarde_keep();
}
