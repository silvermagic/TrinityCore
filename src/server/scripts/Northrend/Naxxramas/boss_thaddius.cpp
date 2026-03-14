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
 * @file boss_thaddius.cpp
 * @brief 纳克萨玛斯副本 - 塔迪乌斯 Boss 战斗脚本模块
 *
 * 本模块实现了 Boss 塔迪乌斯 (Thaddius) 的完整战斗逻辑，包括：
 * - 第一阶段：斯塔拉格和费尔根两个随从的战斗
 * - 过渡阶段：两个随从死亡后，特斯拉线圈充能并激活塔迪乌斯
 * - 第二阶段：塔迪乌斯本体战斗，极性转换机制
 * - "休克！"成就判定
 *
 * 塔迪乌斯战斗特点：
 * - 战斗分为多个阶段，先击杀两个随从（斯塔拉格和费尔根）
 * - 两个随从死亡后会有 5 秒的复活窗口期，需要同时击杀
 * - 随从死亡后，塔迪乌斯被特斯拉线圈激活
 * - 塔迪乌斯会周期性施放极性转换，给玩家赋予正负电荷
 * - 相同电荷的玩家站在一起会增加伤害，不同电荷会造成伤害
 * - "休克！"成就要求：没有任何玩家触碰相反电荷的玩家
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "naxxramas.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_NOT_ENGAGED       = 1,    // 战斗尚未开始
    PHASE_PETS,                     // 随从阶段（斯塔拉格和费尔根）
    PHASE_TRANSITION,               // 过渡阶段（塔迪乌斯激活中）
    PHASE_THADDIUS                  // 塔迪乌斯本体阶段
};

/**
 * @brief AI 动作 ID 枚举，用于 Boss 和随从之间的通信
 */
enum AIActions
{
    ACTION_BEGIN_RESET_ENCOUNTER =  0,  // 塔迪乌斯发送给随从，触发重生和战斗重置
    ACTION_FEUGEN_DIED,                  // 费尔根死亡时发送给塔迪乌斯
    ACTION_STALAGG_DIED,                 // 斯塔拉格死亡时发送给塔迪乌斯
    ACTION_FEUGEN_RESET,                 // 费尔根发送给塔迪乌斯（重置）
    ACTION_STALAGG_RESET,                // 斯塔拉格发送给塔迪乌斯（重置）
    ACTION_FEUGEN_AGGRO,                 // 费尔根开战时发送给塔迪乌斯
    ACTION_STALAGG_AGGRO,                // 斯塔拉格开战时发送给塔迪乌斯
    ACTION_FEUGEN_REVIVING_FX,           // 塔迪乌斯发送给费尔根（复活特效）
    ACTION_STALAGG_REVIVING_FX,          // 塔迪乌斯发送给斯塔拉格（复活特效）
    ACTION_FEUGEN_REVIVED,               // 塔迪乌斯发送给费尔根（复活）
    ACTION_STALAGG_REVIVED,              // 塔迪乌斯发送给斯塔拉格（复活）
    ACTION_TRANSITION,                   // 塔迪乌斯发送给随从（过渡阶段开始，线圈过载动画）
    ACTION_TRANSITION_2,                 // 塔迪乌斯发送给随从（线圈电击塔迪乌斯）
    ACTION_TRANSITION_3,                 // 塔迪乌斯发送给随从（生成后禁用线圈游戏对象）

    ACTION_POLARITY_CROSSED              // 触发成就失败，由法术脚本发送
};

/**
 * @brief 战斗事件 ID 枚举，用于事件调度系统
 */
enum Events
{
    EVENT_SHIFT = 1,                // 极性转换施放事件
    EVENT_SHIFT_TALK,               // 极性转换喊话事件（用于施法完成时的喊话）
    EVENT_CHAIN,                    // 闪电链施放事件
    EVENT_BERSERK,                  // 狂暴计时器事件
    EVENT_REVIVE_FEUGEN,            // 费尔根复活计时器（如果斯塔拉格还活着）
    EVENT_REVIVE_STALAGG,           // 斯塔拉格复活计时器（如果费尔根还活着）
    EVENT_TRANSITION_1,             // 过载表情计时器
    EVENT_TRANSITION_2,             // 线圈电击塔迪乌斯计时器
    EVENT_TRANSITION_3,             // 塔迪乌斯可攻击计时器
    EVENT_ENGAGE,                   // 塔迪乌斯进入战斗计时器
    EVENT_ENABLE_BALL_LIGHTNING     // 塔迪乌斯开怪后的宽限期，之后开始对范围外目标投掷球状闪电
};

/**
 * @brief 其他数据常量枚举
 */
enum Misc
{
    MAX_POLARITY_10M        =  5,    // 10人模式极性转换的最大目标数
    MAX_POLARITY_25M        = 13,    // 25人模式极性转换的最大目标数

    DATA_POLARITY_CROSSED   =  1,    // 极性交叉数据标识（用于成就判定）
};

/**
 * @brief 随从（费尔根和斯塔拉格）台词 ID 枚举
 */
enum PetYells
{
    SAY_STALAGG_AGGRO       = 0,    // 斯塔拉格开战台词
    SAY_STALAGG_SLAY        = 1,    // 斯塔拉格击杀台词
    SAY_STALAGG_DEATH       = 2,    // 斯塔拉格死亡台词

    SAY_FEUGEN_AGGRO        = 0,    // 费尔根开战台词
    SAY_FEUGEN_SLAY         = 1,    // 费尔根击杀台词
    SAY_FEUGEN_DEATH        = 2,    // 费尔根死亡台词

    EMOTE_FEIGN_DEATH       = 3,    // 假死表情
    EMOTE_FEIGN_REVIVE      = 4,    // 假死复活表情

    EMOTE_TESLA_LINK_BREAKS = 0,    // 特斯拉连接断开表情
    EMOTE_TESLA_OVERLOAD    = 1     // 特斯拉过载表情
};

/**
 * @brief 随从法术 ID 枚举
 */
enum PetSpells
{
    SPELL_STALAGG_POWERSURGE        = 28134,   // 斯塔拉格的能量涌动
    //SPELL_STALAGG_TESLA             = 28097,
    SPELL_STALAGG_TESLA_PERIODIC    = 28098,   // 斯塔拉格特斯拉周期性效果
    SPELL_STALAGG_CHAIN_VISUAL      = 28096,   // 斯塔拉格链条视觉效果

