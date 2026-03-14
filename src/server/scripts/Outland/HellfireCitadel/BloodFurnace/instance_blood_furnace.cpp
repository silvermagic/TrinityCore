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
 * @file instance_blood_furnace.cpp
 * @brief 鲜血熔炉副本实例脚本模块
 *
 * 本模块实现鲜血熔炉副本的实例管理逻辑,包括:
 * 1. Boss状态管理和持久化
 * 2. 门和机关的控制
 * 3. 布洛戈克战斗中的囚犯释放事件
 * 4. 囚犯分组管理和激活逻辑
 *
 * 副本结构:
 * - 制造者: 第一个Boss
 * - 布洛戈克: 第二个Boss,需要通过释放囚犯事件激活
 * - 破坏者凯里丹: 最后一个Boss
 *
 * 特殊机制:
 * - 布洛戈克战斗涉及8个牢房,分4批释放囚犯
 * - 每批囚犯全部击杀后开启下一个牢房
 * - 所有囚犯击杀后布洛戈克激活
 */

#include "ScriptMgr.h"
#include "blood_furnace.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptedCreature.h"

/**
 * @brief 门数据配置表
 *
 * 定义副本中各个门与Boss状态的关联关系
 * 当Boss状态改变时,自动控制门的开闭
 */
DoorData const doorData[] =
{
    { GO_PRISON_DOOR_01, DATA_KELIDAN_THE_BREAKER, DOOR_TYPE_PASSAGE },  // 凯里丹后的过道门
    { GO_PRISON_DOOR_02, DATA_THE_MAKER,           DOOR_TYPE_ROOM },     // 制造者房间门
    { GO_PRISON_DOOR_03, DATA_THE_MAKER,           DOOR_TYPE_PASSAGE },  // 制造者后的过道门
    { GO_PRISON_DOOR_04, DATA_BROGGOK,             DOOR_TYPE_PASSAGE },  // 布洛戈克后的过道门
    { GO_PRISON_DOOR_05, DATA_BROGGOK,             DOOR_TYPE_ROOM },     // 布洛戈克房间门
    { GO_SUMMON_DOOR,    DATA_KELIDAN_THE_BREAKER, DOOR_TYPE_PASSAGE },  // 召唤门
    { 0,                 0,                        DOOR_TYPE_ROOM }      // 结束标记
};

/**
 * @brief 生物数据配置表
 *
 * 定义需要保存和查询的生物GUID
 */
ObjectData const creatureData[] =
{
    { NPC_BROGGOK,             DATA_BROGGOK             },  // 布洛戈克
    { 0,                       0                        }   // 结束标记
};

/**
 * @brief 游戏对象数据配置表
 *
 * 定义需要保存和查询的游戏对象GUID
 */
ObjectData const gameObjectData[] =
{
    { GO_BROGGOK_LEVER,      DATA_BROGGOK_LEVER },  // 布洛戈克拉杆
    { 0,                     0                  }   // 结束标记
};

/**
 * @brief 鲜血熔炉副本实例脚本类
 *
 * 管理副本状态、Boss进度和特殊事件
 */
