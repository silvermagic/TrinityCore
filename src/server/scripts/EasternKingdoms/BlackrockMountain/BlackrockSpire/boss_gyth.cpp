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
 * @file boss_gyth.cpp
 * @brief 黑石尖塔副本BOSS：盖斯(Gyth)的AI脚本实现
 *
 * 盖斯是大酋长雷德·黑手(Rend Blackhand)的坐骑，一只巨大的龙兽。
 * 玩家需要先击败盖斯，才能挑战雷德·黑手。
 * 盖斯最初骑在雷德身上出现，生命值降至5%时会召唤雷德下车战斗。
 *
 * BOSS特点：
 * - 与雷德·黑手绑定的双BOSS战斗
 * - 使用腐蚀酸液、冰冻、火焰吐息等龙类技能
 * - 生命值低于5%时召唤雷德·黑手
 * - 由维克多·尼尔法里斯大帝的剧情事件触发
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义盖斯使用的所有法术ID
 */
enum Spells
{
    SPELL_REND_MOUNTS               = 16167,  // 雷德坐骑光环 - 改变模型显示为骑乘状态
    SPELL_CORROSIVE_ACID            = 16359,  // 腐蚀酸液 - 自身施法，对前方敌人造成自然伤害
    SPELL_FLAMEBREATH               = 16390,  // 火焰吐息 - 自身施法，对前方敌人造成火焰伤害
    SPELL_FREEZE                    = 16350,  // 冰冻 - 自身施法，对前方敌人造成冰霜伤害并减速
    SPELL_KNOCK_AWAY                = 10101,  // 击退 - 击退目标并降低仇恨
    SPELL_SUMMON_REND               = 16328   // 召唤雷德 - 在濒死时召唤雷德·黑手
};

/**
 * @brief 杂项数据枚举
 * 定义路径ID和其他常量
 */
enum Misc
{
    NEFARIUS_PATH_2                 = 1379671,  // 尼尔法里斯路径2
    NEFARIUS_PATH_3                 = 1379672,  // 尼尔法里斯路径3
    GYTH_PATH_1                     = 1379681,  // 盖斯路径1 - 进入竞技场的路径
};

/**
 * @brief 事件ID枚举
 * 定义战斗和剧情事件调度器使用的事件类型
 */
enum Events
{
    EVENT_CORROSIVE_ACID            = 1,        // 腐蚀酸液事件
    EVENT_FREEZE                    = 2,        // 冰冻事件
    EVENT_FLAME_BREATH              = 3,        // 火焰吐息事件
    EVENT_KNOCK_AWAY                = 4,        // 击退事件
    EVENT_SUMMONED_1                = 5,        // 召唤事件第一步 - 施加坐骑光环
    EVENT_SUMMONED_2                = 6         // 召唤事件第二步 - 沿路径移动
};

/**
 * @brief 盖斯BOSS AI结构体
 *
 * 继承自BossAI，实现盖斯的战斗和剧情AI逻辑。
 * 该AI控制BOSS的法术释放、剧情事件和雷德·黑手召唤机制。
 */
