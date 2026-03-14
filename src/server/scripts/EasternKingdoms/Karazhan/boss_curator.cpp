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
 * @file boss_curator.cpp
 * @brief 卡拉赞副本 - 馆长(Allied Curator)BOSS脚本模块
 *
 * 本模块实现了馆长BOSS的战斗逻辑，包括:
 * - 馆长BOSS AI，管理战斗阶段和技能释放
 * - 星光火花(Astral Flare)小怪AI，辅助馆长战斗
 *
 * 战斗机制:
 * - 馆长会周期性召唤星光火花小怪
 * - 馆长使用法力值控制战斗节奏，法力低于10%时释放唤醒技能回满法力
 * - 生命值低于15%时进入激化阶段，停止召唤星光火花
 * - 12分钟狂暴时间限制
 */

#include "ScriptMgr.h"
#include "karazhan.h"
#include "ScriptedCreature.h"

/**
 * @brief 馆长BOSS台词枚举
 *
 * 定义馆长在战斗中各个阶段说的话
 */
enum CuratorSays
{
    SAY_AGGRO                    = 0,  ///< 进入战斗台词
    SAY_SUMMON                   = 1,  ///< 召唤星光火花台词
    SAY_EVOCATE                  = 2,  ///< 唤醒台词
    SAY_ENRAGE                   = 3,  ///< 狂暴台词
    SAY_KILL                     = 4,  ///< 击杀玩家台词
    SAY_DEATH                    = 5   ///< 死亡台词
};

/**
 * @brief 馆主BOSS技能ID枚举
 *
 * 定义馆主使用的所有法术技能ID
 */
enum CuratorSpells
{
    SPELL_HATEFUL_BOLT           = 30383,  ///< 憎恨之箭：对仇恨第二高的目标造成奥术伤害
    SPELL_EVOCATION              = 30254,  ///< 唤醒：法力低于10%时施放，恢复所有法力
    SPELL_ARCANE_INFUSION        = 30403,  ///< 奥术灌注：生命值低于15%时使用，增强伤害
    SPELL_BERSERK                = 26662,  ///< 狂暴：12分钟后施放，大幅增加伤害
    SPELL_SUMMON_ASTRAL_FLARE_NE = 30236,  ///< 召唤星光火花(东北方向)
    SPELL_SUMMON_ASTRAL_FLARE_NW = 30239,  ///< 召唤星光火花(西北方向)
    SPELL_SUMMON_ASTRAL_FLARE_SE = 30240,  ///< 召唤星光火花(东南方向)
    SPELL_SUMMON_ASTRAL_FLARE_SW = 30241   ///< 召唤星光火花(西南方向)
};

/**
 * @brief 馆长BOSS事件枚举
 *
 * 定义馆长战斗中的各种事件ID，用于事件调度器
 */
enum CuratorEvents
{
    EVENT_HATEFUL_BOLT = 1,       ///< 憎恨之箭事件
    EVENT_SUMMON_ASTRAL_FLARE,    ///< 召唤星光火花事件
    EVENT_ARCANE_INFUSION,        ///< 奥术灌注事件
    EVENT_BERSERK                 ///< 狂暴事件
};

/**
 * @class boss_curator
 * @brief 馆长BOSS脚本类
 *
 * 继承自CreatureScript，用于注册馆长BOSS的AI脚本
 */
