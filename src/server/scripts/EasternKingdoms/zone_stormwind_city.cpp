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
 * @file zone_stormwind_city.cpp
 * @brief 暴风城(Stormwind City)区域脚本模块
 *
 * 本模块实现了暴风城区域内的NPC交互和任务逻辑，包括：
 * - 任务434: 攻击事件(The Attack) - 揭露贵族叛徒的阴谋
 * - 复杂的NPC对话和事件序列
 * - 多个NPC的协同AI行为
 * - 伪装和背叛剧情
 *
 * 暴风城是人类王国的首都，位于东部王国大陆的西北部。
 *
 * ScriptData
 * SDName: Stormwind_City
 * SD%Complete: 100
 * SDComment: Quest support: 1640, 1447, 434.
 * SDCategory: Stormwind City
 * EndScriptData
 *
 * ContentData
 * npc_tyrion - 任务发布者，间谍大师
 * npc_tyrion_spybot - 泰利恩的间谍机器人
 * npc_marzon_silent_blade - 马尔佐·静刃，刺客
 * npc_lord_gregor_lescovar - 格雷戈·莱斯科瓦勋爵，叛徒贵族
 * EndContentData
 */

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"

/*######
## npc_lord_gregor_lescovar
## 格雷戈·莱斯科瓦勋爵
######*/

/**
 * @brief 格雷戈·莱斯科瓦勋爵相关枚举定义
 *
 * 定义了任务"攻击事件"中涉及的对话ID、NPC ID和任务ID
 */
enum LordGregorLescovar
{
    // 对话ID
    SAY_GUARD_2         = 0,  ///< 守卫对话2
    SAY_LESCOVAR_2      = 0,  ///< 莱斯科瓦对话2
    SAY_LESCOVAR_3      = 1,  ///< 莱斯科瓦对话3
    SAY_LESCOVAR_4      = 2,  ///< 莱斯科瓦对话4 - 揭露叛徒身份
    SAY_MARZON_1        = 0,  ///< 马尔佐对话1
    SAY_MARZON_2        = 1,  ///< 马尔佐对话2 - 战斗开始
    SAY_TYRION_2        = 1,  ///< 泰利恩对话2

    // NPC ID
    NPC_STORMWIND_ROYAL = 1756,  ///< 暴风城皇家守卫
    NPC_MARZON_BLADE    = 1755,  ///< 马尔佐·静刃（刺客）
    NPC_TYRION          = 7766,  ///< 泰利恩（间谍大师）

    // 任务ID
    QUEST_THE_ATTACK    = 434   ///< 任务：攻击事件
};

/**
 * @brief 格雷戈·莱斯科瓦勋爵脚本类
 *
 * 实现叛徒贵族格雷戈·莱斯科瓦勋爵的AI行为。
 * 该NPC是任务"攻击事件"(Quest 434)的关键角色，表面上是一位贵族，
 * 实际上是叛徒，与刺客马尔佐密谋。
 *
 * 行为流程：
 * 1. 接受玩家的护送任务
 * 2. 行走到指定地点
 * 3. 发出信号让守卫离开
 * 4. 召唤刺客马尔佐
 * 5. 揭露叛徒身份并进入战斗
 */
