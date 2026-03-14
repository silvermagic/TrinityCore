/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it;/or modify it
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
 * @file boss_nexusprince_shaffar.cpp
 * @brief 法力陵墓副本BOSS - 节点王子沙法尔的AI脚本实现
 *
 * 本模块实现了节点王子沙法尔BOSS及其召唤生物的战斗逻辑，包括：
 * - 沙法尔BOSS技能施放逻辑（火球术、寒冰箭、冰霜新星、闪烁等）
 * - 信号塔召唤机制（定期召唤信号塔协助战斗）
 * - 信号塔AI（召唤学徒并施放奥术箭）
 * - 以太学徒AI（施放火焰箭和寒冰箭）
 * - 尤尔AI（英雄模式额外BOSS，双头犬）
 *
 * 节点王子沙法尔是法力陵墓的最终BOSS，是一名强大的以太族法师。
 */

/* ScriptData
SDName: Boss_NexusPrince_Shaffar
SD%Complete: 80
SDComment: Need more tuning of spell timers, it should not be as linear fight as current. Also should possibly find a better way to deal with his three initial beacons to make sure all aggro.
SDCategory: Auchindoun, Mana Tombs
EndScriptData */

#include "ScriptMgr.h"
#include "mana_tombs.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"

/**
 * @brief 对话文本枚举
 */
enum Yells
{
    SAY_INTRO   = 0,  ///< 介绍文本 - 当玩家接近时触发
    SAY_AGGRO   = 1,  ///< 开怪文本 - 进入战斗时触发
    SAY_SLAY    = 2,  ///< 击杀文本 - 击杀玩家时触发
    SAY_SUMMON  = 3,  ///< 召唤文本 - 召唤信号塔时触发
    SAY_DEAD    = 4   ///< 死亡文本 - BOSS死亡时触发
};

/**
 * @brief 法术枚举 - 节点王子沙法尔及相关生物使用的法术ID
 */
enum Spells
{
    // 沙法尔的法术
    SPELL_BLINK                     = 34605,  ///< 闪烁 - 冰霜新星后传送
    SPELL_FROSTBOLT                 = 32364,  ///< 寒冰箭 - 主要伤害技能
    SPELL_FIREBALL                  = 32363,  ///< 火球术 - 主要伤害技能
    SPELL_FROSTNOVA                 = 32365,  ///< 冰霜新星 - AOE冰冻技能

    // 信号塔相关法术
    SPELL_ETHEREAL_BEACON           = 32371,  ///< 召唤信号塔法术
    SPELL_ETHEREAL_BEACON_VISUAL    = 32368,  ///< 信号塔视觉效果

    // 信号塔的法术
    SPELL_ARCANE_BOLT               = 15254,  ///< 奥术箭 - 信号塔的主要攻击技能
    SPELL_ETHEREAL_APPRENTICE       = 32372   ///< 召唤以太学徒法术
};

/**
 * @brief 生物枚举 - 相关生物ID
 */
enum Creatures
{
    NPC_BEACON  = 18431,  ///< 信号塔生物ID
    NPC_SHAFFAR = 18344   ///< 节点王子沙法尔生物ID
};

/**
 * @brief 杂项枚举
 */
enum Misc
{
    NR_INITIAL_BEACONS = 3  ///< 初始召唤的信号塔数量
};

/**
 * @brief 事件枚举 - 战斗事件类型
 */
enum Events
{
    EVENT_BLINK        = 1,  ///< 闪烁事件
    EVENT_BEACON,            ///< 召唤信号塔事件
    EVENT_FIREBALL,          ///< 火球术事件
    EVENT_FROSTBOLT,         ///< 寒冰箭事件
    EVENT_FROST_NOVA         ///< 冰霜新星事件
};

/**
 * @brief 节点王子沙法尔BOSS AI结构体
 *
 * 实现节点王子沙法尔的战斗AI，继承自BossAI基类。
 * 沙法尔是一名法师型BOSS，会使用各种法术，
 * 并定期召唤信号塔协助战斗。
 */
