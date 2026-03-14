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
 * @file instance_zulgurub.cpp
 * @brief 祖尔格拉布副本实例脚本实现
 *
 * 祖尔格拉布(Zul'Gurub)是位于荆棘谷的20人团队副本，以巨魔文明为主题。
 * 本实例脚本负责管理副本的全局状态、Boss进度、游戏对象状态等。
 *
 * 主要功能：
 * 1. 管理Boss战斗状态和进度
 * 2. 处理游戏对象（如贝瑟克之锣）的状态
 * 3. 处理特殊事件（如召唤加兹兰卡）
 * 4. 维护生物和游戏对象的引用
 *
 * 副本特色：
 * - 非线性副本结构，大部分Boss可以按任意顺序击杀
 * - 包含隐藏Boss（疯狂之缘Boss、加兹兰卡等）
 * - 哈卡作为最终Boss，击杀其他高阶祭司会削弱他
 *
 * @note 祖尔格拉布的战斗进度不影响副本重置（IsEncounterInProgress返回false）
 */

#include "zulgurub.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ScriptMgr.h"

/**
 * @brief 祖尔格拉布游戏事件ID枚举
 */
enum ZulGurubGameEventIds
{
    EVENT_MUDSKUNK_LURE = 9104  ///< 泥鳞诱饵事件 - 用于召唤加兹兰卡
};

/**
 * @brief 门数据数组
 *
 * 定义副本中的门控制逻辑。
 * 当Boss状态改变时，相关的门会自动开启或关闭。
 */
DoorData const doorData[] =
{
    { GO_FORCEFIELD, DATA_ARLOKK, DOOR_TYPE_ROOM },  ///< 阿洛克的力场门
    { 0,             0,           DOOR_TYPE_ROOM }   ///< 结束标记
};

/**
 * @brief 生物数据数组
 *
 * 将生物的NPC ID映射到实例数据ID，便于在脚本中引用特定的生物。
 */
ObjectData const creatureData[] =
{
    { NPC_ZEALOT_LORKHAN,     DATA_LORKHAN },            ///< 狂热者洛卡恩
    { NPC_ZEALOT_ZATH,        DATA_ZATH },               ///< 狂热者扎斯
    { NPC_HIGH_PRIEST_THEKAL, DATA_THEKAL },             ///< 高阶祭司塞卡尔
    { NPC_JINDO_THE_HEXXER,   DATA_JINDO },              ///< 妖术师金度
    { NPC_ARLOKK,             DATA_ARLOKK },             ///< 阿洛克
    { NPC_PRIESTESS_MARLI,    DATA_MARLI },              ///< 玛尔里女祭司
    { NPC_VILEBRANCH_SPEAKER, DATA_VILEBRANCH_SPEAKER }, ///< 邪枝发言人
    { NPC_GAHZRANKA,          DATA_GAHZRANKA },          ///< 加兹兰卡
    { NPC_HAKKAR,             DATA_HAKKAR },             ///< 哈卡
    { 0,                      0 }                        ///< 结束标记
};

/**
 * @brief 游戏对象数据数组
 *
 * 将游戏对象的ID映射到实例数据ID，便于在脚本中引用特定的游戏对象。
 */
ObjectData const gameobjectData[] =
{
    { GO_GONG_OF_BETHEKK, DATA_GONG_BETHEKK },  ///< 贝瑟克之锣 - 用于召唤阿洛克
    { 0,                  0 }                   ///< 结束标记
};

/**
 * @brief 祖尔格拉布实例脚本类
 *
 * 继承自InstanceMapScript，提供副本实例的核心管理功能。
 */
class instance_zulgurub : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册祖尔格拉布实例脚本，地图ID为309
         */
        instance_zulgurub(): InstanceMapScript(ZGScriptName, 309) { }

        /**
         * @brief 祖尔格拉布实例脚本实现
         *
         * 实现具体的副本状态管理逻辑
         */
        struct instance_zulgurub_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             *
             * 初始化实例脚本，加载数据头、Boss数量和对象数据
             */
            instance_zulgurub_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadObjectData(creatureData, gameobjectData);
                LoadDoorData(doorData);
            }

            /**
             * @brief 检查是否有战斗正在进行
             * @return 总是返回false
             *
             * 在祖尔格拉布中，此功能未激活。
             * 这意味着副本不会因为有Boss战斗而阻止重置。
             */
            bool IsEncounterInProgress() const override
            {
                // 在祖尔格拉布中未激活
                return false;
            }

            /**
             * @brief 游戏对象创建时的处理
             * @param go 新创建的游戏对象
             *
             * 当游戏对象在副本中生成时调用，根据对象类型执行特定逻辑：
             * - 贝瑟克之锣：如果阿洛克已被击杀，则设置为不可选择
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);

                switch (go->GetEntry())
                {
                    case GO_GONG_OF_BETHEKK:
                        // 如果阿洛克已被击杀，锣不可被选择（防止重复召唤）
                        if (GetBossState(DATA_ARLOKK) == DONE)
                            go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                        else
                            go->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 处理副本事件
             * @param obj 触发事件的世界对象（未使用）
             * @param eventId 事件ID
             *
             * 处理副本中的特殊事件：
             * - 泥鳞诱饵事件：召唤隐藏Boss加兹兰卡
             *
             * 加兹兰卡是祖尔格拉布的隐藏Boss，需要玩家在特定水域
             * 使用泥鳞诱饵来召唤。
             */
            void ProcessEvent(WorldObject* /*obj*/, uint32 eventId) override
            {
                // 泥鳞诱饵事件 - 召唤加兹兰卡
                if (eventId == EVENT_MUDSKUNK_LURE && GetBossState(DATA_GAHZRANKA) != DONE && !GetCreature(DATA_GAHZRANKA))
                    instance->SummonCreature(NPC_GAHZRANKA, { -11688.5f, -1737.74f, 2.6789f, 3.9f });
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_zulgurub_InstanceMapScript(map);
        }
};

/**
 * @brief 注册祖尔格拉布实例脚本
 *
 * 将祖尔格拉布的实例脚本注册到脚本系统中，使其在游戏中可以被正确加载。
 */
void AddSC_instance_zulgurub()
{
    new instance_zulgurub();
}
