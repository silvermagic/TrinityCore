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
 * @file boss_shade_of_akama.cpp
 * @brief 阿卡玛之影Boss战脚本
 *
 * 本模块实现了阿卡玛之影的完整战斗逻辑，包括：
 * - 多阶段战斗：被束缚阶段和自由阶段
 * - 阿卡玛的协助战斗机制
 * - 灰舌引导者、法师、防御者、盗贼、元素师、灵魂绑定者的生成和AI
 * - 战斗结束后破碎者的剧情事件
 *
 * 战斗机制：
 * 1. 初始阶段：阿卡玛之影被灰舌引导者束缚，无法移动和攻击
 * 2. 玩家与阿卡玛对话后，阿卡玛会引导法术削弱束缚
 * 3. 杀死灰舌引导者后，阿卡玛之影解除束缚，进入正常战斗
 * 4. 期间会不断刷新灰舌增援怪物
 * 5. 击杀阿卡玛之影后，灰舌破碎者出现并进行剧情对话
 */

#include "ScriptMgr.h"
#include "black_temple.h"
#include "GridNotifiers.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 对白枚举
 */
enum Says
{
    // Akama
    SAY_BROKEN_FREE_0  = 0,  ///< 阿卡玛：破碎者自由了！
    SAY_BROKEN_FREE_1  = 1,  ///< 阿卡玛：光明即将来临...
    SAY_BROKEN_FREE_2  = 2,  ///< 阿卡玛：黑暗终结了...
    SAY_LOW_HEALTH     = 3,  ///< 阿卡玛：低血量警告
    SAY_DEAD           = 4,  ///< 阿卡玛：死亡
    // Ashtongue Broken
    SAY_BROKEN_SPECIAL = 0,  ///< 破碎者：特殊台词
    SAY_BROKEN_HAIL    = 1   ///< 破碎者：向阿卡玛致敬
};

/**
 * @brief 技能枚举
 */
enum Spells
{
    // Akama
    SPELL_STEALTH                    = 34189,  ///< 潜行
    SPELL_AKAMA_SOUL_CHANNEL         = 40447,  ///< 阿卡玛灵魂引导
    SPELL_FIXATE                     = 40607,  ///< 固定目标
    SPELL_CHAIN_LIGHTNING            = 39945,  ///< 闪电链
    SPELL_DESTRUCTIVE_POISON         = 40874,  ///< 毁灭毒药
    SPELL_AKAMA_SOUL_RETRIEVE        = 40902,  ///< 阿卡玛灵魂取回
    // Shade
    SPELL_THREAT                     = 41602,  ///< 威胁
    SPELL_SHADE_OF_AKAMA_TRIGGER     = 40955,  ///< 阿卡玛之影触发
    SPELL_AKAMA_SOUL_EXPEL_CHANNEL   = 40927,  ///< 阿卡玛灵魂驱逐引导
    // Ashtongue Channeler
    SPELL_SHADE_SOUL_CHANNEL         = 40401,  ///< 阿卡玛之影灵魂引导（服务器端）
    SPELL_SHADE_SOUL_CHANNEL_2       = 40520,  ///< 阿卡玛之影灵魂引导
    // Creature Spawner
    SPELL_ASHTONGUE_WAVE_B           = 42035,  ///< 灰舌波次B
    SPELL_SUMMON_ASHTONGUE_SORCERER  = 40476,  ///< 召唤灰舌法师
    SPELL_SUMMON_ASHTONGUE_DEFENDER  = 40474,  ///< 召唤灰舌防御者
    // Ashtongue Defender
    SPELL_DEBILITATING_STRIKE        = 41178,  ///< 致残打击
    SPELL_HEROIC_STRIKE              = 41975,  ///< 英勇打击
    SPELL_SHIELD_BASH                = 41180,  ///< 盾击
    SPELL_WINDFURY                   = 38229,  ///< 风怒
    // Ashtongue Rogue
    SPELL_DEBILITATING_POISON        = 41978,  ///< 致残毒药
    SPELL_EVISCERATE                 = 41177,  ///< 剔骨
    // Ashtongue Elementalist
    SPELL_RAIN_OF_FIRE               = 42023,  ///< 火焰之雨
    SPELL_LIGHTNING_BOLT             = 42024,  ///< 闪电箭
    // Ashtongue Spiritbinder
    SPELL_SPIRIT_MEND                = 42025,  ///< 灵魂治疗
    SPELL_CHAIN_HEAL                 = 42027,  ///< 治疗链
    SPELL_SPIRITBINDER_SPIRIT_HEAL   = 42317   ///< 灵魂治疗者治疗
};

/**
 * @brief 生物枚举
 */
enum Creatures
{
    NPC_ASHTONGUE_CHANNELER    = 23421,  ///< 灰舌引导者
    NPC_ASHTONGUE_BROKEN       = 23319,  ///< 灰舌破碎者
    NPC_CREATURE_SPAWNER_AKAMA = 23210   ///< 阿卡玛生物生成器
};

