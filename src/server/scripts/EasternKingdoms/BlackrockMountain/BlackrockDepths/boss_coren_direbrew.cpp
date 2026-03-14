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
 * @file boss_coren_direbrew.cpp
 * @brief 黑石深渊副本节日BOSS：科林·烈酒(Coren Direbrew)的AI脚本实现
 *
 * 科林·烈酒是美酒节期间在黑石深渊酒吧出现的节日BOSS。
 * 该BOSS是一个多阶段战斗，具有以下特点：
 * - 通过闲聊对话触发战斗
 * - 血量降到66%和33%时分别召唤两个妹妹协助战斗
 * - 使用地穴魔机器召唤小怪
 * - 拥有缴械技能和投掷酒杯机制
 *
 * 这是美酒节的核心BOSS，击败后可获得节日奖励。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "Containers.h"
#include "GameObjectAI.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "InstanceScript.h"
#include "LFGMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 科林·烈酒对话文本枚举
 * 定义科林·烈酒在事件和战斗中使用的对话文本索引
 */
enum DirebrewSays
{
    SAY_INTRO                 = 0,  // 开场对话1
    SAY_INTRO1                = 1,  // 开场对话2
    SAY_INTRO2                = 2,  // 开场对话3
    SAY_INSULT                = 3,  // 侮辱玩家对话（战斗开始）
    SAY_ANTAGONIST_1          = 0,  // 对抗者对话1
    SAY_ANTAGONIST_2          = 1,  // 对抗者对话2
    SAY_ANTAGONIST_COMBAT     = 2   // 对抗者战斗对话
};

/**
 * @brief 科林·烈酒动作枚举
 * 定义用于触发不同事件的动作ID
 */
enum DirebrewActions
{
    ACTION_START_FIGHT        = -1,  // 开始战斗动作
    ACTION_ANTAGONIST_SAY_1   = -2,  // 对抗者对话1动作
    ACTION_ANTAGONIST_SAY_2   = -3,  // 对抗者对话2动作
    ACTION_ANTAGONIST_HOSTILE = -4   // 对抗者变为敌对动作
};

/**
 * @brief 科林·烈酒相关NPC枚举
 * 定义科林·烈酒战斗中召唤的NPC ID
 */
enum DirebrewNpcs
{
    NPC_ILSA_DIREBREW         = 26764,  // 伊尔萨·烈酒 - 科林的妹妹（66%血量召唤）
    NPC_URSULA_DIREBREW       = 26822,  // 乌尔苏拉·烈酒 - 科林的妹妹（33%血量召唤）
    NPC_ANTAGONIST            = 23795   // 对抗者 - 开场对话的NPC
};

/**
 * @brief 科林·烈酒法术枚举
 * 定义科林·烈酒及其妹妹使用的法术ID
 */
enum DirebrewSpells
{
    SPELL_MOLE_MACHINE_EMERGE          = 50313,  // 地穴魔机器出现效果
    SPELL_DIREBREW_DISARM_PRE_CAST     = 47407,  // 烈酒缴械预施法
    SPELL_MOLE_MACHINE_TARGET_PICKER   = 47691,  // 地穴魔机器目标选择器
    SPELL_MOLE_MACHINE_MINION_SUMMONER = 47690,  // 地穴魔机器随从召唤者
    SPELL_DIREBREW_DISARM_GROW         = 47409,  // 烈酒缴械成长效果
    SPELL_DIREBREW_DISARM              = 47310,  // 烈酒缴械
    SPELL_CHUCK_MUG                    = 50276,  // 投掷酒杯
    SPELL_PORT_TO_COREN                = 52850,  // 传送到科林身边
    SPELL_SEND_MUG_CONTROL_AURA        = 47369,  // 发送酒杯控制光环
    SPELL_SEND_MUG_TARGET_PICKER       = 47370,  // 发送酒杯目标选择器
    SPELL_SEND_FIRST_MUG               = 47333,  // 发送第一杯酒
    SPELL_SEND_SECOND_MUG              = 47339,  // 发送第二杯酒
    SPELL_REQUEST_SECOND_MUG           = 47344,  // 请求第二杯酒
    SPELL_HAS_DARK_BREWMAIDENS_BREW    = 47331,  // 拥有黑暗酿酒师的酒
    SPELL_BARRELED_CONTROL_AURA        = 50278,  // 装桶控制光环
    SPELL_BARRELED                     = 47442   // 装桶（乌尔苏拉的技能）
};

