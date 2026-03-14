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
 * @file instance_onyxias_lair.cpp
 * @brief 奥妮克希亚巢穴副本实例脚本
 *
 * 本模块实现了奥妮克希亚巢穴副本的实例管理逻辑，包括：
 * - 首领状态管理
 * - 地面喷发效果处理（BFS算法）
 * - 成就系统支持
 *   - Many Whelps! Handle It! - 10秒内孵化50个蛋
 *   - She Deep Breaths More - 所有人躲避深呼吸
 * - 幼龙生成管理
 *
 * @author TrinityCore Team
 * @date 2024
 */

/* ScriptData
SDName: Instance_Onyxias_Lair
SD%Complete: 100
SDComment:
SDCategory: Onyxia's Lair
EndScriptData */

#include "ScriptMgr.h"
#include "AreaBoundary.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "onyxias_lair.h"
#include "TemporarySummon.h"

/**
 * @brief 首领边界数据
 * 定义奥妮克希亚的活动范围，防止首领被拉出战斗区域
 * 使用圆形边界，中心点 (-34.3697, -212.3296)，半径 100 码
 */
BossBoundaryData const boundaries =
{
    { DATA_ONYXIA, new CircleBoundary(Position(-34.3697f, -212.3296f), 100.0) }
};

/**
 * @brief 奥妮克希亚巢穴副本实例脚本类
 *
 * 管理整个副本的状态和特殊机制，包括：
 * - 首领战斗状态
 * - 地面喷发效果（广度优先搜索算法）
 * - 幼龙生成计数
 * - 成就判定
 */
