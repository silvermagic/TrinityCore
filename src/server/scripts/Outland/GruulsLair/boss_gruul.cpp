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
 * @file boss_gruul.cpp
 * @brief 格鲁尔Boss战脚本
 *
 * 本模块实现了格鲁尔的巢穴最终Boss格鲁尔的战斗逻辑，包括：
 * - 多阶段战斗机制（地面猛击、石化、粉碎等）
 * - 成长机制（每30秒体型增大，伤害提升）
 * - 塌陷机制（随时间增加频率）
 * - 粉碎技能（石化后造成AOE伤害，距离越近伤害越高）
 *
 * @todo 待完善：地面猛击的击退效果需要更精确的实现
 * 目前使用临时的磁力牵引和击退技能来模拟，但与原版机制略有不同
 */

/*
TO-DO:
Slighly(400ms) after spell cast 33965 creatures 19198 are spawned. I guess he forces all enemies including pets(9 summoned units
and 9 units in his threatlist) to cast 39186(19198 were created by that spell(sniff)). Summoned by that spell creature 19198 casts 33496
on self after being summoned. Then probably they casts 33497(Pull Towards: (150)) on their creators and that's how that knockback is handled.
If you look closely, players are knocked to random destinations with random angles, means there is no only one spell which handles knockback.
19198 despawns after 800ms after being summoned.
*/

#include "ScriptMgr.h"
#include "gruuls_lair.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"

/**
 * @brief 格鲁尔的对白枚举
 */
enum Yells
{
    SAY_AGGRO                   = 0,  ///< 战斗开始
    SAY_SLAM                    = 1,  ///< 地面猛击
    SAY_SHATTER                 = 2,  ///< 粉碎
    SAY_SLAY                    = 3,  ///< 击杀玩家
    SAY_DEATH                   = 4,  ///< 死亡

    EMOTE_GROW                  = 5   ///< 成长表情
};

/**
 * @brief 格鲁尔的技能枚举
 */
enum Spells
{
    SPELL_GROWTH                = 36300,  ///< 成长：体型增大，伤害提升
    SPELL_CAVE_IN               = 36240,  ///< 塌陷：随机位置的AoE伤害
    SPELL_GROUND_SLAM           = 33525,  ///< 地面猛击：AoE击退效果，玩家会被击退到随机方向
    SPELL_REVERBERATION         = 36297,  ///< 回响：沉默效果
    SPELL_SHATTER               = 33654,  ///< 粉碎：解除石化并造成AoE伤害

    SPELL_SHATTER_EFFECT        = 33671,  ///< 粉碎效果：根据距离计算伤害
    SPELL_HURTFUL_STRIKE        = 33813,  ///< 致命打击：对第二仇恨目标的高伤害技能
    SPELL_STONED                = 33652,  ///< 石化：玩家自我施放的石化效果

    SPELL_MAGNETIC_PULL         = 28337,  ///< 磁力牵引：临时击退效果
    SPELL_KNOCK_BACK            = 24199   ///< 击退：临时击退效果
};

/**
 * @brief 格鲁尔的事件枚举
 */
enum Events
{
    EVENT_GROWTH = 1,          ///< 成长事件
    EVENT_CAVE_IN,             ///< 塌陷事件
    EVENT_CAVE_IN_STATIC,      ///< 塌陷静态计时器（用于递减间隔）
    EVENT_GROUND_SLAM,         ///< 地面猛击事件
    EVENT_HURTFUL_STRIKE,      ///< 致命打击事件
    EVENT_REVERBERATION        ///< 回响事件
};

/**
 * @class boss_gruul
 * @brief 格鲁尔Boss脚本类
 *
 * 继承自CreatureScript，实现格鲁尔的战斗逻辑
 */