/**
 * @brief 动作枚举
 */
enum Actions
{
    ACTION_START_SPAWNING      = 0,  ///< 开始生成
    ACTION_STOP_SPAWNING       = 1,  ///< 停止生成
    ACTION_DESPAWN_ALL_SPAWNS  = 2,  ///< 消失所有生成物
    ACTION_SHADE_OF_AKAMA_DEAD = 3,  ///< 阿卡玛之影死亡
    ACTION_BROKEN_SPECIAL      = 4,  ///< 破碎者特殊动作
    ACTION_BROKEN_EMOTE        = 5,  ///< 破碎者表情
    ACTION_BROKEN_HAIL         = 6   ///< 破碎者致敬
};

/**
 * @brief 事件枚举
 */
enum Events
{
    // Akama
    EVENT_SHADE_START                    =  1,  ///< 阿卡玛之影开始
    EVENT_SHADE_CHANNEL                  =  2,  ///< 阿卡玛之影引导
    EVENT_FIXATE                         =  3,  ///< 固定
    EVENT_CHAIN_LIGHTNING                =  4,  ///< 闪电链
    EVENT_DESTRUCTIVE_POISON             =  5,  ///< 毁灭毒药
    EVENT_START_BROKEN_FREE              =  6,  ///< 开始破碎者自由
    EVENT_START_SOUL_RETRIEVE            =  7,  ///< 开始灵魂取回
    EVENT_EVADE_CHECK                    =  8,  ///< 脱战检查
    EVENT_BROKEN_FREE_1                  =  9,  ///< 破碎者自由1
    EVENT_BROKEN_FREE_2                  = 10,  ///< 破碎者自由2
    EVENT_BROKEN_FREE_3                  = 11,  ///< 破碎者自由3
    EVENT_BROKEN_FREE_4                  = 12,  ///< 破碎者自由4
    // Shade of Akama
    EVENT_INITIALIZE_SPAWNERS            = 13,  ///< 初始化生成器
    EVENT_START_CHANNELERS_AND_SPAWNERS  = 14,  ///< 启动引导者和生成器
    EVENT_ADD_THREAT                     = 15,  ///< 增加威胁
    // Creature spawner
    EVENT_SPAWN_WAVE_B                   = 16,  ///< 生成波次B
    EVENT_SUMMON_ASHTONGUE_SORCERER      = 17,  ///< 召唤灰舌法师
    EVENT_SUMMON_ASHTONGUE_DEFENDER      = 18,  ///< 召唤灰舌防御者
    // Ashtongue Defender
    EVENT_DEBILITATING_STRIKE            = 19,  ///< 致残打击
    EVENT_HEROIC_STRIKE                  = 20,  ///< 英勇打击
    EVENT_SHIELD_BASH                    = 21,  ///< 盾击
    EVENT_WINDFURY                       = 22,  ///< 风怒
    // Ashtongue Rogue
    EVENT_DEBILITATING_POISON            = 23,  ///< 致残毒药
    EVENT_EVISCERATE                     = 24,  ///< 剔骨
    // Ashtongue Elementalist
    EVENT_RAIN_OF_FIRE                   = 25,  ///< 火焰之雨
    EVENT_LIGHTNING_BOLT                 = 26,  ///< 闪电箭
    // Ashtongue Spiritbinder
    EVENT_SPIRIT_HEAL                    = 27,  ///< 灵魂治疗
    EVENT_SPIRIT_MEND_RESET              = 28,  ///< 灵魂治疗重置
    EVENT_CHAIN_HEAL_RESET               = 29   ///< 治疗链重置
};

/**
 * @brief 杂项枚举
 */
enum Misc
{
    AKAMA_CHANNEL_WAYPOINT = 0,   ///< 阿卡玛引导路径点
    AKAMA_INTRO_WAYPOINT   = 1,   ///< 阿卡玛介绍路径点
    SUMMON_GROUP_RESET     = 1    ///< 重置召唤组
};

/**
 * @brief 阿卡玛路径点坐标
 */
Position const AkamaWP[2] =
{
    { 517.4877f, 400.7993f, 112.7837f },  ///< 引导位置
    { 468.4435f, 401.1062f, 118.5379f }   ///< 介绍位置
};

/**
 * @brief 破碎者生成位置（18个）
 */