/**
 * @brief 科林·烈酒战斗阶段枚举
 * 定义科林·烈酒战斗的不同阶段
 */
enum DirebrewPhases
{
    PHASE_ALL = 1,      // 所有阶段（初始状态）
    PHASE_INTRO,        // 开场对话阶段
    PHASE_ONE,          // 第一阶段（100%-66%血量）
    PHASE_TWO,          // 第二阶段（66%-33%血量）
    PHASE_THREE         // 第三阶段（33%-0%血量）
};

/**
 * @brief 科林·烈酒事件枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum DirebrewEvents
{
    EVENT_INTRO_1 = 1,             // 开场事件1
    EVENT_INTRO_2,                 // 开场事件2
    EVENT_INTRO_3,                 // 开场事件3
    EVENT_DIREBREW_DISARM,         // 烈酒缴械事件
    EVENT_SUMMON_MOLE_MACHINE,     // 召唤地穴魔机器事件
    EVENT_RESPAWN_ILSA,            // 重生伊尔萨事件
    EVENT_RESPAWN_URSULA           // 重生乌尔苏拉事件
};

/**
 * @brief 科林·烈酒杂项枚举
 * 定义闲聊菜单、游戏对象和其他杂项数据
 */
enum DirebrewMisc
{
    GOSSIP_ID                      = 11388,   // 闲聊菜单ID
    GO_MOLE_MACHINE_TRAP           = 188509,  // 地穴魔机器陷阱游戏对象ID
    GOSSIP_OPTION_FIGHT            = 0,       // 闲聊选项：战斗
    GOSSIP_OPTION_APOLOGIZE        = 1,       // 闲聊选项：道歉
    DATA_TARGET_GUID               = 1,       // 目标GUID数据ID
    MAX_ANTAGONISTS                = 3        // 最大对抗者数量
};

/**
 * @brief 对抗者位置数组
 * 定义3个对抗者的生成位置坐标
 */
Position const AntagonistPos[3] =
{
    { 895.3782f, -132.1722f, -49.66423f, 2.6529f   },    // 对抗者1位置
    { 893.9837f, -133.2879f, -49.66541f, 2.583087f },    // 对抗者2位置
    { 896.2667f, -130.483f,  -49.66249f, 2.600541f }     // 对抗者3位置
};

/**
 * @brief 科林·烈酒BOSS结构体
 *
 * 继承自BossAI，实现科林·烈酒的战斗逻辑。
 * 该BOSS是一个多阶段战斗，包含开场对话、姐妹召唤、地穴魔机器召唤等复杂机制。
 */
