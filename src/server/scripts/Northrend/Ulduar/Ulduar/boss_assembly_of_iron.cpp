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
 * @file boss_assembly_of_iron.cpp
 * @brief 奥杜尔副本 - 钢铁议会首领战脚本
 *
 * 本模块实现了奥杜尔副本中的钢铁议会首领战，包括三个首领：
 * - 钢铁破坏者 (Steelbreaker): 坦克型首领，拥有高伤害技能和静电干扰
 * - 符文大师莫尔基姆 (Runemaster Molgeim): 法系首领，召唤符文和护盾
 * - 风暴召唤者布伦迪尔 (Stormcaller Brundir): 远程法系首领，链状闪电和过载技能
 *
 * 战斗机制：
 * - 三个首领共享仇恨列表但独立行动
 * - 当一个首领死亡时，其他首领获得充能增益并进入下一阶段
 * - 击杀顺序决定战斗难度和成就
 * - 支持多种击杀顺序对应的成就
 */

/* ScriptData
SDName: Assembly of Iron encounter
SD%Complete: 60%
SDComment: chain lightning won't cast, supercharge don't work (auras don't stack from different casters)
SDCategory: Ulduar - Ulduar
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "ulduar.h"

/**
 * @brief 钢铁议会战斗使用的法术ID枚举
 *
 * 包含三个首领使用的所有法术ID，按首领分类组织
 */
enum AssemblySpells
{
    // General - 通用法术
    SPELL_SUPERCHARGE                            = 61920,  ///< 充能：首领死亡时其他首领获得的增益效果
    SPELL_BERSERK                                = 47008,  ///< 狂暴：硬暴怒机制（ID待确认）
    SPELL_KILL_CREDIT                            = 65195,  ///< 击杀荣誉：用于给予玩家击杀荣誉的法术

    // Steelbreaker - 钢铁破坏者的法术
    SPELL_HIGH_VOLTAGE                           = 61890,  ///< 高压：被动光环，对近战攻击者造成自然伤害
    SPELL_FUSION_PUNCH                           = 61903,  ///< 聚合重击：对当前目标造成高额伤害
    SPELL_STATIC_DISRUPTION                      = 44008,  ///< 静电干扰：随机目标的自然伤害和沉默效果
    SPELL_OVERWHELMING_POWER                     = 64637,  ///< 压倒性力量：第三阶段技能，对坦克施加致命伤害
    SPELL_ELECTRICAL_CHARGE                      = 61902,  ///< 电荷：第三阶段，击杀玩家时获得电荷叠加

    // Runemaster Molgeim - 符文大师莫尔基姆的法术
    SPELL_SHIELD_OF_RUNES                        = 62274,  ///< 符文护盾：吸收伤害的护盾
    SPELL_SHIELD_OF_RUNES_BUFF                   = 62277,  ///< 符文护盾增益：护盾消失后的攻击强度增益
    SPELL_SUMMON_RUNE_OF_POWER                   = 63513,  ///< 召唤能量符文：增加伤害的符文
    SPELL_RUNE_OF_DEATH                          = 62269,  ///< 死亡符文：在目标位置造成持续伤害
    SPELL_RUNE_OF_SUMMONING                      = 62273,  ///< 召唤符文：召唤闪电元素的符文法术
    SPELL_RUNE_OF_SUMMONING_SUMMON               = 62020,  ///< 召唤符文召唤效果：实际召唤闪电元素的法术

    // Stormcaller Brundir - 风暴召唤者布伦迪尔的法术
    SPELL_CHAIN_LIGHTNING                        = 61879,  ///< 链状闪电：跳跃的自然伤害法术
    SPELL_OVERLOAD                               = 61869,  ///< 过载：需打断的高伤害法术
    SPELL_LIGHTNING_WHIRL                        = 61915,  ///< 闪电旋风：向随机目标发射闪电
    SPELL_LIGHTNING_TENDRILS                     = 61887,  ///< 闪电卷须：飞行并追逐玩家的技能
    SPELL_LIGHTNING_TENDRILS_VISUAL              = 61883,  ///< 闪电卷须视觉效果：飞行时的视觉效果
    SPELL_STORMSHIELD                            = 64187   ///< 风暴护盾：第三阶段的伤害减免护盾
};

/**
 * @brief 钢铁议会战斗的事件ID枚举
 *
 * 用于首领AI的事件调度系统，管理技能冷却和特殊行为
 */
enum AssemblyEvents
{
    // General - 通用事件
    EVENT_BERSERK                                = 1,  ///< 狂暴事件：战斗超时触发暴怒

    // Steelbreaker - 钢铁破坏者的事件
    EVENT_FUSION_PUNCH                           = 2,  ///< 聚合重击事件：主要输出技能
    EVENT_STATIC_DISRUPTION                      = 3,  ///< 静电干扰事件：第二阶段随机目标技能
    EVENT_OVERWHELMING_POWER                     = 4,  ///< 压倒性力量事件：第三阶段坦克杀手技能