Position const BrokenPos[18] =
{
    { 495.5628f, 462.7089f, 112.8169f, 4.1808090f },
    { 498.3421f, 463.8384f, 112.8673f, 4.5634810f },
    { 501.6708f, 463.8806f, 112.8673f, 3.7157850f },
    { 532.4264f, 448.4718f, 112.8563f, 3.9813020f },
    { 532.9113f, 451.6227f, 112.8671f, 4.6479530f },
    { 532.8243f, 453.9475f, 112.8671f, 4.7032810f },
    { 521.5317f, 402.3790f, 112.8671f, 3.1138120f },
    { 521.9184f, 404.6848f, 112.8671f, 4.0787760f },
    { 522.4290f, 406.5160f, 112.8671f, 3.3869470f },
    { 521.0833f, 393.1852f, 112.8611f, 3.0750830f },
    { 521.9014f, 395.6381f, 112.8671f, 4.0157140f },
    { 522.2610f, 397.7423f, 112.8671f, 3.4417790f },
    { 532.4565f, 345.3987f, 112.8585f, 1.7232640f },
    { 532.5565f, 346.8792f, 112.8671f, 1.8325960f },
    { 532.5491f, 348.6840f, 112.8671f, 0.2054047f },
    { 501.4669f, 338.5967f, 112.8504f, 1.7038430f },
    { 499.0937f, 337.9894f, 112.8673f, 1.8586250f },
    { 496.8722f, 338.0152f, 112.8673f, 0.5428222f }
};

/**
 * @brief 破碎者路径点坐标（18个）
 */
Position const BrokenWP[18] =
{
    { 479.1884f, 434.8635f, 112.7838f },
    { 479.7349f, 435.9843f, 112.7838f },
    { 480.5328f, 436.8310f, 112.7838f },
    { 493.1714f, 420.1136f, 112.7838f },
    { 494.7830f, 417.4830f, 112.7838f },
    { 492.9280f, 423.1891f, 112.7838f },
    { 491.8618f, 403.2035f, 112.7838f },
    { 491.7784f, 400.2046f, 112.7838f },
    { 491.9451f, 406.2023f, 112.7838f },
    { 488.3535f, 395.3652f, 112.7838f },
    { 488.8324f, 392.3267f, 112.7838f },
    { 489.2300f, 398.3135f, 112.7838f },
    { 491.9286f, 383.0433f, 112.7838f },
    { 491.1526f, 380.0966f, 112.7839f },
    { 493.6747f, 385.5407f, 112.7838f },
    { 476.2499f, 369.0865f, 112.7839f },
    { 473.7637f, 367.8766f, 112.7839f },
    { 478.8986f, 370.1895f, 112.7839f }
};

/// 房间中心Y坐标，用于判断生成器位置
static float const MIDDLE_OF_ROOM    = 400.0f;
/// 面向门的角度
static float const FACE_THE_DOOR     = 0.08726646f;
/// 面向平台的角度
static float const FACE_THE_PLATFORM = 3.118662f;

/**
 * @struct boss_shade_of_akama
 * @brief 阿卡玛之影AI结构体
 *
 * 继承自BossAI，实现阿卡玛之影的战斗逻辑
 *
 * 战斗流程：
 * 1. 初始状态：被灰舌引导者束缚，免疫PC，无法交互
 * 2. 阿卡玛施放灵魂引导后，开始战斗
 * 3. 启动引导者和生成器，不断刷新增援
 * 4. 当接近阿卡玛时解除束缚，进入正常战斗
 * 5. 死亡后触发阿卡玛的灵魂取回事件
 */
