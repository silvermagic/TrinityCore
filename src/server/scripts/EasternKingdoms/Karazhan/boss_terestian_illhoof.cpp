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
 * @file boss_terestian_illhoof.cpp
 * @brief 卡拉赞副本 - 特雷斯坦·邪蹄BOSS脚本模块
 *
 * 本模块实现了特雷斯坦·邪蹄BOSS及其爪牙的战斗逻辑，包括:
 * - 特雷斯坦·邪蹄BOSS AI
 * - 基尔雷克(小鬼)AI
 * - 恶魔锁链AI
 * - 邪恶传送门AI
 * - 邪恶小鬼AI
 *
 * 战斗机制:
 * - 特雷斯坦会定期召唤邪恶传送门，传送门持续生成邪恶小鬼
 * - 基尔雷克是特雷斯坦的宠物，死亡后会给特雷斯坦施加破碎契约Debuff
 * - 特雷斯坦会使用献祭技能将玩家传送到恶魔锁链中，需要击杀锁链解救
 * - 10分钟狂暴时间限制
 */

/* ScriptData
SDName: Boss_Terestian_Illhoof
SD%Complete: 100
SDComment:
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "karazhan.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

/**
 * @brief 特雷斯坦台词枚举
 *
 * 定义特雷斯坦在战斗中各个阶段说的话
 */
enum TerestianSays
{
    SAY_SLAY                  = 0,  ///< 击杀玩家台词
    SAY_DEATH                 = 1,  ///< 死亡台词
    SAY_AGGRO                 = 2,  ///< 进入战斗台词
    SAY_SACRIFICE             = 3,  ///< 施放献祭技能台词
    SAY_SUMMON_PORTAL         = 4   ///< 召唤传送门台词
};

/**
 * @brief 特雷斯坦及相关NPC技能ID枚举
 *
 * 定义特雷斯坦及其爪牙使用的所有法术技能ID
 */
enum TerestianSpells
{
    // 特雷斯坦技能
    SPELL_SHADOW_BOLT         = 30055,  ///< 暗影箭：对目标造成暗影伤害
    SPELL_SUMMON_IMP          = 30066,  ///< 召唤小鬼：召唤基尔雷克
    SPELL_FIENDISH_PORTAL_1   = 30171,  ///< 邪恶传送门1：左侧传送门
    SPELL_FIENDISH_PORTAL_2   = 30179,  ///< 邪恶传送门2：右侧传送门
    SPELL_BERSERK             = 32965,  ///< 狂暴：10分钟后施放
    SPELL_SUMMON_FIENDISH_IMP = 30184,  ///< 召唤邪恶小鬼：由传送门施放
    SPELL_BROKEN_PACT         = 30065,  ///< 破碎契约：基尔雷克死亡后对特雷斯坦施放

    // 基尔雷克技能
    SPELL_AMPLIFY_FLAMES      = 30053,  ///< 增强火焰：增加目标受到的火焰伤害
    SPELL_FIREBOLT            = 30050,  ///< 火焰箭：邪恶小鬼使用的火焰攻击

    // 恶魔锁链技能
    SPELL_SUMMON_DEMONCHAINS  = 30120,  ///< 召唤恶魔锁链：献祭技能触发
    SPELL_DEMON_CHAINS        = 30206,  ///< 恶魔锁链：束缚被献祭的目标
    SPELL_SACRIFICE           = 30115   ///< 献祭：将目标困在锁链中
};

/**
 * @brief 特雷斯坦战斗杂项枚举
 *
 * 定义NPC ID和动作ID
 */
enum TerestianMisc
{
    NPC_FIENDISH_PORTAL       = 17265,  ///< 邪恶传送门NPC ID
    ACTION_DESPAWN_IMPS       = 1       ///< 消失小鬼动作ID
};

/**
 * @brief 特雷斯坦事件枚举
 *
 * 定义特雷斯坦战斗中的各种事件ID，用于事件调度器
 */
enum TerestianEvents
{
    EVENT_SACRIFICE = 1,       ///< 献祭事件
    EVENT_SHADOWBOLT,          ///< 暗影箭事件
    EVENT_SUMMON_PORTAL_1,     ///< 召唤传送门1事件
    EVENT_SUMMON_PORTAL_2,     ///< 召唤传送门2事件
    EVENT_SUMMON_KILREK,       ///< 召唤基尔雷克事件
    EVENT_ENRAGE               ///< 狂暴事件
};

/**
 * @class boss_terestian_illhoof
 * @brief 特雷斯坦·邪蹄BOSS脚本类
 *
 * 继承自CreatureScript，用于注册特雷斯坦BOSS的AI脚本
 */
