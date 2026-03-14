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
 * @file boss_midnight.cpp
 * @brief 卡拉赞副本 - 午夜(骏马)与阿图门(猎人)BOSS脚本模块
 *
 * 本模块实现了午夜和阿图门双BOSS的战斗逻辑，包括:
 * - 午夜(骏马)BOSS AI
 * - 阿图门(猎人)BOSS AI
 * - 阿图门骑乘状态BOSS AI
 *
 * 战斗机制(三阶段):
 * - 第一阶段: 午夜独自战斗，生命值降至95%时召唤阿图门
 * - 第二阶段: 午夜和阿图门同时战斗，两者生命值降至25%时合并
 * - 第三阶段: 阿图门骑乘午夜，以单一BOSS形式战斗
 *
 * 特殊机制:
 * - 午夜和阿图门在合并前不会真正死亡
 * - 合并后共享生命值
 */

/* ScriptData
SDName: Boss_Midnight
SD%Complete: 100
SDComment:
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "Containers.h"
#include "karazhan.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

/**
 * @brief 文本枚举
 *
 * 定义午夜和阿图门在战斗中各个阶段说的话和表情
 */
enum Texts
{
    SAY_KILL          = 0,  ///< 阿图门击杀玩家台词
    SAY_RANDOM        = 1,  ///< 阿图门随机台词
    SAY_DISARMED      = 2,  ///< 阿图门被缴械台词
    SAY_MIDNIGHT_KILL = 3,  ///< 午夜击杀玩家时阿图门的台词
    SAY_APPEAR        = 4,  ///< 阿图门出现台词
    SAY_MOUNT         = 5,  ///< 阿图门骑乘午夜台词

    SAY_DEATH         = 3,  ///< 阿图门死亡台词

    // Midnight - 午夜的表情
    EMOTE_CALL_ATTUMEN = 0,  ///< 午夜召唤阿图门的表情
    EMOTE_MOUNT_UP     = 1   ///< 午夜准备让阿图门骑乘的表情
};

/**
 * @brief 技能ID枚举
 *
 * 定义午夜和阿图门使用的所有法术技能ID
 */
enum Spells
{
    // Attumen - 阿图门技能
    SPELL_SHADOWCLEAVE           = 29832,  ///< 影子顺劈斩：对前方敌人造成暗影伤害
    SPELL_INTANGIBLE_PRESENCE    = 29833,  ///< 虚无存在：降低目标的命中和躲闪几率
    SPELL_SPAWN_SMOKE            = 10389,  ///< 生成烟雾：视觉效果
    SPELL_CHARGE                 = 29847,  ///< 冲锋：向目标冲锋造成伤害并昏迷

    // Midnight - 午夜技能
    SPELL_KNOCKDOWN              = 29711,  ///< 击倒：使目标倒地
    SPELL_SUMMON_ATTUMEN         = 29714,  ///< 召唤阿图门：午夜生命值95%时召唤
    SPELL_MOUNT                  = 29770,  ///< 骑乘：午夜和阿图门合并的触发法术
    SPELL_SUMMON_ATTUMEN_MOUNTED = 29799   ///< 召唤骑乘状态的阿图门：合并后的BOSS
};

/**
 * @brief 战斗阶段枚举
 *
 * 定义午夜和阿图门战斗的各个阶段
 */
enum Phases
{
    PHASE_NONE,              ///< 无阶段(初始状态)
    PHASE_ATTUMEN_ENGAGES,   ///< 阿图门参战阶段(第二阶段)
    PHASE_MOUNTED            ///< 骑乘阶段(第三阶段)
};

/**
 * @class boss_attumen
 * @brief 阿图门BOSS脚本类
 *
 * 继承自CreatureScript，用于注册阿图门BOSS的AI脚本
 * 阿图门是午夜(骏马)的骑手，战斗中会与午夜配合
 */
