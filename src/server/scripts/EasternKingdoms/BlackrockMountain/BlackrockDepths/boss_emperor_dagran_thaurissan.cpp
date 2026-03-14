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
 * @file boss_emperor_dagran_thaurissan.cpp
 * @brief 黑石深渊副本最终BOSS：达格兰·索瑞森大帝(Emperor Dagran Thaurissan)的AI脚本实现
 *
 * 达格兰·索瑞森大帝是黑石深渊副本的最终BOSS，与茉艾拉·铜须公主一同战斗。
 * 该BOSS是黑铁矮人的统治者，拥有强大的火焰法术能力。
 * 战斗特色：
 * - 使用索瑞森之手随机攻击目标
 * - 使用火焰化身增强自身战斗力
 * - 死亡后茉艾拉变为友好状态并发表台词
 *
 * 这是一个典型的双BOSS战斗，需要同时处理大帝和茉艾拉的攻击。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

/**
 * @brief 对话文本枚举
 * 定义达格兰·索瑞森大帝使用的对话文本索引
 */
enum Yells
{
    SAY_AGGRO                                              = 0,  // 进入战斗对话
    SAY_SLAY                                               = 1   // 击杀玩家对话
};

/**
 * @brief 法术ID枚举
 * 定义达格兰·索瑞森大帝使用的法术ID
 */
enum Spells
{
    SPELL_HANDOFTHAURISSAN                                 = 17492,  // 索瑞森之手 - 随机目标暗影伤害
    SPELL_AVATAROFFLAME                                    = 15636   // 火焰化身 - 增强自身战斗能力
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_HANDOFTHAURISSAN                                 = 1,  // 索瑞森之手事件
    EVENT_AVATAROFFLAME                                    = 2   // 火焰化身事件
};

/**
 * @brief 表情枚举
 * 定义茉艾拉公主使用的表情文本索引
 */
enum Emotes
{
    EMOTE_SHAKEN                                           = 0  // 颤抖表情 - 大帝死亡后茉艾拉的反应
};

/**
 * @brief 达格兰·索瑞森大帝BOSS脚本类
 *
 * 继承自CreatureScript，为达格兰·索瑞森大帝提供AI逻辑支持。
 * 该脚本负责管理BOSS的战斗行为、技能释放和与茉艾拉的交互。
 * 大帝是黑石深渊的最终BOSS，战斗难度中等。
 */
class boss_emperor_dagran_thaurissan : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_emperor_dagran_thaurissan"
         */
        boss_emperor_dagran_thaurissan() : CreatureScript("boss_emperor_dagran_thaurissan") { }

        /**
         * @brief 达格兰·索瑞森大帝AI结构体
         *
         * 继承自ScriptedAI，实现达格兰·索瑞森大帝的战斗AI逻辑。
         * 该AI控制BOSS的技能释放时机和与茉艾拉的交互。
         */
        struct boss_draganthaurissanAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针，用于初始化基类
             */
            boss_draganthaurissanAI(Creature* creature) : ScriptedAI(creature)
            {
                _instance = me->GetInstanceScript();  // 获取副本实例脚本
            }

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
                _events.Reset();  // 重置事件映射表
            }

            /**
             * @brief 进入战斗事件处理
             * @param who 进入战斗的目标单位（当前未使用）
             *
             * 当BOSS进入战斗状态时调用。
             * 说开场对话，呼叫茉艾拉协助战斗，并调度技能释放。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             * 性能注意事项：事件调度为轻量级操作，无性能影响
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                Talk(SAY_AGGRO);  // 说开场对话
                me->CallForHelp(VISIBLE_RANGE);  // 呼叫视线范围内的茉艾拉协助战斗
                _events.ScheduleEvent(EVENT_HANDOFTHAURISSAN, 4s);  // 4秒后首次施放索瑞森之手
                _events.ScheduleEvent(EVENT_AVATAROFFLAME, 25s);    // 25秒后首次施放火焰化身
            }

            /**
             * @brief 击杀单位事件处理
             * @param who 被击杀的单位
             *
             * 当BOSS击杀玩家时调用。
             * 说击杀对话以增强战斗氛围。
             *
             * 调用时机：BOSS击杀玩家时
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);  // 说击杀对话
            }

            /**
             * @brief 死亡事件处理
             * @param killer 击杀者（当前未使用）
             *
             * 当BOSS死亡时调用。
             * 让茉艾拉停止战斗，变为友好阵营，并发表颤抖的表情。
             *
             * 调用时机：BOSS死亡时
             */
            void JustDied(Unit* /*killer*/) override
            {
                // 获取茉艾拉并让她停止战斗
                if (Creature* moira = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_MOIRA)))
                {
                    moira->AI()->EnterEvadeMode();  // 茉艾拉脱离战斗
                    moira->SetFaction(FACTION_FRIENDLY);  // 茉艾拉变为友好阵营
                    moira->AI()->Talk(EMOTE_SHAKEN);  // 茉艾拉发表颤抖表情
                }
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 每个游戏循环周期调用一次，处理战斗逻辑。
             * 包括检查战斗状态、更新事件计时器、执行技能释放。
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
                        case EVENT_HANDOFTHAURISSAN:
                            // 随机选择一个目标施放索瑞森之手
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_HANDOFTHAURISSAN);
                            _events.ScheduleEvent(EVENT_HANDOFTHAURISSAN, 5s);  // 5秒后再次施放
                            break;
                        case EVENT_AVATAROFFLAME:
                            // 对当前目标施放火焰化身，增强自身战斗力
                            DoCastVictim(SPELL_AVATAROFFLAME);
                            _events.ScheduleEvent(EVENT_AVATAROFFLAME, 18s);  // 18秒后再次施放
                            break;
                        default:
                            break;
                    }
                }

                // 如果技能都在冷却中，执行普通攻击
                DoMeleeAttackIfReady();
            }

        private:
            InstanceScript* _instance;  ///< 副本实例脚本指针，用于访问副本数据和茉艾拉GUID
            EventMap _events;           ///< 事件映射表，用于管理技能释放的计时和调度
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回达格兰·索瑞森大帝的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         * 使用模板函数确保类型安全的转换。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_draganthaurissanAI>(creature);
        }
};

/**
 * @brief 注册达格兰·索瑞森大帝脚本
 *
 * 将达格兰·索瑞森大帝BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_draganthaurissan()
{
    new boss_emperor_dagran_thaurissan();
}
