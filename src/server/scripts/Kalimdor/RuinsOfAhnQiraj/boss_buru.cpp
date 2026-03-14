/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file boss_buru.cpp
 * @brief 安其拉废墟BOSS布鲁（Buru the Gorger）的AI脚本
 *
 * 本模块实现了安其拉废墟第四个BOSS布鲁的战斗逻辑：
 * - 蛋阶段：BOSS追逐玩家，玩家需要打蛋对BOSS造成伤害
 * - 变形阶段：血量低于20%时BOSS蜕皮，进入第二阶段
 * - 荆棘护盾：对攻击者造成反射伤害
 * - 速度递增：随时间增加移动速度
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ruins_of_ahnqiraj.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

/**
 * @brief 表情文本枚举
 */
enum Emotes
{
    EMOTE_TARGET                = 0   ///< 锁定目标表情
};

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_CREEPING_PLAGUE       = 20512,  ///< 蠕行瘟疫：变形后的AOE疾病效果
    SPELL_DISMEMBER             = 96,     ///< 肢解：造成持续流血伤害
    SPELL_GATHERING_SPEED       = 1834,   ///< 加速：提高移动速度
    SPELL_FULL_SPEED            = 1557,   ///< 全速：最大移动速度
    SPELL_THORNS                = 25640,  ///< 荆棘：反射近战伤害
    SPELL_BURU_TRANSFORM        = 24721,  ///< 变形：BOSS蜕皮进入第二阶段
    SPELL_SUMMON_HATCHLING      = 1881,   ///< 召唤幼蜂：蛋被摧毁后召唤
    SPELL_EXPLODE               = 19593,  ///< 爆炸：蛋爆炸对附近造成伤害
    SPELL_EXPLODE_2             = 5255,   ///< 爆炸2：未知用途的附加效果
    SPELL_BURU_EGG_TRIGGER      = 26646   ///< 蛋触发器
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_DISMEMBER             = 1,  ///< 肢解事件
    EVENT_GATHERING_SPEED       = 2,  ///< 加速事件
    EVENT_FULL_SPEED            = 3,  ///< 全速事件
    EVENT_CREEPING_PLAGUE       = 4,  ///< 蠕行瘟疫事件
    EVENT_RESPAWN_EGG           = 5   ///< 重生蛋事件
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_EGG                   = 0,  ///< 蛋阶段：BOSS追逐玩家，利用蛋造成伤害
    PHASE_TRANSFORM             = 1   ///< 变形阶段：BOSS蜕皮后进入第二阶段
};

/**
 * @brief 动作ID枚举
 */
enum Actions
{
    ACTION_EXPLODE              = 0   ///< 爆炸动作：蛋爆炸对BOSS造成伤害
};

/**
 * @class boss_buru
 * @brief 布鲁BOSS脚本类
 *
 * 负责注册和管理布鲁BOSS的AI行为
 */