struct boss_shade_of_akama : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    boss_shade_of_akama(Creature* creature) : BossAI(creature, DATA_SHADE_OF_AKAMA)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _spawners.clear();
        _isInPhaseOne = true;  // 初始处于第一阶段（被束缚）
    }

    /**
     * @brief 重置战斗
     *
     * 重置Boss状态：
     * - 设置免疫PC和无法交互标志
     * - 设置昏迷表情
     * - 初始化生成器
     * - 召唤重置组生物（灰舌引导者）
     */
    void Reset() override
    {
        _Reset();
        Initialize();
        me->SetImmuneToPC(true);
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        me->SetEmoteState(EMOTE_STATE_STUN);
        me->SetWalk(true);
        events.ScheduleEvent(EVENT_INITIALIZE_SPAWNERS, 1s);
        me->SummonCreatureGroup(SUMMON_GROUP_RESET);
    }

    /**
     * @brief 进入脱战模式
     * @param why 脱战原因
     *
     * 清除所有事件和召唤物，通知生成器消失所有生成物
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        events.Reset();
        summons.DespawnAll();

        for (ObjectGuid spawnerGuid : _spawners)
            if (Creature* spawner = ObjectAccessor::GetCreature(*me, spawnerGuid))
                spawner->AI()->DoAction(ACTION_DESPAWN_ALL_SPAWNS);

        _DespawnAtEvade();
    }

    /**
     * @brief 法术命中回调
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 处理关键法术：
     * - 灵魂引导：开始战斗，激活引导者和生成器
     * - 灵魂取回：施放灵魂驱逐引导
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_AKAMA_SOUL_CHANNEL)
        {
            events.ScheduleEvent(EVENT_START_CHANNELERS_AND_SPAWNERS, 1s);
            me->SetEmoteState(EMOTE_STATE_NONE);
            events.ScheduleEvent(EVENT_EVADE_CHECK, 10s);
            if (Creature* akama = instance->GetCreature(DATA_AKAMA_SHADE))
                AttackStart(akama);
        }

        if (spellInfo->Id == SPELL_AKAMA_SOUL_RETRIEVE)
            DoCastSelf(SPELL_AKAMA_SOUL_EXPEL_CHANNEL);
    }

    /**
     * @brief 移动通知回调
     * @param motionType 移动类型
     * @param pointId 路径点ID
     *
     * 当追上阿卡玛时，解除束缚状态，进入正常战斗
     */
    void MovementInform(uint32 motionType, uint32 /*pointId*/) override
    {
        if (_isInPhaseOne && motionType == CHASE_MOTION_TYPE)
        {
            _isInPhaseOne = false;
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            me->SetImmuneToPC(false);
            me->SetWalk(false);
            events.ScheduleEvent(EVENT_ADD_THREAT, Milliseconds(100));

            // 停止生成器生成新怪物
            for (ObjectGuid spawnerGuid : _spawners)
                if (Creature* spawner = ObjectAccessor::GetCreature(*me, spawnerGuid))
                    spawner->AI()->DoAction(ACTION_STOP_SPAWNING);
        }
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者
     *
     * 触发死亡事件：
     * - 施放阿卡玛之影触发法术
     * - 通知阿卡玛开始后续剧情
     * - 清除所有生成物
     */
    void JustDied(Unit* /*killer*/) override
    {
        DoCastSelf(SPELL_SHADE_OF_AKAMA_TRIGGER);

        if (Creature* akama = instance->GetCreature(DATA_AKAMA_SHADE))
            akama->AI()->DoAction(ACTION_SHADE_OF_AKAMA_DEAD);

        for (ObjectGuid spawnerGuid : _spawners)
            if (Creature* spawner = ObjectAccessor::GetCreature(*me, spawnerGuid))
                spawner->AI()->DoAction(ACTION_DESPAWN_ALL_SPAWNS);

        events.Reset();
        summons.DespawnEntry(NPC_ASHTONGUE_CHANNELER);
        instance->SetBossState(DATA_SHADE_OF_AKAMA, DONE);
    }

    /**
     * @brief 检查是否需要脱战
     *
     * 如果区域内没有存活的非GM玩家，则脱战
     */
    void EnterEvadeModeIfNeeded()
    {
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
            if (Player* player = i->GetSource())
                if (player->IsAlive() && !player->IsGameMaster() && IsInBoundary(player))
                    return;

        EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 处理战斗中的事件
     */
    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (!UpdateVictim())
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INITIALIZE_SPAWNERS:
                {
                    // 查找所有生成器并保存GUID
                    std::list<Creature*> SpawnerList;
                    me->GetCreatureListWithEntryInGrid(SpawnerList, NPC_CREATURE_SPAWNER_AKAMA);
                    for (Creature* spawner : SpawnerList)
                        _spawners.push_back(spawner->GetGUID());

                    break;
                }
                case EVENT_START_CHANNELERS_AND_SPAWNERS:
                {
                    // 激活所有引导者
                    for (ObjectGuid summonGuid : summons)
                        if (Creature* channeler = ObjectAccessor::GetCreature(*me, summonGuid))
                            channeler->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

                    // 启动所有生成器
                    for (ObjectGuid spawnerGuid : _spawners)
                        if (Creature* spawner = ObjectAccessor::GetCreature(*me, spawnerGuid))
                            spawner->AI()->DoAction(ACTION_START_SPAWNING);

                    break;
                }
                case EVENT_ADD_THREAT:
                    DoCast(SPELL_THREAT);
                    events.Repeat(Seconds(3) + Milliseconds(500));
                    break;
                case EVENT_EVADE_CHECK:
                    EnterEvadeModeIfNeeded();
                    events.Repeat(Seconds(10));
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    GuidVector _spawners;      ///< 生成器GUID列表
    bool _isInPhaseOne;        ///< 是否在第一阶段（被束缚）
};

/**
 * @struct npc_akama_shade
 * @brief 阿卡玛（阿卡玛之影战）AI结构体
 *
 * 继承自ScriptedAI，实现阿卡玛在阿卡玛之影战中的AI逻辑
 *
 * 战斗流程：
 * 1. 玩家与阿卡玛对话，开始事件
 * 2. 阿卡玛移动到平台，施放灵魂引导削弱束缚
 * 3. 对阿卡玛之影施放固定法术
 * 4. 被威胁法术命中后，进入战斗状态
 * 5. 阿卡玛之影死亡后，进行后续剧情：灵魂取回、召唤破碎者、台词
 */
struct npc_akama_shade : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    npc_akama_shade(Creature* creature) : ScriptedAI(creature), _summons(me)
    {
        Initialize();
        _instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _isInCombat = false;
        _hasYelledOnce = false;
        _chosen.Clear();
        _summons.DespawnAll();
        _events.Reset();
    }

    /**
     * @brief 重置AI
     *
     * 设置阿卡玛的初始状态：
     * - 阵营：灰舌死亡誓约
     * - 施放潜行
     * - 如果Boss未死，显示Gossip标志
     */
    void Reset() override
    {
        Initialize();
        me->SetFaction(FACTION_ASHTONGUE_DEATHSWORN);
        DoCastSelf(SPELL_STEALTH);

        if (_instance->GetBossState(DATA_SHADE_OF_AKAMA) != DONE)
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    /**
     * @brief 法术命中回调
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 当被威胁法术命中时，进入战斗状态
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_THREAT && !_isInCombat)
        {
            _isInCombat = true;
            me->SetWalk(false);
            me->RemoveAurasDueToSpell(SPELL_AKAMA_SOUL_CHANNEL);
            if (Creature* shade = _instance->GetCreature(DATA_SHADE_OF_AKAMA))
            {
                shade->RemoveAurasDueToSpell(SPELL_AKAMA_SOUL_CHANNEL);
                AttackStart(shade);
                _events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 2s);
                _events.ScheduleEvent(EVENT_DESTRUCTIVE_POISON, 5s);
            }
        }
    }

    /**
     * @brief 受到伤害回调
     * @param who 伤害来源
     * @param damage 伤害值
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 当血量低于20%时，播放低血量台词
     */
    void DamageTaken(Unit* /*who*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->HealthBelowPct(20) && !_hasYelledOnce)
        {
            _hasYelledOnce = true;
            Talk(SAY_LOW_HEALTH);
        }
    }

    /**
     * @brief 执行动作回调
     * @param actionId 动作ID
     *
     * 处理阿卡玛之影死亡后的后续事件
     */
    void DoAction(int32 actionId) override
    {
        if (actionId == ACTION_SHADE_OF_AKAMA_DEAD)
        {
            _isInCombat = false;
            me->CombatStop(true);
            me->SetFaction(FACTION_ASHTONGUE_DEATHSWORN);
            me->SetWalk(true);
            _events.Reset();
            me->GetMotionMaster()->MovePoint(AKAMA_INTRO_WAYPOINT, AkamaWP[1]);
        }
    }

    /**
     * @brief 移动通知回调
     * @param motionType 移动类型
     * @param pointId 路径点ID
     *
     * 处理到达路径点后的动作
     */
    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType != POINT_MOTION_TYPE)
            return;

        if (pointId == AKAMA_CHANNEL_WAYPOINT)
            _events.ScheduleEvent(EVENT_SHADE_CHANNEL, 1s);

        else if (pointId == AKAMA_INTRO_WAYPOINT)
        {
            me->SetWalk(false);
            _events.ScheduleEvent(EVENT_START_SOUL_RETRIEVE, 1s);
        }
    }

    /**
     * @brief 召唤破碎者
     *
     * 召唤18个灰舌破碎者，并让第10个破碎者说特殊台词
     */
    void SummonBrokens()
    {
        for (uint8 i = 0; i < 18; i++)
        {
            if (TempSummon* summoned = me->SummonCreature(NPC_ASHTONGUE_BROKEN, BrokenPos[i]))
            {
                summoned->SetWalk(true);
                summoned->GetMotionMaster()->MovePoint(0, BrokenWP[i]);
                // 根据抓包数据，第10个生成的NPC说"特殊"台词
                if (i == 9)
                    _chosen = summoned->GetGUID();
            }
        }
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 处理所有事件，包括战斗技能和后续剧情
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SHADE_START:
                    _instance->SetBossState(DATA_SHADE_OF_AKAMA, IN_PROGRESS);
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                    me->RemoveAurasDueToSpell(SPELL_STEALTH);
                    me->SetWalk(true);
                    me->GetMotionMaster()->MovePoint(AKAMA_CHANNEL_WAYPOINT, AkamaWP[0], false);
                    break;
                case EVENT_SHADE_CHANNEL:
                    me->SetFacingTo(FACE_THE_PLATFORM);
                    DoCastSelf(SPELL_AKAMA_SOUL_CHANNEL);
                    me->SetFaction(FACTION_MONSTER_SPAR_BUDDY);
                    _events.ScheduleEvent(EVENT_FIXATE, 5s);
                    break;
                case EVENT_FIXATE:
                    DoCast(SPELL_FIXATE);
                    break;
                case EVENT_CHAIN_LIGHTNING:
                    DoCastVictim(SPELL_CHAIN_LIGHTNING);
                    _events.Repeat(Seconds(8), Seconds(15));
                    break;
                case EVENT_DESTRUCTIVE_POISON:
                    DoCastSelf(SPELL_DESTRUCTIVE_POISON);
                    _events.Repeat(Seconds(3), Seconds(7));
                    break;
                case EVENT_START_SOUL_RETRIEVE:
                    me->SetFacingTo(FACE_THE_DOOR);
                    DoCast(SPELL_AKAMA_SOUL_RETRIEVE);
                    _events.ScheduleEvent(EVENT_START_BROKEN_FREE, 15s);
                    break;
                case EVENT_START_BROKEN_FREE:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                    Talk(SAY_BROKEN_FREE_0);
                    SummonBrokens();
                    _events.ScheduleEvent(EVENT_BROKEN_FREE_1, Seconds(10));
                    break;
                case EVENT_BROKEN_FREE_1:
                    Talk(SAY_BROKEN_FREE_1);
                    _events.ScheduleEvent(EVENT_BROKEN_FREE_2, Seconds(12));
                    break;
                case EVENT_BROKEN_FREE_2:
                    Talk(SAY_BROKEN_FREE_2);
                    _events.ScheduleEvent(EVENT_BROKEN_FREE_3, Seconds(15));
                    break;
                case EVENT_BROKEN_FREE_3:
                    if (Creature* special = ObjectAccessor::GetCreature(*me, _chosen))
                        special->AI()->Talk(SAY_BROKEN_SPECIAL);

                    _summons.DoAction(ACTION_BROKEN_EMOTE, _pred);
                    _events.ScheduleEvent(EVENT_BROKEN_FREE_4, Seconds(5));
                    break;
                case EVENT_BROKEN_FREE_4:
                    _summons.DoAction(ACTION_BROKEN_HAIL, _pred);
                    break;
                default:
                    break;
            }
        }

        if (me->GetFaction() == FACTION_MONSTER_SPAR_BUDDY)
        {
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者
     */
    void JustDied(Unit* /*killer*/) override
    {
        _summons.DespawnAll();
        Talk(SAY_DEAD);
        if (Creature* shade = _instance->GetCreature(DATA_SHADE_OF_AKAMA))
            if (shade->IsAlive())
                shade->AI()->EnterEvadeMode(EVADE_REASON_OTHER);
    }

    /**
     * @brief Gossip选择回调
     * @param player 玩家指针
     * @param menuId 菜单ID
     * @param gossipListId Gossip列表ID
     * @return 是否处理
     *
     * 玩家选择Gossip选项后开始事件
     */
    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        if (gossipListId == 0)
        {
            CloseGossipMenuFor(player);
            _events.ScheduleEvent(EVENT_SHADE_START, Milliseconds(500));
        }
        return false;
    }