    // Molgeim - 符文大师莫尔基姆的事件
    EVENT_RUNE_OF_POWER                          = 5,  ///< 能量符文事件：提升伤害的符文
    EVENT_SHIELD_OF_RUNES                        = 6,  ///< 符文护盾事件：防御性护盾
    EVENT_RUNE_OF_DEATH                          = 7,  ///< 死亡符文事件：第二阶段区域伤害
    EVENT_RUNE_OF_SUMMONING                      = 8,  ///< 召唤符文事件：第三阶段召唤闪电元素
    EVENT_LIGHTNING_BLAST                        = 9,  ///< 闪电冲击事件：未使用的技能

    // Brundir - 风暴召唤者布伦迪尔的事件
    EVENT_CHAIN_LIGHTNING                        = 10, ///< 链状闪电事件：主要伤害技能
    EVENT_OVERLOAD                               = 11, ///< 过载事件：需打断的高伤害技能
    EVENT_LIGHTNING_WHIRL                        = 12, ///< 闪电旋风事件：第二阶段随机伤害
    EVENT_LIGHTNING_TENDRILS                     = 13, ///< 闪电卷须事件：第三阶段飞行技能
    EVENT_FLIGHT                                 = 14, ///< 飞行事件：飞行期间的移动控制
    EVENT_ENDFLIGHT                              = 15, ///< 结束飞行事件：结束飞行状态
    EVENT_GROUND                                 = 16, ///< 着陆事件：返回地面后的清理
    EVENT_LAND                                   = 17, ///< 降落事件：降落过程控制
    EVENT_MOVE_POSITION                          = 18  ///< 移动位置事件：战斗中的位置调整
};

/**
 * @brief 钢铁议会战斗的动作ID枚举
 *
 * 用于首领之间的通信和协调
 */
enum AssemblyActions
{
    ACTION_SUPERCHARGE                           = 1,  ///< 充能动作：首领死亡时通知其他首领进入下一阶段
    ACTION_ADD_CHARGE                            = 2   ///< 增加电荷动作：第三阶段击杀玩家时增加电荷
};

/**
 * @brief 钢铁议会战斗的台词ID枚举
 *
 * 定义三个首领的各种战斗台词索引
 */
enum AssemblyYells
{
    // Steelbreaker - 钢铁破坏者台词
    SAY_STEELBREAKER_AGGRO                      = 0,  ///< 开战台词
    SAY_STEELBREAKER_SLAY                       = 1,  ///< 击杀玩家台词
    SAY_STEELBREAKER_POWER                      = 2,  ///< 施放压倒性力量台词
    SAY_STEELBREAKER_DEATH                      = 3,  ///< 死亡台词
    SAY_STEELBREAKER_ENCOUNTER_DEFEATED         = 4,  ///< 战斗胜利台词
    SAY_STEELBREAKER_BERSERK                    = 5,  ///< 狂暴台词

    // Runemaster Molgeim - 符文大师莫尔基姆台词
    SAY_MOLGEIM_AGGRO                           = 0,  ///< 开战台词
    SAY_MOLGEIM_SLAY                            = 1,  ///< 击杀玩家台词
    SAY_MOLGEIM_RUNE_DEATH                      = 2,  ///< 施放死亡符文台词
    SAY_MOLGEIM_SUMMON                          = 3,  ///< 召唤符文台词
    SAY_MOLGEIM_DEATH                           = 4,  ///< 死亡台词
    SAY_MOLGEIM_ENCOUNTER_DEFEATED              = 5,  ///< 战斗胜利台词
    SAY_MOLGEIM_BERSERK                         = 6,  ///< 狂暴台词

    // Stormcaller Brundir - 风暴召唤者布伦迪尔台词
    SAY_BRUNDIR_AGGRO                           = 0,  ///< 开战台词
    SAY_BRUNDIR_SLAY                            = 1,  ///< 击杀玩家台词
    SAY_BRUNDIR_SPECIAL                         = 2,  ///< 特殊技能台词
    SAY_BRUNDIR_FLIGHT                          = 3,  ///< 飞行技能台词
    SAY_BRUNDIR_DEATH                           = 4,  ///< 死亡台词
    SAY_BRUNDIR_ENCOUNTER_DEFEATED              = 5,  ///< 战斗胜利台词
    SAY_BRUNDIR_BERSERK                         = 6,  ///< 狂暴台词
    EMOTE_BRUNDIR_OVERLOAD                      = 7   ///< 过载表情（用于提示玩家打断）
};

/**
 * @brief 杂项枚举定义
 *
 * 包含NPC ID和数据查询类型
 */
enum Misc
{
    NPC_WORLD_TRIGGER                            = 22515,  ///< 世界触发器NPC：用于位置计算和范围检测

    DATA_PHASE_3                                 = 1       ///< 数据查询：检查是否进入第三阶段
};

/**
 * @class boss_steelbreaker
 * @brief 钢铁破坏者首领脚本类
 *
 * 钢铁破坏者是钢铁议会三首领之一，属于坦克型首领。
 * 主要特点：
 * - 拥有高压光环，对近战攻击者造成自然伤害
 * - 使用聚合重击作为主要输出技能
 * - 随着首领死亡进入新阶段，获得更强大的技能
 */
class boss_steelbreaker : public CreatureScript
{
    public:
        boss_steelbreaker() : CreatureScript("boss_steelbreaker") { }

