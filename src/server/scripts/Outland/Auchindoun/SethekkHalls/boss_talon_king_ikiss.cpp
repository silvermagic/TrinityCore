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
 * @file boss_talon_king_ikiss.cpp
 * @brief 赛泰克大厅副本BOSS - 利爪之王艾吉斯的AI脚本实现
 *
 * 本模块实现了利爪之王艾吉斯BOSS的战斗逻辑，包括：
 * - 技能施放逻辑（奥术齐射、变形术、闪烁等）
 * - 生命值管理（20%生命值时施放法力护盾）
 * - 闪烁传送机制（随机传送到玩家位置并施放奥术爆炸）
 * - 英雄模式额外技能（减速术）
 *
 * 利爪之王艾吉斯是赛泰克大厅的最终BOSS，是一名强大的鸦人法师。
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "sethekk_halls.h"

/**
 * @brief 对话文本枚举
 */
enum Says
{
    SAY_INTRO               = 0,  ///< 介绍文本 - 当玩家接近时触发
    SAY_AGGRO               = 1,  ///< 开怪文本 - 进入战斗时触发
    SAY_SLAY                = 2,  ///< 击杀文本 - 击杀玩家时触发
    SAY_DEATH               = 3,  ///< 死亡文本 - BOSS死亡时触发
    EMOTE_ARCANE_EXPLOSION  = 4   ///< 奥术爆炸表情 - 闪烁前警告玩家
};

/**
 * @brief 法术枚举 - 利爪之王艾吉斯使用的所有法术ID
 */
enum Spells
{
    SPELL_BLINK                 = 38194,  ///< 闪烁选择目标法术（选择随机玩家作为传送目标）
    SPELL_BLINK_TELEPORT        = 38203,  ///< 闪烁传送法术（执行实际传送）
    SPELL_MANA_SHIELD           = 38151,  ///< 法力护盾 - 20%生命值时施放
    SPELL_ARCANE_BUBBLE         = 9438,   ///< 奥术气泡 - 闪烁后保护BOSS
    SPELL_SLOW                  = 35032,  ///< 减速术 - 英雄模式技能
    SPELL_POLYMORPH             = 38245,  ///< 变形术 - 将目标变成绵羊
    SPELL_ARCANE_VOLLEY         = 35059,  ///< 奥术齐射 - AOE伤害技能
    SPELL_ARCANE_EXPLOSION      = 38197,  ///< 奥术爆炸 - 闪烁后施放的高伤害技能
};

/**
 * @brief 事件枚举 - 战斗事件类型
 */
enum Events
{
    EVENT_POLYMORPH = 1,       ///< 变形术事件
    EVENT_BLINK,               ///< 闪烁事件
    EVENT_SLOW,                ///< 减速术事件（英雄模式）
    EVENT_ARCANE_VOLLEY,       ///< 奥术齐射事件
    EVENT_ARCANE_EXPLOSION     ///< 奥术爆炸事件（闪烁后触发）
};

/**
 * @brief 利爪之王艾吉斯BOSS AI结构体
 *
 * 实现利爪之王艾吉斯的战斗AI，继承自BossAI基类。
 * 艾吉斯是一名法师型BOSS，会使用各种奥术法术，
 * 并定期闪烁到随机玩家位置施放奥术爆炸。
 */
