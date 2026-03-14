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
 * @file boss_murmur.cpp
 * @brief 暗影迷宫副本BOSS - 摩摩尔的AI脚本实现
 *
 * 本模块实现了摩摩尔BOSS的战斗逻辑，包括：
 * - 技能施放逻辑（音速爆破、摩摩尔的触碰、磁力牵引等）
 * - 生命值管理（摩摩尔初始只有40%生命值）
 * - 战斗状态切换和威胁管理
 * - 法术脚本（音速爆破伤害计算、雷霆风暴目标过滤等）
 *
 * 摩摩尔是暗影迷宫的最终BOSS，具有独特的战斗机制：
 * - 不会移动，定点战斗
 * - 当近战目标不在攻击范围内时会施放共鸣
 * - 音速爆破会造成80%最大生命值伤害
 */

/* ScriptData
SDName: Boss_Murmur
SD%Complete: 90
SDComment: Timers may be incorrect
SDCategory: Auchindoun, Shadow Labyrinth
EndScriptData */

#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "shadow_labyrinth.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 文本枚举 - BOSS的文本消息
 */
enum Texts
{
    EMOTE_SONIC_BOOM            = 0   ///< 音速爆破前的表情文本
};

/**
 * @brief 法术枚举 - 摩摩尔使用的所有法术ID
 */
enum Spells
{
    SPELL_RESONANCE             = 33657,  ///< 共鸣 - 当目标不在近战范围时施放
    SPELL_MAGNETIC_PULL         = 33689,  ///< 磁力牵引 - 将随机目标拉到摩摩尔身边
    SPELL_SONIC_SHOCK           = 38797,  ///< 音速冲击 - 英雄模式技能
    SPELL_THUNDERING_STORM      = 39365,  ///< 雷霆风暴 - 英雄模式技能
    SPELL_SONIC_BOOM_CAST       = 33923,  ///< 音速爆破（施放法术）
    SPELL_SONIC_BOOM_EFFECT     = 33666,  ///< 音速爆破（效果法术）
    SPELL_MURMURS_TOUCH         = 33711,  ///< 摩摩尔的触碰（普通模式）
    SPELL_MURMURS_TOUCH_H       = 38794,  ///< 摩摩尔的触碰（英雄模式）

    SPELL_MURMURS_TOUCH_DUMMY   = 33760,  ///< 摩摩尔的触碰（视觉效果）
    SPELL_SHOCKWAVE             = 33686,  ///< 冲击波
    SPELL_SHOCKWAVE_KNOCK_BACK  = 33673   ///< 冲击波击退效果
};

/**
 * @brief 事件枚举 - 战斗事件类型
 */
enum Events
{
    EVENT_SONIC_BOOM            = 1,  ///< 音速爆破事件
    EVENT_MURMURS_TOUCH         = 2,  ///< 摩摩尔的触碰事件
    EVENT_RESONANCE             = 3,  ///< 共鸣事件
    EVENT_MAGNETIC_PULL         = 4,  ///< 磁力牵引事件
    EVENT_THUNDERING_STORM      = 5,  ///< 雷霆风暴事件（英雄模式）
    EVENT_SONIC_SHOCK           = 6   ///< 音速冲击事件（英雄模式）
};

/**
 * @brief 摩摩尔BOSS AI结构体
 *
 * 实现摩摩尔的战斗AI，继承自BossAI基类。
 * 摩摩尔是一个定点战斗的BOSS，不会移动，通过磁力牵引和共鸣机制处理远程玩家。
 */
