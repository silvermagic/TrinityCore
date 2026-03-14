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
 * @file wailing_caverns.cpp
 * @brief 哀嚎洞穴副本核心脚本 - 纳雷克斯唤醒事件
 *
 * 该模块实现哀嚎洞穴的主要剧情事件:纳雷克斯唤醒仪式
 *
 * 剧情背景:
 * 纳雷克斯是一位德鲁伊,在翡翠梦境中陷入噩梦无法醒来
 * 他的弟子(Disciple of Naralex)试图唤醒他,但需要玩家帮助
 *
 * 事件流程:
 * 1. 玩家必须先击杀4个芬里斯领主
 * 2. 与纳雷克斯的弟子对话,开始护送任务
 * 3. 弟子引导玩家前往纳雷克斯沉睡的祭坛
 * 4. 沿途经历3个阶段的怪物波次:
 *    - 第1阶段: 变异掠夺者(Deviate Ravager)
 *    - 第2阶段: 变异毒蛇(Deviate Viper),弟子施放净化法术
 *    - 第3阶段: 噩梦精华和最终Boss穆塔努斯
 * 5. 穆塔努斯被击杀后,纳雷克斯苏醒并离开
 *
 * 关键技术点:
 * - 使用EscortAI实现护送NPC功能
 * - 事件分为多个阶段,通过waypoint触发
 * - 召唤的怪物数量较多,需要注意性能优化
 */

/* ScriptData
SDName: Wailing Caverns
SD%Complete: 95
SDComment: Need to add skill usage for Disciple of Naralex
SDCategory: Wailing Caverns
EndScriptData */

/* ContentData
EndContentData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "wailing_caverns.h"

/*######
## npc_disciple_of_naralex
######*/

/**
 * @enum Enums
 * @brief 定义对话文本、法术ID和NPC ID的枚举常量
 */
enum Enums
{
    // 对话文本ID(对应数据库中的creature_text表)
    SAY_AT_LAST                   = 0,  ///< "终于!芬里斯领主都已死亡!"
    SAY_MAKE_PREPARATIONS         = 1,  ///< "我会做好准备工作..."
    SAY_TEMPLE_OF_PROMISE         = 2,  ///< "承诺神殿就在前方..."
    SAY_MUST_CONTINUE             = 3,  ///< "我们必须继续前进..."
    SAY_BANISH_THE_SPIRITS        = 4,  ///< "我会驱逐这里的邪恶灵魂..."
    SAY_CAVERNS_PURIFIED          = 5,  ///< "洞穴已被净化..."
    SAY_BEYOND_THIS_CORRIDOR      = 6,  ///< "穿过这条走廊..."
    SAY_EMERALD_DREAM             = 7,  ///< "开始进入翡翠梦境..."
    EMOTE_AWAKENING_RITUAL        = 8,  ///< *开始唤醒仪式*
    EMOTE_TROUBLED_SLEEP          = 0,  ///< *纳雷克斯在痛苦中翻腾*
    EMOTE_WRITHE_IN_AGONY         = 1,  ///< *纳雷克斯在噩梦中挣扎*
    EMOTE_HORRENDOUS_VISION       = 2,  ///< *纳雷克斯看到可怕的幻象*
    SAY_MUTANUS_THE_DEVOURER      = 9,  ///< "穆塔努斯出现了!"
    SAY_I_AM_AWAKE                = 3,  ///< 纳雷克斯: "我醒了!"
    SAY_NARALEX_AWAKES            = 10, ///< "纳雷克斯醒了!"
    SAY_THANK_YOU                 = 4,  ///< 纳雷克斯: "谢谢你们..."
    SAY_FAREWELL                  = 5,  ///< 纳雷克斯: "再见..."
    SAY_ATTACKED                  = 11, ///< "我们被攻击了!"

    // Gossip菜单选项
    GOSSIP_OPTION_LET_EVENT_BEGIN = 201,  ///< Gossip菜单ID:开始事件
    NPC_TEXT_NARALEX_SLEEPS_AGAIN = 698,  ///< NPC文本ID:纳雷克斯继续沉睡(未击杀领主)
    NPC_TEXT_FANGLORDS_ARE_DEAD   = 699,  ///< NPC文本ID:芬里斯领主已死(可以开始事件)

