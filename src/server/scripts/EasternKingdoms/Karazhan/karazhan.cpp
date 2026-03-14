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
 * @file    karazhan.cpp
 * @brief   卡拉赞副本辅助NPC脚本
 * @details 实现卡拉赞副本中的辅助NPC,包括:
 *          - 巴恩斯(Barnes): 歌剧事件主持人,控制歌剧表演的开始和进程
 *          - 麦迪文影像(Image of Medivh): 任务NPC,展示麦迪文与阿卡纳戈斯的对话
 *          支持任务9645(麦迪文的日记)
 */

/* ScriptData
SDName: Karazhan
SD%Complete: 100
SDComment: Support for Barnes (Opera controller) and Berthold (Doorman), Support for Quest 9645.
SDCategory: Karazhan
EndScriptData */

/* ContentData
npc_barnes
npc_image_of_medivh
EndContentData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "karazhan.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    // 巴恩斯相关法术
    SPELL_SPOTLIGHT             = 25824, ///< 聚光灯效果 - 歌剧表演时的舞台照明
    SPELL_TUXEDO                = 32616, ///< 燕尾服 - 巴恩斯的演出服装

    // 贝托尔德相关法术
    SPELL_TELEPORT              = 39567, ///< 传送法术 - 将玩家传送到卡拉赞入口

    // 麦迪文影像相关法术
    SPELL_FIRE_BALL             = 30967, ///< 火球术 - 麦迪文的基础攻击法术
    SPELL_UBER_FIREBALL         = 30971, ///< 强化火球 - 麦迪文的强力火球
    SPELL_CONFLAGRATION_BLAST   = 30977, ///< 爆燃冲击 - 对阿卡纳戈斯的终结技
    SPELL_MANA_SHIELD           = 31635  ///< 法力护盾 - 麦迪文的防御护盾
};

/**
 * @brief 生物ID枚举
 */
enum Creatures
{
    NPC_ARCANAGOS               = 17652, ///< 阿卡纳戈斯 - 与麦迪文对话的蓝龙
    NPC_SPOTLIGHT               = 19525  ///< 聚光灯 - 歌剧表演时的舞台灯光NPC
};

/*######
# npc_barnesAI
######*/

/**
 * @brief 杂项常量枚举
 */
enum Misc
{
    OZ_GOSSIP1_MID              = 7421, ///< 我不是演员 - 菜单ID
    OZ_GOSSIP1_OID              = 0,    ///< 选项ID
    OZ_GOSSIP2_MID              = 7422, ///< 好吧,那我试试 - 菜单ID
    OZ_GOSSIP2_OID              = 0,    ///< 选项ID
};

#define OZ_GM_GOSSIP1       "[GM] Change event to EVENT_OZ"   ///< GM调试选项:切换到绿野仙踪
#define OZ_GM_GOSSIP2       "[GM] Change event to EVENT_HOOD" ///< GM调试选项:切换到小红帽
#define OZ_GM_GOSSIP3       "[GM] Change event to EVENT_RAJ"  ///< GM调试选项:切换到罗密欧与朱丽叶

/**
 * @struct Dialogue
 * @brief 对话数据结构体
 * @details 定义对话文本ID和对应的延迟时间
 */
struct Dialogue
{
    int32 textid;  ///< 对话文本ID
    uint32 timer;  ///< 对话延迟时间(毫秒)
};

/**
 * @brief 绿野仙踪对话数据
 * @details 巴恩斯在绿野仙踪表演前的开场白
 */
static Dialogue OzDialogue[]=
{
    {0, 6000},   ///< 对话1,延迟6秒
    {1, 18000},  ///< 对话2,延迟18秒
    {2, 9000},   ///< 对话3,延迟9秒
    {3, 15000}   ///< 对话4,延迟15秒
};

/**
 * @brief 小红帽对话数据
 * @details 巴恩斯在小红帽表演前的开场白
 */
