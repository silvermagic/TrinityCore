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
 * @file boss_brutallus.cpp
 * @brief 太阳之井高地 - 布鲁塔卢斯Boss脚本模块
 *
 * 本模块实现了布鲁塔卢斯Boss的战斗逻辑，包括：
 * - 布鲁塔卢斯与玛德里戈萨的过场动画
 * - 战斗技能系统（流星斩、燃烧、践踏）
 * - 狂暴机制
 * - 与菲米丝的联动（死亡后触发菲米丝战斗）
 *
 * 布鲁塔卢斯是太阳之井高地的第二个Boss，是一个纯粹的装备检测型Boss。
 * 完成度: 80%
 * 待完成: 找到正确的方式启动过场动画
 */

/* ScriptData
SDName: Boss_Brutallus
SD%Complete: 80
SDComment: Find a way to start the intro, best code for the intro
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "sunwell_plateau.h"

/**
 * @brief 对话和喊话枚举
 *
 * 定义布鲁塔卢斯和玛德里戈萨的各种对话ID
 */
enum Quotes
{
    // 布鲁塔卢斯对话
    YELL_INTRO                          = 0,  // 过场动画对话
    YELL_INTRO_BREAK_ICE                = 1,  // 打破冰块
    YELL_INTRO_CHARGE                   = 2,  // 冲锋
    YELL_INTRO_KILL_MADRIGOSA           = 3,  // 杀死玛德里戈萨
    YELL_INTRO_TAUNT                    = 4,  // 嘲讽玩家

    YELL_AGGRO                          = 5,  // 开战喊话
    YELL_KILL                           = 6,  // 击杀玩家
    YELL_LOVE                           = 7,  // 践踏时喊话（表达对践踏的"爱"）
    YELL_BERSERK                        = 8,  // 狂暴
    YELL_DEATH                          = 9,  // 死亡

    // 玛德里戈萨对话
    YELL_MADR_ICE_BARRIER               = 0,  // 冰屏障
    YELL_MADR_INTRO                     = 1,  // 开场白
    YELL_MADR_ICE_BLOCK                 = 2,  // 冰块
    YELL_MADR_TRAP                      = 3,  // 陷阱
    YELL_MADR_DEATH                     = 4   // 死亡
};

/**
 * @brief 技能枚举
 *
 * 定义布鲁塔卢斯使用的所有技能ID
 */
enum Spells
{
    SPELL_METEOR_SLASH                  = 45150,  // 流星斩 - 对正面锥形区域造成大量伤害
    SPELL_BURN                          = 46394,  // 燃烧 - 持续火焰伤害，可跳跃传播
    SPELL_STOMP                         = 45185,  // 践踏 - 造成伤害并移除燃烧效果
    SPELL_BERSERK                       = 26662,  // 狂暴 - 6分钟后进入狂暴状态
    SPELL_DUAL_WIELD                    = 42459,  // 双持 - 被动技能

    // 过场动画技能
    SPELL_INTRO_FROST_BLAST             = 45203,  // 冰霜冲击 - 玛德里戈萨对布鲁塔卢斯使用
    SPELL_INTRO_FROSTBOLT               = 44843,  // 冰霜箭 - 持续施放
    SPELL_INTRO_ENCAPSULATE             = 45665,  // 封印 - 束缚布鲁塔卢斯
    SPELL_INTRO_ENCAPSULATE_CHANELLING  = 45661   // 封印引导
};

/**
 * @brief 布鲁塔卢斯Boss AI类
 *
 * 实现布鲁塔卢斯的完整战斗逻辑，包括过场动画和战斗阶段
 */
