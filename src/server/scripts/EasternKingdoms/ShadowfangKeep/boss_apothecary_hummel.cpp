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
 * @file boss_apothecary_hummel.cpp
 * @brief 药剂师哈梅尔BOSS脚本 - 情人节事件
 *
 * 该模块实现情人节事件中影牙城堡的BOSS药剂师哈梅尔的战斗逻辑：
 * - 三药剂师团队战斗：哈梅尔、巴克斯特、弗莱
 * - 独特的死亡机制：哈梅尔假死直到两个助手死亡
 * - 香水/古龙水机制：多种药剂相关的AOE和地面效果
 * - 情人节小怪召唤机制
 *
 * @note 这是情人节季节性事件BOSS，通过日常任务触发
 * @note 任务ID：14488 "You've Been Served"（请君入瓮）
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "InstanceScript.h"
#include "LFGMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "shadowfang_keep.h"
#include "SpellScript.h"

/**
 * @brief 药剂师哈梅尔法术ID枚举
 *
 * 定义药剂师哈梅尔及其助手使用的所有法术ID
 */
enum ApothecarySpells
{
    SPELL_ALLURING_PERFUME       = 68589,  ///< 迷人香水光环 - 哈梅尔的被动光环
    SPELL_PERFUME_SPRAY          = 68607,  ///< 香水喷射 - 对当前目标造成伤害
    SPELL_CHAIN_REACTION         = 68821,  ///< 连锁反应 - 主要AOE技能
    SPELL_SUMMON_TABLE           = 69218,  ///< 召唤实验桌 - 用于连锁反应
    SPELL_PERMANENT_FEIGN_DEATH  = 29266,  ///< 永久假死 - 哈梅尔假死效果
    SPELL_QUIET_SUICIDE          = 3617,   ///< 安静自杀 - 哈梅尔真实死亡
    SPELL_COLOGNE_SPRAY          = 68948,  ///< 古龙水喷射 - 巴克斯特的攻击技能
    SPELL_VALIDATE_AREA          = 68644,  ///< 验证区域 - 检查可投掷位置
    SPELL_THROW_COLOGNE          = 68841,  ///< 投掷古龙水 - 地面效果法术
    SPELL_BUNNY_LOCKDOWN         = 69039,  ///< 目标锁定 - 标记已被投掷的位置
    SPELL_THROW_PERFUME          = 68799,  ///< 投掷香水 - 地面效果法术
    SPELL_PERFUME_SPILL          = 68798,  ///< 香水泼洒 - 地面效果光环
    SPELL_COLOGNE_SPILL          = 68614,  ///< 古龙水泼洒 - 地面效果光环
    SPELL_PERFUME_SPILL_DAMAGE   = 68927,  ///< 香水泼洒伤害 - 周期性伤害
    SPELL_COLOGNE_SPILL_DAMAGE   = 68934   ///< 古龙水泼洒伤害 - 周期性伤害
};

/**
 * @brief 药剂师对话文本枚举
 *
 * 定义药剂师哈梅尔及其助手的对话文本ID
 */
enum ApothecarySays
{
    SAY_INTRO_0      = 0,   ///< 哈梅尔开场对话1
    SAY_INTRO_1      = 1,   ///< 哈梅尔开场对话2
    SAY_INTRO_2      = 2,   ///< 哈梅尔开场对话3
    SAY_CALL_BAXTER  = 3,   ///< 哈梅尔召唤巴克斯特
    SAY_CALL_FRYE    = 4,   ///< 哈梅尔召唤弗莱
    SAY_HUMMEL_DEATH = 5,   ///< 哈梅尔假死对话
    SAY_SUMMON_ADDS  = 6,   ///< 哈梅尔召唤小怪
    SAY_BAXTER_DEATH = 0,   ///< 巴克斯特死亡对话
    SAY_FRYE_DEATH   = 0    ///< 弗莱死亡对话
};

/**
 * @brief 药剂师事件ID枚举
 *
 * 定义药剂师哈梅尔战斗中的事件计时器ID
 */