struct boss_nexusprince_shaffar : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置DATA_NEXUSPRINCE_SHAFFAR作为BOSS标识，
     * 并初始化是否已嘲讽标志为false。
     */
    boss_nexusprince_shaffar(Creature* creature) : BossAI(creature, DATA_NEXUSPRINCE_SHAFFAR)
    {
        _hasTaunted = false;  // 介绍文本是否已触发
    }

    /**
     * @brief 重置BOSS状态
     *
     * 在战斗重置时调用。负责：
     * - 重置事件调度器
     * - 在初始位置召唤3个信号塔
     *
     * @调用时机 当BOSS脱离战斗或重置时调用
     */
    void Reset() override
    {
        _Reset();  // 调用基类重置方法

        // 在BOSS初始位置周围召唤3个信号塔
        float dist = 8.0f;  // 距离BOSS的距离
        float posX, posY, posZ, angle;
        me->GetHomePosition(posX, posY, posZ, angle);  // 获取BOSS的初始位置

        // 召唤3个信号塔，分布在不同方向
        me->SummonCreature(NPC_BEACON, posX - dist, posY - dist, posZ, angle, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2h);
        me->SummonCreature(NPC_BEACON, posX - dist, posY + dist, posZ, angle, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2h);
        me->SummonCreature(NPC_BEACON, posX + dist, posY, posZ, angle, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2h);
    }

    /**
     * @brief 视线检测回调
     * @param who 进入视野的单位
     *
     * 当有单位进入BOSS视野时调用。
     * 如果是玩家且距离在100码内，触发介绍文本。
     *
     * @调用时机 当单位进入BOSS视野时
     */
    void MoveInLineOfSight(Unit* who) override
    {
        // 如果介绍文本未触发，且是玩家，且距离在100码内
        if (!_hasTaunted && who->GetTypeId() == TYPEID_PLAYER && me->IsWithinDistInMap(who, 100.0f))
        {
            Talk(SAY_INTRO);    // 发送介绍文本
            _hasTaunted = true; // 标记介绍文本已触发
        }
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当BOSS进入战斗时调用。负责：
     * - 发送开怪文本
     * - 调度初始技能事件
     *
     * @调用时机 当BOSS进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);  // 发送开怪文本
        BossAI::JustEngagedWith(who);

        // 调度技能事件
        events.ScheduleEvent(EVENT_BEACON, 10s);       // 召唤信号塔，10秒后开始
        events.ScheduleEvent(EVENT_FIREBALL, 8s);      // 火球术，8秒后开始
        events.ScheduleEvent(EVENT_FROSTBOLT, 4s);     // 寒冰箭，4秒后开始
        events.ScheduleEvent(EVENT_FROST_NOVA, 15s);   // 冰霜新星，15秒后开始
    }

    /**
     * @brief 召唤生物回调
     * @param summoned 被召唤的生物
     *
     * 当有生物被召唤时调用。负责：
     * - 为信号塔添加视觉效果
     * - 让信号塔开始攻击随机目标
     * - 将召唤生物添加到召唤列表
     *
     * @调用时机 当BOSS召唤生物时
     */
    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_BEACON)
        {
            // 为信号塔添加视觉效果
            summoned->CastSpell(summoned, SPELL_ETHEREAL_BEACON_VISUAL, false);

            // 让信号塔攻击随机目标
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                summoned->AI()->AttackStart(target);
        }

        summons.Summon(summoned);  // 添加到召唤列表
    }

    /**
     * @brief 击杀单位回调
     * @param victim 被击杀的单位
     *
     * 当BOSS击杀单位时调用。
     * 如果击杀的是玩家，发送击杀文本。
     *
     * @调用时机 当BOSS击杀单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者（未使用）
     *
     * 当BOSS死亡时调用。发送死亡文本并通知实例。
     *
     * @调用时机 当BOSS死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEAD);  // 发送死亡文本
        _JustDied();     // 调用基类处理（通知实例、掉落等）
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 处理各种战斗事件的具体逻辑。
     *
     * @调用时机 当事件调度器触发事件时
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_BLINK:
                // 闪烁：打断当前施法，清除移动，传送到随机位置
                if (me->IsNonMeleeSpellCast(false))
                    me->InterruptNonMeleeSpells(true);  // 打断当前施法

                // 清除移动，防止施法后立即回到原位置
                // （但应该在某个时间重新使用MoveChase吗？还是他不移动？）
                me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);

                DoCast(me, SPELL_BLINK);  // 施放闪烁
                break;

            case EVENT_BEACON:
                // 召唤信号塔：25%几率触发对话
                if (!urand(0, 3))
                    Talk(SAY_SUMMON);

                DoCast(me, SPELL_ETHEREAL_BEACON, true);  // 召唤信号塔（瞬发）
                events.ScheduleEvent(EVENT_BEACON, 10s);
                break;

            case EVENT_FIREBALL:
                // 火球术：对当前目标施放
                DoCastVictim(SPELL_FROSTBOLT);  // 注意：代码中写的是FROSTBOLT，可能是bug
                events.ScheduleEvent(EVENT_FIREBALL, 4500ms, 6s);
                break;

            case EVENT_FROSTBOLT:
                // 寒冰箭：对当前目标施放
                DoCastVictim(SPELL_FROSTBOLT);
                events.ScheduleEvent(EVENT_FROSTBOLT, 4500ms, 6s);
                break;

            case EVENT_FROST_NOVA:
                // 冰霜新星：对周围敌人施放冰冻，然后闪烁
                DoCast(me, SPELL_FROSTNOVA);
                events.ScheduleEvent(EVENT_FROST_NOVA, 17500ms, 25s);
                events.ScheduleEvent(EVENT_BLINK, 1500ms);  // 1.5秒后闪烁
                break;

            default:
                break;
        }
    }

private:
    bool _hasTaunted;  ///< 是否已触发介绍文本标志
};

/**
 * @brief 以太信号塔事件枚举
 */
