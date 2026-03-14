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
 * @file boss_baron_geddon.cpp
 * @brief 熔火之心副本 - 沙尔登男爵 (Baron Geddon) BOSS脚本
 *
 * 本模块实现了熔火之心副本中沙尔登男爵BOSS的AI逻辑。
 * 沙尔登男爵是一个火焰元素BOSS，拥有以下主要技能：
 * - 地狱火(Inferno): 对周围造成递增的周期性伤害
 * - 燃烧法力(Ignite Mana): 燃烧目标的法力值
 * - 活体炸弹(Living Bomb): 使目标在一段时间后爆炸，对周围造成伤害
 * - 末日降临(Armageddon): 低血量时的自爆技能
 *
 * 开发进度: 100%
 * 存在问题: 无
 */

/* ScriptData
SDName: Boss_Baron_Geddon
SD%Complete: 100
SDComment:
SDCategory: Molten Core
EndScriptData */

#include "ScriptMgr.h"
#include "molten_core.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "ObjectMgr.h"

/**
 * @brief 表情文本枚举
 */
enum Emotes
{
    EMOTE_SERVICE       = 0   ///< 服役表情，用于末日降临时
};

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_INFERNO       = 19695,    ///< 地狱火 - 周期性伤害光环
    SPELL_INFERNO_DMG   = 19698,    ///< 地狱火伤害 - 实际的伤害法术
    SPELL_IGNITE_MANA   = 19659,    ///< 燃烧法力 - 燃烧目标法力值
    SPELL_LIVING_BOMB   = 20475,    ///< 活体炸弹 - 使目标变成炸弹
    SPELL_ARMAGEDDON    = 20478,    ///< 末日降临 - 自爆技能
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_INFERNO       = 1,    ///< 地狱火事件
    EVENT_IGNITE_MANA   = 2,    ///< 燃烧法力事件
    EVENT_LIVING_BOMB   = 3,    ///< 活体炸弹事件
};

/**
 * @brief 沙尔登男爵BOSS AI结构体
 *
 * 继承自BossAI，实现了沙尔登男爵的战斗逻辑。
 * 该BOSS在血量低于2%时会施放末日降临自爆。
 */
struct boss_baron_geddon : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_baron_geddon(Creature* creature) : BossAI(creature, BOSS_BARON_GEDDON)
    {
    }

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
        events.ScheduleEvent(EVENT_INFERNO, 45s);       // 地狱火，45秒后首次施放
        events.ScheduleEvent(EVENT_IGNITE_MANA, 30s);   // 燃烧法力，30秒后首次施放
        events.ScheduleEvent(EVENT_LIVING_BOMB, 35s);   // 活体炸弹，35秒后首次施放
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要职责:
     * 1. 检查BOSS是否有有效目标
     * 2. 检查血量是否低于2%，触发末日降临
     * 3. 处理技能事件队列
     * 4. 执行近战攻击
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

        // 检查血量是否低于2%，触发末日降临自爆
        // 这是BOSS的特殊机制，低血量时会自爆
        if (!HealthAbovePct(2))
        {
            // 打断所有非近战法术
            me->InterruptNonMeleeSpells(true);
            // 施放末日降临
            DoCast(me, SPELL_ARMAGEDDON);
            // 发表表情文本
            Talk(EMOTE_SERVICE);
            return;
        }

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INFERNO:
                    // 地狱火：对自身施放，造成周围AOE伤害
                    DoCast(me, SPELL_INFERNO);
                    events.ScheduleEvent(EVENT_INFERNO, 45s);
                    break;
                case EVENT_IGNITE_MANA:
                    // 燃烧法力：选择一个有法力值且未被燃烧的目标
                    // SelectTarget参数说明:
                    // - SelectTargetMethod::Random: 随机选择
                    // - 0: 距离限制（0表示不限制）
                    // - 0.0f: 距离值
                    // - true: 是否包括死亡单位（否）
                    // - true: 是否要求有法力值（是）
                    // - -SPELL_IGNITE_MANA: 排除已有该法术的目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true, true, -SPELL_IGNITE_MANA))
                        DoCast(target, SPELL_IGNITE_MANA);
                    events.ScheduleEvent(EVENT_IGNITE_MANA, 30s);
                    break;
                case EVENT_LIVING_BOMB:
                    // 活体炸弹：随机选择一个目标变成炸弹
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        DoCast(target, SPELL_LIVING_BOMB);
                    events.ScheduleEvent(EVENT_LIVING_BOMB, 35s);
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
 * @brief 地狱火法术脚本 (Spell ID: 19695)
 *
 * 地狱火是一个周期性触发光环，每个周期造成递增的伤害。
 * 伤害按以下规律递增：
 * - 第1-2跳: 500伤害
 * - 第3-4跳: 1000伤害
 * - 第5-6跳: 2000伤害
 * - 第7跳: 3000伤害
 * - 第8跳: 5000伤害
 *
 * 这个递增伤害机制使玩家需要在后期更快地跑开。
 */
class spell_baron_geddon_inferno : public AuraScript
{
    PrepareAuraScript(spell_baron_geddon_inferno);

    /**
     * @brief 周期性触发回调
     * @param aurEff 光环效果指针
     *
     * 当地狱火光环的周期性效果触发时调用。
     * 根据当前跳数从伤害表中获取对应伤害值并施放伤害法术。
     *
     * 调用时机: 每次周期性效果触发时
     * 性能注意事项: 静态数组的访问是O(1)复杂度
     */
    void OnPeriodic(AuraEffect const* aurEff)
    {
        // 阻止默认的触发行为
        PreventDefaultAction();

        // 地狱火伤害递增表，共8跳
        static const int32 damageForTick[8] = { 500, 500, 1000, 1000, 2000, 2000, 3000, 5000 };

        // 构建法术参数
        CastSpellExtraArgs args;
        args.TriggerFlags = TRIGGERED_FULL_MASK;    // 使用完全触发模式
        args.TriggeringAura = aurEff;               // 设置触发光环
        // 根据当前跳数（从1开始）设置伤害值
        args.AddSpellMod(SPELLVALUE_BASE_POINT0, damageForTick[aurEff->GetTickNumber() - 1]);

        // 施放实际伤害法术
        GetTarget()->CastSpell(nullptr, SPELL_INFERNO_DMG, args);
    }

    /**
     * @brief 注册脚本回调
     *
     * 将OnPeriodic函数绑定到周期性效果触发事件。
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_baron_geddon_inferno::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 注册沙尔登男爵BOSS脚本
 *
 * 此函数用于将BOSS AI和法术脚本注册到脚本系统中。
 * 在服务器启动时由脚本加载器调用。
 */
void AddSC_boss_baron_geddon()
{
    // 注册沙尔登男爵的AI
    RegisterMoltenCoreCreatureAI(boss_baron_geddon);
    // 注册地狱火法术脚本
    RegisterSpellScript(spell_baron_geddon_inferno);
}
