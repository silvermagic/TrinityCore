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
 * @file boss_selin_fireheart.cpp
 * @brief 魔导师平台副本第一BOSS - 塞林·火心AI脚本
 *
 * 本模块实现了魔导师平台副本中第一个BOSS塞林·火心的战斗逻辑。
 * 塞林·火心是一个依赖法力值的BOSS，会从房间中的邪能水晶中吸取能量。
 *
 * 主要功能：
 * - BOSS的AI逻辑和技能施放
 * - 邪能水晶的AI实现
 * - 法力吸取和能量充能机制
 * - 邪能爆炸技能循环
 *
 * 战斗特点：
 * - BOSS的法力值低于10%时会寻找水晶吸取能量
 * - 普通难度：20-25秒后吸取水晶
 * - 英雄难度：10-15秒后吸取水晶，额外施放吸取法力技能
 * - 吸取水晶时会移到水晶旁边，进入充能阶段
 * - 充能期间无法移动，但可以施放技能
 * - 水晶被摧毁后BOSS恢复战斗状态
 * - 死亡时摧毁所有剩余水晶
 *
 * @note 水晶应该作为DB生物召唤组实现，当前实现可能导致大量消失/重生错误
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "magisters_terrace.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

/**
 * @brief 塞林·火心的对话枚举
 *
 * 定义了塞林在战斗中各个时机的对话ID
 */
enum Says
{
    SAY_AGGRO                       = 0,
    SAY_ENERGY                      = 1,
    SAY_EMPOWERED                   = 2,
    SAY_KILL                        = 3,
    SAY_DEATH                       = 4,
    EMOTE_CRYSTAL                   = 5
};

/**
 * @brief 塞林·火心及相关水晶的法术ID枚举
 *
 * 包含水晶效果法术和塞林的战斗法术
 */
enum Spells
{
    // Crystal effect spells
    SPELL_FEL_CRYSTAL_DUMMY         = 44329,
    SPELL_MANA_RAGE                 = 44320,               // This spell triggers 44321, which changes scale and regens mana Requires an entry in spell_script_target

    // Selin's spells
    SPELL_DRAIN_LIFE                = 44294,
    SPELL_FEL_EXPLOSION             = 44314,

    SPELL_DRAIN_MANA                = 46153               // Heroic only（英雄难度专属）
};

/**
 * @brief 战斗阶段枚举
 *
 * 定义了塞林战斗的两个主要阶段
 */
enum Phases
{
    PHASE_NORMAL                    = 1,
    PHASE_DRAIN                     = 2
};

/**
 * @brief 事件ID枚举
 *
 * 定义了战斗中各种事件的时间调度ID
 */
enum Events
{
    EVENT_FEL_EXPLOSION             = 1,
    EVENT_DRAIN_CRYSTAL,
    EVENT_DRAIN_MANA,
    EVENT_DRAIN_LIFE,
    EVENT_EMPOWER
};

/**
 * @brief 杂项常量枚举
 *
 * 定义自定义动作ID
 */
enum Misc
{
    ACTION_SWITCH_PHASE             = 1  ///< 切换阶段动作ID
};

/**
 * @brief 塞林·火心BOSS脚本
 *
 * 实现了塞林的主要AI逻辑，包括：
 * - 邪能爆炸技能循环
 * - 法力值低于10%时寻找水晶吸取能量
 * - 吸取生命和法力（英雄难度）
 * - 水晶充能阶段管理
 * - 死亡时摧毁所有剩余水晶
 *
 * @todo 水晶应该作为DB生物召唤组实现，当前在`creature`表中的实现会导致大量消失/重生错误
 */
// @todo crystals should really be a DB creature summon group, having them in `creature` like this will cause tons of despawn/respawn bugs
class boss_selin_fireheart : public CreatureScript
{
    public:
        boss_selin_fireheart() : CreatureScript("boss_selin_fireheart") { }

