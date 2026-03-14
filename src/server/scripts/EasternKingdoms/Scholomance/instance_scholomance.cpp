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
 * @file instance_scholomance.cpp
 * @brief 通灵学院副本实例脚本
 *
 * 本模块实现了通灵学院副本的实例管理功能：
 * - 副本内BOSS和游戏对象的状态管理
 * - 黑暗院长甘德林的自动刷新机制
 * - 各个密室门的状态管理
 *
 * 通灵学院包含多个BOSS：
 * - 基尔图诺斯（Kirtonos）
 * - 詹迪斯·巴罗夫（Jandice Barov）
 * - 腐木（Rattlegore）
 * - 阿莱克斯·巴罗夫（Lord Alexei Barov）
 * - 讲师玛丽希亚（Instructor Malicia）
 * - 拉文尼亚（The Ravenian）
 * - 博学者波尔凯尔特（Lorekeeper Polkelt）
 * - 塞欧克瑞拉斯（Doctor Theolen Krastinov）
 * - 伊露希亚·巴罗夫（Lady Illucia Barov）
 * - 黑暗院长甘德林（Darkmaster Gandling）- 最终BOSS
 *
 * 特殊机制：
 * - 击杀6个小BOSS后，甘德林会在主厅自动刷新
 * - 各个密室门在暗影传送门事件时会被控制
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "scholomance.h"

/**
 * @brief 甘德林刷新位置
 *
 * 定义黑暗院长甘德林在主厅的刷新位置
 */
Position const GandlingLoc = { 180.7712f, -5.428603f, 75.57024f, 1.291544f };

/**
 * @brief 通灵学院副本实例脚本类
 *
 * 继承自InstanceMapScript，实现通灵学院副本的实例管理
 */
