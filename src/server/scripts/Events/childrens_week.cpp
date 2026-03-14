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
 * @file    childrens_week.cpp
 * @brief   儿童周事件脚本模块
 *
 * 本模块实现了儿童周（Children's Week）节日相关的游戏机制，包括：
 * - 孤儿陪同任务：玩家可以收养各种族孤儿并带他们参观世界各地
 * - 孤儿类型：
 *   - 神谕者孤儿（Oracle Orphan）- 诺森德
 *   - 狼獾人孤儿（Wolvar Orphan）- 诺森德
 *   - 血精灵孤儿（Blood Elf Orphan）- 部落
 *   - 德莱尼孤儿（Draenei Orphan）- 联盟
 *   - 人类孤儿（Human Orphan）- 联盟
 *   - 兽人孤儿（Orcish Orphan）- 部落
 * - 参观地点任务：带孤儿参观各种著名地点并触发对话
 * - 玩伴互动：孤儿与其他NPC的互动场景
 *
 * 儿童周是一个慈善主题的节日活动，玩家通过完成孤儿任务
 * 可以获得成就和奖励。
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"

/**
 * @brief 孤儿NPC ID枚举
 *
 * 定义了各种族孤儿的NPC标识符
 */
enum Orphans
{
    ORPHAN_ORACLE                           = 33533, ///< 神谕者孤儿（诺森德）
    ORPHAN_WOLVAR                           = 33532, ///< 狼獾人孤儿（诺森德）
    ORPHAN_BLOOD_ELF                        = 22817, ///< 血精灵孤儿（部落）
    ORPHAN_DRAENEI                          = 22818, ///< 德莱尼孤儿（联盟）
    ORPHAN_HUMAN                            = 14305, ///< 人类孤儿（联盟）
    ORPHAN_ORCISH                           = 14444  ///< 兽人孤儿（部落）
};

/**
 * @brief 对话文本ID枚举
 *
 * 定义了各种孤儿和NPC的对话文本标识符
 */
enum Texts
{
    // 神谕者孤儿对话文本
    TEXT_ORACLE_ORPHAN_1                    = 1,  ///< 神谕者孤儿对话1
    TEXT_ORACLE_ORPHAN_2                    = 2,  ///< 神谕者孤儿对话2
    TEXT_ORACLE_ORPHAN_3                    = 3,  ///< 神谕者孤儿对话3
    TEXT_ORACLE_ORPHAN_4                    = 4,  ///< 神谕者孤儿对话4
    TEXT_ORACLE_ORPHAN_5                    = 5,  ///< 神谕者孤儿对话5
    TEXT_ORACLE_ORPHAN_6                    = 6,  ///< 神谕者孤儿对话6
    TEXT_ORACLE_ORPHAN_7                    = 7,  ///< 神谕者孤儿对话7
    TEXT_ORACLE_ORPHAN_8                    = 8,  ///< 神谕者孤儿对话8
    TEXT_ORACLE_ORPHAN_9                    = 9,  ///< 神谕者孤儿对话9
    TEXT_ORACLE_ORPHAN_10                   = 10, ///< 神谕者孤儿对话10
    TEXT_ORACLE_ORPHAN_11                   = 11, ///< 神谕者孤儿对话11
    TEXT_ORACLE_ORPHAN_12                   = 12, ///< 神谕者孤儿对话12
    TEXT_ORACLE_ORPHAN_13                   = 13, ///< 神谕者孤儿对话13
    TEXT_ORACLE_ORPHAN_14                   = 14, ///< 神谕者孤儿对话14

    // 狼獾人孤儿对话文本
    TEXT_WOLVAR_ORPHAN_1                    = 1,  ///< 狼獾人孤儿对话1
    TEXT_WOLVAR_ORPHAN_2                    = 2,  ///< 狼獾人孤儿对话2
    TEXT_WOLVAR_ORPHAN_3                    = 3,  ///< 狼獾人孤儿对话3
    TEXT_WOLVAR_ORPHAN_4                    = 4,  ///< 狼獾人孤儿对话4
    TEXT_WOLVAR_ORPHAN_5                    = 5,  ///< 狼獾人孤儿对话5
    // 6 - 9 used in Nesingwary script
    TEXT_WOLVAR_ORPHAN_10                   = 10, ///< 狼獾人孤儿对话10
    TEXT_WOLVAR_ORPHAN_11                   = 11, ///< 狼獾人孤儿对话11
    TEXT_WOLVAR_ORPHAN_12                   = 12, ///< 狼獾人孤儿对话12
    TEXT_WOLVAR_ORPHAN_13                   = 13, ///< 狼獾人孤儿对话13