struct boss_coren_direbrew : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * 初始化BOSS AI，设置数据ID为DATA_COREN
     */
    boss_coren_direbrew(Creature* creature) : BossAI(creature, DATA_COREN) { }

    /**
     * @brief 闲聊选项选择事件处理
     * @param player 选择闲聊选项的玩家指针
     * @param menuId 闲聊菜单ID
     * @param gossipListId 闲聊列表ID
     * @return 返回false表示不阻止后续处理
     *
     * 处理玩家与科林·烈酒的对话选项：
     * - 选择战斗选项：触发战斗
     * - 选择道歉选项：关闭闲聊菜单
     *
     * 调用时机：玩家选择闲聊选项时
     */
    bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
    {
        if (menuId != GOSSIP_ID)
            return false;

        if (gossipListId == GOSSIP_OPTION_FIGHT)
        {
            // 玩家选择战斗，科林说侮辱性对话并开始战斗
            Talk(SAY_INSULT, player);
            DoAction(ACTION_START_FIGHT);
        }
        else if (gossipListId == GOSSIP_OPTION_APOLOGIZE)
            CloseGossipMenuFor(player);  // 玩家选择道歉，关闭菜单

        return false;
    }

    /**
     * @brief 重置AI状态
     *
     * 当BOSS脱离战斗或重置时调用。
     * 设置BOSS为免疫PC状态、友好阵营，并召唤3个对抗者NPC。
     *
     * 调用时机：BOSS脱离战斗、团灭重置、实例重置时
     */
    void Reset() override
    {
        _Reset();
        me->SetImmuneToPC(true);        // 设置免疫玩家攻击
        me->SetFaction(FACTION_FRIENDLY);  // 设置为友好阵营
        events.SetPhase(PHASE_ALL);     // 设置初始阶段

        // 召唤3个对抗者NPC
        for (uint8 i = 0; i < MAX_ANTAGONISTS; ++i)
            me->SummonCreature(NPC_ANTAGONIST, AntagonistPos[i], TEMPSUMMON_DEAD_DESPAWN);
    }

    /**
     * @brief 进入逃避模式事件处理
     * @param why 逃避原因
     *
     * 当BOSS脱离战斗并逃避时调用。
     * 消失所有召唤的生物，并在10秒后消失。
     *
     * 调用时机：BOSS脱离战斗逃避时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        _EnterEvadeMode();
        summons.DespawnAll();  // 消失所有召唤物
        _DespawnAtEvade(Seconds(10));  // 10秒后消失
    }

    /**
     * @brief 视线范围内事件处理
     * @param who 进入视线范围的单位
     *
     * 当玩家进入BOSS视线范围时触发开场对话序列。
     * 仅在PHASE_ALL阶段触发，触发后进入PHASE_INTRO阶段。
     *
     * 调用时机：玩家进入BOSS视线范围时
     */
    void MoveInLineOfSight(Unit* who) override
    {
        // 仅在PHASE_ALL阶段且为玩家时触发
        if (!events.IsInPhase(PHASE_ALL) || who->GetTypeId() != TYPEID_PLAYER)
            return;

        events.SetPhase(PHASE_INTRO);  // 进入开场对话阶段
        events.ScheduleEvent(EVENT_INTRO_1, Seconds(6), 0, PHASE_INTRO);  // 6秒后触发第一个对话事件
        Talk(SAY_INTRO);  // 说开场白
    }

    /**
     * @brief 执行动作事件处理
     * @param action 动作ID
     *
     * 处理来自闲聊选项或其他脚本的动作请求。
     * 主要处理开始战斗动作，设置BOSS为敌对状态并进入战斗。
     *
     * 调用时机：闲聊选项触发或其他脚本调用时
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_START_FIGHT)
        {
            events.SetPhase(PHASE_ONE);  // 进入第一阶段
            me->SetImmuneToPC(false);    // 取消免疫玩家攻击
            me->SetFaction(FACTION_GOBLIN_DARK_IRON_BAR_PATRON);  // 设置为黑铁酒吧顾客阵营（敌对）
            DoZoneInCombat();            // 进入战斗状态

            // 让所有对抗者变为敌对
            EntryCheckPredicate pred(NPC_ANTAGONIST);
            summons.DoAction(ACTION_ANTAGONIST_HOSTILE, pred);

            // 调度地穴魔机器和缴械技能
            events.ScheduleEvent(EVENT_SUMMON_MOLE_MACHINE, 15s);
            events.ScheduleEvent(EVENT_DIREBREW_DISARM, 20s);
        }
    }

    /**
     * @brief 受到伤害事件处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当BOSS受到伤害时调用，用于检测血量阈值并触发阶段转换。
     * 血量低于66%时召唤伊尔萨，血量低于33%时召唤乌尔苏拉。
     *
     * 调用时机：BOSS受到伤害时
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 血量低于66%时召唤伊尔萨·烈酒
        if (me->HealthBelowPctDamaged(66, damage) && events.IsInPhase(PHASE_ONE))
        {
            events.SetPhase(PHASE_TWO);  // 进入第二阶段
            SummonSister(NPC_ILSA_DIREBREW);  // 召唤伊尔萨
        }
        // 血量低于33%时召唤乌尔苏拉·烈酒
        else if (me->HealthBelowPctDamaged(33, damage) && events.IsInPhase(PHASE_TWO))
        {
            events.SetPhase(PHASE_THREE);  // 进入第三阶段
            SummonSister(NPC_URSULA_DIREBREW);  // 召唤乌尔苏拉
        }
    }

    /**
     * @brief 召唤生物死亡事件处理
     * @param summon 死亡的召唤生物
     * @param killer 击杀者（未使用）
     *
     * 当召唤的姐妹死亡时，安排在1秒后重新召唤她们。
     * 这是科林·烈酒战斗的一个特殊机制：姐妹可以重复召唤。
     *
     * 调用时机：召唤的姐妹（伊尔萨或乌尔苏拉）死亡时
     */
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        // 如果伊尔萨死亡，安排重生
        if (summon->GetEntry() == NPC_ILSA_DIREBREW)
            events.ScheduleEvent(EVENT_RESPAWN_ILSA, 1s);
        // 如果乌尔苏拉死亡，安排重生
        else if (summon->GetEntry() == NPC_URSULA_DIREBREW)
            events.ScheduleEvent(EVENT_RESPAWN_URSULA, 1s);
    }

    /**
     * @brief 死亡事件处理
     * @param killer 击杀者（未使用）
     *
     * 当科林·烈酒死亡时调用。
     * 如果是随机队伍（LFG），则完成对应的地下城进度。
     *
     * 调用时机：BOSS死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();

        // 获取地图上的玩家列表
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        if (!players.isEmpty())
        {
            // 如果是LFG队伍，完成地下城进度（ID 287为美酒节科林·烈酒）
            if (Group* group = players.begin()->GetSource()->GetGroup())
                if (group->isLFGGroup())
                    sLFGMgr->FinishDungeon(group->GetGUID(), 287, me->GetMap());
        }
    }

    /**
     * @brief 召唤姐妹
     * @param entry 姐妹的NPC ID（伊尔萨或乌尔苏拉）
     *
     * 在BOSS位置召唤指定的姐妹，并让其进入战斗状态。
     *
     * 调用时机：血量达到阈值时
     */
    void SummonSister(uint32 entry)
    {
        // 召唤姐妹并让其进入战斗
        if (Creature* sister = me->SummonCreature(entry, me->GetPosition(), TEMPSUMMON_DEAD_DESPAWN))
            DoZoneInCombat(sister);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每个游戏循环周期调用一次，处理战斗逻辑和开场对话序列。
     * 包括更新事件计时器、执行事件、处理施法状态和普通攻击。
     *
     * 调用时机：每个游戏Tick（约每50毫秒）
     * 性能注意事项：频繁调用，需要优化处理逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果不在战斗且不在开场对话阶段，则不执行任何操作
        if (!UpdateVictim() && !events.IsInPhase(PHASE_INTRO))
            return;

        events.Update(diff);  // 更新事件计时器

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INTRO_1:
                    // 开场对话1
                    Talk(SAY_INTRO1);
                    events.ScheduleEvent(EVENT_INTRO_2, Seconds(4), 0, PHASE_INTRO);
                    break;
                case EVENT_INTRO_2:
                {
                    // 让对抗者说第一句对话
                    EntryCheckPredicate pred(NPC_ANTAGONIST);
                    summons.DoAction(ACTION_ANTAGONIST_SAY_1, pred);
                    events.ScheduleEvent(EVENT_INTRO_3, Seconds(3), 0, PHASE_INTRO);
                    break;
                }
                case EVENT_INTRO_3:
                {
                    // 开场对话2，让对抗者说第二句对话
                    Talk(SAY_INTRO2);
                    EntryCheckPredicate pred(NPC_ANTAGONIST);
                    summons.DoAction(ACTION_ANTAGONIST_SAY_2, pred);
                    break;
                }
                case EVENT_RESPAWN_ILSA:
                    // 重生伊尔萨
                    SummonSister(NPC_ILSA_DIREBREW);
                    break;
                case EVENT_RESPAWN_URSULA:
                    // 重生乌尔苏拉
                    SummonSister(NPC_URSULA_DIREBREW);
                    break;
                case EVENT_SUMMON_MOLE_MACHINE:
                {
                    // 召唤地穴魔机器
                    CastSpellExtraArgs args;
                    args.TriggerFlags = TRIGGERED_FULL_MASK;
                    args.AddSpellMod(SPELLVALUE_MAX_TARGETS, 1);
                    me->CastSpell(nullptr, SPELL_MOLE_MACHINE_TARGET_PICKER, args);
                    events.Repeat(Seconds(15));  // 15秒后再次召唤
                    break;
                }
                case EVENT_DIREBREW_DISARM:
                    // 施放缴械技能
                    DoCastSelf(SPELL_DIREBREW_DISARM_PRE_CAST, true);
                    events.Repeat(Seconds(20));  // 20秒后再次施放
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，则等待下次更新
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果技能都在冷却中，执行普通攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 科林·烈酒的姐妹NPC结构体
 *
 * 继承自ScriptedAI，实现伊尔萨和乌尔苏拉·烈酒的战斗逻辑。
 * 姐妹会周期性地投掷酒杯和传送回科林身边，乌尔苏拉还会使用装桶技能。
 */
struct npc_coren_direbrew_sisters : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_coren_direbrew_sisters(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 设置GUID
     * @param guid 要设置的GUID
     * @param id 数据ID
     *
     * 用于保存投掷酒杯的目标GUID。
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_TARGET_GUID)
            _targetGUID = guid;  // 保存目标GUID
    }

    /**
     * @brief 获取GUID
     * @param data 数据ID
     * @return 返回对应的GUID
     *
     * 用于获取保存的目标GUID。
     */
    ObjectGuid GetGUID(int32 data) const override
    {
        if (data == DATA_TARGET_GUID)
            return _targetGUID;  // 返回保存的目标GUID

        return ObjectGuid::Empty;
    }

    /**
     * @brief 进入战斗事件处理
     * @param who 进入战斗的目标（未使用）
     *
     * 当姐妹进入战斗时调用。
     * 施放传送技能传送到科林身边，并根据身份施放不同的控制光环。
     * 乌尔苏拉使用装桶控制光环，伊尔萨使用发送酒杯控制光环。
     *
     * 调用时机：姐妹被召唤并进入战斗时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // 传送到科林身边
        DoCastSelf(SPELL_PORT_TO_COREN);

        // 根据身份施放不同的控制光环
        if (me->GetEntry() == NPC_URSULA_DIREBREW)
            DoCastSelf(SPELL_BARRELED_CONTROL_AURA);  // 乌尔苏拉：装桶控制光环
        else
            DoCastSelf(SPELL_SEND_MUG_CONTROL_AURA);  // 伊尔萨：发送酒杯控制光环

        // 设置调度器验证器，确保不在施法时执行任务
        _scheduler
            .SetValidator([this]
        {
            return !me->HasUnitState(UNIT_STATE_CASTING);
        })
            .Schedule(Seconds(2), [this](TaskContext mugChuck)
        {
            // 选择一个没有黑暗酿酒师的酒增益的随机目标，投掷酒杯
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, false, true, -SPELL_HAS_DARK_BREWMAIDENS_BREW))
                DoCast(target, SPELL_CHUCK_MUG);
            mugChuck.Repeat(Seconds(4));  // 4秒后重复
        });
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 更新任务调度器并执行普通攻击。
     *
     * 调用时机：每个游戏Tick
     */
    void UpdateAI(uint32 diff) override
    {
        // 更新任务调度器，在空闲时执行普通攻击
        _scheduler.Update(diff, [this]
        {
            DoMeleeAttackIfReady();
        });
    }