        /**
         * @struct boss_steelbreakerAI
         * @brief 钢铁破坏者的AI实现
         *
         * 实现钢铁破坏者的战斗逻辑，包括多阶段技能和充能机制
         */
        struct boss_steelbreakerAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_steelbreakerAI(Creature* creature) : BossAI(creature, DATA_ASSEMBLY_OF_IRON)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 在构造函数和重置时调用，将阶段重置为0
             */
            void Initialize()
            {
                phase = 0;
            }

            uint32 phase;  ///< 当前阶段（0-3），每次首领死亡时+1

            /**
             * @brief 重置首领状态
             *
             * 在战斗结束或重置时调用，清理所有光环和状态
             * 调用时机：首领脱战、重置副本、重置事件
             */
            void Reset() override
            {
                _Reset();
                Initialize();
                me->RemoveAllAuras();
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             *
             * 首领进入战斗时的初始化：
             * - 播放开战台词
             * - 施加高压光环
             * - 设置阶段为1
             * - 启动狂暴计时器（15分钟）
             * - 启动聚合重击技能循环
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_STEELBREAKER_AGGRO);
                DoCast(me, SPELL_HIGH_VOLTAGE);  // 施加被动光环
                events.SetPhase(++phase);  // 进入第一阶段
                events.ScheduleEvent(EVENT_BERSERK, 15min);  // 狂暴计时器
                events.ScheduleEvent(EVENT_FUSION_PUNCH, 15s);  // 主要技能
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 数据值
             *
             * 用于成就检查，返回是否进入第三阶段
             */
            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_PHASE_3)
                    return (phase >= 3) ? 1 : 0;  // 阶段3或更高返回1

                return 0;
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理来自其他首领的动作通知：
             * - ACTION_SUPERCHARGE: 其他首领死亡，进入新阶段
             * - ACTION_ADD_CHARGE: 第三阶段击杀玩家时增加电荷
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_SUPERCHARGE:
                        // 其他首领死亡，充能并进入下一阶段
                        me->SetFullHealth();  // 恢复满血
                        me->AddAura(SPELL_SUPERCHARGE, me);  // 添加充能光环
                        events.SetPhase(++phase);  // 阶段+1
                        events.RescheduleEvent(EVENT_FUSION_PUNCH, 15s);  // 重置技能CD

                        // 第二阶段解锁：静电干扰
                        if (phase >= 2)
                            events.RescheduleEvent(EVENT_STATIC_DISRUPTION, 30s);

                        // 第三阶段解锁：压倒性力量
                        if (phase >= 3)
                            events.RescheduleEvent(EVENT_OVERWHELMING_POWER, 2s, 5s);
                        break;
                    case ACTION_ADD_CHARGE:
                        // 第三阶段：击杀玩家时增加电荷叠加
                        DoCast(me, SPELL_ELECTRICAL_CHARGE, true);
                        break;
                }
            }

            /**
             * @brief 首领死亡
             * @param killer 击杀者
             *
             * 处理首领死亡逻辑：
             * - 如果整个战斗结束，给予击杀荣誉
             * - 否则，通知其他存活首领充能
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();

                // 检查是否整个战斗结束（所有首领死亡）
                if (instance->GetBossState(DATA_ASSEMBLY_OF_IRON) == DONE)
                {
                    DoCastAOE(SPELL_KILL_CREDIT, true);  // 给予击杀荣誉
                    Talk(SAY_STEELBREAKER_ENCOUNTER_DEFEATED);
                }
                else
                {
                    // 战斗未结束，通知其他首领
                    me->SetLootRecipient(nullptr);  // 清除战利品接收者
                    Talk(SAY_STEELBREAKER_DEATH);

                    // 通知布伦迪尔充能
                    if (Creature* Brundir = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_BRUNDIR)))
                        if (Brundir->IsAlive())
                            Brundir->AI()->DoAction(ACTION_SUPERCHARGE);

                    // 通知莫尔基姆充能
                    if (Creature* Molgeim = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_MOLGEIM)))
                        if (Molgeim->IsAlive())
                            Molgeim->AI()->DoAction(ACTION_SUPERCHARGE);
                }
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 玩家被击杀时播放台词
             * 第三阶段时额外获得电荷叠加
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_STEELBREAKER_SLAY);

                // 第三阶段：击杀玩家获得电荷增益
                if (phase == 3)
                    DoCast(me, SPELL_ELECTRICAL_CHARGE);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主循环，处理技能施放和战斗逻辑
             */
            void UpdateAI(uint32 diff) override
            {
                // 确保有有效目标
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停技能循环
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_BERSERK:
                            // 狂暴：15分钟超时
                            Talk(SAY_STEELBREAKER_BERSERK);
                            DoCast(SPELL_BERSERK);
                            events.CancelEvent(EVENT_BERSERK);  // 只触发一次
                            break;
                        case EVENT_FUSION_PUNCH:
                            // 聚合重击：主要输出技能，只在近战范围内施放
                            if (me->IsWithinMeleeRange(me->GetVictim()))
                                DoCastVictim(SPELL_FUSION_PUNCH);
                            events.ScheduleEvent(EVENT_FUSION_PUNCH, 13s, 22s);  // CD 13-22秒
                            break;
                        case EVENT_STATIC_DISRUPTION:
                            // 静电干扰：第二阶段解锁，随机目标自然伤害+沉默
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_STATIC_DISRUPTION);
                            events.ScheduleEvent(EVENT_STATIC_DISRUPTION, 20s, 40s);  // CD 20-40秒
                            break;
                        case EVENT_OVERWHELMING_POWER:
                            // 压倒性力量：第三阶段解锁，坦克杀手技能
                            Talk(SAY_STEELBREAKER_POWER);
                            DoCastVictim(SPELL_OVERWHELMING_POWER);
                            // 10人模式60秒CD，25人模式35秒CD
                            events.ScheduleEvent(EVENT_OVERWHELMING_POWER, RAID_MODE(60s, 35s));
                            break;
                    }

                    // 施法后暂停事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 执行近战攻击
                DoMeleeAttackIfReady();
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_steelbreakerAI>(creature);
        }
};

