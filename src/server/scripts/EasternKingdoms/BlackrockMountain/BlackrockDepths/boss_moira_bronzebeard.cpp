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
 * @file boss_moira_bronzebeard.cpp
 * @brief 黑石深渊副本BOSS：茉艾拉·铜须(Moira Bronzebeard)的AI脚本实现
 *
 * 茉艾拉·铜须是黑石深渊副本中的最终BOSS之一，与索瑞森大帝一同出现。
 * 她是一个暗影牧师型BOSS，会使用暗影法术攻击玩家。
 * 根据玩家的任务进度，她可能以茉艾拉公主或瑟瑞斯萨女祭司的形态出现。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义茉艾拉·铜须使用的所有法术ID
 */
enum Spells
{
    SPELL_HEAL                                             = 10917,  // 治疗术（当前未使用）
    SPELL_RENEW                                            = 10929,  // 恢复术（当前未使用）
    SPELL_SHIELD                                           = 10901,  // 真言术：盾（当前未使用）
    SPELL_MINDBLAST                                        = 10947,  // 心灵震爆 - 暗影伤害法术
    SPELL_SHADOWWORDPAIN                                   = 10894,  // 暗言术：痛 - 暗影持续伤害法术
    SPELL_SMITE                                            = 10934   // 神圣之火 - 神圣伤害法术
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_MINDBLAST                                        = 1,  // 心灵震爆事件
    EVENT_SHADOW_WORD_PAIN                                 = 2,  // 暗言术：痛事件
    EVENT_SMITE                                            = 3,  // 神圣之火事件
    EVENT_HEAL                                             = 4   // 治疗事件（当前未使用）
};

/**
 * @brief 茉艾拉·铜须BOSS脚本类
 *
 * 继承自CreatureScript，为茉艾拉·铜须提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为、法术释放和事件调度。
 */
class boss_moira_bronzebeard : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_moira_bronzebeard"
         */
        boss_moira_bronzebeard() : CreatureScript("boss_moira_bronzebeard") { }

        /**
         * @brief 茉艾拉·铜须AI结构体
         *
         * 继承自ScriptedAI，实现茉艾拉·铜须的战斗AI逻辑。
         * 该AI控制BOSS的法术释放时机和战斗行为。
         */
        struct boss_moira_bronzebeardAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类和成员变量
             */
            boss_moira_bronzebeardAI(Creature* creature) : ScriptedAI(creature) { }

            /**
             * @brief 重置AI状态
             *
             * 当BOSS脱离战斗或重置时调用。
             * 清空所有已调度的事件，使BOSS恢复初始状态。
             *
             * 调用时机：BOSS脱离战斗、团灭重置、实例重置时
             */
            void Reset() override
            {
                _events.Reset();
            }

            /**
             * @brief 进入战斗事件处理
             * @param who 进入战斗的目标单位（当前未使用）
             *
             * 当BOSS进入战斗状态时调用。
             * 调度所有战斗法术的初始释放时间。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                //_events.ScheduleEvent(EVENT_HEAL, 12s); // not used atm // These times are probably wrong
                _events.ScheduleEvent(EVENT_MINDBLAST, 16s);          // 16秒后首次施放心灵震爆
                _events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 2s);     // 2秒后首次施放暗言术：痛
                _events.ScheduleEvent(EVENT_SMITE, 8s);                // 8秒后首次施放神圣之火
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 每个游戏循环周期调用一次，处理战斗逻辑。
             * 包括检查战斗状态、更新事件计时器、执行法术释放。
             *
             * 调用时机：每个游戏Tick（约每50毫秒）
             * 性能注意事项：频繁调用，需要优化处理逻辑
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有有效的攻击目标，则不执行任何操作
                if (!UpdateVictim())
                    return;

                // 更新事件计时器
                _events.Update(diff);

                // 执行所有到期的事件
                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_MINDBLAST:
                            // 施放心灵震爆，造成暗影伤害
                            DoCastVictim(SPELL_MINDBLAST);
                            _events.ScheduleEvent(EVENT_MINDBLAST, 14s);  // 14秒后再次施放
                            break;
                        case EVENT_SHADOW_WORD_PAIN:
                            // 施放暗言术：痛，造成持续暗影伤害
                            DoCastVictim(SPELL_SHADOWWORDPAIN);
                            _events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 18s);  // 18秒后再次施放
                            break;
                        case EVENT_SMITE:
                            // 施放神圣之火，造成神圣伤害
                            DoCastVictim(SPELL_SMITE);
                            _events.ScheduleEvent(EVENT_SMITE, 10s);  // 10秒后再次施放
                            break;
                        default:
                            break;
                    }
                }
            }

        private:
            EventMap _events;  ///< 事件映射表，用于管理法术释放的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回茉艾拉·铜须的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_moira_bronzebeardAI>(creature);
        }
};

/**
 * @brief 注册茉艾拉·铜须脚本
 *
 * 将茉艾拉·铜须BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_moira_bronzebeard()
{
    new boss_moira_bronzebeard();
}