class boss_curator : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册馆长脚本名称
     */
    boss_curator() : CreatureScript("boss_curator") { }

    /**
     * @struct boss_curatorAI
     * @brief 馆长BOSS的AI实现
     *
     * 继承自BossAI，实现馆长的战斗逻辑:
     * - 管理法力值，周期性召唤星光火花
     * - 法力低于10%时施放唤醒恢复法力
     * - 生命值低于15%时施放奥术灌注进入激化阶段
     * - 12分钟狂暴机制
     */
    struct boss_curatorAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化馆长AI，设置副本数据ID和激化状态标志
         */
        boss_curatorAI(Creature* creature) : BossAI(creature, DATA_CURATOR), _infused(false) { }

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能: 重置所有战斗相关状态，包括激化标志
         */
        void Reset() override
        {
            _Reset();
            _infused = false;
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位
         *
         * 调用时机: 当馆长杀死一个单位时
         * 功能: 如果击杀的是玩家，播放击杀台词
         */
        void KilledUnit(Unit* victim) override
        {
            if (victim->GetTypeId() == TYPEID_PLAYER)
                Talk(SAY_KILL);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 馆长死亡时
         * 功能: 触发死亡事件，播放死亡台词
         */
        void JustDied(Unit* /*killer*/) override
        {
            _JustDied();
            Talk(SAY_DEATH);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         *
         * 调用时机: 馆长进入战斗时
         * 功能:
         * - 调用父类的进入战斗方法
         * - 播放战斗开始台词
         * - 调度憎恨之箭事件(12秒后)
         * - 调度召唤星光火花事件(10秒后)
         * - 调度狂暴事件(12分钟后)
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);
            Talk(SAY_AGGRO);

            events.ScheduleEvent(EVENT_HATEFUL_BOLT, 12s);
            events.ScheduleEvent(EVENT_SUMMON_ASTRAL_FLARE, 10s);
            events.ScheduleEvent(EVENT_BERSERK, 12min);
        }

        /**
         * @brief 受到伤害回调
         * @param attacker 攻击者(未使用)
         * @param damage 伤害值(未使用)
         * @param damageType 伤害类型(未使用)
         * @param spellInfo 法术信息(未使用)
         *
         * 调用时机: 馆长受到伤害时
         * 功能:
         * - 当生命值低于15%且未处于激化状态时
         * - 进入激化阶段，施放奥术灌注
         * - 取消后续的星光火花召唤事件
         *
         * 性能注意: 每次伤害都会调用，避免在此执行耗时操作
         */
        void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            // 生命值低于15%时触发奥术灌注，进入激化阶段
            if (!HealthAbovePct(15) && !_infused)
            {
                _infused = true;
                events.ScheduleEvent(EVENT_ARCANE_INFUSION, Milliseconds(1));
                events.CancelEvent(EVENT_SUMMON_ASTRAL_FLARE);  // 激化后不再召唤星光火花
            }
        }

        /**
         * @brief 执行事件回调
         * @param eventId 事件ID
         *
         * 调用时机: 事件调度器触发事件时
         * 功能: 根据事件ID执行相应技能
         * - EVENT_HATEFUL_BOLT: 对仇恨第二高的目标施放憎恨之箭
         * - EVENT_ARCANE_INFUSION: 对自己施放奥术灌注
         * - EVENT_SUMMON_ASTRAL_FLARE: 召唤星光火花并消耗法力
         * - EVENT_BERSERK: 施放狂暴法术
         */
        void ExecuteEvent(uint32 eventId) override
        {
            switch (eventId)
            {
                case EVENT_HATEFUL_BOLT:
                    // 选择仇恨值第二高的目标施放憎恨之箭
                    if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 1))
                        DoCast(target, SPELL_HATEFUL_BOLT);
                    events.Repeat(Seconds(7), Seconds(15));
                    break;
                case EVENT_ARCANE_INFUSION:
                    // 施放奥术灌注，进入激化阶段
                    DoCastSelf(SPELL_ARCANE_INFUSION, true);
                    break;
                case EVENT_SUMMON_ASTRAL_FLARE:
                    // 50%概率说出台词
                    if (roll_chance_i(50))
                        Talk(SAY_SUMMON);

                    // 随机在四个方向召唤星光火花
                    DoCastSelf(RAND(SPELL_SUMMON_ASTRAL_FLARE_NE, SPELL_SUMMON_ASTRAL_FLARE_NW, SPELL_SUMMON_ASTRAL_FLARE_SE, SPELL_SUMMON_ASTRAL_FLARE_SW), true);

                    // 每次召唤消耗10%最大法力值
                    if (int32 mana = int32(me->GetMaxPower(POWER_MANA) / 10))
                    {
                        me->ModifyPower(POWER_MANA, -mana);

                        // 法力低于10%时施放唤醒恢复法力
                        if (me->GetPower(POWER_MANA) * 100 / me->GetMaxPower(POWER_MANA) < 10)
                        {
                            Talk(SAY_EVOCATE);
                            me->InterruptNonMeleeSpells(false);  // 打断非近战法术
                            DoCastSelf(SPELL_EVOCATION);
                        }
                    }
                    events.Repeat(Seconds(10));
                    break;
                case EVENT_BERSERK:
                    // 12分钟后进入狂暴状态
                    Talk(SAY_ENRAGE);
                    DoCastSelf(SPELL_BERSERK, true);
                    break;
                default:
                    break;
            }
        }

    private:
        bool _infused;  ///< 是否已进入激化阶段(生命值<15%时触发)
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回馆长AI实例
     *
     * 调用时机: 服务器创建馆长生物时
     * 功能: 创建并返回馆长AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_curatorAI>(creature);
    }
};

/**
 * @class npc_curator_astral_flare
 * @brief 星光火花小怪脚本类
 *
 * 继承自CreatureScript，用于注册星光火花的AI脚本
 * 星光火花是馆长战斗中召唤的小怪，会攻击玩家
 */
class npc_curator_astral_flare : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册星光火花脚本名称
     */
    npc_curator_astral_flare() : CreatureScript("npc_curator_astral_flare") { }

    /**
     * @struct npc_curator_astral_flareAI
     * @brief 星光火花AI实现
     *
     * 继承自ScriptedAI，实现星光火花的简单行为逻辑:
     * - 召唤后延迟2秒进入战斗
     * - 攻击玩家目标
     */
    struct npc_curator_astral_flareAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化星光火花AI，设置为被动反应状态
         */
        npc_curator_astral_flareAI(Creature* creature) : ScriptedAI(creature)
        {
            me->SetReactState(REACT_PASSIVE);  // 初始设置为被动，等待延迟激活
        }

        /**
         * @brief 重置状态
         *
         * 调用时机: 生物重置时
         * 功能:
         * - 调度2秒延迟任务
         * - 延迟后激活生物，设置为主动攻击
         * - 移除不可交互标志
         * - 加入战斗
         */
        void Reset() override
        {
            _scheduler.Schedule(Seconds(2), [this](TaskContext /*context*/)
            {
                me->SetReactState(REACT_AGGRESSIVE);  // 切换为主动攻击
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 允许玩家攻击
                DoZoneInCombat();  // 加入战斗
            });
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 更新任务调度器，执行延迟任务
         *
         * 性能注意: 每帧调用，保持简洁
         */
        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
        }

    private:
        TaskScheduler _scheduler;  ///< 任务调度器，用于延迟激活
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回星光火花AI实例
     *
     * 调用时机: 服务器创建星光火花生物时
     * 功能: 创建并返回星光火花AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_curator_astral_flareAI>(creature);
    }
};

/**
 * @brief 注册馆长BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建馆长和星光火花脚本实例，注册到脚本系统
 */
void AddSC_boss_curator()
{
    new boss_curator();
    new npc_curator_astral_flare();
}