private:
    ObjectGuid _targetGUID;      ///< 投掷酒杯的目标GUID
    TaskScheduler _scheduler;    ///< 任务调度器，用于管理周期性任务
};

/**
 * @brief 科林·烈酒随从NPC结构体
 *
 * 继承自ScriptedAI，实现地穴魔机器召唤的小怪的战斗逻辑。
 * 这些随从被召唤后立即进入战斗。
 */
struct npc_direbrew_minion : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_direbrew_minion(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    /**
     * @brief 重置AI状态
     *
     * 设置随从为黑铁酒吧顾客阵营并进入战斗。
     */
    void Reset() override
    {
        me->SetFaction(FACTION_GOBLIN_DARK_IRON_BAR_PATRON);  // 设置为敌对阵营
        DoZoneInCombat();  // 立即进入战斗
    }

    /**
     * @brief 被召唤事件处理
     * @param summoner 召唤者（未使用）
     *
     * 当随从被召唤时，通知科林·烈酒有新召唤物。
     *
     * 调用时机：随从被召唤时
     */
    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        // 获取科林·烈酒并通知他有新召唤物
        if (Creature* coren = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_COREN)))
            coren->AI()->JustSummoned(me);
    }

private:
    InstanceScript* _instance;  ///< 副本实例脚本指针
};