    // 冬鳍玩伴对话文本
    TEXT_WINTERFIN_PLAYMATE_1               = 1,  ///< 冬鳍玩伴对话1
    TEXT_WINTERFIN_PLAYMATE_2               = 2,  ///< 冬鳍玩伴对话2

    // 雪落空地玩伴对话文本
    TEXT_SNOWFALL_GLADE_PLAYMATE_1          = 1,  ///< 雪落空地玩伴对话1
    TEXT_SNOWFALL_GLADE_PLAYMATE_2          = 2,  ///< 雪落空地玩伴对话2

    // 其他NPC对话文本
    TEXT_SOO_ROO_1                          = 1,  ///< 苏罗对话1
    TEXT_ELDER_KEKEK_1                      = 1,  ///< 凯凯克长者对话1

    TEXT_ALEXSTRASZA_2                      = 2,  ///< 阿莱克丝塔萨对话2
    TEXT_KRASUS_8                           = 8   ///< 克拉苏斯对话8
};

/**
 * @brief 任务ID枚举
 *
 * 定义了儿童周相关任务的标识符
 */
enum Quests
{
    QUEST_PLAYMATE_WOLVAR                   = 13951, ///< 玩伴任务（狼獾人）
    QUEST_PLAYMATE_ORACLE                   = 13950, ///< 玩伴任务（神谕者）
    QUEST_THE_BIGGEST_TREE_EVER             = 13929, ///< 最大的树
    QUEST_THE_BRONZE_DRAGONSHRINE_ORACLE    = 13933, ///< 青铜龙殿（神谕者）
    QUEST_THE_BRONZE_DRAGONSHRINE_WOLVAR    = 13934, ///< 青铜龙殿（狼獾人）
    QUEST_MEETING_A_GREAT_ONE               = 13956, ///< 会见伟人
    QUEST_THE_MIGHTY_HEMET_NESINGWARY       = 13957, ///< 强大的赫米特·奈辛瓦里
    QUEST_DOWN_AT_THE_DOCKS                 = 910,   ///< 码头边（旧版）
    QUEST_GATEWAY_TO_THE_FRONTIER           = 911,   ///< 前线之门（旧版）
    QUEST_BOUGHT_OF_ETERNALS                = 1479,  ///< 永恒之债
    QUEST_SPOOKY_LIGHTHOUSE                 = 1687,  ///< 诡异的灯塔
    QUEST_STONEWROUGHT_DAM                  = 1558,  ///< 石坝
    QUEST_DARK_PORTAL_H                     = 10951, ///< 黑暗之门（部落）
    QUEST_DARK_PORTAL_A                     = 10952, ///< 黑暗之门（联盟）
    QUEST_LORDAERON_THRONE_ROOM             = 1800,  ///< 洛丹伦王座大厅
    QUEST_AUCHINDOUN_AND_THE_RING           = 10950, ///< 奥金顿和环形山
    QUEST_TIME_TO_VISIT_THE_CAVERNS_H       = 10963, ///< 参观时光之穴（部落）
    QUEST_TIME_TO_VISIT_THE_CAVERNS_A       = 10962, ///< 参观时光之穴（联盟）
    QUEST_THE_SEAT_OF_THE_NARUU             = 10956, ///< 纳鲁的宝座
    QUEST_CALL_ON_THE_FARSEER               = 10968, ///< 拜访先知
    QUEST_JHEEL_IS_AT_AERIS_LANDING         = 10954, ///< 吉希尔在埃瑞斯码头
    QUEST_HCHUU_AND_THE_MUSHROOM_PEOPLE     = 10945, ///< 赫楚和蘑菇人
    QUEST_VISIT_THE_THRONE_OF_ELEMENTS      = 10953, ///< 参观元素王座
    QUEST_NOW_WHEN_I_GROW_UP                = 11975, ///< 当我长大后
    QUEST_HOME_OF_THE_BEAR_MEN              = 13930, ///< 熊人的家
    QUEST_THE_DRAGON_QUEEN_ORACLE           = 13954, ///< 龙女王（神谕者）
    QUEST_THE_DRAGON_QUEEN_WOLVAR           = 13955  ///< 龙女王（狼獾人）
};

/**
 * @brief 区域触发器和触发NPC枚举
 *
 * 定义了儿童周任务相关的区域触发器和触发NPC标识符
 */
