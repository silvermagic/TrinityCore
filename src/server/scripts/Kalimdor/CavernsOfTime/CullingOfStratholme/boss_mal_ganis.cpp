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
 * @file boss_mal_ganis.cpp
 * @brief 斯坦索姆的抉择副本 - 玛尔加尼斯Boss脚本
 *
 * 本模块实现玛尔加尼斯Boss的AI行为，这是斯坦索姆抉择副本的最终Boss。
 * 玛尔加尼斯是恐惧魔王，在剧情中是阿萨斯堕落的关键角色之一。
 *
 * 主要功能：
 * - Boss战斗AI逻辑
 * - 血量阈值触发台词
 * - 特殊的"被击败"状态（剧情上逃跑而非死亡）
 * - 多种技能组合（腐尸群、精神冲击、睡眠、吸血鬼之触）
 *
 * @note 该Boss不真正死亡，而是血量降到1时视为"被击败"，这与原版魔兽争霸III剧情一致
 */

#include "culling_of_stratholme.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"

/**
 * @brief Boss技能ID枚举
 */
enum Spells
{
    SPELL_CARRION_SWARM = 52720,     // 腐尸群 - AOE伤害技能，对多个目标造成伤害
    SPELL_MIND_BLAST = 52722,        // 精神冲击 - 对随机目标造成暗影伤害
    SPELL_SLEEP = 52721,             // 睡眠 - 使随机目标沉睡，无法行动
    SPELL_VAMPIRIC_TOUCH = 52723     // 吸血鬼之触 - Buff自身，提高伤害并治疗
};

/**
 * @brief Boss台词ID枚举
 */
enum Yells
{
    SAY_KILL = 3,       // 击杀玩家台词
    SAY_SLAY = 4,       // 击杀玩家台词（备用）
    SAY_SLEEP = 5,      // 施放睡眠技能台词
    SAY_30HEALTH = 6,   // 血量低于30%时的台词
    SAY_15HEALTH = 7    // 血量低于15%时的台词
};

/**
 * @brief 事件ID枚举，用于战斗AI事件调度
 */
enum Events
{
    EVENT_CARRION_SWARM = 1,    // 腐尸群技能事件
    EVENT_MIND_BLAST,           // 精神冲击技能事件
    EVENT_VAMPIRIC_TOUCH,       // 吸血鬼之触技能事件
    EVENT_SLEEP                 // 睡眠技能事件
};

/**
 * @brief 玛尔加尼斯Boss脚本类
 *
 * 继承自CreatureScript，负责注册和管理玛尔加尼斯Boss的AI实例。
 * 玛尔加尼斯是斯坦索姆抉择副本的最终Boss。
 */