enum ApothecaryEvents
{
    EVENT_HUMMEL_SAY_0 = 1,             ///< 开场对话1事件
    EVENT_HUMMEL_SAY_1,                 ///< 开场对话2事件
    EVENT_HUMMEL_SAY_2,                 ///< 开场对话3事件
    EVENT_START_FIGHT,                  ///< 开始战斗事件
    EVENT_CHAIN_REACTION,               ///< 连锁反应技能事件
    EVENT_PERFUME_SPRAY,                ///< 香水喷射技能事件
    EVENT_COLOGNE_SPRAY,                ///< 古龙水喷射技能事件（巴克斯特）
    EVENT_CALL_BAXTER,                  ///< 召唤巴克斯特参战事件
    EVENT_CALL_FRYE,                    ///< 召唤弗莱参战事件
    EVENT_CALL_CRAZED_APOTHECARY,       ///< 召唤疯狂药剂师喊话事件
    EVENT_CRAZED_APOTHECARY             ///< 实际召唤疯狂药剂师事件
};

/**
 * @brief 药剂师杂项枚举
 *
 * 定义药剂师哈梅尔战斗中的杂项常量
 */
enum ApothecaryMisc
{
    ACTION_START_EVENT          = 1,        ///< 开始事件动作
    ACTION_START_FIGHT          = 2,        ///< 开始战斗动作
    GOSSIP_OPTION_START         = 0,        ///< 对话选项索引
    GOSSIP_MENU_HUMMEL          = 10847,    ///< 哈梅尔对话菜单ID
    QUEST_YOUVE_BEEN_SERVED     = 14488,    ///< 任务ID：请君入瓮
    NPC_APOTHECARY_FRYE         = 36272,    ///< 弗莱NPC ID
    NPC_APOTHECARY_BAXTER       = 36565,    ///< 巴克斯特NPC ID
    NPC_VIAL_BUNNY              = 36530,    ///< 小瓶兔子（法术目标）NPC ID
    NPC_CROWN_APOTHECARY        = 36885,    ///< 皇冠药剂师（场景小怪）NPC ID
    PHASE_ALL                   = 0,        ///< 所有阶段（默认）
    PHASE_INTRO                 = 1         ///< 开场阶段
};

/// 巴克斯特移动目标位置
Position const BaxterMovePos = { -221.4115f, 2206.825f, 79.93151f, 0.0f };

/// 弗莱移动目标位置
Position const FryeMovePos = { -196.2483f, 2197.224f, 79.9315f, 0.0f };

/**
 * @brief 药剂师哈梅尔BOSS AI结构体
 *
 * 实现药剂师哈梅尔的完整战斗逻辑：
 * - 开场对话阶段：三段对话后进入战斗
 * - 独特的假死机制：当血量降至1时假死，等待助手全部死亡
 * - 技能循环：香水喷射、连锁反应、召唤小怪
 * - 助手召唤：战斗开始后依次召唤巴克斯特和弗莱参战
 */
