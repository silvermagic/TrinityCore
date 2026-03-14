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
 * @file    boss_postmaster_malown.cpp
 * @brief   邮差马龙(Postmaster Malown)BOSS脚本
 *
 * @details 本模块实现了斯坦索姆副本中的隐藏BOSS邮差马龙的AI逻辑:
 *          - 邮差马龙是打开第三个邮箱后刷新的隐藏BOSS
 *          - 掉落著名的"马龙的长柄扫把"等装备
 *          - 使用多种亡灵诅咒和暗影技能
 *          - 使用事件驱动系统管理技能施放
 *
 * @note BOSS完成度: 50%
 *       召唤法术ID: 24627 "召唤邮差马龙"
 *       应在第三个邮箱打开时与三个精英怪一起刷新
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 邮差马龙的台词枚举
 */
enum Says
{
    SAY_KILL                    = 0   // 击杀玩家时的台词
};

/**
 * @brief 邮差马龙使用的法术枚举
 */
enum Spells
{
    SPELL_WAILINGDEAD           = 7713,  // 亡者哀嚎 - 造成暗影伤害并降低属性
    SPELL_BACKHAND              = 6253,  // 反手一击 - 眩晕目标2秒
    SPELL_CURSEOFWEAKNESS       = 8552,  // 虚弱诅咒 - 降低目标力量和敏捷
    SPELL_CURSEOFTONGUES        = 12889, // 诅咒之舌 - 增加施法时间
    SPELL_CALLOFTHEGRAVE        = 17831  // 坟墓召唤 - 暗影伤害法术
};

/**
 * @brief 事件ID枚举
 *
 * @details 用于事件调度系统管理技能冷却
 */
enum Events
{
    EVENT_WAILINGDEAD          = 1,  // 亡者哀嚎事件
    EVENT_BACKHAND             = 2,  // 反手一击事件
    EVENT_CURSEOFWEAKNESS      = 3,  // 虚弱诅咒事件
    EVENT_CURSEOFTONGUES       = 4,  // 诅咒之舌事件
    EVENT_CALLOFTHEGRAVE       = 5   // 坟墓召唤事件
};

/**
 * @class boss_postmaster_malown
 * @brief 邮差马龙BOSS脚本类
 *
 * @details 实现邮差马龙BOSS的AI脚本注册和AI逻辑
 */
class boss_postmaster_malown : public CreatureScript
{
    public:
        boss_postmaster_malown() : CreatureScript("boss_postmaster_malown") { }

        /**
         * @struct boss_postmaster_malownAI
         * @brief 邮差马龙AI实现
         *
         * @details 实现邮差马龙的战斗AI:
         *          - 继承BossAI基类,使用事件调度系统
         *          - 使用多种亡灵诅咒和暗影技能
         *          - 每个技能都有施放概率和独立冷却
         *          - 击杀玩家时会说话
         */
        struct boss_postmaster_malownAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             *
             * @details 初始化BossAI基类,指定BOSS类型为TYPE_MALOWN
             */
            boss_postmaster_malownAI(Creature* creature) : BossAI(creature, TYPE_MALOWN) { }