        /**
         * @brief 塞林·火心AI结构体
         *
         * 继承自BossAI，实现塞林的所有战斗逻辑
         */
        struct boss_selin_fireheartAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             *
             * 初始化AI，设置事件调度标志为false
             */
            boss_selin_fireheartAI(Creature* creature) : BossAI(creature, DATA_SELIN_FIREHEART), _scheduledEvents(false) { }

            /**
             * @brief 重置BOSS状态
             *
             * 重生所有水晶，调用基类重置函数，清空水晶GUID，重置事件调度标志
             */
            void Reset() override
            {
                std::list<Creature*> crystals;
                me->GetCreatureListWithEntryInGrid(crystals, NPC_FEL_CRYSTAL, 250.0f);

                for (Creature* creature : crystals)
                    creature->Respawn(true);

                _Reset();
                CrystalGUID.Clear();
                _scheduledEvents = false;
            }

            /**
             * @brief 执行动作时调用
             * @param action 动作ID
             *
             * 处理水晶被摧毁时的阶段切换：
             * - 切换回正常战斗阶段
             * - 恢复攻击目标
             * - 重新开始邪能爆炸循环
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_SWITCH_PHASE:
                        events.SetPhase(PHASE_NORMAL);
                        events.ScheduleEvent(EVENT_FEL_EXPLOSION, 2s, 0, PHASE_NORMAL);
                        AttackStart(me->GetVictim());
                        me->GetMotionMaster()->MoveChase(me->GetVictim());
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 选择最近的水晶
             *
             * 寻找250码范围内最近的邪能水晶：
             * - 喊出对话和表情
             * - 对水晶施放虚拟法术
             * - 存储水晶GUID
             * - 移动到水晶旁边
             * - 切换到充能阶段
             */
            void SelectNearestCrystal()
            {
                if (Creature* crystal = me->FindNearestCreature(NPC_FEL_CRYSTAL, 250.0f))
                {
                    Talk(SAY_ENERGY);
                    Talk(EMOTE_CRYSTAL);

                    DoCast(crystal, SPELL_FEL_CRYSTAL_DUMMY);
                    CrystalGUID = crystal->GetGUID();

                    float x, y, z;
                    crystal->GetClosePoint(x, y, z, me->GetCombatReach(), CONTACT_DISTANCE);

                    events.SetPhase(PHASE_DRAIN);
                    me->SetWalk(false);
                    me->GetMotionMaster()->MovePoint(1, x, y, z);
                }
            }

            /**
             * @brief 摧毁所有剩余水晶
             *
             * 在死亡时调用，杀死所有剩余的邪能水晶
             */
            void ShatterRemainingCrystals()
            {
                std::list<Creature*> crystals;
                me->GetCreatureListWithEntryInGrid(crystals, NPC_FEL_CRYSTAL, 250.0f);

                for (Creature* crystal : crystals)
                    crystal->KillSelf();
            }

            /**
             * @brief 进入战斗时调用
             * @param who 进入战斗的目标
             *
             * 喊出战斗对话，设置正常战斗阶段，开始邪能爆炸循环
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_AGGRO);
                BossAI::JustEngagedWith(who);

                events.SetPhase(PHASE_NORMAL);
                events.ScheduleEvent(EVENT_FEL_EXPLOSION, 2100ms, 0, PHASE_NORMAL);
             }

            /**
             * @brief 击杀单位时调用
             * @param victim 被击杀的单位
             *
             * 当击杀玩家时喊出击杀对话
             */
            void KilledUnit(Unit* victim) override
            {
                if (victim->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_KILL);
            }