/**
 * @class boss_runemaster_molgeim
 * @brief 符文大师莫尔基姆首领脚本类
 *
 * 符文大师莫尔基姆是钢铁议会三首领之一，属于法系首领。
 * 主要特点：
 * - 召唤各种符文增强团队或削弱敌人
 * - 使用符文护盾保护自己
 * - 随着首领死亡进入新阶段，解锁更强大的符文技能
 */
class boss_runemaster_molgeim : public CreatureScript
{
    public:
        boss_runemaster_molgeim() : CreatureScript("boss_runemaster_molgeim") { }

        /**
         * @struct boss_runemaster_molgeimAI
         * @brief 符文大师莫尔基姆的AI实现
         *
         * 实现莫尔基姆的战斗逻辑，包括符文召唤和多阶段技能
         */
        struct boss_runemaster_molgeimAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_runemaster_molgeimAI(Creature* creature) : BossAI(creature, DATA_ASSEMBLY_OF_IRON)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 在构造函数和重置时调用，将阶段重置为0
             */
            void Initialize()
            {
                phase = 0;
            }

            uint32 phase;  ///< 当前阶段（0-3），每次首领死亡时+1

            /**
             * @brief 重置首领状态
             *
             * 在战斗结束或重置时调用，清理所有光环和状态
             * 调用时机：首领脱战、重置副本、重置事件
             */
            void Reset() override
            {
                _Reset();
                Initialize();
                me->RemoveAllAuras();
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             *
             * 首领进入战斗时的初始化：
             * - 播放开战台词
             * - 设置阶段为1
             * - 启动狂暴计时器（15分钟）
             * - 启动符文护盾和能量符文技能循环
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_MOLGEIM_AGGRO);
                events.SetPhase(++phase);  // 进入第一阶段
                events.ScheduleEvent(EVENT_BERSERK, 15min);  // 狂暴计时器
                events.ScheduleEvent(EVENT_SHIELD_OF_RUNES, 30s);  // 符文护盾
                events.ScheduleEvent(EVENT_RUNE_OF_POWER, 20s);  // 能量符文
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 数据值
             *
             * 用于成就检查，返回是否进入第三阶段
             */
            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_PHASE_3)
                    return (phase >= 3) ? 1 : 0;  // 阶段3或更高返回1

                return 0;
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理来自其他首领的动作通知：
             * - ACTION_SUPERCHARGE: 其他首领死亡，进入新阶段
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_SUPERCHARGE:
                    {
                        // 其他首领死亡，充能并进入下一阶段
                        me->SetFullHealth();  // 恢复满血
                        me->AddAura(SPELL_SUPERCHARGE, me);  // 添加充能光环
                        events.SetPhase(++phase);  // 阶段+1
                        events.RescheduleEvent(EVENT_SHIELD_OF_RUNES, 27s);  // 重置护盾CD
                        events.RescheduleEvent(EVENT_RUNE_OF_POWER, 25s);  // 重置符文CD

                        // 第二阶段解锁：死亡符文
                        if (phase >= 2)
                            events.RescheduleEvent(EVENT_RUNE_OF_DEATH, 30s);

                        // 第三阶段解锁：召唤符文
                        if (phase >= 3)
                            events.RescheduleEvent(EVENT_RUNE_OF_SUMMONING, 20s, 30s);
                        break;
                    }
                }
            }

            /**
             * @brief 首领死亡
             * @param killer 击杀者
             *
             * 处理首领死亡逻辑：
             * - 如果整个战斗结束，给予击杀荣誉
             * - 否则，通知其他存活首领充能
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();

                // 检查是否整个战斗结束（所有首领死亡）
                if (instance->GetBossState(DATA_ASSEMBLY_OF_IRON) == DONE)
                {
                    DoCastAOE(SPELL_KILL_CREDIT, true);  // 给予击杀荣誉
                    Talk(SAY_MOLGEIM_ENCOUNTER_DEFEATED);
                }
                else
                {
                    // 战斗未结束，通知其他首领
                    me->SetLootRecipient(nullptr);  // 清除战利品接收者
                    Talk(SAY_MOLGEIM_DEATH);

                    // 通知布伦迪尔充能
                    if (Creature* Brundir = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_BRUNDIR)))
                        if (Brundir->IsAlive())
                            Brundir->AI()->DoAction(ACTION_SUPERCHARGE);

                    // 通知钢铁破坏者充能
                    if (Creature* Steelbreaker = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STEELBREAKER)))
                        if (Steelbreaker->IsAlive())
                            Steelbreaker->AI()->DoAction(ACTION_SUPERCHARGE);
                }
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 玩家被击杀时播放台词
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_MOLGEIM_SLAY);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主循环，处理技能施放和战斗逻辑
             */
            void UpdateAI(uint32 diff) override
            {
                // 确保有有效目标
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停技能循环
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_BERSERK:
                            // 狂暴：15分钟超时
                            Talk(SAY_MOLGEIM_BERSERK);
                            DoCast(SPELL_BERSERK);
                            events.CancelEvent(EVENT_BERSERK);  // 只触发一次
                            break;
                        case EVENT_RUNE_OF_POWER:
                        {
                            // 能量符文：提升伤害的符文，优先给存活的首领
                            Unit* target = me;  // 默认给自己

                            // 随机选择一个存活的首领作为目标
                            switch (urand(0, 2))
                            {
                                case 1:
                                    if (Creature* Steelbreaker = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STEELBREAKER)))
                                        if (Steelbreaker->IsAlive())
                                            target = Steelbreaker;
                                    break;
                                case 2:
                                    if (Creature* Brundir = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STEELBREAKER)))
                                        if (Brundir->IsAlive())
                                            target = Brundir;
                                    break;
                                default:
                                    break;
                            }
                            DoCast(target, SPELL_SUMMON_RUNE_OF_POWER);
                            events.ScheduleEvent(EVENT_RUNE_OF_POWER, 1min);  // 1分钟CD
                            break;
                        }
                        case EVENT_SHIELD_OF_RUNES:
                            // 符文护盾：吸收伤害，护盾被打破后获得攻击强度增益
                            DoCast(me, SPELL_SHIELD_OF_RUNES);
                            events.ScheduleEvent(EVENT_SHIELD_OF_RUNES, 27s, 34s);  // CD 27-34秒
                            break;
                        case EVENT_RUNE_OF_DEATH:
                            // 死亡符文：第二阶段解锁，在目标位置造成持续自然伤害
                            Talk(SAY_MOLGEIM_RUNE_DEATH);
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_RUNE_OF_DEATH);
                            events.ScheduleEvent(EVENT_RUNE_OF_DEATH, 30s, 40s);  // CD 30-40秒
                            break;
                        case EVENT_RUNE_OF_SUMMONING:
                            // 召唤符文：第三阶段解锁，召唤闪电元素
                            Talk(SAY_MOLGEIM_SUMMON);
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_RUNE_OF_SUMMONING);
                            events.ScheduleEvent(EVENT_RUNE_OF_SUMMONING, 30s, 45s);  // CD 30-45秒
                            break;
                    }

                    // 施法后暂停事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 执行近战攻击
                DoMeleeAttackIfReady();
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_runemaster_molgeimAI>(creature);
        }
};