            /**
             * @brief 重置AI状态
             *
             * @details 在战斗重置时调用,目前无特殊逻辑
             */
            void Reset() override { }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标(未使用)
             *
             * @details 当BOSS进入战斗时调用,初始化所有技能的事件调度:
             *          - 亡者哀嚎: 19秒后首次施放,持续6秒
             *          - 反手一击: 8秒后首次施放,眩晕2秒
             *          - 虚弱诅咒: 20秒后首次施放,持续2分钟
             *          - 诅咒之舌: 22秒后首次施放
             *          - 坟墓召唤: 25秒后首次施放
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                events.ScheduleEvent(EVENT_WAILINGDEAD, 19s);     // lasts 6 sec - 持续6秒
                events.ScheduleEvent(EVENT_BACKHAND, 8s);         // 2 sec stun - 眩晕2秒
                events.ScheduleEvent(EVENT_CURSEOFWEAKNESS, 20s); // lasts 2 mins - 持续2分钟
                events.ScheduleEvent(EVENT_CURSEOFTONGUES, 22s);
                events.ScheduleEvent(EVENT_CALLOFTHEGRAVE, 25s);
            }

            /**
             * @brief 击杀单位处理
             * @param victim 被击杀的单位(未使用)
             *
             * @details 当BOSS击杀玩家时播放击杀台词
             */
            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_KILL);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间差(毫秒)
             *
             * @details 主循环逻辑,每帧调用:
             *          1. 检查是否有战斗目标,无则返回
             *          2. 更新事件调度器
             *          3. 检查是否正在施法,是则等待施法完成
             *          4. 执行事件队列:
             *             - 亡者哀嚎: 65%概率施放,19秒冷却
             *             - 反手一击: 45%概率施放,8秒冷却
             *             - 虚弱诅咒: 3%概率施放,20秒冷却
             *             - 诅咒之舌: 3%概率施放,22秒冷却
             *             - 坟墓召唤: 5%概率施放,25秒冷却
             *          5. 每个事件后检查施法状态
             *          6. 执行近战攻击
             *
             * @note 所有技能使用概率施放机制,增加战斗的随机性
             * @note 注意: 事件重新调度时所有技能都错误地使用了EVENT_WAILINGDEAD
             *       这是一个BUG,应该使用各自的EVENT_ID
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 检查是否正在施法,施法期间不执行新事件
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_WAILINGDEAD:
                            if (rand32() % 100 < 65) //65% chance to cast - 65%概率施放
                                DoCastVictim(SPELL_WAILINGDEAD, true);
                            events.ScheduleEvent(EVENT_WAILINGDEAD, 19s);
                            break;
                        case EVENT_BACKHAND:
                            if (rand32() % 100 < 45) //45% chance to cast - 45%概率施放
                                DoCastVictim(SPELL_BACKHAND, true);
                            // BUG: 应该是EVENT_BACKHAND而非EVENT_WAILINGDEAD
                            events.ScheduleEvent(EVENT_WAILINGDEAD, 8s);
                            break;
                        case EVENT_CURSEOFWEAKNESS:
                            if (rand32() % 100 < 3) //3% chance to cast - 3%概率施放
                                DoCastVictim(SPELL_CURSEOFWEAKNESS, true);
                            // BUG: 应该是EVENT_CURSEOFWEAKNESS而非EVENT_WAILINGDEAD
                            events.ScheduleEvent(EVENT_WAILINGDEAD, 20s);
                            break;
                        case EVENT_CURSEOFTONGUES:
                            if (rand32() % 100 < 3) //3% chance to cast - 3%概率施放
                                DoCastVictim(SPELL_CURSEOFTONGUES, true);
                            // BUG: 应该是EVENT_CURSEOFTONGUES而非EVENT_WAILINGDEAD
                            events.ScheduleEvent(EVENT_WAILINGDEAD, 22s);
                            break;
                        case EVENT_CALLOFTHEGRAVE:
                            if (rand32() % 100 < 5) //5% chance to cast - 5%概率施放
                                DoCastVictim(SPELL_CALLOFTHEGRAVE, true);
                            // BUG: 应该是EVENT_CALLOFTHEGRAVE而非EVENT_WAILINGDEAD
                            events.ScheduleEvent(EVENT_WAILINGDEAD, 25s);
                            break;
                        default:
                            break;
                    }

                    // 检查是否开始施法,施法期间停止处理后续事件
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         *
         * @details 创建并返回邮差马龙AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetStratholmeAI<boss_postmaster_malownAI>(creature);
        }
};

/**
 * @brief 注册脚本
 *
 * @details 将邮差马龙BOSS脚本注册到脚本系统
 */
void AddSC_boss_postmaster_malown()
{
    new boss_postmaster_malown();
}