    SPELL_FEUGEN_STATICFIELD        = 28135,   // 费尔根静电场
    //SPELL_FEUGEN_TESLA              = 28109,
    SPELL_FEUGEN_TESLA_PERIODIC     = 28110,   // 费尔根特斯拉周期性效果
    SPELL_FEUGEN_CHAIN_VISUAL       = 28111,   // 费尔根链条视觉效果

    SPELL_MAGNETIC_PULL             = 54517,   // 磁力牵引（交换坦克位置）
    SPELL_MAGNETIC_PULL_EFFECT      = 28337,   // 磁力牵引效果

    // @hack 费尔根/斯塔拉格在 P1 阶段抓取坦克后使用此技能，防止 mmaps 将它们引导出平台
    // 来自未来的开发者，如果你读到这个并且房间内的 mmaps 已经修复，请删除此 hackfix
    SPELL_ROOT_SELF                 = 75215,   // 自我定身

    SPELL_TESLA_SHOCK               = 28099    // 特斯拉电击
};

/**
 * @brief 随从其他常量枚举
 */
enum PetMisc
{
    OVERLOAD_DISTANCE       = 28    // 过载距离（超出此距离会断开特斯拉连接）
};

/**
 * @brief 塔迪乌斯台词 ID 枚举
 */
enum ThaddiusYells
{
    SAY_GREET               = 0,    // 问候台词（未开战时）
    SAY_AGGRO               = 1,    // 开战台词
    SAY_SLAY                = 2,    // 击杀台词
    SAY_ELECT               = 3,    // 电击台词
    SAY_DEATH               = 4,    // 死亡台词
    SAY_SCREAM              = 5,    // 尖叫台词

    EMOTE_POLARITY_SHIFTED  = 6     // 极性转换表情
};

/**
 * @brief 塔迪乌斯法术 ID 枚举
 */
enum ThaddiusSpells
{
    SPELL_THADDIUS_INACTIVE_VISUAL  = 28160,   // 塔迪乌斯非激活状态视觉效果
    SPELL_THADDIUS_SPARK_VISUAL     = 28136,   // 塔迪乌斯火花视觉效果
    SPELL_SHOCK_VISUAL              = 28159,   // 电击视觉效果

    SPELL_BALL_LIGHTNING            = 28299,   // 球状闪电（对范围外目标施放）
    SPELL_CHAIN_LIGHTNING           = 28167,   // 闪电链
    SPELL_BERSERK                   = 27680,   // 狂暴

    // 极性处理相关法术
    SPELL_POLARITY_SHIFT            = 28089,   // 极性转换

    SPELL_POSITIVE_CHARGE_APPLY     = 28059,   // 正电荷应用
    SPELL_POSITIVE_CHARGE_TICK      = 28062,   // 正电荷周期性伤害
    SPELL_POSITIVE_CHARGE_AMP       = 29659,   // 正电荷增益

    SPELL_NEGATIVE_CHARGE_APPLY     = 28084,   // 负电荷应用
    SPELL_NEGATIVE_CHARGE_TICK      = 28085,   // 负电荷周期性伤害
    SPELL_NEGATIVE_CHARGE_AMP       = 29660,   // 负电荷增益
};

/**
 * @brief 塔迪乌斯 Boss AI 结构体
 *
 * 继承自 BossAI，实现塔迪乌斯的完整战斗逻辑。
 * 塔迪乌斯战斗的核心机制是多阶段战斗和极性转换。
 */
struct boss_thaddius : public BossAI
{
public:
    /**
     * @brief 构造函数
     * @param creature Boss 生物对象指针
     */
    boss_thaddius(Creature* creature) : BossAI(creature, BOSS_THADDIUS), stalaggAlive(true), feugenAlive(true), ballLightningUnlocked(false), ballLightningEnabled(false), shockingEligibility(true) {}

    /**
     * @brief 初始化 AI
     *
     * 在 AI 创建时调用，设置初始状态。
     * 如果 Boss 尚未击杀，设置为未开战阶段并禁用移动。
     *
     * @调用时机 AI 创建时
     */
    void InitializeAI() override
    {
        if (instance->GetBossState(BOSS_THADDIUS) != DONE)
        {
            events.SetPhase(PHASE_NOT_ENGAGED);  // 设置为未开战阶段
            SetCombatMovement(false);             // 禁用战斗移动
        }
    }

    /**
     * @brief 击杀单位时的回调函数
     * @param victim 被击杀的单位
     *
     * 当塔迪乌斯击杀玩家时，播放击杀台词。
     *
     * @调用时机 Boss 击杀任何单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);                       // 播放击杀台词
    }

    /**
     * @brief 重置 Boss 状态（空实现）
     *
     * 塔迪乌斯的重置逻辑在 ResetEncounter 和 BeginResetEncounter 中处理。
     */
    void Reset() override { }

    /**
     * @brief 进入躲避模式
     * @param why 躲避原因
     *
     * 当战斗异常时（如没有敌对目标），处理战斗重置逻辑。
     * 特殊处理球状闪电机制。
     *
     * @调用时机 Boss 脱离战斗时
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        // 如果球状闪电尚未启用且因为没有敌对目标进入躲避模式，先启用球状闪电再试
        if (!ballLightningEnabled && why == EVADE_REASON_NO_HOSTILES)
        {
            ballLightningEnabled = true;
            return; // try again
        }
        // 如果在过渡阶段或塔迪乌斯阶段，开始重置战斗
        if (events.IsInPhase(PHASE_TRANSITION) || (events.IsInPhase(PHASE_THADDIUS) && me->IsAlive()))
            BeginResetEncounter();
    }

    /**
     * @brief 检查 AI 是否可以攻击目标
     * @param who 目标单位
     * @return 如果可以攻击返回 true
     *
     * 只有在球状闪电启用或目标在近战范围内时才允许攻击。
     *
     * @调用时机 AI 选择攻击目标时
     */
    bool CanAIAttack(Unit const* who) const override
    {
        if (ballLightningEnabled || me->IsWithinMeleeRange(who))
            return BossAI::CanAIAttack(who);
        else
            return false;
    }

