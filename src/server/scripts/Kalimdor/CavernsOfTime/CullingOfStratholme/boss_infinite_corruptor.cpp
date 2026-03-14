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
 * @file boss_infinite_corruptor.cpp
 * @brief 斯坦索姆的抉择副本 - 无限腐蚀者Boss脚本
 *
 * 本模块实现无限腐蚀者Boss的AI行为，该Boss是英雄难度专属的限时挑战Boss。
 * 玩家需要在25分钟内击败无限腐蚀者，否则时间守护者会被杀死，任务失败。
 *
 * 主要功能：
 * - Boss战斗AI逻辑
 * - 与时间守护者和时间裂隙的交互
 * - 超时失败机制
 * - 战斗技能调度
 */

#include "culling_of_stratholme.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"

/**
 * @brief Boss技能ID枚举
 */
enum Spells
{
    SPELL_CORRUPTING_BLIGHT = 60588,        // 腐蚀之疫 - 对随机目标施放的AOE伤害技能
    SPELL_VOID_STRIKE = 60590,              // 虚空打击 - 对当前目标施放的主要攻击技能
    SPELL_CORRUPTION_OF_TIME_CHANNEL = 60422, // 时间腐蚀引导 - 引导法术，影响时间守护者
    SPELL_CORRUPTION_OF_TIME_TARGET = 60451  // 时间腐蚀目标效果 - 施加在时间守护者身上的debuff
};

/**
 * @brief Boss台词ID枚举
 */
enum Yells
{
    SAY_AGGRO = 0,  // 开战台词
    SAY_DEATH = 1,  // 死亡台词
    SAY_FAIL = 2    // 失败台词（超时）
};

/**
 * @brief 事件ID枚举，用于战斗AI事件调度
 */
enum Events
{
    EVENT_CORRUPTING_BLIGHT = 1,  // 腐蚀之疫技能事件
    EVENT_VOID_STRIKE             // 虚空打击技能事件
};

/**
 * @brief 相关NPC条目ID枚举
 */
enum Entries
{
    NPC_TIME_RIFT = 28409,         // 时间裂隙 - 召唤点
    NPC_GUARDIAN_OF_TIME = 32281   // 时间守护者 - 需要被保护的NPC
};

/**
 * @brief 杂项枚举
 */
enum Misc
{
    MOVEMENT_TIME_RIFT = 1  // 移动到时间裂隙的点ID
};

/**
 * @brief 无限腐蚀者Boss脚本类
 *
 * 继承自CreatureScript，负责注册和管理无限腐蚀者Boss的AI实例。
 * 该Boss是英雄难度专属的限时挑战Boss。
 */
