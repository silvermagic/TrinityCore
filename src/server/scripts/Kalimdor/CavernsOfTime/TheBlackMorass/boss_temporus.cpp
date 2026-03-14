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
 * @file boss_temporus.cpp
 * @brief 黑色沼泽副本BOSS——坦普卢斯(Temporus)的AI脚本实现
 *
 * 模块职责:
 * - 实现BOSS坦普卢斯的AI行为逻辑
 * - 管理BOSS的物理和魔法技能释放时间轴
 * - 处理BOSS与时间守护者(Time Keeper)的交互
 * - 协调副本事件流程和裂隙状态
 *
 * BOSS概述:
 * 坦普卢斯是黑色沼泽副本的第二个BOSS,是一条时间龙,擅长物理攻击和时间魔法。
 * 他会使用急速、致命伤口、翼龙猛击等技能攻击玩家,在英雄难度下还能反射法术。
 *
 * 主要技能:
 * - Haste(急速): 提高攻击速度
 * - Mortal Wound(致命伤口): 对目标造成持续伤害并降低治疗效果
 * - Wing Buffet(翼龙猛击): 击退周围的敌人
 * - Spell Reflection(法术反射): 英雄难度技能,反射法术
 *
 * 完成度: 75%
 * 备注: 更多能力需要实现
 * 分类: 时光之穴 - 黑色沼泽
 */

/*
Name: Boss_Temporus
%Complete: 75
Comment: More abilities need to be implemented
Category: Caverns of Time, The Black Morass
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "the_black_morass.h"

/**
 * @enum Enums
 * @brief BOSS坦普卢斯使用的文本ID和法术ID枚举
 */
enum Enums
{
    // BOSS文本ID
    SAY_ENTER               = 0,    ///< 进入战斗时的对话
    SAY_AGGRO               = 1,    ///< 激活时的对话
    SAY_BANISH              = 2,    ///< 放逐时间守护者时的对话
    SAY_SLAY                = 3,    ///< 击杀玩家时的对话
    SAY_DEATH               = 4,    ///< 死亡时的对话

    // BOSS法术ID
    SPELL_HASTE             = 31458,    ///< 急速 - 提高攻击速度
    SPELL_MORTAL_WOUND      = 31464,    ///< 致命伤口 - 降低治疗效果
    SPELL_WING_BUFFET       = 31475,    ///< 翼龙猛击(普通难度) - 击退周围敌人
    H_SPELL_WING_BUFFET     = 38593,    ///< 翼龙猛击(英雄难度) - 英雄难度版本
    SPELL_REFLECT           = 38592     ///< 法术反射(英雄难度) - 反射法术(未实现)
};

/**
 * @enum Events
 * @brief BOSS坦普卢斯的事件类型枚举,用于事件调度系统
 */
enum Events
{
    EVENT_HASTE             = 1,    ///< 急速技能事件
    EVENT_MORTAL_WOUND      = 2,    ///< 致命伤口技能事件
    EVENT_WING_BUFFET       = 3,    ///< 翼龙猛击技能事件
    EVENT_SPELL_REFLECTION  = 4     ///< 法术反射技能事件(仅英雄难度)
};

/**
 * @struct boss_temporus
 * @brief BOSS坦普卢斯的AI实现类
 *
 * 继承自BossAI基类,实现坦普卢斯的所有战斗行为。
 * 坦普卢斯是黑色沼泽副本的第二个BOSS,擅长物理攻击和时间魔法。
 *
 * 战斗流程:
 * 1. BOSS会定期对自己使用急速增益
 * 2. 对当前目标施放致命伤口,降低治疗效果
 * 3. 使用翼龙猛击击退周围的敌人
 * 4. 英雄难度下还能反射法术
 * 5. 会主动攻击时间守护者
 */
struct boss_temporus : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_temporus(Creature* creature) : BossAI(creature, TYPE_TEMPORUS) { }

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
     * - 急速: 15-23秒后首次施放
     * - 致命伤口: 8秒后首次施放
     * - 翼龙猛击: 25-35秒后首次施放
     * - 法术反射: 仅英雄难度,30秒后首次施放
     *
     * @调用时机: BOSS被玩家攻击并成功建立仇恨时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // 调度急速技能,15-23秒后首次施放
        events.ScheduleEvent(EVENT_HASTE, 15s, 23s);
        // 调度致命伤口技能,8秒后首次施放
        events.ScheduleEvent(EVENT_MORTAL_WOUND, 8s);
        // 调度翼龙猛击技能,25-35秒后首次施放
        events.ScheduleEvent(EVENT_WING_BUFFET, 25s, 35s);
        // 英雄难度下调度法术反射技能
        if (IsHeroic())
            events.ScheduleEvent(EVENT_SPELL_REFLECTION, 30s);

        // 播放进入战斗的对话
        Talk(SAY_AGGRO);
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
     * @brief AI更新函数,每个游戏帧调用
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 主要逻辑:
     * 1. 检查是否有有效目标,无目标则返回
     * 2. 更新事件调度器
     * 3. 如果正在施法则等待
     * 4. 处理事件队列:
     *    - 急速: 对自己施放,提高攻击速度
     *    - 致命伤口: 对自己施放(会传染给攻击者)
     *    - 翼龙猛击: 对自己施放,击退周围敌人
     *    - 法术反射: 英雄难度,对自己施放
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
                case EVENT_HASTE:
                    // 对自己施放急速增益
                    DoCast(me, SPELL_HASTE);
                    // 调度下一次急速,20-25秒后
                    events.ScheduleEvent(EVENT_HASTE, 20s, 25s);
                    break;
                case EVENT_MORTAL_WOUND:
                    // 对自己施放致命伤口(会被近战攻击者传染)
                    DoCast(me, SPELL_MORTAL_WOUND);
                    // 调度下一次致命伤口,10-20秒后
                    events.ScheduleEvent(EVENT_MORTAL_WOUND, 10s, 20s);
                    break;
                case EVENT_WING_BUFFET:
                    // 对自己施放翼龙猛击,击退周围敌人
                     DoCast(me, SPELL_WING_BUFFET);
                    // 调度下一次翼龙猛击,20-30秒后
                    events.ScheduleEvent(EVENT_WING_BUFFET, 20s, 30s);
                    break;
                case EVENT_SPELL_REFLECTION: // 仅英雄难度
                    // 对自己施放法术反射
                    DoCast(me, SPELL_REFLECT);
                    // 调度下一次法术反射,25-35秒后
                    events.ScheduleEvent(EVENT_SPELL_REFLECTION, 25s, 35s);
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
 * @brief 注册BOSS坦普卢斯的AI脚本
 *
 * 此函数由脚本系统在服务器启动时调用,用于注册BOSS的AI。
 * 使用RegisterBlackMorassCreatureAI宏将boss_temporus AI注册到脚本系统。
 *
 * @调用时机: 服务器启动时,脚本加载阶段
 */
void AddSC_boss_temporus()
{
    RegisterBlackMorassCreatureAI(boss_temporus);
}