struct boss_apothecary_hummel : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_apothecary_hummel(Creature* creature) : BossAI(creature, DATA_APOTHECARY_HUMMEL), _deadCount(0), _isDead(false) { }

    /**
     * @brief 对话选项选择回调
     *
     * 处理玩家选择开始战斗的对话选项
     *
     * @param player 玩家对象指针
     * @param menuId 菜单ID
     * @param gossipListId 对话选项列表ID
     * @return false 停止后续处理
     */
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
    {
        if (menuId == GOSSIP_MENU_HUMMEL && gossipListId == GOSSIP_OPTION_START)
        {
            me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            CloseGossipMenuFor(player);
            DoAction(ACTION_START_EVENT);
        }
        return false;
    }

    /**
     * @brief 重置回调
     *
     * 重置BOSS状态：
     * - 重置死亡计数
     * - 重置假死状态
     * - 设置为友好阵营
     * - 召唤助手NPC
     */
    void Reset() override
    {
        _Reset();
        _deadCount = 0;
        _isDead = false;
        events.SetPhase(PHASE_ALL);
        me->SetFaction(FACTION_FRIENDLY);
        me->SummonCreatureGroup(1);  // 召唤助手（巴克斯特和弗莱）
    }

    /**
     * @brief 进入逃避模式回调
     * @param why 逃避原因（未使用）
     *
     * 清理召唤物并设置10秒后消失
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        summons.DespawnAll();
        _EnterEvadeMode();
        _DespawnAtEvade(Seconds(10));
    }

    /**
     * @brief 执行动作回调
     * @param action 动作ID
     *
     * 处理开始事件动作：
     * - 进入开场阶段
     * - 设置敌对阵营
     * - 通知助手开始移动
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_START_EVENT && events.IsInPhase(PHASE_ALL))
        {
            events.SetPhase(PHASE_INTRO);
            events.ScheduleEvent(EVENT_HUMMEL_SAY_0, Milliseconds(1));

            me->SetImmuneToPC(true);
            me->SetFaction(FACTION_MONSTER);
            DummyEntryCheckPredicate pred;
            summons.DoAction(ACTION_START_EVENT, pred);
        }
    }

    /**
     * @brief 受到伤害回调
     *
     * 实现哈梅尔的假死机制：
     * - 当血量降至1时进入假死状态
     * - 必须等待两个助手全部死亡才会真正死亡
     * - 假死时移除香水光环并播放假死动画
     *
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值，可能被修改
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth())
            if (_deadCount < 2)
            {
                // 保持1点血量不死
                damage = me->GetHealth() - 1;
                if (!_isDead)
                {
                    _isDead = true;
                    me->RemoveAurasDueToSpell(SPELL_ALLURING_PERFUME);
                    DoCastSelf(SPELL_PERMANENT_FEIGN_DEATH, true);
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    Talk(SAY_HUMMEL_DEATH);
                }
            }
    }

    /**
     * @brief 召唤生物死亡回调
     * @param summon 死亡的召唤生物
     * @param killer 击杀者（未使用）
     *
     * 当助手死亡时增加死亡计数
     * 当两个助手都死亡且哈梅尔假死时，哈梅尔真正死亡
     */
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_APOTHECARY_FRYE || summon->GetEntry() == NPC_APOTHECARY_BAXTER)
            _deadCount++;

        // 两个助手死亡后，假死的哈梅尔自杀
        if (me->HasAura(SPELL_PERMANENT_FEIGN_DEATH) && _deadCount == 2)
            DoCastSelf(SPELL_QUIET_SUICIDE, true);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者（未使用）
     *
     * 完成BOSS击杀：
     * - 如果尚未假死则播放死亡对话
     * - 更新副本进度
     * - 完成随机副本任务（LFG）
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (!_isDead)
            Talk(SAY_HUMMEL_DEATH);

        events.Reset();
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        instance->SetBossState(DATA_APOTHECARY_HUMMEL, DONE);

        // 如果是随机副本，通知系统完成
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        if (!players.isEmpty())
        {
            if (Group* group = players.begin()->GetSource()->GetGroup())
                if (group->isLFGGroup())
                    sLFGMgr->FinishDungeon(group->GetGUID(), 288, me->GetMap());
        }
    }

    /**
     * @brief 更新AI
     *
     * 处理药剂师哈梅尔的完整战斗逻辑：
     * - 开场阶段：依次播放三段对话
     * - 战斗阶段：香水喷射、连锁反应、召唤小怪
     * - 助手召唤：战斗开始后依次召唤助手参战
     *
     * @param diff 距离上次更新的时间差（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        // 非战斗状态且非开场阶段时不执行
        if (!UpdateVictim() && !events.IsInPhase(PHASE_INTRO))
            return;

        events.Update(diff);

        // 施法中不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_HUMMEL_SAY_0:
                    // 开场对话1
                    Talk(SAY_INTRO_0);
                    events.ScheduleEvent(EVENT_HUMMEL_SAY_1, Seconds(4));
                    break;
                case EVENT_HUMMEL_SAY_1:
                    // 开场对话2
                    Talk(SAY_INTRO_1);
                    events.ScheduleEvent(EVENT_HUMMEL_SAY_2, Seconds(4));
                    break;
                case EVENT_HUMMEL_SAY_2:
                    // 开场对话3
                    Talk(SAY_INTRO_2);
                    events.ScheduleEvent(EVENT_START_FIGHT, 4s);
                    break;
                case EVENT_START_FIGHT:
                {
                    // 开始战斗
                    me->SetImmuneToAll(false);
                    DoZoneInCombat();
                    // 安排各种事件
                    events.ScheduleEvent(EVENT_CALL_BAXTER, 6s);
                    events.ScheduleEvent(EVENT_CALL_FRYE, 14s);
                    events.ScheduleEvent(EVENT_PERFUME_SPRAY, Milliseconds(3640));
                    events.ScheduleEvent(EVENT_CHAIN_REACTION, 15s);
                    events.ScheduleEvent(EVENT_CALL_CRAZED_APOTHECARY, 15s);
                    events.ScheduleEvent(EVENT_CRAZED_APOTHECARY, 15s);

                    // 移除场景中的皇冠药剂师
                    std::vector<Creature*> trashs;
                    me->GetCreatureListWithEntryInGrid(trashs, NPC_CROWN_APOTHECARY);
                    for (Creature* crea : trashs)
                        crea->DespawnOrUnsummon();

                    break;
                }
                case EVENT_CALL_BAXTER:
                {
                    // 召唤巴克斯特参战
                    Talk(SAY_CALL_BAXTER);
                    EntryCheckPredicate pred(NPC_APOTHECARY_BAXTER);
                    summons.DoAction(ACTION_START_FIGHT, pred);
                    summons.DoZoneInCombat(NPC_APOTHECARY_BAXTER);
                    break;
                }
                case EVENT_CALL_FRYE:
                {
                    // 召唤弗莱参战
                    Talk(SAY_CALL_FRYE);
                    EntryCheckPredicate pred(NPC_APOTHECARY_FRYE);
                    summons.DoAction(ACTION_START_FIGHT, pred);
                    break;
                }
                case EVENT_CALL_CRAZED_APOTHECARY:
                    // 召唤疯狂药剂师喊话
                    Talk(SAY_SUMMON_ADDS);
                    break;
                case EVENT_CRAZED_APOTHECARY:
                    // 实际召唤疯狂药剂师
                    instance->SetData(DATA_SPAWN_VALENTINE_ADDS, 0);
                    events.Repeat(Seconds(4), Seconds(6));
                    break;
                case EVENT_PERFUME_SPRAY:
                    // 香水喷射攻击
                    DoCastVictim(SPELL_PERFUME_SPRAY);
                    events.Repeat(Milliseconds(3640));
                    break;
                case EVENT_CHAIN_REACTION:
                    // 连锁反应AOE技能
                    DoCastVictim(SPELL_SUMMON_TABLE, true);
                    DoCastAOE(SPELL_CHAIN_REACTION);
                    events.Repeat(Seconds(25));
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 任务奖励回调
     * @param player 玩家对象指针（未使用）
     * @param quest 任务对象
     * @param opt 选项（未使用）
     *
     * 通过完成任务触发事件开始
     */
    void OnQuestReward(Player* /*player*/, Quest const* quest, uint32 /*opt*/) override
    {
        if (quest->GetQuestId() == QUEST_YOUVE_BEEN_SERVED)
            DoAction(ACTION_START_EVENT);
    }

    private:
        uint8 _deadCount;   ///< 已死亡的助手数量
        bool _isDead;       ///< 哈梅尔是否已假死
};