class instance_blood_furnace : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册鲜血熔炉副本实例脚本,地图ID为542
         */
        instance_blood_furnace() : InstanceMapScript(BFScriptName, 542) { }

        /**
         * @brief 鲜血熔炉实例脚本实现类
         *
         * 继承自InstanceScript,实现副本的完整管理逻辑
         */
        struct instance_blood_furnace_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本脚本:
             * 1. 设置数据头和Boss数量
             * 2. 加载门数据和对象数据
             * 3. 初始化囚犯计数器
             */
            instance_blood_furnace_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadDoorData(doorData);  // 加载门与Boss的关联数据
                LoadObjectData(creatureData, gameObjectData);  // 加载对象数据

                // 初始化4个牢房的囚犯计数器
                PrisonerCounter5        = 0;
                PrisonerCounter6        = 0;
                PrisonerCounter7        = 0;
                PrisonerCounter8        = 0;
            }

            /**
             * @brief 生物创建时调用
             * @param creature 新创建的生物
             *
             * 当副本中有新生物刷新时:
             * 1. 调用基类处理
             * 2. 根据生物类型保存GUID:
             *    - 三个Boss的GUID
             *    - 囚犯按位置分配到对应牢房
             *
             * @调用时机 副本中任何生物刷新时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                InstanceScript::OnCreatureCreate(creature);

                switch (creature->GetEntry())
                {
                    case NPC_THE_MAKER:
                        TheMakerGUID = creature->GetGUID();  // 保存制造者GUID
                        break;
                    case NPC_BROGGOK:
                        BroggokGUID = creature->GetGUID();  // 保存布洛戈克GUID
                        break;
                    case NPC_KELIDAN_THE_BREAKER:
                        KelidanTheBreakerGUID = creature->GetGUID();  // 保存凯里丹GUID
                        break;
                    case NPC_PRISONER1:
                    case NPC_PRISONER2:
                        StorePrisoner(creature);  // 存储囚犯到对应牢房
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 单位死亡时调用
             * @param unit 死亡的单位
             *
             * 监控囚犯死亡事件,当囚犯死亡时:
             * - 检查是否是囚犯类型的生物
             * - 触发囚犯死亡处理(可能开启下一个牢房)
             *
             * @调用时机 副本中任何单位死亡时
             */
            void OnUnitDeath(Unit* unit) override
            {
                // 只处理囚犯死亡事件
                if (unit->GetTypeId() == TYPEID_UNIT && (unit->GetEntry() == NPC_PRISONER1 || unit->GetEntry() == NPC_PRISONER2))
                    PrisonerDied(unit->GetGUID());
            }

            /**
             * @brief 游戏对象创建时调用
             * @param go 新创建的游戏对象
             *
             * 当副本中有新游戏对象刷新时:
             * 1. 调用基类处理
             * 2. 保存重要游戏对象的GUID:
             *    - 监狱门4(布洛戈克房间的门)
             *    - 布洛戈克拉杆
             *    - 8个牢房门(用于释放囚犯)
             *
             * @调用时机 副本中任何游戏对象刷新时
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_PRISON_DOOR_04:
                        PrisonDoor4GUID = go->GetGUID();  // 保存布洛戈克房间门GUID
                        break;
                    case GO_BROGGOK_LEVER:
                        BroggokLeverGUID = go->GetGUID();  // 保存布洛戈克拉杆GUID
                        break;
                    case GO_PRISON_CELL_DOOR_1:
                        PrisonCellGUIDs[DATA_PRISON_CELL1 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房1门
                        break;
                    case GO_PRISON_CELL_DOOR_2:
                        PrisonCellGUIDs[DATA_PRISON_CELL2 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房2门
                        break;
                    case GO_PRISON_CELL_DOOR_3:
                        PrisonCellGUIDs[DATA_PRISON_CELL3 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房3门
                        break;
                    case GO_PRISON_CELL_DOOR_4:
                        PrisonCellGUIDs[DATA_PRISON_CELL4 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房4门
                        break;
                    case GO_PRISON_CELL_DOOR_5:
                        PrisonCellGUIDs[DATA_PRISON_CELL5 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房5门(布洛戈克战斗第一批)
                        break;
                    case GO_PRISON_CELL_DOOR_6:
                        PrisonCellGUIDs[DATA_PRISON_CELL6 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房6门
                        break;
                    case GO_PRISON_CELL_DOOR_7:
                        PrisonCellGUIDs[DATA_PRISON_CELL7 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房7门
                        break;
                    case GO_PRISON_CELL_DOOR_8:
                        PrisonCellGUIDs[DATA_PRISON_CELL8 - DATA_PRISON_CELL1] = go->GetGUID();  // 牢房8门
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取对象GUID
             * @param type 数据类型标识
             * @return 对应类型的对象GUID,不存在则返回空GUID
             *
             * 根据数据类型返回对应的Boss或重要对象GUID
             *
             * @调用时机 Boss AI或脚本需要获取其他对象引用时
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_THE_MAKER:
                        return TheMakerGUID;  // 返回制造者GUID
                    case DATA_BROGGOK:
                        return BroggokGUID;  // 返回布洛戈克GUID
                    case DATA_KELIDAN_THE_BREAKER:
                        return KelidanTheBreakerGUID;  // 返回凯里丹GUID
                    case DATA_BROGGOK_LEVER:
                        return BroggokLeverGUID;  // 返回布洛戈克拉杆GUID
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 设置Boss状态
             * @param type Boss类型标识
             * @param state 新的战斗状态
             * @return 成功返回true,失败返回false
             *
             * 当Boss状态改变时触发相应逻辑:
             * - 布洛戈克战斗开始:激活第5个牢房
             * - 布洛戈克战斗重置:重置所有牢房和囚犯
             *
             * @调用时机 Boss战斗开始、结束、重置时
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_BROGGOK:
                        switch (state)
                        {
                            case IN_PROGRESS:
                                ActivateCell(DATA_PRISON_CELL5);  // 战斗开始:激活第一批囚犯
                                break;
                            case NOT_STARTED:
                                ResetPrisons();  // 战斗重置:重置所有牢房
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 重置所有牢房
             *
             * 重置布洛戈克战斗相关的4个牢房:
             * 1. 重置每个牢房的囚犯
             * 2. 重置囚犯计数器
             * 3. 关闭所有牢房门
             *
             * @调用时机 布洛戈克战斗重置时(SetBossState中调用)
             */
            void ResetPrisons()
            {
                // 重置第5牢房
                ResetPrisoners(PrisonersCell5);
                PrisonerCounter5 = uint8(PrisonersCell5.size());
                HandleGameObject(PrisonCellGUIDs[DATA_PRISON_CELL5 - DATA_PRISON_CELL1], false);

                // 重置第6牢房
                ResetPrisoners(PrisonersCell6);
                PrisonerCounter6 = uint8(PrisonersCell6.size());
                HandleGameObject(PrisonCellGUIDs[DATA_PRISON_CELL6 - DATA_PRISON_CELL1], false);

                // 重置第7牢房
                ResetPrisoners(PrisonersCell7);
                PrisonerCounter7 = uint8(PrisonersCell7.size());
                HandleGameObject(PrisonCellGUIDs[DATA_PRISON_CELL7 - DATA_PRISON_CELL1], false);

                // 重置第8牢房
                ResetPrisoners(PrisonersCell8);
                PrisonerCounter8 = uint8(PrisonersCell8.size());
                HandleGameObject(PrisonCellGUIDs[DATA_PRISON_CELL8 - DATA_PRISON_CELL1], false);
            }

            /**
             * @brief 重置一组囚犯
             * @param prisoners 囚犯GUID集合
             *
             * 遍历集合中的所有囚犯:
             * 1. 如果囚犯已死亡,从集合中移除
             * 2. 重置囚犯状态(复活、设为不可攻击等)
             *
             * @调用时机 ResetPrisons()中调用
             */
            void ResetPrisoners(GuidSet& prisoners)
            {
                for (GuidSet::const_iterator i = prisoners.begin(); i != prisoners.end();)
                {
                    if (Creature * prisoner = instance->GetCreature(*i))
                    {
                        // 如果囚犯已死亡,从集合中移除
                        if (!prisoner->IsAlive())
                            i = prisoners.erase(i);
                        else
                            ++i;

                        ResetPrisoner(prisoner);  // 重置囚犯状态
                    }
                    else
                        ++i;
                }
            }

            /**
             * @brief 重置单个囚犯
             * @param prisoner 囚犯生物指针
             *
             * 重置囚犯到初始状态:
             * 1. 如果已死亡,则复活
             * 2. 设置为不可攻击状态
             * 3. 设置免疫所有伤害
             * 4. 让AI进入逃避模式
             *
             * @调用时机 ResetPrisoners()和StorePrisoner()中调用
             */
            void ResetPrisoner(Creature* prisoner)
            {
                // 如果已死亡,复活囚犯
                if (!prisoner->IsAlive())
                    prisoner->Respawn(true);
                // 设置为不可攻击状态
                prisoner->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                prisoner->SetImmuneToAll(true);
                // 让AI进入逃避模式(返回初始位置)
                if (prisoner->IsAIEnabled())
                    prisoner->AI()->EnterEvadeMode();
            }

            /**
             * @brief 存储囚犯到对应牢房
             * @param creature 囚犯生物指针
             *
             * 根据囚犯的坐标位置判断属于哪个牢房:
             * - 第5牢房: X[405-423], Y[106-123], Z<=17
             * - 第6牢房: X[405-423], Y[76-91], Z<=17
             * - 第7牢房: X[490-506], Y[106-123], Z<=17
             * - 第8牢房: X[490-506], Y[76-91], Z<=17
             *
             * 存储后会立即重置囚犯状态
             *
             * @调用时机 OnCreatureCreate()中检测到囚犯时
             */
            void StorePrisoner(Creature* creature)
            {
                float posX = creature->GetPositionX();
                float posY = creature->GetPositionY();
                float posZ = creature->GetPositionZ();

                // 第一组牢房(第5、6牢房)
                if (posX >= 405.0f && posX <= 423.0f && posZ <= 17)
                {
                    if (posY >= 106.0f && posY <= 123.0f)
                    {
                        // 第5牢房(战斗开始的第一批)
                        PrisonersCell5.insert(creature->GetGUID());
                        ++PrisonerCounter5;
                    }
                    else if (posY >= 76.0f && posY <= 91.0f)
                    {
                        // 第6牢房
                        PrisonersCell6.insert(creature->GetGUID());
                        ++PrisonerCounter6;
                    }
                    else return;
                }
                // 第二组牢房(第7、8牢房)
                else if (posX >= 490.0f && posX <= 506.0f && posZ <= 17)
                {
                    if (posY >= 106.0f && posY <= 123.0f)
                    {
                        // 第7牢房
                        PrisonersCell7.insert(creature->GetGUID());
                        ++PrisonerCounter7;
                    }
                    else if (posY >= 76.0f && posY <= 91.0f)
                    {
                        // 第8牢房
                        PrisonersCell8.insert(creature->GetGUID());
                        ++PrisonerCounter8;
                    }
                    else
                        return;
                }
                else
                    return;

                ResetPrisoner(creature);  // 重置囚犯状态
            }

            /**
             * @brief 囚犯死亡处理
             * @param guid 死亡囚犯的GUID
             *
             * 当囚犯死亡时:
             * 1. 减少对应牢房的囚犯计数器
             * 2. 如果计数器降为0,激活下一个牢房或布洛戈克:
             *    - 第5牢房清空 -> 开启第6牢房
             *    - 第6牢房清空 -> 开启第7牢房
             *    - 第7牢房清空 -> 开启第8牢房
             *    - 第8牢房清空 -> 开启布洛戈克房间的门并激活布洛戈克
             *
             * @调用时机 OnUnitDeath()中检测到囚犯死亡时
             */
            void PrisonerDied(ObjectGuid guid)
            {
                // 检查囚犯属于哪个牢房,减少计数器,如果清空则激活下一个
                if (PrisonersCell5.find(guid) != PrisonersCell5.end() && --PrisonerCounter5 <= 0)
                    ActivateCell(DATA_PRISON_CELL6);  // 第5牢房清空,开启第6牢房
                else if (PrisonersCell6.find(guid) != PrisonersCell6.end() && --PrisonerCounter6 <= 0)
                    ActivateCell(DATA_PRISON_CELL7);  // 第6牢房清空,开启第7牢房
                else if (PrisonersCell7.find(guid) != PrisonersCell7.end() && --PrisonerCounter7 <= 0)
                    ActivateCell(DATA_PRISON_CELL8);  // 第7牢房清空,开启第8牢房
                else if (PrisonersCell8.find(guid) != PrisonersCell8.end() && --PrisonerCounter8 <= 0)
                    ActivateCell(DATA_DOOR_4);  // 第8牢房清空,激活布洛戈克
            }

            /**
             * @brief 激活牢房或Boss
             * @param id 牢房ID或门ID
             *
             * 根据ID执行不同操作:
             * - DATA_PRISON_CELL5-8: 开启对应牢房门,激活囚犯进入战斗
             * - DATA_DOOR_4: 开启布洛戈克房间门,激活布洛戈克Boss
             *
             * @调用时机 SetBossState(布洛戈克战斗开始)或PrisonerDied(牢房清空)时
             */
            void ActivateCell(uint8 id)
            {
                switch (id)
                {
                    case DATA_PRISON_CELL5:
                        // 开启第5牢房门,激活囚犯
                        HandleGameObject(PrisonCellGUIDs[id - DATA_PRISON_CELL1], true);
                        ActivatePrisoners(PrisonersCell5);
                        break;
                    case DATA_PRISON_CELL6:
                        // 开启第6牢房门,激活囚犯
                        HandleGameObject(PrisonCellGUIDs[id - DATA_PRISON_CELL1], true);
                        ActivatePrisoners(PrisonersCell6);
                        break;
                    case DATA_PRISON_CELL7:
                        // 开启第7牢房门,激活囚犯
                        HandleGameObject(PrisonCellGUIDs[id - DATA_PRISON_CELL1], true);
                        ActivatePrisoners(PrisonersCell7);
                        break;
                    case DATA_PRISON_CELL8:
                        // 开启第8牢房门,激活囚犯
                        HandleGameObject(PrisonCellGUIDs[id - DATA_PRISON_CELL1], true);
                        ActivatePrisoners(PrisonersCell8);
                        break;
                    case DATA_DOOR_4:
                        // 所有囚犯已击杀,开启布洛戈克房间门并激活Boss
                        HandleGameObject(PrisonDoor4GUID, true);
                        if (Creature* broggok = instance->GetCreature(BroggokGUID))
                            broggok->AI()->DoAction(ACTION_ACTIVATE_BROGGOK);
                        break;
                }
            }

            /**
             * @brief 激活一组囚犯
             * @param prisoners 囚犯GUID集合
             *
             * 将囚犯从被动状态激活为攻击状态:
             * 1. 移除不可攻击标志
             * 2. 取消免疫状态
             * 3. 让囚犯进入战斗并攻击玩家
             *
             * @调用时机 ActivateCell()中开启牢房时
             */
            void ActivatePrisoners(GuidSet const& prisoners)
            {
                for (GuidSet::const_iterator i = prisoners.begin(); i != prisoners.end(); ++i)
                    if (Creature* prisoner = instance->GetCreature(*i))
                    {
                        // 移除不可攻击和免疫状态
                        prisoner->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                        prisoner->SetImmuneToAll(false);
                        // 让囚犯进入战斗,攻击附近玩家
                        prisoner->AI()->DoZoneInCombat();
                    }
            }

        protected:
            // Boss GUID
            ObjectGuid TheMakerGUID;             ///< 制造者Boss的GUID
            ObjectGuid BroggokGUID;              ///< 布洛戈克Boss的GUID
            ObjectGuid KelidanTheBreakerGUID;    ///< 凯里丹Boss的GUID

            // 重要游戏对象GUID
            ObjectGuid BroggokLeverGUID;         ///< 布洛戈克拉杆的GUID
            ObjectGuid PrisonDoor4GUID;          ///< 监狱门4(布洛戈克房间门)的GUID

            // 牢房门GUID数组
            ObjectGuid PrisonCellGUIDs[8];       ///< 8个牢房门的GUID数组

            // 囚犯管理
            GuidSet PrisonersCell5;              ///< 第5牢房的囚犯GUID集合
            GuidSet PrisonersCell6;              ///< 第6牢房的囚犯GUID集合
            GuidSet PrisonersCell7;              ///< 第7牢房的囚犯GUID集合
            GuidSet PrisonersCell8;              ///< 第8牢房的囚犯GUID集合

            // 囚犯计数器
            uint8 PrisonerCounter5;              ///< 第5牢房剩余囚犯数量
            uint8 PrisonerCounter6;              ///< 第6牢房剩余囚犯数量
            uint8 PrisonerCounter7;              ///< 第7牢房剩余囚犯数量
            uint8 PrisonerCounter8;              ///< 第8牢房剩余囚犯数量
        };

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 实例脚本指针
         *
         * 工厂方法,创建并返回鲜血熔炉实例脚本
         *
         * @调用时机 服务器创建副本实例时
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_blood_furnace_InstanceMapScript(map);
        }
};

/**
 * @brief 注册脚本函数
 *
 * 将鲜血熔炉实例脚本注册到脚本系统
 *
 * @调用时机 服务器启动时,脚本系统初始化阶段
 */
void AddSC_instance_blood_furnace()
{
    new instance_blood_furnace();
}
