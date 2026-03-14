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
 * @file instance_shadow_labyrinth.cpp
 * @brief 暗影迷宫副本实例脚本实现
 *
 * 本模块实现了暗影迷宫副本的实例管理逻辑，包括：
 * - BOSS状态追踪和保存
 * - 门的状态管理（根据BOSS击杀状态自动开关）
 * - 禁锢恶魔（Fel Overseer）计数，用于触发大使赫尔默斯的战斗
 * - 黑心傻瓜生物（Blackheart Dummy）管理，用于黑心激励者的战斗
 *
 * 暗影迷宫包含以下BOSS：
 * 1. 大使赫尔默斯（Ambassador Hellmaw）- 需要先击杀所有禁锢恶魔才能激活
 * 2. 黑心激励者（Blackheart the Inciter）
 * 3. 大师沃皮尔（Grandmaster Vorpil）
 * 4. 摩摩尔（Murmur）
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "shadow_labyrinth.h"

/**
 * @brief 门数据数组
 *
 * 定义了副本中需要根据BOSS状态自动控制的门。
 * 门会在对应BOSS被击杀后打开。
 */
DoorData const doorData[] =
{
    { GO_REFECTORY_DOOR,        DATA_BLACKHEART_THE_INCITER,    DOOR_TYPE_PASSAGE },  ///< 餐厅门 - 黑心激励者击杀后开启
    { GO_SCREAMING_HALL_DOOR,   DATA_GRANDMASTER_VORPIL,        DOOR_TYPE_PASSAGE },  ///< 尖叫大厅门 - 大师沃皮尔击杀后开启
    { 0,                        0,                              DOOR_TYPE_ROOM }     ///< 结束标记
};

/**
 * @brief 暗影迷宫实例脚本类
 *
 * 继承自InstanceMapScript，提供暗影迷宫副本的实例管理功能。
 * 负责追踪BOSS状态、管理游戏对象、处理生物创建和死亡事件等。
 */