class boss_buru : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_buru() : CreatureScript("boss_buru") { }

        /**
         * @class boss_buruAI
         * @brief 布鲁BOSS的AI实现类
         *
         * 实现了布鲁的两阶段战斗逻辑：
         * 1. 蛋阶段：追逐随机目标，被打蛋时受到伤害，速度逐渐增加
         * 2. 变形阶段：血量低于20%时蜕皮，移除荆棘，施放蠕行瘟疫
         */
        struct boss_buruAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_buruAI(Creature* creature) : BossAI(creature, DATA_BURU)
            {
                _phase = 0;
            }

            /**
             * @brief 进入脱战模式
             * @param why 脱战原因
             *
             * 当BOSS脱离战斗时，重生所有被摧毁的蛋
             * @调用时机 BOSS脱战、重置
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                BossAI::EnterEvadeMode(why);

                // 重生所有蛋
                for (ObjectGuid eggGuid : Eggs)
                    if (Creature* egg = ObjectAccessor::GetCreature(*me, eggGuid))
                        egg->Respawn();

                Eggs.clear();
            }

            /**
             * @brief 进入战斗
             * @param who 攻击者
             *
             * 当BOSS进入战斗时，施放荆棘护盾并初始化技能事件
             * @调用时机 BOSS进入战斗
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(EMOTE_TARGET, who);
                DoCast(me, SPELL_THORNS);  // 施放荆棘护盾

                // 安排技能事件
                events.ScheduleEvent(EVENT_DISMEMBER, 5s);        // 肢解
                events.ScheduleEvent(EVENT_GATHERING_SPEED, 9s); // 加速
                events.ScheduleEvent(EVENT_FULL_SPEED, 1min);    // 全速

                _phase = PHASE_EGG;
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 当蛋爆炸时，如果BOSS在附近则受到大量伤害
             * @调用时机 蛋爆炸时
             */
            void DoAction(int32 action) override
            {
                if (action == ACTION_EXPLODE)
                    if (_phase == PHASE_EGG)
                        Unit::DealDamage(me, me, 45000);  // 蛋爆炸对BOSS造成45000伤害
            }

            /**
             * @brief 击杀单位回调
             * @param victim 被击杀的单位
             *
             * 当BOSS击杀玩家时，切换追逐新的目标
             * @调用时机 BOSS击杀玩家时
             */
            void KilledUnit(Unit* victim) override
            {
                if (victim->GetTypeId() == TYPEID_PLAYER)
                    ChaseNewVictim();
            }

            /**
             * @brief 切换追逐新目标
             *
             * 重置速度buff，选择新的随机目标追逐
             * @调用时机 击杀玩家后、蛋被摧毁后
             */
            void ChaseNewVictim()
            {
                // 只在蛋阶段执行
                if (_phase != PHASE_EGG)
                    return;

                // 移除速度buff并重新安排加速事件
                me->RemoveAurasDueToSpell(SPELL_FULL_SPEED);
                me->RemoveAurasDueToSpell(SPELL_GATHERING_SPEED);
                events.ScheduleEvent(EVENT_GATHERING_SPEED, 9s);
                events.ScheduleEvent(EVENT_FULL_SPEED, 1min);

                // 随机选择新目标
                if (Unit* victim = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                {
                    ResetThreatList();
                    AttackStart(victim);
                    Talk(EMOTE_TARGET, victim);
                }
            }

            /**
             * @brief 管理蛋的重生
             * @param EggGUID 蛋的GUID
             *
             * 当蛋被摧毁时，记录其GUID并安排重生时间
             * @调用时机 蛋死亡时
             */
            void ManageRespawn(ObjectGuid EggGUID)
            {
                ChaseNewVictim();              // 切换追逐目标
                Eggs.push_back(EggGUID);       // 记录蛋的GUID
                events.ScheduleEvent(EVENT_RESPAWN_EGG, 100s);  // 100秒后重生
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 每帧调用，处理BOSS的战斗逻辑
             * @调用时机 每帧更新
             * @性能注意事项 该函数每帧调用，需保持高效
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有目标则返回
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_DISMEMBER:
                            // 对当前目标施放肢解
                            DoCastVictim(SPELL_DISMEMBER);
                            events.ScheduleEvent(EVENT_DISMEMBER, 5s);
                            break;
                        case EVENT_GATHERING_SPEED:
                            // 施放加速，提高移动速度
                            DoCast(me, SPELL_GATHERING_SPEED);
                            events.ScheduleEvent(EVENT_GATHERING_SPEED, 9s);
                            break;
                        case EVENT_FULL_SPEED:
                            // 施放全速，达到最大移动速度
                            DoCast(me, SPELL_FULL_SPEED);
                            break;
                        case EVENT_CREEPING_PLAGUE:
                            // 变形阶段施放蠕行瘟疫
                            DoCast(me, SPELL_CREEPING_PLAGUE);
                            events.ScheduleEvent(EVENT_CREEPING_PLAGUE, 6s);
                            break;
                        case EVENT_RESPAWN_EGG:
                            // 重生蛋
                            if (Creature* egg = ObjectAccessor::GetCreature(*me, Eggs.front()))
                            {
                                egg->Respawn();
                                Eggs.pop_front();
                            }
                            break;
                        default:
                            break;
                    }
                }

                // 阶段转换：当血量低于20%时进入变形阶段
                if (me->GetHealthPct() < 20.0f && _phase == PHASE_EGG)
                {
                    DoCast(me, SPELL_BURU_TRANSFORM);   // 变形
                    DoCast(me, SPELL_FULL_SPEED, true); // 获得全速
                    me->RemoveAurasDueToSpell(SPELL_THORNS);  // 移除荆棘护盾
                    _phase = PHASE_TRANSFORM;
                }

                DoMeleeAttackIfReady();
            }
        private:
            GuidList Eggs;      ///< 已被摧毁的蛋GUID列表，用于重生管理
            uint8 _phase;       ///< 当前战斗阶段（蛋阶段/变形阶段）
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<boss_buruAI>(creature);
        }
};

/**
 * @class npc_buru_egg
 * @brief 布鲁的蛋脚本类
 *
 * 负责管理蛋的行为，包括被摧毁时爆炸和召唤幼蜂
 */