/**
 * @brief 科林·烈酒对抗者NPC结构体
 *
 * 继承自ScriptedAI，实现开场对话中的对抗者NPC的逻辑。
 * 对抗者在开场对话中与科林互动，战斗开始后变为敌对。
 */
struct npc_direbrew_antagonist : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_direbrew_antagonist(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 执行动作事件处理
     * @param action 动作ID
     *
     * 处理来自科林·烈酒的动作请求：
     * - 对话1：说第一句对话
     * - 对话2：说第二句对话
     * - 变为敌对：取消免疫并进入战斗
     *
     * 调用时机：科林·烈酒触发对应动作时
     */
    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_ANTAGONIST_SAY_1:
                Talk(SAY_ANTAGONIST_1);  // 说第一句对话
                break;
            case ACTION_ANTAGONIST_SAY_2:
                Talk(SAY_ANTAGONIST_2);  // 说第二句对话
                break;
            case ACTION_ANTAGONIST_HOSTILE:
                // 变为敌对状态
                me->SetImmuneToPC(false);  // 取消免疫玩家攻击
                me->SetFaction(FACTION_GOBLIN_DARK_IRON_BAR_PATRON);  // 设置为敌对阵营
                DoZoneInCombat();  // 进入战斗
                break;
            default:
                break;
        }
    }

    /**
     * @brief 进入战斗事件处理
     * @param who 进入战斗的目标
     *
     * 当对抗者进入战斗时，说战斗对话。
     *
     * 调用时机：对抗者进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_ANTAGONIST_COMBAT, who);  // 说战斗对话
        ScriptedAI::JustEngagedWith(who);
    }
};

/**
 * @brief 地穴魔机器游戏对象脚本类
 *
 * 继承自GameObjectScript，实现地穴魔机器的生成和激活逻辑。
 * 地穴魔机器会在激活时施放出现效果并触发陷阱。
 */