enum Areatriggers
{
    AT_DOWN_AT_THE_DOCKS                    = 3551, ///< 码头区域触发器
    AT_GATEWAY_TO_THE_FRONTIER              = 3549, ///< 前线之门区域触发器
    AT_LORDAERON_THRONE_ROOM                = 3547, ///< 洛丹伦王座大厅区域触发器
    AT_BOUGHT_OF_ETERNALS                   = 3546, ///< 永恒之债区域触发器
    AT_SPOOKY_LIGHTHOUSE                    = 3552, ///< 诡异灯塔区域触发器
    AT_STONEWROUGHT_DAM                     = 3548, ///< 石坝区域触发器
    AT_DARK_PORTAL                          = 4356, ///< 黑暗之门区域触发器

    NPC_CAVERNS_OF_TIME_CW_TRIGGER          = 22872, ///< 时光之穴触发NPC
    NPC_EXODAR_01_CW_TRIGGER                = 22851, ///< 埃索达触发NPC 01
    NPC_EXODAR_02_CW_TRIGGER                = 22905, ///< 埃索达触发NPC 02
    NPC_AERIS_LANDING_CW_TRIGGER            = 22838, ///< 埃瑞斯码头触发NPC
    NPC_AUCHINDOUN_CW_TRIGGER               = 22831, ///< 奥金顿触发NPC
    NPC_SPOREGGAR_CW_TRIGGER                = 22829, ///< 孢子村触发NPC
    NPC_THRONE_OF_ELEMENTS_CW_TRIGGER       = 22839, ///< 元素王座触发NPC
    NPC_SILVERMOON_01_CW_TRIGGER            = 22866, ///< 银月城触发NPC
    NPC_KRASUS                              = 27990  ///< 克拉苏斯NPC
};

/**
 * @brief 杂项数据枚举
 *
 * 定义了法术ID和显示模型ID等其他数据
 */
enum Misc
{
    SPELL_SNOWBALL                          = 21343, ///< 雪球法术
    SPELL_ORPHAN_OUT                        = 58818, ///< 孤儿外出法术

    DISPLAY_INVISIBLE                       = 11686  ///< 隐形显示模型ID
};

/**
 * @brief 获取玩家的孤儿GUID
 * @param player 玩家对象
 * @param orphan 孤儿NPC ID
 * @return 孤儿的GUID，如果没有对应的孤儿返回空GUID
 *
 * 通过检查玩家身上的"孤儿外出"光环来获取孤儿的GUID
 */
ObjectGuid getOrphanGUID(Player* player, uint32 orphan)
{
    if (Aura* orphanOut = player->GetAura(SPELL_ORPHAN_OUT))
        if (orphanOut->GetCaster() && orphanOut->GetCaster()->GetEntry() == orphan)
            return orphanOut->GetCaster()->GetGUID();

    return ObjectGuid::Empty;
}

/**
 * @class npc_winterfin_playmate
 * @brief 冬鳍玩伴NPC脚本 - 处理神谕者孤儿的玩伴互动任务
 *
 * 这个脚本实现了神谕者孤儿的玩伴任务。当玩家带着神谕者孤儿
 * 接近冬鳍玩伴时，会触发一系列对话和舞蹈互动。
 * 任务：QUEST_PLAYMATE_ORACLE (13950)
 */
/*######
## npc_winterfin_playmate
######*/
class npc_winterfin_playmate : public CreatureScript
{
    public:
        npc_winterfin_playmate() : CreatureScript("npc_winterfin_playmate") { }