class instance_scholomance : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称和地图ID（289为通灵学院地图ID）
         */
        instance_scholomance() : InstanceMapScript(ScholomanceScriptName, 289) { }

        /**
         * @brief 获取实例脚本
         * @param map 副本地图对象指针
         * @return 实例脚本对象指针
         *
         * 创建并返回通灵学院实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_scholomance_InstanceMapScript(map);
        }

        /**
         * @brief 通灵学院实例脚本实现类
         *
         * 继承自InstanceScript，实现副本的具体管理逻辑：
         * - 各个密室门的状态管理
         * - BOSS状态跟踪
         * - 甘德林自动刷新机制
         */
        struct instance_scholomance_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图对象指针
             *
             * 初始化副本脚本：
             * - 设置数据头（用于存档验证）
             * - 设置BOSS数量
             */
            instance_scholomance_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);         // 设置存档数据头
                SetBossNumber(EncounterCount);  // 设置BOSS遭遇战数量
            }

            /**
             * @brief 游戏对象创建事件
             * @param go 游戏对象指针
             *
             * 当副本中的游戏对象创建时，保存其GUID供后续使用：
             * - 基尔图诺斯大门
             * - 甘德林大门
             * - 玛丽希亚大门
             * - 塞欧克瑞拉斯大门
             * - 波尔凯尔特大门
             * - 拉文尼亚大门
             * - 巴罗夫大门
             * - 伊露希亚大门
             * - 先兆火盆
             *
             * 调用时机：副本中创建游戏对象时由核心代码调用
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_GATE_KIRTONOS:
                        GateKirtonosGUID = go->GetGUID();
                        break;
                    case GO_GATE_GANDLING:
                        GateGandlingGUID = go->GetGUID();
                        break;
                    case GO_GATE_MALICIA:
                        GateMiliciaGUID = go->GetGUID();
                        break;
                    case GO_GATE_THEOLEN:
                        GateTheolenGUID = go->GetGUID();
                        break;
                    case GO_GATE_POLKELT:
                        GatePolkeltGUID = go->GetGUID();
                        break;
                    case GO_GATE_RAVENIAN:
                        GateRavenianGUID = go->GetGUID();
                        break;
                    case GO_GATE_BAROV:
                        GateBarovGUID = go->GetGUID();
                        break;
                    case GO_GATE_ILLUCIA:
                        GateIlluciaGUID = go->GetGUID();
                        break;
                    case GO_BRAZIER_OF_THE_HERALD:
                        BrazierOfTheHeraldGUID = go->GetGUID();
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 设置BOSS状态
             * @param type BOSS数据类型
             * @param state BOSS遭遇战状态
             * @return 是否设置成功
             *
             * 当BOSS状态改变时检查是否应该刷新甘德林：
             * - 阿莱克斯·巴罗夫
             * - 塞欧克瑞拉斯
             * - 拉文尼亚
             * - 波尔凯尔特
             * - 玛丽希亚
             * - 伊露希亚·巴罗夫
             *
             * 这6个小BOSS都被击杀后，甘德林会自动刷新
             *
             * 调用时机：BOSS状态改变时由核心代码调用
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_LORD_ALEXEI_BAROV:
                    case DATA_DOCTOR_THEOLEN_KRASTINOV:
                    case DATA_THE_RAVENIAN:
                    case DATA_LOREKEEPER_POLKELT:
                    case DATA_INSTRUCTOR_MALICIA:
                    case DATA_LADY_ILLUCIA_BAROV:
                        CheckToSpawnGandling();  // 检查是否应该刷新甘德林
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 获取GUID数据
             * @param type 游戏对象类型
             * @return 游戏对象的GUID
             *
             * 根据游戏对象类型返回对应的GUID：
             * - 基尔图诺斯大门
             * - 甘德林大门
             * - 玛丽希亚大门
             * - 塞欧克瑞拉斯大门
             * - 波尔凯尔特大门
             * - 拉文尼亚大门
             * - 巴罗夫大门
             * - 伊露希亚大门
             * - 先兆火盆
             *
             * 调用时机：由其他脚本调用以获取游戏对象GUID
             */
            ObjectGuid GetGuidData(uint32 type) const override
            {
                switch (type)
                {
                    case GO_GATE_KIRTONOS:
                        return GateKirtonosGUID;
                    case GO_GATE_GANDLING:
                        return GateGandlingGUID;
                    case GO_GATE_MALICIA:
                        return GateMiliciaGUID;
                    case GO_GATE_THEOLEN:
                        return GateTheolenGUID;
                    case GO_GATE_POLKELT:
                        return GatePolkeltGUID;
                    case GO_GATE_RAVENIAN:
                        return GateRavenianGUID;
                    case GO_GATE_BAROV:
                        return GateBarovGUID;
                    case GO_GATE_ILLUCIA:
                        return GateIlluciaGUID;
                    case GO_BRAZIER_OF_THE_HERALD:
                        return BrazierOfTheHeraldGUID;
                    default:
                        break;
                }

                return ObjectGuid::Empty;
            }

            /**
             * @brief 检查前置BOSS是否完成
             * @param bossId BOSS ID
             * @return 是否满足前置条件
             *
             * 检查是否满足刷新甘德林的条件：
             * - 阿莱克斯·巴罗夫已击杀
             * - 塞欧克瑞拉斯已击杀
             * - 拉文尼亚已击杀
             * - 波尔凯尔特已击杀
             * - 玛丽希亚已击杀
             * - 伊露希亚·巴罗夫已击杀
             * - 甘德林尚未击杀
             *
             * 调用时机：由CheckToSpawnGandling函数调用
             */
            bool CheckPreBosses(uint32 bossId) const
            {
                switch (bossId)
                {
                    case DATA_DARKMASTER_GANDLING:
                        // 检查所有前置BOSS是否已击杀
                        if (GetBossState(DATA_LORD_ALEXEI_BAROV) != DONE)
                            return false;
                        if (GetBossState(DATA_DOCTOR_THEOLEN_KRASTINOV) != DONE)
                            return false;
                        if (GetBossState(DATA_THE_RAVENIAN) != DONE)
                            return false;
                        if (GetBossState(DATA_LOREKEEPER_POLKELT) != DONE)
                            return false;
                        if (GetBossState(DATA_INSTRUCTOR_MALICIA) != DONE)
                            return false;
                        if (GetBossState(DATA_LADY_ILLUCIA_BAROV) != DONE)
                            return false;
                        // 确保甘德林尚未击杀
                        if (GetBossState(DATA_DARKMASTER_GANDLING) == DONE)
                            return false;
                        break;
                    default:
                        break;
                }

                return true;
            }

            /**
             * @brief 检查并刷新甘德林
             *
             * 如果满足刷新条件（所有前置BOSS已击杀），则在主厅召唤甘德林
             *
             * 调用时机：
             * - 小BOSS状态改变时
             * - 副本数据加载后
             */
            void CheckToSpawnGandling()
            {
                if (CheckPreBosses(DATA_DARKMASTER_GANDLING))
                    instance->SummonCreature(NPC_DARKMASTER_GANDLING, GandlingLoc);
            }

            /**
             * @brief 读取存档数据的额外处理
             * @param data 输入字符串流
             *
             * 在读取存档数据后检查是否应该刷新甘德林
             * 这确保了副本重置后甘德林的状态正确
             *
             * 调用时机：副本加载存档数据时
             */
            void ReadSaveDataMore(std::istringstream& /*data*/) override
            {
                CheckToSpawnGandling();
            }

        protected:
            ObjectGuid GateKirtonosGUID;       // 基尔图诺斯大门GUID
            ObjectGuid GateGandlingGUID;       // 甘德林大门GUID
            ObjectGuid GateMiliciaGUID;        // 玛丽希亚大门GUID
            ObjectGuid GateTheolenGUID;        // 塞欧克瑞拉斯大门GUID
            ObjectGuid GatePolkeltGUID;        // 波尔凯尔特大门GUID
            ObjectGuid GateRavenianGUID;       // 拉文尼亚大门GUID
            ObjectGuid GateBarovGUID;          // 巴罗夫大门GUID
            ObjectGuid GateIlluciaGUID;        // 伊露希亚大门GUID
            ObjectGuid BrazierOfTheHeraldGUID; // 先兆火盆GUID
        };
};

/**
 * @brief 注册副本脚本
 *
 * 将通灵学院副本脚本注册到脚本系统中
 */
void AddSC_instance_scholomance()
{
    new instance_scholomance();
}