class npc_lord_gregor_lescovar : public CreatureScript
{
public:
    npc_lord_gregor_lescovar() : CreatureScript("npc_lord_gregor_lescovar") { }

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lord_gregor_lescovarAI(creature);
    }

    /**
     * @brief 格雷戈·莱斯科瓦勋爵AI结构体
     *
     * 继承自EscortAI，实现护送任务和事件序列
     */
    struct npc_lord_gregor_lescovarAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_lord_gregor_lescovarAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 重置所有状态变量到初始值
         */
        void Initialize()
        {
            uiTimer = 0;      // 事件计时器
            uiPhase = 0;      // 事件阶段

            MarzonGUID.Clear();  // 马尔佐的GUID清空
        }

        uint32 uiTimer;       ///< 事件计时器（毫秒）
        uint32 uiPhase;       ///< 当前事件阶段

        ObjectGuid MarzonGUID;  ///< 马尔佐·静刃的GUID，用于跨函数引用

        /**
         * @brief 重置AI状态
         *
         * 当NPC重置时调用（如脱离战斗、重生等）
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入脱战模式
         * @param why 脱战原因
         *
         * 当NPC脱离战斗或事件失败时调用，清理所有相关NPC
         */
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            // 让自己消失并死亡
            me->DisappearAndDie();

            // 让马尔佐也消失
            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
            {
                if (pMarzon->IsAlive())
                    pMarzon->DisappearAndDie();
            }
        }

        /**
         * @brief 进入战斗
         * @param who 攻击目标
         *
         * 当NPC进入战斗时，通知马尔佐一起参战
         */
        void JustEngagedWith(Unit* who) override
        {
            // 如果马尔佐存活且未参战，让他也攻击目标
            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
            {
                if (pMarzon->IsAlive() && !pMarzon->IsInCombat())
                    pMarzon->AI()->AttackStart(who);
            }
        }

        /**
         * @brief 到达路径点时的回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * 当NPC到达特定路径点时触发事件序列
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 14:
                    // 到达路径点14：开始对话序列
                    SetEscortPaused(true);  // 暂停护送
                    Talk(SAY_LESCOVAR_2);   // 说出台词
                    uiTimer = 3000;         // 设置3秒延时
                    uiPhase = 1;            // 进入阶段1
                    break;
                case 16:
                    // 到达路径点16：召唤马尔佐
                    SetEscortPaused(true);
                    // 召唤马尔佐·静刃
                    if (Creature* pMarzon = me->SummonCreature(NPC_MARZON_BLADE, -8411.360352f, 480.069733f, 123.760895f, 4.941504f, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1s))
                    {
                        // 让马尔佐移动到指定位置
                        pMarzon->GetMotionMaster()->MovePoint(0, -8408.000977f, 468.611450f, 123.759903f);
                        MarzonGUID = pMarzon->GetGUID();  // 保存GUID
                    }
                    uiTimer = 2000;  // 设置2秒延时
                    uiPhase = 4;     // 进入阶段4
                    break;
            }
        }

        /**
         * @brief 让附近的守卫消失
         *
         * 清除周围8码内的暴风城皇家守卫
         * 用于叛徒与刺客会面时清除目击者
         *
         * @note TODO: 由于缺乏移动地图(movemaps)，无法让守卫正常走开，
         *       只能让他们直接消失
         */
        //TO-DO: We don't have movemaps, also we can't make 2 npcs walks to one point propperly (and we can not use escort ai, because they are 2 different spawns and with same entry), because of it we make them, disappear.
        void DoGuardsDisappearAndDie()
        {
            std::list<Creature*> GuardList;
            // 获取周围8码内的所有皇家守卫
            me->GetCreatureListWithEntryInGrid(GuardList, NPC_STORMWIND_ROYAL, 8.0f);
            if (!GuardList.empty())
            {
                for (std::list<Creature*>::const_iterator itr = GuardList.begin(); itr != GuardList.end(); ++itr)
                {
                    if (Creature* pGuard = *itr)
                        pGuard->DisappearAndDie();  // 让守卫消失
                }
            }
        }

        /**
         * @brief 更新AI状态
         * @param uiDiff 距上次更新的时间差（毫秒）
         *
         * 主更新循环，处理事件序列和战斗逻辑
         *
         * 事件阶段说明：
         * - 阶段1-3：清除守卫序列
         * - 阶段4：问候马尔佐
         * - 阶段5-7：揭露叛徒身份并进入战斗
         */
        void UpdateAI(uint32 uiDiff) override
        {
            // 处理事件序列
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            // 阶段1：守卫回应
                            if (Creature* pGuard = me->FindNearestCreature(NPC_STORMWIND_ROYAL, 8.0f, true))
                                pGuard->AI()->Talk(SAY_GUARD_2);
                            uiTimer = 3000;
                            uiPhase = 2;
                            break;
                        case 2:
                            // 阶段2：守卫离开（消失）
                            DoGuardsDisappearAndDie();
                            uiTimer = 2000;
                            uiPhase = 3;
                            break;
                        case 3:
                            // 阶段3：继续移动
                            SetEscortPaused(false);  // 恢复护送
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 4:
                            // 阶段4：问候马尔佐
                            Talk(SAY_LESCOVAR_3);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 5:
                            // 阶段5：马尔佐说话
                            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
                                pMarzon->AI()->Talk(SAY_MARZON_1);
                            uiTimer = 3000;
                            uiPhase = 6;
                            break;
                        case 6:
                            // 阶段6：揭露叛徒身份，完成任务目标
                            Talk(SAY_LESCOVAR_4);
                            if (Player* player = GetPlayerForEscort())
                                player->AreaExploredOrEventHappens(QUEST_THE_ATTACK);  // 标记任务事件完成
                            uiTimer = 2000;
                            uiPhase = 7;
                            break;
                        case 7:
                            // 阶段7：泰利恩揭露身份，叛徒进入战斗
                            if (Creature* pTyrion = me->FindNearestCreature(NPC_TYRION, 20.0f, true))
                                pTyrion->AI()->Talk(SAY_TYRION_2);
                            // 将马尔佐和自己设为敌对阵营
                            if (Creature* pMarzon = ObjectAccessor::GetCreature(*me, MarzonGUID))
                                pMarzon->SetFaction(FACTION_MONSTER);
                            me->SetFaction(FACTION_MONSTER);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                    }
                } else uiTimer -= uiDiff;
            }
            // 调用父类EscortAI的更新
            EscortAI::UpdateAI(uiDiff);

            // 战斗逻辑
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_marzon_silent_blade
## 马尔佐·静刃
######*/