class boss_gruul : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_gruul() : CreatureScript("boss_gruul") { }

        /**
         * @struct boss_gruulAI
         * @brief 格鲁尔AI结构体
         *
         * 继承自BossAI，实现格鲁尔的核心战斗逻辑
         * 战斗机制：
         * - 成长：每30秒体型和伤害增加（最多30次）
         * - 塌陷：初始30秒间隔，随时间递减到4秒
         * - 地面猛击+粉碎：将玩家击退并石化，然后粉碎造成大量伤害
         * - 致命打击：对第二仇恨目标施放
         * - 回响：群体沉默
         */
        struct boss_gruulAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_gruulAI(Creature* creature) : BossAI(creature, DATA_GRUUL)
            {
                Initialize();
            }

            /**
             * @brief 初始化所有计时器和状态
             *
             * 设置初始技能冷却时间
             */
            void Initialize()
            {
                m_uiGrowth_Timer = 30000;           // 成长：30秒间隔
                m_uiCaveIn_Timer = 27000;           // 塌陷：初始27秒
                m_uiCaveIn_StaticTimer = 30000;     // 塌陷静态计时器：用于递减
                m_uiGroundSlamTimer = 35000;        // 地面猛击：35秒间隔
                m_bPerformingGroundSlam = false;    // 是否正在执行地面猛击序列
                m_uiHurtfulStrike_Timer = 8000;     // 致命打击：8秒间隔
                m_uiReverberation_Timer = 60000 + 45000;  // 回响：初始60-105秒随机
            }

            uint32 m_uiGrowth_Timer;           ///< 成长技能计时器
            uint32 m_uiCaveIn_Timer;           ///< 塌陷技能计时器
            uint32 m_uiCaveIn_StaticTimer;     ///< 塌陷静态计时器（递减用）
            uint32 m_uiGroundSlamTimer;        ///< 地面猛击计时器
            uint32 m_uiHurtfulStrike_Timer;    ///< 致命打击计时器
            uint32 m_uiReverberation_Timer;    ///< 回响计时器

            bool m_bPerformingGroundSlam;      ///< 是否正在执行地面猛击+粉碎序列

            /**
             * @brief 重置战斗
             *
             * Boss脱离战斗时调用，重置所有数据
             */
            void Reset() override
            {
                _Reset();
                Initialize();
            }

            /**
             * @brief 进入战斗
             * @param who 仇恨目标
             *
             * 触发战斗开始，播放战斗开始台词
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_AGGRO);
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 当击杀玩家时播放击杀台词
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

            /**
             * @brief 死亡
             * @param killer 击杀者
             *
             * Boss死亡时播放死亡台词
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                Talk(SAY_DEATH);
            }

            /**
             * @brief 法术命中目标回调
             * @param target 目标
             * @param spellInfo 法术信息
             *
             * 处理地面猛击和粉碎的特殊逻辑：
             * - 地面猛击：对玩家施放随机方向的击退
             * - 粉碎：结束地面猛击序列，恢复追击移动
             *
             * @note 地面猛击的击退实现是临时的，使用磁力牵引和击退技能模拟
             */
            void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
            {
                // 模拟地面猛击的击退效果
                // 注意：当前实现可能导致跌落伤害，原版设计应该不会造成跌落伤害
                if (spellInfo->Id == SPELL_GROUND_SLAM)
                {
                    if (target->GetTypeId() == TYPEID_PLAYER)
                    {
                        // 随机选择击退方式
                        switch (urand(0, 1))
                        {
                            case 0:
                                target->CastSpell(target, SPELL_MAGNETIC_PULL, me->GetGUID());
                                break;

                            case 1:
                                target->CastSpell(target, SPELL_KNOCK_BACK, me->GetGUID());
                                break;
                        }
                    }
                }

                // 粉碎法术命中后的处理
                // 这部分应该在核心代码中处理，但为了兼容性放在这里
                if (spellInfo->Id == SPELL_SHATTER)
                {
                    /// @todo 使用事件映射来处理这个逻辑
                    // 如果还在执行地面猛击序列，则清除状态
                    if (m_bPerformingGroundSlam)
                    {
                        m_bPerformingGroundSlam = false;

                        // 恢复追击移动
                        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                        {
                            if (me->GetVictim())
                                me->GetMotionMaster()->MoveChase(me->GetVictim());
                        }
                    }
                }
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 核心战斗逻辑循环，处理所有技能的施放
             *
             * 性能注意事项：
             * - 成长技能每30秒施放一次，最多30次
             * - 塌陷技能间隔递减，最小4秒
             * - 地面猛击序列需要暂停其他技能
             */
            void UpdateAI(uint32 diff) override
            {
                // 检查是否有有效目标
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，跳过本次更新
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                /// @todo: 将这些计时器转换为事件映射系统

                // 成长技能
                // 格鲁尔最多可以施放此技能30次
                if (m_uiGrowth_Timer <= diff)
                {
                    Talk(EMOTE_GROW);
                    DoCast(me, SPELL_GROWTH);
                    m_uiGrowth_Timer = 30000;
                }
                else
                    m_uiGrowth_Timer -= diff;

                // 地面猛击+粉碎序列
                if (m_bPerformingGroundSlam)
                {
                    if (m_uiGroundSlamTimer <= diff)
                    {
                        m_uiGroundSlamTimer = 120000;  // 重置为2分钟
                        m_uiHurtfulStrike_Timer = 8000;

                        // 给玩家一点时间来应对粉碎的伤害
                        if (m_uiReverberation_Timer < 10000)
                            m_uiReverberation_Timer += 10000;

                        DoCast(me, SPELL_SHATTER);
                    }
                    else
                        m_uiGroundSlamTimer -= diff;
                }
                else
                {
                    // 致命打击
                    if (m_uiHurtfulStrike_Timer <= diff)
                    {
                        Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 1);

                        // 如果有第二仇恨目标且在近战范围内，对第二目标施放
                        // 否则对主仇恨目标施放
                        if (target && me->IsWithinMeleeRange(me->GetVictim()))
                            DoCast(target, SPELL_HURTFUL_STRIKE);
                        else
                            DoCastVictim(SPELL_HURTFUL_STRIKE);

                        m_uiHurtfulStrike_Timer = 8000;
                    }
                    else
                        m_uiHurtfulStrike_Timer -= diff;

                    // 回响（沉默）
                    if (m_uiReverberation_Timer <= diff)
                    {
                        DoCastVictim(SPELL_REVERBERATION, true);
                        m_uiReverberation_Timer = urand(15000, 25000);
                    }
                    else
                        m_uiReverberation_Timer -= diff;

                    // 塌陷
                    if (m_uiCaveIn_Timer <= diff)
                    {
                        // 对随机目标施放塌陷
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            DoCast(target, SPELL_CAVE_IN);

                        // 塌陷间隔递减，最小4秒
                        if (m_uiCaveIn_StaticTimer >= 4000)
                            m_uiCaveIn_StaticTimer -= 2000;

                        m_uiCaveIn_Timer = m_uiCaveIn_StaticTimer;
                    }
                    else
                        m_uiCaveIn_Timer -= diff;

                    // 地面猛击 -> 格隆领主之握 -> 石化 -> 粉碎
                    if (m_uiGroundSlamTimer <= diff)
                    {
                        // 清除移动并进入空闲状态
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MoveIdle();

                        m_bPerformingGroundSlam = true;
                        m_uiGroundSlamTimer = 10000;  // 10秒后施放粉碎

                        DoCast(me, SPELL_GROUND_SLAM);
                    }
                    else
                        m_uiGroundSlamTimer -= diff;

                    DoMeleeAttackIfReady();
                }
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetGruulsLairAI<boss_gruulAI>(creature);
        }
};