static Dialogue HoodDialogue[]=
{
    {4, 6000},   ///< 对话1,延迟6秒
    {5, 10000},  ///< 对话2,延迟10秒
    {6, 14000},  ///< 对话3,延迟14秒
    {7, 15000}   ///< 对话4,延迟15秒
};

/**
 * @brief 罗密欧与朱丽叶对话数据
 * @details 巴恩斯在罗密欧与朱丽叶表演前的开场白
 */
static Dialogue RAJDialogue[]=
{
    {8, 5000},   ///< 对话1,延迟5秒
    {9, 7000},   ///< 对话2,延迟7秒
    {10, 14000}, ///< 对话3,延迟14秒
    {11, 14000}  ///< 对话4,延迟14秒
};

/**
 * @brief 歌剧事件生物生成数据
 * @details 定义了各个歌剧事件中Boss的ID和生成X坐标
 *          格式: {生物ID, X坐标}
 */
float Spawns[6][2]=
{
    {17535, -10896},  ///< 桃乐丝(Dorothee) - 绿野仙踪
    {17546, -10891},  ///< 狮子(Roar) - 绿野仙踪
    {17547, -10884},  ///< 铁皮人(Tinhead) - 绿野仙踪
    {17543, -10902},  ///< 稻草人(Strawman) - 绿野仙踪
    {17603, -10892},  ///< 外婆(Grandmother) - 小红帽
    {17534, -10900},  ///< 朱丽叶(Julianne) - 罗密欧与朱丽叶
};

#define SPAWN_Z             90.5f   ///< 歌剧事件生物生成的固定Z坐标
#define SPAWN_Y             -1758   ///< 歌剧事件生物生成的固定Y坐标
#define SPAWN_O             4.738f  ///< 歌剧事件生物生成的固定朝向

/**
 * @class npc_barnes
 * @brief 巴恩斯NPC脚本类
 * @details 管理歌剧事件的主持人巴恩斯,负责:
 *          - 与玩家交互,启动歌剧事件
 *          - 表演开场白
 *          - 召唤歌剧事件的Boss
 *          - 监控团队状态(团灭检测)
 */