struct boss_murmur : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置DATA_MURMUR作为BOSS标识，
     * 并禁用战斗移动（摩摩尔不移动）。
     */
    boss_murmur(Creature* creature) : BossAI(creature, DATA_MURMUR)
    {
        SetCombatMovement(false);  // 摩摩尔不移动，定点战斗
    }

    /**
     * @brief 重置BOSS状态
     *
     * 在战斗重置时调用，负责：
     * - 重置事件调度器
     * - 重新调度所有技能事件
     * - 设置摩摩尔生命值为40%（数据库应设置RegenHealth=0防止回血）
     * - 重置玩家伤害需求
     *
     * @调用时机 当BOSS脱离战斗或重置时调用
     */
    void Reset() override
    {
        _Reset();  // 调用基类重置方法

        // 调度技能事件
        events.ScheduleEvent(EVENT_SONIC_BOOM, 30s);           // 音速爆破，每30秒施放
        events.ScheduleEvent(EVENT_MURMURS_TOUCH, 8s, 20s);    // 摩摩尔的触碰，8-20秒随机间隔
        events.ScheduleEvent(EVENT_RESONANCE, 5s);             // 共鸣，每5秒检查一次
        events.ScheduleEvent(EVENT_MAGNETIC_PULL, 15s, 30s);   // 磁力牵引，15-30秒随机间隔

        // 英雄模式额外技能
        if (IsHeroic())
        {
            events.ScheduleEvent(EVENT_THUNDERING_STORM, 15s); // 雷霆风暴，每15秒
            events.ScheduleEvent(EVENT_SONIC_SHOCK, 10s);      // 音速冲击，每10秒
        }

        // 设置摩摩尔生命值为最大生命值的40%
        // 数据库应该有 RegenHealth=0 来防止自动回血
        uint32 hp = me->CountPctFromMaxHealth(40);
        if (hp)
            me->SetHealth(hp);
        me->ResetPlayerDamageReq();  // 重置玩家伤害需求，用于记录首杀
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主战斗循环，每帧调用一次。负责：
     * - 检查是否有战斗目标
     * - 更新事件调度器
     * - 处理施法状态（施法时不执行其他动作）
     * - 执行已触发的事件
     * - 管理威胁列表（当近战目标太远时重置威胁）
     * - 执行近战攻击
     *
     * @调用时机 每个游戏tick调用（约每秒多次）
     * @性能注意事项 频繁调用，避免在此函数中执行重量级操作
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        events.Update(diff);  // 更新事件调度器

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有已触发的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SONIC_BOOM:
                    // 音速爆破：摩摩尔的主要伤害技能，造成80%最大生命值伤害
                    Talk(EMOTE_SONIC_BOOM);  // 发送表情文本警告玩家
                    DoCast(me, SPELL_SONIC_BOOM_CAST);  // 施放音速爆破
                    events.ScheduleEvent(EVENT_SONIC_BOOM, 30s);
                    events.ScheduleEvent(EVENT_RESONANCE, 1500ms);  // 1.5秒后检查是否需要施放共鸣
                    break;

                case EVENT_MURMURS_TOUCH:
                    // 摩摩尔的触碰：给随机玩家施加Debuff，持续14秒后爆炸
                    // 选择80码内的随机玩家目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 80.0f, true))
                        DoCast(target, SPELL_MURMURS_TOUCH);
                    events.ScheduleEvent(EVENT_MURMURS_TOUCH, 25s, 35s);
                    break;

                case EVENT_RESONANCE:
                    // 共鸣：当摩摩尔的目标不在近战范围内时施放
                    // 这是一个惩罚机制，迫使玩家必须近战
                    if (!(me->IsWithinMeleeRange(me->GetVictim())))
                    {
                        DoCast(me, SPELL_RESONANCE);
                        events.ScheduleEvent(EVENT_RESONANCE, 5s);
                    }
                    break;

                case EVENT_MAGNETIC_PULL:
                    // 磁力牵引：将随机玩家拉到摩摩尔身边
                    // 选择任意距离的随机玩家
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                    {
                        DoCast(target, SPELL_MAGNETIC_PULL);
                        events.ScheduleEvent(EVENT_MAGNETIC_PULL, 15s, 30s);
                        break;
                    }
                    // 如果没有找到有效目标，500毫秒后重试
                    events.ScheduleEvent(EVENT_MAGNETIC_PULL, 500ms);
                    break;

                case EVENT_THUNDERING_STORM:
                    // 雷霆风暴（英雄模式）：对25-100码范围内的所有玩家造成自然伤害
                    DoCastAOE(SPELL_THUNDERING_STORM, true);
                    events.ScheduleEvent(EVENT_THUNDERING_STORM, 15s);
                    break;

                case EVENT_SONIC_SHOCK:
                    // 音速冲击（英雄模式）：对20码内的随机目标造成奥术伤害
                    // 不需要视线检查，可以攻击隐身目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 20.0f, false))
                        DoCast(target, SPELL_SONIC_SHOCK);
                    events.ScheduleEvent(EVENT_SONIC_SHOCK, 10s, 20s);
                    break;
            }

            // 如果施放了法术，则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果攻击未就绪，则等待
        if (!me->isAttackReady())
            return;

        // 如果当前目标不在近战范围内，重置其威胁值
        // 这是为了让BOSS切换到更近的目标
        if (!me->IsWithinMeleeRange(me->GetVictim()))
            me->GetThreatManager().ResetThreat(me->GetVictim());

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 音速爆破法术脚本
 *
 * 处理音速爆破（33923/38796）的施放逻辑。
 * 当法术施放完成时，触发音速爆破效果。
 */
