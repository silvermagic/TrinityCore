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
 * @file chapter2.cpp
 * @brief 血色飞地第二章：血色突围任务脚本
 *
 * 本模块实现了死亡骑士新手区域（阿彻鲁斯）的第二章任务内容，包括：
 * - 血色突围任务链（任务ID: 12727）
 * - 血色信使事件
 * - 特别处决任务（任务ID: 12739-12750）
 * - 吞噬人形生物法术（法术ID: 53110）
 *
 * 主要NPC交互：
 * - 科尔蒂拉·织亡者（NPC 28912）：血色突围任务关键NPC
 * - 瓦尔罗斯（NPC 29001）：血色突击军指挥官
 * - 血色信使（NPC 29076）：可疑的树木任务
 * - 特别处决目标（NPC 29032-29074）：各种族的处决对象
 */

#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "SpellScript.h"

/**
 * @brief 血色突围任务文本枚举
 *
 * 定义科尔蒂拉和瓦尔罗斯在任务过程中的台词ID。
 */
enum BloodyBreakoutTexts
{
    SAY_KOLTIRA_0 = 0,   ///< 科尔蒂拉台词 0：任务开始
    SAY_KOLTIRA_1 = 1,   ///< 科尔蒂拉台词 1：站起来
    SAY_KOLTIRA_2 = 2,   ///< 科尔蒂拉台词 2：施放反魔法领域
    SAY_KOLTIRA_3 = 3,   ///< 科尔蒂拉台词 3：准备防御
    SAY_KOLTIRA_4 = 4,   ///< 科尔蒂拉台词 4：第二波警告
    SAY_KOLTIRA_5 = 5,   ///< 科尔蒂拉台词 5：第三波警告
    SAY_KOLTIRA_6 = 6,   ///< 科尔蒂拉台词 6：瓦尔罗斯出现
    SAY_KOLTIRA_7 = 7,   ///< 科尔蒂拉台词 7：战斗建议
    SAY_KOLTIRA_8 = 8,   ///< 科尔蒂拉台词 8：胜利感言
    SAY_KOLTIRA_9 = 9,   ///< 科尔蒂拉台词 9：后续剧情
    SAY_KOLTIRA_10 = 10, ///< 科尔蒂拉台词 10：离开

    SAY_VALROTH_0 = 0,   ///< 瓦尔罗斯台词 0：看到科尔蒂拉
    SAY_VALROTH_1 = 1,   ///< 瓦尔罗斯台词 1：第二波攻击
    SAY_VALROTH_2 = 2,   ///< 瓦尔罗斯台词 2：第三波攻击
    SAY_VALROTH_3 = 3,   ///< 瓦尔罗斯台词 3：亲自出场

    TEXT_ID_EVENT = 13425  ///< 事件八卦菜单文本ID
};

/**
 * @brief 血色突围任务事件枚举
 *
 * 定义任务流程中各个阶段的事件ID，用于事件调度系统。
 */
enum BloodyBreakoutEvents
{
    EVENT_INTRO_0           = 1,  ///< 介绍阶段 0：坐下
    EVENT_INTRO_1           = 2,  ///< 介绍阶段 1：站起来
    EVENT_INTRO_2           = 3,  ///< 介绍阶段 2：跳跃
    EVENT_INTRO_3           = 4,  ///< 介绍阶段 3：移动到防御点
    EVENT_INTRO_4           = 5,  ///< 介绍阶段 4：变身和装备
    EVENT_INTRO_5           = 6,  ///< 介绍阶段 5：施放反魔法领域
    EVENT_INTRO_6           = 7,  ///< 介绍阶段 6：准备完成

    EVENT_SPAWN_WAVE_1      = 8,  ///< 刷出第一波敌人
    EVENT_SPAWN_WAVE_2      = 9,  ///< 刷出第二波敌人
    EVENT_SPAWN_WAVE_3      = 10, ///< 刷出第三波敌人
    EVENT_SPAWN_VALROTH     = 11, ///< 刷出瓦尔罗斯

