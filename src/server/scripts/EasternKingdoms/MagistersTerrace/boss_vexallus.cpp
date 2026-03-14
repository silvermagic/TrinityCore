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
 * @file boss_vexallus.cpp
 * @brief 魔导师平台副本第二BOSS - 维萨鲁斯AI脚本
 *
 * 本模块实现了魔导师平台副本中第二个BOSS维萨鲁斯的战斗逻辑。
 * 维萨鲁斯是一个能量构造体BOSS，会在特定生命值百分比召唤纯能量球。
 *
 * 主要功能：
 * - BOSS的AI逻辑和技能施放
 * - 纯能量召唤物的AI实现
 * - 能量释放机制（生命值达到阈值时召唤纯能量）
 * - 过载技能（生命值低于10%时连续施放）
 *
 * 战斗特点：
 * - 每15%生命值损失会召唤纯能量球：
 *   - 普通难度：召唤1个纯能量球
 *   - 英雄难度：召唤2个纯能量球
 * - 召唤纯能量时会对全团施放能量释放效果
 * - 纯能量球会跟随随机玩家，被击杀时对击杀者施放能量反馈
 * - 生命值低于10%时进入过载状态，连续对目标施放过载
 *
 * @note 纯能量球击杀者会获得能量反馈debuff，增加受到的伤害
 */

#include "ScriptMgr.h"
#include "magisters_terrace.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"

/**
 * @brief 维萨鲁斯的喊话枚举
 *
 * 定义了维萨鲁斯在战斗中各个时机的喊话ID
 */
enum Yells
{
    SAY_AGGRO                       = 0,
    SAY_ENERGY                      = 1,
    SAY_OVERLOAD                    = 2,
    SAY_KILL                        = 3,
    EMOTE_DISCHARGE_ENERGY          = 4

    //is this text for real?
    //#define SAY_DEATH             "What...happen...ed."
};

/**
 * @brief 法术ID枚举
 *
 * 定义了维萨鲁斯及纯能量球使用的所有法术
 */
enum Spells
{
    SPELL_CHAIN_LIGHTNING           = 44318,
    SPELL_OVERLOAD                  = 44353,
    SPELL_ARCANE_SHOCK              = 44319,

    SPELL_SUMMON_PURE_ENERGY        = 44322, // mod scale -10
    H_SPELL_SUMMON_PURE_ENERGY1     = 46154, // mod scale -5
    H_SPELL_SUMMON_PURE_ENERGY2     = 46159  // mod scale -5（英雄难度，缩放-5）
};

/**
 * @brief 事件ID枚举
 *
 * 定义了战斗中各种事件的时间调度ID
 */
enum Events
{
    EVENT_ENERGY_BOLT               = 1,
    EVENT_ENERGY_FEEDBACK,
    EVENT_CHAIN_LIGHTNING,
    EVENT_OVERLOAD,
    EVENT_ARCANE_SHOCK
};

/**
 * @brief 杂项常量枚举
 *
 * 定义了生命值阈值相关的常量
 */
enum Misc
{
    INTERVAL_MODIFIER               = 15,  ///< 每次召唤纯能量的生命值百分比间隔
    INTERVAL_SWITCH                 = 6    ///< 切换到过载阶段的阈值（10%生命值，即6 * 15% = 90%已损失）
};

/**
 * @brief 维萨鲁斯BOSS脚本
 *
 * 实现了维萨鲁斯的主要AI逻辑，包括：
 * - 闪电链和奥术冲击技能循环
 * - 生命值达到特定百分比时召唤纯能量球
 * - 过载阶段（生命值低于10%）
 * - 召唤物管理
 */
class boss_vexallus : public CreatureScript
{
    public:
        boss_vexallus() : CreatureScript("boss_vexallus") { }

        /**
         * @brief 维萨鲁斯AI结构体
         *
         * 继承自BossAI，实现维萨鲁斯的所有战斗逻辑
         */
        struct boss_vexallusAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 初始化AI，设置间隔生命值计数器和激怒标志
             */
            boss_vexallusAI(Creature* creature) : BossAI(creature, DATA_VEXALLUS)
            {
                _intervalHealthAmount = 1;
                _enraged = false;
            }

            /**
             * @brief 重置BOSS状态
             *
             * 调用基类重置函数，重置间隔生命值计数器和激怒标志
             */
            void Reset() override
            {
                _Reset();
                _intervalHealthAmount = 1;
                _enraged = false;
            }