        /**
         * @class npc_winterfin_playmateAI
         * @brief 冬鳍玩伴AI - 实现玩伴互动的场景脚本
         */
        struct npc_winterfin_playmateAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 关联的生物对象
             */
            npc_winterfin_playmateAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                timer = 0;       ///< 事件计时器
                phase = 0;       ///< 当前阶段
                playerGUID.Clear(); ///< 玩家GUID
                orphanGUID.Clear(); ///< 孤儿GUID
            }

            void Reset() override
            {
                Initialize();
            }

            /**
             * @brief 视线内移动检测 - 触发互动
             * @param who 进入视线的单位
             *
             * 当玩家带着神谕者孤儿接近时，启动互动场景
             */
            void MoveInLineOfSight(Unit* who) override
            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_PLAYMATE_ORACLE) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_ORACLE);
                            if (orphanGUID)
                                phase = 1;
                        }
            }

            /**
             * @brief 更新AI - 处理分阶段对话和动画
             * @param diff 时间间隔（毫秒）
             *
             * 分阶段执行互动场景：
             * - 阶段1：孤儿移动到玩伴附近并说话
             * - 阶段2：玩伴面向孤儿，说话并跳舞
             * - 阶段3：孤儿回应
             * - 阶段4：玩伴回应
             * - 阶段5：孤儿结束对话，任务完成
             */
            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            // 孤儿移动到玩伴附近并说话
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_1);
                            timer = 3000;
                            break;
                        case 2:
                            // 玩伴面向孤儿，说话并跳舞
                            orphan->SetFacingToObject(me);
                            Talk(TEXT_WINTERFIN_PLAYMATE_1);
                            me->HandleEmoteCommand(EMOTE_STATE_DANCE);
                            timer = 3000;
                            break;
                        case 3:
                            // 孤儿回应
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_2);
                            timer = 3000;
                            break;
                        case 4:
                            // 玩伴回应
                            Talk(TEXT_WINTERFIN_PLAYMATE_2);
                            timer = 5000;
                            break;
                        case 5:
                            // 孤儿结束对话，任务完成
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_3);
                            me->HandleEmoteCommand(EMOTE_STATE_NONE);
                            player->GroupEventHappens(QUEST_PLAYMATE_ORACLE, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;        ///< 事件计时器
            int8 phase;          ///< 当前阶段
            ObjectGuid playerGUID; ///< 玩家GUID
            ObjectGuid orphanGUID; ///< 孤儿GUID

        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_winterfin_playmateAI(creature);
        }
};

/**
 * @class npc_snowfall_glade_playmate
 * @brief 雪落空地玩伴NPC脚本 - 处理狼獾人孤儿的玩伴互动任务
 *
 * 这个脚本实现了狼獾人孤儿的玩伴任务。当玩家带着狼獾人孤儿
 * 接近雪落空地玩伴时，会触发一系列对话和雪球互扔互动。
 * 任务：QUEST_PLAYMATE_WOLVAR (13951)
 */
/*######
## npc_snowfall_glade_playmate
######*/
class npc_snowfall_glade_playmate : public CreatureScript
{
    public:
        npc_snowfall_glade_playmate() : CreatureScript("npc_snowfall_glade_playmate") { }

        /**
         * @class npc_snowfall_glade_playmateAI
         * @brief 雪落空地玩伴AI - 实现玩伴互动的场景脚本
         */
        struct npc_snowfall_glade_playmateAI : public ScriptedAI
        {
            npc_snowfall_glade_playmateAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                timer = 0;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            /**
             * @brief 视线内移动检测 - 触发互动
             * @param who 进入视线的单位
             */
            void MoveInLineOfSight(Unit* who) override

            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_PLAYMATE_WOLVAR) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_WOLVAR);
                            if (orphanGUID)
                                phase = 1;
                        }
            }

            /**
             * @brief 更新AI - 处理分阶段对话和雪球互扔
             * @param diff 时间间隔（毫秒）
             *
             * 分阶段执行互动场景：
             * - 阶段1：孤儿移动到玩伴附近并说话
             * - 阶段2：玩伴向孤儿扔雪球
             * - 阶段3：玩伴继续对话
             * - 阶段4：孤儿向玩伴扔回雪球
             * - 阶段5：孤儿结束对话，任务完成
             */
            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            // 孤儿移动到玩伴附近并说话
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_1);
                            timer = 5000;
                            break;
                        case 2:
                            // 玩伴面向孤儿，说话并扔雪球
                            orphan->SetFacingToObject(me);
                            Talk(TEXT_SNOWFALL_GLADE_PLAYMATE_1);
                            DoCast(orphan, SPELL_SNOWBALL);
                            timer = 5000;
                            break;
                        case 3:
                            // 玩伴继续对话
                            Talk(TEXT_SNOWFALL_GLADE_PLAYMATE_2);
                            timer = 5000;
                            break;
                        case 4:
                            // 孤儿向玩伴扔回雪球
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_2);
                            orphan->CastSpell(me, SPELL_SNOWBALL);
                            timer = 5000;
                            break;
                        case 5:
                            // 孤儿结束对话，任务完成
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_3);
                            player->GroupEventHappens(QUEST_PLAYMATE_WOLVAR, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;        ///< 事件计时器
            int8 phase;          ///< 当前阶段
            ObjectGuid playerGUID; ///< 玩家GUID
            ObjectGuid orphanGUID; ///< 孤儿GUID
        };

        CreatureAI* GetAI(Creature* pCreature) const override
        {
            return new npc_snowfall_glade_playmateAI(pCreature);
        }
};

/*######
## npc_the_biggest_tree
######*/
class npc_the_biggest_tree : public CreatureScript
{
    public:
        npc_the_biggest_tree() : CreatureScript("npc_the_biggest_tree") { }