private:
    InstanceScript* _instance;              ///< 副本实例脚本
    EventMap _events;                       ///< 事件映射
    SummonList _summons;                    ///< 召唤列表
    DummyEntryCheckPredicate _pred;         ///< 虚拟条目检查谓词
    ObjectGuid _chosen;                     ///< 被选中说特殊台词的破碎者GUID
    bool _isInCombat;                       ///< 是否在战斗中
    bool _hasYelledOnce;                    ///< 是否已经说过一次台词
};

struct npc_ashtongue_channeler : public PassiveAI
{
    npc_ashtongue_channeler(Creature* creature) : PassiveAI(creature)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override
    {
        _scheduler.Schedule(Seconds(2), [this](TaskContext channel)
        {
            if (Creature* shade = _instance->GetCreature(DATA_SHADE_OF_AKAMA))
            {
                if (shade->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE))
                    DoCastSelf(SPELL_SHADE_SOUL_CHANNEL);

                else
                    me->DespawnOrUnsummon(Seconds(3));
            }

            channel.Repeat(Seconds(2));
        });
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    InstanceScript* _instance;
    TaskScheduler _scheduler;
};

struct npc_creature_generator_akama : public ScriptedAI
{
    npc_creature_generator_akama(Creature* creature) : ScriptedAI(creature), _summons(me)
    {
        Initialize();
    }

