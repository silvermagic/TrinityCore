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
 * @file boss_kurinnaxx.cpp
 * @brief 安其拉废墟BOSS库林纳克斯（Kurinnaxx）的AI脚本
 *
 * 本模块实现了安其拉废墟第一个BOSS库林纳克斯的战斗逻辑：
 * - 致命创伤：对目标施加减益效果，降低受到的治疗效果
 * - 沙尘陷阱：在玩家位置生成陷阱，造成AOE伤害
 * - 狂暴：血量低于30%时进入狂暴状态，提高攻击速度
 * - 大范围横扫：对前方多个目标造成伤害
 */

#include "ScriptMgr.h"
#include "CreatureTextMgr.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ruins_of_ahnqiraj.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_MORTALWOUND       = 25646,  ///< 致命创伤：降低受到的治疗效果，可叠加
    SPELL_SANDTRAP          = 25648,  ///< 沙尘陷阱：在目标位置生成陷阱，造成自然伤害
    SPELL_ENRAGE            = 26527,  ///< 狂暴：提高攻击速度和伤害
    SPELL_SUMMON_PLAYER     = 26446,  ///< 召唤玩家：未使用
    SPELL_TRASH             =  3391,  ///< 猛击：近战攻击技能，可能由光环触发（未找到相关光环）
    SPELL_WIDE_SLASH        = 25814   ///< 大范围横扫：对前方多个目标造成伤害
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_MORTAL_WOUND      = 1,  ///< 致命创伤事件
    EVENT_SANDTRAP          = 2,  ///< 沙尘陷阱事件
    EVENT_TRASH             = 3,  ///< 猛击事件
    EVENT_WIDE_SLASH        = 4   ///< 大范围横扫事件
};

/**
 * @brief 文本枚举
 */
enum Texts
{
    SAY_KURINAXX_DEATH      = 5,  ///< 库林纳克斯死亡时奥库塔尔喊话
};

/**
 * @class boss_kurinnaxx
 * @brief 库林纳克斯BOSS脚本类
 *
 * 负责注册和管理库林纳克斯BOSS的AI行为
 */
class boss_kurinnaxx : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_kurinnaxx() : CreatureScript("boss_kurinnaxx") { }

        /**
         * @class boss_kurinnaxxAI
         * @brief 库林纳克斯BOSS的AI实现类
         *
         * 实现了库林纳克斯的战斗逻辑：
         * - 周期性施放致命创伤，降低坦克的治疗效果
         * - 随机在玩家位置生成沙尘陷阱
         * - 血量低于30%时进入狂暴状态
         * - 使用猛击和大范围横扫进行近战攻击
         */
        struct boss_kurinnaxxAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_kurinnaxxAI(Creature* creature) : BossAI(creature, DATA_KURINNAXX)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 在构造函数和重置时调用，用于初始化战斗状态变量
             */
            void Initialize()
            {
                _enraged = false;  ///< 是否已进入狂暴状态
            }

            /**
             * @brief 重置BOSS状态
             *
             * 当BOSS脱离战斗或重置时调用，安排技能事件
             * @调用时机 战斗重置、BOSS脱战
             */
            void Reset() override
            {
                _Reset();
                Initialize();
                // 安排技能事件
                events.ScheduleEvent(EVENT_MORTAL_WOUND, 8s);    // 致命创伤
                events.ScheduleEvent(EVENT_SANDTRAP, 5s, 15s);  // 沙尘陷阱
                events.ScheduleEvent(EVENT_TRASH, 1s);          // 猛击
                events.ScheduleEvent(EVENT_WIDE_SLASH, 11s);    // 大范围横扫
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当BOSS血量低于30%时进入狂暴状态
             * @调用时机 BOSS受到伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (!_enraged && HealthBelowPct(30))
                {
                    DoCast(me, SPELL_ENRAGE);  // 施放狂暴
                    _enraged = true;
                }
            }

            /**
             * @brief 死亡回调
             * @param killer 击杀者（未使用）
             *
             * 当BOSS死亡时，让奥库塔尔喊话
             * @调用时机 BOSS死亡时
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                // 让奥库塔尔喊话，在整个区域广播
                if (Creature* Ossirian = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_OSSIRIAN)))
                    sCreatureTextMgr->SendChat(Ossirian, SAY_KURINAXX_DEATH, nullptr, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_ZONE);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 每帧调用，处理BOSS的战斗逻辑
             * @调用时机 每帧更新
             * @性能注意事项 该函数每帧调用，需保持高效
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有目标则返回
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停其他操作
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_MORTAL_WOUND:
                            // 对当前目标施放致命创伤
                            DoCastVictim(SPELL_MORTALWOUND);
                            events.ScheduleEvent(EVENT_MORTAL_WOUND, 8s);
                            break;
                        case EVENT_SANDTRAP:
                            // 在随机玩家位置生成沙尘陷阱
                            // 优先选择100码内的随机玩家，如果没有则选择当前目标
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                                target->CastSpell(target, SPELL_SANDTRAP, true);
                            else if (Unit* victim = me->GetVictim())
                                victim->CastSpell(victim, SPELL_SANDTRAP, true);
                            events.ScheduleEvent(EVENT_SANDTRAP, 5s, 15s);
                            break;
                        case EVENT_WIDE_SLASH:
                            // 施放大范围横扫
                            DoCast(me, SPELL_WIDE_SLASH);
                            events.ScheduleEvent(EVENT_WIDE_SLASH, 11s);
                            break;
                        case EVENT_TRASH:
                            // 施放猛击
                            DoCast(me, SPELL_TRASH);
                            events.ScheduleEvent(EVENT_WIDE_SLASH, 15s);
                            break;
                        default:
                            break;
                    }

                    // 如果正在施法，暂停其他操作
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();
            }
            private:
                bool _enraged;  ///< 是否已进入狂暴状态
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<boss_kurinnaxxAI>(creature);
        }
};

/**
 * @brief 注册脚本
 *
 * 将库林纳克斯BOSS脚本注册到脚本系统
 */
void AddSC_boss_kurinnaxx()
{
    new boss_kurinnaxx();
}