    EVENT_KOLTIRA_ADVICE    = 12, ///< 科尔蒂拉战斗建议
    EVENT_OUTRO_1           = 13, ///< 结束阶段 1：移除反魔法领域
    EVENT_OUTRO_2           = 14, ///< 结束阶段 2：对话
    EVENT_OUTRO_3           = 15, ///< 结束阶段 3：对话
    EVENT_OUTRO_4           = 16, ///< 结束阶段 4：离开

    EVENT_CHECK_PLAYER      = 17  ///< 检查玩家状态（是否死亡或离开）
};

/**
 * @brief 血色突围任务常量枚举
 *
 * 定义任务相关的NPC ID、法术ID、路径点ID等常量。
 */
enum BloodyBreakout
{
    POINT_ID_1                  = 1,      ///< 路径点ID 1
    POINT_ID_2                  = 2,      ///< 路径点ID 2
    POINT_ID_6                  = 6,      ///< 路径点ID 6
    POINT_ID_10                 = 10,     ///< 路径点ID 10

    SUMMON_ACOLYTES_0           = 0,      ///< 召唤组ID：第一波侍从
    SUMMON_ACOLYTES_1           = 1,      ///< 召唤组ID：第二波侍从
    SUMMON_ACOLYTES_2           = 2,      ///< 召唤组ID：第三波侍从
    SUMMON_VALROTH              = 3,      ///< 召唤组ID：瓦尔罗斯

    QUEST_BLOODY_BREAKOUT       = 12727,  ///< 血色突围任务ID

    NPC_FAKE_VALROTH            = 29011,  ///< 假瓦尔罗斯（用于对话）
    NPC_VALROTH                 = 29001,  ///< 真瓦尔罗斯（战斗NPC）
    NPC_ACOLYTE                 = 29007,  ///< 血色侍从
    NPC_KOLTIRA                 = 28912,  ///< 科尔蒂拉·织亡者
    NPC_KOLTIRA_MOUNT           = 25445,  ///< 科尔蒂拉的坐骑

    SPELL_KOLTIRA_TRANSFORM     = 52899,  ///< 科尔蒂拉变身法术
    SPELL_ANTI_MAGIC_ZONE       = 52894,  ///< 反魔法领域法术
    SPELL_HERO_AGGRO            = 53627   ///< 英雄仇恨法术
};

/**
 * @brief 科尔蒂拉的移动路径坐标
 *
 * 定义科尔蒂拉在任务过程中的关键位置点：
 * [0] - 初始跳跃落点
 * [1] - 武器架位置
 * [2] - 防御位置
 */
Position const koltiraPos[3] =
{
    { 1653.36f, -6038.34f, 127.584f },    ///< 跳跃落点
    { 1653.765f, -6035.075f, 127.5844f }, ///< 武器架位置
    { 1651.89f, -6037.101f, 127.5844f }   ///< 防御位置
};

/**
 * @struct npc_koltira_deathweaver
 * @brief 科尔蒂拉·织亡者 NPC AI
 *
 * 实现血色突围任务的核心NPC - 科尔蒂拉的行为逻辑。
 * 科尔蒂拉是死亡骑士组织的一员，被血色十字军俘虏，
 * 玩家需要帮助他击败血色十字军的攻击。
 *
 * 任务流程：
 * 1. 玩家接受任务后，科尔蒂拉站起来并准备防御
 * 2. 分三波刷出血色侍从攻击玩家
 * 3. 最后刷出瓦尔罗斯亲自攻击
 * 4. 击败瓦尔罗斯后完成任务
 * 5. 科尔蒂拉感谢玩家并离开
 */