/**
 * @brief 药剂师通用AI结构体
 *
 * 提供药剂师助手的通用行为逻辑：
 * - 收到开始事件动作后移动到指定位置
 * - 收到开始战斗动作后进入战斗
 */
struct npc_apothecary_genericAI : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * @param pos 移动目标位置
     */
    npc_apothecary_genericAI(Creature* creature, Position pos) : ScriptedAI(creature), _movePos(pos) { }

    /**
     * @brief 执行动作回调
     * @param action 动作ID
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_START_EVENT)
        {
            // 开始移动到指定位置
            me->SetImmuneToPC(true);
            me->SetFaction(FACTION_MONSTER);
            me->GetMotionMaster()->MovePoint(1, _movePos);
        }
        else if (action == ACTION_START_FIGHT)
        {
            // 进入战斗
            me->SetImmuneToAll(false);
            DoZoneInCombat();
        }
    }

    /**
     * @brief 移动通知回调
     * @param type 移动类型
     * @param pointId 路径点ID
     *
     * 到达目标位置后播放使用物品动画
     */
    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == POINT_MOTION_TYPE && pointId == 1)
            me->SetEmoteState(EMOTE_STATE_USE_STANDING);
    }

protected:
    Position _movePos;  ///< 移动目标位置
};

/**
 * @brief 药剂师弗莱AI结构体
 *
 * 弗莱是哈梅尔的助手之一，没有特殊技能，主要提供额外伤害输出
 */
