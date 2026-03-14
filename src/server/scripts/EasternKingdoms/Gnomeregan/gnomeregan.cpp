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
 * @file    gnomeregan.cpp
 * @brief   诺莫瑞根副本脚本实现
 *
 * @details 该模块实现了诺莫瑞根副本中的主要功能，包括：
 *          - 爆破专家艾米·短路(NPC Blastmaster Emi Shortfuse)护送任务
 *          - Grubbis Boss战斗
 *          - 收集辐射尘法术脚本
 *
 *          爆破专家艾米·短路护送任务流程：
 *          1. 玩家与NPC对话触发护送任务
 *          2. NPC带领玩家穿越副本，沿途触发对话
 *          3. 在多个地点召唤敌人伏击
 *          4. 在特定地点放置炸药炸开隧道
 *          5. 最终召唤Boss Grubbis并击败
 *          6. 任务完成后放置红色火箭信号
 *
 * @note    该脚本实现了约90%的功能，部分视觉效果未实现。
 *
 * Script Data Start
 * SDName: Gnomeregan
 * SDAuthor: Manuel
 * SD%Complete: 90%
 * SDComment: Some visual effects are not implemented.
 * Script Data End
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "gnomeregan.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 爆破专家艾米·短路对话和文本ID枚举
 */
enum BlastmasterEmi
{
    SAY_BLASTMASTER_0   = 0,   ///< 初始对话
    SAY_BLASTMASTER_1   = 1,   ///< 第一段对话
    SAY_BLASTMASTER_2   = 2,   ///< 第二段对话
    SAY_BLASTMASTER_3   = 3,   ///< 第三段对话
    SAY_BLASTMASTER_4   = 4,   ///< 第四段对话
    SAY_BLASTMASTER_5   = 5,   ///< 第五段对话（开始放置炸药）
    SAY_BLASTMASTER_6   = 6,   ///< 第六段对话
    SAY_BLASTMASTER_7   = 7,   ///< 第七段对话
    SAY_BLASTMASTER_8   = 8,   ///< 第八段对话
    SAY_BLASTMASTER_9   = 9,   ///< 第九段对话
    SAY_BLASTMASTER_10  = 10,  ///< 第十段对话
    SAY_BLASTMASTER_11  = 11,  ///< 第十一段对话（爆炸效果）
    SAY_BLASTMASTER_12  = 12,  ///< 第十二段对话
    SAY_BLASTMASTER_13  = 13,  ///< 第十三段对话
    SAY_BLASTMASTER_14  = 14,  ///< 第十四段对话
    SAY_BLASTMASTER_15  = 15,  ///< 第十五段对话
    SAY_BLASTMASTER_16  = 16,  ///< 第十六段对话
    SAY_BLASTMASTER_17  = 17,  ///< 第十七段对话
    SAY_BLASTMASTER_18  = 18,  ///< 第十八段对话
    SAY_BLASTMASTER_19  = 19,  ///< 最终对话

    SAY_GRUBBIS         = 0    ///< Grubbis Boss对话
};

/**
 * @brief 生物生成位置坐标数组
 *
 * @details 定义了在护送任务中各种生物的生成位置。
 *          包括洞穴伏击者、Grubbis和Chomper等。
 */
