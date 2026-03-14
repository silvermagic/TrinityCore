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
 * @file instance_nexus.cpp
 * @brief 诺森德副本"魔枢"实例脚本
 *
 * 模块职责：
 * 1. 管理副本内所有BOSS的状态和GUID
 * 2. 管理束缚之球游戏对象的状态
 * 3. 根据玩家阵营动态切换小怪类型（联盟/部落）
 * 4. 保存和加载副本进度数据
 *
 * 副本包含的BOSS：
 * - 大魔导师泰蕾斯塔（Magus Telestra）
 * - 异常者（Anomalus）
 * - 奥摩洛克（Ormorok the Tree-Shaper）
 * - 克莉斯塔萨（Keristrasza，最终BOSS）
 *
 * 特殊机制：
 * - 三个束缚之球，每个对应一个前置BOSS
 * - 激活所有束缚之球后解除最终BOSS的冻结监狱
 * - 根据进入副本的玩家阵营显示对应的小怪
 *
 * @author TrinityCore Team
 * @date 2026
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "nexus.h"
#include "Player.h"

/**
 * @class instance_nexus
 * @brief 魔枢实例脚本类
 *
 * 继承自：InstanceMapScript（实例地图脚本基类）
 *
 * 职责：
 * - 管理魔枢副本的整体逻辑
 * - 提供实例脚本的具体实现
 */