    // 法术ID
    SPELL_MARK_OF_THE_WILD_RANK_2 = 5232,  ///< 野性印记(等级2)-Buff法术
    SPELL_SERPENTINE_CLEANSING    = 6270,  ///< 蛇形净化-持续施法法术
    SPELL_NARALEXS_AWAKENING      = 6271,  ///< 纳雷克斯唤醒仪式-引导法术
    SPELL_FLIGHT_FORM             = 33943, ///< 飞行形态-德鲁伊鸟形态

    // 怪物NPC ID
    NPC_DEVIATE_RAVAGER           = 3636,  ///< 变异掠夺者(第1阶段)
    NPC_DEVIATE_VIPER             = 5755,  ///< 变异毒蛇(第2阶段)
    NPC_DEVIATE_MOCCASIN          = 5762,  ///< 变异水蛇(第3阶段)
    NPC_NIGHTMARE_ECTOPLASM       = 5763,  ///< 噩梦精华(第3阶段)
    NPC_MUTANUS_THE_DEVOURER      = 3654,  ///< 吞噬者穆塔努斯(最终Boss)
};

/**
 * @class npc_disciple_of_naralex
 * @brief 纳雷克斯的弟子NPC脚本
 *
 * 实现纳雷克斯弟子的AI和行为:
 * - 与玩家交互(Gossip对话)
 * - 护送任务(Escort AI)
 * - 唤醒仪式事件管理
 * - 召唤怪物波次
 */