class boss_terestian_illhoof : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册特雷斯坦脚本名称
     */
    boss_terestian_illhoof() : CreatureScript("boss_terestian_illhoof") { }

    /**
     * @struct boss_terestianAI
     * @brief 特雷斯坦BOSS的AI实现
     *
     * 继承自BossAI，实现特雷斯坦的战斗逻辑:
     * - 施放暗影箭攻击仇恨最高的目标
     * - 周期性召唤基尔雷克和邪恶传送门
     * - 使用献祭技能困住随机玩家
     * - 10分钟狂暴机制
     */
    struct boss_terestianAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化特雷斯坦AI，设置副本数据ID
         */
        boss_terestianAI(Creature* creature) : BossAI(creature, DATA_TERESTIAN) { }

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能:
         * - 消失所有传送门召唤的小鬼
         * - 重置事件调度器
         * - 重新调度所有技能事件
         */
        void Reset() override
        {
            // 消失所有传送门召唤的小鬼
            EntryCheckPredicate pred(NPC_FIENDISH_PORTAL);
            summons.DoAction(ACTION_DESPAWN_IMPS, pred);
            _Reset();

            // 调度技能事件
            events.ScheduleEvent(EVENT_SHADOWBOLT, 1s);
            events.ScheduleEvent(EVENT_SUMMON_KILREK, 3s);
            events.ScheduleEvent(EVENT_SACRIFICE, 30s);
            events.ScheduleEvent(EVENT_SUMMON_PORTAL_1, Seconds(10));
            events.ScheduleEvent(EVENT_SUMMON_PORTAL_2, Seconds(11));
            events.ScheduleEvent(EVENT_ENRAGE, 10min);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         *
         * 调用时机: 特雷斯坦进入战斗时
         * 功能: 调用父类方法并播放战斗开始台词
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);
            Talk(SAY_AGGRO);
        }

        /**
         * @brief 法术命中回调
         * @param caster 施法者(未使用)
         * @param spellInfo 法术信息
         *
         * 调用时机: 法术命中特雷斯坦时
         * 功能: 如果是破碎契约法术，32秒后重新召唤基尔雷克
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if (spellInfo->Id == SPELL_BROKEN_PACT)
                events.ScheduleEvent(EVENT_SUMMON_KILREK, 32s);
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位
         *
         * 调用时机: 特雷斯坦杀死一个单位时
         * 功能: 如果击杀的是玩家，播放击杀台词
         */
        void KilledUnit(Unit* victim) override
        {
            if (victim->GetTypeId() == TYPEID_PLAYER)
                Talk(SAY_SLAY);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 特雷斯坦死亡时
         * 功能: 播放死亡台词，消失所有小鬼，触发死亡事件
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            // 消失所有传送门召唤的小鬼
            EntryCheckPredicate pred(NPC_FIENDISH_PORTAL);
            summons.DoAction(ACTION_DESPAWN_IMPS, pred);
            _JustDied();
        }

        /**
         * @brief 执行事件回调
         * @param eventId 事件ID
         *
         * 调用时机: 事件调度器触发事件时
         * 功能: 根据事件ID执行相应技能
         * - EVENT_SACRIFICE: 对随机目标施放献祭并召唤恶魔锁链
         * - EVENT_SHADOWBOLT: 对仇恨最高的目标施放暗影箭
         * - EVENT_SUMMON_KILREK: 移除破碎契约并召唤基尔雷克
         * - EVENT_SUMMON_PORTAL_1/2: 召唤邪恶传送门
         * - EVENT_ENRAGE: 施放狂暴法术
         */
        void ExecuteEvent(uint32 eventId) override
        {
            switch (eventId)
            {
                case EVENT_SACRIFICE:
                    // 对随机目标施放献祭，并召唤恶魔锁链困住目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                    {
                        DoCast(target, SPELL_SACRIFICE, true);
                        target->CastSpell(target, SPELL_SUMMON_DEMONCHAINS, true);
                        Talk(SAY_SACRIFICE);
                    }
                    events.Repeat(Seconds(42));
                    break;
                case EVENT_SHADOWBOLT:
                    // 对仇恨最高的目标施放暗影箭
                    if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0))
                        DoCast(target, SPELL_SHADOW_BOLT);
                    events.Repeat(Seconds(4), Seconds(10));
                    break;
                case EVENT_SUMMON_KILREK:
                    // 移除破碎契约Debuff，重新召唤基尔雷克
                    me->RemoveAurasDueToSpell(SPELL_BROKEN_PACT);
                    DoCastAOE(SPELL_SUMMON_IMP, true);
                    break;
                case EVENT_SUMMON_PORTAL_1:
                    // 召唤第一个邪恶传送门
                    Talk(SAY_SUMMON_PORTAL);
                    DoCastAOE(SPELL_FIENDISH_PORTAL_1);
                    break;
                case EVENT_SUMMON_PORTAL_2:
                    // 召唤第二个邪恶传送门
                    DoCastAOE(SPELL_FIENDISH_PORTAL_2, true);
                    break;
                case EVENT_ENRAGE:
                    // 10分钟后进入狂暴状态
                    DoCastSelf(SPELL_BERSERK, true);
                    break;
                default:
                    break;
            }
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回特雷斯坦AI实例
     *
     * 调用时机: 服务器创建特雷斯坦生物时
     * 功能: 创建并返回特雷斯坦AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_terestianAI>(creature);
    }
};

