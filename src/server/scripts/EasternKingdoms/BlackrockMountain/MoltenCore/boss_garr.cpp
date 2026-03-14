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
 * @file boss_garr.cpp
 * @brief 熔火之心副本 - 迦顿男爵 (Garr) BOSS脚本
 *
 * 本模块实现了熔火之心副本中迦顿男爵BOSS及其小怪(熔岩怒火者)的AI逻辑。
 * 迦顿男爵是一个大型熔岩元素BOSS，拥有以下主要技能：
 * - 反魔法脉冲(Antimagic Pulse): 驱散周围敌人的魔法效果
 * - 熔岩镣铐(Magma Shackles): 减速周围敌人
 *
 * BOSS的特殊机制：
 * - BOSS周围有多个熔岩怒火者(Firesworn)小怪
 * - 小怪在血量低于10%时会施放爆发(Eruption)技能
 * - 小怪如果距离BOSS超过20码会获得"分离焦虑"增益
 *
 * 开发进度: 50%
 * 存在问题: 小怪机制尚未完全实现
 */

/* ScriptData
SDName: Boss_Garr
SD%Complete: 50
SDComment: Adds NYI
SDCategory: Molten Core
EndScriptData */

#include "ScriptMgr.h"
#include "molten_core.h"
#include "ObjectMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    // 迦顿男爵技能
    SPELL_ANTIMAGIC_PULSE    = 19492,    ///< 反魔法脉冲 - 驱散魔法效果
    SPELL_MAGMA_SHACKLES     = 19496,    ///< 熔岩镣铐 - 减速敌人
    SPELL_ENRAGE             = 19516,    ///< 狂暴 - 增加攻击力（未使用）
    SPELL_SEPARATION_ANXIETY = 23492,    ///< 分离焦虑 - 小怪远离BOSS时获得

    // 小怪技能
    SPELL_ERUPTION          = 19497,     ///< 爆发 - 小怪死亡时施放
    SPELL_IMMOLATE          = 15732,     ///< 献祭 - 周期性火焰伤害
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_ANTIMAGIC_PULSE    = 1,    ///< 反魔法脉冲事件
    EVENT_MAGMA_SHACKLES     = 2,    ///< 熔岩镣铐事件
};

/**
 * @brief 迦顿男爵BOSS AI结构体
 *
 * 继承自BossAI，实现了迦顿男爵的战斗逻辑。
 * 该BOSS主要负责使用群体驱散和减速技能。
 */
struct boss_garr : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_garr(Creature* creature) : BossAI(creature, BOSS_GARR) { }

    /**
     * @brief 进入战斗时调用
     * @param victim 战斗目标
     *
     * 初始化战斗事件调度器，安排各个技能的施放时间。
     * 调用时机: 当BOSS进入战斗状态时触发
     */
    void JustEngagedWith(Unit* victim) override
    {
        BossAI::JustEngagedWith(victim);
        // 初始化技能冷却时间
        events.ScheduleEvent(EVENT_ANTIMAGIC_PULSE, 25s);   // 反魔法脉冲，25秒后首次施放
        events.ScheduleEvent(EVENT_MAGMA_SHACKLES, 15s);    // 熔岩镣铐，15秒后首次施放
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要职责:
     * 1. 检查BOSS是否有有效目标
     * 2. 处理技能事件队列
     * 3. 执行近战攻击
     *
     * 性能注意事项:
     * - 每帧都会调用此函数，应避免复杂计算
     * - 使用事件系统管理技能冷却，避免频繁创建计时器
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件系统
        events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
            case EVENT_ANTIMAGIC_PULSE:
                // 反魔法脉冲：对自身施放，驱散周围敌人的魔法效果
                DoCast(me, SPELL_ANTIMAGIC_PULSE);
                events.ScheduleEvent(EVENT_ANTIMAGIC_PULSE, 10s, 15s);
                break;
            case EVENT_MAGMA_SHACKLES:
                // 熔岩镣铐：对自身施放，减速周围敌人
                DoCast(me, SPELL_MAGMA_SHACKLES);
                events.ScheduleEvent(EVENT_MAGMA_SHACKLES, 8s, 12s);
                break;
            default:
                break;
            }

            // 如果施法后进入施法状态，退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果没有施法且可以近战，执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 熔岩怒火者小怪AI结构体
 *
 * 继承自ScriptedAI，实现了迦顿男爵周围小怪的战斗逻辑。
 * 小怪的特殊机制：
 * - 在血量低于10%时会施放爆发技能并消失
 * - 如果距离BOSS超过20码会获得分离焦虑增益
 * - 定期对随机目标施放献祭
 */