            /**
             * @brief 击杀单位时调用
             * @param victim 被击杀的单位（未使用）
             *
             * 喊出击杀对话
             */
            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_KILL);
            }

            /**
             * @brief 进入战斗时调用
             * @param who 进入战斗的目标
             *
             * 喊出战斗对话，开始闪电链和奥术冲击的技能循环
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_AGGRO);
                BossAI::JustEngagedWith(who);

                events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 8s);
                events.ScheduleEvent(EVENT_ARCANE_SHOCK, 5s);
            }

            /**
             * @brief 召唤生物时调用
             * @param summoned 被召唤的生物
             *
             * 让纯能量球跟随随机目标，并将召唤物添加到管理列表
             */
            void JustSummoned(Creature* summoned) override
            {
                if (Unit* temp = SelectTarget(SelectTargetMethod::Random, 0))
                    summoned->GetMotionMaster()->MoveFollow(temp, 0, 0);

                summons.Summon(summoned);
            }

            /**
             * @brief 受到伤害时调用
             * @param who 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型
             * @param spellInfo 法术信息
             *
             * 处理生命值阈值检查：
             * - 每15%生命值损失（85%, 70%, 55%, 40%, 25%）召唤纯能量球
             * - 生命值低于10%时进入过载阶段，停止常规技能，连续施放过载
             */
            void DamageTaken(Unit* /*who*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (_enraged)
                    return;

                // 85%, 70%, 55%, 40%, 25%
                if (!HealthAbovePct(100 - INTERVAL_MODIFIER * _intervalHealthAmount))
                {
                    // increase amount, unless we're at 10%, then we switch and return
                    if (_intervalHealthAmount == INTERVAL_SWITCH)
                    {
                        _enraged = true;
                        events.Reset();
                        events.ScheduleEvent(EVENT_OVERLOAD, 1200ms);
                        return;
                    }
                    else
                        ++_intervalHealthAmount;

                    Talk(SAY_ENERGY);
                    Talk(EMOTE_DISCHARGE_ENERGY);

                    if (IsHeroic())
                    {
                        DoCast(me, H_SPELL_SUMMON_PURE_ENERGY1);
                        DoCast(me, H_SPELL_SUMMON_PURE_ENERGY2);
                    }
                    else
                        DoCast(me, SPELL_SUMMON_PURE_ENERGY);
                }
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 主要战斗逻辑循环：
             * - 检查是否有目标
             * - 更新事件计时器
             * - 检查是否正在施法
             * - 处理各种事件的执行：
             *   - 闪电链：对随机目标施放
             *   - 奥术冲击：对近战范围内随机目标施放
             *   - 过载：对当前目标连续施放
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
                        case EVENT_CHAIN_LIGHTNING:
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                                DoCast(target, SPELL_CHAIN_LIGHTNING);
                            events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 8s);
                            break;
                        case EVENT_ARCANE_SHOCK:
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 20.0f, true))
                                DoCast(target, SPELL_ARCANE_SHOCK);
                            events.ScheduleEvent(EVENT_ARCANE_SHOCK, 8s);
                            break;
                        case EVENT_OVERLOAD:
                            DoCastVictim(SPELL_OVERLOAD);
                            events.ScheduleEvent(EVENT_OVERLOAD, 2s);
                            break;
                        default:
                            break;
                    }

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();
            }

        private:
            uint32 _intervalHealthAmount;  ///< 生命值间隔计数器，用于追踪召唤纯能量的阈值
            bool _enraged;                 ///< 是否已进入过载阶段
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetMagistersTerraceAI<boss_vexallusAI>(creature);
        };
};

/**
 * @brief 纯能量球法术枚举
 *
 * 定义了纯能量球使用的法术ID
 */
enum NpcPureEnergy
{
    SPELL_ENERGY_BOLT               = 46156,  ///< 能量冲击法术
    SPELL_ENERGY_FEEDBACK           = 44335,  ///< 能量反馈法术（击杀者获得的debuff）
    SPELL_PURE_ENERGY_PASSIVE       = 44326   ///< 纯能量被动法术
};

/**
 * @brief 纯能量球NPC脚本
 *
 * 实现了纯能量球的AI逻辑：
 * - 跟随随机目标
 * - 被击杀时对击杀者施放能量反馈
 * - 移除被动光环
 */
class npc_pure_energy : public CreatureScript
{
    public:
        npc_pure_energy() : CreatureScript("npc_pure_energy") { }

        struct npc_pure_energyAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 设置纯能量球的显示模型
             */
            npc_pure_energyAI(Creature* creature) : ScriptedAI(creature)
            {
                me->SetDisplayId(me->GetCreatureTemplate()->Modelid2);
            }

            /**
             * @brief 死亡时调用
             * @param killer 击杀者
             *
             * 对击杀者施放能量反馈debuff，移除被动光环
             */
            void JustDied(Unit* killer) override
            {
                if (killer)
                    killer->CastSpell(killer, SPELL_ENERGY_FEEDBACK, true);
                me->RemoveAurasDueToSpell(SPELL_PURE_ENERGY_PASSIVE);
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetMagistersTerraceAI<npc_pure_energyAI>(creature);
        };
};

void AddSC_boss_vexallus()
{
    new boss_vexallus();
    new npc_pure_energy();
}
