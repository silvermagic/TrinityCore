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
 * @file boss_galvangar.cpp
 * @brief 奥特兰克山谷 - 加尔范上尉首领AI脚本
 *
 * 本模块实现了奥特兰克山谷战场中部落方上尉加尔范的AI行为逻辑。
 * 加尔范是一名战士型首领，拥有强大的近战技能组合。
 * 他的技能包括顺劈斩、恐惧喊叫、旋风斩和致死打击。
 *
 * 主要功能：
 * - 多重近战攻击技能循环
 * - AOE恐惧和旋风斩能力
 * - 致死打击造成持续伤害
 * - 脱离战斗检测和重置机制
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 加尔范使用的法术ID枚举
 */
enum Spells
{
    SPELL_CLEAVE                                  = 15284,  ///< 顺劈斩 - 攻击前方多个目标
    SPELL_FRIGHTENING_SHOUT                       = 19134,  ///< 恐惧喊叫 - AOE恐惧效果
    SPELL_WHIRLWIND1                              = 15589,  ///< 旋风斩 - 第一种旋风斩
    SPELL_WHIRLWIND2                              = 13736,  ///< 旋风斩 - 第二种旋风斩
    SPELL_MORTAL_STRIKE                           = 16856   ///< 致死打击 - 造成伤害并降低治疗效果
};

/**
 * @brief 加尔范的文本ID枚举
 */
enum Texts
{
    SAY_AGGRO                                    = 0,  ///< 进入战斗时的喊话
    SAY_EVADE                                    = 1,  ///< 脱离战斗时的喊话
    SAY_BUFF                                     = 2   ///< 增益喊话
};

/**
 * @brief 事件ID枚举，用于调度技能施放
 */
enum Events
{
    EVENT_CLEAVE = 1,            ///< 顺劈斩事件
    EVENT_FRIGHTENING_SHOUT,     ///< 恐惧喊叫事件
    EVENT_WHIRLWIND1,            ///< 旋风斩1事件
    EVENT_WHIRLWIND2,            ///< 旋风斩2事件
    EVENT_MORTAL_STRIKE          ///< 致死打击事件
};

/**
 * @brief 动作ID枚举
 */
enum Action
{
    ACTION_BUFF_YELL                              = -30001  ///< 增益喊话动作ID（与战场共享）
};

/**
 * @struct boss_galvangar
 * @brief 加尔范上尉AI实现
 *
 * 该AI类实现了奥特兰克山谷部落上尉加尔范的战斗逻辑。
 * 加尔范是一名战士型首领，使用顺劈斩、恐惧喊叫、旋风斩和致死打击等技能。
 * 他的技能组合具有强大的AOE伤害和控场能力。
 */
struct boss_galvangar : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_galvangar(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 重置所有事件调度器。
     * 当生物脱离战斗时调用。
     */
    void Reset() override
    {
        events.Reset();
    }

    /**
     * @brief 进入战斗时的回调
     * @param who 进入战斗的目标（未使用）
     *
     * 当加尔范进入战斗时：
     * 1. 发送进入战斗喊话
     * 2. 调度所有技能事件的初始施放时间
     *
     * 技能调度：
     * - 顺劈斩：1-9秒
     * - 恐惧喊叫：2-19秒
     * - 旋风斩1：1-13秒
     * - 旋风斩2：5-20秒
     * - 致死打击：5-20秒
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_AGGRO);
        events.ScheduleEvent(EVENT_CLEAVE, 1s, 9s);
        events.ScheduleEvent(EVENT_FRIGHTENING_SHOUT, 2s, 19s);
        events.ScheduleEvent(EVENT_WHIRLWIND1, 1s, 13s);
        events.ScheduleEvent(EVENT_WHIRLWIND2, 5s, 20s);
        events.ScheduleEvent(EVENT_MORTAL_STRIKE, 5s, 20s);
    }

    /**
     * @brief 执行动作的回调
     * @param actionId 动作ID
     *
     * 当收到动作指令时执行相应操作。
     * 目前处理战场传来的增益喊话动作。
     */
    void DoAction(int32 actionId) override
    {
        if (actionId == ACTION_BUFF_YELL)
            Talk(SAY_BUFF);
    }

    /**
     * @brief 检查是否在房间内
     * @return 如果在房间内返回true，否则返回false
     *
     * 检查加尔范是否离开了出生点50码范围。
     * 如果离开范围，则进入逃避模式并发送脱离战斗喊话。
     * 该函数在每次UpdateAI时都会被调用。
     */
    bool CheckInRoom() override
    {
        if (me->GetDistance2d(me->GetHomePosition().GetPositionX(), me->GetHomePosition().GetPositionY()) > 50)
        {
            EnterEvadeMode();
            Talk(SAY_EVADE);
            return false;
        }

        return true;
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要AI更新循环，每帧调用一次：
     * 1. 检查是否有有效目标和是否在房间内
     * 2. 更新事件调度器
     * 3. 如果正在施法则等待
     * 4. 处理事件队列中的所有待执行事件
     * 5. 根据事件类型施放相应技能
     * 6. 如果没有施法则进行近战攻击
     *
     * 性能注意：每帧都会调用此函数，应避免耗时操作。
     * CheckInRoom()每帧都会检查位置，可能有轻微性能开销。
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || !CheckInRoom())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩，攻击前方多个目标
                    DoCastVictim(SPELL_CLEAVE);
                    events.ScheduleEvent(EVENT_CLEAVE, 10s, 16s);
                    break;
                case EVENT_FRIGHTENING_SHOUT:
                    // 施放恐惧喊叫，使周围敌人恐惧
                    DoCastVictim(SPELL_FRIGHTENING_SHOUT);
                    events.ScheduleEvent(EVENT_FRIGHTENING_SHOUT, 10s, 15s);
                    break;
                case EVENT_WHIRLWIND1:
                    // 对当前目标施放第一种旋风斩
                    DoCastVictim(SPELL_WHIRLWIND1);
                    events.ScheduleEvent(EVENT_WHIRLWIND1, 6s, 10s);
                    break;
                case EVENT_WHIRLWIND2:
                    // 对当前目标施放第二种旋风斩
                    DoCastVictim(SPELL_WHIRLWIND2);
                    events.ScheduleEvent(EVENT_WHIRLWIND2, 10s, 25s);
                    break;
                case EVENT_MORTAL_STRIKE:
                    // 对当前目标施放致死打击，造成伤害并降低治疗效果
                    DoCastVictim(SPELL_MORTAL_STRIKE);
                    events.ScheduleEvent(EVENT_MORTAL_STRIKE, 10s, 30s);
                    break;
                default:
                    break;
            }

            // 如果施法状态改变，立即返回避免重复施法
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap events;  ///< 事件调度器，管理技能施放计时
};

/**
 * @brief 注册加尔范AI脚本
 *
 * 该函数用于将加尔范的AI注册到脚本系统中，
 * 使游戏服务器能够加载和使用该AI。
 */
void AddSC_boss_galvangar()
{
    RegisterCreatureAI(boss_galvangar);
}