/**
 * @class boss_stormcaller_brundir
 * @brief 风暴召唤者布伦迪尔首领脚本类
 *
 * 风暴召唤者布伦迪尔是钢铁议会三首领之一，属于远程法系首领。
 * 主要特点：
 * - 使用链状闪电和过载作为主要伤害技能
 * - 可以飞行并追逐玩家（第三阶段）
 * - 战斗中会不断移动位置
 * - 随着首领死亡进入新阶段，获得飞行能力和风暴护盾
 */
class boss_stormcaller_brundir : public CreatureScript
{
    public:
        boss_stormcaller_brundir() : CreatureScript("boss_stormcaller_brundir") { }

        /**
         * @struct boss_stormcaller_brundirAI
         * @brief 风暴召唤者布伦迪尔的AI实现
         *
         * 实现布伦迪尔的战斗逻辑，包括飞行机制和多阶段技能
         */
        struct boss_stormcaller_brundirAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_stormcaller_brundirAI(Creature* creature) : BossAI(creature, DATA_ASSEMBLY_OF_IRON)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 在构造函数和重置时调用，将阶段重置为0
             */
            void Initialize()
            {
                phase = 0;
            }

            uint32 phase;  ///< 当前阶段（0-3），每次首领死亡时+1

            /**
             * @brief 重置首领状态
             *
             * 在战斗结束或重置时调用，清理所有光环和状态
             * 重置免疫状态：默认可被打断和击晕
             * 调用时机：首领脱战、重置副本、重置事件
             */
            void Reset() override
            {
                _Reset();
                Initialize();
                me->RemoveAllAuras();
                me->SetHover(false);  // 清除飞行状态
                // 重置免疫：默认可被打断（除非被过载覆盖）
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_INTERRUPT, false);
                // 重置免疫：默认可被击晕
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, false);
            }

            /**
             * @brief 获取数据
             * @param type 数据类型
             * @return 数据值
             *
             * 用于成就检查，返回是否进入第三阶段
             */
            uint32 GetData(uint32 type) const override
            {
                if (type == DATA_PHASE_3)
                    return (phase >= 3) ? 1 : 0;  // 阶段3或更高返回1

                return 0;
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             *
             * 首领进入战斗时的初始化：
             * - 播放开战台词
             * - 设置阶段为1
             * - 启动狂暴计时器（15分钟）
             * - 启动链状闪电、过载和位置移动技能循环
             * - 查找并记录房间中心的世界触发器用于位置计算
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_BRUNDIR_AGGRO);
                events.SetPhase(++phase);  // 进入第一阶段
                events.ScheduleEvent(EVENT_MOVE_POSITION, 1s);  // 位置移动
                events.ScheduleEvent(EVENT_BERSERK, 15min);  // 狂暴计时器
                events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 4s);  // 主要伤害技能
                events.ScheduleEvent(EVENT_OVERLOAD, 60s, 120s);  // 高伤害技能

                // 查找房间中心的世界触发器，用于位置范围检测
                if (Creature* trigger = me->FindNearestCreature(NPC_WORLD_TRIGGER, 100.0f))
                    m_TriggerGUID = trigger->GetGUID();
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理来自其他首领的动作通知：
             * - ACTION_SUPERCHARGE: 其他首领死亡，进入新阶段
             */
            void DoAction(int32 action) override
            {
                switch (action)
                {
                    case ACTION_SUPERCHARGE:
                    {
                        // 其他首领死亡，充能并进入下一阶段
                        me->SetFullHealth();  // 恢复满血
                        me->AddAura(SPELL_SUPERCHARGE, me);  // 添加充能光环
                        events.SetPhase(++phase);  // 阶段+1
                        events.RescheduleEvent(EVENT_CHAIN_LIGHTNING, 7s, 12s);  // 重置闪电CD
                        events.RescheduleEvent(EVENT_OVERLOAD, 40s, 50s);  // 重置过载CD

                        // 第二阶段解锁：闪电旋风
                        if (phase >= 2)
                            events.RescheduleEvent(EVENT_LIGHTNING_WHIRL, 15s, 250s);

                        // 第三阶段解锁：闪电卷须（飞行）和风暴护盾
                        if (phase >= 3)
                        {
                            DoCast(me, SPELL_STORMSHIELD);  // 施加风暴护盾
                            events.RescheduleEvent(EVENT_LIGHTNING_TENDRILS, 50s, 60s);  // 飞行技能
                            // 第三阶段免疫击晕
                            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, true);
                        }
                        break;
                    }
                }
            }

            /**
             * @brief 首领死亡
             * @param killer 击杀者
             *
             * 处理首领死亡逻辑：
             * - 如果整个战斗结束，给予击杀荣誉
             * - 否则，通知其他存活首领充能
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();

                // 检查是否整个战斗结束（所有首领死亡）
                if (instance->GetBossState(DATA_ASSEMBLY_OF_IRON) == DONE)
                {
                    DoCastAOE(SPELL_KILL_CREDIT, true);  // 给予击杀荣誉
                    Talk(SAY_BRUNDIR_ENCOUNTER_DEFEATED);
                }
                else
                {
                    // 战斗未结束，通知其他首领
                    me->SetLootRecipient(nullptr);  // 清除战利品接收者
                    Talk(SAY_BRUNDIR_DEATH);

                    // 通知莫尔基姆充能
                    if (Creature* Molgeim = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_MOLGEIM)))
                        if (Molgeim->IsAlive())
                            Molgeim->AI()->DoAction(ACTION_SUPERCHARGE);

                    // 通知钢铁破坏者充能
                    if (Creature* Steelbreaker = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_STEELBREAKER)))
                        if (Steelbreaker->IsAlive())
                            Steelbreaker->AI()->DoAction(ACTION_SUPERCHARGE);
                }
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 玩家被击杀时播放台词
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_BRUNDIR_SLAY);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 主循环，处理技能施放和战斗逻辑
             * 包括复杂的飞行状态机处理
             */
            void UpdateAI(uint32 diff) override
            {
                // 确保有有效目标
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 如果正在施法，暂停技能循环
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_BERSERK:
                            // 狂暴：15分钟超时
                            Talk(SAY_BRUNDIR_BERSERK);
                            DoCast(SPELL_BERSERK);
                            events.CancelEvent(EVENT_BERSERK);  // 只触发一次
                            break;
                        case EVENT_CHAIN_LIGHTNING:
                            // 链状闪电：主要伤害技能，随机目标并跳跃
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                DoCast(target, SPELL_CHAIN_LIGHTNING);
                            events.ScheduleEvent(EVENT_CHAIN_LIGHTNING, 7s, 10s);  // CD 7-10秒
                            break;
                        case EVENT_OVERLOAD:
                            // 过载：高伤害技能，需玩家打断
                            Talk(EMOTE_BRUNDIR_OVERLOAD);  // 表情提示玩家打断
                            Talk(SAY_BRUNDIR_SPECIAL);
                            DoCast(SPELL_OVERLOAD);
                            events.ScheduleEvent(EVENT_OVERLOAD, 60s, 120s);  // CD 60-120秒
                            break;
                        case EVENT_LIGHTNING_WHIRL:
                            // 闪电旋风：第二阶段解锁，向随机目标发射闪电
                            DoCast(SPELL_LIGHTNING_WHIRL);
                            events.ScheduleEvent(EVENT_LIGHTNING_WHIRL, 15s, 20s);  // CD 15-20秒
                            break;
                        case EVENT_LIGHTNING_TENDRILS:
                            // 闪电卷须：第三阶段解锁，飞行并追逐玩家
                            Talk(SAY_BRUNDIR_FLIGHT);
                            DoCast(me, SPELL_LIGHTNING_TENDRILS);  // 伤害光环
                            DoCast(me, SPELL_LIGHTNING_TENDRILS_VISUAL);  // 视觉效果
                            me->AttackStop();  // 停止攻击
                            me->SetHover(true);  // 进入飞行状态
                            events.DelayEvents(35s);  // 延迟其他事件
                            events.ScheduleEvent(EVENT_FLIGHT, 2500ms);  // 开始追逐
                            events.ScheduleEvent(EVENT_ENDFLIGHT, 32500ms);  // 飞行持续32.5秒
                            events.ScheduleEvent(EVENT_LIGHTNING_TENDRILS, 90s);  // 90秒后再次飞行
                            break;
                        case EVENT_FLIGHT:
                            // 飞行期间：每6秒随机追逐一个玩家
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                me->GetMotionMaster()->MovePoint(0, *target);
                            events.ScheduleEvent(EVENT_FLIGHT, 6s);  // 每6秒换一次目标
                            break;
                        case EVENT_ENDFLIGHT:
                            // 结束飞行：返回房间中心
                            me->GetMotionMaster()->Initialize();  // 重置移动控制器
                            // 移动到房间中心
                            me->GetMotionMaster()->MovePoint(0, 1586.920166f, 119.848984f, me->GetPositionZ());
                            events.CancelEvent(EVENT_FLIGHT);  // 取消追逐事件
                            events.CancelEvent(EVENT_ENDFLIGHT);  // 取消自身
                            events.ScheduleEvent(EVENT_LAND, 4s);  // 4秒后降落
                            break;
                        case EVENT_LAND:
                            // 降落：清除飞行状态
                            me->SetHover(false);
                            events.CancelEvent(EVENT_LAND);
                            events.ScheduleEvent(EVENT_GROUND, 2500ms);  // 2.5秒后着陆
                            break;
                        case EVENT_GROUND:
                            // 着陆后：清理光环和重置威胁列表
                            me->RemoveAurasDueToSpell(sSpellMgr->GetSpellIdForDifficulty(SPELL_LIGHTNING_TENDRILS, me));
                            me->RemoveAurasDueToSpell(SPELL_LIGHTNING_TENDRILS_VISUAL);
                            DoStartMovement(me->GetVictim());  // 重新追踪目标
                            events.CancelEvent(EVENT_GROUND);
                            ResetThreatList();  // 重置威胁列表
                            break;
                        case EVENT_MOVE_POSITION:
                            // 位置移动：如果在近战范围内，随机移动位置
                            if (me->IsWithinMeleeRange(me->GetVictim()))
                            {
                                // 生成随机偏移
                                float x = float(irand(-25, 25));
                                float y = float(irand(-25, 25));

                                Position pos = me->GetPosition();
                                pos.m_positionX += x;
                                pos.m_positionY += y;

                                // 防止移出房间或进入墙壁
                                if (Creature* trigger = ObjectAccessor::GetCreature(*me, m_TriggerGUID))
                                {
                                    // 如果距离中心超过50码，返回中心
                                    if (pos.GetExactDist2d(trigger) >= 50.0f)
                                        me->GetMotionMaster()->MovePoint(0, *trigger);
                                    else
                                        me->GetMotionMaster()->MovePoint(0, pos);
                                }
                            }
                            events.ScheduleEvent(EVENT_MOVE_POSITION, 7500ms, 10s);  // CD 7.5-10秒
                            break;
                        default:
                            break;
                    }

                    // 施法后暂停事件处理
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                // 执行近战攻击
                DoMeleeAttackIfReady();
            }

            private:
                ObjectGuid m_TriggerGUID;  ///< 房间中心世界触发器的GUID，用于位置范围检测
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUlduarAI<boss_stormcaller_brundirAI>(creature);
        }
};