/**
 * @class npc_kilrek
 * @brief 基尔雷克(小鬼)NPC脚本类
 *
 * 继承自CreatureScript，用于注册基尔雷克的AI脚本
 * 基尔雷克是特雷斯坦的宠物小鬼，协助战斗
 */
class npc_kilrek : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册基尔雷克脚本名称
     */
    npc_kilrek() : CreatureScript("npc_kilrek") { }

    /**
     * @struct npc_kilrekAI
     * @brief 基尔雷克AI实现
     *
     * 继承自ScriptedAI，实现基尔雷克的战斗逻辑:
     * - 周期性施放增强火焰技能
     * - 死亡时对特雷斯坦施放破碎契约Debuff
     */
    struct npc_kilrekAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化基尔雷克AI
         */
        npc_kilrekAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 重置状态
         *
         * 调用时机: 生物重置时
         * 功能: 调度增强火焰技能(每8-9秒施放一次)
         */
        void Reset() override
        {
            _scheduler.Schedule(Seconds(8), [this](TaskContext amplify)
            {
                DoCastVictim(SPELL_AMPLIFY_FLAMES);
                amplify.Repeat(Seconds(9));
            });
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 基尔雷克死亡时
         * 功能:
         * - 对周围施放破碎契约法术，对特雷斯坦施加Debuff
         * - 15秒后消失
         */
        void JustDied(Unit* /*killer*/) override
        {
            DoCastAOE(SPELL_BROKEN_PACT, true);
            me->DespawnOrUnsummon(Seconds(15));
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 更新任务调度器并执行近战攻击
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            _scheduler.Update(diff, [this]
            {
                DoMeleeAttackIfReady();
            });
        }

    private:
        TaskScheduler _scheduler;  ///< 任务调度器，用于调度技能施放
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回基尔雷克AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_kilrekAI>(creature);
    }
};

/**
 * @class npc_demon_chain
 * @brief 恶魔锁链NPC脚本类
 *
 * 继承自CreatureScript，用于注册恶魔锁链的AI脚本
 * 恶魔锁链用于困住被献祭的玩家，需要被击杀才能解救玩家
 */
class npc_demon_chain : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册恶魔锁链脚本名称
     */
    npc_demon_chain() : CreatureScript("npc_demon_chain") { }

    /**
     * @struct npc_demon_chainAI
     * @brief 恶魔锁链AI实现
     *
     * 继承自PassiveAI，实现恶魔锁链的行为:
     * - 被召唤时记录被献祭的目标
     * - 死亡时移除目标的献祭效果
     */
    struct npc_demon_chainAI : public PassiveAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化恶魔锁链AI
         */
        npc_demon_chainAI(Creature* creature) : PassiveAI(creature) { }

        /**
         * @brief 被召唤回调
         * @param summoner 召唤者(被献祭的目标)
         *
         * 调用时机: 恶魔锁链被召唤时
         * 功能:
         * - 记录被献祭目标的GUID
         * - 对自己施放恶魔锁链视觉效果
         */
        void IsSummonedBy(WorldObject* summoner) override
        {
            _sacrificeGUID = summoner->GetGUID();
            DoCastSelf(SPELL_DEMON_CHAINS, true);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 恶魔锁链被击杀时
         * 功能: 移除被献祭目标身上的献祭效果，释放玩家
         */
        void JustDied(Unit* /*killer*/) override
        {
            if (Unit* sacrifice = ObjectAccessor::GetUnit(*me, _sacrificeGUID))
                sacrifice->RemoveAurasDueToSpell(SPELL_SACRIFICE);
        }

    private:
        ObjectGuid _sacrificeGUID;  ///< 被献祭目标的GUID
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回恶魔锁链AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_demon_chainAI>(creature);
    }
};