/**
 * @brief 马尔佐·静刃脚本类
 *
 * 实现刺客马尔佐·静刃的AI行为。
 * 马尔佐是格雷戈·莱斯科瓦勋爵的同谋，在任务"攻击事件"中扮演反派角色。
 *
 * 行为特点：
 * - 被召唤后走向指定位置
 * - 与莱斯科瓦勋爵协同作战
 * - 进入战斗时发表威胁性言论
 */
class npc_marzon_silent_blade : public CreatureScript
{
public:
    npc_marzon_silent_blade() : CreatureScript("npc_marzon_silent_blade") { }

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_marzon_silent_bladeAI(creature);
    }

    /**
     * @brief 马尔佐·静刃AI结构体
     *
     * 实现刺客的基本行为和与召唤者的协同机制
     */
    struct npc_marzon_silent_bladeAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_marzon_silent_bladeAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetWalk(true);  // 设置为行走模式
        }

        /**
         * @brief 重置AI状态
         *
         * 恢复默认阵营，避免在非任务状态下攻击玩家
         */
        void Reset() override
        {
            me->RestoreFaction();
        }

        /**
         * @brief 进入战斗
         * @param who 攻击目标
         *
         * 进入战斗时发表威胁性言论，并召唤召唤者一起参战
         */
        void JustEngagedWith(Unit* who) override
        {
            Talk(SAY_MARZON_2);  // 发表战斗台词

            // 如果是召唤生物，让召唤者也参战
            if (me->IsSummon())
            {
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                {
                    if (summoner->GetTypeId() == TYPEID_UNIT && summoner->IsAlive() && !summoner->IsInCombat())
                        summoner->ToCreature()->AI()->AttackStart(who);
                }
            }
        }

        /**
         * @brief 进入脱战模式
         * @param why 脱战原因
         *
         * 事件失败时清理自己和召唤者
         */
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            me->DisappearAndDie();  // 自己消失

            // 让召唤者也消失
            if (me->IsSummon())
            {
                if (Unit* summoner = me->ToTempSummon()->GetSummonerUnit())
                {
                    if (summoner->GetTypeId() == TYPEID_UNIT && summoner->IsAlive())
                        summoner->ToCreature()->DisappearAndDie();
                }
            }
        }

        /**
         * @brief 移动完成通知
         * @param uiType 移动类型
         * @param uiId 移动点ID（未使用）
         *
         * 当到达目标位置时，通知召唤者继续事件序列
         */
        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            if (uiType != POINT_MOTION_TYPE)
                return;

            // 如果是召唤生物
            if (me->IsSummon())
            {
                Unit* summoner = me->ToTempSummon()->GetSummonerUnit();
                if (summoner && summoner->GetTypeId() == TYPEID_UNIT && summoner->IsAIEnabled())
                {
                    // 获取召唤者的AI（莱斯科瓦勋爵）
                    npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI* ai =
                        CAST_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, summoner->GetAI());
                    if (ai)
                    {
                        // 触发下一阶段事件
                        ai->uiTimer = 2000;
                        ai->uiPhase = 5;
                    }
                    //me->ChangeOrient(0.0f, summoner);
                }
            }
        }

        /**
         * @brief 更新AI状态
         * @param diff 距上次更新的时间差（未使用）
         *
         * 简单的近战攻击AI
         */
        void UpdateAI(uint32 /*diff*/) override
        {
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_tyrion_spybot
## 泰利恩的间谍机器人
######*/

/**
 * @brief 泰利恩间谍机器人相关枚举定义
 *
 * 定义了间谍机器人在任务序列中的对话ID和NPC ID
 */
enum TyrionSpybot
{
    // 对话ID
    SAY_QUEST_ACCEPT_ATTACK  = 0,  ///< 接受任务时的对话
    SAY_SPYBOT_1             = 1,  ///< 间谍机器人对话1
    SAY_SPYBOT_2             = 2,  ///< 间谍机器人对话2
    SAY_SPYBOT_3             = 3,  ///< 间谍机器人对话3
    SAY_SPYBOT_4             = 4,  ///< 间谍机器人对话4
    SAY_TYRION_1             = 0,  ///< 泰利恩对话1
    SAY_GUARD_1              = 1,  ///< 守卫对话1
    SAY_LESCOVAR_1           = 3,  ///< 莱斯科瓦对话1

    // NPC ID
    NPC_PRIESTESS_TYRIONA    = 7779,  ///< 泰利恩娜女祭司（伪装形态）
    NPC_LORD_GREGOR_LESCOVAR = 1754,  ///< 格雷戈·莱斯科瓦勋爵
};

/**
 * @brief 泰利恩间谍机器人脚本类
 *
 * 实现泰利恩的间谍机器人的AI行为。
 * 间谍机器人是任务"攻击事件"(Quest 434)的核心角色，负责伪装成
 * 泰利恩娜女祭司，引诱叛徒莱斯科瓦勋爵暴露其叛国阴谋。
 *
 * 行为流程：
 * 1. 接受任务后开始护送
 * 2. 伪装成泰利恩娜女祭司
 * 3. 与守卫和莱斯科瓦勋爵对话
 * 4. 触发莱斯科瓦勋爵的事件序列
 * 5. 完成任务目标后消失
 *
 * 这是一个复杂的事件序列，涉及多个NPC的交互。
 */
class npc_tyrion_spybot : public CreatureScript
{
public:
    npc_tyrion_spybot() : CreatureScript("npc_tyrion_spybot") { }

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_tyrion_spybotAI(creature);
    }

    /**
     * @brief 泰利恩间谍机器人AI结构体
     *
     * 继承自EscortAI，实现复杂的伪装和诱导事件序列
     */
    struct npc_tyrion_spybotAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_tyrion_spybotAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            uiTimer = 0;  // 事件计时器
            uiPhase = 0;  // 事件阶段
        }

        uint32 uiTimer;  ///< 事件计时器（毫秒）
        uint32 uiPhase;  ///< 当前事件阶段

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 到达路径点时的回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * 在关键路径点触发事件序列
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 1:
                    // 路径点1：开始伪装序列
                    SetEscortPaused(true);
                    uiTimer = 2000;
                    uiPhase = 1;
                    break;
                case 5:
                    // 路径点5：与守卫交互
                    SetEscortPaused(true);
                    Talk(SAY_SPYBOT_1);
                    uiTimer = 2000;
                    uiPhase = 5;
                    break;
                case 17:
                    // 路径点17：与莱斯科瓦勋爵会面
                    SetEscortPaused(true);
                    Talk(SAY_SPYBOT_3);
                    uiTimer = 3000;
                    uiPhase = 8;
                    break;
            }
        }

        /**
         * @brief 更新AI状态
         * @param uiDiff 距上次更新的时间差（毫秒）
         *
         * 处理事件序列的主更新循环
         *
         * 事件阶段说明：
         * - 阶段1-4：伪装成泰利恩娜女祭司
         * - 阶段5-7：通过守卫检查
         * - 阶段8-10：引诱莱斯科瓦勋爵并触发后续事件
         */
        void UpdateAI(uint32 uiDiff) override
        {
            // 处理事件序列
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            // 阶段1：开始伪装对话
                            Talk(SAY_QUEST_ACCEPT_ATTACK);
                            uiTimer = 3000;
                            uiPhase = 2;
                            break;
                        case 2:
                            // 阶段2：泰利恩对玩家说话
                            if (Creature* pTyrion = me->FindNearestCreature(NPC_TYRION, 10.0f))
                            {
                                if (Player* player = GetPlayerForEscort())
                                    pTyrion->AI()->Talk(SAY_TYRION_1, player);
                            }
                            uiTimer = 3000;
                            uiPhase = 3;
                            break;
                        case 3:
                            // 阶段3：变身为泰利恩娜女祭司
                            me->UpdateEntry(NPC_PRIESTESS_TYRIONA);
                            uiTimer = 2000;
                            uiPhase = 4;
                            break;
                        case 4:
                            // 阶段4：继续移动
                           SetEscortPaused(false);
                           uiPhase = 0;
                           uiTimer = 0;
                           break;
                        case 5:
                            // 阶段5：守卫询问身份
                            if (Creature* pGuard = me->FindNearestCreature(NPC_STORMWIND_ROYAL, 10.0f, true))
                                pGuard->AI()->Talk(SAY_GUARD_1);
                            uiTimer = 3000;
                            uiPhase = 6;
                            break;
                        case 6:
                            // 阶段6：伪造的身份证明
                            Talk(SAY_SPYBOT_2);
                            uiTimer = 3000;
                            uiPhase = 7;
                            break;
                        case 7:
                            // 阶段7：通过检查，继续移动
                            SetEscortPaused(false);
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                        case 8:
                            // 阶段8：莱斯科瓦勋爵回应
                            if (Creature* pLescovar = me->FindNearestCreature(NPC_LORD_GREGOR_LESCOVAR, 10.0f))
                                pLescovar->AI()->Talk(SAY_LESCOVAR_1);
                            uiTimer = 3000;
                            uiPhase = 9;
                            break;
                        case 9:
                            // 阶段9：完成引诱，准备离开
                            Talk(SAY_SPYBOT_4);
                            uiTimer = 3000;
                            uiPhase = 10;
                            break;
                        case 10:
                            // 阶段10：触发莱斯科瓦勋爵的事件序列
                            if (Creature* pLescovar = me->FindNearestCreature(NPC_LORD_GREGOR_LESCOVAR, 10.0f))
                            {
                                if (Player* player = GetPlayerForEscort())
                                {
                                    // 启动莱斯科瓦勋爵的护送AI
                                    ENSURE_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, pLescovar->AI())->Start(false, false, player->GetGUID());
                                    ENSURE_AI(npc_lord_gregor_lescovar::npc_lord_gregor_lescovarAI, pLescovar->AI())->SetMaxPlayerDistance(200.0f);
                                }
                            }
                            // 间谍机器人完成任务，消失
                            me->DisappearAndDie();
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                    }
                } else uiTimer -= uiDiff;
            }
            // 调用父类EscortAI的更新
            EscortAI::UpdateAI(uiDiff);

            // 战斗逻辑（通常不会进入战斗）
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_tyrion
## 泰利恩（间谍大师）
######*/