        struct npc_the_biggest_treeAI : public ScriptedAI
        {
            npc_the_biggest_treeAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                me->SetDisplayId(DISPLAY_INVISIBLE);
            }

            void Initialize()
            {
                timer = 1000;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            void MoveInLineOfSight(Unit* who) override
            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_THE_BIGGEST_TREE_EVER) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_ORACLE);
                            if (orphanGUID)
                                phase = 1;
                        }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            timer = 2000;
                            break;
                        case 2:
                            orphan->SetFacingToObject(me);
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_4);
                            timer = 5000;
                            break;
                        case 3:
                            player->GroupEventHappens(QUEST_THE_BIGGEST_TREE_EVER, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;
            uint8 phase;
            ObjectGuid playerGUID;
            ObjectGuid orphanGUID;

        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_the_biggest_treeAI(creature);
        }
};

/*######
## npc_high_oracle_soo_roo
######*/
class npc_high_oracle_soo_roo : public CreatureScript
{
    public:
        npc_high_oracle_soo_roo() : CreatureScript("npc_high_oracle_soo_roo") { }

        struct npc_high_oracle_soo_rooAI : public ScriptedAI
        {
            npc_high_oracle_soo_rooAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                timer = 0;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            void MoveInLineOfSight(Unit* who) override

            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_THE_BRONZE_DRAGONSHRINE_ORACLE) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_ORACLE);
                            if (orphanGUID)
                                phase = 1;
                        }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_5);
                            timer = 3000;
                            break;
                        case 2:
                            orphan->SetFacingToObject(me);
                            Talk(TEXT_SOO_ROO_1, player);
                            timer = 6000;
                            break;
                        case 3:
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_6);
                            player->GroupEventHappens(QUEST_THE_BRONZE_DRAGONSHRINE_ORACLE, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;
            int8 phase;
            ObjectGuid playerGUID;
            ObjectGuid orphanGUID;

        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_high_oracle_soo_rooAI(creature);
        }
};

/*######
## npc_elder_kekek
######*/
class npc_elder_kekek : public CreatureScript
{
    public:
        npc_elder_kekek() : CreatureScript("npc_elder_kekek") { }

        struct npc_elder_kekekAI : public ScriptedAI
        {
            npc_elder_kekekAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                timer = 0;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            void MoveInLineOfSight(Unit* who) override
            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_THE_BRONZE_DRAGONSHRINE_WOLVAR) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_WOLVAR);
                            if (orphanGUID)
                                phase = 1;
                        }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!player || !orphan)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_4);
                            timer = 3000;
                            break;
                        case 2:
                            Talk(TEXT_ELDER_KEKEK_1);
                            timer = 6000;
                            break;
                        case 3:
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_5);
                            player->GroupEventHappens(QUEST_THE_BRONZE_DRAGONSHRINE_WOLVAR, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;
            int8 phase;
            ObjectGuid playerGUID;
            ObjectGuid orphanGUID;

        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_elder_kekekAI(creature);
        }
};

enum TheEtymidian
{
    SAY_ACTIVATION            = 0,
    QUEST_THE_ACTIVATION_RUNE = 12547
};

/*######
## npc_the_etymidian
## @todo A red crystal as a gift for the great one should be spawned during the event.
######*/
class npc_the_etymidian : public CreatureScript
{
    public:
        npc_the_etymidian() : CreatureScript("npc_the_etymidian") { }

        struct npc_the_etymidianAI : public ScriptedAI
        {
            npc_the_etymidianAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                timer = 0;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            void OnQuestReward(Player* /*player*/, Quest const* quest, uint32 /*opt*/) override
            {
                if (quest->GetQuestId() != QUEST_THE_ACTIVATION_RUNE)
                    return;

                Talk(SAY_ACTIVATION);
            }

            // doesn't trigger if creature is stunned. Restore aura 25900 when it will be possible or
            // find another way to start event(from orphan script)
            void MoveInLineOfSight(Unit* who) override
            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                {
                    if (Player* player = who->ToPlayer())
                    {
                        if (player->GetQuestStatus(QUEST_MEETING_A_GREAT_ONE) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_ORACLE);
                            if (orphanGUID)
                                phase = 1;
                        }
                    }
                }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_7);
                            timer = 5000;
                            break;
                        case 2:
                            orphan->SetFacingToObject(me);
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_8);
                            timer = 5000;
                            break;
                        case 3:
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_9);
                            timer = 5000;
                            break;
                        case 4:
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_10);
                            timer = 5000;
                            break;
                        case 5:
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            player->GroupEventHappens(QUEST_MEETING_A_GREAT_ONE, me);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

        private:
            uint32 timer;
            int8 phase;
            ObjectGuid playerGUID;
            ObjectGuid orphanGUID;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_the_etymidianAI(creature);
        }
};