/**
 * @class spell_gruul_shatter
 * @brief 粉碎法术脚本
 *
 * 处理粉碎技能的核心逻辑：
 * - 移除石化效果
 * - 触发粉碎效果，造成基于距离的伤害
 */
class spell_gruul_shatter : public SpellScriptLoader
{
    public:
        spell_gruul_shatter() : SpellScriptLoader("spell_gruul_shatter") { }

        /**
         * @class spell_gruul_shatter_SpellScript
         * @brief 粉碎法术的具体实现
         */
        class spell_gruul_shatter_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_gruul_shatter_SpellScript);

            /**
             * @brief 验证法术
             * @param spell 法术信息
             * @return 验证是否成功
             */
            bool Validate(SpellInfo const* /*spell*/) override
            {
                return ValidateSpellInfo({ SPELL_STONED, SPELL_SHATTER_EFFECT });
            }

            /**
             * @brief 处理脚本效果
             * @param effIndex 效果索引
             *
             * 当粉碎命中目标时：
             * 1. 移除石化效果
             * 2. 施放粉碎效果，造成基于距离的伤害
             */
            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                {
                    target->RemoveAurasDueToSpell(SPELL_STONED);
                    target->CastSpell(nullptr, SPELL_SHATTER_EFFECT, true);
                }
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_gruul_shatter_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_gruul_shatter_SpellScript();
        }
};

/**
 * @class spell_gruul_shatter_effect
 * @brief 粉碎效果法术脚本
 *
 * 计算粉碎效果的实际伤害，基于玩家之间的距离：
 * - 距离越近，伤害越高
 * - 伤害计算公式：基础伤害 * (半径 - 距离) / 半径
 */
class spell_gruul_shatter_effect : public SpellScriptLoader
{
    public:
        spell_gruul_shatter_effect() : SpellScriptLoader("spell_gruul_shatter_effect") { }

        /**
         * @class spell_gruul_shatter_effect_SpellScript
         * @brief 粉碎效果伤害计算实现
         */
        class spell_gruul_shatter_effect_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_gruul_shatter_effect_SpellScript);

            /**
             * @brief 计算伤害
             *
             * 根据施法者与目标之间的距离计算伤害
             * - 距离小于1码时造成全额伤害
             * - 距离超过法术半径时造成最小伤害
             */
            void CalculateDamage()
            {
                if (!GetHitUnit())
                    return;

                float radius = GetEffectInfo(EFFECT_0).CalcRadius(GetCaster());
                if (!radius)
                    return;

                float distance = GetCaster()->GetDistance2d(GetHitUnit());
                if (distance > 1.0f)
                    SetHitDamage(int32(GetHitDamage() * ((radius - distance) / radius)));
            }

            void Register() override
            {
                OnHit += SpellHitFn(spell_gruul_shatter_effect_SpellScript::CalculateDamage);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_gruul_shatter_effect_SpellScript();
        }
};

/**
 * @brief 注册格鲁尔Boss脚本
 *
 * 在服务器启动时调用，注册以下脚本：
 * - 格鲁尔Boss AI
 * - 粉碎法术脚本
 * - 粉碎效果法术脚本
 */
void AddSC_boss_gruul()
{
    new boss_gruul();
    new spell_gruul_shatter();
    new spell_gruul_shatter_effect();
}