    void Initialize()
    {
        _leftSide = false;
        _events.Reset();
        _summons.DespawnAll();
    }

    void Reset() override
    {
        Initialize();

        if (me->GetPositionY() < MIDDLE_OF_ROOM)
            _leftSide = true;
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
    }

    void DoAction(int32 actionId) override
    {
        switch (actionId)
        {
            case ACTION_START_SPAWNING:
                if (_leftSide)
                {
                    _events.ScheduleEvent(EVENT_SPAWN_WAVE_B, Milliseconds(100));
                    _events.ScheduleEvent(EVENT_SUMMON_ASHTONGUE_SORCERER, 2s, 5s);
                }
                else
                {
                    _events.ScheduleEvent(EVENT_SPAWN_WAVE_B, 10s);
                    _events.ScheduleEvent(EVENT_SUMMON_ASHTONGUE_DEFENDER, 2s, 5s);
                }
                break;
            case ACTION_STOP_SPAWNING:
                _events.Reset();
                break;
            case ACTION_DESPAWN_ALL_SPAWNS:
                _events.Reset();
                _summons.DespawnAll();
                break;
            default:
                break;
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SPAWN_WAVE_B:
                    DoCastSelf(SPELL_ASHTONGUE_WAVE_B);
                    _events.Repeat(Seconds(50), Seconds(60));
                    break;
                case EVENT_SUMMON_ASHTONGUE_SORCERER: // left
                    DoCastSelf(SPELL_SUMMON_ASHTONGUE_SORCERER);
                    _events.Repeat(Seconds(30), Seconds(35));
                    break;
                case EVENT_SUMMON_ASHTONGUE_DEFENDER: // right
                    DoCastSelf(SPELL_SUMMON_ASHTONGUE_DEFENDER);
                    _events.Repeat(Seconds(30), Seconds(40));
                    break;
                default:
                    break;
            }
        }
    }

