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
 * @file boss_chrono_lord_deja.cpp
 * @brief 黑色沼泽副本BOSS——时空领主德贾(Chrono Lord Deja)的AI脚本实现
 *
 * 模块职责:
 * - 实现BOSS时空领主德贾的AI行为逻辑
 * - 管理BOSS的奥术系技能释放时间轴
 * - 处理BOSS与时间守护者(Time Keeper)的交互
 * - 协调副本事件流程和裂隙状态
 *
 * BOSS概述:
 * 时空领主德贾是黑色沼泽副本的第一个BOSS,是一条时间龙,掌握奥术魔法。
 * 她会使用奥术冲击、奥术爆破和时间流逝等技能攻击玩家。
 * 在英雄难度下还会使用吸引技能。
 *
 * 主要技能:
 * - Arcane Blast(奥术冲击): 对目标造成奥术伤害
 * - Arcane Discharge(奥术爆破): 对随机目标造成范围奥术伤害
 * - Time Lapse(时间流逝): 使周围敌人减速
 * - Attraction(吸引): 英雄难度技能,吸引敌人
 *
 * 完成度: 65%
 * 备注: 部分能力尚未实现
 * 分类: 时光之穴 - 黑色沼泽
 */

/*
Name: Boss_Chrono_Lord_Deja
%Complete: 65
Comment: All abilities not implemented
Category: Caverns of Time, The Black Morass
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "the_black_morass.h"

/**
 * @enum Enums
 * @brief BOSS时空领主德贾使用的文本ID和法术ID枚举
 */
enum Enums
{
    // BOSS文本ID
    SAY_ENTER                   = 0,    ///< 进入战斗时的对话
    SAY_AGGRO                   = 1,    ///< 激活时的对话
    SAY_BANISH                  = 2,    ///< 放逐时间守护者时的对话
    SAY_SLAY                    = 3,    ///< 击杀玩家时的对话
    SAY_DEATH                   = 4,    ///< 死亡时的对话

    // BOSS法术ID
    SPELL_ARCANE_BLAST          = 31457,    ///< 奥术冲击(普通难度) - 对目标造成奥术伤害
    H_SPELL_ARCANE_BLAST        = 38538,    ///< 奥术冲击(英雄难度) - 英雄难度版本
    SPELL_ARCANE_DISCHARGE      = 31472,    ///< 奥术爆破(普通难度) - 对随机目标造成奥术伤害
    H_SPELL_ARCANE_DISCHARGE    = 38539,    ///< 奥术爆破(英雄难度) - 英雄难度版本
    SPELL_TIME_LAPSE            = 31467,    ///< 时间流逝 - 使周围敌人减速
    SPELL_ATTRACTION            = 38540     ///< 吸引(英雄难度) - 英雄难度技能(未实现)
};

/**
 * @enum Events
 * @brief BOSS时空领主德贾的事件类型枚举,用于事件调度系统
 */
enum Events
{
    EVENT_ARCANE_BLAST          = 1,    ///< 奥术冲击技能事件
    EVENT_TIME_LAPSE            = 2,    ///< 时间流逝技能事件
    EVENT_ARCANE_DISCHARGE      = 3,    ///< 奥术爆破技能事件
    EVENT_ATTRACTION            = 4     ///< 吸引技能事件(仅英雄难度)
};

/**
 * @struct boss_chrono_lord_deja
 * @brief BOSS时空领主德贾的AI实现类
 *
 * 继承自BossAI基类,实现时空领主德贾的所有战斗行为。
 * 时空领主德贾是黑色沼泽副本的第一个BOSS,使用奥术系魔法攻击玩家。
 *
 * 战斗流程:
 * 1. BOSS会定期对当前目标使用奥术冲击
 * 2. 使用时间流逝技能减速玩家
 * 3. 随机对目标施放奥术爆破
 * 4. 英雄难度下还会使用吸引技能
 * 5. 会主动攻击时间守护者
 */