class boss_mal_ganis : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化Boss脚本，注册脚本名称为"boss_mal_ganis"
         */
        boss_mal_ganis() : CreatureScript("boss_mal_ganis") { }

        /**
         * @brief 获取AI实例
         * @param creature 需要AI的生物对象
         * @return 创建的AI实例指针，如果条件不满足则返回null或NullCreatureAI
         *
         * 创建AI的条件：
         * - 实例脚本必须存在
         * - 实例进度必须达到MALGANIS_IN_PROGRESS阶段
         *
         * @note 如果进度未到，返回NullCreatureAI使Boss处于被动状态
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            if (!InstanceHasScript(creature, CoSScriptName))
                return nullptr;

            if (creature->GetInstanceScript()->GetData(DATA_INSTANCE_PROGRESS) < MALGANIS_IN_PROGRESS)
                return new NullCreatureAI(creature);
            else
                return new boss_mal_ganisAI(creature);
        }

        /**
         * @brief 玛尔加尼斯AI结构体
         *
         * 继承自BossAI，实现玛尔加尼斯的战斗逻辑。
         * 主要职责：
         * - 管理战斗技能循环
         * - 处理特殊的"被击败"状态（血量降到1而非死亡）
         * - 血量阈值台词触发
         * - 战斗失败后的消失逻辑
         */
        struct boss_mal_ganisAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 关联的生物对象指针
             *
             * 初始化BossAI基类，绑定数据为DATA_MAL_GANIS
             * 初始化成员变量：_defeated, _hadYell30, _hadYell15均为false
             */
            boss_mal_ganisAI(Creature* creature) : BossAI(creature, DATA_MAL_GANIS), _defeated(false), _hadYell30(false), _hadYell15(false) { }

            /**
             * @brief 重置Boss状态
             *
             * 调用时机：Boss脱离战斗或实例重置时
             *
             * 执行操作：
             * - 如果Boss未被击败，将Boss状态设为NOT_STARTED
             * - 如果已被击败，保持状态不变（避免重复设置）
             */
            void Reset() override
            {
                if (!_defeated)
                    instance->SetBossState(DATA_MAL_GANIS, NOT_STARTED);
            }

            /**
             * @brief 受到伤害回调
             * @param source 伤害来源
             * @param damage 伤害值（可修改）
             * @param damageType 伤害类型
             * @param spellInfo 法术信息（如果有）
             *
             * 调用时机：Boss每次受到伤害时
             *
             * 特殊处理：
             * - 当伤害会杀死Boss时，将伤害限制为剩余血量-1
             * - 标记Boss为"被击败"状态
             * - 将所有玩家永久绑定到该副本实例
             *
             * @note 这是特殊的剧情处理，玛尔加尼斯不真正死亡，而是逃跑
             *       这与魔兽争霸III的剧情一致
             *
             * @warning 永久绑定玩家的逻辑可能需要进一步验证
             */
            void DamageTaken(Unit* /*source*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (damage >= me->GetHealth())
                {
                    damage = me->GetHealth() - 1;
                    if (_defeated)
                        return;
                    _defeated = true;

                    // @todo 这很可能是临时方案
                    if (InstanceMap* map = instance->instance->ToInstanceMap())
                        map->PermBindAllPlayers();
                }
            }

            /**
             * @brief 进入战斗回调
             * @param who 触发战斗的单位
             *
             * 调用时机：Boss进入战斗时
             *
             * 执行操作：
             * - 重置被击败状态和台词标记
             * - 调度技能：腐尸群(5秒)、精神冲击(6-8秒)、吸血鬼之触(4秒)、睡眠(17-21秒)
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                _defeated = false;
                _hadYell30 = false;
                _hadYell15 = false;
                events.ScheduleEvent(EVENT_CARRION_SWARM, Seconds(5));
                events.ScheduleEvent(EVENT_MIND_BLAST, Seconds(6), Seconds(8));
                events.ScheduleEvent(EVENT_VAMPIRIC_TOUCH, Seconds(4));
                events.ScheduleEvent(EVENT_SLEEP, Seconds(17), Seconds(21));
            }

            /**
             * @brief 返回出生点回调
             *
             * 调用时机：Boss脱战后返回出生点时
             *
             * 执行操作：
             * - 如果Boss未被击败，1秒后消失
             * - 如果已被击败，保持状态（已由其他逻辑处理消失）
             */
            void JustReachedHome() override
            {
                if (!_defeated)
                    me->DespawnOrUnsummon(Seconds(1));
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 调用时机：每个游戏tick（通常约50ms）
             *
             * 主要逻辑：
             * 1. 如果已被击败：
             *    - 如果仍在战斗，进入闪避模式并设为免疫状态
             *    - 直接返回，不执行后续逻辑
             * 2. 如果没有战斗目标，返回
             * 3. 检查血量阈值并播放台词（30%和15%）
             * 4. 更新事件调度器
             * 5. 如果正在施法，跳过本次更新
             * 6. 执行事件并施放对应技能
             * 7. 如果可以近战攻击，执行近战攻击
             *
             * @性能注意事项：
             * - 每帧都会调用，避免在此函数中进行耗时操作
             * - 使用事件调度器而非轮询来管理技能冷却
             */
            void UpdateAI(uint32 diff) override
            {
                if (_defeated)
                {
                    if (me->IsInCombat())
                    {
                        EnterEvadeMode();
                        me->SetImmuneToAll(true);
                    }
                    return;
                }

                if (!UpdateVictim())
                    return;

                // 血量阈值台词检查
                if (!_hadYell30 && HealthBelowPct(30))
                {
                    Talk(SAY_30HEALTH);
                    _hadYell30 = true;
                }

                if (!_hadYell15 && HealthBelowPct(15))
                {
                    Talk(SAY_15HEALTH);
                    _hadYell15 = true;
                }

                events.Update(diff);

                // 如果正在施法，等待施法完成
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 执行所有到期的事件
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CARRION_SWARM:
                            // 腐尸群：AOE技能，6秒冷却
                            DoCastAOE(SPELL_CARRION_SWARM);
                            events.Repeat(Seconds(6));
                            break;
                        case EVENT_MIND_BLAST:
                            // 精神冲击：随机目标，排除睡眠目标，8-12秒冷却
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100.0f, true, -int32(sSpellMgr->GetSpellIdForDifficulty(SPELL_SLEEP, me))))
                                DoCast(target, SPELL_MIND_BLAST);
                            else
                                DoCastVictim(SPELL_MIND_BLAST);
                            events.Repeat(Seconds(8), Seconds(12));
                            break;
                        case EVENT_VAMPIRIC_TOUCH:
                            // 吸血鬼之触：自身Buff，30秒冷却
                            DoCastSelf(SPELL_VAMPIRIC_TOUCH);
                            events.Repeat(Seconds(30));
                            break;
                        case EVENT_SLEEP:
                            // 睡眠：随机目标，10-15秒冷却
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100.0f))
                                DoCast(target, SPELL_SLEEP);
                            else
                                DoCastVictim(SPELL_SLEEP);
                            events.Repeat(Seconds(10), Seconds(15));
                            break;
                        default:
                            break;
                    }

                    // 施法后立即检查施法状态，避免同时施放多个法术
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();
            }

            /**
             * @brief 击杀单位回调
             * @param victim 被击杀的单位
             *
             * 调用时机：Boss击杀任何单位时
             *
             * 执行操作：
             * - 如果Boss未被击败且受害者是玩家，播放击杀台词
             */
            void KilledUnit(Unit* victim) override
            {
                if (!_defeated && victim->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

        private:
            bool _defeated;    // 是否已被击败（血量降到1）
            bool _hadYell30;   // 是否已播放30%血量台词
            bool _hadYell15;   // 是否已播放15%血量台词
        };
};

/**
 * @brief 注册玛尔加尼斯Boss脚本
 *
 * 创建并注册玛尔加尼斯Boss脚本实例
 */
void AddSC_boss_mal_ganis()
{
    new boss_mal_ganis();
}
