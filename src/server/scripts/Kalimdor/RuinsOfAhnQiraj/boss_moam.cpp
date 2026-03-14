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
 * @file boss_moam.cpp
 * @brief 安其拉废墟BOSS莫阿姆（Moam）的AI脚本
 *
 * 本模块实现了安其拉废墟第二个BOSS莫阿姆的战斗逻辑：
 * - 法力燃烧：从玩家身上吸取法力并转化为自身法力
 * - 石像阶段：血量低于45%或战斗90秒后进入石像状态
 * - 召唤法力元素：石像阶段召唤三个法力元素
 * - 奥术爆发：法力满时释放全团奥术伤害
 * - 充能：石像阶段快速恢复法力
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "ScriptedCreature.h"
#include "ruins_of_ahnqiraj.h"

/**
 * @brief 文本枚举
 */
enum Texts
{
    EMOTE_AGGRO             = 0,  ///< 进入战斗表情
    EMOTE_MANA_FULL         = 1   ///< 法力全满表情
};

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_TRAMPLE               = 15550,  ///< 践踏：AOE伤害
    SPELL_DRAIN_MANA            = 25671,  ///< 吸取法力：从目标吸取法力
    SPELL_ARCANE_ERUPTION       = 25672,  ///< 奥术爆发：法力满时释放的全团伤害
    SPELL_SUMMON_MANA_FIEND_1   = 25681,  ///< 召唤法力元素1：前方位置
    SPELL_SUMMON_MANA_FIEND_2   = 25682,  ///< 召唤法力元素2：左侧位置
    SPELL_SUMMON_MANA_FIEND_3   = 25683,  ///< 召唤法力元素3：右侧位置
    SPELL_ENERGIZE              = 25685   ///< 充能：快速恢复法力
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_TRAMPLE           = 1,  ///< 践踏事件
    EVENT_DRAIN_MANA        = 2,  ///< 吸取法力事件
    EVENT_STONE_PHASE       = 3,  ///< 石像阶段事件
    EVENT_STONE_PHASE_END   = 4,  ///< 石像阶段结束事件
    EVENT_WIDE_SLASH        = 5,  ///< 大范围横扫事件（已注释）
};

/**
 * @brief 动作ID枚举
 */
enum Actions
{
    ACTION_STONE_PHASE_START = 1,  ///< 开始石像阶段
    ACTION_STONE_PHASE_END   = 2,  ///< 结束石像阶段
};

/**
 * @class boss_moam
 * @brief 莫阿姆BOSS脚本类
 *
 * 负责注册和管理莫阿姆BOSS的AI行为
 */