struct npc_koltira_deathweaver : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC生物对象指针
     *
     * 初始化召唤列表和事件八卦标志。
     */
    npc_koltira_deathweaver(Creature* creature) : ScriptedAI(creature), _summons(me)
    {
        _eventGossip = false;
    }

    /**
     * @brief 八卦菜单交互
     * @param player 交互的玩家指针
     * @return true表示拦截默认交互，false表示继续默认交互
     *
     * 根据任务状态提供不同的八卦菜单内容：
     * - 如果任务进行中，显示事件特定的八卦文本
     * - 否则显示默认的NPC对话或任务菜单
     */
    bool OnGossipHello(Player* player) override
    {
        ObjectGuid const guid = me->GetGUID();
        _playerGUID = player->GetGUID();

        if (me->IsQuestGiver())
            player->PrepareQuestMenu(guid);

        // 覆盖默认八卦菜单
        if (_eventGossip)
        {
            SendGossipMenuFor(player, TEXT_ID_EVENT, guid);
            return true;
        }

        return false;
    }

    /**
     * @brief 任务接受事件
     * @param player 接受任务的玩家指针
     * @param quest 被接受的任务对象指针
     *
     * 当玩家接受血色突围任务时，启动任务事件流程。
     * 安排介绍阶段的第一个事件和玩家检查事件。
     */
    void OnQuestAccept(Player* /* player */, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_BLOODY_BREAKOUT)
        {
            _events.ScheduleEvent(EVENT_INTRO_0, 500ms);
            _events.ScheduleEvent(EVENT_CHECK_PLAYER, 5s);
        }
    }

    /**
     * @brief 重置AI状态
     *
     * 将科尔蒂拉重置到初始状态：
     * - 设置免疫NPC攻击标志
     * - 设置可交互标志
     * - 设置死亡姿态（假装死亡）
     * - 移除所有光环
     * - 清空事件和召唤列表
     */
    void Reset() override
    {
        me->SetUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
        me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        me->SetStandState(UNIT_STAND_STATE_DEAD);
        me->RemoveAllAuras();

        _events.Reset();
        _summons.DespawnAll();
        _playerGUID.Clear();
        _eventGossip = false;
    }

    /**
     * @brief 让假瓦尔罗斯说话
     * @param id 台词ID
     *
     * 查找附近的假瓦尔罗斯NPC并让它说出指定台词。
     * 假瓦尔罗斯用于在战斗前进行对话。
     */
    void FakeValrothTalk(uint32 id)
    {
        if (Creature* fakeValroth = me->FindNearestCreature(NPC_FAKE_VALROTH, INSPECT_DISTANCE * 2))
            fakeValroth->AI()->Talk(id);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主循环函数，处理任务流程中的所有事件。
     *
     * 事件流程分为四个阶段：
     * 1. 介绍阶段（EVENT_INTRO_0-6）：科尔蒂拉站起来，准备防御
     * 2. 战斗阶段（EVENT_SPAWN_WAVE_1-VALROTH）：分波刷出敌人
     * 3. 战斗建议阶段（EVENT_KOLTIRA_ADVICE）：科尔蒂拉提供战斗提示
     * 4. 结束阶段（EVENT_OUTRO_1-4）：任务完成，科尔蒂拉离开
     *
     * 另外还有周期性的玩家检查事件，确保玩家没有死亡或离开。
     *
     * @note 每帧调用，性能关键函数
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INTRO_0:
                    // 介绍阶段0：从假装死亡变为坐姿
                    me->SetStandState(UNIT_STAND_STATE_SIT);
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    Talk(SAY_KOLTIRA_0);

                    _events.ScheduleEvent(EVENT_INTRO_1, 5s);
                    break;
                case EVENT_INTRO_1:
                    // 介绍阶段1：站起来
                    me->SetStandState(UNIT_STAND_STATE_STAND);
                    Talk(SAY_KOLTIRA_1);

                    _events.ScheduleEvent(EVENT_INTRO_2, 2s);
                    break;
                case EVENT_INTRO_2:
                    // 介绍阶段2：跳到中间位置
                    me->GetMotionMaster()->MoveJump(koltiraPos[0], 25.0f, 15.0f);

                    _events.ScheduleEvent(EVENT_INTRO_3, 2s);
                    break;

                case EVENT_INTRO_3:
                    // 介绍阶段3：走到武器架
                    me->SetWalk(true);
                    me->GetMotionMaster()->MovePoint(POINT_ID_1, koltiraPos[1]);

                    break;
                case EVENT_INTRO_4:
                    // 介绍阶段4：变身并装备武器，移动到防御位置
                    DoCastSelf(SPELL_KOLTIRA_TRANSFORM);
                    me->LoadEquipment(POINT_ID_1);
                    me->SetStandState(UNIT_STAND_STATE_STAND);
                    me->GetMotionMaster()->MovePoint(POINT_ID_2, koltiraPos[2], true, 3.839724f);

                    break;
                case EVENT_INTRO_5:
                    // 介绍阶段5：施放反魔法领域
                    Talk(SAY_KOLTIRA_2);
                    DoCastSelf(SPELL_ANTI_MAGIC_ZONE);

                    _events.ScheduleEvent(EVENT_INTRO_6, 4s);
                    break;
                case EVENT_INTRO_6:
                    // 介绍阶段6：跪下并准备防御
                    Talk(SAY_KOLTIRA_3);
                    me->SetStandState(UNIT_STAND_STATE_KNEEL);
                    me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    _eventGossip = true;

                    break;
                case EVENT_SPAWN_WAVE_1:
                    // 刷出第一波血色侍从
                    me->SummonCreatureGroup(SUMMON_ACOLYTES_0);

                    _events.ScheduleEvent(EVENT_SPAWN_WAVE_2, 29s);
                    break;
                case EVENT_SPAWN_WAVE_2:
                    // 刷出第二波血色侍从
                    Talk(SAY_KOLTIRA_4);
                    FakeValrothTalk(SAY_VALROTH_1);
                    me->SummonCreatureGroup(SUMMON_ACOLYTES_1);

                    _events.ScheduleEvent(EVENT_SPAWN_WAVE_3, 21s);
                    break;
                case EVENT_SPAWN_WAVE_3:
                    // 刷出第三波血色侍从
                    Talk(SAY_KOLTIRA_5);
                    FakeValrothTalk(SAY_VALROTH_2);
                    me->SummonCreatureGroup(SUMMON_ACOLYTES_2);

                    _events.ScheduleEvent(EVENT_SPAWN_VALROTH, 24s);
                    break;
                case EVENT_SPAWN_VALROTH:
                    // 刷出瓦尔罗斯
                    Talk(SAY_KOLTIRA_6);
                    FakeValrothTalk(SAY_VALROTH_3);
                    me->SummonCreatureGroup(SUMMON_VALROTH);

                    _events.ScheduleEvent(EVENT_KOLTIRA_ADVICE, 8s, 16s);
                    break;
                case EVENT_KOLTIRA_ADVICE:
                    // 战斗建议：如果瓦尔罗斯还在，给出战斗提示
                    if (_summons.HasEntry(NPC_VALROTH))
                        Talk(SAY_KOLTIRA_7);

                    break;
                case EVENT_OUTRO_1:
                    // 结束阶段1：移除反魔法领域，站起来
                    me->RemoveAurasDueToSpell(SPELL_ANTI_MAGIC_ZONE);
                    me->SetStandState(UNIT_STAND_STATE_STAND);
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    Talk(SAY_KOLTIRA_8);

                    _events.ScheduleEvent(EVENT_OUTRO_2, 7s);
                    _events.CancelEvent(EVENT_CHECK_PLAYER);
                    break;
                case EVENT_OUTRO_2:
                    // 结束阶段2：对话
                    Talk(SAY_KOLTIRA_9);

                    _events.ScheduleEvent(EVENT_OUTRO_3, 4s);
                    break;
                case EVENT_OUTRO_3:
                    // 结束阶段3：对话
                    Talk(SAY_KOLTIRA_10);

                    _events.ScheduleEvent(EVENT_OUTRO_4, 3s);
                    break;
                case EVENT_OUTRO_4:
                    // 结束阶段4：上坐骑离开
                    me->SetWalk(false);
                    me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);
                    DoCastSelf(SPELL_HERO_AGGRO);
                    me->GetMotionMaster()->MovePath(NPC_KOLTIRA, false);

                    break;
                case EVENT_CHECK_PLAYER:
                    // 周期性检查玩家状态
                    if (!_playerGUID)
                        return;

                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGUID))
                    {
                        // 如果玩家死亡或离开太远，任务失败
                        if (!player->IsAlive() || !player->IsWithinDist(me, INTERACTION_DISTANCE * 6))
                        {
                            _summons.DespawnAll();
                            me->DespawnOrUnsummon(1s);
                            player->FailQuest(QUEST_BLOODY_BREAKOUT);
                        }
                    }

                    _events.ScheduleEvent(EVENT_CHECK_PLAYER, 5s);
                    break;
            }
        }
    }

    /**
     * @brief 移动完成通知
     * @param type 移动类型
     * @param pointId 移动点ID
     *
     * 当科尔蒂拉移动到指定位置后触发相应的事件。
     * 主要处理两个关键路径点：
     * - POINT_ID_1：到达武器架，跪下，瓦尔罗斯出现
     * - POINT_ID_2：到达防御位置，准备防御
     * - POINT_ID_6：路径结束，上坐骑
     */
    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == POINT_MOTION_TYPE)
        {
            if (pointId == POINT_ID_1)
            {
                // 到达武器架：跪下，瓦尔罗斯说话，刷出第一波敌人
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
                FakeValrothTalk(SAY_VALROTH_0);

                _events.ScheduleEvent(EVENT_SPAWN_WAVE_1, 1s);
                _events.ScheduleEvent(EVENT_INTRO_4, 3s);
            }
            else if (pointId == POINT_ID_2)
                // 到达防御位置：准备施放反魔法领域
                _events.ScheduleEvent(EVENT_INTRO_5, 1s);
        }
        else
        {
            if (pointId == POINT_ID_6)
                // 离开路径：上坐骑
                me->Mount(NPC_KOLTIRA_MOUNT);
        }
    }

    /**
     * @brief 召唤生物事件
     * @param summon 被召唤的生物指针
     *
     * 将召唤的生物添加到召唤列表中进行管理。
     */
    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
    }

    /**
     * @brief 召唤生物消失事件
     * @param summon 消失的生物指针
     *
     * 当瓦尔罗斯消失（被击败）时，触发结束阶段。
     * 从召唤列表中移除该生物。
     */
    void SummonedCreatureDespawn(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_VALROTH)
            _events.ScheduleEvent(EVENT_OUTRO_1, 1s);

        _summons.Despawn(summon);
    }