/**
 * @class spell_shield_of_runes
 * @brief 符文护盾法术脚本
 *
 * 处理符文护盾的特殊逻辑：
 * - 护盾被击破（未自然过期）时，施放者获得攻击强度增益
 */
class spell_shield_of_runes : public SpellScriptLoader
{
    public:
        spell_shield_of_runes() : SpellScriptLoader("spell_shield_of_runes") { }

        /**
         * @class spell_shield_of_runes_AuraScript
         * @brief 符文护盾光环脚本
         */
        class spell_shield_of_runes_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_shield_of_runes_AuraScript);

            /**
             * @brief 光环移除处理
             * @param aurEff 光环效果
             * @param mode 处理模式
             *
             * 当符文护盾被移除时：
             * - 如果护盾被击破（非自然过期），给予施放者攻击强度增益
             * - 自然过期则不触发增益
             */
            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (Unit* caster = GetCaster())
                    // 检查是否被击破而非自然过期
                    if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
                        caster->CastSpell(caster, SPELL_SHIELD_OF_RUNES_BUFF, false);
            }

            void Register() override
            {
                 AfterEffectRemove += AuraEffectRemoveFn(spell_shield_of_runes_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_shield_of_runes_AuraScript();
        }
};

/**
 * @class spell_assembly_meltdown
 * @brief 融化法术脚本
 *
 * 处理第三阶段钢铁破坏者的融化技能：
 * - 玩家被融化击杀时，通知钢铁破坏者增加电荷
 */