/*######
## npc_cw_alexstrasza_trigger
######*/
class npc_alexstraza_the_lifebinder : public CreatureScript
{
    public:
        npc_alexstraza_the_lifebinder() : CreatureScript("npc_alexstraza_the_lifebinder") { }

        struct npc_alexstraza_the_lifebinderAI : public ScriptedAI
        {
            npc_alexstraza_the_lifebinderAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                timer = 0;
                phase = 0;
                playerGUID.Clear();
                orphanGUID.Clear();
            }

            void Reset() override
            {
                Initialize();
            }

            void SetData(uint32 type, uint32 data) override
            {
                // Existing SmartAI
                if (type == 0)
                {
                    switch (data)
                    {
                        case 1:
                            me->SetOrientation(1.6049f);
                            break;
                        case 2:
                            me->SetOrientation(me->GetHomePosition().GetOrientation());
                            break;
                    }
                }
            }

            void MoveInLineOfSight(Unit* who) override
            {
                if (!phase && who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                    {
                        if (player->GetQuestStatus(QUEST_THE_DRAGON_QUEEN_ORACLE) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_ORACLE);
                            if (orphanGUID)
                                phase = 1;
                        }
                        else if (player->GetQuestStatus(QUEST_THE_DRAGON_QUEEN_WOLVAR) == QUEST_STATUS_INCOMPLETE)
                        {
                            playerGUID = player->GetGUID();
                            orphanGUID = getOrphanGUID(player, ORPHAN_WOLVAR);
                            if (orphanGUID)
                                phase = 7;
                        }
                    }
            }

            void UpdateAI(uint32 diff) override
            {
                if (!phase)
                    return;

                if (timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, playerGUID);
                    Creature* orphan = ObjectAccessor::GetCreature(*me, orphanGUID);

                    if (!orphan || !player)
                    {
                        Reset();
                        return;
                    }

                    switch (phase)
                    {
                        case 1:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_11);
                            timer = 5000;
                            break;
                        case 2:
                            orphan->SetFacingToObject(me);
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_12);
                            timer = 5000;
                            break;
                        case 3:
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_13);
                            timer = 5000;
                            break;
                        case 4:
                            Talk(TEXT_ALEXSTRASZA_2, orphan);
                            me->SetStandState(UNIT_STAND_STATE_KNEEL);
                            me->SetFacingToObject(orphan);
                            timer = 5000;
                            break;
                        case 5:
                            orphan->AI()->Talk(TEXT_ORACLE_ORPHAN_14);
                            timer = 5000;
                            break;
                        case 6:
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                            me->SetOrientation(me->GetHomePosition().GetOrientation());
                            player->GroupEventHappens(QUEST_THE_DRAGON_QUEEN_ORACLE, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                        case 7:
                            orphan->GetMotionMaster()->MovePoint(0, me->GetPositionX() + std::cos(me->GetOrientation()) * 5, me->GetPositionY() + std::sin(me->GetOrientation()) * 5, me->GetPositionZ());
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_11);
                            timer = 5000;
                            break;
                        case 8:
                            if (Creature* krasus = me->FindNearestCreature(NPC_KRASUS, 10.0f))
                            {
                                orphan->SetFacingToObject(krasus);
                                krasus->AI()->Talk(TEXT_KRASUS_8);
                            }
                            timer = 5000;
                            break;
                        case 9:
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_12);
                            timer = 5000;
                            break;
                        case 10:
                            orphan->SetFacingToObject(me);
                            Talk(TEXT_ALEXSTRASZA_2, orphan);
                            timer = 5000;
                            break;
                        case 11:
                            orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_13);
                            timer = 5000;
                            break;
                        case 12:
                            player->GroupEventHappens(QUEST_THE_DRAGON_QUEEN_WOLVAR, me);
                            orphan->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
                            Reset();
                            return;
                    }
                    ++phase;
                }
                else
                    timer -= diff;
            }

            private:
                int8 phase;
                uint32 timer;
                ObjectGuid playerGUID;
                ObjectGuid orphanGUID;

        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_alexstraza_the_lifebinderAI(creature);
        }
};

/*######
## at_bring_your_orphan_to
######*/

class at_bring_your_orphan_to : public AreaTriggerScript
{
    public:
        at_bring_your_orphan_to() : AreaTriggerScript("at_bring_your_orphan_to") { }