struct npc_apothecary_frye : public npc_apothecary_genericAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_apothecary_frye(Creature* creature) : npc_apothecary_genericAI(creature, FryeMovePos) { }

    /**
     * @brief 死亡回调
     * @param killer 击杀者（未使用）
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_FRYE_DEATH);
    }
};

/**
 * @brief 药剂师巴克斯特AI结构体
 *
 * 巴克斯特是哈梅尔的助手之一，拥有古龙水喷射和连锁反应技能
 */
struct npc_apothecary_baxter : public npc_apothecary_genericAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_apothecary_baxter(Creature* creature) : npc_apothecary_genericAI(creature, BaxterMovePos) { }

    /**
     * @brief 重置回调
     *
     * 初始化技能事件计时器
     */
    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_COLOGNE_SPRAY, 7s);
        _events.ScheduleEvent(EVENT_CHAIN_REACTION, 12s);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者（未使用）
     */
    void JustDied(Unit* /*killer*/) override
    {
        _events.Reset();
        Talk(SAY_BAXTER_DEATH);
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间差（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_COLOGNE_SPRAY:
                    // 古龙水喷射攻击
                    DoCastVictim(SPELL_COLOGNE_SPRAY);
                    _events.Repeat(Seconds(4));
                    break;
                case EVENT_CHAIN_REACTION:
                    // 连锁反应AOE技能
                    DoCastVictim(SPELL_SUMMON_TABLE);
                    DoCastVictim(SPELL_CHAIN_REACTION);
                    _events.Repeat(Seconds(25));
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;   ///< 事件映射表
};

/**
 * @brief 残留烟雾目标选择法术脚本 (Spell ID: 68965)
 *
 * 实现疯狂药剂师的移动逻辑：
 * - 50%几率在战斗中移动到随机的小瓶兔子位置
 * - 用于增加战斗的动态性和不可预测性
 */
class spell_apothecary_lingering_fumes : public SpellScript
{
    PrepareSpellScript(spell_apothecary_lingering_fumes);

    /**
     * @brief 施法后回调
     *
     * 50%几率移动到随机的小瓶兔子位置
     */
    void HandleAfterCast()
    {
        Unit* caster = GetCaster();
        if (!caster->IsInCombat() || roll_chance_i(50))
            return;

        std::list<Creature*> triggers;
        caster->GetCreatureListWithEntryInGrid(triggers, NPC_VIAL_BUNNY, 100.0f);
        if (triggers.empty())
            return;

        Creature* trigger = Trinity::Containers::SelectRandomContainerElement(triggers);
        caster->GetMotionMaster()->MovePoint(0, trigger->GetPosition());

    }