class instance_shadow_labyrinth : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册暗影迷宫实例脚本，地图ID为555。
         */
        instance_shadow_labyrinth() : InstanceMapScript(SLScriptName, 555) { }

        /**
         * @brief 暗影迷宫实例地图脚本
         *
         * 继承自InstanceScript，实现具体的副本管理逻辑。
         */
        struct instance_shadow_labyrinth_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * 初始化实例脚本，设置数据头、BOSS数量、加载门数据，
             * 并初始化禁锢恶魔计数器。
             */
            instance_shadow_labyrinth_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);           // 设置数据保存头
                SetBossNumber(EncounterCount);    // 设置BOSS数量
                LoadDoorData(doorData);           // 加载门数据

                FelOverseerCount = 0;             // 初始化禁锢恶魔计数
            }

            /**
             * @brief 生物创建回调
             * @param creature 新创建的生物
             *
             * 当副本中有生物被创建时调用。负责：
             * - 记录各个BOSS的GUID
             * - 记录黑心傻瓜生物的GUID集合
             * - 统计禁锢恶魔数量，并通知大使赫尔默斯
             *
             * @调用时机 当生物在副本中生成时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_AMBASSADOR_HELLMAW:
                        // 记录大使赫尔默斯的GUID
                        AmbassadorHellmawGUID = creature->GetGUID();
                        break;
                    case NPC_BLACKHEART:
                        // 记录黑心激励者的GUID
                        BlackheartGUID = creature->GetGUID();
                        break;
                    case NPC_BLACKHEART_DUMMY1:
                    case NPC_BLACKHEART_DUMMY2:
                    case NPC_BLACKHEART_DUMMY3:
                    case NPC_BLACKHEART_DUMMY4:
                    case NPC_BLACKHEART_DUMMY5:
                        // 记录黑心傻瓜生物的GUID，用于黑心激励者的战斗
                        BlackheartDummyGUIDs.insert(creature->GetGUID());
                        break;
                    case NPC_GRANDMASTER_VORPIL:
                        // 记录大师沃皮尔的GUID
                        GrandmasterVorpilGUID = creature->GetGUID();
                        break;
                    case NPC_FEL_OVERSEER:
                        // 处理禁锢恶魔
                        if (creature->IsAlive())
                        {
                            ++FelOverseerCount;  // 增加存活计数
                            // 通知大使赫尔默斯有新的禁锢恶魔存在（保持禁锢状态）
                            if (Creature* hellmaw = instance->GetCreature(AmbassadorHellmawGUID))
                                hellmaw->AI()->DoAction(ACTION_AMBASSADOR_HELLMAW_BANISH);
                        }
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 生物移除回调
             * @param creature 被移除的生物
             *
             * 当生物从副本中移除时调用。
             * 主要用于从黑心傻瓜GUID集合中移除已消失的生物。
             *
             * @调用时机 当生物从副本中移除时
             */
            void OnCreatureRemove(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_BLACKHEART_DUMMY1:
                    case NPC_BLACKHEART_DUMMY2:
                    case NPC_BLACKHEART_DUMMY3:
                    case NPC_BLACKHEART_DUMMY4:
                    case NPC_BLACKHEART_DUMMY5:
                        // 从黑心傻瓜GUID集合中移除
                        BlackheartDummyGUIDs.erase(creature->GetGUID());
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象创建回调
             * @param go 新创建的游戏对象
             *
             * 当副本中有游戏对象被创建时调用。
             * 负责将门添加到门管理系统中。
             *
             * @调用时机 当游戏对象在副本中生成时
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_REFECTORY_DOOR:
                    case GO_SCREAMING_HALL_DOOR:
                        // 将门添加到门管理系统，状态会根据BOSS状态自动更新
                        AddDoor(go, true);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 游戏对象移除回调
             * @param go 被移除的游戏对象
             *
             * 当游戏对象从副本中移除时调用。
             * 负责从门管理系统中移除门。
             *
             * @调用时机 当游戏对象从副本中移除时
             */
            void OnGameObjectRemove(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_REFECTORY_DOOR:
                    case GO_SCREAMING_HALL_DOOR:
                        // 从门管理系统中移除门
                        AddDoor(go, false);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 单位死亡回调
             * @param unit 死亡的单位
             *
             * 当副本中有单位死亡时调用。
             * 主要处理禁锢恶魔的死亡，当所有禁锢恶魔死亡时，
             * 触发大使赫尔默斯的介绍事件。
             *
             * @调用时机 当副本中有单位死亡时
             */
            void OnUnitDeath(Unit* unit) override
            {
                Creature* creature = unit->ToCreature();
                if (!creature)
                    return;

                if (creature->GetEntry() == NPC_FEL_OVERSEER)
                {
                    // 禁锢恶魔死亡
                    if (FelOverseerCount)
                        --FelOverseerCount;

                    // 当所有禁锢恶魔都被击杀时，触发大使赫尔默斯的介绍事件
                    if (!FelOverseerCount)
                        if (Creature* hellmaw = instance->GetCreature(AmbassadorHellmawGUID))
                            hellmaw->AI()->DoAction(ACTION_AMBASSADOR_HELLMAW_INTRO);
                }
            }

            /**
             * @brief 获取实例数据
             * @param type 数据类型
             * @return 对应的数据值
             *
             * 返回实例相关数据。
             * 目前只支持DATA_FEL_OVERSEER，返回禁锢恶魔是否全部被击杀。
             *
             * @调用时机 当其他脚本请求数据时
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_FEL_OVERSEER:
                        // 返回1表示所有禁锢恶魔已被击杀，返回0表示还有存活的禁锢恶魔
                        return !FelOverseerCount ? 1 : 0;
                    default:
                        break;
                }
                return 0;
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型
             * @return 对应的GUID
             *
             * 返回指定类型BOSS的GUID。
             *
             * @调用时机 当其他脚本请求BOSS GUID时
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_BLACKHEART_THE_INCITER:
                        return BlackheartGUID;
                    case DATA_GRANDMASTER_VORPIL:
                        return GrandmasterVorpilGUID;
                    default:
                        break;
                }
                return ObjectGuid::Empty;
            }

            /**
             * @brief 获取黑心傻瓜GUID集合
             * @return 黑心傻瓜生物的GUID集合引用
             *
             * 返回黑心傻瓜生物的GUID集合，用于黑心激励者的战斗。
             *
             * @调用时机 黑心激励者AI需要获取傻瓜生物时
             */
            GuidUnorderedSet const& GetBlackheartDummies() const { return BlackheartDummyGUIDs; }

        protected:
            ObjectGuid AmbassadorHellmawGUID;      ///< 大使赫尔默斯的GUID
            ObjectGuid BlackheartGUID;             ///< 黑心激励者的GUID
            GuidUnorderedSet BlackheartDummyGUIDs; ///< 黑心傻瓜生物的GUID集合
            ObjectGuid GrandmasterVorpilGUID;      ///< 大师沃皮尔的GUID
            uint32 FelOverseerCount;               ///< 存活的禁锢恶魔数量
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         *
         * 工厂方法，创建并返回暗影迷宫的实例脚本。
         *
         * @调用时机 当副本实例被创建时
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_shadow_labyrinth_InstanceMapScript(map);
        }
};

/**
 * @brief 获取黑心傻瓜GUID集合
 * @param s 实例脚本指针
 * @return 黑心傻瓜GUID集合指针，如果类型不匹配则返回nullptr
 *
 * 全局辅助函数，用于从实例脚本中获取黑心傻瓜生物的GUID集合。
 * 这个函数供黑心激励者的AI调用。
 *
 * @调用时机 黑心激励者AI需要获取傻瓜生物时
 */
GuidUnorderedSet const* GetBlackheartDummies(InstanceScript const* s)
{
    // 使用dynamic_cast进行类型安全转换
    if (auto* script = dynamic_cast<instance_shadow_labyrinth::instance_shadow_labyrinth_InstanceMapScript const*>(s))
        return &script->GetBlackheartDummies();
    return nullptr;
}

/**
 * @brief 注册暗影迷宫实例脚本
 *
 * 此函数在服务器启动时被调用，注册暗影迷宫实例脚本。
 */
void AddSC_instance_shadow_labyrinth()
{
    new instance_shadow_labyrinth();
}