class instance_nexus : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * @param NexusScriptName 脚本名称
         * @param 576 魔枢的地图ID
         */
        instance_nexus() : InstanceMapScript(NexusScriptName, 576) { }

        /**
         * @struct instance_nexus_InstanceMapScript
         * @brief 魔枢实例脚本的具体实现
         *
         * 继承自：InstanceScript
         *
         * 职责：
         * - 跟踪副本内所有BOSS和游戏对象的GUID
         * - 根据玩家阵营动态切换小怪类型
         * - 管理束缚之球的状态变化
         * - 处理BOSS状态更新时的游戏对象联动
         */
        struct instance_nexus_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             */
            instance_nexus_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                // 设置数据头标识
                SetHeaders(DataHeader);
                // 设置BOSS数量
                SetBossNumber(EncounterCount);
                // 初始化阵营为0（未确定）
                _teamInInstance = 0;
            }

            /**
             * @brief 玩家进入副本时的处理函数
             * @param player 进入的玩家
             *
             * 调用时机：
             * - 玩家进入副本时
             *
             * 功能：
             * - 记录首先进入副本的玩家的阵营
             * - 用于确定小怪的类型（联盟/部落）
             */
            void OnPlayerEnter(Player* player) override
            {
                if (!_teamInInstance)
                    _teamInInstance = player->GetTeam();
            }

            /**
             * @brief 生物创建时的处理函数
             * @param creature 创建的生物
             *
             * 调用时机：
             * - 副本中的生物被创建或加载时
             *
             * 功能：
             * - 记录重要BOSS的GUID
             * - 根据服务器设置调整联盟小怪的阵营
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case NPC_ANOMALUS:
                        // 记录异常者的GUID
                        AnomalusGUID = creature->GetGUID();
                        break;
                    case NPC_KERISTRASZA:
                        // 记录克莉斯塔萨的GUID
                        KeristraszaGUID = creature->GetGUID();
                        break;
                    case NPC_ALLIANCE_BERSERKER:
                    case NPC_ALLIANCE_RANGER:
                    case NPC_ALLIANCE_CLERIC:
                    case NPC_ALLIANCE_COMMANDER:
                    case NPC_COMMANDER_STOUTBEARD:
                        // 联盟小怪处理
                        // 如果服务器允许跨阵营组队，将联盟小怪设为中立阵营
                        // 这样部落玩家也可以攻击它们
                        if (ServerAllowsTwoSideGroups())
                            creature->SetFaction(FACTION_MONSTER_2);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 获取生物入口ID
             * @param guidLow 生物GUID的低32位
             * @param data 生物数据
             * @return 实际使用的生物入口ID
             *
             * 调用时机：
             * - 副本生成生物前
             *
             * 功能：
             * - 根据玩家阵营动态返回对应的小怪入口ID
             * - 联盟玩家看到部落小怪，部落玩家看到联盟小怪
             *
             * 机制说明：
             * - 默认配置是联盟小怪
             * - 如果进入的玩家是联盟阵营，则切换为部落小怪
             * - 这样玩家可以体验到"对抗敌方阵营"的感觉
             */
            uint32 GetCreatureEntry(ObjectGuid::LowType /*guidLow*/, CreatureData const* data) override
            {
                // 如果还未确定阵营，从当前副本中的玩家获取
                if (!_teamInInstance)
                {
                    Map::PlayerList const& players = instance->GetPlayers();
                    if (!players.isEmpty())
                        if (Player* player = players.begin()->GetSource())
                            _teamInInstance = player->GetTeam();
                }

                uint32 entry = data->id;
                switch (entry)
                {
                    case NPC_ALLIANCE_BERSERKER:
                        // 联盟玩家看到部落狂战士，部落玩家看到联盟狂战士
                        return _teamInInstance == ALLIANCE ? NPC_HORDE_BERSERKER : NPC_ALLIANCE_BERSERKER;
                    case NPC_ALLIANCE_RANGER:
                        // 联盟玩家看到部落游侠，部落玩家看到联盟游侠
                        return _teamInInstance == ALLIANCE ? NPC_HORDE_RANGER : NPC_ALLIANCE_RANGER;
                    case NPC_ALLIANCE_CLERIC:
                        // 联盟玩家看到部落牧师，部落玩家看到联盟牧师
                        return _teamInInstance == ALLIANCE ? NPC_HORDE_CLERIC : NPC_ALLIANCE_CLERIC;
                    case NPC_ALLIANCE_COMMANDER:
                        // 联盟玩家看到部落指挥官，部落玩家看到联盟指挥官
                        return _teamInInstance == ALLIANCE ? NPC_HORDE_COMMANDER : NPC_ALLIANCE_COMMANDER;
                    case NPC_COMMANDER_STOUTBEARD:
                        // 联盟玩家看到库鲁格指挥官，部落玩家看到斯托特比尔德指挥官
                        return _teamInInstance == ALLIANCE ? NPC_COMMANDER_KOLURG : NPC_COMMANDER_STOUTBEARD;
                    default:
                        return entry;
                }
            }

            /**
             * @brief 游戏对象创建时的处理函数
             * @param go 创建的游戏对象
             *
             * 调用时机：
             * - 副本中的游戏对象被创建或加载时
             *
             * 功能：
             * - 记录束缚之球的GUID
             * - 如果对应的BOSS已被击杀，解除束缚之球的不可选择标志
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_ANOMALUS_CONTAINMENT_SPHERE:
                        // 异常者的束缚之球
                        AnomalusContainmentSphere = go->GetGUID();
                        // 如果异常者已被击杀，解除不可选择标志，允许玩家交互
                        if (GetBossState(DATA_ANOMALUS) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_ORMOROKS_CONTAINMENT_SPHERE:
                        // 奥摩洛克的束缚之球
                        OrmoroksContainmentSphere = go->GetGUID();
                        // 如果奥摩洛克已被击杀，解除不可选择标志
                        if (GetBossState(DATA_ORMOROK) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case GO_TELESTRAS_CONTAINMENT_SPHERE:
                        // 泰蕾斯塔的束缚之球
                        TelestrasContainmentSphere = go->GetGUID();
                        // 如果泰蕾斯塔已被击杀，解除不可选择标志
                        if (GetBossState(DATA_MAGUS_TELESTRA) == DONE)
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 设置BOSS状态
             * @param type BOSS数据类型ID
             * @param state 遭遇战状态
             * @return 是否设置成功
             *
             * 调用时机：
             * - BOSS战斗状态发生变化时（开始、失败、成功等）
             *
             * 功能：
             * - 调用父类的SetBossState方法保存状态
             * - 当BOSS被击杀时，解除对应束缚之球的不可选择标志
             * - 允许玩家交互束缚之球以解除最终BOSS的冻结
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                // 调用父类方法，如果失败则返回
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_MAGUS_TELESTRA:
                        // 大魔导师泰蕾斯塔
                        if (state == DONE)
                        {
                            // 击杀后解除泰蕾斯塔束缚之球的不可选择标志
                            if (GameObject* sphere = instance->GetGameObject(TelestrasContainmentSphere))
                                sphere->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                    case DATA_ANOMALUS:
                        // 异常者
                        if (state == DONE)
                        {
                            // 击杀后解除异常者束缚之球的不可选择标志
                            if (GameObject* sphere = instance->GetGameObject(AnomalusContainmentSphere))
                                sphere->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                    case DATA_ORMOROK:
                        // 奥摩洛克
                        if (state == DONE)
                        {
                            // 击杀后解除奥摩洛克束缚之球的不可选择标志
                            if (GameObject* sphere = instance->GetGameObject(OrmoroksContainmentSphere))
                                sphere->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 获取GUID数据
             * @param type 数据类型ID
             * @return 对应的GUID
             *
             * 调用时机：
             * - 其他脚本需要获取特定BOSS或游戏对象的GUID时
             *
             * 功能：
             * - 根据类型ID返回对应的GUID
             * - 用于BOSS脚本和游戏对象脚本之间的通信
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_ANOMALUS:
                        // 返回异常者的GUID
                        return AnomalusGUID;
                    case DATA_KERISTRASZA:
                        // 返回克莉斯塔萨的GUID
                        return KeristraszaGUID;
                    case ANOMALUS_CONTAINMENT_SPHERE:
                        // 返回异常者束缚之球的GUID
                        return AnomalusContainmentSphere;
                    case ORMOROKS_CONTAINMENT_SPHERE:
                        // 返回奥摩洛克束缚之球的GUID
                        return OrmoroksContainmentSphere;
                    case TELESTRAS_CONTAINMENT_SPHERE:
                        // 返回泰蕾斯塔束缚之球的GUID
                        return TelestrasContainmentSphere;
                    default:
                        break;
                }

                return ObjectGuid::Empty;
            }

        private:
            ObjectGuid AnomalusGUID;                    // 异常者BOSS的GUID
            ObjectGuid KeristraszaGUID;                 // 克莉斯塔萨BOSS的GUID
            ObjectGuid AnomalusContainmentSphere;       // 异常者束缚之球的GUID
            ObjectGuid OrmoroksContainmentSphere;       // 奥摩洛克束缚之球的GUID
            ObjectGuid TelestrasContainmentSphere;      // 泰蕾斯塔束缚之球的GUID
            uint32 _teamInInstance;                     // 首先进入副本的玩家的阵营（ALLIANCE或HORDE）
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_nexus_InstanceMapScript(map);
        }
};

/**
 * @brief 注册实例脚本
 *
 * 调用时机：
 * - 服务器启动时，脚本加载系统会调用此函数
 *
 * 功能：
 * - 注册魔枢实例脚本
 */
void AddSC_instance_nexus()
{
    new instance_nexus();
}
