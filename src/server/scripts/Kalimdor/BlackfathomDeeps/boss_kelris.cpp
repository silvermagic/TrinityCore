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
 * @file boss_kelris.cpp
 * @brief 黑暗深渊副本 - Boss Twilight Lord Kelris（暮光领主凯尔里斯）脚本
 *
 * Twilight Lord Kelris是黑暗深渊副本的第一个Boss，一名暮光之锤成员。
 * 战斗机制：
 * - 战斗前一直施放引导法术，进入战斗后取消
 * - 周期性施放精神冲击，对当前目标造成暗影伤害
 * - 随机使一名玩家睡眠，持续一段时间
 * - 睡眠技能是主要威胁，会移除玩家对战斗的参与
 *
 * 击败他是开启后续Boss战的前提条件。
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "blackfathom_deeps.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_MIND_BLAST             = 15587, ///< 精神冲击 - 对目标造成暗影伤害
    SPELL_SLEEP                  = 8399,  ///< 睡眠 - 使目标沉睡，无法行动
    SPELL_BLACKFATHOM_CHANNELING = 8734   ///< 黑暗深渊引导 - 非战斗时的视觉效果
};

/**
 * @brief 对话文本ID枚举
 */
enum Texts
{
    SAY_AGGRO    = 0,  ///< 进入战斗时的对话
    SAY_SLEEP    = 1,  ///< 施放睡眠时的对话
    SAY_DEATH    = 2   ///< 死亡时的对话
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_MIND_BLAST = 1,  ///< 精神冲击技能事件
    EVENT_SLEEP             ///< 睡眠技能事件
};

/**
 * @struct boss_kelris
 * @brief Boss Twilight Lord Kelris AI实现
 *
 * 继承自BossAI基类，实现暮光领主的战斗逻辑。
 * 战斗策略：
 * - 非战斗状态下持续施放引导法术（视觉效果）
 * - 开战后立即取消引导法术
 * - 周期性施放精神冲击造成暗影伤害
 * - 随机睡眠玩家，削弱团队战斗力
 * - 睡眠目标不是仇恨列表第一位，而是随机选择
 */
struct boss_kelris : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    boss_kelris(Creature* creature) : BossAI(creature, DATA_KELRIS) { }

    /**
     * @brief 重置Boss状态
     *
     * 当战斗重置时调用（如脱离战斗、团队灭团）。
     * 执行：
     * 1. 调用父类的_Reset()处理标准的Boss重置逻辑
     * 2. 对自己施放引导法术，恢复非战斗状态的视觉效果
     */
    void Reset() override
    {
        _Reset();
        DoCastSelf(SPELL_BLACKFATHOM_CHANNELING);
    }

    /**
     * @brief 返回初始位置时调用
     *
     * 当Boss脱离战斗并回到刷新点时触发。
     * 执行：
     * 1. 调用父类的_JustReachedHome()处理标准的返回逻辑
     * 2. 重新施放引导法术，恢复视觉效果
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        DoCastSelf(SPELL_BLACKFATHOM_CHANNELING);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 当Boss被攻击或主动攻击玩家时触发。
     * 执行：
     * 1. 调用父类的JustEngagedWith()处理标准的进入战斗逻辑
     * 2. 喊出战斗开始对话
     * 3. 移除引导法术的视觉效果
     * 4. 安排首次精神冲击，2-5秒后执行
     * 5. 安排首次睡眠，9-12秒后执行
     *
     * @note 睡眠技能的延迟较长，给玩家准备时间
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        me->RemoveAurasDueToSpell(SPELL_BLACKFATHOM_CHANNELING);
        events.ScheduleEvent(EVENT_MIND_BLAST, 2s, 5s);
        events.ScheduleEvent(EVENT_SLEEP, 9s, 12s);
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者（未使用）
     *
     * 执行：
     * 1. 喊出死亡对话
     * 2. 调用父类的_JustDied()处理标准的死亡逻辑
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);
        _JustDied();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主循环处理战斗中的技能施放：
     * 1. 检查是否有战斗目标，无目标则返回
     * 2. 更新事件定时器
     * 3. 如果正在施法，则跳过本次更新（防止打断施法）
     * 4. 执行到期的事件：
     *    - 精神冲击：对当前目标施放暗影伤害，安排下一次施放（7-9秒）
     *    - 睡眠：随机选择一名玩家（100码内）施放睡眠效果，安排下一次施放（15-20秒）
     *      睡眠目标必须是存活的玩家，不包括当前坦克（SelectTargetMethod::Random的默认行为）
     * 5. 如果没有事件需要处理且未在施法，进行近战攻击
     *
     * 睡眠技能是战斗的关键机制，会显著影响团队的输出和治疗能力。
     * 玩家需要准备解除睡眠的技能或等待自然醒来。
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_MIND_BLAST:
                    DoCastVictim(SPELL_MIND_BLAST);
                    events.ScheduleEvent(EVENT_MIND_BLAST, 7s, 9s);
                    break;
                case EVENT_SLEEP:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    {
                        Talk(SAY_SLEEP);
                        DoCast(target, SPELL_SLEEP);
                    }
                    events.ScheduleEvent(EVENT_SLEEP, 15s, 20s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册Boss Twilight Lord Kelris脚本
 *
 * 使用宏注册Boss AI，使其在副本中生效。
 */
void AddSC_boss_kelris()
{
    RegisterBlackfathomDeepsCreatureAI(boss_kelris);
}
