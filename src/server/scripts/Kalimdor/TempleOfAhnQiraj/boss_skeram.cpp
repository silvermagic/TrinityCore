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
 * @file boss_skeram.cpp
 * @brief 斯克拉姆首领AI脚本
 *
 * 本模块实现了安其拉神殿第一个首领斯克拉姆的AI逻辑，包括：
 * - 奥术爆炸（AOE伤害）
 * - 大地冲击（打断施法）
 * - 真实实现（精神控制玩家）
 * - 闪烁（瞬移并清除仇恨）
 * - 分身机制（血量降低时分裂成多个分身）
 *
 * 特殊机制：
 * - 分身会继承本体的一部分血量
 * - 分身和本体会随机闪烁到不同位置
 * - 需要击杀真身才能完成战斗
 *
 * @author TrinityCore Team
 * @date 2024
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "temple_of_ahnqiraj.h"

/**
 * @brief 斯克拉姆喊话枚举
 */
enum Yells
{
    SAY_AGGRO                   = 0,  // 开战喊话
    SAY_SLAY                    = 1,  // 击杀玩家喊话
    SAY_SPLIT                   = 2,  // 分身喊话
    SAY_DEATH                   = 3   // 死亡喊话
};

/**
 * @brief 斯克拉姆使用的法术ID枚举
 */
enum Spells
{
    SPELL_ARCANE_EXPLOSION      = 26192,  // 奥术爆炸，AOE伤害
    SPELL_EARTH_SHOCK           = 26194,  // 大地冲击，打断施法
    SPELL_TRUE_FULFILLMENT      = 785,    // 真实实现，精神控制玩家
    SPELL_TRUE_FULFILLMENT_2    = 2313,   // 真实实现效果法术
    SPELL_INITIALIZE_IMAGE      = 3730,   // 初始化分身
    SPELL_SUMMON_IMAGES         = 747,    // 召唤分身
    SPELL_GENERIC_DISMOUNT      = 61286   // 通用下马法术
};

/**
 * @brief 斯克拉姆的事件类型枚举
 */
enum Events
{
    EVENT_ARCANE_EXPLOSION      = 1,  // 奥术爆炸事件
    EVENT_FULLFILMENT           = 2,  // 真实实现事件
    EVENT_BLINK                 = 3,  // 闪烁事件
    EVENT_EARTH_SHOCK           = 4   // 大地冲击事件
};

/**
 * @brief 闪烁法术ID数组
 * 定义了三个不同的闪烁法术，对应不同的位置
 */
uint32 const BlinkSpells[3] = { 4801, 8195, 20449 };

/**
 * @brief 斯克拉姆首领AI脚本类
 *
 * 实现斯克拉姆的分身和闪烁机制
 */