// 33923, 38796 - Sonic Boom
class spell_murmur_sonic_boom : public SpellScript
{
    PrepareSpellScript(spell_murmur_sonic_boom);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 如果依赖的法术存在则返回true
     *
     * 确保音速爆破效果法术存在于数据库中。
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SONIC_BOOM_EFFECT });
    }

    /**
     * @brief 处理法术效果
     * @param effIndex 效果索引（未使用）
     *
     * 当音速爆破施放法术命中时，施放音速爆破效果法术。
     * 这是一个哑弹法术，实际效果由效果法术产生。
     */
    void HandleEffect(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(nullptr, SPELL_SONIC_BOOM_EFFECT, true);
    }

    /**
     * @brief 注册法术脚本钩子
     *
     * 将HandleEffect方法绑定到EFFECT_0的SPELL_EFFECT_DUMMY效果上。
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_murmur_sonic_boom::HandleEffect, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 音速爆破效果法术脚本
 *
 * 处理音速爆破效果（33666/38795）的伤害计算。
 * 音速爆破造成目标最大生命值80%的伤害。
 */
// 33666, 38795 - Sonic Boom Effect
class spell_murmur_sonic_boom_effect : public SpellScript
{
    PrepareSpellScript(spell_murmur_sonic_boom_effect);

    /**
     * @brief 计算伤害
     *
     * 当法术命中目标时，设置伤害为目标最大生命值的80%。
     * 这是摩摩尔最具威胁的技能，玩家必须在施放前躲避。
     */
    void CalcDamage()
    {
        if (Unit* target = GetHitUnit())
            SetHitDamage(target->CountPctFromMaxHealth(80)); /// @todo: find correct value，需要确认准确数值
    }

    /**
     * @brief 注册法术脚本钩子
     *
     * 将CalcDamage方法绑定到法术命中事件。
     */
    void Register() override
    {
        OnHit += SpellHitFn(spell_murmur_sonic_boom_effect::CalcDamage);
    }
};

/**
 * @brief 雷霆风暴目标检查函子
 *
 * 用于过滤雷霆风暴法术的目标。
 * 雷霆风暴只影响25-100码范围内的玩家（排除过近或过远的目标）。
 */
class ThunderingStormCheck
{
    public:
        /**
         * @brief 构造函数
         * @param source 施法者对象
         */
        ThunderingStormCheck(WorldObject* source) : _source(source) { }

        /**
         * @brief 目标过滤操作符
         * @param obj 待检查的世界对象
         * @return 如果目标应该被移除（不在有效范围内）则返回true
         *
         * 检查目标是否在25-100码范围内。
         * 返回true表示目标应该从目标列表中移除。
         */
        bool operator()(WorldObject* obj)
        {
            float distSq = _source->GetExactDist2dSq(obj);
            // 移除距离小于25码或大于100码的目标
            return distSq < (25.0f * 25.0f) || distSq > (100.0f * 100.0f);
        }

    private:
        WorldObject const* _source;  ///< 施法者对象
};

/**
 * @brief 雷霆风暴法术脚本
 *
 * 处理雷霆风暴（39365）的目标选择逻辑。
 * 雷霆风暴只对25-100码范围内的敌人造成伤害。
 */