/**
 * @class npc_fiendish_portal
 * @brief 邪恶传送门NPC脚本类
 *
 * 继承自CreatureScript，用于注册邪恶传送门的AI脚本
 * 邪恶传送门会持续召唤邪恶小鬼协助战斗
 */
class npc_fiendish_portal : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册邪恶传送门脚本名称
     */
    npc_fiendish_portal() : CreatureScript("npc_fiendish_portal") { }

    /**
     * @struct npc_fiendish_portalAI
     * @brief 邪恶传送门AI实现
     *
     * 继承自PassiveAI，实现邪恶传送门的行为:
     * - 周期性召唤邪恶小鬼
     * - 管理召唤的小鬼列表
     */
    struct npc_fiendish_portalAI : public PassiveAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化邪恶传送门AI和召唤列表
         */
        npc_fiendish_portalAI(Creature* creature) : PassiveAI(creature), _summons(me) { }

        /**
         * @brief 重置状态
         *
         * 调用时机: 生物重置时
         * 功能: 调度召唤小鬼任务(每2.4-8秒召唤一次)
         */
        void Reset() override
        {
            _scheduler.Schedule(Milliseconds(2400), Seconds(8), [this](TaskContext summonImp)
            {
                DoCastAOE(SPELL_SUMMON_FIENDISH_IMP, true);
                summonImp.Repeat();
            });
        }

        /**
         * @brief 执行动作回调
         * @param action 动作ID
         *
         * 调用时机: 其他AI向传送门发送动作时
         * 功能: 如果是消失小鬼动作，消失所有召唤的小鬼
         */
        void DoAction(int32 action) override
        {
            if (action == ACTION_DESPAWN_IMPS)
                _summons.DespawnAll();
        }

        /**
         * @brief 召唤单位回调
         * @param summon 被召唤的单位
         *
         * 调用时机: 传送门召唤小鬼时
         * 功能:
         * - 将小鬼加入召唤列表
         * - 让小鬼进入战斗
         */
        void JustSummoned(Creature* summon) override
        {
            _summons.Summon(summon);
            DoZoneInCombat(summon);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 更新任务调度器
         */
        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
        }

    private:
        SummonList _summons;       ///< 召唤的小鬼列表
        TaskScheduler _scheduler;  ///< 任务调度器
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回邪恶传送门AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_fiendish_portalAI>(creature);
    }
};

/**
 * @class npc_fiendish_imp
 * @brief 邪恶小鬼NPC脚本类
 *
 * 继承自CreatureScript，用于注册邪恶小鬼的AI脚本
 * 邪恶小鬼由邪恶传送门召唤，使用火焰攻击玩家
 */
class npc_fiendish_imp : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册邪恶小鬼脚本名称
     */
    npc_fiendish_imp() : CreatureScript("npc_fiendish_imp") { }

    /**
     * @struct npc_fiendish_impAI
     * @brief 邪恶小鬼AI实现
     *
     * 继承自ScriptedAI，实现邪恶小鬼的战斗逻辑:
     * - 使用火焰箭攻击目标
     * - 免疫火焰伤害
     */
    struct npc_fiendish_impAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化邪恶小鬼AI
         */
        npc_fiendish_impAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 重置状态
         *
         * 调用时机: 生物重置时
         * 功能:
         * - 调度火焰箭技能(每2.4秒施放一次)
         * - 设置火焰伤害免疫
         */
        void Reset() override
        {
            _scheduler.Schedule(Seconds(2), [this](TaskContext firebolt)
            {
                DoCastVictim(SPELL_FIREBOLT);
                firebolt.Repeat(Milliseconds(2400));
            });

            // 免疫火焰伤害
            me->ApplySpellImmune(0, IMMUNITY_SCHOOL, SPELL_SCHOOL_MASK_FIRE, true);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 更新任务调度器并执行近战攻击
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            _scheduler.Update(diff, [this]
            {
                DoMeleeAttackIfReady();
            });
        }

    private:
        TaskScheduler _scheduler;  ///< 任务调度器
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回邪恶小鬼AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_fiendish_impAI>(creature);
    }
};

/**
 * @brief 注册特雷斯坦·邪蹄BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建特雷斯坦及其相关NPC的脚本实例，注册到脚本系统
 */
void AddSC_boss_terestian_illhoof()
{
    new boss_terestian_illhoof();
    new npc_kilrek();
    new npc_demon_chain();
    new npc_fiendish_portal();
    new npc_fiendish_imp();
}