class spell_assembly_meltdown : public SpellScriptLoader
{
    public:
        spell_assembly_meltdown() : SpellScriptLoader("spell_assembly_meltdown") { }

        /**
         * @class spell_assembly_meltdown_SpellScript
         * @brief 融化法术脚本
         */
        class spell_assembly_meltdown_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_assembly_meltdown_SpellScript);

            /**
             * @brief 处理即死效果
             * @param effIndex 效果索引
             *
             * 当玩家被融化击杀时，通知钢铁破坏者增加电荷叠加
             * 这是第三阶段的机制：钢铁破坏者击杀玩家获得增益
             */
            void HandleInstaKill(SpellEffIndex /*effIndex*/)
            {
                if (InstanceScript* instance = GetCaster()->GetInstanceScript())
                    // 获取钢铁破坏者并通知其增加电荷
                    if (Creature* Steelbreaker = ObjectAccessor::GetCreature(*GetCaster(), instance->GetGuidData(DATA_STEELBREAKER)))
                        Steelbreaker->AI()->DoAction(ACTION_ADD_CHARGE);
            }

            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_assembly_meltdown_SpellScript::HandleInstaKill, EFFECT_1, SPELL_EFFECT_INSTAKILL);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_assembly_meltdown_SpellScript();
        }
};

