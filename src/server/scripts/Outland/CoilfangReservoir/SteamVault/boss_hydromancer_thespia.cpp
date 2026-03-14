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
 * @file boss_hydromancer_thespia.cpp
 * @brief 海度斯曼·瑟斯皮亚BOSS战AI脚本
 *
 * 本文件实现了蒸汽地窖副本中第一个BOSS海度斯曼·瑟斯皮亚的战斗AI。
 *
 * BOSS技能：
 * - 闪电云：在目标位置生成闪电云，造成自然伤害
 * - 肺部爆裂：对随机目标施放，造成伤害
 * - 包裹之风：对随机目标施放，眩晕目标
 * - 英雄模式下会施放两次闪电云和包裹之风
 *
 * 战斗机制：
 * - BOSS会召唤盘牙水元素协助战斗（由副本脚本控制）
 * - 在英雄模式下技能施放更频繁
 *
 * @see steam_vault.h 关联的副本定义头文件
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "steam_vault.h"

/**
 * @brief BOSS对话文本枚举
 */
enum Yells
{
    SAY_SUMMON                  = 0,  // 召唤对话
    SAY_AGGRO                   = 1,  // 开战对话
    SAY_SLAY                    = 2,  // 击杀玩家对话
    SAY_DEAD                    = 3,  // 死亡对话
};

/**
 * @brief BOSS技能枚举
 */
enum Spells
{
    SPELL_LIGHTNING_CLOUD       = 25033,  // 闪电云：在目标位置生成闪电云，造成自然伤害
    SPELL_LUNG_BURST            = 31481,  // 肺部爆裂：对目标造成伤害
    SPELL_ENVELOPING_WINDS      = 31718   // 包裹之风：眩晕目标
};

/**
 * @brief 事件枚举
 */
enum Events
{
    EVENT_LIGHTNING_CLOUD       = 1,  // 闪电云事件
    EVENT_LUNG_BURST,                 // 肺部爆裂事件
    EVENT_ENVELOPING_WINDS            // 包裹之风事件
};

class boss_hydromancer_thespia : public CreatureScript
{
    public:
        boss_hydromancer_thespia() : CreatureScript("boss_hydromancer_thespia") { }

        struct boss_thespiaAI : public BossAI
        {
            boss_thespiaAI(Creature* creature) : BossAI(creature, DATA_HYDROMANCER_THESPIA) { }

            void Reset() override
            {
                _Reset();
            }

            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEAD);
                _JustDied();
            }

            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_AGGRO);
                BossAI::JustEngagedWith(who);

                events.ScheduleEvent(EVENT_LIGHTNING_CLOUD, 15s);
                events.ScheduleEvent(EVENT_LUNG_BURST, 7s);
                events.ScheduleEvent(EVENT_ENVELOPING_WINDS, 9s);
            }

            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_LIGHTNING_CLOUD:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, true))
                            DoCast(target, SPELL_LIGHTNING_CLOUD);
                        // cast twice in Heroic mode
                        if (IsHeroic())
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 30.0f, true))
                                DoCast(target, SPELL_LIGHTNING_CLOUD);

                        events.ScheduleEvent(EVENT_LIGHTNING_CLOUD, 15s, 25s);
                        break;
                    case EVENT_LUNG_BURST:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 40.0f, true))
                            DoCast(target, SPELL_LUNG_BURST);
                        events.ScheduleEvent(EVENT_LUNG_BURST, 7s, 12s);
                        break;
                    case EVENT_ENVELOPING_WINDS:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 35.0f, true))
                            DoCast(target, SPELL_ENVELOPING_WINDS);
                        // cast twice in Heroic mode
                        if (IsHeroic())
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 35.0f, true))
                                DoCast(target, SPELL_ENVELOPING_WINDS);

                        events.ScheduleEvent(EVENT_ENVELOPING_WINDS, 10s, 15s);
                        break;
                    default:
                        break;
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetSteamVaultAI<boss_thespiaAI>(creature);
        }
};

enum CoilfangWaterElemental
{
    EVENT_WATER_BOLT_VOLLEY     = 1,
    SPELL_WATER_BOLT_VOLLEY     = 34449
};

class npc_coilfang_waterelemental : public CreatureScript
{
    public:
        npc_coilfang_waterelemental() : CreatureScript("npc_coilfang_waterelemental") { }

        struct npc_coilfang_waterelementalAI : public ScriptedAI
        {
            npc_coilfang_waterelementalAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override
            {
                _events.Reset();
            }

            void JustEngagedWith(Unit* /*who*/) override
            {
                _events.ScheduleEvent(EVENT_WATER_BOLT_VOLLEY, 3s, 6s);
            }

            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                _events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_WATER_BOLT_VOLLEY:
                            DoCast(me, SPELL_WATER_BOLT_VOLLEY);
                            _events.ScheduleEvent(EVENT_WATER_BOLT_VOLLEY, 7s, 12s);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

        private:
            EventMap _events;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetSteamVaultAI<npc_coilfang_waterelementalAI>(creature);
        }
};

void AddSC_boss_hydromancer_thespia()
{
    new boss_hydromancer_thespia();
    new npc_coilfang_waterelemental();
}