class go_direbrew_mole_machine : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     * 初始化游戏对象脚本，注册名称为"go_direbrew_mole_machine"
     */
    go_direbrew_mole_machine() : GameObjectScript("go_direbrew_mole_machine") { }

    /**
     * @brief 地穴魔机器AI结构体
     *
     * 继承自GameObjectAI，管理地穴魔机器的激活序列。
     */
    struct go_direbrew_mole_machineAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_direbrew_mole_machineAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 重置AI状态
         *
         * 重置地穴魔机器状态，并调度激活序列：
         * - 1秒后开启机器并施放出现效果
         * - 4秒后触发链接的陷阱
         *
         * 调用时机：游戏对象重置时
         */
        void Reset() override
        {
            me->SetLootState(GO_READY);  // 设置游戏对象状态为就绪
            _scheduler
                .Schedule(Seconds(1), [this](TaskContext /*context*/)
                {
                    // 1秒后：开启机器并施放出现效果
                    me->UseDoorOrButton(10000);  // 开启10秒
                    me->CastSpell(nullptr, SPELL_MOLE_MACHINE_EMERGE, true);
                })
                .Schedule(Seconds(4), [this](TaskContext /*context*/)
                {
                    // 4秒后：触发链接的陷阱
                    if (GameObject* trap = me->GetLinkedTrap())
                    {
                        trap->SetLootState(GO_ACTIVATED);
                        trap->UseDoorOrButton();
                    }
                });
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 更新任务调度器。
         *
         * 调用时机：每个游戏Tick
         */
        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);  // 更新任务调度器
        }

    private:
        TaskScheduler _scheduler;  ///< 任务调度器，用于管理激活序列
    };

    /**
     * @brief 获取AI实例
     * @param go 游戏对象指针
     * @return 返回地穴魔机器AI实例
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetBlackrockDepthsAI<go_direbrew_mole_machineAI>(go);
    }
};

/**
 * @brief 召唤地穴魔机器目标选择器法术脚本
 *
 * 法术ID: 47691
 * 随机选择一个目标并在其位置召唤地穴魔机器。
 */