const Position SpawnPosition[] =
{
    {-557.630f, -114.514f, -152.209f, 0.641f},   ///< 洞穴伏击者位置1
    {-555.263f, -113.802f, -152.737f, 0.311f},   ///< 洞穴伏击者位置2
    {-552.154f, -112.476f, -153.349f, 0.621f},   ///< 洞穴伏击者位置3
    {-548.692f, -111.089f, -154.090f, 0.621f},   ///< 洞穴伏击者位置4
    {-546.905f, -108.340f, -154.877f, 0.729f},   ///< 洞穴伏击者位置5
    {-547.736f, -105.154f, -155.176f, 0.372f},   ///< 洞穴伏击者位置6
    {-547.274f, -114.109f, -153.952f, 0.735f},   ///< 洞穴伏击者位置7
    {-552.534f, -110.012f, -153.577f, 0.747f},   ///< 洞穴伏击者位置8
    {-550.708f, -116.436f, -153.103f, 0.679f},   ///< 洞穴伏击者位置9
    {-554.030f, -115.983f, -152.635f, 0.695f},   ///< 洞穴伏击者位置10
    {-494.595f, -87.516f, -149.116f, 3.344f},    ///< 洞穴伏击者位置11
    {-493.349f, -90.845f, -148.882f, 3.717f},    ///< 洞穴伏击者位置12
    {-491.995f, -87.619f, -148.197f, 3.230f},    ///< 洞穴伏击者位置13
    {-490.732f, -90.739f, -148.091f, 3.230f},    ///< 洞穴伏击者位置14
    {-490.554f, -89.114f, -148.055f, 3.230f},    ///< 洞穴伏击者位置15
    {-495.240f, -90.808f, -149.493f, 3.238f},    ///< 洞穴伏击者位置16
    {-494.195f, -89.553f, -149.131f, 3.254f},    ///< 洞穴伏击者位置17
    {-511.3304f, -139.9622f, -152.4761f, 0.7504908f},  ///< 红色火箭位置1
    {-510.6754f, -139.4371f, -152.6167f, 3.33359f},    ///< 红色火箭位置2
    {-511.8976f, -139.3562f, -152.4785f, 3.961899f}    ///< 红色火箭位置3
};

/**
 * @class npc_blastmaster_emi_shortfuse
 * @brief 爆破专家艾米·短路NPC脚本类
 *
 * @details 该类实现了诺莫瑞根副本中护送任务NPC艾米·短路的AI。
 *          该NPC会带领玩家穿越副本，沿途触发对话、召唤敌人、
 *          放置炸药炸开隧道，最终召唤Boss Grubbis。
 */
