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
 * @file boss_balinda.cpp
 * @brief 奥特兰克山谷 - 巴琳达·石炉首领AI脚本
 *
 * 本模块实现了奥特兰克山谷战场中联盟方首领巴琳达·石炉的AI行为逻辑。
 * 巴琳达是一名法师型首领,拥有多种法术技能和召唤水元素的能力。
 * 当生命值低于40%时会使用寒冰屏障进行自我保护。
 *
 * 主要功能：
 * - 法术攻击循环（奥爆、冰锥、火球、冰箭）
 * - 召唤水元素协助战斗
 * - 低血量时施放寒冰屏障
 * - 脱离战斗检测和重置机制
 */

#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

/**
 * @brief 巴琳达使用的法术ID枚举
 */
enum Spells
{
    SPELL_ARCANE_EXPLOSION                  = 46608,  ///< 奥术爆炸 - AOE伤害法术
    SPELL_CONE_OF_COLD                      = 38384,  ///< 冰锥术 - 锥形范围冰霜伤害
    SPELL_FIREBALL                          = 46988,  ///< 火球术 - 主要远程攻击法术
    SPELL_FROSTBOLT                         = 46987,  ///< 寒冰箭 - 远程冰霜伤害法术
    SPELL_SUMMON_WATER_ELEMENTAL            = 45067,  ///< 召唤水元素 - 召唤宠物协助战斗
    SPELL_ICEBLOCK                          = 46604   ///< 寒冰屏障 - 低血量保命技能
};

/**
 * @brief 巴琳达的文本ID枚举
 */
enum Texts
{
    SAY_AGGRO                              = 0,  ///< 进入战斗时的喊话
    SAY_EVADE                              = 1,  ///< 脱离战斗时的喊话
    SAY_SALVATION                          = 2,  ///< 救援喊话（未使用）
};

/**
 * @brief 动作ID枚举
 */
enum Action
{
    ACTION_BUFF_YELL                        = -30001  ///< 增益喊话动作ID（与战场共享）
};

/**
 * @brief 事件ID枚举，用于调度技能施放
 */
enum Events
{
    // Balinda
    EVENT_ARCANE_EXPLOSION = 1,          ///< 奥术爆炸事件
    EVENT_CONE_OF_COLD,                  ///< 冰锥术事件
    EVENT_FIREBOLT,                      ///< 火球术事件
    EVENT_FROSTBOLT,                     ///< 寒冰箭事件
    EVENT_SUMMON_WATER_ELEMENTAL,        ///< 召唤水元素事件
    EVENT_CHECK_RESET,                   ///< 检查重置事件 - 检查巴琳达或水元素是否离开建筑物范围
};

/**
 * @struct boss_balinda
 * @brief 巴琳达·石炉AI实现
 *
 * 该AI类实现了奥特兰克山谷联盟首领巴琳达·石炉的战斗逻辑。
 * 巴琳达是一名法师型首领，使用多种法术攻击并召唤水元素协助战斗。
 * 当生命值降低到40%以下时，会施放寒冰屏障进行自保。
 */