        bool OnTrigger(Player* player, AreaTriggerEntry const* trigger) override
        {
            if (player->isDead() || !player->HasAura(SPELL_ORPHAN_OUT))
                return false;

            uint32 questId = 0;
            uint32 orphanId = 0;

            switch (trigger->ID)
            {
                case AT_DOWN_AT_THE_DOCKS:
                    questId = QUEST_DOWN_AT_THE_DOCKS;
                    orphanId = ORPHAN_ORCISH;
                    break;
                case AT_GATEWAY_TO_THE_FRONTIER:
                    questId = QUEST_GATEWAY_TO_THE_FRONTIER;
                    orphanId = ORPHAN_ORCISH;
                    break;
                case AT_LORDAERON_THRONE_ROOM:
                    questId = QUEST_LORDAERON_THRONE_ROOM;
                    orphanId = ORPHAN_ORCISH;
                    break;
                case AT_BOUGHT_OF_ETERNALS:
                    questId = QUEST_BOUGHT_OF_ETERNALS;
                    orphanId = ORPHAN_HUMAN;
                    break;
                case AT_SPOOKY_LIGHTHOUSE:
                    questId = QUEST_SPOOKY_LIGHTHOUSE;
                    orphanId = ORPHAN_HUMAN;
                    break;
                case AT_STONEWROUGHT_DAM:
                    questId = QUEST_STONEWROUGHT_DAM;
                    orphanId = ORPHAN_HUMAN;
                    break;
                case AT_DARK_PORTAL:
                    questId = player->GetTeam() == ALLIANCE ? QUEST_DARK_PORTAL_A : QUEST_DARK_PORTAL_H;
                    orphanId = player->GetTeam() == ALLIANCE ? ORPHAN_DRAENEI : ORPHAN_BLOOD_ELF;
                    break;
            }

            if (questId && orphanId && getOrphanGUID(player, orphanId) && player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                player->AreaExploredOrEventHappens(questId);

            return true;
        }
};

/*######
## npc_cw_area_trigger
######*/
class npc_cw_area_trigger : public CreatureScript
{
    public:
        npc_cw_area_trigger() : CreatureScript("npc_cw_area_trigger") { }

        struct npc_cw_area_triggerAI : public ScriptedAI
        {
            npc_cw_area_triggerAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetDisplayId(DISPLAY_INVISIBLE);
            }

            void MoveInLineOfSight(Unit* who) override
            {
                if (who && me->GetDistance2d(who) < 20.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->HasAura(SPELL_ORPHAN_OUT))
                        {
                            uint32 questId = 0;
                            uint32 orphanId = 0;
                            switch (me->GetEntry())
                            {
                                case NPC_CAVERNS_OF_TIME_CW_TRIGGER:
                                    questId = player->GetTeam() == ALLIANCE ? QUEST_TIME_TO_VISIT_THE_CAVERNS_A : QUEST_TIME_TO_VISIT_THE_CAVERNS_H;
                                    orphanId = player->GetTeam() == ALLIANCE ? ORPHAN_DRAENEI : ORPHAN_BLOOD_ELF;
                                    break;
                                case NPC_EXODAR_01_CW_TRIGGER:
                                    questId = QUEST_THE_SEAT_OF_THE_NARUU;
                                    orphanId = ORPHAN_DRAENEI;
                                    break;
                                case NPC_EXODAR_02_CW_TRIGGER:
                                    questId = QUEST_CALL_ON_THE_FARSEER;
                                    orphanId = ORPHAN_DRAENEI;
                                    break;
                                case NPC_AERIS_LANDING_CW_TRIGGER:
                                    questId = QUEST_JHEEL_IS_AT_AERIS_LANDING;
                                    orphanId = ORPHAN_DRAENEI;
                                    break;
                                case NPC_AUCHINDOUN_CW_TRIGGER:
                                    questId = QUEST_AUCHINDOUN_AND_THE_RING;
                                    orphanId = ORPHAN_DRAENEI;
                                    break;
                                case NPC_SPOREGGAR_CW_TRIGGER:
                                    questId = QUEST_HCHUU_AND_THE_MUSHROOM_PEOPLE;
                                    orphanId = ORPHAN_BLOOD_ELF;
                                    break;
                                case NPC_THRONE_OF_ELEMENTS_CW_TRIGGER:
                                    questId = QUEST_VISIT_THE_THRONE_OF_ELEMENTS;
                                    orphanId = ORPHAN_BLOOD_ELF;
                                    break;
                                case NPC_SILVERMOON_01_CW_TRIGGER:
                                    if (player->GetQuestStatus(QUEST_NOW_WHEN_I_GROW_UP) == QUEST_STATUS_INCOMPLETE && getOrphanGUID(player, ORPHAN_BLOOD_ELF))
                                    {
                                        player->AreaExploredOrEventHappens(QUEST_NOW_WHEN_I_GROW_UP);
                                        if (player->GetQuestStatus(QUEST_NOW_WHEN_I_GROW_UP) == QUEST_STATUS_COMPLETE)
                                            if (Creature* samuro = me->FindNearestCreature(25151, 20.0f))
                                            {
                                                Emote const emotes[] =
                                                    {
                                                        EMOTE_ONESHOT_WAVE,
                                                        EMOTE_ONESHOT_ROAR,
                                                        EMOTE_ONESHOT_FLEX,
                                                        EMOTE_ONESHOT_SALUTE,
                                                        EMOTE_ONESHOT_DANCE
                                                    };
                                                samuro->HandleEmoteCommand(Trinity::Containers::SelectRandomContainerElement(emotes));
                                            }
                                    }
                                    break;
                            }
                            if (questId && orphanId && getOrphanGUID(player, orphanId) && player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                                player->AreaExploredOrEventHappens(questId);
                        }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_cw_area_triggerAI(creature);
        }
};

/**
 * @class npc_grizzlemaw_cw_trigger
 * @brief 灰熊之丘儿童周触发器NPC脚本 - 处理"熊人的家"任务
 *
 * 这个触发器NPC是隐形的，当玩家带着狼獾人孤儿接近时，
 * 会完成任务并让孤儿发表评论。
 * 任务：QUEST_HOME_OF_THE_BEAR_MEN (13930)
 */
/*######
## npc_grizzlemaw_cw_trigger
######*/
class npc_grizzlemaw_cw_trigger : public CreatureScript
{
    public:
        npc_grizzlemaw_cw_trigger() : CreatureScript("npc_grizzlemaw_cw_trigger") { }