class npc_blastmaster_emi_shortfuse : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册NPC脚本名称
     */
    npc_blastmaster_emi_shortfuse() : CreatureScript("npc_blastmaster_emi_shortfuse") { }

    /**
     * @brief 获取NPC的AI实例
     *
     * @param creature 生物对象指针
     * @return CreatureAI* 返回NPC的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetGnomereganAI<npc_blastmaster_emi_shortfuseAI>(creature);
    }

    /**
     * @struct npc_blastmaster_emi_shortfuseAI
     * @brief 爆破专家艾米·短路的护送AI实现
     *
     * @details 该AI实现了复杂的护送任务流程：
     *          - 玩家交互触发护送
     *          - 沿路径点移动并触发事件
     *          - 在特定地点召唤敌人
     *          - 放置炸药并炸开隧道
     *          - 召唤Boss Grubbis
     *          - 完成任务后消失
     */
    struct npc_blastmaster_emi_shortfuseAI : public EscortAI
    {
        /**
         * @brief 构造函数，初始化NPC AI
         *
         * @param creature 生物对象指针
         *
         * @details 初始化护送AI，获取副本实例脚本，恢复阵营，初始化计时器。
         */
        npc_blastmaster_emi_shortfuseAI(Creature* creature) : EscortAI(creature)
        {
            instance = creature->GetInstanceScript();
            creature->RestoreFaction();  // 恢复默认阵营
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 重置计时器和阶段控制变量。
         */
        void Initialize()
        {
            uiTimer = 0;    ///< 事件计时器
            uiPhase = 0;    ///< 事件流程阶段
        }

        InstanceScript* instance;       ///< 副本实例脚本指针

        uint8 uiPhase;                  ///< 当前事件流程阶段
        uint32 uiTimer;                 ///< 事件计时器

        GuidList SummonList;            ///< 召唤的生物GUID列表
        GuidList GoSummonList;          ///< 召唤的GameObject GUID列表

        /**
         * @brief 重置NPC状态
         *
         * @details 在NPC重置时调用。
         *          如果不在护送状态，重置所有状态并清理召唤物。
         */
        void Reset() override
        {
            if (!HasEscortState(STATE_ESCORT_ESCORTING))
            {
                Initialize();

                RestoreAll();  // 恢复所有状态

                // 清空召唤列表
                SummonList.clear();
                GoSummonList.clear();
            }
        }

        /**
         * @brief 玩家选择对话选项回调
         *
         * @param player 玩家对象指针
         * @param menuId 菜单ID（未使用）
         * @param gossipListId 对话选项ID
         *
         * @return bool 返回false表示继续处理
         *
         * @details 当玩家选择对话选项时触发。
         *          如果选择了第一个选项，开始护送任务。
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (gossipListId == 0)
            {
                // 开始护送任务
                Start(true, false, player->GetGUID());

                // 设置NPC阵营为玩家阵营
                me->SetFaction(player->GetFaction());
                SetData(1, 0);

                // 关闭对话窗口
                player->PlayerTalkClass->SendCloseGossip();
            }
            return false;
        }

        /**
         * @brief 设置下一步事件
         *
         * @param uiTimerStep 下一步计时器时间
         * @param bNextStep 是否进入下一阶段
         * @param uiPhaseStep 指定的阶段（如果bNextStep为false）
         *
         * @details 用于控制事件流程的阶段转换。
         */
        void NextStep(uint32 uiTimerStep, bool bNextStep = true, uint8 uiPhaseStep = 0)
        {
            uiTimer = uiTimerStep;
            if (bNextStep)
                ++uiPhase;
            else
                uiPhase = uiPhaseStep;
        }

        /**
         * @brief 炸开隧道
         *
         * @param isRight 是否为右侧隧道
         *
         * @details 在放置炸药后，播放爆炸效果并移除炸药GameObject。
         *          同时关闭对应的隧道门。
         *
         * @note 视觉效果未完全实现。
         */
        void CaveDestruction(bool isRight)
        {
            if (GoSummonList.empty())
                return;

            // 遍历所有召唤的炸药GameObject
            for (GuidList::const_iterator itr = GoSummonList.begin(); itr != GoSummonList.end(); ++itr)
            {
                if (GameObject* go = ObjectAccessor::GetGameObject(*me, *itr))
                {
                    // 召唤触发生物用于施放视觉效果
                    if (Creature* trigger = go->SummonTrigger(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), 0, 1ms))
                    {
                        // 施放爆炸视觉效果（未完全实现）
                        trigger->CastSpell(trigger, 11542, true);
                        trigger->CastSpell(trigger, 35470, true);
                    }
                    go->RemoveFromWorld();  // 移除炸药GameObject
                }
            }

            // 关闭隧道门
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(isRight ? DATA_GO_CAVE_IN_RIGHT : DATA_GO_CAVE_IN_LEFT)))
                instance->HandleGameObject(ObjectGuid::Empty, false, go);
        }

        /**
         * @brief 面向隧道门
         *
         * @param isRight 是否为右侧隧道
         *
         * @details 让NPC面向指定的隧道门GameObject。
         */
        void SetInFace(bool isRight)
        {
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(isRight ? DATA_GO_CAVE_IN_RIGHT : DATA_GO_CAVE_IN_LEFT)))
                me->SetFacingToObject(go);
        }

        /**
         * @brief 恢复所有状态
         *
         * @details 在护送任务重置时调用。
         *          关闭隧道门，移除所有召唤的GameObject和生物。
         */
        void RestoreAll()
        {
            // 关闭右侧隧道门
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_CAVE_IN_RIGHT)))
                instance->HandleGameObject(ObjectGuid::Empty, false, go);

            // 关闭左侧隧道门
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_CAVE_IN_LEFT)))
                instance->HandleGameObject(ObjectGuid::Empty, false, go);

            // 移除所有召唤的GameObject
            if (!GoSummonList.empty())
                for (GuidList::const_iterator itr = GoSummonList.begin(); itr != GoSummonList.end(); ++itr)
                {
                    if (GameObject* go = ObjectAccessor::GetGameObject(*me, *itr))
                        go->RemoveFromWorld();
                }

            // 移除所有召唤的生物
            if (!SummonList.empty())
                for (GuidList::const_iterator itr = SummonList.begin(); itr != SummonList.end(); ++itr)
                {
                    if (Creature* summon = ObjectAccessor::GetCreature(*me, *itr))
                    {
                        summon->DespawnOrUnsummon();
                    }
                }
        }

        /**
         * @brief 让召唤的生物对所有玩家产生仇恨
         *
         * @param temp 召唤的生物
         *
         * @details 遍历副本中的所有玩家，并让召唤的生物对他们产生仇恨。
         *          确保召唤的敌人会主动攻击玩家。
         */
        void AggroAllPlayers(Creature* temp)
        {
            Map::PlayerList const& PlList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator i = PlList.begin(); i != PlList.end(); ++i)
            {
                if (Player* player = i->GetSource())
                {
                    // 跳过管理员玩家
                    if (player->IsGameMaster())
                        continue;

                    // 对存活的玩家产生仇恨
                    if (player->IsAlive())
                        AddThreat(player, 0.0f, temp);
                }
            }
        }

        /**
         * @brief 到达路径点回调
         *
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * @details 当NPC到达特定路径点时触发对应的事件。
         *          包括暂停移动、触发对话、召唤敌人等。
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            // 确保NPC阵营与护送玩家一致
            if (GetPlayerForEscort())
                if (me->GetFaction() != GetPlayerForEscort()->GetFaction())
                    me->SetFaction(GetPlayerForEscort()->GetFaction());

            switch (waypointId)
            {
                case 3:
                    SetEscortPaused(true);  // 暂停护送
                    NextStep(2000, false, 3);
                    break;
                case 7:
                    SetEscortPaused(true);
                    NextStep(2000, false, 4);
                    break;
                case 9:
                    NextStep(1000, false, 8);
                    break;
                case 10:
                    NextStep(25000, false, 10);
                    break;
                case 11:
                    SetEscortPaused(true);
                    SetInFace(true);  // 面向右侧隧道
                    NextStep(1000, false, 11);
                    break;
                case 12:
                    NextStep(25000, false, 18);
                    break;
                case 13:
                    Summon(6);  // 召唤敌人
                    NextStep(25000, false, 19);
                    break;
                case 14:
                    SetInFace(false);  // 面向左侧隧道
                    Talk(SAY_BLASTMASTER_17);
                    SetEscortPaused(true);
                    NextStep(5000, false, 20);
                    break;
            }
        }

        /**
         * @brief 设置数据回调
         *
         * @param uiI 数据类型标识
         * @param uiValue 数据值
         *
         * @details 用于外部脚本设置AI状态。
         *          类型1：开始护送任务
         *          类型2：Boss事件状态（1=进行中，2=完成）
         */
        void SetData(uint32 uiI, uint32 uiValue) override
        {
            switch (uiI)
            {
                case 1:
                    // 开始护送任务
                    SetEscortPaused(true);
                    Talk(SAY_BLASTMASTER_0);  // 播放初始对话
                    NextStep(2000, true);
                    break;
                case 2:
                    // Boss事件状态控制
                    switch (uiValue)
                    {
                        case 1:
                            // Boss事件开始
                            instance->SetBossState(DATA_BLASTMASTER_EVENT, IN_PROGRESS);
                            break;
                        case 2:
                            // Boss事件完成
                            instance->SetBossState(DATA_BLASTMASTER_EVENT, DONE);
                            NextStep(5000, false, 22);  // 进入下一阶段
                            break;
                    }
                    break;
            }
        }

        /**
         * @brief 召唤生物或GameObject
         *
         * @param uiCase 召唤类型
         *
         * @details 根据不同的召唤类型执行不同的召唤逻辑：
         *          - 类型1-5：召唤洞穴伏击者
         *          - 类型6-7：放置炸药GameObject
         *          - 类型8：召唤Boss Grubbis和Chomper
         *          - 类型9：放置红色火箭信号
         */
        void Summon(uint8 uiCase)
        {
            switch (uiCase)
            {
                case 1:
                    // 召唤10个洞穴伏击者（第一波）
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[4], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[5], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[6], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[7], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[8], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[9], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    break;
                case 2:
                    // 放置第一个炸药并召唤更多敌人
                    if (GameObject* go = me->SummonGameObject(183410, -533.140f, -105.322f, -156.016f, 0.f, QuaternionData(), 1s))
                    {
                        GoSummonList.push_back(go->GetGUID());
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);  // 设置为不可交互
                    }
                    Summon(3);  // 召唤更多敌人
                    break;
                case 3:
                    // 召唤4个洞穴伏击者（第二波）
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    Talk(SAY_BLASTMASTER_7);
                    break;
                case 4:
                    // 放置第二个炸药
                    if (GameObject* go = me->SummonGameObject(183410, -542.199f, -96.854f, -155.790f, 0.f, QuaternionData(), 1s))
                    {
                        GoSummonList.push_back(go->GetGUID());
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    }
                    break;
                case 5:
                    // 召唤5个洞穴伏击者（第三波）
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[10], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[11], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[12], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[13], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    me->SummonCreature(NPC_CAVERNDEEP_AMBUSHER, SpawnPosition[14], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    break;
                case 6:
                    // 放置第三个炸药并召唤敌人
                    if (GameObject* go = me->SummonGameObject(183410, -507.820f, -103.333f, -151.353f, 0.f, QuaternionData(), 1s))
                    {
                        GoSummonList.push_back(go->GetGUID());
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                        Summon(5);
                    }
                    break;
                case 7:
                    // 放置第四个炸药
                    if (GameObject* go = me->SummonGameObject(183410, -511.829f, -86.249f, -151.431f, 0.f, QuaternionData(), 1s))
                    {
                        GoSummonList.push_back(go->GetGUID());
                        go->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    }
                    break;
                case 8:
                    // 召唤Boss Grubbis和Chomper
                    if (Creature* grubbis = me->SummonCreature(NPC_GRUBBIS, SpawnPosition[15], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min))
                        grubbis->AI()->Talk(SAY_GRUBBIS);  // Grubbis对话
                    me->SummonCreature(NPC_CHOMPER, SpawnPosition[16], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30min);
                    break;
                case 9:
                    // 放置3个红色火箭信号（任务完成标志）
                    me->SummonGameObject(GO_RED_ROCKET, SpawnPosition[17], QuaternionData(), 2h);
                    me->SummonGameObject(GO_RED_ROCKET, SpawnPosition[18], QuaternionData(), 2h);
                    me->SummonGameObject(GO_RED_ROCKET, SpawnPosition[19], QuaternionData(), 2h);
                    break;
            }
        }

        /**
         * @brief 更新护送AI逻辑
         *
         * @param uiDiff 自上次更新以来的时间差（毫秒）
         *
         * @details 每帧调用，负责：
         *          1. 处理事件流程的各个阶段
         *          2. 执行对话、召唤、爆炸等事件
         *          3. 执行近战攻击
         *
         * @note 性能注意事项：该函数每帧调用，应保持高效。
         */
        void UpdateEscortAI(uint32 uiDiff) override
        {
            // 如果有活动的事件流程
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            Talk(SAY_BLASTMASTER_1);
                            NextStep(2000, true);
                            break;
                        case 2:
                            SetEscortPaused(false);  // 恢复护送移动
                            NextStep(0, false, 0);
                            break;
                        case 3:
                            Talk(SAY_BLASTMASTER_2);
                            SetEscortPaused(false);
                            NextStep(0, false, 0);
                            break;
                        case 4:
                            Talk(SAY_BLASTMASTER_3);
                            NextStep(3000, true);
                            break;
                        case 5:
                            Talk(SAY_BLASTMASTER_4);
                            NextStep(3000, true);
                            break;
                        case 6:
                            SetInFace(true);  // 面向右侧隧道
                            Talk(SAY_BLASTMASTER_5);
                            Summon(1);  // 召唤第一波敌人
                            // 打开右侧隧道门
                            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_CAVE_IN_RIGHT)))
                                instance->HandleGameObject(ObjectGuid::Empty, true, go);
                            NextStep(3000, true);
                            break;
                        case 7:
                            Talk(SAY_BLASTMASTER_6);
                            SetEscortPaused(false);
                            NextStep(0, false, 0);
                            break;
                        case 8:
                            me->HandleEmoteCommand(EMOTE_STATE_USE_STANDING);  // 播放使用动作
                            NextStep(25000, true);
                            break;
                        case 9:
                            Summon(2);  // 放置第一个炸药并召唤敌人
                            NextStep(0, false);
                            break;
                        case 10:
                            Summon(4);  // 放置第二个炸药
                            Talk(SAY_BLASTMASTER_8);
                            NextStep(0, false);
                            break;
                        case 11:
                            Talk(SAY_BLASTMASTER_9);
                            NextStep(5000, true);
                            break;
                        case 12:
                            Talk(SAY_BLASTMASTER_10);
                            NextStep(5000, true);
                            break;
                        case 13:
                            Talk(SAY_BLASTMASTER_11);
                            CaveDestruction(true);  // 炸毁右侧隧道
                            NextStep(8000, true);
                            break;
                        case 14:
                            Talk(SAY_BLASTMASTER_12);
                            NextStep(8500, true);
                            break;
                        case 15:
                            Talk(SAY_BLASTMASTER_13);
                            NextStep(2000, true);
                            break;
                        case 16:
                            Talk(SAY_BLASTMASTER_14);
                            SetInFace(false);  // 面向左侧隧道
                            // 打开左侧隧道门
                            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_CAVE_IN_LEFT)))
                                instance->HandleGameObject(ObjectGuid::Empty, true, go);
                            NextStep(2000, true);
                            break;
                        case 17:
                            SetEscortPaused(false);
                            Talk(SAY_BLASTMASTER_15);
                            Summon(5);  // 召唤第三波敌人
                            NextStep(0, false);
                            break;
                        case 18:
                            Summon(6);  // 放置第三个炸药并召唤敌人
                            NextStep(0, false);
                            break;
                        case 19:
                            SetInFace(false);
                            Summon(7);  // 放置第四个炸药
                            Talk(SAY_BLASTMASTER_16);
                            NextStep(0, false);
                            break;
                        case 20:
                            Talk(SAY_BLASTMASTER_18);
                            NextStep(2000, true);
                            break;
                        case 21:
                            Summon(8);  // 召唤Boss Grubbis和Chomper
                            NextStep(0, false);
                            break;
                        case 22:
                            CaveDestruction(false);  // 炸毁左侧隧道
                            Talk(SAY_BLASTMASTER_11);
                            NextStep(3000, true);
                            break;
                        case 23:
                            Summon(9);  // 放置红色火箭信号
                            Talk(SAY_BLASTMASTER_19);
                            NextStep(0, false);
                            break;
                    }
                } else uiTimer -= uiDiff;
            }

            // 如果没有仇恨目标，则返回
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 召唤生物回调
         *
         * @param summon 召唤的生物
         *
         * @details 当召唤一个生物时触发。
         *          将生物GUID添加到召唤列表，并让其攻击所有玩家。
         */
        void JustSummoned(Creature* summon) override
        {
            SummonList.push_back(summon->GetGUID());
            AggroAllPlayers(summon);  // 让召唤的生物攻击所有玩家
        }
    };

};

