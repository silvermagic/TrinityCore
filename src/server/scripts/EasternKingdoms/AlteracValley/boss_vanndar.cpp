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
 * @file boss_vanndar.cpp
 * @brief 奥特兰克山谷 - 范达尔·雷矛首领AI脚本
 *
 * 本模块实现了奥特兰克山谷战场中联盟方首领范达尔·雷矛的AI行为逻辑。
 * 范达尔是一名战士型首领，拥有强大的近战和雷电技能。
 * 他的技能组合包括化身、雷霆一击和风暴之锤等强大技能。
 *
 * 主要功能：
 * - 化身增益技能
 * - AOE雷霆一击
 * - 风暴之锤单体伤害
 * - 战斗喊话系统
 * - 脱离战斗检测和重置机制
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 范达尔的文本ID枚举
 */
enum Yells
{
    YELL_AGGRO                                    = 0,  ///< 进入战斗时的喊话
    YELL_EVADE                                    = 1,  ///< 脱离战斗时的喊话
  //YELL_RESPAWN1                                 = -1810010, // 数据库中缺失
  //YELL_RESPAWN2                                 = -1810011, // 数据库中缺失
    YELL_RANDOM                                   = 2,  ///< 随机喊话
    YELL_SPELL                                    = 3,  ///< 施法喊话
};

/**
 * @brief 范达尔使用的法术ID枚举
 */
enum Spells
{
    SPELL_AVATAR                                  = 19135,  ///< 化身 - 增加攻击强度和攻击速度
    SPELL_THUNDERCLAP                             = 15588,  ///< 雷霆一击 - AOE伤害并降低攻击速度
    SPELL_STORMBOLT                               = 20685   ///< 风暴之锤 - 远程伤害技能（未确认）
};

/**
 * @struct boss_vanndar
 * @brief 范达尔·雷矛AI实现
 *
 * 该AI类实现了奥特兰克山谷联盟首领范达尔·雷矛的战斗逻辑。
 * 范达尔是一名战士型首领，使用化身、雷霆一击和风暴之锤等技能。
 * 他的技能组合具有强大的增益和AOE伤害能力。
 *
 * 注意：该AI使用TaskScheduler而非EventMap来管理技能调度。
 */
struct boss_vanndar : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_vanndar(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 取消所有调度任务。
     * 当生物脱离战斗时调用。
     */
    void Reset() override
    {
        _scheduler.CancelAll();
    }

    /**
     * @brief 进入战斗时的回调
     * @param who 进入战斗的目标（未使用）
     *
     * 当范达尔进入战斗时：
     * 1. 调度所有技能任务的初始施放时间
     * 2. 发送进入战斗喊话
     *
     * 技能调度：
     * - 化身：3秒，之后15-20秒循环
     * - 雷霆一击：4秒，之后5-15秒循环
     * - 风暴之锤：6秒，之后10-25秒循环
     * - 随机喊话：20-30秒循环
     * - 位置检查：5秒循环
     *
     * 注意：使用TaskScheduler的lambda表达式实现任务调度。
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _scheduler
            .Schedule(3s, [this](TaskContext task)
            {
                // 施放化身，增加攻击强度和攻击速度
                DoCastVictim(SPELL_AVATAR);
                task.Repeat(15s, 20s);
            })
            .Schedule(4s, [this](TaskContext task)
            {
                // 施放雷霆一击，AOE伤害并降低攻击速度
                DoCastVictim(SPELL_THUNDERCLAP);
                task.Repeat(5s, 15s);
            })
            .Schedule(6s, [this](TaskContext task)
            {
                // 施放风暴之锤，远程伤害
                DoCastVictim(SPELL_STORMBOLT);
                task.Repeat(10s, 25s);
            })
            .Schedule(20s, 30s, [this](TaskContext task)
            {
                // 发送随机战斗喊话
                Talk(YELL_RANDOM);
                task.Repeat(20s, 30s);
            })
            .Schedule(5s, [this](TaskContext task)
            {
                // 检查范达尔是否离开了出生点50码范围
                if (me->GetDistance2d(me->GetHomePosition().GetPositionX(), me->GetHomePosition().GetPositionY()) > 50)
                {
                    EnterEvadeMode();
                    Talk(YELL_EVADE);
                }
                task.Repeat();
            });

        Talk(YELL_AGGRO);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要AI更新循环，每帧调用一次：
     * 1. 检查是否有有效目标
     * 2. 更新任务调度器
     * 3. 如果没有任务在执行则进行近战攻击
     *
     * 性能注意：每帧都会调用此函数，应避免耗时操作。
     * 位置检查通过调度器每5秒执行一次，比每帧检查更高效。
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
    TaskScheduler _scheduler;  ///< 任务调度器，使用现代C++ lambda表达式管理技能施放
};

/**
 * @brief 注册范达尔AI脚本
 *
 * 该函数用于将范达尔的AI注册到脚本系统中，
 * 使游戏服务器能够加载和使用该AI。
 */
void AddSC_boss_vanndar()
{
    RegisterCreatureAI(boss_vanndar);
}