class boss_attumen : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册阿图门脚本名称
     */
    boss_attumen() : CreatureScript("boss_attumen") { }

    /**
     * @struct boss_attumenAI
     * @brief 阿图门BOSS的AI实现
     *
     * 继承自BossAI，实现阿图门的战斗逻辑:
     * - 被午夜召唤后进入战斗
     * - 施放影子顺劈斩和虚无存在
     * - 生命值降至25%时与午夜合并
     * - 合并后以骑乘状态继续战斗
     */
    struct boss_attumenAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化阿图门AI，设置副本数据ID并初始化成员变量
         */
        boss_attumenAI(Creature* creature) : BossAI(creature, DATA_ATTUMEN)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 功能: 清空午夜的GUID，重置战斗阶段为无阶段
         */
        void Initialize()
        {
            _midnightGUID.Clear();
            _phase = PHASE_NONE;
        }

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能: 重置所有战斗相关状态
         */
        void Reset() override
        {
            Initialize();
            BossAI::Reset();
        }

        /**
         * @brief 进入逃避模式
         * @param why 逃避原因(未使用)
         *
         * 调用时机: BOSS需要逃避时(如战斗区域外)
         * 功能: 10秒后消失午夜和阿图门
         */
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            // 如果午夜存在，10秒后消失
            if (Creature* midnight = ObjectAccessor::GetCreature(*me, _midnightGUID))
                BossAI::_DespawnAtEvade(Seconds(10), midnight);

            me->DespawnOrUnsummon();
        }

        /**
         * @brief 调度任务
         *
         * 调用时机: 进入战斗后
         * 功能: 设置技能释放计划:
         * - 15-25秒后施放影子顺劈斩，重复
         * - 25-45秒后施放虚无存在，重复
         * - 30-60秒后说随机台词，重复
         */
        void ScheduleTasks() override
        {
            // 影子顺劈斩任务
            scheduler.Schedule(Seconds(15), Seconds(25), [this](TaskContext task)
            {
                DoCastVictim(SPELL_SHADOWCLEAVE);
                task.Repeat(Seconds(15), Seconds(25));
            });

            // 虚无存在任务
            scheduler.Schedule(Seconds(25), Seconds(45), [this](TaskContext task)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target,SPELL_INTANGIBLE_PRESENCE);

                task.Repeat(Seconds(25), Seconds(45));
            });

            // 随机台词任务
            scheduler.Schedule(Seconds(30), Seconds(60), [this](TaskContext task)
            {
                Talk(SAY_RANDOM);
                task.Repeat(Seconds(30), Seconds(60));
            });
        }

        /**
         * @brief 受到伤害回调
         * @param attacker 攻击者(未使用)
         * @param damage 伤害值(可修改)
         * @param damageType 伤害类型(未使用)
         * @param spellInfo 法术信息(未使用)
         *
         * 调用时机: 阿图门受到伤害时
         * 功能:
         * - 在骑乘前不会真正死亡，生命值最低为1
         * - 生命值降至25%时触发与午夜合并
         *
         * 性能注意: 每次伤害都会调用，避免在此执行耗时操作
         */
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            // 阿图门在骑乘前不会死亡，将伤害限制在生命值-1
            if (damage >= me->GetHealth() && _phase != PHASE_MOUNTED)
                damage = me->GetHealth() - 1;

            // 生命值低于25%时触发合并
            if (_phase == PHASE_ATTUMEN_ENGAGES && me->HealthBelowPctDamaged(25, damage))
            {
                _phase = PHASE_NONE;

                // 让午夜施放骑乘法术
                if (Creature* midnight = ObjectAccessor::GetCreature(*me, _midnightGUID))
                    midnight->AI()->DoCastAOE(SPELL_MOUNT, true);
            }
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位(未使用)
         *
         * 调用时机: 阿图门杀死一个单位时
         * 功能: 播放击杀台词
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_KILL);
        }

        /**
         * @brief 召唤单位回调
         * @param summon 被召唤的单位
         *
         * 调用时机: 阿图门召唤其他单位时
         * 功能:
         * - 如果召唤的是骑乘状态的阿图门，设置其生命值为午夜和阿图门中较高的那个
         * - 让骑乘状态的阿图门进入战斗并记录午夜的GUID
         */
        void JustSummoned(Creature* summon) override
        {
            if (summon->GetEntry() == NPC_ATTUMEN_MOUNTED)
                if (Creature* midnight = ObjectAccessor::GetCreature(*me, _midnightGUID))
                {
                    // 设置合并后的生命值为两者中较高的
                    if (midnight->GetHealth() > me->GetHealth())
                        summon->SetHealth(midnight->GetHealth());
                    else
                        summon->SetHealth(me->GetHealth());

                    summon->AI()->DoZoneInCombat();
                    summon->AI()->SetGUID(_midnightGUID, NPC_MIDNIGHT);
                }

            BossAI::JustSummoned(summon);
        }

        /**
         * @brief 被召唤回调
         * @param summoner 召唤者
         *
         * 调用时机: 阿图门被召唤时
         * 功能:
         * - 如果召唤者是午夜，设置为阿图门参战阶段
         * - 如果召唤者是未骑乘的阿图门(合并)，设置为骑乘阶段，并调度冲锋和击倒技能
         */
        void IsSummonedBy(WorldObject* summoner) override
        {
            // 被午夜召唤，进入第二阶段
            if (summoner->GetEntry() == NPC_MIDNIGHT)
                _phase = PHASE_ATTUMEN_ENGAGES;

            // 合并后被召唤，进入第三阶段(骑乘状态)
            if (summoner->GetEntry() == NPC_ATTUMEN_UNMOUNTED)
            {
                _phase = PHASE_MOUNTED;
                DoCastSelf(SPELL_SPAWN_SMOKE);

                // 冲锋任务：选择8-25码范围内的随机目标冲锋
                scheduler.Schedule(Seconds(10), Seconds(25), [this](TaskContext task)
                {
                    Unit* target = nullptr;
                    std::vector<Unit*> target_list;

                    // 筛选8-25码范围内的目标
                    for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
                    {
                        target = ref->GetVictim();
                        if (target && !target->IsWithinDist(me, 8.00f, false) && target->IsWithinDist(me, 25.0f, false))
                            target_list.push_back(target);

                        target = nullptr;
                    }

                    // 随机选择一个目标冲锋
                    if (!target_list.empty())
                        target = Trinity::Containers::SelectRandomContainerElement(target_list);

                    DoCast(target, SPELL_CHARGE);
                    task.Repeat(Seconds(10), Seconds(25));
                });

                // 击倒任务
                scheduler.Schedule(Seconds(25), Seconds(35), [this](TaskContext task)
                {
                    DoCastVictim(SPELL_KNOCKDOWN);
                    task.Repeat(Seconds(25), Seconds(35));
                });
            }
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 阿图门死亡时
         * 功能: 播放死亡台词，杀死午夜，触发死亡事件
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            // 杀死午夜
            if (Unit* midnight = ObjectAccessor::GetUnit(*me, _midnightGUID))
                midnight->KillSelf();

            _JustDied();
        }

        /**
         * @brief 设置GUID
         * @param guid 要设置的GUID
         * @param id 标识符
         *
         * 调用时机: 其他AI传递GUID给阿图门时
         * 功能: 如果id是午夜的NPC ID，记录午夜的GUID
         */
        void SetGUID(ObjectGuid const& guid, int32 id) override
        {
            if (id == NPC_MIDNIGHT)
                _midnightGUID = guid;
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 更新调度器并执行近战攻击
         *
         * 性能注意: 每帧调用，保持简洁
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有有效攻击目标且不在特殊阶段，直接返回
            if (!UpdateVictim() && _phase != PHASE_NONE)
                return;

            // 更新调度器，如果没有事件执行则进行近战攻击
            scheduler.Update(diff,
                std::bind(&BossAI::DoMeleeAttackIfReady, this));
        }

        /**
         * @brief 法术命中回调
         * @param caster 施法者(未使用)
         * @param spellInfo 法术信息
         *
         * 调用时机: 法术命中阿图门时
         * 功能:
         * - 如果是缴械法术，说出台词
         * - 如果是骑乘法术，执行合并逻辑:
         *   1. 停止所有攻击
         *   2. 让午夜和阿图门互相靠近
         *   3. 距离足够近时召唤骑乘状态的阿图门
         *   4. 隐藏午夜间和阿图门
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            // 被缴械时的反应
            if (spellInfo->Mechanic == MECHANIC_DISARM)
                Talk(SAY_DISARMED);

            // 骑乘法术命中，开始合并流程
            if (spellInfo->Id == SPELL_MOUNT)
            {
                if (Creature* midnight = ObjectAccessor::GetCreature(*me, _midnightGUID))
                {
                    _phase = PHASE_NONE;
                    scheduler.CancelAll();

                    // 午夜停止攻击，跟随阿图门
                    midnight->AttackStop();
                    midnight->RemoveAllAttackers();
                    midnight->SetReactState(REACT_PASSIVE);
                    midnight->GetMotionMaster()->MoveFollow(me, 2.0f, 0.0f);
                    midnight->AI()->Talk(EMOTE_MOUNT_UP);

                    // 阿图门停止攻击，跟随午夜
                    me->AttackStop();
                    me->RemoveAllAttackers();
                    me->SetReactState(REACT_PASSIVE);
                    me->GetMotionMaster()->MoveFollow(midnight, 2.0f, 0.0f);
                    Talk(SAY_MOUNT);

                    // 每秒检查距离，足够近时合并
                    scheduler.Schedule(Seconds(1), [this](TaskContext task)
                    {
                        if (Creature* midnight = ObjectAccessor::GetCreature(*me, _midnightGUID))
                        {
                            // 距离小于5码时合并
                            if (me->IsWithinDist2d(midnight, 5.0f))
                            {
                                // 召唤骑乘状态的阿图门
                                DoCastAOE(SPELL_SUMMON_ATTUMEN_MOUNTED);
                                me->SetVisible(false);
                                me->GetMotionMaster()->Clear();
                                midnight->SetVisible(false);
                            }
                            else
                            {
                                // 继续互相靠近
                                midnight->GetMotionMaster()->MoveFollow(me, 2.0f, 0.0f);
                                me->GetMotionMaster()->MoveFollow(midnight, 2.0f, 0.0f);
                                task.Repeat();
                            }
                        }
                    });
                }
            }
        }

    private:
        ObjectGuid _midnightGUID;  ///< 午夜的GUID
        uint8 _phase;              ///< 当前战斗阶段
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回阿图门AI实例
     *
     * 调用时机: 服务器创建阿图门生物时
     * 功能: 创建并返回阿图门AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_attumenAI>(creature);
    }
};

/**
 * @class boss_midnight
 * @brief 午夜(骏马)BOSS脚本类
 *
 * 继承自CreatureScript，用于注册午夜BOSS的AI脚本
 * 午夜是阿图门的坐骑，战斗中与阿图门配合
 */