struct boss_talon_king_ikiss : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置DATA_TALON_KING_IKISS作为BOSS标识，
     * 并初始化介绍和法力护盾标志为false。
     */
    boss_talon_king_ikiss(Creature* creature) : BossAI(creature, DATA_TALON_KING_IKISS)
    {
        Intro = false;      // 介绍文本是否已触发
        ManaShield = false; // 法力护盾是否已施放
    }

    /**
     * @brief 重置BOSS状态
     *
     * 在战斗重置时调用，负责：
     * - 重置事件调度器
     * - 重置介绍和法力护盾标志
     *
     * @调用时机 当BOSS脱离战斗或重置时调用
     */
    void Reset() override
    {
        _Reset();  // 调用基类重置方法
        Intro = false;
        ManaShield = false;
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
        if (!Intro && who->GetTypeId() == TYPEID_PLAYER && me->IsWithinDistInMap(who, 100.0f))
        {
            Intro = true;       // 标记介绍文本已触发
            Talk(SAY_INTRO);    // 发送介绍文本
        }

        BossAI::MoveInLineOfSight(who);  // 调用基类处理
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
        BossAI::JustEngagedWith(who);  // 调用基类处理
        Talk(SAY_AGGRO);               // 发送开怪文本

        // 调度技能事件
        events.ScheduleEvent(EVENT_ARCANE_VOLLEY, 5s);         // 奥术齐射，5秒后开始
        events.ScheduleEvent(EVENT_POLYMORPH, 8s);             // 变形术，8秒后开始
        events.ScheduleEvent(EVENT_BLINK, 35s);                // 闪烁，35秒后开始

        // 英雄模式额外技能
        if (IsHeroic())
            events.ScheduleEvent(EVENT_SLOW, 15s, 30s);        // 减速术，15-30秒随机间隔
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
            case EVENT_POLYMORPH:
                // 变形术：普通模式选择仇恨第二高的目标，英雄模式选择随机目标
                // 这样可以避免将当前坦克变形
                if (IsHeroic())
                    DoCast(SelectTarget(SelectTargetMethod::Random, 0), SPELL_POLYMORPH);
                else
                    DoCast(SelectTarget(SelectTargetMethod::MaxThreat, 1), SPELL_POLYMORPH);
                events.ScheduleEvent(EVENT_POLYMORPH, 15s, 17500ms);
                break;

            case EVENT_ARCANE_VOLLEY:
                // 奥术齐射：对周围所有敌人造成奥术伤害
                DoCast(me, SPELL_ARCANE_VOLLEY);
                events.ScheduleEvent(EVENT_ARCANE_VOLLEY, 7s, 12s);
                break;

            case EVENT_SLOW:
                // 减速术（英雄模式）：降低周围敌人的移动和攻击速度
                DoCast(me, SPELL_SLOW);
                events.ScheduleEvent(EVENT_SLOW, 15s, 40s);
                break;

            case EVENT_BLINK:
                // 闪烁：打断当前施法，传送到随机玩家位置
                if (me->IsNonMeleeSpellCast(false))
                    me->InterruptNonMeleeSpells(false);  // 打断当前施法

                Talk(EMOTE_ARCANE_EXPLOSION);             // 发送表情警告
                DoCastAOE(SPELL_BLINK);                   // 施放闪烁选择目标法术
                events.ScheduleEvent(EVENT_BLINK, 35s, 40s);
                events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 1s);  // 1秒后施放奥术爆炸
                break;

            case EVENT_ARCANE_EXPLOSION:
                // 奥术爆炸：闪烁后施放的高伤害AOE技能
                // 同时施放奥术气泡保护自己
                DoCast(me, SPELL_ARCANE_EXPLOSION);
                DoCast(me, SPELL_ARCANE_BUBBLE, true);  // 施放奥术气泡（瞬发，不触发GCD）
                break;

            default:
                break;
        }
    }

    /**
     * @brief 受到伤害回调
     * @param who 造成伤害的单位（未使用）
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当BOSS受到伤害时调用。
     * 当生命值降至20%以下时，施放法力护盾（只触发一次）。
     *
     * @调用时机 当BOSS受到伤害时
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 如果法力护盾未施放，且伤害将使生命值降至20%以下
        if (!ManaShield && me->HealthBelowPctDamaged(20, damage))
        {
            DoCast(me, SPELL_MANA_SHIELD);  // 施放法力护盾
            ManaShield = true;               // 标记法力护盾已施放
        }
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
        _JustDied();   // 调用基类处理（通知实例、掉落等）
        Talk(SAY_DEATH);  // 发送死亡文本
    }

    /**
     * @brief 击杀单位回调
     * @param who 被击杀的单位
     *
     * 当BOSS击杀单位时调用。
     * 如果击杀的是玩家，发送击杀文本。
     *
     * @调用时机 当BOSS击杀单位时
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    private:
        bool ManaShield;  ///< 法力护盾是否已施放标志
        bool Intro;       ///< 介绍文本是否已触发标志
};

/**
 * @brief 闪烁法术脚本
 *
 * 处理闪烁法术（38194）的目标选择和传送逻辑。
 * 闪烁会将BOSS传送到随机玩家位置。
 */
// 38194 - Blink
class spell_talon_king_ikiss_blink : public SpellScript
{
    PrepareSpellScript(spell_talon_king_ikiss_blink);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 如果依赖的法术存在则返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BLINK_TELEPORT });
    }

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 从目标列表中随机选择一个目标，移除其他所有目标。
     * 这确保BOSS只传送到一个随机玩家位置。
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        if (targets.empty())
            return;

        // 随机选择一个目标
        WorldObject* target = Trinity::Containers::SelectRandomContainerElement(targets);
        targets.clear();          // 清空目标列表
        targets.push_back(target); // 只保留选中的目标
    }

    /**
     * @brief 处理目标命中效果
     * @param effIndex 效果索引
     *
     * 阻止默认效果，改为让目标将施法者传送到自己位置。
     * 注意：这里的逻辑比较特殊，是被选中的目标将BOSS传送到自己位置。
     */
    void HandleDummyHitTarget(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);  // 阻止默认效果
        // 让被选中的目标将施法者传送到自己位置
        GetHitUnit()->CastSpell(GetCaster(), SPELL_BLINK_TELEPORT, true);
    }

    /**
     * @brief 注册法术脚本钩子
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_talon_king_ikiss_blink::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget += SpellEffectFn(spell_talon_king_ikiss_blink::HandleDummyHitTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册利爪之王艾吉斯BOSS脚本
 *
 * 此函数在服务器启动时被调用，注册以下内容：
 * - 利爪之王艾吉斯BOSS AI
 * - 闪烁法术脚本
 */
void AddSC_boss_talon_king_ikiss()
{
    RegisterSethekkHallsCreatureAI(boss_talon_king_ikiss);
    RegisterSpellScript(spell_talon_king_ikiss_blink);
}