enum EtherealBeacon
{
    EVENT_APPRENTICE = 1,  ///< 召唤学徒事件
    EVENT_ARCANE_BOLT      ///< 奥术箭事件
};

/**
 * @brief 以太信号塔AI结构体
 *
 * 实现以太信号塔的AI，继承自ScriptedAI基类。
 * 信号塔会在战斗一段时间后召唤以太学徒，并施放奥术箭攻击。
 */
struct npc_ethereal_beacon : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_ethereal_beacon(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 重置事件调度器。
     *
     * @调用时机 当生物脱离战斗或重置时调用
     */
    void Reset() override
    {
        _events.Reset();  // 重置事件调度器
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 当信号塔进入战斗时调用。负责：
     * - 如果沙法尔未在战斗，让沙法尔加入战斗
     * - 调度召唤学徒和奥术箭事件
     *
     * @调用时机 当信号塔进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        // 如果沙法尔在100码内且未在战斗，让沙法尔加入战斗
        if (Creature* shaffar = me->FindNearestCreature(NPC_SHAFFAR, 100.0f))
            if (!shaffar->IsInCombat())
                shaffar->AI()->AttackStart(who);

        // 调度事件：英雄模式10秒召唤学徒，普通模式20秒
        _events.ScheduleEvent(EVENT_APPRENTICE, DUNGEON_MODE(20s, 10s));
        _events.ScheduleEvent(EVENT_ARCANE_BOLT, 1s);  // 1秒后开始施放奥术箭
    }

    /**
     * @brief 召唤生物回调
     * @param summoned 被召唤的生物
     *
     * 当召唤以太学徒时，让学徒攻击当前目标。
     *
     * @调用时机 当信号塔召唤生物时
     */
    void JustSummoned(Creature* summoned) override
    {
        summoned->AI()->AttackStart(me->GetVictim());
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主战斗循环，负责：
     * - 检查是否有战斗目标
     * - 更新事件调度器
     * - 处理施法状态
     * - 执行已触发的事件
     *
     * @调用时机 每个游戏tick调用（约每秒多次）
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        _events.Update(diff);  // 更新事件调度器

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有已触发的事件
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_APPRENTICE:
                    // 召唤以太学徒，然后信号塔消失
                    DoCast(me, SPELL_ETHEREAL_APPRENTICE, true);
                    me->DespawnOrUnsummon();  // 信号塔消失
                    break;

                case EVENT_ARCANE_BOLT:
                    // 施放奥术箭攻击当前目标
                    DoCastVictim(SPELL_ARCANE_BOLT);
                    _events.ScheduleEvent(EVENT_ARCANE_BOLT, 2s, 4500ms);
                    break;

                default:
                    break;
            }
        }
    }

private:
    EventMap _events;  ///< 事件调度器
};

/**
 * @brief 以太学徒枚举
 */
enum EtherealApprentice
{
    SPELL_ETHEREAL_APPRENTICE_FIREBOLT          = 32369,  ///< 火焰箭法术ID
    SPELL_ETHEREAL_APPRENTICE_FROSTBOLT         = 32370,  ///< 寒冰箭法术ID
    EVENT_ETHEREAL_APPRENTICE_FIREBOLT          = 1,      ///< 火焰箭事件
    EVENT_ETHEREAL_APPRENTICE_FROSTBOLT                  ///< 寒冰箭事件
};