class boss_midnight : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册午夜脚本名称
     */
    boss_midnight() : CreatureScript("boss_midnight") { }

    /**
     * @struct boss_midnightAI
     * @brief 午夜BOSS的AI实现
     *
     * 继承自BossAI，实现午夜的战斗逻辑:
     * - 初始独自战斗，施放击倒技能
     * - 生命值降至95%时召唤阿图门
     * - 生命值降至25%时与阿图门合并
     * - 合并前不会死亡
     */
    struct boss_midnightAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化午夜AI，设置副本数据ID并初始化成员变量
         */
        boss_midnightAI(Creature* creature) : BossAI(creature, DATA_ATTUMEN)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 功能: 重置战斗阶段为无阶段
         */
        void Initialize()
        {
            _phase = PHASE_NONE;
        }

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能: 重置所有战斗相关状态，显示午夜并设置为防御状态
         */
        void Reset() override
        {
            Initialize();
            BossAI::Reset();
            me->SetVisible(true);
            me->SetReactState(REACT_DEFENSIVE);
        }

        /**
         * @brief 受到伤害回调
         * @param attacker 攻击者(未使用)
         * @param damage 伤害值(可修改)
         * @param damageType 伤害类型(未使用)
         * @param spellInfo 法术信息(未使用)
         *
         * 调用时机: 午夜受到伤害时
         * 功能:
         * - 在合并前不会真正死亡，生命值最低为1
         * - 生命值降至95%时召唤阿图门
         * - 生命值降至25%时触发合并
         *
         * 性能注意: 每次伤害都会调用，避免在此执行耗时操作
         */
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            // 午夜在合并前不会死亡，将伤害限制在生命值-1
            if (damage >= me->GetHealth())
                damage = me->GetHealth() - 1;

            // 生命值低于95%时召唤阿图门(第一阶段->第二阶段)
            if (_phase == PHASE_NONE && me->HealthBelowPctDamaged(95, damage))
            {
                _phase = PHASE_ATTUMEN_ENGAGES;
                Talk(EMOTE_CALL_ATTUMEN);
                DoCastAOE(SPELL_SUMMON_ATTUMEN);
            }
            // 生命值低于25%时与阿图门合并(第二阶段->第三阶段)
            else if (_phase == PHASE_ATTUMEN_ENGAGES && me->HealthBelowPctDamaged(25, damage))
            {
                _phase = PHASE_MOUNTED;
                DoCastAOE(SPELL_MOUNT, true);
            }
        }

        /**
         * @brief 召唤单位回调
         * @param summon 被召唤的单位
         *
         * 调用时机: 午夜召唤其他单位时
         * 功能:
         * - 如果召唤的是阿图门，记录其GUID
         * - 让阿图门攻击午夜的目标
         * - 让阿图门说出出现台词
         */
        void JustSummoned(Creature* summon) override
        {
            if (summon->GetEntry() == NPC_ATTUMEN_UNMOUNTED)
            {
                _attumenGUID = summon->GetGUID();
                summon->AI()->SetGUID(me->GetGUID(), NPC_MIDNIGHT);
                summon->AI()->AttackStart(me->GetVictim());
                summon->AI()->Talk(SAY_APPEAR);
            }

            BossAI::JustSummoned(summon);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         *
         * 调用时机: 午夜进入战斗时
         * 功能:
         * - 调用父类的进入战斗方法
         * - 调度击倒技能(15-25秒后)
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);

            scheduler.Schedule(Seconds(15), Seconds(25), [this](TaskContext task)
            {
                DoCastVictim(SPELL_KNOCKDOWN);
                task.Repeat(Seconds(15), Seconds(25));
            });
        }

        /**
         * @brief 进入逃避模式
         * @param why 逃避原因(未使用)
         *
         * 调用时机: BOSS需要逃避时
         * 功能: 10秒后消失午夜
         */
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            BossAI::_DespawnAtEvade(Seconds(10));
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位(未使用)
         *
         * 调用时机: 午夜杀死一个单位时
         * 功能: 如果在阿图门参战阶段，让阿图门说出击杀台词
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            if (_phase == PHASE_ATTUMEN_ENGAGES)
            {
                if (Unit* unit = ObjectAccessor::GetUnit(*me, _attumenGUID))
                    Talk(SAY_MIDNIGHT_KILL, unit);
            }
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能:
         * - 检查是否有有效攻击目标
         * - 如果进入骑乘阶段，停止攻击
         * - 更新调度器并执行近战攻击
         *
         * 性能注意: 每帧调用，保持简洁
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有有效攻击目标或已进入骑乘阶段，直接返回
            if (!UpdateVictim() || _phase == PHASE_MOUNTED)
                return;

            // 更新调度器，如果没有事件执行则进行近战攻击
            scheduler.Update(diff,
                std::bind(&BossAI::DoMeleeAttackIfReady, this));
        }

        private:
            ObjectGuid _attumenGUID;  ///< 阿图门的GUID
            uint8 _phase;             ///< 当前战斗阶段
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回午夜AI实例
     *
     * 调用时机: 服务器创建午夜生物时
     * 功能: 创建并返回午夜AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_midnightAI>(creature);
    }
};

/**
 * @brief 注册午夜和阿图门BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建午夜和阿图门脚本实例，注册到脚本系统
 */
void AddSC_boss_attumen()
{
    new boss_attumen();
    new boss_midnight();
}