    /**
     * @brief Boss 出现时的回调函数
     *
     * 当塔迪乌斯生成时调用，初始化战斗状态。
     *
     * @调用时机 Boss 生成时
     */
    void JustAppeared() override
    {
        if (instance->GetBossState(BOSS_THADDIUS) != DONE)
            ResetEncounter();                     // 重置战斗状态
    }

    /**
     * @brief Boss 死亡时的回调函数
     * @param killer 击杀者（未使用）
     *
     * 当塔迪乌斯死亡时调用，执行清理工作并播放死亡台词。
     * 同时将所有相关生物（包括两个随从）设置为非活跃状态。
     *
     * @调用时机 Boss 生命值降为 0 时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();                              // 调用父类死亡处理
        me->setActive(false);                     // 设置为非活跃状态
        me->SetFarVisible(false);                 // 设置为远距离不可见
        // 将斯塔拉格设置为非活跃状态
        if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
        {
            stalagg->setActive(false);
            stalagg->SetFarVisible(false);
        }
        // 将费尔根设置为非活跃状态
        if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
        {
            feugen->setActive(false);
            feugen->SetFarVisible(false);
        }
        Talk(SAY_DEATH);                          // 播放死亡台词
    }

    /**
     * @brief 处理 AI 动作
     * @param action 动作 ID
     *
     * 处理来自随从和法术脚本的各种动作消息。
     * 主要处理：
     * - 随从重置和开战
     * - 随从死亡和复活
     * - 极性交叉（成就失败）
     *
     * @调用时机 其他 AI 或法术脚本发送动作消息时
     */
    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_FEUGEN_RESET:
            case ACTION_STALAGG_RESET:
                // 随从重置，如果不在未开战阶段，开始重置战斗
                if (!events.IsInPhase(PHASE_NOT_ENGAGED))
                    BeginResetEncounter();
                break;

            case ACTION_FEUGEN_AGGRO:
            case ACTION_STALAGG_AGGRO:
                // 随从开战，如果已经在战斗中则忽略
                if (!events.IsInPhase(PHASE_NOT_ENGAGED))
                    return;
                events.SetPhase(PHASE_PETS);      // 切换到随从阶段

                shockingEligibility = true;        // 重置"休克！"成就资格

                // 检查前置条件
                if (!instance->CheckRequiredBosses(BOSS_THADDIUS))
                {
                    BeginResetEncounter();
                    return;
                }
                instance->SetBossState(BOSS_THADDIUS, IN_PROGRESS);

                // 将 Boss 和随从设置为活跃状态
                me->setActive(true);
                me->SetFarVisible(true);
                DoZoneInCombat();                 // 将范围内玩家拉入战斗
                if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                {
                    stalagg->setActive(true);
                    stalagg->SetFarVisible(true);
                }
                if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                {
                    feugen->setActive(true);
                    feugen->SetFarVisible(true);
                }
                break;

            case ACTION_FEUGEN_DIED:
                // 费尔根死亡
                if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                    feugen->AI()->DoAction(ACTION_FEUGEN_REVIVING_FX);  // 播放复活特效
                feugenAlive = false;
                // 如果斯塔拉格还活着，5秒后复活费尔根；否则进入过渡阶段
                if (stalaggAlive)
                    events.ScheduleEvent(EVENT_REVIVE_FEUGEN, 5s, 0, PHASE_PETS);
                else
                    Transition();                 // 两个随从都死亡，进入过渡阶段

                break;

            case ACTION_STALAGG_DIED:
                // 斯塔拉格死亡
                if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                    stalagg->AI()->DoAction(ACTION_STALAGG_REVIVING_FX);  // 播放复活特效
                stalaggAlive = false;
                // 如果费尔根还活着，5秒后复活斯塔拉格；否则进入过渡阶段
                if (feugenAlive)
                    events.ScheduleEvent(EVENT_REVIVE_STALAGG, 5s, 0, PHASE_PETS);
                else
                    Transition();                 // 两个随从都死亡，进入过渡阶段

                break;

            case ACTION_POLARITY_CROSSED:
                // 极性交叉，标记"休克！"成就失败
                shockingEligibility = false;
                break;