private:
    EventMap _events;           ///< 事件映射表
    SummonList _summons;        ///< 召唤列表
    ObjectGuid _playerGUID;     ///< 玩家GUID

    bool _eventGossip;          ///< 事件八卦标志（控制八卦菜单内容）
};

/**
 * @brief 血色信使相关枚举定义
 *
 * 血色信使是"可疑的树木"任务中的目标NPC，
 * 玩家需要伪装成树木来伏击信使。
 */
enum ScarletCourierEnum
{
    SAY_TREE1                          = 0,      ///< 台词1：看到树木
    SAY_TREE2                          = 1,      ///< 台词2：进入战斗
    SPELL_SHOOT                        = 52818,  ///< 射击法术
    GO_INCONSPICUOUS_TREE              = 191144, ///< 不显眼的树木游戏对象ID
    NPC_SCARLET_COURIER                = 29076   ///< 血色信使NPC ID
};

/**
 * @class npc_scarlet_courier
 * @brief 血色信使NPC脚本
 *
 * 实现血色信使的行为逻辑。信使会骑着马巡逻，
 * 当看到玩家伪装的树木时会停下来查看，然后发起攻击。
 */
class npc_scarlet_courier : public CreatureScript
{
public:
    npc_scarlet_courier() : CreatureScript("npc_scarlet_courier") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_scarlet_courierAI(creature);
    }

    /**
     * @struct npc_scarlet_courierAI
     * @brief 血色信使AI实现
     *
     * 信使的行为分为两个阶段：
     * 1. 阶段1：发现树木，走过去查看
     * 2. 阶段2：攻击伪装成树木的玩家
     */
    struct npc_scarlet_courierAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature NPC生物对象指针
         */
        npc_scarlet_courierAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化状态变量
         */
        void Initialize()
        {
            uiStage = 1;
            uiStage_timer = 3000;
        }

        uint32 uiStage;         ///< 当前阶段（1=前往树木，2=攻击）
        uint32 uiStage_timer;   ///< 阶段计时器（毫秒）

        /**
         * @brief 重置AI状态
         *
         * 上马并重置阶段状态。
         */
        void Reset() override
        {
            me->Mount(14338); // 坐骑ID
            Initialize();
        }

        /**
         * @brief 进入战斗事件
         * @param who 攻击者单位指针
         *
         * 下马并进入战斗。
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_TREE2);
            me->Dismount();
            uiStage = 0;
        }

        /**
         * @brief 移动完成通知
         * @param type 移动类型
         * @param id 移动点ID
         *
         * 到达树木位置后，切换到攻击阶段。
         */
        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (id == 1)
                uiStage = 2;
        }

        /**
         * @brief 更新AI状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 处理非战斗状态下的阶段逻辑：
         * - 阶段1：发现树木，走过去查看
         * - 阶段2：攻击树木的主人（玩家）
         */
        void UpdateAI(uint32 diff) override
        {
            if (uiStage && !me->IsInCombat())
            {
                if (uiStage_timer <= diff)
                {
                    switch (uiStage)
                    {
                    case 1:
                        // 发现树木，走过去查看
                        me->SetWalk(true);
                        if (GameObject* tree = me->FindNearestGameObject(GO_INCONSPICUOUS_TREE, 40.0f))
                        {
                            Talk(SAY_TREE1);
                            float x, y, z;
                            tree->GetContactPoint(me, x, y, z);
                            me->GetMotionMaster()->MovePoint(1, x, y, z);
                        }
                        break;
                    case 2:
                        // 到达树木，攻击伪装的玩家
                        if (GameObject* tree = me->FindNearestGameObject(GO_INCONSPICUOUS_TREE, 40.0f))
                            if (Unit* unit = tree->GetOwner())
                                AttackStart(unit);
                        break;
                    }
                    uiStage_timer = 3000;
                    uiStage = 0;
                } else uiStage_timer -= diff;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @class npc_a_special_surprise
 * @brief 特别处决任务NPC脚本
 *
 * 实现死亡骑士新手区域中"特别处决"任务系列的目标NPC。
 * 这些NPC是玩家之前认识的人（根据玩家种族不同而不同），
 * 玩家需要处决他们以证明对巫妖王的忠诚。
 *
 * 任务ID范围：12739-12750
 * 涉及NPC：
 * - 29061：艾伦·斯坦布里奇（人类女性）
 * - 29072：库格·铁颚（兽人男性）
 * - 29067：多诺万·普尔弗罗斯特（矮人男性）
 * - 29065：亚兹米娜·奥克恩索恩（暗夜精灵女性）
 * - 29071：安托万·布拉克（人类男性）
 * - 29032：马拉尔·勇角（牛头人男性）
 * - 29068：戈比·爆弹海默（侏儒男性）
 * - 29073：伊吉·暗齿（巨魔男性）
 * - 29074：艾奥尼斯女士（亡灵女性）
 * - 29070：正义者瓦洛克（德莱尼男性）
 */
enum SpecialSurprise
{
    SAY_EXEC_START            = 0,  ///< 开始处决台词
    SAY_EXEC_PROG             = 1,  ///< 进展台词
    SAY_EXEC_NAME             = 2,  ///< 提到玩家名字
    SAY_EXEC_RECOG            = 3,  ///< 认出台词
    SAY_EXEC_NOREM            = 4,  ///< 无记忆台词
    SAY_EXEC_THINK            = 5,  ///< 思考台词
    SAY_EXEC_LISTEN           = 6,  ///< 倾听台词
    SAY_EXEC_TIME             = 7,  ///< 时间台词
    SAY_EXEC_WAITING          = 8,  ///< 等待台词
    EMOTE_DIES                = 9,  ///< 死亡表情

    SAY_PLAGUEFIST            = 0,  ///< 瘟疫之拳台词
    NPC_PLAGUEFIST            = 29053  ///< 瘟疫之拳NPC ID
};

class npc_a_special_surprise : public CreatureScript
{
public:
    npc_a_special_surprise() : CreatureScript("npc_a_special_surprise") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_a_special_surpriseAI(creature);
    }

    /**
     * @struct npc_a_special_surpriseAI
     * @brief 特别处决目标NPC AI
     *
     * 管理处决事件的对话流程，当玩家接近时触发一系列对话，
     * 最后玩家必须杀死NPC以完成任务。
     */
    struct npc_a_special_surpriseAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature NPC生物对象指针
         */
        npc_a_special_surpriseAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化状态变量
         */
        void Initialize()
        {
            ExecuteSpeech_Timer = 0;
            ExecuteSpeech_Counter = 0;
            PlayerGUID.Clear();
        }

        uint32 ExecuteSpeech_Timer;      ///< 对话计时器（毫秒）
        uint32 ExecuteSpeech_Counter;    ///< 对话计数器（当前对话步骤）
        ObjectGuid PlayerGUID;           ///< 玩家GUID

        /**
         * @brief 重置AI状态
         *
         * 设置为免疫玩家攻击状态，防止被意外攻击。
         */
        void Reset() override
        {
            Initialize();

            me->SetImmuneToPC(true);
        }

        /**
         * @brief 检查玩家是否满足任务条件
         * @param player 玩家指针
         * @return true表示满足条件，false表示不满足
         *
         * 根据NPC的ID检查玩家是否有对应的未完成任务。
         * 每个NPC对应一个特定的处决任务。
         */
        bool MeetQuestCondition(Player* player)
        {
            switch (me->GetEntry())
            {
                case 29061:  // 艾伦·斯坦布里奇
                    if (player->GetQuestStatus(12742) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29072:  // 库格·铁颚
                    if (player->GetQuestStatus(12748) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29067:  // 多诺万·普尔弗罗斯特
                    if (player->GetQuestStatus(12744) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29065:  // 亚兹米娜·奥克恩索恩
                    if (player->GetQuestStatus(12743) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29071:  // 安托万·布拉克
                    if (player->GetQuestStatus(12750) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29032:  // 马拉尔·勇角
                    if (player->GetQuestStatus(12739) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29068:  // 戈比·爆弹海默
                    if (player->GetQuestStatus(12745) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29073:  // 伊吉·暗齿
                    if (player->GetQuestStatus(12749) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29074:  // 艾奥尼斯女士
                    if (player->GetQuestStatus(12747) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
                case 29070:  // 正义者瓦洛克
                    if (player->GetQuestStatus(12746) == QUEST_STATUS_INCOMPLETE)
                        return true;
                    break;
            }

            return false;
        }

        /**
         * @brief 视线范围内检测
         * @param who 进入视线的单位指针
         *
         * 当玩家进入交互距离且有对应未完成任务时，记录玩家GUID。
         */
        void MoveInLineOfSight(Unit* who) override

        {
            if (PlayerGUID || who->GetTypeId() != TYPEID_PLAYER || !who->IsWithinDist(me, INTERACTION_DISTANCE))
                return;

            if (MeetQuestCondition(who->ToPlayer()))
                PlayerGUID = who->GetGUID();
        }

        /**
         * @brief 更新AI状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 执行处决事件的对话流程，共12个步骤：
         * 0-7：NPC与玩家的对话，逐渐揭示故事背景
         * 8：瘟疫之拳NPC的台词
         * 9：NPC跪下并取消免疫，允许玩家攻击
         * 10：最后的等待台词
         * 11：NPC死亡
         *
         * 对话间隔：前9步为7秒，后3步为15秒。
         */
        void UpdateAI(uint32 diff) override
        {
            if (PlayerGUID && !me->GetVictim() && me->IsAlive())
            {
                if (ExecuteSpeech_Timer <= diff)
                {
                    Player* player = ObjectAccessor::GetPlayer(*me, PlayerGUID);

                    if (!player)
                    {
                        Reset();
                        return;
                    }

                    switch (ExecuteSpeech_Counter)
                    {
                        case 0:
                            // 步骤0：开始对话
                            Talk(SAY_EXEC_START, player);
                            break;
                        case 1:
                            // 步骤1：站起来
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                            break;
                        case 2:
                            // 步骤2：进展台词
                            Talk(SAY_EXEC_PROG, player);
                            break;
                        case 3:
                            // 步骤3：提到玩家名字
                            Talk(SAY_EXEC_NAME, player);
                            break;
                        case 4:
                            // 步骤4：认出台词
                            Talk(SAY_EXEC_RECOG, player);
                            break;
                        case 5:
                            // 步骤5：无记忆台词
                            Talk(SAY_EXEC_NOREM, player);
                            break;
                        case 6:
                            // 步骤6：思考台词
                            Talk(SAY_EXEC_THINK, player);
                            break;
                        case 7:
                            // 步骤7：倾听台词
                            Talk(SAY_EXEC_LISTEN, player);
                            break;
                        case 8:
                            // 步骤8：瘟疫之拳NPC说话
                            if (Creature* Plaguefist = GetClosestCreatureWithEntry(me, NPC_PLAGUEFIST, 85.0f))
                                Plaguefist->AI()->Talk(SAY_PLAGUEFIST, player);
                            break;
                        case 9:
                            // 步骤9：跪下并取消免疫，允许玩家攻击
                            Talk(SAY_EXEC_TIME, player);
                            me->SetStandState(UNIT_STAND_STATE_KNEEL);
                            me->SetImmuneToPC(false);
                            break;
                        case 10:
                            // 步骤10：等待玩家处决
                            Talk(SAY_EXEC_WAITING, player);
                            break;
                        case 11:
                            // 步骤11：死亡
                            Talk(EMOTE_DIES);
                            me->setDeathState(JUST_DIED);
                            me->SetHealth(0);
                            return;
                    }

                    // 设置下一次对话的时间间隔
                    if (ExecuteSpeech_Counter >= 9)
                        ExecuteSpeech_Timer = 15000;  // 最后几步间隔更长
                    else
                        ExecuteSpeech_Timer = 7000;

                    ++ExecuteSpeech_Counter;
                }
                else
                    ExecuteSpeech_Timer -= diff;
            }
        }
    };
};

/**
 * @class spell_death_knight_devour_humanoid
 * @brief 吞噬人形生物法术脚本
 *
 * 法术ID: 53110
 * 死亡骑士技能，用于吞噬人形生物尸体以恢复生命值。
 * 触发目标对施法者施放效果法术。
 */
class spell_death_knight_devour_humanoid : public SpellScript
{
    PrepareSpellScript(spell_death_knight_devour_humanoid);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引（未使用）
     *
     * 让目标对施法者施放效果法术（由法术效果值决定）。
     */
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        GetHitUnit()->CastSpell(GetCaster(), GetEffectValue(), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_death_knight_devour_humanoid::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册血色飞地第二章脚本
 *
 * 注册所有与第二章任务相关的NPC和法术脚本。
 */
void AddSC_the_scarlet_enclave_c2()
{
    RegisterCreatureAI(npc_koltira_deathweaver);
    new npc_scarlet_courier();
    new npc_a_special_surprise();
    RegisterSpellScript(spell_death_knight_devour_humanoid);
}