struct npc_firesworn : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_firesworn(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 调度任务
     *
     * 使用任务调度器安排小怪的技能施放。
     * 调用时机: 进入战斗时调用
     *
     * 性能注意事项:
     * - 使用TaskScheduler管理计时器，性能优于手动计时
     */
    void ScheduleTasks()
    {
        // 献祭技能定时器
        // 注意: 此计时器可能不正确，需要进一步验证
        _scheduler.Schedule(4s, [this](TaskContext context)
        {
            // 随机选择一个目标施放献祭
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_IMMOLATE);

            // 5-10秒后重复
            context.Repeat(5s, 10s);
        });

        // 分离焦虑检测定时器
        // 定期检查小怪是否距离迦顿男爵太远
        _scheduler.Schedule(3s, [this](TaskContext context)
        {
            // 检查20码内是否有迦顿男爵
            if (!me->FindNearestCreature(NPC_GARR, 20.0f))
            {
                // 如果没有，获得分离焦虑增益
                DoCastSelf(SPELL_SEPARATION_ANXIETY);
            }
            else if (me->HasAura(SPELL_SEPARATION_ANXIETY))
            {
                // 如果有且已有分离焦虑，移除它
                me->RemoveAurasDueToSpell(SPELL_SEPARATION_ANXIETY);
            }

            // 立即重复检测
            context.Repeat();
        });
    }

    /**
     * @brief 重置AI状态
     *
     * 当小怪脱离战斗或重置时调用。
     * 清除所有已调度的任务。
     * 调用时机: 小怪重置时
     */
    void Reset() override
    {
        _scheduler.CancelAll();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 战斗目标
     *
     * 开始调度任务。
     * 调用时机: 当小怪进入战斗状态时触发
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        ScheduleTasks();
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 当小怪受到伤害时检查血量，如果血量将降至10%以下，
     * 则施放爆发技能并消失。
     *
     * 调用时机: 每次小怪受到伤害时
     * 性能注意事项: 每次受伤都会调用，计算量应该很小
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 计算10%血量阈值
        uint32 const health10pct = me->CountPctFromMaxHealth(10);
        uint32 health = me->GetHealth();

        // 如果这次伤害会导致血量降至10%以下
        if (int32(health) - int32(damage) < int32(health10pct))
        {
            // 取消这次伤害
            damage = 0;
            // 对当前目标施放爆发技能
            DoCastVictim(SPELL_ERUPTION);
            // 小怪消失
            me->DespawnOrUnsummon();
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 更新任务调度器并执行近战攻击。
     *
     * 性能注意事项:
     * - 使用lambda绑定近战攻击回调，简化代码
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效战斗目标
        if (!UpdateVictim())
            return;

        // 更新调度器，如果空闲则执行近战攻击
        _scheduler.Update(diff,
            std::bind(&ScriptedAI::DoMeleeAttackIfReady, this));
    }

private:
    TaskScheduler _scheduler;   ///< 任务调度器，用于管理技能冷却
};

/**
 * @brief 注册迦顿男爵BOSS和小怪脚本
 *
 * 此函数用于将BOSS和小怪的AI注册到脚本系统中。
 * 在服务器启动时由脚本加载器调用。
 */
void AddSC_boss_garr()
{
    // 注册迦顿男爵的AI
    RegisterMoltenCoreCreatureAI(boss_garr);
    // 注册熔岩怒火者小怪的AI
    RegisterMoltenCoreCreatureAI(npc_firesworn);
}