            void MovementInform(uint32 type, uint32 id) override
            {
                if (type == POINT_MOTION_TYPE && id == 1)
                {
                    Unit* CrystalChosen = ObjectAccessor::GetUnit(*me, CrystalGUID);
                    if (CrystalChosen && CrystalChosen->IsAlive())
                    {
                        CrystalChosen->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        CrystalChosen->CastSpell(me, SPELL_MANA_RAGE, true);
                        events.ScheduleEvent(EVENT_EMPOWER, 10s, PHASE_DRAIN);
                    }
                }
            }

            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEATH);
                _JustDied();

                ShatterRemainingCrystals();
            }

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
                        case EVENT_FEL_EXPLOSION:
                            DoCastAOE(SPELL_FEL_EXPLOSION);
                            events.ScheduleEvent(EVENT_FEL_EXPLOSION, 2s, 0, PHASE_NORMAL);
                            break;
                        case EVENT_DRAIN_CRYSTAL:
                            SelectNearestCrystal();
                            _scheduledEvents = false;
                            break;
                        case EVENT_DRAIN_MANA:
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 45.0f, true))
                                DoCast(target, SPELL_DRAIN_MANA);
                            events.ScheduleEvent(EVENT_DRAIN_MANA, 10s, 0, PHASE_NORMAL);
                            break;
                        case EVENT_DRAIN_LIFE:
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 20.0f, true))
                                DoCast(target, SPELL_DRAIN_LIFE);
                            events.ScheduleEvent(EVENT_DRAIN_LIFE, 10s, 0, PHASE_NORMAL);
                            break;
                        case EVENT_EMPOWER:
                        {
                            Talk(SAY_EMPOWERED);

                            Creature* CrystalChosen = ObjectAccessor::GetCreature(*me, CrystalGUID);
                            if (CrystalChosen && CrystalChosen->IsAlive())
                                CrystalChosen->KillSelf();

                            CrystalGUID.Clear();

                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MoveChase(me->GetVictim());
                            break;
                        }
                        default:
                            break;
                    }

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                if (me->GetPowerPct(POWER_MANA) < 10.f)
                {
                    if (events.IsInPhase(PHASE_NORMAL) && !_scheduledEvents)
                    {
                        _scheduledEvents = true;
                        Milliseconds timer = randtime(3s, 7s);
                        events.ScheduleEvent(EVENT_DRAIN_LIFE, timer, 0, PHASE_NORMAL);

                        if (IsHeroic())
                        {
                            events.ScheduleEvent(EVENT_DRAIN_CRYSTAL, 10s, 15s, 0, PHASE_NORMAL);
                            events.ScheduleEvent(EVENT_DRAIN_MANA, timer + 5s, 0, PHASE_NORMAL);
                        }
                        else
                            events.ScheduleEvent(EVENT_DRAIN_CRYSTAL, 20s, 25s, 0, PHASE_NORMAL);
                    }
                }

                DoMeleeAttackIfReady();
            }

        private:
            ObjectGuid CrystalGUID;
            bool _scheduledEvents;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetMagistersTerraceAI<boss_selin_fireheartAI>(creature);
        };
};

/**
 * @brief 邪能水晶NPC脚本
 *
 * 实现了邪能水晶的AI逻辑：
 * - 水晶可以被塞林·火心吸取能量
 * - 水晶被摧毁时会通知塞林·火心切换回正常战斗阶段
 */
class npc_fel_crystal : public CreatureScript
{
    public:
        npc_fel_crystal() : CreatureScript("npc_fel_crystal") { }

        /**
         * @brief 邪能水晶AI结构体
         *
         * 继承自ScriptedAI，实现水晶的基本AI逻辑
         */
        struct npc_fel_crystalAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_fel_crystalAI(Creature* creature) : ScriptedAI(creature) { }

            /**
             * @brief 死亡时调用
             * @param killer 击杀者（未使用）
             *
             * 通知塞林·火心切换回正常战斗阶段
             */
            void JustDied(Unit* /*killer*/) override
            {
                if (InstanceScript* instance = me->GetInstanceScript())
                {
                    Creature* selin = instance->GetCreature(DATA_SELIN_FIREHEART);
                    if (selin && selin->IsAlive())
                        selin->AI()->DoAction(ACTION_SWITCH_PHASE);
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetMagistersTerraceAI<npc_fel_crystalAI>(creature);
        };
};

/**
 * @brief 注册塞林·火心相关脚本
 *
 * 注册以下脚本：
 * - boss_selin_fireheart：塞林·火心BOSS AI
 * - npc_fel_crystal：邪能水晶AI
 */
void AddSC_boss_selin_fireheart()
{
    new boss_selin_fireheart();
    new npc_fel_crystal();
}