            default:
                break;
        }
    }

    /**
     * @brief 获取 Boss 自定义数据
     * @param id 数据类型标识
     * @return 如果是极性交叉查询且成就仍然可能，返回 1；否则返回 0
     *
     * @调用时机 成就系统检查是否完成"休克！"成就时
     */
    uint32 GetData(uint32 id) const override
    {
        return (id == DATA_POLARITY_CROSSED && shockingEligibility) ? 1u : 0u;
    }

    /**
     * @brief 启动过渡阶段
     *
     * 当两个随从都死亡后，启动从随从阶段到塔迪乌斯阶段的过渡。
     * 移除不可交互标志，并调度过渡事件序列。
     *
     * @调用时机 两个随从都死亡时
     */
    void Transition()
    {
        events.SetPhase(PHASE_TRANSITION);        // 切换到过渡阶段

        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

        // 调度过渡事件序列：10秒后过载，12秒后电击，14秒后激活
        events.ScheduleEvent(EVENT_TRANSITION_1, 10s, 0, PHASE_TRANSITION);
        events.ScheduleEvent(EVENT_TRANSITION_2, 12s, 0, PHASE_TRANSITION);
        events.ScheduleEvent(EVENT_TRANSITION_3, 14s, 0, PHASE_TRANSITION);
    }

    /**
     * @brief 开始重置战斗
     *
     * 当战斗需要重置时调用，清理战斗状态并重置所有生物。
     * 移除玩家身上的极性光环，生成 Boss 和随从。
     *
     * @调用时机 战斗重置、躲避模式、随从重置时
     */
    void BeginResetEncounter()
    {
        if (instance->GetBossState(BOSS_THADDIUS) == DONE)
            return;

        // 移除玩家身上的极性转换减益效果
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_POSITIVE_CHARGE_APPLY);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_NEGATIVE_CHARGE_APPLY);

        me->DespawnOrUnsummon(0s, 30s);           // 生成 Boss（30秒后重生）

        // 重置 Boss 状态
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_STUNNED);
        me->SetImmuneToPC(true);
        me->setActive(false);
        me->SetFarVisible(false);

        // 通知随从重置
        if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
            feugen->AI()->DoAction(ACTION_BEGIN_RESET_ENCOUNTER);
        if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
            stalagg->AI()->DoAction(ACTION_BEGIN_RESET_ENCOUNTER);
    }

    /**
     * @brief 重置战斗状态
     *
     * 初始化战斗状态，重置随从存活标志，生成随从。
     *
     * @调用时机 Boss 生成时
     */
    void ResetEncounter()
    {
        feugenAlive = true;                       // 重置费尔根存活标志
        stalaggAlive = true;                      // 重置斯塔拉格存活标志

        _Reset();                                 // 调用父类重置函数
        events.SetPhase(PHASE_NOT_ENGAGED);       // 设置为未开战阶段
        me->SetReactState(REACT_PASSIVE);         // 设置为被动反应状态

        // @todo 这些家伙应该被移到召唤组 - 这只是一个 hack 让它们在 dynamic_spawning 中工作
        instance->instance->Respawn(SPAWN_TYPE_CREATURE, 130958); // Stalagg
        instance->instance->Respawn(SPAWN_TYPE_CREATURE, 130959); // Feugen
    }

    /**
     * @brief Boss AI 主更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 这是 Boss AI 的核心逻辑循环，每帧调用一次。
     * 处理事件调度、阶段转换、技能施放等所有战斗逻辑。
     *
     * 主要处理的事件：
     * - EVENT_REVIVE_FEUGEN/STALAGG: 随从复活
     * - EVENT_TRANSITION_1/2/3: 过渡阶段事件序列
     * - EVENT_ENABLE_BALL_LIGHTNING: 启用球状闪电
     * - EVENT_ENGAGE: 进入战斗
     * - EVENT_SHIFT: 极性转换
     * - EVENT_CHAIN: 闪电链
     * - EVENT_BERSERK: 狂暴
     *
     * @调用时机 每帧（服务器 tick）调用一次，频率约为 50ms
     * @性能注意 频繁调用，需要保持代码简洁高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果在未开战阶段，直接返回
        if (events.IsInPhase(PHASE_NOT_ENGAGED))
            return;
        // 如果在塔迪乌斯阶段且没有有效目标，直接返回
        if (events.IsInPhase(PHASE_THADDIUS) && !UpdateVictim())
            return;

        // 更新事件调度器
        events.Update(diff);
        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_REVIVE_FEUGEN:
                    // 复活费尔根
                    feugenAlive = true;
                    if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                        feugen->AI()->DoAction(ACTION_FEUGEN_REVIVED);
                    break;

                case EVENT_REVIVE_STALAGG:
                    // 复活斯塔拉格
                    stalaggAlive = true;
                    if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                        stalagg->AI()->DoAction(ACTION_STALAGG_REVIVED);
                    break;

                case EVENT_TRANSITION_1:
                    // 特斯拉线圈过载
                    if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                        feugen->AI()->DoAction(ACTION_TRANSITION);
                    if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                        stalagg->AI()->DoAction(ACTION_TRANSITION);
                    break;

                case EVENT_TRANSITION_2:
                    // 特斯拉线圈电击塔迪乌斯
                    me->CastSpell(me, SPELL_THADDIUS_SPARK_VISUAL, true);
                    if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                        feugen->AI()->DoAction(ACTION_TRANSITION_2);
                    if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                        stalagg->AI()->DoAction(ACTION_TRANSITION_2);
                    break;

                case EVENT_TRANSITION_3:
                    // 塔迪乌斯激活，开始本体战斗
                    me->CastSpell(me, SPELL_THADDIUS_SPARK_VISUAL, true);
                    ballLightningUnlocked = false;                // 先禁用球状闪电
                    me->RemoveAura(SPELL_THADDIUS_INACTIVE_VISUAL);
                    me->SetImmuneToPC(false);                     // 移除对玩家免疫
                    DoZoneInCombat();                             // 将范围内玩家拉入战斗

                    // 通知随从执行过渡阶段最后一步
                    if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
                        feugen->AI()->DoAction(ACTION_TRANSITION_3);
                    if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                        stalagg->AI()->DoAction(ACTION_TRANSITION_3);

                    events.SetPhase(PHASE_THADDIUS);              // 切换到塔迪乌斯阶段

                    Talk(SAY_AGGRO);                              // 播放开战台词

                    // 调度塔迪乌斯阶段的技能事件
                    events.ScheduleEvent(EVENT_ENGAGE, 2s, 0, PHASE_THADDIUS);
                    events.ScheduleEvent(EVENT_ENABLE_BALL_LIGHTNING, 5s, 0, PHASE_THADDIUS);
                    events.ScheduleEvent(EVENT_SHIFT, 10s, 0, PHASE_THADDIUS);
                    events.ScheduleEvent(EVENT_CHAIN, 10s, 20s, 0, PHASE_THADDIUS);
                    events.ScheduleEvent(EVENT_BERSERK, 6min, 0, PHASE_THADDIUS);

                    break;

                case EVENT_ENABLE_BALL_LIGHTNING:
                    // 启用球状闪电（用于攻击范围外目标）
                    ballLightningUnlocked = true;
                    break;

                case EVENT_ENGAGE:
                    // 进入战斗，设置主动攻击状态
                    me->SetReactState(REACT_AGGRESSIVE);
                    break;

                case EVENT_SHIFT:
                    // 极性转换：给所有玩家赋予正负电荷
                    me->CastStop();                               // 极性转换优先级最高，停止其他施法
                    DoCastAOE(SPELL_POLARITY_SHIFT);
                    events.ScheduleEvent(EVENT_SHIFT_TALK, 3s, PHASE_THADDIUS);
                    events.ScheduleEvent(EVENT_SHIFT, 30s, PHASE_THADDIUS);
                    break;

                case EVENT_SHIFT_TALK:
                    // 极性转换完成后的台词
                    Talk(SAY_ELECT);
                    Talk(EMOTE_POLARITY_SHIFTED);
                    break;

                case EVENT_CHAIN:
                    // 闪电链：如果正在施放极性转换，延迟3秒
                    if (me->FindCurrentSpellBySpellId(SPELL_POLARITY_SHIFT))
                        events.Repeat(Seconds(3));
                    else
                    {
                        me->CastStop();
                        DoCastVictim(SPELL_CHAIN_LIGHTNING);
                        events.Repeat(randtime(Seconds(10), Seconds(20)));
                    }
                    break;

                case EVENT_BERSERK:
                    // 狂暴：6分钟后进入狂暴状态
                    me->CastStop();
                    DoCast(me, SPELL_BERSERK);
                    break;

                default:
                    break;
            }
        }

        // 塔迪乌斯阶段的攻击逻辑
        if (events.IsInPhase(PHASE_THADDIUS) && !me->HasUnitState(UNIT_STATE_CASTING) && me->isAttackReady())
        {
            if (me->IsWithinMeleeRange(me->GetVictim()))
            {
                // 目标在近战范围内，执行近战攻击
                ballLightningEnabled = false;
                DoMeleeAttackIfReady();
            }
            else if (ballLightningUnlocked)
            {
                // 目标不在近战范围内且球状闪电已解锁，施放球状闪电
                if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                    DoCast(target, SPELL_BALL_LIGHTNING);
            }
        }
    }

private:
    bool stalaggAlive;              // 斯塔拉格是否存活
    bool feugenAlive;               // 费尔根是否存活
    bool ballLightningUnlocked;     // 球状闪电是否已解锁（初始球状闪电宽限期已过，应开始无情攻击）
    bool ballLightningEnabled;      // 球状闪电是否已启用（当由于近战范围内无合格目标而尝试躲避时切换为 true）
    bool shockingEligibility;       // "休克！"成就资格（true = 尚未触碰相反电荷）
};

struct npc_stalagg : public ScriptedAI
{
public:
    npc_stalagg(Creature* creature) : ScriptedAI(creature),
        instance(creature->GetInstanceScript()), powerSurgeTimer(), _myCoil(ObjectGuid::Empty), _myCoilGO(ObjectGuid::Empty), isOverloading(false), refreshBeam(false), isFeignDeath(false)
    {
        instance = creature->GetInstanceScript();
        SetBoundary(instance->GetBossBoundary(BOSS_THADDIUS));
    }

    void InitializeAI() override
    {
        if (GameObject* coil = myCoilGO())
            coil->SetGoState(GO_STATE_ACTIVE);

        powerSurgeTimer = 10 * IN_MILLISECONDS;

        // force tesla coil state refresh
        refreshBeam = true;
    }

    void EnterEvadeMode(EvadeReason /*reason*/) override
    {
        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_STALAGG_RESET);
    }

    void BeginResetEncounter()
    {
        if (GameObject* coil = myCoilGO())
            coil->SetGoState(GO_STATE_READY);
        me->DespawnOrUnsummon(0s, 7_days); // will be force respawned by thaddius
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_BEGIN_RESET_ENCOUNTER:
                BeginResetEncounter();
                break;
            case ACTION_STALAGG_REVIVING_FX:
                break;
            case ACTION_STALAGG_REVIVED:
                if (!isFeignDeath)
                    break;

                me->SetFullHealth();
                me->SetStandState(UNIT_STAND_STATE_STAND);
                me->SetReactState(REACT_AGGRESSIVE);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(false, UNIT_STATE_ROOT);
                Talk(EMOTE_FEIGN_REVIVE);
                isFeignDeath = false;

                refreshBeam = true; // force beam refresh

                DoZoneInCombat();
                if (!me->IsEngaged())
                    BeginResetEncounter();
                break;
            case ACTION_TRANSITION:
                me->KillSelf(); // true death

                if (Creature* coil = myCoil())
                {
                    coil->CastStop();
                    coil->AI()->Talk(EMOTE_TESLA_OVERLOAD);
                }
                break;
            case ACTION_TRANSITION_2:
                if (Creature* coil = myCoil())
                    if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
                        coil->CastSpell(thaddius, SPELL_SHOCK_VISUAL);
                break;
            case ACTION_TRANSITION_3:
                if (GameObject* coil = myCoilGO())
                    coil->SetGoState(GO_STATE_READY);
                me->DespawnOrUnsummon(0s, 7_days);
                break;
            default:
                break;
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_STALAGG_SLAY);
    }

    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_STALAGG_AGGRO);

        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_STALAGG_AGGRO);

        if (Creature* feugen = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FEUGEN)))
            if (!feugen->IsEngaged())
                AddThreat(who, 0.0f, feugen);
    }

    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage < me->GetHealth())
            return;

        if (isFeignDeath) // don't take damage while feigning death
        {
            damage = 0;
            return;
        }

        isFeignDeath = true;
        isOverloading = false;

        Talk(EMOTE_FEIGN_DEATH);
        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_STALAGG_DIED);

        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        me->RemoveAllAuras();
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetControlled(true, UNIT_STATE_ROOT);
        me->SetStandState(UNIT_STAND_STATE_DEAD);

        damage = me->GetHealth()-1;

        // force beam refresh as we just removed auras
        refreshBeam = true;
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Creature* creatureCaster = caster->ToCreature();
        if (!creatureCaster)
            return;

        if (spellInfo->Id != SPELL_STALAGG_TESLA_PERIODIC)
            return;
        if (!isFeignDeath && me->IsInCombat() && !me->GetHomePosition().IsInDist(me, OVERLOAD_DISTANCE))
        {
            if (!isOverloading)
            {
                isOverloading = true;
                creatureCaster->SetImmuneToPC(false);
                creatureCaster->AI()->Talk(EMOTE_TESLA_LINK_BREAKS);
                me->RemoveAura(SPELL_STALAGG_CHAIN_VISUAL);
            }
            if (Unit* target = SelectTarget(SelectTargetMethod::Random))
            {
                creatureCaster->CastStop(SPELL_TESLA_SHOCK);
                creatureCaster->CastSpell(target, SPELL_TESLA_SHOCK,true);
            }
        }
        else if (isOverloading || refreshBeam)
        {
            isOverloading = false;
            refreshBeam = false;
            creatureCaster->CastStop();
            creatureCaster->CastSpell(me, SPELL_STALAGG_CHAIN_VISUAL, true);
            creatureCaster->SetImmuneToPC(true);
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (!isFeignDeath)
            if (!UpdateVictim())
                return;

        if (powerSurgeTimer <= uiDiff)
        {
            if (isFeignDeath) // delay until potential revive
                powerSurgeTimer = 0u;
            else
            {
                DoCast(me, SPELL_STALAGG_POWERSURGE);
                powerSurgeTimer = urandms(25, 30);
            }
        }
        else
            powerSurgeTimer -= uiDiff;

        if (!isFeignDeath)
            DoMeleeAttackIfReady();
    }