struct boss_gyth : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * 初始化BOSS AI，关联盖斯的副本数据
     */
    boss_gyth(Creature* creature) : BossAI(creature, DATA_GYTH)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     * 将雷德召唤标志重置为false
     */
    void Initialize()
    {
        SummonedRend = false;
    }

    bool SummonedRend;  ///< 是否已召唤雷德·黑手标志，防止重复召唤

    /**
     * @brief 重置AI状态
     *
     * 当BOSS脱离战斗或重置时调用。
     * 如果BOSS状态为进行中（异常情况），标记为完成并消失。
     *
     * 调用时机：BOSS脱离战斗、团灭重置、实例重置时
     */
    void Reset() override
    {
        Initialize();
        // 如果战斗状态异常（正在进行中），标记为完成并消失
        if (instance->GetBossState(DATA_GYTH) == IN_PROGRESS)
        {
            instance->SetBossState(DATA_GYTH, DONE);
            me->DespawnOrUnsummon();
        }
    }

    /**
     * @brief 进入战斗事件处理
     * @param who 进入战斗的目标单位
     *
     * 当BOSS进入战斗状态时调用。
     * 调用基类的进入战斗方法，并调度所有战斗法术的初始释放时间。
     *
     * 调用时机：BOSS被攻击或主动攻击玩家时
     * 性能注意事项：事件调度为轻量级操作，无性能影响
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        events.ScheduleEvent(EVENT_CORROSIVE_ACID, 8s, 16s);   // 8-16秒后首次施放腐蚀酸液
        events.ScheduleEvent(EVENT_FREEZE, 8s, 16s);           // 8-16秒后首次施放冰冻
        events.ScheduleEvent(EVENT_FLAME_BREATH, 8s, 16s);     // 8-16秒后首次施放火焰吐息
        events.ScheduleEvent(EVENT_KNOCK_AWAY, 12s, 18s);      // 12-18秒后首次施放击退
    }

    /**
     * @brief 死亡事件处理
     * @param killer 击杀者单位（当前未使用）
     *
     * 当BOSS死亡时，标记副本状态为完成。
     *
     * 调用时机：BOSS被击杀时
     */
    void JustDied(Unit* /*killer*/) override
    {
        instance->SetBossState(DATA_GYTH, DONE);
    }

    /**
     * @brief 设置数据事件处理
     * @param type 数据类型（当前未使用）
     * @param data 数据值
     *
     * 用于接收外部事件触发，主要由维克多·尼尔法里斯大帝的AI调用。
     * 当data为1时，开始盖斯的入场事件序列。
     *
     * 调用时机：由其他AI脚本通过SetData方法触发
     */
    void SetData(uint32 /*type*/, uint32 data) override
    {
        switch (data)
        {
            case 1:
                // 开始入场事件序列
                events.ScheduleEvent(EVENT_SUMMONED_1, 1s);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每个游戏循环周期调用一次，处理战斗和剧情逻辑。
     * 包括：
     * - 濒死时召唤雷德·黑手
     * - 非战斗状态下的事件处理（入场剧情）
     * - 战斗状态下的法术释放
     *
     * 调用时机：每个游戏Tick（约每50毫秒）
     * 性能注意事项：频繁调用，已优化处理逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 生命值低于5%且尚未召唤雷德时，召唤雷德·黑手并移除坐骑光环
        if (!SummonedRend && HealthBelowPct(5))
        {
            DoCast(me, SPELL_SUMMON_REND);
            me->RemoveAura(SPELL_REND_MOUNTS);
            SummonedRend = true;
        }

        // 非战斗状态下的处理（入场剧情）
        if (!UpdateVictim())
        {
            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_SUMMONED_1:
                        // 施加坐骑光环，显示为雷德骑乘状态
                        me->AddAura(SPELL_REND_MOUNTS, me);
                        // 开启竞技场大门
                        if (GameObject* portcullis = me->FindNearestGameObject(GO_DR_PORTCULLIS, 40.0f))
                            portcullis->UseDoorOrButton();
                        // 通知维克多·尼尔法里斯大帝继续剧情
                        if (Creature* victor = me->FindNearestCreature(NPC_LORD_VICTOR_NEFARIUS, 75.0f, true))
                            victor->AI()->SetData(1, 1);
                        events.ScheduleEvent(EVENT_SUMMONED_2, 2s);
                        break;
                    case EVENT_SUMMONED_2:
                        // 沿预定义路径进入竞技场
                        me->GetMotionMaster()->MovePath(GYTH_PATH_1, false);
                        break;
                    default:
                        break;
                }
            }
            return;
        }

        // 战斗状态下的处理
        events.Update(diff);

        // 如果正在施法，则不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CORROSIVE_ACID:
                    // 施放腐蚀酸液，对前方敌人造成自然伤害
                    DoCast(me, SPELL_CORROSIVE_ACID);
                    events.ScheduleEvent(EVENT_CORROSIVE_ACID, 10s, 16s);
                    break;
                case EVENT_FREEZE:
                    // 施放冰冻，对前方敌人造成冰霜伤害并减速
                    DoCast(me, SPELL_FREEZE);
                    events.ScheduleEvent(EVENT_FREEZE, 10s, 16s);
                    break;
                case EVENT_FLAME_BREATH:
                    // 施放火焰吐息，对前方敌人造成火焰伤害
                    DoCast(me, SPELL_FLAMEBREATH);
                    events.ScheduleEvent(EVENT_FLAME_BREATH, 10s, 16s);
                    break;
                case EVENT_KNOCK_AWAY:
                    // 施放击退，击退目标并降低仇恨
                    DoCastVictim(SPELL_KNOCK_AWAY);
                    events.ScheduleEvent(EVENT_KNOCK_AWAY, 14s, 20s);
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册盖斯脚本
 *
 * 将盖斯BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_gyth()
{
    RegisterBlackrockSpireCreatureAI(boss_gyth);
}