class npc_disciple_of_naralex : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"npc_disciple_of_naralex"
     */
    npc_disciple_of_naralex() : CreatureScript("npc_disciple_of_naralex") { }

    /**
     * @class npc_disciple_of_naralexAI
     * @brief 纳雷克斯弟子的AI实现
     *
     * 继承自EscortAI,实现护送任务的路径和事件逻辑
     */
    struct npc_disciple_of_naralexAI : public EscortAI
    {
        /**
         * @brief 构造函数,初始化AI状态
         * @param creature NPC生物指针
         *
         * 设置NPC为活跃状态和远距离可见,确保事件正确运行
         */
        npc_disciple_of_naralexAI(Creature* creature) : EscortAI(creature)
        {
            instance = creature->GetInstanceScript();
            eventTimer = 0;
            currentEvent = 0;
            eventProgress = 0;
            me->setActive(true);          // 激活对象,确保其在范围外时也被更新
            me->SetFarVisible(true);      // 远距离可见,防止过早消失
            me->SetImmuneToPC(false);     // 允许玩家攻击(事件开始后会设置阵营)
        }

        /// 事件计时器(毫秒),用于控制事件进度
        uint32 eventTimer;
        /// 当前事件阶段(TYPE_NARALEX_PART1/2/3)
        uint32 currentEvent;
        /// 事件进度步骤(1-11)
        uint32 eventProgress;
        /// 副本实例脚本指针
        InstanceScript* instance;

        /**
         * @brief 到达路径点时的回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID(未使用)
         *
         * 当NPC到达特定路径点时触发相应的事件阶段
         *
         * 关键路径点:
         * - 点4: 触发第1阶段(变异掠夺者)
         * - 点5: 完成第1阶段
         * - 点11: 触发第2阶段(变异毒蛇)
         * - 点19: 对话提示
         * - 点24: 触发第3阶段(唤醒仪式)
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 4:
                    eventProgress = 1;
                    currentEvent = TYPE_NARALEX_PART1;
                    instance->SetData(TYPE_NARALEX_PART1, IN_PROGRESS);
                    break;
                case 5:
                    Talk(SAY_MUST_CONTINUE);
                    instance->SetData(TYPE_NARALEX_PART1, DONE);
                    break;
                case 11:
                    eventProgress = 1;
                    currentEvent = TYPE_NARALEX_PART2;
                    instance->SetData(TYPE_NARALEX_PART2, IN_PROGRESS);
                    break;
                case 19:
                    Talk(SAY_BEYOND_THIS_CORRIDOR);
                    break;
                case 24:
                    eventProgress = 1;
                    currentEvent = TYPE_NARALEX_PART3;
                    instance->SetData(TYPE_NARALEX_PART3, IN_PROGRESS);
                    break;
            }
        }

        /**
         * @brief 重置NPC状态
         *
         * 当事件失败或NPC脱离战斗时调用
         * 目前未实现具体逻辑,依赖基类EscortAI的重置
         */
        void Reset() override
        {

        }

        /**
         * @brief 进入战斗时的回调
         * @param who 攻击者
         *
         * NPC被攻击时喊话警告
         */
        void JustEngagedWith(Unit* who) override
        {
            Talk(SAY_ATTACKED, who);
        }

        /**
         * @brief NPC死亡时的回调
         * @param killer 击杀者(未使用)
         *
         * 将所有事件阶段标记为失败,重置副本进度
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(TYPE_NARALEX_EVENT, FAIL);
            instance->SetData(TYPE_NARALEX_PART1, FAIL);
            instance->SetData(TYPE_NARALEX_PART2, FAIL);
            instance->SetData(TYPE_NARALEX_PART3, FAIL);
        }

        /**
         * @brief 召唤生物时的回调
         * @param summoned 被召唤的生物
         *
         * 让召唤的怪物立即攻击弟子,增加战斗紧迫感
         */
        void JustSummoned(Creature* summoned) override
        {
             summoned->AI()->AttackStart(me);
        }

        /**
         * @brief 主更新函数,每帧调用
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 处理护送AI逻辑和事件进度更新
         *
         * 性能注意:
         * - 大量怪物召唤可能影响性能
         * - 事件计时器确保阶段有序进行
         * - 第3阶段暂停移动,只处理事件逻辑
         */
        void UpdateAI(uint32 diff) override
        {
            // 第3阶段不执行移动逻辑,停留在祭坛前
            if (currentEvent != TYPE_NARALEX_PART3)
                EscortAI::UpdateAI(diff);

            if (eventTimer <= diff)
            {
                eventTimer = 0;
                if (instance->GetData(currentEvent) == IN_PROGRESS)
                {
                    switch (currentEvent)
                    {
                        case TYPE_NARALEX_PART1:
                            // 第1阶段: 召唤2个变异掠夺者
                            if (eventProgress == 1)
                            {
                                ++eventProgress;
                                Talk(SAY_TEMPLE_OF_PROMISE);
                                me->SummonCreature(NPC_DEVIATE_RAVAGER, -82.1763f, 227.874f, -93.3233f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                                me->SummonCreature(NPC_DEVIATE_RAVAGER, -72.9506f, 216.645f, -93.6756f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                            }
                        break;
                        case TYPE_NARALEX_PART2:
                            // 第2阶段: 净化仪式和怪物波次
                            if (eventProgress == 1)
                            {
                                ++eventProgress;
                                Talk(SAY_BANISH_THE_SPIRITS);
                                DoCast(me, SPELL_SERPENTINE_CLEANSING);
                                //CAST_AI(EscortAI, me->AI())->SetCanDefend(false); // 禁止防御(暂未实现)
                                eventTimer = 30000; // 30秒后完成净化
                                me->SummonCreature(NPC_DEVIATE_VIPER, -61.5261f, 273.676f, -92.8442f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                                me->SummonCreature(NPC_DEVIATE_VIPER, -58.4658f, 280.799f, -92.8393f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                                me->SummonCreature(NPC_DEVIATE_VIPER, -50.002f,  278.578f, -92.8442f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5s);
                            }
                            else
                            if (eventProgress == 2)
                            {
                                //CAST_AI(EscortAI, me->AI())->SetCanDefend(true); // 恢复防御
                                Talk(SAY_CAVERNS_PURIFIED);
                                instance->SetData(TYPE_NARALEX_PART2, DONE);
                                if (me->HasAura(SPELL_SERPENTINE_CLEANSING))
                                    me->RemoveAura(SPELL_SERPENTINE_CLEANSING);
                            }
                        break;
                        case TYPE_NARALEX_PART3:
                            // 第3阶段: 唤醒仪式(最长最复杂的阶段)
                            if (eventProgress == 1)
                            {
                                // 步骤1: 跪下并开始仪式
                                ++eventProgress;
                                eventTimer = 4000;
                                me->SetStandState(UNIT_STAND_STATE_KNEEL);
                                Talk(SAY_EMERALD_DREAM);
                            }
                            else
                            if (eventProgress == 2)
                            {
                                // 步骤2: 施放唤醒法术
                                ++eventProgress;
                                eventTimer = 15000;
                                //CAST_AI(EscortAI, me->AI())->SetCanDefend(false);
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    DoCast(naralex, SPELL_NARALEXS_AWAKENING, true);
                                Talk(EMOTE_AWAKENING_RITUAL);
                            }
                            else
                            if (eventProgress == 3)
                            {
                                // 步骤3: 纳雷克斯开始翻腾,召唤水蛇
                                ++eventProgress;
                                eventTimer = 15000;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->AI()->Talk(EMOTE_TROUBLED_SLEEP);
                                me->SummonCreature(NPC_DEVIATE_MOCCASIN, 135.943f, 199.701f, -103.529f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_DEVIATE_MOCCASIN, 151.08f,  221.13f,  -103.609f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_DEVIATE_MOCCASIN, 128.007f, 227.428f, -97.421f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                            }
                            else
                            if (eventProgress == 4)
                            {
                                // 步骤4: 纳雷克斯挣扎,召唤7个噩梦精华
                                ++eventProgress;
                                eventTimer = 30000;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->AI()->Talk(EMOTE_WRITHE_IN_AGONY);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 133.413f, 207.188f, -102.469f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 142.857f, 218.645f, -102.905f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 105.102f, 227.211f, -102.752f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 153.372f, 235.149f, -102.826f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 149.524f, 251.113f, -102.558f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 136.208f, 266.466f, -102.977f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                                me->SummonCreature(NPC_NIGHTMARE_ECTOPLASM, 126.167f, 274.759f, -102.962f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 15s);
                            }
                            else
                            if (eventProgress == 5)
                            {
                                // 步骤5: 召唤最终Boss穆塔努斯
                                ++eventProgress;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->AI()->Talk(EMOTE_HORRENDOUS_VISION);
                                if (Creature* mutanus = me->SummonCreature(NPC_MUTANUS_THE_DEVOURER, 150.872f, 262.905f, -103.503f, 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 5min))
                                    Talk(SAY_MUTANUS_THE_DEVOURER, mutanus);

                                instance->SetData(TYPE_MUTANUS_THE_DEVOURER, IN_PROGRESS);
                            }
                            else
                            if (eventProgress == 6 && instance->GetData(TYPE_MUTANUS_THE_DEVOURER) == DONE)
                            {
                                // 步骤6: 穆塔努斯被击杀,纳雷克斯苏醒
                                ++eventProgress;
                                eventTimer = 3000;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                {
                                    if (me->HasAura(SPELL_NARALEXS_AWAKENING))
                                        me->RemoveAura(SPELL_NARALEXS_AWAKENING);
                                    naralex->SetStandState(UNIT_STAND_STATE_STAND);
                                    naralex->AI()->Talk(SAY_I_AM_AWAKE);
                                }
                                Talk(SAY_NARALEX_AWAKES);
                            }
                            else
                            if (eventProgress == 7)
                            {
                                // 步骤7: 纳雷克斯感谢玩家
                                ++eventProgress;
                                eventTimer = 6000;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->AI()->Talk(SAY_THANK_YOU);
                            }
                            else
                            if (eventProgress == 8)
                            {
                                // 步骤8: 纳雷克斯告别,变身为飞行形态
                                ++eventProgress;
                                eventTimer = 8000;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                {
                                    naralex->AI()->Talk(SAY_FAREWELL);
                                    naralex->AddAura(SPELL_FLIGHT_FORM, naralex);
                                }
                                SetRun();
                                me->SetStandState(UNIT_STAND_STATE_STAND);
                                me->AddAura(SPELL_FLIGHT_FORM, me);
                            }
                            else
                            if (eventProgress == 9)
                            {
                                // 步骤9: 纳雷克斯开始移动
                                ++eventProgress;
                                eventTimer = 1500;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->GetMotionMaster()->MovePoint(25, naralex->GetPositionX(), naralex->GetPositionY(), naralex->GetPositionZ());
                            }
                            else
                            if (eventProgress == 10)
                            {
                                // 步骤10: 纳雷克斯和弟子飞向出口
                                ++eventProgress;
                                eventTimer = 2500;
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                {
                                    naralex->GetMotionMaster()->MovePoint(0, 117.095512f, 247.107971f, -96.167870f);
                                    naralex->GetMotionMaster()->MovePoint(1, 90.388809f, 276.135406f, -83.389801f);
                                }
                                me->GetMotionMaster()->MovePoint(26, 117.095512f, 247.107971f, -96.167870f);
                                me->GetMotionMaster()->MovePoint(27, 144.375443f, 281.045837f, -82.477135f);
                            }
                            else
                            if (eventProgress == 11)
                            {
                                // 步骤11: 纳雷克斯和弟子消失,事件完成
                                if (Creature* naralex = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NARALEX)))
                                    naralex->SetVisible(false);
                                me->SetVisible(false);
                                instance->SetData(TYPE_NARALEX_PART3, DONE);
                            }
                        break;
                    }
                }
            } else eventTimer -= diff;
        }

        /**
         * @brief Gossip菜单选项选择回调
         * @param player 玩家指针
         * @param menuId 菜单ID(未使用)
         * @param gossipListId Gossip选项列表ID
         * @return true表示处理完成
         *
         * 当玩家选择"开始事件"选项时,启动护送任务
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                if (instance)
                    instance->SetData(TYPE_NARALEX_EVENT, IN_PROGRESS);

                Talk(SAY_MAKE_PREPARATIONS);

                me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_ACTIVE); // 设置为中立项队
                me->SetImmuneToPC(false);

                Start(false, false, player->GetGUID()); // 启动护送任务(不攻击敌方,不运行模式)
                SetDespawnAtFar(false);  // 玩家远离时不消失
                SetDespawnAtEnd(false);  // 事件结束时不消失
            }
            return true;
        }

        /**
         * @brief Gossip菜单打开回调
         * @param player 玩家指针
         * @return true表示处理完成
         *
         * 根据副本进度显示不同的对话内容:
         * - 未击杀所有芬里斯领主: 提示纳雷克斯继续沉睡
         * - 已击杀所有领主: 显示开始事件选项
         *
         * 同时给玩家施加野性印记Buff
         */
        bool OnGossipHello(Player* player) override
        {
            DoCast(player, SPELL_MARK_OF_THE_WILD_RANK_2, true);
            if ((instance->GetData(TYPE_LORD_COBRAHN) == DONE) && (instance->GetData(TYPE_LORD_PYTHAS) == DONE) &&
                (instance->GetData(TYPE_LADY_ANACONDRA) == DONE) && (instance->GetData(TYPE_LORD_SERPENTIS) == DONE))
            {
                InitGossipMenuFor(player, GOSSIP_OPTION_LET_EVENT_BEGIN);
                AddGossipItemFor(player, GOSSIP_OPTION_LET_EVENT_BEGIN, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, NPC_TEXT_FANGLORDS_ARE_DEAD, me->GetGUID());

                // 首次对话时喊话,避免重复
                if (!instance->GetData(TYPE_NARALEX_YELLED))
                {
                    Talk(SAY_AT_LAST);
                    instance->SetData(TYPE_NARALEX_YELLED, 1);
                }
            }
            else
            {
                SendGossipMenuFor(player, NPC_TEXT_NARALEX_SLEEPS_AGAIN, me->GetGUID());
            }
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return 新创建的AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetWailingCavernsAI<npc_disciple_of_naralexAI>(creature);
    }
};

/**
 * @brief 注册哀嚎洞穴脚本
 *
 * 此函数在脚本加载时被调用,创建NPC脚本对象
 */
void AddSC_wailing_caverns()
{
    new npc_disciple_of_naralex();
}