private:
    Creature* myCoil()
    {
        Creature* coil = nullptr;
        if (_myCoil)
            coil = ObjectAccessor::GetCreature(*me, _myCoil);
        if (!coil)
        {
            coil = me->FindNearestCreature(NPC_TESLA, 1000.0f, true);
            if (coil)
            {
                _myCoil = coil->GetGUID();
                coil->SetReactState(REACT_PASSIVE);
            }
        }
        return coil;
    }

    GameObject* myCoilGO()
    {
        GameObject* coil = nullptr;
        if (_myCoilGO)
            coil = ObjectAccessor::GetGameObject(*me, _myCoilGO);
        if (!coil)
        {
            coil = me->FindNearestGameObject(GO_CONS_NOX_TESLA_STALAGG, 1000.0f);
            if (coil)
                _myCoilGO = coil->GetGUID();
        }
        return coil;
    }

    InstanceScript* instance;

    uint32 powerSurgeTimer;

    ObjectGuid _myCoil;
    ObjectGuid _myCoilGO;
    bool isOverloading;
    bool refreshBeam;
    bool isFeignDeath;
};

struct npc_feugen : public ScriptedAI
{
public:
    npc_feugen(Creature* creature) : ScriptedAI(creature),
        instance(creature->GetInstanceScript()), magneticPullTimer(), staticFieldTimer(), _myCoil(ObjectGuid::Empty), _myCoilGO(ObjectGuid::Empty), isOverloading(false), refreshBeam(false), isFeignDeath(false)
    {
        instance = creature->GetInstanceScript();
        SetBoundary(instance->GetBossBoundary(BOSS_THADDIUS));
    }

