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
 * @file boss_jandice_barov.cpp
 * @brief 通灵学院BOSS詹迪斯·巴罗夫战斗脚本
 *
 * 本模块实现了通灵学院BOSS詹迪斯·巴罗夫的战斗AI：
 * - 詹迪斯·巴罗夫（巴罗夫家族成员之一）
 * - 幻象分身机制
 * - 血之诅咒技能
 *
 * 战斗机制：
 * 1. 血之诅咒：对当前目标施放，降低治疗效果
 * 2. 幻象分身：召唤幻象分身迷惑玩家，本体隐身
 * 3. 玩家需要在幻象中找到真正的詹迪斯
 *
 * 特殊机制：
 * - 施放幻象时BOSS会隐身并消除99%威胁值
 * - 幻象分身会攻击随机目标
 * - 死亡后掉落《詹迪斯的日记》
 *
 * 完成度：100%
 */

#include "ScriptMgr.h"
#include "scholomance.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义詹迪斯·巴罗夫使用的所有法术ID
 */
enum Spells
{
    SPELL_CURSE_OF_BLOOD        = 24673,  // 血之诅咒 - 降低治疗效果
    SPELL_ILLUSION              = 17773,  // 幻象 - 召唤幻象分身
    SPELL_DROP_JOURNAL          = 26096   // 掉落日记 - 死亡后掉落《詹迪斯的日记》
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_CURSE_OF_BLOOD = 1,   // 血之诅咒事件
    EVENT_ILLUSION,             // 幻象事件
    EVENT_CLEAVE,               // 顺劈斩事件（未使用）
    EVENT_SET_VISIBILITY        // 设置可见性事件
};

/**
 * @brief 詹迪斯·巴罗夫脚本类
 *
 * 实现詹迪斯的战斗AI，包括：
 * - 血之诅咒技能
 * - 幻象分身机制（隐身+召唤分身）
 * - 死亡后掉落日记
 */
class boss_jandice_barov : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称
     */
    boss_jandice_barov() : CreatureScript("boss_jandice_barov") { }

    /**
     * @brief 詹迪斯·巴罗夫AI结构体
     *
     * 实现詹迪斯的战斗AI逻辑，继承自ScriptedAI
     */
    struct boss_jandicebarovAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_jandicebarovAI(Creature* creature) : ScriptedAI(creature), Summons(me) { }

        /**
         * @brief 重置AI状态
         *
         * 当战斗重置时调用：
         * - 重置事件计时器
         * - 清除所有召唤的幻象
         *
         * 调用时机：战斗重置或BOSS脱离战斗时
         */
        void Reset() override
        {
            events.Reset();
            Summons.DespawnAll();
        }

        /**
         * @brief 召唤生物事件
         * @param summoned 被召唤的生物
         *
         * 当幻象被召唤时：
         * - 让幻象攻击随机目标
         * - 将幻象添加到召唤列表
         *
         * 调用时机：施放幻象法术后召唤出幻象时
         */
        void JustSummoned(Creature* summoned) override
        {
            // 幻象应该攻击随机目标
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                summoned->AI()->AttackStart(target);

            Summons.Summon(summoned);
        }

        /**
         * @brief 进入战斗事件
         * @param who 进入战斗的目标
         *
         * 当詹迪斯进入战斗时：
         * - 安排血之诅咒技能（15秒后）
         * - 安排幻象技能（30秒后）
         *
         * 调用时机：BOSS进入战斗时
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            events.ScheduleEvent(EVENT_CURSE_OF_BLOOD, 15s);
            events.ScheduleEvent(EVENT_ILLUSION, 30s);
        }

        /**
         * @brief 死亡事件
         * @param killer 击杀者
         *
         * 当詹迪斯死亡时：
         * - 清除所有召唤的幻象
         * - 施放掉落日记法术
         *
         * 调用时机：BOSS被击杀时
         */
        void JustDied(Unit* /*killer*/) override
        {
            Summons.DespawnAll();
            DoCastSelf(SPELL_DROP_JOURNAL, true);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 每帧调用，处理战斗逻辑：
         * - 检查是否有战斗目标
         * - 更新事件计时器
         * - 检查施法状态
         * - 执行技能事件（血之诅咒、幻象、恢复可见性）
         * - 进行近战攻击
         *
         * 调用时机：每帧由核心代码调用
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())  // 没有战斗目标则返回
                return;

            events.Update(diff);  // 更新事件计时器

            if (me->HasUnitState(UNIT_STATE_CASTING))  // 正在施法则等待
                return;

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CURSE_OF_BLOOD:
                        // 对当前目标施放血之诅咒
                        DoCastVictim(SPELL_CURSE_OF_BLOOD);
                        events.ScheduleEvent(EVENT_CURSE_OF_BLOOD, 30s);  // 30秒后再次施放
                        break;
                    case EVENT_ILLUSION:
                        // 施放幻象法术
                        DoCast(SPELL_ILLUSION);
                        // 设置为不可交互状态
                        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        // 切换为隐形模型（模型ID 11686）
                        me->SetDisplayId(11686);
                        // 减少99%的威胁值，让BOSS"消失"在仇恨列表中
                        ModifyThreatByPercent(me->GetVictim(), -99);
                        events.ScheduleEvent(EVENT_SET_VISIBILITY, 3s);  // 3秒后恢复可见性
                        events.ScheduleEvent(EVENT_ILLUSION, 25s);       // 25秒后再次施放幻象
                        break;
                    case EVENT_SET_VISIBILITY:
                        // 恢复可见性和交互能力
                        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        // 恢复为詹迪斯模型（模型ID 11073）
                        me->SetDisplayId(11073);
                        break;
                    default:
                        break;
                }

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }

            DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
        }

    private:
        EventMap events;        ///< 事件映射表，管理技能冷却
        SummonList Summons;     ///< 召唤列表，管理所有召唤的幻象
    };

    /**
     * @brief 获取AI
     * @param creature 生物对象指针
     * @return AI对象指针
     *
     * 创建并返回詹迪斯AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetScholomanceAI<boss_jandicebarovAI>(creature);
    }
};

void AddSC_boss_jandicebarov()
{
    new boss_jandice_barov();
}