struct boss_chrono_lord_deja : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_chrono_lord_deja(Creature* creature) : BossAI(creature, TYPE_CRONO_LORD_DEJA) { }

    /**
     * @brief 重置BOSS状态
     *
     * 当BOSS脱离战斗或被重置时调用。
     * 清除所有仇恨和事件状态。
     *
     * @调用时机: BOSS脱离战斗、重置副本、BOSS被重置时
     */
    void Reset() override { }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标(通常是最先攻击BOSS的玩家)
     *
     * 初始化战斗事件调度:
     * - 奥术冲击: 18-23秒后首次施放
     * - 时间流逝: 10-15秒后首次施放
     * - 奥术爆破: 20-30秒后首次施放
     * - 吸引: 仅英雄难度,25-35秒后首次施放
     *
     * @调用时机: BOSS被玩家攻击并成功建立仇恨时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // 调度奥术冲击技能,18-23秒后首次施放
        events.ScheduleEvent(EVENT_ARCANE_BLAST, 18s, 23s);
        // 调度时间流逝技能,10-15秒后首次施放
        events.ScheduleEvent(EVENT_TIME_LAPSE, 10s, 15s);
        // 调度奥术爆破技能,20-30秒后首次施放
        events.ScheduleEvent(EVENT_ARCANE_DISCHARGE, 20s, 30s);
        // 英雄难度下调度吸引技能
        if (IsHeroic())
            events.ScheduleEvent(EVENT_ATTRACTION, 25s, 35s);

        // 播放进入战斗的对话
        Talk(SAY_AGGRO);
    }

    /**
     * @brief 视线范围内检测函数
     * @param who 进入视线范围的单位
     *
     * 特殊功能:检测并消灭时间守护者(Time Keeper)
     * 当时间守护者进入BOSS的20码范围内时,BOSS会直接消灭它。
     * 这是副本机制的一部分,时间守护者可以帮助玩家防守,
     * 但BOSS会优先消灭这些助手。
     *
     * @调用时机: 每个游戏帧,当有单位进入BOSS视线范围时
     * @性能注意: 该函数频繁调用,应避免复杂计算
     */
    void MoveInLineOfSight(Unit* who) override

    {
        // 检测是否为时间守护者NPC
        if (who->GetTypeId() == TYPEID_UNIT && who->GetEntry() == NPC_TIME_KEEPER)
        {
            // 如果时间守护者在20码范围内
            if (me->IsWithinDistInMap(who, 20.0f))
            {
                // 播放放逐对话
                Talk(SAY_BANISH);
                // 直接对时间守护者造成其生命值上限的伤害,秒杀
                Unit::DealDamage(me, who, who->GetHealth(), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
            }
        }

        // 调用父类的视线检测函数,继续正常行为
        ScriptedAI::MoveInLineOfSight(who);
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     *
     * 当BOSS击杀玩家时播放击杀对话。
     *
     * @调用时机: BOSS击杀任何单位时
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        // 播放击杀对话
        Talk(SAY_SLAY);
    }

    /**
     * @brief BOSS死亡时调用
     * @param killer 击杀BOSS的单位(可为nullptr)
     *
     * 完成副本事件:
     * - 播放死亡对话
     * - 通知副本脚本裂隙事件有特殊进展
     *
     * @调用时机: BOSS生命值降至0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 播放死亡对话
        Talk(SAY_DEATH);

        // 通知副本脚本裂隙事件有特殊进展(触发下一个事件)
        instance->SetData(TYPE_RIFT, SPECIAL);
    }

    /**
     * @brief AI更新函数,每个游戏帧调用
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 主要逻辑:
     * 1. 检查是否有有效目标,无目标则返回
     * 2. 更新事件调度器
     * 3. 如果正在施法则等待
     * 4. 处理事件队列:
     *    - 奥术冲击: 对当前目标施放
     *    - 时间流逝: 对自己施放(影响周围敌人)
     *    - 奥术爆破: 对随机目标施放
     *    - 吸引: 英雄难度,对自己施放
     * 5. 执行近战攻击
     *
     * @调用时机: 每个游戏帧,频率取决于服务器帧率(通常约50-100ms)
     * @性能注意: 此函数每帧调用,应避免复杂计算,保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有有效目标,直接返回
        if (!UpdateVictim())
            return;

        // 更新事件调度器的时间
        events.Update(diff);

        // 如果正在施法,暂停其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ARCANE_BLAST:
                    // 对当前目标施放奥术冲击
                    DoCastVictim(SPELL_ARCANE_BLAST);
                    // 调度下一次奥术冲击,15-25秒后
                    events.ScheduleEvent(EVENT_ARCANE_BLAST, 15s, 25s);
                    break;
                case EVENT_TIME_LAPSE:
                    // 播放对话(注意:这里使用了SAY_BANISH,可能是复用)
                    Talk(SAY_BANISH);
                    // 对自己施放时间流逝(影响周围敌人)
                    DoCast(me, SPELL_TIME_LAPSE);
                    // 调度下一次时间流逝,15-25秒后
                    events.ScheduleEvent(EVENT_TIME_LAPSE, 15s, 25s);
                    break;
                case EVENT_ARCANE_DISCHARGE:
                    // 选择随机目标施放奥术爆破
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_ARCANE_DISCHARGE);
                    // 调度下一次奥术爆破,20-30秒后
                    events.ScheduleEvent(EVENT_ARCANE_DISCHARGE, 20s, 30s);
                    break;
                case EVENT_ATTRACTION: // 仅英雄难度
                    // 对自己施放吸引技能
                    DoCast(me, SPELL_ATTRACTION);
                    // 调度下一次吸引,25-35秒后
                    events.ScheduleEvent(EVENT_ATTRACTION, 25s, 35s);
                    break;
                default:
                    break;
            }

            // 如果事件处理后正在施法,暂停后续事件处理
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果准备好进行近战攻击,则执行
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册BOSS时空领主德贾的AI脚本
 *
 * 此函数由脚本系统在服务器启动时调用,用于注册BOSS的AI。
 * 使用RegisterBlackMorassCreatureAI宏将boss_chrono_lord_deja AI注册到脚本系统。
 *
 * @调用时机: 服务器启动时,脚本加载阶段
 */
void AddSC_boss_chrono_lord_deja()
{
    RegisterBlackMorassCreatureAI(boss_chrono_lord_deja);
}