class spell_direbrew_summon_mole_machine_target_picker : public SpellScript
{
    PrepareSpellScript(spell_direbrew_summon_mole_machine_target_picker);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 返回true表示验证通过
     *
     * 验证所需的法术是否存在。
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MOLE_MACHINE_MINION_SUMMONER });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引（未使用）
     *
     * 对被击中的目标施放地穴魔机器随从召唤者法术。
     */
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        // 对目标施放随从召唤者法术
        GetCaster()->CastSpell(GetHitUnit(), SPELL_MOLE_MACHINE_MINION_SUMMONER, true);
    }

    /**
     * @brief 注册法术效果
     *
     * 注册脚本效果处理函数。
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_direbrew_summon_mole_machine_target_picker::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 发送酒杯目标选择器法术脚本
 *
 * 法术ID: 47370
 * 选择一个没有黑暗酿酒师的酒增益的玩家发送酒杯。
 */
class spell_send_mug_target_picker : public SpellScript
{
    PrepareSpellScript(spell_send_mug_target_picker);

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 从目标列表中移除已有酒增益的玩家和上次发送目标的玩家，
     * 随机选择一个目标。
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        Unit* caster = GetCaster();

        // 移除已有黑暗酿酒师的酒增益的目标
        targets.remove_if(Trinity::UnitAuraCheck(true, SPELL_HAS_DARK_BREWMAIDENS_BREW));

        // 如果有多个目标，移除上次发送的目标
        if (targets.size() > 1)
            targets.remove_if([caster](WorldObject* obj)
        {
            if (obj->GetGUID() == caster->GetAI()->GetGUID(DATA_TARGET_GUID))
                return true;
            return false;
        });

        if (targets.empty())
            return;

        // 随机选择一个目标
        WorldObject* target = Trinity::Containers::SelectRandomContainerElement(targets);
        targets.clear();
        targets.push_back(target);
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 效果索引（未使用）
     *
     * 保存目标GUID并发送第一杯酒。
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        // 保存目标GUID以便下次排除
        caster->GetAI()->SetGUID(GetHitUnit()->GetGUID(), DATA_TARGET_GUID);
        // 发送第一杯酒
        caster->CastSpell(GetHitUnit(), SPELL_SEND_FIRST_MUG, true);
    }

    /**
     * @brief 注册法术效果
     *
     * 注册目标过滤和虚拟效果处理函数。
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_send_mug_target_picker::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
        OnEffectHitTarget += SpellEffectFn(spell_send_mug_target_picker::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 请求第二杯酒法术脚本
 *
 * 法术ID: 47344
 * 玩家使用此法术请求第二杯酒。
 */
class spell_request_second_mug : public SpellScript
{
    PrepareSpellScript(spell_request_second_mug);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 返回true表示验证通过
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SEND_SECOND_MUG });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引（未使用）
     *
     * 让目标向施法者发送第二杯酒。
     */
    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        // 目标向施法者发送第二杯酒
        GetHitUnit()->CastSpell(GetCaster(), SPELL_SEND_SECOND_MUG, true);
    }

    /**
     * @brief 注册法术效果
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_request_second_mug::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 发送酒杯控制光环脚本
 *
 * 法术ID: 47369
 * 伊尔萨的光环，周期性地选择目标发送酒杯。
 */