    /**
     * @brief 法术命中回调
     * @param effindex 效果索引（未使用）
     *
     * 对命中目标施放区域验证法术
     */
    void HandleScript(SpellEffIndex /*effindex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(GetHitUnit(), SPELL_VALIDATE_AREA, true);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_apothecary_lingering_fumes::HandleAfterCast);
        OnEffectHitTarget += SpellEffectFn(spell_apothecary_lingering_fumes::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 情人节BOSS区域验证法术脚本 (Spell ID: 68644)
 *
 * 实现区域验证逻辑：
 * - 排除已被锁定的目标
 * - 随机选择一个有效目标
 * - 锁定目标并投掷香水或古龙水
 */
class spell_apothecary_validate_area : public SpellScript
{
    PrepareSpellScript(spell_apothecary_validate_area);

    /**
     * @brief 目标过滤回调
     * @param targets 目标列表
     *
     * 移除已锁定的目标，随机选择一个有效目标
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_BUNNY_LOCKDOWN));
        if (targets.empty())
            return;

        WorldObject* target = Trinity::Containers::SelectRandomContainerElement(targets);
        targets.clear();
        targets.push_back(target);
    }

    /**
     * @brief 法术命中回调
     * @param effindex 效果索引（未使用）
     *
     * 锁定目标并投掷香水或古龙水
     */
    void HandleScript(SpellEffIndex /*effindex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_BUNNY_LOCKDOWN, true);
        GetCaster()->CastSpell(GetHitUnit(), RAND(SPELL_THROW_COLOGNE, SPELL_THROW_PERFUME), true);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_apothecary_validate_area::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENTRY);
        OnEffectHitTarget += SpellEffectFn(spell_apothecary_validate_area::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 投掷古龙水法术脚本 (Spell ID: 69038)
 *
 * 在目标位置创建古龙水泼洒效果
 */
class spell_apothecary_throw_cologne : public SpellScript
{
    PrepareSpellScript(spell_apothecary_throw_cologne);

    /**
     * @brief 法术命中回调
     * @param effindex 效果索引（未使用）
     */
    void HandleScript(SpellEffIndex /*effindex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_COLOGNE_SPILL, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_apothecary_throw_cologne::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 投掷香水法术脚本 (Spell ID: 68966)
 *
 * 在目标位置创建香水泼洒效果
 */
class spell_apothecary_throw_perfume : public SpellScript
{
    PrepareSpellScript(spell_apothecary_throw_perfume);

    /**
     * @brief 法术命中回调
     * @param effindex 效果索引（未使用）
     */
    void HandleScript(SpellEffIndex /*effindex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_PERFUME_SPILL, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_apothecary_throw_perfume::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 浓缩迷人香水泼洒光环脚本 (Spell ID: 68798)
 *
 * 实现香水泼洒地面的周期性伤害效果
 */
class spell_apothecary_perfume_spill : public AuraScript
{
    PrepareAuraScript(spell_apothecary_perfume_spill);

    /**
     * @brief 周期性触发回调
     * @param aurEff 光环效果（未使用）
     */
    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_PERFUME_SPILL_DAMAGE, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_apothecary_perfume_spill::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 浓缩不可抗拒古龙水泼洒光环脚本 (Spell ID: 68614)
 *
 * 实现古龙水泼洒地面的周期性伤害效果
 */
class spell_apothecary_cologne_spill : public AuraScript
{
    PrepareAuraScript(spell_apothecary_cologne_spill);

    /**
     * @brief 周期性触发回调
     * @param aurEff 光环效果（未使用）
     */
    void OnPeriodic(AuraEffect const* /*aurEff*/)
    {
        GetTarget()->CastSpell(GetTarget(), SPELL_COLOGNE_SPILL_DAMAGE, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_apothecary_cologne_spill::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 注册药剂师哈梅尔相关脚本
 *
 * 注册以下脚本：
 * - boss_apothecary_hummel：药剂师哈梅尔BOSS
 * - npc_apothecary_baxter：药剂师巴克斯特助手
 * - npc_apothecary_frye：药剂师弗莱助手
 * - spell_apothecary_lingering_fumes：残留烟雾法术
 * - spell_apothecary_validate_area：区域验证法术
 * - spell_apothecary_throw_cologne：投掷古龙水法术
 * - spell_apothecary_throw_perfume：投掷香水法术
 * - spell_apothecary_perfume_spill：香水泼洒光环
 * - spell_apothecary_cologne_spill：古龙水泼洒光环
 */
void AddSC_boss_apothecary_hummel()
{
    RegisterShadowfangKeepCreatureAI(boss_apothecary_hummel);
    RegisterShadowfangKeepCreatureAI(npc_apothecary_baxter);
    RegisterShadowfangKeepCreatureAI(npc_apothecary_frye);
    RegisterSpellScript(spell_apothecary_lingering_fumes);
    RegisterSpellScript(spell_apothecary_validate_area);
    RegisterSpellScript(spell_apothecary_throw_cologne);
    RegisterSpellScript(spell_apothecary_throw_perfume);
    RegisterSpellScript(spell_apothecary_perfume_spill);
    RegisterSpellScript(spell_apothecary_cologne_spill);
}