// 39365 - Thundering Storm
class spell_murmur_thundering_storm : public SpellScript
{
    PrepareSpellScript(spell_murmur_thundering_storm);

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 从目标列表中移除不在有效范围内的目标。
     * 使用ThunderingStormCheck函子进行过滤。
     */
    void FilterTarget(std::list<WorldObject*>& targets)
    {
        targets.remove_if(ThunderingStormCheck(GetCaster()));
    }

    /**
     * @brief 注册法术脚本钩子
     *
     * 将FilterTarget方法绑定到区域目标选择事件。
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_murmur_thundering_storm::FilterTarget, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
    }
};

/**
 * @brief 摩摩尔的触碰光环脚本
 *
 * 处理摩摩尔的触碰（33711/38794）的周期性触发效果。
 * 这个Debuff会持续14秒（普通）或7秒（英雄），
 * 然后在目标位置产生冲击波并击退周围玩家。
 */
// 33711, 38794 - Murmur's Touch
class spell_murmur_murmurs_touch : public AuraScript
{
    PrepareAuraScript(spell_murmur_murmurs_touch);

    /**
     * @brief 验证法术信息
     * @param spellInfo 法术信息（未使用）
     * @return 如果所有依赖的法术都存在则返回true
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_MURMURS_TOUCH_DUMMY,  // 视觉效果法术
            SPELL_SHOCKWAVE,             // 冲击波法术
            SPELL_SHOCKWAVE_KNOCK_BACK   // 击退效果法术
        });
    }

    /**
     * @brief 周期性效果触发
     * @param aurEff 光环效果指针
     *
     * 在Debuff持续期间定期调用。
     * 普通模式：14秒内触发14次，在特定的tick施加视觉效果和冲击波
     * 英雄模式：7秒内触发7次，触发频率更高
     *
     * @调用时机 每秒调用一次（根据光环的周期性设置）
     */
    void OnPeriodic(AuraEffect const* aurEff)
    {
        Unit* target = GetTarget();

        switch (GetId())
        {
            case SPELL_MURMURS_TOUCH:  // 普通模式
                switch (aurEff->GetTickNumber())
                {
                    case 7:   // 第7次tick
                    case 10:  // 第10次tick
                    case 12:  // 第12次tick
                    case 13:  // 第13次tick
                        target->CastSpell(target, SPELL_MURMURS_TOUCH_DUMMY, true);
                        break;
                    case 14:  // 第14次tick（最后一次）
                        target->CastSpell(target, SPELL_MURMURS_TOUCH_DUMMY, true);
                        target->CastSpell(target, SPELL_SHOCKWAVE, true);          // 施放冲击波
                        target->CastSpell(target, SPELL_SHOCKWAVE_KNOCK_BACK, true); // 施放击退效果
                        break;
                    default:
                        break;
                }
                break;

            case SPELL_MURMURS_TOUCH_H:  // 英雄模式
                switch (aurEff->GetTickNumber())
                {
                    case 3:  // 第3次tick
                    case 6:  // 第6次tick
                        target->CastSpell(target, SPELL_MURMURS_TOUCH_DUMMY, true);
                        break;
                    case 7:  // 第7次tick（最后一次）
                        target->CastSpell(target, SPELL_MURMURS_TOUCH_DUMMY, true);
                        target->CastSpell(target, SPELL_SHOCKWAVE, true);
                        target->CastSpell(target, SPELL_SHOCKWAVE_KNOCK_BACK, true);
                        break;
                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }

    /**
     * @brief 注册光环脚本钩子
     *
     * 将OnPeriodic方法绑定到周期性触发效果。
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_murmur_murmurs_touch::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 注册摩摩尔BOSS脚本
 *
 * 此函数在服务器启动时被调用，注册以下内容：
 * - 摩摩尔BOSS AI
 * - 音速爆破法术脚本
 * - 音速爆破效果法术脚本
 * - 雷霆风暴法术脚本
 * - 摩摩尔的触碰光环脚本
 */
void AddSC_boss_murmur()
{
    RegisterShadowLabyrinthCreatureAI(boss_murmur);
    RegisterSpellScript(spell_murmur_sonic_boom);
    RegisterSpellScript(spell_murmur_sonic_boom_effect);
    RegisterSpellScript(spell_murmur_thundering_storm);
    RegisterSpellScript(spell_murmur_murmurs_touch);
}
