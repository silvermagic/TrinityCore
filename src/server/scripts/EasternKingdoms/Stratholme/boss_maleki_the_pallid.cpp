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
 * @file    boss_maleki_the_pallid.cpp
 * @brief   斯坦索姆副本 - 苍白的玛勒基Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本苍白的玛勒基的战斗逻辑:
 *          - 使用冰霜和暗影法术的亡灵Boss
 *          - 寒冰箭、吸取生命、法力抽取、冰霜之墓等技能
 *          - 斯坦索姆重要的亡灵Boss之一
 *
 * @note 脚本完成度: 100%
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 法术ID枚举
 * 定义苍白的玛勒基使用的所有法术技能ID
 */
enum Spells
{
    SPELL_FROSTBOLT     = 17503,  // 寒冰箭 - 冰霜伤害法术
    SPELL_DRAINLIFE     = 20743,  // 吸取生命 - 持续伤害并治疗自己
    SPELL_DRAIN_MANA    = 17243,  // 法力抽取 - 抽取目标法力(未使用)
    SPELL_ICETOMB       = 16869   // 冰霜之墓 - 将目标冻在冰块中
};

/**
 * @brief 事件ID枚举
 * 用于Boss AI事件调度系统
 */
enum MalekiEvents
{
    EVENT_FROSTBOLT     = 1,  // 寒冰箭事件
    EVENT_DRAINLIFE     = 2,  // 吸取生命事件
    EVENT_DRAIN_MANA    = 3,  // 法力抽取事件(未使用)
    EVENT_ICETOMB       = 4   // 冰霜之墓事件
};

/**
 * @class boss_maleki_the_pallid
 * @brief 苍白的玛勒基Boss脚本类
 *
 * @details 实现苍白的玛勒基的脚本注册和AI创建
 */
class boss_maleki_the_pallid : public CreatureScript
{
public:
    boss_maleki_the_pallid() : CreatureScript("boss_maleki_the_pallid") { }

    /**
     * @struct boss_maleki_the_pallidAI
     * @brief 苍白的玛勒基Boss AI
     *
     * @details 实现苍白的玛勒基的战斗逻辑:
     *          - 使用冰霜和暗影法术
     *          - 主要技能包括寒冰箭、冰霜之墓、吸取生命
     *          - 与副本实例脚本交互
     */
    struct boss_maleki_the_pallidAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_maleki_the_pallidAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = me->GetInstanceScript();
        }

        /**
         * @brief 重置Boss状态
         *
         * @details 在战斗结束或重置时调用:
         *          - 重置所有事件计时器
         */
        void Reset() override
        {
            _events.Reset();
        }

        /**
         * @brief 进入战斗
         * @param who 进入战斗的目标(未使用)
         *
         * @details 在Boss进入战斗时调用:
         *          - 初始化技能事件调度:
         *            * 寒冰箭: 1秒后首次施放
         *            * 冰霜之墓: 16秒后首次施放
         *            * 吸取生命: 31秒后首次施放
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            _events.ScheduleEvent(EVENT_FROSTBOLT, 1s);
            _events.ScheduleEvent(EVENT_ICETOMB, 16s);
            _events.ScheduleEvent(EVENT_DRAINLIFE, 31s);
        }

        /**
         * @brief Boss死亡处理
         * @param killer 击杀者(未使用)
         *
         * @details Boss死亡时调用:
         *          - 设置副本状态为进行中
         *          - 可能触发后续事件
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(TYPE_PALLID, IN_PROGRESS);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有有效目标
         *          2. 更新事件计时器
         *          3. 处理事件队列,施放对应技能
         *          4. 执行近战攻击
         *
         * @par 技能循环:
         *          - 寒冰箭: 3.5秒间隔, 90%概率施放
         *          - 冰霜之墓: 28秒间隔, 65%概率施放
         *          - 吸取生命: 31秒间隔, 55%概率施放
         *
         * @note 当Boss正在施法时跳过事件处理,避免打断施法
         */
        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            // 没有目标则返回
            if (!UpdateVictim())
                return;

            _events.Update(diff);

            // 如果正在施法,等待施法完成
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // 处理事件队列
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_FROSTBOLT:
                        // 90%概率施放寒冰箭
                        if (rand32() % 90)
                            DoCastVictim(SPELL_FROSTBOLT);
                        _events.ScheduleEvent(EVENT_FROSTBOLT, 3500ms);
                        break;
                    case EVENT_ICETOMB:
                        // 65%概率施放冰霜之墓
                        if (rand32() % 65)
                            DoCastVictim(SPELL_ICETOMB);
                        _events.ScheduleEvent(EVENT_ICETOMB, 28s);
                        break;
                    case EVENT_DRAINLIFE:
                        // 55%概率施放吸取生命
                        if (rand32() % 55)
                            DoCastVictim(SPELL_DRAINLIFE);
                        _events.ScheduleEvent(EVENT_DRAINLIFE, 31s);
                        break;
                    default:
                        break;
                }
            }

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }

    private:
        EventMap _events;               // 事件调度映射表
        InstanceScript* instance;       // 副本实例脚本指针
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 创建的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_maleki_the_pallidAI>(creature);
    }
};

/**
 * @brief 注册脚本
 *
 * @details 将苍白的玛勒基Boss脚本注册到脚本系统
 */
void AddSC_boss_maleki_the_pallid()
{
    new boss_maleki_the_pallid();
}