struct boss_brutallus : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化Boss AI，设置过场动画标志
     */
    boss_brutallus(Creature* creature) : BossAI(creature, DATA_BRUTALLUS)
    {
        Initialize();
        Intro = true;  // 初始状态：需要播放过场动画
    }

    /**
     * @brief 初始化所有计时器和状态变量
     *
     * 将所有计时器重置为初始值，状态标志重置为默认值
     *
     * @note 在Reset和构造函数中调用
     */
    void Initialize()
    {
        SlashTimer = 11000;      // 流星斩计时器 - 11秒首次施放
        StompTimer = 30000;      // 践踏计时器 - 30秒首次施放
        BurnTimer = 60000;       // 燃烧计时器 - 60秒首次施放
        BerserkTimer = 360000;   // 狂暴计时器 - 6分钟（360秒）

        IntroPhase = 0;          // 过场动画阶段
        IntroPhaseTimer = 0;     // 过场动画阶段计时器
        IntroFrostBoltTimer = 0; // 冰霜箭施放计时器

        IsIntro = false;         // 是否正在播放过场动画
        Enraged = false;         // 是否已狂暴
    }

    // 战斗技能计时器
    uint32 SlashTimer;       ///< 流星斩冷却计时器（毫秒）
    uint32 BurnTimer;        ///< 燃烧冷却计时器（毫秒）
    uint32 StompTimer;       ///< 践踏冷却计时器（毫秒）
    uint32 BerserkTimer;     ///< 狂暴计时器（毫秒）

    // 过场动画相关变量
    uint32 IntroPhase;       ///< 当前过场动画阶段（0-10）
    uint32 IntroPhaseTimer;  ///< 当前阶段持续时间（毫秒）
    uint32 IntroFrostBoltTimer; ///< 冰霜箭施放间隔（毫秒）

    // 状态标志
    bool Intro;    ///< 是否需要播放过场动画（首次触发时为true）
    bool IsIntro;  ///< 是否正在播放过场动画
    bool Enraged;  ///< 是否已进入狂暴状态

    /**
     * @brief 重置Boss状态
     *
     * 当Boss脱离战斗或重置时调用，恢复所有初始状态
     */
    void Reset() override
    {
        Initialize();

        // 施放双持被动技能
        DoCast(me, SPELL_DUAL_WIELD, true);

        BossAI::Reset();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 当Boss进入战斗状态时调用，播放开战喊话
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(YELL_AGGRO);

        BossAI::JustEngagedWith(who);
    }

    /**
     * @brief 击杀单位
     * @param victim 被击杀的目标
     *
     * 当Boss击杀玩家时播放击杀喊话
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(YELL_KILL);
    }

    /**
     * @brief Boss死亡
     * @param killer 击杀Boss的单位
     *
     * 处理Boss死亡事件，播放死亡喊话，并触发菲米丝战斗
     */
    void JustDied(Unit* killer) override
    {
        Talk(YELL_DEATH);

        // 设置菲米丝为特殊状态，准备激活菲米丝战斗
        instance->SetBossState(DATA_FELMYST, SPECIAL);
        BossAI::JustDied(killer);
    }

    /**
     * @brief 进入脱战模式
     * @param why 脱战原因
     *
     * 当Boss需要脱战时调用
     * 如果过场动画尚未播放，则不会脱战
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        if (!Intro)
            BossAI::EnterEvadeMode(why);
    }

    /**
     * @brief 启动过场动画
     *
     * 初始化并启动布鲁塔卢斯与玛德里戈萨之间的过场动画
     * 复活玛德里戈萨，设置双方为不可攻击状态，开始互殴
     *
     * @note 只有在Intro为true且未在播放过场动画时才会执行
     */
    void StartIntro()
    {
        if (!Intro || IsIntro)
            return;

        if (Creature* Madrigosa = instance->GetCreature(DATA_MADRIGOSA))
        {
            Madrigosa->Respawn();           // 复活玛德里戈萨
            Madrigosa->setActive(true);     // 激活对象
            Madrigosa->SetFarVisible(true); // 设置远距离可见
            IsIntro = true;
            // 设置双方生命值相等（为了公平的"战斗"）
            Madrigosa->SetMaxHealth(me->GetMaxHealth());
            Madrigosa->SetHealth(me->GetMaxHealth());
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 布鲁塔卢斯设为不可攻击
            me->Attack(Madrigosa, true);    // 攻击玛德里戈萨
            Madrigosa->Attack(me, true);    // 玛德里戈萨攻击布鲁塔卢斯
        }
        else
        {
            // 玛德里戈萨未找到，跳过过场动画
            TC_LOG_ERROR("scripts", "Madrigosa was not found");
            EndIntro();
        }
    }

    /**
     * @brief 结束过场动画
     *
     * 清除过场动画状态，移除不可攻击标志，允许玩家攻击Boss
     */
    void EndIntro()
    {
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        Intro = false;
        IsIntro = false;
    }

    /**
     * @brief 攻击开始
     * @param who 攻击目标
     *
     * 处理攻击开始逻辑
     * 在过场动画期间不执行攻击
     */
    void AttackStart(Unit* who) override
    {
        if (!who || Intro || IsIntro)
            return;
        BossAI::AttackStart(who);
    }

    /**
     * @brief 执行过场动画逻辑
     *
     * 按阶段执行完整的过场动画序列：
     * 阶段0: 玛德里戈萨冰屏障喊话
     * 阶段1: 玛德里戈萨开场白，双方面向对方
     * 阶段2: 布鲁塔卢斯回应
     * 阶段3: 玛德里戈萨使用冰霜冲击，开始持续施放冰霜箭
     * 阶段4: 布鲁塔卢斯打破冰块
     * 阶段5: 玛德里戈萨封印布鲁塔卢斯
     * 阶段6: 布鲁塔卢斯冲锋
     * 阶段7: 布鲁塔卢斯杀死玛德里戈萨
     * 阶段8: 布鲁塔卢斯胜利喊话
     * 阶段9: 布鲁塔卢斯嘲讽玩家
     * 阶段10: 结束过场动画
     */
    void DoIntro()
    {
        Creature* Madrigosa = instance->GetCreature(DATA_MADRIGOSA);
        if (!Madrigosa)
            return;

        switch (IntroPhase)
        {
            case 0:
                // 阶段0：玛德里戈萨使用冰屏障
                Madrigosa->AI()->Talk(YELL_MADR_ICE_BARRIER);
                IntroPhaseTimer = 7000;
                ++IntroPhase;
                break;
            case 1:
                // 阶段1：双方面向对方，玛德里戈萨开场白
                me->SetFacingToObject(Madrigosa);
                Madrigosa->SetFacingToObject(me);
                Madrigosa->AI()->Talk(YELL_MADR_INTRO, me);
                IntroPhaseTimer = 9000;
                ++IntroPhase;
                break;
            case 2:
                // 阶段2：布鲁塔卢斯回应
                Talk(YELL_INTRO, Madrigosa);
                IntroPhaseTimer = 13000;
                ++IntroPhase;
                break;
            case 3:
                // 阶段3：玛德里戈萨使用冰霜冲击，开始持续施放冰霜箭
                DoCast(me, SPELL_INTRO_FROST_BLAST);
                Madrigosa->SetDisableGravity(true);  // 玛德里戈萨悬浮
                me->AttackStop();
                Madrigosa->AttackStop();
                IntroFrostBoltTimer = 3000;  // 3秒后开始施放冰霜箭
                IntroPhaseTimer = 28000;
                ++IntroPhase;
                break;
            case 4:
                // 阶段4：布鲁塔卢斯打破冰块
                Talk(YELL_INTRO_BREAK_ICE);
                IntroPhaseTimer = 6000;
                ++IntroPhase;
                break;
            case 5:
                // 阶段5：玛德里戈萨封印布鲁塔卢斯
                Madrigosa->CastSpell(me, SPELL_INTRO_ENCAPSULATE_CHANELLING, false);
                Madrigosa->AI()->Talk(YELL_MADR_TRAP);
                DoCast(me, SPELL_INTRO_ENCAPSULATE);
                IntroPhaseTimer = 11000;
                ++IntroPhase;
                break;
            case 6:
                // 阶段6：布鲁塔卢斯冲锋
                Talk(YELL_INTRO_CHARGE);
                IntroPhaseTimer = 5000;
                ++IntroPhase;
                break;
            case 7:
                // 阶段7：布鲁塔卢斯杀死玛德里戈萨
                Unit::Kill(me, Madrigosa);
                Madrigosa->AI()->Talk(YELL_MADR_DEATH);
                me->SetFullHealth();  // 恢复满血
                me->AttackStop();
                IntroPhaseTimer = 4000;
                ++IntroPhase;
                break;
            case 8:
                // 阶段8：布鲁塔卢斯胜利喊话，玛德里戈萨进入尸体状态
                Talk(YELL_INTRO_KILL_MADRIGOSA);
                me->SetOrientation(0.14f);
                me->StopMoving();
                Madrigosa->setDeathState(CORPSE);
                IntroPhaseTimer = 8000;
                ++IntroPhase;
                break;
            case 9:
                // 阶段9：布鲁塔卢斯嘲讽玩家
                Talk(YELL_INTRO_TAUNT);
                IntroPhaseTimer = 5000;
                ++IntroPhase;
                break;
            case 10:
                // 阶段10：结束过场动画
                EndIntro();
                break;
        }
    }

    /**
     * @brief 视线内移动处理
     * @param who 进入视线范围的单位
     *
     * 当玩家进入Boss视线范围时触发
     * 如果需要播放过场动画，则启动过场动画
     */
    void MoveInLineOfSight(Unit* who) override
    {
        if (!me->IsValidAttackTarget(who))
            return;

        if (Intro)
            instance->SetBossState(DATA_BRUTALLUS, SPECIAL);

        if (Intro && !IsIntro)
            StartIntro();

        if (!Intro)
            BossAI::MoveInLineOfSight(who);
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主更新函数，处理过场动画和战斗逻辑
     *
     * 战斗阶段技能循环：
     * - 流星斩：每11秒施放一次，对前方锥形区域造成伤害
     * - 践踏：每30秒施放一次，移除燃烧效果
     * - 燃烧：随机对玩家施放，持续火焰伤害，可跳跃传播
     * - 狂暴：6分钟后进入狂暴状态
     */
    void UpdateAI(uint32 diff) override
    {
        // 处理过场动画
        if (IsIntro)
        {
            if (IntroPhaseTimer <= diff)
                DoIntro();
            else IntroPhaseTimer -= diff;

            // 阶段3时持续施放冰霜箭
            if (IntroPhase == 3 + 1)
            {
                if (IntroFrostBoltTimer <= diff)
                {
                    if (Creature* Madrigosa = instance->GetCreature(DATA_MADRIGOSA))
                    {
                        Madrigosa->CastSpell(me, SPELL_INTRO_FROSTBOLT, true);
                        IntroFrostBoltTimer = 2000;  // 每2秒施放一次
                    }
                }
                else
                    IntroFrostBoltTimer -= diff;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        // 战斗阶段逻辑
        if (!UpdateVictim() || IsIntro)
            return;

        // 流星斩：对前方锥形区域造成大量伤害，需要团队分摊
        if (SlashTimer <= diff)
        {
            DoCastVictim(SPELL_METEOR_SLASH);
            SlashTimer = 11000;
        } else SlashTimer -= diff;

        // 践踏：造成伤害并移除燃烧效果
        if (StompTimer <= diff)
        {
            Talk(YELL_LOVE);
            DoCastVictim(SPELL_STOMP);
            StompTimer = 30000;
        } else StompTimer -= diff;

        // 燃烧：随机选择没有燃烧效果的玩家施放
        if (BurnTimer <= diff)
        {
            // 选择没有燃烧效果的随机玩家
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true, true, -SPELL_BURN))
                target->CastSpell(target, SPELL_BURN, true);
            BurnTimer = urand(60000, 180000);  // 60-180秒随机间隔
        } else BurnTimer -= diff;

        // 狂暴：6分钟后进入狂暴状态
        if (BerserkTimer < diff && !Enraged)
        {
            Talk(YELL_BERSERK);
            DoCast(me, SPELL_BERSERK);
            Enraged = true;
        } else BerserkTimer -= diff;

        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 燃烧技能光环脚本
 *
 * 处理燃烧（Burn）技能的周期性伤害更新逻辑
 * 燃烧效果每11跳伤害翻倍，是一个递增伤害的Debuff
 */
// 46394 - Burn
class spell_brutallus_burn : public AuraScript
{
    PrepareAuraScript(spell_brutallus_burn);

    /**
     * @brief 处理周期性效果更新
     * @param aurEff 光环效果指针
     *
     * 每隔11跳将伤害翻倍
     * 这是一个递增伤害机制，使得燃烧效果越来越危险
     */
    void HandleEffectPeriodicUpdate(AuraEffect* aurEff)
    {
        // 每11跳伤害翻倍
        if (aurEff->GetTickNumber() % 11 == 0)
            aurEff->SetAmount(aurEff->GetAmount() * 2);
    }

    /**
     * @brief 注册光环脚本
     */
    void Register() override
    {
        OnEffectUpdatePeriodic += AuraEffectUpdatePeriodicFn(spell_brutallus_burn::HandleEffectPeriodicUpdate, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

/**
 * @brief 践踏技能脚本
 *
 * 处理践踏（Stomp）技能的附加效果
 * 践踏会移除目标的燃烧效果，帮助玩家清理燃烧Debuff
 */
// 45185 - Stomp
class spell_brutallus_stomp : public SpellScript
{
    PrepareSpellScript(spell_brutallus_stomp);

    /**
     * @brief 验证技能信息
     * @param spellInfo 技能信息指针
     * @return 始终返回true
     *
     * 确保燃烧技能存在于技能系统中
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BURN });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引（未使用）
     *
     * 移除被击中目标的燃烧光环效果
     * 这是践踏技能的战略意义之一：清理燃烧Debuff
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_BURN);
    }

    /**
     * @brief 注册技能脚本
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_brutallus_stomp::HandleScript, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 注册布鲁塔卢斯Boss脚本
 *
 * 将布鲁塔卢斯Boss和相关技能脚本注册到脚本系统中
 */
void AddSC_boss_brutallus()
{
    RegisterSunwellPlateauCreatureAI(boss_brutallus);
    RegisterSpellScript(spell_brutallus_burn);
    RegisterSpellScript(spell_brutallus_stomp);
}