/**
 * @brief 泰利恩相关枚举定义
 */
enum Tyrion
{
    NPC_TYRION_SPYBOT = 8856  ///< 泰利恩间谍机器人的NPC ID
};

/**
 * @brief 泰利恩脚本类
 *
 * 实现间谍大师泰利恩的AI行为。
 * 泰利恩是任务"攻击事件"(Quest 434)的发布者，他策划了一个计划
 * 来揭露暴风城贵族中的叛徒。
 *
 * 主要职责：
 * - 接受玩家领取任务
 * - 启动间谍机器人的事件序列
 */
class npc_tyrion : public CreatureScript
{
public:
    npc_tyrion() : CreatureScript("npc_tyrion") { }

    /**
     * @brief 泰利恩AI结构体
     *
     * 简单的AI，主要处理任务接受事件
     */
    struct npc_tyrionAI : ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_tyrionAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 玩家接受任务时的回调
         * @param player 接受任务的玩家
         * @param quest 接受的任务
         *
         * 当玩家接受任务"攻击事件"时，启动间谍机器人的护送序列
         */
        void OnQuestAccept(Player* player, Quest const* quest) override
        {
            if (quest->GetQuestId() == QUEST_THE_ATTACK)
            {
                // 寻找附近的间谍机器人
                if (Creature* spybot = me->FindNearestCreature(NPC_TYRION_SPYBOT, 5.0f, true))
                {
                    // 启动间谍机器人的护送AI
                    ENSURE_AI(npc_tyrion_spybot::npc_tyrion_spybotAI, spybot->AI())->Start(false, false, player->GetGUID());
                    ENSURE_AI(npc_tyrion_spybot::npc_tyrion_spybotAI, spybot->AI())->SetMaxPlayerDistance(200.0f);
                }
            }
        }
    };

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_tyrionAI(creature);
    }
};

/**
 * @brief 注册暴风城区域脚本
 *
 * 该函数由脚本系统在服务器启动时自动调用，用于注册本文件中定义的所有NPC脚本。
 *
 * @note 函数名遵循TrinityCore命名规范：AddSC_<区域名称>
 * @note 该函数会在WorldSession初始化期间被调用，不应手动调用
 */
void AddSC_stormwind_city()
{
    // 注册泰利恩（任务发布者）
    new npc_tyrion();
    // 注册泰利恩的间谍机器人（伪装者）
    new npc_tyrion_spybot();
    // 注册格雷戈·莱斯科瓦勋爵（叛徒贵族）
    new npc_lord_gregor_lescovar();
    // 注册马尔佐·静刃（刺客）
    new npc_marzon_silent_blade();
}