/**
 * @brief 以太学徒AI结构体
 *
 * 实现以太学徒的AI，继承自ScriptedAI基类。
 * 学徒会交替施放火焰箭和寒冰箭攻击目标。
 */
struct npc_ethereal_apprentice : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_ethereal_apprentice(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 重置事件调度器。
     *
     * @调用时机 当生物脱离战斗或重置时调用
     */
    void Reset() override
    {
        _events.Reset();  // 重置事件调度器
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标（未使用）
     *
     * 调度火焰箭事件。
     *
     * @调用时机 当学徒进入战斗时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_ETHEREAL_APPRENTICE_FIREBOLT, 3s);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主战斗循环，负责：
     * - 检查是否有战斗目标
     * - 更新事件调度器
     * - 处理施法状态
     * - 执行已触发的事件（交替施放火焰箭和寒冰箭）
     *
     * @调用时机 每个游戏tick调用（约每秒多次）
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        _events.Update(diff);  // 更新事件调度器

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有已触发的事件
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ETHEREAL_APPRENTICE_FIREBOLT:
                    // 施放火焰箭，然后调度寒冰箭
                    DoCastVictim(SPELL_ETHEREAL_APPRENTICE_FIREBOLT, true);
                    _events.ScheduleEvent(EVENT_ETHEREAL_APPRENTICE_FROSTBOLT, 3s);
                    break;

                case EVENT_ETHEREAL_APPRENTICE_FROSTBOLT:
                    // 施放寒冰箭，然后调度火焰箭
                    DoCastVictim(SPELL_ETHEREAL_APPRENTICE_FROSTBOLT, true);
                    _events.ScheduleEvent(EVENT_ETHEREAL_APPRENTICE_FIREBOLT, 3s);
                    break;

                default:
                    break;
            }
        }
    }

private:
    EventMap _events;  ///< 事件调度器
};

/**
 * @brief 尤尔枚举
 */
enum Yor
{
    SPELL_DOUBLE_BREATH = 38361,  ///< 双重吐息法术ID
    EVENT_DOUBLE_BREATH = 1       ///< 双重吐息事件
};

/**
 * @brief 尤尔AI结构体
 *
 * 实现尤尔的AI，继承自ScriptedAI基类。
 * 尤尔是英雄模式的额外BOSS（双头犬），
 * 会施放双重吐息攻击近战范围内的目标。
 */
struct npc_yor : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_yor(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 尤尔重置时不做任何操作。
     *
     * @调用时机 当生物脱离战斗或重置时调用
     */
    void Reset() override { }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标（未使用）
     *
     * 调度双重吐息事件。
     *
     * @调用时机 当尤尔进入战斗时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_DOUBLE_BREATH, 6s, 9s);
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主战斗循环，负责：
     * - 检查是否有战斗目标
     * - 更新事件调度器
     * - 执行已触发的事件
     * - 执行近战攻击
     *
     * @调用时机 每个游戏tick调用（约每秒多次）
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        _events.Update(diff);  // 更新事件调度器

        // 处理所有已触发的事件
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_DOUBLE_BREATH:
                    // 双重吐息：只在目标在近战范围内时施放
                    if (me->IsWithinDist(me->GetVictim(), ATTACK_DISTANCE))
                        DoCastVictim(SPELL_DOUBLE_BREATH);
                    _events.ScheduleEvent(EVENT_DOUBLE_BREATH, 6s, 9s);
                    break;

                default:
                    break;
            }
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

    private:
        EventMap _events;  ///< 事件调度器
};

/**
 * @brief 注册节点王子沙法尔BOSS脚本
 *
 * 此函数在服务器启动时被调用，注册以下内容：
 * - 节点王子沙法尔BOSS AI
 * - 以太信号塔AI
 * - 以太学徒AI
 * - 尤尔AI
 */
void AddSC_boss_nexusprince_shaffar()
{
    RegisterManaTombsCreatureAI(boss_nexusprince_shaffar);
    RegisterManaTombsCreatureAI(npc_ethereal_beacon);
    RegisterManaTombsCreatureAI(npc_ethereal_apprentice);
    RegisterManaTombsCreatureAI(npc_yor);
}