class npc_barnes : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_barnes() : CreatureScript("npc_barnes") { }

    /**
     * @struct npc_barnesAI
     * @brief 巴恩斯AI结构体
     * @details 实现巴恩斯的护卫AI,管理歌剧事件流程
     */
    struct npc_barnesAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_barnesAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
            RaidWiped = false;
            m_uiEventId = 0;
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            m_uiSpotlightGUID.Clear();

            TalkCount = 0;
            TalkTimer = 2000;
            WipeTimer = 5000;

            PerformanceReady = false;
        }

        InstanceScript* instance;      ///< 副本实例脚本指针

        ObjectGuid m_uiSpotlightGUID;  ///< 聚光灯NPC的GUID

        uint32 TalkCount;              ///< 已完成的对话计数
        uint32 TalkTimer;              ///< 对话计时器
        uint32 WipeTimer;              ///< 团灭检测计时器
        uint32 m_uiEventId;            ///< 当前歌剧事件ID

        bool PerformanceReady;         ///< 表演是否就绪
        bool RaidWiped;                ///< 团队是否已团灭

        /**
         * @brief 重置NPC状态
         * @details 重置对话计数和状态,获取当前歌剧事件类型
         */
        void Reset() override
        {
            Initialize();

            m_uiEventId = instance->GetData(DATA_OPERA_PERFORMANCE);
        }

        /**
         * @brief 启动歌剧事件
         * @details 设置Boss状态为进行中,重置死亡计数,开始护送路径
         */
        void StartEvent()
        {
            instance->SetBossState(DATA_OPERA_PERFORMANCE, IN_PROGRESS);

            // 重置绿野仙踪事件的死亡计数(如果是该事件)
            if (m_uiEventId == EVENT_OZ)
                instance->SetData(DATA_OPERA_OZ_DEATHCOUNT, IN_PROGRESS);

            Start(false, false);
        }

        /**
         * @brief 进入战斗时调用(不执行任何操作)
         * @param who 仇恨目标
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 到达路径点时调用
         * @param waypointId 路径点ID
         * @param pathId 路径ID(未使用)
         * @details 处理巴恩斯在歌剧事件中的移动节点:
         *          - 路径点0: 穿上燕尾服,打开舞台门
         *          - 路径点4: 暂停移动,召唤聚光灯,开始表演开场白
         *          - 路径点8: 打开舞台门,标记表演就绪
         *          - 路径点9: 准备遭遇战,打开幕布
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 0:
                    // 穿上燕尾服并打开舞台门
                    DoCast(me, SPELL_TUXEDO, false);
                    instance->DoUseDoorOrButton(instance->GetGuidData(DATA_GO_STAGEDOORLEFT));
                    break;
                case 4:
                    // 到达舞台中央,准备开始表演
                    TalkCount = 0;
                    SetEscortPaused(true);

                    // 召唤聚光灯NPC
                    if (Creature* spotlight = me->SummonCreature(NPC_SPOTLIGHT,
                        me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0.0f,
                        TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 1min))
                    {
                        spotlight->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        spotlight->CastSpell(spotlight, SPELL_SPOTLIGHT, false);
                        m_uiSpotlightGUID = spotlight->GetGUID();
                    }
                    break;
                case 8:
                    // 打开舞台门,准备召唤Boss
                    instance->DoUseDoorOrButton(instance->GetGuidData(DATA_GO_STAGEDOORLEFT));
                    PerformanceReady = true;
                    break;
                case 9:
                    // 准备遭遇战并打开幕布
                    PrepareEncounter();
                    instance->DoUseDoorOrButton(instance->GetGuidData(DATA_GO_CURTAINS));
                    break;
            }
        }

        /**
         * @brief 执行对话
         * @param count 对话索引
         * @details 根据当前歌剧事件类型,播放对应的对话文本并设置下一次对话的延迟时间
         */
        void Talk(uint32 count)
        {
            int32 text = 0;

            switch (m_uiEventId)
            {
                case EVENT_OZ:  ///< 绿野仙踪
                    if (OzDialogue[count].textid)
                         text = OzDialogue[count].textid;
                    if (OzDialogue[count].timer)
                        TalkTimer = OzDialogue[count].timer;
                    break;

                case EVENT_HOOD:  ///< 小红帽
                    if (HoodDialogue[count].textid)
                        text = HoodDialogue[count].textid;
                    if (HoodDialogue[count].timer)
                        TalkTimer = HoodDialogue[count].timer;
                    break;

                case EVENT_RAJ:  ///< 罗密欧与朱丽叶
                     if (RAJDialogue[count].textid)
                         text = RAJDialogue[count].textid;
                    if (RAJDialogue[count].timer)
                        TalkTimer = RAJDialogue[count].timer;
                    break;
            }

            if (text)
                 CreatureAI::Talk(text);
        }

        /**
         * @brief 准备遭遇战
         * @details 根据歌剧事件类型召唤对应的Boss:
         *          - 绿野仙踪: 召唤桃乐丝、狮子、铁皮人、稻草人(4个)
         *          - 小红帽: 召唤外婆(1个)
         *          - 罗密欧与朱丽叶: 召唤朱丽叶(1个)
         *          召唤的Boss初始为不可攻击状态
         */
        void PrepareEncounter()
        {
            TC_LOG_DEBUG("scripts", "Barnes Opera Event - Introduction complete - preparing encounter {}", m_uiEventId);
            uint8 index = 0;
            uint8 count = 0;

            switch (m_uiEventId)
            {
                case EVENT_OZ:  ///< 绿野仙踪 - 4个Boss
                    index = 0;
                    count = 4;
                    break;
                case EVENT_HOOD:  ///< 小红帽 - 1个Boss
                    index = 4;
                    count = index+1;
                    break;
                case EVENT_RAJ:  ///< 罗密欧与朱丽叶 - 1个Boss
                    index = 5;
                    count = index+1;
                    break;
            }

            // 召唤对应的Boss
            for (; index < count; ++index)
            {
                uint32 entry = ((uint32)Spawns[index][0]);
                float PosX = Spawns[index][1];

                if (Creature* creature = me->SummonCreature(entry, PosX, SPAWN_Y, SPAWN_Z, SPAWN_O, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2h))
                    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  ///< 初始不可攻击
            }

            RaidWiped = false;
        }

        /**
         * @brief AI更新函数
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 管理对话序列和团灭检测:
         *          1. 处理护送AI的更新
         *          2. 当护送暂停时,按时间间隔播放对话
         *          3. 当表演就绪后,定期检查团队存活状态
         */
        void UpdateAI(uint32 diff) override
        {
            EscortAI::UpdateAI(diff);

            // 处理对话序列
            if (HasEscortState(STATE_ESCORT_PAUSED))
            {
                if (TalkTimer <= diff)
                {
                    // 对话完成后,移除聚光灯并继续移动
                    if (TalkCount > 3)
                    {
                        if (Creature* pSpotlight = ObjectAccessor::GetCreature(*me, m_uiSpotlightGUID))
                            pSpotlight->DespawnOrUnsummon();

                        SetEscortPaused(false);
                        return;
                    }

                    Talk(TalkCount);
                    ++TalkCount;
                } else TalkTimer -= diff;
            }

            // 团灭检测逻辑
            if (PerformanceReady)
            {
                if (!RaidWiped)
                {
                    if (WipeTimer <= diff)
                    {
                        // 检查地图中所有玩家的存活状态
                        Map::PlayerList const& PlayerList = me->GetMap()->GetPlayers();
                        if (PlayerList.isEmpty())
                            return;

                        RaidWiped = true;
                        for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                        {
                            // 如果有存活的非GM玩家,则团队未团灭
                            if (i->GetSource()->IsAlive() && !i->GetSource()->IsGameMaster())
                            {
                                RaidWiped = false;
                                break;
                            }
                        }

                        // 如果团队团灭,进入规避模式
                        if (RaidWiped)
                        {
                            EnterEvadeMode();
                            return;
                        }

                        WipeTimer = 15000;  ///< 每15秒检查一次
                    } else WipeTimer -= diff;
                }
            }
        }

        /**
         * @brief 处理玩家选择菜单选项
         * @param player 玩家指针
         * @param menuId 菜单ID(未使用)
         * @param gossipListId 选项列表ID
         * @return 是否处理成功
         * @details 处理玩家与巴恩斯的对话选项:
         *          - 选项1: 显示第二个对话菜单
         *          - 选项2: 随机选择一个歌剧事件并启动
         *          - 选项3-5: GM调试选项,手动设置歌剧事件类型
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            switch (action)
            {
                case GOSSIP_ACTION_INFO_DEF + 1:
                    // 显示第二个对话菜单
                    InitGossipMenuFor(player, OZ_GOSSIP2_MID);
                    AddGossipItemFor(player, OZ_GOSSIP2_MID, OZ_GOSSIP2_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                    SendGossipMenuFor(player, 8971, me->GetGUID());
                    break;
                case GOSSIP_ACTION_INFO_DEF + 2:
                    // 随机选择歌剧事件并启动
                    CloseGossipMenuFor(player);
                    m_uiEventId = urand(EVENT_OZ, EVENT_RAJ);
                    StartEvent();
                    break;
                case GOSSIP_ACTION_INFO_DEF + 3:
                    // GM选项: 手动设置为绿野仙踪
                    CloseGossipMenuFor(player);
                    m_uiEventId = EVENT_OZ;
                    TC_LOG_DEBUG("scripts", "player ({}) manually set Opera event to EVENT_OZ", player->GetGUID().ToString());
                    break;
                case GOSSIP_ACTION_INFO_DEF + 4:
                    // GM选项: 手动设置为小红帽
                    CloseGossipMenuFor(player);
                    m_uiEventId = EVENT_HOOD;
                    TC_LOG_DEBUG("scripts", "player ({}) manually set Opera event to EVENT_HOOD", player->GetGUID().ToString());
                    break;
                case GOSSIP_ACTION_INFO_DEF + 5:
                    // GM选项: 手动设置为罗密欧与朱丽叶
                    CloseGossipMenuFor(player);
                    m_uiEventId = EVENT_RAJ;
                    TC_LOG_DEBUG("scripts", "player ({}) manually set Opera event to EVENT_RAJ", player->GetGUID().ToString());
                    break;
            }

            return true;
        }

        /**
         * @brief 处理玩家与NPC对话
         * @param player 玩家指针
         * @return 是否处理成功
         * @details 检查前置条件(莫罗斯已死亡,歌剧事件未完成),
         *          显示对话菜单,对GM玩家显示调试选项
         */
        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, OZ_GOSSIP1_MID);

            // 检查前置条件: 莫罗斯已死亡且歌剧事件未完成
            if (instance->GetBossState(DATA_MOROES) == DONE && instance->GetBossState(DATA_OPERA_PERFORMANCE) != DONE)
            {
                AddGossipItemFor(player, OZ_GOSSIP1_MID, OZ_GOSSIP1_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

                // 为GM玩家显示调试选项
                if (player->IsGameMaster())
                {
                    AddGossipItemFor(player, GOSSIP_ICON_DOT, OZ_GM_GOSSIP1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
                    AddGossipItemFor(player, GOSSIP_ICON_DOT, OZ_GM_GOSSIP2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
                    AddGossipItemFor(player, GOSSIP_ICON_DOT, OZ_GM_GOSSIP3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);
                }

                // 根据团队状态显示不同的对话文本
                if (!RaidWiped)
                    SendGossipMenuFor(player, 8970, me->GetGUID());
                else
                    SendGossipMenuFor(player, 8975, me->GetGUID());

                return true;
            }

            // 不满足条件时显示默认对话
            SendGossipMenuFor(player, 8978, me->GetGUID());
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_barnesAI>(creature);
    }
};

/*###
# npc_image_of_medivh
####*/

/**
 * @brief 麦迪文对话文本枚举
 * @details 定义麦迪文与阿卡纳戈斯对话中的文本ID
 */
enum
{
    SAY_DIALOG_MEDIVH_1             = 0,  ///< 麦迪文对话1
    SAY_DIALOG_ARCANAGOS_2          = 0,  ///< 阿卡纳戈斯对话2
    SAY_DIALOG_MEDIVH_3             = 1,  ///< 麦迪文对话3
    SAY_DIALOG_ARCANAGOS_4          = 1,  ///< 阿卡纳戈斯对话4
    SAY_DIALOG_MEDIVH_5             = 2,  ///< 麦迪文对话5
    SAY_DIALOG_ARCANAGOS_6          = 2,  ///< 阿卡纳戈斯对话6
    EMOTE_DIALOG_MEDIVH_7           = 3,  ///< 麦迪文表情7
    SAY_DIALOG_ARCANAGOS_8          = 3,  ///< 阿卡纳戈斯对话8
    SAY_DIALOG_MEDIVH_9             = 4   ///< 麦迪文对话9
};

/**
 * @brief 麦迪文的位置坐标
 * @details 格式: {X, Y, Z, 朝向}
 */
static float MedivPos[4] = {-11161.49f, -1902.24f, 91.48f, 1.94f};

/**
 * @brief 阿卡纳戈斯的位置坐标
 * @details 格式: {X, Y, Z, 朝向}
 */
static float ArcanagosPos[4] = {-11169.75f, -1881.48f, 95.39f, 4.83f};

/**
 * @class npc_image_of_medivh
 * @brief 麦迪文影像NPC脚本类
 * @details 管理麦迪文影像的AI,用于任务9645(麦迪文的日记)
 *          展示麦迪文与阿卡纳戈斯之间的对话和战斗动画
 */
class npc_image_of_medivh : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_image_of_medivh() : CreatureScript("npc_image_of_medivh") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_image_of_medivhAI>(creature);
    }

    /**
     * @struct npc_image_of_medivhAI
     * @brief 麦迪文影像AI结构体
     * @details 实现麦迪文影像的事件序列,包括对话、战斗动画和任务完成
     */
    struct npc_image_of_medivhAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_image_of_medivhAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            Step = 0;
            FireArcanagosTimer = 0;
            FireMedivhTimer = 0;
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            ArcanagosGUID.Clear();
            EventStarted = false;
            YellTimer = 0;
        }

        InstanceScript* instance;      ///< 副本实例脚本指针

        ObjectGuid ArcanagosGUID;      ///< 阿卡纳戈斯的GUID

        uint32 YellTimer;              ///< 对话计时器
        uint32 Step;                   ///< 当前步骤
        uint32 FireMedivhTimer;        ///< 麦迪文火球计时器
        uint32 FireArcanagosTimer;     ///< 阿卡纳戈斯火球计时器

        bool EventStarted;             ///< 事件是否已开始

        /**
         * @brief 重置NPC状态
         * @details 设置不可攻击状态,移动到初始位置,
         *          如果已存在影像实例则消失
         */
        void Reset() override
        {
            Initialize();
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);

            // 确保只有一个麦迪文影像存在
            if (instance->GetGuidData(DATA_IMAGE_OF_MEDIVH).IsEmpty())
            {
                instance->SetGuidData(DATA_IMAGE_OF_MEDIVH, me->GetGUID());
                (*me).GetMotionMaster()->MovePoint(1, MedivPos[0], MedivPos[1], MedivPos[2]);
                Step = 0;
            }
            else
            {
                me->DespawnOrUnsummon();
            }
        }

        /**
         * @brief 进入战斗时调用(不执行任何操作)
         * @param who 仇恨目标
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 移动完成通知
         * @param type 移动类型
         * @param id 移动点ID
         * @details 当移动到初始位置后启动事件
         */
        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;
            if (id == 1)
            {
                StartEvent();
                me->SetOrientation(MedivPos[3]);
                me->SetOrientation(MedivPos[3]);
            }
        }

        /**
         * @brief 启动事件
         * @details 召唤阿卡纳戈斯并开始对话序列
         */
        void StartEvent()
        {
            Step = 1;
            EventStarted = true;

            // 召唤阿卡纳戈斯(蓝龙)
            Creature* Arcanagos = me->SummonCreature(NPC_ARCANAGOS, ArcanagosPos[0], ArcanagosPos[1], ArcanagosPos[2], 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 20s);
            if (!Arcanagos)
                return;

            ArcanagosGUID = Arcanagos->GetGUID();
            Arcanagos->SetDisableGravity(true);
            Arcanagos->GetMotionMaster()->MovePoint(0, ArcanagosPos[0], ArcanagosPos[1], ArcanagosPos[2]);
            Arcanagos->SetOrientation(ArcanagosPos[3]);
            me->SetOrientation(MedivPos[3]);
            YellTimer = 10000;  ///< 10秒后开始对话
        }

        /**
         * @brief 执行下一步事件
         * @param step 步骤编号
         * @return 返回延迟时间(毫秒)
         * @details 执行对话和动画序列:
         *          - 步骤0: 空闲状态
         *          - 步骤1-6: 麦迪文与阿卡纳戈斯的对话
         *          - 步骤7-12: 战斗动画
         *          - 步骤13-15: 结束动画和任务完成
         */
        uint32 NextStep(uint32 step)
        {
            switch (step)
            {
            case 0: return 9999999;  ///< 空闲状态
            case 1:
                // 麦迪文开始对话
                Talk(SAY_DIALOG_MEDIVH_1);
                return 10000;
            case 2:
                // 阿卡纳戈斯回应
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    arca->AI()->Talk(SAY_DIALOG_ARCANAGOS_2);
                return 20000;
            case 3:
                // 麦迪文继续对话
                Talk(SAY_DIALOG_MEDIVH_3);
                return 10000;
            case 4:
                // 阿卡纳戈斯警告
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    arca->AI()->Talk(SAY_DIALOG_ARCANAGOS_4);
                return 20000;
            case 5:
                // 麦迪文变得愤怒
                Talk(SAY_DIALOG_MEDIVH_5);
                return 20000;
            case 6:
                // 阿卡纳戈斯最后的警告
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    arca->AI()->Talk(SAY_DIALOG_ARCANAGOS_6);
                return 10000;
            case 7:
                // 开始战斗 - 阿卡纳戈斯开始施放火球
                FireArcanagosTimer = 500;
                return 5000;
            case 8:
                // 麦迪文开启法力护盾并开始反击
                FireMedivhTimer = 500;
                DoCast(me, SPELL_MANA_SHIELD);
                return 10000;
            case 9:
                // 麦迪文吟唱终极法术
                Talk(EMOTE_DIALOG_MEDIVH_7);
                return 10000;
            case 10:
                // 麦迪文对阿卡纳戈斯施放爆燃冲击
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    DoCast(arca, SPELL_CONFLAGRATION_BLAST, false);
                return 1000;
            case 11:
                // 阿卡纳戈斯尖叫并试图逃离
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    arca->AI()->Talk(SAY_DIALOG_ARCANAGOS_8);
                return 5000;
            case 12:
                // 阿卡纳戈斯飞离并燃烧
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                {
                    arca->GetMotionMaster()->MovePoint(0, -11010.82f, -1761.18f, 156.47f);
                    arca->setActive(true);
                    arca->SetFarVisible(true);
                    arca->InterruptNonMeleeSpells(true);
                    arca->SetSpeedRate(MOVE_FLIGHT, 2.0f);
                }
                return 10000;
            case 13:
                // 麦迪文最后的话语
                Talk(SAY_DIALOG_MEDIVH_9);
                return 10000;
            case 14:
            {
                // 事件结束,完成玩家任务
                me->SetVisible(false);
                me->ClearInCombat();

                // 检查所有存活的玩家,如果任务9645未完成则标记完成
                InstanceMap::PlayerList const& PlayerList = me->GetMap()->GetPlayers();
                for (InstanceMap::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                {
                    if (i->GetSource()->IsAlive())
                    {
                        if (i->GetSource()->GetQuestStatus(9645) == QUEST_STATUS_INCOMPLETE)
                            i->GetSource()->CompleteQuest(9645);
                    }
                }
                return 50000;
            }
            case 15:
                // 阿卡纳戈斯死亡
                if (Creature* arca = ObjectAccessor::GetCreature(*me, ArcanagosGUID))
                    arca->KillSelf();
                return 5000;
            default:
                return 9999999;
            }
        }

        /**
         * @brief AI更新函数
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 管理对话序列和战斗动画:
         *          1. 按时触发下一步事件
         *          2. 在战斗阶段(步骤7-12)管理双方的火球施放
         */
        void UpdateAI(uint32 diff) override
        {
            // 管理对话和事件序列
            if (YellTimer <= diff)
            {
                if (EventStarted)
                    YellTimer = NextStep(Step++);
            } else YellTimer -= diff;

            // 在战斗阶段管理火球施放
            if (Step >= 7 && Step <= 12)
            {
                Unit* arca = ObjectAccessor::GetUnit(*me, ArcanagosGUID);

                // 阿卡纳戈斯的火球攻击
                if (FireArcanagosTimer <= diff)
                {
                    if (arca)
                        arca->CastSpell(me, SPELL_FIRE_BALL, false);
                    FireArcanagosTimer = 6000;
                } else FireArcanagosTimer -= diff;

                // 麦迪文的火球反击
                if (FireMedivhTimer <= diff)
                {
                    if (arca)
                        DoCast(arca, SPELL_FIRE_BALL);
                    FireMedivhTimer = 5000;
                } else FireMedivhTimer -= diff;
            }
        }
    };
};

/**
 * @brief 注册卡拉赞辅助NPC脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建巴恩斯和麦迪文影像的NPC脚本，注册到脚本系统
 */
void AddSC_karazhan()
{
    new npc_barnes();
    new npc_image_of_medivh();
}