    void InitializeAI() override
    {
        if (GameObject* coil = myCoilGO())
            coil->SetGoState(GO_STATE_ACTIVE);

        staticFieldTimer = 6 * IN_MILLISECONDS;
        magneticPullTimer = 20 * IN_MILLISECONDS;

        // force coil state to refresh
        refreshBeam = true;
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_FEUGEN_RESET);
    }

    void BeginResetEncounter()
    {
        if (GameObject* coil = myCoilGO())
            coil->SetGoState(GO_STATE_READY);
        me->DespawnOrUnsummon(0s, 7_days); // will be force respawned by thaddius
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_BEGIN_RESET_ENCOUNTER:
                BeginResetEncounter();
                break;
            case ACTION_FEUGEN_REVIVING_FX:
                break;
            case ACTION_FEUGEN_REVIVED:
                if (!isFeignDeath)
                    break;

                me->SetFullHealth();
                me->SetStandState(UNIT_STAND_STATE_STAND);
                me->SetReactState(REACT_AGGRESSIVE);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(false, UNIT_STATE_ROOT);
                Talk(EMOTE_FEIGN_REVIVE);
                isFeignDeath = false;

                refreshBeam = true; // force beam refresh

                if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
                    if (stalagg->GetVictim())
                    {
                        AddThreat(stalagg->EnsureVictim(), 0.0f);
                        me->SetInCombatWith(stalagg->EnsureVictim());
                    }
                staticFieldTimer = 6 * IN_MILLISECONDS;
                magneticPullTimer = 30 * IN_MILLISECONDS;
                break;
            case ACTION_TRANSITION:
                me->KillSelf(); // true death this time around

                if (Creature* coil = myCoil())
                {
                    coil->CastStop();
                    coil->AI()->Talk(EMOTE_TESLA_OVERLOAD);
                }
                break;
            case ACTION_TRANSITION_2:
                if (Creature* coil = myCoil())
                    if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
                        coil->CastSpell(thaddius, SPELL_SHOCK_VISUAL);
                break;
            case ACTION_TRANSITION_3:
                if (GameObject* coil = myCoilGO())
                    coil->SetGoState(GO_STATE_READY);
                me->DespawnOrUnsummon(0s, 7_days);
                break;
            default:
                break;
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_FEUGEN_SLAY);
    }

    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_FEUGEN_AGGRO);

        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_FEUGEN_AGGRO);

        if (Creature* stalagg = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STALAGG)))
            if (!stalagg->IsInCombat())
                AddThreat(who, 0.0f, stalagg);
    }

    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage < me->GetHealth())
            return;

        if (isFeignDeath) // don't take damage while feigning death
        {
            damage = 0;
            return;
        }

        isFeignDeath = true;
        isOverloading = false;

        Talk(EMOTE_FEIGN_DEATH);
        if (Creature* thaddius = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THADDIUS)))
            thaddius->AI()->DoAction(ACTION_FEUGEN_DIED);

        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        me->RemoveAllAuras();
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetControlled(true, UNIT_STATE_ROOT);
        me->SetStandState(UNIT_STAND_STATE_DEAD);

        damage = me->GetHealth()-1;

        // force beam refresh as we just removed auras
        refreshBeam = true;
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Creature* creatureCaster = caster->ToCreature();
        if (!creatureCaster)
            return;

        if (spellInfo->Id != SPELL_FEUGEN_TESLA_PERIODIC)
            return;

        if (!isFeignDeath && me->IsInCombat() && !me->GetHomePosition().IsInDist(me, OVERLOAD_DISTANCE))
        {
            if (!isOverloading)
            {
                isOverloading = true;
                creatureCaster->SetImmuneToPC(false);
                creatureCaster->AI()->Talk(EMOTE_TESLA_LINK_BREAKS);
                me->RemoveAura(SPELL_STALAGG_CHAIN_VISUAL);
            }
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                creatureCaster->CastStop(SPELL_TESLA_SHOCK);
                creatureCaster->CastSpell(target, SPELL_TESLA_SHOCK,true);
            }
        }
        else if (isOverloading || refreshBeam)
        {
            isOverloading = false;
            refreshBeam = false;
            creatureCaster->CastStop();
            creatureCaster->CastSpell(me, SPELL_FEUGEN_CHAIN_VISUAL, true);
            creatureCaster->SetImmuneToPC(true);
        }
    }

    void UpdateAI(uint32 uiDiff) override
    {
        if (isFeignDeath)
            return;
        if (!UpdateVictim())
            return;

        if (magneticPullTimer <= uiDiff)
        {
            DoCast(me, SPELL_MAGNETIC_PULL);
            magneticPullTimer = 20 * IN_MILLISECONDS;
        }
        else magneticPullTimer -= uiDiff;

        if (staticFieldTimer <= uiDiff)
        {
            DoCast(me, SPELL_FEUGEN_STATICFIELD);
            staticFieldTimer = 6 * IN_MILLISECONDS;
        }
        else staticFieldTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }

private:
    Creature* myCoil()
    {
        Creature* coil = nullptr;
        if (_myCoil)
            coil = ObjectAccessor::GetCreature(*me, _myCoil);
        if (!coil)
        {
            coil = me->FindNearestCreature(NPC_TESLA, 1000.0f, true);
            if (coil)
            {
                _myCoil = coil->GetGUID();
                coil->SetReactState(REACT_PASSIVE);
            }
        }
        return coil;
    }

    GameObject* myCoilGO()
    {
        GameObject* coil = nullptr;
        if (_myCoilGO)
            coil = ObjectAccessor::GetGameObject(*me, _myCoilGO);
        if (!coil)
        {
            coil = me->FindNearestGameObject(GO_CONS_NOX_TESLA_FEUGEN, 1000.0f);
            if (coil)
                _myCoilGO = coil->GetGUID();
        }
        return coil;
    }
    InstanceScript* instance;

    uint32 magneticPullTimer;
    uint32 staticFieldTimer;

    ObjectGuid _myCoil;
    ObjectGuid _myCoilGO;

    bool isOverloading;
    bool refreshBeam;
    bool isFeignDeath;
};

struct npc_tesla : public ScriptedAI
{
    npc_tesla(Creature* creature) : ScriptedAI(creature) { }

    void EnterEvadeMode(EvadeReason /*why*/) override { } // never stop casting due to evade
    void UpdateAI(uint32 /*diff*/) override { } // never do anything unless told
    void JustEngagedWith(Unit* /*who*/) override { }
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override { damage = 0; } // no, you can't kill it
};

// 28062 - Positive Charge
// 28085 - Negative Charge
class spell_thaddius_polarity_charge : public SpellScript
{
    PrepareSpellScript(spell_thaddius_polarity_charge);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_POLARITY_SHIFT,
            SPELL_POSITIVE_CHARGE_APPLY,
            SPELL_POSITIVE_CHARGE_TICK,
            SPELL_POSITIVE_CHARGE_AMP,
            SPELL_NEGATIVE_CHARGE_APPLY,
            SPELL_NEGATIVE_CHARGE_TICK,
            SPELL_NEGATIVE_CHARGE_AMP
        });
    }

    void HandleTargets(std::list<WorldObject*>& targetList)
    {
        if (!GetTriggeringSpell())
            return;

        uint32 triggeringId = GetTriggeringSpell()->Id;
        uint32 ampId;
        switch (triggeringId)
        {
            case SPELL_POSITIVE_CHARGE_APPLY:
                ampId = SPELL_POSITIVE_CHARGE_AMP;
                break;
            case SPELL_NEGATIVE_CHARGE_APPLY:
                ampId = SPELL_NEGATIVE_CHARGE_AMP;
                break;
            default:
                return;
        }

        uint8 maxStacks = 0;
        if (GetCaster())
            switch (GetCaster()->GetMap()->GetDifficulty())
            {
                case RAID_DIFFICULTY_10MAN_NORMAL:
                    maxStacks = MAX_POLARITY_10M;
                    break;
                case RAID_DIFFICULTY_25MAN_NORMAL:
                    maxStacks = MAX_POLARITY_25M;
                    break;
                default:
                    break;
            }

        uint8 stacksCount = 1; // do we get a stack for our own debuff?
        std::list<WorldObject*>::iterator it = targetList.begin();
        while(it != targetList.end())
        {
            if ((*it)->GetTypeId() != TYPEID_PLAYER)
            {
                it = targetList.erase(it);
                continue;
            }
            if ((*it)->ToPlayer()->HasAura(triggeringId))
            {
                it = targetList.erase(it);
                if (stacksCount < maxStacks)
                    stacksCount++;
                continue;
            }

            // this guy will get hit - achievement failure trigger
            if (Creature* thaddius = (*it)->FindNearestCreature(NPC_THADDIUS, 200.0f))
                thaddius->AI()->DoAction(ACTION_POLARITY_CROSSED);

            ++it;
        }

        if (GetCaster() && GetCaster()->ToPlayer())
        {
            if (!GetCaster()->ToPlayer()->HasAura(ampId))
                GetCaster()->ToPlayer()->AddAura(ampId, GetCaster());
            GetCaster()->ToPlayer()->SetAuraStack(ampId, GetCaster(), stacksCount);
        }
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_thaddius_polarity_charge::HandleTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ALLY);
    }
};

// 28089 - Polarity Shift
class spell_thaddius_polarity_shift : public SpellScript
{
    PrepareSpellScript(spell_thaddius_polarity_shift);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_POLARITY_SHIFT,
            SPELL_POSITIVE_CHARGE_APPLY,
            SPELL_POSITIVE_CHARGE_TICK,
            SPELL_POSITIVE_CHARGE_AMP,
            SPELL_NEGATIVE_CHARGE_APPLY,
            SPELL_NEGATIVE_CHARGE_TICK,
            SPELL_NEGATIVE_CHARGE_AMP
        });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (target->GetTypeId() == TYPEID_PLAYER)
            {
                if (roll_chance_i(50))
                { // positive
                    target->CastSpell(target, SPELL_POSITIVE_CHARGE_APPLY, true);
                    target->RemoveAura(SPELL_POSITIVE_CHARGE_AMP);
                }
                else
                { // negative
                    target->CastSpell(target, SPELL_NEGATIVE_CHARGE_APPLY, true);
                    target->RemoveAura(SPELL_NEGATIVE_CHARGE_AMP);
                }
            }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_thaddius_polarity_shift::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 54517 - Magnetic Pull