struct boss_balinda : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_balinda(Creature* creature) : ScriptedAI(creature), summons(me)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 重置水元素GUID和寒冰屏障标志位。
     * 调用时机：在构造函数和Reset()中被调用。
     */
    void Initialize()
    {
        WaterElementalGUID.Clear();
        HasCastIceblock = false;
    }

    /**
     * @brief 重置AI状态
     *
     * 重置所有成员变量、事件调度器和召唤列表。
     * 当生物脱离战斗或重生时调用。
     */
    void Reset() override
    {
        Initialize();
        events.Reset();
        summons.DespawnAll();
    }

    /**
     * @brief 进入战斗时的回调
     * @param who 进入战斗的目标（未使用）
     *
     * 当巴琳达进入战斗时：
     * 1. 发送进入战斗喊话
     * 2. 调度所有技能事件的初始施放时间
     *
     * 技能调度：
     * - 奥术爆炸：5-15秒
     * - 冰锥术：8秒
     * - 火球术：1秒
     * - 寒冰箭：4秒
     * - 召唤水元素：3秒
     * - 位置检查：5秒
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_AGGRO);
        events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 5s, 15s);
        events.ScheduleEvent(EVENT_CONE_OF_COLD, 8s);
        events.ScheduleEvent(EVENT_FIREBOLT, 1s);
        events.ScheduleEvent(EVENT_FROSTBOLT, 4s);
        events.ScheduleEvent(EVENT_SUMMON_WATER_ELEMENTAL, 3s);
        events.ScheduleEvent(EVENT_CHECK_RESET, 5s);
    }

    /**
     * @brief 召唤生物时的回调
     * @param summoned 被召唤的生物指针
     *
     * 当水元素被召唤时：
     * 1. 让水元素攻击随机目标
     * 2. 设置水元素的阵营与巴琳达相同
     * 3. 记录水元素的GUID
     * 4. 将水元素加入召唤列表
     */
    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(SelectTarget(SelectTargetMethod::Random, 0, 50, true));
        summoned->SetFaction(me->GetFaction());
        WaterElementalGUID = summoned->GetGUID();
        summons.Summon(summoned);
    }

    /**
     * @brief 召唤生物消失时的回调
     * @param summoned 消失的生物指针
     *
     * 从召唤列表中移除消失的生物。
     */
    void SummonedCreatureDespawn(Creature* summoned) override
    {
        summons.Despawn(summoned);
    }

    /**
     * @brief 死亡时的回调
     * @param killer 击杀者（未使用）
     *
     * 巴琳达死亡时，消除所有召唤的水元素。
     */
    void JustDied(Unit* /*killer*/) override
    {
        summons.DespawnAll();
    }

    /**
     * @brief 执行动作的回调
     * @param actionId 动作ID
     *
     * 当收到动作指令时执行相应操作。
     * 目前仅处理战场传来的增益喊话动作。
     */
    void DoAction(int32 actionId) override
    {
        if (actionId == ACTION_BUFF_YELL)
            Talk(SAY_AGGRO);
    }

    /**
     * @brief 受到伤害时的回调
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（输入输出参数）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当生命值降至40%以下且尚未施放过寒冰屏障时，
     * 施放寒冰屏障技能进行自保。
     *
     * 注意：该技能每场战斗只会施放一次。
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->HealthBelowPctDamaged(40, damage) && !HasCastIceblock)
        {
            DoCast(SPELL_ICEBLOCK);
            HasCastIceblock = true;
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要AI更新循环，每帧调用一次：
     * 1. 检查是否有有效目标
     * 2. 更新事件调度器
     * 3. 如果正在施法则等待
     * 4. 处理事件队列中的所有待执行事件
     * 5. 根据事件类型施放相应技能
     * 6. 如果没有施法则进行近战攻击
     *
     * 性能注意：每帧都会调用此函数，应避免耗时操作。
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ARCANE_EXPLOSION:
                    // 对当前目标施放奥术爆炸
                    DoCastVictim(SPELL_ARCANE_EXPLOSION);
                    events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 5s, 15s);
                    break;
                case EVENT_CONE_OF_COLD:
                    // 对当前目标施放冰锥术
                    DoCastVictim(SPELL_CONE_OF_COLD);
                    events.ScheduleEvent(EVENT_CONE_OF_COLD, 10s, 20s);
                    break;
                case EVENT_FIREBOLT:
                    // 对当前目标施放火球术
                    DoCastVictim(SPELL_FIREBALL);
                    events.ScheduleEvent(EVENT_FIREBOLT, 5s, 9s);
                    break;
                case EVENT_FROSTBOLT:
                    // 对当前目标施放寒冰箭
                    DoCastVictim(SPELL_FROSTBOLT);
                    events.ScheduleEvent(EVENT_FROSTBOLT, 4s, 12s);
                    break;
                case EVENT_SUMMON_WATER_ELEMENTAL:
                    // 仅在没有存活的水元素时才召唤新的
                    if (summons.empty())
                        DoCast(SPELL_SUMMON_WATER_ELEMENTAL);
                    events.ScheduleEvent(EVENT_SUMMON_WATER_ELEMENTAL, 50s);
                    break;
                case EVENT_CHECK_RESET:
                    // 检查巴琳达是否离开了出生点50码范围
                    if (me->GetDistance2d(me->GetHomePosition().GetPositionX(), me->GetHomePosition().GetPositionY()) > 50)
                    {
                        EnterEvadeMode();
                        Talk(SAY_EVADE);
                    }
                    // 检查水元素是否离开了出生点50码范围
                    if (Creature* elemental = ObjectAccessor::GetCreature(*me, WaterElementalGUID))
                        if (elemental->GetDistance2d(me->GetHomePosition().GetPositionX(), me->GetHomePosition().GetPositionY()) > 50)
                            elemental->AI()->EnterEvadeMode();
                    events.ScheduleEvent(EVENT_CHECK_RESET, 5s);
                    break;
                default:
                    break;
            }

            // 如果施法状态改变，立即返回避免重复施法
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap events;                ///< 事件调度器，管理技能施放计时
    SummonList summons;             ///< 召唤列表，管理召唤的水元素
    ObjectGuid WaterElementalGUID;  ///< 水元素的GUID，用于追踪和检查位置
    bool HasCastIceblock;           ///< 是否已经施放过寒冰屏障的标志
};

void AddSC_boss_balinda()
{
    RegisterCreatureAI(boss_balinda);
}