/**
 * @class boss_grubbis
 * @brief Grubbis Boss脚本类
 *
 * @details 该类实现了诺莫瑞根副本中的Boss Grubbis的AI。
 *          Grubbis是由爆破专家艾米·短路护送任务召唤的Boss。
 *          该Boss会在召唤时通知召唤者，并在死亡时报告事件完成。
 */
class boss_grubbis : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册Boss脚本名称
     */
    boss_grubbis() : CreatureScript("boss_grubbis") { }

    /**
     * @brief 获取Boss的AI实例
     *
     * @param creature 生物对象指针
     * @return CreatureAI* 返回Boss的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetGnomereganAI<boss_grubbisAI>(creature);
    }

    /**
     * @struct boss_grubbisAI
     * @brief Grubbis Boss的AI实现
     *
     * @details 该Boss AI相对简单，主要负责与召唤者的通信。
     *          在被召唤时通知召唤者Boss事件开始，
     *          在死亡时通知召唤者Boss事件完成。
     */
    struct boss_grubbisAI : public ScriptedAI
    {
        /**
         * @brief 构造函数，初始化Boss AI
         *
         * @param creature 生物对象指针
         *
         * @details 初始化ScriptedAI基类，并设置召唤者数据。
         */
        boss_grubbisAI(Creature* creature) : ScriptedAI(creature)
        {
            SetDataSummoner();
        }

        /**
         * @brief 设置召唤者数据
         *
         * @details 如果Boss是被召唤的，通知召唤者Boss事件开始。
         *          设置召唤者AI的数据为(2, 1)，表示Boss事件开始。
         */
        void SetDataSummoner()
        {
            if (!me->IsSummon())
                return;

            // 获取召唤者并通知Boss事件开始
            if (Unit* summon = me->ToTempSummon()->GetSummonerUnit())
                if (Creature* creature = summon->ToCreature())
                    creature->AI()->SetData(2, 1);
        }

        /**
         * @brief 更新AI逻辑
         *
         * @param diff 自上次更新以来的时间差（未使用）
         *
         * @details 每帧调用，执行基本的近战攻击。
         */
        void UpdateAI(uint32 /*diff*/) override
        {
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 死亡回调
         *
         * @param killer 击杀者（未使用）
         *
         * @details 当Boss死亡时，通知召唤者Boss事件完成。
         *          设置召唤者AI的数据为(2, 2)，表示Boss事件完成。
         */
        void JustDied(Unit* /*killer*/) override
        {
            if (!me->IsSummon())
                return;

            // 获取召唤者并通知Boss事件完成
            if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                if (Creature* creature = summoner->ToCreature())
                    creature->AI()->SetData(2, 2);
        }
    };

};

