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
 * @file boss_gelihast.cpp
 * @brief 黑暗深渊副本 - Boss Gelihast（格里哈斯特）脚本
 *
 * Gelihast是黑暗深渊副本的第二个Boss，一只鱼人首领。
 * 战斗机制：
 * - 周期性对当前目标投掷渔网，使其定身
 * - 渔网技能会打断玩家的施法并阻止移动
 * - 主要威胁在于定身效果，需要队友及时解救
 *
 * 击败后玩家可以激活Gelihast的神殿祭坛，获得增益效果。
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "blackfathom_deeps.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_NET         = 6533   ///< 投网 - 使目标定身，无法移动和施法
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_THROW_NET   = 1      ///< 投网技能事件
};

/**
 * @struct boss_gelihast
 * @brief Boss Gelihast AI实现
 *
 * 继承自BossAI基类，实现鱼人首领的战斗逻辑。
 * 战斗策略：
 * - 开战后立即准备投网技能
 * - 定期对坦克施放投网，需要其他玩家制造仇恨或等待效果消失
 * - 投网期间Boss会继续攻击，治疗需要注意
 */
struct boss_gelihast : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    boss_gelihast(Creature* creature) : BossAI(creature, DATA_GELIHAST) { }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 当Boss被攻击或主动攻击玩家时触发。
     * 执行：
     * 1. 调用父类的JustEngagedWith()处理标准的进入战斗逻辑
     * 2. 安排首次投网施放，2-4秒后执行
     *
     * @note 投网的快速首次施放要求坦克保持高度警惕
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_THROW_NET, 2s, 4s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主循环处理战斗中的技能施放：
     * 1. 检查是否有战斗目标，无目标则返回
     * 2. 更新事件定时器
     * 3. 执行到期的事件：
     *    - 投网：对当前目标施放定身效果，安排下一次施放（4-7秒）
     * 4. 如果没有事件需要处理，进行近战攻击
     *
     * 投网的冷却时间较短，战斗中会频繁施放。
     * 玩家需要准备解除定身的技能或物品。
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            if (eventId == EVENT_THROW_NET)
            {
                DoCastVictim(SPELL_NET);
                events.ScheduleEvent(EVENT_THROW_NET, 4s, 7s);
            }
        }

        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册Boss Gelihast脚本
 *
 * 使用宏注册Boss AI，使其在副本中生效。
 */
void AddSC_boss_gelihast()
{
    RegisterBlackfathomDeepsCreatureAI(boss_gelihast);
}