private:
    EventMap _events;
    SummonList _summons;
    bool _leftSide;
};

struct npc_ashtongue_sorcerer : public ScriptedAI
{
    npc_ashtongue_sorcerer(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        _instance = creature->GetInstanceScript();
    }

    void Initialize()
    {
        _switchToCombat = false;
        _inBanish = false;
    }

    void Reset() override
    {
        if (Creature* shade = _instance->GetCreature(DATA_SHADE_OF_AKAMA))
        {
            if (shade->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE))
                me->GetMotionMaster()->MovePoint(0, shade->GetPosition());

            else if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
                AttackStart(akama);
        }
        Initialize();
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(Seconds(5));
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }

    void AttackStart(Unit* who) override
    {
        if (!_switchToCombat)
            return;

        ScriptedAI::AttackStart(who);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_inBanish && who->GetGUID() == _instance->GetGuidData(DATA_SHADE_OF_AKAMA) && me->IsWithinDist(who, 20.0f, false))
        {
            _inBanish = true;
            me->StopMoving();
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MovePoint(1, me->GetPositionX() + frand(-8.0f, 8.0f), me->GetPositionY() + frand(-8.0f, 8.0f), me->GetPositionZ());

            _scheduler.Schedule(Seconds(1) + Milliseconds(500), [this](TaskContext sorcer_channel)
            {
                if (Creature* shade = _instance->GetCreature(DATA_SHADE_OF_AKAMA))
                {
                    if (shade->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE))
                    {
                        me->SetFacingToObject(shade);
                        DoCastSelf(SPELL_SHADE_SOUL_CHANNEL);
                        sorcer_channel.Repeat(Seconds(2));
                    }
                    else
                    {
                        me->InterruptSpell(CURRENT_CHANNELED_SPELL);
                        _switchToCombat = true;
                        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
                            AttackStart(akama);
                    }
                }
            });
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    TaskScheduler _scheduler;
    bool _switchToCombat;
    bool _inBanish;
};