class npc_buru_egg : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_buru_egg() : CreatureScript("npc_buru_egg") { }

        /**
         * @class npc_buru_eggAI
         * @brief 布鲁的蛋AI实现类
         *
         * 实现蛋的行为逻辑：
         * - 不移动，固定位置
         * - 被攻击时触发BOSS进入战斗
         * - 死亡时爆炸并召唤幼蜂
         */
        struct npc_buru_eggAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            npc_buru_eggAI(Creature* creature) : ScriptedAI(creature)
            {
                _instance = me->GetInstanceScript();
                SetCombatMovement(false);  // 蛋不移动
            }

            /**
             * @brief 进入战斗
             * @param attacker 攻击者
             *
             * 当蛋被攻击时，如果BOSS不在战斗则触发BOSS进入战斗
             * @调用时机 蛋被攻击时
             */
            void JustEngagedWith(Unit* attacker) override
            {
                if (Creature* buru = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_BURU)))
                    if (!buru->IsInCombat())
                        buru->AI()->AttackStart(attacker);
            }

            /**
             * @brief 召唤生物回调
             * @param who 被召唤的生物
             *
             * 当幼蜂被召唤时，让它攻击随机目标
             * @调用时机 召唤生物时
             */
            void JustSummoned(Creature* who) override
            {
                if (who->GetEntry() == NPC_HATCHLING)
                    if (Creature* buru = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_BURU)))
                        if (Unit* target = buru->AI()->SelectTarget(SelectTargetMethod::Random))
                            who->AI()->AttackStart(target);
            }

            /**
             * @brief 死亡回调
             * @param killer 击杀者（未使用）
             *
             * 当蛋死亡时，爆炸并召唤幼蜂
             * @调用时机 蛋死亡时
             */
            void JustDied(Unit* /*killer*/) override
            {
                DoCastAOE(SPELL_EXPLODE, true);      // 施放爆炸AOE
                DoCastAOE(SPELL_EXPLODE_2, true);    // 施放附加效果（未知用途）
                DoCast(me, SPELL_SUMMON_HATCHLING, true);  // 召唤幼蜂

                // 通知BOSS蛋被摧毁
                if (Creature* buru = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_BURU)))
                    if (boss_buru::boss_buruAI* buruAI = dynamic_cast<boss_buru::boss_buruAI*>(buru->AI()))
                        buruAI->ManageRespawn(me->GetGUID());
            }
        private:
            InstanceScript* _instance;  ///< 副本脚本实例，用于查询BOSS GUID
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<npc_buru_eggAI>(creature);
        }
};

/**
 * @class spell_egg_explosion
 * @brief 蛋爆炸法术脚本类
 *
 * 负责处理蛋爆炸法术的特殊逻辑
 */
// 19593 - Egg Explosion
class spell_egg_explosion : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         */
        spell_egg_explosion() : SpellScriptLoader("spell_egg_explosion") { }

        /**
         * @class spell_egg_explosion_SpellScript
         * @brief 蛋爆炸法术脚本实现类
         *
         * 实现蛋爆炸的特殊效果：
         * - 如果BOSS在爆炸范围内，对BOSS造成伤害
         * - 对范围内的目标造成基于距离的伤害
         */
        class spell_egg_explosion_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_egg_explosion_SpellScript);

            /**
             * @brief 施法后处理
             *
             * 如果BOSS在爆炸范围内（5码），触发爆炸动作
             * @调用时机 法术施放完成后
             */
            void HandleAfterCast()
            {
                if (Creature* buru = GetCaster()->FindNearestCreature(NPC_BURU, 5.f))
                    buru->AI()->DoAction(ACTION_EXPLODE);
            }

            /**
             * @brief 处理命中目标
             * @param effIndex 效果索引（未使用）
             *
             * 对目标造成基于距离的伤害，距离越近伤害越高
             * 伤害公式：500 - 16 * 距离
             * @调用时机 法术命中目标时
             */
            void HandleDummyHitTarget(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    // 伤害随距离递减：近距离500伤害，远距离递减
                    Unit::DealDamage(GetCaster(), target, -16 * GetCaster()->GetDistance(target) + 500);
            }

            /**
             * @brief 注册法术事件
             *
             * 注册施法后和命中目标的处理函数
             */
            void Register() override
            {
                AfterCast += SpellCastFn(spell_egg_explosion_SpellScript::HandleAfterCast);
                OnEffectHitTarget += SpellEffectFn(spell_egg_explosion_SpellScript::HandleDummyHitTarget, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        /**
         * @brief 获取法术脚本实例
         * @return 法术脚本指针
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_egg_explosion_SpellScript();
        }
};

/**
 * @brief 注册脚本
 *
 * 将布鲁BOSS、蛋和蛋爆炸法术脚本注册到脚本系统
 */
void AddSC_boss_buru()
{
    new boss_buru();
    new npc_buru_egg();
    new spell_egg_explosion();
}