class boss_moam : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_moam() : CreatureScript("boss_moam") { }

        /**
         * @class boss_moamAI
         * @brief 莫阿姆BOSS的AI实现类
         *
         * 实现了莫阿姆的战斗逻辑：
         * - 战斗开始时法力为0
         * - 从玩家身上吸取法力
         * - 法力满时释放奥术爆发
         * - 血量低于45%或战斗90秒后进入石像阶段
         * - 石像阶段召唤三个法力元素并快速充能
         */
        struct boss_moamAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_moamAI(Creature* creature) : BossAI(creature, DATA_MOAM)
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
                _isStonePhase = false;  ///< 是否处于石像阶段
            }

            /**
             * @brief 重置BOSS状态
             *
             * 当BOSS脱离战斗或重置时调用，将法力设为0并安排石像阶段
             * @调用时机 战斗重置、BOSS脱战
             */
            void Reset() override
            {
                _Reset();
                me->SetPower(POWER_MANA, 0);  // 法力归零
                Initialize();
                events.ScheduleEvent(EVENT_STONE_PHASE, 90s);  // 90秒后强制进入石像阶段
                //events.ScheduleEvent(EVENT_WIDE_SLASH, 11s);
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 当BOSS血量低于45%时进入石像阶段
             * @调用时机 BOSS受到伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (!_isStonePhase && HealthBelowPct(45))
                {
                    _isStonePhase = true;
                    DoAction(ACTION_STONE_PHASE_START);
                }
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理石像阶段的开始和结束
             * @调用时机 触发石像阶段转换时
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_STONE_PHASE_END:
                    {
                        // 结束石像阶段
                        me->RemoveAurasDueToSpell(SPELL_ENERGIZE);  // 移除充能效果
                        events.ScheduleEvent(EVENT_STONE_PHASE, 90s);  // 重新安排石像阶段
                        _isStonePhase = false;
                        break;
                    }
                    case ACTION_STONE_PHASE_START:
                    {
                        // 开始石像阶段
                        // 在三个方向召唤法力元素
                        DoCast(me, SPELL_SUMMON_MANA_FIEND_1);  // 前方
                        DoCast(me, SPELL_SUMMON_MANA_FIEND_2);  // 左侧
                        DoCast(me, SPELL_SUMMON_MANA_FIEND_3);  // 右侧
                        DoCast(me, SPELL_ENERGIZE);  // 施放充能，快速恢复法力
                        events.ScheduleEvent(EVENT_STONE_PHASE_END, 90s);  // 90秒后结束石像阶段
                        break;
                    }
                    default:
                        break;
                }
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

                // 检查法力是否已满
                if (me->GetPower(POWER_MANA) == me->GetMaxPower(POWER_MANA))
                {
                    // 如果处于石像阶段，结束石像阶段
                    if (_isStonePhase)
                        DoAction(ACTION_STONE_PHASE_END);

                    // 施放奥术爆发，造成全团伤害
                    DoCastAOE(SPELL_ARCANE_ERUPTION);
                    me->SetPower(POWER_MANA, 0);  // 法力归零
                }

                // 石像阶段只等待法力充满
                if (_isStonePhase)
                {
                    if (events.ExecuteEvent() == EVENT_STONE_PHASE_END)
                        DoAction(ACTION_STONE_PHASE_END);
                    return;
                }

                // 注意：吸取法力是引导法术，不应打断
                // Messing up mana-drain channel
                //if (me->HasUnitState(UNIT_STATE_CASTING))
                //    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_STONE_PHASE:
                            // 强制进入石像阶段
                            DoAction(ACTION_STONE_PHASE_START);
                            break;
                        case EVENT_DRAIN_MANA:
                        {
                            // 从最多5个有法力的玩家身上吸取法力
                            std::list<Unit*> targetList;
                            {
                                // 遍历威胁列表，筛选有法力的玩家
                                for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                                    if (ref->GetVictim()->GetTypeId() == TYPEID_PLAYER && ref->GetVictim()->GetPowerType() == POWER_MANA)
                                        targetList.push_back(ref->GetVictim());
                            }

                            // 随机选择最多5个目标
                            Trinity::Containers::RandomResize(targetList, 5);

                            // 对每个目标施放吸取法力
                            for (std::list<Unit*>::iterator itr = targetList.begin(); itr != targetList.end(); ++itr)
                                DoCast(*itr, SPELL_DRAIN_MANA);

                            events.ScheduleEvent(EVENT_DRAIN_MANA, 5s, 15s);
                            break;
                        }/*
                        case EVENT_WIDE_SLASH:
                            DoCast(me, SPELL_WIDE_SLASH);
                            events.ScheduleEvent(EVENT_WIDE_SLASH, 11s);
                            break;
                        case EVENT_TRASH:
                            DoCast(me, SPELL_TRASH);
                            events.ScheduleEvent(EVENT_WIDE_SLASH, 15s);
                            break;*/
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }
        private:
            bool _isStonePhase;  ///< 是否处于石像阶段
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<boss_moamAI>(creature);
        }
};

/**
 * @brief 注册脚本
 *
 * 将莫阿姆BOSS脚本注册到脚本系统
 */
void AddSC_boss_moam()
{
    new boss_moam();
}