        struct npc_grizzlemaw_cw_triggerAI : public ScriptedAI
        {
            /**
             * @brief 构造函数 - 设置隐形显示模型
             * @param creature 关联的生物对象
             */
            npc_grizzlemaw_cw_triggerAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetDisplayId(DISPLAY_INVISIBLE);
            }

            /**
             * @brief 视线内移动检测 - 触发任务完成
             * @param who 进入视线的单位
             *
             * 当玩家带着狼獾人孤儿接近时完成任务
             */
            void MoveInLineOfSight(Unit* who) override
            {
                if (who && who->GetDistance2d(me) < 10.0f)
                    if (Player* player = who->ToPlayer())
                        if (player->GetQuestStatus(QUEST_HOME_OF_THE_BEAR_MEN) == QUEST_STATUS_INCOMPLETE)
                            if (Creature* orphan = ObjectAccessor::GetCreature(*me, getOrphanGUID(player, ORPHAN_WOLVAR)))
                            {
                                // 完成任务并让孤儿说话
                                player->AreaExploredOrEventHappens(QUEST_HOME_OF_THE_BEAR_MEN);
                                orphan->AI()->Talk(TEXT_WOLVAR_ORPHAN_10);
                            }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_grizzlemaw_cw_triggerAI(creature);
        }
};

/**
 * @brief 注册儿童周事件脚本
 *
 * 此函数在服务器启动时被调用，用于注册所有儿童周相关的NPC和区域触发器脚本。
 * 注册的脚本包括：
 * - 各种玩伴NPC
 * - 任务触发器NPC
 * - 区域触发器
 */
void AddSC_event_childrens_week()
{
    new npc_elder_kekek();           ///< 凯凯克长者NPC
    new npc_high_oracle_soo_roo();   ///< 高阶神谕者苏罗NPC
    new npc_winterfin_playmate();    ///< 冬鳍玩伴NPC
    new npc_snowfall_glade_playmate(); ///< 雪落空地玩伴NPC
    new npc_the_etymidian();         ///< 词源者NPC
    new npc_the_biggest_tree();      ///< 最大的树NPC
    new at_bring_your_orphan_to();   ///< 带孤儿参观区域触发器
    new npc_grizzlemaw_cw_trigger(); ///< 灰熊之丘儿童周触发器NPC
    new npc_cw_area_trigger();       ///< 儿童周区域触发器NPC
    new npc_alexstraza_the_lifebinder(); ///< 生命缚誓者阿莱克丝塔萨NPC
}