class instance_onyxias_lair : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * 初始化副本脚本，地图ID为249（奥妮克希亚巢穴）
     */
    instance_onyxias_lair() : InstanceMapScript(OnyxiaScriptName, 249) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_onyxias_lair_InstanceMapScript(map);
    }

    /**
     * @brief 奥妮克希亚巢穴实例脚本实现
     *
     * 核心实例逻辑，处理副本内的各种事件和状态
     */
    struct instance_onyxias_lair_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化实例数据、首领数量、边界和计时器
         */
        instance_onyxias_lair_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadBossBoundaries(boundaries);

            onyxiaLiftoffTimer = 0;
            manyWhelpsCounter = 0;
            eruptTimer = 0;

            achievManyWhelpsHandleIt = false;
            achievSheDeepBreathMore = true;
        }

        //Eruption is a BFS graph problem
        //One map to remember all floor, one map to keep floor that still need to erupt and one queue to know what needs to be removed
        /**
         * @brief 生物创建回调
         * @param creature 新创建的生物
         *
         * 记录奥妮克希亚的GUID，用于后续访问
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_ONYXIA:
                    onyxiaGUID = creature->GetGUID();  // 保存奥妮克希亚的GUID
                    break;
            }
        }

        /**
         * @brief 游戏对象创建回调
         * @param go 新创建的游戏对象
         *
         * 处理两种游戏对象：
         * 1. 地面喷发陷阱（displayId 4392/4472, spellId 17731）
         * 2. 幼龙生成器（GO_WHELP_SPAWNER）
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            // 检查是否为地面喷发陷阱
            if ((go->GetGOInfo()->displayId == 4392 || go->GetGOInfo()->displayId == 4472) && go->GetGOInfo()->trap.spellId == 17731)
            {
                FloorEruptionGUID[0].insert(std::make_pair(go->GetGUID(), 0));  // 添加到喷发列表，初始深度为0
                return;
            }

            switch (go->GetEntry())
            {
                case GO_WHELP_SPAWNER:  // 幼龙生成器
                    Position goPos = go->GetPosition();
                    // 在生成器位置召唤幼龙
                    if (Creature* temp = go->SummonCreature(NPC_WHELP, goPos, TEMPSUMMON_CORPSE_DESPAWN))
                    {
                        temp->AI()->DoZoneInCombat();  // 让幼龙进入战斗
                        ++manyWhelpsCounter;           // 增加幼龙计数
                    }
                    break;
            }
        }

        /**
         * @brief 游戏对象移除回调
         * @param go 被移除的游戏对象
         *
         * 从地面喷发列表中移除被删除的游戏对象
         */
        void OnGameObjectRemove(GameObject* go) override
        {
            // 从喷发列表中移除
            if ((go->GetGOInfo()->displayId == 4392 || go->GetGOInfo()->displayId == 4472) && go->GetGOInfo()->trap.spellId == 17731)
            {
                FloorEruptionGUID[0].erase(go->GetGUID());
                return;
            }
        }

        /**
         * @brief 地面喷发处理函数
         * @param floorEruptedGUID 要喷发的地面GUID
         *
         * 使用广度优先搜索（BFS）算法处理地面喷发的扩散效果：
         * 1. 触发指定地面的喷发动画和伤害
         * 2. 查找附近的地面游戏对象
         * 3. 更新它们的深度值并加入队列
         * 4. 从待处理列表中移除已喷发的地面
         *
         * 性能注意事项：
         * - 使用BFS算法确保喷发按距离扩散
         * - 每次只处理15码范围内的地面
         */
        void FloorEruption(ObjectGuid floorEruptedGUID)
        {
            if (GameObject* floorEruption = instance->GetGameObject(floorEruptedGUID))
            {
                //THIS GOB IS A TRAP - What shall i do? =(
                //Cast it spell? Copyed Heigan method
                // 触发喷发动画
                floorEruption->SendCustomAnim(floorEruption->GetGoAnimProgress());
                // 施放陷阱法术
                CastSpellExtraArgs args;
                args.OriginalCaster = onyxiaGUID;  // 设置施法者为奥妮克希亚
                floorEruption->CastSpell(floorEruption, floorEruption->GetGOInfo()->trap.spellId, args);

                //Get all immediatly nearby floors
                // 查找15码范围内的所有地面游戏对象
                std::list<GameObject*> nearFloorList;
                Trinity::GameObjectInRangeCheck check(floorEruption->GetPositionX(), floorEruption->GetPositionY(), floorEruption->GetPositionZ(), 15);
                Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(floorEruption, nearFloorList, check);
                Cell::VisitGridObjects(floorEruption, searcher, SIZE_OF_GRIDS);

                //remove all that are not present on FloorEruptionGUID[1] and update treeLen on each GUID
                // 更新附近地面的深度值并加入队列
                for (std::list<GameObject*>::const_iterator itr = nearFloorList.begin(); itr != nearFloorList.end(); ++itr)
                {
                    // 检查是否为地面喷发陷阱
                    if (((*itr)->GetGOInfo()->displayId == 4392 || (*itr)->GetGOInfo()->displayId == 4472) && (*itr)->GetGOInfo()->trap.spellId == 17731)
                    {
                        ObjectGuid nearFloorGUID = (*itr)->GetGUID();
                        // 如果该地面在待处理列表中且深度为0（未处理）
                        if (FloorEruptionGUID[1].find(nearFloorGUID) != FloorEruptionGUID[1].end() && (*FloorEruptionGUID[1].find(nearFloorGUID)).second == 0)
                        {
                            // 设置深度为当前地面深度+1
                            (*FloorEruptionGUID[1].find(nearFloorGUID)).second = (*FloorEruptionGUID[1].find(floorEruptedGUID)).second+1;
                            FloorEruptionGUIDQueue.push(nearFloorGUID);  // 加入处理队列
                        }
                    }
                }
            }
            // 从待处理列表中移除已喷发的地面
            FloorEruptionGUID[1].erase(floorEruptedGUID);
        }

        /**
         * @brief 设置首领状态
         * @param type 首领类型ID
         * @param state 战斗状态
         * @return 是否成功设置状态
         *
         * 当奥妮克希亚进入战斗时，同时启动"She Deep Breaths More"成就追踪
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_ONYXIA:
                    if (state == IN_PROGRESS)
                        SetBossState(DATA_SHE_DEEP_BREATH_MORE, IN_PROGRESS);  // 启动深呼吸成就追踪
                    break;
            }
            return true;
        }

        /**
         * @brief 设置实例数据
         * @param type 数据类型
         * @param data 数据值
         *
         * 处理两种数据类型：
         * 1. DATA_ONYXIA_PHASE: 奥妮克希亚阶段转换
         *    - PHASE_BREATH: 启动幼龙计数计时器（10秒）
         * 2. DATA_SHE_DEEP_BREATH_MORE: 深呼吸成就状态
         *    - IN_PROGRESS: 重置成就标志为true
         *    - FAIL: 设置成就标志为false
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_ONYXIA_PHASE:
                    if (data == PHASE_BREATH) //Used to mark the liftoff phase
                    {
                        achievManyWhelpsHandleIt = false;  // 重置幼龙成就标志
                        manyWhelpsCounter = 0;             // 重置幼龙计数器
                        onyxiaLiftoffTimer = 10000;        // 启动10秒计时器
                    }
                    break;
                case DATA_SHE_DEEP_BREATH_MORE:
                    if (data == IN_PROGRESS)
                    {
                        achievSheDeepBreathMore = true;  // 成就可以完成
                    }
                    else if (data == FAIL)
                    {
                        achievSheDeepBreathMore = false;  // 成就失败
                    }
                    break;
            }
        }

        /**
         * @brief 设置GUID数据
         * @param type 数据类型
         * @param data GUID数据
         *
         * 用于启动地面喷发效果：
         * - 复制所有地面到待处理列表
         * - 将起始地面加入队列
         * - 启动喷发计时器
         */
        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            switch (type)
            {
                case DATA_FLOOR_ERUPTION_GUID:
                    FloorEruptionGUID[1] = FloorEruptionGUID[0];  // 复制所有地面到待处理列表
                    FloorEruptionGUIDQueue.push(data);            // 将起始地面加入队列
                    eruptTimer = 2500;                            // 2.5秒后开始喷发
                    break;
            }
        }

        /**
         * @brief 获取GUID数据
         * @param data 数据类型
         * @return 对应的GUID，如果未找到则返回空GUID
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            switch (data)
            {
                case NPC_ONYXIA:
                    return onyxiaGUID;  // 返回奥妮克希亚的GUID
            }

            return ObjectGuid::Empty;
        }

        /**
         * @brief 实例更新主循环
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理两个主要逻辑：
         * 1. 幼龙成就判定：在10秒计时结束后检查是否召唤了50只以上的幼龙
         * 2. 地面喷发处理：按BFS顺序处理喷发队列
         *
         * 性能注意事项：
         * - 地面喷发每次处理同一深度的所有地面
         * - 喷发间隔1秒
         */
        void Update(uint32 diff) override
        {
            // 检查奥妮克希亚是否在战斗中
            if (GetBossState(DATA_ONYXIA) == IN_PROGRESS)
            {
                // 幼龙计数计时器
                if (onyxiaLiftoffTimer && onyxiaLiftoffTimer <= diff)
                {
                    onyxiaLiftoffTimer = 0;
                    // 如果10秒内召唤了50只以上幼龙，成就完成
                    if (manyWhelpsCounter >= 50)
                        achievManyWhelpsHandleIt = true;
                } else onyxiaLiftoffTimer -= diff;
            }

            // 处理地面喷发队列
            if (!FloorEruptionGUIDQueue.empty())
            {
                if (eruptTimer <= diff)
                {
                    ObjectGuid frontGuid = FloorEruptionGUIDQueue.front();
                    std::map<ObjectGuid, uint32>::iterator itr = FloorEruptionGUID[1].find(frontGuid);
                    if (itr != FloorEruptionGUID[1].end())
                    {
                        uint32 treeHeight = itr->second;  // 获取当前深度

                        // 处理同一深度的所有地面
                        do
                        {
                            FloorEruption(frontGuid);              // 触发喷发
                            FloorEruptionGUIDQueue.pop();          // 从队列移除
                            if (FloorEruptionGUIDQueue.empty())
                                break;

                            frontGuid = FloorEruptionGUIDQueue.front();
                            itr = FloorEruptionGUID[1].find(frontGuid);
                        } while (itr != FloorEruptionGUID[1].end() && itr->second == treeHeight);  // 继续处理同深度的地面
                    }

                    eruptTimer = 1000;  // 1秒后处理下一批
                }
                else
                    eruptTimer -= diff;
            }
        }

        /**
         * @brief 检查成就条件是否满足
         * @param criteriaId 成就条件ID
         * @param source 触发玩家（未使用）
         * @param target 目标单位（未使用）
         * @param miscValue1 附加值（未使用）
         * @return 是否满足成就条件
         *
         * 支持的成就：
         * - ACHIEV_CRITERIA_MANY_WHELPS: 10秒内孵化50个蛋
         * - ACHIEV_CRITERIA_DEEP_BREATH: 所有人躲避深呼吸
         */
        bool CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* /*source*/, Unit const* /*target = nullptr*/, uint32 /*miscValue1 = 0*/) override
        {
            switch (criteriaId)
            {
                case ACHIEV_CRITERIA_MANY_WHELPS_10_PLAYER:  // Criteria for achievement 4403: Many Whelps! Handle It! (10 player) Hatch 50 eggs in 10s
                case ACHIEV_CRITERIA_MANY_WHELPS_25_PLAYER:  // Criteria for achievement 4406: Many Whelps! Handle It! (25 player) Hatch 50 eggs in 10s
                    return achievManyWhelpsHandleIt;
                case ACHIEV_CRITERIA_DEEP_BREATH_10_PLAYER:  // Criteria for achievement 4404: She Deep Breaths More (10 player) Everybody evade Deep Breath
                case ACHIEV_CRITERIA_DEEP_BREATH_25_PLAYER:  // Criteria for achievement 4407: She Deep Breaths More (25 player) Everybody evade Deep Breath
                    return achievSheDeepBreathMore;
            }
            return false;
        }

    protected:
        std::map<ObjectGuid, uint32> FloorEruptionGUID[2];  // [0]: 所有地面, [1]: 待喷发地面（值为深度）
        std::queue<ObjectGuid> FloorEruptionGUIDQueue;      // 喷发处理队列
        ObjectGuid onyxiaGUID;                              // 奥妮克希亚的GUID
        uint32 onyxiaLiftoffTimer;                          // 奥妮克希亚起飞计时器
        uint32 manyWhelpsCounter;                           // 幼龙计数器
        uint32 eruptTimer;                                  // 喷发计时器
        bool   achievManyWhelpsHandleIt;                    // 幼龙成就标志
        bool   achievSheDeepBreathMore;                     // 深呼吸成就标志
    };
};

/**
 * @brief 注册实例脚本
 *
 * 创建并注册奥妮克希亚巢穴副本实例脚本
 */
void AddSC_instance_onyxias_lair()
{
    new instance_onyxias_lair();
}