/**
 * @class spell_assembly_rune_of_summoning
 * @brief 召唤符文法术脚本
 *
 * 处理莫尔基姆第三阶段的召唤符文：
 * - 周期性召唤闪电元素
 * - 符文消失时自动消散
 */
class spell_assembly_rune_of_summoning : public SpellScriptLoader
{
    public:
        spell_assembly_rune_of_summoning() : SpellScriptLoader("spell_assembly_rune_of_summoning") { }

        /**
         * @class spell_assembly_rune_of_summoning_AuraScript
         * @brief 召唤符文光环脚本
         */
        class spell_assembly_rune_of_summoning_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_assembly_rune_of_summoning_AuraScript);

            /**
             * @brief 验证法术信息
             * @param spell 法术信息
             * @return 验证是否成功
             */
            bool Validate(SpellInfo const* /*spell*/) override
            {
                return ValidateSpellInfo({ SPELL_RUNE_OF_SUMMONING_SUMMON });
            }

            /**
             * @brief 处理周期性效果
             * @param aurEff 光环效果
             *
             * 每次周期触发时，召唤一个闪电元素
             */
            void HandlePeriodic(AuraEffect const* aurEff)
            {
                PreventDefaultAction();
                // 施放召唤法术，传递召唤者的GUID
                GetTarget()->CastSpell(GetTarget(), SPELL_RUNE_OF_SUMMONING_SUMMON, { aurEff, GetTarget()->IsSummon() ? GetTarget()->ToTempSummon()->GetSummonerGUID() : ObjectGuid::Empty });
            }

            /**
             * @brief 光环移除处理
             * @param aurEff 光环效果
             * @param mode 处理模式
             *
             * 符文消失时，立即消散符文生物
             */
            void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                if (TempSummon* summ = GetTarget()->ToTempSummon())
                    summ->DespawnOrUnsummon(1ms);  // 1毫秒后消散
            }

            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_assembly_rune_of_summoning_AuraScript::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
                OnEffectRemove += AuraEffectRemoveFn(spell_assembly_rune_of_summoning_AuraScript::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_assembly_rune_of_summoning_AuraScript();
        }
};

/**
 * @class achievement_assembly_i_choose_you
 * @brief 钢铁议会成就脚本："我选择了你"
 *
 * 成就要求：在第三阶段击杀钢铁破坏者（最后击杀）
 * 即让钢铁破坏者进入第三阶段后再击杀他
 */
class achievement_assembly_i_choose_you : public AchievementCriteriaScript
{
    public:
        achievement_assembly_i_choose_you() : AchievementCriteriaScript("achievement_assembly_i_choose_you") { }

        /**
         * @brief 检查成就是否满足条件
         * @param player 玩家对象
         * @param target 目标单位（钢铁破坏者）
         * @return 是否满足条件
         *
         * 检查目标是否进入第三阶段（phase >= 3）
         */
        bool OnCheck(Player* /*player*/, Unit* target) override
        {
            return target && target->GetAI()->GetData(DATA_PHASE_3);
        }
};

/**
 * @brief 注册钢铁议会战斗脚本
 *
 * 注册所有钢铁议会相关的脚本：
 * - 三个首领AI脚本
 * - 法术脚本
 * - 成就脚本
 */
void AddSC_boss_assembly_of_iron()
{
    new boss_steelbreaker();  // 钢铁破坏者
    new boss_runemaster_molgeim();  // 符文大师莫尔基姆
    new boss_stormcaller_brundir();  // 风暴召唤者布伦迪尔
    new spell_shield_of_runes();  // 符文护盾法术
    new spell_assembly_meltdown();  // 融化法术
    new spell_assembly_rune_of_summoning();  // 召唤符文法术
    new achievement_assembly_i_choose_you();  // 成就脚本
}