class boss_skeram : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_skeram() : CreatureScript("boss_skeram") { }

        /**
         * @brief 斯克拉姆AI结构体
         */
        struct boss_skeramAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_skeramAI(Creature* creature) : BossAI(creature, DATA_SKERAM)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _flag = 0;        // 用于追踪闪烁位置的标志
                _hpct = 75.0f;    // 下一次分身的血量阈值（75%、50%、25%）
            }

            /**
             * @brief 重置AI状态
             */
            void Reset() override
            {
                Initialize();
                me->SetVisible(true);  // 确保可见
            }

            /**
             * @brief 击杀单位回调
             * @param victim 被击杀的单位（未使用）
             */
            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_SLAY);  // 击杀喊话
            }

            /**
             * @brief 进入躲避模式回调
             * @param why 躲避原因
             *
             * 如果是分身，则直接消失
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                ScriptedAI::EnterEvadeMode(why);
                if (me->IsSummon())
                    me->DespawnOrUnsummon();  // 分身直接消失
            }

            /**
             * @brief 召唤生物回调
             * @param creature 被召唤的生物（分身）
             *
             * 处理分身召唤逻辑：
             * 1. 为本体和分身分配不同的闪烁位置
             * 2. 设置分身的血量（根据当前血量百分比）
             * 3. 让分身攻击随机目标
             *
             * 分身血量规则：
             * - 本体血量 < 25%: 分身血量 = 本体最大血量的50%
             * - 本体血量 < 50%: 分身血量 = 本体最大血量的20%
             * - 其他情况: 分身血量 = 本体最大血量的10%
             */
            void JustSummoned(Creature* creature) override
            {
                // Shift the boss and images (Get it? *Shift*?)
                // 为Boss和分身分配不同的闪烁位置（使用位标志追踪）
                uint8 rand = 0;
                if (_flag != 0)
                {
                    // 找到一个未被使用的闪烁位置
                    while (_flag & (1 << rand))
                        rand = urand(0, 2);
                    DoCast(me, BlinkSpells[rand]);  // 本体闪烁
                    _flag |= (1 << rand);
                    _flag |= (1 << 7);  // 标记本体已闪烁
                }

                // 为分身分配闪烁位置
                while (_flag & (1 << rand))
                    rand = urand(0, 2);
                creature->CastSpell(creature, BlinkSpells[rand]);  // 分身闪烁
                _flag |= (1 << rand);

                if (_flag & (1 << 7))
                    _flag = 0;  // 重置标志

                // 让分身攻击随机目标
                if (Unit* Target = SelectTarget(SelectTargetMethod::Random))
                    creature->AI()->AttackStart(Target);

                float ImageHealthPct;

                // 根据本体血量设置分身血量
                if (me->GetHealthPct() < 25.0f)
                    ImageHealthPct = 0.50f;  // 50%最大血量
                else if (me->GetHealthPct() < 50.0f)
                    ImageHealthPct = 0.20f;  // 20%最大血量
                else
                    ImageHealthPct = 0.10f;  // 10%最大血量

                creature->SetMaxHealth(me->GetMaxHealth() * ImageHealthPct);
                creature->SetHealth(creature->GetMaxHealth() * (me->GetHealthPct() / 100.0f));

                summons.Summon(creature);  // 添加到召唤列表
            }

            /**
             * @brief 死亡回调
             * @param killer 击杀者
             *
             * 只有本体死亡时才触发死亡喊话和正常死亡流程
             * 分身死亡时直接消失
             */
            void JustDied(Unit* killer) override
            {
                if (!me->IsSummon())  // 如果是本体
                {
                    Talk(SAY_DEATH);
                    BossAI::JustDied(killer);
                }
                else  // 如果是分身
                    me->DespawnOrUnsummon();
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             *
             * 初始化事件调度器，安排技能施放
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                events.Reset();

                events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 6s, 12s);   // 奥术爆炸
                events.ScheduleEvent(EVENT_FULLFILMENT, 15s);            // 真实实现
                events.ScheduleEvent(EVENT_BLINK, 30s, 45s);             // 闪烁
                events.ScheduleEvent(EVENT_EARTH_SHOCK, 2s);             // 大地冲击

                Talk(SAY_AGGRO);  // 开战喊话
            }

            /**
             * @brief 更新AI主循环
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 处理斯克拉姆的战斗逻辑：
             * - 奥术爆炸：AOE伤害
             * - 真实实现：精神控制随机玩家
             * - 闪烁：瞬移并清除仇恨
             * - 大地冲击：打断施法
             * - 分身：血量低于阈值时分裂
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_ARCANE_EXPLOSION:  // 奥术爆炸
                            DoCastAOE(SPELL_ARCANE_EXPLOSION, true);
                            events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 8s, 18s);
                            break;
                        case EVENT_FULLFILMENT:  // 真实实现
                            // 选择45码范围内的随机玩家
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 45.0f, true))
                                DoCast(target, SPELL_TRUE_FULFILLMENT);
                            events.ScheduleEvent(EVENT_FULLFILMENT, 20s, 30s);
                            break;
                        case EVENT_BLINK:  // 闪烁
                            DoCast(me, BlinkSpells[urand(0, 2)]);  // 随机选择闪烁位置
                            ResetThreatList();                     // 重置仇恨列表
                            me->SetVisible(true);
                            events.ScheduleEvent(EVENT_BLINK, 10s, 30s);
                            break;
                        case EVENT_EARTH_SHOCK:  // 大地冲击
                            DoCastVictim(SPELL_EARTH_SHOCK);
                            events.ScheduleEvent(EVENT_EARTH_SHOCK, 2s);
                            break;
                    }
                }

                // 检查是否需要分身（只有本体会触发）
                if (!me->IsSummon() && me->GetHealthPct() < _hpct)
                {
                    DoCastAOE(SPELL_SUMMON_IMAGES, true);  // 召唤分身
                    Talk(SAY_SPLIT);
                    _hpct -= 25.0f;        // 下一次分身的血量阈值降低25%
                    me->SetVisible(false); // 本体隐身
                    events.RescheduleEvent(EVENT_BLINK, 2s);  // 2秒后闪烁
                }

                // 如果在近战范围内，使用大地冲击打断施法
                if (me->IsWithinMeleeRange(me->GetVictim()))
                {
                    events.RescheduleEvent(EVENT_EARTH_SHOCK, 2s);
                    DoMeleeAttackIfReady();
                }
            }

        private:
            float _hpct;    // 下一次分身的血量阈值
            uint8 _flag;    // 闪烁位置标志位
        };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<boss_skeramAI>(creature);
    }
};

/**
 * @brief 奥术爆炸法术脚本
 *
 * 过滤法术目标，只对玩家和宠物生效
 */