class spell_send_mug_control_aura : public AuraScript
{
    PrepareAuraScript(spell_send_mug_control_aura);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 返回true表示验证通过
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SEND_MUG_TARGET_PICKER });
    }

    /**
     * @brief 周期性触发
     * @param aurEff 光环效果（未使用）
     *
     * 每次周期触发时施放发送酒杯目标选择器法术。
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        // 施放发送酒杯目标选择器法术
        GetTarget()->CastSpell(GetTarget(), SPELL_SEND_MUG_TARGET_PICKER, true);
    }

    /**
     * @brief 注册光环效果
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_send_mug_control_aura::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

/**
 * @brief 装桶控制光环脚本
 *
 * 法术ID: 50278
 * 乌尔苏拉的光环，周期性地施放装桶法术。
 */
class spell_barreled_control_aura : public AuraScript
{
    PrepareAuraScript(spell_barreled_control_aura);

    /**
     * @brief 周期性触发
     * @param aurEff 光环效果（未使用）
     *
     * 每次周期触发时施放装桶法术。
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        PreventDefaultAction();  // 阻止默认行为
        GetTarget()->CastSpell(nullptr, SPELL_BARRELED, true);  // 施放装桶法术
    }

    /**
     * @brief 注册光环效果
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_barreled_control_aura::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 烈酒缴械光环脚本（预施法）
 *
 * 法术ID: 47407
 * 管理缴械技能的预施法阶段，逐渐增大缴械效果并最终施放缴械。
 */
class spell_direbrew_disarm : public AuraScript
{
    PrepareAuraScript(spell_direbrew_disarm);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 返回true表示验证通过
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DIREBREW_DISARM, SPELL_DIREBREW_DISARM_GROW });
    }

    /**
     * @brief 周期性触发
     * @param aurEff 光环效果（未使用）
     *
     * 每次周期触发时增加缴械成长效果的层数。
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        // 增加缴械成长效果的层数
        if (Aura* aura = GetTarget()->GetAura(SPELL_DIREBREW_DISARM_GROW))
        {
            aura->SetStackAmount(aura->GetStackAmount() + 1);  // 增加层数
            aura->SetDuration(aura->GetDuration() - 1500);      // 减少持续时间
        }
    }

    /**
     * @brief 应用光环效果
     * @param aurEff 光环效果（未使用）
     * @param mode 处理模式（未使用）
     *
     * 当光环应用时，施放缴械成长效果和缴械法术。
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 施放缴械成长效果和缴械法术
        GetTarget()->CastSpell(GetTarget(), SPELL_DIREBREW_DISARM_GROW, true);
        GetTarget()->CastSpell(GetTarget(), SPELL_DIREBREW_DISARM);
    }

    /**
     * @brief 注册光环效果
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_direbrew_disarm::PeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY);
        OnEffectApply += AuraEffectRemoveFn(spell_direbrew_disarm::OnApply, EFFECT_1, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册科林·烈酒脚本
 *
 * 将科林·烈酒及其相关NPC、游戏对象和法术脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_coren_direbrew()
{
    RegisterBlackrockDepthsCreatureAI(boss_coren_direbrew);              // 科林·烈酒
    RegisterBlackrockDepthsCreatureAI(npc_coren_direbrew_sisters);       // 科林的姐妹
    RegisterBlackrockDepthsCreatureAI(npc_direbrew_minion);              // 科林的随从
    RegisterBlackrockDepthsCreatureAI(npc_direbrew_antagonist);          // 对抗者
    new go_direbrew_mole_machine();                                      // 地穴魔机器
    RegisterSpellScript(spell_direbrew_summon_mole_machine_target_picker);  // 召唤地穴魔机器目标选择器
    RegisterSpellScript(spell_send_mug_target_picker);                   // 发送酒杯目标选择器
    RegisterSpellScript(spell_request_second_mug);                       // 请求第二杯酒
    RegisterSpellScript(spell_send_mug_control_aura);                    // 发送酒杯控制光环
    RegisterSpellScript(spell_barreled_control_aura);                    // 装桶控制光环
    RegisterSpellScript(spell_direbrew_disarm);                          // 烈酒缴械
}