class spell_thaddius_magnetic_pull : public SpellScript
{
    PrepareSpellScript(spell_thaddius_magnetic_pull);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_MAGNETIC_PULL });
    }

    void HandleCast() // only feugen ever casts this according to wowhead data
    {
        Unit* feugen = GetCaster();
        if (!feugen || feugen->GetEntry() != NPC_FEUGEN)
            return;

        Unit* stalagg = ObjectAccessor::GetCreature(*feugen, feugen->GetInstanceScript()->GetGuidData(DATA_STALAGG));
        if (!stalagg)
            return;

        ThreatManager& feugenThreat = feugen->GetThreatManager();
        ThreatManager& stalaggThreat = stalagg->GetThreatManager();

        Unit* feugenTank = feugenThreat.GetCurrentVictim();
        Unit* stalaggTank = stalaggThreat.GetCurrentVictim();

        if (!feugenTank || !stalaggTank)
            return;

        if (feugenTank == stalaggTank) // special behavior if the tanks are the same (taken from retail)
        {
            float feugenTankThreat = feugenThreat.GetThreat(feugenTank);
            float stalaggTankThreat = stalaggThreat.GetThreat(stalaggTank);

            feugen->GetThreatManager().AddThreat(feugenTank, stalaggTankThreat - feugenTankThreat, nullptr, true, true);
            stalagg->GetThreatManager().AddThreat(stalaggTank, feugenTankThreat - stalaggTankThreat, nullptr, true, true);

            feugen->CastSpell(stalaggTank, SPELL_MAGNETIC_PULL_EFFECT, true);
        }
        else // normal case, two tanks
        {
            float feugenTankThreat = feugenThreat.GetThreat(feugenTank);
            float feugenOtherThreat = feugenThreat.GetThreat(stalaggTank);
            float stalaggTankThreat = stalaggThreat.GetThreat(stalaggTank);
            float stalaggOtherThreat = stalaggThreat.GetThreat(feugenTank);

            // set the two entries in feugen's threat table to be equal to the ones in stalagg's
            feugen->GetThreatManager().AddThreat(stalaggTank, stalaggTankThreat - feugenOtherThreat, nullptr, true, true);
            feugen->GetThreatManager().AddThreat(feugenTank, stalaggOtherThreat - feugenTankThreat, nullptr, true, true);

            // set the two entries in stalagg's threat table to be equal to the ones in feugen's
            stalagg->GetThreatManager().AddThreat(feugenTank, feugenTankThreat - stalaggOtherThreat, nullptr, true, true);
            stalagg->GetThreatManager().AddThreat(stalaggTank, feugenOtherThreat - stalaggTankThreat, nullptr, true, true);

            // pull the two tanks across
            feugenTank->CastSpell(stalaggTank, SPELL_MAGNETIC_PULL_EFFECT, true);
            stalaggTank->CastSpell(feugenTank, SPELL_MAGNETIC_PULL_EFFECT, true);

            // @hack prevent mmaps clusterfucks from breaking tesla while tanks are midair
            feugen->AddAura(SPELL_ROOT_SELF, feugen);
            stalagg->AddAura(SPELL_ROOT_SELF, stalagg);

            // and make both attack their respective new tanks
            if (feugen->GetAI())
                feugen->GetAI()->AttackStart(stalaggTank);
            if (stalagg->GetAI())
                stalagg->GetAI()->AttackStart(feugenTank);
        }
    }

    void Register() override
    {
        OnCast += SpellCastFn(spell_thaddius_magnetic_pull::HandleCast);
    }
};

class at_thaddius_entrance : public OnlyOnceAreaTriggerScript
{
    public:
        at_thaddius_entrance() : OnlyOnceAreaTriggerScript("at_thaddius_entrance") { }

        bool TryHandleOnce(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
        {
            InstanceScript* instance = player->GetInstanceScript();
            if (!instance || instance->GetBossState(BOSS_THADDIUS) == DONE)
                return true;

            if (Creature* thaddius = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_THADDIUS)))
                thaddius->AI()->Talk(SAY_GREET);

            return true;
        }
};

/**
 * @brief 塔迪乌斯"休克！"成就脚本
 *
 * 实现"休克！"成就的判定逻辑。
 * 成就要求：没有任何玩家触碰相反电荷的玩家。
 */
class achievement_thaddius_shocking : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_thaddius_shocking() : AchievementCriteriaScript("achievement_thaddius_shocking") { }

        /**
         * @brief 检查是否满足成就条件
         * @param source 玩家对象（未使用）
         * @param target 目标单位（应为塔迪乌斯 Boss）
         * @return 如果满足成就条件返回 true，否则返回 false
         *
         * 通过查询塔迪乌斯 AI 的 shockingEligibility 标志来判断是否有玩家触碰相反电荷。
         *
         * @调用时机 成就系统检查该成就是否完成时
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            return target && target->GetAI() && target->GetAI()->GetData(DATA_POLARITY_CROSSED);
        }
};

/**
 * @brief 注册塔迪乌斯 Boss 脚本
 *
 * 将所有塔迪乌斯相关的脚本注册到系统中，包括：
 * - Boss AI（塔迪乌斯、斯塔拉格、费尔根、特斯拉线圈）
 * - 法术脚本（极性电荷、极性转换、磁力牵引）
 * - 区域触发脚本（入口问候）
 * - 成就脚本
 *
 * @调用时机 服务器启动时，由脚本加载系统自动调用
 */
void AddSC_boss_thaddius()
{
    RegisterNaxxramasCreatureAI(boss_thaddius);         // 注册塔迪乌斯 Boss AI
    RegisterNaxxramasCreatureAI(npc_stalagg);           // 注册斯塔拉格 AI
    RegisterNaxxramasCreatureAI(npc_feugen);            // 注册费尔根 AI
    RegisterNaxxramasCreatureAI(npc_tesla);             // 注册特斯拉线圈 AI

    RegisterSpellScript(spell_thaddius_polarity_charge);  // 注册极性电荷法术脚本
    RegisterSpellScript(spell_thaddius_polarity_shift);   // 注册极性转换法术脚本
    RegisterSpellScript(spell_thaddius_magnetic_pull);    // 注册磁力牵引法术脚本

    new at_thaddius_entrance();                         // 注册入口区域触发脚本

    new achievement_thaddius_shocking();                // 注册"休克！"成就脚本
}