class boss_infinite_corruptor : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化Boss脚本，注册脚本名称为"boss_infinite_corruptor"
         */
        boss_infinite_corruptor() : CreatureScript("boss_infinite_corruptor") { }

        /**
         * @brief 无限腐蚀者AI结构体
         *
         * 继承自BossAI，实现无限腐蚀者的战斗逻辑。
         * 主要职责：
         * - 管理战斗技能循环
         * - 维持对时间守护者的腐蚀效果
         * - 处理超时失败逻辑
         */
        struct boss_infinite_corruptorAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 关联的生物对象指针
             *
             * 初始化BossAI基类，绑定数据为DATA_INFINITE_CORRUPTOR
             */
            boss_infinite_corruptorAI(Creature* creature) : BossAI(creature, DATA_INFINITE_CORRUPTOR) { }

            /**
             * @brief 重置Boss状态
             *
             * 调用时机：Boss脱离战斗或实例重置时
             *
             * 执行操作：
             * - 调用基类的_Reset方法清理战斗状态
             * - 施放时间腐蚀引导法术，持续影响时间守护者
             *
             * @note 该引导法术隐式地以时间守护者为目标
             */
            void Reset() override
            {
                _Reset();
                DoCastAOE(SPELL_CORRUPTION_OF_TIME_CHANNEL); // 隐式以时间守护者为目标
            }

            /**
             * @brief 法术命中目标回调
             * @param target 法术命中的目标对象
             * @param spellInfo 法术信息指针
             *
             * 调用时机：当法术命中目标时由核心调用
             *
             * 处理逻辑：
             * - 当时间腐蚀引导法术命中目标时，让目标对自己施放时间腐蚀目标效果
             * - 这确保时间守护者身上有正确的debuff效果
             */
            void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
            {
                if (spellInfo->Id == SPELL_CORRUPTION_OF_TIME_CHANNEL)
                    target->CastSpell(target, SPELL_CORRUPTION_OF_TIME_TARGET, true);
            }

            /**
             * @brief 进入战斗回调
             * @param who 触发战斗的单位
             *
             * 调用时机：Boss进入战斗时
             *
             * 执行操作：
             * - 播放开战台词
             * - 调用基类的JustEngagedWith方法
             * - 初始化技能调度：腐蚀之疫(7秒后)，虚空打击(5秒后)
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_AGGRO);
                BossAI::JustEngagedWith(who);
                events.ScheduleEvent(EVENT_CORRUPTING_BLIGHT, 7s);
                events.ScheduleEvent(EVENT_VOID_STRIKE, 5s);
            }

            /**
             * @brief Boss死亡回调
             * @param killer 击杀Boss的单位
             *
             * 调用时机：Boss死亡时
             *
             * 执行操作：
             * - 播放死亡台词
             * - 调用基类的_JustDied方法
             * - 移除时间守护者身上的腐蚀debuff
             * - 让时间守护者在5秒后消失
             * - 消失时间裂隙
             *
             * @note 这表示玩家成功在时限内击败了Boss
             */
            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEATH);
                _JustDied();

                if (Creature* guardian = me->FindNearestCreature(NPC_GUARDIAN_OF_TIME, 100.0f))
                {
                    guardian->RemoveAurasDueToSpell(SPELL_CORRUPTION_OF_TIME_TARGET);
                    guardian->DespawnOrUnsummon(5s);
                }

                if (Creature* rift = me->FindNearestCreature(NPC_TIME_RIFT, 100.0f))
                    rift->DespawnOrUnsummon();
            }

            /**
             * @brief 执行事件回调
             * @param eventId 要执行的事件ID
             *
             * 调用时机：事件调度器触发事件时
             *
             * 事件处理：
             * - EVENT_CORRUPTING_BLIGHT: 对随机玩家施放腐蚀之疫，15秒后重复
             * - EVENT_VOID_STRIKE: 对当前目标施放虚空打击，5秒后重复
             */
            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_CORRUPTING_BLIGHT:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 60.0f, true))
                            DoCast(target, SPELL_CORRUPTING_BLIGHT);
                        events.ScheduleEvent(EVENT_CORRUPTING_BLIGHT, 15s);
                        break;
                    case EVENT_VOID_STRIKE:
                        DoCastVictim(SPELL_VOID_STRIKE);
                        events.ScheduleEvent(EVENT_VOID_STRIKE, 5s);
                        break;
                    default:
                        break;
                }
            }

            /**
             * @brief 进入闪避模式回调
             * @param why 闪避原因
             *
             * 调用时机：Boss脱战或重置时
             *
             * 特殊处理：
             * - 如果Boss处于被动反应状态（正在执行失败离开逻辑），不执行闪避
             * - 否则调用基类的闪避逻辑
             *
             * @note 被动状态用于防止Boss在执行超时离开时被打断
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                if (me->HasReactState(REACT_PASSIVE))
                    return;
                BossAI::EnterEvadeMode(why);
            }

            /**
             * @brief 移动完成通知回调
             * @param type 移动类型
             * @param id 移动点ID
             *
             * 调用时机：当生物完成移动时
             *
             * 处理逻辑：
             * - 当Boss移动到时间裂隙点时，延迟2秒消失
             * - 设置Boss状态为失败（FAIL）
             *
             * @note 这处理超时失败后Boss离开的最终阶段
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                if (type == POINT_MOTION_TYPE && id == MOVEMENT_TIME_RIFT)
                {
                    me->DespawnOrUnsummon(Seconds(2));
                    instance->SetBossState(DATA_INFINITE_CORRUPTOR, FAIL);
                }
            }

            /**
             * @brief 执行动作回调
             * @param action 动作ID
             *
             * 调用时机：外部脚本请求Boss执行特定动作时
             *
             * 处理逻辑（ACTION_CORRUPTOR_LEAVE）：
             * - 设置Boss为被动状态，停止攻击
             * - 播放失败台词
             * - 寻找时间裂隙并移动到那里
             * - 如果已经在裂隙附近，直接触发消失逻辑
             *
             * @note 负数action表示反转，用于内部处理
             */
            void DoAction(int32 action) override
            {
                if (action == -ACTION_CORRUPTOR_LEAVE)
                {
                    me->SetReactState(REACT_PASSIVE);
                    Talk(SAY_FAIL);
                    if (Creature* rift = me->FindNearestCreature(NPC_TIME_RIFT, 300.0f))
                    {
                        if (me->IsWithinDist2d(rift, 5.0f))
                            MovementInform(POINT_MOTION_TYPE, MOVEMENT_TIME_RIFT);
                        else
                            me->GetMotionMaster()->MovePoint(MOVEMENT_TIME_RIFT, rift->GetPosition()); // @todo 需要添加偏移量
                    }
                    else
                        MovementInform(POINT_MOTION_TYPE, MOVEMENT_TIME_RIFT);
                }
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要AI的生物对象
         * @return 创建的AI实例指针
         *
         * 使用模板函数GetCullingOfStratholmeAI创建AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetCullingOfStratholmeAI<boss_infinite_corruptorAI>(creature);
        }
};

/**
 * @brief 注册无限腐蚀者Boss脚本
 *
 * 创建并注册无限腐蚀者Boss脚本实例
 */
void AddSC_boss_infinite_corruptor()
{
    new boss_infinite_corruptor();
}