struct npc_ashtongue_defender : public ScriptedAI
{
    npc_ashtongue_defender(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override
    {
        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
            AttackStart(akama);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(Seconds(5));
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_HEROIC_STRIKE, 5s);
        _events.ScheduleEvent(EVENT_SHIELD_BASH, 10s, 16s);
        _events.ScheduleEvent(EVENT_DEBILITATING_STRIKE, 10s, 16s);
        _events.ScheduleEvent(EVENT_WINDFURY, 8s, 12s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DEBILITATING_STRIKE:
                    DoCastVictim(SPELL_DEBILITATING_STRIKE);
                    _events.Repeat(Seconds(20), Seconds(25));
                    break;
                case EVENT_HEROIC_STRIKE:
                    DoCastSelf(SPELL_HEROIC_STRIKE);
                    _events.Repeat(Seconds(5), Seconds(15));
                    break;
                case EVENT_SHIELD_BASH:
                    DoCastVictim(SPELL_SHIELD_BASH);
                    _events.Repeat(Seconds(10), Seconds(20));
                    break;
                case EVENT_WINDFURY:
                    DoCastVictim(SPELL_WINDFURY);
                    _events.Repeat(Seconds(6), Seconds(8));
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    EventMap _events;
};

struct npc_ashtongue_rogue : public ScriptedAI
{
    npc_ashtongue_rogue(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override
    {
        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
            AttackStart(akama);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(Seconds(5));
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_DEBILITATING_POISON, Milliseconds(500), Seconds(2));
        _events.ScheduleEvent(EVENT_EVISCERATE, 2s, 5s);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DEBILITATING_POISON:
                    DoCastVictim(SPELL_DEBILITATING_POISON);
                    _events.Repeat(Seconds(15), Seconds(20));
                    break;
                case EVENT_EVISCERATE:
                    DoCastVictim(SPELL_EVISCERATE);
                    _events.Repeat(Seconds(12), Seconds(20));
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    EventMap _events;
};

struct npc_ashtongue_elementalist : public ScriptedAI
{
    npc_ashtongue_elementalist(Creature* creature) : ScriptedAI(creature)
    {
        _instance = creature->GetInstanceScript();
    }

    void Reset() override
    {
        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
            AttackStart(akama);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(Seconds(5));
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_RAIN_OF_FIRE, 18s);
        _events.ScheduleEvent(EVENT_LIGHTNING_BOLT, 6s);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_RAIN_OF_FIRE:
                    DoCastVictim(SPELL_RAIN_OF_FIRE);
                    _events.Repeat(Seconds(15), Seconds(20));
                    break;
                case EVENT_LIGHTNING_BOLT:
                    DoCastVictim(SPELL_LIGHTNING_BOLT);
                    _events.Repeat(Seconds(8), Seconds(15));
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    EventMap _events;
};

struct npc_ashtongue_spiritbinder : public ScriptedAI
{
    npc_ashtongue_spiritbinder(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        _instance = creature->GetInstanceScript();
    }

    void Initialize()
    {
        _spiritMend = false;
        _chainHeal = false;
    }

    void Reset() override
    {
        Initialize();

        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
            AttackStart(akama);
    }

    void JustDied(Unit* /*killer*/) override
    {
        me->DespawnOrUnsummon(Seconds(5));
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_SPIRIT_HEAL, 5s, 6s);
    }

    void DamageTaken(Unit* /*who*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!_spiritMend)
            if (HealthBelowPct(30))
            {
                DoCastSelf(SPELL_SPIRIT_MEND);
                _spiritMend = true;
                _events.ScheduleEvent(EVENT_SPIRIT_MEND_RESET, 10s, 15s);
            }

        if (!_chainHeal)
            if (HealthBelowPct(50))
            {
                DoCastSelf(SPELL_CHAIN_HEAL);
                _chainHeal = true;
                _events.ScheduleEvent(EVENT_CHAIN_HEAL_RESET, 10s, 15s);
            }

    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SPIRIT_HEAL:
                    DoCastSelf(SPELL_SPIRITBINDER_SPIRIT_HEAL);
                    _events.Repeat(Seconds(13), Seconds(16));
                    break;
                case EVENT_SPIRIT_MEND_RESET:
                    _spiritMend = false;
                    break;
                case EVENT_CHAIN_HEAL_RESET:
                    _chainHeal = false;
                    break;
                default:
                    break;
            }
        }

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;
    EventMap _events;
    bool _spiritMend;
    bool _chainHeal;
};

struct npc_ashtongue_broken : public ScriptedAI
{
    npc_ashtongue_broken(Creature* creature) : ScriptedAI(creature)
    {
        _instance = me->GetInstanceScript();
    }

    void MovementInform(uint32 motionType, uint32 /*pointId*/) override
    {
        if (motionType != POINT_MOTION_TYPE)
            return;

        if (Creature* akama = _instance->GetCreature(DATA_AKAMA_SHADE))
            me->SetFacingToObject(akama);
    }

    void DoAction(int32 actionId) override
    {
        switch (actionId)
        {
            case ACTION_BROKEN_SPECIAL:
                Talk(SAY_BROKEN_SPECIAL);
                break;
            case ACTION_BROKEN_HAIL:
                me->SetFaction(FACTION_ASHTONGUE_DEATHSWORN);
                Talk(SAY_BROKEN_HAIL);
                break;
            case ACTION_BROKEN_EMOTE:
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
                break;
            default:
                break;
        }
    }

private:
    InstanceScript* _instance;
};

// 40401 - Shade Soul Channel (serverside spell)
class spell_shade_soul_channel_serverside : public AuraScript
{
    PrepareAuraScript(spell_shade_soul_channel_serverside);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADE_SOUL_CHANNEL_2 });
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAuraFromStack(SPELL_SHADE_SOUL_CHANNEL_2);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_shade_soul_channel_serverside::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 40520 - Shade Soul Channel
class spell_shade_soul_channel : public AuraScript
{
    PrepareAuraScript(spell_shade_soul_channel);

    void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        int32 const maxSlowEff = -99;
        if (aurEff->GetAmount() < maxSlowEff)
            if (AuraEffect* slowEff = GetEffect(EFFECT_0))
                slowEff->ChangeAmount(maxSlowEff);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_shade_soul_channel::OnApply, EFFECT_0, SPELL_AURA_MOD_DECREASE_SPEED, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

void AddSC_boss_shade_of_akama()
{
    RegisterBlackTempleCreatureAI(boss_shade_of_akama);
    RegisterBlackTempleCreatureAI(npc_akama_shade);
    RegisterBlackTempleCreatureAI(npc_ashtongue_channeler);
    RegisterBlackTempleCreatureAI(npc_creature_generator_akama);
    RegisterBlackTempleCreatureAI(npc_ashtongue_sorcerer);
    RegisterBlackTempleCreatureAI(npc_ashtongue_defender);
    RegisterBlackTempleCreatureAI(npc_ashtongue_rogue);
    RegisterBlackTempleCreatureAI(npc_ashtongue_elementalist);
    RegisterBlackTempleCreatureAI(npc_ashtongue_spiritbinder);
    RegisterBlackTempleCreatureAI(npc_ashtongue_broken);
    RegisterSpellScript(spell_shade_soul_channel_serverside);
    RegisterSpellScript(spell_shade_soul_channel);
}
