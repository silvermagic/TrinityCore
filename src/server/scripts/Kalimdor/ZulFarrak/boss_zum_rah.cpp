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
 * @file boss_zum_rah.cpp
 * @brief 祖尔法拉克副本Boss - 祖尔拉姆(Zum'rah) AI脚本
 *
 * 实现祖尔拉姆的战斗AI,包括:
 * - 暗影箭和暗影箭齐射的周期性施放
 * - 血量触发机制:在80%和40%血量时施放守护结界
 * - 低血量(30%)时自我治疗
 * - 区域触发器激活Boss(从友方变为敌方)
 *
 * Boss位置:祖尔法拉克墓地区域
 * 难度:中等,主要威胁是暗影伤害和治疗能力
 */

/*
Name: Boss_Zum_Rah
Category: Tanaris, ZulFarrak
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "zulfarrak.h"

/**
 * @enum Says
 * @brief Boss对话文本ID
 */
enum Says
{
    SAY_SANCT_INVADE    = 0,  ///< "你们敢入侵我的圣地!"
    SAY_WARD            = 1,  ///< "祖尔拉姆的守护!"
    SAY_KILL            = 2   ///< 击杀玩家时的喊话
};

/**
 * @enum Spells
 * @brief Boss使用的法术ID
 */
enum Spells
{
    SPELL_SHADOW_BOLT               = 12739,  ///< 暗影箭 - 单体暗影伤害
    SPELL_SHADOWBOLT_VOLLEY         = 15245,  ///< 暗影箭齐射 - AOE暗影伤害
    SPELL_WARD_OF_ZUM_RAH           = 11086,  ///< 祖尔拉姆守护 - 召唤墓穴僵尸
    SPELL_HEALING_WAVE              = 12491   ///< 治疗波 - 自我治疗
};

/**
 * @enum Events
 * @brief Boss事件ID,用于事件调度器
 */
enum Events
{
    EVENT_SHADOW_BOLT           = 1,  ///< 暗影箭事件
    EVENT_SHADOWBOLT_VOLLEY     = 2,  ///< 暗影箭齐射事件
    EVENT_WARD_OF_ZUM_RAH       = 3,  ///< 祖尔拉姆守护事件
    EVENT_HEALING_WAVE          = 4   ///< 治疗波事件
};

/**
 * @class boss_zum_rah
 * @brief 祖尔拉姆Boss脚本类
 *
 * 实现祖尔拉姆的完整战斗逻辑
 */
class boss_zum_rah : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"boss_zum_rah"
     */
    boss_zum_rah() : CreatureScript("boss_zum_rah") { }

    /**
     * @class boss_zum_rahAI
     * @brief 祖尔拉姆的AI实现
     *
     * 继承自BossAI,使用事件调度器管理法术施放
     */
    struct boss_zum_rahAI : public BossAI
    {
        /**
         * @brief 构造函数,初始化Boss状态
         * @param creature Boss生物指针
         */
        boss_zum_rahAI(Creature* creature) : BossAI(creature, DATA_ZUM_RAH)
        {
            Initialize();
        }

        /**
         * @brief 初始化状态标志
         *
         * 重置所有血量触发的法术标志
         * 确保每次战斗这些法术都能正确触发
         */
        void Initialize()
        {
            _ward80 = false;  ///< 80%血量时是否已施放守护
            _ward40 = false;  ///< 40%血量时是否已施放守护
            _heal30 = false;  ///< 30%血量时是否已治疗
        }

        /**
         * @brief 重置Boss状态
         *
         * 将Boss设置为友方阵营,直到被区域触发器激活
         * 重置所有状态标志
         */
        void Reset() override
        {
            me->SetFaction(FACTION_FRIENDLY); // 区域触发器会设置阵营为敌人
            Initialize();
        }

        /**
         * @brief 进入战斗回调
         * @param who 攻击者(未使用)
         *
         * 喊话并安排初始法术事件
         *
         * 调用时机:Boss首次被玩家攻击时
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_SANCT_INVADE);
            events.ScheduleEvent(EVENT_SHADOW_BOLT, 1s);           // 1秒后施放暗影箭
            events.ScheduleEvent(EVENT_SHADOWBOLT_VOLLEY, 10s);    // 10秒后施放暗影箭齐射
        }

        /**
         * @brief Boss死亡回调
         * @param killer 击杀者(未使用)
         *
         * 标记Boss为已击杀状态,更新副本进度
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(DATA_ZUM_RAH, DONE);
        }

        /**
         * @brief 击杀玩家回调
         * @param victim 被击杀的玩家(未使用)
         *
         * Boss击杀玩家时喊话
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_KILL);
        }

        /**
         * @brief 主更新函数,每帧调用
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 处理法术施放和血量触发机制
         *
         * 战斗逻辑:
         * 1. 周期性施放暗影箭(每4秒)
         * 2. 周期性施放暗影箭齐射(每9秒)
         * 3. 血量降至80%时施放守护并喊话
         * 4. 血量降至40%时再次施放守护
         * 5. 血量降至30%时自我治疗
         *
         * 性能注意:
         * - 使用事件调度器优化法术计时
         * - 血量检查每帧执行,但标志位确保只触发一次
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            events.Update(diff);

            // 处理事件队列中的所有到期事件
            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_SHADOW_BOLT:
                        DoCastVictim(SPELL_SHADOW_BOLT);
                        events.ScheduleEvent(EVENT_SHADOW_BOLT, 4s);  // 4秒冷却
                        break;
                    case EVENT_WARD_OF_ZUM_RAH:
                        DoCast(me,SPELL_WARD_OF_ZUM_RAH);  // 自我施放守护
                        break;
                    case EVENT_HEALING_WAVE:
                        DoCast(me,SPELL_HEALING_WAVE);  // 自我治疗
                        break;
                    case EVENT_SHADOWBOLT_VOLLEY:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            DoCast(target, SPELL_SHADOWBOLT_VOLLEY);
                        events.ScheduleEvent(EVENT_SHADOWBOLT_VOLLEY, 9s);  // 9秒冷却
                        break;
                    default:
                        break;
                }
            }

            // 血量触发机制:80%血量时施放守护
            if (!_ward80 && HealthBelowPct(80))
            {
                _ward80 = true;
                Talk(SAY_WARD);
                events.ScheduleEvent(EVENT_WARD_OF_ZUM_RAH, 1s);
            }

            // 血量触发机制:40%血量时再次施放守护
            if (!_ward40 && HealthBelowPct(40))
            {
                _ward40 = true;
                Talk(SAY_WARD);
                events.ScheduleEvent(EVENT_WARD_OF_ZUM_RAH, 1s);
            }

            // 血量触发机制:30%血量时自我治疗
            if (!_heal30 && HealthBelowPct(30))
            {
                _heal30 = true;
                events.ScheduleEvent(EVENT_HEALING_WAVE, 3s);
            }

            DoMeleeAttackIfReady();
        }

        private:
            bool _ward80;   ///< 80%血量时是否已施放守护
            bool _ward40;   ///< 40%血量时是否已施放守护
            bool _heal30;   ///< 30%血量时是否已治疗

    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return 新创建的AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<boss_zum_rahAI>(creature);
    }
};

/**
 * @brief 注册祖尔拉姆Boss脚本
 *
 * 此函数在脚本加载时被调用,创建Boss脚本对象
 */
void AddSC_boss_zum_rah()
{
    new boss_zum_rah();
}