// 26192 - Arcane Explosion
class spell_skeram_arcane_explosion : public SpellScriptLoader
{
    public:
        spell_skeram_arcane_explosion() : SpellScriptLoader("spell_skeram_arcane_explosion") { }

        class spell_skeram_arcane_explosion_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_skeram_arcane_explosion_SpellScript);

            /**
             * @brief 过滤目标
             * @param targets 目标列表
             *
             * 只保留玩家和宠物，移除其他单位
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if([](WorldObject* object) -> bool
                {
                    if (object->GetTypeId() == TYPEID_PLAYER)
                        return false;  // 保留玩家

                    if (Creature* creature = object->ToCreature())
                        return !creature->IsPet();  // 保留宠物

                    return true;  // 移除其他单位
                });
            }

            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_skeram_arcane_explosion_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_skeram_arcane_explosion_SpellScript();
        }
};

/**
 * @brief 真实实现法术脚本
 *
 * 精神控制玩家，强制下马并添加控制效果
 */
// 785 - True Fulfillment
class spell_skeram_true_fulfillment : public SpellScriptLoader
{
public:
    spell_skeram_true_fulfillment() : SpellScriptLoader("spell_skeram_true_fulfillment") { }

    class spell_skeram_true_fulfillment_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_skeram_true_fulfillment_SpellScript);

        /**
         * @brief 验证法术
         * @param spell 法术信息（未使用）
         * @return 是否验证通过
         */
        bool Validate(SpellInfo const* /*spell*/) override
        {
            return ValidateSpellInfo({ SPELL_TRUE_FULFILLMENT_2, SPELL_GENERIC_DISMOUNT });
        }

        /**
         * @brief 处理法术效果
         * @param effIndex 效果索引（未使用）
         *
         * 强制目标下马并施加控制效果
         */
        void HandleEffect(SpellEffIndex /*effIndex*/)
        {
            GetCaster()->CastSpell(GetHitUnit(), SPELL_GENERIC_DISMOUNT, true);      // 强制下马
            GetCaster()->CastSpell(GetHitUnit(), SPELL_TRUE_FULFILLMENT_2, true);    // 施加控制效果
        }

        void Register() override
        {
            OnEffectHitTarget += SpellEffectFn(spell_skeram_true_fulfillment_SpellScript::HandleEffect, EFFECT_0, SPELL_AURA_MOD_CHARM);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_skeram_true_fulfillment_SpellScript();
    }
};

/**
 * @brief 注册斯克拉姆脚本
 *
 * 创建并注册斯克拉姆首领脚本和相关法术脚本
 */
void AddSC_boss_skeram()
{
    new boss_skeram();
    new spell_skeram_arcane_explosion();
    new spell_skeram_true_fulfillment();
}
