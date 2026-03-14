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
 * @file boss_drekthar.cpp
 * @brief 奥特兰克山谷 - 德雷克塔尔首领AI脚本
 *
 * 本模块实现了奥特兰克山谷战场中部落方首领德雷克塔尔的AI行为逻辑。
 * 德雷克塔尔是一名萨满型首领，拥有多种近战和风系技能。
 * 他的技能组合具有高伤害输出和控场能力。
 *
 * 主要功能：
 * - 双重旋风斩技能循环
 * - 击倒和狂暴机制
 * - 战斗喊话系统
 * - 脱离战斗检测和重置机制
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 德雷克塔尔使用的法术ID枚举
 */
enum Spells
{
    SPELL_WHIRLWIND                               = 15589,  ///< 旋风斩 - 第一种旋风斩技能
    SPELL_WHIRLWIND2                              = 13736,  ///< 旋风斩 - 第二种旋风斩技能
    SPELL_KNOCKDOWN                               = 19128,  ///< 击倒 - 使目标倒地
    SPELL_FRENZY                                  = 8269,   ///< 狂暴 - 增加攻击速度
    SPELL_SWEEPING_STRIKES                        = 18765,  ///< 横扫攻击（未确认）
    SPELL_CLEAVE                                  = 20677,  ///< 顺劈斩（未确认）
    SPELL_WINDFURY                                = 35886,  ///< 风怒（未确认）
    SPELL_STORMPIKE                               = 51876   ///< 风暴pike（未确认）
};

/**
 * @brief 德雷克塔尔的文本ID枚举
 */
enum Texts
{
    SAY_AGGRO                                    = 0,  ///< 进入战斗时的喊话
    SAY_EVADE                                    = 1,  ///< 脱离战斗时的喊话
    SAY_RESPAWN                                  = 2,  ///< 重生时的喊话
    SAY_RANDOM                                   = 3   ///< 随机喊话
};

/**
 * @brief 事件ID枚举，用于调度技能施放
 */
enum Events
{
    EVENT_WHIRLWIND = 1,      ///< 旋风斩1事件
    EVENT_WHIRLWIND2,         ///< 旋风斩2事件
    EVENT_KNOCKDOWN,          ///< 击倒事件
    EVENT_FRENZY,             ///< 狂暴事件
    EVENT_RANDOM_YELL         ///< 随机喊话事件
};

/**
 * @struct boss_drekthar
 * @brief 德雷克塔尔AI实现
 *
 * 该AI类实现了奥特兰克山谷部落首领德雷克塔尔的战斗逻辑。
 * 德雷克塔尔是一名萨满型首领，使用旋风斩、击倒和狂暴等技能。
 * 他的双重旋风斩技能使其具有极高的AOE伤害输出。
 */
struct boss_drekthar : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_drekthar(Creature* creature) : ScriptedAI(creature) { }

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
     * 当德雷克塔尔进入战斗时：
     * 1. 发送进入战斗喊话
     * 2. 调度所有技能事件的初始施放时间
     *
     * 技能调度：
     * - 旋风斩1：1-20秒
     * - 旋风斩2：1-20秒
     * - 击倒：12秒
     * - 狂暴：6秒
     * - 随机喊话：20-30秒
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_AGGRO);
        events.ScheduleEvent(EVENT_WHIRLWIND, 1s, 20s);
        events.ScheduleEvent(EVENT_WHIRLWIND2, 1s, 20s);
        events.ScheduleEvent(EVENT_KNOCKDOWN, 12s);
        events.ScheduleEvent(EVENT_FRENZY, 6s);
        events.ScheduleEvent(EVENT_RANDOM_YELL, 20s, 30s);
    }

    /**
     * @brief 生物出现时的回调
     *
     * 当德雷克塔尔重新出现时（如重生），重置状态并发送重生喊话。
     */
    void JustAppeared() override
    {
        Reset();
        Talk(SAY_RESPAWN);
    }

    /**
     * @brief 检查是否在房间内
     * @return 如果在房间内返回true，否则返回false
     *
     * 检查德雷克塔尔是否离开了出生点50码范围。
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
                case EVENT_WHIRLWIND:
                    // 对当前目标施放第一种旋风斩
                    DoCastVictim(SPELL_WHIRLWIND);
                    events.ScheduleEvent(EVENT_WHIRLWIND, 8s, 18s);
                    break;
                case EVENT_WHIRLWIND2:
                    // 对当前目标施放第二种旋风斩
                    DoCastVictim(SPELL_WHIRLWIND2);
                    events.ScheduleEvent(EVENT_WHIRLWIND2, 7s, 25s);
                    break;
                case EVENT_KNOCKDOWN:
                    // 对当前目标施放击倒，使其倒地
                    DoCastVictim(SPELL_KNOCKDOWN);
                    events.ScheduleEvent(EVENT_KNOCKDOWN, 10s, 15s);
                    break;
                case EVENT_FRENZY:
                    // 施放狂暴，增加攻击速度
                    DoCastVictim(SPELL_FRENZY);
                    events.ScheduleEvent(EVENT_FRENZY, 20s, 30s);
                    break;
                case EVENT_RANDOM_YELL:
                    // 发送随机战斗喊话
                    Talk(SAY_RANDOM);
                    events.ScheduleEvent(EVENT_RANDOM_YELL, 20s, 30s);
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
 * @brief 注册德雷克塔尔AI脚本
 *
 * 该函数用于将德雷克塔尔的AI注册到脚本系统中，
 * 使游戏服务器能够加载和使用该AI。
 */
void AddSC_boss_drekthar()
{
    RegisterCreatureAI(boss_drekthar);
}