/**
 * @class spell_collecting_fallout
 * @brief 收集辐射尘法术脚本
 *
 * @details 实现了"收集辐射尘"任务的法术逻辑。
 *          该法术有25%的成功率收集辐射尘样本。
 *
 *          法术效果：
 *          - 效果0：触发成功时的法术
 *          - 效果1：触发失败时的法术
 *
 * @note 该法术用于诺莫瑞根副本相关的任务。
 */
// 12709 - Collecting Fallout
class spell_collecting_fallout : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数，注册法术脚本名称
         */
        spell_collecting_fallout() : SpellScriptLoader("spell_collecting_fallout") { }

        /**
         * @class spell_collecting_fallout_SpellScript
         * @brief 收集辐射尘法术脚本实现
         *
         * @details 实现了法术的成功/失败逻辑。
         *          法术有25%的成功率，成功时阻止效果1，
         *          失败时阻止效果0。
         */
        class spell_collecting_fallout_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_collecting_fallout_SpellScript);

            /**
             * @brief 法术发射时回调（效果0）
             *
             * @param effIndex 法术效果索引
             *
             * @details 在法术发射时触发，进行成功/失败判定。
             *          有25%的概率成功，成功时不阻止效果0。
             *          失败时阻止效果0的默认效果。
             */
            void OnLaunch(SpellEffIndex effIndex)
            {
                // 估计有25%的成功率
                if (roll_chance_i(25))
                    _spellFail = false;  // 成功
                else
                    PreventHitDefaultEffect(effIndex);  // 失败，阻止效果0
            }

            /**
             * @brief 处理失败效果（效果1）
             *
             * @param effIndex 法术效果索引
             *
             * @details 如果法术成功，则阻止效果1（失败效果）。
             *          如果法术失败，则不阻止效果1，让失败效果触发。
             */
            void HandleFail(SpellEffIndex effIndex)
            {
                if (!_spellFail)
                    PreventHitDefaultEffect(effIndex);  // 成功时阻止失败效果
            }

            /**
             * @brief 注册法术效果回调
             *
             * @details 注册两个效果的处理函数：
             *          - 效果0：成功效果
             *          - 效果1：失败效果
             */
            void Register() override
            {
                OnEffectLaunch += SpellEffectFn(spell_collecting_fallout_SpellScript::OnLaunch, EFFECT_0, SPELL_EFFECT_TRIGGER_SPELL);
                OnEffectLaunch += SpellEffectFn(spell_collecting_fallout_SpellScript::HandleFail, EFFECT_1, SPELL_EFFECT_TRIGGER_SPELL);
            }

            bool _spellFail = true;  ///< 法术失败标志，初始为true
        };

        /**
         * @brief 获取法术脚本实例
         *
         * @return SpellScript* 返回新创建的法术脚本实例
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_collecting_fallout_SpellScript();
        }
};

/**
 * @brief 注册诺莫瑞根副本脚本
 *
 * @details 该函数在服务器启动时被调用，用于注册各种脚本实例。
 *          包括NPC脚本、Boss脚本和法术脚本。
 *          这是TrinityCore脚本系统的标准入口点。
 */
void AddSC_gnomeregan()
{
    new npc_blastmaster_emi_shortfuse();  // 注册爆破专家艾米·短路NPC脚本
    new boss_grubbis();                   // 注册Grubbis Boss脚本

    new spell_collecting_fallout();       // 注册收集辐射尘法术脚本
}
